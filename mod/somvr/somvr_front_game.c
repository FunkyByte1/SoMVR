/* somvr_front_game.c - front-first enemies, the game side (HG-5, SH-9; docs/re-xr-pipeline.md 42; included by somvr.c
 * last; the pure part is somvr_front.c). The fight circle (AICombatMgr, FUN_028d7b50) scores every orc that may attack
 * Talion, sorts them and grants the best; the first negative score ends the grants. After the game's score
 * (FR_SCORE) the VR rule judges each candidate against the HMD head (THREAT's frame): an orc out of view is vetoed
 * (score -50) until its warning has run SOMVR_FRONT_WARN_S; the warning = a THREAT "pre" track (the warning sound at the
 * orc, the edge glow on its side rising with the warning, one announce pulse on that side's hand).
 *   SOMVR_FRONT=0|log|1      (default 0: nothing patched). log: the hooks in, every judgement, grant, copy and damage
 *                           logged, nothing changed (the baseline). 1: the veto. Needs SOMVR_THREAT=1 (the head, the
 *                           cue, Talion's HOBJECTs).
 *   SOMVR_FRONT_VIEW_DEG     the in-view cone (default SOMVR_THREAT_VIEW_DEG, 110)
 *   SOMVR_FRONT_WARN_S=1.0   the warning before a grant from out of view
 *   SOMVR_FRONT_READY_S=2.0  how long a finished warning lets the orc be granted
 *   SOMVR_FRONT_QUIET_S=2.0  no new warning for an orc after an unused one
 *   SOMVR_FRONT_FAR_M=0      out-of-view orcs farther than this are not vetoed (0 = all)
 *   SOMVR_FRONT_MAX_WARN=1   warnings at a time
 *   SOMVR_FRONT_CAMCOPY=off|head  the AI's camera copy (CPlayerObj+0xb94..+0xbd3, FUN_027aa490) from the head after
 *                           the game's (FR_CAMCOPY_A/B); SOMVR_FRONT_FOV_H / _FOV_V 110 / 90 deg
 *   SOMVR_FRONT_LOCKOUT=1.0  x the slot's 0.25 s re-arm and 0.75 s disarm (two writable statics, once)
 *   SOMVR_FRONT_DAMAGE=1.0   x the attack damage at Talion (FR_DMG_ATK, patched when < 1 or with log);
 *                           SOMVR_FRONT_KEEP=1000: amounts from this on stay (scripted kills)
 *   SOMVR_FRONT_GAMELOG=0|1  the game's own Combat.Debug.AttackerSelection / .AIMgr bytes set, and
 *                           Combat.Value.LogFileName = RUN/combat-debug.log (a test)
 *   SOMVR_FRONT_ONECUE=1|0   one warning per attack from out of view (default 1): the pre track's sound and pulse are
 *                           the attack's only ones (THREAT's announce, its sound and its urgent pulse are skipped for the
 *                           warned attack), its edge glow held from the warning to the blow; 0 = round 1's (the pre,
 *                           then THREAT's own announce + sound + urgent pulse: 3 pulses and 2 sounds per blow)
 * Fail-soft: a site that does not match turns its part off; no fresh head (flat, paused, third person: the head is
 * older than 100 ms), no record free or another target than Talion = the game's own choice.
 * Words: `front status`, `front keep 0|1` (a test: the practice's spawns keep the previous orc).
 * Lines: `CONFIG front ...`, `FRONT score|veto|warn|ready|grant|attack|expired|camcopy|damage|lockout|d12 ...`,
 * `THREAT pre ...` (the warning's cue), `THREAT quiet ...` (THREAT's pulse/sound skipped: one warning), `FRONT cues ...`
 * (what the player got for one warned attack), `FRONT END ...`. */

/* FRONT_SITES_BEGIN (check_sites.py counts this block on its own; re-xr-pipeline.md 42.8. Patched: FR_SCORE (the
 * veto after AICombatMgr_ScoreAttacker), FR_GRANT (FUN_028cd4b0), FR_CAMCOPY_A/B (FUN_027aa490's two calls),
 * FR_DMG_ATK (DamageStruct_DoDamage from FUN_02810e80). Read only: FR_CAMTERM (the camera term's class test and the
 * copy's reader), FR_VF59 (the copy's layout), FR_REARM (the 0.25 s static), FR_DMG_ALL (DoDamage's tail jump: the
 * alternative, not patched). Order = FFS_*) */
static const struct site_spec g_ff_sites_game[] = {
SITE("FR_SCORE", K_CALL, 0x28d7d31, 0x28d7d1b, "8b 4c 24 10 45 89 e0 4c 89 fa 48 89 ee 44 88 54 24 08 41 83 c6 01 e8 6a 5a ff ff 44 39 b4 24 50", 0x28cd7a0, "41 57 41 56 41 55 41 54 55 53 48 89 fb 48 81 ec"),
SITE("FR_GRANT", K_CALL, 0x28d8295, 0x28d8281, "4c 89 c7 44 89 f6 88 44 24 2f 44 88 54 24 2e 4c 89 44 24 20 e8 16 52 ff ff 4c 8b 44 24 20", 0x28cd4b0, "48 83 bf 00 02 00 00 00 0f 84 9a 01 00 00 41 56"),
SITE("FR_CAMCOPY_A", K_CALL, 0x27add66, 0x27add60, "48 89 ee 48 89 df e8 25 c7 ff ff e9 24 ff ff ff", 0x27aa490, "48 8b 86 d0 02 00 00 f3 0f 10 80 90 04 00 00"),
SITE("FR_CAMCOPY_B", K_CALL, 0x27b6dd6, 0x27b6dd0, "48 89 ee 48 89 df e8 b5 36 ff ff e9 d4 fc ff ff", 0x27aa490, "48 8b 86 d0 02 00 00 f3 0f 10 80 90 04 00 00"),
SITE("FR_CAMTERM", K_CALL, 0x28ce4e4, 0x28ce4bd, "48 3d 40 70 c4 04 75 eb 4c 8b 6a 38 4d 85 ed 0f 84 97 00 00 00 4c 8b a3 80 01 00 00 4c 89 ee 49 8d bc 24 2c 0d 00 00 e8 47 9c 0d 00 84 c0 0f 84 b7 06 00 00 49 8b 45 00 48 8d b4 24 80 00 00 00 4c 89 ef ff 90 d8 01 00 00", 0x29a8130, "48 83 ec 38 48 8b 86 cc 0b 00 00 f3 0f 10 86 c8"),
SITE("FR_VF59", K_VSLOT, 0x3995b78, 0x3995b70, "f0 e3 68 02 00 00 00 00 b0 77 7b 02 00 00 00 00 f0 df 6a 02 00 00 00 00", 0x27b77b0, "0f 10 8f 94 0b 00 00 0f 11 0e f3 0f 10 87 a4 0b 00 00 f3 0f 11 46 10"),
SITE("FR_REARM", K_ENTRY, 0x28d6ef0, 0x28d6ef0, "53 80 bf b1 00 00 00 00 48 89 fb 74 63 48 8b 3d 0c 04 de 01 48 8b 07 ff 90 90 00 00 00 f3 0f 10 05 9b 72 37 02", 0x28d6ef0, "53 80 bf b1 00 00 00 00 48 89 fb 74 63 48 8b 3d"),
SITE("FR_DMG_ALL", K_JMP, 0x2763094, 0x2763081, "48 8b 46 38 48 85 c0 74 16 48 8b 77 20 48 89 fa 48 89 c7 e9 f7 47 51 fe", 0xc77890, "48 81 ec b8 00 00 00 48 8d 4c 24 48 48 8d 44 24"),
SITE("FR_DMG_ATK", K_CALL, 0x2811007, 0x2811000, "48 8b 73 08 48 89 ef e8 44 20 f5 ff", 0x2763050, "48 85 f6 74 4b 48 8b 46 48 48 85 c0 74 42 48 8b"),
};
/* FRONT_SITES_END */
enum { FFS_SCORE, FFS_GRANT, FFS_CAM_A, FFS_CAM_B, FFS_CAMTERM, FFS_VF59, FFS_REARM, FFS_DMG_ALL, FFS_DMG_ATK, FFS_N };
_Static_assert(sizeof g_ff_sites_game / sizeof g_ff_sites_game[0] == FFS_N, "front sites");
static const struct site_spec *g_ff_sites = g_ff_sites_game; /* selftest: stand-ins */
static struct { uintptr_t clock_pp, lock_a, lock_b, dbg_sel, dbg_aimgr; } g_ffg = {
	0x463e290, /* *(*(*0x463e290 + 8) + 0x14): the fight circle's clock (ms; re-xr-pipeline.md 42.2) */
	0x4c4e1b0, /* the re-arm's 0.25 s (FUN_028d6ef0; FR_REARM's movss) */
	0x4c4e1b4, /* the disarm's 0.75 s (FUN_028d6de0) */
	0x4c44458, /* Combat.Debug.AttackerSelection's value (re-gameplay.md 4) */
	0x4c444d8, /* Combat.Debug.AIMgr's value */
};
#define FF_E_ORC 0x180   /* attacker entry: the orc (a server character: +0x8 its HOBJECT, +0xd2c its position, cm) */
#define FF_E_TGT 0x1a0   /* its target's HOBJECT */
#define FF_E_DIST 0x208  /* the distance (cm) */
#define FF_E_SCORE 0x20c /* the score (float; < 0 = never granted) */
#define FF_E_GRANT 0x214 /* the grant's time (ms, 0 = none) */
#define FF_E_TIMER 0x224 /* the prio timer (u32 ms; run out = < now) */
#define FF_E_LEFT 0x22c  /* attacks left in the grant */
#define FF_E_RING 0x22e  /* its ring (1 Inner, 2 Middle, 3 Outer, 4 Special) */
#define FF_E_PRIO 0x23a  /* "Can Be Attacker" prio flag */
#define FF_T_HOBJ 0x70   /* target entry: the target's HOBJECT */
#define FF_O_HOBJ 0x8
#define FF_O_POS 0xd2c
#define FF_PL_COPY 0xb94 /* CPlayerObj: the AI's camera copy (0x40 bytes) */
#define FF_PL_D12 0xd12  /* CPlayerObj: FUN_027aa490's second call is skipped while set */
#define FF_DS_DAMAGER 0x20 /* DamageStruct: the damager's HOBJECT (DoDamage's tail jump passes it) */
#define FF_DS_AMOUNT 0x50
#define FF_FRESH_S 0.1   /* the head older than this: the game's own choice */
#define FF_HOLD_S 2.5    /* a warned grant's attack starts within this (0.05-0.95 s seen) or it is not the warned one */
typedef void (*ff_score_fn)(uintptr_t e, uintptr_t tgt, uintptr_t slot, uint64_t n, uint64_t ring);
typedef uint64_t (*ff_two_fn)(uintptr_t a, uintptr_t b);
enum { FF_OFF = 0, FF_LOG = 1, FF_ON = 2 };

