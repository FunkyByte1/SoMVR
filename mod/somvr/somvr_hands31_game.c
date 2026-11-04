/* somvr_hands31_game.c - build 31 (som-b31-hands, docs/re-xr-pipeline.md 135; handoff/HANDOFF-combat.md "build 31
 * hands"): the free hand on an orc. Included by somvr.c after somvr_combat21_game.c (it uses the verbs' hold, the stealth
 * kill's hit, the shove). The user's session 20261102-101318 (marks 11-17):
 *   - a cut at the held orc did nothing (STEALTH "touch ... no thrust" at 2742.64) and the hand near the face 0.27 s later
 *     asked the interrogation instead;
 *   - the hold ended in ~3 s: the left trigger closed with the grip (11 ms after it) = B held = the game's drain
 *     (PC_Grab_HOG_Into -> _Complete -> the release); somvr_verbs.c's fist rule (SOMVR_VERBS_FIST_S);
 *   - the kill the player did not make: the bug mark's chord B + Y (mark 15) reached the interrogation's Eliminate as the
 *     Touch's B (vb_hands read the raw buttons, not the chord's filtered ones; fixed there, SOMVR_VERBS_CHORD_SAFE).
 *
 * SOMVR_HOLD31=on|off (code default on; needs SOMVR_VERBS=on):
 *   - the held kill: any blow of the blade on the orc the left hand holds (a hit's tier, or a stab along the blade) kills
 *     it at the contact - the stealth kill's own hit (the stock silent stealth kill's record, forceKill; a captain by his
 *     SOMVR_CAPTAIN_RULES' combat rule) instead of build 30's X (the grab's 'Shank': 10 health). A touch slower than a blow
 *     holds on (no hit). The interrogated orc's kill stays its Eliminate (stl_ask_kill: any blow too). h31_held_kill is
 *     the one place a held orc dies: the kill path of som-b31-kills (133) goes there.
 *   - the hold's push: the holding hand pushed toward the held orc at SOMVR_HOLD31_PUSH_MPS (1.5 m/s, the shove's) lets
 *     it go and the shove follows once the game has let it go (somvr_parry_game.c py_hold_push: the far shove's 2-3 m).
 *   - no pull to the face (the interrogation) while the sword swings (its speed over 1.5 m/s) or within 0.8 s of the
 *     blade's contact with the held orc.
 */

#define H31_HIST 16
static struct {
	int on;
	float push_mps;
	struct { double t; float p[3]; } h[H31_HIST];
	int nh, hpos;
	uintptr_t hist_c;   /* the held orc the history is for */
	double blow_t;      /* the blade's last contact with the held orc */
	uintptr_t kill_c;
	double kill_t, touch_line;
	uint64_t kills, kill_old, touches, pushes, fists, no_asks, lines;
} g_h31;

static int h31_log_ok(void) { return g_h31.lines++ < 400; }

static void h31_start(int *bad)
{
	memset(&g_h31, 0, sizeof g_h31);
	g_h31.on = env_word("SOMVR_HOLD31", (const char *const[]){ "off", "on" }, 2, 1);
	g_h31.push_mps = env_float("SOMVR_HOLD31_PUSH_MPS", 1.5f, 0.5f, 6, bad);
	if (g_vb.mode != VB_ON) {
		g_h31.on = 0;
		return;
	}
	logf_("CONFIG hold31=%s (SOMVR_HOLD31, som-b31-hands): %s; the fist rule SOMVR_VERBS_FIST_S %.2f s, the hold's longest "
	      "SOMVR_VERBS_GRAB_MAX_S %.1f s (0 = the game's end), the pull to the face %.2f s (SOMVR_VERBS_ASK_S) with the palm "
	      "at most %.0f cm under the eyes (SOMVR_VERBS_ASK_LOW_CM)",
	      g_h31.on ? "on" : "off", g_h31.on ? "any blow of the blade on the held orc kills it (the stealth kill's hit); the "
	      "holding hand pushed at the orc lets it go and shoves it; no pull to the face while the sword swings" : "build 30",
	      (double)g_vb.cfg.fist_s, (double)g_vb.cfg.grab_max_s, (double)g_vb.cfg.ask_s, (double)g_vb.cfg.ask_low_cm);
	if (g_h31.on)
		logf_("CONFIG hold31 push: the holding palm at %.1f m/s or faster toward the held orc (80..250 ms, within 53 deg)",
		      (double)g_h31.push_mps);
}

/* vb_hands, before the hand's step: the push's let-go, the pull's guard. v/sp: the push (world cm/s) */
static void h31_hands(const somvr_vb_hand_in *in, double t, int *let_go, int *no_ask, float v[3], float *sp)
{
	*let_go = *no_ask = 0, *sp = 0;
	if (!g_h31.on || g_vb.hand.phase != SOMVR_VBH_GRAB || !g_vb.held_c) {
		g_h31.nh = 0, g_h31.hist_c = 0;
		return;
	}
	*no_ask = t - g_h31.blow_t < 0.8 || g_cb.sp.mean_mps > 1.5f;
	if (*no_ask && !g_vb.hand.asked && g_vb.hand.near_t >= 0)
		g_h31.no_asks++;
	if (!in->have_palm || g_h31.hist_c != g_vb.held_c)
		g_h31.nh = 0;
	g_h31.hist_c = g_vb.held_c;
	if (!in->have_palm)
		return;
	g_h31.h[g_h31.hpos].t = t, memcpy(g_h31.h[g_h31.hpos].p, in->palm, sizeof in->palm);
	g_h31.hpos = (g_h31.hpos + 1) % H31_HIST, g_h31.nh += g_h31.nh < H31_HIST;
	if (t - g_vb.hand.t0 < 0.3 || g_h31.nh < 2)
		return; /* the grab's own reach is no push */
	int now = (g_h31.hpos - 1 + H31_HIST) % H31_HIST, k = -1;
	for (int j = 1; j < g_h31.nh; j++) {
		int ix = (now - j + H31_HIST) % H31_HIST;
		double age = g_h31.h[now].t - g_h31.h[ix].t;
		if (age > 0.25)
			break;
		if (age >= 0.08) {
			k = ix;
			break;
		}
	}
	if (k < 0)
		return;
	double age = g_h31.h[now].t - g_h31.h[k].t;
	for (int j = 0; j < 3; j++)
		v[j] = (float)((g_h31.h[now].p[j] - g_h31.h[k].p[j]) / age);
	*sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (*sp < g_h31.push_mps * 100.0f || *sp > PY_SHOVE_MAX_CMS)
		return;
	const float *hd = NULL;
	struct cb_orc ho;
	for (int i = 0; i < g_vb.nheads; i++)
		if (g_vb.head_c[i] == g_vb.held_c)
			hd = g_vb.heads[i];
	if (!hd && cb_orc_get(g_vb.held_c, &ho)) /* the held orc out of the targeting bands (the game's grab): its own place
						  * (boot 20261102-114641: the push found no head and let nothing go) */
		hd = ho.pos;
	if (!hd)
		return;
	float dx = hd[0] - in->palm[0], dz = hd[2] - in->palm[2], dh = hypotf(dx, dz), vh = hypotf(v[0], v[2]);
	float cs = dh > 1 && vh > 1 ? (v[0] * dx + v[2] * dz) / (dh * vh) : 0;
	if (cs < 0.6f || vh < 0.7f * *sp) /* toward the orc, along the floor (not a pull up or down) */
		return;
	*let_go = 1, g_h31.pushes++;
	if (h31_log_ok())
		logf_("HOLD31 t=%.3f the push: the holding hand at %.2f m/s toward orc %#llx (%.0f deg off it, the palm %.0f cm from "
		      "its head): let go, the shove follows", t, *sp * 0.01, (unsigned long long)g_vb.held_c,
		      acosf(fminf(1, fmaxf(-1, cs))) * 57.29578f, dh);
}

/* vb_hands, after the step */
static void h31_after(const somvr_vb_hand_out *o, uintptr_t held, const float v[3], float sp, double t)
{
	if (!g_h31.on)
		return;
	if (o->fist) {
		g_h31.fists++;
		if (h31_log_ok())
			logf_("HOLD31 t=%.3f the trigger %.0f ms after the grab: the fist closing, not the wraith hand (no drain; the "
			      "next press of the trigger is)", t, (t - g_vb.hand.t0) * 1e3);
	}
	if (o->event == SOMVR_VBE_RELEASE && o->why == SOMVR_VBR_PUSH && held)
		py_hold_push(held, v, sp, t);
}

/* vb_frame (the game thread): the pending push's shove once the game has let the orc go */
static void h31_frame(const float *root, double t)
{
	if (!g_h31.on || !g_py.pend_c)
		return;
	int released = g_vb.hand.phase == SOMVR_VBH_IDLE && (g_vb.kind != SOMVR_VBK_GRAB || t - g_py.pend_t0 > 0.6);
	py_hold_push_frame(released, root, t);
}

