/* Real Quartz event construction; posting, key state and cursor warps are
 * intercepted so this test never injects input into the user's desktop. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    exit(1); \
} } while (0)

static CGEventRef output, delivered[128];
static unsigned posted, warped, delivered_count;
static CGEventFlags receiver_flags, wheel_receiver_flags;
static CGError warp_error;
static CGPoint last_warp;
static double suppression;
static bool physical_keys[128];
static int release_key_during_wheel;
static unsigned key_creations, fail_key_creation_at;

static void capture_post(CGEventTapLocation location, CGEventRef event)
{
    (void)location;
    CHECK(delivered_count < 128);
    delivered[delivered_count++] = CGEventCreateCopy(event);
    CHECK(delivered[delivered_count - 1]);
    if (CGEventGetType(event) == kCGEventFlagsChanged)
        receiver_flags = CGEventGetFlags(event);
    if (CGEventGetType(event) == kCGEventScrollWheel) {
        wheel_receiver_flags = receiver_flags;
        if (output) CFRelease(output);
        output = CGEventCreateCopy(event);
        CHECK(output);
        ++posted;
        if (release_key_during_wheel >= 0) physical_keys[release_key_during_wheel] = false;
    }
}

static void capture_tap_post(CGEventTapProxy proxy, CGEventRef event)
{
    (void)proxy;
    capture_post(kCGSessionEventTap, event);
}

static CGError capture_warp(CGPoint point)
{
    last_warp = point;
    ++warped;
    return warp_error;
}

static void capture_suppression(CGEventSourceRef source, double interval)
{
    (void)source;
    suppression = interval;
}

static bool key_state(CGEventSourceStateID state, CGKeyCode key)
{
    (void)state;
    return key < 128 && physical_keys[key];
}

static CGEventRef create_keyboard(CGEventSourceRef source, CGKeyCode code, bool down)
{
    if (++key_creations == fail_key_creation_at) return NULL;
    return CGEventCreateKeyboardEvent(source, code, down);
}

#define CGEventCreateKeyboardEvent create_keyboard
#define CGEventPost capture_post
#define CGEventTapPostEvent capture_tap_post
#define CGWarpMouseCursorPosition capture_warp
#define CGEventSourceSetLocalEventsSuppressionInterval capture_suppression
#define CGEventSourceKeyState key_state
#include "../src/macos.c"
#undef CGEventCreateKeyboardEvent
#undef CGEventPost
#undef CGEventTapPostEvent
#undef CGWarpMouseCursorPosition
#undef CGEventSourceSetLocalEventsSuppressionInterval
#undef CGEventSourceKeyState

static void initialize(Mac *mac, double speed)
{
    *mac = (Mac){0};
    mac->config = (ScrollConfig){
        .modifier = SCROLL_MOD_LEFT_SHIFT, .speed_multiplier = speed,
        .diagonal = true, .diagonal_modifier = SCROLL_MOD_NONE
    };
    mac->loop = CFRunLoopGetCurrent();
    CHECK(scroll_init(&mac->motion, speed));
    mac->wheel_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    mac->cursor_source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CHECK(mac->wheel_source && mac->cursor_source);
    mac->original_suppression = 0.375; /* Test restoring a non-default value. */
    posted = warped = delivered_count = 0;
    receiver_flags = wheel_receiver_flags = 0;
    release_key_during_wheel = -1;
    key_creations = fail_key_creation_at = 0;
    warp_error = kCGErrorSuccess;
    suppression = mac->original_suppression;
    for (unsigned i = 0; i < 128; ++i) physical_keys[i] = false;
}

static void dispose(Mac *mac)
{
    if (output) { CFRelease(output); output = NULL; }
    for (unsigned i = 0; i < delivered_count; ++i) CFRelease(delivered[i]);
    CHECK(release_pointer(mac));
    CHECK(suppression == mac->original_suppression);
    CFRelease(mac->cursor_source);
    CFRelease(mac->wheel_source);
}

static CGEventRef motion(CGEventFlags flags, int x, int y)
{
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                              CGPointMake(400, 200), kCGMouseButtonLeft);
    CHECK(event);
    CGEventSetFlags(event, flags);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, x);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, y);
    return event;
}

