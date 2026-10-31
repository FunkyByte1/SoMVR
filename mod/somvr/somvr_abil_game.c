/* somvr_abil_game.c - build 27 (som-b27-abilities; docs/re-xr-pipeline.md 114, handoff/HANDOFF-combat.md "build 27b"):
 * the abilities Talion gains, by the VR player's hands, SOMVR_ABIL (off | on | log; code default off). NOT a translation
 * unit: somvr.c includes it last (after somvr_struggle_game.c; it uses g_vb, g_cb, gc_pick, vb_pick, xc_read_hc, the
 * log).
 *
 * The game's own (`verbs tree`, re-xr-pipeline.md 39.1 / 114.1): the streak's specials are root nodes of Talion's tree
 * whose pools pass only with the skill owned, the streak high enough and a fitting target - 'PC_WraithAoE' (the chord
 * X + A: 'WraithAoE_HC_*', the wraith flash), 'PC_GroundTakedown' (X with RT held on a downed orc:
 * 'PC_Bip_GroundTakedown_*'), 'PC_PoolAttacks_KD' (X on a downed orc), 'PC_GroundKill_AoE' (the chord X + Y:
 * 'GroundKillAoE_HC_*'). Whether one is possible is the game's: its selection (vb_pick) is asked for the near orcs every
 * SOMVR_VERBS_CUE_RECHECK_S - no offer, nothing.
 *   - the wraith flash: while the game offers it, the right trigger held (the wraith charge, as for the execution) and
 *     the open left hand thrust out at an orc (somvr_ab_push_judge) = the game's own attack started on that orc
 *     (FUN_00a8c700, as SOMVR_ABOVE starts the drop kill; SOMVR_ABIL_START=press: the chord pressed instead). Its cue:
 *     a wraith-blue glow at the left palm while the game offers it, brighter with the trigger held; one tick in the left
 *     hand as the offer comes.
 *   - the ground finisher: while the game offers it for a downed orc (its body's blue glow), the sword's stab down into
 *     him (somvr_ab_stab_judge: fast, down, point first, the tip at his body) = the game's own record of that offer
 *     dealt through Combat_DealHit with forceKill (the execution's and the stealth thrust's way: the stab is the kill,
 *     no sync, the view never moves); with the right trigger held and the game offering its ground AoE
 *     ('PC_GroundKill_AoE'), that attack started instead.
 * Test words (SOMVR_HANDS_TEST): `abil status|probe [nowin]|links NODE|skill NAME|kd|start flash|ground|gaoe|
 * watch S`. */

static const char *const g_abl_w[] = { "off", "on", "log" };
enum { AB_OFF = 0, AB_ON = 1, AB_LOG = 2 };
enum { ABK_FLASH = 0, ABK_GROUND, ABK_GAOE, ABK_N };
static const char *const g_abk_w[ABK_N] = { "the wraith flash", "the ground finisher", "the ground AoE" };
static const char *const g_abk_short[ABK_N] = { "flash", "ground", "gaoe" };
#define AB_HK 16
#define AB_SPAN_S 0.08, 0.16 /* the hands' way measured over their newest 80-160 ms */
#define AB_NODES 4
#define AB_WATCH_N 6
struct abl_offer {
	uintptr_t c, atk;    /* the orc, the attack the game's selection picked */
	double t;            /* asked then (0 = none) */
	float pos[3];        /* his root */
	float body[3];       /* a downed orc's body: over his root at the ground + SOMVR_ABIL_BODY_UP_CM (his root stays
			      * ~95 cm up while he lies: run 20261030-120747's stab met the ground 115-122 cm under it) */
	char atk_name[96], node[48];
};
static struct {
	int mode, on[ABK_N], start_game, test;
	somvr_ab_cfg cfg;
	char nodes[ABK_N][AB_NODES][48];
	int nnodes[ABK_N];
	int cmds[ABK_N][2];  /* SOMVR_ABIL_START=press: the commands pressed */
	float near_cm, ground_cm, cue_level, fresh_s, press_s, body_up_cm;
	struct abl_offer off[ABK_N];
	uintptr_t said_c[ABK_N];
	int said_ok[ABK_N];
	double ask_t, offer_tick_t;
	/* the left palm (abl_hands: tracking space, the rig's way out at use) */
	struct { double t; sv_vec3 p; } ph[AB_HK];
	int ph_n, ph_i;
	float palm[3], eye[3], fwd[3], squeeze;
	int palm_ok, free_hand, rtrig;
	double push_miss_t;
	/* the sword hand (abl_blade: the blade's base in the rig) */
	struct { double t; float base[3]; } hk[AB_HK];
	int hk_n, hk_i;
	float hk_yaw;
	uint64_t hk_epoch;
	double stab_miss_t;
	/* the press */
	int want_start, kind;
	uintptr_t c, pc;     /* pc: Talion's Combat (abl_frame's, for the combat frame's kill) */
	double press_t;
	/* the watch after a press: Talion's attack, his root, the near orcs' attacks, states and health */
	double watch_until, watch_t0;
	float root0[3], root_max_cm;
	uintptr_t tal_atk;
	struct { uintptr_t c, atk, st; float hp; } w[AB_WATCH_N];
	int nw;
	somvr_ex_hc hc0;
	/* the cues (th_frame) */
	float glow_palm, glow_body, body_pos[3];
	double glow_t;
	uintptr_t body_c;
	/* the orcs seen lately: a knocked-down orc leaves Talion's combat bands (cb_orcs_r; run 20261030-100947) */
	struct { uintptr_t c; double t; } rc[12];
	/* tests */
	_Atomic int req_probe, req_kd, req_start, req_links, req_skill, req_watch_ms, req_streak, req_goto, req_rec;
	char links_name[64], skill_name[64], rec_name[64];
	int probe_nowin;
	uint32_t lines, lsec_n;
	double lsec;
	uint64_t asks, offers[ABK_N], pushes, push_misses, stabs, stab_misses, presses[ABK_N], started[ABK_N], refused[ABK_N],
		kills, glow_frames, ticks, vetoes;
} g_abl;

static int abl_log_ok(double t) /* the first 400 lines, then at most 6 a second */
{
	if (g_abl.lines < 400)
		return ++g_abl.lines;
	if (t >= g_abl.lsec + 1.0)
		g_abl.lsec = t, g_abl.lsec_n = 0;
	return g_abl.lsec_n++ < 6;
}

/* "A,B,C" -> the kind's node names */
static void abl_nodes_set(int k, const char *csv)
{
	char buf[256];
	snprintf(buf, sizeof buf, "%s", csv);
	g_abl.nnodes[k] = 0;
	for (char *sv = NULL, *p = strtok_r(buf, ",", &sv); p && g_abl.nnodes[k] < AB_NODES; p = strtok_r(NULL, ",", &sv))
		snprintf(g_abl.nodes[k][g_abl.nnodes[k]++], sizeof g_abl.nodes[0][0], "%s", p);
}

