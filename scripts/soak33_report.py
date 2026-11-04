#!/usr/bin/env python3
"""soak33_report.py RUN - the Windows soak's numbers (som-b33-soak, docs/re-winport.md 7.13) from scripts/soak33-drive.sh's
RUN/soak-samples.tsv, soak-steps.txt, soak-handles.txt, somvr.log and xr-layer.log: soak29_report.py's report (the growth
lines, per item, the items' RSS, the modules' log budgets, the root-node thrash), then the Windows build's columns (the
game's own VRAM/GTT, the wineserver, the Windows handles and threads, the DXVK logs, the DLL's crash notes) the same way,
the handle types' first/last counts, and every log's liveness: per line kind (somvr.log's first word, the layer's word
after `xr:`), its lines in each 10 minutes of the session by the logs' own clock (t=, the layer's stamp) and the minute of
its last line - a kind with lines all along that stops before the end = a subsystem gone quiet (or its budget spent).
Reads only."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import soak27_report as s27  # noqa: E402
import soak29_report as s29  # noqa: E402

WIN_COLS = ("game_vram_mb", "game_gtt_mb", "wsrv_rss_mb", "wsrv_fds", "win_handles", "win_threads", "dxvk_kb", "crash_notes")


def growth(run, warm):
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(run, "soak-samples.tsv"))]
    head, rows = rows[0], [r for r in rows[1:] if len(r) == len(rows[0])]
    col = {h: i for i, h in enumerate(head)}
    mins = [s27.num(r[col["min"]]) for r in rows]
    print("the Windows build's columns (the fit from minute %.0f):" % warm)
    for h in WIN_COLS:
        if h not in col:
            continue
        pts = [(m, s27.num(r[col[h]])) for m, r in zip(mins, rows) if m is not None and m >= warm and s27.num(r[col[h]]) is not None]
        allv = [s27.num(r[col[h]]) for r in rows if s27.num(r[col[h]]) is not None]
        if not allv:
            print(f"  {h:17s} no values")
            continue
        s = s27.fit([p[0] for p in pts], [p[1] for p in pts]) if pts else None
        per_h = "" if s is None else f"; {s * 60:+.1f}/h, at 4 h {pts[-1][1] + s * (240 - pts[-1][0]):.0f}"
        print(f"  {h:17s} first {allv[0]:g} last {allv[-1]:g} min {min(allv):g} max {max(allv):g}{per_h}")


def handles(run):
    p = os.path.join(run, "soak-handles.txt")
    if not os.path.exists(p):
        return
    rx = re.compile(r"types=(\S*)")
    first = last = None
    peak = {}
    for l in open(p):
        m = rx.search(l)
        # (soak 1's first 26 minutes: the counter's first build named the types two places off - "DebugObject" in a game)
        if not m or "DebugObject:" in l:
            continue
        d = {}
        for kv in m.group(1).split(","):
            if ":" in kv:
                k, v = kv.rsplit(":", 1)
                d[k] = int(v)
                peak[k] = max(peak.get(k, 0), int(v))
        first = first or d
        last = d
    if first is None:
        return
    print("the game's Windows handles by type (the first named sample -> the last, the peak):")
    for k in sorted(set(first) | set(last), key=lambda k: -(last.get(k, 0))):
        a, b = first.get(k, 0), last.get(k, 0)
        print(f"  {k:16s} {a:6d} -> {b:6d} ({b - a:+d}), peak {peak.get(k, 0)}")


def liveness(path, kind_rx, time_rx, label, bucket_min=10):
    """per line kind: the lines in each bucket of the session (by the log's own clock) and its last minute"""
    if not os.path.exists(path):
        return
    t0 = tl = None
    kinds = {}
    for l in open(path, errors="replace"):
        m = time_rx.search(l)
        if m:
            t = float(m.group(1))
            if t0 is None:
                t0 = t
            if t >= (tl or t):
                tl = t
        if tl is None:
            continue
        k = kind_rx(l)
        if not k:
            continue
        mi = (tl - t0) / 60.0
        e = kinds.setdefault(k, {"n": 0, "b": {}, "first": mi, "last": mi})
        e["n"] += 1
        e["b"][int(mi // bucket_min)] = e["b"].get(int(mi // bucket_min), 0) + 1
        e["last"] = mi
    if t0 is None:
        return
    end = (tl - t0) / 60.0
    nb = int(end // bucket_min) + 1
    print(f"{label}: {end:.0f} minutes by its clock; lines per {bucket_min} min for every kind with 20+ lines "
          f"(QUIET = lines in most buckets before, none in the last {bucket_min} min):")
    for k, e in sorted(kinds.items(), key=lambda kv: -kv[1]["n"]):
        if e["n"] < 20:
            continue
        cells = [e["b"].get(i, 0) for i in range(nb)]
        before = cells[:-1]
        quiet = len(before) >= 3 and sum(1 for c in before if c) >= 0.6 * len(before) and cells[-1] == 0
        flag = "  <- QUIET" if quiet else ""
        print(f"  {k[:22]:22s} {e['n']:7d}  last at min {e['last']:5.1f}  " + " ".join(f"{c:5d}" for c in cells) + flag)


def somvr_kind(l):
    m = re.match(r"(?:WARN )?([A-Z][A-Z0-9_]+)\b", l)
    return m.group(1) if m else None


def layer_kind(l):
    m = re.search(r"\] xr: ([a-z][a-z0-9 _-]{0,20}?)(?::|\s\d|\s\(|$)", l)
    return m.group(1) if m else None


def main():
    run = sys.argv[1]
    warm = float(os.environ.get("SK_WARM", "8"))
    s29.main()
    s29.thrash(run)
    growth(run, warm)
    handles(run)
    liveness(os.path.join(run, "somvr.log"), somvr_kind, re.compile(r"\bt=([0-9]+\.[0-9]+)"), "somvr.log")
    liveness(os.path.join(run, "xr-layer.log"), layer_kind, re.compile(r"^([0-9]+\.[0-9]+) \["), "xr-layer.log")
    sm = os.path.join(run, "summary.txt")
    if os.path.exists(sm):
        print("the harness:")
        for l in open(sm):
            if re.search(r"save after|crash|exited by itself|stopping the game|program exit|hold .* over|QUIT", l):
                print("  " + l.strip()[:200])


if __name__ == "__main__":
    main()
