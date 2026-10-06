/* Direct pixel scrolling and cursor suppression/warping follow DragScroll
 * at 879bd0e6f4f1aae239364c5e7e49f19746729993.
 * Copyright (c) 2024 Emre Yolcu; MIT notice in THIRD_PARTY_NOTICES. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <dispatch/dispatch.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#include "platform.h"
#include "scroll.h"

#define INPUT_MARKER INT64_C(0x534b4559)

/* Each family has either-side, left-only and right-only bindings. */
typedef struct {
    CGKeyCode left_key, right_key;
    CGEventFlags aggregate, left_flag, right_flag;
} ModifierKeys;

static const ModifierKeys keys[] = {
    {56, 60, kCGEventFlagMaskShift, NX_DEVICELSHIFTKEYMASK, NX_DEVICERSHIFTKEYMASK},
    {59, 62, kCGEventFlagMaskControl, NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK},
    {58, 61, kCGEventFlagMaskAlternate, NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK},
    {55, 54, kCGEventFlagMaskCommand, NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK}
};

typedef struct {
    ScrollConfig config;
    ScrollMotion motion;
    CFMachPortRef tap;
    CFRunLoopRef loop;
    CGEventSourceRef wheel_source, cursor_source;
    double original_suppression;
    CGPoint anchor;
    bool active;
    int result;
} Mac;

static const ModifierKeys *binding(const Mac *mac)
{
    return &keys[(unsigned)mac->config.modifier / 3];
}

static bool side_down(CGEventFlags flags, const ModifierKeys *key, bool right)
{
    CGEventFlags sides = flags & (key->left_flag | key->right_flag);
    if (sides)
        return (sides & (right ? key->right_flag : key->left_flag)) != 0;
    /* Some sources omit device-dependent bits. Query the physical HID table,
     * not the private source used to inject our wheel events. */
    return (flags & key->aggregate) && CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,
                                                          right ? key->right_key : key->left_key);
}

static bool modifier_down(const Mac *mac, CGEventFlags flags)
{
    const ModifierKeys *key = binding(mac);
    unsigned side = (unsigned)mac->config.modifier % 3;
    if (side == 1) return side_down(flags, key, false);
    if (side == 2) return side_down(flags, key, true);
    return (flags & key->aggregate) != 0;
}

static CGEventFlags wheel_flags(const Mac *mac, CGEventFlags flags)
{
    const ModifierKeys *key = binding(mac);
    unsigned side = (unsigned)mac->config.modifier % 3;
    bool other_down = side == 1 ? side_down(flags, key, true) :
                      side == 2 ? side_down(flags, key, false) : false;
    if (side != 2) flags &= ~key->left_flag;
    if (side != 1) flags &= ~key->right_flag;
    if (!other_down) flags &= ~key->aggregate;
    return flags;
}

static bool release_pointer(Mac *mac)
{
    scroll_reset(&mac->motion);
    if (!mac->active)
        return true;
    mac->active = false;
    /* DragScroll restores with a zero suppression interval before restoring
     * the normal interval. Do this on key release, failure AND shutdown. */
    CGEventSourceSetLocalEventsSuppressionInterval(mac->cursor_source, 0.0);
    CGError result = CGWarpMouseCursorPosition(mac->anchor);
    CGEventSourceSetLocalEventsSuppressionInterval(mac->cursor_source, mac->original_suppression);
    return result == kCGErrorSuccess;
}

static void fail(Mac *mac, const char *message)
{
    fprintf(stderr, "scrollkey: %s\n", message);
    mac->result = 1;
    if (mac->tap) CGEventTapEnable(mac->tap, false);
    release_pointer(mac);
    CFRunLoopStop(mac->loop);
}

