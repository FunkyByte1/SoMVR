#!/usr/bin/env python3
"""somvr_window.py - the small PC window of a Shadow of Mordor VR session (the user after headset session 2: "I
couldn't figure out how to close the game. I had to top and kill the process. I think it should open a window on the pc
that I can see when the game is running").

    somvr_window.py [--unit somvr-headset] [--since EPOCH] [--run DIR] [--build NAME] [--every SECS] [--close-after SECS]

It shows: the session's state (starting / loading / in the game / ending / ended) and how long it has run; a live
picture of the game (gamescope's own screenshot of the session's headless screen, every 2 s (--every), made small);
a Stop button (`systemctl --user stop UNIT`: boot_check's cleanup stops the game and checks the save; without a unit:
RUN/quit.request, the harness's clean stop); the bug marks taken (RUN/marks.log; a double click opens the mark's
folder). It closes by itself CLOSE_AFTER seconds (default 30) after the session ended, showing the save check.

The run folder: --run, else the newest logs/boot-check-<ts> made after --since (scripts/som-vr-headset.sh passes its
start time). The game never sees this window: it runs in its own headless gamescope (no desktop window, xdotool goes to
that nested display), so the window cannot take its input focus; the pictures are gamescope's work (its screenshot
path) and this process runs at nice 10; RUN/window.log has the cost (the screenshot's time, gamescope's CPU per shot).
Needs python3's tkinter and PIL (ImageTk); nothing else.
"""
import argparse
import os
import queue
import re
import subprocess
import sys
import threading
import time

SOM = "/home/bara/Projects/flat2vr/ShadowOfMordor"
RT = os.environ.get("XDG_RUNTIME_DIR") or "/run/user/%d" % os.getuid()


def sh(argv, timeout=5):
    try:
        return subprocess.run(argv, capture_output=True, text=True, timeout=timeout).stdout.strip()
    except Exception:
        return ""


def read_text(path, limit=1 << 20):
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            n = f.tell()
            f.seek(max(0, n - limit))
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def find_run(since):
    """the newest logs/boot-check-<ts> folder made at or after since - 5 (not a dry run)"""
    best = None
    try:
        for e in os.scandir(os.path.join(SOM, "logs")):
            if not e.name.startswith("boot-check-2") or not e.is_dir():
                continue
            try:
                m = e.stat().st_mtime
            except OSError:
                continue
            if m >= since - 5 and (best is None or e.name > best[0]):
                best = (e.name, e.path)
    except OSError:
        pass
    return best[1] if best else None


