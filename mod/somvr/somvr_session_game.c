/* somvr_session_game.c - SOMVR_MARK (bug marks) and SOMVR_QUIT (the VR menu's Quit game), the game side; included by
 * somvr.c after the practice area's file (somvr_session.h, docs/re-xr-pipeline.md 28). Game thread (in_tick) unless
 * said otherwise. The marks and the quit have no hook of their own: the input tick, the trigger words and an exported
 * object. SOMVR_SKIP_SPLASH=0|1 (default 0): one call-site patch (SS_SPLASH, below): no boot movies.
 *
 *   SOMVR_MARK=0|1     (default 0) the bug marks: the chord B + Y (held back from the game for 0.12 s each so the pair
 *                      can be told apart; somvr_chord), the VR menu's Report button, the trigger word `mark [NOTE]`
 *   SOMVR_MARKS=DIR    where the marks go (default <SOM_BOOTCHECK_RUN>/marks, else <the folder of SOMVR_LOG>/marks)
 *   SOMVR_MARK_VOICE_S the voice memo's length the harness records (default 9; only shown in the messages here)
 *   SOMVR_QUIT=0|1     (default 0) the VR menu's Quit game (and the word `quitgame`): <SOM_BOOTCHECK_RUN>/quit.request,
 *                      boot_check.sh stops the game cleanly; without SOM_BOOTCHECK_RUN (no harness) it stays off
 *   the UI state       `SESSION ui ifs=.. menus=.. paused=.. video=.. ctx=..` whenever one of them changes (with
 *                      SOMVR_INPUT=1; at most 400 lines): what boot_check's `waitlog` waits for in a start sequence
 *
 * Logs: `SESSION config ...`, `SESSION ui ...`, `MARK #N (why) -> DIR ...`, `MARK #N: the eyes ...`, `MARK chord ...`,
 * `QUIT ...`, `SESSION END ...`. */

__attribute__((visibility("default"), used)) somvr_session_shared g_ssn __asm__(SOMVR_SESSION_SYMBOL);

static struct {
	int mark, quit;                    /* the switches as they run */
	char dir[SOMVR_SS_DIR_MAX];        /* the marks' folder */
	char quitf[SOMVR_SS_DIR_MAX];      /* the harness's quit request file */
	float voice_s;
	somvr_chord ch;
	somvr_chord_cfg cc;
	somvr_input_state fst;             /* the snapshot as the controls see it (B and Y through the chord) */
	uint32_t report_seen, quit_seen, wmark_seen, wquit_seen;
	uint32_t n;                        /* marks made */
	double last_t;
	uint32_t want_dump;                /* the last mark's dump_req (0 = none asked) */
	int buzz2;                         /* the second buzz is due: when the eyes are written, at the latest at buzz2_at */
	double buzz2_at, buzz1_t;
	int quit_sent;
	uint32_t chord_lines, ui_lines;
	int ui_have, ui_ifs, ui_menus, ui_paused, ui_video, ui_ctx;
	double fr_t0, fps_t0;
	uint64_t fr_n0, fr_g0;
	float fps, gfps;                   /* the layer's stereo frames and the game's frames per second (the last 2 s) */
	uint64_t n_short, n_long, n_ignored, n_quit;
} g_ss;
static _Atomic uint32_t g_ss_wmark, g_ss_wquit; /* the trigger words: the writer thread -> the next input tick */
static char g_ss_wnote[96];                      /* the word's note, written before g_ss_wmark moves (release) */

/* SOMVR_SKIP_SPLASH=0|1 (default 0; build 6, the user: "make it skip all the launch movies? I just had to sit there
 * waiting 30 seconds for all the company logos"): the five boot movies (Feral, WB, the middleware and legal logos, the
 * title; ~31 s in logs/boot-check-20261023-160221) are never queued. The menu state machine's init FUN_00a1f7d0 builds
 * the SplashScreen queue (Client.SplashMovies' SplashMovieSequence, SplashMoviesBundle) only while the byte 0x46b2061
 * is 0 (the cmp at 0xa1fab0: the game's own dead "no splash" switch; only _INIT_277 writes it, to 0, after our
 * constructor). SS_SPLASH = the init's one caller's call (0xa21107 in FUN_00a210f0) through a stub page to
 * somvr_w_splash, which sets the byte for that call, then puts it back. Not a movie cut short (the SplashScreen's
 * stop event pops the queue unchecked): no movie starts. docs/re-launcher.md 8. Fail soft: a mismatch = the movies. */
