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
 * Lines: `CONFIG finisher ...`, `FINISHER ...`, `FINISHER END`.
 *
 * Build 37 (docs/re-xr-pipeline.md 171, som-b37-finisher2; the user's 11-04 try: the snap went unnoticed, the pull back
 * crawled, the orc died 10 s later, some of the flat kill's sounds missing, no word why nothing came):
 *   SOMVR_FINISHER_FEEL=b33|b37 (code default b33) - b37: the snap is felt, heard and seen (CLANK 1 on the sword hand,
 *     the lock sound SOMVR_FINISHER_LOCK_SOUND, the outline's flash FN_FLASH_S, the blade pulled onto the line over
 *     FN_PULL_S, not put there); the way in stays guided but a pull back draws the blade back with the hand; the orc
 *     dies the moment the point is full depth, or where the hand turns back once it is FN_TURN_CM in (SOMVR_KILLS' kl_instant: the ragdoll from the blade, the execution's
 *     record, the charge spent then; no paired half), with the blood along the stab and the flat kill's sounds; the way
 *     out follows the hand along the wound's axis (somvr_fn_out_p: 0.9 of its pull past 1.5 cm), the blade let go to
 *     the hand (blended back over FN_PULL_S) the moment it is out, after FN_OUT_WAIT_S without a pull, or with the hand
 *     FN_OUT_FREE_CM off the line; the slow motion off then.
 *   SOMVR_FINISHER_SOUND=on|off (code default off; b37's env on): the flat F6 kill's own sounds (Talion's half, which
 *     VR never plays: pl_syncaction_chordkill_stand.anix's keys) posted at the blade - the way in cbt_insta_stab, the
 *     kill cbt_instakill_st_f_03 + cbt_insta_slice + Talion's vx_PlayerSyncPre_snd + the orc's vx_EnemyDeath, the way
 *     out cbt_sync_stab_dry; each post's playing id logged.
 *   SOMVR_FINISHER_HINT=on|off (code default off) - the execution ready FN_HINT_AFTER_S with no orc to finish: the
 *     nearest that could be (within FN_HINT_CM) gets the objective layer's finisher ring with "FACE TO FACE / IN
 *     REACH" (SOMVR_OBF_FINISH; out of view the layer's edge arrow toward him), the reason logged (somvr_fn_why). */

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
/* build 37 (re-xr-pipeline.md 171) */
#define FN_PULL_S 0.12      /* the snap's pull of the blade onto the line; the release's blend back to the hand */
#define FN_FLASH_S 0.15     /* the outline's flash at the snap, then gone (SOMVR_FINISHER_FLASH_S: a test's longer one) */
#define FN_OUT_WAIT_S 1.5   /* after the kill no pull this long: the blade let go to the hand (never stuck) */
#define FN_OUT_FREE_CM 30.0f /* the hand this far off the wound's line after the kill: the blade let go */
#define FN_HINT_CM 2000.0f  /* the hint's orcs within this of Talion */
#define FN_HINT_AFTER_S 0.5 /* the execution ready this long with no orc to finish: the hint */
#define FN_TURN_CM 6.0f     /* the hand turning back with the point this deep in the neck: the kill there */
#define FN_BACK_S 2.0       /* drawn back out with no kill: this long to go in again, else it ends (the world at speed) */
#define FN_STAB_S 8.0       /* no kill this long after the snap: it ends (a resting hand drifts: the stall alone never came) */
#define FN_SND_LOCK "cbt_sync_block_sword" /* the game's sword-on-sword clank of its paired kills (165 keys in the sets) */
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
	/* build 37 (re-xr-pipeline.md 171) */
	int feel, sound, hint;            /* SOMVR_FINISHER_FEEL=b37, _SOUND, _HINT */
	float flash_s;                    /* SOMVR_FINISHER_FLASH_S (FN_FLASH_S) */
	float follow_vmax;                /* SOMVR_FINISHER_FOLLOW_VMAX: the way in's cap with the hand (cm/s) */
	_Atomic int pull_log;             /* the pull's end to log (the arms thread: its frames, from how far) */
	int pull_frames;
	float pull_from_cm;
	char snd_lock[48];
	double pull_t0, flash_until;      /* the snap's pull (the arms thread blends the hand's sword onto the line), flash */
	_Atomic int rel;                  /* the release's blend back to the hand runs (rel_t0); over stays 1 meanwhile */
	double rel_t0;
	float ride_op[3], ride_q[4];      /* the arms thread's last ridden pose (the release blends from it) */
	int ride_have;
	int killed;                       /* the orc killed at full depth (the way out on its body) */
	double kill_t, dead_t;
	float hdeep[3];                   /* the hand (its hilt against the rig) at the deepest: the way out's zero */
	float s_deep;                     /* the share of the line where the way out starts (1 = full depth; less: the turn) */
	int place_why;                    /* build 37: somvr_fn_place's reason when no start was found (the hint) */
	float place_ext, place_g[3];      /* ... its hand's end from the body, its grip (the head's frame, cm) */
	double back_t;                    /* drawn back out before the kill (FN_BACK_S) */
	int hdeep_ok;
	double out_move_t;
	float out_v;                      /* the blade's speed on the way out (cm/s, smoothed) */
	double ready_t0;                  /* the execution's ready since (the hint's wait) */
	_Atomic int hint_show;            /* the hint's ring this frame (ob_frame: fn_entry) */
	float hint_pos[3];                /* its orc's chest (world cm) */
	double hint_t;
	int hint_why, hint_was;
	uintptr_t hint_c;
	float near_cm;                    /* the nearest live orc this frame (Practice > Charge execution's spawn) */
	double near_t;
	uint64_t hints, hint_frames, pulls, releases, snd_ok, snd_fail;
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
static int fn_hint_on(void) { return g_fin.on && g_fin.ok && g_fin.hint; } /* build 37 (ob_frame) */
static float fn_near_cm(double t) { return g_fin.on && g_fin.ok && t - g_fin.near_t < 1.0 ? g_fin.near_cm : 1e9f; }

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

/* build 37 (Practice > Charge execution's orc): the point cm ahead of the eyes in the head's yaw, at the eyes' height (the
 * spawn's ray finds the ground under it); 0 = no views yet */
static int fn_spot_ahead(float cm, float out[3])
{
	const somvr_posef *ev = g_fr.views.pose;
	if (!isfinite(ev[0].q[3]) || (ev[0].q[0] == 0 && ev[0].q[1] == 0 && ev[0].q[2] == 0 && ev[0].q[3] == 0))
		return 0;
	float h[3] = { 0, 0, cm };
	fn_h2w(h, out);
	return vec_ok(out, 1e7f);
}

/* build 37 (SOMVR_FINISHER_SOUND): a game sound at `at` (world cm) on the threat's Wwise object, facing the eyes; its
 * playing id (0: the event is not in a loaded bank, or no sound sites) logged */
static uint32_t fn_snd(const char *name, const float at[3], const char *what, double t)
{
	if (!g_fin.sound || !name || !name[0] || !at || !vec_ok(at, 1e7f))
		return 0;
	if (!g_th.ok_snd) {
		if (!g_fin.snd_fail++)
			logf_("FINISHER t=%.3f sound '%s' (%s) not posted: the sound sites (SOMVR_THREAT's THS_SND_*) did not check out "
			      "(logged once)", t, name, what);
		return 0;
	}
	float hz[3] = { 0, 0, 0 }, head[3];
	fn_h2w(hz, head);
	uint32_t id = th_snd_post(name, at, head);
	g_fin.snd_ok += id != 0, g_fin.snd_fail += id == 0;
	logf_("FINISHER t=%.3f sound '%s' (%s) at %.0f,%.0f,%.0f: %s %u", t, name, what, (double)at[0], (double)at[1],
	      (double)at[2], id ? "playing id" : "PostEvent gave no playing id (not in a loaded bank?)", id);
	return id;
}

/* the world (cm) -> `hands test`'s frame (cm right, up, ahead of the eyes in the head's yaw): fn_h2w's inverse */
static void fn_w2h(const float w[3], float h[3])
{
	const somvr_posef *ev = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(ev[0].q));
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 });
	float yaw = atan2f(-f.x, -f.z), hp[3] = { 0.5f * (ev[0].p[0] + ev[1].p[0]), 0.5f * (ev[0].p[1] + ev[1].p[1]),
						 0.5f * (ev[0].p[2] + ev[1].p[2]) }, g[3];
	sv_quat qyi = { 0, -sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	fn_w2g(w, g);
	sv_vec3 r = sv_qrot(qyi, (sv_vec3){ 0.01f * g[0] - hp[0], 0.01f * g[1] - hp[1], -0.01f * g[2] - hp[2] });
	h[0] = r.x * 100, h[1] = r.y * 100, h[2] = -r.z * 100;
}

/* the outline from the neck: the point start_cm short of it on the line from the player's shoulder height, into the neck */
static void fn_outline_pose(void);
static int fn_outline(const struct cb_orc *o, const float root[3])
{
	float nd[4][3];
	if (!stl_body(o, nd))
		return 0;
	memcpy(g_fin.neck, nd[2], sizeof g_fin.neck);
	if (g_fin.feel) { /* build 37 (171.6; the user's 22:37 marks): the hand starts within a foot of the body, the stab the
			   * arm's full extension (somvr_fn_place); none: the hint says why (closer / step back) */
		somvr_fn_place_cfg pc;
		somvr_fn_place_defaults(&pc);
		pc.blade = FN_BLADE_CM, pc.depth = g_fin.cfg.depth_cm;
		float nh[3], gh[3], dh[3], gap, ext, gw[3];
		fn_w2h(g_fin.neck, nh);
		if (!somvr_fn_place(&pc, nh, gh, dh, &gap, &ext, &g_fin.place_why))
			return 0;
		fn_h2w(gh, gw);
		float d[3] = { g_fin.neck[0] - gw[0], g_fin.neck[1] - gw[1], g_fin.neck[2] - gw[2] };
		float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (!(l > 5.0f))
			return 0;
		g_fin.cfg.start_cm = l - FN_BLADE_CM; /* (this finisher's gap: the travel = it + the depth) */
		for (int i = 0; i < 3; i++)
			g_fin.dir[i] = d[i] / l, g_fin.start[i] = g_fin.neck[i] - g_fin.dir[i] * g_fin.cfg.start_cm;
		g_fin.place_ext = ext;
		memcpy(g_fin.place_g, gh, sizeof gh);
		fn_outline_pose();
		return 1;
	}
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
	/* (glow >= 2: the layer's outline; 2.2 .. 2.8 a slow pulse, 1.25 Hz: alpha 0.35 .. 0.50; build 37: 3 = the snap's
	 * flash, bright and nearly opaque, for FN_FLASH_S - then gone) */
	e->arrow_len = (FN_BLADE_CM + 6.0f) * 0.01f, e->arrow_glow = 2.5f + 0.3f * sinf((float)(mono() * 7.854));
	if (g_fin.flash_until > 0 && mono() < g_fin.flash_until)
		e->arrow_glow = 3.0f;
	if (g_fin.feel) /* build 37 (171.5; the user's 22:27 mark): the blade alone in the execution-ready glow's look, pulsing
			 * (4.2 .. 4.8); the snap's flash 5 */
		e->arrow_glow = g_fin.flash_until > 0 && mono() < g_fin.flash_until ? 5.0f
				: 4.5f + 0.3f * sinf((float)(mono() * 7.854));
}

/* build 37: a blend of two turns (the shorter way), normalised: s 0 = a, 1 = b */
static void fn_nlerp(const float a[4], const float b[4], float s, float out[4])
{
	float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], k = d < 0 ? -1.0f : 1.0f, l = 0;
	for (int i = 0; i < 4; i++)
		out[i] = (1 - s) * a[i] + s * k * b[i], l += out[i] * out[i];
	l = sqrtf(l);
	for (int i = 0; i < 4; i++)
		out[i] = l > 1e-6f ? out[i] / l : b[i];
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
	if (atomic_load(&g_fin.rel)) { /* build 37: the release - from the last ridden pose back to the hand's sword */
		float s = g_fin.ride_have ? somvr_fn_ease((float)((mono() - g_fin.rel_t0) / FN_PULL_S)) : 1.0f;
		if (s >= 1.0f) {
			atomic_store(&g_fin.rel, 0), atomic_store(&g_fin.over, 0), g_fin.ride_have = 0;
			return 0;
		}
		float q[4];
		for (int i = 0; i < 3; i++)
			op[i] = (1 - s) * g_fin.ride_op[i] + s * op[i];
		fn_nlerp(g_fin.ride_q, qs, s, q);
		memcpy(qs, q, sizeof q);
		return 1;
	}
	sv_quat qn = { g_fin.qnew[0], g_fin.qnew[1], g_fin.qnew[2], g_fin.qnew[3] };
	sv_vec3 off = sv_qrot(qn, (sv_vec3){ g_fin.off_tip[0], g_fin.off_tip[1], g_fin.off_tip[2] });
	float top[3] = { (g_fin.tip_g[0] - off.x) * 0.01f, (g_fin.tip_g[1] - off.y) * 0.01f, (g_fin.tip_g[2] - off.z) * 0.01f };
	/* build 37: the snap pulls the blade onto the line over FN_PULL_S (from the hand's sword to the line), not a jump */
	float s = g_fin.feel ? somvr_fn_ease((float)((mono() - g_fin.pull_t0) / FN_PULL_S)) : 1.0f, q[4];
	if (g_fin.feel && g_fin.pull_from_cm < 0) /* (the pull's start: how far the hand's sword was from the line) */
		g_fin.pull_from_cm = 100.0f * sqrtf((top[0] - op[0]) * (top[0] - op[0]) + (top[1] - op[1]) * (top[1] - op[1]) +
						    (top[2] - op[2]) * (top[2] - op[2]));
	if (g_fin.feel && s < 1.0f)
		g_fin.pull_frames++;
	else if (g_fin.feel && g_fin.pull_frames >= 0)
		atomic_store(&g_fin.pull_log, 1);
	for (int i = 0; i < 3; i++)
		op[i] = (1 - s) * op[i] + s * top[i];
	fn_nlerp(qs, g_fin.qnew, s, q);
	memcpy(qs, q, sizeof q);
	memcpy(g_fin.ride_op, op, sizeof g_fin.ride_op), memcpy(g_fin.ride_q, q, sizeof g_fin.ride_q), g_fin.ride_have = 1;
	return 1;
}

