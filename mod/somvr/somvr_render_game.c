/* somvr_render_game.c - build 21 (docs/re-xr-pipeline.md 86): the flat screen's effects out of the eyes. Included by
 * somvr.c after the counter icon's file (it uses check_site, go_slot, bo_patch_call, logf_, env_word, cb_now, g_eye).
 *
 * SOMVR_FLATFX=game|all|LIST (default game = nothing patched; LIST = a comma list of the parts below) - effects that
 *   only work on a flat screen, each turned off in the eyes:
 *   flares    the lens flares: CLensFlareRender::vf_04 (slot 4 of 0x3b5de58, 0x2d6f170), the draw of every ClientFX
 *             LensFlare (the ghosts and rings the sun throws across the view; their occlusion queries are re-issued per
 *             eye, re-stereo.md 3) returns at once. The sun itself is the sky's and stays.
 *   shafts    the screen-space sun shafts: FUN_007a1f70 (the RenderScene executor's per-view bloom/shaft setup, the call
 *             0x7a2541 in FUN_007a24d0, render thread) copies the HDR state's RadialBlurEnabled into DAT_046257ac;
 *             FUN_007a1530 runs the four radialblur_* passes when it is set and only clears the shaft target when not.
 *             After the setup the byte is 0: no streaks from the sun's screen position (each eye's would point
 *             elsewhere); the bloom itself is untouched.
 *   vignette  the game's own vignette and box blur: the DoF shader's two other jobs in the final post FUN_007eb1d0 -
 *             sp+0x144 (ctx+0x1dc: k_fVignetteParams/Color) and sp+0x130 (ctx+0x1c8: k_fBoxBlurParams) - cleared after
 *             H1 (FUN_00a5a820 filled sp). A screen-fixed darkening or blur in each eye reads as a visor; our own comfort
 *             vignette (SOMVR_VIGNETTE) is the layer's and stays.
 *   rain      the drops on the lens: the call 0x9ef2f3 of FUN_00ca53e0(Player+0x438, rt) in FUN_009ee170 (step 10, after
 *             RenderScene) skipped while an eye renders (g_eye >= 0).
 *   Already off in every VR build (SOMVR_COMFORT=1's pokes): motion blur, depth of field, the camera shake. Not in
 *   this build at all (re-ui-effects.md 3.2): chromatic aberration, film grain, lens dirt, screen-space reflections.
 * SOMVR_FX_TEST=1 (headless tests only, never in a player's build): the draws of the other four ClientFX renderers get
 *   a pass-through too (CParticleSystemRender, CGPUParticleSystemRender, CSpriteRender, CFlareSpriteRender: slot 4 of
 *   each), so the words can tell which renderer draws what:
 *   `fx hide MASK` (bit 0 CPU particles, 1 GPU particles, 2 sprites, 3 flare sprites, 4 lens flares; 0 = all drawn),
 *   `fx eyesame 0|1` (1 = the right eye rendered from the left eye's pose and fov: anything that still differs between
 *   the eyes is state the game carries from one render call to the next, not the eyes' geometry), `fx console CMD`
 *   (a console string after the next stereo frame, e.g. GameServerShell.ChangeWeather SON_Day01 for the sun); the
 *   probe on CParticleSystemRender::vf_03 compares each CPU emitter's per-view particle copy between the two eyes
 *   (`FLATFX probe ..` lines: the same / DIFFERENT particles, what each eye's request got: a block, nothing, no room) and
 *   the naked pass-through on CParticleSystemFX::vf_04 counts the emitters' updates by when they start (outside the
 *   eyes' calls, in eye 0's, in eye 1's). Always (behind RD_VIS_INIT / RD_VIS_REQ): after both eyes, the frame's render
 *   request buffer (both eyes share it; 4 MB) - the most used and any bytes refused (`FLATFX t=.. REFUSED`).
 * Trigger words: "flatfx LIST|all|game|state" (live; the hooks must be in: boot with a part on or SOMVR_FX_TEST=1),
 * "fx hide MASK", "fx eyesame 0|1", "fx console CMD", "fx state". Lines: CONFIG flatfx=..; FLATFX t=.. (the word; the
 * game's own vignette or box blur seen: the first 8 of each); FLATFX state|END. Fail soft: a site mismatch = that part
 * the game's. */

/* RENDER_SITES_BEGIN (check_sites.py counts this block on its own: the patched K_VSLOT RD_LENSFLARE, the patched
 * K_CALLs RD_SHAFTS and RD_RAIN, SOMVR_FX_TEST's four patched K_VSLOTs RD_PSYS, RD_GPUPSYS, RD_SPRITE, RD_FLARESPR;
 * RD_PSYS_UPD (CParticleSystemFX::vf_04, the emitter's update: counted by when it runs) and RD_PSYS_PREP
 * (CParticleSystemRender::vf_03, the per-view copy of an emitter's particles: the probe); a slot's
 * context = slots 0..4 of its class's vtable, RD_PSYS_PREP's slots 0..3 - RD_PSYS patches slot 4 before it; the
 * read-only K_ENTRY proofs RD_VIS_INIT (FUN_007b7260: the request buffer's capacity = DAT_0462d734 << 10 at +0x18, its
 * two buffers) and RD_VIS_REQ (FUN_007b79f0: a request's room = +0x18 - +0x1c) before RD_VIS is read) */
