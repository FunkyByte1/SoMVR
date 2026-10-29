/* somvr_combat21_game.c - build 21's combat round (docs/re-xr-pipeline.md 85; handoff/HANDOFF-combat.md "build 21"),
 * the game side; included after somvr_verbs_game.c (its picks, heads and names). Three things:
 *
 * 1. SOMVR_VERBS_CUE (default on): the grab cue. When the empty, open left hand is in grab reach of an orc by the verbs'
 *    own rule (somvr_vb_reach: the head's sphere, the reach-out cone, a stealth target's back) and the game would take the
 *    grab now (its own selection, vb_pick, offers an attack of `PC_Grab_Attempt` - or of `PC_Grab_Attempt_Stealth` -
 *    for that orc: none for an orc it would refuse, while Talion's attack window is shut, ...), the orc's collar gets a
 *    faint wraith-blue glow (THREAT's world glow, the blue row: SOMVR_THK_BLUE) and the left hand one light tick as it
 *    becomes grabbable; gone when not. No prompt. The game's answer is asked from vb_frame (at most every
 *    SOMVR_VERBS_CUE_RECHECK_S per orc), the reach from vb_hands, the glow published by th_frame.
 * 2. SOMVR_STRIKE_SWORD (default hand): the Shadow Strike is a sword kill - as it starts (Talion's attack '*WraithDash*')
 *    the bow goes to its slot as usual (somvr_bow_stop, the slot's end) and the sword comes to the main hand: the game's
 *    own draw of it is the player's (the slots' take: not kept on the back, no undraw), else our draw is asked; it stays
 *    in the hand after (the player puts it back). Not while that hand holds a wall (a climb hold is never let go by us).
 *    =off: build 20c (the game's draw kept on the back: the strike's blue streak with no sword, marks 194211_6, 194517_10).
 * 3. The test words `react21 defender|berserker V` (SOMVR_HANDS_TEST): REACT's no-table chance at run time, and
 *    `react21 notify`: REACT's own FUN_029ac890 (rx_notify) on the nearest orc once it is out of its own attack - the
 *    turtle's A/B in one boot (build 21 ships SOMVR_COMBAT_REACT_DEFENDER=0: 85.2). */

/* ---- 1. the grab cue ------------------------------------------------------------------------------------------------ */
static const char *const g_gc_w[] = { "off", "on", "log" };
enum { GC_OFF = 0, GC_ON = 1, GC_LOG = 2 };
static struct {
	int mode;            /* GC_*: on = the glow and the tick, log = the lines only */
	float level;         /* SOMVR_VERBS_CUE_LEVEL: the glow's strength at full fade (0..1) */
	float size_cm;       /* SOMVR_VERBS_CUE_CM: the glow's quad (cm in the world; / the player size in the room) */
	float down_cm;       /* SOMVR_VERBS_CUE_DOWN_CM: the collar under the head point */
	float keep_cm, keep_deg; /* the shown orc keeps the cue this far past the reach's edge (no flicker) */
	float tick_amp, tick_s;  /* SOMVR_VERBS_CUE_TICK: the left hand's tick (x SOMVR_VERBS_HAPTIC) */
	float recheck_s;     /* the game's answer asked again this often for the orc in reach */
	somvr_vb_cue_cfg cfg;
	somvr_vb_cue st;
	/* vb_hands -> vb_frame: the orc in reach this frame */
	uintptr_t reach_c;
	int reach_body;
	uint64_t reach_frame;
	/* vb_frame: the game's answer for it */
	uintptr_t ask_c;
	int ask_body, ask_ok;
	double ask_t;
	char ask_atk[96], ask_sync[96], ask_node[40];
	const char *ask_why;
	uintptr_t said_c; /* the last answer's line: per orc and answer */
	int said_ok;
	/* -> th_frame: the glow */
	float pos[3], glow, dist_cm;
	uint64_t glow_frame;
	double on_t;
	_Atomic int req_probe;
	_Atomic int req_mode;   /* -1 none */
	_Atomic int req_react[2]; /* react21: the chance x 1000 (-1 none): [0] the defender, [1] the berserker */
	_Atomic int req_notify;   /* react21 notify: REACT's own FUN_029ac890 on the nearest orc (out of its attack) */
	struct { uintptr_t rec; int front; char atk[96]; } counter[8]; /* orc kinds (their character combat record) that
									 * answered our grab with a counter, and from which
									 * side: no cue for that kind from there */
	int ncounter;
	uintptr_t seen_counter_c;
	uint64_t shield_noes, counter_noes;
	double notify_until;
	uint64_t asks, yes, no, ons, ticks, glow_frames, probes, lines;
} g_gc;

