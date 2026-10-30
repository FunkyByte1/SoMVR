/* somvr_parry_game.c - build 18 (the parry18 helper, docs/re-xr-pipeline.md 65): the parry pays off, the shove opens a
 * guard. Included last by somvr.c (it uses DEFENCE's recoil call, COMBAT's records and shield, THREAT's sound object,
 * EXECUTE's counter).
 *
 *   SOMVR_PARRY=off|on (default off) a block of ours that meets the orc's blow (DEFENCE's contact judge) at most
 *                     SOMVR_PARRY_WINDOW_MS before its impact (up to the judge's own +80 ms after it) is a parry: on top
 *                     of the block's own recoil and clank, our clang (SOMVR_PARRY_SOUND at the blades), one pulse in the
 *                     free hand, +1 on the game's hit streak (SOMVR_PARRY_STREAK) and an opening of the orc for
 *                     SOMVR_PARRY_OPEN_MS: no block of its own stops our hits then (our REACT rolls skip it, and each hit
 *                     is dealt with Talion's attack type "cannot block", Combat+0x55b = 3, for the call: the game's
 *                     damage context then skips its IsBlock block), and a committed cut (or a stab) is the riposte: the
 *                     game's High strike whatever the streak, never glanced off a raised shield; into the shield it
 *                     breaks it (SOMVR_PARRY_SHIELD=break: the shield's HP 1 for the call, so the game's own blocked
 *                     branch takes the rest and plays its own break).
 *   SOMVR_PARRY_WINDOW_MS=0..250 (default 250 = the whole contact window [impact - 250, + 80 ms]: every block that meets
 *                     the blow is a parry; a smaller number = a stricter timing, earlier contacts stay plain blocks)
 *   SOMVR_PARRY_OPEN_MS=200..3000 (default 1000)
 *   SOMVR_PARRY_STREAK=on|off (default on)  SOMVR_PARRY_SHIELD=break|off (default break)
 *   SOMVR_PARRY_SOUND=EVENT|off (default cbt_imp_surface_player: the sword met a hard surface; posted at the contact
 *                     from THREAT's Wwise object)
 *   SOMVR_SHOVE=off|on (default off) the free left hand pushed into a raised shield (its sphere, SOMVR_COMBAT_SHIELD_CM
 *                     + 8 cm, or at the chest within SOMVR_SHOVE_REACH_CM, default 25, of a shield orc's body: the shield
 *                     held before it) or into a blocking orc's body at SOMVR_SHOVE_MPS (default 1.5 m/s, relative to Talion's
 *                     root, toward the orc) shoves its guard aside: the orc's recoil from Talion (FUN_02810af0) by the
 *                     first of SOMVR_SHOVE_RECOIL's types that takes (default Block_Breaker,Medium,Light; Medium and
 *                     Light get the hand's direction), a thump in the left hand, SOMVR_SHOVE_SOUND (default
 *                     cbt_imp_atk_biped) and the same opening as a parry (its riposte breaks the shield too). Not with
 *                     the bow out, a climb hold, the grab, both hands on the sword; once per orc in 1.5 s.
 *   SOMVR_PARRY_MARKS=N (test only) a SOMVR_MARK (the eyes) at the next N parries, shoves and shield breaks.
 * Test words (SOMVR_HANDS_TEST): "parry open" (the nearest orc opened as by a parry), "parry status", "parry auto MS"
 * (build 19: MS after each parry's opening the test right hand swings out to the right in 120 ms, then the edge cut
 * across in 250 ms - a player's counter-cut without the driver's log latency; 0 = off).
 */

/* PARRY_SITES_BEGIN (read only, never patched). PY_STREAK FUN_02726500(hc, bool all): the hit counter's pending
 * increments, +0xcf += all ? +0xca : 1 (re-xr-pipeline.md 30.23's "pending increments FUN_026d4940 / FUN_02726500 add
 * at a hit"; FUN_027259c0 applies them: the streak, the charge, the timer's restart). Order = PYS_* */