static const struct site_spec g_rd_sites_game[] = {
SITE("RD_LENSFLARE", K_VSLOT, 0x3b5de78, 0x3b5de58, "30 0a d7 02 00 00 00 00 70 0a d7 02 00 00 00 00 20 0a d7 02 00 00 00 00 b0 0a d7 02 00 00 00 00 70 f1 d6 02 00 00 00 00", 0x2d6f170, "41 57 41 56 41 55 41 54 55 53 48 81 ec 98 00"),
SITE("RD_SHAFTS", K_CALL, 0x7a2541, 0x7a2530, "8b 03 f3 0f 10 43 08 48 89 04 24 f3 0f 11 44 24 08 e8 2a fa ff ff", 0x7a1f70, "53 66 0f ef c9 48 89 fb 48 83 ec 10 f3 0f 10"),
SITE("RD_RAIN", K_CALL, 0x9ef2f3, 0x9ef2e4, "e8 e7 5f 2b 00 84 c0 74 0b 4c 89 f6 4c 89 e7 e8 e8 60 2b 00", 0xca53e0, "48 8b 57 10 48 8b 47 18 48 29 d0 48 c1 f8 03"),
SITE("RD_PSYS", K_VSLOT, 0x3b63bc0, 0x3b63ba0, "30 51 d8 02 00 00 00 00 40 51 d8 02 00 00 00 00 80 c5 d8 02 00 00 00 00 b0 80 d8 02 00 00 00 00 d0 85 d8 02 00 00 00 00", 0x2d885d0, "41 57 41 56 41 55 41 54 55 53 48 81 ec a8 01"),
SITE("RD_GPUPSYS", K_VSLOT, 0x395afa8, 0x395af88, "f0 37 65 02 00 00 00 00 e0 3d 65 02 00 00 00 00 30 8d 65 02 00 00 00 00 c0 7a 65 02 00 00 00 00 10 3d 65 02 00 00 00 00", 0x2653d10, "48 83 7e 10 00 74 79 55 31 c0 48 89 f5 53 48"),
SITE("RD_SPRITE", K_VSLOT, 0x3b68eb8, 0x3b68e98, "10 3c da 02 00 00 00 00 20 3c da 02 00 00 00 00 60 aa d6 02 00 00 00 00 30 2c db 02 00 00 00 00 a0 33 da 02 00 00 00 00", 0x2da33a0, "4c 8b 4e 10 4d 85 c9 0f 84 83 02 00 00 53 31"),
SITE("RD_FLARESPR", K_VSLOT, 0x3b5bc40, 0x3b5bc20, "80 aa d6 02 00 00 00 00 90 aa d6 02 00 00 00 00 60 aa d6 02 00 00 00 00 30 2c db 02 00 00 00 00 10 a1 d6 02 00 00 00 00", 0x2d6a110, "4c 8b 56 10 4d 85 d2 0f 84 49 03 00 00 48 8b"),
SITE("RD_PSYS_UPD", K_VSLOT, 0x3b625c0, 0x3b625a0, "b0 c8 d7 02 00 00 00 00 80 c9 d7 02 00 00 00 00 30 48 d8 02 00 00 00 00 d0 c6 d7 02 00 00 00 00 b0 26 d8 02 00 00 00 00", 0x2d826b0, "41 57 41 56 41 55 41 54 55 53 48 89 fb 48 81"),
SITE("RD_PSYS_PREP", K_VSLOT, 0x3b63bb8, 0x3b63ba0, "30 51 d8 02 00 00 00 00 40 51 d8 02 00 00 00 00 80 c5 d8 02 00 00 00 00 b0 80 d8 02 00 00 00 00", 0x2d880b0, "48 8b 4e 08 48 8b 3e 4c 8b 51 18 4c 8b 0f f3"),
SITE("RD_VIS_INIT", K_ENTRY, 0x7b7260, 0x7b7260, "53 48 89 fb 8b 3d ca 64 e7 03 c1 e7 0a 89 7b 18 e8 5b 9a ae 02 8b 7b 18", 0x7b7260, "53 48 89 fb 8b 3d ca 64 e7 03 c1 e7 0a 89 7b"),
SITE("RD_VIS_REQ", K_ENTRY, 0x7b79f0, 0x7b79f0, "55 89 d5 53 48 89 fb 48 83 ec 58 8b 47 1c 8b 4b 10 8b 7f 18 29 c7 83 f9", 0x7b79f0, "55 89 d5 53 48 89 fb 48 83 ec 58 8b 47 1c 8b"),
};
/* RENDER_SITES_END */
static const struct site_spec *g_rd_sites = g_rd_sites_game; /* selftest: stand-ins */

