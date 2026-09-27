#!/usr/bin/env python3
"""Extracts every tile of a ROM's CHR data directly from the .nes file - no playing needed.

    python3 tools/nes_extract_chr.py game.nes build/skins/game

Writes one or more synthetic captures (capture/cap_chr<N>.json + .png, up to 960 tiles per page - a full
NES screen's worth, 32x30 tiles) in exactly the format tools/nes_skin_editor.py already reads: every
distinct 8x8 tile stored in the ROM's CHR-ROM, i.e. every tile that could ever appear on screen across
every bank a mapper might switch in, without running a single frame of the game. Open the pack in the
skin editor like any other capture: pick a tile from the list (Tile tab) and paint it. The tile hash is
computed exactly the way the running emulator computes it (NES::HdLayer::HashBytes), so anything painted
here shows up in the real game the moment that tile is actually drawn - even for a tile the game only
shows deep in a level you never captured live.

What this does NOT give you: **groups** (which tiles combine into one on-screen character, e.g. a 2x2
ship) are not stored anywhere in the ROM - that composition only exists as a pattern of OAM writes the
game's code makes while it runs, so grouping still needs at least one real, live capture (press ' in the
emulator) to find. Auto-group / Select tiles in the skin editor works on real captures as before; this
tool only pre-populates the *individual* tile list so painting does not have to wait for a tile to
happen to appear on screen first.

Only works for CHR-ROM games (graphics baked into the cartridge - the common case, including every test
ROM this project ships). CHR-RAM games (the game draws its own graphics into video memory at runtime,
common on some MMC1/UNROM boards) have nothing to extract until the game actually runs; this tool
detects that (CHR-ROM size 0 in the header) and says so instead of writing an empty, misleading pack.
"""
import json
import os
import sys

from PIL import Image

TILES_PER_PAGE_ROW = 32   # 32 * 8 = 256 - exactly the NES screen width
TILES_PER_PAGE_COL = 30   # 30 * 8 = 240 - exactly the NES screen height
TILES_PER_PAGE = TILES_PER_PAGE_ROW * TILES_PER_PAGE_COL   # 960

# Same constants as NES::HdLayer::HashBytes (NES/HdLayer.cpp) - must match exactly, or a tile painted
# here would be saved under a different file name than the one the running emulator looks up.
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

# A fixed, readable grayscale ramp for tiles that have no known in-game palette yet (index 0 is
# transparent for these synthetic sprite cells, so its colour is never actually drawn onto the picture -
# kept only so every cell has the same 4-entry shape the real format uses).
RAMP = [[24, 24, 24], [110, 110, 110], [180, 180, 180], [255, 255, 255]]
BACKGROUND = (24, 24, 24)


def hash_bytes(data):
    """The same 64-bit FNV-1a hash NES::HdLayer::HashBytes computes over a tile's 16 pattern bytes."""
    h = FNV_OFFSET
    for b in data:
        h = (h ^ b) & MASK64
        h = (h * FNV_PRIME) & MASK64
    return h


def hash_name(h):
    """16 lowercase hex digits, matching NES::HdLayer::HashName."""
    return "%016x" % h


def pixel_index(data, x, y):
    """Palette index 0-3 of pixel (x, y) of a tile given as its 16 raw pattern-table bytes."""
    return ((data[y] >> (7 - x)) & 1) | (((data[y + 8] >> (7 - x)) & 1) << 1)


def read_ines_header(data):
    """Parses the 16-byte iNES header - mirrors INES.cpp exactly (mapper number, trainer, CHR-ROM size;
    CHR-ROM size 0 means CHR-RAM, nothing to extract). Raises ValueError if this isn't an iNES file."""
    if len(data) < 16 or data[0:4] != b"NES\x1a":
        raise ValueError("not an iNES ROM (missing the 'NES\\x1a' header)")
    prg_size = 16384 * data[4]
    chr_size = 8192 * data[5]
    trainer = (data[6] & 0x04) != 0
    lmapper = (data[6] & 0xF0) >> 4
    hmapper = (data[7] & 0xF0) >> 4
    mapper = (hmapper << 4) | lmapper
    return {"prg_size": prg_size, "chr_size": chr_size, "trainer": trainer, "mapper": mapper}


