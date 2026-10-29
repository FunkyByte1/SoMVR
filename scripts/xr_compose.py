#!/usr/bin/env python3
"""xr_compose.py - see a dumped stereo frame as the player does: the eyes (<log>.stereoN.ppm) with the frame's quads
(and, build 10, our VR hands' projection layer from <log>.handsN.ppm + .alpha.pgm, in its slot's place)
drawn over them as the compositor would (the HUD or the menu board and the wrist minimap from <log>.hudN.ppm, the VR
menu's panel or card from <log>.vrmenuN.ppm, SOMVR_THREAT's attack glows from its texture rebuilt here, the pointer's
markers as dots, the fade - a blink's or a practice trip's black - at its level over the whole view; round 2 of
the void: SOMVR_XR_MENU_BACK's dim over the world and its dark panel behind a menu board, rebuilt here), from the layer's
`xr: dump layers frame N:` lines (eye poses and fovs, every quad's pose, size, sub-image rect, flags; build 6).

  xr_compose.py RUN_DIR_OR_XR_LAYER_LOG [-f N ...] [-o OUT_DIR] [--ppd P]

Writes OUT_DIR (default RUN/compose)/frameN.png (both eyes, half size), frameN-<quad>.png (the left eye around each
quad, full size) and prints per quad: the distance, its size in degrees, the texture's pixels per degree, the eye
image's and a Quest 2's (about 20 px/deg in the middle of the lens) pixels for it, and for the VR menu the 32-pixel
text cell / about 22-pixel capitals, for the game's UI an 18- and a 28-pixel line (1080 lines) in degrees and Quest 2
pixels. Readable on a Quest 2: capitals of about 0.6 deg (12 px) and more; comfortable from about 1 deg.
Blending: in the stored (sRGB) bytes, premultiplied unless the layer says UNPREMULTIPLIED; quads seen from both sides.
--ppd P (build 8): draw each eye at P pixels per degree in its middle (20 = a Quest 2) over the same fov instead of the
dump's size (a headless dump is 352x264 per eye: 3 px/deg): the world picture scaled up (blurred), the quads drawn at
that density, so their text looks as sharp as in the headset.
"""
import argparse
import math
import os
import re
import sys

import numpy as np
from PIL import Image

Q2_PPD = 20.0
LINE = re.compile(r"xr: dump layers frame (\d+): (.+?) pose ([-+\d.]+),([-+\d.]+),([-+\d.]+) q ([-+\d.]+),([-+\d.]+),"
                  r"([-+\d.]+),([-+\d.]+) (.*)$")
EYE = re.compile(r"fov ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) ([-+\d.]+) rect (\d+),(\d+) (\d+)x(\d+)")
QUAD = re.compile(r"size ([\d.]+) x ([\d.]+) rect (-?\d+),(-?\d+) (\d+)x(\d+)(?: flags 0x([0-9a-f]+) slot (\d+))?")
HANDS = re.compile(r"xr: dump layers frame (\d+): hands (\d+)x(\d+) per eye slot (\d+)")


def read_pnm(path):
    if not os.path.exists(path):
        return None
    return np.asarray(Image.open(path))


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def parse(log):
    frames = {}
    with open(log, errors="replace") as f:
        for ln in f:
            m = HANDS.search(ln)
            if m:  # build 10: our VR hands, a projection layer with the eyes' poses (<log>.handsN.ppm side by side)
                fr = frames.setdefault(int(m.group(1)), {"eyes": {}, "quads": []})
                fr["hands"] = {"w": int(m.group(2)), "h": int(m.group(3)), "slot": int(m.group(4))}
                continue
            m = LINE.search(ln)
            if not m:
                continue
            n, what = int(m.group(1)), m.group(2)
            p = np.array([float(m.group(i)) for i in (3, 4, 5)])
            q = np.array([float(m.group(i)) for i in (6, 7, 8, 9)])
            rest = m.group(10)
            fr = frames.setdefault(n, {"eyes": {}, "quads": []})
            if what.startswith("eye "):
                e = EYE.search(rest)
                if e:
                    fr["eyes"][int(what[4:])] = {"p": p, "q": q, "fov": [float(e.group(i)) for i in (1, 2, 3, 4)],
                                                 "rect": [int(e.group(i)) for i in (5, 6, 7, 8)]}
            else:
                e = QUAD.search(rest)
                if e:
                    fr["quads"].append({"what": what, "p": p, "q": q, "size": (float(e.group(1)), float(e.group(2))),
                                        "rect": [int(e.group(i)) for i in (3, 4, 5, 6)],
                                        "flags": int(e.group(7), 16) if e.group(7) else 0x6,
                                        "slot": int(e.group(8)) if e.group(8) else 0,
                                        "other_space": "space other" in rest})
    return frames


