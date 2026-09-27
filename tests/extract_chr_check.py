#!/usr/bin/env python3
"""Checks tools/nes_extract_chr.py.

hash_bytes()/hash_name() and the iNES offset math (trainer skip, PRG-ROM skip, CHR-ROM slice) were also
cross-checked live against the real emulator during development (a throwaway harness dumped
NES_PPU_Memory::PatternTableN right after NES_Console::LoadRom(), with zero CPU execution, and compared
it byte for byte against this module's extraction - identical, including a case with a 512-byte trainer
present). That harness was not real project code and is not kept here; this file re-checks the same
things in a way that needs no C++ build.
"""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import nes_extract_chr as x

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


# hash_bytes/hash_name: pinned against a real capture's own hash for the same (all-zero) tile bytes -
# see nes_extract_chr.py's module docstring for how this was cross-checked against the C++ side.
check(x.hash_name(x.hash_bytes(bytes(16))) == "a31e272015f12c43", "hash_bytes/hash_name: matches a known real emulator hash for an all-zero tile")
check(x.hash_bytes(bytes(16)) != x.hash_bytes(bytes([1] + [0] * 15)), "hash_bytes: different tiles hash differently")
check(len(x.hash_name(x.hash_bytes(b"anything"))) == 16, "hash_name: always 16 hex digits")

# pixel_index: same convention used throughout the project (bit 7-x of byte y = low bit, byte y+8 = high bit)
data = bytes([0x80] * 8 + [0x01] + [0] * 7)
check(x.pixel_index(data, 0, 3) == 1 and x.pixel_index(data, 7, 0) == 2 and x.pixel_index(data, 4, 4) == 0, "pixel_index")


def make_header(prg_units=1, chr_units=1, trainer=False, mapper=0):
    h = bytearray(16)
    h[0:4] = b"NES\x1a"
    h[4] = prg_units
    h[5] = chr_units
    h[6] = (0x04 if trainer else 0) | ((mapper & 0x0F) << 4)
    h[7] = (mapper & 0xF0)
    return bytes(h)


# read_ines_header: sizes, trainer flag, and the mapper number split across both bytes (low nibble in
# byte 6, high nibble in byte 7 - the same real bug class INES.cpp's own history warns about: using the
# wrong nibble/mask silently drops mapper numbers like 1 or 66).
h = x.read_ines_header(make_header(prg_units=2, chr_units=2, trainer=True, mapper=66))
check(h == {"prg_size": 32768, "chr_size": 16384, "trainer": True, "mapper": 66}, "read_ines_header: sizes/trainer/mapper (got %r)" % h)
h0 = x.read_ines_header(make_header(mapper=0))
check(h0["mapper"] == 0 and not h0["trainer"], "read_ines_header: mapper 0, no trainer")
h1 = x.read_ines_header(make_header(mapper=1))
check(h1["mapper"] == 1, "read_ines_header: mapper 1 (low nibble only) is not dropped")
try:
    x.read_ines_header(b"not an ines file at all")
    check(False, "read_ines_header must reject a file with no iNES magic")
except ValueError:
    pass

