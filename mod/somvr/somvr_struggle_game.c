/* somvr_struggle_game.c - build 27 (som-b27-combat; docs/re-xr-pipeline.md 108.6): the death struggle's VR answer
 * (SOMVR_STRUGGLE). Not a translation unit of its own: somvr.c includes it last (it uses somvr.c's input tick, peek, the
 * rig, the haptics and the logs).
 *
 * When Talion goes down near an orc ('PC_RecoilAny_PreDeath', then the orc's '*_PreDeath' sync) the game starts a
 * struggle - the "last chance" - at PlayerCombat+0x8d8 (from a combat state, FUN_00e15690) or +0x8e0 (from a sync
 * action, FUN_00e16ef0) (re-m1m2.md 6.2). Each struggle class listens to one command through its OnCommandOn delegate
 * (re-xr-pipeline.md 108.6, static): CStrugglePressCount (vtable 0x3592ab8) counts presses of its command (+0x2f0) up to
 * its need (+0x2f4; the count +0x2f5, the phase +0x2f6: 0 waiting, 1 open, 2 over; +0x8c 1 = done by the presses);
 * CStruggleQuickTime (0x3592af8) one press of its command (+0xd0); CStruggleAim (0x3592bb8) a press of its command
 * (+0x58c) while its cursor is in the target; CStruggleHold (0x3592b38) and CStruggleInputDirection (0x3592b78): logged
 * only. The pad's buttons (A/B/X/Y pass through in a struggle) reach the command only through the binding the game
 * shows as a glyph; the VR player has no such button in mind (soak 2: 167 s on the knees).
 *
 * The answer: **a hard thrust of either hand** (its speed in the world over 80-250 ms past SOMVR_STRUGGLE_MPS, 2.0 m/s;
 * re-armed under a third of it) = one press of the struggle's own command (read from the object, our tap: 2 input ticks
 * on, 2 off; a thrust queued while one runs counts too). Stab or shove the orc off you: a mash is many thrusts, a timed
 * press is one. A pulse in that hand per press; at the struggle's start an urgent double pulse in both hands (now!).
 *   stg_hands (in_frame, after py_hands): the hands' history, the thrusts;
 *   stg_tick (the input tick, after st_tick): the struggle's object, its class and command, the taps, the logs;
 *   stg_config / stg_end_log (start / end). Lines: `CONFIG struggle`, `STRUGGLE t=...` (start with its fields and a
 *   dump, the thrusts, the presses, its fields as they change, the end and Talion's state), `STRUGGLE END`. */

/* Build 41 (som-b41-struggle, docs/re-xr-pipeline.md 180; GAME-STUDY item 1.4) - SOMVR_STRUGGLE_RULE=b41 (code default
 * b27 = all of the above unchanged): one VR rule for EVERY struggle on Talion, by its class (somvr_struggle.h):
 *   CStrugglePressCount (the caragor's takedown 'PC_Warg_Struggle_Takedown_*': 8 presses in ~2.5 s) - each hard thrust
 *     or pull of either hand (over SOMVR_STRUGGLE_MASH_MPS) one press of its command; a thump as the game counts it;
 *     the white ring at the struggle's node runs down a step per press left;
 *   CStruggleQuickTime - one hard thrust while its window is open (+0xd4; outside it nothing is pressed);
 *   CStruggleHold - a hand closed after the struggle began (a hand closed at its start opens first) and kept closed =
 *     its command held; the ring runs down over its +0x100 s while the window is open, a strain pulse in that hand;
 *   CStruggleInputDirection - a swipe of either hand (SOMVR_STRUGGLE_SWIPE_MPS, 15 cm) or a lean of the head
 *     (SOMVR_STRUGGLE_LEAN_CM) within SOMVR_STRUGGLE_DIR_DEG of the way the arrow at its node shows = MenuAxisX/Y sent
 *     for 4 ticks as the vector its own test takes (somvr_sg_dir_send); any other way is ignored (never a wrong press:
 *     the class fails on one), the stick is held neutral meanwhile;
 *   CStruggleAim - the ring's node (the struggle's UI data: W 0x1405abcd0, the node of Talion or of the other) outlined
 *     by the finisher's blue sword (its point short of the node, placed by somvr_fn_place); the blade laid on it and a
 *     thrust along it = completed as the game's own (+0xa5, its next update wins: build 27's write). No node (Linux,
 *     a record without one): the nearest orc's Neck; no orc: build 27's steer.
 *   A beast's domination (Talion's 'PC_Warg_Domination_*' / Troll / Graug) stays SOMVR_RIDE's grip; its ring runs
 *   down over SOMVR_RIDE_DOM_HOLD_MS at the beast's node while the hand holds.
 * The cue (SOMVR_STRUGGLE_CUE, objective layer SOMVR_OBF_STRUGGLE): a quad facing the head at the node (or 70 cm ahead
 * of the eyes): the timer ring's steps, or the arrow. Lines: `STRUGGLE t=.. b41 ...`, `STRUGGLE END build 41 ...`. */
#include "somvr_struggle.h"
#ifdef _WIN32 /* (Windows-only sites, win/sites-struggle.tsv: the ring's node of the struggle's UI data) */
static SITE_TABLE struct site_spec g_stg_sites_game[] = {
SITE("STG_UI_XF", K_ENTRY, 0, 0, "", 0, ""),   /* FUN_1405abcd0(struggle, LTRigidTransform *out) = L FUN_00e114f0(out, s) */
SITE("STG_UI_REC", K_ENTRY, 0, 0, "", 0, ""),  /* FUN_1405ab600(the struggle's record +0x78) -> its UI data */
SITE("STG_UI_NODE", K_ENTRY, 0, 0, "", 0, ""), /* FUN_1405ab090(UI data) -> the node's name (char *) */
SITE("STG_UI_TYPE", K_ENTRY, 0, 0, "", 0, ""), /* FUN_1405aaf70(UI data) -> ECombatStruggleUIPositionType */
};
enum { STGS_XF, STGS_REC, STGS_NODE, STGS_TYPE, STGS_N };
_Static_assert(sizeof g_stg_sites_game / sizeof g_stg_sites_game[0] == STGS_N, "struggle sites");
typedef float *(*stg_uixf_fn)(uintptr_t s, float *out);
typedef uintptr_t (*stg_uirec_fn)(uintptr_t rec);
typedef const char *(*stg_uinode_fn)(uintptr_t ui);
typedef int (*stg_uitype_fn)(uintptr_t ui);
#endif
#define STG_DIR_TICKS 4          /* a matched swipe: the vector held this many input ticks, then neutral again */
#define STG_CUE_M 0.22f          /* the cue's quad at most (m) */
#define STG_CUE_NEAR_CM 50.0f    /* ... never nearer the eyes than this */
#define STG_CUE_DEG 13.0f        /* ... this much of the view */
#define STG_WON_FLASH_S 0.45     /* the bright ring after a win */
#define STG_ANC_MAX_CM 400.0f    /* a node farther than this from the eyes is no anchor */
/* the finisher's outline (somvr_finisher_game.c, included after this file: its frames' helpers declared here) */
#define STG_BLADE_CM 95.0f       /* = FN_BLADE_CM: the outline's sword from the grip to the point */
#define STG_PREF_R 15.0f         /* = FN_PREF_R/U/A: the stab's natural grip (cm right, up, ahead of the eyes) */
#define STG_PREF_U -30.0f
#define STG_PREF_A 35.0f
#define STG_START_CM 15.0f       /* = somvr_fn_defaults' start_cm: the point this far short of the node */
#define STG_DEPTH_CM 22.0f       /* = its depth_cm: the stab's way into it (the placement's travel) */
static void fn_w2g(const float w[3], float g[3]);
static void fn_w2g_dir(const float w[3], float g[3]);
static void fn_h2w(const float h[3], float w[3]);
static void fn_w2h(const float w[3], float h[3]);
static void fn_euler(sv_quat q, float ang[3]);
static sv_quat fn_qalign(sv_vec3 a, sv_vec3 b);
/* the test aid (SOMVR_HANDS_TEST, `struggle auto on`): the test hands answer each struggle as a player's would, timed on
 * the game thread (the Windows build reads the trigger file every 1-2 s: a mash of 8 in 2.5 s cannot be words) */
#define STG_REST_L "-25 -35 25 0 0 0"
#define STG_REST_R "25 -35 25 0 0 0"
static void stg_auto(const unsigned char *f, double t);
static int stg_lineup(const somvr_blade *b, float out[6]);
static void stg_way_h(float w[3]);
static int stg_rel(float out[6]);
static void stg_stab_result(int r, const char *who, float dc, float ag, float al, double t);
#define STG_VT_PRESS GADDR(STG_VT_PRESS, 0x3592ab8)
#define STG_VT_QT GADDR(STG_VT_QT, 0x3592af8)
#define STG_VT_HOLD GADDR(STG_VT_HOLD, 0x3592b38)
#define STG_VT_DIR GADDR(STG_VT_DIR, 0x3592b78)
#define STG_VT_AIM GADDR(STG_VT_AIM, 0x3592bb8)
enum { STG_NONE = 0, STG_PRESS, STG_QT, STG_HOLD, STG_DIR, STG_AIM, STG_OTHER, STG_N };
static const char *const g_stg_kind[STG_N] = { "none", "CStrugglePressCount", "CStruggleQuickTime", "CStruggleHold",
					       "CStruggleInputDirection", "CStruggleAim", "unknown" };
#define STG_HIST 16

static struct {
	int on;                  /* SOMVR_STRUGGLE=thrust (1) | off (0) */
	float mps;               /* SOMVR_STRUGGLE_MPS */
	uintptr_t obj;           /* the struggle now (0 = none) */
	int kind, slot, cmd;     /* its class, 0 = the state's (+0x8d8) / 1 = the sync's (+0x8e0), its command */
	double t0, next_line;
	unsigned char f[0x600];  /* its last read (the change lines) */
	int have_f;
	struct { double t; sv_vec3 p; } h[2][STG_HIST];
	int nh[2], hpos[2], armed[2];
	int pending, tap_on, tap_off;
	float aim_cm;            /* SOMVR_STRUGGLE_AIM_CM: a hand this far from where it was at the start = the aim's full tilt */
	int aim_have, aim_hand;  /* CStruggleAim: the hand's aim now (MenuAxisX/Y, the cursor's analog direction) */
	float aim[2], aim0[2][2];/* the aim (x right, y up; -1..1); each hand's place at the start (cm, the head's right/up) */
	int aim0_ok[2];
	double aim_line;
	uint64_t n_aim_ticks, n_aim_pad, n_aim_outside;
	int aim_via;             /* SOMVR_STRUGGLE_AIM: 1 the pad's left stick, 2 MenuAxisX/Y, 3 both */
	int death;               /* the struggle is Talion's last chance (his attack '*PreDeath*'): ours to answer */
	int finisher;            /* build 27, SOMVR_STRUGGLE_FINISHER: a beaten captain's Eliminate ('PC_Bip_Finisher_*') too */
	int fin_now;             /* this struggle is that finisher's */
	uintptr_t fin_c;         /* its captain (the Last Words answered last) */
	int fin_stab;            /* a stab of the sword hand at him: the completion asked (stg_tick writes it) */
	float fin_cos;           /* SOMVR_STRUGGLE_FINISHER_DEG's cosine: how near the stab's line must point at him */
	uint64_t n_fin, n_fin_stabs, n_fin_won;
	uint64_t n_struggles, n_kind[STG_N], n_thrusts, n_thrusts_in, n_presses, n_nocmd, n_done;
	int lines;
	/* build 41 (SOMVR_STRUGGLE_RULE=b41): one rule for every struggle */
	int rule;                /* 0 b27, 1 b41 */
	int ours, beast, act;    /* answered by the rule (all but a beast's domination: SOMVR_RIDE's); somvr_sg_act */
	float mash_mps, cone_deg, lean_cm;
	int cue;                 /* SOMVR_STRUGGLE_CUE */
	somvr_sg_swipe_cfg swc;
	somvr_sg_swipe sw[2];
	int lean_armed, dir_ticks, dir_ok, cue_show_was;
	float dir_v[2], dir_now[2], head0[3], yaw0;
	int head0_ok;
	somvr_sg_hold hold;
	double hold_t, hold_hap_t;
	int sq_dn[2];
	somvr_sg_stab_cfg kcfg;
	somvr_sg_stab stab;
	_Atomic int aim_stab;    /* 1: the stab on the outline asked (stg_tick writes +0xa5), 2: written */
	int sites_ok;
	int anc_ok, anc_game, anc_type, anc_side; /* the anchor: known, from the game's UI node (else an orc's Neck) */
	float anc[3];
	char anc_node[48];
	double anc_t;
	int o_ok, o_fixed;       /* the outline placed; its point fixed in the world */
	float o_start[3], o_dir[3], o_start_cm, o_grip_g[3], o_q_g[4], o_anc0[3], o_handle[3];
	int blade_ok;            /* combat_frame had the drawn blade this frame (else the empty hand's stab, stg_hands) */
	int vh_logged;
	double o_line_t;
	double o_flash_until;
	int cue_show;
	uint32_t cue_id;
	float cue_pos[3], cue_roll;
	double cue_t, won_t;
	int made_was, made_need, last_hand, res_was;
	float held_need;
	_Atomic int req_status, req_pose, req_rule, req_auto; /* the test words (the writer thread) */
	_Atomic uint32_t req_auto_delay;                      /* `struggle auto on S`: the aim's stab S s after the line-up */
	float auto_delay;
	int auto_on, auto_step, lu_ok;
	double auto_t;
	float lu[6];             /* the right test hand that lays the drawn blade on the outline (`hands test R` numbers) */
	uint64_t n_auto;
	uint64_t n_rule[SOMVR_SGA_N], n_won41, n_lost41, n_anc_game, n_anc_orc, n_laid, n_stabs, n_stab_won, n_holds, n_swipes,
		n_swipe_ok, n_swipe_off, n_leans, n_dir_sent, n_qt_outside, n_counted, n_cue;
} g_stg = { .armed = { 1, 1 } };

