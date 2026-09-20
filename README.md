# nes-emu-cpp

A C++ port of a personal NES (Nintendo Entertainment System) emulator originally
written in C# as a learning project. The goal of both the original and this port
is **understanding real NES hardware**, not just playing games - so the code is
written and commented to be read, not just run.

If you're new to emulator development, this file is meant to be your entry
point: where to start reading, how the pieces fit together, and - probably the
most useful part - a list of the specific ways real hardware behaves
differently from what you'd naively guess, collected from real bugs found and
fixed in this exact codebase.

## What this is (and isn't)

- A **cycle-approximate**, not cycle-accurate, NES emulator. There is no
  per-scanline/per-dot PPU clock (see "Known structural simplifications"
  below) - rendering happens once per displayed frame, not pixel-by-pixel as
  the beam would sweep across a real CRT.
- A **1:1 structural port** of the C# original for all hardware-modeling code
  (CPU, PPU, Memory, Mapper, addressing, controller). Only the UI shell
  (`NES/main.cpp`) and the software-framebuffer class (`ClassLibrary1/Picture`)
  are allowed to differ from the C# original, since neither models hardware.
- Verified against [`tests/nestest`](tests/nestest), the standard 6502 CPU
  conformance test ROM - see "How to verify CPU correctness yourself" below.

## Where to start reading

Read in this order; each step assumes you understood the previous one.

1. **`CPU/CPU/NES_Register.h`** - the 6502's registers (A, X, Y, P, S, PC).
   Small and self-contained; this is the state everything else mutates.
2. **`NES.Memory/AddressSetup.h`** - the single most important abstraction in
   this codebase. Every one of the NES's 64KB of addressable bytes is one of
   these. It's not just a byte: it has `BeforGet`/`AfterGet`/`BeforSet`/`AfterSet`
   hooks, which is how memory-mapped I/O works here - e.g. writing to $2007
   (PPUDATA) needs to actually copy the byte into PPU memory, and reading
   $2002 (PPUSTATUS) needs to clear the vblank flag as a side effect.
   Understanding this class is understanding the whole memory model.
3. **`NES.Memory/NES_Memory.h/.cpp`** - the CPU's 64KB address space, built as
   a flat `vector<shared_ptr<AddressSetup>>`. Read `InitMemory()`'s mirroring
   logic (why $0800-$1FFF alias $0000-$07FF) - mirroring on real hardware
   happens because certain address lines are left undecoded, and this class
   reproduces the *effect* (shared cells) without modeling the *cause* (wired
   logic gates).
4. **`CPU/CPU/AssemblyList.cpp`** - the opcode dispatch table: a
   `vector<function<void()>>` indexed by opcode byte, built once at startup.
   `NES_CPU::Step()` (in `NES_CPU.cpp`) is the entire fetch-decode-execute
   loop: fetch the byte at PC, look it up in this table, call it.
5. **`CPU/CPU/Assembly_6502.h/.cpp`** and **`CPU/CPU/Math.h/.cpp`** - the
   actual opcode implementations. `Assembly_6502` has one tiny function per
   opcode+addressing-mode combination (e.g. `ADC_69` = ADC immediate,
   `ADC_6D` = ADC absolute); `Math` holds the shared arithmetic/flag logic
   they call into (`Math::ADC`, `Math::CMP`, ...).
6. **`NES.Memory/Interrupt.h/.cpp`** and **`NES.Memory/Stack.h/.cpp`** - how
   BRK/IRQ/NMI/RESET actually transfer control, and how the hardware stack
   at $0100-$01FF works. Read this *after* you're comfortable with the CPU
   loop above - interrupts are where the subtlest bugs live (see the gotcha
   list below).
7. **`NES_PPU/Memory/NES_PPU_Register.h/.cpp`** - the PPU's memory-mapped
   registers ($2000-$2007, $4014): PPUCTRL, PPUMASK, PPUSTATUS, OAMADDR,
   PPUSCROLL, PPUADDR, PPUDATA, OAMDMA.
