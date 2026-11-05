#!/usr/bin/env python3
"""live.py - the coordinator's side of the live loop (docs/re-xr-pipeline.md 162). Standard library only; run with
python3 -I. The shell names (scripts/live_wait.sh, live_status.sh, live_say.sh, live_patch.sh) call it.

  live.py wait [SINCE] [--dir DIR] [--timeout S]   block until a note with n > SINCE is in the inbox (SINCE: default
                                                   the newest now = the next one), print it; exit 0. The session
                                                   ends first: exit 3; the timeout: exit 4
  live.py status [--dir DIR] [--json]              the session (on/off, the daemon), notes, messages, patches, lane
  live.py say TEXT... [--dir DIR] [--quiet]        a message for the headset: DIR/say/<n>.txt (the card on the palm
                                                   at the next calm moment, b37: also spoken then with
                                                   SOMVR_LIVE_TTS=on; --quiet: the card only)
  live.py patch stage DLL WHAT TRY [--dir DIR] [--tree MOD] [--force-unchecked] [--allow-no-abi]
                                                   a logic DLL (hotreload's somvr_logic.dll) into DIR/patches/<id>/;
                                                   it must carry the logic ABI export and the tree's passing checks
                                                   (live.py patch check MOD); a layer or host change -> kind restart
  live.py patch check MOD                          both build checks of the tree (mod/somvr) in the lane, then
                                                   MOD/build/live-checks.txt with the DLL's sha256
  live.py patch list [--dir DIR]                   the patches, the one live, the log
  live.py fingerprint MOD                          the layer's and the host's source hashes (DIR/build.txt)
DIR: --dir, else $LIVE_DIR, else logs/live/current (the newest session's; tests: LIVE_ROOT/current).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SOM = "/home/bara/Projects/flat2vr/ShadowOfMordor"


def live_dir(arg):
    d = arg or os.environ.get("LIVE_DIR") or os.path.join(os.environ.get("LIVE_ROOT") or f"{SOM}/logs/live", "current")
    return os.path.realpath(d)


def read(p, n=1 << 20):
    try:
        with open(p, errors="replace") as f:
            return f.read(n)
    except OSError:
        return None


def write_atomic(p, s):
    tmp = p + ".tmp"
    with open(tmp, "w") as f:
        f.write(s)
    os.replace(tmp, p)


def kv(text):
    out = {}
    for ln in (text or "").splitlines():
        k, sep, v = ln.partition("=")
        if sep:
            out[k.strip()] = v.strip()
    return out


def notes(d):
    try:
        return sorted(int(x[:-5]) for x in os.listdir(os.path.join(d, "inbox")) if re.fullmatch(r"\d+\.json", x))
    except OSError:
        return []


def fmt_note(d, n):
    p = os.path.join(d, "inbox", f"{n}.json")
    with open(p) as f:
        j = json.load(f)
    gt = f"t={j['game_t']:.1f}" if isinstance(j.get("game_t"), (int, float)) else "t=?"
    head = (f"note {j['n']} - mark {j['mark']} ({gt}, {j.get('wall', '?')}"
            + (f"; {j['audio_s']} s of voice, transcribed in {j['stt_s']} s, in the inbox {j['latency_s']} s after "
               f"the note ended" if j.get("stt_s") is not None else f"; {j.get('why') or 'no voice'}") + ")")
    lines = [head, f"  said: {j.get('transcript') or '(nothing transcribed)'}"]
    if j.get("update_of"):  # b37: a late transcript for an earlier note
        lines[0] += f" - THE WORDS OF NOTE {j['update_of']}"
    for s in (j.get("state") or [])[:9]:
        lines.append(f"  | {s}")
    pics = j.get("pictures") or {}
    lines.append("  pictures: " + "  ".join(f"{k} {v}" for k, v in pics.items()))
    lines.append(f"  folder: {j.get('dir')}")
    lines.append(f"  json: {p}")
    return "\n".join(lines)


def cmd_wait(a):
    d = live_dir(a.dir)
    have = notes(d)
    since = a.since if a.since is not None else (have[-1] if have else 0)
    t0 = time.monotonic()
    while True:
        new = [n for n in notes(d) if n > since]
        if new:
            print(fmt_note(d, new[0]))
            return 0
        if not os.path.exists(os.path.join(d, "on")):
            st = read(os.path.join(d, "daemon.json"))
            print(f"live: the session {os.path.basename(d)} is not on ({len(notes(d))} notes){'' if st else ' - no daemon.json'}")
            return 3
        if a.timeout and time.monotonic() - t0 >= a.timeout:
            print(f"live: no new note within {a.timeout:.0f} s (newest {since})")
            return 4
        time.sleep(0.3)


def say_items(d):
    out = []
    try:
        names = os.listdir(os.path.join(d, "say"))
    except OSError:
        return out
    for x in sorted((int(m.group(1)) for m in (re.fullmatch(r"(\d+)\.txt", y) for y in names) if m)):
        out.append({"n": x, "shown": os.path.exists(os.path.join(d, "say", f"{x}.shown")),
                    "read": os.path.exists(os.path.join(d, "say", f"{x}.read"))})
    return out


def patch_ids(d):
    try:
        return sorted(int(x) for x in os.listdir(os.path.join(d, "patches")) if x.isdigit())
    except OSError:
        return []


def cmd_status(a):
    d = live_dir(a.dir)
    dj = {}
    try:
        with open(os.path.join(d, "daemon.json")) as f:
            dj = json.load(f)
    except (OSError, ValueError):
        pass
    on = os.path.exists(os.path.join(d, "on")) and dj.get("alive")
    age = None
    try:
        age = time.time() - os.stat(os.path.join(d, "daemon.json")).st_mtime
    except OSError:
        pass
    st = kv(read(os.path.join(d, "patches", "state.txt")))
    ready = (read(os.path.join(d, "patches", "ready")) or "").strip()
    says = say_items(d)
    lane = subprocess.run([os.path.join(HERE, "live_lane.sh"), "status"], capture_output=True, text=True).stdout.strip()
    res = {"dir": d, "session": os.path.basename(d), "on": bool(on), "daemon_age_s": round(age, 1) if age else None,
           "stt": dj.get("stt"), "model": dj.get("model"), "notes": len(notes(d)), "waiting": dj.get("waiting", []),
           "messages": len(says), "shown": sum(s["shown"] for s in says), "read": sum(s["read"] for s in says),
           "patches": patch_ids(d), "ready": ready, "current": st.get("current", "0"), "last": st, "lane": lane,
           "tts": dj.get("tts"), "spoken": dj.get("spoken", 0)}
    if a.json:
        print(json.dumps(res, indent=1))
        return 0
    print(f"live {res['session']}: {'ON' if on else 'off'}"
          + (f" (the daemon {age:.0f} s ago, stt {res['stt']} {res['model']})" if age is not None else " (no daemon)"))
    nn = notes(d)
    newest = ""
    if nn:
        try:
            with open(os.path.join(d, "inbox", f"{nn[-1]}.json")) as f:
                j = json.load(f)
            newest = f"; newest {nn[-1]}: {(j.get('transcript') or j.get('why') or '')[:90]!r}"
        except (OSError, ValueError):
            pass
    print(f"  notes: {len(nn)}{newest}{'; waiting for ' + ', '.join(res['waiting']) if res['waiting'] else ''}")
    print(f"  messages: {res['messages']} sent, {res['shown']} shown, {res['read']} read; spoken lines: "
          f"{res['spoken']} (tts {res['tts'] or 'off'})")
    print(f"  patches: {res['patches'] or 'none'}; ready {ready or '-'}; live now {res['current']}"
          + (f"; last answer {st.get('seq')}: {st.get('last')} {st.get('id')} - {st.get('msg', '')[:80]}" if st else ""))
    print(f"  lane: {lane}")
    return 0


LV_COLS, LV_LINES = 29, 6   # the card (somvr_live.h SOMVR_LV_COLS, SOMVR_LV_LINES)
SAY_MAX = 1500              # b37 live3: a longer answer is refused (pages, never a cut)
FOLD = {"\u2018": "'", "\u2019": "'", "\u2032": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "-",
        "\u2212": "-", "\u2026": ".", "\u00a0": " ", "\t": " ", "\r": ""}


def lv_fold(text):
    """the card's ASCII (somvr_live.c fold(): quotes, dashes, the ellipsis; '?' for the rest; the degree sign kept)"""
    return "".join(FOLD.get(c, c if ord(c) < 0x80 or c == "\u00b0" else "?") for c in text)


