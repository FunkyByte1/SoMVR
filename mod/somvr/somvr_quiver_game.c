/* somvr_quiver_game.c - build 16's bow (docs/re-xr-pipeline.md 51; included by somvr.c after the VR hands' game side):
 * the quiver behind the string hand's shoulder (SOMVR_BOW_QUIVER=on), the nock on the live string (SOMVR_BOW_HOLD=fist),
 * the grips' axes line (what a real hand does with the bow, once a second while it is out) and the arrow the layer draws
 * in the string hand or on the string (vrhands v4). Game thread; the state machine is somvr_bow.c's. */

/* a hand's distance from its shoulder slot: the head + R(the slots' torso yaw) ofs, x mirrored toward that hand's side
 * (the bow's slot, the quiver; p = the grip in tracking space, as the slots') */
static float bo_shoulder_dist(int h, const float ofs[3], const sv_vec3 *p, float off[3])
{
	sv_vec3 hp;
	sv_quat hq;
	somvr_head_game(g_fr.views.pose, &hp, &hq);
	float yaw = 0;
	if (!sl_torso(&yaw))
		yaw = somvr_slots_heading(hq);
	sv_vec3 o = { (h == SOMVR_HAND_L ? -1.0f : 1.0f) * ofs[0], ofs[1], ofs[2] };
	sv_vec3 r = sv_qrot(sv_qyaw(yaw), o), d = { p->x - hp.x - r.x, p->y - hp.y - r.y, p->z - hp.z - r.z };
	if (off) { /* build 19: d on the torso's yaw (right toward that hand's side, up, ahead) for the take / miss lines */
		sv_vec3 l = sv_qrot(sv_qconj(sv_qyaw(yaw)), d);
		off[0] = (h == SOMVR_HAND_L ? -1.0f : 1.0f) * l.x, off[1] = l.y, off[2] = l.z;
	}
	return sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
}

/* SOMVR_BOW_QUIVER=on: the string hand inside the quiver (radius SOMVR_BOW_QUIVER_R_CM, 4 cm more to leave); a tick when
 * it comes in while the bow is out and no arrow is anywhere (the arrow is there to take) */
/* build 19 (SOMVR_BOW_QUIVER_TO_CM, re-xr-pipeline.md 71): the quiver a capsule from qv_ofs down to qv_to - the reach over
 * the shoulder and down the back: the user's 17b session closed the hand 18-49 cm behind the head (HAND lines next to the
 * misses and the takes: +17 right -19 up -41 ahead, +28 +5 -49), outside build 16's sphere; unset: that sphere */
static float bo_quiver_dist(int h, const sv_vec3 *p, float off[3])
{
	if (!g_bow.qv_cap)
		return bo_shoulder_dist(h, g_bow.qv_ofs, p, off);
	static const float zero[3] = { 0, 0, 0 };
	float r[3], ab[3], ab2 = 0, k = 0;
	bo_shoulder_dist(h, zero, p, r); /* the hand from the head (right toward its side, up, ahead) */
	for (int i = 0; i < 3; i++)
		ab[i] = g_bow.qv_to[i] - g_bow.qv_ofs[i], ab2 += ab[i] * ab[i], k += (r[i] - g_bow.qv_ofs[i]) * ab[i];
	k = ab2 > 1 ? fminf(1.0f, fmaxf(0.0f, k / ab2)) : 0;
	float d2 = 0;
	for (int i = 0; i < 3; i++) {
		float e = r[i] - g_bow.qv_ofs[i] - k * ab[i];
		d2 += e * e, off[i] = r[i] - g_bow.qv_ofs[i];
	}
	return sqrtf(d2);
}

static int bo_quiver_in(int h, const sv_vec3 *p)
{
	float d = bo_quiver_dist(h, p, g_bow.qv_off);
	int was = g_bow.quiver_in;
	g_bow.qv_dist = d;
	g_bow.quiver_in = g_inp.hands[h].valid && d < (was ? g_bow.qv_r + 4 : g_bow.qv_r);
	if (g_bow.quiver_in && !was && g_bow.s.phase != SOMVR_BOW_PH_IDLE && g_bow.s.arrow == SOMVR_BOW_ARROW_NONE)
		in_haptic_event(h, 0.18f, 0.012f, SOMVR_HAP_UI), g_bow.qv_ticks++;
	return g_bow.quiver_in;
}

/* build 19 (SOMVR_BOW_STRING_LOW_CM=ALONG,BACK): the lower limb's end on its own (the bow is not symmetric: its tip nodes
 * 35.2 and 23.8 cm off the riser); unset = SOMVR_BOW_STRING_TIPS_CM / _BACK_CM. k 0: along, 1: back */
static float bo_tips_lo(int k)
{
	float v = g_bow.string_lo[k];
	return isfinite(v) ? v : k ? g_bow.string_back_cm : g_bow.string_tips_cm;
}

/* SOMVR_BOW_HOLD=fist (bo_skel, phase 2, the bow aimed and not drawn, after bo_hold's turn): the nock = the point of the
 * live string (the line between the limbs' tips as the game poses them; our string node is drawn there at rest) at the
 * arrow's height SOMVR_BOW_REST_CM up the fist's tube, side_cm beside the riser on the bow hand's side. Grip frame =
 * hold_q (model - the grip node). Unless SOMVR_BOW_NOCK_CM is set */
