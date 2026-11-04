#!/usr/bin/env python3
"""looks37-perf.py [--md] LABEL=RUN ... - som-b37-looks (docs/re-winport.md 7.15): the frame cost of each preset boot,
per window of scripts/perf27-drive.sh (the words `perf NAME`): the layer's `xr: perf NAME:` lines (GPU total p50/p95, the
shader clock and its busy share) and the preload's `PERF NAME t=` lines (presents a second, the render thread's frame
work wall p50/p95; frames a second = 1000 / the game thread's mean frame interval, its p95 = late frames). Each line
holds a window's first 2048 frames (PF_WIN, PFG_WIN: 22.7 s at 90 Hz). A window logged in several lines (SOMVR_PERF_S / SOMVR_XR_PERF_S shorter than it) is averaged
(p95: the mean and the max). GPU work in Mcycles = GPU ms x the shader clock (MHz) / 1000: the tests run on the GPU's
Default profile, whose clock follows the load, so ms alone understate a heavier preset (re-winport.md 7.15).
--md: a markdown table. --base LABEL: each window's GPU (ms and Mcycles) also as a ratio to that boot's."""
import re, sys, os

def stats(s):
    m = re.match(r'([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)', s)
    return [float(v) for v in m.groups()] if m else None

def windows(run):
    w = {}
    xr = os.path.join(run, 'xr-layer.log')
    for line in open(xr, errors='replace') if os.path.exists(xr) else []:
        m = re.search(r'xr: perf ([A-Za-z0-9_.-]+): ([\d.]+) s, (\d+) frames .*?GPU total ([\d./]+)', line)
        if not m:
            continue
        d = w.setdefault(m.group(1), {'gpu': [], 'clk': [], 'busy': [], 'xs': 0.0, 'xf': 0, 'iv': [], 'fw': []})
        d['gpu'].append(stats(m.group(4)))
        d['xs'] += float(m.group(2)); d['xf'] += int(m.group(3))
        c = re.search(r'shader clock (\d+) MHz, busy (\d+)%', line) or re.search(r'clock (\d+) MHz, busy (\d+)%', line)
        if c:
            d['clk'].append(int(c.group(1))); d['busy'].append(int(c.group(2)))
    sl = os.path.join(run, 'somvr.log')
    for line in open(sl, errors='replace') if os.path.exists(sl) else []:
        m = re.match(r'PERF ([A-Za-z0-9_.-]+) t=[\d.]+ s=([\d.]+) frames=\d+', line)
        if not m or 'shadow passes' in line:
            continue
        d = w.setdefault(m.group(1), {'gpu': [], 'clk': [], 'busy': [], 'xs': 0.0, 'xf': 0, 'iv': [], 'fw': []})
        p = re.search(r'game thread: busy [^ ]+ \(over [^)]*\) wall ([\d./]+)', line)
        if p and stats(p.group(1)):
            d['iv'].append(stats(p.group(1)))
        f = re.search(r'its frame work \(eye 0\'s start to the present: \d+ frames\) cpu [^ ]+ \(over: \d+\) wall ([\d./]+)', line)
        if f and stats(f.group(1)):
            d['fw'].append(stats(f.group(1)))
    return w

def mean(v):
    return sum(v) / len(v) if v else float('nan')

def row(d):
    g = d['gpu']
    r = {'p50': mean([x[1] for x in g]), 'p95': mean([x[2] for x in g]), 'p95max': max([x[2] for x in g], default=float('nan')),
         'clk': mean(d['clk']), 'busy': mean(d['busy']),
         'fps': 1000.0 / mean([x[0] for x in d['iv']]) if d['iv'] else float('nan'),
         'iv95': mean([x[2] for x in d['iv']]),
         'fw50': mean([x[1] for x in d['fw']]), 'fw95': mean([x[2] for x in d['fw']])}
    r['mc50'] = r['p50'] * r['clk'] / 1000.0
    r['mc95'] = r['p95'] * r['clk'] / 1000.0
    return r

args = sys.argv[1:]
md = '--md' in args
base = None
if '--base' in args:
    base = args[args.index('--base') + 1]
    del args[args.index('--base'):args.index('--base') + 2]
args = [a for a in args if a != '--md']
runs = [(a.split('=', 1) if '=' in a else (os.path.basename(a.rstrip('/')), a)) for a in args]
rows = {}
for label, run in runs:
    for name, d in windows(run).items():
        if name in ('end', 'after') or not d['gpu']:
            continue
        rows[(label, name)] = row(d)
names = []
for (_, n) in rows:
    if n not in names:
        names.append(n)
hdr = ['window', 'boot', 'GPU p50', 'p95 (max)', 'clock MHz', 'busy %', 'Mcyc p50', 'Mcyc p95', 'fps (frame p95 ms)', 'RT work p50 (p95)']
if base:
    hdr += ['x base ms', 'x base Mcyc']
out = []
for n in names:
    for label, _ in runs:
        r = rows.get((label, n))
        if not r:
            continue
        c = [n, label, f"{r['p50']:.2f}", f"{r['p95']:.2f} ({r['p95max']:.2f})", f"{r['clk']:.0f}", f"{r['busy']:.0f}",
             f"{r['mc50']:.1f}", f"{r['mc95']:.1f}", f"{r['fps']:.1f} ({r['iv95']:.2f})", f"{r['fw50']:.2f} ({r['fw95']:.2f})"]
        if base:
            b = rows.get((base, n))
            c += [f"{r['p50'] / b['p50']:.3f}" if b else '-', f"{r['mc50'] / b['mc50']:.3f}" if b else '-']
        out.append(c)
if md:
    print('| ' + ' | '.join(hdr) + ' |'); print('|' + '---|' * len(hdr))
    for c in out:
        print('| ' + ' | '.join(c) + ' |')
else:
    wd = [max(len(str(x[i])) for x in [hdr] + out) for i in range(len(hdr))]
    for c in [hdr] + out:
        print('  '.join(str(v).ljust(wd[i]) for i, v in enumerate(c)))