static void immediate_pixels(void)
{
    Mac mac;
    initialize(&mac, 1.0);
    CGEventRef event = motion(kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK, 4, -7);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    /* DragScroll emits -3 * delta on BOTH axes in this very callback. */
    CHECK(posted == 1);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventIsContinuous) == 1);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 21);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == -12);
    CHECK(!(CGEventGetFlags(output) & kCGEventFlagMaskShift));
    CHECK(warped > 0 && suppression == 10.0);
    CHECK(last_warp.x == 400 && last_warp.y == 200);
    CFRelease(event);
    dispose(&mac);
    puts("macOS immediate pixel scrolling passed (no desktop injection)");
}

static void modifier_and_passthrough(void)
{
    Mac mac;
    initialize(&mac, 1.0);
    CGEventFlags right = kCGEventFlagMaskShift | NX_DEVICERSHIFTKEYMASK;
    CGEventRef event = motion(right, 4, -7);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
    CHECK(!mac.active && !posted && !warped);
    CGEventFlags both = right | NX_DEVICELSHIFTKEYMASK | kCGEventFlagMaskControl;
    physical_keys[56] = physical_keys[60] = true;
    CGEventSetFlags(event, both);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 1);
    CGEventFlags flags = CGEventGetFlags(output);
    CHECK(flags & kCGEventFlagMaskShift);
    CHECK(flags & NX_DEVICERSHIFTKEYMASK);
    CHECK(!(flags & NX_DEVICELSHIFTKEYMASK));
    CHECK(flags & kCGEventFlagMaskControl);
    CHECK(wheel_receiver_flags == flags && receiver_flags == both);
    CHECK(CGEventGetIntegerValueField(output, kCGEventSourceUserData) == INPUT_MARKER);
    CHECK(on_input(NULL, kCGEventScrollWheel, output, &mac) == output);
    CHECK(posted == 1 && mac.active); /* No feedback or activation reset. */
    CGEventRef wheel = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, 1);
    CHECK(wheel);
    CHECK(on_input(NULL, kCGEventScrollWheel, wheel, &mac) == wheel);
    CHECK(posted == 1 && mac.active); /* Ordinary physical scrolling passes. */
    CGEventSetType(event, kCGEventLeftMouseDown);
    CHECK(on_input(NULL, kCGEventLeftMouseDown, event, &mac) == event);
    CHECK(posted == 1);
    physical_keys[56] = false;
    CGEventSetType(event, kCGEventFlagsChanged);
    CGEventSetFlags(event, right);
    CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
    CHECK(!mac.active && suppression == mac.original_suppression);
    CFRelease(wheel);
    CFRelease(event);
    dispose(&mac);
    puts("macOS side selection, modifier flags and ordinary input passed");
}

static void fractional_release_and_fallback(void)
{
    Mac mac;
    initialize(&mac, 0.5);
    physical_keys[56] = true;
    CGEventRef event = motion(kCGEventFlagMaskShift, 1, -1);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 1);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 1);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == -1);
    CHECK(!(CGEventGetFlags(output) & kCGEventFlagMaskShift));
    CGEventSetType(event, kCGEventLeftMouseDragged);
    CHECK(on_input(NULL, kCGEventLeftMouseDragged, event, &mac) == NULL);
    CHECK(posted == 2);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 2);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == -2);
    CHECK(on_input(NULL, kCGEventLeftMouseDragged, event, &mac) == NULL);
    CHECK(posted == 3); /* Leave half-pixels pending before release. */
    physical_keys[56] = false;
    CGEventSetFlags(event, 0);
    CGEventSetType(event, kCGEventFlagsChanged);
    CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
    CHECK(!mac.active && suppression == mac.original_suppression);
    CHECK(mac.motion.remainder_x == 0.0 && mac.motion.remainder_y == 0.0);
    CGEventSetType(event, kCGEventMouseMoved);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
    CHECK(posted == 3); /* No delayed or post-release scrolling. */
    physical_keys[56] = true;
    CGEventSetFlags(event, kCGEventFlagMaskShift);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 4);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 1);
    CFRelease(event);
    dispose(&mac);
    puts("macOS fractional speed, drag, release and HID key fallback passed");
}

