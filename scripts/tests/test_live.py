#!/usr/bin/env python3
"""test_live.py - the live loop's scripts offline (docs/re-xr-pipeline.md 162): a session folder of our own, no game,
no microphone, no sound out (the note is made by espeak-ng into a WAV FILE - never played).

  python3 -I scripts/tests/test_live.py        (about 25 s; the speech-to-text worker on one core, base.en)

  1. live_session.sh start: the folder, the daemon, DIR/on, the lane's line in live.log, build.txt (fingerprint)
  2. a mark with a recorded note (voice.wav, then voice.txt as voice_rec.py leaves it): inbox/1.json within 5 s of
     the note's end with the words said; live_wait.sh started before it wakes on it
  3. a mark without a voice: inbox/2.json after the 3 s grace, no transcript
  4. live_say.sh: say/1.txt; live_status.sh: on, 2 notes, 1 message
  5. live_patch.sh stage: refused without the logic ABI / without the checks; a tree whose layer changed -> restart;
     the same tree -> live; ready = the newest; list
  5b. b37 (162.8), the spoken answer with stand-ins for pactl and paplay (nothing reaches a sound device): a card's
     line (say/say-1.line, as the layer writes it) synthesised by Kokoro in the lane, NOT played before its calm
     moment; say/say-1.calm -> played on the test's sink at the game's stream volume, the game ducked and put back;
     a quiet message never synthesised; a sink not listed and the default (wivrn.sink, not in the list) never played
  b37 live3 (162.9): the acknowledgements made at the TTS model's load (ack/*.wav); a note's voice ending -> "Got it."
     on the test sink and the buzz word in RUN/somvr.trigger at once (< 0.6 s, before the words); a note without
     voice / without talk -> the mic line; no RUN/somvr.trigger -> no buzz and the file NOT made; a message straight
     from say/<n>.txt (no .line), streamed sentence by sentence (KEY.wav, KEY.1.wav, KEY.parts) into one pacat stream
     at its calm moment; a long answer in pages (6 lines of 29, "(1 of 2: ...)"), the whole said once with page 1
  6. live_session.sh stop: the daemon ends, DIR/on gone, live_wait.sh says the session is off (exit 3)
  b37 (lane2, 163.7): the lane job names live-stt / live-tts / live-build (and, with the lane's lane.conf in the tree,
  the speech workers in its LANE_VR_GUARDED and the build in its LANE_VR_JOBS); the two models loaded one at a time
  (the speech-to-text's ready before Kokoro's load starts), each load's time logged, 1 thread each
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(os.path.dirname(HERE), "mod", "somvr")
fails = checks = 0


def check(c, msg):
    global fails, checks
    checks += 1
    if not c:
        fails += 1
    print(("ok   " if c else "FAIL ") + msg)


def run(*a, env=None, timeout=60):
    return subprocess.run(list(a), capture_output=True, text=True, env=env, timeout=timeout)


def main():
    if not shutil.which("espeak-ng"):
        print("SKIP: no espeak-ng (the note is made with it)")
        return 0
    tmp = tempfile.mkdtemp(prefix="live-test-")
    env = dict(os.environ, LIVE_ROOT=os.path.join(tmp, "live"))
    run_dir = os.path.join(tmp, "run-test")
    os.makedirs(os.path.join(run_dir, "marks"))
    os.makedirs(env["LIVE_ROOT"])
    sess = os.path.join(HERE, "live_session.sh")
    # b37: the spoken answer's stand-ins (the daemon plays through them: nothing reaches a sound device)
    fb = os.path.join(tmp, "bin")
    os.makedirs(fb)
    calls = os.path.join(tmp, "calls.log")
    with open(os.path.join(fb, "pactl"), "w") as f:
        f.write(f"""#!/bin/sh
echo "pactl $*" >> {calls}
case "$*" in
  "-f json list short sinks") echo '[{{"index":7,"name":"somvr_live_test_null"}}]' ;;
  "-f json list sink-inputs") echo '[{{"index":31,"sink":7,"volume":{{"front-left":{{"value_percent":"80%"}}}},"properties":{{"application.name":"ShadowOfMordor.exe"}}}},{{"index":32,"sink":7,"volume":{{}},"properties":{{"application.name":"other"}}}}]' ;;
