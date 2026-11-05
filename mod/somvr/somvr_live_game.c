/* somvr_live_game.c - SOMVR_LIVE=on, the game side (docs/re-xr-pipeline.md 162; somvr_live.h): included by somvr.c.
 *
 * The player's Apply patch / Undo patch (the VR menu's Test page; the layer writes SOMVR_LIVE_DIR/patches/request.txt)
 * reaches the host's hot reload (win/somvr_win_host.h, docs 156): `reload SOMVR_DEV_RELOAD_DLL=<the patch's DLL>` -
 * the host copies it, waits for this module's quiet moment (its idle check, at most SOMVR_DEV_RELOAD_WAIT_S), swaps
 * every hook to the new module; Undo swaps back to the module before it (the patch applied before, or the session's
 * own somvr_logic.dll beside the host). Nothing swaps without that press: this file never reloads on its own.
 *   - the writer thread, every 0.5 s (lv_game_poll): a new request (its seq above the answered one) -> checked
 *     (the patch staged, kind live, its DLL there) -> patches/applying.txt, then the host's reload; no host (the
 *     one-DLL build, Linux): answered "stub" (the patch waits for a restart). The module the host swapped in finds
 *     applying.txt with the host's generation moved by one: it answers "applied" / "undone" (state.txt, stack.txt,
 *     log.txt). Still no swap 20 s after the request (the host found the game busy, or refused the DLL): "refused".
 *   - vm_gate's SOMVR_VMG_FIGHT: an attack at Talion now or in the last 6 s (th_attacker) - the card waits for a calm
 *     moment by it (only with SOMVR_LIVE on: the gate's word stays build 34's otherwise).
 * Every request, answer and swap is a LIVE line in somvr.log and a line in patches/log.txt with the patch's id.
 * b37 (162.8): the host's own call for the player's press when it has one - somvr_host_player_swap(dll, why)
 * (som-b37-hotreload2: allowed with SOMVR_LIVE=on, no SOMVR_DEV_RELOAD; the only swap in a session the launcher started
 * for the user) - else build 35's `reload` word. The host is always handed a Linux path (its copy step reads only
 * those): before the first Apply this module keeps a copy of the session's own module (patches/own_somvr_logic.dll),
 * and Undo back to the session's start swaps that copy in. */

static struct {
	int on;
	char dir[512];
	uint32_t seq_done;   /* the last request answered (state.txt's seq; a swapped-in module reads it at its start) */
	int current;         /* the patch live now (0 = the session's own module) */
	int busy, busy_op, busy_id, busy_gen0;
	uint32_t busy_seq;
	double busy_t;
	double next_t;
	uint64_t n_req, n_ok, n_no;
} g_lvg;

#ifdef _WIN32
__declspec(dllimport) void *GetModuleHandleA(const char *name); /* kernel32 (somvr.c has no windows.h) */
__declspec(dllimport) void *GetProcAddress(void *mod, const char *name);
__declspec(dllimport) unsigned long GetModuleFileNameA(void *mod, char *out, unsigned long n);
typedef void (*lvg_reload_fn)(const char *word);
typedef int (*lvg_gen_fn)(void);
typedef int (*lvg_swap_fn)(const char *dll, const char *why);
static lvg_reload_fn lvg_reload;
static lvg_gen_fn lvg_gen;
static lvg_swap_fn lvg_swap; /* b37: somvr_host_player_swap (0 = queued, -1 = refused at once: the host's line says why) */
/* the host's two entry points (docs 156's split build: xinput9_1_0.dll exports them); the one-DLL build: none */
static int lvg_host(void)
{
	static int done;
	if (!done) {
		done = 1;
		void *m = GetModuleHandleA("xinput9_1_0.dll");
		lvg_reload = m ? (lvg_reload_fn)GetProcAddress(m, "somvr_host_reload_request") : NULL;
		lvg_gen = m ? (lvg_gen_fn)GetProcAddress(m, "somvr_host_gen") : NULL;
		lvg_swap = m ? (lvg_swap_fn)GetProcAddress(m, "somvr_host_player_swap") : NULL;
	}
	return (lvg_reload || lvg_swap) && lvg_gen;
}
/* the session's own logic module: somvr_logic.dll beside the host (the host's watched_path default) */
static int lvg_boot_dll(char *out, size_t cap)
{
	char me[600] = "";
	void *m = GetModuleHandleA("xinput9_1_0.dll");
	unsigned long n = m ? GetModuleFileNameA(m, me, sizeof me) : 0;
	char *bs = n ? strrchr(me, '\\') : NULL;
	if (!bs)
		return 0;
	*bs = 0;
	return snprintf(out, cap, "%s\\somvr_logic.dll", me) < (int)cap;
}
static int lvg_has_swap(void) { return lvg_host() && lvg_swap; }
#else
static int lvg_has_swap(void) { return 0; }
static int lvg_host(void) { return 0; } /* Linux: one library for the run */
static int lvg_gen_now(void) { return 0; }
static int lvg_boot_dll(char *out, size_t cap) { return (void)out, (void)cap, 0; }
#endif
#ifdef _WIN32
static int lvg_gen_now(void) { return lvg_host() ? lvg_gen() : 0; }
#endif

