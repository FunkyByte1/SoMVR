/* somvr_cues_game.c - build 37 (som-b37-cues, docs/re-xr-pipeline.md 169, handoff/HANDOFF-cues.md): what the hidden flat
 * prompts told the player, the VR way. Included by somvr.c after the knife's file (it uses the threat's glows, the stealth
 * judge's awareness read, SOMVR_USE's pick prompts and press, the herbs' hand rule, the nav markers' walk). The pure part:
 * somvr_cues.c/h. Build 36 (SOMVR_GAME_PROMPTS=off) hid every prompt and world icon; these give back what they said:
 *
 *   SOMVR_CUE_AWARE=on|off (code default off)  a small soft triangle over an orc's head, its point down at him, from the
 *       game's own AI alert state (the stealth judge's read): white = it noticed something, amber = it looks for Talion,
 *       red = it has seen him (fights him); in 0.25 s, out 0.5 s, red auto-hidden after 3 s (the flat game hides its red
 *       icon too); never on a dead orc; only within SOMVR_CUE_AWARE_M (40) and the view's 60 deg. Drawn in the threat
 *       cues' glow slots (the strongest three of the frame: an attack's glow still wins). A soft double tick in both hands
 *       when an orc within 15 m turns amber or red. The alarm runner (an orc whose action is the alarm's: its record
 *       matches SOMVR_CUE_RUNNER_RECS) gets a pulsing red mark that never auto-hides, and a warm glow at his chest.
 *       _KINDS (the level per alert state 0..7: 0,1,2,3,3,0,0,0), _UP_CM (the mark over the root: 125), _RED_S (3).
 *       SOMVR_CUE_AWARE_TICK=on|off (code default off): the double tick without our marks (the user chose the game's own
 *       triangles, 11-04: they show on their orcs with the layer's SOMVR_XR_HUD_EYE - docs 169.8).
 *   SOMVR_CUE_ALERT=on|off (code default off)  high alert: an orc within 60 m reacts to a raised area alarm (its action
 *       '*ReactToAlarm_High*') - the watch's rim red (the HUD tag's SOMVR_HUDF_ALARM; the layer draws it) while orcs
 *       within 60 m stay aware, at most SOMVR_CUE_ALERT_S (60) after the last such reaction.
 *   SOMVR_CUE_USE=on|off (code default off)  every thing the game offers a pick for ('Combat_Activate_*', SOMVR_USE's kept
 *       prompts; the herbs keep SOMVR_HERBS' eat) glows where it is - the nearest nav marker of its kind within 4 m
 *       (somvr_cu_use_recs: artifacts, Ithildin, the forge tower's stone, arrows, the camp's things) or, none known, at
 *       the hand reached low near Talion - faint while the game offers it, full with a tick when an open, free hand is
 *       within SOMVR_CUE_USE_REACH_CM (45); the grip there does it: the game's own pick (SOMVR_USE presses 123). B stays.
 *   SOMVR_CUE_TIMER=on|off (code default off)  the mission timer's seconds left from the game's own HUD timer tick (CU_TIMER:
 *       the call of the timer's text update, FUN_00b3f680(timer, left), patched); the HUD tag carries SOMVR_HUDF_TIMER (a
 *       timer runs) and SOMVR_HUDF_TIMER_POP (the layer shows its line at the gaze: 4 s at the start, 4 s at 30 s left,
 *       from 10 s left to the end); the watch shows it always (SOMVR_XR_WRIST_RECTS, the layer).
 *   SOMVR_CUE_ARC=on|off (code default off)  a captain's health arc over him while he fights Talion (the threat slots'
 *       arc row, the health in 16 steps).
 * Words (live): `cues status|probe [S]|aware on|off|use on|off|timer on|off|arc on|off|alert on|off`; test: `cues rim [S]`
 * (high alert forced for S s: the watch's rim seen headless). Lines: `CONFIG cues ...`, `CUES
 * aware ...` (each change of an orc's level), `CUES probe ...` (test: every orc within 40 m, 4 a second), `CUES use ...`,
 * `CUES timer ...`, `CUES alert ...`, `CUES arc ...`, `CUES END ...`. */

/* CUES_SITES_BEGIN (check_sites.py counts this block on its own: the patched call of the HUD timer's text update in its
 * tick FUN_00b3fd00 = mov %rbx,%rdi; call FUN_00b3f680(float left %xmm0, timer %rdi) - Windows: (timer %rcx, left %xmm1),
 * the same C signature (uintptr_t, float) on both) */
static SITE_TABLE struct site_spec g_cu_sites_game[] = {
SITE("CU_TIMER", K_CALL, 0xb3fdc1, 0xb3fdae, "00 00 e8 8b 2c cc 01 0f 2f 05 cc 88 80 02 76 22 48 89 df e8 ba f8 ff ff", 0xb3f680, "53 48 89 fb 48 83 ec 30 4c 8b 05 11 49 02 04"),
};
/* CUES_SITES_END */
enum { CUS_TIMER, CUS_N };
_Static_assert(sizeof g_cu_sites_game / sizeof g_cu_sites_game[0] == CUS_N, "cues sites");
static const struct site_spec *g_cu_sites = g_cu_sites_game; /* selftest: a stand-in */
typedef void (*cu_timer_fn)(uintptr_t timer, float left);

