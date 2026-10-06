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

static CGEventRef output;
static unsigned posted, warped;
static CGError warp_error;
static CGPoint last_warp;
static double suppression;
static bool physical_keys[128];

static void capture_post(CGEventTapLocation location, CGEventRef event)
{
    (void)location;
    if (output) CFRelease(output);
    output = CGEventCreateCopy(event);
    CHECK(output);
    ++posted;
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

#define CGEventPost capture_post
#define CGEventTapPostEvent capture_tap_post
#define CGWarpMouseCursorPosition capture_warp
#define CGEventSourceSetLocalEventsSuppressionInterval capture_suppression
#define CGEventSourceKeyState key_state
#include "../src/macos.c"
#undef CGEventPost
#undef CGEventTapPostEvent
#undef CGWarpMouseCursorPosition
#undef CGEventSourceSetLocalEventsSuppressionInterval
#undef CGEventSourceKeyState

static void initialize(Mac *mac, double speed)
{
    *mac = (Mac){0};
    mac->config = (ScrollConfig){SCROLL_MOD_LEFT_SHIFT, speed};
    mac->loop = CFRunLoopGetCurrent();
    CHECK(scroll_init(&mac->motion, speed));
    mac->wheel_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    mac->cursor_source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CHECK(mac->wheel_source && mac->cursor_source);
    mac->original_suppression = 0.375; /* Test restoring a non-default value. */
    posted = warped = 0;
    warp_error = kCGErrorSuccess;
    suppression = mac->original_suppression;
    for (unsigned i = 0; i < 128; ++i) physical_keys[i] = false;
}

static void dispose(Mac *mac)
{
    if (output) { CFRelease(output); output = NULL; }
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
    CGEventSetFlags(event, both);
    CHECK(on_input(NULL, kCGEventMouseMoved, event, &mac) == NULL);
    CHECK(posted == 1);
    CGEventFlags flags = CGEventGetFlags(output);
    CHECK(flags & kCGEventFlagMaskShift);
    CHECK(flags & NX_DEVICERSHIFTKEYMASK);
    CHECK(!(flags & NX_DEVICELSHIFTKEYMASK));
    CHECK(flags & kCGEventFlagMaskControl);
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

int main(void)
{
    immediate_pixels();
    modifier_and_passthrough();
    fractional_release_and_fallback();
    fail_open();
    return 0;
}