static bool update_activation(Mac *mac, CGEventFlags flags, CGEventRef event)
{
    if (!modifier_down(mac, flags)) {
        if (!release_pointer(mac)) {
            fail(mac, "cannot release the cursor anchor; input filtering has stopped");
            return false;
        }
    } else if (!mac->active) {
        if (event) {
            mac->anchor = CGEventGetLocation(event);
        } else {
            CGEventRef position = CGEventCreate(NULL);
            if (!position) {
                fail(mac, "cannot read the cursor position");
                return false;
            }
            mac->anchor = CGEventGetLocation(position);
            CFRelease(position);
        }
        mac->active = true;
        CGEventSourceSetLocalEventsSuppressionInterval(mac->cursor_source, 10.0);
        if (CGWarpMouseCursorPosition(mac->anchor) != kCGErrorSuccess) {
            fail(mac, "cannot anchor the cursor; input filtering has stopped");
            return false;
        }
    }
    return true;
}

static bool emit_scroll(Mac *mac, CGEventTapProxy proxy, CGEventFlags flags, ScrollDelta delta)
{
    if (!delta.horizontal && !delta.vertical)
        return true;
    CGEventRef event = CGEventCreateScrollWheelEvent(mac->wheel_source,
        kCGScrollEventUnitPixel, 2, delta.vertical, delta.horizontal);
    if (!event)
        return false;
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, INPUT_MARKER);
    CGEventSetFlags(event, wheel_flags(mac, flags));
    /* Insert immediately after this tap, as DragScroll does; do not queue a
     * timer or send the converted event back through our own input filter. */
    CGEventTapPostEvent(proxy, event);
    CFRelease(event);
    return true;
}

static CGEventRef on_input(CGEventTapProxy proxy, CGEventType type,
                           CGEventRef event, void *context)
{
    Mac *mac = context;
    if (mac->result)
        return event;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (!release_pointer(mac))
            fail(mac, "cannot release the cursor after the event tap was disabled");
        else if (!AXIsProcessTrusted())
            fail(mac, "Accessibility permission was lost; input filtering has stopped");
        else
            CGEventTapEnable(mac->tap, true);
        return event;
    }
    if (!event || CGEventGetIntegerValueField(event, kCGEventSourceUserData) == INPUT_MARKER)
        return event;
    bool movement = type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
                    type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged;
    if (!movement && type != kCGEventFlagsChanged)
        return event; /* Physical wheel input and buttons retain normal behavior. */
    CGEventFlags flags = CGEventGetFlags(event);
    if (!update_activation(mac, flags, event) || type == kCGEventFlagsChanged || !mac->active)
        return event; /* Never swallow normal modifier-key events. */
    int64_t x = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
    int64_t y = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
    ScrollDelta delta;
    if (x > INT_MAX || x < INT_MIN || y > INT_MAX || y < INT_MIN ||
        !scroll_input(&mac->motion, (int)x, (int)y, &delta)) {
        fail(mac, "cannot convert mouse movement; input filtering has stopped");
        return event;
    }
    if (CGWarpMouseCursorPosition(mac->anchor) != kCGErrorSuccess) {
        fail(mac, "cannot restore the cursor anchor; input filtering has stopped");
        return event;
    }
    if (!emit_scroll(mac, proxy, flags, delta)) {
        fail(mac, "cannot create the scroll event; input filtering has stopped");
        return event;
    }
    return NULL;
}

static void stop_loop(void *context) { CFRunLoopStop((CFRunLoopRef)context); }
static void release_reference(void *context) { CFRelease(context); }

static void request_stop(void *context)
{
    CFRunLoopSourceRef source = (CFRunLoopSourceRef)context;
    CFRunLoopSourceContext info = {0};
    CFRunLoopSourceGetContext(source, &info);
    /* Signaling a source also covers a signal arriving before CFRunLoopRun. */
    CFRunLoopSourceSignal(source);
    CFRunLoopWakeUp((CFRunLoopRef)info.info);
}

static dispatch_source_t watch_signal(int number, CFRunLoopSourceRef shutdown)
{
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
        (uintptr_t)number, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
    if (source) {
        CFRetain(shutdown);
        dispatch_set_context(source, shutdown);
        dispatch_source_set_event_handler_f(source, request_stop);
        dispatch_set_finalizer_f(source, release_reference);
        dispatch_resume(source);
    }
    return source;
}

