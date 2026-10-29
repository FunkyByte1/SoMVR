/* somvr_screens_game.c - build 22 (som-b22-screens; docs/re-xr-pipeline.md 90, handoff/HANDOFF-drill.md "build 22"): the
 * game side of somvr_screens.h; included at the end of somvr.c. Two things:
 *
 * 1. SOMVR_BM_DEATH=first (code default off = build 21): the death in first person. bm_pre asks scr_bm() each H4 frame
 *    after the body moments' policy: while the death holds and the game runs, the window (the game camera) is dropped;
 *    the pause after it (the Nemesis screens) opens the window (the theatre) although the game is paused; a death hold's
 *    release outside the window re-seats with a slow blink. The current presentation (PlayerCameraPresentation +0x90)
 *    is logged as it changes while dying (what the game shows; nothing acts on it).
 * 2. SOMVR_POINTER_MOUSE1=nemesis|all (code default off = build 21): ptr_tick asks scr_ptr() each input tick; while the
 *    pointer's click is held in a game menu and the player UI's top menu layer is the Nemesis hierarchy
 *    (CUIHierarchyLayer, its vtable checked at start: slot 10 = 0xedf470, the layer's command handler) - or in every
 *    menu (all) - the command Mouse1 (100) is held (in_apply's ON / OFF events). That command is the layer's MenuEnter on
 *    the selected captain (vf_10: 100 -> the 54 branch; run 20261028-232819: held, nothing picked). The pick under the
 *    mouse is the hierarchy manager's (FUN_00a07730: the values of MouseAxisX/Y 104/105 moved -> FUN_009fe470: a ray
 *    from the stored mouse pixel through the game camera, FUN_00dcbd00 along it, FUN_009fe1d0 selects) - KBM only: its
 *    first call is the pad check FUN_00a38e80. So while the pointer is on the board over the hierarchy: SCR_NEM_PAD (the
 *    patched call 0x9fe483) answers "not the pad" and the two axis commands carry the pointer's uv (scr_axis_wants,
 *    in_apply: values only): the captain under the laser or the fingertip is selected as the mouse's would be, and the
 *    press (Mouse1) opens his card. Each click logs the menu stack (the state, the layers' vtables).
 * The test word `screens status|death first|game|mouse1 off|nemesis|all`. */

#define SCR_VT_HIER 0x35b8da8UL     /* CUIHierarchyLayer's vtable (reference/index/classes.tsv) */
#define SCR_VT_HIER_VF10 0xedf470UL /* its slot 10: CUIHierarchyLayer::vf_10, the command handler (Mouse1 = 100) */
#define SCR_P_UI 0x2e0              /* Player: the CPlayerUI (re-ui-pointer.md 1.2: FUN_00a0f760 -> FUN_00acbca0) */
#define SCR_UI_SM 0xb0              /* CPlayerUI: its CPlayerUIStateMachine */
#define SCR_SM_STATE 0xc            /* the state (-1: the one at +0x8) */
#define SCR_SM_STATE0 0x8
#define SCR_SM_MGR 0x10             /* state 5 (the pause menus): CUIMenuManagerBase embedded here (FUN_00acbd10) */
#define SCR_MGR_LAYERS 0x198        /* its layer stack: 8 pointers */
#define SCR_MGR_COUNT 0x1e8
#define SCR_MGR_OVERRIDE 0x1ec
#define SCR_CMD_MOUSE1 100
#define SCR_CMD_MAXIS_X 104         /* MouseAxisX / MouseAxisY: the hierarchy manager's mouse move (FUN_00a07730) */
#define SCR_CMD_MAXIS_Y 105

/* SCREENS_SITES_BEGIN (check_sites.py counts this block on its own: the patched pad check of the hierarchy's mouse
 * pick, FUN_009fe470's first call) */
