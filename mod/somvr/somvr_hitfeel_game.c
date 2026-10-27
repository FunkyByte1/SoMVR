/* somvr_hitfeel_game.c - build 17: cuts that land feel like they land (docs/re-xr-pipeline.md 59; handoff/HANDOFF-feel.md
 * "build 17: the bite"; the combat design note's item 3: Blade & Sorcery's bite, Until You Fall's feedback). NOT a
 * translation unit of its own: somvr.c includes it after somvr_smooth_game.c (it uses g_arm, g_aw, g_bd, g_sf, g_hsm,
 * g_cb, g_df, g_fig, g_sl, g_fr, the haptics, the log).
 *   SOMVR_COMBAT_BITE=off|on  (default off; build 17's env: on) a full cut that lands (the heavy tier: a committed cut;
 *                     not a tap, a flat slap or a stab; not a glance off a raised shield, not one the orc blocks) stops
 *                     the drawn blade in the body: from the next arms frame the weapon node turns about the grip so the
 *                     blade passes through the point that was in the body at the hit (somvr_smooth.h swb_*), for at most
 *                     SOMVR_COMBAT_BITE_MS (60) or until that turn passes SOMVR_COMBAT_BITE_DEG (30: a fast cut tears
 *                     free sooner), then eases back over SOMVR_COMBAT_BITE_EASE_MS (100) onto the blade as drawn (the
 *                     hand's, after SOMVR_SWORD_WEIGHT's offset). The hand stays the controller's (SOMVR_SWORD_WEIGHT_HAND
 *                     =follow turns our drawn hands with the weight only, never the bite). The hits read the drawn blade
 *                     (R_Weapon), so what is detected is what is drawn; the re-hit cooldown (0.25 s a orc) is longer
 *                     than a bite (at most ~0.17 s); during a bite the swing tracker is not fed (the stop is not the end of
 *                     the swing, the release no new one). The haptic: a full pulse for the hold (both hands on the hilt:
 *                     the left at 0.6) and a short weaker beat when the blade comes free - no ring (the clank's is a crack
 *                     and a ring). The VR menu's "Blade bite" row (Off / On; settings.conf COMBAT_BITE) sets it live.
 *   SOMVR_COMBAT_HITSND=off|on (default off) our own sound per landed hit kind, posted at the contact from our Wwise object
 *                     (SOMVR_SLOTS' sound): SOMVR_COMBAT_HITSND_CUT, _SLAP, _TAP, _STAB = a game event name each (off =
 *                     none; unset: HF_SND_*, a tap as a slap); the game's own hit sound (cbt_imp_atk_player, posted inside
 *                     Combat_DealHit) stays; the shield's clank stays DEFENCE's.
 *   SOMVR_COMBAT_HITFX=off|on (default off) a game FX at the contact of a landed cut (SOMVR_COMBAT_HITFX_CUT, a .fxp
 *                     name, '/' taken for '\\'; unset: HF_FX_CUT) and a stab (_STAB, HF_FX_STAB): the game's own blood
 *                     (blood_level01_hitfx, which Combat_DealHit makes at every hit but low, 50-80 cm under our
 *                     contact), a one-shot ClientFX with no parent at the contact (CLIENTFX_CREATESTRUCT +0x11c = the
 *                     world place, as the game's own), kept within HF_FX_R of the orc's axis; SOMVR_COMBAT_HITFX_ON=orc:
 *                     on the orc's object at the contact's offset (+0x118 node -1). SOMVR_FINISH's FX sites.
 *   SOMVR_COMBAT_HITLOG=0|1|2 (test only) the game's sound posts by name (SOMVR_BOW_SNDLOG's patch of AK's PostEvent
 *                     callers) and the FX it makes (SOMVR_VERBS_GLOW's hook, when on) within HF_LOG_S of each of our
 *                     hits, tagged with the hit's kind: `HITSND`, `HITFX` lines; 2: also each new sound name once, all
 *                     the time (`BOWSND ... new`: the orcs' blows, the fight's voices; 96 names at most).
 * Test words (SOMVR_HANDS_TEST): `fight bite on|off`, `fight bite hold MS [EASE_MS [DEG]]`, `fight bite dumps N` (the
 * next N landed cuts marked for the layer's dumps: SOMVR_XR_DUMP_MARKED), `fight hitfx on|off` (the headless pictures).
 * Lines: `CONFIG hitfeel ...`, `BITE t=... start|free|end ...`,
 * `HITFEEL t=... sound|fx ...`, `HITFEEL END ...`. */

