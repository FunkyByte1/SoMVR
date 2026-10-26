/* somvr_fingers_game.c - Talion's fingers on the controllers, the game side (included by somvr.c after
 * somvr_body_game.c; pure parts in somvr_fingers.c; docs/re-xr-pipeline.md 29, handoff/HANDOFF-hands.md round 3):
 *   SOMVR_FINGERS=1      in phase 2 (arm_eval, after the IK, before the arms are written back) each solved arm's finger
 *                        joints get their local rotations from the open hand / the fist per finger: the grip closes the
 *                        middle, ring and pinky, the trigger the index (touched: a little), the thumb lies down on a
 *                        button touch; the sword in the right hand (SOMVR_SLOTS' state), the dagger in the left, the hilt
 *                        held with both hands (SOMVR_TWOHAND) = a closed fist; the bow hand while the bow is out keeps
 *                        the game's fingers. Needs SOMVR_ARMS=1. SOMVR_FINGERS_MS=30: each finger's ease (tau; 95 % in
 *                        three of them).
 *   words (with SOMVR_HANDS_TEST=1): `hands trigger L|R V` holds a trigger value (0..1; -1 = the controller's again),
 *                        `hands thumb L|R V` a thumb touch (0/1; -1 = the controller's); with a test value the hand counts
 *                        as touch-sensing. Always: `hands fingers` logs both hands' finger joints once (the game's local
 *                        rotations and offsets before our curl, then the bind pose's from the skin palette:
 *                        bind = skin^-1 x model, SOMVR_BODY=hands' phase 7), `hands fingers on|off` switches them live.
 * Lines: `CONFIG fingers ...`, `FINGERS hands ...`, `FINGERS t=... last 10 s: ...`, `FINGERS hold ...`, `FINGERS dump ...`,
 * `FINGERS END ...`.
 */

struct bf_cnt {
	uint64_t evals, applied[2], skipped[2], game[2], fist[2];
};
static struct {
	int want, on;
	fg_cfg cfg;
	/* the hands for the arms' skeleton */
	uintptr_t skel;
	uint32_t nodes;
	int ok, logged;
	fg_hand f[2];
	fg_state st[2];
	fg_in in[2];
	float w[2];
	int hold[2];
	/* SOMVR_HANDS_TEST */
	float t_trig[2], t_thumb[2];
	/* `hands fingers`: the dump */
	int dump_anim, dump_bind;
	/* logs */
	double next_log;
	struct bf_cnt n, wn;
} g_bf = { .t_trig = { -1, -1 }, .t_thumb = { -1, -1 } };
#define BF_COUNT(f) (g_bf.n.f++, g_bf.wn.f++)
/* the writer thread's words -> the game thread */
static _Atomic uint32_t g_bf_req_seq, g_bf_req_dump, g_bf_req_onoff; /* onoff: 0 none, 1 off, 2 on */
static struct { int seq; float trig[2], thumb[2]; } g_bf_req = { 0, { -1, -1 }, { -1, -1 } };
static const char *const g_bf_hold_w[] = { "the controller", "a fist (a held weapon)", "the game's (the bow)" };

static int bf_ready(void)
{
	if (!g_arm.chains_ok || !g_arm.nodes)
		return 0;
	if (g_bf.skel == g_arm.skel && g_bf.nodes == g_arm.nodes)
		return g_bf.ok;
	static uint8_t parent[SA_MAXN];
	static uint32_t hash[SA_MAXN];
	uint32_t n = 0;
	char why[2][240] = { "", "" };
	const char *bad = skel_read(g_arm.skel, g_arm.nodes, parent, hash, &n);
	g_bf.skel = g_arm.skel, g_bf.nodes = g_arm.nodes;
	g_bf.ok = !bad;
	for (int h = 0; g_bf.ok && h < 2; h++)
		g_bf.ok = fg_hand_build(&g_bf.f[h], h, g_arm.ch[h].w, parent, hash, (int)n, why[h], sizeof why[h]) == 0;
	memset(g_bf.st, 0, sizeof g_bf.st);
	if (g_bf.logged < 8) {
		g_bf.logged++;
		logf_("FINGERS hands skel=%#lx nodes=%u: %s; %s; %s%s", (unsigned long)g_arm.skel, g_arm.nodes, bad ? bad : "read",
		      why[0], why[1], g_bf.ok ? "" : " (the fingers stay the game's for this skeleton)");
	}
	return g_bf.ok;
}

