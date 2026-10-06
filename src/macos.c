#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>

#include "platform.h"
#include "scroll.h"

#define INPUT_MARKER INT64_C(0x534b4559)
#define IDLE_SECONDS (86400.0 * 365.0)

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
    ScrollCounter *counter;
    CFMachPortRef tap;
    CFRunLoopTimerRef timer;
    CFRunLoopRef loop;
    CGEventSourceRef wheel_source;
    CGPoint anchor;
    bool active;
    int result;
    mach_timebase_info_data_t clock_scale;
} Mac;

static int64_t now_ms(const Mac *mac)
{
    long double ticks = mach_absolute_time();
    return (int64_t)(ticks * mac->clock_scale.numer / mac->clock_scale.denom / 1000000.0L);
}

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

static void fail(Mac *mac, const char *message)
{
    fprintf(stderr, "scrollkey: %s\n", message);
    mac->result = 1;
    mac->active = false;
    scroll_reset(mac->counter);
    if (mac->tap) CGEventTapEnable(mac->tap, false);
    CFRunLoopStop(mac->loop);
}

static bool update_activation(Mac *mac, CGEventFlags flags)
{
    bool active = modifier_down(mac, flags);
    if (!active) {
        mac->active = false;
        scroll_reset(mac->counter);
        return true;
    }
    if (!mac->active) {
        CGEventRef position = CGEventCreate(NULL);
        if (!position) {
            fail(mac, "cannot read the cursor position");
            return false;
        }
        mac->anchor = CGEventGetLocation(position);
        CFRelease(position);
        mac->active = true;
    }
    return true;
}

static void schedule(Mac *mac)
{
    int64_t deadline = scroll_deadline(mac->counter);
    double delay = deadline < 0 ? IDLE_SECONDS : (double)(deadline - now_ms(mac)) / 1000.0;
    if (delay < 0.0) delay = 0.0;
    CFRunLoopTimerSetNextFireDate(mac->timer, CFAbsoluteTimeGetCurrent() + delay);
}

static bool emit_scroll(void *context, int64_t time_ms, int horizontal, int vertical)
{
    (void)time_ms;
    Mac *mac = context;
    CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
    /* The timer also checks this before entering the counter. */
    if (!modifier_down(mac, flags))
        return true;
    CGEventRef event = CGEventCreateScrollWheelEvent(mac->wheel_source,
        kCGScrollEventUnitLine, 2, vertical, horizontal);
    if (!event)
        return false;
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, INPUT_MARKER);
    CGEventSetFlags(event, wheel_flags(mac, flags));
    CGEventPost(kCGSessionEventTap, event);
    CFRelease(event);
    return true;
}

static void timer_tick(CFRunLoopTimerRef timer, void *context)
{
    (void)timer;
    Mac *mac = context;
    if (!update_activation(mac, CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState)))
        return;
    if (!scroll_tick(mac->counter, now_ms(mac), emit_scroll, mac)) {
        fail(mac, "scroll counter/output failed (check the speed multiplier)");
        return;
    }
    schedule(mac);
}

