#!/usr/bin/env python3
"""ladder_look.py - som-b37-ladder (docs/re-xr-pipeline.md 172): a story ladder rung's README lines from a ladder boot's
`look` (scripts/ladder-drive.sh -> RUN/ladder-look<N>.txt).

  ladder_look.py RUN [N] [--readme RUNG_DIR --checked TEXT]      (N = the look's number, default 1)

Prints `open:` (the golden markers: the missions the save offers, with their places), `Talion:` (his place), `light:`
(the weather and the time of day now) and `unlocked:` (the owned skills by their records, from `beasts skills`).
--readme: those lines (and `checked:` = TEXT) replace the same lines of RUNG_DIR/README.txt.
"""
import re
import sys


def main():
    args = sys.argv[1:]
    readme = checked = None
    if "--readme" in args:
        i = args.index("--readme")
        readme = args[i + 1]
        del args[i:i + 2]
    if "--checked" in args:
        i = args.index("--checked")
        checked = args[i + 1]
        del args[i:i + 2]
    run = args[0]
    n = args[1] if len(args) > 1 else "1"
    txt = open(f"{run}/ladder-look{n}.txt", encoding="utf-8", errors="replace").read().splitlines()
    gold, seen = [], set()
    for ln in txt:
        m = re.match(r"PRACTICE nav \d+ '(golden[A-Za-z]*)' at \((-?\d+) (-?\d+) (-?\d+)\), ([0-9.]+) m", ln)
        if m and m.group(1, 2, 3, 4) not in seen:
            seen.add(m.group(1, 2, 3, 4))
            gold.append(f"{m.group(1)} {m.group(2)} {m.group(3)} {m.group(4)} ({m.group(5)} m)")
    pos = light = ""
    for ln in txt:
        m = re.search(r"Talion known at \((-?[0-9.]+) (-?[0-9.]+) (-?[0-9.]+)\)", ln)
        if m and not pos:
            pos = " ".join(str(round(float(v))) for v in m.groups())
        m = re.search(r"status light now: weather '([^']*)', time of day '([^']*)'", ln)
        if m and not light:
            light = f"weather '{m.group(1)}', time of day '{m.group(2)}'"
    owned, total = [], ""
    for ln in txt:
        if ln.startswith("BEASTS skills:"):
            owned += [k for k, v in re.findall(r" ([A-Za-z0-9_]+)=([01])", ln) if v == "1"]
        m = re.search(r"skills: (\d+) records, (\d+) owned", ln)
        if m:
            total = f"{m.group(2)} of {m.group(1)} skill records owned"
    out = {
        "open": "open: " + ("; ".join(gold) if gold else "no golden marker"),
        "Talion": "Talion: (" + (pos or "?") + ") cm (the save's own place: Continue loads him there)",
        "light": "light: " + (light or "?"),
        "unlocked": "unlocked: " + (total or "?") + (": " + " ".join(owned) if owned else ""),
    }
    if checked:
        out["checked"] = "checked: " + checked
    for v in out.values():
        print(v)
    if readme:
        p = f"{readme}/README.txt"
        lines = open(p, encoding="utf-8").read().splitlines()
        lines = [out.get(ln.split(":", 1)[0], ln) if ":" in ln else ln for ln in lines]
        open(p, "w", encoding="utf-8").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