static void abl_start(int *bad)
{
	memset(&g_abl, 0, sizeof g_abl);
	somvr_ab_cfg_default(&g_abl.cfg);
	g_abl.mode = env_word("SOMVR_ABIL", g_abl_w, 3, AB_OFF);
	static const char *const ow[] = { "off", "on" };
	g_abl.on[ABK_FLASH] = env_word("SOMVR_ABIL_FLASH", ow, 2, 1);
	g_abl.on[ABK_GROUND] = env_word("SOMVR_ABIL_GROUND", ow, 2, 1);
	g_abl.on[ABK_GAOE] = env_word("SOMVR_ABIL_GAOE", ow, 2, 0); /* off: never offered headless (docs 114) */
	static const char *const sw[] = { "press", "game" };
	g_abl.start_game = env_word("SOMVR_ABIL_START", sw, 2, 1);
	const char *e;
	abl_nodes_set(ABK_FLASH, (e = getenv("SOMVR_ABIL_FLASH_NODES")) && *e ? e : "PC_WraithAoE");
	abl_nodes_set(ABK_GROUND, (e = getenv("SOMVR_ABIL_GROUND_NODES")) && *e ? e : "PC_GroundTakedown,PC_PoolAttacks_KD,PC_InstaKill");
	abl_nodes_set(ABK_GAOE, (e = getenv("SOMVR_ABIL_GAOE_NODES")) && *e ? e : "PC_GroundKill_AoE");
	g_abl.cmds[ABK_FLASH][0] = VB_CMD_X, g_abl.cmds[ABK_FLASH][1] = VB_CMD_A;     /* 'WraithAoE_Chord' */
	g_abl.cmds[ABK_GROUND][0] = VB_CMD_X, g_abl.cmds[ABK_GROUND][1] = 0;
	g_abl.cmds[ABK_GAOE][0] = VB_CMD_X, g_abl.cmds[ABK_GAOE][1] = 119;            /* 'GroundKillAoE_Chord': X + Y */
	float pu[3] = { g_abl.cfg.push_cms / 100, g_abl.cfg.push_out_deg, g_abl.cfg.push_aim_deg };
	env_vec("SOMVR_ABIL_PUSH", pu, 3, 0, 180, bad); /* m/s, deg out from the body, deg off the orc */
	g_abl.cfg.push_cms = pu[0] * 100, g_abl.cfg.push_out_deg = pu[1], g_abl.cfg.push_aim_deg = pu[2];
	float st[4] = { g_abl.cfg.stab_cms / 100, g_abl.cfg.stab_down_cms / 100, g_abl.cfg.stab_point_deg, g_abl.cfg.stab_reach_cm };
	env_vec("SOMVR_ABIL_STAB", st, 4, 0, 180, bad); /* m/s, m/s down, deg point first, cm reach */
	g_abl.cfg.stab_cms = st[0] * 100, g_abl.cfg.stab_down_cms = st[1] * 100, g_abl.cfg.stab_point_deg = st[2];
	g_abl.cfg.stab_reach_cm = st[3];
	g_abl.near_cm = env_float("SOMVR_ABIL_NEAR_CM", 400, 100, 1500, bad);   /* the flash's orcs */
	g_abl.ground_cm = env_float("SOMVR_ABIL_GROUND_CM", 300, 100, 800, bad); /* the downed orcs asked */
	g_abl.cue_level = env_float("SOMVR_ABIL_CUE_LEVEL", 0.5f, 0, 1, bad);
	g_abl.body_up_cm = env_float("SOMVR_ABIL_BODY_UP_CM", 25, 0, 100, bad); /* a downed orc's body over the ground */
	g_abl.fresh_s = 0.3f, g_abl.press_s = 0.15f;
	g_abl.test = g_vb.test;
	atomic_store(&g_abl.req_streak, -1);
	if (!g_abl.mode)
		return;
	if (g_vb.mode != VB_ON || !g_vb.ok_tree) {
		logf_("CONFIG abil=%s: %s - off", g_abl_w[g_abl.mode], g_vb.mode != VB_ON ? "SOMVR_VERBS is not on"
		      : "FINISH's selection sites did not match (no game's answer)");
		g_abl.mode = AB_OFF;
		return;
	}
	char sw_why[512];
	if (g_abl.start_game && check_site(&g_fi_sites[FIS_START], sw_why, sizeof sw_why)) {
		logf_("CONFIG abil: SOMVR_ABIL_START=game: FI_START (FUN_00a8c700) MISMATCH (%s): the press instead", sw_why);
		g_abl.start_game = 0;
	}
	char nl[ABK_N][200];
	for (int k = 0; k < ABK_N; k++) {
		size_t o = 0;
		nl[k][0] = 0;
		for (int i = 0; i < g_abl.nnodes[k]; i++)
			o += (size_t)snprintf(nl[k] + o, sizeof nl[k] - o, "%s'%s'", i ? ", " : "", g_abl.nodes[k][i]);
	}
	logf_("CONFIG abil=%s build 27 (SOMVR_ABIL): the game's selection asked every %.2f s; start=%s (SOMVR_ABIL_START: %s)",
	      g_abl_w[g_abl.mode], g_gc.recheck_s, g_abl.start_game ? "game" : "press", g_abl.start_game ? "the offered attack "
	      "started on that orc - FUN_00a8c700(pc, node, attack, the orc), as SOMVR_ABOVE starts the drop kill" :
	      "the game's chord pressed for 150 ms");
	logf_("CONFIG abil: the wraith flash %s (SOMVR_ABIL_FLASH; nodes %s for an orc within %.0f cm, SOMVR_ABIL_FLASH_NODES): "
	      "the right trigger held + the open left hand thrust out at it - %.1f+ m/s within %.0f deg of out from the eyes, "
	      "level within %.0f deg of the orc, the palm %.0f+ cm out (SOMVR_ABIL_PUSH); its cue a blue glow at the left "
	      "palm, level %.2f (SOMVR_ABIL_CUE_LEVEL)", g_abl.on[ABK_FLASH] ? "on" : "off", nl[ABK_FLASH], g_abl.near_cm,
	      g_abl.cfg.push_cms / 100, g_abl.cfg.push_out_deg, g_abl.cfg.push_aim_deg, g_abl.cfg.push_out_cm, g_abl.cue_level);
	logf_("CONFIG abil: the ground finisher %s (SOMVR_ABIL_GROUND; nodes %s for an orc within %.0f cm): the sword's stab "
	      "into him - %.1f+ m/s, %.1f+ m/s down, the blade within %.0f deg of down and %.0f deg of its way, the tip within "
	      "%.0f cm of his body (%.0f cm over the ground under his root; SOMVR_ABIL_STAB, SOMVR_ABIL_BODY_UP_CM) = the offered record by "
	      "Combat_DealHit with forceKill; the ground AoE %s (SOMVR_ABIL_GAOE; nodes %s): the same stab with the right "
	      "trigger held = that attack started", g_abl.on[ABK_GROUND] ? "on" : "off", nl[ABK_GROUND], g_abl.ground_cm,
	      g_abl.cfg.stab_cms / 100, g_abl.cfg.stab_down_cms / 100, g_abl.cfg.stab_vert_deg, g_abl.cfg.stab_point_deg,
	      g_abl.cfg.stab_reach_cm, g_abl.body_up_cm, g_abl.on[ABK_GAOE] ? "on" : "off", nl[ABK_GAOE]);
}

/* the game's pick for the first of the kind's nodes that offers one (1 = offered; *of filled) */
static int abl_pick(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int k, struct abl_offer *of, const char **why,
		   double t)
{
	char a[96], s[96];
	for (int i = 0; i < g_abl.nnodes[k]; i++) {
		uintptr_t at = gc_pick(pcomb, pc, o, g_abl.nodes[k][i], a, sizeof a, s, sizeof s, why);
		if (at) {
			of->c = o->c, of->atk = at, of->t = t;
			memcpy(of->pos, o->pos, 12);
			float hit[3] = { 0, 0, 0 };
			uintptr_t g = 0;
			of->body[0] = o->pos[0], of->body[2] = o->pos[2];
			of->body[1] = k != ABK_FLASH && pr_ground(o->pos, hit, &g) && hit[1] <= o->pos[1] + 20 && hit[1] > o->pos[1] - 200
				      ? hit[1] + g_abl.body_up_cm : o->pos[1] - 70 + g_abl.body_up_cm;
			snprintf(of->atk_name, sizeof of->atk_name, "%s", a), snprintf(of->node, sizeof of->node, "%s", g_abl.nodes[k][i]);
			return 1;
		}
	}
	return 0;
}

static int abl_fresh(int k, double t) { return g_abl.off[k].c && t - g_abl.off[k].t <= g_abl.fresh_s; }

static void abl_said(int k, uintptr_t c, int ok, const struct cb_orc *o, const char *why, double t)
{
	if (c == g_abl.said_c[k] && ok == g_abl.said_ok[k])
		return;
	g_abl.said_c[k] = c, g_abl.said_ok[k] = ok;
	g_abl.offers[k] += ok != 0;
	char an[96], sn[96];
	if (abl_log_ok(t))
		logf_("ABIL t=%.3f orc %#lx at %.0f cm (its attack '%s' state '%s' health %.1f): %s%s%s%s%s", t, (unsigned long)c,
		      o->dist, vb_name(peek_ptr(o->c + VB_C_ATTACK), an, sizeof an), vb_name(peek_ptr(o->c + VB_C_STATE), sn, sizeof sn),
		      vb_health(o->c), ok ? "the game offers " : "no ", g_abk_w[k], ok ? " ('" : " (", ok ? g_abl.off[k].atk_name : why,
		      ok ? "')" : ")");
	if (ok && k == ABK_FLASH && t - g_abl.offer_tick_t > 3.0 && g_abl.mode == AB_ON && g_vb.haptic > 0) {
		g_abl.offer_tick_t = t, g_abl.ticks++; /* the cue's tick: the flash is there (once in 3 s) */
		in_haptic_event(SOMVR_HAND_L, 0.35f * g_vb.haptic, 0.04f, SOMVR_HAP_COMBAT);
	}
}

