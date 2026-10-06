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

static const UINT key_pairs[][2] = {
    {VK_LSHIFT, VK_RSHIFT}, {VK_LCONTROL, VK_RCONTROL},
    {VK_LMENU, VK_RMENU}, {VK_LWIN, VK_RWIN}
};

typedef struct {
    ScrollConfig config;
    ScrollMotion motion;
    HWND window;
    HHOOK mouse_hook, keyboard_hook;
    unsigned down; /* Physical modifier-key bits; marked injection never updates them. */
    bool active;
    int result;
} Windows;

/* Low-level callbacks execute on the thread installing the hooks. */
static Windows *current;
/* Written before registration; the console handler never touches stack state. */
static DWORD console_thread;

static bool modifier_down(const Windows *windows, ScrollModifier modifier)
{
    return (windows->down & scroll_modifier_mask(modifier)) != 0;
}

static void update_activation(Windows *windows)
{
    bool diagonal = windows->config.diagonal;
    if (modifier_down(windows, windows->config.diagonal_modifier)) diagonal = !diagonal;
    scroll_set_diagonal(&windows->motion, diagonal);
    windows->active = modifier_down(windows, windows->config.modifier);
    if (!windows->active)
        scroll_reset(&windows->motion);
}

static void fail(Windows *windows, const char *message)
{
    fprintf(stderr, "scrollkey: %s (Windows error %lu)\n", message, GetLastError());
    windows->result = 1;
    windows->active = false;
    scroll_reset(&windows->motion);
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

static bool emit_scroll(Windows *windows, ScrollDelta delta)
{
    if (!windows->active || (!delta.horizontal && !delta.vertical))
        return true;
    if (delta.horizontal == INT_MIN)
        return false;
    /* SendInput has no pixel-scroll API. Submit fine wheel units immediately,
     * NOT a full 120-unit notch per pixel. Applications choose their own
     * handling of partial notches; this is not identical to Quartz pixels. */
    LONG horizontal = -(LONG)delta.horizontal;
    LONG vertical = (LONG)delta.vertical;
    /* Two bindings select at most four keys, plus two wheel events. */
    INPUT events[10] = {0};
    UINT count = 0, released[4], release_count = 0;
    unsigned selected = scroll_output_modifiers(&windows->config);
    for (unsigned i = 0; i < 8; ++i) {
        if (!(selected & windows->down & (1u << i))) continue;
        UINT key = key_pairs[i / 2][i % 2];
        if (!key_input(&events[count++], key, true))
            return false;
        released[release_count++] = key;
    }
    if (vertical) {
        events[count].type = INPUT_MOUSE;
        events[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        events[count].mi.mouseData = (DWORD)vertical;
        events[count++].mi.dwExtraInfo = INPUT_MARKER;
    }
    if (horizontal) {
        events[count].type = INPUT_MOUSE;
        events[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        events[count].mi.mouseData = (DWORD)horizontal;
        events[count++].mi.dwExtraInfo = INPUT_MARKER;
    }
    for (UINT i = 0; i < release_count; ++i)
        if (!key_input(&events[count++], released[i], false))
            return false;
    /* SendInput submits the up/wheel/down batch without interleaving other
     * input. Our own key events must not alter physical activation state. */
    UINT sent = SendInput(count, events, sizeof(INPUT));
    if (sent != count) {
        /* Best-effort restore after a partial submission, then stop filtering.
         * No elevation attempt: UIPI can prevent injection into elevated apps. */
        INPUT restore[4] = {0};
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
            for (unsigned i = 0; i < 8; ++i)
                if (key == key_pairs[i / 2][i % 2]) {
                    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                        windows->down |= 1u << i;
                    else windows->down &= ~(1u << i);
                    update_activation(windows);
                    break;
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
            message == WM_MOUSEMOVE)
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
    if (!x && !y)
        return; /* Wheel and button-only packets are not converted. */
    ScrollDelta delta;
    if (!scroll_input(&windows->motion, x, y, &delta)) {
        fail(windows, "cannot convert movement; input filtering has stopped");
        return;
    }
    if (!emit_scroll(windows, delta))
        fail(windows, "scroll injection failed (check speed and target app privileges)");
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
    if (!scroll_init(&windows.motion, config->speed_multiplier)) {
        fputs("scrollkey: cannot initialize scrolling speed\n", stderr);
        return 1;
    }
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"ScrollkeyInput";
    if (!RegisterClassW(&window_class)) {
        fputs("scrollkey: cannot register the input window\n", stderr);
        return 1;
    }
    windows.window = CreateWindowExW(0, window_class.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, instance, &windows);
    if (!windows.window) {
        UnregisterClassW(window_class.lpszClassName, instance);
        fputs("scrollkey: cannot create the input window\n", stderr);
        return 1;
    }
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_INPUTSINK, windows.window};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        DestroyWindow(windows.window);
        UnregisterClassW(window_class.lpszClassName, instance);
        fputs("scrollkey: cannot register raw mouse input\n", stderr);
        return 1;
    }
    for (unsigned i = 0; i < 8; ++i)
        if (GetAsyncKeyState((int)key_pairs[i / 2][i % 2]) & 0x8000)
            windows.down |= 1u << i;
    update_activation(&windows);
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
    scroll_reset(&windows.motion);
    if (windows.mouse_hook) UnhookWindowsHookEx(windows.mouse_hook);
    if (windows.keyboard_hook) UnhookWindowsHookEx(windows.keyboard_hook);
    current = NULL;
    if (console_installed) SetConsoleCtrlHandler(console_control, FALSE);
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = NULL;
    RegisterRawInputDevices(&device, 1, sizeof(device));
    DestroyWindow(windows.window);
    UnregisterClassW(window_class.lpszClassName, instance);
    return windows.result;
}
