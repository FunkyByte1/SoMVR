/* somvr_hold_game.c - SOMVR_COMBAT_HOLD_TALION=on (build 17; docs/re-xr-pipeline.md 64, handoff/HANDOFF-combat.md
 * "build 17: no game moves of the player"). NOT a translation unit of its own: somvr.c includes it last (it uses g_lo,
 * the Combat reads, the log).
 * The user's build 16 session: a berserker's paired sync on Talion ('AI_Berserker_PC_Sync_Headbutt_F', his half
 * 'PC_OrcBzrk_Headbutt'; the game starts it when our Medium strike lands on a berserker) took LOCO's ticks
 * (combatmove+sync) for 0.9 s and carried Talion - and the eyes on his root - 86 cm. Every enemy's sync on the player
 * in the database (196 records: the berserker's headbutt, bodyslam and mangler, the defender's wrecker, the throat
 * grab, the captains' counters, the pins and the last-chance ones) is an AI-owned sync ('AI_..._PC_...'); Talion's
 * Combat holds the one he is the target of at +0x190 with +0x188's record empty (his own - a grab, an execution, the
 * Shadow Strike's knockdown, a counter - at +0x188 with its record).
 *   off (the default)  as build 16: the game's sync takes the ticks (SOMVR_LOCO_HOLD's words as they are).
 *   on                 while an enemy's sync holds Talion (never in the Shadow Strike's lift or a finisher's moment),
 *                      LOCO keeps his ticks through the attack, combat-move, sync and translation-lock reasons: the
 *                      game's root motion goes, the stick still walks him; the orc's half, its blow, the damage,
 *                      SOMVR_HURT and the haptics stay the game's. A move of the game's outside our delta (an
 *                      alignment) is measured against where our own moves put him.
 * Lines: `CONFIG hold talion ...`, `HOLD talion start|end ...` (per hold: the sync, the reasons held, how far the root
 * went beyond our own moves), `HOLD talion END ...`. */

#define TK_BITS (SOMVR_LOCO_W_ATTACK | SOMVR_LOCO_W_COMBATMOVE | SOMVR_LOCO_W_SYNC | SOMVR_LOCO_W_LOCK)
#define TK_LINES 400
#define TK_PULL_CM 60.0f /* a tick's pull back at most (LOCO's own max step) */
#define TK_ATK_S 0.3      /* his half: its attack seen within this of the start; gone this long = the hold ends */
#define TK_MAX_S 8.0      /* the longest hold (the paired attacks run 1-4 s) */
#define TK_BACK_CM 1.0f   /* the body back under the view: the view takes the root again */
#define TK_BACK_Y_CM 2.0f /* ... and as high as the view (the game's fall puts him down) */
#define TK_RET_S 0.5      /* ... or after this long (the rest: the anchor's hand-off ease) */

static struct {
	int on, used;                  /* used: on at some point (the END line) */
	int nopin;                     /* the test word "holdtalion nopin": measured, not pulled back */
	/* the hold under way */
	int active;
	uintptr_t s0, s1, rec1, atk;   /* Talion's Combat +0x188 / +0x190 at the start, the target sync's record, his attack */
	uintptr_t refused, ref_rec, ref_atk; /* a hold's sync slot, record and his attack once it ended: not held again (the
					      * record outlives the halves) until the slot clears, the record changes or his
					      * attack leaves and comes back (a new paired attack of the same kind) */
	int ref_gone;
	double t0, atk_t;              /* the start; the last tick his attack was the hold's */
	int have_pin;
	double ret_t;                  /* the hold's reasons over: the body going back under the view since */
	const char *ret_why;
	float pin[3];                  /* where our own moves put the root since the start (the start + our asked deltas) */
	float dev, dev_max;            /* the root's horizontal distance from the pin before a tick: now, the most */
	float dev_y, dev_y_max;        /* ... its height off the pin's */
	float asked_cm, pulled_cm;     /* our asked moves over the hold; the pulls back (the game's moves outside our delta) */
	sv_vec3 pull;                  /* this tick's pull back (in LOCO's delta, not in the pin) */
	uint32_t held, left;           /* the game's reasons held ours / left the game's (others: input, fsm, ...) */
	uint64_t ticks, owned, pulls, after, rebased, view_frames, ret_ticks;
	float view_max;                /* the frame's root off the pin at the most (what the view did not take) */
	/* totals */
	uint64_t holds, held_ticks, owned_ticks, all_pulls, lines, suppressed;
	float dev_all, pulled_all;
	double secs;
} g_tk;

static int tk_log_ok(void)
{
	if (g_tk.lines < TK_LINES)
		return ++g_tk.lines;
	g_tk.suppressed++;
	return 0;
}