/* the watch after a press: what the game did (Talion's attack, his root, the near orcs) */
static void abl_watch_begin(uintptr_t pc, const struct cb_orc *o, int n, double t, double s)
{
	g_abl.watch_t0 = t, g_abl.watch_until = t + s, g_abl.root_max_cm = 0, g_abl.nw = 0;
	uintptr_t me = pc ? peek_ptr(pc + C_HOBJ) : 0;
	if (!me || !peek(me + OBJ_POS, g_abl.root0, 12))
		memset(g_abl.root0, 0, sizeof g_abl.root0);
	g_abl.tal_atk = pc ? peek_ptr(pc + VB_C_ATTACK) : 0;
	memset(&g_abl.hc0, 0, sizeof g_abl.hc0);
	if (pc)
		xc_read_hc(pc, &g_abl.hc0);
	for (int i = 0; i < n && g_abl.nw < AB_WATCH_N; i++)
		g_abl.w[g_abl.nw++] = (typeof(g_abl.w[0])){ o[i].c, peek_ptr(o[i].c + VB_C_ATTACK), peek_ptr(o[i].c + VB_C_STATE),
							vb_health(o[i].c) };
}

static void abl_watch(uintptr_t pc, double t)
{
	if (!(g_abl.watch_until > 0))
		return;
	char a[96], b[96];
	uintptr_t me = pc ? peek_ptr(pc + C_HOBJ) : 0;
	float r[3];
	if (me && peek(me + OBJ_POS, r, 12) && vec_ok(r, 1e7f) && vec_ok(g_abl.root0, 1e7f)) {
		float d = hypotf(r[0] - g_abl.root0[0], r[2] - g_abl.root0[2]);
		if (d > g_abl.root_max_cm)
			g_abl.root_max_cm = d;
	}
	uintptr_t ta = pc ? peek_ptr(pc + VB_C_ATTACK) : 0;
	if (ta != g_abl.tal_atk) {
		logf_("ABIL watch t=%.3f +%.2f s: Talion's attack '%s' -> '%s'", t, t - g_abl.watch_t0, vb_name(g_abl.tal_atk, a, sizeof a),
		      vb_name(ta, b, sizeof b));
		g_abl.tal_atk = ta;
	}
	for (int i = 0; i < g_abl.nw; i++) {
		uintptr_t c = g_abl.w[i].c, at = peek_ptr(c + VB_C_ATTACK), st = peek_ptr(c + VB_C_STATE);
		float hp = vb_health(c);
		if (at != g_abl.w[i].atk)
			logf_("ABIL watch t=%.3f +%.2f s: orc %#lx attack '%s' -> '%s'", t, t - g_abl.watch_t0, (unsigned long)c,
			      vb_name(g_abl.w[i].atk, a, sizeof a), vb_name(at, b, sizeof b)), g_abl.w[i].atk = at;
		if (st != g_abl.w[i].st)
			logf_("ABIL watch t=%.3f +%.2f s: orc %#lx state '%s' -> '%s'", t, t - g_abl.watch_t0, (unsigned long)c,
			      vb_name(g_abl.w[i].st, a, sizeof a), vb_name(st, b, sizeof b)), g_abl.w[i].st = st;
		if (isfinite(hp) && isfinite(g_abl.w[i].hp) && fabsf(hp - g_abl.w[i].hp) >= 0.5f)
			logf_("ABIL watch t=%.3f +%.2f s: orc %#lx health %.1f -> %.1f%s", t, t - g_abl.watch_t0, (unsigned long)c,
			      g_abl.w[i].hp, hp, cb_dead(c) ? " (dead)" : ""), g_abl.w[i].hp = hp;
	}
	if (t >= g_abl.watch_until) {
		somvr_ex_hc h;
		memset(&h, 0, sizeof h);
		if (pc)
			xc_read_hc(pc, &h);
		logf_("ABIL watch t=%.3f end after %.2f s: Talion's root moved at most %.1f cm (level); the streak %d -> %d, the "
		      "charge %d -> %d (of %d)", t, t - g_abl.watch_t0, g_abl.root_max_cm, g_abl.hc0.streak, h.streak, g_abl.hc0.charge,
		      h.charge, h.need);
		g_abl.watch_until = 0;
	}
}

static void abl_probe(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, double t);
static void abl_links(uintptr_t pcomb, const char *name, double t);
static void abl_skill(const char *name, double t);
static void abl_rec(uintptr_t pcomb, const char *name, double t);
static void abl_kd(uintptr_t pc, const struct cb_orc *o, int n, double t);
static void abl_streak(uintptr_t pc, int v, double t);
static void abl_goto(const struct cb_orc *o, int n, const float root[3], float cm, double t);

/* the orcs of this frame (the bands) and the ones seen lately that are alive and near: the nearest first, at most cap */
static int abl_near(const struct cb_orc *o, int n, const float root[3], float range, struct cb_orc *out, int cap, double t)
{
	for (int i = 0; i < n; i++) { /* note this frame's */
		int k = -1, old = 0;
		for (int j = 0; j < 12 && k < 0; j++)
			if (g_abl.rc[j].c == o[i].c)
				k = j;
		for (int j = 1; j < 12 && k < 0; j++)
			if (g_abl.rc[j].t < g_abl.rc[old].t)
				old = j;
		g_abl.rc[k >= 0 ? k : old].c = o[i].c, g_abl.rc[k >= 0 ? k : old].t = t;
	}
	int m = 0;
	for (int j = 0; j < 12 && m < cap; j++) {
		struct cb_orc c;
		if (!g_abl.rc[j].c || t - g_abl.rc[j].t > 20.0 || !cb_orc_get(g_abl.rc[j].c, &c) || cb_dead(c.c))
			continue;
		c.dist = dist3(c.pos, root);
		if (c.dist > range)
			continue;
		int at = m++;
		while (at > 0 && out[at - 1].dist > c.dist) /* by distance */
			out[at] = out[at - 1], at--;
		out[at] = c;
	}
	return m;
}

