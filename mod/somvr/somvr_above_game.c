/* somvr_above_game.c - build 25 (som-b25-above; docs/re-xr-pipeline.md 102, handoff/HANDOFF-stealth.md "build 25"): the
 * game's kills and brands from above by the VR player's own hands, SOMVR_ABOVE (off | on | log; code default off). NOT a
 * translation unit: somvr.c includes it last (after somvr_brand_game.c; it uses g_vb, g_cb, g_bm, gc_pick, the log).
 *
 * The game's own (re-xr-pipeline.md 39.1, `verbs tree`): standing over an orc, X ('Light') = 'PC_InstaKill_FromAbove_F'
 * / '_B' (the drop kill: Talion jumps down onto him), B ('StealthBrand') = 'PC_Brand_FromAbove_F' / '_B'; whether it is
 * possible is the game's: its selection (vb_pick, the brand's and the cue's way) is asked for the nearest orc under
 * Talion every SOMVR_VERBS_CUE_RECHECK_S - no offer, no kill.
 *   - the drop kill: while the game offers it, a faint wraith-blue glow at his collar (the grab cue's row) is the *where*;
 *     the sword drawn, a fast downward plunge of the sword hand, point first, aimed at him (somvr_fa_plunge_judge) is the
 *     *when* = X for 150 ms; no plunge = nothing (the *cancel*).
 *   - the brand from above: the open left hand out on the line to his head (somvr_fa_aimed) + a new press of the left
 *     trigger, while the game offers it = B tapped (150 ms).
 *   - the view (somvr_fa_blink): the game drops Talion 2-5 m - the view never rides it. From the press the body moments
 *     hold the view (no window); as the game's kill starts the black fades in (0.12 s; a press the game refuses shows no
 *     black at all); when Talion is on the ground the held view is put behind the
 *     orc (70 cm short of him on the line from where Talion stood, at Talion's standing eye height over his landed root;
 *     the horizon level) and the black fades out (0.2 s); held there until the game's kill ends, then the body moments'
 *     own release (the ease, the body's return). The game took nothing within 0.6 s: back in where he stands.
 *   - LOCO's hold of the game's attack moves is lifted from the press until the view is seated (the game's sync carries
 *     Talion down); the rest of the kill plays in place under the held view.
 *   - the ledge kill ('PC_InstaKill_LedgeHang', X): its offer is asked and logged while our climb is on (an orc over
 *     Talion); see docs 102 for what the game answers.
 * Test words (SOMVR_HANDS_TEST): `above status|probe|on|off|log|far CM|trace S`. */

static const char *const g_fa_w[] = { "off", "on", "log" };
enum { FA_OFF = 0, FA_ON = 1, FA_LOG = 2 };
enum { FAK_NONE = 0, FAK_DROP, FAK_BRAND, FAK_LEDGE, FAK_N };
static const char *const g_fak_w[FAK_N] = { "-", "the drop kill", "the brand from above", "the ledge kill" };
#define FA_HK 16
#define FA_SPAN_S 0.08, 0.16 /* the plunge measured over the newest 80-160 ms of the hand's way */
#define FA_SEAT_BACK_CM 70.0f /* the view put this far short of the orc on the line from where Talion stood */
#define FA_RET_CM 300.0f      /* the release after a kill from above: his body put under the held view from this far */
#define FA_RET_S 4.0          /* ... for this long after the kill's end (the body moments let go ~1 s after it) */
struct fa_offer {
	uintptr_t c, atk;      /* the orc, the attack the game's selection picked */
	double t;              /* asked then (0 = none) */
	float pos[3], head[3]; /* his root, his head point */
	char atk_name[96], node[40];
};
static struct {
	int mode, drop, brand, start_game; /* start_game: the game's attack started on the orc (FINISH's way), else X / B pressed */
	int want_start;                    /* fa_press -> fa_frame: start it now */
	somvr_fa_cfg cfg;
	float below_cm, far_cm, cue_level, fresh_s, press_s;
	/* the offers (vb_frame): [FAK_DROP], [FAK_BRAND], [FAK_LEDGE] */
	struct fa_offer off[FAK_N];
	uintptr_t said_c[FAK_N];
	int said_ok[FAK_N];
	double ask_t;
	/* the sword hand (combat_frame): its blade's base in the rig, a ring */
	struct { double t; float base[3]; } hk[FA_HK];
	int hk_n, hk_i;
	float hk_yaw;
	uint64_t hk_epoch;
	double miss_t;
	/* the press and its blink */
	int kind;
	uintptr_t c, atk_offer, atk0, atk_kill;
	char atk0_name[96], kill_name[96];
	double press_t, start_t, kill_end_t, ret_until;
	float root0[3], orc0[3], seat_off[3], y_last, seat[3];
	int have_root0, still;
	somvr_fa_blink bl;
	somvr_fa_blink_out bo;
	/* the left trigger (vb_hands) */
	int trig_was;
	/* the cue (th_frame) */
	float glow, glow_pos[3], glow_dist;
	uintptr_t glow_c;
	uint64_t glow_frame;
	double glow_t;
	/* tests */
	_Atomic int req_probe, req_mode, req_far, req_trace_ms, req_start;
	double trace_until;
	uint32_t lines, lsec_n;
	double lsec;
	uint64_t asks, offers[FAK_N], offer_frames[FAK_N], plunges, misses, presses[FAK_N], started, refused, seats, ends,
		glow_frames, holds, lifts, traces;
	float black_max;
} g_fa;

static int fa_log_ok(double t) /* the first 300 lines, then at most 6 a second */
{
	if (g_fa.lines < 300)
		return ++g_fa.lines;
	if (t >= g_fa.lsec + 1.0)
		g_fa.lsec = t, g_fa.lsec_n = 0;
	return g_fa.lsec_n++ < 6;
}