def extract_chr(rom_path):
    """Reads `rom_path` and returns (header, chr_bytes). Raises ValueError, with a message fit to show
    the user directly, for a CHR-RAM ROM, a non-iNES file, or a file shorter than its header claims."""
    with open(rom_path, "rb") as f:
        data = f.read()
    header = read_ines_header(data)
    if header["chr_size"] == 0:
        raise ValueError("this ROM has no CHR-ROM (it uses CHR-RAM: the game draws its own graphics into "
                         "video memory at runtime) - there is nothing to extract without running it")
    prg_start = 16 + (512 if header["trainer"] else 0)
    chr_start = prg_start + header["prg_size"]
    chr_end = chr_start + header["chr_size"]
    if chr_end > len(data):
        raise ValueError("the file is shorter than its header says (%d bytes needed, %d present) - it "
                         "may be truncated, or not really an iNES ROM" % (chr_end, len(data)))
    return header, data[chr_start:chr_end]


def build_pages(chr_bytes):
    """Splits the CHR data into 16-byte tiles and lays them out into pages of up to 960 (32x30 - a full
    NES screen), each returned as (RGB image, cells) ready to write as a synthetic capture. A trailing
    partial tile (chr_bytes not a multiple of 16 - should not happen for a real ROM) is dropped."""
    tiles = [chr_bytes[i:i + 16] for i in range(0, len(chr_bytes) - len(chr_bytes) % 16, 16)]
    pages = []
    for page_start in range(0, len(tiles), TILES_PER_PAGE):
        page_tiles = tiles[page_start:page_start + TILES_PER_PAGE]
        image = Image.new("RGB", (256, 240), tuple(BACKGROUND))
        px = image.load()
        cells = []
        for n, data in enumerate(page_tiles):
            col, row = n % TILES_PER_PAGE_ROW, n // TILES_PER_PAGE_ROW
            x, y = col * 8, row * 8
            opaque = 0
            for ty in range(8):
                for tx in range(8):
                    idx = pixel_index(data, tx, ty)
                    if idx:
                        px[x + tx, y + ty] = tuple(RAMP[idx])
                        opaque += 1
            cells.append({"x": x, "y": y, "s": 1, "fh": 0, "fv": 0, "pal": 0, "hash": hash_name(hash_bytes(data)),
                         "bytes": data.hex(), "vis": opaque, "rgb": RAMP})
        pages.append((image, cells))
    return pages


def write_pages(pack_dir, pages):
    """Writes capture/cap_chr<N>.json + .png for every page (silently overwrites files of the same
    name from an earlier run of this tool). Returns the list of stems written, in page order."""
    folder = os.path.join(pack_dir, "capture")
    os.makedirs(folder, exist_ok=True)
    written = []
    for n, (image, cells) in enumerate(pages):
        stem = "cap_chr%d" % n
        image.save(os.path.join(folder, stem + ".png"))
        # Negative frame numbers only ever come from this tool - real captures always have frame >= 0 -
        # so they are easy to tell apart from real gameplay captures at a glance in the editor's dropdown.
        with open(os.path.join(folder, stem + ".json"), "w", encoding="utf-8") as f:
            json.dump({"frame": -(n + 1), "cells": cells}, f)
        written.append(stem)
    return written


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    rom_path, pack_dir = argv[1], argv[2]
    try:
        header, chr_bytes = extract_chr(rom_path)
    except (OSError, ValueError) as e:
        print("Could not extract: %s" % e)
        return 1
    pages = build_pages(chr_bytes)
    total_tiles = sum(len(cells) for _, cells in pages)
    written = write_pages(pack_dir, pages)
    print("Mapper %d, %d bytes of CHR-ROM = %d tiles, written as %d page(s) to %s:" %
         (header["mapper"], header["chr_size"], total_tiles, len(pages), os.path.join(pack_dir, "capture")))
    for stem in written:
        print("  " + stem)
    print("Open with: python3 tools/nes_skin_editor.py %s" % pack_dir)
    print("Note: this lists every individual tile, but not which tiles combine into one on-screen "
         "character (a group) - that still needs at least one real capture (' in the emulator).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
