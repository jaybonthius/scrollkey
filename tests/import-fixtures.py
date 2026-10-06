#!/usr/bin/env python3
"""Translate the pinned upstream numeric JSONC recordings into C test data.

Usage: python3 tests/import-fixtures.py /path/to/Karabiner-Elements
Only needed to regenerate fixtures.h; ordinary builds/tests do not need Python.
"""
import json
from pathlib import Path
import re
import subprocess
import sys

PIN = "ec2fea8940f7254a748c052a890ee9db7113d9b1"
root = Path(sys.argv[1])
head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
if head != PIN:
    raise SystemExit(f"Expected upstream {PIN}, found {head}")
source = root / "tests/src/manipulator_mouse_motion_to_scroll/json"


def load(path):
    # These numeric recordings contain no comment-like text in strings.
    return json.loads(re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(), flags=re.S))


lines = [
    f"/* Generated from Karabiner-Elements {PIN}; see LICENSE. */",
    "#ifndef SCROLLKEY_FIXTURES_H",
    "#define SCROLLKEY_FIXTURES_H",
    "typedef struct { int x, y; int64_t time_ms; } FixtureMotion;",
    "typedef struct { int64_t time_ms; int horizontal, vertical; } FixtureOutput;",
    "typedef struct {",
    "    const char *name; double speed; bool momentum;",
    "    const FixtureMotion *input; size_t input_count;",
    "    const FixtureOutput *expected; size_t expected_count;",
    "} Fixture;",
]
fixtures = []
for index, entry in enumerate(load(source / "files.jsonc"), 1):
    data = load(source / entry["input"])
    expected = load(source / entry["expected"])
    options = data["options"]
    if set(options) - {"speed_multiplier", "momentum_scroll_enabled"}:
        raise SystemExit(f"Unexpected options in {entry['input']}")
    speed = data["parameters"].get("mouse_motion_to_scroll.speed", 100) / 100
    speed *= options.get("speed_multiplier", 1.0)
    momentum = "true" if options.get("momentum_scroll_enabled", True) else "false"
    first = data["input"][0]["time_stamp"] // 1_000_000
    lines.append(f"static const FixtureMotion input_{index}[] = {{")
    for event in data["input"]:
        motion = event["pointing_motion"]
        time = event["time_stamp"] // 1_000_000 - first
        lines.append(f"    {{{motion['x']}, {motion['y']}, {time}}},")
    lines.append("};")
    lines.append(f"static const FixtureOutput expected_{index}[] = {{")
    for event in expected:
        lines.append(f"    {{{event['time']}, {event['wh']}, {event['wv']}}},")
    lines.append("};")
    name = Path(entry["input"]).stem
    fixtures.append(f'    {{"{name}", {speed!r}, {momentum}, input_{index}, '
                    f'{len(data["input"])}, expected_{index}, {len(expected)}}},')
lines += ["static const Fixture fixtures[] = {", *fixtures, "};", "#endif", ""]
Path(__file__).with_name("fixtures.h").write_text("\n".join(lines))
print(f"Imported {len(fixtures)} upstream fixtures from {PIN}")
