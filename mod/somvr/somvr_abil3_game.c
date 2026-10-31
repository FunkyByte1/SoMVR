/* somvr_abil3_game.c - build 27d (som-b27-abilities3; docs/re-xr-pipeline.md 121, handoff/HANDOFF-combat.md "build
 * 27d"): the abilities 118.2 decided and left unbuilt - brutalize, taunt, Wraith Distract - each on the game's own offer
 * and attack. Under SOMVR_ABIL (not off), each its own switch (code default off): SOMVR_ABIL_BRUTAL, SOMVR_ABIL_TAUNT,
 * SOMVR_ABIL_DISTRACT. NOT a translation unit: somvr.c includes it after somvr_abil2_game.c (it uses g_ab2's pcomb/pc,
 * ab2_start_on, ab2_skill_owned, gc_pick, abl_watch_begin, the stealth kill's thrust, the bow's state).
 *   - brutalize ('PC_InstaKill_Sneak_Loud', the flat game's heavy attack behind an unaware orc - its prompt
 *     'Combat_Sneak_Instakill_Loud'; NOT 'PC_StealthTorment', the stealth brand's paired drain, 95 -> 45, no kill): the
 *     stealth kill's thrust (SOMVR_STEALTH_KILL's judge: behind an unaware orc, point first, hard) with the LEFT trigger
 *     held and the left hand free (not on the hilt, not holding an orc) = the game's own torment started on him
 *     (FUN_00a8c700, as the flash) where the game offers it - its paired animation and its terror (the orcs who see it
 *     flee); the view held by the body moments (its sync), Talion's body put back under it after. Not offered: the
 *     stealth kill as ever, a soft buzz in the left hand.
 *   - taunt ('PC_Taunt'; the flat game's 123 with 124 held): a free open hand (either; with the bow out, the string
 *     hand) held out at an orc, palm up, beckoned - pulled in toward the chest twice within 1.5 s (somvr_ab_beckon_step)
 *     = the game's taunt started on the orc nearest the eyes -> palm line it offers it for. Not offered: a soft buzz.
 *   - Wraith Distract ('PC_Wraith_Ranged_Normal_Distract'; the flat game's Y while aiming): X (the bow hand's lower
 *     button) while an arrow is drawn kindles the distract arrow where the game offers it (its selection for the orc on
 *     the ray, else with no target, else its record by name with PSS_Wraith_Distract owned) - a pulsing wraith glow at
 *     the tip; the release shoots our arrow with that attack (bo_shoot, as the ignite). X again or a let-down puts it
 *     out; the ignite and the distract put each other out. While distract is on and owned, X while drawn is the
 *     distract's (the old X Shadow Strike of build 12 keeps X with the bow aimed, not drawn).
 * Words: `abil3 status` (the offers: `abil2 probe` asks PC_Taunt, PC_StealthTorment and the distract's node too). */

#define AB3_CMD_Y 119 /* CombatPrimary02: the pad's Y (the flat game's distraction while aiming, the loud sneak kill) */
static struct {
	int on, brutal, taunt, distract;
	somvr_ab3_cfg cfg;
	char br_node[64], ta_node[64], di_node[64], di_name[96], di_skill[48];
	float own_s, watch_s;
	/* brutalize */
	uintptr_t br_c;
	double br_t;
	/* taunt (the stereo frame's left hand, the game thread's start) */
	somvr_ab_beckon bk[2];
	int ta_req, ta_hand;
	float ta_eye[3], ta_palm[3];
	double ta_t, ta_said_t;
	const char *ta_why;
	/* distract */
	int di_off, lit, x_was, owned, di_press;
	double di_t;
	uintptr_t di_atk;
	char di_atk_name[96], di_how[96];
	double ask_t, lit_t, glow_t;
	float glow;
	_Atomic int req_palm, req_aimnear;
	char req_name[96];
	_Atomic int req_name_set;
	uint32_t lines, lsec_n;
	double lsec;
	uint64_t br_asks, br_started, br_refused, br_held, ta_beckons, ta_started, ta_refused, di_asks, di_offers, di_presses,
		di_lit, di_denied, di_shots, di_out;
} g_ab3;

static int ab3_log_ok(double t)
{
	if (g_ab3.lines < 300)
		return ++g_ab3.lines;
	if (t >= g_ab3.lsec + 1.0)
		g_ab3.lsec = t, g_ab3.lsec_n = 0;
	return g_ab3.lsec_n++ < 6;
}

