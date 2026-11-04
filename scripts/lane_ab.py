#!/usr/bin/env python3
"""lane_ab.py RUN [--budget MS] [--md] - the lane's A/B table (som-b37-lane2, docs/re-xr-pipeline.md 163.8) from a
lane37-drive.sh boot: per `perf NAME` window (idle-N, busy-N, old-1, idle-old, loop-1, idle-loop; gaps left out; the
first 2 s after each mark left out) -
  frames: the presents' intervals (what the headset gets) - p50, p99, p99.9, max, the share over the budget (11.1 ms =
          90 Hz) and the share missed (over 1.5 x the run's median interval: a compositor slot without a new frame); the
          render thread's frame work p50 / p99
  PSI:    /proc/pressure's cpu some, memory some/full, io some - the share of the window's time (RUN/lane-sample.txt)
  game:   its threads' wait for a CPU per second of their run time (schedstat), and of its two busiest threads (the
          render and game threads); the lane: its CPU (cores), throttled share, memory peak, memory.high events, swap
  WiVRn:  the stand-in's 90 Hz tick (RUN/wivrn-standin.txt): interval p99, ticks over 12 ms, work p99
then the decision rule: every busy window's interval p99 and missed share within the idle windows' noise (at most the
idle max + the idle spread, and the busy mean at most the idle max); the share over 11.1 ms and the max are shown, not
in the rule (at a 90 Hz compositor's pacing about half the intervals land a hair over 11.1 ms by jitter).
"""
import argparse
import os
import statistics
import sys


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    return s[min(len(s) - 1, max(0, int(round(q / 100.0 * (len(s) - 1)))))]


def load_raw(path):
    marks, frames = [], []
    for ln in open(path):
        p = ln.split()
        if not p:
            continue
        if p[0] == "#":
            if len(p) >= 4 and p[1] == "perf":
                try:
                    marks.append((float(p[-1]), " ".join(p[2:-1])))
                except ValueError:
                    pass
            continue
        try:
            frames.append((float(p[0]), None if p[1] == "-" else float(p[1]), None if p[2] == "-" else float(p[2])))
        except (ValueError, IndexError):
            pass
    return marks, frames


def load_sample(path):
    S, TH = [], []
    if not os.path.exists(path):
        return S, TH
    for ln in open(path):
        p = ln.split()
        if not p:
            continue
        try:
            if p[0] == "S":
                S.append([float(p[1]), float(p[2])] + [int(x) for x in p[3:]])
            elif p[0] == "TH":
                TH.append((float(p[1]), int(p[2]), p[3], int(p[4]), int(p[5])))
        except (ValueError, IndexError):
            pass
    return S, TH


def window_stats(fr, budget, miss):
    iv = [f[1] for f in fr if f[1] is not None]
    wk = [f[2] for f in fr if f[2] is not None]
    return {"n": len(iv), "p50": pct(iv, 50), "p99": pct(iv, 99), "p999": pct(iv, 99.9),
            "max": max(iv) if iv else float("nan"),
            "late": 100.0 * sum(1 for v in iv if v > budget) / len(iv) if iv else float("nan"),
            "miss": 100.0 * sum(1 for v in iv if v > miss) / len(iv) if iv else float("nan"),
            "w50": pct(wk, 50), "w99": pct(wk, 99)}


