/* somvr_knife_game.c - build 37 (som-b37-knife; docs/re-xr-pipeline.md 168, handoff/HANDOFF-kills.md "build 37"):
 * SOMVR_KNIFE_KILL=off|on (code default off) - the stealth kill with Talion's knife, Acharn, by hand. Included by
 * somvr.c after somvr_kills_game.c (it uses the stealth judge, the kills' AI hold and combat's hit).
 *
 * With the knife drawn (SOMVR_SLOTS' hip, either hand), behind or beside an orc that has not noticed Talion:
 *  - the hold (SOMVR_KNIFE_KILL_HOLD=on): the other, empty hand's grip closed on him - his torso up to his head (the
 *    shoulder, the neck, the head) within SOMVR_KNIFE_KILL_GRAB_CM (the stealth grab's reach) - holds him: his
 *    navigation held as a sync's start holds it (FUN_02972f90 bit 2), his own attacks and reactions refused (SOMVR_KILLS'
 *    AI hold, somvr_w_klai), no touch alert, a stealth target while held (stl_who). No game command and no grab of the
 *    game's (the VERBS hand offers no stealth target while the knife is out: vb_heads), nothing written to Talion - the
 *    player never moves. It ends when the grip opens, the hand leaves him (GRAB_CM + 25 cm), he dies, the knife goes
 *    back to the belt, his kill or a sync takes him, or after SOMVR_KNIFE_KILL_HOLD_S (8 s: he breaks free).
 *  - the kill: the knife's own stab rule in the stealth judge (somvr_stl_judge with cfg.knife: a deliberate push of its
 *    point into the neck, the back or the side - held: anywhere - the point leading within _DEG of the blade, the blade
 *    within _IN_DEG of straight into the body, the hand's own push, _DEPTH_CM deep, at _MPS or faster) = the stealth
 *    kill's own hit (the stock silent record, forceKill, SOMVR_COMBAT_DEATH's type: dead at once, the body drops where
 *    it stands - build 36's rules; no sync, no attack of Talion's); the knife sticks (SOMVR_STEALTH_KILL_STICK's pin in
 *    the knife's hand, a pull of _STICK_CM before it comes, the strain felt), the kill's thud, the blood turned onto the
 *    stab.
 *  - silent (SOMVR_KNIFE_KILL_SEEN=on): the stock silent kill's stimuli, as Combat_StartAttack registers them from its
 *    AttackData (FUN_026c7180: 'Instakill_Seen', 'Player_StartleSlave' and the persistent 'Performing_Instakill', off
 *    after 1 s): only orcs that see it react. The game's "Stealth Kill" XP comes with the record (run 20261028-011320).
 *  - an orc that is no stealth target (it fights Talion, he is in front of it, neither crouched nor slow): the knife's
 *    contact with a hit's speed or a stab, in its body, is a light hit (combat's own cb_hit at the light tier), no kill.
 * Lines: `CONFIG knife ...`, `KNIFE t=.. hold|let go|kill|light|seen ...`, `KNIFE END ...`. Test words (SOMVR_HANDS_TEST):
 * `knife status|where|on|off|seen on|off`. */

#define KN_CANDS 4
#define KN_LEAVE_CM 25.0f /* the holding hand this much past the reach: it let him go */
#define KN_SEEN_S 1.0     /* the persistent stimulus off after this (the stock sync's is held while it plays) */

enum { KN_END_OPEN, KN_END_LEFT, KN_END_DEAD, KN_END_BELT, KN_END_TAKEN, KN_END_FREE, KN_END_GONE, KN_END_N };
static const char *const g_kn_end_w[KN_END_N] = { "the grip opened", "the hand left him", "he died",
						 "the knife went back to the belt", "his kill or a sync took him",
						 "he broke free (SOMVR_KNIFE_KILL_HOLD_S)", "he is out of the orc list" };

static struct {
	int on, hold, seen, stick, reverse;
	float grab_cm, hold_s, stick_cm;
	/* the candidates (the combat frame -> in_frame's free hand): the knife's stealth targets, their torso -> head */
	int ncand;
	struct kn_cand { uintptr_t c, hobj; float a[3], b[3], n[3], root[3], yaw; } cand[KN_CANDS], near; /* near: the nearest
	                                                                                 * orc of any kind (`knife where`) */
	int have_near;
	uint64_t cand_frame;
	int closed[2];
	/* the hold */
	uintptr_t c, hobj, ai;
	_Atomic uintptr_t held_c; /* (any thread: somvr_w_klai) */
	int hand, nav, end;       /* the holding hand; its navigation held; a pending end (KN_END_*, -1 none) */
	double t0, line_t, miss_t;
	float pos0[3], drift, seg[2][3], u;
	/* the light hit (cb_hit): the knife's hand for its pulse, no bite (-1 = not the knife's) */
	int hit_hand;
	/* the seen stimuli */
	int32_t seen_id;
	double seen_end;
	_Atomic int req_mode, req_seen, req_status, req_where;
	/* build 40 (re-xr-pipeline.md 175.3; a test aid): `knife push D MS [sword]` - the push measured and started in one
	 * frame (D cm past his back, MS ms; the sword's blade with `sword`): the words' thread sets, in_frame takes */
	_Atomic int req_push;
	float push_d, push_ms;
	int push_sword, push_turn, push_pend;
	uint64_t push_frame;
	char push_w[160]; /* the swing, 2 frames after the ready's test pose (a test cancels a swing given in its frame) */
	uint64_t holds, ends[KN_END_N], misses, kills, lights, light_slow, light_air, seen_n, seen_fail, vb_skips, frames;
	uint32_t lines;
} g_kn = { .hand = -1, .end = -1, .hit_hand = -1 };