#define HF_LOG_S 0.6      /* the hit log's window after each of our hits */
#define HF_FRESH_S 0.1    /* a bite starts from the drawn blade of an arms frame at most this old */
#define HF_LINES 300
#define HF_FX_R 20.0f     /* cm: the FX at most this far off the struck orc's axis (on its body) */
#define HF_SND_CUT "cbt_pris_kd_imp"   /* the defaults of SOMVR_COMBAT_HITSND_* and _HITFX_* (59.2, 59.3) */
#define HF_SND_SLAP "fol_cmn_nudge"
#define HF_SND_STAB "cbt_imp_atk_biped"
#define HF_FX_CUT "fx/common/blood/blood_level01_hitfx.fxp" /* the game's own at every Combat_DealHit, but not at our contact */
#define HF_FX_STAB "fx/common/blood/blood_level01_hitfx.fxp"

static const char *const g_hf_kind_w[SC_KIND_N] = { "none", "cut", "slap", "tap", "stab" };
static struct {
	int bite, snd, fx, log;      /* the switches */
	int fx_orc;                  /* SOMVR_COMBAT_HITFX_ON=orc: on the orc's object at the contact's offset (else the world) */
	int ok_fx;                   /* SOMVR_FINISH's FX sites matched (df_fx_check) */
	_Atomic int req;             /* `fight bite on|off` (the writer thread): 1 + on, 0 = none */
	_Atomic uint32_t req_hold_b, req_ease_b, req_deg_b; /* `fight bite hold`: ms, ms, deg (UINT32_MAX = kept) */
	_Atomic int req_seq;
	int req_seen;
	_Atomic int req_fx;          /* `fight hitfx on|off`: 1 + on */
	_Atomic int req_dumps;       /* `fight bite dumps N`: 1 + N */
	int dumps;                   /* landed cuts left to mark for the layer's dumps (SOMVR_XR_DUMP_MARKED, tests) */
	double dump_t[2];            /* the dumps after a marked cut: inside the bite's hold, and the FX grown (0 = none) */
	swb_cfg cfg;
	swb_state st;
	/* the drawn blade of the last arms frame (the grips' space): a hit in this frame's combat bites from it */
	int d_have;
	double d_t;
	sv_vec3 d_p, d_shift;
	sv_quat d_q;
	float P[3];                  /* the held point (world, cm) */
	uintptr_t orc;               /* the bitten orc's Combat */
	int last_phase;
	char sound[SC_KIND_N][64];   /* SOMVR_COMBAT_HITSND_* (index SC_KIND_*) */
	char fxname[SC_KIND_N][128]; /* SOMVR_COMBAT_HITFX_* */
	uint64_t frames_held, frames_eased, applied, cancels, stale, played[SC_KIND_N], snd_fail[SC_KIND_N], fx_made[SC_KIND_N],
		fx_fail, hits[SC_KIND_N], bites_frees, lines;
	float peak_deg;
	/* the hits' blade against the held point while a bite holds (hf_blade): frames, the largest distance (cm) */
	uint64_t chk_n, chk_all;
	float chk_max, chk_all_max;
} g_hf;
static _Atomic int64_t g_hf_log_until_ms; /* SOMVR_COMBAT_HITLOG: the window after our last hit (mono ms) */
static _Atomic int g_hf_log_kind;
static _Atomic double g_hf_log_t;

static int hf_log_ok(void) { return g_hf.lines < HF_LINES ? (int)++g_hf.lines : 0; }

/* the world's point P (cm) in the arms' grip space of a frame with this shift (sa_grip_to_model's world, inverted) */
static sv_vec3 hf_world_to_grip(const float P[3], sv_vec3 shift)
{
	sa_xf w = { { P[0] - shift.x, P[1] - shift.y, P[2] - shift.z }, { 0, 0, 0, 1 } };
	return sa_world_to_track(&g_arm.rig, w).p;
}

/* the haptic of a bite: a full pulse for the hold (at least 50 ms), the left hand too with both on the hilt */
static void hf_bite_haptic(void)
{
	float dur = g_hf.cfg.hold_s > 0.05f ? g_hf.cfg.hold_s : 0.05f;
	in_haptic_event(SOMVR_HAND_R, 1.0f, dur, SOMVR_HAP_COMBAT);
	if (g_bd.two.w > 0)
		in_haptic_event(SOMVR_HAND_L, 0.6f, dur, SOMVR_HAP_COMBAT);
}

/* arm_eval, right after swg_arm (the weight's offset is on the weapon node): the bite's extra turn on the right weapon
 * node about the grip, and this frame's drawn blade kept for a hit in this frame's combat */
