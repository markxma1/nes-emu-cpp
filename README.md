# nes-emu-cpp

A NES (Nintendo Entertainment System) emulator written in C++ as a **learning
project**: the point is to *understand* how an emulator works, not to be the
fastest or most complete one. Every part of the hardware (CPU, PPU, memory map,
mappers, controller) lives in its own folder, is written to be read, and is
commented with the hardware behaviour it models and a link to the documentation
it was checked against ([nesdev.org](https://www.nesdev.org/)).

If you want to write your own emulator, this repository is meant to help: read
the code in the order suggested below, use the test ROMs to see what each piece
has to get right, and compare against the "hardware gotchas" list, which
collects real mistakes found in this code base and how they were fixed.

Priorities, in order: **understandability**, clear structure, working code
(examples and tests actually run).

## Status

- CPU: all official opcodes plus the unofficial ones commonly used, passes the
  `nestest` conformance log (`tests/nestest`), cycle counts include branch and
  page-crossing penalties and the OAM DMA stall.
- PPU: scanline-accurate background and sprite rendering, scroll registers
  (`t`/`v`/`x` model for mid-frame splits), sprite 0 hit, left-column masking.
- Mappers: NROM, MMC1, MMC3, UxROM, CNROM, AxROM.
- APU: basic channels.
- Verified against [FCEUX](https://fceux.com/) frame by frame (CPU RAM and
  picture) with the same recorded input on several commercial games and with
  the self-written test ROMs in `tests/roms` (see its README).
- Not (yet) cycle-accurate inside an instruction; see "Known structural
  simplifications" below.

More extensions and experiments will come depending on the version (debug
viewers for name tables, pattern tables and OAM are already included).

## Build

Requirements: a C++17 compiler, CMake >= 3.16, OpenCV (window and image
output) and SDL2 (audio). On Arch: `sudo pacman -S cmake ninja opencv sdl2`;
on Debian/Ubuntu: `sudo apt install cmake ninja-build libopencv-dev libsdl2-dev`.

```sh
# run these in the project folder (the one with CMakeLists.txt)
cmake -S . -B build -G Ninja      # or omit -G Ninja for Makefiles
cmake --build build -j
ctest --test-dir build --output-on-failure   # unit tests + nestest
./build/nes-emu path/to/your.nes
```

### Where do I run what?

Every command in this README is meant to be run from the **project folder** (the one that contains
`CMakeLists.txt`), not from inside `build/`. `build/` is only where CMake puts its results:

| I want to ... | command (run in the project folder) |
|---|---|
| set up once | `cmake -S . -B build -G Ninja` (`-S .` = sources are here, `-B build` = results go to `build/`) |
| compile (again) | `cmake --build build -j` |
| run the tests | `ctest --test-dir build --output-on-failure` |
| play a game | `./build/nes-emu path/to/game.nes` |
| measure speed | `./build/nes-bench path/to/game.nes 1500` |
| build the documentation | `doxygen Doxyfile`, then open `docs/html/index.html` |
| read a profile | `python3 tools/profile_report.py trace.json` |

Programs you start with `./build/...` may change into their own folder while running (they look
for `Palletes/2C03and2C05.bmp` next to themselves), so give ROM and input files as paths that work from
the project folder; they are resolved before the program changes folder.

**ROMs are not included.** Commercial game ROMs are copyrighted; supply your
own legally obtained file. The repository ships only `tests/nestest` (a freely
distributed CPU test) and small ROMs written for this project in `tests/roms`.

Optional: `cmake -DNES_SANITIZER=address` (or `thread`, `undefined`) for sanitizer builds (see
`CMakeLists.txt`), and API documentation with Doxygen:

```sh
doxygen Doxyfile        # writes docs/html/index.html
```

## Power-on RAM

CPU RAM starts as all zero (like FCEUX). Real consoles start with unpredictable contents, and a few
programs read RAM they never wrote: one Super Mario Bros. dump takes its starting world from `$013E` and
shows the hidden "0-1" world if that byte is `$FF`. `NES_RAM_INIT=ff` or `NES_RAM_INIT=pattern`
(`00 00 00 00 FF FF FF FF ...`) start with other contents, useful to find such programs.

## Speed and profiling

`build/nes-bench ROM.nes [frames] [input.txt]` runs a ROM without a window and without the speed limit and
prints how many times faster than a real NES it ran; `NES_BENCH_RENDER_EVERY=N` draws only every Nth frame
(0 = never; game state stays identical, see `NES_PPU::SetRenderPixels`). `NES_PROFILE=trace.json` records
scoped timers (`Profiler/Profiler.h`); `python3 tools/profile_report.py trace.json` prints a table and writes
timeline pictures.

### Finding the slow parts with `gprof` (step by step)

`gprof` is a separate command line tool from GNU binutils (usually already installed; check with
`gprof --version`). It samples where a program spends its time. It needs a build with the extra flag `-pg`,
so use a **second build folder** and keep the normal `build/` untouched. All commands run in the project folder:

```sh
# 1. a separate profiling build (keeps optimisation on, adds profiling code)
cmake -S . -B build-prof -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-pg" -DCMAKE_EXE_LINKER_FLAGS="-pg"
cmake --build build-prof -j --target nes-bench

# 2. run the benchmark; when it ends normally it writes gmon.out
ROM=$PWD/path/to/game.nes
./build-prof/nes-bench "$ROM" 1500

# 3. read the result - gmon.out is in build-prof/ (nes-bench changes into its own folder)
gprof ./build-prof/nes-bench build-prof/gmon.out | head -30
```

How to read the "Flat profile":

- `% time` / `self seconds`: the share of time spent *inside* that function itself. The top lines are the
  hot spots - look at these first.
- `calls`: how often it ran. A tiny function with hundreds of millions of calls is not slow by itself,
  but it is called too often.
- The percentages compare functions with each other reliably; the absolute seconds are only rough (the
  profiling code itself slows the program down).

Limits: only code built with `-pg` is measured (not OpenCV/libc), waiting is not counted, and threads are
covered badly - which is why `nes-bench` runs without a window. For an exact picture of *when* something
happens (single frames, spikes) use the built-in timers above instead. If `perf` is installed
(`sudo pacman -S perf`), `perf record ./build/nes-bench game.nes 1500` and `perf report` need no special
build at all.

## Controls and debug windows

Keyboard bindings are stored in `keyboard.cfg` next to the binary (remap menu:
`M`). Debug windows: `N` name table, `P` pattern table, `U` OAM viewer,
`V` memory viewer, `C` CPU speed; `K` cycles which CHR bank state (as drawn,
#0, #1, ...) the name table, pattern table and OAM viewers show.

## What this is (and isn't)

- A **scanline-accurate**, not fully cycle-accurate, NES emulator.
- Structured so each hardware part (CPU, PPU, Memory, Mapper, addressing,
  controller) lives in its own folder, separate from the UI shell
  (`NES/main.cpp`) and the software-framebuffer class (`ClassLibrary1/Picture`),
  which model no hardware.
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
    three debug windows). Read it last, since it's plumbing, not hardware.

`NES.Console/NES_Console.h/.cpp` is the thin glue layer wiring 1-9 together
and exposing the handful of entry points `main.cpp` needs
(`INIT`, `LoadRom`, `Run`, `getDisplay`, ...) - worth a skim early on as a map
of "what talks to what", even before you've read the pieces it wires up.

## Hardware gotchas: things this codebase got wrong at least once

Every item below was a **real bug found in this exact code** (in an early
version that had never been tested against a real game). They're listed here
because each one is a genuinely easy mistake to make when implementing a
6502/NES emulator from the spec sheet alone, not something specific to this
codebase's quirks. Each is also documented in-place with a `// FIXED ...` comment and a nesdev.org link at its
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
- **OAM DMA is a byte-*value* copy**, not a reference/alias - when memory
  cells are reference-typed objects (here, `shared_ptr`), it's easy to accidentally alias the source instead of
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
- **CPU timing is instruction-atomic**: each instruction runs as one step and
  its total cycle count (including branch / page-crossing penalties and the OAM
  DMA stall) is charged afterwards, so the PPU is advanced per instruction, not
  per cycle. Interrupts are recognised at instruction boundaries and cost 7 cycles.

## Reading the comments

Two comment styles appear throughout the hardware-modeling code:

- `// NOTE: ...` - a known simplification or quirk that is kept on purpose,
  with the reason given.
- `// FIXED ...` - a bug that was root-caused with real documentation
  evidence (nesdev.org links are attached). These record what was wrong and
  why, so the reasoning survives even after the bug doesn't.

`ClassLibrary1/Picture` models no hardware; it is just a software
framebuffer - see its own class-level comment.

## Reference documentation

Everything in this project was learned from and checked against the community
documentation - thank you to everyone who maintains it:

- [nesdev.org](https://www.nesdev.org/) - the main source (wiki + forums). Pages
  used most: [CPU](https://www.nesdev.org/wiki/CPU),
  [CPU interrupts](https://www.nesdev.org/wiki/CPU_interrupts),
  [Instruction reference](https://www.nesdev.org/wiki/Instruction_reference),
  [CPU addressing modes](https://www.nesdev.org/wiki/CPU_addressing_modes),
  [PPU registers](https://www.nesdev.org/wiki/PPU_registers),
  [PPU scrolling](https://www.nesdev.org/wiki/PPU_scrolling),
  [PPU palettes](https://www.nesdev.org/wiki/PPU_palettes),
  [PPU power up state](https://www.nesdev.org/wiki/PPU_power_up_state),
  [PPU OAM](https://www.nesdev.org/wiki/PPU_OAM),
  [Mirroring](https://www.nesdev.org/wiki/Mirroring),
  [MMC3](https://www.nesdev.org/wiki/MMC3), [MMC1](https://www.nesdev.org/wiki/MMC1),
  [Controller reading](https://www.nesdev.org/wiki/Controller_reading),
  [Open bus behavior](https://www.nesdev.org/wiki/Open_bus_behavior),
  [Errata](https://www.nesdev.org/wiki/Errata).
- [nestest](https://www.nesdev.org/wiki/Emulator_tests) - the CPU conformance ROM
  and log used in `tests/nestest`; the same page lists further test ROMs.
- [6502 instruction set](https://www.masswerk.at/6502/6502_instruction_set.html) -
  base cycle counts.
- [Wikibooks: NES Programming](https://en.wikibooks.org/wiki/NES_Programming) - a
  gentler introduction.
- [FCEUX](https://fceux.com/) - used as the reference emulator for the
  frame-by-frame comparisons in `tests/roms`.

## License

This project is licensed under the **GNU General Public License v3.0**. The full,
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
