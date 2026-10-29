/* somvr_execute_game.c - SOMVR_EXECUTE=1|log, the game side of the execution in VR (somvr_execute.h; design-vr.md 3
 * "Executions and finishers"; docs/re-xr-pipeline.md 30.23). Not a translation unit of its own: somvr.c includes it last
 * (it uses combat's hit path and records, SOMVR_FINISH's sites for the streak, the ClientFX and the slow motion,
 * SOMVR_COMBAT_DEATH's recoil type and the knockback watch's death trace). Off by default; needs SOMVR_COMBAT=1.
 *
 * Calls (game thread, H4's combat_frame): xc_frame(pl, root, blade, t) every combat frame (the counter,
 * the ready signal, the trigger, the slow motion's end, the marks); xc_contact(...) at a contact that deals a hit, before
 * cb_hit (1 = the execution took it); xc_eye(o) from H1's eye 0 (the trace); xc_start / xc_end_log / xc_word.
 *
 * The hit streak is the game's own: our hits run the hit counter (Combat_DealHit), its charge fills at the character's
 * need (8; 5 with the skill: +0xc8 is the game's value, not ours; it lapses with the streak, 2.5-3 s after the last
 * hit). Ready (the charged state) = one pulse in the sword hand and the glow: SOMVR_EXECUTE_GLOW=game (default) is the
 * game's own - its charged buff lights Urfael blue-white by itself once our hits fill the charge (run 095557), =fx adds
 * a looping ClientFX on the active weapon's model object (re-combat-items.md 2.4, SOMVR_FINISH's recipe and sites;
 * SOMVR_EXECUTE_GLOW_FX, default the light chain the stock strikes put on the sword), =off. An execution = a heavy hit
 * with the right trigger held while ready: Combat_DealHit(contact, Talion's Combat, the orc, the stock execution record,
 * forceKill 1) - the front or the back family by the contact's side (SOMVR_EXECUTE_FRONT / _BACK: the
 * PC_Bip_Standing_InstaKill_* records in turn, their _Sever variants among them), the charge spent first as the stock
 * attack start spends it (XC_SPEND's rule through XC_CHARGE; the records carry ConsumeHitCounterCharge 1; a record's
 * ResetHitCounterOnStart clears the counter, XC_CLEAR), SOMVR_COMBAT_DEATH's recoil type for the call (the orc falls
 * where it stands; SOMVR_EXECUTE_DEATH=record keeps the record's own, which is none), SOMVR_EXECUTE_SEVER=key (default):
 * a record with a SeverTarget ('Head': the _Sever ones) cuts it with the game's own sever as its SEVER animation key
 * would (the death handler's sever-on-kill does not cut for our hit, run 100848), the world at SOMVR_EXECUTE_SLOWMO
 * (0.3) for SOMVR_EXECUTE_SLOWMO_MS (500 ms real time; SlowMO.Set on the game timer: the world and Talion, never the
 * tracked view; 0 = off; at its end the timer's values back only while it is still ours: the game's SlowMo manager,
 * e.g. the bow's aim begun meanwhile, keeps its own) and a strong pulse. Nothing starts an attack of Talion's or a sync: no paired animation, no
 * camera, the view is the player's. Not ready, or not a heavy hit, with the trigger held: the ordinary hit (one line).
 * Test only (SOMVR_HANDS_TEST): the words `execute where|face|chase on|off` (the headless player's aim and legs),
 * SOMVR_EXECUTE_MARKS / _MARK_MS (a mark of the eyes after each kill).
 *
 * Lines: `CONFIG execute ...`, `EXECUTE site ... MISMATCH`, `EXECUTE streak ...` (each change), `EXECUTE ready ...` /
 * `EXECUTE ready off ...`, `EXECUTE glow fx ...`, `EXECUTE record '...' ...` (its data, once), `EXECUTE kill ...`,
 * `EXECUTE refused ...`, `EXECUTE f=...` (the eye trace: from a trigger press while ready to 1.5 s after the kill),
 * `EXECUTE END`. */

/* EXECUTE_SITES_BEGIN (read only, never patched). XC_CHARGE FUN_027256c0(hc, short v): the charge's setter (its bytes
 * prove +0xd4 charged, +0xc6 the charge, +0xc8 the need; at the need the charged buff and the HUD, FUN_02724fe0);
 * XC_SPEND FUN_02728110+0xaa7: the stock attack start's spend for ConsumeHitCounterCharge (+0xd2 the uses: > 1 -> - 1,
 * else XC_CHARGE(hc, 0)); XC_CLEAR FUN_02727200(hc): the counter's clear (ResetHitCounterOnStart, the miss reset);
 * XC_SEVER FUN_026d4670(victim Combat*, HRECORD severType, LTVector* velocity, LTVector* rotation, HOBJECT attacker): the
 * sever the stock SEVER animation key and the death handler call (re-combat.md 2.4); XC_LAPSE FUN_027259c0+0x46: the
 * counter's timer expiry (+0x61 running, +0x60 paused, +0x38 the start, +0x40 the delay, +0x58 the clock's +0x18 = now:
 * now - start >= delay -> the value 0, the clear; the proof of the offsets SOMVR_EXECUTE_LAPSE_S writes). Order = XCS_* */
static const struct site_spec g_xc_sites_game[] = {
SITE("XC_CHARGE", K_ENTRY, 0x27256c0, 0x27256c0, "53 80 bf d4 00 00 00 00 48 89 fb 66 89 b7 c6 00 00 00 48 8b 47 30 75 09 66 3b b7 c8 00 00 00", 0x27256c0, "53 80 bf d4 00 00 00 00 48 89 fb 66 89 b7 c6"),
SITE("XC_SPEND", K_ENTRY, 0x2728bb7, 0x2728bb7, "0f b6 85 d2 00 00 00 3c 01 76 0e 83 e8 01 88 85 d2 00 00 00", 0x2728bb7, "0f b6 85 d2 00 00 00 3c 01 76 0e 83 e8 01 88"),
SITE("XC_CLEAR", K_ENTRY, 0x2727200, 0x2727200, "55 53 48 89 fb 48 83 ec 58 48 8b 47 70 48 2b 47 68 48 c1 f8 03", 0x2727200, "55 53 48 89 fb 48 83 ec 58 48 8b 47 70 48 2b"),
SITE("XC_SEVER", K_ENTRY, 0x26d4670, 0x26d4670, "41 57 4c 8d 8f 30 05 00 00 49 89 cf 41 56 49 89 f6 41 55 49 89 d5", 0x26d4670, "41 57 4c 8d 8f 30 05 00 00 49 89 cf 41 56 49"),
SITE("XC_LAPSE", K_ENTRY, 0x2725a06, 0x2725a06, "80 7b 61 00 0f 84 89 00 00 00 80 7b 60 00 f2 0f 10 53 38 f2 0f 10 4b 40 0f 85 b4 05 00 00 48 8b 43 58 66 0f ef c0 48 85 c0 74 05 f2 0f 10 40 18 f2 0f 5c c2 66 0f 2f c1 0f 93 c0", 0x2725a06, "80 7b 61 00 0f 84 89 00 00 00 80 7b 60 00 f2"),
};
/* EXECUTE_SITES_END */
enum { XCS_CHARGE, XCS_SPEND, XCS_CLEAR, XCS_SEVER, XCS_LAPSE, XCS_N };
_Static_assert(sizeof g_xc_sites_game / sizeof g_xc_sites_game[0] == XCS_N, "execute sites");
static const struct site_spec *g_xc_sites = g_xc_sites_game; /* selftest: stand-ins */
typedef void (*xc_charge_fn)(uintptr_t hc, int16_t v);
typedef void (*xc_clear_fn)(uintptr_t hc);
typedef void (*xc_sever_fn)(uintptr_t victim, uintptr_t type, const float *vel, const float *rot, uintptr_t attacker);
#define XC_FN(i, T) ((T)g_xc_sites[i].target)

