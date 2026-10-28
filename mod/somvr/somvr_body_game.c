/* somvr_body_game.c - the hands and the sword, the game side (included by somvr.c after the slots region; pure parts in
 * somvr_body.c; the findings in handoff/HANDOFF-hands.md):
 *   SOMVR_BODY=hands     only Talion's hands drawn: the vtable slot BD_REND_COPY (CSimpleModelRenderable::vf_02, the
 *                        output listener that gets the mapped skin palette, phase 7, game thread): for the player's main
 *                        renderable every bone outside the tracked hands is replaced in that palette itself (it draws the
 *                        owner's shared buffer and copies nothing; SOMVR_BODY_EDIT=auto|inplace|copy, word `hands edit W`;
 *                        run 161800). SOMVR_BODY_HIDE=line (default: every hidden vertex projected onto the line through
 *                        both wrists, no area, no seams; zero = the bind pose at the wrists, nan = the hands too, far =
 *                        1 km streaks; run 161800; round 3: cuff = a stub of the forearm (the twist node) rigid with
 *                        the hand, forearm = the whole forearm so, the line through the stubs' ends), word `hands hide W`. Needs SOMVR_ARMS=1 (the chains,
 *                        the IK) and SOMVR_BASE=head; while it edits, SOMVR_HEAD_PARTS=main draws the body node in the eyes
 *                        again (the put-away weapons and the cloth stay hidden). Third person, a flat frame, the body
 *                        moments: the whole body.
 *   SOMVR_SWORD_ANGLE=D  the sword's angle in the fist (deg, + = the blade up toward the knuckles' side, i.e. the grip's +y):
 *                        the drawn sword (the right weapon node's calibration) and the combat blade together. Trigger word
 *                        `hands angle D`.
 *   SOMVR_SWORD_GRIP_CM=D  build 15b's follow-up (headset marks #4/#5: the fist closed at the crossguard, the leather grip
 *                        out behind it): the drawn sword slid D cm ahead along its blade in the right fist (the weapon
 *                        node's calibration, before the angle), the combat blade's tip D cm farther with it; 0 = build 15
 *                        (the default). 8: the fist right under the guard (run 182336's pictures, `hands cal R 0 0 Z 90 0 0`).
 *   SOMVR_TWOHAND=1      the left hand squeezed on the hilt holds the sword with both hands (somvr_body.h 3; needs
 *                        SOMVR_ARMS=1 and SOMVR_SLOTS=1 for the sword's state).
 *   SOMVR_TWOHAND_HOLD_CM=LO,HI  (default 8,24; build 18's env 10.5,13) where the left fist sits on the hilt, cm behind
 *                        the right grip point (clamped there whatever the hands' distance): on the leather, not the pommel.
 *   SOMVR_HANDS_TEST=1   headless tests: `hands test L|R x y z [rx ry rz]` puts that grip at x right, y up, z ahead (cm) of
 *                        the head's yaw frame (deg about the grip's X, Y, Z; OpenXR's grip frame), `hands test off`;
 *                        `hands squeeze L|R V` holds a squeeze (0..1; -1 = the controller's again; SOMVR_CLIMB counts that
 *                        hand's grip as live while it does); `hands draw` = two quick right squeezes (the slots'
 *                        quick draw); `hands cal L|R x y z rx ry rz` = SOMVR_ARMS_CAL_* live. Never in a player's build.
 * Lines: `BODY site ...`, `CONFIG body ...`, `BODY keep ...`, `BODY t=... last 10 s: ...`, `TWOHAND ...`, `BODY END ...`.
 */

/* HANDS_SITES_BEGIN (SOMVR_BODY=hands: the patched slot BD_REND_COPY and three read-only proofs that the listener call
 * hands the skinning transforms in rcx and that vf_02 copies from there) */
static const struct site_spec g_bd_sites_game[] = {
SITE("BD_REND_COPY", K_VSLOT, 0x38de488, 0x38de480, "b0 fc 37 02 00 00 00 00 e0 fd 37 02 00 00 00 00 10 fd 37 02 00 00 00 00", 0x237fde0, "41 57 41 56 41 55 41 54 55 53 48 81 ec b8 00 00"),
SITE("BD_LISTEN_CALL", K_ENTRY, 0x237cf10, 0x237cf10, "44 89 f8 8b 0c 24 49 8b b6 88 03 00 00 48 89 da 49 8b bc c6 20 02 00 00 49 89 e9 41 83 c7 01 48 8b 07 51 4c 89 e9 41 54 44 8b 84 24 30 01 00 00 ff 50 10", 0x237cf10, "44 89 f8 8b 0c 24 49 8b b6 88 03 00 00 48 89"),
SITE("BD_COPY_SRC", K_ENTRY, 0x237fe07, 0x237fe07, "80 bf a4 00 00 00 00 48 89 cd 49 89 d4 48 89 fb", 0x237fe07, "80 bf a4 00 00 00 00 48 89 cd 49 89 d4 48 89"),
SITE("BD_COPY_LOOP", K_ENTRY, 0x2380078, 0x2380078, "31 c9 66 0f 1f 44 00 00 0f 28 4c 0d 00 0f 2b 0c 0a 8d 4f 10 48 89 cf 48 39 c8 77 ec", 0x2380078, "31 c9 66 0f 1f 44 00 00 0f 28 4c 0d 00 0f 2b"),
};
/* HANDS_SITES_END */
/* SOMVR_FINGERS (somvr_fingers_game.c, included after this file) */
static const char *bf_word(const char *a, const char *rest);
static void bf_bind(const sa_node *skin, const sa_node *model, uint32_t n);
static void bf_frame(void);
static void bf_config(int *bad);
static void bf_start(void);
static void bf_end_log(void);
enum { BDS_COPY, BDS_CALL, BDS_SRC, BDS_LOOP, BDS_N };
_Static_assert(sizeof g_bd_sites_game / sizeof g_bd_sites_game[0] == BDS_N, "body sites");
static const struct site_spec *g_bd_sites = g_bd_sites_game; /* selftest: stand-ins */
#define BD_REND_SLOT 2      /* BD_REND_COPY = slot 2 of the CSimpleModelRenderable vtable */
#define BD_LIVE_FRAMES 3    /* the last H4's first-person word holds for this many renderer frames */
#define BD_DEG 0.01745329252f
#define BD_R_OWNER 0x008    /* CSimpleModelRenderable: its owner (the caller's r14: +0x348 the shared skin buffer) */
#define BD_R_BUF 0x0b8      /* ... the skin buffer it draws (0x237fe77: the owner's shared one on the no-copy path) */
#define BD_R_MAP 0x1e8      /* ... its bone map (begin, end at +0x1f0): empty = no copy (0x237fe30) */
#define BD_O_SHARED 0x348   /* the owner: the shared skin buffer the caller maps and hands every listener (0x237cea6) */
enum { BD_EDIT_AUTO, BD_EDIT_INPLACE, BD_EDIT_COPY, BD_EDIT_N }; /* SOMVR_BODY_EDIT */
static const char *const g_bd_edit_w[BD_EDIT_N] = { "auto", "inplace", "copy" };
static const char *bd_edit_name(int e) { return e >= 0 && e < BD_EDIT_N ? g_bd_edit_w[e] : "?"; }

typedef uint64_t (*bd_copy_fn)(void *self, void *skel, void *mpose, void *skin, uint64_t count, void *hdr, uint64_t a7,
			       uint64_t a8);