static void bo_nock_skel(const sa_node *nd, uint32_t cnt)
{
	if (g_bow.hold_mode == SOMVR_BOW_HOLD_FIST && cnt == BO_BOW_NODES && g_bow.hold_done >= 1) {
		/* build 18 (SOMVR_BOW_STRING_DRAW): the live tips in the bow hand's grip, every skeleton update while out */
		/* (the tip nodes end the limbs' bones a hand's width past the riser; the string is tied at the limbs' ends:
		 * SOMVR_BOW_STRING_TIPS_CM further along each limb's last bone, nodes 3 -> 5 and 4 -> 6) */
		const float *l0 = nd[BO_TIP_LO].pos, *h0 = nd[BO_TIP_HI].pos, *l1 = nd[3].pos, *h1 = nd[4].pos;
		float dl[3] = { l0[0] - l1[0], l0[1] - l1[1], l0[2] - l1[2] }, dh[3] = { h0[0] - h1[0], h0[1] - h1[1], h0[2] - h1[2] };
		float nl = sqrtf(dl[0] * dl[0] + dl[1] * dl[1] + dl[2] * dl[2]), nh = sqrtf(dh[0] * dh[0] + dh[1] * dh[1] + dh[2] * dh[2]);
		float kl = nl > 1 ? bo_tips_lo(0) / nl : 0, kh = nh > 1 ? g_bow.string_tips_cm / nh : 0;
		float bk = g_bow.string_back_cm; /* (the limbs sweep back: the tips toward the string's side, the model's -Z) */
		sv_vec3 ta = sv_qrot(g_bow.hold_q, (sv_vec3){ l0[0] + kl * dl[0], l0[1] + kl * dl[1] - BO_GRIP_NODE_Y,
							      l0[2] + kl * dl[2] - bo_tips_lo(1) }),
			tb = sv_qrot(g_bow.hold_q, (sv_vec3){ h0[0] + kh * dh[0], h0[1] + kh * dh[1] - BO_GRIP_NODE_Y,
							      h0[2] + kh * dh[2] - bk });
		float va[3] = { ta.x, ta.y, ta.z }, vb[3] = { tb.x, tb.y, tb.z };
		/* (build 19: within 100 cm - build 18's 60 dropped every end past 60 cm along the tube, so SOMVR_BOW_STRING_TIPS_CM
		 * 40 and up kept the last shorter ends: the string never drawn with them from the start) */
		if (vec_ok(va, 100.0f) && vec_ok(vb, 100.0f) && fabsf(tb.z - ta.z) > 20.0f)
			g_bow.tip_g[0] = ta, g_bow.tip_g[1] = tb, g_bow.tips_ok = 1;
	}
	if (g_bow.hold_mode != SOMVR_BOW_HOLD_FIST || g_bow.nock_set || cnt != BO_BOW_NODES || g_bow.hold_done < 1 ||
	    g_bow.s.phase != SOMVR_BOW_PH_AIM)
		return;
	const float *lo = nd[BO_TIP_LO].pos, *hi = nd[BO_TIP_HI].pos;
	sv_vec3 a = sv_qrot(g_bow.hold_q, (sv_vec3){ lo[0], lo[1] - BO_GRIP_NODE_Y, lo[2] }),
		b = sv_qrot(g_bow.hold_q, (sv_vec3){ hi[0], hi[1] - BO_GRIP_NODE_Y, hi[2] });
	if (!(fabsf(b.z - a.z) > 20.0f)) /* not the bow's limbs along the tube */
		return;
	float k = (g_bow.rest_cm - a.z) / (b.z - a.z), *c = g_bow.cfg.nock_ofs;
	float n[3] = { a.x + k * (b.x - a.x) + g_bow.side_cm, a.y + k * (b.y - a.y), g_bow.rest_cm };
	if (!vec_ok(n, 30.0f))
		return;
	float dd = sqrtf((n[0] - c[0]) * (n[0] - c[0]) + (n[1] - c[1]) * (n[1] - c[1]) + (n[2] - c[2]) * (n[2] - c[2]));
	if (dd < 0.3f && g_bow.nock_logs) /* (the first measurement is logged even when it agrees) */
		return;
	if (g_bow.nock_logs++ < 6)
		logf_("BOW nock t=%.3f: on the live string at the arrow's height: %.1f,%.1f,%.1f cm in the grip (was %.1f,%.1f,%.1f; the "
		      "tips %.1f,%.1f,%.1f and %.1f,%.1f,%.1f in the grip)", cb_now(), n[0], n[1], n[2], c[0], c[1], c[2], a.x, a.y,
		      a.z, b.x, b.y, b.z);
	memcpy(c, n, sizeof n);
}

/* once a second while the bow is out: both grips' axes in the head's yaw frame (right, up, ahead) - the fist's tube
 * (+z: OpenXR's -Z, little finger to thumb) and +x (the left palm's normal, the right hand's back) - and the bow's limbs and
 * shot from its hold, so a session records what a real hand does (the HAND lines carry positions only) */
static void bo_axes_log(double t, const sv_quat q[2])
{
	if (t < g_bow.axes_next)
		return;
	g_bow.axes_next = t + 1.0;
	sv_vec3 hp;
	sv_quat hq;
	somvr_head_game(g_fr.views.pose, &hp, &hq);
	sv_quat iy = sv_qconj(sv_qyaw(sv_heading(hq)));
	char buf[480];
	size_t o = 0;
	buf[0] = 0;
	for (int h = 0; h < 2 && o < sizeof buf; h++) {
		if (!g_inp.hands[h].valid) {
			o += (size_t)snprintf(buf + o, sizeof buf - o, " %s lost", h ? "R" : "L");
			continue;
		}
		sv_quat r = sv_qmul(iy, q[h]);
		sv_vec3 z = sv_qrot(r, (sv_vec3){ 0, 0, 1 }), x = sv_qrot(r, (sv_vec3){ 1, 0, 0 });
		o += (size_t)snprintf(buf + o, sizeof buf - o, " %s tube (%.2f %.2f %.2f) +x (%.2f %.2f %.2f)", h ? "R" : "L", z.x, z.y,
				      z.z, x.x, x.y, x.z);
	}
	int B = g_bow.cfg.bow_hand;
	if (g_inp.hands[B].valid && o < sizeof buf) {
		sv_quat r = sv_qmul(sv_qmul(iy, q[B]), g_bow.hold_q);
		sv_vec3 l = sv_qrot(r, (sv_vec3){ 0, 1, 0 }), f = sv_qrot(r, (sv_vec3){ 0, 0, 1 });
		o += (size_t)snprintf(buf + o, sizeof buf - o, "; the bow's limbs (%.2f %.2f %.2f) shot (%.2f %.2f %.2f)%s", l.x, l.y,
				      l.z, f.x, f.y, f.z, g_bow.hold_done ? "" : " (its hold not measured yet)");
	}
	logf_("BOW axes t=%.3f (the head's yaw frame: right, up, ahead):%s; arrow %s", t, buf,
	      somvr_bow_arrow_name(g_bow.s.arrow));
}