static void hf_arm(const int done[2], const sa_xf gg[2], const int gv[2], const int bowed[2], sa_xf X, sv_vec3 shift)
{
	int rq = atomic_exchange(&g_hf.req, 0);
	if (rq) {
		g_hf.bite = rq - 1;
		logf_("BITE t=%.3f %s (trigger word)", cb_now(), g_hf.bite ? "on" : "off");
	}
	int rs = atomic_load(&g_hf.req_seq);
	if (rs != g_hf.req_seen) {
		g_hf.req_seen = rs;
		uint32_t e = atomic_load(&g_hf.req_ease_b), d = atomic_load(&g_hf.req_deg_b);
		g_hf.cfg.hold_s = (float)atomic_load(&g_hf.req_hold_b) * 1e-3f;
		if (e != UINT32_MAX)
			g_hf.cfg.ease_s = (float)e * 1e-3f;
		if (d != UINT32_MAX)
			g_hf.cfg.max_rad = (float)d * 0.01745329f;
		logf_("BITE t=%.3f hold %.0f ms, eased over %.0f ms, torn free past %.0f deg (trigger word)", cb_now(),
		      (double)g_hf.cfg.hold_s * 1e3, (double)g_hf.cfg.ease_s * 1e3, (double)g_hf.cfg.max_rad / 0.01745329);
	}
	g_hf.d_have = 0;
	const sa_chain *c = &g_arm.ch[SA_R];
	int ok = g_hf.bite && done[SA_R] && gv[SA_R] && !bowed[SA_R] && c->wpn >= 0 && g_aw.s[SA_R].used_held &&
		 bd_sword_in_hand() && g_arm.weight[SA_R] >= 1 && !(g_sf.ghost_a[SA_R] > 0);
	if (!ok) {
		if (g_hf.st.phase != SWB_IDLE) {
			g_hf.cancels++;
			if (hf_log_ok())
				logf_("BITE t=%.3f end: cancelled (%s)", cb_now(), !g_hf.bite ? "the switch is off" : g_sf.ghost_a[SA_R] > 0
				      ? "a surface holds the hand" : "no sword in the solved hand");
		}
		swb_cancel(&g_hf.st), g_hf.last_phase = SWB_IDLE;
		return;
	}
	sa_xf g = gg[SA_R];
	float zero[3] = { 0, 0, 0 }, e[4], q[4], b[3] = { 0, 0, 0 };
	swg_qexp(g_hsm.w_now ? g_hsm.w_rot_now : zero, e);
	sv_quat qw = sv_qnormalize(sv_qmul(sv_quat_of(e), sv_qnormalize(g.q)));
	double t = cb_now();
	int ph = SWB_IDLE, was = g_hf.st.phase;
	uint64_t gaps0 = g_hf.st.gaps;
	if (was != SWB_IDLE) {
		sv_vec3 Pg = hf_world_to_grip(g_hf.P, shift);
		float p[3] = { g.p.x, g.p.y, g.p.z }, P[3] = { Pg.x, Pg.y, Pg.z };
		sv_quat_to(qw, q);
		ph = swb_step(&g_hf.st, &g_hf.cfg, t, p, q, P, b);
		g_hf.frames_held += ph == SWB_HOLD, g_hf.frames_eased += ph == SWB_EASE;
		if (was == SWB_HOLD && ph == SWB_EASE) { /* the blade comes free: a short weaker beat */
			in_haptic_event(SOMVR_HAND_R, 0.5f, 0.04f, SOMVR_HAP_COMBAT);
			g_hf.bites_frees++;
			if (hf_log_ok())
				logf_("BITE t=%.3f free after %.0f ms: %s at %.1f deg off the drawn blade (the hand on, the blade eases "
				      "back over %.0f ms); the hits' blade passed %.1f cm from the held point at most in the %llu combat "
				      "frames held", t, (double)g_hf.st.held_s * 1e3, g_hf.st.capped ? "torn free" : "held its time",
				      (double)g_hf.st.peak / 0.01745329, (double)g_hf.cfg.ease_s * 1e3, (double)g_hf.chk_max,
				      (unsigned long long)g_hf.chk_n);
		}
		if (ph == SWB_IDLE && hf_log_ok())
			logf_("BITE t=%.3f end: %.0f ms after the hit, the largest turn %.1f deg, the hits' blade %.1f cm from the "
			      "held point at most in %llu combat frames held%s", t, (t - g_hf.st.t0) * 1e3,
			      (double)g_hf.st.peak / 0.01745329, (double)g_hf.chk_max, (unsigned long long)g_hf.chk_n,
			      g_hf.st.gaps > gaps0 ? " (ended by a gap: no arms frame for over 100 ms)" : "");
		g_hf.peak_deg = fmaxf(g_hf.peak_deg, g_hf.st.peak / 0.01745329f);
	}
	g_hf.last_phase = ph;
	sv_quat qd = qw;
	int turned = b[0] != 0 || b[1] != 0 || b[2] != 0;
	if (turned) {
		swg_qexp(b, e);
		qd = sv_qnormalize(sv_qmul(sv_quat_of(e), qw));
		/* the weapon node as if the drawn grip were turned by b: W' = gm3 (x) gm2^-1 (x) W (about the grip point) */
		sa_xf g2 = { g.p, qw }, g3 = { g.p, qd };
		sa_xf gm2 = sa_grip_to_model(&g_arm.rig, shift, X, g2), gm3 = sa_grip_to_model(&g_arm.rig, shift, X, g3);
		float bs = sz_body_scale();
		if (bs != 1.0f)
			gm2.p = (sv_vec3){ gm2.p.x / bs, gm2.p.y / bs, gm2.p.z / bs }, gm3.p = (sv_vec3){ gm3.p.x / bs, gm3.p.y / bs,
													gm3.p.z / bs };
		sa_xf W = sa_node_xf(&g_arm.pose[c->wpn]);
		sa_node_set(&g_arm.pose[c->wpn], sa_xf_mul(sa_xf_mul(gm3, sa_xf_inv(gm2)), W));
		g_hf.applied++;
	}
	g_hf.d_have = 1, g_hf.d_t = t, g_hf.d_p = g.p, g_hf.d_q = qd, g_hf.d_shift = shift;
}