static void gc_start(int *bad)
{
	memset(&g_gc, 0, sizeof g_gc);
	atomic_store(&g_gc.req_mode, -1), atomic_store(&g_gc.req_react[0], -1), atomic_store(&g_gc.req_react[1], -1);
	g_gc.mode = env_word("SOMVR_VERBS_CUE", g_gc_w, 3, GC_ON);
	g_gc.level = env_float("SOMVR_VERBS_CUE_LEVEL", 0.55f, 0.05f, 1, bad);
	g_gc.size_cm = env_float("SOMVR_VERBS_CUE_CM", 26, 5, 80, bad);
	g_gc.down_cm = env_float("SOMVR_VERBS_CUE_DOWN_CM", 12, 0, 40, bad);
	g_gc.keep_cm = 8, g_gc.keep_deg = 5;
	float tk[2] = { 0.25f, 0.025f };
	env_vec("SOMVR_VERBS_CUE_TICK", tk, 2, 0, 1, bad);
	g_gc.tick_amp = tk[0], g_gc.tick_s = tk[1] > 0.005f ? tk[1] : 0.005f;
	g_gc.recheck_s = env_float("SOMVR_VERBS_CUE_RECHECK_S", 0.1f, 0.02f, 1, bad);
	somvr_vb_cue_cfg_default(&g_gc.cfg);
	somvr_vb_cue_reset(&g_gc.st);
	if (g_vb.mode != VB_ON) {
		if (g_gc.mode)
			logf_("CONFIG verbs_cue=%s: SOMVR_VERBS is not on - no cue", g_gc_w[g_gc.mode]);
		g_gc.mode = GC_OFF;
		return;
	}
	logf_("CONFIG verbs_cue=%s (SOMVR_VERBS_CUE): the open left hand in grab reach (the verbs' rule; kept +%.0f cm / +%.0f deg) "
	      "of an orc the game's selection would grab now ('PC_Grab_Attempt', a stealth target's back "
	      "'PC_Grab_Attempt_Stealth'; asked every %.2f s): a wraith-blue glow %.0f cm at its collar (%.0f cm under the head "
	      "point; level %.2f, THREAT's blue row; fade %.2f s, grace %.2f s) and one tick in the left hand (%.2f x %.0f ms; "
	      "again for that orc after %.1f s off)%s", g_gc_w[g_gc.mode], g_gc.keep_cm, g_gc.keep_deg, g_gc.recheck_s,
	      g_gc.size_cm, g_gc.down_cm, g_gc.level, g_gc.cfg.fade_s, g_gc.cfg.grace_s, g_gc.tick_amp, g_gc.tick_s * 1e3,
	      g_gc.cfg.rearm_s, g_th.on ? "" : " - SOMVR_THREAT is off: the tick only, no glow");
}

/* the game's pick of a root node for orc o: the attack's name in *atk ('' = none, *why then) */
static uintptr_t gc_pick(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, const char *node_name, char *atk, size_t na,
			 char *sync, size_t ns, const char **why)
{
	uintptr_t node = vb_node_named(pcomb, node_name), sy = 0, a = 0;
	atk[0] = sync[0] = 0;
	*why = "";
	if (!node)
		*why = "the root node is not in the player's tree";
	else if ((a = vb_pick(pcomb, pc, o, node, &sy, why)))
		vb_name(a, atk, na), vb_name(sy, sync, ns);
	return a;
}

