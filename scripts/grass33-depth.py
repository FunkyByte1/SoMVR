#!/usr/bin/env python3
"""grass33-depth.py - the meadow grass's depth in the two eyes (docs/re-xr-pipeline.md 148; scripts/grass33-drive.sh's dumps).

  grass33-depth.py RUN NAME [NAME ...] [--box x0,y0,x1,y1]       per pose: the grass's depth from the eyes' disparity
  grass33-depth.py --ab RUN_A RUN_B NAME [NAME ...] [--box ..]   the same poses in two boots side by side

NAME = a pose in RUN/fx-steps.txt (one lossless stereo dump: <log>.stereoN.ppm, both eyes side by side; the eyes' poses
and fovs from the layer's `xr: dump layers frame N` line). In the box (fractions of the left eye; default the lower
middle, where the grass is when the head looks down) every 12x12 block of the left eye with texture is matched along
its row in the right eye (normalized cross-correlation, shifts 0..160 px, a parabola through the best three for the
sub-pixel); kept when the match is clear (NCC >= 0.8, the second-best peak >= 3 px away clearly lower). Depth from the
disparity: Z = s * b / (d - c), s = the eye's width / (tan r - tan l), b = the eyes' distance, c = s * (tan l_R - tan
l_L) (infinity's disparity). Prints the blocks kept, the depth's p10 / p50 / p90 and its local spread (the median, over
blocks, of |Z - the median Z of its 5x5 neighbours| = how much the depth varies inside a patch of grass: a flat card is
0, a tuft with depth more), in cm.
"""
import argparse
import math
import os
import re
import sys

import numpy as np
from PIL import Image

POSE = re.compile(r"xr: dump layers frame (\d+): eye (\d) pose ([-+\d.]+),([-+\d.]+),([-+\d.]+) q ([-+\d.]+),([-+\d.]+),"
                  r"([-+\d.]+),([-+\d.]+) fov ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) ([-+\d.]+)")


def frames(run):
    out = {}
    p = os.path.join(run, 'fx-steps.txt')
    for line in open(p):
        f = line.split()
        if len(f) == 2:
            out[f[0]] = int(f[1])
    return out


def eyes_of(run, frame):
    log = os.path.join(run, 'xr-layer.log')
    e = {}
    for line in open(log, errors='replace'):
        m = POSE.search(line)
        if m and int(m.group(1)) == frame:
            e[int(m.group(2))] = [float(m.group(i)) for i in range(3, 14)]
    img = Image.open('%s.stereo%d.ppm' % (log, frame)).convert('L')
    a = np.asarray(img, dtype=np.float32)
    h, w2 = a.shape
    return a[:, :w2 // 2], a[:, w2 // 2:], e


def disparity(L, R, box, bs=12, dmax=160):
    h, w = L.shape
    x0, y0, x1, y1 = int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)
    x0 = max(x0, dmax + 2)
    nb = (x1 - x0) // bs
    xs = x0 + bs * np.arange(nb)
    pts = []
    for y in range(y0, y1 - bs, bs):
        Lb = L[y:y + bs, x0:x0 + nb * bs].reshape(bs, nb, bs).transpose(1, 0, 2).reshape(nb, -1)
        Lm = Lb - Lb.mean(1, keepdims=True)
        sl = np.sqrt((Lm * Lm).sum(1))
        ncc = np.full((dmax, nb), -1.0, dtype=np.float32)
        for d in range(dmax):
            Rb = R[y:y + bs, x0 - d:x0 - d + nb * bs].reshape(bs, nb, bs).transpose(1, 0, 2).reshape(nb, -1)
            Rm = Rb - Rb.mean(1, keepdims=True)
            sr = np.sqrt((Rm * Rm).sum(1))
            ncc[d] = (Lm * Rm).sum(1) / np.maximum(sl * sr, 1e-6)
        for k_b in range(nb):
            if sl[k_b] < bs * 6:
                continue   # no texture
            best = ncc[:, k_b]
            k = int(best.argmax())
            if best[k] < 0.8 or k == 0 or k == dmax - 1:
                continue
            far = np.concatenate([best[:max(0, k - 3)], best[k + 4:]])
            if far.size and far.max() > best[k] - 0.05:
                continue   # two near-equal peaks: a repeating texture
            a, c0, c1 = best[k - 1], best[k], best[k + 1]
            den = a - 2 * c0 + c1
            sub = 0.5 * (a - c1) / den if den < 0 else 0.0
            pts.append((int(xs[k_b]), y, k + sub))
    return pts