static void fa_start(int *bad)
{
	memset(&g_fa, 0, sizeof g_fa);
	atomic_store(&g_fa.req_mode, -1), atomic_store(&g_fa.req_far, -1);
	somvr_fa_cfg_default(&g_fa.cfg);
	g_fa.mode = env_word("SOMVR_ABOVE", g_fa_w, 3, FA_OFF);
	static const char *const ow[] = { "off", "on" };
	g_fa.drop = env_word("SOMVR_ABOVE_DROP", ow, 2, 1);
	g_fa.brand = env_word("SOMVR_ABOVE_BRAND", ow, 2, 0); /* off: the game never offered it on the test save (docs 102) */
	static const char *const sw[] = { "press", "game" };
	g_fa.start_game = env_word("SOMVR_ABOVE_START", sw, 2, 1);
	float pl[4] = { g_fa.cfg.plunge_cms / 100, g_fa.cfg.down_cms / 100, g_fa.cfg.point_deg, g_fa.cfg.aim_deg };
	/* (build 25: 2.0, 1.4, 40, 40 - a stab; boot 20261029-220827's lowered sword peaked at 1.57 / 1.02) */
	env_vec("SOMVR_ABOVE_PLUNGE", pl, 4, 0, 180, bad); /* m/s, m/s, deg, deg */
	g_fa.cfg.plunge_cms = pl[0] * 100, g_fa.cfg.down_cms = pl[1] * 100, g_fa.cfg.point_deg = pl[2], g_fa.cfg.aim_deg = pl[3];
	float fm[2] = { g_fa.cfg.out_s * 1e3f, g_fa.cfg.in_s * 1e3f };
	env_vec("SOMVR_ABOVE_FADE_MS", fm, 2, 0, 2000, bad);
	g_fa.cfg.out_s = fm[0] / 1e3f, g_fa.cfg.in_s = fm[1] / 1e3f;
	g_fa.cue_level = env_float("SOMVR_ABOVE_CUE_LEVEL", 0.55f, 0, 1, bad); /* the grab cue's level */
	g_fa.below_cm = env_float("SOMVR_ABOVE_BELOW_CM", 120, 50, 1000, bad);
	g_fa.far_cm = env_float("SOMVR_ABOVE_FAR_CM", 600, 100, 2000, bad);
	g_fa.fresh_s = 0.3f, g_fa.press_s = 0.15f;
	if (!g_fa.mode)
		return;
	if (g_vb.mode != VB_ON || !g_vb.ok_tree) {
		logf_("CONFIG above=%s: %s - off", g_fa_w[g_fa.mode], g_vb.mode != VB_ON ? "SOMVR_VERBS is not on"
		      : "FINISH's selection sites did not match (no game's answer)");
		g_fa.mode = FA_OFF;
		return;
	}
	char sw_why[512];
	if (g_fa.start_game && check_site(&g_fi_sites[FIS_START], sw_why, sizeof sw_why)) { /* SOMVR_FINISH's read-only site */
		logf_("CONFIG above: SOMVR_ABOVE_START=game: FI_START (FUN_00a8c700) MISMATCH (%s): the press instead", sw_why);
		g_fa.start_game = 0;
	}
	logf_("CONFIG above build 25: start=%s (SOMVR_ABOVE_START: %s)", g_fa.start_game ? "game" : "press", g_fa.start_game ?
	      "the game's own attack the selection offered started on that orc - FUN_00a8c700(pc, node, attack, the orc), as "
	      "SOMVR_FINISH starts an execution (headless: the pad's X with the offer up started nothing - the game's own pick "
	      "takes no orc below as its target, docs 102)" : "X / B pressed as the pad's");
	logf_("CONFIG above=%s (SOMVR_ABOVE): over an orc %.0f+ cm under Talion within %.0f cm, the game's selection asked every "
	      "%.2f s - the drop kill %s ('PC_InstaKill_FromAbove_B/F': the sword drawn, a plunge of the sword hand at %.1f+ m/s, "
	      "%.1f+ m/s down, point first within %.0f deg, aimed at him within %.0f deg = X for %.0f ms; SOMVR_ABOVE_DROP), the "
	      "brand from above %s ('PC_Brand_FromAbove_B/F': the open left hand out %.0f+ cm on the line to his head within %.0f "
	      "deg + the left trigger = B tapped; SOMVR_ABOVE_BRAND); the cue %.2f (SOMVR_ABOVE_CUE_LEVEL: his collar's blue glow "
	      "while the game offers it); the view: black over %.0f ms at the press, seated %.0f cm short of him on the ground, "
	      "back in over %.0f ms, held to the kill's end; the game took nothing in %.1f s: back in (SOMVR_ABOVE_FADE_MS)",
	      g_fa_w[g_fa.mode], g_fa.below_cm, g_fa.far_cm, g_gc.recheck_s, g_fa.drop ? "on" : "off", g_fa.cfg.plunge_cms / 100,
	      g_fa.cfg.down_cms / 100, g_fa.cfg.point_deg, g_fa.cfg.aim_deg, g_fa.press_s * 1e3, g_fa.brand ? "on" : "off",
	      g_fa.cfg.brand_out_cm, g_fa.cfg.brand_deg, g_fa.cue_level, g_fa.cfg.out_s * 1e3, FA_SEAT_BACK_CM,
	      g_fa.cfg.in_s * 1e3, g_fa.cfg.start_s);
}

static int fa_fresh(int kind, double t);
static void fa_press(int kind, double t);

