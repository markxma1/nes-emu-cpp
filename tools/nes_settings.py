#!/usr/bin/env python3
"""Settings program for the NES emulator (buttons, second controller, window size, volume).

Start it before a game:      python3 tools/nes_settings.py build
or from the running emulator with the key S. The folder argument is the one that contains the
emulator binary (there are settings.cfg, keyboard.cfg and gamepad.cfg). The emulator looks at these
files about twice a second, so "Save" takes effect in a running game without a restart.

Needs only Python 3 with tkinter (Arch: sudo pacman -S tk).
"""
import os
import re
import struct
import sys

# ---------------------------------------------------------------------------------------------
# Config files (plain code, no window: also used by the tests in tests/settings_check.py)
# ---------------------------------------------------------------------------------------------

BUTTONS = ["U", "D", "L", "R", "A", "B", "START", "SELECT"]
BUTTON_TEXT = {"U": "Up", "D": "Down", "L": "Left", "R": "Right", "A": "A", "B": "B",
               "START": "Start", "SELECT": "Select"}

DEFAULT_SETTINGS = {"scale": 3, "viewer_scale": 1, "volume": 100, "two_pads": 0}
SETTINGS_RANGE = {"scale": (1, 8), "viewer_scale": (1, 4), "volume": (0, 100), "two_pads": (0, 1)}

# The emulator's own defaults (NES/KeyboardInputSource.cpp, NES/GamepadInputSource.cpp).
DEFAULT_KEYBOARD = {"U": "KEY_W", "D": "KEY_S", "L": "KEY_A", "R": "KEY_D",
                    "A": "KEY_K", "B": "KEY_J", "START": "KEY_ENTER", "SELECT": "KEY_SPACE"}
DEFAULT_GAMEPAD = {"L": "Axis 0-", "R": "Axis 0+", "U": "Axis 1-", "D": "Axis 1+",
                   "A": "Button 1", "B": "Button 0", "START": "Button 9", "SELECT": "Button 8"}
# Player 2 on the number pad (the letters are taken by the emulator's own hotkeys).
SUGGESTED_KEYBOARD_P2 = {"U": "KEY_72", "D": "KEY_76", "L": "KEY_75", "R": "KEY_77",
                         "A": "KEY_79", "B": "KEY_80", "START": "KEY_KPENTER", "SELECT": "KEY_82"}