static const struct site_spec g_scr_sites_game[] = {
SITE("SCR_NEM_PAD", K_CALL, 0x9fe483, 0x9fe47f, "48 8b 7f 18 e8 f8 a9 03 00 84 c0 75 1b", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
};
/* SCREENS_SITES_END */
static const struct site_spec *g_scr_sites = g_scr_sites_game;
static struct hook g_scr_h = { .name = "SCR_NEM_PAD" };

static struct {
	somvr_sd_cfg sd_cfg;
	somvr_sd sd;
	somvr_bm_cfg slow;           /* the body moments' cfg with the death's slow blink */
	int sd_bad;
	uintptr_t pres_last;         /* the presentation seen last while dying */
	int pm1, pm1_bad, hier_ok;   /* SOMVR_POINTER_MOUSE1, the vtable check */
	int m1_on;                   /* Mouse1 held this tick */
	uint64_t m1_ticks, m1_presses, clicks_seen, nemesis_clicks, m1_skipped;
	int ui_state, ui_n, ui_top_hier;
	uintptr_t ui_vt[8];
	_Atomic int nem_live;        /* the pointer on the board over the hierarchy now: SCR_NEM_PAD answers "not the pad" */
	int hover_on;                /* the axis values are ours this tick */
	float axis[2];               /* the values given (the pointer's uv) */
	uint64_t hover_ticks, pad_calls, pad_kbm, picks_seen;
	uint64_t grey_world;         /* stereo frames whose death grey was the world's only (SOMVR_REC_FADE_WORLD) */
	int paused_now;              /* the game timer's pause at the last H4 frame (scr_bm) */
	int lines;
} g_scr;
static _Atomic int g_scr_req_death = -1, g_scr_req_m1 = -1, g_scr_req_status;

static int scr_log_ok(void) { return g_scr.lines++ < 400; }

/* the player UI's menu stack: the state, the layer count and vtables; 1 = state 5 read */
static int scr_ui_stack(uintptr_t P)
{
	g_scr.ui_state = -1, g_scr.ui_n = 0, g_scr.ui_top_hier = 0;
	uintptr_t ui = P ? peek_ptr(P + SCR_P_UI) : 0, sm = ui ? peek_ptr(ui + SCR_UI_SM) : 0;
	int32_t st = -1;
	if (!sm || !peek(sm + SCR_SM_STATE, &st, sizeof st))
		return 0;
	if (st == -1 && !peek(sm + SCR_SM_STATE0, &st, sizeof st))
		return 0;
	g_scr.ui_state = st;
	if (st != 5)
		return 0;
	uintptr_t mgr = sm + SCR_SM_MGR;
	uint32_t n = 0;
	uint8_t ovr = 0;
	if (!peek(mgr + SCR_MGR_COUNT, &n, sizeof n) || !peek(mgr + SCR_MGR_OVERRIDE, &ovr, 1))
		return 0;
	int top = somvr_scr_top_index(n);
	g_scr.ui_n = top + 1;
	for (int i = 0; i <= top; i++) {
		uintptr_t l = peek_ptr(mgr + SCR_MGR_LAYERS + 8 * (uintptr_t)i);
		g_scr.ui_vt[i] = l ? peek_ptr(l) : 0;
	}
	g_scr.ui_top_hier = top >= 0 && !ovr && g_scr.hier_ok && g_scr.ui_vt[top] == SCR_VT_HIER;
	return 1;
}

static const char *scr_stack_str(char *out, size_t cap)
{
	size_t o = (size_t)snprintf(out, cap, "the player UI state %d", g_scr.ui_state);
	if (g_scr.ui_state == 5) {
		o += (size_t)snprintf(out + o, cap - o, ", %d layer(s):", g_scr.ui_n);
		for (int i = 0; i < g_scr.ui_n && o + 24 < cap; i++)
			o += (size_t)snprintf(out + o, cap - o, " %#lx%s", (unsigned long)g_scr.ui_vt[i],
					      g_scr.ui_vt[i] == SCR_VT_HIER ? " (CUIHierarchyLayer)" : "");
	}
	return out;
}

/* ptr_tick's end (the input tick): the click held now, a game menu up with the pointer on it */
static void scr_ptr(sc_tick_out *o, int act, int on, int click_held, int new_click, double t)
{
	int rq = atomic_exchange(&g_scr_req_m1, -1);
	if (rq >= 0 && rq != g_scr.pm1) {
		logf_("SCREENS t=%.3f the pointer's Mouse1: %s -> %s (the test word)", t, somvr_pm1_names[g_scr.pm1],
		      somvr_pm1_names[rq]);
		g_scr.pm1 = rq;
	}
	if (atomic_exchange(&g_scr_req_status, 0) || (new_click && act)) {
		uintptr_t sh = peek_ptr(g_ing.shell_pp), P = sh && g_ptr.ok_pl ? LP_FN(LP_LOCALPLAYER, lp_player_fn)(sh) : 0;
		scr_ui_stack(P);
		char sb[400];
		g_scr.clicks_seen += new_click != 0, g_scr.nemesis_clicks += new_click && g_scr.ui_top_hier;
		if (scr_log_ok())
			logf_("SCREENS t=%.3f %s: %s; the top layer %s the Nemesis hierarchy; Mouse1 %s", t,
			      new_click ? "a pointer click" : "status", scr_stack_str(sb, sizeof sb), g_scr.ui_top_hier ? "is" : "is not",
			      somvr_pm1_names[g_scr.pm1]);
	} else if ((click_held || on) && act && g_scr.pm1 && !(g_ptr.ticks & 3)) {
		uintptr_t sh = peek_ptr(g_ing.shell_pp), P = sh && g_ptr.ok_pl ? LP_FN(LP_LOCALPLAYER, lp_player_fn)(sh) : 0;
		scr_ui_stack(P); /* the pointer on the board: the stack now and then (the hierarchy on top: the hover) */
	}
	/* the hover: the pointer on the board over the hierarchy - the axes carry its uv, the pick's pad check says KBM */
	int hover = g_scr.pm1 && g_scr_h.installed && act && on && g_scr.ui_top_hier;
	if (hover) {
		somvr_ptr_pub p;
		if (somvr_ptr_read(&g_in, &p) && isfinite(p.u) && isfinite(p.v))
			g_scr.axis[0] = p.u, g_scr.axis[1] = p.v;
		g_scr.hover_ticks++;
	}
	if (hover != g_scr.hover_on && scr_log_ok())
		logf_("SCREENS t=%.3f the Nemesis hover %s (the pointer %s the hierarchy; MouseAxisX/Y = its uv %.3f,%.3f)", t,
		      hover ? "on" : "off", hover ? "on" : "off", g_scr.axis[0], g_scr.axis[1]);
	g_scr.hover_on = hover;
	atomic_store_explicit(&g_scr.nem_live, hover, memory_order_release);
	int want = somvr_pm1_want(g_scr.pm1, click_held, act, g_scr.ui_top_hier);
	if (want && o->ncmd >= SC_OUT_CMDS)
		want = 0, g_scr.m1_skipped++;
	if (want) {
		o->cmd[o->ncmd++] = SCR_CMD_MOUSE1;
		g_scr.m1_ticks++;
	}
	if (want != g_scr.m1_on) {
		g_scr.m1_presses += want != 0;
		if (scr_log_ok())
			logf_("SCREENS t=%.3f the game's Mouse1 (command %d) %s at %d,%d (%s; %llu so far)", t, SCR_CMD_MOUSE1,
			      want ? "held" : "let go", g_ptr.pos[0], g_ptr.pos[1], g_scr.pm1 == SOMVR_PM1_ALL ? "every menu" :
			      "the Nemesis hierarchy on top", (unsigned long long)g_scr.m1_presses);
	}
	g_scr.m1_on = want;
}

/* bm_pre, after the policy and before the step: the death's rule. Returns the paused flag for the step; *cfg = the
 * step's config (the slow blink while dying) */
static int scr_bm(uint32_t conds, int paused, int *w, int *wc, uintptr_t pres, double t, const somvr_bm_cfg **cfg)
{
	int rq = atomic_exchange(&g_scr_req_death, -1);
	if (rq >= 0 && rq != g_scr.sd_cfg.on) {
		logf_("SCREENS t=%.3f the death: %s -> %s (the test word)", t, g_scr.sd_cfg.on ? "first" : "game",
		      rq ? "first" : "game");
		g_scr.sd_cfg.on = rq;
	}
	*cfg = &g_bm.cfg;
	g_scr.paused_now = paused;
	if (!g_scr.sd_cfg.on && g_scr.sd.phase == SOMVR_SD_IDLE)
		return paused;
	int death = (conds >> SOMVR_FB_DEATH) & 1, w0 = *w;
	somvr_sd_out so;
	somvr_sd_step(&g_scr.sd, &g_scr.sd_cfg, death, *w, *wc, paused, g_bm.s.win,
		      g_bm.s.frozen || g_bm.s.blink != SOMVR_BMB_IDLE, SOMVR_FB_DEATH, t, &so);
	*w = so.win, *wc = so.win_cond;
	if (so.slow_blink) {
		g_scr.slow = g_bm.cfg;
		g_scr.slow.blink_out_s = g_scr.sd_cfg.blink_out_s, g_scr.slow.blink_black_s = g_scr.sd_cfg.blink_black_s;
		g_scr.slow.blink_in_s = g_scr.sd_cfg.blink_in_s;
		*cfg = &g_scr.slow;
	}
	if (so.changed && scr_log_ok())
		logf_("SCREENS t=%.3f the death: %s (deaths %llu, the Nemesis screens %llu, respawns %llu, alive again %llu; the "
		      "game's window dropped %llu frame(s))%s", t, somvr_sd_phase_names[g_scr.sd.phase % 3],
		      (unsigned long long)g_scr.sd.deaths, (unsigned long long)g_scr.sd.nemesis,
		      (unsigned long long)g_scr.sd.respawns, (unsigned long long)g_scr.sd.recovered,
		      (unsigned long long)g_scr.sd.dropped_frames, g_scr.sd.phase == SOMVR_SD_DYING ?
		      ": first person, greyed, no game camera" : g_scr.sd.phase == SOMVR_SD_NEMESIS ?
		      ": the window (the Nemesis screens on the theatre)" : "");
	if (g_scr.sd.phase != SOMVR_SD_IDLE && pres != g_scr.pres_last) {
		char nm[SOMVR_FB_TAGLEN + 16];
		if (scr_log_ok())
			logf_("SCREENS t=%.3f the death: the presentation %#lx -> %#lx ('%s') paused=%d; the window %s", t,
			      (unsigned long)g_scr.pres_last, (unsigned long)pres, fb_rec_name(pres, nm, sizeof nm), paused,
			      w0 && !so.win ? "asked for and dropped (first person)" : so.win ? "on" : "off");
		g_scr.pres_last = pres;
	} else if (g_scr.sd.phase == SOMVR_SD_IDLE) {
		g_scr.pres_last = 0;
	}
	return so.step_paused;
}

/* in_apply: the hover's axis values (commands MouseAxisX/Y, values only); the count */
static int scr_axis_wants(int cmd[2], float val[2])
{
	if (!g_scr.hover_on)
		return 0;
	cmd[0] = SCR_CMD_MAXIS_X, cmd[1] = SCR_CMD_MAXIS_Y, val[0] = g_scr.axis[0], val[1] = g_scr.axis[1];
	return 2;
}

/* SCR_NEM_PAD (FUN_009fe470's FUN_00a38e80(P) = "the profile uses the pad"): 0 while the hover is on, else the game's */
__attribute__((used)) void somvr_scrpad_c(struct regs *r);
void somvr_w_scrpad(void);
WRAPPER("somvr_w_scrpad", "somvr_scrpad_c");
__attribute__((used)) void somvr_scrpad_c(struct regs *r)
{
	atomic_fetch_add_explicit(&g_scr_h.calls, 1, memory_order_relaxed);
	if (atomic_load_explicit(&g_scr.nem_live, memory_order_acquire)) {
		r->rax = 0;
		atomic_fetch_add_explicit(&g_scr_h.mods, 1, memory_order_relaxed);
	} else {
		r->rax = ((lp_padmode_fn)g_scr_h.orig)((void *)r->rdi);
	}
}

/* the stub page for the call (as sl_install's), the rel32 written; 1 = in */
static int scr_install(const struct site_spec *s)
{
	char why[512] = "", err[200] = "", nb[64];
	int ok = s->kind == K_CALL && check_site(s, why, sizeof why) == 0;
	if (s->kind != K_CALL)
		snprintf(why, sizeof why, "not a call site");
	unsigned char *pg = NULL;
	for (int i = 0; ok && i < 64 + 4 && !pg; i++) {
		static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
		uintptr_t at = i < 64 ? (uintptr_t)0x3f0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
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
		uintptr_t tgt = (uintptr_t)somvr_w_scrpad;
		memcpy(pg, jmp, 6);
		memcpy(pg + 6, &tgt, 8);
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			ok = 0, snprintf(why, sizeof why, "stub mprotect rx: %s", strerror(errno));
	}
	g_scr_h.orig = s->target; /* before the call can reach somvr_scrpad_c */
	int32_t rel = ok ? (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(s->addr + 5)) : 0;
	if (!ok || write_code(s->addr + 1, &rel, 4, err, sizeof err) != 0) {
		logf_("HOOK %s SKIPPED %#lx: %s%s", s->name, (unsigned long)s->addr, ok ? "write failed: " : "", ok ? err : why);
		return 0;
	}
	g_scr_h.installed = 1;
	hex_of((const unsigned char *)s->addr, 5, nb, sizeof nb);
	logf_("HOOK %s ok call %#lx orig=[%s] target=%#lx now=[%s] via=%#lx", s->name, (unsigned long)s->addr, why,
	      (unsigned long)s->target, nb, (unsigned long)pg);
	return 1;
}

/* bm_frame: the record's fade is the death's grey (dying, its level above the blink): the world only, under the HUD -
 * the killer's name and line, our hands and the VR menu stay clear (SOMVR_REC_FADE_WORLD; the layer's XL_MBD slot) */
static int scr_fade_world(void)
{
	if (!g_scr.sd_cfg.on || g_scr.sd.phase != SOMVR_SD_DYING || g_bm.o.act != SOMVR_BMA_GREY)
		return 0;
	if (!(g_bm.s.level > 0) || g_bm.s.level < g_bm.s.blink_alpha)
		return 0;
	g_scr.grey_world++;
	return 1;
}

/* ss_tick: the session's flat frames are the window's while dying in the game's pause (the Nemesis screens before the
 * window's own debounce: run 20261029-004550 put their first 0.3 s on the 2.4 m load panel, `theatre 2: a loading
 * screen`, then the 4 m theatre) */
static int scr_mono_window(void)
{
	return g_scr.sd_cfg.on && g_scr.sd.phase == SOMVR_SD_DYING && g_scr.paused_now;
}

/* somvr_start, after bm_start and ptr_start */
static void scr_start(int *bad)
{
	memset(&g_scr, 0, sizeof g_scr);
	somvr_sd_init(&g_scr.sd);
	const char *dv = getenv("SOMVR_BM_DEATH"), *mv = getenv("SOMVR_POINTER_MOUSE1");
	if (somvr_sd_parse(dv, &g_scr.sd_cfg)) {
		*bad = 1;
		logf_("WARN SOMVR_BM_DEATH=%s is not first|game[,OUT_MS,BLACK_MS,IN_MS]: game (build 21)", dv);
	}
	if (g_scr.sd_cfg.on && !g_bm.on) {
		logf_("WARN SOMVR_BM_DEATH=first needs SOMVR_BODYMOMENTS=1: the game's death presentation as before");
		g_scr.sd_cfg.on = 0, *bad = 1;
	}
	g_scr.pm1 = somvr_pm1_parse(mv, &g_scr.pm1_bad);
	if (g_scr.pm1_bad) {
		*bad = 1;
		logf_("WARN SOMVR_POINTER_MOUSE1=%s is not off|nemesis|all: off", mv);
	}
	uintptr_t vf = 0;
	g_scr.hier_ok = peek(SCR_VT_HIER + 10 * 8, &vf, sizeof vf) && vf == SCR_VT_HIER_VF10;
	if (g_scr.pm1 && (!g_ptr.on || !g_inp.inject || !g_scr.hier_ok)) {
		logf_("WARN SOMVR_POINTER_MOUSE1=%s: %s: off", somvr_pm1_names[g_scr.pm1], !g_ptr.on ? "needs SOMVR_POINTER" :
		      !g_inp.inject ? "needs SOMVR_INPUT's command events" : "CUIHierarchyLayer's vtable does not match");
		g_scr.pm1 = SOMVR_PM1_OFF, *bad = 1;
	}
	if (g_scr.pm1 && !scr_install(&g_scr_sites[0])) {
		logf_("WARN SOMVR_POINTER_MOUSE1=%s: SCR_NEM_PAD is not in: the press opens the selected captain, the pointer "
		      "picks none", somvr_pm1_names[g_scr.pm1]);
		*bad = 1;
	}
	if (g_scr.sd_cfg.on || dv)
		logf_("CONFIG screens death %s (SOMVR_BM_DEATH): %s; a death hold's release outside the window: a blink %.0f / "
		      "%.0f / %.0f ms", g_scr.sd_cfg.on ? "first" : "game", g_scr.sd_cfg.on ?
		      "the death in first person - no window while dying and the game runs, the Nemesis screens' pause opens it "
		      "(the theatre), the respawn back through the fade" : "the game's death presentation in the window (build "
		      "21)", g_scr.sd_cfg.blink_out_s * 1000, g_scr.sd_cfg.blink_black_s * 1000, g_scr.sd_cfg.blink_in_s * 1000);
	if (g_scr.pm1 || mv)
		logf_("CONFIG screens pointer Mouse1 %s (SOMVR_POINTER_MOUSE1): %s; CUIHierarchyLayer's vtable %#lx %s",
		      somvr_pm1_names[g_scr.pm1], g_scr.pm1 == SOMVR_PM1_ALL ? "every pointer click in a game menu also holds the "
		      "command Mouse1" : g_scr.pm1 ? "a pointer click on the Nemesis screens (the top menu layer the hierarchy) "
		      "also holds the command Mouse1 (the selected captain's card), its point over the hierarchy selects the "
		      "captain under it (SCR_NEM_PAD, MouseAxisX/Y)" : "the pointer drives the mouse only",
		      SCR_VT_HIER, g_scr.hier_ok ? "checked" : "NOT matched");
}

/* the trigger word "screens ..." (the writer thread) */
static const char *scr_word(const char *w)
{
	char a[16] = "", b[16] = "";
	if (sscanf(w, "screens %15s %15s", a, b) < 1)
		return "screens (not status|death first|game|mouse1 off|nemesis|all)";
	if (!strcmp(a, "status")) {
		atomic_store(&g_scr_req_status, 1);
		return "screens status";
	}
	if (!strcmp(a, "death") && (!strcmp(b, "first") || !strcmp(b, "game"))) {
		atomic_store(&g_scr_req_death, b[0] == 'f');
		return b[0] == 'f' ? "screens death first" : "screens death game";
	}
	if (!strcmp(a, "mouse1")) {
		int bad = 0, m = somvr_pm1_parse(b, &bad);
		if (!bad && (m == SOMVR_PM1_OFF || g_scr.hier_ok)) {
			atomic_store(&g_scr_req_m1, m);
			return m == SOMVR_PM1_ALL ? "screens mouse1 all" : m ? "screens mouse1 nemesis" : "screens mouse1 off";
		}
	}
	return "screens (not status|death first|game|mouse1 off|nemesis|all)";
}

static void scr_end_log(void)
{
	if (!g_scr.sd_cfg.on && !g_scr.sd.deaths && !g_scr.pm1 && !g_scr.m1_presses)
		return;
	logf_("SCREENS END death %s: deaths %llu, the Nemesis screens %llu (opened in the pause %llu), respawns %llu, alive "
	      "again %llu, the game's window dropped %llu frame(s); Mouse1 %s: pointer clicks %llu (on the hierarchy %llu), "
	      "Mouse1 held %llu time(s) for %llu tick(s), skipped %llu; the hover %llu tick(s), SCR_NEM_PAD %llu/%llu "
	      "(calls/answered KBM); the death's grey under the HUD %llu frame(s)", g_scr.sd_cfg.on ? "first" : "game",
	      (unsigned long long)g_scr.sd.deaths, (unsigned long long)g_scr.sd.nemesis,
	      (unsigned long long)g_scr.sd.paused_opens, (unsigned long long)g_scr.sd.respawns,
	      (unsigned long long)g_scr.sd.recovered, (unsigned long long)g_scr.sd.dropped_frames, somvr_pm1_names[g_scr.pm1],
	      (unsigned long long)g_scr.clicks_seen, (unsigned long long)g_scr.nemesis_clicks,
	      (unsigned long long)g_scr.m1_presses, (unsigned long long)g_scr.m1_ticks, (unsigned long long)g_scr.m1_skipped,
	      (unsigned long long)g_scr.hover_ticks, (unsigned long long)atomic_load(&g_scr_h.calls),
	      (unsigned long long)atomic_load(&g_scr_h.mods), (unsigned long long)g_scr.grey_world);
}
