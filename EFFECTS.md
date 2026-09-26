# Skins / HD layer: replacing sprites and background tiles at display time

Goal: show the game with new artwork (redrawn characters, smoother AI-upscaled textures, more pixels
than the original) **without touching the emulated NES** - the game, the CPU and the PPU run exactly
as before, only the picture that reaches the window is different. Think "texture pack", not "ROM hack".

## Can this be done without changing the core? Yes (checked)

Everything a replacement needs is already readable from outside the core:

| Needed | Where it is read |
|---|---|
| position, tile number, flips, palette, priority of the 64 sprites | `NES_PPU_OAM::Sprite*` |
| the 16 bytes of any tile | `NES_PPU_Memory::PatternTableN[bank][tile*16 + i]` |
| background tile numbers, attributes, scroll | `NES_PPU_Memory::NameTableN`, `NES_PPU_AttributeTable`, `NES_PPU::xScroll/yScroll` |
| real palette colours | `NES_PPU_Palette::get*ColorPalette()` |
| the finished 256x240 picture | `NES_Console::getDisplay()` |

`NES/HdLayer.cpp` takes a snapshot of these at the end of every frame (from the frame hook, on the
CPU thread) and the UI thread builds the enlarged picture from it. The only change outside the new
files is three lines in `main.cpp` (hook, key handling, showing the composed picture).

### How a tile is recognised

A tile's identity is the hash of its 16 pattern bytes (the same idea as Mesen's HD packs), so it does
not matter where on screen it appears, which nametable slot or sprite slot draws it, or whether it is
flipped (the skin is stored unflipped and flipped at draw time). A skin can apply to every use of a
tile (`<hash>.png`) or only to one palette (`<hash>_s<pal>.png` for sprites, `<hash>_b<pal>.png` for
background) - useful when the game recolours the same shapes.

### How wrong guesses are avoided ("verify against the picture")

The snapshot is taken after the frame is finished, so the state may differ from what was drawn (a
mapper switched CHR banks mid-frame, a sprite is hidden behind another one, the 8-sprites-per-line limit
dropped one, ...). So every candidate cell is decoded again and compared with the finished frame: a
pixel is painted with the skin **only if it is really that tile's pixel in the picture** (colour
equal). Cells that match less than 40% are ignored. Consequences: covered parts, behind-background
sprites, the left-column mask and sprite flicker are all handled for free, and a wrong guess cannot
paint garbage - the worst case is "this tile keeps its original look in this frame".
(Checked on a real capture: the Python editor and the C++ layer agree on all 1003 cells.)

## What works now (prototype)

- `H` in the emulator: skin layer off / 2x / 3x / 4x (`hd_scale` in `settings.cfg`, also in the settings program).
- Skin packs live in `skins/<rom name>/tiles/`; the folder is watched, so saving in the editor changes the running game within half a second.
- `X` captures the current picture (`skins/<rom>/capture/cap_<frame>.json + .png`) for the editor.
- Sprites (8x8 and 8x16) and background tiles are replaced by tile; transparency (alpha) is respected.
- `tools/nes_skin_editor.py`: list of all tiles seen in the captures, pixel editor (pencil, eraser, fill, picker, undo), painting directly on the captured picture (**Frame** tab, flips and shared tiles handled), "start from original", import/export PNG, and **sheet export/import** to send all tiles through any external AI upscaler / image editor and get them back.

## Limits of this first version (and what would lift them)

1. **A skin is limited to its tile's silhouette.** HD pixels are painted only where the original tile has visible pixels, so outlines/glow that extend beyond the original shape are not possible yet. *Lift:* paint the full HD tile where nothing in front of it is drawn (needs a "what is in front" mask) - no core change.
2. **One tile = one picture.** A character made of 6 sprite tiles is edited tile by tile (the Frame tab makes this feel like editing the whole character, but the pictures are still per tile). Seams between tiles cannot be smoothed across tiles. *Lift:* "objects" = a group of tiles at fixed relative positions (matched per frame) with one big picture; the layer above is designed to allow this.
3. **Mid-frame changes.** Split screens (status bars) and games that swap CHR banks in the middle of the frame are only handled through the verification above (mismatch = original shown). *Lift (small, read-only core change):* record scroll/CTRL/CHR state per scanline in the PPU (per-row CHR snapshots already exist for the viewers).
4. **Background tiles under sprites of the same colour** can be painted over the sprite pixel (rare, only when a skin exists for that background tile).
5. **Whole-background replacement** (a painted picture instead of tiles) is not tile based; it would need scene matching. Not attempted.
6. **AI:** no model is built in. The sheet export/import connects the editor to any external upscaler (Real-ESRGAN, waifu2x, a diffusion model, ...). Doing it inside the tool (e.g. calling a local model per tile with colour-consistency checks) is a possible next step.

## Files