int platform_run(const ScrollConfig *config)
{
    if (!AXIsProcessTrusted()) {
        fputs("scrollkey: grant Accessibility permission to this executable (or its launching\n"
              "terminal, if macOS lists that), then restart it. Open System Settings >\n"
              "Privacy & Security > Accessibility. Input Monitoring may also be required.\n", stderr);
        return 1;
    }
    Mac mac = {0};
    mac.config = *config;
    mac.loop = CFRunLoopGetCurrent();
    if (!scroll_init(&mac.motion, config->speed_multiplier)) {
        fputs("scrollkey: cannot initialize scrolling speed\n", stderr);
        return 1;
    }
    mac.wheel_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    mac.cursor_source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if (!mac.wheel_source || !mac.cursor_source) {
        if (mac.wheel_source) CFRelease(mac.wheel_source);
        if (mac.cursor_source) CFRelease(mac.cursor_source);
        fputs("scrollkey: cannot create event sources\n", stderr);
        return 1;
    }
    CGEventSourceSetLocalEventsSuppressionInterval(mac.wheel_source, 0.0);
    mac.original_suppression = CGEventSourceGetLocalEventsSuppressionInterval(mac.cursor_source);
    CGEventMask mask = CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventMouseMoved) |
        CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDragged) |
        CGEventMaskBit(kCGEventOtherMouseDragged);
    mac.tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                              kCGEventTapOptionDefault, mask, on_input, &mac);
    if (!mac.tap) {
        fprintf(stderr, "scrollkey: cannot create an active session event tap; check Accessibility\n"
                        "and Input Monitoring permissions (listen access: %s).\n",
                CGPreflightListenEventAccess() ? "allowed" : "not allowed");
        CFRelease(mac.cursor_source);
        CFRelease(mac.wheel_source);
        return 1;
    }
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, mac.tap, 0);
    CFRunLoopSourceContext shutdown_context = {0};
    shutdown_context.info = mac.loop;
    shutdown_context.retain = CFRetain;
    shutdown_context.release = CFRelease;
    shutdown_context.perform = stop_loop;
    CFRunLoopSourceRef shutdown = CFRunLoopSourceCreate(NULL, 0, &shutdown_context);
    if (!source || !shutdown) {
        if (source) CFRelease(source);
        if (shutdown) CFRelease(shutdown);
        CFRelease(mac.tap);
        CFRelease(mac.cursor_source);
        CFRelease(mac.wheel_source);
        fputs("scrollkey: cannot create the event loop\n", stderr);
        return 1;
    }
    CFRunLoopAddSource(mac.loop, source, kCFRunLoopCommonModes);
    CFRunLoopAddSource(mac.loop, shutdown, kCFRunLoopCommonModes);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    dispatch_source_t interrupt = watch_signal(SIGINT, shutdown);
    dispatch_source_t terminate = watch_signal(SIGTERM, shutdown);
    if (!interrupt || !terminate)
        fail(&mac, "cannot install shutdown handlers");
    else if (update_activation(&mac, CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState), NULL))
        CFRunLoopRun();
    if (interrupt) { dispatch_source_cancel(interrupt); dispatch_release(interrupt); }
    if (terminate) { dispatch_source_cancel(terminate); dispatch_release(terminate); }
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    CGEventTapEnable(mac.tap, false);
    if (!release_pointer(&mac)) {
        fputs("scrollkey: cannot restore the cursor position on shutdown\n", stderr);
        mac.result = 1;
    }
    CFRunLoopRemoveSource(mac.loop, source, kCFRunLoopCommonModes);
    CFRunLoopSourceInvalidate(shutdown);
    CFRunLoopRemoveSource(mac.loop, shutdown, kCFRunLoopCommonModes);
    CFMachPortInvalidate(mac.tap);
    CFRelease(shutdown);
    CFRelease(source);
    CFRelease(mac.tap);
    CFRelease(mac.cursor_source);
    CFRelease(mac.wheel_source);
    return mac.result;
}