struct ff_orc {
	uint64_t id;      /* the orc's HOBJECT (0 = free) */
	uintptr_t ch;     /* its server character */
	float pos[3], score, off, dist;
	int verdict, ring, prio;
	double seen_t, warn_t, grant_t, ready_t;
	int cue_due;      /* its warning started: the cue at the next stereo frame */
	int warned;       /* a warning of ours is on (for the expiry line) */
	int start_seen;   /* the grant's attack start was found (THREAT's track) */
	uint32_t grants;
	int grant_warned; /* its last grant came after a warning of ours: the attack it starts is the warned one */
	uintptr_t atk_c;  /* that attack's THREAT track (its Combat, its start's serial), its start and its impact (our clock:
			   * the track's own, which follows the orc's animation timer) */
	uint64_t atk_serial;
	double atk_t0, atk_end, impact_t;
	int pre_pulse, pre_sound; /* the warning's cue gave its pulse / sound (0: looked at before it was due) */
	int th_pulses, th_sounds, th_quiet; /* THREAT's own for the warned attack: given / skipped (ONECUE) */
	int hold_frames, cue_frames, summed;
	double edge_t0, edge_t1;  /* the first / last frame with this warning's edge (the pre's or the hold) */
};
struct ff_cnt {
	uint64_t calls, talion, other_tgt, stale, other_thread, verdict[SOMVR_FR_N], written, grants, grants_talion,
		grant_verdict[SOMVR_FR_N], starts, no_start, warn_cues, cam[2], cam_writes, cam_skips, dmg_calls, dmg_talion,
		dmg_scaled, dmg_other_damager, prewarned, quiet_pulses, quiet_sounds, warned_atk, warned_pulses,
		warned_sounds, warned_max_pulses, hold_frames, dmg_keyed, dmg_keyless, dmg_notrack;
	double start_lat_sum, start_lat_max, sc_sum[2], sc_min[2], sc_max[2];
	uint64_t sc_n[2];
};
static struct {
	int mode, camcopy, gamelog, onecue;
	somvr_front_cfg cfg;
	somvr_front f;
	float fov_h, fov_v, lockout, damage, keep;
	int ok[FFS_N], on_score, on_grant, on_cam[2], on_dmg;
	struct ff_orc o[SOMVR_FRONT_ORCS];
	float head[3], hq[4];
	double head_t;
	int head_ok, tid, once;
	float game_pos[3], game_q[4]; /* the game's own copy (FUN_027aa490's, before ours) */
	int view_half_ok;             /* ... seen once */
	uintptr_t pl;     /* the CPlayerObj FUN_027aa490 last ran for */
	int d12;
	double cam_next, cam_t0;
	uint64_t cam_last[2];
	struct ff_cnt n;
	uint32_t lines, vlines, elines;
	int lsec_n, vsec_n, esec_n;
	double lsec, vsec, esec;
	uint64_t suppressed;
} g_ff;
static pthread_mutex_t g_ff_mu = PTHREAD_MUTEX_INITIALIZER; /* g_ff's state: the AI tick, the frame, the words */
static void ff_summary(struct ff_orc *r, double t); /* one warned attack is over: what the player got (g_ff_mu held) */

static int ff_log_ok(double t)
{
	if (g_ff.lines < 200)
		return ++g_ff.lines;
	if (t >= g_ff.lsec + 1.0)
		g_ff.lsec = t, g_ff.lsec_n = 0;
	if (g_ff.lsec_n < 8)
		return ++g_ff.lsec_n;
	g_ff.suppressed++;
	return 0;
}
static int ff_ev_log_ok(double t) /* the events (warn, ready, grant, attack, expired): the first 400, then 20 a second */
{
	if (g_ff.elines < 400)
		return ++g_ff.elines;
	if (t >= g_ff.esec + 1.0)
		g_ff.esec = t, g_ff.esec_n = 0;
	if (g_ff.esec_n < 20)
		return ++g_ff.esec_n;
	g_ff.suppressed++;
	return 0;
}
static int ff_veto_log_ok(double t) /* the vetoes: the first 50, then 2 a second */
{
	if (g_ff.vlines < 50)
		return ++g_ff.vlines;
	if (t >= g_ff.vsec + 1.0)
		g_ff.vsec = t, g_ff.vsec_n = 0;
	if (g_ff.vsec_n < 2)
		return ++g_ff.vsec_n;
	g_ff.suppressed++;
	return 0;
}

static int ff_clock(uint32_t *now)
{
	uintptr_t m = peek_ptr(g_ffg.clock_pp), q = m ? peek_ptr(m + 8) : 0;
	return q && peek(q + 0x14, now, sizeof *now);
}

static struct ff_orc *ff_rec(uint64_t id, int make, double t) /* g_ff_mu held */
{
	struct ff_orc *fr = NULL, *old = NULL;
	for (int i = 0; i < SOMVR_FRONT_ORCS; i++) {
		struct ff_orc *r = &g_ff.o[i];
		if (r->id == id)
			return r;
		if (!r->id && !fr)
			fr = r;
		else if (r->id && t - r->seen_t > 10 && (!old || r->seen_t < old->seen_t))
			old = r;
	}
	if (!make || !(fr = fr ? fr : old))
		return NULL;
	memset(fr, 0, sizeof *fr);
	fr->id = id;
	return fr;
}

static void ff_fwd(const float q[4], float f[3]) /* R(q)(0,0,1) */
{
	f[0] = 2 * (q[0] * q[2] + q[1] * q[3]);
	f[1] = 2 * (q[1] * q[2] - q[0] * q[3]);
	f[2] = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
}