/* Talion's Combat, his attack record and its name, his root (1 = read) */
static int fa_talion(uintptr_t *atk, char *name, size_t cap, float root[3])
{
	uintptr_t pk = vb_talion_combat(), me = pk ? peek_ptr(pk + C_HOBJ) : 0;
	*atk = pk ? peek_ptr(pk + VB_C_ATTACK) : 0;
	vb_name(*atk, name, cap);
	return me && peek(me + OBJ_POS, root, 12) && vec_ok(root, 1e7f);
}

/* the orc's head point (vb_heads), else his root + 60 cm */
static void fa_head(const struct cb_orc *o, float h[3])
{
	h[0] = o->pos[0], h[1] = o->pos[1] + 60, h[2] = o->pos[2];
	if (g_fr.frame - g_vb.heads_frame > 2)
		return;
	for (int k = 0; k < g_vb.nheads; k++)
		if (g_vb.head_c[k] == o->c)
			memcpy(h, g_vb.heads[k], 12);
}

static const char *fa_geom(const struct cb_orc *o, const float root[3], char *out, size_t cap)
{
	float q[4] = { 0, 0, 0, 1 }, yaw = 0;
	if (peek(o->hobj + OBJ_ROT, q, sizeof q) && vec_ok(q, 2.0f))
		yaw = sv_heading(sv_qnormalize(sv_quat_of(q)));
	float dx = root[0] - o->pos[0], dz = root[2] - o->pos[2];
	char an[96];
	snprintf(out, cap, "%.0f cm under Talion, %.0f cm out, Talion %.0f deg off its facing, its attack '%s', it %s him",
		 root[1] - o->pos[1], hypotf(dx, dz), fabsf(sv_wrap_pi(atan2f(dx, dz) - yaw)) * 57.29578f,
		 vb_name(peek_ptr(o->c + VB_C_ATTACK), an, sizeof an), peek_ptr(o->c + FI_C_TARGET) ? "targets" : "does not target");
	return out;
}

/* the game's pick for the first of two nodes that offers one (1 = offered; *of filled) */
static int fa_pick2(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, const char *n0, const char *n1, struct fa_offer *of,
		    const char **why, double t)
{
	char a[96], s[96];
	const char *nm[2] = { n0, n1 };
	for (int k = 0; k < 2; k++) {
		uintptr_t at = gc_pick(pcomb, pc, o, nm[k], a, sizeof a, s, sizeof s, why);
		if (at) {
			of->c = o->c, of->atk = at, of->t = t;
			memcpy(of->pos, o->pos, 12), fa_head(o, of->head);
			snprintf(of->atk_name, sizeof of->atk_name, "%s", a), snprintf(of->node, sizeof of->node, "%s", nm[k]);
			return 1;
		}
	}
	return 0;
}

static void fa_said(int kind, uintptr_t c, int ok, const struct cb_orc *o, const float root[3], const char *why, double t)
{
	if (c == g_fa.said_c[kind] && ok == g_fa.said_ok[kind])
		return;
	g_fa.said_c[kind] = c, g_fa.said_ok[kind] = ok;
	g_fa.offers[kind] += ok != 0;
	char geo[200];
	if (fa_log_ok(t))
		logf_("ABOVE t=%.3f orc %#lx (%s): %s%s%s%s%s", t, (unsigned long)c, fa_geom(o, root, geo, sizeof geo),
		      ok ? "the game offers " : "no ", g_fak_w[kind], ok ? " ('" : " (", ok ? g_fa.off[kind].atk_name : why,
		      ok ? "')" : ")");
}

/* the test word `above probe`: every node of the kind for the near orcs (the game thread) */
static void fa_probe(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const float root[3], double t)
{
	static const char *const nodes[] = { "PC_InstaKill_FromAbove_B", "PC_InstaKill_FromAbove_F", "PC_LoudKill_FromAbove_B",
					     "PC_LoudKill_FromAbove_F", "PC_Brand_FromAbove_B", "PC_Brand_FromAbove_F",
					     "PC_KD_FromAbove_B", "PC_InstaKill_LedgeHang", "PC_InstaKill_LedgeHang_Loud",
					     "PC_InstaKill_Sneak_Silent", "PC_StealthBrand" };
	uintptr_t ta;
	char tn[96], geo[200];
	float r2[3];
	fa_talion(&ta, tn, sizeof tn, r2);
	logf_("ABOVE probe t=%.3f Talion at %.0f,%.0f,%.0f attack '%s'%s%s, %d near orc(s)", t, root[0], root[1], root[2], tn,
	      g_cl.on && g_cl.s.active ? ", our climb on" : "", bd_sword_in_hand() ? ", the sword in the hand" : "", n);
	struct cb_orc all[4];
	int m = 0, have_pr = 0;
	for (int i = 0; i < n && m < 3; i++)
		all[m++] = o[i], have_pr |= o[i].c == g_pr.orc_c;
	if (g_pr.orc_c && !have_pr && cb_orc_get(g_pr.orc_c, &all[m])) { /* the practice orc, not in the targeting bands */
		all[m].dist = dist3(all[m].pos, root);
		logf_("ABOVE probe t=%.3f the practice orc %#lx at %.0f,%.0f,%.0f (%.0f cm) is NOT in the targeting bands", t,
		      (unsigned long)g_pr.orc_c, all[m].pos[0], all[m].pos[1], all[m].pos[2], all[m].dist);
		m++;
	}
	o = all, n = m;
	for (int i = 0; i < n; i++) {
		char line[1600], a[96], s[96];
		int k0 = snprintf(line, sizeof line, "ABOVE probe t=%.3f orc %#lx at %.0f,%.0f,%.0f (%s):", t, (unsigned long)o[i].c,
				  o[i].pos[0], o[i].pos[1], o[i].pos[2], fa_geom(&o[i], root, geo, sizeof geo));
		for (size_t j = 0; j < sizeof nodes / sizeof nodes[0] && k0 < (int)sizeof line - 1; j++) {
			const char *why = "";
			uintptr_t at = gc_pick(pcomb, pc, &o[i], nodes[j], a, sizeof a, s, sizeof s, &why);
			k0 += snprintf(line + k0, sizeof line - (size_t)k0, " %s -> %s%s%s;", nodes[j], at ? "'" : "none (",
				       at ? a : why, at ? "'" : ")");
		}
		logf_("%s", line);
	}
}

