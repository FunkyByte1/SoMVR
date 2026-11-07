/* somvr_bleed_game.c - build 42 (som-b42-bleed, docs/re-xr-pipeline.md 191, handoff/HANDOFF-bleed42.md): the graug's
 * bleed (Torvin's "Hunting Partners", "Bleed the Graug") made findable and done by the hand. Included by somvr.c after the
 * cues' file (it uses the verbs' selection vb_pick and their hamstring node, the beasts' bone nodes, the threat's glows,
 * the brand's node log for the probe, the crouch's state).
 *
 * Build 30 (131.5, SOMVR_GRAUG_BLEED) made a blade contact on a graug the game's hamstring (X) whenever the game's own
 * selection offered it at that moment. On build 41 the mission's drill never met the offer (re-xr-pipeline.md 190.1 (a):
 * 8 placements a boot, 135-162 cm behind it) and the player has no cue for where it is: the flat game's prompt is hidden
 * in VR (SOMVR_GAME_PROMPTS=off). Here:
 *
 *   the watch (always with SOMVR_GRAUG_BLEED): 5 times a second the nearest graug within BL_NEAR_CM is asked the same
 *       question (vb_pick, the hamstring node) - the answer kept for the cue and logged when it changes, with where
 *       Talion is: root to root, how far behind it, his own facing toward it, crouched, its feet.
 *   SOMVR_GRAUG_BLEED_CUE=on|off (code default on)  while the game offers the bleed, the wraith-blue glow (the grab and
 *       mount cues' kind: "the game lets you do this here") on the back of the leg it would cut, faint; full with a tick in
 *       the sword hand when the blade is within BL_BLADE_CM of it. Nothing while it is not offered.
 *   SOMVR_GRAUG_BLEED_LEG=on|off (code default on)  the cut is at its legs: while the bleed is offered, a contact on the
 *       graug's thighs or shins (the beasts' bone capsules; with no chain: BL_LEG_CM round the shin) is the bleed; a contact elsewhere on
 *       it is a touch only (no hit: the flat game in the crouch there has no other attack, and an ordinary hit wakes it).
 *       off = build 30: any contact while offered.
 *   SOMVR_GRAUG_BLEED_HANDS=on|off (code default on)  the stealth's hands for the bleed. The game offers its hamstring only
 *       with the stealth's active item (Acharn: the crouch's PC_Stealth makes it active and sheathes the sword); boot
 *       20261107-030755-1's attack log: "Rejected Attack: PC_Hamstring_CaveTroll_Sitting_RightLeg - Active Item" standing,
 *       offered the moment Talion crouched, 80-190 cm behind it and up to 90 deg to its side. The sword drawn WHILE
 *       crouched (SOMVR_CROUCH_DRAW=keep, build 38) is the game's own draw - the sword the active item, the crouch held:
 *       the hamstring refused, the game's X an ordinary combo (build 41's Hunting Partners: crouch, then draw, then the
 *       cut, 3 boots). So while crouched behind an unaware graug with the game's sword drawn and the bleed refused, the
 *       slots put the game's sword away and make Acharn active (what PC_Stealth does when Talion crouches with the sword
 *       out); the attach keep holds the VR sword in the hand (SOMVR_SLOTS_KEEP=attach) - the state of a draw before the
 *       crouch. Once per crouch.
 * Test word (SOMVR_HANDS_TEST): `verbs bleed probe S` - every ask for S seconds logged with its geometry and the game's
 * own reasons (the brand's attack log, SOMVR_VERBS_BRAND_NODELOG=on with _FILTER=Hamstring: its window kept open). */

#define BL_ASK_S 0.2       /* the watch's asks */
#define BL_NEAR_CM 450.0f  /* a graug within this of Talion (root to root, flat) is asked */
#define BL_FRESH_S 0.5     /* the glow shows while the last ask (within this) said yes */
#define BL_BLADE_CM 45.0f  /* the glow full (and the tick) with the blade this near its point */
#define BL_LEG_CM 60.0f    /* a contact this near a shin's axis or a foot is on the leg (the shin capsule's 38 + slack) */
#define BL_OUT_CM 34.0f    /* the glow sits this far out of the shin's axis toward the eyes: at the leg's surface (the shin
			    * capsule 38; build 42b: 22 put it inside the calf, read as a smudge behind the skin) */
