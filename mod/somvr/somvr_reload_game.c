/* somvr_reload_game.c - the logic module's side of the Windows host/logic split (win/somvr_win_host.h,
 * docs/re-xr-pipeline.md 156): the entry points the host calls - the init (the boot's, or a reload's in the world),
 * the COM rows, the first Present's work, the quiet check, the stop - included by somvr.c on Windows only.
 *
 * A reloaded module starts fresh, as if the mod had just started with the player already in the world: somvr_start
 * runs again (the switches re-read, so a switch change is a reload with the same code; the checks see the game's
 * original bytes through the host's view; every hook claims its slot again). What only the boot or a load would
 * have told it comes from the host: D3D's device, context, swap chain and first Present (d3d_replay), the eye / HUD /
 * hands-depth textures it made (the persistent area: g_et, g_ht, g_vd), the stereo trigger's state; the rest the
 * features find again in the world as they do after any load (the player, the orcs, the schema holders once a
 * second, the settings file). */

uintptr_t somvr_img; /* the exe's base (the host has its own) */

/* a dev build's note in the init's RELOAD line: build/win/somvr_reload_note.h (scripts/reload35-drive.sh writes it, so a
 * reload of a real code change shows in the log; win/build.sh makes an empty one = none - always there, so the build's
 * object cache sees it change: b37) */
#include "somvr_reload_note.h"
#ifndef SOMVR_RELOAD_NOTE
#define SOMVR_RELOAD_NOTE ""
#endif

/* what one run learns once and a reloaded module must not do again (the old module hands it over at its stop): the
 * practice area's auto trip done (else a reload would take the player there again), the hit-stop's timer (one game
 * timer for the run, not one per module) */
struct run_facts {
	int pr_auto_done;
	uintptr_t hs_timer;
	/* SOMVR_EYE_SIZE: the game's last mode set (seen only at RES_3D_DESC; without it the new module's switch waits for
	 * a mode set it never sees and holds the eyes - boot 230725's first reload: WARN ESIZE eyes held) */
	int es_valid, es_target, es_cur;
	uint32_t es_w, es_h, es_fw, es_fh;
	uint64_t es_sets;
	/* SOMVR_PRACTICE: a trip there arrived (and no trip back since) and where the way back goes - else the new module
	 * refuses every `practice spawn` with "not in the practice area" (boot 230725's sword31 after the reloads: no orc) */
	int pr_went, pr_here, pr_have_back;
	float pr_back[3];
	/* ... its orc (else `practice remove` finds none and a second one is spawned beside it), the min health it set on
	 * Talion and the light it changed (both put back by the trip back) */
	uintptr_t pr_orc_h, pr_orc_npc, pr_orc_c, pr_mh_ch;
	int pr_orc_type, pr_orc_dead, pr_orc_checks, pr_cur_orc, pr_mh_on, pr_advanced, pr_weather_set;
	float pr_mh_old;
	double pr_orc_t;
	char pr_orig_weather[64], pr_orig_tod[32];
	/* b37: the Player the module last saw (else its first practice frame takes the live one for a load's new player:
	 * the area, the trip and our orc forgotten - boot 103436's every `practice spawn` after the first reload refused
	 * "not in the practice area"), the area's spot (a trip elsewhere moves it), the spawn manager we hold off and its
	 * flag before (else the trip back never gives the ambient AI back) */
	uintptr_t pr_pl, pr_quiet_mgr;
	int pr_quiet, pr_quiet_was;
	float pr_spot[3];
	unsigned char spare[228]; /* the block's size stays 512 bytes: a new field here keeps the old facts (a new size =
				   * a fresh block, the facts lost for one reload) */
};
_Static_assert(sizeof(struct run_facts) == 512, "run_facts: keep the persistent block's size (add fields in spare)");
static struct run_facts g_runf_own, *g_runf = &g_runf_own;

__declspec(dllexport) int somvr_logic_abi(void);
__declspec(dllexport) int somvr_logic_abi(void) { return SOMVR_LOGIC_ABI; }

/* the persistent blocks: a struct whose layout changed (a new size) starts fresh */
#define PERSIST(p, name)                                                                                               \
	do {                                                                                                           \
		int fresh_ = 0;                                                                                        \
		void *q_ = somvr_host_persist(name, sizeof *(p), &fresh_);                                             \
		if (q_) {                                                                                              \
			if (fresh_)                                                                                    \
				memcpy(q_, (p), sizeof *(p));                                                          \
			(p) = q_;                                                                                      \
		}                                                                                                      \
	} while (0)

/* the stand-in's reload test (SOMVR_WIN_TEST=1 + SOMVR_WIN_TEST_RELOAD=N; win/tests/d3dtest.c): this module hooks
 * d3dtest's f3 (an entry hook), f4's call of g (its rel32 through a stub page of ours, as the install paths do), the
 * slot d3dtest_tab[0] (f5) and f6 (a mid detour); each adds 100 x (gen + 1), the detour gen + 1 to the first argument.
 * SOMVR_TEST_NO_F3=1: no f3 hook in this generation (the host must hand f3 back to the original) */