static CGEventRef on_input(CGEventTapProxy proxy, CGEventType type,
                           CGEventRef event, void *context)
{
    (void)proxy;
    Mac *mac = context;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        mac->active = false;
        scroll_reset(mac->counter);
        schedule(mac);
        if (!AXIsProcessTrusted())
            fail(mac, "Accessibility permission was lost; input filtering has stopped");
        else
            CGEventTapEnable(mac->tap, true);
        return event;
    }
    if (!event || CGEventGetIntegerValueField(event, kCGEventSourceUserData) == INPUT_MARKER)
        return event;
    if (!update_activation(mac, CGEventGetFlags(event)))
        return event;
    if (type == kCGEventFlagsChanged) {
        schedule(mac);
        return event; /* Never swallow normal modifier-key events. */
    }
    if (!mac->active)
        return event;
    bool movement = type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
                    type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged;
    if (movement || type == kCGEventScrollWheel) {
        int64_t x = movement ? CGEventGetIntegerValueField(event, kCGMouseEventDeltaX) : 0;
        int64_t y = movement ? CGEventGetIntegerValueField(event, kCGMouseEventDeltaY) : 0;
        if (x > INT_MAX || x < INT_MIN || y > INT_MAX || y < INT_MIN ||
            !scroll_input(mac->counter, (int)x, (int)y, now_ms(mac))) {
            fail(mac, "cannot queue mouse movement; input filtering has stopped");
            return event;
        }
        if (movement && CGWarpMouseCursorPosition(mac->anchor) != kCGErrorSuccess) {
            fail(mac, "cannot restore the cursor anchor; input filtering has stopped");
            return event;
        }
        schedule(mac);
        /* Karabiner also consumes physical wheel motion while its modifier
         * condition matches; wheel-only input contributes x=y=0. */
        return NULL;
    }
    return event;
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
    mac.counter = scroll_create(config->speed_multiplier, config->momentum_scroll_enabled);
    if (!mac.counter || mach_timebase_info(&mac.clock_scale) != KERN_SUCCESS) {
        scroll_destroy(mac.counter);
        fputs("scrollkey: cannot initialize the scroll counter/clock\n", stderr);
        return 1;
    }
    mac.wheel_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!mac.wheel_source) {
        scroll_destroy(mac.counter);
        fputs("scrollkey: cannot create an event source\n", stderr);
        return 1;
    }
    CGEventSourceSetLocalEventsSuppressionInterval(mac.wheel_source, 0.0);
    CGEventMask mask = CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventMouseMoved) |
        CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDragged) |
        CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventScrollWheel);
    /* A session tap avoids the SDK's root requirement for a HID tap. */
    mac.tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                              kCGEventTapOptionDefault, mask, on_input, &mac);
    if (!mac.tap) {
        fprintf(stderr, "scrollkey: cannot create an active session event tap; check Accessibility\n"
                        "and Input Monitoring permissions (listen access: %s).\n",
                CGPreflightListenEventAccess() ? "allowed" : "not allowed");
        CFRelease(mac.wheel_source);
        scroll_destroy(mac.counter);
        return 1;
    }
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL, mac.tap, 0);
    CFRunLoopTimerContext timer_context = {0, &mac, NULL, NULL, NULL};
    mac.timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + IDLE_SECONDS,
                                   0.02, 0, 0, timer_tick, &timer_context);
    CFRunLoopSourceContext shutdown_context = {0};
    shutdown_context.info = mac.loop;
    shutdown_context.retain = CFRetain;
    shutdown_context.release = CFRelease;
    shutdown_context.perform = stop_loop;
    CFRunLoopSourceRef shutdown = CFRunLoopSourceCreate(NULL, 0, &shutdown_context);
    if (!source || !mac.timer || !shutdown) {
        if (source) CFRelease(source);
        if (shutdown) CFRelease(shutdown);
        if (mac.timer) CFRelease(mac.timer);
        CFRelease(mac.tap);
        CFRelease(mac.wheel_source);
        scroll_destroy(mac.counter);
        fputs("scrollkey: cannot create the event loop\n", stderr);
        return 1;
    }
    CFRunLoopAddSource(mac.loop, source, kCFRunLoopCommonModes);
    CFRunLoopAddSource(mac.loop, shutdown, kCFRunLoopCommonModes);
    CFRunLoopAddTimer(mac.loop, mac.timer, kCFRunLoopCommonModes);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    dispatch_source_t interrupt = watch_signal(SIGINT, shutdown);
    dispatch_source_t terminate = watch_signal(SIGTERM, shutdown);
    if (!interrupt || !terminate)
        fail(&mac, "cannot install shutdown handlers");
    else if (update_activation(&mac, CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState)))
        CFRunLoopRun();
    if (interrupt) { dispatch_source_cancel(interrupt); dispatch_release(interrupt); }
    if (terminate) { dispatch_source_cancel(terminate); dispatch_release(terminate); }
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    CGEventTapEnable(mac.tap, false);
    CFRunLoopTimerInvalidate(mac.timer);
    CFRunLoopRemoveTimer(mac.loop, mac.timer, kCFRunLoopCommonModes);
    CFRunLoopRemoveSource(mac.loop, source, kCFRunLoopCommonModes);
    CFRunLoopSourceInvalidate(shutdown);
    CFRunLoopRemoveSource(mac.loop, shutdown, kCFRunLoopCommonModes);
    CFMachPortInvalidate(mac.tap);
    CFRelease(shutdown);
    CFRelease(mac.timer);
    CFRelease(source);
    CFRelease(mac.tap);
    CFRelease(mac.wheel_source);
    scroll_destroy(mac.counter);
    return mac.result;
}
