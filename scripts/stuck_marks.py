#!/usr/bin/env python3
"""stuck_marks.py - the user's stuck / no-way marks and why the climb refused there (build 40, som-b40-ledge).

  stuck_marks.py RUN_DIR [RUN_DIR ...] [--all] [--window S]

For each run (logs/win-session-*): every mark (marks/<id>/state.txt's t=, voice.txt or voice-live.txt) whose words say
stuck / can't / nothing happens / floating / no way (--all: every mark), the climb's and the walk's refusal lines in the
S seconds (default 25) before it, classified by the code's own reason texts (the same words somvr.c logs), and where
Talion was. Prints one row a mark and a count of the reasons over all of them.
"""
import os
import re
import sys

WORDS = re.compile(r"stuck|can't|cannot|can not|won't|nothing happen|no way|float|clip|not able|unable|doesn't let",
                   re.I)
REASONS = [  # (code, pattern) - the first that matches a line
    ("PO_NOFLOOR", r"pullover: a hold on a top's edge, no landing: no floor"),
    ("PO_NOROOM", r"pullover: a hold on a top's edge, no landing: a floor on the top, no room"),
    ("AM_NOFLOOR", r"mantle: A at the top, no way onto it: no floor"),
    ("AM_NOROOM", r"mantle: A at the top, no way onto it: no room"),
    ("AM_NOWAY", r"mantle: A at the top, no way onto it"),
    ("DN_NOEDGE", r"down: a grip reaching down .*no edge within reach"),
    ("DN_NOFACE", r"down: .*(no face|a slope or a ledge)"),
    ("DN_OTHER", r"down: a grip reaching down .*no way over the edge"),
    ("FA_NODOWN", r"climb down \(the stick back \+ A\): no way down by hand"),
    ("MISS", r"press [LR]: nothing climbable within reach"),
    ("NOT_CLIMBABLE", r"press [LR]: not climbable"),
    ("LOW_HOLD", r"no climb from the ground"),
    ("TOPPED_HANG", r"hang \(topped"),
    ("HANG_NOHOLD", r"hang: no hand holds"),
    ("PO_NOWAY", r"no way up here"),
    ("THR_REFUSED", r"through.*(refused|not clear)"),
    ("ROCK_GUARD", r"rock guard"),
    ("DM_NOLAND", r"drop mark: no safe landing"),
    ("FOOT_NOEDGE", r"A on foot: no blink: no edge ahead"),
    ("JUMP_REFUSED", r"^JUMP t=.*refused"),
    ("WALK_HELD", r"^WALK edge stop t=.*held at a drop"),
    ("HANG_FREE", r"hang free: .*released"),
]
REASONS = [(c, re.compile(p)) for c, p in REASONS]


def mark_rows(run, window, every):
    log = os.path.join(run, "somvr.log")
    md = os.path.join(run, "marks")
    if not os.path.isfile(log) or not os.path.isdir(md):
        return []
    marks = []
    for m in sorted(os.listdir(md)):
        st = os.path.join(md, m, "state.txt")
        if not os.path.isfile(st):
            continue
        txt = open(st, errors="replace").read(4000)
        t = re.search(r"\bt=([0-9.]+)", txt)
        root = re.search(r"talion root ([-0-9.]+) ([-0-9.]+) ([-0-9.]+)", txt)
        words = ""
        for vf in ("voice-live.txt", "voice.txt"):
            p = os.path.join(md, m, vf)
            if os.path.isfile(p):
                for ln in open(p, errors="replace").read().split("\n"):
                    ln = ln.strip()
                    if ln and not re.match(r"^[0-9.]+ s from ", ln):
                        words = ln
                        break
                if words:
                    break
        if not t or (not every and not WORDS.search(words)):
            continue
        marks.append((m, float(t.group(1)), root.groups() if root else ("?", "?", "?"), words))
    if not marks:
        return []
    lines = []
    tre = re.compile(r"\bt=([0-9]+\.[0-9]+)")
    with open(log, errors="replace") as f:
        for ln in f:
            if not (ln.startswith("CLIMB t=") or ln.startswith("JUMP t=") or ln.startswith("WALK ")):
                continue
            mt = tre.search(ln)
            if mt:
                lines.append((float(mt.group(1)), ln.rstrip()))
    rows = []
    for m, t, root, words in marks:
        seen = {}
        for lt, ln in lines:
            if t - window <= lt <= t + 1.0:
                for code, rx in REASONS:
                    if rx.search(ln):
                        seen[code] = seen.get(code, 0) + 1
                        break
        rows.append((os.path.basename(run), m, t, root, words, seen))
    return rows


def main(argv):
    every = "--all" in argv
    window = 25.0
    if "--window" in argv:
        window = float(argv[argv.index("--window") + 1])
    runs = [a for a in argv if os.path.isdir(a)]
    total, per = {}, {}
    for run in runs:
        for run_n, m, t, root, words, seen in mark_rows(run, window, every):
            top = sorted(seen.items(), key=lambda kv: -kv[1])
            print(f"{run_n} {m} t={t:.1f} at {root[0]},{root[1]},{root[2]}: "
                  f"{' '.join(f'{c}x{n}' for c, n in top) or '-'} | {words[:110]}")
            for c, n in seen.items():
                total[c] = total.get(c, 0) + n
                per[c] = per.get(c, 0) + 1
    print("\nreasons over all the marks (marks with it / lines):")
    for c, n in sorted(per.items(), key=lambda kv: -kv[1]):
        print(f"  {c:14s} {n:3d} marks  {total[c]:4d} lines")


if __name__ == "__main__":
    main(sys.argv[1:])