/* a warning started for orc id (the pass's end): its cue is due at the next stereo frame */
static void ff_started(uint64_t id, uint32_t now, double t) /* g_ff_mu held */
{
	struct ff_orc *r = ff_rec(id, 0, t);
	const somvr_front_orc *p = somvr_front_find(&g_ff.f, id);
	if (r && r->grant_warned && r->start_seen && !r->summed)
		ff_summary(r, t); /* the last warned attack's, before this warning resets it */
	if (r) {
		r->cue_due = 1, r->warned = 1, r->warn_t = t, r->ready_t = 0;
		r->pre_pulse = r->pre_sound = r->th_pulses = r->th_sounds = r->th_quiet = r->hold_frames = r->cue_frames = 0;
		r->grant_warned = 0, r->summed = 0, r->edge_t0 = r->edge_t1 = 0;
	}
	if (ff_ev_log_ok(t))
		logf_("FRONT warn t=%.3f orc=%#lx clock=%u off=%.0f deg dist=%.1f m score=%.2f%s (warnings now %d)", t,
		      (unsigned long)id, now, p ? p->off_deg : NAN, p ? p->dist_cm * 0.01f : NAN, p ? p->score : NAN,
		      r && r->prio ? " prio (the game sorts it first)" : "", somvr_front_warnings(&g_ff.f, &g_ff.cfg, now));
}

/* FR_SCORE: after AICombatMgr_ScoreAttacker(e, tgt, slot, n, ring) (the AI tick) */
static void ff_judge(uintptr_t e, uintptr_t tgt, uintptr_t slot)
{
	uintptr_t th = peek_ptr(tgt + FF_T_HOBJ);
	double t = cb_now();
	pthread_mutex_lock(&g_ff_mu);
	g_ff.n.calls++;
	if (!th || (th != g_th.ph[0] && th != g_th.ph[1])) {
		g_ff.n.other_tgt++;
		pthread_mutex_unlock(&g_ff_mu);
		return;
	}
	g_ff.n.talion++;
	if (g_ff.tid && my_tid() != g_ff.tid)
		g_ff.n.other_thread++;
	unsigned char b[0xc0]; /* the entry's +0x180 .. +0x23f */
	uintptr_t ch = 0;
	uint32_t now = 0, timer = 0;
	float pos[3], sc0 = NAN, gd = NAN;
	if (!peek(e + FF_E_ORC, b, sizeof b) || !ff_clock(&now)) {
		pthread_mutex_unlock(&g_ff_mu);
		return;
	}
	memcpy(&ch, b, 8), memcpy(&sc0, b + (FF_E_SCORE - FF_E_ORC), 4), memcpy(&gd, b + (FF_E_DIST - FF_E_ORC), 4);
	memcpy(&timer, b + (FF_E_TIMER - FF_E_ORC), 4);
	int ring = b[FF_E_RING - FF_E_ORC], prio = b[FF_E_PRIO - FF_E_ORC] && timer < now;
	uint64_t id = ch ? peek_ptr(ch + FF_O_HOBJ) : 0;
	if (!id || !peek(ch + FF_O_POS, pos, sizeof pos) || !vec_ok(pos, 1e7f)) {
		pthread_mutex_unlock(&g_ff_mu);
		return;
	}
	if (!g_ff.head_ok || !(t - g_ff.head_t < FF_FRESH_S)) { /* no fresh head: the game's own choice */
		g_ff.n.stale++;
		pthread_mutex_unlock(&g_ff_mu);
		return;
	}
	float l[3];
	somvr_th_local(g_ff.head, g_ff.hq, pos, l);
	somvr_th_dir d = somvr_th_direction(l);
	float sc = sc0;
	uint64_t started = 0;
	/* in view = THREAT's test (somvr_th_view_off: its bearing in the head's frame, |yaw|, not the full angle): +0xd2c
	 * is the orc's root, so a close orc straight ahead is 50 deg below the eyes (run 080611: vetoed at yaw -40, 1.4 m)
	 * - and seen */
	float vo = somvr_th_view_off(&g_th.cfg, d);
	int v = somvr_front_judge(&g_ff.f, &g_ff.cfg, slot, now, id, vo, d.dist_cm, prio, &sc, &started);
	if (started)
		ff_started(started, now, t);
	g_ff.n.verdict[v]++;
	if (sc0 >= 0) { /* the game's accepted totals, in view [0] / out of view [1] */
		int k = v != SOMVR_FR_IN_VIEW;
		if (!g_ff.n.sc_n[k] || sc0 < g_ff.n.sc_min[k])
			g_ff.n.sc_min[k] = sc0;
		if (!g_ff.n.sc_n[k] || sc0 > g_ff.n.sc_max[k])
			g_ff.n.sc_max[k] = sc0;
		g_ff.n.sc_sum[k] += sc0, g_ff.n.sc_n[k]++;
	}
	int wrote = 0;
	if (g_ff.mode == FF_ON && sc != sc0) {
		memcpy((void *)(e + FF_E_SCORE), &sc, sizeof sc);
		g_ff.n.written++, wrote = 1;
	}
	struct ff_orc *r = ff_rec(id, 1, t);
	if (r) {
		r->ch = ch, memcpy(r->pos, pos, sizeof pos), r->score = sc0, r->off = vo, r->dist = d.dist_cm;
		r->verdict = v, r->ring = ring, r->prio = prio, r->seen_t = t;
		if (v == SOMVR_FR_READY && r->warned && !(r->ready_t > 0)) {
			r->ready_t = t;
			if (ff_ev_log_ok(t))
				logf_("FRONT ready t=%.3f orc=%#lx after %.3f s of warning (clock %u)", t, (unsigned long)id, t - r->warn_t,
				      now);
		}
	}
	if (v == SOMVR_FR_WAIT || v == SOMVR_FR_VETO) {
		if (ff_veto_log_ok(t))
			logf_("FRONT veto t=%.3f orc=%#lx score=%.2f -> %.0f%s off=%.0f deg yaw=%+.0f dist=%.1f m ring=%d%s verdict=%s "
			      "clock=%u slot=%#lx", t, (unsigned long)id, sc0, (double)g_ff.cfg.veto_score, wrote ? "" : " (log only)",
			      d.off_deg, d.yaw_deg, d.dist_cm * 0.01f, ring, prio ? " prio" : "", somvr_front_verdict_name(v), now,
			      (unsigned long)slot);
	} else if (ff_log_ok(t)) {
		logf_("FRONT score t=%.3f orc=%#lx total=%.2f game_dist=%.1f m off=%.0f deg yaw=%+.0f dist=%.1f m ring=%d%s "
		      "verdict=%s clock=%u slot=%#lx", t, (unsigned long)id, sc0, gd * 0.01f, d.off_deg, d.yaw_deg,
		      d.dist_cm * 0.01f, ring, prio ? " prio" : "", somvr_front_verdict_name(v), now, (unsigned long)slot);
	}
	pthread_mutex_unlock(&g_ff_mu);
}

__attribute__((used)) void somvr_w_ffscore(uintptr_t e, uintptr_t tgt, uintptr_t slot, uint64_t n, uint64_t ring)
{
	((ff_score_fn)g_ff_sites[FFS_SCORE].target)(e, tgt, slot, n, ring);
	ff_judge(e, tgt, slot);
}