/* orc c's kind: its character combat record (REACT's rx_table; the Combat objects are reused by the next orc) */
static uintptr_t gc_kind(uintptr_t c)
{
	struct rx_tbl b;
	const char *why = NULL;
	uintptr_t npc = c ? peek_ptr(c + C_OWNER) : 0;
	return npc && rx_table(npc, &b, &why) >= 0 ? b.ccr : 0;
}

/* Talion in orc o's front half (1), its back half (0), unknown (-1) - its facing against the line to his root */
static int gc_front(const struct cb_orc *o)
{
	float q[4] = { 0, 0, 0, 1 };
	if (!g_vb.have_root || !peek(o->hobj + OBJ_ROT, q, sizeof q) || !vec_ok(q, 2.0f))
		return -1;
	float yaw = sv_heading(sv_qnormalize(sv_quat_of(q))), dx = g_vb.root[0] - o->pos[0], dz = g_vb.root[2] - o->pos[2];
	return fabsf(sv_wrap_pi(atan2f(dx, dz) - yaw)) < 1.5707963f;
}

/* vb_frame (after vb_heads): the game's answer for the orc in reach; the probe word's lines; the react21 word */
static void gc_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, double t)
{
	for (int k = 0; k < 2; k++) {
		int v = atomic_exchange(&g_gc.req_react[k], -1);
		if (v >= 0) {
			g_rx.cfg.chance[k ? SC_ARCH_BERSERKER : SC_ARCH_DEFENDER] = v / 1000.0f;
			logf_("REACT21 t=%.3f the %s's chance now %.3f (the test word; SOMVR_COMBAT_REACT_%s)", t,
			      k ? "berserker" : "defender", v / 1000.0, k ? "BERSERKER" : "DEFENDER");
		}
	}
	if (atomic_exchange(&g_gc.req_notify, 0))
		g_gc.notify_until = t + 4.0;
	if (g_gc.notify_until > 0 && pc && n > 0) { /* the turtle's cause on demand: the call REACT makes at a won roll */
		uintptr_t npc = peek_ptr(o[0].c + C_OWNER), own = peek_ptr(pc + C_OWNER), brain = npc ? peek_ptr(npc + RX_NPC_BRAIN) : 0;
		uintptr_t at = peek_ptr(o[0].c + RX_C_ATTACK);
		uint8_t cw[2] = { 0, 0 };
		peek(o[0].c + RX_C_CANCEL, cw, 2);
		if (own && npc && brain && peek_ptr(npc + RX_GB_HOBJ) == o[0].hobj && !sc_in_own_attack(at, cw[0], cw[1])) {
			logf_("REACT21 t=%.3f notify (the test word) orc %#lx at %.0f cm, out of its own attack:", t, (unsigned long)o[0].c,
			      o[0].dist);
			rx_notify(own, npc, brain, &o[0], t);
			g_gc.notify_until = 0;
		} else if (t > g_gc.notify_until) {
			logf_("REACT21 t=%.3f notify (the test word): not made in 4 s (the nearest orc in its own attack, or not readable)", t);
			g_gc.notify_until = 0;
		}
	}
	int m = atomic_exchange(&g_gc.req_mode, -1);
	if (m >= 0 && g_vb.mode == VB_ON)
		g_gc.mode = m, logf_("VERBS cue t=%.3f mode %s (the test word)", t, g_gc_w[m]);
	if (g_vb.mode != VB_ON || !pc || !pcomb || !g_vb.ok_tree)
		return;
	char a[96], s[96], an[96];
	const char *why;
	if (atomic_exchange(&g_gc.req_probe, 0)) { /* the data: what the game's selection offers for each near orc */
		static const char *const nodes[] = { "PC_Grab_Attempt", "PC_Grab_Attempt_Stealth", "PC_Grab_Attempt_Fail",
						     "PC_Interrogation_Into", "PC_Grab_HOG_Into", "PC_InstaKill_Sneak_Silent" };
		g_gc.probes++;
		for (int i = 0; i < n && i < 4; i++) {
			char line[900];
			int k0 = snprintf(line, sizeof line, "VERBS cue probe t=%.3f orc %#lx at %.0f cm (attack '%s', health %.1f):", t,
					  (unsigned long)o[i].c, o[i].dist, vb_name(peek_ptr(o[i].c + VB_C_ATTACK), an, sizeof an),
					  vb_health(o[i].c));
			for (size_t j = 0; j < sizeof nodes / sizeof nodes[0] && k0 < (int)sizeof line - 1; j++) {
				uintptr_t at = gc_pick(pcomb, pc, &o[i], nodes[j], a, sizeof a, s, sizeof s, &why);
				k0 += snprintf(line + k0, sizeof line - (size_t)k0, " %s -> %s%s%s%s%s;", nodes[j], at ? "'" : "none (",
					       at ? a : why, at ? "' sync '" : ")", at ? s : "", at ? "'" : "");
			}
			logf_("%s", line);
		}
	}
	if (!g_gc.mode)
		return;
	/* the orc's own answer to our grab: an attack named '*GrabAttempt*' (the shield orc's ShieldBash_GrabAttempt) is a
	 * counter - that orc gets no cue from that side again */
	if (g_vb.hand.phase != SOMVR_VBH_GRAB)
		g_gc.seen_counter_c = 0; /* (one line per grab) */
	if (g_vb.hand.phase == SOMVR_VBH_GRAB && g_vb.held_c && g_vb.held_c != g_gc.seen_counter_c) {
		vb_name(peek_ptr(g_vb.held_c + VB_C_ATTACK), an, sizeof an);
		const struct cb_orc *hc = NULL;
		for (int i = 0; i < n && !hc; i++)
			if (o[i].c == g_vb.held_c)
				hc = &o[i];
		uintptr_t kind = strstr(an, "GrabAttempt") && hc ? gc_kind(g_vb.held_c) : 0;
		if (kind) {
			int k = g_gc.ncounter < 8 ? g_gc.ncounter++ : 7;
			g_gc.counter[k].rec = kind, g_gc.counter[k].front = gc_front(hc) != 0;
			snprintf(g_gc.counter[k].atk, sizeof g_gc.counter[k].atk, "%s", an);
			g_gc.seen_counter_c = g_vb.held_c;
			char kn[96];
			logf_("VERBS cue t=%.3f orc %#lx ('%s') answered the grab with '%s' (a counter) from its %s half: no cue for its "
			      "kind from there again", t, (unsigned long)g_vb.held_c, rx_rec_name(kind, kn, sizeof kn), an,
			      g_gc.counter[k].front ? "front" : "back");
		}
	}
	uintptr_t c = g_fr.frame - g_gc.reach_frame <= 2 ? g_gc.reach_c : 0;
	if (!c || (c == g_gc.ask_c && g_gc.reach_body == g_gc.ask_body && t - g_gc.ask_t < g_gc.recheck_s))
		return;
	const struct cb_orc *oc = NULL;
	for (int i = 0; i < n && !oc; i++)
		if (o[i].c == c)
			oc = &o[i];
	if (!oc)
		return;
	/* the squeeze presses the grab's command (123): the game takes whichever of its grab nodes passes - the plain one,
	 * or behind an unaware orc the stealth grab; a node whose pick is a fail move ('*_Fail') is no grab */
	const char *nn = "PC_Grab_Attempt";
	int front = gc_front(oc), countered = -1;
	float shield = cb_shield_hp(c);
	uintptr_t kind = g_gc.ncounter ? gc_kind(c) : 0;
	for (int k = 0; k < g_gc.ncounter && kind; k++)
		if (g_gc.counter[k].rec == kind && g_gc.counter[k].front == (front != 0))
			countered = k;
	uintptr_t at = 0;
	a[0] = s[0] = 0;
	if (shield > 0 && front == 1) {
		/* a shield orc with its shield whole bashes a grab from its front (run 20261028-214243: the game's pick offered
		 * 'PC_Grab_Attempt_F', the orc answered 'AI_Defender_ShieldBash_GrabAttempt', Talion knocked down) */
		why = "its shield is up: it bashes a grab from the front", g_gc.shield_noes++;
	} else if (countered >= 0) {
		why = "its kind answered a grab from this side with a counter", g_gc.counter_noes++;
	} else if (!(at = gc_pick(pcomb, pc, oc, nn, a, sizeof a, s, sizeof s, &why)) || strstr(a, "Fail")) {
		const char *w0 = why;
		nn = "PC_Grab_Attempt_Stealth";
		at = gc_pick(pcomb, pc, oc, nn, a, sizeof a, s, sizeof s, &why);
		if (!at)
			why = w0, nn = "PC_Grab_Attempt";
	}
	int ok = at && !strstr(a, "Fail") && !(shield > 0 && front == 1) && countered < 0;
	g_gc.asks++, g_gc.yes += ok, g_gc.no += !ok;
	g_gc.ask_c = c, g_gc.ask_body = g_gc.reach_body, g_gc.ask_ok = ok, g_gc.ask_t = t, g_gc.ask_why = ok ? "" : at ? "a fail move" : why;
	snprintf(g_gc.ask_atk, sizeof g_gc.ask_atk, "%s", a), snprintf(g_gc.ask_sync, sizeof g_gc.ask_sync, "%s", s);
	snprintf(g_gc.ask_node, sizeof g_gc.ask_node, "%s", nn);
	if ((c != g_gc.said_c || ok != g_gc.said_ok) && g_gc.lines++ < 400 && vb_log_ok())
		logf_("VERBS cue t=%.3f orc %#lx in reach (%s, %.0f cm from Talion; health %.1f, attack '%s'): the game %s '%s'%s%s%s%s",
		      t, (unsigned long)c, g_gc.reach_body ? "its back" : "its head", oc->dist, vb_health(c),
		      vb_name(peek_ptr(c + VB_C_ATTACK), an, sizeof an), ok ? "would grab it:" : "would not grab it:", nn,
		      ok ? " -> '" : " -> ", ok ? a : at ? a : g_gc.ask_why, ok ? "'" : "", ok && s[0] && strcmp(s, "-") ? " (a sync)" : "");
	g_gc.said_c = c, g_gc.said_ok = ok;
}

