#!/usr/bin/env python3
"""Checks the window-less logic of tools/nes_skin_editor.py."""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import nes_skin_editor as e
from PIL import Image

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


# a tile: column x=0 uses index 1 in all rows, pixel (7,0) has index 2
data = bytes([0x80] * 8 + [0x01] + [0] * 7)
check(e.pixel_index(data, 0, 5) == 1 and e.pixel_index(data, 7, 0) == 2 and e.pixel_index(data, 3, 3) == 0, "pixel_index")
rgb = [[0, 0, 0], [255, 0, 0], [0, 255, 0], [0, 0, 255]]
img = e.tile_rgba(data, rgb, True, 2)
check(img.size == (16, 16) and img.getpixel((0, 0)) == (255, 0, 0, 255) and img.getpixel((8, 8))[3] == 0, "tile_rgba sprite: colours and transparency")
check(img.getpixel((15, 1)) == (0, 255, 0, 255), "tile_rgba: index 2 pixel at the right edge, enlarged")
flipped = e.tile_rgba(data, rgb, True, 1, flip_h=True)
check(flipped.getpixel((7, 4)) == (255, 0, 0, 255) and flipped.getpixel((0, 0)) == (0, 255, 0, 255), "tile_rgba: horizontal flip")

with tempfile.TemporaryDirectory() as d:
    # build a capture like the emulator writes it: one flipped sprite at (10,20) over a black background
    frame = Image.new("RGB", (256, 240), (0, 0, 0))
    cell = {"x": 10, "y": 20, "s": 1, "fh": 1, "fv": 0, "pal": 2, "hash": "aaaa", "bytes": data.hex(), "vis": 9, "rgb": rgb}
    for y in range(8):
        for x in range(8):
            idx = e.pixel_index(data, 7 - x, y)
            if idx:
                frame.putpixel((10 + x, 20 + y), tuple(rgb[idx]))
    os.makedirs(os.path.join(d, "capture"))
    frame.save(os.path.join(d, "capture", "cap_5.png"))
    with open(os.path.join(d, "capture", "cap_5.json"), "w") as f:
        json.dump({"frame": 5, "cells": [cell]}, f)
    caps = e.load_captures(d)
    check(len(caps) == 1 and caps[0].frame_number == 5, "capture loads")
    cap = caps[0]
    check(len(cap.visible(0)) == 9, "all 9 opaque pixels are visible (got %d)" % len(cap.visible(0)))
    check(e.cell_at(cap, 10 + 7, 20) == (0, 7, 0), "cell_at finds the sprite at its flipped position")
    check(e.cell_at(cap, 100, 100) is None, "cell_at: nothing on empty background")
    tiles = e.collect_tiles(caps)
    check(list(tiles) == ["aaaa"] and tiles["aaaa"]["uses"][("s", 2)] == 1, "collect_tiles")

    # a wrong tile (frame shows something else) is not visible
    other = Image.new("RGB", (256, 240), (9, 9, 9))
    check(e.cell_visibility(cell, other) == set(), "a cell that does not match the picture is not visible")

    # composing: skin = white tile with a red pixel at its top-left (unflipped); flipped sprite -> red at the right
    skin = Image.new("RGBA", (16, 16), (255, 255, 255, 255))
    skin.putpixel((0, 0), (255, 0, 0, 255))
    out = e.compose(cap, {"aaaa": skin}, 2)
    check(out.size == (512, 480), "compose size")
    check(out.getpixel(((10 + 7) * 2 + 1, 20 * 2)) == (255, 0, 0), "compose: skin pixel lands on the mirrored side")
    check(out.getpixel((10 * 2, 20 * 2)) == (255, 255, 255), "compose: visible pixel painted with the skin")
    check(out.getpixel((10 * 2 + 4, 20 * 2 + 3)) == (0, 0, 0), "compose: pixels that are transparent in the tile keep the background")
    # palette specific skin wins over the general one
    green = Image.new("RGBA", (16, 16), (0, 255, 0, 255))
    out2 = e.compose(cap, {"aaaa": skin, "aaaa_s2": green}, 2)
    check(out2.getpixel((10 * 2, 20 * 2)) == (0, 255, 0), "compose: palette variant is preferred")
    check(e.find_skin_key({"aaaa_s1": skin}, "aaaa", True, 2) is None, "no skin for a different palette variant")

    # save / load / delete
    e.save_skin(d, "aaaa", skin)
    check(list(e.load_skins(d)) == ["aaaa"], "save_skin + load_skins")
    e.delete_skin(d, "aaaa")
    check(e.load_skins(d) == {}, "delete_skin")

    # sheet round trip, also when an upscaler doubled the sheet
    tiles2 = {"aaaa": tiles["aaaa"]}
    sheet = os.path.join(d, "sheet.png")
    layout = e.export_sheet(tiles2, {}, sheet, ["aaaa"], cell=32, columns=4)
    big = Image.open(sheet)
    big.resize((big.width * 2, big.height * 2), Image.NEAREST).save(sheet)
    pieces = e.import_sheet(sheet, layout, 16)
    check(pieces["aaaa"].size == (16, 16), "import_sheet size")
    r, g, b = pieces["aaaa"].getpixel((0, 8))[:3]