static int (*g_rt_f3)(int, int);
static uintptr_t g_rt_g, g_rt_f5;
static int rt_f3(int a, int b) { return g_rt_f3(a, b) + 100 * (g_logic_gen + 1); }
static int rt_g(int a, int b) { return ((int (*)(int, int))g_rt_g)(a, b) + 100 * (g_logic_gen + 1); }
static int rt_f5(int a, int b) { return ((int (*)(int, int))g_rt_f5)(a, b) + 100 * (g_logic_gen + 1); }
static void rt_f6(struct somvr_win_regs *r) { r->rcx += (uint64_t)(g_logic_gen + 1); }
__declspec(dllimport) void *GetModuleHandleA(const char *name); /* kernel32 (somvr.c has no windows.h) */
__declspec(dllimport) void *GetProcAddress(void *mod, const char *name);
static void reload_selftest(void)
{
	void *exe = GetModuleHandleA(NULL);
	uintptr_t f3 = (uintptr_t)GetProcAddress(exe, "d3dtest_f3"), f4 = (uintptr_t)GetProcAddress(exe, "d3dtest_f4");
	uintptr_t g = (uintptr_t)GetProcAddress(exe, "d3dtest_g"), f6 = (uintptr_t)GetProcAddress(exe, "d3dtest_f6");
	uintptr_t tab = (uintptr_t)GetProcAddress(exe, "d3dtest_tab");
	char why[300] = "", err[200] = "";
	if (!f3 || !f4 || !g || !f6 || !tab) {
		somvr_win_log("RELOADTEST gen %d: the stand-in's functions not found", g_logic_gen);
		return;
	}
	int a = getenv("SOMVR_TEST_NO_F3") ? 1 : somvr_win_hook_entry(f3, (void *)rt_f3, (void **)&g_rt_f3, why, sizeof why);
	somvr_win_log("RELOADTEST gen %d entry f3: %s %s", g_logic_gen, a == 1 ? "not hooked (SOMVR_TEST_NO_F3)" : a ? "FAILED" : "ok", why);
	unsigned char bb[32];
	somvr_host_view(f4, bb, sizeof bb); /* an earlier module's patch shows the original call */
	uintptr_t site = 0;
	for (int i = 0; i + 5 <= (int)sizeof bb && !site; i++) {
		int32_t r32;
		memcpy(&r32, bb + i + 1, 4);
		if ((bb[i] == 0xe8 || bb[i] == 0xe9) && f4 + i + 5 + (intptr_t)r32 == g)
			site = f4 + i;
	}
	unsigned char *pg = site ? stub_alloc(site, site, 0) : NULL;
	int c = -1;
	if (pg) {
		uintptr_t to = (uintptr_t)rt_g;
		static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
		memcpy(pg, jmp, 6), memcpy(pg + 6, &to, 8);
		mprotect(pg, 4096, PROT_READ | PROT_EXEC);
		g_rt_g = g;
		int32_t rel = (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(site + 5));
		c = write_code(site + 1, &rel, 4, err, sizeof err);
	}
	somvr_win_log("RELOADTEST gen %d call f4->g at %#llx: %s %s", g_logic_gen, (unsigned long long)site,
		      !site ? "no call of g" : !pg ? "no stub page" : c ? "FAILED" : "ok", err);
	uintptr_t fn = (uintptr_t)rt_f5;
	somvr_host_view(tab, &g_rt_f5, 8);
	int t = write_code(tab, &fn, 8, err, sizeof err);
	somvr_win_log("RELOADTEST gen %d slot d3dtest_tab[0]: %s (the original %#llx) %s", g_logic_gen, t ? "FAILED" : "ok",
		      (unsigned long long)g_rt_f5, err);
	int m = somvr_win_hook_mid(f6, rt_f6, why, sizeof why);
	somvr_win_log("RELOADTEST gen %d mid f6: %s %s", g_logic_gen, m ? "FAILED" : "ok", why);
}

/* SOMVR_SLOTS: what the slots wrote into the game's items and must put back (the sword kept in the hand by its
 * InactiveNodes while the game has it sheathed, the sword kept away, the dagger in the hand: each item's saved
 * entries) and the player's slot state - a reloaded module without it sees no sword in the hand (boot 232217's sword31
 * after the reload: no TWOHAND grab) and could never restore the entries. Its own block: a layout change (a new size)
 * starts it fresh, logged by the host. */
struct sl_keep {
	int valid;
	int dag_on, dag_n;
	uintptr_t dag_item;
	uint32_t dag_saved[SL_MAX_ENTRIES];
	int sw_on, sw_n, sw_back;
	uintptr_t sw_item;
	uint32_t sw_saved[SL_MAX_ENTRIES], sw_act[SL_MAX_ENTRIES];
	int sw_away, aw_n;
	uintptr_t aw_item;
	uint32_t aw_saved[SL_MAX_ENTRIES], aw_inact[SL_MAX_ENTRIES];
	somvr_slots s;
};
static struct sl_keep g_slk_own, *g_slk = &g_slk_own;
#define SLK_COPY(to, from)                                                                                             \
	do {                                                                                                           \
		(to)->dag_on = (from)->dag_on, (to)->dag_n = (from)->dag_n, (to)->dag_item = (from)->dag_item;         \
		memcpy((to)->dag_saved, (from)->dag_saved, sizeof (to)->dag_saved);                                    \
		(to)->sw_on = (from)->sw_on, (to)->sw_n = (from)->sw_n, (to)->sw_back = (from)->sw_back;               \
		(to)->sw_item = (from)->sw_item;                                                                       \
		memcpy((to)->sw_saved, (from)->sw_saved, sizeof (to)->sw_saved);                                       \
		memcpy((to)->sw_act, (from)->sw_act, sizeof (to)->sw_act);                                             \
		(to)->sw_away = (from)->sw_away, (to)->aw_n = (from)->aw_n, (to)->aw_item = (from)->aw_item;           \
		memcpy((to)->aw_saved, (from)->aw_saved, sizeof (to)->aw_saved);                                       \
		memcpy((to)->aw_inact, (from)->aw_inact, sizeof (to)->aw_inact);                                       \
		(to)->s = (from)->s;                                                                                   \
	} while (0)

