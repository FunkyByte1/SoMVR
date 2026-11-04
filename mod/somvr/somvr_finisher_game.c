/* somvr_finisher_game.c - SOMVR_FINISHER=on|off (build 33, docs/re-xr-pipeline.md 140, handoff/HANDOFF-combat.md "build 33
 * finisher"): the finisher as the player's own guided motion, first the single neck stab. Included by somvr.c after the
 * kills (it uses SOMVR_EXECUTE's ready and spend, SOMVR_KILLS' half and death, the stealth kill's body nodes and blood,
 * SOMVR_SWORD_PHYS's drawn sword, the layer's arrow slot); the pure part is somvr_finisher.c.
 *
 * Ready = SOMVR_EXECUTE's (the hit counter's charge) and an orc in reach in front (within FN_REACH_CM of Talion's root,
 * within FN_VIEW_DEG of the head's forward, facing him within FN_FACE_DEG; not a captain, a beast, in a sync, dead). Then
 * the world goes slow (SlowMO.Set: the game timer - the world and Talion; the tracked view and the hands stay real time)
 * and the layer draws a blue outline of the sword (the arrow's slot, somvr_vh_outline): its point start_cm short of the
 * orc's Neck node, pointing into it from Talion's side. The drawn sword lined up with it (somvr_fn_aligned) snaps onto it
 * (SOMVR_SWORD_PHYS's drawn pose replaced, the controller's own hand shown as the wraith ghost) and rides the stab's path
 * (into the neck depth_cm, back out) at the speed the hand earns (somvr_fn_step). The way in: the orc's half starts
 * (SOMVR_KILLS: the row SOMVR_FINISHER_ROW, its AI held, the charge spent as the stock execution spends it), the game's
 * blood at the wound along the blade, a thud; the way out: the blood again, a beat. At the end the slow motion goes, the
 * sword is the hand's again; the orc's half runs on to the ground and the game's DealHit (Talion's Combat: XP, credit,
 * Nemesis) kills it from that pose (SOMVR_KILLS' death rules). Cancels (the slow motion off, nothing spent): the orc gone,
 * dead or out of reach, the charge gone, SOMVR_FINISHER_TIMEOUT_S without a snap, or 4 s stalled in the stab.
 * Replaces SOMVR_KILLS_EXEC's execution by the blow (and the trigger's) while on.
 * Test words (SOMVR_HANDS_TEST): `finisher status|on|off|pose` (pose: the outline's grip as a `hands test R` pose, logged).
 * Lines: `CONFIG finisher ...`, `FINISHER ...`, `FINISHER END`. */

#define FN_REACH_CM 230.0f
#define FN_VIEW_DEG 40.0f
#define FN_FACE_DEG 70.0f
#define FN_LOST_S 0.5        /* a finisher with no full frame (the hand tracked) this long ends */
#define FN_FOLLOW_CM 35.0f /* the stab's path follows the orc's neck by at most this */
#define FN_ARM_CM 70.0f  /* the outline's grip at most this far out from the eyes (an arm's reach: else not ready yet) */
#define FN_BLADE_CM 95.0f
/* the stab's natural grip (cm right, up, ahead of the eyes): the outline's guard put on the sphere of the blade's reach
 * around the neck nearest it - an orc at the fight's ~95 cm gets a stab down from the shoulder, one farther a thrust
 * forward and down (the orc never moved: boots 122204-152601, a slid orc walked back to its fight distance) */
#define FN_PREF_R 15.0f
#define FN_PREF_U -30.0f
#define FN_PREF_A 35.0f
enum { FN_IDLE = 0, FN_READY, FN_STAB, FN_END };
static const char *const g_fn_ph_w[] = { "idle", "ready", "stab", "end" };
static struct {
	int on, ok;
	somvr_fn_cfg cfg;
	float slowmo, timeout_s;
	char row[96];
	int phase;
	uintptr_t c, hobj;
	double t0, snap_t, last_move_t, smo_on_t;
	float neck[3], dir[3], start[3]; /* world cm: the neck point, the way in (unit), the outline's point */
	float p, depth_max;
	int in_done, out_done, deep_done, pose_done;
	float follow_cm; /* the neck's most from where the path was laid, this stab */
	double cool_t;                        /* no ready again before this (after a cancel) */
	double seen_t;                        /* the last full finisher frame with the sword hand tracked */
	float hv[3], hlast[3];                /* the hand's own blade: its point's velocity against the rig (cm/s), smoothed */
	double hlast_t;
	int hlast_ok;
	/* the drawn sword's override (the arms thread reads these): the grip to the point in the sword's frame */
	_Atomic int over;
	int snap_req, have_off;
	float off_tip[3], qnew[4], L;    /* grips' space: the point's offset (sword frame, cm), the snapped turn */
	float tip_g[3], dir_g[3];        /* grips' space: the guided point now, the way in (unit) */
	float last_tip_w[3], last_base_w[3];
	/* the smo */
	uint32_t smo_num, smo_den, smo_set[2];
	int smo;
	/* the outline for the layer */
	_Atomic int show;
	float ogrip_g[3], oq_g[4];
	_Atomic int req_mode, req_status, req_pose;
	_Atomic uintptr_t hold_c, hold_h; /* the orc held from ready to the way in (its own attack starts, its steps) */
	_Atomic uint64_t aimed;           /* its steps undone (kl_aim's calls) */
	uint64_t readies, snaps, stabs, kills, cancels, frames_ready, frames_stab, ins, outs;
	float match_sum;
	uint64_t match_n;
} g_fin;

/* the world <-> the grips' space (cm; SOMVR_ARMS' rig: world = base + R(yaw) k (p - p0)) */
static void fn_w2g(const float w[3], float g[3])
{
	const sc_rigxf *r = &g_arm.rig;
	float k = sc_rig_scale(r);
	sv_vec3 d = sv_qrot(sv_qyaw(-r->yaw), (sv_vec3){ w[0] - r->base[0], w[1] - r->base[1], w[2] - r->base[2] });
	g[0] = r->p0[0] + d.x / k, g[1] = r->p0[1] + d.y / k, g[2] = r->p0[2] + d.z / k;
}
static void fn_w2g_dir(const float w[3], float g[3])
{
	sv_vec3 d = sv_qrot(sv_qyaw(-g_arm.rig.yaw), (sv_vec3){ w[0], w[1], w[2] });
	g[0] = d.x, g[1] = d.y, g[2] = d.z;
}