/* SOMVR_XR_DUMP_MARKED (tests): this H4 frame's record marked, so the layer dumps the next stereo frame */
static void hf_dump_mark(const char *what, double t)
{
	if (!g_fr.rec)
		return;
	g_fr.rec->flags |= SOMVR_REC_DUMP;
	logf_("BITE t=%.3f dump mark %s: frame %llu", t, what, (unsigned long long)g_fr.frame);
}

/* the combat frame (H4), every frame: the test words' FX switch and dumps, a pending late dump */
static void hf_h4(double t)
{
	int f = atomic_exchange(&g_hf.req_fx, 0), d = atomic_exchange(&g_hf.req_dumps, 0);
	if (f)
		g_hf.fx = f - 1, logf_("HITFEEL t=%.3f fx %s (trigger word)", t, g_hf.fx ? "on" : "off");
	if (d)
		g_hf.dumps = d - 1, logf_("HITFEEL t=%.3f the next %d landed cuts marked for dumps (trigger word)", t, g_hf.dumps);
	for (int i = 0; i < 2; i++) /* one a frame (the layer dumps the frame after a marked record) */
		if (g_hf.dump_t[i] > 0 && t >= g_hf.dump_t[i]) {
			g_hf.dump_t[i] = 0, hf_dump_mark(i ? "0.5 s after the cut (the FX)" : "0.12 s after the cut (the hold)", t);
			break;
		}
}

/* the combat frame, with this frame's hits' blade (world): while a bite holds, how far its segment passes from the held
 * point (the drawn blade is the hits' blade: ~0 = what is drawn in the body is what the hits see) */
static void hf_blade(const somvr_blade *b, double t)
{
	(void)t;
	if (!g_hf.bite || g_hf.st.phase != SWB_HOLD || !b->valid)
		return;
	float d[3], v[3], L2 = 0, k = 0;
	for (int i = 0; i < 3; i++)
		d[i] = b->tip[i] - b->base[i], v[i] = g_hf.P[i] - b->base[i], L2 += d[i] * d[i], k += d[i] * v[i];
	k = L2 > 1e-6f ? k / L2 : 0;
	k = k < 0 ? 0 : k > 1 ? 1 : k;
	float e = 0;
	for (int i = 0; i < 3; i++)
		e += (v[i] - k * d[i]) * (v[i] - k * d[i]);
	e = sqrtf(e);
	g_hf.chk_n++, g_hf.chk_all++;
	g_hf.chk_max = fmaxf(g_hf.chk_max, e), g_hf.chk_all_max = fmaxf(g_hf.chk_all_max, e);
}

/* cb_combat: while a bite is on, the swing tracker is not fed (its stop is not the swing's end, its release no new one) */
static int hf_biting(void) { return g_hf.bite && g_hf.st.phase != SWB_IDLE; }

/* cb_hit, before its haptic: this hit will bite (its own haptic then instead of the hit's) */
static int hf_will_bite(int kind, int glance, int blk, int call)
{
	return g_hf.bite && call && sc_kind_bites(kind, glance, blk) && g_hf.st.phase == SWB_IDLE && g_hf.d_have &&
	       cb_now() - g_hf.d_t < HF_FRESH_S;
}

