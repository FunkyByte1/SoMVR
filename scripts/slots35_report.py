#!/usr/bin/env python3
"""slots35_report.py OUT - the table of scripts/slots35-measure.sh's measurement (som-b35-slots, re-xr-pipeline.md 158.4).

Per boot (wave-driver): the slot count of its wave, boot time (the harness's start to `PRACTICE ... arrived`), the
frame rate the compositor got (the layer's `xr: perf` frames / s), the render thread's frame work (eye 0's start to the
present: wall mean / p95 ms), the game thread's busy time, the GPU's time a frame (mean / p95), the drivers' PASS / FAIL,
their PASS/FAIL lines against the same driver's solo boot (the text without its numbers; the numbers that changed), the
game's CPU (cores) and peak RSS; per wave the peaks of VRAM, GTT, RAM, CPU and the GPU's busy share. Only the perf
windows wholly after the arrival count (the world, not the loading)."""
import glob, os, re, sys

OUT = sys.argv[1]
NUM = re.compile(r"-?\d+(?:\.\d+)?")


def rd(p, default=""):
    try:
        with open(p, errors="replace") as f:
            return f.read()
    except OSError:
        return default


def mean(v):
    return sum(v) / len(v) if v else float("nan")


def perf(run, t_arr):
    """(fps, work mean, work p95 worst, busy mean, busy p95 worst, gpu mean, gpu p95 worst, windows)"""
    som = rd(os.path.join(run, "somvr.log"))
    xr = rd(os.path.join(run, "xr-layer.log"))
    ta = None
    m = re.search(r"^PRACTICE t=([\d.]+) .*arrived", som, re.M)
    if m:
        ta = float(m.group(1))
    work, busy = [], []
    for m in re.finditer(r"^PERF t=([\d.]+) s=([\d.]+) frames=\d+ .*?game thread: busy ([\d.]+)/[\d.]+/([\d.]+)/.*?"
                         r"its frame work \(eye 0's start to the present: \d+ frames\) cpu [\d./]+ \(over: \d+\) wall "
                         r"([\d.]+)/[\d.]+/([\d.]+)/", som, re.M):
        t, s = float(m.group(1)), float(m.group(2))
        if ta is None or t - s < ta:
            continue
        busy.append((float(m.group(3)), float(m.group(4))))
        work.append((float(m.group(5)), float(m.group(6))))
    fps, gpu = [], []
    for m in re.finditer(r"^([\d.]+) \[[^]]*\] xr: perf: ([\d.]+) s, (\d+) frames \(mean/p50/p95/max ms\): GPU total "
                         r"([\d.]+)/[\d.]+/([\d.]+)/", xr, re.M):
        t, s, n = float(m.group(1)), float(m.group(2)), int(m.group(3))
        if ta is None or t - s < ta or s <= 0:
            continue
        fps.append(n / s)
        gpu.append((float(m.group(4)), float(m.group(5))))
    mx = lambda v, i: max((x[i] for x in v), default=float("nan"))
    return (mean(fps), mean([w[0] for w in work]), mx(work, 1), mean([b[0] for b in busy]), mx(busy, 1),
            mean([g[0] for g in gpu]), mx(gpu, 1), len(work))


def lines(drive):
    out = []
    for l in drive.splitlines():
        if re.search(r"\b(PASS|FAIL)\b", l):
            out.append(re.sub(r"^\d\d:\d\d:\d\d ", "", l).strip())
    return out


NOISE = re.compile(r"\bt=[\d.]+|0x[0-9a-fA-F]+|\b\d{5,}(?:\.\d+)?\b|\b(?:frame|line|member|orc|hash) [\w.]+|\[[\d/]+\]")


def key(l):   # the verdict's text: no numbers, no game-clock stamps, addresses, frame/line numbers
    return NUM.sub("#", NOISE.sub("", l))


def nums(l):   # the numbers that can say something (not the clock, an address, a frame or line number)
    return NUM.findall(NOISE.sub("", l))


samples = []
for l in rd(os.path.join(OUT, "samples.tsv")).splitlines()[1:]:
    f = l.split("\t")
    if len(f) == 8:
        samples.append([float(x) for x in f])
games = {}
for l in rd(os.path.join(OUT, "games.tsv")).splitlines()[1:]:
    f = l.split("\t")
    if len(f) == 5:
        e, pid, ticks, rss, run = float(f[0]), f[1], int(f[2]), int(f[3]), f[4]
        g = games.setdefault(run, {})
        p = g.setdefault(pid, [e, ticks, e, ticks, 0])
        p[2], p[3], p[4] = e, ticks, max(p[4], rss)
waves = {}
for l in rd(os.path.join(OUT, "waves.txt")).splitlines():
    f = l.split()
    if len(f) >= 3 and f[2] != "end":
        waves[int(f[1])] = [float(f[0]), None, f[2]]
    elif len(f) == 3 and f[2] == "end" and int(f[1]) in waves:
        waves[int(f[1])][1] = float(f[0])