static void fn_slowmo(int on, double t)
{
	uintptr_t tp = g_fig.timer_pp ? peek_ptr(g_fig.timer_pp) : 0, tm = tp ? peek_ptr(tp + 8) : 0;
	if (!g_xc.ok_slowmo)
		return;
	if (on && !g_fin.smo) {
		if (!tm)
			return; /* (not set: nothing to put back) */
		uint32_t nd[2] = { 0, 0 };
		if (!peek(tm + FI_TM_NUM, nd, sizeof nd) || nd[0] != nd[1] || !nd[0])
			return; /* the game's own slow motion runs: not ours to change */
		char a[32];
		snprintf(a, sizeof a, "%.3f", (double)g_fin.slowmo);
		const char *argv[1] = { a };
		FI_FN(FIS_SLOWMO, fi_slowmo_fn)(1, argv);
		peek(tm + FI_TM_NUM, g_fin.smo_set, sizeof g_fin.smo_set);
		g_fin.smo_num = nd[0], g_fin.smo_den = nd[1], g_fin.smo = 1, g_fin.smo_on_t = t;
	} else if (!on && g_fin.smo) {
		/* put back what was found, whatever the timer reads now (review b34: an unreadable timer left the world slow);
		 * only a scale the game set since (its own slow motion, read) is left to it */
		g_fin.smo = 0;
		uint32_t cur[2] = { 0, 0 };
		int rd = tm && peek(tm + FI_TM_NUM, cur, sizeof cur);
		if (rd && (cur[0] != g_fin.smo_set[0] || cur[1] != g_fin.smo_set[1])) {
			logf_("FINISHER t=%.3f the slow motion left to the game (its own since: %u/%u)", t, cur[0], cur[1]);
			return;
		}
		char a[2][16];
		snprintf(a[0], sizeof a[0], "%u", g_fin.smo_num ? g_fin.smo_num : 1u);
		snprintf(a[1], sizeof a[1], "%u", g_fin.smo_den ? g_fin.smo_den : 1u);
		const char *argv[2] = { a[0], a[1] };
		FI_FN(FIS_SLOWMO, fi_slowmo_fn)(2, argv);
		if (!rd)
			logf_("FINISHER t=%.3f the slow motion put back to %s/%s (the timer unreadable)", t, a[0], a[1]);
	}
}

static int fn_busy(uintptr_t c) { return g_fin.on && g_fin.phase >= FN_READY && c && c == g_fin.c; }
static int fn_on(void) { return g_fin.on && g_fin.ok; }

/* the candidate orc: in reach, in front of the head, facing Talion */
static int fn_pick(const struct cb_orc *o, int n, const float root[3], const float head_fwd[3], struct cb_orc *out)
{
	float best = 1e9f;
	int k = -1;
	for (int i = 0; i < n; i++) {
		float dx = o[i].pos[0] - root[0], dz = o[i].pos[2] - root[2], d = hypotf(dx, dz);
		if (!(d < FN_REACH_CM) || cb_dead(o[i].c) || cb_sync(o[i].c) || kp_of_combat(o[i].c) || kl_is_beast(o[i].c, o[i].hobj) ||
		    kl_of(o[i].c))
			continue;
		float fl = hypotf(head_fwd[0], head_fwd[2]);
		float cv = fl > 1e-3f && d > 1 ? (dx * head_fwd[0] + dz * head_fwd[2]) / (d * fl) : -1;
		if (cv < cosf(FN_VIEW_DEG * 0.01745329f))
			continue;
		float yaw = kl_yaw(o[i].hobj), off = fabsf(sv_wrap_pi(atan2f(-dx, -dz) - yaw)) * 57.29578f;
		if (off > FN_FACE_DEG)
			continue;
		if (d < best)
			best = d, k = i;
	}
	if (k < 0)
		return 0;
	*out = o[k];
	return 1;
}

/* build 36 (docs/re-xr-pipeline.md 164): the finisher owns the charged kill. A killing blow on the orc it would take now
 * (its phase idle, the charge ready - this blow's own hit counted - and that orc fn_pick's: in reach, in front of the
 * head, facing Talion) leaves the orc at the floor's health (SOMVR_KILLS' 2) instead of killing it, so the finisher's
 * ready comes on it next frame (the user's 11-03 mark: "I can't even try a finisher because I can't get them to a point
 * where I can even try it"). Once per orc in 5 s: a ready that does not come never keeps an orc from dying. 1 = spared */
static int fn_spares(uintptr_t pc, const struct cb_orc *o, double t)
{
	static uintptr_t last_h;
	static double last_t;
	if (!fn_on() || g_fin.phase != FN_IDLE || t < g_fin.cool_t || !pc || !o)
		return 0;
	somvr_ex_hc h;
	if (!xc_read_hc(pc, &h) || !somvr_ex_ready(&h))
		return 0;
	uintptr_t th = peek_ptr(pc + C_HOBJ);
	float root[3];
	if (!th || !peek(th + OBJ_POS, root, sizeof root) || !vec_ok(root, 1e7f))
		return 0;
	const somvr_posef *e = g_fr.views.pose; /* the head's forward in the world, as fn_frame reads it */
	sv_vec3 fx = sv_qrot(sv_qnormalize(sv_quat_of(e[0].q)), (sv_vec3){ 0, 0, -1 });
	sv_vec3 hw = sv_qrot(sv_qyaw(g_arm.rig.yaw), (sv_vec3){ fx.x, fx.y, -fx.z });
	float hfwd[3] = { hw.x, hw.y, hw.z };
	struct cb_orc oc;
	if (!fn_pick(o, 1, root, hfwd, &oc) || (last_h == o->hobj && t - last_t < 5.0))
		return 0;
	last_h = o->hobj, last_t = t;
	g_kl.spared++;
	logf_("FINISHER t=%.3f the killing blow on orc %#llx spared: the charge ready (the streak %d, the charge %d of %d) and the "
	      "orc the finisher's (%.0f cm, in front, facing Talion) - left at %.1f health, its ready next (the finisher owns the "
	      "charged kill)", t, (unsigned long long)o->c, h.streak, h.charge, h.need, hypotf(o->pos[0] - root[0], o->pos[2] - root[2]),
	      cb_health(o->c));
	return 1;
}

