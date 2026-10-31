/* somvr_perf_game.c - SOMVR_PERF=on: the CPU time of a stereo frame, the game's and ours (build 27, som-b27-perf,
 * docs/re-xr-pipeline.md 115). Included by somvr.c (mono, logf_). The layer's SOMVR_XR_PERF has the GPU's side.
 *   The game thread, from one H4 (FUN_009ee170's caller: both eye passes) to the next, when that frame was stereo:
 *     frame   its wall time and the thread's CPU time (the update, the AI, the animation, both eye passes' command
 *             building, our hooks on this thread);
 *     ours    our H4 work before eye 0 (the views' locate, the rig, the body, the head parts, the hands) + after eye 1
 *             (input, arms, combat, threat, fight, practice...), wall;
 *     eye 0/1 each pass's wall and CPU time; eye 0's wall holds the vf_24 sync (the wait for the render thread = the
 *             runtime's pace through xrWaitFrame);
 *     busy    = the frame's wall - the sync's wait (eye 0's wall - its CPU): the game thread's own time a frame, what
 *             must stay under 11.1 ms at 90 Hz (a wait on the job workers inside eye 0 counts as the sync's: a little
 *             low then).
 *   The render thread, from one present (H9) to the next: its CPU time (the D3D11 -> Vulkan translation of the game's
 *     commands, the layer's present path; xrWaitFrame blocks without CPU).
 *   The process: all threads' CPU per stereo frame (the job workers too).
 *   Every SOMVR_PERF_S seconds (default 10) one `PERF t=...` line (mean/p50/p95/max ms; frames over 11.1 ms); off = no
 *   clock read at all. */

#define PFG_WIN 2048u
#define PFG_BUDGET_MS 11.111
/* build 27 (som-b27-perf2, re-xr-pipeline.md 119): "over" counts against the runtime's display period (SOMVR_XR_REFRESH
 * 80 = 12.5 ms), 90 Hz's without one */
static double g_pfg_bud = PFG_BUDGET_MS;

/* our H4 work after eye 1, by function (pfg_part(k) after each; the rest = rd_frame + sz_after) */
#define PFG_NPART 12
static const char *const pfg_part_name[PFG_NPART] = { "in", "arm", "combat", "threat", "strike_gaze", "wraith_step", "vault",
						      "objective", "defence", "fight", "practice", "rest" };

enum { PFG_WALL, PFG_CPU, PFG_BUSY, PFG_OURS, PFG_PRE, PFG_POST, PFG_E0W, PFG_E0C, PFG_E1W, PFG_E1C, PFG_PROC, PFG_PEEKS,
       PFG_N };

static struct {
	int on, bad, period_s, period_bad;
	double last_log;
	/* the game thread (H4) */
	int have;              /* a previous entry */
	int stereo;            /* the previous frame was stereo (its eyes timed) */
	double w_in, c_in, p_in; /* the previous entry: wall, thread CPU, process CPU (s) */
	uint64_t pk_in;          /* ... the game thread's peeks (process_vm_readv: ~0.75 us each) */
	double e_w[2][2], e_c[2][2]; /* eye e: [0] start, [1] end */
	double w_done;         /* the previous frame's post end */
	double part_t, part[PFG_NPART], part_sum[PFG_NPART];
	uint64_t part_pk0, part_pk[PFG_NPART], part_pksum[PFG_NPART]; /* their peeks */
	float part_max[PFG_NPART];
	uint32_t part_n;
	float g[PFG_N][PFG_WIN];
	uint32_t ng;
	/* the render thread (H9) */
	pthread_mutex_t mx;
	double r_w, r_c;
	int r_have;
	float rc[PFG_WIN], rw[PFG_WIN], rpc[PFG_WIN], rpw[PFG_WIN]; /* + the Present call's CPU and wall */
	float rpk[PFG_WIN]; /* the render thread's peeks a present */
	uint64_t r_pk;
	double rp_w, rp_c;
	uint32_t nr;
	uint64_t frames, logs;
	/* the four shadow passes of the opaque stage (HEAD_SHADOW_PRE/LIGHTS/CASC/LIGHTS2, the render thread): per eye
	 * [e][k] wall and CPU sums (s), calls; the word `perf skip MASK`: eye 1 skips the passes in MASK (a measurement:
	 * what sharing eye 0's maps would save) and gets eye 0's return value */
	double sh_w[2][4], sh_c[2][4], sh_c0;
	/* the render thread's work on each eye: H3's eye match -> H8's end of the eye's target (the scene, its shadows, its
	 * post); and from eye 0's start to the present (H9): both eyes, the HUD, the composite = the render thread's frame
	 * work, without the waits between frames */
	double re_w0[2], re_c0[2];
	float re_c[2][PFG_WIN], rs_c[PFG_WIN], rs_w[PFG_WIN];
	uint32_t nre[2], nrs;
	int re_have0;
	uint32_t sh_n[2][4];
	uint64_t sh_rax[4], sh_skipped;
	atomic_uint sh_skip;
	char label[40], next_label[40]; /* the word `perf NAME`: the window's name (the trigger thread -> the game thread) */
	atomic_int relabel;
} g_pfg = {.mx = PTHREAD_MUTEX_INITIALIZER};