/* SOMVR_ARMS_LW=held: the weapon node's place in the hand, captured only while the sword is sheathed - a module loaded
 * with the sword drawn never captures it: no weight, no bite (boot 235031's sword31 after the reload: 0 swing frames) */
struct aw_keep {
	int valid;
	uintptr_t skel;
	sa_lw_hold hold[2];
};
static struct aw_keep g_awk_own, *g_awk = &g_awk_own;

/* ---- b37: where the view, Talion and the hands point - across a swap (docs/re-xr-pipeline.md 156.9) ---------------
 * The per-frame filters and offsets a module keeps decide them: the body moments' yaw offset (every turn of the game
 * camera absorbed - SOMVR_BODYMOMENTS' yaw `player`: a fresh module's 0 turned the view by all of them, 103 and 158 deg
 * at boot 022458's swaps after drivers that turned), the root anchor's filters and hand-off offset (the eye point:
 * 17.4 cm there), the eye-forward push, the size's state, the layer's snap / recentre requests seen (exactly: a snap held
 * for its blink is applied, not dropped), the turn not yet given to the rig, the body follow's move in flight (the game
 * took it, the rig not shifted yet), the hands' One Euro filters, the sword's weight and its body. Each module publishes
 * them at the end of every game-thread hook that changes them (H4, PM, the input tick: all on the game's main thread),
 * into persistent blocks of their own; a reloaded module adopts them at the start of its first such hook - the old
 * module's state after its very last frame (it renders ~0.8 s past its stop, through the new module's init). A block
 * whose layout changed (a new size: a struct edited by the patch) starts that part fresh, logged by the host. */
#define VK_PART(T, ...)                                                                                                \
	struct T {                                                                                                     \
		int valid;                                                                                             \
		__VA_ARGS__                                                                                            \
	}
VK_PART(vk_bm, __typeof__(g_bm.s) s; __typeof__(g_bm.o) o; uint64_t recenters;);
VK_PART(vk_lo, __typeof__(g_lo.a) a; __typeof__(g_lo.ao) ao; uint64_t a_frames; float anchor_pos[3];);
VK_PART(vk_hv, __typeof__(g_hv.s) s; __typeof__(g_hv.o) o; float pos[3]; uint64_t frames;);
VK_PART(vk_sz, __typeof__(g_sz.z) z; float eff, lift;);
VK_PART(vk_in, uint32_t seen_xr_recenter; int32_t seen_xr_snap; int pend_snap; float pend_turn;
	int fl_req_on, fl_on; uint64_t req_frame, fl_frame; sv_vec3 req_w, fl_req, fl_root0, fl_xf0, fl_xf1;
	uintptr_t pm, obj; int tid;);
VK_PART(vk_hd, __typeof__(g_hsm.arm) arm; __typeof__(g_hsm.inp) inp; int64_t arm_T; __typeof__(g_hsm.ws) ws;
	int64_t w_T; float w_prev_rot[3]; __typeof__(g_swp.st) st; int64_t swp_T;);
static struct {
	struct vk_bm *bm;
	struct vk_lo *lo;
	struct vk_hv *hv;
	struct vk_sz *sz;
	struct vk_in *in;
	struct vk_hd *hd;
	struct somvr_where *where;
	int ready;    /* the blocks are there (the init) */
	int adopted;  /* this module took the old one's (or it is the boot's: nothing to take) - then it publishes */
	int tid;      /* the game's main thread (the first hook that published); other threads neither take nor publish */
	char took[200];
} g_vk;
#define VK_BLOCK(f, name)                                                                                              \
	do {                                                                                                           \
		int fresh_ = 0;                                                                                        \
		g_vk.f = somvr_host_persist(name, sizeof *g_vk.f, &fresh_);                                            \
	} while (0)
static void vk_init(void)
{
	VK_BLOCK(bm, "vk.bodymoments");
	VK_BLOCK(lo, "vk.anchor");
	VK_BLOCK(hv, "vk.eyeoff");
	VK_BLOCK(sz, "vk.size");
	VK_BLOCK(in, "vk.input");
	VK_BLOCK(hd, "vk.hands");
	int fresh = 0;
	g_vk.where = somvr_host_persist(SOMVR_WHERE_BLOCK, sizeof *g_vk.where, &fresh);
	g_vk.ready = g_vk.bm && g_vk.lo && g_vk.hv && g_vk.sz && g_vk.in && g_vk.hd;
	g_vk.adopted = !g_logic_reason; /* the boot's module publishes from its first hook on */
}

