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
	if (g_kx.mode != KX_STAGGER || tier == SC_TIER_LIGHT || blk || !isfinite(hp0) || (isfinite(sh0) && sh0 > 0))
		return 0;
	const char *ln = "(none)";
	uintptr_t light = cb_record(SC_TIER_LIGHT, 0, &ln), own = peek_ptr(c + C_OWNER);
	float e = g_kx.light_dmg * (tier == SC_TIER_HEAVY ? 3.0f : 1.0f), nh = hp0 - e;
	int kill = !(nh > g_kx.light_dmg + 0.5f); /* the tier's hit kills: the light hit kills too (a light death: boot 6's
						   * heavy record's 'AI_Bip_STR_Death_Hvy_F' threw the body 4.8 m) */
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
/* the trigger word "fight guard off|learn|path|miss", "fight death game|TYPE", "fight death dir S", "fight death pin CM" */
static const char *fight_word(const char *w)
{
	char a[16] = "", b[48] = "";
	float v = 0;
	if (sscanf(w, "%15s %47s", a, b) < 2)
		return "fight (not guard|death ...: ignored)";
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
		return "fight guard (not off|learn|path|miss: ignored)";
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
	return "fight (not guard|death ...: ignored)";
}

static uint64_t hu_hurts(void)
{
	return g_hu.hurts;
}

/* H4, after df_frame (every frame: flat ones too) */
static void fight_frame(uintptr_t pl, int stereo)
{
	double t = cb_now();
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
	g_hu.on = env_word("SOMVR_HURT", (const char *const[]){ "0", "1" }, 2, 0);
	g_hu.ms = env_float("SOMVR_HURT_MS", g_hu.ms, 50, 2000, bad);
	g_hu.flash = env_float("SOMVR_HURT_FLASH", g_hu.flash, 0, 1, bad);
	g_kx.mode = env_word("SOMVR_COMBAT_KNOCKBACK", g_kx_mode_w, 4, KX_OFF);
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