/* the grips' space -> the world (cm) */
static void fn_g2w(const float g[3], float w[3])
{
	const sc_rigxf *r = &g_arm.rig;
	float k = sc_rig_scale(r);
	sv_vec3 d = sv_qrot(sv_qyaw(r->yaw), (sv_vec3){ k * (g[0] - r->p0[0]), k * (g[1] - r->p0[1]), k * (g[2] - r->p0[2]) });
	w[0] = r->base[0] + d.x, w[1] = r->base[1] + d.y, w[2] = r->base[2] + d.z;
}
/* `hands test`'s frame (cm right, up, ahead of the eyes in the head's yaw) -> the world (cm) */
static void fn_h2w(const float h[3], float w[3])
{
	const somvr_posef *ev = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(ev[0].q));
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 });
	float yaw = atan2f(-f.x, -f.z);
	sv_quat qy = { 0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	sv_vec3 o = sv_qrot(qy, (sv_vec3){ 0.01f * h[0], 0.01f * h[1], -0.01f * h[2] });
	float g[3] = { 100.0f * (0.5f * (ev[0].p[0] + ev[1].p[0]) + o.x), 100.0f * (0.5f * (ev[0].p[1] + ev[1].p[1]) + o.y),
		       -100.0f * (0.5f * (ev[0].p[2] + ev[1].p[2]) + o.z) };
	fn_g2w(g, w);
}

/* the outline from the neck: the point start_cm short of it on the line from the player's shoulder height, into the neck */
static void fn_outline_pose(void);
static int fn_outline(const struct cb_orc *o, const float root[3])
{
	float nd[4][3];
	if (!stl_body(o, nd))
		return 0;
	memcpy(g_fin.neck, nd[2], sizeof g_fin.neck);
	/* the guard on the blade's reach around the neck (the blade and the start short of it), nearest the natural grip:
	 * the way in from there into the neck (boot 115139: a fighting infantry's Neck node 85 cm under the eyes) */
	float pref[3], ph[3] = { FN_PREF_R, FN_PREF_U, FN_PREF_A };
	(void)root;
	fn_h2w(ph, pref);
	float d[3] = { g_fin.neck[0] - pref[0], g_fin.neck[1] - pref[1], g_fin.neck[2] - pref[2] }, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (!(l > 5.0f))
		return 0;
	for (int i = 0; i < 3; i++)
		g_fin.dir[i] = d[i] / l, g_fin.start[i] = g_fin.neck[i] - g_fin.dir[i] * g_fin.cfg.start_cm;
	fn_outline_pose();
	return 1;
}
/* the layer's pose of the outline (the grips' space) from its line in the world - every frame while it shows: the line
 * stays put in the world when the rig turns (boot 154546: a turn after the ready swung the outline off the neck) */
static void fn_outline_pose(void)
{
	/* the grip FN_BLADE_CM back from the point along the blade, its +Z (game) along it */
	float gw[3] = { g_fin.start[0] - g_fin.dir[0] * FN_BLADE_CM, g_fin.start[1] - g_fin.dir[1] * FN_BLADE_CM,
			g_fin.start[2] - g_fin.dir[2] * FN_BLADE_CM }, dg[3];
	fn_w2g(gw, g_fin.ogrip_g), fn_w2g_dir(g_fin.dir, dg);
	sv_quat q = somvr_bow_look((sv_vec3){ dg[0], dg[1], dg[2] }, 0);
	g_fin.oq_g[0] = q.x, g_fin.oq_g[1] = q.y, g_fin.oq_g[2] = q.z, g_fin.oq_g[3] = q.w;
}

static void fn_euler(sv_quat q, float ang[3]);
/* the outline's grip against the eyes (the head's yaw frame: cm right, up, ahead), its turn as `hands test`'s angles
 * (deg, R = Rx Ry Rz relative to the head's yaw) and the way in (right, up, ahead) */
static void fn_rel(float rel_cm[3], float ang[3], float way[3])
{
	const somvr_posef *ev = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(ev[0].q));
	float hp[3] = { 0.5f * (ev[0].p[0] + ev[1].p[0]), 0.5f * (ev[0].p[1] + ev[1].p[1]), 0.5f * (ev[0].p[2] + ev[1].p[2]) };
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 });
	float yaw = atan2f(-f.x, -f.z);
	sv_quat qyi = { 0, -sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	somvr_posef xp;
	sa_xf xg = { { g_fin.ogrip_g[0], g_fin.ogrip_g[1], g_fin.ogrip_g[2] }, { g_fin.oq_g[0], g_fin.oq_g[1], g_fin.oq_g[2], g_fin.oq_g[3] } };
	vh_to_xr(&xg, &xp);
	sv_vec3 rel = sv_qrot(qyi, (sv_vec3){ xp.p[0] - hp[0], xp.p[1] - hp[1], xp.p[2] - hp[2] });
	rel_cm[0] = rel.x * 100, rel_cm[1] = rel.y * 100, rel_cm[2] = -rel.z * 100;
	sv_vec3 dw0 = sv_qrot(sv_qyaw(-g_arm.rig.yaw), (sv_vec3){ g_fin.dir[0], g_fin.dir[1], g_fin.dir[2] });
	sv_vec3 dw = sv_qrot(qyi, (sv_vec3){ dw0.x, dw0.y, -dw0.z });
	way[0] = dw.x, way[1] = dw.y, way[2] = -dw.z;
	fn_euler(sv_qmul(qyi, sv_quat_of(xp.q)), ang);
}
/* `hands test`'s angles of a turn q (the head's yaw frame): q = Rx(a) Ry(b) Rz(c) */
static void fn_euler(sv_quat q, float ang[3])
{
	q = sv_qnormalize(q);
	float xx = q.x, yy = q.y, zz = q.z, ww = q.w;
	float m00 = 1 - 2 * (yy * yy + zz * zz), m01 = 2 * (xx * yy - zz * ww), m02 = 2 * (xx * zz + yy * ww);
	float m12 = 2 * (yy * zz - xx * ww), m22 = 1 - 2 * (xx * xx + yy * yy);
	ang[0] = atan2f(-m12, m22) * 57.29578f, ang[1] = asinf(m02 > 1 ? 1 : m02 < -1 ? -1 : m02) * 57.29578f;
	ang[2] = atan2f(-m01, m00) * 57.29578f;
}
/* the shortest turn taking the unit a onto the unit b */
static sv_quat fn_qalign(sv_vec3 a, sv_vec3 b)
{
	sv_vec3 ax = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	float s = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z), c = a.x * b.x + a.y * b.y + a.z * b.z, ang = atan2f(s, c);
	if (s < 1e-6f)
		return (sv_quat){ 0, 0, 0, 1 };
	float h = sinf(0.5f * ang) / s;
	return (sv_quat){ ax.x * h, ax.y * h, ax.z * h, cosf(0.5f * ang) };
}
/* the drivers' line-up (SOMVR_HANDS_TEST): the right test hand's pose that lays the drawn sword on the outline - the
 * current test pose turned by the turn that takes the drawn blade onto the way, moved so its point lands on the
 * outline's (the head's yaw frame: cm right, up, ahead; `hands test`'s angles). 0 = no test hand */
