/* somvr_wisps_game.c - SOMVR_WISPS (build 12; docs/re-xr-pipeline.md 33.9, handoff/HANDOFF-wraith.md): Talion's
 * left-hand wraith wisps on our VR hand. NOT a translation unit of its own: somvr.c includes it last (it uses g_arm,
 * g_vh, the log).
 * With SOMVR_VRHANDS the game's hands are not drawn in the eyes, but the game's left-hand effect (the blue wisps, the
 * wraith's mark) is not part of the body node: it still draws where the game's hidden left hand is - a few cm from the
 * hand we draw (ours: the fist round the grip's -Z tube, its wrist joint behind it; the game's: the weapon node on the
 * grip by SOMVR_ARMS_CAL_L, the hand where the skeleton holds it from there).
 *   SOMVR_WISPS=game (default)  as build 11.
 *   SOMVR_WISPS=hand            in phase 2, after the arms' solve, the game's left hand (L_Hand and the nodes below it,
 *                               not the weapon node: the bow and the dagger stay on the grip) is put with its wrist
 *                               joint on our drawn hand's wrist joint (the hand model's rest wrist on this frame's left
 *                               grip, SOMVR_XR_HANDS_CAL included) and turned about it by the swing that lays its bone
 *                               axis (local X, the arm chain's) along our forearm's (at most 60 deg, else moved only), only while the layer draws our hands in place of the game's (SOMVR_VRHANDS on,
 *                               the left arm solved). Run 063808: the wisps sit on L_Hand 90 (moving that node alone
 *                               moves all of them).
 * Test words (headless): "wisps test LO HI DX DY DZ" moves the left arm's nodes LO..HI by (DX, DY, DZ) cm in Talion's
 * model frame (x out, y up, z forward) after everything else (which node carries the effect); "wisps test off";
 * "wisps game|hand".
 * Lines: `CONFIG wisps ...`, `WISPS t=... L_Hand ... our wrist ...` every 5 s while the left arm is solved, `WISPS END`. */

static struct {
	int want;                      /* SOMVR_WISPS: 0 game, 1 hand */
	_Atomic int mode;              /* the word may change it */
	int model_ok;
	somvr_vh_cfg vc;
	_Atomic uint32_t test_seq;     /* "wisps test": lo, hi, d */
	_Atomic int test_lo, test_hi;
	_Atomic uint32_t test_d[3];    /* float bits */
	uint32_t test_seen;
	int t_lo, t_hi;
	float t_d[3];
	double next_log;
	uint64_t frames, moved, tested, nodes_moved;
	float d_max, d_last, swing_last, swing_max;
} g_wp;

/* the shortest rotation taking unit a onto unit b (identity when either is degenerate or they are opposite) */
static sv_quat wp_swing(sv_vec3 a, sv_vec3 b, float *deg)
{
	float c = a.x * b.x + a.y * b.y + a.z * b.z;
	sv_vec3 x = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	*deg = acosf(fminf(1, fmaxf(-1, c))) * 57.29578f;
	if (!(c > -0.999f) || !isfinite(c))
		return (sv_quat){ 0, 0, 0, 1 };
	return sv_qnormalize((sv_quat){ x.x, x.y, x.z, 1 + c });
}
static sv_vec3 wp_unit(sv_vec3 v)
{
	float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	return l > 1e-6f ? (sv_vec3){ v.x / l, v.y / l, v.z / l } : (sv_vec3){ 0, 0, 0 };
}
static somvr_vh_model g_wp_m;      /* the hand model (as the layer's): our wrist joint in the grip */
static const char *const g_wp_w[] = { "game", "hand" };

static void wp_config(int *bad)
{
	(void)bad;
	g_wp.want = env_word("SOMVR_WISPS", g_wp_w, 2, 0);
	g_wp.t_lo = -1;
	atomic_store(&g_wp.test_lo, -1);
	atomic_store(&g_wp.mode, g_wp.want);
	somvr_vh_cfg_default(&g_wp.vc);
	const char *cal = getenv("SOMVR_XR_HANDS_CAL"); /* the layer's calibration moves our drawn hand: the same here */
	if (cal && cal[0])
		somvr_vh_parse_cal(cal, &g_wp.vc);
	g_wp.model_ok = somvr_vh_init(&g_wp_m, &g_wp.vc) == 0;
	logf_("CONFIG wisps=%s (SOMVR_WISPS: %s); the hand model %s: our wrist joint at %.1f,%.1f,%.1f cm in the left grip "
	      "(game convention)%s", g_wp_w[g_wp.want],
	      g_wp.want ? "the game's left hand (L_Hand and below, not the weapon node) moved onto our drawn hand's wrist in "
			  "phase 2 while our hands are drawn" : "the game's left hand where the skeleton holds it (build 11)",
	      g_wp.model_ok ? "ok" : g_wp_m.why, -100 * g_wp_m.wrist_g[0], 100 * g_wp_m.wrist_g[1], -100 * g_wp_m.wrist_g[2],
	      g_wp.want && !g_vh.want ? " (WARN-free note: SOMVR_VRHANDS is off: nothing moves)" : "");
}