#define CU_ORCS 24
#define CU_PROBE_N 16
struct cu_orc {
	uintptr_t c, atk_rec;
	somvr_cu_aw_mark m;
	int state, engaged, runner, alarm;
	double seen_t;
	float meter;
	char atk[56];
};
static struct {
	int aware, use, timer, arc, alert, tid;
	int tick; /* SOMVR_CUE_AWARE_TICK: the double tick at amber/red - on its own (the marks may be the game's: 169.8) */
	somvr_cu_aw_cfg aw;
	float range_cm, up_cm, view_cos, tick_cm, use_reach_cm, alert_s;
	char runner[160];
	struct cu_orc orc[CU_ORCS];
	double last_t, tick_t, alarm_t, alarm_last_aware_t, probe_until, probe_next, hud_t;
	int alarm_on, runner_n;
	/* the frame's arc (SOMVR_CUE_ARC) */
	uintptr_t arc_c;
	float arc_hp, arc_max;
	/* the use cue */
	struct { uintptr_t mk; float pos[3]; char name[40]; } u[8];
	int un, ucue_i, ucue_was, uclosed[2], uok_pos;
	double uscan_t, uline_t;
	char uprompt[64];
	float ucue_pos[3], ucue_level, ucue_dist;
	int ucue_ring;
	int uground; /* build 40: the cue's place a guess (at the hand, low; ahead of the chest): no outline, the glow stays */
	/* the pick's feedback (build 37, the user's herb: "I didn't know how I triggered it"): Talion's attack 'Activate_*'
	 * begun by any control (B or the hand) - a buzz in both hands; an herb's heal: the green flash on both edges */
	uintptr_t pk_rec;
	char pk_name[48];
	double heal_t0, pk_t;
	float pk_hp;
	int pk_heal, pk_hp_pending;
	uint64_t picks_seen, heals;
	uint64_t ucue_frame;
	/* the timer */
	uintptr_t tm_orig;
	int tm_hooked;
	_Atomic uint32_t tm_left_bits;
	_Atomic uint64_t tm_ticks;
	_Atomic double tm_tick_t;
	uint64_t tm_seen;
	somvr_cu_tm_cfg tmc;
	somvr_cu_tm tm;
	int tm_pop;
	uint32_t hud_flags;
	/* counts */
	uint64_t plines; /* the probe's lines (test) */
	uint64_t frames, changes, marks_drawn, ticks2, runners, use_cues, use_ticks, use_takes, tm_events[5], alarms, arcs, lines;
	_Atomic int req_probe, req_rim;
	double rim_until; /* test: `cues rim S` - high alert's rim forced for S s (the layer's ring seen headless) */
} g_cu;
static struct log_late g_cu_late;
static int cu_log_ok(void) { return g_cu.lines++ < 500 || log_late_ok(&g_cu_late, 120, mono()); }
static const char *const g_cu_lvl_w[] = { "none", "white", "amber", "red" };

/* the timer's tick (the UI's thread; patched CU_TIMER): the seconds left, then the game's own update */
__attribute__((used)) void somvr_w_cutimer(uintptr_t timer, float left)
{
	if (isfinite(left)) {
		uint32_t b;
		memcpy(&b, &left, sizeof b);
		atomic_store_explicit(&g_cu.tm_left_bits, b, memory_order_relaxed);
		atomic_store_explicit(&g_cu.tm_tick_t, mono(), memory_order_relaxed);
		atomic_fetch_add_explicit(&g_cu.tm_ticks, 1, memory_order_release);
	}
	((cu_timer_fn)g_cu.tm_orig)(timer, left);
}

static void cu_start(int *bad)
{
	memset(&g_cu, 0, sizeof g_cu);
	const char *const ow[] = { "off", "on" };
	g_cu.aware = env_word("SOMVR_CUE_AWARE", ow, 2, 0);
	g_cu.use = env_word("SOMVR_CUE_USE", ow, 2, 0);
	g_cu.timer = env_word("SOMVR_CUE_TIMER", ow, 2, 0);
	g_cu.arc = env_word("SOMVR_CUE_ARC", ow, 2, 0);
	g_cu.alert = env_word("SOMVR_CUE_ALERT", ow, 2, 0);
	g_cu.tick = env_word("SOMVR_CUE_AWARE_TICK", ow, 2, 0);
	somvr_cu_aw_defaults(&g_cu.aw);
	const char *k = getenv("SOMVR_CUE_AWARE_KINDS");
	if (k && *k && !somvr_cu_aw_parse_kinds(k, &g_cu.aw))
		logf_("WARN SOMVR_CUE_AWARE_KINDS=%s is not N,N,.. (0..3, at most %d): the defaults", k, SOMVR_CU_KINDS), ++*bad;
	g_cu.range_cm = 100 * env_float("SOMVR_CUE_AWARE_M", 40, 5, 120, bad);
	g_cu.up_cm = env_float("SOMVR_CUE_AWARE_UP_CM", 125, 40, 250, bad);
	g_cu.aw.red_s = env_float("SOMVR_CUE_AWARE_RED_S", 3, 0, 60, bad);
	g_cu.view_cos = cosf(0.017453293f * env_float("SOMVR_CUE_AWARE_VIEW_DEG", 60, 20, 180, bad));
	g_cu.tick_cm = 100 * env_float("SOMVR_CUE_AWARE_TICK_M", 15, 0, 60, bad);
	g_cu.use_reach_cm = env_float("SOMVR_CUE_USE_REACH_CM", 45, 10, 100, bad);
	g_cu.alert_s = env_float("SOMVR_CUE_ALERT_S", 60, 5, 600, bad);
	const char *r = getenv("SOMVR_CUE_RUNNER_RECS");
	snprintf(g_cu.runner, sizeof g_cu.runner, "%s", r && *r ? r : "*RaiseAlarm*,*RunForAlarm*,Activate_Horn*,*Horn_*");
	somvr_cu_tm_defaults(&g_cu.tmc);
	g_cu.ucue_i = -1;
	if (!g_us.on)
		g_cu.use = 0;
	char why[200];
	g_cu.uok_pos = check_site(&g_prn_sites[PRN_NAV_POS], why, sizeof why) == 0;
	if (g_cu.timer) {
		g_cu.tm_orig = g_cu_sites[CUS_TIMER].target;
		int ok = check_site(&g_cu_sites[CUS_TIMER], why, sizeof why) == 0;
		g_cu.tm_hooked = ok && bo_patch_call(&g_cu_sites[CUS_TIMER], (void *)somvr_w_cutimer);
		logf_("CUES site CU_TIMER %s %#llx: %s%s", ok ? "ok" : "MISMATCH", (unsigned long long)g_cu_sites[CUS_TIMER].addr, why,
		      ok && !g_cu.tm_hooked ? " (not patched)" : "");
		if (!g_cu.tm_hooked)
			logf_("WARN SOMVR_CUE_TIMER=on: the HUD timer's tick not hooked - no timer popups (the watch still shows it)");
	}
	logf_("CONFIG cues aware=%s (SOMVR_CUE_AWARE: the mark over an orc from its alert state, levels by state %d,%d,%d,%d,%d,%d,%d,%d "
	      "(0 none 1 white 2 amber 3 red), within %.0f m and %.0f deg of the view, %.0f cm over its root, in %.2f s out %.2f s, "
	      "red hidden after %.1f s; the runner '%s'), alert=%s (%.0f s), use=%s (SOMVR_CUE_USE: reach %.0f cm, places %s), "
	      "timer=%s (%s), arc=%s; the double tick at amber/red %s (SOMVR_CUE_AWARE_TICK)", ow[g_cu.aware], g_cu.aw.lvl[0], g_cu.aw.lvl[1], g_cu.aw.lvl[2], g_cu.aw.lvl[3], g_cu.aw.lvl[4],
	      g_cu.aw.lvl[5], g_cu.aw.lvl[6], g_cu.aw.lvl[7], g_cu.range_cm / 100, acosf(g_cu.view_cos) * 57.29578f, g_cu.up_cm,
	      g_cu.aw.in_s, g_cu.aw.out_s, g_cu.aw.red_s, g_cu.runner, ow[g_cu.alert], g_cu.alert_s, ow[g_cu.use],
	      g_cu.use_reach_cm, g_cu.uok_pos ? "by PRN_NAV_POS" : "the hand's reach only (PRN_NAV_POS not matched)",
	      ow[g_cu.timer], !g_cu.timer ? "-" : g_cu.tm_hooked ? "CU_TIMER hooked" : "NOT hooked", ow[g_cu.arc],
	      g_cu.aware || g_cu.tick ? "on" : "off");
}

