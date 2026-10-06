# Port evidence and validation boundaries

## Source baseline

The counter, chunk accumulation, default constants, and recorded test data come from [Karabiner-Elements commit ec2fea8940f7254a748c052a890ee9db7113d9b1](https://github.com/pqrs-org/Karabiner-Elements/tree/ec2fea8940f7254a748c052a890ee9db7113d9b1). Upstream's [Unlicense](https://github.com/pqrs-org/Karabiner-Elements/blob/ec2fea8940f7254a748c052a890ee9db7113d9b1/LICENSE.md) is preserved in this project's LICENSE.

Relevant files are `src/share/manipulator/manipulators/mouse_motion_to_scroll/{mouse_motion_to_scroll,counter,counter_chunk_value,options}.hpp`, `src/share/core_configuration/details/profile/complex_modifications_parameters.hpp`, and `tests/src/manipulator_mouse_motion_to_scroll/`. The recorded inputs and expected outputs are independent of this C port. The fixture importer folds upstream's global speed percentage into the public speed multiplier and normalizes recorded timestamps exactly as the upstream test harness does. No C++ dispatcher, JSON library, virtual HID driver, or third-party runtime is copied into the executable.

Important preserved details: a 20 ms timer; a 100 ms accumulation window; threshold 128; a 400 ms direction history; a 100 ms emitted-scroll gap cutoff; truncation after speed multiplication; upstream's asymmetric negative rounding; and the same momentum wait/decay formulas. The manipulator resets its counter when the modifier condition fails, and removes mandatory modifiers around its generated scroll reports. It consumes pointing motion, including wheel-only reports, while active. This tool implements the single mandatory modifier with any optional modifiers, not the complete Karabiner rule language.

Only defined, representable inputs are compatibility targets. The C port rejects invalid CLI values and fails open on allocation, output, or arithmetic-range failures instead of reproducing upstream's possible integer overflow or nonfinite conversions. Timers stop when the counter is idle; a late timer performs one tick at the current monotonic time rather than inventing a catch-up burst.

## macOS

[meizure commit 6da6611c488f12b6cc51d1ad1179ae0d45b16747](https://github.com/jaybonthius/meizure/commit/6da6611c488f12b6cc51d1ad1179ae0d45b16747) supplied the reference for explicit Apple Clang/SDK selection and avoiding the generic `Point` name. Its repository was not modified.

An active session [event tap](https://developer.apple.com/documentation/coregraphics/cgeventtapcallback) filters movement and generates [line-unit wheel events](https://developer.apple.com/documentation/coregraphics/cgeventcreatescrollwheelevent). The installed SDK explicitly documents a root requirement for `kCGHIDEventTap`, so the runtime uses `kCGSessionEventTap` instead. Cursor position is restored to an anchor with `CGWarpMouseCursorPosition`, following the mechanism in [an existing small C implementation](https://github.com/emreyolcu/drag-scroll/blob/master/DragScroll/main.c); it does not disconnect the system cursor from the mouse. Own injected events carry a marker and bypass conversion. A private event source has hardware-event suppression disabled, following meizure's native setup.

Signal delivery uses system libdispatch to signal a Core Foundation shutdown source. This avoids unsafe signal-handler calls into the run loop and handles a shutdown request arriving before the loop starts. No new runtime dependency or GUI framework is required.

An initial probe reported Accessibility and Input Monitoring denied. A later native startup invocation remained running until the command timeout; after termination, no scrollkey process remained. No privacy settings were changed and no interactive mouse test was performed. Compiler/core/CLI validation does not require input access; live cursor pinning, direction/polarity, acceleration, permission attribution, sleep/wake, and interaction with other event taps remain unverified.

## Windows

The backend combines [Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input) for relative deltas with [low-level hooks](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc) for suppression. Screen-position subtraction is deliberately not used, because a pinned cursor or display edge makes it an unreliable movement source. A message-only window receives Raw Input and timer notifications. The hooks pass keyboard events and buttons through; own injected events are identified by a marker. Absolute pointing input causes a clear failure and stops filtering.

[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput) emits signed wheel ticks, with an activating-modifier up/wheel/down batch to match the upstream manipulator. Physical activation state is kept separate from those tagged synthetic key events. Microsoft documents atomic ordering within a submitted batch, existing-key-state interaction, and integrity-level restrictions. Partial injection triggers best-effort modifier restoration and stops filtering; no automatic elevation or service is added.

No Windows compiler or runtime is available in the development environment. The owner explicitly accepted implementing this backend with native validation deferred. Its compilation, Raw Input delivery while legacy movement is suppressed, injected-modifier races/side effects, elevated-application behavior, and real cursor pinning are not proven by the Mac tests. These are first-run checks, not claims of working Windows support.

## Fixture regeneration

With the pinned upstream checkout available, run:

```sh
python3 tests/import-fixtures.py /path/to/Karabiner-Elements
make test
```

`tests/fixtures.h` is bundled with this project; normal builds/tests do not invoke Python or fetch sources. Upstream has 12 recordings covering slight movement, direction locking/cancellation, continuous movement, high-speed/low-speed movement, different speed percentages, and disabled momentum. Additional public-counter tests cover the requested half-speed setting, release/reset and reactivation, invalid settings, and output/range failures.