/* vh_arm (phase 2): the arrow for the layer (vrhands v4), on the grips our hands are drawn with (tracking space, game
 * convention): in the string hand (taken from the quiver: held near its nock between the thumb and the index, the pinch
 * 3 cm up the fist's tube, the shaft out past the knuckles 30 deg up the tube from the arm's way), nocked (its nock on
 * the string hand, where our string node is, along the ray: the bow's shot until drawn back, then through the nock),
 * resting on the string (along the bow's shot) or, after our shot, flying (bo_flight_start). Without the quiver: the
 * drawn arrow and its flight */
#define BO_ARROW_LEN_CM 72.0f
static void bo_vh_arrow(const sa_xf g[2], const int valid[2], somvr_vrhands_frame *e)
{
	e->arrow_len = 0, e->arrow_glow = 0;
	if (!g_bow.on || g_bow.tid != my_tid())
		return;
	int B = g_bow.cfg.bow_hand, S = 1 - B, a = g_bow.s.phase == SOMVR_BOW_PH_IDLE ? SOMVR_BOW_ARROW_NONE : g_bow.s.arrow;
	sv_vec3 tail, dir;
	float glow = 1.0f;
	/* SOMVR_BOW_GAME_ARROW=on: the game's own arrow is the one in the hand and on the drawn string (bo_game_arrow);
	 * ours only rests on the string and flies */
	if (g_bow.game_arrow && atomic_load_explicit(&g_bo_ga_force, memory_order_relaxed) != BO_GA_GAME) {
		if ((a == SOMVR_BOW_ARROW_HAND || a == SOMVR_BOW_ARROW_STRING) && valid[S]) /* (the flight starts there) */
			g_bow.fl_on = 0, g_bow.ar_have = 1, bo_arrow_line(g, a, &g_bow.ar_tail, &g_bow.ar_dir, NULL);
		else if (a == SOMVR_BOW_ARROW_REST)
			g_bow.fl_on = 0;
		return;
	}
	if (a == SOMVR_BOW_ARROW_HAND && valid[S]) {
		bo_arrow_line(g, SOMVR_BOW_ARROW_HAND, &tail, &dir, NULL);
		glow = 0.6f;
	} else if ((a == SOMVR_BOW_ARROW_STRING || a == SOMVR_BOW_ARROW_REST) && valid[B]) {
		/* nocked: from the string hand along the ray (the bow's shot until drawn back, then through the nock) */
		sv_vec3 nock = somvr_bow_nock(&g_bow.cfg, g[B].p, g[B].q);
		tail = nock, dir = somvr_bow_fwd(&g_bow.cfg, g[B].q);
		if (a == SOMVR_BOW_ARROW_STRING && valid[S])
			tail = g[S].p, dir = somvr_bow_ray_dir(&g_bow.cfg, g[B].q, nock, g[S].p, NULL);
	} else if (bo_flight_at(&tail, &dir)) { /* our shot flying (it hit at once: ImmediateImpact) */
		g_bow.fl_frames++;
	} else {
		g_bow.ar_have = 0;
		return;
	}
	if (a != SOMVR_BOW_ARROW_NONE)
		g_bow.fl_on = 0, g_bow.ar_have = 1, g_bow.ar_tail = tail, g_bow.ar_dir = dir;
	sa_xf x = { tail, somvr_bow_look(dir, 0) };
	if (!isfinite(x.p.x + x.p.y + x.p.z + x.q.x + x.q.y + x.q.z + x.q.w))
		return;
	vh_to_xr(&x, &e->arrow); /* its -Z (XR) along the shaft, the origin at the nock end */
	e->arrow_len = BO_ARROW_LEN_CM * 0.01f, e->arrow_glow = glow;
	g_bow.arrow_frames++;
}

/* ---- the bow's sounds (build 16): SOMVR_BOW_SNDLOG=1 (test only) logs the game's sound events by name while the bow is
 * out and 2 s after (the two callers of AK's PostEvent by name, FUN_008c9840 and FUN_008c9870 - every by-name post goes
 * through them; calls.tsv: their only callers of 0x1467870): each name once, and every post within 1.5 s of a release
 * (boot 010322: the world's loops - the spirit tower, spiders - filled a plain log in 20 s); SOMVR_BOW_SOUND=EVENT posts
 * that event at our shot from the bow (sl_sound: our own Wwise object, with the slots' sound on) - our shot has no
 * shoot animation, so none of its sounds come by themselves */