static struct cu_orc *cu_orc_of(uintptr_t c, double t)
{
	struct cu_orc *old = NULL;
	for (int i = 0; i < CU_ORCS; i++) {
		if (g_cu.orc[i].c == c)
			return &g_cu.orc[i];
		if (!old || g_cu.orc[i].seen_t < old->seen_t)
			old = &g_cu.orc[i];
	}
	if (old->c && t - old->seen_t < 0.5)
		return NULL; /* all in use this frame */
	memset(old, 0, sizeof *old);
	old->c = c, old->state = -1;
	return old;
}

/* the orc's current action's name (cached by record) */
static const char *cu_atk(struct cu_orc *k)
{
	uintptr_t rec = peek_ptr(k->c + TH_C_ATTACK);
	if (rec != k->atk_rec) {
		k->atk_rec = rec, k->atk[0] = 0;
		if (rec)
			rx_rec_name(rec, k->atk, sizeof k->atk);
	}
	return k->atk;
}

/* a Nemesis captain (SOMVR_CAPTAIN's members), his max health */
static int cu_captain(uintptr_t c) { return g_kp.on && kp_of_combat(c) != 0; }
static float cu_health_max(uintptr_t c)
{
	uintptr_t own = peek_ptr(c + C_OWNER);
	float mx = 0;
	return own && peek(own + PR_CH_HEALTH_MAX, &mx, sizeof mx) && isfinite(mx) ? mx : 0;
}

static float cu_glow_size_m(const struct th_glow *g)
{
	float d = g->dist / 100.0f / somvr_rig_scale(&g_rig);
	if (g->kind & SOMVR_THK_ARC) {
		float s = 2.0f * d * tanf(0.5f * 4.0f * 0.017453293f);
		return s < 0.25f ? 0.25f : s > 1.4f ? 1.4f : s;
	}
	return somvr_cu_aw_size(&g_cu.aw, d);
}

