#!/usr/bin/env python3
"""bow_tips.py RUN FRAME up|down [--thr LUM] [--win PX] [--out PNG]: the bow's limb tip seen in both eyes of a stereo
dump (build 33b, docs/re-xr-pipeline.md 142.9) against our string's end at that frame. --pick: the two eyes' pixels of
another point instead (where the game's own string meets the limb: the model's string end - the alignment build 33b
took).

The string's ends and the bow's axes come from somvr.log's `BOW slog frame=F: lo X,Y,Z hi X,Y,Z (XR m); the bow's limbs
X,Y,Z shot X,Y,Z (XR); stips K B lo K B` (the word `bow slog N`), the line nearest FRAME. In each eye of
<xr-layer.log>.stereoFRAME.ppm the end is projected (the dump's eye pose and fov) and the bow's bright pixels (luminance
over --thr) within --win pixels of it are searched for the one furthest out along the bow's limbs (up: the upper end's
way, down: the lower's): the tip. The two eyes' rays through their tip pixels are closed (their nearest points' middle):
the tip in XR. Printed: the tip, the end, the end -> tip offset along the bow's limbs, its shot and across (cm), and the
stips and SOMVR_BOW_STRING_SIDE_CM changes that move the end there (ends = the tip node + K along the limb's last bone - B along the shot; the last
bones from the 7 nodes: upper 0,0.978,-0.208, lower 0.009,-0.966,-0.259). --out: both eyes with the end (red) and the
tip (green) marked."""
import argparse
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xr_compose as xc  # noqa: E402

SLOG = re.compile(r"BOW slog frame=(\d+): lo ([-\d.]+),([-\d.]+),([-\d.]+) hi ([-\d.]+),([-\d.]+),([-\d.]+) \(XR m\); the bow's "
                  r"limbs ([-\d.]+),([-\d.]+),([-\d.]+) shot ([-\d.]+),([-\d.]+),([-\d.]+) \(XR\); stips ([-\d.]+) ([-\d.]+) lo "
                  r"([-\d.]+) ([-\d.]+)")
BONE = {"up": np.array([0.0, 0.978, -0.208]), "down": np.array([0.009, -0.966, -0.259])}