rows, solo = [], {}
for tf in sorted(glob.glob(os.path.join(OUT, "run-*.txt")), key=lambda p: [int(x) if x.isdigit() else x for x in re.split(r"[-.]", os.path.basename(p))]):
    tag = os.path.basename(tf)[4:-4]
    w, d = tag.split("-", 1)
    w = int(w)
    run = rd(tf).strip()
    wstr = waves.get(w, [0, 0, d])[2]
    gate = wstr.startswith("gate:")
    n_at_once = len(wstr.replace("gate:", "").split(","))
    t = rd(os.path.join(OUT, "times-%s.txt" % tag)).split()
    boot = float(t[1]) - float(t[0]) if len(t) == 3 and t[1] != "-" else float("nan")
    drive = rd(os.path.join(OUT, "drive-%s.txt" % tag))
    pf = lines(drive)
    npass = sum(1 for l in pf if re.search(r"\bPASS\b", l))
    nfail = sum(1 for l in pf if re.search(r"\bFAIL\b", l))
    summ = rd(os.path.join(run, "summary.txt"))
    save = "UNCHANGED" if "GameData.sav UNCHANGED" in summ else ("CHANGED" if "GameData.sav CHANGED" in summ else "?")
    crash = "crash" if "!!! crash" in summ else ""
    warn = len(re.findall(r"^WARN", rd(os.path.join(run, "somvr.log")), re.M))
    g = games.get(os.path.basename(run), {})
    cores = sum((p[3] - p[1]) / 100.0 for p in g.values()) / max(1e-9, max((p[2] - p[0] for p in g.values()), default=0))
    rss = sum(p[4] for p in g.values()) / 1048576.0
    p = perf(run, None)
    if n_at_once == 1 and d not in solo and not gate:
        solo[d] = pf
    if gate:
        crash = (crash + " the VR stop test").strip()
    rows.append((w, d, n_at_once, tag, run, boot, p, npass, nfail, pf if not gate else None, save, crash, warn, cores, rss))

print("| wave | driver | at once | boot s | fps | frame work ms mean/p95 | game busy ms mean/p95 | GPU ms mean/p95 | PASS/FAIL | vs solo | CPU cores | RSS GiB | save | WARN |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
diffs = []
for (w, d, n, tag, run, boot, p, npass, nfail, pf, save, crash, warn, cores, rss) in rows:
    vs = "-"
    if pf is None:
        vs = "(gate test)"
    elif n > 1 or solo.get(d) is not pf:
        s = solo.get(d)
        if s is None:
            vs = "no solo"
        else:
            ks, kp = [key(l) for l in s], [key(l) for l in pf]
            same_text = sorted(ks) == sorted(kp)
            vs = "same" if same_text else "DIFFERENT"
            if same_text:
                ch = []
                for a, b in zip(sorted(s, key=key), sorted(pf, key=key)):
                    if nums(a) != nums(b):
                        na, nb = nums(a), nums(b)
                        ch.append("; ".join("%s->%s" % (x, y) for x, y in zip(na, nb) if x != y))
                if ch:
                    vs = "same (%d with other numbers)" % len(ch)
                    diffs.append((tag, ch))
            else:
                diffs.append((tag, ["missing: " + l for l in s if key(l) not in kp] + ["new: " + l for l in pf if key(l) not in ks]))
    print("| %d | %s | %d | %.0f | %.1f | %.2f/%.2f | %.2f/%.2f | %.2f/%.2f | %d/%d | %s | %.2f | %.2f | %s | %d |%s" % (
        w, d, n, boot, p[0], p[1], p[2], p[3], p[4], p[5], p[6], npass, nfail, vs, cores, rss, save, warn,
        " " + crash if crash else ""))
print()
print("| wave | drivers | s | VRAM peak MiB | GTT peak MiB | RAM used peak MiB | CPU mean/peak % | GPU busy mean/peak % | games peak RSS MiB |")
print("|---|---|---|---|---|---|---|---|---|")
for w in sorted(waves):
    a, b, ds = waves[w]
    if b is None:
        continue
    s = [x for x in samples if a <= x[0] <= b]
    if not s:
        continue
    print("| %d | %s | %.0f | %d | %d | %d | %.0f/%d | %.0f/%d | %d |" % (
        w, ds, b - a, max(x[1] for x in s), max(x[2] for x in s), max(x[4] for x in s), mean([x[5] for x in s]),
        max(x[5] for x in s), mean([x[3] for x in s]), max(x[3] for x in s), max(x[7] for x in s)))
if samples:
    base = [x for x in samples if x[6] == 0]
    if base:
        print("\nno game running: VRAM %d MiB, RAM used %d MiB, CPU %.0f %% (%d samples)" % (
            min(x[1] for x in base), min(x[4] for x in base), mean([x[5] for x in base]), len(base)))
for g in sorted(glob.glob(os.path.join(OUT, "gate-*.txt"))):
    t = rd(g).split("\n")
    t0 = float(t[0].split()[1]) if t and t[0].startswith("made ") else None
    print("\nthe VR stop test %s: %s" % (os.path.basename(g)[:-4], "; ".join(
        "%s gone after %.1f s" % (l.split()[0], float(l.split()[2]) - t0) for l in t[1:] if l.strip() and t0)))
for tag, ch in diffs:
    print("\n%s against its solo boot:" % tag)
    for c in ch[:12]:
        print("  " + c[:220])
k = rd(os.path.join(OUT, "kernel.log"))
bad = [l for l in k.splitlines() if re.search(r"amdgpu|VRAM|GPU|drm", l, re.I)]
print("\nkernel log during the measurement: %d lines, %d amdgpu/drm lines%s" % (len(k.splitlines()), len(bad), ":" if bad else ""))
for l in bad[:10]:
    print("  " + l[:200])
if os.path.exists(os.path.join(OUT, "STOP")):
    print("\nSTOP: " + rd(os.path.join(OUT, "STOP")).strip())