/* vb_hands (after the hand's step): the reach, the cue's state, the tick, the glow's place */
static void gc_hands(const somvr_vb_hand_in *in, double t)
{
	if (!g_gc.mode || g_vb.mode != VB_ON)
		return;
	int keep = -1;
	for (int k = 0; k < in->norc && k < g_vb.nheads; k++)
		if (g_gc.st.on && g_vb.head_c[k] == (uintptr_t)g_gc.st.id)
			keep = k;
	int bb = 0, i = somvr_vb_reach(&g_vb.cfg, &g_vb.hand, in, keep, g_gc.keep_cm, g_gc.keep_deg, &bb);
	if (t - g_vb.hand.t_end < g_vb.cfg.cool_s)
		i = -1; /* the squeeze takes nothing in the cooldown after a hold */
	uintptr_t c = i >= 0 && i < g_vb.nheads ? g_vb.head_c[i] : 0;
	g_gc.reach_c = c, g_gc.reach_body = bb, g_gc.reach_frame = g_fr.frame;
	int ok = c && g_gc.ask_c == c && g_gc.ask_body == bb && g_gc.ask_ok && t - g_gc.ask_t <= 0.3;
	int was = g_gc.st.on;
	uintptr_t wid = (uintptr_t)g_gc.st.id;
	somvr_vb_cue_out o;
	somvr_vb_cue_step(&g_gc.st, &g_gc.cfg, t, ok ? (uint64_t)c : 0, &o);
	if (o.tick) {
		g_gc.ticks++;
		if (g_gc.mode == GC_ON && g_vb.haptic > 0 && g_gc.tick_amp > 0)
			in_haptic_event(SOMVR_HAND_L, g_gc.tick_amp * g_vb.haptic, g_gc.tick_s, SOMVR_HAP_COMBAT);
	}
	g_gc.glow = 0;
	for (int k = 0; o.id && k < g_vb.nheads; k++)
		if (g_vb.head_c[k] == (uintptr_t)o.id) {
			g_gc.pos[0] = g_vb.heads[k][0], g_gc.pos[1] = g_vb.heads[k][1] - g_gc.down_cm, g_gc.pos[2] = g_vb.heads[k][2];
			g_gc.glow = o.level * g_gc.level, g_gc.glow_frame = g_fr.frame;
			g_gc.dist_cm = dist3(g_gc.pos, in->eye);
		}
	if (o.changed) {
		if (o.on)
			g_gc.ons++, g_gc.on_t = t;
		if (g_gc.lines++ < 400 && vb_log_ok())
			logf_("VERBS cue t=%.3f %s orc %#lx%s%s%s (the palm %.0f cm from its head point)%s", t, o.on ? "on:" : "off:",
			      (unsigned long)(o.on ? (uintptr_t)o.id : wid), o.on ? (g_gc.reach_body ? " by its back" : " by its head") : "",
			      o.on ? ", the game's '" : "", o.on ? g_gc.ask_atk : "",
			      i >= 0 ? dist3(in->palm, in->head[i]) : -1.0f,
			      o.on ? (o.tick ? "': the glow at its collar, a tick" : "': the glow (no tick: back within the re-arm)")
				   : was ? (c ? (g_gc.ask_c == c && !g_gc.ask_ok ? " - the game would not grab it now" : " - another orc")
					      : " - out of reach (or the hand closed, busy)") : "");
	}
	(void)wid;
}