/* a one-shot game FX at the world point P on the struck orc's object (its frame: the offset), SOMVR_FINISH's sites */
static int hf_fx_at(const char *name, uintptr_t hobj, const float P[3], double t, char *why_out, size_t cap)
{
	float op[3], oq[4];
	uintptr_t os = peek_ptr(g_fig.objsys_pp), vt = vptr_of((void *)os), shell = peek_ptr(g_fig.shell_pp);
	uintptr_t mgr = shell ? peek_ptr(shell + FI_SH_FXMGR) : 0;
	const char *why = !g_hf.ok_fx ? "an FX site did not match" : !hobj ? "no orc object"
			  : !peek(hobj + OBJ_POS, op, sizeof op) || !peek(hobj + OBJ_ROT, oq, sizeof oq) || !vec_ok(op, 1e7f) ||
					  !vec_ok(oq, 2.0f) ? "the orc's place is not readable"
			  : !os || vt != g_fig.vt_objsys || vt + 0x118 != g_fi_sites[FIS_OBJSYS_MH].addr ||
					  peek_ptr(vt + 0x118) != g_fi_sites[FIS_OBJSYS_MH].target ? "the object system is not the checked one"
			  : !mgr ? "no ClientFX manager" : NULL;
	if (why) {
		snprintf(why_out, cap, "%s", why);
		return 0;
	}
	sv_quat r = sv_qnormalize(sv_quat_of(oq));
	sv_vec3 local = sv_qrot(sv_qconj(r), (sv_vec3){ P[0] - op[0], P[1] - op[1], P[2] - op[2] });
	if (!g_hf.fx_orc) /* in the world, as the game makes its blood at a hit (no parent: the offset is the place) */
		local = (sv_vec3){ P[0], P[1], P[2] };
	uintptr_t par = g_hf.fx_orc ? hobj : 0, mh = par ? FI_FN(FIS_OBJSYS_MH, fi_mh_fn)(os, par) : 0;
	static unsigned char cs[0x400] __attribute__((aligned(16))); /* CLIENTFX_CREATESTRUCT (>= 0x1c8 bytes); game thread */
	memset(cs, 0, sizeof cs);
	FI_FN(FIS_FX_CSINIT, fi_cs_fn)(cs, name, mh, 0u /* one shot */, par);
	int32_t node = -1;
	float off[3] = { local.x, local.y, local.z };
	memcpy(cs + 0x118, &node, sizeof node), memcpy(cs + 0x11c, off, sizeof off);
	long rc = FI_FN(FIS_FX_CREATE, fi_create_fn)(mgr + 0x30, NULL, cs, 1, 0, 0);
	FI_FN(FIS_FX_CSFREE, fi_csfree_fn)(cs);
	snprintf(why_out, cap, "rc=%ld, at %.1f,%.1f,%.1f (%s)", rc, (double)off[0], (double)off[1], (double)off[2],
		 g_hf.fx_orc ? "the orc's frame" : "the world");
	(void)t;
	return 1;
}

/* cb_hit, after Combat_DealHit (call: the game was called; landed: it did damage or killed): the bite, our sound, the FX,
 * the log's window */
