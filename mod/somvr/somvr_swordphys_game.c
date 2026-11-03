/* somvr_swordphys_game.c - build 31: the sword as a weighted body on a joint to the hand, and the blade in flesh
 * (docs/re-xr-pipeline.md 134; somvr_smooth.h swp_*; handoff/HANDOFF-combat.md "build 31 sword"). NOT a translation unit
 * of its own: somvr.c includes it near the end (it uses g_arm, g_hsm, g_hf, g_bd, g_cb, g_stl, g_sf, g_vh, g_df, the
 * haptics, the log).
 *   SOMVR_SWORD_PHYS=off|on  (default off; build 31's env: on) with SOMVR_SWORD_WEIGHT=heavy|light (the VR menu's Sword
 *                     weight row: Heavy = Urfael as a 1.3 kg arming sword, Light = a stronger hand for it; Off = 1:1) the
 *                     drawn sword is a rigid body held to the controller by a spring-damper whose strength falls with the
 *                     speed (somvr_smooth.h swp_step) in place of build 16-30's trail (swg): a committed cut carries it, a
 *                     flick of the wrist cannot whip the tip; both hands on the hilt stronger; a blow coming (an orc's
 *                     attack tracked by SOMVR_DEFENCE) stronger still, so a block is never later than build 30's. The
 *                     drawn hands are on the hilt where the sword is (place and turn: swl_arm), never on the controller
 *                     alone; SOMVR_SWORD_PHYS_GHOST=on (default on) shows the controller's faint hand while they are far
 *                     apart (a stuck blade). Set on R_Weapon where build 16's weight was, so the drawn sword is the hits'
 *                     blade (the tiers read its speed; the defence's contact and the clank its place).
 *   SOMVR_SWORD_FLESH=off|on (default on: with SOMVR_SWORD_PHYS and the VR menu's Blade bite row / SOMVR_COMBAT_BITE on,
 *                     which then means this instead of build 17-30's bite) a landed cut or stab (combat's hit that did
 *                     damage or killed: a committed cut, a light edge-led one - a weak cut - or a stab; not a flat
 *                     slap, a glance off a shield or the orc's block) opens a wound in
 *                     the orc's body (its Pelvis/Torso/Neck/Head nodes, somvr_stealth_game.c stl_body; only a real
 *                     skeleton): the blade is held by the flesh, a cut slows through it and comes out (the far side, back
 *                     the way it came, or ripped free as the hand goes on) or stops in it, a stab stays in and goes with
 *                     the body until drawn out along the blade with a pull (or levered or yanked free). No other hit on
 *                     that orc while the blade is in it; the swing tracker waits. Haptics: a thud going in, a rumble while
 *                     it slides or cuts, a faint tick while it is held, a beat as it comes out. The game's blood
 *                     (SOMVR_COMBAT_HITFX's FX) where it comes out.
 *   For the kill helper (build 31's som-b31-kills): swp_blade_in(c) = the blade is in that orc now (a killing stab stays
 *   in the dying body: the wound follows its nodes by its object until drawn out, 10 s at most); swp_release(why) frees it.
 * Test words (SOMVR_HANDS_TEST): `fight phys on|off` (build 30's weight / this, live), `fight flesh on|off`, `fight flesh
 * dumps N` (the next N wounds marked for the layer's dumps as they open and 0.25 s in: SOMVR_XR_DUMP_MARKED).
 * Lines: `CONFIG swordphys ...`, `SWORDPHYS t=... wound open|hold|end ...`, `SWORDPHYS t=... last 10 s: ...`,
 * `SWORDPHYS END ...`. */

#define SWP_LINES 400
#define SWP_SWING_LINES 160 /* the test swings' per-frame lines (their own budget) */
#define SWP_HOLD_LINE_S 0.25
#define SWP_FX_S 0.25     /* the blood at the exit: made in a combat frame at most this late */

/* cm: the pelvis-torso, torso-neck, neck-head links about stl_body's nodes - an Uruk's belly, chest and neck (the stealth
 * kill's 17/16/9 are its spine and back from behind; boot 20261102-120800: with those the cuts' hits at combat's 35 cm
 * capsule never reached the body before the hit's own reaction took it away) */
static const float g_swp_bn_r[3] = { 21.0f, 23.0f, 11.0f };
static const char *const g_swp_kind_w[3] = { "-", "cut", "stab" };