/* SESSION_SITES_BEGIN (check_sites.py counts this block: the patched SS_SPLASH K_CALL, the read-only SS_SPLASH_FLAG) */
static const struct site_spec g_ssp_sites_game[] = {
SITE("SS_SPLASH", K_CALL, 0xa21107, 0xa21101, "48 89 ee 48 89 df e8 c4 e6 ff ff", 0xa1f7d0, "41 57 66 0f ef c0 31 c9 48 8d 87 28 2e 00 00"),
SITE("SS_SPLASH_FLAG", K_ENTRY, 0xa1fab0, 0xa1fab0, "80 3d aa 25 c9 03 00 48 c7 43 20 38 60 6b 04 48", 0xa1fab0, "80 3d aa 25 c9 03 00 48 c7 43 20 38 60 6b 04"),
};
/* SESSION_SITES_END */
#define SS_SPLASH_BYTE 0x46b2061u /* the cmp's operand: 0xa1fab7 + 0x3c925aa */
typedef void (*ss_splash_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static struct {
	uintptr_t orig;
	int installed;
	uint32_t calls;
	int was;
} g_ssp;

/* the menu state machine's init (game thread, once at the start): the no-splash byte set for this call only. All six
 * integer argument registers passed on (the call passes two; its prologue clears xmm0, reads none) */
__attribute__((used)) void somvr_w_splash(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f)
{
	volatile uint8_t *flag = (volatile uint8_t *)(uintptr_t)SS_SPLASH_BYTE;
	int was = *flag;
	*flag = 1;
	((ss_splash_fn)g_ssp.orig)(a, b, c, d, e, f);
	*flag = (uint8_t)was;
	g_ssp.was = was;
	if (g_ssp.calls++ < 4)
		logf_("SESSION splash: the menu's init (call %u): no boot movies queued (the no-splash byte was %d, 1 for the "
		      "call, %d again)", g_ssp.calls, was, was);
}

static void ss_splash_start(void)
{
	static const char *const onoff_w[] = { "0", "1" };
	if (!env_word("SOMVR_SKIP_SPLASH", onoff_w, 2, 0))
		return;
	const struct site_spec *s = &g_ssp_sites_game[0];
	char why[512] = "", err[200] = "", nb[64];
	int ok = check_site(&g_ssp_sites_game[1], why, sizeof why) == 0;
	if (!ok)
		logf_("SESSION splash site %s MISMATCH %#lx: %s", g_ssp_sites_game[1].name,
		      (unsigned long)g_ssp_sites_game[1].addr, why);
	ok = ok && s->kind == K_CALL && check_site(s, why, sizeof why) == 0;
	unsigned char *pg = NULL;
	for (int i = 0; ok && i < 64 + 4 && !pg; i++) { /* a stub page of its own within +-2 GB (as MM_CAMQ) */
		static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
		uintptr_t at = i < 64 ? (uintptr_t)0x3b0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
		void *p = mmap((void *)at, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED)
			continue;
		if (stub_reach((uintptr_t)p, s->addr))
			memset(pg = p, 0xcc, 4096);
		else
			munmap(p, 4096);
	}
	if (ok && !pg)
		ok = 0, snprintf(why, sizeof why, "no stub page within +-2 GB");
	if (ok) {
		static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
		uintptr_t tgt = (uintptr_t)somvr_w_splash;
		memcpy(pg, jmp, 6);
		memcpy(pg + 6, &tgt, 8);
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			ok = 0, snprintf(why, sizeof why, "stub mprotect rx: %s", strerror(errno));
	}
	g_ssp.orig = s->target; /* (before the write: the call can come any time after it) */
	int32_t rel = ok ? (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(s->addr + 5)) : 0;
	if (!ok || write_code(s->addr + 1, &rel, 4, err, sizeof err) != 0) {
		logf_("HOOK %s SKIPPED %#lx: %s%s", s->name, (unsigned long)s->addr, ok ? "write failed: " : "", ok ? err : why);
		logf_("WARN SOMVR_SKIP_SPLASH=1: not hooked: the boot movies play");
		if (pg)
			munmap(pg, 4096);
		return;
	}
	g_ssp.installed = 1;
	hex_of((const unsigned char *)s->addr, 5, nb, sizeof nb);
	logf_("HOOK %s ok call %#lx target=%#lx now=[%s] via=%#lx (the menu's init without the boot movies)", s->name,
	      (unsigned long)s->addr, (unsigned long)s->target, nb, (unsigned long)pg);
	logf_("CONFIG skip_splash=1 (the boot movies are not queued: the no-splash byte %#x for the menu's init)",
	      SS_SPLASH_BYTE);
}

static const char *ss_ctx_name(int c)
{
	return c == SC_CTX_WORLD ? "world" : c == SC_CTX_STRUGGLE ? "struggle" : "menu";
}

/* the folder of a path ("" = none) */
static void ss_dirname(const char *p, char *out, size_t cap)
{
	const char *s = p ? strrchr(p, '/') : NULL;
	if (!s || s == p)
		out[0] = 0;
	else
		snprintf(out, cap, "%.*s", (int)(s - p), p);
}

/* somvr_start, after the VR menu: the switches, the folders, the object (active last) */
static void ss_start(void)
{
	static const char *const onoff_w[] = { "0", "1" };
	int bad = 0;
	ss_splash_start(); /* SOMVR_SKIP_SPLASH: before the game's main (the menu's init comes later) */
	g_ss.mark = env_word("SOMVR_MARK", onoff_w, 2, 0);
	g_ss.quit = env_word("SOMVR_QUIT", onoff_w, 2, 0);
	g_ss.voice_s = env_float("SOMVR_MARK_VOICE_S", 9, 1, 30, &bad);
	somvr_chord_cfg_default(&g_ss.cc);
	const char *run = getenv("SOM_BOOTCHECK_RUN"), *md = getenv("SOMVR_MARKS");
	char d[SOMVR_SS_DIR_MAX] = "";
	if (md && md[0])
		snprintf(d, sizeof d, "%s", md);
	else if (run && run[0])
		snprintf(d, sizeof d, "%s/marks", run);
	else if (g_log_path[0]) {
		char p[SOMVR_SS_DIR_MAX];
		ss_dirname(g_log_path, p, sizeof p);
		if (p[0])
			if (snprintf(d, sizeof d, "%s/marks", p) >= (int)sizeof d)
				d[0] = 0; /* too long: no marks (said below) */
	}
	if (g_ss.mark && !somvr_ss_path_ok(d, sizeof d)) {
		logf_("WARN SESSION SOMVR_MARK=1 without a usable folder (SOMVR_MARKS, SOM_BOOTCHECK_RUN or SOMVR_LOG; '%s'): no marks",
		      d);
		g_ss.mark = 0;
	}
	snprintf(g_ss.dir, sizeof g_ss.dir, "%s", d);
	if (run && run[0])
		snprintf(g_ss.quitf, sizeof g_ss.quitf, "%s/quit.request", run);
	if (g_ss.quit && !somvr_ss_path_ok(g_ss.quitf, sizeof g_ss.quitf)) {
		logf_("WARN SESSION SOMVR_QUIT=1 without a harness (no SOM_BOOTCHECK_RUN): no Quit game (the game's own quit is "
		      "not used)");
		g_ss.quit = 0;
	}
	if (g_ss.mark && !g_inp.on)
		logf_("SESSION SOMVR_MARK=1 without SOMVR_INPUT=1: no chord (the menu's Report and the word `mark` still work)");
	g_ssn.magic = SOMVR_SESSION_MAGIC;
	g_ssn.version = SOMVR_SESSION_VERSION;
	g_ssn.size = (uint32_t)sizeof g_ssn;
	g_ssn.pid = (int32_t)getpid();
	g_ssn.avail = (g_ss.mark ? SOMVR_SSA_MARK : 0u) | (g_ss.quit ? SOMVR_SSA_QUIT : 0u);
	if (g_ss.mark || g_ss.quit)
		atomic_store_explicit(&g_ssn.active, 1, memory_order_release);
	logf_("SESSION config mark=%d (the chord B + Y held %.1f s and let go, the VR menu's Report, the word `mark`; -> %s; "
	      "voice %.0f s by the harness) quit=%d (the VR menu's Quit game, the word `quitgame` -> %s); somvr_session %p "
	      "(%zu bytes, v%u, %s)",
	      g_ss.mark, (double)g_ss.cc.hold_s, g_ss.mark ? g_ss.dir : "-", (double)g_ss.voice_s, g_ss.quit,
	      g_ss.quit ? g_ss.quitf : "-", (void *)&g_ssn, sizeof g_ssn, g_ssn.version,
	      g_ss.mark || g_ss.quit ? "active" : "inactive");
}

/* a short write of a whole file (0 = written) */
static int ss_write_file(const char *path, const char *s, size_t n)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return errno;
	size_t u = 0;
	while (u < n) {
		ssize_t k = write(fd, s + u, n - u);
		if (k <= 0) {
			int e = errno;
			close(fd);
			return e ? e : EIO;
		}
		u += (size_t)k;
	}
	return close(fd) ? errno : 0;
}