enum { RDS_LENSFLARE, RDS_SHAFTS, RDS_RAIN, RDS_PSYS, RDS_GPUPSYS, RDS_SPRITE, RDS_FLARESPR, RDS_PSYS_UPD, RDS_PSYS_PREP,
       RDS_VIS_INIT, RDS_VIS_REQ, RDS_N };
_Static_assert(sizeof g_rd_sites_game / sizeof g_rd_sites_game[0] == RDS_N, "render sites");
/* the SOMVR_FLATFX parts (bits) */
enum { RDF_FLARES = 1u << 0, RDF_SHAFTS = 1u << 1, RDF_VIGNETTE = 1u << 2, RDF_RAIN = 1u << 3, RDF_ALL = 15u };
static const char *const g_rd_part_w[] = { "flares", "shafts", "vignette", "rain" };
/* the draws with a pass-through (index into g_rd.orig; bit i of `fx hide`): CPU particles, GPU particles, sprites, flare
 * sprites, lens flares */
enum { RDD_PSYS, RDD_GPUPSYS, RDD_SPRITE, RDD_FLARESPR, RDD_LENSFLARE, RDD_N };
static const char *const g_rd_draw_w[RDD_N] = { "CPU particles", "GPU particles", "sprites", "flare sprites",
						"lens flares" };
#define RD_SHAFT_BYTE 0x46257acUL /* DAT_046257ac: this view's RadialBlurEnabled (FUN_007a1f70 writes it, FUN_007a1530 reads) */
/* the renderer's per-frame visibility request buffer (FUN_007b79f0 / FUN_007b72a0; the RenderScene custom nodes' per-view
 * blocks, CParticleSystemRender::vf_03 among them): two buffers +0/+8 (this frame's index +0x10, BeginFrame
 * FUN_007b7ad0 toggles it and zeroes +0x1c/+0x20), the capacity +0x18 (FUN_007b7260 at the renderer's start:
 * DAT_0462d734 KB << 10 = 4 MB, set by _INIT_95), the bytes used this frame +0x1c (all views together), refused +0x20 */
#define RD_VIS 0x462d700UL
#define RD_SP_BOXBLUR 0x130       /* sp: the box blur's enable byte (-> ctx+0x1c8) */
#define RD_SP_VIGNETTE 0x144      /* sp: the game's vignette's enable byte (-> ctx+0x1dc) */

typedef void (*rd_draw_fn)(void *self, void *ctx);
typedef void (*rd_shaft_fn)(void *params);
typedef uint64_t (*rd_rain_fn)(void *drops, void *rt);
typedef void (*rd_prep_fn)(void *self, void **args);
#define RD_PROBE_N 1024              /* emitters remembered per frame (hashed by address, no probing: a clash = the game's) */
/* RD_PSYS_UPD's counters by when the update ran (somvr_rd_phase: 0 = outside the eyes' calls, 1 = from eye 0's H1 to eye
 * 1's, 2 = from eye 1's H1 to the frame's end); a naked pass-through (the update's arguments untouched) */
volatile int somvr_rd_phase;
uint64_t somvr_rd_upd[3];
uintptr_t somvr_rd_upd_orig;
__attribute__((naked, used)) void somvr_w_rdupd(void)
{
	__asm__ volatile("movslq somvr_rd_phase(%rip), %r11\n\t"
			 "leaq somvr_rd_upd(%rip), %r10\n\t"
			 "lock incq (%r10,%r11,8)\n\t"
			 "jmp *somvr_rd_upd_orig(%rip)\n\t");
}

static struct {
	unsigned want;              /* SOMVR_FLATFX at start (RDF_*) */
	_Atomic unsigned off;       /* now (the word changes it) */
	int test;                   /* SOMVR_FX_TEST=1 */
	int ok[RDS_N];              /* each site installed */
	uintptr_t orig[RDD_N];      /* the draws' originals */
	_Atomic unsigned hide;      /* `fx hide MASK` (bits = RDD_*) */
	_Atomic int eyesame;        /* `fx eyesame 1` */
	_Atomic uint64_t n_draw[RDD_N], n_skip[RDD_N], n_shaft, n_shaft_off, n_rain, n_rain_off, n_vig, n_box;
	_Atomic uint32_t vig_lines;
	int vis_ok;                 /* RD_VIS_INIT and RD_VIS_REQ matched: RD_VIS may be read (the probe) */
	uintptr_t orig_prep;        /* RD_PSYS_PREP's original */
	struct { uintptr_t fx; uint32_t frame, n; uint64_t h; long tid; } probe[RD_PROBE_N]; /* eye 0's copies (game thread) */