static void fail_open(void)
{
    Mac mac;
    initialize(&mac, DBL_MAX);
    CGEventRef event = motion(kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK, 0, 0);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(mac.active && !posted);
    /* Ordinary motion at a pathological CLI speed must fail open. Quartz
     * narrows large synthetic mouse deltas, so use a real representable delta. */
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, 1);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
    CHECK(mac.result == 1 && !mac.active && !posted);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
    CHECK(!mac.active && !posted);
    CHECK(suppression == mac.original_suppression);
    CFRelease(event);
    dispose(&mac);
    initialize(&mac, 1.0);
    event = motion(kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK, 1, 1);
    warp_error = kCGErrorFailure;
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
    CHECK(mac.result == 1 && !mac.active && !posted);
    CHECK(suppression == mac.original_suppression);
    CFRelease(event);
    dispose(&mac);
    puts("macOS fail-open filtering and cursor-source restoration passed");
}

static void cached_shift_and_diagonal(void)
{
    Mac mac;
    initialize(&mac, 1.0);
    CGEventFlags held = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK;
    physical_keys[56] = true;
    receiver_flags = held; /* Ghostty keeps modifiers from keyboard events. */
    CGEventRef event = motion(held, 4, -7);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(!(wheel_receiver_flags & kCGEventFlagMaskShift));
    CHECK(delivered_count == 3 && posted == 1);
    CHECK(CGEventGetType(delivered[0]) == kCGEventFlagsChanged);
    CHECK(CGEventGetIntegerValueField(delivered[0], kCGKeyboardEventKeycode) == 56);
    CHECK(CGEventGetFlags(delivered[0]) == 0);
    CHECK(CGEventGetType(delivered[1]) == kCGEventScrollWheel);
    CHECK(CGEventGetIntegerValueField(delivered[1], kCGScrollWheelEventPointDeltaAxis1) == 21);
    CHECK(CGEventGetIntegerValueField(delivered[1], kCGScrollWheelEventPointDeltaAxis2) == -12);
    CHECK(CGEventGetType(delivered[2]) == kCGEventFlagsChanged);
    CHECK(CGEventGetFlags(delivered[2]) == held);
    CHECK(receiver_flags == held); /* Restore normal Shift behavior afterward. */
    for (unsigned i = 0; i < delivered_count; ++i) {
        CHECK(CGEventGetIntegerValueField(delivered[i], kCGEventSourceUserData) == INPUT_MARKER);
        CHECK(on_input(NULL, CGEventGetType(delivered[i]), delivered[i], &mac) == delivered[i]);
    }
    CHECK(posted == 1 && mac.active);
    CFRelease(event);
    dispose(&mac);
    puts("macOS cached-Shift neutralization and one-event diagonal scrolling passed");
}

static void physical_release_during_scroll(void)
{
    Mac mac;
    initialize(&mac, 1.0);
    CGEventFlags held = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK;
    physical_keys[56] = true;
    receiver_flags = held;
    release_key_during_wheel = 56;
    CGEventRef event = motion(held, 1, 2);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 1);
    CHECK(!(receiver_flags & kCGEventFlagMaskShift));
    CHECK(delivered_count == 2); /* Do not re-press a physically released key. */
    CGEventSetType(event, kCGEventFlagsChanged);
    CGEventSetFlags(event, 0);
    CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
    CHECK(!mac.active);
    CFRelease(event);
    dispose(&mac);
    puts("macOS physical release during scrolling cannot leave cached Shift stuck");
}

