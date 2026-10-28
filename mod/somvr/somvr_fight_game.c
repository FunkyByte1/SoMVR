/* somvr_fight_game.c - build 10's fight, the game side (included last by somvr.c; handoff/HANDOFF-combat.md round 3,
 * docs/re-xr-pipeline.md 30.12 and up). Headset session 3 (build 9, 2026-10-24): "whenever I draw my sword my hands fly
 * out", "nothing is happening when I'm getting hit", "when I hit an orc they fly really far away".
 *
 *   SOMVR_ARMS_LW=held|anim   the weapon node's transform in the hand (sa_lw_hold, somvr_arms.h): held = captured once
 *                        from a steady idle hand with the sword sheathed and kept, the weapon node put back on the solved
 *                        hand at it (the hand and the sword rigid on the controller); anim (the default, build 9) = as
 *                        animated each frame. Both: the measure (how far the animated one would put the hand off the
 *                        controller) in `ARMS lw` lines, and a watch of 3 s after each draw or put-back (`ARMS watch`):
 *                        the hand nodes the renderer gets (GetNodeTransform at H4) against the solved ones.
 * Lines: `CONFIG arms lw ...`, `ARMS lw L|R captured ...`, `ARMS lw t=... last N s: ...`, `ARMS watch ...`,
 * `ARMS watch end ...`, `FIGHT END ...`.
 */

#define AW_STEADY_FRAMES 20   /* the capture: the animated weapon node steady this many frames (0.22 s at 90 Hz) */
#define AW_WATCH_S 3.0        /* the watch after a draw / put-back */
#define AW_WATCH_EVERY 6      /* ... a line every this many frames (15 a second at 90 Hz) */
enum { AW_ANIM = 0, AW_HELD = 1 };
static const char *const g_aw_mode_w[] = { "anim", "held" };
struct aw_side {
	sa_lw_hold hold;
	sa_xf anim;                /* this frame's animated weapon node in the hand */
	int have_anim, used_held;
	float off_cm, off_deg;     /* the animated one against the held one */
	float hand_cm;             /* how far the animated one puts the hand from the held (controller-rigid) target */
	int have_m;
	sv_vec3 hand_m;            /* the solved hand (model space) this frame's H4 checks */
};
struct aw_win { float off_cm[2], off_deg[2], hand_cm[2], node_cm[2]; uint64_t frames, held[2], nodes; };
static struct {
	int mode;
	uintptr_t skel;            /* the skeleton the captures belong to */
	struct aw_side s[2];
	sa_xf X;                   /* the object's transform of this phase 2 (the H4 check) */
	int have_x;
	uint32_t hand_hash[2];
	/* the watch */
	double watch_t0, watch_until;
	const char *watch_why;
	uint64_t watch_n;
	struct aw_win ww;          /* the watch's maxima */
	struct aw_win w, tot;      /* since the last line, totals */
	double next_log, last_line;
	uint64_t watches, captures;
} g_aw = { .hand_hash = { SA_H_L_HAND, SA_H_R_HAND } };

static void aw_win_max(struct aw_win *a, const struct aw_win *b)
{
	for (int h = 0; h < 2; h++) {
		a->off_cm[h] = fmaxf(a->off_cm[h], b->off_cm[h]), a->off_deg[h] = fmaxf(a->off_deg[h], b->off_deg[h]);
		a->hand_cm[h] = fmaxf(a->hand_cm[h], b->hand_cm[h]), a->node_cm[h] = fmaxf(a->node_cm[h], b->node_cm[h]);
		a->held[h] += b->held[h];
	}
	a->frames += b->frames, a->nodes += b->nodes;
}

/* arm_eval, per arm with a valid grip: the weapon node's transform in the hand to put the hand target at (tw (x) lw^-1):
 * the held one once captured with SOMVR_ARMS_LW=held, else the animated one; both measured */
static sa_xf aw_lw(int h, sa_xf tw)
{
	struct aw_side *a = &g_aw.s[h];
	if (g_arm.skel != g_aw.skel) { /* a new skeleton: captures again */
		g_aw.skel = g_arm.skel;
		for (int k = 0; k < 2; k++)
			sa_lw_reset(&g_aw.s[k].hold), g_aw.s[k].have_anim = 0;
	}
	sa_xf lw = sa_weapon_local(g_arm.pose, &g_arm.ch[h]);
	a->anim = lw, a->have_anim = g_arm.ch[h].wpn >= 0;
	a->used_held = 0;
	if (!a->have_anim)
		return lw;
	/* the capture: the idle hand with the sword sheathed (SOMVR_SLOTS' last read; without the slots any steady hand) */
	int sheathed = !g_sl.on || (g_sl.have_g && g_sl.g.item[0] && g_sl.g.sheathed && !g_sl.sw_on);
	if (sa_lw_step(&a->hold, lw, sheathed && !bo_out(), AW_STEADY_FRAMES)) {
		g_aw.captures++;
		sv_vec3 e = sv_qrot(a->hold.ref.q, (sv_vec3){ 0, 1, 0 });
		logf_("ARMS lw %s captured t=%.3f: the weapon node %.2f,%.2f,%.2f cm in the hand (%.1f cm), its +Y %.3f,%.3f,%.3f "
		      "(steady %d frames, the sword %s)%s", g_arm_side[h], cb_now(), a->hold.ref.p.x, a->hold.ref.p.y,
		      a->hold.ref.p.z, sqrtf(a->hold.ref.p.x * a->hold.ref.p.x + a->hold.ref.p.y * a->hold.ref.p.y +
		      a->hold.ref.p.z * a->hold.ref.p.z), e.x, e.y, e.z, AW_STEADY_FRAMES, g_sl.on ? "sheathed" : "(no slots)",
		      g_aw.mode == AW_HELD ? ": held from now on" : " (measure only: SOMVR_ARMS_LW=anim)");
	}
	if (!a->hold.have)
		return lw;
	a->off_cm = sa_xf_dist(lw, a->hold.ref, &a->off_deg);
	a->hand_cm = sa_xf_dist(sa_hand_target_lw(tw, lw), sa_hand_target_lw(tw, a->hold.ref), NULL);
	g_aw.w.off_cm[h] = fmaxf(g_aw.w.off_cm[h], a->off_cm), g_aw.w.off_deg[h] = fmaxf(g_aw.w.off_deg[h], a->off_deg);
	g_aw.w.hand_cm[h] = fmaxf(g_aw.w.hand_cm[h], a->hand_cm);
	if (g_aw.mode != AW_HELD)
		return lw;
	a->used_held = 1, g_aw.w.held[h]++;
	return a->hold.ref;
}

/* arm_eval, after the solves (before the write-back): the held weapon nodes back on their solved hands; the solved
 * hands for this frame's H4 check */
static void aw_after(const int done[2], sa_xf X)
{
	g_aw.X = X, g_aw.have_x = 1;
	g_aw.w.frames++;
	for (int h = 0; h < 2; h++) {
		struct aw_side *a = &g_aw.s[h];
		a->have_m = done[h];
		if (!done[h])
			continue;
		if (a->used_held)
			sa_weapon_set(g_arm.pose, &g_arm.ch[h], a->hold.ref);
		const sa_node *n = &g_arm.pose[g_arm.ch[h].w];
		a->hand_m = (sv_vec3){ n->pos[0], n->pos[1], n->pos[2] };
	}
}

/* a slots event (sl_frame): watch the hands for AW_WATCH_S */
static void aw_watch(double t, const char *why)
{
	if (!g_arm.on)
		return;
	if (t >= g_aw.watch_until) /* a new watch (one running is extended) */
		memset(&g_aw.ww, 0, sizeof g_aw.ww), g_aw.watch_t0 = t, g_aw.watch_n = 0, g_aw.watches++;
	g_aw.watch_until = t + AW_WATCH_S, g_aw.watch_why = why;
}

/* H4 (arm_frame): each hand node as the renderer gets it (GetNodeTransform, world) against this frame's solved hand
 * through the object's transform: ~0 = nothing after phase 2 moved the hands */