static void ab3_start(int *bad)
{
	memset(&g_ab3, 0, sizeof g_ab3);
	somvr_ab3_cfg_default(&g_ab3.cfg);
	static const char *const ow[] = { "off", "on" };
	g_ab3.brutal = env_word("SOMVR_ABIL_BRUTAL", ow, 2, 0);
	g_ab3.taunt = env_word("SOMVR_ABIL_TAUNT", ow, 2, 0);
	g_ab3.distract = env_word("SOMVR_ABIL_DISTRACT", ow, 2, 0);
	g_ab3.di_press = !env_word("SOMVR_ABIL_DISTRACT_START", (const char *const[]){ "press", "record" }, 2, 0);
	const char *e;
	snprintf(g_ab3.br_node, sizeof g_ab3.br_node, "%s", (e = getenv("SOMVR_ABIL_BRUTAL_NODE")) && *e ? e :
		 "PC_InstaKill_Sneak_Loud"); /* the loud sneak kill (its prompt 'Combat_Sneak_Instakill_Loud'), not the torment:
					      * 'PC_StealthTorment' is the stealth brand's sync, no kill (boot 20261030-234411) */
	snprintf(g_ab3.ta_node, sizeof g_ab3.ta_node, "%s", (e = getenv("SOMVR_ABIL_TAUNT_NODE")) && *e ? e : "PC_Taunt");
	snprintf(g_ab3.di_node, sizeof g_ab3.di_node, "%s", (e = getenv("SOMVR_ABIL_DISTRACT_NODE")) && *e ? e :
		 "PC_Wraith_Ranged_Normal_Distract");
	snprintf(g_ab3.di_name, sizeof g_ab3.di_name, "%s", (e = getenv("SOMVR_ABIL_DISTRACT_ATTACK")) ? e : "");
	snprintf(g_ab3.di_skill, sizeof g_ab3.di_skill, "%s", (e = getenv("SOMVR_ABIL_DISTRACT_SKILL")) && *e ? e :
		 "PSS_Wraith_Distract");
	float bk[4] = { g_ab3.cfg.out_cm, g_ab3.cfg.pull_cm, g_ab3.cfg.pull_cms, g_ab3.cfg.win_s * 1000 };
	env_vec("SOMVR_ABIL_TAUNT_BECKON", bk, 4, 0, 5000, bad); /* cm out, cm a pull, cm/s, ms for both pulls */
	g_ab3.cfg.out_cm = bk[0], g_ab3.cfg.pull_cm = bk[1], g_ab3.cfg.pull_cms = bk[2], g_ab3.cfg.win_s = bk[3] / 1000;
	g_ab3.cfg.aim_deg = env_float("SOMVR_ABIL_TAUNT_AIM_DEG", g_ab3.cfg.aim_deg, 5, 90, bad);
	g_ab3.own_s = env_float("SOMVR_ABIL_BRUTAL_OWN_MS", 6000, 500, 20000, bad) / 1000; /* no hit on him meanwhile */
	g_ab3.watch_s = 8;
	g_ab3.on = g_abl.mode != AB_OFF && (g_ab3.brutal || g_ab3.taunt || g_ab3.distract);
	if (!g_ab3.brutal && !g_ab3.taunt && !g_ab3.distract)
		return;
	if (g_abl.mode == AB_OFF) {
		logf_("CONFIG abil3: SOMVR_ABIL_BRUTAL/_TAUNT/_DISTRACT need SOMVR_ABIL (off, or its selection did not match) - off");
		return;
	}
	if ((g_ab3.brutal || g_ab3.taunt) && !g_abl.start_game)
		logf_("CONFIG abil3: no start site (FI_START): brutalize and the taunt are logged only");
	logf_("CONFIG abil3 build 27d: brutalize %s (SOMVR_ABIL_BRUTAL; node '%s'): the stealth kill's thrust with the left trigger "
	      "held and the left hand free = the game's torment where it offers it; the orc ours %.1f s after "
	      "(SOMVR_ABIL_BRUTAL_OWN_MS)", g_ab3.brutal ? "on" : "off", g_ab3.br_node, g_ab3.own_s);
	logf_("CONFIG abil3: the taunt %s (SOMVR_ABIL_TAUNT; node '%s'): the open left hand out (%.0f+ cm), palm up, pulled in "
	      "%.0f+ cm at %.0f+ cm/s twice within %.2f s (SOMVR_ABIL_TAUNT_BECKON) at an orc within %.0f deg of the eyes -> palm "
	      "line (SOMVR_ABIL_TAUNT_AIM_DEG)", g_ab3.taunt ? "on" : "off", g_ab3.ta_node, g_ab3.cfg.out_cm, g_ab3.cfg.pull_cm,
	      g_ab3.cfg.pull_cms, g_ab3.cfg.win_s, g_ab3.cfg.aim_deg);
	logf_("CONFIG abil3: Wraith Distract %s (SOMVR_ABIL_DISTRACT, '%s' owned): X while an arrow is drawn - %s",
	      g_ab3.distract ? "on" : "off", g_ab3.di_skill, g_ab3.di_press ?
	      "at the game's prompt 'UI_Wraith_Distraction', its own input (119) pressed: the game's distraction (SOMVR_ABIL_DISTRACT_START=press)" :
	      "kindles the node's record, the release shoots it (=record: the record on our arrow; no orc reacted, boots 20261030-203644 .. "
	      "20261031-004828)");
}

