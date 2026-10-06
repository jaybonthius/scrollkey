#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "scroll.h"

#define INPUT_MARKER ((ULONG_PTR)0x534b4559)
#define TIMER_ID 1

static const UINT key_pairs[][2] = {
    {VK_LSHIFT, VK_RSHIFT}, {VK_LCONTROL, VK_RCONTROL},
    {VK_LMENU, VK_RMENU}, {VK_LWIN, VK_RWIN}
};

typedef struct {
    ScrollConfig config;
    ScrollCounter *counter;
    HWND window;
    HHOOK mouse_hook, keyboard_hook;
    bool down[2], active, timer_running;
    int result;
} Windows;

/* Low-level callbacks execute on the thread installing the hooks. */
static Windows *current;
/* Written before registration; the console handler never touches stack state. */
static DWORD console_thread;

static int64_t now_ms(void) { return (int64_t)GetTickCount64(); }

static bool modifier_down(const Windows *windows)
{
    unsigned side = (unsigned)windows->config.modifier % 3;
    return side == 1 ? windows->down[0] : side == 2 ? windows->down[1] :
           windows->down[0] || windows->down[1];
}

static void stop_timer(Windows *windows)
{
    if (windows->timer_running) {
        KillTimer(windows->window, TIMER_ID);
        windows->timer_running = false;
    }
}

static void update_activation(Windows *windows)
{
    windows->active = modifier_down(windows);
    if (!windows->active) {
        scroll_reset(windows->counter);
        stop_timer(windows);
    }
}

static void fail(Windows *windows, const char *message)
{
    fprintf(stderr, "scrollkey: %s (Windows error %lu)\n", message, GetLastError());
    windows->result = 1;
    windows->active = false;
    scroll_reset(windows->counter);
    stop_timer(windows);
    PostQuitMessage(1);
}

static bool key_input(INPUT *input, UINT virtual_key, bool up)
{
    UINT scan = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC_EX);
    if (!scan)
        return false;
    memset(input, 0, sizeof(*input));
    input->type = INPUT_KEYBOARD;
    input->ki.wScan = (WORD)(scan & 0xff);
    input->ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    if (scan & 0xff00) input->ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    input->ki.dwExtraInfo = INPUT_MARKER;
    return true;
}

