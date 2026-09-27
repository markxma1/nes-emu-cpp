#!/usr/bin/env python3
"""Skin editor for the NES emulator's HD layer (see EFFECTS.md).

    python3 tools/nes_skin_editor.py build/skins/Galaga

How it fits together:
  1. Run the game with the skin layer on (key / in the emulator), press ' while the picture shows what
     you want to edit. That writes `capture/cap_<frame>.json + .png` into the game's skin folder.
  2. Open the folder here. Every distinct 8x8 tile (sprite or background) of all captures is listed.
     Paint a tile, or paint directly on the captured picture (Frame tab): every click lands on the tile
     under the mouse, flips are handled for you.
  3. Save. The tile pictures are written to `tiles/<hash>.png`. A running emulator picks them up within
     half a second - you see your edit in the game.

Tiles that belong together (a 2x2 ship, a big enemy) can be made into a group with ONE picture (Frame tab:
'Auto-group sprites' or 'Select tiles' + 'Group selected'); see EFFECTS.md.

Sprites and background sitting close together can make it easy to click/select the wrong one - the Frame
tab's 'Show: Sprites / Background' checkboxes hide one layer and make it un-clickable, so that can't happen.

Want every individual tile the ROM contains, without playing the game at all? Run
`python3 tools/nes_extract_chr.py game.nes build/skins/game` first - it reads the ROM file directly and
writes synthetic captures this editor opens exactly like a real one. Groups still need one real capture.

The tile picture is the tile in its original (unflipped) orientation, any size that is a multiple of 8
(default 32x32 = 4 times the NES resolution), with transparency. Transparent pixels keep the original.
Needs Python 3 + tkinter + Pillow (sudo pacman -S tk python-pillow).
"""
import glob
import json
import os
import sys
from collections import Counter

from PIL import Image, ImageChops, ImageDraw

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


def compose(capture, skins, scale, objects=None):
    """What the emulator shows for this capture with these skins: an RGB picture of 256*scale x 240*scale."""
    out = capture.image.resize((256 * scale, 240 * scale), Image.NEAREST).convert("RGBA")
    matches = match_objects(capture, objects) if objects else []
    in_object = {i for m in matches for i in m.members}
    for m in matches:
        paint_object_match(out, capture, m, scale)
    order = [i for i, c in enumerate(capture.cells) if not c["s"]] + [i for i, c in enumerate(capture.cells) if c["s"]]
    for i in order:
        if i in in_object:
            continue
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


# A visibly-not-part-of-the-game checkerboard (nothing in an NES picture looks like this), so a hidden
# layer reads as "hidden", not as if the game genuinely drew flat grey there.
_HIDE_CHECKER = ((90, 60, 90), (60, 40, 60))


def hide_layers(capture, image, scale, show_sprites=True, show_background=True, objects=None):
    """Returns a copy of the composed `image` (RGB, 256*scale x 240*scale, as compose() returns) with a
    hidden layer's pixels painted over with a checkerboard, so sprites and background can be told apart
    (and, combined with cell_at(..., layers=...)/cells_in_rect() filtering to the visible layer, clicking
    can never land on the hidden one by accident). `objects` should be the same dict compose() was given:
    a matched GROUP paints its own area (object_pixels(), which can extend beyond any single member
    cell's own visible pixels with overflow) - without this, a hidden group's pixels would stay showing."""
    if show_sprites and show_background:
        return image
    out = image.copy()

    def blank(x, y):
        colour = _HIDE_CHECKER[(x + y) % 2]
        out.paste(colour, (x * scale, y * scale, (x + 1) * scale, (y + 1) * scale))

    in_object = set()
    for m in (match_objects(capture, objects) if objects else []):
        if (m.obj.layer == "s" and show_sprites) or (m.obj.layer == "b" and show_background):
            continue
        in_object.update(m.members)
        for (x, y) in object_pixels(capture, m):
            blank(x, y)
    for i, c in enumerate(capture.cells):
        if i in in_object or (c["s"] and show_sprites) or (not c["s"] and show_background):
            continue
        for (x, y) in capture.visible(i):
            blank(c["x"] + x, c["y"] + y)
    return out