static int fn_lineup(const somvr_blade *b, float out[6])
{
	if (!g_bd.test || !g_bd.t_on[1])
		return 0;
	const somvr_posef *ev = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(ev[0].q));
	float hp[3] = { 0.5f * (ev[0].p[0] + ev[1].p[0]), 0.5f * (ev[0].p[1] + ev[1].p[1]), 0.5f * (ev[0].p[2] + ev[1].p[2]) };
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 });
	float yaw = atan2f(-f.x, -f.z);
	sv_quat qyi = { 0, -sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	float g[3];
	fn_w2g(b->tip, g);
	sv_vec3 tip = sv_qrot(qyi, (sv_vec3){ 0.01f * g[0] - hp[0], 0.01f * g[1] - hp[1], -0.01f * g[2] - hp[2] });
	fn_w2g(g_fin.start, g);
	sv_vec3 st = sv_qrot(qyi, (sv_vec3){ 0.01f * g[0] - hp[0], 0.01f * g[1] - hp[1], -0.01f * g[2] - hp[2] });
	float aw[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] }, ag[3], dg[3];
	fn_w2g_dir(aw, ag), fn_w2g_dir(g_fin.dir, dg);
	sv_vec3 ax = sv_qrot(qyi, (sv_vec3){ ag[0], ag[1], -ag[2] }), dw = sv_qrot(qyi, (sv_vec3){ dg[0], dg[1], -dg[2] });
	float la = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z), ld = sqrtf(dw.x * dw.x + dw.y * dw.y + dw.z * dw.z);
	if (!(la > 1e-3f && ld > 1e-3f))
		return 0;
	ax.x /= la, ax.y /= la, ax.z /= la, dw.x /= ld, dw.y /= ld, dw.z /= ld;
	const float *v = g_bd.t_v[1];
	sv_vec3 gp = { 0.01f * v[0], 0.01f * v[1], -0.01f * v[2] };
	sv_quat qa = fn_qalign(ax, dw), qn = sv_qmul(qa, sa_xf_euler((float[6]){ 0, 0, 0, v[3], v[4], v[5] }).q);
	sv_vec3 r = sv_qrot(qa, (sv_vec3){ tip.x - gp.x, tip.y - gp.y, tip.z - gp.z });
	out[0] = (st.x - r.x) * 100, out[1] = (st.y - r.y) * 100, out[2] = -(st.z - r.z) * 100;
	fn_euler(qn, out + 3);
	return 1;
}

/* the outline's grip in an arm's reach of the eyes (a real arm: FN_ARM_CM out, from 10 cm behind the eyes (a hand by the
 * ear, a stab down) to anywhere ahead, 25 cm over them to 85 cm under) */
static int fn_reachable(float slack_cm)
{
	float rel[3], ang[3], way[3];
	fn_rel(rel, ang, way);
	return hypotf(rel[0], rel[2]) <= FN_ARM_CM + slack_cm && rel[2] > -10.0f - slack_cm && rel[1] < 25.0f + slack_cm &&
	       rel[1] > -85.0f - slack_cm;
}

/* kl_aim (the movement's thread, after the game's step): the finisher's orc held where it stands from ready to the way
 * in (its own step undone; the height the game's); 1 = ours (not held by the fight space meanwhile) */
static int fn_aim(uintptr_t h, const float was[3], float *xf)
{
	if (!h || h != atomic_load(&g_fin.hold_h))
		return 0;
	if (was && vec_ok(was, 1e7f))
		xf[0] = was[0], xf[2] = was[2], atomic_fetch_add(&g_fin.aimed, 1);
	return 1;
}
static int fn_held(uintptr_t c, uintptr_t h)
{
	return (c && c == atomic_load(&g_fin.hold_c)) || (h && h == atomic_load(&g_fin.hold_h));
}
static void fn_unhold(void) { atomic_store(&g_fin.hold_h, 0), atomic_store(&g_fin.hold_c, 0); }

/* vh_arm (after the bow's arrow): the outline in the arrow's slot while ready */
static void fn_vh(somvr_vrhands_frame *e)
{
	if (!atomic_load(&g_fin.show) || e->arrow_len > 0.05f)
		return;
	sa_xf x = { { g_fin.ogrip_g[0], g_fin.ogrip_g[1], g_fin.ogrip_g[2] }, { g_fin.oq_g[0], g_fin.oq_g[1], g_fin.oq_g[2], g_fin.oq_g[3] } };
	if (!isfinite(x.p.x + x.p.y + x.p.z + x.q.w))
		return;
	vh_to_xr(&x, &e->arrow);
	/* (glow >= 2: the layer's outline; 2.2 .. 2.8 a slow pulse, 1.25 Hz: alpha 0.35 .. 0.50) */
	e->arrow_len = (FN_BLADE_CM + 6.0f) * 0.01f, e->arrow_glow = 2.5f + 0.3f * sinf((float)(mono() * 7.854));
}

/* SOMVR_SWORD_PHYS's arms frame, before the weapon node is set: the snapped sword rides the path. op (m) and qs (x y z w)
 * the drawn sword's grip pose in the grips' space; 1 = replaced */