/* the start of a game-thread hook (H4, PM, the input tick): a reloaded module's first one takes the old module's */
static void vk_enter(int h4)
{
	if (g_vk.adopted || !g_vk.ready)
		return;
	int tid = my_tid();
	if (g_vk.in->valid && g_vk.in->tid ? tid != g_vk.in->tid : !h4)
		return; /* not the game's main thread (the old module's publisher; else H4's): its first hook there takes them */
	g_vk.adopted = 1, g_vk.tid = tid;
	/* SOMVR_TEST_VK_SKIP=PART[,PART] (a test only, default none): those parts NOT taken - the host's check must then see
	 * the loss (`WARN RELOAD view moved`): bodymoments, anchor, eyeoff, size, input, hands */
	const char *skip = getenv("SOMVR_TEST_VK_SKIP");
#define VK_SKIP(name) (skip && *skip && strstr(skip, name))
	if (VK_SKIP("bodymoments"))
		g_vk.bm->valid = 0;
	if (VK_SKIP("anchor"))
		g_vk.lo->valid = 0;
	if (VK_SKIP("eyeoff"))
		g_vk.hv->valid = 0;
	if (VK_SKIP("size"))
		g_vk.sz->valid = 0;
	if (VK_SKIP("input"))
		g_vk.in->valid = 0;
	if (VK_SKIP("hands"))
		g_vk.hd->valid = 0;
#undef VK_SKIP
	float yaw_off = g_vk.bm->valid ? g_vk.bm->s.yaw_off * 57.29578f : 0,
	      lo_off = g_vk.lo->valid ? sqrtf(g_vk.lo->a.e.x * g_vk.lo->a.e.x + g_vk.lo->a.e.z * g_vk.lo->a.e.z) : 0;
	char *w = g_vk.took;
	size_t cap = sizeof g_vk.took, o = 0;
	w[0] = 0;
#define VK_TOOK(ok, name) (o += (size_t)snprintf(w + o, o < cap ? cap - o : 0, "%s%s%s", o ? ", " : "", (ok) ? "" : "NOT ", name))
	if (g_vk.bm->valid)
		g_bm.s = g_vk.bm->s, g_bm.o = g_vk.bm->o, g_bm.recenters = g_vk.bm->recenters;
	VK_TOOK(g_vk.bm->valid, "the body moments' yaw and hold");
	if (g_vk.lo->valid) {
		g_lo.a = g_vk.lo->a, g_lo.ao = g_vk.lo->ao, g_lo.a_frames = g_vk.lo->a_frames;
		memcpy(g_lo.anchor_pos, g_vk.lo->anchor_pos, sizeof g_lo.anchor_pos);
	}
	VK_TOOK(g_vk.lo->valid, "the anchor's filters");
	if (g_vk.hv->valid) {
		g_hv.s = g_vk.hv->s, g_hv.o = g_vk.hv->o, g_hv.frames = g_vk.hv->frames;
		memcpy(g_hv.pos, g_vk.hv->pos, sizeof g_hv.pos);
	}
	VK_TOOK(g_vk.hv->valid, "the eye-forward push");
	if (g_vk.sz->valid)
		g_sz.z = g_vk.sz->z, g_sz.eff = g_vk.sz->eff, g_sz.lift = g_vk.sz->lift;
	VK_TOOK(g_vk.sz->valid, "the size");
	struct vk_in *in = g_vk.in;
	if (in->valid) {
		g_seen_xr_recenter = in->seen_xr_recenter, g_seen_xr_snap = in->seen_xr_snap;
		g_inp.pend_snap = in->pend_snap, g_inp.pend_turn = in->pend_turn;
		if (g_follow.on) {
			g_follow.req = in->fl_req_on, g_follow.fl = in->fl_on;
			g_follow.req_frame = in->req_frame, g_follow.fl_frame = in->fl_frame, g_follow.req_w = in->req_w;
			g_follow.fl_req = in->fl_req, g_follow.fl_root0 = in->fl_root0, g_follow.fl_xf0 = in->fl_xf0;
			g_follow.fl_xf1 = in->fl_xf1, g_follow.pm = in->pm, g_follow.obj = in->obj, g_follow.tid = in->tid;
		}
	}
	VK_TOOK(in->valid, "the requests seen, the turn pending, the follow's move");
	struct vk_hd *hd = g_vk.hd;
	if (hd->valid) {
		memcpy(g_hsm.arm, hd->arm, sizeof g_hsm.arm), memcpy(g_hsm.inp, hd->inp, sizeof g_hsm.inp);
		g_hsm.arm_T = hd->arm_T, g_hsm.ws = hd->ws, g_hsm.w_T = hd->w_T;
		memcpy(g_hsm.w_prev_rot, hd->w_prev_rot, sizeof g_hsm.w_prev_rot);
		g_swp.st = hd->st, g_swp.T = hd->swp_T;
	}
	VK_TOOK(hd->valid, "the hands' filters, the sword's weight and body");
#undef VK_TOOK
	logf_("RELOAD gen %d took the old module's view state at its first game-thread hook (frame %llu): %s (the body "
	      "moments' yaw offset %.2f deg, the anchor's hand-off offset %.1f cm)%s%s", g_logic_gen,
	      (unsigned long long)g_vk.where->frame, g_vk.took, yaw_off, lo_off, skip && *skip ? "; SOMVR_TEST_VK_SKIP=" : "",
	      skip && *skip ? skip : "");
}

