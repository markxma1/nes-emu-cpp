#!/usr/bin/env python3
"""Proves the test ROMs catch the bugs they were written for: undo one fix at a
time in the emulator source, rebuild, run the affected ROM against the FCEUX
reference (run_compare.py) and expect a FAIL; then restore the source.
"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

MUTATIONS = [
    ('palette mirror $3F10', 'ppu_palette', 'NES_PPU/Memory/NES_PPU_Memory.cpp',
     "        else if (i == 0x3F10 || i == 0x3F14 || i == 0x3F18 || i == 0x3F1C)\n            Memory[i] = Memory[i - 0x10];\n", ""),
    ('backdrop colour', 'ppu_palette', 'NES_PPU/NES_PPU_Folder/NES_PPU.Display.cpp',
     "            frame.FillRectangle(backdropByScanline[static_cast<size_t>(y)], 0, y, 256, y + 1);", "            (void)y;"),
    ('left 8 pixel mask', 'ppu_palette', 'NES_PPU/NES_PPU_Folder/NES_PPU.Display.cpp',
     "            backgroundBuffer.FillRectangle(Color::Transparent(), 0, screenY, 8, screenY + 1);", "            (void)screenY;"),
    ('NMI restores I flag', 'nmi_flags', 'NES.Memory/Interrupt.cpp',
     "        ReplacePC(0xfffa, false, true);\n        NES_Register::P.Interrupt(true);",
     "        NES_Register::P.Interrupt(true);\n        ReplacePC(0xfffa, false, true);"),
    ('NMI raised by enabling it in vblank', 'nmi_flags', 'NES_PPU/Memory/NES_PPU_Register.cpp',
     "                Interrupt::RaiseNmiAfterNextInstruction();", "                (void)0;"),
    ('mid-frame $2006 scroll split', 'mmc3_split', 'NES_PPU/NES_PPU_Folder/NES_PPU.Scroll.cpp',
     "        if (!splitActive || scanline < splitStartLine)\n            return;", "        return;"),
    ('controller open bus', 'ctrl_read', 'NES.Controller/Controller/NES_GamePad.cpp',
     "        output4016.OpenBus(0x40);\n", ""),
    ('controller 9th read = 1', 'ctrl_read', 'NES.Controller/Controller/NES_GamePad.cpp',
     "(P1BID > 7) ? true :", "(P1BID > 7) ? false :"),
    ('taken branch cycle', 'timing', 'CPU/CPU/NES_CPU.cpp',
     "        if (Math::branchTaken)\n", "        if (false)\n"),
    ('page-cross read cycle', 'timing', 'CPU/CPU/NES_CPU.cpp',
     "else if (kPageCrossSensitiveOpcode[opcode] && Parameter::pageCrossed)", "else if (false)"),
    ('OAM DMA stall', 'timing', 'NES_PPU/OAM/NES_PPU_OAM.cpp',
     "        NES_CPU::pendingExtraCycles += onOddCycle ? 514 : 513;", "        (void)onOddCycle;"),
]

def build():
    return subprocess.run(['cmake', '--build', os.path.join(ROOT, 'build'), '--target', 'nes-emu', '-j8'],
                          capture_output=True, text=True).returncode == 0

def main():
    only = sys.argv[1:]
    all_ok = True
    for name, rom, path, old, new in MUTATIONS:
        if only and name not in only:
            continue
        full = os.path.join(ROOT, path)
        orig = open(full).read()
        if old not in orig:
            print(f'{name}: SKIPPED (pattern not found)'); all_ok = False; continue
        try:
            open(full, 'w').write(orig.replace(old, new, 1))
            if not build():
                print(f'{name}: SKIPPED (mutant does not build)'); all_ok = False; continue
            r = subprocess.run([sys.executable, '-u', os.path.join(HERE, 'run_compare.py'), rom],
                               capture_output=True, text=True)
            caught = r.returncode != 0
            print(f'{name}: {"caught by " + rom if caught else "NOT CAUGHT by " + rom}', flush=True)
            all_ok &= caught
        finally:
            open(full, 'w').write(orig)
    build()
    return 0 if all_ok else 1

if __name__ == '__main__':
    sys.exit(main())
