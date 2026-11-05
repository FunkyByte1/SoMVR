#!/usr/bin/env python3
"""lane_attrib.py - were a session's late frames the lane's? (docs/re-xr-pipeline.md 163.8, som-b37-live3; offline,
read only)

  python3 -I lane_attrib.py --raw RUN/perf-raw.txt --lane logs/lane/lane-YYYYMMDD.log [--live LIVE/live.log]
                            [--x 1.5,2.5] [--from HH:MM:SS --to HH:MM:SS]

Every frame of the session (perf-raw.txt: the render thread's present intervals, the game's MONOTONIC_RAW clock) gets
the lane's state at its time, from the lane log's FREEZE / THAW lines of the whole lane (their raw= clock is the same):
  frozen   the closed loop (or the VR gate) held the lane
  running  the lane thawed with jobs in it (its jobs MAY run; an idle speech worker is still "running" here)
  busy     running AND a speech job working: a transcription or a synthesis from live.log (its done line minus its
           seconds in the lane), or the session's load stretch (only with --live)
and is late when its interval is over X x the session's median interval (X = 1.5: a missed compositor slot; 2.5: two
in a row). The table: frames, late frames and their rate per state and X, and the seconds with a burst (>= 2 late frames
in that second: what the loop pauses on). The lane is plausibly the cause only if running/busy is clearly worse than
frozen. Standard library only.
"""
import argparse
import datetime as dt
import re
import statistics
import sys

LANE_RE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) mono=[\d.]+ raw=([\d.]+) (FREEZE|THAW) the lane \((\d+) jobs")
LIVE_RE = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d) (?:note \d+: .* -> ([\d.]+) s in the lane|tts: \S+: [\d.]+ s of "
                     r"speech synthesised in ([\d.]+) s in the lane)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", required=True)
    ap.add_argument("--lane", required=True)
    ap.add_argument("--live")
    ap.add_argument("--x", default="1.5,2.5")
    ap.add_argument("--from", dest="t_from")
    ap.add_argument("--to", dest="t_to")
    a = ap.parse_args()
    frames = []
    with open(a.raw) as f:
        for ln in f:
            s = ln.split()
            if len(s) >= 2 and s[0] != "#" and s[1] != "-":
                frames.append((float(s[0]), float(s[1])))
    if not frames:
        print("no frames")
        return 1
    # the lane's state changes, and the wall -> raw offset (each lane line has both)
    ev, off = [], []
    with open(a.lane) as f:
        for ln in f:
            m = LANE_RE.match(ln)
            if not m:
                continue
            wall = dt.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()
            raw = float(m.group(2))
            off.append(wall - raw)
            ev.append((raw, m.group(3) == "FREEZE", int(m.group(4))))
    off = statistics.median(off) if off else 0.0
    t0, t1 = frames[0][0], frames[-1][0]
    day = dt.datetime.fromtimestamp(t0 + off).strftime("%Y-%m-%d")

    def clip(hms, dflt):
        return dflt if not hms else dt.datetime.strptime(day + " " + hms, "%Y-%m-%d %H:%M:%S").timestamp() - off
    lo, hi = clip(a.t_from, t0), clip(a.t_to, t1)
    busy = []
    if a.live:
        with open(a.live) as f:
            for ln in f:
                m = LIVE_RE.match(ln)
                if m:
                    end = dt.datetime.strptime(m.group(1), "%Y-%m-%dT%H:%M:%S").timestamp() - off + 0.5
                    secs = float(m.group(2) or m.group(3))
                    busy.append((end - secs, end))
    ev.sort()
    xs = [float(x) for x in a.x.split(",")]
    med = statistics.median(iv for t, iv in frames if lo <= t <= hi)
    st = {}
    sec = {}
    k, frozen, jobs = 0, True, 0
    for t, iv in frames:
        while k < len(ev) and ev[k][0] <= t:
            frozen, jobs = ev[k][1], ev[k][2]
            k += 1
        if not lo <= t <= hi:
            continue
        state = "frozen" if frozen or not jobs else "running"
        names = [state]
        if state == "running" and any(b0 <= t <= b1 for b0, b1 in busy):
            names.append("busy")
        if state == "running" and a.live and "busy" not in names:
            names.append("running idle")
        for nm in names:
            e = st.setdefault(nm, {"n": 0, "late": [0] * len(xs), "secs": set(), "burst": [{} for _ in xs]})
            e["n"] += 1
            e["secs"].add(int(t))
            for i, x in enumerate(xs):
                if iv > x * med:
                    e["late"][i] += 1
                    e["burst"][i][int(t)] = e["burst"][i].get(int(t), 0) + 1
    print(f"frames {sum(1 for t, _ in frames if lo <= t <= hi)} from "
          f"{dt.datetime.fromtimestamp(lo + off):%H:%M:%S} to {dt.datetime.fromtimestamp(hi + off):%H:%M:%S}; "
          f"the median interval {med:.2f} ms ({1000 / med:.0f} Hz)")
    for nm in ("frozen", "running", "busy", "running idle"):
        e = st.get(nm)
        if not e:
            continue
        cols = []
        for i, x in enumerate(xs):
            nb = sum(1 for v in e["burst"][i].values() if v >= 2)
            cols.append(f"x{x:g}: {e['late'][i]} late = {100 * e['late'][i] / e['n']:.2f} %, bursts in "
                        f"{nb}/{len(e['secs'])} s = {100 * nb / max(1, len(e['secs'])):.1f} %")
        print(f"  {nm:13s} {e['n']:7d} frames ({len(e['secs'])} s): " + "; ".join(cols))
    return 0


if __name__ == "__main__":
    sys.exit(main())