/* vb_frame (the game thread), after SOMVR_ABOVE's: the game's answers for the near orcs; the start; the watch */
static void abl_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t)
{
	if (g_abl.mode == AB_OFF)
		return;
	g_abl.pc = pc;
	if (!pc || !root)
		return;
	if (atomic_exchange(&g_abl.req_probe, 0)) {
		struct cb_orc nr[4];
		int nn = abl_near(o, n, root, 1500, nr, 4, t);
		abl_probe(pcomb, pc, nr, nn, t);
	}
	int sk = atomic_exchange(&g_abl.req_streak, -1);
	if (sk >= 0)
		abl_streak(pc, sk, t);
	int gc = atomic_exchange(&g_abl.req_goto, 0);
	if (gc > 0)
		abl_goto(o, n, root, (float)gc, t);
	if (atomic_exchange(&g_abl.req_links, 0))
		abl_links(pcomb, g_abl.links_name, t);
	if (atomic_exchange(&g_abl.req_skill, 0))
		abl_skill(g_abl.skill_name, t);
	if (atomic_exchange(&g_abl.req_rec, 0))
		abl_rec(pcomb, g_abl.rec_name, t);
	if (atomic_exchange(&g_abl.req_kd, 0))
		abl_kd(pc, o, n, t);
	int wm = atomic_exchange(&g_abl.req_watch_ms, 0);
	if (wm > 0)
		abl_watch_begin(pc, o, n, t, wm / 1000.0), logf_("ABIL t=%.3f watch for %.1f s (a test)", t, wm / 1000.0);
	abl_watch(pc, t);
	int rs = atomic_exchange(&g_abl.req_start, 0); /* a test aid: `abil start KIND` - the press without the gesture */
	if (rs > 0 && rs <= ABK_N) {
		int k = rs - 1;
		if (abl_fresh(k, t))
			g_abl.want_start = k + 1, g_abl.kind = k, g_abl.c = g_abl.off[k].c, g_abl.press_t = t, g_abl.presses[k]++,
			logf_("ABIL t=%.3f start %s (a test, no gesture) on orc %#lx", t, g_abk_w[k], (unsigned long)g_abl.c);
		else
			logf_("ABIL t=%.3f start %s (a test): the game does not offer it now", t, g_abk_w[k]);
	}
	if (g_abl.want_start) { /* the offered attack started on him, asked again now */
		int k = g_abl.want_start - 1;
		g_abl.want_start = 0;
		struct cb_orc oc;
		const char *why = "the orc is gone";
		uintptr_t node = 0, n2 = 0, sy = 0, atk = 0;
		uint8_t pad = 0;
		char an[96];
		if (g_abl.mode != AB_ON) {
			logf_("ABIL t=%.3f %s on orc %#lx: logged only (SOMVR_ABIL=log)", t, g_abk_w[k], (unsigned long)g_abl.c);
		} else if (!g_abl.start_game) {
			g_vb.pcmd[0] = g_abl.cmds[k][0], g_vb.pcmd[1] = g_abl.cmds[k][1], g_vb.press_until = t + g_abl.press_s;
			logf_("ABIL t=%.3f %s: %d%s%.0d pressed for %.0f ms", t, g_abk_w[k], g_abl.cmds[k][0], g_abl.cmds[k][1] ? "+" : "",
			      g_abl.cmds[k][1], g_abl.press_s * 1e3);
			abl_watch_begin(pc, o, n, t, 3.0);
		} else if (cb_orc_get(g_abl.c, &oc) && (node = vb_node_named(pcomb, g_abl.off[k].node)) &&
			   (atk = vb_pick(pcomb, pc, &oc, node, &sy, &why))) {
			peek(pcomb + FI_PC_PAD, &pad, 1);
			n2 = FI_FN(FIS_KBMNODE, fi_kbm_fn)(node, pad != 0);
			if (n2) {
				abl_watch_begin(pc, o, n, t, 3.0);
				FI_FN(FIS_START, fi_start_fn)(pcomb, n2, atk, oc.hobj);
				g_abl.started[k]++;
				logf_("ABIL t=%.3f start: %s - FUN_00a8c700(pc=%#lx, node '%s', attack '%s', the orc's hobj %#lx) %.2f s "
				      "after the press; the streak %d charge %d", t, g_abk_w[k], (unsigned long)pcomb, g_abl.off[k].node,
				      vb_name(atk, an, sizeof an), (unsigned long)oc.hobj, t - g_abl.press_t, g_abl.hc0.streak, g_abl.hc0.charge);
			} else {
				g_abl.refused[k]++;
				logf_("ABIL t=%.3f start: %s NOT started - no KBM node", t, g_abk_w[k]);
			}
		} else {
			g_abl.refused[k]++;
			logf_("ABIL t=%.3f start: %s NOT started - the game offers it no more (%s)", t, g_abk_w[k], why);
		}
	}
	if (t - g_abl.ask_t < g_gc.recheck_s)
		return;
	g_abl.ask_t = t;
	const char *why = "";
	g_abl.asks++;
	struct cb_orc nr[8];
	int nn = abl_near(o, n, root, g_abl.near_cm > g_abl.ground_cm ? g_abl.near_cm : g_abl.ground_cm, nr, 8, t);
	for (int k = 0; k < ABK_N; k++) {
		if (!g_abl.on[k])
			continue;
		float range = k == ABK_FLASH ? g_abl.near_cm : g_abl.ground_cm;
		int ok = 0;
		const struct cb_orc *first = NULL;
		for (int i = 0; i < nn && !ok; i++) { /* the nearest first */
			if (nr[i].dist > range)
				continue;
			char sn[96];
			if (k != ABK_FLASH && sf_state_kind(vb_name(peek_ptr(nr[i].c + VB_C_STATE), sn, sizeof sn)) != SF_ST_DOWN)
				continue; /* the ground's: a downed orc (its state 'KnockDown'; the execution's node picks for any) */
			if (!first)
				first = &nr[i];
			ok = abl_pick(pcomb, pc, &nr[i], k, &g_abl.off[k], &why, t);
			if (ok)
				abl_said(k, nr[i].c, 1, &nr[i], why, t);
		}
		if (!ok && first)
			abl_said(k, first->c, 0, first, why, t);
		else if (!ok)
			g_abl.said_c[k] = 0, g_abl.said_ok[k] = 0;
	}
}

/* the push or the stab judged: the start (the next abl_frame) or the kill (now) */
static void abl_press(int k, double t)
{
	g_abl.kind = k, g_abl.c = g_abl.off[k].c, g_abl.press_t = t, g_abl.presses[k]++;
	g_abl.want_start = k + 1;
	if (g_abl.mode == AB_ON && g_vb.haptic > 0)
		in_haptic_event(k == ABK_FLASH ? SOMVR_HAND_L : SOMVR_HAND_R, 0.8f * g_vb.haptic, 0.12f, SOMVR_HAP_COMBAT);
	logf_("ABIL t=%.3f press: %s on orc %#lx (the game offers '%s' by '%s', asked %.2f s ago)", t, g_abk_w[k],
	      (unsigned long)g_abl.c, g_abl.off[k].atk_name, g_abl.off[k].node, t - g_abl.off[k].t);
}

/* the stab's kill: the offered record by Combat_DealHit with forceKill at the tip (the combat frame) */
static void abl_ground_kill(uintptr_t pc, const float tip[3], double t)
{
	struct cb_orc oc;
	const struct abl_offer *of = &g_abl.off[ABK_GROUND];
	g_abl.kind = ABK_GROUND, g_abl.c = of->c, g_abl.press_t = t, g_abl.presses[ABK_GROUND]++;
	if (g_abl.mode != AB_ON || !pc || !cb_orc_get(of->c, &oc) || cb_dead(of->c)) {
		logf_("ABIL t=%.3f the ground finisher on orc %#lx: %s", t, (unsigned long)of->c, g_abl.mode != AB_ON ?
		      "logged only (SOMVR_ABIL=log)" : "the orc is gone or dead");
		return;
	}
	float hp0 = vb_health(of->c);
	struct cb_orc one[1] = { oc };
	abl_watch_begin(pc, one, 1, t, 3.0);
	const char *spent = "";
	if (!strcmp(of->node, "PC_InstaKill")) { /* the execution's own record on a downed orc: its charge spent as the
						  * execution spends it (somvr_execute_game.c, the stock rule) */
		uintptr_t hc = peek_ptr(pc + FI_C_HITCTR);
		somvr_ex_hc h0;
		int16_t cto;
		uint8_t uto;
		int sp = g_xc.ok_spend && hc && xc_read_hc(pc, &h0) ? somvr_ex_spend(&h0, &cto, &uto) : 0;
		if (sp == 1)
			*(volatile uint8_t *)(hc + XC_HC_USES) = uto, spent = "; the charge's use spent";
		else if (sp == 2)
			XC_FN(XCS_CHARGE, xc_charge_fn)(hc, 0), spent = "; the charge spent";
	}
	int dead0 = cb_dead(of->c);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ tip[0], tip[1], tip[2] }, pc, oc.hobj, of->atk, 1);
	g_abl.kills++, g_abl.started[ABK_GROUND]++;
	/* a record that severs (the '_Sever' ones: SeverTarget 'Head') cuts as SOMVR_EXECUTE_SEVER=key cuts an execution's: the
	 * game's own sever, the stab's way down as the velocity (our hit plays no SEVER key) */
	uintptr_t ad = g_cb.ok_ad ? CB_FN(CS_ATTACKDATA, cb_attackdata_fn)(of->atk) : 0, sev = 0;
	uint64_t sv[2] = { 0, 0 };
	int sty = 0, sn = 0;
	if (ad && rx_db_list(ad, g_rxg.sch_ad, XC_AD_SEVER, sv, 1, &sty, &sn) >= 0 && sn && sty == 0xa)
		sev = (uintptr_t)sv[0];
	char sevl[120] = "";
	if (g_xc.sever && g_xc.ok_sever && sev && cb_dead(of->c) && !dead0 && peek_ptr(of->c + 0x90)) {
		float vel[3] = { 0, -300.0f, 0 }, rot[3] = { 0, 0, 0 };
		char snm[96] = "?";
		XC_FN(XCS_SEVER, xc_sever_fn)(of->c, sev, vel, rot, peek_ptr(pc + C_HOBJ));
		snprintf(sevl, sizeof sevl, "; severed '%s' (the record's SeverTarget, SOMVR_EXECUTE_SEVER's way)",
			 rx_rec_name(sev, snm, sizeof snm));
	}
	if (g_vb.haptic > 0)
		in_haptic_event(SOMVR_HAND_R, 0.9f * g_vb.haptic, 0.25f, SOMVR_HAP_COMBAT);
	logf_("ABIL t=%.3f the ground finisher: Combat_DealHit(the tip %.0f,%.0f,%.0f, Talion, orc %#lx, '%s' (by '%s'), "
	      "forceKill 1): health %.1f -> %.1f%s%s%s", t, tip[0], tip[1], tip[2], (unsigned long)of->c, of->atk_name, of->node,
	      hp0, vb_health(of->c), cb_dead(of->c) ? ", dead" : "", spent, sevl);
}

