#!/usr/bin/env python3
"""handsdepth_aim.py RUN FRAME WHAT [OFF_CM] - where to put a test hand so it goes into the world (build 11's world
depth, docs/re-xr-pipeline.md 33.8): reads the dump FRAME's world depth (RUN/xr-layer.log.worldN.pgm, 16-bit mm) and
eye 0's pose and fov (its `xr: dump layers frame N: eye 0` line) and prints "x y z dist": the point of the world's
surface seen at WHAT, OFF_CM farther along eye 0's ray (into the surface; negative = in front of it), in SOMVR_XR_TEST's
head frame (cm: x right, y up, z forward, levelled; the head = the eyes' midpoint), and the surface's view distance (m).
WHAT: ground (the lower middle of eye 0), wall (the left of eye 0, 1-8 m), near (the nearest thing 0.35-2.5 m in
the middle: an orc), px:U,V (eye 0's fraction). Exit 1: nothing there."""
import re, sys, os
import numpy as np

run, n, what = sys.argv[1], int(sys.argv[2]), sys.argv[3]
off = float(sys.argv[4]) * 0.01 if len(sys.argv) > 4 else 0.0
eyes = {}
pat = re.compile(r"xr: dump layers frame %d: eye (\d) pose ([-+\d.]+),([-+\d.]+),([-+\d.]+) q ([-+\d.]+),([-+\d.]+),"
                 r"([-+\d.]+),([-+\d.]+) fov ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) ([-+\d.]+)" % n)
with open(os.path.join(run, 'xr-layer.log'), errors='replace') as f:
    for ln in f:
        m = pat.search(ln)
        if m:
            v = [float(m.group(i)) for i in range(2, 13)]
            eyes[int(m.group(1))] = {'p': np.array(v[0:3]), 'q': v[3:7], 'fov': v[7:11]}
if 0 not in eyes or 1 not in eyes:
    sys.exit('no eye poses for frame %d' % n)
with open(os.path.join(run, 'xr-layer.log.world%d.pgm' % n), 'rb') as f:
    f.readline(); w, h = map(int, f.readline().split()); f.readline()
    z = np.frombuffer(f.read(w * h * 2), dtype='>u2').reshape(h, w).astype(np.float64) / 1000.0
W = w // 2
z0 = z[:, :W]


def qmat(q):
    x, y, zz, ww = q
    return np.array([[1 - 2 * (y * y + zz * zz), 2 * (x * y - zz * ww), 2 * (x * zz + y * ww)],
                     [2 * (x * y + zz * ww), 1 - 2 * (x * x + zz * zz), 2 * (y * zz - x * ww)],
                     [2 * (x * zz - y * ww), 2 * (y * zz + x * ww), 1 - 2 * (x * x + y * y)]])


def pick():
    if what == 'ground':
        cands = [(0.5, v) for v in (0.90, 0.86, 0.82, 0.78)]
        lo, hi = 0.6, 3.5
    elif what == 'wall':
        cands = [(u, v) for v in (0.50, 0.45, 0.55) for u in (0.12, 0.18, 0.08, 0.24)]
        lo, hi = 1.0, 8.0
    elif what == 'near':
        sub = z0[int(0.3 * h):int(0.85 * h), int(0.2 * W):int(0.8 * W)]
        ok = (sub > 0.35) & (sub < 2.5)
        if not ok.any():
            return None
        mn = sub[ok].min()
        ys, xs = np.nonzero(ok & (sub < mn + 0.25))
        cy, cx = int(np.median(ys)) + int(0.3 * h), int(np.median(xs)) + int(0.2 * W)
        return cx, cy
    else:
        u, v = map(float, what.split(':')[1].split(','))
        cands = [(u, v)]
        lo, hi = 0.2, 50.0
    for u, v in cands:
        x, y = int(u * W), int(v * h)
        d = z0[max(0, y - 3):y + 4, max(0, x - 3):x + 4]
        if d.size and (d > 0).all() and lo <= np.median(d) <= hi and d.max() - d.min() < 0.1 * np.median(d):
            return x, y
    return None


xy = pick()
if xy is None:
    sys.exit(1)
x, y = xy
e = eyes[0]
l, r, u, dn = (np.tan(a) for a in e['fov'])
tx = l + (x + 0.5) / W * (r - l)
ty = u - (y + 0.5) / h * (u - dn)
zf = float(np.median(z0[max(0, y - 2):y + 3, max(0, x - 2):x + 3]))
ray = np.array([tx, ty, -1.0])
R = qmat(e['q'])
pt = e['p'] + R @ (ray * zf + ray / np.linalg.norm(ray) * off)
head = (eyes[0]['p'] + eyes[1]['p']) / 2
f = R @ np.array([0, 0, -1.0])
hx, hz = f[0], f[2]
nn = (hx * hx + hz * hz) ** 0.5
hx, hz = hx / nn, hz / nn
dv = pt - head
print('%.1f %.1f %.1f %.3f' % (100 * (dv[0] * -hz + dv[2] * hx), 100 * dv[1], 100 * (dv[0] * hx + dv[2] * hz), zf))