static void hf_hit(const struct cb_orc *o, const sc_contact *ct, int kind, int bite, int glance, int blk, int call,
		   int landed, const somvr_blade *b, double t)
{
	(void)call;
	g_hf.hits[kind]++;
	if (bite) {
		float P[3];
		for (int i = 0; i < 3; i++)
			P[i] = b->base[i] + ct->along * (b->tip[i] - b->base[i]); /* on the drawn blade, in the body */
		sv_vec3 Pg = hf_world_to_grip(P, g_hf.d_shift);
		float p[3] = { g_hf.d_p.x, g_hf.d_p.y, g_hf.d_p.z }, Pa[3] = { Pg.x, Pg.y, Pg.z }, q[4];
		sv_quat_to(g_hf.d_q, q);
		int started = landed && swb_start(&g_hf.st, t, p, q, Pa);
		if (started) {
			memcpy(g_hf.P, P, sizeof P), g_hf.orc = o->c, g_hf.chk_n = 0, g_hf.chk_max = 0;
			hf_bite_haptic();
		} else {
			in_haptic_event(SOMVR_HAND_R, 1.0f, 0.2f, SOMVR_HAP_COMBAT); /* the hit's own pulse after all */
		}
		if (hf_log_ok())
			logf_("BITE t=%.3f %s orc=%#lx: a cut at %.0f %% along the blade (u %.2f), the point %.1f,%.1f,%.1f %.1f cm from "
			      "the grip; hold %.0f ms at most, torn free past %.0f deg, eased over %.0f ms", t,
			      started ? "start" : landed ? "refused" : "none (the hit did no damage)", (unsigned long)o->c,
			      (double)ct->along * 100, (double)ct->u, (double)P[0], (double)P[1], (double)P[2],
			      (double)sqrtf((Pa[0] - p[0]) * (Pa[0] - p[0]) + (Pa[1] - p[1]) * (Pa[1] - p[1]) +
					    (Pa[2] - p[2]) * (Pa[2] - p[2])),
			      (double)g_hf.cfg.hold_s * 1e3, (double)g_hf.cfg.max_rad / 0.01745329, (double)g_hf.cfg.ease_s * 1e3);
	}
	if (!landed || glance || blk)
		return;
	if (g_hf.dumps > 0 && kind == SC_KIND_CUT) /* tests: 0.12 s after (inside a long test hold: a dump's own stall would
						     * end a bite as a gap) and 0.5 s after (the FX grown) */
		g_hf.dumps--, g_hf.dump_t[0] = t + 0.12, g_hf.dump_t[1] = t + 0.5;
	if (g_hf.snd && g_hf.sound[kind][0]) {
		sv_vec3 hw;
		sv_quat hq;
		somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hw, &hq);
		uint64_t before = g_sl.sounds;
		sl_sound(g_hf.sound[kind], (sv_vec3){ ct->point[0], ct->point[1], ct->point[2] }, hw);
		int ok = g_sl.sounds > before;
		g_hf.played[kind] += ok, g_hf.snd_fail[kind] += !ok;
		if (hf_log_ok())
			logf_("HITFEEL t=%.3f sound '%s' (a %s) at the contact %.1f,%.1f,%.1f: %s", t, g_hf.sound[kind], g_hf_kind_w[kind],
			      (double)ct->point[0], (double)ct->point[1], (double)ct->point[2], ok ? "played" : "NOT played (0)");
	}
	if (g_hf.fx && g_hf.fxname[kind][0]) {
		char why[160];
		/* the point on the drawn blade at the contact (the contact's own point is on the hit capsule's surface), kept
		 * within HF_FX_R of the orc's axis (a fast cut's blade may be through it by this frame): on the body */
		float P[3];
		for (int i = 0; i < 3; i++)
			P[i] = b->base[i] + ct->along * (b->tip[i] - b->base[i]);
		float dx = P[0] - o->pos[0], dz = P[2] - o->pos[2], dh = sqrtf(dx * dx + dz * dz);
		if (dh > HF_FX_R)
			P[0] = o->pos[0] + dx * HF_FX_R / dh, P[2] = o->pos[2] + dz * HF_FX_R / dh;
		int made = hf_fx_at(g_hf.fxname[kind], o->hobj, P, t, why, sizeof why);
		g_hf.fx_made[kind] += made, g_hf.fx_fail += !made;
		if (hf_log_ok())
			logf_("HITFEEL t=%.3f fx '%s' (a %s) on orc %#lx at %.1f,%.1f,%.1f: %s%s", t, g_hf.fxname[kind],
			      g_hf_kind_w[kind], (unsigned long)o->hobj, (double)P[0], (double)P[1], (double)P[2],
			      made ? "made, " : "not made: ", why);
	}
}

/* cb_hit, before Combat_DealHit (SOMVR_COMBAT_HITLOG): the log's window opens (the game's own hit sound and FX come
 * inside the call) */
static void hf_log_open(int kind, double t)
{
	if (!g_hf.log)
		return;
	atomic_store(&g_hf_log_kind, kind), atomic_store(&g_hf_log_t, t);
	atomic_store_explicit(&g_hf_log_until_ms, (int64_t)(mono() * 1e3 + HF_LOG_S * 1e3), memory_order_relaxed);
}

/* bs_post (SOMVR_COMBAT_HITLOG): a sound posted by name within the window of our last hit */
static int hf_log_window(double *since, int *kind)
{
	if (!g_hf.log || (int64_t)(mono() * 1e3) >= atomic_load_explicit(&g_hf_log_until_ms, memory_order_relaxed))
		return 0;
	*since = cb_now() - atomic_load(&g_hf_log_t), *kind = atomic_load(&g_hf_log_kind);
	return 1;
}

/* the trigger word `fight bite on|off` or `fight bite hold MS [EASE_MS [DEG]]` (the writer thread; the hold for the
 * headless pictures: a dump lands within a long one) */
