# scrollkey

Hold a keyboard modifier and move your mouse or trackball to scroll instead of moving the cursor. A standalone C executable for macOS and Windows; no GUI, app bundle, driver, third-party runtime libraries, or startup installer.

**Status:** the shared counter passes all 12 pinned Karabiner recordings, and the macOS executable compiles. Interactive macOS input behavior has not been verified; an initial permission probe reported missing access. The Windows backend has not been compiled or live-tested. Treat both native backends as prototypes until the checklist below passes on your machine.

## macOS

Install Apple's command-line tools if necessary, then build:

```sh
make
make test
./build/scrollkey left_shift --momentum-scroll-enabled true --speed-multiplier 0.5
```

The Makefile uses Apple's Clang and the macOS SDK through `xcrun`, respecting explicit `CC`/`CFLAGS` overrides. A stable executable path helps with permissions; rebuilding or moving it may require renewed approval.

Grant Accessibility access in **System Settings → Privacy & Security → Accessibility**. Use the `+` picker and Command–Shift–G to select the executable's full path if needed. Depending on launch context, macOS may list the launching terminal instead. Check Input Monitoring too if creating the event tap is denied, then quit and rerun. Permission granted to one launch context must not be assumed to apply to another. The program reports missing access; it does not change privacy settings or require `sudo`.

## Windows

From a Visual Studio Developer Command Prompt with C11-capable C++ Build Tools:

```bat
build-windows.cmd
build\scrollkey.exe left_shift --momentum-scroll-enabled true --speed-multiplier 0.5
```

The script builds the executable and runs the shared counter tests. There are no non-system runtime libraries beyond the normal C runtime. The backend targets relative mouse/trackball input, not absolute tablets/touch input. Windows may prevent input injection into elevated applications or secure desktops; the tool does not elevate itself or install a service.

## Options

```text
scrollkey MODIFIER [--momentum-scroll-enabled true|false] [--speed-multiplier NUMBER]
```

Both `--option value` and `--option=value` forms work. Momentum defaults to `true`; the speed multiplier defaults to `1.0` and must be finite and greater than zero. These are Karabiner's defaults; `0.5` matches the multiplier in the reference rule. `--help` shows usage; Ctrl+C stops the process.

Modifiers: `shift`, `control`, `alt`, `meta`, and their `left_`/`right_` variants. `ctrl` aliases `control`, `option` aliases `alt`, and `command`/`win` alias `meta`. Meta means Command on macOS and the Windows key on Windows. A side-specific binding does not activate from the other side. Other held modifiers are permitted, as with the reference rule's `optional: ["any"]`. Arbitrary letter keys, modifier combinations, Caps Lock, and Fn are outside this version's scope.

The counter ports Karabiner's accumulation, direction locking, integer rounding, speed scaling, and momentum decay—not a new trackpad-style smoothing algorithm. Releasing the activation modifier clears pending motion and momentum. Its inherited accumulation window can delay the first scroll by roughly 100–120 ms. Small movements can truncate to zero at low multipliers, just as upstream does. Physical wheel input is consumed while the activation condition matches, also following upstream; otherwise normal input passes through. Keyboard events and mouse buttons are not swallowed. During converted scrolling the activating modifier is removed from generated wheel input; any additional held modifier retains its normal meaning.

Startup is external: run the executable directly or arrange a per-user startup task yourself. There is no daemonization, persistence configuration, network access, telemetry, key logging, or modification of Karabiner settings. Disable your existing Karabiner mouse-to-scroll rule while testing this tool to avoid double conversion.

## Verification

```sh
make test
make test-sanitize
```

Ordinary tests need only the compiler and system shell. The CLI suite uses a test-only platform adapter, so it never captures input. Python is optional and only needed to regenerate the bundled upstream fixtures; see [port notes](docs/port.md). Sanitizers exercise the shared core, not privileged native input.

Live acceptance checklist, still outstanding on both platforms:

1. Hold **left Shift**, move vertically and horizontally, and verify scrolling with a stationary cursor. Repeat at display edges and across monitors.
2. Verify **right Shift alone** does not activate a `left_shift` binding; test both keys together and another held modifier.
3. Compare `0.5` and `1.0`, momentum enabled/disabled, and the existing Karabiner rule (separately). Release the key before the first scroll and during momentum; scrolling should stop, with no stale burst on reactivation.
4. Test typing/Shift shortcuts, clicks, dragging, and ordinary wheel input before, during, and after activation. Modifier state must not remain stuck.
5. Stop with Ctrl+C and your normal process manager; normal pointer movement must remain available. Test sleep/wake and device reconnection.

The core's recorded outputs and timestamps match upstream, but that does **not** establish identical on-screen feel. macOS uses Quartz movement events, Windows uses Raw Input, and Karabiner uses HID-level reports; OS acceleration, wheel processing, and application handling differ. macOS cursor anchoring and Windows Raw Input plus hook suppression/modifier neutralization specifically require live verification. See [implementation evidence and limits](docs/port.md).