/* combat_frame (the game thread), every frame with a blade: the sword hand's stab into a downed orc the game offers */
static void abl_blade(const float *root, double t)
{
	if (g_abl.mode == AB_OFF)
		return;
	const sc_rigxf *rx = &g_cb.hand_rx;
	int ok = root && g_cb.hand.valid && isfinite(rx->yaw + rx->base[0] + rx->base[1] + rx->base[2]);
	if (!ok || fabsf(rx->yaw - g_abl.hk_yaw) > 1e-4f || g_rig_epoch != g_abl.hk_epoch)
		g_abl.hk_n = 0; /* a turn or a jump of the rig: no motion across it */
	g_abl.hk_yaw = rx->yaw, g_abl.hk_epoch = g_rig_epoch;
	if (!ok)
		return;
	g_abl.hk[g_abl.hk_i].t = t;
	for (int j = 0; j < 3; j++)
		g_abl.hk[g_abl.hk_i].base[j] = g_cb.hand.base[j] - rx->base[j];
	int now = g_abl.hk_i;
	g_abl.hk_i = (g_abl.hk_i + 1) % AB_HK;
	if (g_abl.hk_n < AB_HK)
		g_abl.hk_n++;
	int gaoe = g_abl.on[ABK_GAOE] && abl_fresh(ABK_GAOE, t) && g_abl.rtrig;
	if ((!(g_abl.on[ABK_GROUND] && abl_fresh(ABK_GROUND, t)) && !gaoe) || t - g_abl.press_t < 1.0 || !bd_sword_in_hand() ||
	    g_abl.want_start)
		return;
	double ts[AB_HK];
	float ps[AB_HK][3], v[3];
	int m = 0;
	for (int k = g_abl.hk_n - 1; k >= 0; k--) { /* oldest first */
		int i = (now - k + AB_HK) % AB_HK;
		ts[m] = g_abl.hk[i].t, memcpy(ps[m], g_abl.hk[i].base, 12), m++;
	}
	if (!somvr_fa_mean_vel(ts, (const float(*)[3])ps, m, AB_SPAN_S, v))
		return;
	if (v[1] > -g_abl.cfg.stab_down_cms * 0.6f)
		return; /* not going down: nothing to judge */
	int k = gaoe ? ABK_GAOE : ABK_GROUND;
	const struct abl_offer *of = &g_abl.off[k];
	float body[3] = { of->body[0], of->body[1], of->body[2] }, ax[3];
	for (int j = 0; j < 3; j++)
		ax[j] = g_cb.hand.tip[j] - g_cb.hand.base[j];
	somvr_ab_stab s;
	if (somvr_ab_stab_judge(&g_abl.cfg, v, ax, g_cb.hand.tip, body, &s)) {
		g_abl.stabs++;
		logf_("ABIL t=%.3f stab: the sword hand at %.2f m/s (%.2f down), the blade %.0f deg off down, the point leading by "
		      "%.0f deg, the tip %.0f cm from his body%s", t, s.speed / 100, s.down / 100, s.vert_deg, s.point_deg, s.reach_cm,
		      gaoe ? " - the right trigger held: the ground AoE" : "");
		if (k == ABK_GROUND)
			abl_ground_kill(g_abl.pc, g_cb.hand.tip, t);
		else
			abl_press(k, t);
	} else if (t - g_abl.stab_miss_t > 0.5) {
		g_abl.stab_misses++, g_abl.stab_miss_t = t;
		if (abl_log_ok(t))
			logf_("ABIL t=%.3f a downward move by a downed orc, not the stab (%s): %.2f m/s, %.2f down, the blade %.0f deg "
			      "off down, the point %.0f deg off the way, the tip %.0f cm from his body", t, s.why, s.speed / 100,
			      s.down / 100, s.vert_deg, s.point_deg, s.reach_cm);
	}
}

/* the stereo frame's hands (after the struggle's): the left palm's way, the right trigger; the push */
static void abl_hands(const sv_vec3 p[2], const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	if (g_abl.mode == AB_OFF)
		return;
	double t = cb_now();
	const somvr_hand_in *hl = &g_inp.st.hand[SOMVR_HAND_L], *hr = &g_inp.st.hand[SOMVR_HAND_R];
	g_abl.rtrig = somvr_ab_trigger(&g_abl.cfg, g_abl.rtrig, (hr->active & SOMVR_HACT_INPUT) ? hr->trigger : 0);
	g_abl.squeeze = (hl->active & SOMVR_HACT_INPUT) ? hl->squeeze : 0;
	g_abl.palm_ok = g_inp.hands[SOMVR_HAND_L].valid && vec_ok((const float[3]){ pw[0].x, pw[0].y, pw[0].z }, 1e7f);
	g_abl.palm[0] = pw[0].x, g_abl.palm[1] = pw[0].y, g_abl.palm[2] = pw[0].z;
	g_abl.eye[0] = hp.x, g_abl.eye[1] = hp.y, g_abl.eye[2] = hp.z;
	sv_vec3 hf = sv_qrot(sv_qnormalize(hq), (sv_vec3){ 0, 0, 1 }); /* the head's forward (STRIKE's gaze way) */
	g_abl.fwd[0] = hf.x, g_abl.fwd[1] = hf.y, g_abl.fwd[2] = hf.z;
	g_abl.free_hand = g_abl.palm_ok && !bo_out() && !cl_holds(SOMVR_HAND_L) && !vb_grab_held() && !(g_bd.two.w > 0);
	if (!g_abl.palm_ok) {
		g_abl.ph_n = 0;
		return;
	}
	g_abl.ph[g_abl.ph_i].t = t, g_abl.ph[g_abl.ph_i].p = p[0];
	int now = g_abl.ph_i;
	g_abl.ph_i = (g_abl.ph_i + 1) % AB_HK, g_abl.ph_n += g_abl.ph_n < AB_HK;
	if (!g_abl.on[ABK_FLASH] || !abl_fresh(ABK_FLASH, t) || !g_abl.free_hand || t - g_abl.press_t < 1.0 || g_abl.want_start)
		return;
	int k = -1;
	for (int j = 1; j < g_abl.ph_n; j++) { /* the newest sample 80-160 ms older */
		int ix = (now - j + AB_HK) % AB_HK;
		double age = g_abl.ph[now].t - g_abl.ph[ix].t;
		if (age > 0.16)
			break;
		if (age >= 0.08) {
			k = ix;
			break;
		}
	}
	if (k < 0)
		return;
	double age = g_abl.ph[now].t - g_abl.ph[k].t;
	sv_vec3 a = somvr_rig_point(&g_rig, g_fr.base, g_abl.ph[k].p), b = somvr_rig_point(&g_rig, g_fr.base, g_abl.ph[now].p);
	float v[3] = { (float)((b.x - a.x) / age), (float)((b.y - a.y) / age), (float)((b.z - a.z) / age) };
	if (!(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] >= 0.5f * g_abl.cfg.push_cms * g_abl.cfg.push_cms))
		return; /* not moving fast: nothing to judge */
	const struct abl_offer *of = &g_abl.off[ABK_FLASH];
	struct cb_orc oc;
	float orc[3] = { of->pos[0], of->pos[1], of->pos[2] };
	if (cb_orc_get(of->c, &oc))
		memcpy(orc, oc.pos, 12);
	somvr_ab_push r;
	if (somvr_ab_push_judge(&g_abl.cfg, v, g_abl.eye, g_abl.palm, orc, g_abl.squeeze, g_abl.rtrig, &r)) {
		g_abl.pushes++;
		logf_("ABIL t=%.3f push: the open left hand at %.2f m/s, %.0f deg off out from the eyes, %.0f deg off the orc, the "
		      "palm %.0f cm out, the right trigger held", t, r.speed / 100, r.out_deg, r.aim_deg, r.out_cm);
		abl_press(ABK_FLASH, t);
	} else if (t - g_abl.push_miss_t > 0.5) {
		g_abl.push_misses++, g_abl.push_miss_t = t;
		if (abl_log_ok(t))
			logf_("ABIL t=%.3f a fast left hand while the wraith flash is offered, not the push (%s): %.2f m/s, %.0f deg off "
			      "out, %.0f deg off the orc, the palm %.0f cm out, the grip %.2f, the right trigger %s", t, r.why,
			      r.speed / 100, r.out_deg, r.aim_deg, r.out_cm, g_abl.squeeze, g_abl.rtrig ? "held" : "up");
	}
}