/* th_frame: the cue's glow among the frame's (the blue row) */
static void gc_glow_add(struct th_glow *gl, int *ng)
{
	if (g_gc.mode != GC_ON || !(g_gc.glow > 0.01f) || g_fr.frame - g_gc.glow_frame > 2 || !vec_ok(g_gc.pos, 1e7f))
		return;
	th_glow_add(gl, ng, g_gc.pos, g_gc.glow, g_gc.dist_cm, SOMVR_THK_MELEE | SOMVR_THK_BLUE);
	g_gc.glow_frames++;
}
/* th_frame: the cue glow's quad (metres in the room) */
static float gc_glow_size_m(void) { return g_gc.size_cm * 0.01f / somvr_rig_scale(&g_rig); }

/* ---- 2. the Shadow Strike's sword ------------------------------------------------------------------------------------ */
static struct {
	int on;              /* SOMVR_STRIKE_SWORD=hand (1) | off */
	float take_s;        /* the take lasts this long from the strike's start */
	uint64_t frame;      /* ssw_detect's last frame */
	uintptr_t atk;       /* Talion's attack record last read */
	int in_strike, bow_put;
	double start_t, until;
	char name[96];
	uint64_t strikes, bow_puts, takes, climbs, frames, took;
	_Atomic int req_on;  /* the test word: -1 none */
} g_ssw;