/* BOWSND_SITES_BEGIN (check_sites.py counts this block on its own: the two patched K_CALLs of AK::PostEvent by name) */
static const struct site_spec g_bs_sites_game[] = {
SITE("BS_POST_A", K_CALL, 0x8c9865, 0x8c9852, "48 0f 45 f0 48 89 d7 45 31 c9 6a 00 45 31 c0 31 c9 31 d2 e8 06 e0 b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("BS_POST_B", K_CALL, 0x8c988d, 0x8c987a, "89 f6 6a 00 45 31 c9 b9 a0 8b 8c 00 ba 05 00 00 00 6a 00 e8 de df b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
};
/* BOWSND_SITES_END */
typedef uint32_t (*bs_post_fn)(const char *name, uint64_t obj, uint32_t flags, void *cb, void *cookie, uint32_t n_ext,
			       void *ext, uint32_t playing);
static uintptr_t g_bs_orig;
static _Atomic int64_t g_bs_until_ms;      /* log the posts until then (bo_frame: the bow out + 2 s) */
static _Atomic int64_t g_bs_shot_ms;       /* every post until then (a release + 1.5 s) */
static _Atomic uint64_t g_bs_posts, g_bs_logged;
#define BS_SEEN 96
static char g_bs_seen[BS_SEEN][48];        /* the names logged once (under g_bs_mu) */
static int g_bs_nseen;
static atomic_flag g_bs_mu = ATOMIC_FLAG_INIT;
static struct { int log, drawing; char sound[64], draw[64], draw_stp[72]; uint64_t played, failed, draws; } g_bs;
static uint32_t bs_post(const char *name, uint64_t obj, uint32_t flags, void *cb, void *cookie, uint32_t n_ext, void *ext,
			uint32_t playing)
{
	uint32_t id = ((bs_post_fn)g_bs_orig)(name, obj, flags, cb, cookie, n_ext, ext, playing);
	atomic_fetch_add_explicit(&g_bs_posts, 1, memory_order_relaxed);
	int64_t now = (int64_t)(mono() * 1e3);
	double since = 0;
	int hk = 0, hit = hf_log_window(&since, &hk); /* build 17: SOMVR_COMBAT_HITLOG's window after our hit */
	if (!hit && ((g_hf.log != 2 && now >= atomic_load_explicit(&g_bs_until_ms, memory_order_relaxed)) ||
		     atomic_load_explicit(&g_bs_logged, memory_order_relaxed) >= 400))
		return id; /* (SOMVR_COMBAT_HITLOG=2: every new name, all the time) */
	char n[48];
	size_t k = 0;
	for (; name && k + 1 < sizeof n && peek((uintptr_t)name + k, &n[k], 1) && n[k]; k++)
		;
	n[k] = 0;
	if (hit) {
		static _Atomic int hit_lines;
		if (atomic_fetch_add(&hit_lines, 1) < 400)
			logf_("HITSND t=%.3f '%s' obj %#lx flags %#x -> playing %u (%.0f ms after our %s hit)", now * 1e-3, n,
			      (unsigned long)obj, flags, id, since * 1e3, g_hf_kind_w[hk >= 0 && hk < SC_KIND_N ? hk : 0]);
		return id;
	}
	int shot = now < atomic_load_explicit(&g_bs_shot_ms, memory_order_relaxed), fresh = 1;
	while (atomic_flag_test_and_set_explicit(&g_bs_mu, memory_order_acquire))
		;
	for (int i = 0; i < g_bs_nseen && fresh; i++)
		fresh = strcmp(g_bs_seen[i], n) != 0;
	if (fresh && g_bs_nseen < BS_SEEN)
		memcpy(g_bs_seen[g_bs_nseen++], n, sizeof n);
	atomic_flag_clear_explicit(&g_bs_mu, memory_order_release);
	if (fresh || shot) {
		atomic_fetch_add_explicit(&g_bs_logged, 1, memory_order_relaxed);
		logf_("BOWSND t=%.3f '%s' obj %#lx flags %#x -> playing %u (bow %s%s%s)", now * 1e-3, n, (unsigned long)obj, flags,
		      id, somvr_bow_phase_name(g_bow.s.phase), shot ? ", after a release" : "", fresh ? ", new" : "");
	}
	return id;
}

/* SOMVR_BOW_SNDLOG's patch (also build 17's SOMVR_COMBAT_HITLOG), once: how many of the two calls are ours */
static int bs_install_log(void)
{
	static int done = -1;
	if (done >= 0)
		return done;
	g_bs_orig = g_bs_sites_game[0].target, done = 0;
	for (int i = 0; i < 2; i++)
		done += bo_patch_call(&g_bs_sites_game[i], (void *)bs_post);
	return done;
}

/* bo_start: SOMVR_BOW_SNDLOG, SOMVR_BOW_SOUND */
static void bs_config(int *bad)
{
	(void)bad;
	g_bs.log = env_word("SOMVR_BOW_SNDLOG", (const char *const[]){ "0", "1" }, 2, 0);
	const char *v = getenv("SOMVR_BOW_SOUND");
	snprintf(g_bs.sound, sizeof g_bs.sound, "%s", v && v[0] && strcmp(v, "off") ? v : "");
	v = getenv("SOMVR_BOW_DRAW_SOUND");
	snprintf(g_bs.draw, sizeof g_bs.draw, "%s", v && v[0] && strcmp(v, "off") ? v : "");
	snprintf(g_bs.draw_stp, sizeof g_bs.draw_stp, "%s_stp", g_bs.draw);
	if (g_bs.log) {
		int in = bs_install_log();
		logf_("CONFIG bow sndlog=1 (SOMVR_BOW_SNDLOG, test only): %d of 2 PostEvent-by-name calls patched; the game's sound "
		      "events logged while the bow is out and 2 s after (BOWSND lines, 400 at most)", in);
	}
	if (g_bs.sound[0] || g_bs.draw[0])
		logf_("CONFIG bow sound='%s' draw='%s' (SOMVR_BOW_SOUND, SOMVR_BOW_DRAW_SOUND): the shot's event at our release, the "
		      "draw's at the nock and its '%s' at the release, a let-down or the bow's end, from the bow hand (the slots' "
		      "sound object%s)", g_bs.sound, g_bs.draw, g_bs.draw_stp, g_sl.sound ? "" : "; SOMVR_SLOTS' sound is off: not "
		      "played");
}

/* bo_frame: the log's window; our shot's sound at the bow (E_RELEASE with a shot of ours) */
static void bs_frame(const somvr_bow_out *o, const sv_vec3 pw[2])
{
	if (g_bs.log && o->phase != SOMVR_BOW_PH_IDLE)
		atomic_store_explicit(&g_bs_until_ms, (int64_t)(mono() * 1e3) + 2000, memory_order_relaxed);
	if (g_bs.log && (o->events & (SOMVR_BOW_E_RELEASE | SOMVR_BOW_E_FIRE)))
		atomic_store_explicit(&g_bs_shot_ms, (int64_t)(mono() * 1e3) + 1500, memory_order_relaxed);
	uint32_t e = o->events;
	int nock = g_bs.draw[0] && (e & SOMVR_BOW_E_NOCK), stop = g_bs.drawing && (e & (SOMVR_BOW_E_RELEASE |
		   SOMVR_BOW_E_LETDOWN | SOMVR_BOW_E_EXIT)), fire = g_bs.sound[0] && (e & SOMVR_BOW_E_RELEASE);
	if (!nock && !stop && !fire)
		return;
	sv_vec3 hw;
	sv_quat hqw;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hw, &hqw);
	sv_vec3 at = pw[g_bow.cfg.bow_hand];
	if (stop) /* the draw's creak cut at the release (the stock shot posts its _stp at the fire too) */
		sl_sound(g_bs.draw_stp, at, hw), g_bs.drawing = 0;
	if (fire) {
		uint64_t before = g_sl.sounds;
		sl_sound(g_bs.sound, at, hw);
		if (g_sl.sounds > before)
			g_bs.played++;
		else
			g_bs.failed++;
	}
	if (nock && !(e & (SOMVR_BOW_E_RELEASE | SOMVR_BOW_E_LETDOWN | SOMVR_BOW_E_EXIT)))
		sl_sound(g_bs.draw, at, hw), g_bs.drawing = 1, g_bs.draws++;
}