/* vb_frame (the game thread), after the cue: the game's answers for the nearest orc under Talion (and, climbing, over) */
static void fa_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t)
{
	if (g_fa.mode == FA_OFF)
		return;
	int rq = atomic_exchange(&g_fa.req_mode, -1);
	if (rq >= 0)
		logf_("ABOVE t=%.3f %s -> %s (trigger word)", t, g_fa_w[g_fa.mode], g_fa_w[rq]), g_fa.mode = rq;
	int fc = atomic_exchange(&g_fa.req_far, -1);
	if (fc > 0)
		g_pr.far_cm = (float)fc, logf_("ABOVE t=%.3f the next practice far orc %d cm ahead (a test)", t, fc);
	int tr = atomic_exchange(&g_fa.req_trace_ms, 0);
	if (tr > 0)
		g_fa.trace_until = t + tr / 1000.0, g_fa.traces++;
	if (!pc || !root)
		return;
	if (atomic_exchange(&g_fa.req_probe, 0))
		fa_probe(pcomb, pc, o, n, root, t);
	int rs = atomic_exchange(&g_fa.req_start, 0); /* a test aid: `above start drop|brand|ledge` - the press without the gesture */
	if (rs > 0 && rs < FAK_N) {
		if (fa_fresh(rs, t) && g_fa.bl.phase == SOMVR_FAB_IDLE)
			fa_press(rs, t);
		else
			logf_("ABOVE t=%.3f start %s (a test): %s", t, g_fak_w[rs], g_fa.bl.phase != SOMVR_FAB_IDLE ? "a blink is on"
			      : "the game does not offer it now");
	}
	if (g_fa.want_start) { /* SOMVR_ABOVE_START=game: the offered attack started on him, asked again now */
		g_fa.want_start = 0;
		struct cb_orc oc;
		const char *why = "the orc is gone";
		uintptr_t node = 0, n2 = 0, sy = 0, atk = 0;
		uint8_t pad = 0;
		if (cb_orc_get(g_fa.c, &oc) && (node = vb_node_named(pcomb, g_fa.off[g_fa.kind].node)) &&
		    (atk = vb_pick(pcomb, pc, &oc, node, &sy, &why))) {
			peek(pcomb + FI_PC_PAD, &pad, 1);
			n2 = FI_FN(FIS_KBMNODE, fi_kbm_fn)(node, pad != 0);
		}
		char an[96];
		if (atk && n2) {
			FI_FN(FIS_START, fi_start_fn)(pcomb, n2, atk, oc.hobj);
			g_fa.atk_offer = atk;
		}
		if (atk && n2)
			logf_("ABOVE t=%.3f start: FUN_00a8c700(pc=%#lx, node '%s', attack '%s', the orc's hobj %#lx) %.2f s after the press", t,
			      (unsigned long)pcomb, g_fa.off[g_fa.kind].node, vb_name(atk, an, sizeof an), (unsigned long)oc.hobj,
			      t - g_fa.press_t);
		else
			logf_("ABOVE t=%.3f start: NOT started - the game offers it no more (%s): nothing started (no black)", t, why);
	}
	if (t - g_fa.ask_t < g_gc.recheck_s || g_fa.bl.phase != SOMVR_FAB_IDLE)
		return;
	g_fa.ask_t = t;
	const struct cb_orc *below = NULL, *over = NULL;
	for (int i = 0; i < n; i++) {
		float h = hypotf(o[i].pos[0] - root[0], o[i].pos[2] - root[2]), dy = root[1] - o[i].pos[1];
		if (!below && dy >= g_fa.below_cm && h <= g_fa.far_cm)
			below = &o[i];
		if (!over && -dy >= 60 && h <= 300)
			over = &o[i];
	}
	const char *why = "";
	if (below) {
		g_fa.asks++;
		if (g_fa.drop) {
			int ok = fa_pick2(pcomb, pc, below, "PC_InstaKill_FromAbove_B", "PC_InstaKill_FromAbove_F", &g_fa.off[FAK_DROP],
					  &why, t);
			fa_said(FAK_DROP, below->c, ok, below, root, why, t);
		}
		if (g_fa.brand) {
			int ok = fa_pick2(pcomb, pc, below, "PC_Brand_FromAbove_B", "PC_Brand_FromAbove_F", &g_fa.off[FAK_BRAND], &why,
					  t);
			fa_said(FAK_BRAND, below->c, ok, below, root, why, t);
		}
	} else {
		g_fa.said_c[FAK_DROP] = g_fa.said_c[FAK_BRAND] = 0;
	}
	if (over && g_cl.on && g_cl.s.active) { /* the ledge kill: logged (docs 102) */
		int ok = fa_pick2(pcomb, pc, over, "PC_InstaKill_LedgeHang", "PC_InstaKill_LedgeHang_Loud", &g_fa.off[FAK_LEDGE],
				  &why, t);
		fa_said(FAK_LEDGE, over->c, ok, over, root, why, t);
	}
}

static int fa_fresh(int kind, double t) { return g_fa.off[kind].c && t - g_fa.off[kind].t <= g_fa.fresh_s; }