/* the writer thread: "wisps test LO HI DX DY DZ" | "wisps test off" | "wisps game|hand" */
static const char *wp_word(const char *w)
{
	int lo, hi;
	float d[3];
	if (!strncmp(w, "test off", 8)) {
		atomic_store(&g_wp.test_lo, -1);
		atomic_fetch_add(&g_wp.test_seq, 1);
		return "wisps test off";
	}
	if (sscanf(w, "test %d %d %f %f %f", &lo, &hi, &d[0], &d[1], &d[2]) == 5 && lo >= 0 && hi >= lo && hi < SA_MAXN &&
	    fabsf(d[0]) <= 200 && fabsf(d[1]) <= 200 && fabsf(d[2]) <= 200) {
		atomic_store(&g_wp.test_lo, lo), atomic_store(&g_wp.test_hi, hi);
		for (int i = 0; i < 3; i++) {
			uint32_t b;
			memcpy(&b, &d[i], 4);
			atomic_store(&g_wp.test_d[i], b);
		}
		atomic_fetch_add(&g_wp.test_seq, 1);
		return "wisps test (from the next phase 2)";
	}
	for (int i = 0; i < 2; i++)
		if (!strncmp(w, g_wp_w[i], strlen(g_wp_w[i]))) {
			atomic_store(&g_wp.mode, i);
			return i ? "wisps hand" : "wisps game";
		}
	return "wisps (not test LO HI DX DY DZ, test off, game or hand: ignored)";
}

/* arm_eval, after aw_after and before the write-back (the arms' blocks): gg/gv the grips solved to (game convention,
 * tracking space), X Talion's object, shift as arm_eval's */