static int kn_line(void) { return g_kn.lines < 400 ? (int)++g_kn.lines : 0; }
static int kn_held(uintptr_t c) { return c && c == atomic_load(&g_kn.held_c); }
static int kn_hitting(void) { return g_kn.hit_hand; }
static float kn_stick_cm(void) { return g_kn.stick ? g_kn.stick_cm : -1.0f; }

/* the orc as the knife's stealth target now: unaware, Talion behind or beside it, crouched or slow (or held by us) */
static int kn_target(const struct cb_orc *o, uintptr_t pc, const float *root)
{
	if (!o->c || cb_dead(o->c) || kl_is_beast(o->c, o->hobj))
		return 0;
	somvr_stl_who w;
	struct stl_aw a;
	if (stl_who(o, pc, root, &w, &a) == 2 && kn_held(o->c))
		return 1;
	somvr_stl_cfg kc = g_stl.cfg;
	kc.behind_deg = kc.knife_behind_deg;
	return somvr_stl_possible(&kc, &w) == SOMVR_STL_KILL;
}

/* vb_heads (the combat frame): the VERBS hand takes no stealth target while the knife is out - the knife's hold does */
static int kn_takes(const struct cb_orc *o, uintptr_t pc, const float *root)
{
	if (!g_kn.on || !g_kn.hold || g_stl.mode != STL_ON || sl_knife_side() < 0 || !root || !pc)
		return 0;
	int y = kn_held(o->c) || kn_target(o, pc, root);
	g_kn.vb_skips += y;
	return y;
}

static float kn_seg_d(const float p[3], const float a[3], const float b[3], float *u)
{
	float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
	float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
	float k = l2 > 1e-6f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0;
	k = k < 0 ? 0 : k > 1 ? 1 : k;
	if (u)
		*u = k;
	float d[3] = { ap[0] - k * ab[0], ap[1] - k * ab[1], ap[2] - k * ab[2] };
	return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

static const char *kn_part(float u) { return u < 0.45f ? "his back/shoulder" : u < 0.8f ? "his neck" : "his head"; }

/* the hold's end (either frame; the navigation given back in the combat frame: kn_frame) */
static void kn_end(int why, double t)
{
	if (!g_kn.c || g_kn.end >= 0)
		return;
	g_kn.end = why;
	atomic_store(&g_kn.held_c, 0);
	g_kn.ends[why]++;
	if (g_kn.hand >= 0 && why != KN_END_DEAD)
		haptic(g_kn.hand, SOMVR_HX_TICK);
	if (kn_line())
		logf_("KNIFE t=%.3f let go of orc %#llx: %s after %.2f s (his root moved %.1f cm while held)", t,
		      (unsigned long long)g_kn.c, g_kn_end_w[why], t - g_kn.t0, (double)g_kn.drift);
}

/* the test word `knife where` (in_frame): the nearest candidate in the head's yaw frame (x right, y up, z ahead, cm -
 * the `hands test` frame): its back's surface between the torso and the neck (16 cm behind their middle, the way it
 * faces), the nodes, its facing; the grips too (against their test poses: the frame's check) */
static void kn_where(const sv_vec3 pw[2], sv_vec3 eye, double t)
{
	float hv = g_inp.hv, cs = cosf(hv), sn = sinf(hv);
#define KN_HF(P, X) do { float dx_ = (P)[0] - eye.x, dz_ = (P)[2] - eye.z; \
	(X)[0] = dx_ * cs - dz_ * sn, (X)[1] = (P)[1] - eye.y, (X)[2] = dx_ * sn + dz_ * cs; } while (0)
	float hl[3], hr[3], wl[3] = { pw[0].x, pw[0].y, pw[0].z }, wr[3] = { pw[1].x, pw[1].y, pw[1].z }, kb[3] = { 0, 0, 0 },
	      kt[3] = { 0, 0, 0 };
	KN_HF(wl, hl);
	KN_HF(wr, hr);
	const somvr_blade *kbl = &g_stl.bl[SOMVR_HAND_L]; /* the judge's own knife blade (stl_hands) */
	if (kbl->valid) {
		KN_HF(kbl->base, kb);
		KN_HF(kbl->tip, kt);
		logf_("KNIFE where t=%.3f: the knife's blade (the judge's) base %.1f %.1f %.1f tip %.1f %.1f %.1f; the grips L %.1f %.1f "
		      "%.1f R %.1f %.1f %.1f (the head's frame)", t, (double)kb[0], (double)kb[1], (double)kb[2], (double)kt[0],
		      (double)kt[1], (double)kt[2], (double)hl[0], (double)hl[1], (double)hl[2], (double)hr[0], (double)hr[1],
		      (double)hr[2]);
	}
	if ((!g_kn.ncand && !g_kn.have_near) || g_fr.frame - g_kn.cand_frame > 2) {
		logf_("KNIFE where t=%.3f: no orc within 3 m (the knife %s); the grips L %.0f %.0f %.0f R %.0f %.0f %.0f", t,
		      sl_knife_side() < 0 ? "at the belt" : "out", (double)hl[0], (double)hl[1], (double)hl[2], (double)hr[0],
		      (double)hr[1], (double)hr[2]);
		return;
	}
	const struct kn_cand *k = &g_kn.near;
	float bd = 1e9f;
	for (int i = 0; i < g_kn.ncand; i++) {
		float d = dist3(g_kn.cand[i].a, (float[3]){ eye.x, eye.y, eye.z });
		if (d < bd)
			bd = d, k = &g_kn.cand[i];
	}
	const float *T = k->a, *H = k->b, *N = k->n, *O = k->root;
	float y = k->yaw, B[3] = { 0.5f * (T[0] + N[0]) - 16.0f * sinf(y), 0.5f * (T[1] + N[1]),
						    0.5f * (T[2] + N[2]) - 16.0f * cosf(y) };
	float hb[3], ht[3], hn[3], hh[3], ho[3];
	KN_HF(B, hb);
	KN_HF(T, ht);
	KN_HF(N, hn);
	KN_HF(H, hh);
	KN_HF(O, ho);
#undef KN_HF
	float face = sv_wrap_pi(y - hv) * 57.29578f;
	logf_("KNIFE where t=%.3f: orc %#llx%s%s in the head's frame (x right, y up, z ahead; cm): back %.0f %.0f %.0f torso %.0f %.0f "
	      "%.0f neck %.0f %.0f %.0f head %.0f %.0f %.0f root %.0f %.0f %.0f; it faces %+.0f deg off the head's way (0 = its back "
	      "to Talion); the grips L %.0f %.0f %.0f R %.0f %.0f %.0f", t, (unsigned long long)k->c,
	      kn_held(k->c) ? " (held)" : "", k == &g_kn.near ? " (no stealth target)" : " (a stealth target)", (double)hb[0], (double)hb[1], (double)hb[2], (double)ht[0], (double)ht[1],
	      (double)ht[2], (double)hn[0], (double)hn[1], (double)hn[2], (double)hh[0], (double)hh[1], (double)hh[2],
	      (double)ho[0], (double)ho[1], (double)ho[2], (double)face, (double)hl[0], (double)hl[1], (double)hl[2],
	      (double)hr[0], (double)hr[1], (double)hr[2]);
}

