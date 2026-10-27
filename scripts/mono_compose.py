#!/usr/bin/env python3
"""mono_compose.py - see a flat (mono) game frame on its quad as the player does, in both eyes (the collect drill's
round 2: SOMVR_INSPECT's artifact view on the menu board). The layer dumps only stereo frames (xr_compose.py); a mono
frame is one image on one quad over the space. This draws that image (a flat shot or a mark's mono copy) on the quad
the layer logged (`xr: menus: the menu quad's board placed at X,Y,Z facing yaw D deg, W x H m`, or the theatre's
`theatre N: ... on a W m screen at X,Y,Z`) in the eyes of a stereo dump's frame (its eye poses and fovs; the headless
test head does not move while the menu is up), over a dark grey space, both eyes side by side, and prints the quad's
size in degrees and the game UI's line heights in Quest 2 pixels (xr_compose.py's report).

  mono_compose.py RUN -f N --quad X,Y,Z,YAWDEG,W,H[,TILTDEG] --tex IMAGE [-o OUT.png] [--ppd 20] [--look]

TILTDEG: the board's normal's pitch (the layer's `(tilt D deg)`, build 17: a board down the gaze tilts up).

--look: both eyes turned to the quad's middle (a player looking at the board: the menu's board is placed level at the
head's yaw, so a head that looked down at an artifact looks up to it), their positions kept.
"""
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xr_compose as xc  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("run")
    ap.add_argument("-f", "--frame", type=int, required=True)
    ap.add_argument("--quad", required=True, help="X,Y,Z,YAWDEG,W,H (the layer's line, metres, LOCAL space)")
    ap.add_argument("--tex", required=True)
    ap.add_argument("-o", "--out")
    ap.add_argument("--ppd", type=float, default=20)
    ap.add_argument("--look", action="store_true")
    a = ap.parse_args()
    log = a.run if os.path.isfile(a.run) else os.path.join(a.run, "xr-layer.log")
    fr = xc.parse(log).get(a.frame)
    if not fr or len(fr["eyes"]) != 2:
        sys.exit(f"frame {a.frame}: no eyes in {log}")
    qv = [float(v) for v in a.quad.split(",")]
    x, y, z, yaw, w, h = qv[:6]
    tilt = qv[6] if len(qv) > 6 else 0.0
    r = math.radians(yaw) / 2
    t = math.radians(-tilt) / 2  # about the board's X: +tilt = its normal up
    tex = np.asarray(Image.open(a.tex).convert("RGB")).astype(np.float32) / 255
    tex = np.concatenate([tex, np.ones(tex.shape[:2] + (1,), np.float32)], axis=-1)
    q = xc.qmul(np.array([0, math.sin(r), 0, math.cos(r)]), np.array([math.sin(t), 0, 0, math.cos(t)]))
    qd = {"what": "hud (mono frame)", "p": np.array([x, y, z]), "q": q,
          "size": (w, h), "rect": [0, 0, tex.shape[1], tex.shape[0]], "flags": 0x6, "slot": 0}
    if a.look:  # a yaw-pitch orientation whose -Z points at the quad's middle
        mid = 0.5 * (fr["eyes"][0]["p"] + fr["eyes"][1]["p"])
        d = qd["p"] - mid
        yw, pt = math.atan2(-d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2]))
        qy = np.array([0, math.sin(yw / 2), 0, math.cos(yw / 2)])
        qp = np.array([math.sin(pt / 2), 0, 0, math.cos(pt / 2)])
        for ei in (0, 1):
            fr["eyes"][ei]["q"] = xc.qmul(qy, qp)
    halves = []
    for ei in (0, 1):
        e = fr["eyes"][ei]
        size = xc.eye_size(e, a.ppd)
        img = np.full((size[1], size[0], 3), 0.08, np.float32)
        m = xc.draw(img, e, xc.eye_rays(e, size), qd, tex)
        print(f"eye {ei}: the quad covers {100.0 * m.mean():.1f} % of the eye picture")
        halves.append((np.clip(img, 0, 1) * 255).astype(np.uint8))
    print(xc.report(fr, qd, fr["eyes"], a.ppd))
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(log)), "compose", f"mono-frame{a.frame}.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    hh = min(hv.shape[0] for hv in halves)
    Image.fromarray(np.concatenate([hv[:hh] for hv in halves], axis=1)).save(out)
    print(out)


if __name__ == "__main__":
    main()
