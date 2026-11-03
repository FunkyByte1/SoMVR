#!/usr/bin/env python3
"""render31-yaw.py - does a picture stay put in the world when only the head turns? (docs/re-xr-pipeline.md 137)

  render31-yaw.py RUN A B [--eye 0|1] [-o OUT.png] [--crop x0,y0,x1,y1] [--scale S]

A and B are names in RUN/fx-steps.txt (scripts/render31-drive.sh: one lossless stereo dump each, the layer's
<log>.stereoN.ppm with its `xr: dump layers frame N: eye E pose .. q .. fov ..` line). B's eye image is re-projected into
A's eye (a pure rotation: the eye's own turn between the two dumps; the eyes' few centimetres of travel are left out, so
only things farther than a few metres line up), the overlap compared after one gain per colour channel (the exposure
re-adapts after a turn: a whole-picture brightness change is the game's eye adaptation, not something that moves with the
head). Prints the overlap's share, the gains, the mean |A - B'| (0..255) over the overlap and over its upper and lower
halves, and the share of pixels off by more than 12; writes OUT (default RUN/compose/yaw-A-B.png): A, B re-projected,
and |A - B'| x4 side by side (at --scale, default 0.5). World-locked content gives a dark difference picture; a fog or a
card that turns with the head shows as a bright band.
"""
import argparse
import math
import os
import re
import sys

import numpy as np
from PIL import Image

POSE = re.compile(r"xr: dump layers frame (\d+): eye (\d) pose ([-+\d.]+),([-+\d.]+),([-+\d.]+) q ([-+\d.]+),([-+\d.]+),"
                  r"([-+\d.]+),([-+\d.]+) fov ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) rect (\d+),(\d+) (\d+)x(\d+)")


def qmat(x, y, z, w):
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def load(run, name, eye):
    steps = {}
    for line in open(os.path.join(run, "fx-steps.txt")):
        p = line.split()
        if len(p) == 2 and p[1].isdigit():
            steps[p[0]] = int(p[1])
    if name not in steps:
        sys.exit("no %s in %s/fx-steps.txt" % (name, run))
    f = steps[name]
    pose = None
    for line in open(os.path.join(run, "xr-layer.log"), errors="replace"):
        if "dump layers frame %d: eye %d pose" % (f, eye) in line:
            m = POSE.search(line)
            if m:
                pose = m
    if not pose:
        sys.exit("no eye %d pose line for frame %d (%s)" % (eye, f, name))
    q = [float(pose.group(i)) for i in (6, 7, 8, 9)]
    fov = [math.tan(float(pose.group(i))) for i in (10, 11, 12, 13)]
    rx, ry, rw, rh = (int(pose.group(i)) for i in (14, 15, 16, 17))
    img = np.asarray(Image.open(os.path.join(run, "xr-layer.log.stereo%d.ppm" % f)).convert("RGB"), dtype=np.float32)
    return img[ry:ry + rh, rx:rx + rw], qmat(*q), fov


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--eye", type=int, default=0)
    ap.add_argument("-o", "--out")
    ap.add_argument("--crop")
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--search", type=float, default=0.0,
                    help="also search B's world turn about the vertical within +-DEG (the rig's heading drifts between "
                         "dumps headless: the tracking space turns in the world) - best on the far band")
    o = ap.parse_args()
    A, RA, fa = load(o.run, o.a, o.eye)
    B, RB, fb = load(o.run, o.b, o.eye)
    if o.search > 0:
        best = None
        for sc, lo, hi, st in ((0.125, -o.search, o.search, 1.0), (0.25, None, None, 0.1)):
            if lo is None:
                lo, hi = best[1] - 1.0, best[1] + 1.0
            for dd in np.arange(lo, hi + 1e-6, st):
                err = compare(A, RA, fa, B, ry(dd) @ RB, fb, sc, None, quiet=True)
                if best is None or err < best[0]:
                    best = (err, float(dd))
        print("the rig's turn between the dumps: %+.1f deg (far band error %.2f)" % (best[1], best[0]))
        RB = ry(best[1]) @ RB
    compare(A, RA, fa, B, RB, fb, o.scale, o)


def ry(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])