/* in_frame (the game thread, after the slots, before the climb): the free hand's grip on a candidate. pw: the grips,
 * world cm; eye: the head */
/* build 40 (re-xr-pipeline.md 175.3; a test aid for headless drivers): `knife push D MS [sword]` - the nearest
 * candidate's back (kn_where's: the torso-neck middle 16 cm toward his back), the judge's blade (the knife's, or the
 * sword's), both this frame: the right test hand swung by the point's way to his back + D cm on, in MS ms, its turn
 * kept (`hands swing R`). With `turn`: the hand first turned so the blade lies along that way, the point 10 cm short of
 * his back (`hands test R`), then 2 frames later swung D cm past it (a straight thrust along the blade) - exact for a
 * blade rigid on the test hand (offline, 200 poses), but in boot 093501 a turn of 80-90 deg missed: opt-in only. The Windows build reads one word a second: a measure (`knife where`) and a push word
 * written from it came 2-3 s apart while the orc swayed 10-30 cm (boots 084104, 085331: the push 7-17 cm off his body) */
static void kn_push(sv_vec3 eye, double t)
{
	const somvr_blade *b = &g_stl.bl[g_kn.push_sword ? SOMVR_HAND_R : SOMVR_HAND_L];
	const struct kn_cand *k = NULL;
	float bd = 1e9f;
	for (int i = 0; i < g_kn.ncand; i++) {
		float d = dist3(g_kn.cand[i].a, (float[3]){ eye.x, eye.y, eye.z });
		if (d < bd)
			bd = d, k = &g_kn.cand[i];
	}
	if (!k && g_kn.have_near)
		k = &g_kn.near;
	if (!k || g_fr.frame - g_kn.cand_frame > 2 || !b->valid || !g_bd.test || !g_bd.t_on[1]) {
		logf_("KNIFE push t=%.3f: nothing done (%s)", t, !k || g_fr.frame - g_kn.cand_frame > 2 ? "no orc within 3 m"
		      : !b->valid ? "no blade" : "no right test hand");
		return;
	}
	const float *T = k->a, *N = k->n;
	float y = k->yaw, B[3] = { 0.5f * (T[0] + N[0]) - 16.0f * sinf(y), 0.5f * (T[1] + N[1]), 0.5f * (T[2] + N[2]) - 16.0f * cosf(y) };
	float hv = g_inp.hv, cs = cosf(hv), sn = sinf(hv);
#define KP_HF(P) ((sv_vec3){ ((P)[0] - eye.x) * cs - ((P)[2] - eye.z) * sn, (P)[1] - eye.y, ((P)[0] - eye.x) * sn + ((P)[2] - eye.z) * cs })
	sv_vec3 bh = KP_HF(B), th = KP_HF(b->tip), sh = KP_HF(b->base); /* (the head's yaw frame: x right, y up, z ahead) */
#undef KP_HF
	sv_vec3 dv = { bh.x - th.x, bh.y - th.y, bh.z - th.z };
	float dl = sqrtf(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
	sv_vec3 ax = { th.x - sh.x, th.y - sh.y, th.z - sh.z };
	float al = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z);
	if (!(dl > 1e-3f && al > 1e-3f))
		return;
	sv_vec3 u = { dv.x / dl, dv.y / dl, dv.z / dl };
	float cosax = (ax.x * u.x + ax.y * u.y + ax.z * u.z) / al;
	const float *v0 = g_bd.t_v[1];
	if (!g_kn.push_turn) { /* (the default, boot 090833's: the grip moved by the point's way to his back + D cm on, its turn
	                        * kept - boot 093501: a turn of 80-90 deg onto the way put the hand where the judge's blade did
	                        * not follow, the grab and the sword missed) */
		char w0[160];
		float m = 1.0f + g_kn.push_d / dl;
		snprintf(w0, sizeof w0, "swing R %.1f %.1f %.1f %.1f %.1f %.1f %.0f", v0[0] + dv.x * m, v0[1] + dv.y * m, v0[2] + dv.z * m,
			 v0[3], v0[4], v0[5], (double)g_kn.push_ms);
		bd_word(w0);
		logf_("KNIFE push t=%.3f: orc %#llx's back %.0f cm from the %s's point (the blade %.2f along that way); the right hand "
		      "%+.1f %+.1f %+.1f cm in %.0f ms (%.0f cm past his back; a test aid)", t, (unsigned long long)k->c, (double)dl,
		      g_kn.push_sword ? "sword" : "knife", (double)cosax, (double)(dv.x * m), (double)(dv.y * m), (double)(dv.z * m),
		      (double)g_kn.push_ms, (double)g_kn.push_d);
		return;
	}
	/* `turn`: the blade turned onto the way (about the grip; `hands test`'s turns are OpenXR's axes: z back), its point then 10
	 * cm short of his back on that way: the ready; the push from there D cm past it along the way - the grip moved, not
	 * turned (a straight thrust along the blade) */
	const float *v = g_bd.t_v[1];
	sv_quat qh = sa_xf_euler((float[6]){ 0, 0, 0, v[3], v[4], v[5] }).q;
	sv_quat qa = fn_qalign((sv_vec3){ ax.x / al, ax.y / al, -ax.z / al }, (sv_vec3){ u.x, u.y, -u.z });
	float ang[3];
	fn_euler(sv_qmul(qa, qh), ang);
	sv_vec3 o = sv_qrot(qa, (sv_vec3){ th.x - v[0], th.y - v[1], -(th.z - v[2]) }); /* the point from the grip, turned */
	o.z = -o.z;
	float r0 = 10.0f, r1 = g_kn.push_d;
	char w[160];
	snprintf(w, sizeof w, "test R %.1f %.1f %.1f %.1f %.1f %.1f", bh.x - r0 * u.x - o.x, bh.y - r0 * u.y - o.y, bh.z - r0 * u.z - o.z,
		 ang[0], ang[1], ang[2]);
	bd_word(w);
	snprintf(g_kn.push_w, sizeof g_kn.push_w, "swing R %.1f %.1f %.1f %.1f %.1f %.1f %.0f", bh.x + r1 * u.x - o.x, bh.y + r1 * u.y - o.y,
		 bh.z + r1 * u.z - o.z, ang[0], ang[1], ang[2], (double)g_kn.push_ms);
	g_kn.push_pend = 1, g_kn.push_frame = g_fr.frame;
	logf_("KNIFE push t=%.3f: orc %#llx's back %.0f cm from the %s's point (the blade %.2f along that way, turned onto it); the "
	      "ready 10 cm short (%s), then %s (%.0f cm past his back; a test aid)", t, (unsigned long long)k->c, (double)dl,
	      g_kn.push_sword ? "sword" : "knife", (double)cosax, w, g_kn.push_w, (double)g_kn.push_d);
}