/* a game prompt up now for action act whose StringId holds str (the verbs' table of shown prompts) */
static int ab3_prompt_up(int act, const char *str)
{
	for (int i = 0; i < g_vb.nup; i++)
		if (g_vb.up[i].act == act && strstr(g_vb.up[i].str, str))
			return 1;
	return 0;
}

/* the left trigger (the test hands' `hands trigger L V` over the controller's) */
static float ab3_ltrig(void)
{
	const somvr_hand_in *hi = &g_inp.st.hand[SOMVR_HAND_L];
	float v = (hi->active & SOMVR_HACT_INPUT) ? hi->trigger : 0;
	if (g_bd.test && g_bf.t_trig[SOMVR_HAND_L] >= 0)
		v = g_bf.t_trig[SOMVR_HAND_L];
	return v;
}

/* ---- brutalize ---------------------------------------------------------------------------------------------------- */
/* stl_strike, first: the tormented orc takes no hit of ours while the torment runs (1 = taken) */
static int ab3_brutal_owns(uintptr_t c, double t)
{
	return g_ab3.br_c && c == g_ab3.br_c && t - g_ab3.br_t < g_ab3.own_s;
}

/* stl_strike, at a stealth kill's verdict (before its hit): the left trigger held = the game's torment where it offers
 * it; 1 = started (no stealth kill dealt) */
static int ab3_brutal(uintptr_t pc, const struct cb_orc *o, int hand, int sword, int fk, double t)
{
	if (!g_ab3.on || !g_ab3.brutal)
		return 0;
	float lt = ab3_ltrig();
	if (!(lt >= 0.6f))
		return 0;
	g_ab3.br_asks++;
	uintptr_t pcomb = g_ab2.pcomb;
	const char *why = !pcomb ? "no player combat yet" : hand != SOMVR_HAND_R || !sword ? "not the sword's thrust"
			  : g_bd.two.w > 0 ? "two hands on the sword (the left trigger is the hilt's)"
			  : vb_grab_held() ? "the left hand holds an orc"
			  : fk <= 0 ? "a captain the stealth kill does not kill" : NULL;
	char an[96] = "", sn[96] = "";
	const char *w2 = "";
	uintptr_t a = why ? 0 : gc_pick(pcomb, pc, o, g_ab3.br_node, an, sizeof an, sn, sizeof sn, &w2);
	if (!a && !why) { /* the window's answer alone? (asked again without it, for the line) */
		const char *w3 = "";
		char a3[96], s3[96];
		g_vb_pick_nowin = 1;
		uintptr_t a2 = gc_pick(pcomb, pc, o, g_ab3.br_node, a3, sizeof a3, s3, sizeof s3, &w3);
		g_vb_pick_nowin = 0;
		why = w2;
		if (a2)
			logf_("ABIL3 t=%.3f brutalize: '%s' passes with the attack window ignored (the window: %s)", t, a3, w2);
	}
	if (!a) {
		g_ab3.br_refused++;
		in_haptic_event(SOMVR_HAND_L, 0.2f, 0.08f, SOMVR_HAP_COMBAT);
		logf_("ABIL3 t=%.3f brutalize: the thrust with the left trigger held (%.2f) at orc %#lx (%.0f cm) - not offered (%s): "
		      "the stealth kill", t, lt, (unsigned long)o->c, o->dist, why);
		return 0;
	}
	if (pc != g_ab2.pc && ab3_log_ok(t))
		logf_("ABIL3 t=%.3f brutalize: the stealth kill's combat %#lx, the abilities' %#lx", t, (unsigned long)pc,
		      (unsigned long)g_ab2.pc);
	logf_("ABIL3 t=%.3f brutalize: the thrust with the left trigger held (%.2f) at orc %#lx (%.0f cm): the game offers '%s' "
	      "(sync '%s')", t, lt, (unsigned long)o->c, o->dist, an, sn[0] ? sn : "-");
	if (!ab2_start_on(pcomb, pc, g_ab3.br_node, o, "brutalize", t)) {
		g_ab3.br_refused++;
		return 0;
	}
	g_ab3.br_c = o->c, g_ab3.br_t = t, g_ab3.br_started++;
	/* the watch: him and every orc within 30 m (the terror: who flees) */
	struct cb_orc nr[AB_WATCH_N];
	uintptr_t me = peek_ptr(pc + C_HOBJ);
	float root[3];
	int n = 0;
	nr[n++] = *o;
	if (me && peek(me + OBJ_POS, root, 12)) {
		struct cb_orc near[AB_WATCH_N];
		int k = cb_orcs_r(pcomb, pc, root, 3000, near, AB_WATCH_N);
		for (int i = 0; i < k && n < AB_WATCH_N; i++)
			if (near[i].c != o->c)
				nr[n++] = near[i];
	}
	abl_watch_begin(pc, nr, n, t, g_ab3.watch_s);
	in_haptic_event(SOMVR_HAND_R, 1.0f, 0.12f, SOMVR_HAP_COMBAT), in_haptic_event(SOMVR_HAND_L, 0.6f, 0.1f, SOMVR_HAP_COMBAT);
	logf_("ABIL3 t=%.3f brutalize started on orc %#lx; the watch %d orc(s) for %.0f s", t, (unsigned long)o->c, n,
	      g_ab3.watch_s);
	return 1;
}