static struct log_late g_stg_late;
/* the first 400, then (build 29, som-b29-soak2) 100 a 10 minutes: before, silent after ~20 last chances (17-22 lines
 * each in the rehearsals; re-xr-pipeline.md 129) */
static int stg_log_ok(void) { return g_stg.lines++ < 400 || log_late_ok(&g_stg_late, 100, mono()); }

/* Talion's attack record now (his Combat's: the PreDeath sync's own, 'PC_PreDeathRecover' when he gets up) */
static const char *stg_talion_attack(char *buf, size_t cap)
{
	uintptr_t pcomb = g_inp.pl ? peek_ptr(g_inp.pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	snprintf(buf, cap, "-");
	if (pc && vptr_of((void *)pc) == g_cbg.vt_combat)
		rx_rec_name(peek_ptr(pc + RX_C_ATTACK), buf, cap);
	return buf;
}

static void stg_config(void)
{
	int bad = 0;
	g_stg.on = env_word("SOMVR_STRUGGLE", (const char *const[]){ "off", "thrust" }, 2, 0);
	g_stg.mps = env_float("SOMVR_STRUGGLE_MPS", 2.0f, 0.8f, 6.0f, &bad);
	g_stg.aim_cm = env_float("SOMVR_STRUGGLE_AIM_CM", 20.0f, 8.0f, 60.0f, &bad);
	g_stg.aim_via = 1 + env_word("SOMVR_STRUGGLE_AIM", (const char *const[]){ "stick", "axes", "both" }, 3, 2);
	g_stg.finisher = env_word("SOMVR_STRUGGLE_FINISHER", (const char *const[]){ "off", "on" }, 2, 1); /* build 27 */
	g_stg.fin_cos = cosf(env_float("SOMVR_STRUGGLE_FINISHER_DEG", 40.0f, 10.0f, 90.0f, &bad) / 57.29578f);
	logf_("CONFIG struggle=%s: in the game's death struggle (its last chance) a hard thrust of either hand (over %.1f m/s) "
	      "= one press of the struggle's own command (a mash = many thrusts, a timed press = one); an urgent double "
	      "pulse at its start, a pulse per press (SOMVR_STRUGGLE, _MPS); an aim struggle (the cursor into the target) "
	      "steered by the hand that moves from where it was at its start (%.0f cm = full tilt, the head's right/up = the "
	      "cursor's: MenuAxisX/Y); the struggle's class, command and fields are logged either way",
	      g_stg.on ? "thrust" : "off", g_stg.mps, g_stg.aim_cm);
	logf_("CONFIG struggle build 27: finisher=%s (SOMVR_STRUGGLE_FINISHER%s): %s", g_stg.finisher ? "on" : "off",
	      getenv("SOMVR_STRUGGLE_FINISHER") ? "" : "; unset: on", g_stg.finisher ? "a beaten captain's Eliminate (Talion's "
	      "'PC_Bip_Finisher_*', the captain's own last chance) answered the same way, and the sword hand's thrust at him "
	      "(its line within SOMVR_STRUGGLE_FINISHER_DEG of him) = the kill (the struggle completed as the game's own)"
	      : "only Talion's own last chance "
	      "(build 26: the captain's finisher struggle left alone - lost, he gets up whole)");
	/* build 41 (re-xr-pipeline.md 180) */
	g_stg.rule = env_word("SOMVR_STRUGGLE_RULE", (const char *const[]){ "b27", "b41" }, 2, 0);
	g_stg.mash_mps = env_float("SOMVR_STRUGGLE_MASH_MPS", 1.5f, 0.6f, 6.0f, &bad);
	g_stg.cone_deg = env_float("SOMVR_STRUGGLE_DIR_DEG", 45.0f, 10.0f, 80.0f, &bad);
	g_stg.lean_cm = env_float("SOMVR_STRUGGLE_LEAN_CM", 15.0f, 0.0f, 60.0f, &bad);
	g_stg.cue = env_word("SOMVR_STRUGGLE_CUE", (const char *const[]){ "off", "on" }, 2, 1);
	somvr_sg_swipe_defaults(&g_stg.swc);
	g_stg.swc.min_mps = env_float("SOMVR_STRUGGLE_SWIPE_MPS", g_stg.swc.min_mps, 0.6f, 6.0f, &bad);
	somvr_sg_stab_defaults(&g_stg.kcfg);
	g_stg.kcfg.align_cm = env_float("SOMVR_STRUGGLE_AIM_ALIGN_CM", g_stg.kcfg.align_cm, 5.0f, 40.0f, &bad);
	g_stg.kcfg.align_deg = env_float("SOMVR_STRUGGLE_AIM_ALIGN_DEG", g_stg.kcfg.align_deg, 5.0f, 60.0f, &bad);
	char sw[200] = "";
#ifdef _WIN32
	g_stg.sites_ok = 1;
	for (int i = 0; i < STGS_N; i++) {
		char why[160];
		if (check_site(&g_stg_sites_game[i], why, sizeof why) != 0) {
			g_stg.sites_ok = 0;
			snprintf(sw, sizeof sw, "; site %s: %s - no node (an orc's Neck instead)", g_stg_sites_game[i].name, why);
			break;
		}
	}
	if (g_stg.sites_ok)
		snprintf(sw, sizeof sw, "; the ring's node from the struggle's UI data (STG_UI_XF %#llx)",
			 (unsigned long long)g_stg_sites_game[STGS_XF].target);
#else
	snprintf(sw, sizeof sw, "; the ring's node: none on Linux (an orc's Neck instead)");
#endif
	logf_("CONFIG struggle build 41: rule=%s (SOMVR_STRUGGLE_RULE%s)%s: %s; mash %.1f m/s, the swipe %.1f m/s / %.0f cm, the "
	      "lean %.0f cm, the way's cone %.0f deg, the stab laid within %.0f cm / %.0f deg; cue=%s%s",
	      g_stg.rule ? "b41" : "b27", getenv("SOMVR_STRUGGLE_RULE") ? "" : "; unset: b27", g_stg.on ? "" : " (SOMVR_STRUGGLE=off: "
	      "nothing answered)", g_stg.rule ? "every struggle on Talion by its class - press count: thrusts; quick time: one "
	      "thrust in its window; hold: a hand closed and kept closed; direction: a swipe or a lean the arrow's way; aim: "
	      "the blade laid on the outline at the ring's node and a thrust (a beast's domination stays SOMVR_RIDE's grip)"
	      : "build 27: only Talion's last chance (and a captain's finisher)", g_stg.mash_mps, g_stg.swc.min_mps,
	      g_stg.swc.min_cm, g_stg.lean_cm, g_stg.cone_deg, g_stg.kcfg.align_cm, g_stg.kcfg.align_deg, g_stg.cue ? "on" : "off", sw);
}

/* build 41: the struggle's class as somvr_struggle.h's */
static int stg_sg_kind(int k)
{
	return k == STG_PRESS ? SOMVR_SG_PRESS : k == STG_QT ? SOMVR_SG_QT : k == STG_HOLD ? SOMVR_SG_HOLD : k == STG_DIR ? SOMVR_SG_DIR
	       : k == STG_AIM ? SOMVR_SG_AIM : k == STG_NONE ? SOMVR_SG_NONE : SOMVR_SG_OTHER;
}

/* build 41: a beast's domination (SOMVR_RIDE answers it: the hand that took the beast held) */
static int stg_beast_dom(const char *atk)
{
	return atk && strstr(atk, "Domination") && !strstr(atk, "Fail") &&
	       (strstr(atk, "Warg") || strstr(atk, "Troll") || strstr(atk, "Graug"));
}

/* build 41: the ring's node of the struggle's UI data in the world (cm) - the game's own anchor of its widget (W
 * FUN_1405abcd0: the record's UI data, its node on Talion (+0x38) or the other (+0x70) by its position type and +0xa4);
 * called on the game thread from the input tick with the object just read and its vtable one of the five. 0 = none */
static int stg_anchor_game(uintptr_t obj, int first)
{
#ifdef _WIN32
	if (!g_stg.sites_ok || !obj)
		return 0;
	float xf[8] = { 0, 0, 0, 0, 0, 0, 1, 0 };
	((stg_uixf_fn)g_stg_sites_game[STGS_XF].target)(obj, xf);
	if (!vec_ok(xf, 1e7f) || (xf[0] == 0 && xf[1] == 0 && xf[2] == 0))
		return 0;
	memcpy(g_stg.anc, xf, sizeof g_stg.anc);
	if (first) { /* its name and position type, once per struggle (logs) */
		uintptr_t rec = peek_ptr(obj + 0x78);
		uintptr_t ui = rec ? ((stg_uirec_fn)g_stg_sites_game[STGS_REC].target)(rec) : 0;
		const char *nm = ui ? ((stg_uinode_fn)g_stg_sites_game[STGS_NODE].target)(ui) : NULL;
		g_stg.anc_type = ui ? ((stg_uitype_fn)g_stg_sites_game[STGS_TYPE].target)(ui) : -1;
		rx_str((uintptr_t)nm, g_stg.anc_node, sizeof g_stg.anc_node);
		uint8_t a4 = 0;
		peek(obj + 0xa4, &a4, 1);
		g_stg.anc_side = (g_stg.anc_type == 1) == (a4 != 0) ? 0 : 1; /* 0 the struggle's owner (+0x38), 1 the other (+0x70) */
	}
	return 1;
#else
	(void)obj, (void)first;
	return 0;
#endif
}

static int stg_kind_of(uintptr_t s)
{
	uintptr_t vt = s ? (uintptr_t)vptr_of((void *)s) : 0;
	return !s ? STG_NONE : vt == STG_VT_PRESS ? STG_PRESS : vt == STG_VT_QT ? STG_QT : vt == STG_VT_HOLD ? STG_HOLD
	       : vt == STG_VT_DIR ? STG_DIR : vt == STG_VT_AIM ? STG_AIM : STG_OTHER;
}

/* the class's command (the one its OnCommandOn compares with), -1 = not known for it */
static int stg_cmd_of(int kind, const unsigned char *f)
{
	int32_t c = -1;
	if (kind == STG_PRESS)
		memcpy(&c, f + 0x2f0, 4);
	else if (kind == STG_QT)
		memcpy(&c, f + 0xd0, 4);
	else if (kind == STG_AIM)
		memcpy(&c, f + 0x58c, 4);
	else if (kind == STG_HOLD && g_stg.rule) { /* build 41: the hold's command, a u16 (its OnCommandOn/Off compare it) */
		uint16_t w;
		memcpy(&w, f + 0xfc, 2);
		c = w;
	}
	return c > 0 && c < SC_NCMD ? c : -1;
}

/* the class's fields in a line (what decides it) */
static void stg_fields(int kind, const unsigned char *f, char *out, size_t cap)
{
	uint32_t a80, a84, a88, a8c;
	memcpy(&a80, f + 0x80, 4), memcpy(&a84, f + 0x84, 4), memcpy(&a88, f + 0x88, 4), memcpy(&a8c, f + 0x8c, 4);
	int n = snprintf(out, cap, "+80 %u +84 %u +88 %u result(+8c) %u +a0 %u +a2 %u +a5 %u", a80, a84, a88, a8c, f[0xa0], f[0xa2],
			 f[0xa5]);
	if (n < 0 || (size_t)n >= cap)
		return;
	if (kind == STG_PRESS)
		snprintf(out + n, cap - (size_t)n, "; presses %u of %u, phase %u (0 waits, 1 open, 2 over)", f[0x2f5], f[0x2f4], f[0x2f6]);
	else if (kind == STG_QT)
		snprintf(out + n, cap - (size_t)n, "; +d4 %u pressed(+d5) %u wrong(+d6) %u", f[0xd4], f[0xd5], f[0xd6]);
	else if (kind == STG_AIM) { /* its widget at +0xe8: the aim input (+0x474/+0x478 there), the cursor's speed (+0x45c) */
		float ax, ay, sp;
		memcpy(&ax, f + 0xe8 + 0x474, 4), memcpy(&ay, f + 0xe8 + 0x478, 4), memcpy(&sp, f + 0xe8 + 0x45c, 4);
		snprintf(out + n, cap - (size_t)n, "; +57a %u +57b %u done(+591) %u +592 %u; the widget's aim %+.2f %+.2f speed %.2f",
			 f[0x57a], f[0x57b], f[0x591], f[0x592], ax, ay, sp);
	} else if (kind == STG_HOLD) { /* build 41 (re-xr-pipeline.md 180.1) */
		uint16_t c;
		float need;
		memcpy(&c, f + 0xfc, 2), memcpy(&need, f + 0x100, 4);
		snprintf(out + n, cap - (size_t)n, "; its command %u, phase(+fe) %u (0 waits, 1 open, 2 over), the hold %.2f s, held(+118) %u",
			 c, f[0xfe], need, f[0x118]);
	} else if (kind == STG_DIR) {
		float a, tol;
		uint16_t ax;
		memcpy(&a, f + 0xa8, 4), memcpy(&tol, f + 0xac, 4), memcpy(&ax, f + 0xb0, 2);
		snprintf(out + n, cap - (size_t)n, "; the way %.3f rad (%s) within %.1f deg, its axis %u, phase(+b2) %u, held(+200) %u, "
			 "failed(+203) %u, a wrong way fails(+204) %u", a, somvr_sg_dir_word(a), tol * 57.29578f, ax, f[0xb2], f[0x200],
			 f[0x203], f[0x204]);
	}
}

/* in_frame: the hands' places (tracking space) -> the world's speed -> a thrust; their places against the head (world,
 * pw / hp / hq) -> an aim struggle's direction */
/* build 41: a direction struggle's gestures - each hand's swipe in the head's yaw frame (cm right, world up, relative to
 * the head: Talion's own motion is no swipe), the head's lean from where it was at the start (the tracked eyes) */
static void stg_dir_gesture(const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq, double t)
{
	uint8_t ph = g_stg.have_f ? g_stg.f[0xb2] : 0, failed = g_stg.have_f ? g_stg.f[0x203] : 0;
	float target, tol;
	memcpy(&target, g_stg.f + 0xa8, 4), memcpy(&tol, g_stg.f + 0xac, 4);
	sv_vec3 rw = sv_qrot(sv_qnormalize(hq), (sv_vec3){ 1, 0, 0 }); /* the head's right (build 27's aim: its +X), flat */
	float rl = hypotf(rw.x, rw.z), rx = rl > 1e-4f ? rw.x / rl : 1, rz = rl > 1e-4f ? rw.z / rl : 0;
	float g[2] = { 0, 0 }, mps = 0, off = 180;
	int have = 0, hand = -1, lean = 0;
	for (int h = 0; h < 2; h++) {
		if (!g_inp.hands[h].valid || !isfinite(pw[h].x + pw[h].y + pw[h].z + hp.x)) {
			somvr_sg_swipe_init(&g_stg.sw[h]);
			continue;
		}
		float ox = pw[h].x - hp.x, oz = pw[h].z - hp.z, x = ox * rx + oz * rz, y = pw[h].y - hp.y, d[2], m;
		if (somvr_sg_swipe_step(&g_stg.sw[h], &g_stg.swc, (float)(t - g_stg.t0), x, y, d, &m) && !have)
			have = 1, hand = h, g[0] = d[0], g[1] = d[1], mps = m;
	}
	const somvr_posef *ev = g_fr.views.pose; /* the eyes (reference space, m): the lean against the start */
	float e3[3] = { 50.0f * (ev[0].p[0] + ev[1].p[0]), 50.0f * (ev[0].p[1] + ev[1].p[1]), 50.0f * (ev[0].p[2] + ev[1].p[2]) };
	if (!have && g_stg.lean_cm > 0 && vec_ok(e3, 1e6f)) {
		if (!g_stg.head0_ok) {
			sv_vec3 f0 = sv_qrot(sv_qnormalize(sv_quat_of(ev[0].q)), (sv_vec3){ 0, 0, -1 });
			memcpy(g_stg.head0, e3, sizeof e3), g_stg.yaw0 = atan2f(f0.x, f0.z), g_stg.head0_ok = 1, g_stg.lean_armed = 1;
		} else {
			float dx = e3[0] - g_stg.head0[0], dz = e3[2] - g_stg.head0[2], dy = e3[1] - g_stg.head0[1];
			float lx = -dx * cosf(g_stg.yaw0) + dz * sinf(g_stg.yaw0), d[2]; /* (its right: -Z ahead, +X right) */
			if (hypotf(lx, dy) < 0.5f * g_stg.lean_cm)
				g_stg.lean_armed = 1;
			else if (g_stg.lean_armed && somvr_sg_lean(lx, dy, g_stg.lean_cm, d))
				have = 1, lean = 1, g_stg.lean_armed = 0, g[0] = d[0], g[1] = d[1], mps = hypotf(lx, dy);
		}
	}
	if (!have)
		return;
	int ok = somvr_sg_dir_match(g, target, g_stg.cone_deg, &off), open = ph == 1 && !failed;
	g_stg.n_swipes += !lean, g_stg.n_leans += lean;
	if (ok && open && !g_stg.dir_ticks) {
		g_stg.dir_ok = somvr_sg_dir_send(target, tol, g_stg.dir_v);
		g_stg.dir_ticks = g_stg.dir_ok ? STG_DIR_TICKS : 0, g_stg.n_swipe_ok += g_stg.dir_ok, g_stg.n_dir_sent += g_stg.dir_ok;
		if (g_stg.dir_ok)
			haptic_k(hand >= 0 ? hand : SOMVR_HAND_R, SOMVR_HX_THUMP, 0.6f);
	} else
		g_stg.n_swipe_off += !ok;
	if (stg_log_ok())
		logf_("STRUGGLE t=%.3f +%.2f s: b41 %s %s (%+.2f %+.2f, %s %.1f%s) - the shown way %s (%.0f deg off, its cone %.0f): %s",
		      t, t - g_stg.t0, lean ? "a lean of the head" : "a swipe of the", lean ? "" : hand ? "right hand" : "left hand",
		      g[0], g[1], lean ? "by" : "at", mps, lean ? " cm" : " m/s", somvr_sg_dir_word(target), off, g_stg.cone_deg,
		      !open ? "its window not open - nothing sent" : !ok ? "not that way - ignored (never a wrong way)"
		      : g_stg.dir_ok ? "sent (MenuAxisX/Y for 4 ticks: the vector its own test takes)"
		      : "its tolerance 0 - nothing it would take");
}

static void stg_hands(const sv_vec3 p[2], const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	double t = cb_now();
	if (g_stg.on && g_stg.rule && g_stg.obj && g_stg.ours && g_stg.act == SOMVR_SGA_SWIPE)
		stg_dir_gesture(pw, hp, hq, t);
	if (g_stg.on && g_stg.obj && (g_stg.rule ? g_stg.ours && g_stg.act == SOMVR_SGA_STEER : g_stg.death) && g_stg.kind == STG_AIM) {
		sv_quat qn = sv_qnormalize(hq);
		sv_vec3 r = sv_qrot(qn, (sv_vec3){ 1, 0, 0 }), u = sv_qrot(qn, (sv_vec3){ 0, 1, 0 });
		float best = 0;
		int bh = -1;
		float bd[2] = { 0, 0 };
		for (int hnd = 0; hnd < 2; hnd++) {
			if (!g_inp.hands[hnd].valid || !isfinite(pw[hnd].x + pw[hnd].y + pw[hnd].z + hp.x + r.x + u.x))
				continue;
			sv_vec3 o = { pw[hnd].x - hp.x, pw[hnd].y - hp.y, pw[hnd].z - hp.z };
			float x = o.x * r.x + o.y * r.y + o.z * r.z, y = o.x * u.x + o.y * u.y + o.z * u.z;
			if (!g_stg.aim0_ok[hnd]) {
				g_stg.aim0[hnd][0] = x, g_stg.aim0[hnd][1] = y, g_stg.aim0_ok[hnd] = 1;
				continue;
			}
			float dx = x - g_stg.aim0[hnd][0], dy = y - g_stg.aim0[hnd][1], d = hypotf(dx, dy);
			if (d > best)
				best = d, bh = hnd, bd[0] = dx, bd[1] = dy;
		}
		g_stg.aim_have = bh >= 0 && best >= 3.0f;
		if (g_stg.aim_have) {
			float k = 1.0f / g_stg.aim_cm, ax = bd[0] * k, ay = bd[1] * k, l = hypotf(ax, ay);
			if (l > 1)
				ax /= l, ay /= l;
			g_stg.aim[0] = ax, g_stg.aim[1] = ay, g_stg.aim_hand = bh;
			if (t >= g_stg.aim_line && stg_log_ok()) {
				logf_("STRUGGLE t=%.3f +%.2f s: the aim %+.2f %+.2f (the %s hand %+.0f cm right, %+.0f cm up of where it was "
				      "at the start)", t, t - g_stg.t0, ax, ay, bh ? "right" : "left", bd[0], bd[1]);
				g_stg.aim_line = t + 0.25;
			}
		}
	}
	for (int hnd = 0; hnd < 2; hnd++) {
		if (!g_inp.hands[hnd].valid || !isfinite(p[hnd].x + p[hnd].y + p[hnd].z)) {
			g_stg.nh[hnd] = 0;
			continue;
		}
		int pos = g_stg.hpos[hnd];
		g_stg.h[hnd][pos].t = t, g_stg.h[hnd][pos].p = p[hnd];
		g_stg.hpos[hnd] = (pos + 1) % STG_HIST, g_stg.nh[hnd] += g_stg.nh[hnd] < STG_HIST;
		if (!g_stg.obj || g_stg.nh[hnd] < 2)
			continue;
		int now = pos, k = -1;
		for (int j = 1; j < g_stg.nh[hnd]; j++) { /* the newest sample at least 80 ms older (at most 250) */
			int ix = (now - j + STG_HIST) % STG_HIST;
			double age = t - g_stg.h[hnd][ix].t;
			if (age > 0.25)
				break;
			if (age >= 0.08) {
				k = ix;
				break;
			}
		}
		if (k < 0)
			continue;
		double age = t - g_stg.h[hnd][k].t;
		sv_vec3 a = somvr_rig_point(&g_rig, g_fr.base, g_stg.h[hnd][k].p), b = somvr_rig_point(&g_rig, g_fr.base, p[hnd]);
		float sp = (float)(sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z)) / age);
		if (g_stg.rule && g_stg.on && g_stg.ours && g_stg.act == SOMVR_SGA_STAB && g_stg.o_ok && g_stg.o_fixed && !g_stg.blade_ok &&
		    hnd == SOMVR_HAND_R && !atomic_load(&g_stg.aim_stab)) { /* (b41: no blade drawn - the empty hand at the outline's
									      * grip, then a thrust along its way) */
			float v[3] = { (b.x - a.x) / (float)age, (b.y - a.y) / (float)age, (b.z - a.z) / (float)age }, hpnt[3] = { b.x, b.y, b.z };
			float dc, ag, al;
			int r = somvr_sg_stab_step(&g_stg.stab, &g_stg.kcfg, (float)(t - g_stg.t0), hpnt, g_stg.o_dir, v, g_stg.o_handle,
						   g_stg.o_dir, &dc, &ag, &al);
			stg_stab_result(r, "the empty hand", dc, ag, al, t);
		}
		float thr = g_stg.rule && g_stg.ours && g_stg.act == SOMVR_SGA_THRUSTS ? g_stg.mash_mps : g_stg.mps; /* (b41: a mash) */
		if (sp < thr * 100.0f / 3.0f)
			g_stg.armed[hnd] = 1;
		if (!g_stg.armed[hnd] || sp < thr * 100.0f || sp > 1500.0f) /* (faster: a tracking jump) */
			continue;
		g_stg.armed[hnd] = 0, g_stg.n_thrusts++;
		/* build 27: the finisher's struggle - a thrust of the sword hand along a line at the captain held before Talion
		 * (B&S: the kill is the player's own stab, not a ring on the HUD) completes it as the game's own (+0xa5) */
		struct cb_orc oc;
		if (g_stg.on && g_stg.fin_now && g_stg.fin_c && !g_stg.fin_stab && hnd == SOMVR_HAND_R && g_stg.kind == STG_AIM &&
		    (!g_stg.rule || g_stg.act == SOMVR_SGA_STEER) && cb_orc_get(g_stg.fin_c, &oc)) { /* (b41: the outline's stab first) */
			float v[3] = { b.x - a.x, b.y - a.y, b.z - a.z }, d[3] = { oc.pos[0] - a.x, oc.pos[1] + 100.0f - a.y,
				     oc.pos[2] - a.z };
			float lv = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), ld = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			float c = lv > 1 && ld > 1 ? (v[0] * d[0] + v[1] * d[1] + v[2] * d[2]) / (lv * ld) : -1;
			int at = c >= g_stg.fin_cos && ld < 400.0f;
			g_stg.fin_stab = at, g_stg.n_fin_stabs += (uint64_t)at;
			if (at)
				haptic_k(hnd, SOMVR_HX_HIT, 1);
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f the finisher: a thrust of the sword hand at %.1f m/s, %.0f deg off the line to the "
				      "captain %#llx (%.0f cm away): %s", t, sp * 0.01f, acosf(fmaxf(-1.0f, fminf(1.0f, c))) * 57.29578f,
				      (unsigned long long)g_stg.fin_c, ld, at ? "the stab - the kill asked (completed as the game's own)"
				      : "not at him");
		}
		int b41p = g_stg.rule && g_stg.ours && (g_stg.act == SOMVR_SGA_THRUSTS || g_stg.act == SOMVR_SGA_THRUST ||
							 g_stg.act == SOMVR_SGA_STEER); /* (b41: the classes a thrust presses) */
		int counts = g_stg.on && (g_stg.rule ? b41p : g_stg.death) && g_stg.cmd > 0;
		if (counts) {
			g_stg.pending += g_stg.pending < 4, g_stg.n_thrusts_in++, g_stg.last_hand = hnd;
			if (!g_stg.rule || g_stg.act != SOMVR_SGA_THRUSTS) /* (b41's press count: the thump as the game counts it) */
				haptic_k(hnd, SOMVR_HX_THUMP, 0.33f);
		}
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f a thrust of the %s hand at %.1f m/s%s", t, hnd ? "right" : "left", sp * 0.01f,
			      counts ? ": one press of the struggle's command queued" : !g_stg.on ? " (SOMVR_STRUGGLE=off: nothing)"
			      : g_stg.rule && !g_stg.ours ? " (b41: a beast's domination - the grip's, not a thrust's)"
			      : g_stg.rule && g_stg.ours && !b41p ? " (b41: this class is not answered by a thrust)"
			      : !g_stg.rule && !g_stg.death ? " (not the last chance: nothing)" : " (no command known for this struggle: nothing)");
	}
}