static struct {
	int on, flesh, ghost;     /* SOMVR_SWORD_PHYS, SOMVR_SWORD_FLESH, SOMVR_SWORD_PHYS_GHOST */
	_Atomic int req_on, req_flesh; /* the test words: 1 + on */
	_Atomic int req_dumps;    /* `fight flesh dumps N`: 1 + N */
	int dumps, dump_pend;     /* wounds left to mark for the layer's dumps (SOMVR_XR_DUMP_MARKED); this frame's mark */
	double dump_t2;           /* the second mark: 0.25 s into the wound */
	int level;                /* the cfg's SWG_* */
	swp_cfg cfg;
	swp_state st;
	int64_t T;                /* the arms' XR time of the last step */
	int now;                  /* applied this arms frame */
	sv_vec3 dp;               /* the drawn grip off the controller this frame (the grips' space, cm) */
	float rot[3];             /* its turn off the controller (rotation vector) */
	float ghost_a;            /* the controller's ghost this frame (0 = none) */
	/* the wounded (or the hit's) orc: its body from the combat frame (world cm) */
	uintptr_t c, hobj;
	int hit_kind;             /* a landed hit for the next arms frame (SWP_W_*; 0 = none) */
	double hit_t;
	float nd[4][3], bq[4];
	double nd_t;
	int have_nd;
	/* the exit's blood for the next combat frame */
	int fx_pend;
	float fx_P[3];
	double fx_t;
	int fx_kind;
	/* haptics, lines */
	double rum_t, hold_line_t, open_t;
	float open_v, open_s;
	int lines, swing_lines;
	double next_log;
	uint64_t frames, applied, opened, refused, hits_skipped, fx_made, fx_fail, ghosts, held_frames;
	uint64_t ends[SWP_END_N];
	float w_peak_depth, w_peak_stretch;
} g_swp;

static int swp_log_ok(void) { return g_swp.lines < SWP_LINES ? ++g_swp.lines : 0; }
static void swp_summary(double t);
static int swp_flesh(void) { return g_swp.on && g_swp.flesh && g_hf.bite && g_hsm.weight; }
static int swp_stuck(void) { return g_swp.on && g_swp.st.wound != SWP_W_NONE; }
/* build 31's kill helper: the blade is in this orc now */
__attribute__((unused)) static int swp_blade_in(uintptr_t c) { return swp_stuck() && c && c == g_swp.c; }
/* combat's contact loop: the blade is in (or going into) this orc - no other hit on it */
static int swp_contact_held(uintptr_t c)
{
	int held = g_swp.on && (g_swp.st.wound || g_swp.st.pend || g_swp.hit_kind) && c && c == g_swp.c;
	g_swp.hits_skipped += held;
	return held;
}
static void swp_release(const char *why)
{
	g_swp.hit_kind = 0; /* (som-b31-kills: a landed hit not yet taken by the arms frame goes too) */
	if (!g_swp.st.wound && !g_swp.st.pend)
		return;
	if (swp_log_ok())
		logf_("SWORDPHYS t=%.3f wound end: released (%s)", cb_now(), why ? why : "-");
	swp_wound_end(&g_swp.st, SWP_END_CANCEL);
	g_swp.hit_kind = 0;
}
/* som-b31-kills (kl_frame, every combat frame): the orc the blade is in or going into (0 = none) and its object */
static uintptr_t swp_held_orc(uintptr_t *hobj)
{
	*hobj = g_swp.hobj;
	return g_swp.on && (g_swp.st.wound || g_swp.st.pend || g_swp.hit_kind) ? g_swp.c : 0;
}
/* swg_arm: no sword in the solved hand, or a surface holds it - the body starts again on the hand */
static void swp_none(const char *why)
{
	if (!g_swp.on)
		return;
	swp_release(why);
	g_swp.st.have = 0, g_swp.now = 0;
}
/* swl_arm: the drawn grip this frame (dp cm, the turn) and the ghost; 0 = not applied */
static int swp_drawn(sv_vec3 *dp, float *ghost_a)
{
	if (!g_swp.now)
		return 0;
	*dp = g_swp.dp, *ghost_a = g_swp.ghost_a;
	return 1;
}