static int fn_sword(float op[3], float qs[4])
{
	if (!atomic_load(&g_fin.over))
		return 0;
	if (g_fin.snap_req) { /* once: the point's offset in the sword's frame, the turn that lays the blade on the way */
		g_fin.snap_req = 0;
		float tg[3], bg[3], dnow[3];
		fn_w2g(g_fin.last_tip_w, tg), fn_w2g(g_fin.last_base_w, bg);
		for (int i = 0; i < 3; i++)
			dnow[i] = tg[i] - bg[i];
		float ln = sqrtf(dnow[0] * dnow[0] + dnow[1] * dnow[1] + dnow[2] * dnow[2]);
		sv_quat qd = sv_qnormalize(sv_quat_of(qs));
		sv_vec3 rel = { tg[0] - op[0] * 100, tg[1] - op[1] * 100, tg[2] - op[2] * 100 };
		sv_vec3 loc = sv_qrot(sv_qconj(qd), rel);
		g_fin.off_tip[0] = loc.x, g_fin.off_tip[1] = loc.y, g_fin.off_tip[2] = loc.z;
		sv_vec3 a = { dnow[0] / (ln > 1e-3f ? ln : 1), dnow[1] / (ln > 1e-3f ? ln : 1), dnow[2] / (ln > 1e-3f ? ln : 1) };
		sv_vec3 bdir = { g_fin.dir_g[0], g_fin.dir_g[1], g_fin.dir_g[2] };
		sv_vec3 ax = { a.y * bdir.z - a.z * bdir.y, a.z * bdir.x - a.x * bdir.z, a.x * bdir.y - a.y * bdir.x };
		float s = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z), c = a.x * bdir.x + a.y * bdir.y + a.z * bdir.z;
		float ang = atan2f(s, c);
		sv_quat qa = { 0, 0, 0, 1 };
		if (s > 1e-6f)
			qa = (sv_quat){ ax.x / s * sinf(0.5f * ang), ax.y / s * sinf(0.5f * ang), ax.z / s * sinf(0.5f * ang), cosf(0.5f * ang) };
		sv_quat qn = sv_qnormalize(sv_qmul(qa, qd));
		g_fin.qnew[0] = qn.x, g_fin.qnew[1] = qn.y, g_fin.qnew[2] = qn.z, g_fin.qnew[3] = qn.w;
		g_fin.have_off = 1;
	}
	if (!g_fin.have_off)
		return 0;
	sv_quat qn = { g_fin.qnew[0], g_fin.qnew[1], g_fin.qnew[2], g_fin.qnew[3] };
	sv_vec3 off = sv_qrot(qn, (sv_vec3){ g_fin.off_tip[0], g_fin.off_tip[1], g_fin.off_tip[2] });
	op[0] = (g_fin.tip_g[0] - off.x) * 0.01f, op[1] = (g_fin.tip_g[1] - off.y) * 0.01f, op[2] = (g_fin.tip_g[2] - off.z) * 0.01f;
	memcpy(qs, g_fin.qnew, sizeof g_fin.qnew);
	return 1;
}

/* every way out of a finisher: the outline and the sword's ride off, the orc let go, the slow motion put back */
static void fn_stop(double t)
{
	atomic_store(&g_fin.show, 0), atomic_store(&g_fin.over, 0), fn_unhold();
	fn_slowmo(0, t);
	g_fin.phase = FN_IDLE, g_fin.c = 0, g_fin.cool_t = t + 2.0;
}
static void fn_cancel(const char *why, double t)
{
	g_fin.cancels++;
	logf_("FINISHER t=%.3f cancelled (%s) in the %s phase after %.2f s: the slow motion off, %s; the orc's own steps undone "
	      "%llu times while held", t, why, g_fn_ph_w[g_fin.phase], t - g_fin.t0,
	      g_fin.in_done ? "the charge spent (its half goes on)" : "nothing spent", (unsigned long long)atomic_load(&g_fin.aimed));
	fn_stop(t);
}
/* combat_frame's every frame, before anything can skip the finisher (paused, climbing, the VR menu, the sword hand
 * lost): FN_LOST_S without a full finisher frame ends it (review b34: the world stayed slow and the orc held until the
 * hand came back) */
static void fn_watch(double t)
{
	if (g_fin.phase != FN_IDLE && t - g_fin.seen_t > FN_LOST_S)
		fn_cancel("the sword hand away: paused, climbing, the VR menu or not tracked", t);
}
/* the finisher's orc this frame: the same orc (its Combat and its object - the practice's respawn reuses a Combat:
 * review b34) and alive; else it ends, no hit on another; 1 = go on */
static int fn_orc(struct cb_orc *oc, double t)
{
	int same = kl_same(g_fin.c, g_fin.hobj, oc);
	if (same && !cb_dead(g_fin.c))
		return 1;
	if (same && g_fin.phase == FN_STAB && g_fin.in_done) { /* (its half took it: SOMVR_KILLS' death) */
		g_fin.kills++;
		logf_("FINISHER t=%.3f end: the orc dead in its half %.2f s into the stab (p %.2f); the slow motion off, the sword "
		      "the hand's again", t, t - g_fin.snap_t, g_fin.p);
		fn_stop(t);
		return 0;
	}
	fn_cancel(same ? "the orc dead" : "the orc gone or another (its Combat reused)", t);
	return 0;
}

/* the charge spent as the stock execution's start spends it (somvr_execute_game.c's rule) */
static void fn_spend(uintptr_t pc)
{
	uintptr_t hc = peek_ptr(pc + FI_C_HITCTR);
	somvr_ex_hc h0;
	int16_t cto;
	uint8_t uto;
	if (!g_xc.ok_spend || !hc || !xc_read_hc(pc, &h0))
		return;
	int sp = somvr_ex_spend(&h0, &cto, &uto);
	if (sp == 1)
		*(volatile uint8_t *)(hc + XC_HC_USES) = uto;
	else if (sp == 2)
		XC_FN(XCS_CHARGE, xc_charge_fn)(hc, 0);
}

