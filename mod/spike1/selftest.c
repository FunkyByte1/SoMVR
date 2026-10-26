/*
 * selftest.c - runs somvr_spike1's patching, wrappers, logging and trigger against stand-in functions in this
 * program (no game): two call-site hooks (H1, H3) and one vtable slot (H2) built at run time, then checks that
 * the offsets appear only while the trigger file exists, at the hook it names, and that H2 never writes through
 * the caller's pointer. sbs layout A (halves) and B (sbsB/sbsB0: full rect and FOV) around a stand-in body (H4), the
 * comfort pokes and the "blur" word's restore, the two log hooks (EXPO call site, AUDIO vtable slot: arguments and
 * return values pass through unchanged, rate limits, guarded reads). Then the save guard against a stand-in vtable
 * (never reaching the originals) and its fail-closed exit (status 3) in forked children. Run by ./build.sh --check.
 */
#define SOMVR_SELFTEST
#include "somvr_spike1.c"
#include <sys/wait.h>

static float g_seen_h2[7], g_seen_h3[7], g_seen_rect[4];

__attribute__((noinline, noclone)) uint64_t t_h1(void *cam, float *view, void *sp)
{
	(void)cam, (void)sp;
	static const float v[11] = { 100, 200, 300, 0, 0, 0, 1, 1.5f, 0.9f, 7, 100000 };
	memcpy(view, v, sizeof v);
	return 0;
}
__attribute__((noinline, noclone)) uint64_t t_h2(float nz, float fz, void *self, const float *xf, const float *fov,
						    const float *rect)
{
	(void)self, (void)fov, (void)rect;
	memcpy(g_seen_h2, xf, sizeof g_seen_h2);
	memcpy(g_seen_rect, rect, sizeof g_seen_rect);
	return nz == 7.0f && fz == 100000.0f;
}
__attribute__((noinline, noclone)) uint64_t t_h3(float nz, float fz, void *ds, const float *xf, float *tan_half,
						    float *center, int *prect, int type)
{
	(void)ds, (void)tan_half, (void)center, (void)prect;
	memcpy(g_seen_h3, xf, sizeof g_seen_h3);
	return nz == 7.0f && fz == 100000.0f && type == 0;
}
typedef uint64_t (*t_h2_fn)(float, float, void *, const float *, const float *, const float *);
static void *volatile g_vt[40];

__attribute__((noinline, noclone)) uint64_t c_h1(void *cam, float *view, void *sp) { return t_h1(cam, view, sp) + 1; }
__attribute__((noinline, noclone)) uint64_t c_h3(void *ds, const float *xf, float *th, float *co, int *pr)
{
	return t_h3(7.0f, 100000.0f, ds, xf, th, co, pr, 0) + 1;
}

static char g_hex[HK_N][256];
static int call_site(void *caller, void *target, struct site_spec *s, const char *name, int i)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 64; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_hex[i], 128);
			hex_of(target, 4, g_hex[i] + 128, 128);
			*s = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_hex[i],
						 (uintptr_t)target, g_hex[i] + 128 };
			return 0;
		}
	}
	return -1;
}

/* H4 stand-in: the per-view body. It checks the per-call state it starts from, builds a view through H1 and a
 * viewport through H2 like the real body, then changes its per-call state (as FUN_009ee170 does) */