/* ---- build 41: the one rule's parts (re-xr-pipeline.md 180) ---- */
/* a hand closed (its squeeze, with the test controllers' too; hysteresis as SOMVR_RIDE's grip) */
static int stg_closed(int h)
{
	float sq = bd_squeeze(h, g_inp.st.hand[h].squeeze);
	g_stg.sq_dn[h] = g_stg.sq_dn[h] ? sq > 0.35f : sq > 0.7f;
	return g_stg.sq_dn[h] && g_inp.hands[h].valid;
}

/* a new struggle (or none): what the rule answers it with */
static void stg_b41_start(uintptr_t obj, const unsigned char *f, const char *atk, double t)
{
	g_stg.ours = g_stg.beast = 0, g_stg.act = SOMVR_SGA_NONE, g_stg.anc_ok = g_stg.anc_game = 0, g_stg.anc_node[0] = 0;
	g_stg.anc_type = -1, g_stg.anc_side = 0, g_stg.o_ok = 0, g_stg.cue_show = 0, g_stg.dir_ticks = 0, g_stg.head0_ok = 0;
	g_stg.dir_v[0] = g_stg.dir_v[1] = 0, g_stg.made_was = 0, g_stg.res_was = 0, g_stg.won_t = 0, g_stg.held_need = 0;
	g_stg.o_flash_until = 0, g_stg.hold_t = 0, g_stg.auto_step = 0, g_stg.auto_t = 0, g_stg.lu_ok = 0, g_stg.o_fixed = 0;
	g_stg.o_line_t = 0, g_stg.blade_ok = 0, g_stg.vh_logged = 0;
	atomic_store(&g_stg.aim_stab, 0);
	somvr_sg_stab_init(&g_stg.stab);
	for (int h = 0; h < 2; h++)
		somvr_sg_swipe_init(&g_stg.sw[h]);
	if (!g_stg.rule || !obj || !f)
		return;
	int k = stg_sg_kind(g_stg.kind);
	g_stg.beast = stg_beast_dom(atk);
	g_stg.ours = !g_stg.beast && k > SOMVR_SG_NONE && k < SOMVR_SG_OTHER;
	g_stg.anc_game = g_stg.kind != STG_OTHER && stg_anchor_game(obj, 1);
	if (g_stg.anc_game)
		g_stg.anc_ok = 1, g_stg.anc_t = t, g_stg.n_anc_game++;
	g_stg.act = somvr_sg_act(k, g_stg.beast, 1); /* (an aim with no node of its own: an orc's Neck, else the steer) */
	int cl[2] = { stg_closed(0), stg_closed(1) };
	somvr_sg_hold_init(&g_stg.hold, cl);
	g_stg.n_rule[g_stg.act]++;
	(void)f;
}