_TH = None


def threat_tex():
    """SOMVR_THREAT's static texture as the layer fills it (somvr_threat.c somvr_th_image): 16 cells per row of 32 px in
    1 px guards, row 0 the radial glow (1 - r^2)^2, row 1 the edge band; premultiplied warm colour, alpha 0.35 x it;
    row 2 (build 10) the edge band in the hurt red, alpha 0.75 x it; row 3 (build 17) the radial glow in an alarm red,
    alpha 0.90 x it (an unblockable blow); row 4 (build 21) the radial glow in the wraith's blue, alpha 0.30 x it (the
    grab cue)"""
    global _TH
    if _TH is None:
        rgb, a = np.zeros((170, 544, 3), np.uint8), np.zeros((170, 544), np.uint8)
        c = (np.arange(32) + 0.5) / 32
        U, V = np.meshgrid(c, c)

        def smooth(e0, e1, x):
            t = np.clip((x - e0) / (e1 - e0), 0, 1)
            return t * t * (3 - 2 * t)
        r = np.hypot(2 * U - 1, 2 * V - 1)
        edge = (1 - smooth(0, 1, U)) ** 2 * smooth(0, 0.2, V) * smooth(0, 0.2, 1 - V)
        glow = np.where(r < 1, (1 - r * r) ** 2, 0)
        prof = [glow, edge, edge, glow, glow]
        col = [(1.0, 0.55, 0.15, 0.35), (1.0, 0.55, 0.15, 0.35), (1.0, 0.04, 0.02, 0.75), (1.0, 0.06, 0.03, 0.90),
               (0.35, 0.75, 1.0, 0.30)]
        for row in (0, 1, 2, 3, 4):
            for k in range(1, 17):
                w = prof[row] * k / 16
                x, y = (k - 1) * 34 + 1, row * 34 + 1
                rgb[y:y + 32, x:x + 32] = np.rint(np.stack([w * col[row][0], w * col[row][1], w * col[row][2]], axis=-1) * 255)
                a[y:y + 32, x:x + 32] = np.rint(w * col[row][3] * 255)
        _TH = (rgb, a[..., None])
    return _TH


def back_tex(qd):
    """SOMVR_XR_MENU_BACK's panel (vklayer/somvr_void.c somvr_back_*): the cell its rect shows, premultiplied, the stored
    sRGB bytes / 255"""
    CW, CH, LEVELS, ALPHA, SOFT, RAD = 192, 120, 8, 0.9, 0.05, 0.075
    x, _, rw, rh = qd["rect"]
    k = (x - 1) // CW + 1
    W, H = qd["size"]
    u = (np.arange(1, 1 + rw) - 0.5) / (CW - 2)
    v = (np.arange(1, 1 + rh) - 0.5) / (CH - 2)
    X, Y = np.meshgrid((u - 0.5) * W, (v - 0.5) * H)
    r = min(RAD, W / 2, H / 2)
    qx, qy = np.abs(X) - (W / 2 - r), np.abs(Y) - (H / 2 - r)
    d = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
    t = np.clip(-d / SOFT, 0, 1)
    a = (ALPHA * t * t * (3 - 2 * t) * k / LEVELS).astype(np.float32)
    lin = np.array([0.010, 0.011, 0.014], np.float32) * a[..., None]
    enc = np.where(lin <= 0.0031308, 12.92 * lin, 1.055 * np.power(np.maximum(lin, 1e-9), 1 / 2.4) - 0.055)
    return np.concatenate([np.round(np.clip(enc, 0, 1) * 255) / 255, a[..., None]], axis=2).astype(np.float32)