/* the world point (cm) of a grips' space point (cm) with this frame's rig and shift */
static void swp_grip_to_world(const float p[3], sv_vec3 shift, float out[3])
{
	const sc_rigxf *rig = &g_arm.rig;
	float k = sc_rig_scale(rig);
	sv_vec3 d = sv_qrot(sv_qyaw(rig->yaw), (sv_vec3){ k * (p[0] - rig->p0[0]), k * (p[1] - rig->p0[1]), k * (p[2] - rig->p0[2]) });
	out[0] = rig->base[0] + shift.x + d.x, out[1] = rig->base[1] + shift.y + d.y, out[2] = rig->base[2] + shift.z + d.z;
}

/* the wounded orc's body this arms frame, in the grips' space (m); 0 = none (no nodes, too old) */
static int swp_body_now(sv_vec3 shift, swp_body *b)
{
	if (!g_swp.have_nd || cb_now() - g_swp.nd_t > 0.15)
		return 0;
	memset(b, 0, sizeof *b);
	float k = sc_rig_scale(&g_arm.rig);
	b->n = 4;
	for (int i = 0; i < 4; i++) {
		sv_vec3 g = hf_world_to_grip(g_swp.nd[i], shift);
		b->node[i][0] = g.x * 0.01f, b->node[i][1] = g.y * 0.01f, b->node[i][2] = g.z * 0.01f;
	}
	for (int i = 0; i < 3; i++)
		b->r[i] = g_swp_bn_r[i] * 0.01f / k;
	memcpy(b->o, b->node[1], sizeof b->o);
	sa_xf w = { { g_swp.nd[1][0] - shift.x, g_swp.nd[1][1] - shift.y, g_swp.nd[1][2] - shift.z }, sv_qnormalize(sv_quat_of(g_swp.bq)) };
	sv_quat_to(sa_world_to_track(&g_arm.rig, w).q, b->q);
	return 1;
}

/* the haptics of a wound's end */
static void swp_end_haptic(int why, int two)
{
	float amp = why == SWP_END_TEAR ? 0.85f : why == SWP_END_THROUGH ? 0.55f : why == SWP_END_OUT || why == SWP_END_BACK ? 0.6f : 0;
	if (!(amp > 0))
		return;
	in_haptic_event(SOMVR_HAND_R, amp, 0.06f, SOMVR_HAP_COMBAT);
	if (two)
		in_haptic_event(SOMVR_HAND_L, 0.6f * amp, 0.06f, SOMVR_HAP_COMBAT);
}

/* swg_arm (SOMVR_SWORD_WEIGHT on, the sword in the solved right hand, no surface holding it): this frame's sword on the
 * weapon node. g: the grip the arm was solved to (the grips' space, cm); q, axis: as swg_step's. 1 = done here (swg's
 * own weight not applied) */