def sample_stats(S, TH, t0, t1):
    s = [x for x in S if t0 <= x[0] < t1]
    d = {}
    if len(s) < 2:
        return d
    a, b = s[0], s[-1]
    dt = (b[0] - a[0]) * 1e6   # us
    # S columns: 0 T_RAW 1 T_MONO 2 cpu_some 3 cpu_full 4 mem_some 5 mem_full 6 io_some 7 io_full 8 lane_cpu_us
    # 9 lane_thr_us 10 lane_mem 11 lane_high 12 lane_oom 13 swap_kb 14 pswpin 15 pswpout 16 game_run 17 game_wait 18 thr
    d["cpu"] = 100.0 * (b[2] - a[2]) / dt
    d["mem"] = 100.0 * (b[4] - a[4]) / dt
    d["memf"] = 100.0 * (b[5] - a[5]) / dt
    d["io"] = 100.0 * (b[6] - a[6]) / dt
    d["lane"] = (b[8] - a[8]) / dt
    d["thr"] = 100.0 * (b[9] - a[9]) / dt
    d["lmem"] = max(x[10] for x in s) / 2 ** 30
    d["high"] = b[11] - a[11]
    d["oom"] = b[12] - a[12]
    d["swap"] = (b[13] - a[13]) / 1024.0
    d["swpio"] = (b[14] - a[14]) + (b[15] - a[15])
    run, wait = b[16] - a[16], b[17] - a[17]
    d["gwait"] = 1000.0 * wait / run if run > 0 else float("nan")   # ms waiting per s on a CPU
    th = [x for x in TH if t0 <= x[0] < t1]
    first, last = {}, {}
    for x in th:
        first.setdefault(x[1], x)
        last[x[1]] = x
    dl = sorted(((last[t][3] - first[t][3], last[t][4] - first[t][4], last[t][2]) for t in last if t in first),
                reverse=True)[:2]
    d["top"] = ", ".join("%s %.1f" % (c[:12], 1000.0 * w / r) for r, w, c in dl if r > 0) or "-"
    return d


def standin(path, t0, t1):
    if not os.path.exists(path):
        return {}
    iv, wk = [], []
    for ln in open(path):
        p = ln.split()
        if not p or p[0] == "#":
            continue
        try:
            t = float(p[0])
        except ValueError:
            continue
        if t0 <= t < t1:
            iv.append(float(p[1]))
            wk.append(float(p[2]))
    if not iv:
        return {}
    return {"p99": pct(iv, 99), "over": sum(1 for v in iv if v > 12.0), "w99": pct(wk, 99), "n": len(iv)}


