#!/usr/bin/env python3
"""lane_frames.py RUN [--budget MS] [--md] - the lane's frame-time table (som-b35-lane, docs/re-xr-pipeline.md 163) from a
boot's SOMVR_PERF_RAW file (RUN/perf-raw.txt): each `perf NAME` window's render-thread frame work (eye 0's start to the
present) - frames, mean, p50, p95, p99, max, frames over the budget (11.1 ms = 90 Hz) - and its present intervals (p99,
late = over 1.5 x the run's median); each load against the quiet windows around it. Then the closed loop's window
(loop-hog): the overload's start (RUN/lane-loop-hog.log), the watcher's FREEZE/THAW lines (logs/lane/), the latency to
the first pause, the 0.5 s rolling median at it against the quiet baseline's p99, the frames over p99 before it, the
lane's share of the window.
"""
import argparse
import bisect
import glob
import os
import re
import statistics
import sys


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    k = min(len(s) - 1, max(0, int(round(q / 100.0 * (len(s) - 1)))))
    return s[k]


def load(run):
    marks, frames, clock = [], [], "monotonic"
    for ln in open(os.path.join(run, "perf-raw.txt")):
        p = ln.split()
        if not p:
            continue
        if p[0] == "#":
            if "monotonic_raw" in ln:
                clock = "monotonic_raw"
            if len(p) >= 4 and p[1] == "perf":
                marks.append((float(p[-1]), " ".join(p[2:-1])))
            continue
        try:
            frames.append((float(p[0]), None if p[1] == "-" else float(p[1]), None if p[2] == "-" else float(p[2])))
        except (ValueError, IndexError):
            pass
    return marks, frames, clock


def windows(marks, frames):
    out = []
    for i, (t, n) in enumerate(marks):
        e = marks[i + 1][0] if i + 1 < len(marks) else float("inf")
        fs = [f for f in frames if t + 1.0 <= f[0] < e]   # (the first second after a word: the job starting)
        out.append((n, t, e, fs))
    return out