static int swp_arm(const sa_xf *g, const float q[4], const float axis[3], float dt, int two, sa_xf X, sv_vec3 shift,
		   int64_t T)
{
	int rq = atomic_exchange(&g_swp.req_on, 0), rf = atomic_exchange(&g_swp.req_flesh, 0);
	if (rq) {
		g_swp.on = rq - 1, g_swp.st.have = 0;
		logf_("SWORDPHYS t=%.3f %s (trigger word)", cb_now(), g_swp.on ? "on: the sword a weighted body" : "off: build 30's weight");
	}
	if (rf)
		g_swp.flesh = rf - 1, logf_("SWORDPHYS t=%.3f flesh %s (trigger word)", cb_now(), g_swp.flesh ? "on" : "off");
	g_swp.now = 0, g_swp.ghost_a = 0;
	if (!g_swp.on)
		return 0;
	if (g_stl.stk.on || g_stl.stk_ease) { /* the stealth kill's own stuck blade (somvr_stealth_game.c) holds the sword */
		swp_release("the stealth kill's blade");
		g_swp.st.have = 0;
		return 1;
	}
	if (g_swp.level != g_hsm.weight) { /* the VR menu's Sword weight row */
		g_swp.level = g_hsm.weight;
		swp_defaults(&g_swp.cfg, g_swp.level);
		swp_release("the sword's weight changed");
		swp_reset(&g_swp.st);
	}
	g_swp.frames++;
	const sc_blade_cfg *bc = &g_cb.bcfg;
	float k = sc_rig_scale(&g_arm.rig), bs = sz_body_scale();
	g_swp.cfg.s_tip = bc->len_cm * bs / k * 0.01f;
	g_swp.cfg.s_tip = g_swp.cfg.s_tip > 0.4f ? g_swp.cfg.s_tip : 1.0f;
	if (!swp_flesh() && (g_swp.st.wound || g_swp.st.pend))
		swp_release("the flesh is off");
	swp_body b;
	int hb = (g_swp.st.wound || g_swp.st.pend || g_swp.hit_kind) && swp_body_now(shift, &b);
	if (g_swp.hit_kind && !g_swp.st.wound && !g_swp.st.pend) { /* combat's landed hit of the last frame */
		int ok = hb && swp_wound_hit(&g_swp.st, &g_swp.cfg, g_swp.hit_kind, &b);
		if (!ok) {
			g_swp.refused++;
			in_haptic_event(SOMVR_HAND_R, 1.0f, 0.2f, SOMVR_HAP_COMBAT); /* the hit's own pulse after all */
		}
		if (swp_log_ok())
			logf_("SWORDPHYS t=%.3f hit (%s) orc=%#lx: %s", cb_now(), g_swp_kind_w[g_swp.hit_kind], (unsigned long)g_swp.c,
			      ok ? "waiting for the blade in its body" : !hb ? "refused: its body not read" : "refused");
		g_swp.hit_kind = 0;
	}
	/* a blow coming at Talion: the guard (SOMVR_DEFENCE's tracked attacks) */
	int guard = 0;
	for (int i = 0; i < TH_TRACKS && !guard; i++)
		guard = g_df.tr[i].live && g_df.tr[i].c;
	float p[3] = { g->p.x * 0.01f, g->p.y * 0.01f, g->p.z * 0.01f }, op[3], oq[4];
	if (T > 0 && g_swp.T > 0 && g_swp.st.have)
		dt = (float)((double)(T - g_swp.T) * 1e-9);
	g_swp.T = T;
	int had = g_swp.st.wound;
	int stepped = swp_step(&g_swp.st, &g_swp.cfg, cb_now(), p, q, axis, dt, two, guard, hb ? &b : NULL, op, oq);
	swp_state *s = &g_swp.st;
	double t = cb_now();
	g_swp_off_rad = stepped ? s->off_rad : 0;
	if (s->ev & SWP_EV_START) {
		g_swp.opened++, g_swp.open_t = t, g_swp.open_v = s->in_v, g_swp.open_s = s->s;
		in_haptic_event(SOMVR_HAND_R, 1.0f, 0.09f, SOMVR_HAP_COMBAT); /* the thud going in */
		if (g_swp.dumps > 0) /* tests: the eyes as it goes in and 0.25 s into it */
			g_swp.dumps--, g_swp.dump_pend = 1, g_swp.dump_t2 = t + 0.25;
		if (two)
			in_haptic_event(SOMVR_HAND_L, 0.6f, 0.09f, SOMVR_HAP_COMBAT);
		if (swp_log_ok())
			logf_("SWORDPHYS t=%.3f wound open (%s) orc=%#lx: the blade %.0f cm from the grip into its %s at %.1f m/s",
			      t, g_swp_kind_w[s->wound], (unsigned long)g_swp.c, (double)s->s * 100,
			      s->link == 0 ? "belly" : s->link == 1 ? "chest" : "neck", (double)s->in_v);
	}
	if (s->ev & SWP_EV_END) {
		if (s->end >= 0 && s->end < SWP_END_N)
			g_swp.ends[s->end]++;
		if (had)
			swp_end_haptic(s->end, two);
		if (had && s->end != SWP_END_CANCEL && s->end != SWP_END_MISS) { /* the blood where it came out */
			float P[3] = { s->end_P[0] * 100, s->end_P[1] * 100, s->end_P[2] * 100 };
			swp_grip_to_world(P, shift, g_swp.fx_P);
			g_swp.fx_pend = 1, g_swp.fx_t = t, g_swp.fx_kind = s->end_kind;
		}
		if (!had && s->end == SWP_END_MISS) {
			in_haptic_event(SOMVR_HAND_R, 1.0f, 0.2f, SOMVR_HAP_COMBAT); /* the hit's own pulse after all */
			if (swp_log_ok())
				logf_("SWORDPHYS t=%.3f the blade came within %.1f cm of its body's surface while the hit waited (%s)", t,
				      (double)-s->pend_pen * 100, s->pend_pen > -1 ? "never" : "-");
		}
		if (had && s->tear_why >= 3 && swp_log_ok()) { /* (a stab's tear: what moved - the body's frame this frame) */
			float bm = sqrtf((s->body.o[0] - s->body0.o[0]) * (s->body.o[0] - s->body0.o[0]) + (s->body.o[1] - s->body0.o[1]) *
					 (s->body.o[1] - s->body0.o[1]) + (s->body.o[2] - s->body0.o[2]) * (s->body.o[2] - s->body0.o[2]));
			float bd = fabsf(s->body.q[0] * s->body0.q[0] + s->body.q[1] * s->body0.q[1] + s->body.q[2] * s->body0.q[2] +
					 s->body.q[3] * s->body0.q[3]);
			logf_("SWORDPHYS t=%.3f the stab's tear: the blade turned %.1f deg in the body, the wound slid %.1f cm; the body's frame "
			      "moved %.1f cm and turned %.1f deg this frame", t, (double)s->dbg_lever * 57.29578, (double)s->dbg_cut * 100,
			      (double)bm * 100, (double)(2.0f * acosf(fminf(1.0f, bd))) * 57.29578);
		}
		if (swp_log_ok())
			logf_("SWORDPHYS t=%.3f wound end (%s): %s%s after %.0f ms (in at %.1f m/s; the deepest %.1f cm, the hand drawn %.1f cm "
			      "off the controller at most)", t, g_swp_kind_w[s->end_kind], swp_end_name(s->end),
			      s->end != SWP_END_TEAR ? "" : s->tear_why == 1 ? " (the hand 30 cm off)" : s->tear_why == 2 ? " (the hand turned off)"
			      : s->tear_why == 3 ? " (levered in the body)" : " (cut out sideways)",
			      had ? (t - g_swp.open_t) * 1e3 : 0.0, (double)g_swp.open_v, (double)g_swp.w_peak_depth * 100,
			      (double)g_swp.w_peak_stretch * 100);
		g_swp.w_peak_depth = g_swp.w_peak_stretch = 0;
		if (!s->wound && !s->pend)
			g_swp.have_nd = 0;
	}
	if (s->wound) { /* held: the rumble while it slides or cuts, a faint tick while it is still */
		g_swp.held_frames++;
		g_swp.w_peak_depth = fmaxf(g_swp.w_peak_depth, s->depth), g_swp.w_peak_stretch = fmaxf(g_swp.w_peak_stretch, s->stretch);
		float v = fabsf(s->slide_v) + s->flow_v;
		if (t >= g_swp.rum_t) {
			float amp = v > 0.04f ? 0.25f + 0.5f * fminf(1.0f, v / 1.0f) : 0.12f;
			in_haptic_event(SOMVR_HAND_R, amp, 0.04f, SOMVR_HAP_COMBAT);
			if (two)
				in_haptic_event(SOMVR_HAND_L, 0.6f * amp, 0.04f, SOMVR_HAP_COMBAT);
			g_swp.rum_t = t + (v > 0.04f ? 0.035 : 0.12);
		}
		if (t >= g_swp.hold_line_t && swp_log_ok()) {
			g_swp.hold_line_t = t + SWP_HOLD_LINE_S;
			logf_("SWORDPHYS t=%.3f wound hold (%s): %.1f cm deep, the blade %.0f cm from the grip at the wound, sliding %.2f "
			      "m/s, the wound moving %.2f m/s in the body, the flesh's force %.0f N; the hand drawn %.1f cm and %.1f deg off "
			      "the controller", t, g_swp_kind_w[s->wound], (double)s->depth * 100, (double)s->s * 100, (double)s->slide_v,
			      (double)s->flow_v, (double)s->f_wound, (double)s->stretch * 100, (double)s->off_rad * 57.29578);
		}
	}
	if (g_bd.test && g_bd_sw.on[SOMVR_HAND_R] && g_swp.swing_lines < SWP_SWING_LINES) { /* `hands swing`: the timeline */
		g_swp.swing_lines++;
		logf_("SWORDPHYS t=%.3f swing: the hand turns %.1f rad/s, the sword %.1f; the sword %.1f deg and %.1f cm off the hand, "
		      "its tip %.1f m/s%s%s", t, (double)sqrtf(s->tw_last[0] * s->tw_last[0] + s->tw_last[1] * s->tw_last[1] +
							s->tw_last[2] * s->tw_last[2]),
		      (double)sqrtf(s->w[0] * s->w[0] + s->w[1] * s->w[1] + s->w[2] * s->w[2]), (double)s->off_rad * 57.29578,
		      (double)s->stretch * 100, (double)s->tip_v, two ? " (both hands)" : "", guard ? " (a blow coming)" : "");
	}
	if (!stepped)
		return 1; /* (a reset: the sword on the controller this frame) */
	/* the weapon node as if the grip were the sword's: W' = gm2 (x) gm^-1 (x) W */
	float qc[4], dq[4], qs[4] = { oq[0], oq[1], oq[2], oq[3] };
	memcpy(qc, q, sizeof qc);
	sv_quat d = sv_qnormalize(sv_qmul(sv_quat_of(qs), sv_qconj(sv_quat_of(qc))));
	sv_quat_to(d, dq);
	if (dq[3] < 0)
		for (int i = 0; i < 4; i++)
			dq[i] = -dq[i];
	float sn = sqrtf(dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2]), ang = 2.0f * atan2f(sn, dq[3]);
	for (int i = 0; i < 3; i++)
		g_swp.rot[i] = sn > 1e-9f ? dq[i] / sn * ang : 0;
	g_swp.dp = (sv_vec3){ op[0] * 100 - g->p.x, op[1] * 100 - g->p.y, op[2] * 100 - g->p.z };
	sa_xf g2 = { { op[0] * 100, op[1] * 100, op[2] * 100 }, sv_qnormalize(sv_quat_of(qs)) };
	sa_xf gm = sa_grip_to_model(&g_arm.rig, shift, X, *g), gm2 = sa_grip_to_model(&g_arm.rig, shift, X, g2);
	if (bs != 1.0f)
		gm.p = (sv_vec3){ gm.p.x / bs, gm.p.y / bs, gm.p.z / bs }, gm2.p = (sv_vec3){ gm2.p.x / bs, gm2.p.y / bs, gm2.p.z / bs };
	const sa_chain *c = &g_arm.ch[SA_R];
	sa_xf W = sa_node_xf(&g_arm.pose[c->wpn]);
	sa_node_set(&g_arm.pose[c->wpn], sa_xf_mul(sa_xf_mul(gm2, sa_xf_inv(gm)), W));
	g_swp.applied++, g_swp.now = 1;
	memcpy(g_hsm.w_rot_now, g_swp.rot, sizeof g_swp.rot), g_hsm.w_now = 1; /* (the later turns compose on it) */
	g_hsm.w_applied++;
	/* the controller's ghost while the hand drawn on the hilt is far from it */
	if (g_swp.ghost) {
		float st = s->stretch * 100, off = s->off_rad * 57.29578f;
		/* (free, the turn's lag is the weight itself: no ghost for it - only a held blade's, or a stretch) */
		float a = s->wound ? fmaxf((st - 6.0f) / 9.0f, (off - 25.0f) / 20.0f) : (st - 6.0f) / 9.0f;
		g_swp.ghost_a = a > 0 ? 0.35f * fminf(1.0f, a) : 0;
		g_swp.ghosts += g_swp.ghost_a > 0;
	}
	return 1;
}