check(r > 200 and g < 60 and b < 60, "import_sheet: colours survive a 2x enlarged sheet (got %s)" % ((r, g, b),))

# ---- multiple captures: load order and picking up new ones made after the fact ----
with tempfile.TemporaryDirectory() as d:
    os.makedirs(os.path.join(d, "capture"))

    def write_capture(stem, frame_number):
        Image.new("RGB", (256, 240), (0, 0, 0)).save(os.path.join(d, "capture", stem + ".png"))
        with open(os.path.join(d, "capture", stem + ".json"), "w") as f:
            json.dump({"frame": frame_number, "cells": []}, f)

    # frame numbers whose filenames do NOT sort the same way as their numeric value ("cap_100" < "cap_20"
    # < "cap_9" as plain text) - load_captures must still show them in real chronological order
    write_capture("cap_100", 100)
    write_capture("cap_9", 9)
    write_capture("cap_20", 20)
    order_caps = e.load_captures(d)
    check([c.frame_number for c in order_caps] == [9, 20, 100],
          "load_captures: real captures sort by their actual frame number, not by filename text (got %r)" % [c.frame_number for c in order_caps])

    # synthetic pages (negative frame numbers from nes_extract_chr.py) sort by page, not by raw frame number
    # (-2 < -1, so a plain ascending sort would show page 1 before page 0)
    write_capture("cap_chr1", -2)
    write_capture("cap_chr0", -1)
    order_caps2 = e.load_captures(d)
    synthetic = [c.frame_number for c in order_caps2 if c.frame_number < 0]
    check(synthetic == [-1, -2], "load_captures: synthetic pages sort in page order (page 0 = frame -1 first), got %r" % synthetic)

    # capture_paths() (what the editor's live-reload polls) reflects a file added after the first load
    before = e.capture_paths(d)
    write_capture("cap_500", 500)
    after = e.capture_paths(d)
    check(len(after) == len(before) + 1, "capture_paths: a capture written after the fact is found on the next scan")
    check(e.load_captures(d)[-1].frame_number == 500, "load_captures: picks up the newly written capture too, in its right chronological place")

# ---- objects: groups of tiles with one picture ----
def make_cell(x, y, hash_name, fh=0, fv=0, s=1, pal=0):
    tile = bytes([0xFF] * 8 + [0x00] * 8)   # solid index 1
    return {"x": x, "y": y, "s": s, "fh": fh, "fv": fv, "pal": pal, "hash": hash_name, "bytes": tile.hex(),
            "vis": 64, "rgb": [[0, 0, 0], [200, 200, 200], [0, 0, 0], [0, 0, 0]]}