	_Atomic uint64_t p_calls[3], p_same, p_diff, p_nomatch; /* prep calls flat / eye 0 / eye 1; eye 1 vs eye 0 */
	_Atomic uint32_t p_lines, p_over_lines;
	_Atomic uint64_t p_wrote[2], p_culled[2], p_over[2]; /* each eye's prep calls: a block, nothing, no room */
	uint32_t p_cap, p_used_max[2];
	char cmd[128];              /* `fx console CMD` (SOMVR_FX_TEST): run by rd_frame on the game thread */
	uint64_t vis_frames, vis_full;  /* stereo frames seen by rd_frame; with bytes refused */
	uint32_t vis_used_max, vis_over_max, vis_cap;
	_Atomic int cmd_pend;
} g_rd;

static inline unsigned rd_hidden(void)
{
	unsigned m = atomic_load_explicit(&g_rd.hide, memory_order_relaxed);
	if (atomic_load_explicit(&g_rd.off, memory_order_relaxed) & RDF_FLARES)
		m |= 1u << RDD_LENSFLARE;
	return m;
}

#define RD_DRAW_STUB(N)                                                                                                \
	__attribute__((used)) void somvr_w_rddraw##N(void *self, void *ctx)                                            \
	{                                                                                                              \
		atomic_fetch_add_explicit(&g_rd.n_draw[N], 1, memory_order_relaxed);                                   \
		if (rd_hidden() & (1u << N)) {                                                                         \
			atomic_fetch_add_explicit(&g_rd.n_skip[N], 1, memory_order_relaxed);                           \
			return;                                                                                        \
		}                                                                                                      \
		((rd_draw_fn)g_rd.orig[N])(self, ctx);                                                                 \
	}
RD_DRAW_STUB(0)
RD_DRAW_STUB(1)
RD_DRAW_STUB(2)
RD_DRAW_STUB(3)
RD_DRAW_STUB(4)
static void (*const g_rd_stub[RDD_N])(void *, void *) = { somvr_w_rddraw0, somvr_w_rddraw1, somvr_w_rddraw2,
							   somvr_w_rddraw3, somvr_w_rddraw4 };

/* RD_PSYS_PREP (SOMVR_FX_TEST): the per-view copy of a CPU emitter's particles. The probe: eye 0's copy of each emitter
 * hashed (the count and every particle's first 0x30 bytes, read where vf_03 reads them: the block list +0xc0, the stride
 * +0xbc, the count +0xb8), eye 1's of the same frame compared - a difference = the particles changed between the two
 * eyes' render calls */
static uint64_t rd_psys_hash(uintptr_t fx, uint32_t *n_out)
{
	uint32_t n = *(volatile uint32_t *)(fx + 0xb8), stride = *(volatile uint32_t *)(fx + 0xbc);
	uintptr_t blk = *(volatile uintptr_t *)(fx + 0xc0);
	uint64_t h = 1469598103934665603ull ^ n;
	*n_out = n;
	if (!blk || !n || stride < 0x30 || stride > 0x200 || n > 0x4000)
		return h;
	/* every particle, walked as vf_03 copies them: backwards from the block's end (+0x10), on to the next block (+0)
	 * when below its header (+0x18) */
	uintptr_t p = *(volatile uintptr_t *)(blk + 0x10) - stride;
	for (uint32_t k = 0; k < n && p >= blk + 0x18; k++) {
		for (int i = 0; i < 0x30; i += 8)
			h = (h ^ *(volatile uint64_t *)(p + i)) * 1099511628211ull;
		p -= stride;
		if (p < blk + 0x18) {
			blk = *(volatile uintptr_t *)blk;
			if (!blk)
				break;
			p = *(volatile uintptr_t *)(blk + 0x10) - stride;
		}
	}
	return h;
}
/* the command writer vf_03 fills (args[2]): its base, the used and capacity offsets, the bytes refused for want of room
 * (+0x10: a block that does not fit is added there and the view gets nothing for that emitter) */
