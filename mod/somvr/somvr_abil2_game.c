/* somvr_abil2_game.c - build 27c (som-b27-abilities2; docs/re-xr-pipeline.md 118, handoff/HANDOFF-combat.md "build
 * 27c"): the abilities the user's save owns that had no VR control - the ignite shot, the dagger's quick throw, the
 * whistle - each on the game's own offer and attack. Under SOMVR_ABIL (not off), each its own switch (code default off):
 * SOMVR_ABIL_IGNITE, SOMVR_ABIL_THROW, SOMVR_ABIL_WHISTLE. NOT a translation unit: somvr.c includes it after
 * somvr_abil_game.c (it uses g_abl's watch and start sites, vb_pick, cb_orcs_r, the bow's state, the slots).
 *   - the ignite shot (PSS_Wraith_Ignite; the root node 'PC_Wraith_Ranged_Immolate_Shoot', input CombatSecondary02 =
 *     the flat game's interact button while aiming): B (the string hand's upper button: our interact, the same button)
 *     pressed while an arrow is drawn kindles it where the game offers the ignite - its selection for the orc on the ray,
 *     else with no target, else (SOMVR_ABIL_IGNITE_ATTACK) its record by name with the skill owned. The cue: a
 *     wraith-blue glow at the arrow's tip, a tick. The release shoots our arrow with that attack (bo_shoot: the
 *     projectile takes the attack's RangedWeaponOverride - the game's ignite arrow and its rules). B again or a let-down
 *     puts it out. No offer: a short denied buzz, nothing kindled.
 *   - the quick throw (PSS_Quick_Throw_1/2; 'PC_Ranged_QuickThrow', input CombatSecondary03 tapped): the dagger drawn in
 *     the left hand (L grip at the hip), thrown overhand ahead and let go (somvr_ab_throw_judge: fast, ahead, the hand
 *     from the shoulder up, the grip opening within 0.25 s of the fast move) = the game's quick throw started on the orc
 *     nearest the throw's line that it offers it for (FUN_00a8c700, as the flash). The dagger leaves the hand for the
 *     flight (the slots' away: put back) and comes back to it (the slots' restore: the wraith recall), a thunk.
 *   - the whistle (PSS_Whistle; 'PC_Attract', input CombatPrimary02 with CombatSecondary04 held): a free hand held at the
 *     mouth (two fingers to the lips) for 0.3 s = the game's whistle started on the nearest orc it offers it for; then
 *     the hand away from the mouth before the next one.
 * Test words (SOMVR_HANDS_TEST): `abil2 status|probe|props|aim N|aim off`. */

#define AB2_PROPS 12
static struct {
	int on, ignite, thr, whistle;
	somvr_ab2_cfg cfg;
	char ign_node[64], thr_node[64], wh_node[64], ign_name[96], ign_skill[48];
	float ign_cone_deg, cue_level, recall_s;
	uintptr_t pcomb, pc;
	double ask_t;
	/* the ignite */
	int ign_off, lit, b_was, phase_was;
	uintptr_t ign_atk;
	char ign_atk_name[96], ign_how[96];
	double lit_t;
	float tip[3], glow;
	double glow_t;
	/* the throw (the dagger's hand: tracking-space samples) */
	struct { double t; sv_vec3 p; } th[AB_HK];
	int th_n, th_i, grip_dn;
	double grip_t, fast_t, miss_t;
	float fast_v[3], fast_hand[3], eye[3], fwd[3];
	somvr_ab_throw fast_m, best_m;
	int thr_req, thr_offers;     /* the orcs the game offers the quick throw for now (asked every recheck) */
	uintptr_t thr_said_c;
	double thr_ask_t;
	float dhand[3], dglow;
	int dhand_ok;
	float thr_hand[3], thr_v[3];
	double thr_t, away_until, bow_t;
	/* the whistle */
	int mouth_h, mouth_armed, wh_req;
	double mouth_t0, wh_t;
	/* tests */
	_Atomic int req_probe, req_props, req_aim; /* req_aim: 1 + N, or -1 off */
	int aim_on;
	float aim_pt[3];
	int nprops;
	float props[AB2_PROPS][3];
	char prop_kind[AB2_PROPS][16];
	uint32_t lines, lsec_n;
	double lsec;
	uint64_t asks, ign_offers, ign_presses, ign_lit, ign_denied, ign_shots, ign_out, throws, thr_moves, thr_started,
		thr_refused, thr_none, recalls, wh_holds, wh_started, wh_refused;
} g_ab2;