struct bd_cnt {
	uint64_t calls, player, edits, inplace, badwrite, nolive, notready, badread, other, shown[2], hidden[2];
};
static struct {
	int want, on, mode, edit, tid, twohand, test;
	uintptr_t vt_rend;            /* the CSimpleModelRenderable vtable (BD_REND_COPY - 8 x 2) */
	uintptr_t orig;
	/* H4 -> the next phase 7 */
	_Atomic uintptr_t rend;       /* the player's main renderable (model+0xb0) */
	_Atomic uint64_t live_frame;  /* the renderer frame of the last first-person H4 (0 = none) */
	uint64_t edit_seq, h4_seq;    /* phase 7 edits since the last H4 */
	int drawn;                    /* this H4: the body was edited this frame (SOMVR_HEAD_PARTS=main draws it) */
	/* the keep set, built for the arms' skeleton */
	uintptr_t keep_skel;
	uint32_t keep_nodes;
	int keep_ok, keep_logged;
	sb_keep keep;
	sa_node skin[SA_MAXN] __attribute__((aligned(16)));
	sa_node model[SA_MAXN] __attribute__((aligned(16)));
	/* the sword angle */
	float angle_deg, pitch0;
	float grip_cm, len0;    /* SOMVR_SWORD_GRIP_CM; the combat blade's length before it (taken once) */
	int len_set;
	float cal_v[2][6];
	/* two hands (phase 2) */
	sb_tw_cfg tw;
	sb_tw_state tws;
	sb_tw_out two;
	/* SOMVR_HANDS_TEST */
	int t_on[2];
	float t_v[2][6];
	float t_sq[2];
	double t_draw;
	/* logs */
	double next_log;
	struct bd_cnt n, w;
	int lines;
} g_bd = { .mode = SB_HIDE_LINE };
#define BD_COUNT(f) (g_bd.n.f++, g_bd.w.f++)
/* the writer thread's words -> the game thread */
static _Atomic uint32_t g_bd_req_seq, g_bd_req_mode = SB_HIDE_N, g_bd_req_edit = BD_EDIT_N;
static _Atomic uint32_t g_bd_req_angle, g_bd_req_angle_set; /* float bits; 1 once a word set it (0 deg too) */
static _Atomic uint32_t g_bd_req_hold, g_bd_req_hold_lo, g_bd_req_hold_hi; /* build 18: `hands twohold LO HI` (float bits) */
static pthread_mutex_t g_bd_mu = PTHREAD_MUTEX_INITIALIZER; /* the test poses, squeezes and calibrations */
static struct { int seq, on[2], cal_set[2], draw; float v[2][6], sq[2], cal[2][6]; } g_bd_req;

/* ---- SOMVR_BODY=hands: the render node's copy (game thread, phase 7) ----------------------------------------------- */

/* the keep set for the arms' current skeleton (their chains), built again on a new skeleton */
static int bd_keep_ready(uint32_t cnt)
{
	if (!g_arm.chains_ok || g_arm.nodes != cnt)
		return 0;
	if (g_bd.keep_skel == g_arm.skel && g_bd.keep_nodes == cnt)
		return g_bd.keep_ok;
	static uint8_t parent[SA_MAXN];
	static uint32_t hash[SA_MAXN];
	uint32_t n = 0;
	char why[240];
	const char *bad = skel_read(g_arm.skel, cnt, parent, hash, &n);
	g_bd.keep_skel = g_arm.skel, g_bd.keep_nodes = cnt;
	g_bd.keep_ok = !bad && sb_keep_build(&g_bd.keep, g_arm.ch, parent, (int)n, why, sizeof why) == 0;
	if (g_bd.keep_logged < 8) {
		g_bd.keep_logged++;
		logf_("BODY keep skel=%#lx nodes=%u: %s", (unsigned long)g_arm.skel, cnt, bad ? bad : why);
	}
	return g_bd.keep_ok;
}

/* n bytes to a, or 0 if they are not mapped writable (one syscall; never faults) */
static int bd_poke(uintptr_t a, const void *in, size_t n)
{
	struct iovec l = { (void *)in, n }, rm = { (void *)a, n };
	return a && syscall(SYS_process_vm_writev, (long)getpid(), &l, 1L, &rm, 1L, 0L) == (long)n;
}

/* vf_02's path for this renderable: 1 = it draws the owner's shared skin buffer (an empty bone map +0x1e8 == +0x1f0;
 * 0x237fe30: no copy), 0 = it copies `skin` through its bone map into its own buffer, -1 = unread. A line on a change */
static int bd_path(uintptr_t self, uint64_t a8)
{
	uintptr_t m[2] = { 0, 0 };
	int p = peek(self + BD_R_MAP, m, sizeof m) ? m[0] == m[1] : -1;
	static uintptr_t last_self, last_buf;
	static int last = -2, lines;
	uintptr_t own = peek_ptr(self + BD_R_OWNER), buf = peek_ptr(self + BD_R_BUF), shb = own ? peek_ptr(own + BD_O_SHARED) : 0;
	if ((p != last || self != last_self || buf != last_buf) && lines < 12) {
		lines++;
		logf_("BODY path t=%.3f the player's renderable %#lx: bone map %#lx..%#lx (%s), its skin buffer %#lx, its owner %#lx's "
		      "shared buffer %#lx (%s); the copy flag %llu; SOMVR_BODY_EDIT=%s", mono(), (unsigned long)self,
		      (unsigned long)m[0], (unsigned long)m[1], p == 1 ? "empty: it draws the shared buffer, no copy" : p == 0 ?
		      "a bone map: it copies the skin" : "unread", (unsigned long)buf, (unsigned long)own, (unsigned long)shb,
		      buf && buf == shb ? "the same" : "another", (unsigned long long)(a8 & 0xff), bd_edit_name(g_bd.edit));
	}
	last = p, last_self = self, last_buf = buf;
	return p;
}

