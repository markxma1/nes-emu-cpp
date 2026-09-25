#!/usr/bin/env python3
"""Runs each test ROM in FCEUX (reference) and in this emulator with the same
frame-numbered input and compares CPU RAM ($0300-$03EF; $03F0+ holds free-running counters) every frame, plus the
picture at the chosen frame (structurally: same partition of pixels into
colours, so palette differences do not matter).

Needs: fceux, Xvfb on :99, a built nes-emu (default ../../build/nes-emu),
Pillow. Not part of ctest because it depends on the external reference.
"""
import os, struct, subprocess, sys, tempfile, glob
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
EMU = os.environ.get('NES_EMU', os.path.join(HERE, '..', '..', 'build', 'nes-emu'))
ENV = dict(os.environ, DISPLAY=':99', SDL_AUDIODRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1', SDL_VIDEODRIVER='x11')
for k in ('WAYLAND_DISPLAY', 'XDG_SESSION_TYPE'):
    ENV.pop(k, None)

# name: (frames, screenshot frame, input events)
CASES = {
    'ctrl_read':   (300, 0, ['60 A 1', '70 A 0', '80 START 1', '90 START 0', '100 R 1', '110 R 0', '120 SELECT 1', '125 SELECT 0', '140 L 1', '150 L 0']),
    'nmi_flags':   (200, 0, []),
    'ppu_palette': (200, 150, ['100 A 1']),
    'mmc3_split':  (150, 100, []),
    'timing':      (200, 0, []),
}


def records(path):
    data = open(path, 'rb').read()
    out = {}
    for i in range(len(data) // 2052):
        chunk = data[i * 2052:(i + 1) * 2052]
        out[struct.unpack('<I', chunk[:4])[0]] = chunk[4:]
    return out


def structural_mismatch(a_path, b_path):
    """Pixels that break a one-to-one colour correspondence between the two
    pictures (checked in both directions, so a merged or lost colour is caught)."""
    from PIL import Image
    a = Image.open(a_path).convert('RGB')
    b = Image.open(b_path).convert('RGB').crop((0, 8, 256, 232))  # FCEUX hides 8 rows top/bottom
    w, h = a.size
    pa = [a.getpixel((x, y)) for y in range(h) for x in range(w)]
    pb = [b.getpixel((x, y)) for y in range(h) for x in range(w)]

    def one_way(p, q):
        m = defaultdict(Counter)
        for c, d in zip(p, q):
            m[c][d] += 1
        best = {k: v.most_common(1)[0][0] for k, v in m.items()}
        return sum(1 for c, d in zip(p, q) if best[c] != d)

    return max(one_way(pa, pb), one_way(pb, pa))


def run(name, tolerance, rom=None, cases=None, ram_range=range(0x300, 0x3F0)):
    frames, shot, events = (cases or CASES)[name]
    rom_is_game = cases is not None
    rom = rom or os.path.join(HERE, name + '.nes')
    tmp = tempfile.mkdtemp()
    inp = os.path.join(tmp, 'in.txt')
    open(inp, 'w').write('\n'.join(events) + '\n')
    ref_bin, our_bin = os.path.join(tmp, 'ref.bin'), os.path.join(tmp, 'ours.bin')
    snaps = os.path.expanduser('~/.fceux/snaps')
    snap_prefix = os.path.splitext(os.path.basename(rom))[0]
    for f in glob.glob(os.path.join(snaps, snap_prefix + '*')):
        os.remove(f)
    env = dict(ENV, REF_IN=inp, REF_N=str(frames), REF_OUT=ref_bin, REF_SHOT=str(shot))
    try:
        subprocess.run(['fceux', '--loadlua', os.path.join(HERE, 'ref_dump.lua'), rom], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
    except subprocess.TimeoutExpired:
        pass  # FCEUX sometimes hangs in emu.exit(); the log is flushed every frame
    env = dict(ENV, NES_PLAYBACK_INPUT=inp, NES_DUMP_RAM_LOG=our_bin, NES_AUTO_QUIT_FRAME=str(frames),
               NES_PPU_START_DELAY_DOTS='7524', NES_RAM_ZERO='1', NES_SKIP_FIRST_VBLANK='1')
    if shot:
        env['NES_DUMP_FRAME'] = os.path.join(tmp, 'ours.png')
        env['NES_AUTO_QUIT_FRAME'] = str(shot + (1 if rom_is_game else 0))
        subprocess.run([EMU, rom], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
        env['NES_AUTO_QUIT_FRAME'] = str(frames)
        env.pop('NES_DUMP_FRAME')
    subprocess.run([EMU, rom], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    ref, ours = records(ref_bin), records(our_bin)
    bad = []
    for f in sorted(ours):
        if f in ref:
            d = [a for a in ram_range if ref[f][a] != ours[f][a]]
            if len(d) > tolerance:
                bad.append((f, [(hex(a), ref[f][a], ours[f][a]) for a in d[:6]]))
    result = 'PASS' if not bad else 'FAIL'
    line = f'{name}: RAM {result}'
    if bad:
        line += f' (first diff frame {bad[0][0]}: {bad[0][1]}; {len(bad)} frames differ)'
    if shot:
        shots = sorted(glob.glob(os.path.join(snaps, snap_prefix + '*.png')))
        if shots:
            mism = structural_mismatch(shots[-1], os.path.join(tmp, 'ours.png'))
            line += f'; picture mismatch {mism} px'
            if mism > 300:
                result = 'FAIL'
    print(line)
    return result == 'PASS'


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1].endswith('.nes'):
        # generic mode for any ROM: run_compare.py GAME.nes INPUT.txt FRAMES [SHOT_FRAME]
        rom, inp, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
        shot = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        events = [l.strip() for l in open(inp) if l.strip()]
        ram = [a for a in range(0x800) if not 0x100 <= a < 0x200]
        ok = run('game', 3, rom=rom, cases={'game': (frames, shot, events)}, ram_range=ram)
        sys.exit(0 if ok else 1)
    names = sys.argv[1:] or list(CASES)
    ok = True
    for n in names:
        tol = 1 if n == 'timing' else 0
        ok &= run(n, tol)
    sys.exit(0 if ok else 1)
