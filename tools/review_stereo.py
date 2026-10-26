#!/usr/bin/env python3
"""review_stereo.py - review Shadow of Mordor VR stereo frames "as a player", offline (no game, no VR app).

Inputs
  <SOMVR_XR_LOG>.stereoN.ppm  the vklayer's SOMVR_XR_DUMP picture: P6, both eyes side by side (2W x H), the sRGB bytes
                              as submitted. A PPM is split into eye L (left half) and eye R (right half).
  X.png --split               any side-by-side picture, e.g. a gamescope shot of a two-view desktop frame
  X.png [--mono]              a single view (the desktop shots): luma and clipping only. A non-PPM input without
                              --split is read as mono.

Usage
  python3 -I tools/review_stereo.py -o OUT [options] INPUT...
    python3 -I tools/review_stereo.py -o $RUN/review $RUN/xr-layer.log.stereo{60,300,700}.ppm
    python3 -I tools/review_stereo.py -o $TMPDIR/shots --mono logs/boot-check-20261019-211128/shots/*.png
  Then open OUT/index.html (self-contained, relative links: the folder can be moved). Exit 0 = no flags,
  1 = at least one flag, 2 = an input could not be read (the others are still reviewed).

Options
  -o/--out DIR      output folder (made if missing), one subfolder per input
  --split, --mono   how to read the inputs (default: PPM = split, anything else = mono)
  --ipd-cm X        eye separation in cm
  --fov-deg F | L,R | LL,LR,RL,RR
                    horizontal fov in degrees: the symmetric total, or eye L's angleLeft,angleRight (eye R
                    mirrored), or both eyes' angleLeft,angleRight. Gives d_inf (below) and, with --ipd-cm, the depth
                    check. For a dump both default to the layer log next to it (<SOMVR_XR_LOG> = the PPM's name
                    without .stereoN.ppm, its last "xr: stereo frame" line: views = the eye positions in m, fov in
                    radians). --xr-log PATH reads another log, --xr-log none none at all. Write a negative first
                    angle as --fov-deg=-52,44.
  --min-depth-cm X  the nearest believable distance for TOO_NEAR (default 15)
  --max-disp PX     disparity search range beyond d_inf, both signs (default eye width / 4)
  --vdisp-px X      vertical disparity limit (default 1.0)
  --no-images       numbers only (report.json/report.txt, index.html without pictures)

Per input, OUT/<name>/:
  stereo: left.png, right.png (full size), preview.png (half size, L|R = the parallel pair), cross.png (half size,
          R|L = the cross-eye pair), anaglyph.png (red = eye L luma, green/blue = eye R; red/cyan glasses),
          blocks.png (eye L, half size, the matched blocks: green = nearer than infinity, yellow = at infinity,
          red = beyond infinity; the number = d - d_inf in px; a magenta line = eye L's flagged seam), report.json
  mono:   view.png (full size), preview.png (half size), report.json
OUT/index.html, OUT/report.json (all frames), OUT/report.txt (the human lines, also printed).

Checks. Luma = 0.299 R + 0.587 G + 0.114 B on the bytes as they are (sRGB, 0-255, as PIL's "L"). Flags:
  LUMA_SPLIT      mean luma L/R differ by > 3 % and >= 1 level (re-stereo.md 9.5 test 1). Over the overlap (the
                  columns both eyes see at infinity) when the fov is known, else over the whole eyes.
  BLOWN_OUT_L/R   > 25 % of an eye clipped white (luma >= 250)      } run 2's 250/27 sbs picture
  DARK_L/R        > 50 % of an eye black (luma <= 5)                }
  CLIP_SPLIT      the eyes' white shares differ by > 5 points, or the black shares by > 10 points
  SEAM_HARD_L/R   a column boundary with a median step >= 8 levels where >= 90 % of the row bands (H/64 rows
                  averaged, so texture noise cancels) agree (same sign, >= half the median). A twin edge in the other
                  eye at a disparity nearer than infinity makes it a scene edge (a pillar seen level): a note instead.
  SEAM_SOFT_L/R   a gradient band: a median change >= 24 levels over 32 px (8 px means) where >= 90 % of the row
                  bands agree, with no hard step in it (run 2's ~100 px band); the width from the 8 px slopes
  IDENTICAL_EYES  the eyes are byte-identical; NEAR_IDENTICAL: mean |diff| < 0.5 and < 0.1 % of the pixels differ by
                  more than 8 levels (the stereo did not happen)
  EYES_SWAPPED    disparity d = x_L - x_R of a matched block. A near object sits further left in eye R than in eye
                  L, so d > d_inf for anything nearer than infinity. Flag: > 70 % of the blocks with depth
                  (|d - d_inf| > 1 px) have d < d_inf.
  DIVERGENT       > 15 % of all matched blocks beyond infinity (d < d_inf - 1 px) without that majority
  FLAT            no parallax: 90 % of the matched blocks within 1 px of each other and the median within 1 px of
                  d_inf (eye offset 0, or nothing near)
  TOO_NEAR        (ipd and fov known) the 95th percentile disparity implies a distance < --min-depth-cm: the eye
                  separation or the projection scale is wrong (or something really is that close: look)
  VERTICAL        vertical disparity dy (y_R - y_L): |median| > --vdisp-px (an eye shifted), or dy changes from the
                  centre to the edge by > 2x that across x (a rolled eye) or across y (a vertical scale/fov mismatch);
                  the changes come from the medians of the outer thirds, so single mismatches do not count
  d_inf = the disparity of infinity: 0 for a symmetric fov, s*tan(R.angleLeft) - s*tan(L.angleLeft) in general
  (parallel views; s = eye width / (tan(angleRight) - tan(angleLeft)) px per tan unit). Depth Z = s * ipd / (d - d_inf).
Block matching: 7 rows (10..90 % of the height) of B x B blocks of eye L (B = eye width / 40, 16..64 px; luma std
>= 4), normalized cross-correlation in eye R over +-(|d_inf| + max-disp) horizontally and +- 32 px vertically (FFT),
subpixel by a parabola; a block counts at NCC >= 0.6 with a unique peak (no other peak within 0.03 outside B/4 px)
not on the search border, when matching its eye-R twin back into eye L lands within 1 px of it (left-right
consistency: occluded or repeated content drops out). Fewer than 8 blocks: no disparity flags (a note says why).
"""
import argparse
import html
import json
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