/* build 40: the push's swing, 2 frames after its ready (kn_hands, every frame) */
static void kn_push_frame(void)
{
	if (g_kn.push_pend && g_fr.frame >= g_kn.push_frame + 2)
		g_kn.push_pend = 0, bd_word(g_kn.push_w);
}

static void kn_hands(const sv_vec3 pw[2], sv_vec3 eye)
{
	if (!g_kn.on)
		return;
	double t = cb_now();
	if (atomic_exchange(&g_kn.req_where, 0))
		kn_where(pw, eye, t);
	if (atomic_exchange(&g_kn.req_push, 0))
		kn_push(eye, t);
	kn_push_frame();
	if (!g_kn.hold)
		return;
	int ks = sl_knife_side(), fh = ks >= 0 ? 1 - ks : -1, edge[2] = { 0, 0 };
	for (int h = 0; h < 2; h++) {
		float s = bd_squeeze(h, (g_inp.st.hand[h].active & SOMVR_HACT_INPUT) ? g_inp.st.hand[h].squeeze : 0);
		int was = g_kn.closed[h];
		g_kn.closed[h] = was ? s > 0.35f : s >= 0.6f;
		edge[h] = !was && g_kn.closed[h];
	}
	if (g_kn.c) {
		if (g_kn.end >= 0)
			return;
		int h = g_kn.hand;
		float p[3] = { pw[h].x, pw[h].y, pw[h].z };
		if (!g_kn.closed[h])
			kn_end(KN_END_OPEN, t);
		else if (kn_seg_d(p, g_kn.seg[0], g_kn.seg[1], NULL) > g_kn.grab_cm + KN_LEAVE_CM)
			kn_end(KN_END_LEFT, t);
		return;
	}
	if (fh < 0 || !edge[fh] || g_kn.end >= 0)
		return;
	const char *no = g_inp.ctx != SC_CTX_WORLD || game_paused() || vm_open() ? "not in the world"
			 : fh == somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD) && bd_sword_in_hand() ? "that hand holds the sword"
			 : bo_out() ? "the bow is out" : cl_holds(fh) ? "that hand holds a climb"
			 : fh == SOMVR_HAND_L && g_vb.hand.phase != SOMVR_VBH_IDLE ? "the verbs' hand holds"
			 : g_fr.frame - g_kn.cand_frame > 2 ? "no fresh orcs" : NULL;
	float p[3] = { pw[fh].x, pw[fh].y, pw[fh].z }, best = 1e9f, bu = 0;
	int bi = -1;
	for (int i = 0; !no && i < g_kn.ncand; i++) {
		float u, d = kn_seg_d(p, g_kn.cand[i].a, g_kn.cand[i].b, &u);
		if (d < best)
			best = d, bi = i, bu = u;
	}
	if (no || bi < 0 || best > g_kn.grab_cm) {
		g_kn.misses++;
		if (t - g_kn.miss_t > 0.5 && kn_line()) {
			g_kn.miss_t = t;
			logf_("KNIFE t=%.3f the %s hand's grip with the knife out: no hold (%s)", t, fh ? "right" : "left",
			      no ? no : bi < 0 ? "no unaware orc behind or beside" : "out of reach");
			if (!no && bi >= 0 && kn_line()) {
				const float *a = g_kn.cand[bi].a, *b = g_kn.cand[bi].b;
				float q[3] = { a[0] + bu * (b[0] - a[0]), a[1] + bu * (b[1] - a[1]), a[2] + bu * (b[2] - a[2]) };
				logf_("KNIFE   the nearest unaware orc %#llx: the hand %.0f cm from %s (the reach %.0f): %.0f cm %s it, %.0f "
				      "cm short of it across the ground (his torso %.0f, his head %.0f cm over the hand)",
				      (unsigned long long)g_kn.cand[bi].c, (double)best, kn_part(bu), (double)g_kn.grab_cm,
				      fabsf(q[1] - p[1]), q[1] > p[1] ? "under" : "over", sqrtf((q[0] - p[0]) * (q[0] - p[0]) +
				      (q[2] - p[2]) * (q[2] - p[2])), (double)(a[1] - p[1]), (double)(b[1] - p[1]));
			}
		}
		return;
	}
	g_kn.c = g_kn.cand[bi].c, g_kn.hobj = g_kn.cand[bi].hobj, g_kn.hand = fh, g_kn.t0 = t, g_kn.nav = 0, g_kn.ai = 0;
	g_kn.drift = 0, g_kn.u = bu, g_kn.pos0[0] = NAN;
	memcpy(g_kn.seg[0], g_kn.cand[bi].a, sizeof g_kn.seg[0]), memcpy(g_kn.seg[1], g_kn.cand[bi].b, sizeof g_kn.seg[1]);
	atomic_store(&g_kn.held_c, g_kn.c);
	g_kn.holds++;
	haptic(fh, SOMVR_HX_TAKE);
	if (kn_line())
		logf_("KNIFE t=%.3f hold: the %s hand took orc %#llx by %s (%.0f cm from it, the reach %.0f) - held in place and "
		      "quiet up to %.0f s; nothing moves Talion or the view", t, fh ? "right" : "left", (unsigned long long)g_kn.c,
		      kn_part(bu), (double)best, (double)g_kn.grab_cm, (double)g_kn.hold_s);
}

