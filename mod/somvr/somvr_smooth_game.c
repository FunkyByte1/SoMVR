/* somvr_smooth_game.c - SOMVR_HANDS_SMOOTH and SOMVR_SWORD_WEIGHT, the game side (somvr_smooth.h; docs/re-xr-pipeline.md
 * 48). NOT a translation unit of its own: somvr.c includes it after somvr_fight_game.c (it uses g_arm, g_aw, g_bd,
 * g_sf, g_inp, g_fr, the haptics, the log).
 *   SOMVR_HANDS_SMOOTH=off|on   (default off) the One Euro filter (somvr_smooth.h) on both grips the game thread
 *                     locates each stereo frame, each with its own state: phase 2's (the arms: the drawn hands, the
 *                     drawn sword and the hits' blade from R_Weapon) and H4's (the climb, the bow, the slots, the verbs,
 *                     the finishers, the controller blade when no arm is solved). The same pose for what is drawn and
 *                     what hits; the views' time is untouched.
 *   SOMVR_HANDS_SMOOTH_ROT=MIN,BETA  (1.5,12) the rotation's cutoff: MIN Hz + BETA Hz per rad/s of the runtime's angular
 *                     speed; SOMVR_HANDS_SMOOTH_POS=MIN,BETA (4,40): the position's, per m/s.
 *   SOMVR_SWORD_WEIGHT=off|on|light|heavy  (default off; on = light) the sword's blade follows the right hand through
 *                     a critically damped spring-damper (rotation about the grip, a little position), driven only by
 *                     the hand's speed above 1.5..3.5 rad/s (0.6..1.5 m/s): a still or slow hand has none; light: 40/s,
 *                     at most 3 deg and 0.5 cm; heavy: 26/s, 6 deg, 1 cm; both hands on the hilt (SOMVR_TWOHAND): 1.6 x
 *                     stiffer. Set on the weapon node after SOMVR_ARMS_LW=held put it back on the hand, so the drawn
 *                     sword and the hits' blade (R_Weapon) are the same; none while a surface holds the hand
 *                     (SOMVR_HANDS_SURFACE's ghost), the bow, no sword. Plus the haptics: a soft pulse when a swing
 *                     passes 3.5 rad/s (re-armed under 1.5), the hits at full strength and 1.5 x as long.
 *   The measurement runs whatever the switches (it costs a few multiplies): HANDJIT every 10 s.
 *   Trigger word (SOMVR_HANDS_TEST): `hands noise DEG MM` adds white noise (DEG per axis, MM per axis; a fixed seed per
 *   time and hand, so phase 2 and H4 see the same) to the test hands; `hands noise 0 0` = none.
 * Lines: `CONFIG smooth ...`, `HANDJIT t=... last 10 s: ...`, `SWORDWT t=... last 10 s: ...`, `HANDJIT END ...`. */

static struct {
	int on, weight;     /* SOMVR_HANDS_SMOOTH; SOMVR_SWORD_WEIGHT's SWG_* */
	hsm_cfg cfg;
	hsm_state arm[2], inp[2];
	/* the arms' (phase 2) locate against H4's */
	int64_t arm_T;
	int arm_rv;
	float arm_rq[4], arm_rp[3];
	uint64_t t_same, t_other, pairs;
	double t_other_max_ms;
	float pair_mm, pair_deg;
	/* the weight */
	swg_cfg wcfg;
	swg_state ws;
	int64_t w_T; /* the arms' time of the spring's last step */
	uint64_t w_frames, w_applied, w_surface, w_none, whooshes, hits;
	int whoosh_armed;
	double next_log;
	/* the test noise: degrees and millimetres as float bits (the writer thread's word) */
	_Atomic uint32_t noise_deg_b, noise_mm_b;
} g_hsm;

static float hsm_bits_f(uint32_t b)
{
	float f;
	memcpy(&f, &b, 4);
	return f;
}