/* its end: the outcome (the last live read of its result +0x8c: 1 or 2 won) */
static void stg_b41_end(double t)
{
	char an[96];
	stg_talion_attack(an, sizeof an);
	/* (a won struggle is freed at once: its last live read is often still 0 - boot 173820's won last chance) */
	int won = g_stg.res_was == 1 || g_stg.res_was == 2 || (g_stg.act == SOMVR_SGA_STAB && atomic_load(&g_stg.aim_stab) == 2) ||
		  (g_stg.act == SOMVR_SGA_THRUSTS && g_stg.made_need > 0 && g_stg.made_was >= g_stg.made_need) ||
		  (g_stg.act == SOMVR_SGA_HOLD && g_stg.hold.held >= g_stg.held_need && g_stg.held_need > 0) || strstr(an, "Win");
	g_stg.n_won41 += won, g_stg.n_lost41 += !won;
	if (stg_log_ok())
		logf_("STRUGGLE t=%.3f b41 over: %s answered by %s - %s (its last result %d); the node %s; thrusts in it %llu, "
		      "presses of ours %llu, counted by the game %llu, holds %llu, swipes %llu (the way: %llu, ignored %llu), leans %llu, "
		      "the blade laid %llu, stabs %llu; Talion's attack now '%s'", t, g_stg_kind[g_stg.kind], somvr_sg_act_name(g_stg.act),
		      won ? "WON" : "lost", g_stg.res_was, g_stg.anc_node[0] ? g_stg.anc_node : "-", (unsigned long long)g_stg.n_thrusts_in,
		      (unsigned long long)g_stg.n_presses, (unsigned long long)g_stg.n_counted, (unsigned long long)g_stg.n_holds,
		      (unsigned long long)g_stg.n_swipes, (unsigned long long)g_stg.n_swipe_ok, (unsigned long long)g_stg.n_swipe_off,
		      (unsigned long long)g_stg.n_leans, (unsigned long long)g_stg.n_laid, (unsigned long long)g_stg.n_stabs, an);
}

/* the cue at the node (the objective layer, stg_entry): the ring's steps, or the arrow */
static void stg_cue(const unsigned char *f, double t)
{
	(void)t;
	g_stg.cue_show = 0;
	if (!g_stg.cue)
		return;
	uint32_t id = 0;
	float roll = 0, need;
	int show = 1;
	switch (g_stg.act) {
	case SOMVR_SGA_THRUSTS:
		id = somvr_sg_steps_count(f[0x2f5], f[0x2f4], SOMVR_OB_TIMER_STEPS);
		break;
	case SOMVR_SGA_THRUST:
		show = f[0xd4] || f[0x8c] == 1;
		id = f[0x8c] == 1 ? SOMVR_OB_TIMER_ARMED : SOMVR_OB_TIMER_STEPS;
		break;
	case SOMVR_SGA_HOLD:
		memcpy(&need, f + 0x100, 4);
		id = f[0x8c] == 1 ? SOMVR_OB_TIMER_ARMED : somvr_sg_steps_hold(g_stg.hold.held, need, SOMVR_OB_TIMER_STEPS);
		break;
	case SOMVR_SGA_GRIP: /* SOMVR_RIDE's hold on the beast */
		id = g_mt.sg.won ? SOMVR_OB_TIMER_ARMED : somvr_sg_steps_hold(g_mt.sg.held, g_mt.dom_hold_s, SOMVR_OB_TIMER_STEPS);
		break;
	case SOMVR_SGA_SWIPE:
		memcpy(&roll, f + 0xa8, 4);
		id = SOMVR_OB_SG_ARROW, roll *= 57.29578f, show = f[0xb2] == 1 && !f[0x203];
		break;
	default:
		show = 0; /* (the aim: the outline is its cue) */
	}
	if (!show)
		return;
	if (g_stg.anc_ok && cb_now() - g_stg.anc_t < 0.5 && vec_ok(g_stg.anc, 1e7f))
		memcpy(g_stg.cue_pos, g_stg.anc, sizeof g_stg.cue_pos);
	else { /* no node: 70 cm ahead of the eyes, 20 cm under them */
		float h[3] = { 0, -20, 70 };
		fn_h2w(h, g_stg.cue_pos);
	}
	if (!g_stg.cue_show_was && (g_stg.n_cue++, 1))
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: b41 the cue: %s at %.0f %.0f %.0f (%s)", t, t - g_stg.t0, id == SOMVR_OB_SG_ARROW ?
			      "the arrow" : "the timer ring", g_stg.cue_pos[0], g_stg.cue_pos[1], g_stg.cue_pos[2], g_stg.anc_ok ?
			      g_stg.anc_node : "ahead of the eyes: no node");
	g_stg.cue_id = id, g_stg.cue_roll = roll, g_stg.cue_show = 1, g_stg.cue_t = mono();
}