def slog_at(run, n, burst=0):
    """the slog line nearest frame n - or (burst K) the middle line of the K-th burst of them (the game's frame ids are
    not the layer's: a burst is one `bow slog N`, taken right before the dump)"""
    rows = []
    with open(os.path.join(run, "somvr.log"), errors="replace") as f:
        for ln in f:
            m = SLOG.search(ln)
            if m:
                v = [float(m.group(i)) for i in range(2, 18)]
                rows.append((int(m.group(1)), np.array(v[0:3]), np.array(v[3:6]), np.array(v[6:9]), np.array(v[9:12]),
                             v[12:16]))
    if not rows:
        return None
    if burst:
        groups, cur = [], [rows[0]]
        for r in rows[1:]:
            if r[0] - cur[-1][0] > 5:
                groups.append(cur)
                cur = [r]
            else:
                cur.append(r)
        groups.append(cur)
        g = groups[min(burst, len(groups)) - 1]
        return g[len(g) // 2]
    return min(rows, key=lambda r: abs(r[0] - n))


def project(eye, w, h, P):
    """pixel (x, y) of XR point P in this eye (None behind)"""
    d = xc.qmat(eye["q"]).T @ (P - eye["p"])
    if d[2] >= -1e-6:
        return None
    l, r, u, dn = (math.tan(v) for v in eye["fov"])
    tx, ty = d[0] / -d[2], d[1] / -d[2]
    return ((tx - l) / (r - l) * w - 0.5, (u - ty) / (u - dn) * h - 0.5)


def ray(eye, w, h, px, py):
    l, r, u, dn = (math.tan(v) for v in eye["fov"])
    tx = l + (px + 0.5) / w * (r - l)
    ty = u - (py + 0.5) / h * (u - dn)
    d = xc.qmat(eye["q"]) @ np.array([tx, ty, -1.0])
    return eye["p"], d / np.linalg.norm(d)


def closest(o1, d1, o2, d2):
    w0 = o1 - o2
    a, b, c, d, e = d1 @ d1, d1 @ d2, d2 @ d2, d1 @ w0, d2 @ w0
    den = a * c - b * b
    s = (b * e - c * d) / den if abs(den) > 1e-12 else 0.0
    t = (a * e - b * d) / den if abs(den) > 1e-12 else 0.0
    p1, p2 = o1 + s * d1, o2 + t * d2
    return 0.5 * (p1 + p2), float(np.linalg.norm(p1 - p2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("frame", type=int)
    ap.add_argument("which", choices=("up", "down"))
    ap.add_argument("--thr", type=float, default=150)
    ap.add_argument("--win", type=int, default=450)
    ap.add_argument("--out")
    ap.add_argument("--burst", type=int, default=0, help="the K-th `bow slog` burst (1..) instead of the nearest frame")
    ap.add_argument("--pick", nargs=2, metavar="X,Y", help="build 33b: the two eyes' pixels of the point to take (e.g. "
                    "where the game's own string meets the limb, read off crops of the eyes) instead of the brightest tip")
    a = ap.parse_args()
    log = os.path.join(a.run, "xr-layer.log")
    fr = xc.parse(log).get(a.frame)
    st = xc.read_pnm(f"{log}.stereo{a.frame}.ppm")
    sl = slog_at(a.run, a.frame, a.burst)
    if fr is None or st is None or len(fr["eyes"]) != 2 or sl is None:
        sys.exit(f"frame {a.frame}: no eyes, no stereo picture or no `BOW slog` line")
    sf, lo, hi, ay, az, stips = sl
    end, other = (hi, lo) if a.which == "up" else (lo, hi)
    axis = end - other
    axis /= np.linalg.norm(axis)
    rays, marks = [], []
    for ei in (0, 1):
        e = fr["eyes"][ei]
        x0, y0, w, h = e["rect"]
        img = st[y0:y0 + h, x0:x0 + w].astype(np.float32)
        lum = img.mean(axis=2)
        pe, po = project(e, w, h, end), project(e, w, h, end + 0.1 * axis)
        if pe is None or po is None:
            sys.exit(f"eye {ei}: the string's end is behind the eye")
        ax2 = np.array([po[0] - pe[0], po[1] - pe[1]])
        ax2 /= max(np.linalg.norm(ax2), 1e-6)
        ys, xs = np.nonzero(lum > a.thr)
        keep = (np.abs(xs - pe[0]) < a.win) & (np.abs(ys - pe[1]) < a.win)
        xs, ys = xs[keep], ys[keep]
        if not len(xs):
            sys.exit(f"eye {ei}: no pixel over {a.thr} within {a.win} px of the string's end")
        k = int(np.argmax((xs - pe[0]) * ax2[0] + (ys - pe[1]) * ax2[1]))
        tip = (float(xs[k]), float(ys[k]))
        if a.pick:
            tip = tuple(float(v) for v in a.pick[ei].split(","))
        rays.append(ray(e, w, h, *tip))
        marks.append((pe, tip, x0, y0))
        print(f"eye {ei}: the end at {pe[0]:.0f},{pe[1]:.0f} px, the tip at {tip[0]:.0f},{tip[1]:.0f} px "
              f"({len(xs)} bright pixels in the window)")
    tip3, miss = closest(*rays[0], *rays[1])
    off = tip3 - end
    across = np.cross(ay, az)
    dy, dz, dx = 100 * off @ ay, 100 * off @ az, 100 * off @ across
    bone = BONE[a.which]
    dk = dy / bone[1]
    db = dk * bone[2] - dz
    k0, b0 = (stips[0], stips[1]) if a.which == "up" else (stips[2], stips[3])
    print(f"frame {a.frame} (slog frame {sf}), {a.which}: the tip {tip3.round(4)} (the rays {100 * miss:.1f} cm apart), "
          f"the string's end {end.round(4)}: the end -> the tip {100 * np.linalg.norm(off):.1f} cm = {dy:+.1f} along the "
          f"limbs, {dz:+.1f} along the shot, {dx:+.1f} across")
    # (across = the limbs x the shot in XR, mirrored from the game's space: the model's -X - SOMVR_BOW_STRING_SIDE_CM
    # moves the ends along the model's +X, so it takes -across; build 33b's first try had the sign the wrong way)
    print(f"  stips{'' if a.which == 'up' else 'lo'} {k0:.1f} {b0:.1f} -> {k0 + dk:.1f} {b0 + db:.1f} "
          f"(K {dk:+.1f}, B {db:+.1f}); SOMVR_BOW_STRING_SIDE_CM {-dx:+.1f} more (across {dx:+.1f} cm)")
    if a.out:
        from PIL import Image, ImageDraw
        im = Image.fromarray(st)
        dr = ImageDraw.Draw(im)
        for pe, tip, x0, y0 in marks:
            for (cx, cy), col in ((pe, (255, 40, 40)), (tip, (40, 255, 40))):
                dr.ellipse([x0 + cx - 9, y0 + cy - 9, x0 + cx + 9, y0 + cy + 9], outline=col, width=3)
        im.resize((im.width // 2, im.height // 2)).save(a.out)


if __name__ == "__main__":
    main()