static void tk_config(void)
{
	memset(&g_tk, 0, sizeof g_tk);
	g_tk.on = g_tk.used = env_word("SOMVR_COMBAT_HOLD_TALION", (const char *const[]){ "off", "on" }, 2, 0) && g_lo.on;
	if (g_tk.on)
		logf_("CONFIG hold talion=on: an enemy's sync on Talion (his Combat +0x190 set, +0x188's record empty) and his "
		      "half of it play in place: LOCO keeps his ticks through attack+combatmove+sync+lock (not in the Shadow "
		      "Strike's lift or a finisher's moment), a game move outside our delta pulled back (at most %.0f cm a tick); "
		      "the blow, the damage and the hurt feedback stay the game's", TK_PULL_CM);
	else if (getenv("SOMVR_COMBAT_HOLD_TALION") && !strcmp(getenv("SOMVR_COMBAT_HOLD_TALION"), "on"))
		logf_("CONFIG hold talion=on asked, off: SOMVR_LOCO is off (nothing of ours owns Talion's ticks)");
}

static void tk_finish(double t, const char *why)
{
	char a[96], b[96], nm[96];
	g_tk.active = 0, g_tk.holds++, g_tk.refused = g_tk.s1, g_tk.ref_rec = g_tk.rec1, g_tk.ref_atk = g_tk.atk, g_tk.ref_gone = 0;
	g_tk.dev_all = fmaxf(g_tk.dev_all, g_tk.dev_max), g_tk.pulled_all += g_tk.pulled_cm, g_tk.secs += t - g_tk.t0;
	if (tk_log_ok())
		logf_("HOLD talion end t=%.3f after %.3f s (%s): the sync '%s'; %llu ticks (%llu after its slot, %llu not ours before), "
		      "%llu of them ours; the game's reasons held ours %s, left the game's %s; the root before a tick at most "
		      "%.1f cm from where our own moves put it (%.1f at the end; its height off by up to %.1f), %llu pulls back "
		      "(%.1f cm), our own moves %.1f cm; "
		      "the view on that point %llu frames (the root off it up to %.1f cm); back under the view in %llu ticks (%.3f "
		      "s), the view's step to the root %.1f cm", t, t - g_tk.t0, why, rx_rec_name(g_tk.rec1, nm, sizeof nm)[0] ? nm : "?", (unsigned long long)g_tk.ticks,
		      (unsigned long long)g_tk.after, (unsigned long long)g_tk.rebased, (unsigned long long)g_tk.owned, somvr_loco_why_str(g_tk.held, a, sizeof a),
		      g_tk.left ? somvr_loco_why_str(g_tk.left, b, sizeof b) : "none", g_tk.dev_max, g_tk.dev,
		      g_tk.dev_y_max, (unsigned long long)g_tk.pulls, g_tk.pulled_cm, g_tk.asked_cm, (unsigned long long)g_tk.view_frames,
		      g_tk.view_max, (unsigned long long)g_tk.ret_ticks, g_tk.ret_t > 0 ? t - g_tk.ret_t : 0.0,
		      g_tk.nopin ? 0.0f : g_tk.dev);
}

/* lo_pm_pre, after SOMVR_LOCO_HOLD (sc = Talion's server Combat, root = his root before this tick's vf_11): an enemy's
 * sync on him keeps the ticks ours while his half of it plays (his attack the one the hold started with; a sync record
 * outlives both halves - HANDOFF-combat.md build 7 - so the slot alone does not end it), at most TK_MAX_S; then the
 * body goes back under the view (the pull back alone, the view still on the pin) before the view takes the root again */