static unsigned char g_fol[FOLIAGE_LEN];
static int g_h4_calls, g_h4_first[4], g_h4_bad_state;
static float g_h4_x[4], g_h4_fov[4], g_h4_rect[4][4];
__attribute__((noinline, noclone)) uint64_t c_h1(void *cam, float *view, void *sp);
__attribute__((noinline, noclone)) uint64_t t_h4(void *gm, void *rt, const int *org, void *pl, int ef, int first)
{
	(void)rt, (void)org, (void)pl, (void)ef;
	int k = g_h4_calls++;
	if (k < 4) {
		char cam[0x510] = { 0 }, self[0x100] = { 0 };
		float view[11], xf[7] = { 1, 2, 3, 0, 0, 0, 1 }, fov[2] = { 1.5f, 0.9f }, rect[4] = { 0, 0, 1, 1 };
		g_h4_first[k] = first;
		if (g_fol[0] != 0x11 || ((unsigned char *)gm)[0x20] != 0x22)
			g_h4_bad_state++;
		c_h1(cam, view, NULL);
		g_h4_x[k] = view[0];
		g_h4_fov[k] = view[7];
		((t_h2_fn)g_vt[37])(7.0f, 100000.0f, self, xf, fov, rect);
		memcpy(g_h4_rect[k], g_seen_rect, sizeof g_h4_rect[k]);
	}
	g_fol[0] = 0x99;
	((unsigned char *)gm)[0x20] = 0x77;
	return 5;
}
__attribute__((noinline, noclone)) uint64_t c_h4(void *gm, void *rt, const int *org, void *pl)
{
	return t_h4(gm, rt, org, pl, 2, 1) + 1;
}

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static void round_(const char *what, float want_h1, float want_h2)
{
	char cam[0x510] = { 0 }, self[0x100] = { 0 };
	float view[11] = { 0 }, xf[7] = { 1, 2, 3, 0, 0, 0, 1 }, fov[2] = { 1.5f, 0.9f }, rect[4] = { 0, 0, 1, 1 };
	float th[2] = { 1, 1 }, co[2] = { 0, 0 };
	int pr[4] = { 0, 0, 1920, 1080 };
	EXPECT(c_h1(cam, view, NULL) == 1, "%s: H1 return value", what);
	EXPECT(view[0] == 100 + want_h1 && view[1] == 200 && view[2] == 300, "%s: H1 view pos %g,%g,%g want x+%g", what,
	       view[0], view[1], view[2], want_h1);
	EXPECT(((t_h2_fn)g_vt[37])(7.0f, 100000.0f, self, xf, fov, rect) == 1, "%s: H2 return value/xmm args", what);
	EXPECT(g_seen_h2[0] == 1 + want_h2 && g_seen_h2[1] == 2 && g_seen_h2[2] == 3, "%s: H2 saw pos %g,%g,%g want x+%g",
	       what, g_seen_h2[0], g_seen_h2[1], g_seen_h2[2], want_h2);
	EXPECT(xf[0] == 1, "%s: H2 wrote through the caller's xf", what);
	EXPECT(c_h3(self, xf, th, co, pr) == 2, "%s: H3 return value/args", what);
	EXPECT(g_seen_h3[0] == 1, "%s: H3 xf", what);
	printf("round %-10s H1 x=%g H2 x=%g\n", what, view[0], g_seen_h2[0]);
}

static void set_trigger(const char *path, const char *word);
static void sbs_round(const char *what, const char *trig, const char *word, int want_calls, float dx, int full)
{
	char gm[0x100] = { 0 };
	set_trigger(trig, word);
	g_fol[0] = 0x11;
	gm[0x20] = 0x22;
	g_h4_calls = 0;
	g_h4_bad_state = 0;
	EXPECT(c_h4(gm, NULL, NULL, NULL) == 6, "%s: H4 return value", what);
	EXPECT(g_h4_calls == want_calls, "%s: the body ran %d times, want %d", what, g_h4_calls, want_calls);
	EXPECT(g_h4_first[0] == 1 && (want_calls < 2 || g_h4_first[1] == 0), "%s: first flags %d,%d", what, g_h4_first[0],
	       g_h4_first[1]);
	EXPECT(g_h4_bad_state == 0, "%s: eye R did not start from eye L's per-call state", what);
	if (want_calls == 2) {
		float half = full ? 1.5f : 2.0f * atanf(0.5f * tanf(0.75f)); /* layout B: the full fovX */
		EXPECT(fabsf(g_h4_x[0] - (100 - dx)) < 1e-3f && fabsf(g_h4_x[1] - (100 + dx)) < 1e-3f, "%s: eye x %g,%g", what,
		       g_h4_x[0], g_h4_x[1]);
		EXPECT(fabsf(g_h4_fov[0] - half) < 1e-5f && fabsf(g_h4_fov[1] - half) < 1e-5f, "%s: eye fovX %g,%g want %g", what,
		       g_h4_fov[0], g_h4_fov[1], half);
		if (full)
			EXPECT(g_h4_rect[0][0] == 0 && g_h4_rect[0][1] == 0 && g_h4_rect[0][2] == 1 && g_h4_rect[0][3] == 1 &&
			       g_h4_rect[1][0] == 0 && g_h4_rect[1][1] == 0 && g_h4_rect[1][2] == 1 && g_h4_rect[1][3] == 1,
			       "%s: layout B eye rects %g..%g, %g..%g (want 0..1 both)", what, g_h4_rect[0][0], g_h4_rect[0][2],
			       g_h4_rect[1][0], g_h4_rect[1][2]);
		else
			EXPECT(g_h4_rect[0][0] == 0 && g_h4_rect[0][2] == 0.5f && g_h4_rect[1][0] == 0.5f && g_h4_rect[1][2] == 1,
			       "%s: eye rects %g..%g, %g..%g", what, g_h4_rect[0][0], g_h4_rect[0][2], g_h4_rect[1][0],
			       g_h4_rect[1][2]);
	} else {
		EXPECT(g_h4_x[0] == 100 && g_h4_fov[0] == 1.5f && g_h4_rect[0][2] == 1, "%s: one call must be unchanged", what);
	}
	printf("sbs   %-10s calls=%d x=%g,%g fovX=%g rect R=%g..%g\n", what, g_h4_calls, g_h4_x[0],
	       want_calls == 2 ? g_h4_x[1] : 0.0f, g_h4_fov[0], g_h4_rect[1][0], g_h4_rect[1][2]);
}