# ---- selecting tiles in the Frame tab (cells_in_rect / filter_one_layer): the actual bug reported by the
# user - a rectangle dragged tightly around a sprite also touches background pixels peeking through its
# corners (a sprite is rarely a filled rectangle), so grouping used to fail with "must be all sprites or
# background" even though the user only meant to select the sprite. ----
with tempfile.TemporaryDirectory() as d:
    # a diamond-shaped 8x8 sprite (only the middle 4 pixels of each half-row opaque, corners transparent) at
    # (20, 20), sitting over a background of fully opaque tiles (every background pixel "visible" everywhere,
    # as real NES background tiles always are - there is no transparent index 0 for the background layer).
    diamond = bytes([0b00011000, 0b00111100, 0b01111110, 0b11111111,
                     0b11111111, 0b01111110, 0b00111100, 0b00011000] + [0] * 8)
    frame = Image.new("RGB", (256, 240), (10, 10, 10))
    for y in range(8):
        for x in range(8):
            if e.pixel_index(diamond, x, y):
                frame.putpixel((20 + x, 20 + y), (200, 200, 200))
    bg_cells = [make_cell(bx, by, "bg", s=0) for bx in (16, 24) for by in (16, 24)]
    for c in bg_cells:
        for y in range(8):
            for x in range(8):
                frame.putpixel((c["x"] + x, c["y"] + y), (200, 200, 200))  # background fills the whole tile
    sprite_cell = {**make_cell(20, 20, "diamond"), "bytes": diamond.hex()}
    os.makedirs(os.path.join(d, "capture"))
    frame.save(os.path.join(d, "capture", "cap_1.png"))
    with open(os.path.join(d, "capture", "cap_1.json"), "w") as f:
        json.dump({"frame": 1, "cells": bg_cells + [sprite_cell]}, f)
    cap = e.load_captures(d)[0]

    # a rectangle drawn tightly around the sprite's own 8x8 box still overlaps the background tiles behind
    # its transparent corners
    tight = e.cells_in_rect(cap, 20, 20, 27, 27)
    check(any(cap.cells[i]["s"] for i in tight) and any(not cap.cells[i]["s"] for i in tight),
          "cells_in_rect: a tight box around the sprite still picks up background behind its transparent corners (reproduces the reported bug)")
    only_sprite = e.filter_one_layer(cap, tight)
    check(only_sprite == {len(bg_cells)}, "filter_one_layer: keeps only the sprite when the selection has one (drops the background bleed)")

    # a rectangle over an area with no sprite keeps the background as before
    bg_only = e.cells_in_rect(cap, 16, 16, 31, 31)
    bg_only.discard(len(bg_cells))  # exclude the sprite itself for this check
    check(e.filter_one_layer(cap, bg_only) == bg_only, "filter_one_layer: a selection with no sprite is left as background, unfiltered")

    # end to end: grouping the raw (unfiltered) rectangle selection fails exactly as the user saw it; grouping
    # the filtered one succeeds
    try:
        e.object_from_cells(cap, sorted(tight), 2, "raw")
        check(False, "object_from_cells: an unfiltered selection mixing sprite and background must still be rejected")
    except ValueError:
        pass
    grouped = e.object_from_cells(cap, sorted(only_sprite), 2, "fixed")
    check(grouped.width == 8 and grouped.height == 8, "object_from_cells: the filtered (sprite-only) selection groups fine")