static const char *hf_word(const char *w)
{
	char a[16] = "";
	float ms = -1, ease = -1, deg = -1;
	if (!g_bd.test)
		return "fight bite (SOMVR_HANDS_TEST is off: ignored)";
	int n = sscanf(w, "%*s %15s %f %f %f", a, &ms, &ease, &deg);
	if (n == 1 && (!strcmp(a, "on") || !strcmp(a, "off")) && !strncmp(w, "hitfx ", 6))
		return atomic_store(&g_hf.req_fx, 1 + !strcmp(a, "on")), "fight hitfx";
	if (n == 1 && (!strcmp(a, "on") || !strcmp(a, "off"))) {
		atomic_store(&g_hf.req, 1 + !strcmp(a, "on"));
		return "fight bite";
	}
	if (n >= 2 && !strcmp(a, "dumps") && ms >= 0 && ms <= 20)
		return atomic_store(&g_hf.req_dumps, 1 + (int)ms), "fight bite dumps";
	if (n >= 2 && !strcmp(a, "hold") && ms >= 0 && ms <= 2000 && ease <= 2000 && deg <= 90) {
		atomic_store(&g_hf.req_hold_b, (uint32_t)(ms + 0.5f));
		atomic_store(&g_hf.req_ease_b, ease >= 0 ? (uint32_t)(ease + 0.5f) : UINT32_MAX);
		atomic_store(&g_hf.req_deg_b, deg > 0 ? (uint32_t)(deg + 0.5f) : UINT32_MAX);
		atomic_fetch_add(&g_hf.req_seq, 1);
		return "fight bite hold";
	}
	return "fight bite (not on|off|hold MS [EASE_MS [DEG]]: ignored)";
}

/* the VR menu's Blade bite row (vm_apply_key, vm_start): settings' bite 0 off, 1 on, live */
static const char *hf_vm_set(const somvr_settings *s)
{
	if (!g_arm.on)
		return " (SOMVR_ARMS is off: no sword in the hand)";
	int on = s->bite != 0;
	if (on != g_hf.bite) {
		logf_("BITE t=%.3f %s -> %s (the VR menu)", cb_now(), g_hf.bite ? "on" : "off", on ? "on" : "off");
		g_hf.bite = on;
		if (!on)
			swb_cancel(&g_hf.st);
	}
	return " (a full cut stops in the body for a moment)";
}
static void hf_vm_settings(somvr_settings *s) { s->bite = (uint8_t)(g_hf.bite != 0); }
static uint32_t hf_vm_avail(void) { return g_arm.on && g_cb.mode ? SOMVR_VMA_BITE : 0u; }