static uint64_t bd_copy(void *self, void *skel, void *mpose, void *skin, uint64_t count, void *hdr, uint64_t a7, uint64_t a8)
{
	bd_copy_fn f = (bd_copy_fn)g_bd.orig;
	BD_COUNT(calls);
	if (!self || (uintptr_t)self != atomic_load_explicit(&g_bd.rend, memory_order_relaxed) || !g_bd.on)
		return f(self, skel, mpose, skin, count, hdr, a7, a8);
	BD_COUNT(player);
	uint32_t n = (uint32_t)count;
	uint64_t lf = atomic_load_explicit(&g_bd.live_frame, memory_order_relaxed);
	if (my_tid() != g_bd.tid) {
		BD_COUNT(other);
		return f(self, skel, mpose, skin, count, hdr, a7, a8);
	}
	if (!lf || rend_frame() - lf > BD_LIVE_FRAMES) {
		BD_COUNT(nolive);
		return f(self, skel, mpose, skin, count, hdr, a7, a8);
	}
	if (n < 2 || n > SA_MAXN || !bd_keep_ready(n)) {
		BD_COUNT(notready);
		return f(self, skel, mpose, skin, count, hdr, a7, a8);
	}
	if (!skin || !mpose || !peek((uintptr_t)skin, g_bd.skin, n * sizeof(sa_node)) ||
	    !peek((uintptr_t)mpose, g_bd.model, n * sizeof(sa_node))) {
		BD_COUNT(badread);
		return f(self, skel, mpose, skin, count, hdr, a7, a8);
	}
	unsigned show = 0;
	for (int s = 0; s < 2; s++) { /* a hand the IK holds is drawn; a lost one goes with the body */
		int on = g_arm.on && g_arm.weight[s] > 0.01f;
		show |= (unsigned)on << s;
		if (on)
			BD_COUNT(shown[s]);
		else
			BD_COUNT(hidden[s]);
	}
	bf_bind(g_bd.skin, g_bd.model, n); /* `hands fingers`: the bind pose (the hands' palette is never edited) */
	sa_node pre[2] = { g_bd.skin[0], g_bd.skin[g_bd.keep.wrist[1]] }; /* the game's, for the first lines */
	/* SOMVR_BOW_GHOST (build 11): while the bow is out the wraith (the bow mode's MaterialTransform draws Talion's body as
	 * Celebrimbor) shares this palette: its own mode then (our VR hands are drawn, the game's hands are not needed) */
	int mode = bo_ghost_mode(g_bd.mode, &show);
	if (sb_skin_edit(g_bd.skin, g_bd.model, (int)n, &g_bd.keep, show, mode) <= 0 && mode != SB_HIDE_SHOW)
		return f(self, skel, mpose, skin, count, hdr, a7, a8);
	/* where vf_02 takes the palette from (handoff/HANDOFF-hands.md, "the palette"): a renderable with an empty bone map
	 * draws its owner's shared skin buffer, the one the caller mapped and hands in as `skin`, and copies nothing (run
	 * 154417: the copy changed nothing); one with a bone map copies `skin` into its own buffer */
	int path = bd_path((uintptr_t)self, a8);
	int inplace = g_bd.edit == BD_EDIT_INPLACE || (g_bd.edit == BD_EDIT_AUTO && path == 1);
	if (inplace && mode != SB_HIDE_SHOW) {
		if (!bd_poke((uintptr_t)skin, g_bd.skin, n * sizeof(sa_node))) {
			BD_COUNT(badwrite);
			return f(self, skel, mpose, skin, count, hdr, a7, a8);
		}
		BD_COUNT(inplace);
	}
	BD_COUNT(edits);
	g_bd.edit_seq = g_bd.h4_seq + 1;
	if (g_bd.lines < 6) {
		g_bd.lines++;
		const sa_node *wl = &g_bd.model[g_bd.keep.wrist[0]], *wr = &g_bd.model[g_bd.keep.wrist[1]];
		const sa_node *m0 = &g_bd.model[0];
		logf_("BODY t=%.3f edit %u nodes (%s, %s, hands shown L=%u R=%u): wrists L %.1f,%.1f,%.1f R %.1f,%.1f,%.1f (model); "
		      "the game's skin: node 0 pos %.2f,%.2f,%.2f q %.3f,%.3f,%.3f,%.3f (its model pos %.2f,%.2f,%.2f q %.3f,%.3f,%.3f,"
		      "%.3f), the right wrist pos %.2f,%.2f,%.2f q %.3f,%.3f,%.3f,%.3f", mono(), n, sb_hide_name(g_bd.mode),
		      inplace ? "in place" : "a copy", show & 1, show >> 1 & 1, wl->pos[0], wl->pos[1], wl->pos[2], wr->pos[0],
		      wr->pos[1], wr->pos[2], pre[0].pos[0], pre[0].pos[1], pre[0].pos[2], pre[0].q[0], pre[0].q[1], pre[0].q[2],
		      pre[0].q[3], m0->pos[0], m0->pos[1], m0->pos[2], m0->q[0], m0->q[1], m0->q[2], m0->q[3], pre[1].pos[0],
		      pre[1].pos[1], pre[1].pos[2], pre[1].q[0], pre[1].q[1], pre[1].q[2], pre[1].q[3]);
	}
	return f(self, skel, mpose, inplace ? skin : g_bd.skin, count, hdr, a7, a8);
}

/* the frame's body edits for SOMVR_HEAD_PARTS=main (hp_frame, the same H4): 1 = the body node is drawn in the eyes */
static int bd_body_drawn(void)
{
	return g_bd.on && g_bd.drawn && !vh_hides(); /* SOMVR_VRHANDS: the layer draws ours, the body node stays hidden */
}

/* ---- the sword angle and the calibrations ------------------------------------------------------------------------- */

static void bd_apply_cal(void)
{
	for (int h = 0; h < 2; h++) {
		sa_xf c = sa_xf_euler(g_bd.cal_v[h]);
		g_arm.cal[h] = h == SA_R ? sb_cal_angle(sb_cal_grip(c, g_bd.grip_cm), g_bd.angle_deg * BD_DEG) : c;
		memcpy(g_arm.cal_v[h], g_bd.cal_v[h], sizeof g_bd.cal_v[h]);
	}
	g_cb.bcfg.pitch_rad = g_bd.pitch0 + g_bd.angle_deg * BD_DEG;
}

/* SOMVR_SWORD_GRIP_CM: the drawn sword slid ahead in the fist, the combat blade's tip with it (its length taken once) */
static void bd_grip_blade(void)
{
	if (!g_bd.len_set)
		g_bd.len0 = g_cb.bcfg.len_cm, g_bd.len_set = 1;
	g_cb.bcfg.len_cm = g_bd.len0 + g_bd.grip_cm;
}

/* the VR menu's Sword angle row (vm_apply_key, vm_start): the settings' sword_deg is SOMVR_SWORD_ANGLE, live */
static const char *bd_vm_set(int k, const somvr_settings *s)
{
	if (k != SOMVR_SK_SWORD_ANGLE)
		return "";
	if (!g_arm.on)
		return " (SOMVR_ARMS is off: no sword in the hand to turn)";
	float d = s->sword_deg < -90 ? -90 : s->sword_deg > 90 ? 90 : s->sword_deg;
	if (d != g_bd.angle_deg) {
		logf_("BODY t=%.3f sword angle %.1f -> %.1f deg (the VR menu)", mono(), g_bd.angle_deg, d);
		g_bd.angle_deg = d;
		bd_apply_cal();
	}
	return " (the drawn sword and the combat blade)";
}
static void bd_vm_settings(somvr_settings *s) { s->sword_deg = g_bd.angle_deg; }
static uint32_t bd_vm_avail(void) { return g_arm.on ? SOMVR_VMA_SWORD : 0u; }

/* the sword is in the right hand (SOMVR_SLOTS' state: drawn, or being drawn by our slot) */
static int bd_sword_in_hand(void)
{
	return g_sl.on && g_sl.have_g && sl_sword_held(&g_sl.g); /* (build 10: not while kept on the back) */
}

/* ---- SOMVR_HANDS_TEST ----------------------------------------------------------------------------------------------- */

static int bd_swing_hand(int s, int64_t time, int log, sv_quat qy, sv_vec3 *o, sv_quat *q, float lin[3], float ang[3]);

/* the test grips over the located ones (game thread: in_frame's and the arms' locates at their time; log: in_frame's) */
static void bd_test_hands(somvr_hand h[2], int64_t time, int log)
{
	if (!g_bd.test || (!g_bd.t_on[0] && !g_bd.t_on[1]))
		return;
	const somvr_posef *e = g_fr.views.pose;
	float hp[3] = { 0.5f * (e[0].p[0] + e[1].p[0]), 0.5f * (e[0].p[1] + e[1].p[1]), 0.5f * (e[0].p[2] + e[1].p[2]) };
	sv_quat hq = sv_qnormalize(sv_quat_of(e[0].q));
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 }); /* OpenXR: forward = -Z */
	float yaw = atan2f(-f.x, -f.z);                  /* about +Y (OpenXR, right-handed) */
	sv_quat qy = { 0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	for (int s = 0; s < 2; s++) {
		if (!g_bd.t_on[s])
			continue;
		const float *v = g_bd.t_v[s];
		sv_vec3 o = sv_qrot(qy, (sv_vec3){ 0.01f * v[0], 0.01f * v[1], -0.01f * v[2] });
		sv_quat q = sv_qmul(qy, sa_xf_euler((float[6]){ 0, 0, 0, v[3], v[4], v[5] }).q);
		somvr_hand *d = &h[s];
		memset(d->lin, 0, sizeof d->lin), memset(d->ang, 0, sizeof d->ang);
		bd_swing_hand(s, time, log, qy, &o, &q, d->lin, d->ang); /* `hands swing`: its pose and velocity now */
		d->valid = 1, d->tracked = 1, d->src = SOMVR_HSRC_LOCATED, d->vel_valid = 3, d->result = 0;
		d->time = time; /* the locate's time (headless: no controller, so the locate left 0): the blade's clock */
		d->pose.p[0] = hp[0] + o.x, d->pose.p[1] = hp[1] + o.y, d->pose.p[2] = hp[2] + o.z;
		sv_quat_to(sv_qnormalize(q), d->pose.q);
		hsm_test_noise(s, d); /* `hands noise DEG MM`: tracking noise on the test hands (SOMVR_HANDS_SMOOTH's proof) */
	}
}

