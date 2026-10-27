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
 *   SOMVR_SWORD_WEIGHT=off|light|heavy|on  (default off; on = light; build 16, docs/re-xr-pipeline.md 52) the sword's
 *                     blade follows the right hand's direction through an acceleration-limited follower (somvr_smooth.h):
 *                     slow cuts 1:1, fast ones trail (heavy: up to 16 deg, back in ~0.1-0.3 s), quick wiggles mostly
 *                     absorbed (heavy: a 5 Hz one a third), the twist about the blade and the hand 1:1; both hands on
 *                     the hilt (SOMVR_TWOHAND) near 1:1. Set on the weapon node after SOMVR_ARMS_LW=held put it back on
 *                     the hand, so the drawn sword is the hits' blade (R_Weapon -> the combat blade -> the defence's
 *                     contact and the clank); none while a surface holds the hand (SOMVR_HANDS_SURFACE's ghost), the
 *                     bow, no sword. Haptics: a drag pulse when the blade falls behind in a swing (heavy stronger), the
 *                     hits at full strength and longer (heavy 2 x, the left hand too with both on the hilt). The VR
 *                     menu's Sword weight row (Off / Light / Heavy; settings.conf SWORD_WEIGHT) sets it live.
 *   SOMVR_SWORD_WEIGHT_HAND=fixed|follow  (default fixed) follow: the layer's drawn hands (SOMVR_VRHANDS) turn with the
 *                     weighted blade about their grip points, so the fist stays closed on the handle (the right hand;
 *                     the left too with both hands on the hilt); their positions and the twist stay the controllers'.
 *                     fixed: the hands stay on the controllers and the handle turns in the fist. Build 17: the VR
 *                     menu's Sword hand row (Fixed / Follow; settings.conf SWORD_WEIGHT_HAND) sets it live.
 *   SOMVR_SWORD_REC=off|on  (default off) the right grip as the weight's step gets it, each arms frame it applies, into
 *                     <SOMVR_LOG>.swordrec (text, one line a frame, at most 48 MB) for replays (tests/test_smooth.c with
 *                     SWG_REPLAY=FILE: every level on the user's own motion).
 *   The measurement runs whatever the switches (it costs a few multiplies): HANDJIT every 10 s.
 *   Trigger words (SOMVR_HANDS_TEST): `hands noise DEG MM` adds white noise (DEG per axis, MM per axis; a fixed seed per
 *   time and hand, so phase 2 and H4 see the same) to the test hands; `hands noise 0 0` = none. `hands wiggle L|R DEG HZ
 *   S` (build 16): that test hand turns +-DEG about its own up axis at HZ for S s (the weight's proof); `hands
 *   weighthand fixed|follow`: SOMVR_SWORD_WEIGHT_HAND live.
 * Lines: `CONFIG smooth ...`, `HANDJIT t=... last 10 s: ...`, `SWORDWT t=... last 10 s: ...`, `SWORDWT t=... weight A ->
 * B (the VR menu)`, `SWORDWT t=... the drawn hands fixed|follow the blade (the VR menu's Sword hand|trigger word)`,
 * `HANDJIT END ...`. */

static struct {
	int on, weight;     /* SOMVR_HANDS_SMOOTH; SOMVR_SWORD_WEIGHT's SWG_* */
	int hand_follow;    /* SOMVR_SWORD_WEIGHT_HAND=follow */
	_Atomic int hand_req; /* `hands weighthand fixed|follow` (the writer thread): 1 + the mode; build 17, the VR menu's
				 Sword hand row (swg_vm_hand_set): 3 + the mode; 0 = none */
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
	uint64_t w_frames, w_applied, w_surface, w_none, whooshes, hits, w_followed;
	int whoosh_armed;
	float w_hand_max, w_blade_max; /* rad/s: the hand's and the blade's peak swing speeds in the window */
	double drag_t;                 /* the last drag pulse (at most one each SWG_DRAG_GAP_S: a wiggle is no buzz) */
	/* SOMVR_SWORD_REC=on: the right grip each arms frame the weight applies, for replays (tests/test_smooth.c
	 * SWG_REPLAY=FILE): <SOMVR_LOG>.swordrec */
	FILE *rec;
	char rec_path[512];
	uint64_t rec_n, rec_bytes;
	float w_prev_rot[3];           /* the last offset (the blade's own speed) */
	float w_rot_now[3];            /* this arms frame's offset on the weapon node (w_now: applied this frame); build 17's
					* bite turns the drawn blade on top of it (somvr_hitfeel_game.c) */
	int w_now;
	double next_log;
	/* the test noise: degrees and millimetres as float bits (the writer thread's word) */
	_Atomic uint32_t noise_deg_b, noise_mm_b;
	/* the test wiggle (`hands wiggle`): the writer thread's word (side, degrees, Hz, seconds as float bits; seq) and
	 * the game thread's run of it (from the first test hand's time after the word) */
	_Atomic uint32_t wig_b[3];
	_Atomic int wig_side, wig_seq;
	int wig_seen, wig_on, wig_s;
	float wig_deg, wig_hz, wig_dur;
	int64_t wig_t0, wig_last;
	uint64_t wig_frames;
	/* the weight during a wiggle (swg_arm): the hand's angle about the grip's up axis (its speed integrated) and the
	 * blade's (+ the offset's part on that axis), their extremes; the blade's largest offset */
	double wig_th, wig_lk[4]; /* the lock-in at the wiggle's frequency: the hand's and the blade's sin, cos sums */
	float wig_h[2], wig_bl[2], wig_off;
	uint64_t wig_n;
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

/* `hands wiggle L|R DEG HZ S` (bd_word, the writer thread): the test hand turns +-DEG about its own up axis (the blade
 * sweeps sideways) at HZ for S seconds, a sine from its test pose; `hands wiggle R 0 1 1` = none */
static const char *hsm_wiggle_word(const char *w)
{
	char side = 0;
	float d, hz, sec;
	if (sscanf(w, " %c %f %f %f", &side, &d, &hz, &sec) != 4 || (side != 'L' && side != 'R') || !(d >= 0 && d <= 45) ||
	    !(hz >= 0.2f && hz <= 12) || !(sec > 0 && sec <= 20))
		return "hands wiggle (not L|R DEG 0..45 HZ 0.2..12 S 0..20: ignored)";
	float v[3] = { d, hz, sec };
	for (int i = 0; i < 3; i++) {
		uint32_t b;
		memcpy(&b, &v[i], 4);
		atomic_store(&g_hsm.wig_b[i], b);
	}
	atomic_store(&g_hsm.wig_side, side == 'R' ? SOMVR_HAND_R : SOMVR_HAND_L);
	atomic_fetch_add(&g_hsm.wig_seq, 1);
	return "hands wiggle";
}

/* `hands weighthand fixed|follow` (bd_word, the writer thread): SOMVR_SWORD_WEIGHT_HAND live (the proof's both modes) */
static const char *hsm_hand_word(const char *w)
{
	char m[16] = "";
	if (sscanf(w, "%15s", m) != 1 || (strcmp(m, "fixed") && strcmp(m, "follow")))
		return "hands weighthand (not fixed|follow: ignored)";
	atomic_store(&g_hsm.hand_req, 1 + !strcmp(m, "follow"));
	return "hands weighthand";
}

/* bd_test_hands, each test hand (after a swing's pose): a running wiggle's turn and its angular speed (OpenXR space) */
static void hsm_test_wiggle(int h, somvr_hand *d)
{
	int seq = atomic_load(&g_hsm.wig_seq);
	if (seq != g_hsm.wig_seen && d->time > 0) {
		g_hsm.wig_seen = seq, g_hsm.wig_s = atomic_load(&g_hsm.wig_side);
		g_hsm.wig_deg = hsm_bits_f(atomic_load(&g_hsm.wig_b[0])), g_hsm.wig_hz = hsm_bits_f(atomic_load(&g_hsm.wig_b[1]));
		g_hsm.wig_dur = hsm_bits_f(atomic_load(&g_hsm.wig_b[2]));
		g_hsm.wig_t0 = d->time, g_hsm.wig_on = g_hsm.wig_deg > 0, g_hsm.wig_frames = 0;
		g_hsm.wig_th = 0, g_hsm.wig_n = 0, g_hsm.wig_off = 0;
		memset(g_hsm.wig_lk, 0, sizeof g_hsm.wig_lk);
		g_hsm.wig_h[0] = g_hsm.wig_bl[0] = 1e9f, g_hsm.wig_h[1] = g_hsm.wig_bl[1] = -1e9f;
		logf_("BODY test wiggle %s: +-%.1f deg at %.1f Hz for %.1f s (about the grip's up axis)", g_arm_side[g_hsm.wig_s],
		      (double)g_hsm.wig_deg, (double)g_hsm.wig_hz, (double)g_hsm.wig_dur);
	}
	if (!g_hsm.wig_on || h != g_hsm.wig_s)
		return;
	double x = (double)(d->time - g_hsm.wig_t0) * 1e-9;
	if (x < 0)
		return;
	if (x > g_hsm.wig_dur) {
		g_hsm.wig_on = 0;
		float hh = 0.5f * (g_hsm.wig_h[1] - g_hsm.wig_h[0]), bb = 0.5f * (g_hsm.wig_bl[1] - g_hsm.wig_bl[0]);
		double *k = g_hsm.wig_lk, n = (double)(g_hsm.wig_n ? g_hsm.wig_n : 1);
		double ha = 2 / n * sqrt(k[0] * k[0] + k[1] * k[1]), ba = 2 / n * sqrt(k[2] * k[2] + k[3] * k[3]);
		double lag = (atan2(k[1], k[0]) - atan2(k[3], k[2])) / 0.017453292519943295;
		lag = lag > 180 ? lag - 360 : lag < -180 ? lag + 360 : lag;
		if (g_hsm.wig_n > 2 && hh > 0)
			logf_("BODY test wiggle %s end: %llu frames; with the weight (%s, %s): at %.1f Hz the blade +-%.1f deg for "
			      "the hand's +-%.1f (%.0f %%, %.0f deg behind); the extremes +-%.1f for +-%.1f; off the hand up to %.1f "
			      "deg (%llu frames)", g_arm_side[h], (unsigned long long)g_hsm.wig_frames,
			      g_hsm.weight == SWG_HEAVY ? "heavy" : g_hsm.weight ? "light" : "off",
			      g_bd.two.w > 0 ? "two hands" : "one hand", (double)g_hsm.wig_hz, ba / 0.017453292519943295,
			      ha / 0.017453292519943295, ha > 0 ? 100.0 * ba / ha : 0.0, lag, (double)bb / 0.01745329,
			      (double)hh / 0.01745329, (double)g_hsm.wig_off / 0.01745329, (unsigned long long)g_hsm.wig_n);
		else
			logf_("BODY test wiggle %s end: %llu frames (no weight measured: %llu frames)", g_arm_side[h],
			      (unsigned long long)g_hsm.wig_frames, (unsigned long long)g_hsm.wig_n);
		return;
	}
	if (d->time != g_hsm.wig_last)
		g_hsm.wig_frames++, g_hsm.wig_last = d->time;
	double ph = 6.283185307179586 * g_hsm.wig_hz * x, a = g_hsm.wig_deg * 0.017453292519943295;
	float th = (float)(a * sin(ph)), rate = (float)(a * 6.283185307179586 * g_hsm.wig_hz * cos(ph));
	sv_quat q = sv_qnormalize(sv_quat_of(d->pose.q)), dq = { 0, sinf(0.5f * th), 0, cosf(0.5f * th) };
	sv_vec3 up = sv_qrot(q, (sv_vec3){ 0, 1, 0 }); /* the grip's own +Y */
	sv_quat_to(sv_qnormalize(sv_qmul(q, dq)), d->pose.q);
	d->ang[0] += rate * up.x, d->ang[1] += rate * up.y, d->ang[2] += rate * up.z;
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
	hsm_test_wiggle(h, d); /* `hands wiggle` */
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

#define SWG_DRAG_GAP_S 0.3
#define SWG_REC_MAX (48u << 20) /* SOMVR_SWORD_REC's file: about 2.5 h of a drawn sword at 90 Hz */

/* the weight's level now (SOMVR_SWORD_WEIGHT, the VR menu): its constants, the spring from the hand */
static void swg_level(int lv)
{
	g_hsm.weight = lv;
	swg_defaults(&g_hsm.wcfg, lv ? lv : SWG_LIGHT);
	swg_reset(&g_hsm.ws);
	g_hsm.whoosh_armed = 0;
}

/* SOMVR_SWORD_WEIGHT_HAND=follow (swg_arm): the layer's drawn hands (vh_arm's entry of this frame, from the same
 * grips gg) turned with the weighted blade about their grip points - the right one, and the left with both hands on
 * the hilt; their positions and the twist about the blade stay the controllers' */
static void swg_vh_follow(somvr_vrhands_frame *e, const sa_xf gg[2], const float rot[3], int two)
{
	for (int h = 0; h < 2; h++) {
		if (!(e->flags & (h ? SOMVR_VHF_R : SOMVR_VHF_L)) || (h == SA_L && !two))
			continue;
		float q[4], oq[4];
		sv_quat_to(sv_qnormalize(gg[h].q), q);
		swg_follow(rot, q, oq);
		sa_xf g2 = { gg[h].p, sv_quat_of(oq) };
		vh_to_xr(&g2, &e->grip[h]);
	}
	g_hsm.w_followed++;
}

/* arm_eval, after aw_after (the weapon nodes back on the solved hands) and before the blade is taken from R_Weapon:
 * the sword's weight on the right weapon node, about the grip (gg: the grips the arms were solved to, after the
 * surfaces and the two hands) */
static void swg_arm(const int done[2], const sa_xf gg[2], const int gv[2], const int bowed[2], sa_xf X, sv_vec3 shift,
		    float dt)
{
	int hr = atomic_exchange(&g_hsm.hand_req, 0);
	if (hr) {
		int was = g_hsm.hand_follow;
		g_hsm.hand_follow = (hr - 1) & 1;
		logf_("SWORDWT t=%.3f the drawn hands %s%s (%s)", cb_now(), g_hsm.hand_follow ? "follow the blade" : "fixed",
		      was == g_hsm.hand_follow ? " (already)" : "", hr >= 3 ? "the VR menu's Sword hand" : "trigger word");
	}
	g_hsm.w_now = 0;
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
	float q[4], rot[3];
	sv_quat_to(sv_qnormalize(g.q), q);
	int two = g_bd.two.w > 0;
	/* the grips' own XR times apart (the frame's wall time when unknown) */
	int64_t T = g_hsm.arm_T;
	if (T > 0 && g_hsm.w_T > 0 && g_hsm.ws.have)
		dt = (float)((double)(T - g_hsm.w_T) * 1e-9);
	g_hsm.w_T = T;
	/* the blade in the grip: along +Z after the mirror, tilted up by SOMVR_SWORD_ANGLE (the twist is about it) */
	float a = g_bd.angle_deg * 0.01745329f, axis[3] = { 0, sinf(a), cosf(a) };
	float pdt = dt;
	int stepped = swg_step(&g_hsm.ws, &g_hsm.wcfg, q, axis, dt, two, rot);
	if (g_hsm.rec && g_hsm.rec_bytes < SWG_REC_MAX) { /* SOMVR_SWORD_REC: the input as the step had it */
		int nb = fprintf(g_hsm.rec, "%.6f %.6f %.6f %.6f %.6f %d %d %.2f %.3f\n", T > 0 ? (double)T * 1e-9 : cb_now(),
				 (double)q[0], (double)q[1], (double)q[2], (double)q[3], two, g_hsm.weight, (double)g_bd.angle_deg,
				 stepped ? (double)g_hsm.ws.off / 0.01745329 : 0.0);
		g_hsm.rec_n++, g_hsm.rec_bytes += nb > 0 ? (uint64_t)nb : 0;
		if (g_hsm.rec_bytes >= SWG_REC_MAX)
			logf_("SWORDREC full (%llu frames, %llu bytes): no more recorded", (unsigned long long)g_hsm.rec_n,
			      (unsigned long long)g_hsm.rec_bytes);
	}
	if (!stepped) {
		memset(g_hsm.w_prev_rot, 0, 12);
		return;
	}
	/* the window's peaks: the hand's swing speed, the blade's (the hand's + the offset's change) */
	float wh = sqrtf(g_hsm.ws.wh[0] * g_hsm.ws.wh[0] + g_hsm.ws.wh[1] * g_hsm.ws.wh[1] + g_hsm.ws.wh[2] * g_hsm.ws.wh[2]);
	float wb[3];
	for (int k = 0; k < 3; k++)
		wb[k] = g_hsm.ws.wh[k] + (pdt > 0 ? (rot[k] - g_hsm.w_prev_rot[k]) / pdt : 0);
	memcpy(g_hsm.w_prev_rot, rot, 12);
	if (g_hsm.wig_on && g_hsm.wig_s == SOMVR_HAND_R && pdt > 0) { /* `hands wiggle`: the swing about the grip's up axis */
		sv_vec3 up = sv_qrot(sv_qnormalize(g.q), (sv_vec3){ 0, 1, 0 });
		g_hsm.wig_th += (double)((g_hsm.ws.wh[0] * up.x + g_hsm.ws.wh[1] * up.y + g_hsm.ws.wh[2] * up.z) * pdt);
		float th = (float)g_hsm.wig_th, tb = th + rot[0] * up.x + rot[1] * up.y + rot[2] * up.z;
		g_hsm.wig_h[0] = fminf(g_hsm.wig_h[0], th), g_hsm.wig_h[1] = fmaxf(g_hsm.wig_h[1], th);
		g_hsm.wig_bl[0] = fminf(g_hsm.wig_bl[0], tb), g_hsm.wig_bl[1] = fmaxf(g_hsm.wig_bl[1], tb);
		g_hsm.wig_off = fmaxf(g_hsm.wig_off, g_hsm.ws.off), g_hsm.wig_n++;
		double ph = 6.283185307179586 * g_hsm.wig_hz * (double)(T - g_hsm.wig_t0) * 1e-9;
		g_hsm.wig_lk[0] += th * sin(ph), g_hsm.wig_lk[1] += th * cos(ph), g_hsm.wig_lk[2] += tb * sin(ph);
		g_hsm.wig_lk[3] += tb * cos(ph);
	}
	g_hsm.w_hand_max = fmaxf(g_hsm.w_hand_max, wh);
	g_hsm.w_blade_max = fmaxf(g_hsm.w_blade_max, sqrtf(wb[0] * wb[0] + wb[1] * wb[1] + wb[2] * wb[2]));
	/* the drag: a pulse when the blade falls behind in a swing (stronger the faster), re-armed once it is back */
	float k2 = two ? g_hsm.wcfg.two_k : 1.0f, drag = 0.6f * g_hsm.wcfg.rope0 / k2;
	double now = cb_now();
	if (g_hsm.ws.off >= drag && wh >= 3.0f && g_hsm.whoosh_armed && now - g_hsm.drag_t >= SWG_DRAG_GAP_S) {
		float f = (wh - 4.0f) / 8.0f, amp = g_hsm.weight == SWG_HEAVY ? 0.4f : 0.25f;
		f = f < 0 ? 0 : f > 1 ? 1 : f;
		amp += (g_hsm.weight == SWG_HEAVY ? 0.3f : 0.15f) * f * f * (3 - 2 * f);
		in_haptic_event(SOMVR_HAND_R, two ? 0.7f * amp : amp, 0.07f, SOMVR_HAP_COMBAT);
		if (two)
			in_haptic_event(SOMVR_HAND_L, 0.5f * amp, 0.07f, SOMVR_HAP_COMBAT);
		g_hsm.whoosh_armed = 0, g_hsm.whooshes++, g_hsm.drag_t = now;
	} else if (g_hsm.ws.off < 0.5f * drag) {
		g_hsm.whoosh_armed = 1;
	}
	if (rot[0] == 0 && rot[1] == 0 && rot[2] == 0)
		return;
	if (g_hsm.hand_follow && g_vh.on && g_vh.have) /* SOMVR_SWORD_WEIGHT_HAND=follow: our drawn hands turn with it */
		swg_vh_follow(&g_vh.e, gg, rot, two);
	float er[4];
	swg_qexp(rot, er);
	sa_xf g2 = { g.p, sv_qnormalize(sv_qmul(sv_quat_of(er), g.q)) };
	sa_xf gm = sa_grip_to_model(&g_arm.rig, shift, X, g), gm2 = sa_grip_to_model(&g_arm.rig, shift, X, g2);
	float bs = sz_body_scale();
	if (bs != 1.0f)
		gm.p = (sv_vec3){ gm.p.x / bs, gm.p.y / bs, gm.p.z / bs }, gm2.p = (sv_vec3){ gm2.p.x / bs, gm2.p.y / bs, gm2.p.z / bs };
	/* the weapon node as if the grip were at g2: W' = gm2 (x) gm^-1 (x) W (about the grip point) */
	sa_xf W = sa_node_xf(&g_arm.pose[c->wpn]);
	sa_node_set(&g_arm.pose[c->wpn], sa_xf_mul(sa_xf_mul(gm2, sa_xf_inv(gm)), W));
	g_hsm.w_applied++;
	memcpy(g_hsm.w_rot_now, rot, 12), g_hsm.w_now = 1;
}

/* the controller blade's hit haptic with the weight: full strength, longer (heavy 2 x, light 1.5 x); both hands on the
 * hilt: the left hand feels it too */
static void swg_hit_haptic(float *amp, float *dur)
{
	if (!g_hsm.weight)
		return;
	*amp = 1.0f, *dur *= g_hsm.weight == SWG_HEAVY ? 2.0f : 1.5f;
	if (g_bd.two.w > 0)
		in_haptic_event(SOMVR_HAND_L, 0.6f, *dur, SOMVR_HAP_COMBAT);
	g_hsm.hits++;
}

/* the VR menu's Sword weight row (vm_apply_key, vm_start): settings' sword_weight 0 off, 1 light, 2 heavy, live */
static const char *swg_vm_set(const somvr_settings *s)
{
	if (!g_arm.on)
		return " (SOMVR_ARMS is off: no sword in the hand)";
	int lv = s->sword_weight > SWG_HEAVY ? SWG_HEAVY : s->sword_weight;
	if (lv != g_hsm.weight) {
		static const char *const nm[] = { "off", "light", "heavy" };
		logf_("SWORDWT t=%.3f weight %s -> %s (the VR menu)", cb_now(), nm[g_hsm.weight], nm[lv]);
		swg_level(lv);
	}
	return " (the drawn sword = the hits' blade)";
}
static void swg_vm_settings(somvr_settings *s)
{
	s->sword_weight = (uint8_t)g_hsm.weight, s->sword_hand = (uint8_t)g_hsm.hand_follow;
}
/* build 17: the VR menu's Sword hand row (SOMVR_SWORD_WEIGHT_HAND live): swg_arm takes it at the arms' next frame */
static const char *swg_vm_hand_set(const somvr_settings *s)
{
	if (!g_arm.on || !g_vh.want)
		return " (no drawn hands to turn: SOMVR_ARMS and SOMVR_VRHANDS)";
	atomic_store(&g_hsm.hand_req, 3 + (s->sword_hand != 0));
	if (!g_hsm.weight)
		return " (used with the Sword weight on)";
	return s->sword_hand ? " (the drawn hands turn with the weighted blade)" : " (the drawn hands stay on the controllers)";
}
/* the Sword weight row with the arms; the Sword hand row with the layer's hands too */
static uint32_t swg_vm_avail(void)
{
	return g_arm.on ? SOMVR_VMA_WEIGHT | (g_vh.want ? SOMVR_VMA_SWHAND : 0u) : 0u;
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
		logf_("SWORDWT t=%.3f last 10 s (%s): frames %llu, applied %llu, held by a surface %llu, no sword/arm %llu; the "
		      "blade off the hand up to %.2f deg (at the rope %llu sub-steps); the swing speed up to %.1f rad/s, the "
		      "blade's %.1f; drag pulses %llu, hits %llu; the drawn hands followed in %llu frames (%s)", t,
		      g_hsm.weight == SWG_HEAVY ? "heavy" : "light",
		      (unsigned long long)g_hsm.w_frames, (unsigned long long)g_hsm.w_applied,
		      (unsigned long long)g_hsm.w_surface, (unsigned long long)g_hsm.w_none,
		      (double)g_hsm.ws.peak_rad / 0.01745329, (unsigned long long)g_hsm.ws.roped, (double)g_hsm.w_hand_max,
		      (double)g_hsm.w_blade_max, (unsigned long long)g_hsm.whooshes, (unsigned long long)g_hsm.hits,
		      (unsigned long long)g_hsm.w_followed, g_hsm.hand_follow ? "follow" : "fixed");
		g_hsm.w_frames = g_hsm.w_applied = g_hsm.w_surface = g_hsm.w_none = g_hsm.w_followed = 0, g_hsm.ws.peak_rad = 0;
		g_hsm.ws.roped = 0, g_hsm.w_hand_max = g_hsm.w_blade_max = 0;
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
	swg_level(wt == 3 ? SWG_LIGHT : wt);
	if (g_hsm.weight && !g_arm.on)
		*bad = 1, logf_("WARN SOMVR_SWORD_WEIGHT needs SOMVR_ARMS=1 (the weapon node on the hand): no weight"),
		swg_level(SWG_OFF);
	static const char *const hand_w[] = { "fixed", "follow" };
	g_hsm.hand_follow = env_word("SOMVR_SWORD_WEIGHT_HAND", hand_w, 2, 0);
	if (g_hsm.hand_follow && !g_vh.want)
		logf_("CONFIG smooth: SOMVR_SWORD_WEIGHT_HAND=follow turns the layer's own hands (SOMVR_VRHANDS=1, off here): "
		      "nothing to turn");
	const char *lp = getenv("SOMVR_LOG");
	if (env_word("SOMVR_SWORD_REC", onoff_w, 2, 0) && !g_hsm.rec && lp && lp[0]) {
		snprintf(g_hsm.rec_path, sizeof g_hsm.rec_path, "%s.swordrec", lp);
		g_hsm.rec = fopen(g_hsm.rec_path, "w");
		if (!g_hsm.rec)
			*bad = 1, logf_("WARN SOMVR_SWORD_REC: %s not opened (%s): not recorded", g_hsm.rec_path, strerror(errno));
		else {
			setvbuf(g_hsm.rec, NULL, _IOFBF, 1 << 16);
			fprintf(g_hsm.rec, "# SWORDREC v1 (re-xr-pipeline.md 52): xr_time_s qx qy qz qw two level sword_angle_deg "
					   "off_deg - the right grip in the arms' space each arms frame the weight applies (the input "
					   "of swg_step; replay: SWG_REPLAY=FILE tests/test_smooth)\n");
			logf_("CONFIG sword rec: %s", g_hsm.rec_path);
		}
	}
	logf_("CONFIG smooth hands=%s rot %.2f Hz + %.1f/(rad/s) (the delay %.1f ms at 3 rad/s) pos %.2f Hz + %.1f/(m/s) (%.1f ms "
	      "at 1 m/s); sword weight=%s (the blade's swing speed grows at %.0f, slows at %.0f rad/s^2, at most %.0f rad/s, "
	      "the rope %.0f..%.0f deg, the pull %.0f/s; two hands x%.1f; light/heavy in the VR menu; the drawn hands %s); "
	      "HANDJIT every 10 s",
	      g_hsm.on ? "on" : "off", (double)g_hsm.cfg.rot_min_hz, (double)g_hsm.cfg.rot_beta,
	      (double)hsm_tau(g_hsm.cfg.rot_min_hz, g_hsm.cfg.rot_beta, 3.0f) * 1e3, (double)g_hsm.cfg.pos_min_hz,
	      (double)g_hsm.cfg.pos_beta, (double)hsm_tau(g_hsm.cfg.pos_min_hz, g_hsm.cfg.pos_beta, 1.0f) * 1e3,
	      g_hsm.weight == SWG_HEAVY ? "heavy" : g_hsm.weight ? "light" : "off", (double)g_hsm.wcfg.a_up,
	      (double)g_hsm.wcfg.a_dn, (double)g_hsm.wcfg.w_max, (double)g_hsm.wcfg.rope0 / 0.01745329,
	      (double)g_hsm.wcfg.rope / 0.01745329, (double)(g_hsm.wcfg.omega / (2 * g_hsm.wcfg.zeta)),
	      (double)g_hsm.wcfg.two_k, g_hsm.hand_follow ? "follow the blade (SOMVR_SWORD_WEIGHT_HAND)" : "fixed on the controllers");
}

static void hsm_end_log(void)
{
	logf_("HANDJIT END smoothing %s: steps L %llu R %llu (H4 L %llu R %llu), resets L %llu R %llu; weight %s: drag pulses "
	      "%llu, hits %llu, the spring's resets %llu",
	      g_hsm.on ? "on" : "off", (unsigned long long)g_hsm.arm[0].steps, (unsigned long long)g_hsm.arm[1].steps,
	      (unsigned long long)g_hsm.inp[0].steps, (unsigned long long)g_hsm.inp[1].steps,
	      (unsigned long long)g_hsm.arm[0].resets, (unsigned long long)g_hsm.arm[1].resets,
	      g_hsm.weight == SWG_HEAVY ? "heavy" : g_hsm.weight ? "light" : "off", (unsigned long long)g_hsm.whooshes,
	      (unsigned long long)g_hsm.hits, (unsigned long long)g_hsm.ws.resets);
	if (g_hsm.rec) {
		fclose(g_hsm.rec), g_hsm.rec = NULL;
		logf_("SWORDREC END %llu frames, %llu bytes: %s", (unsigned long long)g_hsm.rec_n,
		      (unsigned long long)g_hsm.rec_bytes, g_hsm.rec_path);
	}
}