/* th_frame (the game thread, a stereo frame): the awareness marks, the runner, high alert, the arc, the use glow */
static void cu_glow_add(uintptr_t pcomb, uintptr_t pc, const float root[3], const float head[3], const float hq[4], double t,
			struct th_glow *gl, int *ng)
{
	if (!g_cu.tid)
		g_cu.tid = my_tid();
	if (g_cu.tid != my_tid())
		return;
	float dt = g_cu.last_t > 0 && t > g_cu.last_t && t - g_cu.last_t < 0.5 ? (float)(t - g_cu.last_t) : 0;
	g_cu.last_t = t;
	g_cu.frames++;
	int probe = atomic_exchange(&g_cu.req_probe, 0);
	if (probe > 0)
		g_cu.probe_until = t + probe, g_cu.probe_next = 0;
	int want = g_cu.aware || g_cu.tick || g_cu.alert || g_cu.arc || g_cu.probe_until > t || atomic_load(&g_cu.req_rim) || t < g_cu.rim_until;
	if (!want || !pcomb)
		return;
	sv_vec3 f = sv_qrot(sv_qnormalize((sv_quat){ hq[0], hq[1], hq[2], hq[3] }), (sv_vec3){ 0, 0, 1 }); /* (the game's +Z: sv_heading's forward) */
	float fwd[3] = { f.x, f.y, f.z };
	uintptr_t me = pc ? peek_ptr(pc + C_HOBJ) : 0;
	struct cb_orc o[16];
	int n = cb_orcs_r(pcomb, pc, root, g_cu.range_cm > 6000 ? g_cu.range_cm : 6000, o, 16);
	int any_aware = 0, rose = 0, plog = g_cu.probe_until > t && t >= g_cu.probe_next;
	float arc_best = INFINITY;
	uintptr_t arc_c = 0;
	if (plog)
		g_cu.probe_next = t + 0.25;
	for (int i = 0; i < n; i++) {
		if (mt_is_mount(o[i].hobj))
			continue;
		struct cu_orc *k = cu_orc_of(o[i].c, t);
		if (!k)
			continue;
		k->seen_t = t;
		struct stl_aw a;
		int dead = cb_dead(o[i].c);
		stl_aware(&o[i], me, &a);
		k->state = a.kind, k->engaged = a.engaged, k->meter = a.meter;
		const char *atk = cu_atk(k);
		int runner = atk[0] && somvr_cu_match(g_cu.runner, atk), alarm = atk[0] && strstr(atk, "ReactToAlarm_High") != NULL;
		if (runner && !k->runner && cu_log_ok())
			g_cu.runners++, logf_("CUES aware t=%.3f orc %#llx at %.0f cm: the alarm's action '%s' - the runner's mark", t,
					      (unsigned long long)o[i].c, o[i].dist, atk);
		if (alarm && !k->alarm) {
			g_cu.alarms++, g_cu.alarm_t = t;
			if (cu_log_ok())
				logf_("CUES alert t=%.3f orc %#llx at %.0f cm reacts to a raised alarm ('%s')%s", t,
				      (unsigned long long)o[i].c, o[i].dist, atk, g_cu.alert ? ": high alert - the watch's rim red" : "");
		}
		k->runner = runner, k->alarm = alarm;
		int lvl = o[i].dist <= g_cu.range_cm ? somvr_cu_aw_level(&g_cu.aw, a.kind, a.engaged, dead) : SOMVR_CU_NONE;
		if (runner && !dead)
			lvl = SOMVR_CU_RED;
		any_aware |= lvl >= SOMVR_CU_AMBER && o[i].dist <= 6000;
		float mp[3] = { o[i].pos[0], o[i].pos[1] + g_cu.up_cm, o[i].pos[2] }, d[3] = { mp[0] - head[0], mp[1] - head[1], mp[2] - head[2] };
		float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		int vis = dl > 1 && (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / dl >= g_cu.view_cos;
		int was = k->m.lvl;
		if (runner) /* the runner's mark never auto-hides: as a fresh red each frame */
			k->m.red_at = t;
		if (somvr_cu_aw_step(&g_cu.aw, &k->m, lvl, vis, t, dt)) {
			g_cu.changes++;
			if (lvl > was && lvl >= SOMVR_CU_AMBER && o[i].dist <= g_cu.tick_cm)
				rose = 1;
			if (cu_log_ok())
				logf_("CUES aware t=%.3f orc %#llx at %.0f cm: %s -> %s (the game's alert state %d, +0xeb8 %.3f, fights Talion %d, "
				      "dead %d; its action '%s')%s", t, (unsigned long long)o[i].c, o[i].dist, g_cu_lvl_w[was], g_cu_lvl_w[lvl],
				      a.kind, (double)a.meter, a.engaged, dead, atk, !g_cu.aware ? " (SOMVR_CUE_AWARE off: not drawn)"
				      : vis ? "" : " (out of view: not drawn)");
		}
		if (plog && o[i].dist <= g_cu.range_cm && g_cu.plines++ < 3000) /* (its own budget: the others' lines stay) */
			logf_("CUES probe t=%.3f orc %#llx %.0f cm: state %d +0xeb8 %.3f fights %d dead %d action '%s' -> %s (shown %s %.2f)%s", t,
			      (unsigned long long)o[i].c, o[i].dist, a.kind, (double)a.meter, a.engaged, dead, atk, g_cu_lvl_w[lvl],
			      g_cu_lvl_w[k->m.shown], (double)k->m.fade, vis ? "" : " out of view");
		if (g_cu.aware && k->m.fade > 0.02f && k->m.shown > 0) {
			float base = k->m.shown == SOMVR_CU_WHITE ? 0.75f : k->m.shown == SOMVR_CU_AMBER ? 0.85f : 0.95f;
			if (runner)
				base = 0.75f + 0.25f * (0.5f + 0.5f * cosf(6.2831853f * 2.0f * (float)t));
			uint32_t kind = SOMVR_THK_MARK | (k->m.shown == SOMVR_CU_AMBER ? SOMVR_THK_MARK_AMBER
							  : k->m.shown == SOMVR_CU_RED ? SOMVR_THK_MARK_RED : 0u);
			th_glow_add(gl, ng, mp, base * k->m.fade, dl, kind);
			g_cu.marks_drawn++;
			if (runner) { /* and a warm glow at his chest */
				float ch[3] = { o[i].pos[0], o[i].pos[1] + 30, o[i].pos[2] };
				th_glow_add(gl, ng, ch, 0.8f * base, dist3(ch, head), SOMVR_THK_MELEE | SOMVR_THK_RED);
			}
		}
		/* the arc: the nearest live captain in a fight with Talion (his alert state 3/4: the attack target is Talion only
		 * during each blow, boot 20261104-205033) */
		if (g_cu.arc && !dead && (a.engaged || a.kind == 3 || a.kind == 4) && o[i].dist < arc_best && o[i].dist < 1500 && cu_captain(o[i].c))
			arc_best = o[i].dist, arc_c = o[i].c;
	}
	if (rose && (g_cu.aware || g_cu.tick) && t - g_cu.tick_t > 2.0) { /* a soft double tick in both hands */
		g_cu.tick_t = t, g_cu.ticks2++;
		for (int h = 0; h < 2; h++)
			haptic_play(h, SOMVR_HX_TICK, 2, 1);
	}
	/* high alert */
	if (any_aware)
		g_cu.alarm_last_aware_t = t;
	int rr = atomic_exchange(&g_cu.req_rim, 0);
	if (rr > 0)
		g_cu.rim_until = t + rr;
	int alarm = (g_cu.alarm_t > 0 && t - g_cu.alarm_t < g_cu.alert_s && t - g_cu.alarm_last_aware_t < 10) || t < g_cu.rim_until;
	if (alarm != g_cu.alarm_on && cu_log_ok())
		logf_("CUES alert t=%.3f high alert %s (the last reaction to the alarm %.1f s ago, an aware orc within 60 m %.1f s ago)%s",
		      t, alarm ? "on" : "off", g_cu.alarm_t > 0 ? t - g_cu.alarm_t : -1.0, t - g_cu.alarm_last_aware_t,
		      g_cu.alert ? "" : " (SOMVR_CUE_ALERT off: no rim)");
	g_cu.alarm_on = alarm;
	/* the arc */
	if (arc_c != g_cu.arc_c && cu_log_ok())
		logf_("CUES arc t=%.3f %s", t, arc_c ? "a captain fights Talion: his health arc" : "no captain in a fight: no arc");
	g_cu.arc_c = arc_c;
	if (arc_c) {
		struct cb_orc oc;
		float hp = cb_health(arc_c), mx = cu_health_max(arc_c);
		if (cb_orc_get(arc_c, &oc) && mx > 0 && hp > 0) {
			int steps = (int)ceilf(SOMVR_TH_LEVELS * fminf(1.0f, hp / mx));
			float p[3] = { oc.pos[0], oc.pos[1] + g_cu.up_cm + 25, oc.pos[2] };
			th_glow_add(gl, ng, p, 0.6f, dist3(p, head),
				    SOMVR_THK_MARK | SOMVR_THK_ARC | (uint32_t)(steps < 1 ? 1 : steps) << SOMVR_THK_ARC_SHIFT);
			g_cu.arcs++;
		}
	}
}

/* ---- the use cue (the herbs' pattern for every pick the game offers) ---------------------------------------------- */
/* the pick prompt up now that is ours (not the herbs' while SOMVR_HERBS takes them, not arrows when full) */
static const char *cu_use_prompt(void)
{
	for (int i = 0; i < g_us.nup; i++) {
		const char *s = g_us.up[i].str;
		if (!strncmp(s, "Combat_Activate_Flower", 22) && g_hrb.on)
			continue;
		return s;
	}
	return NULL;
}

/* the things of that prompt within 4 m of Talion (the nav markers' walk, as the herbs') */
static void cu_use_scan(const float root[3], const char *recs, int tell)
{
	uintptr_t node = 0;
	int n = 0, k = 0, na = 0;
	struct { float d; char name[40]; float pos[3]; } near[4];
	peek(PRN_LIST + 8, &node, sizeof node);
	while (node && node != PRN_LIST && n++ < 1024) {
		uintptr_t mk = node - PRN_NODE, rec = 0, nx = 0;
		uint8_t ae[2] = { 0, 0 };
		char name[40] = "";
		peek(mk + PRN_REC, &rec, sizeof rec), peek(mk + PRN_ACTIVE, ae, sizeof ae);
		if (rec && ae[0] && ae[1])
			rx_rec_name(rec, name, sizeof name);
		if (name[0] && tell) { /* the first scan of a prompt: the nearest markers of any kind (what is there) */
			float pos[3] = { 0, 0, 0 }, d;
			((prn_pos_fn)g_prn_sites[PRN_NAV_POS].target)(mk, pos);
			if (vec_ok(pos, 1e7f) && (d = dist3(pos, root)) < 600) {
				int j = na < 4 ? na++ : 3;
				if (j < 3 || d < near[3].d) {
					near[j].d = d, memcpy(near[j].pos, pos, sizeof pos), snprintf(near[j].name, sizeof near[j].name, "%s", name);
					for (; j > 0 && near[j].d < near[j - 1].d; j--) {
						__typeof__(near[0]) x = near[j];
						near[j] = near[j - 1], near[j - 1] = x;
					}
				}
			}
		}
		if (name[0] && somvr_cu_match(recs, name) && k < 8) {
			float pos[3] = { 0, 0, 0 };
			((prn_pos_fn)g_prn_sites[PRN_NAV_POS].target)(mk, pos);
			if (vec_ok(pos, 1e7f) && dist3(pos, root) < 400) {
				g_cu.u[k].mk = mk, memcpy(g_cu.u[k].pos, pos, sizeof pos);
				snprintf(g_cu.u[k].name, sizeof g_cu.u[k].name, "%s", name);
				k++;
			}
		}
		peek(node + 8, &nx, sizeof nx);
		node = nx;
	}
	g_cu.un = k;
	if (tell && cu_log_ok()) {
		char b[400];
		int l = 0;
		b[0] = 0;
		for (int i = 0; i < na && l < (int)sizeof b - 90; i++)
			l += snprintf(b + l, sizeof b - (size_t)l, "%s '%s' %.0f cm (%.0f %.0f %.0f)", i ? ";" : "", near[i].name, near[i].d,
				      near[i].pos[0], near[i].pos[1], near[i].pos[2]);
		logf_("CUES use the markers within 6 m: %d of its kind; the nearest: %s", k, na ? b : "none");
	}
}

static int cu_hand_free(int h)
{
	return g_inp.hands[h].valid && !cl_holds(h) && !(h == SOMVR_HAND_L && (vb_grab_held() || bo_out())) &&
	       !(h == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD) && bd_sword_in_hand()) && !bo_out() && sl_knife_side() != h &&
	       !hrb_holding(h);
}

