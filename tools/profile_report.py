#!/usr/bin/env python3
"""Turns a trace written with NES_PROFILE=<file.json> into readable results.

  python3 tools/profile_report.py trace.json                 # table + pictures next to the trace
  python3 tools/profile_report.py trace.json --from 2000 --to 2100   # timeline window in ms
  python3 tools/profile_report.py trace.json --name render_background_scanline --a-min 100 --a-max 140

Output: a table (calls, total, mean, max, share of wall time) per event name, the
"hot" totals from <trace>.summary.json, `<trace>.timeline.png` (bars per thread and
name over time - see what starts when) and `<trace>.frames.png` (frame duration over
time, spikes are hitches). Open the .json in https://ui.perfetto.dev for an
interactive view as well. Needs Pillow (PIL).
"""
import argparse
import json
import os
import sys
from collections import defaultdict


def load(path):
    with open(path) as f:
        data = json.load(f)
    threads, events = {}, []
    for e in data['traceEvents']:
        if e.get('ph') == 'M' and e.get('name') == 'thread_name':
            threads[e['tid']] = e['args']['name']
        elif e.get('ph') == 'X':
            a = e.get('args', {})
            events.append((e['name'], e['tid'], e['ts'], e['dur'], a.get('id', 0), a.get('a', 0), a.get('b', 0)))
    summary = {}
    if os.path.exists(path + '.summary.json'):
        with open(path + '.summary.json') as f:
            summary = json.load(f)
    return threads, events, summary


def table(events, threads, summary, args):
    wall_us = (max(e[2] + e[3] for e in events) - min(e[2] for e in events)) if events else 0
    stat = defaultdict(lambda: [0, 0.0, 0.0])
    for name, tid, ts, dur, _id, a, b in events:
        s = stat[(threads.get(tid, tid), name)]
        s[0] += 1
        s[1] += dur
        s[2] = max(s[2], dur)
    print(f'wall time covered by events: {wall_us / 1000:.1f} ms\n')
    print(f'{"thread":8} {"name":28} {"calls":>9} {"total ms":>10} {"mean us":>9} {"max us":>9} {"% wall":>7}')
    for (thread, name), (n, total, mx) in sorted(stat.items(), key=lambda kv: -kv[1][1]):
        print(f'{str(thread):8} {name:28} {n:9d} {total / 1000:10.1f} {total / n:9.1f} {mx:9.1f} {100 * total / wall_us if wall_us else 0:7.1f}')
    hot = summary.get('hot', [])
    if hot:
        wall_ns = summary.get('wall_ns', 1)
        print('\nhot totals (many tiny calls, no single events):')
        print(f'{"name":28} {"calls":>12} {"total ms":>10} {"mean ns":>9} {"max us":>9} {"% wall":>7}')
        for h in sorted(hot, key=lambda h: -h['total_ns']):
            if h['calls']:
                print(f'{h["name"]:28} {h["calls"]:12d} {h["total_ns"] / 1e6:10.1f} {h["total_ns"] / h["calls"]:9.0f} {h["max_ns"] / 1000:9.1f} {100 * h["total_ns"] / wall_ns:7.1f}')
    if summary.get('dropped_events'):
        print(f'\nWARNING: {summary["dropped_events"]} events were dropped (buffer full)')


PALETTE = [(31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75),
           (227, 119, 194), (127, 127, 127), (188, 189, 34), (23, 190, 207), (174, 199, 232), (255, 187, 120)]


def font(size):
    from PIL import ImageFont
    try:
        return ImageFont.load_default(size)
    except TypeError:  # very old Pillow
        return ImageFont.load_default()