/* ---- the taunt ---------------------------------------------------------------------------------------------------- */
static void ab3_taunt_go(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	struct cb_orc nr[8];
	int n = cb_orcs_r(pcomb, pc, root, g_ab3.cfg.aim_max_cm, nr, 8), best = -1;
	float bd = g_ab3.cfg.aim_deg, line[3] = { g_ab3.ta_palm[0] - g_ab3.ta_eye[0], 0, g_ab3.ta_palm[2] - g_ab3.ta_eye[2] };
	float ly = atan2f(line[0], line[2]);
	for (int i = 0; i < n; i++) {
		float d = remainderf(atan2f(nr[i].pos[0] - g_ab3.ta_eye[0], nr[i].pos[2] - g_ab3.ta_eye[2]) - ly, 6.2831853f) * 57.29578f;
		if (ab3_log_ok(t))
			logf_("ABIL3 t=%.3f the taunt's line: orc %#lx at %.0f cm, %+.1f deg off it", t, (unsigned long)nr[i].c, nr[i].dist,
			      d);
		if (fabsf(d) <= bd)
			bd = fabsf(d), best = i;
	}
	if (best < 0) {
		g_ab3.ta_refused++;
		in_haptic_event(g_ab3.ta_hand, 0.2f, 0.08f, SOMVR_HAP_COMBAT);
		logf_("ABIL3 t=%.3f the taunt: no orc within %.0f deg of the hand (%d near): nothing", t, g_ab3.cfg.aim_deg, n);
		return;
	}
	const char *why = "";
	char an[96], sn[96];
	if (!gc_pick(pcomb, pc, &nr[best], g_ab3.ta_node, an, sizeof an, sn, sizeof sn, &why)) {
		g_ab3.ta_refused++;
		in_haptic_event(g_ab3.ta_hand, 0.2f, 0.08f, SOMVR_HAP_COMBAT);
		char st[96];
		logf_("ABIL3 t=%.3f the taunt at orc %#lx (%.0f cm, %.1f deg off the hand, state '%s'): not offered (%s): nothing", t,
		      (unsigned long)nr[best].c, nr[best].dist, bd, vb_name(peek_ptr(nr[best].c + VB_C_STATE), st, sizeof st), why);
		return;
	}
	if (ab2_start_on(pcomb, pc, g_ab3.ta_node, &nr[best], "the taunt", t)) {
		g_ab3.ta_started++;
		in_haptic_event(g_ab3.ta_hand, 0.5f, 0.06f, SOMVR_HAP_COMBAT);
	} else
		g_ab3.ta_refused++;
}

/* ---- Wraith Distract: the offer while drawn ------------------------------------------------------------------------ */
static void ab3_distract_ask(uintptr_t pcomb, uintptr_t pc, double t)
{
	const somvr_bow *s = &g_bow.s;
	int was = g_ab3.di_off;
	char was_how[96];
	snprintf(was_how, sizeof was_how, "%s", g_ab3.di_how);
	g_ab3.di_off = 0, g_ab3.di_atk = 0, g_ab3.di_atk_name[0] = 0;
	snprintf(g_ab3.di_how, sizeof g_ab3.di_how, "-");
	g_ab3.owned = ab2_skill_owned(pc, g_ab3.di_skill) == 1;
	if (!s->have_ray)
		return;
	g_ab3.di_asks++;
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
	if (best >= 0 && (at = gc_pick(pcomb, pc, &nr[best], g_ab3.di_node, an, sizeof an, (char[96]){ 0 }, 96, &why)))
		snprintf(g_ab3.di_how, sizeof g_ab3.di_how, "the orc on the ray (%.1f deg, %.0f cm)", bd, nr[best].dist);
	if (!at) {
		float p[3] = { o[0] + d[0] * 2000, o[1] + d[1] * 2000, o[2] + d[2] * 2000 };
		if ((at = ab2_pick_at(pcomb, pc, 0, p, g_ab3.di_node, an, sizeof an, &why)))
			snprintf(g_ab3.di_how, sizeof g_ab3.di_how, "no target");
	}
	if (!at && g_ab3.di_name[0] && g_ab3.owned && (at = CB_FN(CS_ATK_BYNAME, cb_byname_fn)(g_ab3.di_name))) {
		snprintf(an, sizeof an, "%s", g_ab3.di_name);
		snprintf(g_ab3.di_how, sizeof g_ab3.di_how, "by name, '%s' owned", g_ab3.di_skill);
	}
	if (at) {
		g_ab3.di_off = 1, g_ab3.di_atk = at;
		snprintf(g_ab3.di_atk_name, sizeof g_ab3.di_atk_name, "%s", an);
	}
	if (g_ab3.di_off != was || (g_ab3.di_off && strncmp(was_how, g_ab3.di_how, 10))) {
		g_ab3.di_offers += g_ab3.di_off;
		if (ab3_log_ok(t))
			logf_("ABIL3 t=%.3f the distract: %s%s%s%s (%s)", t, g_ab3.di_off ? "the game offers '" : "not offered",
			      g_ab3.di_off ? g_ab3.di_atk_name : "", g_ab3.di_off ? "' - " : "", g_ab3.di_off ? g_ab3.di_how : "",
			      g_ab3.di_off ? "the arrow drawn" : why[0] ? why : g_ab3.owned ? "no orc on the ray, none with no target" :
			      "not owned");
	}
}