/* `hands noise DEG MM` (bd_word, the writer thread) */
static const char *hsm_noise_word(const char *w)
{
	float d, m;
	if (sscanf(w, "%f %f", &d, &m) != 2 || !(d >= 0 && d <= 5) || !(m >= 0 && m <= 20))
		return "hands noise (not DEG 0..5 MM 0..20: ignored)";
	uint32_t a, b;
	memcpy(&a, &d, 4), memcpy(&b, &m, 4);
	atomic_store(&g_hsm.noise_deg_b, a), atomic_store(&g_hsm.noise_mm_b, b);
	return "hands noise";
}

/* a Gaussian from a hash of (time, hand, k): the same numbers for the same time (phase 2 and H4 locate the same T) */
static double hsm_gauss(int64_t t, int h, int k)
{
	uint64_t x = (uint64_t)t * 0x9e3779b97f4a7c15ull ^ ((uint64_t)(h * 16 + k) << 56 | (uint64_t)(h * 16 + k));
	double u[2];
	for (int i = 0; i < 2; i++) {
		x += 0x9e3779b97f4a7c15ull;
		uint64_t z = x;
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull, z = (z ^ (z >> 27)) * 0x94d049bb133111ebull, z ^= z >> 31;
		u[i] = ((z >> 11) + 0.5) / 9007199254740992.0;
	}
	return sqrt(-2.0 * log(u[0])) * cos(6.283185307179586 * u[1]);
}

/* bd_test_hands, each test hand: the noise of `hands noise` */
static void hsm_test_noise(int h, somvr_hand *d)
{
	float deg = hsm_bits_f(atomic_load(&g_hsm.noise_deg_b)), mm = hsm_bits_f(atomic_load(&g_hsm.noise_mm_b));
	if (!(deg > 0) && !(mm > 0))
		return;
	float r[3], e[4];
	for (int k = 0; k < 3; k++) {
		r[k] = (float)(hsm_gauss(d->time, h, k) * deg * 0.01745329f);
		d->pose.p[k] += (float)(hsm_gauss(d->time, h, 3 + k) * mm * 0.001f);
	}
	swg_qexp(r, e);
	sv_quat q = sv_qnormalize(sv_qmul(sv_quat_of(e), sv_quat_of(d->pose.q)));
	sv_quat_to(q, d->pose.q);
}

/* arm_eval (phase 2), after the locate and the test hands: the arms' grips filtered (the drawn ones) */
static void hsm_arm(somvr_hand h[2], int64_t T)
{
	g_hsm.arm_T = T;
	g_hsm.arm_rv = h[SOMVR_HAND_R].valid;
	memcpy(g_hsm.arm_rq, h[SOMVR_HAND_R].pose.q, 16), memcpy(g_hsm.arm_rp, h[SOMVR_HAND_R].pose.p, 12);
	for (int k = 0; k < 2; k++)
		hsm_step(&g_hsm.arm[k], &g_hsm.cfg, g_hsm.on, 1.0f, &h[k]);
}

static void hsm_summary(double t);

/* in_frame (H4), after the locate and the test hands: H4's grips filtered; the arms' time and pose against these */
static void hsm_inp(somvr_hand h[2], int64_t T)
{
	const somvr_hand *R = &h[SOMVR_HAND_R];
	if (g_hsm.arm_T) {
		if (g_hsm.arm_T == T) {
			g_hsm.t_same++;
			if (g_hsm.arm_rv && R->valid) {
				float d[3] = { R->pose.p[0] - g_hsm.arm_rp[0], R->pose.p[1] - g_hsm.arm_rp[1],
					       R->pose.p[2] - g_hsm.arm_rp[2] };
				float mm = 1e3f * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				float dg = hsm_qangle(R->pose.q, g_hsm.arm_rq) / 0.01745329f;
				g_hsm.pairs++;
				g_hsm.pair_mm = fmaxf(g_hsm.pair_mm, mm), g_hsm.pair_deg = fmaxf(g_hsm.pair_deg, dg);
			}
		} else {
			g_hsm.t_other++;
			g_hsm.t_other_max_ms = fmax(g_hsm.t_other_max_ms, fabs((double)(T - g_hsm.arm_T)) * 1e-6);
		}
		g_hsm.arm_T = 0;
	}
	for (int k = 0; k < 2; k++)
		hsm_step(&g_hsm.inp[k], &g_hsm.cfg, g_hsm.on, 1.0f, &h[k]);
	hsm_summary(cb_now());
}