def timeline(events, threads, path, args):
    """Bars per thread over time; nested calls are drawn one row lower."""
    from PIL import Image, ImageDraw
    t0 = min(e[2] for e in events)
    lo = t0 + (args.from_ms or 0) * 1000
    hi = lo + ((args.to_ms - (args.from_ms or 0)) * 1000 if args.to_ms else 100_000)  # default: 100 ms window
    sel = [e for e in events if e[2] + e[3] >= lo and e[2] <= hi and (not args.name or e[0] == args.name)
           and args.a_min <= e[5] <= args.a_max]
    if not sel:
        print('no events in the selected window')
        return
    names = sorted({e[0] for e in sel})
    tids = sorted({e[1] for e in sel})
    colors = {n: PALETTE[i % len(PALETTE)] for i, n in enumerate(names)}
    left, right, top, rowh, depth_max = 90, 260, 30, 14, 5
    plot_w = 1600
    lane_h = rowh * depth_max + 12
    img = Image.new('RGB', (left + plot_w + right, top + lane_h * len(tids) + 40), 'white')
    d = ImageDraw.Draw(img)
    f, fs = font(13), font(11)
    scale = plot_w / (hi - lo)
    for lane, tid in enumerate(tids):
        y0 = top + lane * lane_h
        d.text((6, y0 + 4), str(threads.get(tid, tid)), fill='black', font=f)
        d.line([(left, y0 + lane_h - 4), (left + plot_w, y0 + lane_h - 4)], fill=(210, 210, 210))
        evs = sorted((e for e in sel if e[1] == tid), key=lambda e: (e[2], -e[3]))
        stack = []
        for e in evs:
            while stack and stack[-1] <= e[2]:
                stack.pop()
            depth = min(len(stack), depth_max - 1)
            stack.append(e[2] + e[3])
            x1 = left + max(0, (e[2] - lo)) * scale
            x2 = left + min(hi - lo, (e[2] + e[3] - lo)) * scale
            d.rectangle([x1, y0 + depth * rowh, max(x2, x1 + 1), y0 + depth * rowh + rowh - 2], fill=colors[e[0]])
    y_axis = top + lane_h * len(tids) + 4
    step_ms = max(1, round((hi - lo) / 1000 / 10))
    for k in range(0, int((hi - lo) / 1000) + 1, step_ms):
        x = left + k * 1000 * scale
        d.line([(x, top - 4), (x, y_axis)], fill=(225, 225, 225))
        d.text((x - 10, y_axis + 2), f'{(lo - t0) / 1000 + k:.0f} ms', fill='black', font=fs)
    for i, n in enumerate(names):
        d.rectangle([left + plot_w + 12, top + i * 16, left + plot_w + 22, top + i * 16 + 10], fill=colors[n])
        d.text((left + plot_w + 28, top + i * 16 - 2), n, fill='black', font=fs)
    d.text((left, 6), 'timeline: each bar = one call, lower rows = calls nested inside it', fill='black', font=f)
    out = path + '.timeline.png'
    img.save(out)
    print('wrote', out)


def frames(events, path):
    """Frame duration over time; peaks are hitches."""
    from PIL import Image, ImageDraw
    fr = sorted(e for e in events if e[0] == 'frame')
    if not fr:
        return
    t0 = min(e[2] for e in events)
    xs = [(e[2] - t0) / 1e6 for e in fr]
    ys = [e[3] / 1000 for e in fr]
    w, h, l, b = 1400, 420, 60, 40
    ymax = max(max(ys) * 1.1, 20)
    img = Image.new('RGB', (w + l + 20, h + b + 20), 'white')
    d = ImageDraw.Draw(img)
    f = font(12)
    X = lambda x: l + x / max(xs[-1], 1e-9) * w
    Y = lambda y: 10 + h - y / ymax * h
    for ms in range(0, int(ymax) + 1, 10):
        d.line([(l, Y(ms)), (l + w, Y(ms))], fill=(230, 230, 230))
        d.text((8, Y(ms) - 6), f'{ms} ms', fill='black', font=f)
    d.line([(l, Y(16.64)), (l + w, Y(16.64))], fill=(214, 39, 40), width=2)
    d.text((l + 6, Y(16.64) - 16), 'real NES frame time (16.64 ms)', fill=(214, 39, 40), font=f)
    d.line([(X(x), Y(y)) for x, y in zip(xs, ys)], fill=(31, 119, 180), width=1)
    d.text((l, 10 + h + 6), f'time (s): 0 ... {xs[-1]:.1f}', fill='black', font=f)
    out = path + '.frames.png'
    img.save(out)
    avg = sum(ys) / len(ys)
    print(f'frames: {len(fr)}, mean {avg:.2f} ms ({1000 / avg:.1f} fps), worst {max(ys):.2f} ms; wrote {out}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--from', dest='from_ms', type=float, help='timeline window start in ms')
    ap.add_argument('--to', dest='to_ms', type=float, help='timeline window end in ms (default: 100 ms after start)')
    ap.add_argument('--name', help='only this event name in the timeline')
    ap.add_argument('--a-min', type=int, default=-2**63, help='only events with info a >= this (e.g. scanline)')
    ap.add_argument('--a-max', type=int, default=2**63 - 1)
    a = ap.parse_args()
    if not os.path.exists(a.trace):
        # Most common mistake: the trace was recorded in another folder than the one you are in now.
        here = os.getcwd()
        root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        name = os.path.basename(a.trace)
        found = [os.path.join(d, name) for d in {here, root, os.path.join(root, 'build')} if os.path.exists(os.path.join(d, name))]
        print(f'File not found: {os.path.abspath(a.trace)}\n'
              f'The trace is written where you STARTED the emulator (not into build/). Record it first:\n'
              f'    NES_PROFILE={name} ./build/nes-bench tests/roms/timing.nes 1500     # run in the project folder')
        if found:
            print('A file with that name exists here - use its full path:\n    ' + '\n    '.join(found))
        sys.exit(1)
    th, ev, sm = load(a.trace)
    if not ev:
        sys.exit('no events in trace')
    table(ev, th, sm, a)
    frames(ev, a.trace)
    timeline(ev, th, a.trace, a)