/* stl_frame (the combat frame, after the orcs' awareness): the requests, the seen stimuli's end, the candidates, the hold */
static void kn_frame(uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t)
{
	int rm = atomic_exchange(&g_kn.req_mode, 0), rs = atomic_exchange(&g_kn.req_seen, 0);
	if (rm) {
		g_kn.on = rm == 2, g_stl.cfg.knife = g_kn.on;
		logf_("KNIFE t=%.3f %s (the word)", t, g_kn.on ? "on" : "off");
	}
	if (rs)
		g_kn.seen = rs == 2, logf_("KNIFE t=%.3f seen %s (the word)", t, g_kn.seen ? "on" : "off");
	if (atomic_exchange(&g_kn.req_status, 0))
		logf_("KNIFE t=%.3f status: %s, the knife %s, hold %s (orc %#llx), %d candidates; holds %llu misses %llu kills %llu "
		      "lights %llu seen %llu", t, g_kn.on ? "on" : "off", sl_knife_side() < 0 ? "at the belt" : sl_knife_side() ?
		      "in the right hand" : "in the left hand", g_kn.hold ? "on" : "off", (unsigned long long)g_kn.c, g_kn.ncand,
		      (unsigned long long)g_kn.holds, (unsigned long long)g_kn.misses, (unsigned long long)g_kn.kills,
		      (unsigned long long)g_kn.lights, (unsigned long long)g_kn.seen_n);
	if (g_kn.seen_id && t >= g_kn.seen_end) {
		if (g_cb.stim.on && g_cb.stim.id == g_kn.seen_id)
			cb_stim_end(t, "the knife kill's stimuli, 1 s on");
		g_kn.seen_id = 0;
	}
	if (!g_kn.on)
		return;
	g_kn.frames++;
	int ks = sl_knife_side(), ni = -1;
	g_kn.ncand = 0;
	for (int i = 0; root && i < n; i++) {
		if (o[i].dist > 300.0f || cb_dead(o[i].c))
			continue;
		if (ni < 0 || o[i].dist < o[ni].dist)
			ni = i;
		if (g_kn.ncand >= KN_CANDS || !kn_target(&o[i], pc, root))
			continue;
		float nd[4][3];
		stl_body(&o[i], nd);
		struct kn_cand *k = &g_kn.cand[g_kn.ncand++];
		k->c = o[i].c, k->hobj = o[i].hobj, k->yaw = stl_orc_yaw(&o[i]);
		memcpy(k->a, nd[1], sizeof nd[1]), memcpy(k->b, nd[3], sizeof nd[3]), memcpy(k->n, nd[2], sizeof nd[2]);
		memcpy(k->root, o[i].pos, sizeof o[i].pos);
	}
	g_kn.have_near = ni >= 0;
	if (ni >= 0) {
		float nd[4][3];
		stl_body(&o[ni], nd);
		struct kn_cand *k = &g_kn.near;
		k->c = o[ni].c, k->hobj = o[ni].hobj, k->yaw = stl_orc_yaw(&o[ni]);
		memcpy(k->a, nd[1], sizeof nd[1]), memcpy(k->b, nd[3], sizeof nd[3]), memcpy(k->n, nd[2], sizeof nd[2]);
		memcpy(k->root, o[ni].pos, sizeof o[ni].pos);
	}
	g_kn.cand_frame = g_fr.frame;
	if (!g_kn.c)
		return;
	if (g_kn.end < 0) {
		int at = -1;
		for (int i = 0; i < n && at < 0; i++)
			if (o[i].c == g_kn.c)
				at = i;
		int why = at < 0 ? KN_END_GONE : cb_dead(g_kn.c) ? KN_END_DEAD : ks < 0 ? KN_END_BELT
			  : kl_busy(g_kn.c) || cb_sync(g_kn.c) ? KN_END_TAKEN : t - g_kn.t0 > g_kn.hold_s ? KN_END_FREE : -1;
		if (why >= 0) {
			kn_end(why, t);
		} else {
			float nd[4][3];
			stl_body(&o[at], nd);
			memcpy(g_kn.seg[0], nd[1], sizeof g_kn.seg[0]), memcpy(g_kn.seg[1], nd[3], sizeof g_kn.seg[1]);
			if (g_kn.pos0[0] != g_kn.pos0[0])
				memcpy(g_kn.pos0, o[at].pos, sizeof g_kn.pos0);
			g_kn.drift = fmaxf(g_kn.drift, dist3(o[at].pos, g_kn.pos0));
			if (!g_kn.nav && g_kl.ok_nav && (g_kn.ai = peek_ptr(g_kn.c + C_AI)) != 0) {
				KL_FN(KLS_NAV, kl_nav_fn)(g_kn.ai, 0, KL_NAV_BIT); /* his navigation held, as a sync's start holds it */
				g_kn.nav = 1;
				if (kn_line())
					logf_("KNIFE t=%.3f orc %#llx: his navigation held (FUN_02972f90 bit %d), his own attacks %s", t,
					      (unsigned long long)g_kn.c, KL_NAV_BIT, g_kl.on && g_kl.ai_hold ? "refused (SOMVR_KILLS' AI hold)"
					      : "NOT refused (SOMVR_KILLS or its AI hold off)");
			}
			if (t >= g_kn.line_t && kn_line()) {
				g_kn.line_t = t + 1.0;
				struct stl_aw a;
				int aw = stl_aware(&o[at], pc ? peek_ptr(pc + C_HOBJ) : 0, &a);
				logf_("KNIFE t=%.3f holding orc %#llx %.1f s: the game's aware %d (+0xeb8 %.3f, fights %d), moved %.1f cm", t,
				      (unsigned long long)g_kn.c, t - g_kn.t0, aw, (double)a.meter, a.engaged, (double)g_kn.drift);
			}
		}
	}
	if (g_kn.end >= 0) { /* the hold's end: his navigation back */
		if (g_kn.nav && g_kn.ai && g_kl.ok_nav && !cb_dead(g_kn.c))
			KL_FN(KLS_NAV, kl_nav_fn)(g_kn.ai, 1, KL_NAV_BIT);
		g_kn.c = 0, g_kn.hobj = 0, g_kn.ai = 0, g_kn.nav = 0, g_kn.hand = -1, g_kn.end = -1;
	}
}