/* FR_GRANT: FUN_028cd4b0(entry, now) granted the orc its attack streak */
__attribute__((used)) uint64_t somvr_w_ffgrant(uintptr_t e, uintptr_t now)
{
	uint64_t rv = ((ff_two_fn)g_ff_sites[FFS_GRANT].target)(e, now);
	double t = cb_now();
	uintptr_t ch = peek_ptr(e + FF_E_ORC), tg = peek_ptr(e + FF_E_TGT);
	uint64_t id = ch ? peek_ptr(ch + FF_O_HOBJ) : 0;
	uint32_t gt = 0, clk = 0;
	uint8_t left = 0;
	peek(e + FF_E_GRANT, &gt, 4), peek(e + FF_E_LEFT, &left, 1);
	int clk_ok = ff_clock(&clk);
	pthread_mutex_lock(&g_ff_mu);
	g_ff.n.grants++;
	int at = tg && (tg == g_th.ph[0] || tg == g_th.ph[1]);
	if (at && id) {
		g_ff.n.grants_talion++;
		struct ff_orc *r = ff_rec(id, 0, t);
		const somvr_front_orc *p = somvr_front_find(&g_ff.f, id);
		int v = r ? r->verdict : SOMVR_FR_GAME;
		g_ff.n.grant_verdict[v]++;
		if (ff_ev_log_ok(t))
			logf_("FRONT grant t=%.3f orc=%#lx verdict=%s attacks=%u off=%.0f deg dist=%.1f m score=%.2f grant_ms=%u now=%u "
			      "clock=%u%s", t, (unsigned long)id, r ? somvr_front_verdict_name(v) : "unseen", left,
			      r ? r->off : NAN, r ? r->dist * 0.01f : NAN, r ? r->score : NAN, gt, (uint32_t)now,
			      clk_ok ? clk : 0, p && p->warning ? ", its warning used up" : "");
		if (r) {
			if (r->warned && ff_ev_log_ok(t))
				logf_("FRONT grant t=%.3f orc=%#lx came %.3f s after its warning started", t, (unsigned long)id,
				      t - r->warn_t);
			if (r->grant_warned && r->start_seen && !r->summed)
				ff_summary(r, t);
			r->grant_warned = r->warned, r->atk_c = 0, r->atk_serial = 0, r->atk_t0 = r->atk_end = r->impact_t = 0;
			r->warned = 0, r->grant_t = t, r->start_seen = 0, r->grants++;
		}
		somvr_front_granted(&g_ff.f, id);
	}
	pthread_mutex_unlock(&g_ff_mu);
	return rv;
}

/* FR_CAMCOPY_A/B: FUN_027aa490(CPlayerObj, the client player) wrote the AI's camera copy; with CAMCOPY=head ours over it */
static uint64_t ff_cam(int k, uintptr_t pl, uintptr_t cp)
{
	uint64_t rv = ((ff_two_fn)g_ff_sites[k ? FFS_CAM_B : FFS_CAM_A].target)(pl, cp);
	double t = cb_now();
	pthread_mutex_lock(&g_ff_mu);
	g_ff.n.cam[k]++;
	g_ff.pl = pl;
	somvr_front_camcopy game, ours;
	int have = peek(pl + FF_PL_COPY, &game, sizeof game), fresh = g_ff.head_ok && t - g_ff.head_t < FF_FRESH_S;
	if (have)
		memcpy(g_ff.game_pos, game.pos2, sizeof g_ff.game_pos), memcpy(g_ff.game_q, game.quat2, sizeof g_ff.game_q),
			g_ff.view_half_ok = 1;
	if (have && g_ff.camcopy && fresh) {
		somvr_front_camcopy_fill(&ours, g_ff.head, g_ff.hq, g_ff.fov_h, g_ff.fov_v);
		memcpy((void *)(pl + FF_PL_COPY), &ours, sizeof ours);
		g_ff.n.cam_writes++;
	} else if (g_ff.camcopy) {
		g_ff.n.cam_skips++;
	}
	if (have && t >= g_ff.cam_next) {
		double dt = g_ff.cam_t0 > 0 ? t - g_ff.cam_t0 : 0;
		float gf[3], hf[3];
		ff_fwd(game.quat, gf), ff_fwd(g_ff.hq, hf);
		logf_("FRONT camcopy t=%.3f calls A %llu B %llu (%.1f / %.1f a second) d12=%d; the game's pos %.0f,%.0f,%.0f fwd "
		      "%+.2f,%+.2f,%+.2f fov %.1f x %.1f deg; the head %.0f,%.0f,%.0f fwd %+.2f,%+.2f,%+.2f (%s): %s", t,
		      (unsigned long long)g_ff.n.cam[0], (unsigned long long)g_ff.n.cam[1],
		      dt > 0 ? (double)(g_ff.n.cam[0] - g_ff.cam_last[0]) / dt : 0.0,
		      dt > 0 ? (double)(g_ff.n.cam[1] - g_ff.cam_last[1]) / dt : 0.0, g_ff.d12, game.pos[0], game.pos[1],
		      game.pos[2], gf[0], gf[1], gf[2], game.fov[0] * 57.29578f, game.fov[1] * 57.29578f, g_ff.head[0],
		      g_ff.head[1], g_ff.head[2], hf[0], hf[1], hf[2], fresh ? "fresh" : "stale",
		      !g_ff.camcopy ? "the game's kept (CAMCOPY off)" : fresh ? "ours written" : "the game's kept (no fresh head)");
		g_ff.cam_next = t + 1.0, g_ff.cam_t0 = t, g_ff.cam_last[0] = g_ff.n.cam[0], g_ff.cam_last[1] = g_ff.n.cam[1];
	}
	pthread_mutex_unlock(&g_ff_mu);
	return rv;
}
__attribute__((used)) uint64_t somvr_w_ffcam_a(uintptr_t pl, uintptr_t cp) { return ff_cam(0, pl, cp); }
__attribute__((used)) uint64_t somvr_w_ffcam_b(uintptr_t pl, uintptr_t cp) { return ff_cam(1, pl, cp); }

/* FR_DMG_ATK: DamageStruct_DoDamage(ds, the target's HOBJECT) from FUN_02810e80 (all attack damage) */
__attribute__((used)) uint64_t somvr_w_ffdmg(uintptr_t ds, uintptr_t hobj)
{
	double t = cb_now();
	pthread_mutex_lock(&g_ff_mu);
	g_ff.n.dmg_calls++;
	float a = NAN;
	if (hobj && (hobj == g_th.ph[0] || hobj == g_th.ph[1]) && peek(ds + FF_DS_AMOUNT, &a, sizeof a)) {
		g_ff.n.dmg_talion++;
		uint64_t from = peek_ptr(ds + FF_DS_DAMAGER);
		struct ff_orc *r = from ? ff_rec(from, 0, t) : NULL;
		g_ff.n.dmg_other_damager += !r;
		float b = g_ff.damage < 1 ? somvr_front_damage(a, g_ff.damage, g_ff.keep) : a;
		if (b != a) {
			memcpy((void *)(ds + FF_DS_AMOUNT), &b, sizeof b);
			g_ff.n.dmg_scaled++;
		}
		/* the attacker's THREAT track now (game thread): a keyed blow at its impact, or a keyless move (round 2: they
		 * get no cue - does one ever hurt?) */
		char tk[96] = "not on the game thread";
		if (g_th.tid == my_tid()) {
			const struct th_track *k = NULL;
			for (int j = 0; j < TH_TRACKS && !k; j++)
				if (g_th.tr[j].c && from && g_th.tr[j].hobj == from)
					k = &g_th.tr[j];
			if (!k)
				snprintf(tk, sizeof tk, "none"), g_ff.n.dmg_notrack++;
			else if (somvr_th_keyless(&k->k))
				snprintf(tk, sizeof tk, "KEYLESS at %.2f s", t - k->t0), g_ff.n.dmg_keyless++;
			else
				snprintf(tk, sizeof tk, "keyed at %.2f s (impact %.2f s%s)", t - k->t0, k->k.impact,
					 k->k.ranged ? ", ranged" : ""), g_ff.n.dmg_keyed++;
		}
		if (ff_ev_log_ok(t))
			logf_("FRONT damage at Talion t=%.3f %.2f -> %.2f from %#lx (%s); its track: %s", t, a, b, (unsigned long)from,
			      r ? "a fight-circle orc" : "not a fight-circle candidate: an arrow? a prop?", tk);
	}
	pthread_mutex_unlock(&g_ff_mu);
	return ((ff_two_fn)g_ff_sites[FFS_DMG_ATK].target)(ds, hobj);
}