/* ---- SOMVR_HANDS_TEST: the scripted swing `hands swing L|R x y z rx ry rz MS` (combat tests) ------------------------
 * From where the test grip is now to the pose given (the `hands test` frame) in MS ms of the views' time, so frame by
 * frame at the game's rate (Monado's null compositor: 20 Hz): the minimum-jerk timing, an arc about that side's
 * shoulder, the orientation slerped (sc_swing_pose); the grip's velocity from the pose 5 ms before. The hand stays at
 * the end pose. A `hands test` of that hand (or `hands test off`) cancels a swing. An optional 9th number OPEN (0..1):
 * the hand's test squeeze opens (0) in the frame the swing passes u = OPEN (SOMVR_CLIMB_FLING: a release mid-pull, at
 * the frame's own time, which the 250 ms trigger words cannot place; 0 = none); a 10th CATCH (0..1]: the other hand's
 * test squeeze closes (1) in the frame the swing passes u = CATCH (a grip during the flight) */
#define BD_SW_SHOULDER_X 18.0f  /* the pivot: the shoulder, cm right (left: -), up, ahead of the head's yaw frame */
#define BD_SW_SHOULDER_Y -25.0f
#define BD_SW_SHOULDER_Z -8.0f
#define BD_SW_VEL_NS 5000000    /* the velocity's step */
static struct {
	int on[2], frames[2], lines;
	float pa[2][3], qa[2][4], pb[2][3], qb[2][4];
	int64_t t0[2], dur[2], logged[2];
	float peak[2]; /* the grip's peak speed (m/s) in the logged frames */
	float open_u[2]; /* > 0: the test squeeze opens at this u (once) */
	float catch_u[2]; /* > 0: the other hand's test squeeze closes at this u (once) */
} g_bd_sw;
static struct { int n[2]; float to[2][6], ms[2], open[2], catch_[2]; } g_bd_swreq; /* the writer thread's words (g_bd_mu) */

/* the pose (cm, the yaw frame's quaternion) of test hand s at time (a running swing's, else its test pose) */
static void bd_swing_at(int s, int64_t time, float p[3], float q[4])
{
	if (g_bd_sw.on[s]) {
		float u = g_bd_sw.dur[s] > 0 ? (float)((double)(time - g_bd_sw.t0[s]) / (double)g_bd_sw.dur[s]) : 1.0f;
		static const float pivot[2][3] = { { -BD_SW_SHOULDER_X, BD_SW_SHOULDER_Y, BD_SW_SHOULDER_Z },
						   { BD_SW_SHOULDER_X, BD_SW_SHOULDER_Y, BD_SW_SHOULDER_Z } };
		if (sc_swing_pose(g_bd_sw.pa[s], g_bd_sw.qa[s], g_bd_sw.pb[s], g_bd_sw.qb[s], pivot[s], u, p, q))
			return;
	}
	memcpy(p, g_bd.t_v[s], 3 * sizeof(float));
	sv_quat_to(sa_xf_euler((float[6]){ 0, 0, 0, g_bd.t_v[s][3], g_bd.t_v[s][4], g_bd.t_v[s][5] }).q, q);
}

/* bd_test_hands: a running swing's offset o and orientation q (OpenXR, the head's yaw qy applied) and velocity */
static int bd_swing_hand(int s, int64_t time, int log, sv_quat qy, sv_vec3 *o, sv_quat *q, float lin[3], float ang[3])
{
	if (!g_bd_sw.on[s] || time <= 0)
		return 0;
	float p[2][3], r[2][4];
	bd_swing_at(s, time, p[0], r[0]);
	bd_swing_at(s, time - BD_SW_VEL_NS, p[1], r[1]);
	sv_vec3 x[2];
	sv_quat y[2];
	for (int i = 0; i < 2; i++) {
		x[i] = sv_qrot(qy, (sv_vec3){ 0.01f * p[i][0], 0.01f * p[i][1], -0.01f * p[i][2] });
		y[i] = sv_qnormalize(sv_qmul(qy, sv_quat_of(r[i])));
	}
	*o = x[0], *q = y[0];
	const float k = 1e9f / BD_SW_VEL_NS;
	lin[0] = (x[0].x - x[1].x) * k, lin[1] = (x[0].y - x[1].y) * k, lin[2] = (x[0].z - x[1].z) * k;
	sv_quat dq = sv_qmul(y[0], sv_qconj(y[1])); /* the turn over the step, in the tracking space */
	if (dq.w < 0)
		dq.x = -dq.x, dq.y = -dq.y, dq.z = -dq.z, dq.w = -dq.w;
	float sn = sqrtf(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z), a = 2 * atan2f(sn, dq.w);
	if (sn > 1e-9f)
		ang[0] = dq.x / sn * a * k, ang[1] = dq.y / sn * a * k, ang[2] = dq.z / sn * a * k;
	float u = (float)((double)(time - g_bd_sw.t0[s]) / (double)(g_bd_sw.dur[s] > 0 ? g_bd_sw.dur[s] : 1));
	if (!log || time == g_bd_sw.logged[s])
		return 1;
	g_bd_sw.logged[s] = time, g_bd_sw.frames[s]++;
	float sp = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);
	if (sp > g_bd_sw.peak[s])
		g_bd_sw.peak[s] = sp;
	if (g_bd_sw.lines++ < 400)
		logf_("BODY test swing %s frame %d u=%.2f: the grip at %.1f,%.1f,%.1f cm (the head's yaw frame), %.2f m/s, %.0f deg/s",
		      g_arm_side[s], g_bd_sw.frames[s], u, p[0][0], p[0][1], p[0][2], sp,
		      sqrtf(ang[0] * ang[0] + ang[1] * ang[1] + ang[2] * ang[2]) * 57.29578f);
	if (g_bd_sw.open_u[s] > 0 && u >= g_bd_sw.open_u[s]) { /* the release mid-swing: the test squeeze 0 from now */
		g_bd_sw.open_u[s] = 0;
		pthread_mutex_lock(&g_bd_mu);
		g_bd_req.sq[s] = 0;
		pthread_mutex_unlock(&g_bd_mu);
		g_bd.t_sq[s] = 0;
		logf_("BODY test swing %s frame %d u=%.2f: the squeeze opened (%.2f m/s)", g_arm_side[s], g_bd_sw.frames[s], u, sp);
	}
	if (g_bd_sw.catch_u[s] > 0 && u >= g_bd_sw.catch_u[s]) { /* the other hand grips now */
		g_bd_sw.catch_u[s] = 0;
		pthread_mutex_lock(&g_bd_mu);
		g_bd_req.sq[1 - s] = 1;
		pthread_mutex_unlock(&g_bd_mu);
		g_bd.t_sq[1 - s] = 1;
		logf_("BODY test swing %s frame %d u=%.2f: the %s squeeze closed", g_arm_side[s], g_bd_sw.frames[s], u,
		      g_arm_side[1 - s]);
	}
	if (u >= 1) {
		g_bd_sw.on[s] = 0; /* at its end pose: the test pose (the word set it) */
		logf_("BODY test swing %s end: %d frames, the grip's peak %.2f m/s", g_arm_side[s], g_bd_sw.frames[s],
		      g_bd_sw.peak[s]);
	}
	return 1;
}