/* stl_strike: the knife on an orc that is no stealth target (v: why): a light hit when it is a blow in its body */
static int kn_light(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, const somvr_blade *b, const sc_speed *sp,
		    const somvr_stl_hit *h, int v, double t)
{
	int tier = sc_tier(&g_stl.dsc, sp);
	int stab = sp->axis_mps >= g_stl.cfg.stab_mps && fabsf(sp->cos_axis) >= g_stl.cfg.stab_cos;
	int air = g_stl.cfg.thrust && !(h->depth_cm >= 0), slow = !air && tier == SC_TIER_NONE && !stab;
	const char *no = air ? "combat's capsule, not its body" : slow ? "slower than a hit or a stab"
			 : !g_cb.ok_hit || g_cb.mode != CB_FULL || !pc ? "combat cannot deal hits" : NULL;
	if (no) {
		g_kn.light_air += air, g_kn.light_slow += slow;
		if (t - g_kn.miss_t > 0.3 && kn_line()) {
			g_kn.miss_t = t;
			logf_("KNIFE t=%.3f the knife on orc %#llx (%s): no hit - %s (along %.2f m/s cos %.2f, the tier %s)", t,
			      (unsigned long long)o->c, somvr_stl_why(v), no, (double)sp->axis_mps, (double)sp->cos_axis,
			      sc_tier_name(tier));
		}
		return 0;
	}
	float hp0 = cb_health(o->c), vv = sqrtf(b->vel_tip[0] * b->vel_tip[0] + b->vel_tip[1] * b->vel_tip[1] + b->vel_tip[2] * b->vel_tip[2]);
	int d0 = cb_dead(o->c), ks = sl_knife_side();
	g_kn.hit_hand = ks >= 0 ? ks : SOMVR_HAND_L;
	g_cb.hit_v = vv * 0.01f, g_cb.hit_k = 1.0f;
	cb_hit(pc, o, ct, SC_TIER_LIGHT, b, t);
	g_kn.hit_hand = -1;
	g_kn.lights++;
	if (kn_line())
		logf_("KNIFE t=%.3f light: the knife's %s on orc %#llx (%s) - combat's light hit, no kill rule: health %.1f -> %.1f, "
		      "dead %d -> %d (along %.2f m/s cos %.2f, %.1f cm in)", t, stab ? "stab" : sc_tier_name(tier),
		      (unsigned long long)o->c, somvr_stl_why(v), (double)hp0, (double)cb_health(o->c), d0, cb_dead(o->c),
		      (double)sp->axis_mps, (double)sp->cos_axis, (double)h->depth_cm);
	return 1;
}