static void modifier_batch_failures_and_bindings(void)
{
    /* Allocation failure on either modifier event posts NOTHING, so the
     * receiver's keyboard state remains exactly as it was before scrolling. */
    for (unsigned failure = 1; failure <= 2; ++failure) {
        Mac mac;
        initialize(&mac, 1.0);
        CGEventFlags held = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK;
        physical_keys[56] = true;
        receiver_flags = held;
        fail_key_creation_at = failure;
        CGEventRef event = motion(held, 2, 3);
        CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
        CHECK(mac.result == 1 && !mac.active);
        CHECK(!delivered_count && !posted && receiver_flags == held);
        CHECK(suppression == mac.original_suppression);
        CFRelease(event);
        dispose(&mac);
    }
    /* Real Quartz keycodes/flags for all four supported modifier families. */
    static const struct {
        ScrollModifier modifier;
        CGKeyCode left, right;
        CGEventFlags aggregate, left_flag, right_flag;
    } cases[] = {
        {SCROLL_MOD_SHIFT, 56, 60, kCGEventFlagMaskShift, NX_DEVICELSHIFTKEYMASK, NX_DEVICERSHIFTKEYMASK},
        {SCROLL_MOD_CONTROL, 59, 62, kCGEventFlagMaskControl, NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK},
        {SCROLL_MOD_ALT, 58, 61, kCGEventFlagMaskAlternate, NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK},
        {SCROLL_MOD_META, 55, 54, kCGEventFlagMaskCommand, NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK}
    };
    for (unsigned family = 0; family < 4; ++family) {
        for (unsigned side = 0; side < 3; ++side) {
            Mac mac;
            initialize(&mac, 1.0);
            mac.config.modifier = (ScrollModifier)(cases[family].modifier + side);
            CGEventFlags held = cases[family].aggregate | cases[family].left_flag |
                                cases[family].right_flag | kCGEventFlagMaskAlphaShift;
            physical_keys[cases[family].left] = physical_keys[cases[family].right] = true;
            receiver_flags = held;
            CGEventRef event = motion(held, 2, -1);
            CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
            CHECK(posted == 1 && receiver_flags == held);
            CHECK(wheel_receiver_flags & kCGEventFlagMaskAlphaShift);
            if (side == 0) {
                CHECK(delivered_count == 5);
                CHECK(!(wheel_receiver_flags & cases[family].aggregate));
            } else {
                CHECK(delivered_count == 3);
                CHECK(wheel_receiver_flags & cases[family].aggregate);
                CHECK(wheel_receiver_flags & (side == 1 ? cases[family].right_flag : cases[family].left_flag));
                CHECK(!(wheel_receiver_flags & (side == 1 ? cases[family].left_flag : cases[family].right_flag)));
            }
            CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 3);
            CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == -6);
            for (unsigned i = 1; i < delivered_count; ++i)
                CHECK(CGEventGetTimestamp(delivered[i]) > CGEventGetTimestamp(delivered[i - 1]));
            /* A normal character key must still reach the application. */
            CGEventRef letter = CGEventCreateKeyboardEvent(NULL, 0, true);
            CHECK(letter);
            CGEventSetFlags(letter, held);
            CHECK(on_input(NULL, kCGEventKeyDown, letter, &mac) == letter);
            CHECK(posted == 1);
            CFRelease(letter);
            CFRelease(event);
            dispose(&mac);
        }
    }
    puts("macOS all modifier bindings, ordered timestamps, typing and allocation failures passed");
}

static void right_side_fallback_and_zero_motion(void)
{
    Mac mac;
    initialize(&mac, 1.0);
    mac.config.modifier = SCROLL_MOD_RIGHT_SHIFT;
    physical_keys[60] = true;
    receiver_flags = kCGEventFlagMaskShift; /* No device-dependent bits. */
    CGEventRef event = motion(kCGEventFlagMaskShift, 0, 0);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(!posted && !delivered_count && !key_creations);
    CHECK(receiver_flags == kCGEventFlagMaskShift);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, -3);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, -2);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 1 && delivered_count == 3);
    CHECK(!(wheel_receiver_flags & kCGEventFlagMaskShift));
    CHECK(CGEventGetIntegerValueField(delivered[0], kCGKeyboardEventKeycode) == 60);
    CHECK(receiver_flags == (kCGEventFlagMaskShift | NX_DEVICERSHIFTKEYMASK));
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 6);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == 9);
    CFRelease(event);
    dispose(&mac);
    puts("macOS right-side HID fallback, diagonal signs and zero-motion passthrough passed");
}