def cell_at(capture, nes_x, nes_y, layers=(1, 0)):
    """The cell whose tile shows at NES pixel (x, y): sprites first (they are on top), then background,
    or only the given layer(s) (1 = sprites, 0 = background - see `layers`, e.g. (1,) to never hit
    background, for a "hide background" view where accidentally clicking through it isn't possible).
    Returns (cell index, local x, local y in the flipped cell) or None."""
    for want_sprite in layers:
        for i in range(len(capture.cells) - 1, -1, -1):
            c = capture.cells[i]
            if c["s"] != want_sprite:
                continue
            lx, ly = nes_x - c["x"], nes_y - c["y"]
            if 0 <= lx < 8 and 0 <= ly < 8 and (lx, ly) in capture.visible(i):
                return i, lx, ly
    return None


def cells_in_rect(capture, x0, y0, x1, y1):
    """Indices of every cell that has a visible pixel inside the rectangle (x0..x1, y0..y1, NES pixels)."""
    found = set()
    for i, c in enumerate(capture.cells):
        for (px, py) in capture.visible(i):
            if x0 <= c["x"] + px <= x1 and y0 <= c["y"] + py <= y1:
                found.add(i)
                break
    return found


def filter_one_layer(capture, indices):
    """Keeps one layer only: sprites if the selection has any, background otherwise. A rectangle dragged around
    a sprite (which is rarely a filled 8x8/16x16 block) almost always also touches background pixels peeking
    through its corners; without this, grouping such a selection fails ("must be all sprites or background")
    even though the user only meant to select the sprite."""
    sprites = {i for i in indices if capture.cells[i]["s"]}
    return sprites if sprites else set(indices)


# ---------------------------------------------------------------------------------------------
# Objects: groups of tiles with one picture (same rules as HdLayer::PaintObjects in C++)
# ---------------------------------------------------------------------------------------------

class Obj:
    """A group of tiles found together (a 2x2 ship, a big enemy) with one picture for all of them."""

    def __init__(self, name, layer, width, height, tiles, picture, margin=0, overflow=False):
        self.name, self.layer, self.width, self.height = name, layer, width, height
        self.tiles = tiles            # list of dicts: dx, dy, hash, fh, fv (as they were seen when the group was made)
        self.picture = picture        # RGBA, (width+2*margin)*k x (height+2*margin)*k
        self.margin, self.overflow = margin, overflow

    @property
    def k(self):
        return self.picture.width // (self.width + 2 * self.margin)

    def native_size(self, scale):
        """The (width, height) in pixels this object's picture should have at a given per-tile scale -
        generally NOT square (e.g. a wide, flat enemy), unlike a plain tile. Only `overflow` objects
        actually reserve room for their margin in the picture; without overflow the margin is unused."""
        m = self.margin if self.overflow else 0
        return ((self.width + 2 * m) * scale, (self.height + 2 * m) * scale)

    def to_text(self):
        lines = ["layer " + self.layer, "size %d %d" % (self.width, self.height), "margin %d" % self.margin,
                 "overflow %d" % (1 if self.overflow else 0)]
        for t in self.tiles:
            lines.append("tile %d %d %s %d %d" % (t["dx"], t["dy"], t["hash"], t["fh"], t["fv"]))
        return "\n".join(lines) + "\n"


def parse_object(name, text, picture):
    layer, size, margin, overflow, tiles = "s", None, 0, False, []
    for line in text.splitlines():
        parts = line.split()
        if not parts or parts[0].startswith("#"):
            continue
        if parts[0] == "layer":
            layer = parts[1]
        elif parts[0] == "size":
            size = (int(parts[1]), int(parts[2]))
        elif parts[0] == "margin":
            margin = int(parts[1])
        elif parts[0] == "overflow":
            overflow = parts[1] != "0"
        elif parts[0] == "tile":
            tiles.append({"dx": int(parts[1]), "dy": int(parts[2]), "hash": parts[3], "fh": int(parts[4]), "fv": int(parts[5])})
    if size is None or not tiles:
        raise ValueError("bad object file")
    return Obj(name, layer, size[0], size[1], tiles, picture, margin, overflow)


def load_objects(pack_dir):
    result = {}
    for path in sorted(glob.glob(os.path.join(pack_dir, "objects", "*.obj"))):
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
            picture = Image.open(os.path.join(pack_dir, "objects", name + ".png")).convert("RGBA")
            result[name] = parse_object(name, text, picture)
        except (OSError, ValueError, IndexError):
            continue
    return result