/* SOMVR_SHOVE: the push with the wraith charge is the flash's, not a shove (py's shove asks before it acts) */
static int abl_shove_veto(void)
{
	int v = g_abl.mode != AB_OFF && g_abl.on[ABK_FLASH] && g_abl.rtrig && abl_fresh(ABK_FLASH, cb_now());
	g_abl.vetoes += v != 0;
	return v;
}

/* th_frame: the cues - the left palm's blue glow while the flash is offered (brighter with the charge), a downed orc's
 * body glow while the game offers its finisher */
static void abl_glow_add(struct th_glow *gl, int *ng)
{
	if (g_abl.mode != AB_ON || !(g_abl.cue_level > 0))
		return;
	double t = cb_now();
	float dt = g_abl.glow_t > 0 && t > g_abl.glow_t ? (float)(t - g_abl.glow_t) : 0, a = dt / 0.15f;
	g_abl.glow_t = t;
	a = a < 1 ? a : 1;
	int fl = g_abl.on[ABK_FLASH] && abl_fresh(ABK_FLASH, t) && g_abl.palm_ok && g_abl.free_hand;
	float want = fl ? g_abl.cue_level * (g_abl.rtrig ? 1.4f : 0.6f) : 0;
	g_abl.glow_palm += (want - g_abl.glow_palm) * a;
	if (g_abl.glow_palm > 0.01f && vec_ok(g_abl.palm, 1e7f)) {
		th_glow_add(gl, ng, g_abl.palm, g_abl.glow_palm > 1 ? 1 : g_abl.glow_palm, dist3(g_abl.palm, g_fr.head),
			    SOMVR_THK_MELEE | SOMVR_THK_BLUE);
		g_abl.glow_frames++;
	}
	int gk = g_abl.on[ABK_GROUND] && abl_fresh(ABK_GROUND, t) ? ABK_GROUND : g_abl.on[ABK_GAOE] && abl_fresh(ABK_GAOE, t) ?
		 ABK_GAOE : -1;
	if (gk >= 0) {
		if (g_abl.off[gk].c != g_abl.body_c)
			g_abl.glow_body = 0, g_abl.body_c = g_abl.off[gk].c;
		memcpy(g_abl.body_pos, g_abl.off[gk].body, sizeof g_abl.body_pos);
	}
	g_abl.glow_body += ((gk >= 0 ? g_abl.cue_level : 0) - g_abl.glow_body) * a;
	if (g_abl.glow_body > 0.01f && vec_ok(g_abl.body_pos, 1e7f)) {
		th_glow_add(gl, ng, g_abl.body_pos, g_abl.glow_body, dist3(g_abl.body_pos, g_fr.head), SOMVR_THK_MELEE | SOMVR_THK_BLUE);
		g_abl.glow_frames++;
	}
}

/* ---- tests -------------------------------------------------------------------------------------------------------- */
/* `abil probe [nowin]`: the abilities' nodes against the near orcs (the game thread) */
static void abl_probe(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, double t)
{
	static const char *const nodes[] = { "PC_WraithAoE", "PC_GroundKill_AoE", "PC_GroundTakedown", "PC_PoolAttacks_KD",
					     "PC_GroundTakedown_GetYouThere", "PC_InstaKill", "PC_InstaHOG", "PC_Stun_Root",
					     "PC_Stun_Root_Lowest", "PC_HoG_Standing_Start", "PC_DashOver", "PC_StealthTorment",
					     "PC_StealthBrand", "PC_PoolAttacks", "PC_Flurry_Standing_INTO", "PC_Counter",
					     "PC_Tackle_Attempt", "PC_Taunt", "PC_Environmental", "PC_Grab_Attempt" };
	somvr_ex_hc h;
	memset(&h, 0, sizeof h);
	xc_read_hc(pc, &h);
	char ta[96], ts[96];
	logf_("ABIL probe t=%.3f%s: Talion's attack '%s' state '%s', the streak %d charge %d of %d (charged %d), %d near orc(s)",
	      t, g_abl.probe_nowin ? " (the attack window not asked)" : "", vb_name(peek_ptr(pc + VB_C_ATTACK), ta, sizeof ta),
	      vb_name(peek_ptr(pc + VB_C_STATE), ts, sizeof ts), h.streak, h.charge, h.need, h.charged, n);
	g_vb_pick_nowin = g_abl.probe_nowin;
	for (int i = 0; i < n && i < 3; i++) {
		char line[2400], a[96], s[96], an[96], sn[96];
		int k0 = snprintf(line, sizeof line, "ABIL probe t=%.3f orc %#lx at %.0f cm (attack '%s' state '%s' health %.1f%s):", t,
				  (unsigned long)o[i].c, o[i].dist, vb_name(peek_ptr(o[i].c + VB_C_ATTACK), an, sizeof an),
				  vb_name(peek_ptr(o[i].c + VB_C_STATE), sn, sizeof sn), vb_health(o[i].c), cb_dead(o[i].c) ? ", dead" : "");
		for (size_t j = 0; j < sizeof nodes / sizeof nodes[0] && k0 < (int)sizeof line - 1; j++) {
			const char *why = "";
			uintptr_t at = gc_pick(pcomb, pc, &o[i], nodes[j], a, sizeof a, s, sizeof s, &why);
			k0 += snprintf(line + k0, sizeof line - (size_t)k0, " %s -> %s%s%s%s%s;", nodes[j], at ? "'" : "none (",
				       at ? a : why, at ? "'" : ")", at && s[0] ? " sync " : "", at && s[0] ? s : "");
		}
		logf_("%s", line);
	}
	g_vb_pick_nowin = 0;
}

/* `abil links NODE`: a root node's Links (one level): each link's name, type, its input's command or chord */
static void abl_links(uintptr_t pcomb, const char *name, double t)
{
	uintptr_t node = vb_node_named(pcomb, name);
	int lx = vb_field_ix(g_fig.sch_node, "Links");
	uint64_t ls[48];
	int ty = 0, cnt = 0, nl = node && lx >= 0 ? rx_db_list(node, g_fig.sch_node, lx, ls, 48, &ty, &cnt) : -1;
	logf_("ABIL links t=%.3f '%s' %s: %d link(s)%s", t, name, node ? "found" : "NOT in the tree", nl > 0 && ty == 0xa ? nl : 0,
	      cnt > 48 ? " (the first 48)" : "");
	for (int k = 0; nl > 0 && ty == 0xa && k < nl; k++) {
		uintptr_t ln = (uintptr_t)ls[k], pred = rx_db_rec(ln, g_fig.sch_node, 2);
		uintptr_t inp = pred ? rx_db_rec(pred, VB_SCH_CHORD, 0) : 0, ch = pred ? rx_db_rec(pred, VB_SCH_CHORD, 1) : 0;
		uint64_t v[2] = { 0, 0 };
		int t2 = 0, c2 = 0;
		long ntype = rx_db_list(ln, g_fig.sch_node, 0, v, 1, &t2, &c2) == 1 ? (long)(uint32_t)v[0] : -1;
		uint32_t a0 = 0, held = 0, h1 = 0, h2 = 0;
		long itype = -1;
		if (inp && rx_db_list(inp, VB_SCH_INREC, 0, v, 1, &t2, &c2) == 1)
			a0 = (uint32_t)v[0];
		if (inp && rx_db_list(inp, VB_SCH_INREC, 1, v, 1, &t2, &c2) == 1)
			itype = (long)(uint32_t)v[0];
		if (inp && rx_db_list(inp, VB_SCH_INREC, 4, v, 1, &t2, &c2) == 1)
			held = (uint32_t)v[0];
		if (ch && rx_db_list(ch, VB_SCH_INPUT, 0, v, 1, &t2, &c2) == 1)
			h1 = (uint32_t)v[0];
		if (ch && rx_db_list(ch, VB_SCH_INPUT, 1, v, 1, &t2, &c2) == 1)
			h2 = (uint32_t)v[0];
		int ka = vb_cmd_of(a0), kh = vb_cmd_of(held), k1 = vb_cmd_of(h1), k2 = vb_cmd_of(h2);
		char n1[96], n2[64], n3[64];
		logf_("ABIL links   %d '%s' type %ld input '%s' (%s, kind %ld, held %s) chord '%s' %s+%s", k, vb_name(ln, n1, sizeof n1),
		      ntype, vb_name(inp, n2, sizeof n2), ka > 0 ? in_cmd_name(ka) : a0 ? "?" : "-", itype, kh > 0 ? in_cmd_name(kh) : "-",
		      vb_name(ch, n3, sizeof n3), k1 > 0 ? in_cmd_name(k1) : h1 ? "?" : "-", k2 > 0 ? in_cmd_name(k2) : h2 ? "?" : "-");
	}
}