LUMA_W = np.array([0.299, 0.587, 0.114], np.float32)
WHITE, BLACK = 250, 5
LUMA_TOL, LUMA_MIN_LEVELS = 0.03, 1.0
BLOWN_FRAC, DARK_FRAC = 0.25, 0.50
CLIP_SPLIT_WHITE, CLIP_SPLIT_BLACK = 0.05, 0.10
SEAM_AGREE, SEAM_HARD, SEAM_BAND_K, SEAM_BAND, SEAM_EDGE = 0.90, 8.0, 32, 24.0, 2
NEAR_MEAN, NEAR_LEVEL, NEAR_FRAC = 0.5, 8, 0.001
N_ROWS, MIN_STD, MIN_NCC, UNIQ, MAX_V, MIN_BLOCKS = 7, 4.0, 0.6, 0.03, 32, 8
DISP_TOL, SWAP_SHARE, DIVERGE_SHARE = 1.0, 0.70, 0.15
STEREO_LINE = re.compile(
    r"xr: stereo frame (\d+).*?views ([-+]?[\d.]+),([-+]?[\d.]+),([-+]?[\d.]+) / ([-+]?[\d.]+),([-+]?[\d.]+),"
    r"([-+]?[\d.]+) fov ([-+]?[\d.]+)\.\.([-+]?[\d.]+) / ([-+]?[\d.]+)\.\.([-+]?[\d.]+)")
DUMP_NAME = re.compile(r"^(.*)\.stereo\d+\.ppm$")


class InputError(Exception):
    pass


def r2(x, n=2):
    return None if x is None else round(float(x), n)


def pct(x):
    return "%.1f%%" % (100 * x)


