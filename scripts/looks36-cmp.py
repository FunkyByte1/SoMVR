#!/usr/bin/env python3
"""looks36-cmp.py RUN_A RUN_B OUT [LABEL_A LABEL_B] - som-b36-winlooks (docs/re-winport.md 7.14): the views of two
scripts/looks36-drive.sh runs side by side - the marks paired by name (RUN/looks-steps.txt), each pair's left eyes
(the mark's eyes.jpg, else eyes.ppm) as OUT/<name>.jpg, A left and B right, 900 px each, labelled. Typical: a Linux
and a Windows boot of the same places and weather (the weather each run arrived in: its `PRACTICE t=.. light:` lines)."""
import os, sys
from PIL import Image, ImageDraw

def steps(run):
    out = {}
    for line in open(os.path.join(run, 'looks-steps.txt')):
        p = line.split()
        if len(p) >= 2:
            out[p[0]] = os.path.join(run, 'marks', p[1])
    return out

def eye(d, which=0):
    for f in ('eyes.jpg', 'eyes.ppm'):
        p = os.path.join(d, f)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            w, h = im.size
            return im.crop((0, 0, w // 2, h)) if which == 0 else im.crop((w // 2, 0, w, h))
    return None

a, b, out = sys.argv[1:4]
la = sys.argv[4] if len(sys.argv) > 4 else os.path.basename(a)
lb = sys.argv[5] if len(sys.argv) > 5 else os.path.basename(b)
os.makedirs(out, exist_ok=True)
sa, sb = steps(a), steps(b)
for n in sa:
    if n not in sb or n.endswith('-ref'):
        continue
    ea, eb = eye(sa[n]), eye(sb[n])
    if ea is None or eb is None:
        print(n, 'missing eyes'); continue
    W = 900
    ea = ea.resize((W, int(ea.size[1] * W / ea.size[0])))
    eb = eb.resize((W, int(eb.size[1] * W / eb.size[0])))
    im = Image.new('RGB', (2 * W + 10, max(ea.size[1], eb.size[1])), (0, 0, 0))
    im.paste(ea, (0, 0)); im.paste(eb, (W + 10, 0))
    dr = ImageDraw.Draw(im)
    for x, t in ((8, f'{la} {n}'), (W + 18, f'{lb} {n}')):
        dr.rectangle((x - 4, 4, x + 8 * len(t), 24), fill=(0, 0, 0)); dr.text((x, 8), t, fill=(255, 255, 0))
    im.save(os.path.join(out, n + '.jpg'), quality=88)
    print(n, 'ok')