/* a record's raw attributes, one line each (hash, type, count, the values: records by name, floats, else the number and
 * the text at it if it reads as one); its linked records' too, depth levels down */
static int g_abl_dumps; /* the records dumped by this word (at most 40) */
static void abl_rec_dump(uintptr_t rec, const char *what, int depth, double t)
{
	if (++g_abl_dumps > 40)
		return;
	unsigned char hdr[2], e[16];
	uintptr_t arr = peek_ptr(rec + 8), vals = peek_ptr(rec + 0x10), sub[12];
	int nsub = 0;
	char nm[96];
	if (!peek(rec, hdr, 2) || !arr) {
		logf_("ABIL skill t=%.3f %s '%s': no attributes", t, what, rx_rec_name(rec, nm, sizeof nm));
		return;
	}
	logf_("ABIL skill t=%.3f %s '%s' (record %#lx): %u attributes", t, what, rx_rec_name(rec, nm, sizeof nm), (unsigned long)rec,
	      hdr[0]);
	for (int k = 0; k < hdr[0]; k++) {
		if (!peek(arr + (uintptr_t)k * 16, e, 16))
			break;
		uint32_t hs;
		uint16_t cnt, vi;
		memcpy(&hs, e, 4), memcpy(&cnt, e + 0xc, 2), memcpy(&vi, e + 0xe, 2);
		int ty = e[0xa] & 0x3f;
		char line[900];
		int o = snprintf(line, sizeof line, "ABIL skill   %s %d hash %08x type %d count %u:", what, k, hs, ty, cnt);
		for (int j = 0; j < cnt && j < 6 && vals && o < (int)sizeof line - 120; j++) {
			uint64_t v = 0;
			if (ty == 1) {
				o += snprintf(line + o, sizeof line - (size_t)o, " (bool)");
				break;
			}
			if (!peek(vals + (uintptr_t)(vi + j) * 8, &v, 8))
				break;
			char s[96];
			if (ty == 0xa) {
				o += snprintf(line + o, sizeof line - (size_t)o, " '%s'", rx_rec_name((uintptr_t)v, s, sizeof s));
				if (v && strcmp(s, "SkillPoint") && nsub < 12)
					sub[nsub++] = (uintptr_t)v;
			} else if (ty == 2) {
				float f;
				uint32_t u = (uint32_t)v;
				memcpy(&f, &u, 4);
				o += snprintf(line + o, sizeof line - (size_t)o, " %.3f", f);
			} else {
				o += snprintf(line + o, sizeof line - (size_t)o, " %#llx", (unsigned long long)v);
				if (v > 0x400000 && v < 0x7fffffffffffull && rx_str((uintptr_t)v, s, sizeof s)[0]) {
					int pr = 1;
					for (const char *q = s; *q && pr; q++)
						pr = *q >= 32 && *q < 127;
					if (pr)
						o += snprintf(line + o, sizeof line - (size_t)o, " \"%.60s\"", s);
				}
			}
		}
		logf_("%s", line);
	}
	for (int i = 0; depth > 0 && i < nsub; i++)
		abl_rec_dump(sub[i], "linked", depth - 1, t);
}

/* `abil rec NAME`: a root node of Talion's tree (its pools and their attacks) or an attack record by name (its
 * predicate and the records it links), raw, three levels */
static void abl_rec(uintptr_t pcomb, const char *name, double t)
{
	g_abl_dumps = 0;
	uintptr_t node = vb_node_named(pcomb, name);
	if (node) {
		abl_rec_dump(node, "node", 3, t);
		return;
	}
	uintptr_t cat = peek_ptr(g_cbg.atkcat_pp), base = cat ? peek_ptr(cat + CAT_RECS) : 0;
	uint32_t n = 0;
	if (!base || !peek(cat + CAT_COUNT, &n, sizeof n) || n > 40000) {
		logf_("ABIL rec t=%.3f '%s': the attacks not read", t, name);
		return;
	}
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		char nm[96];
		if (!strcmp(fb_rec_name(rec, nm, sizeof nm), name)) {
			abl_rec_dump(rec, "attack", 3, t);
			return;
		}
	}
	logf_("ABIL rec t=%.3f '%s': not a root node nor an attack record", t, name);
}

/* `abil skill NAME`: a skill record's raw attributes and its linked records' */
static void abl_skill(const char *name, double t)
{
	uintptr_t cat = g_bz.ok_skill ? peek_ptr(BZ_SKILL_CAT_PP) : 0, base = cat ? peek_ptr(cat + CAT_RECS) : 0;
	uint32_t n = 0;
	if (!cat || !base || !peek(cat + CAT_COUNT, &n, sizeof n) || n > 4000) {
		logf_("ABIL skill t=%.3f '%s': the skills not read", t, name);
		return;
	}
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		char nm[80];
		if (strcmp(fb_rec_name(rec, nm, sizeof nm), name))
			continue;
		g_abl_dumps = 0;
		abl_rec_dump(rec, "skill", 2, t);
		return;
	}
	logf_("ABIL skill t=%.3f '%s': no such skill record", t, name);
}

/* `abil kd`: a test aid - the nearest orc knocked down: our heavy record dealt as the game's (its own RecoilTypeHit,
 * SOMVR_COMBAT_KNOCKBACK's lighter type not put in: that is cb_hit's), no kill */
static void abl_kd(uintptr_t pc, const struct cb_orc *o, int n, double t)
{
	const char *rn = "-";
	uintptr_t rec = cb_record(SC_TIER_HEAVY, 0, &rn);
	if (!n || !rec) {
		logf_("ABIL kd t=%.3f: %s", t, !n ? "no orc near" : "no heavy record");
		return;
	}
	float at[3] = { o[0].pos[0], o[0].pos[1] + 40, o[0].pos[2] };
	abl_watch_begin(pc, o, 1, t, 4.0);
	float hp0 = vb_health(o[0].c);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ at[0], at[1], at[2] }, pc, o[0].hobj, rec, 0);
	logf_("ABIL kd t=%.3f: orc %#lx at %.0f cm dealt '%s' (a test aid): health %.1f -> %.1f", t, (unsigned long)o[0].c,
	      o[0].dist, rn, hp0, vb_health(o[0].c));
}

/* `abil streak N`: a test aid - Talion's hit streak set to N (the counter's +0xc4; the game keeps counting from it and
 * lapses it by its own timer): the specials' pools read it */
static void abl_streak(uintptr_t pc, int v, double t)
{
	uintptr_t hc = pc ? peek_ptr(pc + FI_C_HITCTR) : 0;
	int16_t was = 0, now = (int16_t)v;
	if (!hc || !peek(hc + XC_HC_STREAK, &was, sizeof was) || !arm_poke(hc + XC_HC_STREAK, &now, sizeof now)) {
		logf_("ABIL t=%.3f streak: not written (no hit counter)", t);
		return;
	}
	logf_("ABIL t=%.3f streak %d -> %d (a test aid: `abil streak`)", t, was, v);
}

/* `abil goto CM`: a test aid - Talion teleported CM short of the nearest downed orc (else the nearest one) on the line
 * from where he stands; the line says where the orc is from the head (the driver turns the headless head that much) */