static const struct site_spec g_py_sites[] = {
SITE("PY_STREAK", K_ENTRY, 0x2726500, 0x2726500, "0f b6 97 cf 00 00 00 8d 42 01 40 84 f6 74 09 0f b6 87 ca 00 00 00 01 d0 88 87 cf 00 00 00 c3", 0x2726500, "0f b6 97 cf 00 00 00 8d 42 01 40 84 f6 74 09"),
};
/* PARRY_SITES_END */
enum { PYS_STREAK, PYS_N };
_Static_assert(sizeof g_py_sites / sizeof g_py_sites[0] == PYS_N, "parry sites");
static const struct site_spec *g_pys = g_py_sites; /* selftest: stand-ins */
typedef void (*py_streak_fn)(uintptr_t hc, int all);
#define PY_HC 0x1f0       /* Combat: the hit counter object (the player's streak; re-xr-pipeline.md 30.23) */
#define PY_N 8
#define PY_HIST 16
#define PY_SHOVE_COOL_S 1.5
#define PY_HAND_CM 8.0f
#define PY_SHOVE_MAX_CMS 1000.0f /* 10 m/s */
#define PY_TYPE_THROUGH 3 /* AttackType "cannot block" (re-combat.md 3.3): any nonzero +0x55b skips the target's block */

static struct {
	int on, shove, streak, brk, ok_streak, used, marks;
	float window_s, open_s, shove_mps, shove_reach, dodge_s, reel_cm;
	int dodge, shield_mode; /* build 19: SOMVR_PARRY_DODGE; SOMVR_PARRY_SHIELD (PY_SH_*) */
	char sound[64], shove_sound[64], recoil[96];
	struct py_open { uintptr_t c, hobj; double t0, until, shoved; int why, hits, ripostes; } o[PY_N];
	/* the left palm (world cm) and its history in the tracking space (in_frame: the hand's own motion, mapped to the
	 * world with the base of the frame it is judged in - a turn, a walk or a game move of Talion is not a push) */
	int palm_ok;
	float palm[3];
	struct { double t; sv_vec3 p; } h[PY_HIST];
	int nh, hpos;
	double refused_next;
	uintptr_t nearest; /* the nearest orc of the last combat frame (the word "parry open") */
	int auto_ms, auto_step; /* build 19 (test): "parry auto MS" - MS after a parry's opening the test right hand cuts */
	double auto_t;
	uint64_t parries, early, opens, streaks, streak_fail, sounds, sound_fail, ripostes, breaks, break_miss, through,
		rx_skipped, shoves, shove_took, shove_refused, pushes_refused, dodge_opens, reels_short, lines, suppressed;
	float vault_s;       /* build 27: SOMVR_PARRY_VAULT_MS - the orc the wraith step's vault went over is open this long */
	uint64_t vault_opens;
} g_py;

static const char *const g_py_why[5] = { "-", "a parry", "a shove", "a dodge", "a vault over it" };
enum { PY_SH_OFF, PY_SH_GAME, PY_SH_SPHERE };

static int py_log_ok(void)
{
	if (g_py.lines < 600)
		return ++g_py.lines;
	g_py.suppressed++;
	return 0;
}