#define XC_HC_STREAK 0xc4
#define XC_HC_CHARGE 0xc6
#define XC_HC_NEED 0xc8
#define XC_HC_USES 0xd2
#define XC_HC_ENABLED 0xd3
#define XC_HC_CHARGED 0xd4
#define XC_HC_TIMER 0x38     /* the counter's timer (XC_LAPSE): +0x38 the start, +0x40 the delay (doubles), +0x58 the clock */
#define XC_HC_DELAY 0x40
#define XC_HC_CLOCK 0x58
#define XC_HC_PAUSED 0x60
#define XC_HC_RUNNING 0x61
#define XC_AD_RECOIL_HIT 16 /* AttackData (schema 0x4ba7280, attrs.tsv): RecoilTypeHit */
#define XC_AD_SEVER_CHANCE 57
#define XC_AD_SEVER 58
#define XC_AD_SEVER_ON_KILL 59
#define XC_AD_CONSUME 70    /* ConsumeHitCounterCharge */
#define XC_AD_RESET 83      /* ResetHitCounterOnStart */
#define XC_SYNC_SCHEMA 0x4baa4a0
#define XC_SYNC_SLOWMO 33   /* Combat/Sync: EnableDynamicSlowMo */
#define XC_RECS 24
#define XC_TRACE_S 1.5      /* the eye trace after the kill */
#define XC_ARM_S 4.0        /* ... and from a trigger press while ready, at most this long without a kill */

enum { XC_OFF = 0, XC_ON = 1, XC_LOG = 2 };
enum { XC_GLOW_FX = 0, XC_GLOW_GAME = 1, XC_GLOW_OFF = 2 };
enum { XC_DEATH_COMBAT = 0, XC_DEATH_RECORD = 1 };
static const char *const g_xc_glow_w[] = { "fx", "game", "off" };
struct xc_rec { char name[96]; uintptr_t rec, sever; int state, consume, reset; }; /* state 0 unknown, 1 found, -1 not in the DB */
static struct {
	int mode, glow, death, sever, ok_hc, ok_spend, ok_fx, ok_slowmo, ok_sever, ok_lapse, marks;
	char front[640], back[640], glow_fx[160];
	float slowmo, slowmo_s, back_deg, mark_s, lapse_s, ready_s;
	struct xc_rec recs[XC_RECS];
	int nrecs;
	unsigned turn[2];
	/* state */
	somvr_ex_hc hc;
	int ready, trig, trig_ready_edge;
	double ready_t, arm_until, trace_until, smo_until, mark_at, next_log, last_line, refuse_t;
	uint32_t smo_num, smo_den, smo_set[2];
	uintptr_t fx_handle, fx_item, pcomb;
	float root[3];
	int have_eye, req_status, req_where, test, chase;
	double chase_snap_t;
	uint64_t chase_snaps, chase_walks;
	float eye_last[3], root_last[3];
	uint64_t trace_lines;
	/* counts */
	uint64_t frames, ready_on, ready_off, streak_lines, executions, kills, not_killed, refused[SOMVR_EXJ_NO_TRIGGER + 1],
		spent_charge, spent_use, cleared, slowmo_n, slowmo_skip, slowmo_game, fx_on, fx_fail, norec, severed;
	int max_streak, max_charge;
	float max_eye_step, max_root_step;
	/* the lapse (SOMVR_EXECUTE_LAPSE_S / _READY_S) */
	double up_t, delay_seen, delay_game, gap_max;
	uint64_t lapse_writes, lapse_lines, lapsed, lapsed_ours, cleared_early;
} g_xc;

static int xc_log_ok(void) { return g_xc.streak_lines < 400; }

/* the counter (Combat+0x1f0, FI_HITCOUNT's offsets) */
static int xc_read_hc(uintptr_t pc, somvr_ex_hc *h)
{
	memset(h, 0, sizeof *h);
	uintptr_t hc = pc ? peek_ptr(pc + FI_C_HITCTR) : 0;
	unsigned char b[0x18];
	if (!hc || !peek(hc + XC_HC_STREAK, b, sizeof b))
		return 0;
	memcpy(&h->streak, b, 2), memcpy(&h->charge, b + 2, 2), memcpy(&h->need, b + 4, 2);
	h->uses = b[XC_HC_USES - XC_HC_STREAK], h->enabled = b[XC_HC_ENABLED - XC_HC_STREAK];
	h->charged = b[XC_HC_CHARGED - XC_HC_STREAK];
	return h->valid = 1;
}

/* ---- the glow: a looping ClientFX on the active weapon's model object (fi_fx_start's recipe) ---------------------- */
static void xc_fx_stop(const char *why)
{
	if (!g_xc.fx_handle)
		return;
	uintptr_t h = g_xc.fx_handle;
	FI_FN(FIS_FX_RELEASE, fi_release_fn)(&g_xc.fx_handle);
	logf_("EXECUTE glow fx off (%s): handle %#lx -> %#lx", why, (unsigned long)h, (unsigned long)g_xc.fx_handle);
	g_xc.fx_handle = 0, g_xc.fx_item = 0;
}
static void xc_fx_start(uintptr_t pcomb)
{
	if (g_xc.fx_handle || !g_xc.ok_fx || g_xc.glow != XC_GLOW_FX || !g_xc.glow_fx[0] || g_xc.mode != XC_ON || !pcomb)
		return;
	uintptr_t item = fi_active_item(pcomb), b = item ? peek_ptr(item + FI_IT_ENTRIES) : 0;
	uintptr_t e = item ? peek_ptr(item + FI_IT_ENTRIES + 8) : 0, obj = b && e > b ? peek_ptr(b + FI_IE_OBJ) : 0;
	uintptr_t os = peek_ptr(g_fig.objsys_pp), vt = vptr_of((void *)os), shell = peek_ptr(g_fig.shell_pp);
	uintptr_t mgr = shell ? peek_ptr(shell + FI_SH_FXMGR) : 0;
	const char *why = !obj ? "no active weapon model object" : !os || vt != g_fig.vt_objsys ||
			  vt + 0x118 != g_fi_sites[FIS_OBJSYS_MH].addr || peek_ptr(vt + 0x118) != g_fi_sites[FIS_OBJSYS_MH].target
			  ? "the object system is not the checked one" : !mgr ? "no ClientFX manager (shell+0x80c78)" : NULL;
	if (why) {
		g_xc.fx_fail++;
		if (g_xc.fx_fail <= 8)
			logf_("EXECUTE glow fx not started: %s (item %#lx)", why, (unsigned long)item);
		return;
	}
	uintptr_t mh = FI_FN(FIS_OBJSYS_MH, fi_mh_fn)(os, obj);
	static unsigned char cs[0x400] __attribute__((aligned(16))); /* CLIENTFX_CREATESTRUCT (>= 0x1c8 bytes); game thread */
	memset(cs, 0, sizeof cs);
	FI_FN(FIS_FX_CSINIT, fi_cs_fn)(cs, g_xc.glow_fx, mh, 1u /* loop */, obj);
	long rc = FI_FN(FIS_FX_CREATE, fi_create_fn)(mgr + 0x30, &g_xc.fx_handle, cs, 1, 0, 0);
	FI_FN(FIS_FX_CSFREE, fi_csfree_fn)(cs);
	unsigned char on = 0;
	peek(g_fig.fx_on, &on, 1);
	if (g_xc.fx_handle)
		g_xc.fx_item = item, g_xc.fx_on++;
	else
		g_xc.fx_fail++;
	logf_("EXECUTE glow fx '%s' on item %#lx obj %#lx: rc=%ld handle=%#lx clientfx=%u", g_xc.glow_fx, (unsigned long)item,
	      (unsigned long)obj, rc, (unsigned long)g_xc.fx_handle, on);
}