static void ssw_start(int *bad)
{
	memset(&g_ssw, 0, sizeof g_ssw);
	atomic_store(&g_ssw.req_on, -1);
	g_ssw.on = env_word("SOMVR_STRIKE_SWORD", (const char *const[]){ "off", "hand" }, 2, 1);
	g_ssw.take_s = env_float("SOMVR_STRIKE_SWORD_S", 1.5f, 0.2f, 5, bad);
	if (g_ssw.on && !g_sl.on)
		g_ssw.on = 0;
	if (g_ssw.on)
		logf_("CONFIG strike_sword=hand (SOMVR_STRIKE_SWORD): the Shadow Strike (Talion's attack '*WraithDash*') takes the "
		      "sword into the main hand as it starts - the bow to its slot, the game's draw the player's (not kept on the "
		      "back), else our draw asked - for its first %.1f s; it stays after (not while that hand holds a wall)",
		      (double)g_ssw.take_s);
	else
		logf_("CONFIG strike_sword=off (SOMVR_STRIKE_SWORD): the strike's draw follows the slots' rules (build 20c: kept on "
		      "the back while the player keeps it away)");
}

/* once a frame (bo_frame, sl_frame): Talion's attack; a Shadow Strike's start */
static void ssw_detect(double t)
{
	int r = atomic_exchange(&g_ssw.req_on, -1);
	if (r >= 0 && g_sl.on)
		g_ssw.on = r, logf_("STRIKE sword t=%.3f %s (the test word)", t, r ? "hand" : "off (build 20c)");
	if (!g_ssw.on || g_ssw.frame == g_fr.frame)
		return;
	g_ssw.frame = g_fr.frame;
	uintptr_t pl = g_fr.pl, pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (!pc || vptr_of((void *)pc) != g_cbg.vt_combat)
		return;
	uintptr_t at = peek_ptr(pc + VB_C_ATTACK);
	if (at == g_ssw.atk)
		return;
	g_ssw.atk = at;
	char nm[96];
	vb_name(at, nm, sizeof nm);
	int st = at && strstr(nm, "WraithDash") != NULL;
	if (st && !g_ssw.in_strike && t - g_ssw.start_t > 3.0) {
		g_ssw.strikes++, g_ssw.start_t = t, g_ssw.until = t + g_ssw.take_s;
		g_ssw.bow_put = bo_out();
		snprintf(g_ssw.name, sizeof g_ssw.name, "%s", nm);
		int climb = cl_holds(somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD));
		g_ssw.climbs += climb;
		logf_("STRIKE sword t=%.3f the Shadow Strike ('%s'): %s%s", t, nm,
		      climb ? "the sword hand holds a wall - the sword stays where it is (a hold is never let go by us)"
			    : "the sword to the main hand", g_ssw.bow_put && !climb ? "; the bow to its slot" : "");
		if (climb)
			g_ssw.until = 0, g_ssw.bow_put = 0;
	}
	g_ssw.in_strike = st;
}