8. **`NES_PPU/NES_PPU_Folder/NES_PPU.h`** and its `.cpp` files
   (`.Tile.cpp`, `.NameTable.cpp`, `.Display.cpp`, `.Scroll.cpp`) - turns PPU
   memory (pattern tables, name tables, attribute tables, OAM, palettes) into
   pixels. `Display()` is the entry point, called once per rendered frame.
9. **`NES.Controller/Controller/NES_GamePad.h/.cpp`** - the controller
   read protocol (shift register + strobe bit).
10. **`NES/main.cpp`** - the UI shell (OpenCV window, keyboard input, the
    three debug windows). This is the *only* file with no C# equivalent to
    mirror - read it last, since it's plumbing, not hardware.

`NES.Console/NES_Console.h/.cpp` is the thin glue layer wiring 1-9 together
and exposing the handful of entry points `main.cpp` needs
(`INIT`, `LoadRom`, `Run`, `getDisplay`, ...) - worth a skim early on as a map
of "what talks to what", even before you've read the pieces it wires up.

## Hardware gotchas: things this codebase got wrong at least once

Every item below was a **real bug found in this exact code** (inherited
verbatim from the 2016 C# original, which never wired up real keyboard input
and so never actually got tested against a real game). They're listed here
because each one is a genuinely easy mistake to make when implementing a
6502/NES emulator from the spec sheet alone, not something specific to this
codebase's quirks. Each is also documented in-place with a `// FIXED (was a
preserved C# bug, now corrected): ...` comment and a nesdev.org link at its
actual location, cited below - read those for the full explanation and the
exact fix.

- **`BIT`'s flags don't all come from the same value.** Only Zero comes from
  `value & A`; Negative and Overflow are copied directly from bits 7 and 6 of
  the *raw* memory operand, independent of A. `Math::BIT`
  (`CPU/CPU/Math.cpp`).
- **`BVC`/`BVS` test the Overflow flag, not Carry** - easy to typo/confuse
  since both are "does this branch on a flag I don't touch every instruction"
  situations. `Assembly_6502::BVC_50`/`BVS_70` (`CPU/CPU/Assembly_6502.cpp`).
- **A "just move a byte" instruction can still need to set flags.** `PLA`
  (pull accumulator) sets N/Z from the popped value - it's easy to write it
  as a pure data-movement instruction and forget the flags. Same file.
- **`CMP`/`CPX`/`CPY`'s Negative flag comes from the actual subtraction
  result's bit 7**, not from which operand was numerically bigger - those
  aren't the same thing once you're comparing as signed 8-bit numbers. The
  `Compare()` helper in `Math.cpp`.
- **ADC/SBC Overflow is *signed* overflow**, a completely different
  condition from Carry (unsigned overflow) - reusing one "did this spill
  past 8 bits" check for both is a natural-looking but wrong shortcut.
  `Math::ADC`/`SBC`.
- **SBC's Carry meaning is "no borrow", computed differently from ADC's
  Carry** ("bit 8 set") - SBC's raw `A - B - borrow` can go negative, where
  ADC's raw sum never does. `Math::SBC`.
- **ROL/ROR must capture the incoming Carry *before* shifting** - the shift
  step itself overwrites Carry with the outgoing bit, so reading Carry
  *after* calling the shift helper silently reads the wrong (already
  overwritten) value. `Math::ROL`/`ROR`.
- **Zero-page indexed and indexed-indirect addressing wrap within the zero
  page (`$00`-`$FF`)**, including the *pointer table* reads for `(zp,X)` and
  `(zp),Y` - not just the final effective address. `Parameter::zpx1/zpy1/
  zpx2/zpy2` (`CPU/CPU/Parameter.cpp`).
- **`JMP` indirect has a real hardware bug you have to reproduce, not fix**:
  if the pointer's low byte is `$FF`, the CPU reads the high byte from the
  *start* of the same page instead of the next page. Some real games rely on
  this. `Parameter::MemoryValueToAdress`.
- **A real interrupt (BRK/IRQ/NMI) pushes the *exact* PC** - unlike `JSR`,
  which pushes `return_address - 1` (because `RTS` always adds 1 back).
  Reusing the same "subtract 1 first" push helper for both is a subtle,
  easy-to-miss bug since it only breaks the *return address*, not anything
  visible at the push site. `Interrupt::ReplacePC`, `Assembly_6502::RTI_40`.