static void set_trigger(const char *path, const char *word)
{
	if (!word) {
		unlink(path);
	} else {
		FILE *f = fopen(path, "w");
		fputs(word, f);
		fclose(f);
	}
	struct timespec ts = { 0, 600 * 1000 * 1000 };
	nanosleep(&ts, NULL);
}

/* the last log line that starts with prefix (flushes first) */
static char g_logp[1024], g_line[4096];
static const char *last_line(const char *prefix)
{
	flush_log();
	g_line[0] = 0;
	FILE *f = fopen(g_logp, "r");
	char l[4096];
	size_t pn = strlen(prefix);
	while (f && fgets(l, sizeof l, f))
		if (!strncmp(l, prefix, pn))
			snprintf(g_line, sizeof g_line, "%s", l);
	if (f)
		fclose(f);
	return g_line;
}
#define HAS(line, sub, what) EXPECT(strstr(line, sub), "%s: '%s' not in: %s", what, sub, line)

/* comfort pokes against a buffer: shake (want 1), motion blur, DoF, the two velocity-blur bytes (want 0) */
static unsigned char g_pk[PK_N] = { 0, 1, 1, 1, 1 };
static void poke_check(const char *what, const unsigned char want[PK_N])
{
	EXPECT(!memcmp(g_pk, want, PK_N), "%s: poke bytes %u%u%u%u%u want %u%u%u%u%u", what, g_pk[0], g_pk[1], g_pk[2],
	       g_pk[3], g_pk[4], want[0], want[1], want[2], want[3], want[4]);
	printf("poke  %-10s bytes=%u%u%u%u%u\n", what, g_pk[0], g_pk[1], g_pk[2], g_pk[3], g_pk[4]);
}

/* EXPO stand-in: FUN_007ea320 stores k_fRealTime at state+4 (the step stamp) and a new exposure at state+0 */
static float g_real, g_est[2];
static uintptr_t g_bound;
static unsigned char g_rtobj[0x60];
static void *g_expo_seen[2];
__attribute__((noinline, noclone)) uint64_t t_expo(void *state, void *ctx)
{
	g_expo_seen[0] = state, g_expo_seen[1] = ctx;
	((float *)state)[1] = g_real;
	((float *)state)[0] = 2.0f;
	return 0x1234567890ull;
}
__attribute__((noinline, noclone)) uint64_t c_expo(void *state, void *ctx) { return t_expo(state, ctx) + 1; }

static int expo_burst(const char *what, int calls)
{
	uint64_t l0 = atomic_load(&g_h[HK_EXPO].logged);
	int bad = 0;
	for (int i = 0; i < calls; i++) {
		g_est[0] = 1.0f, g_est[1] = 4.0f, g_real = 5.0f;
		g_expo_seen[0] = g_expo_seen[1] = NULL;
		bad += c_expo(g_est, (void *)0x5150) != 0x1234567891ull || g_expo_seen[0] != g_est ||
		       g_expo_seen[1] != (void *)0x5150 || g_est[0] != 2.0f || g_est[1] != 5.0f;
	}
	EXPECT(!bad, "%s: EXPO changed the arguments or the return value (%d of %d calls)", what, bad, calls);
	return (int)(atomic_load(&g_h[HK_EXPO].logged) - l0);
}