static void abl_goto(const struct cb_orc *o, int n, const float root[3], float cm, double t)
{
	struct cb_orc nr[8];
	int nn = abl_near(o, n, root, 2000, nr, 8, t), k = -1;
	char sn[96];
	for (int i = 0; i < nn && k < 0; i++)
		if (sf_state_kind(vb_name(peek_ptr(nr[i].c + VB_C_STATE), sn, sizeof sn)) == SF_ST_DOWN)
			k = i;
	if (k < 0 && nn)
		k = 0;
	if (k < 0) {
		logf_("ABIL t=%.3f goto: no orc within 20 m", t);
		return;
	}
	float dx = nr[k].pos[0] - root[0], dz = nr[k].pos[2] - root[2], h = hypotf(dx, dz);
	if (!(h > 1))
		dx = 0, dz = 1, h = 1;
	float at[3] = { nr[k].pos[0] - dx / h * cm, nr[k].pos[1], nr[k].pos[2] - dz / h * cm }, hit[3] = { 0, 0, 0 };
	uintptr_t g = 0;
	if (!pr_ground(at, hit, &g) || fabsf(hit[1] - nr[k].pos[1]) > 150) {
		logf_("ABIL t=%.3f goto: no ground %.0f cm short of orc %#lx", t, cm, (unsigned long)nr[k].c);
		return;
	}
	char cmd[96], why[256] = "";
	snprintf(cmd, sizeof cmd, "Teleport %ld %ld %ld", lroundf(at[0]), lroundf(hit[1] + 110), lroundf(at[2]));
	int rc = es_run(cmd, why, sizeof why);
	float by = remainderf(atan2f(dx, dz) - atan2f(g_abl.fwd[0], g_abl.fwd[2]), 6.2831853f) * 57.29578f;
	logf_("ABIL t=%.3f goto: orc %#lx (state '%s') at %.0f,%.0f,%.0f, %.0f cm away: '%s' %s%s; the orc %+.0f deg right of the "
	      "head", t, (unsigned long)nr[k].c, vb_name(peek_ptr(nr[k].c + VB_C_STATE), sn, sizeof sn), nr[k].pos[0], nr[k].pos[1],
	      nr[k].pos[2], nr[k].dist, cmd, rc == 0 ? "run" : "NOT run: ", rc == 0 ? "" : why, by);
}

/* the trigger word "abil ..." (the writer thread) */
static const char *abl_word(const char *w)
{
	if (!strcmp(w, "on") || !strcmp(w, "off") || !strcmp(w, "log")) {
		if (g_abl.mode == AB_OFF)
			return "abil (SOMVR_ABIL is off at start: ignored)";
		g_abl.mode = w[1] == 'n' ? AB_ON : w[1] == 'f' ? AB_OFF : AB_LOG;
		return "abil on|off|log";
	}
	if (!strcmp(w, "status")) {
		logf_("ABIL status: mode %s; asks %llu, offers flash %llu ground %llu gaoe %llu; pushes %llu (misses %llu), stabs %llu "
		      "(misses %llu); presses %llu/%llu/%llu, started %llu/%llu/%llu, refused %llu/%llu/%llu, kills %llu; cue frames %llu, "
		      "ticks %llu, shove vetoes %llu", g_abl_w[g_abl.mode], (unsigned long long)g_abl.asks,
		      (unsigned long long)g_abl.offers[0], (unsigned long long)g_abl.offers[1], (unsigned long long)g_abl.offers[2],
		      (unsigned long long)g_abl.pushes, (unsigned long long)g_abl.push_misses, (unsigned long long)g_abl.stabs,
		      (unsigned long long)g_abl.stab_misses, (unsigned long long)g_abl.presses[0], (unsigned long long)g_abl.presses[1],
		      (unsigned long long)g_abl.presses[2], (unsigned long long)g_abl.started[0], (unsigned long long)g_abl.started[1],
		      (unsigned long long)g_abl.started[2], (unsigned long long)g_abl.refused[0], (unsigned long long)g_abl.refused[1],
		      (unsigned long long)g_abl.refused[2], (unsigned long long)g_abl.kills, (unsigned long long)g_abl.glow_frames,
		      (unsigned long long)g_abl.ticks, (unsigned long long)g_abl.vetoes);
		return "abil status";
	}
	if (g_abl.mode == AB_OFF)
		return "abil (SOMVR_ABIL is off: ignored)";
	if (!g_abl.test)
		return "abil (SOMVR_HANDS_TEST is off: ignored)";
	if (!strncmp(w, "probe", 5)) {
		g_abl.probe_nowin = !strncmp(w + 5, " nowin", 6);
		atomic_store(&g_abl.req_probe, 1);
		return "abil probe (a test: the abilities' nodes against the near orcs)";
	}
	if (!strncmp(w, "links ", 6)) {
		snprintf(g_abl.links_name, sizeof g_abl.links_name, "%s", w + 6);
		atomic_store(&g_abl.req_links, 1);
		return "abil links (a test: a root node's links)";
	}
	if (!strncmp(w, "rec ", 4)) {
		snprintf(g_abl.rec_name, sizeof g_abl.rec_name, "%s", w + 4);
		atomic_store(&g_abl.req_rec, 1);
		return "abil rec (a test: a node's or an attack's records)";
	}
	if (!strncmp(w, "skill ", 6)) {
		snprintf(g_abl.skill_name, sizeof g_abl.skill_name, "%s", w + 6);
		atomic_store(&g_abl.req_skill, 1);
		return "abil skill (a test: a skill record's attributes)";
	}
	if (!strncmp(w, "streak ", 7)) {
		long v = strtol(w + 7, NULL, 10);
		if (v < 0 || v > 60)
			return "abil streak N (0..60: ignored)";
		atomic_store(&g_abl.req_streak, (int)v);
		return "abil streak (a test aid: Talion's hit streak set)";
	}
	if (!strncmp(w, "goto ", 5)) {
		long cm = strtol(w + 5, NULL, 10);
		if (cm < 30 || cm > 500)
			return "abil goto CM (30..500: ignored)";
		atomic_store(&g_abl.req_goto, (int)cm);
		return "abil goto (a test aid: Talion that far short of the nearest downed orc)";
	}
	if (!strcmp(w, "kd")) {
		atomic_store(&g_abl.req_kd, 1);
		return "abil kd (a test aid: the nearest orc dealt our heavy record as the game's)";
	}
	if (!strncmp(w, "start ", 6)) {
		for (int k = 0; k < ABK_N; k++)
			if (!strcmp(w + 6, g_abk_short[k])) {
				atomic_store(&g_abl.req_start, k + 1);
				return "abil start (a test: the press without the gesture, where the game offers it)";
			}
		return "abil start flash|ground|gaoe (ignored)";
	}
	if (!strncmp(w, "watch ", 6)) {
		double s = strtod(w + 6, NULL);
		if (!(s > 0 && s <= 30))
			return "abil watch S (0..30: ignored)";
		atomic_store(&g_abl.req_watch_ms, (int)(s * 1000));
		return "abil watch (a test: Talion and the near orcs logged as they change)";
	}
	return "abil status|on|off|log|probe [nowin]|links NODE|skill NAME|kd|streak N|goto CM|start flash|ground|gaoe|watch S";
}

static void abl_end_log(void)
{
	if (g_abl.mode == AB_OFF)
		return;
	logf_("ABIL END build 27: asks %llu; offers: the wraith flash %llu, the ground finisher %llu, the ground AoE %llu; pushes "
	      "%llu (other fast moves %llu), stabs %llu (other downward moves %llu); presses %llu/%llu/%llu, started %llu/%llu/%llu, "
	      "refused %llu/%llu/%llu, ground kills %llu; cue frames %llu, ticks %llu, shove vetoes %llu",
	      (unsigned long long)g_abl.asks, (unsigned long long)g_abl.offers[0], (unsigned long long)g_abl.offers[1],
	      (unsigned long long)g_abl.offers[2], (unsigned long long)g_abl.pushes, (unsigned long long)g_abl.push_misses,
	      (unsigned long long)g_abl.stabs, (unsigned long long)g_abl.stab_misses, (unsigned long long)g_abl.presses[0],
	      (unsigned long long)g_abl.presses[1], (unsigned long long)g_abl.presses[2], (unsigned long long)g_abl.started[0],
	      (unsigned long long)g_abl.started[1], (unsigned long long)g_abl.started[2], (unsigned long long)g_abl.refused[0],
	      (unsigned long long)g_abl.refused[1], (unsigned long long)g_abl.refused[2], (unsigned long long)g_abl.kills,
	      (unsigned long long)g_abl.glow_frames, (unsigned long long)g_abl.ticks, (unsigned long long)g_abl.vetoes);
}
