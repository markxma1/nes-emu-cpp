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
