# Self-written test ROMs

Small NES programs written for this project (assembled by `nesasm.py` /
`build_roms.py`, no third-party code and no commercial data), so they can live
in the repository. Each one reproduces a behaviour that once broke the
emulator, so a regression shows up in seconds instead of needing a whole game.

| ROM | What it checks | Where the result is |
|---|---|---|
| `ctrl_read.nes` | controller strobe, button order, open-bus bit 6, reads after the 8th bit | RAM `$0300-$031x` |
| `nmi_flags.nes` | RTI restores the I flag, an NMI raised inside a handler nests | RAM `$0300-$0313` |
| `ppu_palette.nes` | `$3F10` mirrors `$3F00`, backdrop colour, left-8-pixel mask (hold A) | RAM `$0300-$0301` + picture |
| `mmc3_split.nes` | MMC3 scanline IRQ, CHR bank switch, mid-frame `$2006` reload (status bar) | picture + RAM |
| `timing.nes` | cycle counts: taken branch, page-crossing branch/read, OAM DMA stall | 16-bit counters `$0300-$0309` |

RAM `$03F0-$03FF` holds free-running frame counters and is not compared.

## How they are verified

`run_compare.py` runs every ROM with the same frame-numbered input in FCEUX
(reference) and in this emulator and compares RAM every frame plus the picture
(structurally, so different colour palettes do not matter). It needs `fceux`,
Xvfb on `:99` and Pillow, so it is not part of `ctest`.

`mutation_check.py` proves the ROMs are sensitive: it undoes one fix at a time,
rebuilds, and expects the matching ROM to fail.

Rebuild the ROMs with `python3 build_roms.py`.