/* what hand h holds (the game thread, phase 2) */
static int bf_hold(int h)
{
	if (h == g_bow.cfg.bow_hand && bo_out())
		return FG_HOLD_GAME;
	if (h == SA_R && bd_sword_in_hand())
		return FG_HOLD_FIST;
	if (h == SA_L && (g_bd.two.w > 0 || (g_sl.on && g_sl.have_g && sl_dagger_drawn(&g_sl.g))))
		return FG_HOLD_FIST;
	return FG_HOLD_NONE;
}

static void bf_input(int h, fg_in *in)
{
	memset(in, 0, sizeof *in);
	const somvr_hand_in *hi = &g_inp.st.hand[h];
	int live = g_inp.on && g_inp.live && (hi->active & SOMVR_HACT_INPUT);
	int prof = g_inp.st.profile[h];
	in->squeeze = bd_squeeze(h, live ? hi->squeeze : 0);
	in->trigger = live ? hi->trigger : 0;
	in->trigger_touch = live && (hi->buttons & SOMVR_BTN_TRIGGER_TOUCH);
	in->thumb_touch = live && (hi->buttons & SOMVR_BTN_THUMB_TOUCH);
	in->touch_ok = live && (prof == SOMVR_PROF_TOUCH || prof == SOMVR_PROF_INDEX);
	if (g_bd.test && g_bf.t_trig[h] >= 0)
		in->trigger = g_bf.t_trig[h], in->trigger_touch = g_bf.t_trig[h] > 0;
	if (g_bd.test && g_bf.t_thumb[h] >= 0)
		in->thumb_touch = g_bf.t_thumb[h] > 0.5f, in->touch_ok = 1;
	in->hold = bf_hold(h);
}

static void bf_dump_anim(void)
{
	for (int h = 0; h < 2; h++)
		for (int a = 0; a < FG_NF; a++)
			for (int k = 0; k < FG_NJ; k++) {
				sa_xf l;
				if (!fg_local(g_arm.pose, &g_bf.f[h], a, k, &l))
					continue;
				logf_("FINGERS dump anim %s %s%d node %d: q %.6f,%.6f,%.6f,%.6f t %.3f,%.3f,%.3f (in its parent; hold %d)",
				      g_arm_side[h], fg_finger_name(a), k + 1, g_bf.f[h].j[a][k], l.q.x, l.q.y, l.q.z, l.q.w, l.p.x,
				      l.p.y, l.p.z, g_bf.hold[h]);
			}
}

/* phase 2 (arm_eval): after both arms are solved, before they are written back */
static void bf_eval(const int done[2], float dt)
{
	if (!g_bf.on)
		return;
	BF_COUNT(evals);
	if (!bf_ready())
		return;
	if (g_bf.dump_anim) {
		g_bf.dump_anim = 0;
		bf_dump_anim();
	}
	for (int h = 0; h < 2; h++) {
		fg_in *in = &g_bf.in[h];
		bf_input(h, in);
		if (in->hold != g_bf.hold[h]) {
			logf_("FINGERS hold %s t=%.3f: %s -> %s", g_arm_side[h], cb_now(), g_bf_hold_w[g_bf.hold[h]],
			      g_bf_hold_w[in->hold]);
			g_bf.hold[h] = in->hold;
		}
		float w = fg_step(&g_bf.st[h], &g_bf.cfg, in, dt);
		g_bf.w[h] = w;
		if (in->hold == FG_HOLD_GAME)
			BF_COUNT(game[h]);
		else if (in->hold == FG_HOLD_FIST)
			BF_COUNT(fist[h]);
		float wa = g_arm.weight[h] * w;
		if (!done[h] || !(wa > 0)) {
			BF_COUNT(skipped[h]);
			continue;
		}
		if (fg_apply(g_arm.pose, (int)g_arm.nodes, &g_bf.f[h], &fg_pose_open, &fg_pose_fist, g_bf.st[h].c, wa))
			BF_COUNT(applied[h]);
		else
			BF_COUNT(skipped[h]);
	}
}