extern char **environ;

/* state.txt: what the player saw and did (the game thread's own copies; no game call) */
static size_t ss_state(char *b, size_t cap, uint32_t n, const char *why, const char *note, double t)
{
	size_t u = 0;
#define SS_P(...) (u += (size_t)snprintf(b + (u < cap ? u : cap), u < cap ? cap - u : 0, __VA_ARGS__))
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char ts[64];
	strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
	SS_P("bug mark #%u (%s) %s%s%s\n", n, why, ts, note && note[0] ? " - note: " : "", note ? note : "");
	SS_P("somvr built %s %s, pid %d, t=%.3f\n", __DATE__, __TIME__, (int)getpid(), t);
	SS_P("context %s (ifs=%d menus=%d paused=%d video=%d struggle=%d); stereo %s (layout %s); input %s\n",
	     ss_ctx_name(g_inp.ctx), g_inp.ifs, g_inp.menus, g_inp.paused, g_inp.video, g_inp.struggle,
	     atomic_load(&g_trig) ? "on" : "off", g_layout_names[g_layout], g_inp.live ? "live" : "not live");
	SS_P("frame rate: %.1f stereo frames/s (the layer's submits), %.1f game frames/s (the last 2 s)\n", (double)g_ss.fps,
	     (double)g_ss.gfps);
	uintptr_t pm = 0, obj = 0;
	float op[3] = { 0, 0, 0 }, oq[4] = { 0, 0, 0, 1 };
	if (g_fr.pl && player_objs(g_fr.pl, &pm, &obj) && peek(obj + OBJ_POS, op, sizeof op) && vec_ok(op, 1e7f)) {
		peek(obj + OBJ_ROT, oq, sizeof oq);
		SS_P("talion root %.1f %.1f %.1f cm (the world), yaw %.1f deg\n", (double)op[0], (double)op[1], (double)op[2],
		     (double)(sv_heading((sv_quat){ oq[0], oq[1], oq[2], oq[3] }) * 57.29578f));
	} else {
		SS_P("talion: no player object now\n");
	}
	/* the head and the hands at the last eye frame: the rig's world points (as the HAND lines), and the room (XR) */
	sv_vec3 hp, hw;
	sv_quat hq;
	somvr_head_game(g_fr.views.pose, &hp, &hq);
	hw = somvr_rig_point(&g_rig, g_fr.base, hp);
	SS_P("head %.1f %.1f %.1f cm (the world, eye frame %llu), yaw %.1f deg (the room: %.1f %.1f %.1f cm); "
	     "%.1f cm above Talion's root\n",
	     (double)hw.x, (double)hw.y, (double)hw.z, (unsigned long long)g_fr.frame,
	     (double)(sv_heading(somvr_rig_quat(&g_rig, g_fr.base, hq)) * 57.29578f), (double)hp.x, (double)hp.y, (double)hp.z,
	     (double)(hw.y - op[1]));
	SS_P("player size %.0f %% (SOMVR_PLAYER_SIZE / the menu's row: the eyes %+.1f cm over Talion's, the tracked moves x %.2f)\n",
	     (double)(sz_scale() * 100), (double)sz_lift(), (double)sz_scale());
	for (int h = 0; h < 2; h++) {
		const somvr_hand *hd = &g_inp.hands[h];
		const somvr_hand_in *hi = &g_inp.st.hand[h];
		char wp[96] = "not located at the last eye frame";
		if (hd->valid) {
			sv_vec3 p, v, w, pw;
			sv_quat q;
			somvr_hand_game(hd, &p, &q, &v, &w);
			pw = somvr_rig_point(&g_rig, g_fr.base, p);
			snprintf(wp, sizeof wp, "%.1f %.1f %.1f cm (the world)%s", (double)pw.x, (double)pw.y, (double)pw.z,
				 hd->tracked ? "" : ", inferred");
		}
		SS_P("%s hand %s; the controller (xr, m) %.3f %.3f %.3f (pose flags %u); trigger %.2f grip %.2f buttons 0x%x "
		     "stick %.2f %.2f\n",
		     h ? "right" : "left", wp, (double)hi->grip.p[0], (double)hi->grip.p[1], (double)hi->grip.p[2],
		     hi->grip_flags, (double)hi->trigger, (double)hi->squeeze, hi->buttons, (double)hi->stick[0],
		     (double)hi->stick[1]);
	}
	uint32_t ps = atomic_load_explicit(&g_vmo.pstate, memory_order_relaxed);
	SS_P("practice area: %s%s%s\n", ps & SOMVR_VMA_PR_HERE ? "here" : "not here", ps & SOMVR_VMA_PR_ORC ? ", our orc out" : "",
	     ps & SOMVR_VMA_PR_BUSY ? ", a blink on" : "");
	if (g_vm.on) {
		char sb[1024];
		somvr_settings s;
		if (somvr_vrmenu_read(&g_vmo, &s)) {
			somvr_set_format(&s, SOMVR_SK_ALL, sb, sizeof sb);
			SS_P("\n---- the VR menu's settings now\n%s", sb);
		}
	}
	SS_P("\n---- the SOMVR_* switches\n");
	for (char **e = environ; e && *e; e++)
		if (!strncmp(*e, "SOMVR_", 6))
			SS_P("%s\n", *e);
#undef SS_P
	return u < cap ? u : cap - 1;
}