#define BL_HANDS_CM 320.0f /* SOMVR_GRAUG_BLEED_HANDS: within this of the graug's root */
#define BL_ALONG 0.65f     /* ... and this far from the knee toward the foot (the calf, toward the ankle the lesson names) */

static struct {
	int on, cue, leg;           /* the watch (SOMVR_GRAUG_BLEED), SOMVR_GRAUG_BLEED_CUE, SOMVR_GRAUG_BLEED_LEG */
	double next_t, ask_t;       /* the next ask; the last answer's time */
	uintptr_t c, hobj, atk;     /* the graug asked last; the attack offered (0 = none) */
	int side;                   /* the leg of the attack offered: 0 right, 1 left */
	char why[96], atk_name[96];
	int was;                    /* the last logged answer: -1 none yet, 0 no, 1 offered */
	char was_why[96];
	double probe_until;         /* `verbs bleed probe S`: every ask logged */
	float glow, glow_pos[3];    /* the cue's level (faded) and place (world cm) */
	double glow_t;
	int near_was;               /* the blade was near the glow last frame (the tick on its rise) */
	float cue_cm, cue_deg, cue_level; /* build 42b: SOMVR_GRAUG_BLEED_CUE_CM / _DEG / _LEVEL - its size and brightness */
	int hands, hands_asked;     /* SOMVR_GRAUG_BLEED_HANDS; asked in this crouch */
	double hands_t;             /* ... when */
	uint64_t swap_touches;      /* build 42b: contacts while the swap was due: touches, no hit */
	uint64_t asks, offers, changes, glow_frames, ticks, leg_hits, body_touches, probe_lines, hands_reqs;
} g_bl;
static _Atomic int g_bl_req_where; /* `verbs bleed where` asked (the next stereo frame answers) */
static void bl_where(double t);

static void bl_start(int *bad)
{
	memset(&g_bl, 0, sizeof g_bl);
	g_bl.was = -1;
	g_bl.on = g_vb.mode == VB_ON && g_vb.bleed;
	g_bl.cue = env_word("SOMVR_GRAUG_BLEED_CUE", (const char *const[]){ "off", "on" }, 2, 1);
	g_bl.leg = env_word("SOMVR_GRAUG_BLEED_LEG", (const char *const[]){ "off", "on" }, 2, 1);
	g_bl.hands = env_word("SOMVR_GRAUG_BLEED_HANDS", (const char *const[]){ "off", "on" }, 2, 1);
	g_bl.cue_cm = env_float("SOMVR_GRAUG_BLEED_CUE_CM", 42, 10, 120, bad);   /* build 42b: the glow's quad, at least */
	g_bl.cue_deg = env_float("SOMVR_GRAUG_BLEED_CUE_DEG", 9, 2, 30, bad);    /* ... and at least this much of the view */
	g_bl.cue_level = env_float("SOMVR_GRAUG_BLEED_CUE_LEVEL", 0.8f, 0.2f, 1, bad); /* offered (full: 1 with the blade near) */
	logf_("CONFIG verbs build 42: graug_bleed_cue=%s (SOMVR_GRAUG_BLEED_CUE%s), graug_bleed_leg=%s (SOMVR_GRAUG_BLEED_LEG%s): "
	      "the watch asks the game's hamstring for the nearest graug within %.0f cm every %.1f s%s%s", g_bl.cue ? "on" : "off",
	      getenv("SOMVR_GRAUG_BLEED_CUE") ? "" : "; unset: on", g_bl.leg ? "on" : "off",
	      getenv("SOMVR_GRAUG_BLEED_LEG") ? "" : "; unset: on", (double)BL_NEAR_CM, BL_ASK_S,
	      g_bl.cue ? "; while offered the blue glow on the back of its leg (full + a tick with the blade near)" : "",
	      g_bl.leg ? "; the cut at its legs (a contact elsewhere on it a touch while offered)" : "");
	logf_("CONFIG verbs build 42: graug_bleed_hands=%s (SOMVR_GRAUG_BLEED_HANDS%s): %s", g_bl.hands ? "on" : "off",
	      getenv("SOMVR_GRAUG_BLEED_HANDS") ? "" : "; unset: on", g_bl.hands ? "crouched behind an unaware graug with the "
	      "game's sword drawn and the bleed refused: the game's sword put away and Acharn active (the stealth's hands), the "
	      "VR sword kept in the hand by the attach keep - once per crouch" : "the game's sword as the draw left it");
}

