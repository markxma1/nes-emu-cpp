#!/usr/bin/env python3
"""Skin editor for the NES emulator's HD layer (see EFFECTS.md).

    python3 tools/nes_skin_editor.py build/skins/Galaga

How it fits together:
  1. Run the game with the skin layer on (key H in the emulator), press X while the picture shows what
     you want to edit. That writes `capture/cap_<frame>.json + .png` into the game's skin folder.
  2. Open the folder here. Every distinct 8x8 tile (sprite or background) of all captures is listed.
     Paint a tile, or paint directly on the captured picture (Frame tab): every click lands on the tile
     under the mouse, flips are handled for you.
  3. Save. The tile pictures are written to `tiles/<hash>.png`. A running emulator picks them up within
     half a second - you see your edit in the game.

The tile picture is the tile in its original (unflipped) orientation, any size that is a multiple of 8
(default 32x32 = 4 times the NES resolution), with transparency. Transparent pixels keep the original.
Needs Python 3 + tkinter + Pillow (sudo pacman -S tk python-pillow).
"""
import glob
import json
import os
import sys
from collections import Counter

from PIL import Image, ImageChops

# ---------------------------------------------------------------------------------------------
# Logic without any window (tested by tests/skin_editor_check.py)
# ---------------------------------------------------------------------------------------------


def pixel_index(data, x, y):
    """Palette index 0-3 of pixel (x, y) of a tile given as 16 bytes (before flips)."""
    return ((data[y] >> (7 - x)) & 1) | (((data[y + 8] >> (7 - x)) & 1) << 1)


def tile_rgba(data, rgb, sprite, scale, flip_h=False, flip_v=False):
    """The tile as an RGBA picture of 8*scale pixels (index 0 of sprites is transparent)."""
    img = Image.new("RGBA", (8 * scale, 8 * scale), (0, 0, 0, 0))
    px = img.load()
    for y in range(8):
        for x in range(8):
            idx = pixel_index(data, 7 - x if flip_h else x, 7 - y if flip_v else y)
            if sprite and idx == 0:
                continue
            r, g, b = rgb[idx]
            for j in range(scale):
                for i in range(scale):
                    px[x * scale + i, y * scale + j] = (r, g, b, 255)
    return img


def cell_visibility(cell, frame):
    """Which pixels of a captured cell really show this tile in the captured picture.
    Returns a set of (x, y) inside the cell (same rule as HdLayer::MeasureVisibility in C++)."""
    data = bytes.fromhex(cell["bytes"])
    fpx = frame.load()
    visible, opaque = set(), 0
    for y in range(8):
        for x in range(8):
            idx = pixel_index(data, 7 - x if cell["fh"] else x, 7 - y if cell["fv"] else y)
            if cell["s"] and idx == 0:
                continue
            opaque += 1
            sx, sy = cell["x"] + x, cell["y"] + y
            if 0 <= sx < frame.width and 0 <= sy < frame.height and tuple(fpx[sx, sy][:3]) == tuple(cell["rgb"][idx]):
                visible.add((x, y))
    if opaque == 0 or len(visible) * 100 < opaque * 40:
        return set()
    return visible


class Capture:
    def __init__(self, path):
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        self.path = path
        self.frame_number = data["frame"]
        self.cells = data["cells"]
        self.image = Image.open(os.path.splitext(path)[0] + ".png").convert("RGB")
        self._visible = {}

    def visible(self, index):
        if index not in self._visible:
            self._visible[index] = cell_visibility(self.cells[index], self.image)
        return self._visible[index]


def find_skin_key(skins, hash_name, sprite, palette):
    for key in ("%s_%s%d" % (hash_name, "s" if sprite else "b", palette), hash_name):
        if key in skins:
            return key
    return None


