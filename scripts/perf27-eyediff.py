#!/usr/bin/env python3
"""perf27-eyediff.py RUN [-o DIR | --alt] - SOMVR_SHADOW_SHARE's check (docs/re-xr-pipeline.md 115.5, som-b27-perf).

perf27-drive.sh's step `shcheck` dumps three consecutive stereo frames losslessly in each mode (off, cascades, all,
off again; RUN/perf-steps.txt: `dump sh-MODE N1 N2 N3`, the files RUN/xr-layer.log.stereoN.ppm, both eyes side by
side). The share touches only eye 1, so eye 0 is the yardstick: between two modes eye 0 differs only by what moved
(rain, grass, the time); if eye 1 differs by no more than eye 0 does, eye 1 with eye 0's shadow maps is eye 1 with its
own. Per pair: the mean absolute difference (0..255, RGB), the share of pixels off by more than 8 and 24, each eye;
the noise = the frames of one mode against each other. -o DIR: eye 1's difference, x8, as PNG (where it differs)."""
import sys, os, re
import numpy as np

def ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    w, h = int(m.group(1)), int(m.group(2))
    return np.frombuffer(data[m.end():m.end() + w * h * 3], np.uint8).reshape(h, w, 3)

def stats(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    return d, float(d.mean()), float((d > 8).mean() * 100), float((d > 24).mean() * 100)

def alt(run):
    """--alt: triples dumped with `shadowshare alt...` (the share in odd frames only): eye e's middle frame against the
    mean of its neighbours; the share changes eye 1 alone, so eye 1's deviation above eye 0's is what it changed"""
    print('mode            frames            | eye 0 dev  >16%% | eye 1 dev  >16%% | eye 1 - eye 0')
    for line in open(os.path.join(run, 'perf-steps.txt')):
        p = line.split()
        if len(p) != 5 or p[0] != 'dump' or not p[1].startswith('alt-'):
            continue
        f = [ppm(os.path.join(run, 'xr-layer.log.stereo%s.ppm' % n)).astype(np.float32) for n in p[2:]]
        w = f[0].shape[1] // 2
        r = []
        for e in (0, 1):
            a, b, c = (x[:, e * w:(e + 1) * w] for x in f)
            d = np.abs(b - 0.5 * (a + c)).max(axis=2)
            r.append((float(d.mean()), float((d > 16).mean() * 100)))
        print('%-15s %-17s | %9.3f %5.2f | %9.3f %5.2f | %+.3f' % (p[1][4:], ' '.join(p[2:]), r[0][0], r[0][1], r[1][0],
                                                                 r[1][1], r[1][0] - r[0][0]))

def main():
    if '--alt' in sys.argv:
        return alt(sys.argv[1])
    run = sys.argv[1]
    out = sys.argv[sys.argv.index('-o') + 1] if '-o' in sys.argv else None
    sets = []
    for line in open(os.path.join(run, 'perf-steps.txt')):
        p = line.split()
        if len(p) >= 3 and p[0] == 'dump' and p[1].startswith('sh-'):
            sets.append((p[1][3:], [int(x) for x in p[2:]]))
    if not sets:
        sys.exit('no `dump sh-MODE` lines in perf-steps.txt')
    img = {}
    for _, frames in sets:
        for n in frames:
            img[n] = ppm(os.path.join(run, 'xr-layer.log.stereo%d.ppm' % n))
    w = next(iter(img.values())).shape[1] // 2
    eye = lambda n, e: img[n][:, e * w:(e + 1) * w]
    base = sets[0][1][0]
    print('mode          frames         | eye 0: mean  >8%%  >24%% | eye 1: mean  >8%%  >24%% | eye 1 - eye 0 (mean)')
    for k, (mode, frames) in enumerate(sets):
        for n in frames[:1] if k else frames[1:2]:
            r = []
            for e in (0, 1):
                d, mn, p8, p24 = stats(eye(base, e), eye(n, e))
                r.append((mn, p8, p24))
                if e == 1 and out and k:
                    os.makedirs(out, exist_ok=True)
                    from PIL import Image
                    Image.fromarray(np.clip(d.astype(np.int32) * 8, 0, 255).astype(np.uint8)).save(
                        os.path.join(out, 'eye1-diff-%s-%d-vs-%d.png' % (mode, n, base)))
            label = '%s vs off %d' % (mode, base) if k else 'noise (off, next frame)'
            print('%-28s %6d | %10.3f %5.2f %5.2f | %10.3f %5.2f %5.2f | %+.3f' % (label, n, r[0][0], r[0][1], r[0][2],
                                                                                r[1][0], r[1][1], r[1][2], r[1][0] - r[0][0]))

if __name__ == '__main__':
    main()