static void ss_buzz(float amp, float dur)
{
	for (int h = 0; h < 2; h++)
		in_haptic_event(h, amp, dur, SOMVR_HAP_UI);
}

/* one mark: the folder, the eyes asked from the layer, state.txt, `want` for the harness, the first buzz */
static void ss_mark(const char *why, const char *note, double t)
{
	if (!g_ss.mark)
		return;
	if (g_ss.n && t - g_ss.last_t < 2.0) {
		g_ss.n_ignored++;
		logf_("MARK ignored (%s): the last one was %.1f s ago (one every 2 s)", why, t - g_ss.last_t);
		return;
	}
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char name[32], d[SOMVR_SS_DIR_MAX + 40], f[SOMVR_SS_DIR_MAX + 64];
	somvr_mark_name(tm.tm_hour, tm.tm_min, tm.tm_sec, g_ss.n + 1, name, sizeof name);
	snprintf(d, sizeof d, "%s/%s", g_ss.dir, name);
	if ((mkdir(g_ss.dir, 0755) && errno != EEXIST) || mkdir(d, 0755)) {
		logf_("WARN MARK (%s): mkdir %s: %s - no mark", why, d, strerror(errno));
		return;
	}
	double prev_t = g_ss.last_t;
	g_ss.n++, g_ss.last_t = t;
	const char *eyes = "not asked (no layer frames: no XR session)";
	uint32_t done = atomic_load_explicit(&g_ssn.dump_done, memory_order_acquire),
		 req = atomic_load_explicit(&g_ssn.dump_req, memory_order_relaxed);
	g_ss.want_dump = 0;
	/* the layer is free, or the last request is 10 s old (a layer that does not serve them: asked again anyway) */
	if (atomic_load(&g_sh.xr.state) == SOMVR_XRS_RUNNING && (done == req || t - prev_t > 10)) {
		if (snprintf(g_ssn.dump_dir, sizeof g_ssn.dump_dir, "%s", d) >= (int)sizeof g_ssn.dump_dir)
			g_ssn.dump_dir[0] = 0; /* (cannot happen: the folder is checked at the start) */
		g_ss.want_dump = req + 1 ? req + 1 : 1;
		atomic_store_explicit(&g_ssn.dump_req, g_ss.want_dump, memory_order_release);
		eyes = "asked from the layer (eyes.ppm, the next frame)";
	} else if (done != req) {
		eyes = "not asked (the layer is still writing the last mark's)";
	}
	static char sb[16384];
	size_t n = ss_state(sb, sizeof sb, g_ss.n, why, note, t);
	snprintf(f, sizeof f, "%s/state.txt", d);
	int e1 = ss_write_file(f, sb, n);
	char w[160];
	int wn = snprintf(w, sizeof w, "%s%s%s\n", why, note && note[0] ? ": " : "", note ? note : "");
	snprintf(f, sizeof f, "%s/want", d); /* last: the harness serves the folder from here on */
	int e2 = ss_write_file(f, w, (size_t)(wn > 0 && (size_t)wn < sizeof w ? wn : 0));
	atomic_store_explicit(&g_ssn.marks, g_ss.n, memory_order_release);
	ss_buzz(0.6f, 0.06f);
	g_ss.buzz2 = 1, g_ss.buzz1_t = t, g_ss.buzz2_at = t + 2.0;
	logf_("MARK #%u (%s%s%s) -> %s: state.txt %zu bytes%s%s; the eyes %s; the harness adds the flat picture, the log "
	      "tails and %.0f s of the headset's microphone",
	      g_ss.n, why, note && note[0] ? ": " : "", note ? note : "", d, n, e1 ? " WRITE FAILED: " : "",
	      e1 ? strerror(e1) : "", eyes, (double)g_ss.voice_s);
	if (e2)
		logf_("WARN MARK #%u: %s/want: %s (the harness will not see this mark)", g_ss.n, d, strerror(e2));
}

