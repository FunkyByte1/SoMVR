#!/usr/bin/env python3
"""quest_text.py - som-b22-screens: how a game page's text reads on a Quest 2 at a board's size, from its flat picture.

  quest_text.py SHOT.png OUT.png [--deg D ...] [--crop X0,Y0,X1,Y1] [--ppd 20]

The game's UI (1920 x 1080) on a board D degrees wide is seen at 1920 / D UI pixels per degree; a Quest 2 shows about
20 pixels per degree in the middle of the lens. Each D: the page resampled to the headset's pixels (Lanczos, linear
light) and shown back at 2x for a crop (the UI's pixel box X0,Y0,X1,Y1, default the whole page) - side by side, one row
per D, labelled. 62 deg = the board as built (0.72 m at 0.6 m). Prints each D's 18- and 28-pixel line in degrees and
Quest 2 pixels.
"""
import argparse

import numpy as np
from PIL import Image, ImageDraw


def to_lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def to_srgb(a):
    a = np.clip(a, 0, 1)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055) * 255.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("out")
    ap.add_argument("--deg", type=float, nargs="+", default=[62.0, 70.0, 78.0])
    ap.add_argument("--crop", default=None)
    ap.add_argument("--ppd", type=float, default=20.0)
    a = ap.parse_args()
    im = Image.open(a.shot).convert("RGB")
    W, H = im.size
    x0, y0, x1, y1 = (0, 0, W, H) if not a.crop else [int(v) for v in a.crop.split(",")]
    lin = to_lin(np.asarray(im, dtype=np.float64))
    rows = []
    for d in a.deg:
        qw = max(8, int(round(d * a.ppd)))  # the page's width in headset pixels
        k = qw / W
        small = np.zeros((int(round(H * k)), qw, 3))
        for c in range(3):
            ch = Image.fromarray(lin[:, :, c].astype(np.float32), mode="F")
            small[:, :, c] = np.asarray(ch.resize((qw, small.shape[0]), Image.LANCZOS))
        sm = Image.fromarray(to_srgb(small).astype(np.uint8))
        cx0, cy0, cx1, cy1 = int(x0 * k), int(y0 * k), int(x1 * k), int(y1 * k)
        crop = sm.crop((cx0, cy0, cx1, cy1))
        crop = crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST)
        lab = Image.new("RGB", (crop.width, crop.height + 22), (40, 40, 40))
        lab.paste(crop, (0, 22))
        ImageDraw.Draw(lab).text((4, 4), "%.0f deg wide: %.1f UI px/deg, an 18-px line %.2f deg = %.1f Quest 2 px" %
                                 (d, W / d, 18 * d / W, 18 * d / W * a.ppd), fill=(255, 255, 0))
        rows.append(lab)
        print("%.0f deg: an 18-px line %.2f deg (%.1f Quest 2 px), a 28-px line %.2f deg (%.1f px)" %
              (d, 18 * d / W, 18 * d / W * a.ppd, 28 * d / W, 28 * d / W * a.ppd))
    out = Image.new("RGB", (max(r.width for r in rows), sum(r.height for r in rows)), (0, 0, 0))
    y = 0
    for r in rows:
        out.paste(r, (0, y))
        y += r.height
    out.save(a.out)


if __name__ == "__main__":
    main()