/* phase 7 (bd_copy, the player's skin palette and model pose read): the bind pose's finger joints for `hands fingers` */
static void bf_bind(const sa_node *skin, const sa_node *model, uint32_t n)
{
	if (!g_bf.dump_bind || !g_bf.ok || n != g_bf.nodes)
		return;
	g_bf.dump_bind = 0;
	for (int h = 0; h < 2; h++) {
		const fg_hand *f = &g_bf.f[h];
		for (int a = -1; a < FG_NF; a++)
			for (int k = 0; k < (a < 0 ? 1 : FG_NJ); k++) {
				int j = a < 0 ? f->hand : f->j[a][k], p = a < 0 ? g_arm.ch[h].e : k ? f->j[a][k - 1] : f->hand;
				sa_xf bj = sa_xf_mul(sa_xf_inv(sa_node_xf(&skin[j])), sa_node_xf(&model[j]));
				sa_xf bp = sa_xf_mul(sa_xf_inv(sa_node_xf(&skin[p])), sa_node_xf(&model[p]));
				sa_xf l = sa_xf_mul(sa_xf_inv(bp), bj);
				logf_("FINGERS dump bind %s %s%d node %d: model p %.3f,%.3f,%.3f q %.6f,%.6f,%.6f,%.6f; in its parent "
				      "q %.6f,%.6f,%.6f,%.6f t %.3f,%.3f,%.3f", g_arm_side[h], a < 0 ? "hand" : fg_finger_name(a), k + 1, j,
				      bj.p.x, bj.p.y, bj.p.z, bj.q.x, bj.q.y, bj.q.z, bj.q.w, l.q.x, l.q.y, l.q.z, l.q.w, l.p.x, l.p.y,
				      l.p.z);
			}
	}
}

/* the writer thread: "hands trigger|thumb|fingers ..." (NULL = not ours) */
static const char *bf_word(const char *a, const char *rest)
{
	if (!strcmp(a, "fingers")) {
		char m[8] = "";
		if (sscanf(rest, "%7s", m) == 1 && (!strcmp(m, "on") || !strcmp(m, "off"))) {
			atomic_store(&g_bf_req_onoff, m[1] == 'n' ? 2u : 1u);
			atomic_fetch_add(&g_bf_req_seq, 1);
			return "hands fingers on|off";
		}
		atomic_store(&g_bf_req_dump, 1);
		atomic_fetch_add(&g_bf_req_seq, 1);
		return "hands fingers (a dump)";
	}
	if (strcmp(a, "trigger") && strcmp(a, "thumb"))
		return NULL;
	if (!g_bd.test)
		return "hands (SOMVR_HANDS_TEST is off: ignored)";
	char side = 0;
	float v = 0;
	if (sscanf(rest, " %c %f", &side, &v) != 2 || (side != 'L' && side != 'R'))
		return "hands trigger|thumb (not L|R V: ignored)";
	int s = side == 'R';
	v = v < 0 ? -1 : v > 1 ? 1 : v;
	pthread_mutex_lock(&g_bd_mu);
	if (a[1] == 'r')
		g_bf_req.trig[s] = v;
	else
		g_bf_req.thumb[s] = v;
	g_bf_req.seq++;
	pthread_mutex_unlock(&g_bd_mu);
	atomic_fetch_add(&g_bf_req_seq, 1);
	return a[1] == 'r' ? "hands trigger" : "hands thumb";
}