static void bs_end_log(void)
{
	if (g_bs.log || g_bs.sound[0] || g_bs.draw[0])
		logf_("BOW END sounds: the shot's '%s' played %llu, not %llu; the draw's '%s' %llu; sndlog %d: %llu posts by name, "
		      "%llu logged; our arrow drawn flying %llu times, %llu frames", g_bs.sound, (unsigned long long)g_bs.played,
		      (unsigned long long)g_bs.failed, g_bs.draw, (unsigned long long)g_bs.draws, g_bs.log,
		      (unsigned long long)atomic_load(&g_bs_posts), (unsigned long long)atomic_load(&g_bs_logged),
		      (unsigned long long)g_bow.flights, (unsigned long long)g_bow.fl_frames);
	else
		logf_("BOW END flight: our arrow drawn flying %llu times, %llu frames", (unsigned long long)g_bow.flights,
		      (unsigned long long)g_bow.fl_frames);
}

/* bo_shoot, our shot made: the arrow drawn on the string flies on (tracking space) along the shot - toward the assist's aim
 * point when it picked one (aim, world; aim_m from the nock), else straight - at SOMVR_BOW_FLIGHT_MPS until its head is
 * there (else 40 m). Only a picture: the game's arrow hit at once */
static void bo_flight_start(double t, sv_vec3 o, sv_vec3 d, const float *aim, float aim_m)
{
	if (!(g_bow.fl_mps > 0) || !g_bow.ar_have)
		return;
	sv_vec3 w = d;
	if (aim) {
		sv_vec3 v = { aim[0] - o.x, aim[1] - o.y, aim[2] - o.z };
		float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
		if (l > 1.0f)
			w = (sv_vec3){ v.x / l, v.y / l, v.z / l };
	}
	sv_quat rq = somvr_rig_quat(&g_rig, g_fr.base, (sv_quat){ 0, 0, 0, 1 });
	sv_vec3 td = sv_qrot(sv_qconj(rq), w);
	if (!isfinite(td.x + td.y + td.z))
		return;
	float back = g_bow.s.last_draw > 0 ? g_bow.s.last_draw : 50.0f;
	g_bow.fl_tail = g_bow.ar_tail, g_bow.fl_dir = td, g_bow.fl_t0 = t;
	g_bow.fl_len_cm = fmaxf(0.0f, (aim ? aim_m * 100.0f : 4000.0f) + back - BO_ARROW_LEN_CM);
	g_bow.fl_on = 1, g_bow.flights++;
	if (bo_log_ok(t))
		logf_("BOW t=%.3f flight: our arrow drawn flying %.0f m at %.0f m/s %s (the game's hit at once: ImmediateImpact)", t,
		      g_bow.fl_len_cm / 100.0f, g_bow.fl_mps, aim ? "to the assist's aim point" : "straight along the shot");
}

/* the word `bow inv` (headless): every inventory slot's item, its record and its model entries - the object, its attach
 * nodes, where it is and how far from each grip (which object is the arrow in the right hand while aiming?) */