def compose(capture, skins, scale):
    """What the emulator shows for this capture with these skins: an RGB picture of 256*scale x 240*scale."""
    out = capture.image.resize((256 * scale, 240 * scale), Image.NEAREST).convert("RGBA")
    order = [i for i, c in enumerate(capture.cells) if not c["s"]] + [i for i, c in enumerate(capture.cells) if c["s"]]
    for i in order:
        cell = capture.cells[i]
        key = find_skin_key(skins, cell["hash"], cell["s"], cell["pal"])
        if key is None:
            continue
        tile = skins[key]
        if tile.width != 8 * scale:
            tile = tile.resize((8 * scale, 8 * scale), Image.BICUBIC if tile.width < 8 * scale else Image.BOX)
        if cell["fh"]:
            tile = tile.transpose(Image.FLIP_LEFT_RIGHT)
        if cell["fv"]:
            tile = tile.transpose(Image.FLIP_TOP_BOTTOM)
        vis = capture.visible(i)
        if not vis:
            continue
        # only the pixels that really show this tile may be painted; PIL clips what is off screen
        mask = Image.new("L", tile.size, 0)
        for (x, y) in vis:
            mask.paste(255, (x * scale, y * scale, (x + 1) * scale, (y + 1) * scale))
        alpha = ImageChops.multiply(tile.getchannel("A"), mask)
        out.paste(tile, (cell["x"] * scale, cell["y"] * scale), alpha)
    return out.convert("RGB")


def cell_at(capture, nes_x, nes_y):
    """The cell whose tile shows at NES pixel (x, y): sprites first (they are on top), then background.
    Returns (cell index, local x, local y in the flipped cell) or None."""
    for want_sprite in (1, 0):
        for i in range(len(capture.cells) - 1, -1, -1):
            c = capture.cells[i]
            if c["s"] != want_sprite:
                continue
            lx, ly = nes_x - c["x"], nes_y - c["y"]
            if 0 <= lx < 8 and 0 <= ly < 8 and (lx, ly) in capture.visible(i):
                return i, lx, ly
    return None


def collect_tiles(captures):
    """All distinct tiles of the captures: hash -> {bytes, sprite uses, background uses, first colours per use}."""
    tiles = {}
    for cap in captures:
        for c in cap.cells:
            t = tiles.setdefault(c["hash"], {"bytes": c["bytes"], "uses": Counter(), "rgb": {}})
            key = ("s" if c["s"] else "b", c["pal"])
            t["uses"][key] += 1
            t["rgb"].setdefault(key, c["rgb"])
    return tiles


def default_colours(tile):
    """Colours of the most common use of a tile (for 'start from original')."""
    key = tile["uses"].most_common(1)[0][0]
    return key, tile["rgb"][key]


def load_skins(pack_dir):
    skins = {}
    for path in glob.glob(os.path.join(pack_dir, "tiles", "*.png")):
        try:
            img = Image.open(path).convert("RGBA")
        except OSError:
            continue
        if img.width == img.height and img.width % 8 == 0:
            skins[os.path.splitext(os.path.basename(path))[0]] = img
    return skins


def save_skin(pack_dir, stem, image):
    """Writes tiles/<stem>.png atomically (the emulator notices the folder change)."""
    folder = os.path.join(pack_dir, "tiles")
    os.makedirs(folder, exist_ok=True)
    tmp = os.path.join(folder, "." + stem + ".tmp.png")
    image.save(tmp)
    os.replace(tmp, os.path.join(folder, stem + ".png"))


def delete_skin(pack_dir, stem):
    try:
        os.remove(os.path.join(pack_dir, "tiles", stem + ".png"))
    except OSError:
        pass


def paint(image, x, y, colour, size=1):
    """Sets a size x size block (top-left at x, y) to colour (RGBA; alpha 0 erases)."""
    px = image.load()
    for j in range(size):
        for i in range(size):
            if 0 <= x + i < image.width and 0 <= y + j < image.height:
                px[x + i, y + j] = colour


def flood_fill(image, x, y, colour):
    px = image.load()
    target = px[x, y]
    if target == colour:
        return
    stack = [(x, y)]
    while stack:
        cx, cy = stack.pop()
        if 0 <= cx < image.width and 0 <= cy < image.height and px[cx, cy] == target:
            px[cx, cy] = colour
            stack.extend(((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)))