class Session:
    """what the run folder says: the state, the times, the save, the marks"""

    def __init__(self, unit, run):
        self.unit, self.run = unit, run
        self.t_game = self.t_stop = None  # the game's start (run.log's "game started:" time), the end (clock frozen)
        self.in_world = False
        self.state, self.detail = "starting", "the game is starting"

    def ui_scan(self, somlog):
        """somvr.log's `SESSION ui ifs= menus= paused= video= ctx= t=` lines (SOMVR_INPUT=1): the last one's fields;
        in_world (sticky) once a ctx=world line stood 5 s (the story's load starts with a moment of ctx=world). "Now" =
        the last RATE line's t (the same clock, every second)"""
        lines = re.findall(r"^SESSION ui ((?:\w+=\S+ ?)+)", somlog, re.M)
        rate = re.findall(r"^RATE t=([0-9.]+)", somlog[-20000:], re.M)
        now = float(rate[-1]) if rate else None
        recs = [dict(kv.split("=", 1) for kv in l.split()) for l in lines]
        for i, r in enumerate(recs):
            if self.in_world or r.get("ctx") != "world":
                continue
            try:
                t0 = float(r.get("t", "x"))
                t1 = float(recs[i + 1].get("t", "x")) if i + 1 < len(recs) else now
            except ValueError:
                continue
            if t1 is not None and t1 - t0 >= 5:
                self.in_world = True
        return recs[-1] if recs else {}

    @staticmethod
    def started_at(runlog):
        """epoch of run.log's `HH:MM:SS +Ns game started:` line (today; yesterday across midnight)"""
        m = re.search(r"^(\d\d):(\d\d):(\d\d) \+\d+s game started:", runlog, re.M)
        if not m:
            return None
        lt = time.localtime()
        t = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, int(m.group(1)), int(m.group(2)), int(m.group(3)), 0, 0, -1))
        return t - 86400 if t > time.time() + 60 else t

    def unit_state(self):
        if not self.unit:
            return ""
        return sh(["systemctl", "--user", "is-active", self.unit]) or "inactive"

    def update(self):
        us = self.unit_state()
        runlog = read_text(os.path.join(self.run, "run.log")) if self.run else ""
        somlog = read_text(os.path.join(self.run, "somvr.log"), 1 << 18) if self.run else ""
        ended = (self.unit and us not in ("active", "activating", "deactivating", "reloading")) or (
            self.run and os.path.exists(os.path.join(self.run, "summary.txt")))
        if self.t_game is None and "game started:" in runlog:
            self.t_game = self.started_at(runlog) or time.time()
        last = self.ui_scan(somlog)  # the last `SESSION ui` line's fields; in_world once the world stood 5 s
        if " MARK world at game time" in runlog:
            self.in_world = True  # (the start sequence's `mark world`: also a build without SESSION lines)
        if "REFUSED:" in runlog:
            line = runlog[runlog.index("REFUSED:"):].split("\n", 1)[0]
            self.state, self.detail = "did not start", line[len("REFUSED:"):].strip()
        elif ended:
            self.state = "ended"
            self.detail = self.save_text(runlog)
        elif us == "deactivating" or "stopping the game" in runlog or "quit requested" in runlog:
            self.state, self.detail = "ending", "stopping the game, then checking your save (about a minute)"
        elif self.in_world:
            self.state = "in the game"
            self.detail = "playing" if last.get("ctx") == "world" else "in a menu, or loading"
        elif "game started:" in runlog:
            self.state = "loading"
            self.detail = ("at the main menu (the game goes on by itself)" if last.get("ifs") == "1" and
                           last.get("menus") == "15" else "the game is loading (you wait in the headset's loading space)")
        else:
            self.state, self.detail = "starting", "the session is starting"
        if self.state in ("ending", "ended", "did not start") and self.t_stop is None:
            self.t_stop = time.time()
        return self.state

    @staticmethod
    def save_text(runlog):
        if "GameData.sav UNCHANGED" in runlog or "SAVE OK" in runlog:
            return "ended - your real save is unchanged"
        if "RESTORED, verified" in runlog:
            return "ended - your real save was restored from the backup (tell Claude)"
        if "GameData.sav CHANGED" in runlog or "SAVE CHANGED" in runlog:
            return "ended - the save check found a change (tell Claude)"
        return "ended - checking the save..." if runlog else "ended"

    def marks(self):
        """[(text, folder name)] from RUN/marks.log (boot_check: `HH:MM:SS DIR bug mark #N (how) DATE TIME[ - note:
        NOTE] | voice: recording SRC|none`)"""
        txt = read_text(os.path.join(self.run, "marks.log")) if self.run else ""
        out = []
        for l in txt.splitlines():
            m = re.match(r"(\S+) (\S+) bug mark #(\d+) \(([^)]*)\)(?:[^|]*? - note: ([^|]*?))?\s*\| voice: (\S+)", l)
            if m:
                out.append(("%s  mark %s  %s%s - %s" % (m.group(1), m.group(3), m.group(4),
                                                       (": " + m.group(5).strip()) if m.group(5) else "",
                                                       "voice recorded" if m.group(6) == "recording" else "no voice"),
                            m.group(2)))
            elif l.strip():
                out.append((l.strip(), l.split()[1] if len(l.split()) > 1 else ""))
        return out

    def gamescope(self):
        """the session's gamescope: its wayland socket name and its pid (from RUN/vdisplay.env)"""
        env = read_text(os.path.join(self.run, "vdisplay.env")) if self.run else ""
        vals = dict(l.split("=", 1) for l in env.splitlines() if "=" in l)
        pid, gs = vals.get("CHILD_PID", ""), ""
        for _ in range(4):  # up from boot_check's placeholder child: gamescopereaper, then gamescope
            if not pid.isdigit():
                break
            try:
                with open("/proc/%s/stat" % pid) as f:
                    st = f.read()
                comm, pid = st[st.index("(") + 1:st.rindex(")")], st.rsplit(")", 1)[1].split()[1]
            except (OSError, ValueError, IndexError):
                break
            if comm.startswith("gamescope") and comm != "gamescopereaper":  # ("gamescope-wl" here)
                gs = st.split(" ", 1)[0]
                break
        return vals.get("GAMESCOPE_WAYLAND_DISPLAY", ""), gs