- **The B flag and bit 5 don't physically exist in the P register** - they're
  artifacts of what gets written to the *stack* by `PHP`/`BRK`, and are
  discarded (not restored) when `PLP`/`RTI` pull flags back. `Stack::
  StackToProcessorstatus`.
- **OAM DMA is a byte-*value* copy**, not a reference/alias - in a language
  with reference-typed "memory cell" objects (C#'s classes, or here,
  `shared_ptr`), it's easy to accidentally alias the source instead of
  copying it, silently turning "OAM" into a live view of whatever the CPU
  writes to that RAM page next. `NES_PPU_OAM::OAMDMA`.
- **PPUSTATUS's vblank flag sets every single vblank, unconditionally** -
  PPUCTRL's NMI-enable bit only gates whether that event *also* raises an
  interrupt; it has no bearing on the status flag. Gating both behind the
  same condition deadlocks the very first "wait for vblank" loop real boot
  code always starts with, since that loop runs *before* any game ever
  touches PPUCTRL. `NES_PPU::Display` (`NES_PPU/NES_PPU_Folder/
  NES_PPU.Display.cpp`).
- **The 6502 has ~105 "unofficial"/"illegal" opcodes** that real commercial
  ROMs sometimes use anyway (often just Nintendo's own boilerplate boot code,
  using unofficial NOPs for exact timing padding). Leaving them all
  unimplemented is fine right up until a ROM's real, correct control-flow
  path executes one - see `AssemblyList::UnofficialNOPs`/`UnofficialOpcodes`.
- **The standard controller's 8 sequential bit-reads have one fixed
  order**: A, B, Select, Start, Up, Down, Left, Right. Getting this order
  wrong doesn't disable input - it silently remaps directions (e.g. Left
  reads back as Up), which is a much more confusing bug to notice than
  "nothing responds". `NES_GamePad::Controller`.
- **Real controllers report simultaneous button presses fine** (it's a
  parallel shift register, not one-button-at-a-time) - if your emulator's
  *input source* only ever reports one key event per poll (true of many
  simple polling APIs, OpenCV's `cv::waitKey` included), simultaneous
  presses can get dropped even though the emulated controller model itself
  is perfectly capable of representing them. See `NES/main.cpp`'s key-queue
  draining.

## How to verify CPU correctness yourself

```sh
cmake --build build --target nestest_check
./build/tests/nestest_check tests/nestest/nestest.nes tests/nestest/nestest.log
```

This replays [nestest.nes](https://www.nesdev.org/wiki/Emulator_tests) (the
standard 6502 conformance ROM) through `NES_CPU::Step()` and diffs register
state against `nestest.log`, a known-correct trace captured from Nintendulator.
It stops at the *first* mismatched instruction and prints exactly what
differed - this is a far faster way to isolate a CPU bug than reading a
running game's behavior and guessing. All 8991 lines (official *and*
unofficial-opcode sections) currently pass. If you introduce a CPU change and
this stops passing, that's your bug, found before it ever reaches a game ROM.

## Known structural simplifications (not bugs, but worth knowing)

- **Scanline-accurate, not per-dot.** Real hardware renders one pixel per PPU
  clock, 341 dots/scanline, 262 scanlines/frame. This emulator drives a real
  per-scanline/per-dot clock (`NES_PPU::AdvanceDots()`/`OnScanlineStart()`)
  off the CPU's own executed cycle count, and renders background/sprites one
  real scanline at a time (`RenderBackgroundScanline()`/
  `RenderSpriteScanline()`), sampling PPU/mapper state fresh at the exact
  scanline it's needed - so *scanline*-granularity mid-frame effects (raster
  splits, a scanline-IRQ-driven CHR-bank switch, sprite-0-hit-gated HUD
  writes) render correctly. What's *not* modeled is anything finer than one
  scanline: real hardware's internal v/t/x/w scroll registers (this port
  uses a logical scroll-accumulator across a doubled nametable space
  instead) and true per-dot/mid-instruction timing (`NES_CPU::Step()` stays
  instruction-atomic - no mid-instruction NMI/IRQ delivery).
