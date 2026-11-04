#!/usr/bin/env python3
"""live_stt.py - the live loop's speech-to-text worker (docs/re-xr-pipeline.md 162): a small faster-whisper model
loaded once from the local Hugging Face cache (nothing downloaded: local_files_only), then one job per stdin line.

  shared/tools-venv/bin/python -I live_stt.py [--model base.en] [--threads 2] [--beam 1]
      stdin:  WAV<TAB>OUT.json            one job per line (the WAV a mark's voice.wav, 48 kHz mono s16le or any rate)
      stdout: "ready MODEL load S" once, then "done OUT S" (or "fail OUT why") per job
  ... --bench WAV...                       each file once, its time and text (the model's cost, offline)

Run only inside the lane (scripts/live_lane.sh: its CPUs, nice 19, idle I/O) - never on the GPU (device cpu,
int8). OUT.json: {"text", "model", "secs" (the transcription), "audio_s", "lang", "segments": [[t0, t1, text]...]},
written atomically (OUT.tmp, then renamed). The big model (HandOfFate2/scripts/transcribe_marks.py) re-transcribes
after the session for the record; this one only has to be quick and good enough to act on.
"""
import argparse
import json
import os
import sys
import time
import wave

MODELS = {  # the short names the switch takes -> the cached repos (Systran's CTranslate2 conversions)
    "base.en": "Systran/faster-whisper-base.en",
    "small.en": "Systran/faster-whisper-small.en",
    "distil-small.en": "Systran/faster-distil-whisper-small.en",
    "medium.en": "Systran/faster-whisper-medium.en",
}


def load16k(path):
    """the WAV as 16 kHz mono float32 (the venv's PyAV is too old for faster-whisper's own decoder)"""
    import numpy as np
    with wave.open(path) as w:
        ch, rate, n, sw = w.getnchannels(), w.getframerate(), w.getnframes(), w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        raise ValueError(f"{sw * 8}-bit samples (want 16)")
    a = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    if rate != 16000 and len(a):
        t_old = np.arange(len(a)) / rate
        t_new = np.arange(int(len(a) * 16000 / rate)) / 16000.0
        a = np.interp(t_new, t_old, a).astype(np.float32)
    return a, len(a) / 16000.0


def transcribe(model, path, beam):
    a, dur = load16k(path)
    t0 = time.monotonic()
    segs, info = model.transcribe(a, language="en", beam_size=beam, vad_filter=False, condition_on_previous_text=False)
    out = [[round(s.start, 2), round(s.end, 2), s.text.strip()] for s in segs]
    secs = time.monotonic() - t0
    return {"text": " ".join(s[2] for s in out).strip(), "secs": round(secs, 2), "audio_s": round(dur, 2),
            "lang": getattr(info, "language", "en"), "segments": out}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="base.en")
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--beam", type=int, default=1)
    ap.add_argument("--bench", nargs="*")
    a = ap.parse_args()
    repo = MODELS.get(a.model, a.model)
    t0 = time.monotonic()
    try:
        from faster_whisper import WhisperModel
        model = WhisperModel(repo, device="cpu", compute_type="int8", cpu_threads=max(1, a.threads), num_workers=1,
                             local_files_only=True)
    except Exception as e:  # not cached (or no faster_whisper): say which to fetch, never download
        print(f"fail - the model {repo} is not in the local Hugging Face cache or faster_whisper is missing ({e}); "
              f"fetch it once by hand: huggingface-cli download {repo}", flush=True)
        return 2
    load = time.monotonic() - t0
    print(f"ready {a.model} load {load:.2f}", flush=True)
    if a.bench is not None:
        for p in a.bench:
            r = transcribe(model, p, a.beam)
            print(f"bench {os.path.basename(os.path.dirname(p))} {r['audio_s']:.1f} s audio -> {r['secs']:.2f} s: "
                  f"{r['text'][:160]}", flush=True)
        return 0
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        wav, _, out = line.partition("\t")
        try:
            r = transcribe(model, wav, a.beam)
            r["model"] = a.model
            with open(out + ".tmp", "w") as f:
                json.dump(r, f)
            os.replace(out + ".tmp", out)
            print(f"done {out} {r['secs']:.2f}", flush=True)
        except Exception as e:
            print(f"fail {out} {type(e).__name__}: {e}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