static float ab2_len(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

static int ab2_log_ok(double t)
{
	if (g_ab2.lines < 300)
		return ++g_ab2.lines;
	if (t >= g_ab2.lsec + 1.0)
		g_ab2.lsec = t, g_ab2.lsec_n = 0;
	return g_ab2.lsec_n++ < 6;
}

static void ab2_start(int *bad)
{
	memset(&g_ab2, 0, sizeof g_ab2);
	somvr_ab2_cfg_default(&g_ab2.cfg);
	static const char *const ow[] = { "off", "on" };
	g_ab2.ignite = env_word("SOMVR_ABIL_IGNITE", ow, 2, 0);
	g_ab2.thr = env_word("SOMVR_ABIL_THROW", ow, 2, 0);
	g_ab2.whistle = env_word("SOMVR_ABIL_WHISTLE", ow, 2, 0);
	const char *e;
	snprintf(g_ab2.ign_node, sizeof g_ab2.ign_node, "%s", (e = getenv("SOMVR_ABIL_IGNITE_NODE")) && *e ? e :
		 "PC_Wraith_Ranged_Immolate_Shoot");
	snprintf(g_ab2.ign_name, sizeof g_ab2.ign_name, "%s", (e = getenv("SOMVR_ABIL_IGNITE_ATTACK")) && *e ? e :
		 "PC_Ranged_Ground_Immolate_Shoot_Single"); /* the node's pool 'PC_Ranged_Immolate_Shoot' (boot 20261030-145019) */
	snprintf(g_ab2.ign_skill, sizeof g_ab2.ign_skill, "%s", (e = getenv("SOMVR_ABIL_IGNITE_SKILL")) && *e ? e :
		 "PSS_Wraith_Ignite");
	snprintf(g_ab2.thr_node, sizeof g_ab2.thr_node, "%s", (e = getenv("SOMVR_ABIL_THROW_NODE")) && *e ? e :
		 "PC_Ranged_QuickThrow");
	snprintf(g_ab2.wh_node, sizeof g_ab2.wh_node, "%s", (e = getenv("SOMVR_ABIL_WHISTLE_NODE")) && *e ? e : "PC_Attract");
	float tw[4] = { g_ab2.cfg.throw_cms / 100, g_ab2.cfg.throw_fwd_deg, g_ab2.cfg.throw_low_cm, g_ab2.cfg.aim_level_deg };
	env_vec("SOMVR_ABIL_THROW_JUDGE", tw, 4, 0, 180, bad); /* m/s, deg ahead, cm under the eyes, deg off the line */
	g_ab2.cfg.throw_cms = tw[0] * 100, g_ab2.cfg.throw_fwd_deg = tw[1], g_ab2.cfg.throw_low_cm = tw[2];
	g_ab2.cfg.aim_level_deg = tw[3];
	g_ab2.cfg.mouth_cm = env_float("SOMVR_ABIL_WHISTLE_CM", g_ab2.cfg.mouth_cm, 5, 40, bad);
	g_ab2.cfg.mouth_s = 0.001f * env_float("SOMVR_ABIL_WHISTLE_MS", 1000 * g_ab2.cfg.mouth_s, 0, 3000, bad);
	g_ab2.ign_cone_deg = env_float("SOMVR_ABIL_IGNITE_CONE_DEG", 8, 1, 45, bad); /* the orc on the ray the selection asks */
	g_ab2.recall_s = env_float("SOMVR_ABIL_THROW_RECALL_MS", 900, 200, 5000, bad) / 1000; /* the dagger back in the hand */
	g_ab2.cue_level = g_abl.cue_level;
	g_ab2.mouth_h = -1, g_ab2.mouth_armed = 1;
	g_ab2.on = g_abl.mode != AB_OFF && (g_ab2.ignite || g_ab2.thr || g_ab2.whistle);
	if (!g_ab2.ignite && !g_ab2.thr && !g_ab2.whistle)
		return;
	if (g_abl.mode == AB_OFF) {
		logf_("CONFIG abil2: SOMVR_ABIL_IGNITE/_THROW/_WHISTLE need SOMVR_ABIL (off, or its selection did not match) - off");
		return;
	}
	if ((g_ab2.thr || g_ab2.whistle) && !g_abl.start_game)
		logf_("CONFIG abil2: no start site (FI_START): the quick throw and the whistle are logged only");
	logf_("CONFIG abil2 build 27c: the ignite shot %s (SOMVR_ABIL_IGNITE; node '%s' for the orc within %.0f deg of the ray, "
	      "else no target; by name '%s' with '%s' owned): B while an arrow is drawn kindles it, the release shoots it",
	      g_ab2.ignite ? "on" : "off", g_ab2.ign_node, g_ab2.ign_cone_deg, g_ab2.ign_name[0] ? g_ab2.ign_name : "-",
	      g_ab2.ign_skill);
	logf_("CONFIG abil2: the quick throw %s (SOMVR_ABIL_THROW; node '%s'): the dagger in the left hand thrown overhand - "
	      "%.1f+ m/s within %.0f deg of the head's way, the hand at most %.0f cm under the eyes, let go within %.2f s; the "
	      "orc within %.0f deg (level) / %.0f deg (pitch) of the line, %.0f-%.0f cm (SOMVR_ABIL_THROW_JUDGE); the dagger "
	      "back in the hand %.1f s after (SOMVR_ABIL_THROW_RECALL_MS)", g_ab2.thr ? "on" : "off", g_ab2.thr_node,
	      g_ab2.cfg.throw_cms / 100, g_ab2.cfg.throw_fwd_deg, g_ab2.cfg.throw_low_cm, g_ab2.cfg.throw_win_s,
	      g_ab2.cfg.aim_level_deg, g_ab2.cfg.aim_pitch_deg, g_ab2.cfg.aim_min_cm, g_ab2.cfg.aim_max_cm, g_ab2.recall_s);
	logf_("CONFIG abil2: the whistle %s (SOMVR_ABIL_WHISTLE; node '%s'): a free hand within %.0f cm of the mouth for %.2f s "
	      "(SOMVR_ABIL_WHISTLE_CM / _MS)", g_ab2.whistle ? "on" : "off", g_ab2.wh_node, g_ab2.cfg.mouth_cm, g_ab2.cfg.mouth_s);
}

/* the skill's owned bit in Talion's inventory (bz_skills' reads, by name; -1 = not read) */
static int ab2_skill_owned(uintptr_t pc, const char *name)
{
	uintptr_t cat = g_bz.ok_skill ? peek_ptr(BZ_SKILL_CAT_PP) : 0, base = cat ? peek_ptr(cat + CAT_RECS) : 0;
	uint32_t n = 0;
	uintptr_t ch = pc ? peek_ptr(pc + C_OWNER) : 0, inv = ch ? peek_ptr(ch + BZ_CH_INV) : 0;
	uintptr_t vb = inv ? peek_ptr(inv + BZ_INV_SKILLS) : 0, ve = inv ? peek_ptr(inv + BZ_INV_SKILLS + 8) : 0;
	if (!cat || !base || !peek(cat + CAT_COUNT, &n, sizeof n) || n > 4000 || !vb || ve < vb)
		return -1;
	uint32_t words = (uint32_t)((ve - vb) / 4);
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE, rc, rb;
		uint8_t fl = 0;
		char nm[80];
		if (!peek(rec + 1, &fl, 1) || !(rc = peek_ptr(rec + 0x18)) || strcmp(fb_rec_name(rec, nm, sizeof nm), name))
			continue;
		uint32_t word, bit, v = 0;
		if (!(fl & 2)) {
			rb = peek_ptr(rc + 0x48);
			uintptr_t ix = (rec - rb) >> 5;
			word = (uint32_t)(ix >> 5 & 0x7ffffff), bit = (uint32_t)(ix & 31);
		} else {
			rb = peek_ptr(rc + 0x38);
			uint32_t u = (uint32_t)((int32_t)((rec - rb) >> 4) * -0x55555555);
			word = u >> 5, bit = u & 31;
		}
		return word < words && peek(vb + (uintptr_t)word * 4, &v, 4) && (v >> bit & 1);
	}
	return -1;
}

/* an orc's body point for a line at him: his chest (the root is at the pelvis, ~1 m up), or a downed orc's body at the
 * ground (his root stays ~95 cm up while he lies: re-xr-pipeline.md 114.1) */
static void ab2_body_point(const struct cb_orc *o, const char *state, float p[3])
{
	memcpy(p, o->pos, 12);
	p[1] += sf_state_kind(state) == SF_ST_DOWN ? -70 : 35;
}

/* the game's selection for a node with no orc: the target object hobj (0 = none) at pos (the context's angle) */
static uintptr_t ab2_pick_at(uintptr_t pcomb, uintptr_t pc, uintptr_t hobj, const float pos[3], const char *node_name,
			     char *an, size_t na, const char **why)
{
	struct cb_orc o;
	memset(&o, 0, sizeof o);
	o.hobj = hobj;
	memcpy(o.pos, pos, 12);
	uintptr_t node = vb_node_named(pcomb, node_name), sy = 0, a = 0;
	an[0] = 0;
	if (!node)
		*why = "the root node is not in the player's tree";
	else if ((a = vb_pick(pcomb, pc, &o, node, &sy, why)))
		vb_name(a, an, na);
	return a;
}