static void py_config(void)
{
	int bad = 0;
	memset(&g_py, 0, sizeof g_py);
	g_py.on = env_word("SOMVR_PARRY", (const char *const[]){ "off", "on" }, 2, 0);
	g_py.shove = env_word("SOMVR_SHOVE", (const char *const[]){ "off", "on" }, 2, 0);
	g_py.window_s = env_float("SOMVR_PARRY_WINDOW_MS", 250, 0, 250, &bad) * 1e-3f;
	g_py.open_s = env_float("SOMVR_PARRY_OPEN_MS", 1000, 200, 3000, &bad) * 1e-3f;
	g_py.streak = env_word("SOMVR_PARRY_STREAK", (const char *const[]){ "off", "on" }, 2, 1);
	/* break = the game's own direction rule decides "into the shield" for the riposte (its shield block, FUN_028adf30);
	 * sphere = build 18's: our sweep met SOMVR_COMBAT_SHIELD_CM about the shield model's origin (its forearm) first */
	g_py.shield_mode = env_word("SOMVR_PARRY_SHIELD", (const char *const[]){ "off", "break", "sphere" }, 3, PY_SH_GAME);
	g_py.brk = g_py.shield_mode != PY_SH_OFF;
	g_py.dodge = env_word("SOMVR_PARRY_DODGE", (const char *const[]){ "off", "on" }, 2, 0);
	g_py.dodge_s = env_float("SOMVR_PARRY_DODGE_MS", 1500, 200, 3000, &bad) * 1e-3f;
	g_py.vault_s = env_float("SOMVR_PARRY_VAULT_MS", 0, 0, 3000, &bad) * 1e-3f; /* build 27 (re-xr-pipeline.md 108.5) */
	g_py.reel_cm = env_float("SOMVR_PARRY_REEL_CM", 0, 0, 200, &bad);
	g_py.shove_mps = env_float("SOMVR_SHOVE_MPS", 1.5f, 0.5f, 6, &bad);
	g_py.shove_reach = env_float("SOMVR_SHOVE_REACH_CM", 25, 0, 60, &bad);
	g_py.marks = (int)env_float("SOMVR_PARRY_MARKS", 0, 0, 20, &bad);
	const char *s = getenv("SOMVR_PARRY_SOUND"), *ss = getenv("SOMVR_SHOVE_SOUND"), *rc = getenv("SOMVR_SHOVE_RECOIL");
	snprintf(g_py.sound, sizeof g_py.sound, "%s", s && *s ? (strcmp(s, "off") ? s : "") : "cbt_imp_surface_player");
	snprintf(g_py.shove_sound, sizeof g_py.shove_sound, "%s", ss && *ss ? (strcmp(ss, "off") ? ss : "") : "cbt_imp_atk_biped");
	snprintf(g_py.recoil, sizeof g_py.recoil, "%s", rc && *rc ? rc : "Block_Breaker,Medium,Light");
	if (!g_py.on && !g_py.shove)
		return;
	if (g_py.dodge || g_py.reel_cm > 0)
		logf_("CONFIG parry (build 19): a dodged blow opens its attacker %s (SOMVR_PARRY_DODGE, %.0f ms); a parry's reel "
		      "at most %.0f cm (SOMVR_PARRY_REEL_CM; 0 = SOMVR_COMBAT_STAGGER_CM's); the riposte into a raised shield by %s",
		      g_py.dodge ? "on" : "off", g_py.dodge_s * 1e3f, g_py.reel_cm, g_py.shield_mode == PY_SH_GAME
		      ? "the game's own direction rule" : g_py.shield_mode == PY_SH_SPHERE ? "our sphere at its origin" : "nothing");
	if (g_py.on && g_py.vault_s > 0)
		logf_("CONFIG parry vault (build 27): the orc the wraith step's vault went over is open %.0f ms (its block skipped, a "
		      "committed cut the riposte; SOMVR_PARRY_VAULT_MS)", g_py.vault_s * 1e3f);
	char why[160];
	g_py.ok_streak = check_site(&g_pys[PYS_STREAK], why, sizeof why) == 0;
	if (!g_py.ok_streak && g_py.streak)
		logf_("WARN PARRY site PY_STREAK at %#lx did not match (%s): a parry adds nothing to the streak",
		      (unsigned long)g_pys[PYS_STREAK].addr, why);
	logf_("CONFIG parry=%s shove=%s: a contact block at most %.0f ms before the blow's impact is a parry (the clang '%s', "
	      "a pulse in the free hand, the streak +1 %s); a parry or a shove opens the orc for %.0f ms: its own block stops "
	      "no hit of ours then (REACT skips it; Talion's attack type %d for the call), a committed cut or a stab is the "
	      "game's High strike%s; the shove: the left hand into a raised shield (or %.0f cm before a shield orc's body) or a "
	      "blocking orc at %.1f m/s, its recoil "
	      "'%s', the sound '%s'", g_py.on ? "on" : "off", g_py.shove ? "on" : "off", g_py.window_s * 1e3f,
	      g_py.sound[0] ? g_py.sound : "off", !g_py.streak ? "(off)" : g_py.ok_streak ? "(PY_STREAK ok)" : "(no site)",
	      g_py.open_s * 1e3f, PY_TYPE_THROUGH, g_py.brk ? " and breaks a raised shield it meets" : "", g_py.shove_reach,
	      g_py.shove_mps,
	      g_py.recoil, g_py.shove_sound[0] ? g_py.shove_sound : "off");
}

static struct py_open *py_find(uintptr_t c)
{
	for (int i = 0; c && i < PY_N; i++)
		if (g_py.o[i].c == c)
			return &g_py.o[i];
	return NULL;
}
static struct py_open *py_slot(uintptr_t c, double t)
{
	struct py_open *p = py_find(c);
	for (int i = 0; !p && i < PY_N; i++)
		if (!g_py.o[i].c || (t > g_py.o[i].until + 2 && t > g_py.o[i].shoved + PY_SHOVE_COOL_S))
			p = &g_py.o[i], memset(p, 0, sizeof *p);
	if (p)
		p->c = c;
	return p;
}

/* the orc is open now (a parry's or a shove's window) */
static int py_open_now(uintptr_t c, double t)
{
	struct py_open *p = (g_py.on || g_py.shove) ? py_find(c) : NULL;
	return p && t <= p->until;
}

/* the player's server Combat (NULL-safe) */
static uintptr_t py_pc(void)
{
	uintptr_t pl = g_fr.pl, pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	return pc && vptr_of((void *)pc) == g_cbg.vt_combat && peek_ptr(pc + C_PLAYER) ? pc : 0;
}