/* the press: X (the drop kill) or B (the brand) for 150 ms; the blink starts */
static void fa_press(int kind, double t)
{
	struct fa_offer *of = &g_fa.off[kind];
	char tn[96];
	g_fa.kind = kind, g_fa.c = of->c, g_fa.atk_offer = of->atk, g_fa.press_t = t, g_fa.start_t = 0, g_fa.atk_kill = 0;
	g_fa.kill_name[0] = 0, g_fa.kill_end_t = 0, g_fa.still = 0;
	memcpy(g_fa.orc0, of->pos, 12);
	g_fa.have_root0 = fa_talion(&g_fa.atk0, tn, sizeof tn, g_fa.root0);
	snprintf(g_fa.atk0_name, sizeof g_fa.atk0_name, "%s", tn);
	g_fa.y_last = g_fa.root0[1];
	g_fa.seat_off[0] = g_fa.seat_off[2] = 0, g_fa.seat_off[1] = 60;
	if (g_bm.have_live && g_fa.have_root0) /* the view's base over his root now (standing): kept for the seat */
		for (int k = 0; k < 3; k++)
			g_fa.seat_off[k] = g_bm.live[k] - g_fa.root0[k];
	if (hypotf(g_fa.seat_off[0], g_fa.seat_off[2]) > 60)
		g_fa.seat_off[0] = g_fa.seat_off[2] = 0;
	g_fa.presses[kind]++;
	br_nl_open(t, 3.0); /* SOMVR_VERBS_BRAND_NODELOG (a test switch): the game's own node lines around the press */
	if (g_fa.mode == FA_ON) {
		if (g_fa.start_game)
			g_fa.want_start = 1; /* fa_frame, the next frame: the game's attack on this orc */
		else
			g_vb.pcmd[0] = kind == FAK_BRAND ? VB_CMD_B : VB_CMD_X, g_vb.pcmd[1] = 0, g_vb.press_until = t + g_fa.press_s;
		somvr_fa_blink_press(&g_fa.bl, t, kind != FAK_LEDGE);
		if (g_vb.haptic > 0)
			in_haptic_event(kind == FAK_BRAND ? SOMVR_HAND_L : SOMVR_HAND_R, 0.7f * g_vb.haptic, 0.08f, SOMVR_HAP_COMBAT);
	}
	logf_("ABOVE t=%.3f press: %s on orc %#lx (the game offers '%s' by '%s', asked %.2f s ago; Talion at %.0f,%.0f,%.0f "
	      "attack '%s'; the view's base %+.0f,%+.0f,%+.0f over his root): %s", t, g_fak_w[kind], (unsigned long)of->c,
	      of->atk_name, of->node, t - of->t, g_fa.root0[0], g_fa.root0[1], g_fa.root0[2], g_fa.atk0_name, g_fa.seat_off[0],
	      g_fa.seat_off[1], g_fa.seat_off[2], g_fa.mode != FA_ON ? "logged only (SOMVR_ABOVE=log)" : g_fa.start_game ?
	      "the game's attack started on him, the blink" : kind == FAK_BRAND ? "B (120) tapped for 150 ms, the blink"
	      : "X (118) for 150 ms, the blink");
}

/* combat_frame (the game thread), every frame with a blade: the sword hand's plunge at the orc the game would drop-kill */
static void fa_blade(const float *root, double t)
{
	if (g_fa.mode == FA_OFF)
		return;
	const sc_rigxf *rx = &g_cb.hand_rx;
	int ok = root && g_cb.hand.valid && isfinite(rx->yaw + rx->base[0] + rx->base[1] + rx->base[2]);
	if (!ok || fabsf(rx->yaw - g_fa.hk_yaw) > 1e-4f || g_rig_epoch != g_fa.hk_epoch)
		g_fa.hk_n = 0; /* a turn or a jump of the rig: no motion across it */
	g_fa.hk_yaw = rx->yaw, g_fa.hk_epoch = g_rig_epoch;
	if (!ok)
		return;
	g_fa.hk[g_fa.hk_i].t = t;
	for (int j = 0; j < 3; j++)
		g_fa.hk[g_fa.hk_i].base[j] = g_cb.hand.base[j] - rx->base[j];
	int now = g_fa.hk_i;
	g_fa.hk_i = (g_fa.hk_i + 1) % FA_HK;
	if (g_fa.hk_n < FA_HK)
		g_fa.hk_n++;
	if (!g_fa.drop || g_fa.bl.phase != SOMVR_FAB_IDLE || t - g_fa.press_t < 1.0 || !fa_fresh(FAK_DROP, t) ||
	    !bd_sword_in_hand())
		return;
	/* the hand's mean velocity over its newest 80-160 ms (the blade's base in the rig; not one frame's: 102.3) */
	double ts[FA_HK];
	float ps[FA_HK][3], v[3];
	int m = 0;
	for (int k = g_fa.hk_n - 1; k >= 0; k--) { /* oldest first */
		int i = (now - k + FA_HK) % FA_HK;
		ts[m] = g_fa.hk[i].t, memcpy(ps[m], g_fa.hk[i].base, 12), m++;
	}
	if (!somvr_fa_mean_vel(ts, (const float(*)[3])ps, m, FA_SPAN_S, v))
		return;
	if (v[1] > -g_fa.cfg.down_cms * 0.6f)
		return; /* not going down: nothing to judge */
	const struct fa_offer *of = &g_fa.off[FAK_DROP];
	float ax[3], to[3];
	for (int j = 0; j < 3; j++)
		ax[j] = g_cb.hand.tip[j] - g_cb.hand.base[j], to[j] = of->pos[j] + (j == 1 ? 30.0f : 0.0f) - g_cb.hand.tip[j];
	somvr_fa_plunge p;
	if (somvr_fa_plunge_judge(&g_fa.cfg, v, ax, to, &p)) {
		g_fa.plunges++;
		logf_("ABOVE t=%.3f plunge: the sword hand at %.2f m/s (%.2f down), the point leading by %.0f deg, aimed %.0f deg off "
		      "his chest (%.0f cm off)", t, p.speed / 100, p.down / 100, p.point_deg, p.aim_deg,
		      sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]));
		fa_press(FAK_DROP, t);
	} else if (t - g_fa.miss_t > 0.5) {
		g_fa.misses++, g_fa.miss_t = t;
		if (fa_log_ok(t))
			logf_("ABOVE t=%.3f a downward move, not the plunge (%s): %.2f m/s, %.2f down, the point %.0f deg off the hand's "
			      "way, the blade %.0f deg off his chest", t, p.why, p.speed / 100, p.down / 100, p.point_deg, p.aim_deg);
	}
}