/* the game's own attack of node_name started on orc oc (FUN_00a8c700, as the flash's start); 1 = started */
static int ab2_start_on(uintptr_t pcomb, uintptr_t pc, const char *node_name, const struct cb_orc *oc, const char *what,
			double t)
{
	uintptr_t node = vb_node_named(pcomb, node_name), sy = 0, atk = 0, n2 = 0;
	const char *why = "the root node is not in the player's tree";
	uint8_t pad = 0;
	char an[96] = "";
	if (node && (atk = vb_pick(pcomb, pc, oc, node, &sy, &why))) {
		peek(pcomb + FI_PC_PAD, &pad, 1);
		if (!(n2 = FI_FN(FIS_KBMNODE, fi_kbm_fn)(node, pad != 0)))
			why = "no KBM node";
	}
	if (!n2 || g_abl.mode != AB_ON || !g_abl.start_game) {
		logf_("ABIL2 t=%.3f %s on orc %#lx (%.0f cm): NOT started - %s", t, what, (unsigned long)oc->c, oc->dist,
		      !n2 ? why : g_abl.mode != AB_ON ? "logged only (SOMVR_ABIL=log)" : "no start site");
		return 0;
	}
	abl_watch_begin(pc, oc, 1, t, 3.0);
	FI_FN(FIS_START, fi_start_fn)(pcomb, n2, atk, oc->hobj);
	logf_("ABIL2 t=%.3f start: %s - FUN_00a8c700(pc=%#lx, node '%s', attack '%s', orc %#lx hobj %#lx at %.0f cm)", t, what,
	      (unsigned long)pcomb, node_name, vb_name(atk, an, sizeof an), (unsigned long)oc->c, (unsigned long)oc->hobj,
	      oc->dist);
	return 1;
}

/* the ignite's offer now: the orc on the bow's ray (within ign_cone_deg, 40 m), else no target, else by name */
static void ab2_ignite_ask(uintptr_t pcomb, uintptr_t pc, double t)
{
	const somvr_bow *s = &g_bow.s;
	int was = g_ab2.ign_off;
	char was_how[96];
	snprintf(was_how, sizeof was_how, "%s", g_ab2.ign_how);
	g_ab2.ign_off = 0, g_ab2.ign_atk = 0, g_ab2.ign_atk_name[0] = 0;
	snprintf(g_ab2.ign_how, sizeof g_ab2.ign_how, "-");
	if (!s->have_ray)
		return;
	float o[3] = { s->origin.x, s->origin.y, s->origin.z }, d[3] = { s->dir.x, s->dir.y, s->dir.z };
	struct cb_orc nr[8];
	int n = cb_orcs_r(pcomb, pc, o, 4000, nr, 8), best = -1;
	float bd = g_ab2.ign_cone_deg;
	for (int i = 0; i < n; i++) {
		float to[3] = { nr[i].pos[0] - o[0], nr[i].pos[1] + 35 - o[1], nr[i].pos[2] - o[2] }, l = ab2_len(to);
		float c = l > 1 ? (to[0] * d[0] + to[1] * d[1] + to[2] * d[2]) / l : -1, a = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
		if (a <= bd)
			bd = a, best = i;
	}
	const char *why = "";
	char an[96] = "";
	uintptr_t at = 0;
	if (best >= 0 && (at = gc_pick(pcomb, pc, &nr[best], g_ab2.ign_node, an, sizeof an, (char[96]){ 0 }, 96, &why)))
		snprintf(g_ab2.ign_how, sizeof g_ab2.ign_how, "the orc on the ray (%.1f deg, %.0f cm)", bd, nr[best].dist);
	if (!at) {
		float p[3] = { o[0] + d[0] * 2000, o[1] + d[1] * 2000, o[2] + d[2] * 2000 };
		if ((at = ab2_pick_at(pcomb, pc, 0, p, g_ab2.ign_node, an, sizeof an, &why)))
			snprintf(g_ab2.ign_how, sizeof g_ab2.ign_how, "no target");
	}
	if (!at && g_ab2.ign_name[0] && ab2_skill_owned(pc, g_ab2.ign_skill) == 1 &&
	    (at = CB_FN(CS_ATK_BYNAME, cb_byname_fn)(g_ab2.ign_name))) {
		snprintf(an, sizeof an, "%s", g_ab2.ign_name);
		snprintf(g_ab2.ign_how, sizeof g_ab2.ign_how, "by name, '%s' owned", g_ab2.ign_skill);
	}
	if (at) {
		g_ab2.ign_off = 1, g_ab2.ign_atk = at;
		snprintf(g_ab2.ign_atk_name, sizeof g_ab2.ign_atk_name, "%s", an);
	}
	if (g_ab2.ign_off != was || (g_ab2.ign_off && strcmp(was_how, g_ab2.ign_how) && strncmp(was_how, g_ab2.ign_how, 10))) {
		g_ab2.ign_offers += g_ab2.ign_off;
		if (ab2_log_ok(t))
			logf_("ABIL2 t=%.3f the ignite: %s%s%s%s (%s)", t, g_ab2.ign_off ? "the game offers '" : "not offered",
			      g_ab2.ign_off ? g_ab2.ign_atk_name : "", g_ab2.ign_off ? "' - " : "", g_ab2.ign_off ? g_ab2.ign_how : "",
			      g_ab2.ign_off ? "the arrow drawn" : why[0] ? why : "no orc on the ray, none with no target");
	}
}

