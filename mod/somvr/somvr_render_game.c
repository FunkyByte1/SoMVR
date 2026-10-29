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
 *   overlay   build 24 (re-xr-pipeline.md 100): ClientFX's full-screen overlays (COverlayFX: the blood splashes and the
 *             red at low health) - its two draw entries, vf_18 (slot 18 of 0x3b62468, 0x2d7af10) and the
 *             IClientFXOverlay thunk vf_0xb0_03 (slot 3 of 0x3b62518, 0x2d7b040), both FUN_02d7a700 (an overlay view
 *             with the eye's camera: H2 ra 0x2d7a743) and return 1 - return 1 at once while H4's eye calls run. A
 *             screen-fixed sprite in each eye sits at the eye's infinity, a smear in the face; Talion's hurt stays
 *             SOMVR_HURT's red flash on the view's edges.
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
 * Build 22 (re-xr-pipeline.md 94): SOMVR_CURFRAME=game|eye (default game; word "curframe game|eye|state") - eye 1's
 *   pass with its own "current frame" images (rt3D +0xc0/+0x110 swapped with two of ours from eye 0's end to eye 1's
 *   end; see the block below). SOMVR_FX_TEST adds `fx trace N` (FXTRACE lines: the render thread's BeginRenderTarget
 *   bindings, EndRenderTargets, CPU-particle draws), `fx hideeye E` (`fx hide` in one eye's pass), `fx sync 0|1` (the
 *   game thread waits after both eyes for eye 1's end), `fx vbgrow N` (the custom draws' dynamic vertex buffers made
 *   N times bigger), `fx bloomblend 0|game`, and the probes: each CPU emitter's block from its copy to its draw, eye 1's
 *   draw against eye 0's (the vertices through a proxy on the render context), the vertex memory at the presents.
 * Build 23 (re-xr-pipeline.md 96): SOMVR_KTIME=frame|game (default frame; word "ktime frame|game|state") - eye 1's pass
 *   gets eye 0's shaders' clock k_fTime back (RenderScene's late pass left k_fRealTime in it: the fire's flame and the
 *   wraith sky moved differently in each eye). SOMVR_FX_TEST adds the D3D11 capture `fx d3d N` (eye 1's draws against
 *   eye 0's: shaders, views, constant bytes; DCAP lines) and `fx d3dsame MASK` (eye 1's draws bind eye 0's state).
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
 * two buffers) and RD_VIS_REQ (FUN_007b79f0: a request's room = +0x18 - +0x1c) before RD_VIS is read; build 22
 * (re-xr-pipeline.md 94): SOMVR_FX_TEST's K_CALLs RD_BRT_A / RD_BRT_B (the two BeginRenderTargets' calls of
 * FUN_0074b1e0(rt), which binds the target's last / current frame images: the trace) and the K_ENTRY RD_CF_MAKE
 * (FUN_00747e60, the maker of a target's "current frame" image: SOMVR_CURFRAME=eye makes eye 1's with it); build 23
 * (re-xr-pipeline.md 96): SOMVR_KTIME's K_ENTRY RD_KT_UPLOAD (FUN_00750080, a shader constant's upload: called) and the
 * read-only K_CALL RD_KT_LATE (RenderScene's call 0x7d7030, its context the k_fTime := k_fRealTime write after it); build
 * 24 (re-xr-pipeline.md 100): `overlay`'s patched K_VSLOTs RD_OVERLAY (COverlayFX::vf_18; its context slots 14..18) and
 * RD_OVERLAY_IF (its IClientFXOverlay vtable's slot 3; the context slots 0..3)) */
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
SITE("RD_BRT_A", K_CALL, 0x74beb6, 0x74beac, "44 89 bb 48 04 00 00 48 89 df e8 25 f3 ff ff", 0x74b1e0, "53 be 00 2e 62 04 48 89 fb 48 81 c7 90 01 00"),
SITE("RD_BRT_B", K_CALL, 0x74b646, 0x74b638, "48 04 00 00 48 89 df 83 05 36 46 ed 03 01 e8 95 fb ff ff", 0x74b1e0, "53 be 00 2e 62 04 48 89 fb 48 81 c7 90 01 00"),
SITE("RD_CF_MAKE", K_ENTRY, 0x747e60, 0x747e60, "48 b8 01 00 00 00 01 00 00 00 41 57 41 56 45 89 c6 41 55 41 89 d5 41 54", 0x747e60, "48 b8 01 00 00 00 01 00 00 00 41 57 41 56 45"),
SITE("RD_KT_UPLOAD", K_ENTRY, 0x750080, 0x750080, "55 0f b7 ff 53 48 8d 3c 7f 48 83 ec 18 48 8b 05", 0x750080, "55 0f b7 ff 53 48 8d 3c 7f 48 83 ec 18 48 8b"),
SITE("RD_KT_LATE", K_CALL, 0x7d7030, 0x7d7030, "e8 0b cb f6 ff 80 7c 24 26 00 0f 84 07 01 00 00 b8 80 54 62 04 48 3d 20 54 62 04 74 2d f3 0f 10 05 0b e4 e4 03 0f b7 3d 2c e4 e4 03 f3 0f 11 05 5c e4 e4 03", 0x743b40, "c3 66 2e 0f 1f 84 00 00 00 00 00 0f 1f 44 00"),
SITE("RD_OVERLAY", K_VSLOT, 0x3b624f8, 0x3b624d8, "60 37 65 02 00 00 00 00 70 37 65 02 00 00 00 00 80 37 65 02 00 00 00 00 d0 a6 d7 02 00 00 00 00 10 af d7 02 00 00 00 00", 0x2d7af10, "48 83 bf c0 00 00 00 00 74 26 80 bf e8 00 00"),
SITE("RD_OVERLAY_IF", K_VSLOT, 0x3b62530, 0x3b62518, "50 b1 d7 02 00 00 00 00 80 b1 d7 02 00 00 00 00 10 b0 d7 02 00 00 00 00 40 b0 d7 02 00 00 00 00", 0x2d7b040, "48 83 7f 10 00 74 29 80 7f 38 00 74 23 48 83"),
};
/* RENDER_SITES_END */
static const struct site_spec *g_rd_sites = g_rd_sites_game; /* selftest: stand-ins */

enum { RDS_LENSFLARE, RDS_SHAFTS, RDS_RAIN, RDS_PSYS, RDS_GPUPSYS, RDS_SPRITE, RDS_FLARESPR, RDS_PSYS_UPD, RDS_PSYS_PREP,
       RDS_VIS_INIT, RDS_VIS_REQ, RDS_BRT_A, RDS_BRT_B, RDS_CF_MAKE, RDS_KT_UPLOAD, RDS_KT_LATE, RDS_OVERLAY,
       RDS_OVERLAY_IF, RDS_N };
_Static_assert(sizeof g_rd_sites_game / sizeof g_rd_sites_game[0] == RDS_N, "render sites");
/* the SOMVR_FLATFX parts (bits) */
enum { RDF_FLARES = 1u << 0, RDF_SHAFTS = 1u << 1, RDF_VIGNETTE = 1u << 2, RDF_RAIN = 1u << 3, RDF_OVERLAY = 1u << 4,
       RDF_ALL = 31u };
#define RD_NPARTS 5
static const char *const g_rd_part_w[RD_NPARTS] = { "flares", "shafts", "vignette", "rain", "overlay" };
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
/* build 24, `overlay`: COverlayFX's draw entries [0] vf_18, [1] the IClientFXOverlay thunk (game thread, inside an eye's
 * body call). somvr_rd_ov_skip (rd_ov_eyes: H4 around its eye calls, the part on) = return 1 (drawn, as the game's do)
 * at once; else the original. Naked: the draw's float arguments in xmm0/xmm1 (FUN_02d7a700 hands them to the renderer's
 * view setup) and every other register reach the original untouched. Counters: calls, skipped. */
volatile int somvr_rd_ov_skip;
uint64_t somvr_rd_ov_n[2], somvr_rd_ov_skipped[2];
uintptr_t somvr_rd_ov_orig[2];
__attribute__((naked, used)) void somvr_w_rdov18(void)
{
	__asm__ volatile("lock incq somvr_rd_ov_n(%rip)\n\t"
			 "cmpl $0, somvr_rd_ov_skip(%rip)\n\t"
			 "je 1f\n\t"
			 "lock incq somvr_rd_ov_skipped(%rip)\n\t"
			 "movl $1, %eax\n\t"
			 "ret\n"
			 "1:\n\t"
			 "jmp *somvr_rd_ov_orig(%rip)\n\t");
}
__attribute__((naked, used)) void somvr_w_rdovif(void)
{
	__asm__ volatile("lock incq somvr_rd_ov_n+8(%rip)\n\t"
			 "cmpl $0, somvr_rd_ov_skip(%rip)\n\t"
			 "je 1f\n\t"
			 "lock incq somvr_rd_ov_skipped+8(%rip)\n\t"
			 "movl $1, %eax\n\t"
			 "ret\n"
			 "1:\n\t"
			 "jmp *somvr_rd_ov_orig+8(%rip)\n\t");
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


/* ---- build 22 (re-xr-pipeline.md 94): each eye its own "current frame" images - SOMVR_CURFRAME=game|eye ----------
 * A RenderTarget with flag 0x10 (rt3D has it, flags 0x37d7) carries two images of its own last pixels, made by
 * FUN_00747e60 (RD_CF_MAKE) into +0xc0 and +0x110 (0x50 bytes each: +4 w, +8 h, +0xc/+0x10 the texture's and the
 * view's formats, +0x14, +0x18 the texture, +0x20 its render target view, +0x28 its shader view, +0x30 the bindable:
 * +0x38 the shader view again, +0x48 the bytes). FUN_0074b1e0, at every BeginRenderTarget, binds them as tCurFrameMap
 * (+0xf0, or +0x140 in mode 1), tForcedCurFrameMap and tCurFrameMapDiscard (the other one); EndRenderTarget(rt, 1)
 * copies the target's pixels into the first (FUN_0074bff0). A material that reads "the scene behind it" samples them.
 * Both eyes render into the one rt3D, so whatever one eye's pass leaves there the other eye's pass reads.
 * eye: from eye 0's end to eye 1's end (render thread, H8's eye ends) the target's two image objects are swapped, all
 * 0x50 bytes, with two of ours made by the game's own maker with the target's sizes and formats (at the first eye 0
 * end, again when they change; the game's pixels copied in once): whatever the game copies into them or binds from
 * them during eye 1's pass is eye 1's own, and eye 0 keeps the game's. A present with the swap still in (an eye 1
 * end missed) swaps back. Fail soft: RD_CF_MAKE not matched, the target without the images, a maker that fails or
 * views that are not the expected objects = the game's (logged once). */
#define RD_CF_OFF 0xc0UL  /* RenderTarget: the first "current frame" image object (the second at +0x110) */
#define RD_CF_SZ 0x50UL
#define RD_RT_MODE 0x448  /* RenderTarget: the mode its BeginRenderTarget set (FUN_0074b1e0 reads it; the end sets 0xd) */
#define RD_RTF_CF 0x10u   /* RenderTarget flag: it has the two images */
/* the bound values (each binding param + 0x40): tCurFrameMap, tForcedCurFrameMap, tCurFrameMapDiscard, tLastFrameMap */
static const uintptr_t g_rd_bindv[4] = { 0x4622de0UL, 0x4622d80UL, 0x4622d20UL, 0x4622e40UL };
typedef int (*rd_cfmake_fn)(void *obj, int w, int h, int fmt, int vfmt, int p6, int noreg);
#define RD_BLK_N 2048 /* SOMVR_FX_TEST: CPU emitters' per-view blocks remembered from the copy to the draw */
static struct {
	int want;                    /* SOMVR_CURFRAME at start: 0 game, 1 eye */
	_Atomic int on;              /* now (the word) */
	int ok;                      /* RD_CF_MAKE matched */
	int state;                   /* render thread: 0 not made, 1 made, -1 off for the run */
	_Alignas(16) unsigned char x[2][RD_CF_SZ]; /* eye 1's two images (in the target during eye 1's pass) */
	uintptr_t rt;                /* the target they were made for */
	int swapped;                 /* render thread: ours are in the target */
	int phase;                   /* render thread: 0 up to eye 0's end, 1 eye 1's pass, 2 after eye 1's end */
	uint64_t frame;              /* render thread: the frame of the eye that ended last */
	uint64_t swaps, late, makes, nocf;
	_Atomic uint64_t e1_frame;   /* eye 1's last end (`fx sync`) */
	/* SOMVR_FX_TEST */
	_Atomic int hide_eye;        /* `fx hideeye E`: `fx hide` only in eye E's pass (-1 both) */
	_Atomic int sync;            /* `fx sync 1`: the game thread waits after both eyes for eye 1's end on the render thread */
	uint64_t sync_ok, sync_late;
	double sync_wait;
	_Atomic int tr_req;          /* `fx trace N`: frames still to trace */
	int tracing, tr_frames;      /* render thread */
	_Atomic uint32_t tr_lines;
	uint64_t ends[3][2], begins[3][2], ends1[3][2]; /* per phase, [other target, rt3D]: ends, begins, ends with flags 1 */
	struct { _Atomic uintptr_t key; uint32_t len, frame; int eye; uint64_t h; uintptr_t fx; } blk[RD_BLK_N];
	/* the draw's own record per emitter (render thread): what eye 0 drew it with, compared at eye 1's draw */
	struct { uintptr_t fx; uint64_t frame; uint64_t cam, em, vh; uintptr_t props, empp; uint32_t nv, req, granted; } dr[RD_BLK_N];
	_Atomic uint64_t d_cmp, d_cam, d_em, d_vh, d_nv, d_nomatch, d_props;
	_Atomic uint32_t d_lines;
	_Atomic uint64_t b_same[2], b_diff[2], b_norec[2], b_newer[2], b_draws[3];
	_Atomic uint32_t b_lines;
} g_rcf;

static void rcf_state_line(const char *head);
static uint64_t rd_fnv(const unsigned char *p, uint32_t n)
{
	uint64_t h = 1469598103934665603ull ^ n;
	for (uint32_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

/* a bound "frame" image by name: 0, CF0 / CF1 (rt3D's, "x" while ours are swapped in), another target's */
static const char *rd_cfname(uintptr_t v, char *b, size_t n)
{
	uintptr_t rt = g_rcf.rt ? g_rcf.rt : (uintptr_t)g_rt_target;
	if (!v)
		snprintf(b, n, "0");
	else if (rt && v == rt + RD_CF_OFF + 0x30)
		snprintf(b, n, "CF0%s", g_rcf.swapped ? "x" : "");
	else if (rt && v == rt + RD_CF_OFF + RD_CF_SZ + 0x30)
		snprintf(b, n, "CF1%s", g_rcf.swapped ? "x" : "");
	else if (rt && v == rt + 0x190)
		snprintf(b, n, "LAST");
	else
		snprintf(b, n, "%#lx", (unsigned long)v);
	return b;
}

/* ---- build 23 (re-xr-pipeline.md 96): the shaders' clock the same in both eyes - SOMVR_KTIME=frame|game ------------
 * k_fTime (0x46254c0, re-stereo.md 1), the clock of every time-animated material, is set once per frame by BeginFrame
 * (FUN_0072a850: the game's time) - and by RenderScene (FUN_007d6e20, 0x7d704d..0x7d705c, proven by RD_KT_LATE) to
 * k_fRealTime (0x4625460) before its late pass for category-4 nodes (step 11), never set back: on a flat screen the
 * next frame's BeginFrame does it. In stereo eye 1's whole pass ran on k_fRealTime and eye 0's on the game's time (the
 * D3D11 capture, run 20261029-060526: the CPU particles' constant c19.y 91.241 / 152.496, the next frame 91.341 /
 * 158.848): the fire's flame and every other time-animated material moved differently in each eye.
 * frame (default): at eye 1's view setup (H3, render thread) k_fTime gets back the value it had at eye 0's (the game's
 * time of this frame) and is uploaded the game's way (FUN_00750080(its handle 0x4625488, its data pointer 0x4625490,
 * 4) = RD_KT_UPLOAD; no handle yet: the dirty word 0x462548a = 4, as the game does). game: as the game does. Fail soft:
 * a site mismatch = the game's. Word "ktime frame|game|state"; lines CONFIG ktime=.., KTIME (the first 4 fixes, state). */
#define RD_KT 0x46254c0UL        /* k_fTime's data (float) */
#define RD_KT_HANDLE 0x4625488UL /* its binding handle (uint16; 0xffff = none) */
#define RD_KT_PTR 0x4625490UL    /* the data pointer FUN_00750080 is given */
#define RD_KT_DIRTY 0x462548aUL  /* uint16: 4 = set without a handle, 0 = uploaded */
typedef void (*rd_ktup_fn)(uint32_t handle, void *data, uint32_t size);
static struct {
	int want, ok;                 /* SOMVR_KTIME at start (1 frame); both sites matched */
	_Atomic int on;               /* now (the word) */
	float t, last0, last1;        /* eye 0's k_fTime this frame; the last fix's two values */
	uint64_t frame, fixed, same, nohandle, lines;
	int have;
} g_kt;
/* H3 (render thread): the eye's view setup, before its pass */
static void rd_kt_eye(int eye, uint64_t frame)
{
	if (!g_kt.ok)
		return;
	volatile float *kt = (volatile float *)RD_KT;
	if (eye == 0) {
		g_kt.t = *kt, g_kt.frame = frame, g_kt.have = 1;
		return;
	}
	if (eye != 1 || !g_kt.have || g_kt.frame != frame || !atomic_load_explicit(&g_kt.on, memory_order_relaxed))
		return;
	float now = *kt;
	if (now == g_kt.t) {
		g_kt.same++;
		return;
	}
	g_kt.last0 = g_kt.t, g_kt.last1 = now;
	*kt = g_kt.t;
	uint16_t h = *(volatile uint16_t *)RD_KT_HANDLE;
	if (h == 0xffff) {
		*(volatile uint16_t *)RD_KT_DIRTY = 4;
		g_kt.nohandle++;
	} else {
		((rd_ktup_fn)g_rd_sites[RDS_KT_UPLOAD].target)(h, *(void *volatile *)RD_KT_PTR, 4);
		*(volatile uint16_t *)RD_KT_DIRTY = 0;
	}
	if (g_kt.fixed++ < 4)
		logf_("KTIME t=%.3f frame %llu: eye 1's pass found k_fTime %.3f (eye 0's %.3f; RenderScene's late pass left "
		      "k_fRealTime): set back to %.3f%s", mono(), (unsigned long long)frame, now, g_kt.t, g_kt.t,
		      h == 0xffff ? " (no handle yet: marked dirty)" : " and uploaded");
}
static void rd_kt_state(const char *head)
{
	logf_("%s t=%.3f: SOMVR_KTIME %s (start %s; sites %s): eye 1's k_fTime set back %llu times (the last: %.3f -> %.3f), "
	      "already the same %llu, without a handle %llu", head, cb_now(), atomic_load(&g_kt.on) ? "frame" : "game",
	      g_kt.want ? "frame" : "game", g_kt.ok ? "matched" : "NOT matched", (unsigned long long)g_kt.fixed, g_kt.last1,
	      g_kt.last0, (unsigned long long)g_kt.same, (unsigned long long)g_kt.nohandle);
}

/* ---- build 23 (re-xr-pipeline.md 96): SOMVR_FX_TEST's D3D11 capture, `fx d3d N` -------------------------------------
 * The engine's render thread draws through IndirectX's threaded context (IdxD3D11DeviceContextThreaded, vtable
 * 0x45537f0; its slots in ID3D11DeviceContext's order - CopyResource 47 and RSSetViewports 44 are proven, Map 14 and
 * GetType/GetDesc of a buffer (slots 7/10) read from its code). With SOMVR_FX_TEST twenty of its slots get a
 * pass-through: the shaders, shader views, constant buffers, pixel samplers, the first render target, the depth view
 * and the blend state bound are kept (render thread), and while a capture runs the bytes of every buffer bound as
 * constants (Map ... Unmap, UpdateSubresource). `fx d3d N` captures N stereo frames after one frame of warm-up: each
 * eye's pass counted from its first BeginRenderTarget of rt3D, eye 1's draw k compared with eye 0's draw k; what
 * differs is logged (DCAP lines: the slots, the constant registers with both eyes' values). With `fx eyesame 1` both
 * eyes render one view: whatever still differs is state that changes from one pass to the next. */
/* D3DCAP_SITES_BEGIN (check_sites.py counts this block on its own: SOMVR_FX_TEST's 20 patched K_VSLOTs of
 * IdxD3D11DeviceContextThreaded's vtable 0x45537f0: VS/PS constant buffers, PS shader views, PS/VS shaders, PS samplers,
 * DrawIndexed, Draw, Map, Unmap, PS constant buffers, DrawIndexedInstanced, DrawInstanced, VS shader views,
 * OMSetRenderTargets, OMSetBlendState, Dispatch, UpdateSubresource, CS shader views, CS shader, CS constant buffers) */
static const struct site_spec g_dc_sites_game[] = {
SITE("DC_VSCB", K_VSLOT, 0x4553828, 0x4553828, "50 2a ac 01 00 00 00 00", 0x1ac2a50, "41 57 49 89 cf 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_PSSRV", K_VSLOT, 0x4553830, 0x4553830, "c0 37 ac 01 00 00 00 00", 0x1ac37c0, "41 57 41 89 f7 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_PSSH", K_VSLOT, 0x4553838, 0x4553838, "90 10 ac 01 00 00 00 00", 0x1ac1090, "41 55 4c 8d af c8 00 00 00 41 54 49 89 fc 4c"),
SITE("DC_PSSMP", K_VSLOT, 0x4553840, 0x4553840, "b0 38 ac 01 00 00 00 00", 0x1ac38b0, "41 57 41 89 f7 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_VSSH", K_VSLOT, 0x4553848, 0x4553848, "10 0e ac 01 00 00 00 00", 0x1ac0e10, "41 55 4c 8d af c8 00 00 00 41 54 49 89 fc 4c"),
SITE("DC_DRAWIDX", K_VSLOT, 0x4553850, 0x4553850, "60 5d ac 01 00 00 00 00", 0x1ac5d60, "41 57 41 89 f7 41 56 41 89 d6 41 55 41 89 cd"),
SITE("DC_DRAW", K_VSLOT, 0x4553858, 0x4553858, "40 58 ac 01 00 00 00 00", 0x1ac5840, "41 57 41 56 41 89 f6 41 55 41 89 d5 41 54 4c"),
SITE("DC_MAP", K_VSLOT, 0x4553860, 0x4553860, "a0 88 ac 01 00 00 00 00", 0x1ac88a0, "41 57 4c 8d bf c8 00 00 00 41 56 45 89 c6 41"),
SITE("DC_UNMAP", K_VSLOT, 0x4553868, 0x4553868, "40 82 ac 01 00 00 00 00", 0x1ac8240, "41 57 41 56 41 89 d6 41 55 45 31 ed 41 54 4c"),
SITE("DC_PSCB", K_VSLOT, 0x4553870, 0x4553870, "90 36 ac 01 00 00 00 00", 0x1ac3690, "41 57 49 89 cf 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_DRAWIDXI", K_VSLOT, 0x4553890, 0x4553890, "90 60 ac 01 00 00 00 00", 0x1ac6090, "41 57 41 89 f7 41 56 45 89 c6 41 55 45 89 cd"),
SITE("DC_DRAWI", K_VSLOT, 0x4553898, 0x4553898, "f0 5e ac 01 00 00 00 00", 0x1ac5ef0, "41 57 41 56 41 55 4c 8d af c8 00 00 00 41 54"),
SITE("DC_VSSRV", K_VSLOT, 0x45538b8, 0x45538b8, "80 2b ac 01 00 00 00 00", 0x1ac2b80, "41 57 41 89 f7 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_OMRT", K_VSLOT, 0x45538f8, 0x45538f8, "80 44 ac 01 00 00 00 00", 0x1ac4480, "55 48 89 e5 41 57 49 89 ff 41 56 49 89 ce 41"),
SITE("DC_BLEND", K_VSLOT, 0x4553908, 0x4553908, "70 12 ac 01 00 00 00 00", 0x1ac1270, "41 57 41 89 cf 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_DISPATCH", K_VSLOT, 0x4553938, 0x4553938, "d0 5b ac 01 00 00 00 00", 0x1ac5bd0, "41 57 41 89 f7 41 56 41 89 d6 41 55 41 89 cd"),
SITE("DC_UPDSUB", K_VSLOT, 0x4553970, 0x4553970, "40 7e ac 01 00 00 00 00", 0x1ac7e40, "41 57 48 89 f8 45 89 cf 41 56 48 05 c8 00 00"),
SITE("DC_CSSRV", K_VSLOT, 0x4553a08, 0x4553a08, "d0 3a ac 01 00 00 00 00", 0x1ac3ad0, "41 57 41 89 f7 41 56 4c 8d b7 c8 00 00 00 41"),
SITE("DC_CSSH", K_VSLOT, 0x4553a18, 0x4553a18, "30 11 ac 01 00 00 00 00", 0x1ac1130, "41 55 4c 8d af c8 00 00 00 41 54 49 89 fc 4c"),
SITE("DC_CSCB", K_VSLOT, 0x4553a28, 0x4553a28, "a0 39 ac 01 00 00 00 00", 0x1ac39a0, "41 57 49 89 cf 41 56 4c 8d b7 c8 00 00 00 41"),
};
/* D3DCAP_SITES_END */
static const struct site_spec *g_dc_sites = g_dc_sites_game; /* selftest: stand-ins */
enum { DCS_VSCB, DCS_PSSRV, DCS_PSSH, DCS_PSSMP, DCS_VSSH, DCS_DRAWIDX, DCS_DRAW, DCS_MAP, DCS_UNMAP, DCS_PSCB, DCS_DRAWIDXI,
       DCS_DRAWI, DCS_VSSRV, DCS_OMRT, DCS_BLEND, DCS_DISPATCH, DCS_UPDSUB, DCS_CSSRV, DCS_CSSH, DCS_CSCB, DCS_N };
_Static_assert(sizeof g_dc_sites_game / sizeof g_dc_sites_game[0] == DCS_N, "capture sites");
#define DC_CB 8               /* constant buffer slots kept per stage */
#define DC_SRV 16             /* shader view slots kept per stage */
#define DC_SMP 8              /* pixel sampler slots kept */
#define DC_REC_N 12288        /* eye 0's draws kept per frame */
#define DC_BUF_N 8192         /* constant buffers' last bytes (open addressing by the buffer) */
#define DC_POOL (32u << 20)   /* the bytes (two pools: the live entries move to the other one at each present) */
enum { DC_VS, DC_PS, DC_CS };
static const char *const g_dc_stage[3] = { "VS", "PS", "CS" };
static const char *const g_dc_kind[6] = { "?", "DrawIndexed", "Draw", "DrawIndexedInstanced", "DrawInstanced", "Dispatch" };
struct dc_cb { void *buf; uint64_t h; uint32_t off, size; }; /* h 0 = its bytes unknown; off ~0u = not kept */
struct dc_rec {
	uint8_t kind, src;
	uint32_t n;
	void *sh[3], *srv[3][DC_SRV], *smp[DC_SMP], *rtv0, *dsv, *blend;
	struct dc_cb cb[3][DC_CB];
};
static __thread int dc_rt;    /* this thread is the render thread (set at H9) */
static __thread int dc_src;   /* inside a ClientFX renderer's draw (RD_DRAW_STUB): its RDD_* + 1 */
static struct {
	int ok;                       /* the hooks are in */
	uintptr_t orig[DCS_N];
	void *sh[3], *cb[3][DC_CB], *srv[3][DC_SRV], *smp[DC_SMP], *rtv0, *dsv, *blend; /* bound now (render thread) */
	_Atomic int req;              /* `fx d3d N` */
	_Atomic int replay;           /* `fx d3dsame MASK` */
	uint64_t r_draws[2], r_srv, r_cb; /* replayed: other / CPU-particle draws; views and constant buffers rebound */
	int state, left, pass;        /* 0 off, 1 warm-up, 2 capturing; frames left; the eye pass (-1 none) */
	uint32_t k[2];                /* draws in each eye's pass this frame */
	struct dc_rec *rec;           /* eye 0's draws */
	unsigned char *pool[2];
	int cur;
	uint32_t used;
	struct dc_cb buf[DC_BUF_N];
	struct { void *res; unsigned char *p; uint32_t size; } map[64];
	int nmap;
	uint32_t f_cmp, f_mis, f_srv, f_cb, f_cbunk, f_smp, f_rt, f_bl, f_px, f_pxd, f_lines, f_pxlines, f_mislines;
	uint32_t f_cbslot[3][DC_CB], f_srvslot[3][DC_SRV];
	uint64_t frames, maps, upds, full;
	uint32_t lines;
} g_dc;

static unsigned dc_bslot(const void *b)
{
	return (unsigned)(((uintptr_t)b >> 4) * 2654435761u) & (DC_BUF_N - 1);
}
static struct dc_cb *dc_buf_find(void *b, int add)
{
	unsigned i = dc_bslot(b);
	for (unsigned j = 0; j < DC_BUF_N; j++, i = (i + 1) & (DC_BUF_N - 1)) {
		if (g_dc.buf[i].buf == b)
			return &g_dc.buf[i];
		if (!g_dc.buf[i].buf)
			return add ? (g_dc.buf[i].buf = b, &g_dc.buf[i]) : NULL;
	}
	return NULL;
}
static uint32_t dc_keep_bytes(const unsigned char *p, uint32_t size)
{
	if (!g_dc.pool[g_dc.cur] || g_dc.used + size > DC_POOL) {
		g_dc.full++;
		return ~0u;
	}
	uint32_t off = g_dc.used;
	memcpy(g_dc.pool[g_dc.cur] + off, p, size);
	g_dc.used += (size + 15u) & ~15u;
	return off;
}
static void dc_buf_set(void *b, const unsigned char *p, uint32_t size)
{
	struct dc_cb *e = dc_buf_find(b, 1);
	if (!e)
		return;
	e->h = rd_fnv(p, size) | 1, e->size = size, e->off = dc_keep_bytes(p, size);
}
/* the present: the live entries' bytes into the other pool, the pool they were in emptied */
static void dc_pool_turn(void)
{
	int o = g_dc.cur;
	g_dc.cur = !o, g_dc.used = 0;
	for (unsigned i = 0; i < DC_BUF_N; i++)
		if (g_dc.buf[i].buf && g_dc.buf[i].off != ~0u)
			g_dc.buf[i].off = dc_keep_bytes(g_dc.pool[o] + g_dc.buf[i].off, g_dc.buf[i].size);
}
typedef void (*dc_gettype_fn)(void *res, int *dim);
typedef void (*dc_bufdesc_fn)(void *res, uint32_t *desc);
/* a buffer bound as constants (GetType 1 = a buffer, GetDesc's BindFlags has D3D11_BIND_CONSTANT_BUFFER): its size */
static uint32_t dc_cb_size(void *res)
{
	int dim = 0;
	((dc_gettype_fn)(*(void ***)res)[7])(res, &dim);
	if (dim != 1)
		return 0;
	uint32_t d[6] = { 0 };
	((dc_bufdesc_fn)(*(void ***)res)[10])(res, d);
	return (d[2] & 4u) && d[0] && d[0] <= 65536 ? d[0] : 0;
}

typedef void (*dc_set_fn)(void *ctx, uint32_t start, uint32_t n, void *const *v);
typedef void (*dc_sh_fn)(void *ctx, void *sh, void *const *ci, uint32_t nci);
#define DC_SET(NAME, S, ARR, CAP)                                                                                     \
	__attribute__((used)) void somvr_dc_##NAME(void *ctx, uint32_t start, uint32_t n, void *const *v)             \
	{                                                                                                              \
		if (dc_rt)                                                                                             \
			for (uint32_t i = 0; i < n && start + i < (CAP); i++)                                          \
				(ARR)[start + i] = v ? v[i] : NULL;                                                     \
		((dc_set_fn)g_dc.orig[S])(ctx, start, n, v);                                                           \
	}
DC_SET(vscb, DCS_VSCB, g_dc.cb[DC_VS], DC_CB)
DC_SET(pscb, DCS_PSCB, g_dc.cb[DC_PS], DC_CB)
DC_SET(cscb, DCS_CSCB, g_dc.cb[DC_CS], DC_CB)
DC_SET(vssrv, DCS_VSSRV, g_dc.srv[DC_VS], DC_SRV)
DC_SET(pssrv, DCS_PSSRV, g_dc.srv[DC_PS], DC_SRV)
DC_SET(cssrv, DCS_CSSRV, g_dc.srv[DC_CS], DC_SRV)
DC_SET(pssmp, DCS_PSSMP, g_dc.smp, DC_SMP)
#define DC_SH(NAME, S, ST)                                                                                            \
	__attribute__((used)) void somvr_dc_##NAME(void *ctx, void *sh, void *const *ci, uint32_t nci)                \
	{                                                                                                              \
		if (dc_rt)                                                                                             \
			g_dc.sh[ST] = sh;                                                                              \
		((dc_sh_fn)g_dc.orig[S])(ctx, sh, ci, nci);                                                            \
	}
DC_SH(vssh, DCS_VSSH, DC_VS)
DC_SH(pssh, DCS_PSSH, DC_PS)
DC_SH(cssh, DCS_CSSH, DC_CS)
typedef void (*dc_omrt_fn)(void *ctx, uint32_t n, void *const *rtv, void *dsv);
__attribute__((used)) void somvr_dc_omrt(void *ctx, uint32_t n, void *const *rtv, void *dsv)
{
	if (dc_rt)
		g_dc.rtv0 = n && rtv ? rtv[0] : NULL, g_dc.dsv = dsv;
	((dc_omrt_fn)g_dc.orig[DCS_OMRT])(ctx, n, rtv, dsv);
}
typedef void (*dc_blend_fn)(void *ctx, void *bs, const float *f, uint32_t mask);
__attribute__((used)) void somvr_dc_blend(void *ctx, void *bs, const float *f, uint32_t mask)
{
	if (dc_rt)
		g_dc.blend = bs;
	((dc_blend_fn)g_dc.orig[DCS_BLEND])(ctx, bs, f, mask);
}
typedef int32_t (*dc_map_fn)(void *ctx, void *res, uint32_t sub, uint32_t type, uint32_t flags, void *out);
__attribute__((used)) int32_t somvr_dc_map(void *ctx, void *res, uint32_t sub, uint32_t type, uint32_t flags, void *out)
{
	int32_t hr = ((dc_map_fn)g_dc.orig[DCS_MAP])(ctx, res, sub, type, flags, out);
	/* WRITE, WRITE_DISCARD, WRITE_NO_OVERWRITE of a constant buffer: its bytes read at the Unmap */
	if (dc_rt && g_dc.state && hr >= 0 && out && res && !sub && (type == 2 || type == 4 || type == 5) && g_dc.nmap < 64) {
		uint32_t sz = dc_cb_size(res);
		if (sz && *(unsigned char **)out)
			g_dc.map[g_dc.nmap].res = res, g_dc.map[g_dc.nmap].p = *(unsigned char **)out, g_dc.map[g_dc.nmap++].size = sz;
	}
	return hr;
}
typedef void (*dc_unmap_fn)(void *ctx, void *res, uint32_t sub);
__attribute__((used)) void somvr_dc_unmap(void *ctx, void *res, uint32_t sub)
{
	if (dc_rt)
		for (int i = g_dc.nmap - 1; i >= 0; i--)
			if (g_dc.map[i].res == res) {
				dc_buf_set(res, g_dc.map[i].p, g_dc.map[i].size), g_dc.maps++;
				g_dc.map[i] = g_dc.map[--g_dc.nmap];
				break;
			}
	((dc_unmap_fn)g_dc.orig[DCS_UNMAP])(ctx, res, sub);
}
typedef void (*dc_upd_fn)(void *ctx, void *dst, uint32_t sub, const void *box, const void *src, uint32_t rp, uint32_t dp);
__attribute__((used)) void somvr_dc_updsub(void *ctx, void *dst, uint32_t sub, const void *box, const void *src, uint32_t rp,
					   uint32_t dp)
{
	if (dc_rt && g_dc.state && dst && src && !box && !sub) {
		uint32_t sz = dc_cb_size(dst);
		if (sz)
			dc_buf_set(dst, src, sz), g_dc.upds++;
	}
	((dc_upd_fn)g_dc.orig[DCS_UPDSUB])(ctx, dst, sub, box, src, rp, dp);
}

static void dc_fill(struct dc_rec *r, int kind, uint32_t n)
{
	memset(r, 0, sizeof *r);
	r->kind = (uint8_t)kind, r->src = (uint8_t)dc_src, r->n = n;
	r->rtv0 = g_dc.rtv0, r->dsv = g_dc.dsv;
	for (int s = 0; s < 3; s++) {
		if ((kind == 5) != (s == DC_CS)) /* a draw: the VS and PS state; a dispatch: the CS state */
			continue;
		r->sh[s] = g_dc.sh[s];
		memcpy(r->srv[s], g_dc.srv[s], sizeof r->srv[s]);
		for (int j = 0; j < DC_CB; j++) {
			struct dc_cb *e = g_dc.cb[s][j] ? dc_buf_find(g_dc.cb[s][j], 0) : NULL;
			r->cb[s][j].buf = g_dc.cb[s][j];
			if (e)
				r->cb[s][j].h = e->h, r->cb[s][j].off = e->off, r->cb[s][j].size = e->size;
		}
	}
	if (kind != 5)
		memcpy(r->smp, g_dc.smp, sizeof r->smp), r->blend = g_dc.blend;
}
/* eye 1's draw k against eye 0's: what differs, one line (the constant registers with both eyes' values) */
static void dc_cmp(uint32_t k, const struct dc_rec *a, const struct dc_rec *b)
{
	g_dc.f_cmp++;
	if (b->src == RDD_PSYS + 1)
		g_dc.f_px++;
	if (a->kind != b->kind || a->n != b->n || memcmp(a->sh, b->sh, sizeof a->sh)) {
		g_dc.f_mis++;
		if (g_dc.f_mislines++ < 3 && g_dc.lines++ < 400)
			logf_("DCAP t=%.3f frame %llu draw %u: eye 1's is not eye 0's (%s/%s, count %u/%u, VS %p/%p, PS %p/%p, CS %p/%p)",
			      mono(), (unsigned long long)g_rt_frame, k, g_dc_kind[a->kind % 6], g_dc_kind[b->kind % 6], a->n, b->n,
			      a->sh[0], b->sh[0], a->sh[1], b->sh[1], a->sh[2], b->sh[2]);
		return;
	}
	char o[900];
	size_t n = 0;
	int what = 0;
#define DC_P(...) (n += (size_t)snprintf(o + n, n < sizeof o ? sizeof o - n : 0, __VA_ARGS__), n = n < sizeof o ? n : sizeof o - 1)
	int loud = 0; /* worth a line: not only a view left bound from before (one side NULL) or the bones (VS b1) */
	for (int s = 0; s < 3; s++)
		for (int j = 0; j < DC_SRV; j++)
			if (a->srv[s][j] != b->srv[s][j]) {
				what |= 1, g_dc.f_srvslot[s][j]++;
				loud |= a->srv[s][j] && b->srv[s][j];
				DC_P(" %s t%d %p/%p;", g_dc_stage[s], j, a->srv[s][j], b->srv[s][j]);
			}
	for (int s = 0; s < 3; s++)
		for (int j = 0; j < DC_CB; j++) {
			const struct dc_cb *x = &a->cb[s][j], *y = &b->cb[s][j];
			if (x->buf != y->buf) {
				what |= 2, g_dc.f_cbslot[s][j]++, loud = 1;
				DC_P(" %s b%d buffer %p/%p;", g_dc_stage[s], j, x->buf, y->buf);
				continue;
			}
			if (!x->buf)
				continue;
			if (!x->h || !y->h) {
				g_dc.f_cbunk++;
				continue;
			}
			if (x->h == y->h)
				continue;
			what |= 2, g_dc.f_cbslot[s][j]++;
			loud |= !(s == DC_VS && j == 1);
			DC_P(" %s b%d (%u bytes) differs", g_dc_stage[s], j, x->size);
			if (x->off == ~0u || y->off == ~0u || x->size != y->size) {
				DC_P(" (bytes not kept);");
				continue;
			}
			/* both in the current pool (kept this frame, or moved there at the present) */
			const unsigned char *p = g_dc.pool[g_dc.cur] + x->off, *q = g_dc.pool[g_dc.cur] + y->off;
			int nd = 0;
			for (uint32_t r = 0; r + 16 <= x->size; r += 16)
				if (memcmp(p + r, q + r, 16)) {
					if (nd++ < 4) {
						const float *f = (const float *)(p + r), *g = (const float *)(q + r);
						DC_P(" c%u (%g %g %g %g | %g %g %g %g)", r / 16, f[0], f[1], f[2], f[3], g[0], g[1], g[2], g[3]);
					}
				}
			DC_P(": %d registers;", nd);
		}
	if (memcmp(a->smp, b->smp, sizeof a->smp))
		what |= 4, DC_P(" samplers;");
	if (a->rtv0 != b->rtv0 || a->dsv != b->dsv)
		what |= 8, DC_P(" target %p/%p depth %p/%p;", a->rtv0, b->rtv0, a->dsv, b->dsv);
	if (a->blend != b->blend)
		what |= 16, DC_P(" blend %p/%p;", a->blend, b->blend);
#undef DC_P
	g_dc.f_srv += !!(what & 1), g_dc.f_cb += !!(what & 2), g_dc.f_smp += !!(what & 4), g_dc.f_rt += !!(what & 8),
		g_dc.f_bl += !!(what & 16);
	int px = b->src == RDD_PSYS + 1;
	if (px && what)
		g_dc.f_pxd++;
	loud |= !!(what & ~3);
	if (!what || !loud || g_dc.lines >= 400 || (px ? g_dc.f_pxlines++ >= 16 : g_dc.f_lines++ >= 24))
		return;
	g_dc.lines++;
	static const char *const srcw[RDD_N + 1] = { "", " CPU particles", " GPU particles", " sprites", " flare sprites",
						    " lens flares" };
	logf_("DCAP t=%.3f frame %llu draw %u (%s%s, count %u, VS %p PS %p CS %p, target %p):%s", mono(),
	      (unsigned long long)g_rt_frame, k, g_dc_kind[b->kind % 6], srcw[b->src <= RDD_N ? b->src : 0], b->n, b->sh[0],
	      b->sh[1], b->sh[2], b->rtv0, o);
}
/* `fx d3dsame MASK` (eyesame tests): eye 1's draw k binds eye 0's shader views and constant bytes for the draw (bit 0
 * the CPU-particle draws, bit 1 all others; bit 2 only the views, bit 3 only the constants; neither = both), then its
 * own again - if the eyes then match, the cause is in that state. Only for draws alike in both eyes (kind, count,
 * shaders). The originals are called directly (no hook sees our binds). */
typedef void (*dc_map0_fn)(void *ctx, void *res, uint32_t sub, uint32_t type, uint32_t flags, void *out);
struct dc_undo { int srv[3], ncb; struct { void *buf; uint32_t off, size; } cb[3 * DC_CB]; };
static const uint32_t g_dc_setsrv[3] = { DCS_VSSRV, DCS_PSSRV, DCS_CSSRV };
static void dc_cb_write(void *ctx, void *buf, uint32_t off, uint32_t size)
{
	unsigned char m[16] = { 0 };
	if (((dc_map_fn)g_dc.orig[DCS_MAP])(ctx, buf, 0, 4, 0, m) < 0 || !*(unsigned char **)m)
		return;
	memcpy(*(unsigned char **)m, g_dc.pool[g_dc.cur] + off, size);
	((dc_unmap_fn)g_dc.orig[DCS_UNMAP])(ctx, buf, 0);
}
static int dc_replay(void *ctx, const struct dc_rec *a, const struct dc_rec *b, struct dc_undo *u)
{
	int m = atomic_load_explicit(&g_dc.replay, memory_order_relaxed), done = 0;
	if (!(b->src == RDD_PSYS + 1 ? m & 1 : m & 2) || a->kind != b->kind || a->n != b->n || memcmp(a->sh, b->sh, sizeof a->sh))
		return 0;
	int views = !(m & 12) || (m & 4), consts = !(m & 12) || (m & 8);
	memset(u, 0, sizeof *u);
	for (int s = 0; s < 3 && views; s++)
		if (memcmp(a->srv[s], b->srv[s], sizeof a->srv[s])) {
			((dc_set_fn)g_dc.orig[g_dc_setsrv[s]])(ctx, 0, DC_SRV, a->srv[s]);
			u->srv[s] = 1, done = 1, g_dc.r_srv++;
		}
	for (int s = 0; s < 3 && consts; s++)
		for (int j = 0; j < DC_CB; j++) {
			const struct dc_cb *x = &a->cb[s][j], *y = &b->cb[s][j];
			if (!x->buf || x->buf != y->buf || !x->h || !y->h || x->h == y->h || x->off == ~0u || y->off == ~0u ||
			    x->size != y->size)
				continue;
			dc_cb_write(ctx, x->buf, x->off, x->size);
			u->cb[u->ncb].buf = y->buf, u->cb[u->ncb].off = y->off, u->cb[u->ncb++].size = y->size;
			done = 1, g_dc.r_cb++;
		}
	if (done)
		g_dc.r_draws[b->src == RDD_PSYS + 1]++;
	return done;
}
static void dc_unreplay(void *ctx, const struct dc_rec *b, const struct dc_undo *u)
{
	for (int s = 0; s < 3; s++)
		if (u->srv[s])
			((dc_set_fn)g_dc.orig[g_dc_setsrv[s]])(ctx, 0, DC_SRV, b->srv[s]);
	for (int i = 0; i < u->ncb; i++)
		dc_cb_write(ctx, u->cb[i].buf, u->cb[i].off, u->cb[i].size);
}
/* before a draw: eye 0's kept, eye 1's compared (`fx d3d`) and replayed (`fx d3dsame`); 1 = undo after the draw */
static int dc_draw(void *ctx, int kind, uint32_t n, struct dc_rec *now, struct dc_undo *u)
{
	if (!dc_rt || g_dc.state != 2 || g_dc.pass < 0)
		return 0;
	int e = g_dc.pass;
	uint32_t k = g_dc.k[e]++;
	if (k >= DC_REC_N)
		return 0;
	if (e == 0) {
		dc_fill(&g_dc.rec[k], kind, n);
		return 0;
	}
	if (k >= g_dc.k[0])
		return 0;
	dc_fill(now, kind, n);
	if (g_dc.left > 0)
		dc_cmp(k, &g_dc.rec[k], now);
	return atomic_load_explicit(&g_dc.replay, memory_order_relaxed) && dc_replay(ctx, &g_dc.rec[k], now, u);
}
#define DC_DRAW_PRE(KIND, N)                                                                                          \
	struct dc_rec now;                                                                                             \
	struct dc_undo u;                                                                                              \
	int und = dc_draw(ctx, KIND, N, &now, &u)
#define DC_DRAW_POST()                                                                                                \
	if (und)                                                                                                       \
	dc_unreplay(ctx, &now, &u)
typedef void (*dc_di_fn)(void *ctx, uint32_t n, uint32_t start, int32_t base);
__attribute__((used)) void somvr_dc_drawidx(void *ctx, uint32_t n, uint32_t start, int32_t base)
{
	DC_DRAW_PRE(1, n);
	((dc_di_fn)g_dc.orig[DCS_DRAWIDX])(ctx, n, start, base);
	DC_DRAW_POST();
}
typedef void (*dc_d_fn)(void *ctx, uint32_t n, uint32_t start);
__attribute__((used)) void somvr_dc_draw(void *ctx, uint32_t n, uint32_t start)
{
	DC_DRAW_PRE(2, n);
	((dc_d_fn)g_dc.orig[DCS_DRAW])(ctx, n, start);
	DC_DRAW_POST();
}
typedef void (*dc_dii_fn)(void *ctx, uint32_t n, uint32_t inst, uint32_t start, int32_t base, uint32_t sinst);
__attribute__((used)) void somvr_dc_drawidxi(void *ctx, uint32_t n, uint32_t inst, uint32_t start, int32_t base, uint32_t sinst)
{
	DC_DRAW_PRE(3, n * 65536u + inst);
	((dc_dii_fn)g_dc.orig[DCS_DRAWIDXI])(ctx, n, inst, start, base, sinst);
	DC_DRAW_POST();
}
typedef void (*dc_dri_fn)(void *ctx, uint32_t n, uint32_t inst, uint32_t start, uint32_t sinst);
__attribute__((used)) void somvr_dc_drawi(void *ctx, uint32_t n, uint32_t inst, uint32_t start, uint32_t sinst)
{
	DC_DRAW_PRE(4, n * 65536u + inst);
	((dc_dri_fn)g_dc.orig[DCS_DRAWI])(ctx, n, inst, start, sinst);
	DC_DRAW_POST();
}
typedef void (*dc_disp_fn)(void *ctx, uint32_t x, uint32_t y, uint32_t z);
__attribute__((used)) void somvr_dc_dispatch(void *ctx, uint32_t x, uint32_t y, uint32_t z)
{
	DC_DRAW_PRE(5, x * 65536u + y * 256u + z);
	((dc_disp_fn)g_dc.orig[DCS_DISPATCH])(ctx, x, y, z);
	DC_DRAW_POST();
}

static void dc_install(void)
{
	char why[512];
	for (int i = 0; i < DCS_N; i++)
		if (check_site(&g_dc_sites[i], why, sizeof why) != 0) {
			logf_("DCAP site %s %#lx MISMATCH: %s (no D3D11 capture)", g_dc_sites[i].name,
			      (unsigned long)g_dc_sites[i].addr, why);
			return;
		}
	void *fn[DCS_N] = { (void *)somvr_dc_vscb, (void *)somvr_dc_pssrv, (void *)somvr_dc_pssh, (void *)somvr_dc_pssmp,
			    (void *)somvr_dc_vssh, (void *)somvr_dc_drawidx, (void *)somvr_dc_draw, (void *)somvr_dc_map,
			    (void *)somvr_dc_unmap, (void *)somvr_dc_pscb, (void *)somvr_dc_drawidxi, (void *)somvr_dc_drawi,
			    (void *)somvr_dc_vssrv, (void *)somvr_dc_omrt, (void *)somvr_dc_blend, (void *)somvr_dc_dispatch,
			    (void *)somvr_dc_updsub, (void *)somvr_dc_cssrv, (void *)somvr_dc_cssh, (void *)somvr_dc_cscb };
	int n = 0;
	for (int i = 0; i < DCS_N; i++)
		g_dc.orig[i] = g_dc_sites[i].target;
	for (int i = 0; i < DCS_N; i++)
		n += go_slot(&g_dc_sites[i], fn[i], &g_dc.orig[i]);
	g_dc.ok = n == DCS_N;
	g_dc.pass = -1;
	logf_("DCAP t=%.3f the threaded context's %d slots %s (`fx d3d N`: N stereo frames compared draw by draw)", mono(), n,
	      g_dc.ok ? "hooked" : "NOT all hooked");
}
/* rd's BeginRenderTarget hook (render thread): an eye's pass starts at its first begin of rt3D */
static void dc_begin(int is3d, int ph)
{
	if (g_dc.state != 2 || !is3d || ph > 1 || g_dc.pass >= ph)
		return;
	if (ph == 1 && g_dc.pass < 0 && !g_dc.k[0])
		return; /* eye 0's pass was not seen this frame */
	g_dc.pass = ph, g_dc.k[ph] = 0;
}
static void dc_frame_line(void)
{
	char s[600];
	size_t n = 0;
	for (int st = 0; st < 3; st++) {
		for (int j = 0; j < DC_CB; j++)
			if (g_dc.f_cbslot[st][j] && n < sizeof s - 40)
				n += (size_t)snprintf(s + n, sizeof s - n, " %s b%d x%u", g_dc_stage[st], j, g_dc.f_cbslot[st][j]);
		for (int j = 0; j < DC_SRV; j++)
			if (g_dc.f_srvslot[st][j] && n < sizeof s - 40)
				n += (size_t)snprintf(s + n, sizeof s - n, " %s t%d x%u", g_dc_stage[st], j, g_dc.f_srvslot[st][j]);
	}
	logf_("DCAP t=%.3f frame %llu: eye 0 drew %u, eye 1 %u; %u compared (%u not alike), different: shader views %u, "
	      "constants %u (bytes unknown %u), samplers %u, targets %u, blend %u; CPU-particle draws compared %u, different %u;"
	      " by slot:%s; buffers' bytes so far: %llu maps, %llu updates, pool %u KB used, %llu not kept; d3dsame %#x so far: "
	      "%llu draws (%llu CPU-particle) given eye 0's state, %llu view sets, %llu constant buffers",
	      mono(), (unsigned long long)g_rt_frame, g_dc.k[0], g_dc.k[1], g_dc.f_cmp, g_dc.f_mis, g_dc.f_srv, g_dc.f_cb,
	      g_dc.f_cbunk, g_dc.f_smp, g_dc.f_rt, g_dc.f_bl, g_dc.f_px, g_dc.f_pxd, n ? s : " none",
	      (unsigned long long)g_dc.maps, (unsigned long long)g_dc.upds, g_dc.used >> 10, (unsigned long long)g_dc.full,
	      atomic_load(&g_dc.replay), (unsigned long long)(g_dc.r_draws[0] + g_dc.r_draws[1]),
	      (unsigned long long)g_dc.r_draws[1], (unsigned long long)g_dc.r_srv, (unsigned long long)g_dc.r_cb);
}
/* H9 (render thread): the frame's end - its summary, the next frame armed */
static void dc_present(void)
{
	dc_rt = 1;
	if (!g_dc.ok)
		return;
	int rp = atomic_load_explicit(&g_dc.replay, memory_order_relaxed);
	if (g_dc.state == 2 && g_dc.left > 0) {
		dc_frame_line(), g_dc.frames++;
		if (--g_dc.left <= 0)
			logf_("DCAP t=%.3f the capture is over (%llu frames)%s", mono(), (unsigned long long)g_dc.frames,
			      rp ? "; recording goes on for fx d3dsame" : "");
	}
	if (g_dc.state == 2 && g_dc.left <= 0 && !rp) {
		g_dc.state = 0;
		free(g_dc.rec), free(g_dc.pool[0]), free(g_dc.pool[1]);
		g_dc.rec = NULL, g_dc.pool[0] = g_dc.pool[1] = NULL;
		memset(g_dc.buf, 0, sizeof g_dc.buf);
		g_dc.nmap = 0, g_dc.used = 0;
	} else if (g_dc.state == 1)
		g_dc.state = 2;
	if (g_dc.state == 2 && g_dc.left <= 0) { /* recording for `fx d3dsame`: a capture asked for logs from the next frame */
		int r = atomic_exchange(&g_dc.req, 0);
		if (r > 0)
			g_dc.left = r, g_dc.lines = 0;
	}
	if (!g_dc.state) {
		int r = atomic_exchange(&g_dc.req, 0);
		if (r <= 0 && atomic_load_explicit(&g_dc.replay, memory_order_relaxed))
			r = -1; /* `fx d3dsame` alone: recording, no logs */
		if (r) {
			g_dc.rec = malloc(sizeof(struct dc_rec) * DC_REC_N);
			g_dc.pool[0] = malloc(DC_POOL), g_dc.pool[1] = malloc(DC_POOL);
			if (!g_dc.rec || !g_dc.pool[0] || !g_dc.pool[1]) {
				free(g_dc.rec), free(g_dc.pool[0]), free(g_dc.pool[1]);
				g_dc.rec = NULL, g_dc.pool[0] = g_dc.pool[1] = NULL;
				logf_("DCAP t=%.3f no memory for the capture", mono());
			} else
				g_dc.state = 1, g_dc.left = r > 0 ? r : 0, g_dc.cur = 0, g_dc.used = 0, g_dc.lines = 0;
		}
	} else if (g_dc.state == 2)
		dc_pool_turn();
	g_dc.pass = -1, g_dc.k[0] = g_dc.k[1] = 0;
	g_dc.f_cmp = g_dc.f_mis = g_dc.f_srv = g_dc.f_cb = g_dc.f_cbunk = g_dc.f_smp = g_dc.f_rt = g_dc.f_bl = 0;
	g_dc.f_px = g_dc.f_pxd = g_dc.f_lines = g_dc.f_pxlines = g_dc.f_mislines = 0;
	memset(g_dc.f_cbslot, 0, sizeof g_dc.f_cbslot), memset(g_dc.f_srvslot, 0, sizeof g_dc.f_srvslot);
}

/* SOMVR_FX_TEST's trace (`fx trace N`): render-thread events of N stereo frames */
__attribute__((format(printf, 1, 2))) static void rd_tr(const char *fmt, ...)
{
	if (!g_rcf.tracing || atomic_fetch_add(&g_rcf.tr_lines, 1) >= 900)
		return;
	char b[400];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	logf_("FXTRACE t=%.4f f=%llu ph=%d%s %s", mono(), (unsigned long long)g_rt_frame, g_rcf.phase, g_rcf.swapped ? " (swapped)" : "",
	      b);
}
static void rd_tr_bound(char *o, size_t n)
{
	char a[4][24];
	for (int i = 0; i < 4; i++)
		rd_cfname(*(volatile uintptr_t *)g_rd_bindv[i], a[i], sizeof a[i]);
	snprintf(o, n, "bound cur=%s forced=%s discard=%s last=%s", a[0], a[1], a[2], a[3]);
}

static void rcf_swap(uintptr_t rt)
{
	for (int k = 0; k < 2; k++) {
		unsigned char t[RD_CF_SZ], *c = (unsigned char *)(rt + RD_CF_OFF + k * RD_CF_SZ);
		memcpy(t, c, RD_CF_SZ);
		memcpy(c, g_rcf.x[k], RD_CF_SZ);
		memcpy(g_rcf.x[k], t, RD_CF_SZ);
	}
	g_rcf.swapped = !g_rcf.swapped;
}

__attribute__((format(printf, 1, 2))) static int rcf_off(const char *fmt, ...)
{
	char b[300];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	g_rcf.state = -1;
	logf_("CURFRAME t=%.3f OFF for the run (the game's images in both eyes): %s", mono(), b);
	return 0;
}

/* a COM-style object's Release (slot 2), as FUN_00747e60 releases what it replaces */
static void rcf_release(unsigned char *slot)
{
	void *o = *(void **)slot;
	if (o) {
		void (*rel)(void *) = (void (*)(void *))(*(uintptr_t **)o)[2];
		rel(o);
	}
	*(void **)slot = NULL;
}

/* our two images like the target's (render thread); 1 = made */
static int rcf_make(uintptr_t rt)
{
	struct d3d_src s;
	if (!g_d3d_ok || !d3d_source(HOLDER_TEX3D, &s))
		return rcf_off("the device, its context or the 3D surface is not the expected object");
	rd_cfmake_fn make = (rd_cfmake_fn)g_rd_sites[RDS_CF_MAKE].target;
	for (int k = 0; k < 2; k++) {
		unsigned char *c = (unsigned char *)(rt + RD_CF_OFF + k * RD_CF_SZ), *x = g_rcf.x[k];
		void *ctex = *(void **)(c + 0x18);
		int w = *(int *)(c + 4), h = *(int *)(c + 8);
		if (!ctex || vptr_of(ctex) != g_d3d.vt_tex || w < 16 || h < 16 || w > 16384 || h > 16384)
			return rcf_off("target %#lx image %d: texture %p (vtable %#lx) %dx%d is not a made image", (unsigned long)rt, k,
				      ctex, (unsigned long)vptr_of(ctex), w, h);
		if (g_rcf.state == 1)
			for (int i = 0; i < 3; i++)
				rcf_release(x + 0x18 + 8 * i);
		memcpy(x, c, RD_CF_SZ);
		memset(x + 0x18, 0, 0x18);
		int r = make(x, w, h, *(int *)(c + 0xc), *(int *)(c + 0x10), *(int *)(c + 0x14), 1);
		void *xt = *(void **)(x + 0x18);
		if ((r & 0xff) != 1 || !xt || !*(void **)(x + 0x20) || !*(void **)(x + 0x28) || vptr_of(xt) != g_d3d.vt_tex)
			return rcf_off("the maker for image %d (%dx%d formats %d/%d): %d, texture %p, views %p %p", k, w, h,
				      *(int *)(c + 0xc), *(int *)(c + 0x10), r & 0xff, xt, *(void **)(x + 0x20), *(void **)(x + 0x28));
		s.copy(s.ctx, xt, ctex); /* eye 1's first frame reads the game's last pixels, not an empty image */
	}
	g_rcf.state = 1, g_rcf.rt = rt, g_rcf.makes++;
	logf_("CURFRAME t=%.3f made %llu: eye 1's two images for target %#lx (%dx%d formats %d/%d and %d/%d; textures %p %p)",
	      mono(), (unsigned long long)g_rcf.makes, (unsigned long)rt, *(int *)(rt + RD_CF_OFF + 4),
	      *(int *)(rt + RD_CF_OFF + 8), *(int *)(rt + RD_CF_OFF + 0xc), *(int *)(rt + RD_CF_OFF + 0x10),
	      *(int *)(rt + RD_CF_OFF + RD_CF_SZ + 0xc), *(int *)(rt + RD_CF_OFF + RD_CF_SZ + 0x10), *(void **)(g_rcf.x[0] + 0x18),
	      *(void **)(g_rcf.x[1] + 0x18));
	return 1;
}
/* ours still like the target's (sizes and formats of both) */
static int rcf_same(uintptr_t rt)
{
	for (int k = 0; k < 2; k++)
		if (memcmp(g_rcf.x[k] + 4, (const void *)(rt + RD_CF_OFF + k * RD_CF_SZ + 4), 0x10))
			return 0;
	return 1;
}

/* H8, before the original (render thread, every EndRenderTarget): the trace and the counts */
static void rd_rt_end_pre(uintptr_t rt, uint32_t flags)
{
	if (!g_rd.test || !rt)
		return;
	int is3d = rt == (uintptr_t)g_rt_target || (g_rcf.rt && rt == g_rcf.rt), ph = g_rcf.phase < 3 ? g_rcf.phase : 2;
	g_rcf.ends[ph][is3d]++;
	if (flags == 1)
		g_rcf.ends1[ph][is3d]++;
	if (g_rcf.tracing) {
		char b[160];
		rd_tr_bound(b, sizeof b);
		rd_tr("end rt=%#lx%s flags=%u mode=%d rtflags=%#x; %s", (unsigned long)rt, is3d ? " (3D)" : "", flags,
		      *(volatile int *)(rt + RD_RT_MODE), *(volatile uint32_t *)(rt + RT_FLAGS), b);
	}
}

/* H8's eye end (render thread, after the original): eye 0's end = eye 1's pass starts (ours in), eye 1's = it is over */
static void rd_rt_eye_end(int e, uintptr_t rt, uint64_t fr)
{
	g_rcf.frame = fr;
	g_dc.pass = -1; /* `fx d3d`: the next pass starts at its first begin of rt3D */
	if (e == 0) {
		g_rcf.phase = 1;
		if (atomic_load_explicit(&g_rcf.on, memory_order_relaxed) && g_rcf.ok && g_rcf.state >= 0 && !g_rcf.swapped) {
			uint32_t rf = *(volatile uint32_t *)(rt + RT_FLAGS);
			if (!(rf & RD_RTF_CF)) {
				if (!g_rcf.nocf++)
					logf_("CURFRAME t=%.3f target %#lx (flags %#x) has no current-frame images: nothing to swap",
					      mono(), (unsigned long)rt, rf);
			} else if ((g_rcf.state == 1 && g_rcf.rt == rt && rcf_same(rt)) || rcf_make(rt)) {
				rcf_swap(rt);
				g_rcf.swaps++;
			}
		}
	} else if (e == 1) {
		if (g_rcf.swapped)
			rcf_swap(g_rcf.rt);
		g_rcf.phase = 2;
		atomic_store_explicit(&g_rcf.e1_frame, fr, memory_order_release);
	}
	rd_tr("eye %d end (target %#lx)%s", e, (unsigned long)rt, e == 0 && g_rcf.swapped ? ": eye 1's images in" : "");
}

static void rd_vr_present(void);

/* H9 (render thread): the frame's end */
static void rd_rt_present(void)
{
	if (g_rd.test)
		rd_vr_present(), dc_present();
	if (g_rcf.swapped) {
		rcf_swap(g_rcf.rt);
		if (g_rcf.late++ < 8)
			logf_("CURFRAME t=%.3f a present with eye 1's images still in (no eye 1 end since eye 0's): swapped back",
			      mono());
	}
	if (g_rcf.tracing && ++g_rcf.tr_frames >= 1) {
		rd_tr("present");
		g_rcf.tracing = 0;
	}
	g_rcf.phase = 0;
	if (!g_rcf.tracing && atomic_load_explicit(&g_rcf.tr_req, memory_order_relaxed) > 0) {
		atomic_fetch_sub(&g_rcf.tr_req, 1);
		g_rcf.tracing = 1, g_rcf.tr_frames = 0;
	}
}

/* RD_BRT_A / RD_BRT_B (SOMVR_FX_TEST, render thread): a BeginRenderTarget's binding of the target's frame images */
typedef uint64_t (*rd_brt_fn)(uintptr_t rt);
__attribute__((used)) uint64_t somvr_w_rdbrt(uintptr_t rt)
{
	uint64_t r = ((rd_brt_fn)g_rd_sites[RDS_BRT_A].target)(rt);
	int is3d = rt && (rt == (uintptr_t)g_rt_target || (g_rcf.rt && rt == g_rcf.rt)), ph = g_rcf.phase < 3 ? g_rcf.phase : 2;
	g_rcf.begins[ph][is3d]++;
	dc_begin(is3d, ph);
	if (g_rcf.tracing) {
		char b[160];
		rd_tr_bound(b, sizeof b);
		rd_tr("begin rt=%#lx%s mode=%d rtflags=%#x; %s", (unsigned long)rt, is3d ? " (3D)" : "",
		      *(volatile int *)(rt + RD_RT_MODE), *(volatile uint32_t *)(rt + RT_FLAGS), b);
	}
	return r;
}

/* draw N skipped: SOMVR_FLATFX's lens flares, or `fx hide`'s bit (in the pass `fx hideeye` names: -1 both) */
static inline int rd_skip(int n)
{
	if (n == RDD_LENSFLARE && (atomic_load_explicit(&g_rd.off, memory_order_relaxed) & RDF_FLARES))
		return 1;
	if (!(atomic_load_explicit(&g_rd.hide, memory_order_relaxed) & (1u << n)))
		return 0;
	int e = atomic_load_explicit(&g_rcf.hide_eye, memory_order_relaxed);
	return e < 0 || e == g_rcf.phase;
}

/* SOMVR_FX_TEST, CPU particles' draw (render thread, before the original sorts the block in place): the block as the
 * copy left it? (its record from somvr_w_rdprep: the same bytes / changed / re-filled by a later frame) */
static int rd_draw0_probe(void **a, uintptr_t *fx_out)
{
	const unsigned char *blk = a ? (const unsigned char *)a[2] : NULL;
	int ph = g_rcf.phase < 3 ? g_rcf.phase : 2;
	atomic_fetch_add_explicit(&g_rcf.b_draws[ph], 1, memory_order_relaxed);
	*fx_out = 0;
	if (!blk)
		return -1;
	uint32_t n = *(const volatile uint32_t *)blk;
	if (g_rcf.tracing && n >= 4) {
		char b[160];
		rd_tr_bound(b, sizeof b);
		rd_tr("CPU particles: block %p, %u particles; %s", (const void *)blk, n, b);
	}
	unsigned i = (unsigned)(((uintptr_t)blk >> 3) * 2654435761u) % RD_BLK_N;
	uintptr_t k = atomic_load_explicit(&g_rcf.blk[i].key, memory_order_acquire);
	if (k != (uintptr_t)blk) {
		atomic_fetch_add_explicit(&g_rcf.b_norec[ph == 1], 1, memory_order_relaxed);
		return -1;
	}
	uint32_t len = g_rcf.blk[i].len, fr = g_rcf.blk[i].frame;
	int e = g_rcf.blk[i].eye;
	uint64_t h0 = g_rcf.blk[i].h;
	uintptr_t fx = g_rcf.blk[i].fx;
	if (atomic_load_explicit(&g_rcf.blk[i].key, memory_order_acquire) != (uintptr_t)blk || e < 0 || e > 1)
		return -1;
	if (fr > (uint32_t)g_rt_frame && g_rt_frame) {
		atomic_fetch_add_explicit(&g_rcf.b_newer[e], 1, memory_order_relaxed);
		if (atomic_fetch_add(&g_rcf.b_lines, 1) < 12)
			logf_("FLATFX block t=%.3f eye %d's block %p of frame %llu drawn with a record of frame %u: re-filled by a "
			      "later frame before its draw", mono(), e, (const void *)blk, (unsigned long long)g_rt_frame, fr);
		return -1;
	}
	uint64_t h = rd_fnv(blk, len);
	int same = h == h0;
	atomic_fetch_add_explicit(same ? &g_rcf.b_same[e] : &g_rcf.b_diff[e], 1, memory_order_relaxed);
	if (!same && atomic_fetch_add(&g_rcf.b_lines, 1) < 12)
		logf_("FLATFX block t=%.3f eye %d's block %p (%u bytes, %u particles, frame %u; the render thread at frame %llu, "
		      "phase %d) CHANGED between its copy and its draw", mono(), e, (const void *)blk, len, n, fr,
		      (unsigned long long)g_rt_frame, g_rcf.phase);
	*fx_out = fx;
	return e;
}

/* SOMVR_FX_TEST: the draw's render context behind a proxy for the one call (a[4]: slot 9 the vertex map, slot 15 the
 * draw - their vertices hashed, the other slots straight through), so the two eyes' vertices of one emitter can be
 * compared. The proxy is {vtable, the real context}; thunk k = this -> the real one, then its slot k. */
#define RD_PX_T(K, OFF)                                                                                                \
	__attribute__((naked, used)) void somvr_rd_px##K(void)                                                         \
	{                                                                                                              \
		__asm__ volatile("movq 8(%rdi), %rdi\n\tmovq (%rdi), %rax\n\tjmpq *" #OFF "(%rax)\n\t");                  \
	}
RD_PX_T(0, 0) RD_PX_T(1, 8) RD_PX_T(2, 16) RD_PX_T(3, 24) RD_PX_T(4, 32) RD_PX_T(5, 40) RD_PX_T(6, 48) RD_PX_T(7, 56)
RD_PX_T(8, 64) RD_PX_T(9, 72) RD_PX_T(10, 80) RD_PX_T(11, 88) RD_PX_T(12, 96) RD_PX_T(13, 104) RD_PX_T(14, 112)
RD_PX_T(15, 120) RD_PX_T(16, 128) RD_PX_T(17, 136) RD_PX_T(18, 144) RD_PX_T(19, 152) RD_PX_T(20, 160) RD_PX_T(21, 168)
RD_PX_T(22, 176) RD_PX_T(23, 184) RD_PX_T(24, 192) RD_PX_T(25, 200) RD_PX_T(26, 208) RD_PX_T(27, 216) RD_PX_T(28, 224)
RD_PX_T(29, 232) RD_PX_T(30, 240) RD_PX_T(31, 248)
static struct { unsigned char *ptr; uint32_t granted, req; int64_t nv; uint64_t h; int maps, draws; } g_px; /* render thread */
/* the frame's CPU-particle vertex ranges as drawn (render thread), hashed again at the present: memory the dynamic vertex
 * buffer handed out again before the frame went to the GPU (CD3D11BaseCustomRenderCallback: 4 buffers +0x80..+0x98 in
 * turn, the one in use +0x100, its fill +0xa0, the capacity +0xc; DISCARD at a buffer's start, NO_OVERWRITE after) */
#define RD_VR_N 512
static struct {
	struct { const unsigned char *p; uint32_t len, idx, off; uint64_t h; int eye; } r[RD_VR_N], pr[RD_VR_N];
	int n, npr, cap_logged;
	uint64_t pranges[2], pchanged[2]; /* the previous frame's ranges at this present: changed during this frame */
	uint32_t last_idx;
	uint64_t frames, ranges[2], changed[2], rot, rot_max, rot_frame, over;
	uint32_t lines;
	void *cb;                    /* the callback seen at a draw */
	_Atomic int grow;            /* `fx vbgrow N`: its four buffers made N times bigger at the next present */
} g_vr;

/* `fx vbgrow N` (SOMVR_FX_TEST, H9, render thread, no lock outstanding): the custom draws' four dynamic vertex buffers
 * made again N times bigger (ID3D11Buffer slot 10 GetDesc, the device's slot 3 CreateBuffer, the old ones released),
 * the capacity +0xc and the fill +0xa0 set: fewer turns a frame */
typedef void (*rd_bufdesc_fn)(void *buf, void *desc);
typedef int32_t (*rd_mkbuf_fn)(void *dev, const void *desc, const void *init, void **out);
static void rd_vb_grow(void)
{
	int n = atomic_exchange(&g_vr.grow, 0);
	unsigned char *cb = g_vr.cb;
	struct d3d_src s;
	if (n < 2 || !cb)
		return;
	if (*(volatile unsigned char *)(cb + 0xf8) || !g_d3d_ok || !d3d_source(HOLDER_TEX3D, &s)) {
		logf_("FLATFX vb t=%.3f vbgrow %d: not now (a lock outstanding or no device)", mono(), n);
		return;
	}
	uint32_t cap0 = *(volatile uint32_t *)(cb + 0xc), d[6], cap = 0;
	void *nb[4] = { 0 };
	for (int i = 0; i < 4; i++) {
		void *b = *(void **)(cb + 0x80 + 8 * i);
		if (!b) {
			logf_("FLATFX vb t=%.3f vbgrow %d: buffer %d missing: not grown", mono(), n, i);
			goto fail;
		}
		memset(d, 0, sizeof d);
		((rd_bufdesc_fn)(*(void ***)b)[10])(b, d);
		if (d[0] != cap0 || d[0] > (64u << 20) / (uint32_t)n) {
			logf_("FLATFX vb t=%.3f vbgrow %d: buffer %d is %u bytes (usage %u bind %#x cpu %#x), capacity %u: not grown",
			      mono(), n, i, d[0], d[1], d[2], d[3], cap0);
			goto fail;
		}
		d[0] *= (uint32_t)n, cap = d[0];
		int32_t hr = ((rd_mkbuf_fn)(*(void ***)s.dev)[3])(s.dev, d, NULL, &nb[i]);
		if (hr < 0 || !nb[i]) {
			logf_("FLATFX vb t=%.3f vbgrow %d: CreateBuffer(%u bytes) %d: hr %#x: not grown", mono(), n, d[0], i, (unsigned)hr);
			goto fail;
		}
	}
	for (int i = 0; i < 4; i++) {
		void *b = *(void **)(cb + 0x80 + 8 * i);
		((void (*)(void *))(*(void ***)b)[2])(b);
		*(void **)(cb + 0x80 + 8 * i) = nb[i];
	}
	*(volatile uint32_t *)(cb + 0xc) = cap, *(volatile uint32_t *)(cb + 0xa0) = 0;
	logf_("FLATFX vb t=%.3f vbgrow %d: the four buffers %u -> %u bytes (usage %u bind %#x cpu %#x)", mono(), n, cap0, cap, d[1],
	      d[2], d[3]);
	return;
fail:
	for (int i = 0; i < 4; i++)
		if (nb[i])
			((void (*)(void *))(*(void ***)nb[i])[2])(nb[i]);
}

/* SOMVR_FX_TEST (H9, render thread): the frame's vertex ranges again - changed since their draw = handed out again */
static void rd_vr_present(void)
{
	if (atomic_load_explicit(&g_vr.grow, memory_order_relaxed))
		rd_vb_grow();
	int pc[2] = { 0, 0 };
	for (int k = 0; k < g_vr.npr; k++) { /* the frame before: handed out again while this frame was recorded? */
		int e = g_vr.pr[k].eye;
		g_vr.pranges[e]++;
		if (rd_fnv(g_vr.pr[k].p, g_vr.pr[k].len) != g_vr.pr[k].h)
			g_vr.pchanged[e]++, pc[e]++;
	}
	if ((pc[0] || pc[1]) && g_vr.lines++ < 16)
		logf_("FLATFX vb t=%.3f frame %llu: of the previous frame's %d CPU-particle vertex ranges %d of eye 0's and %d of eye "
		      "1's were written again during this frame", mono(), (unsigned long long)g_rt_frame, g_vr.npr, pc[0], pc[1]);
	memcpy(g_vr.pr, g_vr.r, sizeof g_vr.r[0] * (size_t)g_vr.n);
	g_vr.npr = g_vr.n;
	if (!g_vr.n)
		return;
	g_vr.frames++;
	int ch[2] = { 0, 0 }, first = -1;
	for (int k = 0; k < g_vr.n; k++) {
		int e = g_vr.r[k].eye;
		g_vr.ranges[e]++;
		if (rd_fnv(g_vr.r[k].p, g_vr.r[k].len) != g_vr.r[k].h) {
			g_vr.changed[e]++, ch[e]++;
			if (first < 0)
				first = k;
		}
	}
	if (g_vr.rot_frame > g_vr.rot_max)
		g_vr.rot_max = g_vr.rot_frame;
	if ((ch[0] || ch[1]) && g_vr.lines++ < 16) {
		int k = first;
		logf_("FLATFX vb t=%.3f frame %llu: of its %d CPU-particle vertex ranges %d of eye 0's and %d of eye 1's CHANGED before "
		      "the present (the first: eye %d's draw %d, %p %u bytes, buffer %u at %u); buffer turns this frame %llu",
		      mono(), (unsigned long long)g_rt_frame, g_vr.n, ch[0], ch[1], g_vr.r[k].eye, k, (const void *)g_vr.r[k].p,
		      g_vr.r[k].len, g_vr.r[k].idx, g_vr.r[k].off, (unsigned long long)g_vr.rot_frame);
	}
	g_vr.n = 0, g_vr.rot_frame = 0;
}

typedef int64_t (*rd_px_map_fn)(void *, int64_t, int64_t, int64_t, void *, int64_t, int64_t);
typedef int64_t (*rd_px_draw_fn)(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
static int64_t rd_px_map(void **px, int64_t a, int64_t n, int64_t c, void *out, int64_t e, int64_t f)
{
	void *real = px[1];
	int64_t r = ((rd_px_map_fn)(*(void ***)real)[9])(real, a, n, c, out, e, f);
	g_px.ptr = out && (int32_t)r == 0 ? *(unsigned char **)out : NULL;
	g_px.granted = out ? *(uint32_t *)((char *)out + 0x10) : 0, g_px.req = (uint32_t)n, g_px.maps++;
	return r;
}
static int64_t rd_px_draw(void **px, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t nv, int64_t g)
{
	void *real = px[1];
	/* the vertices it draws (24 bytes each at least: the smaller of its two formats), read before the draw */
	if (g_px.ptr && nv > 0 && nv <= (int64_t)g_px.granted) {
		g_px.h = rd_fnv(g_px.ptr, (uint32_t)nv * 24u), g_px.nv = nv;
		uint32_t idx = *(volatile uint32_t *)((char *)real + 0x100), off = *(volatile uint32_t *)((char *)real + 0xa0);
		g_vr.cb = real;
		if (idx != g_vr.last_idx)
			g_vr.rot++, g_vr.rot_frame++, g_vr.last_idx = idx;
		if (!g_vr.cap_logged++)
			logf_("FLATFX vb t=%.3f the custom draws' dynamic vertex buffer: callback %p, capacity %u bytes, buffers %p %p %p "
			      "%p, in use %u, filled to %u", mono(), real, *(volatile uint32_t *)((char *)real + 0xc),
			      *(void **)((char *)real + 0x80), *(void **)((char *)real + 0x88), *(void **)((char *)real + 0x90),
			      *(void **)((char *)real + 0x98), idx, off);
		if (g_vr.n < RD_VR_N && (g_rcf.phase == 0 || g_rcf.phase == 1)) {
			int k = g_vr.n++;
			g_vr.r[k].p = g_px.ptr, g_vr.r[k].len = (uint32_t)nv * 24u, g_vr.r[k].h = g_px.h, g_vr.r[k].eye = g_rcf.phase;
			g_vr.r[k].idx = idx, g_vr.r[k].off = off;
		} else if (g_vr.n >= RD_VR_N)
			g_vr.over++;
	}
	g_px.draws++;
	return ((rd_px_draw_fn)(*(void ***)real)[15])(real, a, b, c, d, e, nv, g);
}
static void *g_px_vt[32] = { somvr_rd_px0, somvr_rd_px1, somvr_rd_px2, somvr_rd_px3, somvr_rd_px4, somvr_rd_px5,
			     somvr_rd_px6, somvr_rd_px7, somvr_rd_px8, (void *)rd_px_map, somvr_rd_px10, somvr_rd_px11,
			     somvr_rd_px12, somvr_rd_px13, somvr_rd_px14, (void *)rd_px_draw, somvr_rd_px16, somvr_rd_px17,
			     somvr_rd_px18, somvr_rd_px19, somvr_rd_px20, somvr_rd_px21, somvr_rd_px22, somvr_rd_px23,
			     somvr_rd_px24, somvr_rd_px25, somvr_rd_px26, somvr_rd_px27, somvr_rd_px28, somvr_rd_px29,
			     somvr_rd_px30, somvr_rd_px31 };

/* SOMVR_FX_TEST, CPU particles' draw: the probe, the original through the proxy, then eye 0's draw of an emitter
 * against eye 1's of the same frame - the camera's and the emitter's transforms it was given (a[0], a[1]: position +
 * quaternion), its props (a[3]) and the vertices it drew */
static void rd_draw0_full(void *self, void **a)
{
	uintptr_t fx = 0;
	int e = rd_draw0_probe(a, &fx);
	if (!a || !a[4] || !a[0] || !a[1]) {
		((rd_draw_fn)g_rd.orig[RDD_PSYS])(self, a);
		return;
	}
	uint64_t cam = rd_fnv((const unsigned char *)a[0], 28), em = rd_fnv((const unsigned char *)a[1], 28);
	void *px[2] = { g_px_vt, a[4] }, *real = a[4];
	memset(&g_px, 0, sizeof g_px);
	a[4] = px;
	((rd_draw_fn)g_rd.orig[RDD_PSYS])(self, a);
	a[4] = real;
	if (e < 0 || !fx)
		return;
	unsigned i = (unsigned)((fx >> 4) * 2654435761u) % RD_BLK_N;
	if (e == 0) {
		g_rcf.dr[i].fx = fx, g_rcf.dr[i].frame = g_rt_frame, g_rcf.dr[i].cam = cam, g_rcf.dr[i].em = em;
		g_rcf.dr[i].vh = g_px.h, g_rcf.dr[i].nv = (uint32_t)g_px.nv, g_rcf.dr[i].props = (uintptr_t)a[3];
		g_rcf.dr[i].empp = (uintptr_t)a[1], g_rcf.dr[i].req = g_px.req, g_rcf.dr[i].granted = g_px.granted;
		return;
	}
	if (g_rcf.dr[i].fx != fx || g_rcf.dr[i].frame != g_rt_frame) {
		atomic_fetch_add_explicit(&g_rcf.d_nomatch, 1, memory_order_relaxed);
		return;
	}
	atomic_fetch_add_explicit(&g_rcf.d_cmp, 1, memory_order_relaxed);
	int dc = cam != g_rcf.dr[i].cam, de = em != g_rcf.dr[i].em, dv = g_px.h != g_rcf.dr[i].vh,
	    dn = (uint32_t)g_px.nv != g_rcf.dr[i].nv, dp = (uintptr_t)a[3] != g_rcf.dr[i].props;
	if (dc)
		atomic_fetch_add_explicit(&g_rcf.d_cam, 1, memory_order_relaxed);
	if (de)
		atomic_fetch_add_explicit(&g_rcf.d_em, 1, memory_order_relaxed);
	if (dv)
		atomic_fetch_add_explicit(&g_rcf.d_vh, 1, memory_order_relaxed);
	if (dn)
		atomic_fetch_add_explicit(&g_rcf.d_nv, 1, memory_order_relaxed);
	if (dp)
		atomic_fetch_add_explicit(&g_rcf.d_props, 1, memory_order_relaxed);
	if ((dv || dn || de || dp || (atomic_load(&g_rd.eyesame) && dc)) && g_px.nv >= 16 &&
	    atomic_fetch_add(&g_rcf.d_lines, 1) < 24) {
		const float *m = (const float *)a[1];
		logf_("FLATFX draw t=%.3f frame %llu emitter %#lx: eye 1's draw against eye 0's: camera %s, emitter transform %s "
		      "(%p vs %p; now %.2f %.2f %.2f q %.4f %.4f %.4f %.4f), props %s, vertices %s (%lld vs %u drawn; map asked %u "
		      "got %u vs %u/%u)", mono(), (unsigned long long)g_rt_frame, (unsigned long)fx, dc ? "DIFFERENT" : "the same",
		      de ? "DIFFERENT" : "the same", a[1], (void *)g_rcf.dr[i].empp, m[0], m[1], m[2], m[3], m[4], m[5], m[6],
		      dp ? "DIFFERENT" : "the same", dv ? "DIFFERENT" : "the same", (long long)g_px.nv, g_rcf.dr[i].nv, g_px.req,
		      g_px.granted, g_rcf.dr[i].req, g_rcf.dr[i].granted);
	}
}
/* ... and the record (game thread, after the copy) */
static void rd_prep_record(const unsigned char *blk, uint32_t len, int e, uintptr_t fx)
{
	if (!blk || !len || len > (1u << 20))
		return;
	unsigned i = (unsigned)(((uintptr_t)blk >> 3) * 2654435761u) % RD_BLK_N;
	atomic_store_explicit(&g_rcf.blk[i].key, 0, memory_order_release);
	g_rcf.blk[i].len = len, g_rcf.blk[i].frame = (uint32_t)g_fr.frame, g_rcf.blk[i].eye = e, g_rcf.blk[i].fx = fx;
	g_rcf.blk[i].h = rd_fnv(blk, len);
	atomic_store_explicit(&g_rcf.blk[i].key, (uintptr_t)blk, memory_order_release);
}

#define RD_DRAW_STUB(N)                                                                                                \
	__attribute__((used)) void somvr_w_rddraw##N(void *self, void *ctx)                                            \
	{                                                                                                              \
		atomic_fetch_add_explicit(&g_rd.n_draw[N], 1, memory_order_relaxed);                                   \
		if (rd_skip(N)) {                                                                                      \
			atomic_fetch_add_explicit(&g_rd.n_skip[N], 1, memory_order_relaxed);                           \
			return;                                                                                        \
		}                                                                                                      \
		int s0 = dc_src;                                                                                       \
		dc_src = N + 1;                                                                                        \
		if (N == 0 && g_rd.test)                                                                               \
			rd_draw0_full(self, (void **)ctx);                                                             \
		else                                                                                                   \
			((rd_draw_fn)g_rd.orig[N])(self, ctx);                                                         \
		dc_src = s0;                                                                                           \
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
		if (cb->used > u0 && cb->base)
			rd_prep_record(cb->base + u0, cb->used - u0, e, fx); /* build 22: its bytes, for the draw's check */
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
	if (atomic_load_explicit(&g_rcf.sync, memory_order_relaxed)) { /* `fx sync 1`: no running ahead into the next frame */
		uint64_t f = g_fr.frame;
		double t0 = mono();
		while (atomic_load_explicit(&g_rcf.e1_frame, memory_order_acquire) < f && mono() - t0 < 0.25)
			usleep(100);
		double dt = mono() - t0;
		if (atomic_load_explicit(&g_rcf.e1_frame, memory_order_acquire) >= f)
			g_rcf.sync_ok++, g_rcf.sync_wait += dt;
		else
			g_rcf.sync_late++;
	}
	if (!atomic_load_explicit(&g_rd.cmd_pend, memory_order_acquire))
		return;
	char why[256] = "";
	int rc = es_run(g_rd.cmd, why, sizeof why);
	logf_("FLATFX t=%.3f console '%s'%s%s", cb_now(), g_rd.cmd, rc ? ": not run: " : ": run", rc ? why : "");
	atomic_store(&g_rd.cmd_pend, 0);
}

/* RD_SHAFTS: the per-view bloom/shaft setup (render thread). SOMVR_FX_TEST's `fx bloomblend 0`: the setup run on a copy
 * of the HDR state with +0xc = 0 (the factor it turns into the 1-f / f pair at 0x4622000, the bloom blend's) */
static float g_rd_bb_min = 1e9f, g_rd_bb_max = -1e9f;
static _Atomic int g_rd_bb_zero;
__attribute__((used)) void somvr_w_rdshaft(void *params)
{
	float f = *(const float *)((const char *)params + 0xc);
	if (f < g_rd_bb_min)
		g_rd_bb_min = f;
	if (f > g_rd_bb_max)
		g_rd_bb_max = f;
	if (atomic_load_explicit(&g_rd_bb_zero, memory_order_relaxed)) {
		unsigned char cp[0x5c];
		memcpy(cp, params, sizeof cp);
		*(float *)(cp + 0xc) = 0.0f;
		((rd_shaft_fn)g_rd_sites[RDS_SHAFTS].target)(cp);
	} else
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

/* H4 (game thread) around its two eye calls: 1 = an eye renders - COverlayFX's draws skipped while `overlay` is off in
 * the eyes (live: the word); 0 = after them (a flat frame's overlays are the game's) */
static void rd_ov_eyes(int in_eyes)
{
	somvr_rd_ov_skip = in_eyes && (atomic_load_explicit(&g_rd.off, memory_order_relaxed) & RDF_OVERLAY);
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
	for (int i = 0; i < RD_NPARTS; i++)
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
		while (i < RD_NPARTS && strcmp(t, g_rd_part_w[i]))
			i++;
		if (i == RD_NPARTS)
			return -1;
		m |= 1u << i;
	}
	return (int)m;
}

static void rd_state_line(const char *head)
{
	char p[64];
	rd_parts(atomic_load(&g_rd.off), p, sizeof p);
	logf_("%s t=%.3f: SOMVR_FLATFX %s (lens flares %s, shafts %s, rain %s, overlay %s); the overlays' draws (skipped in the "
	      "eyes): %llu (%llu) + %llu (%llu); draws (skipped): CPU particles %llu (%llu), "
	      "GPU particles %llu (%llu), sprites %llu (%llu), flare sprites %llu (%llu), lens flares %llu (%llu); shaft setups "
	      "%llu (shafts cleared %llu), rain draws %llu (skipped %llu), the game's vignette cleared %llu, box blur %llu; "
	      "fx hide %#x eyesame %d; the probe: CPU emitters copied flat %llu / eye 0 %llu / eye 1 %llu, eye 1 the same "
	      "particles as eye 0 %llu, different %llu, unmatched %llu; emitter updates outside the eyes %llu, in eye 0's call "
	      "%llu, in eye 1's %llu; per eye (0 / 1): blocks %llu / %llu, culled %llu / %llu, NO ROOM %llu / %llu, the writer's "
	      "capacity %u, the most used %u / %u; the frame's request buffer (all views): %u bytes, the most used %u, frames with "
	      "requests refused %llu of %llu (the most refused %u)", head, cb_now(), p, g_rd.ok[RDS_LENSFLARE] ? "hooked" : "not hooked",
	      g_rd.ok[RDS_SHAFTS] ? "hooked" : "not hooked", g_rd.ok[RDS_RAIN] ? "hooked" : "not hooked",
	      g_rd.ok[RDS_OVERLAY] && g_rd.ok[RDS_OVERLAY_IF] ? "hooked" : "not hooked",
	      (unsigned long long)somvr_rd_ov_n[0], (unsigned long long)somvr_rd_ov_skipped[0],
	      (unsigned long long)somvr_rd_ov_n[1], (unsigned long long)somvr_rd_ov_skipped[1],
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

/* build 24: SOMVR_EYE_MATCH / the word "eyedrop": a state line */
static void em_state(const char *head)
{
	logf_("%s t=%.3f: SOMVR_EYE_MATCH %s: views inside an open eye pass %llu; with a queued view of their hash kept for "
	      "their pass %llu, taken (hash) %llu; eyedrop: %llu tag(s) without an eye", head, cb_now(),
	      atomic_load(&g_em.pass) ? "pass" : "hash", (unsigned long long)atomic_load(&g_em.inpass),
	      (unsigned long long)atomic_load(&g_em.kept), (unsigned long long)atomic_load(&g_em.stolen),
	      (unsigned long long)atomic_load(&g_em.dropped));
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
			return "flatfx (not game|all|a list of flares,shafts,vignette,rain,overlay: ignored)";
		atomic_store(&g_rd.off, (unsigned)m);
		char p[64];
		rd_parts((unsigned)m, p, sizeof p);
		logf_("FLATFX t=%.3f the word: %s off in the eyes from the next frame%s", cb_now(), p,
		      (m & RDF_FLARES) && !g_rd.ok[RDS_LENSFLARE] ? " (the lens flares' slot is NOT hooked)" : "");
		return "flatfx (from the next frame)";
	}
	if (!strcmp(w, "eyematch state") || !strcmp(w, "eyematch")) {
		em_state("EYEMATCH state");
		return "eyematch (a state line)";
	}
	if (!strcmp(w, "eyematch pass") || !strcmp(w, "eyematch hash")) {
		int pass = w[9] == 'p';
		atomic_store(&g_em.pass, pass);
		logf_("EYEMATCH t=%.3f the word: %s", cb_now(), pass ? "pass (a view with the open eye pass's camera is that "
		      "eye's)" : "hash (build 23's match: the first queued view with the hash)");
		return "eyematch";
	}
	int de, dn;
	if (sscanf(w, "eyedrop %d %d", &de, &dn) == 2 && de >= 0 && de <= 1 && dn >= 0) {
		atomic_store(&g_em.drop_n, (int64_t)dn);
		atomic_store(&g_em.drop_eye, dn ? de : -1);
		logf_("EYEDROP t=%.3f the word: eye %d's bit left out of the next %d stereo tag(s) (the layer's stale-frame net)",
		      cb_now(), de, dn);
		return "eyedrop";
	}
	if (!strncmp(w, "eyematch", 8) || !strncmp(w, "eyedrop", 7))
		return "eyematch pass|hash|state, eyedrop E N (ignored)";
	if (!strcmp(w, "ktime state") || !strcmp(w, "ktime")) {
		rd_kt_state("KTIME state");
		return "ktime (a state line)";
	}
	if (!strcmp(w, "ktime frame") || !strcmp(w, "ktime game")) {
		int on = w[6] == 'f';
		atomic_store(&g_kt.on, on);
		logf_("KTIME t=%.3f the word: %s%s", cb_now(), on ? "frame (eye 1's pass with eye 0's k_fTime)" :
		      "game (eye 1's pass with what the late pass left)", on && !g_kt.ok ? " - a site did NOT match: stays the game's" : "");
		return "ktime";
	}
	if (!strcmp(w, "curframe state") || !strcmp(w, "curframe")) {
		rcf_state_line("CURFRAME state");
		return "curframe (a state line)";
	}
	if (!strcmp(w, "curframe game") || !strcmp(w, "curframe eye")) {
		int on = w[9] == 'e';
		atomic_store(&g_rcf.on, on);
		logf_("CURFRAME t=%.3f the word: %s%s", cb_now(), on ? "eye (each eye its own current-frame images)" :
		      "game (both eyes share the target's)", on && !g_rcf.ok ? " - RD_CF_MAKE did NOT match: stays the game's" : "");
		return "curframe";
	}
	if (!strncmp(w, "fx ", 3) && !g_rd.test)
		return "fx (needs SOMVR_FX_TEST=1)";
	int iv;
	if (sscanf(w, "fx hideeye %d", &iv) == 1) {
		atomic_store(&g_rcf.hide_eye, iv < 0 || iv > 1 ? -1 : iv);
		logf_("FLATFX t=%.3f the word: fx hideeye %d (`fx hide` %s)", cb_now(), iv < 0 || iv > 1 ? -1 : iv,
		      iv == 0 ? "in eye 0's pass only" : iv == 1 ? "in eye 1's pass only" : "in both eyes");
		return "fx hideeye";
	}
	if (sscanf(w, "fx sync %d", &iv) == 1) {
		atomic_store(&g_rcf.sync, iv != 0);
		logf_("FLATFX t=%.3f the word: fx sync %d (%s); so far %llu waits done (%.2f ms on average), %llu given up", cb_now(),
		      iv != 0, iv ? "the game thread waits after both eyes for eye 1's end on the render thread, 250 ms at most" :
		      "the game thread runs ahead as it does", (unsigned long long)g_rcf.sync_ok,
		      g_rcf.sync_ok ? 1000.0 * g_rcf.sync_wait / (double)g_rcf.sync_ok : 0.0, (unsigned long long)g_rcf.sync_late);
		return "fx sync";
	}
	if (!strcmp(w, "fx bloomblend 0") || !strcmp(w, "fx bloomblend game")) {
		atomic_store(&g_rd_bb_zero, w[14] == '0');
		logf_("FLATFX t=%.3f the word: %s (the HDR state's +0xc so far %.4f..%.4f)", cb_now(), w, g_rd_bb_min, g_rd_bb_max);
		return "fx bloomblend";
	}
	if (sscanf(w, "fx vbgrow %d", &iv) == 1) {
		atomic_store(&g_vr.grow, iv < 2 ? 0 : iv > 8 ? 8 : iv);
		logf_("FLATFX t=%.3f the word: fx vbgrow %d (the custom draws' four dynamic vertex buffers that many times bigger at "
		      "the next present)", cb_now(), iv);
		return "fx vbgrow";
	}
	if (sscanf(w, "fx d3dsame %i", &iv) == 1) {
		atomic_store(&g_dc.replay, iv & 15);
		logf_("DCAP t=%.3f the word: fx d3dsame %#x (eye 1's %s draws bind eye 0's %s; so far %llu draws (%llu CPU-particle), "
		      "%llu view sets, %llu constant buffers)", cb_now(), iv & 15, (iv & 3) == 3 ? "CPU-particle and other" :
		      iv & 1 ? "CPU-particle" : iv & 2 ? "non-particle" : "no", !(iv & 12) || (iv & 12) == 12 ? "views and constants" :
		      iv & 4 ? "shader views" : "constants", (unsigned long long)(g_dc.r_draws[0] + g_dc.r_draws[1]),
		      (unsigned long long)g_dc.r_draws[1], (unsigned long long)g_dc.r_srv, (unsigned long long)g_dc.r_cb);
		return "fx d3dsame";
	}
	if (sscanf(w, "fx d3d %d", &iv) == 1) {
		atomic_store(&g_dc.req, iv < 0 ? 0 : iv > 8 ? 8 : iv);
		logf_("DCAP t=%.3f the word: fx d3d %d (the next %d stereo frames after one of warm-up: eye 1's draws against eye "
		      "0's, DCAP lines; the hooks %s)", cb_now(), iv, iv, g_dc.ok ? "in" : "NOT in");
		return "fx d3d";
	}
	if (sscanf(w, "fx trace %d", &iv) == 1) {
		atomic_store(&g_rcf.tr_lines, 0);
		atomic_store(&g_rcf.tr_req, iv < 0 ? 0 : iv > 4 ? 4 : iv);
		logf_("FLATFX t=%.3f the word: fx trace %d (the render thread's targets, bindings and CPU particle draws of the "
		      "next %d stereo frames: FXTRACE lines, 900 at most; BeginRenderTarget's binding %s)", cb_now(), iv, iv,
		      g_rd.ok[RDS_BRT_A] && g_rd.ok[RDS_BRT_B] ? "hooked" : "NOT hooked");
		return "fx trace";
	}
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
	return "fx (not hide MASK|hideeye E|eyesame 0|1|sync 0|1|trace N|d3d N|console CMD|state: ignored)";
}

static void rcf_state_line(const char *head)
{
	logf_("%s t=%.3f: SOMVR_CURFRAME %s (start %s; RD_CF_MAKE %s; images %s for target %#lx), eye 1 passes with its own images "
	      "%llu, made %llu, swapped back at a present %llu, a target without images %llu; per phase (eye 0 / eye 1 / after): "
	      "EndRenderTarget of rt3D %llu / %llu / %llu (flags 1: %llu / %llu / %llu), of others %llu / %llu / %llu; "
	      "BeginRenderTarget of rt3D %llu / %llu / %llu, of others %llu / %llu / %llu; CPU particle draws %llu / %llu / %llu, "
	      "their blocks as copied (eye 0 / eye 1): the same %llu / %llu, CHANGED %llu / %llu, re-filled by a later frame "
	      "%llu / %llu, no record %llu / %llu; eye 1's draws against eye 0's of the same emitter: %llu compared (%llu "
	      "unmatched), camera different %llu, emitter transform different %llu, props different %llu, vertices "
	      "different %llu, vertex counts different %llu; vertex ranges changed before the present (eye 0 / eye 1): %llu of %llu "
	      "/ %llu of %llu in %llu frames, the previous frame's written again during the next %llu of %llu / %llu of %llu, "
	      "buffer turns %llu (the most in a frame %llu, ranges not kept %llu); fx sync: %llu "
	      "waits (%.2f ms on average), %llu given up", head, cb_now(),
	      atomic_load(&g_rcf.on) ? "eye" : "game", g_rcf.want ? "eye" : "game", g_rcf.ok ? "matched" : "NOT matched",
	      g_rcf.state == 1 ? "made" : g_rcf.state < 0 ? "OFF" : "not made yet", (unsigned long)g_rcf.rt,
	      (unsigned long long)g_rcf.swaps, (unsigned long long)g_rcf.makes, (unsigned long long)g_rcf.late,
	      (unsigned long long)g_rcf.nocf, (unsigned long long)g_rcf.ends[0][1], (unsigned long long)g_rcf.ends[1][1],
	      (unsigned long long)g_rcf.ends[2][1], (unsigned long long)g_rcf.ends1[0][1], (unsigned long long)g_rcf.ends1[1][1],
	      (unsigned long long)g_rcf.ends1[2][1], (unsigned long long)g_rcf.ends[0][0], (unsigned long long)g_rcf.ends[1][0],
	      (unsigned long long)g_rcf.ends[2][0], (unsigned long long)g_rcf.begins[0][1], (unsigned long long)g_rcf.begins[1][1],
	      (unsigned long long)g_rcf.begins[2][1], (unsigned long long)g_rcf.begins[0][0], (unsigned long long)g_rcf.begins[1][0],
	      (unsigned long long)g_rcf.begins[2][0], (unsigned long long)g_rcf.b_draws[0], (unsigned long long)g_rcf.b_draws[1],
	      (unsigned long long)g_rcf.b_draws[2], (unsigned long long)g_rcf.b_same[0], (unsigned long long)g_rcf.b_same[1],
	      (unsigned long long)g_rcf.b_diff[0], (unsigned long long)g_rcf.b_diff[1], (unsigned long long)g_rcf.b_newer[0],
	      (unsigned long long)g_rcf.b_newer[1], (unsigned long long)g_rcf.b_norec[0], (unsigned long long)g_rcf.b_norec[1],
	      (unsigned long long)g_rcf.d_cmp, (unsigned long long)g_rcf.d_nomatch, (unsigned long long)g_rcf.d_cam,
	      (unsigned long long)g_rcf.d_em, (unsigned long long)g_rcf.d_props, (unsigned long long)g_rcf.d_vh,
	      (unsigned long long)g_rcf.d_nv, (unsigned long long)g_vr.changed[0], (unsigned long long)g_vr.ranges[0],
	      (unsigned long long)g_vr.changed[1], (unsigned long long)g_vr.ranges[1], (unsigned long long)g_vr.frames,
	      (unsigned long long)g_vr.pchanged[0], (unsigned long long)g_vr.pranges[0], (unsigned long long)g_vr.pchanged[1],
	      (unsigned long long)g_vr.pranges[1],
	      (unsigned long long)g_vr.rot, (unsigned long long)g_vr.rot_max, (unsigned long long)g_vr.over,
	      (unsigned long long)g_rcf.sync_ok, g_rcf.sync_ok ? 1000.0 * g_rcf.sync_wait / (double)g_rcf.sync_ok : 0.0,
	      (unsigned long long)g_rcf.sync_late);
}

static void rd_start(const struct site_spec *sites, int *bad)
{
	memset(&g_rd, 0, sizeof g_rd);
	memset(&g_rcf, 0, sizeof g_rcf);
	atomic_store(&g_rcf.hide_eye, -1);
	g_rd_sites = sites;
	{ /* build 22: SOMVR_CURFRAME (the maker proven read-only; used only from H8's eye ends) */
		char cwhy[512];
		g_rcf.want = env_word("SOMVR_CURFRAME", (const char *const[]){ "game", "eye" }, 2, 0);
		g_rcf.ok = check_site(&sites[RDS_CF_MAKE], cwhy, sizeof cwhy) == 0;
		if (!g_rcf.ok)
			logf_("FLATFX site %s %#lx MISMATCH: %s (SOMVR_CURFRAME stays the game's)", sites[RDS_CF_MAKE].name,
			      (unsigned long)sites[RDS_CF_MAKE].addr, cwhy);
		atomic_store(&g_rcf.on, g_rcf.want);
		logf_("CONFIG curframe=%s (SOMVR_CURFRAME: %s)%s", g_rcf.want ? "eye" : "game",
		      g_rcf.want ? "each eye its own current-frame images - eye 1's pass gets two of ours" :
		      "both eyes share the 3D target's current-frame images, as the game made them",
		      g_rcf.want && !g_rcf.ok ? " - NOT possible: RD_CF_MAKE did not match" : "");
	}
	{ /* build 23: SOMVR_KTIME (the upload proven, the late write proven read-only) */
		char kwhy[512];
		memset(&g_kt, 0, sizeof g_kt);
		g_kt.want = env_word("SOMVR_KTIME", (const char *const[]){ "game", "frame" }, 2, 1);
		g_kt.ok = 1;
		for (int i = RDS_KT_UPLOAD; i <= RDS_KT_LATE; i++)
			if (check_site(&sites[i], kwhy, sizeof kwhy) != 0) {
				g_kt.ok = 0;
				logf_("FLATFX site %s %#lx MISMATCH: %s (SOMVR_KTIME stays the game's)", sites[i].name,
				      (unsigned long)sites[i].addr, kwhy);
			}
		atomic_store(&g_kt.on, g_kt.want);
		logf_("CONFIG ktime=%s (SOMVR_KTIME: %s)%s", g_kt.want ? "frame" : "game",
		      g_kt.want ? "eye 1's pass gets the shaders' clock k_fTime of eye 0's (the game's time of the frame) back" :
		      "eye 1's pass keeps the k_fRealTime the late pass of eye 0's left in k_fTime",
		      g_kt.want && !g_kt.ok ? " - NOT possible: a site did not match" : "");
	}
	{ /* build 24: SOMVR_EYE_MATCH (H3's eye match inside an open eye pass; somvr.c g_em) */
		int pass = env_word("SOMVR_EYE_MATCH", (const char *const[]){ "hash", "pass" }, 2, 1);
		atomic_store(&g_em.pass, pass);
		logf_("CONFIG eyematch=%s (SOMVR_EYE_MATCH: %s)", pass ? "pass" : "hash",
		      pass ? "a view with the open eye pass's camera is a repeat of that eye's projection, never another queued "
			     "eye's or frame's" : "build 23's: the first queued view with the hash, also inside another eye's pass");
	}
	const char *s = getenv("SOMVR_FLATFX");
	int m = s ? rd_parse(s) : 0;
	if (m < 0) {
		logf_("WARN SOMVR_FLATFX=%s is not game|all|a comma list of flares,shafts,vignette,rain,overlay: game", s);
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
	if ((g_rd.want & RDF_OVERLAY) || g_rd.test) /* build 24: COverlayFX's two draw entries (the hooks in: the word works) */
		for (int i = RDS_OVERLAY; i <= RDS_OVERLAY_IF; i++) {
			if (check_site(&sites[i], why, sizeof why) != 0) {
				logf_("FLATFX site %s %#lx -> %#lx MISMATCH: %s", sites[i].name, (unsigned long)sites[i].addr,
				      (unsigned long)sites[i].target, why);
				continue;
			}
			g_rd.ok[i] = go_slot(&sites[i], i == RDS_OVERLAY ? (void *)somvr_w_rdov18 : (void *)somvr_w_rdovif,
					     &somvr_rd_ov_orig[i - RDS_OVERLAY]);
		}
	if (g_rd.test) /* build 22: the trace's BeginRenderTarget bindings */
		for (int i = RDS_BRT_A; i <= RDS_BRT_B; i++)
			g_rd.ok[i] = bo_patch_call(&sites[i], (void *)somvr_w_rdbrt);
	if (g_rd.test) /* build 23: the D3D11 capture's pass-throughs (`fx d3d N`) */
		dc_install();
	unsigned have = (g_rd.ok[RDS_LENSFLARE] ? RDF_FLARES : 0) | (g_rd.ok[RDS_SHAFTS] ? RDF_SHAFTS : 0) | RDF_VIGNETTE |
			(g_rd.ok[RDS_RAIN] ? RDF_RAIN : 0) | (g_rd.ok[RDS_OVERLAY] && g_rd.ok[RDS_OVERLAY_IF] ? RDF_OVERLAY : 0);
	atomic_store(&g_rd.off, g_rd.want & have);
	char p[64], q[64];
	rd_parts(g_rd.want, p, sizeof p);
	rd_parts(g_rd.want & ~have, q, sizeof q);
	logf_("CONFIG flatfx=%s (SOMVR_FLATFX: the flat screen's effects off in the eyes - lens flares' draw, the sun shafts' "
	      "passes, the game's vignette and box blur, the lens drops, the full-screen overlays (blood, low health); motion "
	      "blur, DoF and the shake were already off)%s%s "
	      "test=%d (SOMVR_FX_TEST: the particle and sprite draws' pass-through %s)", p, g_rd.want & ~have ? "; NOT hooked: " : "",
	      g_rd.want & ~have ? q : "", g_rd.test,
	      !g_rd.test ? "off" : g_rd.ok[RDS_PSYS] && g_rd.ok[RDS_GPUPSYS] && g_rd.ok[RDS_SPRITE] && g_rd.ok[RDS_FLARESPR]
		      ? "hooked" : "NOT all hooked");
}

static void rd_end_log(void)
{
	em_state("EYEMATCH END");
	rd_kt_state("KTIME END");
	if (g_rd.want || g_rd.test || g_rd.vis_full)
		rd_state_line("FLATFX END");
	if (g_rcf.want || g_rcf.swaps || g_rd.test)
		rcf_state_line("CURFRAME END");
}