def stats(fs, budget, iv50):
    wk = [f[2] for f in fs if f[2] is not None]
    iv = [f[1] for f in fs if f[1] is not None]
    return {"n": len(wk), "mean": statistics.fmean(wk) if wk else float("nan"), "p50": pct(wk, 50), "p95": pct(wk, 95),
            "p99": pct(wk, 99), "max": max(wk) if wk else float("nan"), "over": sum(1 for v in wk if v > budget),
            "iv99": pct(iv, 99), "late": sum(1 for v in iv if v > 1.5 * iv50)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--budget", type=float, default=11.1)
    ap.add_argument("--lanelog", help="the watcher's log (default logs/lane/lane-*.log, all)")
    a = ap.parse_args()
    marks, frames, clock = load(a.run)
    if not marks:
        print("no `perf NAME` marks in %s/perf-raw.txt" % a.run)
        return 1
    iv50 = pct([f[1] for f in frames if f[1] is not None], 50)
    W = windows(marks, frames)
    rows = [(n, t, e, stats(fs, a.budget, iv50)) for n, t, e, fs in W if n not in ("end", "-")]
    quiet = [r for r in rows if r[0].startswith("none")]
    allq = [f for n, t, e, fs in W if n.startswith("none") for f in fs]
    qs = stats(allq, a.budget, iv50)
    print("run %s: %d frames, %d windows, clock %s, the present interval's median %.2f ms, budget %.1f ms" % (
        a.run, len(frames), len(rows), clock, iv50, a.budget))
    print("| window | frames | mean | p50 | p95 | p99 | max | over %.1f | over %% | p99 interval | late | vs quiet around: mean, p99, over %% |" % a.budget)
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for i, (n, t, e, s) in enumerate(rows):
        near = [r[3] for r in rows[max(0, i - 1):i + 2] if r[0].startswith("none") and r[0] != n]
        cmp_ = ""
        if not n.startswith("none") and near and s["n"]:
            m0 = statistics.fmean(x["mean"] for x in near)
            p0 = statistics.fmean(x["p99"] for x in near)
            o0 = statistics.fmean(100.0 * x["over"] / max(1, x["n"]) for x in near)
            cmp_ = "%+.2f ms (%+.1f %%), %+.2f ms, %+.1f pts" % (s["mean"] - m0, 100 * (s["mean"] - m0) / m0,
                                                                s["p99"] - p0, 100.0 * s["over"] / s["n"] - o0)
        print("| %s | %d | %.2f | %.2f | %.2f | %.2f | %.2f | %d | %.1f | %.2f | %d | %s |" % (
            n, s["n"], s["mean"], s["p50"], s["p95"], s["p99"], s["max"], s["over"],
            100.0 * s["over"] / max(1, s["n"]), s["iv99"], s["late"], cmp_))
    print("| all quiet windows | %d | %.2f | %.2f | %.2f | %.2f | %.2f | %d | %.1f | %.2f | %d | |" % (
        qs["n"], qs["mean"], qs["p50"], qs["p95"], qs["p99"], qs["max"], qs["over"], 100.0 * qs["over"] / max(1, qs["n"]),
        qs["iv99"], qs["late"]))
    # the quiet windows' own spread: what "no cost" can mean here
    if len(quiet) >= 2:
        ms = [r[3]["mean"] for r in quiet]
        ps = [r[3]["p99"] for r in quiet]
        os_ = [100.0 * r[3]["over"] / max(1, r[3]["n"]) for r in quiet]
        print("quiet windows' spread: mean %.2f..%.2f ms, p99 %.2f..%.2f ms, over %.1f..%.1f %%" % (
            min(ms), max(ms), min(ps), max(ps), min(os_), max(os_)))
    loop_report(a, W, frames, clock, qs)
    return 0


def loop_report(a, W, frames, clock, qs):
    lw = [w for w in W if w[0] == "loop-hog"]
    if not lw:
        return
    n, t0, e, fs = lw[0]
    onset = None
    try:
        for ln in open(os.path.join(a.run, "lane-loop-hog.log")):
            m = re.search(r"start mono=([0-9.]+) raw=([0-9.]+)", ln)
            if m:
                onset = float(m.group(2) if clock == "monotonic_raw" else m.group(1))
    except OSError:
        pass
    if onset is None:
        print("loop: no start line in lane-loop-hog.log")
        return
    key = "raw" if clock == "monotonic_raw" else "mono"
    ev = []
    for f in sorted(glob.glob(a.lanelog or "/home/bara/Projects/flat2vr/ShadowOfMordor/logs/lane/lane-*.log")):
        for ln in open(f):
            m = re.search(r" %s=([0-9.]+) (FREEZE|THAW) (.*)" % key, ln)
            if m:
                ev.append((float(m.group(1)), m.group(2), m.group(3)))
    ev = [x for x in ev if onset - 5 <= x[0] <= e + 5]
    # the baseline: the quiet window before the loop window
    idx = W.index(lw[0])
    prev = next((w for w in reversed(W[:idx]) if w[0].startswith("none")), None)
    bwk = [f[2] for f in (prev[3] if prev else []) if f[2] is not None]
    p50, p90, p99 = pct(bwk, 50), pct(bwk, 90), pct(bwk, 99)
    wf = [f for f in frames if f[2] is not None]
    ts = [f[0] for f in wf]
    print("\nclosed loop (window loop-hog; clock %s): the hog started at %.3f; the quiet window before it (%s): p50 %.2f "
          "p90 %.2f p99 %.2f ms" % (clock, onset, prev[0] if prev else "-", p50, p90, p99))
    fz = [x for x in ev if x[1] == "FREEZE" and x[0] >= onset]
    for x in ev:
        print("  %+7.3f s %s %s" % (x[0] - onset, x[1], x[2][:150]))
    if fz:
        f0 = fz[0][0]
        i0, i1 = bisect.bisect_left(ts, onset), bisect.bisect_right(ts, f0)
        before = wf[i0:i1]
        roll = []
        for j in range(i0, i1):
            w = [g[2] for g in wf[bisect.bisect_left(ts, ts[j] - 0.5):j + 1]]
            roll.append(statistics.median(w))
        print("first pause %.3f s after the hog started; %d frames in between: work %s ms; over p99: %d; the 0.5 s "
              "rolling median at most %.2f ms (p99 %.2f)" % (f0 - onset, len(before),
                                                             " ".join("%.2f" % g[2] for g in before[:30]),
                                                             sum(1 for g in before if g[2] > p99),
                                                             max(roll) if roll else float("nan"), p99))
        # the lane's share of the window: time not frozen between the onset and the window's end
        t, frozen, run_s = onset, False, 0.0
        end = min(e, onset + 45)
        for x in [y for y in ev if y[0] >= onset] + [(end, "END", "")]:
            if x[0] > end:
                x = (end, "END", "")
            if not frozen:
                run_s += x[0] - t
            t = x[0]
            frozen = x[1] == "FREEZE" or (frozen and x[1] == "END")
            if x[1] == "END":
                break
        print("the lane ran %.1f of %.1f s of the hog (%.0f %%): %d pauses" % (run_s, end - onset,
                                                                           100 * run_s / max(0.1, end - onset), len(fz)))
        inside = [f for f in wf if onset <= f[0] <= end]
        s = stats(inside, a.budget, pct([f[1] for f in frames if f[1] is not None], 50))
        print("the window's frames under the loop: mean %.2f p95 %.2f p99 %.2f max %.2f, over %.1f: %d of %d" % (
            s["mean"], s["p95"], s["p99"], s["max"], a.budget, s["over"], s["n"]))
    else:
        print("no pause after the hog started")


if __name__ == "__main__":
    sys.exit(main())