static void tk_pm_own(uintptr_t sc, somvr_loco_in *in, const float root[3], int have_root, double t)
{
	g_tk.pull = (sv_vec3){ 0, 0, 0 };
	if (!g_tk.on)
		return;
	uintptr_t s[2] = { 0, 0 }, rec0 = 0, rec1 = 0, atk = 0;
	if (sc && vptr_of((void *)sc) == g_cbg.vt_combat && peek(sc + C_SYNC, s, sizeof s)) {
		rec0 = s[0] ? peek_ptr(s[0] + FB_SYNC_REC) : 0;
		rec1 = s[1] ? peek_ptr(s[1] + FB_SYNC_REC) : 0;
		atk = peek_ptr(sc + VB_C_ATTACK);
	}
	int mine = sk_lift(t) || fi_holds(), enemy = s[1] && !rec0 && !mine;
	if (!g_tk.active) {
		if (g_tk.refused && (!s[1] || rec1 != g_tk.ref_rec || (g_tk.ref_gone && atk && atk == g_tk.ref_atk)))
			g_tk.refused = 0;
		else if (g_tk.refused && atk != g_tk.ref_atk)
			g_tk.ref_gone = 1;
		if (!enemy || (g_tk.refused && g_tk.refused == s[1]))
			return;
		char n0[96], n1[96], na[96], w[96];
		g_tk.active = 1, g_tk.t0 = g_tk.atk_t = t, g_tk.ret_t = 0, g_tk.ret_why = NULL;
		g_tk.s0 = s[0], g_tk.s1 = s[1], g_tk.rec1 = rec1, g_tk.atk = atk;
		g_tk.ticks = g_tk.owned = g_tk.pulls = g_tk.after = g_tk.rebased = g_tk.view_frames = g_tk.ret_ticks = 0;
		g_tk.held = g_tk.left = 0, g_tk.dev = g_tk.dev_max = g_tk.asked_cm = g_tk.pulled_cm = g_tk.view_max = 0;
		g_tk.dev_y = g_tk.dev_y_max = 0;
		g_tk.have_pin = have_root;
		if (have_root)
			memcpy(g_tk.pin, root, sizeof g_tk.pin);
		if (tk_log_ok())
			logf_("HOLD talion start t=%.3f frame=%llu: his Combat +0x188 %#lx (record '%s') +0x190 %#lx (record '%s'), "
			      "his attack '%s'; the game's reasons now %s -> the root held at %.1f,%.1f,%.1f", t,
			      (unsigned long long)rend_frame(), (unsigned long)s[0], rx_rec_name(rec0, n0, sizeof n0),
			      (unsigned long)s[1], rx_rec_name(rec1, n1, sizeof n1), rx_rec_name(atk, na, sizeof na),
			      somvr_loco_why_str(in->why, w, sizeof w), g_tk.pin[0], g_tk.pin[1], g_tk.pin[2]);
	} else {
		if (have_root && g_tk.have_pin && g_lo.s.owned_last) { /* the pin follows our own last move (the stick, the
									 * follow; not the pull back) */
			float ax = g_lo.s.asked_last.x, az = g_lo.s.asked_last.z;
			g_tk.pin[0] += ax, g_tk.pin[2] += az;
			if (hypotf(ax, az) > 0.1f) /* the height: his own while our moves walk him (the ground), else the start's
						    * (run 121811: the headbutt lifted the root 12.7 cm) */
				g_tk.pin[1] = root[1];
			g_tk.asked_cm += hypotf(ax, az);
			g_tk.dev = hypotf(root[0] - g_tk.pin[0], root[2] - g_tk.pin[2]), g_tk.dev_y = fabsf(root[1] - g_tk.pin[1]);
			if (!isfinite(g_tk.dev + g_tk.dev_y))
				g_tk.dev = g_tk.dev_y = 0, g_tk.have_pin = 0;
			g_tk.dev_max = fmaxf(g_tk.dev_max, g_tk.dev), g_tk.dev_y_max = fmaxf(g_tk.dev_y_max, g_tk.dev_y);
		} else { /* the last tick was not ours (another reason took it: the input, the FSM, a climb) or no root: whatever
			  * moved him then is not pulled back; the pin starts again from where he is */
			g_tk.have_pin = have_root, g_tk.rebased++, g_tk.dev = g_tk.dev_y = 0;
			if (have_root)
				memcpy(g_tk.pin, root, sizeof g_tk.pin);
		}
		if (!g_tk.atk && atk && t - g_tk.t0 < TK_ATK_S)
			g_tk.atk = atk; /* his half: the first attack of the hold */
		if (atk && atk == g_tk.atk)
			g_tk.atk_t = t;
		if (mine || rec0) { /* the player's own move (the Shadow Strike, a finisher, a sync of his own) takes over */
			tk_finish(t, mine ? "the Shadow Strike or a finisher" : "a sync of his own");
			return;
		}
		const char *why = t - g_tk.t0 > TK_MAX_S ? "the longest hold" : !s[1] && atk != g_tk.atk ? "the sync and his half ended"
				: t - (g_tk.atk ? g_tk.atk_t : g_tk.t0) > TK_ATK_S ? "his half ended" : NULL;
		if (why && !g_tk.ret_t)
			g_tk.ret_t = t, g_tk.ret_why = why;
		if (g_tk.ret_t && (g_tk.nopin || !g_tk.have_pin || (g_tk.dev <= TK_BACK_CM && g_tk.dev_y <= TK_BACK_Y_CM) ||
				   t - g_tk.ret_t > TK_RET_S)) {
			tk_finish(t, g_tk.ret_why);
			return;
		}
	}
	if (!g_tk.nopin && g_tk.have_pin && g_tk.dev > 0.5f && have_root) { /* the game's move outside our delta: back */
		float k = g_tk.dev > TK_PULL_CM ? TK_PULL_CM / g_tk.dev : 1;
		g_tk.pull = (sv_vec3){ (g_tk.pin[0] - root[0]) * k, 0, (g_tk.pin[2] - root[2]) * k };
	}
	g_tk.ticks++, g_tk.held_ticks++, g_tk.after += !enemy, g_tk.ret_ticks += g_tk.ret_t > 0;
	g_tk.held |= in->why & TK_BITS, g_tk.left |= in->why & ~TK_BITS;
	in->why &= ~TK_BITS;
	if (!in->why)
		g_tk.owned++, g_tk.owned_ticks++;
}