/* the throw: the orc nearest its line that the game offers the quick throw for, started */
static void ab2_throw_go(uintptr_t pcomb, uintptr_t pc, double t)
{
	struct cb_orc nr[8], *cand[8];
	float lv[8], pv[8], dv[8];
	int n = cb_orcs_r(pcomb, pc, g_ab2.thr_hand, g_ab2.cfg.aim_max_cm + 200, nr, 8), m = 0;
	for (int i = 0; i < n; i++) {
		char sn[96];
		float chest[3] = { nr[i].pos[0], nr[i].pos[1], nr[i].pos[2] };
		ab2_body_point(&nr[i], vb_name(peek_ptr(nr[i].c + VB_C_STATE), sn, sizeof sn), chest);
		float l, p, d;
		int in = somvr_ab_throw_aim(&g_ab2.cfg, g_ab2.thr_hand, g_ab2.thr_v, chest, &l, &p, &d);
		if (ab2_log_ok(t))
			logf_("ABIL2 t=%.3f the throw's line: orc %#lx at %.0f cm, %+.1f deg level, %+.1f deg pitch off it%s", t,
			      (unsigned long)nr[i].c, d, l, p, in ? "" : " - outside");
		if (!in)
			continue;
		int at = m++;
		while (at > 0 && fabsf(lv[at - 1]) > fabsf(l))
			cand[at] = cand[at - 1], lv[at] = lv[at - 1], pv[at] = pv[at - 1], dv[at] = dv[at - 1], at--;
		cand[at] = &nr[i], lv[at] = l, pv[at] = p, dv[at] = d;
	}
	for (int i = 0; i < m; i++) {
		const char *why = "";
		char an[96], sn[96];
		if (!gc_pick(pcomb, pc, cand[i], g_ab2.thr_node, an, sizeof an, sn, sizeof sn, &why)) {
			logf_("ABIL2 t=%.3f the quick throw at orc %#lx (%.1f deg off the line): not offered (%s)", t,
			      (unsigned long)cand[i]->c, lv[i], why);
			continue;
		}
		if (ab2_start_on(pcomb, pc, g_ab2.thr_node, cand[i], "the quick throw", t)) {
			g_ab2.thr_started++;
			if (g_vb.haptic > 0)
				in_haptic_event(somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_DAGGER), 0.7f * g_vb.haptic, 0.06f, SOMVR_HAP_COMBAT);
		} else
			g_ab2.thr_refused++;
		return;
	}
	g_ab2.thr_none++;
	logf_("ABIL2 t=%.3f the quick throw: no orc on the line the game offers it for (%d near, %d in the cone): nothing", t, n, m);
	g_ab2.away_until = 0; /* no flight: the dagger stays in the hand */
	if (g_vb.haptic > 0)
		in_haptic_event(somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_DAGGER), 0.25f * g_vb.haptic, 0.08f, SOMVR_HAP_COMBAT);
}

/* the whistle: the nearest orc the game offers it for, started */
static void ab2_whistle_go(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	struct cb_orc nr[8];
	int n = cb_orcs_r(pcomb, pc, root, 3000, nr, 8);
	for (int i = 0; i < n; i++) {
		const char *why = "";
		char an[96], sn[96];
		if (!gc_pick(pcomb, pc, &nr[i], g_ab2.wh_node, an, sizeof an, sn, sizeof sn, &why)) {
			if (ab2_log_ok(t))
				logf_("ABIL2 t=%.3f the whistle for orc %#lx at %.0f cm: not offered (%s)", t, (unsigned long)nr[i].c,
				      nr[i].dist, why);
			continue;
		}
		if (ab2_start_on(pcomb, pc, g_ab2.wh_node, &nr[i], "the whistle", t))
			g_ab2.wh_started++;
		else
			g_ab2.wh_refused++;
		return;
	}
	g_ab2.wh_refused++;
	logf_("ABIL2 t=%.3f the whistle: no orc within 30 m the game offers it for (%d near): nothing", t, n);
}

static void ab2_probe(uintptr_t pcomb, uintptr_t pc, const float *root, double t);
static void ab2_props(const float *root, double t);

/* abl_frame's (the game thread, vb_frame): the tests, the throw's and the whistle's starts, the ignite's offer */
static void ab2_frame(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	if (!g_ab2.on && !g_abl.test)
		return;
	g_ab2.pcomb = pcomb, g_ab2.pc = pc;
	if (!pcomb || !pc)
		return;
	if (!root) /* (the combat frame without Talion's body: the eyes stand in; boot 20261030-165922's second throw) */
		root = g_ab2.eye;
	if (atomic_exchange(&g_ab2.req_probe, 0))
		ab2_probe(pcomb, pc, root, t);
	if (atomic_exchange(&g_ab2.req_props, 0))
		ab2_props(root, t);
	int ra = atomic_exchange(&g_ab2.req_aim, 0);
	if (ra < 0)
		g_ab2.aim_on = 0, logf_("ABIL2 t=%.3f aim off (a test aid)", t);
	else if (ra > 0 && ra - 1 < g_ab2.nprops) {
		memcpy(g_ab2.aim_pt, g_ab2.props[ra - 1], 12), g_ab2.aim_on = 1;
		float dx = g_ab2.aim_pt[0] - root[0], dz = g_ab2.aim_pt[2] - root[2];
		float by = remainderf(atan2f(dx, dz) - atan2f(g_abl.fwd[0], g_abl.fwd[2]), 6.2831853f) * 57.29578f;
		logf_("ABIL2 t=%.3f aim: the next shots' ray onto prop %d '%s' at %.0f,%.0f,%.0f, %.0f cm away, %+.0f deg right of the "
		      "head (a test aid)", t, ra - 1, g_ab2.prop_kind[ra - 1], g_ab2.aim_pt[0], g_ab2.aim_pt[1], g_ab2.aim_pt[2],
		      dist3(g_ab2.aim_pt, root), by);
	} else if (ra > 0)
		logf_("ABIL2 t=%.3f aim: no prop %d (`abil2 props` first)", t, ra - 1);
	if (!g_ab2.on)
		return;
	if (g_ab2.thr_req) {
		g_ab2.thr_req = 0;
		ab2_throw_go(pcomb, pc, t);
	}
	/* the quick throw's offer while the dagger is in the hand (the cue; the log on a change) */
	int dag = g_ab2.thr && g_sl.on && g_sl.have_g && sl_dagger_drawn(&g_sl.g);
	if (!dag)
		g_ab2.thr_offers = 0, g_ab2.thr_said_c = 0;
	else if (t - g_ab2.thr_ask_t >= g_gc.recheck_s) {
		g_ab2.thr_ask_t = t;
		struct cb_orc nr[4];
		int n = cb_orcs_r(pcomb, pc, root, g_ab2.cfg.aim_max_cm, nr, 4), k = 0;
		uintptr_t first = 0;
		const char *why = "no orc within reach";
		char an[96] = "", sn[96], st[96] = "-";
		float fd = 0, by = 0;
		for (int i = 0; i < n; i++) {
			char a[96];
			const char *w2 = "";
			if (gc_pick(pcomb, pc, &nr[i], g_ab2.thr_node, a, sizeof a, sn, sizeof sn, &w2)) {
				if (!k++)
					first = nr[i].c, fd = nr[i].dist, snprintf(an, sizeof an, "%s", a),
					vb_name(peek_ptr(nr[i].c + VB_C_STATE), st, sizeof st),
					by = remainderf(atan2f(nr[i].pos[0] - root[0], nr[i].pos[2] - root[2]) - atan2f(g_ab2.fwd[0], g_ab2.fwd[2]),
							6.2831853f) * 57.29578f;
			} else if (i == 0)
				why = w2, fd = nr[i].dist, vb_name(peek_ptr(nr[i].c + VB_C_STATE), st, sizeof st), first = 0;
		}
		uintptr_t key = first ? first : (n ? nr[0].c : 0);
		if ((k > 0) != (g_ab2.thr_offers > 0) || key != g_ab2.thr_said_c) {
			if (ab2_log_ok(t))
				logf_("ABIL2 t=%.3f the quick throw: %s%s%s (%d near; the nearest offered/asked at %.0f cm, %+.0f deg right of the "
				      "head, state '%s', health %.1f)%s%s", t, k ? "the game offers it - '" : "not offered", k ? an : "",
				      k ? "'" : "", n, fd, k ? by : 0.0f, st, key ? vb_health(key) : 0.0f, k ? "" : ": ", k ? "" : why);
			if (k && !g_ab2.thr_offers && g_vb.haptic > 0)
				in_haptic_event(somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_DAGGER), 0.3f * g_vb.haptic, 0.03f, SOMVR_HAP_COMBAT);
		}
		g_ab2.thr_offers = k, g_ab2.thr_said_c = key;
	}
	if (g_ab2.wh_req) {
		g_ab2.wh_req = 0;
		ab2_whistle_go(pcomb, pc, root, t);
	}
	if (g_ab2.ignite && g_bow.on && (g_bow.s.phase == SOMVR_BOW_PH_DRAW || g_ab2.lit) && t - g_ab2.ask_t >= g_gc.recheck_s) {
		g_ab2.ask_t = t, g_ab2.asks++;
		ab2_ignite_ask(pcomb, pc, t);
	} else if (g_bow.s.phase != SOMVR_BOW_PH_DRAW && !g_ab2.lit)
		g_ab2.ign_off = 0;
}