static void aw_h4(uintptr_t obj)
{
	double t = cb_now();
	struct aw_win f;
	memset(&f, 0, sizeof f);
	uintptr_t m = peek_ptr(g_som.model_pp);
	float op[3], oq[4];
	int xok = g_aw.have_x && obj && m && g_node_ok && peek(obj + OBJ_POS, op, sizeof op) && peek(obj + OBJ_ROT, oq, sizeof oq) &&
		  vec_ok(op, 1e7f);
	g_aw.have_x = 0;
	for (int h = 0; h < 2 && xok; h++) {
		struct aw_side *a = &g_aw.s[h];
		if (!a->have_m)
			continue;
		float xf[16] = { 0 };
		if (((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, obj, g_aw.hand_hash[h], xf, 1) & 0xff || !vec_ok(xf, 1e7f))
			continue;
		sa_xf Xn = { { op[0], op[1], op[2] }, sv_qnormalize(sv_quat_of(oq)) };
		sv_vec3 w = sa_xf_mul(Xn, (sa_xf){ a->hand_m, { 0, 0, 0, 1 } }).p;
		float d = sqrtf((xf[0] - w.x) * (xf[0] - w.x) + (xf[1] - w.y) * (xf[1] - w.y) + (xf[2] - w.z) * (xf[2] - w.z));
		if (!isfinite(d))
			continue;
		f.node_cm[h] = d, f.nodes++;
		g_aw.w.node_cm[h] = fmaxf(g_aw.w.node_cm[h], d);
		a->have_m = 0;
	}
	g_aw.w.nodes += f.nodes;
	if (g_aw.watch_until > 0 && t < g_aw.watch_until) {
		for (int h = 0; h < 2; h++) {
			struct aw_side *a = &g_aw.s[h];
			f.off_cm[h] = a->hold.have ? a->off_cm : 0, f.off_deg[h] = a->hold.have ? a->off_deg : 0;
			f.hand_cm[h] = a->hold.have ? a->hand_cm : 0, f.held[h] = a->used_held;
		}
		f.frames = 1;
		aw_win_max(&g_aw.ww, &f);
		if (g_aw.watch_n++ % AW_WATCH_EVERY == 0)
			logf_("ARMS watch +%.2f s (%s): the animated weapon node off the held one L %.1f cm %.0f deg, R %.1f cm %.0f deg "
			      "-> the hand %s off the controller-rigid target by L %.1f R %.1f cm; the hand nodes at H4 vs the solved L "
			      "%.2f R %.2f cm", t - g_aw.watch_t0, g_aw.watch_why ? g_aw.watch_why : "?", f.off_cm[0], f.off_deg[0],
			      f.off_cm[1], f.off_deg[1], g_aw.mode == AW_HELD ? "would be (held: not)" : "is", f.hand_cm[0], f.hand_cm[1],
			      f.node_cm[0], f.node_cm[1]);
	} else if (g_aw.watch_until > 0) {
		const struct aw_win *x = &g_aw.ww;
		logf_("ARMS watch end (%s, %.1f s, %llu frames): the animated weapon node off the held one up to L %.1f cm %.0f deg, "
		      "R %.1f cm %.0f deg; the hand %s off the controller-rigid target up to L %.1f R %.1f cm (held frames L %llu R "
		      "%llu); the hand nodes at H4 vs the solved up to L %.2f R %.2f cm", g_aw.watch_why ? g_aw.watch_why : "?",
		      AW_WATCH_S, (unsigned long long)x->frames, x->off_cm[0], x->off_deg[0], x->off_cm[1], x->off_deg[1],
		      g_aw.mode == AW_HELD ? "would have been (held: was not)" : "was", x->hand_cm[0], x->hand_cm[1],
		      (unsigned long long)x->held[0], (unsigned long long)x->held[1], x->node_cm[0], x->node_cm[1]);
		aw_win_max(&g_aw.tot, x);
		g_aw.watch_until = 0;
	}
	if (t >= g_aw.next_log && (g_aw.w.frames || t - g_aw.last_line >= 10.0)) {
		struct aw_win *w = &g_aw.w;
		int busy = w->off_cm[0] > 0.5f || w->off_cm[1] > 0.5f || w->node_cm[0] > 0.5f || w->node_cm[1] > 0.5f;
		if (busy || t - g_aw.last_line >= 10.0) {
			logf_("ARMS lw t=%.3f last %.1f s (%s): the animated weapon node off the held one up to L %.1f cm %.0f deg, R "
			      "%.1f cm %.0f deg (the hand off the controller-rigid target by up to L %.1f R %.1f cm with it); held "
			      "frames L %llu R %llu of %llu; the hand nodes at H4 vs the solved up to L %.2f R %.2f cm (%llu checks)",
			      t, t - g_aw.last_line, g_aw_mode_w[g_aw.mode], w->off_cm[0], w->off_deg[0], w->off_cm[1], w->off_deg[1],
			      w->hand_cm[0], w->hand_cm[1], (unsigned long long)w->held[0], (unsigned long long)w->held[1],
			      (unsigned long long)w->frames, w->node_cm[0], w->node_cm[1], (unsigned long long)w->nodes);
			aw_win_max(&g_aw.tot, w);
			memset(w, 0, sizeof *w);
			g_aw.last_line = t;
		}
		g_aw.next_log = t + 1.0;
	}
}

/* arm_config's (after the arms' switches) */
static void aw_config(int *bad)
{
	(void)bad;
	g_aw.mode = env_word("SOMVR_ARMS_LW", g_aw_mode_w, 2, AW_ANIM);
	if (g_arm.on)
		logf_("CONFIG arms lw=%s: the weapon node in the hand %s; captured from the idle hand (steady %d frames, the sword "
		      "sheathed); the animated one measured against it (`ARMS lw`, `ARMS watch` for %.0f s after a draw)",
		      g_aw_mode_w[g_aw.mode], g_aw.mode == AW_HELD ? "held at the captured one (the hand and the sword rigid on the "
		      "controller)" : "as animated (build 9)", AW_STEADY_FRAMES, AW_WATCH_S);
}


/* ---- the orcs' reactions to our hits and parries (build 10 item 5: "they fly really far away") -----------------------
 * kb_note after each Combat_DealHit of ours (cb_hit) and each clank (df_clank): the orc's root then; kb_frame follows it
 * for KB_WATCH_S: its attacks (the reaction the game picked: 'AI_Bip_Recoil_F_KD_STR' = a knockdown, 'AI_Bip_H300_*' a
 * heavy stagger, 'AI_Bip_L100_*' a light one) and how far its root went (horizontal, from the root at the hit). Line:
 * `COMBAT reaction orc=.. after <what> ...: its attacks 'A' -> 'B' ..., moved X m (at most Y m) in Z s`. */
#define KB_N 8
#define KB_WATCH_S 2.5
#define KB_NAMES 4
/* SOMVR_COMBAT_DEATH (build 11, a killing blow; the code after the knockback's): kd_pre's note for kb_note */
static struct {
	int mode;                  /* KD_GAME: the game's; KD_TYPE: the blow's RecoilTypeHit = type */
	char type[40];
	float dir, pin_cm;         /* the hit direction's scale for a kill (1 = the game's); the test pin (0 = off) */
	char note[96];
	uintptr_t pre_c;           /* the orc of the call kd_pre saw, dead or not before it */
	int pre_dead;
	uint64_t kills, typed, scaled, pins, missing;
	float max_cm;
} g_kd = { .dir = 1 };
#define KB_DEATH_S 4.0     /* a kill: followed this long (build 10's 2.5 s ended inside the 2.55 s death reaction) */
#define KB_TRACE 16
struct kb_watch {
	uintptr_t c, hobj, rec;
	double t0, react_t;        /* the hit; when the reaction ended (its attack changed again) */
	float p0[3], max_cm, end_cm, react_cm;
	char over[128];            /* what our override gave the hit (SOMVR_COMBAT_KNOCKBACK), "" = none */
	uintptr_t atk[KB_NAMES];
	int natk, dead0, dead;
	char what[48];
	float tr[KB_TRACE];        /* build 11: the root's distance from the hit every 0.25 s (a kill's throw: its profile) */
	int ntr, pins, pin_held;   /* SOMVR_COMBAT_DEATH_PIN_CM: the writes back, the frames it stayed */
	float y0, dy;              /* the root's height at the hit, its lowest change since */
	char death[96];            /* SOMVR_COMBAT_DEATH: what the killing blow was given ("" = the game's) */
};
static struct {
	struct kb_watch w[KB_N];
	uint64_t notes, done;
	float max_cm, react_max_cm; /* the largest moves so far (FIGHT END) */
	char over[128];            /* the override of the hit kb_note is about to note (cb_hit) */
} g_kb;

static void kb_note(uintptr_t c, uintptr_t hobj, uintptr_t rec, const char *what, double t)
{
	int k = -1;
	for (int i = 0; i < KB_N && k < 0; i++) /* the same orc again: a new watch from here */
		if (g_kb.w[i].c == c)
			k = i;
	for (int i = 0; i < KB_N && k < 0; i++)
		if (!g_kb.w[i].c)
			k = i;
	if (k < 0)
		return;
	struct kb_watch *w = &g_kb.w[k];
	memset(w, 0, sizeof *w);
	if (!hobj || !peek(hobj + OBJ_POS, w->p0, sizeof w->p0) || !vec_ok(w->p0, 1e7f))
		return;
	w->c = c, w->hobj = hobj, w->rec = rec, w->t0 = t, w->y0 = w->p0[1];
	w->dead0 = g_kd.pre_c == c ? g_kd.pre_dead : cb_dead(c); /* (a hit: before the call; build 10 read it after) */
	g_kd.pre_c = 0;
	snprintf(w->death, sizeof w->death, "%s", g_kd.note);
	g_kd.note[0] = 0;
	w->atk[0] = peek_ptr(c + RX_C_ATTACK), w->natk = 1;
	snprintf(w->what, sizeof w->what, "%s", what);
	snprintf(w->over, sizeof w->over, "%s", g_kb.over);
	g_kb.over[0] = 0;
	g_kb.notes++;
}

static void kb_frame(double t)
{
	for (int i = 0; i < KB_N; i++) {
		struct kb_watch *w = &g_kb.w[i];
		if (!w->c)
			continue;
		float p[3];
		int gone = !peek_ptr(w->c + C_OWNER) || !peek(w->hobj + OBJ_POS, p, sizeof p) || !vec_ok(p, 1e7f);
		if (!gone) {
			float d = hypotf(p[0] - w->p0[0], p[2] - w->p0[2]);
			w->dead = cb_dead(w->c);
			if (w->dead && !w->dead0 && g_kd.pin_cm > 0 && d > g_kd.pin_cm) { /* SOMVR_COMBAT_DEATH_PIN_CM (a test) */
				float k = g_kd.pin_cm / d, q[3] = { w->p0[0] + (p[0] - w->p0[0]) * k, p[1], w->p0[2] + (p[2] - w->p0[2]) * k };
				w->pins += arm_poke(w->hobj + OBJ_POS, q, sizeof q) != 0;
				g_kd.pins++;
			} else if (w->pins)
				w->pin_held++;
			w->end_cm = d, w->max_cm = fmaxf(w->max_cm, d), w->dy = fminf(w->dy, p[1] - w->y0);
			while (w->ntr < KB_TRACE && t - w->t0 >= 0.25 * (w->ntr + 1))
				w->tr[w->ntr++] = d;
			uintptr_t a = peek_ptr(w->c + RX_C_ATTACK);
			if (a != w->atk[w->natk - 1] && w->natk < KB_NAMES)
				w->atk[w->natk++] = a;
			if (w->natk <= 2) /* the attack at the hit, then the reaction: its own move */
				w->react_cm = fmaxf(w->react_cm, d), w->react_t = t - w->t0;
		}
		if (!gone && t - w->t0 < (w->dead && !w->dead0 ? KB_DEATH_S : KB_WATCH_S))
			continue;
		char an[KB_NAMES][96], rn[96], rh[64] = "";
		for (int k = 0; k < KB_NAMES; k++)
			an[k][0] = 0;
		for (int k = 0; k < w->natk; k++)
			rx_rec_name(w->atk[k], an[k], sizeof an[k]);
		uintptr_t ad = w->rec && g_df.ok_ad ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(w->rec) : 0;
		rx_rec_name(ad ? rx_db_rec(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0, rh, sizeof rh);
		g_kb.max_cm = fmaxf(g_kb.max_cm, w->max_cm), g_kb.done++;
		g_kb.react_max_cm = fmaxf(g_kb.react_max_cm, w->react_cm);
		logf_("COMBAT reaction orc=%#lx after %s ('%s', its RecoilTypeHit '%s'%s%s): its attacks '%s'%s%s%s%s%s%s; moved %.2f m "
		      "during the reaction (%.2f s), %.2f m at the end, at most %.2f m in %.1f s%s%s", (unsigned long)w->c, w->what,
		      rx_rec_name(w->rec, rn, sizeof rn), rh, w->over[0] ? ", for the hit: " : "", w->over, an[0],
		      w->natk > 1 ? " -> '" : "", an[1], w->natk > 1 ? "'" : "", w->natk > 2 ? " -> '" : "",
		      w->natk > 2 ? an[2] : "", w->natk > 2 ? "'" : "", w->react_cm * 0.01f, w->react_t, w->end_cm * 0.01f,
		      w->max_cm * 0.01f, t - w->t0, w->dead && !w->dead0 ? " (it died)" : "", gone ? " (gone)" : "");
		if (w->dead && !w->dead0) { /* build 11: the kill's throw, its profile (a slide = the death's root motion) */
			char tr[KB_TRACE * 6 + 8] = "";
			size_t o = 0;
			for (int k = 0; k < w->ntr && o < sizeof tr - 8; k++)
				o += (size_t)snprintf(tr + o, sizeof tr - o, "%s%.2f", k ? " " : "", w->tr[k] * 0.01f);
			g_kd.kills++, g_kd.max_cm = fmaxf(g_kd.max_cm, w->max_cm);
			logf_("COMBAT death orc=%#lx: the body %.2f m from the hit at the end (at most %.2f m), its root %+.0f cm lower at "
			      "most; every 0.25 s (m): %s; the killing blow: %s%s", (unsigned long)w->c, w->end_cm * 0.01f,
			      w->max_cm * 0.01f, w->dy, tr, w->death[0] ? w->death : "the game's",
			      w->pins ? "; pinned" : "");
			if (w->pins)
				logf_("COMBAT death orc=%#lx pin: %d writes back to %.0f cm, %d frames it stayed", (unsigned long)w->c,
				      w->pins, g_kd.pin_cm, w->pin_held);
		}
		memset(w, 0, sizeof *w);
	}
}

/* ---- SOMVR_COMBAT_KNOCKBACK=light|medium|off (build 10 item 5) ------------------------------------------------------
 * Boot 1 (logs/boot-check-20261024-173938): our heavy record's RecoilTypeHit 'Heavy_Straight' put the berserker into
 * 'AI_Bip_Recoil_F_KD_STR' (a knockdown, 3.9 m), the stab's 'Light_Straight' an infantry into 'AI_Bip_Recoil_F_ToStun_STR'
 * (3.1 m); the strikes' light recoils ('Light_RightToLeft', 'Light_UpToDown': 'AI_Bip_L100_*') are a stagger. The hit's
 * damage is the record's; the reaction is picked from its AttackData's RecoilTypeHit inside Combat_DealHit. So for our
 * call the record's RecoilTypeHit slot holds a lighter recoil type of the swing's direction ('<Light|Medium>_<RightToLeft|
 * LeftToRight|UpToDown|DownToUp>', by the blade's velocity in the orc's frame), put back right after (as Talion's attack
 * type byte is, cb_type_set). light: every hit 'Light_*'; medium: a heavy hit 'Medium_*', the rest 'Light_*'. And a clank's
 * recoil whose type is 'Heavy_*' (an infantry's 'Front_3': 'AI_Bip_H300_U2D_F', ~3 m) is taken from the same direction's
 * lighter one. The recoil records by name: every attack record's AttackData RecoilTypeHit/Block/Parry, KX_STEP records a
 * frame once the database is loaded. */
#define KX_N 96
#define KX_STEP 200
enum { KX_OFF = 0, KX_LIGHT = 1, KX_MEDIUM = 2, KX_STAGGER = 3 };
static const char *const g_kx_mode_w[] = { "off", "light", "medium", "stagger" };
static struct {
	int mode, state;           /* state: 0 not yet, 1 building, 2 done */
	uint32_t pos, count;
	int n;
	struct { uintptr_t rec; char name[40]; } e[KX_N];
	uint64_t overrides, missing, clank_lighter, restaggers, restagger_took, staggers, stagger_undone;
	float light_dmg;           /* =stagger: a light hit's damage (learnt; 10 on the practice orcs) */
	float sliver;              /* SOMVR_COMBAT_SLIVER (build 16): a hit that would leave an orc this much health or less
				    * kills it (0 = only a hit whose damage kills; build 15b's session: a shield orc left at 5 of
				    * 125 never attacked again and the game ended the fight with it alive) */
	uint64_t slivers;
} g_kx = { .light_dmg = 10 };

static void kx_step(void)
{
	if (!g_kx.mode || g_kx.state == 2 || !g_df.ok_ad)
		return;
	uintptr_t cat = peek_ptr(g_cbg.atkcat_pp), base = cat ? peek_ptr(cat + CAT_RECS) : 0;
	uint32_t cnt = 0;
	if (!base || !peek(cat + CAT_COUNT, &cnt, sizeof cnt) || !cnt)
		return;
	if (!g_kx.state)
		g_kx.state = 1, g_kx.count = cnt, g_kx.pos = 0;
	static const int fields[3] = { DF_AD_RECOIL_HIT, DF_AD_RECOIL_BLOCK, DF_AD_RECOIL_PARRY };
	uint32_t end = g_kx.pos + KX_STEP < g_kx.count ? g_kx.pos + KX_STEP : g_kx.count;
	for (uint32_t i = g_kx.pos; i < end; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE, ad = DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(rec);
		for (int f = 0; ad && f < 3; f++) {
			uintptr_t r = rx_db_rec(ad, g_dfg.sch_ad, fields[f]);
			int seen = !r;
			for (int k = 0; k < g_kx.n && !seen; k++)
				seen = g_kx.e[k].rec == r;
			if (seen || g_kx.n >= KX_N)
				continue;
			char nm[40];
			if (!rx_rec_name(r, nm, sizeof nm)[0])
				continue;
			g_kx.e[g_kx.n].rec = r, snprintf(g_kx.e[g_kx.n].name, sizeof g_kx.e[0].name, "%s", nm), g_kx.n++;
		}
	}
	g_kx.pos = end;
	if (end >= g_kx.count) {
		g_kx.state = 2;
		char ls[1200];
		size_t o = 0;
		ls[0] = 0;
		for (int k = 0; k < g_kx.n && o < sizeof ls - 48; k++)
			o += (size_t)snprintf(ls + o, sizeof ls - o, "%s'%s'", k ? " " : "", g_kx.e[k].name);
		logf_("COMBAT knockback: %d recoil types from %u attack records: %s", g_kx.n, g_kx.count, ls);
	}
}
static uintptr_t kx_find(const char *name)
{
	for (int k = 0; k < g_kx.n; k++)
		if (!strcmp(g_kx.e[k].name, name))
			return g_kx.e[k].rec;
	return 0;
}
/* the lighter recoil of a strength (Light/Medium) and a direction, with fallbacks (Medium -> Light; the direction ->
 * 'Light_RightToLeft'); 0 = none known */
static uintptr_t kx_pick(const char *str, const char *dir, char *nm, size_t cap)
{
	const char *tries[3][2] = { { str, dir }, { "Light", dir }, { "Light", "RightToLeft" } };
	for (int k = 0; k < 3; k++) {
		snprintf(nm, cap, "%s_%s", tries[k][0], tries[k][1]);
		uintptr_t r = kx_find(nm);
		if (r)
			return r;
	}
	nm[0] = 0;
	return 0;
}
/* a database record's single record-typed field i: its 8-byte value slot (0 = absent, another type or a list) */
static uintptr_t kx_slot(uintptr_t rec, uintptr_t holder, int i)
{
	unsigned char e[16];
	uint16_t cnt, vi;
	if (!rec || rx_db_entry(rec, rx_sch_hash(holder, i), e))
		return 0;
	memcpy(&cnt, e + 0xc, 2), memcpy(&vi, e + 0xe, 2);
	uintptr_t vals = peek_ptr(rec + 0x10);
	return (e[0xa] & 0x3f) == 0xa && cnt == 1 && vals ? vals + (uintptr_t)vi * 8 : 0;
}
/* the swing's direction against the orc's facing: its right axis (the blade moving to its right = from its left) and up */
static const char *kx_dir(const float vel[3], uintptr_t hobj)
{
	float oq[4] = { 0, 0, 0, 1 };
	if (!peek(hobj + OBJ_ROT, oq, sizeof oq) || !vec_ok(oq, 2.0f))
		return "RightToLeft";
	float yaw = sv_heading(sv_qnormalize(sv_quat_of(oq)));
	float rx = cosf(yaw), rz = -sinf(yaw); /* the orc's right in the world (+X right of +Z, yaw about +Y) */
	float dx = vel[0] * rx + vel[2] * rz, dy = vel[1];
	if (fabsf(dy) > fabsf(dx))
		return dy < 0 ? "UpToDown" : "DownToUp";
	return dx > 0 ? "LeftToRight" : "RightToLeft";
}
/* cb_hit, before Combat_DealHit: our record's RecoilTypeHit for the call (*slot, *was to put back; 0 = none) */
static int kx_hit_set(uintptr_t rec, int tier, const float vel[3], uintptr_t hobj, uintptr_t *slot, uint64_t *was)
{
	*slot = 0, *was = 0;
	g_kb.over[0] = 0;
	if (!g_kx.mode || g_kx.state != 2 || !g_df.ok_ad || !rec)
		return 0;
	uintptr_t ad = DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(rec), sl = kx_slot(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT);
	char nm[48];
	const char *dir = kx_dir(vel, hobj);
	if (g_kx.mode == KX_STAGGER) /* the hit's own reaction, then the stagger (kx_restagger) */
		return 0;
	uintptr_t r = kx_pick(g_kx.mode == KX_MEDIUM && tier == SC_TIER_HEAVY ? "Medium" : "Light", dir, nm, sizeof nm);
	if (!sl || !r || !peek(sl, was, 8)) {
		g_kx.missing++;
		snprintf(g_kb.over, sizeof g_kb.over, "no lighter recoil (%s)", !sl ? "no RecoilTypeHit slot" : !r ? "none known" : "?");
		return 0;
	}
	uint64_t v = (uint64_t)r;
	if (*was == v || !arm_poke(sl, &v, 8))
		return 0;
	*slot = sl;
	g_kx.overrides++;
	snprintf(g_kb.over, sizeof g_kb.over, "'%s' (%s)", nm, dir);
	return 1;
}
static void kx_hit_back(uintptr_t slot, uint64_t was)
{
	if (slot)
		arm_poke(slot, &was, 8);
}
/* cb_hit, after a Combat_DealHit that did not kill: the reaction the hit picked replaced by the light stagger of the
 * swing's direction - the orc's recoil FUN_02810af0(Talion, orc, the light record, 'Light_<dir>') as a clank's (boot 2,
 * logs/boot-check-20261024-182517: the RecoilTypeHit swap alone turned a stab's 'AI_Bip_Recoil_F_ToStun_STR' into
 * '_ToStun_R2L', still 3.2 m: the reaction's kind comes from our attack's categories, the direction from the type; the
 * clank's call with the light record picks 'AI_Bip_L100_*'). The damage stays the hit's. 1 = its recoil took */
static int kx_restagger(uintptr_t c, uintptr_t hobj, const float vel[3], const float point[3], double t)
{
	if ((g_kx.mode != KX_LIGHT && g_kx.mode != KX_MEDIUM) || g_kx.state != 2 || !g_df.ok_recoil || !g_df.ph[0] || cb_dead(c))
		return 0;
	const char *ln = "(none)";
	uintptr_t owner = peek_ptr(c + C_OWNER), light = cb_record(SC_TIER_LIGHT, 0, &ln);
	char nm[48];
	/* the orc's own strike's RecoilTypeHit when it is a light or medium one (a clank's recipe: an infantry's
	 * 'AI_Bip_L100_*', a berserker's 'AI_Bip_M200_*'), else 'Light_<the swing's direction>' */
	uintptr_t cur = peek_ptr(c + RX_C_ATTACK), ad = cur ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(cur) : 0;
	uintptr_t r = ad ? rx_db_rec(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0;
	if (!r || (strncmp(rx_rec_name(r, nm, sizeof nm), "Light_", 6) && strncmp(nm, "Medium_", 7)))
		r = kx_pick("Light", kx_dir(vel, hobj), nm, sizeof nm);
	if (!owner || !light || !r)
		return 0;
	int took = df_recoil_call(g_df.ph[0], owner, c, light, r, point);
	g_kx.restaggers++, g_kx.restagger_took += took;
	size_t o = strlen(g_kb.over);
	snprintf(g_kb.over + o, sizeof g_kb.over - o, "%sthen the stagger '%s' %s", o ? "; " : "", nm, took ? "took" : "refused");
	(void)t;
	return took;
}

/* =stagger (boot 5, logs/boot-check-20261024-190307: with the type swap and the restagger an infantry's heavy hits
 * still gave 'AI_Bip_Recoil_F_Lght_KD_*', a light knockdown, 1.9-2.5 m; the reaction's kind follows the hit's record):
 * a heavy hit or a stab that does not kill is dealt with the light record (a light hit's reaction: the stagger) after
 * the orc's health took the difference (the tier's damage = 4 / 2 x the light hit's, as on the practice orcs: 40 / 20 /
 * 10), so the damage stays the tier's; a hit that would kill leaves half a light hit's health and the light hit kills
 * (a light death). Not for a blocked one (the REACT block) or a shield up; a light call that does no damage gives the
 * difference back. 0 = the hit's own record */
static uintptr_t kx_stagger_rec(uintptr_t c, int tier, int blk, float hp0, float sh0, float *extra)
{
	*extra = 0;
	if (g_kx.mode != KX_STAGGER || !isfinite(hp0) || (isfinite(sh0) && sh0 > 0))
		return 0;
	uintptr_t own = peek_ptr(c + C_OWNER);
	if (tier == SC_TIER_LIGHT || blk) { /* SOMVR_COMBAT_SLIVER: a light hit, or one through the orc's block (its record's
					     * own damage: 40 for a High one, boot 035244), that would leave a sliver kills */
		float td = g_kx.light_dmg * (tier == SC_TIER_HEAVY ? 4.0f : tier == SC_TIER_STAB ? 2.0f : 1.0f), nl = 0.5f * g_kx.light_dmg;
		if (!(g_kx.sliver > 0) || !(hp0 - td > 0.5f) || hp0 - td > g_kx.sliver || !(hp0 > nl) || !own ||
		    !arm_poke(own + CH_HEALTH, &nl, sizeof nl))
			return 0;
		*extra = hp0 - nl, g_kx.slivers++;
		snprintf(g_kb.over, sizeof g_kb.over, "a %s hit%s leaving %.0f health (SOMVR_COMBAT_SLIVER %.0f): %.0f health off, "
			 "the kill", sc_tier_name(tier), blk ? " through its block" : "", hp0 - td, g_kx.sliver, *extra);
		return 0;
	}
	const char *ln = "(none)";
	uintptr_t light = cb_record(SC_TIER_LIGHT, 0, &ln);
	float e = g_kx.light_dmg * (tier == SC_TIER_HEAVY ? 3.0f : 1.0f), nh = hp0 - e;
	int kill = !(nh > g_kx.light_dmg + 0.5f); /* the tier's hit kills: the light hit kills too (a light death: boot 6's
						   * heavy record's 'AI_Bip_STR_Death_Hvy_F' threw the body 4.8 m) */
	if (!kill && g_kx.sliver > 0 && !(nh - g_kx.light_dmg > g_kx.sliver)) /* SOMVR_COMBAT_SLIVER: no sliver left */
		kill = 1, g_kx.slivers++;
	if (kill)
		nh = fminf(hp0, 0.5f * g_kx.light_dmg);
	if (!light || !own || !(nh > 0) || !arm_poke(own + CH_HEALTH, &nh, sizeof nh))
		return 0;
	*extra = hp0 - nh, g_kx.staggers++;
	snprintf(g_kb.over, sizeof g_kb.over, "dealt with the light record '%s' after %.0f health off%s", ln, *extra,
		 kill ? " (the kill a light one)" : "");
	return light;
}
/* after the call: a light call that did no damage gives the difference back; a light hit's damage is learnt */
static void kx_stagger_back(uintptr_t c, int tier, float hp0, float extra, float hp1)
{
	if (!isfinite(hp0) || !isfinite(hp1))
		return;
	float d = hp0 - extra - hp1;
	if (extra > 0 && !(d > 0.01f)) {
		uintptr_t own = peek_ptr(c + C_OWNER);
		if (own && arm_poke(own + CH_HEALTH, &hp0, sizeof hp0))
			g_kx.stagger_undone++;
		return;
	}
	if ((extra > 0 || tier == SC_TIER_LIGHT) && hp1 > 0 && d > 0.5f && d < 200) /* (a kill's damage is the health left) */
		g_kx.light_dmg = 0.7f * g_kx.light_dmg + 0.3f * d;
}

/* df_recoil: a clank's recoil record of type 'Heavy_<dir>' -> the lighter one of that direction (0 = keep it) */
static uintptr_t kx_clank(uintptr_t rec)
{
	char nm[64], nn[48];
	if (!g_kx.mode || g_kx.state != 2 || !rec || strncmp(rx_rec_name(rec, nm, sizeof nm), "Heavy_", 6))
		return 0;
	uintptr_t r = kx_pick(g_kx.mode == KX_MEDIUM ? "Medium" : "Light", nm + 6, nn, sizeof nn);
	g_kx.clank_lighter += r != 0;
	return r;
}

/* ---- the orcs' weapons (build 10 item 3: the contact block needs the blow's blade, not only its hand) ----------------
 * og_probe at an attack's impact: the orc's R_Hand and R_Weapon nodes and its active item's model objects (world), and
 * the nodes a blade model may carry; once per attack record, the first OG_MAX attacks. `FIGHT orc weapon ...` */
#define OG_MAX 16
static struct { uintptr_t seen[OG_MAX]; int n; } g_og;
static int og_node(uintptr_t m, uintptr_t obj, const char *name, float xf[8])
{
	uint32_t node = 0;
	memset(xf, 0, 8 * sizeof(float));
	uintptr_t mi = peek_ptr(obj + OBJ_MODEL), mv = vptr_of((void *)mi), f = mv ? peek_ptr(mv + 0x20) : 0;
	int pr = f ? page_prot(f) : -1;
	if (pr < 0 || !(pr & PROT_EXEC) || (TH_FN(THS_GETNODE, th_getnode_fn)(m, obj, name, &node) & 0xff))
		return 0;
	float x[16] = { 0 };
	if ((((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, obj, node, x, 1) & 0xff) || !vec_ok(x, 1e7f))
		return 0;
	memcpy(xf, x, 7 * sizeof(float));
	return 1;
}
static void og_probe(const struct th_track *tr, double t)
{
	if (g_og.n >= OG_MAX || !g_th.ok_node)
		return;
	for (int i = 0; i < g_og.n; i++)
		if (g_og.seen[i] == tr->attack)
			return;
	g_og.seen[g_og.n++] = tr->attack;
	uintptr_t m = peek_ptr(g_som.model_pp), owner = peek_ptr(tr->c + C_OWNER), inv = owner ? peek_ptr(owner + 0x910) : 0;
	float hand[8] = { 0 }, wpn[8] = { 0 };
	int hh = m && og_node(m, tr->hobj, "R_Hand", hand), hw = m && og_node(m, tr->hobj, "R_Weapon", wpn);
	char an[96], line[1400];
	size_t o = (size_t)snprintf(line, sizeof line, "FIGHT orc weapon orc=%#lx attack '%s' at %.3f s (impact %.3f): R_Hand %s "
				    "%.1f,%.1f,%.1f; R_Weapon %s %.1f,%.1f,%.1f q %.3f,%.3f,%.3f,%.3f", (unsigned long)tr->c,
				    rx_rec_name(tr->attack, an, sizeof an), t - tr->t0, tr->k.impact, hh ? "" : "(none)", hand[0],
				    hand[1], hand[2], hw ? "" : "(none)", wpn[0], wpn[1], wpn[2], wpn[3], wpn[4], wpn[5], wpn[6]);
	uint32_t slot = 8;
	uintptr_t item = inv && peek(inv + 0x280, &slot, 4) && slot <= 7 ? peek_ptr(inv + 0x30 + (uintptr_t)slot * 0x38) : 0;
	uintptr_t b = item ? peek_ptr(item + SL_IT_ENTRIES) : 0, e = item ? peek_ptr(item + SL_IT_ENTRIES + 8) : 0;
	int ne = b && e > b && (e - b) % SL_IT_ESIZE == 0 ? (int)((e - b) / SL_IT_ESIZE) : 0;
	o += (size_t)snprintf(line + o, sizeof line - o, "; active slot %u item %#lx entries %d", slot, (unsigned long)item, ne);
	static const char *const names[] = { "sword_tip", "sword_handle", "weapon_tip", "tip", "Tip", "blade_tip", "Blade_Tip",
					     "axe_tip", "weapon_end", "end" };
	for (int k = 0; k < ne && k < 4 && o < sizeof line - 200; k++) {
		uintptr_t obj = peek_ptr(b + (uintptr_t)k * SL_IT_ESIZE + SL_IE_OBJ);
		float op[3] = { 0, 0, 0 }, oq[4] = { 0, 0, 0, 1 };
		uint32_t act = 0, ina = 0;
		peek(b + (uintptr_t)k * SL_IT_ESIZE + SL_IE_ACT, &act, 4), peek(b + (uintptr_t)k * SL_IT_ESIZE + SL_IE_INACT, &ina, 4);
		if (!obj || !peek(obj + OBJ_POS, op, sizeof op) || !peek(obj + OBJ_ROT, oq, sizeof oq))
			continue;
		sv_vec3 y = sv_qrot(sv_qnormalize(sv_quat_of(oq)), (sv_vec3){ 0, 1, 0 });
		o += (size_t)snprintf(line + o, sizeof line - o, "; entry %d obj %#lx at %.1f,%.1f,%.1f (+Y %.2f,%.2f,%.2f; active node "
				      "%#x inactive %#x)", k, (unsigned long)obj, op[0], op[1], op[2], y.x, y.y, y.z, act, ina);
		for (size_t j = 0; j < sizeof names / sizeof names[0] && o < sizeof line - 120; j++) {
			float x[8];
			if (m && og_node(m, obj, names[j], x))
				o += (size_t)snprintf(line + o, sizeof line - o, " node '%s' at %.1f,%.1f,%.1f (%.1f cm from the obj)",
						      names[j], x[0], x[1], x[2], dist3(x, op));
		}
	}
	logf_("%s", line);
}

/* ---- Talion hit (build 10 item 4: "nothing is happening when I'm getting hit") ------------------------------------
 * The player's health (CCharacter+0x164) each frame: a drop = a hit, from whatever (a blow, an arrow, a fall).
 *   SOMVR_HURT=1         then: a strong pulse in both hands (a thump and its fading ring) and a short red flash on the
 *                        view's edges (SOMVR_THREAT's edge bands in the hurt red: a layer overlay, nothing moves), the
 *                        side the blow came from at full strength, the other at half (both full when the source is
 *                        unknown: an arrow, a fall); SOMVR_HURT_MS (300) the flash's length, SOMVR_HURT_FLASH (0.8)
 *                        its strength. 0 = the measure only (the default, build 9's behaviour).
 * The game's own hit sound and Talion's grunt come from its Combat_DealHit, which DF_IMPACT passes on for a landed
 * hit (untouched). `FIGHT hurt ...` */
static struct {
	int on;
	float ms, flash;
	uintptr_t own;
	float hp, hp_max;
	int have;
	uint64_t hurts, flashes;
	double last_t;
	/* the flash: its start, the side levels (0..1) */
	double flash_t;
	float side[2];
	/* the last landed blow (df_drain): its yaw off the view (rad, + right) */
	double blow_t;
	float blow_yaw;
} g_hu = { .ms = 300, .flash = 0.8f };

/* df_drain: a landed blow at Talion from the weapon hand at `from` (world cm) */
static void hu_blow(const float from[3], double t)
{
	if (!vec_ok(from, 1e7f))
		return;
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	g_hu.blow_yaw = sv_wrap_pi(atan2f(from[0] - hp.x, from[2] - hp.z) - sv_heading(hq)), g_hu.blow_t = t;
}

/* th_frame, before its put: the flash's levels this frame (0..255 per side) */
static void hu_flash(uint8_t out[2], double t)
{
	out[0] = out[1] = 0;
	float u = g_hu.flash_t > 0 && g_hu.ms > 0 ? (float)((t - g_hu.flash_t) * 1e3 / g_hu.ms) : 2;
	if (!g_hu.on || !(u >= 0 && u < 1))
		return;
	float k = (1 - u) * (1 - u); /* quick in, eased out */
	for (int s = 0; s < 2; s++)
		out[s] = (uint8_t)lrintf(255.0f * fminf(1.0f, g_hu.flash * g_hu.side[s] * k));
}

static void hu_frame(uintptr_t pl, int stereo, double t)
{
	(void)stereo;
	uintptr_t pc = pl ? peek_ptr(pl + P_COMBAT) : 0, sc = pc ? peek_ptr(pc + PC_SERVER) : 0;
	uintptr_t own = sc && vptr_of((void *)sc) == g_cbg.vt_combat ? peek_ptr(sc + C_OWNER) : 0;
	float hp = NAN;
	if (!own || !peek(own + CH_HEALTH, &hp, sizeof hp) || !isfinite(hp)) {
		g_hu.have = 0;
		return;
	}
	if (own != g_hu.own || !g_hu.have) {
		g_hu.own = own, g_hu.hp = hp, g_hu.have = 1, g_hu.hp_max = fmaxf(g_hu.hp_max, hp);
		return;
	}
	g_hu.hp_max = fmaxf(g_hu.hp_max, hp);
	if (hp < g_hu.hp - 0.01f) {
		g_hu.hurts++;
		float dmg = g_hu.hp - hp, yaw = NAN;
		const char *from = "unknown (both sides)";
		g_hu.side[0] = g_hu.side[1] = 1;
		if (g_hu.blow_t > 0 && t - g_hu.blow_t <= 0.3 && isfinite(g_hu.blow_yaw)) {
			yaw = g_hu.blow_yaw * 57.29578f;
			int right = yaw >= 0, behind = fabsf(yaw) > 135;
			g_hu.side[right] = 1, g_hu.side[!right] = behind ? 1 : 0.5f;
			from = behind ? "behind (both sides)" : right ? "the right" : "the left";
		}
		if (g_hu.on && stereo) {
			float a = 0.8f + 0.2f * fminf(1.0f, dmg / 30.0f);
			for (int h = 0; h < 2; h++) {
				in_haptic_event(h, a, 0.09f, SOMVR_HAP_COMBAT);
				in_haptic_ramp(h, 0.6f * a, 0.25f, SOMVR_HAP_COMBAT);
			}
			g_hu.flash_t = t, g_hu.flashes++;
		}
		logf_("FIGHT hurt t=%.3f: Talion's health %.1f -> %.1f (-%.1f, max seen %.1f); the blow from %s%s%.0f deg%s", t,
		      g_hu.hp, hp, dmg, g_hu.hp_max, from, isfinite(yaw) ? " at " : "", isfinite(yaw) ? yaw : 0.0f,
		      !g_hu.on ? " (SOMVR_HURT off: no pulse, no flash)" : stereo ? ": the pulse, the red flash" : " (a flat frame: none)");
		g_hu.last_t = t;
	}
	g_hu.hp = hp;
}

/* ---- SOMVR_COMBAT_DEATH (build 11: "a killed orc's body lands 3.7 m away") ------------------------------------------
 * Build 10's =stagger deals the kill with the light record: 'AI_Bip_R2L_Death_F' threw the body 3.68 m in 2.55 s (boot
 * 192239; the heavy record's 'AI_Bip_STR_Death_Hvy_F' 4.8 m). kd_pre, before our Combat_DealHit, when the blow will kill
 * (the health left <= the record's damage): =TYPE puts that recoil type into the record's RecoilTypeHit for the call
 * (the death reaction is picked from it inside DealHit, as a stagger's is), SOMVR_COMBAT_DEATH_DIR scales the hit
 * direction the call reads (Talion's C_HITDIR). SOMVR_COMBAT_DEATH_PIN_CM (a test only) writes the body's root back to
 * that distance (kb_frame). Lines: `COMBAT death ...` per kill (the throw's profile every 0.25 s), `CONFIG death ...`.
 * The throw is the death reaction's own move (a slide: ~1 m in the first 0.25 s, settled by 1.25 s), picked by the
 * type's direction: boots 003132/004917 (=stagger's light kill): the game's 'R2L_Death_F' 3.27 m, Light_UpToDown
 * 'U2D_Death_F' 2.78 m, Light_DownToUp 'D2U_Death_F' 3.34 m, Light_Straight 'STR_Death_F' 4.29 m; the direction x0.2 changed
 * nothing (4.64 m); the pin's 73 writes never held (the game puts the root back); a type no death reaction answers
 * ('Block_Light': no death attack, the body falls where it stands, 0.18 m; 'Ambient_Converse_Recoil' 0.82 m) */
enum { KD_GAME = 0, KD_TYPE = 1 };
static void kd_set_type(const char *v)
{
	if (!v || !*v || !strcmp(v, "game"))
		g_kd.mode = KD_GAME, g_kd.type[0] = 0;
	else
		g_kd.mode = KD_TYPE, snprintf(g_kd.type, sizeof g_kd.type, "%s", v);
}
static void kd_config(int *bad)
{
	kd_set_type(getenv("SOMVR_COMBAT_DEATH"));
	g_kd.dir = env_float("SOMVR_COMBAT_DEATH_DIR", 1, 0, 1, bad);
	g_kd.pin_cm = env_float("SOMVR_COMBAT_DEATH_PIN_CM", 0, 0, 1000, bad);
	logf_("CONFIG death=%s dir=%.2f pin=%.0f cm: a killing blow of ours %s%s%s", g_kd.mode ? g_kd.type : "game", g_kd.dir,
	      g_kd.pin_cm, g_kd.mode ? "gets that RecoilTypeHit for the call (the death reaction is picked from it)"
	      : "keeps the record's RecoilTypeHit", g_kd.dir < 1 ? ", its hit direction scaled" : "",
	      g_kd.pin_cm > 0 ? ", the body written back to the pin distance (a test)" : "");
}
/* cb_hit, before Combat_DealHit (rec = the record of the call; light = =stagger's light record): a blow that kills */
static void kd_pre(uintptr_t c, uintptr_t pc, uintptr_t rec, int tier, int light, uintptr_t *slot, uint64_t *was, float dir[3])
{
	*slot = 0, *was = 0;
	g_kd.note[0] = 0;
	g_kd.pre_c = c, g_kd.pre_dead = cb_dead(c);
	float hp = cb_health(c), dmg = g_kx.light_dmg * (light || tier == SC_TIER_LIGHT ? 1.0f : tier == SC_TIER_HEAVY ? 4.0f : 2.0f);
	if (g_kd.pre_dead || !isfinite(hp) || hp - dmg > 0.5f || (g_kd.mode == KD_GAME && g_kd.dir >= 1))
		return;
	size_t o = 0;
	if (g_kd.mode == KD_TYPE) {
		uintptr_t ad = rec && g_df.ok_ad ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(rec) : 0;
		uintptr_t sl = ad ? kx_slot(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0, r = kx_find(g_kd.type);
		uint64_t v = (uint64_t)r;
		if (sl && r && peek(sl, was, 8) && (*was == v || arm_poke(sl, &v, 8))) {
			*slot = *was == v ? 0 : sl, g_kd.typed++;
			o += (size_t)snprintf(g_kd.note + o, sizeof g_kd.note - o, "RecoilTypeHit '%s'", g_kd.type);
		} else {
			g_kd.missing++;
			o += (size_t)snprintf(g_kd.note + o, sizeof g_kd.note - o, "no '%s' (%s)", g_kd.type,
					      !sl ? "no RecoilTypeHit slot" : !r ? "not a known recoil type" : "not written");
		}
	}
	if (g_kd.dir < 1) {
		float d[3] = { dir[0] * g_kd.dir, dir[1] * g_kd.dir, dir[2] * g_kd.dir };
		memcpy((void *)(pc + C_HITDIR), d, sizeof d); /* (put back after the call with the rest of cb_hit's) */
		g_kd.scaled++;
		snprintf(g_kd.note + o, sizeof g_kd.note - o, "%sthe hit direction x%.2f", o ? ", " : "", g_kd.dir);
	}
}

/* ---- SOMVR_HANDS_TEST: the test guard (`fight guard off|learn|path|miss`; build 11's open check 1) --------------------
 * A player's block for headless boots, from what a player sees at the telegraph (the attack started, the orc's place and
 * facing), never the live weapon: each attack record's path is learnt from its first strikes (its weapon's segment at
 * the impact -0.15/-0.10/-0.05 s, in the frame of the orc's pose at the reaction time, impact - TG_LEAD_S; only strikes
 * that met no blade); at the reaction time of the next one of a learnt record the right test hand swings (TG_SWING_MS)
 * to put the blade's middle on the predicted middle of the blow at -0.10 s, across the swing's plane (path), or the same
 * guard moved off the path (miss: mirrored or shifted, at least 35 cm from the whole predicted blow); +0.25 s after the
 * impact (or when the attack ends) back to the pose it came from. The blade in the grip is measured from the still test
 * poses. Lines: `TGUARD ...`, `TGUARD END`. */
#define TG_LEAD_S 0.6f  /* the reaction: 0.2 s into an infantry's 0.8 s wind-up (boot 1's 0.45 s / 150 ms swing hit the orc) */
#define TG_SWING_MS 300
#define TG_N 24
enum { TG_OFF = 0, TG_LEARN = 1, TG_PATH = 2, TG_MISS = 3 };
static const char *const g_tg_mode_w[] = { "off", "learn", "path", "miss" };
static const float g_tg_at[3] = { -0.15f, -0.10f, -0.05f };
struct tg_learn { uintptr_t atk; int n; float b[3][3], p[3][3]; };
static struct {
	int mode;
	int head_frame;            /* "fight guard frame head": learnt and predicted from the head, turned toward the orc */
	struct tg_learn l[TG_N];
	int nl;
	int have_blade, still;     /* the blade in the right grip (OpenXR grip axes, cm), measured; still frames */
	float bl_b[3], bl_t[3], last_v[6];
	uintptr_t c, atk;          /* the attack followed (one at a time) */
	uint64_t serial;
	float impact, ts, o_pos[3], o_yaw, sb[3][3], sp[3][3], rest[6], tgt[3], pose[6], lam;
	int frozen, samples, placed, checked, learnt_n;
	uint64_t contacts0, calls0;
	double seen_t;
	char an[96];
	uint64_t guards[4], blocks[4], ourhit[4], unknown, learnt, checks;
	float off_sum, pred_sum;
} g_tg;

static void tg_to(const float p[3], const float org[3], float yaw, float o[3]) /* world -> right, up, ahead (cm) */
{
	float c = cosf(yaw), sn = sinf(yaw), dx = p[0] - org[0], dz = p[2] - org[2];
	o[0] = dx * c - dz * sn, o[1] = p[1] - org[1], o[2] = dx * sn + dz * c;
}
static void tg_from(const float l[3], const float org[3], float yaw, float o[3])
{
	float c = cosf(yaw), sn = sinf(yaw);
	o[0] = org[0] + l[0] * c + l[2] * sn, o[1] = org[1] + l[1], o[2] = org[2] - l[0] * sn + l[2] * c;
}
/* R = Rx(a) Ry(b) Rz(c) (degrees; sa_xf_euler's order) */
static void tg_euler_m(const float *e, float m[3][3])
{
	float a = e[0] * 0.017453293f, b = e[1] * 0.017453293f, c = e[2] * 0.017453293f;
	float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b), cc = cosf(c), sc = sinf(c);
	m[0][0] = cb * cc, m[0][1] = -cb * sc, m[0][2] = sb;
	m[1][0] = ca * sc + sa * sb * cc, m[1][1] = ca * cc - sa * sb * sc, m[1][2] = -sa * cb;
	m[2][0] = sa * sc - ca * sb * cc, m[2][1] = sa * cc + ca * sb * sc, m[2][2] = ca * cb;
}
static void tg_mv(float m[3][3], const float v[3], float o[3])
{
	for (int i = 0; i < 3; i++)
		o[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
}
static float tg_norm(float v[3])
{
	float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1e-6f)
		v[0] /= l, v[1] /= l, v[2] /= l;
	return l;
}
static void tg_cross(const float a[3], const float b[3], float o[3])
{
	o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
}
/* the closest approach of two segments (sampled, n steps each; one check per attack) */
static float tg_segdist_n(const float a0[3], const float a1[3], const float b0[3], const float b1[3], int n)
{
	float best = 1e9f;
	for (int i = 0; i <= n; i++)
		for (int j = 0; j <= n; j++) {
			float u = (float)i / (float)n, v = (float)j / (float)n, d[3];
			for (int k = 0; k < 3; k++)
				d[k] = a0[k] + (a1[k] - a0[k]) * u - b0[k] - (b1[k] - b0[k]) * v;
			best = fminf(best, sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
		}
	return best;
}
static float tg_segdist(const float a0[3], const float a1[3], const float b0[3], const float b1[3])
{
	return tg_segdist_n(a0, a1, b0, b1, 20);
}
/* the miss guard: how near our blade (its point lam at P along N) comes to the predicted blow, its three segments and
 * the frames between them, one step before and after (-0.20 .. 0 s) */
static float tg_path_dist(float w[3][2][3], const float P[3], const float N[3], float lam)
{
	float L = 0, b0[3], b1[3], best = 1e9f;
	for (int i = 0; i < 3; i++)
		L += (g_tg.bl_t[i] - g_tg.bl_b[i]) * (g_tg.bl_t[i] - g_tg.bl_b[i]);
	L = sqrtf(L);
	for (int i = 0; i < 3; i++)
		b0[i] = P[i] - lam * L * N[i], b1[i] = P[i] + (1 - lam) * L * N[i];
	for (int s = -4; s <= 12; s++) { /* s / 4 steps of 0.05 s from -0.15 s (-0.20 .. 0) */
		float f = (float)s / 4.0f, sb[3], st[3];
		int k = f < 1 ? 0 : 1;
		float u = f - (float)k;
		for (int i = 0; i < 3; i++)
			sb[i] = w[k][0][i] + (w[k + 1][0][i] - w[k][0][i]) * u, st[i] = w[k][1][i] + (w[k + 1][1][i] - w[k][1][i]) * u;
		best = fminf(best, tg_segdist_n(b0, b1, sb, st, 8));
	}
	return best;
}
/* our blade now in the head frame (0 = none) */
static int tg_blade_h(double t, float hb[3], float ht[3], float head[3], float *yaw)
{
	const somvr_blade *b = &g_cb.prev;
	if (!b->valid || fabs(b->t - t) > 0.1)
		return 0;
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	head[0] = hp.x, head[1] = hp.y, head[2] = hp.z, *yaw = sv_heading(hq);
	tg_to(b->base, head, *yaw, hb), tg_to(b->tip, head, *yaw, ht);
	return 1;
}
/* every frame: the blade in the right grip while its test pose is still (3 frames, no swing) */
static void tg_calib(double t)
{
	if (!g_bd.test || !g_bd.t_on[1] || g_bd_sw.on[1]) {
		g_tg.still = 0;
		return;
	}
	if (memcmp(g_tg.last_v, g_bd.t_v[1], sizeof g_tg.last_v))
		memcpy(g_tg.last_v, g_bd.t_v[1], sizeof g_tg.last_v), g_tg.still = 0;
	float hb[3], ht[3], head[3], yaw;
	if (++g_tg.still < 3 || !tg_blade_h(t, hb, ht, head, &yaw))
		return;
	const float *v = g_bd.t_v[1];
	float m[3][3], g[3] = { v[0], v[1], -v[2] };
	tg_euler_m(v + 3, m);
	for (int q = 0; q < 2; q++) { /* local = R^T (p - g), p in OpenXR axes (ahead = -Z) */
		const float *h = q ? ht : hb;
		float d[3] = { h[0] - g[0], h[1] - g[1], -h[2] - g[2] }, *o = q ? g_tg.bl_t : g_tg.bl_b;
		for (int i = 0; i < 3; i++)
			o[i] = m[0][i] * d[0] + m[1][i] * d[1] + m[2][i] * d[2];
	}
	if (!g_tg.have_blade)
		logf_("TGUARD t=%.3f the blade in the right grip: base %.1f,%.1f,%.1f tip %.1f,%.1f,%.1f cm (OpenXR grip axes; from the "
		      "test pose %.0f %.0f %.0f %.0f %.0f %.0f)", t, g_tg.bl_b[0], g_tg.bl_b[1], g_tg.bl_b[2], g_tg.bl_t[0], g_tg.bl_t[1],
		      g_tg.bl_t[2], v[0], v[1], v[2], v[3], v[4], v[5]);
	g_tg.have_blade = 1;
}
static void tg_swing(const float v[6], int ms)
{
	char w[160];
	snprintf(w, sizeof w, "swing R %.1f %.1f %.1f %.1f %.1f %.1f %d", v[0], v[1], v[2], v[3], v[4], v[5], ms);
	bd_word(w);
}
/* the grip pose that puts the blade's point lam (0 base .. 1 tip) at M (head frame) along D (unit, head frame); its
 * distance from the right shoulder (18 right, 25 down) */
static float tg_pose(const float M[3], const float D[3], float lam, float out[6])
{
	float mid[3], u[3], R0[3][3], d0[3], Dx[3] = { D[0], D[1], -D[2] }, Mx[3] = { M[0], M[1], -M[2] };
	for (int i = 0; i < 3; i++)
		mid[i] = g_tg.bl_b[i] + lam * (g_tg.bl_t[i] - g_tg.bl_b[i]), u[i] = g_tg.bl_t[i] - g_tg.bl_b[i];
	tg_norm(u);
	tg_euler_m(g_tg.rest + 3, R0);
	tg_mv(R0, u, d0);
	float k[3], c = d0[0] * Dx[0] + d0[1] * Dx[1] + d0[2] * Dx[2];
	tg_cross(d0, Dx, k);
	float s = tg_norm(k);
	if (s < 1e-4f) { /* (parallel: any axis across) */
		float up[3] = { 0, 1, 0 };
		tg_cross(d0, up, k);
		if (tg_norm(k) < 1e-4f)
			k[0] = 1, k[1] = k[2] = 0;
		s = 0;
	}
	float ang = atan2f(s, c), ca = cosf(ang), sa = sinf(ang), Q[3][3], R[3][3];
	float K[3][3] = { { 0, -k[2], k[1] }, { k[2], 0, -k[0] }, { -k[1], k[0], 0 } };
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			Q[i][j] = (i == j ? ca : 0) + (1 - ca) * k[i] * k[j] + sa * K[i][j];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			R[i][j] = Q[i][0] * R0[0][j] + Q[i][1] * R0[1][j] + Q[i][2] * R0[2][j];
	float rm[3];
	tg_mv(R, mid, rm);
	float g[3] = { Mx[0] - rm[0], Mx[1] - rm[1], Mx[2] - rm[2] };
	float b = asinf(fmaxf(-1, fminf(1, R[0][2]))), a = atan2f(-R[1][2], R[2][2]), cz = atan2f(-R[0][1], R[0][0]);
	out[0] = g[0], out[1] = g[1], out[2] = -g[2];
	out[3] = a * 57.29578f, out[4] = b * 57.29578f, out[5] = cz * 57.29578f;
	return sqrtf((out[0] - 18) * (out[0] - 18) + (out[1] + 25) * (out[1] + 25) + out[2] * out[2]);
}
static struct tg_learn *tg_find(uintptr_t atk)
{
	for (int i = 0; i < g_tg.nl; i++)
		if (g_tg.l[i].atk == atk)
			return &g_tg.l[i];
	return NULL;
}
/* the followed attack ends: the learnt path (no blade met it), the hand back, the outcome */
static void tg_finish(double t, const char *why)
{
	int met = g_df.n.contacts > g_tg.contacts0;
	if (g_tg.samples == 7 && !met && !g_tg.placed) {
		struct tg_learn *l = tg_find(g_tg.atk);
		if (!l && g_tg.nl < TG_N)
			l = &g_tg.l[g_tg.nl++], memset(l, 0, sizeof *l), l->atk = g_tg.atk;
		if (l) {
			float w = 1.0f / (float)(l->n < 3 ? l->n + 1 : 4); /* the mean of the first ones, then a moving one */
			for (int k = 0; k < 3; k++)
				for (int i = 0; i < 3; i++)
					l->b[k][i] += (g_tg.sb[k][i] - l->b[k][i]) * w, l->p[k][i] += (g_tg.sp[k][i] - l->p[k][i]) * w;
			l->n++, g_tg.learnt++;
			logf_("TGUARD t=%.3f learnt '%s' (%d strikes): its blow's middle at -0.10 s %.0f,%.0f,%.0f cm (right, up, ahead "
			      "of the orc at the reaction time)", t, g_tg.an, l->n, 0.5f * (l->b[1][0] + l->p[1][0]),
			      0.5f * (l->b[1][1] + l->p[1][1]), 0.5f * (l->b[1][2] + l->p[1][2]));
		}
	}
	if (g_tg.placed) {
		int m = g_tg.mode, hit = g_cb.n.calls > g_tg.calls0; /* the guard's move dealt a hit first: not a block's case */
		g_tg.blocks[m] += met && !hit, g_tg.ourhit[m] += hit;
		logf_("TGUARD t=%.3f %s guard for '%s': %s (%s)", t, g_tg_mode_w[m], g_tg.an, hit ? "our blade hit the orc first: "
		      "not judged" : met ? "the blades met: blocked" : "no contact: not blocked", why);
		tg_swing(g_tg.rest, 250);
	}
	g_tg.c = 0, g_tg.atk = 0, g_tg.placed = 0, g_tg.frozen = 0, g_tg.samples = 0, g_tg.checked = 0;
}
/* df_frame's tracks (the game thread, outside g_df_mu): the reaction time, the samples, the check at -0.10 s */
static void tg_track(struct df_track *d, const somvr_df_in *in, sv_quat hq, double t)
{
	if (!g_tg.mode || !g_bd.test || !(in->impact > 0) || in->norc < 1)
		return;
	if (g_tg.c && (g_tg.c != d->c || g_tg.atk != d->attack || g_tg.serial != d->serial)) {
		if (g_tg.c == d->c) /* this orc's next attack: the last one is over */
			tg_finish(t, "its next attack");
		else
			return;
	}
	if (!g_tg.c) {
		if (in->ts >= in->impact - TG_LEAD_S) /* (begun before the reaction time: not this one) */
			return;
		g_tg.c = d->c, g_tg.atk = d->attack, g_tg.serial = d->serial, g_tg.contacts0 = g_df.n.contacts, g_tg.calls0 = g_cb.n.calls;
		rx_rec_name(d->attack, g_tg.an, sizeof g_tg.an);
	}
	g_tg.seen_t = t, g_tg.impact = in->impact, g_tg.ts = in->ts;
	float oq[4] = { 0, 0, 0, 1 };
	if (!g_tg.frozen && in->ts >= in->impact - TG_LEAD_S) {
		if (!peek(d->hobj + OBJ_ROT, oq, sizeof oq) || !vec_ok(oq, 2.0f))
			return;
		memcpy(g_tg.o_pos, in->orc, sizeof g_tg.o_pos), g_tg.o_yaw = sv_heading(sv_qnormalize(sv_quat_of(oq)));
		if (g_tg.head_frame) { /* the head, its heading toward the orc: the blow's path as the player sees it come */
			sv_vec3 fhp;
			sv_quat fhq;
			somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &fhp, &fhq);
			float hd[3] = { fhp.x, fhp.y, fhp.z };
			if (vec_ok(hd, 1e7f) && hypotf(in->orc[0] - hd[0], in->orc[2] - hd[2]) > 1.0f)
				g_tg.o_yaw = atan2f(in->orc[0] - hd[0], in->orc[2] - hd[2]), memcpy(g_tg.o_pos, hd, sizeof hd);
		}
		g_tg.frozen = 1;
		struct tg_learn *l = tg_find(d->attack);
		if (g_tg.mode >= TG_PATH && l && g_tg.have_blade && g_bd.t_on[1]) {
			sv_vec3 hp;
			sv_quat hq2;
			somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq2);
			float head[3] = { hp.x, hp.y, hp.z }, yaw = sv_heading(hq), w[3][2][3];
			for (int k = 0; k < 3; k++) { /* the learnt segments: the orc's frame now -> world -> the head's */
				float pw[3];
				tg_from(l->b[k], g_tg.o_pos, g_tg.o_yaw, pw), tg_to(pw, head, yaw, w[k][0]);
				tg_from(l->p[k], g_tg.o_pos, g_tg.o_yaw, pw), tg_to(pw, head, yaw, w[k][1]);
			}
			/* the place within the arm's reach: a point of the blow (at -0.15/-0.10/-0.05 s, along its weapon) met by a
			 * point of our blade, across the swing's plane, the grip nearest the shoulder (build 11 boot 1: the blow's
			 * middle alone put the grip up to 135 cm out) */
			float V[3], best = 1e9f, M[3] = { 0, 0, 0 }, D[3] = { 0, 0, 1 }, pz[6], lam = 0.5f;
			for (int i = 0; i < 3; i++)
				V[i] = 0.5f * (w[2][0][i] + w[2][1][i] - w[0][0][i] - w[0][1][i]);
			memcpy(g_tg.rest, g_bd.t_v[1], sizeof g_tg.rest);
			float pathd = 0;
			for (int k = 0; k < 3; k++)
				for (int ui = 1; ui <= 5; ui++)
					for (int li = 0; li < 5; li++)
						for (int sg = 0; sg < 2; sg++)
							for (int sh = 0; sh < (g_tg.mode == TG_MISS ? 5 : 1); sh++) {
								float W[3], N[3], P[3], u = 0.2f * (float)ui, lm = 0.3f + 0.15f * (float)li, pp[6], pd = 0;
								for (int i = 0; i < 3; i++)
									W[i] = w[k][1][i] - w[k][0][i], P[i] = w[k][0][i] + u * W[i];
								tg_norm(W);
								tg_cross(W, V, N);
								if (tg_norm(N) < 1e-3f) {
									float up[3] = { 0, 1, 0 };
									tg_cross(W, up, N), tg_norm(N);
								}
								if (g_tg.mode == TG_MISS) { /* moved off the path: mirrored, 60 cm to a side, 50 up, 60
											     * down; kept only 35 cm or more from the whole predicted
											     * blow (boots 2-3: a mirror of a diagonal chop's point met it) */
									static const float shv[5][3] = { { 0, 0, 0 }, { 60, 0, 0 }, { -60, 0, 0 },
													 { 0, 50, 0 }, { 0, -60, 0 } };
									if (!sh)
										P[0] = -P[0], N[0] = -N[0];
									for (int i = 0; i < 3; i++)
										P[i] += shv[sh][i];
								}
								if (sg)
									N[0] = -N[0], N[1] = -N[1], N[2] = -N[2];
								float r = tg_pose(P, N, lm, pp) + (k != 1) * 2.0f + fabsf(u - 0.6f) * 4.0f;
								if (g_tg.mode == TG_MISS && r < best) {
									pd = tg_path_dist(w, P, N, lm);
									r += pd < 35 ? 1000 : 0;
								}
								if (r < best)
									best = r, memcpy(M, P, sizeof M), memcpy(D, N, sizeof D), memcpy(pz, pp, sizeof pz), lam = lm,
									pathd = pd;
							}
			float ra = tg_pose(M, D, lam, g_tg.pose);
			(void)pz;
			memcpy(g_tg.tgt, M, sizeof M), g_tg.lam = lam;
			tg_swing(g_tg.pose, TG_SWING_MS);
			g_tg.placed = 1, g_tg.guards[g_tg.mode]++;
			logf_("TGUARD t=%.3f %s guard for '%s' (learnt from %d) at %.3f s (impact %.3f s): a point of the blow "
			      "predicted at %.0f,%.0f,%.0f cm (right, up, ahead of the head), the blade (at %.0f %% of it) across it; the grip "
			      "%.0f %.0f %.0f %.0f %.0f %.0f (%.0f cm from the right shoulder%s) in %d ms%s", t, g_tg_mode_w[g_tg.mode], g_tg.an,
			      l->n, in->ts, in->impact, M[0], M[1], M[2], lam * 100, g_tg.pose[0], g_tg.pose[1], g_tg.pose[2], g_tg.pose[3],
			      g_tg.pose[4], g_tg.pose[5], ra, ra > 70 ? ": beyond an arm's reach" : "", TG_SWING_MS,
			      g_tg.mode == TG_MISS ? pathd >= 35 ? "; the miss guard off the predicted blow" : "; the miss guard: no place "
			      "35 cm off the predicted blow" : "");
			if (g_tg.mode == TG_MISS)
				logf_("TGUARD t=%.3f the miss guard %.0f cm from the predicted blow (-0.20 .. 0 s)", t, pathd);
		} else if (g_tg.mode >= TG_PATH) {
			g_tg.unknown++;
			logf_("TGUARD t=%.3f no guard for '%s' (%s): learning it", t, g_tg.an, !l ? "not learnt yet"
			      : !g_tg.have_blade ? "the blade in the grip not measured yet" : "no right test pose");
		}
	}
	for (int k = 0; k < 3 && g_tg.frozen; k++) /* the samples, in the frame of the orc at the reaction time */
		if (!(g_tg.samples & 1 << k) && in->ts >= in->impact + g_tg_at[k]) {
			tg_to(in->ob[0], g_tg.o_pos, g_tg.o_yaw, g_tg.sb[k]), tg_to(in->ot[0], g_tg.o_pos, g_tg.o_yaw, g_tg.sp[k]);
			g_tg.samples |= 1 << k;
			if (k == 1 && g_tg.placed && !g_tg.checked) { /* where our blade and its weapon are now */
				float hb[3], ht[3], head[3], yaw, wb[3], wt[3];
				g_tg.checked = 1;
				if (tg_blade_h(t, hb, ht, head, &yaw)) {
					tg_to(in->ob[0], head, yaw, wb), tg_to(in->ot[0], head, yaw, wt);
					float om = 0, pm = 1e9f, bp[3];
					for (int i = 0; i < 3; i++)
						bp[i] = hb[i] + g_tg.lam * (ht[i] - hb[i]), om += powf(bp[i] - g_tg.tgt[i], 2);
					for (int j = 0; j <= 20; j++) { /* its weapon's nearest point to the target */
						float e = 0;
						for (int i = 0; i < 3; i++)
							e += powf(wb[i] + (wt[i] - wb[i]) * (float)j / 20.0f - g_tg.tgt[i], 2);
						pm = fminf(pm, e);
					}
					g_tg.checks++, g_tg.off_sum += sqrtf(om), g_tg.pred_sum += sqrtf(pm);
					logf_("TGUARD t=%.3f check at -0.10 s ('%s'): our blade's point %.0f,%.0f,%.0f (%.1f cm off the target), its "
					      "weapon %.1f cm from the target%s, the blades %.1f cm apart now", t, g_tg.an, bp[0], bp[1], bp[2],
					      sqrtf(om), sqrtf(pm), g_tg.mode == TG_MISS ? " (the miss guard's target is the other side's)" : "",
					      tg_segdist(hb, ht, wb, wt));
				}
			}
		}
	if (in->ts >= in->impact + 0.25f)
		tg_finish(t, "after its impact");
}
/* every frame: the blade's measure; an attack no longer seen ends */
static void tg_frame(double t)
{
	if (!g_tg.mode)
		return;
	if (!g_tg.placed)
		tg_calib(t);
	if (g_tg.c && t - g_tg.seen_t > 0.15)
		tg_finish(t, "its attack ended");
}
/* an attack's StrikeRange (AttackData field 9: a range, the slot points at {min, max}) pointed at our own pair (the
 * game's pair may be shared by other records); 1 = written */
static float g_fr_rng[16][2];
static uintptr_t g_ft_pl; /* fight_frame's player (the test words) */
static int g_fr_rng_n;
static int fr_range_set(const char *name, float lo, float hi, const char *why)
{
	uintptr_t rec = g_cb.ok_hit && g_cb.ok_ad ? cb_rec_valid(CB_FN(CS_ATK_BYNAME, cb_byname_fn)(name)) : 0;
	uintptr_t ad = rec ? CB_FN(CS_ATTACKDATA, cb_attackdata_fn)(rec) : 0, vals = ad ? peek_ptr(ad + 0x10) : 0;
	unsigned char e[16];
	uint16_t cnt = 0, vi = 0;
	float old[2] = { NAN, NAN };
	uintptr_t slot = 0, p0 = 0;
	if (ad && vals && g_rxg.sch_ad && !rx_db_entry(ad, rx_sch_hash(g_rxg.sch_ad, RX_AD_STRIKERANGE), e)) {
		memcpy(&cnt, e + 0xc, 2), memcpy(&vi, e + 0xe, 2);
		slot = (e[0xa] & 0x3f) == 7 && cnt == 1 ? vals + (uintptr_t)vi * 8 : 0;
		p0 = slot ? peek_ptr(slot) : 0;
	}
	if (!p0 || !peek(p0, old, sizeof old) || g_fr_rng_n >= 16) {
		logf_("FIGHT range '%s' %.0f..%.0f (%s): not set (%s)", name, lo, hi, why, !rec ? "no such attack record"
		      : !ad ? "no AttackData" : !slot ? "no StrikeRange range in it" : g_fr_rng_n >= 16 ? "16 set already" : "unreadable");
		return 0;
	}
	float *pr = g_fr_rng[g_fr_rng_n++];
	pr[0] = lo, pr[1] = hi;
	uintptr_t np = (uintptr_t)pr;
	int ok = arm_poke(slot, &np, sizeof np);
	logf_("FIGHT range '%s' StrikeRange %.0f..%.0f -> %.0f..%.0f cm (%s): %s", name, old[0], old[1], lo, hi, why,
	      ok ? "its slot points at ours" : "the slot write failed");
	return ok;
}

/* the trigger word "fight guard off|learn|path|miss", "fight death game|TYPE", "fight death dir S", "fight death pin CM",
 * "fight rec TIER NAMES", "fight stagger CM", "fight body ..." */
static int rl_word(float cm); /* "fight stagger CM" (SOMVR_COMBAT_STAGGER_CM, below) */
static const char *nb_word(const char *w); /* "fight body ..." (SOMVR_FIGHT_BODY_CM, below) */
static const char *fight_word(const char *w)
{
	char a[16] = "", b[48] = "";
	float v = 0;
	if (sscanf(w, "%15s %47s", a, b) < 2)
		return "fight (not guard|death ...: ignored)";
	if (!strcmp(a, "dodge") && !strcmp(b, "auto")) { /* build 17b: "fight dodge auto MS": A MS after each red warning */
		int ms = -1;
		if (!g_bd.test)
			return "fight dodge auto: test only (SOMVR_HANDS_TEST)";
		if (sscanf(w, "%*s %*s %d", &ms) != 1 || ms < 0 || ms > 2000)
			return "fight dodge auto MS: MS 0..2000 (0 = off)";
		atomic_store(&g_dg_auto_ms, ms);
		return ms ? "fight dodge auto: A pressed that long after each red warning" : "fight dodge auto: off";
	}
	if (!strcmp(a, "dodge") && !strcmp(b, "probe")) { /* build 17: SOMVR_DODGE's rays in 8 directions (DODGE probe lines) */
		if (!g_bd.test)
			return "fight dodge probe: test only (SOMVR_HANDS_TEST)";
		atomic_store(&g_dg_probe_req, 1);
		return "fight dodge probe: the 8 directions at the next input tick (SOMVR_DODGE on)";
	}
	if (!strcmp(a, "bite") || !strcmp(a, "hitfx")) /* build 17: "fight bite on|off|hold ...|dumps N", "fight hitfx on|off" */
		return hf_word(w);
	if (!strcmp(a, "guard")) {
		if (!g_bd.test)
			return "fight guard (SOMVR_HANDS_TEST is off: ignored)";
		for (int m = 0; m < 4; m++)
			if (!strcmp(b, g_tg_mode_w[m])) {
				if (g_tg.c)
					tg_finish(cb_now(), "the mode changed");
				g_tg.mode = m;
				logf_("TGUARD mode %s (learnt %d attack records)", b, g_tg.nl);
				return "fight guard";
			}
		if (!strcmp(b, "frame")) { /* "fight guard frame orc|head" (build 16 round 3) */
			char f[16] = "";
			if (sscanf(w, "%*s %*s %15s", f) != 1 || (strcmp(f, "orc") && strcmp(f, "head")))
				return "fight guard frame (not orc|head: ignored)";
			g_tg.head_frame = !strcmp(f, "head"), g_tg.nl = 0;
			logf_("TGUARD frame %s: the blows learnt in the frame of %s at the reaction time (the learnt ones forgotten)", f,
			      g_tg.head_frame ? "the head, facing the orc (where the blow comes at the player from)" : "the orc's pose");
			return "fight guard frame";
		}
		return "fight guard (not off|learn|path|miss|frame orc|head: ignored)";
	}
	if (!strcmp(a, "death")) {
		if (!strcmp(b, "dir") && sscanf(w, "%*s %*s %f", &v) == 1 && v >= 0 && v <= 1)
			g_kd.dir = v;
		else if (!strcmp(b, "pin") && sscanf(w, "%*s %*s %f", &v) == 1 && v >= 0 && v <= 1000)
			g_kd.pin_cm = v;
		else if (strcmp(b, "dir") && strcmp(b, "pin"))
			kd_set_type(b);
		else
			return "fight death (dir 0..1, pin 0..1000: ignored)";
		logf_("CONFIG death=%s dir=%.2f pin=%.0f cm (the word)", g_kd.mode ? g_kd.type : "game", g_kd.dir, g_kd.pin_cm);
		return "fight death";
	}
	if (!strcmp(a, "body")) /* build 19b: "fight body CM|marks N CM|status" (SOMVR_FIGHT_BODY_CM) */
		return nb_word(w);
	if (!strcmp(a, "stagger")) { /* build 17's tests: "fight stagger CM" (SOMVR_COMBAT_STAGGER_CM's cap; 0 = measured only) */
		v = (float)atof(b);
		return v >= 0 && v <= 300 && rl_word(v) ? "fight stagger"
			: "fight stagger (not CM in 0..300, or the hook is not in: SOMVR_COMBAT_STAGGER_CM unset: ignored)";
	}
	if (!strcmp(a, "range")) { /* build 16's tests: "fight range ATTACK MIN MAX" (its StrikeRange; headless) */
		float lo = 0, hi = 0;
		if (!g_bd.test || sscanf(w, "%*s %*s %f %f", &lo, &hi) != 2 || !(lo >= 0 && hi >= lo && hi <= 1000))
			return "fight range (SOMVR_HANDS_TEST off, or not ATTACK MIN MAX in 0..1000 cm: ignored)";
		return fr_range_set(b, lo, hi, "the word") ? "fight range" : "fight range (not set: see the FIGHT range line)";
	}
	if (!strcmp(a, "health")) { /* build 16's tests: "fight health N": the nearest orc at Talion (within 8 m) gets N health */
		float hv = (float)atof(b);
		uintptr_t pm = 0, obj = 0, pl = g_ft_pl;
		float root[3];
		struct cb_orc o[CB_ORCS];
		int m = 0;
		if (g_bd.test && hv > 0 && hv < 10000 && pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, root, sizeof root)) {
			uintptr_t pcomb = peek_ptr(pl + P_COMBAT), pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
			if (pc && vptr_of((void *)pc) != g_cbg.vt_combat)
				pc = 0;
			m = pcomb ? cb_orcs_r(pcomb, pc, root, 800.0f, o, CB_ORCS) : 0;
		}
		uintptr_t own = m ? peek_ptr(o[0].c + C_OWNER) : 0;
		float h0 = m ? cb_health(o[0].c) : NAN;
		if (!own || !isfinite(h0) || cb_dead(o[0].c) || !arm_poke(own + CH_HEALTH, &hv, sizeof hv))
			return "fight health (SOMVR_HANDS_TEST off, no live orc within 8 m, or not N in 0..10000: ignored)";
		logf_("FIGHT health orc=%#lx %.1f -> %.1f (the word; %.0f cm from Talion)", (unsigned long)o[0].c, h0, hv, o[0].dist);
		return "fight health";
	}
	if (!strcmp(a, "shield") || !strcmp(a, "class")) { /* build 16's tests: "fight shield game|glance", "fight class speed|cut" */
		int sh = !strcmp(a, "shield"), on = !strcmp(b, sh ? "glance" : "cut") ? 1 : !strcmp(b, sh ? "game" : "speed") ? 0
			: sh && !strcmp(b, "physical") ? 2 : -1;
		if (!g_bd.test || on < 0)
			return "fight shield|class (SOMVR_HANDS_TEST off, or not game|glance|physical / speed|cut: ignored)";
		*(sh ? &g_cb.shield : &g_cb.klass) = on;
		logf_("CONFIG combat %s=%s (the word)", sh ? "shield" : "class", b);
		return sh ? "fight shield" : "fight class";
	}
	if (!strcmp(a, "rec")) { /* build 16's tests: "fight rec light|heavy|stab|medium NAME[,NAME...]|default" (headless) */
		int k = !strcmp(b, "light") ? SC_TIER_LIGHT : !strcmp(b, "heavy") ? SC_TIER_HEAVY : !strcmp(b, "stab") ? SC_TIER_STAB
			: !strcmp(b, "medium") ? CB_MED : 0;
		const char *l = k ? strstr(w, b) + strlen(b) : NULL;
		while (l && *l == ' ')
			l++;
		if (!g_bd.test || !k || !l || !*l)
			return "fight rec (SOMVR_HANDS_TEST off, or not light|heavy|stab|medium NAMES|default: ignored)";
		cb_names_from(k, !strcmp(l, "default") ? g_cb_rec_dflt[k] : l);
		logf_("CONFIG combat %s records (the word): %d, the first '%s'", b, g_cb.nrec[k], g_cb.nrec[k] ? g_cb.rec[k][0].name : "");
		return "fight rec";
	}
	return "fight (not guard|death|rec ...: ignored)";
}

static uint64_t hu_hurts(void)
{
	return g_hu.hurts;
}

/* H4, after df_frame (every frame: flat ones too) */
/* ---- the shield's place (build 16 round 2): the shield item's model object, against a cut ---------------------- */
static int cb_shield_where(uintptr_t c, float pos[3], float rot[4])
{
	uintptr_t sh = 0, item = cb_shield_item(c, &sh);
	uintptr_t b = item ? peek_ptr(item + SL_IT_ENTRIES) : 0, e = item ? peek_ptr(item + SL_IT_ENTRIES + 8) : 0;
	if (!b || !(e > b) || (e - b) % SL_IT_ESIZE)
		return 0;
	uintptr_t obj = peek_ptr(b + SL_IE_OBJ);
	return obj && peek(obj + OBJ_POS, pos, 12) && peek(obj + OBJ_ROT, rot, 16) && vec_ok(pos, 1e7f) && vec_ok(rot, 2.0f);
}
/* v (world, cm) in the orc's frame: right, up, ahead */
static void cb_orc_frame(const struct cb_orc *o, const float v[3], float out[3])
{
	float orot[4] = { 0, 0, 0, 1 };
	peek(o->hobj + OBJ_ROT, orot, sizeof orot);
	float yaw = sv_heading(sv_qnormalize(sv_quat_of(orot))), c = cosf(yaw), s = sinf(yaw);
	out[0] = c * v[0] - s * v[2], out[1] = v[1], out[2] = s * v[0] + c * v[2];
}
/* "; the shield at R,U,A cm from its root (right, up, ahead), its axes ...; the contact R,U,A from the shield; the
 * blade D cm from the shield's centre" (the COMBAT hit line's tail) */
static void cb_shield_log(const struct cb_orc *o, const sc_contact *ct, const somvr_blade *b, double t, char *out, size_t cap)
{
	float sp[3], sq[4], d[3], f[3], g[3], cp[3];
	(void)t;
	out[0] = 0;
	if (!cb_shield_where(o->c, sp, sq))
		return;
	for (int i = 0; i < 3; i++)
		d[i] = sp[i] - o->pos[i], cp[i] = ct->point[i] - sp[i];
	cb_orc_frame(o, d, f), cb_orc_frame(o, cp, g);
	sv_quat q = sv_qnormalize(sv_quat_of(sq));
	sv_vec3 ax[3] = { sv_qrot(q, (sv_vec3){ 1, 0, 0 }), sv_qrot(q, (sv_vec3){ 0, 1, 0 }), sv_qrot(q, (sv_vec3){ 0, 0, 1 }) };
	float axo[3][3];
	for (int k = 0; k < 3; k++) {
		float w[3] = { ax[k].x, ax[k].y, ax[k].z };
		cb_orc_frame(o, w, axo[k]);
	}
	float s0, t0, bd = sc_seg_dist(b->base, b->tip, sp, sp, &s0, &t0);
	snprintf(out, cap, "; the shield at %+.0f,%+.0f,%+.0f cm from its root (right, up, ahead; +X %+.1f,%+.1f,%+.1f +Y "
		 "%+.1f,%+.1f,%+.1f +Z %+.1f,%+.1f,%+.1f), the contact %+.0f,%+.0f,%+.0f from it, the blade %.0f cm from it now",
		 f[0], f[1], f[2], axo[0][0], axo[0][1], axo[0][2], axo[1][0], axo[1][1], axo[1][2], axo[2][0], axo[2][1],
		 axo[2][2], g[0], g[1], g[2], bd);
}

/* ---- SOMVR_FIGHT_SPACE_CM (build 16): no orc walks into Talion ------------------------------------------------- *
 * The infantry's overhead chop 'AI_Infantry_Melee_Front_3_LL' walks the orc on after its blow to ~50 cm of Talion's
 * root (5 of 5, boot 001351; the flat game's heavy recoil would have pushed Talion away, ours keeps him still), and its
 * next strike 'AI_Infantry_Melee_Right' starts 43 cm off and passes through him (2 cm). The NPC movement's root motion
 * (CAIMovement::vf_11, the slot every character's movement shares; its combat's own warp writes the same transform)
 * is held: an orc whose attack targets Talion never moves nearer than the space to his root (horizontal); one already
 * nearer is not pushed, only stopped from coming nearer. The strikes' blows (136-154 cm at their impact) are not in it. */
/* SPACE_SITES_BEGIN (check_sites.py counts this block on its own: the patched K_VSLOT FS_AIMOVE, docs/re-xr-pipeline.md
 * 54; the slot: design-locomotion.md 2.8's LOCO_MOUNT_AIMOVE) */
static const struct site_spec g_fs_sites[] = {
SITE("FS_AIMOVE", K_VSLOT, 0x3ad3ef0, 0x3ad3ee0, "80 5e a7 02 00 00 00 00 80 8c a7 02 00 00 00 00 e0 81 a7 02 00 00 00 00", 0x2a781e0, "41 54 49 89 f4 55 53 48 89 fb 48 83 ec 20 c6"),
};
/* SPACE_SITES_END */
#define FS_MV_CHAR 0x218 /* CAIMovement -> its CCharacter (vf_11's Character_GetCombat argument) */
#define FS_MV_POS 0x8c   /* CAIMovement: the current transform's position (vf_11's copy before the step) */
#define FS_ORCS 16
#define RL_N 8
typedef void (*fs_move_fn)(void *mv, float *xf);
static struct {
	int on, ok;
	float space;          /* cm */
	uintptr_t orig;
	_Atomic uint32_t seq; /* the game thread's snapshot (a seqlock): Talion's root, the orcs at him */
	float root[3], head[3]; /* head: the player's eyes (the rig's head), from_head: SOMVR_FIGHT_SPACE_FROM=head */
	int have_head, from_head, have_root;
	uintptr_t hobj[FS_ORCS];
	int n;
	/* SOMVR_COMBAT_STAGGER_CM (build 17): the orcs in a reel (rl_*): their HOBJECT, anchor, g_rl.e index and gen */
	int rn, ri[RL_N];
	uintptr_t rh[RL_N];
	float ra[RL_N][3], rcap, rcapk[RL_N]; /* rcapk: a reel's own cap (build 19: SOMVR_PARRY_REEL_CM for a parry) */
	uint32_t rgen[RL_N];
	/* SOMVR_FIGHT_BODY_CM (build 19b): per orc at him the root's distance from the eyes its body needs (0 = none), the
	 * eyes; the hook's counters per slot (the game thread takes them each frame) */
	float need[FS_ORCS], need_a[FS_ORCS], eye[3]; /* need_a: the move's learnt lean, ahead of it (eased slower) */
	int have_eye;
	_Atomic uint32_t kcalls[FS_ORCS], ksteps[FS_ORCS], kpush_mm[FS_ORCS];
	double t;
	_Atomic uint64_t calls, near, held, other;
	_Atomic uintptr_t last_hobj;
	_Atomic uint32_t last_was, last_want, last_got; /* cm (the last hold, for the log) */
	uint64_t logged;
	double next_log;
} g_fs;

/* the hold: xf's new position (cm) kept at least min(space, the distance it had) from Talion's root */
static void fs_hold(uintptr_t mv, const float was[3], float *xf)
{
	atomic_fetch_add(&g_fs.calls, 1);
	uint32_t s0 = atomic_load(&g_fs.seq);
	if (s0 & 1)
		return;
	float root[3], head[3];
	uintptr_t hs[FS_ORCS];
	memcpy(root, g_fs.root, sizeof root), memcpy(head, g_fs.head, sizeof head);
	int n = g_fs.n, hh = g_fs.have_head;
	if (n > FS_ORCS || n < 0)
		return;
	memcpy(hs, g_fs.hobj, sizeof hs);
	if (atomic_load(&g_fs.seq) != s0 || !n)
		return;
	float sp = g_fs.space, dx = xf[0] - root[0], dz = xf[2] - root[2], ex = xf[0] - head[0], ez = xf[2] - head[2];
	if (!isfinite(dx) || !isfinite(dz) || ((fabsf(dx) >= sp || fabsf(dz) >= sp || dx * dx + dz * dz >= sp * sp) &&
	    (!hh || fabsf(ex) >= sp || fabsf(ez) >= sp || ex * ex + ez * ez >= sp * sp)))
		return; /* far from both */
	uintptr_t ch = *(uintptr_t *)(mv + FS_MV_CHAR), h = ch ? *(uintptr_t *)(ch + 8) : 0;
	int k = 0;
	while (k < n && hs[k] != h)
		k++;
	if (!h || k == n) {
		atomic_fetch_add(&g_fs.other, 1);
		return;
	}
	atomic_fetch_add(&g_fs.near, 1);
	float d0 = hypotf(was[0] - root[0], was[2] - root[2]), d = NAN, lim = NAN;
	if (hh)
		d0 = fminf(d0, hypotf(was[0] - head[0], was[2] - head[2]));
	if (!sc_space_hold2(root, hh && g_fs.from_head ? head : NULL, was, xf, sp, &d, &lim))
		return;
	atomic_fetch_add(&g_fs.held, 1);
	atomic_store(&g_fs.last_hobj, h);
	atomic_store(&g_fs.last_was, (uint32_t)d0), atomic_store(&g_fs.last_want, (uint32_t)d), atomic_store(&g_fs.last_got, (uint32_t)lim);
}

/* ---- SOMVR_FIGHT_BODY_CM (build 19b, docs/re-xr-pipeline.md 74): the orcs' bodies out of the player's face -------- *
 * FIGHT_SPACE keeps an attacker's root 87-89 cm from Talion, but a move leans the body far past the root (run 022755
 * frame 4806: a shoulder and head ~0.3 m from the eyes, half the view). Each stereo frame (the game thread, fs_frame)
 * the near orcs' bones are read (GetNodeTransform: Head, Neck, Spine4_joint and the two shoulders = the body; the hands
 * and the weapons measured only) and their nearest distance from the eyes kept per move (the attack record; none =
 * between blows): `FIGHT body near` per move under NB_LOG_CM, `FIGHT END body` (per move: how many, how many under 50
 * cm, the nearest). With the cm > 0, an orc at Talion whose body is nearer than that to the eyes has its root held
 * back (FS_AIMOVE's post-hook, after the reel and the space: sc_body_need here, sc_body_push in the hook - eased out at
 * most NB_PUSH_CMS, never nearer than its own step put it). Not the hands or the weapons: the blows still reach
 * Talion's body and the guard. Talion and the view never move. */
#define NB_TRK 8
#define NB_MOVES 96
#define NB_LOG_CM 70.0f   /* a move whose body came nearer than this gets its line */
#define NB_CLOSE_CM 50.0f /* "too close" in the END table */
#define NB_PUSH_CMS 600.0f /* the hook's ease out: 6 m/s (a step = this x the frame's time: 6.7 cm at 90 Hz, 30 at the
			    * null compositor's 20 Hz; run 061738: a berserker's Front_3 leans 38 cm in 2-3 frames) */
#define NB_ANTIC_CMS 150.0f /* ... toward a move's learnt lean, ahead of it: 1.5 m/s (a step back, not a slide) */
#define NB_ANTIC_S 0.45     /* ... from this long before the time into the move its lean began (the earliest seen) */
#define NB_MAX_CM 60.0f   /* the most a root is held back past where it stands */
#define NB_LEAD_CM 25.0f  /* the lead: the body's nearing in the last frame asked as a margin (at most this): the need
			   * is read from the last frame's pose, the lean grows meanwhile (run 055508 at 20 Hz: 42-49 cm
			   * under a 55 cm hold, a frame's lean) */
#define NB_LINES 300
enum { NB_HEAD, NB_NECK, NB_CHEST, NB_LSH, NB_RSH, NB_LH, NB_RH, NB_NODES };
#define NB_BODY 5 /* the first five nodes are the body (held); the hands and the weapons are measured only */
#define NB_H_NULL 0x88baa7e7u /* somhash("Null"): the skeleton's root node (nodes-2.tsv node 0) - the pose's own root */
static const uint32_t g_nb_hash[NB_NODES] = { 0x72598328u, 0x87ec5687u, 0x7af43530u, 0x338cf5b2u, 0x67d021bcu,
					       0x9b878e09u, 0xc7485ad3u }; /* somhash: docs/re-animation.md 2.3 */
static const char *const g_nb_node_w[NB_NODES + 1] = { "head", "neck", "chest", "left shoulder", "right shoulder",
							"left hand", "right hand", "weapon" };
enum { NBP_HEAD, NBP_CHEST, NBP_SHOULDERS, NBP_HANDS, NBP_WEAPON, NBP_N }; /* the line's parts (neck + chest as one) */
static const int g_nb_part_of[NB_NODES + 1] = { NBP_HEAD, NBP_CHEST, NBP_CHEST, NBP_SHOULDERS, NBP_SHOULDERS, NBP_HANDS,
						 NBP_HANDS, NBP_WEAPON };
struct nb_trk {
	uintptr_t c, hobj, atk;      /* the move = its attack record (0 = between blows) */
	int st;                      /* its model interface reaches code: 0 not checked, 1 yes, -1 no (not measured) */
	int wst, nw, wtip[2];        /* its weapons (as df_weapons) */
	uintptr_t wobj[2];
	uint32_t wnode[2];
	double t0, tmin, seen;
	uint64_t fmin;               /* the frame of the move's nearest */
	float dmin, root_eye, root_tal, part[NBP_N], need_max, pushed;
	int bmin, sync, marked, mk_b;
	int mvi;                     /* its move's row in the table (-1 none: between blows) */
	float lean_seen, t_lean;     /* this move: its need past the cm at the most, the time into it the need began (-1) */
	uint32_t antic;              /* frames held ahead by the learnt lean */
	float mk_d;                  /* "fight body marks": under the mark's cm and still nearing (0 = not yet) */
	float prev_bd;               /* the body's nearest last frame (the lead), kept across its moves */
	double prev_bt;
	float r0;                    /* the root's distance from the eyes when this hold began (0 = none): the hold never
				      * takes it past this + NB_MAX_CM (run 063903: the pose a frame behind the root - each
				      * step out read as more lean - carried a defender 3.7 m away in a second, again and
				      * again); kept across its moves */
	double need_t;               /* the last frame it needed a hold */
	float prev_pos[3];           /* its root last frame (the pose's lag is at most the root's last step) */
	float push_ep;               /* the root's way out in this hold at the most (cm) */
	uint32_t frames, needs, calls, steps;
	char kind[24];
};
struct nb_move {
	uintptr_t rec;
	char name[64], kind[24];
	uint32_t n, close, held, n_lean;
	float min, sum, pushed;
	float lean, t_lean; /* learnt (the hold on): the root's need past the cm (cm), the time into it the need began */
};
static struct {
	int set, on;                 /* SOMVR_FIGHT_BODY_CM given (0 = measured only); on: the hold (cm > 0, FS_AIMOVE in) */
	float cm;
	int sync_hold;               /* SOMVR_FIGHT_BODY_SYNC=hold: an enemy's sync on Talion (its headbutt) held too; free:
				      * no sync held. Talion's own (a grab, an execution, the strike's lift) never */
	int tsync;                   /* this frame (fs_frame): 0 no sync on Talion, 1 an enemy's on him, 2 his own */
	uintptr_t last_hs[FS_ORCS];  /* the last stereo frame's needs (a frame without the eyes keeps them 0.15 s) */
	float last_need[FS_ORCS], last_eye[3];
	float last_need_a[FS_ORCS];
	int last_n;
	double last_t, prev_t;
	float step_cm, step_a;       /* the hook's ease out per step: NB_PUSH_CMS / NB_ANTIC_CMS x the last frame's time */
	int marks;                   /* the test word "fight body marks N CM": a bug mark when a body comes nearer than */
	float mark_cm;               /* ... this (each move once) */
	struct nb_trk tr[NB_TRK];
	struct nb_move mv[NB_MOVES];
	int nmv;
	uintptr_t pk[FS_ORCS];       /* the snapshot's slots last frame (whose the hook's counters are) */
	int npk;
	uint64_t frames, orc_frames, at_frames, nonode, moves, lines, suppressed, mv_full, need_frames, calls, steps,
		sync_free, kept, antic, null_ok, null_no, capped;
	float lag_max, push_ep_max;
	float pushed;
} g_nb;

/* the hook's thread: an orc at Talion whose body needs its root farther from the eyes (the snapshot's need) */
static void nb_hold(uintptr_t mv, const float was[3], float *xf)
{
	uint32_t s0 = atomic_load(&g_fs.seq);
	int n = g_fs.n;
	if ((s0 & 1) || n <= 0 || n > FS_ORCS)
		return;
	uintptr_t ch = *(uintptr_t *)(mv + FS_MV_CHAR), h = ch ? *(uintptr_t *)(ch + 8) : 0;
	int k = 0;
	while (h && k < n && g_fs.hobj[k] != h)
		k++;
	if (!h || k == n)
		return;
	float need = g_fs.need[k], need_a = g_fs.need_a[k], eye[3], pushed = 0, p2 = 0;
	int he = g_fs.have_eye;
	memcpy(eye, g_fs.eye, sizeof eye);
	if (atomic_load(&g_fs.seq) != s0)
		return;
	atomic_fetch_add(&g_fs.kcalls[k], 1);
	if (!g_nb.on || !he)
		return;
	int a = need_a > 0 && sc_body_push(eye, was, xf, need_a, g_nb.step_a, &pushed); /* ahead of its lean, slower */
	int b = need > 0 && sc_body_push(eye, was, xf, need, g_nb.step_cm, &p2);      /* the body now */
	if (a || b)
		atomic_fetch_add(&g_fs.ksteps[k], 1), atomic_fetch_add(&g_fs.kpush_mm[k], (uint32_t)((pushed + p2) * 10.0f));
}

static int nb_log_ok(void)
{
	if (g_nb.lines < NB_LINES)
		return ++g_nb.lines;
	g_nb.suppressed++;
	return 0;
}

/* the move's row (its attack record; between blows per kind): -1 = the table full */
static int nb_mv_row(uintptr_t rec, const char *kind)
{
	for (int i = 0; i < g_nb.nmv; i++)
		if (g_nb.mv[i].rec == rec && (rec || !strcmp(g_nb.mv[i].kind, kind)))
			return i;
	if (g_nb.nmv >= NB_MOVES)
		return -1;
	struct nb_move *m = &g_nb.mv[g_nb.nmv];
	memset(m, 0, sizeof *m);
	m->rec = rec, m->min = INFINITY, m->t_lean = -1;
	if (!rec || !rx_rec_name(rec, m->name, sizeof m->name)[0])
		snprintf(m->name, sizeof m->name, "%s", rec ? "?" : "(between blows)");
	snprintf(m->kind, sizeof m->kind, "%s", kind);
	return g_nb.nmv++;
}

/* a move over (its attack changed, the orc gone or out of range): its row in the table (and what the hold learnt of
 * its lean), its line */
static void nb_flush(struct nb_trk *e, const char *why)
{
	if (!e->c || !e->frames || !isfinite(e->dmin))
		return;
	int r = e->mvi >= 0 && e->mvi < g_nb.nmv && g_nb.mv[e->mvi].rec == e->atk ? e->mvi : nb_mv_row(e->atk, e->kind);
	struct nb_move *m = r >= 0 ? &g_nb.mv[r] : NULL;
	const char *nm = m ? m->name : "?";
	if (!m)
		g_nb.mv_full++;
	else {
		m->n++, m->close += e->dmin < NB_CLOSE_CM, m->held += e->steps > 0, m->sum += e->dmin, m->pushed += e->pushed;
		m->min = fminf(m->min, e->dmin);
		if (e->atk && e->t_lean >= 0) { /* learnt: the next of its kind is held back ahead of the lean */
			m->lean = fmaxf(m->lean, e->lean_seen);
			m->t_lean = m->n_lean ? fminf(m->t_lean, e->t_lean) : e->t_lean;
			m->n_lean++;
		}
	}
	g_nb.moves++;
	if (e->dmin < NB_LOG_CM && nb_log_ok()) {
		char pt[NBP_N][12];
		for (int k = 0; k < NBP_N; k++)
			snprintf(pt[k], sizeof pt[k], isfinite(e->part[k]) ? "%.0f" : "-", e->part[k]);
		logf_("FIGHT body near t=%.3f frame=%llu orc=%#lx '%s' move '%s'%s: its %s %.0f cm from the eyes %.2f s into it "
		      "(%.2f s long); its root then %.0f cm from the eyes, %.0f from Talion's root; nearest head %s neck/chest %s "
		      "shoulders %s hands %s weapon %s cm; %s (%u frames, %u needing a hold, the hook's calls %u, %u steps held "
		      "%.0f cm; the most it needed %.0f cm from the eyes; held ahead of its lean %u frames; its way out %.0f cm at the most); "
		      "%s",
		      e->tmin, (unsigned long long)e->fmin, (unsigned long)e->c, e->kind, nm, e->sync ? " (a sync)" : "",
		      g_nb_node_w[e->bmin >= 0 && e->bmin <= NB_NODES ? e->bmin : 0], e->dmin, e->tmin - e->t0,
		      e->seen - e->t0, e->root_eye, e->root_tal, pt[NBP_HEAD], pt[NBP_CHEST], pt[NBP_SHOULDERS], pt[NBP_HANDS],
		      pt[NBP_WEAPON], g_nb.on ? "held" : "measured only", e->frames, e->needs, e->calls, e->steps, e->pushed,
		      e->need_max, e->antic, e->push_ep, why);
	}
}

/* its weapons this frame: base = the model object, tip = its tip node (df_weapons' reads, without its counters) */
static int nb_weapons(struct nb_trk *e, uintptr_t m, float ob[2][3], float ot[2][3])
{
	if (!g_th.ok_node)
		return 0;
	if (!e->wst) {
		e->wst = 1, e->nw = 0;
		uintptr_t owner = peek_ptr(e->c + C_OWNER), inv = owner ? peek_ptr(owner + 0x910) : 0;
		uint32_t slot = 8;
		uintptr_t item = inv && peek(inv + 0x280, &slot, 4) && slot <= 7 ? peek_ptr(inv + 0x30 + (uintptr_t)slot * 0x38) : 0;
		uintptr_t b = item ? peek_ptr(item + SL_IT_ENTRIES) : 0, en = item ? peek_ptr(item + SL_IT_ENTRIES + 8) : 0;
		int ne = b && en > b && (en - b) % SL_IT_ESIZE == 0 ? (int)((en - b) / SL_IT_ESIZE) : 0;
		static const char *const tips[] = { "weapon_tip", "axe_tip", "sword_tip" };
		for (int k = 0; k < ne && k < SL_MAX_ENTRIES && e->nw < 2; k++) {
			uintptr_t obj = peek_ptr(b + (uintptr_t)k * SL_IT_ESIZE + SL_IE_OBJ);
			uintptr_t mi = obj ? peek_ptr(obj + OBJ_MODEL) : 0, mv = vptr_of((void *)mi), f = mv ? peek_ptr(mv + 0x20) : 0;
			int pr = f ? page_prot(f) : -1;
			if (!obj || pr < 0 || !(pr & PROT_EXEC))
				continue;
			int w = e->nw++;
			e->wobj[w] = obj, e->wtip[w] = 0;
			for (size_t j = 0; j < sizeof tips / sizeof tips[0] && !e->wtip[w]; j++)
				e->wtip[w] = !(TH_FN(THS_GETNODE, th_getnode_fn)(m, obj, tips[j], &e->wnode[w]) & 0xff);
		}
	}
	int n = 0;
	for (int w = 0; w < e->nw; w++) {
		float op[3], oq[4], xf[16] = { 0 };
		if (!peek(e->wobj[w] + OBJ_POS, op, sizeof op) || !peek(e->wobj[w] + OBJ_ROT, oq, sizeof oq) || !vec_ok(op, 1e7f))
			continue;
		if (e->wtip[w] && !(((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, e->wobj[w], e->wnode[w], xf, 1) & 0xff) &&
		    vec_ok(xf, 1e7f) && dist3(xf, op) < 250) {
			memcpy(ot[n], xf, 12);
		} else {
			sv_vec3 y = sv_qrot(sv_qnormalize(sv_quat_of(oq)), (sv_vec3){ 0, 1, 0 });
			ot[n][0] = op[0] + y.x * 80, ot[n][1] = op[1] + y.y * 80, ot[n][2] = op[2] + y.z * 80;
		}
		memcpy(ob[n], op, 12);
		n++;
	}
	return n;
}

/* the game thread (fs_frame, before its snapshot): the near orcs o[m] measured from the eyes; need[k] for each orc at
 * Talion hs[k] (0 = none) */
static void nb_frame(const struct cb_orc *o, int m, const uintptr_t *hs, int n, const float root[3], int stereo,
		     double t, float *need, float *need_a, float eye_out[3], int *have_eye)
{
	for (int k = 0; k < n; k++)
		need[k] = need_a[k] = 0;
	*have_eye = 0;
	if (!g_nb.set)
		return;
	/* the hook's counters of the last snapshot's slots */
	for (int k = 0; k < g_nb.npk; k++) {
		uint32_t ca = atomic_exchange(&g_fs.kcalls[k], 0), st = atomic_exchange(&g_fs.ksteps[k], 0);
		uint32_t mm = atomic_exchange(&g_fs.kpush_mm[k], 0);
		g_nb.calls += ca, g_nb.steps += st, g_nb.pushed += mm * 0.1f;
		for (int i = 0; i < NB_TRK; i++)
			if (g_nb.tr[i].c && g_nb.tr[i].hobj == g_nb.pk[k])
				g_nb.tr[i].calls += ca, g_nb.tr[i].steps += st, g_nb.tr[i].pushed += mm * 0.1f;
	}
	g_nb.npk = n;
	memcpy(g_nb.pk, hs, sizeof(uintptr_t) * (size_t)n);
	double dt = g_nb.prev_t > 0 ? t - g_nb.prev_t : 1.0 / 90;
	dt = dt < 0.005 ? 0.005 : dt > 0.06 ? 0.06 : dt;
	g_nb.prev_t = t, g_nb.step_cm = NB_PUSH_CMS * (float)dt, g_nb.step_a = NB_ANTIC_CMS * (float)dt;
	g_nb.at_frames += (uint64_t)n;
	uintptr_t mp = peek_ptr(g_som.model_pp);
	if (!stereo && g_nb.on && t - g_nb.last_t < 0.15) { /* no eyes this frame: the last ones' needs */
		for (int k = 0; k < n; k++)
			for (int j = 0; j < g_nb.last_n; j++)
				if (g_nb.last_hs[j] == hs[k])
					need[k] = g_nb.last_need[j], need_a[k] = g_nb.last_need_a[j];
		memcpy(eye_out, g_nb.last_eye, sizeof g_nb.last_eye), *have_eye = 1;
		g_nb.kept++;
	}
	if (!stereo || !g_node_ok || !mp)
		return;
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	float eye[3] = { hp.x, hp.y, hp.z };
	if (!vec_ok(eye, 1e7f))
		return;
	memcpy(eye_out, eye, sizeof eye), *have_eye = 1;
	g_nb.frames++;
	for (int i = 0; i < m; i++) {
		struct nb_trk *e = NULL, *fr = NULL;
		for (int j = 0; j < NB_TRK && !e; j++)
			if (g_nb.tr[j].c == o[i].c)
				e = &g_nb.tr[j];
			else if (!g_nb.tr[j].c && !fr)
				fr = &g_nb.tr[j];
		if (!e && !(e = fr))
			continue;
		if (!e->c) {
			memset(e, 0, sizeof *e);
			e->c = o[i].c, e->hobj = o[i].hobj, e->atk = peek_ptr(o[i].c + RX_C_ATTACK), e->t0 = t, e->dmin = INFINITY;
			for (int k = 0; k < NBP_N; k++)
				e->part[k] = INFINITY;
			e->bmin = -1, e->t_lean = -1;
			if (vptr_of((void *)o[i].c) == g_cbg.vt_combat)
				fb_rec_name(peek_ptr(o[i].c + PR_C_CHARREC), e->kind, sizeof e->kind);
			if (!e->kind[0])
				snprintf(e->kind, sizeof e->kind, "?");
			e->mvi = e->atk ? nb_mv_row(e->atk, e->kind) : -1;
		}
		e->seen = t;
		uintptr_t a = peek_ptr(o[i].c + RX_C_ATTACK);
		if (a != e->atk) { /* a new move */
			nb_flush(e, "its next move");
			uintptr_t c = e->c, h = e->hobj;
			int st = e->st, wst = e->wst, nw = e->nw, wt[2] = { e->wtip[0], e->wtip[1] };
			uintptr_t wo[2] = { e->wobj[0], e->wobj[1] };
			uint32_t wn[2] = { e->wnode[0], e->wnode[1] };
			char kd[24];
			float pbd = e->prev_bd, pr0 = e->r0, ppos[3] = { e->prev_pos[0], e->prev_pos[1], e->prev_pos[2] };
			double pbt = e->prev_bt, pnt = e->need_t;
			memcpy(kd, e->kind, sizeof kd);
			memset(e, 0, sizeof *e);
			e->prev_bd = pbd, e->prev_bt = pbt, e->r0 = pr0, e->need_t = pnt;
			memcpy(e->prev_pos, ppos, sizeof ppos);
			e->c = c, e->hobj = h, e->st = st, e->wst = wst, e->nw = nw, e->atk = a, e->t0 = e->seen = t, e->dmin = INFINITY;
			memcpy(e->wtip, wt, sizeof wt), memcpy(e->wobj, wo, sizeof wo), memcpy(e->wnode, wn, sizeof wn);
			memcpy(e->kind, kd, sizeof kd);
			for (int k = 0; k < NBP_N; k++)
				e->part[k] = INFINITY;
			e->bmin = -1, e->t_lean = -1;
			e->mvi = a ? nb_mv_row(a, e->kind) : -1;
		}
		if (!e->st) { /* once per orc: its model interface must reach code (GetNodeTransform's FUN_008f3c20) */
			uintptr_t mi = peek_ptr(o[i].hobj + OBJ_MODEL), mv = vptr_of((void *)mi), f = mv ? peek_ptr(mv + 0x20) : 0;
			int pr = f ? page_prot(f) : -1;
			e->st = pr >= 0 && (pr & PROT_EXEC) ? 1 : -1;
		}
		if (e->st < 0) {
			g_nb.nonode++;
			continue;
		}
		float p[NB_NODES][3], body[NB_BODY][3], d[NB_NODES + 1], nx[16] = { 0 }, dl[2] = { 0, 0 };
		int nb = 0, got = 0;
		/* the pose's own root: the bones are read as the pose has them, the root (o.pos) where it is now; the body's
		 * points for the hold are moved by the difference - only when it is no more than the root's last step (a
		 * pose behind by a frame), never a standing offset */
		float st = e->prev_bt > 0 && t - e->prev_bt < 0.1 ? hypotf(o[i].pos[0] - e->prev_pos[0], o[i].pos[2] - e->prev_pos[2])
			: 0.0f;
		memcpy(e->prev_pos, o[i].pos, sizeof e->prev_pos);
		if (!(((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)mp, o[i].hobj, NB_H_NULL, nx, 1) & 0xff) &&
		    vec_ok(nx, 1e7f) && hypotf(o[i].pos[0] - nx[0], o[i].pos[2] - nx[2]) <= st + 2.0f)
			dl[0] = o[i].pos[0] - nx[0], dl[1] = o[i].pos[2] - nx[2], g_nb.null_ok++;
		else
			g_nb.null_no++;
		g_nb.lag_max = fmaxf(g_nb.lag_max, hypotf(dl[0], dl[1]));
		for (int k = 0; k < NB_NODES; k++) {
			float xf[16] = { 0 };
			d[k] = INFINITY;
			if ((((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)mp, o[i].hobj, g_nb_hash[k], xf, 1) & 0xff) ||
			    !vec_ok(xf, 1e7f) || dist3(xf, o[i].pos) > 300.0f)
				continue;
			memcpy(p[k], xf, 12), d[k] = dist3(p[k], eye), got++;
			if (k < NB_BODY)
				body[nb][0] = p[k][0] + dl[0], body[nb][1] = p[k][1], body[nb][2] = p[k][2] + dl[1], nb++;
		}
		float ob[2][3], ot[2][3], s0, t0;
		int nw = nb_weapons(e, mp, ob, ot);
		d[NB_NODES] = INFINITY;
		for (int w = 0; w < nw; w++)
			d[NB_NODES] = fminf(d[NB_NODES], sc_seg_dist(ob[w], ot[w], eye, eye, &s0, &t0));
		if (!got) {
			g_nb.nonode++;
			continue;
		}
		g_nb.orc_frames++, e->frames++;
		e->sync |= cb_sync(o[i].c);
		float bd = INFINITY;
		int bk = -1;
		for (int k = 0; k <= NB_NODES; k++) {
			int pk = g_nb_part_of[k];
			e->part[pk] = fminf(e->part[pk], d[k]);
			if (k < NB_BODY && d[k] < bd)
				bd = d[k], bk = k;
		}
		if (bd < e->dmin) {
			e->dmin = bd, e->bmin = bk, e->tmin = t, e->fmin = g_fr.frame;
			e->root_eye = hypotf(o[i].pos[0] - eye[0], o[i].pos[2] - eye[2]);
			e->root_tal = hypotf(o[i].pos[0] - root[0], o[i].pos[2] - root[2]);
		}
		/* headless pictures (the test word): under the mark's cm, at the move's nearest (the first frame it recedes) */
		if (g_nb.marks > 0 && !e->marked && bd < g_nb.mark_cm && (!(e->mk_d > 0) || bd <= e->mk_d))
			e->mk_d = bd, e->mk_b = bk;
		else if (g_nb.marks > 0 && !e->marked && e->mk_d > 0 && bd > e->mk_d + 1.0f) {
			char note[96], nm[64] = "";
			if (!e->atk || !rx_rec_name(e->atk, nm, sizeof nm)[0])
				snprintf(nm, sizeof nm, "%s", e->atk ? "?" : "(between blows)");
			snprintf(note, sizeof note, "%s %s %.0f cm (now %.0f)", nm, g_nb_node_w[e->mk_b], e->mk_d, bd);
			e->marked = 1, g_nb.marks--;
			ss_mark("an orc's body near the eyes (fight body marks)", note, t);
		}
		int insync = cb_sync(o[i].c);
		float lead = e->prev_bt > 0 && t - e->prev_bt < 0.1 && isfinite(e->prev_bd) && isfinite(bd)
			? fminf(fmaxf(e->prev_bd - bd, 0.0f), NB_LEAD_CM) : 0.0f;
		e->prev_bd = bd, e->prev_bt = t;
		for (int k = 0; k < n; k++)
			if (hs[k] == o[i].hobj && g_nb.on) {
				int w = -1;
				float nd = sc_body_need(eye, o[i].pos, (const float (*)[3])body, nb, g_nb.cm + lead, NB_MAX_CM, &w);
				float n0 = lead > 0 ? sc_body_need(eye, o[i].pos, (const float (*)[3])body, nb, g_nb.cm, NB_MAX_CM, &w) : nd;
				float rr = hypotf(o[i].pos[0] - eye[0], o[i].pos[2] - eye[2]), na = 0, ti = (float)(t - e->t0);
				const struct nb_move *mr = e->mvi >= 0 && e->mvi < g_nb.nmv ? &g_nb.mv[e->mvi] : NULL;
				if (mr && mr->n_lean && mr->lean > 0 && ti >= mr->t_lean - NB_ANTIC_S) /* ahead of its learnt lean */
					na = fminf(g_nb.cm + mr->lean, rr + NB_MAX_CM);
				/* a sync not held: Talion's own (a grab, an execution, the strike's lift), an enemy's on him with
				 * SOMVR_FIGHT_BODY_SYNC=free (the orc's own flag alone says nothing: a berserker's every strike
				 * has it, run 053004) */
				if ((nd > 0 || na > rr) && (g_nb.tsync == 2 || (g_nb.tsync == 1 && insync && !g_nb.sync_hold))) {
					g_nb.sync_free++;
					continue;
				}
				if (n0 > 0) { /* learning: how far past the cm its root had to be, from when into the move */
					e->lean_seen = fmaxf(e->lean_seen, n0 - g_nb.cm);
					if (e->t_lean < 0)
						e->t_lean = ti;
				}
				if (nd > 0 || na > rr) { /* a hold: never past where it began + NB_MAX_CM */
					if (!(e->r0 > 0) || t - e->need_t > 0.5)
						e->r0 = rr;
					e->need_t = t;
					float cap = e->r0 + NB_MAX_CM;
					if (nd > cap || na > cap)
						g_nb.capped++;
					nd = fminf(nd, cap), na = fminf(na, cap);
					e->push_ep = fmaxf(e->push_ep, rr - e->r0);
					g_nb.push_ep_max = fmaxf(g_nb.push_ep_max, rr - e->r0);
				}
				need[k] = nd;
				if (na > rr)
					need_a[k] = na, e->antic++, g_nb.antic++;
				if (need[k] > 0 || need_a[k] > 0)
					e->needs++, g_nb.need_frames++, e->need_max = fmaxf(e->need_max, fmaxf(need[k], need_a[k]));
			}
	}
	g_nb.last_n = n, g_nb.last_t = t;
	memcpy(g_nb.last_hs, hs, sizeof(uintptr_t) * (size_t)n), memcpy(g_nb.last_need, need, sizeof(float) * (size_t)n);
	memcpy(g_nb.last_need_a, need_a, sizeof(float) * (size_t)n);
	memcpy(g_nb.last_eye, eye, sizeof eye);
	for (int j = 0; j < NB_TRK; j++) /* gone: not among the near orcs for 0.5 s */
		if (g_nb.tr[j].c && t - g_nb.tr[j].seen > 0.5) {
			nb_flush(&g_nb.tr[j], "out of range or gone");
			g_nb.tr[j].c = 0;
		}
}

/* "fight body CM" (0 = measured only), "fight body marks N CM", "fight body status" */
static const char *nb_word(const char *w)
{
	char b[16] = "";
	float v = 0, c = 0;
	int k = 0;
	if (sscanf(w, "%*s %15s", b) != 1)
		return "fight body (CM | marks N CM | status: ignored)";
	if (!strcmp(b, "marks")) {
		if (!g_bd.test || sscanf(w, "%*s %*s %d %f", &k, &c) != 2 || k < 0 || k > 20 || !(c > 0 && c <= 200))
			return "fight body marks (SOMVR_HANDS_TEST off, or not N 0..20 CM 1..200: ignored)";
		g_nb.marks = k, g_nb.mark_cm = c;
		for (int j = 0; j < NB_TRK; j++)
			g_nb.tr[j].marked = 0;
		logf_("FIGHT body marks %d under %.0f cm (the word)", k, c);
		return "fight body marks";
	}
	if (!strcmp(b, "sync")) { /* "fight body sync hold|free" */
		char c2[8] = "";
		if (sscanf(w, "%*s %*s %7s", c2) != 1 || (strcmp(c2, "hold") && strcmp(c2, "free")))
			return "fight body sync (not hold|free: ignored)";
		g_nb.sync_hold = !strcmp(c2, "hold");
		logf_("CONFIG fight body sync=%s (the word)", c2);
		return "fight body sync";
	}
	if (!strcmp(b, "status")) {
		logf_("FIGHT body status: %s %.0f cm; frames %llu, orc frames %llu (at Talion %llu), moves %llu, needing a hold "
		      "%llu frames, the hook's calls %llu (steps held %llu, %.0f cm); syncs not held %llu frames; sync=%s",
		      g_nb.on ? "held at" : "measured only,", g_nb.cm, (unsigned long long)g_nb.frames, (unsigned long long)g_nb.orc_frames,
		      (unsigned long long)g_nb.at_frames, (unsigned long long)g_nb.moves, (unsigned long long)g_nb.need_frames,
		      (unsigned long long)g_nb.calls, (unsigned long long)g_nb.steps, g_nb.pushed,
		      (unsigned long long)g_nb.sync_free, g_nb.sync_hold ? "hold" : "free");
		return "fight body status";
	}
	v = (float)atof(b);
	if (!g_nb.set || !(v >= 0 && v <= 150) || (v > 0 && !(g_fs.ok && g_fs.space > 0)))
		return "fight body (SOMVR_FIGHT_BODY_CM unset, not CM 0..150, or the FS_AIMOVE hook not in: ignored)";
	g_nb.cm = v, g_nb.on = v > 0;
	logf_("CONFIG fight body %.0f cm (the word)%s", v, v > 0 ? "" : ": measured only");
	return "fight body";
}

static void nb_end_log(void)
{
	if (!g_nb.set)
		return;
	for (int j = 0; j < NB_TRK; j++)
		if (g_nb.tr[j].c)
			nb_flush(&g_nb.tr[j], "the end"), g_nb.tr[j].c = 0;
	logf_("FIGHT END body %s %.0f cm: stereo frames %llu, orc frames measured %llu (no nodes %llu), at Talion %llu (the "
	      "hook's calls %llu = %.2f a frame each; frames needing a hold %llu, steps held %llu, %.0f cm; syncs not held %llu "
	      "frames; frames without the eyes on the last needs %llu; held ahead of a learnt lean %llu; capped %llu, a hold's way out "
	      "%.0f cm at the most; the pose's root read %llu, not %llu, behind the root by %.0f cm at the most), moves %llu (the table full %llu), lines suppressed %llu", g_nb.on ? "held at" : "measured only,", g_nb.cm,
	      (unsigned long long)g_nb.frames, (unsigned long long)g_nb.orc_frames, (unsigned long long)g_nb.nonode,
	      (unsigned long long)g_nb.at_frames, (unsigned long long)g_nb.calls,
	      g_nb.at_frames ? (double)g_nb.calls / (double)g_nb.at_frames : 0.0, (unsigned long long)g_nb.need_frames,
	      (unsigned long long)g_nb.steps, g_nb.pushed, (unsigned long long)g_nb.sync_free, (unsigned long long)g_nb.kept,
	      (unsigned long long)g_nb.antic, (unsigned long long)g_nb.capped, g_nb.push_ep_max,
	      (unsigned long long)g_nb.null_ok, (unsigned long long)g_nb.null_no, g_nb.lag_max, (unsigned long long)g_nb.moves, (unsigned long long)g_nb.mv_full,
	      (unsigned long long)g_nb.suppressed);
	int ord[NB_MOVES];
	for (int i = 0; i < g_nb.nmv; i++)
		ord[i] = i;
	for (int i = 1; i < g_nb.nmv; i++) /* nearest first */
		for (int j = i; j > 0 && g_nb.mv[ord[j]].min < g_nb.mv[ord[j - 1]].min; j--) {
			int x = ord[j];
			ord[j] = ord[j - 1], ord[j - 1] = x;
		}
	for (int i = 0; i < g_nb.nmv && i < 40; i++) {
		const struct nb_move *m = &g_nb.mv[ord[i]];
		logf_("FIGHT END body move '%s' (%s): %u, under %.0f cm %u, nearest %.0f cm, mean nearest %.0f; held %u (%.0f cm); "
		      "its lean learnt %.0f cm from %.2f s into it (%u)", m->name, m->kind, m->n, NB_CLOSE_CM, m->close, m->min,
		      m->n ? m->sum / (float)m->n : 0.0f, m->held, m->pushed, m->lean, m->t_lean, m->n_lean);
	}
}

static void nb_start(int *bad)
{
	memset(&g_nb, 0, sizeof g_nb);
	g_nb.set = getenv("SOMVR_FIGHT_BODY_CM") != NULL;
	g_nb.cm = env_float("SOMVR_FIGHT_BODY_CM", 0, 0, 150, bad);
	if (!g_nb.set)
		return;
	g_nb.on = g_nb.cm > 0 && g_fs.ok && g_fs.space > 0;
	g_nb.sync_hold = env_word("SOMVR_FIGHT_BODY_SYNC", (const char *const[]){ "free", "hold" }, 2, 1);
	logf_("CONFIG fight body %.0f cm (SOMVR_FIGHT_BODY_CM): %s; `FIGHT body near` per move whose body came within %.0f cm "
	      "of the eyes; sync=%s (SOMVR_FIGHT_BODY_SYNC: an enemy's sync on Talion %s; his own never)", g_nb.cm, g_nb.on ? "an orc at Talion whose head, neck, chest or shoulders come nearer than this to "
	      "the eyes has its root held back (eased out, the hands and weapons free; the NPC movement's slot FS_AIMOVE; "
	      "SOMVR_FIGHT_BODY_SYNC: an enemy's sync on him too)"
	      : g_nb.cm > 0 ? "the hold needs SOMVR_FIGHT_SPACE_CM (its hook and the orcs at Talion): measured only"
	      : "measured only", NB_LOG_CM, g_nb.sync_hold ? "hold" : "free", g_nb.sync_hold ? "held too" : "not held");
}

/* ---- SOMVR_COMBAT_STAGGER_CM (build 17): the orc stays in reach after our hit or parry -------------------------------
 * =stagger's reaction to our hit or a parry ('AI_Bip_L100_*') carried the orc 1.36-1.63 m back before it walked back in
 * (the user's session 233334; 30.16's 1.36-1.50 m): every hit broke reach. rl_note at each hit or clank on a living orc
 * starts a reel: the anchor = its root then; while its reaction lasts (until its attack ends or changes again, at most
 * RL_MAX_S) the NPC movement's steps for it (FS_AIMOVE's post-hook) are eased under the cap (sc_reel_cap: 1:1 to half
 * of it, then slowing to a stop just under it; a step nearer Talion free, FIGHT_SPACE after it). The watch goes on to
 * its next strike (RL_WATCH_S): `COMBAT reel` per reel, with the movement's share of the root's path (the runtime
 * answer: does the reaction's root motion pass vf_11?), the cut, the trace every 0.25 s, the next strike's time. */
#define RL_MAX_S 3.0
#define RL_WATCH_S 6.0
#define RL_TRACE 24
struct rl_e {
	uintptr_t c, hobj;
	_Atomic uint32_t gen;
	double t0, t_end, t_next;  /* the hit; the reel's end (0 = still on); its next strike (0 = none yet) */
	float p0[3], prev[3], d0;  /* the anchor; the root last frame; the anchor's distance from Talion's root */
	float max_cm, end_cm, path_cm, dmin, hmin, end_t, end_h, cap;
	float tr[RL_TRACE], trh[RL_TRACE]; /* every 0.25 s: from the hit, from the player's head (NAN = no stereo frame) */
	int ntr, natk;
	uintptr_t atk[3];
	char what[48], next[64];
	_Atomic uint32_t calls, seen_mm, cut_mm, capped; /* FS_AIMOVE's steps for it during the reel (the hook's thread) */
};
static struct {
	float cap;                 /* cm, 0 = measure only */
	int set;                   /* SOMVR_COMBAT_STAGGER_CM given: the hook goes in even at 0 (the word may raise it) */
	struct rl_e e[RL_N];
	uint32_t gen;
	uint64_t reels, capped, vf_seen, longest_n;
	float longest, longest_cap;
} g_rl;

/* the hook's thread: an orc in a reel - its step eased under the cap (and the share of its path counted) */
static void rl_hold(uintptr_t mv, const float was[3], float *xf)
{
	uint32_t s0 = atomic_load(&g_fs.seq);
	int rn = g_fs.rn;
	if ((s0 & 1) || rn <= 0 || rn > RL_N)
		return;
	uintptr_t ch = *(uintptr_t *)(mv + FS_MV_CHAR), h = ch ? *(uintptr_t *)(ch + 8) : 0;
	int k = 0;
	while (h && k < rn && g_fs.rh[k] != h)
		k++;
	if (!h || k == rn)
		return;
	float a[3], root[3];
	memcpy(a, g_fs.ra[k], sizeof a), memcpy(root, g_fs.root, sizeof root);
	int hr = g_fs.have_root, i = g_fs.ri[k];
	uint32_t gen = g_fs.rgen[k];
	float cap = g_fs.rcapk[k] > 0 && g_fs.rcapk[k] < g_fs.rcap ? g_fs.rcapk[k] : g_fs.rcap;
	if (atomic_load(&g_fs.seq) != s0 || i < 0 || i >= RL_N)
		return;
	struct rl_e *e = &g_rl.e[i];
	if (atomic_load(&e->gen) != gen)
		return;
	float step = hypotf(xf[0] - was[0], xf[2] - was[2]), cut = 0;
	atomic_fetch_add(&e->calls, 1);
	if (isfinite(step) && step < 1000)
		atomic_fetch_add(&e->seen_mm, (uint32_t)(step * 10.0f));
	if (cap > 0 && sc_reel_cap(a, hr ? root : NULL, was, xf, cap, &cut))
		atomic_fetch_add(&e->capped, 1), atomic_fetch_add(&e->cut_mm, (uint32_t)(cut * 10.0f));
}

__attribute__((used)) void somvr_w_aimove(void *mv, float *xf)
{
	float was[3];
	int have = g_fs.ok && mv && xf;
	if (have)
		memcpy(was, (char *)mv + FS_MV_POS, sizeof was);
	((fs_move_fn)g_fs.orig)(mv, xf);
	if (have) {
		rl_hold((uintptr_t)mv, was, xf); /* the reel's cap first, the space after it (it has the last word) */
		if (g_fs.space > 0)
			fs_hold((uintptr_t)mv, was, xf);
		if (g_nb.set) /* the body's hold (build 19b) after both: its root farther, never nearer */
			nb_hold((uintptr_t)mv, was, xf);
	}
}

static void fs_start(int *bad)
{
	memset(&g_fs, 0, sizeof g_fs);
	memset(&g_rl, 0, sizeof g_rl);
	g_fs.space = env_float("SOMVR_FIGHT_SPACE_CM", 0, 0, 200, bad);
	g_rl.set = getenv("SOMVR_COMBAT_STAGGER_CM") != NULL;
	g_rl.cap = env_float("SOMVR_COMBAT_STAGGER_CM", 0, 0, 300, bad);
	if (g_rl.set)
		logf_("CONFIG combat stagger %.0f cm (SOMVR_COMBAT_STAGGER_CM%s): an orc's reaction to our hit or a parry keeps it "
		      "within this of where the hit found it (1:1 to half of it, then slowing; a step nearer Talion free; the NPC "
		      "movement's slot FS_AIMOVE), `COMBAT reel` lines", g_rl.cap, g_rl.cap > 0 ? "" : ": 0 = measured only");
	if (!(g_fs.space > 0) && !g_rl.set)
		return;
	static const char *const from_w[] = { "root", "head" };
	g_fs.from_head = env_word("SOMVR_FIGHT_SPACE_FROM", from_w, 2, 1);
	char why[512], err[200] = "";
	if (check_site(&g_fs_sites[0], why, sizeof why) != 0) {
		logf_("WARN SOMVR_FIGHT_SPACE_CM / _COMBAT_STAGGER_CM: the site FS_AIMOVE %#lx did not match (%s): off",
		      (unsigned long)g_fs_sites[0].addr, why);
		(*bad)++;
		return;
	}
	void *fn = (void *)somvr_w_aimove;
	g_fs.orig = g_fs_sites[0].target;
	if (write_code(g_fs_sites[0].addr, &fn, 8, err, sizeof err) != 0) {
		logf_("WARN SOMVR_FIGHT_SPACE_CM / _COMBAT_STAGGER_CM: the slot %#lx not written (%s): off",
		      (unsigned long)g_fs_sites[0].addr, err);
		(*bad)++;
		return;
	}
	g_fs.ok = 1;
	if (g_fs.space > 0)
		logf_("CONFIG fight space %.0f cm (SOMVR_FIGHT_SPACE_CM): an orc whose attack targets Talion moves no nearer than "
		      "this to %s (SOMVR_FIGHT_SPACE_FROM; the NPC movement's slot FS_AIMOVE %#lx: orig %#lx, now %p)", g_fs.space,
		      g_fs.from_head ? "his root or the player's head, whichever is nearer" : "his root",
		      (unsigned long)g_fs_sites[0].addr, (unsigned long)g_fs.orig, fn);
}

/* the game thread, every frame: the snapshot (Talion's root, the orcs whose attack target is Talion) and the lines */
static void fs_frame(uintptr_t pl, int stereo, double t)
{
	if (!g_fs.ok)
		return;
	uintptr_t pm = 0, obj = 0;
	float root[3];
	uintptr_t hs[FS_ORCS];
	int n = 0;
	int hr = pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, root, sizeof root) && vec_ok(root, 1e7f), m = 0;
	struct cb_orc o[CB_ORCS];
	if (hr && (g_fs.space > 0 || g_nb.set)) {
		uintptr_t pcomb = peek_ptr(pl + P_COMBAT), pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0, ph = 0;
		if (pc && vptr_of((void *)pc) != g_cbg.vt_combat)
			pc = 0;
		ph = pc ? peek_ptr(pc + C_HOBJ) : 0;
		uintptr_t sy[2] = { 0, 0 }; /* SOMVR_FIGHT_BODY_SYNC: an enemy's sync on him (as SOMVR_COMBAT_HOLD_TALION's) */
		if (pc && peek(pc + C_SYNC, sy, sizeof sy)) {
			int own = (sy[0] && peek_ptr(sy[0] + FB_SYNC_REC)) || sk_lift(t) || fi_holds();
			g_nb.tsync = own ? 2 : sy[1] ? 1 : 0;
		} else
			g_nb.tsync = 0;
		m = pcomb ? cb_orcs_r(pcomb, pc, root, fmaxf(2.0f * g_fs.space + 100.0f, 280.0f), o, CB_ORCS) : 0;
		for (int i = 0; i < m && n < FS_ORCS && g_fs.space > 0; i++) {
			uintptr_t ta = 0;
			if (peek(o[i].c + TH_C_TARGET, &ta, sizeof ta) && ta && (ta == obj || ta == ph) && !cb_dead(o[i].c))
				hs[n++] = o[i].hobj;
		}
	}
	float need[FS_ORCS], need_a[FS_ORCS], eye[3] = { NAN, NAN, NAN }; /* SOMVR_FIGHT_BODY_CM (build 19b) */
	int he = 0;
	if (hr)
		nb_frame(o, m, hs, n, root, stereo, t, need, need_a, eye, &he);
	float head[3] = { NAN, NAN, NAN };
	int hh = 0;
	if (n && g_fs.from_head && stereo) { /* the eyes the player sees from (a stereo frame's views) */
		sv_vec3 hp;
		sv_quat hq;
		somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
		head[0] = hp.x, head[1] = hp.y, head[2] = hp.z;
		hh = vec_ok(head, 1e7f);
	}
	int rn = 0, ri[RL_N];
	for (int i = 0; i < RL_N; i++) /* the reels on now (rl_frame's) */
		if (g_rl.e[i].c && !g_rl.e[i].t_end)
			ri[rn++] = i;
	atomic_fetch_add(&g_fs.seq, 1);
	if (n)
		memcpy(g_fs.hobj, hs, sizeof hs), memcpy(g_fs.head, head, sizeof head);
	if (n && hr)
		memcpy(g_fs.need, need, sizeof(float) * (size_t)n), memcpy(g_fs.need_a, need_a, sizeof(float) * (size_t)n),
			memcpy(g_fs.eye, eye, sizeof eye);
	g_fs.have_eye = n && hr && he;
	if (hr)
		memcpy(g_fs.root, root, sizeof root);
	for (int k = 0; k < rn; k++) {
		const struct rl_e *e = &g_rl.e[ri[k]];
		g_fs.ri[k] = ri[k], g_fs.rh[k] = e->hobj, g_fs.rgen[k] = atomic_load(&e->gen), g_fs.rcapk[k] = e->cap;
		memcpy(g_fs.ra[k], e->p0, sizeof e->p0);
	}
	g_fs.n = n, g_fs.t = t, g_fs.have_head = hh, g_fs.have_root = hr, g_fs.rn = rn, g_fs.rcap = g_rl.cap;
	atomic_fetch_add(&g_fs.seq, 1);
	uint64_t held = atomic_load(&g_fs.held);
	if (held != g_fs.logged && t >= g_fs.next_log) {
		g_fs.next_log = t + 0.5;
		logf_("FIGHT space t=%.3f held %llu move%s so far (the last: hobj %#lx, from %u cm of the nearer of Talion's root and "
		      "the head its move wanted %u, kept at %u); %d orc%s at him", t, (unsigned long long)held, held == 1 ? "" : "s",
		      (unsigned long)atomic_load(&g_fs.last_hobj), atomic_load(&g_fs.last_was), atomic_load(&g_fs.last_want),
		      atomic_load(&g_fs.last_got), n, n == 1 ? "" : "s");
		g_fs.logged = held;
	}
}

/* the reel's line (its end: the next strike, RL_WATCH_S, another hit on it, the orc gone) */
static void rl_flush(struct rl_e *e, double t, const char *why)
{
	char tr[RL_TRACE * 10 + 8] = "", an[3][64];
	size_t o = 0;
	for (int k = 0; k < e->ntr && o < sizeof tr - 11; k++)
		o += (size_t)snprintf(tr + o, sizeof tr - o, "%s%.0f/%.0f", k ? " " : "", e->tr[k], e->trh[k]);
	for (int k = 0; k < 3; k++)
		an[k][0] = 0;
	for (int k = 0; k < e->natk && k < 3; k++)
		if (!rx_rec_name(e->atk[k], an[k], sizeof an[k])[0])
			snprintf(an[k], sizeof an[k], "%s", e->atk[k] ? "?" : "none");
	uint32_t calls = atomic_load(&e->calls), seen = atomic_load(&e->seen_mm), cut = atomic_load(&e->cut_mm),
		 capped = atomic_load(&e->capped);
	double rs = (e->t_end ? e->t_end : t) - e->t0;
	g_rl.reels++, g_rl.capped += capped > 0, g_rl.vf_seen += calls > 0;
	if (e->max_cm > g_rl.longest)
		g_rl.longest = e->max_cm, g_rl.longest_cap = e->cap;
	logf_("COMBAT reel orc=%#lx after %s: its attacks '%s'%s%s%s%s%s%s; the reel %.0f cm at most from where the hit found it "
	      "(%.0f cm from Talion's root then) in %.2f s, %.0f cm at its end (%.0f cm from Talion's root, %.0f from the head); "
	      "the root's path %.0f cm, the NPC movement's steps for it %u (%.0f cm), the cap %.0f cm: %u steps eased (%.0f cm "
	      "off); every 0.25 s (cm from the hit/from the head): %s; nearest Talion's root %.0f cm, the head %.0f; %s",
	      (unsigned long)e->c, e->what, an[0], e->natk > 1 ? " -> '" : "", an[1], e->natk > 1 ? "'" : "",
	      e->natk > 2 ? " -> '" : "", an[2], e->natk > 2 ? "'" : "", e->max_cm, e->d0, rs, e->end_cm, e->end_t, e->end_h,
	      e->path_cm, calls, seen * 0.1f, e->cap, capped, cut * 0.1f, tr, e->dmin, e->hmin, why);
	atomic_fetch_add(&e->gen, 1);
	e->c = 0;
}

static int rl_word(float cm)
{
	if (cm > 0 && !g_fs.ok)
		return 0;
	g_rl.cap = cm;
	logf_("CONFIG combat stagger %.0f cm (the word)", cm);
	return 1;
}

/* a hit or a clank on a living orc (cb_hit, df_clank): its reel starts here */
static void rl_note(uintptr_t c, uintptr_t hobj, const char *what, double t)
{
	struct rl_e *e = NULL;
	for (int i = 0; i < RL_N && !e; i++)
		if (g_rl.e[i].c == c)
			rl_flush(e = &g_rl.e[i], t, "hit again");
	for (int i = 0; i < RL_N && !e; i++)
		if (!g_rl.e[i].c)
			e = &g_rl.e[i];
	float p[3];
	if (!e || !hobj || cb_dead(c) || !peek(hobj + OBJ_POS, p, sizeof p) || !vec_ok(p, 1e7f))
		return;
	uint32_t gen = atomic_load(&e->gen) + 1;
	memset(e, 0, sizeof *e); /* (the hook skips it until the snapshot names the new gen) */
	atomic_store(&e->gen, gen);
	e->c = c, e->hobj = hobj, e->t0 = t, e->cap = g_rl.cap, e->dmin = e->d0 = e->hmin = e->end_t = e->end_h = NAN;
	memcpy(e->p0, p, sizeof p), memcpy(e->prev, p, sizeof p);
	e->atk[0] = peek_ptr(c + RX_C_ATTACK), e->natk = 1;
	snprintf(e->what, sizeof e->what, "%s", what);
}

/* the game thread, every frame: each reel's trace, its end (the reaction over, or a sync action after the hit: never
 * eased - a berserker's strikes are flagged sync themselves), the next strike (THREAT's track with an impact) */
static void rl_frame(uintptr_t pl, int stereo, double t)
{
	uintptr_t pm = 0, obj = 0;
	float root[3], head[3] = { NAN, NAN, NAN };
	int hr = pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, root, sizeof root) && vec_ok(root, 1e7f), any = 0;
	for (int i = 0; i < RL_N; i++)
		any |= g_rl.e[i].c != 0;
	if (any && stereo) { /* the eyes the player sees from */
		sv_vec3 hp;
		sv_quat hq;
		somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
		head[0] = hp.x, head[1] = hp.y, head[2] = hp.z;
	}
	for (int i = 0; i < RL_N; i++) {
		struct rl_e *e = &g_rl.e[i];
		float p[3];
		if (!e->c)
			continue;
		if (!peek_ptr(e->c + C_OWNER) || !peek(e->hobj + OBJ_POS, p, sizeof p) || !vec_ok(p, 1e7f)) {
			rl_flush(e, t, "the orc gone");
			continue;
		}
		float d = hypotf(p[0] - e->p0[0], p[2] - e->p0[2]), dt = hr ? hypotf(p[0] - root[0], p[2] - root[2]) : NAN;
		float dh = hypotf(p[0] - head[0], p[2] - head[2]);
		if (hr && !isfinite(e->d0))
			e->d0 = hypotf(e->p0[0] - root[0], e->p0[2] - root[2]);
		if (isfinite(dt) && !(e->dmin <= dt))
			e->dmin = dt;
		if (isfinite(dh) && !(e->hmin <= dh))
			e->hmin = dh;
		while (e->ntr < RL_TRACE && t - e->t0 >= 0.25 * (e->ntr + 1))
			e->trh[e->ntr] = dh, e->tr[e->ntr++] = d;
		uintptr_t a = peek_ptr(e->c + RX_C_ATTACK);
		if (!e->t_end) {
			e->path_cm += hypotf(p[0] - e->prev[0], p[2] - e->prev[2]);
			e->max_cm = fmaxf(e->max_cm, d);
			if (a != e->atk[e->natk - 1] && e->natk < 3)
				e->atk[e->natk++] = a;
			int dead = cb_dead(e->c);
			if (dead || (cb_sync(e->c) && a != e->atk[0]) || t - e->t0 >= RL_MAX_S || e->natk >= 3 ||
			    (!a && t - e->t0 >= 0.2))
				e->t_end = t, e->end_cm = d, e->end_t = dt, e->end_h = dh; /* the reaction is over */
			if (dead) {
				rl_flush(e, t, "it died");
				continue;
			}
		} else { /* its next strike: a THREAT track of this orc with an impact, started after the hit */
			const struct th_track *st = NULL;
			for (int k = 0; k < TH_TRACKS && !st && g_th.on; k++)
				if (g_th.tr[k].c == e->c && g_th.tr[k].k.impact > 0 && g_th.tr[k].t0 > e->t0 + 0.1)
					st = &g_th.tr[k];
			char nm[64];
			if (st || (a && a != e->atk[e->natk - 1] && strstr(rx_rec_name(a, nm, sizeof nm), "_Melee_"))) {
				char why[128];
				if (st)
					rx_rec_name(st->attack, nm, sizeof nm);
				snprintf(why, sizeof why, "its next strike '%s' %.2f s after the hit, %.0f cm from Talion's root", nm,
					 (st ? st->t0 : t) - e->t0, dt);
				rl_flush(e, t, why);
				continue;
			}
			if (a && a != e->atk[e->natk - 1] && e->natk < 3)
				e->atk[e->natk++] = a;
		}
		memcpy(e->prev, p, sizeof p);
		if (t - e->t0 >= RL_WATCH_S)
			rl_flush(e, t, "no strike in 6 s");
	}
}