/* ---- the slow motion: SlowMO.Set on the game timer, its own values back after the time (fi_slowmo's way) --------- */
static void xc_slowmo(double t)
{
	uintptr_t tm = peek_ptr(g_fig.timer_pp) ? peek_ptr(peek_ptr(g_fig.timer_pp) + 8) : 0;
	uint32_t nd[2] = { 0, 0 };
	if (!(g_xc.slowmo > 0) || !g_xc.ok_slowmo || g_xc.smo_until > 0)
		return;
	if (!tm || !peek(tm + FI_TM_NUM, nd, sizeof nd) || nd[0] != nd[1] || !nd[0]) {
		g_xc.slowmo_skip++; /* the game's own slow motion (or none to read): not ours to change */
		logf_("EXECUTE slow motion skipped: the game timer %u/%u (%s)", nd[0], nd[1], tm ? "not at its normal rate" : "unread");
		return;
	}
	char a[32];
	snprintf(a, sizeof a, "%.3f", (double)g_xc.slowmo);
	const char *argv[1] = { a };
	FI_FN(FIS_SLOWMO, fi_slowmo_fn)(1, argv);
	uint32_t now[2] = { 0, 0 };
	peek(tm + FI_TM_NUM, now, sizeof now);
	g_xc.smo_num = nd[0], g_xc.smo_den = nd[1], g_xc.smo_until = t + g_xc.slowmo_s;
	g_xc.smo_set[0] = now[0], g_xc.smo_set[1] = now[1];
	g_xc.slowmo_n++;
	logf_("EXECUTE slow motion %.2f for %.0f ms: the game timer %u/%u -> %u/%u", (double)g_xc.slowmo, g_xc.slowmo_s * 1e3,
	      nd[0], nd[1], now[0], now[1]);
}
static void xc_slowmo_end(double t, int now)
{
	if (!(g_xc.smo_until > 0) || (!now && t < g_xc.smo_until))
		return;
	g_xc.smo_until = 0;
	/* the game's SlowMo manager writes the same timer (the bow's aim, the combat slots: re-bow.md 12): a value that is no
	 * longer ours is the game's own since, left to the game (which ends it); ours = its values back */
	uintptr_t tm = peek_ptr(g_fig.timer_pp) ? peek_ptr(peek_ptr(g_fig.timer_pp) + 8) : 0;
	uint32_t cur[2] = { 0, 0 };
	if (tm && peek(tm + FI_TM_NUM, cur, sizeof cur) && (cur[0] != g_xc.smo_set[0] || cur[1] != g_xc.smo_set[1])) {
		g_xc.slowmo_game++;
		logf_("EXECUTE slow motion over: the game timer %u/%u is the game's own since (ours %u/%u): left to the game",
		      cur[0], cur[1], g_xc.smo_set[0], g_xc.smo_set[1]);
		return;
	}
	char a[2][16];
	snprintf(a[0], sizeof a[0], "%u", g_xc.smo_num), snprintf(a[1], sizeof a[1], "%u", g_xc.smo_den);
	const char *argv[2] = { a[0], a[1] };
	FI_FN(FIS_SLOWMO, fi_slowmo_fn)(2, argv);
	logf_("EXECUTE slow motion over: the game timer back to %u/%u", g_xc.smo_num, g_xc.smo_den);
}

/* ---- the records ------------------------------------------------------------------------------------------------- */
static struct xc_rec *xc_rec_of(const char *name)
{
	struct xc_rec *e = NULL;
	for (int i = 0; i < g_xc.nrecs && !e; i++)
		if (!strcmp(g_xc.recs[i].name, name))
			e = &g_xc.recs[i];
	if (!e && g_xc.nrecs < XC_RECS) {
		e = &g_xc.recs[g_xc.nrecs++];
		memset(e, 0, sizeof *e);
		snprintf(e->name, sizeof e->name, "%s", name);
	}
	if (!e || e->state)
		return e;
	uintptr_t cat = peek_ptr(g_cbg.atkcat_pp);
	uint32_t cnt = 0;
	if (!cat || !peek(cat + CAT_COUNT, &cnt, sizeof cnt) || !cnt)
		return e; /* the database is not loaded yet */
	e->rec = cb_rec_valid(CB_FN(CS_ATK_BYNAME, cb_byname_fn)(e->name));
	e->state = e->rec ? 1 : -1;
	if (!e->rec) {
		logf_("EXECUTE record '%s': not in Combat/Attack (skipped for the run)", e->name);
		return e;
	}
	/* its data, once: what the game's start and death do with it */
	uintptr_t ad = g_cb.ok_ad ? CB_FN(CS_ATTACKDATA, cb_attackdata_fn)(e->rec) : 0;
	uint64_t v[2] = { 0, 0 };
	int ty = 0, ns = 0, nk = 0, tks = 0;
	char rn[96] = "-", sn[96] = "-";
	e->consume = ad ? rx_db_bool(ad, g_rxg.sch_ad, XC_AD_CONSUME) : -1;
	e->reset = ad ? rx_db_bool(ad, g_rxg.sch_ad, XC_AD_RESET) : -1;
	uintptr_t rh = ad ? rx_db_rec(ad, g_rxg.sch_ad, XC_AD_RECOIL_HIT) : 0;
	if (rh)
		rx_rec_name(rh, rn, sizeof rn);
	if (ad && rx_db_list(ad, g_rxg.sch_ad, XC_AD_SEVER, v, 1, &ty, &ns) >= 0 && ns && ty == 0xa && v[0])
		e->sever = (uintptr_t)v[0], rx_rec_name(e->sever, sn, sizeof sn);
	if (ad)
		rx_db_list(ad, g_rxg.sch_ad, XC_AD_SEVER_ON_KILL, v, 1, &tks, &nk);
	float chance = ad ? rx_db_float(ad, g_rxg.sch_ad, XC_AD_SEVER_CHANCE, NAN) : NAN;
	uintptr_t anim = rx_db_rec(e->rec, g_fig.sch_attack, FI_ATK_ANIM), sync = anim ? rx_db_rec(anim, g_fig.sch_anim, FI_ANIM_SYNC) : 0;
	char syn[96] = "-";
	if (sync)
		rx_rec_name(sync, syn, sizeof syn);
	int smo = sync ? rx_db_bool(sync, XC_SYNC_SCHEMA, XC_SYNC_SLOWMO) : -1;
	logf_("EXECUTE record '%s' -> %#lx: ConsumeHitCounterCharge %d ResetHitCounterOnStart %d RecoilTypeHit '%s' "
	      "SeverTarget %d ('%s') SeverTargetOnKill %d (type %d) SeverTargetChance %.2f; its sync '%s' EnableDynamicSlowMo %d",
	      e->name, (unsigned long)e->rec, e->consume, e->reset, rn, ns, sn, nk, tks, (double)chance, syn, smo);
	return e;
}