def lv_wrap(text, cols=LV_COLS):
    """every line of the card's wrap (somvr_lv_wrap without its line limit): words, line breaks kept, a word longer
    than a line cut"""
    lines = []
    for para in lv_fold(text).split("\n"):
        cur = ""
        for w in para.split():
            while len(w) > cols:   # (the C cuts a long word at the line's end and goes on)
                if cur:
                    lines.append(cur)
                    cur = ""
                lines.append(w[:cols])
                w = w[cols:]
            if not cur:
                cur = w
            elif len(cur) + 1 + len(w) <= cols:
                cur += " " + w
            else:
                lines.append(cur)
                cur = w
        lines.append(cur)
    while lines and not lines[-1]:
        lines.pop()
    return lines


def say_pages(text):
    """b37 live3 (162.9): the cards of one answer - one when it fits (6 lines of 29), else pages of 5 lines and
    "(K of N: tap for the next)" / "(N of N)" - never a cut with "..." (the user's note: a long answer ended in dots)"""
    lines = lv_wrap(text)
    if len(lines) <= LV_LINES:
        return [text]
    per = LV_LINES - 1
    chunks = [lines[i:i + per] for i in range(0, len(lines), per)]
    n = len(chunks)
    return ["\n".join(c + [f"({k + 1} of {n}: tap for the next)" if k + 1 < n else f"({n} of {n})"])
            for k, c in enumerate(chunks)]