- **Sprite-0-hit is checked per-scanline**, not per-dot - see
  `NES_PPU::RenderSpriteScanline()`'s own comment for exactly what it
  trades off (it fires as soon as sprite 0's decoded row for the *current*
  scanline overlaps an opaque background pixel on that same row, not at the
  exact real-hardware dot within the row).
- **A handful of well-known *unstable* unofficial opcodes are deliberately
  unimplemented** (`LAX #imm`/`$AB`, `XAA`/`$8B`, the `SHA`/`SHX`/`SHY`/`TAS`/
  `AHX` family) - their real behavior varies by console/temperature, and no
  real software deliberately relies on them.
- **CPU timing is instruction-count-based, not cycle-count-based** - see
  `NES_CPU::SleepTime`'s own `// NOTE:` for the specific consequence.

## Preserved-vs-fixed bugs: reading the comments

Two comment styles appear throughout the hardware-modeling code, and the
distinction matters:

- `// NOTE: preserved from the C# original ...` - a bug (or an
  intentionally-still-inherited-but-real quirk) that's kept **verbatim** on
  purpose, because the porting brief for this project is "structurally
  identical to the C# original for anything modeling hardware" - don't
  "fix" these without checking with whoever owns the project first.
- `// FIXED (was a preserved C# bug, now corrected): ...` - a bug that
  *was* preserved this way at some point, but has since been deliberately
  fixed once root-caused with real documentation evidence (nesdev.org links
  are attached). These record what was wrong and why, so the reasoning
  survives even after the bug doesn't.

`ClassLibrary1/Picture` is the one exception to all of this: it has no C#
equivalent (`System.Drawing.Bitmap` doesn't exist in C++), models no
hardware, and is explicitly free to differ - see its own class-level comment.

## Reference documentation

- [nesdev.org wiki](https://www.nesdev.org/wiki/) - the primary source used
  throughout this codebase's comments. Look up register names ($2000-$2007,
  $4016/$4017), "Addressing modes", "CPU interrupts", "Errata" (hardware
  bugs like the JMP indirect one above), and "Instruction reference".
- [wikibooks.org NES Programming](https://en.wikibooks.org/wiki/NES_Programming) -
  a gentler, tutorial-style introduction; several files in `NES.Memory`
  reference this directly.
- [nesdev.org/wiki/Emulator_tests](https://www.nesdev.org/wiki/Emulator_tests) -
  where `tests/nestest/nestest.nes` and `.log` came from, and a good list of
  further conformance ROMs if you want to extend the test suite (PPU timing
  tests, APU tests, mapper tests - none of which this project has yet).

## License

This project - both this C++ port and the original C# emulator it's ported
from - is licensed under the **GNU General Public License v3.0**. The full,
verbatim license text is in [`LICENSE`](LICENSE); every source file also
carries its own copyright/license header at the top, so the terms travel
with the code even if a single file gets copied out on its own.

In practice: you're free to read, run, study, modify and redistribute this
code, including for your own learning projects - that's the whole point of
it being public. If you redistribute a modified version, GPLv3 requires
keeping it under the same license and preserving the copyright notices. The
`LICENSE` file and per-file headers are the actual legal terms; this
paragraph is just a plain-language summary, not a substitute for reading
them.

**Game ROM files are not included and must not be committed to this
repository.** A `.nes` game ROM is a copy of copyrighted commercial data
(the game itself), separate from - and not covered by - this project's own
GPL license; this emulator doesn't include or distribute any. If you clone
this repo, you'll need to supply your own legally-obtained ROM (e.g. dumped
from a cartridge you own) to actually run a game. `tests/nestest/nestest.nes`
is the one exception - a freely-distributed 6502 test/homebrew ROM from the
NES developer community, not a commercial game (see
[nesdev.org/wiki/Emulator_tests](https://www.nesdev.org/wiki/Emulator_tests)) -
`.gitignore` is set up to reflect exactly this distinction.