static void py_sound(const char *name, const float at[3], const float head[3], double t, const char *what)
{
	if (!name[0] || !g_th.ok_snd)
		return;
	uint32_t id = th_snd_post(name, at, head);
	g_py.sounds += id != 0, g_py.sound_fail += id == 0;
	if (py_log_ok())
		logf_("PARRY sound '%s' (%s) at %.1f,%.1f,%.1f t=%.3f: %s %u", name, what, at[0], at[1], at[2], t,
		      id ? "playing id" : "PostEvent gave no playing id (not in a loaded bank?)", id);
}

/* the opening (a parry's or a shove's) */
static void py_open_for(uintptr_t c, int why, const float at[3], double t, float dur)
{
	struct py_open *p = py_slot(c, t);
	if (!p)
		return;
	struct cb_orc o;
	p->hobj = cb_orc_get(c, &o) ? o.hobj : 0;
	p->t0 = t, p->until = t + dur, p->why = why, p->hits = p->ripostes = 0;
	g_py.opens++;
	if (g_py.auto_ms > 0 && why == 1) /* the test word "parry auto MS": the counter-cut MS from now */
		g_py.auto_t = t + g_py.auto_ms * 1e-3, g_py.auto_step = 1;
	char an[96];
	if (py_log_ok())
		logf_("PARRY open orc=%#lx hobj=%#lx t=%.3f by %s for %.0f ms (its attack now '%s', health %.1f, shield %.1f) at "
		      "%.1f,%.1f,%.1f", (unsigned long)c, (unsigned long)p->hobj, t, g_py_why[why], dur * 1e3f,
		      rx_rec_name(peek_ptr(c + RX_C_ATTACK), an, sizeof an), cb_health(c), cb_shield_hp(c), at[0], at[1], at[2]);
	if (g_py.marks > 0) {
		g_py.marks--;
		ss_mark("the opening (SOMVR_PARRY_MARKS)", g_py_why[why], t);
	}
}

static void py_open(uintptr_t c, int why, const float at[3], double t) { py_open_for(c, why, at, t, g_py.open_s); }

/* SOMVR_PARRY_REEL_CM: the parried orc's reel (stagger17's, started by the clank) eased under a shorter cap */
static void py_reel_short(uintptr_t c)
{
	if (!(g_py.reel_cm > 0) || !(g_rl.cap > 0))
		return;
	for (int i = 0; i < RL_N; i++)
		if (g_rl.e[i].c == c && !g_rl.e[i].t_end && g_rl.e[i].cap > g_py.reel_cm)
			g_rl.e[i].cap = g_py.reel_cm, g_py.reels_short++;
}

/* build 19 (SOMVR_PARRY_DODGE): DEFENCE judged a blow dodged (the head out of it, or the A dodge): its attacker open */
static void py_dodge(uintptr_t c, uintptr_t attack, int applied, float dash_ms, double t)
{
	if (!g_py.on || !g_py.dodge || !applied || !c || cb_dead(c))
		return;
	struct cb_orc o;
	float at[3] = { 0, 0, 0 };
	if (cb_orc_get(c, &o))
		memcpy(at, o.pos, sizeof at);
	char an[96];
	g_py.dodge_opens++;
	if (py_log_ok())
		logf_("PARRY dodge orc=%#lx attack '%s' t=%.3f: dodged (%s) -> its opening", (unsigned long)c,
		      rx_rec_name(attack, an, sizeof an), t, dash_ms >= 0 ? "the A dodge" : "the head out of the blow");
	py_open_for(c, 3, at, t, g_py.dodge_s);
}

/* build 27 (SOMVR_PARRY_VAULT_MS; re-xr-pipeline.md 108.5): the wraith step's vault landed behind orc c (ws_arrival):
 * its opening - the player's half turn and a committed cut inside it is the riposte (through its block; into a shield
 * it has turned back, the break), as after a dodged blow (run 021916: the defender had its shield round in 1.4-2 s) */
static void py_vault(uintptr_t c, double t)
{
	if (!g_py.on || !(g_py.vault_s > 0) || !c || cb_dead(c))
		return;
	struct cb_orc o;
	float at[3] = { 0, 0, 0 };
	if (cb_orc_get(c, &o))
		memcpy(at, o.pos, sizeof at);
	g_py.vault_opens++;
	py_open_for(c, 4, at, t, g_py.vault_s);
}