/* the leg's two nodes (0 right, 1 left): its lower leg's start (the knee) and the foot */
static int bl_leg(uintptr_t c, uintptr_t hobj, int side, float knee[3], float foot[3])
{
	struct bz_one *e = bz_get(c, hobj, cb_now());
	uintptr_t m = peek_ptr(g_som.model_pp);
	if (!e || !m)
		return 0;
	if (!e->nodes_st) {
		float r[3];
		if (!peek(hobj + OBJ_POS, r, sizeof r))
			return 0;
		bz_nodes(e, r, cb_now());
	}
	return bz_limb_pos(e, m, side ? BZN_L_LEGL : BZN_R_LEGL, knee) && bz_limb_pos(e, m, side ? BZN_L_FOOT : BZN_R_FOOT, foot);
}

/* Talion's own yaw (rad; his object's rotation) */
static int bl_talion_yaw(uintptr_t pc, float *yaw)
{
	uintptr_t me = pc ? peek_ptr(pc + C_HOBJ) : 0;
	float q[4] = { 0, 0, 0, 1 };
	if (!me || !peek(me + OBJ_ROT, q, sizeof q) || !vec_ok(q, 2.0f))
		return 0;
	*yaw = sv_heading(sv_qnormalize(sv_quat_of(q)));
	return 1;
}

/* one ask's line: the answer and where Talion stands */
static void bl_line(const char *what, const struct cb_orc *o, const float root[3], uintptr_t pc, double t)
{
	float gyaw = 0, behind = vb_off_facing(o, root, &gyaw), my = 0, face = 999, head = 999;
	float dx = o->pos[0] - root[0], dz = o->pos[2] - root[2], b = atan2f(dx, dz);
	if (bl_talion_yaw(pc, &my))
		face = sv_wrap_pi(b - my) * 57.29578f;
	head = sv_wrap_pi(b - g_inp.hv) * 57.29578f;
	float kn[3], ft[3], fd[2] = { -1, -1 };
	for (int s = 0; s < 2; s++)
		if (bl_leg(o->c, o->hobj, s, kn, ft))
			fd[s] = hypotf(ft[0] - root[0], ft[2] - root[2]);
	char an[96], sn[96];
	logf_("VERBS bleed watch t=%.3f graug %#llx ('%s', state '%s', health %.0f): %s - %.0f cm root to root (%.0f cm over), "
	      "%.0f deg off its facing (180 = behind), Talion's body %.0f deg off it (the head %.0f), crouched %d/%d, its feet R %.0f "
	      "L %.0f cm", t, (unsigned long long)o->c, vb_name(peek_ptr(o->c + VB_C_ATTACK), an, sizeof an),
	      vb_name(peek_ptr(o->c + VB_C_STATE), sn, sizeof sn), vb_health(o->c), what, hypotf(dx, dz), o->pos[1] - root[1],
	      behind, face, head, g_cr.held, g_cr.game, fd[0], fd[1]);
}

/* SOMVR_GRAUG_BLEED_HANDS' conditions for graug o: crouched within BL_HANDS_CM in its back half, it not at Talion, the
 * game's sword drawn and active */