def texture(base, n, qd):
    """RGBA float (premultiplied) of the quad's sub-image, or None (then a dot or skipped)"""
    w = qd["what"]
    if w == "menu backdrop":
        return back_tex(qd)
    if w in ("threat cue", "threat edge"):  # an attack's glow; the edge bands (a VIEW space: main() maps them to the head)
        rgb, a = threat_tex()
    elif w.startswith("hud") or w == "minimap":
        rgb, a = read_pnm(f"{base}.hud{n}.ppm"), read_pnm(f"{base}.hud{n}.alpha.pgm")
    elif w == "objective":  # build 15: SOMVR_OBJECTIVE's marker or edge arrow (its picture dumped with the frame)
        rgb, a = read_pnm(f"{base}.objective{n}.ppm"), read_pnm(f"{base}.objective{n}.alpha.pgm")
    elif w in ("vrmenu panel", "vrmenu card"):
        rgb, a = read_pnm(f"{base}.vrmenu{n}.ppm"), read_pnm(f"{base}.vrmenu{n}.alpha.pgm")
    else:
        return None
    if rgb is None or a is None:
        return None
    if a.ndim == 3:
        a = a[..., 0]
    x, y, rw, rh = qd["rect"]
    rgb = rgb[y:y + rh, x:x + rw].astype(np.float32) / 255
    a = a[y:y + rh, x:x + rw].astype(np.float32)[..., None] / 255
    if not rgb.size or not a.size:  # a rect outside this picture (a quad of another swapchain): skipped
        return None
    if qd["flags"] & 0x4:  # XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT
        rgb = rgb * a
    if not qd["flags"] & 0x2:  # no BLEND_TEXTURE_SOURCE_ALPHA: opaque
        a = np.ones_like(a)
    return np.concatenate([rgb, a], axis=2)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])


def view_to_ref(fr, qd):
    """a quad in the VIEW space (the edge bands) mapped into the reference space by the head: the eyes' mid point and
    the left eye's orientation (Monado's simulated HMD: both eyes the same)"""
    e0, e1 = fr["eyes"][0], fr["eyes"][1]
    hp, hq = 0.5 * (e0["p"] + e1["p"]), e0["q"]
    out = dict(qd)
    out["p"] = hp + qmat(hq) @ qd["p"]
    out["q"] = qmul(hq, qd["q"])
    out["other_space"] = False
    return out


def eye_size(eye, ppd):
    """the eye picture's size: the dump's, or ppd pixels per degree in its middle"""
    if not ppd:
        return eye["rect"][2], eye["rect"][3]
    l, r, u, d = (math.tan(v) for v in eye["fov"])
    return int(round(ppd * 57.29578 * (r - l))), int(round(ppd * 57.29578 * (u - d)))


def eye_rays(eye, size):
    l, r, u, d = (math.tan(v) for v in eye["fov"])
    w, h = size
    tx = l + (np.arange(w) + 0.5) / w * (r - l)
    ty = u - (np.arange(h) + 0.5) / h * (u - d)
    X, Y = np.meshgrid(tx, ty)
    dirs = np.stack([X, Y, -np.ones_like(X)], axis=-1)
    return dirs @ qmat(eye["q"]).T  # world directions (h, w, 3)


def ring(img, eye, size, qd):
    """comfort's vignette ring (re-xr-pipeline.md 11.3, 34): a head-locked black quad |p.z| m ahead in its VIEW space,
    centred on the view axis; alpha = smoothstep(R0, R0 * K, r / half) in tangent units, half = its half-size / |p.z|
    (the eyes look along the head's axis: the eye-space tangents are the view's)"""
    R0, K = 0.16, 1.75
    half = qd["size"][0] / 2 / max(1e-6, abs(qd["p"][2]))
    l, r, u, d = (math.tan(v) for v in eye["fov"])
    w, h = size
    X, Y = np.meshgrid(l + (np.arange(w) + 0.5) / w * (r - l), u - (np.arange(h) + 0.5) / h * (u - d))
    t = np.clip((np.sqrt(X * X + Y * Y) / half - R0) / (R0 * K - R0), 0, 1)
    a = (t * t * (3 - 2 * t))[..., None]
    if "wraith" in qd["what"]:  # build 12: the wraith dash's white-blue tunnel (premultiplied in linear light)
        tint = np.array([0.62, 0.80, 1.0]) * a
        img[:] = np.where(tint <= 0.0031308, 12.92 * tint, 1.055 * np.power(tint, 1 / 2.4) - 0.055) + img * (1 - a)
    else:
        img *= (1 - a)