/* lo_pm_pre, after the core's step on an owned tick: the pull back rides in our delta (not in LOCO's asked move: the
 * pin follows our own moves only) */
static void tk_pm_step(somvr_loco_out *o)
{
	if (!g_tk.active || !o->owned || (g_tk.pull.x == 0 && g_tk.pull.z == 0))
		return;
	o->delta.x += g_tk.pull.x, o->delta.z += g_tk.pull.z;
	g_tk.pulls++, g_tk.all_pulls++, g_tk.pulled_cm += hypotf(g_tk.pull.x, g_tk.pull.z);
}

/* lo_anchor_body, the frame's root for the view: during a hold, where our own moves put him (the pin), not where the
 * game's move left him before the next tick's pull back */
static void tk_anchor_root(sv_vec3 *root)
{
	if (!g_tk.on || !g_tk.active || g_tk.nopin || !g_tk.have_pin || !g_lo.owned_last || !isfinite(g_tk.pin[0] + g_tk.pin[2]))
		return;
	float d = sqrtf((root->x - g_tk.pin[0]) * (root->x - g_tk.pin[0]) + (root->y - g_tk.pin[1]) * (root->y - g_tk.pin[1]) +
			(root->z - g_tk.pin[2]) * (root->z - g_tk.pin[2]));
	g_tk.view_max = fmaxf(g_tk.view_max, isfinite(d) ? d : 0), g_tk.view_frames++;
	root->x = g_tk.pin[0], root->y = g_tk.pin[1], root->z = g_tk.pin[2];
}

/* the test words "holdtalion on|off|nopin|pin", "holdtalion health N" (headless A/B in one boot; SOMVR_HANDS_TEST, SOMVR_LOCO on): nopin =
 * the ticks ours, the game's outside move measured, not pulled back, the view on the root */
static const char *tk_word(const char *w)
{
	float hv = 0;
	if (sscanf(w, "health %f", &hv) == 1) { /* "holdtalion health N": Talion's health (a blow's damage seen above the
						 * practice's min health) */
		uintptr_t pl = g_fr.pl, pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, sc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
		uintptr_t ch = sc && vptr_of((void *)sc) == g_cbg.vt_combat ? peek_ptr(sc + C_OWNER) : 0;
		float h0 = NAN;
		if (!g_bd.test || !(hv > 0 && hv <= 10000) || !ch || !peek(ch + CH_HEALTH, &h0, sizeof h0) ||
		    !arm_poke(ch + CH_HEALTH, &hv, sizeof hv))
			return "holdtalion health (SOMVR_HANDS_TEST off, no player, or not N in 0..10000: ignored)";
		logf_("HOLD talion health %.1f -> %.1f (the word)", h0, hv);
		return "holdtalion health";
	}
	int on = !strncmp(w, "on", 2) ? 1 : !strncmp(w, "off", 3) ? 0 : !strncmp(w, "nopin", 5) ? 2 : !strncmp(w, "pin", 3) ? 3 : -1;
	if (on < 0 || !g_bd.test || !g_lo.on)
		return "holdtalion (not on|off|nopin|pin, SOMVR_HANDS_TEST off or SOMVR_LOCO off: ignored)";
	if (on >= 2) {
		g_tk.nopin = on == 2;
		logf_("CONFIG hold talion: the pull back %s (the word)", g_tk.nopin ? "off, measured only" : "on");
		return "holdtalion";
	}
	if (!on && g_tk.active)
		tk_finish(cb_now(), "the word holdtalion off");
	g_tk.on = on, g_tk.used |= on;
	logf_("CONFIG hold talion=%s (the word)", on ? "on" : "off");
	return "holdtalion";
}

static void tk_end_log(void)
{
	if (!g_tk.used)
		return;
	if (g_tk.active)
		tk_finish(cb_now(), "the game ends");
	logf_("HOLD talion END holds=%llu (%.1f s), ticks %llu, ours %llu; the root at most %.1f cm from where our own moves "
	      "put it; lines suppressed %llu", (unsigned long long)g_tk.holds, g_tk.secs, (unsigned long long)g_tk.held_ticks,
	      (unsigned long long)g_tk.owned_ticks, g_tk.dev_all, (unsigned long long)g_tk.suppressed);
}