/* the stereo frame's hands (after abl_hands): the ignite's button, the throw, the whistle */
static void ab2_hands(const sv_vec3 p[2], const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	if (!g_ab2.on)
		return;
	double t = cb_now();
	sv_vec3 hf = sv_qrot(sv_qnormalize(hq), (sv_vec3){ 0, 0, 1 });
	g_ab2.eye[0] = hp.x, g_ab2.eye[1] = hp.y, g_ab2.eye[2] = hp.z;
	g_ab2.fwd[0] = hf.x, g_ab2.fwd[1] = hf.y, g_ab2.fwd[2] = hf.z;
	/* ---- the ignite: B on the string hand while an arrow is drawn */
	if (g_ab2.ignite && g_bow.on) {
		int sh = 1 - g_bow.cfg.bow_hand, ph = g_bow.s.phase;
		int b = (g_inp.ctl.down[sh] & SOMVR_BTN_UPPER) != 0, edge = b && !g_ab2.b_was;
		g_ab2.b_was = b;
		if (g_ab2.lit && (ph == SOMVR_BOW_PH_IDLE || ph == SOMVR_BOW_PH_AIM)) { /* let down or put away: out */
			g_ab2.lit = 0, g_ab2.ign_out++;
			if (ab2_log_ok(t))
				logf_("ABIL2 t=%.3f the ignite put out: the arrow %s", t, ph == SOMVR_BOW_PH_AIM ? "let down" : "away");
		}
		if (edge && ph == SOMVR_BOW_PH_DRAW) {
			g_ab2.ign_presses++;
			if (g_ab2.lit) {
				g_ab2.lit = 0, g_ab2.ign_out++;
				logf_("ABIL2 t=%.3f the ignite put out (B again)", t);
			} else if (g_ab2.ign_off && g_ab2.ign_atk) {
				g_ab2.lit = 1, g_ab2.lit_t = t, g_ab2.ign_lit++;
				in_haptic_event(sh, 0.45f, 0.05f, SOMVR_HAP_COMBAT), in_haptic_event(1 - sh, 0.25f, 0.03f, SOMVR_HAP_COMBAT);
				logf_("ABIL2 t=%.3f the ignite kindled: B while drawn (%.0f cm), the game offers '%s' (%s)", t,
				      g_bow.s.draw_cm, g_ab2.ign_atk_name, g_ab2.ign_how);
			} else {
				g_ab2.ign_denied++;
				in_haptic_event(sh, 0.2f, 0.08f, SOMVR_HAP_COMBAT);
				logf_("ABIL2 t=%.3f B while drawn: the game does not offer the ignite now - nothing kindled", t);
			}
		} else if (edge && ab2_log_ok(t))
			logf_("ABIL2 t=%.3f B with the bow %s: not drawn - no ignite", t, somvr_bow_phase_name(ph));
		if (g_bow.s.have_ray)
			g_ab2.tip[0] = g_bow.s.origin.x + g_bow.s.dir.x * 6, g_ab2.tip[1] = g_bow.s.origin.y + g_bow.s.dir.y * 6,
			g_ab2.tip[2] = g_bow.s.origin.z + g_bow.s.dir.z * 6;
	}
	/* ---- the quick throw: the dagger in its hand, an overhand throw ahead, the grip let go */
	int dh = somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_DAGGER);
	int dag = g_ab2.thr && g_sl.on && g_sl.have_g && sl_dagger_drawn(&g_sl.g) && g_inp.hands[dh].valid;
	g_ab2.dhand_ok = dag;
	if (dag)
		g_ab2.dhand[0] = pw[dh].x, g_ab2.dhand[1] = pw[dh].y, g_ab2.dhand[2] = pw[dh].z;
	if (!dag) {
		g_ab2.th_n = 0, g_ab2.grip_dn = 0;
	} else {
		g_ab2.th[g_ab2.th_i].t = t, g_ab2.th[g_ab2.th_i].p = p[dh];
		int now = g_ab2.th_i;
		g_ab2.th_i = (g_ab2.th_i + 1) % AB_HK, g_ab2.th_n += g_ab2.th_n < AB_HK;
		int k = -1;
		for (int j = 1; j < g_ab2.th_n; j++) { /* the newest sample 80-160 ms older */
			int ix = (now - j + AB_HK) % AB_HK;
			double age = g_ab2.th[now].t - g_ab2.th[ix].t;
			if (age > 0.16)
				break;
			if (age >= 0.08) {
				k = ix;
				break;
			}
		}
		/* the hand's way: over its newest 80-160 ms (as the push's), or the runtime's own velocity of the grip (a slow
		 * frame rate puts no two samples inside a throw: headless boot 20261030-145019 ran 3-4 frames a second), the
		 * faster of the two */
		float v[3] = { 0, 0, 0 }, vr[3] = { 0, 0, 0 };
		if (k >= 0) {
			double age = g_ab2.th[now].t - g_ab2.th[k].t;
			sv_vec3 a = somvr_rig_point(&g_rig, g_fr.base, g_ab2.th[k].p), b = somvr_rig_point(&g_rig, g_fr.base, g_ab2.th[now].p);
			v[0] = (float)((b.x - a.x) / age), v[1] = (float)((b.y - a.y) / age), v[2] = (float)((b.z - a.z) / age);
		}
		if (g_inp.hands[dh].vel_valid & 1) {
			sv_vec3 hv;
			somvr_hand_game(&g_inp.hands[dh], NULL, NULL, &hv, NULL);
			hv = somvr_rig_vec(&g_rig, g_fr.base, hv);
			vr[0] = hv.x, vr[1] = hv.y, vr[2] = hv.z;
			if (ab2_len(vr) > ab2_len(v))
				memcpy(v, vr, sizeof v);
		}
		{
			float hw[3] = { pw[dh].x, pw[dh].y, pw[dh].z };
			somvr_ab_throw m;
			if (ab2_len(v) >= 0.6f * g_ab2.cfg.throw_cms) {
				int ok = somvr_ab_throw_judge(&g_ab2.cfg, v, g_ab2.eye, g_ab2.fwd, hw, &m);
				if (ok)
					g_ab2.fast_t = t, memcpy(g_ab2.fast_v, v, 12), memcpy(g_ab2.fast_hand, hw, 12), g_ab2.fast_m = m;
				else if (!(t - g_ab2.miss_t < 0.3 && m.speed <= g_ab2.best_m.speed))
					g_ab2.best_m = m, g_ab2.miss_t = t;
			}
		}
		const somvr_hand_in *hi = &g_inp.st.hand[dh];
		float sq = bd_squeeze(dh, (hi->active & SOMVR_HACT_INPUT) ? hi->squeeze : 0);
		int was = g_ab2.grip_dn;
		if (was ? sq < 0.35f : sq > 0.6f)
			g_ab2.grip_dn = !was;
		if (g_ab2.grip_dn && !was)
			g_ab2.grip_t = t;
		if (!g_ab2.grip_dn && was) { /* the grip let go: a throw if the fast move was just now */
			if (t - g_ab2.fast_t <= g_ab2.cfg.throw_win_s && t - g_ab2.thr_t > 1.0 && t >= g_ab2.away_until) {
				g_ab2.throws++, g_ab2.thr_req = 1, g_ab2.thr_t = t;
				memcpy(g_ab2.thr_hand, g_ab2.fast_hand, 12), memcpy(g_ab2.thr_v, g_ab2.fast_v, 12);
				g_ab2.away_until = t + g_ab2.recall_s; /* the dagger leaves the hand for the flight */
				logf_("ABIL2 t=%.3f throw: the dagger let go %.2f s after an overhand throw - %.2f m/s, %.0f deg off the head's "
				      "way, pitch %+.0f deg, the hand %.0f cm under the eyes", t, t - g_ab2.fast_t, g_ab2.fast_m.speed / 100,
				      g_ab2.fast_m.fwd_deg, g_ab2.fast_m.pitch_deg, g_ab2.fast_m.low_cm);
			} else if (t - g_ab2.miss_t <= 0.4) {
				g_ab2.thr_moves++;
				if (ab2_log_ok(t))
					logf_("ABIL2 t=%.3f the dagger let go after a fast move, not a throw (%s): %.2f m/s, %.0f deg off the "
					      "head's way, pitch %+.0f deg, the hand %.0f cm under the eyes", t, g_ab2.best_m.why,
					      g_ab2.best_m.speed / 100, g_ab2.best_m.fwd_deg, g_ab2.best_m.pitch_deg, g_ab2.best_m.low_cm);
			}
		}
	}
	if (g_ab2.away_until > 0 && t >= g_ab2.away_until) {
		g_ab2.away_until = 0, g_ab2.recalls++;
		if (g_vb.haptic > 0)
			in_haptic_event(dh, 0.55f * g_vb.haptic, 0.05f, SOMVR_HAP_COMBAT); /* the dagger back: a thunk */
		if (ab2_log_ok(t))
			logf_("ABIL2 t=%.3f the dagger's recall: back to the hand (the slots' restore)", t);
	}
	/* ---- the whistle: a free hand at the mouth */
	if (bo_out())
		g_ab2.bow_t = t;
	if (g_ab2.whistle && t - g_ab2.bow_t > 1.5) { /* (the bow's string hand anchors at the mouth's corner) */
		int at = -1;
		float dmin = 1e9f;
		for (int h = 0; h < 2; h++) {
			if (!g_inp.hands[h].valid || cl_holds(h) || (h == SOMVR_HAND_L && vb_grab_held()) ||
			    (h == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD) && bd_sword_in_hand()) ||
			    (h == dh && g_sl.on && g_sl.have_g && sl_dagger_drawn(&g_sl.g))) /* two fingers to the lips: an empty hand */
				continue;
			float hw[3] = { pw[h].x, pw[h].y, pw[h].z }, d = somvr_ab_mouth_dist(g_ab2.eye, g_ab2.fwd, hw);
			if (d < dmin)
				dmin = d, at = h;
		}
		if (at >= 0 && dmin <= g_ab2.cfg.mouth_cm) {
			if (g_ab2.mouth_h != at)
				g_ab2.mouth_h = at, g_ab2.mouth_t0 = t;
			if (g_ab2.mouth_armed && t - g_ab2.mouth_t0 >= g_ab2.cfg.mouth_s && t - g_ab2.wh_t > 2.0) {
				g_ab2.mouth_armed = 0, g_ab2.wh_t = t, g_ab2.wh_req = 1, g_ab2.wh_holds++;
				in_haptic_event(at, 0.3f, 0.04f, SOMVR_HAP_COMBAT);
				logf_("ABIL2 t=%.3f whistle: the %s hand at the mouth (%.0f cm) for %.2f s", t, at ? "right" : "left", dmin,
				      t - g_ab2.mouth_t0);
			}
		} else {
			g_ab2.mouth_h = -1;
			if (!(dmin <= g_ab2.cfg.mouth_cm + g_ab2.cfg.mouth_out_cm))
				g_ab2.mouth_armed = 1;
		}
	}
}