/* arm_eval, after aw_after (the weapon nodes back on the solved hands) and before the blade is taken from R_Weapon:
 * the sword's weight on the right weapon node, about the grip (gg: the grips the arms were solved to, after the
 * surfaces and the two hands) */
static void swg_arm(const int done[2], const sa_xf gg[2], const int gv[2], const int bowed[2], sa_xf X, sv_vec3 shift,
		    float dt)
{
	if (!g_hsm.weight)
		return;
	g_hsm.w_frames++;
	const sa_chain *c = &g_arm.ch[SA_R];
	int ok = done[SA_R] && gv[SA_R] && !bowed[SA_R] && c->wpn >= 0 && g_aw.s[SA_R].used_held && bd_sword_in_hand() &&
		 g_arm.weight[SA_R] >= 1;
	if (!ok) {
		g_hsm.ws.have = 0, g_hsm.w_none++, g_hsm.whoosh_armed = 0;
		return;
	}
	if (g_sf.ghost_a[SA_R] > 0) { /* SOMVR_HANDS_SURFACE holds the hand: the blade stays where the surface stopped it */
		g_hsm.ws.have = 0, g_hsm.w_surface++;
		return;
	}
	sa_xf g = gg[SA_R];
	float q[4], p[3] = { g.p.x, g.p.y, g.p.z }, rot[3], pos[3];
	sv_quat_to(sv_qnormalize(g.q), q);
	int two = g_bd.two.w > 0;
	/* the grips' own XR times apart (the frame's wall time when unknown) */
	int64_t T = g_hsm.arm_T;
	if (T > 0 && g_hsm.w_T > 0 && g_hsm.ws.have)
		dt = (float)((double)(T - g_hsm.w_T) * 1e-9);
	g_hsm.w_T = T;
	if (!swg_step(&g_hsm.ws, &g_hsm.wcfg, q, p, dt, two, rot, pos))
		return;
	/* the haptic of a fast swing: the hand's speed from the spring's gated input */
	float u = sqrtf(g_hsm.ws.u[0] * g_hsm.ws.u[0] + g_hsm.ws.u[1] * g_hsm.ws.u[1] + g_hsm.ws.u[2] * g_hsm.ws.u[2]);
	if (u >= g_hsm.wcfg.gate_hi && g_hsm.whoosh_armed) {
		in_haptic_event(SOMVR_HAND_R, two ? 0.15f : 0.22f, 0.05f, SOMVR_HAP_COMBAT);
		g_hsm.whoosh_armed = 0, g_hsm.whooshes++;
	} else if (u < g_hsm.wcfg.gate_lo) {
		g_hsm.whoosh_armed = 1;
	}
	if (rot[0] == 0 && rot[1] == 0 && rot[2] == 0 && pos[0] == 0 && pos[1] == 0 && pos[2] == 0)
		return;
	float er[4];
	swg_qexp(rot, er);
	sa_xf g2 = { { g.p.x + pos[0], g.p.y + pos[1], g.p.z + pos[2] }, sv_qnormalize(sv_qmul(sv_quat_of(er), g.q)) };
	sa_xf gm = sa_grip_to_model(&g_arm.rig, shift, X, g), gm2 = sa_grip_to_model(&g_arm.rig, shift, X, g2);
	float bs = sz_body_scale();
	if (bs != 1.0f)
		gm.p = (sv_vec3){ gm.p.x / bs, gm.p.y / bs, gm.p.z / bs }, gm2.p = (sv_vec3){ gm2.p.x / bs, gm2.p.y / bs, gm2.p.z / bs };
	/* the weapon node as if the grip were at g2: W' = gm2 (x) gm^-1 (x) W (about the grip point) */
	sa_xf W = sa_node_xf(&g_arm.pose[c->wpn]);
	sa_node_set(&g_arm.pose[c->wpn], sa_xf_mul(sa_xf_mul(gm2, sa_xf_inv(gm)), W));
	g_hsm.w_applied++;
}