/* bd_word (g_bd_mu held): "swing L|R x y z rx ry rz MS [OPEN [CATCH]]" */
static const char *bd_swing_word(const char *w)
{
	char side = 0;
	float v[9] = { 0 };
	int k = sscanf(w, " %c %f %f %f %f %f %f %f %f %f", &side, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
		       &v[8]);
	if (k < 8 || k > 10 || (side != 'L' && side != 'R'))
		return "hands swing (not L|R x y z rx ry rz MS [OPEN [CATCH]]: ignored)";
	if (k >= 9 && !(v[7] >= 0 && v[7] < 1))
		return "hands swing (OPEN not 0 or between 0 and 1: ignored)";
	if (k == 10 && !(v[8] > 0 && v[8] <= 1))
		return "hands swing (CATCH not in (0, 1]: ignored)";
	int s = side == 'R';
	for (int i = 0; i < 6; i++)
		if (!(fabsf(v[i]) <= 400))
			return "hands swing (a number out of -400..400: ignored)";
	if (!(v[6] >= 10 && v[6] <= 5000))
		return "hands swing (MS not 10..5000: ignored)";
	if (!g_bd_req.on[s])
		return "hands swing (that hand has no test pose: `hands test` first; ignored)";
	memcpy(g_bd_swreq.to[s], v, sizeof g_bd_swreq.to[s]), g_bd_swreq.ms[s] = v[6], g_bd_swreq.n[s]++;
	g_bd_swreq.open[s] = k >= 9 ? v[7] : 0, g_bd_swreq.catch_[s] = k == 10 ? v[8] : 0;
	memcpy(g_bd_req.v[s], v, sizeof g_bd_req.v[s]), g_bd_req.seq++; /* its end pose = the hand's test pose */
	return "hands swing";
}

/* bd_take_words (g_bd_mu held, before the test poses are copied): a new swing starts where the hand is now; a test
 * word for a swinging hand cancels its swing */
static void bd_swing_take(void)
{
	static int seen[2];
	int64_t now = g_fr.views.time;
	for (int s = 0; s < 2; s++) {
		if (g_bd_swreq.n[s] != seen[s]) {
			seen[s] = g_bd_swreq.n[s];
			float p[3], q[4];
			bd_swing_at(s, now, p, q);
			memcpy(g_bd_sw.pa[s], p, sizeof p), memcpy(g_bd_sw.qa[s], q, sizeof q);
			memcpy(g_bd_sw.pb[s], g_bd_swreq.to[s], sizeof g_bd_sw.pb[s]);
			sv_quat_to(sa_xf_euler((float[6]){ 0, 0, 0, g_bd_swreq.to[s][3], g_bd_swreq.to[s][4], g_bd_swreq.to[s][5] }).q,
				   g_bd_sw.qb[s]);
			g_bd_sw.t0[s] = now, g_bd_sw.dur[s] = (int64_t)(g_bd_swreq.ms[s] * 1e6f), g_bd_sw.on[s] = now > 0;
			g_bd_sw.frames[s] = 0, g_bd_sw.peak[s] = 0, g_bd_sw.logged[s] = now;
			g_bd_sw.open_u[s] = g_bd_sw.on[s] ? g_bd_swreq.open[s] : 0;
			g_bd_sw.catch_u[s] = g_bd_sw.on[s] ? g_bd_swreq.catch_[s] : 0;
			const float *b = g_bd_swreq.to[s];
			logf_("BODY test swing %s: from %.1f,%.1f,%.1f to %g,%g,%g cm %g,%g,%g deg in %g ms (the head's yaw frame; an "
			      "arc about the shoulder, the views' time%s)", g_arm_side[s], p[0], p[1], p[2], b[0], b[1], b[2], b[3],
			      b[4], b[5], g_bd_swreq.ms[s], now > 0 ? "" : ": NONE YET, at the end pose at once");
		} else if (g_bd_sw.on[s] && (!g_bd_req.on[s] || memcmp(g_bd_req.v[s], g_bd_swreq.to[s], sizeof g_bd_req.v[s]))) {
			g_bd_sw.on[s] = 0;
			logf_("BODY test swing %s cancelled after %d frames (a test word)", g_arm_side[s], g_bd_sw.frames[s]);
		}
	}
}

/* the squeeze the slots and the two hands see (a test squeeze over the controller's) */
static float bd_squeeze(int h, float v)
{
	if (!g_bd.test || h < 0 || h > 1)
		return v;
	double t = cb_now();
	if (h == SA_R && g_bd.t_draw > 0) { /* two quick presses: 0-0.08 s, 0.16-0.24 s (the quick draw, 0.35 s) */
		double a = t - g_bd.t_draw;
		if (a > 0.6)
			g_bd.t_draw = 0;
		return (a < 0.08 || (a >= 0.16 && a < 0.24)) ? 1.0f : 0.0f;
	}
	return g_bd.t_sq[h] >= 0 ? g_bd.t_sq[h] : v;
}

/* a test squeeze drives hand h now (`hands squeeze`, the right's `hands draw`): SOMVR_CLIMB counts its grip as live
 * then, as if the controller's inputs were active (headless: no controller) */
static int bd_test_squeezing(int h)
{
	return g_bd.test && h >= 0 && h <= 1 && (g_bd.t_sq[h] >= 0 || (h == SA_R && g_bd.t_draw > 0));
}

/* the writer thread: "hands ..." (one word at a time; NULL = not ours) */
static const char *bd_word(const char *w)
{
	char a[16] = "", side = 0;
	float v[6] = { 0, 0, 0, 0, 0, 0 };
	int k = 0;
	if (sscanf(w, "%15s", a) != 1)
		return "hands (no word)";
	const char *fw = bf_word(a, strstr(w, a) + strlen(a)); /* SOMVR_FINGERS: fingers, trigger, thumb */
	if (fw)
		return fw;
	if (!strcmp(a, "hide")) {
		char m[16] = "";
		int md = sscanf(w + 4, "%15s", m) == 1 ? sb_hide_parse(m) : -1;
		if (md < 0)
			return "hands hide (not line|cuff|cuffik|forearm|zero|nan|far|show: ignored)";
		atomic_store(&g_bd_req_mode, (uint32_t)md);
		atomic_fetch_add(&g_bd_req_seq, 1);
		return "hands hide";
	}
	if (!strcmp(a, "edit")) {
		char m[16] = "";
		int e = -1;
		if (sscanf(w + 4, "%15s", m) == 1)
			for (int i = 0; i < BD_EDIT_N; i++)
				if (!strcmp(m, g_bd_edit_w[i]))
					e = i;
		if (e < 0)
			return "hands edit (not auto|inplace|copy: ignored)";
		atomic_store(&g_bd_req_edit, (uint32_t)e);
		atomic_fetch_add(&g_bd_req_seq, 1);
		return "hands edit";
	}
	if (!strcmp(a, "angle")) {
		float d;
		if (sscanf(w + 5, "%f", &d) != 1 || !(d >= -90 && d <= 90))
			return "hands angle (not -90..90 deg: ignored)";
		uint32_t b;
		memcpy(&b, &d, 4);
		atomic_store(&g_bd_req_angle, b), atomic_store(&g_bd_req_angle_set, 1);
		atomic_fetch_add(&g_bd_req_seq, 1);
		return "hands angle";
	}
	if (!g_bd.test)
		return "hands (SOMVR_HANDS_TEST is off: ignored)";
	pthread_mutex_lock(&g_bd_mu);
	const char *what = "hands (not test|swing|noise|wiggle|weighthand|twohold|squeeze|draw|cal|hide|angle: ignored)";
	if (!strcmp(a, "swing")) {
		what = bd_swing_word(w + 5);
	} else if (!strcmp(a, "noise")) {
		what = hsm_noise_word(w + 5);
	} else if (!strcmp(a, "wiggle")) {
		what = hsm_wiggle_word(w + 6); /* SOMVR_SWORD_WEIGHT's proof (somvr_smooth_game.c) */
	} else if (!strcmp(a, "weighthand")) {
		what = hsm_hand_word(w + 10); /* SOMVR_SWORD_WEIGHT_HAND live (somvr_smooth_game.c) */
	} else if (!strcmp(a, "twohold")) { /* build 18: SOMVR_TWOHAND_HOLD_CM live (the pictures) */
		float lo, hi;
		if (sscanf(w + 7, " %f %f", &lo, &hi) == 2 && lo >= 4 && hi <= 30 && lo <= hi) {
			uint32_t bl, bh;
			memcpy(&bl, &lo, 4), memcpy(&bh, &hi, 4);
			atomic_store(&g_bd_req_hold_lo, bl), atomic_store(&g_bd_req_hold_hi, bh), atomic_store(&g_bd_req_hold, 1);
			what = "hands twohold";
		} else {
			what = "hands twohold (not LO HI, 4 <= LO <= HI <= 30 cm: ignored)";
		}
	} else if (!strcmp(a, "draw")) {
		g_bd_req.draw = 1, g_bd_req.seq++, what = "hands draw";
	} else if (!strcmp(a, "test") && strstr(w, "off")) {
		g_bd_req.on[0] = g_bd_req.on[1] = 0, g_bd_req.seq++, what = "hands test off";
	} else if ((!strcmp(a, "test") || !strcmp(a, "cal") || !strcmp(a, "squeeze")) &&
		   (k = sscanf(w + strlen(a), " %c %f %f %f %f %f %f", &side, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5])) >= 2 &&
		   (side == 'L' || side == 'R')) {
		int s = side == 'R';
		if (!strcmp(a, "squeeze"))
			g_bd_req.sq[s] = v[0] < 0 ? -1 : v[0] > 1 ? 1 : v[0], what = "hands squeeze";
		else if (!strcmp(a, "test") && k >= 4)
			memcpy(g_bd_req.v[s], v, sizeof v), g_bd_req.on[s] = 1, what = "hands test";
		else if (!strcmp(a, "cal") && k == 7)
			memcpy(g_bd_req.cal[s], v, sizeof v), g_bd_req.cal_set[s] = 1, what = "hands cal";
		else
			what = "hands (too few numbers: ignored)";
		g_bd_req.seq++;
	}
	pthread_mutex_unlock(&g_bd_mu);
	return what;
}