/* SOMVR_FIGHT_REACH="ATTACK:MIN..MAX,..." (build 16): those attacks' StrikeRange set once the database is loaded (the
 * infantry's overhead chop 'AI_Infantry_Melee_Front_3_LL' 50..70 lands ~25 cm nearer than its other strikes and its
 * follow-through walks it into Talion: its sibling Front_1's 65..85) */
static struct { char name[8][64]; float lo[8], hi[8]; int n, done, tries; } g_frr;
static void frr_config(int *bad)
{
	const char *v = getenv("SOMVR_FIGHT_REACH"), *p = v;
	memset(&g_frr, 0, sizeof g_frr);
	while (p && *p && g_frr.n < 8) {
		size_t n = strcspn(p, ",");
		char item[96];
		float lo = 0, hi = 0;
		snprintf(item, sizeof item, "%.*s", (int)(n < sizeof item - 1 ? n : sizeof item - 1), p);
		char *c = strchr(item, ':'), *dd = c ? strstr(c + 1, "..") : NULL, *e1 = NULL, *e2 = NULL;
		if (dd) /* (sscanf's "%f..%f" takes "65." as the first number) */
			*dd = 0, lo = strtof(c + 1, &e1), hi = strtof(dd + 2, &e2);
		if (dd && e1 && !*e1 && e2 && !*e2 && e1 > c + 1 && e2 > dd + 2 && lo >= 0 && hi >= lo && hi <= 1000 && c - item < 64) {
			*c = 0;
			memcpy(g_frr.name[g_frr.n], item, (size_t)(c - item) + 1);
			g_frr.lo[g_frr.n] = lo, g_frr.hi[g_frr.n] = hi, g_frr.n++;
		} else if (n) {
			logf_("WARN SOMVR_FIGHT_REACH: '%.*s' is not ATTACK:MIN..MAX (cm): skipped", (int)n, p);
			(*bad)++;
		}
		p += n + (p[n] == ',');
	}
	if (g_frr.n)
		logf_("CONFIG fight reach: %d %s StrikeRange set once the database is loaded (SOMVR_FIGHT_REACH), the first "
		      "'%s' %.0f..%.0f cm", g_frr.n, g_frr.n == 1 ? "attack's" : "attacks'", g_frr.name[0], g_frr.lo[0], g_frr.hi[0]);
}
static void frr_frame(void)
{
	if (!g_frr.n || g_frr.done || !g_cb.ok_hit || g_cb.mode != CB_FULL)
		return;
	uintptr_t cat = peek_ptr(g_cbg.atkcat_pp);
	uint32_t cnt = 0;
	if (!cat || !peek(cat + CAT_COUNT, &cnt, sizeof cnt) || !cnt)
		return; /* the database is not loaded yet */
	g_frr.done = 1;
	for (int i = 0; i < g_frr.n; i++)
		fr_range_set(g_frr.name[i], g_frr.lo[i], g_frr.hi[i], "SOMVR_FIGHT_REACH");
}