/* every way out of a finisher: the outline and the sword's ride off, the orc let go, the slow motion put back */
static void fn_stop(double t)
{
	atomic_store(&g_fin.show, 0), fn_unhold();
	if (g_fin.feel && atomic_load(&g_fin.over) && g_fin.ride_have) /* build 37: the blade blended back to the hand */
		g_fin.rel_t0 = mono(), atomic_store(&g_fin.rel, 1), g_fin.releases++;
	else
		atomic_store(&g_fin.over, 0);
	fn_slowmo(0, t);
	g_fin.phase = FN_IDLE, g_fin.c = 0, g_fin.cool_t = t + 2.0, g_fin.flash_until = 0;
}
static void fn_cancel(const char *why, double t)
{
	g_fin.cancels++;
	logf_("FINISHER t=%.3f cancelled (%s) in the %s phase after %.2f s: the slow motion off, %s; the orc's own steps undone "
	      "%llu times while held", t, why, g_fn_ph_w[g_fin.phase], t - g_fin.t0,
	      g_fin.in_done && !g_fin.feel ? "the charge spent (its half goes on)" : "nothing spent", /* (b37: spent only at the kill) */ (unsigned long long)atomic_load(&g_fin.aimed));
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
	if (same && (!cb_dead(g_fin.c) || g_fin.killed))
		return 1; /* (build 37: killed at full depth - the way out on its body) */
	if (g_fin.killed) { /* build 37: its body gone under the blade: the blade back to the hand */
		logf_("FINISHER t=%.3f end: the killed orc's body gone %.2f s after the kill; the blade the hand's again", t,
		      t - g_fin.kill_t);
		fn_stop(t);
		return 0;
	}
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

/* build 37 (SOMVR_FINISHER_FEEL=b37): the kill the moment the point is full depth - SOMVR_KILLS' death at the blow (the
 * game's DealHit, forceKill, the ragdoll from the pose it is in, pushed along the stab; the execution's own record for
 * its credit), the charge spent now, the blood along the stab, the flat kill's sounds, the KILL pulse */
static void fn_kill(uintptr_t pc, const struct cb_orc *oc, double t)
{
	g_fin.killed = 1, g_fin.kill_t = t, g_fin.dead_t = 0;
	fn_unhold();
	float nd[4][3], nk[3];
	memcpy(nk, g_fin.neck, sizeof nk);
	if (stl_body(oc, nd) && dist3(nd[2], g_fin.neck) < 40.0f)
		memcpy(nk, nd[2], sizeof nk); /* (the neck where it is now) */
	float hp0 = cb_health(oc->c);
	fn_spend(pc);
	int row = somvr_kl_find(g_kl_rows, KL_ROWS, g_fin.row);
	uintptr_t rec = row >= 0 ? kl_srec(row) : 0;
	float vel[3] = { g_fin.dir[0] * 250, g_fin.dir[1] * 250, g_fin.dir[2] * 250 };
	int dealt = kl_instant(pc, oc, rec, nk, vel, g_fin.dir, "the finisher", t);
	float in_at[3] = { nk[0] - g_fin.dir[0] * 2, nk[1] - g_fin.dir[1] * 2, nk[2] - g_fin.dir[2] * 2 };
	stl_blood_dir(oc->hobj, in_at, g_fin.dir, "the finisher's kill at full depth", t);
	haptic_k(SOMVR_HAND_R, SOMVR_HX_KILL, 1.0f);
	fn_snd("cbt_instakill_st_f_03", nk, "the kill: the flat F6's killing stab", t);
	fn_snd("cbt_insta_slice", nk, "the kill: the blade through", t);
	fn_snd("vx_PlayerSyncPre_snd", nk, "the kill: Talion's effort", t);
	fn_snd("vx_EnemyDeath", nk, "the kill: the orc's death cry", t);
	int dead = cb_dead(oc->c);
	if (dead)
		g_fin.dead_t = t;
	g_fin.kills++;
	logf_("FINISHER t=%.3f kill: the point %s (%.0f cm into the neck) after %.2f s of the stab - the death at once "
	      "(%s; record '%s'): dead %d now, health %.1f -> %.1f; the charge spent; the blood along the stab; the slow motion on "
	      "until the blade is out", t, g_fin.s_deep >= 1.0f ? "full depth" : "at the hand's turn",
	      (double)somvr_fn_depth(&g_fin.cfg, g_fin.p), t - g_fin.snap_t,
	      dealt ? "kl_instant: DealHit forceKill, the ragdoll from the blade" : "kl_instant FAILED", row >= 0 ? g_fin.row : "-",
	      dead, hp0, cb_health(oc->c));
}

/* build 37 (SOMVR_FINISHER_HINT): the execution ready FN_HINT_AFTER_S with no orc to finish - the nearest that could be
 * (alive, not a captain, a beast, in a sync or a kill; within FN_HINT_CM) and why it is not (somvr_fn_why: too far, his
 * back to Talion, not looked at, his neck out of the arm's reach); its chest to ob_frame (fn_entry: the finisher ring
 * and its label). picked = fn_pick's orc whose outline was out of reach (ok = 0), or NULL */
static void fn_hint(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const float root[3], const float hfwd[3],
		    const struct cb_orc *picked, int ok, double t)
{
	int want = g_fin.hint && g_xc.ready && !ok && g_fin.ready_t0 > 0 && t - g_fin.ready_t0 >= FN_HINT_AFTER_S &&
		   t >= g_fin.cool_t;
	static struct cb_orc far[24]; /* (combat_frame's list holds the orcs within CB_RANGE_CM: boot 205015's far orc 10 m
				       * off was not in it - "no orc near") */
	if (want && !picked && pcomb)
		n = cb_orcs_r(pcomb, pc, root, FN_HINT_CM, far, 24), o = far;
	const struct cb_orc *h = NULL;
	int why = SOMVR_FN_WHY_NONE;
	float hd = 0, hcv = -1, hface = 180, fl = hypotf(hfwd[0], hfwd[2]);
	for (int i = 0; want && i < n; i++) {
		if (picked && o[i].c != picked->c)
			continue;
		float dx = o[i].pos[0] - root[0], dz = o[i].pos[2] - root[2], d = hypotf(dx, dz);
		if (!(d < FN_HINT_CM) || cb_dead(o[i].c) || cb_sync(o[i].c) || kp_of_combat(o[i].c) || kl_is_beast(o[i].c, o[i].hobj) ||
		    kl_of(o[i].c) || (h && d >= hd))
			continue;
		h = &o[i], hd = d;
		hcv = fl > 1e-3f && d > 1 ? (dx * hfwd[0] + dz * hfwd[2]) / (d * fl) : -1;
		hface = fabsf(sv_wrap_pi(atan2f(-dx, -dz) - kl_yaw(o[i].hobj))) * 57.29578f;
	}
	if (h) {
		why = somvr_fn_why(hd, FN_REACH_CM, hcv, cosf(FN_VIEW_DEG * 0.01745329f), hface, FN_FACE_DEG, 0);
		if (why == SOMVR_FN_WHY_REACH && picked && g_fin.feel && g_fin.place_why == SOMVR_FN_WHY_NEAR)
			why = SOMVR_FN_WHY_NEAR; /* (build 37: no start a foot from the body - too near for the blade) */
		float nd[4][3];
		if (stl_body(h, nd))
			memcpy(g_fin.hint_pos, nd[1], sizeof g_fin.hint_pos); /* (its torso) */
		else
			g_fin.hint_pos[0] = h->pos[0], g_fin.hint_pos[1] = h->pos[1] + 110.0f, g_fin.hint_pos[2] = h->pos[2];
		g_fin.hint_t = mono(), g_fin.hint_c = h->c, g_fin.hint_why = why, g_fin.hint_frames++;
	}
	atomic_store(&g_fin.hint_show, h != NULL);
	int state = want ? 1 + why : 0;
	if (state != g_fin.hint_was) {
		if (want && !g_fin.hint_was)
			g_fin.hints++;
		static int lines;
		if (lines++ < 300)
			logf_("FINISHER t=%.3f hint %s: %s%s", t, want ? "on" : "off",
			      want ? somvr_fn_why_text(why) : g_xc.ready ? "an orc to finish now, or the cooldown" : "the execution not ready",
			      "");
		if (want && h && lines < 300)
			logf_("FINISHER t=%.3f hint: the nearest that could be, orc %#llx: %.0f cm from Talion (reach %.0f), %.0f deg off "
			      "the view (%.0f), facing %.0f deg off Talion (%.0f); its ring and \"FACE TO FACE / IN REACH\" on him (the "
			      "execution ready %.1f s)", t, (unsigned long long)h->c, (double)hd, (double)FN_REACH_CM,
			      acosf(fmaxf(-1.0f, fminf(1.0f, hcv))) * 57.29578f, (double)FN_VIEW_DEG, (double)hface, (double)FN_FACE_DEG,
			      t - g_fin.ready_t0);
		g_fin.hint_was = state;
	}
}
/* ob_frame (H4, the game thread): the hint's ring into this frame's objective entry; 1 = published */
static int fn_entry(somvr_objective_frame *e)
{
	if (!atomic_load(&g_fin.hint_show) || mono() - g_fin.hint_t > 0.25 || !vec_ok(g_fin.hint_pos, 1e7f))
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	somvr_th_to_xr(&mp, g_fin.hint_pos, e->pos);
	const somvr_posef *ev = g_fr.views.pose;
	float d = 0;
	for (int i = 0; i < 3; i++) {
		float k = e->pos[i] - 0.5f * (ev[0].p[i] + ev[1].p[i]);
		d += k * k;
	}
	e->n = 1, e->id = (uint32_t)g_fin.hint_why, e->flags = SOMVR_OBF_FINISH;
	e->dist_m = somvr_ob_strike_ring(sqrtf(d), 0.7f, 8.0f); /* (the ring's diameter: the strike's rule) */
	return 1;
}

/* combat_frame, with the drawn blade b: the whole finisher */
static void fn_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const somvr_blade *b, const float *root, double t)
{
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
	g_fin.near_cm = 1e9f, g_fin.near_t = t; /* (build 37: Practice > Charge execution's spawn asks for it) */
	for (int i = 0; i < n; i++)
		if (!cb_dead(o[i].c))
			g_fin.near_cm = fminf(g_fin.near_cm, hypotf(o[i].pos[0] - root[0], o[i].pos[2] - root[2]));
	if (g_fin.phase == FN_IDLE) {
		/* (an attack under way does not stop it: slowed with the world, it lands only if the player is slow - its hit
		 * clears the charge, the game's rule, and that cancels; the orc's half at the way in replaces its attack; boots
		 * 151121: a practice orc's attack in flight at the ready cancelled 2 of 3) */
		if (!g_xc.ready)
			g_fin.ready_t0 = 0;
		else if (!(g_fin.ready_t0 > 0))
			g_fin.ready_t0 = t;
		int pk = g_xc.ready && t >= g_fin.cool_t && fn_pick(o, n, root, hfwd, &oc);
		int rc = pk && fn_outline(&oc, root) && fn_reachable(0);
		fn_hint(pcomb, pc, o, n, root, hfwd, pk ? &oc : NULL, rc, t); /* build 37: why not, and where one that could be is */
		if (!rc)
			return;
		atomic_store(&g_fin.hold_c, oc.c), atomic_store(&g_fin.hold_h, oc.hobj);
		g_fin.phase = FN_READY, g_fin.c = oc.c, g_fin.hobj = oc.hobj, g_fin.t0 = t, g_fin.readies++;
		g_fin.p = 0, g_fin.in_done = g_fin.out_done = g_fin.deep_done = g_fin.pose_done = 0, g_fin.have_off = 0;
		g_fin.follow_cm = 0, g_fin.match_sum = 0, g_fin.match_n = 0;
		g_fin.killed = 0, g_fin.kill_t = g_fin.dead_t = 0, g_fin.hdeep_ok = 0, g_fin.flash_until = 0, g_fin.out_v = 0;
		g_fin.s_deep = 1, g_fin.back_t = 0;
		atomic_store(&g_fin.rel, 0);
		atomic_store(&g_fin.show, 1);
		fn_slowmo(1, t);
		haptic(SOMVR_HAND_R, SOMVR_HX_READY); /* (the outline's there: the hand is asked) */
		g_fin.seen_t = t;
		if (g_fin.feel)
			logf_("FINISHER t=%.3f place: the grip at %.0f %.0f %.0f (cm right, up, ahead of the eyes) %.0f cm from the shoulder, the "
			      "stab %.0f cm of travel to %.0f cm deep, the hand's end %.0f cm out", t, g_fin.place_g[0], g_fin.place_g[1],
			      g_fin.place_g[2], sqrtf((g_fin.place_g[0] - 17) * (g_fin.place_g[0] - 17) +
						      (g_fin.place_g[1] + 22) * (g_fin.place_g[1] + 22) + (g_fin.place_g[2] + 4) * (g_fin.place_g[2] + 4)),
			      (double)(g_fin.cfg.start_cm + g_fin.cfg.depth_cm), (double)g_fin.cfg.depth_cm, (double)g_fin.place_ext);
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
		g_fin.snap_req = 1, g_fin.pull_t0 = mono(), g_fin.ride_have = 0;
		atomic_store(&g_fin.rel, 0);
		if (g_fin.feel) { /* build 37: unmistakable - the thud, the lock, the outline's flash, the pull onto the line */
			g_fin.flash_until = mono() + g_fin.flash_s, g_fin.pulls++, g_fin.pull_frames = 0, g_fin.pull_from_cm = -1;
			atomic_store(&g_fin.over, 1);
			haptic_k(SOMVR_HAND_R, SOMVR_HX_CLANK, 1.0f);
			if (!fn_snd(g_fin.snd_lock, g_fin.start, "the snap's lock", t) && strcmp(g_fin.snd_lock, "cbt_imp_sword") &&
			    !fn_snd("cbt_imp_sword", g_fin.start, "the snap's lock (the second choice)", t))
				fn_snd("cbt_imp_surface_player", g_fin.start, "the snap's lock (the parry's clang)", t);
		} else {
			atomic_store(&g_fin.show, 0), atomic_store(&g_fin.over, 1);
			haptic(SOMVR_HAND_R, SOMVR_HX_TAKE); /* (the sword taken onto the outline) */
		}
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
	if (atomic_load(&g_fin.show) && !(mono() < g_fin.flash_until)) {
		atomic_store(&g_fin.show, 0); /* (build 37: the outline's flash over - gone) */
		logf_("FINISHER t=%.3f flash over %.2f s after the snap: the outline gone", t, t - g_fin.snap_t);
	}
	if (atomic_exchange(&g_fin.pull_log, 0)) {
		logf_("FINISHER t=%.3f pull: the blade drawn onto the line over %d arms frames (%.2f s), from %.1f cm off it", t,
		      g_fin.pull_frames, (double)FN_PULL_S, (double)g_fin.pull_from_cm);
		g_fin.pull_frames = -1; /* (once) */
	}
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
	int hand_out = g_fin.feel && g_fin.killed, turned = 0; /* build 37: after the kill the way out is the hand's */
	if (hand_out) {
		if (g_fin.hlast_ok && !g_fin.hdeep_ok)
			memcpy(g_fin.hdeep, g_fin.hlast, sizeof g_fin.hdeep), g_fin.hdeep_ok = 1, g_fin.out_move_t = t;
		float r = 0, perp = 0;
		if (g_fin.hdeep_ok && g_fin.hlast_ok) {
			float d[3] = { g_fin.hlast[0] - g_fin.hdeep[0], g_fin.hlast[1] - g_fin.hdeep[1], g_fin.hlast[2] - g_fin.hdeep[2] };
			r = -(d[0] * g_fin.dir[0] + d[1] * g_fin.dir[1] + d[2] * g_fin.dir[2]); /* (+ = back along the wound's axis) */
			float px = d[0] + r * g_fin.dir[0], py = d[1] + r * g_fin.dir[1], pz = d[2] + r * g_fin.dir[2];
			perp = sqrtf(px * px + py * py + pz * pz);
			if (r < 0) /* (the hand still going in past the kill: the way out counts from its deepest - no dead travel) */
				memcpy(g_fin.hdeep, g_fin.hlast, sizeof g_fin.hdeep), r = 0, perp = 0;
		}
		g_fin.p = somvr_fn_out_p2(&g_fin.cfg, r, g_fin.s_deep);
		float tot = g_fin.cfg.start_cm + g_fin.cfg.depth_cm, v = (g_fin.p - p0) * tot / dt;
		g_fin.out_v += (1.0f - expf(-dt / 0.04f)) * (v - g_fin.out_v);
		rate = g_fin.out_v, match = 1;
		if (fabsf(g_fin.p - p0) * tot > 0.05f)
			g_fin.out_move_t = t;
		if (somvr_fn_depth(&g_fin.cfg, g_fin.p) >= 0 && g_fin.out_v > 5.0f) /* (the drag of the blade in him) */
			haptic_k(SOMVR_HAND_R, SOMVR_HX_FLESH, fminf(1.0f, fmaxf(0.25f, g_fin.out_v / 100.0f)));
		const char *why = perp > FN_OUT_FREE_CM ? "the hand away from the wound's line"
				  : t - g_fin.out_move_t > FN_OUT_WAIT_S ? "no pull after the kill" : NULL;
		if (why && somvr_fn_depth(&g_fin.cfg, g_fin.p) >= 0) {
			g_fin.stabs++;
			logf_("FINISHER t=%.3f end: %s (%.0f cm off it, %.1f s still) - the blade let go to the hand %.2f s after the kill, "
			      "%.0f cm deep; the slow motion off", t, why, (double)perp, t - g_fin.out_move_t, t - g_fin.kill_t,
			      (double)somvr_fn_depth(&g_fin.cfg, g_fin.p));
			fn_stop(t);
			return;
		}
	} else {
		/* build 37: the way in follows the hand 1:1 along the line (up to SOMVR_FINISHER_FOLLOW_VMAX, its accuracy kept,
		 * no drift under 15 cm/s) and a pull back draws the blade back with it - the 22:09 session's own thrust (mark
		 * 221801_7: ~60 cm in 0.3 s, a 3 m/s peak) left build 33's 1 m/s blade at the neck: "I can't move the thing fast
		 * enough" */
		somvr_fn_cfg sc = g_fin.cfg;
		sc.back_free = sc.follow = g_fin.feel;
		if (g_fin.feel)
			sc.v_max = g_fin.follow_vmax;
		g_fin.p = somvr_fn_step(&sc, hv, g_fin.dir, g_fin.p, dt, &rate, &match);
		if (g_fin.feel && g_fin.p >= 1.0f && !g_fin.killed)
			g_fin.p = 1.0f, g_fin.s_deep = 1.0f; /* (the kill below first; the way out is then the hand's) */
		/* build 37: the hand turns back with the point FN_TURN_CM in or more - the deepest is where it turned: the kill
		 * there (a natural 40 cm thrust at 1 m/s reached 15-19 of 22 cm in boot 205015, a 30 cm one ~8 cm in boot 210501;
		 * pulling back drew the blade out again and nothing died - with its blood and its sound already shown) */
		if (g_fin.feel && !g_fin.killed && (p0 - g_fin.p) * (g_fin.cfg.start_cm + g_fin.cfg.depth_cm) > 15.0f * dt &&
		    somvr_fn_depth(&g_fin.cfg, p0) >= FN_TURN_CM) { /* (a clear pull back: over 15 cm/s, not a jitter) */
			g_fin.p = p0, g_fin.s_deep = p0, turned = 1;
			logf_("FINISHER t=%.3f deepest: the hand turned back with the point %.0f cm into the neck (of %.0f; %.0f or more) "
			      "after %.2f s - the kill there, the way out by the hand", t, (double)somvr_fn_depth(&g_fin.cfg, p0),
			      (double)g_fin.cfg.depth_cm, (double)FN_TURN_CM, t - g_fin.snap_t);
		}
	}
	if (sqrtf(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]) > 15.0f)
		g_fin.match_sum += match, g_fin.match_n++; /* (the hand's match while it moves) */
	if (fabsf(g_fin.p - p0) * (g_fin.cfg.start_cm + g_fin.cfg.depth_cm) > 0.05f)
		g_fin.last_move_t = t; /* (build 37: a real move - a resting hand's smoothed velocity decays but never reaches 0:
					* boot 210501's stab drawn back and left kept the world slow to the boot's end) */
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
	if (g_fin.killed && !(g_fin.dead_t > 0) && cb_dead(g_fin.c)) {
		g_fin.dead_t = t;
		logf_("FINISHER t=%.3f dead: the orc dead %.3f s after the kill's moment", t, t - g_fin.kill_t);
	}
	if (!g_fin.in_done && d0 < 0 && d1 >= 0) { /* the way in: the blood, the thud (b33: the half and the charge too) */
		g_fin.in_done = 1, g_fin.ins++, g_fin.back_t = 0;
		float nd[4][3], *nk = g_fin.neck;
		if (stl_body(&oc, nd) && dist3(nd[2], g_fin.neck) < 40.0f)
			nk = nd[2]; /* (the neck where it is now) */
		float blood_at[3] = { nk[0] - g_fin.dir[0] * 3, nk[1] - g_fin.dir[1] * 3, nk[2] - g_fin.dir[2] * 3 };
		if (g_fin.feel) { /* build 37: the blood along the stab, the stab's sound, a stab's thump; the orc still held */
			stl_blood_dir(oc.hobj, blood_at, g_fin.dir, "the finisher's stab going in", t);
			haptic_k(SOMVR_HAND_R, SOMVR_HX_HIT, 0.5f);
			fn_snd("cbt_insta_stab", blood_at, "the way in", t);
			logf_("FINISHER t=%.3f in: the point at the neck after %.2f s of the stab (the hand's match %.2f, the blade %.0f cm/s): "
			      "blood along the stab; the kill at full depth (%.0f cm) or where the hand turns back past %.0f cm", t,
			      t - g_fin.snap_t, match, rate, (double)g_fin.cfg.depth_cm, (double)FN_TURN_CM);
		} else {
			fn_unhold(); /* (SOMVR_KILLS holds it from its half's start) */
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
	}
	if (g_fin.feel && g_fin.in_done && !g_fin.killed && d0 >= 0 && d1 < 0) { /* build 37: drawn back out before the kill */
		g_fin.in_done = 0, g_fin.back_t = t;
		float out_at[3] = { g_fin.neck[0] - g_fin.dir[0] * 4, g_fin.neck[1] - g_fin.dir[1] * 4, g_fin.neck[2] - g_fin.dir[2] * 4 };
		float back[3] = { -g_fin.dir[0], -g_fin.dir[1], -g_fin.dir[2] };
		stl_blood_dir(oc.hobj, out_at, back, "the finisher's blade drawn back before full depth", t);
		haptic_k(SOMVR_HAND_R, SOMVR_HX_TEAR, 0.2f);
		logf_("FINISHER t=%.3f back: the hand drew the blade back out before %.0f cm in (%.2f s into the stab): no kill yet, "
		      "the stab may go in again", t, (double)FN_TURN_CM, t - g_fin.snap_t);
	}
	if (turned && !g_fin.deep_done) { /* build 37: the turn short of the deepest - the kill there */
		g_fin.deep_done = 1;
		haptic_k(SOMVR_HAND_R, SOMVR_HX_HIT, 0.5f);
		fn_kill(pc, &oc, t);
	}
	if (!g_fin.deep_done && g_fin.p >= 1.0f) {
		g_fin.deep_done = 1;
		haptic_k(SOMVR_HAND_R, SOMVR_HX_HIT, 0.5f); /* (the deepest: a stab's thump) */
		logf_("FINISHER t=%.3f deepest: %.0f cm into the neck after %.2f s; out now%s", t, (double)g_fin.cfg.depth_cm,
		      t - g_fin.snap_t, g_fin.feel ? " - by the hand" : "");
		if (g_fin.feel)
			fn_kill(pc, &oc, t);
	}
	if (g_fin.in_done && !g_fin.out_done && d0 >= 0 && d1 < 0) { /* the way out: the blood again */
		g_fin.out_done = 1, g_fin.outs++;
		float blood_at[3] = { g_fin.neck[0] - g_fin.dir[0] * 4, g_fin.neck[1] - g_fin.dir[1] * 4, g_fin.neck[2] - g_fin.dir[2] * 4 };
		if (g_fin.feel) {
			float back[3] = { -g_fin.dir[0], -g_fin.dir[1], -g_fin.dir[2] };
			stl_blood_dir(oc.hobj, blood_at, back, "the finisher's blade coming out", t);
			fn_snd("cbt_sync_stab_dry", blood_at, "the way out", t);
		} else {
			stl_blood(oc.hobj, blood_at, "the finisher's blade coming out", t);
		}
		haptic_k(SOMVR_HAND_R, SOMVR_HX_TEAR, 0.2f); /* (the blade drawn out) */
		logf_("FINISHER t=%.3f out: the blade out of the neck after %.2f s of the stab%s", t, t - g_fin.snap_t,
		      g_fin.killed ? "" : " (no kill)");
		if (g_fin.feel && g_fin.killed) { /* build 37: out = done - the blade the hand's again, the world at speed */
			g_fin.stabs++;
			logf_("FINISHER t=%.3f end: the blade out %.2f s after the kill (the pull %.0f cm/s); the slow motion off, the sword "
			      "the hand's again (blended over %.2f s); the orc %s", t, t - g_fin.kill_t, (double)g_fin.out_v,
			      (double)FN_PULL_S, cb_dead(g_fin.c) ? "dead" : "alive (!)");
			fn_stop(t);
			return;
		}
	}
	if (g_fin.feel && !g_fin.killed) { /* build 37: never a slow world left waiting (boot 211008: the drifting test hand kept
					      * a stab drawn back out alive 12 s - the stall below never came) */
		const char *why = g_fin.back_t > 0 && !g_fin.in_done && t - g_fin.back_t > FN_BACK_S
					  ? "drawn back out and not in again within 2 s"
				  : t - g_fin.snap_t > FN_STAB_S ? "no kill within 8 s of the snap" : NULL;
		if (why) {
			fn_cancel(why, t);
			return;
		}
	}
	if (g_fin.p >= 2.0f || t - g_fin.last_move_t > 4.0) {
		int done = g_fin.p >= 2.0f;
		g_fin.stabs += done;
		if (!g_fin.in_done) { /* stalled before the neck: a cancel */
			fn_cancel("the stab stalled 4 s before the neck", t);
			return;
		}
		if (g_fin.feel) { /* build 37: a stall never kills - no kill without the blade driven in (the 22:09 session: "he
				   * just dies immediately even though I didn't actually do anything") */
			if (g_fin.killed) {
				logf_("FINISHER t=%.3f end: stalled 4 s after the kill; the blade the hand's again", t);
				fn_stop(t);
			} else {
				fn_cancel("the stab stalled 4 s: no kill without the blade driven in", t);
			}
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
		if (!g_fin.feel)
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
	/* build 37 (re-xr-pipeline.md 171) */
	g_fin.feel = env_word("SOMVR_FINISHER_FEEL", (const char *const[]){ "b33", "b37" }, 2, 0);
	g_fin.sound = env_word("SOMVR_FINISHER_SOUND", w2, 2, 0);
	g_fin.hint = env_word("SOMVR_FINISHER_HINT", w2, 2, 0);
	g_fin.flash_s = env_float("SOMVR_FINISHER_FLASH_S", FN_FLASH_S, 0.05f, 3.0f, bad);
	g_fin.follow_vmax = env_float("SOMVR_FINISHER_FOLLOW_VMAX", 400, 100, 1000, bad);
	const char *ls = getenv("SOMVR_FINISHER_LOCK_SOUND");
	snprintf(g_fin.snd_lock, sizeof g_fin.snd_lock, "%s", ls && *ls ? ls : FN_SND_LOCK);
	g_fin.ok = g_kl.ok && g_xc.mode && g_swp.on;
	logf_("CONFIG finisher=%s (SOMVR_FINISHER): the neck stab - when the execution is ready and an orc in reach faces Talion, "
	      "the world at %.2f, a blue sword outline %.0f cm short of its neck; the drawn sword within %.0f cm / %.0f deg snaps on "
	      "and rides the stab (%.0f cm in and out, at most %.0f cm/s, the hand's speed against %.0f cm/s x its accuracy); the "
	      "orc's half '%s' from the way in; %.0f s to snap%s", g_fin.on ? "on" : "off", (double)g_fin.slowmo,
	      (double)g_fin.cfg.start_cm, (double)g_fin.cfg.align_cm, (double)g_fin.cfg.align_deg, (double)g_fin.cfg.depth_cm,
	      (double)g_fin.cfg.v_max, (double)g_fin.cfg.v_ref, g_fin.row, (double)g_fin.timeout_s,
	      g_fin.ok ? "" : " - OFF (needs SOMVR_KILLS' sites, SOMVR_EXECUTE and SOMVR_SWORD_PHYS)");
	logf_("CONFIG finisher build 37: feel=%s (SOMVR_FINISHER_FEEL: %s), sound=%s (SOMVR_FINISHER_SOUND: the flat F6 kill's "
	      "own - the lock '%s', the way in cbt_insta_stab, the kill cbt_instakill_st_f_03 + cbt_insta_slice + "
	      "vx_PlayerSyncPre_snd + vx_EnemyDeath, the way out cbt_sync_stab_dry), hint=%s (SOMVR_FINISHER_HINT: the execution "
	      "ready %.1f s with no orc to finish - the ring and \"FACE TO FACE / IN REACH\" on the nearest that could be, within "
	      "%.0f m)", g_fin.feel ? "b37" : "b33",
	      g_fin.feel ? "the snap's thud, lock, flash and pull; the way in with the hand 1:1 along the line (to "
			   "SOMVR_FINISHER_FOLLOW_VMAX); the kill at full depth or the hand's turn past 6 cm, never by itself (no paired "
			   "half); the way out the hand's, 0.9 of its pull past 1.5 cm"
			 : "build 33's: a tick at the snap, the half from the way in, the guided way out",
	      g_fin.sound ? "on" : "off", g_fin.snd_lock, g_fin.hint ? "on" : "off", FN_HINT_AFTER_S, (double)FN_HINT_CM / 100);
}
static void fn_end_log(void)
{
	logf_("FINISHER END %s: readies %llu, snaps %llu, stabs %llu (in %llu, out %llu), kills %llu, cancels %llu; frames ready "
	      "%llu, stabbing %llu", g_fin.on ? "on" : "off", (unsigned long long)g_fin.readies, (unsigned long long)g_fin.snaps,
	      (unsigned long long)g_fin.stabs, (unsigned long long)g_fin.ins, (unsigned long long)g_fin.outs,
	      (unsigned long long)g_fin.kills, (unsigned long long)g_fin.cancels, (unsigned long long)g_fin.frames_ready,
	      (unsigned long long)g_fin.frames_stab);
	logf_("FINISHER END build 37: feel %s, pulls %llu, releases %llu; sounds %llu played, %llu not; hints %llu (frames %llu)",
	      g_fin.feel ? "b37" : "b33", (unsigned long long)g_fin.pulls, (unsigned long long)g_fin.releases,
	      (unsigned long long)g_fin.snd_ok, (unsigned long long)g_fin.snd_fail, (unsigned long long)g_fin.hints,
	      (unsigned long long)g_fin.hint_frames);
}