def fmt(v, f="%.2f"):
    return "-" if v is None or v != v else f % v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--budget", type=float, default=1000.0 / 90)
    ap.add_argument("--md", action="store_true")
    ap.add_argument("--trim", action="append", default=[],
                    help="NAME:SECONDS - leave out a window's first SECONDS (another job overlapped them)")
    a = ap.parse_args()
    marks, frames = load_raw(os.path.join(a.run, "perf-raw.txt"))
    allv = [f[1] for f in frames if f[1] is not None]
    miss = 1.5 * statistics.median(allv) if allv else 16.7   # a missed compositor slot: 1.5 x the run's median interval
    S, TH = load_sample(os.path.join(a.run, "lane-sample.txt"))
    rows = []
    for i, (t, n) in enumerate(marks):
        e = marks[i + 1][0] if i + 1 < len(marks) else float("inf")
        if n.startswith("gap") or n == "end":
            continue
        trim = max([2.0] + [float(x.split(":")[1]) for x in a.trim if x.split(":")[0] == n])
        t = t + trim - 2.0
        fr = [f for f in frames if t + 2.0 <= f[0] < e]
        if not fr:
            continue
        e = min(e, fr[-1][0] + 0.001)
        st = window_stats(fr, a.budget, miss)
        st.update({"name": n, "secs": e - t - 2.0})
        st["s"] = sample_stats(S, TH, t + 2.0, e)
        st["w"] = standin(os.path.join(a.run, "wivrn-standin.txt"), t + 2.0, e)
        rows.append(st)
    hdr = ["window", "s", "frames", "iv p50", "iv p99", "iv p99.9", "iv max", "over %.1f %%" % a.budget,
           "missed >%.1f %%" % miss, "work p50", "work p99",
           "PSI cpu %", "mem %", "mem full %", "io %", "game wait ms/s", "busiest 2 (ms/s)", "lane cores", "throttled %",
           "lane GB", "high ev", "swap MB", "swap io", "wivrn p99", "wivrn >12", "wivrn work p99"]
    out = []
    for r in rows:
        s, w = r["s"], r["w"]
        out.append([r["name"], "%.0f" % r["secs"], str(r["n"]), fmt(r["p50"]), fmt(r["p99"]), fmt(r["p999"]),
                    fmt(r["max"], "%.1f"), fmt(r["late"], "%.3f"), fmt(r["miss"], "%.3f"), fmt(r["w50"]), fmt(r["w99"]), fmt(s.get("cpu"), "%.1f"),
                    fmt(s.get("mem"), "%.2f"), fmt(s.get("memf"), "%.2f"), fmt(s.get("io"), "%.1f"),
                    fmt(s.get("gwait"), "%.1f"), s.get("top", "-"), fmt(s.get("lane"), "%.2f"),
                    fmt(s.get("thr"), "%.1f"), fmt(s.get("lmem"), "%.1f"), fmt(s.get("high"), "%d"),
                    fmt(s.get("swap"), "%.0f"), fmt(s.get("swpio"), "%d"), fmt(w.get("p99")), fmt(w.get("over"), "%d"),
                    fmt(w.get("w99"))])
    if a.md:
        print("| " + " | ".join(hdr) + " |")
        print("|" + "---|" * len(hdr))
        for o in out:
            print("| " + " | ".join(o) + " |")
    else:
        wd = [max(len(h), *(len(o[i]) for o in out)) if out else len(h) for i, h in enumerate(hdr)]
        print("  ".join(h.rjust(wd[i]) for i, h in enumerate(hdr)))
        for o in out:
            print("  ".join(v.rjust(wd[i]) for i, v in enumerate(o)))
    # the decision rule
    idle = [r for r in rows if r["name"].startswith("idle-") and r["name"][5:].isdigit()]
    if len([r for r in idle if r["secs"] >= 400]) >= 2:   # the A/B's own idle windows (the knobs' short ones apart)
        idle = [r for r in idle if r["secs"] >= 400]
    busy = [r for r in rows if r["name"].startswith("busy-")]
    if len(idle) >= 2 and not busy:   # (a knobs-only boot: every window against this boot's idle windows)
        lim = {}
        for key in ("p99", "miss"):
            iv = [r[key] for r in idle]
            lim[key] = max(iv) + (max(iv) - min(iv))
        print()
        print("idle windows: p99 %s, missed %s; limits p99 %.3f ms, missed %.3f %%" % (
            " ".join("%.3f" % r["p99"] for r in idle), " ".join("%.3f" % r["miss"] for r in idle), lim["p99"], lim["miss"]))
        for r in rows:
            if r in idle or r["name"] == "warm":
                continue
            print("  %-14s %4.0f s  p99 %.3f  missed %.3f %%  max %.1f  -> %s" % (
                r["name"], r["secs"], r["p99"], r["miss"], r["max"],
                "within" if r["p99"] <= lim["p99"] and r["miss"] <= lim["miss"] else "OVER"))
    if len(idle) >= 2 and busy:
        print()
        ok = True
        for key, label in (("p99", "interval p99 (ms)"), ("late", "share over %.1f ms (%%)" % a.budget),
                           ("miss", "share missed, over %.1f ms (%%)" % miss), ("max", "interval max (ms)")):
            iv = [r[key] for r in idle]
            lo, hi = min(iv), max(iv)
            lim = hi + (hi - lo)
            bv = [r[key] for r in busy]
            within = all(v <= lim for v in bv) and statistics.fmean(bv) <= hi
            if key in ("p99", "miss"):   # (over 11.1 ms: at a 90 Hz compositor's pacing half the intervals are a hair
                ok = ok and within      # over the period by jitter - shown, not in the rule)
            print("%-28s idle %s (spread %.3f) | busy %s (mean %.3f) | limit %.3f: %s%s" % (
                label, " ".join("%.3f" % v for v in iv), hi - lo, " ".join("%.3f" % v for v in bv),
                statistics.fmean(bv), lim, "within the noise" if within else "OVER the noise",
                "" if key in ("p99", "miss") else " (shown, not in the rule)"))
        knobs = [r for r in rows if r not in idle and r not in busy and r["name"] != "warm"]
        if knobs:
            lim = {}
            for key in ("p99", "miss"):
                iv = [r[key] for r in idle]
                lim[key] = max(iv) + (max(iv) - min(iv))
            print("other windows against the A/B's idle limits (p99 %.3f ms, missed %.3f %%):" % (lim["p99"], lim["miss"]))
            for r in knobs:
                print("  %-12s %4.0f s  p99 %.3f  missed %.3f %%  max %.1f  -> %s" % (
                    r["name"], r["secs"], r["p99"], r["miss"], r["max"],
                    "within" if r["p99"] <= lim["p99"] and r["miss"] <= lim["miss"] else "OVER"))
        print("decision: %s" % ("the busy windows are within the idle windows' noise - the lane may run during VR"
                                if ok else "the busy windows cost frames - not every job may run during VR"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