/* sk_tick (before X's own): X while an arrow is drawn is the distract's (the skill owned, the switch on) */
static int ab3_x_taken(void)
{
	return g_ab3.on && g_ab3.distract && g_bow.on && g_bow.s.phase == SOMVR_BOW_PH_DRAW && g_ab3.owned;
}

/* abl_frame's (the game thread, via ab2_frame's place): the taunt's start, the distract's offer */
static void ab3_frame(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	if (!g_ab3.on || !pcomb || !pc)
		return;
	if (!root)
		root = g_ab2.eye;
	if (atomic_exchange(&g_ab3.req_name_set, 0)) {
		snprintf(g_ab3.di_name, sizeof g_ab3.di_name, "%s", g_ab3.req_name);
		logf_("ABIL3 t=%.3f the distract's record by name now '%s' (a test word)", t, g_ab3.di_name);
	}
	if (atomic_exchange(&g_ab3.req_aimnear, 0)) { /* a test aid: the next shots' ray onto the ground beside the nearest orc */
		struct cb_orc nr[1];
		if (cb_orcs_r(pcomb, pc, root, 4000, nr, 1) == 1) {
			float dx = root[0] - nr[0].pos[0], dz = root[2] - nr[0].pos[2], l = hypotf(dx, dz);
			dx = l > 1 ? dx / l : 0, dz = l > 1 ? dz / l : 1;
			g_ab2.aim_pt[0] = nr[0].pos[0] + 80 * dx, g_ab2.aim_pt[1] = nr[0].pos[1] - 95,
			g_ab2.aim_pt[2] = nr[0].pos[2] + 80 * dz, g_ab2.aim_on = 1; /* (2.1 m off: no reaction, boots 203644, 234411) */
			logf_("ABIL3 t=%.3f aimnear: the next shots' ray onto the ground at %.0f,%.0f,%.0f - 0.8 m from orc %#lx (%.0f cm "
			      "away), toward Talion (a test aid; `abil2 aim off`)", t, g_ab2.aim_pt[0], g_ab2.aim_pt[1],
			      g_ab2.aim_pt[2], (unsigned long)nr[0].c, nr[0].dist);
		} else
			logf_("ABIL3 t=%.3f aimnear: no orc within 40 m", t);
	}
	if (g_ab3.ta_req) {
		g_ab3.ta_req = 0;
		ab3_taunt_go(pcomb, pc, root, t);
	}
	if (g_ab3.distract && g_bow.on && (g_bow.s.phase == SOMVR_BOW_PH_DRAW || g_ab3.lit) && t - g_ab3.ask_t >= g_gc.recheck_s) {
		g_ab3.ask_t = t;
		ab3_distract_ask(pcomb, pc, t);
	} else if (g_bow.s.phase != SOMVR_BOW_PH_DRAW && !g_ab3.lit) {
		g_ab3.di_off = 0;
		if (g_ab3.distract && t - g_ab3.ask_t >= 1.0) /* the skill's bit for X (asked now and then) */
			g_ab3.ask_t = t, g_ab3.owned = ab2_skill_owned(pc, g_ab3.di_skill) == 1;
	}
}