static void held_diagonal_modifier(void)
{
    for (unsigned baseline = 0; baseline < 2; ++baseline) {
        Mac mac;
        initialize(&mac, 1.0);
        mac.config.diagonal = baseline != 0;
        mac.config.diagonal_modifier = SCROLL_MOD_LEFT_CONTROL;
        physical_keys[56] = true;
        CGEventFlags shift = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK;
        CGEventFlags chord = shift | kCGEventFlagMaskControl | NX_DEVICELCTLKEYMASK;
        receiver_flags = shift;
        CGEventRef event = motion(shift, 4, -7);
        CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
        CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == (baseline ? -12 : 0));
        physical_keys[59] = true;
        receiver_flags = chord;
        CGEventSetFlags(event, chord);
        CGEventSetType(event, kCGEventFlagsChanged);
        CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
        CHECK(posted == 1 && mac.active);
        CGEventSetType(event, kCGEventMouseMoved);
        CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
        CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == 21);
        CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == (baseline ? 0 : -12));
        CHECK(!(wheel_receiver_flags & (kCGEventFlagMaskShift | kCGEventFlagMaskControl)));
        CHECK(!(CGEventGetFlags(output) & (kCGEventFlagMaskShift | kCGEventFlagMaskControl)));
        CHECK(receiver_flags == chord);
        physical_keys[59] = false;
        receiver_flags = shift;
        CGEventSetFlags(event, shift);
        CGEventSetType(event, kCGEventFlagsChanged);
        CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
        CGEventSetType(event, kCGEventMouseMoved);
        CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
        CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == (baseline ? -12 : 0));
        CHECK(posted == 3 && mac.active);
        /* The mode key on its own must not activate pointer conversion. */
        physical_keys[56] = false;
        physical_keys[59] = true;
        CGEventSetFlags(event, kCGEventFlagMaskControl | NX_DEVICELCTLKEYMASK);
        CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
        CHECK(!mac.active && posted == 3);
        CFRelease(event);
        dispose(&mac);
    }
    puts("macOS both held-key directions, cache neutralization and release-to-default passed");
}

static void diagonal_modifier_variants(void)
{
    for (unsigned family = 0; family < 4; ++family) {
        for (unsigned side = 0; side < 3; ++side) {
            Mac mac;
            initialize(&mac, 1.0);
            unsigned base_family = (family + 1) % 4;
            const ModifierKeys *base = &keys[base_family], *mode = &keys[family];
            mac.config.modifier = (ScrollModifier)(3 * base_family);
            mac.config.diagonal = false;
            mac.config.diagonal_modifier = (ScrollModifier)(3 * family + side);
            physical_keys[base->left_key] = physical_keys[base->right_key] = true;
            physical_keys[mode->left_key] = side != 2;
            physical_keys[mode->right_key] = side != 1;
            CGEventFlags base_flags = base->aggregate | base->left_flag | base->right_flag | kCGEventFlagMaskAlphaShift;
            CGEventFlags chord = base_flags | mode->aggregate;
            if (side != 2) chord |= mode->left_flag;
            if (side != 1) chord |= mode->right_flag;
            receiver_flags = chord;
            CGEventRef event = motion(chord, 4, -7);
            CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
            CHECK(posted == 1 && delivered_count == (side == 0 ? 9 : 7));
            CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == -12);
            CHECK(CGEventGetFlags(output) == kCGEventFlagMaskAlphaShift);
            CHECK(wheel_receiver_flags == kCGEventFlagMaskAlphaShift && receiver_flags == chord);
            for (unsigned i = 0; i < delivered_count; ++i)
                CHECK(on_input(NULL, CGEventGetType(delivered[i]), delivered[i], &mac) == delivered[i]);
            CHECK(posted == 1 && mac.active);
            physical_keys[mode->left_key] = physical_keys[mode->right_key] = false;
            CGEventSetFlags(event, base_flags);
            receiver_flags = base_flags;
            CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
            CHECK(posted == 2);
            CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == 0);
            CFRelease(event);
            dispose(&mac);
        }
    }
    /* Independent left/right keys in the SAME family are also valid. */
    Mac mac;
    initialize(&mac, 1.0);
    mac.config.diagonal = false;
    mac.config.diagonal_modifier = SCROLL_MOD_RIGHT_SHIFT;
    physical_keys[56] = physical_keys[60] = true;
    CGEventFlags chord = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK;
    receiver_flags = chord;
    CGEventRef event = motion(chord, 4, -7);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(delivered_count == 5 && !wheel_receiver_flags && receiver_flags == chord);
    CHECK(CGEventGetFlags(output) == 0);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == -12);
    CFRelease(event);
    dispose(&mac);
    /* Wrong-side Control must not switch modes or lose its normal meaning. */
    initialize(&mac, 1.0);
    mac.config.diagonal = false;
    mac.config.diagonal_modifier = SCROLL_MOD_LEFT_CONTROL;
    physical_keys[56] = physical_keys[62] = true;
    chord = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK | kCGEventFlagMaskControl | NX_DEVICERCTLKEYMASK;
    receiver_flags = chord;
    event = motion(chord, 4, -7);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis2) == 0);
    CHECK(wheel_receiver_flags == (kCGEventFlagMaskControl | NX_DEVICERCTLKEYMASK));
    CHECK(receiver_flags == chord);
    CFRelease(event);
    dispose(&mac);
    puts("macOS all mode-key variants, maximum nine-event chord and opposite-side semantics passed");
}