def draw(img, eye, dirs, qd, tex):
    """img float (h, w, 3) in place; returns the quad's pixel mask"""
    R = qmat(qd["q"])
    o = R.T @ (eye["p"] - qd["p"])
    dl = dirs @ R  # = (R^T d) per pixel
    dz = dl[..., 2]
    with np.errstate(divide="ignore", invalid="ignore"):
        t = -o[2] / dz
    px, py = o[0] + t * dl[..., 0], o[1] + t * dl[..., 1]
    W, H = qd["size"]
    s, v = px / W + 0.5, 0.5 - py / H
    m = (t > 0) & (s >= 0) & (s < 1) & (v >= 0) & (v < 1)
    if not m.any():
        return m
    if tex is None:  # a marker: a white dot
        r2 = (s - 0.5) ** 2 + (v - 0.5) ** 2
        m &= r2 < 0.25
        img[m] = img[m] * 0.1 + 0.9
        return m
    th, tw = tex.shape[:2]
    fx, fy = s[m] * tw - 0.5, v[m] * th - 0.5  # bilinear
    x0, y0 = np.clip(np.floor(fx).astype(int), 0, tw - 1), np.clip(np.floor(fy).astype(int), 0, th - 1)
    x1, y1 = np.clip(x0 + 1, 0, tw - 1), np.clip(y0 + 1, 0, th - 1)
    ax, ay = np.clip(fx - x0, 0, 1)[:, None], np.clip(fy - y0, 0, 1)[:, None]
    c = (tex[y0, x0] * (1 - ax) * (1 - ay) + tex[y0, x1] * ax * (1 - ay) + tex[y1, x0] * (1 - ax) * ay +
         tex[y1, x1] * ax * ay)
    img[m] = c[:, :3] + img[m] * (1 - c[:, 3:4])
    return m