/* in_frame (the game thread, a stereo frame; after the herbs'): the cue and the hand's pick */
static void cu_use_hands(const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	if (!g_cu.use)
		return;
	double t = cb_now();
	float rt[3];
	int root_ok = g_cl.obj && peek(g_cl.obj + OBJ_POS, rt, sizeof rt) && vec_ok(rt, 1e7f);
	const char *pr = cu_use_prompt();
	int live = g_inp.ctx == SC_CTX_WORLD && !game_paused() && !vm_open();
	if (pr && strcmp(pr, g_cu.uprompt)) {
		snprintf(g_cu.uprompt, sizeof g_cu.uprompt, "%s", pr), g_cu.uscan_t = 0;
		if (cu_log_ok())
			logf_("CUES use t=%.3f the game offers '%s': its things '%s'", t, pr, somvr_cu_use_recs(pr));
	} else if (!pr && g_cu.uprompt[0]) {
		if (cu_log_ok())
			logf_("CUES use t=%.3f '%s' no longer offered", t, g_cu.uprompt);
		g_cu.uprompt[0] = 0, g_cu.un = 0;
	}
	if (pr && g_cu.uok_pos && root_ok && t >= g_cu.uscan_t) {
		cu_use_scan(rt, somvr_cu_use_recs(pr), g_cu.uscan_t == 0);
		g_cu.uscan_t = t + 0.25;
	}
	float hd = sv_heading(hq), eye[3] = { hp.x, hp.y, hp.z };
	int sq_closed[2];
	for (int h = 0; h < 2; h++) {
		int act = (g_inp.st.hand[h].active & SOMVR_HACT_INPUT) != 0;
		float sq = bd_squeeze(h, act ? g_inp.st.hand[h].squeeze : 0);
		sq_closed[h] = g_cu.uclosed[h] ? sq > 0.4f : sq >= 0.6f;
	}
	g_cu.ucue_level = 0;
	int best_i = -1, best_h = -1, ground = 0;
	float best = INFINITY, at[3] = { 0, 0, 0 };
	/* build 40 (the 11-05 note 7, the herbs' rule): a thing low on the ground is also taken by a grip over it */
	somvr_cu_take_cfg tk;
	somvr_cu_take_defaults(&tk);
	tk.reach_cm = g_cu.use_reach_cm, tk.over_cm = g_hrb.take.over_cm, tk.up_cm = g_hrb.take.up_cm, tk.low_cm = g_hrb.take.low_cm,
	tk.top_cm = 0;
	if (pr && live)
		for (int i = 0; i < g_cu.un; i++)
			for (int h = 0; h < 2; h++) {
				float pa[3] = { pw[h].x, pw[h].y, pw[h].z }, d = somvr_cu_take_dist(&tk, pa, g_cu.u[i].pos, eye[1], NULL);
				if (cu_hand_free(h) && d < best)
					best = d, best_i = i, best_h = h;
			}
	int stand = pr && strcmp(pr, "Combat_Activate_Collectable") && strcmp(pr, "Combat_Activate_Ammo");
	if (pr && live && best_i < 0 && root_ok && stand) { /* no place known, a thing that stands: 55 cm ahead of the chest */
		float hd0 = sv_heading(hq), fp[3] = { rt[0] + 55 * sinf(hd0), rt[1] + 25, rt[2] + 55 * cosf(hd0) };
		for (int h = 0; h < 2; h++) {
			float pa[3] = { pw[h].x, pw[h].y, pw[h].z }, d = dist3(pa, fp);
			if (cu_hand_free(h) && d < best)
				best = d, best_h = h, best_i = 0, ground = 2, memcpy(at, fp, sizeof at);
		}
	}
	if (pr && live && best_i < 0 && root_ok) { /* no place known: where a free hand reaches low near Talion */
		float feet = rt[1] - (g_cl.cfg.foot_cm > 0 ? g_cl.cfg.foot_cm : 102);
		for (int h = 0; h < 2; h++) {
			float up = pw[h].y - feet, dh = hypotf(pw[h].x - rt[0], pw[h].z - rt[2]), d = up > 0 ? up : 0;
			if (cu_hand_free(h) && dh <= 100 && d < best)
				best = d, best_h = h, best_i = 0, ground = 1;
		}
	}
	if (best_i >= 0) {
		if (ground == 2)
			; /* (at: ahead of the chest) */
		else if (ground)
			at[0] = pw[best_h].x, at[1] = rt[1] - (g_cl.cfg.foot_cm > 0 ? g_cl.cfg.foot_cm : 102) + 10, at[2] = pw[best_h].z;
		else
			memcpy(at, g_cu.u[best_i].pos, sizeof at);
		int in = best <= g_cu.use_reach_cm && !g_cu.uclosed[best_h];
		memcpy(g_cu.ucue_pos, at, sizeof at);
		g_cu.ucue_level = somvr_cu_reach_level(best, g_cu.use_reach_cm, in) * g_gc.level, g_cu.ucue_frame = g_fr.frame,
		g_cu.ucue_dist = dist3(at, eye), g_cu.ucue_ring = somvr_cu_ring_cell(best, g_cu.use_reach_cm, in);
		if (g_pko.look && g_gc.mode == GC_ON && !ground) { /* build 40: the thing outlined (its known place, never a guess) */
			float foot[3] = { at[0], at[1] - 15, at[2] };
			pko_set(foot, somvr_cu_reach_level(best, g_cu.use_reach_cm, in), 30, 60);
		}
		if (in && !g_cu.ucue_was)
			haptic_play(best_h, SOMVR_HX_TICK, 1, g_gc.tick_amp / 0.25f), g_cu.use_ticks++;
		if ((best_i != g_cu.ucue_i || t - g_cu.uline_t > 1) && cu_log_ok()) {
			float dx = at[0] - hp.x, dy = at[1] - hp.y, dz = at[2] - hp.z;
			g_cu.use_cues++;
			logf_("CUES use t=%.3f cue: '%s' %s at %.0f %.0f %.0f: %+.1f right %+.1f up %+.1f ahead of the eyes, the %s hand %.0f cm "
			      "from it%s", t, pr, ground == 2 ? "(no place known: ahead of the chest)" : ground ? "(no place known: at the hand, low)"
			      : g_cu.u[best_i].name, at[0], at[1], at[2],
			      dx * cosf(hd) - dz * sinf(hd), dy, dx * sinf(hd) + dz * cosf(hd), best_h ? "right" : "left", best,
			      in ? ": in reach" : "");
			g_cu.uline_t = t;
		}
		g_cu.ucue_i = best_i, g_cu.ucue_was = in, g_cu.uground = ground != 0;
		if (in && sq_closed[best_h] && !g_cu.uclosed[best_h]) { /* the grip there: the game's own pick */
			g_cu.use_takes++;
			us_hand_pick(t);
			haptic(best_h, SOMVR_HX_TAKE);
			if (cu_log_ok())
				logf_("CUES use t=%.3f taken: the %s hand gripped at '%s' (%.0f cm) - the game's pick of '%s' (123 by SOMVR_USE)", t,
				      best_h ? "right" : "left", ground == 2 ? "ahead of the chest" : ground ? "the ground" : g_cu.u[best_i].name, best,
				      pr);
		}
	} else {
		g_cu.ucue_i = -1, g_cu.ucue_was = 0;
	}
	for (int h = 0; h < 2; h++)
		g_cu.uclosed[h] = sq_closed[h];
}