static int lvg_read(const char *rel, char *buf, size_t cap)
{
	char p[700];
	snprintf(p, sizeof p, "%s/%s", g_lvg.dir, rel);
	FILE *f = fopen(p, "rb");
	if (!f)
		return -1;
	size_t n = fread(buf, 1, cap - 1, f);
	fclose(f);
	buf[n] = 0;
	return (int)n;
}
static int lvg_write(const char *rel, const char *s, int append)
{
	char p[700], tmp[720];
	snprintf(p, sizeof p, "%s/%s", g_lvg.dir, rel);
	snprintf(tmp, sizeof tmp, "%s.tmp", p);
	FILE *f = fopen(append ? p : tmp, append ? "ab" : "wb");
	if (!f)
		return -1;
	int ok = fputs(s, f) >= 0;
	ok &= fclose(f) == 0;
	return ok && (append || rename(tmp, p) == 0) ? 0 : -1;
}
/* the answer: state.txt (the layer shows it), log.txt (the record) */
static void lvg_answer(uint32_t seq, const char *last, int id, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void lvg_answer(uint32_t seq, const char *last, int id, const char *fmt, ...)
{
	char msg[200], st[400], ln[400];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	g_lvg.seq_done = seq;
	snprintf(st, sizeof st, "seq=%u\ncurrent=%d\nlast=%s\nid=%d\nmsg=%s\n", seq, g_lvg.current, last, id, msg);
	int ok = lvg_write("patches/state.txt", st, 0) == 0;
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char ts[32];
	strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
	snprintf(ln, sizeof ln, "%s request %u: %s patch %d (now %d live, module gen %d): %s\n", ts, seq, last, id,
		 g_lvg.current, lvg_gen_now(), msg);
	lvg_write("patches/log.txt", ln, 1);
	logf_("LIVE request %u: %s patch %d (now %d live, module gen %d): %s%s", seq, last, id, g_lvg.current, lvg_gen_now(),
	      msg, ok ? "" : " (state.txt NOT written)");
}
/* the module a patch id stands for: 0 = the session's own, else patches/<id>/somvr_logic.dll */
static int lvg_dll_of(int id, char *out, size_t cap)
{
	if (id == 0) /* b37: the session's own module, as kept by lvg_keep_own (a Linux path, as every patch's) */
		return snprintf(out, cap, "%s/patches/own_somvr_logic.dll", g_lvg.dir) < (int)cap;
	return snprintf(out, cap, "%s/patches/%d/somvr_logic.dll", g_lvg.dir, id) < (int)cap;
}
/* b37: before the first swap away from it, a copy of the session's own module into the live folder (Undo back to the
 * start swaps it in); 1 = there */
static int lvg_keep_own(void)
{
	char own[700], boot[600];
	lvg_dll_of(0, own, sizeof own);
	FILE *o = fopen(own, "rb");
	if (o) {
		fclose(o);
		return 1;
	}
	if (!lvg_boot_dll(boot, sizeof boot))
		return 0;
	FILE *in = fopen(boot, "rb");
	if (!in)
		return 0;
	char tmp[720];
	snprintf(tmp, sizeof tmp, "%s.tmp", own);
	FILE *out = fopen(tmp, "wb");
	size_t n, all = 0;
	char buf[65536];
	int ok = out != NULL;
	while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0)
		ok = fwrite(buf, 1, n, out) == n, all += n;
	fclose(in);
	if (out && fclose(out) != 0)
		ok = 0;
	if (ok)
		ok = rename(tmp, own) == 0;
	if (!ok)
		remove(tmp);
	logf_("LIVE the session's own module %s %s %s (%zu bytes) for Undo back to the start", boot, ok ? "kept as" : "NOT kept as",
	      own, all);
	return ok;
}
/* the patch applied before the current one (stack.txt: one id a line, the order applied; undone ones removed) */
static int lvg_stack(int *ids, int cap)
{
	char b[1024];
	int n = 0;
	if (lvg_read("patches/stack.txt", b, sizeof b) <= 0)
		return 0;
	for (char *s = b; *s && n < cap;) {
		int v = atoi(s);
		if (v > 0)
			ids[n++] = v;
		while (*s && *s != '\n')
			s++;
		if (*s)
			s++;
	}
	return n;
}
static void lvg_stack_write(const int *ids, int n)
{
	char b[1024] = "";
	size_t u = 0;
	for (int i = 0; i < n && u < sizeof b - 16; i++)
		u += (size_t)snprintf(b + u, sizeof b - u, "%d\n", ids[i]);
	lvg_write("patches/stack.txt", b, 0);
}