def cmd_say(a):
    d = live_dir(a.dir)
    sd = os.path.join(d, "say")
    if not os.path.isdir(sd):
        print(f"live: no session folder {d}", file=sys.stderr)
        return 2
    text = " ".join(a.text).strip()
    if not text:
        print("live: say what?", file=sys.stderr)
        return 2
    if len(text.encode()) > SAY_MAX:
        print(f"live: {len(text.encode())} bytes - say it in {SAY_MAX} or less (it would be cut otherwise)", file=sys.stderr)
        return 2
    have = [s["n"] for s in say_items(d)]
    n = (have[-1] if have else 0) + 1
    pages = say_pages(text)
    # every side file before the cards (the daemon reads <n>.voice / .quiet when <n>.txt appears)
    if len(pages) > 1 and not a.quiet:   # the whole answer said once, with its first page; the others quiet
        write_atomic(os.path.join(sd, f"{n}.voice"), text + "\n")
    for k in range(len(pages)):
        if a.quiet or k > 0:  # b37: every card is spoken (SOMVR_LIVE_TTS=on) unless it says so
            write_atomic(os.path.join(sd, f"{n + k}.quiet"), "1\n")
    for k, pg in enumerate(pages):
        write_atomic(os.path.join(sd, f"{n + k}.txt"), pg + "\n")
    on = os.path.exists(os.path.join(d, "on"))
    print(f"say {n}{f'..{n + len(pages) - 1} ({len(pages)} pages)' if len(pages) > 1 else ''}: queued"
          f"{'' if on else ' (the session is not on: it shows if the game starts with this folder)'} -"
          f" the headset shows it at the next calm moment")
    return 0