/* H4 (bd_frame): the words, the 10 s line */
static void bf_frame(void)
{
	static uint32_t seen;
	static int seen_t;
	uint32_t sq = atomic_load(&g_bf_req_seq);
	if (sq != seen) {
		seen = sq;
		if (atomic_exchange(&g_bf_req_dump, 0)) {
			g_bf.dump_anim = g_bf.dump_bind = 1;
			logf_("FINGERS t=%.3f dump asked (trigger word): the next phase 2 (the game's fingers) and phase 7 (the bind "
			      "pose)%s", mono(), g_bf.on ? "" : "; SOMVR_FINGERS is off: phase 2 dumps nothing");
		}
		uint32_t oo = atomic_exchange(&g_bf_req_onoff, 0);
		if (oo && (int)(oo == 2) != g_bf.on) {
			g_bf.on = oo == 2 && g_arm.on;
			memset(g_bf.st, 0, sizeof g_bf.st);
			logf_("FINGERS t=%.3f %s (trigger word)%s", mono(), g_bf.on ? "on" : "off",
			      oo == 2 && !g_arm.on ? ": SOMVR_ARMS is off, stays off" : "");
		}
		pthread_mutex_lock(&g_bd_mu);
		if (g_bf_req.seq != seen_t) {
			seen_t = g_bf_req.seq;
			for (int s = 0; s < 2; s++) {
				if (g_bf.t_trig[s] != g_bf_req.trig[s])
					logf_("FINGERS test trigger %s %.2f", g_arm_side[s], g_bf_req.trig[s]);
				if (g_bf.t_thumb[s] != g_bf_req.thumb[s])
					logf_("FINGERS test thumb %s %.2f", g_arm_side[s], g_bf_req.thumb[s]);
				g_bf.t_trig[s] = g_bf_req.trig[s], g_bf.t_thumb[s] = g_bf_req.thumb[s];
			}
		}
		pthread_mutex_unlock(&g_bd_mu);
	}
	double t = mono();
	if (!g_bf.on || t < g_bf.next_log)
		return;
	g_bf.next_log = t + 10.0;
	struct bf_cnt *w = &g_bf.wn;
	if (!w->evals)
		return;
	const float *c0 = g_bf.st[0].c, *c1 = g_bf.st[1].c;
	logf_("FINGERS t=%.3f last 10 s: evals %llu applied L %llu R %llu skipped L %llu R %llu, the game's (bow) L %llu R %llu, "
	      "fist L %llu R %llu; now curls L %.2f/%.2f/%.2f/%.2f/%.2f R %.2f/%.2f/%.2f/%.2f/%.2f (thumb..pinky) weight L %.2f R "
	      "%.2f", t, (unsigned long long)w->evals, (unsigned long long)w->applied[0], (unsigned long long)w->applied[1],
	      (unsigned long long)w->skipped[0], (unsigned long long)w->skipped[1], (unsigned long long)w->game[0],
	      (unsigned long long)w->game[1], (unsigned long long)w->fist[0], (unsigned long long)w->fist[1], c0[0], c0[1],
	      c0[2], c0[3], c0[4], c1[0], c1[1], c1[2], c1[3], c1[4], g_bf.w[0], g_bf.w[1]);
	memset(w, 0, sizeof *w);
}

/* bd_config: the switches */
static void bf_config(int *bad)
{
	static const char *const onoff_w[] = { "0", "1" };
	fg_cfg_default(&g_bf.cfg);
	g_bf.want = env_word("SOMVR_FINGERS", onoff_w, 2, 0);
	g_bf.cfg.tau_s = env_float("SOMVR_FINGERS_MS", g_bf.cfg.tau_s * 1e3f, 0, 500, bad) * 1e-3f;
	g_bf.t_trig[0] = g_bf.t_trig[1] = g_bf.t_thumb[0] = g_bf.t_thumb[1] = -1;
}

/* bd_start: after arm_start */
static void bf_start(void)
{
	if (g_bf.want && !g_arm.on)
		logf_("WARN SOMVR_FINGERS=1 needs SOMVR_ARMS=1 (the hands' IK): the fingers stay the game's");
	g_bf.on = g_bf.want && g_arm.on;
	g_bf.next_log = mono() + 10.0;
	logf_("CONFIG fingers=%d (the grip closes the middle, ring and pinky, the trigger the index (%.0f %% on its touch), the "
	      "thumb rests on a button touch (%.0f %%); a held sword, dagger or hilt = a fist; the bow hand the game's) ease "
	      "tau %.0f ms, blend %.0f ms", g_bf.on, g_bf.cfg.index_touch * 100, g_bf.cfg.thumb_rest * 100, g_bf.cfg.tau_s * 1e3,
	      g_bf.cfg.blend_s * 1e3);
}

static void bf_end_log(void)
{
	if (!g_bf.want)
		return;
	const struct bf_cnt *c = &g_bf.n;
	logf_("FINGERS END on=%d: evals %llu applied L %llu R %llu skipped L %llu R %llu, the game's (bow) L %llu R %llu, fist "
	      "L %llu R %llu", g_bf.on, (unsigned long long)c->evals, (unsigned long long)c->applied[0],
	      (unsigned long long)c->applied[1], (unsigned long long)c->skipped[0], (unsigned long long)c->skipped[1],
	      (unsigned long long)c->game[0], (unsigned long long)c->game[1], (unsigned long long)c->fist[0],
	      (unsigned long long)c->fist[1]);
}