static void expo_tests(const char *trig)
{
	g_bound = (uintptr_t)g_rtobj;
	g_rtobj[8] = 0x81;
	uintptr_t set = 0xabcdef;
	memcpy(g_rtobj + 0x50, &set, sizeof set);
	set_trigger(trig, NULL);
	atomic_store(&g_trig_off_us, mono_us() - 3000000u); /* the off was 3 s ago: the 2-s window is closed */
	int n = expo_burst("expo quiet", 10);
	EXPECT(n == 1, "EXPO outside the trigger window logged %d of 10 quick calls, want 1 (<= 2/s)", n);
	const char *l = last_line("EXPO ");
	HAS(l, "real=5 last=4->5 step=1 expo=1->2", "EXPO line");
	HAS(l, "hdr=1 set=0xabcdef", "EXPO line");
	HAS(l, "win=0", "EXPO line");
	set_trigger(trig, "sbsB");
	n = expo_burst("expo trigger", 10);
	EXPECT(n == 10, "EXPO while the trigger stands logged %d of 10 calls", n);
	HAS(last_line("EXPO "), "win=1", "EXPO line in the trigger");
	set_trigger(trig, NULL);
	n = expo_burst("expo tail", 10);
	EXPECT(n == 10, "EXPO within 2 s after the trigger logged %d of 10 calls", n);
	g_bound = 0;
	expo_burst("expo rt null", 1);
	HAS(last_line("EXPO "), "rt=0 hdr=- set=-", "EXPO line, no bound target");
	void *pg = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	munmap(pg, 4096);
	g_bound = (uintptr_t)pg; /* a stale pointer: the guarded reads must fail, not fault */
	expo_burst("expo rt stale", 1);
	HAS(last_line("EXPO "), "hdr=? set=?", "EXPO line, stale bound target");
	printf("expo  quiet=1/10 trigger=10/10 tail=10/10, guarded reads ok\n");
}

/* AUDIO stand-in: CSoundMgr::vf_23 (returns 0 without touching the vectors while this+8 == 0, else copies them) */
static void *volatile g_avt[2];
static unsigned char g_mgr_up[16] = { [8] = 1 }, g_mgr_down[16];
static float g_aud_seen[9];
static void *g_aud_self;
static uint32_t g_aud_idx;
typedef uint64_t (*t_aud_fn)(void *, const float *, const float *, const float *, uint32_t);
__attribute__((noinline, noclone)) uint64_t t_aud(void *self, const float *pos, const float *front, const float *top,
						     uint32_t idx)
{
	g_aud_self = self, g_aud_idx = idx;
	if (!((unsigned char *)self)[8])
		return 0;
	memcpy(g_aud_seen, pos, 12);
	memcpy(g_aud_seen + 3, front, 12);
	memcpy(g_aud_seen + 6, top, 12);
	return 0x7700 | (idx & 1);
}

static int aud_burst(const char *what, uint32_t idx, int calls, const float *front)
{
	static const float pos[3] = { 100, 200, 330 }, fr[3] = { 0, 0, 1 }, top[3] = { 0, 1, 0 };
	void *self = front ? (void *)g_mgr_down : (void *)g_mgr_up; /* a given front = a bad pointer: manager down */
	uint64_t l0 = atomic_load(&g_h[HK_AUD].logged);
	int bad = 0;
	for (int i = 0; i < calls; i++) {
		memset(g_aud_seen, 0, sizeof g_aud_seen);
		uint64_t rv = ((t_aud_fn)g_avt[1])(self, pos, front ? front : fr, top, idx);
		bad += rv != (front ? 0 : 0x7700u | (idx & 1)) || g_aud_self != self || g_aud_idx != idx ||
		       (!front && (memcmp(g_aud_seen, pos, 12) || memcmp(g_aud_seen + 3, fr, 12) || memcmp(g_aud_seen + 6, top, 12)));
	}
	EXPECT(!bad, "%s: AUDIO changed the arguments or the return value (%d of %d calls)", what, bad, calls);
	return (int)(atomic_load(&g_h[HK_AUD].logged) - l0);
}