static int bl_hands_due(uintptr_t pc, const struct cb_orc *o, const float *root)
{
	if (!g_bl.hands || !root || !pc || !(g_cr.held || g_cr.game > 0) || !g_sl.have_g || !g_sl.g.item[0] ||
	    g_sl.g.slot != SL_SLOT_SWORD || g_sl.g.sheathed)
		return 0;
	uintptr_t me = peek_ptr(pc + C_HOBJ);
	return hypotf(o->pos[0] - root[0], o->pos[2] - root[2]) <= BL_HANDS_CM && peek_ptr(o->c + FI_C_TARGET) != me &&
	       vb_off_facing(o, root, NULL) >= 90.0f;
}
/* ... due: the slots asked (once per crouch); 1 = due (asked now or before in this crouch) */
static int bl_hands_ask(uintptr_t pc, const struct cb_orc *o, const float *root, double t)
{
	if (!bl_hands_due(pc, o, root))
		return 0;
	if (!g_bl.hands_asked) {
		g_bl.hands_asked = 1, g_bl.hands_reqs++, g_bl.hands_t = t;
		atomic_store(&g_sl_req_stealth_hands, 1);
		if (vb_log_ok())
			logf_("VERBS bleed watch t=%.3f graug %#llx: crouched %.0f cm behind it with the game's sword drawn (the active "
			      "item; the bleed refused) - the stealth's hands asked (SOMVR_GRAUG_BLEED_HANDS)", t, (unsigned long long)o->c,
			      hypotf(o->pos[0] - root[0], o->pos[2] - root[2]));
	}
	return 1;
}
/* vb_bleed, the bleed refused at a contact: 1 = a touch, no hit - the stealth's hands are due or were asked a moment ago
 * (build 42b: boot 20261107-044911-1, the blade by its leg right after the crouched draw, before the watch's next ask,
 * dealt the ordinary hit; the swap made the bleed offered 7 s later) */
static int bl_refused_touch(uintptr_t pc, const struct cb_orc *o, double t)
{
	int due = bl_hands_ask(pc, o, g_vb.have_root ? g_vb.root : NULL, t) && t - g_bl.hands_t < 1.0;
	if (due)
		g_bl.swap_touches++;
	return due;
}

/* vb_frame (the game thread, every frame): the watch */
static void bl_watch(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t)
{
	if (g_bl.on && atomic_exchange(&g_bl_req_where, 0))
		bl_where(t);
	if (!(g_cr.held || g_cr.game > 0))
		g_bl.hands_asked = 0; /* (a new crouch: the stealth's hands may be asked again) */
	if (!g_bl.on || !g_vb.bleed || !g_vb.ok_tree || !pcomb || !pc || !root || t < g_bl.next_t)
		return;
	g_bl.next_t = t + BL_ASK_S;
	int gi = -1;
	float best = BL_NEAR_CM;
	for (int i = 0; i < n; i++) {
		float d = hypotf(o[i].pos[0] - root[0], o[i].pos[2] - root[2]);
		if (d < best && bz_is_graug(o[i].c, o[i].hobj))
			best = d, gi = i;
	}
	int probe = t < g_bl.probe_until;
	if (gi < 0) {
		if (g_bl.was >= 0 && vb_log_ok())
			logf_("VERBS bleed watch t=%.3f no graug within %.0f cm: the watch idle", t, (double)BL_NEAR_CM);
		g_bl.was = -1, g_bl.atk = 0, g_bl.c = 0;
		return;
	}
	uintptr_t node = vb_node_like(pcomb, g_vb.bleed_node), sy = 0;
	if (!node)
		return;
	if (probe)
		br_nl_open(t, 1.0); /* the game's own reasons (SOMVR_VERBS_BRAND_NODELOG=on): the window kept open */
	const char *why = "";
	uintptr_t atk = vb_pick(pcomb, pc, &o[gi], node, &sy, &why);
	g_bl.asks++, g_bl.ask_t = t;
	g_bl.c = o[gi].c, g_bl.hobj = o[gi].hobj, g_bl.atk = atk;
	snprintf(g_bl.why, sizeof g_bl.why, "%s", atk ? "" : why);
	if (atk) {
		g_bl.offers++;
		vb_name(atk, g_bl.atk_name, sizeof g_bl.atk_name);
		g_bl.side = strstr(g_bl.atk_name, "LeftLeg") != NULL;
	}
	/* SOMVR_GRAUG_BLEED_HANDS: crouched behind it, unaware, the game's sword drawn - the stealth's hands (sl_frame) */
	if (!atk)
		bl_hands_ask(pc, &o[gi], root, t);
	int now = atk != 0, changed = now != g_bl.was || (!now && strcmp(g_bl.why, g_bl.was_why));
	if (changed)
		g_bl.changes++;
	if ((changed && vb_log_ok()) || (probe && g_bl.probe_lines < 4000 && (g_bl.probe_lines++, 1))) {
		char what[200];
		if (now)
			snprintf(what, sizeof what, "OFFERED '%s'", g_bl.atk_name);
		else
			snprintf(what, sizeof what, "not offered (%s)", g_bl.why);
		bl_line(what, &o[gi], root, pc, t);
	}
	g_bl.was = now;
	snprintf(g_bl.was_why, sizeof g_bl.was_why, "%s", g_bl.why);
}