/* gc_glow_add's (th_frame): the use cue's glow, the herb's look */
static void cu_use_glow_add(struct th_glow *gl, int *ng)
{
	if (!g_cu.use || g_gc.mode != GC_ON || !(g_cu.ucue_level > 0.01f) || g_fr.frame - g_cu.ucue_frame > 2)
		return;
	float p[3] = { g_cu.ucue_pos[0], g_cu.ucue_pos[1] + 15, g_cu.ucue_pos[2] };
	if (g_pko.look && !g_cu.uground) /* build 40: the white outline instead (pko_vh) */
		return;
	th_glow_add(gl, ng, p, g_cu.ucue_level, g_cu.ucue_dist, SOMVR_THK_MELEE | SOMVR_THK_BLUE);
	if (g_cu.ucue_ring > 0)
		th_glow_add(gl, ng, p, 0.95f, g_cu.ucue_dist, SOMVR_THK_RING | (uint32_t)g_cu.ucue_ring << SOMVR_THK_ARC_SHIFT);
}

/* th_frame (the game thread, every stereo frame): the game's pick begun - by B or by the hand - felt and seen */
static void cu_pick_watch(double t)
{
	uintptr_t tc = vb_talion_combat(), rec = tc ? peek_ptr(tc + VB_C_ATTACK) : 0;
	if (rec == g_cu.pk_rec)
		goto heal;
	g_cu.pk_rec = rec, g_cu.pk_name[0] = 0;
	if (rec)
		rx_rec_name(rec, g_cu.pk_name, sizeof g_cu.pk_name);
	if (!strncmp(g_cu.pk_name, "Activate_", 9)) {
		int heal = strstr(g_cu.pk_name, "HealthCollect") != NULL;
		g_cu.picks_seen++, g_cu.pk_t = t, g_cu.pk_heal = heal;
		for (int h = 0; h < 2; h++)
			haptic(h, SOMVR_HX_TAKE); /* the pick buzz (the hand that took it already had its own) */
		if (heal)
			g_cu.heal_t0 = t, g_cu.heals++, g_cu.pk_hp = cb_health(tc), g_cu.pk_hp_pending = 1;
		if (cu_log_ok())
			logf_("CUES pick t=%.3f the game's pick '%s' began (by %s): a buzz in both hands%s", t, g_cu.pk_name,
			      us_hand_recent(t) ? "the hand" : "B", heal ? ", the green heal flash on both edges" : "");
	}
heal:
	if (g_cu.pk_hp_pending && t - g_cu.pk_t > 1.5) {
		g_cu.pk_hp_pending = 0;
		if (cu_log_ok())
			logf_("CUES pick t=%.3f after '%s': Talion's health %.1f -> %.1f", t, g_cu.pk_name, (double)g_cu.pk_hp,
			      tc ? (double)cb_health(tc) : -1.0);
	}
}

/* th_frame, after the hurt flash: the heal's green flash (0.15 s in, 0.6 s out) on both edges where no hurt is */
static void cu_heal_flash(uint8_t hurt[2], uint32_t *flags, double t)
{
	double s = t - g_cu.heal_t0;
	if (!(g_cu.heal_t0 > 0) || s < 0 || s > 0.75)
		return;
	float k = s < 0.15 ? (float)(s / 0.15) : (float)(1.0 - (s - 0.15) / 0.6);
	if (k <= 0 || hurt[0] || hurt[1])
		return;
	hurt[0] = hurt[1] = (uint8_t)lrintf(200.0f * k);
	*flags |= SOMVR_THF_HEAL | SOMVR_THF_HURT;
}