/* the end of a game-thread hook: publish (a module that took the old one's, or the boot's) */
static void vk_leave(int h4)
{
	if (!g_vk.adopted || !g_vk.ready)
		return;
	int tid = my_tid();
	if (!g_vk.tid && h4)
		g_vk.tid = tid; /* the game's main thread: H4's (the renderer's frame) */
	if (!g_vk.tid || tid != g_vk.tid)
		return;
	g_vk.bm->s = g_bm.s, g_vk.bm->o = g_bm.o, g_vk.bm->recenters = g_bm.recenters, g_vk.bm->valid = g_bm.on;
	g_vk.lo->a = g_lo.a, g_vk.lo->ao = g_lo.ao, g_vk.lo->a_frames = g_lo.a_frames;
	memcpy(g_vk.lo->anchor_pos, g_lo.anchor_pos, sizeof g_lo.anchor_pos);
	g_vk.lo->valid = g_lo.anchor;
	g_vk.hv->s = g_hv.s, g_vk.hv->o = g_hv.o, g_vk.hv->frames = g_hv.frames, g_vk.hv->valid = g_hv.on;
	memcpy(g_vk.hv->pos, g_hv.pos, sizeof g_hv.pos);
	g_vk.sz->z = g_sz.z, g_vk.sz->eff = g_sz.eff, g_vk.sz->lift = g_sz.lift, g_vk.sz->valid = 1;
	struct vk_in *in = g_vk.in;
	in->seen_xr_recenter = g_seen_xr_recenter, in->seen_xr_snap = g_seen_xr_snap;
	in->pend_snap = g_inp.pend_snap, in->pend_turn = g_inp.pend_turn;
	in->fl_req_on = g_follow.req, in->fl_on = g_follow.fl, in->req_frame = g_follow.req_frame;
	in->fl_frame = g_follow.fl_frame, in->req_w = g_follow.req_w, in->fl_req = g_follow.fl_req;
	in->fl_root0 = g_follow.fl_root0, in->fl_xf0 = g_follow.fl_xf0, in->fl_xf1 = g_follow.fl_xf1;
	in->pm = g_follow.pm, in->obj = g_follow.obj, in->tid = tid, in->valid = 1;
	struct vk_hd *hd = g_vk.hd;
	memcpy(hd->arm, g_hsm.arm, sizeof hd->arm), memcpy(hd->inp, g_hsm.inp, sizeof hd->inp);
	hd->arm_T = g_hsm.arm_T, hd->ws = g_hsm.ws, hd->w_T = g_hsm.w_T;
	memcpy(hd->w_prev_rot, g_hsm.w_prev_rot, sizeof hd->w_prev_rot);
	hd->st = g_swp.st, hd->swp_T = g_swp.T, hd->valid = 1;
}

/* H4's end, a stereo frame whose eyes were set up: the frame's mapping and Talion for the host's check of a swap */
static void vk_where(void)
{
	struct somvr_where *w = g_vk.where;
	if (!w || !g_vk.adopted || !g_fr.have_game || my_tid() != g_vk.tid)
		return;
	float s = g_rig.scale > 0 ? g_rig.scale : 1.0f, yaw = sv_wrap_pi(g_fr.base.yaw - g_rig.yaw0);
	sv_vec3 rp = sv_qrot(sv_qyaw(yaw), g_rig.p0);
	float tp[3] = { 0, 0, 0 }, q[4], ty = 0;
	uintptr_t pm, obj;
	int ht = player_objs(g_fr.pl, &pm, &obj) && peek(obj + OBJ_POS, tp, sizeof tp) && vec_ok(tp, 1e7f) &&
		 peek(obj + OBJ_ROT, q, sizeof q) && vec_ok(q, 2.0f);
	if (ht)
		ty = sv_heading(sv_qnormalize(sv_quat_of(q)));
	somvr_seq_wbegin(&w->seq);
	w->frame = g_fr.frame, w->gen = g_logic_gen, w->have_talion = ht;
	w->o[0] = g_fr.base.pos.x - s * rp.x, w->o[1] = g_fr.base.pos.y - s * rp.y, w->o[2] = g_fr.base.pos.z - s * rp.z;
	w->yaw = yaw, w->scale = s;
	w->base[0] = g_fr.base.pos.x, w->base[1] = g_fr.base.pos.y, w->base[2] = g_fr.base.pos.z, w->psi = g_fr.base.yaw;
	memcpy(w->talion, tp, sizeof w->talion), w->talion_yaw = ty;
	w->t = mono();
	somvr_seq_wend(&w->seq);
}