/* sl_frame: the dagger's hand must not hold it while the dagger flies (the slots put it back, then restore it) */
static int ab2_dagger_away(int h)
{
	return g_ab2.away_until > 0 && h == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_DAGGER);
}

/* bo_shoot: the shot's attack - the ignite's record when kindled (the projectile takes its RangedWeaponOverride) */
static uintptr_t ab2_shot_attack(uintptr_t atk, double t)
{
	if (!g_ab2.lit)
		return atk;
	g_ab2.lit = 0;
	if (!g_ab2.ign_atk || g_abl.mode != AB_ON) {
		logf_("ABIL2 t=%.3f the shot: kindled, but %s - the normal arrow", t, g_abl.mode != AB_ON ? "logged only" :
		      "no ignite record");
		return atk;
	}
	g_ab2.ign_shots++;
	char an[96], ad[160];
	logf_("ABIL2 t=%.3f the shot: the ignite - attack '%s' (%s) instead of '%s'; %s", t, g_ab2.ign_atk_name, g_ab2.ign_how,
	      vb_name(atk, an, sizeof an), cb_ad_desc(g_ab2.ign_atk, ad, sizeof ad));
	return g_ab2.ign_atk;
}

/* bo_shoot: the test aid `abil2 aim N` - the shot's ray turned onto prop N */
static void ab2_shot_aim(sv_vec3 *o, sv_vec3 *d, double t)
{
	if (!g_ab2.aim_on)
		return;
	sv_vec3 v = { g_ab2.aim_pt[0] - o->x, g_ab2.aim_pt[1] - o->y, g_ab2.aim_pt[2] - o->z };
	float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	if (!(l > 1))
		return;
	v = (sv_vec3){ v.x / l, v.y / l, v.z / l };
	float c = v.x * d->x + v.y * d->y + v.z * d->z;
	logf_("ABIL2 t=%.3f test: the shot's ray turned %.1f deg onto the prop %.1f m away (abil2 aim)", t,
	      acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f, l / 100);
	*d = v;
}