with tempfile.TemporaryDirectory() as d:
    def write_rom(name, prg, chr_data, trainer_data=b"", **header_kwargs):
        path = os.path.join(d, name)
        with open(path, "wb") as f:
            f.write(make_header(prg_units=len(prg) // 16384, chr_units=len(chr_data) // 8192,
                                trainer=bool(trainer_data), **header_kwargs))
            f.write(trainer_data)
            f.write(prg)
            f.write(chr_data)
        return path

    prg = bytes(range(256)) * 64  # 16384 bytes, deterministic filler
    chr_data = bytes((i * 37 + 5) % 256 for i in range(8192))  # deterministic, non-trivial pattern

    # no trainer: extract_chr returns exactly the CHR bytes, unchanged
    rom = write_rom("plain.nes", prg, chr_data)
    header, extracted = x.extract_chr(rom)
    check(extracted == chr_data, "extract_chr: CHR bytes round-trip without a trainer")

    # trainer present: must still land on exactly the same CHR bytes (a wrong skip would shift everything)
    rom_t = write_rom("trained.nes", prg, chr_data, trainer_data=bytes(range(256)) * 2)
    header_t, extracted_t = x.extract_chr(rom_t)
    check(extracted_t == chr_data, "extract_chr: trainer is skipped correctly")

    # CHR-RAM (chr_size 0): a clear, specific error, not a crash or an empty/misleading result
    rom_ram = write_rom("charram.nes", prg, b"")
    try:
        x.extract_chr(rom_ram)
        check(False, "extract_chr must reject a CHR-RAM ROM")
    except ValueError as e:
        check("CHR-RAM" in str(e), "extract_chr: CHR-RAM error message names the actual reason")

    # truncated file: header claims more data than is actually present
    path = os.path.join(d, "short.nes")
    with open(path, "wb") as f:
        f.write(make_header(prg_units=1, chr_units=1))
        f.write(prg)  # CHR data missing entirely
    try:
        x.extract_chr(path)
        check(False, "extract_chr must reject a file shorter than its header claims")
    except ValueError:
        pass

    # build_pages: tile count, hash/bytes correctness, and exact grid position of a chosen tile
    pages = x.build_pages(chr_data)
    check(len(pages) == 1, "build_pages: 8KB CHR (512 tiles) fits on a single page")
    image, cells = pages[0]
    check(len(cells) == 512, "build_pages: one cell per 16-byte tile")
    check(cells[0]["bytes"] == chr_data[0:16].hex(), "build_pages: first cell holds the first tile's bytes")
    check(cells[5]["hash"] == x.hash_name(x.hash_bytes(chr_data[5 * 16:5 * 16 + 16])), "build_pages: hash matches hash_bytes of that tile")
    check((cells[0]["x"], cells[0]["y"]) == (0, 0), "build_pages: tile 0 at the grid origin")
    check((cells[32]["x"], cells[32]["y"]) == (0, 8), "build_pages: tile 32 starts row 2 (32 tiles per row)")
    check(image.size == (256, 240), "build_pages: page image is exactly one NES screen")
    # the rendered pixel at a known opaque position matches the grayscale ramp for its palette index
    idx = x.pixel_index(chr_data[0:16], 3, 2)
    if idx:
        check(image.getpixel((3, 2)) == tuple(x.RAMP[idx]), "build_pages: rendered pixel matches the ramp colour for its index")

    # pagination boundary: exactly TILES_PER_PAGE + 1 tiles must split 960 / 1 across two pages
    big_chr = bytes((i * 13 + 1) % 256 for i in range((x.TILES_PER_PAGE + 1) * 16))
    big_pages = x.build_pages(big_chr)
    check(len(big_pages) == 2 and len(big_pages[0][1]) == x.TILES_PER_PAGE and len(big_pages[1][1]) == 1,
          "build_pages: pagination splits exactly at 960 tiles (got %r)" % [len(c) for _, c in big_pages])
    check(big_pages[1][1][0]["hash"] == x.hash_name(x.hash_bytes(big_chr[x.TILES_PER_PAGE * 16:x.TILES_PER_PAGE * 16 + 16])),
          "build_pages: the one tile on the second page is the right one")

    # write_pages: files land where expected, with negative, distinct frame numbers
    pack = os.path.join(d, "pack")
    written = x.write_pages(pack, big_pages)
    check(written == ["cap_chr0", "cap_chr1"], "write_pages: stems in page order")
    for stem, expected_frame in zip(written, (-1, -2)):
        with open(os.path.join(pack, "capture", stem + ".json"), encoding="utf-8") as f:
            saved = json.load(f)
        check(saved["frame"] == expected_frame, "write_pages: %s has frame %d" % (stem, expected_frame))
        check(os.path.exists(os.path.join(pack, "capture", stem + ".png")), "write_pages: %s.png was written" % stem)

    # interoperability: the skin editor's own loader must read this pack exactly like a real capture,
    # and list every distinct tile hash
    import nes_skin_editor as e
    caps = e.load_captures(pack)
    check(len(caps) == 2, "the skin editor finds both synthetic pages")
    tiles = e.collect_tiles(caps)
    all_hashes = {c["hash"] for _, cells in big_pages for c in cells}
    check(set(tiles) == all_hashes, "the skin editor's tile list contains every extracted tile, nothing more or less")
    # painting one of these tiles and composing must show the painted colour, exactly like a real capture
    target_hash = big_pages[0][1][7]["hash"]
    skin = __import__("PIL.Image", fromlist=["Image"]).new("RGBA", (16, 16), (10, 20, 30, 255))
    e.save_skin(pack, target_hash, skin)
    out = e.compose(caps[0], e.load_skins(pack), 2)
    tx, ty = big_pages[0][1][7]["x"], big_pages[0][1][7]["y"]
    check(out.getpixel((tx * 2, ty * 2)) == (10, 20, 30), "a skin painted from the extracted tile shows up when composed, like a real capture")

if failures:
    print("%d extract_chr check(s) FAILED" % failures)
    sys.exit(1)
print("PASS: CHR extraction")