static void bo_inv_log(double t)
{
	struct bo_act a;
	bo_active(g_fr.pl, &a);
	if (!a.combat) {
		logf_("BOW inv t=%.3f: no player Combat / inventory", t);
		return;
	}
	int B = g_bow.cfg.bow_hand, S = 1 - B;
	for (uint32_t slot = 0; slot < 8; slot++) {
		uintptr_t it = peek_ptr(a.inv + BO_INV_ITEMS + (uintptr_t)slot * BO_INV_STRIDE), rec = it ? peek_ptr(it + BO_ITEM_REC) : 0;
		if (!it)
			continue;
		uintptr_t b = peek_ptr(it + BO_IT_ENTRIES), e = peek_ptr(it + BO_IT_ENTRIES + 8);
		int ne = b && e > b && (e - b) % BO_IT_ESIZE == 0 ? (int)((e - b) / BO_IT_ESIZE) : 0;
		char buf[1200];
		size_t o = 0;
		buf[0] = 0;
		for (int k = 0; k < ne && k < 8 && o < sizeof buf; k++) {
			uintptr_t en = b + (uintptr_t)k * BO_IT_ESIZE, obj = peek_ptr(en + BO_IE_OBJ);
			uint32_t act = 0, ina = 0;
			peek(en + BO_IE_ACT, &act, 4), peek(en + BO_IE_INACT, &ina, 4);
			float op[3] = { 0, 0, 0 };
			int got = obj && peek(obj + OBJ_POS, op, sizeof op) && vec_ok(op, 1e7f);
			float dl = 0, dr = 0;
			if (got) {
				sv_vec3 pl = g_bow.s.p[B], pr = g_bow.s.p[S];
				dl = sqrtf((op[0] - pl.x) * (op[0] - pl.x) + (op[1] - pl.y) * (op[1] - pl.y) + (op[2] - pl.z) * (op[2] - pl.z));
				dr = sqrtf((op[0] - pr.x) * (op[0] - pr.x) + (op[1] - pr.y) * (op[1] - pr.y) + (op[2] - pr.z) * (op[2] - pr.z));
			}
			o += (size_t)snprintf(buf + o, sizeof buf - o, " [%d] obj %#lx act %#x%s inact %#x%s at %.0f,%.0f,%.0f (L %.0f cm, R %.0f cm);",
					      k, (unsigned long)obj, act, act == SA_H_L_WEAPON ? "(L_Weapon)" : act == SA_H_R_WEAPON ?
					      "(R_Weapon)" : "", ina, ina == SA_H_L_WEAPON ? "(L_Weapon)" : ina == SA_H_R_WEAPON ?
					      "(R_Weapon)" : "", op[0], op[1], op[2], got ? dl : -1.0f, got ? dr : -1.0f);
		}
		logf_("BOW inv t=%.3f slot %u%s: item %#lx record %#lx (ranged %s), %d model entries:%s", t, slot,
		      slot == a.slot ? " (active)" : "", (unsigned long)it, (unsigned long)rec,
		      rec && rx_db_rec(rec, g_bog.sch_item, BO_ITEM_RANGED) ? "yes" : "no", ne, buf);
	}
}

/* our shot's arrow in flight now (tracking space): its nock end and direction; 0 = none (ended: fl_on cleared) */
static int bo_flight_at(sv_vec3 *tail, sv_vec3 *dir)
{
	if (!g_bow.fl_on)
		return 0;
	float k = (float)(cb_now() - g_bow.fl_t0) * g_bow.fl_mps * 100.0f;
	if (k > g_bow.fl_len_cm) {
		g_bow.fl_on = 0;
		return 0;
	}
	*tail = (sv_vec3){ g_bow.fl_tail.x + k * g_bow.fl_dir.x, g_bow.fl_tail.y + k * g_bow.fl_dir.y, g_bow.fl_tail.z + k * g_bow.fl_dir.z };
	*dir = g_bow.fl_dir;
	return 1;
}

/* the arrow's line for state a (HAND / STRING) on the grips g (tracking space, game convention): its nock end, its
 * direction and a frame (q: +Z along it, the roll from the string hand's grip) - the layer's arrow and the game's */
/* build 19 (SOMVR_BOW_STRING_REST=ours): where the drawn string's middle and the nocked arrow's nock are - the string
 * hand, or with the hand in front of the string's rest line (between it and the riser) its point on the rest plane: a
 * hand there does not push the string toward the riser (continuous where the hand crosses the line). g: the grips */
static sv_vec3 bo_string_pull(const sa_xf g[2])
{
	int B = g_bow.cfg.bow_hand, S = 1 - B;
	sv_vec3 h = g[S].p;
	if (!g_bow.srest_ours || !g_bow.tips_ok)
		return h;
	sv_vec3 lo = sv_qrot(g[B].q, g_bow.tip_g[0]), hi = sv_qrot(g[B].q, g_bow.tip_g[1]), f = somvr_bow_fwd(&g_bow.cfg, g[B].q);
	sv_vec3 rest = { g[B].p.x + 0.5f * (lo.x + hi.x), g[B].p.y + 0.5f * (lo.y + hi.y), g[B].p.z + 0.5f * (lo.z + hi.z) };
	float d = (h.x - rest.x) * f.x + (h.y - rest.y) * f.y + (h.z - rest.z) * f.z;
	return d > 0 ? (sv_vec3){ h.x - d * f.x, h.y - d * f.y, h.z - d * f.z } : h;
}

