#!/usr/bin/env python3
"""Checks the config-file handling of tools/nes_settings.py (no window needed)."""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import nes_settings as s

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


with tempfile.TemporaryDirectory() as d:
    # missing files -> the emulator's defaults
    check(s.load_settings(d) == s.DEFAULT_SETTINGS, "missing settings.cfg gives defaults")
    kb = s.load_bindings(d, "keyboard")
    check(kb[(1, "A")] == "KEY_K" and (2, "A") not in kb, "keyboard defaults: player 1 set, player 2 empty")

    # values are clamped and round-trip
    values = dict(s.DEFAULT_SETTINGS, scale=5, volume=40, two_pads=1)
    s.save_settings(d, values)
    check(s.load_settings(d) == values, "settings round-trip")
    with open(os.path.join(d, "settings.cfg"), "w") as f:
        f.write("scale=99\nvolume=-5\nviewer_scale=abc\n")
    got = s.load_settings(d)
    check(got["scale"] == 8 and got["volume"] == 0 and got["viewer_scale"] == 1, "out-of-range / broken values are clamped or ignored")

    # bindings: player 2 gets a P2. prefix, a cleared player-1 button becomes NONE, one change keeps the rest
    kb = s.load_bindings(d, "keyboard")
    kb[(1, "A")] = "KEY_H"
    kb[(1, "B")] = ""
    kb[(2, "U")] = "KEY_72"
    s.save_bindings(d, "keyboard", kb)
    text = open(os.path.join(d, "keyboard.cfg")).read()
    check("P2.U=KEY_72" in text, "player 2 binding written with P2. prefix")
    check("B=NONE" in text, "cleared player-1 button written as NONE")
    check("P2.A" not in text, "unset player-2 buttons are not written")
    again = s.load_bindings(d, "keyboard")
    check(again[(1, "A")] == "KEY_H" and again[(1, "R")] == "KEY_D" and again[(2, "U")] == "KEY_72", "bindings round-trip")

    # gamepad file format
    gp = s.load_bindings(d, "gamepad")
    gp[(2, "A")] = "Button 3"
    s.save_bindings(d, "gamepad", gp)
    check("P2.A=Button 3" in open(os.path.join(d, "gamepad.cfg")).read(), "gamepad player 2 binding")

# key labels: emulator names for known keys, numbers for others
check(s.key_label_from_code(17) == "KEY_W", "evdev 17 is KEY_W")
check(s.key_label_from_code(72) == "KEY_72", "keys the emulator has no name for are written as numbers")
check(s.key_label_from_code(96) == "KEY_KPENTER", "numpad enter has a name")
check(s.friendly("KEY_W") == "W" and s.friendly("KEY_72") == "Numpad 8", "friendly names (%r, %r)" % (s.friendly("KEY_W"), s.friendly("KEY_72")))
check(s.friendly("NONE") == "(none)" and s.friendly("Axis 0-") == "Axis 0-", "friendly names for none / axis")

if failures:
    print("%d settings check(s) FAILED" % failures)
    sys.exit(1)
print("PASS: settings file handling")