/* the game thread takes the words (H4) */
static void bd_take_words(void)
{
	static uint32_t seen;
	static int seen_t;
	uint32_t sq = atomic_load(&g_bd_req_seq);
	if (sq != seen) {
		seen = sq;
		uint32_t md = atomic_load(&g_bd_req_mode);
		if (md < SB_HIDE_N && (int)md != g_bd.mode) {
			logf_("BODY t=%.3f hide %s -> %s (trigger word)", mono(), sb_hide_name(g_bd.mode), sb_hide_name((int)md));
			g_bd.mode = (int)md;
		}
		uint32_t ed = atomic_load(&g_bd_req_edit);
		if (ed < BD_EDIT_N && (int)ed != g_bd.edit) {
			logf_("BODY t=%.3f edit %s -> %s (trigger word)", mono(), bd_edit_name(g_bd.edit), bd_edit_name((int)ed));
			g_bd.edit = (int)ed;
		}
		uint32_t b = atomic_load(&g_bd_req_angle);
		float d;
		memcpy(&d, &b, 4);
		if (atomic_load(&g_bd_req_angle_set) && d != g_bd.angle_deg && d >= -90 && d <= 90) {
			logf_("BODY t=%.3f sword angle %.1f -> %.1f deg (trigger word)", mono(), g_bd.angle_deg, d);
			g_bd.angle_deg = d;
			bd_apply_cal();
		}
	}
	if (!g_bd.test)
		return;
	pthread_mutex_lock(&g_bd_mu);
	if (g_bd_req.seq != seen_t) {
		seen_t = g_bd_req.seq;
		bd_swing_take(); /* `hands swing`: from the pose before this copy */
		for (int s = 0; s < 2; s++) {
			if (g_bd.t_on[s] != g_bd_req.on[s] || memcmp(g_bd.t_v[s], g_bd_req.v[s], sizeof g_bd.t_v[s]))
				logf_("BODY test %s %s %g,%g,%g cm %g,%g,%g deg (the head's yaw frame)", g_arm_side[s],
				      g_bd_req.on[s] ? "at" : "off", g_bd_req.v[s][0], g_bd_req.v[s][1], g_bd_req.v[s][2],
				      g_bd_req.v[s][3], g_bd_req.v[s][4], g_bd_req.v[s][5]);
			g_bd.t_on[s] = g_bd_req.on[s];
			memcpy(g_bd.t_v[s], g_bd_req.v[s], sizeof g_bd.t_v[s]);
			if (g_bd.t_sq[s] != g_bd_req.sq[s])
				logf_("BODY test squeeze %s %.2f", g_arm_side[s], g_bd_req.sq[s]);
			g_bd.t_sq[s] = g_bd_req.sq[s];
			if (g_bd_req.cal_set[s]) {
				g_bd_req.cal_set[s] = 0;
				memcpy(g_bd.cal_v[s], g_bd_req.cal[s], sizeof g_bd.cal_v[s]);
				bd_apply_cal();
				logf_("BODY test cal %s = %g,%g,%g,%g,%g,%g", g_arm_side[s], g_bd.cal_v[s][0], g_bd.cal_v[s][1],
				      g_bd.cal_v[s][2], g_bd.cal_v[s][3], g_bd.cal_v[s][4], g_bd.cal_v[s][5]);
			}
		}
		if (g_bd_req.draw) {
			g_bd_req.draw = 0, g_bd.t_draw = cb_now();
			logf_("BODY test draw: two quick right squeezes");
		}
	}
	pthread_mutex_unlock(&g_bd_mu);
}

/* ---- SOMVR_TWOHAND (phase 2, arm_eval: the grips before the targets) ---------------------------------------------- */

static void bd_twohand(sa_xf g[2], const int valid[2], const int bowed[2], float dt)
{
	if (!g_bd.twohand)
		return;
	if (atomic_exchange(&g_bd_req_hold, 0)) { /* `hands twohold LO HI` (build 18) */
		uint32_t bl = atomic_load(&g_bd_req_hold_lo), bh = atomic_load(&g_bd_req_hold_hi);
		memcpy(&g_bd.tw.hold_lo_cm, &bl, 4), memcpy(&g_bd.tw.hold_hi_cm, &bh, 4);
		logf_("TWOHAND t=%.3f the left hand held %.1f..%.1f cm behind the right grip (trigger word)", cb_now(),
		      (double)g_bd.tw.hold_lo_cm, (double)g_bd.tw.hold_hi_cm);
	}
	sb_tw_in in;
	memset(&in, 0, sizeof in);
	in.sword = bd_sword_in_hand();
	in.allow = !bowed[0] && !bowed[1] && !bo_out() && !cl_holds(SA_L) && !sl_dagger_drawn(&g_sl.g) && !fb_third();
	in.valid[0] = valid[0], in.valid[1] = valid[1];
	in.g[0] = g[0], in.g[1] = g[1];
	in.squeeze_l = bd_squeeze(SA_L, bo_grab(SA_L, (g_inp.st.hand[SA_L].active & SOMVR_HACT_INPUT) ?
						 g_inp.st.hand[SA_L].squeeze : 0)); /* SOMVR_BOW_DRAW=slot: or the trigger */
	in.angle_rad = g_bd.angle_deg * BD_DEG;
	in.dt = dt;
	sb_tw_step(&g_bd.tws, &g_bd.tw, &in, &g_bd.two);
	const sb_tw_out *o = &g_bd.two;
	if (o->ev)
		logf_("TWOHAND t=%.3f %s%s%s%s%s: hands %.1f cm apart, the left on the hilt %.1f cm behind the right grip, weight %.2f "
		      "(sword %d allow %d; grabs %llu releases %llu breaks %llu lost %llu misses %llu)", cb_now(),
		      o->ev & SB_TW_E_GRAB ? "grab" : "", o->ev & SB_TW_E_RELEASE ? "release" : "",
		      o->ev & SB_TW_E_BREAK ? "break (the hands too far apart)" : "", o->ev & SB_TW_E_LOST ? "lost" : "",
		      o->ev & SB_TW_E_MISS ? "a squeeze off the hilt (no grab)" : "", o->dist_cm, o->hold_cm, o->w, in.sword,
		      in.allow, (unsigned long long)g_bd.tws.grabs, (unsigned long long)g_bd.tws.releases,
		      (unsigned long long)g_bd.tws.breaks, (unsigned long long)g_bd.tws.lost, (unsigned long long)g_bd.tws.misses);
	if (o->w > 0)
		g[0] = o->g[0], g[1] = o->g[1];
}