static double pfg_clock(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void pfg_config(void)
{
	const char *p = getenv("SOMVR_PERF");
	g_pfg.on = p && (!strcmp(p, "on") || !strcmp(p, "1"));
	g_pfg.bad = p && *p && !g_pfg.on && strcmp(p, "off") && strcmp(p, "0");
	p = getenv("SOMVR_PERF_S");
	char *end = NULL;
	long s = p && *p ? strtol(p, &end, 10) : 10;
	g_pfg.period_bad = p && *p && (!end || *end || s < 1 || s > 600);
	g_pfg.period_s = g_pfg.period_bad ? 10 : (int)s;
	if (g_pfg.bad || g_pfg.period_bad)
		logf_("WARN CONFIG perf: SOMVR_PERF=%s / SOMVR_PERF_S=%s not understood (on|off; seconds 1..600): %s, every %d s",
		      getenv("SOMVR_PERF") ? getenv("SOMVR_PERF") : "", getenv("SOMVR_PERF_S") ? getenv("SOMVR_PERF_S") : "",
		      g_pfg.on ? "on" : "off", g_pfg.period_s);
	logf_("CONFIG perf: %s (SOMVR_PERF; every %d s a PERF line: the game thread's frame - its wall, CPU, busy = the wall "
	      "less the sync's wait in eye 0 -, our H4 work before eye 0 and after eye 1, each eye pass, the render thread's "
	      "CPU a present, the process's CPU a frame)",
	      g_pfg.on ? "on" : "off", g_pfg.period_s);
	g_pfg.last_log = mono();
}

static int pfg_cmp(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;
	return x < y ? -1 : x > y;
}
static void pfg_stat(char *o, size_t cap, float *v, uint32_t n, uint32_t *over)
{
	if (over) {
		*over = 0;
		for (uint32_t i = 0; i < n; i++)
			*over += v[i] > g_pfg_bud;
	}
	if (!n) {
		snprintf(o, cap, "-");
		return;
	}
	double s = 0;
	for (uint32_t i = 0; i < n; i++)
		s += v[i];
	qsort(v, n, sizeof *v, pfg_cmp);
	uint32_t k = (n * 95u) / 100u;
	snprintf(o, cap, "%.2f/%.2f/%.2f/%.2f", s / n, v[n / 2], v[k < n ? k : n - 1], v[n - 1]);
}

static void pfg_log(double now)
{
	char s[PFG_N][48], rc[48], rw[48], rpc[48], rpw[48];
	uint32_t n = g_pfg.ng, ob = 0, oc = 0, orc = 0;
	somvr_time tm;
	g_pfg_bud = somvr_time_read(&g_sh, &tm) && tm.period > 0 ? (double)tm.period * 1e-6 : PFG_BUDGET_MS;
	for (int i = 0; i < PFG_N; i++)
		pfg_stat(s[i], sizeof s[i], g_pfg.g[i], n, i == PFG_BUSY ? &ob : i == PFG_CPU ? &oc : NULL);
	pthread_mutex_lock(&g_pfg.mx);
	uint32_t nr = g_pfg.nr;
	pfg_stat(rc, sizeof rc, g_pfg.rc, nr, &orc);
	pfg_stat(rw, sizeof rw, g_pfg.rw, nr, NULL);
	pfg_stat(rpc, sizeof rpc, g_pfg.rpc, nr, NULL);
	pfg_stat(rpw, sizeof rpw, g_pfg.rpw, nr, NULL);
	char re0[48], re1[48], rsc[48], rsw[48], rpk[48];
	pfg_stat(rpk, sizeof rpk, g_pfg.rpk, nr, NULL);
	uint32_t ors = 0, n0 = g_pfg.nre[0], n1 = g_pfg.nre[1], ns = g_pfg.nrs;
	pfg_stat(re0, sizeof re0, g_pfg.re_c[0], n0, NULL);
	pfg_stat(re1, sizeof re1, g_pfg.re_c[1], n1, NULL);
	pfg_stat(rsc, sizeof rsc, g_pfg.rs_c, ns, &ors);
	pfg_stat(rsw, sizeof rsw, g_pfg.rs_w, ns, NULL);
	g_pfg.nre[0] = g_pfg.nre[1] = g_pfg.nrs = 0;
	g_pfg.nr = 0;
	pthread_mutex_unlock(&g_pfg.mx);
	pthread_mutex_lock(&g_pfg.mx);
	if (g_pfg.sh_n[0][0] | g_pfg.sh_n[0][2] | g_pfg.sh_n[1][0] | g_pfg.sh_n[1][2]) {
		static const char *const kn[4] = { "sky (pre)", "lights", "sun cascades", "deferred lights" };
		char b[640];
		size_t o = 0;
		for (int k = 0; k < 4 && o < sizeof b; k++)
			o += (size_t)snprintf(b + o, sizeof b - o, "%s%s eye 0 %.3f/%.3f eye 1 %.3f/%.3f", k ? "; " : "", kn[k],
					      g_pfg.sh_n[0][k] ? g_pfg.sh_w[0][k] * 1e3 / g_pfg.sh_n[0][k] : 0.0,
					      g_pfg.sh_n[0][k] ? g_pfg.sh_c[0][k] * 1e3 / g_pfg.sh_n[0][k] : 0.0,
					      g_pfg.sh_n[1][k] ? g_pfg.sh_w[1][k] * 1e3 / g_pfg.sh_n[1][k] : 0.0,
					      g_pfg.sh_n[1][k] ? g_pfg.sh_c[1][k] * 1e3 / g_pfg.sh_n[1][k] : 0.0);
		logf_("PERF%s%s shadow passes (the render thread, wall/CPU ms a call; eye 1 skips %#x, %llu skipped): %s",
		      g_pfg.label[0] ? " " : "", g_pfg.label, atomic_load(&g_pfg.sh_skip), (unsigned long long)g_pfg.sh_skipped, b);
	}
	memset(g_pfg.sh_w, 0, sizeof g_pfg.sh_w), memset(g_pfg.sh_c, 0, sizeof g_pfg.sh_c);
	memset(g_pfg.sh_n, 0, sizeof g_pfg.sh_n);
	g_pfg.sh_skipped = 0;
	pthread_mutex_unlock(&g_pfg.mx);
	logf_("PERF%s%s t=%.6f s=%.1f frames=%u (mean/p50/p95/max ms) game thread: busy %s (over %.1f ms: %u) wall %s cpu %s "
	      "(over: %u); ours %s (before eye 0 %s, after eye 1 %s); eye 0 wall %s cpu %s; eye 1 wall %s cpu %s; process "
	      "cpu %s; our peeks a frame (process_vm_readv, the game thread) %s; render thread (%u presents): cpu %s (over: %u) "
	      "wall %s, of it the Present call (the layer's present "
	      "path, xrWaitFrame, the driver's) cpu %s wall %s; its frame work (eye 0's start to the present: %u frames) cpu "
	      "%s (over: %u) wall %s, eye 0 cpu %s, eye 1 cpu %s; its peeks %s",
	      g_pfg.label[0] ? " " : "", g_pfg.label, now, now - g_pfg.last_log, n, s[PFG_BUSY], g_pfg_bud, ob, s[PFG_WALL], s[PFG_CPU], oc, s[PFG_OURS], s[PFG_PRE],
	      s[PFG_POST], s[PFG_E0W], s[PFG_E0C], s[PFG_E1W], s[PFG_E1C], s[PFG_PROC], s[PFG_PEEKS], nr, rc, orc, rw, rpc, rpw, ns, rsc, ors, rsw, re0,
	      re1, rpk);
	if (g_pfg.part_n) {
		char pb[768];
		size_t po = 0;
		for (int k = 0; k < PFG_NPART && po < sizeof pb; k++)
			po += (size_t)snprintf(pb + po, sizeof pb - po, "%s%s %.3f/%.2f/%.0f", k ? ", " : "", pfg_part_name[k],
					       g_pfg.part_sum[k] * 1e3 / g_pfg.part_n, g_pfg.part_max[k],
					       (double)g_pfg.part_pksum[k] / g_pfg.part_n);
		logf_("PERF%s%s ours after eye 1 by function (%u frames, mean ms/max ms/peeks): %s", g_pfg.label[0] ? " " : "",
		      g_pfg.label, g_pfg.part_n, pb);
	}
	memset(g_pfg.part_sum, 0, sizeof g_pfg.part_sum), memset(g_pfg.part_max, 0, sizeof g_pfg.part_max);
	memset(g_pfg.part_pksum, 0, sizeof g_pfg.part_pksum);
	g_pfg.part_n = 0;
	g_pfg.ng = 0;
	g_pfg.last_log = now;
	g_pfg.logs++;
}

/* every H4 call's entry (game thread): the previous frame's sample, if it was stereo; the period's line */
static void pfg_h4_entry(void)
{
	if (!g_pfg.on)
		return;
	double w = mono(), c = pfg_clock(CLOCK_THREAD_CPUTIME_ID), p = pfg_clock(CLOCK_PROCESS_CPUTIME_ID);
	if (g_pfg.have && g_pfg.stereo && g_pfg.ng < PFG_WIN) {
		uint32_t k = g_pfg.ng++;
		double wall = w - g_pfg.w_in, e0w = g_pfg.e_w[0][1] - g_pfg.e_w[0][0], e0c = g_pfg.e_c[0][1] - g_pfg.e_c[0][0];
		double wait = e0w - e0c > 0 ? e0w - e0c : 0;
		double pre = g_pfg.e_w[0][0] - g_pfg.w_in, post = g_pfg.w_done - g_pfg.e_w[1][1];
		g_pfg.g[PFG_WALL][k] = (float)(wall * 1e3);
		g_pfg.g[PFG_CPU][k] = (float)((c - g_pfg.c_in) * 1e3);
		g_pfg.g[PFG_BUSY][k] = (float)((wall - wait) * 1e3);
		g_pfg.g[PFG_PRE][k] = (float)(pre * 1e3);
		g_pfg.g[PFG_POST][k] = (float)(post * 1e3);
		g_pfg.g[PFG_OURS][k] = (float)((pre + post) * 1e3);
		g_pfg.g[PFG_E0W][k] = (float)(e0w * 1e3);
		g_pfg.g[PFG_E0C][k] = (float)(e0c * 1e3);
		g_pfg.g[PFG_E1W][k] = (float)((g_pfg.e_w[1][1] - g_pfg.e_w[1][0]) * 1e3);
		g_pfg.g[PFG_E1C][k] = (float)((g_pfg.e_c[1][1] - g_pfg.e_c[1][0]) * 1e3);
		g_pfg.g[PFG_PROC][k] = (float)((p - g_pfg.p_in) * 1e3);
		g_pfg.g[PFG_PEEKS][k] = (float)(g_peeks - g_pfg.pk_in); /* a count, not ms */
		g_pfg.frames++;
	}
	g_pfg.have = 1, g_pfg.stereo = 0;
	g_pfg.w_in = w, g_pfg.c_in = c, g_pfg.p_in = p, g_pfg.pk_in = g_peeks;
	if (atomic_exchange(&g_pfg.relabel, 0)) { /* the word `perf NAME`: the window so far under its old name */
		pfg_log(w);
		pthread_mutex_lock(&g_pfg.mx);
		memcpy(g_pfg.label, g_pfg.next_label, sizeof g_pfg.label);
		pthread_mutex_unlock(&g_pfg.mx);
	} else if (w - g_pfg.last_log >= g_pfg.period_s) {
		pfg_log(w);
	}
}
/* the stereo path: eye e's pass starts (end 0) or ends (end 1) */
static void pfg_eye(int e, int end)
{
	if (!g_pfg.on)
		return;
	g_pfg.e_w[e][end] = mono();
	g_pfg.e_c[e][end] = pfg_clock(CLOCK_THREAD_CPUTIME_ID);
	if (e == 1 && end)
		g_pfg.part_pk0 = g_peeks; /* the post's peeks from here */
}
/* the stereo path, our work after eye 1: function k done */
static void pfg_part(int k)
{
	if (!g_pfg.on || k < 0 || k >= PFG_NPART)
		return;
	double t = mono();
	g_pfg.part[k] = t - (k ? g_pfg.part_t : g_pfg.e_w[1][1]);
	g_pfg.part_t = t;
	g_pfg.part_pk[k] = g_peeks - g_pfg.part_pk0, g_pfg.part_pk0 = g_peeks;
}
/* the stereo path's end (our work after eye 1 done): this frame is a sample at the next entry */
static void pfg_h4_done(void)
{
	if (!g_pfg.on)
		return;
	g_pfg.w_done = mono();
	g_pfg.part[PFG_NPART - 1] = g_pfg.w_done - g_pfg.part_t;
	g_pfg.part_pk[PFG_NPART - 1] = g_peeks - g_pfg.part_pk0;
	for (int k = 0; k < PFG_NPART; k++) {
		g_pfg.part_sum[k] += g_pfg.part[k], g_pfg.part_pksum[k] += g_pfg.part_pk[k];
		if (g_pfg.part[k] * 1e3 > g_pfg.part_max[k])
			g_pfg.part_max[k] = (float)(g_pfg.part[k] * 1e3);
	}
	g_pfg.part_n++;
	g_pfg.stereo = 1;
}
/* every present (H9, the render thread) */
static void pfg_h9(void)
{
	if (!g_pfg.on)
		return;
	double w = mono(), c = pfg_clock(CLOCK_THREAD_CPUTIME_ID);
	pthread_mutex_lock(&g_pfg.mx);
	if (g_pfg.re_have0 && g_pfg.nrs < PFG_WIN) { /* eye 0's start to this present */
		uint32_t k = g_pfg.nrs++;
		g_pfg.rs_c[k] = (float)((c - g_pfg.re_c0[0]) * 1e3), g_pfg.rs_w[k] = (float)((w - g_pfg.re_w0[0]) * 1e3);
	}
	g_pfg.re_have0 = 0;
	if (g_pfg.r_have && g_pfg.nr < PFG_WIN) {
		uint32_t k = g_pfg.nr++;
		g_pfg.rc[k] = (float)((c - g_pfg.r_c) * 1e3), g_pfg.rw[k] = (float)((w - g_pfg.r_w) * 1e3);
		g_pfg.rpc[k] = (float)(g_pfg.rp_c * 1e3), g_pfg.rpw[k] = (float)(g_pfg.rp_w * 1e3);
		g_pfg.rpk[k] = (float)(g_peeks - g_pfg.r_pk);
	}
	g_pfg.r_have = 1, g_pfg.r_w = w, g_pfg.r_c = c, g_pfg.r_pk = g_peeks;
	pthread_mutex_unlock(&g_pfg.mx);
}
/* ---- SOMVR_SHADOW_SHARE (build 27, re-xr-pipeline.md 115.5): eye 1 draws with eye 0's shadow maps ---------------
 * The opaque stage's four shadow passes (HEAD_SHADOW_PRE / _LIGHTS / _CASC / _LIGHTS2, the render thread) ran once per
 * eye: eye 1 re-rendered the sun's cascades (and the sky's and the lights' maps) from a view 6.4 cm to the side. With
 * the switch eye 1 skips the passes named and keeps eye 0's maps and state from the same frame (eye 0's pass of that
 * kind ran in this frame, else eye 1 draws its own); the caller gets eye 0's return value (the sky pass's decides a
 * branch). Saves eye 1's GPU time of the pass and the render thread's work for it (115.4).
 *   SOMVR_SHADOW_SHARE=off       both eyes draw every shadow map (build 26)
 *   SOMVR_SHADOW_SHARE=cascades  eye 1 uses eye 0's sun cascades (the pass FUN_007f6250, call 0x7dd605)
 *   SOMVR_SHADOW_SHARE=all       ... and the sky's and the lights' maps (FUN_007f5820, FUN_007db760)
 * Never the deferred lights (FUN_007dbd30 shades too). The test word `shadowshare off|cascades|all` (live), and for the
 * check (scripts/perf27-eyediff.py --alt) `shadowshare alt|altall|altbad`: the same only in odd frames (altbad: eye 1
 * without its deferred lights = wrong lighting, the check's control), so three dumped frames in a row go off-on-off
 * or on-off-on in eye 1 alone. Lines:
 * `CONFIG shadow_share`, `SHSHARE ...` at a change, `SHSHARE END: N skipped by kind`. */
#define SSH_CASC (1u << 2)
#define SSH_ALL ((1u << 0) | (1u << 1) | (1u << 2))
static atomic_uint g_ssh;               /* the kinds eye 1 skips (bits: 0 sky, 1 lights, 2 cascades; 3 the control) */
static atomic_int g_ssh_alt;            /* the check: only in odd frames */
/* the sun cascades' pass (FUN_007f6250, its first argument = the pipeline state P = 0x4625840) ends - when it drew
 * them - with P+0x3809 = 1 (the shadow mask FUN_007e2940 draws no shadow without it: the pipeline FUN_007d6e20 clears it
 * for every view, 0x7d7897), P+0x70f0 = 0 and five copies P+0x3918/30/48/60/78 = P+0x3910/28/40/58/70 (0x7f8eec..):
 * eye 1's skip leaves them as eye 0's pass left them (build 27's first try without this: eye 1 had no sun shadows, the
 * check of 115.5) */
#define SSH_P 0x4625840ul
static const uint32_t g_ssh_off[7] = { 0x3809, 0x70f0, 0x3918, 0x3930, 0x3948, 0x3960, 0x3978 };
static uint64_t g_ssh_val[7];           /* eye 0's values after its pass (the first one byte) */
static uint64_t g_ssh_vframe;           /* ... of this frame */
static uint64_t g_ssh_badp;             /* a pass with another first argument: not shared */
static uint64_t g_ssh_frame0[4];        /* the render thread: the frame of eye 0's last pass of each kind */
static uint64_t g_ssh_n[4], g_ssh_own;  /* skipped by kind; eye 1 drew its own (no eye 0 pass this frame) */
static int ssh_parse(const char *p, unsigned *m)
{
	if (!p || !*p || !strcmp(p, "off") || !strcmp(p, "0"))
		return *m = 0, 1;
	if (!strcmp(p, "cascades"))
		return *m = SSH_CASC, 1;
	if (!strcmp(p, "all"))
		return *m = SSH_ALL, 1;
	return 0;
}
static int ssh_env_on(void)
{
	unsigned m = 0;
	return ssh_parse(getenv("SOMVR_SHADOW_SHARE"), &m) && m;
}
static void ssh_config(void)
{
	unsigned m = 0;
	const char *p = getenv("SOMVR_SHADOW_SHARE");
	if (!ssh_parse(p, &m))
		logf_("WARN CONFIG shadow_share: SOMVR_SHADOW_SHARE=%s is not off|cascades|all: off", p);
	atomic_store(&g_ssh, m);
	logf_("CONFIG shadow_share=%s (SOMVR_SHADOW_SHARE: eye 1 %s; the hooks HEAD_SHADOW_* %s)",
	      m == SSH_ALL ? "all" : m ? "cascades" : "off",
	      m == SSH_ALL ? "draws with eye 0's sun cascades, sky and lights' shadow maps"
	      : m ? "draws with eye 0's sun cascades" : "draws its own shadow maps",
	      g_ex[EX_SH_CASC].installed ? "in" : "not in: nothing shared");
}
static const char *ssh_word(const char *w)
{
	unsigned m;
	if (strncmp(w, "shadowshare ", 12))
		return NULL;
	char a[16];
	snprintf(a, sizeof a, "%.*s", (int)strcspn(w + 12, "\r\n "), w + 12);
	int alt = !strcmp(a, "alt") || !strcmp(a, "altall") || !strcmp(a, "altbad");
	if (alt)
		m = !strcmp(a, "alt") ? SSH_CASC : !strcmp(a, "altall") ? SSH_ALL : 1u << 3;
	else if (!ssh_parse(a, &m))
		return "shadowshare (not off|cascades|all|alt|altall|altbad: ignored)";
	atomic_store(&g_ssh_alt, alt);
	atomic_store(&g_ssh, m);
	logf_("SHSHARE t=%.3f the word: %s (eye 1 skips %#x)", mono(), a, m);
	return "shadowshare";
}
static void ssh_end_log(void)
{
	logf_("SHSHARE END: eye 1 skipped sky %llu, lights %llu, cascades %llu; drew its own (no eye 0 pass that frame) %llu, "
	      "(a cascade pass not on the pipeline state 0x4625840) %llu",
	      (unsigned long long)g_ssh_n[0], (unsigned long long)g_ssh_n[1], (unsigned long long)g_ssh_n[2],
	      (unsigned long long)g_ssh_own, (unsigned long long)g_ssh_badp);
}

/* the render thread's shadow passes (head_window): eye 1 skips a pass of SOMVR_SHADOW_SHARE or `perf skip MASK`
 * (1 = handled: *rax = eye 0's return value) */
static int pfg_sh_skip(int k, int eye, uint64_t frame, uintptr_t p, uint64_t *rax)
{
	if (eye != 1 || k < 0 || k > 3)
		return 0;
	if (k == 2 && (p != SSH_P || g_ssh_vframe != frame)) { /* the cascades: only with eye 0's state of this frame */
		if (p != SSH_P)
			g_ssh_badp++;
		return 0;
	}
	unsigned share = atomic_load_explicit(&g_ssh, memory_order_relaxed) & (1u << k);
	if (share && atomic_load_explicit(&g_ssh_alt, memory_order_relaxed) && !(frame & 1))
		share = 0; /* the check: even frames draw their own */
	unsigned test = g_pfg.on ? atomic_load_explicit(&g_pfg.sh_skip, memory_order_relaxed) & (1u << k) : 0;
	if (!share && !test)
		return 0;
	if (share && g_ssh_frame0[k] != frame) { /* no eye 0 pass of this kind in this frame: eye 1 draws its own */
		g_ssh_own++;
		return 0;
	}
	*rax = g_pfg.sh_rax[k];
	if (k == 2) { /* the state eye 0's cascade pass left (P is the game's static pipeline state) */
		*(volatile uint8_t *)(p + g_ssh_off[0]) = (uint8_t)g_ssh_val[0];
		for (int i = 1; i < 7; i++)
			*(volatile uint64_t *)(p + g_ssh_off[i]) = g_ssh_val[i];
	}
	if (share)
		g_ssh_n[k]++;
	if (g_pfg.on) {
		pthread_mutex_lock(&g_pfg.mx);
		g_pfg.sh_skipped++;
		pthread_mutex_unlock(&g_pfg.mx);
	}
	return 1;
}
static double pfg_sh_t0(void)
{
	if (!g_pfg.on)
		return 0.0;
	g_pfg.sh_c0 = pfg_clock(CLOCK_THREAD_CPUTIME_ID); /* the render thread only */
	return mono();
}
static void pfg_sh_t1(int k, int eye, uint64_t frame, uintptr_t p, double w0, uint64_t rax)
{
	if (eye == 0 && k >= 0 && k <= 3) /* SOMVR_SHADOW_SHARE: eye 0's pass of this frame, its return value */
		g_ssh_frame0[k] = frame, g_pfg.sh_rax[k] = rax;
	if (eye == 0 && k == 2 && p == SSH_P) { /* ... and the state its cascade pass left */
		g_ssh_val[0] = *(volatile const uint8_t *)(p + g_ssh_off[0]);
		for (int i = 1; i < 7; i++)
			g_ssh_val[i] = *(volatile const uint64_t *)(p + g_ssh_off[i]);
		g_ssh_vframe = frame;
	}
	if (!g_pfg.on || eye < 0 || eye > 1 || k < 0 || k > 3)
		return;
	double w = mono(), c = pfg_clock(CLOCK_THREAD_CPUTIME_ID);
	pthread_mutex_lock(&g_pfg.mx);
	g_pfg.sh_w[eye][k] += w - w0, g_pfg.sh_c[eye][k] += c - g_pfg.sh_c0, g_pfg.sh_n[eye][k]++;
	pthread_mutex_unlock(&g_pfg.mx);
}

/* the trigger word `perf NAME` (the trigger thread): the game thread logs the window so far and names the next;
 * `perf skip MASK` (a measurement, never for play): eye 1 skips the shadow passes in MASK (1 sky, 2 lights, 4 sun
 * cascades, 8 deferred lights) and gets eye 0's maps; `perf console CMD` (a measurement): CMD on the game's console
 * after the next stereo frame (somvr_render_game.c's runner, without SOMVR_FX_TEST) */
static const char *pfg_word(const char *w)
{
	if (strncmp(w, "perf ", 5))
		return NULL;
	if (!g_pfg.on)
		return "perf (off: SOMVR_PERF is not on)";
	unsigned m;
	if (!strncmp(w, "perf console ", 13) && w[13]) { /* a measurement: the game's console (e.g. Render.Setting.*) without
							   * SOMVR_FX_TEST's hooks (they cost the render thread ~25 ms) */
		if (atomic_load(&g_rd.cmd_pend))
			return "perf console (one is pending: ignored)";
		snprintf(g_rd.cmd, sizeof g_rd.cmd, "%.*s", (int)strcspn(w + 13, "\r\n"), w + 13);
		atomic_store_explicit(&g_rd.cmd_pend, 1, memory_order_release);
		return "perf console (after the next stereo frame)";
	}
	if (sscanf(w, "perf skip %i", (int *)&m) == 1) {
		atomic_store(&g_pfg.sh_skip, m & 15u);
		logf_("PERF the word: perf skip %#x (eye 1 skips:%s%s%s%s)", m & 15u, m & 1 ? " the sky shadow" : "",
		      m & 2 ? " the lights' shadows" : "", m & 4 ? " the sun cascades" : "", m & 8 ? " the deferred lights" : "");
		return "perf skip";
	}
	pthread_mutex_lock(&g_pfg.mx);
	snprintf(g_pfg.next_label, sizeof g_pfg.next_label, "%.*s", (int)strcspn(w + 5, "\r\n "), w + 5);
	pthread_mutex_unlock(&g_pfg.mx);
	atomic_store(&g_pfg.relabel, 1);
	return "perf";
}
/* the render thread: eye e's work starts (H3's match, end 0) or ends (H8, end 1) */
static void pfg_rt_eye(int e, int end)
{
	if (!g_pfg.on || e < 0 || e > 1)
		return;
	double w = mono(), c = pfg_clock(CLOCK_THREAD_CPUTIME_ID);
	pthread_mutex_lock(&g_pfg.mx);
	if (!end) {
		g_pfg.re_w0[e] = w, g_pfg.re_c0[e] = c;
		if (!e)
			g_pfg.re_have0 = 1;
	} else if (g_pfg.re_w0[e] > 0 && g_pfg.nre[e] < PFG_WIN) {
		g_pfg.re_c[e][g_pfg.nre[e]++] = (float)((c - g_pfg.re_c0[e]) * 1e3);
	}
	pthread_mutex_unlock(&g_pfg.mx);
}
static int pfg_env_on(void) /* before pfg_config (the hooks' install) */
{
	const char *p = getenv("SOMVR_PERF");
	return p && (!strcmp(p, "on") || !strcmp(p, "1"));
}
/* around H9's Present call (the render thread): its CPU and wall, for the next present's sample */
static void pfg_h9_call(int end)
{
	if (!g_pfg.on)
		return;
	static __thread double w0, c0;
	double w = mono(), c = pfg_clock(CLOCK_THREAD_CPUTIME_ID);
	if (!end) {
		w0 = w, c0 = c;
		return;
	}
	pthread_mutex_lock(&g_pfg.mx);
	g_pfg.rp_w = w - w0, g_pfg.rp_c = c - c0;
	pthread_mutex_unlock(&g_pfg.mx);
}
static void pfg_end_log(void)
{
	if (g_pfg.on)
		logf_("PERF END: %llu stereo frames timed, %llu lines", (unsigned long long)g_pfg.frames,
		      (unsigned long long)g_pfg.logs);
}