/* once, at the first stereo frame (the game's statics are set by then): the lockout scale, the game's debug logs */
static void ff_once(double t)
{
	if (g_ff.once)
		return;
	g_ff.once = 1;
	if (g_ff.lockout != 1.0f) {
		float a = 0, b = 0;
		int pa = page_prot(g_ffg.lock_a), ok = g_ff.ok[FFS_REARM] && peek(g_ffg.lock_a, &a, 4) && peek(g_ffg.lock_b, &b, 4) &&
						      pa >= 0 && (pa & PROT_WRITE) && fabsf(a - 0.25f) < 1e-4f && fabsf(b - 0.75f) < 1e-4f;
		if (ok) {
			float na = a * g_ff.lockout, nb = b * g_ff.lockout;
			memcpy((void *)g_ffg.lock_a, &na, 4), memcpy((void *)g_ffg.lock_b, &nb, 4);
		}
		logf_("FRONT lockout t=%.3f x%.2f: the re-arm %.3f s, the disarm %.3f s: %s", t, g_ff.lockout, a, b,
		      ok ? "written (x the scale)" : "NOT written (not 0.25 / 0.75, FR_REARM did not match or not writable)");
	}
	if (g_ff.gamelog) {
		uint8_t one = 1, v0 = 9, v1 = 9;
		int p0 = page_prot(g_ffg.dbg_sel), p1 = page_prot(g_ffg.dbg_aimgr);
		int ok = peek(g_ffg.dbg_sel, &v0, 1) && peek(g_ffg.dbg_aimgr, &v1, 1) && v0 <= 1 && v1 <= 1 && p0 >= 0 &&
			 (p0 & PROT_WRITE) && p1 >= 0 && (p1 & PROT_WRITE);
		if (ok)
			memcpy((void *)g_ffg.dbg_sel, &one, 1), memcpy((void *)g_ffg.dbg_aimgr, &one, 1);
		char dir[1024], cmd[1100], why[256] = "";
		snprintf(dir, sizeof dir, "%s", g_log_path);
		char *sl = strrchr(dir, '/');
		if (sl)
			*sl = 0;
		snprintf(cmd, sizeof cmd, "Combat.Value.LogFileName %s/combat-debug.log", sl ? dir : ".");
		int rc = es_run(cmd, why, sizeof why);
		logf_("FRONT gamelog t=%.3f Combat.Debug.AttackerSelection %u, .AIMgr %u -> %s; console '%s' %s%s", t, v0, v1,
		      ok ? "1" : "NOT set (not a bool or not writable)", cmd, rc == 0 ? "run" : "NOT run: ", rc == 0 ? "" : why);
	}
}

/* the arrivals and departures (SH-9; what the camera copy changes for the spawner and the off-screen despawn): every
 * 0.5 s the PlayerCombat bands' orcs; one not seen before (after the first pass) = an arrival, one gone that was not
 * dead = a departure; each logged with its angle off the head's view and off the game's own copy (the rig's heading) */
#define FF_WATCH 64
static struct { uintptr_t c; float pos[3]; double t0, t1; int dead, live; } g_ffw[FF_WATCH];
static struct { int passes; double next; uint64_t arrivals, departures, in_head, in_rig, far; } g_ffwn;
static float ff_off_deg(const float from[3], const float q[4], const float p[3]) /* off the view: THREAT's test's angle */
{
	float l[3];
	somvr_th_local(from, q, p, l);
	return somvr_th_view_off(&g_th.cfg, somvr_th_direction(l));
}
#define FF_ARRIVE_CM 6000.0f /* farther: an orc coming into the bands' range (they reach ~100 m), not a spawn */
static void ff_watch(uintptr_t pcomb, uintptr_t pc, const float head[3], const float hq[4], double t) /* g_ff_mu held */
{
	static unsigned char band[48 * PCB_SIZE]; /* game thread only */
	if (!pcomb || t < g_ffwn.next)
		return;
	g_ffwn.next = t + 0.5;
	for (int i = 0; i < FF_WATCH; i++)
		g_ffw[i].live = 0;
	int first = g_ffwn.passes++ == 0, half = g_ff.view_half_ok;
	float rq[4];
	memcpy(rq, g_ff.game_q, sizeof rq);
	for (int k = 0; k < 3; k++) {
		uintptr_t vb = pcomb + PC_BANDS + 0x18 * (uintptr_t)k, b = peek_ptr(vb), e = peek_ptr(vb + 8);
		size_t m = e > b && (e - b) % PCB_SIZE == 0 ? (size_t)(e - b) / PCB_SIZE : 0;
		if (m > 48)
			m = 48;
		if (!m || !peek(b, band, m * PCB_SIZE))
			continue;
		for (size_t i = 0; i < m; i++) {
			uintptr_t c;
			memcpy(&c, band + i * PCB_SIZE + PCB_COMBAT, sizeof c);
			struct cb_orc o;
			if (!c || c == pc || !cb_orc_get(c, &o))
				continue;
			int j, fr = -1;
			for (j = 0; j < FF_WATCH && g_ffw[j].c != c; j++)
				if (!g_ffw[j].c && fr < 0)
					fr = j;
			if (j < FF_WATCH) {
				g_ffw[j].live = 1, g_ffw[j].t1 = t, g_ffw[j].dead = cb_dead(c);
				memcpy(g_ffw[j].pos, o.pos, sizeof o.pos);
				continue;
			}
			if (fr < 0)
				continue;
			g_ffw[fr].c = c, g_ffw[fr].t0 = g_ffw[fr].t1 = t, g_ffw[fr].live = 1, g_ffw[fr].dead = cb_dead(c);
			memcpy(g_ffw[fr].pos, o.pos, sizeof o.pos);
			if (first)
				continue;
			if (!(dist3(o.pos, head) < FF_ARRIVE_CM)) {
				g_ffwn.far++;
				continue;
			}
			float oh = ff_off_deg(head, hq, o.pos), orr = half ? ff_off_deg(g_ff.game_pos, rq, o.pos) : NAN;
			g_ffwn.arrivals++, g_ffwn.in_head += oh <= g_ff.cfg.view_deg * 0.5f, g_ffwn.in_rig += orr <= g_ff.cfg.view_deg * 0.5f;
			if (ff_log_ok(t))
				logf_("FRONT arrival t=%.3f orc=%#lx hobj=%#lx dist=%.1f m off the head %.0f deg (%s), off the game's copy "
				      "%.0f deg (%s)%s", t, (unsigned long)c, (unsigned long)o.hobj, dist3(o.pos, head) * 0.01f, oh,
				      oh <= g_ff.cfg.view_deg * 0.5f ? "IN VIEW" : "out of view", orr,
				      orr <= g_ff.cfg.view_deg * 0.5f ? "in" : "out", cb_dead(c) ? " dead" : "");
		}
	}
	for (int i = 0; i < FF_WATCH; i++) {
		if (!g_ffw[i].c || g_ffw[i].live)
			continue;
		if (!g_ffw[i].dead && !first && dist3(g_ffw[i].pos, head) < FF_ARRIVE_CM) {
			float oh = ff_off_deg(head, hq, g_ffw[i].pos), orr = half ? ff_off_deg(g_ff.game_pos, rq, g_ffw[i].pos) : NAN;
			g_ffwn.departures++;
			if (ff_log_ok(t))
				logf_("FRONT departure t=%.3f orc=%#lx dist=%.1f m off the head %.0f deg (%s), off the game's copy %.0f deg "
				      "after %.1f s in the bands", t, (unsigned long)g_ffw[i].c, dist3(g_ffw[i].pos, head) * 0.01f, oh,
				      oh <= g_ff.cfg.view_deg * 0.5f ? "IN VIEW" : "out of view", orr, g_ffw[i].t1 - g_ffw[i].t0);
		}
		memset(&g_ffw[i], 0, sizeof g_ffw[i]);
	}
}

/* the warned attack's THREAT track (Combat c, start serial) still on: its impact on our clock (the track's t0 follows
 * the orc's animation timer: hit-stop, slow motion), else -1 (game thread) */
static double ff_track_impact(uintptr_t c, uint64_t serial)
{
	for (int j = 0; j < TH_TRACKS; j++)
		if (g_th.tr[j].c == c && g_th.tr[j].serial == serial)
			return g_th.tr[j].t0 + (g_th.tr[j].k.impact >= 0 ? g_th.tr[j].k.impact : 1.0);
	return -1;
}