/* the controller blade's hit haptic with the weight: full strength, 1.5 x as long */
static void swg_hit_haptic(float *amp, float *dur)
{
	if (!g_hsm.weight)
		return;
	*amp = 1.0f, *dur *= 1.5f;
	g_hsm.hits++;
}

static void hsm_summary(double t)
{
	if (t < g_hsm.next_log)
		return;
	g_hsm.next_log = t + 10.0;
	char b[2][220];
	for (int k = 0; k < 2; k++) {
		hsm_jit *j = &g_hsm.arm[k].jit;
		double n = j->n ? (double)j->n : 1;
		snprintf(b[k], sizeof b[k], "%c still %llu: hand %.2f -> %.2f mm %.3f -> %.3f deg, the 1 m tip %.2f -> %.2f mm "
			 "(max %.1f -> %.1f)", "LR"[k], (unsigned long long)j->n, sqrt(j->raw_pos2 / n) * 1e3,
			 sqrt(j->out_pos2 / n) * 1e3, sqrt(j->raw_rot2 / n) / 0.01745329, sqrt(j->out_rot2 / n) / 0.01745329,
			 sqrt(j->raw_tip2 / n) * 1e3, sqrt(j->out_tip2 / n) * 1e3, (double)j->raw_tip_max * 1e3,
			 (double)j->out_tip_max * 1e3);
	}
	if (g_hsm.arm[0].jit.n || g_hsm.arm[1].jit.n || g_hsm.t_same || g_hsm.t_other)
		logf_("HANDJIT t=%.3f last 10 s (smoothing %s): the frame-to-frame jitter (the 2nd difference's RMS, still frames) "
		      "raw -> smoothed: %s; %s; the arms' time = the eyes' %llu, other %llu (up to %.1f ms); the two locates of one "
		      "time differ by up to %.2f mm %.3f deg (%llu); resets L %llu R %llu, a repeated time L %llu R %llu",
		      t, g_hsm.on ? "on" : "off (measured only)", b[0], b[1], (unsigned long long)g_hsm.t_same,
		      (unsigned long long)g_hsm.t_other, g_hsm.t_other_max_ms, (double)g_hsm.pair_mm, (double)g_hsm.pair_deg,
		      (unsigned long long)g_hsm.pairs, (unsigned long long)g_hsm.arm[0].resets,
		      (unsigned long long)g_hsm.arm[1].resets, (unsigned long long)g_hsm.arm[0].same_time,
		      (unsigned long long)g_hsm.arm[1].same_time);
	for (int k = 0; k < 2; k++)
		memset(&g_hsm.arm[k].jit, 0, sizeof g_hsm.arm[k].jit);
	g_hsm.t_same = g_hsm.t_other = g_hsm.pairs = 0, g_hsm.t_other_max_ms = 0, g_hsm.pair_mm = g_hsm.pair_deg = 0;
	if (g_hsm.weight && g_hsm.w_frames) {
		logf_("SWORDWT t=%.3f last 10 s: frames %llu, applied %llu, held by a surface %llu, no sword/arm %llu; the blade off "
		      "the hand up to %.2f deg, the hilt %.2f cm (capped %llu); swing pulses %llu, hits %llu", t,
		      (unsigned long long)g_hsm.w_frames, (unsigned long long)g_hsm.w_applied,
		      (unsigned long long)g_hsm.w_surface, (unsigned long long)g_hsm.w_none,
		      (double)g_hsm.ws.peak_rad / 0.01745329, (double)g_hsm.ws.peak_pos, (unsigned long long)g_hsm.ws.capped,
		      (unsigned long long)g_hsm.whooshes, (unsigned long long)g_hsm.hits);
		g_hsm.w_frames = g_hsm.w_applied = g_hsm.w_surface = g_hsm.w_none = 0, g_hsm.ws.peak_rad = g_hsm.ws.peak_pos = 0;
		g_hsm.ws.capped = 0;
	}
}

