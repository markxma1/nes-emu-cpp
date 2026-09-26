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
