#!/usr/bin/env python3
"""soak27_report.py RUN - the soak's numbers (build 27, docs/re-xr-pipeline.md 107) from RUN/soak-samples.tsv
(scripts/soak27-drive.sh's sampler): per column the first and last value, the growth per hour by a least-squares line
over the samples after the warm-up (SK_WARM minutes, default 8: the load into the world and the first practice trip
settle), and that line extrapolated to 4 hours; the stereo frame rate's first/last 10 minutes and its worst minute; the
cycles' state lines (RUN/soak-state.txt) that are not back to normal. Reads only."""
import os
import sys


def num(v):
    try:
        return float(v)
    except ValueError:
        return None


def fit(xs, ys):
    n = len(xs)
    if n < 3:
        return None
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx  # per minute


def main():
    run = sys.argv[1]
    warm = float(os.environ.get("SK_WARM", "8"))
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(run, "soak-samples.tsv"))]
    head, rows = rows[0], [r for r in rows[1:] if len(r) == len(rows[0])]
    col = {h: i for i, h in enumerate(head)}
    mins = [num(r[col["min"]]) for r in rows]
    print(f"{len(rows)} samples, minutes {mins[0]:.0f}..{mins[-1]:.0f} (the fit from minute {warm:.0f})")
    for h in ("rss_mb", "vsz_mb", "threads", "fds", "cpu_cores", "monado_rss_mb", "gamescope_rss_mb", "vram_mb",
              "warn_somvr", "warn_layer", "gaps", "somvr_kb", "layer_kb", "run_mb"):
        pts = [(m, num(r[col[h]])) for m, r in zip(mins, rows) if m is not None and m >= warm and num(r[col[h]]) is not None]
        allv = [num(r[col[h]]) for r in rows if num(r[col[h]]) is not None]
        if not allv:
            print(f"  {h:17s} no values")
            continue
        s = fit([p[0] for p in pts], [p[1] for p in pts]) if pts else None
        per_h = "" if s is None else f"; {s * 60:+.1f}/h, at 4 h {pts[-1][1] + s * (240 - pts[-1][0]):.0f}"
        print(f"  {h:17s} first {allv[0]:g} last {allv[-1]:g} min {min(allv):g} max {max(allv):g}{per_h}")
    fps = [(m, num(r[col["stereo_fps"]])) for m, r in zip(mins, rows) if num(r[col["stereo_fps"]]) is not None]
    if fps:
        early = [v for m, v in fps if m < fps[0][0] + 10]
        late = [v for m, v in fps if m > fps[-1][0] - 10]
        worst = min(fps, key=lambda p: p[1])
        s = fit([m for m, _ in fps if m >= warm], [v for m, v in fps if m >= warm])
        print(f"  stereo fps: the first 10 min {sum(early) / len(early):.1f}, the last 10 min {sum(late) / len(late):.1f};"
              f" the worst minute {worst[1]:.1f} at minute {worst[0]:.0f}" + ("" if s is None else f"; the trend {s * 60:+.2f} fps/h"))
    errs = [abs(num(r[col["disp_err_ms"]])) for r in rows if num(r[col["disp_err_ms"]]) is not None]
    if errs:
        print(f"  |display-time error| at the samples: max {max(errs):.2f} ms, median {sorted(errs)[len(errs) // 2]:.2f} ms")
    st = os.path.join(run, "soak-state.txt")
    if os.path.exists(st):
        cyc, bad = None, []
        for l in open(st):
            if l.startswith("== "):
                cyc = l.strip()
                continue
            odd = (l.startswith("SESSION ui") and ("ctx=world" not in l or "paused=0" not in l)) or \
                  ("xr: comfort frame" in l and "amount 0.000" not in l) or \
                  ("xr: vrmenu:" in l and "opened" in l) or \
                  (l.startswith("BOW") and " enter " in l) or \
                  (l.startswith("CLIMB state") and " climbing" in l and "not climbing" not in l)
            if odd:
                bad.append(f"{cyc}: {l.strip()[:160]}")
        print(f"  state checks: {len(bad)} not back to normal" + ("".join("\n    " + b for b in bad[:20])))


if __name__ == "__main__":
    main()