struct rd_cmdbuf { unsigned char *base; uint32_t used, cap; int32_t over; };
__attribute__((used)) void somvr_w_rdprep(void *self, void **a)
{
	int e = g_eye;
	struct rd_cmdbuf *cb = a ? (struct rd_cmdbuf *)a[2] : NULL;
	uint32_t u0 = cb ? cb->used : 0;
	int32_t o0 = cb ? cb->over : 0;
	atomic_fetch_add_explicit(&g_rd.p_calls[e == 0 ? 1 : e == 1 ? 2 : 0], 1, memory_order_relaxed);
	uintptr_t fx = a ? (uintptr_t)a[1] : 0;
	if (fx && (e == 0 || e == 1)) {
		uint32_t n, fr = g_fr.frame;
		uint64_t h = rd_psys_hash(fx, &n);
		long tid = syscall(SYS_gettid);
		unsigned k = (unsigned)((fx >> 4) * 2654435761u) % RD_PROBE_N;
		if (e == 0)
			g_rd.probe[k].fx = fx, g_rd.probe[k].frame = fr, g_rd.probe[k].n = n, g_rd.probe[k].h = h,
			g_rd.probe[k].tid = tid;
		else if (g_rd.probe[k].fx != fx || g_rd.probe[k].frame != fr)
			atomic_fetch_add_explicit(&g_rd.p_nomatch, 1, memory_order_relaxed);
		else {
			int same = g_rd.probe[k].h == h && g_rd.probe[k].n == n;
			atomic_fetch_add_explicit(same ? &g_rd.p_same : &g_rd.p_diff, 1, memory_order_relaxed);
			if (n >= 4 && atomic_fetch_add(&g_rd.p_lines, 1) < 16)
				logf_("FLATFX probe t=%.3f frame %u emitter %#lx: eye 0 copied %u particles (thread %ld), eye 1 %u "
				      "(thread %ld): %s; updates so far outside the eyes %llu, in eye 0's call %llu, in eye 1's %llu",
				      cb_now(), fr, (unsigned long)fx, g_rd.probe[k].n, g_rd.probe[k].tid, n, tid,
				      same ? "the same particles" : "DIFFERENT particles (changed between the eyes' calls)",
				      (unsigned long long)somvr_rd_upd[0], (unsigned long long)somvr_rd_upd[1],
				      (unsigned long long)somvr_rd_upd[2]);
		}
	}
	((rd_prep_fn)g_rd.orig_prep)(self, a);
	if (cb && (e == 0 || e == 1)) { /* what this view got for the emitter: its block, nothing (culled), or no room */
		if (cb->over != o0) {
			atomic_fetch_add_explicit(&g_rd.p_over[e], 1, memory_order_relaxed);
			if (atomic_fetch_add(&g_rd.p_over_lines, 1) < 12)
				logf_("FLATFX probe t=%.3f frame %u eye %d emitter %#lx: NO ROOM for its %d bytes (the writer %u of %u "
				      "used, %d refused so far this view): this eye draws nothing of it", cb_now(), (unsigned)g_fr.frame, e,
				      (unsigned long)fx, cb->over - o0, cb->used, cb->cap, cb->over);
		} else if (cb->used > u0)
			atomic_fetch_add_explicit(&g_rd.p_wrote[e], 1, memory_order_relaxed);
		else
			atomic_fetch_add_explicit(&g_rd.p_culled[e], 1, memory_order_relaxed);
		if (cb->cap > g_rd.p_cap)
			g_rd.p_cap = cb->cap;
		if (cb->used > g_rd.p_used_max[e])
			g_rd.p_used_max[e] = cb->used;
	}
}

/* after both eyes of a stereo frame (H4, game thread): `fx console CMD` */
static void rd_frame(void)
{
	somvr_rd_phase = 0;
	if (!g_rd.vis_ok)
		goto cmd;
	/* the frame's request buffer after both eyes: how full, anything refused (a node, an emitter that got no room in a
	 * view is not drawn in that eye) */
	uint32_t used = *(volatile uint32_t *)(RD_VIS + 0x1c), over = *(volatile uint32_t *)(RD_VIS + 0x20);
	g_rd.vis_cap = *(volatile uint32_t *)(RD_VIS + 0x18);
	g_rd.vis_frames++;
	if (used > g_rd.vis_used_max)
		g_rd.vis_used_max = used;
	if (over) {
		if (!g_rd.vis_full++ || over > g_rd.vis_over_max)
			logf_("FLATFX t=%.3f the frame's render requests: %u of %u bytes used, %u REFUSED (no room: what came last in "
			      "the right eye is not drawn there)%s", cb_now(), used, g_rd.vis_cap, over,
			      g_rd.vis_full == 1 ? " - the first such frame" : " - the most so far");
		if (over > g_rd.vis_over_max)
			g_rd.vis_over_max = over;
	}
cmd:
	if (!atomic_load_explicit(&g_rd.cmd_pend, memory_order_acquire))
		return;
	char why[256] = "";
	int rc = es_run(g_rd.cmd, why, sizeof why);
	logf_("FLATFX t=%.3f console '%s'%s%s", cb_now(), g_rd.cmd, rc ? ": not run: " : ": run", rc ? why : "");
	atomic_store(&g_rd.cmd_pend, 0);
}