static void bo_arrow_line(const sa_xf g[2], int a, sv_vec3 *tail, sv_vec3 *dir, sv_quat *q)
{
	int B = g_bow.cfg.bow_hand, S = 1 - B;
	if (a == SOMVR_BOW_ARROW_HAND) { /* held near its nock: the pinch 3 cm up the tube, out past the knuckles */
		sv_quat r = sv_qmul(g[S].q, (sv_quat){ 0.5f, 0, 0, 0.8660254f }); /* R_x(60): +Z -> (0, -0.866, 0.5) */
		/* build 18, SOMVR_BOW_ARROW_HOLD=pinch: through our drawn hand's thumb-index pinch (its size, its calibration),
		 * the nock 1.5 cm behind it - not 3 cm up the tube's axis inside the fist (the user's 17b mark 2) */
		int pinch = g_bow.arrow_pinch && g_bow.pinch_ok;
		sv_vec3 pin = sv_qrot(g[S].q, pinch ? (sv_vec3){ S == SOMVR_HAND_R ? g_bow.pinch_g[0] : -g_bow.pinch_g[0],
								  g_bow.pinch_g[1], g_bow.pinch_g[2] } : (sv_vec3){ 0, 0, 3 });
		float back = pinch ? 1.5f : 3.0f;
		*dir = sv_qrot(r, (sv_vec3){ 0, 0, 1 });
		*tail = (sv_vec3){ g[S].p.x + pin.x - back * dir->x, g[S].p.y + pin.y - back * dir->y,
				   g[S].p.z + pin.z - back * dir->z };
		if (q)
			*q = sv_qnormalize(r);
		return;
	}
	sv_vec3 nock = somvr_bow_nock(&g_bow.cfg, g[B].p, g[B].q); /* nocked: from the string hand along the ray */
	*tail = bo_string_pull(g), *dir = somvr_bow_ray_dir(&g_bow.cfg, g[B].q, nock, g[S].p, NULL);
	if (q)
		*q = somvr_bow_align_ax(g[S].q, (sv_vec3){ 0, 0, 1 }, *dir, 1);
}

/* arm_eval, after the arms' write-back (SOMVR_BOW_GAME_ARROW=on): the game draws its own glowing arrow on Talion's right
 * weapon node while the bow is out (the wraith round's runs 035928 / 041615: the long shaft and the crystal fletching at
 * the right hand, drawn outside the skeletal calls; boot 050509: no inventory model, not the mode's FX). R_Weapon is a
 * leaf the arms only aim at, so it can sit anywhere: here it carries the arrow by our quiver - in the hand along the
 * held arrow's line, nocked or drawn along the ray from the string hand, resting on the string along the bow's shot,
 * and after our shot flying on (bo_flight_at: the game's own hit is at once); with no arrow (none taken, after the
 * flight) it is put away 100 m under Talion. Through the arms' own right calibration (the arrow lies where a held
 * sword's blade would: along the frame's +Z, its nock at the origin: boot 051654); one node poked over whatever the
 * arms' block or the animation left there */
static void bo_game_arrow(const sa_xf g[2], const int valid[2], sa_xf X, sv_vec3 shift, uintptr_t xf)
{
	if (!g_bow.on || !g_bow.game_arrow || g_bow.tid != my_tid() || !xf)
		return;
	int B = g_bow.cfg.bow_hand, S = 1 - B, force = atomic_load_explicit(&g_bo_ga_force, memory_order_relaxed);
	if (g_bow.s.phase == SOMVR_BOW_PH_IDLE) /* the bow away: the node is the sword's again (a restore may draw it) */
		g_bow.fl_on = 0;
	if (force == BO_GA_GAME || g_arm.ch[S].wpn < 0 || g_bow.s.phase == SOMVR_BOW_PH_IDLE)
		return;
	int a = g_bow.s.phase == SOMVR_BOW_PH_IDLE ? SOMVR_BOW_ARROW_NONE : g_bow.s.arrow;
	sa_xf f = { { 0, 0, 0 }, { 0, 0, 0, 1 } };
	sv_vec3 tail, dir;
	int mode = 0; /* 0 away, 1 in the hand, 2 nocked/drawn, 3 resting, 4 flying */
	if (force == BO_GA_HAND || (force != BO_GA_HIDE && a == SOMVR_BOW_ARROW_HAND)) {
		if (valid[S])
			mode = 1, bo_arrow_line(g, SOMVR_BOW_ARROW_HAND, &tail, &dir, &f.q);
	} else if (force != BO_GA_HIDE && a == SOMVR_BOW_ARROW_STRING) {
		if (valid[S] && valid[B])
			mode = 2, bo_arrow_line(g, SOMVR_BOW_ARROW_STRING, &tail, &dir, &f.q);
	} else if (force != BO_GA_HIDE && a == SOMVR_BOW_ARROW_REST) {
		if (valid[B]) {
			tail = somvr_bow_nock(&g_bow.cfg, g[B].p, g[B].q), dir = somvr_bow_fwd(&g_bow.cfg, g[B].q);
			if (g_bow.srest_ours && g_bow.tips_ok) { /* build 19: its nock on the string's rest line, not the riser */
				sv_vec3 lo = sv_qrot(g[B].q, g_bow.tip_g[0]), hi = sv_qrot(g[B].q, g_bow.tip_g[1]);
				float d = (tail.x - g[B].p.x - 0.5f * (lo.x + hi.x)) * dir.x + (tail.y - g[B].p.y - 0.5f * (lo.y + hi.y)) *
					  dir.y + (tail.z - g[B].p.z - 0.5f * (lo.z + hi.z)) * dir.z;
				if (d > 0)
					tail = (sv_vec3){ tail.x - d * dir.x, tail.y - d * dir.y, tail.z - d * dir.z };
			}
			f.q = somvr_bow_align_ax(g[B].q, (sv_vec3){ 0, 0, 1 }, dir, 1), mode = 3;
		}
	} else if (force != BO_GA_HIDE && bo_flight_at(&tail, &dir)) {
		f.q = somvr_bow_look(dir, 0), mode = 4, g_bow.fl_frames++;
	}
	sa_xf tw = { { 0, -10000.0f, 0 }, { 0, 0, 0, 1 } }; /* away: 100 m under his root (model space) */
	if (mode) {
		f.p = (sv_vec3){ tail.x + g_bow.ga_ofs_cm * dir.x, tail.y + g_bow.ga_ofs_cm * dir.y, tail.z + g_bow.ga_ofs_cm * dir.z };
		sa_xf gm = sa_grip_to_model(&g_arm.rig, shift, X, f);
		float bs = sz_body_scale();
		if (bs != 1.0f)
			gm.p = (sv_vec3){ gm.p.x / bs, gm.p.y / bs, gm.p.z / bs };
		/* build 18 (SOMVR_BOW_ARROW_HOLD=pinch): the right weapon node's own calibration, without the sword's slide in
		 * the fist (SOMVR_SWORD_GRIP_CM: 8 cm ahead along the blade = along the arrow) and its angle - the arrow's nock
		 * was 8 cm ahead of the hand (the user's 17b mark 2) */
		tw = sa_xf_mul(gm, g_bow.arrow_pinch ? sa_xf_euler(g_bd.cal_v[S]) : g_arm.cal[S]);
	}
	sa_node n;
	sa_node_set(&n, tw);
	if (!arm_poke(xf + (uintptr_t)g_arm.ch[S].wpn * sizeof(sa_node), &n, sizeof n))
		return;
	if (mode == 4 && g_bow.ga_mode_was != 4)
		g_bow.ga_fl_logs = 0;
	if (mode == 4 && g_bow.ga_fl_logs++ < 6) /* the flight's first frames: how far, where the node went (model space) */
		logf_("BOW t=%.3f game arrow flying: %.0f cm out, the nock at %.0f,%.0f,%.0f (tracking), the node at %.0f,%.0f,%.0f (model)",
		      cb_now(), (cb_now() - g_bow.fl_t0) * g_bow.fl_mps * 100.0, tail.x, tail.y, tail.z, tw.p.x, tw.p.y, tw.p.z);
	g_bow.ga_frames[mode]++;
	if (mode != g_bow.ga_mode_was && bo_log_ok(cb_now()))
		logf_("BOW t=%.3f game arrow: %s (the right weapon node%s)", cb_now(), (const char *const[]){ "put away", "in the hand",
		      "nocked / drawn", "resting on the string", "flying" }[mode], force >= 0 ? ", the test word" : "");
	g_bow.ga_mode_was = mode;
}