/* ---- every combat frame ------------------------------------------------------------------------------------------- */
/* SOMVR_EXECUTE_LAPSE_S / _READY_S: the counter's own timer kept longer (XC_LAPSE's offsets). The game restarts it with
 * its delay (3 s) at every hit; while the streak is up this raises the delay to ours (somvr_ex_lapse: never shorter than
 * the game's). The game's expiry and clear, Talion's hits (the counter's own decrement / clear), the misses and the need
 * stay the game's; nothing else is written. Returns the delay now in the timer (0 = none running) */
static double xc_lapse(uintptr_t pc, const somvr_ex_hc *h, double t)
{
	uintptr_t hc = pc ? peek_ptr(pc + FI_C_HITCTR) : 0;
	unsigned char b[XC_HC_RUNNING + 1 - XC_HC_TIMER];
	if (!hc || !peek(hc + XC_HC_TIMER, b, sizeof b))
		return 0;
	double start, delay, now = NAN;
	uintptr_t clk;
	memcpy(&start, b, 8), memcpy(&delay, b + XC_HC_DELAY - XC_HC_TIMER, 8), memcpy(&clk, b + XC_HC_CLOCK - XC_HC_TIMER, 8);
	int running = b[XC_HC_RUNNING - XC_HC_TIMER], paused = b[XC_HC_PAUSED - XC_HC_TIMER];
	if (!running)
		return 0;
	if (clk)
		peek(clk + 0x18, &now, 8);
	if (delay > 0 && fabs(delay - g_xc.lapse_s) > 1e-3 && fabs(delay - g_xc.ready_s) > 1e-3)
		g_xc.delay_game = delay; /* not ours: the game's own (written at its last restart) */
	double want = g_xc.mode == XC_ON && g_xc.ok_lapse ? somvr_ex_lapse(h, running, delay, g_xc.lapse_s, g_xc.ready_s) : 0;
	if (want > 0) {
		*(volatile double *)(hc + XC_HC_DELAY) = want;
		g_xc.lapse_writes++;
		if (g_xc.lapse_lines++ < 200)
			logf_("EXECUTE lapse t=%.3f: the counter's timer %.2f -> %.2f s (the streak %d, the charge %d of %d%s; %.2f s of it "
			      "gone%s)", t, delay, want, h->streak, h->charge, h->need, somvr_ex_ready(h) ? ", ready" : "",
			      now - start, paused ? ", paused" : "");
		delay = want;
	}
	return delay;
}

static int xc_streak_now(void)
{
	return g_xc.mode && g_xc.ok_hc ? g_xc.hc.streak : 0;
}