/* bo_frame (after its step): 1 once = put the bow away for the strike (its slot's end) */
static int ssw_bow_put(double t)
{
	ssw_detect(t);
	if (!g_ssw.on || !g_ssw.bow_put)
		return 0;
	g_ssw.bow_put = 0, g_ssw.bow_puts++;
	return 1;
}

/* sl_frame: the strike's take now (1 in its first take_s; never while the sword hand holds a wall) */
static int ssw_take(double t)
{
	ssw_detect(t);
	if (!g_ssw.on || !(t < g_ssw.until))
		return 0;
	if (cl_holds(somvr_slots_hand(&g_sl.cfg, SOMVR_SLOT_SWORD))) {
		g_ssw.until = 0, g_ssw.climbs++;
		return 0;
	}
	g_ssw.frames++;
	return 1;
}

static void ssw_took(double t, const char *how)
{
	if (g_ssw.took == g_ssw.strikes)
		return; /* once a strike */
	g_ssw.took = g_ssw.strikes, g_ssw.takes++;
	logf_("STRIKE sword t=%.3f %.2f s after the strike's start: %s", t, t - g_ssw.start_t, how);
}

/* ---- the words, the end -------------------------------------------------------------------------------------------- */
/* "cue on|off|log|probe|status" (probe: SOMVR_HANDS_TEST) */
static const char *gc_word(const char *w)
{
	while (*w == ' ')
		w++;
	for (int m = 0; m < 3; m++)
		if (!strcmp(w, g_gc_w[m])) {
			atomic_store(&g_gc.req_mode, m);
			return m == GC_ON ? "cue on" : m == GC_LOG ? "cue log" : "cue off";
		}
	if (!strncmp(w, "probe", 5)) {
		if (!g_vb.test)
			return "cue probe (SOMVR_HANDS_TEST is off: ignored)";
		atomic_store(&g_gc.req_probe, 1);
		return "cue probe";
	}
	if (!strncmp(w, "status", 6)) {
		logf_("VERBS cue status: mode %s, on %d orc %#llx level %.2f; asks %llu (yes %llu no %llu), ons %llu, ticks %llu, glow "
		      "frames %llu; the last answer: orc %#lx %s '%s' (%s)", g_gc_w[g_gc.mode], g_gc.st.on,
		      (unsigned long long)g_gc.st.id, g_gc.st.level, (unsigned long long)g_gc.asks, (unsigned long long)g_gc.yes,
		      (unsigned long long)g_gc.no, (unsigned long long)g_gc.ons, (unsigned long long)g_gc.ticks,
		      (unsigned long long)g_gc.glow_frames, (unsigned long)g_gc.ask_c, g_gc.ask_ok ? "yes" : "no", g_gc.ask_atk,
		      g_gc.ask_why ? g_gc.ask_why : "");
		return "cue status";
	}
	return "cue (not on|off|log|probe|status: ignored)";
}

