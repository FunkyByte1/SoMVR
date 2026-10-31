#!/usr/bin/env python3
"""fx27-eyediff.py RUN [-o DIR] - som-b27-fx's per-eye check (docs/re-xr-pipeline.md 125).

fx27-drive.sh's `ds` steps (RUN/fx27-steps.txt: `NAME FRAME TIME`, NAME ending in -same) dump a stereo frame with both
eyes rendered from one pose (`fx eyesame 1`): the two halves of RUN/xr-layer.log.stereoN.ppm should be the same picture.
Per step: the mean absolute difference (0..255, the largest of R, G, B), the share of pixels off by more than 8 and 40,
and the box (x0,y0-x1,y1 in eye pixels) holding 99 % of the pixels off by more than 40 - where a per-eye effect sits.
-o DIR: the difference x4 as PNG per step."""
import sys, os, re
import numpy as np

def ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    w, h = int(m.group(1)), int(m.group(2))
    return np.frombuffer(data[m.end():m.end() + w * h * 3], np.uint8).reshape(h, w, 3)

def main():
    a = sys.argv[1:]
    out = None
    if '-o' in a:
        i = a.index('-o'); out = a[i + 1]; del a[i:i + 2]
    run = a[0]
    steps = [l.split() for l in open(os.path.join(run, 'fx27-steps.txt')) if l.strip()]
    for st in steps:
        if len(st) < 2 or not st[1].isdigit():
            continue
        name, fr = st[0], st[1]
        if len(a) < 2 and not name.endswith('-same'):
            continue
        p = os.path.join(run, 'xr-layer.log.stereo%s.ppm' % fr)
        if not os.path.exists(p):
            print('%-22s frame %s: no stereo dump' % (name, fr)); continue
        im = ppm(p)
        w = im.shape[1] // 2
        l, r = im[:, :w].astype(np.int16), im[:, w:2 * w].astype(np.int16)
        dd = np.abs(l - r).max(axis=2)
        big = dd > 40
        box = '-'
        if big.sum() > 20:
            ys, xs = np.nonzero(big)
            x0, x1 = np.percentile(xs, [0.5, 99.5]); y0, y1 = np.percentile(ys, [0.5, 99.5])
            box = '%d,%d-%d,%d of %dx%d' % (x0, y0, x1, y1, w, im.shape[0])
        print('%-22s frame %-6s mean %.2f  >8 %.3f %%  >40 %.3f %%  box %s' % (name, fr, dd.mean(), (dd > 8).mean() * 100,
                                                                         big.mean() * 100, box))
        if out:
            from PIL import Image
            os.makedirs(out, exist_ok=True)
            Image.fromarray(np.clip(dd * 4, 0, 255).astype(np.uint8)).save(os.path.join(out, '%s-%s.png' % (name, fr)))

main()