def report(fr, qd, eyes, ppd=0):
    if qd["what"].startswith("comfort ring"):  # head-locked: the clear angle and how black the view's edges are
        half = qd["size"][0] / 2 / max(1e-6, abs(qd["p"][2]))
        dark = lambda a: (lambda t: t * t * (3 - 2 * t))(min(1, max(0, (math.tan(a) / half - 0.16) / (0.28 - 0.16))))
        edge = min(abs(v) for v in eyes[0]["fov"])
        return (f"  comfort ring: clear {math.degrees(math.atan(0.16 * half)):4.1f} deg, black from "
                f"{math.degrees(math.atan(0.28 * half)):4.1f} deg; {100 * dark(edge):3.0f}% at the view's nearest edge "
                f"({math.degrees(edge):4.1f} deg), {100 * dark(math.radians(35)):3.0f}% at 35 deg")
    mid = 0.5 * (eyes[0]["p"] + eyes[1]["p"])
    dist = float(np.linalg.norm(qd["p"] - mid))
    W, H = qd["size"]
    wdeg, hdeg = (2 * math.degrees(math.atan(0.5 * s / max(dist, 1e-6))) for s in (W, H))
    e = eyes[0]
    _, _, ew, eh = e["rect"]
    eye_ppd_mid = ppd or ew / (math.tan(e["fov"][1]) - math.tan(e["fov"][0])) / 57.29578
    _, _, tw, th = qd["rect"]
    tppd = th / hdeg if hdeg else 0
    fwd = qmat(e["q"]) @ np.array([0, 0, -1.0])
    to = qd["p"] - mid
    off = math.degrees(math.acos(max(-1, min(1, float(fwd @ to) / max(np.linalg.norm(to), 1e-6)))))
    line = (f"  {qd['what']:<20} {dist:5.2f} m away, {off:4.0f} deg off the gaze, {W * 100:5.1f} x {H * 100:5.1f} cm = "
            f"{wdeg:5.1f} x {hdeg:5.1f} deg; texture {tw}x{th} = {tppd:5.1f} px/deg (eye image {eye_ppd_mid:4.1f} "
            f"px/deg mid, Quest 2 ~{Q2_PPD:.0f})")
    k = hdeg / th if th else 0
    if qd["what"] in ("vrmenu panel", "vrmenu card"):
        line += f"\n{'':24}text: a 32-px cell {32 * k:4.2f} deg, ~22-px capitals {22 * k:4.2f} deg = {22 * k * Q2_PPD:4.1f} " \
                f"Quest 2 px"
    elif qd["what"].startswith("hud") or qd["what"] == "minimap":  # the texture's pixels = the UI's (1080 lines)
        line += f"\n{'':24}game UI: an 18-px line {18 * k:4.2f} deg = {18 * k * Q2_PPD:4.1f} Quest 2 px, a 28-px line " \
                f"{28 * k:4.2f} deg = {28 * k * Q2_PPD:4.1f} px"
    return line


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("run")
    ap.add_argument("-f", "--frame", type=int, nargs="*")
    ap.add_argument("-o", "--out")
    ap.add_argument("--ppd", type=float, default=0, help="draw the eyes at this many px/deg (20: a Quest 2)")
    a = ap.parse_args()
    log = a.run if os.path.isfile(a.run) else os.path.join(a.run, "xr-layer.log")
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(log)), "compose")
    os.makedirs(out, exist_ok=True)
    frames = parse(log)
    for n in sorted(frames):
        if a.frame and n not in a.frame:
            continue
        fr = frames[n]
        st = read_pnm(f"{log}.stereo{n}.ppm")
        if st is None or len(fr["eyes"]) != 2:
            print(f"frame {n}: no stereo picture or eyes, skipped")
            continue
        print(f"frame {n}: {len(fr['quads'])} quads{', our hands' if fr.get('hands') else ''}")
        halves = []
        for ei in (0, 1):
            e = fr["eyes"][ei]
            x, y, w, h = e["rect"]
            img = st[y:y + h, x:x + w]
            size = eye_size(e, a.ppd)
            if size != (w, h):
                img = np.asarray(Image.fromarray(img).resize(size, Image.BILINEAR))
                w, h = size
            img = img.astype(np.float32) / 255
            dirs = eye_rays(e, size)
            hands = fr.get("hands")
            hl = None
            if hands:  # its half for this eye at this eye's size, premultiplied
                hrgb, ha = read_pnm(f"{log}.hands{n}.ppm"), read_pnm(f"{log}.hands{n}.alpha.pgm")
                if hrgb is not None and ha is not None:
                    hw = hands["w"]
                    rgb = Image.fromarray(hrgb[:, ei * hw:(ei + 1) * hw]).resize(size, Image.BILINEAR)
                    al = Image.fromarray(ha[:, ei * hw:(ei + 1) * hw]).resize(size, Image.BILINEAR)
                    hl = (np.asarray(rgb).astype(np.float32) / 255, np.asarray(al).astype(np.float32)[..., None] / 255)
            for i, qd in enumerate(fr["quads"]):
                if qd["other_space"] and qd["what"] in ("threat edge", "objective"):
                    fr["quads"][i] = view_to_ref(fr, qd)
            order = sorted(fr["quads"], key=lambda qd: qd["slot"])
            if hl is not None:
                order.append({"what": "hands", "slot": hands["slot"]})
                order.sort(key=lambda qd: qd["slot"])
            for qd in order:
                if qd["what"] == "hands":
                    img[:] = hl[0] + img * (1 - hl[1])
                    continue
                if qd["what"].startswith("comfort ring"):  # build 10: the vignette, in its place in the composition
                    ring(img, e, size, qd)
                    continue
                if qd["what"] == "comfort blink wraith":  # build 12: the strike's white blink (one uniform cell)
                    wa = qd["rect"][0] // 4 / 127 if "rect" in qd else 1.0
                    img[:] = img * (1 - wa) + np.array([0.82, 0.91, 1.0]) * wa
                    continue
                if qd["what"] in ("fade", "menu dim") and "rect" in qd:  # a blink/trip black, a menu's dim: the whole view
                    fa = max(0, min(63, (qd["rect"][0] - 1) // 4)) / 63  # vklayer/somvr_fade.h's 64 levels x 4 texels
                    img[:] = img * (1 - fa)
                    continue
                if qd["other_space"] or qd["what"] in ("comfort blink", "fade", "menu dim"):
                    continue
                m = draw(img, e, dirs, qd, texture(log, n, qd))
                if ei == 0 and m.any():
                    ys, xs = np.nonzero(m)
                    pad = 40
                    x0, x1 = max(0, xs.min() - pad), min(w, xs.max() + pad)
                    y0, y1 = max(0, ys.min() - pad), min(h, ys.max() + pad)
                    qd["crop"] = (x0, y0, x1, y1)
            u8 = (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)
            halves.append(u8)
            if ei == 0:
                for qd in fr["quads"]:
                    if "crop" in qd:
                        x0, y0, x1, y1 = qd["crop"]
                        name = qd["what"].replace(" ", "-").replace("(", "").replace(")", "")
                        Image.fromarray(u8[y0:y1, x0:x1]).save(os.path.join(out, f"frame{n}-{name}.png"))
        both = np.concatenate(halves, axis=1)
        Image.fromarray(both).resize((both.shape[1] // 2, both.shape[0] // 2), Image.LANCZOS).save(
            os.path.join(out, f"frame{n}.png"))
        for qd in fr["quads"]:
            print(report(fr, qd, fr["eyes"], a.ppd) + ("" if "crop" in qd or qd["what"].endswith("marker") else
                                                 "  [not in the left eye's view]"))
    print(f"-> {out}")


if __name__ == "__main__":
    sys.exit(main())