/* the one place a held orc dies by the blade (som-b31-kills: its kill path goes here). 1 = dead by it */
static int h31_held_kill(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier, const somvr_blade *b,
			 const float *root, double t)
{
	uintptr_t rec = stl_rec();
	if (!pc || !root || !rec || !g_cb.ok_hit || g_cb.mode != CB_FULL)
		return 0;
	int fk = kp_force_kill(o->c, SOMVR_CPF_COMBAT, "the held kill", t);
	if (fk < 0)
		return 0; /* a captain immune to it: build 30's X */
	/* som-b31-kills: the held orc plays its half of the game's held kill (the interrogation's standing kills by the side),
	 * the death at its moment (SOMVR_KILLS; not taken: the forceKill below) */
	float kax[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] };
	if (fk > 0 && kl_held_kill(pc, o, rec, ct->point, b->vel_tip, kax, t)) {
		g_h31.kills++, g_h31.kill_c = o->c, g_h31.kill_t = t;
		haptic(SOMVR_HAND_R, SOMVR_HX_KILL), haptic_k(SOMVR_HAND_L, SOMVR_HX_THUMP, 0.1f); /* the holding hand feels him go */
		logf_("HOLD31 kill t=%.3f orc %#llx held by the left hand: the blade (tier %s, %.1f m/s) -> SOMVR_KILLS plays its half "
		      "(the death at its moment)", t, (unsigned long long)o->c, sc_tier_name(tier), (double)g_cb.sp.mean_mps);
		return 1;
	}
	char death[160], rn[96] = "?";
	int dead0 = cb_dead(o->c);
	float hp0 = cb_health(o->c);
	rx_rec_name(rec, rn, sizeof rn);
	struct kp_floor kf;
	kp_floor_on(o->c, fk, "the held kill", t, &kf);
	stl_deal(pc, o, ct, b, root, rec, fk, death, sizeof death);
	kp_floor_off(&kf);
	int dead1 = cb_dead(o->c);
	if (dead1 && !dead0)
		g_h31.kills++, CB_COUNT(kills);
	CB_COUNT(calls);
	haptic(SOMVR_HAND_R, SOMVR_HX_KILL), haptic_k(SOMVR_HAND_L, SOMVR_HX_THUMP, 0.1f);
	logf_("HOLD31 kill t=%.3f orc %#llx held by the left hand: the blade (tier %s, %.1f m/s, along %.2f m/s cos %.2f) -> "
	      "Combat_DealHit(rec '%s', forceKill=%d) dead %d->%d health %.1f->%.1f; the death: %s", t, (unsigned long long)o->c,
	      sc_tier_name(tier), (double)g_cb.sp.mean_mps, (double)g_cb.sp.axis_mps, (double)g_cb.sp.cos_axis, rn, fk, dead0,
	      dead1, hp0, cb_health(o->c), death);
	kb_note(o->c, o->hobj, rec, "the held kill", t);
	g_h31.kill_c = o->c, g_h31.kill_t = t;
	return dead1;
}

/* stl_contact's first word (the combat loop's contacts and the held orc's own sweep): the orc the left hand holds. 0 =
 * not ours (the rest decide), 1 = taken (a kill), 2 = taken, no damage (a touch) */
static int h31_held_blow(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier, const somvr_blade *b,
			 const float *root, double t)
{
	if (!g_h31.on || g_vb.mode != VB_ON || g_vb.hand.phase != SOMVR_VBH_GRAB || !g_vb.held_c || o->c != g_vb.held_c ||
	    g_vb.ask_c)
		return 0; /* the interrogated orc: stl_ask_kill (its Eliminate) */
	g_h31.blow_t = t;
	if (g_h31.kill_c == o->c && t - g_h31.kill_t < 1.5)
		return 2;
	const sc_speed *sp = &g_cb.sp;
	int stab = sp->axis_mps >= g_stl.cfg.stab_mps && fabsf(sp->cos_axis) >= g_stl.cfg.stab_cos;
	if (tier == SC_TIER_NONE && !stab) {
		g_h31.touches++;
		if (t - g_h31.touch_line > 0.5 && h31_log_ok())
			g_h31.touch_line = t, logf_("HOLD31 t=%.3f the blade touches the held orc %#llx (%.1f m/s, along %.2f m/s cos "
						    "%.2f): below a blow - held on", t, (unsigned long long)o->c,
						    (double)sp->mean_mps, (double)sp->axis_mps, (double)sp->cos_axis);
		return 2;
	}
	if (h31_held_kill(pc, o, ct, tier, b, root, t))
		return 1;
	g_h31.kill_old++;
	if (h31_log_ok())
		logf_("HOLD31 t=%.3f the blow on the held orc %#llx did not kill it (no PlayerCombat/root/record, or a captain): build "
		      "30's way", t, (unsigned long long)o->c);
	return 0;
}

static void h31_end_log(void)
{
	if (!g_h31.on)
		return;
	logf_("HOLD31 END kills by the blade %llu (not: build 30's way %llu), touches held on %llu, pushes that let go %llu, "
	      "fists (no drain) %llu, pulls held off by the blade %llu", (unsigned long long)g_h31.kills,
	      (unsigned long long)g_h31.kill_old, (unsigned long long)g_h31.touches, (unsigned long long)g_h31.pushes,
	      (unsigned long long)g_h31.fists, (unsigned long long)g_h31.no_asks);
}

/* ---- build 31, item 4 (re-xr-pipeline.md 135.9): the vault over an orc by the hands - SOMVR_VAULT_HANDS=on|off ------------
 * (code default off: the hold and the throw work, his body stops the flight - the pass-through is the next step). The pure rule: somvr_ovault.h. The game side as the climb's: the player's PlayerMovement and proxy
 * (cl_read's), the body's move as the PM hook's one-shot extra move (ov_pm, before the follow's), the proxy's gravity flag
 * off while the hold or the flight lasts, the move stick held off (ov_tick), LOCO's ticks given up as for a climb. The
 * vaultable orc: one of the verbs' near orcs, in front (SOMVR_VAULT_HANDS_DEG of the head's heading), its root within
 * SOMVR_VAULT_HANDS_CM of Talion's, alive. The cue: the grab cue's glow on his crown (faint; full with the open palm on it,
 * one tick). The landing past him: his 2 s opening (py_vault, as the stick vault's). `OVAULT` lines: the hold, the
 * throw, every flight frame (Talion's root, the asked move, what the game kept, the orc's distance), the landing. */
#include "somvr_ovault.h"

static struct {
	int on, tid, through, through_now, evade, evade_cmd; /* build 31b: SOMVR_VAULT_HANDS_EVADE - the flight under the stock
							      * evade (SOMVR_VAULT_HANDS_EVADE_CMD, 121 = A; a test: 118 = X, a swing) */
	uintptr_t col_cmb, col_atk; /* the attack whose EnableCollision (Combat+0x56c) the flight holds 0 */
	unsigned char col_was;
	int col_on;
	char atk_name[64];
	uint64_t evade_taps, col_ticks, col_restored, own_ticks, not_fight;
	int col_zone, fsm;              /* the flight not yet past him (the hold's place); Talion's FSM (7 = a fight) */
	float near_cm, deg, cue_k;
	somvr_ov_cfg cfg;
	somvr_ov s;
	uintptr_t orc_c, act_pm, act_pc;
	int req, path_n;
	sv_vec3 req_w;
	uint64_t req_frame, hurts_seen, cue_frame;
	float cue_pos[3], cue_level, cue_dist;
	uintptr_t cue_c;
	int cue_on;
	double cue_line_t;
	float start[3], path_max_y;
	float yaw_grab;                 /* build 31b (139): the view's yaw (the rig's) at the grab - the landing's line */
	uint64_t grabs, throws, letgos, lands, blocked, opens, pm_moves, pm_busy, pm_notcons, ticks_held, lines, through_ticks;
} g_ov;

static int ov_log_ok(void) { return g_ov.lines++ < 800; }
static int ov_active(void) { return g_ov.on && g_ov.s.phase != SOMVR_OV_IDLE; }