/* the input tick, a struggle of the rule's (f just read): 1 = done here, 0 = the thrusts' presses below (press count,
 * quick time, the steer) */
static int stg_b41_tick(sc_tick_out *o, uintptr_t obj, const unsigned char *f, double t)
{
	int cl[2] = { stg_closed(0), stg_closed(1) };
	float dt = g_stg.hold_t > 0 && t > g_stg.hold_t ? (float)fmin(t - g_stg.hold_t, 0.2) : 0;
	g_stg.hold_t = t, g_stg.dir_now[0] = g_stg.dir_now[1] = 0;
	int was_show = g_stg.cue_show;
	g_stg.cue_show_was = was_show;
	if (g_stg.act != SOMVR_SGA_STAB)
		stg_auto(f, t); /* (`struggle auto on`: the test hands) */
	if (!g_stg.on || !g_stg.ours) { /* a beast's domination: SOMVR_RIDE answers it; its ring here */
		g_stg.pending = 0;
		if (g_stg.on)
			stg_cue(f, t);
		return 1;
	}
	switch (g_stg.act) {
	case SOMVR_SGA_THRUSTS: {
		int made = f[0x2f5], need = f[0x2f4];
		if (made > g_stg.made_was) { /* the game counted it: the thump (stronger as it nears its need) */
			haptic_k(g_stg.last_hand, SOMVR_HX_THUMP, 0.45f + 0.45f * (float)made / (float)(need > 0 ? need : 1));
			g_stg.n_counted += (uint64_t)(made - g_stg.made_was);
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: b41 the game counts %d of %d%s", t, t - g_stg.t0, made, need,
				      made >= need ? " - done" : "");
		}
		g_stg.made_was = made, g_stg.made_need = need;
		stg_cue(f, t);
		return 0;
	}
	case SOMVR_SGA_THRUST:
		if (g_stg.pending > 0 && !g_stg.tap_on && !g_stg.tap_off && (!f[0xd4] || f[0xd5])) {
			g_stg.pending = 0, g_stg.n_qt_outside++;
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: b41 a thrust outside the quick time's window: not pressed", t, t - g_stg.t0);
		}
		stg_cue(f, t);
		return 0;
	case SOMVR_SGA_STEER:
		return 0;
	case SOMVR_SGA_HOLD: {
		int was = g_stg.hold.on, on = somvr_sg_hold_step(&g_stg.hold, cl, f[0xfe] == 1, dt);
		memcpy(&g_stg.held_need, f + 0x100, 4);
		if (on != was) {
			g_stg.n_holds += on;
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: b41 the hold: the %s hand %s - its command %d '%s' %s (the game's held(+118) %u, "
				      "phase %u, %.2f s of %.2f)", t, t - g_stg.t0, (on ? g_stg.hold.hand : !g_stg.hold.hand) ? "right" : "left",
				      on ? "closed" : "let go", g_stg.cmd, g_stg.cmd > 0 ? bo_cmd_name(g_stg.cmd) : "-", on ? "on" : "off",
				      f[0x118], f[0xfe], was ? g_stg.hold.held : 0, g_stg.held_need);
		}
		if (on && g_stg.cmd > 0 && f[0xfe] < 2) {
			if (o->ncmd < SC_OUT_CMDS)
				o->cmd[o->ncmd++] = g_stg.cmd;
			if (sc_denied(g_stg.cmd, g_inp.ctx))
				g_inp.allow_cmd = g_stg.cmd;
			if (t >= g_stg.hold_hap_t && g_stg.hold.hand >= 0) /* the effort felt, harder as it nears */
				haptic_k(g_stg.hold.hand, SOMVR_HX_STRAIN, 0.4f + 0.5f * fminf(1, g_stg.hold.held / fmaxf(0.1f, g_stg.held_need))),
					g_stg.hold_hap_t = t + 0.2;
		}
		g_stg.pending = 0;
		stg_cue(f, t);
		return 1;
	}
	case SOMVR_SGA_SWIPE:
		if (g_stg.dir_ticks > 0)
			g_stg.dir_now[0] = g_stg.dir_v[0], g_stg.dir_now[1] = g_stg.dir_v[1], g_stg.dir_ticks--;
		o->pad[SC_PAD_LX] = g_stg.dir_now[0], o->pad[SC_PAD_LY] = g_stg.dir_now[1]; /* (the stick neutral meanwhile) */
		if (g_stg.dir_now[0] != 0 || g_stg.dir_now[1] != 0)
			o->pad_on |= (1u << SC_PAD_LX) | (1u << SC_PAD_LY);
		g_stg.pending = 0;
		stg_cue(f, t);
		return 1;
	case SOMVR_SGA_STAB:
		/* (only once its window is open: CStruggleAim's update returns at once while +0xa5 is set and +0x592 is not -
		 * its open would never come; a stab before it waits for it) */
		if (atomic_load(&g_stg.aim_stab) == 1 && g_stg.kind == STG_AIM && f[0x592] && !f[0x591] &&
		    peek_ptr(obj) == MT_VT_STRUGGLE_AIM) {
			static const uint8_t one = 1;
			int ok = arm_poke(obj + MT_STRUG_DONE, &one, 1);
			atomic_store(&g_stg.aim_stab, 2), g_stg.n_stab_won += (uint64_t)ok;
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: b41 the stab on the outline %s", t, t - g_stg.t0, ok ? "completed it as the "
				      "game's own (ICombatStruggle+0xa5: its next update wins)" : "NOT written");
		}
		if (!g_stg.anc_ok && t - g_stg.t0 > 0.3) { /* no node and no orc: build 27's steer */
			g_stg.act = SOMVR_SGA_STEER, g_stg.n_rule[SOMVR_SGA_STEER]++;
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: b41 an aim with no node and no orc near: the hand steers its cursor (build 27)",
				      t, t - g_stg.t0);
		}
		g_stg.pending = 0;
		return 1;
	default:
		g_stg.pending = 0;
		return 1;
	}
}

/* the aim's stab check's outcome (the blade's in combat_frame, the empty hand's in stg_hands), with the outline's
 * distance every half second (logs) */
static void stg_stab_result(int r, const char *who, float dc, float ag, float al, double t)
{
	if (t >= g_stg.o_line_t && stg_log_ok()) {
		g_stg.o_line_t = t + 0.5;
		logf_("STRUGGLE t=%.3f +%.2f s: b41 %s %.0f cm off the outline's %s, %.0f deg off its way, %+.1f m/s along it; the node "
		      "%.0f cm from where it was placed", t, t - g_stg.t0, who, dc, g_stg.blade_ok ? "point" : "grip", ag, al,
		      dist3(g_stg.anc, g_stg.o_anc0));
	}
	if (r == SOMVR_SGS_LAID) {
		g_stg.n_laid++;
		haptic(SOMVR_HAND_R, SOMVR_HX_READY);
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: b41 %s laid on the outline (%.0f cm off its %s, %.0f deg off its way)", t,
			      t - g_stg.t0, who, dc, g_stg.blade_ok ? "point" : "grip", ag);
	} else if (r == SOMVR_SGS_STAB) {
		g_stg.n_stabs++, g_stg.o_flash_until = mono() + 0.15;
		atomic_store(&g_stg.aim_stab, 1);
		haptic_k(SOMVR_HAND_R, SOMVR_HX_HIT, 1);
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: b41 the stab along the outline at %.1f m/s (%s %.0f cm off, %.0f deg): the "
			      "struggle's completion asked", t, t - g_stg.t0, al, who, dc, ag);
	}
}

/* the outline's grip as a `hands test R` pose (no blade drawn: the empty hand holds it; the finisher's fn_rel) */
static int stg_rel(float out[6])
{
	if (!g_bd.test)
		return 0;
	const somvr_posef *ev = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(ev[0].q));
	float hp[3] = { 0.5f * (ev[0].p[0] + ev[1].p[0]), 0.5f * (ev[0].p[1] + ev[1].p[1]), 0.5f * (ev[0].p[2] + ev[1].p[2]) };
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 });
	float yaw = atan2f(-f.x, -f.z);
	sv_quat qyi = { 0, -sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	somvr_posef xp;
	sa_xf xg = { { g_stg.o_grip_g[0], g_stg.o_grip_g[1], g_stg.o_grip_g[2] },
		     { g_stg.o_q_g[0], g_stg.o_q_g[1], g_stg.o_q_g[2], g_stg.o_q_g[3] } };
	vh_to_xr(&xg, &xp);
	sv_vec3 rel = sv_qrot(qyi, (sv_vec3){ xp.p[0] - hp[0], xp.p[1] - hp[1], xp.p[2] - hp[2] });
	out[0] = rel.x * 100, out[1] = rel.y * 100, out[2] = -rel.z * 100;
	fn_euler(sv_qmul(qyi, sv_quat_of(xp.q)), out + 3);
	return isfinite(out[0] + out[1] + out[2] + out[3]);
}

/* combat_frame (H4, the game thread): an aim's outline at its node and the stab on it - b the drawn blade (NULL: none
 * this frame) */