# ---- reading ------------------------------------------------------------------------------------------------------
def load(path, how):
    """-> (format, eye L or the view (H x W x 3 uint8), eye R or None, share of not-opaque alpha or None)"""
    try:
        im = Image.open(path)
        im.load()
    except (OSError, ValueError, SyntaxError) as e:
        raise InputError("cannot read: %s" % e)
    fmt = im.format or "?"
    alpha = None
    if "A" in im.getbands():
        alpha = float((np.asarray(im.getchannel("A")) < 255).mean())
    rgb = np.asarray(im.convert("RGB"))
    if not (how == "split" or (how == "auto" and fmt == "PPM")):
        return fmt, rgb, None, alpha
    h, w, _ = rgb.shape
    if w % 2:
        raise InputError("%dx%d: an odd width cannot be two eyes side by side (--mono for a single view)" % (w, h))
    return fmt, rgb[:, : w // 2], rgb[:, w // 2:], alpha


def read_xr_log(path):
    """the last 'xr: stereo frame' line of a layer log -> {log, frame, ipd_cm, fov_rad (4)}, or None"""
    last = None
    try:
        with open(path, errors="replace") as f:
            for line in f:
                if "xr: stereo frame" in line:
                    m = STEREO_LINE.search(line)
                    if m:
                        last = m
    except OSError:
        return None
    if not last:
        return None
    v = [float(x) for x in last.groups()[1:]]
    ipd_m = math.dist(v[0:3], v[3:6])
    return {"log": path, "frame": int(last.group(1)), "ipd_cm": ipd_m * 100, "fov_rad": v[6:10]}


def parse_fov(s):
    v = [float(x) for x in s.split(",")]
    if len(v) == 1:
        v = [-v[0] / 2, v[0] / 2, -v[0] / 2, v[0] / 2]
    elif len(v) == 2:
        v = [v[0], v[1], -v[1], -v[0]]
    elif len(v) != 4:
        raise argparse.ArgumentTypeError("--fov-deg takes 1, 2 or 4 numbers")
    if not (-90 < v[0] < v[1] < 90 and -90 < v[2] < v[3] < 90):
        raise argparse.ArgumentTypeError("--fov-deg: angleLeft < angleRight, both within +-90")
    return [math.radians(x) for x in v]


# ---- numbers ------------------------------------------------------------------------------------------------------
def luma(rgb):
    return rgb.astype(np.float32) @ LUMA_W


def clip_stats(y):
    return {"mean": r2(y.mean()), "std": r2(y.std()), "white": r2((y >= WHITE).mean(), 4),
            "black": r2((y <= BLACK).mean(), 4)}


def _agree(s):
    """s: steps (row bands x columns) -> (the median step per column, the share of the bands that agree with it:
    the same sign and at least half its size)"""
    med = np.median(s, axis=0)
    return med, ((s * np.sign(med) >= 0.5 * np.abs(med)) & (med != 0)).mean(axis=0)


def seam_check(y):
    """vertical seams in one eye. The rows are averaged in bands (H/64 rows) so texture noise cancels while a
    seam, being in every row, stays; a step counts where >= SEAM_AGREE of the bands agree with the median."""
    h, w = y.shape
    k = SEAM_BAND_K
    if w < k + 2 * SEAM_EDGE + 24 or h < 16:
        return {"skipped": "eye too small"}
    bh = max(4, h // 64)
    nb = h // bh
    rb = y[: nb * bh].reshape(nb, bh, w).mean(axis=1, dtype=np.float64)
    med1, cov1 = _agree(rb[:, 1:] - rb[:, :-1])  # index x: the boundary between columns x and x + 1
    hard = np.where(cov1 >= SEAM_AGREE, med1, 0.0)
    c = np.cumsum(np.pad(rb, ((0, 0), (1, 0))), axis=1)
    m8 = (c[:, 8:] - c[:, :-8]) / 8  # index i: the mean of columns i .. i + 7
    medk, covk = _agree(m8[:, k:] - m8[:, :-k])  # index i: the change over k px, centres i + 3.5 -> i + 3.5 + k
    band = np.where(covk >= SEAM_AGREE, medk, 0.0)
    for a in (hard, band):
        a[:SEAM_EDGE] = 0
        a[-SEAM_EDGE:] = 0
    hx, bx = int(np.abs(hard).argmax()), int(np.abs(band).argmax())
    hard_in = float(np.abs(med1[bx + 4: bx + 4 + k]).max())
    slope = np.median(m8[:, 8:] - m8[:, :-8], axis=0) / 8 * (np.sign(band[bx]) or 1.0)  # index i: at column i + 7.5
    i0, i1 = max(0, bx - 3), max(1, min(len(slope), bx + k - 3))
    pk = i0 + int(slope[i0:i1].argmax())
    thr = max(0.1, 0.25 * slope[pk])
    a = b = pk
    while a > 0 and slope[a - 1] >= thr:
        a -= 1
    while b + 1 < len(slope) and slope[b + 1] >= thr:
        b += 1
    out = {"hard_step": r2(hard[hx], 1), "hard_x": hx + 1, "band_step": r2(band[bx], 1), "band_x": bx + 4,
           "band_width_px": b - a + 1, "hard_in_band": r2(hard_in, 1), "kind": None, "_steps": (med1, cov1)}
    if abs(hard[hx]) >= SEAM_HARD:
        out["kind"] = "hard"
    if abs(band[bx]) >= SEAM_BAND and hard_in < SEAM_HARD:
        out["kind"] = "hard+soft" if out["kind"] else "soft"
    return out


def seam_twins(seam, d_inf, max_disp):
    """A hard seam with a twin in the other eye - a step of the same sign, at least half the size, in >= 75 % of the
    bands, at a disparity nearer than infinity (d_inf + 1 < d <= max) - is a straight full-height edge of the scene
    (a pillar seen level), not a seam: kind 'hard' becomes 'edge' (a note, no flag)."""
    for e, o, sign in (("L", "R", -1), ("R", "L", +1)):
        s = seam[e]
        if not s.get("kind") or "hard" not in s["kind"] or "_steps" not in seam[o]:
            continue
        med, cov = seam[o]["_steps"]
        x, st = s["hard_x"] - 1, s["hard_step"]
        # eye L's edge at x sits at x - d in eye R; eye R's edge at x sits at x + d in eye L
        js = [x + sign * d for d in range(int(math.floor(d_inf)) + 2, int(math.ceil(d_inf)) + max_disp + 1)]
        js = [j for j in js if 0 <= j < len(med)]
        ok = [j for j in js if np.sign(med[j]) == np.sign(st) and abs(med[j]) >= 0.5 * abs(st) and cov[j] >= 0.75]
        if ok:
            j = max(ok, key=lambda j: abs(med[j]))
            s.update({"kind": s["kind"].replace("hard", "edge"), "twin_x": j + 1, "twin_d": sign * (j - x)})


def eyes_diff(L, R):
    if np.array_equal(L, R):
        return {"identical": True, "near_identical": True, "mean_abs": 0.0, "frac_gt8": 0.0}
    d = np.abs(L.astype(np.int16) - R.astype(np.int16))
    mean, frac = float(d.mean()), float((d.max(axis=2) > NEAR_LEVEL).mean())
    return {"identical": False, "near_identical": mean < NEAR_MEAN and frac < NEAR_FRAC, "mean_abs": r2(mean, 3),
            "frac_gt8": r2(frac, 5)}


def _box(a, bh, bw):
    c = np.zeros((a.shape[0] + 1, a.shape[1] + 1))
    c[1:, 1:] = a.cumsum(0).cumsum(1)
    return c[bh:, bw:] - c[:-bh, bw:] - c[bh:, :-bw] + c[:-bh, :-bw]


def ncc_map(t, win):
    """normalized cross-correlation of template t at every position inside win (valid positions only)"""
    th, tw = t.shape
    H, W = win.shape
    t = t - t.mean()
    tn = math.sqrt(float((t * t).sum()))
    corr = np.fft.irfft2(np.fft.rfft2(win) * np.conj(np.fft.rfft2(t, s=(H, W))), s=(H, W))[: H - th + 1, : W - tw + 1]
    n = th * tw
    s1, s2 = _box(win, th, tw), _box(win * win, th, tw)
    var = s2 - s1 * s1 / n
    ok = var > n * 0.25  # a patch std > 0.5 level
    return np.where(ok, corr / (tn * np.sqrt(np.where(ok, var, 1.0))), 0.0)


def _parab(a, b, c):
    den = a - 2 * b + c
    return 0.0 if den >= 0 else float(np.clip(0.5 * (a - c) / den, -0.5, 0.5))


def _search(t, img, cx, cy, max_disp):
    """find template t in img around (cx, cy): +-max_disp in x, +-MAX_V in y.
    -> (None, x, y, sub-px x, sub-px y, ncc) or (reject reason, ...)"""
    H, W = img.shape
    B = t.shape[0]
    if t.std() < MIN_STD / 2:  # the eye-R twin of a textured block is flat: no match
        return ("weak",)
    wx0, wx1 = max(0, cx - max_disp), min(W, cx + max_disp + B)
    wy0, wy1 = max(0, cy - MAX_V), min(H, cy + MAX_V + B)
    if wx1 - wx0 < B + 2 or wy1 - wy0 < B + 2:
        return ("edge",)
    m = ncc_map(t, img[wy0:wy1, wx0:wx1])
    iy, ix = np.unravel_index(int(m.argmax()), m.shape)
    best = float(m[iy, ix])
    if best < MIN_NCC:
        return ("weak",)
    if iy in (0, m.shape[0] - 1) or ix in (0, m.shape[1] - 1):
        return ("edge",)
    rad = max(2, B // 4)
    m2 = m.copy()
    m2[max(0, iy - rad): iy + rad + 1, max(0, ix - rad): ix + rad + 1] = -1
    if m2.max() > best - UNIQ:
        return ("ambiguous",)
    return (None, wx0 + ix, wy0 + iy, _parab(m[iy, ix - 1], best, m[iy, ix + 1]),
            _parab(m[iy - 1, ix], best, m[iy + 1, ix]), best)


def match_blocks(yL, yR, max_disp):
    """block matching eye L -> eye R and back (left-right consistency: occluded or repeated content drops out),
    d within +-max_disp. -> (blocks [(x, y, B, d, dy, ncc)], rejects, B)"""
    H, W = yL.shape
    B = int(np.clip(W // 40, 16, 64))
    yL, yR = yL.astype(np.float64), yR.astype(np.float64)
    blocks, rej = [], {"flat": 0, "weak": 0, "edge": 0, "ambiguous": 0, "inconsistent": 0}
    for fy in np.linspace(0.10, 0.90, N_ROWS):
        y0 = int(round(fy * H - B / 2))
        if y0 < 0 or y0 + B > H:
            continue
        for x0 in range(B // 2, W - B - B // 2 + 1, B):
            t = yL[y0: y0 + B, x0: x0 + B]
            if t.std() < MIN_STD:
                rej["flat"] += 1
                continue
            f = _search(t, yR, x0, y0, max_disp)
            if f[0]:
                rej[f[0]] += 1
                continue
            _, xr, yr, sx, sy, best = f
            b = _search(yR[yr: yr + B, xr: xr + B], yL, xr, yr, max_disp)
            if b[0] or abs(b[1] - x0) > 1 or abs(b[2] - y0) > 1:
                rej["inconsistent"] += 1
                continue
            blocks.append((x0, y0, B, x0 - (xr + sx), (yr + sy) - y0, best))
    return blocks, rej, B


def _edge_change(pos, dy, extent):
    """the change of dy from the centre to the edge of the eye (px), from the medians of the outer thirds (robust to
    single mismatches): along x = a rolled eye, along y = a vertical scale (fov) mismatch. None: too few blocks."""
    lo, hi = pos < extent / 3, pos > 2 * extent / 3
    if lo.sum() < 3 or hi.sum() < 3:
        return None
    span = float(np.median(pos[hi]) - np.median(pos[lo]))
    return float(np.median(dy[hi]) - np.median(dy[lo])) / span * extent / 2 if span > 0 else None


def geometry(fov_rad, ipd_cm, W):
    if fov_rad is None:
        return None
    tlL, trL, tlR, trR = (math.tan(a) for a in fov_rad)
    sL, sR = W / (trL - tlL), W / (trR - tlR)
    s = (sL + sR) / 2
    g = {"fov_deg": [r2(math.degrees(a), 2) for a in fov_rad], "px_per_tan": r2(s, 1),
         "d_inf_px": r2(sR * tlR - sL * tlL, 2), "ipd_cm": r2(ipd_cm, 3)}
    if ipd_cm:
        g["expected_px"] = {"%gcm" % z: r2(s * ipd_cm / z + g["d_inf_px"], 1) for z in (50, 100, 300, 1000)}
    return g


def search_px(args, W, d_inf):
    """the disparity search half-range: --max-disp (default W/4) beyond d_inf on either side (eyes swapped: -d_inf)"""
    return (args.max_disp or max(16, W // 4)) + int(math.ceil(abs(d_inf)))


def disparity(yL, yR, geo, args):
    W = yL.shape[1]
    d_inf = geo["d_inf_px"] if geo else 0.0
    max_disp = search_px(args, W, d_inf)
    blocks, rej, B = match_blocks(yL, yR, max_disp)
    out = {"block_px": B, "search_px": max_disp, "d_inf_px": d_inf, "matched": len(blocks), "rejected": rej,
           "blocks": [[x, y, r2(d, 2), r2(dy, 2), r2(n, 3)] for x, y, _b, d, dy, n in blocks]}
    if len(blocks) < MIN_BLOCKS:
        out["note"] = "only %d textured blocks matched (< %d): no disparity checks" % (len(blocks), MIN_BLOCKS)
        return out
    a = np.array([(b[0] + B / 2, b[1] + B / 2, b[3], b[4]) for b in blocks])
    x, y, d, dy = a[:, 0], a[:, 1], a[:, 2], a[:, 3]
    rel = d - d_inf
    q = np.percentile(d, [5, 10, 50, 90, 95])
    pos, neg = int((rel > DISP_TOL).sum()), int((rel < -DISP_TOL).sum())
    out.update({"d_p5": r2(q[0]), "d_p10": r2(q[1]), "d_median": r2(q[2]), "d_p90": r2(q[3]), "d_p95": r2(q[4]),
                "d_min": r2(d.min()), "d_max": r2(d.max()), "nearer": pos, "beyond": neg,
                "at_infinity": len(blocks) - pos - neg, "dy_median": r2(np.median(dy)),
                "dy_p90_abs": r2(np.percentile(np.abs(dy), 90)), "dy_roll_edge_px": r2(_edge_change(x, dy, W)),
                "dy_scale_edge_px": r2(_edge_change(y, dy, yL.shape[0]))})
    if geo and geo.get("ipd_cm"):
        s, ipd = geo["px_per_tan"], geo["ipd_cm"]
        near, med = q[4] - d_inf, q[2] - d_inf
        out["depth_near_cm"] = r2(s * ipd / near, 1) if near > DISP_TOL else None
        out["depth_median_cm"] = r2(s * ipd / med, 1) if med > DISP_TOL else None
    return out


# ---- flags --------------------------------------------------------------------------------------------------------
def luma_flags(lum, flags):
    for e in ("L", "R"):
        st = lum[e]
        if st["white"] > BLOWN_FRAC:
            flags.append(("BLOWN_OUT_" + e, "eye %s blown out: %s clipped white (mean %.1f)" % (e, pct(st["white"]),
                                                                                             st["mean"])))
        if st["black"] > DARK_FRAC:
            flags.append(("DARK_" + e, "eye %s dark: %s black (mean %.1f)" % (e, pct(st["black"]), st["mean"])))
    ov = lum.get("overlap")
    a, b = (ov["L_mean"], ov["R_mean"]) if ov else (lum["L"]["mean"], lum["R"]["mean"])
    if abs(a - b) >= LUMA_MIN_LEVELS and (b == 0 or abs(a / b - 1) > LUMA_TOL):
        where = "over the overlap" if ov else "whole eyes"
        flags.append(("LUMA_SPLIT", "mean luma L %.1f / R %.1f (%s): %s apart, limit 3 %%" % (
            a, b, where, "inf" if b == 0 else "%+.1f %%" % (100 * (a / b - 1)))))
    dw, db = abs(lum["L"]["white"] - lum["R"]["white"]), abs(lum["L"]["black"] - lum["R"]["black"])
    if dw > CLIP_SPLIT_WHITE or db > CLIP_SPLIT_BLACK:
        flags.append(("CLIP_SPLIT", "clipping differs: white %s / %s, black %s / %s" % (
            pct(lum["L"]["white"]), pct(lum["R"]["white"]), pct(lum["L"]["black"]), pct(lum["R"]["black"]))))


def stereo_flags(fr, args, flags):
    for e in ("L", "R"):
        s = fr["seam"][e]
        if s.get("kind") and "hard" in s["kind"]:
            flags.append(("SEAM_HARD_" + e, "eye %s: hard vertical seam at x %d (%+.1f levels, 90 %% of the row bands "
                          "agree)" % (e, s["hard_x"], s["hard_step"])))
        if s.get("kind") and "soft" in s["kind"]:
            flags.append(("SEAM_SOFT_" + e, "eye %s: gradient band at x %d..%d (%+.1f levels over %d px, ~%d px wide)"
                          % (e, s["band_x"], s["band_x"] + SEAM_BAND_K, s["band_step"], SEAM_BAND_K,
                             s["band_width_px"])))
    ed = fr["eyes_diff"]
    if ed["identical"]:
        flags.append(("IDENTICAL_EYES", "both eyes byte-identical: the stereo did not happen"))
        return
    if ed["near_identical"]:
        flags.append(("NEAR_IDENTICAL", "eyes nearly identical (mean |diff| %.3f, %s of the pixels > 8 levels): the "
                      "stereo did not happen?" % (ed["mean_abs"], pct(ed["frac_gt8"]))))
    dp = fr["disparity"]
    if "d_median" not in dp:
        return
    signed = dp["nearer"] + dp["beyond"]
    if signed and dp["beyond"] > SWAP_SHARE * signed:
        flags.append(("EYES_SWAPPED", "%d of %d blocks with depth lie beyond infinity (d < d_inf %+.1f px, median d "
                      "%+.1f): near things sit further right in eye R - eyes swapped (or the eye offsets' sign)"
                      % (dp["beyond"], signed, dp["d_inf_px"], dp["d_median"])))
    elif dp["beyond"] > DIVERGE_SHARE * dp["matched"]:
        flags.append(("DIVERGENT", "%d of %d matched blocks beyond infinity (d < d_inf %+.1f px - 1): offsets or fov "
                      "wrong" % (dp["beyond"], dp["matched"], dp["d_inf_px"])))
    if dp["d_p95"] - dp["d_p5"] <= DISP_TOL and abs(dp["d_median"] - dp["d_inf_px"]) <= DISP_TOL:
        flags.append(("FLAT", "no parallax: all matched blocks at d_inf %+.1f px (median %+.2f, p5..p95 %+.2f..%+.2f):"
                      " eye offset 0, or nothing near" % (dp["d_inf_px"], dp["d_median"], dp["d_p5"], dp["d_p95"])))
    near = dp.get("depth_near_cm")
    if near is not None and near < args.min_depth_cm:
        flags.append(("TOO_NEAR", "the nearest blocks (p95 d %+.1f px) imply %.1f cm < %g cm with IPD %.2f cm: "
                      "separation or scale wrong?" % (dp["d_p95"], near, args.min_depth_cm, fr["geometry"]["ipd_cm"])))
    v, why = args.vdisp_px, []
    if abs(dp["dy_median"]) > v:
        why.append("eye R sits %.2f px %s (median dy, limit %g)" % (abs(dp["dy_median"]),
                                                                   "lower" if dp["dy_median"] > 0 else "higher", v))
    for k, what in (("dy_roll_edge_px", "across x: a rolled eye"),
                    ("dy_scale_edge_px", "across y: vertical scale/fov")):
        if dp[k] is not None and abs(dp[k]) > 2 * v:
            why.append("dy changes by %+.2f px centre to edge %s (limit %g)" % (dp[k], what, 2 * v))
    if why:
        flags.append(("VERTICAL", "vertical disparity: %s; an eye misaligned" % "; ".join(why)))


# ---- pictures -----------------------------------------------------------------------------------------------------
def save(arr, path):
    Image.fromarray(arr).save(path, compress_level=3)


def half(im):
    return im.resize((max(1, im.width // 2), max(1, im.height // 2)), Image.Resampling.BOX)


def write_stereo_images(d, L, R, fr):
    iL, iR = Image.fromarray(L), Image.fromarray(R)
    save(L, os.path.join(d, "left.png"))
    save(R, os.path.join(d, "right.png"))
    hL, hR = half(iL), half(iR)
    for name, a, b in (("preview.png", hL, hR), ("cross.png", hR, hL)):
        pair = Image.new("RGB", (a.width * 2, a.height))
        pair.paste(a, (0, 0))
        pair.paste(b, (a.width, 0))
        pair.save(os.path.join(d, name), compress_level=3)
    ana = R.copy()
    ana[..., 0] = np.clip(luma(L) + 0.5, 0, 255).astype(np.uint8)
    save(ana, os.path.join(d, "anaglyph.png"))
    ov = hL.convert("RGB")
    dr = ImageDraw.Draw(ov)
    dp = fr["disparity"]
    d_inf = dp.get("d_inf_px", 0.0)
    for x, y, dd, _dy, _n in dp.get("blocks", []):
        rel = dd - d_inf
        col = (0, 230, 0) if rel > DISP_TOL else (255, 40, 40) if rel < -DISP_TOL else (255, 220, 0)
        b = dp["block_px"] // 2
        dr.rectangle([x // 2, y // 2, x // 2 + b - 1, y // 2 + b - 1], outline=col)
        if b >= 16:
            dr.text((x // 2 + 2, y // 2 + 2), "%.0f" % rel, fill=col)
    k = fr["seam"]["L"].get("kind") or ""
    if "hard" in k or "soft" in k:
        s = fr["seam"]["L"]
        sx = s["hard_x"] if "hard" in k else s["band_x"] + SEAM_BAND_K // 2
        dr.line([sx // 2, 0, sx // 2, ov.height], fill=(255, 0, 255), width=2)
    ov.save(os.path.join(d, "blocks.png"), compress_level=3)
    return ["preview.png", "cross.png", "anaglyph.png", "blocks.png", "left.png", "right.png"]


def write_mono_images(d, V):
    save(V, os.path.join(d, "view.png"))
    half(Image.fromarray(V)).save(os.path.join(d, "preview.png"), compress_level=3)
    return ["preview.png", "view.png"]


# ---- one frame ----------------------------------------------------------------------------------------------------
def review(path, name, outdir, args):
    fr = {"input": os.path.abspath(path), "name": name}
    try:
        fmt, L, R, alpha = load(path, args.how)
    except InputError as e:
        fr["error"] = str(e)
        fr["flags"] = []
        fr["line"] = "%s: ERROR %s" % (name, e)
        return fr
    fr.update({"format": fmt, "kind": "mono" if R is None else "stereo", "eye_size": [L.shape[1], L.shape[0]],
               "alpha_not_opaque": r2(alpha, 4) if alpha else None})
    flags, notes = [], []
    if alpha:
        notes.append("alpha below 255 on %s of the pixels (ignored)" % pct(alpha))
    d = os.path.join(outdir, name)
    if not args.no_images:
        os.makedirs(d, exist_ok=True)
    yL = luma(L)
    if R is None:
        st = clip_stats(yL)
        fr["luma"] = {"view": st}
        if st["white"] > BLOWN_FRAC:
            flags.append(("BLOWN_OUT", "blown out: %s clipped white (mean %.1f)" % (pct(st["white"]), st["mean"])))
        if st["black"] > DARK_FRAC:
            flags.append(("DARK", "dark: %s black (mean %.1f)" % (pct(st["black"]), st["mean"])))
        if args.how == "auto":
            notes.append("read as a single view (--split for side by side)")
        fr["images"] = [] if args.no_images else write_mono_images(d, L)
        line = "luma %.1f (std %.1f) | white %s black %s | mono %dx%d" % (
            st["mean"], st["std"], pct(st["white"]), pct(st["black"]), L.shape[1], L.shape[0])
    else:
        yR = luma(R)
        W = L.shape[1]
        xr = None
        if args.xr_log != "none":
            lp = args.xr_log
            if lp is None:
                m = DUMP_NAME.match(path)
                lp = m.group(1) if m and os.path.isfile(m.group(1)) else None
            xr = read_xr_log(lp) if lp else None
            if args.xr_log and not xr:
                notes.append("no 'xr: stereo frame' line in %s" % args.xr_log)
        fov = args.fov or (xr["fov_rad"] if xr else None)
        ipd = args.ipd_cm or (xr["ipd_cm"] if xr else None)
        if xr:
            notes.append("from %s (stereo frame %d): %s" % (os.path.basename(xr["log"]), xr["frame"], ", ".join(
                k for k, v in (("ipd", not args.ipd_cm), ("fov", not args.fov)) if v) or "nothing (options given)"))
        geo = geometry(fov, ipd, W)
        fr["geometry"] = geo
        if fov is None:
            notes.append("fov unknown: d_inf taken as 0 (symmetric), no depth check")
        elif not ipd:
            notes.append("ipd unknown: no depth check")
        lum = {"L": clip_stats(yL), "R": clip_stats(yR)}
        lum["ratio"] = r2(lum["L"]["mean"] / lum["R"]["mean"], 4) if lum["R"]["mean"] else None
        di = int(round(geo["d_inf_px"])) if geo else 0
        if di and abs(di) < W // 2:
            oL, oR = (yL[:, di:], yR[:, : W - di]) if di > 0 else (yL[:, : W + di], yR[:, -di:])
            lum["overlap"] = {"cols": W - abs(di), "L_mean": r2(oL.mean()), "R_mean": r2(oR.mean())}
            lum["overlap"]["ratio"] = r2(oL.mean() / oR.mean(), 4) if oR.mean() else None
        fr["luma"] = lum
        fr["seam"] = {"L": seam_check(yL), "R": seam_check(yR)}
        d_inf = geo["d_inf_px"] if geo else 0.0
        seam_twins(fr["seam"], d_inf, search_px(args, W, d_inf))
        for e in ("L", "R"):
            fr["seam"][e].pop("_steps", None)
            s = fr["seam"][e]
            if s.get("kind") and "edge" in s["kind"]:
                notes.append("eye %s: a full-height edge at x %d (%+.1f levels) has a twin in eye %s at d %+d px: a "
                             "straight scene edge, not a seam (look)" % (e, s["hard_x"], s["hard_step"],
                                                                         "R" if e == "L" else "L", s["twin_d"]))
        fr["eyes_diff"] = eyes_diff(L, R)
        fr["disparity"] = {"note": "eyes identical: not matched"} if fr["eyes_diff"]["identical"] else \
            disparity(yL, yR, geo, args)
        if fr["disparity"].get("note"):
            notes.append(fr["disparity"]["note"])
        luma_flags(lum, flags)
        stereo_flags(fr, args, flags)
        fr["images"] = [] if args.no_images else write_stereo_images(d, L, R, fr)
        line = human_stereo(fr)
    fr["flags"] = [{"code": c, "msg": m} for c, m in flags]
    fr["notes"] = notes
    fr["line"] = "%s: %s | %s" % (name, ("FLAGS " + " ".join(c for c, _ in flags)) if flags else "OK", line)
    if not args.no_images:
        with open(os.path.join(d, "report.json"), "w") as f:
            json.dump(fr, f, indent=1)
    return fr


def human_stereo(fr):
    lum, dp = fr["luma"], fr["disparity"]
    ratio = lum["overlap"]["ratio"] if lum.get("overlap") else lum["ratio"]
    parts = ["luma L %.1f R %.1f (L/R %s%s)" % (lum["L"]["mean"], lum["R"]["mean"],
                                               "-" if ratio is None else "%.3f" % ratio,
                                               ", overlap" if lum.get("overlap") else ""),
             "white %s/%s black %s/%s" % (pct(lum["L"]["white"]), pct(lum["R"]["white"]), pct(lum["L"]["black"]),
                                          pct(lum["R"]["black"]))]
    sp = []
    for e in ("L", "R"):
        s = fr["seam"][e]
        sp.append("%s %s" % (e, s["kind"] or "none") if "kind" in s else "%s -" % e)
    parts.append("seam " + " ".join(sp))
    ed = fr["eyes_diff"]
    parts.append("eyes identical" if ed["identical"] else "eyes |diff| %.2f" % ed["mean_abs"])
    if "d_median" in dp:
        parts.append("disp %+.1f px median [p5 %+.1f .. p95 %+.1f] d_inf %+.1f, %d blocks (%d nearer, %d beyond), "
                     "vert %+.2f px (p90 %.2f)" % (dp["d_median"], dp["d_p5"], dp["d_p95"], dp["d_inf_px"],
                                                   dp["matched"], dp["nearer"], dp["beyond"], dp["dy_median"],
                                                   dp["dy_p90_abs"]))
        if "depth_near_cm" in dp:
            parts.append("depth near %s median %s" % tuple(
                "-" if v is None else "%.0f cm" % v for v in (dp["depth_near_cm"], dp["depth_median_cm"])))
    elif dp.get("note"):
        parts.append("disp: " + dp["note"])
    parts.append("%dx%d per eye" % tuple(fr["eye_size"]))
    return " | ".join(parts)


# ---- html ---------------------------------------------------------------------------------------------------------
CSS = """body{font-family:sans-serif;margin:1em;background:#1b1b1b;color:#ddd}a{color:#8cf}
h2{margin-top:2em;border-top:1px solid #555;padding-top:.6em}.flag{color:#ff5050;font-weight:bold}
.ok{color:#6c6}.note{color:#aaa}.line{font-family:monospace;font-size:90%;white-space:pre-wrap}
table{border-collapse:collapse;font-size:90%}td,th{border:1px solid #444;padding:2px 6px;text-align:right}
figure{display:inline-block;margin:.4em;vertical-align:top}figcaption{font-size:85%;color:#aaa}
img{max-width:100%;border:1px solid #333}.big{max-width:96vw}.mid{max-width:47vw}"""
CAPTION = {"preview.png": "parallel pair: eye L | eye R (half size)", "cross.png": "cross-eye pair: R | L (half size)",
           "anaglyph.png": "anaglyph: red = L, cyan = R", "blocks.png": "matched blocks on eye L: number = d - d_inf px"
           " (green nearer, yellow infinity, red beyond)", "view.png": "full size", "left.png": "eye L full size",
           "right.png": "eye R full size"}


def write_html(outdir, frames, params):
    e = html.escape
    o = ["<!doctype html><html><head><meta charset='utf-8'><title>review_stereo</title><style>%s</style></head><body>"
         % CSS, "<h1>review_stereo: %d frame(s)</h1><p class='note'>%s</p><table><tr><th>frame</th><th>result</th></tr>"
         % (len(frames), e(params))]
    for fr in frames:
        res = ("<span class='flag'>ERROR</span>" if "error" in fr else
               "<span class='flag'>%s</span>" % e(" ".join(f["code"] for f in fr["flags"])) if fr["flags"] else
               "<span class='ok'>OK</span>")
        o.append("<tr><td style='text-align:left'><a href='#%s'>%s</a></td><td style='text-align:left'>%s</td></tr>"
                 % (e(fr["name"]), e(fr["name"]), res))
    o.append("</table>")
    for fr in frames:
        n = fr["name"]
        o.append("<h2 id='%s'>%s</h2><p class='note'>%s</p>" % (e(n), e(n), e(fr["input"])))
        if "error" in fr:
            o.append("<p class='flag'>ERROR: %s</p>" % e(fr["error"]))
            continue
        for f in fr["flags"]:
            o.append("<div class='flag'>%s: %s</div>" % (e(f["code"]), e(f["msg"])))
        if not fr["flags"]:
            o.append("<div class='ok'>no flags</div>")
        for nt in fr["notes"]:
            o.append("<div class='note'>%s</div>" % e(nt))
        o.append("<p class='line'>%s</p>" % e(fr["line"]))
        o.append(table(fr))
        imgs = fr["images"]
        for im in imgs:
            if im in ("left.png", "right.png", "view.png"):
                continue
            cls = "big" if im in ("preview.png", "cross.png") else "mid"
            o.append("<figure><a href='%s/%s'><img class='%s' loading='lazy' src='%s/%s'></a><figcaption>%s"
                     "</figcaption></figure>" % (e(n), im, cls, e(n), im, CAPTION[im]))
        full = [im for im in imgs if im in ("left.png", "right.png", "view.png")]
        if full:
            o.append("<p>full size: %s</p>" % " ".join("<a href='%s/%s'>%s</a>" % (e(n), im, CAPTION[im])
                                                      for im in full))
    o.append("</body></html>")
    with open(os.path.join(outdir, "index.html"), "w") as f:
        f.write("\n".join(o))


def table(fr):
    lum = fr["luma"]
    if fr["kind"] == "mono":
        v = lum["view"]
        return ("<table><tr><th></th><th>mean luma</th><th>std</th><th>white</th><th>black</th></tr><tr><td>view</td>"
                "<td>%.1f</td><td>%.1f</td><td>%s</td><td>%s</td></tr></table>"
                % (v["mean"], v["std"], pct(v["white"]), pct(v["black"])))
    rows = ["<table><tr><th></th><th>mean luma</th><th>std</th><th>white</th><th>black</th><th>hard step</th>"
            "<th>band step</th></tr>"]
    for e in ("L", "R"):
        v, s = lum[e], fr["seam"][e]
        rows.append("<tr><td>eye %s</td><td>%.1f</td><td>%.1f</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
                    % (e, v["mean"], v["std"], pct(v["white"]), pct(v["black"]),
                       "%+.1f @%d" % (s["hard_step"], s["hard_x"]) if s.get("hard_step") else "0",
                       "%+.1f @%d (~%d px)" % (s["band_step"], s["band_x"], s["band_width_px"])
                       if s.get("band_step") else "0"))
    rows.append("</table>")
    dp, g = fr["disparity"], fr.get("geometry")
    kv = [("L/R ratio", lum["ratio"])]
    if lum.get("overlap"):
        kv.append(("L/R ratio overlap (%d cols)" % lum["overlap"]["cols"], lum["overlap"]["ratio"]))
    kv += [("eyes mean |diff|", fr["eyes_diff"]["mean_abs"])]
    for k in ("d_inf_px", "matched", "d_median", "d_p5", "d_p95", "d_min", "d_max", "nearer", "at_infinity", "beyond",
              "dy_median", "dy_p90_abs", "dy_roll_edge_px", "dy_scale_edge_px", "depth_near_cm", "depth_median_cm"):
        if k in dp:
            kv.append((k, dp[k]))
    if "rejected" in dp:
        kv.append(("rejected", ", ".join("%s %d" % i for i in dp["rejected"].items())))
    if g:
        kv.append(("fov deg L / R", "%s..%s / %s..%s" % tuple(g["fov_deg"])))
        kv.append(("ipd cm", g["ipd_cm"]))
        if "expected_px" in g:
            kv.append(("expected d px", ", ".join("%s %s" % i for i in g["expected_px"].items())))
    rows.append("<table>" + "".join("<tr><th style='text-align:left'>%s</th><td>%s</td></tr>"
                                    % (html.escape(str(k)), html.escape(str(v))) for k, v in kv) + "</table>")
    return "".join(rows)


# ---- main ---------------------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="review SoM VR stereo frame dumps (see the top of this file)")
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--split", dest="how", action="store_const", const="split")
    g.add_argument("--mono", dest="how", action="store_const", const="mono")
    ap.set_defaults(how="auto")
    ap.add_argument("--ipd-cm", type=float)
    ap.add_argument("--fov-deg", dest="fov", type=parse_fov)
    ap.add_argument("--xr-log")
    ap.add_argument("--min-depth-cm", type=float, default=15.0)
    ap.add_argument("--max-disp", type=int)
    ap.add_argument("--vdisp-px", type=float, default=1.0)
    ap.add_argument("--no-images", action="store_true")
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    frames, used = [], set()
    for p in args.inputs:
        base = re.sub(r"[^\w.+-]", "_", os.path.basename(p))
        base = re.sub(r"\.(ppm|png|pnm|jpe?g|bmp|tiff?)$", "", base, flags=re.I) or "frame"
        name, i = base, 2
        while name in used:
            name, i = "%s-%d" % (base, i), i + 1
        used.add(name)
        fr = review(p, name, args.out, args)
        frames.append(fr)
        print(fr["line"], flush=True)
    params = "options: %s" % " ".join("%s=%s" % (k, v) for k, v in sorted(vars(args).items())
                                      if k not in ("inputs", "out") and v not in (None, False))
    with open(os.path.join(args.out, "report.json"), "w") as f:
        json.dump({"tool": "review_stereo.py", "params": params, "frames": frames}, f, indent=1)
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write("".join(fr["line"] + "\n" for fr in frames))
    write_html(args.out, frames, params)
    if any("error" in fr for fr in frames):
        return 2
    return 1 if any(fr["flags"] for fr in frames) else 0


if __name__ == "__main__":
    sys.exit(main())