/* ---- H4 ------------------------------------------------------------------------------------------------------------- */

static void bd_summary(double t)
{
	if (t < g_bd.next_log)
		return;
	g_bd.next_log = t + 10.0;
	struct bd_cnt *w = &g_bd.w;
	if (!w->calls)
		return;
	logf_("BODY t=%.3f last 10 s: listener calls %llu, the player's %llu: edited %llu (%s; in place %llu, write failed %llu) "
	      "not live %llu not ready %llu bad reads %llu other thread %llu; hands shown L %llu R %llu, hidden L %llu R %llu; drawn "
	      "in the eyes %d", t, (unsigned long long)w->calls, (unsigned long long)w->player, (unsigned long long)w->edits,
	      sb_hide_name(g_bd.mode), (unsigned long long)w->inplace, (unsigned long long)w->badwrite,
	      (unsigned long long)w->nolive, (unsigned long long)w->notready, (unsigned long long)w->badread,
	      (unsigned long long)w->other, (unsigned long long)w->shown[0], (unsigned long long)w->shown[1],
	      (unsigned long long)w->hidden[0], (unsigned long long)w->hidden[1], g_bd.drawn);
	memset(w, 0, sizeof *w);
}

/* the drawn sword against the right weapon node (the grip's calibration, section 28.3): its engine object's transform
 * in R_Weapon's frame and its render node's world box, each 2 s while drawn, 6 lines at most */
static void bd_sword_probe(void)
{
	static double next;
	static int lines;
	double t = mono();
	if (lines >= 6 || t < next || !g_arm.on || !g_arm.chains_ok || g_arm.ch[SA_R].wpn < 0 || !g_sl.on || !g_sl.have_g ||
	    !sl_sword_held(&g_sl.g))
		return;
	next = t + 2.0;
	uintptr_t b = peek_ptr(g_sl.g.item[0] + SL_IT_ENTRIES), ho = b ? peek_ptr(b + SL_IE_OBJ) : 0;
	float op[3], oq[4], sp[3], sq[4];
	if (!ho || !peek(g_arm.obj + OBJ_POS, op, sizeof op) || !peek(g_arm.obj + OBJ_ROT, oq, sizeof oq) ||
	    !peek(ho + OBJ_POS, sp, sizeof sp) || !peek(ho + OBJ_ROT, sq, sizeof sq))
		return;
	lines++;
	sa_xf X = { { op[0], op[1], op[2] }, sv_qnormalize(sv_quat_of(oq)) };
	sa_xf W = sa_xf_mul(X, sa_node_xf(&g_arm.pose[g_arm.ch[SA_R].wpn])); /* the last phase 2's (solved) node */
	sa_xf S = { { sp[0], sp[1], sp[2] }, sv_qnormalize(sv_quat_of(sq)) };
	sa_xf rel = sa_xf_mul(sa_xf_inv(W), S);
	sv_vec3 ax[3] = { sv_qrot(rel.q, (sv_vec3){ 1, 0, 0 }), sv_qrot(rel.q, (sv_vec3){ 0, 1, 0 }),
			  sv_qrot(rel.q, (sv_vec3){ 0, 0, 1 }) };
	float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
	uintptr_t wm = hp_weapon_model(ho, t), mr = wm ? peek_ptr(wm + SM_MAIN) : 0, nd = mr ? hp_node_of(mr) : 0;
	int box = nd && peek(nd + NODE_BOX, lo, sizeof lo) && peek(nd + NODE_BOX + 0x10, hi, sizeof hi);
	/* the box centre and its corner extents in R_Weapon's frame */
	sv_vec3 c = { 0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2]) }, cw = { 0, 0, 0 };
	float ext[3] = { 0, 0, 0 };
	if (box) {
		sa_xf iw = sa_xf_inv(W);
		cw = sa_xf_mul(iw, (sa_xf){ c, { 0, 0, 0, 1 } }).p;
		for (int k = 0; k < 8; k++) {
			sv_vec3 p = { k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2] };
			sv_vec3 q = sa_xf_mul(iw, (sa_xf){ p, { 0, 0, 0, 1 } }).p;
			float d[3] = { q.x - cw.x, q.y - cw.y, q.z - cw.z };
			for (int a = 0; a < 3; a++)
				ext[a] = fmaxf(ext[a], fabsf(d[a]));
		}
	}
	logf_("BODY sword t=%.3f: its object %#lx in R_Weapon's frame pos %.1f,%.1f,%.1f cm q %.3f,%.3f,%.3f,%.3f (its x -> "
	      "%.2f,%.2f,%.2f y -> %.2f,%.2f,%.2f z -> %.2f,%.2f,%.2f); render node %#lx box %s centre %.1f,%.1f,%.1f half-extents "
	      "%.1f,%.1f,%.1f cm (R_Weapon's axes; a world box); raw lo %.1f,%.1f,%.1f hi %.1f,%.1f,%.1f; R_Weapon at %.1f,%.1f,"
	      "%.1f (world)", t, (unsigned long)ho, rel.p.x, rel.p.y, rel.p.z, rel.q.x,
	      rel.q.y, rel.q.z, rel.q.w, ax[0].x, ax[0].y, ax[0].z, ax[1].x, ax[1].y, ax[1].z, ax[2].x, ax[2].y, ax[2].z,
	      (unsigned long)nd, box ? "ok" : "unread", cw.x, cw.y, cw.z, ext[0], ext[1], ext[2], lo[0], lo[1], lo[2], hi[0],
	      hi[1], hi[2], W.p.x, W.p.y, W.p.z);
}

/* H4, every stereo frame (before hp_frame): this frame's edits, then the renderable and the word for the next phase 7 */
static void bd_frame(int live)
{
	bf_frame(); /* SOMVR_FINGERS: its words and lines */
	if (!g_bd.on && !g_bd.test && !g_bd.twohand)
		return;
	bd_take_words();
	bd_sword_probe();
	if (!g_bd.on)
		return;
	if (!g_bd.tid)
		g_bd.tid = g_arm.tid ? g_arm.tid : my_tid();
	g_bd.drawn = g_bd.edit_seq == g_bd.h4_seq + 1;
	g_bd.h4_seq++;
	uintptr_t rend = 0;
	if (live && g_arm.model) {
		rend = peek_ptr(g_arm.model + SM_MAIN);
		if (rend && vptr_of((void *)rend) != g_bd.vt_rend)
			rend = 0;
	}
	atomic_store_explicit(&g_bd.rend, rend, memory_order_relaxed);
	atomic_store_explicit(&g_bd.live_frame, live && rend ? rend_frame() : 0, memory_order_relaxed);
	bd_summary(mono());
}

/* ---- start and end -------------------------------------------------------------------------------------------------- */