# Names the emulator knows (KeyTable in KeyboardInputSource.cpp); every other key is written as KEY_<number>.
EMULATOR_KEY_NAMES = set(
    ["KEY_" + c for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"]
    + ["KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT", "KEY_ENTER", "KEY_KPENTER", "KEY_SPACE", "KEY_TAB", "KEY_ESC",
       "KEY_LEFTSHIFT", "KEY_RIGHTSHIFT", "KEY_LEFTCTRL", "KEY_RIGHTCTRL", "KEY_LEFTALT", "KEY_RIGHTALT"])
NONE = "NONE"  # a player-1 button that must not react to anything (an empty line would mean "default")


def read_kv(path):
    """Reads `key=value` lines (comments with # are skipped). Missing file -> empty dict."""
    result = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                result[key.strip()] = value.strip()
    except OSError:
        pass
    return result


def write_kv(path, header, values):
    with open(path, "w", encoding="utf-8") as f:
        for h in header:
            f.write("# " + h + "\n")
        for key, value in values.items():
            f.write("%s=%s\n" % (key, value))


def load_settings(folder):
    values = dict(DEFAULT_SETTINGS)
    for key, text in read_kv(os.path.join(folder, "settings.cfg")).items():
        if key in SETTINGS_RANGE:
            try:
                lo, hi = SETTINGS_RANGE[key]
                values[key] = max(lo, min(hi, int(text)))
            except ValueError:
                pass
    return values


def save_settings(folder, values):
    write_kv(os.path.join(folder, "settings.cfg"),
             ["NES emulator settings - edited by tools/nes_settings.py (safe to hand-edit)"],
             {k: values[k] for k in DEFAULT_SETTINGS})


def load_bindings(folder, kind):
    """kind = "keyboard" or "gamepad". Returns {(player, button): label}: what the emulator would use."""
    defaults = DEFAULT_KEYBOARD if kind == "keyboard" else DEFAULT_GAMEPAD
    result = {(1, b): defaults[b] for b in BUTTONS}
    for key, label in read_kv(os.path.join(folder, kind + ".cfg")).items():
        if key.startswith("P2.") and key[3:] in BUTTONS:
            result[(2, key[3:])] = label
        elif key in BUTTONS:
            result[(1, key)] = label
    return result


def save_bindings(folder, kind, bindings):
    values = {}
    for player in (1, 2):
        for b in BUTTONS:
            label = bindings.get((player, b), "")
            if label:
                values[b if player == 1 else "P2." + b] = label
            elif player == 1:
                values[b] = NONE
    what = "evdev key label (e.g. KEY_W)" if kind == "keyboard" else '"Button N" or "Axis N+/-"'
    write_kv(os.path.join(folder, kind + ".cfg"),
             ["NES emulator %s bindings - button=%s; P2.<button> = player 2" % (kind, what),
              "Written by tools/nes_settings.py; safe to hand-edit."], values)


def key_label_from_code(code):
    """evdev key code -> label the emulator understands."""
    name = KEY_NAMES.get(code)
    return name if name in EMULATOR_KEY_NAMES else "KEY_%d" % code


def friendly(label):
    """Label -> text for humans: KEY_W -> W, KEY_72 -> Numpad 8, Axis 0- -> Axis 0-."""
    if not label or label == NONE:
        return "(none)"
    if not label.startswith("KEY_"):
        return label
    name = label
    if label[4:].isdigit():
        name = KEY_NAMES.get(int(label[4:]), label)
    text = name[4:]
    if text.startswith("KP") and len(text) > 2:
        text = "Numpad " + text[2:]
    return text if len(text) == 1 else text.capitalize()


def _load_key_names():
    names = {}
    try:
        with open("/usr/include/linux/input-event-codes.h", encoding="utf-8") as f:
            for line in f:
                m = re.match(r"#define\s+(KEY_[A-Z0-9_]+)\s+(\d+)\s*(/\*.*)?$", line.rstrip())
                if m and int(m.group(2)) not in names:
                    names[int(m.group(2))] = m.group(1)
    except OSError:
        pass
    # If the header is missing keep at least the keys the emulator knows.
    if not names:
        letters = "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"
        for row, start in zip(letters, (16, 30, 44)):
            for i, c in enumerate(row):
                names[start + i] = "KEY_" + c
        for i, c in enumerate("1234567890"):
            names[2 + i] = "KEY_" + c
        names.update({103: "KEY_UP", 108: "KEY_DOWN", 105: "KEY_LEFT", 106: "KEY_RIGHT", 28: "KEY_ENTER",
                      96: "KEY_KPENTER", 57: "KEY_SPACE", 15: "KEY_TAB", 1: "KEY_ESC", 42: "KEY_LEFTSHIFT",
                      54: "KEY_RIGHTSHIFT", 29: "KEY_LEFTCTRL", 97: "KEY_RIGHTCTRL", 56: "KEY_LEFTALT",
                      100: "KEY_RIGHTALT"})
    return names


KEY_NAMES = _load_key_names()


# ---------------------------------------------------------------------------------------------
# Gamepad reading (Linux joystick API, the same one the emulator uses)
# ---------------------------------------------------------------------------------------------

class PadReader:
    """Opens /dev/input/js0-3 without blocking; poll() returns the label of a newly pressed button/axis."""

    AXIS_THRESHOLD = 16000

    def __init__(self):
        self.fds = []
        for i in range(4):
            try:
                self.fds.append(os.open("/dev/input/js%d" % i, os.O_RDONLY | os.O_NONBLOCK))
            except OSError:
                pass

    def count(self):
        return len(self.fds)

    def poll(self, device=None):
        """device None = any pad, else index. Returns (device index, label) or None."""
        for index, fd in enumerate(self.fds):
            while True:
                try:
                    data = os.read(fd, 8)
                except OSError:
                    break
                if len(data) < 8:
                    break
                _time, value, etype, number = struct.unpack("<IhBB", data)
                if etype & 0x80:  # initial state events
                    continue
                if device is not None and index != device:
                    continue
                if etype == 1 and value == 1:
                    return index, "Button %d" % number
                if etype == 2 and abs(value) > self.AXIS_THRESHOLD:
                    return index, "Axis %d%s" % (number, "+" if value > 0 else "-")
        return None

    def close(self):
        for fd in self.fds:
            os.close(fd)
        self.fds = []


# ---------------------------------------------------------------------------------------------
# The window
# ---------------------------------------------------------------------------------------------

def run_ui(folder):
    import tkinter as tk
    from tkinter import ttk, messagebox

    root = tk.Tk()
    root.title("NES settings - " + os.path.abspath(folder))
    root.geometry("640x560")

    settings = load_settings(folder)
    keyboard = load_bindings(folder, "keyboard")
    gamepad = load_bindings(folder, "gamepad")
    saved = [dict(settings), dict(keyboard), dict(gamepad)]
    pad = PadReader()

    status = tk.StringVar(value="Click Change next to a button, then press the key / pad button you want.")
    player = tk.IntVar(value=1)
    capture = {"target": None}  # (kind, player, button) while waiting for a key / pad input
    cells = {}                  # (kind, button) -> StringVar shown in the table

    def dirty():
        return [settings, keyboard, gamepad] != saved

    def refresh():
        p = player.get()
        for b in BUTTONS:
            cells[("keyboard", b)].set(friendly(keyboard.get((p, b), "")))
            cells[("gamepad", b)].set(friendly(gamepad.get((p, b), "")))
        title = "NES settings" + (" *" if dirty() else "")
        root.title(title + " - " + os.path.abspath(folder))

    def start_capture(kind, button):
        capture["target"] = (kind, player.get(), button)
        what = "a key" if kind == "keyboard" else "a button or stick direction on the gamepad"
        status.set("Player %d, %s: press %s   (Cancel button or Esc = keep the old one)" % (player.get(), BUTTON_TEXT[button], what))
        if kind == "keyboard":
            root.focus_force()

    def finish_capture(label):
        kind, p, button = capture["target"]
        capture["target"] = None
        (keyboard if kind == "keyboard" else gamepad)[(p, button)] = label
        status.set("Player %d, %s = %s. Click Save to use it." % (p, BUTTON_TEXT[button], friendly(label)))
        refresh()

    def cancel_capture():
        if capture["target"]:
            capture["target"] = None
            status.set("Cancelled - nothing changed.")

    def on_key(event):
        if not capture["target"] or capture["target"][0] != "keyboard":
            return
        if event.keysym == "Escape":
            cancel_capture()
            return "break"
        code = event.keycode - 8  # X11 key codes are the Linux evdev codes plus 8
        if code <= 0:
            return "break"
        finish_capture(key_label_from_code(code))
        return "break"

    root.bind("<KeyPress>", on_key)

    def poll_pad():
        t = capture["target"]
        if t and t[0] == "gamepad":
            device = None
            if player.get() == 2 and settings["two_pads"] and pad.count() >= 2:
                device = 1
            elif player.get() == 1 and settings["two_pads"] and pad.count() >= 2:
                device = 0
            hit = pad.poll(device)
            if hit:
                finish_capture(hit[1])
        elif pad.count():
            pad.poll()  # drain
        root.after(30, poll_pad)

    def clear(kind, button):
        p = player.get()
        store = keyboard if kind == "keyboard" else gamepad
        store[(p, button)] = NONE if p == 1 else ""
        refresh()

    def reset(kind):
        p = player.get()
        store = keyboard if kind == "keyboard" else gamepad
        defaults = (DEFAULT_KEYBOARD if kind == "keyboard" else DEFAULT_GAMEPAD)
        for b in BUTTONS:
            store[(p, b)] = defaults[b] if p == 1 else ""
        status.set("Player %d %s reset to the defaults (Save to use)." % (p, kind))
        refresh()

    def suggest_p2():
        for b in BUTTONS:
            keyboard[(2, b)] = SUGGESTED_KEYBOARD_P2[b]
        status.set("Player 2 keyboard set to the number pad (Save to use).")
        refresh()

    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True, padx=8, pady=8)

    # --- Controls -------------------------------------------------------------------------
    controls = ttk.Frame(notebook, padding=8)
    notebook.add(controls, text="Controls")
    top = ttk.Frame(controls)
    top.pack(fill="x")
    ttk.Label(top, text="Player:").pack(side="left")
    ttk.Radiobutton(top, text="1", variable=player, value=1, command=lambda: (cancel_capture(), refresh())).pack(side="left", padx=4)
    ttk.Radiobutton(top, text="2", variable=player, value=2, command=lambda: (cancel_capture(), refresh())).pack(side="left", padx=4)
    ttk.Button(top, text="Cancel capture", command=cancel_capture).pack(side="right")

    grid = ttk.Frame(controls)
    grid.pack(fill="x", pady=10)
    for col, text in enumerate(["Button", "Keyboard", "", "", "Gamepad", "", ""]):
        ttk.Label(grid, text=text, font=("TkDefaultFont", 10, "bold")).grid(row=0, column=col, padx=4, sticky="w")
    for row, b in enumerate(BUTTONS, start=1):
        ttk.Label(grid, text=BUTTON_TEXT[b]).grid(row=row, column=0, padx=4, pady=2, sticky="w")
        for col0, kind in ((1, "keyboard"), (4, "gamepad")):
            var = tk.StringVar()
            cells[(kind, b)] = var
            ttk.Label(grid, textvariable=var, width=12, relief="sunken", anchor="center").grid(row=row, column=col0, padx=4)
            ttk.Button(grid, text="Change", width=7, command=lambda k=kind, x=b: start_capture(k, x)).grid(row=row, column=col0 + 1)
            ttk.Button(grid, text="Clear", width=5, command=lambda k=kind, x=b: clear(k, x)).grid(row=row, column=col0 + 2)
    bottom = ttk.Frame(controls)
    bottom.pack(fill="x")
    ttk.Button(bottom, text="Reset keyboard", command=lambda: reset("keyboard")).pack(side="left", padx=2)
    ttk.Button(bottom, text="Reset gamepad", command=lambda: reset("gamepad")).pack(side="left", padx=2)
    ttk.Button(bottom, text="Player 2: number pad", command=suggest_p2).pack(side="left", padx=2)
    ttk.Label(controls, wraplength=580, justify="left", foreground="#555",
              text="Player 1 also always answers to the arrow keys. Player 2 has no keys until you set them "
                   "(the number-pad button above is a quick start). With two gamepads see the 'Second controller' tab.").pack(fill="x", pady=8)

    # --- Display ---------------------------------------------------------------------------
    display = ttk.Frame(notebook, padding=12)
    notebook.add(display, text="Display")
    size_text = tk.StringVar()
    scale_var = tk.IntVar(value=settings["scale"])
    viewer_var = tk.IntVar(value=settings["viewer_scale"])

    def on_scale(_=None):
        settings["scale"] = int(round(float(scale_var.get())))
        size_text.set("Game window: %d x %d pixels" % (256 * settings["scale"], 240 * settings["scale"]))
        settings["viewer_scale"] = int(round(float(viewer_var.get())))
        refresh()

    ttk.Label(display, text="Game window size (every NES pixel = N screen pixels)").pack(anchor="w")
    tk.Scale(display, from_=1, to=8, orient="horizontal", variable=scale_var, command=on_scale, length=360).pack(anchor="w")
    ttk.Label(display, textvariable=size_text).pack(anchor="w", pady=(0, 14))
    ttk.Label(display, text="Debug windows (name table, pattern table, OAM, ...) size").pack(anchor="w")
    tk.Scale(display, from_=1, to=4, orient="horizontal", variable=viewer_var, command=on_scale, length=360).pack(anchor="w")
    ttk.Label(display, wraplength=560, justify="left", foreground="#555",
              text="In the running emulator: , and . change the game window size, < and > the debug windows, "
                   "F toggles full screen; the game window can also be resized by dragging its corner.").pack(anchor="w", pady=14)
    on_scale()

    # --- Sound -----------------------------------------------------------------------------
    sound = ttk.Frame(notebook, padding=12)
    notebook.add(sound, text="Sound")
    volume_var = tk.IntVar(value=settings["volume"])

    def on_volume(_=None):
        settings["volume"] = int(round(float(volume_var.get())))
        refresh()

    ttk.Label(sound, text="Volume (%)").pack(anchor="w")
    tk.Scale(sound, from_=0, to=100, orient="horizontal", variable=volume_var, command=on_volume, length=360).pack(anchor="w")

    # --- Players ---------------------------------------------------------------------------
    players = ttk.Frame(notebook, padding=12)
    notebook.add(players, text="Second controller")
    two_var = tk.IntVar(value=settings["two_pads"])

    def on_two():
        settings["two_pads"] = int(two_var.get())
        refresh()

    ttk.Checkbutton(players, text="Two gamepads: first pad = player 1, second pad = player 2",
                    variable=two_var, command=on_two).pack(anchor="w")
    ttk.Label(players, text="Gamepads found: %d" % pad.count()).pack(anchor="w", pady=6)
    ttk.Label(players, wraplength=560, justify="left", foreground="#555",
              text="Player 2 can also use the keyboard: set its keys on the Controls tab (Player 2). "
                   "Both ways work at the same time. Games decide themselves whether they use a second player "
                   "(for example a 2-player mode in the game menu).").pack(anchor="w", pady=10)

    # --- Bottom bar ------------------------------------------------------------------------
    bar = ttk.Frame(root, padding=(8, 0, 8, 8))
    bar.pack(fill="x")
    ttk.Label(bar, textvariable=status, wraplength=420, justify="left").pack(side="left", fill="x", expand=True)

    def save():
        try:
            save_settings(folder, settings)
            save_bindings(folder, "keyboard", keyboard)
            save_bindings(folder, "gamepad", gamepad)
        except OSError as e:
            messagebox.showerror("Save failed", str(e))
            return False
        saved[:] = [dict(settings), dict(keyboard), dict(gamepad)]
        status.set("Saved. A running emulator applies it within a second.")
        refresh()
        return True

    def revert():
        settings.clear(); settings.update(saved[0])
        keyboard.clear(); keyboard.update(saved[1])
        gamepad.clear(); gamepad.update(saved[2])
        scale_var.set(settings["scale"]); viewer_var.set(settings["viewer_scale"])
        volume_var.set(settings["volume"]); two_var.set(settings["two_pads"])
        on_scale()
        status.set("Changes discarded.")
        refresh()

    def close():
        if dirty() and not messagebox.askyesno("Unsaved changes", "Close without saving?"):
            return
        pad.close()
        root.destroy()

    ttk.Button(bar, text="Save", command=save).pack(side="right", padx=2)
    ttk.Button(bar, text="Revert", command=revert).pack(side="right", padx=2)
    ttk.Button(bar, text="Close", command=close).pack(side="right", padx=2)
    root.protocol("WM_DELETE_WINDOW", close)

    refresh()
    poll_pad()
    root.mainloop()


def find_folder(argument):
    if argument:
        return argument
    here = os.path.dirname(os.path.abspath(__file__))
    for candidate in (os.getcwd(), os.path.join(here, "..", "build")):
        if os.path.exists(os.path.join(candidate, "nes-emu")):
            return candidate
    return os.getcwd()


if __name__ == "__main__":
    run_ui(find_folder(sys.argv[1] if len(sys.argv) > 1 else None))