/* DEFENCE's contact judge accepted a block (df_contact, the recoil and the clank done): a parry if in the window */
static void py_parry(uintptr_t c, uintptr_t attack, const somvr_df_in *in, const float point[3], double t)
{
	if (!g_py.on || !c || cb_dead(c))
		return;
	if (in->impact >= 0 && in->ts < in->impact - g_py.window_s - 1e-4f) { /* a block early in the judge's window */
		g_py.early++;
		if (py_log_ok())
			logf_("PARRY none orc=%#lx: the blades met %.0f ms before the impact (SOMVR_PARRY_WINDOW_MS %.0f): a block",
			      (unsigned long)c, (in->impact - in->ts) * 1e3f, g_py.window_s * 1e3f);
		return;
	}
	g_py.parries++;
	int16_t s0 = -1;
	uintptr_t pc = py_pc(), hc = pc ? peek_ptr(pc + PY_HC) : 0;
	uint8_t pend0 = 0, pend1 = 0;
	if (g_py.streak && g_py.ok_streak && hc) { /* the game's own increment: applied at its next tick like a hit's */
		peek(hc + 0xc4, &s0, 2), peek(hc + 0xcf, &pend0, 1);
		((py_streak_fn)g_pys[PYS_STREAK].target)(hc, 0);
		peek(hc + 0xcf, &pend1, 1);
		g_py.streaks += pend1 != pend0, g_py.streak_fail += pend1 == pend0;
	} else if (g_py.streak)
		g_py.streak_fail++;
	in_haptic_event(SOMVR_HAND_L, 0.9f, 0.09f, SOMVR_HAP_COMBAT); /* the parry's own pulse: the free hand, once */
	py_sound(g_py.sound, point, in->head, t, "the parry's clang");
	char an[96];
	if (py_log_ok())
		logf_("PARRY parry orc=%#lx attack '%s' t=%.3f: the blades met %.0f ms %s its impact (the window %.0f ms before "
		      "it) -> the streak %d, pending %u -> %u (the game applies it), the free hand's pulse", (unsigned long)c,
		      rx_rec_name(attack, an, sizeof an), t, fabsf(in->impact - in->ts) * 1e3f,
		      in->ts <= in->impact ? "before" : "after", g_py.window_s * 1e3f, s0, pend0, pend1);
	py_open(c, 1, point, t);
	py_reel_short(c);
}

/* cb_hit: 0 = the orc is not open, 1 = open (its block stops nothing), 2 = open and this hit is the riposte (a committed
 * cut or a stab: the game's High strike) */
static int py_hit_kind(uintptr_t c, int tier, double t)
{
	if (!py_open_now(c, t))
		return 0;
	struct py_open *p = py_find(c);
	p->hits++;
	if (tier == SC_TIER_LIGHT || tier == SC_TIER_NONE || !g_cb.nrec[SC_TIER_HEAVY])
		return 1;
	p->ripostes++, g_py.ripostes++;
	return 2;
}
/* the riposte into a raised shield: its HP 1 for the call (the game's blocked branch takes the rest: its break); 1 =
 * written (*was the HP) */
static int py_break_pre(uintptr_t c, float *was)
{
	uintptr_t sh = 0;
	float one = 1;
	*was = NAN;
	if (!g_py.brk || !cb_shield_item(c, &sh) || !sh || !peek(sh + 0xa8, was, sizeof *was) || !(*was > one))
		return 0;
	return arm_poke(sh + 0xa8, &one, sizeof one);
}
static void py_break_post(uintptr_t c, int set, float was, float hp0, float hp1, double t)
{
	if (!set)
		return;
	uintptr_t sh = 0;
	float now = NAN;
	int got = cb_shield_item(c, &sh) && sh && peek(sh + 0xa8, &now, sizeof now);
	if (got && now >= 1.0f) /* the blow passed it by (behind, its own direction test): the shield back as it was */
		arm_poke(sh + 0xa8, &was, sizeof was);
	g_py.breaks += got && now < 1.0f, g_py.break_miss += !(got && now < 1.0f);
	if (py_log_ok())
		logf_("PARRY riposte orc=%#lx t=%.3f into its raised shield: the shield %.1f (1 for the call) -> %.1f%s, health "
		      "%.1f -> %.1f", (unsigned long)c, t, was, got ? now : NAN, got && now < 1.0f ? " BROKEN (the game's break)"
		      : " not taken (the shield given back)", hp0, hp1);
	if (got && now < 1.0f && g_py.marks > 0) {
		g_py.marks--;
		ss_mark("the shield broken (SOMVR_PARRY_MARKS)", "riposte", t);
	}
}
/* cb_hit: the riposte's "into a raised shield" is the game's own direction rule (no sphere, no shield down for the call) */
static int py_shield_game(void) { return g_py.shield_mode == PY_SH_GAME; }

