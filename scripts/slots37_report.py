#!/usr/bin/env python3
"""slots37_report.py OUT [OUT...] - the tables of scripts/slots37-measure.sh (som-b37-slotsvram, re-xr-pipeline.md 158.5).

Per wave: its games (driver:mode), the window when all were in the world (the last `PRACTICE ... arrived` + 20 s to the
first boot's end), the card's VRAM and GTT in it (mean / peak, MiB) and the free VRAM left at the peak; per game: its own
VRAM resident / asked and GTT resident (peak, MiB, /proc fdinfo) over its time in the world. Per driver: its PASS/FAIL
lines at light against the same driver's vr boots (the text without numbers)."""
import os, re, sys
from collections import defaultdict

NUM = re.compile(r"-?\d+(?:\.\d+)?")
TOTAL = 16368


def rd(p, d=""):
    try:
        with open(p, errors="replace") as f:
            return f.read()
    except OSError:
        return d


def tsv(p):
    rows = [l.split("\t") for l in rd(p).splitlines()[1:] if l.strip()]
    return rows


def verdicts(txt):
    out = []
    for l in txt.splitlines():
        m = re.search(r"\b(PASS|FAIL)\b (.*)", l)
        if m:
            out.append((m.group(1), NUM.sub("#", re.sub(r"\(.*", "", m.group(2))).strip()))
    return out


def main(outs):
    waves, games = [], []
    for out in outs:
        S = [(int(r[0]), int(r[1]), int(r[2])) for r in tsv(os.path.join(out, "samples.tsv"))]
        G = defaultdict(list)
        for r in tsv(os.path.join(out, "games.tsv")):
            if len(r) >= 7:
                G[r[2]].append((int(r[0]), int(r[3]), int(r[4]), int(r[5]), int(r[6])))
        for l in rd(os.path.join(out, "waves.txt")).splitlines():
            e, w, spec = l.split(" ", 2)
            if spec == "end":
                continue
            items = []
            for s, sp in enumerate(spec.split(","), 1):
                tag = f"{w}-{s}"
                run = rd(os.path.join(out, f"run-{tag}.txt")).strip()
                t = rd(os.path.join(out, f"times-{tag}.txt")).split()
                arr = float(t[1]) if len(t) > 1 and t[1] != "-" else None
                end = float(t[2]) if len(t) > 2 else None
                g = [x for x in G.get(os.path.basename(run), []) if arr and end and arr + 20 <= x[0] <= end - 5]
                pk = lambda i: max((x[i] for x in g), default=0)
                d = rd(os.path.join(out, f"drive-{tag}.txt"))
                mode = sp.split(":")[1] if ":" in sp else "?"
                lset = re.search(r"render\.cfg: WIN_RENDER=\S+ \(.*?\) - .*?(?:, then the light set (\S+))? \(put back",
                                 rd(os.path.join(run, "summary.txt")))
                items.append(dict(tag=tag, spec=sp, drv=sp.split(":")[0], mode=mode, run=run, arr=arr, end=end,
                                  rv=pk(1), tv=pk(2), rg=pk(3), tg=pk(4), n=len(g), v=verdicts(d),
                                  lset=(lset.group(1) if lset and lset.group(1) else ""),
                                  save="UNCHANGED" in rd(os.path.join(run, "summary.txt"))))
            arrs = [i["arr"] for i in items if i["arr"]]
            ends = [i["end"] for i in items if i["end"]]
            lo = max(arrs) + 20 if len(arrs) == len(items) else None
            hi = min(ends) - 5 if ends else None
            win = [x for x in S if lo and hi and lo <= x[0] <= hi]
            vm = sum(x[1] for x in win) / len(win) if win else 0
            gm = sum(x[2] for x in win) / len(win) if win else 0
            waves.append(dict(out=out, w=w, spec=spec, items=items, n=len(win), vmean=vm, vpk=max((x[1] for x in win), default=0),
                              gmean=gm, gpk=max((x[2] for x in win), default=0)))
            games += items
    print("| Wave | Games (slot order) | All in the world | VRAM used mean / peak (MiB) | Free at the peak | GTT mean / peak (MiB) |")
    print("|---|---|---|---|---|---|")
    for w in waves:
        print(f"| {os.path.basename(w['out'])} {w['w']} | {w['spec']} | {w['n'] * 2} s | {w['vmean']:.0f} / {w['vpk']} | "
              f"{TOTAL - w['vpk']} | {w['gmean']:.0f} / {w['gpk']} |")
    print()
    print("| Game | Driver:mode (light set) | Its VRAM resident / asked peak (MiB) | Its GTT resident peak | Samples | PASS / FAIL | Save |")
    print("|---|---|---|---|---|---|---|")
    for g in games:
        p = sum(1 for v in g["v"] if v[0] == "PASS"); f = sum(1 for v in g["v"] if v[0] == "FAIL")
        print(f"| {os.path.basename(g['run'])} ({g['tag']}) | {g['spec'].split(':')[0]}:{g['mode']}{' (' + g['lset'] + ')' if g['lset'] else ''} | "
              f"{g['rv']} / {g['tv']} | {g['rg']} | {g['n']} | {p} / {f} | {'UNCHANGED' if g['save'] else '?'} |")
    print()
    by = defaultdict(lambda: defaultdict(list))
    for g in games:
        if g["v"]:
            by[g["drv"]][g["mode"]].append(g)
    for drv, modes in by.items():
        ref = set(v for g in modes.get("vr", []) for v in g["v"])
        for mode, gs in modes.items():
            for g in gs:
                diff = [v for v in g["v"] if v not in ref]
                print(f"{drv} {mode} {os.path.basename(g['run'])}: {len(g['v'])} verdicts; not in any vr boot: "
                      + ("; ".join(f"{a} {b[:110]}" for a, b in diff) if diff else "none"))


if __name__ == "__main__":
    main(sys.argv[1:])