/* after arm_config (its calibrations), before the hooks: the switches */
static void bd_config(int *bad)
{
	static const char *const onoff_w[] = { "0", "1" };
	static const char *const body_w[] = { "full", "hands" };
	g_bd.want = env_word("SOMVR_BODY", body_w, 2, 0);
	g_bd.twohand = env_word("SOMVR_TWOHAND", onoff_w, 2, 0);
	g_bd.test = env_word("SOMVR_HANDS_TEST", onoff_w, 2, 0);
	const char *hm = getenv("SOMVR_BODY_HIDE");
	if (hm && hm[0]) {
		int m = sb_hide_parse(hm);
		if (m < 0)
			*bad = 1, logf_("WARN SOMVR_BODY_HIDE=%s is not line|cuff|cuffik|forearm|zero|nan|far|show: line", hm);
		else
			g_bd.mode = m;
	}
	g_bd.edit = env_word("SOMVR_BODY_EDIT", g_bd_edit_w, BD_EDIT_N, BD_EDIT_AUTO);
	g_bd.angle_deg = env_float("SOMVR_SWORD_ANGLE", 0, -90, 90, bad);
	g_bd.grip_cm = env_float("SOMVR_SWORD_GRIP_CM", 0, -10, 25, bad);
	g_bd.t_sq[0] = g_bd.t_sq[1] = -1, g_bd_req.sq[0] = g_bd_req.sq[1] = -1;
	sb_tw_cfg_default(&g_bd.tw);
	/* build 18 (docs/re-xr-pipeline.md 66.3): where the left fist may sit on the hilt, cm behind the right grip point -
	 * build 17's 8..24 let it ride over the pommel (24 cm behind the grip at SOMVR_SWORD_GRIP_CM 8 = the pommel's end) */
	const char *hv = getenv("SOMVR_TWOHAND_HOLD_CM");
	if (hv && hv[0]) {
		float lo, hi;
		char tail;
		if (sscanf(hv, "%f,%f%c", &lo, &hi, &tail) == 2 && lo >= 4 && hi <= 30 && lo <= hi)
			g_bd.tw.hold_lo_cm = lo, g_bd.tw.hold_hi_cm = hi;
		else
			*bad = 1, logf_("WARN SOMVR_TWOHAND_HOLD_CM=%s is not LO,HI (4 <= LO <= HI <= 30 cm): %.0f,%.0f", hv,
					(double)g_bd.tw.hold_lo_cm, (double)g_bd.tw.hold_hi_cm);
	}
	bf_config(bad); /* SOMVR_FINGERS */
	vh_config(bad); /* SOMVR_VRHANDS */
	sf_config(bad); /* SOMVR_HANDS_SURFACE */
	wp_config(bad); /* SOMVR_WISPS (after SOMVR_VRHANDS) */
}

/* after arm_start and sl_start: what the switches need, the slot, the config line */
static void bd_start(void)
{
	const struct site_spec *sites = g_bd_sites;
	if (g_arm.on) {
		memcpy(g_bd.cal_v, g_arm.cal_v, sizeof g_bd.cal_v);
		g_bd.pitch0 = g_cb.bcfg.pitch_rad;
		bd_grip_blade();
		if (g_bd.angle_deg != 0 || g_bd.grip_cm != 0)
			bd_apply_cal();
	}
	if (g_bd.twohand && (!g_arm.on || !g_sl.on)) {
		logf_("WARN SOMVR_TWOHAND=1 needs SOMVR_ARMS=1 and SOMVR_SLOTS=1 (the sword's state): two hands off");
		g_bd.twohand = 0;
	}
	if (g_bd.test && !g_arm.on && !g_inp.on)
		logf_("WARN SOMVR_HANDS_TEST=1 without SOMVR_INPUT: no grips to put anywhere");
	if (g_bd.want) {
		const char *off = !g_arm.on ? "SOMVR_ARMS is off (the chains and the IK)" : g_rigcfg.base != SOMVR_BASE_HEAD
			? "SOMVR_BASE is not head (first person only)" : NULL;
		char why[BDS_N][512];
		int ok = 1;
		for (int i = 0; i < BDS_N; i++) {
			int o = check_site(&sites[i], why[i], sizeof why[i]) == 0;
			logf_("BODY site %s %s %#lx -> %#lx: %s", sites[i].name, o ? "ok" : "MISMATCH", (unsigned long)sites[i].addr,
			      (unsigned long)sites[i].target, why[i]);
			ok &= o;
		}
		if (!off && !ok)
			off = "a site did not match";
		if (!off) {
			char err[200] = "";
			void *fn = (void *)bd_copy;
			g_bd.orig = sites[BDS_COPY].target;
			g_bd.vt_rend = sites[BDS_COPY].addr - 8 * BD_REND_SLOT;
			if (write_code(sites[BDS_COPY].addr, &fn, 8, err, sizeof err) != 0) {
				logf_("HOOK BD_REND_COPY SKIPPED %#lx: write failed: %s", (unsigned long)sites[BDS_COPY].addr, err);
				off = "the slot could not be written";
			} else {
				logf_("HOOK BD_REND_COPY ok slot %#lx orig=%#lx now=%p (CSimpleModelRenderable::vf_02: every renderable's "
				      "skin copy, game thread, phase 7)", (unsigned long)sites[BDS_COPY].addr, (unsigned long)g_bd.orig, fn);
				g_bd.on = 1;
			}
		}
		if (off)
			logf_("WARN SOMVR_BODY=hands: %s: the whole body as before (SOMVR_HEAD_PARTS=main hides it in the eyes)", off);
	}
	g_bd.next_log = mono() + 10.0;
	logf_("CONFIG body=%s hide=%s edit=%s sword angle %.1f deg (the blade line (0, sin a, cos a) in the grip's frame: the right weapon "
	      "node's calibration and the combat blade's pitch %.1f deg), grip %.1f cm (SOMVR_SWORD_GRIP_CM: the sword slid along "
	      "the blade in the fist, the combat blade's tip %.1f cm) twohand=%d (grab within %.0f cm of the hilt %.0f..%.0f cm "
	      "behind the right grip, the left hand held %.1f..%.1f cm behind it, apart past %.0f cm = one hand, ease %.0f ms) "
	      "test=%d", g_bd.on ? "hands (the body's bones collapsed in the player's skin palette, the hands kept)" : "full",
	      sb_hide_name(g_bd.mode), bd_edit_name(g_bd.edit), g_bd.angle_deg, g_cb.bcfg.pitch_rad / BD_DEG, g_bd.grip_cm,
	      g_cb.bcfg.len_cm, g_bd.twohand, g_bd.tw.grab_r_cm,
	      g_bd.tw.hilt_lo_cm, g_bd.tw.hilt_hi_cm, g_bd.tw.hold_lo_cm, g_bd.tw.hold_hi_cm, g_bd.tw.break_cm,
	      g_bd.tw.blend_s * 1e3, g_bd.test);
	bf_start(); /* SOMVR_FINGERS */
	vh_start(); /* SOMVR_VRHANDS */
	sf_start(); /* SOMVR_HANDS_SURFACE */
}

static void bd_end_log(void)
{
	bf_end_log(); /* SOMVR_FINGERS */
	vh_end_log(); /* SOMVR_VRHANDS */
	sf_end_log(); /* SOMVR_HANDS_SURFACE */
	if (!g_bd.on && !g_bd.twohand)
		return;
	const struct bd_cnt *c = &g_bd.n;
	logf_("BODY END body=%s hide=%s edit=%s: listener calls %llu, the player's %llu, edited %llu (in place %llu, write failed "
	      "%llu), not live %llu, not ready %llu, bad reads %llu, other thread %llu; hands shown L %llu R %llu; two hands: grabs "
	      "%llu releases %llu breaks %llu lost %llu misses %llu", g_bd.on ? "hands" : "full", sb_hide_name(g_bd.mode),
	      bd_edit_name(g_bd.edit), (unsigned long long)c->calls, (unsigned long long)c->player, (unsigned long long)c->edits,
	      (unsigned long long)c->inplace, (unsigned long long)c->badwrite, (unsigned long long)c->nolive,
	      (unsigned long long)c->notready, (unsigned long long)c->badread, (unsigned long long)c->other,
	      (unsigned long long)c->shown[0], (unsigned long long)c->shown[1], (unsigned long long)g_bd.tws.grabs,
	      (unsigned long long)g_bd.tws.releases, (unsigned long long)g_bd.tws.breaks, (unsigned long long)g_bd.tws.lost,
	      (unsigned long long)g_bd.tws.misses);
}