esac
""")
    with open(os.path.join(fb, "paplay"), "w") as f:
        f.write(f"#!/bin/sh\necho \"paplay $*\" >> {calls}\n")
    pbytes = os.path.join(tmp, "pacat.bytes")
    with open(os.path.join(fb, "pacat"), "w") as f:   # b37 live3: the streamed line's stand-in (its stdin counted)
        f.write(f"#!/bin/sh\necho \"pacat $*\" >> {calls}\ncat >> {pbytes}\n")
    for b in ("pactl", "paplay", "pacat"):
        os.chmod(os.path.join(fb, b), 0o755)
    trig = os.path.join(run_dir, "somvr.trigger")   # b37 live3: the session's stereo switch (the buzz's word goes in)
    open(trig, "w").close()
    env.update(SOMVR_LIVE_TTS="on", LIVE_TTS_BIN=fb, LIVE_TTS_SINK="somvr_live_test_null")
    d = run(sess, "dir", run_dir, env=env).stdout.strip()
    check(os.path.isdir(os.path.join(d, "inbox")) and os.path.isdir(os.path.join(d, "patches")), f"dir makes {d}")
    pid = int(run(sess, "start", run_dir, str(os.getpid()), MOD, env=env).stdout.strip())
    check(os.path.exists(os.path.join(d, "on")), f"start: the daemon {pid}, DIR/on")
    bt = open(os.path.join(d, "build.txt")).read()
    check("layer=" in bt and "host=" in bt, "build.txt: the layer's and the host's hashes")
    # wait for the worker's model
    t0 = time.time()
    while time.time() - t0 < 30:
        try:
            if json.load(open(os.path.join(d, "daemon.json"))).get("stt") == "ready":
                break
        except (OSError, ValueError):
            pass
        time.sleep(0.2)
    check(json.load(open(os.path.join(d, "daemon.json"))).get("stt") == "ready", f"the worker ready in {time.time() - t0:.1f} s")
    log = open(os.path.join(d, "live.log")).read()
    check("the lane:" in log, "live.log names the lane")
    # b37: the lane job names, and the lane's rules for them when its conf is in this tree
    names = [run(os.path.join(HERE, "live_lane.sh"), "name", k).stdout.strip() for k in ("stt", "tts", "build")]
    check(names == ["live-stt", "live-tts", "live-build"], f"the lane job names: {names}")
    conf = os.path.join(HERE, "lane", "lane.conf")
    if os.path.exists(conf):
        import fnmatch
        kv = {}
        for ln in open(conf):
            k, _, v = ln.strip().partition("=")
            if k.startswith("LANE_") and v:
                kv[k] = v.strip('"')
        guarded, free = kv.get("LANE_VR_GUARDED", "").split(), kv.get("LANE_VR_JOBS", "").split()
        check("live-stt" in guarded and "live-tts" in guarded and any(fnmatch.fnmatch("live-build", p) for p in free),
              f"lane.conf: the speech workers guarded in VR ({guarded}), the build free ({free})")
    else:
        print("skip the lane.conf names (no scripts/lane/lane.conf in this tree: the stub lane)")
    # b37: one model load at a time - the TTS model's starts only after the STT's is ready; both timed, 1 thread
    t0 = time.time()
    while time.time() - t0 < 60:
        try:
            if json.load(open(os.path.join(d, "daemon.json"))).get("tts") == "ready":
                break
        except (OSError, ValueError):
            pass
        time.sleep(0.2)
    log = open(os.path.join(d, "live.log")).read()
    i_stt, i_go, i_tts = log.find("stt: ready"), log.find("tts: its model loads now"), log.find("tts: ready")
    check(0 <= i_stt < i_go < i_tts and "the load took" in log[i_stt:i_go] and "the load took" in log[i_tts:]
          and "(1 thread)" in log[i_stt:i_go] and log.count("worker started in the lane") == 2
          and all("threads 1)" in ln for ln in log.splitlines() if "worker started in the lane" in ln),
          f"the loads one at a time: stt ready -> tts starts -> tts ready, each timed, 1 thread "
          f"({[ln[20:110] for ln in log.splitlines() if 'the load took' in ln]})")
    t0 = time.time()
    while time.time() - t0 < 30 and "the acknowledgements made" not in open(os.path.join(d, "live.log")).read():
        time.sleep(0.2)
    acks = sorted(x for x in os.listdir(os.path.join(d, "ack")) if x.endswith(".wav"))
    check(acks == ["got.wav", "mic.wav", "noted.wav", "onit.wav"],
          f"b37 live3: the acknowledgements made after the model's load: {acks} "
          f"({[ln[20:100] for ln in open(os.path.join(d, 'live.log')).read().splitlines() if 'acknowledgements' in ln]})")
    # 2. a mark with a recorded note
    waiter = subprocess.Popen([os.path.join(HERE, "live_wait.sh"), "--timeout", "40"], stdout=subprocess.PIPE, text=True,
                              env=dict(env, LIVE_DIR=d))
    m1 = os.path.join(run_dir, "marks", "120000_1")
    os.makedirs(m1)
    with open(os.path.join(m1, "state.txt"), "w") as f:
        f.write("bug mark #1 (the chord B + Y) 2026-11-03 12:00:00\nsomvr built test, pid 1, t=1234.500\n"
                "context world (ifs=3); stereo on\n\n---- the VR menu's settings now\nTURN=smooth\n")
    open(os.path.join(m1, "served"), "w").write("1\n")
    said = "The climbing feels much better now but the sword gets stuck in the wall."
    r = run("espeak-ng", "-v", "en-us", "-s", "150", "-w", os.path.join(m1, "voice.wav"), said)
    check(r.returncode == 0 and os.path.getsize(os.path.join(m1, "voice.wav")) > 10000, "espeak-ng made the note (a file)")
    open(calls, "w").close()
    open(os.path.join(m1, "voice.txt"), "w").write("4.2 s from test (talk 0.1 .. 4.0 s; ended: 2 s quiet)\n")
    t_end = time.time()
    got = os.path.join(d, "ack", "got.wav")
    while time.time() - t_end < 3 and "paplay" not in open(calls).read():
        time.sleep(0.02)
    t_ack = time.time() - t_end
    c = open(calls).read()
    check(f"paplay --device=somvr_live_test_null" in c and got in c and open(trig).read() == "haptic L 0.35 90"
          and t_ack < 0.6, f"b37 live3: the note's end -> 'Got it.' on the test sink + the buzz word "
                           f"{open(trig).read()!r} in {t_ack:.2f} s (< 0.6, before the words)")
    p1 = os.path.join(d, "inbox", "1.json")
    while time.time() - t_end < 15 and not os.path.exists(p1):
        time.sleep(0.05)
    lat = time.time() - t_end
    check(os.path.exists(p1) and lat < 5, f"inbox/1.json {lat:.2f} s after the note ended (< 5)")
    j = json.load(open(p1)) if os.path.exists(p1) else {}
    words = [w for w in ("climbing", "sword", "wall") if w in (j.get("transcript") or "").lower()]
    check(len(words) == 3, f"the words said: {j.get('transcript')!r} (stt {j.get('stt_s')} s)")
    check(j.get("game_t") == 1234.5 and j.get("state", [""])[0].startswith("bug mark #1") and len(j.get("state", [])) == 3,
          f"game time {j.get('game_t')}, state's head lines {len(j.get('state', []))}")
    try:
        out = waiter.communicate(timeout=10)[0]
    except subprocess.TimeoutExpired:
        waiter.kill()
        out = ""
    check(waiter.returncode == 0 and out.startswith("note 1 - mark 120000_1") and "said: " in out,
          f"live_wait woke: {out.splitlines()[0] if out else '(nothing)'}")
    # 3. a mark without a voice
    m2 = os.path.join(run_dir, "marks", "120100_2")
    os.makedirs(m2)
    open(os.path.join(m2, "state.txt"), "w").write("bug mark #2\nsomvr built test, pid 1, t=1300.0\n")
    open(os.path.join(m2, "served"), "w").write("1\n")
    open(os.path.join(m2, "voice.txt"), "w").write("no voice: no WiVRn microphone source\n")
    t2 = time.time()
    p2 = os.path.join(d, "inbox", "2.json")
    while time.time() - t2 < 10 and not os.path.exists(p2):
        time.sleep(0.1)
    j2 = json.load(open(p2)) if os.path.exists(p2) else {}
    check(os.path.exists(p2) and 2.5 < time.time() - t2 < 6 and not j2.get("transcript") and "no voice" in j2.get("why", ""),
          f"no voice: inbox/2.json after the grace, why {j2.get('why')!r}")
    lg = open(os.path.join(d, "live.log")).read()
    check("ack 120100_2: 'I couldn't hear that." in lg and j2.get("ack") == "mic",
          f"b37 live3: no voice -> the mic line ({[ln[20:120] for ln in lg.splitlines() if 'ack 120100_2' in ln]})")
    # b37 live3: a note without talk, and no RUN/somvr.trigger: the mic line at once, no buzz, the file not made
    os.unlink(trig)
    m3 = os.path.join(run_dir, "marks", "120200_3")
    os.makedirs(m3)
    open(os.path.join(m3, "state.txt"), "w").write("bug mark #3\nsomvr built test, pid 1, t=1400.0\n")
    open(os.path.join(m3, "served"), "w").write("1\n")
    shutil.copy2(os.path.join(m1, "voice.wav"), os.path.join(m3, "voice.wav"))
    open(os.path.join(m3, "voice.txt"), "w").write("5.0 s from test (no talk; ended: nothing said in the first 5 s)\n")
    t3 = time.time()
    while time.time() - t3 < 3 and "ack 120200_3" not in open(os.path.join(d, "live.log")).read():
        time.sleep(0.05)
    lg = open(os.path.join(d, "live.log")).read()
    a3 = [ln for ln in lg.splitlines() if "ack 120200_3" in ln]
    check(a3 and "I couldn't hear that" in a3[0] and "no buzz" in a3[0] and not os.path.exists(trig)
          and time.time() - t3 < 1.0, f"b37 live3: no talk -> the mic line at once, no trigger file: no buzz, none made "
                                      f"({a3[0][20:150] if a3 else '-'})")
    t3 = time.time()
    while time.time() - t3 < 15 and not os.path.exists(os.path.join(d, "inbox", "3.json")):
        time.sleep(0.1)
    open(trig, "w").close()
    # 4. say, status
    t_say = time.time()
    r = run(os.path.join(HERE, "live_say.sh"), "Fixed the sword sticking in walls. Try a slash at the rock, then a "
            "thrust at the tree.", env=dict(env, LIVE_DIR=d))
    check(r.returncode == 0 and os.path.exists(os.path.join(d, "say", "1.txt")), f"say: {r.stdout.strip()[:80]}")
    r = run(os.path.join(HERE, "live_status.sh"), "--json", env=dict(env, LIVE_DIR=d))
    st = json.loads(r.stdout) if r.returncode == 0 else {}
    check(st.get("on") and st.get("notes") == 3 and st.get("messages") == 1, f"status: on {st.get('on')}, notes "
          f"{st.get('notes')}, messages {st.get('messages')}, lane {st.get('lane', '')[:40]}")
    # 5. patches
    lp = os.path.join(HERE, "live_patch.sh")
    host_dll = os.path.join(MOD, "build", "win", "xinput9_1_0.dll")
    if os.path.exists(host_dll):
        r = run(lp, "stage", host_dll, "x", "y", "--dir", d)
        check(r.returncode == 2 and "somvr_logic_abi" in r.stderr, "stage refuses a DLL without the logic ABI export")
        r = run(lp, "stage", host_dll, "x", "y", "--dir", d, "--allow-no-abi")
        check(r.returncode == 2 and "build checks" in r.stderr, "stage refuses a DLL without the passing checks")
        # a tree whose layer changed: a copy of the layer's sources with one file edited
        tree = os.path.join(tmp, "tree")
        shutil.copytree(MOD, tree, ignore=shutil.ignore_patterns("build", "*.o", "*.obj"))
        os.makedirs(os.path.join(tree, "build", "win"))
        shutil.copy2(host_dll, os.path.join(tree, "build", "win", "somvr_logic.dll"))
        r = run(lp, "stage", os.path.join(tree, "build", "win", "somvr_logic.dll"), "the same code", "anything", "--dir",
                d, "--allow-no-abi", "--force-unchecked")
        p = open(os.path.join(d, "patches", "1", "patch.txt")).read() if r.returncode == 0 else ""
        check(r.returncode == 0 and "kind=live" in p and "checked=NO" in p, f"the same tree: patch 1 live ({r.stdout.strip()[:60]})")
        with open(os.path.join(tree, "somvr_vrmenu.c"), "a") as f:
            f.write("\n/* a layer change */\n")
        r = run(lp, "stage", os.path.join(tree, "build", "win", "somvr_logic.dll"), "a menu change", "the menu", "--dir",
                d, "--allow-no-abi", "--force-unchecked")
        p = open(os.path.join(d, "patches", "2", "patch.txt")).read() if r.returncode == 0 else ""
        check("kind=restart" in p and "somvr_vrmenu.c" in p, "a layer source changed: patch 2 restart, the file named")
        check(open(os.path.join(d, "patches", "ready")).read().strip() == "2", "ready = 2 (the newest)")
        r = run(lp, "list", "--dir", d)
        check("patch 1 [live" in r.stdout and "patch 2 [restart" in r.stdout and "staged patch 2" in r.stdout,
              "list: both, the log")
        # b37 live3: a patch card too long for build 37's layer (the 22:13 session's: cut at the bottom) - without the
        # layer's "pages" its whole text follows as a quiet paged message; with it, nothing more; > 150 refused
        what = "Walking stops at drops; tap A to step off, hold A to blink down. Plus the wall-climb fix."
        tr = "Walk to a ledge: you stop with a nudge. Tap A to step off. Then climb your stuck wall."
        n_say = len([x for x in os.listdir(os.path.join(d, "say")) if x.endswith(".txt")])
        r = run(lp, "stage", os.path.join(tree, "build", "win", "somvr_logic.dll"), what, tr, "--dir", d, "--allow-no-abi",
                "--force-unchecked")
        new = sorted((x for x in os.listdir(os.path.join(d, "say")) if x.endswith(".txt") and x[:-4].isdigit()),
                     key=lambda x: int(x[:-4]))[n_say:]
        txt = " ".join(" ".join(open(os.path.join(d, "say", x)).read() for x in new).split())
        check(r.returncode == 0 and new and "Patch 3 in full" in txt and "climb your stuck wall" in txt and "..." not in txt
              and all(os.path.exists(os.path.join(d, "say", x[:-4] + ".quiet")) for x in new),
              f"a long patch, build 37's layer: its whole text as {len(new)} quiet message(s) ({r.stdout.strip()[-90:]})")
        open(os.path.join(d, "layer.caps"), "w").write("pages card-out dot-hand\n")
        n_say = len([x for x in os.listdir(os.path.join(d, "say")) if x.endswith(".txt")])
        r = run(lp, "stage", os.path.join(tree, "build", "win", "somvr_logic.dll"), what, tr, "--dir", d, "--allow-no-abi",
                "--force-unchecked")
        check(r.returncode == 0 and len([x for x in os.listdir(os.path.join(d, "say")) if x.endswith(".txt")]) == n_say,
              "a long patch, a layer that pages it (layer.caps): no extra message")
        r = run(lp, "stage", os.path.join(tree, "build", "win", "somvr_logic.dll"), "x" * 151, "y", "--dir", d,
                "--allow-no-abi", "--force-unchecked")
        check(r.returncode == 2 and "150 or less" in r.stderr and not os.path.exists(os.path.join(d, "patches", "5")),
              "a 151-character 'what': refused before anything is staged (never cut)")
    else:
        print("SKIP the patch checks: no build/win/xinput9_1_0.dll (win/build.sh first)")
    # 5b. b37: the spoken answer, end to end through the daemon (live3: Piper by default, streamed; stand-ins play)
    sd = os.path.join(d, "say")
    wav = os.path.join(sd, "say-1.wav")
    while time.time() - t_say < 30 and not os.path.exists(os.path.join(sd, "say-1.parts")):
        time.sleep(0.05)
    import wave
    pieces = [wav] + [os.path.join(sd, f"say-1.{i}.wav") for i in range(1, 8) if os.path.exists(os.path.join(sd, f"say-1.{i}.wav"))]
    secs = nbytes = 0
    for pc in pieces if os.path.exists(wav) else []:
        with wave.open(pc) as wv:
            secs += wv.getnframes() / wv.getframerate()
            nbytes += wv.getnframes() * 2
    lg = open(os.path.join(d, "live.log")).read()
    first = [ln for ln in lg.splitlines() if "say-1: the first sentence" in ln]
    check(secs > 2.0 and len(pieces) >= 2 and open(os.path.join(sd, "say-1.parts")).read().strip() == str(len(pieces))
          and not os.path.exists(os.path.join(sd, "say-1.line")) and first,
          f"tts: say/1.txt (no .line) -> {len(pieces)} sentences, {secs:.1f} s of speech; {first[0][20:130] if first else '-'}")
    time.sleep(0.5)
    c = open(calls).read() if os.path.exists(calls) else ""
    check("pacat" not in c and not os.path.exists(os.path.join(sd, "say-1.spoken")),
          "tts: not played before the card's calm moment")
    open(calls, "w").close()
    with open(os.path.join(sd, "say-1.calm"), "w") as f:
        f.write("now\n")
    t6 = time.time()
    while time.time() - t6 < 15 and not os.path.exists(os.path.join(sd, "say-1.spoken")):
        time.sleep(0.1)
    c = open(calls).read() if os.path.exists(calls) else ""
    order = [ln for ln in c.splitlines() if "set-sink-input-volume" in ln or ln.startswith("pacat")]
    with wave.open(wav) as wv:
        rate = wv.getframerate()
    check(order == ["pactl set-sink-input-volume 31 40%", f"pacat --playback --device=somvr_live_test_null --format=s16le "
                    f"--rate={rate} --channels=1 --volume=52428 --client-name=SoM VR live --latency-msec=60",
                    "pactl set-sink-input-volume 31 80%"] and os.path.getsize(pbytes) == nbytes,
          f"tts: at say-1.calm ({time.time() - t6:.1f} s): ducked 80 -> 40 %, every sentence into one stream on the "
          f"test sink at the game's 80 % ({os.path.getsize(pbytes) if os.path.exists(pbytes) else 0} of {nbytes} bytes), "
          f"put back: {order}")
    sp = open(os.path.join(sd, "say-1.spoken")).read() if os.path.exists(os.path.join(sd, "say-1.spoken")) else ""
    check("spoken: on somvr_live_test_null at 80 %" in sp and f"{len(pieces)} sentences streamed" in sp,
          f"tts: say-1.spoken: {sp.strip()[:120]}")
    # the layer's cards still come as .line (a patch's): synthesised as before
    with open(os.path.join(sd, "patch-1.line"), "w") as f:
        f.write("Patch 1 ready. The climb's wall-top hang. Apply it: menu, Test tab.\n")
    t7 = time.time()
    while time.time() - t7 < 20 and not os.path.exists(os.path.join(sd, "patch-1.parts")):
        time.sleep(0.1)
    np1 = open(os.path.join(sd, "patch-1.parts")).read().strip() if os.path.exists(os.path.join(sd, "patch-1.parts")) else "?"
    check(os.path.exists(os.path.join(sd, "patch-1.wav")) and (np1 == "1" or not os.path.exists(host_dll)),
          f"tts: a layer card's patch-1.line -> patch-1.wav; b37 live3: said short, 'Patch 1 ready: <what>' from its "
          f"patch.txt ({np1} sentence; the layer's line had 3)")
    # b37 live3: a long answer in pages - never a cut with "..."
    long = ("Your orc was 10 metres away with his back turned, so the finisher never started. Get within 2 metres, "
            "face him, then hold the trigger to charge it. The blue outline means it is ready.")
    m1 = 1 + max(int(x[:-4]) for x in os.listdir(sd) if x.endswith(".txt") and x[:-4].isdigit())
    r = run(os.path.join(HERE, "live_say.sh"), long, env=dict(env, LIVE_DIR=d))
    sys.path.insert(0, HERE)
    import live as livepy
    p2_, p3_ = (open(os.path.join(sd, f"{k}.txt")).read() for k in (m1, m1 + 1))
    check(r.returncode == 0 and "2 pages" in r.stdout and p2_.rstrip().endswith("(1 of 2: tap for the next)")
          and p3_.rstrip().endswith("(2 of 2)") and "..." not in p2_ + p3_
          and all(len(livepy.lv_wrap(x)) <= 6 for x in (p2_, p3_)) and os.path.exists(os.path.join(sd, f"{m1 + 1}.quiet"))
          and open(os.path.join(sd, f"{m1}.voice")).read().strip() == long,
          f"say: a long answer in 2 pages of <= 6 lines, no '...', the whole said with page 1 ({r.stdout.strip()[:60]})")
    t8 = time.time()
    while time.time() - t8 < 20 and not os.path.exists(os.path.join(sd, f"say-{m1}.parts")):
        time.sleep(0.1)
    lg = open(os.path.join(d, "live.log")).read()
    np2 = int(open(os.path.join(sd, f"say-{m1}.parts")).read()) if os.path.exists(os.path.join(sd, f"say-{m1}.parts")) else 0
    check(np2 >= 3 and f"say-{m1 + 1}: quiet" in lg and not os.path.exists(os.path.join(sd, f"say-{m1 + 1}.wav")),
          f"tts: page 1 says the whole answer ({np2} sentences), page 2 quiet")
    open(calls, "w").close()
    r = run(sys.executable, "-I", os.path.join(HERE, "live_tts.py"), "--wav", wav, "--out", os.path.join(tmp, "x.spoken"),
            "--sink", "somvr_gone", env=dict(env, LIVE_TTS_SINK=""))
    check(r.returncode == 1 and "paplay" not in open(calls).read() and "not listed" in
          open(os.path.join(tmp, "x.spoken")).read(), "tts: a sink not listed - never played")
    r = run(sys.executable, "-I", os.path.join(HERE, "live_tts.py"), "--wav", wav, "--out", os.path.join(tmp, "y.spoken"),
            env=dict(env, LIVE_TTS_SINK=""))
    check(r.returncode == 1 and "paplay" not in open(calls).read() and "wivrn.sink is not listed" in
          open(os.path.join(tmp, "y.spoken")).read(), "tts: the default is wivrn.sink by its exact name - not there, never "
                                                      "played (no other sink tried)")
    t9 = time.time()   # (daemon.json is written every 2 s)
    while time.time() - t9 < 4 and json.load(open(os.path.join(d, "daemon.json"))).get("spoken") != 1:
        time.sleep(0.2)
    dj = json.load(open(os.path.join(d, "daemon.json")))
    check(dj.get("tts") == "ready" and dj.get("spoken") == 1, f"daemon.json: tts {dj.get('tts')}, {dj.get('spoken')} spoken"
          + ("" if dj.get("spoken") == 1 else str(dj) + str([ln[20:140] for ln in open(os.path.join(d, "live.log")).read().splitlines()
                                                  if "say-1" in ln])))
    # 6. stop
    r = run(sess, "stop", run_dir, env=env)
    alive = os.path.exists(f"/proc/{pid}")
    check(not alive and not os.path.exists(os.path.join(d, "on")), "stop: the daemon ended, DIR/on gone")
    r = run(os.path.join(HERE, "live_wait.sh"), "--dir", d)
    check(r.returncode == 3 and "not on" in r.stdout, f"live_wait after the end: exit {r.returncode}")
    dj = json.load(open(os.path.join(d, "daemon.json")))
    check(dj.get("alive") is False and dj.get("notes") == 3, "daemon.json: not alive, 3 notes")
    shutil.rmtree(tmp)
    print(f"test_live: {checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
