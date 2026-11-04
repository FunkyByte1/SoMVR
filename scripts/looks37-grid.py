#!/usr/bin/env python3
"""looks37-grid.py OUT.jpg [--views a,b,..] [--eye 0|1] [--crop F] [--w PX] LABEL=RUN ... - som-b37-looks
(docs/re-winport.md 7.15): one sheet of scripts/looks36-drive.sh runs - a row per view (default the brief's five:
fog-ahead dry-down wall-up ground-down meadow-feet), a column per run (labelled), each cell that view's eye (the mark's
eyes.jpg, else eyes.ppm; --crop F its centre F) PX wide (default 640). Per view the first run's last mark, and in each
other run the mark of that view nearest it in head yaw (its difference on the label)."""
import os, sys
from PIL import Image, ImageDraw

args = sys.argv[1:]
out = args.pop(0)
views = 'fog-ahead dry-down wall-up ground-down meadow-feet'.split()
EYE, CROP, W = 0, 1.0, 640
runs = []
while args:
    a = args.pop(0)
    if a == '--views':
        views = args.pop(0).split(',')
    elif a == '--eye':
        EYE = int(args.pop(0))
    elif a == '--crop':
        CROP = max(0.2, min(1.0, float(args.pop(0))))
    elif a == '--w':
        W = int(args.pop(0))
    else:
        lab, run = a.split('=', 1)
        runs.append((lab, run))

def steps(run):   # {view: [(mark dir, head yaw)]} - every mark of the view (a re-aimed view leaves several)
    m = {}
    for line in open(os.path.join(run, 'looks-steps.txt')):
        p = line.split()
        if len(p) >= 2:
            try:
                hy = float(p[-1])
            except ValueError:
                hy = 0.0
            m.setdefault(p[0], []).append((os.path.join(run, 'marks', p[1]), hy))
    return m

def wrapd(a):
    return (a + 180.0) % 360.0 - 180.0

def cell(d):
    for f in ('eyes.jpg', 'eyes.ppm'):
        p = os.path.join(d, f)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            w, h = im.size
            im = im.crop((0, 0, w // 2, h)) if EYE == 0 else im.crop((w // 2, 0, w, h))
            if CROP < 1.0:
                w, h = im.size
                cw, ch = int(w * CROP), int(h * CROP)
                im = im.crop(((w - cw) // 2, (h - ch) // 2, (w - cw) // 2 + cw, (h - ch) // 2 + ch))
            return im.resize((W, int(im.size[1] * W / im.size[0])))
    return None

st = [steps(r) for _, r in runs]
pick, dys = [], []   # per view: the first run's last mark; each other run's mark nearest it in head yaw
for v in views:
    ref = st[0][v][-1][1] if v in st[0] else None
    row, drow = [], []
    for k, s in enumerate(st):
        if v not in s:
            row.append(None); drow.append(None); continue
        d, y = s[v][-1] if k == 0 or ref is None else min(s[v], key=lambda t: abs(wrapd(t[1] - ref)))
        row.append(d); drow.append(None if ref is None else wrapd(y - ref))
    pick.append(row); dys.append(drow)
cells = [[cell(d) if d else None for d in row] for row in pick]
H = max((c.size[1] for row in cells for c in row if c), default=W)
G = 6
sheet = Image.new('RGB', (len(runs) * (W + G), len(views) * (H + G) + 26), (0, 0, 0))
dr = ImageDraw.Draw(sheet)
for j, (lab, _) in enumerate(runs):
    dr.text((j * (W + G) + 6, 6), lab, fill=(255, 255, 0))
for i, v in enumerate(views):
    for j, c in enumerate(cells[i]):
        x, y = j * (W + G), 26 + i * (H + G)
        if c:
            sheet.paste(c, (x, y))
        t = (v + (f' {dys[i][j]:+.0f} deg' if j and dys[i][j] is not None else '')) if c else v + ': missing'
        dr.rectangle((x + 2, y + 2, x + 8 + 7 * len(t), y + 18), fill=(0, 0, 0))
        dr.text((x + 5, y + 5), t, fill=(255, 255, 0))
sheet.save(out, quality=88)
print(out, sheet.size)