/* the VR menu's Quit game (confirmed), or the word: the harness's request file */
static void ss_quit(const char *why, double t)
{
	g_ss.n_quit++;
	if (!g_ss.quit) {
		logf_("QUIT asked (%s) but SOMVR_QUIT is off: nothing", why);
		return;
	}
	if (g_ss.quit_sent)
		return;
	char s[160];
	time_t now = time(NULL);
	int n = snprintf(s, sizeof s, "%s at %lld (t=%.3f)\n", why, (long long)now, t);
	int e = ss_write_file(g_ss.quitf, s, (size_t)(n > 0 && (size_t)n < sizeof s ? n : 0));
	if (e) {
		logf_("WARN QUIT (%s): %s: %s - the game goes on", why, g_ss.quitf, strerror(e));
		return;
	}
	g_ss.quit_sent = 1;
	atomic_store_explicit(&g_ssn.quitting, 1, memory_order_release);
	ss_buzz(0.5f, 0.15f);
	logf_("QUIT the player asked (%s): %s written; the harness stops the game (its cleanup checks the save)", why,
	      g_ss.quitf);
	somvr_end_logs("the player's Quit game"); /* the harness's SIGTERM skips the destructor */
}

/* the trigger words (the writer thread): "mark [NOTE]", "quitgame"; NULL = not one of them */
static const char *ss_word(const char *w)
{
	char note[sizeof g_ss_wnote];
	if (somvr_mark_word(w, note, sizeof note)) {
		memcpy(g_ss_wnote, note, sizeof note);
		atomic_fetch_add_explicit(&g_ss_wmark, 1, memory_order_release);
		return g_ss.mark ? "mark" : "mark (SOMVR_MARK is off: nothing)";
	}
	if (!strncmp(w, "quitgame", 8) && (!w[8] || w[8] == '\n' || w[8] == ' ')) {
		atomic_fetch_add_explicit(&g_ss_wquit, 1, memory_order_release);
		return g_ss.quit ? "quitgame" : "quitgame (SOMVR_QUIT is off: nothing)";
	}
	return NULL;
}