/* the bleed is offered now (the watch's last ask, fresh) on graug c */
static int bl_offered(uintptr_t c)
{
	return g_bl.on && g_bl.atk && g_bl.c && (!c || c == g_bl.c) && cb_now() - g_bl.ask_t < BL_FRESH_S;
}

/* vb_bleed (the contact loop): 1 = this contact is on the graug's legs (SOMVR_GRAUG_BLEED_LEG off: any contact). The
 * beasts' sweep names the capsule it met (bz_sweep): a thigh or a shin is the leg - the kneeling graug's thigh lies over
 * its shin, and a stab at the calf meets the thigh first (boot 20261107-040553-1: two contacts at the glow's line ~60-70
 * cm off the shin's axis, called "not on its legs" by the axis rule alone); else (no bone chain) the shin's axis */
static int bl_contact_on_leg(const struct cb_orc *o, const sc_contact *ct)
{
	if (!g_bl.leg || !ct)
		return 1;
	if (g_bz.ct_c == o->c && g_bz.ct_part <= -10) {
		int limb = -10 - g_bz.ct_part;
		return limb == SOMVR_BZL_R_THIGH || limb == SOMVR_BZL_L_THIGH || limb == SOMVR_BZL_R_SHIN || limb == SOMVR_BZL_L_SHIN;
	}
	if (g_bz.ct_c == o->c && g_bz.ct_part > -10)
		return 0; /* (its body, head, neck, tail or the facing's fallback capsule) */
	for (int s = 0; s < 2; s++) {
		float kn[3], ft[3], u, v;
		if (!bl_leg(o->c, o->hobj, s, kn, ft))
			continue;
		if (sc_seg_dist(ct->point, ct->point, kn, ft, &u, &v) <= BL_LEG_CM)
			return 1;
	}
	return 0;
}

/* vb_bleed: the game offers the bleed at this contact - 1 = the cut (on its legs), 0 = a touch elsewhere on it (no hit) */
static double g_bl_touch_line;
static int bl_contact(const struct cb_orc *o, const sc_contact *ct, double t)
{
	if (bl_contact_on_leg(o, ct)) {
		g_bl.leg_hits++;
		if (vb_log_ok())
			logf_("VERBS t=%.3f bleed: the blade on graug %#llx's leg at %.0f %.0f %.0f (its capsule %d: %s)", t,
			      (unsigned long long)o->c, ct->point[0], ct->point[1], ct->point[2], g_bz.ct_c == o->c ? g_bz.ct_part : 99,
			      !g_bl.leg ? "any contact (SOMVR_GRAUG_BLEED_LEG off)" : g_bz.ct_c == o->c && g_bz.ct_part <= -10 ? "a thigh or a shin" : "by the shin's axis");
		return 1;
	}
	g_bl.body_touches++;
	if (t - g_bl_touch_line > 1.0 && vb_log_ok())
		logf_("VERBS t=%.3f bleed: the blade on graug %#llx at %.0f %.0f %.0f (its capsule %d) - not on its legs: a touch, no hit "
		      "(the bleed is offered: the cut at the back of its leg; SOMVR_GRAUG_BLEED_LEG)", t, (unsigned long long)o->c,
		      ct->point[0], ct->point[1], ct->point[2], g_bz.ct_c == o->c ? g_bz.ct_part : 99);
	g_bl_touch_line = t;
	return 0;
}