def save_object(pack_dir, obj):
    """Writes objects/<name>.png and .obj atomically (the emulator notices the folder change)."""
    folder = os.path.join(pack_dir, "objects")
    os.makedirs(folder, exist_ok=True)
    tmp_png = os.path.join(folder, "." + obj.name + ".tmp.png")
    obj.picture.save(tmp_png)
    os.replace(tmp_png, os.path.join(folder, obj.name + ".png"))
    tmp = os.path.join(folder, "." + obj.name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(obj.to_text())
    os.replace(tmp, os.path.join(folder, obj.name + ".obj"))


def delete_object(pack_dir, name):
    for ext in (".obj", ".png"):
        try:
            os.remove(os.path.join(pack_dir, "objects", name + ext))
        except OSError:
            pass


class Match:
    def __init__(self, obj, members, ox, oy, fh, fv):
        self.obj, self.members, self.ox, self.oy, self.fh, self.fv = obj, members, ox, oy, fh, fv


def match_objects(capture, objects):
    """Every place in the capture where all tiles of an object are present (each cell belongs to one match)."""
    cells = capture.cells
    at = {}
    for i, c in enumerate(cells):
        at.setdefault((c["s"], c["x"], c["y"]), []).append(i)
    consumed = set()
    matches = []
    for obj in objects.values():
        sprite = 1 if obj.layer == "s" else 0
        anchor = obj.tiles[0]
        for first, a in enumerate(cells):
            if first in consumed or a["s"] != sprite or a["hash"] != anchor["hash"]:
                continue
            for variant in range(4):
                fh, fv = variant & 1, (variant >> 1) & 1
                if a["fh"] != (anchor["fh"] ^ fh) or a["fv"] != (anchor["fv"] ^ fv):
                    continue
                ox = a["x"] - (obj.width - 8 - anchor["dx"] if fh else anchor["dx"])
                oy = a["y"] - (obj.height - 8 - anchor["dy"] if fv else anchor["dy"])
                members = []
                for t in obj.tiles:
                    x = ox + (obj.width - 8 - t["dx"] if fh else t["dx"])
                    y = oy + (obj.height - 8 - t["dy"] if fv else t["dy"])
                    found = None
                    for i in at.get((sprite, x, y), []):
                        c = cells[i]
                        if i not in consumed and i not in members and c["hash"] == t["hash"] \
                                and c["fh"] == (t["fh"] ^ fh) and c["fv"] == (t["fv"] ^ fv):
                            found = i
                            break
                    if found is None:
                        break
                    members.append(found)
                else:
                    consumed.update(members)
                    matches.append(Match(obj, members, ox, oy, fh, fv))
                    break
    return matches


def object_pixels(capture, match):
    """NES pixels (absolute x, y) where the object's picture is painted: the visible pixels of its tiles,
    or the whole area including the margin when the object has overflow."""
    obj = match.obj
    if obj.overflow:
        m = obj.margin
        return {(match.ox + x, match.oy + y) for y in range(-m, obj.height + m) for x in range(-m, obj.width + m)}
    out = set()
    for i in match.members:
        c = capture.cells[i]
        for (x, y) in capture.visible(i):
            out.add((c["x"] + x, c["y"] + y))
    return out


def paint_object_match(out, capture, match, scale):
    """Paints one matched object onto the composed picture `out` (RGBA, 256*scale x 240*scale)."""
    obj = match.obj
    pw, ph = (obj.width + 2 * obj.margin) * scale, (obj.height + 2 * obj.margin) * scale
    pic = obj.picture.resize((pw, ph), Image.BICUBIC if obj.picture.width < pw else Image.BOX)
    if match.fh:
        pic = pic.transpose(Image.FLIP_LEFT_RIGHT)
    if match.fv:
        pic = pic.transpose(Image.FLIP_TOP_BOTTOM)
    mask = Image.new("L", (pw, ph), 0)
    for (x, y) in object_pixels(capture, match):
        lx, ly = x - match.ox + obj.margin, y - match.oy + obj.margin
        if 0 <= lx < obj.width + 2 * obj.margin and 0 <= ly < obj.height + 2 * obj.margin:
            mask.paste(255, (lx * scale, ly * scale, (lx + 1) * scale, (ly + 1) * scale))
    alpha = ImageChops.multiply(pic.getchannel("A"), mask)
    out.paste(pic, ((match.ox - obj.margin) * scale, (match.oy - obj.margin) * scale), alpha)


def object_from_cells(capture, indices, scale, name, margin=0, overflow=False):
    """Makes an object out of the chosen captured cells (all of one layer). Its picture starts as the original
    pixels enlarged, so nothing changes until it is painted."""
    cells = [capture.cells[i] for i in indices]
    if not cells:
        raise ValueError("nothing selected")
    if len({c["s"] for c in cells}) != 1:
        raise ValueError("a group must be all sprites or all background")
    minx, miny = min(c["x"] for c in cells), min(c["y"] for c in cells)
    maxx, maxy = max(c["x"] for c in cells) + 8, max(c["y"] for c in cells) + 8
    width, height = maxx - minx, maxy - miny
    m = margin if overflow else 0
    pic = Image.new("RGBA", ((width + 2 * m) * scale, (height + 2 * m) * scale), (0, 0, 0, 0))
    tiles = []
    for c in cells:
        tiles.append({"dx": c["x"] - minx, "dy": c["y"] - miny, "hash": c["hash"], "fh": c["fh"], "fv": c["fv"]})
        img = tile_rgba(bytes.fromhex(c["bytes"]), c["rgb"], c["s"], scale, bool(c["fh"]), bool(c["fv"]))
        pic.paste(img, ((c["x"] - minx + m) * scale, (c["y"] - miny + m) * scale), img)
    if len({(t["dx"], t["dy"]) for t in tiles}) != len(tiles):
        raise ValueError("two selected tiles lie on top of each other; select one layer of tiles only")
    return Obj(name, "s" if cells[0]["s"] else "b", width, height, tiles, pic, m, overflow)


def auto_group(capture, same_palette=True):
    """Suggests groups: sprite cells that touch each other (also at the corners) form one group. Returns lists of cell
    indices of groups with at least two tiles, one per distinct shape."""
    idx = [i for i, c in enumerate(capture.cells) if c["s"]]
    parent = {i: i for i in idx}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for a in idx:
        for b in idx:
            if a < b:
                ca, cb = capture.cells[a], capture.cells[b]
                if abs(ca["x"] - cb["x"]) <= 8 and abs(ca["y"] - cb["y"]) <= 8 and (not same_palette or ca["pal"] == cb["pal"]):
                    parent[find(a)] = find(b)
    groups = {}
    for i in idx:
        groups.setdefault(find(i), []).append(i)
    seen, result = set(), []
    for members in groups.values():
        if len(members) < 2:
            continue
        minx = min(capture.cells[i]["x"] for i in members)
        miny = min(capture.cells[i]["y"] for i in members)
        width = max(capture.cells[i]["x"] for i in members) - minx + 8
        height = max(capture.cells[i]["y"] for i in members) - miny + 8

        def signature(fh, fv):
            return frozenset((width - 8 - (capture.cells[i]["x"] - minx) if fh else capture.cells[i]["x"] - minx,
                              height - 8 - (capture.cells[i]["y"] - miny) if fv else capture.cells[i]["y"] - miny,
                              capture.cells[i]["hash"], capture.cells[i]["fh"] ^ fh, capture.cells[i]["fv"] ^ fv) for i in members)

        variants = [signature(fh, fv) for fh in (0, 1) for fv in (0, 1)]
        if not any(v in seen for v in variants):  # the same shape mirrored counts as the same group
            seen.add(variants[0])
            result.append(sorted(members))
    return result


def match_at(capture, matches, nes_x, nes_y):
    """The object match whose picture is painted at NES pixel (x, y), or None."""
    for m in matches:
        if (nes_x, nes_y) in object_pixels(capture, m):
            return m
    return None


def picture_position(match, nes_x, nes_y, sub_x, sub_y):
    """Position inside the object's own picture (unflipped) for a click at NES pixel + sub-pixel fraction (0..1)."""
    obj = match.obj
    lx, ly = nes_x - match.ox + obj.margin, nes_y - match.oy + obj.margin
    w, h = obj.width + 2 * obj.margin, obj.height + 2 * obj.margin
    if match.fh:
        lx, sub_x = w - 1 - lx, 1 - sub_x
    if match.fv:
        ly, sub_y = h - 1 - ly, 1 - sub_y
    k = obj.k
    return min(int((lx + sub_x) * k), w * k - 1), min(int((ly + sub_y) * k), h * k - 1)


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


def capture_paths(pack_dir):
    """The capture/*.json files currently on disk, in no particular order (see load_captures for the
    order they should be shown in)."""
    return sorted(glob.glob(os.path.join(pack_dir, "capture", "cap_*.json")))


def _capture_order_key(capture):
    """Real captures (frame_number >= 0, from playing the game) sort chronologically; synthetic ones
    from nes_extract_chr.py (frame_number = -(page+1)) sort by page (0, 1, 2, ...) - plain ascending
    frame_number would show page 1 before page 0, since -2 < -1."""
    n = capture.frame_number
    return n if n >= 0 else -(n + 1)


def load_captures(pack_dir):
    """Every capture in the pack, in display order (see _capture_order_key) - filenames alone don't sort
    right (e.g. "cap_1300" would come before "cap_200" as plain text once frame numbers reach different
    digit counts), so this loads them all and then sorts by the frame number actually stored inside."""
    caps = []
    for path in capture_paths(pack_dir):
        try:
            caps.append(Capture(path))
        except (OSError, ValueError, KeyError):
            pass
    caps.sort(key=_capture_order_key)
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
    objects = load_objects(pack_dir)      # groups of tiles with one picture, as saved on disk
    selection = set()                     # cell indices selected in the Frame tab (to be grouped)
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
    overflow_var = tk.IntVar(value=0)
    status = tk.StringVar(value="Pick a tile on the left, or click the picture in the Frame tab.")

    # ---------- helpers ----------
    def is_object(h):
        return h is not None and h.startswith("obj:")

    def stem_for(h):
        """File stem the edit of tile h is saved under (palette variant or general)."""
        if is_object(h):
            return h
        if variant_var.get() and state["pal_key"]:
            kind, pal = state["pal_key"]
            return "%s_%s%d" % (h, kind, pal)
        return h

    def target_picture_size(h):
        """The (width, height) in pixels a picture for `h` should have at the current tile-size setting:
        a tile is always square (8*scale); an object is (width, height) - each plus 2*margin when it has
        overflow - times scale, which is generally NOT square. Getting this wrong (e.g. always forcing a
        square size) squishes/stretches an object's artwork - this is what Import PNG and the sheet
        export/import used to do for objects before this existed."""
        scale = scale_var.get()
        if is_object(h):
            obj = objects.get(h[4:])
            if obj is not None:
                return obj.native_size(scale)
        return (8 * scale, 8 * scale)

    def current_image(h, create=True):
        stem = stem_for(h)
        if stem in work:
            return work[stem]
        if is_object(h):
            obj = objects.get(h[4:])
            if obj is None:
                return None
            if create:
                work[stem] = obj.picture.copy()
                return work[stem]
            return obj.picture
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
        merged = {k: v for k, v in skins.items()}
        merged.update({k: v for k, v in work.items() if not k.startswith("obj:")})
        return merged

    def objects_view():
        """The objects with their unsaved pictures, for the preview."""
        view = {}
        for name, obj in objects.items():
            edited = work.get("obj:" + name)
            if edited is None:
                view[name] = obj
            else:
                view[name] = Obj(obj.name, obj.layer, obj.width, obj.height, obj.tiles, edited, obj.margin, obj.overflow)
        return view

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
        for name in sorted(objects):
            layer_ok = only_var.get() in ("all", "skin") or only_var.get() == objects[name].layer
            if layer_ok and only_var.get() != "new":
                order.append("obj:" + name)
                listbox.insert("end", "%s [group] %s  %dx%d" % ("*" if "obj:" + name in work else "\u2713", name, objects[name].width, objects[name].height))
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
        if is_object(h):
            overflow_var.set(1 if objects[h[4:]].overflow else 0)
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
        t = None if is_object(h) else tiles.get(h)
        if is_object(h) and h[4:] in objects:
            o = objects[h[4:]]
            ttk.Label(swatches, text="group of %d tiles, %dx%d pixels, picture %dx%d" % (len(o.tiles), o.width, o.height, o.picture.width, o.picture.height)).pack(side="left")
            ttk.Checkbutton(swatches, text="overflow (paint beyond the tiles)", variable=overflow_var, command=toggle_overflow).pack(side="left", padx=8)
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
        z = max(1, 512 // max(img.width, img.height))
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

    def toggle_overflow():
        h = state["hash"]
        if is_object(h) and h[4:] in objects:
            objects[h[4:]].overflow = bool(overflow_var.get())
            save_object(pack_dir, objects[h[4:]])
            redraw_frame()

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
        z = max(1, 512 // max(img.width, img.height))
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
        if is_object(h):
            status.set("For a group: delete it and group the tiles again to start over.")
            return
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
            size = target_picture_size(h)  # square for a tile, but an object is usually NOT square
            work[stem] = Image.open(path).convert("RGBA").resize(size, Image.LANCZOS)
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
        if is_object(h):
            if messagebox.askyesno("Delete group", "Delete this group and its picture? (the tiles keep their own skins)"):
                work.pop(h, None)
                delete_object(pack_dir, h[4:])
                objects.pop(h[4:], None)
                state["hash"] = None
                redraw_tile()
                redraw_frame()
                fill_list()
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
            if stem.startswith("obj:"):
                obj = objects[stem[4:]]
                obj.picture = img.copy()
                save_object(pack_dir, obj)
                continue
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
    ttk.Button(fbar, text="Reload captures", command=lambda: reload_captures(True)).pack(side="left", padx=2)
    ttk.Label(fbar, text="  zoom").pack(side="left")
    zoom_box = ttk.Combobox(fbar, state="readonly", width=4, values=["2", "3", "4"])
    zoom_box.set("3")
    zoom_box.pack(side="left")
    mode_var = tk.StringVar(value="paint")
    ttk.Radiobutton(fbar, text="Paint", variable=mode_var, value="paint").pack(side="left", padx=(12, 2))
    ttk.Radiobutton(fbar, text="Select tiles (click one, or drag a rectangle)", variable=mode_var, value="select").pack(side="left", padx=2)
    ttk.Button(fbar, text="Group selected", command=lambda: group_selected()).pack(side="left", padx=4)
    ttk.Button(fbar, text="Clear selection", command=lambda: clear_selection()).pack(side="left", padx=2)
    ttk.Button(fbar, text="Auto-group sprites", command=lambda: do_auto_group()).pack(side="left", padx=2)
    fbar2 = ttk.Frame(frame_tab)
    fbar2.pack(fill="x")
    show_sprites_var = tk.IntVar(value=1)
    show_bg_var = tk.IntVar(value=1)
    ttk.Label(fbar2, text="Show:").pack(side="left")
    ttk.Checkbutton(fbar2, text="Sprites", variable=show_sprites_var, command=lambda: redraw_frame()).pack(side="left", padx=(2, 8))
    ttk.Checkbutton(fbar2, text="Background", variable=show_bg_var, command=lambda: redraw_frame()).pack(side="left")
    ttk.Label(fbar2, text="  - hide the one you don't want to click, so a click/drag can never land on it by accident",
             foreground="#555").pack(side="left")
    fcanvas = tk.Canvas(frame_tab, bg="#202020", highlightthickness=0)
    fcanvas.pack(fill="both", expand=True, pady=4)
    fphoto = {}

    def hit_layers():
        """Which layers clicks/drags are allowed to hit, matching what's currently shown - sprites checked
        first when both are shown, same order cell_at() always used."""
        layers = []
        if show_sprites_var.get():
            layers.append(1)
        if show_bg_var.get():
            layers.append(0)
        return tuple(layers)

    def redraw_frame():
        if not captures:
            fcanvas.delete("all")
            fcanvas.create_text(300, 200, fill="#aaa", text="No captures yet.\nRun the game, press / (skin layer on), then ' to capture.")
            return
        cap = captures[state["capture"]]
        scale = scale_var.get()
        img = compose(cap, all_skins(), scale, objects_view())
        img = hide_layers(cap, img, scale, bool(show_sprites_var.get()), bool(show_bg_var.get()), objects_view())
        z = int(zoom_box.get())
        f = z / scale
        shown = img.resize((int(img.width * f), int(img.height * f)), Image.NEAREST if f >= 1 else Image.BOX)
        fphoto["img"] = ImageTk.PhotoImage(shown)
        fcanvas.delete("all")
        fcanvas.create_image(0, 0, anchor="nw", image=fphoto["img"])
        for m in match_objects(cap, objects_view()):      # thin outline around every found group
            c0 = [cap.cells[i] for i in m.members]
            fcanvas.create_rectangle(min(c["x"] for c in c0) * z, min(c["y"] for c in c0) * z,
                                     (max(c["x"] for c in c0) + 8) * z, (max(c["y"] for c in c0) + 8) * z, outline="#40ff40")
        for i in selection:
            c = cap.cells[i]
            fcanvas.create_rectangle(c["x"] * z, c["y"] * z, (c["x"] + 8) * z, (c["y"] + 8) * z, outline="#ffff00", width=2)

    drag = {}
    kDragThreshold = 4  # pixels of mouse movement below this: treat as a click, not a drag

    def on_select_mouse(event, phase):
        cap = captures[state["capture"]]
        z = int(zoom_box.get())
        if phase == "down":
            drag["start"] = (event.x, event.y)
            drag["moved"] = False
        if "start" not in drag:
            return
        x0, y0 = drag["start"]
        if abs(event.x - x0) > kDragThreshold or abs(event.y - y0) > kDragThreshold:
            drag["moved"] = True
        fcanvas.delete("drag")
        if drag["moved"]:
            fcanvas.create_rectangle(x0, y0, event.x, event.y, outline="#ffff00", dash=(3, 3), tags="drag")
        if phase == "up":
            if drag["moved"]:
                # a dragged rectangle ADDS the tiles it covers (one layer - see filter_one_layer) to the
                # selection, so several drags (or a drag plus single clicks) can build up one group step by step
                found = cells_in_rect(cap, min(x0, event.x) // z, min(y0, event.y) // z, max(x0, event.x) // z, max(y0, event.y) // z)
                found = {i for i in found if cap.cells[i]["s"] in hit_layers()}  # never a hidden layer
                selection.update(filter_one_layer(cap, found))
            else:
                # a plain click (no real drag) toggles the exact tile under the cursor - the precise way to pick
                # one specific sprite among several identical-looking ones, or to drop a wrongly included tile
                hit = cell_at(cap, event.x // z, event.y // z, layers=hit_layers())
                if hit:
                    i = hit[0]
                    (selection.discard if i in selection else selection.add)(i)
            drag.clear()
            n_sprite = sum(1 for i in selection if cap.cells[i]["s"])
            n_bg = len(selection) - n_sprite
            status.set("%d tile(s) selected (%s). Click a tile to add/remove just that one, drag a rectangle for "
                       "an area, then 'Group selected'." % (len(selection), "sprites" if n_sprite else "background" if n_bg else "none"))
            redraw_frame()

    def clear_selection():
        selection.clear()
        redraw_frame()
        status.set("Selection cleared.")

    def group_selected():
        if not captures or not selection:
            status.set("Nothing selected: switch to 'Select tiles', then click each tile of the object (or drag a "
                       "rectangle around it), and 'Group selected'.")
            return
        cap = captures[state["capture"]]
        from tkinter import simpledialog
        first = sorted(selection)[0]
        name = simpledialog.askstring("New group", "Name of the group:", initialvalue="obj_" + cap.cells[first]["hash"][:6], parent=root)
        if not name:
            return
        name = "".join(ch for ch in name if ch.isalnum() or ch in "_-") or "obj"
        overflow = messagebox.askyesno("Overflow", "Let the picture extend beyond the tiles' own pixels (bigger glow/outline)?\n\nNo = the picture only replaces the visible pixels of the tiles.")
        margin = simpledialog.askinteger("Margin", "Extra NES pixels of picture on every side (0-16):", initialvalue=4, minvalue=0, maxvalue=16, parent=root) if overflow else 0
        try:
            obj = object_from_cells(cap, sorted(selection), scale_var.get(), name, margin or 0, overflow)
        except ValueError as e:
            messagebox.showerror("Cannot group", str(e))
            return
        objects[name] = obj
        save_object(pack_dir, obj)
        selection.clear()
        fill_list()
        select_hash("obj:" + name)
        status.set("Group '%s' made (%d tiles). Paint it in the Tile tab or on the picture; the game already uses it." % (name, len(obj.tiles)))
        redraw_frame()

    def do_auto_group():
        if not captures:
            return
        cap = captures[state["capture"]]
        known = {i for m in match_objects(cap, objects_view()) for i in m.members}
        made = 0
        for members in auto_group(cap):
            if all(i in known for i in members):
                continue
            name = "auto_%s_%d" % (cap.cells[members[0]]["hash"][:6], len(objects))
            try:
                obj = object_from_cells(cap, members, scale_var.get(), name)
            except ValueError:
                continue
            objects[name] = obj
            save_object(pack_dir, obj)
            made += 1
        fill_list()
        redraw_frame()
        status.set("Auto-group made %d groups (touching sprite tiles of one palette). Check them in the Frame tab (green outline); "
                   "wrong ones can be deleted, or select tiles by hand." % made)

    def on_frame_mouse(event, first):
        if not captures:
            return
        if mode_var.get() == "select":
            return
        cap = captures[state["capture"]]
        z = int(zoom_box.get())
        nes_x, nes_y = event.x // z, event.y // z
        visible_layers = {"s" if v else "b" for v in hit_layers()}
        matches = [m for m in match_objects(cap, objects_view()) if m.obj.layer in visible_layers]
        hit_match = match_at(cap, matches, nes_x, nes_y)
        if hit_match is not None:
            key = "obj:" + hit_match.obj.name
            if key != state["hash"]:
                select_hash(key)
            img = current_image(key)
            px, py = picture_position(hit_match, nes_x, nes_y, (event.x % z) / z, (event.y % z) / z)
            if apply_tool(img, key, px, py, first):
                redraw_frame()
                fill_list_keep()
            return
        hit = cell_at(cap, nes_x, nes_y, layers=hit_layers())
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

    def frame_down(e):
        on_select_mouse(e, "down") if mode_var.get() == "select" else on_frame_mouse(e, True)

    def frame_move(e):
        on_select_mouse(e, "move") if mode_var.get() == "select" else on_frame_mouse(e, False)

    fcanvas.bind("<Button-1>", frame_down)
    fcanvas.bind("<B1-Motion>", frame_move)
    fcanvas.bind("<ButtonRelease-1>", lambda e: on_select_mouse(e, "up") if mode_var.get() == "select" else None)

    def fill_captures():
        cap_box.configure(values=["frame %d (%d tiles)" % (c.frame_number, len(c.cells)) for c in captures])
        if captures:
            cap_box.current(state["capture"])

    def on_cap_changed(_=None):
        state["capture"] = cap_box.current()
        redraw_frame()

    def reload_captures(announce=False):
        """Re-scans capture/ for files this window hasn't loaded yet (e.g. you pressed ' in the emulator,
        or ran nes_extract_chr.py, after this editor was already open - captures/skins/objects are only
        read once at startup otherwise). Keeps looking at the same capture if it still exists."""
        on_disk = capture_paths(pack_dir)
        known = [c.path for c in captures]
        if on_disk == known and not announce:
            return False
        current_path = captures[state["capture"]].path if captures else None
        captures[:] = load_captures(pack_dir)
        tiles.clear()
        tiles.update(collect_tiles(captures))
        if current_path and any(c.path == current_path for c in captures):
            state["capture"] = next(i for i, c in enumerate(captures) if c.path == current_path)
        else:
            state["capture"] = len(captures) - 1 if captures else 0  # jump to the newest one
        fill_captures()
        fill_list_keep()
        redraw_frame()
        if announce:
            added = len(captures) - len(known)
            status.set("Reloaded: %d capture(s) (%+d new)." % (len(captures), added) if added else
                       "Reloaded: no new captures found (press ' in the emulator, or run nes_extract_chr.py, first).")
        return True

    def poll_captures():
        reload_captures(False)
        root.after(1000, poll_captures)

    cap_box.bind("<<ComboboxSelected>>", on_cap_changed)
    zoom_box.bind("<<ComboboxSelected>>", lambda e: redraw_frame())
    notebook.bind("<<NotebookTabChanged>>", lambda e: redraw_frame() if notebook.index("current") == 1 else redraw_tile())

    # ---------- bottom ----------
    def sheet_export():
        path = filedialog.asksaveasfilename(defaultextension=".png", title="Save sheet for an AI upscaler / image editor")
        if not path:
            return
        # Groups are excluded: the sheet uses one uniform square cell per entry, but a group's picture is
        # usually not square (e.g. a wide, flat enemy) - forcing it into a square cell would stretch or
        # squash it. Paint/import a group's own picture directly (Tile tab: Import PNG..., or paint on it).
        tile_only = [h for h in order if not h.startswith("obj:")]
        skipped = len(order) - len(tile_only)
        layout = export_sheet(tiles, all_skins(), path, tile_only)
        with open(os.path.splitext(path)[0] + ".layout.json", "w", encoding="utf-8") as f:
            json.dump(layout, f)
        status.set("Sheet written: %s (+ .layout.json)%s. Process it, then use 'Import sheet'." %
                   (path, " - %d group(s) skipped, they don't fit a square grid" % skipped if skipped else ""))

    def sheet_import():
        path = filedialog.askopenfilename(title="Processed sheet (the .layout.json must sit next to the original name)")
        if not path:
            return
        layout_path = filedialog.askopenfilename(title="The .layout.json written by 'Export sheet'")
        if not layout_path:
            return
        with open(layout_path, encoding="utf-8") as f:
            layout = json.load(f)
        # A .layout.json from before groups were excluded from sheets may still list "obj:" entries;
        # skip them here too rather than importing a group at the wrong (forced-square) size.
        skipped = sum(1 for h in layout.get("order", []) if h.startswith("obj:"))
        layout["order"] = [h for h in layout.get("order", []) if not h.startswith("obj:")]
        pieces = import_sheet(path, layout, 8 * scale_var.get())
        for h, img in pieces.items():
            push_undo(h)
            work[h] = img
        status.set("Imported %d tiles (not saved yet)%s." %
                   (len(pieces), " - %d group(s) in that layout skipped" % skipped if skipped else ""))
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
    poll_captures()
    root.mainloop()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    run_ui(sys.argv[1])