/* the stereo frame's hands (after ab2_hands): the beckon, X while drawn */
static void ab3_hands(const sv_vec3 pw[2], const sv_quat qw[2], sv_vec3 hp)
{
	if (!g_ab3.on)
		return;
	double t = cb_now();
	/* ---- the taunt: a free open hand out, palm up, beckoned (either hand: with the bow out, the string hand) */
	if (g_ab3.taunt || atomic_load(&g_ab3.req_palm)) {
		int palm_req = atomic_exchange(&g_ab3.req_palm, 0);
		for (int h = 0; h < 2; h++) {
			const somvr_hand_in *hi = &g_inp.st.hand[h];
			float sq = bd_squeeze(h, (hi->active & SOMVR_HACT_INPUT) ? hi->squeeze : 0);
			/* the palm's normal: OpenXR's grip +X points out of the left palm and into the right one */
			sv_vec3 n = sv_qrot(sv_qnormalize(qw[h]), (sv_vec3){ h == SOMVR_HAND_L ? 1.0f : -1.0f, 0, 0 });
			int dag = g_sl.on && g_sl.have_g && sl_dagger_drawn(&g_sl.g) && somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_DAGGER) == h;
			int sword = h == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD) && bd_sword_in_hand();
			int bow = bo_out() && (h == g_bow.cfg.bow_hand || g_bow.s.phase == SOMVR_BOW_PH_DRAW);
			int busy = cl_holds(h) || (h == SOMVR_HAND_L && (vb_grab_held() || g_bd.two.w > 0)) || dag || sword || bow;
			int ok = g_inp.hands[h].valid && sq < g_ab3.cfg.open_max && n.y >= g_ab3.cfg.palm_up && !busy &&
				 g_inp.ctx == SC_CTX_WORLD && !vm_open();
			float d = hypotf(pw[h].x - hp.x, pw[h].z - hp.z);
			if (palm_req)
				logf_("ABIL3 t=%.3f palm (a test): the %s hand %s, %.0f cm out (level), its palm's normal (%.2f %.2f %.2f), "
				      "grip %.2f: %s%s%s%s%s%s%s%s%s", t, h ? "right" : "left", g_inp.hands[h].valid ? "valid" : "NOT valid", d,
				      n.x, n.y, n.z, sq, ok ? "open, palm up and free" : "not:", bow ? " the bow's" : "",
				      cl_holds(h) ? " a climb hold" : "", h == SOMVR_HAND_L && vb_grab_held() ? " an orc held" : "",
				      dag ? " the dagger in it" : "", sword ? " the sword in it" : "",
				      h == SOMVR_HAND_L && g_bd.two.w > 0 ? " on the hilt" : "", n.y < g_ab3.cfg.palm_up ? " palm not up" : "",
				      g_inp.ctx != SC_CTX_WORLD ? " not the world" : vm_open() ? " the wrist menu" : "");
			if (!g_ab3.taunt)
				continue;
			const char *why = "";
			somvr_ab_beckon *bk = &g_ab3.bk[h];
			int pulls0 = bk->pulls;
			if (somvr_ab_beckon_step(&g_ab3.cfg, bk, t, d, ok, &why) && t - g_ab3.ta_t > 2.0) {
				g_ab3.ta_t = t, g_ab3.ta_req = 1, g_ab3.ta_beckons++;
				g_ab3.ta_eye[0] = hp.x, g_ab3.ta_eye[1] = hp.y, g_ab3.ta_eye[2] = hp.z;
				g_ab3.ta_palm[0] = pw[h].x, g_ab3.ta_palm[1] = pw[h].y, g_ab3.ta_palm[2] = pw[h].z, g_ab3.ta_hand = h;
				logf_("ABIL3 t=%.3f taunt: the beckon - the open %s hand (grip %.2f, palm up %.2f) %.0f cm out, pulled in "
				      "twice (the last %.1f cm at %.0f cm/s)%s", t, h ? "right" : "left", sq, n.y, d, bk->last_pull_cm,
				      bk->last_pull_cms, bo_out() ? "; the bow out" : "");
			} else if (bk->pulls > pulls0 && ab3_log_ok(t))
				logf_("ABIL3 t=%.3f taunt: a first pull of the %s hand (%.1f cm at %.0f cm/s, %.0f cm out, palm up %.2f)", t,
				      h ? "right" : "left", bk->last_pull_cm, bk->last_pull_cms, d, n.y);
			else if (why[0] && strcmp(why, "a pull") && t - g_ab3.ta_said_t > 0.5 && ab3_log_ok(t))
				g_ab3.ta_said_t = t, logf_("ABIL3 t=%.3f taunt: the %s hand: %s (%.0f cm out, palm up %.2f, grip %.2f)", t,
							   h ? "right" : "left", why, d, n.y, sq);
		}
	}
	/* ---- Wraith Distract: X on the bow hand while an arrow is drawn */
	if (g_ab3.distract && g_bow.on) {
		int bh = g_bow.cfg.bow_hand, ph = g_bow.s.phase;
		int x = (g_inp.ctl.down[bh] & SOMVR_BTN_LOWER) != 0, edge = x && !g_ab3.x_was;
		g_ab3.x_was = x;
		if (g_ab3.lit && (ph == SOMVR_BOW_PH_IDLE || ph == SOMVR_BOW_PH_AIM)) {
			g_ab3.lit = 0, g_ab3.di_out++;
			if (ab3_log_ok(t))
				logf_("ABIL3 t=%.3f the distract put out: the arrow %s", t, ph == SOMVR_BOW_PH_AIM ? "let down" : "away");
		}
		if (g_ab3.lit && g_ab2.lit) /* the ignite kindled after it: that one holds */
			g_ab3.lit = 0, g_ab3.di_out++, logf_("ABIL3 t=%.3f the distract put out: the ignite kindled", t);
		if (edge && ph == SOMVR_BOW_PH_DRAW && g_ab3.owned && g_ab3.di_press) {
			/* the game's own: its prompt 'UI_Wraith_Distraction' (action 119, the flat game's Y while aiming) is up
			 * the whole time the bow is out where it offers the distraction (boot 20261031-004828: 13.2 s) */
			g_ab3.di_presses++;
			if (ab3_prompt_up(AB3_CMD_Y, "Distraction")) {
				g_vb.pcmd[0] = AB3_CMD_Y, g_vb.pcmd[1] = 0, g_vb.press_until = t + 0.15;
				g_ab3.di_shots++, g_ab3.di_t = t;
				in_haptic_event(bh, 0.45f, 0.05f, SOMVR_HAP_COMBAT), in_haptic_event(1 - bh, 0.3f, 0.04f, SOMVR_HAP_COMBAT);
				if (g_ab2.lit) /* (the ignite kindled before: the game's own shot goes, ours stays kindled) */
					logf_("ABIL3 t=%.3f the distract: the ignite stays kindled on the drawn arrow", t);
				logf_("ABIL3 t=%.3f the distract: X while drawn (%.0f cm) at the game's prompt 'UI_Wraith_Distraction' - %d "
				      "pressed for 150 ms (the game's own distraction along the bow's ray)", t, g_bow.s.draw_cm, AB3_CMD_Y);
				if (g_ab2.pc && g_ab2.pcomb) {
					struct cb_orc nr[AB_WATCH_N];
					int n = cb_orcs_r(g_ab2.pcomb, g_ab2.pc, g_ab2.eye, 3000, nr, AB_WATCH_N);
					abl_watch_begin(g_ab2.pc, nr, n, t, 10.0);
				}
			} else {
				g_ab3.di_denied++;
				in_haptic_event(bh, 0.2f, 0.08f, SOMVR_HAP_COMBAT);
				logf_("ABIL3 t=%.3f X while drawn: no 'UI_Wraith_Distraction' prompt (the game does not offer it now) - nothing", t);
			}
		} else if (edge && ph == SOMVR_BOW_PH_DRAW && g_ab3.owned) {
			g_ab3.di_presses++;
			if (g_ab3.lit) {
				g_ab3.lit = 0, g_ab3.di_out++;
				logf_("ABIL3 t=%.3f the distract put out (X again)", t);
			} else if (g_ab3.di_off && g_ab3.di_atk) {
				g_ab3.lit = 1, g_ab3.lit_t = t, g_ab3.di_lit++;
				if (g_ab2.lit)
					g_ab2.lit = 0, g_ab2.ign_out++, logf_("ABIL2 t=%.3f the ignite put out: the distract kindled", t);
				in_haptic_event(bh, 0.4f, 0.05f, SOMVR_HAP_COMBAT), in_haptic_event(1 - bh, 0.25f, 0.03f, SOMVR_HAP_COMBAT);
				logf_("ABIL3 t=%.3f the distract kindled: X while drawn (%.0f cm), the game offers '%s' (%s)", t,
				      g_bow.s.draw_cm, g_ab3.di_atk_name, g_ab3.di_how);
			} else {
				g_ab3.di_denied++;
				in_haptic_event(bh, 0.2f, 0.08f, SOMVR_HAP_COMBAT);
				logf_("ABIL3 t=%.3f X while drawn: the game does not offer the distract now - nothing kindled", t);
			}
		}
	}
}