/* every input tick: the UI state lines, the frame rate, the requests (the menu's Report and Quit, the words), the second
 * buzz */
static void ss_tick(double t)
{
	/* never a void: what flat frames show now (the layer's theatre, SOMVR_XR_THEATRE) */
	/* (SOMVR_INSPECT: a paused menu's own view, e.g. an artifact's, is a menu frame - the board - not a load: the pause
	 * menu's states are a load's, ifs=3 menus=0 paused=1, run 20261026-160543's 2.4 m load panel) */
	/* (build 22, SOMVR_BM_DEATH=first: the Nemesis screens' first flat frames, before the window's debounce, are the
	 * window's - the theatre's screen at once, not the load panel for 0.3 s) */
	atomic_store_explicit(&g_ssn.mono, g_inp.on && g_inp.video == 1 ? SOMVR_SSM_VIDEO
					   : g_bm.last_win || scr_mono_window() ? SOMVR_SSM_WINDOW
					   : co_flat_now() ? SOMVR_SSM_MENU
					   : g_inp.on && g_inp.ifs == 3 && g_inp.menus == 0 && g_inp.paused == 1 ? SOMVR_SSM_LOAD
					   : SOMVR_SSM_MENU, memory_order_relaxed);
	if (g_inp.on && (!g_ss.ui_have || g_inp.ifs != g_ss.ui_ifs || g_inp.menus != g_ss.ui_menus ||
			 g_inp.paused != g_ss.ui_paused || g_inp.video != g_ss.ui_video || g_inp.ctx != g_ss.ui_ctx)) {
		g_ss.ui_have = 1, g_ss.ui_ifs = g_inp.ifs, g_ss.ui_menus = g_inp.menus, g_ss.ui_paused = g_inp.paused;
		g_ss.ui_video = g_inp.video, g_ss.ui_ctx = g_inp.ctx;
		if (g_ss.ui_lines++ < 400)
			logf_("SESSION ui ifs=%d menus=%d paused=%d video=%d ctx=%s t=%.3f", g_inp.ifs, g_inp.menus, g_inp.paused,
			      g_inp.video, ss_ctx_name(g_inp.ctx), t);
	}
	if (t - g_ss.fr_t0 >= 2.0) {
		uint64_t ns = atomic_load_explicit(&g_sh.n_stereo, memory_order_relaxed), ng = g_fr.frame;
		if (g_ss.fr_t0 > 0) {
			g_ss.fps = (float)((double)(ns - g_ss.fr_n0) / (t - g_ss.fr_t0));
			g_ss.gfps = (float)((double)(ng - g_ss.fr_g0) / (t - g_ss.fr_t0));
		}
		g_ss.fr_t0 = t, g_ss.fr_n0 = ns, g_ss.fr_g0 = ng;
	}
	if (!atomic_load_explicit(&g_ssn.active, memory_order_relaxed))
		return;
	uint32_t r = atomic_load_explicit(&g_ssn.report_req, memory_order_acquire);
	if (r != g_ss.report_seen) {
		g_ss.report_seen = r;
		ss_mark("the VR menu's Report", "", t);
	}
	uint32_t wm = atomic_load_explicit(&g_ss_wmark, memory_order_acquire);
	if (wm != g_ss.wmark_seen) {
		g_ss.wmark_seen = wm;
		char note[sizeof g_ss_wnote];
		memcpy(note, g_ss_wnote, sizeof note);
		note[sizeof note - 1] = 0;
		ss_mark("the trigger word", note, t);
	}
	uint32_t q = atomic_load_explicit(&g_ssn.quit_req, memory_order_acquire);
	if (q != g_ss.quit_seen) {
		g_ss.quit_seen = q;
		ss_quit("the VR menu's Quit game", t);
	}
	uint32_t wq = atomic_load_explicit(&g_ss_wquit, memory_order_acquire);
	if (wq != g_ss.wquit_seen) {
		g_ss.wquit_seen = wq;
		ss_quit("the trigger word quitgame", t);
	}
	if (g_ss.buzz2 && t - g_ss.buzz1_t >= 0.35) {
		int eyes = g_ss.want_dump && atomic_load_explicit(&g_ssn.dump_done, memory_order_acquire) == g_ss.want_dump;
		if (eyes || !g_ss.want_dump || t >= g_ss.buzz2_at) {
			g_ss.buzz2 = 0;
			ss_buzz(0.6f, 0.06f);
			logf_("MARK #%u: the second buzz %.2f s after the first; the eyes %s", g_ss.n, t - g_ss.buzz1_t,
			      !g_ss.want_dump ? "were not asked"
			      : eyes	       ? (atomic_load(&g_ssn.dump_fail) ? "given up by the layer (see xr-layer.log)" : "written")
					       : "not written within 2 s (the layer still at it, or no frame)");
		}
	}
}