/* combat_frame, with the drawn blade b: the whole finisher */
static void fn_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const somvr_blade *b, const float *root, double t)
{
	(void)pcomb;
	int m = atomic_exchange(&g_fin.req_mode, 0);
	if (m) {
		g_fin.on = m == 2, logf_("FINISHER t=%.3f %s (the word)", t, g_fin.on ? "on" : "off");
		if (!g_fin.on && g_fin.phase != FN_IDLE)
			fn_cancel("switched off", t), g_fin.cool_t = 0;
	}
	if (atomic_exchange(&g_fin.req_status, 0))
		logf_("FINISHER status t=%.3f %s, phase %s: readies %llu, snaps %llu, stabs %llu (in %llu, out %llu), kills %llu, "
		      "cancels %llu; the hand's match %.2f on average", t, g_fin.on ? "on" : "off", g_fn_ph_w[g_fin.phase],
		      (unsigned long long)g_fin.readies, (unsigned long long)g_fin.snaps, (unsigned long long)g_fin.stabs,
		      (unsigned long long)g_fin.ins, (unsigned long long)g_fin.outs, (unsigned long long)g_fin.kills,
		      (unsigned long long)g_fin.cancels, g_fin.match_n ? g_fin.match_sum / (float)g_fin.match_n : 0.0f);
	if (!g_fin.on || !g_fin.ok || !root || !pc)
		return;
	if (g_cb.hand.valid && b->valid)
		g_fin.seen_t = t; /* (fn_watch: none for FN_LOST_S ends it) */
	memcpy(g_fin.last_tip_w, b->tip, 12), memcpy(g_fin.last_base_w, b->base, 12);
	/* the hand's own motion: the controller's blade (g_cb.hand: no history of its own) against its rig's base, smoothed
	 * over ~40 ms (Talion's own move does not count) */
	if (g_cb.hand.valid && isfinite(g_cb.hand_rx.base[0])) {
		float rel[3] = { g_cb.hand.base[0] - g_cb.hand_rx.base[0], g_cb.hand.base[1] - g_cb.hand_rx.base[1],
				 g_cb.hand.base[2] - g_cb.hand_rx.base[2] }; /* (the hilt: a wrist's flick does not drive it) */
		double hdt = t - g_fin.hlast_t;
		if (g_fin.hlast_ok && hdt > 1e-4 && hdt < 0.25) {
			float a = 1.0f - expf(-(float)hdt / 0.04f);
			for (int i = 0; i < 3; i++)
				g_fin.hv[i] += a * ((rel[i] - g_fin.hlast[i]) / (float)hdt - g_fin.hv[i]);
		} else
			memset(g_fin.hv, 0, sizeof g_fin.hv);
		memcpy(g_fin.hlast, rel, sizeof rel), g_fin.hlast_t = t, g_fin.hlast_ok = 1;
	} else
		g_fin.hlast_ok = 0, memset(g_fin.hv, 0, sizeof g_fin.hv);
	struct cb_orc oc;
	const somvr_posef *e = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(e[0].q));
	sv_vec3 fx = sv_qrot(hq, (sv_vec3){ 0, 0, -1 }); /* the head's forward (XR) -> the world's x, z (game: z flipped) */
	float hfwd_g[3] = { fx.x, fx.y, -fx.z }, hfwd[3];
	sv_vec3 hw = sv_qrot(sv_qyaw(g_arm.rig.yaw), (sv_vec3){ hfwd_g[0], hfwd_g[1], hfwd_g[2] });
	hfwd[0] = hw.x, hfwd[1] = hw.y, hfwd[2] = hw.z;
	if (g_fin.phase == FN_IDLE) {
		/* (an attack under way does not stop it: slowed with the world, it lands only if the player is slow - its hit
		 * clears the charge, the game's rule, and that cancels; the orc's half at the way in replaces its attack; boots
		 * 151121: a practice orc's attack in flight at the ready cancelled 2 of 3) */
		if (!g_xc.ready || t < g_fin.cool_t || !fn_pick(o, n, root, hfwd, &oc) || !fn_outline(&oc, root) || !fn_reachable(0))
			return;
		atomic_store(&g_fin.hold_c, oc.c), atomic_store(&g_fin.hold_h, oc.hobj);
		g_fin.phase = FN_READY, g_fin.c = oc.c, g_fin.hobj = oc.hobj, g_fin.t0 = t, g_fin.readies++;
		g_fin.p = 0, g_fin.in_done = g_fin.out_done = g_fin.deep_done = g_fin.pose_done = 0, g_fin.have_off = 0;
		g_fin.follow_cm = 0, g_fin.match_sum = 0, g_fin.match_n = 0;
		atomic_store(&g_fin.show, 1);
		fn_slowmo(1, t);
		haptic(SOMVR_HAND_R, SOMVR_HX_READY); /* (the outline's there: the hand is asked) */
		g_fin.seen_t = t;
		logf_("FINISHER t=%.3f ready: orc %#llx at %.0f cm (health %.1f), the neck at %.0f,%.0f,%.0f; the outline's point %.0f cm "
		      "short of it, the way in %.2f,%.2f,%.2f; the world slow %.2f%s", t, (unsigned long long)oc.c,
		      hypotf(oc.pos[0] - root[0], oc.pos[2] - root[2]), cb_health(oc.c), g_fin.neck[0], g_fin.neck[1], g_fin.neck[2],
		      (double)g_fin.cfg.start_cm, g_fin.dir[0], g_fin.dir[1], g_fin.dir[2], (double)g_fin.slowmo,
		      g_fin.smo ? "" : " (not set: the game's own runs)");
	}
	if (!fn_orc(&oc, t))
		return;
	float dist = hypotf(oc.pos[0] - root[0], oc.pos[2] - root[2]);
	if (g_fin.phase == FN_READY) {
		g_fin.frames_ready++;
		if (!g_xc.ready) {
			fn_cancel("the charge gone", t);
			return;
		}
		/* (the outline stays where it appeared in the world: boot 153522's held orc swayed its neck 20-40 cm through its
		 * slowed attack in 4 s and the outline that followed it ran from the hand) */
		fn_outline_pose();
		int far = dist > FN_REACH_CM + 30 || !fn_reachable(25.0f);
		if (far || t - g_fin.t0 > g_fin.timeout_s) {
			fn_cancel(far ? "out of reach" : "the timeout", t);
			return;
		}
		float ax[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] }, dcm, adeg;
		int al = somvr_fn_aligned(&g_fin.cfg, b->tip, ax, g_fin.start, g_fin.dir, &dcm, &adeg);
		if (atomic_exchange(&g_fin.req_pose, 0) || !g_fin.pose_done) {
			g_fin.pose_done = 1;
			/* the outline's grip as a `hands test R` pose (the head's yaw frame; the drivers' real-hand line-up) */
			float rel[3], ang[3], way[3];
			fn_rel(rel, ang, way);
			logf_("FINISHER pose t=%.3f the outline's grip: hands test R %.1f %.1f %.1f %.1f %.1f %.1f (cm right, up, ahead of the "
			      "eyes; deg); the way in %.3f %.3f %.3f (right, up, ahead); the drawn point %.1f cm and %.1f deg off it", t,
			      rel[0], rel[1], rel[2], ang[0], ang[1], ang[2], way[0], way[1], way[2], dcm, adeg);
			float lu[6];
			if (fn_lineup(b, lu))
				logf_("FINISHER lineup t=%.3f the test hand that lays the drawn sword on it: hands test R %.1f %.1f %.1f %.1f "
				      "%.1f %.1f", t, lu[0], lu[1], lu[2], lu[3], lu[4], lu[5]);
		}
		if (!al)
			return;
		/* the snap */
		g_fin.phase = FN_STAB, g_fin.snap_t = g_fin.last_move_t = t, g_fin.snaps++;
		fn_w2g(g_fin.start, g_fin.tip_g), fn_w2g_dir(g_fin.dir, g_fin.dir_g);
		g_fin.snap_req = 1;
		atomic_store(&g_fin.show, 0), atomic_store(&g_fin.over, 1);
		haptic(SOMVR_HAND_R, SOMVR_HX_TAKE); /* (the sword taken onto the outline) */
		float srel[3], sang[3], sway[3];
		fn_rel(srel, sang, sway);
		logf_("FINISHER t=%.3f snap: the drawn point %.1f cm and the blade %.1f deg off the outline's (within %.0f cm / %.0f "
		      "deg) after %.2f s - the sword rides the stab; the way in %.3f %.3f %.3f (right, up, ahead)", t, dcm, adeg,
		      (double)g_fin.cfg.align_cm, (double)g_fin.cfg.align_deg, t - g_fin.t0, sway[0], sway[1], sway[2]);
		return;
	}
	if (g_fin.phase != FN_STAB)
		return;
	g_fin.frames_stab++;
	static double last_t;
	float dt = (float)(t - last_t);
	last_t = t;
	if (!(dt > 0))
		dt = 0.011f;
	if (dt > 0.1f)
		dt = 0.1f; /* (a slow frame counts, capped: headless runs ~20 fps) */
	float hv[3];
	memcpy(hv, g_fin.hv, sizeof hv); /* the controller's blade: the hand's own motion */
	float rate = 0, match = 0, p0 = g_fin.p;
	g_fin.p = somvr_fn_step(&g_fin.cfg, hv, g_fin.dir, g_fin.p, dt, &rate, &match);
	if (sqrtf(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]) > 15.0f)
		g_fin.match_sum += match, g_fin.match_n++; /* (the hand's match while it moves) */
	if (g_fin.p > p0)
		g_fin.last_move_t = t;
	/* the path rides the orc's neck where it is now (its sway, its half's reel once the blade is in), by at most
	 * FN_FOLLOW_CM (boot 164142: the frozen path's point stood ~1 m from the orc the half had moved) */
	float ndn[4][3], st[3] = { g_fin.start[0], g_fin.start[1], g_fin.start[2] };
	if (stl_body(&oc, ndn)) {
		float off[3] = { ndn[2][0] - g_fin.neck[0], ndn[2][1] - g_fin.neck[1], ndn[2][2] - g_fin.neck[2] };
		float ol = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]), k = ol > FN_FOLLOW_CM ? FN_FOLLOW_CM / ol : 1.0f;
		for (int i = 0; i < 3; i++)
			st[i] += off[i] * k;
		g_fin.follow_cm = fmaxf(g_fin.follow_cm, ol);
	}
	float tip_w[3];
	somvr_fn_point(&g_fin.cfg, st, g_fin.dir, g_fin.p, tip_w);
	fn_w2g(tip_w, g_fin.tip_g);
	float d0 = somvr_fn_depth(&g_fin.cfg, p0), d1 = somvr_fn_depth(&g_fin.cfg, g_fin.p);
	if (!g_fin.in_done && d0 < 0 && d1 >= 0) { /* the way in: the half, the blood, the thud, the charge */
		g_fin.in_done = 1, g_fin.ins++;
		fn_unhold(); /* (SOMVR_KILLS holds it from its half's start) */
		float nd[4][3], *nk = g_fin.neck;
		if (stl_body(&oc, nd) && dist3(nd[2], g_fin.neck) < 40.0f)
			nk = nd[2]; /* (the neck where it is now) */
		float blood_at[3] = { nk[0] - g_fin.dir[0] * 3, nk[1] - g_fin.dir[1] * 3, nk[2] - g_fin.dir[2] * 3 };
		stl_blood(oc.hobj, blood_at, "the finisher's stab going in", t);
		haptic(SOMVR_HAND_R, SOMVR_HX_KILL); /* (the death blow: the way in) */
		fn_spend(pc);
		int row = somvr_kl_find(g_kl_rows, KL_ROWS, g_fin.row);
		float vel[3] = { g_fin.dir[0] * 300, g_fin.dir[1] * 300, g_fin.dir[2] * 300 };
		int took = row >= 0 && kl_take_row(pc, &oc, 0, kl_srec(row), 1, g_fin.neck, vel, g_fin.dir, SOMVR_KL_FIGHT, row,
						   "the finisher", t);
		logf_("FINISHER t=%.3f in: the point at the neck after %.2f s of the stab (the hand's match %.2f, the blade %.0f cm/s): "
		      "blood, the charge spent; the orc's half '%s' %s", t, t - g_fin.snap_t, match, rate, g_fin.row,
		      took ? "started (SOMVR_KILLS: its AI held, the death at its moment)" : "NOT started - its death at the out");
	}
	if (!g_fin.deep_done && g_fin.p >= 1.0f) {
		g_fin.deep_done = 1;
		haptic_k(SOMVR_HAND_R, SOMVR_HX_HIT, 0.5f); /* (the deepest: a stab's thump) */
		logf_("FINISHER t=%.3f deepest: %.0f cm into the neck after %.2f s; out now", t, (double)g_fin.cfg.depth_cm, t - g_fin.snap_t);
	}
	if (g_fin.in_done && !g_fin.out_done && d0 >= 0 && d1 < 0) { /* the way out: the blood again */
		g_fin.out_done = 1, g_fin.outs++;
		float blood_at[3] = { g_fin.neck[0] - g_fin.dir[0] * 4, g_fin.neck[1] - g_fin.dir[1] * 4, g_fin.neck[2] - g_fin.dir[2] * 4 };
		stl_blood(oc.hobj, blood_at, "the finisher's blade coming out", t);
		haptic_k(SOMVR_HAND_R, SOMVR_HX_TEAR, 0.2f); /* (the blade drawn out) */
		logf_("FINISHER t=%.3f out: the blade out of the neck after %.2f s of the stab", t, t - g_fin.snap_t);
	}
	if (g_fin.p >= 2.0f || t - g_fin.last_move_t > 4.0) {
		int done = g_fin.p >= 2.0f;
		g_fin.stabs += done;
		if (!g_fin.in_done) { /* stalled before the neck: a cancel */
			fn_cancel("the stab stalled 4 s before the neck", t);
			return;
		}
		if (!kl_of(g_fin.c) && !cb_dead(g_fin.c)) { /* its half did not start: the game's kill now */
			struct kl_one k;
			memset(&k, 0, sizeof k);
			k.c = g_fin.c, k.hobj = g_fin.hobj, k.fk = 1, k.drec = stl_rec(), k.row = -1;
			memcpy(k.dir, g_fin.dir, sizeof k.dir), memcpy(k.vel, g_fin.dir, sizeof k.vel);
			snprintf(k.vname, sizeof k.vname, "-"), snprintf(k.who, sizeof k.who, "the finisher");
			kl_deal(pc, &k, "the finisher's end", t);
		}
		g_fin.kills++;
		logf_("FINISHER t=%.3f end: %s after %.2f s of the stab (the hand's match %.2f on average while it moved); the path "
		      "followed the neck up to %.0f cm; the slow motion off, the sword the hand's again; the orc %s", t,
		      done ? "the stab done" : "stalled 4 s after the way in", t - g_fin.snap_t,
		      g_fin.match_n ? g_fin.match_sum / (float)g_fin.match_n : 0.0f, (double)g_fin.follow_cm,
		      kl_of(g_fin.c) ? "plays its half to the ground" : cb_dead(g_fin.c) ? "dead" : "alive (!)");
		fn_stop(t);
	}
}