/* bo_shoot: the shot's attack - the distract's record when kindled (after the ignite's: one of the two is lit) */
static uintptr_t ab3_shot_attack(uintptr_t atk, double t)
{
	if (!g_ab3.lit)
		return atk;
	g_ab3.lit = 0;
	if (!g_ab3.di_atk || g_abl.mode != AB_ON) {
		logf_("ABIL3 t=%.3f the shot: the distract kindled, but %s - the normal arrow", t, g_abl.mode != AB_ON ? "logged only" :
		      "no distract record");
		return atk;
	}
	g_ab3.di_shots++;
	char an[96], ad[160];
	logf_("ABIL3 t=%.3f the shot: the distract - attack '%s' (%s) instead of '%s'; %s", t, g_ab3.di_atk_name, g_ab3.di_how,
	      vb_name(atk, an, sizeof an), cb_ad_desc(g_ab3.di_atk, ad, sizeof ad));
	/* the watch: the orcs within 30 m of the shot's end (who comes to look) */
	if (g_ab2.pc && g_ab2.pcomb) {
		struct cb_orc nr[AB_WATCH_N];
		int n = cb_orcs_r(g_ab2.pcomb, g_ab2.pc, g_ab2.eye, 3000, nr, AB_WATCH_N);
		abl_watch_begin(g_ab2.pc, nr, n, t, 8.0);
	}
	return g_ab3.di_atk;
}