static void stg_frame(uintptr_t pl, const somvr_blade *b, const float *root, double t)
{
	if (!g_stg.rule || !g_stg.on || !g_stg.obj || !g_stg.ours || g_stg.act != SOMVR_SGA_STAB)
		return;
	if (!g_stg.anc_game) { /* no node of its own: the captain held (a finisher), else the nearest orc's Neck */
		uintptr_t pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
		if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
			pc = 0;
		struct cb_orc os[CB_ORCS], oc;
		int n = pcomb && root ? cb_orcs(pcomb, pc, root, os, CB_ORCS) : 0;
		const struct cb_orc *best = NULL;
		if (g_stg.fin_c && cb_orc_get(g_stg.fin_c, &oc))
			best = &oc;
		for (int i = 0; !best && i < n; i++)
			if (os[i].dist < 300.0f && (!best || os[i].dist < best->dist))
				best = &os[i];
		float nd[4][3];
		if (best && stl_body(best, nd) && vec_ok(nd[2], 1e7f)) {
			if (!g_stg.anc_ok)
				g_stg.n_anc_orc++, snprintf(g_stg.anc_node, sizeof g_stg.anc_node, "Neck of the orc %#llx (no node of its own)",
							   (unsigned long long)best->c);
			memcpy(g_stg.anc, nd[2], sizeof g_stg.anc), g_stg.anc_ok = 1, g_stg.anc_t = t;
		}
	}
	if (!g_stg.anc_ok || t - g_stg.anc_t > 0.5)
		return;
	float ehz[3] = { 0, 0, 0 }, eye[3];
	fn_h2w(ehz, eye);
	if (dist3(eye, g_stg.anc) > STG_ANC_MAX_CM)
		return;
	if (!g_stg.o_ok) { /* the outline's way in, once (the finisher's placement: the hand near the body, a full thrust) */
		somvr_fn_place_cfg pc;
		somvr_fn_place_defaults(&pc);
		pc.blade = STG_BLADE_CM, pc.depth = STG_DEPTH_CM;
		float nh[3], gh[3], dh[3], gap, ext, gw[3];
		int why = 0;
		fn_w2h(g_stg.anc, nh);
		int placed = somvr_fn_place(&pc, nh, gh, dh, &gap, &ext, &why);
		if (placed)
			fn_h2w(gh, gw);
		else {
			float ph[3] = { STG_PREF_R, STG_PREF_U, STG_PREF_A };
			fn_h2w(ph, gw);
		}
		float d[3] = { g_stg.anc[0] - gw[0], g_stg.anc[1] - gw[1], g_stg.anc[2] - gw[2] };
		float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (!(l > 5.0f))
			return;
		for (int i = 0; i < 3; i++)
			g_stg.o_dir[i] = d[i] / l;
		g_stg.o_start_cm = placed ? fmaxf(6.0f, l - STG_BLADE_CM) : STG_START_CM;
		g_stg.o_ok = 1;
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: b41 the outline at the node '%s' (%.0f %.0f %.0f; %.0f cm right, %.0f up, %.0f ahead of "
			      "the eyes): its point %.0f cm short of it, the way in %+.2f %+.2f %+.2f, %s", t, t - g_stg.t0, g_stg.anc_node,
			      g_stg.anc[0], g_stg.anc[1], g_stg.anc[2], nh[0], nh[1], nh[2], g_stg.o_start_cm, g_stg.o_dir[0],
			      g_stg.o_dir[1], g_stg.o_dir[2], placed ? "placed for the arm's thrust (somvr_fn_place)" :
			      "the grip's natural place (somvr_fn_place found none)");
	}
	if (!g_stg.o_fixed) { /* (fixed in the world where it was placed: the node is a moving hand or head - the 1st boot's
			       * R_Hand swung away from the line-up within a second; the player lines up with what stays) */
		for (int i = 0; i < 3; i++)
			g_stg.o_start[i] = g_stg.anc[i] - g_stg.o_dir[i] * g_stg.o_start_cm, g_stg.o_anc0[i] = g_stg.anc[i];
		g_stg.o_fixed = 1;
	}
	float gw[3] = { g_stg.o_start[0] - g_stg.o_dir[0] * STG_BLADE_CM, g_stg.o_start[1] - g_stg.o_dir[1] * STG_BLADE_CM,
			g_stg.o_start[2] - g_stg.o_dir[2] * STG_BLADE_CM }, dg[3];
	memcpy(g_stg.o_handle, gw, sizeof gw);
	fn_w2g(gw, g_stg.o_grip_g), fn_w2g_dir(g_stg.o_dir, dg);
	sv_quat q = somvr_bow_look((sv_vec3){ dg[0], dg[1], dg[2] }, 0);
	g_stg.o_q_g[0] = q.x, g_stg.o_q_g[1] = q.y, g_stg.o_q_g[2] = q.z, g_stg.o_q_g[3] = q.w;
	g_stg.blade_ok = b && b->valid;
	if ((g_bd.test && !g_stg.lu_ok) || atomic_exchange(&g_stg.req_pose, 0)) { /* the drivers' line-up */
		g_stg.lu_ok = g_stg.blade_ok ? stg_lineup(b, g_stg.lu) : stg_rel(g_stg.lu);
		float w[3];
		stg_way_h(w);
		if (g_stg.lu_ok && stg_log_ok())
			logf_("STRUGGLE lineup t=%.3f +%.2f s: the test hand that %s: hands test R %.1f %.1f %.1f %.1f %.1f %.1f; the way in "
			      "%.3f %.3f %.3f (right, up, ahead)", t, t - g_stg.t0, g_stg.blade_ok ? "lays the drawn blade on the outline" :
			      "holds the outline's grip (no blade drawn)", g_stg.lu[0], g_stg.lu[1], g_stg.lu[2], g_stg.lu[3], g_stg.lu[4],
			      g_stg.lu[5], w[0], w[1], w[2]);
	}
	if (g_stg.auto_on && g_stg.lu_ok)
		stg_auto(g_stg.f, t);
	if (!g_stg.blade_ok || atomic_load(&g_stg.aim_stab))
		return; /* (no blade drawn: the hand at the outline's grip, stg_hands) */
	float ax[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] }, dc, ag, al;
	int r = somvr_sg_stab_step(&g_stg.stab, &g_stg.kcfg, (float)(t - g_stg.t0), b->tip, ax, b->vel_tip, g_stg.o_start,
				   g_stg.o_dir, &dc, &ag, &al);
	stg_stab_result(r, "the blade", dc, ag, al, t);
}

/* the drivers' line-up (SOMVR_HANDS_TEST; the finisher's fn_lineup for the struggle's outline): the right test hand's pose
 * that lays the drawn blade on the outline - the hand's pose now turned by the turn taking the blade onto the way, moved
 * so its point lands on the outline's (the head's yaw frame: cm right, up, ahead; `hands test`'s angles). 0 = no test hand */
static int stg_lineup(const somvr_blade *b, float out[6])
{
	if (!g_bd.test || !g_bd.t_on[1] || !b || !b->valid)
		return 0;
	const somvr_posef *ev = g_fr.views.pose;
	sv_quat hq = sv_qnormalize(sv_quat_of(ev[0].q));
	float hp[3] = { 0.5f * (ev[0].p[0] + ev[1].p[0]), 0.5f * (ev[0].p[1] + ev[1].p[1]), 0.5f * (ev[0].p[2] + ev[1].p[2]) };
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, -1 });
	float yaw = atan2f(-f.x, -f.z), g[3];
	sv_quat qyi = { 0, -sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	fn_w2g(b->tip, g);
	sv_vec3 tip = sv_qrot(qyi, (sv_vec3){ 0.01f * g[0] - hp[0], 0.01f * g[1] - hp[1], -0.01f * g[2] - hp[2] });
	fn_w2g(g_stg.o_start, g);
	sv_vec3 st = sv_qrot(qyi, (sv_vec3){ 0.01f * g[0] - hp[0], 0.01f * g[1] - hp[1], -0.01f * g[2] - hp[2] });
	float aw[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] }, ag[3], dg[3];
	fn_w2g_dir(aw, ag), fn_w2g_dir(g_stg.o_dir, dg);
	sv_vec3 ax = sv_qrot(qyi, (sv_vec3){ ag[0], ag[1], -ag[2] }), dw = sv_qrot(qyi, (sv_vec3){ dg[0], dg[1], -dg[2] });
	float la = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z), ld = sqrtf(dw.x * dw.x + dw.y * dw.y + dw.z * dw.z);
	if (!(la > 1e-3f && ld > 1e-3f))
		return 0;
	ax.x /= la, ax.y /= la, ax.z /= la, dw.x /= ld, dw.y /= ld, dw.z /= ld;
	const float *v = g_bd.t_v[1];
	const somvr_hand *hd = &g_inp.hands[1];
	sv_vec3 gp = { 0.01f * v[0], 0.01f * v[1], -0.01f * v[2] };
	sv_quat hy = sa_xf_euler((float[6]){ 0, 0, 0, v[3], v[4], v[5] }).q;
	if (hd->valid)
		gp = sv_qrot(qyi, (sv_vec3){ hd->pose.p[0] - hp[0], hd->pose.p[1] - hp[1], hd->pose.p[2] - hp[2] }),
		hy = sv_qnormalize(sv_qmul(qyi, sv_quat_of(hd->pose.q)));
	sv_quat qa = fn_qalign(ax, dw), qn = sv_qmul(qa, hy);
	sv_vec3 r = sv_qrot(qa, (sv_vec3){ tip.x - gp.x, tip.y - gp.y, tip.z - gp.z });
	out[0] = (st.x - r.x) * 100, out[1] = (st.y - r.y) * 100, out[2] = -(st.z - r.z) * 100;
	fn_euler(qn, out + 3);
	return 1;
}

/* the outline's way in the head's yaw frame (right, up, ahead) */
static void stg_way_h(float w[3])
{
	float a[3] = { 0, 0, 0 }, b2[3], ha[3], hb[3];
	for (int i = 0; i < 3; i++)
		b2[i] = a[i] + g_stg.o_dir[i] * 100.0f;
	float e[3] = { 0, 0, 0 }, eye[3];
	fn_h2w(e, eye);
	for (int i = 0; i < 3; i++)
		a[i] = eye[i], b2[i] += eye[i];
	fn_w2h(a, ha), fn_w2h(b2, hb);
	for (int i = 0; i < 3; i++)
		w[i] = (hb[i] - ha[i]) * 0.01f;
}