/* one warned attack is over: what the player got for it (g_ff_mu held) */
static void ff_summary(struct ff_orc *r, double t)
{
	r->summed = 1;
	if (!r->atk_c)
		return; /* no attack start within 6 s: its own line */
	int pulses = r->pre_pulse + r->th_pulses, sounds = r->pre_sound + r->th_sounds;
	g_ff.n.warned_atk++, g_ff.n.warned_pulses += (uint64_t)pulses, g_ff.n.warned_sounds += (uint64_t)sounds;
	if ((uint64_t)pulses > g_ff.n.warned_max_pulses)
		g_ff.n.warned_max_pulses = (uint64_t)pulses;
	logf_("FRONT cues t=%.3f orc=%#lx (THREAT orc=%#lx): a warned attack, its warning %.2f s before the start and %.2f "
		      "s before the impact: pulses %d (the warning's %d + THREAT's %d), sounds %d (%d + %d), THREAT's skipped %d "
		      "(one warning); the edge %.2f..%.2f s after the warning's start (the warning's %d frames, held %d)", t,
		      (unsigned long)r->id, (unsigned long)r->atk_c, r->atk_t0 - r->warn_t, r->impact_t - r->warn_t, pulses,
		      r->pre_pulse, r->th_pulses, sounds, r->pre_sound, r->th_sounds, r->th_quiet,
		      r->edge_t0 > 0 ? r->edge_t0 - r->warn_t : -1.0, r->edge_t1 > 0 ? r->edge_t1 - r->warn_t : -1.0,
		      r->cue_frames, r->hold_frames);
}

/* SOMVR_THREAT's stereo frame (game thread, after its tracks): the head for the judge, the pass's end, the cues */
static void ff_cues(const float head[3], const float hq[4], double t, somvr_threat_frame *e, int haps[2], uintptr_t pcomb,
		    uintptr_t pc)
{
	if (!g_ff.mode)
		return;
	ff_once(t);
	uint32_t now = 0;
	int clk = ff_clock(&now);
	pthread_mutex_lock(&g_ff_mu);
	memcpy(g_ff.head, head, sizeof g_ff.head), memcpy(g_ff.hq, hq, sizeof g_ff.hq);
	g_ff.head_t = t, g_ff.head_ok = 1;
	if (!g_ff.tid)
		g_ff.tid = my_tid();
	if (clk) {
		uint64_t s = somvr_front_end_pass(&g_ff.f, &g_ff.cfg, now);
		if (s)
			ff_started(s, now, t);
	}
	ff_watch(pcomb, pc, head, hq, t);
	if (g_ff.pl) { /* +0xd12: FUN_027aa490's second call is skipped while it is set */
		uint8_t d = 0;
		if (peek(g_ff.pl + FF_PL_D12, &d, 1) && d != g_ff.d12) {
			if (ff_log_ok(t))
				logf_("FRONT d12 t=%.3f CPlayerObj %#lx +0xd12 %d -> %d", t, (unsigned long)g_ff.pl, g_ff.d12, d);
			g_ff.d12 = d;
		}
	}
	for (int i = 0; i < SOMVR_FRONT_ORCS; i++) {
		struct ff_orc *r = &g_ff.o[i];
		if (!r->id)
			continue;
		float p[3] = { r->pos[0], r->pos[1] + 40.0f, r->pos[2] }, l[3]; /* (+0xd2c: about the body's middle) */
		somvr_th_local(head, hq, p, l);
		somvr_th_dir d = somvr_th_direction(l);
		float lv = clk ? somvr_front_warn_level(&g_ff.f, &g_ff.cfg, r->id, now) : -1.f;
		int seen = somvr_th_view_off(&g_th.cfg, d) <= g_ff.cfg.view_deg * 0.5f;
		/* ONECUE: after a warned grant its edge stays (at the warning's end level) until that attack's impact, so the
		 * warning and the blow are one cue (THREAT's own edge rises over it in the last 0.8 s) */
		if (r->atk_c && !r->summed) { /* the warned attack's impact as its track runs; gone = over */
			double it = ff_track_impact(r->atk_c, r->atk_serial);
			r->atk_end = it > 0 ? it : fmin(r->atk_end, t);
		}
		int hold = g_ff.onecue && g_ff.mode == FF_ON && lv < 0 && r->grant_warned &&
			   (r->start_seen ? r->atk_c && t < r->atk_end : t - r->grant_t < FF_HOLD_S);
		if (lv >= 0 && g_ff.mode == FF_ON && seen) {
			r->cue_due = 0; /* looked at: seen, no cue */
		} else if (hold && !seen) {
			int side = somvr_th_side(d);
			for (int s = 0; s < 2; s++)
				if (side & (1 << s))
					e->edge[s] = fmaxf(e->edge[s], g_th.cfg.edge * 0.8f);
			r->hold_frames++, g_ff.n.hold_frames++, r->edge_t1 = t;
		} else if (lv >= 0 && g_ff.mode == FF_ON) { /* the warning's cue */
			int side = somvr_th_side(d);
			float edge = g_th.cfg.edge * (0.3f + 0.5f * lv);
			for (int s = 0; s < 2; s++)
				if (side & (1 << s))
					e->edge[s] = fmaxf(e->edge[s], edge);
			r->cue_frames++, r->edge_t1 = t;
			if (!(r->edge_t0 > 0))
				r->edge_t0 = t;
			if (r->cue_due) {
				r->cue_due = 0;
				g_ff.n.warn_cues++;
				for (int s = 0; s < 2; s++)
					if (side & (1 << s))
						haps[s] = haps[s] > SOMVR_THH_ANNOUNCE ? haps[s] : SOMVR_THH_ANNOUNCE;
				th_sound(p, head, t, (uintptr_t)r->id);
				r->pre_pulse = 1, r->pre_sound = g_th.sound[0] != 0;
				if (th_log_ok(t))
					logf_("THREAT pre orc=%#lx (FRONT warning) cue=edge side=%s yaw=%+.0f pitch=%+.0f off=%.0f deg dist=%.1f "
					      "m level=%.2f haptic=announce sound", (unsigned long)r->id, somvr_th_side_name(side), d.yaw_deg,
					      d.pitch_deg, d.off_deg, d.dist_cm * 0.01f, edge);
			}
		} else if (r->cue_due && lv < 0) {
			r->cue_due = 0;
		}
		const somvr_front_orc *p0 = somvr_front_find(&g_ff.f, r->id);
		if (r->warned && (!p0 || (!p0->warning && p0->quiet))) {
			r->warned = 0;
			if (ff_ev_log_ok(t))
				logf_("FRONT expired t=%.3f orc=%#lx: its warning ran %.2f s unused (quiet %.1f s)", t, (unsigned long)r->id,
				      t - r->warn_t, (double)g_ff.cfg.quiet_s);
		}
		if (r->grant_t > 0 && !r->start_seen) { /* the grant -> the attack's start (THREAT's track of that orc) */
			for (int j = 0; j < TH_TRACKS && !r->start_seen; j++) {
				struct th_track *tr = &g_th.tr[j];
				if (!tr->c || tr->t0 < r->grant_t - 0.05 || somvr_th_keyless(&tr->k) || /* a move, not the blow */
				    !(tr->hobj == r->id || (r->ch && peek_ptr(tr->c + C_OWNER) == r->ch)))
					continue;
				r->start_seen = 1;
				if (r->grant_warned)
					r->atk_c = tr->c, r->atk_serial = tr->serial, r->atk_t0 = tr->t0,
					r->impact_t = tr->t0 + (tr->k.impact >= 0 ? tr->k.impact : 1.0), r->atk_end = r->impact_t;
				double lat = tr->t0 - r->grant_t;
				g_ff.n.starts++, g_ff.n.start_lat_sum += lat;
				g_ff.n.start_lat_max = lat > g_ff.n.start_lat_max ? lat : g_ff.n.start_lat_max;
				if (ff_ev_log_ok(t))
					logf_("FRONT attack t=%.3f orc=%#lx (THREAT orc=%#lx) started %.3f s after its grant%s, impact %.3f s "
					      "after the start; the warning started %.3f s before the start", t, (unsigned long)r->id,
					      (unsigned long)tr->c, lat, r->warn_t > 0 && r->warn_t <= r->grant_t ? " (warned)" : "",
					      tr->k.impact, r->warn_t > 0 && r->warn_t <= r->grant_t ? tr->t0 - r->warn_t : -1.0);
			}
			if (!r->start_seen && t - r->grant_t > 6.0) {
				r->start_seen = 1;
				g_ff.n.no_start++;
				if (ff_ev_log_ok(t))
					logf_("FRONT attack t=%.3f orc=%#lx: no attack start within 6 s of its grant", t, (unsigned long)r->id);
			}
		}
		if (r->grant_warned && r->start_seen && !r->summed && (!r->atk_c || t > r->atk_end + 0.3))
			ff_summary(r, t);
		if (t - r->seen_t > 10 && !r->warned && (r->start_seen || !(r->grant_t > 0)))
			memset(r, 0, sizeof *r);
	}
	pthread_mutex_unlock(&g_ff_mu);
}

