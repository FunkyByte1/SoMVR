#!/usr/bin/env python3
"""voice_rec.py - a bug mark's voice note, recorded until the player stops talking (build 21, docs/re-xr-pipeline.md 82;
boot_check.sh's mark_serve runs it with SOMVR_MARK_VOICE=talk, the default).

  python3 -I voice_rec.py --src SOURCE --out DIR/voice.wav [--txt DIR/voice.txt] [--err DIR/voice.err] [--max 30]
                          [--quiet 2]
  python3 -I voice_rec.py --analyze FILE.wav...     the stop rule over recorded notes (offline): where each would end

Records the PipeWire / PulseAudio source SOURCE (parec: 48 kHz mono s16le) into a WAV whose header is right after
every write (a kill leaves a playable file) and ends:
  - --quiet (QUIET_S, 2) s after the last speech;
  - at FIRST_S (5) s when nothing has been said by then;
  - at --max s (30, at most 30);
  - at once on SIGTERM / SIGINT (boot_check: the next mark - notes never overlap);
  - when the source ends (parec gone).
Speech = a 50 ms frame whose level (dBFS) is at least OVER_DB (15) over the noise floor and at least MIN_DB (-40);
the noise floor = the lowest 250 ms level (five frames' mean power) of the last 5 s, taken as at most FLOOR_CAP_DB
(-50): a note that starts mid-sentence has no quiet yet, and its own speech must not count as the floor (so the bar
is -40 .. -35 dBFS: a loud room raises it to -35, never higher; louder than that the note runs to --max - longer,
never cut). The build 20c session's 22 notes (logs/boot-check-20261028-193826): speech at -14 .. -35 dBFS (its
syllables' tails to -40), the room and the game at -42 .. -70, the Quest's gated silence -90; a pause inside a
sentence up to about 1 s.
Prints one line (and writes it to --txt): "12.4 s from SOURCE (talk 0.3 .. 10.4 s; ended: 2 s quiet)".
Standard library only (boot_check runs it with the system python3 -I).
"""
import argparse
import array
import math
import os
import select
import signal
import subprocess
import sys
import time
import wave

RATE = 48000
FRAME = RATE // 20          # 50 ms
FLOOR_WIN = 100             # frames: the floor's 5 s
SMOOTH = 5                  # frames: its 250 ms levels
OVER_DB = 15.0
MIN_DB = -40.0
FLOOR_CAP_DB = -50.0
QUIET_S = 2.0
FIRST_S = 5.0
MAX_S = 30.0


def level_db(samples):
    """the frame's mean power in dBFS (-120 for digital silence)"""
    if not samples:
        return -120.0
    p = sum(x * x for x in samples) / (len(samples) * 32768.0 * 32768.0)
    return 10.0 * math.log10(p) if p > 1e-12 else -120.0


class StopRule:
    """The note's end, fed 50 ms frames (their levels): end() = None while it goes on, else why it ended."""

    def __init__(self, max_s=MAX_S, quiet_s=QUIET_S, first_s=FIRST_S, over_db=OVER_DB, min_db=MIN_DB):
        self.max_s, self.quiet_s, self.first_s, self.over_db, self.min_db = max_s, quiet_s, first_s, over_db, min_db
        self.n = 0              # frames fed
        self.pw = []            # the last SMOOTH frames' powers
        self.smooth = []        # the 250 ms levels of the last FLOOR_WIN frames
        self.floor = None
        self.first = None       # the first speech frame's end (s)
        self.last = None        # the last speech frame's end (s)

    def t(self):
        return self.n * FRAME / RATE

    def feed(self, db):
        self.n += 1
        self.pw.append(10.0 ** (db / 10.0))
        if len(self.pw) > SMOOTH:
            self.pw.pop(0)
        m = sum(self.pw) / len(self.pw)
        self.smooth.append(10.0 * math.log10(m) if m > 1e-12 else -120.0)
        if len(self.smooth) > FLOOR_WIN:
            self.smooth.pop(0)
        self.floor = min(min(self.smooth), FLOOR_CAP_DB)
        if db >= self.min_db and db >= self.floor + self.over_db:
            if self.first is None:
                self.first = self.t()
            self.last = self.t()
        return self.end()

    def end(self):
        t = self.t()
        if t >= self.max_s - 1e-9:
            return "the %g s limit" % self.max_s
        if self.last is None and t >= self.first_s - 1e-9:
            return "nothing said in the first %g s" % self.first_s
        if self.last is not None and t - self.last >= self.quiet_s - 1e-9:
            return "%g s quiet" % self.quiet_s
        return None

    def summary(self, why):
        talk = "talk %.1f .. %.1f s" % (self.first - FRAME / RATE, self.last) if self.first is not None else "no talk"
        return "%s; ended: %s" % (talk, why)