static bool emit_scroll(void *context, int64_t time_ms, int horizontal, int vertical)
{
    (void)time_ms;
    Windows *windows = context;
    if (!windows->active)
        return true;
    if (horizontal > LONG_MAX / WHEEL_DELTA || horizontal < LONG_MIN / WHEEL_DELTA ||
        vertical > LONG_MAX / WHEEL_DELTA || vertical < LONG_MIN / WHEEL_DELTA)
        return false;
    INPUT events[6] = {0};
    UINT count = 0, released[2], release_count = 0;
    unsigned family = (unsigned)windows->config.modifier / 3;
    unsigned side = (unsigned)windows->config.modifier % 3;
    for (unsigned i = 0; i < 2; ++i) {
        if (!windows->down[i] || (side == 1 && i != 0) || (side == 2 && i != 1))
            continue;
        UINT key = key_pairs[family][i];
        if (!key_input(&events[count++], key, true))
            return false;
        released[release_count++] = key;
    }
    if (vertical) {
        events[count].type = INPUT_MOUSE;
        events[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        events[count].mi.mouseData = (DWORD)(LONG)(vertical * WHEEL_DELTA);
        events[count++].mi.dwExtraInfo = INPUT_MARKER;
    }
    if (horizontal) {
        events[count].type = INPUT_MOUSE;
        events[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        events[count].mi.mouseData = (DWORD)(LONG)(horizontal * WHEEL_DELTA);
        events[count++].mi.dwExtraInfo = INPUT_MARKER;
    }
    for (UINT i = 0; i < release_count; ++i)
        if (!key_input(&events[count++], released[i], false))
            return false;
    /* SendInput submits the up/wheel/down batch without interleaving other
     * input. This mirrors Karabiner's modifier neutralization. Our own key
     * events must not alter the physical activation state in key_hook. */
    UINT sent = SendInput(count, events, sizeof(INPUT));
    if (sent != count) {
        /* Best-effort restore after a partial submission, then stop filtering.
         * No elevation attempt: UIPI can prevent injection into elevated apps. */
        INPUT restore[2] = {0};
        for (UINT i = 0; i < release_count; ++i)
            key_input(&restore[i], released[i], false);
        if (release_count) SendInput(release_count, restore, sizeof(INPUT));
        return false;
    }
    return true;
}

static LRESULT CALLBACK key_hook(int code, WPARAM message, LPARAM data)
{
    Windows *windows = current;
    if (code == HC_ACTION && windows && !windows->result) {
        const KBDLLHOOKSTRUCT *event = (const KBDLLHOOKSTRUCT *)data;
        if (event->dwExtraInfo != INPUT_MARKER) {
            UINT key = event->vkCode;
            if (key == VK_SHIFT) key = event->scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
            if (key == VK_CONTROL) key = event->flags & LLKHF_EXTENDED ? VK_RCONTROL : VK_LCONTROL;
            if (key == VK_MENU) key = event->flags & LLKHF_EXTENDED ? VK_RMENU : VK_LMENU;
            unsigned family = (unsigned)windows->config.modifier / 3;
            for (unsigned i = 0; i < 2; ++i)
                if (key == key_pairs[family][i]) {
                    windows->down[i] = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                    update_activation(windows);
                }
        }
    }
    return CallNextHookEx(NULL, code, message, data);
}

static LRESULT CALLBACK mouse_hook(int code, WPARAM message, LPARAM data)
{
    Windows *windows = current;
    if (code == HC_ACTION && windows && windows->active && !windows->result) {
        const MSLLHOOKSTRUCT *event = (const MSLLHOOKSTRUCT *)data;
        if (event->dwExtraInfo != INPUT_MARKER &&
            (message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL))
            return 1;
    }
    return CallNextHookEx(NULL, code, message, data);
}

static void raw_input(Windows *windows, HRAWINPUT handle)
{
    RAWINPUT input;
    UINT size = sizeof(input);
    UINT received = GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
    if (received == (UINT)-1 || received < sizeof(RAWINPUTHEADER)) {
        fail(windows, "cannot read raw mouse input");
        return;
    }
    if (!windows->active || windows->result || input.header.dwType != RIM_TYPEMOUSE)
        return;
    if (received < offsetof(RAWINPUT, data) + sizeof(RAWMOUSE)) {
        fail(windows, "truncated raw mouse input");
        return;
    }
    if (input.data.mouse.ulExtraInformation == (DWORD)INPUT_MARKER)
        return;
    if (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
        fail(windows, "absolute pointing devices are not supported; input filtering has stopped");
        return;
    }
    int x = input.data.mouse.lLastX, y = input.data.mouse.lLastY;
    if (!x && !y && !(input.data.mouse.usButtonFlags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL)))
        return; /* A button-only packet is not Karabiner pointing motion. */
    if (!scroll_input(windows->counter, x, y, now_ms())) {
        fail(windows, "cannot queue movement; input filtering has stopped");
        return;
    }
    if (!windows->timer_running) {
        if (!SetTimer(windows->window, TIMER_ID, 20, NULL))
            fail(windows, "cannot start the scrolling timer");
        else
            windows->timer_running = true;
    }
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Windows *windows = (Windows *)(uintptr_t)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        windows = create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)windows);
    }
    if (windows && message == WM_INPUT) {
        raw_input(windows, (HRAWINPUT)lparam);
        /* DefWindowProc is required to release foreground WM_INPUT resources. */
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (windows && message == WM_TIMER && wparam == TIMER_ID) {
        if (!windows->result && !scroll_tick(windows->counter, now_ms(), emit_scroll, windows))
            fail(windows, "scroll counter/injection failed (check speed and target app privileges)");
        if (scroll_deadline(windows->counter) < 0)
            stop_timer(windows);
        return 0;
    }
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL WINAPI console_control(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        PostThreadMessageW(console_thread, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}

int platform_run(const ScrollConfig *config)
{
    Windows windows = {0};
    windows.config = *config;
    windows.counter = scroll_create(config->speed_multiplier, config->momentum_scroll_enabled);
    if (!windows.counter) {
        fputs("scrollkey: cannot create the scroll counter\n", stderr);
        return 1;
    }
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"ScrollkeyInput";
    if (!RegisterClassW(&window_class)) {
        scroll_destroy(windows.counter);
        fputs("scrollkey: cannot register the input window\n", stderr);
        return 1;
    }
    windows.window = CreateWindowExW(0, window_class.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, instance, &windows);
    if (!windows.window) {
        UnregisterClassW(window_class.lpszClassName, instance);
        scroll_destroy(windows.counter);
        fputs("scrollkey: cannot create the input window\n", stderr);
        return 1;
    }
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_INPUTSINK, windows.window};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        DestroyWindow(windows.window);
        UnregisterClassW(window_class.lpszClassName, instance);
        scroll_destroy(windows.counter);
        fputs("scrollkey: cannot register raw mouse input\n", stderr);
        return 1;
    }
    unsigned family = (unsigned)config->modifier / 3;
    for (unsigned i = 0; i < 2; ++i)
        windows.down[i] = (GetAsyncKeyState((int)key_pairs[family][i]) & 0x8000) != 0;
    windows.active = modifier_down(&windows);
    current = &windows;
    windows.keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, key_hook, instance, 0);
    windows.mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, instance, 0);
    console_thread = GetCurrentThreadId();
    bool console_installed = SetConsoleCtrlHandler(console_control, TRUE) != 0;
    if (!windows.keyboard_hook || !windows.mouse_hook || !console_installed)
        fail(&windows, "cannot install input/shutdown hooks");
    else {
        MSG message;
        BOOL received;
        while ((received = GetMessageW(&message, NULL, 0, 0)) != 0) {
            if (received == -1) {
                fail(&windows, "input message loop failed");
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    windows.active = false;
    stop_timer(&windows);
    if (windows.mouse_hook) UnhookWindowsHookEx(windows.mouse_hook);
    if (windows.keyboard_hook) UnhookWindowsHookEx(windows.keyboard_hook);
    current = NULL;
    if (console_installed) SetConsoleCtrlHandler(console_control, FALSE);
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = NULL;
    RegisterRawInputDevices(&device, 1, sizeof(device));
    DestroyWindow(windows.window);
    UnregisterClassW(window_class.lpszClassName, instance);
    scroll_destroy(windows.counter);
    return windows.result;
}