static void hf_config(int *bad)
{
	static const char *const onoff_w[] = { "off", "on" }, *const log_w[] = { "0", "1", "2" };
	memset(&g_hf, 0, sizeof g_hf);
	swb_defaults(&g_hf.cfg);
	swb_reset(&g_hf.st);
	g_hf.bite = env_word("SOMVR_COMBAT_BITE", onoff_w, 2, 0);
	g_hf.cfg.hold_s = env_float("SOMVR_COMBAT_BITE_MS", 60, 0, 200, bad) * 1e-3f;
	g_hf.cfg.ease_s = env_float("SOMVR_COMBAT_BITE_EASE_MS", 100, 0, 400, bad) * 1e-3f;
	g_hf.cfg.max_rad = env_float("SOMVR_COMBAT_BITE_DEG", 30, 5, 60, bad) * 0.01745329f;
	if (g_hf.bite && (!g_arm.on || !g_cb.mode))
		*bad = 1, logf_("WARN SOMVR_COMBAT_BITE=on needs SOMVR_ARMS=1 and SOMVR_COMBAT (the drawn sword, our hits): no bite"),
		g_hf.bite = 0;
	g_hf.snd = env_word("SOMVR_COMBAT_HITSND", onoff_w, 2, 0);
	g_hf.fx = env_word("SOMVR_COMBAT_HITFX", onoff_w, 2, 0);
	static const char *const on_w[] = { "world", "orc" };
	g_hf.fx_orc = env_word("SOMVR_COMBAT_HITFX_ON", on_w, 2, 0);
	g_hf.log = env_word("SOMVR_COMBAT_HITLOG", log_w, 3, 0);
	static const char *const snd_k[SC_KIND_N] = { NULL, "SOMVR_COMBAT_HITSND_CUT", "SOMVR_COMBAT_HITSND_SLAP",
						      "SOMVR_COMBAT_HITSND_TAP", "SOMVR_COMBAT_HITSND_STAB" };
	static const char *const fx_k[SC_KIND_N] = { NULL, "SOMVR_COMBAT_HITFX_CUT", NULL, NULL, "SOMVR_COMBAT_HITFX_STAB" };
	/* the defaults (the game's own events and FX, found with SOMVR_COMBAT_HITLOG; re-xr-pipeline.md 59.2-59.3): a cut a
	 * body's heavy impact over the game's own hit sound, a slap a thin foley nudge, a stab the blow-on-a-body impact;
	 * blood at a cut, sparks at a stab */
	static const char *const snd_d[SC_KIND_N] = { NULL, HF_SND_CUT, HF_SND_SLAP, NULL, HF_SND_STAB };
	static const char *const fx_d[SC_KIND_N] = { NULL, HF_FX_CUT, NULL, NULL, HF_FX_STAB };
	for (int k = 1; k < SC_KIND_N; k++) {
		const char *v = getenv(snd_k[k]);
		snprintf(g_hf.sound[k], sizeof g_hf.sound[k], "%s", !v || !v[0] ? (snd_d[k] ? snd_d[k] : "") : strcmp(v, "off") ? v : "");
		v = fx_k[k] ? getenv(fx_k[k]) : NULL;
		snprintf(g_hf.fxname[k], sizeof g_hf.fxname[k], "%s", !v || !v[0] ? (fx_d[k] ? fx_d[k] : "") : strcmp(v, "off") ? v : "");
		for (char *p = g_hf.fxname[k]; *p; p++) /* the game's paths take '\\'; the environment may give '/' */
			if (*p == '/')
				*p = '\\';
	}
	if (!getenv(snd_k[SC_KIND_TAP])) /* a tap sounds as a slap unless it has its own */
		memcpy(g_hf.sound[SC_KIND_TAP], g_hf.sound[SC_KIND_SLAP], sizeof g_hf.sound[0]);
	char fxwhy[200] = "";
	if (g_hf.fx)
		g_hf.ok_fx = df_fx_check(fxwhy, sizeof fxwhy);
	if (g_hf.snd && !g_sl.sound)
		logf_("CONFIG hitfeel: SOMVR_COMBAT_HITSND needs SOMVR_SLOTS' sound (our Wwise object): not played");
	if (g_hf.log)
		bs_install_log(); /* SOMVR_BOW_SNDLOG's patch (somvr_quiver_game.c), once */
	logf_("CONFIG hitfeel bite=%s (SOMVR_COMBAT_BITE: a landed cut holds the drawn blade at the contact %.0f ms at most, "
	      "torn free past %.0f deg, eased back over %.0f ms; the VR menu's Blade bite row) hitsnd=%s (cut '%s', slap '%s', "
	      "tap '%s', stab '%s') hitfx=%s (cut '%s', stab '%s', in %s%s%s) hitlog=%d%s",
	      g_hf.bite ? "on" : "off", (double)g_hf.cfg.hold_s * 1e3, (double)g_hf.cfg.max_rad / 0.01745329,
	      (double)g_hf.cfg.ease_s * 1e3, g_hf.snd ? "on" : "off", g_hf.sound[1], g_hf.sound[2], g_hf.sound[3], g_hf.sound[4],
	      g_hf.fx ? "on" : "off", g_hf.fxname[1], g_hf.fxname[4], g_hf.fx_orc ? "the orc's frame" : "the world",
	      g_hf.fx && !g_hf.ok_fx ? "; WARN: none: " : "",
	      g_hf.fx && !g_hf.ok_fx ? fxwhy : "", g_hf.log, g_hf.log ? " (test only: the game's sounds and FX after each of our hits)" : "");
	if (g_hf.fx && !g_hf.ok_fx)
		*bad = 1;
}

static void hf_end_log(void)
{
	logf_("HITFEEL END bite=%s: hits cut %llu slap %llu tap %llu stab %llu; bites %llu (freed %llu, torn free %llu, "
	      "refused %llu, cancelled %llu, gaps %llu), frames held %llu eased %llu, node turns %llu, the largest turn %.1f deg, "
	      "the hits' blade off the held point at most %.1f cm in %llu combat frames held; "
	      "sounds played cut %llu slap %llu tap %llu stab %llu (not %llu); fx made cut %llu stab %llu (not %llu)",
	      g_hf.bite ? "on" : "off", (unsigned long long)g_hf.hits[1], (unsigned long long)g_hf.hits[2],
	      (unsigned long long)g_hf.hits[3], (unsigned long long)g_hf.hits[4], (unsigned long long)g_hf.st.bites,
	      (unsigned long long)g_hf.bites_frees, (unsigned long long)g_hf.st.caps, (unsigned long long)g_hf.st.refused,
	      (unsigned long long)g_hf.cancels, (unsigned long long)g_hf.st.gaps, (unsigned long long)g_hf.frames_held,
	      (unsigned long long)g_hf.frames_eased, (unsigned long long)g_hf.applied, (double)g_hf.peak_deg,
	      (double)g_hf.chk_all_max, (unsigned long long)g_hf.chk_all,
	      (unsigned long long)g_hf.played[1], (unsigned long long)g_hf.played[2], (unsigned long long)g_hf.played[3],
	      (unsigned long long)g_hf.played[4],
	      (unsigned long long)(g_hf.snd_fail[1] + g_hf.snd_fail[2] + g_hf.snd_fail[3] + g_hf.snd_fail[4]),
	      (unsigned long long)g_hf.fx_made[1], (unsigned long long)g_hf.fx_made[4], (unsigned long long)g_hf.fx_fail);
}