/* somvr_start (each module's: the boot's, and a swapped-in one's) */
static void lv_game_start(void)
{
	memset(&g_lvg, 0, sizeof g_lvg);
	const char *e = getenv("SOMVR_LIVE"), *d = getenv("SOMVR_LIVE_DIR");
	if (!e || (strcmp(e, "on") && strcmp(e, "1")))
		return;
	if (!d || !*d || strlen(d) >= sizeof g_lvg.dir - 64) {
		logf_("LIVE SOMVR_LIVE=on without a usable SOMVR_LIVE_DIR ('%s'): off", d ? d : "");
		return;
	}
	snprintf(g_lvg.dir, sizeof g_lvg.dir, "%s", d);
	g_lvg.on = 1;
	char b[512];
	somvr_lv_state s;
	int n = lvg_read("patches/state.txt", b, sizeof b);
	if (n > 0 && somvr_lv_state_parse(b, (size_t)n, &s)) {
		g_lvg.seq_done = s.seq, g_lvg.current = s.current;
		if (!strcmp(s.last, "start") && lvg_gen_now() == 0) /* b40 live4: live_session.sh preapply */
			logf_("LIVE patch %d applied at the session's start: this module is that patch (staged for this build "
			      "before the launch); Undo returns to the build's own (patches/own_somvr_logic.dll)", s.id);
	}
	char boot[600] = "";
	int host = lvg_host(), have_boot = lvg_boot_dll(boot, sizeof boot);
	logf_("LIVE config on: the folder %s; the patches by the host's hot reload: %s (module gen %d; the session's own "
	      "module %s); answered so far: request %u, patch %d live; the card's fight bit: an attack at Talion in the "
	      "last 6 s", g_lvg.dir, host ? (lvg_has_swap() ? "yes (somvr_host_player_swap)" : "yes (somvr_host_reload_request)") : "NO - this build has no host/logic split "
	      "(the stub: a patch waits for a restart)", lvg_gen_now(), have_boot ? boot : "-", g_lvg.seq_done, g_lvg.current);
}

/* vm_gate (the game thread): SOMVR_VMG_FIGHT */
static int lvg_fight(double t)
{
	float p[3];
	return g_lvg.on && th_attacker(t, 6.0, p) != 0;
}

