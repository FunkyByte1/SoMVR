#!/usr/bin/env python3
"""live_tts.py - the live loop's spoken answer, played (docs/re-xr-pipeline.md 162.8; SOMVR_LIVE_TTS=on): one WAV
the daemon's Kokoro worker made (live_kokoro.py, in the lane) played into the headset at the card's calm moment.

  python3 -I live_tts.py --wav say/KEY.wav --out say/KEY.spoken [--sink NAME]

  1. the sink: --sink / LIVE_TTS_SINK (tests: a null sink the test made itself), else exactly `wivrn.sink` (WiVRn's
     sink, by that name only - never "the first sink like it", never the default); it must be listed by that exact
     name right before the line (a device that is not there would fall back to the default output - the user's
     speakers): not listed = not spoken
  2. the volume: like the game's voices - our stream at the game's own stream volume on that sink (its loudest
     channel; 100 % when no game stream is there), the WAV itself at a dialogue line's level (-20 dBFS RMS)
  3. the game ducked while the line plays: its streams on that sink (application.name ShadowOfMordor.exe /
     ShadowOfMordor) to DUCK (50 %) of their volume, put back after (also on any failure)
Writes OUT (what happened, one line). Exit 0 = spoken, 1 = not (why in OUT). Standard library only. The binaries:
LIVE_TTS_BIN (a folder searched first: the test's stand-ins).
"""
import argparse
import json
import os
import subprocess
import sys
import time

DUCK = 0.5
SINK = "wivrn.sink"
GAME_APPS = ("ShadowOfMordor.exe", "ShadowOfMordor")


def tool(name):
    b = os.environ.get("LIVE_TTS_BIN")
    if b and os.access(os.path.join(b, name), os.X_OK):
        return os.path.join(b, name)
    return name


def pactl_json(*args):
    r = subprocess.run([tool("pactl"), "-f", "json"] + list(args), capture_output=True, text=True, timeout=5)
    return json.loads(r.stdout) if r.returncode == 0 and r.stdout.strip() else []


def vol_pct(si):
    vol = si.get("volume", {})
    pct = [int(str(v.get("value_percent", "100%")).rstrip("%")) for v in vol.values()] or [100]
    return max(pct)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sink", default=os.environ.get("LIVE_TTS_SINK", "") or SINK)
    a = ap.parse_args()

    def done(ok, why):
        with open(a.out + ".tmp", "w") as f:
            f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {'spoken' if ok else 'not spoken'}: {why}\n")
        os.replace(a.out + ".tmp", a.out)
        return 0 if ok else 1

    if not os.path.isfile(a.wav):
        return done(False, f"no {a.wav}")
    sinks = pactl_json("list", "short", "sinks")
    idx = {str(s.get("index")) for s in sinks if s.get("name") == a.sink}
    if not idx:
        return done(False, f"the sink {a.sink} is not listed: never played elsewhere")
    games = [si for si in pactl_json("list", "sink-inputs")
             if si.get("properties", {}).get("application.name") in GAME_APPS and str(si.get("sink")) in idx]
    level = max([vol_pct(si) for si in games] or [100])
    ducked = []
    t0 = time.monotonic()
    try:
        for si in games:
            p = vol_pct(si)
            subprocess.run([tool("pactl"), "set-sink-input-volume", str(si.get("index")), f"{int(p * DUCK)}%"], timeout=5)
            ducked.append((str(si.get("index")), p))
        r = subprocess.run([tool("paplay"), f"--device={a.sink}", f"--volume={int(65536 * min(level, 150) / 100)}",
                            "--client-name=SoM VR live", a.wav], capture_output=True, text=True, timeout=90)
        played = r.returncode == 0
    finally:
        for i, p in ducked:
            subprocess.run([tool("pactl"), "set-sink-input-volume", i, f"{p}%"], timeout=5)
    return done(played, f"on {a.sink} at {level} % (the game's stream{'s' if len(games) != 1 else ''}: {len(games)}, "
                        f"ducked to {int(DUCK * 100)} % and back) in {time.monotonic() - t0:.1f} s"
                        f"{'' if played else ' - paplay failed: ' + r.stderr.strip()[:100]}")


if __name__ == "__main__":
    sys.exit(main())