/* RD_SHAFTS: the per-view bloom/shaft setup (render thread) */
__attribute__((used)) void somvr_w_rdshaft(void *params)
{
	((rd_shaft_fn)g_rd_sites[RDS_SHAFTS].target)(params);
	atomic_fetch_add_explicit(&g_rd.n_shaft, 1, memory_order_relaxed);
	volatile uint8_t *b = (volatile uint8_t *)RD_SHAFT_BYTE;
	if ((atomic_load_explicit(&g_rd.off, memory_order_relaxed) & RDF_SHAFTS) && *b) {
		*b = 0;
		atomic_fetch_add_explicit(&g_rd.n_shaft_off, 1, memory_order_relaxed);
	}
}

/* RD_RAIN: the lens drops' draw after an eye's RenderScene (game thread) */
__attribute__((used)) uint64_t somvr_w_rdrain(void *drops, void *rt)
{
	atomic_fetch_add_explicit(&g_rd.n_rain, 1, memory_order_relaxed);
	if ((atomic_load_explicit(&g_rd.off, memory_order_relaxed) & RDF_RAIN) && g_eye >= 0) {
		atomic_fetch_add_explicit(&g_rd.n_rain_off, 1, memory_order_relaxed);
		return 0;
	}
	return ((rd_rain_fn)g_rd_sites[RDS_RAIN].target)(drops, rt);
}

/* from H1's post (game thread, every call): the game's vignette and box blur out of the scene params */
static void rd_h1(void *sp)
{
	somvr_rd_phase = g_eye == 0 ? 1 : g_eye == 1 ? 2 : 0; /* RD_PSYS_UPD's counters */
	if (!sp || !(atomic_load_explicit(&g_rd.off, memory_order_relaxed) & RDF_VIGNETTE))
		return;
	volatile uint8_t *b = (volatile uint8_t *)sp;
	int v = b[RD_SP_VIGNETTE], x = b[RD_SP_BOXBLUR];
	if (!v && !x)
		return;
	if (atomic_fetch_add(&g_rd.vig_lines, 1) < 8) {
		const float *vp = (const float *)((const char *)sp + RD_SP_VIGNETTE + 4);
		const float *bp = (const float *)((const char *)sp + RD_SP_BOXBLUR + 4);
		logf_("FLATFX t=%.3f the game's %s%s%s this frame (vignette %.3g %.3g %.3g, box blur %.3g %.3g %.3g): off in "
		      "the eyes (SOMVR_FLATFX vignette)", cb_now(), v ? "vignette" : "", v && x ? " and " : "",
		      x ? "box blur" : "", vp[0], vp[1], vp[2], bp[0], bp[1], bp[2]);
	}
	if (v)
		b[RD_SP_VIGNETTE] = 0, atomic_fetch_add_explicit(&g_rd.n_vig, 1, memory_order_relaxed);
	if (x)
		b[RD_SP_BOXBLUR] = 0, atomic_fetch_add_explicit(&g_rd.n_box, 1, memory_order_relaxed);
}

/* H1: which eye's pose and fov eye e renders with (`fx eyesame 1`: the right eye from the left's) */
static int rd_eye_src(int e)
{
	return e == 1 && atomic_load_explicit(&g_rd.eyesame, memory_order_relaxed) ? 0 : e;
}

static void rd_parts(unsigned m, char *o, size_t n)
{
	o[0] = 0;
	if (!m) {
		snprintf(o, n, "game");
		return;
	}
	for (int i = 0; i < 4; i++)
		if (m & (1u << i))
			snprintf(o + strlen(o), n - strlen(o), "%s%s", o[0] ? "," : "", g_rd_part_w[i]);
}

/* "flatfx ..." / SOMVR_FLATFX: RDF_* bits, or -1 */
static int rd_parse(const char *s)
{
	if (!strcmp(s, "game") || !strcmp(s, "0") || !*s)
		return 0;
	if (!strcmp(s, "all") || !strcmp(s, "off"))
		return RDF_ALL;
	unsigned m = 0;
	char buf[96];
	snprintf(buf, sizeof buf, "%s", s);
	for (char *sv = NULL, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) {
		int i = 0;
		while (i < 4 && strcmp(t, g_rd_part_w[i]))
			i++;
		if (i == 4)
			return -1;
		m |= 1u << i;
	}
	return (int)m;
}