/* THREAT's track of orc (Combat c, HOBJECT hobj, started at t0) picked a pulse, a sound or LATE (game thread, th_frame):
 * if SOMVR_FRONT=1 warned about this attack (the orc's warning runs or is ready, or the attack is the one its warned
 * grant started within FF_HOLD_S), LATE is cleared, and with ONECUE the pulse and the sound the warning already gave
 * are skipped (one warning per attack) */
static void ff_prewarned(uintptr_t c, uintptr_t hobj, double t0, double t, somvr_th_pick *k)
{
	if (g_ff.mode != FF_ON)
		return;
	uintptr_t owner = c ? peek_ptr(c + C_OWNER) : 0;
	pthread_mutex_lock(&g_ff_mu);
	struct ff_orc *w = NULL;
	for (int i = 0; i < SOMVR_FRONT_ORCS && !w; i++) {
		struct ff_orc *r = &g_ff.o[i];
		if (r->id && (r->id == hobj || (owner && r->ch == owner)) && r->warn_t > 0 &&
		    (r->warned || (r->grant_warned && t0 >= r->grant_t - 0.05 && t0 <= r->grant_t + FF_HOLD_S)))
			w = r;
	}
	if (w) {
		double a = t - w->warn_t;
		if (k->late) {
			k->late = 0;
			g_ff.n.prewarned++;
			if (ff_ev_log_ok(t))
				logf_("FRONT prewarned t=%.3f THREAT orc=%#lx: not LATE, its FRONT warning started %.3f s ago", t,
				      (unsigned long)c, a);
		}
		int qh = g_ff.onecue && k->haptic && w->pre_pulse, qs = g_ff.onecue && k->sound && w->pre_sound;
		if (qh || qs) {
			w->th_quiet += qh + qs, g_ff.n.quiet_pulses += qh, g_ff.n.quiet_sounds += qs;
			if (th_log_ok(t))
				logf_("THREAT quiet orc=%#lx%s%s%s skipped: FRONT warned it %.3f s ago (one warning per attack), lead "
				      "%.3f s", (unsigned long)c, qh ? " haptic=" : "", qh ? somvr_th_haptic_name(k->haptic) : "",
				      qs ? " sound" : "", a, k->lead);
			if (qh)
				k->haptic = 0;
			if (qs)
				k->sound = 0;
		}
		w->th_pulses += k->haptic != 0, w->th_sounds += k->sound;
	}
	pthread_mutex_unlock(&g_ff_mu);
}

static void ff_status(const char *why)
{
	double t = cb_now();
	uint32_t now = 0;
	ff_clock(&now);
	pthread_mutex_lock(&g_ff_mu);
	logf_("FRONT status t=%.3f (%s) mode=%d clock=%u calls=%llu talion=%llu stale=%llu in-view=%llu ready=%llu wait=%llu "
	      "veto=%llu game=%llu far=%llu full=%llu written=%llu grants=%llu (at Talion %llu) warnings=%llu (prio %llu) "
	      "cues=%llu expired=%llu used=%llu", t, why, g_ff.mode, now, (unsigned long long)g_ff.n.calls,
	      (unsigned long long)g_ff.n.talion, (unsigned long long)g_ff.n.stale,
	      (unsigned long long)g_ff.n.verdict[SOMVR_FR_IN_VIEW], (unsigned long long)g_ff.n.verdict[SOMVR_FR_READY],
	      (unsigned long long)g_ff.n.verdict[SOMVR_FR_WAIT], (unsigned long long)g_ff.n.verdict[SOMVR_FR_VETO],
	      (unsigned long long)g_ff.n.verdict[SOMVR_FR_GAME], (unsigned long long)g_ff.n.verdict[SOMVR_FR_FAR],
	      (unsigned long long)g_ff.n.verdict[SOMVR_FR_FULL], (unsigned long long)g_ff.n.written,
	      (unsigned long long)g_ff.n.grants, (unsigned long long)g_ff.n.grants_talion, (unsigned long long)g_ff.f.warnings,
	      (unsigned long long)g_ff.f.prio_warnings, (unsigned long long)g_ff.n.warn_cues,
	      (unsigned long long)g_ff.f.expired, (unsigned long long)g_ff.f.used);
	for (int i = 0; i < SOMVR_FRONT_ORCS; i++) {
		const struct ff_orc *r = &g_ff.o[i];
		if (r->id)
			logf_("FRONT status orc=%#lx verdict=%s score=%.2f off=%.0f deg dist=%.1f m ring=%d%s grants=%u seen %.2f s ago",
			      (unsigned long)r->id, somvr_front_verdict_name(r->verdict), r->score, r->off, r->dist * 0.01f, r->ring,
			      r->prio ? " prio" : "", r->grants, t - r->seen_t);
	}
	pthread_mutex_unlock(&g_ff_mu);
}

static const char *ff_word(const char *w)
{
	int v = -1;
	if (!strncmp(w, "status", 6))
		return ff_status("the word"), "front status";
	if (sscanf(w, "keep %d", &v) == 1 && (v == 0 || v == 1)) {
		g_pr_keep.on = v;
		return v ? "front keep 1 (a test: the practice's spawns keep the previous orc)"
			 : "front keep 0 (the kept orcs go with the next practice removal)";
	}
	return "front (not status|keep 0|1: ignored)";
}