static void fight_frame(uintptr_t pl, int stereo)
{
	double t = cb_now();
	g_ft_pl = pl;
	frr_frame();
	rl_frame(pl, stereo, t);
	fs_frame(pl, stereo, t);
	kx_step();
	kb_frame(t);
	tg_frame(t);
	hu_frame(pl, stereo, t);
	if (stereo && g_th.on)
		for (int i = 0; i < TH_TRACKS; i++) {
			const struct th_track *tr = &g_th.tr[i];
			if (tr->c && tr->k.impact > 0 && t - tr->t0 >= tr->k.impact - 0.06 && t - tr->t0 < tr->k.impact + 0.1)
				og_probe(tr, t);
		}
}

/* build 10's switches besides SOMVR_ARMS_LW (the start, after df_start) */
static void fight_start(int *bad)
{
	frr_config(bad);
	fs_start(bad);
	nb_start(bad); /* after fs_start: the hook in or not */
	g_hu.on = env_word("SOMVR_HURT", (const char *const[]){ "0", "1" }, 2, 0);
	g_hu.ms = env_float("SOMVR_HURT_MS", g_hu.ms, 50, 2000, bad);
	g_hu.flash = env_float("SOMVR_HURT_FLASH", g_hu.flash, 0, 1, bad);
	g_kx.mode = env_word("SOMVR_COMBAT_KNOCKBACK", g_kx_mode_w, 4, KX_OFF);
	g_kx.sliver = env_float("SOMVR_COMBAT_SLIVER", 0, 0, 1000, bad);
	if (g_kx.sliver > 0)
		logf_("CONFIG combat sliver %.0f (SOMVR_COMBAT_SLIVER%s): a hit that would leave an orc this much health or less "
		      "kills it", g_kx.sliver, g_kx.mode == KX_STAGGER ? "" : "; needs SOMVR_COMBAT_KNOCKBACK=stagger: inactive");
	kd_config(bad);
	logf_("CONFIG knockback=%s: %s%s", g_kx_mode_w[g_kx.mode],
	      !g_kx.mode ? "the records' own recoil types (build 9)"
	      : g_kx.mode == KX_LIGHT ? "our hits' RecoilTypeHit 'Light_<the swing's direction>' for the call; a clank's 'Heavy_*' "
	      "recoil the light one" : g_kx.mode == KX_MEDIUM ? "our heavy hits 'Medium_<the swing's direction>', the others "
	      "'Light_*'; a clank's 'Heavy_*' recoil the medium one" : "a heavy hit or a stab that does not kill dealt with the "
	      "light record (its stagger) after the orc's health took the difference; a clank's 'Heavy_*' recoil the light one",
	      g_kx.mode == KX_LIGHT || g_kx.mode == KX_MEDIUM ? "; after a hit that does not kill, the stagger (the light "
	      "record's recoil: the orc's own strike's light/medium RecoilTypeHit, else 'Light_<dir>')" : "");
	logf_("CONFIG hurt=%d: Talion's health drop = a hit: %s; the flash %.0f ms at %.2f (SOMVR_THREAT's edge bands in red: %s)",
	      g_hu.on, g_hu.on ? "a pulse in both hands, a red flash on the view's edges (the blow's side full, the other half)"
	      : "logged only", g_hu.ms, g_hu.flash, g_th.on ? "on" : "SOMVR_THREAT is off: no flash, the pulse only");
}