static void wp_arm(const sa_xf gg[2], const int gv[2], sa_xf X, sv_vec3 shift, const int done[2])
{
	uint32_t sq = atomic_load_explicit(&g_wp.test_seq, memory_order_acquire);
	if (sq != g_wp.test_seen) {
		g_wp.test_seen = sq;
		g_wp.t_lo = atomic_load(&g_wp.test_lo), g_wp.t_hi = atomic_load(&g_wp.test_hi);
		for (int i = 0; i < 3; i++) {
			uint32_t b = atomic_load(&g_wp.test_d[i]);
			memcpy(&g_wp.t_d[i], &b, 4);
		}
		logf_("WISPS test %s", g_wp.t_lo < 0 ? "off" : "on");
		if (g_wp.t_lo >= 0)
			logf_("WISPS test: nodes %d..%d moved by %+.1f,%+.1f,%+.1f cm (model) from the next frame", g_wp.t_lo, g_wp.t_hi,
			      g_wp.t_d[0], g_wp.t_d[1], g_wp.t_d[2]);
	}
	const sa_chain *c = &g_arm.ch[SA_L];
	if (!done[SA_L] || !gv[SA_L] || c->w < 0 || c->w >= SA_MAXN)
		return;
	g_wp.frames++;
	double t = cb_now();
	sa_xf gm = sa_grip_to_model(&g_arm.rig, shift, X, gg[SA_L]);
	float bs = sz_body_scale();
	if (bs != 1.0f)
		gm.p = (sv_vec3){ gm.p.x / bs, gm.p.y / bs, gm.p.z / bs };
	/* our drawn left hand's wrist joint: the model's rest wrist in the right grip, mirrored (x), in game convention
	 * (z negated, m -> cm) */
	sv_vec3 pw = { -100 * g_wp_m.wrist_g[0], 100 * g_wp_m.wrist_g[1], -100 * g_wp_m.wrist_g[2] };
	sv_vec3 ow = sv_qrot(gm.q, pw);
	sv_vec3 wm = { gm.p.x + ow.x, gm.p.y + ow.y, gm.p.z + ow.z };
	sa_node *hn = &g_arm.pose[c->w];
	sv_vec3 d = { wm.x - hn->pos[0], wm.y - hn->pos[1], wm.z - hn->pos[2] };
	float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	g_wp.d_last = dl, g_wp.d_max = fmaxf(g_wp.d_max, dl);
	int hand = atomic_load_explicit(&g_wp.mode, memory_order_relaxed) == 1 && g_wp.model_ok && vh_hides() && isfinite(dl) &&
		   dl < 40.0f;
	/* the long axes: the game's hand bone's X axis (the chain's; its sign: the one nearer ours - run 064438: the solved
	 * elbow is no measure, the IK puts it 125-156 deg off the model's rest forearm), ours: the model's forearm in the
	 * left grip */
	float sw = 0;
	sv_quat qs = { 0, 0, 0, 1 };
	{
		sv_vec3 gx = sv_qrot(sv_qnormalize(sv_quat_of(hn->q)), (sv_vec3){ 1, 0, 0 });
		sv_vec3 oa = wp_unit(sv_qrot(gm.q, (sv_vec3){ -g_wp_m.arm_g[0], g_wp_m.arm_g[1], -g_wp_m.arm_g[2] }));
		if (gx.x * oa.x + gx.y * oa.y + gx.z * oa.z < 0)
			gx = (sv_vec3){ -gx.x, -gx.y, -gx.z };
		qs = wp_swing(wp_unit(gx), oa, &sw);
		if (!(sw <= 60.0f))
			qs = (sv_quat){ 0, 0, 0, 1 };
	}
	g_wp.swing_last = sw, g_wp.swing_max = hand && sw <= 60.0f ? fmaxf(g_wp.swing_max, sw) : g_wp.swing_max;
	if (t >= g_wp.next_log) {
		g_wp.next_log = t + 5.0;
		sa_xf hg = sa_xf_mul(sa_xf_inv(gm), sa_node_xf(hn));
		logf_("WISPS t=%.3f L_Hand %d at %.1f,%.1f,%.1f cm in the left grip (game convention), our wrist %.1f,%.1f,%.1f: "
		      "%.1f cm apart (model %+.1f,%+.1f,%+.1f), the long axes %.1f deg apart; mode %s%s; frames %llu moved %llu (max "
		      "%.1f cm)", t, c->w, hg.p.x, hg.p.y, hg.p.z, pw.x, pw.y, pw.z, dl, d.x, d.y, d.z, sw,
		      g_wp_w[atomic_load(&g_wp.mode)],
		      hand ? ": moved" : vh_hides() ? "" : " (our hands not drawn: not moved)", (unsigned long long)g_wp.frames,
		      (unsigned long long)g_wp.moved, g_wp.d_max);
	}
	if (hand) { /* L_Hand and its subtree turned about the wrist and moved onto ours; the weapon node kept */
		g_wp.moved++;
		sv_vec3 h0 = { hn->pos[0], hn->pos[1], hn->pos[2] };
		int n = 0;
		for (int k = -1; k < c->nsub[2]; k++) {
			int i = k < 0 ? c->w : c->sub[2][k];
			if (i < 0 || i >= SA_MAXN || i == c->wpn) /* run 3: nothing hangs below the weapon node 97 */
				continue;
			sa_node *nd = &g_arm.pose[i];
			sv_vec3 r = sv_qrot(qs, (sv_vec3){ nd->pos[0] - h0.x, nd->pos[1] - h0.y, nd->pos[2] - h0.z });
			nd->pos[0] = wm.x + r.x, nd->pos[1] = wm.y + r.y, nd->pos[2] = wm.z + r.z;
			sv_quat q = sv_qnormalize(sv_qmul(qs, sv_quat_of(nd->q)));
			nd->q[0] = q.x, nd->q[1] = q.y, nd->q[2] = q.z, nd->q[3] = q.w;
			n++;
		}
		g_wp.nodes_moved = (uint64_t)n;
	}
	if (g_wp.t_lo >= 0) { /* the test: nodes lo..hi within the left arm's block */
		g_wp.tested++;
		for (int i = g_wp.t_lo; i <= g_wp.t_hi; i++)
			if (i >= c->lo && i <= c->hi)
				g_arm.pose[i].pos[0] += g_wp.t_d[0], g_arm.pose[i].pos[1] += g_wp.t_d[1], g_arm.pose[i].pos[2] += g_wp.t_d[2];
	}
}

static void wp_end_log(void)
{
	if (!g_wp.want && !g_wp.frames)
		return;
	logf_("WISPS END mode %s: frames %llu (the left arm solved) moved %llu (%llu nodes each) tested %llu; our wrist to "
	      "L_Hand %.1f cm last, %.1f max; the long axes %.1f deg apart last, %.1f max moved", g_wp_w[atomic_load(&g_wp.mode)],
	      (unsigned long long)g_wp.frames, (unsigned long long)g_wp.moved, (unsigned long long)g_wp.nodes_moved,
	      (unsigned long long)g_wp.tested, g_wp.d_last, g_wp.d_max, g_wp.swing_last, g_wp.swing_max);
}