static void xc_frame(uintptr_t pl, const float root[3], int blade, double t)
{
	if (!g_xc.mode)
		return;
	uintptr_t pcomb = root && pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	g_xc.frames++;
	g_xc.pcomb = pcomb;
	if (root)
		memcpy(g_xc.root, root, sizeof g_xc.root);
	xc_slowmo_end(t, 0);
	somvr_ex_hc h;
	int okh = g_xc.ok_hc && xc_read_hc(pc, &h);
	if (!okh)
		memset(&h, 0, sizeof h);
	if (okh && (h.streak != g_xc.hc.streak || h.charge != g_xc.hc.charge || h.charged != g_xc.hc.charged ||
		    h.uses != g_xc.hc.uses || h.need != g_xc.hc.need)) {
		g_xc.streak_lines++;
		if (xc_log_ok())
			logf_("EXECUTE streak t=%.3f %d (was %d): charge %d of %d (was %d), enabled %u, charged %u, uses %u", t,
			      h.streak, g_xc.hc.streak, h.charge, h.need, g_xc.hc.charge, h.enabled, h.charged, h.uses);
		g_xc.max_streak = h.streak > g_xc.max_streak ? h.streak : g_xc.max_streak;
		g_xc.max_charge = h.charge > g_xc.max_charge ? h.charge : g_xc.max_charge;
		if (h.streak > g_xc.hc.streak) {
			if (g_xc.up_t > 0 && g_xc.hc.streak > 0 && t - g_xc.up_t > g_xc.gap_max)
				g_xc.gap_max = t - g_xc.up_t; /* the longest gap between hits a streak survived */
			g_xc.up_t = t;
		} else if (!h.streak && g_xc.hc.streak > 0) { /* the timer's expiry, or the game's own clear before it */
			double gap = g_xc.up_t > 0 ? t - g_xc.up_t : 0;
			int timer = g_xc.delay_seen > 0 && gap >= g_xc.delay_seen - 0.15;
			g_xc.lapsed += timer, g_xc.cleared_early += !timer;
			g_xc.lapsed_ours += timer && g_xc.delay_seen > g_xc.delay_game + 1e-3;
			char an[96];
			if (xc_log_ok())
				logf_("EXECUTE streak t=%.3f %d -> 0 %.2f s after its last rise: %s (the timer's delay %.2f s, the game's "
				      "%.2f; Talion's attack now '%s')", t, g_xc.hc.streak, gap, timer ? "the timer ran out" :
				      "cleared before the timer (a hit on Talion, a miss or an attack's reset)", g_xc.delay_seen,
				      g_xc.delay_game, rx_rec_name(pc ? peek_ptr(pc + RX_C_ATTACK) : 0, an, sizeof an));
		}
	}
	if (okh) {
		double d = h.streak > 0 ? xc_lapse(pc, &h, t) : 0;
		if (d > 0)
			g_xc.delay_seen = d;
		g_xc.hc = h;
	}
	int ready = okh && somvr_ex_ready(&h);
	if (ready && !g_xc.ready) {
		g_xc.ready_on++, g_xc.ready_t = t;
		in_haptic_event(SOMVR_HAND_R, 0.5f, 0.06f, SOMVR_HAP_COMBAT);
		logf_("EXECUTE ready t=%.3f: the streak %d, the charge %d of %d (the game's: charged %u, uses %u); one pulse, the "
		      "glow %s", t, h.streak, h.charge, h.need, h.charged, h.uses, g_xc_glow_w[g_xc.glow]);
	} else if (!ready && g_xc.ready) {
		g_xc.ready_off++;
		logf_("EXECUTE ready off t=%.3f after %.2f s: the streak %d, the charge %d of %d", t, t - g_xc.ready_t, h.streak,
		      h.charge, h.need);
	}
	g_xc.ready = ready;
	/* the glow follows the ready state and the sword in the hand (the item's own FX code does not know our handle) */
	if (g_xc.fx_handle && (!ready || !blade || fi_active_item(pcomb) != g_xc.fx_item))
		xc_fx_stop(!ready ? "not ready" : !blade ? "no blade (sheathed, paused or the menu)" : "the active item changed");
	else if (ready && blade && !g_xc.fx_handle && g_xc.fx_fail < 8)
		xc_fx_start(pcomb);
	/* the right trigger (the wraith charge): the pointer's click takes it first (vm_route zeroes it) */
	const somvr_hand_in *hi = &g_inp.st.hand[SOMVR_HAND_R];
	int tr = somvr_ex_trigger(g_xc.trig, (hi->active & SOMVR_HACT_INPUT) ? hi->trigger : 0);
	if (tr && !g_xc.trig && ready) {
		g_xc.arm_until = t + XC_ARM_S; /* the trace: a press while ready (the player is about to execute) */
		logf_("EXECUTE trigger t=%.3f: held while ready (the streak %d)", t, h.streak);
	}
	g_xc.trig = tr;
	if (g_xc.mark_at > 0 && t >= g_xc.mark_at) {
		g_xc.mark_at = 0;
		if (g_xc.marks > 0) {
			g_xc.marks--;
			ss_mark("SOMVR_EXECUTE_MARKS", "the execution (the next frame)", t);
		}
	}
	/* the test words `execute where|face|chase on`: the nearest orc from the head (a headless player's aim and legs) */
	if (g_xc.chase && !g_xc.req_where)
		g_xc.req_where = 3;
	if (g_xc.req_where) {
		struct cb_orc oo[4];
		int no = pcomb && root ? cb_orcs_r(pcomb, pc, root, 1500.0f, oo, 4) : 0;
		sv_vec3 hp;
		sv_quat hq;
		somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq); /* the tracked head, as THREAT's yaw= */
		float head[3] = { hp.x, hp.y, hp.z }, hqv[4] = { hq.x, hq.y, hq.z, hq.w }, l[3] = { 0, 0, 1 };
		if (no)
			somvr_th_local(head, hqv, oo[0].pos, l);
		float br = no ? somvr_th_direction(l).yaw_deg : 0;
		int face = g_xc.req_where == 2, chase = g_xc.req_where == 3;
		int steps = (face || (chase && fabsf(br) > 20 && t - g_xc.chase_snap_t > 0.3)) && no
			    ? (int)lrintf(br / (g_rigcfg.snap_rad * 57.29578f)) : 0;
		if (steps) /* the snap turns toward it at once (the rig applies the pending steps together) */
			atomic_fetch_add(&g_req_snap, steps), g_xc.chase_snap_t = t, g_xc.chase_snaps += chase;
		if (chase) { /* the test stick: forward while it is over 1.15 m away and ahead (the stick word's own variables) */
			float fw = no && oo[0].dist > 115 && fabsf(br) < 45 ? 1.0f : 0;
			uint32_t b0 = 0, b1;
			memcpy(&b1, &fw, 4);
			atomic_store_explicit(&g_tstick[0], b0, memory_order_relaxed);
			atomic_store_explicit(&g_tstick[1], b1, memory_order_relaxed);
			atomic_store_explicit(&g_tstick[2], b0, memory_order_relaxed);
			atomic_store_explicit(&g_tstick_until_ns, fw > 0 ? (int64_t)((mono() + 0.2) * 1e9) : 0, memory_order_release);
			g_xc.chase_walks += fw > 0;
		} else {
			logf_("EXECUTE where t=%.3f: %d orcs within 15 m; the nearest %#lx dist %.0f cm bearing %+.0f deg (from the head, "
			      "+ = right); snap steps %d", t, no, no ? (unsigned long)oo[0].c : 0ul, no ? oo[0].dist : 0.0f, br, steps);
		}
		g_xc.req_where = 0;
	}
	if (g_xc.req_status) {
		g_xc.req_status = 0;
		logf_("EXECUTE status t=%.3f: mode %d, the streak %d, charge %d of %d, enabled %u, charged %u, uses %u, ready %d, "
		      "trigger %d (%.2f), glow %s (handle %#lx), executions %llu", t, g_xc.mode, h.streak, h.charge, h.need,
		      h.enabled, h.charged, h.uses, ready, g_xc.trig, (double)hi->trigger, g_xc_glow_w[g_xc.glow],
		      (unsigned long)g_xc.fx_handle, (unsigned long long)g_xc.executions);
	}
}