/* vb_hands (the game thread, a stereo frame), after the cue: the open left hand aimed down at him + the trigger's press */
static void fa_hands(const somvr_vb_hand_in *in, double t)
{
	if (g_fa.mode == FA_OFF)
		return;
	int tr = g_fa.trig_was ? in->trigger > g_vb.cfg.trig_off : in->trigger >= g_vb.cfg.trig_on, edge = tr && !g_fa.trig_was;
	g_fa.trig_was = tr;
	if (!edge || !g_fa.brand || !in->live || !in->have_palm || !in->have_eye || g_vb.hand.phase != SOMVR_VBH_IDLE ||
	    g_fa.bl.phase != SOMVR_FAB_IDLE || t - g_fa.press_t < 1.0 || !fa_fresh(FAK_BRAND, t))
		return;
	if (in->squeeze > 0.5f) { /* a fist is not the brand's hand */
		if (fa_log_ok(t))
			logf_("ABOVE t=%.3f the left trigger while the brand from above is offered: the hand closed (%.2f) - nothing", t,
			      in->squeeze);
		return;
	}
	float deg = 0;
	if (!somvr_fa_aimed(&g_fa.cfg, in->eye, in->palm, g_fa.off[FAK_BRAND].head, &deg)) {
		if (fa_log_ok(t))
			logf_("ABOVE t=%.3f the left trigger while the brand from above is offered: the hand not aimed at his head (%.0f "
			      "deg off, the palm %.0f cm from the eyes) - nothing", t, deg, dist3(in->eye, in->palm));
		return;
	}
	logf_("ABOVE t=%.3f the open left hand at his head (%.0f deg off the line, the palm %.0f cm out) and the trigger", t, deg,
	      dist3(in->eye, in->palm));
	fa_press(FAK_BRAND, t);
}