static void hrb_start(int *bad);
static void ov_start(int *bad)
{
	hrb_start(bad); /* build 33: the herbs (their own switch) */
	memset(&g_ov, 0, sizeof g_ov);
	somvr_ov_cfg_default(&g_ov.cfg);
	somvr_ov_reset(&g_ov.s);
	g_ov.on = env_word("SOMVR_VAULT_HANDS", (const char *const[]){ "off", "on" }, 2, 0);
	g_ov.near_cm = env_float("SOMVR_VAULT_HANDS_CM", 160, 60, 300, bad);
	g_ov.deg = env_float("SOMVR_VAULT_HANDS_DEG", 50, 10, 90, bad);
	g_ov.cue_k = env_float("SOMVR_VAULT_HANDS_CUE_K", 0.45f, 0, 1, bad);
	g_ov.cfg.over_gain = env_float("SOMVR_VAULT_HANDS_GAIN", g_ov.cfg.over_gain, 0.5f, 3, bad);
	g_ov.cfg.top_cm = env_float("SOMVR_VAULT_HANDS_TOP_CM", g_ov.cfg.top_cm, 10, 40, bad);
	/* build 33 (re-xr-pipeline.md 139.7): the plant - his own motion (a fighting orc's lunge: 40 cm in 0.1 s) ends no hold
	 * this soon after the grip, and his move toward Talion never does */
	g_ov.cfg.plant_s = env_float("SOMVR_VAULT_HANDS_PLANT_MS", 500, 0, 1000, bad) * 1e-3f;
	/* his body blocks the flight (boot 20261102-142210: `blocked` 87 cm from him, Talion's feet 117 cm up): over him the
	 * proxy is put on the arc (the climb's own position calls), the game's collision only there */
	g_ov.through = env_word("SOMVR_VAULT_HANDS_THROUGH", (const char *const[]){ "off", "on" }, 2, 1);
	g_ov.cfg.past_cm = 80; /* (the landing clear of his body: his radius and Talion's) */
	/* build 31b: his body stops the flight's moves (135.5); the game drops its collision correction while an attack runs
	 * whose Combat+0x56c is 0 (the phase's way, 23.5): the throw taps the stock evade (A, 121), the flight holds +0x56c 0
	 * while an attack runs under it */
	g_ov.evade = env_word("SOMVR_VAULT_HANDS_EVADE", (const char *const[]){ "off", "on" }, 2, 1);
	g_ov.evade_cmd = (int)env_float("SOMVR_VAULT_HANDS_EVADE_CMD", VB_CMD_A, 100, 200, bad);
	if (g_vb.mode != VB_ON || !g_cl.on)
		g_ov.on = 0;
	logf_("CONFIG vault_hands=%s (SOMVR_VAULT_HANDS, som-b31-hands; needs SOMVR_VERBS=on and SOMVR_CLIMB): the open hand closed "
	      "on a facing orc's crown (within %.0f cm of its head point, not %.0f under it; the orc within %.0f cm and %.0f deg) "
	      "holds: the body moves 1:1 against the hand (up to %.0f cm up); let go fast = the throw (up at least %.0f cm/s, "
	      "over him at least %.0f cm/s after x%.2f, at most %.0f cm/s; it must land %.0f cm past him), the flight under %.0f "
	      "cm/s^2; his opening at the landing (SOMVR_PARRY_VAULT_MS); the cue on his crown at %.2f of the grab cue's level; "
	      "build 33: his motion ends no hold within %.0f ms of the grip (the plant), his move toward Talion never, a palm "
	      "over his crown is no shove",
	      g_ov.on ? "on" : "off", g_ov.cfg.top_cm, g_ov.cfg.below_cm, g_ov.near_cm, g_ov.deg, g_ov.cfg.rise_max_cm,
	      g_ov.cfg.min_up, g_ov.cfg.min_over, g_ov.cfg.over_gain, g_ov.cfg.max_cms, g_ov.cfg.past_cm, g_ov.cfg.g, g_ov.cue_k,
	      g_ov.cfg.plant_s * 1e3f);
}

/* the proxy's gravity flag (the climb's CL_PC_GRAV) on the vault's own PlayerMovement: -1 = not written */
static int ov_grav(int on)
{
	uintptr_t pc = g_ov.act_pm ? peek_ptr(g_ov.act_pm + CL_PM_PROXY) : 0;
	unsigned char v = 0xff;
	if (!pc || pc != g_ov.act_pc || vptr_of((void *)pc) != g_cl.vt_pc || !peek(pc + CL_PC_GRAV, &v, 1))
		return -1;
	if (v != (on ? 1 : 0))
		*(volatile unsigned char *)(pc + CL_PC_GRAV) = on ? 1 : 0;
	return v;
}

static void ov_collision(uintptr_t pm, int want);
static void ov_end(const char *what, double t)
{
	if (g_ov.col_on && g_ov.act_pm)
		ov_collision(g_ov.act_pm, 0); /* build 31b: the attack's own EnableCollision back */
	int v = ov_grav(1);
	if (ov_log_ok())
		logf_("OVAULT t=%.3f end: %s; gravity %s", t, what, v < 0 ? "not written" : v == 1 ? "1 already" : "0 -> 1");
	g_ov.act_pm = g_ov.act_pc = 0, g_ov.req = 0, g_ov.orc_c = 0;
}

/* build 31b (re-xr-pipeline.md 139.3; a test aid, SOMVR_HANDS_TEST): `ovault auto N` - N vaults by the scripted left
 * hand with no driver's latency, through the same `hands` words a driver sends (bd_word: the test poses feed the hands
 * as a controller would): at the first frame of a fighting orc's attack (its wind-up; Talion's FSM 7, the orc vaultable)
 * the
 * hand's test pose is moved so that the palm lands 8 cm over its head point (the palm's offset from the grip taken from
 * this frame), the squeeze, the hold's push down and back (30 down, 15 back in 200 ms: the body up 1:1), then the throw
 * (50 down, 50 back in 150 ms, the grip opened at 80 %: a real fling's let-go). Every step logged (`OVAULT ... auto`). */