/* ---- the timer, the HUD tag's flags -------------------------------------------------------------------------------- */
static void cu_timer_frame(double t)
{
	if (!g_cu.tm_hooked)
		return;
	uint64_t n = atomic_load_explicit(&g_cu.tm_ticks, memory_order_acquire);
	if (n != g_cu.tm_seen) {
		g_cu.tm_seen = n;
		uint32_t b = atomic_load_explicit(&g_cu.tm_left_bits, memory_order_relaxed);
		float left;
		memcpy(&left, &b, sizeof left);
		int ev = somvr_cu_tm_tick(&g_cu.tmc, &g_cu.tm, left, t);
		if (ev) {
			g_cu.tm_events[ev]++;
			if (cu_log_ok())
				logf_("CUES timer t=%.3f %s: %.1f s left (tick %llu)%s", t,
				      ev == SOMVR_CU_TM_START ? "a mission timer starts" : ev == SOMVR_CU_TM_MARK ? "30 s mark: the line pops"
										     : "10 s mark: the line stays up to the end",
				      (double)left, (unsigned long long)n, ev == SOMVR_CU_TM_START ? ": the line pops for 4 s" : "");
		} else if (n % 10 == 0 && cu_log_ok()) {
			logf_("CUES timer t=%.3f %.1f s left (tick %llu)", t, (double)left, (unsigned long long)n);
		}
	}
	int pop = 0;
	if (somvr_cu_tm_frame(&g_cu.tm, t, 2.5, &pop) == SOMVR_CU_TM_STOP) {
		g_cu.tm_events[SOMVR_CU_TM_STOP]++;
		if (cu_log_ok())
			logf_("CUES timer t=%.3f the timer is over (no tick for 2.5 s; %.1f s left at its last)", t, (double)g_cu.tm.left);
	}
	if (pop != g_cu.tm_pop && cu_log_ok())
		logf_("CUES timer t=%.3f the line %s at the gaze (%.1f s left)", t, pop ? "pops" : "goes back to the watch",
		      (double)g_cu.tm.left);
	g_cu.tm_pop = pop;
}

/* the HUD pass (the game thread): the tag's flags for the layer */
static uint32_t cu_hud_flags(void)
{
	cu_timer_frame(mono());
	uint32_t f = 0;
	if (g_cu.timer && g_cu.tm.on)
		f |= SOMVR_HUDF_TIMER | (g_cu.tm_pop ? SOMVR_HUDF_TIMER_POP : 0u);
	if (g_cu.alert && g_cu.alarm_on)
		f |= SOMVR_HUDF_ALARM;
	g_cu.hud_flags = f;
	return f;
}

/* "cues status|probe [S]|aware|use|timer|arc|alert on|off" (the writer thread) */
static const char *cu_word(const char *w)
{
	static char buf[200];
	if (!strncmp(w, "rim", 3)) { /* test: high alert's rim for S s */
		int s = atoi(w + 3);
		atomic_store(&g_cu.req_rim, s > 0 && s <= 60 ? s : 8);
		return "cues rim (test: high alert forced)";
	}
	if (!strncmp(w, "probe", 5)) {
		int s = atoi(w + 5);
		atomic_store(&g_cu.req_probe, s > 0 && s <= 120 ? s : 20);
		return "cues probe";
	}
	struct { const char *k; int *v; } sw[] = { { "aware", &g_cu.aware }, { "use", &g_cu.use }, { "timer", &g_cu.timer },
						   { "arc", &g_cu.arc }, { "alert", &g_cu.alert } };
	for (size_t i = 0; i < sizeof sw / sizeof sw[0]; i++) {
		size_t l = strlen(sw[i].k);
		if (!strncmp(w, sw[i].k, l) && w[l] == ' ') {
			int on = !strncmp(w + l + 1, "on", 2);
			if (sw[i].v == &g_cu.use && on && !g_us.on)
				return "cues use needs SOMVR_USE";
			if (sw[i].v == &g_cu.timer && on && !g_cu.tm_hooked)
				return "cues timer: not hooked (set SOMVR_CUE_TIMER=on at the start)";
			*sw[i].v = on;
			snprintf(buf, sizeof buf, "cues %s %s", sw[i].k, on ? "on" : "off");
			return buf;
		}
	}
	logf_("CUES status: aware %d use %d timer %d (hooked %d, ticks %llu, on %d, %.1f s left, popup %d) arc %d alert %d (high %d); "
	      "marks drawn %llu, changes %llu, the use prompt '%s' (%d places)", g_cu.aware, g_cu.use, g_cu.timer, g_cu.tm_hooked,
	      (unsigned long long)atomic_load(&g_cu.tm_ticks), g_cu.tm.on, (double)g_cu.tm.left, g_cu.tm_pop, g_cu.arc, g_cu.alert,
	      g_cu.alarm_on, (unsigned long long)g_cu.marks_drawn, (unsigned long long)g_cu.changes, g_cu.uprompt, g_cu.un);
	return "cues status";
}

static void cu_end_log(void)
{
	logf_("CUES END frames %llu, level changes %llu, mark frames %llu, double ticks %llu, runners %llu, alarm reactions %llu, arc "
	      "frames %llu; use: cue lines %llu, ticks %llu, picks by the hand %llu; timer: ticks %llu, starts %llu, 30 s %llu, 10 s "
	      "%llu, stops %llu", (unsigned long long)g_cu.frames, (unsigned long long)g_cu.changes,
	      (unsigned long long)g_cu.marks_drawn, (unsigned long long)g_cu.ticks2, (unsigned long long)g_cu.runners,
	      (unsigned long long)g_cu.alarms, (unsigned long long)g_cu.arcs, (unsigned long long)g_cu.use_cues,
	      (unsigned long long)g_cu.use_ticks, (unsigned long long)g_cu.use_takes,
	      (unsigned long long)atomic_load(&g_cu.tm_ticks), (unsigned long long)g_cu.tm_events[SOMVR_CU_TM_START],
	      (unsigned long long)g_cu.tm_events[SOMVR_CU_TM_MARK], (unsigned long long)g_cu.tm_events[SOMVR_CU_TM_LAST],
	      (unsigned long long)g_cu.tm_events[SOMVR_CU_TM_STOP]);
}