/* bm_pre (H4, every frame), after the return: the blink's step; while it runs the body moments hold (no window) */
static int fa_bm(int act, int *hc, int *w, double t)
{
	if (g_fa.ret_until > 0 && t > g_fa.ret_until) /* the release's reach back to SOMVR_BM_RETURN_CM */
		g_bm.ret_cm = g_bm.ret_cfg, g_fa.ret_until = 0;
	if (g_fa.mode == FA_OFF || g_fa.bl.phase == SOMVR_FAB_IDLE)
		return act;
	uintptr_t at;
	char an[96];
	float r[3];
	int have = fa_talion(&at, an, sizeof an, r);
	int kill = at && at != g_fa.atk0 && (at == g_fa.atk_offer || strstr(an, "Above") || strstr(an, "Ledge") ||
					    (g_fa.kind == FAK_BRAND && strstr(an, "Brand")));
	if (kill && !g_fa.start_t) {
		g_fa.start_t = t, g_fa.atk_kill = at, g_fa.started++;
		snprintf(g_fa.kill_name, sizeof g_fa.kill_name, "%s", an);
		logf_("ABOVE t=%.3f the game's %s started %.2f s after the press: Talion's attack '%s' (at %.0f,%.0f,%.0f)", t,
		      g_fak_w[g_fa.kind], t - g_fa.press_t, an, r[0], r[1], r[2]);
	}
	int on = g_fa.start_t > 0 && at && (kill || strstr(an, "Bip") || strstr(an, "Sync")); /* the kill's records go on */
	if (g_fa.start_t > 0 && !on && !g_fa.kill_end_t)
		g_fa.kill_end_t = t;
	if (on)
		g_fa.kill_end_t = 0;
	int over = g_fa.kill_end_t > 0 && t - g_fa.kill_end_t >= 0.2;
	int landed = 0;
	if (have && g_fa.start_t > 0) {
		g_fa.still = fabsf(r[1] - g_fa.y_last) < 3.0f ? g_fa.still + 1 : 0;
		landed = r[1] <= g_fa.root0[1] - 80.0f && g_fa.still >= 2;
		g_fa.y_last = r[1];
	}
	int ph0 = g_fa.bl.phase;
	somvr_fa_blink_step(&g_fa.bl, &g_fa.cfg, t, g_fa.start_t > 0, landed, over, &g_fa.bo);
	const somvr_fa_blink_out *o = &g_fa.bo;
	if (o->fade >= 1)
		g_fa.black_max = fmaxf(g_fa.black_max, (float)(t - g_fa.press_t));
	if (o->seat) { /* under the full black: the held view behind the orc, at his standing eyes over the landed root */
		struct cb_orc oc;
		float op[3] = { g_fa.orc0[0], g_fa.orc0[1], g_fa.orc0[2] };
		int live = cb_orc_get(g_fa.c, &oc);
		if (live)
			memcpy(op, oc.pos, 12);
		float dx = op[0] - g_fa.root0[0], dz = op[2] - g_fa.root0[2], l = hypotf(dx, dz);
		if (l > 1)
			dx /= l, dz /= l;
		else
			dx = 0, dz = 0;
		float y = (landed && have ? r[1] : op[1]) + g_fa.seat_off[1];
		g_fa.seat[0] = op[0] - dx * FA_SEAT_BACK_CM, g_fa.seat[1] = y, g_fa.seat[2] = op[2] - dz * FA_SEAT_BACK_CM;
		int put = g_bm.on && g_bm.s.frozen;
		if (put)
			memcpy(g_bm.s.F, g_fa.seat, 12), g_bm.s.cut_pending = 0, g_fa.seats++;
		logf_("ABOVE t=%.3f the view %s at %.0f,%.0f,%.0f (%.0f cm short of the orc %s at %.0f,%.0f,%.0f; Talion's root "
		      "%.0f,%.0f,%.0f, %s, %.0f cm under where he stood) %.2f s after the press", t, put ? "seated" :
		      "NOT seated (no held view)", g_fa.seat[0], g_fa.seat[1], g_fa.seat[2], FA_SEAT_BACK_CM, live ? "now" : "at the press",
		      op[0], op[1], op[2], r[0], r[1], r[2], landed ? "landed" : g_fa.bl.over ? "the kill over" : "the black's limit",
		      g_fa.root0[1] - r[1], t - g_fa.press_t);
	}
	if (o->changed && fa_log_ok(t))
		logf_("ABOVE t=%.3f blink %s -> %s (fade %.2f; Talion's attack '%s'%s%s)", t, somvr_fa_blink_names[ph0],
		      somvr_fa_blink_names[g_fa.bl.phase], o->fade, an, g_fa.bl.refused ? "; the game took nothing: back in where he "
		      "stands" : "", o->done && !g_fa.bl.refused ? "; the body moments' own release from here" : "");
	if (o->done)
		g_fa.ends++, g_fa.refused += g_fa.bl.refused != 0;
	/* the kill's end: the game's sync carried him up to ~2 m off the seated view (boots 185325, 192129: 199.5 and 185.9
	 * cm - the body moments' re-seat blink); his body is put under the held view instead (SOMVR_BM_RETURN_TP's Teleport,
	 * its checks: his floor, the way clear) from up to FA_RET_CM - the view stays where the player was put */
	if (o->done && g_fa.kind != FAK_LEDGE && g_fa.bl.started && !g_fa.bl.refused && g_bm.ret_cfg > 0 && g_bm.ret_tp) {
		g_bm.ret_cm = fmaxf(g_bm.ret_cfg, FA_RET_CM), g_fa.ret_until = t + FA_RET_S;
		logf_("ABOVE t=%.3f the kill's end: his body is put under the held view when the hold lets go (up to %.0f cm for %.1f "
		      "s; SOMVR_BM_RETURN_TP)", t, FA_RET_CM, FA_RET_S);
	}
	if (t < g_fa.trace_until && fa_log_ok(t))
		logf_("ABOVE trace t=%.3f %s fade %.2f hold %d root %.1f,%.1f,%.1f attack '%s' frozen %d F %.0f,%.0f,%.0f act %d w %d",
		      t, somvr_fa_blink_names[g_fa.bl.phase], o->fade, o->hold, r[0], r[1], r[2], an, g_bm.s.frozen, g_bm.s.F[0],
		      g_bm.s.F[1], g_bm.s.F[2], act, *w);
	if (o->hold) {
		g_fa.holds++;
		if (act < SOMVR_BMA_HOLD)
			act = SOMVR_BMA_HOLD, *hc = SOMVR_BMC_HOLD;
		*w = 0;
	}
	return act;
}

/* H4's stereo path (the record open): the blink's black (the larger level wins) */
static void fa_fade(void)
{
	if (g_fa.mode != FA_ON || g_fa.bl.phase == SOMVR_FAB_IDLE || !g_fr.rec || !(g_fa.bo.fade > 0))
		return;
	uint32_t lv = (uint32_t)(g_fa.bo.fade * 255 + 0.5f), was = somvr_rec_fade(g_fr.rec->flags);
	if (lv > was)
		g_fr.rec->flags = (g_fr.rec->flags & ~SOMVR_REC_FADE_MASK) | lv << SOMVR_REC_FADE_SHIFT;
}

/* LOCO (somvr.c) and COMBAT_HOLD_TALION: the game's drop carries Talion down - their hold of the game's moves lifted from
 * the press until the view is seated on the ground (the kill's sync moves him on by its own motion after it: 186-200 cm
 * in boots 20261029-185325 and -192129; fa_bm puts his body back under the view at the end) */
static int fa_lift(double t)
{
	(void)t;
	int on = g_fa.mode == FA_ON && g_fa.bl.phase != SOMVR_FAB_IDLE && !g_fa.bl.refused && !g_fa.bl.seated;
	g_fa.lifts += on != 0;
	return on;
}