static void fight_end_log(void)
{
	if (g_fs.ok)
		logf_("FIGHT END space %.0f cm: NPC moves %llu, near Talion %llu (an orc at him) + %llu (another), held %llu",
		      g_fs.space, (unsigned long long)atomic_load(&g_fs.calls), (unsigned long long)atomic_load(&g_fs.near),
		      (unsigned long long)atomic_load(&g_fs.other), (unsigned long long)atomic_load(&g_fs.held));
	nb_end_log();
	if (g_rl.reels || g_rl.set)
		logf_("FIGHT END stagger %.0f cm: reels %llu (eased %llu, the NPC movement saw %llu), the farthest %.0f cm (the cap "
		      "then %.0f)", g_rl.cap, (unsigned long long)g_rl.reels, (unsigned long long)g_rl.capped,
		      (unsigned long long)g_rl.vf_seen, g_rl.longest, g_rl.longest_cap);
	if (g_kx.sliver > 0)
		logf_("FIGHT END sliver %.0f: hits made kills %llu", g_kx.sliver, (unsigned long long)g_kx.slivers);
	logf_("FIGHT END hurt=%d knockback=%s: hits on Talion %llu (flashes %llu); reactions watched %llu (the largest move %.2f m "
	      "during a reaction, %.2f m in its 2.5 s); hits given a lighter recoil %llu (not found %llu), restaggered %llu (took "
	      "%llu), dealt as light hits %llu (undone %llu, a light hit's damage %.1f), clank recoils lighter %llu; recoil types "
	      "indexed %d", g_hu.on, g_kx_mode_w[g_kx.mode],
	      (unsigned long long)g_hu.hurts, (unsigned long long)g_hu.flashes, (unsigned long long)g_kb.done,
	      g_kb.react_max_cm * 0.01f, g_kb.max_cm * 0.01f, (unsigned long long)g_kx.overrides, (unsigned long long)g_kx.missing,
	      (unsigned long long)g_kx.restaggers, (unsigned long long)g_kx.restagger_took, (unsigned long long)g_kx.staggers,
	      (unsigned long long)g_kx.stagger_undone, g_kx.light_dmg, (unsigned long long)g_kx.clank_lighter, g_kx.n);
	logf_("FIGHT END death=%s dir=%.2f: kills followed %llu (the farthest body %.2f m), blows given the type %llu (not %llu), "
	      "directions scaled %llu, pin writes %llu", g_kd.mode ? g_kd.type : "game", g_kd.dir, (unsigned long long)g_kd.kills,
	      g_kd.max_cm * 0.01f, (unsigned long long)g_kd.typed, (unsigned long long)g_kd.missing,
	      (unsigned long long)g_kd.scaled, (unsigned long long)g_kd.pins);
	if (g_tg.learnt || g_tg.guards[TG_PATH] || g_tg.guards[TG_MISS])
		logf_("TGUARD END: learnt %llu strikes of %d records; path guards %llu blocked %llu (our hit first %llu), miss guards "
		      "%llu blocked %llu (our hit first %llu), no guard (not learnt) %llu; checks %llu: our blade %.1f cm, its weapon "
		      "%.1f cm off the target on average",
		      (unsigned long long)g_tg.learnt, g_tg.nl, (unsigned long long)g_tg.guards[TG_PATH],
		      (unsigned long long)g_tg.blocks[TG_PATH], (unsigned long long)g_tg.ourhit[TG_PATH],
		      (unsigned long long)g_tg.guards[TG_MISS], (unsigned long long)g_tg.blocks[TG_MISS],
		      (unsigned long long)g_tg.ourhit[TG_MISS], (unsigned long long)g_tg.unknown, (unsigned long long)g_tg.checks,
		      g_tg.checks ? g_tg.off_sum / g_tg.checks : 0, g_tg.checks ? g_tg.pred_sum / g_tg.checks : 0);
	if (!g_arm.on)
		return;
	struct aw_win *t = &g_aw.tot;
	aw_win_max(t, &g_aw.w);
	logf_("FIGHT END arms lw=%s: captures %llu (L %s, R %s), watches %llu; the animated weapon node off the held one up to "
	      "L %.1f cm %.0f deg, R %.1f cm %.0f deg (the hand off the controller-rigid target up to L %.1f R %.1f cm with it); "
	      "held frames L %llu R %llu; the hand nodes at H4 vs the solved up to L %.2f R %.2f cm", g_aw_mode_w[g_aw.mode],
	      (unsigned long long)g_aw.captures, g_aw.s[0].hold.have ? "held" : "none", g_aw.s[1].hold.have ? "held" : "none",
	      (unsigned long long)g_aw.watches, t->off_cm[0], t->off_deg[0], t->off_cm[1], t->off_deg[1], t->hand_cm[0],
	      t->hand_cm[1], (unsigned long long)t->held[0], (unsigned long long)t->held[1], t->node_cm[0], t->node_cm[1]);
}