static void rd_state_line(const char *head)
{
	char p[64];
	rd_parts(atomic_load(&g_rd.off), p, sizeof p);
	logf_("%s t=%.3f: SOMVR_FLATFX %s (lens flares %s, shafts %s, rain %s); draws (skipped): CPU particles %llu (%llu), "
	      "GPU particles %llu (%llu), sprites %llu (%llu), flare sprites %llu (%llu), lens flares %llu (%llu); shaft setups "
	      "%llu (shafts cleared %llu), rain draws %llu (skipped %llu), the game's vignette cleared %llu, box blur %llu; "
	      "fx hide %#x eyesame %d; the probe: CPU emitters copied flat %llu / eye 0 %llu / eye 1 %llu, eye 1 the same "
	      "particles as eye 0 %llu, different %llu, unmatched %llu; emitter updates outside the eyes %llu, in eye 0's call "
	      "%llu, in eye 1's %llu; per eye (0 / 1): blocks %llu / %llu, culled %llu / %llu, NO ROOM %llu / %llu, the writer's "
	      "capacity %u, the most used %u / %u; the frame's request buffer (all views): %u bytes, the most used %u, frames with "
	      "requests refused %llu of %llu (the most refused %u)", head, cb_now(), p, g_rd.ok[RDS_LENSFLARE] ? "hooked" : "not hooked",
	      g_rd.ok[RDS_SHAFTS] ? "hooked" : "not hooked", g_rd.ok[RDS_RAIN] ? "hooked" : "not hooked",
	      (unsigned long long)g_rd.n_draw[0], (unsigned long long)g_rd.n_skip[0], (unsigned long long)g_rd.n_draw[1],
	      (unsigned long long)g_rd.n_skip[1], (unsigned long long)g_rd.n_draw[2], (unsigned long long)g_rd.n_skip[2],
	      (unsigned long long)g_rd.n_draw[3], (unsigned long long)g_rd.n_skip[3], (unsigned long long)g_rd.n_draw[4],
	      (unsigned long long)g_rd.n_skip[4], (unsigned long long)g_rd.n_shaft, (unsigned long long)g_rd.n_shaft_off,
	      (unsigned long long)g_rd.n_rain, (unsigned long long)g_rd.n_rain_off, (unsigned long long)g_rd.n_vig,
	      (unsigned long long)g_rd.n_box, atomic_load(&g_rd.hide), atomic_load(&g_rd.eyesame),
	      (unsigned long long)g_rd.p_calls[0], (unsigned long long)g_rd.p_calls[1], (unsigned long long)g_rd.p_calls[2],
	      (unsigned long long)g_rd.p_same, (unsigned long long)g_rd.p_diff, (unsigned long long)g_rd.p_nomatch,
	      (unsigned long long)somvr_rd_upd[0], (unsigned long long)somvr_rd_upd[1], (unsigned long long)somvr_rd_upd[2],
	      (unsigned long long)g_rd.p_wrote[0], (unsigned long long)g_rd.p_wrote[1], (unsigned long long)g_rd.p_culled[0],
	      (unsigned long long)g_rd.p_culled[1], (unsigned long long)g_rd.p_over[0], (unsigned long long)g_rd.p_over[1],
	      g_rd.p_cap, g_rd.p_used_max[0], g_rd.p_used_max[1], g_rd.vis_cap, g_rd.vis_used_max,
	      (unsigned long long)g_rd.vis_full, (unsigned long long)g_rd.vis_frames, g_rd.vis_over_max);
}

/* "flatfx LIST|all|game|state", "fx hide MASK|eyesame 0|1|state" (writer thread) */
static const char *rd_word(const char *w0)
{
	char w[96];
	snprintf(w, sizeof w, "%s", w0);
	for (size_t n = strlen(w); n && (w[n - 1] == '\n' || w[n - 1] == ' ' || w[n - 1] == '\r');)
		w[--n] = 0;
	if (!strcmp(w, "flatfx") || !strcmp(w, "flatfx state") || !strcmp(w, "fx") || !strcmp(w, "fx state")) {
		rd_state_line("FLATFX state");
		return "flatfx (a state line)";
	}
	if (!strncmp(w, "flatfx ", 7)) {
		int m = rd_parse(w + 7);
		if (m < 0)
			return "flatfx (not game|all|a list of flares,shafts,vignette,rain: ignored)";
		atomic_store(&g_rd.off, (unsigned)m);
		char p[64];
		rd_parts((unsigned)m, p, sizeof p);
		logf_("FLATFX t=%.3f the word: %s off in the eyes from the next frame%s", cb_now(), p,
		      (m & RDF_FLARES) && !g_rd.ok[RDS_LENSFLARE] ? " (the lens flares' slot is NOT hooked)" : "");
		return "flatfx (from the next frame)";
	}
	if (!strncmp(w, "fx ", 3) && !g_rd.test)
		return "fx (needs SOMVR_FX_TEST=1)";
	unsigned v;
	if (sscanf(w, "fx hide %i", (int *)&v) == 1) {
		atomic_store(&g_rd.hide, v & ((1u << RDD_N) - 1));
		logf_("FLATFX t=%.3f the word: fx hide %#x (%s%s%s%s%s drawn no more)", cb_now(), v, v & 1 ? "CPU particles " : "",
		      v & 2 ? "GPU particles " : "", v & 4 ? "sprites " : "", v & 8 ? "flare sprites " : "",
		      v & 16 ? "lens flares " : "");
		return "fx hide";
	}
	if (sscanf(w, "fx eyesame %u", &v) == 1) {
		atomic_store(&g_rd.eyesame, v != 0);
		logf_("FLATFX t=%.3f the word: fx eyesame %d (%s)", cb_now(), v != 0,
		      v ? "the right eye renders from the left eye's pose and fov" : "each eye its own pose");
		return "fx eyesame";
	}
	if (!strncmp(w, "fx console ", 11) && w[11]) {
		if (atomic_load(&g_rd.cmd_pend))
			return "fx console (one is pending: ignored)";
		snprintf(g_rd.cmd, sizeof g_rd.cmd, "%s", w + 11);
		atomic_store_explicit(&g_rd.cmd_pend, 1, memory_order_release);
		return "fx console (after the next stereo frame)";
	}
	return "fx (not hide MASK|eyesame 0|1|console CMD|state: ignored)";
}