/* through its block: Talion's Combat+0x55b nonzero for the call (*was to put back; 1 = written) */
static int py_through_set(uintptr_t pc, uint8_t *was)
{
	uint8_t v = PY_TYPE_THROUGH;
	if (!pc || !peek(pc + RX_C_NOBLOCK, was, 1))
		return 0;
	*(volatile uint8_t *)(pc + RX_C_NOBLOCK) = v;
	g_py.through++;
	return 1;
}
static void py_through_back(uintptr_t pc, int set, uint8_t was)
{
	if (set)
		*(volatile uint8_t *)(pc + RX_C_NOBLOCK) = was;
}

/* rx_roll: no block roll for an open orc */
static int py_rx_skip(uintptr_t c, double t)
{
	if (!py_open_now(c, t))
		return 0;
	g_py.rx_skipped++;
	return 1;
}

/* in_frame: the left palm in the world (game cm) */
static void py_hands(const sv_vec3 p[2], const sv_vec3 pw[2])
{
	if (!g_py.shove)
		return;
	g_py.palm_ok = g_inp.hands[SOMVR_HAND_L].valid && vec_ok((const float[3]){ pw[0].x, pw[0].y, pw[0].z }, 1e7f);
	g_py.palm[0] = pw[0].x, g_py.palm[1] = pw[0].y, g_py.palm[2] = pw[0].z;
	if (!g_py.palm_ok) {
		g_py.nh = 0;
		return;
	}
	g_py.h[g_py.hpos].t = cb_now(), g_py.h[g_py.hpos].p = p[0];
	g_py.hpos = (g_py.hpos + 1) % PY_HIST, g_py.nh += g_py.nh < PY_HIST;
}

/* the shove's reaction: the recoil types of SOMVR_SHOVE_RECOIL in turn until one takes */
static int py_shove_recoil(const struct cb_orc *o, const float v[3], const float at[3], char *used, size_t cap)
{
	const char *ln = "(none)";
	uintptr_t owner = peek_ptr(o->c + C_OWNER), light = cb_record(SC_TIER_LIGHT, 0, &ln);
	char list[96], nm[48];
	snprintf(list, sizeof list, "%s", g_py.recoil);
	used[0] = 0;
	if (!owner || !light || !g_df.ok_recoil || !g_df.ph[0] || g_kx.state != 2)
		return 0;
	for (char *sv = NULL, *k = strtok_r(list, ",", &sv); k; k = strtok_r(NULL, ",", &sv)) {
		uintptr_t r = 0;
		if (!strcmp(k, "Medium") || !strcmp(k, "Light"))
			r = kx_pick(k, kx_dir(v, o->hobj), nm, sizeof nm);
		else
			snprintf(nm, sizeof nm, "%s", k), r = kx_find(k);
		size_t ul = strlen(used);
		if (!r) {
			snprintf(used + ul, cap - ul, "%s'%s' unknown", ul ? ", " : "", k);
			continue;
		}
		int took = df_recoil_call(g_df.ph[0], owner, o->c, light, r, at);
		snprintf(used + ul, cap - ul, "%s'%s' %s", ul ? ", " : "", nm, took ? "took" : "refused");
		if (took)
			return 1;
	}
	return 0;
}