/* cb_hit -> hf_hit, a landed cut or stab with the flesh on (hf_will_bite said so): the wound for the next arms frame */
static void swp_hit(const struct cb_orc *o, int kind, double t)
{
	if (g_swp.st.wound || g_swp.st.pend || g_swp.hit_kind) {
		in_haptic_event(SOMVR_HAND_R, 1.0f, 0.2f, SOMVR_HAP_COMBAT);
		return;
	}
	float nd[4][3], rq[4] = { 0, 0, 0, 1 };
	int skel = stl_body(o, nd) && peek(o->hobj + OBJ_ROT, rq, sizeof rq) && vec_ok(rq, 2.0f);
	if (!skel) { /* no skeleton (a beast, an unreadable body): the hit as before */
		g_swp.refused++;
		in_haptic_event(SOMVR_HAND_R, 1.0f, 0.2f, SOMVR_HAP_COMBAT);
		if (swp_log_ok())
			logf_("SWORDPHYS t=%.3f hit (%s) orc=%#lx: no wound (its skeleton not read)", t, g_swp_kind_w[kind], (unsigned long)o->c);
		return;
	}
	memcpy(g_swp.nd, nd, sizeof nd), memcpy(g_swp.bq, rq, sizeof rq);
	g_swp.nd_t = t, g_swp.have_nd = 1, g_swp.c = o->c, g_swp.hobj = o->hobj, g_swp.hit_kind = kind, g_swp.hit_t = t;
}