/* th_frame: the kindled arrow's tip */
static void ab2_glow_add(struct th_glow *gl, int *ng)
{
	if (!g_ab2.on || g_abl.mode != AB_ON || !(g_ab2.cue_level > 0))
		return;
	double t = cb_now();
	float dt = g_ab2.glow_t > 0 && t > g_ab2.glow_t ? (float)(t - g_ab2.glow_t) : 0, a = dt / 0.12f;
	g_ab2.glow_t = t;
	a = a < 1 ? a : 1;
	g_ab2.glow += ((g_ab2.lit ? g_ab2.cue_level * 1.6f : 0) - g_ab2.glow) * a;
	g_ab2.dglow += ((g_ab2.thr_offers > 0 && g_ab2.dhand_ok ? g_ab2.cue_level * 0.8f : 0) - g_ab2.dglow) * a;
	if (g_ab2.dglow > 0.01f && vec_ok(g_ab2.dhand, 1e7f))
		th_glow_add(gl, ng, g_ab2.dhand, g_ab2.dglow, dist3(g_ab2.dhand, g_fr.head), SOMVR_THK_MELEE | SOMVR_THK_BLUE);
	if (g_ab2.glow > 0.01f && vec_ok(g_ab2.tip, 1e7f))
		th_glow_add(gl, ng, g_ab2.tip, g_ab2.glow > 1 ? 1 : g_ab2.glow, dist3(g_ab2.tip, g_fr.head),
			    SOMVR_THK_PROJ | SOMVR_THK_BLUE);
}

/* ---- tests -------------------------------------------------------------------------------------------------------- */
/* `abil2 probe`: the three nodes against the near orcs (and the ignite with no target), the skills */
static void ab2_probe(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	struct cb_orc nr[4];
	int n = cb_orcs_r(pcomb, pc, root, 3000, nr, 4);
	const char *nodes[] = { g_ab2.ign_node, g_ab2.thr_node, g_ab2.wh_node, "PC_Taunt", "PC_StealthTorment",
				"PC_Wraith_Ranged_Normal_Distract", "PC_Wraith_Dash_Kill_GetThere", "PC_Ranged_QuickThrow_Upgrade_Attack1",
				"PC_Wraith_Ranged_Immolate_HC_Shoot" };
	logf_("ABIL2 probe t=%.3f: skills Wraith_Ignite %d, Quick_Throw_1 %d, Whistle %d, Taunt %d, Shadow_Chaining %d, "
	      "Wraith_Distract %d; %d orc(s) within 30 m", t, ab2_skill_owned(pc, "PSS_Wraith_Ignite"),
	      ab2_skill_owned(pc, "PSS_Quick_Throw_1"), ab2_skill_owned(pc, "PSS_Whistle"), ab2_skill_owned(pc, "PSS_Taunt"),
	      ab2_skill_owned(pc, "PSS_Shadow_Chaining"), ab2_skill_owned(pc, "PSS_Wraith_Distract"), n);
	g_vb_pick_nowin = 1;
	for (int i = 0; i < n; i++) {
		char line[1600], a[96], s[96], sn[96];
		int k0 = snprintf(line, sizeof line, "ABIL2 probe t=%.3f orc %#lx at %.0f cm (state '%s'):", t, (unsigned long)nr[i].c,
				  nr[i].dist, vb_name(peek_ptr(nr[i].c + VB_C_STATE), sn, sizeof sn));
		for (size_t j = 0; j < sizeof nodes / sizeof nodes[0] && k0 < (int)sizeof line - 1; j++) {
			const char *why = "";
			uintptr_t at = gc_pick(pcomb, pc, &nr[i], nodes[j], a, sizeof a, s, sizeof s, &why);
			k0 += snprintf(line + k0, sizeof line - (size_t)k0, " %s -> %s%s%s;", nodes[j], at ? "'" : "none (", at ? a : why,
				       at ? "'" : ")");
		}
		logf_("%s", line);
	}
	float p[3] = { root[0] + g_abl.fwd[0] * 1500, root[1] + 150, root[2] + g_abl.fwd[2] * 1500 };
	for (size_t j = 0; j < 3; j++) {
		const char *why = "";
		char a[96];
		uintptr_t at = ab2_pick_at(pcomb, pc, 0, p, j == 0 ? g_ab2.ign_node : j == 1 ? "PC_Wraith_Ranged_Immolate_HC_Shoot" :
					   g_ab2.thr_node, a, sizeof a, &why);
		logf_("ABIL2 probe t=%.3f no target (15 m ahead): %s -> %s%s%s", t, j == 0 ? g_ab2.ign_node : j == 1 ?
		      "PC_Wraith_Ranged_Immolate_HC_Shoot" : g_ab2.thr_node, at ? "'" : "none (", at ? a : why, at ? "'" : ")");
	}
	g_vb_pick_nowin = 0;
}