/* build 18: the pinch of our drawn hand (SOMVR_BOW_ARROW_HOLD=pinch): the model the layer draws (SOMVR_XR_HANDS_CAL,
 * SOMVR_XR_HANDS_SIZE) in the arrow hand's fist; the right grip's frame (XR, m) -> the game convention (x, y, -z), cm */
static void bo_pinch_config(void)
{
	g_bow.pinch_ok = 0;
	if (!g_bow.arrow_pinch)
		return;
	static somvr_vh_model m;
	somvr_vh_cfg c;
	somvr_vh_cfg_default(&c);
	const char *cal = getenv("SOMVR_XR_HANDS_CAL");
	if (cal && cal[0])
		somvr_vh_parse_cal(cal, &c);
	somvr_vh_parse_size(getenv("SOMVR_XR_HANDS_SIZE"), &c);
	float p[3];
	if (somvr_vh_init(&m, &c) == 0 && somvr_vh_pinch(&m, &c, p))
		g_bow.pinch_g[0] = 100 * p[0], g_bow.pinch_g[1] = 100 * p[1], g_bow.pinch_g[2] = -100 * p[2], g_bow.pinch_ok = 1;
	logf_("CONFIG bow arrow_hold=pinch (SOMVR_BOW_ARROW_HOLD): the arrow from the quiver through our hand's thumb-index "
	      "pinch %s%.1f,%.1f,%.1f cm in the right grip (game convention; the hand at size %.2f), its nock 1.5 cm behind it; "
	      "build 16's: 0,0,3 (inside the fist)", g_bow.pinch_ok ? "" : "(NO MODEL: build 16's) ", g_bow.pinch_g[0],
	      g_bow.pinch_g[1], g_bow.pinch_g[2], (double)c.size);
}

/* vh_arm (phase 2, after the arrow; build 18, SOMVR_BOW_STRING_DRAW=on): the bow's string for the layer (vrhands v5), on
 * the grips our hands are drawn with - the live limb tips (bo_nock_skel, the bow hand's grip frame) and between them the
 * nock's point of the string at rest, or the string hand while drawn (where the game's string node goes) */
static void bo_vh_string(const sa_xf g[2], const int valid[2], somvr_vrhands_frame *e)
{
	e->string_r = 0;
	if (!g_bow.on || !g_bow.tips_ok || g_bow.tid != my_tid() || !somvr_bow_string_shown(g_bow.string_draw, g_bow.s.phase))
		return; /* (SOMVR_BOW_STRING_DRAW=drawn: only nocked and drawn - the game's own string at rest) */
	int B = g_bow.cfg.bow_hand, S = 1 - B;
	if (!valid[B])
		return;
	sv_vec3 lo = sv_qrot(g[B].q, g_bow.tip_g[0]), hi = sv_qrot(g[B].q, g_bow.tip_g[1]), mid;
	lo = (sv_vec3){ g[B].p.x + lo.x, g[B].p.y + lo.y, g[B].p.z + lo.z };
	hi = (sv_vec3){ g[B].p.x + hi.x, g[B].p.y + hi.y, g[B].p.z + hi.z };
	sv_vec3 rest = { 0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z) };
	mid = rest;
	if (g_bow.s.phase == SOMVR_BOW_PH_DRAW && valid[S])
		mid = bo_string_pull(g); /* the string hand (build 19: on the rest plane while in front of it) */
	const sv_vec3 pts[3] = { lo, mid, hi };
	for (int k = 0; k < 3; k++) { /* the game convention (cm) -> XR (m), as vh_to_xr */
		if (!isfinite(pts[k].x + pts[k].y + pts[k].z))
			return;
		e->string[k][0] = pts[k].x * 0.01f, e->string[k][1] = pts[k].y * 0.01f, e->string[k][2] = -pts[k].z * 0.01f;
	}
	e->string_r = 0.0005f * g_bow.string_mm;
	g_bow.string_frames++;
}