/* ---- a contact that deals a hit: the execution, or 0 (cb_hit deals the ordinary one) ------------------------------- */
static int xc_contact(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier, const somvr_blade *b, double t)
{
	if (!g_xc.mode)
		return 0;
	int j = somvr_ex_judge(g_xc.ready, g_xc.trig, tier);
	if (j == SOMVR_EXJ_NO_TRIGGER)
		return 0;
	if (j != SOMVR_EXJ_EXECUTE) {
		g_xc.refused[j]++;
		if (t - g_xc.refuse_t > 0.5)
			logf_("EXECUTE refused t=%.3f on orc %#lx: %s (tier %s, the streak %d, the charge %d of %d): the ordinary hit",
			      t, (unsigned long)o->c, somvr_ex_judge_name(j), sc_tier_name(tier), g_xc.hc.streak, g_xc.hc.charge,
			      g_xc.hc.need);
		g_xc.refuse_t = t;
		return 0;
	}
	float oq[4] = { 0, 0, 0, 1 }, off = 0;
	float yaw = peek(o->hobj + OBJ_ROT, oq, sizeof oq) && vec_ok(oq, 2.0f) ? sv_heading(sv_qnormalize(sv_quat_of(oq))) : 0;
	int behind = somvr_ex_behind(ct->point, o->pos, yaw, g_xc.back_deg, &off);
	char nm[96];
	struct xc_rec *e = NULL;
	for (int k = 0; k < XC_RECS && !(e && e->state > 0); k++) { /* the family's next record the database has */
		if (!somvr_ex_pick(behind ? g_xc.back : g_xc.front, g_xc.turn[behind]++, nm, sizeof nm))
			break;
		e = xc_rec_of(nm);
	}
	if (g_xc.mode == XC_LOG || !g_cb.ok_hit || g_cb.mode != CB_FULL || !pc || !e || e->state <= 0) {
		if (!e || e->state <= 0)
			g_xc.norec++;
		logf_("EXECUTE t=%.3f on orc %#lx from %s (%+.0f deg): %s - the ordinary hit", t, (unsigned long)o->c,
		      behind ? "behind" : "the front", off, g_xc.mode == XC_LOG ? "log only" : !e || e->state <= 0 ?
		      "no execution record in the database" : "combat cannot deal hits (see its CONFIG)");
		return 0;
	}
	/* build 22 (SOMVR_CAPTAIN_RULES): a captain's traits - immune to combat finishers: no execution (the charge kept, the
	 * ordinary hit); vulnerable: the kill; else the record's own damage through the game's rules (no forceKill) */
	int fk = kp_force_kill(o->c, SOMVR_CPF_COMBAT, "the execution", t);
	if (fk < 0)
		return 0;
	g_xc.executions++;
	/* 1. the charge spent as the stock start spends it, before the hit (the hit then counts as one, as the stock) */
	uintptr_t hc = peek_ptr(pc + FI_C_HITCTR);
	somvr_ex_hc h0 = g_xc.hc, h1;
	int16_t cto;
	uint8_t uto;
	int sp = g_xc.ok_spend && hc ? somvr_ex_spend(&h0, &cto, &uto) : 0;
	if (sp == 1) {
		*(volatile uint8_t *)(hc + XC_HC_USES) = uto;
		g_xc.spent_use++;
	} else if (sp == 2) {
		XC_FN(XCS_CHARGE, xc_charge_fn)(hc, 0);
		g_xc.spent_charge++;
	}
	if (g_xc.ok_spend && hc && e->reset == 1) {
		XC_FN(XCS_CLEAR, xc_clear_fn)(hc);
		g_xc.cleared++;
	}
	xc_read_hc(pc, &h1);
	/* 2. the hit: the stock execution record, forceKill (re-combat.md 1.1, 1.6) */
	float v = sqrtf(b->vel_tip[0] * b->vel_tip[0] + b->vel_tip[1] * b->vel_tip[1] + b->vel_tip[2] * b->vel_tip[2]);
	float dir[3] = { 0, 0, 1 }, hd0[3];
	if (v > 1.0f)
		dir[0] = b->vel_tip[0] / v, dir[1] = b->vel_tip[1] / v, dir[2] = b->vel_tip[2] / v;
	unsigned char once = 0;
	peek(pc + C_HITDIR, hd0, sizeof hd0), peek(pc + C_HITONCE, &once, 1);
	memcpy((void *)(pc + C_HITDIR), dir, sizeof dir);
	*(volatile unsigned char *)(pc + C_HITONCE) = 0;
	uintptr_t dslot = 0;
	uint64_t dwas = 0;
	char death[160] = "the record's own RecoilTypeHit";
	if (g_xc.death == XC_DEATH_COMBAT && g_kd.mode == KD_TYPE) { /* SOMVR_COMBAT_DEATH's type for this kill too */
		uintptr_t ad = g_df.ok_ad ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(e->rec) : 0;
		uintptr_t sl = ad ? kx_slot(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0, r = kx_find(g_kd.type);
		uint64_t want = (uint64_t)r;
		if (sl && r && peek(sl, &dwas, 8) && (dwas == want || arm_poke(sl, &want, 8))) {
			dslot = dwas == want ? 0 : sl;
			snprintf(death, sizeof death, "RecoilTypeHit '%s' (SOMVR_COMBAT_DEATH)", g_kd.type);
		} else {
			snprintf(death, sizeof death, "no '%s' (%s): the record's own", g_kd.type,
				 !sl ? "no RecoilTypeHit slot" : !r ? "not a known recoil type" : "not written");
		}
	}
	snprintf(g_kd.note, sizeof g_kd.note, "the execution: %s", death);
	g_kd.pre_c = o->c, g_kd.pre_dead = cb_dead(o->c);
	int dead0 = cb_dead(o->c);
	float hp0 = cb_health(o->c);
	double t0 = mono();
	struct kp_floor kf; /* build 22: a captain not vulnerable to it survives it (a third of his max health taken) */
	kp_floor_on(o->c, fk, "the execution", t, &kf);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ ct->point[0], ct->point[1], ct->point[2] }, pc, o->hobj, e->rec, fk);
	kp_floor_off(&kf);
	double t1 = mono();
	kx_hit_back(dslot, dwas);
	memcpy((void *)(pc + C_HITDIR), hd0, sizeof hd0);
	*(volatile unsigned char *)(pc + C_HITONCE) = once;
	int dead1 = cb_dead(o->c);
	float hp1 = cb_health(o->c);
	/* SOMVR_EXECUTE_SEVER=key: a record that severs (its SeverTarget: the _Sever family, 'Head') does it in the stock
	 * game by its animation's SEVER key, which our hit does not play; the game's own sever with that type, as the key
	 * calls it, the blade's direction as the velocity (the death handler's sever-on-kill did not cut: run 100848) */
	char sev[160] = "";
	if (g_xc.sever && g_xc.ok_sever && e->sever && dead1 && !dead0 && peek_ptr(o->c + 0x90)) {
		float vel[3] = { dir[0] * 300.0f, dir[1] * 300.0f + 150.0f, dir[2] * 300.0f }, rot[3] = { 0, 0, 0 };
		char sn[96] = "?";
		rx_rec_name(e->sever, sn, sizeof sn);
		XC_FN(XCS_SEVER, xc_sever_fn)(o->c, e->sever, vel, rot, peek_ptr(pc + C_HOBJ));
		g_xc.severed++;
		snprintf(sev, sizeof sev, "; severed '%s' (the record's SeverTarget, as its SEVER key: FUN_026d4670, velocity "
			 "%.0f,%.0f,%.0f)", sn, vel[0], vel[1], vel[2]);
	}
	somvr_ex_hc h2;
	xc_read_hc(pc, &h2);
	if (dead1 && !dead0)
		g_xc.kills++, CB_COUNT(kills);
	else
		g_xc.not_killed++;
	CB_COUNT(calls);
	/* 3. the moment: the slow motion, a strong pulse, the death's trace, the eye trace, the marks */
	xc_slowmo(t);
	in_haptic_event(SOMVR_HAND_R, 1.0f, 0.3f, SOMVR_HAP_COMBAT);
	logf_("EXECUTE kill t=%.3f: Combat_DealHit(pos=%.1f,%.1f,%.1f, pc=%#lx, hobj=%#lx, rec='%s' %#lx, forceKill=%d) %.3f ms "
	      "dead=%d->%d health %.1f->%.1f; from %s (%+.0f deg off its facing), tier heavy at %.2f m/s, the trigger %.2f; the "
	      "streak %d charge %d/%d uses %u -> spent (%s%s) -> %d charge %d/%d -> after the hit %d charge %d/%d charged %u; "
	      "the death: %s%s", t, ct->point[0], ct->point[1], ct->point[2], (unsigned long)pc, (unsigned long)o->hobj, e->name,
	      (unsigned long)e->rec, fk, (t1 - t0) * 1e3, dead0, dead1, hp0, hp1, behind ? "behind" : "the front", off,
	      g_cb.sp.mean_mps, (double)g_inp.st.hand[SOMVR_HAND_R].trigger, h0.streak, h0.charge, h0.need, h0.uses,
	      sp == 1 ? "a use" : sp == 2 ? "the charge" : "nothing to spend", e->reset == 1 ? ", the counter cleared" : "",
	      h1.streak, h1.charge, h1.need, h2.streak, h2.charge, h2.need, h2.charged, death, sev);
	kb_note(o->c, o->hobj, e->rec, "an execution", t); /* COMBAT death orc ...: where the body goes */
	g_xc.trace_until = t + XC_TRACE_S;
	if (g_xc.marks > 0)
		g_xc.mark_at = t + g_xc.mark_s; /* the kill (SOMVR_EXECUTE_MARK_MS after it: default the next frame's eyes) */
	return 1;
}