/* combat_frame (the sword drawn): the shove; the nearest orc for the word */
static void py_frame(uintptr_t pc, const struct cb_orc *o, int n, const float root[3], double t)
{
	if (g_py.auto_step && t >= g_py.auto_t) { /* "parry auto MS": out to the right fast, then the edge cut across */
		const char *wd = g_py.auto_step == 1 ? "swing R 40 -8 35 0 -90 90 120" : "swing R -20 -18 62 0 90 90 250";
		const char *what = bd_word(wd);
		if (py_log_ok())
			logf_("PARRY auto t=%.3f: the test hand's %s (%s)", t, g_py.auto_step == 1 ? "wind-up" : "cut", what);
		g_py.auto_step = g_py.auto_step == 1 ? 2 : 0, g_py.auto_t = t + 0.13;
	}
	float best = 1e9f;
	g_py.nearest = 0;
	for (int i = 0; i < n; i++)
		if (o[i].dist < best)
			best = o[i].dist, g_py.nearest = o[i].c;
	if (!g_py.shove || !root)
		return;
	int free_hand = g_py.palm_ok && !bo_out() && !cl_holds(SOMVR_HAND_L) && !vb_grab_held() && !(g_bd.two.w > 0);
	if (!free_hand || g_py.nh < 2 || !pc)
		return;
	int now = (g_py.hpos - 1 + PY_HIST) % PY_HIST, k = -1;
	for (int j = 1; j < g_py.nh; j++) { /* the newest sample at least 80 ms older (at most 250) */
		int ix = (now - j + PY_HIST) % PY_HIST;
		double age = g_py.h[now].t - g_py.h[ix].t;
		if (age > 0.25)
			break;
		if (age >= 0.08) {
			k = ix;
			break;
		}
	}
	if (k < 0)
		return;
	double age = g_py.h[now].t - g_py.h[k].t;
	sv_vec3 a = somvr_rig_point(&g_rig, g_fr.base, g_py.h[k].p), b = somvr_rig_point(&g_rig, g_fr.base, g_py.h[now].p);
	float v[3] = { (float)((b.x - a.x) / age), (float)((b.y - a.y) / age), (float)((b.z - a.z) / age) };
	float sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (sp < g_py.shove_mps * 100.0f || sp > PY_SHOVE_MAX_CMS) /* (faster: a jump of the hand, not a push) */
		return;
	int near_i = -1;
	float near_d = 1e9f, near_cos = 0, near_ht = NAN, near_sh = NAN;
	for (int i = 0; i < n; i++) {
		struct py_open *p = py_find(o[i].c);
		if (p && t - p->shoved < PY_SHOVE_COOL_S)
			continue;
		float dx = o[i].pos[0] - g_py.palm[0], dz = o[i].pos[2] - g_py.palm[2], dh = sqrtf(dx * dx + dz * dz);
		float vh = sqrtf(v[0] * v[0] + v[2] * v[2]), cosv = dh > 1 && vh > 1 ? (v[0] * dx + v[2] * dz) / (dh * vh) : 0;
		/* the palm's height above the orc's feet (its object sits mid-body: the capsule's SOMVR_ORC_DOWN_CM below) */
		float ht = g_py.palm[1] - (o[i].pos[1] - g_cb.orc_down), sp3[3], sq[4], shp = cb_shield_hp(o[i].c);
		int shielded = isfinite(shp) && shp > 0, have_sh = shielded && cb_shield_where(o[i].c, sp3, sq);
		if (dh < near_d && cosv > 0.3f)
			near_i = i, near_d = dh, near_cos = cosv, near_ht = ht, near_sh = have_sh ? dist3(sp3, g_py.palm) : NAN;
		if (cosv < 0.6f) /* toward the orc */
			continue;
		const char *into = NULL;
		int chest = ht > 60 && ht < 175;
		if (have_sh && dist3(sp3, g_py.palm) <= g_cb.shield_cm + PY_HAND_CM)
			into = "its raised shield";
		else if (shielded && chest && dh <= g_cb.orc_r + g_py.shove_reach) /* the shield held before its body (its
										  * model's origin sits at its forearm) */
			into = "the shield before its body";
		else if (g_rx.ok && (RX_FN(RS_BLOCKING, rx_blocking_fn)(o[i].c) & 1) && chest && dh <= g_cb.orc_r + PY_HAND_CM)
			into = "its blocking body";
		if (!into)
			continue;
		char used[200], an[96], bn[96];
		uintptr_t before = peek_ptr(o[i].c + RX_C_ATTACK);
		int took = py_shove_recoil(&o[i], v, g_py.palm, used, sizeof used);
		g_py.shoves++, g_py.shove_took += took, g_py.shove_refused += !took;
		in_haptic_event(SOMVR_HAND_L, 1.0f, 0.12f, SOMVR_HAP_COMBAT); /* the thump in the shoving hand */
		float head[3] = { root[0], root[1] + 160, root[2] };
		py_sound(g_py.shove_sound, g_py.palm, head, t, "the shove");
		rl_note(o[i].c, o[i].hobj, "a shove", t); /* SOMVR_COMBAT_STAGGER_CM: the reel's cap */
		if (py_log_ok())
			logf_("PARRY shove orc=%#lx hobj=%#lx t=%.3f: the left hand into %s at %.2f m/s (%.0f cm from its root) -> "
			      "its recoil: %s; its attack '%s' -> '%s'", (unsigned long)o[i].c, (unsigned long)o[i].hobj, t, into,
			      sp * 0.01f, dh, used[0] ? used : "none known", rx_rec_name(before, an, sizeof an),
			      rx_rec_name(peek_ptr(o[i].c + RX_C_ATTACK), bn, sizeof bn));
		py_open(o[i].c, 2, g_py.palm, t);
		if ((p = py_find(o[i].c)))
			p->shoved = t;
		g_py.nh = 0; /* one push, one shove */
		return;
	}
	/* a push that met nothing: which test refused it (build 18's 0 of 8) */
	if (t >= g_py.refused_next) {
		g_py.refused_next = t + 0.4, g_py.pushes_refused++;
		if (py_log_ok()) {
			if (near_i >= 0)
				logf_("PARRY push refused t=%.3f: %.2f m/s; the nearest orc ahead %#lx: the palm %.0f cm from its axis (a shield "
				      "orc: <= %.0f, a blocking one: <= %.0f), %.0f cm above its feet (60..175), %.0f deg off the push "
				      "(<= 53), its shield's origin %.0f cm away (<= %.0f), shield %.0f, blocking %d", t, sp * 0.01f,
				      (unsigned long)o[near_i].c, near_d, g_cb.orc_r + g_py.shove_reach, g_cb.orc_r + PY_HAND_CM, near_ht,
				      acosf(fminf(1, fmaxf(-1, near_cos))) * 57.29578f, near_sh, g_cb.shield_cm + PY_HAND_CM,
				      cb_shield_hp(o[near_i].c), g_rx.ok ? RX_FN(RS_BLOCKING, rx_blocking_fn)(o[near_i].c) & 1 : -1);
			else
				logf_("PARRY push refused t=%.3f: %.2f m/s, no orc ahead (of %d)", t, sp * 0.01f, n);
		}
	}
}