static void audio_tests(const char *trig)
{
	/* static: the stand-in t_h1 never reads *cam, so GCC (IPA modref) may drop stores into a local cam */
	static char cam[0x510];
	static float obj[5] = { 0, 0, 7, 8, 9 }; /* *(cam+0x338) + 8 = 7,8,9 */
	float view[11];
	void *op = obj;
	memcpy(cam + 0x338, &op, sizeof op);
	set_trigger(trig, NULL);
	c_h1(cam, view, NULL); /* the last H1 camera: 100,200,300 */
	int n = aud_burst("audio idx0", 0, 5, NULL) + aud_burst("audio idx1", 1, 5, NULL) +
		aud_burst("audio idx20", 20, 3, NULL) + aud_burst("audio idx9", 9, 3, NULL);
	EXPECT(n == 3, "AUDIO without the trigger logged %d lines for idx 0,1,20,9 x5/5/3/3, want 3 (1/s per idx)", n);
	const char *l = last_line("AUDIO ");
	HAS(l, "idx=20 ret=0 pos=100,200,330 front=0,0,1 top=0,1,0 cam=100,200,300", "AUDIO line");
	HAS(l, "d_cam=30 ", "AUDIO line");
	HAS(l, "objpos=7,8,9", "AUDIO line");
	set_trigger(trig, "");
	n = aud_burst("audio trigger", 1, 5, NULL);
	EXPECT(n == 5, "AUDIO while the trigger stands logged %d of 5 calls", n);
	HAS(last_line("AUDIO "), "idx=1 ret=1 ", "AUDIO line in the trigger");
	void *pg = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	munmap(pg, 4096);
	aud_burst("audio stale front", 1, 1, (const float *)pg); /* the hook's guarded read must not fault on it */
	HAS(last_line("AUDIO "), "ret=0 pos=100,200,330 front=?(0x", "AUDIO line, stale front");
	set_trigger(trig, NULL);
	printf("audio quiet=3 lines/4 idx (slot 8 shared), trigger=5/5, pass-through ok\n");
}

/* save guard stand-ins: the "originals" must never run once the guard is on */
static volatile int g_orig_reached;
__attribute__((noinline, noclone)) uint64_t o_write(void *t, const char *n, const void *d, int sz) { (void)t, (void)n, (void)d, (void)sz; g_orig_reached++; return 1; }
__attribute__((noinline, noclone)) uint64_t o_size(void *t, const char *n) { (void)t, (void)n; g_orig_reached += 2; return 7; }
__attribute__((noinline, noclone)) uint64_t o_read(void *t, const char *n, void *b, int sz) { (void)t, (void)n, (void)b, (void)sz; g_orig_reached += 4; return 7; }
__attribute__((noinline, noclone)) uint64_t o_delete(void *t, const char *n) { (void)t, (void)n; g_orig_reached += 8; return 1; }
typedef uint64_t (*w_fn)(void *, const char *, const void *, int);
typedef uint64_t (*s_fn)(void *, const char *);
typedef uint64_t (*r_fn)(void *, const char *, void *, int);
static void *volatile g_gvt[4];
static char g_ghex[4][256];

static void guard_specs(struct site_spec *gs)
{
	static const char *const names[4] = { "G219", "G220", "G221", "G222" };
	void *fns[4] = { (void *)o_write, (void *)o_size, (void *)o_read, (void *)o_delete };
	for (int i = 0; i < 4; i++) {
		g_gvt[i] = fns[i];
		hex_of((const unsigned char *)&g_gvt[i], 8, g_ghex[i], 128);
		hex_of(fns[i], 4, g_ghex[i] + 128, 128);
		gs[i] = (struct site_spec){ names[i], K_VSLOT, (uintptr_t)&g_gvt[i], (uintptr_t)&g_gvt[i], g_ghex[i],
					    (uintptr_t)fns[i], g_ghex[i] + 128 };
	}
}