/* "ssword hand|off" (SOMVR_HANDS_TEST: the A/B in one boot); "react21 defender|berserker V" (SOMVR_HANDS_TEST) */
static const char *ssw_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (!g_vb.test)
		return "ssword (SOMVR_HANDS_TEST is off: ignored)";
	if (!strncmp(w, "hand", 4) || !strncmp(w, "off", 3)) {
		atomic_store(&g_ssw.req_on, w[0] == 'h');
		return w[0] == 'h' ? "ssword hand" : "ssword off";
	}
	return "ssword (not hand|off: ignored)";
}
static const char *rx21_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (!g_vb.test)
		return "react21 (SOMVR_HANDS_TEST is off: ignored)";
	if (!strncmp(w, "notify", 6)) {
		atomic_store(&g_gc.req_notify, 1);
		return "react21 notify";
	}
	int k = !strncmp(w, "defender ", 9) ? 0 : !strncmp(w, "berserker ", 10) ? 1 : -1;
	if (k < 0)
		return "react21 (not defender|berserker V|notify: ignored)";
	float v = strtof(w + (k ? 10 : 9), NULL);
	if (!(v >= 0 && v <= 1))
		return "react21 (V not 0..1: ignored)";
	atomic_store(&g_gc.req_react[k], (int)lrintf(v * 1000));
	return k ? "react21 berserker" : "react21 defender";
}

static void c21_end_log(void)
{
	if (g_gc.mode || g_gc.asks)
		logf_("VERBS cue END: mode %s; the game asked %llu (would grab %llu, would not %llu), cues on %llu, ticks %llu, glow "
		      "frames %llu, probes %llu; no for a shield's front %llu, for a counter seen %llu (%d orcs)", g_gc_w[g_gc.mode],
		      (unsigned long long)g_gc.asks, (unsigned long long)g_gc.yes, (unsigned long long)g_gc.no,
		      (unsigned long long)g_gc.ons, (unsigned long long)g_gc.ticks, (unsigned long long)g_gc.glow_frames,
		      (unsigned long long)g_gc.probes, (unsigned long long)g_gc.shield_noes, (unsigned long long)g_gc.counter_noes,
		      g_gc.ncounter);
	logf_("STRIKE sword END: %s; strikes %llu, the bow to its slot %llu, the sword taken %llu, take frames %llu, a wall in "
	      "the hand %llu", g_ssw.on ? "hand" : "off", (unsigned long long)g_ssw.strikes, (unsigned long long)g_ssw.bow_puts,
	      (unsigned long long)g_ssw.takes, (unsigned long long)g_ssw.frames, (unsigned long long)g_ssw.climbs);
}