static const char *py_word(const char *w)
{
	if (!g_bd.test)
		return "parry (SOMVR_HANDS_TEST off: ignored)";
	if (!strncmp(w, "open", 4)) {
		if (!g_py.nearest || (!g_py.on && !g_py.shove))
			return "parry open (no orc near, or SOMVR_PARRY and SOMVR_SHOVE off: ignored)";
		struct cb_orc o;
		float at[3] = { 0, 0, 0 };
		if (cb_orc_get(g_py.nearest, &o))
			memcpy(at, o.pos, sizeof at);
		py_open(g_py.nearest, 1, at, cb_now());
		return "parry open";
	}
	if (!strncmp(w, "status", 6)) {
		logf_("PARRY status: parries %llu (early %llu), opens %llu, ripostes %llu, breaks %llu, shoves %llu (took %llu)",
		      (unsigned long long)g_py.parries, (unsigned long long)g_py.early, (unsigned long long)g_py.opens,
		      (unsigned long long)g_py.ripostes, (unsigned long long)g_py.breaks, (unsigned long long)g_py.shoves,
		      (unsigned long long)g_py.shove_took);
		return "parry status";
	}
	int ms = -1;
	if (sscanf(w, "auto %d", &ms) == 1 && ms >= 0 && ms <= 1000) { /* build 19: the counter-cut after each parry */
		g_py.auto_ms = ms, g_py.auto_step = 0;
		return "parry auto MS (the test right hand cuts MS after each parry's opening; 0 = off)";
	}
	return "parry (not open|status|auto MS: ignored)";
}

static void py_end_log(void)
{
	if (!g_py.on && !g_py.shove)
		return;
	logf_("PARRY END parries=%llu (early blocks %llu) streak+1 %llu (failed %llu), sounds %llu (no id %llu), opens %llu, "
	      "hits through a block %llu, ripostes %llu, shields broken %llu (not taken %llu), REACT rolls skipped %llu, "
	      "shoves %llu (took %llu, refused %llu), pushes that met nothing %llu; dodge openings %llu, parry reels cut short "
	      "%llu; lines suppressed %llu", (unsigned long long)g_py.parries,
	      (unsigned long long)g_py.early, (unsigned long long)g_py.streaks, (unsigned long long)g_py.streak_fail,
	      (unsigned long long)g_py.sounds, (unsigned long long)g_py.sound_fail, (unsigned long long)g_py.opens,
	      (unsigned long long)g_py.through, (unsigned long long)g_py.ripostes, (unsigned long long)g_py.breaks,
	      (unsigned long long)g_py.break_miss, (unsigned long long)g_py.rx_skipped, (unsigned long long)g_py.shoves,
	      (unsigned long long)g_py.shove_took, (unsigned long long)g_py.shove_refused, (unsigned long long)g_py.pushes_refused,
	      (unsigned long long)g_py.dodge_opens, (unsigned long long)g_py.reels_short, (unsigned long long)g_py.suppressed);
	if (g_py.vault_s > 0)
		logf_("PARRY END build 27: openings by a vault over the orc %llu (SOMVR_PARRY_VAULT_MS %.0f)",
		      (unsigned long long)g_py.vault_opens, g_py.vault_s * 1e3f);
}
