# scrollkey

Hold a keyboard modifier and move your mouse or trackball to scroll instead of moving the cursor. A standalone C executable for macOS and Windows; no GUI, app bundle, driver, third-party runtime libraries, or startup installer.

Scrolling now follows **DragScroll**, not Karabiner's counter: each movement immediately produces `-3 × speed_multiplier × delta` on both axes. There is no 100 ms accumulation window, direction lock, timer, or simulated momentum. At speed `1.0`, macOS matches DragScroll's default pixel mapping. Fractional speeds retain subpixel remainders until they form a whole pixel; releasing the modifier discards that carry. A freely spinning trackball still produces scrolling for as long as it sends movement.

**Status:** macOS compilation, immediate pixel-event callback tests, modifier/release/failure tests, CLI tests, and AddressSanitizer/UndefinedBehaviorSanitizer pass. Owner live checks confirm vertical scrolling in Ghostty/Emacs and diagonal scrolling in Google Sheets. Native tests intercept posting and cursor warps; they do not inject desktop input or establish live feel. Windows has not been compiled or live-tested and cannot promise macOS-identical pixel scrolling. See [implementation evidence and limits](docs/port.md).

## macOS

Build and run:

```sh
make
make test
./build/scrollkey left_shift
```

Quit DragScroll and disable your Karabiner mouse-to-scroll rule before testing; do not run multiple converters at once. `1.0` is the speed to compare against default DragScroll, not the previous Karabiner rule's `0.5`. For half speed:

```sh
./build/scrollkey left_shift --speed-multiplier 0.5
```

The Makefile uses Apple's Clang and the macOS SDK through `xcrun`, respecting explicit `CC`/`CFLAGS` overrides. Grant Accessibility access in **System Settings → Privacy & Security → Accessibility**. Use the `+` picker and Command–Shift–G to select the executable's full path if needed. Depending on launch context, macOS may list the launching terminal instead. Check Input Monitoring too if creating the event tap is denied, then quit and rerun. Rebuilding or moving the executable may require renewed approval. The program reports missing access; it does not change privacy settings or require `sudo`.

## Windows

From a Visual Studio Developer Command Prompt with C11-capable C++ Build Tools:

```bat
build-windows.cmd
build\scrollkey.exe left_shift
```

The backend submits immediate high-resolution wheel units, not Quartz pixels. One mouse count produces three wheel units at speed `1.0`; 120 wheel units make one native notch. Windows/application scroll settings determine the resulting distance, and applications that accumulate partial notches may not move on every event. Adjust the speed multiplier on Windows rather than expecting identical distance or feel across platforms.

The script builds the executable and runs shared motion tests. There are no non-system runtime libraries beyond the normal C runtime. Only relative mice/trackballs are supported, not absolute tablets/touch input. Windows may prevent injection into elevated applications or secure desktops; the tool does not elevate itself or install a service.

## Options

```text
scrollkey MODIFIER [--speed-multiplier NUMBER]
```

Both `--speed-multiplier value` and `--speed-multiplier=value` work. The multiplier defaults to `1.0` and must be finite and greater than zero. The old `--momentum-scroll-enabled` option is removed; old commands receive an explicit error telling you to remove it. `--help` shows usage; Ctrl+C exits.

Modifiers: `shift`, `control`, `alt`, `meta`, and their `left_`/`right_` variants. `ctrl` aliases `control`, `option` aliases `alt`, and `command`/`win` alias `meta`. Meta means Command on macOS and the Windows key on Windows. A side-specific binding does not activate from the other side. Other held modifiers are permitted. Arbitrary letter keys, modifier combinations, Caps Lock, Fn, and DragScroll's mouse-button toggle are outside this version's scope.

The activating modifier is removed from generated scrolling. On macOS, marked modifier-up events precede each emitted wheel event and modifier-down events follow it, to neutralize applications such as Ghostty that cache keyboard modifiers instead of using the wheel's flags. Restoration checks the physical HID table so a released key is not re-pressed. Both pixel axes are carried in the same continuous wheel event: scrollkey has no axis lock, though an application can impose its own scrolling constraints. Additional held modifiers retain their normal meaning; with `left_shift`, holding right Shift too deliberately leaves right Shift active. Normal physical keyboard events, mouse buttons, and physical wheel input pass through. Dragged mouse movement is also converted while the activation modifier is held; release it to return to normal dragging. Releasing the modifier stops conversion immediately and resets fractional carry, with no queued momentum to continue afterward.

Startup is external: run the executable directly or arrange a per-user startup task yourself. There is no daemonization, persistence configuration, network access, telemetry, key logging, or modification of Karabiner/DragScroll settings.

## Verification

```sh
make test
make test-sanitize
```

The shared tests check immediate output, both directions/axes, fractional speed, reset, and invalid/arithmetic-range inputs. macOS tests use actual Quartz pixel-event objects and the native callback, but replace desktop posting, physical key queries, suppression updates, and cursor warps with test boundaries. They check immediate two-axis point deltas, cached keyboard modifiers at wheel delivery, up/wheel/down ordering, all modifier bindings, right-side HID fallback, additional modifier preservation, ordinary typing/input, physical release during posting, release/reactivation, and fail-open cleanup. Modifier-event allocation failures must post nothing. Tests do not require input permissions. The old Karabiner fixtures/importer remain only as historical reference and are no longer the scrolling acceptance target.

Owner live checks confirm the Ghostty/Emacs modifier fix and Google Sheets diagonal scrolling. Remaining live acceptance checks:

1. With other converters off, check cursor pinning at display edges and across monitors, and verify vertical, horizontal, and diagonal movement in additional target applications. DragScroll still clears only wheel flags, so it is not the modifier-handling reference.
2. Verify **right Shift alone** does not activate a `left_shift` binding; test both keys together and another held modifier.
3. Compare `0.5` and `1.0`. Release and reactivate the key during slow movement; there should be no stale fractional jump or delayed scrolling.
4. Test typing, Shift shortcuts, clicks, dragging, and physical wheel input before, during, and after activation. Modifier state must not remain stuck.
5. Stop with Ctrl+C and your normal process manager; normal pointer movement must remain available. Test sleep/wake and device reconnection. On Windows, test applications that handle partial wheel notches and elevated targets separately.

The DragScroll-derived code carries its MIT notice in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES); original code remains covered by [LICENSE](LICENSE).
