#!/usr/bin/env python3
"""handsdepth_stereo.py RUN FRAME... - is the world's depth (build 11, docs/re-xr-pipeline.md 33.8) metric? For each
depth range, every 6th pixel of eye 0's depth (RUN/xr-layer.log.worldN.pgm, 16-bit mm) is put into the world at the
distance x c (c = 0.80..1.30) along its ray and seen from eye 1 (both eyes' poses and fovs from the dump's lines); the c
at which eye 1's depth there agrees (within 0.5 %) is the scale the eyes' stereo says the depth is off by. 1.00 = the
depth is where the eyes see the thing (the world); Talion's held items read 1.16-1.18."""
import re, sys, os
import numpy as np


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def frame(run, n):
    pat = re.compile(r"xr: dump layers frame %d: eye (\d) pose ([-+\d.]+),([-+\d.]+),([-+\d.]+) q ([-+\d.]+),"
                     r"([-+\d.]+),([-+\d.]+),([-+\d.]+) fov ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) ([-+\d.]+)" % n)
    eyes = {}
    with open(os.path.join(run, 'xr-layer.log'), errors='replace') as f:
        for ln in f:
            m = pat.search(ln)
            if m:
                v = [float(m.group(i)) for i in range(2, 13)]
                eyes[int(m.group(1))] = (np.array(v[:3]), qmat(v[3:7]), np.tan(v[7:11]))
    with open(os.path.join(run, 'xr-layer.log.world%d.pgm' % n), 'rb') as f:
        f.readline(); w, h = map(int, f.readline().split()); f.readline()
        z = np.frombuffer(f.read(w * h * 2), dtype='>u2').reshape(h, w).astype(np.float64) / 1000
    W = w // 2
    (p0, R0, t0), (p1, R1, t1) = eyes[0], eyes[1]
    z0, z1 = z[:, :W], z[:, W:]
    ys, xs = np.mgrid[0:h:6, 0:W:6]
    ys, xs = ys.ravel(), xs.ravel()
    keep = z0[ys, xs] > 0.05
    ys, xs = ys[keep], xs[keep]
    l, r, u, d = t0
    tx, ty = l + (xs + 0.5) / W * (r - l), u - (ys + 0.5) / h * (u - d)
    cs = np.arange(0.80, 1.305, 0.01)
    err = np.full((len(cs), len(xs)), 9.0)
    for i, c in enumerate(cs):
        zf = z0[ys, xs] * c
        q = ((np.stack([tx * zf, ty * zf, -zf], 1)) @ R0.T + p0 - p1) @ R1
        zq = -q[:, 2]
        l1, r1, u1, d1 = t1
        px = np.round((q[:, 0] / zq - l1) / (r1 - l1) * W - 0.5).astype(int)
        py = np.round((u1 - q[:, 1] / zq) / (u1 - d1) * h - 0.5).astype(int)
        ok = (px >= 0) & (px < W) & (py >= 0) & (py < h)
        e = np.full(len(xs), 9.0)
        e[ok] = np.abs(z1[py[ok], px[ok]] * c - zq[ok]) / zq[ok]
        err[i] = e
    best, good, zz = cs[np.argmin(err, 0)], err.min(0) < 0.005, z0[ys, xs]
    for lo, hi in ((0.05, 0.3), (0.3, 0.6), (0.6, 1.0), (1.0, 2.0), (2.0, 5.0), (5.0, 70.0)):
        m = good & (zz >= lo) & (zz < hi)
        if m.sum() < 20:
            continue
        print('frame %d %5.2f-%5.2f m: %6d px, the eyes agree at x%.2f (median)' % (n, lo, hi, m.sum(), np.median(best[m])))


for a in sys.argv[2:]:
    frame(sys.argv[1], int(a))
