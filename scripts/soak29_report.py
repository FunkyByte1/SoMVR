#!/usr/bin/env python3
"""soak29_report.py RUN - the second soak's numbers (build 29, docs/re-xr-pipeline.md 129) from scripts/soak29-drive.sh's
RUN/soak-samples.tsv, soak-steps.txt and somvr.log: soak27_report.py's growth lines (per column the first/last value and a
least-squares line after the warm-up, at 4 hours), then per item (the sampler's item column) the means of the game
thread's busy time, our share of it, our peeks a frame, the render thread's frame work and the GPU (the PERF windows
sampled while it ran), the items' RSS steps, and the modules' log budgets: each prefix's lines against its cap and the
time of its last line (a module silent long before the end = its budget spent). Reads only."""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import soak27_report as s27  # noqa: E402

# (prefix regex, the cap after which the module was silent before build 29 - None = paced, not capped)
BUDGETS = [("VERBS ", 1500), ("FXCENSUS|FXSEEN", 3000), ("EXECUTE streak", 400), ("RIDE ", 800), ("STRUGGLE ", 400),
           ("MAP ", 300), ("USE ", 300), ("ABIL ", None), ("ABIL2 ", None), ("ABIL3 ", None), ("OBJECTIVE ", 400),
           ("BOW headkill", 200), ("PROMPTS ", None), ("STEALTH ", None), ("CAPTAIN ", None), ("BEASTS ", None),
           ("HINTS ", None), ("CLIMB ", None), ("PERF ", None), ("FXNEAR ", None)]


def main():
    run = sys.argv[1]
    s27.main()
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(run, "soak-samples.tsv"))]
    head, rows = rows[0], [r for r in rows[1:] if len(r) == len(rows[0])]
    col = {h: i for i, h in enumerate(head)}
    items = {}
    for r in rows:
        items.setdefault(r[col["item"]], []).append(r)
    print("per item (the samples while it ran: mean of the last 10-s PERF window at each; ms):")
    print(f"  {'item':9s} {'n':>3s} {'busy':>6s} {'p95':>6s} {'ours':>5s} {'peeks':>7s} {'render':>7s} {'p95':>6s} {'gpu':>6s}")
    for it, rs in items.items():
        def mean(h):
            v = [s27.num(r[col[h]]) for r in rs if s27.num(r[col[h]]) is not None]
            return f"{sum(v) / len(v):.2f}" if v else "-"
        print(f"  {it:9s} {len(rs):3d} {mean('busy_ms'):>6s} {mean('busy_p95'):>6s} {mean('ours_ms'):>5s} {mean('peeks'):>7s}"
              f" {mean('render_ms'):>7s} {mean('render_p95'):>6s} {mean('gpu_ms'):>6s}")
    st = os.path.join(run, "soak-steps.txt")
    if os.path.exists(st):
        print("the items' RSS (MB, before -> after) and their root-node lines:")
        for l in open(st):
            m = re.match(r"(\S+) (\w+): exit (\S+), (\d+) s, RSS (\S+) -> (\S+) MB, WARN (\S+), (\d+) log lines, root-node lines \+(\d+)", l)
            if m:
                t, it, rc, s, a, b, w, nl, vr = m.groups()
                d = f"{int(b) - int(a):+d}" if a.isdigit() and b.isdigit() else "?"
                print(f"  {t} {it:9s} {s:>4s} s  {a:>5s} -> {b:>5s} ({d:>4s})  WARN {w}  {nl:>6s} lines  root-node +{vr}")
    log = os.path.join(run, "somvr.log")
    print("the modules' log budgets (lines; the last line's place in the log):")
    total = int(subprocess.run(["wc", "-l", log], capture_output=True, text=True).stdout.split()[0])
    for pre, cap in BUDGETS:
        out = subprocess.run(["grep", "-nE", f"^({pre})", log], capture_output=True, text=True).stdout.splitlines()
        n = len(out)
        last = int(out[-1].split(":", 1)[0]) if out else 0
        flag = "" if cap is None or n < cap else "  <- the budget reached"
        print(f"  {pre:18s} {n:6d}" + (f" of {cap}" if cap else "        ") + f"  last at {100.0 * last / max(total, 1):5.1f} %{flag}")


def thrash(run):
    """the PERF windows by the VERBS root-node lines inside them (a read of the 105 root nodes each): the game thread's
    busy time, our share, our peeks a frame - windows with none against windows with 10 or more"""
    import re as _re
    rx = _re.compile(r"^PERF t=\S+ s=\S+ frames=(\d+) .*game thread: busy ([0-9.]+)/.*\(over: \d+\); ours ([0-9.]+)/.*"
                     r"our peeks a frame \(process_vm_readv, the game thread\) ([0-9.]+)/")
    n, groups = 0, {"0": [], "1-9": [], "10+": []}
    for l in open(os.path.join(run, "somvr.log"), errors="replace"):
        if l.startswith("VERBS the root node"):
            n += 1
        elif l.startswith("PERF t="):
            m = rx.match(l)
            if m:
                fr, busy, ours, pk = int(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4))
                groups["0" if n == 0 else "1-9" if n < 10 else "10+"].append((busy, ours, pk, n / max(fr, 1)))
            n = 0
    print("the PERF windows by the root-node reads in them (mean busy / ours ms, peeks a frame, reads a frame):")
    for k, v in groups.items():
        if v:
            print(f"  {k:5s} {len(v):4d} windows: busy {sum(x[0] for x in v) / len(v):.2f}, ours {sum(x[1] for x in v) / len(v):.2f},"
                  f" peeks {sum(x[2] for x in v) / len(v):.0f}, reads {sum(x[3] for x in v) / len(v):.2f} a frame")


if __name__ == "__main__":
    main()
    thrash(sys.argv[1])