static const char *fn_word(const char *w)
{
	if (!strcmp(w, "status"))
		return atomic_store(&g_fin.req_status, 1), "finisher status";
	if (!strcmp(w, "on") || !strcmp(w, "off"))
		return atomic_store(&g_fin.req_mode, w[1] == 'n' ? 2 : 1), w[1] == 'n' ? "finisher on" : "finisher off";
	if (!strcmp(w, "pose"))
		return atomic_store(&g_fin.req_pose, 1), "finisher pose (the outline's grip as a hands-test pose)";
	return NULL;
}

static void fn_start(int *bad)
{
	memset(&g_fin, 0, sizeof g_fin);
	static const char *const w2[] = { "off", "on" };
	g_fin.on = env_word("SOMVR_FINISHER", w2, 2, 0);
	somvr_fn_defaults(&g_fin.cfg);
	g_fin.slowmo = env_float("SOMVR_FINISHER_SLOWMO", 0.15f, 0.02f, 1, bad);
	g_fin.timeout_s = env_float("SOMVR_FINISHER_TIMEOUT_S", 6, 1, 30, bad);
	g_fin.cfg.align_cm = env_float("SOMVR_FINISHER_ALIGN_CM", g_fin.cfg.align_cm, 2, 40, bad);
	g_fin.cfg.align_deg = env_float("SOMVR_FINISHER_ALIGN_DEG", g_fin.cfg.align_deg, 5, 60, bad);
	g_fin.cfg.depth_cm = env_float("SOMVR_FINISHER_DEPTH_CM", g_fin.cfg.depth_cm, 5, 50, bad);
	g_fin.cfg.v_max = env_float("SOMVR_FINISHER_VMAX", g_fin.cfg.v_max, 20, 500, bad);
	g_fin.cfg.v_ref = env_float("SOMVR_FINISHER_VREF", g_fin.cfg.v_ref, 20, 500, bad);
	const char *r = getenv("SOMVR_FINISHER_ROW");
	snprintf(g_fin.row, sizeof g_fin.row, "%s", r && *r ? r : "AI_Bip_PC_Standing_InstaKill_F6");
	g_fin.ok = g_kl.ok && g_xc.mode && g_swp.on;
	logf_("CONFIG finisher=%s (SOMVR_FINISHER): the neck stab - when the execution is ready and an orc in reach faces Talion, "
	      "the world at %.2f, a blue sword outline %.0f cm short of its neck; the drawn sword within %.0f cm / %.0f deg snaps on "
	      "and rides the stab (%.0f cm in and out, at most %.0f cm/s, the hand's speed against %.0f cm/s x its accuracy); the "
	      "orc's half '%s' from the way in; %.0f s to snap%s", g_fin.on ? "on" : "off", (double)g_fin.slowmo,
	      (double)g_fin.cfg.start_cm, (double)g_fin.cfg.align_cm, (double)g_fin.cfg.align_deg, (double)g_fin.cfg.depth_cm,
	      (double)g_fin.cfg.v_max, (double)g_fin.cfg.v_ref, g_fin.row, (double)g_fin.timeout_s,
	      g_fin.ok ? "" : " - OFF (needs SOMVR_KILLS' sites, SOMVR_EXECUTE and SOMVR_SWORD_PHYS)");
}
static void fn_end_log(void)
{
	logf_("FINISHER END %s: readies %llu, snaps %llu, stabs %llu (in %llu, out %llu), kills %llu, cancels %llu; frames ready "
	      "%llu, stabbing %llu", g_fin.on ? "on" : "off", (unsigned long long)g_fin.readies, (unsigned long long)g_fin.snaps,
	      (unsigned long long)g_fin.stabs, (unsigned long long)g_fin.ins, (unsigned long long)g_fin.outs,
	      (unsigned long long)g_fin.kills, (unsigned long long)g_fin.cancels, (unsigned long long)g_fin.frames_ready,
	      (unsigned long long)g_fin.frames_stab);
}