def compare(A, RA, fa, B, RB, fb, s, o, quiet=False):
    h, w = int(A.shape[0] * s), int(A.shape[1] * s)
    Ai = np.asarray(Image.fromarray(A.astype(np.uint8)).resize((w, h), Image.BILINEAR), dtype=np.float32)
    l, r, u, d = fa
    xs = l + (np.arange(w) + 0.5) / w * (r - l)
    ys = u - (np.arange(h) + 0.5) / h * (u - d)
    X, Y = np.meshgrid(xs, ys)
    cam = np.stack([X, Y, -np.ones_like(X)], -1)
    world = cam @ RA.T
    elev = np.degrees(np.arcsin(world[..., 1] / np.linalg.norm(world, axis=-1)))
    far = (elev > -12) & (elev < 4)                                  # the valley's far half and the horizon's haze
    rays = cam @ (RB.T @ RA).T   # A's camera -> the world -> B's camera
    zb = -rays[..., 2]
    ok = zb > 1e-3
    lb, rb, ub, db = fb
    hb, wb = B.shape[:2]
    ub_ = ((rays[..., 0] / np.where(ok, zb, 1)) - lb) / (rb - lb) * wb - 0.5
    vb_ = (ub - rays[..., 1] / np.where(ok, zb, 1)) / (ub - db) * hb - 0.5
    ok &= (ub_ >= 0) & (ub_ <= wb - 1) & (vb_ >= 0) & (vb_ <= hb - 1)
    x0 = np.clip(np.floor(ub_).astype(int), 0, wb - 2)
    y0 = np.clip(np.floor(vb_).astype(int), 0, hb - 2)
    fx = np.clip(ub_ - x0, 0, 1)[..., None]
    fy = np.clip(vb_ - y0, 0, 1)[..., None]
    Bw = (B[y0, x0] * (1 - fx) * (1 - fy) + B[y0, x0 + 1] * fx * (1 - fy) + B[y0 + 1, x0] * (1 - fx) * fy +
          B[y0 + 1, x0 + 1] * fx * fy)
    Bw[~ok] = 0
    m = ok.copy()
    if quiet:
        m &= far
        g = [float((Ai[..., c][m] * Bw[..., c][m]).sum() / max((Bw[..., c][m] ** 2).sum(), 1e-6)) for c in range(3)]
        return float(np.abs(Ai - Bw * np.array(g)).mean(-1)[m].mean()) if m.any() else 1e9
    if o.crop:
        c = [int(float(v) * s) for v in o.crop.split(",")]
        cm = np.zeros_like(m)
        cm[c[1]:c[3], c[0]:c[2]] = True
        m &= cm
    gains = []
    Bg = Bw.copy()
    for ch in range(3):
        a, b = Ai[..., ch][m], Bw[..., ch][m]
        g = float((a * b).sum() / max((b * b).sum(), 1e-6))
        gains.append(g)
        Bg[..., ch] *= g
    D = np.abs(Ai - Bg).mean(-1)
    D[~ok] = 0
    half = h // 2
    top, bot = m.copy(), m.copy()
    top[half:] = False
    bot[:half] = False
    fm = m & far
    print("%s vs %s (eye %d): overlap %.0f %%, gains %.3f %.3f %.3f; mean |A-B'| %.2f (upper half %.2f, lower %.2f; the "
          "far band -12..+4 deg %.2f); off by > 12: %.2f %% (far band %.2f %%)" % (
              o.a, o.b, o.eye, 100.0 * m.sum() / m.size, *gains, D[m].mean(), D[top].mean() if top.any() else 0,
              D[bot].mean() if bot.any() else 0, D[fm].mean() if fm.any() else 0, 100.0 * (D[m] > 12).mean(),
              100.0 * (D[fm] > 12).mean() if fm.any() else 0))
    out = o.out or os.path.join(o.run, "compose", "yaw-%s-%s.png" % (o.a, o.b))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    pic = np.concatenate([Ai, Bg, np.repeat(np.clip(D * 4, 0, 255)[..., None], 3, -1)], 1)
    Image.fromarray(np.clip(pic, 0, 255).astype(np.uint8)).save(out)
    print(out)


main()