static struct {
	_Atomic int req;    /* N + 1 asked (1 = off) */
	int left, step;
	double t, cool;
	float pose[3];      /* the test pose on the crown (cm, the head's yaw frame) */
	uint64_t tries, grabs;
	double next_dump;   /* the first vault's pictures (SOMVR_XR_DUMP_MARKED: the layer's dumps of marked frames) */
	int dumps, fdumps, flights, head_ok;
	uintptr_t atk0;     /* his attack record the last frame (a new one = its start) */
	sv_vec3 head0;      /* the orc's head point the last frame (its move: a still orc is the moment) */
} g_ova;
static const char *ov_word(const char *w)
{
	int n = 0;
	if (!strcmp(w, "auto off"))
		return atomic_store(&g_ova.req, 1), "ovault auto off";
	if (sscanf(w, "auto %d", &n) == 1 && n > 0 && n < 20)
		return atomic_store(&g_ova.req, n + 1), "ovault auto (at the next wind-ups)";
	return "ovault (not auto N|off: ignored)";
}
static void ova_hand(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ova_hand(const char *fmt, ...)
{
	char b[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	const char *r = bd_word(b);
	if (ov_log_ok())
		logf_("OVAULT t=%.3f auto: hands %s -> %s", cb_now(), b, r);
}
static void ova_end(const char *why, double t)
{
	ova_hand("squeeze L 0");
	ova_hand("test L -25 -35 30 0 0 0");
	if (ov_log_ok())
		logf_("OVAULT t=%.3f auto: try %llu over (%s); %d left", t, (unsigned long long)g_ova.tries, why, g_ova.left - 1);
	g_ova.left--, g_ova.step = 0, g_ova.cool = t + 2.5;
}
static void ova_frame(double t, const somvr_ov_in *in, uintptr_t oc, sv_vec3 hp, float fx, float fz, const sv_vec3 pw0)
{
	int rq = atomic_exchange(&g_ova.req, 0);
	if (rq)
		g_ova.left = rq - 1, g_ova.step = 0, g_ova.cool = t;
	if (g_ova.left <= 0)
		return;
	switch (g_ova.step) {
	case 0: { /* armed: a vaultable fighting orc standing (its head under 3 cm a frame): the hand onto its crown and the
		   * squeeze in one frame (both taken by the next frame's hands), the crown led by its last frame's move */
		sv_vec3 hv = { in->head.x - g_ova.head0.x, in->head.y - g_ova.head0.y, in->head.z - g_ova.head0.z };
		int had = g_ova.head_ok;
		uintptr_t atk = oc ? peek_ptr(oc + VB_C_ATTACK) : 0, atk0 = g_ova.atk0;
		g_ova.head0 = in->head, g_ova.head_ok = in->orc_ok, g_ova.atk0 = atk;
		/* the first frame of a new attack of his (its wind-up: he stands; the lunge comes toward its impact 0.8 s on -
		 * boot 193325's holds taken later in the attack ended within 0.1-0.6 s by his root's 40 cm; between his attacks
		 * Talion's FSM is 6, boot 203639: no try in 150 s) */
		if (t < g_ova.cool || g_ov.s.phase != SOMVR_OV_IDLE || !in->orc_ok || !in->live || !in->free[SOMVR_HAND_L] ||
		    !oc || !had || !atk || atk == atk0 || !(sqrtf(hv.x * hv.x + hv.y * hv.y + hv.z * hv.z) < 15))
			return;
		float tx = in->head.x + hv.x - hp.x, ty = in->head.y + hv.y + 8 - hp.y, tz = in->head.z + hv.z - hp.z;
		float px = pw0.x - hp.x, py = pw0.y - hp.y, pz = pw0.z - hp.z;
		float d[3] = { (tx - px) * fz - (tz - pz) * fx, ty - py, (tx - px) * fx + (tz - pz) * fz };
		float cur[3] = { g_bd_req.v[SOMVR_HAND_L][0], g_bd_req.v[SOMVR_HAND_L][1], g_bd_req.v[SOMVR_HAND_L][2] };
		for (int i = 0; i < 3; i++)
			g_ova.pose[i] = cur[i] + d[i];
		char an[96];
		g_ova.tries++, g_ova.step = 2, g_ova.t = t;
		if (ov_log_ok())
			logf_("OVAULT t=%.3f auto: try %llu at orc %#llx's attack start (its head %.1f cm in the last frame; attack '%s', Talion's "
			      "FSM %d): the left hand from %.0f,%.0f,%.0f to %.0f,%.0f,%.0f cm (its palm 8 cm over the crown) and the "
			      "squeeze", t, (unsigned long long)g_ova.tries, (unsigned long long)oc, sqrtf(hv.x * hv.x + hv.y * hv.y + hv.z * hv.z),
			      vb_name(peek_ptr(oc + VB_C_ATTACK), an, sizeof an), g_ov.fsm, cur[0], cur[1], cur[2], g_ova.pose[0],
			      g_ova.pose[1], g_ova.pose[2]);
		ova_hand("test L %.1f %.1f %.1f 0 0 0", g_ova.pose[0], g_ova.pose[1], g_ova.pose[2]);
		ova_hand("squeeze L 1");
		return;
	}
	case 1:
		g_ova.step = 2;
		return;
	case 2: /* the grab: the hold's push down and back */
		if (g_ov.s.phase == SOMVR_OV_HOLD) {
			g_ova.grabs++;
			if (!g_ova.flights && g_ova.dumps < 2)
				hf_dump_mark("ovault hold", t), g_ova.dumps++;
			ova_hand("swing L %.1f %.1f %.1f 0 0 0 200", g_ova.pose[0], g_ova.pose[1] - 30, g_ova.pose[2] - 15);
			g_ova.step = 3, g_ova.t = t;
		} else if (t - g_ova.t > 0.25) {
			if (ov_log_ok())
				logf_("OVAULT t=%.3f auto: no grab - the palm %.1f cm from the head point (%+.1f over it), FSM %d, live %d, "
				      "orc %d", t, dist3((float[3]){ pw0.x, pw0.y, pw0.z }, (float[3]){ in->head.x, in->head.y, in->head.z }),
				      pw0.y - in->head.y, g_ov.fsm, in->live, in->orc_ok);
			ova_end("no grab within 0.25 s", t);
		}
		return;
	case 3: /* the hold's push done: the throw */
		if (g_ov.s.phase != SOMVR_OV_HOLD) {
			ova_end("the hold ended before the throw", t);
		} else if (t - g_ova.t >= 0.22) {
			ova_hand("swing L %.1f %.1f %.1f 0 0 0 150 0.8", g_ova.pose[0], g_ova.pose[1] - 80, g_ova.pose[2] - 65);
			g_ova.step = 4, g_ova.t = t, g_ova.next_dump = t + 0.15, g_ova.flights++;
		}
		return;
	default: /* the flight to its end */
		if (g_ova.flights == 1 && g_ova.fdumps < 6 && t >= g_ova.next_dump) /* the first throw: 6 pictures 0.15 s apart */
			hf_dump_mark(g_ov.s.phase == SOMVR_OV_FLIGHT ? "ovault flight" : "ovault after", t), g_ova.fdumps++,
				g_ova.next_dump = t + 0.15;
		if ((g_ov.s.phase == SOMVR_OV_IDLE && t - g_ova.t > 0.3) || t - g_ova.t > 4)
			ova_end(g_ov.s.phase == SOMVR_OV_IDLE ? "the vault's end" : "4 s", t);
		return;
	}
}

/* build 33 (re-xr-pipeline.md 139.7; the shove's check, any thread's view of the game thread's state): a palm over orc c's
 * crown - the crown's rule, or above his head point less below_cm and within top_cm of it along the floor - or the vault
 * holding or flying over him: the vault's plant, which the shove does not take */
static int ov_plant_at(uintptr_t c, const float palm[3])
{
	if (!g_ov.on || !c || !palm)
		return 0;
	if (g_ov.s.phase != SOMVR_OV_IDLE && g_ov.orc_c == c)
		return 1;
	for (int k = 0; k < g_vb.nheads; k++) {
		if (g_vb.head_c[k] != c)
			continue;
		const float *hd = g_vb.heads[k];
		if (somvr_ov_on_crown(&g_ov.cfg, (sv_vec3){ palm[0], palm[1], palm[2] }, (sv_vec3){ hd[0], hd[1], hd[2] }))
			return 1;
		return palm[1] >= hd[1] - g_ov.cfg.below_cm && hypotf(palm[0] - hd[0], palm[2] - hd[2]) <= g_ov.cfg.top_cm;
	}
	return 0;
}

/* in_frame (the game thread, a stereo frame), before the verbs' hands: the vault's step */
static void hrb_hands(const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq);
static void ov_frame(const sv_vec3 p[2], const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	hrb_hands(pw, hp, hq); /* build 33: the herbs by the hand (SOMVR_HERBS) */
	if (!g_ov.on)
		return;
	if (!g_ov.tid)
		g_ov.tid = my_tid();
	if (g_ov.tid != my_tid())
		return;
	double t = cb_now();
	somvr_ov_in in;
	memset(&in, 0, sizeof in);
	in.t = t, in.pose_t = g_fr.views.time > 0 ? (double)g_fr.views.time * 1e-9 : 0;
	in.rig_yaw = g_fr.base.yaw - g_rig.yaw0, in.rig_scale = somvr_rig_scale(&g_rig);
	float rt[3];
	in.root_ok = g_cl.obj && peek(g_cl.obj + OBJ_POS, rt, sizeof rt) && vec_ok(rt, 1e7f);
	if (in.root_ok)
		in.root = (sv_vec3){ rt[0], rt[1], rt[2] };
	in.live = g_inp.ctx == SC_CTX_WORLD && !game_paused() && !vm_open() && !(g_cl.s.active) && !g_cl.mounted && g_cl.pm &&
		  g_cl.pc && (g_ov.s.phase != SOMVR_OV_IDLE || g_vb.hand.phase == SOMVR_VBH_IDLE);
	uint64_t hn = hu_hurts();
	in.hurt = hn > g_ov.hurts_seen, g_ov.hurts_seen = hn;
	for (int h = 0; h < 2; h++) {
		int act = (g_inp.st.hand[h].active & SOMVR_HACT_INPUT) != 0;
		in.hand[h].valid = g_inp.hands[h].valid, in.hand[h].p = p[h], in.hand[h].pw = pw[h];
		in.hand[h].squeeze = bd_squeeze(h, bo_grab(h, act ? g_inp.st.hand[h].squeeze : 0));
	}
	in.free[SOMVR_HAND_L] = !bo_out() && !cl_holds(SOMVR_HAND_L) && !sl_nograb(); /* the sword's hand: not this round */
	/* the orc: the one held by the vault, else the nearest in front within reach */
	float hy = sv_heading(hq), fx = sinf(hy), fz = cosf(hy), best = INFINITY;
	uintptr_t oc = 0;
	int ok = 0;
	for (int k = 0; k < g_vb.nheads && in.root_ok; k++) {
		uintptr_t c = g_vb.head_c[k];
		struct cb_orc o;
		if (!c || cb_dead(c) || (g_ov.orc_c && c != g_ov.orc_c) || !cb_orc_get(c, &o))
			continue;
		float dx = o.pos[0] - rt[0], dz = o.pos[2] - rt[2], d = hypotf(dx, dz);
		float cs = d > 1 ? (dx * fx + dz * fz) / d : 1;
		if (!g_ov.orc_c && (d > g_ov.near_cm || acosf(fminf(1, fmaxf(-1, cs))) * 57.29578f > g_ov.deg || !(d < best)))
			continue;
		best = d, oc = c, ok = 1;
		in.head = (sv_vec3){ g_vb.heads[k][0], g_vb.heads[k][1], g_vb.heads[k][2] };
		in.orc_root = (sv_vec3){ o.pos[0], o.pos[1] - g_cb.orc_down, o.pos[2] };
	}
	unsigned char fsm = 0;
	if (g_fr.pl)
		peek(g_fr.pl + LO_P_FSM, &fsm, 1);
	g_ov.fsm = fsm;
	if (ok && g_ov.evade && fsm != 7 && g_ov.s.phase == SOMVR_OV_IDLE)
		ok = 0, g_ov.not_fight++; /* build 31b: the flight passes him under the stock evade - the game's only in a fight */
	in.orc_ok = ok;
	static double st_t;
	if (g_bd.test && t - st_t > 1.0 && g_ov.s.phase == SOMVR_OV_IDLE && ov_log_ok()) { /* (a test: why no vault now) */
		st_t = t;
		float nd = INFINITY, na = -1;
		for (int k = 0; k < g_vb.nheads && in.root_ok; k++) {
			struct cb_orc o2;
			if (!g_vb.head_c[k] || !cb_orc_get(g_vb.head_c[k], &o2))
				continue;
			float dx = o2.pos[0] - rt[0], dz = o2.pos[2] - rt[2], d = hypotf(dx, dz);
			if (d < nd)
				nd = d, na = d > 1 ? acosf(fminf(1, fmaxf(-1, (dx * fx + dz * fz) / d))) * 57.29578f : 0;
		}
		logf_("OVAULT t=%.3f state: live %d (ctx %d paused %d vrmenu %d climb %d mounted %d pm %d vb %d fsm %d), the hand free %d, %d "
		      "heads (frame age %llu), the nearest %.0f cm %.0f deg off the head's heading: vaultable %d", t, in.live, g_inp.ctx,
		      game_paused(), vm_open(), g_cl.s.active, g_cl.mounted, g_cl.pm != 0, g_vb.hand.phase, fsm, in.free[SOMVR_HAND_L],
		      g_vb.nheads, (unsigned long long)(g_fr.frame - g_vb.heads_frame), nd, na, ok);
	}
	int ph0 = g_ov.s.phase, cl0 = g_ov.s.closed[SOMVR_HAND_L];
	somvr_ov_out o;
	somvr_ov_step(&g_ov.s, &g_ov.cfg, &in, &o);
	if (ph0 == SOMVR_OV_IDLE && g_ov.s.phase == SOMVR_OV_IDLE && !cl0 && g_ov.s.closed[SOMVR_HAND_L] && in.orc_ok &&
	    ov_log_ok()) /* a squeeze the vault did not take: why */
		logf_("OVAULT t=%.3f squeeze: not the vault's - live %d, root %d, the hand free %d; the palm %.1f cm from orc %#llx's "
		      "head point (%+.1f over it; the rule: within %.0f, not %.0f under)", t, in.live, in.root_ok, in.free[SOMVR_HAND_L],
		      dist3((float[3]){ pw[0].x, pw[0].y, pw[0].z }, (float[3]){ in.head.x, in.head.y, in.head.z }),
		      (unsigned long long)oc, pw[0].y - in.head.y, g_ov.cfg.top_cm, g_ov.cfg.below_cm);
	char an[96];
	if (o.events & SOMVR_OVE_GRAB) {
		g_ov.grabs++, g_ov.orc_c = oc, g_ov.act_pm = g_cl.pm, g_ov.act_pc = g_cl.pc, g_ov.path_n = 0;
		g_ov.yaw_grab = in.rig_yaw;
		memcpy(g_ov.start, rt, sizeof g_ov.start), g_ov.path_max_y = rt[1];
		haptic(SOMVR_HAND_L, SOMVR_HX_TAKE);
		if (ov_log_ok())
			logf_("OVAULT t=%.3f grab: the left hand on orc %#llx's crown (the palm %.1f cm from its head point, %+.1f over it; "
			      "the orc %.0f cm away, attack '%s'): the hold - the body follows the hand", t, (unsigned long long)oc,
			      dist3((float[3]){ pw[0].x, pw[0].y, pw[0].z }, (float[3]){ in.head.x, in.head.y, in.head.z }),
			      pw[0].y - in.head.y, best, vb_name(peek_ptr(oc + VB_C_ATTACK), an, sizeof an));
	}
	if (o.events & SOMVR_OVE_THROW) {
		g_ov.throws++;
		if (g_ov.evade) /* the stock evade under the flight (in a fight: the orc was only vaultable there) */
			vb_tap(g_ov.evade_cmd, t), g_ov.evade_taps++, g_ov.atk_name[0] = 0;
		haptic_k(SOMVR_HAND_L, SOMVR_HX_THUMP, 0.55f);
		if (ov_log_ok())
			logf_("OVAULT t=%.3f throw: the hand's throw up %.0f over %.0f side %.0f cm/s -> the body's %.0f,%.0f,%.0f cm/s "
			      "(risen %.1f cm): it lands %.0f cm along (needs %.0f)", t, o.throw_up, o.throw_over, o.throw_side, o.v0.x,
			      o.v0.y, o.v0.z, g_ov.s.rise, o.land_cm, o.need_cm);
	}
	if (o.events & SOMVR_OVE_LETGO) {
		g_ov.letgos++;
		char w[200];
		snprintf(w, sizeof w, "let go: %s (the hand's throw up %.0f over %.0f; lands %.0f, needs %.0f)",
			 somvr_ov_why_name(o.why), o.throw_up, o.throw_over, o.land_cm, o.need_cm);
		ov_end(w, t);
	}
	if (o.events & SOMVR_OVE_BLOCKED) {
		g_ov.blocked++;
		if (ov_log_ok())
			logf_("OVAULT t=%.3f blocked: the flight's step along the floor kept under half twice (Talion at %.0f,%.0f,%.0f, "
			      "the orc %.0f cm away): the rest straight down", t, rt[0], rt[1], rt[2], best);
	}
	if (o.events & SOMVR_OVE_LAND) {
		g_ov.lands++;
		struct cb_orc oo;
		int past = 0;
		if (g_ov.orc_c && cb_orc_get(g_ov.orc_c, &oo)) {
			float ax = oo.pos[0] - g_ov.start[0], az = oo.pos[2] - g_ov.start[2], bx = rt[0] - g_ov.start[0], bz = rt[2] - g_ov.start[2];
			past = ax * bx + az * bz > 0 && hypotf(bx, bz) > hypotf(ax, az);
		}
		uintptr_t c = g_ov.orc_c;
		char w[280];
		snprintf(w, sizeof w, "landed (%s) %.0f cm from the start, the arc's top %.0f cm over it; %s; the view's yaw %+.1f "
			 "deg since the grab", somvr_ov_why_name(o.why), hypotf(rt[0] - g_ov.start[0], rt[2] - g_ov.start[2]),
			 g_ov.path_max_y - g_ov.start[1], past ? "past the orc: its opening" : "not past the orc",
			 sv_wrap_pi(in.rig_yaw - g_ov.yaw_grab) * 57.29578f);
		ov_end(w, t);
		if (past && c)
			py_vault(c, t), g_ov.opens++;
	}
	g_ov.col_zone = 0;
	if (g_ov.s.phase == SOMVR_OV_FLIGHT && in.root_ok && g_ov.orc_c && rt[1] >= g_ov.start[1] - 5) {
		struct cb_orc oo; /* until Talion is past his far side (then the floor takes the landing again) */
		if (cb_orc_get(g_ov.orc_c, &oo)) {
			float a = (rt[0] - oo.pos[0]) * g_ov.s.over.x + (rt[2] - oo.pos[2]) * g_ov.s.over.z;
			g_ov.col_zone = a < g_cb.orc_r + 40;
		}
	}
	g_ov.through_now = 0;
	if (g_ov.through && g_ov.s.phase == SOMVR_OV_FLIGHT && in.root_ok && rt[1] > g_ov.start[1] + 40 && g_ov.orc_c) {
		struct cb_orc oo;
		g_ov.through_now = cb_orc_get(g_ov.orc_c, &oo) && hypotf(oo.pos[0] - rt[0], oo.pos[2] - rt[2]) <= g_cb.orc_r + 60;
	}
	if (g_ov.s.phase != SOMVR_OV_IDLE) {
		g_ov.req = 1, g_ov.req_w = o.move, g_ov.req_frame = rend_frame();
		if (rt[1] > g_ov.path_max_y)
			g_ov.path_max_y = rt[1];
		if (g_ov.path_n++ < 120 && ov_log_ok()) {
			struct cb_orc oo;
			float od = g_ov.orc_c && cb_orc_get(g_ov.orc_c, &oo) ? hypotf(oo.pos[0] - rt[0], oo.pos[2] - rt[2]) : -1;
			logf_("OVAULT t=%.3f %s: Talion %.1f,%.1f,%.1f (%+.1f up, %.1f along from the start), asked %.1f,%.1f,%.1f; the "
			      "orc %.0f cm; attack '%s' collision %s", t, g_ov.s.phase == SOMVR_OV_HOLD ? "hold" : g_ov.through_now ?
			      "flight (over him: through)" : "flight", rt[0], rt[1], rt[2], rt[1] - g_ov.start[1],
			      hypotf(rt[0] - g_ov.start[0], rt[2] - g_ov.start[2]), o.move.x, o.move.y, o.move.z, od,
			      g_ov.atk_name[0] ? g_ov.atk_name : "-", g_ov.col_on ? "off (+0x56c 0)" : "on");
		}
	} else {
		g_ov.req = 0;
	}
	(void)ph0;
	ova_frame(t, &in, oc, hp, fx, fz, pw[SOMVR_HAND_L]); /* build 31b: `ovault auto N` (a test) */
	/* the cue: his crown, faint while the left hand is free and open; full with the palm on it (one tick) */
	g_ov.cue_level = 0;
	if (g_ov.s.phase == SOMVR_OV_IDLE && in.orc_ok && in.live && in.free[SOMVR_HAND_L] && !g_ov.s.closed[SOMVR_HAND_L]) {
		int on = somvr_ov_on_crown(&g_ov.cfg, pw[SOMVR_HAND_L], in.head);
		g_ov.cue_pos[0] = in.head.x, g_ov.cue_pos[1] = in.head.y + 6, g_ov.cue_pos[2] = in.head.z;
		g_ov.cue_level = (on ? 1.0f : g_ov.cue_k) * g_gc.level, g_ov.cue_frame = g_fr.frame;
		g_ov.cue_dist = dist3(g_ov.cue_pos, (float[3]){ hp.x, hp.y, hp.z });
		if (on && !g_ov.cue_on)
			haptic_play(SOMVR_HAND_L, SOMVR_HX_TICK, 1, g_gc.tick_amp / 0.25f);
		if ((oc != g_ov.cue_c || t - g_ov.cue_line_t > 1) && ov_log_ok()) { /* where his crown is, for a hand (the test's pose) */
			float dx = g_ov.cue_pos[0] - hp.x, dy = g_ov.cue_pos[1] - hp.y, dz = g_ov.cue_pos[2] - hp.z;
			float px = pw[0].x - hp.x, py = pw[0].y - hp.y, pz = pw[0].z - hp.z;
			logf_("OVAULT t=%.3f cue: orc %#llx vaultable, %.0f cm away; its crown %+.1f right %+.1f up %+.1f ahead of the eyes "
			      "(the head's yaw frame); the palm %+.1f %+.1f %+.1f%s", t, (unsigned long long)oc, best, dx * fz - dz * fx, dy,
			      dx * fx + dz * fz, px * fz - pz * fx, py, px * fx + pz * fz, on ? "; the palm on it" : "");
			g_ov.cue_line_t = t;
		}
		g_ov.cue_c = oc, g_ov.cue_on = on;
	} else {
		g_ov.cue_on = 0;
	}
}

/* lo_pm_pre, after st_pm_own (build 31b): while the flight runs under an attack (the stock evade), its ticks are LOCO's -
 * the stick held at 0, so the attack's root motion is replaced by LOCO's (nothing) and its yaw is ours: the evade's flip
 * and its re-face never move Talion; the arc (the PM hook's extra move) is all the motion */
static int ov_flying(void) { return g_ov.on && g_ov.s.phase == SOMVR_OV_FLIGHT; }
static void ov_pm_own(somvr_loco_in *in, int attack)
{
	if (!ov_flying() || !attack)
		return;
	in->why = 0, in->yaw_game = 0, g_ov.own_ticks++;
}

/* th_frame (via gc_glow_add): the vault's cue glow */
static void ov_glow_add(struct th_glow *gl, int *ng)
{
	if (!g_ov.on || g_gc.mode != GC_ON || !(g_ov.cue_level > 0.01f) || g_fr.frame - g_ov.cue_frame > 2)
		return;
	th_glow_add(gl, ng, g_ov.cue_pos, g_ov.cue_level, g_ov.cue_dist, SOMVR_THK_MELEE | SOMVR_THK_BLUE);
}

/* the input tick: no move stick while the vault holds or flies (as the climb's) */
static void ov_tick(sc_tick_out *o)
{
	if (!ov_active() || o->ctx == SC_CTX_MENU)
		return;
	o->pad[SC_PAD_LX] = o->pad[SC_PAD_LY] = 0;
	o->pad_on &= ~((1u << SC_PAD_LX) | (1u << SC_PAD_LY));
	o->move[0] = o->move[1] = 0;
	g_ov.ticks_held++;
}

/* build 31b: the flight's collision hold - while an attack runs on Talion's Combat (the movement's, pm+0x38), its
 * EnableCollision (+0x56c) 0 and the changed flag (+0x56f) 1, as COLLISION_OFF writes (st_collision's way); the
 * attack's own value back at the end when the same attack still runs */
static void ov_collision(uintptr_t pm, int want)
{
	uintptr_t cmb = peek_ptr(pm + LO_PM_ATTACK), atk = cmb && g_step.ok_col ? LO_FN(LOS_ATTACK_Q, lo_attack_fn)(cmb) : 0;
	unsigned char v = 1, zero = 0, one = 1;
	if (g_ov.col_on && (!want || cmb != g_ov.col_cmb || atk != g_ov.col_atk)) {
		if (cmb == g_ov.col_cmb && atk && atk == g_ov.col_atk && g_ov.col_was && arm_poke(cmb + ST_C_COLLIDE, &g_ov.col_was, 1))
			arm_poke(cmb + ST_C_COLDIRTY, &one, 1), g_ov.col_restored++;
		g_ov.col_on = 0;
	}
	if (atk && !g_ov.atk_name[0])
		rx_rec_name(atk, g_ov.atk_name, sizeof g_ov.atk_name);
	if (!want || !atk || !peek(cmb + ST_C_COLLIDE, &v, 1))
		return;
	if (!g_ov.col_on)
		g_ov.col_on = 1, g_ov.col_cmb = cmb, g_ov.col_atk = atk, g_ov.col_was = v;
	if (v && arm_poke(cmb + ST_C_COLLIDE, &zero, 1))
		arm_poke(cmb + ST_C_COLDIRTY, &one, 1);
	g_ov.col_ticks++;
}

/* PM (somvr_pm_c), after the climb's: while the vault holds or flies, this tick is its - the gravity flag 0, its move as
 * the one-shot extra move (Talion's frame, the fields at rest), the follow's request dropped; 1 = handled */
static int ov_pm(struct hook *h, struct regs *r)
{
	uintptr_t pm = (uintptr_t)RA1(r); /* (pm, xf): rdi/rsi, Win64 rcx/rdx */
	float *xf = (float *)RA2(r);
	if (!ov_active() || pm != g_ov.act_pm || my_tid() != g_ov.tid)
		return 0;
	g_follow.req = 0;
	ov_grav(0);
	ov_collision(pm, g_ov.evade && g_ov.s.phase == SOMVR_OV_FLIGHT && g_ov.col_zone);
	float x0[7] = { 0 }, loc[3] = { 0, 0, 0 };
	int go = 0;
	uintptr_t pcx = g_ov.through_now ? peek_ptr(pm + CL_PM_PROXY) : 0;
	if (g_ov.req && rend_frame() - g_ov.req_frame <= 1 && pcx && pcx == g_ov.act_pc && un_slots(pcx)) {
		float pp[3]; /* over him: the proxy put on the arc - no sweep, so his body does not stop it */
		CL_FN(CLS_PC_GETPOS, un_pos_fn)(pcx, pp);
		if (vec_ok(pp, 1e7f)) {
			pp[0] += g_ov.req_w.x, pp[1] += g_ov.req_w.y, pp[2] += g_ov.req_w.z;
			CL_FN(CLS_PC_SETPOS, un_pos_fn)(pcx, pp);
			g_ov.through_ticks++, g_ov.req = 0;
		}
	}
	if (g_ov.req && rend_frame() - g_ov.req_frame <= 1) {
		if (!pm_at_rest(pm) || !peek((uintptr_t)xf, x0, sizeof x0) || !vec_ok(x0, 1e7f)) {
			g_ov.pm_busy++;
		} else {
			sv_vec3 l = sv_qrot(sv_qconj(sv_qnormalize((sv_quat){ x0[3], x0[4], x0[5], x0[6] })), g_ov.req_w);
			loc[0] = l.x, loc[1] = l.y, loc[2] = l.z;
			memcpy((void *)(pm + PM_XPOS), loc, sizeof loc);
			*(volatile unsigned char *)(pm + PM_PENDING) = 1;
			go = 1;
		}
	}
	g_ov.req = 0;
	r->rax = ((pm_fn)h->orig)((void *)pm, xf);
	if (go) {
		if (*(volatile unsigned char *)(pm + PM_PENDING)) {
			memset((void *)(pm + PM_XPOS), 0, sizeof loc);
			*(volatile unsigned char *)(pm + PM_PENDING) = 0;
			g_ov.pm_notcons++;
		} else {
			g_ov.pm_moves++;
		}
	}
	return 1;
}

static void hrb_end_log(void);
static void ov_end_log(void)
{
	hrb_end_log();
	if (!g_ov.on)
		return;
	logf_("OVAULT END grabs %llu, throws %llu, let-gos %llu, landings %llu (blocked %llu), openings %llu; PM moves %llu (busy "
	      "%llu, not taken %llu), stick ticks held %llu, ticks through him %llu; build 31b: evade taps %llu, ticks with the "
	      "collision held off %llu (restored %llu), the evade's ticks LOCO's %llu, frames not vaultable out of a fight %llu",
	      (unsigned long long)g_ov.grabs,
	      (unsigned long long)g_ov.throws,
	      (unsigned long long)g_ov.letgos, (unsigned long long)g_ov.lands, (unsigned long long)g_ov.blocked,
	      (unsigned long long)g_ov.opens, (unsigned long long)g_ov.pm_moves, (unsigned long long)g_ov.pm_busy,
	      (unsigned long long)g_ov.pm_notcons, (unsigned long long)g_ov.ticks_held, (unsigned long long)g_ov.through_ticks,
	      (unsigned long long)g_ov.evade_taps, (unsigned long long)g_ov.col_ticks, (unsigned long long)g_ov.col_restored,
	      (unsigned long long)g_ov.own_ticks, (unsigned long long)g_ov.not_fight);
}

/* ---- build 33 (som-b33-herbs, re-xr-pipeline.md 145): healing herbs picked by hand - SOMVR_HERBS=on|off (code default on)
 * The game's herbs are nav markers 'Collectible_Flower_*' (their place: PRN_NAV_POS, the practice's navdump); within ~0.7 m
 * the game raises the pick prompt 'Combat_Activate_Flower' on 123, and 123 then is its own pick ('Activate_HealthCollectable':
 * the health, the plant gone, its respawn - the game's rules; SOMVR_USE's B holds 123 for it). By the hand:
 *   - the cue: while the prompt is up, the grab cue's glow at the herb (faint), full with a tick when an open, free hand is
 *     within SOMVR_HERBS_REACH_CM (40) of it;
 *   - the grip there takes it: the glow moves into the fist (nothing is picked yet: the plant stays until it is eaten);
 *   - the fist at the mouth (SOMVR_HERBS_MOUTH_CM 16, SOMVR_HERBS_MOUTH_MS 250) eats it: SOMVR_USE presses 123 for the game's
 *     pick - its heal, its sound - with a crunch in the hand; the grip opened before, or the prompt gone, drops it (unpicked).
 * B at the prompt stays the fallback. `HERB` lines; `HERB END`. */
#define HRB_N 8
static struct {
	int on, tid, ok_pos, hold_h;
	float reach_cm, mouth_cm, mouth_s;
	double scan_t, mouth_t0, eat_t, held_t0, cue_line_t;
	int n;
	struct { uintptr_t mk; float pos[3]; char name[40]; } h[HRB_N];
	int cue_i, cue_full, cue_was, held_i, closed[2];
	float cue_pos[3], cue_level, cue_dist, held_pos[3];
	uint64_t cue_frame;
	float hp_before;
	int hp_pending;
	uint64_t scans, takes, eats, drops, cues, lines;
} g_hrb;

static int hrb_log_ok(void) { return g_hrb.lines++ < 300; }
static int hrb_holding(int h) { return g_hrb.on && g_hrb.hold_h >= 0 && (h < 0 || g_hrb.hold_h == h); }

static uint32_t hrb_vm_feat(void) { return g_hrb.on ? SOMVR_VMF_HERBS : 0u; } /* build 34: the Controls page's Herb line */

static void hrb_start(int *bad)
{
	memset(&g_hrb, 0, sizeof g_hrb);
	g_hrb.hold_h = -1, g_hrb.cue_i = g_hrb.held_i = -1;
	g_hrb.on = env_word("SOMVR_HERBS", (const char *const[]){ "off", "on" }, 2, 1);
	g_hrb.reach_cm = env_float("SOMVR_HERBS_REACH_CM", 40, 10, 100, bad);
	g_hrb.mouth_cm = env_float("SOMVR_HERBS_MOUTH_CM", 16, 5, 40, bad);
	g_hrb.mouth_s = 0.001f * env_float("SOMVR_HERBS_MOUTH_MS", 250, 0, 2000, bad);
	if (!g_us.on)
		g_hrb.on = 0;
	char why[160];
	g_hrb.ok_pos = check_site(&g_prn_sites[PRN_NAV_POS], why, sizeof why) == 0;
	logf_("CONFIG herbs=%s (SOMVR_HERBS, som-b33-herbs; needs SOMVR_USE): at the game's 'Combat_Activate_Flower' prompt the "
	      "herb glows; an open hand within %.0f cm grips it, the fist at the mouth (%.0f cm, %.0f ms) eats it - the game's own "
	      "pick (123); B stays; the herbs' places %s", g_hrb.on ? "on" : "off", g_hrb.reach_cm, g_hrb.mouth_cm, g_hrb.mouth_s * 1e3,
	      g_hrb.ok_pos ? "by PRN_NAV_POS" : "unknown (PRN_NAV_POS not matched - the Windows build): the herb is at Talion's "
	      "feet (the game's prompt within ~0.7 m) - an open hand reached down near the ground there takes it");
}

/* Talion's attack is the game's pick ('Activate_HealthCollectable') */
static int hrb_pick_runs(void)
{
	uintptr_t tc = vb_talion_combat();
	char an[64] = "";
	return tc && strstr(rx_rec_name(peek_ptr(tc + VB_C_ATTACK), an, sizeof an), "HealthCollect") != NULL;
}

/* the game's herb prompt up now (SOMVR_USE's pick prompts) */
static int hrb_prompt(void)
{
	for (int i = 0; i < g_us.nup; i++)
		if (!strncmp(g_us.up[i].str, "Combat_Activate_Flower", 22))
			return 1;
	return 0;
}

/* the herbs within 4 m of Talion (the nav markers' walk, as the practice's navdump) */
static void hrb_scan(const float root[3])
{
	uintptr_t node = 0;
	int n = 0, k = 0;
	peek(PRN_LIST + 8, &node, sizeof node);
	g_hrb.n = 0, g_hrb.scans++;
	while (node && node != PRN_LIST && n++ < 1024) {
		uintptr_t mk = node - PRN_NODE, rec = 0, nx = 0, np = 0;
		uint8_t ae[2] = { 0, 0 }, rf = 0;
		char name[40] = "";
		peek(mk + PRN_REC, &rec, sizeof rec), peek(mk + PRN_ACTIVE, ae, sizeof ae);
		if (rec && ae[0] && ae[1] && peek(rec + 1, &rf, 1) && (rf & 2) && peek(rec + 0x20, &np, sizeof np) && np)
			for (int i = 0; i + 1 < (int)sizeof name; i++) {
				char c = 0;
				if (!peek(np + (uintptr_t)i, &c, 1) || !c)
					break;
				name[i] = c, name[i + 1] = 0;
			}
		if (!strncmp(name, "Collectible_Flower", 18) && k < HRB_N) {
			float pos[3] = { 0, 0, 0 };
			((prn_pos_fn)g_prn_sites[PRN_NAV_POS].target)(mk, pos);
			if (vec_ok(pos, 1e7f) && dist3(pos, root) < 400) {
				g_hrb.h[k].mk = mk, memcpy(g_hrb.h[k].pos, pos, sizeof pos);
				snprintf(g_hrb.h[k].name, sizeof g_hrb.h[k].name, "%s", name);
				k++;
			}
		}
		peek(node + 8, &nx, sizeof nx);
		node = nx;
	}
	g_hrb.n = k;
}

/* in_frame (the game thread, a stereo frame): the cue, the take, the eat */
static void hrb_hands(const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	if (!g_hrb.on)
		return;
	if (!g_hrb.tid)
		g_hrb.tid = my_tid();
	if (g_hrb.tid != my_tid())
		return;
	double t = cb_now();
	float rt[3];
	int root_ok = g_cl.obj && peek(g_cl.obj + OBJ_POS, rt, sizeof rt) && vec_ok(rt, 1e7f);
	if (g_hrb.ok_pos && root_ok && t >= g_hrb.scan_t) /* (only with the matched site: the Windows build has none yet) */
		g_hrb.scan_t = t + 0.25, hrb_scan(rt);
	int prompt = hrb_prompt(), live = g_inp.ctx == SC_CTX_WORLD && !game_paused() && !vm_open();
	float hd = sv_heading(hq), eye[3] = { hp.x, hp.y, hp.z }, fwd[3] = { sinf(hd), 0, cosf(hd) };
	int sq_closed[2];
	for (int h = 0; h < 2; h++) {
		int act = (g_inp.st.hand[h].active & SOMVR_HACT_INPUT) != 0;
		float sq = bd_squeeze(h, act ? g_inp.st.hand[h].squeeze : 0);
		sq_closed[h] = g_hrb.closed[h] ? sq > 0.4f : sq >= 0.6f;
	}
	/* holding: the eat at the mouth, or the drop */
	if (g_hrb.hold_h >= 0) {
		int h = g_hrb.hold_h;
		float pa[3] = { pw[h].x, pw[h].y, pw[h].z }, dm = somvr_ab_mouth_dist(eye, fwd, pa);
		memcpy(g_hrb.held_pos, pa, sizeof pa);
		if (!sq_closed[h] || !prompt || !live) {
			g_hrb.drops++;
			if (hrb_log_ok())
				logf_("HERB t=%.3f dropped after %.2f s: %s - nothing picked (the plant stays)", t, t - g_hrb.held_t0,
				      !sq_closed[h] ? "the hand opened" : !prompt ? "the game's prompt went (Talion moved off)" : "not live");
			haptic(h, SOMVR_HX_TICK);
			g_hrb.hold_h = -1, g_hrb.mouth_t0 = 0;
		} else if (dm <= g_hrb.mouth_cm) {
			if (!(g_hrb.mouth_t0 > 0))
				g_hrb.mouth_t0 = t;
			if (t - g_hrb.mouth_t0 >= g_hrb.mouth_s) { /* eaten: the game's own pick */
				g_hrb.eats++, g_hrb.eat_t = t;
				uintptr_t tc = vb_talion_combat();
				g_hrb.hp_before = tc ? cb_health(tc) : NAN, g_hrb.hp_pending = 1;
				us_hand_pick(t);
				haptic(h, SOMVR_HX_EAT); /* the crunch: three bites (build 33's call ran them together into one 0.29 s buzz) */
				if (hrb_log_ok())
					logf_("HERB t=%.3f eaten: the fist %.0f cm from the mouth for %.2f s - the game's pick (123 by SOMVR_USE); "
					      "Talion's health %.1f before", t, dm, t - g_hrb.mouth_t0, (double)g_hrb.hp_before);
				g_hrb.hold_h = -1, g_hrb.mouth_t0 = 0;
			}
		} else {
			g_hrb.mouth_t0 = 0;
		}
	}
	/* the game's pick runs ~3.7 s ('Activate_HealthCollectable', boot 20261103-152929): the health after its end */
	if (g_hrb.hp_pending && t - g_hrb.eat_t > 0.5 && 1 &&
	    (t - g_hrb.eat_t > 6.0 || !hrb_pick_runs())) {
		uintptr_t tc = vb_talion_combat();
		g_hrb.hp_pending = 0;
		if (hrb_log_ok())
			logf_("HERB t=%.3f after the game's pick (%.1f s after the eat): Talion's health %.1f -> %.1f; the prompt %s", t,
			      t - g_hrb.eat_t, (double)g_hrb.hp_before, tc ? (double)cb_health(tc) : -1.0,
			      hrb_prompt() ? "still up" : "gone (the plant picked)");
	}
	/* the cue and the take: the nearest herb to a free, open hand while the game offers the pick */
	g_hrb.cue_level = 0;
	int best_i = -1, best_h = -1;
	float best = INFINITY;
	if (prompt && live && g_hrb.hold_h < 0)
		for (int i = 0; i < g_hrb.n; i++)
			for (int h = 0; h < 2; h++) {
				int free = g_inp.hands[h].valid && !cl_holds(h) && !(h == SOMVR_HAND_L && (vb_grab_held() || bo_out())) &&
					   !(h == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD) && bd_sword_in_hand()) && !bo_out() && sl_knife_side() != h;
				float pa[3] = { pw[h].x, pw[h].y, pw[h].z }, top[3] = { g_hrb.h[i].pos[0], g_hrb.h[i].pos[1] + 15, g_hrb.h[i].pos[2] };
				float d = dist3(pa, top);
				if (free && d < best)
					best = d, best_i = i, best_h = h;
			}
	if (!g_hrb.ok_pos && prompt && live && g_hrb.hold_h < 0 && root_ok) { /* the ground mode: the herb at Talion's feet */
		g_hrb.n = 1, snprintf(g_hrb.h[0].name, sizeof g_hrb.h[0].name, "the herb at the feet");
		for (int h = 0; h < 2; h++) {
			int free = g_inp.hands[h].valid && !cl_holds(h) && !(h == SOMVR_HAND_L && (vb_grab_held() || bo_out())) &&
				   !(h == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD) && bd_sword_in_hand()) && !bo_out() && sl_knife_side() != h;
			float feet = rt[1] - (g_cl.cfg.foot_cm > 0 ? g_cl.cfg.foot_cm : 102); /* (his root sits 102 cm over his feet) */
			float up = pw[h].y - feet, dh = hypotf(pw[h].x - rt[0], pw[h].z - rt[2]);
			float d = up > 0 ? up : 0; /* (how far above the ground; within 1 m of him along it) */
			if (free && dh <= 100 && d < best)
				best = d, best_i = 0, best_h = h;
		}
		if (best_i == 0) /* the point the hand reaches for: on the ground under it (in reach: the palm within reach_cm of it) */
			g_hrb.h[0].pos[0] = pw[best_h].x, g_hrb.h[0].pos[1] = rt[1] - (g_cl.cfg.foot_cm > 0 ? g_cl.cfg.foot_cm : 102),
			g_hrb.h[0].pos[2] = pw[best_h].z;
	}
	if (best_i >= 0) {
		int in = best <= g_hrb.reach_cm && !g_hrb.closed[best_h];
		g_hrb.cue_pos[0] = g_hrb.h[best_i].pos[0], g_hrb.cue_pos[1] = g_hrb.h[best_i].pos[1] + 15, g_hrb.cue_pos[2] = g_hrb.h[best_i].pos[2];
		g_hrb.cue_level = (in ? 1.0f : 0.45f) * g_gc.level, g_hrb.cue_frame = g_fr.frame;
		g_hrb.cue_dist = dist3(g_hrb.cue_pos, eye);
		if (in && !g_hrb.cue_was)
			haptic_play(best_h, SOMVR_HX_TICK, 1, g_gc.tick_amp / 0.25f), g_hrb.cues++;
		if ((best_i != g_hrb.cue_i || t - g_hrb.cue_line_t > 1) && hrb_log_ok()) {
			float dx = g_hrb.cue_pos[0] - hp.x, dy = g_hrb.cue_pos[1] - hp.y, dz = g_hrb.cue_pos[2] - hp.z;
			float px = pw[best_h].x - hp.x, py = pw[best_h].y - hp.y, pz = pw[best_h].z - hp.z;
			logf_("HERB t=%.3f cue: '%s' at %.0f %.0f %.0f (Talion's root %.0f cm off); the herb %+.1f right %+.1f up %+.1f ahead "
			      "of the eyes (the head's yaw frame), the %s hand %.0f cm from it; the palm %+.1f %+.1f %+.1f%s", t,
			      g_hrb.h[best_i].name, g_hrb.h[best_i].pos[0], g_hrb.h[best_i].pos[1], g_hrb.h[best_i].pos[2],
			      root_ok ? dist3(rt, g_hrb.h[best_i].pos) : -1.0f, dx * cosf(hd) - dz * sinf(hd), dy,
			      dx * sinf(hd) + dz * cosf(hd), best_h ? "right" : "left", best, px * cosf(hd) - pz * sinf(hd), py,
			      px * sinf(hd) + pz * cosf(hd), in ? ": in reach" : "");
			g_hrb.cue_line_t = t;
		}
		g_hrb.cue_i = best_i, g_hrb.cue_was = in;
		int sq_edge = sq_closed[best_h] && !g_hrb.closed[best_h];
		if (in && sq_edge) { /* the take */
			g_hrb.takes++, g_hrb.hold_h = best_h, g_hrb.held_i = best_i, g_hrb.held_t0 = t, g_hrb.mouth_t0 = 0;
			haptic(best_h, SOMVR_HX_TAKE);
			if (hrb_log_ok())
				logf_("HERB t=%.3f taken: the %s hand gripped '%s' (%.0f cm from it) - to the mouth to eat it", t,
				      best_h ? "right" : "left", g_hrb.h[best_i].name, best);
		}
	} else {
		g_hrb.cue_i = -1, g_hrb.cue_was = 0;
	}
	if (g_hrb.hold_h >= 0) /* the herb in the fist */
		memcpy(g_hrb.cue_pos, g_hrb.held_pos, sizeof g_hrb.cue_pos), g_hrb.cue_level = g_gc.level, g_hrb.cue_frame = g_fr.frame,
			g_hrb.cue_dist = dist3(g_hrb.held_pos, eye);
	for (int h = 0; h < 2; h++)
		g_hrb.closed[h] = sq_closed[h];
}

/* th_frame (via gc_glow_add): the herb's glow */
static void hrb_glow_add(struct th_glow *gl, int *ng)
{
	if (!g_hrb.on || g_gc.mode != GC_ON || !(g_hrb.cue_level > 0.01f) || g_fr.frame - g_hrb.cue_frame > 2)
		return;
	th_glow_add(gl, ng, g_hrb.cue_pos, g_hrb.cue_level, g_hrb.cue_dist, SOMVR_THK_MELEE | SOMVR_THK_BLUE);
}

static void hrb_end_log(void)
{
	if (g_hrb.on)
		logf_("HERB END takes %llu, eaten %llu, dropped %llu, cue ticks %llu, scans %llu", (unsigned long long)g_hrb.takes,
		      (unsigned long long)g_hrb.eats, (unsigned long long)g_hrb.drops, (unsigned long long)g_hrb.cues,
		      (unsigned long long)g_hrb.scans);
}