/* ---- the eye trace (H1, eye 0): the view must not move but by the tracked head ---------------------------------------- */
static void xc_eye(const somvr_eye_out *e)
{
	double t = cb_now();
	if (!g_xc.mode || (t > g_xc.trace_until && t > g_xc.arm_until)) {
		g_xc.have_eye = 0;
		return;
	}
	const float *ep = e->view.pos;
	float step = g_xc.have_eye ? dist3(ep, g_xc.eye_last) : 0, rstep = g_xc.have_eye ? dist3(g_xc.root, g_xc.root_last) : 0;
	if (g_xc.have_eye)
		g_xc.max_eye_step = fmaxf(g_xc.max_eye_step, step), g_xc.max_root_step = fmaxf(g_xc.max_root_step, rstep);
	float yaw = sv_heading(sv_qnormalize(sv_quat_of(e->view.quat))) * 57.29578f;
	if (g_xc.trace_lines < 2000)
		logf_("EXECUTE f=%llu t=%.4f %s eye %.1f,%.1f,%.1f step %.2f cm yaw %.2f root %.1f,%.1f,%.1f step %.2f cm base yaw "
		      "%.2f slowmo %d", (unsigned long long)g_fr.frame, t, t <= g_xc.trace_until && g_xc.trace_until > 0 ?
		      "after" : "armed", ep[0], ep[1], ep[2], step, yaw, g_xc.root[0], g_xc.root[1], g_xc.root[2], rstep,
		      g_fr.base.yaw * 57.29578f, g_xc.smo_until > 0);
	g_xc.trace_lines++;
	memcpy(g_xc.eye_last, ep, sizeof g_xc.eye_last), memcpy(g_xc.root_last, g_xc.root, sizeof g_xc.root_last);
	g_xc.have_eye = 1;
}

/* ---- the word (test and live): `execute status|where|face|glow fx|game|off|slowmo X` ------------------------------------ */
static const char *xc_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (!strncmp(w, "status", 6)) {
		g_xc.req_status = 1;
		return "execute status";
	}
	if (!strncmp(w, "chase ", 6)) {
		if (!g_xc.test)
			return "execute chase (SOMVR_HANDS_TEST is off: ignored)";
		g_xc.chase = !strncmp(w + 6, "on", 2);
		if (!g_xc.chase)
			atomic_store_explicit(&g_tstick_until_ns, 0, memory_order_release);
		return g_xc.chase ? "execute chase on (a test: turn and walk to the nearest orc)" : "execute chase off";
	}
	if (!strncmp(w, "where", 5) || !strncmp(w, "face", 4)) {
		g_xc.req_where = w[0] == 'f' ? 2 : 1;
		return w[0] == 'f' ? "execute face (a test: snap turns toward the nearest orc)"
				   : "execute where (a test: the nearest orc's distance and bearing)";
	}
	if (!strncmp(w, "glow ", 5)) {
		for (int k = 0; k < 3; k++)
			if (!strncmp(w + 5, g_xc_glow_w[k], strlen(g_xc_glow_w[k]))) {
				g_xc.glow = k; /* (an FX up stops at the next frame's ready check only when not ready: stopped below) */
				return k == XC_GLOW_FX ? "execute glow fx" : k == XC_GLOW_GAME ? "execute glow game" : "execute glow off";
			}
		return "execute glow (fx|game|off: ignored)";
	}
	if (!strncmp(w, "slowmo ", 7)) {
		float s = strtof(w + 7, NULL);
		g_xc.slowmo = s >= 0 && s <= 1 ? s : g_xc.slowmo;
		return "execute slowmo";
	}
	return "execute (status|where|face|glow fx|game|off|slowmo X: ignored)";
}

