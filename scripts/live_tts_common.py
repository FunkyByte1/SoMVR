"""live_tts_common.py - what the live loop's two speech engines share (docs/re-xr-pipeline.md 162.9, som-b37-live3):
the line cut into pieces (sentences), each piece's level, the WAV, the worker's stdin/stdout protocol, the benchmark.
Imported by live_kokoro.py (Kokoro-82M) and live_piper.py (Piper), each in its own venv; needs numpy.

The worker protocol (one job per stdin line, the pieces streamed):
  stdin:  OUT.wav<TAB>TEXT[<TAB>whole]     (whole: one piece, not streamed - the acknowledgements)
  stdout: "ready ENGINE VOICE load S" once, then per job: "part OUT I SYNTH_S AUDIO_S" as each piece is written (piece 0
          is OUT.wav itself - what build 37's layer waits for; piece I > 0 is OUT.I.wav), then
          "done OUT SYNTH_S AUDIO_S N" (all N pieces written) - or "fail OUT why"
Each piece: mono s16le at the engine's rate, its speech at -20 dBFS RMS (a dialogue line), peak at most -1 dBFS, 0.15 s
of silence before the first piece, 0.12 s after each; written atomically (OUT.tmp, then renamed).
"""
import os
import re
import sys
import time
import wave

TARGET_RMS = 0.1       # -20 dBFS
PEAK = 0.89            # -1 dBFS
LEAD_S, GAP_S = 0.15, 0.12
MIN_PIECE = 14         # a sentence shorter than this goes with the next ("OK." alone sounds clipped)


def speakable(text):
    """the card's text as spoken: the menu path's '>' and the list marks read as pauses, the ASCII dots kept"""
    t = text.replace(">", ", ").replace("|", ", ").replace("_", " ").replace("...", ".")
    return re.sub(r"\s+", " ", t).strip()


def pieces(text):
    """the line in sentences (. ! ? and line breaks end one), the short ones joined to the next"""
    raw = [s.strip() for s in re.split(r"(?<=[.!?])\s+|\n+", text) if s.strip()]
    out, cur = [], ""
    for s in raw:
        cur = (cur + " " + s).strip()
        if len(cur) >= MIN_PIECE:
            out.append(cur)
            cur = ""
    if cur:
        if out and len(cur) < MIN_PIECE:
            out[-1] += " " + cur
        else:
            out.append(cur)
    return [speakable(p) for p in out if speakable(p)]


def level(np, a, first, rate):
    """float audio -> int16 at the dialogue level, with its lead (first piece) and tail silence"""
    a = np.asarray(a, dtype=np.float32).reshape(-1)
    loud = np.abs(a) > 1e-4
    rms = float(np.sqrt(np.mean(a[loud] ** 2))) if np.any(loud) else 0.0
    if rms > 0:
        a = a * (TARGET_RMS / rms)
    pk = float(np.max(np.abs(a))) if len(a) else 0.0
    if pk > PEAK:
        a = a * (PEAK / pk)
    parts = ([np.zeros(int(rate * LEAD_S), dtype=np.float32)] if first else []) + \
        [a, np.zeros(int(rate * GAP_S), dtype=np.float32)]
    a = np.concatenate(parts)
    return (np.clip(a, -1, 1) * 32767).astype(np.int16), len(a) / rate


def write_wav(path, pcm, rate):
    tmp = path + ".tmp"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    os.replace(tmp, path)


def piece_path(out, i):
    return out if i == 0 else (out[:-4] if out.endswith(".wav") else out) + f".{i}.wav"


def synth_job(eng, np, out, text, emit, whole=False):
    """one job: each piece synthesised and written in order (whole: the text as one piece - the acknowledgements);
    -> (synth s, audio s, pieces)"""
    ps = [speakable(text)] if whole and speakable(text) else pieces(text)
    if not ps:
        raise ValueError("nothing to say")
    t_all, audio = time.monotonic(), 0.0
    for i, p in enumerate(ps):
        t1 = time.monotonic()
        pcm, dur = level(np, eng.synth(p), i == 0, eng.rate)
        write_wav(piece_path(out, i), pcm, eng.rate)
        audio += dur
        emit(f"part {out} {i} {time.monotonic() - t1:.2f} {dur:.2f}")
    return time.monotonic() - t_all, audio, len(ps)


def serve(eng, np, name, voice, t0):
    def emit(s):
        print(s, flush=True)
    emit(f"ready {name} {voice} load {time.monotonic() - t0:.2f}")
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        out, _, rest = line.partition("\t")
        text, _, opt = rest.partition("\t")   # (OUT<TAB>TEXT<TAB>whole: one piece)
        try:
            s, a, n = synth_job(eng, np, out, text, emit, whole=opt.strip() == "whole")
            emit(f"done {out} {s:.2f} {a:.2f} {n}")
        except Exception as e:
            emit(f"fail {out} {type(e).__name__}: {e}")
    return 0


def bench(eng, np, name, texts, outdir):
    """each text once, streamed: the first piece's time (what the player waits for) and the whole line's"""
    os.makedirs(outdir, exist_ok=True)
    if len(texts) == 1 and texts[0].startswith("@"):   # @FILE: one text a line
        with open(texts[0][1:]) as f:
            texts = [ln.strip() for ln in f if ln.strip()]
    tot_s = tot_a = 0.0
    firsts = []
    for k, text in enumerate(texts):
        got = []
        s, a, n = synth_job(eng, np, os.path.join(outdir, f"{name}-{k + 1}.wav"), text, got.append)
        f = got[0].split()
        firsts.append(float(f[3]))
        tot_s, tot_a = tot_s + s, tot_a + a
        print(f"bench {name} {k + 1}: {len(text)} chars, {n} pieces: the first {f[3]} s for {f[4]} s of speech; all "
              f"{s:.2f} s for {a:.2f} s (x{a / s:.1f} real time)", flush=True)
    firsts.sort()
    print(f"bench {name} total: {tot_s:.2f} s for {tot_a:.2f} s of speech (x{tot_a / max(tot_s, 1e-6):.1f} real time); "
          f"the first piece median {firsts[len(firsts) // 2]:.2f} s, max {firsts[-1]:.2f} s", flush=True)
    return 0