/* ---- build 37 (11-04 22:00, docs 169.8): the game camera's frustum for the layer's head-level HUD quad ------------ */
/* The game projects its world icons (an orc's awareness triangle, a kept health bar, the struggle's ring) into the HUD
 * image through the player camera (FUN_00a60920(*(P+0x2d0), ...) = Windows 0x14045c8c0, the same offsets): its transform
 * *(cam+0x338) (+8 the position, +0x14 the rotation) and the tans of its half FOVs cam+0x404 / +0x408 (the flat 43 x 25
 * deg camera: re-xr-pipeline.md 44.1) - not the eyes. So the quad that shows the HUD image must be that frustum: each icon
 * then lies on the camera's ray to its object (`SOMVR_XR_HUD_EYE=camera`). th_frame, every stereo frame: the quad 10 m
 * out along the camera's forward, in the layer's reference space (the threat map m). */
#define CU_P_CAM 0x2d0   /* the client player: its CPlayerCamera (re-gameplay.md; W 0x1403f2620 the same) */
#define CU_CAM_XF 0x338  /* CPlayerCamera: its transform object (+8 pos, +0x14 rot) (L 0xa60920 / W 0x14045c984) */
#define CU_CAM_TAN 0x404 /* CPlayerCamera: tan of the half FOVs, x and y (L 0xa60920 / W 0x14045c9fd, 0x14045ca0b) */
static struct { uint64_t n, bad; int told; } g_cuhc;
static void th_hudcam(uintptr_t pl, const somvr_th_map *m, const float head_fwd_xr[3], somvr_threat_frame *e)
{
	uintptr_t cam = pl ? peek_ptr(pl + CU_P_CAM) : 0, tr = cam ? peek_ptr(cam + CU_CAM_XF) : 0;
	float pos[3], q[4], tn[2];
	if (!tr || !peek(tr + 8, pos, sizeof pos) || !peek(tr + 0x14, q, sizeof q) || !peek(cam + CU_CAM_TAN, tn, sizeof tn) ||
	    !vec_ok(pos, 1e7f) || !(tn[0] > 0.05f && tn[0] < 5) || !(tn[1] > 0.05f && tn[1] < 5)) {
		g_cuhc.bad++;
		return;
	}
	float qn = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (!(qn > 0.9f && qn < 1.1f)) {
		g_cuhc.bad++;
		return;
	}
	sv_quat Q = sv_qnormalize(sv_quat_of(q));
	sv_vec3 f = sv_qrot(Q, (sv_vec3){ 0, 0, 1 }), u = sv_qrot(Q, (sv_vec3){ 0, 1, 0 });
	float wf[3] = { pos[0] + 100 * f.x, pos[1] + 100 * f.y, pos[2] + 100 * f.z };
	float wu[3] = { pos[0] + 100 * u.x, pos[1] + 100 * u.y, pos[2] + 100 * u.z }, x0[3], xf[3], xu[3];
	somvr_th_to_xr(m, pos, x0), somvr_th_to_xr(m, wf, xf), somvr_th_to_xr(m, wu, xu);
	float F[3] = { xf[0] - x0[0], xf[1] - x0[1], xf[2] - x0[2] }, U[3] = { xu[0] - x0[0], xu[1] - x0[1], xu[2] - x0[2] };
	float fl = sqrtf(F[0] * F[0] + F[1] * F[1] + F[2] * F[2]);
	if (!(fl > 1e-6f))
		return;
	for (int i = 0; i < 3; i++)
		F[i] /= fl;
	float d = U[0] * F[0] + U[1] * F[1] + U[2] * F[2];
	for (int i = 0; i < 3; i++)
		U[i] -= d * F[i];
	float ul = sqrtf(U[0] * U[0] + U[1] * U[1] + U[2] * U[2]);
	if (!(ul > 1e-6f))
		return;
	for (int i = 0; i < 3; i++)
		U[i] /= ul;
	/* the quad's axes: x right, y up, z back toward the camera (-forward); x = y x z */
	float Z[3] = { -F[0], -F[1], -F[2] }, X[3] = { U[1] * Z[2] - U[2] * Z[1], U[2] * Z[0] - U[0] * Z[2], U[0] * Z[1] - U[1] * Z[0] };
	float r00 = X[0], r01 = U[0], r02 = Z[0], r10 = X[1], r11 = U[1], r12 = Z[1], r20 = X[2], r21 = U[2], r22 = Z[2];
	float tr3 = r00 + r11 + r22, qx, qy, qz, qw;
	if (tr3 > 0) {
		float k = 0.5f / sqrtf(tr3 + 1);
		qw = 0.25f / k, qx = (r21 - r12) * k, qy = (r02 - r20) * k, qz = (r10 - r01) * k;
	} else if (r00 > r11 && r00 > r22) {
		float k = 2 * sqrtf(1 + r00 - r11 - r22);
		qw = (r21 - r12) / k, qx = 0.25f * k, qy = (r01 + r10) / k, qz = (r02 + r20) / k;
	} else if (r11 > r22) {
		float k = 2 * sqrtf(1 + r11 - r00 - r22);
		qw = (r02 - r20) / k, qx = (r01 + r10) / k, qy = 0.25f * k, qz = (r12 + r21) / k;
	} else {
		float k = 2 * sqrtf(1 + r22 - r00 - r11);
		qw = (r10 - r01) / k, qx = (r02 + r20) / k, qy = (r12 + r21) / k, qz = 0.25f * k;
	}
	const float D = 10.0f;
	e->hud_q[0] = qx, e->hud_q[1] = qy, e->hud_q[2] = qz, e->hud_q[3] = qw;
	for (int i = 0; i < 3; i++)
		e->hud_p[i] = x0[i] + D * F[i];
	e->hud_size[0] = 2 * D * tn[0], e->hud_size[1] = 2 * D * tn[1];
	e->flags |= SOMVR_THF_HUDCAM;
	g_cuhc.n++;
	if (!g_cuhc.told) {
		g_cuhc.told = 1;
		float dot = head_fwd_xr ? F[0] * head_fwd_xr[0] + F[1] * head_fwd_xr[1] + F[2] * head_fwd_xr[2] : NAN;
		logf_("CUES hudcam: the game camera %#llx at (%.0f %.0f %.0f), its half FOVs' tans %.3f x %.3f (%.1f x %.1f deg): the "
		      "HUD quad %.2f x %.2f m, 10 m along its forward (xr %+.2f %+.2f %+.2f; the head's forward . it %.2f)",
		      (unsigned long long)cam, pos[0], pos[1], pos[2], (double)tn[0], (double)tn[1], 2 * atan((double)tn[0]) * 57.29578,
		      2 * atan((double)tn[1]) * 57.29578, (double)e->hud_size[0], (double)e->hud_size[1], (double)F[0], (double)F[1],
		      (double)F[2], (double)dot);
	}
}