/* the combat frame: the wounded orc's body each frame (its object even once the dead leave combat's list), the blood */
static void swp_h4(const struct cb_orc *o, int n, double t)
{
	if (!g_swp.on)
		return;
	swp_summary(t);
	int dq = atomic_exchange(&g_swp.req_dumps, 0);
	if (dq)
		g_swp.dumps = dq - 1, logf_("SWORDPHYS t=%.3f the next %d wounds marked for dumps (trigger word)", t, g_swp.dumps);
	if (g_swp.dump_pend)
		g_swp.dump_pend = 0, hf_dump_mark("SWORDPHYS: the wound open", t);
	else if (g_swp.dump_t2 > 0 && t >= g_swp.dump_t2)
		g_swp.dump_t2 = 0, hf_dump_mark("SWORDPHYS: 0.25 s into the wound", t);
	if (g_swp.fx_pend) {
		g_swp.fx_pend = 0;
		const char *fxn = g_hf.fxname[g_swp.fx_kind == SWP_W_STAB ? SC_KIND_STAB : SC_KIND_CUT];
		char why[160] = "-";
		int ok = g_hf.fx && fxn[0] && t - g_swp.fx_t <= SWP_FX_S && hf_fx_at(fxn, g_swp.hobj, g_swp.fx_P, t, why, sizeof why);
		g_swp.fx_made += ok, g_swp.fx_fail += !ok && g_hf.fx;
		if (swp_log_ok())
			logf_("SWORDPHYS t=%.3f blood where it came out (%.1f,%.1f,%.1f): %s %s", t, (double)g_swp.fx_P[0],
			      (double)g_swp.fx_P[1], (double)g_swp.fx_P[2], ok ? "made" : "not made", why);
	}
	if (!(g_swp.st.wound || g_swp.st.pend) || !g_swp.hobj)
		return;
	struct cb_orc oc;
	int i = 0;
	while (i < n && o[i].c != g_swp.c)
		i++;
	if (i < n) {
		oc = o[i];
	} else { /* (the dead are not in combat's list: its object) */
		memset(&oc, 0, sizeof oc);
		oc.c = g_swp.c, oc.hobj = g_swp.hobj;
		if (!peek(oc.hobj + OBJ_POS, oc.pos, sizeof oc.pos) || !vec_ok(oc.pos, 1e7f))
			return;
	}
	float nd[4][3], rq[4];
	if (stl_body(&oc, nd) && peek(oc.hobj + OBJ_ROT, rq, sizeof rq) && vec_ok(rq, 2.0f))
		memcpy(g_swp.nd, nd, sizeof nd), memcpy(g_swp.bq, rq, sizeof rq), g_swp.nd_t = t, g_swp.have_nd = 1;
}