/* th_frame: the cue - his collar's blue glow while the game offers the drop kill (or the brand from above) */
static void fa_glow_add(struct th_glow *gl, int *ng)
{
	if (g_fa.mode != FA_ON || !(g_fa.cue_level > 0))
		return;
	double t = cb_now();
	int k = fa_fresh(FAK_DROP, t) ? FAK_DROP : fa_fresh(FAK_BRAND, t) ? FAK_BRAND : FAK_NONE;
	float want = k && g_fa.bl.phase == SOMVR_FAB_IDLE ? g_fa.cue_level : 0;
	float dt = g_fa.glow_t > 0 && t > g_fa.glow_t ? (float)(t - g_fa.glow_t) : 0;
	g_fa.glow_t = t;
	if (k) {
		const struct fa_offer *of = &g_fa.off[k];
		if (of->c != g_fa.glow_c)
			g_fa.glow = 0, g_fa.glow_c = of->c;
		g_fa.glow_pos[0] = of->head[0], g_fa.glow_pos[1] = of->head[1] - 12, g_fa.glow_pos[2] = of->head[2];
		g_fa.glow_dist = dist3(g_fa.glow_pos, g_fr.head);
	}
	float a = dt / 0.15f; /* 0.15 s in and out */
	g_fa.glow += (want - g_fa.glow) * (a < 1 ? a : 1);
	if (!(g_fa.glow > 0.01f) || !vec_ok(g_fa.glow_pos, 1e7f))
		return;
	th_glow_add(gl, ng, g_fa.glow_pos, g_fa.glow, g_fa.glow_dist, SOMVR_THK_MELEE | SOMVR_THK_BLUE);
	g_fa.glow_frames++;
}

/* the trigger word "above ..." (the writer thread) */
static const char *fa_word(const char *w)
{
	if (!strcmp(w, "on") || !strcmp(w, "off") || !strcmp(w, "log")) {
		if (g_fa.mode == FA_OFF)
			return "above (SOMVR_ABOVE is off at start: ignored)";
		atomic_store(&g_fa.req_mode, w[1] == 'n' ? FA_ON : w[1] == 'f' ? FA_OFF : FA_LOG);
		return "above on|off|log";
	}
	if (!g_vb.test)
		return "above (SOMVR_HANDS_TEST is off: ignored)";
	if (!strcmp(w, "probe")) {
		atomic_store(&g_fa.req_probe, 1);
		return "above probe (a test: the game's from-above and ledge nodes against the near orcs)";
	}
	if (!strncmp(w, "far ", 4)) {
		long cm = strtol(w + 4, NULL, 10);
		if (cm < 100 || cm > 4000)
			return "above far CM (100..4000: ignored)";
		atomic_store(&g_fa.req_far, (int)cm);
		return "above far (a test: the next practice far orc that far ahead)";
	}
	if (!strncmp(w, "start ", 6)) {
		const char *k = w + 6;
		int kind = !strcmp(k, "drop") ? FAK_DROP : !strcmp(k, "brand") ? FAK_BRAND : !strcmp(k, "ledge") ? FAK_LEDGE : 0;
		if (!kind)
			return "above start drop|brand|ledge (ignored)";
		atomic_store(&g_fa.req_start, kind);
		return "above start (a test: the press without the gesture, where the game offers it)";
	}
	if (!strncmp(w, "trace ", 6)) {
		double s = strtod(w + 6, NULL);
		if (!(s > 0 && s <= 30))
			return "above trace S (0..30: ignored)";
		atomic_store(&g_fa.req_trace_ms, (int)(s * 1000));
		return "above trace (a test: the blink's frames)";
	}
	if (!strcmp(w, "status")) {
		logf_("ABOVE status: mode %s, drop %d brand %d; asks %llu, offers drop %llu brand %llu ledge %llu; plunges %llu, other "
		      "downward moves %llu; presses drop %llu brand %llu; started %llu, refused %llu, seats %llu, ends %llu; cue frames "
		      "%llu; the longest black %.2f s", g_fa_w[g_fa.mode], g_fa.drop, g_fa.brand, (unsigned long long)g_fa.asks,
		      (unsigned long long)g_fa.offers[FAK_DROP], (unsigned long long)g_fa.offers[FAK_BRAND],
		      (unsigned long long)g_fa.offers[FAK_LEDGE], (unsigned long long)g_fa.plunges, (unsigned long long)g_fa.misses,
		      (unsigned long long)g_fa.presses[FAK_DROP], (unsigned long long)g_fa.presses[FAK_BRAND],
		      (unsigned long long)g_fa.started, (unsigned long long)g_fa.refused, (unsigned long long)g_fa.seats,
		      (unsigned long long)g_fa.ends, (unsigned long long)g_fa.glow_frames, g_fa.black_max);
		return "above status";
	}
	return "above status|probe|on|off|log|far CM|trace S|start drop|brand|ledge";
}

static void fa_end_log(void)
{
	if (g_fa.mode == FA_OFF && !g_fa.asks)
		return;
	logf_("ABOVE END: mode %s; asks %llu; the game's offers: the drop kill %llu, the brand from above %llu, the ledge kill %llu "
	      "(new answers); plunges %llu, other downward moves %llu; presses: drop %llu, brand %llu; the game's kill started "
	      "%llu, refused %llu; the view seated %llu, blinks ended %llu, the longest black %.2f s; the hold %llu frames, LOCO "
	      "lifted %llu ticks; the cue %llu frames", g_fa_w[g_fa.mode], (unsigned long long)g_fa.asks,
	      (unsigned long long)g_fa.offers[FAK_DROP], (unsigned long long)g_fa.offers[FAK_BRAND],
	      (unsigned long long)g_fa.offers[FAK_LEDGE], (unsigned long long)g_fa.plunges, (unsigned long long)g_fa.misses,
	      (unsigned long long)g_fa.presses[FAK_DROP], (unsigned long long)g_fa.presses[FAK_BRAND],
	      (unsigned long long)g_fa.started, (unsigned long long)g_fa.refused, (unsigned long long)g_fa.seats,
	      (unsigned long long)g_fa.ends, g_fa.black_max, (unsigned long long)g_fa.holds, (unsigned long long)g_fa.lifts,
	      (unsigned long long)g_fa.glow_frames);
}