/* stl_strike, after the knife's kill: the hold's end, the seen stimuli, the line */
static void kn_killed(uintptr_t pc, const struct cb_orc *o, uintptr_t rec, const somvr_stl_hit *h, double since_in, int dead0,
		      int dead1, int stick, const float E[3], const float ax[3], double t)
{
	g_kn.kills++;
	int held = kn_held(o->c);
	if (g_kn.c == o->c)
		kn_end(KN_END_DEAD, t);
	char sn[120] = "off (SOMVR_KNIFE_KILL_SEEN)";
	if (g_kn.seen && dead1 && !dead0) {
		uintptr_t ad = g_cb.ok_stim && pc && rec ? CB_FN(CS_ATTACKDATA, cb_attackdata_fn)(rec) : 0;
		if (ad) {
			int32_t id = 0;
			cb_stim_end(t, "the knife kill's stimuli");
			CB_FN(CS_STIMULUS, cb_stim_fn)(pc, ad, o->hobj); /* as Combat_StartAttack registers its record's */
			peek(pc + C_STIM, &id, sizeof id);
			g_cb.stim.on = 1, g_cb.stim.pc = pc, g_cb.stim.c = o->c, g_cb.stim.hobj = o->hobj, g_cb.stim.id = id,
			g_cb.stim.t = t;
			g_kn.seen_id = id ? id : -1, g_kn.seen_end = t + KN_SEEN_S, g_kn.seen_n++;
			snprintf(sn, sizeof sn, "registered (the silent record's: persistent id %d, off in %.0f s)", id, KN_SEEN_S);
		} else {
			g_kn.seen_fail++;
			snprintf(sn, sizeof sn, "NOT registered (%s)", !g_cb.ok_stim ? "a stimulus site did not match" : "no AttackData");
		}
	}
	logf_("KNIFE t=%.3f kill: orc %#llx dead %d -> %d, %.0f ms after the point went in (%.1f cm deep at %.1f,%.1f,%.1f; "
	      "along %.2f m/s cos %.2f, into the body cos %.2f; %s); %s; the stab's way %.2f,%.2f,%.2f; the seen stimuli %s; "
	      "no sync, no attack of Talion's", t, (unsigned long long)o->c, dead0, dead1, since_in * 1e3, (double)h->depth_cm,
	      (double)E[0], (double)E[1], (double)E[2], (double)h->axis_mps, (double)h->cos_axis, (double)h->cos_in,
	      held ? "held by the free hand" : "not held", stick ? "the knife sticks" : "no stick", (double)ax[0], (double)ax[1],
	      (double)ax[2], sn);
}

static const char *kn_word(const char *w)
{
	if (!strcmp(w, "status"))
		return atomic_store(&g_kn.req_status, 1), "knife status";
	if (!strcmp(w, "where"))
		return atomic_store(&g_kn.req_where, 1), "knife where";
	if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
		return "knife (SOMVR_HANDS_TEST only: ignored)";
	if (!strcmp(w, "on") || !strcmp(w, "off"))
		return atomic_store(&g_kn.req_mode, w[1] == 'n' ? 2 : 1), w[1] == 'n' ? "knife on" : "knife off";
	if (!strncmp(w, "push ", 5)) { /* build 40: a test aid (175.3) */
		float d = 0, ms = 0;
		if (sscanf(w + 5, "%f %f", &d, &ms) < 2 || d < -30 || d > 40 || ms < 50 || ms > 3000)
			return "knife push D MS [sword] [turn] (D -30..40 cm past his back, MS 50..3000: ignored)";
		g_kn.push_d = d, g_kn.push_ms = ms, g_kn.push_sword = strstr(w + 5, "sword") != NULL;
		g_kn.push_turn = strstr(w + 5, "turn") != NULL;
		return atomic_store(&g_kn.req_push, 1), "knife push (a test: the hand's push onto his back, measured this frame)";
	}
	if (!strcmp(w, "seen on") || !strcmp(w, "seen off"))
		return atomic_store(&g_kn.req_seen, w[6] == 'n' ? 2 : 1), w[6] == 'n' ? "knife seen on" : "knife seen off";
	return "knife (not status|where|on|off|seen on|off: ignored)";
}