/* `abil2 props`: the nav markers 'Explosive' and 'Firepit' (pr_navdump's list) nearest Talion, kept for `abil2 aim N` */
static void ab2_props(const float *root, double t)
{
	static int ok = -1;
	if (ok < 0) {
		char why[160];
		ok = check_site(&g_prn_sites[PRN_NAV_POS], why, sizeof why) == 0;
		logf_("ABIL2 props: site %s %s", g_prn_sites[PRN_NAV_POS].name, ok ? "ok" : "MISMATCH (no props)");
	}
	g_ab2.nprops = 0;
	if (!ok)
		return;
	uintptr_t node = 0;
	peek(PRN_LIST + 8, &node, sizeof node);
	int n = 0, cnt_kind = 0, cnt_on = 0, cnt_obj = 0;
	float dk[AB2_PROPS];
	while (node && node != PRN_LIST && n < 2048) {
		uintptr_t mk = node - PRN_NODE, rec = 0, nx = 0, np = 0, o4[4] = { 0, 0, 0, 0 };
		uint8_t ae[2] = { 0, 0 }, rf = 0;
		char name[32] = "";
		peek(mk + PRN_REC, &rec, sizeof rec), peek(mk + PRN_ACTIVE, ae, sizeof ae);
		peek(mk + 0x260, &o4[0], 8), peek(mk + 0x268, &o4[1], 8), peek(mk + 0x270, &o4[2], 8), peek(mk + 0xb0, &o4[3], 8);
		if (rec && peek(rec + 1, &rf, 1) && (rf & 2) && peek(rec + 0x20, &np, sizeof np) && np)
			for (int i = 0; i + 1 < (int)sizeof name; i++) {
				char c = 0;
				if (!peek(np + (uintptr_t)i, &c, 1) || !c)
					break;
				name[i] = c, name[i + 1] = 0;
			}
		int kind = !strcmp(name, "Explosive") || !strcmp(name, "Firepit");
		if (kind)
			cnt_kind++, cnt_on += ae[0] && ae[1], cnt_obj += o4[0] || o4[1] || o4[2] || o4[3];
		if (kind && rec && (o4[0] || o4[1] || o4[2] || o4[3])) {
			float pos[3] = { 0, 0, 0 };
			((prn_pos_fn)g_prn_sites[PRN_NAV_POS].target)(mk, pos);
			float d = dist3(pos, root);
			int at = g_ab2.nprops < AB2_PROPS ? g_ab2.nprops++ : d < dk[AB2_PROPS - 1] ? AB2_PROPS - 1 : -1;
			if (at >= 0) {
				while (at > 0 && dk[at - 1] > d)
					memcpy(g_ab2.props[at], g_ab2.props[at - 1], 12), dk[at] = dk[at - 1],
					memcpy(g_ab2.prop_kind[at], g_ab2.prop_kind[at - 1], 16), at--;
				memcpy(g_ab2.props[at], pos, 12), dk[at] = d,
				snprintf(g_ab2.prop_kind[at], 16, "%s%s", name, ae[0] && ae[1] ? "" : "-");
			}
		}
		n++;
		peek(node + 8, &nx, sizeof nx);
		node = nx;
	}
	logf_("ABIL2 props t=%.3f: %d markers; explosives / fire pits %d (active and enabled %d, with an object %d); the nearest "
	      "%d ('-': not active or enabled):", t, n, cnt_kind, cnt_on, cnt_obj, g_ab2.nprops);
	for (int i = 0; i < g_ab2.nprops; i++)
		logf_("ABIL2 props   %d '%s' at %.0f %.0f %.0f, %.1f m from Talion", i, g_ab2.prop_kind[i], g_ab2.props[i][0],
		      g_ab2.props[i][1], g_ab2.props[i][2], dk[i] / 100);
}

/* "abil2 ..." (the writer thread) */
static const char *ab2_word(const char *w)
{
	if (!strcmp(w, "status")) {
		logf_("ABIL2 status: ignite %s throw %s whistle %s; asks %llu, ignite offers %llu presses %llu lit %llu denied %llu shots "
		      "%llu out %llu; throws %llu (other let-gos %llu) started %llu refused %llu none %llu recalls %llu; whistles %llu "
		      "started %llu refused %llu", g_ab2.ignite ? "on" : "off", g_ab2.thr ? "on" : "off", g_ab2.whistle ? "on" : "off",
		      (unsigned long long)g_ab2.asks, (unsigned long long)g_ab2.ign_offers, (unsigned long long)g_ab2.ign_presses,
		      (unsigned long long)g_ab2.ign_lit, (unsigned long long)g_ab2.ign_denied, (unsigned long long)g_ab2.ign_shots,
		      (unsigned long long)g_ab2.ign_out, (unsigned long long)g_ab2.throws, (unsigned long long)g_ab2.thr_moves,
		      (unsigned long long)g_ab2.thr_started, (unsigned long long)g_ab2.thr_refused, (unsigned long long)g_ab2.thr_none,
		      (unsigned long long)g_ab2.recalls, (unsigned long long)g_ab2.wh_holds, (unsigned long long)g_ab2.wh_started,
		      (unsigned long long)g_ab2.wh_refused);
		return "abil2 status";
	}
	if (!g_abl.test)
		return "abil2 (SOMVR_HANDS_TEST is off: ignored)";
	if (!strcmp(w, "probe")) {
		atomic_store(&g_ab2.req_probe, 1);
		return "abil2 probe (a test: the ignite, the quick throw, the whistle and their kin against the near orcs)";
	}
	if (!strcmp(w, "props")) {
		atomic_store(&g_ab2.req_props, 1);
		return "abil2 props (a test: the nearest explosives and fire pits)";
	}
	if (!strcmp(w, "aim off")) {
		atomic_store(&g_ab2.req_aim, -1);
		return "abil2 aim off";
	}
	if (!strncmp(w, "aim ", 4)) {
		long k = strtol(w + 4, NULL, 10);
		if (k < 0 || k >= AB2_PROPS)
			return "abil2 aim N (0..11: ignored)";
		atomic_store(&g_ab2.req_aim, (int)k + 1);
		return "abil2 aim (a test aid: the next shots' ray onto prop N)";
	}
	return "abil2 status|probe|props|aim N|aim off";
}

static void ab2_end_log(void)
{
	if (!g_ab2.on)
		return;
	logf_("ABIL2 END build 27c: ignite offers %llu, presses %llu, lit %llu, denied %llu, shots %llu, put out %llu; throws %llu "
	      "(other let-gos %llu), started %llu, refused %llu, no orc %llu, recalls %llu; whistles %llu, started %llu, refused "
	      "%llu", (unsigned long long)g_ab2.ign_offers, (unsigned long long)g_ab2.ign_presses, (unsigned long long)g_ab2.ign_lit,
	      (unsigned long long)g_ab2.ign_denied, (unsigned long long)g_ab2.ign_shots, (unsigned long long)g_ab2.ign_out,
	      (unsigned long long)g_ab2.throws, (unsigned long long)g_ab2.thr_moves, (unsigned long long)g_ab2.thr_started,
	      (unsigned long long)g_ab2.thr_refused, (unsigned long long)g_ab2.thr_none, (unsigned long long)g_ab2.recalls,
	      (unsigned long long)g_ab2.wh_holds, (unsigned long long)g_ab2.wh_started, (unsigned long long)g_ab2.wh_refused);
}