def cpu_ms(pid):
    try:
        with open("/proc/%s/stat" % pid) as f:
            p = f.read().rsplit(")", 1)[1].split()
        return (int(p[11]) + int(p[12])) * 1000.0 / os.sysconf("SC_CLK_TCK")
    except (OSError, IndexError, ValueError):
        return None


class Shooter(threading.Thread):
    """every `every` seconds: gamescope's screenshot of the session's screen, made small (PIL), into a queue"""

    def __init__(self, sess, out, every, logf):
        super().__init__(daemon=True)
        self.sess, self.out, self.every, self.logf = sess, out, every, logf
        self.dir = os.path.join(RT, "somvr-window")
        os.makedirs(self.dir, exist_ok=True)
        self.stop = False
        self.n = self.fails = 0
        self.t_sum = 0.0
        self.last_log = time.time()
        self.win = None  # (wall, gamescope CPU ms, pictures) at the start of this log interval

    def log(self, s):
        try:
            with open(self.logf, "a") as f:
                f.write("%s %s\n" % (time.strftime("%H:%M:%S"), s))
        except OSError:
            pass

    def log_rate(self, gpid):
        """RUN/window.log: the pictures so far, their time; gamescope's CPU over the interval (its own frames too)"""
        now, c = time.time(), cpu_ms(gpid) if gpid else None
        rate = ""
        if self.win and self.win[1] is not None and c is not None and now > self.win[0]:
            rate = "; gamescope's CPU %.1f %% of a core over the last %.0f s (%d pictures; its frames included)" % (
                (c - self.win[1]) / (now - self.win[0]) / 10.0, now - self.win[0], self.n - self.win[2])
        self.log("pictures %d (failed %d): %.0f ms each (gamescope's screenshot, the file, read, shrink)%s" % (
            self.n, self.fails, 1000 * self.t_sum / max(1, self.n), rate))
        self.last_log, self.win = now, (now, c, self.n) if c is not None else None

    def run(self):
        from PIL import Image
        while not self.stop:
            t0 = time.time()
            wd, gpid = self.sess.gamescope() if self.sess.run else ("", "")
            if wd and self.sess.state in ("loading", "in the game", "starting"):
                path = os.path.join(self.dir, "live-%d.png" % (self.n % 2))
                try:
                    os.unlink(path)
                except OSError:
                    pass
                if self.win is None and gpid:
                    self.win = (t0, cpu_ms(gpid), self.n)
                env = dict(os.environ, GAMESCOPE_WAYLAND_DISPLAY=wd, XDG_RUNTIME_DIR=RT)
                try:
                    subprocess.run(["gamescopectl", "screenshot", path], env=env, capture_output=True, timeout=5)
                except Exception:
                    pass
                img, last = None, -1
                for _ in range(40):  # gamescope writes the file itself, a moment later
                    try:
                        sz = os.path.getsize(path)
                    except OSError:
                        sz = -1
                    if sz > 0 and sz == last:
                        try:
                            im = Image.open(path)
                            im.load()
                            im.thumbnail((560, 315))
                            img = im.convert("RGB")
                        except Exception:
                            img = None
                        break
                    last = sz
                    time.sleep(0.05)
                dt = time.time() - t0
                if img is not None:
                    self.n += 1
                    self.t_sum += dt
                    try:
                        self.out.put_nowait(img)
                    except queue.Full:
                        pass
                else:
                    self.fails += 1
                if time.time() - self.last_log >= 60 and self.n:
                    self.log_rate(gpid)
            time.sleep(max(0.1, self.every - (time.time() - t0)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--unit", default="somvr-headset", help="the session's systemd user unit ('' = none: a test run)")
    ap.add_argument("--since", type=float, default=time.time(), help="the session's start (epoch seconds)")
    ap.add_argument("--run", default="", help="the run folder (default: the newest after --since)")
    ap.add_argument("--build", default="", help="the build's name, for the title")
    ap.add_argument("--every", type=float, default=2.0, help="seconds between pictures (default 2)")
    ap.add_argument("--close-after", type=float, default=30.0, help="close this long after the session ended")
    ap.add_argument("--check", action="store_true", help="only check the modules (tkinter, PIL); no window")
    a = ap.parse_args()
    try:
        os.nice(10)
    except OSError:
        pass
    import tkinter as tk
    from tkinter import messagebox
    from PIL import Image, ImageTk
    if a.check:
        print("ok: tkinter (Tk %s), PIL %s" % (tk.TkVersion, getattr(Image, "__version__", "?")))
        return 0

    sess = Session(a.unit, a.run or None)
    root = tk.Tk()
    root.title("Shadow of Mordor VR" + (" (%s)" % a.build if a.build else ""))
    root.configure(bg="#16191f")
    root.resizable(False, False)
    fg, dim, acc = "#e6e8ec", "#8a919c", "#e8c070"
    title = tk.Label(root, text="Shadow of Mordor VR", fg=acc, bg="#16191f", font=("Sans", 15, "bold"))
    title.pack(anchor="w", padx=12, pady=(10, 0))
    state = tk.Label(root, text="starting", fg=fg, bg="#16191f", font=("Sans", 12))
    state.pack(anchor="w", padx=12)
    detail = tk.Label(root, text="", fg=dim, bg="#16191f", font=("Sans", 10), wraplength=560, justify="left")
    detail.pack(anchor="w", padx=12)
    pic = tk.Label(root, bg="#000000", width=560, height=315)
    pic.pack(padx=12, pady=8)
    row = tk.Frame(root, bg="#16191f")
    row.pack(fill="x", padx=12)
    marks_box = tk.Listbox(root, height=6, bg="#20242c", fg=fg, selectbackground="#3c4a60", borderwidth=0,
                           highlightthickness=0, font=("Monospace", 9))
    note = tk.Label(root, text="Bug marks: in the headset hold B and Y together for half a second and let go (two "
                               "buzzes), or the wrist menu's Report button. Then say what is wrong (about 10 seconds; "
                               "the microphone must be on in the WiVRn app).", fg=dim, bg="#16191f", font=("Sans", 9), wraplength=560, justify="left")
    note.pack(anchor="w", padx=12, pady=(8, 2))
    marks_box.pack(fill="x", padx=12, pady=(0, 12))
    q = queue.Queue(maxsize=2)
    shooter = None
    photo = {"img": None}
    ended_at = {"t": None}
    t_start = time.time()

    def stop_game(ask=True):
        if sess.state in ("ended", "did not start"):
            return True
        if ask and not messagebox.askyesno("Stop Shadow of Mordor VR?",
                                           "Stop the game now?\n\nYour real save is not touched (this session plays on a "
                                           "copy). The game stops, then the save check runs (about a minute).",
                                           parent=root):
            return False
        if sess.unit:
            subprocess.Popen(["systemctl", "--user", "stop", "--no-block", sess.unit])
        elif sess.run:
            with open(os.path.join(sess.run, "quit.request"), "w") as f:
                f.write("the PC window's Stop at %d\n" % time.time())
        state.config(text="ending")
        detail.config(text="stopping the game, then checking your save (about a minute)")
        return True

    def open_folder(path):
        if path and os.path.isdir(path):
            subprocess.Popen(["xdg-open", path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    stop_btn = tk.Button(row, text="Stop the game", command=stop_game, bg="#a83428", fg="white",
                         activebackground="#d04838", activeforeground="white", relief="flat", padx=14, pady=4)
    stop_btn.pack(side="left")
    logs_btn = tk.Button(row, text="Open the session's folder", command=lambda: open_folder(sess.run), bg="#343d4c",
                         fg="white", activebackground="#4a5870", activeforeground="white", relief="flat", padx=10, pady=4)
    logs_btn.pack(side="left", padx=8)
    clock = tk.Label(row, text="", fg=dim, bg="#16191f", font=("Sans", 10))
    clock.pack(side="right")

    mark_dirs = []  # the marks' folder names, in the list's order

    def on_mark(_e):
        sel = marks_box.curselection()
        if sel and sess.run and sel[0] < len(mark_dirs) and mark_dirs[sel[0]]:
            open_folder(os.path.join(sess.run, "marks", mark_dirs[sel[0]]))

    marks_box.bind("<Double-Button-1>", on_mark)

    def on_close():
        if sess.state in ("ended", "did not start"):
            root.destroy()
            return
        r = messagebox.askyesnocancel("Close this window?", "Stop the game too?\n\nYes: stop the game (the save check "
                                      "runs).\nNo: keep playing; only this window closes.", parent=root)
        if r is None:
            return
        if r:
            stop_game(ask=False)
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)

    def fmt(s):
        s = int(max(0, s))
        return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60) if s >= 3600 else "%d:%02d" % (s // 60, s % 60)

    def tick():
        nonlocal shooter
        if not sess.run:
            sess.run = find_run(a.since)
        st = sess.update()
        state.config(text={"in the game": "In the game", "loading": "Loading", "starting": "Starting",
                           "ending": "Ending", "ended": "Ended", "did not start": "Did not start"}.get(st, st))
        detail.config(text=sess.detail)
        now = sess.t_stop or time.time()  # (frozen once the session ends)
        clock.config(text=("%s in the game" % fmt(now - sess.t_game)) if sess.t_game else fmt(now - t_start))
        if sess.run and shooter is None:
            shooter = Shooter(sess, q, a.every, os.path.join(sess.run, "window.log"))
            shooter.start()
        ms = sess.marks()
        if marks_box.size() != len(ms):
            marks_box.delete(0, "end")
            mark_dirs[:] = [d for _, d in ms]
            for text, _ in ms:
                marks_box.insert("end", text)
            marks_box.see("end")
        if st in ("ended", "did not start"):
            stop_btn.config(state="disabled")
            if ended_at["t"] is None:
                ended_at["t"] = time.time()
            left = a.close_after - (time.time() - ended_at["t"])
            detail.config(text="%s (this window closes in %d s)" % (sess.detail, max(0, left)))
            if left <= 0:
                if shooter:
                    shooter.stop = True
                root.destroy()
                return
        root.after(1000, tick)

    def picture():
        try:
            img = q.get_nowait()
            photo["img"] = ImageTk.PhotoImage(img)
            pic.config(image=photo["img"], width=img.width, height=img.height)
        except queue.Empty:
            pass
        root.after(200, picture)

    root.after(100, tick)
    root.after(300, picture)
    root.mainloop()
    if shooter:
        shooter.stop = True
        shooter.log("closed (%s)" % sess.state)
    return 0


if __name__ == "__main__":
    sys.exit(main())
