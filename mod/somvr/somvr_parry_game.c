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
 * Test words (SOMVR_HANDS_TEST): "parry open" (the nearest orc opened as by a parry), "parry status".
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
	float window_s, open_s, shove_mps, shove_reach;
	char sound[64], shove_sound[64], recoil[96];
	struct py_open { uintptr_t c, hobj; double t0, until, shoved; int why, hits, ripostes; } o[PY_N];
	/* the left palm (world cm, in_frame) and its history relative to Talion's root (combat_frame) */
	int palm_ok;
	float palm[3], yaw;
	struct { double t; float rel[3]; } h[PY_HIST];
	int nh, hpos;
	uintptr_t nearest; /* the nearest orc of the last combat frame (the word "parry open") */
	uint64_t parries, early, opens, streaks, streak_fail, sounds, sound_fail, ripostes, breaks, break_miss, through,
		rx_skipped, shoves, shove_took, shove_refused, lines, suppressed;
} g_py;

static const char *const g_py_why[3] = { "-", "a parry", "a shove" };

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
	g_py.brk = env_word("SOMVR_PARRY_SHIELD", (const char *const[]){ "off", "break" }, 2, 1);
	g_py.shove_mps = env_float("SOMVR_SHOVE_MPS", 1.5f, 0.5f, 6, &bad);
	g_py.shove_reach = env_float("SOMVR_SHOVE_REACH_CM", 25, 0, 60, &bad);
	g_py.marks = (int)env_float("SOMVR_PARRY_MARKS", 0, 0, 20, &bad);
	const char *s = getenv("SOMVR_PARRY_SOUND"), *ss = getenv("SOMVR_SHOVE_SOUND"), *rc = getenv("SOMVR_SHOVE_RECOIL");
	snprintf(g_py.sound, sizeof g_py.sound, "%s", s && *s ? (strcmp(s, "off") ? s : "") : "cbt_imp_surface_player");
	snprintf(g_py.shove_sound, sizeof g_py.shove_sound, "%s", ss && *ss ? (strcmp(ss, "off") ? ss : "") : "cbt_imp_atk_biped");
	snprintf(g_py.recoil, sizeof g_py.recoil, "%s", rc && *rc ? rc : "Block_Breaker,Medium,Light");
	if (!g_py.on && !g_py.shove)
		return;
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
static void py_open(uintptr_t c, int why, const float at[3], double t)
{
	struct py_open *p = py_slot(c, t);
	if (!p)
		return;
	struct cb_orc o;
	p->hobj = cb_orc_get(c, &o) ? o.hobj : 0;
	p->t0 = t, p->until = t + g_py.open_s, p->why = why, p->hits = p->ripostes = 0;
	g_py.opens++;
	char an[96];
	if (py_log_ok())
		logf_("PARRY open orc=%#lx hobj=%#lx t=%.3f by %s for %.0f ms (its attack now '%s', health %.1f, shield %.1f) at "
		      "%.1f,%.1f,%.1f", (unsigned long)c, (unsigned long)p->hobj, t, g_py_why[why], g_py.open_s * 1e3f,
		      rx_rec_name(peek_ptr(c + RX_C_ATTACK), an, sizeof an), cb_health(c), cb_shield_hp(c), at[0], at[1], at[2]);
	if (g_py.marks > 0) {
		g_py.marks--;
		ss_mark("the opening (SOMVR_PARRY_MARKS)", g_py_why[why], t);
	}
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
static void py_hands(const sv_vec3 pw[2])
{
	if (!g_py.shove)
		return;
	g_py.palm_ok = g_inp.hands[SOMVR_HAND_L].valid && vec_ok((const float[3]){ pw[0].x, pw[0].y, pw[0].z }, 1e7f);
	g_py.palm[0] = pw[0].x, g_py.palm[1] = pw[0].y, g_py.palm[2] = pw[0].z;
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
	float best = 1e9f;
	g_py.nearest = 0;
	for (int i = 0; i < n; i++)
		if (o[i].dist < best)
			best = o[i].dist, g_py.nearest = o[i].c;
	if (!g_py.shove || !root)
		return;
	float yaw = g_fr.base.yaw;
	int free_hand = g_py.palm_ok && !bo_out() && !cl_holds(SOMVR_HAND_L) && !vb_grab_held() && !(g_bd.two.w > 0);
	if (!free_hand || yaw != g_py.yaw) { /* a turn swings the hand about the root: the history starts again */
		g_py.yaw = yaw, g_py.nh = 0;
		if (!free_hand)
			return;
	}
	float rel[3] = { g_py.palm[0] - root[0], g_py.palm[1] - root[1], g_py.palm[2] - root[2] };
	int k = -1;
	for (int j = 0; j < g_py.nh; j++) { /* the newest sample at least 80 ms old (at most 250) */
		int ix = (g_py.hpos - 1 - j + PY_HIST) % PY_HIST;
		double age = t - g_py.h[ix].t;
		if (age > 0.25)
			break;
		if (age >= 0.08) {
			k = ix;
			break;
		}
	}
	g_py.h[g_py.hpos].t = t, memcpy(g_py.h[g_py.hpos].rel, rel, sizeof rel);
	g_py.hpos = (g_py.hpos + 1) % PY_HIST, g_py.nh += g_py.nh < PY_HIST;
	if (k < 0 || !pc)
		return;
	double age = t - g_py.h[k].t;
	float v[3];
	for (int i = 0; i < 3; i++)
		v[i] = (float)((rel[i] - g_py.h[k].rel[i]) / age);
	float sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (sp < g_py.shove_mps * 100.0f || sp > PY_SHOVE_MAX_CMS) /* (faster: a jump of the hand or of Talion, not a push) */
		return;
	for (int i = 0; i < n; i++) {
		struct py_open *p = py_find(o[i].c);
		if (p && t - p->shoved < PY_SHOVE_COOL_S)
			continue;
		float dx = o[i].pos[0] - g_py.palm[0], dz = o[i].pos[2] - g_py.palm[2], dh = sqrtf(dx * dx + dz * dz);
		float vh = sqrtf(v[0] * v[0] + v[2] * v[2]);
		if (!(dh > 1) || !(vh > 1) || (v[0] * dx + v[2] * dz) / (dh * vh) < 0.6f) /* toward the orc */
			continue;
		float sp3[3], sq[4], shp = cb_shield_hp(o[i].c);
		const char *into = NULL;
		int chest = g_py.palm[1] - o[i].pos[1] > 60 && g_py.palm[1] - o[i].pos[1] < 175, shielded = isfinite(shp) && shp > 0;
		if (shielded && cb_shield_where(o[i].c, sp3, sq) && dist3(sp3, g_py.palm) <= g_cb.shield_cm + PY_HAND_CM)
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
		break;
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
	return "parry (not open|status: ignored)";
}

static void py_end_log(void)
{
	if (!g_py.on && !g_py.shove)
		return;
	logf_("PARRY END parries=%llu (early blocks %llu) streak+1 %llu (failed %llu), sounds %llu (no id %llu), opens %llu, "
	      "hits through a block %llu, ripostes %llu, shields broken %llu (not taken %llu), REACT rolls skipped %llu, "
	      "shoves %llu (took %llu, refused %llu); lines suppressed %llu", (unsigned long long)g_py.parries,
	      (unsigned long long)g_py.early, (unsigned long long)g_py.streaks, (unsigned long long)g_py.streak_fail,
	      (unsigned long long)g_py.sounds, (unsigned long long)g_py.sound_fail, (unsigned long long)g_py.opens,
	      (unsigned long long)g_py.through, (unsigned long long)g_py.ripostes, (unsigned long long)g_py.breaks,
	      (unsigned long long)g_py.break_miss, (unsigned long long)g_py.rx_skipped, (unsigned long long)g_py.shoves,
	      (unsigned long long)g_py.shove_took, (unsigned long long)g_py.shove_refused, (unsigned long long)g_py.suppressed);
}