/* th_frame: the distract arrow's tip - a pulsing wraith glow (the ignite's is steady) */
static void ab3_glow_add(struct th_glow *gl, int *ng)
{
	if (!g_ab3.on || g_abl.mode != AB_ON || !(g_ab2.cue_level > 0))
		return;
	double t = cb_now();
	float dt = g_ab3.glow_t > 0 && t > g_ab3.glow_t ? (float)(t - g_ab3.glow_t) : 0, a = dt / 0.12f;
	g_ab3.glow_t = t;
	a = a < 1 ? a : 1;
	float pulse = 0.55f + 0.45f * sinf((float)(t - g_ab3.lit_t) * 2 * 3.14159265f * 2.5f);
	g_ab3.glow += ((g_ab3.lit ? g_ab2.cue_level * 1.4f * pulse : 0) - g_ab3.glow) * a;
	if (g_ab3.glow > 0.01f && vec_ok(g_ab2.tip, 1e7f))
		th_glow_add(gl, ng, g_ab2.tip, g_ab3.glow > 1 ? 1 : g_ab3.glow, dist3(g_ab2.tip, g_fr.head),
			    SOMVR_THK_PROJ | SOMVR_THK_BLUE);
}

/* "abil3 ..." (the writer thread) */
static const char *ab3_word(const char *w)
{
	if (!strcmp(w, "status")) {
		logf_("ABIL3 status: brutalize %s taunt %s distract %s (owned %d); brutalize asks %llu started %llu refused %llu; "
		      "beckons %llu taunts started %llu refused %llu; distract asks %llu offers %llu presses %llu lit %llu denied %llu "
		      "shots %llu out %llu", g_ab3.brutal ? "on" : "off", g_ab3.taunt ? "on" : "off", g_ab3.distract ? "on" : "off",
		      g_ab3.owned, (unsigned long long)g_ab3.br_asks, (unsigned long long)g_ab3.br_started,
		      (unsigned long long)g_ab3.br_refused, (unsigned long long)g_ab3.ta_beckons, (unsigned long long)g_ab3.ta_started,
		      (unsigned long long)g_ab3.ta_refused, (unsigned long long)g_ab3.di_asks, (unsigned long long)g_ab3.di_offers,
		      (unsigned long long)g_ab3.di_presses, (unsigned long long)g_ab3.di_lit, (unsigned long long)g_ab3.di_denied,
		      (unsigned long long)g_ab3.di_shots, (unsigned long long)g_ab3.di_out);
		return "abil3 status";
	}
	if (!g_abl.test)
		return "abil3 (SOMVR_HANDS_TEST is off: ignored)";
	if (!strcmp(w, "palm")) {
		atomic_store(&g_ab3.req_palm, 1);
		return "abil3 palm (a test: the left palm's normal, the hand's distance)";
	}
	if (!strcmp(w, "aimnear")) {
		atomic_store(&g_ab3.req_aimnear, 1);
		return "abil3 aimnear (a test aid: the next shots' ray onto the ground beside the nearest orc)";
	}
	if (!strncmp(w, "distract ", 9) && !atomic_load(&g_ab3.req_name_set)) {
		snprintf(g_ab3.req_name, sizeof g_ab3.req_name, "%s", w + 9);
		atomic_store(&g_ab3.req_name_set, 1);
		return "abil3 distract NAME (a test aid: the distract's record by name)";
	}
	return "abil3 status|palm|aimnear|distract NAME";
}

static void ab3_end_log(void)
{
	if (!g_ab3.on)
		return;
	logf_("ABIL3 END build 27d: brutalize asks %llu, started %llu, refused %llu; beckons %llu, taunts started %llu, refused "
	      "%llu; distract offers %llu, presses %llu, lit %llu, denied %llu, shots %llu, put out %llu",
	      (unsigned long long)g_ab3.br_asks, (unsigned long long)g_ab3.br_started, (unsigned long long)g_ab3.br_refused,
	      (unsigned long long)g_ab3.ta_beckons, (unsigned long long)g_ab3.ta_started, (unsigned long long)g_ab3.ta_refused,
	      (unsigned long long)g_ab3.di_offers, (unsigned long long)g_ab3.di_presses, (unsigned long long)g_ab3.di_lit,
	      (unsigned long long)g_ab3.di_denied, (unsigned long long)g_ab3.di_shots, (unsigned long long)g_ab3.di_out);
}