def export_sheet(tiles, skins, path, order, cell=64, columns=16):
    """A picture with the given tiles side by side (skin if there is one, else the original enlarged) - for an
    external AI upscaler / image editor. Returns the layout, which import_sheet() needs."""
    rows = (len(order) + columns - 1) // columns
    gap = 4
    sheet = Image.new("RGBA", (columns * (cell + gap) + gap, rows * (cell + gap) + gap), (255, 0, 255, 255))
    for n, h in enumerate(order):
        t = tiles[h]
        (kind, pal), rgb = default_colours(t)
        img = skins.get(h) or tile_rgba(bytes.fromhex(t["bytes"]), rgb, kind == "s", 1)
        img = img.resize((cell, cell), Image.NEAREST if img.width <= 8 else Image.BICUBIC)
        sheet.paste(img, (gap + (n % columns) * (cell + gap), gap + (n // columns) * (cell + gap)), img)
    sheet.save(path)
    return {"cell": cell, "gap": gap, "columns": columns, "order": order}


def import_sheet(path, layout, size):
    """Cuts a (possibly enlarged / AI-processed) sheet back into tile pictures of size x size. Returns {hash: image}."""
    sheet = Image.open(path).convert("RGBA")
    # the sheet may have been scaled by an upscaler: measure the factor from its width
    base_width = layout["columns"] * (layout["cell"] + layout["gap"]) + layout["gap"]
    f = sheet.width / base_width
    result = {}
    for n, h in enumerate(layout["order"]):
        x = (layout["gap"] + (n % layout["columns"]) * (layout["cell"] + layout["gap"])) * f
        y = (layout["gap"] + (n // layout["columns"]) * (layout["cell"] + layout["gap"])) * f
        piece = sheet.crop((round(x), round(y), round(x + layout["cell"] * f), round(y + layout["cell"] * f)))
        result[h] = piece.resize((size, size), Image.LANCZOS)
    return result


def load_captures(pack_dir):
    caps = []
    for path in sorted(glob.glob(os.path.join(pack_dir, "capture", "cap_*.json"))):
        try:
            caps.append(Capture(path))
        except (OSError, ValueError, KeyError):
            pass
    return caps


# ---------------------------------------------------------------------------------------------
# The window
# ---------------------------------------------------------------------------------------------

def run_ui(pack_dir):
    import tkinter as tk
    from tkinter import colorchooser, filedialog, messagebox, ttk
    from PIL import ImageTk

    root = tk.Tk()
    root.title("NES skin editor - " + os.path.abspath(pack_dir))
    root.geometry("1180x780")

    captures = load_captures(pack_dir)
    tiles = collect_tiles(captures)
    skins = load_skins(pack_dir)          # as saved on disk
    work = {}                             # stem -> edited picture (not yet saved)
    undo = []                             # (stem, previous picture or None)
    state = {"hash": None, "tool": "pencil", "colour": (255, 255, 255), "size": 1, "scale": 4,
             "capture": 0, "zoom": 3, "pal_key": None}
    if skins:
        state["scale"] = max(skins.values(), key=lambda i: i.width).width // 8

    tool = tk.StringVar(value="pencil")
    size_var = tk.IntVar(value=1)
    alpha_var = tk.IntVar(value=255)
    scale_var = tk.IntVar(value=state["scale"])
    only_var = tk.StringVar(value="all")
    variant_var = tk.IntVar(value=0)
    status = tk.StringVar(value="Pick a tile on the left, or click the picture in the Frame tab.")

    # ---------- helpers ----------
    def stem_for(h):
        """File stem the edit of tile h is saved under (palette variant or general)."""
        if variant_var.get() and state["pal_key"]:
            kind, pal = state["pal_key"]
            return "%s_%s%d" % (h, kind, pal)
        return h

    def current_image(h, create=True):
        stem = stem_for(h)
        if stem in work:
            return work[stem]
        base = skins.get(stem) or (skins.get(h) if not variant_var.get() else None)
        size = 8 * scale_var.get()
        if base is not None:
            img = base.copy().resize((size, size), Image.NEAREST if base.width < size else Image.BICUBIC) if base.width != size else base.copy()
        elif create and h in tiles:
            (kind, pal), rgb = default_colours(tiles[h]) if not state["pal_key"] else (state["pal_key"], tiles[h]["rgb"].get(state["pal_key"]) or default_colours(tiles[h])[1])
            img = tile_rgba(bytes.fromhex(tiles[h]["bytes"]), rgb, kind == "s", scale_var.get())
        else:
            return None
        if create:
            work[stem] = img  # only editing makes a picture "changed"; merely looking at it does not
        return img

    def push_undo(stem):
        undo.append((stem, work[stem].copy() if stem in work else None))
        del undo[:-60]

    def all_skins():
        merged = dict(skins)
        merged.update(work)
        return merged

    def dirty():
        return bool(work)

    def title():
        root.title("NES skin editor%s - %s" % (" *" if dirty() else "", os.path.abspath(pack_dir)))

    bottom = ttk.Frame(root, padding=4)
    bottom.pack(side="bottom", fill="x")

    # ---------- tile list ----------
    left = ttk.Frame(root, padding=4)
    left.pack(side="left", fill="y")
    filt = ttk.Frame(left)
    filt.pack(fill="x")
    for text, val in (("all", "all"), ("sprites", "s"), ("background", "b"), ("no skin yet", "new"), ("has skin", "skin")):
        ttk.Radiobutton(filt, text=text, variable=only_var, value=val, command=lambda: fill_list()).pack(anchor="w")
    listbox = tk.Listbox(left, width=30, height=34, exportselection=False, font=("TkFixedFont", 9))
    listbox.pack(fill="y", expand=True, pady=4)
    order = []

    def label_for(h):
        t = tiles[h]
        uses = t["uses"]
        kinds = "".join(sorted({k for k, _ in uses}))
        mark = "*" if h in work or any(k.startswith(h) for k in work) else ("✓" if any(k.startswith(h) for k in skins) else " ")
        return "%s %s  %-2s x%d" % (mark, h[:10], kinds, sum(uses.values()))

    def fill_list():
        listbox.delete(0, "end")
        order.clear()
        merged = all_skins()
        for h in sorted(tiles, key=lambda k: -sum(tiles[k]["uses"].values())):
            kinds = {k for k, _ in tiles[h]["uses"]}
            has = any(k.startswith(h) for k in merged)
            f = only_var.get()
            if f in ("s", "b") and f not in kinds:
                continue
            if f == "new" and has:
                continue
            if f == "skin" and not has:
                continue
            order.append(h)
            listbox.insert("end", label_for(h))
        title()

    def select_hash(h):
        state["hash"] = h
        state["pal_key"] = default_colours(tiles[h])[0] if h in tiles else None
        redraw_tile()
        if h in order:
            i = order.index(h)
            listbox.selection_clear(0, "end")
            listbox.selection_set(i)
            listbox.see(i)

    listbox.bind("<<ListboxSelect>>", lambda e: listbox.curselection() and select_hash(order[listbox.curselection()[0]]))

    # ---------- right side: tabs ----------
    notebook = ttk.Notebook(root)
    notebook.pack(side="left", fill="both", expand=True, padx=4, pady=4)
    tile_tab = ttk.Frame(notebook, padding=4)
    frame_tab = ttk.Frame(notebook, padding=4)
    notebook.add(tile_tab, text="Tile")
    notebook.add(frame_tab, text="Frame (paint on the captured picture)")

    # ---------- toolbar (shared) ----------
    bar = ttk.Frame(tile_tab)
    bar.pack(fill="x")
    for text, val in (("Pencil", "pencil"), ("Eraser", "eraser"), ("Fill", "fill"), ("Pick colour", "picker")):
        ttk.Radiobutton(bar, text=text, variable=tool, value=val).pack(side="left", padx=2)
    colour_btn = tk.Button(bar, text="  colour  ", bg="#ffffff", command=lambda: choose_colour())
    colour_btn.pack(side="left", padx=6)
    ttk.Label(bar, text="alpha").pack(side="left")
    ttk.Spinbox(bar, from_=0, to=255, width=4, textvariable=alpha_var).pack(side="left")
    ttk.Label(bar, text=" brush").pack(side="left")
    ttk.Spinbox(bar, from_=1, to=8, width=3, textvariable=size_var).pack(side="left")
    ttk.Label(bar, text=" tile size (x8)").pack(side="left")
    ttk.Spinbox(bar, from_=2, to=8, width=3, textvariable=scale_var).pack(side="left")

    def set_colour(rgb):
        state["colour"] = tuple(rgb)
        colour_btn.configure(bg="#%02x%02x%02x" % tuple(rgb))

    def choose_colour():
        c = colorchooser.askcolor(color="#%02x%02x%02x" % state["colour"])
        if c and c[0]:
            set_colour(tuple(int(v) for v in c[0]))

    swatches = ttk.Frame(tile_tab)
    swatches.pack(fill="x", pady=2)

    canvas = tk.Canvas(tile_tab, width=512, height=512, bg="#303030", highlightthickness=0)
    canvas.pack(pady=4)
    preview = tk.Label(tile_tab)
    preview.pack()
    tile_photo = {}

    def brush_colour():
        if tool.get() == "eraser":
            return (0, 0, 0, 0)
        return state["colour"] + (alpha_var.get(),)

    def redraw_tile():
        for w in swatches.winfo_children():
            w.destroy()
        h = state["hash"]
        if not h:
            return
        t = tiles.get(h)
        if t:
            ttk.Label(swatches, text="colours of the game:").pack(side="left")
            key = state["pal_key"] or default_colours(t)[0]
            for c in t["rgb"].get(key, []):
                tk.Button(swatches, width=3, bg="#%02x%02x%02x" % tuple(c), command=lambda c=c: set_colour(c)).pack(side="left", padx=1)
            uses = ", ".join("%s%d x%d" % (k, p, n) for (k, p), n in sorted(t["uses"].items()))
            ttk.Label(swatches, text="  used as: " + uses).pack(side="left")
        img = current_image(h, create=False)
        canvas.delete("all")
        if img is None:
            canvas.create_text(256, 256, fill="#aaa", text="No skin yet.\nClick 'Start from original' or 'Import PNG'.")
            preview.configure(image="")
            return
        z = 512 // img.width
        bg = Image.new("RGBA", img.size, (48, 48, 48, 255))
        for y in range(0, img.height, 8):
            for x in range(0, img.width, 8):
                if (x // 8 + y // 8) % 2:
                    bg.paste((64, 64, 64, 255), (x, y, x + 8, y + 8))
        shown = Image.alpha_composite(bg, img).resize((img.width * z, img.height * z), Image.NEAREST)
        tile_photo["big"] = ImageTk.PhotoImage(shown)
        canvas.configure(width=shown.width, height=shown.height)
        canvas.create_image(0, 0, anchor="nw", image=tile_photo["big"])
        # preview in the four flips at the size the emulator uses
        strip = Image.new("RGBA", (img.width * 4 + 30, img.height), (0, 0, 0, 255))
        for n, (fh, fv) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
            v = img
            v = v.transpose(Image.FLIP_LEFT_RIGHT) if fh else v
            v = v.transpose(Image.FLIP_TOP_BOTTOM) if fv else v
            strip.paste(v, (n * (img.width + 10), 0), v)
        tile_photo["prev"] = ImageTk.PhotoImage(strip)
        preview.configure(image=tile_photo["prev"])

    def apply_tool(img, stem, x, y, first):
        t = tool.get()
        if t == "picker":
            r, g, b, a = img.getpixel((x, y))
            set_colour((r, g, b))
            alpha_var.set(a)
            return False
        if first:
            push_undo(stem)
        if t == "fill":
            flood_fill(img, x, y, brush_colour())
        else:
            paint(img, x - (size_var.get() - 1) // 2, y - (size_var.get() - 1) // 2, brush_colour(), size_var.get())
        return True

    def on_tile_mouse(event, first):
        h = state["hash"]
        if not h:
            return
        img = current_image(h)
        if img is None:
            return
        z = 512 // img.width
        x, y = event.x // z, event.y // z
        if not (0 <= x < img.width and 0 <= y < img.height):
            return
        if apply_tool(img, stem_for(h), x, y, first):
            redraw_tile()
            fill_list_keep()

    canvas.bind("<Button-1>", lambda e: on_tile_mouse(e, True))
    canvas.bind("<B1-Motion>", lambda e: on_tile_mouse(e, False))

    def fill_list_keep():
        sel = state["hash"]
        fill_list()
        if sel in order:
            listbox.selection_set(order.index(sel))

    # ---------- tile actions ----------
    actions = ttk.Frame(tile_tab)
    actions.pack(fill="x", pady=4)

    def start_from_original():
        h = state["hash"]
        if h in tiles:
            stem = stem_for(h)
            push_undo(stem)
            work.pop(stem, None)
            (kind, pal), rgb = default_colours(tiles[h]) if not state["pal_key"] else (state["pal_key"], tiles[h]["rgb"].get(state["pal_key"]) or default_colours(tiles[h])[1])
            work[stem] = tile_rgba(bytes.fromhex(tiles[h]["bytes"]), rgb, kind == "s", scale_var.get())
            redraw_tile()
            fill_list_keep()

    def import_png():
        h = state["hash"]
        path = filedialog.askopenfilename(filetypes=[("PNG", "*.png"), ("all", "*")])
        if h and path:
            stem = stem_for(h)
            push_undo(stem)
            size = 8 * scale_var.get()
            work[stem] = Image.open(path).convert("RGBA").resize((size, size), Image.LANCZOS)
            redraw_tile()
            fill_list_keep()

    def export_png():
        h = state["hash"]
        img = current_image(h) if h else None
        path = filedialog.asksaveasfilename(defaultextension=".png") if img else None
        if path:
            img.save(path)

    def remove_skin():
        h = state["hash"]
        if not h:
            return
        stem = stem_for(h)
        push_undo(stem)
        work.pop(stem, None)
        if stem in skins and messagebox.askyesno("Delete", "Delete the saved skin picture of this tile?"):
            delete_skin(pack_dir, stem)
            del skins[stem]
        redraw_tile()
        fill_list_keep()

    def do_undo(_=None):
        if not undo:
            return
        stem, previous = undo.pop()
        if previous is None:
            work.pop(stem, None)
        else:
            work[stem] = previous
        redraw_tile()
        redraw_frame()
        fill_list_keep()

    def save_all():
        for stem, img in list(work.items()):
            save_skin(pack_dir, stem, img)
            skins[stem] = img.copy()
        work.clear()
        undo.clear()
        status.set("Saved. A running emulator shows the new pictures within a second.")
        redraw_tile()
        fill_list_keep()

    for text, cmd in (("Start from original", start_from_original), ("Import PNG...", import_png), ("Export PNG...", export_png),
                      ("Undo (Ctrl+Z)", do_undo), ("Delete skin", remove_skin)):
        ttk.Button(actions, text=text, command=cmd).pack(side="left", padx=2)
    ttk.Checkbutton(actions, text="only for this palette", variable=variant_var, command=redraw_tile).pack(side="left", padx=8)
    root.bind("<Control-z>", do_undo)

    # ---------- frame tab ----------
    fbar = ttk.Frame(frame_tab)
    fbar.pack(fill="x")
    cap_box = ttk.Combobox(fbar, state="readonly", width=24)
    cap_box.pack(side="left")
    ttk.Label(fbar, text="  zoom").pack(side="left")
    zoom_box = ttk.Combobox(fbar, state="readonly", width=4, values=["2", "3", "4"])
    zoom_box.set("3")
    zoom_box.pack(side="left")
    ttk.Label(fbar, text="  the tools and colour of the Tile tab are used").pack(side="left")
    fcanvas = tk.Canvas(frame_tab, bg="#202020", highlightthickness=0)
    fcanvas.pack(fill="both", expand=True, pady=4)
    fphoto = {}

    def redraw_frame():
        if not captures:
            fcanvas.delete("all")
            fcanvas.create_text(300, 200, fill="#aaa", text="No captures yet.\nRun the game, press H (skin layer on), then X to capture.")
            return
        cap = captures[state["capture"]]
        scale = scale_var.get()
        img = compose(cap, all_skins(), scale)
        z = int(zoom_box.get())
        f = z / scale
        shown = img.resize((int(img.width * f), int(img.height * f)), Image.NEAREST if f >= 1 else Image.BOX)
        fphoto["img"] = ImageTk.PhotoImage(shown)
        fcanvas.delete("all")
        fcanvas.create_image(0, 0, anchor="nw", image=fphoto["img"])

    def on_frame_mouse(event, first):
        if not captures:
            return
        cap = captures[state["capture"]]
        z = int(zoom_box.get())
        nes_x, nes_y = event.x // z, event.y // z
        hit = cell_at(cap, nes_x, nes_y)
        if not hit:
            return
        i, lx, ly = hit
        cell = cap.cells[i]
        h = cell["hash"]
        if h != state["hash"]:
            select_hash(h)
            state["pal_key"] = ("s" if cell["s"] else "b", cell["pal"])
        img = current_image(h)
        scale = img.width // 8
        # sub-pixel position inside the NES pixel (0..z-1) -> position inside the skin pixel block
        sub_x = (event.x % z) * scale // z
        sub_y = (event.y % z) * scale // z
        fx = 7 - lx if cell["fh"] else lx
        fy = 7 - ly if cell["fv"] else ly
        if apply_tool(img, stem_for(h), fx * scale + sub_x, fy * scale + sub_y, first):
            redraw_frame()
            fill_list_keep()

    fcanvas.bind("<Button-1>", lambda e: on_frame_mouse(e, True))
    fcanvas.bind("<B1-Motion>", lambda e: on_frame_mouse(e, False))

    def fill_captures():
        cap_box.configure(values=["frame %d (%d tiles)" % (c.frame_number, len(c.cells)) for c in captures])
        if captures:
            cap_box.current(state["capture"])

    def on_cap_changed(_=None):
        state["capture"] = cap_box.current()
        redraw_frame()

    cap_box.bind("<<ComboboxSelected>>", on_cap_changed)
    zoom_box.bind("<<ComboboxSelected>>", lambda e: redraw_frame())
    notebook.bind("<<NotebookTabChanged>>", lambda e: redraw_frame() if notebook.index("current") == 1 else redraw_tile())

    # ---------- bottom ----------
    def sheet_export():
        path = filedialog.asksaveasfilename(defaultextension=".png", title="Save sheet for an AI upscaler / image editor")
        if not path:
            return
        layout = export_sheet(tiles, all_skins(), path, list(order))
        with open(os.path.splitext(path)[0] + ".layout.json", "w", encoding="utf-8") as f:
            json.dump(layout, f)
        status.set("Sheet written: %s (+ .layout.json). Process it, then use 'Import sheet'." % path)

    def sheet_import():
        path = filedialog.askopenfilename(title="Processed sheet (the .layout.json must sit next to the original name)")
        if not path:
            return
        layout_path = filedialog.askopenfilename(title="The .layout.json written by 'Export sheet'")
        if not layout_path:
            return
        with open(layout_path, encoding="utf-8") as f:
            layout = json.load(f)
        pieces = import_sheet(path, layout, 8 * scale_var.get())
        for h, img in pieces.items():
            push_undo(h)
            work[h] = img
        status.set("Imported %d tiles (not saved yet)." % len(pieces))
        fill_list()
        redraw_tile()

    ttk.Label(bottom, textvariable=status).pack(side="left", fill="x", expand=True)
    for text, cmd in (("Save all", save_all), ("Import sheet...", sheet_import), ("Export sheet...", sheet_export)):
        ttk.Button(bottom, text=text, command=cmd).pack(side="right", padx=2)

    def close():
        if dirty() and not messagebox.askyesno("Unsaved changes", "Close without saving?"):
            return
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    fill_captures()
    fill_list()
    if order:
        select_hash(order[0])
        listbox.selection_set(0)
    redraw_frame()
    root.mainloop()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    run_ui(sys.argv[1])