__declspec(dllexport) int somvr_logic_init(const struct somvr_logic_boot *b);
__declspec(dllexport) int somvr_logic_init(const struct somvr_logic_boot *b)
{
	g_logic_reason = b->reason, g_logic_gen = b->gen;
	somvr_img = b->img;
	PERSIST(g_et_p, "eyes.textures");
	PERSIST(g_ht_p, "hud.texture");
	PERSIST(g_vd_p, "hands.depth");
	PERSIST(g_rig_p, "rig");
	PERSIST(g_slk, "slots.keep");
	PERSIST(g_awk, "arms.lw");
	vk_init(); /* b37: the view's state blocks (taken at this module's first game-thread hook when a reload) */
	int runf_fresh = 0;
	{
		void *q = somvr_host_persist("run.facts", sizeof *g_runf, &runf_fresh);
		if (q)
			g_runf = q;
	}
	if (b->sha && !b->test)
		somvr_win_exe_tag(b->sha); /* the tables' exe: its sites and globals */
	int blank = 0, com = 0, live = somvr_win_sites_apply(b->sites_off ? 0 : b->img, &blank, &com, b->sites_off);
	extern int somvr_win_sysv_blanked;
	somvr_win_log("SITES live=%d blank=%d (%d of them: a live row but a SysV-register WRAPPER hook, not ported) com=%d%s "
		      "(win/sites-*.tsv; blank = no Windows site: that feature stays off); logic gen %d%s",
		      live, blank, somvr_win_sysv_blanked, com,
		      b->sites_off ? (b->test ? " the stand-in: all blank" : " SOMVR_WIN_SITES=0: all blank") : "", b->gen,
		      b->reason ? " (a reload, in the world)" : "");
	int rc = somvr_win_start(b->who, !b->test);
	if (b->test && getenv("SOMVR_WIN_TEST_RELOAD"))
		reload_selftest();
	if (b->reason) {
		if (g_runf->pr_auto_done || runf_fresh) /* (a fresh block at a reload: an older layout's - the trip assumed done) */
			g_pr.auto_done = 1;
		if (g_slk->valid && g_sl.on)
			SLK_COPY(&g_sl, g_slk);
		if (g_awk->valid)
			g_aw.skel = g_awk->skel, g_aw.s[0].hold = g_awk->hold[0], g_aw.s[1].hold = g_awk->hold[1];
		/* the requests the layer posts in the shared objects (they outlive a module): this module's "seen" copies
		 * start at what the old one saw - else the first frame repeats every snap turn and recentre of the run (boot
		 * 232217: the view turned 73-101 deg at the swaps after a driver) and the menu's last practice request, the
		 * Report's mark, the Quit */
		g_seen_xr_recenter = atomic_load(&g_sh.xr.recenter_seq), g_seen_xr_snap = atomic_load(&g_sh.xr.snap_steps);
		g_ss.report_seen = atomic_load(&g_ssn.report_req), g_ss.quit_seen = atomic_load(&g_ssn.quit_req);
		g_pr.menu_seen = somvr_vm_pr_seq(atomic_load(&g_vmo.practice));
		if (g_runf->pr_went) {
			g_pr.went = 1, g_pr.here = g_runf->pr_here, g_pr.have_back = g_runf->pr_have_back;
			memcpy(g_pr.back, g_runf->pr_back, sizeof g_pr.back);
		}
		g_pr.orc_h = g_runf->pr_orc_h, g_pr.orc_npc = g_runf->pr_orc_npc, g_pr.orc_c = g_runf->pr_orc_c;
		g_pr.orc_type = g_runf->pr_orc_type, g_pr.orc_dead = g_runf->pr_orc_dead, g_pr.orc_checks = g_runf->pr_orc_checks;
		g_pr.orc_t = g_runf->pr_orc_t, g_pr.cur_orc = g_runf->pr_cur_orc;
		g_pr.mh_on = g_runf->pr_mh_on, g_pr.mh_ch = g_runf->pr_mh_ch, g_pr.mh_old = g_runf->pr_mh_old;
		g_pr.advanced = g_runf->pr_advanced, g_pr.weather_set = g_runf->pr_weather_set;
		memcpy(g_pr.orig_weather, g_runf->pr_orig_weather, sizeof g_pr.orig_weather);
		memcpy(g_pr.orig_tod, g_runf->pr_orig_tod, sizeof g_pr.orig_tod);
		if (g_runf->pr_pl) { /* b37 */
			g_pr.pl = g_runf->pr_pl, g_pr.quiet = g_runf->pr_quiet, g_pr.quiet_was = g_runf->pr_quiet_was;
			g_pr.quiet_mgr = g_runf->pr_quiet_mgr;
			memcpy(g_pr.spot, g_runf->pr_spot, sizeof g_pr.spot);
		}
		if (!g_cb.hs.mine)
			g_cb.hs.mine = g_runf->hs_timer;
		if (g_runf->es_valid) {
			atomic_store(&g_es.target, g_runf->es_target), atomic_store(&g_es.cur_w, g_runf->es_w);
			atomic_store(&g_es.cur_h, g_runf->es_h), atomic_store(&g_es.flat_w, g_runf->es_fw);
			atomic_store(&g_es.flat_h, g_runf->es_fh), atomic_store(&g_es.sets, g_runf->es_sets);
			atomic_store(&g_es.cur, g_runf->es_cur);
		}
		logf_("RELOAD gen %d: this module's init rc=%d (the switches re-read; kept: the rig's recentre %s, snap steps %d; "
		      "the practice's auto trip %s, in its area %d (the player %s, the ambient AI %s); the slots' keeps: the sword in the hand %d, away %d, the dagger %d; "
		      "the arms' held weapon node R %d L %d; "
		      "the hit-stop timer %#llx; the eye size's mode %d at %ux%u, the game's %ux%u); "
		      "the swap follows: its hooks go live at once; note '%s'",
		      b->gen, rc, g_rig.have_ref ? "set" : "not set", (int)g_rig.snap_steps,
		      g_pr.auto_done ? "done" : "not yet", g_pr.here, g_pr.pl ? "kept" : "not known", g_pr.quiet ? "held off" : "as the game has it",
		      g_sl.sw_on, g_sl.sw_away, g_sl.dag_on,
		      g_aw.s[SA_R].hold.have, g_aw.s[SA_L].hold.have,
		      (unsigned long long)g_cb.hs.mine, atomic_load(&g_es.cur),
		      atomic_load(&g_es.cur_w), atomic_load(&g_es.cur_h), atomic_load(&g_es.flat_w), atomic_load(&g_es.flat_h),
		      SOMVR_RELOAD_NOTE);
	}
	flush_log();
	return rc;
}