static void xc_start(const struct site_spec *sites, int *bad)
{
	static const char *const mode_w[] = { "0", "1", "log" }, *const death_w[] = { "combat", "record" };
	memset(&g_xc, 0, sizeof g_xc);
	g_xc_sites = sites;
	g_xc.mode = env_word("SOMVR_EXECUTE", mode_w, 3, XC_OFF);
	if (!g_xc.mode)
		return;
	if (g_cb.mode == CB_OFF) {
		logf_("WARN SOMVR_EXECUTE=%s needs SOMVR_COMBAT: the executions stay off", mode_w[g_xc.mode]);
		g_xc.mode = XC_OFF;
		return;
	}
	g_xc.glow = env_word("SOMVR_EXECUTE_GLOW", g_xc_glow_w, 3, XC_GLOW_GAME);
	g_xc.death = env_word("SOMVR_EXECUTE_DEATH", death_w, 2, XC_DEATH_COMBAT);
	g_xc.sever = env_word("SOMVR_EXECUTE_SEVER", (const char *const[]){ "off", "key" }, 2, 1);
	g_xc.slowmo = env_float("SOMVR_EXECUTE_SLOWMO", 0.3f, 0, 1, bad);
	g_xc.slowmo_s = env_float("SOMVR_EXECUTE_SLOWMO_MS", 500, 50, 3000, bad) / 1000;
	g_xc.back_deg = env_float("SOMVR_EXECUTE_BACK_DEG", 100, 60, 180, bad);
	g_xc.marks = (int)env_float("SOMVR_EXECUTE_MARKS", 0, 0, 20, bad);
	g_xc.mark_s = env_float("SOMVR_EXECUTE_MARK_MS", 50, 0, 3000, bad) / 1000;
	g_xc.lapse_s = env_float("SOMVR_EXECUTE_LAPSE_S", 0, 0, 30, bad);
	g_xc.ready_s = env_float("SOMVR_EXECUTE_READY_S", 0, 0, 30, bad);
	const char *ht = getenv("SOMVR_HANDS_TEST");
	g_xc.test = ht && ht[0] == '1';
	const char *v = getenv("SOMVR_EXECUTE_GLOW_FX");
	snprintf(g_xc.glow_fx, sizeof g_xc.glow_fx, "%s", v && v[0] ? v : "fx\\characters\\player\\elf\\elf_lightchain.fxp");
	v = getenv("SOMVR_EXECUTE_FRONT");
	snprintf(g_xc.front, sizeof g_xc.front, "%s", v && v[0] ? v :
		 "PC_Bip_Standing_InstaKill_F,PC_Bip_Standing_InstaKill_F3,PC_Bip_Standing_InstaKill_F2_Sever,"
		 "PC_Bip_Standing_InstaKill_F4,PC_Bip_Standing_InstaKill_F5,PC_Bip_Standing_InstaKill_F7_Sever,"
		 "PC_Bip_Standing_InstaKill_F6,PC_Bip_Standing_InstaKill_F8,PC_Bip_Standing_InstaKill_F10_Sever,"
		 "PC_Bip_Standing_InstaKill_F9");
	v = getenv("SOMVR_EXECUTE_BACK");
	snprintf(g_xc.back, sizeof g_xc.back, "%s", v && v[0] ? v :
		 "PC_Bip_Standing_InstaKill_B,PC_Bip_Standing_InstaKill_B3_Sever,PC_Bip_Standing_InstaKill_B2,"
		 "PC_Bip_Standing_InstaKill_B4_Sever");
	int ok[XCS_N];
	char why[512];
	for (int i = 0; i < XCS_N; i++) {
		ok[i] = check_site(&sites[i], why, sizeof why) == 0;
		if (!ok[i])
			logf_("EXECUTE site %s %#lx MISMATCH: %s", sites[i].name, (unsigned long)sites[i].addr, why);
	}
	static const int fx_need[] = { FIS_FX_CSINIT, FIS_FX_CREATE, FIS_FX_CSFREE, FIS_FX_RELEASE, FIS_OBJSYS_MH, FIS_ITEM };
	g_xc.ok_fx = 1;
	for (size_t i = 0; i < sizeof fx_need / sizeof fx_need[0]; i++)
		g_xc.ok_fx &= check_site(&g_fi_sites[fx_need[i]], why, sizeof why) == 0;
	g_xc.ok_slowmo = check_site(&g_fi_sites[FIS_SLOWMO], why, sizeof why) == 0;
	g_xc.ok_hc = check_site(&g_fi_sites[FIS_HITCOUNT], why, sizeof why) == 0 && ok[XCS_CHARGE];
	g_xc.ok_spend = g_xc.ok_hc && ok[XCS_SPEND] && ok[XCS_CLEAR];
	g_xc.ok_sever = ok[XCS_SEVER];
	g_xc.ok_lapse = ok[XCS_LAPSE];
	if ((g_xc.lapse_s > 0 || g_xc.ready_s > 0) && !g_xc.ok_lapse)
		logf_("WARN SOMVR_EXECUTE_LAPSE_S / _READY_S: XC_LAPSE did not match (the timer's offsets unproven): the game's own "
		      "lapse");
	if (!g_xc.ok_hc) {
		logf_("WARN SOMVR_EXECUTE: FI_HITCOUNT or XC_CHARGE did not match (the counter's offsets unproven): the executions "
		      "stay off");
		g_xc.mode = XC_OFF;
		return;
	}
	g_xc.last_line = cb_now();
	char lapse[160] = "the game's";
	if (g_xc.ok_lapse && (g_xc.lapse_s > 0 || g_xc.ready_s > 0))
		snprintf(lapse, sizeof lapse, "%.1f s after the last hit, %.1f s once ready (never shorter than the game's)",
			 (double)g_xc.lapse_s, (double)(g_xc.ready_s > 0 ? g_xc.ready_s : g_xc.lapse_s));
	logf_("CONFIG execute mode=%s glow=%s ('%s'%s) death=%s sever=%s%s slowmo=%.2f for %.0f ms%s back=%.0f deg spend=%s lapse=%s marks=%d: ready = "
	      "the game's charge (the hit counter's need); a heavy hit with the right trigger held -> Combat_DealHit with the "
	      "stock execution record, forceKill; front '%s', back '%s'", mode_w[g_xc.mode], g_xc_glow_w[g_xc.glow],
	      g_xc.glow_fx, g_xc.ok_fx ? "" : ", WARN: an FX site did not match", g_xc.death ? "record" : "combat",
	      g_xc.sever ? "key" : "off", g_xc.sever && !g_xc.ok_sever ? " (WARN: XC_SEVER did not match: none)" : "",
	      (double)g_xc.slowmo, g_xc.slowmo_s * 1e3, g_xc.ok_slowmo ? "" : " (WARN: FI_SLOWMO did not match: none)",
	      g_xc.back_deg, g_xc.ok_spend ? "the stock rule" : "none (WARN: XC_SPEND or XC_CLEAR did not match)", lapse, g_xc.marks,
	      g_xc.front, g_xc.back);
}

static void xc_end_log(void)
{
	if (!g_xc.mode)
		return;
	xc_slowmo_end(cb_now(), 1);
	xc_fx_stop("the end");
	logf_("EXECUTE END: %llu frames; the streak at most %d, the charge at most %d (need %d); ready %llu times (off %llu); "
	      "executions %llu: killed %llu (severed %llu), not killed %llu, no record %llu; spent: the charge %llu, a use %llu, the counter "
	      "cleared %llu; refused with the trigger held: not ready %llu, not a heavy hit %llu; slow motion %llu (skipped "
	      "%llu, the game's own at its end %llu); glow fx %llu (failed %llu); the eye trace %llu frames: the largest eye step %.2f cm, root step %.2f cm; "
	      "the test chase: %llu snaps, %llu walking frames; the lapse: %llu delay writes, the streak ran out %llu times (%llu "
	      "on our delay), cleared before its timer %llu, the longest gap survived %.2f s (the game's delay %.2f s)",
	      (unsigned long long)g_xc.frames, g_xc.max_streak, g_xc.max_charge, g_xc.hc.need,
	      (unsigned long long)g_xc.ready_on, (unsigned long long)g_xc.ready_off, (unsigned long long)g_xc.executions,
	      (unsigned long long)g_xc.kills, (unsigned long long)g_xc.severed, (unsigned long long)g_xc.not_killed, (unsigned long long)g_xc.norec,
	      (unsigned long long)g_xc.spent_charge, (unsigned long long)g_xc.spent_use, (unsigned long long)g_xc.cleared,
	      (unsigned long long)g_xc.refused[SOMVR_EXJ_NOT_READY], (unsigned long long)g_xc.refused[SOMVR_EXJ_NOT_HEAVY],
	      (unsigned long long)g_xc.slowmo_n, (unsigned long long)g_xc.slowmo_skip, (unsigned long long)g_xc.slowmo_game,
	      (unsigned long long)g_xc.fx_on,
	      (unsigned long long)g_xc.fx_fail, (unsigned long long)g_xc.trace_lines, (double)g_xc.max_eye_step,
	      (double)g_xc.max_root_step, (unsigned long long)g_xc.chase_snaps, (unsigned long long)g_xc.chase_walks,
	      (unsigned long long)g_xc.lapse_writes, (unsigned long long)g_xc.lapsed, (unsigned long long)g_xc.lapsed_ours,
	      (unsigned long long)g_xc.cleared_early, g_xc.gap_max, g_xc.delay_game);
}