/* bd_config */
static void hsm_config(int *bad)
{
	static const char *const onoff_w[] = { "off", "on" };
	static const char *const wt_w[] = { "off", "light", "heavy", "on" };
	hsm_defaults(&g_hsm.cfg);
	g_hsm.on = env_word("SOMVR_HANDS_SMOOTH", onoff_w, 2, 0);
	const char *v = getenv("SOMVR_HANDS_SMOOTH_ROT");
	if (v && v[0] && !hsm_parse_pair(v, &g_hsm.cfg.rot_min_hz, &g_hsm.cfg.rot_beta))
		*bad = 1, logf_("WARN SOMVR_HANDS_SMOOTH_ROT=%s is not MIN,BETA (0.05..60 Hz, 0..1000): using %.1f,%.1f", v,
				(double)HSM_ROT_MIN_HZ, (double)HSM_ROT_BETA);
	v = getenv("SOMVR_HANDS_SMOOTH_POS");
	if (v && v[0] && !hsm_parse_pair(v, &g_hsm.cfg.pos_min_hz, &g_hsm.cfg.pos_beta))
		*bad = 1, logf_("WARN SOMVR_HANDS_SMOOTH_POS=%s is not MIN,BETA (0.05..60 Hz, 0..1000): using %.1f,%.1f", v,
				(double)HSM_POS_MIN_HZ, (double)HSM_POS_BETA);
	int wt = env_word("SOMVR_SWORD_WEIGHT", wt_w, 4, 0);
	g_hsm.weight = wt == 3 ? SWG_LIGHT : wt;
	swg_defaults(&g_hsm.wcfg, g_hsm.weight ? g_hsm.weight : SWG_LIGHT, 100.0f); /* the grips in cm */
	if (g_hsm.weight && !g_arm.on)
		*bad = 1, logf_("WARN SOMVR_SWORD_WEIGHT needs SOMVR_ARMS=1 (the weapon node on the hand): no weight"),
		g_hsm.weight = 0;
	logf_("CONFIG smooth hands=%s rot %.2f Hz + %.1f/(rad/s) (the delay %.1f ms at 3 rad/s) pos %.2f Hz + %.1f/(m/s) (%.1f ms "
	      "at 1 m/s); sword weight=%s (omega %.0f/s, at most %.1f deg and %.2f cm, gate %.1f..%.1f rad/s, two hands x%.1f); "
	      "HANDJIT every 10 s",
	      g_hsm.on ? "on" : "off", (double)g_hsm.cfg.rot_min_hz, (double)g_hsm.cfg.rot_beta,
	      (double)hsm_tau(g_hsm.cfg.rot_min_hz, g_hsm.cfg.rot_beta, 3.0f) * 1e3, (double)g_hsm.cfg.pos_min_hz,
	      (double)g_hsm.cfg.pos_beta, (double)hsm_tau(g_hsm.cfg.pos_min_hz, g_hsm.cfg.pos_beta, 1.0f) * 1e3,
	      g_hsm.weight == SWG_HEAVY ? "heavy" : g_hsm.weight ? "light" : "off", (double)g_hsm.wcfg.omega,
	      (double)g_hsm.wcfg.cap_rad / 0.01745329, (double)g_hsm.wcfg.cap_pos, (double)g_hsm.wcfg.gate_lo,
	      (double)g_hsm.wcfg.gate_hi, (double)g_hsm.wcfg.two_k);
}

static void hsm_end_log(void)
{
	logf_("HANDJIT END smoothing %s: steps L %llu R %llu (H4 L %llu R %llu), resets L %llu R %llu; weight %s: swing pulses "
	      "%llu, hits %llu, the spring's resets %llu",
	      g_hsm.on ? "on" : "off", (unsigned long long)g_hsm.arm[0].steps, (unsigned long long)g_hsm.arm[1].steps,
	      (unsigned long long)g_hsm.inp[0].steps, (unsigned long long)g_hsm.inp[1].steps,
	      (unsigned long long)g_hsm.arm[0].resets, (unsigned long long)g_hsm.arm[1].resets,
	      g_hsm.weight == SWG_HEAVY ? "heavy" : g_hsm.weight ? "light" : "off", (unsigned long long)g_hsm.whooshes,
	      (unsigned long long)g_hsm.hits, (unsigned long long)g_hsm.ws.resets);
}