/* the trigger words `fight phys on|off`, `fight flesh on|off` (the writer thread) */
static const char *swp_word(const char *w)
{
	char a[16] = "", b[8] = "";
	if (!g_bd.test)
		return "fight phys (SOMVR_HANDS_TEST is off: ignored)";
	int nd = -1;
	if (sscanf(w, "flesh dumps %d", &nd) == 1 && nd >= 0 && nd <= 20)
		return atomic_store(&g_swp.req_dumps, 1 + nd), "fight flesh dumps";
	if (sscanf(w, "%15s %7s", a, b) != 2 || (strcmp(b, "on") && strcmp(b, "off")))
		return "fight phys|flesh (not on|off|dumps N: ignored)";
	if (!strcmp(a, "phys"))
		return atomic_store(&g_swp.req_on, 1 + !strcmp(b, "on")), "fight phys";
	return atomic_store(&g_swp.req_flesh, 1 + !strcmp(b, "on")), "fight flesh";
}

static void swp_summary(double t)
{
	if (!g_swp.on || t < g_swp.next_log)
		return;
	g_swp.next_log = t + 10.0;
	swp_state *s = &g_swp.st;
	if (!g_swp.frames)
		return;
	logf_("SWORDPHYS t=%.3f last 10 s (%s): frames %llu, applied %llu; the sword off the hand up to %.1f deg and %.1f cm; the "
	      "hand's swing up to %.1f rad/s, the sword's %.1f, its tip %.1f m/s; held in a wound %llu frames, the deepest %.1f cm; "
	      "so far: wounds opened %llu (hits refused %llu), ends: out %llu, through %llu, back %llu, torn %llu, missed %llu, cancelled %llu; "
	      "the controller's ghost %llu frames; resets %llu", t, g_swp.level == SWG_HEAVY ? "heavy" : "light",
	      (unsigned long long)g_swp.frames, (unsigned long long)g_swp.applied, (double)s->peak_off * 57.29578,
	      (double)s->peak_stretch * 100, (double)s->peak_tw, (double)s->peak_w, (double)s->peak_tip,
	      (unsigned long long)g_swp.held_frames, (double)s->peak_depth * 100, (unsigned long long)g_swp.opened,
	      (unsigned long long)g_swp.refused, (unsigned long long)g_swp.ends[SWP_END_OUT], (unsigned long long)g_swp.ends[SWP_END_THROUGH],
	      (unsigned long long)g_swp.ends[SWP_END_BACK], (unsigned long long)g_swp.ends[SWP_END_TEAR],
	      (unsigned long long)g_swp.ends[SWP_END_MISS], (unsigned long long)g_swp.ends[SWP_END_CANCEL],
	      (unsigned long long)g_swp.ghosts, (unsigned long long)s->resets);
	s->peak_off = s->peak_stretch = s->peak_tw = s->peak_w = s->peak_tip = s->peak_depth = 0;
	g_swp.frames = g_swp.applied = g_swp.held_frames = g_swp.ghosts = 0;
}

