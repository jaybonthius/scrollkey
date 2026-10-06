#!/bin/sh
set -eu
binary=${1:?supply the CLI test executable}

expect() {
    expected=$1
    shift
    actual=$("$binary" "$@")
    if [ "$actual" != "$expected" ]; then
        printf 'CLI mismatch: expected [%s], got [%s]\n' "$expected" "$actual" >&2
        exit 1
    fi
}

invalid() {
    status=0
    "$binary" "$@" >/dev/null 2>&1 || status=$?
    if [ "$status" -ne 2 ]; then
        printf 'Expected argument error (2), got %s\n' "$status" >&2
        exit 1
    fi
}

"$binary" --help | grep -q 'Usage: scrollkey'
expect 'left_shift 1 diagonal=on diagonal_modifier=none' left_shift
expect 'right_shift 1 diagonal=on diagonal_modifier=none' right_shift
expect 'shift 1 diagonal=on diagonal_modifier=none' shift
expect 'left_shift 0.5 diagonal=on diagonal_modifier=none' left_shift --speed-multiplier 0.5
expect 'left_shift 0.5 diagonal=on diagonal_modifier=none' left_shift --speed-multiplier=0.5
expect 'right_alt 1 diagonal=on diagonal_modifier=none' right_option
expect 'left_control 1 diagonal=on diagonal_modifier=none' left_ctrl
expect 'left_meta 1 diagonal=on diagonal_modifier=none' left_command
expect 'right_meta 1 diagonal=on diagonal_modifier=none' right_win
expect 'left_shift 1 diagonal=off diagonal_modifier=left_control' left_shift --diagonal off --diagonal-modifier left_control
expect 'left_shift 1 diagonal=on diagonal_modifier=left_control' left_shift --diagonal on --diagonal-modifier left_control
expect 'left_shift 0.5 diagonal=off diagonal_modifier=right_alt' left_shift --diagonal-modifier=right_option --diagonal=off --speed-multiplier=0.5
expect 'left_shift 1 diagonal=on diagonal_modifier=right_shift' left_shift --diagonal-modifier right_shift
invalid
invalid caps_lock
invalid left_shift --unknown
invalid left_shift --diagonal
invalid left_shift --diagonal=
invalid left_shift --diagonal true
invalid left_shift --diagonal OFF
invalid left_shift --diagonal-modifier
invalid left_shift --diagonal-modifier=
invalid left_shift --diagonal-modifier caps_lock
invalid left_shift --diagonal-modifier none
invalid left_shift --diagonal-modifier left_shift
invalid shift --diagonal-modifier right_shift
invalid right_control --diagonal-modifier ctrl
invalid left_shift --speed-multiplier
invalid left_shift --speed-multiplier=
invalid left_shift --momentum-scroll-enabled
invalid left_shift --momentum-scroll-enabled=
invalid left_shift --momentum-scroll-enabled yes
invalid left_shift --momentum-scroll-enabled true
invalid left_shift --momentum-scroll-enabled=false
message=$("$binary" left_shift --momentum-scroll-enabled true 2>&1 || :)
printf '%s' "$message" | grep -q 'remove --momentum-scroll-enabled'
for value in 0 -1 nan inf 1e999 nope 0.5junk; do
    invalid left_shift --speed-multiplier "$value"
done
printf 'CLI configuration/error cases passed\n'