static void rd_start(const struct site_spec *sites, int *bad)
{
	memset(&g_rd, 0, sizeof g_rd);
	g_rd_sites = sites;
	const char *s = getenv("SOMVR_FLATFX");
	int m = s ? rd_parse(s) : 0;
	if (m < 0) {
		logf_("WARN SOMVR_FLATFX=%s is not game|all|a comma list of flares,shafts,vignette,rain: game", s);
		++*bad;
		m = 0;
	}
	g_rd.want = (unsigned)m;
	g_rd.test = env_word("SOMVR_FX_TEST", (const char *const[]){ "0", "1" }, 2, 0);
	char vwhy[512];
	g_rd.vis_ok = 1;
	for (int i = RDS_VIS_INIT; i <= RDS_VIS_REQ; i++)
		if (check_site(&sites[i], vwhy, sizeof vwhy) != 0) {
			g_rd.vis_ok = 0;
			logf_("FLATFX site %s %#lx MISMATCH: %s (no request-buffer probe)", sites[i].name,
			      (unsigned long)sites[i].addr, vwhy);
		}
	if (!g_rd.want && !g_rd.test)
		return;
	char why[512];
	for (int i = 0; i < RDS_VIS_INIT; i++) {
		if (i >= RDS_PSYS && !g_rd.test)
			break;
		if (check_site(&sites[i], why, sizeof why) != 0) {
			logf_("FLATFX site %s %#lx -> %#lx MISMATCH: %s", sites[i].name, (unsigned long)sites[i].addr,
			      (unsigned long)sites[i].target, why);
			continue;
		}
		if (i == RDS_SHAFTS)
			g_rd.ok[i] = bo_patch_call(&sites[i], (void *)somvr_w_rdshaft);
		else if (i == RDS_RAIN)
			g_rd.ok[i] = bo_patch_call(&sites[i], (void *)somvr_w_rdrain);
		else if (i == RDS_PSYS_PREP)
			g_rd.ok[i] = go_slot(&sites[i], (void *)somvr_w_rdprep, &g_rd.orig_prep);
		else if (i == RDS_PSYS_UPD)
			g_rd.ok[i] = go_slot(&sites[i], (void *)somvr_w_rdupd, &somvr_rd_upd_orig);
		else {
			int d = i == RDS_LENSFLARE ? RDD_LENSFLARE : i - RDS_PSYS;
			g_rd.ok[i] = go_slot(&sites[i], (void *)g_rd_stub[d], &g_rd.orig[d]);
		}
	}
	unsigned have = (g_rd.ok[RDS_LENSFLARE] ? RDF_FLARES : 0) | (g_rd.ok[RDS_SHAFTS] ? RDF_SHAFTS : 0) | RDF_VIGNETTE |
			(g_rd.ok[RDS_RAIN] ? RDF_RAIN : 0);
	atomic_store(&g_rd.off, g_rd.want & have);
	char p[64], q[64];
	rd_parts(g_rd.want, p, sizeof p);
	rd_parts(g_rd.want & ~have, q, sizeof q);
	logf_("CONFIG flatfx=%s (SOMVR_FLATFX: the flat screen's effects off in the eyes - lens flares' draw, the sun shafts' "
	      "passes, the game's vignette and box blur, the lens drops; motion blur, DoF and the shake were already off)%s%s "
	      "test=%d (SOMVR_FX_TEST: the particle and sprite draws' pass-through %s)", p, g_rd.want & ~have ? "; NOT hooked: " : "",
	      g_rd.want & ~have ? q : "", g_rd.test,
	      !g_rd.test ? "off" : g_rd.ok[RDS_PSYS] && g_rd.ok[RDS_GPUPSYS] && g_rd.ok[RDS_SPRITE] && g_rd.ok[RDS_FLARESPR]
		      ? "hooked" : "NOT all hooked");
}

static void rd_end_log(void)
{
	if (g_rd.want || g_rd.test || g_rd.vis_full)
		rd_state_line("FLATFX END");
}