/* run guard_or_die in a child; return its exit status (-1 = it did not exit) */
static int child_status(const struct site_spec *gs, const char *dir)
{
	pid_t pid = fork();
	if (pid == 0) {
		setenv("SOMVR_SAVEDIR", dir, 1);
		guard_or_die(gs, 1);
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void guard_tests(const char *base)
{
	char dir[1024], path[1100], buf[16] = { 0 };
	snprintf(dir, sizeof dir, "%s/saves", base);
	mkdir(dir, 0755);
	struct site_spec gs[4], bad[4];
	guard_specs(gs);

	/* fail closed: a changed slot, a missing directory -> status 3, and the stand-ins stay untouched */
	memcpy(bad, gs, sizeof bad);
	bad[2].ctx_hex = "00 00 00 00 00 00 00 00";
	EXPECT(child_status(bad, dir) == 3, "guard with a mismatching slot did not exit 3");
	EXPECT(child_status(gs, "/nonexistent/somvr") == 3, "guard with a missing directory did not exit 3");
	EXPECT(g_gvt[0] == (void *)o_write, "the failed guard children changed the parent's table");

	EXPECT(guard_install(gs, dir) == 0, "guard_install failed (see the log)");
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 0, "size of an absent file is not 0");
	EXPECT(((w_fn)g_gvt[0])(NULL, "GameData.sav", "hello", 5) == 1, "write failed");
	snprintf(path, sizeof path, "%s/GameData.sav", dir);
	FILE *f = fopen(path, "r");
	EXPECT(f && fread(buf, 1, sizeof buf, f) == 5 && !memcmp(buf, "hello", 5), "written file content");
	if (f)
		fclose(f);
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 5, "size after the write");
	memset(buf, 0, sizeof buf);
	EXPECT(((r_fn)g_gvt[2])(NULL, "GameData.sav", buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5), "read back");
	/* the game's own name form (run 2): "\GameData.sav" is the same file */
	EXPECT(((s_fn)g_gvt[1])(NULL, "\\GameData.sav") == 5, "size of \\GameData.sav");
	memset(buf, 0, sizeof buf);
	EXPECT(((r_fn)g_gvt[2])(NULL, "\\GameData.sav", buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5),
	       "read back as \\GameData.sav");
	EXPECT(((w_fn)g_gvt[0])(NULL, "\\GameData.sav", "world!", 6) == 1, "write as \\GameData.sav");
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 6, "\\GameData.sav wrote another file");
	snprintf(path, sizeof path, "%s/\\GameData.sav", dir);
	EXPECT(access(path, F_OK) != 0, "a file named \\GameData.sav was created");
	EXPECT(((w_fn)g_gvt[0])(NULL, "\\", "x", 1) == 0, "an empty name after the backslash was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, "\\..\\escape.sav", "x", 1) == 0, "a backslash path was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, "a\\b.sav", "x", 1) == 0, "an inner backslash was written");
	EXPECT(((r_fn)g_gvt[2])(NULL, "Other.sav", buf, sizeof buf) == 0, "read of an absent file is not 0");
	EXPECT(((w_fn)g_gvt[0])(NULL, "../escape.sav", "x", 1) == 0, "a name with a path was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, NULL, "x", 1) == 0, "a null name was written");
	EXPECT(((s_fn)g_gvt[3])(NULL, "GameData.sav") == 1, "delete failed");
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 0, "size after the delete");
	EXPECT(g_orig_reached == 0, "an original storage function ran (%d)", g_orig_reached);
	printf("guard: fail-closed children exit 3, write/size/read/delete served from %s, originals never ran\n", dir);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: selftest DIR\n");
		return 2;
	}
	char *logp = g_logp, trig[1024];
	snprintf(logp, sizeof g_logp, "%s/somvr.log", argv[1]);
	snprintf(trig, sizeof trig, "%s/somvr.trigger", argv[1]);
	setenv("SOMVR_LOG", logp, 1);
	setenv("SOMVR_TRIGGER", trig, 1);
	setenv("SOMVR_MODE", "off30", 1);
	setenv("SOMVR_AT", "view", 1);
	setenv("SOMVR_COMFORT", "1", 1);
	for (int i = 0; i < PK_N; i++) /* the pokes, EXPO's globals: buffers here */
		g_pokes[i].addr = (uintptr_t)&g_pk[i];
	g_a_realtime = (uintptr_t)&g_real, g_a_expo = (uintptr_t)g_est, g_a_bound = (uintptr_t)&g_bound;

	struct site_spec sites[HK_N];
	g_vt[37] = (void *)t_h2;
	g_avt[1] = (void *)t_aud;
	if (call_site((void *)c_h1, (void *)t_h1, &sites[0], "H1", 0) || call_site((void *)c_h3, (void *)t_h3, &sites[2], "H3", 2) ||
	    call_site((void *)c_h4, (void *)t_h4, &sites[3], "H4", 3) ||
	    call_site((void *)c_expo, (void *)t_expo, &sites[HK_EXPO], "EXPO_HDR", HK_EXPO)) {
		printf("FAIL: no call site found in the stand-in callers\n");
		return 1;
	}
	hex_of((const unsigned char *)&g_vt[37], 8, g_hex[1], 128);
	hex_of((const unsigned char *)t_h2, 4, g_hex[1] + 128, 128);
	sites[1] = (struct site_spec){ "H2", K_VSLOT, (uintptr_t)&g_vt[37], (uintptr_t)&g_vt[37], g_hex[1],
				       (uintptr_t)t_h2, g_hex[1] + 128 };
	hex_of((const unsigned char *)&g_avt[1], 8, g_hex[HK_AUD], 128);
	hex_of((const unsigned char *)t_aud, 4, g_hex[HK_AUD] + 128, 128);
	sites[HK_AUD] = (struct site_spec){ "AUDIO_LISTENER", K_VSLOT, (uintptr_t)&g_avt[1], (uintptr_t)&g_avt[1],
					    g_hex[HK_AUD], (uintptr_t)t_aud, g_hex[HK_AUD] + 128 };
	if (somvr_start(sites, "selftest") != 0) {
		printf("FAIL: somvr_start\n");
		return 1;
	}
	for (int i = 0; i < HK_N; i++)
		EXPECT(g_h[i].installed, "%s not installed (see the log)", g_h[i].name);

	static const unsigned char pk_on[PK_N] = { 1, 0, 0, 0, 0 }, pk_blur[PK_N] = { 1, 0, 0, 1, 1 };
	round_("no-trigger", 0, 0);
	poke_check("first-h2", pk_on); /* every poke applied by the first H2 call */
	set_trigger(trig, "");
	round_("on/view", 30, 0);
	set_trigger(trig, "vp");
	round_("on/vp", 0, 30);
	set_trigger(trig, NULL);
	round_("off", 0, 0);
	set_trigger(trig, "blur"); /* the offset at SOMVR_AT (view), and the game's velocity-blur bytes back */
	round_("blur", 30, 0);
	poke_check("blur", pk_blur);
	set_trigger(trig, NULL);
	round_("blur-off", 0, 0);
	poke_check("blur-off", pk_on);
	g_pk[PK_VB55] = 1; /* the game writes its value again: poked back (and remembered for the next "blur") */
	round_("game-wrote", 0, 0);
	poke_check("game-wrote", pk_on);
	set_trigger(trig, "bogus"); /* an unknown word: the plain trigger */
	round_("bogus", 30, 0);
	poke_check("bogus", pk_on);
	set_trigger(trig, " view\n");
	round_("ws-view", 30, 0);
	set_trigger(trig, NULL);
	g_mode = M_SBS; /* the sbs rounds: H4 around the stand-in body; any H2 return address counts as the player view */
	g_ra_vp = 0;
	g_foliage = (uintptr_t)g_fol;
	sbs_round("off", trig, NULL, 1, 0, 0);
	sbs_round("on", trig, "", 2, 3.2f, 0);
	sbs_round("sbs0", trig, "sbs0", 2, 0, 0);
	sbs_round("sbsB", trig, "sbsB", 2, 3.2f, 1);
	sbs_round("sbsB0", trig, "sbsB0", 2, 0, 1);
	sbs_round("sbs-again", trig, "sbs", 2, 3.2f, 0);
	sbs_round("off-again", trig, NULL, 1, 0, 0);
	g_mode = M_OFF30; /* the trigger words select sbs at run time in another mode */
	sbs_round("word-sbs", trig, "sbs", 2, 3.2f, 0);
	sbs_round("word-sbsB", trig, "sbsB", 2, 3.2f, 1);
	sbs_round("word-sbsB0", trig, "sbsB0", 2, 0, 1);
	sbs_round("word-off", trig, NULL, 1, 0, 0);
	HAS(last_line("H4 "), "layout=B", "H4 line after sbsB0");
	expo_tests(trig);
	audio_tests(trig);
	guard_tests(argv[1]);
	flush_log();
	printf("%s (%d failures); log %s\n", fails ? "SELFTEST FAILED" : "selftest ok", fails, logp);
	return fails != 0;
}