/* somvr_start, after th_start (the head, the cue, Talion's HOBJECTs) */
static void ff_start(int *bad)
{
	pthread_mutex_lock(&g_ff_mu);
	memset(&g_ff, 0, sizeof g_ff);
	memset(g_ffw, 0, sizeof g_ffw), memset(&g_ffwn, 0, sizeof g_ffwn);
	pthread_mutex_unlock(&g_ff_mu);
	static const char *const mode_w[] = { "0", "log", "1" }, *const cam_w[] = { "off", "head" }, *const onoff_w[] = { "0", "1" };
	int mode = env_word("SOMVR_FRONT", mode_w, 3, 0);
	if (!mode)
		return;
	if (!g_th.on) {
		logf_("WARN SOMVR_FRONT needs SOMVR_THREAT=1 (the head pose, the warning's cue, Talion's HOBJECTs): nothing patched");
		return;
	}
	somvr_front_cfg *c = &g_ff.cfg;
	somvr_front_cfg_default(c);
	c->view_deg = env_float("SOMVR_FRONT_VIEW_DEG", g_th.cfg.view_deg, 40, 180, bad);
	c->warn_s = env_float("SOMVR_FRONT_WARN_S", c->warn_s, 0, 5, bad);
	c->ready_s = env_float("SOMVR_FRONT_READY_S", c->ready_s, 0.2f, 10, bad);
	c->quiet_s = env_float("SOMVR_FRONT_QUIET_S", c->quiet_s, 0, 10, bad);
	c->far_cm = 100 * env_float("SOMVR_FRONT_FAR_M", 0, 0, 100, bad);
	c->max_warn = (int)env_float("SOMVR_FRONT_MAX_WARN", (float)c->max_warn, 1, 4, bad);
	g_ff.camcopy = env_word("SOMVR_FRONT_CAMCOPY", cam_w, 2, 0);
	g_ff.fov_h = env_float("SOMVR_FRONT_FOV_H", 110, 20, 170, bad);
	g_ff.fov_v = env_float("SOMVR_FRONT_FOV_V", 90, 20, 170, bad);
	g_ff.lockout = env_float("SOMVR_FRONT_LOCKOUT", 1, 0.25f, 4, bad);
	g_ff.damage = env_float("SOMVR_FRONT_DAMAGE", 1, 0, 1, bad);
	g_ff.keep = env_float("SOMVR_FRONT_KEEP", 1000, 0, 1e6f, bad);
	g_ff.gamelog = env_word("SOMVR_FRONT_GAMELOG", onoff_w, 2, 0);
	g_ff.onecue = env_word("SOMVR_FRONT_ONECUE", onoff_w, 2, 1);
	somvr_front_init(&g_ff.f);
	char why[512], ln[FFS_N][48];
	for (int i = 0; i < FFS_N; i++) {
		g_ff.ok[i] = check_site(&g_ff_sites[i], why, sizeof why) == 0;
		snprintf(ln[i], sizeof ln[i], "%s %s", g_ff_sites[i].name, g_ff.ok[i] ? "ok" : "MISMATCH");
		if (!g_ff.ok[i])
			logf_("FRONT site %s %#lx -> %#lx MISMATCH: %s", g_ff_sites[i].name, (unsigned long)g_ff_sites[i].addr,
			      (unsigned long)g_ff_sites[i].target, why);
	}
	if (!g_ff.ok[FFS_CAMTERM] || !g_ff.ok[FFS_VF59]) {
		logf_("WARN SOMVR_FRONT: the read-only proofs FR_CAMTERM/FR_VF59 did not match (the entry's layout unproven): "
		      "nothing patched");
		return;
	}
	g_ff.mode = mode;
	/* the hooks: the judge and the grant together; the copy's calls (logged with CAMCOPY off: how often they run); the
	 * damage when scaled or logged */
	if (g_ff.ok[FFS_SCORE] && g_ff.ok[FFS_GRANT]) {
		g_ff.on_score = bo_patch_call(&g_ff_sites[FFS_SCORE], (void *)somvr_w_ffscore);
		g_ff.on_grant = g_ff.on_score && bo_patch_call(&g_ff_sites[FFS_GRANT], (void *)somvr_w_ffgrant);
	}
	g_ff.on_cam[0] = g_ff.ok[FFS_CAM_A] && bo_patch_call(&g_ff_sites[FFS_CAM_A], (void *)somvr_w_ffcam_a);
	g_ff.on_cam[1] = g_ff.ok[FFS_CAM_B] && bo_patch_call(&g_ff_sites[FFS_CAM_B], (void *)somvr_w_ffcam_b);
	if (g_ff.ok[FFS_DMG_ATK] && (g_ff.damage < 1 || mode == FF_LOG))
		g_ff.on_dmg = bo_patch_call(&g_ff_sites[FFS_DMG_ATK], (void *)somvr_w_ffdmg);
	if (g_ff.camcopy && !(g_ff.on_cam[0] || g_ff.on_cam[1]))
		g_ff.camcopy = 0;
	logf_("CONFIG front on=%d (%s) view_deg=%.0f warn_s=%.2f ready_s=%.2f quiet_s=%.2f far_m=%.0f max_warn=%d camcopy=%s "
	      "fov=%.0fx%.0f lockout=%.2f damage=%.2f keep=%.0f gamelog=%d onecue=%d; hooks: score %s, grant %s, camcopy A %s B "
	      "%s, damage "
	      "%s; sites: %s, %s, %s, %s, %s, %s, %s, %s, %s", mode, mode_w[mode], c->view_deg, c->warn_s, c->ready_s,
	      c->quiet_s, c->far_cm * 0.01f, c->max_warn, cam_w[g_ff.camcopy], g_ff.fov_h, g_ff.fov_v, g_ff.lockout, g_ff.damage,
	      g_ff.keep, g_ff.gamelog, g_ff.onecue, g_ff.on_score ? "in" : "OFF", g_ff.on_grant ? "in" : "OFF", g_ff.on_cam[0] ? "in" : "off",
	      g_ff.on_cam[1] ? "in" : "off", g_ff.on_dmg ? "in" : "off", ln[0], ln[1], ln[2], ln[3], ln[4], ln[5], ln[6], ln[7],
	      ln[8]);
	if (!g_ff.on_score || !g_ff.on_grant)
		logf_("WARN SOMVR_FRONT: FR_SCORE/FR_GRANT not both in: no veto (the game's own choice)");
}

static void ff_end_log(void)
{
	if (!g_ff.mode)
		return;
	ff_status("the end");
	pthread_mutex_lock(&g_ff_mu);
	const struct ff_cnt *n = &g_ff.n;
	logf_("FRONT END judged %llu (passes %llu) of %llu calls (Talion %llu, other targets %llu, no fresh head %llu, other "
	      "thread %llu); accepted totals in view %llu (%.2f..%.2f, mean %.2f), out of view %llu (%.2f..%.2f, mean %.2f); "
	      "grants at Talion %llu (in-view %llu, ready %llu, far %llu, full %llu, game %llu, wait %llu, veto %llu); attack "
	      "starts after a grant %llu (mean %.3f s, max %.3f s), none within 6 s %llu; warnings %llu (prio %llu, cues "
	      "%llu), expired %llu, used %llu; camcopy calls A %llu B %llu, written %llu, kept %llu; damage calls %llu, at "
	      "Talion %llu, scaled %llu, from no candidate %llu (the attacker's track keyed %llu, keyless %llu, none %llu); THREAT's LATE cleared (warned) %llu; warned attacks %llu (pulses "
	      "%.2f, max %llu, sounds %.2f per attack; THREAT's skipped: pulses %llu sounds %llu; edge held %llu frames); arrivals %llu (in the head's view %llu, in the game copy's "
	      "%llu; past 60 m %llu), departures %llu; lines suppressed %llu",
	      (unsigned long long)g_ff.f.judged, (unsigned long long)g_ff.f.passes, (unsigned long long)n->calls,
	      (unsigned long long)n->talion, (unsigned long long)n->other_tgt, (unsigned long long)n->stale,
	      (unsigned long long)n->other_thread, (unsigned long long)n->sc_n[0], n->sc_min[0], n->sc_max[0],
	      n->sc_n[0] ? n->sc_sum[0] / (double)n->sc_n[0] : 0.0, (unsigned long long)n->sc_n[1], n->sc_min[1], n->sc_max[1],
	      n->sc_n[1] ? n->sc_sum[1] / (double)n->sc_n[1] : 0.0, (unsigned long long)n->grants_talion,
	      (unsigned long long)n->grant_verdict[SOMVR_FR_IN_VIEW], (unsigned long long)n->grant_verdict[SOMVR_FR_READY],
	      (unsigned long long)n->grant_verdict[SOMVR_FR_FAR], (unsigned long long)n->grant_verdict[SOMVR_FR_FULL],
	      (unsigned long long)n->grant_verdict[SOMVR_FR_GAME], (unsigned long long)n->grant_verdict[SOMVR_FR_WAIT],
	      (unsigned long long)n->grant_verdict[SOMVR_FR_VETO], (unsigned long long)n->starts,
	      n->starts ? n->start_lat_sum / (double)n->starts : 0.0, n->start_lat_max, (unsigned long long)n->no_start,
	      (unsigned long long)g_ff.f.warnings, (unsigned long long)g_ff.f.prio_warnings, (unsigned long long)n->warn_cues,
	      (unsigned long long)g_ff.f.expired, (unsigned long long)g_ff.f.used, (unsigned long long)n->cam[0],
	      (unsigned long long)n->cam[1], (unsigned long long)n->cam_writes, (unsigned long long)n->cam_skips,
	      (unsigned long long)n->dmg_calls, (unsigned long long)n->dmg_talion, (unsigned long long)n->dmg_scaled,
	      (unsigned long long)n->dmg_other_damager, (unsigned long long)n->dmg_keyed,
	      (unsigned long long)n->dmg_keyless, (unsigned long long)n->dmg_notrack, (unsigned long long)n->prewarned, (unsigned long long)n->warned_atk,
	      n->warned_atk ? (double)n->warned_pulses / (double)n->warned_atk : 0.0, (unsigned long long)n->warned_max_pulses,
	      n->warned_atk ? (double)n->warned_sounds / (double)n->warned_atk : 0.0, (unsigned long long)n->quiet_pulses,
	      (unsigned long long)n->quiet_sounds, (unsigned long long)n->hold_frames, (unsigned long long)g_ffwn.arrivals, (unsigned long long)g_ffwn.in_head,
	      (unsigned long long)g_ffwn.in_rig, (unsigned long long)g_ffwn.far, (unsigned long long)g_ffwn.departures, (unsigned long long)g_ff.suppressed);
	pthread_mutex_unlock(&g_ff_mu);
}