static void swp_config(int *bad)
{
	static const char *const onoff_w[] = { "off", "on" };
	memset(&g_swp, 0, sizeof g_swp);
	g_swp.on = env_word("SOMVR_SWORD_PHYS", onoff_w, 2, 0);
	g_swp.flesh = env_word("SOMVR_SWORD_FLESH", onoff_w, 2, 1);
	g_swp.ghost = env_word("SOMVR_SWORD_PHYS_GHOST", onoff_w, 2, 1);
	if (g_swp.on && (!g_arm.on || !g_cb.mode))
		*bad = 1, logf_("WARN SOMVR_SWORD_PHYS=on needs SOMVR_ARMS=1 and SOMVR_COMBAT (the drawn sword, our hits): off"), g_swp.on = 0;
	g_swp.level = g_hsm.weight;
	swp_defaults(&g_swp.cfg, g_swp.level ? g_swp.level : SWG_HEAVY);
	swp_reset(&g_swp.st);
	const swp_cfg *c = &g_swp.cfg;
	logf_("CONFIG swordphys=%s (SOMVR_SWORD_PHYS: the sword a %.1f kg body, its balance %.0f cm over the fist, held by the hand's "
	      "%.0f N/m and %.0f N m/rad, at most %.0f N and %.0f N m (less as it swings faster, nothing past %.0f rad/s; braking %.0f "
	      "N m); both hands x%.1f, a blow coming x%.1f; with SOMVR_SWORD_WEIGHT=%s) flesh=%s (with the Blade bite row: a cut "
	      "%.0f N to slice on, %.0f N to start, x%.1f at the spine, %.0f N back out; a stab drawn out past %.0f N, in %.0f N; "
	      "torn free %.0f cm / %.0f deg off the hand) ghost=%s", g_swp.on ? "on" : "off", (double)c->mass,
	      (double)c->com * 100, (double)c->k_lin, (double)c->k_ang, (double)c->f_max, (double)c->t_max, (double)c->w_hill,
	      (double)c->t_stop, (double)c->two_f, (double)c->guard_f,
	      g_hsm.weight == SWG_HEAVY ? "heavy" : g_hsm.weight ? "light" : "off (no weight: 1:1)", g_swp.flesh ? "on" : "off",
	      (double)c->y_cut, (double)c->y_cut_s, (double)c->core_k, (double)c->y_back, (double)c->y_pull, (double)c->y_push,
	      (double)c->tear_m * 100, (double)c->tear_rad * 57.29578, g_swp.ghost ? "on" : "off");
}

static void swp_end_log(void)
{
	if (!g_swp.on && !g_swp.opened)
		return;
	logf_("SWORDPHYS END wounds opened %llu (refused %llu, other hits skipped while in %llu), blood at the exit %llu (failed %llu); "
	      "the spring's resets %llu", (unsigned long long)g_swp.opened, (unsigned long long)g_swp.refused,
	      (unsigned long long)g_swp.hits_skipped, (unsigned long long)g_swp.fx_made, (unsigned long long)g_swp.fx_fail,
	      (unsigned long long)g_swp.st.resets);
}