/* after stl_start (its cfg's defaults) */
static void kn_start(int *bad)
{
	static const char *const onoff[] = { "off", "on" };
	g_kn.on = env_word("SOMVR_KNIFE_KILL", onoff, 2, 0);
	g_kn.hold = env_word("SOMVR_KNIFE_KILL_HOLD", onoff, 2, 1);
	g_kn.seen = env_word("SOMVR_KNIFE_KILL_SEEN", onoff, 2, 1);
	g_kn.stick = env_word("SOMVR_KNIFE_KILL_STICK", onoff, 2, 1);
	/* the knife's blade as the player sees it: Acharn sits in a reverse grip - the blade out of the pinky side of the
	 * fist, along the grip's +Z (boot 20261104-180058 frame 1181: the right fist's blade back along the forearm; build
	 * 36's 232753 frame 737). The judge's dagger blade (since build 19) ran from the thumb side (-Z, as the sword's):
	 * its "point" was where no blade is drawn. reverse: the judge's knife turned 180 deg about the grip's x */
	g_kn.reverse = env_word("SOMVR_KNIFE_KILL_GRIP", (const char *const[]){ "forward", "reverse" }, 2, 1);
	g_kn.grab_cm = env_float("SOMVR_KNIFE_KILL_GRAB_CM", g_stl.grab_cm > 0 ? g_stl.grab_cm : 45.0f, 5, 80, bad);
	g_kn.hold_s = env_float("SOMVR_KNIFE_KILL_HOLD_S", 8, 1, 30, bad);
	g_kn.stick_cm = env_float("SOMVR_KNIFE_KILL_STICK_CM", 4, 0, 15, bad);
	somvr_stl_cfg *c = &g_stl.cfg;
	c->knife_mps = env_float("SOMVR_KNIFE_KILL_MPS", c->knife_mps, 0.05f, 5, bad);
	c->knife_cos = cosf(env_float("SOMVR_KNIFE_KILL_DEG", 45, 5, 80, bad) * 0.01745329f);
	c->knife_in_cos = cosf(env_float("SOMVR_KNIFE_KILL_IN_DEG", 60, 5, 89, bad) * 0.01745329f);
	c->knife_depth_cm = env_float("SOMVR_KNIFE_KILL_DEPTH_CM", c->knife_depth_cm, 1, 30, bad);
	c->knife_behind_deg = env_float("SOMVR_KNIFE_KILL_BEHIND_DEG", c->knife_behind_deg, 60, 180, bad);
	if (g_kn.on && (!g_stl.mode || !g_stl.dagger)) {
		logf_("CONFIG knife: SOMVR_KNIFE_KILL=on needs SOMVR_STEALTH_KILL and its dagger (SOMVR_STEALTH_KILL_DAGGER=1): off");
		g_kn.on = 0;
	}
	c->knife = g_kn.on;
	if (g_kn.on && g_kn.reverse)
		g_stl.dcfg.pitch_rad = 3.14159265f; /* the blade along the grip's +Z (base 4 cm toward the pommel, the point 30 cm) */
	logf_("CONFIG knife=%s (SOMVR_KNIFE_KILL, build 37): %s", g_kn.on ? "on" : "off", g_kn.on ?
	      "the knife's stab kills an unaware orc from behind or beside, the free hand holds him, a fighting orc a light hit" :
	      "the knife as in build 36 (the stealth judge's thrust rule; no hit on a fighting orc)");
	if (g_kn.on)
		logf_("CONFIG knife: the stab %.2f m/s along the blade, within %.0f deg of it, the blade within %.0f deg of straight in, "
		      "%.0f cm deep, the hand's own push; behind or beside (%.0f deg off its facing); the hold %s (reach %.0f cm, "
		      "up to %.0f s; his attacks refused only with SOMVR_KILLS' AI hold); the stick %s (a pull of %.0f cm); the seen "
		      "stimuli %s; the knife's blade %s (SOMVR_KNIFE_KILL_GRIP)", (double)c->knife_mps, acosf(c->knife_cos) * 57.29578f, acosf(c->knife_in_cos) * 57.29578f,
		      (double)c->knife_depth_cm, (double)c->knife_behind_deg, g_kn.hold ? "on" : "off", (double)g_kn.grab_cm,
		      (double)g_kn.hold_s, g_kn.stick ? "on" : "off", (double)g_kn.stick_cm, g_kn.seen ? "on" : "off",
		      g_kn.reverse ? "reverse: out of the pinky side, as Acharn is drawn" : "forward: out of the thumb side (builds 19-36)");
}

static void kn_end_log(void)
{
	if (!g_kn.on && !g_kn.holds && !g_kn.kills)
		return;
	logf_("KNIFE END: holds %llu (the grip opened %llu, the hand left %llu, he died %llu, the knife back %llu, taken %llu, "
	      "broke free %llu, gone %llu), squeezes with no hold %llu; kills %llu; light hits %llu (too slow %llu, not in the "
	      "body %llu); seen stimuli %llu (failed %llu); the VERBS hand skipped %llu stealth targets", (unsigned long long)g_kn.holds,
	      (unsigned long long)g_kn.ends[KN_END_OPEN], (unsigned long long)g_kn.ends[KN_END_LEFT],
	      (unsigned long long)g_kn.ends[KN_END_DEAD], (unsigned long long)g_kn.ends[KN_END_BELT],
	      (unsigned long long)g_kn.ends[KN_END_TAKEN], (unsigned long long)g_kn.ends[KN_END_FREE],
	      (unsigned long long)g_kn.ends[KN_END_GONE], (unsigned long long)g_kn.misses, (unsigned long long)g_kn.kills,
	      (unsigned long long)g_kn.lights, (unsigned long long)g_kn.light_slow, (unsigned long long)g_kn.light_air,
	      (unsigned long long)g_kn.seen_n, (unsigned long long)g_kn.seen_fail, (unsigned long long)g_kn.vb_skips);
}