def depth_stats(run, name, box):
    fr = frames(run)
    if name not in fr:
        return None
    L, R, e = eyes_of(run, fr[name])
    if 0 not in e or 1 not in e:
        return None
    w = L.shape[1]
    tl = [math.tan(e[i][7]) for i in (0, 1)]
    tr = [math.tan(e[i][8]) for i in (0, 1)]
    s = w / (tr[0] - tl[0])
    b = math.dist(e[0][:3], e[1][:3]) * 100.0      # cm
    c = s * (tl[1] - tl[0])
    pts = disparity(L, R, box)
    if not pts:
        return dict(n=0)
    zs = {}
    for x, y, d in pts:
        if d - c > 0.5:
            zs[(x, y)] = s * b / (d - c)
    # the depth buffer (the layer's <log>.worldN.pgm: 16 bits, both eyes, the view distance in mm) at each block of the
    # left eye: the card's plane; the disparity's depth against it = where the picture appears off its card
    wp = '%s.world%d.pgm' % (os.path.join(run, 'xr-layer.log'), fr[name])
    off = []
    if os.path.exists(wp):
        k = None
        for line in open(os.path.join(run, 'xr-layer.log'), errors='replace'):
            m = re.search(r'dump layers frame %d: the world.s depth \(depth_k ([\d.]+) m\)' % fr[name], line)
            if m:
                k = float(m.group(1))
        D = np.asarray(Image.open(wp), dtype=np.float64)[:, :w]
        if k:
            for (x, y), z in zs.items():
                v = np.median(D[y:y + 12, x:x + 12])     # the view distance in mm (0 = the sky / past 65.5 m)
                if v > 0:
                    zb = v / 10.0
                    if zb < 400:
                        off.append(z - zb)
    off = np.array(off)
    if not zs:
        return dict(n=0)
    z = np.array(list(zs.values()))
    bsz = 12
    loc = []
    for (x, y), v in zs.items():
        nb = [zs[(x + i * bsz, y + j * bsz)] for i in range(-2, 3) for j in range(-2, 3) if (x + i * bsz, y + j * bsz) in zs]
        if len(nb) >= 8:
            loc.append(abs(v - float(np.median(nb))))
    return dict(n=len(z), p10=np.percentile(z, 10), p50=np.percentile(z, 50), p90=np.percentile(z, 90),
                spread=float(np.median(loc)) if loc else float('nan'), nloc=len(loc), b=b, s=s, c=c,
                noff=len(off), off10=np.percentile(off, 10) if len(off) else float('nan'),
                off50=np.percentile(off, 50) if len(off) else float('nan'),
                off90=np.percentile(off, 90) if len(off) else float('nan'),
                near=float((off < -5).mean()) if len(off) else float('nan'))


def fmt(st):
    if not st:
        return 'no dump'
    if not st['n']:
        return 'no blocks'
    return ('%4d blocks  depth p10 %6.1f  p50 %6.1f  p90 %6.1f cm | off the depth buffer (%d within 4 m): p10 %6.1f  '
            'p50 %5.1f  p90 %5.1f cm, nearer by > 5 cm %4.1f %%' %
            (st['n'], st['p10'], st['p50'], st['p90'], st['noff'], st['off10'], st['off50'], st['off90'], 100 * st['near']))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('args', nargs='+')
    ap.add_argument('--ab', action='store_true')
    ap.add_argument('--box', default='0.25,0.55,0.75,0.95')
    a = ap.parse_args()
    box = [float(v) for v in a.box.split(',')]
    if a.ab:
        ra, rb, names = a.args[0], a.args[1], a.args[2:]
        for n in names:
            print('%-6s A %s' % (n, fmt(depth_stats(ra, n, box))))
            print('%-6s B %s' % ('', fmt(depth_stats(rb, n, box))))
    else:
        run, names = a.args[0], a.args[1:]
        for n in names:
            print('%-6s %s' % (n, fmt(depth_stats(run, n, box))))


if __name__ == '__main__':
    sys.exit(main())