with tempfile.TemporaryDirectory() as d:
    frame = Image.new("RGB", (256, 240), (0, 0, 0))
    # a 2x1 ship "aa","bb" at (20,30), the same ship mirrored at (100,50), and a lone "aa" at (150,150)
    cells = [make_cell(20, 30, "aa"), make_cell(28, 30, "bb"),
             make_cell(100, 50, "bb", fh=1), make_cell(108, 50, "aa", fh=1),
             make_cell(150, 150, "aa")]
    for c in cells:
        for y in range(8):
            for x in range(8):
                frame.putpixel((c["x"] + x, c["y"] + y), (200, 200, 200))
    os.makedirs(os.path.join(d, "capture"))
    frame.save(os.path.join(d, "capture", "cap_9.png"))
    with open(os.path.join(d, "capture", "cap_9.json"), "w") as f:
        json.dump({"frame": 9, "cells": cells}, f)
    cap = e.load_captures(d)[0]

    # grouping by hand and automatically
    obj = e.object_from_cells(cap, [0, 1], 2, "ship")
    check(obj.width == 16 and obj.height == 8 and len(obj.tiles) == 2 and obj.picture.size == (32, 16), "object_from_cells: size and picture")
    check(e.object_from_cells(cap, [0, 4], 2, "far").width == 138, "object_from_cells: bounding box of far apart tiles")
    try:
        e.object_from_cells(cap, [0, 1], 2, "x")
        e.object_from_cells(cap, [], 2, "x")
        check(False, "empty selection must be rejected")
    except ValueError:
        pass
    groups = e.auto_group(cap)
    check(len(groups) == 1 and groups[0] in ([0, 1], [2, 3]), "auto_group: one distinct group (ship and its mirror are the same shape), lone tile ignored: %r" % groups)

    # matching finds both orientations
    objs = {"ship": obj}
    ms = e.match_objects(cap, objs)
    check(len(ms) == 2 and {(m.fh, m.fv) for m in ms} == {(0, 0), (1, 0)}, "match_objects: normal and mirrored")
    check(sorted(i for m in ms for i in m.members) == [0, 1, 2, 3], "match_objects: the lone tile is not part of a group")

    # painting: left half red, right half green; the mirrored copy shows it mirrored
    obj.picture.paste((255, 0, 0, 255), (0, 0, 16, 16))
    obj.picture.paste((0, 255, 0, 255), (16, 0, 32, 16))
    out = e.compose(cap, {}, 2, objs)
    check(out.getpixel((20 * 2, 30 * 2)) == (255, 0, 0) and out.getpixel((28 * 2, 30 * 2)) == (0, 255, 0), "compose: object picture painted")
    check(out.getpixel((100 * 2, 50 * 2)) == (0, 255, 0) and out.getpixel((108 * 2, 50 * 2)) == (255, 0, 0), "compose: mirrored group has the picture mirrored")
    check(out.getpixel((150 * 2, 150 * 2)) == (200, 200, 200), "compose: the lone tile keeps the original")
    # a per-tile skin is ignored for tiles that are part of an object
    tile_skin = Image.new("RGBA", (16, 16), (0, 0, 255, 255))
    out2 = e.compose(cap, {"aa": tile_skin}, 2, objs)
    check(out2.getpixel((20 * 2, 30 * 2)) == (255, 0, 0) and out2.getpixel((150 * 2, 150 * 2)) == (0, 0, 255), "compose: object wins over tile skins, lone tile uses its tile skin")

    # overflow with margin paints beyond the tiles
    big = e.object_from_cells(cap, [0, 1], 2, "big", margin=2, overflow=True)
    big.picture.paste((0, 0, 255, 255), (0, 0, big.picture.width, big.picture.height))
    out3 = e.compose(cap, {}, 2, {"big": big})
    check(out3.getpixel(((20 - 2) * 2, (30 - 2) * 2)) == (0, 0, 255), "compose: overflow paints the margin")

    # click position -> position inside the picture (mirrored group is flipped)
    m0 = [m for m in ms if not m.fh][0]
    m1 = [m for m in ms if m.fh][0]
    check(e.picture_position(m0, 20, 30, 0, 0) == (0, 0), "picture_position: normal group top-left")
    check(e.picture_position(m1, 100, 50, 0, 0)[0] == 31 - 0 - 0 or e.picture_position(m1, 100, 50, 0, 0)[0] >= 30, "picture_position: mirrored group reads from the right edge")
    check(e.match_at(cap, ms, 20, 30) is m0 and e.match_at(cap, ms, 150, 150) is None, "match_at")

    # save/load round trip, text format the C++ side reads
    e.save_object(d, obj)
    loaded = e.load_objects(d)
    check(list(loaded) == ["ship"] and loaded["ship"].width == 16 and loaded["ship"].tiles[1]["hash"] == "bb", "objects save/load")
    text = obj.to_text()
    check(text.splitlines()[0] == "layer s" and "tile 8 0 bb 0 0" in text, "object text format")
    e.delete_object(d, "ship")
    check(e.load_objects(d) == {}, "delete_object")

# painting
im = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
e.paint(im, 2, 2, (1, 2, 3, 255), 2)
check(im.getpixel((3, 3)) == (1, 2, 3, 255) and im.getpixel((4, 4))[3] == 0, "paint with brush size 2")
e.flood_fill(im, 7, 7, (9, 9, 9, 255))
check(im.getpixel((0, 0)) == (9, 9, 9, 255) and im.getpixel((2, 2)) == (1, 2, 3, 255), "flood fill stops at other colours")

if failures:
    print("%d skin editor check(s) FAILED" % failures)
    sys.exit(1)
print("PASS: skin editor logic")