# ---- the patches ----------------------------------------------------------------------------------------------------
def pe_exports(path):
    """the export names of a PE DLL (stdlib): [] when it has none, None when it is no PE"""
    with open(path, "rb") as f:
        b = f.read()
    if b[:2] != b"MZ" or len(b) < 0x40:
        return None
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[pe:pe + 4] != b"PE\0\0":
        return None
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    optsz = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", b, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    erva, esz = struct.unpack_from("<II", b, dd)
    secs = []
    for i in range(nsec):
        s = opt + optsz + 40 * i
        vsz, va, rsz, rptr = struct.unpack_from("<IIII", b, s + 8)
        secs.append((va, max(vsz, rsz), rptr))

    def off(rva):
        for va, sz, rp in secs:
            if va <= rva < va + sz:
                return rva - va + rp
        return None

    if not erva:
        return []
    eo = off(erva)
    if eo is None:
        return []
    nnames, names_rva = struct.unpack_from("<I", b, eo + 24)[0], struct.unpack_from("<I", b, eo + 32)[0]
    no = off(names_rva)
    out = []
    for i in range(nnames):
        o = off(struct.unpack_from("<I", b, no + 4 * i)[0])
        e = b.index(b"\0", o)
        out.append(b[o:e].decode(errors="replace"))
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
LAYER_SRC = ["vklayer/somvr_vklayer.c", "vklayer/somvr_logic.c", "vklayer/somvr_env.c", "vklayer/somvr_handdraw.c",
             "vklayer/somvr_void.c", "somvr_pointer.c", "somvr_comfort.c", "somvr_menus.c", "somvr_vrmenu.c",
             "somvr_font.c", "somvr_threat.c", "somvr_vrhands.c", "somvr_objective.c", "somvr_live.c"]
HOST_SRC = ["win/somvr_win_host.c", "win/somvr_win_hook.c", "win/somvr_win_d3d.c", "win/somvr_win_main.c",
            "win/somvr_win_bridge.c"]


def closure(mod, roots):
    """the files the roots compile: the roots and every "header" or .c they include (by the including file's folder,
    then mod/), recursively"""
    seen, todo = set(), [os.path.join(mod, r) for r in roots if os.path.exists(os.path.join(mod, r))]
    while todo:
        p = os.path.normpath(todo.pop())
        if p in seen:
            continue
        seen.add(p)
        txt = read(p) or ""
        for inc in INC.findall(txt):
            for base in (os.path.dirname(p), mod, os.path.join(mod, "vklayer"), os.path.join(mod, "win")):
                q = os.path.normpath(os.path.join(base, inc))
                if os.path.isfile(q):
                    todo.append(q)
                    break
    return sorted(seen)


def fingerprint(mod):
    mod = os.path.realpath(mod)
    out = {"mod": mod}
    for name, roots in (("layer", LAYER_SRC), ("host", HOST_SRC)):
        files = closure(mod, roots)
        h = hashlib.sha256()
        for p in files:
            h.update(os.path.relpath(p, mod).encode() + b"\0" + sha256(p).encode() + b"\n")
        out[name] = h.hexdigest()[:24] if files else "-"
        out[name + "_files"] = [os.path.relpath(p, mod) for p in files]
    return out


def cmd_fingerprint(a):
    fp = fingerprint(a.mod)
    print(f"mod={fp['mod']}\nlayer={fp['layer']}\nhost={fp['host']}\nat={time.strftime('%Y-%m-%d %H:%M:%S')}")
    return 0


def changed_files(mod_a, mod_b, names):
    out = []
    for r in names:
        pa, pb = os.path.join(mod_a, r), os.path.join(mod_b, r)
        ea, eb = os.path.exists(pa), os.path.exists(pb)
        if ea != eb or (ea and sha256(pa) != sha256(pb)):
            out.append(r)
    return out