/* th_frame (the game thread, a stereo frame): the cue's glow among the frame's (the blue row) */
static void bl_glow_add(struct th_glow *gl, int *ng)
{
	if (!g_bl.on)
		return;
	double t = cb_now();
	if (!g_bl.cue)
		return;
	float dt = g_bl.glow_t > 0 && t > g_bl.glow_t ? (float)(t - g_bl.glow_t) : 0, a = dt / 0.15f;
	g_bl.glow_t = t;
	float kn[3], ft[3], p[3], target = 0;
	int have = bl_offered(0) && !game_paused() && bl_leg(g_bl.c, g_bl.hobj, g_bl.side, kn, ft);
	int nearb = 0;
	if (have) {
		float ax[3], l2 = 0, v[3], vd = 0;
		for (int i = 0; i < 3; i++)
			ax[i] = ft[i] - kn[i], l2 += ax[i] * ax[i], p[i] = kn[i] + BL_ALONG * ax[i];
		for (int i = 0; i < 3; i++)
			v[i] = g_fr.head[i] - p[i], vd += v[i] * ax[i];
		float k = l2 > 1 ? vd / l2 : 0, vl = 0;
		for (int i = 0; i < 3; i++)
			v[i] -= k * ax[i], vl += v[i] * v[i];
		vl = sqrtf(vl);
		if (vl > 1)
			for (int i = 0; i < 3; i++)
				p[i] += v[i] * (BL_OUT_CM / vl); /* out of the shin toward the eyes: on the leg the player sees */
		memcpy(g_bl.glow_pos, p, sizeof p);
		/* the blade near it (the drawn sword's last frame) */
		if (t - g_cb.prev.t < 0.2) {
			float u, w;
			nearb = sc_seg_dist(g_cb.prev.base, g_cb.prev.tip, p, p, &u, &w) <= BL_BLADE_CM;
		}
		target = nearb ? 1.0f : g_bl.cue_level; /* build 42b: brighter than the grab cue's 0.55 - a big leg, 1-3 m off */
	}
	if (nearb && !g_bl.near_was) {
		haptic_play(SOMVR_HAND_R, SOMVR_HX_TICK, 1, 1);
		g_bl.ticks++;
	}
	g_bl.near_was = nearb;
	g_bl.glow += (target - g_bl.glow) * (a < 1 ? a : 1);
	if (!(g_bl.glow > 0.01f))
		return;
	th_glow_add(gl, ng, g_bl.glow_pos, g_bl.glow, dist3(g_bl.glow_pos, g_fr.head), SOMVR_THK_MELEE | SOMVR_THK_BLUE | BL_KIND);
	g_bl.glow_frames++;
}

/* th_frame: the bleed glow's quad (metres in the room): SOMVR_GRAUG_BLEED_CUE_CM, at least _DEG of the view at its
 * distance (cm) - 42 cm / 9 deg: ~19 deg wide at 1.3 m, ~10 at 2.5 m (the grab cue's 26 cm: 11 and 6) */
static float bl_glow_size_m(float dist_cm)
{
	float cm = fmaxf(g_bl.cue_cm, 2.0f * dist_cm * tanf(0.5f * g_bl.cue_deg * 0.01745329f));
	return cm * 0.01f / somvr_rig_scale(&g_rig);
}

/* `verbs bleed where` (bl_glow_add, the next stereo frame): the cut's point and the leg in the head's yaw frame (x right,
 * y up, z ahead, cm - the `hands test` frame), for a test hand's aimed cut */