/* a `hands` word from the game thread (bd_word takes its own lock) */
static void stg_hw(const char *fmt, ...)
{
	char w[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(w, sizeof w, fmt, ap);
	va_end(ap);
	const char *r = bd_word(w);
	g_stg.n_auto++;
	if (stg_log_ok())
		logf_("STRUGGLE t=%.3f +%.2f s: b41 test hands: `hands %s` (%s)", cb_now(), cb_now() - g_stg.t0, w, r ? r : "-");
}

/* `struggle auto on` (SOMVR_HANDS_TEST): this struggle answered by the test hands as a player would - the input tick
 * (f just read) and combat_frame (the outline's line-up) */
static void stg_auto(const unsigned char *f, double t)
{
	if (!g_stg.auto_on || !g_bd.test || !g_stg.obj || !g_stg.ours || t < g_stg.auto_t)
		return;
	double dt = t - g_stg.t0;
	int k = g_stg.auto_step;
	switch (g_stg.act) {
	case SOMVR_SGA_THRUSTS: /* both hands in turn, thrust and pull: 30-45 cm in 110 ms each, 0.15 s apart */
		if (k >= 40 || f[0x2f6] >= 2)
			return;
		if (!k)
			stg_hw("test L " STG_REST_L), stg_hw("test R " STG_REST_R);
		else if (k % 4 == 1)
			stg_hw("swing R 10 -12 70 0 0 0 110");
		else if (k % 4 == 2)
			stg_hw("swing L -10 -12 70 0 0 0 110");
		else if (k % 4 == 3)
			stg_hw("swing R " STG_REST_R " 110");
		else
			stg_hw("swing L " STG_REST_L " 110");
		g_stg.auto_t = t + (k ? 0.15 : 0.25);
		break;
	case SOMVR_SGA_THRUST: /* one thrust when its window opens */
		if (k == 0 && f[0xd4] && !f[0xd5])
			stg_hw("swing R 10 -12 70 0 0 0 110"), g_stg.auto_t = t + 0.5;
		else if (k == 1)
			stg_hw("swing R " STG_REST_R " 200");
		else
			return;
		break;
	case SOMVR_SGA_HOLD: /* the free hand opens, then closes after 0.3 s and stays closed */
		if (k == 0)
			stg_hw("squeeze L 0"), g_stg.auto_t = t + 0.3;
		else if (k == 1)
			stg_hw("squeeze L 1"), g_stg.auto_t = t + 60;
		else
			return;
		break;
	case SOMVR_SGA_SWIPE: { /* a 45 cm swipe of the right hand the shown way once its window is open */
		float a, d[2];
		memcpy(&a, f + 0xa8, 4);
		d[0] = cosf(a), d[1] = sinf(a);
		if (k == 0 && f[0xb2] == 1 && dt > 0.2)
			stg_hw("swing R %.0f %.0f 25 0 0 0 180", 25 + 45 * d[0], -35 + 45 * d[1]), g_stg.auto_t = t + 0.8;
		else if (k == 1)
			stg_hw("swing R " STG_REST_R " 300");
		else
			return;
		break;
	}
	case SOMVR_SGA_STAB: /* the hand laid on the outline (its line-up), then a 40 cm thrust along it in 150 ms */
		if (!g_stg.lu_ok || k >= 2)
			return;
		if (k == 0) /* (eased in over 0.6 s: build 41's first boots set the pose at once and the weighted sword's catch-up was
			     * itself a thrust along the line - boot 173820) */
			stg_hw("swing R %.1f %.1f %.1f %.1f %.1f %.1f 600", g_stg.lu[0], g_stg.lu[1], g_stg.lu[2], g_stg.lu[3], g_stg.lu[4],
			       g_stg.lu[5]), g_stg.auto_t = t + 0.6 + (g_stg.auto_delay > 0 ? g_stg.auto_delay : 0.35f);
		else {
			float w[3];
			stg_way_h(w);
			stg_hw("swing R %.1f %.1f %.1f %.1f %.1f %.1f 150", g_stg.lu[0] + 40 * w[0], g_stg.lu[1] + 40 * w[1],
			       g_stg.lu[2] + 40 * w[2], g_stg.lu[3], g_stg.lu[4], g_stg.lu[5]);
		}
		break;
	default:
		return;
	}
	g_stg.auto_step++;
}

/* the test words (the writer thread): "struggle status|pose|rule b27|b41|auto on [S]|off" */
static const char *stg_word(const char *w)
{
	if (!strcmp(w, "status"))
		return atomic_store(&g_stg.req_status, 1), "struggle status";
	if (!strcmp(w, "pose"))
		return atomic_store(&g_stg.req_pose, 1), "struggle pose (the outline's line-up as a hands-test pose)";
	if (!strcmp(w, "rule b27") || !strcmp(w, "rule b41"))
		return atomic_store(&g_stg.req_rule, w[6] == '4' ? 2 : 1), w[6] == '4' ? "struggle rule b41" : "struggle rule b27";
	float dl = 0;
	if (!strncmp(w, "auto on", 7) && (!w[7] || (sscanf(w + 7, " %f", &dl) == 1 && dl >= 0.1f && dl <= 3.0f))) {
		uint32_t b;
		memcpy(&b, &dl, 4);
		atomic_store(&g_stg.req_auto_delay, b);
		return atomic_store(&g_stg.req_auto, 2), "struggle auto on";
	}
	if (!strcmp(w, "auto off"))
		return atomic_store(&g_stg.req_auto, 1), "struggle auto off";
	return NULL;
}

/* the input tick: the words' requests */
static void stg_requests(double t)
{
	int r = atomic_exchange(&g_stg.req_rule, 0), a = atomic_exchange(&g_stg.req_auto, 0);
	if (r)
		g_stg.rule = r == 2, logf_("STRUGGLE t=%.3f rule %s (the word; the next struggle on)", t, g_stg.rule ? "b41" : "b27");
	if (a) {
		g_stg.auto_on = a == 2 && g_bd.test;
		uint32_t b = atomic_exchange(&g_stg.req_auto_delay, 0);
		float dl = 0;
		memcpy(&dl, &b, 4);
		g_stg.auto_delay = dl > 0 ? dl : 0.35f;
		logf_("STRUGGLE t=%.3f auto %s (the aim's stab %.2f s after its line-up)%s", t, g_stg.auto_on ? "on: the test hands "
		      "answer each struggle as a player would" : "off", g_stg.auto_delay, a == 2 && !g_bd.test ?
		      " (SOMVR_HANDS_TEST is off: no test hands)" : "");
	}
	if (atomic_exchange(&g_stg.req_status, 0))
		logf_("STRUGGLE t=%.3f status: rule %s, auto %s; now %s%s (act: %s, the node %s); so far won %llu, lost %llu",
		      t, g_stg.rule ? "b41" : "b27", g_stg.auto_on ? "on" : "off", g_stg.obj ? g_stg_kind[g_stg.kind] : "no struggle",
		      g_stg.obj && g_stg.beast ? " (a beast's domination)" : "", somvr_sg_act_name(g_stg.act),
		      g_stg.anc_node[0] ? g_stg.anc_node : "-", (unsigned long long)g_stg.n_won41, (unsigned long long)g_stg.n_lost41);
}

/* the vrhands frame: an aim's outline in the arrow's slot (the finisher's look; its flash at the stab) */
static void stg_vh(somvr_vrhands_frame *e)
{
	if (!g_stg.rule || !g_stg.o_ok || !g_stg.obj || g_stg.act != SOMVR_SGA_STAB || e->arrow_len > 0.05f ||
	    (atomic_load(&g_stg.aim_stab) && mono() >= g_stg.o_flash_until))
		return;
	sa_xf x = { { g_stg.o_grip_g[0], g_stg.o_grip_g[1], g_stg.o_grip_g[2] },
		    { g_stg.o_q_g[0], g_stg.o_q_g[1], g_stg.o_q_g[2], g_stg.o_q_g[3] } };
	if (!isfinite(x.p.x + x.p.y + x.p.z + x.q.w))
		return;
	vh_to_xr(&x, &e->arrow);
	e->arrow_len = (STG_BLADE_CM + 6.0f) * 0.01f;
	e->arrow_glow = mono() < g_stg.o_flash_until ? 5.0f : 4.5f + 0.3f * sinf((float)(mono() * 7.854));
	if (!g_stg.vh_logged && stg_log_ok()) { /* (once a struggle: where the layer draws it) */
		const somvr_posef *ev = g_fr.views.pose;
		g_stg.vh_logged = 1;
		logf_("STRUGGLE t=%.3f +%.2f s: b41 the outline drawn: its grip at xr %+.2f %+.2f %+.2f (the eyes %+.2f %+.2f %+.2f), %.2f m "
		      "long, the glow %.1f", cb_now(), cb_now() - g_stg.t0, e->arrow.p[0], e->arrow.p[1], e->arrow.p[2],
		      0.5f * (ev[0].p[0] + ev[1].p[0]), 0.5f * (ev[0].p[1] + ev[1].p[1]), 0.5f * (ev[0].p[2] + ev[1].p[2]),
		      e->arrow_len, e->arrow_glow);
	}
}

/* in_apply: an answered hold's command kept off until the rule's hold is on - the drain's B (120) held through the
 * Tower's last beat would never be a fresh press (the class counts only an OnCommandOn after it began; nurn41's boot
 * 165235 lost the Ioreth hold so x4); 0 = none */
static int stg_cmd_block(void)
{
	return g_stg.on && g_stg.rule && g_stg.obj && g_stg.ours && g_stg.act == SOMVR_SGA_HOLD && !g_stg.hold.on && g_stg.cmd > 0 &&
	       (!g_stg.have_f || g_stg.f[0xfe] < 2) ? g_stg.cmd : 0;
}

/* ob_frame: the cue in the objective layer (first: during a struggle nothing else) */
static int stg_cue_on(void) { return g_stg.cue_show && mono() - g_stg.cue_t < 0.25; }
static int stg_entry(somvr_objective_frame *e)
{
	if (!stg_cue_on() || !vec_ok(g_stg.cue_pos, 1e7f))
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	/* nothing in the face: a node nearer than STG_CUE_NEAR_CM (the beast on top of Talion) is shown that far out on
	 * the line to it; the quad STG_CUE_DEG of the view wide (12 cm at 50 cm, 22 at a metre) */
	float ez[3] = { 0, 0, 0 }, eye[3], p[3];
	fn_h2w(ez, eye);
	float d[3] = { g_stg.cue_pos[0] - eye[0], g_stg.cue_pos[1] - eye[1], g_stg.cue_pos[2] - eye[2] };
	float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), k = l > 1 && l < STG_CUE_NEAR_CM ? STG_CUE_NEAR_CM / l : 1;
	for (int i = 0; i < 3; i++)
		p[i] = eye[i] + d[i] * k;
	somvr_th_to_xr(&mp, p, e->pos);
	float size = 2.0f * fmaxf(l * k, STG_CUE_NEAR_CM) * 0.01f * tanf(0.5f * STG_CUE_DEG * 0.01745329f);
	e->n = 1, e->flags = SOMVR_OBF_STRUGGLE, e->id = g_stg.cue_id, e->dist_m = fminf(STG_CUE_M, size), e->top[0] = g_stg.cue_roll;
	return 1;
}