__declspec(dllexport) int somvr_logic_com(const char *iface, void **vtbl);
__declspec(dllexport) int somvr_logic_com(const char *iface, void **vtbl)
{
	int n, done = 0, tried = 0;
	const struct somvr_win_com *r = somvr_win_com_rows(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(r[i].iface, iface)) {
			tried++;
			done += somvr_win_com_fill(r[i].name, vtbl) == 0;
		}
	somvr_win_log("COM %s vtable %p: %d of %d rows filled%s", iface, (void *)vtbl, done, tried,
		      g_logic_gen ? " (a reload: the host's replay)" : "");
	return done;
}

__declspec(dllexport) void somvr_logic_present(void);
__declspec(dllexport) void somvr_logic_present(void)
{
	somvr_core_win_com(); /* winport-core: H8's vtables from the D3D_* COM rows */
	somvr_win_late_h9();  /* the D3D sites again, H9 */
}

/* quiet: nothing of the game held in a state only this module knows how to end - an orc's timer frozen by the
 * hit-stop, a kill or a held sync playing, a slow motion of ours, a hand on a wall, an orc in the hand, a mount */
/* (the reload thread: th_attacker answers on the game thread only - its fields read as they are, a yes/no) */
static int th_busy(double t)
{
	if (g_th.on)
		for (int i = 0; i < TH_TRACKS; i++)
			if (g_th.tr[i].c && !g_th.tr[i].dormant && !g_th.tr[i].k.ranged)
				return 1;
	return (g_th.att_t > 0 && t - g_th.att_t <= 2.0) || (g_hu.flash_t > 0 && t - g_hu.flash_t <= 2.0);
}

__declspec(dllexport) int somvr_logic_idle(char *why, size_t cap);
__declspec(dllexport) int somvr_logic_idle(char *why, size_t cap)
{
	double t = mono();
	const char *w = NULL;
	int kills = 0;
	for (int i = 0; i < KL_N; i++)
		kills += g_kl.k[i].on != 0;
	if (g_cb.hs.on)
		w = "the hit-stop holds an orc's timer";
	else if (kills || g_kl.sy.on || g_kl.pl.on)
		w = "a kill is playing";
	else if (g_tk.active) /* (on: the switch; active: a hold under way) */
		w = "a sync holds Talion";
	else if (g_xc.smo_until > t || g_fi.smo_until > t)
		w = "our slow motion runs";
	else if (g_cl.s.active)
		w = g_cl.s.phase == SOMVR_CLIMB_PH_FLING ? "a climb's fling is in the air"
		    : g_cl.s.phase == SOMVR_CLIMB_PH_HANG  ? "Talion hangs in a climb (no gravity)"
		    : g_cl.s.phase == SOMVR_CLIMB_PH_BLINK ? "a climb's descent blink runs"
							   : "a hand holds a wall";
	else if (g_vb.held_c)
		w = "a hand holds an orc";
	else if (g_mt.state != MT_OFF)
		w = "riding";
	/* b37 (docs 156.9): a calm moment - no attack at Talion now or in the last 2 s, no hit taken in the last 2 s (boot
	 * 115759: the swaps in the fights moved Talion 5-9 cm; a fight's short states - the recoil, the parry's window, the
	 * spacing - are live there). The player's Apply comes from the menu at a calm moment anyway (the card waits too) */
	else if (th_busy(t))
		w = "a fight at Talion (an attack now or in the last 2 s, or a hit taken)";
	/* b37 (docs 156.9): what our own code moves or holds of the view and the sword */
	else if (g_bm.on && (g_bm.o.frozen || g_bm.s.ease || g_bm.s.blink))
		w = "a body moment holds the view";
	else if (g_swp.st.wound || g_swp.st.pend)
		w = "the sword is in a wound";
	else if (g_bow.on && g_bow.s.phase != SOMVR_BOW_PH_IDLE) /* (aiming holds the game's aim: a swap turned Talion 80 deg) */
		w = g_bow.s.phase == SOMVR_BOW_PH_DRAW ? "the bow is drawn" : "the bow is out (aiming)";
	else if (g_pr.trip.phase != SOMVR_PRT_IDLE)
		w = "a practice trip runs";
	else if (ov_active())
		w = "a vault holds or flies";
	else if (jp_exact())
		w = "a jump's arc runs";
	else if (g_step.run.kind != SOMVR_ST_NONE)
		w = "a step, dash or phase runs";
	else if (sk_away())
		w = "the Shadow Strike holds or moves the view";
	else if (g_cf.held || g_cf.alpha > 0)
		w = "a snap turn's blink runs";
	if (w)
		snprintf(why, cap, "%s", w);
	return !w;
}