def frames_of(data):
    """s16le bytes -> the 50 ms frames' sample arrays (whole frames only)"""
    a = array.array("h")
    a.frombytes(data[: len(data) // 2 * 2])
    if sys.byteorder != "little":
        a.byteswap()
    return [a[i:i + FRAME] for i in range(0, len(a) - FRAME + 1, FRAME)]


def analyze(paths, max_s, quiet_s):
    for p in paths:
        with wave.open(p) as w:
            if w.getsampwidth() != 2 or w.getnchannels() != 1:
                print("%s: not 16-bit mono" % p)
                continue
            rate, data = w.getframerate(), w.readframes(w.getnframes())
        if rate != RATE:
            print("%s: %d Hz, not %d" % (p, rate, RATE))
            continue
        rule = StopRule(max_s=max_s, quiet_s=quiet_s)
        why = None
        for fr in frames_of(data):
            why = rule.feed(level_db(fr))
            if why:
                break
        dur = len(data) / 2.0 / RATE
        print("%s: %.1f s recorded; %s" % (p, dur, ("would end at %.1f s: %s" % (rule.t(), rule.summary(why))) if why
                                            else "still going at its end (%s): it was cut" % rule.summary("-")))


def record(a):
    stop = {"why": None}

    def on_signal(sig, _frame):
        stop["why"] = "the next mark" if sig == signal.SIGTERM else "interrupted"
    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)
    errf = open(a.err, "ab") if a.err else subprocess.DEVNULL
    try:
        pr = subprocess.Popen(["parec", "-d", a.src, "--channels=1", "--rate=%d" % RATE, "--format=s16le",
                               "--latency-msec=50"], stdout=subprocess.PIPE, stderr=errf, stdin=subprocess.DEVNULL)
    except OSError as e:
        line = "no voice: parec: %s" % e
        print(line)
        if a.txt:
            open(a.txt, "w").write(line + "\n")
        return 1
    rule = StopRule(max_s=a.max, quiet_s=a.quiet)
    out = wave.open(a.out, "wb")
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(RATE)
    fd, buf, why, t0 = pr.stdout.fileno(), b"", None, time.monotonic()
    while why is None:
        if stop["why"]:
            why = stop["why"]
            break
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            if time.monotonic() - t0 > a.max + 10:   # no sound from the source at all: give up
                why = "no sound from the source"
            continue
        try:
            chunk = os.read(fd, 16384)
        except InterruptedError:
            continue
        if not chunk:
            why = "the source ended"
            break
        buf += chunk
        n = len(buf) // (2 * FRAME) * (2 * FRAME)
        if not n:
            continue
        data, buf = buf[:n], buf[n:]
        for k, fr in enumerate(frames_of(data)):
            why = rule.feed(level_db(fr))
            if why:
                out.writeframes(data[:(k + 1) * 2 * FRAME])
                break
        else:
            out.writeframes(data)   # (the header is patched by every write)
    if pr.poll() is None:
        pr.send_signal(signal.SIGINT)
        try:
            pr.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pr.kill()
            pr.wait()
    out.close()
    if a.err and errf is not subprocess.DEVNULL:
        errf.close()
        if os.path.exists(a.err) and os.path.getsize(a.err) == 0:
            os.remove(a.err)
    secs = rule.t()
    line = ("%.1f s from %s (%s)" % (secs, a.src, rule.summary(why)) if secs > 0
            else "no voice: %s gave nothing (%s)" % (a.src, why))
    print(line)
    if a.txt:
        with open(a.txt, "w") as f:
            f.write(line + "\n")
    return 0


def main():
    ap = argparse.ArgumentParser(description="a bug mark's voice note, until the player stops talking")
    ap.add_argument("--src", help="the PipeWire source (WiVRn's microphone)")
    ap.add_argument("--out", help="the WAV to write")
    ap.add_argument("--txt", help="also write the summary line here")
    ap.add_argument("--err", help="parec's errors (removed when empty)")
    ap.add_argument("--max", type=float, default=MAX_S, help="the longest note (s, 1 .. 30; default 30)")
    ap.add_argument("--quiet", type=float, default=QUIET_S, help="end this long after the last speech (s, 0.5 .. 10; "
                    "default 2)")
    ap.add_argument("--analyze", nargs="+", metavar="WAV", help="the stop rule over recorded notes (offline)")
    a = ap.parse_args()
    a.max = min(max(a.max, 1.0), MAX_S)
    a.quiet = min(max(a.quiet, 0.5), 10.0)
    if a.analyze:
        analyze(a.analyze, a.max, a.quiet)
        return 0
    if not a.src or not a.out:
        ap.error("--src and --out (or --analyze)")
    return record(a)


if __name__ == "__main__":
    sys.exit(main())