/* in_tick, right after the snapshot's read: ss_tick, then the snapshot the controls get: B and Y through the chord
 * (SOMVR_MARK=1; else the snapshot itself) */
static const somvr_input_state *ss_input(const somvr_input_state *raw, int live, double t)
{
	ss_tick(t);
	sy_tick(t); /* build 22, SOMVR_STORY_LOG: the subtitle lines */
	if (!g_ss.mark)
		return raw;
	somvr_chord_in ci;
	for (int b = 0; b < 2; b++) {
		const somvr_hand_in *hi = &raw->hand[b == SOMVR_CHB_Y ? SOMVR_HAND_L : SOMVR_HAND_R];
		ci.held[b] = (hi->buttons & SOMVR_BTN_UPPER) != 0;
		ci.presses[b] = hi->presses[1]; /* SOMVR_BTN_UPPER = bit 1 */
		ci.act[b] = live && (hi->active & SOMVR_HACT_INPUT);
	}
	somvr_chord_out co;
	somvr_chord_tick(&g_ss.ch, &g_ss.cc, &ci, t, &co);
	g_ss.fst = *raw;
	for (int b = 0; b < 2; b++) {
		somvr_hand_in *ho = &g_ss.fst.hand[b == SOMVR_CHB_Y ? SOMVR_HAND_L : SOMVR_HAND_R];
		ho->buttons = (ho->buttons & ~(uint32_t)SOMVR_BTN_UPPER) | (co.held[b] ? (uint32_t)SOMVR_BTN_UPPER : 0u);
		ho->presses[1] = co.presses[b];
	}
	if (co.ev == SOMVR_CHE_ARMED) {
		ss_buzz(0.35f, 0.025f);
	} else if (co.ev == SOMVR_CHE_MARK) {
		ss_mark("the chord B + Y", "", t);
	} else if (co.ev == SOMVR_CHE_SHORT || co.ev == SOMVR_CHE_LONG) {
		if (co.ev == SOMVR_CHE_SHORT)
			g_ss.n_short++;
		else
			g_ss.n_long++;
		if (g_ss.chord_lines++ < 40)
			logf_("MARK chord B + Y held %.2f s: %s (no mark; hold them together %.1f s .. %.0f s and let go)",
			      (double)co.held_s, co.ev == SOMVR_CHE_SHORT ? "too short" : "too long", (double)g_ss.cc.hold_s,
			      (double)g_ss.cc.max_s);
	}
	return &g_ss.fst;
}

static void ss_end_log(void)
{
	if (g_ssp.installed)
		logf_("SESSION splash END the menu's init %u time(s) without the boot movies (the byte was %d)", g_ssp.calls,
		      g_ssp.was);
	if (!g_ss.mark && !g_ss.quit)
		return;
	logf_("SESSION END marks %u (chord %u, too short %llu, too long %llu, ignored %llu; presses held back %u, passed %u), "
	      "eyes asked %u done %u failed %u, quit asks %llu (sent %d)",
	      g_ss.n, g_ss.ch.n_marks, (unsigned long long)g_ss.n_short, (unsigned long long)g_ss.n_long,
	      (unsigned long long)g_ss.n_ignored, g_ss.ch.n_held, g_ss.ch.n_passed, atomic_load(&g_ssn.dump_req),
	      atomic_load(&g_ssn.dump_done), atomic_load(&g_ssn.dump_fail), (unsigned long long)g_ss.n_quit, g_ss.quit_sent);
}
