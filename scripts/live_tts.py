#!/usr/bin/env python3
"""live_tts.py - the live loop's spoken answer, played (docs/re-xr-pipeline.md 162.8; SOMVR_LIVE_TTS=on): one WAV
the daemon's Kokoro worker made (live_kokoro.py, in the lane) played into the headset at the card's calm moment.

  python3 -I live_tts.py --wav say/KEY.wav --out say/KEY.spoken [--sink NAME]
  python3 -I live_tts.py --stream say/KEY.wav --out say/KEY.spoken     (b37 live3, 162.9: the line streamed - KEY.wav,
      then KEY.1.wav, KEY.2.wav ... as the worker writes them, into one pacat stream with no gap; the end: KEY.parts
      (the count, the daemon's) reached, or STREAM_WAIT_S without a new sentence)

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
import wave

DUCK = 0.5
SINK = "wivrn.sink"
GAME_APPS = ("ShadowOfMordor.exe", "ShadowOfMordor")
STREAM_WAIT_S = 30.0


def piece(base, i):
    return base if i == 0 else base[:-4] + f".{i}.wav"


def stream(a, level, sink):
    """the pieces into one pacat stream as they come; -> (played ok, pieces, why)"""
    with wave.open(a.stream) as w:
        rate, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
    if width != 2:
        return False, 0, "not 16-bit"
    p = subprocess.Popen([tool("pacat"), "--playback", f"--device={sink}", "--format=s16le", f"--rate={rate}",
                          f"--channels={ch}", f"--volume={int(65536 * min(level, 150) / 100)}",
                          "--client-name=SoM VR live", "--latency-msec=60"],
                         stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    n, last, why = 0, time.monotonic(), ""
    pf = a.stream[:-4] + ".parts"
    try:
        while True:
            path = piece(a.stream, n)
            if os.path.exists(path):
                with wave.open(path) as w:
                    if w.getframerate() != rate or w.getnchannels() != ch:
                        why = f"piece {n} at another rate: the rest dropped"
                        break
                    p.stdin.write(w.readframes(w.getnframes()))
                    p.stdin.flush()
                n, last = n + 1, time.monotonic()
                continue
            try:
                total = int(open(pf).read().strip() or 0)
            except (OSError, ValueError):
                total = None
            if total is not None and n >= total:
                break
            if time.monotonic() - last > STREAM_WAIT_S:
                why = f"no piece {n} for {STREAM_WAIT_S:.0f} s: the rest dropped"
                break
            time.sleep(0.02)
        p.stdin.close()
    except (BrokenPipeError, OSError) as e:
        why = f"pacat: {e}"
    try:
        rc = p.wait(timeout=120)
    except subprocess.TimeoutExpired:
        p.kill()
        rc = -1
    err = p.stderr.read().decode(errors="replace").strip()[:100] if p.stderr else ""
    return rc == 0 and not why.startswith("pacat"), n, why or (f"pacat failed: {err}" if rc else "")


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
    ap.add_argument("--wav")
    ap.add_argument("--stream", help="b37 live3: KEY.wav, the first piece of a streamed line")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sink", default=os.environ.get("LIVE_TTS_SINK", "") or SINK)
    a = ap.parse_args()

    def done(ok, why):
        with open(a.out + ".tmp", "w") as f:
            f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {'spoken' if ok else 'not spoken'}: {why}\n")
        os.replace(a.out + ".tmp", a.out)
        return 0 if ok else 1

    src = a.stream or a.wav
    if not src or not os.path.isfile(src):
        return done(False, f"no {src}")
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
        if a.stream:
            played, npc, swhy = stream(a, level, a.sink)
        else:
            r = subprocess.run([tool("paplay"), f"--device={a.sink}", f"--volume={int(65536 * min(level, 150) / 100)}",
                                "--client-name=SoM VR live", a.wav], capture_output=True, text=True, timeout=90)
            played, npc, swhy = r.returncode == 0, 1, "" if r.returncode == 0 else "paplay failed: " + r.stderr.strip()[:100]
    finally:
        for i, p in ducked:
            subprocess.run([tool("pactl"), "set-sink-input-volume", i, f"{p}%"], timeout=5)
    return done(played, f"on {a.sink} at {level} % (the game's stream{'s' if len(games) != 1 else ''}: {len(games)}, "
                        f"ducked to {int(DUCK * 100)} % and back) in {time.monotonic() - t0:.1f} s"
                        + (f", {npc} sentence{'s' if npc != 1 else ''} streamed" if a.stream else "")
                        + (f" - {swhy}" if swhy else ""))


if __name__ == "__main__":
    sys.exit(main())