/* the input tick: the struggle's object; our press of its command */
static void stg_tick(sc_tick_out *o)
{
	double t = cb_now();
	stg_requests(t); /* build 41: the test words */
	uintptr_t pc = g_inp.pl ? peek_ptr(g_inp.pl + P_COMBAT) : 0, s[2] = { 0, 0 };
	if (!(pc && peek(pc + IN_PC_STRUGGLE, s, sizeof s)))
		s[0] = s[1] = 0;
	int slot = s[0] ? 0 : s[1] ? 1 : -1;
	uintptr_t obj = slot >= 0 ? s[slot] : 0;
	unsigned char f[sizeof g_stg.f];
	int rd = obj && peek(obj, f, sizeof f);
	if (obj != g_stg.obj) {
		if (g_stg.obj && stg_log_ok()) {
			char fl[260], an[96];
			stg_fields(g_stg.kind, g_stg.f, fl, sizeof fl);
			logf_("STRUGGLE t=%.3f over after %.2f s: %s at %#llx; its last read: %s; presses of ours %llu so far; "
			      "Talion's attack now '%s'", t, t - g_stg.t0, g_stg_kind[g_stg.kind], (unsigned long long)g_stg.obj, fl,
			      (unsigned long long)g_stg.n_presses, stg_talion_attack(an, sizeof an));
		}
		if (g_stg.obj && g_stg.rule && g_stg.ours) /* build 41: the last one's end */
			stg_b41_end(t);
		g_stg.obj = obj, g_stg.kind = stg_kind_of(obj), g_stg.slot = slot, g_stg.t0 = t, g_stg.have_f = 0;
		char dn[96];
		g_stg.death = obj && strstr(stg_talion_attack(dn, sizeof dn), "PreDeath") != NULL; /* the last chance only (the
								 * caragor's domination is a CStruggleAim too: ride2's grip) */
		/* build 27 (re-xr-pipeline.md 116): a beaten captain's Eliminate at his Last Words is Talion's finisher with the
		 * captain's own struggle ('PC_Bip_Finisher_PCLose' from its start; boot 20261030-141203: left alone it was lost
		 * and Ratlug got up whole, 5 -> 569) - the same CStruggleAim, the same answer */
		g_stg.fin_now = obj && !g_stg.death && g_stg.finisher && strstr(dn, "_Finisher_") != NULL;
		g_stg.fin_c = g_stg.fin_now ? g_vb.lw_asked_c : 0, g_stg.fin_stab = 0;
		g_stg.death |= g_stg.fin_now;
		g_stg.n_fin += g_stg.fin_now;
		g_stg.pending = g_stg.tap_on = g_stg.tap_off = 0, g_stg.cmd = -1, g_stg.next_line = t + 0.5;
		g_stg.aim_have = 0, g_stg.aim0_ok[0] = g_stg.aim0_ok[1] = 0, g_stg.aim_line = 0;
		stg_b41_start(obj, rd ? f : NULL, dn, t);
		if (obj && rd) {
			g_stg.n_struggles++, g_stg.n_kind[g_stg.kind]++;
			g_stg.cmd = stg_cmd_of(g_stg.kind, f);
			g_stg.n_nocmd += g_stg.cmd < 0;
			char fl[260], hx[3 * 64 + 1], hx2[3 * 48 + 1], an[96];
			stg_fields(g_stg.kind, f, fl, sizeof fl);
			for (int i = 0; i < 64; i++) /* +0x80..+0xbf: the base's timers and result */
				snprintf(hx + 3 * i, 4, "%02x ", f[0x80 + i]);
			int base2 = g_stg.kind == STG_PRESS ? 0x2e0 : g_stg.kind == STG_AIM ? 0x570 : 0xc0;
			for (int i = 0; i < 48; i++)
				snprintf(hx2 + 3 * i, 4, "%02x ", f[base2 + i]);
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f start: %s at %#llx (PlayerCombat+%#x: the %s's) - its command %d '%s'%s; %s; "
				      "+0x80: %s; +%#x: %s; Talion's attack '%s'", t, g_stg_kind[g_stg.kind], (unsigned long long)obj,
				      0x8d8 + 8 * slot, slot ? "sync action" : "combat state", g_stg.cmd, g_stg.cmd > 0 ?
				      bo_cmd_name(g_stg.cmd) : "-", !g_stg.on ? " (SOMVR_STRUGGLE=off)" : !g_stg.death ?
				      " - not Talion's last chance (no PreDeath attack): left alone" : g_stg.fin_now ? (g_stg.kind ==
				      STG_AIM ? " - a captain's finisher (SOMVR_STRUGGLE_FINISHER): the hand steers its cursor, a thrust in "
				      "its ring presses" : " - a captain's finisher (SOMVR_STRUGGLE_FINISHER): a thrust = one press")
				      : g_stg.kind == STG_AIM ?
				      " - the last chance: the hand steers its cursor, a thrust in its ring presses (its command is set as it "
				      "opens)" : g_stg.cmd > 0 ? " - the last chance: a thrust = one press" : " - the last chance, no "
				      "answer of ours (its class's command not known)", fl, hx, base2, hx2,
				      stg_talion_attack(an, sizeof an));
			if (g_stg.on && (g_stg.rule ? g_stg.ours && g_stg.act != SOMVR_SGA_NONE : g_stg.death && (g_stg.cmd > 0 ||
					 g_stg.kind == STG_AIM)))
				haptic(SOMVR_HAND_R, SOMVR_HX_STRUGGLE); /* both hands */
			if (g_stg.rule && stg_log_ok())
				logf_("STRUGGLE t=%.3f b41: %s (Talion's '%s') - %s; the act: %s; its node %s%s%s",
				      t, g_stg_kind[g_stg.kind], dn, g_stg.beast ? "a beast's domination (SOMVR_RIDE answers: its grip)"
				      : g_stg.ours ? "ours" : "left alone", somvr_sg_act_name(g_stg.act), g_stg.anc_game ? "'" : "",
				      g_stg.anc_game ? g_stg.anc_node : g_stg.sites_ok ? "none in its UI data (an orc's Neck for an aim)"
				      : "not read (no sites)", g_stg.anc_game ? (g_stg.anc_side ? "' on the other (+0x70)" :
				      "' on the owner (+0x38)") : "");
		}
	}
	if (obj && obj == g_stg.obj && g_stg.fin_stab == 1 && g_stg.kind == STG_AIM && rd && f[0x592] && !f[0x591] &&
	    peek_ptr(obj) == MT_VT_STRUGGLE_AIM) { /* (build 41: only once its window is open - before it, +0xa5 freezes the
						    * aim's update: it returns at once while +0x592 is 0; the stab waits) */
		static const uint8_t one = 1;
		int ok = arm_poke(obj + MT_STRUG_DONE, &one, 1);
		g_stg.fin_stab = 2, g_stg.n_fin_won += (uint64_t)ok;
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: the finisher's stab %s", t, t - g_stg.t0, ok ? "completed it as the game's own "
			      "(ICombatStruggle+0xa5: its next update wins)" : "NOT written");
	}
	if (!obj || !rd)
		return;
	if (!g_stg.rule && !g_stg.death) { /* another struggle (a domination, an interrogation): logged at its start, left alone */
		g_stg.pending = 0;
		return;
	}
	char fl[260];
	if (!g_stg.have_f || memcmp(f + 0x80, g_stg.f + 0x80, 0x20) || (g_stg.kind == STG_PRESS && memcmp(f + 0x2f4, g_stg.f + 0x2f4, 3)) ||
	    (g_stg.kind == STG_QT && memcmp(f + 0xd4, g_stg.f + 0xd4, 3)) || (g_stg.kind == STG_AIM && (memcmp(f + 0x591, g_stg.f + 0x591, 2) ||
	    memcmp(f + 0xe8 + 0x474, g_stg.f + 0xe8 + 0x474, 8))) || (g_stg.kind == STG_HOLD && (f[0xfe] != g_stg.f[0xfe] ||
	    f[0x118] != g_stg.f[0x118])) || (g_stg.kind == STG_DIR && (f[0xb2] != g_stg.f[0xb2] || f[0x200] != g_stg.f[0x200] ||
	    f[0x203] != g_stg.f[0x203]))) { /* (build 41: the hold's and the direction's too) */
		int res_chg = g_stg.have_f && memcmp(f + 0x8c, g_stg.f + 0x8c, 4);
		if (res_chg)
			g_stg.n_done++;
		if ((res_chg || t >= g_stg.next_line) && stg_log_ok()) {
			stg_fields(g_stg.kind, f, fl, sizeof fl);
			logf_("STRUGGLE t=%.3f +%.2f s: %s%s", t, t - g_stg.t0, fl, res_chg ? " (the result changed)" : "");
			g_stg.next_line = t + 0.25;
		}
	}
	memcpy(g_stg.f, f, sizeof f), g_stg.have_f = 1;
	int c2 = stg_cmd_of(g_stg.kind, f); /* (an aim struggle's command is set when it opens) */
	if (c2 != g_stg.cmd && stg_log_ok())
		logf_("STRUGGLE t=%.3f +%.2f s: its command now %d '%s'", t, t - g_stg.t0, c2, c2 > 0 ? bo_cmd_name(c2) : "-");
	g_stg.cmd = c2;
	if (g_stg.rule) { /* build 41: the anchor follows its node; the cue; the classes not answered by a thrust */
		if (g_stg.anc_game && stg_kind_of(obj) == g_stg.kind && stg_anchor_game(obj, 0)) /* (its vtable still a struggle's) */
			g_stg.anc_t = t;
		if (stg_kind_of(obj) == g_stg.kind) /* (the object still alive: its result; a freed one reads 0) */
			g_stg.res_was = (int)(f[0x8c] | f[0x8d] << 8);
		if (stg_b41_tick(o, obj, f, t))
			return;
	}
	if (g_stg.on && g_stg.kind == STG_AIM && g_stg.aim_have && (g_stg.aim_via & 1)) { /* the hand as the pad's left */
		o->pad[SC_PAD_LX] = g_stg.aim[0], o->pad[SC_PAD_LY] = g_stg.aim[1];            /* stick (the game's "L") */
		o->pad_on |= (1u << SC_PAD_LX) | (1u << SC_PAD_LY);
		g_stg.n_aim_pad++;
	} else if (g_stg.on && g_stg.kind == STG_AIM) { /* the stick itself: as the widget shows it (run 063029: the struggle's
							 * stick turned by the head's yaw off the body: up became up-left) */
		float dz[2];
		sc_deadzone(o->move_raw, g_inp.cfg.dead, dz);
		o->pad[SC_PAD_LX] = dz[0], o->pad[SC_PAD_LY] = dz[1];
		if (dz[0] != 0 || dz[1] != 0)
			o->pad_on |= (1u << SC_PAD_LX) | (1u << SC_PAD_LY);
	}
	if (g_stg.on && g_stg.kind == STG_AIM && g_stg.pending > 0 && !g_stg.tap_on && !g_stg.tap_off) {
		/* CStruggleAim's OnCommandOn: its command with the cursor in the ring (+0x57a) = the success (+0x8c 1); out of
		 * it a press can end it as a miss - a thrust outside the ring is not pressed */
		g_stg.pending = 0;
		if (!f[0x57a]) {
			g_stg.n_aim_outside++;
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: a thrust with the cursor outside the ring: not pressed", t, t - g_stg.t0);
			return;
		}
		g_stg.pending = 1; /* pressed below, as any struggle's */
	}
	if (!g_stg.on || g_stg.cmd <= 0)
		return;
	if (g_stg.tap_on > 0) { /* our press: held two ticks */
		g_stg.tap_on--;
		if (o->ncmd < SC_OUT_CMDS)
			o->cmd[o->ncmd++] = g_stg.cmd;
		if (sc_denied(g_stg.cmd, g_inp.ctx))
			g_inp.allow_cmd = g_stg.cmd;
		if (!g_stg.tap_on)
			g_stg.tap_off = 2;
	} else if (g_stg.tap_off > 0) {
		g_stg.tap_off--;
	} else if (g_stg.pending > 0) {
		g_stg.pending--, g_stg.tap_on = 2, g_stg.n_presses++;
		if (o->ncmd < SC_OUT_CMDS)
			o->cmd[o->ncmd++] = g_stg.cmd;
		if (sc_denied(g_stg.cmd, g_inp.ctx))
			g_inp.allow_cmd = g_stg.cmd;
		g_stg.tap_on--;
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: our press %llu of its command %d '%s'", t, t - g_stg.t0,
			      (unsigned long long)g_stg.n_presses, g_stg.cmd, bo_cmd_name(g_stg.cmd));
	}
}

/* the input's wants (the struggle context): an aim struggle's direction from the hand = MenuAxisX (56) / MenuAxisY
 * (52), the axes CStruggleAim's update reads (re-xr-pipeline.md 108.6); none = the stick's own (the pad's bindings) */
static int stg_axis_wants(int cmd[2], float val[2])
{
	if (g_stg.on && g_stg.rule && g_stg.obj && g_stg.ours && g_stg.act == SOMVR_SGA_SWIPE) { /* build 41: the direction's */
		cmd[0] = 56, val[0] = g_stg.dir_now[0];                                         /* vector, else neutral */
		cmd[1] = 52, val[1] = g_stg.dir_now[1];
		return 2;
	}
	if (!g_stg.on || !g_stg.obj || !(g_stg.rule ? g_stg.ours && g_stg.act == SOMVR_SGA_STEER : g_stg.death) ||
	    g_stg.kind != STG_AIM || !g_stg.aim_have || !(g_stg.aim_via & 2))
		return 0;
	cmd[0] = 56, val[0] = g_stg.aim[0];
	cmd[1] = 52, val[1] = g_stg.aim[1];
	g_stg.n_aim_ticks++;
	return 2;
}

static void stg_end_log(void)
{
	logf_("STRUGGLE END build 27 (SOMVR_STRUGGLE=%s): struggles %llu (press-count %llu, quick-time %llu, aim %llu, hold "
	      "%llu, direction %llu, unknown %llu; no command known %llu); thrusts %llu (in a struggle with its command %llu), "
	      "our presses %llu; results changed %llu; input ticks aimed by a hand %llu (as the pad's stick %llu), thrusts outside the ring %llu", g_stg.on ? "thrust" : "off",
	      (unsigned long long)g_stg.n_struggles,
	      (unsigned long long)g_stg.n_kind[STG_PRESS], (unsigned long long)g_stg.n_kind[STG_QT],
	      (unsigned long long)g_stg.n_kind[STG_AIM], (unsigned long long)g_stg.n_kind[STG_HOLD],
	      (unsigned long long)g_stg.n_kind[STG_DIR], (unsigned long long)g_stg.n_kind[STG_OTHER],
	      (unsigned long long)g_stg.n_nocmd, (unsigned long long)g_stg.n_thrusts, (unsigned long long)g_stg.n_thrusts_in,
	      (unsigned long long)g_stg.n_presses, (unsigned long long)g_stg.n_done, (unsigned long long)g_stg.n_aim_ticks,
	      (unsigned long long)g_stg.n_aim_pad, (unsigned long long)g_stg.n_aim_outside);
	logf_("STRUGGLE END build 27: a captain's finisher answered %llu, stabs at him %llu, completed %llu "
	      "(SOMVR_STRUGGLE_FINISHER=%s)", (unsigned long long)g_stg.n_fin, (unsigned long long)g_stg.n_fin_stabs,
	      (unsigned long long)g_stg.n_fin_won, g_stg.finisher ? "on" : "off");
	if (g_stg.obj && g_stg.rule && g_stg.ours)
		stg_b41_end(cb_now());
	logf_("STRUGGLE END build 41 (SOMVR_STRUGGLE_RULE=%s): answered by thrusts %llu, one thrust %llu, the hold %llu, the "
	      "swipe %llu, the stab %llu, the steer %llu, the beast's grip %llu; won %llu, lost %llu; nodes from the game %llu, an "
	      "orc's Neck %llu; counted presses %llu, quick-time thrusts outside %llu, holds %llu, swipes %llu / leans %llu (the "
	      "way %llu, ignored %llu; sent %llu), the blade laid %llu, stabs %llu, completed %llu; cues %llu",
	      g_stg.rule ? "b41" : "b27", (unsigned long long)g_stg.n_rule[SOMVR_SGA_THRUSTS],
	      (unsigned long long)g_stg.n_rule[SOMVR_SGA_THRUST], (unsigned long long)g_stg.n_rule[SOMVR_SGA_HOLD],
	      (unsigned long long)g_stg.n_rule[SOMVR_SGA_SWIPE], (unsigned long long)g_stg.n_rule[SOMVR_SGA_STAB],
	      (unsigned long long)g_stg.n_rule[SOMVR_SGA_STEER], (unsigned long long)g_stg.n_rule[SOMVR_SGA_GRIP],
	      (unsigned long long)g_stg.n_won41, (unsigned long long)g_stg.n_lost41, (unsigned long long)g_stg.n_anc_game,
	      (unsigned long long)g_stg.n_anc_orc, (unsigned long long)g_stg.n_counted, (unsigned long long)g_stg.n_qt_outside,
	      (unsigned long long)g_stg.n_holds, (unsigned long long)g_stg.n_swipes, (unsigned long long)g_stg.n_leans,
	      (unsigned long long)g_stg.n_swipe_ok, (unsigned long long)g_stg.n_swipe_off, (unsigned long long)g_stg.n_dir_sent,
	      (unsigned long long)g_stg.n_laid, (unsigned long long)g_stg.n_stabs, (unsigned long long)g_stg.n_stab_won,
	      (unsigned long long)g_stg.n_cue);
}