static void diagonal_modifier_lifecycle(void)
{
    /* A short press/release between mouse packets must discard old carry. */
    Mac mac;
    initialize(&mac, 0.125);
    mac.config.diagonal = false;
    mac.config.diagonal_modifier = SCROLL_MOD_LEFT_CONTROL;
    physical_keys[56] = true;
    CGEventFlags shift = kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK;
    CGEventFlags chord = shift | kCGEventFlagMaskControl | NX_DEVICELCTLKEYMASK;
    CGEventRef event = motion(shift, 1, 1);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CGEventSetType(event, kCGEventFlagsChanged);
    physical_keys[59] = true;
    CGEventSetFlags(event, chord);
    CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
    physical_keys[59] = false;
    CGEventSetFlags(event, shift);
    CHECK(on_input(NULL, kCGEventFlagsChanged, event, &mac) == event);
    CGEventSetType(event, kCGEventMouseMoved);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(!posted && !delivered_count);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 1);
    CHECK(CGEventGetIntegerValueField(output, kCGScrollWheelEventPointDeltaAxis1) == -1);
    CFRelease(event);
    dispose(&mac);
    /* The mode modifier can physically release during the posted wheel. */
    initialize(&mac, 1.0);
    mac.config.diagonal = false;
    mac.config.diagonal_modifier = SCROLL_MOD_LEFT_CONTROL;
    physical_keys[56] = physical_keys[59] = true;
    receiver_flags = chord;
    release_key_during_wheel = 59;
    event = motion(chord, 4, -7);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(delivered_count == 4 && receiver_flags == shift);
    CHECK(!wheel_receiver_flags && !(CGEventGetFlags(output) & kCGEventFlagMaskControl));
    CFRelease(event);
    dispose(&mac);
    /* Any constructor failure in a maximum-size batch posts nothing. */
    for (unsigned failure = 1; failure <= 8; ++failure) {
        initialize(&mac, 1.0);
        mac.config.modifier = SCROLL_MOD_SHIFT;
        mac.config.diagonal_modifier = SCROLL_MOD_CONTROL;
        physical_keys[56] = physical_keys[60] = physical_keys[59] = physical_keys[62] = true;
        chord = shift | NX_DEVICERSHIFTKEYMASK | kCGEventFlagMaskControl | NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK;
        receiver_flags = chord;
        fail_key_creation_at = failure;
        event = motion(chord, 4, -7);
        CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == event);
        CHECK(mac.result == 1 && !mac.active && !delivered_count && receiver_flags == chord);
        CFRelease(event);
        dispose(&mac);
    }
    puts("macOS mode-change carry, physical mode-key release and complete-batch failure cleanup passed");
}

int main(void)
{
    diagonal_modifier_lifecycle();
    diagonal_modifier_variants();
    held_diagonal_modifier();
    right_side_fallback_and_zero_motion();
    modifier_batch_failures_and_bindings();
    physical_release_during_scroll();
    cached_shift_and_diagonal();
    immediate_pixels();
    modifier_and_passthrough();
    fractional_release_and_fallback();
    fail_open();
    return 0;
}