def cmd_patch_check(a):
    mod = os.path.realpath(a.mod)
    res = {}
    for name, cmd in (("build", [os.path.join(mod, "build.sh"), "--check"]), ("win", [os.path.join(mod, "win", "build.sh"), "--check"])):
        t0 = time.monotonic()
        log = os.path.join(mod, "build", f"live-check-{name}.log")
        os.makedirs(os.path.dirname(log), exist_ok=True)
        with open(log, "w") as f:
            r = subprocess.run([os.path.join(HERE, "live_lane.sh"), "exec", "build"] + cmd, cwd=mod, stdout=f,
                               stderr=subprocess.STDOUT)
        res[name] = "ok" if r.returncode == 0 else f"FAILED (exit {r.returncode}, {log})"
        print(f"check {name}: {res[name]} in {time.monotonic() - t0:.0f} s")
        if r.returncode:
            return 1
    dll = os.path.join(mod, "build", "win", "somvr_logic.dll")
    if not os.path.exists(dll):
        print(f"check: no {dll} (the tree has no host/logic split: nothing to patch live)")
        return 1
    write_atomic(os.path.join(mod, "build", "live-checks.txt"),
                 f"dll_sha={sha256(dll)}\nbuild={res['build']}\nwin={res['win']}\nat={time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    print(f"checked: {dll} ({sha256(dll)[:16]})")
    return 0


def cmd_patch_stage(a):
    d = live_dir(a.dir)
    pd = os.path.join(d, "patches")
    if not os.path.isdir(pd):
        print(f"live: no session folder {d}", file=sys.stderr)
        return 2
    for nm, v in (("what", a.what), ("try", a.try_)):   # b37 live3: never cut silently (the card and the box show them)
        if len(" ".join(v.split())) > 150:
            print(f"live: the patch's '{nm}' is {len(' '.join(v.split()))} characters: say it in 150 or less "
                  f"(details in live_say)", file=sys.stderr)
            return 2
    dll = os.path.realpath(a.dll)
    ex = pe_exports(dll) if os.path.isfile(dll) else None
    if ex is None:
        print(f"live: {dll} is not a PE DLL", file=sys.stderr)
        return 2
    if "somvr_logic_abi" not in ex and not a.allow_no_abi:
        print(f"live: {dll} has no somvr_logic_abi export (not hotreload's logic module)", file=sys.stderr)
        return 2
    mod = os.path.realpath(a.tree or os.path.join(os.path.dirname(dll), "..", ".."))
    sha = sha256(dll)
    chk = kv(read(os.path.join(mod, "build", "live-checks.txt")))
    checked = chk.get("dll_sha") == sha and chk.get("build") == "ok" and chk.get("win") == "ok"
    if not checked and not a.force_unchecked:
        print(f"live: {dll} has not passed both build checks (live.py patch check {mod}; its live-checks.txt "
              f"{'names another DLL' if chk else 'is missing'})", file=sys.stderr)
        return 2
    kind, why = "live", ""
    bt = kv(read(os.path.join(d, "build.txt")))
    if bt.get("mod"):
        fp = fingerprint(mod)
        what = []
        if bt.get("layer") and fp["layer"] != bt["layer"]:
            what.append("the layer")
        if bt.get("host") and bt["host"] != "-" and fp["host"] != bt["host"]:
            what.append("the host")
        if what:
            kind = "restart"
            files = changed_files(bt["mod"], mod, sorted(set(fp["layer_files"] + fp["host_files"])))
            why = f"It changes {' and '.join(what)} ({', '.join(files[:3]) or 'their files'}): it needs a game restart."
    else:
        why = ""
    ids = patch_ids(d)
    pid = (ids[-1] if ids else 0) + 1
    pdir = os.path.join(pd, str(pid))
    os.makedirs(pdir)
    shutil.copy2(dll, os.path.join(pdir, "somvr_logic.dll"))
    one = lambda s: " ".join(s.split())[:150]
    write_atomic(os.path.join(pdir, "patch.txt"),
                 f"id={pid}\nkind={kind}\nwhat={one(a.what)}\ntry={one(a.try_)}\nwhy={why}\nsha={sha}\nsrc={dll}\n"
                 f"tree={mod}\nchecked={'yes' if checked else 'NO (forced)'}\nstaged={time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    write_atomic(os.path.join(pd, "ready"), f"{pid}\n")
    with open(os.path.join(pd, "log.txt"), "a") as f:
        f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} staged patch {pid} ({kind}{', unchecked' if not checked else ''}): "
                f"{one(a.what)} / try: {one(a.try_)} ({sha[:16]})\n")
    print(f"patch {pid} staged ({kind}{': ' + why if why else ''}); the headset shows 'Patch {pid} ready' and the VR "
          f"menu's Test page has Apply patch")
    # b37 live3: build 37's layer cuts a long patch card at its 6th line (the 22:13 session); a layer that pages it
    # says so in DIR/layer.caps ("pages"). Else the whole of it follows as a quiet paged message
    card = [f"{one(a.what)}", f"Try: {one(a.try_)}" if one(a.try_) else "", "Apply it: menu > Test tab"] if kind == "live" \
        else [one(a.what), why]
    lines = sum(len(lv_wrap(x)) for x in card if x)
    caps = (read(os.path.join(d, "layer.caps")) or "").split()
    if lines > LV_LINES and "pages" not in caps:
        full = f"Patch {pid} in full:\n{one(a.what)}" + (f"\nTry: {one(a.try_)}" if one(a.try_) else "")
        sd = os.path.join(d, "say")
        have = [s_["n"] for s_ in say_items(d)]
        n = (have[-1] if have else 0) + 1
        pages = say_pages(full)
        for k in range(len(pages)):
            write_atomic(os.path.join(sd, f"{n + k}.quiet"), "1\n")
        for k, pg in enumerate(pages):
            write_atomic(os.path.join(sd, f"{n + k}.txt"), pg + "\n")
        print(f"  (the session's layer cuts a patch card at 6 lines - this one has {lines}: its whole text follows as "
              f"message{'s' if len(pages) > 1 else ''} {n}{f'..{n + len(pages) - 1}' if len(pages) > 1 else ''}, quiet)")
    return 0


