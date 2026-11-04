#!/usr/bin/env python3
"""looks36-cmp.py [--eye 0|1] [--crop F] RUN_A RUN_B OUT [LABEL_A LABEL_B] - som-b36-winlooks (docs/re-winport.md 7.14): the views of two
scripts/looks36-drive.sh runs side by side - the marks paired by name (RUN/looks-steps.txt), each pair's left eyes
(the mark's eyes.jpg, else eyes.ppm) as OUT/<name>.jpg, A left and B right, 900 px each, labelled. Typical: a Linux
and a Windows boot of the same places and weather (the weather each run arrived in: its `PRACTICE t=.. light:` lines).
b37 looks: --match B's mark of each view nearest A's in head yaw (of all B's marks of that name: a re-aimed view
leaves several; the difference on B's label); --eye 1 the right eyes; --crop F the centre F (0.2..1) of each eye, so the relief of a wall or the paving
shows at the sheet's size (OUT/<name>.jpg as before)."""
import os, sys
from PIL import Image, ImageDraw

def steps(run, every=False):
    out = {}
    for line in open(os.path.join(run, 'looks-steps.txt')):
        p = line.split()
        if len(p) >= 2:
            try:
                hy = float(p[-1])
            except ValueError:
                hy = None
            if every:
                out.setdefault(p[0], []).append((os.path.join(run, 'marks', p[1]), hy))
            else:
                out[p[0]] = os.path.join(run, 'marks', p[1])
    return out

def wrapd(a):
    return (a + 180.0) % 360.0 - 180.0

def matched(a, b):
    """b37 looks --match: per view, B's mark (of all its marks of that name - a re-aimed view leaves several) whose head
    yaw is nearest A's last one; {name: (dir, yaw difference)}"""
    la, lb = steps(a, True), steps(b, True)
    out = {}
    for n, ms in la.items():
        if n not in lb or ms[-1][1] is None:
            continue
        ya = ms[-1][1]
        d, yb = min(((d, y) for d, y in lb[n] if y is not None), key=lambda t: abs(wrapd(t[1] - ya)), default=(None, None))
        if d:
            out[n] = (d, wrapd(yb - ya))
    return out

def eye(d, which=0):
    for f in ('eyes.jpg', 'eyes.ppm'):
        p = os.path.join(d, f)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            w, h = im.size
            return im.crop((0, 0, w // 2, h)) if which == 0 else im.crop((w // 2, 0, w, h))
    return None

args = sys.argv[1:]
EYE, CROP = 0, 1.0
MATCH = False
while args and args[0] in ('--eye', '--crop', '--match'):
    if args[0] == '--match':
        MATCH = True; args = args[1:]; continue
    if args[0] == '--eye':
        EYE = int(args[1])
    else:
        CROP = max(0.2, min(1.0, float(args[1])))
    args = args[2:]
a, b, out = args[0:3]
la = args[3] if len(args) > 3 else os.path.basename(a)
lb = args[4] if len(args) > 4 else os.path.basename(b)

def view(d):
    im = eye(d, EYE)
    if im is None or CROP >= 1.0:
        return im
    w, h = im.size
    cw, ch = int(w * CROP), int(h * CROP)
    return im.crop(((w - cw) // 2, (h - ch) // 2, (w - cw) // 2 + cw, (h - ch) // 2 + ch))
os.makedirs(out, exist_ok=True)
sa, sb = steps(a), steps(b)
dy = {}
if MATCH:
    for n, (d, e) in matched(a, b).items():
        sb[n], dy[n] = d, e
for n in sa:
    if n not in sb or n.endswith('-ref'):
        continue
    ea, eb = view(sa[n]), view(sb[n])
    if ea is None or eb is None:
        print(n, 'missing eyes'); continue
    W = 900
    ea = ea.resize((W, int(ea.size[1] * W / ea.size[0])))
    eb = eb.resize((W, int(eb.size[1] * W / eb.size[0])))
    im = Image.new('RGB', (2 * W + 10, max(ea.size[1], eb.size[1])), (0, 0, 0))
    im.paste(ea, (0, 0)); im.paste(eb, (W + 10, 0))
    dr = ImageDraw.Draw(im)
    for x, t in ((8, f'{la} {n}'), (W + 18, f'{lb} {n}' + (f' (head yaw {dy[n]:+.0f} deg)' if n in dy else ''))):
        dr.rectangle((x - 4, 4, x + 8 * len(t), 24), fill=(0, 0, 0)); dr.text((x, 8), t, fill=(255, 255, 0))
    im.save(os.path.join(out, n + '.jpg'), quality=88)
    print(n, 'ok')