```
skins/<rom name>/tiles/<hash>.png              skin of a tile (RGBA, size = multiple of 8)
skins/<rom name>/tiles/<hash>_s2.png           skin only for sprites drawn with palette 2 (_b<pal> = background)
skins/<rom name>/capture/cap_<frame>.json/png  input for the editor
NES/HdLayer.h/.cpp                             snapshot + compositor (no core change)
tools/nes_skin_editor.py                       the editor
tests/skin_editor_check.py, cpu_check.cpp      tests (tile decoding, visibility, painting with flips, compose, sheets)
```

## Speed: what costs how much, and what runs in parallel

Measured with `nes-bench` (headless, no speed limit, 1500 frames, median of 3 runs, 32-core machine).
"ms/frame" is time on the emulation thread per emulated frame (a real NES needs 16.6 ms).

| Configuration | Galaga | Tiny Toon |
|---|---|---|
| baseline (no window, no sound samples) | 2.22 | 2.31 |
| + sound samples generated (`NES_AUDIO_WAV=`) | 3.30 (+1.08) | 3.39 (+1.08) |
| + skin layer 3x, everything on the emulation thread (`NES_BENCH_HD=3:sync`) | 4.11 (+1.89) | 3.88 (+1.57) |
| + skin layer 3x, **worker thread** (`NES_BENCH_HD=3`) | 2.67 (+0.45) | 2.73 (+0.42) |
| + skin layer 4x, worker thread | 2.68 | 2.76 |

Where the time goes (profiler, per frame): building/verifying the cells 0.9 ms and composing 0.8 ms
(both on the worker), copying the emulator state for the worker 0.1 ms (emulation thread).
In the plain emulator (gprof) about 40% is picture composition in the PPU (`Picture::BlendPlain`,
`GetPixel`, ...), 9% memory cell access (`AddressSetup::Value`), 8% CPU instructions, 4% APU clocking
without sound.

### The parallel design ("own RAM")

`HdLayer::OnFrame()` runs on the emulation thread but only **copies** what it needs into a private
`RawState` (OAM, both pattern tables, name tables + attribute palettes, resolved palette colours,
scroll, PPU flags and the finished picture: about 20 KB plus the picture). A worker thread owns that copy:
it decodes, verifies against the picture, paints the skins and publishes the finished HD picture; the UI
just takes the newest one. Frames the worker cannot keep up with are dropped (newest wins), so the
layer can never slow the game down beyond the copy. Because the worker uses nothing but its own memory,
this is the pattern for any other external add-on that watches or improves the picture: it needs no
locks on emulator state and cannot disturb timing. (TSan: no reports in our code.)

What it costs the game: +0.4 ms/frame (+20% at uncapped speed; irrelevant at real time, where a frame has 16.6 ms).
Further options: copy only what changed (CHR tables change rarely), or skip the copy when the game
paused; cache decoded cells per hash.

### Measurements that did not pay off

- Skipping idle APU time by arithmetic (no sound): no measurable change (2.22 ms both ways), so it was not kept.
- Sound samples cost +1.1 ms/frame (per-CPU-cycle mixing). They are only generated when a sound device is open,
  so AI/headless runs do not pay for them.

### Where more parallelism would help (core work, not done)

The PPU's picture composition (about 40% of the time) is the largest block. It could be split off the same
way - the emulation thread records per-scanline state, another thread turns it into pixels - but that changes
the core and is only worth it if uncapped speed becomes the goal again.

## Three ways to run: play, train, publish

The skin layer is optional and costs nothing while it is off (`hd_scale=0`, the default: no thread, no copy).
The same emulation serves three uses:

| Use | Picture | Command / setting | Speed (Tiny Toon, ms per emulated frame) |
|---|---|---|---|
| **Play** (human, 1-3x is plenty) | window, optional skins 2-4x on the worker thread | `nes-emu`, `H` | about 2.7 (of 16.6 available) |
| **Train an AI** | none, or a small grey picture; better still the game's RAM values as observation | `nes-bench`, `NES_BENCH_RENDER_EVERY=N`, `NES_BENCH_OBS=84x84` | 2.45 full picture; 2.9 with an 84x84 grey observation of every frame; 1.55 drawing one frame in four (about 11x real time); **1.2 without drawing anything** (about 13x real time) |
| **Publish** (YouTube, commentary) | rendered later, slowly, as beautiful as wanted | `nes-render` | about 7.6 ms/frame at 4x with skins, ~2x real time |

The idea behind the third row: **record the buttons, not the video.** A game (human or AI) is
reproduced exactly from its input log, because the emulation is deterministic. Afterwards
`nes-render game.nes game.inputs.txt out.mp4 4 skins/game` replays it without a window, composes every frame
with the skin layer (none dropped, unlike the live worker), records the sound and muxes an mp4 with
`ffmpeg`. An AI's commentary or statistics can be logged with the frame numbers and put on top of the
finished video, since the video can be rendered again at any time with newer skins.