def cmd_patch_list(a):
    d = live_dir(a.dir)
    st = kv(read(os.path.join(d, "patches", "state.txt")))
    ready = (read(os.path.join(d, "patches", "ready")) or "").strip()
    for pid in patch_ids(d):
        p = kv(read(os.path.join(d, "patches", str(pid), "patch.txt")))
        mark = " <- live" if st.get("current") == str(pid) else " (ready)" if ready == str(pid) else ""
        print(f"patch {pid} [{p.get('kind')}, checked {p.get('checked')}] {p.get('what')} / try: {p.get('try')}{mark}")
    log = read(os.path.join(d, "patches", "log.txt"))
    if log:
        print("log:")
        for ln in log.strip().splitlines()[-12:]:
            print("  " + ln)
    return 0


def main():
    ap = argparse.ArgumentParser(prog="live.py")
    sp = ap.add_subparsers(dest="cmd", required=True)
    w = sp.add_parser("wait")
    w.add_argument("since", nargs="?", type=int)
    w.add_argument("--dir")
    w.add_argument("--timeout", type=float, default=0)
    s = sp.add_parser("status")
    s.add_argument("--dir")
    s.add_argument("--json", action="store_true")
    y = sp.add_parser("say")
    y.add_argument("text", nargs="+")
    y.add_argument("--dir")
    y.add_argument("--quiet", action="store_true", help="the card only, not spoken")
    y.add_argument("--speak", action="store_true", help=argparse.SUPPRESS)  # (build 35's; every card is spoken now)
    f = sp.add_parser("fingerprint")
    f.add_argument("mod")
    p = sp.add_parser("patch")
    pp = p.add_subparsers(dest="pcmd", required=True)
    st = pp.add_parser("stage")
    st.add_argument("dll")
    st.add_argument("what")
    st.add_argument("try_", metavar="try")
    st.add_argument("--dir")
    st.add_argument("--tree")
    st.add_argument("--force-unchecked", action="store_true")
    st.add_argument("--allow-no-abi", action="store_true", help="tests only: a DLL without the logic ABI export")
    ck = pp.add_parser("check")
    ck.add_argument("mod")
    ls = pp.add_parser("list")
    ls.add_argument("--dir")
    a = ap.parse_args()
    if a.cmd == "wait":
        return cmd_wait(a)
    if a.cmd == "status":
        return cmd_status(a)
    if a.cmd == "say":
        return cmd_say(a)
    if a.cmd == "fingerprint":
        return cmd_fingerprint(a)
    return {"stage": cmd_patch_stage, "check": cmd_patch_check, "list": cmd_patch_list}[a.pcmd](a)


if __name__ == "__main__":
    sys.exit(main())