static void bl_where(double t)
{
	float kn[3], ft[3], p[3], r[3];
	uintptr_t c = g_bl.c, ho = g_bl.hobj;
	int s = g_bl.atk ? g_bl.side : 0;
	if (!c || !bl_leg(c, ho, s, kn, ft) || !peek(ho + OBJ_POS, r, sizeof r)) {
		logf_("VERBS bleed where t=%.3f: no graug watched (none within %.0f cm)", t, (double)BL_NEAR_CM);
		return;
	}
	for (int i = 0; i < 3; i++)
		p[i] = kn[i] + BL_ALONG * (ft[i] - kn[i]);
	float hv = g_inp.hv, cs = cosf(hv), sn = sinf(hv), h[4][3];
	const float *wp[4] = { g_bl.glow > 0.01f ? g_bl.glow_pos : p, kn, ft, r };
	for (int k = 0; k < 4; k++) {
		float dx = wp[k][0] - g_fr.head[0], dz = wp[k][2] - g_fr.head[2];
		h[k][0] = dx * cs - dz * sn, h[k][1] = wp[k][1] - g_fr.head[1], h[k][2] = dx * sn + dz * cs;
	}
	logf_("VERBS bleed where t=%.3f graug %#llx (%s, the %s leg): in the head's frame (x right, y up, z ahead; cm): the cut's "
	      "point %.0f %.0f %.0f knee %.0f %.0f %.0f foot %.0f %.0f %.0f root %.0f %.0f %.0f", t, (unsigned long long)c,
	      bl_offered(c) ? "offered" : "not offered", s ? "left" : "right", h[0][0], h[0][1], h[0][2], h[1][0], h[1][1], h[1][2],
	      h[2][0], h[2][1], h[2][2], h[3][0], h[3][1], h[3][2]);
}
/* `verbs bleed probe S` / `verbs bleed where` (test words) */
static const char *bl_word(const char *w)
{
	if (!g_vb.test)
		return "verbs bleed probe|where|hands (SOMVR_HANDS_TEST is off: ignored)";
	if (!strcmp(w, "hands on") || !strcmp(w, "hands off")) {
		g_bl.hands = w[7] == 'n';
		logf_("VERBS the word: bleed hands %s (SOMVR_GRAUG_BLEED_HANDS; a test)", g_bl.hands ? "on" : "off");
		return g_bl.hands ? "verbs bleed hands on (the stealth's hands for the bleed)" : "verbs bleed hands off (the draw as it is)";
	}
	if (!strcmp(w, "where")) {
		atomic_store(&g_bl_req_where, 1);
		return "verbs bleed where (the cut's point in the head's frame, the next frame)";
	}
	double s = fmin(fmax(atof(w + 6), 0), 1800);
	g_bl.probe_until = cb_now() + s, g_bl.probe_lines = 0;
	logf_("VERBS the word: bleed probe %.0f s (every ask logged; the node log %s)", s, g_brnl.ok ? "kept open" : "off");
	return "verbs bleed probe (every ask of the hamstring logged)";
}

static void bl_end_log(void)
{
	if (!g_bl.on)
		return;
	logf_("VERBS END build 42: the bleed's watch asked %llu time(s), offered %llu (%llu changes); the cue's glow %llu frames, "
	      "ticks %llu; the cut on its legs %llu, touches elsewhere while offered %llu, while the swap was due %llu; the stealth's "
	      "hands asked %llu, done %llu "
	      "(SOMVR_GRAUG_BLEED_CUE=%s, SOMVR_GRAUG_BLEED_LEG=%s, SOMVR_GRAUG_BLEED_HANDS=%s)", (unsigned long long)g_bl.asks, (unsigned long long)g_bl.offers,
	      (unsigned long long)g_bl.changes, (unsigned long long)g_bl.glow_frames, (unsigned long long)g_bl.ticks,
	      (unsigned long long)g_bl.leg_hits, (unsigned long long)g_bl.body_touches, (unsigned long long)g_bl.swap_touches,
	      (unsigned long long)g_bl.hands_reqs,
	      (unsigned long long)g_sl_stealth_hands, g_bl.cue ? "on" : "off", g_bl.leg ? "on" : "off", g_bl.hands ? "on" : "off");
}