/* the writer thread, every 250 ms (each 0.5 s here) */
static void lv_game_poll(double t)
{
	if (!g_lvg.on || t < g_lvg.next_t)
		return;
	g_lvg.next_t = t + 0.5;
	char b[512];
	/* 1. a swap this module came in by: applying.txt with the host's generation one above the asker's */
	int n = lvg_read("patches/applying.txt", b, sizeof b);
	if (n > 0) {
		char v[32];
		uint32_t seq = somvr_lv_kv(b, (size_t)n, "seq", v, sizeof v) ? (uint32_t)strtoul(v, NULL, 10) : 0;
		int gen0 = somvr_lv_kv(b, (size_t)n, "gen0", v, sizeof v) ? atoi(v) : -1;
		int op = somvr_lv_kv(b, (size_t)n, "op", v, sizeof v) && !strcmp(v, "undo") ? 2 : 1;
		int id = somvr_lv_kv(b, (size_t)n, "id", v, sizeof v) ? atoi(v) : 0;
		int to = somvr_lv_kv(b, (size_t)n, "to", v, sizeof v) ? atoi(v) : 0;
		if (seq > g_lvg.seq_done && !g_lvg.busy && lvg_host() && lvg_gen_now() == gen0 + 1) {
			int st[32], k = lvg_stack(st, 32);
			if (op == 1 && k < 32)
				st[k++] = id;
			else if (op == 2 && k > 0 && st[k - 1] == id)
				k--;
			lvg_stack_write(st, k);
			g_lvg.current = op == 1 ? id : to;
			char p[700];
			snprintf(p, sizeof p, "%s/patches/applying.txt", g_lvg.dir);
			unlink(p);
			g_lvg.n_ok++;
			lvg_answer(seq, op == 1 ? "applied" : "undone", id, "swapped in as module gen %d (%s)", lvg_gen_now(),
				   op == 1 ? "the patch's" : to ? "the patch before it" : "the session's own");
		}
	}
	/* 2. a request this module made: no swap within 20 s = refused (the host's line in this log says why) */
	if (g_lvg.busy) {
		if (t - g_lvg.busy_t < 20.0)
			return;
		g_lvg.busy = 0, g_lvg.n_no++;
		char p[700];
		snprintf(p, sizeof p, "%s/patches/applying.txt", g_lvg.dir);
		unlink(p);
		lvg_answer(g_lvg.busy_seq, "refused", g_lvg.busy_id,
			   "the game was busy (a kill, a grab, a climb?) or the DLL was refused - try again at a calm moment");
		return;
	}
	/* 3. a new request */
	n = lvg_read("patches/request.txt", b, sizeof b);
	int op = 0, id = 0;
	uint32_t seq = 0;
	if (n <= 0 || !somvr_lv_request_parse(b, (size_t)n, &op, &id, &seq) || seq <= g_lvg.seq_done)
		return;
	g_lvg.n_req++;
	logf_("LIVE request %u: %s patch %d (now %d live)", seq, op == 1 ? "apply" : "undo", id, g_lvg.current);
	char rel[64], dll[700];
	somvr_lv_patch p;
	snprintf(rel, sizeof rel, "patches/%d/patch.txt", id);
	n = lvg_read(rel, b, sizeof b);
	if (op == 1 && (n <= 0 || !somvr_lv_patch_parse(b, (size_t)n, &p) || p.id != id)) {
		lvg_answer(seq, "bad", id, "patch %d is not staged here", id);
		return;
	}
	if (op == 1 && !p.live) {
		lvg_answer(seq, "restart", id, "%s", p.why[0] ? p.why : "it changes the layer or the host: it needs a restart");
		return;
	}
	if (op == 2 && id != g_lvg.current) {
		lvg_answer(seq, "bad", id, "patch %d is not the one live (%d is)", id, g_lvg.current);
		return;
	}
	if (!lvg_host()) {
		lvg_answer(seq, "stub", id, "this build has no hot reload (the host/logic split): it waits for a restart");
		return;
	}
	int to = 0;
	if (op == 1) {
		lvg_dll_of(id, dll, sizeof dll);
		if (g_lvg.current == 0 && !lvg_keep_own())
			logf_("LIVE request %u: the session's own module could not be kept: Undo back to the start will fail", seq);
	} else {
		int st[32], k = lvg_stack(st, 32);
		to = k >= 2 && st[k - 1] == id ? st[k - 2] : 0;
		if (!lvg_dll_of(to, dll, sizeof dll)) {
			lvg_answer(seq, "bad", id, "the module before patch %d is not known", id);
			return;
		}
	}
	FILE *f = fopen(dll, "rb");
	if (!f) {
		lvg_answer(seq, "bad", id, "the module %.150s is not there", dll);
		return;
	}
	fclose(f);
	if (strchr(dll, ' ')) {
		lvg_answer(seq, "bad", id, "the module's path has a space (the reload word splits it)");
		return;
	}
	char ap[800], word[256];
	int gen0 = lvg_gen_now();
	snprintf(ap, sizeof ap, "seq=%u\nop=%s\nid=%d\nto=%d\ngen0=%d\ndll=%s\n", seq, op == 1 ? "apply" : "undo", id, to, gen0,
		 dll);
	if (lvg_write("patches/applying.txt", ap, 0) != 0) {
		lvg_answer(seq, "bad", id, "patches/applying.txt not written");
		return;
	}
	if (snprintf(word, sizeof word, "reload SOMVR_DEV_RELOAD_DLL=%s", dll) >= (int)sizeof word) {
		lvg_answer(seq, "bad", id, "the module's path is too long for the reload word");
		return;
	}
	g_lvg.busy = 1, g_lvg.busy_seq = seq, g_lvg.busy_op = op, g_lvg.busy_id = id, g_lvg.busy_gen0 = gen0, g_lvg.busy_t = t;
	logf_("LIVE request %u: the host's %s asked for %s (module gen %d now; the swap at its quiet moment)", seq,
	      lvg_has_swap() ? "player swap" : "reload", dll, gen0);
#ifdef _WIN32
	if (lvg_swap) { /* b37: the player's own swap */
		char why[64];
		snprintf(why, sizeof why, "%s patch %d", op == 1 ? "Apply" : "Undo", id);
		if (lvg_swap(dll, why) != 0) {
			char p2[700];
			snprintf(p2, sizeof p2, "%s/patches/applying.txt", g_lvg.dir);
			unlink(p2);
			g_lvg.busy = 0, g_lvg.n_no++;
			lvg_answer(seq, "refused", id, "the host refused the swap now (its RELOAD line says why) - try again in a moment");
		}
	} else {
		lvg_reload(word);
	}
#endif
}