/* phase 0 (before the new module's init): the writer thread ended and joined, the VR menu's settings saved now, the
 * log flushed. phase 1 (after the swap): retired - its last line, no END lines at the exit (somvr_fini), the log
 * file left (a thread still inside this module writes into its buffer only). phase 2 (b37): the swap called off after
 * phase 0 (the game busy again at the swap and not quiet in time): this module runs on - its writer thread again */
__declspec(dllexport) void somvr_logic_stop(int phase);
__declspec(dllexport) void somvr_logic_stop(int phase)
{
	if (phase == 2) {
		atomic_store(&g_stop, 0);
		pthread_t th;
		int rc = g_writer_on ? 0 : pthread_create(&th, NULL, writer_main, NULL);
		if (!g_writer_on && rc == 0)
			g_writer_th = th, g_writer_on = 1;
		logf_("RELOAD gen %d resumed: the swap was called off - this module runs on (its writer thread %s)", g_logic_gen,
		      rc == 0 ? "started again" : "NOT started again");
		flush_log();
		return;
	}
	if (phase == 0) {
		atomic_store(&g_stop, 1);
		if (g_writer_on)
			pthread_join(g_writer_th, NULL), g_writer_on = 0;
		vm_save_tick(mono(), 1, 1);
		SLK_COPY(g_slk, &g_sl);
		g_slk->valid = g_sl.on;
		g_awk->skel = g_aw.skel, g_awk->hold[0] = g_aw.s[0].hold, g_awk->hold[1] = g_aw.s[1].hold, g_awk->valid = 1;
		g_runf->pr_auto_done |= g_pr.auto_done;
		g_runf->pr_went = g_pr.went, g_runf->pr_here = g_pr.here, g_runf->pr_have_back = g_pr.have_back;
		memcpy(g_runf->pr_back, g_pr.back, sizeof g_runf->pr_back);
		g_runf->pr_orc_h = g_pr.orc_h, g_runf->pr_orc_npc = g_pr.orc_npc, g_runf->pr_orc_c = g_pr.orc_c;
		g_runf->pr_orc_type = g_pr.orc_type, g_runf->pr_orc_dead = g_pr.orc_dead, g_runf->pr_orc_checks = g_pr.orc_checks;
		g_runf->pr_orc_t = g_pr.orc_t, g_runf->pr_cur_orc = g_pr.cur_orc;
		g_runf->pr_mh_on = g_pr.mh_on, g_runf->pr_mh_ch = g_pr.mh_ch, g_runf->pr_mh_old = g_pr.mh_old;
		g_runf->pr_advanced = g_pr.advanced, g_runf->pr_weather_set = g_pr.weather_set;
		_Static_assert(sizeof g_runf->pr_orig_weather == sizeof g_pr.orig_weather && sizeof g_runf->pr_orig_tod ==
			       sizeof g_pr.orig_tod, "the practice's light strings");
		memcpy(g_runf->pr_orig_weather, g_pr.orig_weather, sizeof g_runf->pr_orig_weather);
		memcpy(g_runf->pr_orig_tod, g_pr.orig_tod, sizeof g_runf->pr_orig_tod);
		g_runf->pr_pl = g_pr.pl, g_runf->pr_quiet = g_pr.quiet, g_runf->pr_quiet_was = g_pr.quiet_was;
		g_runf->pr_quiet_mgr = g_pr.quiet_mgr;
		_Static_assert(sizeof g_runf->pr_spot == sizeof g_pr.spot, "the practice spot");
		memcpy(g_runf->pr_spot, g_pr.spot, sizeof g_runf->pr_spot);
		g_runf->es_valid = atomic_load(&g_es.cur) >= 0, g_runf->es_target = atomic_load(&g_es.target);
		g_runf->es_cur = atomic_load(&g_es.cur), g_runf->es_w = atomic_load(&g_es.cur_w);
		g_runf->es_h = atomic_load(&g_es.cur_h), g_runf->es_fw = atomic_load(&g_es.flat_w);
		g_runf->es_fh = atomic_load(&g_es.flat_h), g_runf->es_sets = atomic_load(&g_es.sets);
		if (g_cb.hs.mine)
			g_runf->hs_timer = g_cb.hs.mine;
		logf_("RELOAD gen %d stopping: the writer thread joined (the next module's init follows)", g_logic_gen);
		flush_log();
		return;
	}
	logf_("RELOAD gen %d retired: the next module is live (this one stays mapped, never called again)", g_logic_gen);
	flush_log();
	g_active = 0;
	g_fd = -1;
}

__declspec(dllexport) int somvr_logic_abitest(char *out, size_t cap);
__declspec(dllexport) int somvr_logic_abitest(char *out, size_t cap) { return somvr_win_abi_test(out, cap); }
