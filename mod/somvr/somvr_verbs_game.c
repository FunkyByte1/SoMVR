/* somvr_verbs_game.c - build 13 candidate: the game's Nemesis verbs in VR (docs/re-xr-pipeline.md 39; included last by
 * somvr.c, one translation unit; it uses combat's orc reads, the input's press path and the prompts' hooks).
 *
 * SOMVR_VERBS=off|log|on (default off). The game's own verbs - the grab of a stunned or unaware orc, then interrogate,
 * drain, brand, kill; the stealth kill - are the game's attacks, picked by its command and its predicates. The game's
 * prompts (PromptShow {key, action, StringId}, somvr_prompts_game.c) say which verb it offers right now and on which
 * command; our controls press that command, so the game's rules (the stun, the skill, the target, the unlock) decide.
 *   log: the lines only (the prompts the verbs need, Talion's and the near orcs' attack / state / sync by name on each
 *        change); no press of ours.
 * Test words (SOMVR_HANDS_TEST=1 only): "verbs press CMD[+CMD] MS" holds the stock command(s) for MS ms through the
 * input's press path (the deny list's commands too: a test of the flat game's buttons), "verbs status". */

#include "somvr_verbs.h"

enum { VB_OFF, VB_LOG, VB_ON };
#define VB_CMD_B 120     /* CombatPrimary03: pad B (the stun on a tap, the wraith hand's hold - the drain - held) */
#define VB_CMD_GRAB 123  /* CombatSecondary02: the grab (Grab_Attempt pressed, Grab_Held held, Grab_OnRelease let go) */
#define VB_CMD_A 121     /* CombatPrimary04: pad A, while grabbing the interrogation (Grab_Subdue) */
#define VB_CMD_X 118     /* CombatPrimary01: pad X, while grabbing the grab's move (UI_Combat_GrabFlurry), the stealth kill */
#define VB_PRESS_N 4     /* commands at once (vb_press_cmds) */
#define VB_HEAD_UP_CM 75 /* an orc's head above its root without its Head node (combat's capsule: 90 up) */
#define VB_BACK_APART_CM 30 /* build 19b: the hands closer than this (the left on the hilt): a stealth target's back takes no grab */
#define VB_GRAB_CM 300   /* orcs this close to Talion's root are offered to the left hand */
#define VB_PROMPTS 16   /* prompts up at once (each is shown on the HUD's three diamonds: one entry per key and action) */
#define VB_ORCS 6       /* near orcs followed (their state, attack and sync logged on each change) */
#define VB_NEAR_CM 600  /* the followed orcs' range */
#define VB_LINES 1500   /* the change lines' budget */
#define VB_C_STATE 0x268 /* Combat: the combat state record (Combat/States/Definitions; BM_STATE's getter) */
#define VB_C_ATTACK 0x4d0 /* Combat: the current attack record (BM_CURATK) */

struct vb_prompt { uintptr_t key; int act; int n; double t; char str[64]; };
struct vb_orc { uintptr_t c; uintptr_t state, atk, sync; };
struct vb_head { uintptr_t c; int st; }; /* the Head node per orc: 0 unknown, 1 read, -1 none (the estimate) */

static struct {
	int mode, test, tid;
	/* the test press: the writer thread's request, the game thread's hold */
	_Atomic uint32_t req_seq;
	_Atomic int req_cmd0, req_cmd1, req_ms;
	_Atomic int req_status;
	_Atomic int req_behind_cm; /* the test word "verbs behind CM": Talion teleported CM behind the nearest orc */
	_Atomic int req_tree;      /* the test word "verbs tree": the player's root nodes, their chords, the attacks */
	int ok_tree;               /* FINISH's selection sites matched (the tree's attacks, the stealth kill's ask) */
	int stealth;               /* SOMVR_VERBS_STEALTH: the stealth kill by a blade contact (default 1 with on) */
	char stealth_node[64];     /* SOMVR_VERBS_STEALTH_NODE */
	uintptr_t stealth_c;
	double stealth_t, stealth_line;
	uint64_t stealth_asks, stealth_kills;
	uint32_t req_done;
	int pcmd[2];
	double press_until;
	/* the prompts up (the PP hooks, the game thread) */
	struct vb_prompt up[VB_PROMPTS];
	int nup;
	/* Talion's and the near orcs' records, logged on change */
	uintptr_t t_atk, t_state, t_sync;
	struct vb_orc orc[VB_ORCS];
	int norc;
	/* the left hand on an orc's head (SOMVR_VERBS=on): somvr_vb_hand on the game thread */
	somvr_vb_cfg cfg;
	somvr_vb_hand hand;
	float haptic;               /* SOMVR_VERBS_HAPTIC: the hold's pulses' scale (0 = none) */
	int ok_head;                /* NS_NODEXF matched: the orcs' Head node */
	int nheads;                 /* the near orcs' heads (vb_frame, for the next in_frame) */
	uintptr_t head_c[SOMVR_VB_ORCS];
	float heads[SOMVR_VB_ORCS][3];
	float body[SOMVR_VB_ORCS][3], body_cm[SOMVR_VB_ORCS]; /* build 19b: a stealth target's back (SOMVR_STEALTH_KILL_GRAB_CM) */
	uint64_t back_grabs, back_squeezes;
	float root[3];              /* Talion's root at the last vb_frame (the squeeze's geometry line) */
	uint32_t blade_lines;       /* build 19b: the held orc's sweep lines */
	int have_root;
	struct vb_head hst[16];
	int nhst;
	uint64_t heads_frame;
	int press_b, press_grab;    /* hold B / the grab (123) this input tick */
	double tap_until;           /* tap tap_cmd until then (A at the interrogation's pull, its choices) */
	int tap_cmd;
	uint64_t choices;
	uint64_t a_taps, grab_ticks, flurries;
	uint64_t took_frame;        /* the frame a squeeze was the verbs' (SOMVR_CLIMB grabs nothing then) */
	uintptr_t held_c;           /* the orc the hand took */
	int kind, kind_was;         /* Talion's attack record's kind (somvr_vb_attack_kind) */
	int game_done;              /* the game's hold ended since the grab */
	int saw_hog;                /* the game's hold started since the grab */
	double next_buzz, hold_t0;
	double hands_t;             /* the last vb_hands (stereo frames only) */
	double dbg_t;               /* the test's holding line */
	double blade_t;             /* the held orc's own sweep: the last contact */
	int trig_was;               /* the trigger's own hysteresis (the interrogation's choice) */
	uint32_t btn_was[2];        /* the Touch buttons last frame (the prompts' own letters) */
	uint64_t btn_taps;
	uintptr_t ask_c;            /* the orc asked (the interrogation: until its choices are gone) */
	double ask_t;
	/* build 20 (docs/re-xr-pipeline.md 77): the interrogated orc's kill by the player's own blade (SOMVR_VERBS_ASK_KILL);
	 * Talion's Combat read for the kind through a body moment's hold too (the test words `verbs old|new`) */
	int ask_kill, ask_kill_cfg; /* VB_ASK_GAME (build 19b: the blade and B = the game's Eliminate) | VB_ASK_BLADE */
	int own_read;               /* 1: the kind from Talion's Combat when combat_frame has no player (0 = build 19b's) */
	uint64_t ask_kills, ask_weak, own_reads, ask_freed;
	/* build 20 (SOMVR_VERBS_INTEL_BACK_S): the GAIN INTEL menu after Interrogate, left by the pad's Back after this long
	 * (the game back at the held orc and its choices; run out by itself the game plays its finisher) */
	float intel_back_s, intel_back_cfg;
	double intel_ask_t, intel_t0, intel_back_until;
	int intel_up, intel_backed;
	uint64_t intels, intel_backs, intel_outs;
	/* the view while the hand holds an orc (SOMVR_VERBS_VIEW_BACK_CM): the game's grab puts the orc's head where
	 * Talion's is (run 234655, mark 3: the view inside it); the held view eased back from it */
	float back_cm, voff[3], vvel[3], vdir[3];
	int vdir_ok, voff_on;
	double vt;
	float vmax_step;
	uint64_t view_frames;
	float tap_s;                /* SOMVR_VERBS_TAP_S: the stun's tap window (the game's 0.2 s) for a VR trigger */
	int tap_done;               /* written (1), or found nothing to write (-1) */
	_Atomic int aim;            /* the test word "verbs aim head|face|off" (SOMVR_HANDS_TEST): the left palm put at the
				     * nearest orc's head (its collar, 8 cm under it) or 25 cm from the eyes toward it */
	uint64_t stale_lets;        /* holds let go because no stereo frame read the hand for 0.5 s */
	/* counts */
	uint64_t frames, presses, press_ticks, shows, hides, holds, lines, suppressed;
	uint64_t ev[SOMVR_VBE_N], b_ticks, hog_starts, hog_completes, hog_outs, stuns, buzzes, head_node, head_est;
} g_vb;

enum { VB_ASK_GAME, VB_ASK_BLADE };

static void gc_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, double t); /* build 21 (somvr_combat21_game.c) */
static void gc_hands(const somvr_vb_hand_in *in, double t);
static int vb_log_ok(void)
{
	if (g_vb.lines < VB_LINES) {
		g_vb.lines++;
		return 1;
	}
	g_vb.suppressed++;
	return 0;
}

static const char *vb_name(uintptr_t rec, char *out, size_t cap)
{
	rx_rec_name(rec, out, cap);
	if (!out[0])
		snprintf(out, cap, rec ? "%#lx" : "-", (unsigned long)rec);
	return out;
}

/* an orc's (or Talion's) health: its CCharacter's +0x164 (PR_CH_HEALTH, the practice's reads); NaN = unread */
static float vb_health(uintptr_t c)
{
	float h = NAN;
	uintptr_t ch = c ? peek_ptr(c + C_OWNER) : 0;
	if (!ch || !peek(ch + PR_CH_HEALTH, &h, sizeof h))
		h = NAN;
	return h;
}

static uintptr_t vb_sync_rec(uintptr_t c)
{
	uintptr_t sy = c ? peek_ptr(c + C_SYNC) : 0;
	return sy ? peek_ptr(sy + FB_SYNC_REC) : 0;
}

/* the PP hooks (the game thread; somvr_prompts_game.c, any mode but game): a prompt shown or hidden */
static void vb_prompt(uintptr_t key, int act, const char *str, int up)
{
	if (!g_vb.mode)
		return;
	int i = 0;
	while (i < g_vb.nup && !(g_vb.up[i].key == key && g_vb.up[i].act == act))
		i++;
	double t = cb_now();
	if (up) {
		g_vb.shows++;
		if (i < g_vb.nup) {
			g_vb.up[i].n++;
			return;
		}
		if (g_vb.nup == VB_PROMPTS)
			return;
		struct vb_prompt *p = &g_vb.up[g_vb.nup++];
		memset(p, 0, sizeof *p);
		p->key = key, p->act = act, p->n = 1, p->t = t;
		go_str(str, p->str, sizeof p->str);
		char kn[96];
		if (vb_log_ok())
			logf_("VERBS t=%.3f prompt up: action %d '%s' '%s' key %#lx '%s'", t, act, in_cmd_name(act), p->str,
			      (unsigned long)key, vb_name(key, kn, sizeof kn));
		return;
	}
	g_vb.hides++;
	if (i == g_vb.nup)
		return;
	if (--g_vb.up[i].n > 0)
		return;
	if (vb_log_ok())
		logf_("VERBS t=%.3f prompt down: action %d '%s' '%s' (up %.2f s)", t, act, in_cmd_name(act), g_vb.up[i].str,
		      t - g_vb.up[i].t);
	g_vb.up[i] = g_vb.up[--g_vb.nup];
}

/* the prompt's text (the PP show hook, the game thread): our words in the StringId's place, or NULL = the game's. The
 * diamond keeps the pointer (its entry +0x10), so the words live in static storage for the whole run. words test (a
 * test): every prompt's text = "VR " + its StringId - does the HUD show a StringId it cannot find as the text itself? */
#define VB_WORDS 32
static struct { char from[64], to[96]; } g_vb_words[VB_WORDS];
static int g_vb_nwords;
static _Atomic int g_vb_words_mode; /* 0 off, 1 on (the verbs' table), 2 test */

/* a struggle choice up now (CombatStruggle01..10, the interrogation's: 'Domination_Interrogate',
 * 'UI_Prompt_Interrogate_Eliminate'): its command, 0 = none. kill: the one whose StringId names Eliminate */
static int vb_choice(int kill)
{
	for (int i = 0; i < g_vb.nup; i++) {
		int a = g_vb.up[i].act;
		if (a < 161 || a > 170)
			continue;
		int el = strstr(g_vb.up[i].str, "Eliminate") != NULL || strstr(g_vb.up[i].str, "Kill") != NULL;
		if (kill ? el : (!el && strstr(g_vb.up[i].str, "Interrogate") != NULL))
			return a;
	}
	return 0;
}
static void vb_tap(int cmd, double t) { g_vb.tap_cmd = cmd, g_vb.tap_until = t + 0.15; }

/* SOMVR_PAD_PROMPTS=vr (the show hook, the game thread): the grab's own prompts are kept (their keys 'Grab_Button*':
 * 'UI_Combat_GrabFlurry' on X 118, 'Combat_Grab_Drain' on B 120, 'Domination_Subdue' on A 121; run 233249) - the
 * verbs press those commands; B is shown with the left trigger's glyph (the PP map 120:124) */
static int vb_keep(int act, uintptr_t key)
{
	if (g_vb.mode != VB_ON)
		return 0;
	if (act >= 161 && act <= 170 && g_vb.hand.phase == SOMVR_VBH_GRAB)
		return 1; /* CombatStruggle01..10 while the hand holds: the interrogation's choices (run 234655) */
	if (!key)
		return 0;
	static uintptr_t k_yes[16], k_no[64];
	static int n_yes, n_no;
	for (int i = 0; i < n_yes; i++)
		if (k_yes[i] == key)
			return 1;
	for (int i = 0; i < n_no; i++)
		if (k_no[i] == key)
			return 0;
	char nm[96];
	int yes = !strncmp(rx_rec_name(key, nm, sizeof nm), "Grab_Button", 11);
	if (yes && n_yes < 16)
		k_yes[n_yes++] = key;
	else if (!yes && n_no < 64)
		k_no[n_no++] = key;
	return yes;
}

/* our own words in a prompt: the HUD looks the StringId up and shows "[ERROR: Invalid StringID]" for one it does not
 * have (run 20261026-005010, the band's dump 721): no own words; the prompts keep the game's text, their letters are the
 * Touch's where a Touch button does the same (X, A, B; B's drain and the choice's Interrogate shown as LT) */
static const char *vb_text(int act, const char *str)
{
	int wm = atomic_load_explicit(&g_vb_words_mode, memory_order_relaxed);
	if (!g_vb.mode || !wm || !str)
		return NULL;
	char sb[64];
	go_str(str, sb, sizeof sb);
	if (!sb[0])
		return NULL;
	for (int i = 0; i < g_vb_nwords; i++)
		if (!strcmp(g_vb_words[i].from, sb))
			return g_vb_words[i].to[0] ? g_vb_words[i].to : NULL;
	if (wm != 2 || g_vb_nwords == VB_WORDS)
		return NULL;
	snprintf(g_vb_words[g_vb_nwords].from, sizeof g_vb_words[0].from, "%s", sb);
	snprintf(g_vb_words[g_vb_nwords].to, sizeof g_vb_words[0].to, "VR %s", sb);
	if (vb_log_ok())
		logf_("VERBS words test: action %d '%s' text '%s' -> '%s'", act, in_cmd_name(act), sb, g_vb_words[g_vb_nwords].to);
	return g_vb_words[g_vb_nwords++].to;
}

static void vb_prompt_hold(int act, float fill)
{
	if (!g_vb.mode)
		return;
	if (g_vb.holds++ < 40 && vb_log_ok())
		logf_("VERBS t=%.3f prompt hold: action %d '%s' fill %.2f", cb_now(), act, in_cmd_name(act), fill);
}

/* the input tick (in_apply): the commands to hold this tick (the deny list's too: our press, never a pad button) */
static void vb_out_add(int *out, int *n, int cmd)
{
	for (int k = 0; k < *n; k++)
		if (out[k] == cmd)
			return;
	if (*n < VB_PRESS_N)
		out[(*n)++] = cmd;
}
static int vb_press_cmds(int out[4])
{
	if (!g_vb.mode)
		return 0;
	double t = cb_now();
	int n = 0;
	if (t < g_vb.press_until)
		for (int k = 0; k < 2; k++)
			if (g_vb.pcmd[k] > 0)
				vb_out_add(out, &n, g_vb.pcmd[k]);
	if (n)
		g_vb.press_ticks++;
	if (g_vb.mode != VB_ON)
		return n;
	if (g_vb.press_grab)
		vb_out_add(out, &n, VB_CMD_GRAB), g_vb.grab_ticks++;
	if (g_vb.press_b)
		vb_out_add(out, &n, VB_CMD_B), g_vb.b_ticks++;
	if (t < g_vb.tap_until && g_vb.tap_cmd > 0)
		vb_out_add(out, &n, g_vb.tap_cmd);
	return n;
}

/* eye 0 (bm_base, after the body moments' base; the game thread): while the hand holds an orc and the base is held,
 * the view eased back_cm away from the held orc's head (horizontally; a critically damped ease, 0.08 s), and back to
 * nothing after; pos is changed in place */
static void vb_view(float pos[3], int frozen, double t)
{
	if (g_vb.mode != VB_ON || !(g_vb.back_cm > 0))
		return;
	double dt = g_vb.vt > 0 ? t - g_vb.vt : 0;
	g_vb.vt = t;
	if (!(dt > 0) || dt > 0.1)
		dt = 0;
	int want = frozen && g_vb.hand.phase == SOMVR_VBH_GRAB && g_vb.held_c;
	if (want && !g_vb.vdir_ok) { /* the direction once per grab: from the held orc's head to the eyes */
		for (int i = 0; i < g_vb.nheads; i++)
			if (g_vb.head_c[i] == g_vb.held_c) {
				float d[3] = { pos[0] - g_vb.heads[i][0], 0, pos[2] - g_vb.heads[i][2] }, l = hypotf(d[0], d[2]);
				if (l > 1)
					g_vb.vdir[0] = d[0] / l, g_vb.vdir[1] = 0, g_vb.vdir[2] = d[2] / l, g_vb.vdir_ok = 1;
			}
	}
	if (!want)
		g_vb.vdir_ok = 0;
	float target[3] = { 0, 0, 0 };
	if (want && g_vb.vdir_ok)
		for (int k = 0; k < 3; k++)
			target[k] = g_vb.vdir[k] * g_vb.back_cm;
	const float w = 1.0f / 0.08f;
	float step = 0;
	for (int k = 0; k < 3; k++) {
		float x = g_vb.voff[k], v = g_vb.vvel[k], a = -2 * w * v - w * w * (x - target[k]);
		v += a * (float)dt, x += v * (float)dt;
		step += (x - g_vb.voff[k]) * (x - g_vb.voff[k]);
		g_vb.voff[k] = x, g_vb.vvel[k] = v;
	}
	step = sqrtf(step);
	int on = fabsf(g_vb.voff[0]) + fabsf(g_vb.voff[2]) > 0.05f;
	if (on != g_vb.voff_on && vb_log_ok())
		logf_("VERBS t=%.3f view: %s (target %.0f cm back from the held orc's head)", t, on ? "easing back" : "back at the base",
		      want ? g_vb.back_cm : 0);
	g_vb.voff_on = on;
	if (!on) {
		memset(g_vb.voff, 0, sizeof g_vb.voff), memset(g_vb.vvel, 0, sizeof g_vb.vvel);
		return;
	}
	g_vb.view_frames++;
	g_vb.vmax_step = step > g_vb.vmax_step ? step : g_vb.vmax_step;
	for (int k = 0; k < 3; k++)
		pos[k] += g_vb.voff[k];
}

/* SOMVR_BODYMOMENTS: the left hand holds an orc (the game's grab is its mount and attach states: a hold, not the
 * window; run 233249: the window took the stereo frames, so the hand was never read again until the grab ended) */
static int vb_grab_held(void)
{
	return g_vb.mode == VB_ON && (g_vb.hand.phase == SOMVR_VBH_GRAB || g_vb.ask_c); /* (the interrogation too) */
}

/* build 20 (bm_pre, SOMVR_BM_GRAB_FREE_S): the game holds Talion no more - an interrogation without its choices is over */
static int vb_grab_freed(double t)
{
	if (!g_vb.ask_c || vb_choice(0) || vb_choice(1))
		return 0;
	if (vb_log_ok())
		logf_("VERBS t=%.3f the interrogation of orc %#lx is over (%.1f s after the pull): the game holds Talion no more",
		      t, (unsigned long)g_vb.ask_c, t - g_vb.ask_t);
	g_vb.ask_c = 0, g_vb.ask_freed++;
	return 1;
}

/* build 20: Talion's server Combat when combat_frame passes no player (fb_after_body: a body moment's hold with the view
 * away from his head, a knockdown) - the stereo frame's player as the bodymoments' read takes it (vtable checked) */
static uintptr_t vb_talion_combat(void)
{
	uintptr_t pl = g_vb.own_read ? g_fr.pl : 0, pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0;
	uintptr_t pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	return pc && vptr_of((void *)pc) == g_cbg.vt_combat && peek_ptr(pc + C_PLAYER) ? pc : 0;
}

/* SOMVR_BOW: the left trigger is the verbs' now (a grab held, the wraith press running): no bow */
static uint32_t vb_vm_avail(void) { return g_vb.mode == VB_ON ? SOMVR_VMA_VERBS : 0u; }
static int vb_trigger_taken(void) { return g_vb.mode == VB_ON && somvr_vb_trigger_taken(&g_vb.hand); }

static void vb_status(double t)
{
	char a[96], s[96], y[96];
	logf_("VERBS status t=%.3f: Talion attack '%s' state '%s' sync '%s'; %d prompts up", t, vb_name(g_vb.t_atk, a, sizeof a),
	      vb_name(g_vb.t_state, s, sizeof s), vb_name(g_vb.t_sync, y, sizeof y), g_vb.nup);
	for (int i = 0; i < g_vb.nup; i++)
		logf_("VERBS status   prompt %d '%s' '%s' since %.2f s", g_vb.up[i].act, in_cmd_name(g_vb.up[i].act),
		      g_vb.up[i].str, t - g_vb.up[i].t);
}

/* an orc's head point: its Head node (GetNodeTransform with Talion's Head hash, as SOMVR_FINISH reads it; checked once
 * per orc), else its root + VB_HEAD_UP_CM */
static void vb_head_of(const struct cb_orc *o, float out[3])
{
	out[0] = o->pos[0], out[1] = o->pos[1] + VB_HEAD_UP_CM, out[2] = o->pos[2];
	int i = 0;
	while (i < g_vb.nhst && g_vb.hst[i].c != o->c)
		i++;
	if (i == g_vb.nhst) {
		if (g_vb.nhst == 16)
			memmove(g_vb.hst, g_vb.hst + 1, 15 * sizeof g_vb.hst[0]), g_vb.nhst--, i--;
		g_vb.hst[g_vb.nhst++] = (struct vb_head){ o->c, 0 };
		i = g_vb.nhst - 1;
	}
	struct vb_head *e = &g_vb.hst[i];
	uintptr_t m = peek_ptr(g_som.model_pp), mi = peek_ptr(o->hobj + OBJ_MODEL), mv = vptr_of((void *)mi);
	if (!g_vb.ok_head || !m || e->st < 0) {
		g_vb.head_est++;
		return;
	}
	if (!e->st) { /* its model interface must reach code (GetNodeTransform's FUN_008f3c20) */
		uintptr_t f = mv ? peek_ptr(mv + 0x20) : 0;
		int pr = f ? page_prot(f) : -1;
		e->st = pr >= 0 && (pr & PROT_EXEC) ? 1 : -1;
		if (e->st < 0) {
			g_vb.head_est++;
			return;
		}
	}
	float xf[16] = { 0 };
	if ((((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, o->hobj, g_som.head, xf, 1) & 0xff) || !vec_ok(xf, 1e7f) ||
	    dist3(xf, o->pos) > 250.0f) {
		e->st = -1;
		g_vb.head_est++;
		return;
	}
	memcpy(out, xf, 12);
	g_vb.head_node++;
}

/* vb_frame: the near orcs' heads for the next in_frame's hand (within VB_GRAB_CM of Talion's root); a stealth target's
 * back too (build 19b, SOMVR_STEALTH_KILL_GRAB_CM: somvr_stealth_game.c says which orcs and how near) */
static void vb_heads(const struct cb_orc *o, int n, uintptr_t pc, const float *root)
{
	int k = 0;
	for (int i = 0; i < n && k < SOMVR_VB_ORCS; i++) {
		if (o[i].dist > VB_GRAB_CM)
			continue;
		g_vb.head_c[k] = o[i].c;
		vb_head_of(&o[i], g_vb.heads[k]);
		g_vb.body_cm[k] = stl_grab_back(&o[i], pc, root, g_vb.body[k]);
		k++;
	}
	g_vb.nheads = k, g_vb.heads_frame = g_fr.frame;
}

/* vb_frame: Talion's attack record's kind; the game's hold's start and end while the hand holds */
static void vb_kind(uintptr_t pc, double t)
{
	char an[96];
	rx_rec_name(peek_ptr(pc + VB_C_ATTACK), an, sizeof an);
	int k = somvr_vb_attack_kind(an);
	if (k == g_vb.kind)
		return;
	g_vb.kind_was = g_vb.kind, g_vb.kind = k;
	int holding = g_vb.hand.phase != SOMVR_VBH_IDLE;
	if (k == SOMVR_VBK_HOG && g_vb.hand.b && !g_vb.saw_hog)
		g_vb.saw_hog = 1, g_vb.hog_starts++, g_vb.hold_t0 = t;
	if (k == SOMVR_VBK_STUN && holding)
		g_vb.stuns++;
	if (k == SOMVR_VBK_HOG_END && (holding || g_vb.saw_hog)) {
		int comp = strstr(an, "Complete") != NULL || strstr(an, "_Kill") != NULL;
		comp ? g_vb.hog_completes++ : g_vb.hog_outs++;
		if (holding && g_vb.hand.b)
			g_vb.game_done = 1;
		if (comp && g_vb.haptic > 0)
			in_haptic_event(SOMVR_HAND_L, 0.8f * g_vb.haptic, 0.12f, SOMVR_HAP_COMBAT);
	}
	if (vb_log_ok())
		logf_("VERBS t=%.3f the game: Talion's '%s' (%s)%s", t, an[0] ? an : "-", somvr_vb_kind_name(k),
		      holding ? " - the left hand holds" : "");
}

/* build 19b: a squeeze with a stealth target near (its back offered: SOMVR_STEALTH_KILL_GRAB_CM): where the hand was -
 * the palm, the eyes, Talion's root and the orc's root and head in cm ahead / up / right of the eyes along the eyes ->
 * its root (the real reaches the headset logs; the tests' poses) */
static void vb_back_line(const somvr_vb_hand_in *in, const somvr_vb_hand_out *o, double t)
{
	g_vb.back_squeezes++;
	int i = -1;
	float bd = INFINITY;
	for (int k = 0; k < in->norc && k < SOMVR_VB_ORCS; k++)
		if (in->body_cm[k] > 0 && dist3(in->palm, in->body[k]) < bd)
			bd = dist3(in->palm, in->body[k]), i = k;
	if (i < 0 || !vb_log_ok())
		return;
	float fx = in->body[i][0] - in->eye[0], fz = in->body[i][2] - in->eye[2], fl = sqrtf(fx * fx + fz * fz);
	if (!(fl > 1))
		fx = 0, fz = 1, fl = 1;
	fx /= fl, fz /= fl;
	float r[5][3];
	const float *pt[5] = { in->palm, in->eye, g_vb.have_root ? g_vb.root : in->eye, in->body[i], in->head[i] };
	for (int k = 0; k < 5; k++) {
		float dx = pt[k][0] - in->eye[0], dz = pt[k][2] - in->eye[2];
		r[k][0] = dx * fx + dz * fz, r[k][1] = pt[k][1] - in->eye[1], r[k][2] = dx * fz - dz * fx; /* (the sign of right: the game's) */
	}
	logf_("VERBS t=%.3f squeeze by a stealth target (orc %#lx, its back offered within %.0f cm): the palm %.1f cm from its "
	      "back (the reach %.1f deg off it), %.1f cm from its head; %s; cm ahead/up/side of the eyes toward it: the palm "
	      "%+.0f %+.0f %+.0f, Talion's root %+.0f %+.0f %+.0f, its root %+.0f %+.0f %+.0f, its head %+.0f %+.0f %+.0f", t,
	      (unsigned long)g_vb.head_c[i], (double)in->body_cm[i], (double)o->back_cm, (double)o->back_deg,
	      dist3(in->palm, in->head[i]), o->event == SOMVR_VBE_GRAB ? (o->by_body ? "taken by its back" : "taken by a head")
	      : "nothing taken", r[0][0], r[0][1], r[0][2], r[2][0], r[2][1], r[2][2], r[3][0], r[3][1], r[3][2], r[4][0],
	      r[4][1], r[4][2]);
}

/* in_frame (a stereo frame, the game thread), after the slots and before the climb: the left hand's grab. p, pw: the
 * grips (tracking space, world) */
static void vb_hands(const sv_vec3 pw[2], sv_vec3 eye)
{
	if (g_vb.mode != VB_ON)
		return;
	double t = cb_now();
	g_vb.hands_t = t;
	const somvr_hand *L = &g_inp.hands[SOMVR_HAND_L];
	somvr_vb_hand_in in;
	memset(&in, 0, sizeof in);
	in.t = t;
	/* a new grab in the world only; a hold goes on through the game's own contexts (not a menu) */
	in.live = (g_vb.hand.phase != SOMVR_VBH_IDLE ? g_inp.ctx != SC_CTX_MENU : g_inp.ctx == SC_CTX_WORLD) && !game_paused() &&
		  !vm_open() && !bo_out() && !cl_holds(SOMVR_HAND_L) && !sl_nograb();
	in.squeeze = bd_squeeze(SOMVR_HAND_L, g_inp.st.hand[SOMVR_HAND_L].squeeze);
	in.trigger = (g_inp.st.hand[SOMVR_HAND_L].active & SOMVR_HACT_INPUT) ? g_inp.st.hand[SOMVR_HAND_L].trigger : 0;
	if (g_bd.test && g_bf.t_trig[SOMVR_HAND_L] >= 0) /* SOMVR_HANDS_TEST: "hands trigger L V" (a frame's timing) */
		in.trigger = g_bf.t_trig[SOMVR_HAND_L];
	in.have_palm = L->valid;
	in.palm[0] = pw[0].x, in.palm[1] = pw[0].y, in.palm[2] = pw[0].z;
	in.have_eye = 1, in.eye[0] = eye.x, in.eye[1] = eye.y, in.eye[2] = eye.z;
	int fresh = g_fr.frame - g_vb.heads_frame <= 2;
	int aim = g_vb.test ? atomic_load_explicit(&g_vb.aim, memory_order_relaxed) : 0;
	if (aim == 2 && g_vb.hand.phase == SOMVR_VBH_GRAB && !(fresh && g_vb.nheads > 0)) {
		/* the held orc leaves the targeting bands (run 022511): toward where it was, else ahead */
		float f[3] = { -g_vb.vdir[0], 0, -g_vb.vdir[2] };
		if (!g_vb.vdir_ok)
			f[0] = sinf(g_inp.hv), f[2] = cosf(g_inp.hv);
		in.have_palm = 1;
		for (int k = 0; k < 3; k++)
			in.palm[k] = in.eye[k] + f[k] * 25.0f;
	} else if (aim && fresh && g_vb.nheads > 0) { /* a test: the palm where a player's would be (no choreography) */
		int i = 0;
		for (int k = 0; k < g_vb.nheads && g_vb.held_c; k++)
			if (g_vb.head_c[k] == g_vb.held_c)
				i = k;
		float *h = g_vb.heads[i], d[3] = { h[0] - in.eye[0], h[1] - in.eye[1], h[2] - in.eye[2] };
		float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		in.have_palm = 1;
		if (aim == 1 || !(l > 1))
			in.palm[0] = h[0], in.palm[1] = h[1] - 8, in.palm[2] = h[2];
		else
			for (int k = 0; k < 3; k++)
				in.palm[k] = in.eye[k] + d[k] / l * 25.0f;
	}
	in.norc = fresh ? g_vb.nheads : 0;
	memcpy(in.head, g_vb.heads, sizeof in.head);
	memcpy(in.body, g_vb.body, sizeof in.body), memcpy(in.body_cm, g_vb.body_cm, sizeof in.body_cm);
	/* the hands together (the left on the sword's hilt, SOMVR_TWOHAND's two-handed thrust at its back): not its back */
	if (g_inp.hands[SOMVR_HAND_R].valid && sqrtf((pw[0].x - pw[1].x) * (pw[0].x - pw[1].x) + (pw[0].y - pw[1].y) *
	    (pw[0].y - pw[1].y) + (pw[0].z - pw[1].z) * (pw[0].z - pw[1].z)) < VB_BACK_APART_CM)
		memset(in.body_cm, 0, sizeof in.body_cm);
	in.game_done = g_vb.game_done;
	int was = g_vb.hand.phase, b_was = g_vb.hand.b;
	somvr_vb_hand_out o;
	somvr_vb_hand_step(&g_vb.hand, &g_vb.cfg, &in, &o);
	g_vb.press_grab = o.grab, g_vb.press_b = o.b;
	gc_hands(&in, t); /* build 21, SOMVR_VERBS_CUE: the reach, the cue, its tick and glow (somvr_combat21_game.c) */
	/* the interrogation asks (its choices up; the grip may have opened meanwhile): the trigger's press = Interrogate */
	int tr_was = g_vb.trig_was;
	g_vb.trig_was = tr_was ? in.trigger > g_vb.cfg.trig_off : in.trigger >= g_vb.cfg.trig_on;
	int ch = g_vb.ask_c ? vb_choice(0) : 0;
	if (ch) {
		g_vb.press_b = 0;
		if (g_vb.trig_was && !tr_was) {
			vb_tap(ch, t), g_vb.choices++;
			g_vb.intel_ask_t = t; /* build 20: the intel menu comes next */
			g_vb.hand.b = 0, g_vb.hand.b_done = 1; /* no B from this press when the choices are gone */
			if (o.event == SOMVR_VBE_WRAITH)
				o.event = SOMVR_VBE_NONE;
			if (g_vb.haptic > 0)
				in_haptic_event(SOMVR_HAND_L, 0.5f * g_vb.haptic, 0.06f, SOMVR_HAP_COMBAT);
			if (vb_log_ok())
				logf_("VERBS t=%.3f choice: the trigger while the interrogation asks: %d '%s' tapped (Interrogate)", t, ch,
				      in_cmd_name(ch));
		}
	}
	/* the prompts' own letters on the Touch: holding, X (the left lower button) = 118 (UI_Combat_GrabFlurry) and A (the
	 * right lower) = 121 (Domination_Subdue: the interrogation); its choice, B (the right upper) = Eliminate */
	uint32_t bl = (g_inp.st.hand[SOMVR_HAND_L].active & SOMVR_HACT_INPUT) ? g_inp.st.hand[SOMVR_HAND_L].buttons : 0;
	uint32_t br = (g_inp.st.hand[SOMVR_HAND_R].active & SOMVR_HACT_INPUT) ? g_inp.st.hand[SOMVR_HAND_R].buttons : 0;
	uint32_t el = bl & ~g_vb.btn_was[0], er = br & ~g_vb.btn_was[1];
	g_vb.btn_was[0] = bl, g_vb.btn_was[1] = br;
	int bcmd = 0;
	if ((er & SOMVR_BTN_UPPER) && g_vb.ask_c && vb_choice(1))
		bcmd = vb_choice(1);
	else if (g_vb.hand.phase == SOMVR_VBH_GRAB && !g_vb.ask_c && (el & SOMVR_BTN_LOWER))
		bcmd = VB_CMD_X;
	else if (g_vb.hand.phase == SOMVR_VBH_GRAB && !g_vb.ask_c && (er & SOMVR_BTN_LOWER))
		bcmd = VB_CMD_A, g_vb.ask_c = g_vb.held_c, g_vb.ask_t = t;
	if (bcmd) {
		vb_tap(bcmd, t), g_vb.btn_taps++;
		if (vb_log_ok())
			logf_("VERBS t=%.3f button: the Touch's %s: %d '%s' tapped (the prompt's own letter)", t,
			      bcmd == VB_CMD_X ? "X" : bcmd == VB_CMD_A ? "A" : "B", bcmd, in_cmd_name(bcmd));
	}
	if (o.b && !b_was)
		g_vb.game_done = g_vb.saw_hog = 0, g_vb.next_buzz = t + 0.25;
	if (o.event)
		g_vb.ev[o.event]++;
	char an[96], sn[96];
	uintptr_t oc = o.orc >= 0 && o.orc < g_vb.nheads ? g_vb.head_c[o.orc] : 0;
	switch (o.event) {
	case SOMVR_VBE_GRAB:
	case SOMVR_VBE_WRAITH:
		if (was == SOMVR_VBH_IDLE) {
			g_vb.took_frame = g_fr.frame;
			g_vb.held_c = oc;
			if (g_vb.haptic > 0)
				in_haptic_event(SOMVR_HAND_L, 0.35f * g_vb.haptic, 0.04f, SOMVR_HAP_COMBAT);
		}
		if (was == SOMVR_VBH_IDLE && o.by_body)
			g_vb.back_grabs++;
		if (vb_log_ok())
			logf_("VERBS t=%.3f %s: the left hand %s orc %#lx%s (%.1f cm, the reach %.1f deg off; health %.1f, attack '%s' "
			      "state '%s'): %s", t, somvr_vb_event_name(o.event), was != SOMVR_VBH_IDLE ? "holding" : o.by_cone ?
			      "reached out at" : "on", (unsigned long)(was != SOMVR_VBH_IDLE ? g_vb.held_c : oc),
			      was != SOMVR_VBH_IDLE ? "" : o.by_body ? "'s back (a stealth target: SOMVR_STEALTH_KILL_GRAB_CM)" : "'s head",
			      o.dist_cm, o.cone_deg, vb_health(g_vb.held_c),
			      vb_name(peek_ptr(g_vb.held_c + VB_C_ATTACK), an, sizeof an),
			      vb_name(peek_ptr(g_vb.held_c + VB_C_STATE), sn, sizeof sn), o.event == SOMVR_VBE_GRAB
			      ? "the grab (123) held while the grip stays closed" : "B (120) held while the trigger is");
		break;
	case SOMVR_VBE_MISS:
		if (vb_log_ok())
			logf_("VERBS t=%.3f near miss: a squeeze %.1f cm from orc %#lx's head (reach %.0f cm): nothing", t, o.dist_cm,
			      (unsigned long)oc, g_vb.cfg.reach_cm);
		break;
	case SOMVR_VBE_ASK:
		vb_tap(VB_CMD_A, t), g_vb.a_taps++;
		g_vb.ask_c = g_vb.held_c, g_vb.ask_t = t;
		if (g_vb.haptic > 0)
			in_haptic_event(SOMVR_HAND_L, 0.5f * g_vb.haptic, 0.06f, SOMVR_HAP_COMBAT);
		if (vb_log_ok())
			logf_("VERBS t=%.3f ask: orc %#lx pulled to the face (the palm %.0f cm from the eyes): A (121) tapped (the game's "
			      "interrogation)", t, (unsigned long)g_vb.held_c, dist3(in.palm, in.eye));
		break;
	case SOMVR_VBE_NONE:
		if (o.edge && g_vb.ev[0]++ < 40 && vb_log_ok()) /* a squeeze that took nothing: why (the first 40) */
			logf_("VERBS t=%.3f squeeze: nothing: live %d (ctx %d paused %d vrmenu %d bow %d climb %d slot %d) palm %d, %d heads "
			      "(%s; the nearest %.1f cm), the eyes %.0f cm from the palm, the cooldown %.2f s", t, in.live, g_inp.ctx,
			      game_paused(), vm_open(), bo_out(), cl_holds(SOMVR_HAND_L), sl_nograb(), in.have_palm, in.norc,
			      fresh ? "fresh" : "stale", o.dist_cm, dist3(in.palm, in.eye), t - g_vb.hand.t_end);
		break;
	default: /* release, lost, wraith end, done */
		if (vb_log_ok())
			logf_("VERBS t=%.3f %s after %.2f s: %s (the game's hold %s; the orc's health %.1f)", t,
			      somvr_vb_event_name(o.event), o.held_s, o.event == SOMVR_VBE_RELEASE ? "the grip opened: the grab let go"
			      : o.event == SOMVR_VBE_DONE ? (g_vb.game_done ? "B let go: the game's hold ended" : "B let go: the longest hold")
			      : o.event == SOMVR_VBE_WRAITH_END ? "the trigger let go: B let go" : "the hand is busy or the world gone",
			      g_vb.saw_hog ? "ran" : "did not run", vb_health(g_vb.held_c));
		if (g_vb.hand.phase == SOMVR_VBH_IDLE)
			g_vb.held_c = 0;
		break;
	}
	if (o.edge && o.back_cm >= 0)
		vb_back_line(&in, &o, t);
	if (was != SOMVR_VBH_IDLE || g_vb.hand.phase != SOMVR_VBH_IDLE)
		g_vb.took_frame = g_fr.frame;
	if (g_vb.test && g_vb.hand.phase == SOMVR_VBH_GRAB && t >= g_vb.dbg_t) { /* a test: the pull's numbers, once a second */
		g_vb.dbg_t = t + 1.0;
		logf_("VERBS t=%.3f holding: the palm %.1f cm from the eyes (aim %d, palm %d), away before %d, near since %.2f, asked %d",
		      t, dist3(in.palm, in.eye), aim, in.have_palm, g_vb.hand.far_seen,
		      g_vb.hand.near_t >= 0 ? t - g_vb.hand.near_t : -1.0, g_vb.hand.asked);
	}
	/* the drain's feel: a soft pulse every 0.2 s while the game's hold runs, growing over its ~3 s */
	if (o.b && g_vb.saw_hog && g_vb.kind == SOMVR_VBK_HOG && g_vb.haptic > 0 && t >= g_vb.next_buzz) {
		float k = (float)((t - g_vb.hold_t0) / 3.0);
		k = k < 0 ? 0 : k > 1 ? 1 : k;
		in_haptic_event(SOMVR_HAND_L, (0.15f + 0.3f * k) * g_vb.haptic, 0.03f, SOMVR_HAP_COMBAT);
		g_vb.next_buzz = t + 0.2, g_vb.buzzes++;
	}
}

/* SOMVR_CLIMB: the left hand's squeeze was the verbs' this frame (or it holds an orc): no wall grab */
static int vb_nograb(void) { return g_vb.mode == VB_ON && g_fr.frame - g_vb.took_frame <= 1 && g_vb.took_frame; }

/* the test word "verbs behind CM" (SOMVR_HANDS_TEST): Talion teleported CM behind the nearest orc within 20 m (the
 * console's Teleport, as the practice trips; whole centimetres), to stage a stealth approach headless */
static void vb_behind(uintptr_t pcomb, uintptr_t pc, const float root[3], float cm, double t)
{
	struct cb_orc o[1];
	int n = pc && root ? cb_orcs_r(pcomb, pc, root, 2000, o, 1) : 0;
	float q[4] = { 0, 0, 0, 1 };
	if (!n || !peek(o[0].hobj + OBJ_ROT, q, sizeof q) || !vec_ok(q, 2.0f)) {
		logf_("VERBS t=%.3f behind: no orc within 20 m (or its rotation unread)", t);
		return;
	}
	float yaw = sv_heading(sv_qnormalize(sv_quat_of(q)));
	/* right behind it, else turned about it by 30, 60, 90 deg either way: the first spot with ground at its height
	 * (the practice's ray down; run 231228's spot behind an orc on a ridge was a 6 m drop: Talion died) */
	static const float turn_deg[] = { 0, 30, -30, 60, -60, 90, -90 };
	for (size_t k = 0; k < sizeof turn_deg / sizeof turn_deg[0]; k++) {
		float a = yaw + turn_deg[k] / 57.29578f, at[3] = { o[0].pos[0] - sinf(a) * cm, o[0].pos[1], o[0].pos[2] - cosf(a) * cm };
		float hit[3] = { 0, 0, 0 };
		uintptr_t g = 0;
		if (!pr_ground(at, hit, &g) || fabsf(hit[1] - (o[0].pos[1] - 100)) > 80)
			continue; /* (the orc's root is its body's middle: its feet ~100 cm under it) */
		long x = lroundf(at[0]), y = lroundf(hit[1] + 110), z = lroundf(at[2]);
		char cmd[96], why[256] = "";
		snprintf(cmd, sizeof cmd, "Teleport %ld %ld %ld", x, y, z);
		int rc = es_run(cmd, why, sizeof why);
		logf_("VERBS t=%.3f behind: orc %#lx at %.0f,%.0f,%.0f facing %.1f deg (%.0f cm away): %+.0f deg about it, the ground "
		      "at %.0f: '%s' %s%s", t, (unsigned long)o[0].c, o[0].pos[0], o[0].pos[1], o[0].pos[2], yaw * 57.29578f,
		      o[0].dist, turn_deg[k], hit[1], cmd, rc == 0 ? "run" : "NOT run: ", rc == 0 ? "" : why);
		return;
	}
	logf_("VERBS t=%.3f behind: no ground at the orc's height %.0f cm behind it (or beside): not moved", t, cm);
}

/* the test word "verbs tree" (SOMVR_HANDS_TEST): what the game's own selection offers against the nearest orc now -
 * each root node of the player's (PlayerCombat+0xab0's Nodes, the order the update walks them) with its type, its chord
 * (node field 2 -> schema 0x4ba8ea0, its field 1 -> the input record, schema 0x4bb4560, as FUN_02272ea0 reads them) and
 * the attack SharedCombat_SelectAttackFromPool picks for that orc as an explicit target (SOMVR_FINISH's fi_select: no
 * chord check, no start). The schemas' field names are logged once. */
#define VB_TREE_NODES 160
#define VB_SCH_CHORD 0x4ba8ea0   /* the node's Predicate: 0 Input, 1 Chord, 2 DoubleTap */
#define VB_SCH_INPUT 0x4bb4560   /* its Chord: 0 Command1, 1 Command2, 2 MaxPressDelta */
#define VB_SCH_INREC 0x4bb4860   /* its Input (FUN_02271890's schema) */
#define VB_CMD_TABLE 0x4c3a3c0   /* the command table (FUN_025e1450): 349 x {name, hash, id} */
static void vb_schema(const char *what, uintptr_t holder)
{
	char line[900], nm[64];
	size_t o = (size_t)snprintf(line, sizeof line, "VERBS tree schema %s %#lx:", what, (unsigned long)holder);
	uintptr_t a = peek_ptr(holder);
	for (int i = 0; a && i < 24 && o + 70 < sizeof line; i++) {
		rx_str(peek_ptr(a + 0x30u * (unsigned)i), nm, sizeof nm);
		if (!nm[0])
			break;
		o += (size_t)snprintf(line + o, sizeof line - o, " %d=%s", i, nm);
	}
	logf_("%s", line);
}
/* a command enum value (a name hash) -> the command id, by the game's command table; -1 = none */
static int vb_cmd_of(uint32_t h)
{
	static uint32_t hs[SOMVR_NCMD];
	static int have;
	if (!have) {
		unsigned char e[16];
		for (int i = 0; i < SOMVR_NCMD; i++)
			hs[i] = peek(VB_CMD_TABLE + 16u * (unsigned)i, e, sizeof e) ? (memcpy(&hs[i], e + 8, 4), hs[i]) : 0;
		have = 1;
	}
	for (int i = 1; i < SOMVR_NCMD; i++)
		if (h && hs[i] == h)
			return i;
	return -1;
}
/* every field of rec by its schema's names (record names, numbers, the enums as commands where they are ones) */
static void vb_dump(const char *what, uintptr_t rec, uintptr_t holder)
{
	char line[900], nm[64], rn[96];
	size_t o = (size_t)snprintf(line, sizeof line, "VERBS tree   %s '%s':", what, vb_name(rec, rn, sizeof rn));
	uintptr_t a = peek_ptr(holder);
	for (int i = 0; a && i < 32 && o + 120 < sizeof line; i++) {
		rx_str(peek_ptr(a + 0x30u * (unsigned)i), nm, sizeof nm);
		if (!nm[0])
			break;
		uint64_t v[2] = { 0, 0 };
		int ty = 0, cnt = 0, k = rx_db_list(rec, holder, i, v, 2, &ty, &cnt);
		if (k < 0)
			continue;
		if (ty == 0xa)
			o += (size_t)snprintf(line + o, sizeof line - o, " %s='%s'%s", nm, vb_name((uintptr_t)v[0], rn, sizeof rn),
					      cnt > 1 ? "+" : "");
		else if (ty == 1)
			o += (size_t)snprintf(line + o, sizeof line - o, " %s=%d", nm, rx_db_bool(rec, holder, i));
		else if (ty == 2) {
			float f;
			uint32_t u = (uint32_t)v[0];
			memcpy(&f, &u, 4);
			o += (size_t)snprintf(line + o, sizeof line - o, " %s=%g", nm, (double)f);
		} else {
			int c = vb_cmd_of((uint32_t)v[0]);
			o += (size_t)snprintf(line + o, sizeof line - o, " %s=t%d:%#x%s%s", nm, ty, (unsigned)(uint32_t)v[0],
					      c > 0 ? "=" : "", c > 0 ? in_cmd_name(c) : "");
		}
	}
	logf_("%s", line);
}

/* a node schema's field index by its name (-1 = none) */
static int vb_field_ix(uintptr_t holder, const char *name)
{
	char nm[64];
	uintptr_t a = peek_ptr(holder);
	for (int i = 0; a && i < 32; i++) {
		rx_str(peek_ptr(a + 0x30u * (unsigned)i), nm, sizeof nm);
		if (!nm[0])
			break;
		if (!strcmp(nm, name))
			return i;
	}
	return -1;
}
/* the attack the game's selection picks from `node` for orc o as an explicit target (fi_select's setup, one node,
 * the window checked as the update does); 0 = none */
static uintptr_t vb_pick(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, uintptr_t node, uintptr_t *sync,
			 const char **why)
{
	uintptr_t player = peek_ptr(pcomb + FI_PC_PLAYER);
	*sync = 0;
	if (!player)
		return *why = "no Player", 0;
	static unsigned char ctx[0x100] __attribute__((aligned(16))), tgt[FI_TGT_SIZE + 0x68] __attribute__((aligned(16)));
	memset(ctx, 0, sizeof ctx), memset(tgt, 0, sizeof tgt);
	FI_FN(FIS_CTX, fi_ctx_fn)(ctx, pc, FI_FN(FIS_WEAPON, fi_ptr1_fn)(player));
	FI_FN(FIS_CTX_PC, fi_ctxpc_fn)(pcomb, ctx);
	uintptr_t me = peek_ptr(pc + C_HOBJ);
	float xf[7], dir[3], ang = 0;
	if (me && peek(me + OBJ_POS, xf, sizeof xf)) {
		dir[0] = o->pos[0] - xf[0], dir[1] = 0, dir[2] = o->pos[2] - xf[2];
		if (FI_FN(FIS_DIRANGLE, fi_dirangle_fn)(xf, dir, &ang) & 0xff) {
			float deg = ang * FI_RD2DEG;
			memcpy(ctx + FI_CTX_ANGLE, &deg, 4);
		}
	}
	ctx[FI_CTX_NOTME] = peek_ptr(o->c + FI_C_TARGET) != me;
	FI_FN(FIS_TGT_INIT, fi_tgtinit_fn)(tgt);
	uintptr_t vt, took = 0, atk = 0;
	memcpy(&vt, tgt, 8);
	if (vt != g_fig.vt_target)
		return *why = "the target's vtable", 0;
	FI_FN(FIS_TGT_SET, fi_tgtset_fn)(tgt, FI_FN(FIS_PLAYERROOT, fi_ptr1_fn)(player), o->hobj, 0);
	memcpy(&took, tgt + FI_TGT_OBJ, 8);
	uint8_t pad = 0;
	peek(pcomb + FI_PC_PAD, &pad, 1);
	int busy = peek_ptr(pcomb + FI_PC_PENDING) != 0;
	uintptr_t n2 = took == o->hobj ? FI_FN(FIS_KBMNODE, fi_kbm_fn)(node, pad != 0) : 0;
	if (took != o->hobj)
		*why = "the target did not take the orc";
	else if (!n2)
		*why = "no node (KBMNode)";
	else if (!(FI_FN(FIS_WINDOW, fi_window_fn)(pcomb + FI_PC_WINDOW, n2, busy) & 0xff))
		*why = "the attack window refuses the node now";
	else if (!(atk = FI_FN(FIS_SELECT, fi_select_fn)(ctx, n2, 1, tgt, NULL)))
		*why = "no attack of its pools passes against this orc";
	if (atk) {
		uintptr_t anim = rx_db_rec(atk, g_fig.sch_attack, FI_ATK_ANIM);
		*sync = anim ? rx_db_rec(anim, g_fig.sch_anim, FI_ANIM_SYNC) : 0;
	}
	fi_unlink(tgt + 0x58), fi_unlink(tgt + 0x18);
	return atk;
}
static void vb_tree(uintptr_t pcomb, uintptr_t pc, const float root[3], double t)
{
	static int schemas;
	if (!schemas++) {
		vb_schema("node", g_fig.sch_node), vb_schema("predicate", VB_SCH_CHORD), vb_schema("chord", VB_SCH_INPUT);
		vb_schema("input", VB_SCH_INREC);
		vb_schema("character", g_fig.sch_ccr), vb_schema("attack", g_fig.sch_attack);
	}
	uintptr_t rootrec = pcomb ? peek_ptr(pcomb + FI_PC_ROOT) : 0, cur = pcomb ? peek_ptr(pcomb + 0xab8) : 0;
	uint64_t nodes[VB_TREE_NODES + 32];
	int type = 0, count = 0,
	    nn = rootrec ? rx_db_list(rootrec, g_fig.sch_ccr, FI_CCR_NODES, nodes, VB_TREE_NODES, &type, &count) : -1;
	if (nn < 0 || type != 0xa)
		nn = 0;
	int nroot = nn, lx = vb_field_ix(g_fig.sch_node, "Links");
	if (cur && lx >= 0) { /* the current node's links (the update's second list while an attack runs) */
		int t2 = 0, c2 = 0, nl = rx_db_list(cur, g_fig.sch_node, lx, nodes + nn, 32, &t2, &c2);
		if (nl > 0 && t2 == 0xa)
			nn += nl;
	}
	char rn[96], an[96], sn[96];
	struct cb_orc o[1];
	int no = pc && root ? cb_orcs_r(pcomb, pc, root, VB_NEAR_CM, o, 1) : 0;
	logf_("VERBS tree t=%.3f: root '%s' %d nodes (of %d), the current node '%s' its Links (field %d) %d; the nearest orc %s%s "
	      "attack '%s' state '%s'%s", t, vb_name(rootrec, rn, sizeof rn), nroot, count, vb_name(cur, sn, sizeof sn), lx,
	      nn - nroot, no ? "within 6 m" : "none within 6 m", no ? ":" : "",
	      no ? vb_name(peek_ptr(o[0].c + VB_C_ATTACK), an, sizeof an) : "-",
	      no ? vb_name(peek_ptr(o[0].c + VB_C_STATE), an + 48, 48) : "-",
	      g_vb.ok_tree ? "" : " (FINISH's selection sites did not match: no attacks)");
	/* one line per node: its name, type, the predicate's Input record and Chord (its two commands' hashes), the pick */
	for (int k = 0; k < nn; k++) {
		uintptr_t node = (uintptr_t)nodes[k], pred = rx_db_rec(node, g_fig.sch_node, 2), sy = 0;
		uintptr_t inp = pred ? rx_db_rec(pred, VB_SCH_CHORD, 0) : 0, ch = pred ? rx_db_rec(pred, VB_SCH_CHORD, 1) : 0;
		uint64_t v[2] = { 0, 0 };
		int ty = 0, cnt = 0;
		long ntype = rx_db_list(node, g_fig.sch_node, 0, v, 1, &ty, &cnt) == 1 ? (long)(uint32_t)v[0] : -1;
		uint32_t c1 = 0, c2 = 0;
		if (ch && rx_db_list(ch, VB_SCH_INPUT, 0, v, 1, &ty, &cnt) == 1)
			c1 = (uint32_t)v[0];
		if (ch && rx_db_list(ch, VB_SCH_INPUT, 1, v, 1, &ty, &cnt) == 1)
			c2 = (uint32_t)v[0];
		static uintptr_t seen_in[96];
		static int nseen;
		int dup = 0;
		for (int j = 0; j < nseen && !dup; j++)
			dup = seen_in[j] == inp;
		if (inp && !dup && nseen < 96) { /* each Input record once: its command and kind */
			seen_in[nseen++] = inp;
			vb_dump("input", inp, VB_SCH_INREC);
		}
		char nn2[96], in2[64], ch2[64], pk[256] = "";
		if (no && g_vb.ok_tree) {
			const char *why = "";
			uintptr_t atk = vb_pick(pcomb, pc, &o[0], node, &sy, &why);
			if (atk)
				snprintf(pk, sizeof pk, " -> '%s' sync '%s'", vb_name(atk, an, sizeof an), vb_name(sy, sn, sizeof sn));
			else
				snprintf(pk, sizeof pk, " -> none (%s)", why);
		}
		int k1 = vb_cmd_of(c1), k2 = vb_cmd_of(c2);
		logf_("VERBS tree %s %d '%s' type %ld input '%s' chord '%s' %s+%s%s", k < nroot ? "root" : "link",
		      k < nroot ? k : k - nroot, vb_name(node, nn2, sizeof nn2), ntype, vb_name(inp, in2, sizeof in2),
		      vb_name(ch, ch2, sizeof ch2), k1 > 0 ? in_cmd_name(k1) : c1 ? "?" : "-", k2 > 0 ? in_cmd_name(k2) : c2 ? "?" : "-",
		      pk);
	}
}

/* the stealth kill (SOMVR_VERBS=on): a blade contact that would deal a hit, on an orc the game's own selection offers its
 * silent stealth kill for (the root node SOMVR_VERBS_STEALTH_NODE, default PC_InstaKill_Sneak_Silent: unaware, from
 * behind - the game's predicates), presses its button (X, 118) for 150 ms instead of our hit: the game plays its kill on
 * that orc. 1 = the contact is ours. Needs FINISH's selection sites (ok_tree). */
static uintptr_t vb_node_named(uintptr_t pcomb, const char *want)
{
	static struct { char name[64]; uintptr_t root, node; } cache[4];
	uintptr_t rootrec = pcomb ? peek_ptr(pcomb + FI_PC_ROOT) : 0;
	int slot = 0;
	for (int i = 0; i < 4; i++) {
		if (!strcmp(cache[i].name, want)) {
			if (cache[i].root == rootrec)
				return cache[i].node;
			slot = i;
			break;
		}
		if (!cache[i].name[0]) {
			slot = i;
			break;
		}
	}
	snprintf(cache[slot].name, sizeof cache[slot].name, "%s", want);
	cache[slot].root = rootrec, cache[slot].node = 0;
	uint64_t nodes[VB_TREE_NODES];
	int type = 0, count = 0, nn = rootrec ? rx_db_list(rootrec, g_fig.sch_ccr, FI_CCR_NODES, nodes, VB_TREE_NODES, &type, &count) : -1;
	char nm[96];
	for (int k = 0; k < nn && type == 0xa && !cache[slot].node; k++)
		if (!strcmp(rx_rec_name((uintptr_t)nodes[k], nm, sizeof nm), want))
			cache[slot].node = (uintptr_t)nodes[k];
	logf_("VERBS the root node '%s' %s (%d of %d root nodes read)", want, cache[slot].node ? "found" : "NOT found", nn, count);
	return cache[slot].node;
}
static int vb_contact(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int tier, double t)
{
	if (g_vb.mode != VB_ON || tier == SC_TIER_NONE || !pcomb || !pc)
		return 0;
	if (t < g_vb.press_until && g_vb.pcmd[0] == VB_CMD_X)
		return 1; /* our press runs: the game's kill takes the orc */
	/* the orc the left hand holds: the sword's contact = X, the game's own move from the grab (PC_GrabFlurry_Into) */
	if ((g_vb.hand.phase == SOMVR_VBH_GRAB && o->c == g_vb.held_c) || (g_vb.ask_c && o->c == g_vb.ask_c)) {
		int el = g_vb.ask_c && g_vb.ask_kill == VB_ASK_GAME ? vb_choice(1) : 0;
		if (el) { /* the interrogation asks: the sword = its Eliminate choice (build 19b; build 20's blade: stl_ask_kill) */
			vb_tap(el, t), g_vb.choices++;
			logf_("VERBS t=%.3f choice: the blade on the held orc %#lx while the interrogation asks: %d '%s' tapped "
			      "(Eliminate)", t, (unsigned long)o->c, el, in_cmd_name(el));
			return 1;
		}
		if (g_vb.hand.phase != SOMVR_VBH_GRAB)
			return 1; /* the interrogation runs without its choices: no hit on its orc */
		g_vb.flurries++;
		g_vb.pcmd[0] = VB_CMD_X, g_vb.pcmd[1] = 0, g_vb.press_until = t + 0.15;
		if (g_vb.haptic > 0)
			in_haptic_event(SOMVR_HAND_R, 0.7f * g_vb.haptic, 0.08f, SOMVR_HAP_COMBAT);
		logf_("VERBS t=%.3f kill: the blade on the held orc %#lx (health %.1f): X (118) for 150 ms instead of the hit (the "
		      "game's move from the grab)", t, (unsigned long)o->c, vb_health(o->c));
		return 1;
	}
	if (!g_vb.stealth || !g_vb.ok_tree)
		return 0;
	if (t - g_vb.stealth_t < 1.0 && g_vb.stealth_c == o->c)
		return 1; /* the same orc a moment ago: the game's kill is starting */
	uintptr_t node = vb_node_named(pcomb, g_vb.stealth_node), sy = 0;
	if (!node)
		return 0;
	const char *why = "";
	uintptr_t atk = vb_pick(pcomb, pc, o, node, &sy, &why);
	char an[96], sn[96];
	g_vb.stealth_asks++;
	if (!atk) {
		if (t - g_vb.stealth_line > 2.0 && vb_log_ok())
			logf_("VERBS t=%.3f stealth: orc %#lx not offered (%s): the ordinary hit", t, (unsigned long)o->c, why);
		g_vb.stealth_line = t;
		return 0;
	}
	g_vb.stealth_kills++;
	g_vb.stealth_t = t, g_vb.stealth_c = o->c;
	g_vb.pcmd[0] = VB_CMD_X, g_vb.pcmd[1] = 0, g_vb.press_until = t + 0.15;
	if (g_vb.haptic > 0)
		in_haptic_event(SOMVR_HAND_R, 0.7f * g_vb.haptic, 0.08f, SOMVR_HAP_COMBAT);
	logf_("VERBS t=%.3f stealth: the blade on orc %#lx (health %.1f): the game offers '%s' (sync '%s'): X (118) for 150 ms "
	      "instead of the hit", t, (unsigned long)o->c, vb_health(o->c), vb_name(atk, an, sizeof an), vb_name(sy, sn, sizeof sn));
	return 1;
}

/* SOMVR_VERBS_TAP_S: the stun is B released within the Input record's MaxHeldDuration (0.2 s, 'Stun_Release_NoCancel'
 * of PC_Stun_Root, _Lowest, PC_HoG_Standing_NoTrgt_Start); a trigger's quick press is often longer (run 000241: 0.25 s
 * gave the hold's start, no stun). Once, at the first frame with the player's root record: that float raised to tap_s in
 * the game's database (memory only: the database is the game's data, never a file). */
static void vb_tap_window(uintptr_t pcomb, double t)
{
	if (g_vb.tap_done || !(g_vb.tap_s > 0.2f) || !pcomb || !peek_ptr(pcomb + FI_PC_ROOT))
		return;
	static const char *const nodes[] = { "PC_Stun_Root", "PC_Stun_Root_Lowest", "PC_HoG_Standing_NoTrgt_Start" };
	int fx = vb_field_ix(VB_SCH_INREC, "MaxHeldDuration"), n = 0;
	g_vb.tap_done = -1;
	for (size_t k = 0; k < sizeof nodes / sizeof nodes[0] && fx >= 0; k++) {
		uintptr_t node = vb_node_named(pcomb, nodes[k]), pred = node ? rx_db_rec(node, g_fig.sch_node, 2) : 0;
		uintptr_t in = pred ? rx_db_rec(pred, VB_SCH_CHORD, 0) : 0;
		unsigned char e[16];
		uint16_t cnt, vi;
		uintptr_t vals = in ? peek_ptr(in + 0x10) : 0;
		if (!in || !vals || rx_db_entry(in, rx_sch_hash(VB_SCH_INREC, fx), e))
			continue;
		memcpy(&cnt, e + 0xc, 2), memcpy(&vi, e + 0xe, 2);
		float f;
		uintptr_t at = vals + (uintptr_t)vi * 8;
		if ((e[0xa] & 0x3f) != 2 || !cnt || !peek(at, &f, sizeof f) || !(f > 0.05f && f < g_vb.tap_s))
			continue;
		*(volatile float *)at = g_vb.tap_s;
		n++, g_vb.tap_done = 1;
		char nm[96];
		logf_("VERBS t=%.3f tap: '%s' (of %s) MaxHeldDuration %.2f -> %.2f s (a quick trigger press is the stun)", t,
		      vb_name(in, nm, sizeof nm), nodes[k], (double)f, (double)g_vb.tap_s);
	}
	if (!n)
		logf_("VERBS t=%.3f tap: no stun input with a shorter MaxHeldDuration found (the game's own window)", t);
}

/* combat_frame's sweep (the game thread): the orc the hand holds (or the interrogation's) is not in PlayerCombat's
 * targeting bands while held (run 022511), so combat's contact loop never sees it: the blade swept against its capsule
 * here, a contact at a hit's speed = vb_contact (the grab's X, the choice's Eliminate); at most one a 0.5 s */
/* build 19b: the nearest the blade (5 points along it) came to the capsule's axis this frame, cm */
static float vb_blade_gap(const somvr_blade *b, const float c0[3], const float c1[3])
{
	float best = INFINITY, ab[3] = { c1[0] - c0[0], c1[1] - c0[1], c1[2] - c0[2] };
	float ll = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
	for (int k = 0; k <= 4; k++) {
		float p[3], f = k / 4.0f, u = 0;
		for (int j = 0; j < 3; j++)
			p[j] = b->base[j] + f * (b->tip[j] - b->base[j]);
		if (ll > 1e-6f)
			u = ((p[0] - c0[0]) * ab[0] + (p[1] - c0[1]) * ab[1] + (p[2] - c0[2]) * ab[2]) / ll;
		u = u < 0 ? 0 : u > 1 ? 1 : u;
		float q[3] = { c0[0] + u * ab[0], c0[1] + u * ab[1], c0[2] + u * ab[2] }, d = dist3(p, q);
		if (d < best)
			best = d;
	}
	return best;
}

/* the held orc out of combat's bands: its own sweep. Build 19b: a stealth hold (grabbed while unaware) asks the stealth
 * kill's judge first (the stab kills it: somvr_stealth_game.c; with Talion's object and PlayerCombat read here when the
 * grab's mover hides them from combat_frame); a fast swing's frames log how near the blade came */
static void vb_blade(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const somvr_blade *b, int sweep,
		     const float *root, double t)
{
	uintptr_t c = g_vb.mode == VB_ON ? (g_vb.hand.phase == SOMVR_VBH_GRAB ? g_vb.held_c : g_vb.ask_c) : 0;
	if (!c || t - g_vb.blade_t < 0.5)
		return;
	for (int i = 0; i < n; i++)
		if (o[i].c == c)
			return; /* in the bands: combat's loop has it */
	struct cb_orc oc;
	if (!cb_orc_get(c, &oc))
		return;
	float c0[3], c1[3];
	sc_contact ct;
	cb_capsule(&oc, c0, c1);
	int hit = sweep && sc_sweep_capsule(&g_cb.prev, b, g_cb.cfg.blade_r, c0, c1, g_cb.orc_r, g_cb.cfg.steps, &ct);
	int tier = sc_tier(&g_cb.cfg, &g_cb.sp);
	/* build 19b: while the game's grab holds the orc, Talion's mover is the grab's (player_objs refuses it): combat_frame
	 * has no root, no PlayerCombat, no server Combat (run 063243) and the held orc's contacts reached neither the
	 * stealth judge nor X (runs 054549, 060828). His object and PlayerCombat as they are: */
	float rb[3];
	int own = 0;
	uintptr_t pcomb0 = pcomb, pc0 = pc; /* (X keeps combat_frame's: its old rule, untouched) */
	if (!root || !pcomb) {
		uintptr_t pl = g_fr.pl, pm = 0, obj = 0;
		player_objs(pl, &pm, &obj);
		if (!root && obj && peek(obj + OBJ_POS, rb, sizeof rb) && vec_ok(rb, 1e7f))
			root = rb, own |= 1;
		if (!pcomb && pl && (pcomb = peek_ptr(pl + P_COMBAT)))
			own |= 2;
		uintptr_t p2 = !pc && pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
		if (p2 && vptr_of((void *)p2) == g_cbg.vt_combat && peek_ptr(p2 + C_PLAYER))
			pc = p2, own |= 4;
	}
	if (g_cb.sp.mean_mps > 1.5f && g_vb.blade_lines < 60) {
		g_vb.blade_lines++;
		logf_("VERBS t=%.3f the held orc %#lx (out of the bands): the blade %.1f cm from its axis (its radius %.0f), %s, tier "
		      "%s, %.1f m/s (the root %d, PlayerCombat %d, its server Combat %d; read here: %d)", t, (unsigned long)c,
		      vb_blade_gap(b, c0, c1), (double)g_cb.orc_r, !sweep ? "no sweep this frame" : hit ? "a contact" : "no contact",
		      sc_tier_name(tier), (double)g_cb.sp.mean_mps, root != NULL, pcomb != 0, pc != 0, own);
	}
	if (!hit)
		return;
	int took = stl_contact(pcomb, pc, &oc, &ct, tier, b, root, t);
	if (took == 2)
		return; /* build 21: a touch of the held orc that is no kill yet - its next frames judged too */
	if (took) { /* build 19b: the stealth hold's stab */
		g_vb.blade_t = t;
		return;
	}
	if (tier == SC_TIER_NONE)
		return;
	g_vb.blade_t = t;
	vb_contact(pcomb0, pc0, &oc, tier, t);
}

/* build 20 (SOMVR_VERBS_INTEL_BACK_S; docs/re-xr-pipeline.md 77.2): the GAIN INTEL menu - the game paused within 3 s of
 * the trigger's Interrogate while an orc is asked - left by the pad's Back once it has been up this long: the game goes
 * back to the held orc with its choices (runs 20261028-111136, -120916), the blade's Eliminate or the game's own release
 * follow; left by its continue (the user's, 10.1 s in; the Touch's X headless) the game plays its finisher. 0 = never
 * (the Touch's B still is the menu's Back) */
static void vb_intel(double t)
{
	int menu = game_paused() || g_inp.ctx == SC_CTX_MENU;
	if (!g_vb.intel_up) {
		if (!menu || !g_vb.ask_c || !(g_vb.intel_ask_t > 0) || t - g_vb.intel_ask_t > 3.0)
			return;
		g_vb.intel_up = 1, g_vb.intel_backed = 0, g_vb.intel_t0 = t, g_vb.intels++;
		if (vb_log_ok())
			logf_("VERBS t=%.3f the intel menu up (%.2f s after Interrogate)%s", t, t - g_vb.intel_ask_t,
			      g_vb.intel_back_s > 0 ? "" : " (SOMVR_VERBS_INTEL_BACK_S=0: the game's own end)");
		return;
	}
	if (!menu) {
		g_vb.intel_up = 0, g_vb.intel_ask_t = 0;
		g_vb.intel_outs += !g_vb.intel_backed;
		if (vb_log_ok())
			logf_("VERBS t=%.3f the intel menu closed after %.2f s (%s)", t, t - g_vb.intel_t0,
			      g_vb.intel_backed ? "our Back: the held orc again" : "by itself or the player");
		return;
	}
	if (g_vb.intel_back_s > 0 && !g_vb.intel_backed && t - g_vb.intel_t0 >= g_vb.intel_back_s) {
		g_vb.intel_backed = 1, g_vb.intel_back_until = t + 0.15, g_vb.intel_backs++;
		if (vb_log_ok())
			logf_("VERBS t=%.3f the intel menu up %.2f s: the pad's Back for 150 ms (SOMVR_VERBS_INTEL_BACK_S: back to the "
			      "held orc, no game finisher)", t, t - g_vb.intel_t0);
	}
}
/* in_apply (the input tick): the intel's Back as the pad's B (its menu binding) this tick */
static int vb_intel_back(void) { return g_vb.mode == VB_ON && cb_now() < g_vb.intel_back_until; }

/* combat_frame (the game thread, every H4 in the world): the test press's request, the change lines */
static void vb_frame(uintptr_t pl, const float root[3], double t)
{
	if (!g_vb.mode)
		return;
	if (!g_vb.tid)
		g_vb.tid = my_tid();
	if (g_vb.tid != my_tid())
		return;
	g_vb.frames++;
	uint32_t seq = atomic_load_explicit(&g_vb.req_seq, memory_order_acquire);
	if (seq != g_vb.req_done) {
		g_vb.req_done = seq;
		g_vb.pcmd[0] = atomic_load(&g_vb.req_cmd0), g_vb.pcmd[1] = atomic_load(&g_vb.req_cmd1);
		g_vb.press_until = t + atomic_load(&g_vb.req_ms) / 1000.0;
		g_vb.presses++;
		logf_("VERBS t=%.3f test press %d '%s'%s%s for %d ms", t, g_vb.pcmd[0], in_cmd_name(g_vb.pcmd[0]),
		      g_vb.pcmd[1] ? " + " : "", g_vb.pcmd[1] ? in_cmd_name(g_vb.pcmd[1]) : "", atomic_load(&g_vb.req_ms));
	}
	uintptr_t pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	char a[96], s[96], y[96];
	/* build 20: with no player from combat_frame (a body moment's hold away from his head) his Combat as it is - else
	 * the interrogation's end goes unseen and the hold it keeps holds on (the user's build 19b session: 30 s) */
	uintptr_t pk = pc ? pc : pl ? 0 : vb_talion_combat();
	g_vb.own_reads += pk && !pc;
	if (pk) {
		uintptr_t at = peek_ptr(pk + VB_C_ATTACK), st = peek_ptr(pk + VB_C_STATE), sy = vb_sync_rec(pk);
		if ((at != g_vb.t_atk || st != g_vb.t_state || sy != g_vb.t_sync) && vb_log_ok())
			logf_("VERBS t=%.3f Talion attack '%s' state '%s' sync '%s'%s", t, vb_name(at, a, sizeof a),
			      vb_name(st, s, sizeof s), vb_name(sy, y, sizeof y), pc ? "" : " (read through a body moment)");
		g_vb.t_atk = at, g_vb.t_state = st, g_vb.t_sync = sy;
	}
	if (pk && g_vb.mode == VB_ON)
		vb_kind(pk, t);
	if (pc && g_vb.mode == VB_ON)
		vb_tap_window(pcomb, t);
	if (g_vb.ask_c && !vb_choice(0) && !vb_choice(1) && g_vb.kind != SOMVR_VBK_ASK && t - g_vb.ask_t > 6.0) {
		if (vb_log_ok())
			logf_("VERBS t=%.3f the interrogation of orc %#lx is over (%.1f s after the pull)", t, (unsigned long)g_vb.ask_c,
			      t - g_vb.ask_t);
		g_vb.ask_c = 0;
	}
	vb_intel(t); /* build 20: SOMVR_VERBS_INTEL_BACK_S */
	if (g_vb.mode == VB_ON && g_vb.hand.phase != SOMVR_VBH_IDLE && t - g_vb.hands_t > 0.5) {
		/* no stereo frame has read the hand for 0.5 s (a window, a load): let everything go */
		somvr_vb_hand_reset(&g_vb.hand);
		g_vb.hand.t_end = t;
		g_vb.press_grab = g_vb.press_b = 0, g_vb.held_c = 0, g_vb.stale_lets++;
		logf_("VERBS t=%.3f no stereo frame read the left hand for %.2f s: the grab and B let go", t, t - g_vb.hands_t);
	}
	struct cb_orc o[VB_ORCS];
	int n = pc && root ? cb_orcs_r(pcomb, pc, root, VB_NEAR_CM, o, VB_ORCS) : 0;
	if ((g_vb.have_root = root != NULL))
		memcpy(g_vb.root, root, sizeof g_vb.root);
	if (g_vb.mode == VB_ON)
		vb_heads(o, n, pc, root);
	gc_frame(pcomb, pc, o, n, t); /* build 21, SOMVR_VERBS_CUE: the game's answer for the orc in reach (somvr_combat21_game.c) */
	struct vb_orc nw[VB_ORCS];
	for (int i = 0; i < n; i++) {
		uintptr_t c = o[i].c;
		nw[i] = (struct vb_orc){ c, peek_ptr(c + VB_C_STATE), peek_ptr(c + VB_C_ATTACK), vb_sync_rec(c) };
		int j = 0;
		while (j < g_vb.norc && g_vb.orc[j].c != c)
			j++;
		struct vb_orc *was = j < g_vb.norc ? &g_vb.orc[j] : NULL;
		if (was && was->state == nw[i].state && was->atk == nw[i].atk && was->sync == nw[i].sync)
			continue;
		if (!vb_log_ok())
			continue;
		float q[4] = { 0, 0, 0, 1 }, yaw = 0;
		if (peek(o[i].hobj + OBJ_ROT, q, sizeof q) && vec_ok(q, 2.0f))
			yaw = sv_heading(sv_qnormalize(sv_quat_of(q)));
		/* the angle between the orc's facing and the direction to Talion (180 = Talion right behind it) */
		float dx = root[0] - o[i].pos[0], dz = root[2] - o[i].pos[2];
		float off = fabsf(sv_wrap_pi(atan2f(dx, dz) - yaw)) * 57.29578f;
		logf_("VERBS t=%.3f orc %#lx %s at %.0f cm (Talion %.0f deg off its facing): attack '%s' state '%s' sync '%s' health "
		      "%.1f", t, (unsigned long)c, was ? "now" : "near", o[i].dist, off, vb_name(nw[i].atk, a, sizeof a),
		      vb_name(nw[i].state, s, sizeof s), vb_name(nw[i].sync, y, sizeof y), vb_health(c));
	}
	memcpy(g_vb.orc, nw, (size_t)n * sizeof nw[0]);
	g_vb.norc = n;
	if (atomic_exchange(&g_vb.req_status, 0))
		vb_status(t);
	if (atomic_exchange(&g_vb.req_tree, 0))
		vb_tree(pcomb, pc, root, t);
	int bcm = atomic_exchange(&g_vb.req_behind_cm, 0);
	if (bcm > 0)
		vb_behind(pcomb, pc, root, (float)bcm, t);
}

/* the trigger words (the writer thread): "verbs press CMD[+CMD] MS" (a test), "verbs status" */
static const char *vb_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (!g_vb.mode)
		return "verbs (SOMVR_VERBS is off: ignored)";
	if (!strncmp(w, "old", 3) || !strncmp(w, "new", 3)) { /* build 20's A/B in one boot (SOMVR_HANDS_TEST) */
		if (!g_vb.test)
			return "verbs old|new (SOMVR_HANDS_TEST is off: ignored)";
		int nw = w[0] == 'n';
		g_vb.own_read = nw, g_vb.ask_kill = nw ? g_vb.ask_kill_cfg : VB_ASK_GAME;
		g_vb.intel_back_s = nw ? g_vb.intel_back_cfg : 0;
		g_bm.free_s = nw ? g_bm.free_cfg : 0, g_bm.ret_cm = nw ? g_bm.ret_cfg : 0;
		return nw ? "verbs new (build 20: the own read, the grab's free, the body's return, the blade's kill, the intel's "
			    "Back as configured)" : "verbs old (build 19b: no own read, no free, no return, the game's Eliminate, "
			    "no intel Back)";
	}
	if (!strncmp(w, "status", 6)) {
		atomic_store(&g_vb.req_status, 1);
		return "verbs status";
	}
	if (!strncmp(w, "press ", 6)) {
		if (!g_vb.test)
			return "verbs press (SOMVR_HANDS_TEST is off: ignored)";
		char *e;
		long c0 = strtol(w + 6, &e, 10), c1 = 0;
		if (*e == '+')
			c1 = strtol(e + 1, &e, 10);
		long ms = strtol(e, NULL, 10);
		if (c0 <= 0 || c0 >= SC_NCMD || c1 < 0 || c1 >= SC_NCMD || ms <= 0 || ms > 10000)
			return "verbs press (CMD[+CMD] MS: ignored)";
		atomic_store(&g_vb.req_cmd0, (int)c0), atomic_store(&g_vb.req_cmd1, (int)c1), atomic_store(&g_vb.req_ms, (int)ms);
		atomic_fetch_add_explicit(&g_vb.req_seq, 1, memory_order_release);
		return "verbs press";
	}
	if (!strncmp(w, "aim ", 4)) {
		if (!g_vb.test)
			return "verbs aim (SOMVR_HANDS_TEST is off: ignored)";
		int k = !strncmp(w + 4, "head", 4) ? 1 : !strncmp(w + 4, "face", 4) ? 2 : !strncmp(w + 4, "off", 3) ? 0 : -1;
		if (k < 0)
			return "verbs aim (head|face|off: ignored)";
		atomic_store(&g_vb.aim, k);
		return k == 1 ? "verbs aim head (a test: the left palm at the nearest orc's collar)"
		       : k == 2 ? "verbs aim face (a test: the left palm 25 cm from the eyes toward the orc)" : "verbs aim off";
	}
	if (!strncmp(w, "template ", 9)) {
		if (!g_vb.test)
			return "verbs template (SOMVR_HANDS_TEST is off: ignored)";
		char nm[64] = "";
		if (sscanf(w + 9, "%63s", nm) != 1 || !strcmp(nm, "off"))
			nm[0] = 0;
		snprintf(g_pr_tmpl_test, sizeof g_pr_tmpl_test, "%s", nm); /* (a test: the practice's next spawns) */
		return nm[0] ? "verbs template (a test: the practice spawns that AI/Templates record)" : "verbs template off";
	}
	if (!strncmp(w, "tree", 4)) {
		if (!g_vb.test)
			return "verbs tree (SOMVR_HANDS_TEST is off: ignored)";
		atomic_store(&g_vb.req_tree, 1);
		return "verbs tree (a test: the root nodes, their chords and the attacks against the nearest orc)";
	}
	if (!strncmp(w, "behind ", 7) || !strncmp(w, "ahead ", 6)) {
		if (!g_vb.test)
			return "verbs behind|ahead (SOMVR_HANDS_TEST is off: ignored)";
		long cm = strtol(w + (w[0] == 'b' ? 7 : 6), NULL, 10);
		if (cm < 50 || cm > 2000)
			return "verbs behind|ahead (CM 50..2000: ignored)";
		if (w[0] == 'b') {
			atomic_store(&g_vb.req_behind_cm, (int)cm);
			return "verbs behind (a test: Talion teleported behind the nearest orc)";
		}
		g_pr.ahead_cm = (float)cm; /* (a test: the next practice spawns that far ahead) */
		return "verbs ahead (a test: the practice orcs spawn that far ahead)";
	}
	if (!strncmp(w, "words ", 6)) {
		int k = !strncmp(w + 6, "off", 3) ? 0 : !strncmp(w + 6, "test", 4) ? 2 : -1;
		if (k < 0 || (k == 2 && !g_vb.test))
			return "verbs words (off|on|test; test needs SOMVR_HANDS_TEST: ignored)";
		atomic_store(&g_vb_words_mode, k);
		return k == 2 ? "verbs words test (every prompt's text = VR + its StringId; prompts already up keep theirs)"
			      : "verbs words off";
	}
	return "verbs (status|press CMD[+CMD] MS|words off|on|test: ignored)";
}

static void vb_start(int *bad)
{
	static const char *const mode_w[] = { "off", "log", "on" };
	(void)bad;
	memset(&g_vb, 0, sizeof g_vb);
	g_vb.mode = env_word("SOMVR_VERBS", mode_w, 3, VB_OFF);
	if (!g_vb.mode)
		return;
	const char *ht = getenv("SOMVR_HANDS_TEST");
	g_vb.test = ht && ht[0] == '1';
	if (g_cb.mode == CB_OFF) {
		logf_("WARN SOMVR_VERBS=%s needs SOMVR_COMBAT (its orc reads): the verbs stay off", mode_w[g_vb.mode]);
		g_vb.mode = VB_OFF;
		return;
	}
	somvr_vb_cfg_default(&g_vb.cfg);
	somvr_vb_hand_reset(&g_vb.hand);
	g_vb.cfg.reach_cm = env_float("SOMVR_VERBS_REACH_CM", g_vb.cfg.reach_cm, 10, 60, bad);
	g_vb.cfg.cone_deg = env_float("SOMVR_VERBS_CONE_DEG", g_vb.cfg.cone_deg, 0, 45, bad);
	g_vb.cfg.ask_cm = env_float("SOMVR_VERBS_ASK_CM", g_vb.cfg.ask_cm, 15, 60, bad);
	g_vb.back_cm = env_float("SOMVR_VERBS_VIEW_BACK_CM", 35, 0, 80, bad);
	g_vb.tap_s = env_float("SOMVR_VERBS_TAP_S", 0.35f, 0, 1, bad);
	g_vb.cfg.hold_max_s = env_float("SOMVR_VERBS_HOLD_MAX_S", g_vb.cfg.hold_max_s, 1, 20, bad);
	g_vb.haptic = env_float("SOMVR_VERBS_HAPTIC", 1, 0, 1, bad);
	g_vb.ask_kill = g_vb.ask_kill_cfg = env_word("SOMVR_VERBS_ASK_KILL", (const char *const[]){ "game", "blade" }, 2,
						     VB_ASK_GAME);
	g_vb.intel_back_s = g_vb.intel_back_cfg = env_float("SOMVR_VERBS_INTEL_BACK_S", 0, 0, 60, bad);
	g_vb.own_read = 1;
	{
		char nw[512];
		g_vb.ok_head = check_site(&g_node_sites[NS_NODEXF], nw, sizeof nw) == 0;
	}
	if (g_vb.mode == VB_ON && g_inp.on == 0) {
		logf_("WARN SOMVR_VERBS=on needs SOMVR_INPUT=1 (the hands, the press): log only");
		g_vb.mode = VB_LOG;
	}
	g_vb.stealth = env_word("SOMVR_VERBS_STEALTH", (const char *const[]){ "0", "1" }, 2, 1);
	{
		const char *sn = getenv("SOMVR_VERBS_STEALTH_NODE");
		snprintf(g_vb.stealth_node, sizeof g_vb.stealth_node, "%s", sn && sn[0] ? sn : "PC_InstaKill_Sneak_Silent");
	}
	if (g_vb.test || (g_vb.mode == VB_ON && g_vb.stealth)) { /* the selection: SOMVR_FINISH's read-only sites */
		static const int need[] = { FIS_CTX, FIS_CTX_PC, FIS_WINDOW, FIS_SELECT, FIS_TGT_INIT, FIS_TGT_SET, FIS_PLAYERROOT,
					    FIS_WEAPON, FIS_DIRANGLE, FIS_KBMNODE };
		char why[512];
		g_vb.ok_tree = 1;
		for (size_t i = 0; i < sizeof need / sizeof need[0]; i++)
			if (check_site(&g_fi_sites[need[i]], why, sizeof why)) {
				g_vb.ok_tree = 0;
				logf_("VERBS site %s MISMATCH: %s", g_fi_sites[need[i]].name, why);
			}
		if (!g_vb.ok_tree && g_vb.mode == VB_ON && g_vb.stealth)
			logf_("WARN SOMVR_VERBS: a selection site did not match: no stealth kills (the grab works)");
	}
	if (g_vb.mode == VB_ON && atomic_load(&g_pp.mode) == PP_VR) { /* the grab's B shown with the left trigger's glyph */
		int n = atomic_load(&g_pp.nmap), have = 0;
		for (int i = 0; i < n; i++)
			have |= g_pp.map_from[i] == VB_CMD_B;
		if (!have && n + 1 < PP_MAP_MAX) { /* B (the grab's drain), the choice's Interrogate: the left trigger's glyph */
			g_pp.map_from[n] = VB_CMD_B, g_pp.map_to[n] = 124;
			g_pp.map_from[n + 1] = 164, g_pp.map_to[n + 1] = 124;
			atomic_store_explicit(&g_pp.nmap, n + 2, memory_order_release);
		}
	}
	if (atomic_load(&g_pp.mode) == PP_GAME)
		logf_("CONFIG verbs: SOMVR_PAD_PROMPTS=game: the game's prompts are not read (log or vr reads them)");
	logf_("CONFIG verbs build 20: the interrogated orc's kill %s (SOMVR_VERBS_ASK_KILL); Talion's attack read through a "
	      "body moment's hold; the intel menu's Back after %.1f s (SOMVR_VERBS_INTEL_BACK_S; 0 = never)", g_vb.ask_kill ==
	      VB_ASK_BLADE ? "blade: the sword's stab into it = its Eliminate (and the Touch's B)" : "game: the Touch's B = "
	      "its Eliminate (build 19b)", g_vb.intel_back_s);
	logf_("CONFIG verbs mode=%s test=%d: %s; the stealth kill %s ('%s'); reach %.0f cm (or out %.0f cm at a head within %.0f deg, %.0f cm), the longest hold %.1f s, haptics x%.2f, the orcs' heads %s",
	      mode_w[g_vb.mode], g_vb.test, g_vb.mode == VB_ON ? "the left hand's squeeze on an orc's head = B (120) held while "
	      "the grip stays closed (the game's grab, 123); the left trigger holding it or with the open hand on a head = B (120: a tap "
	      "the stun, held the drain); the held orc pulled to the face = A (121: the interrogation); the sword on the held orc = X "
	      "(118)" : "the lines only",
	      g_vb.mode == VB_ON && g_vb.stealth && g_vb.ok_tree ? "by a blade contact the game offers it for (X)" : "off",
	      g_vb.stealth_node,
	      g_vb.cfg.reach_cm, g_vb.cfg.out_cm, g_vb.cfg.cone_deg, g_vb.cfg.far_cm, g_vb.cfg.hold_max_s, g_vb.haptic, g_vb.ok_head ? "by the Head node" : "estimated (NS_NODEXF did not match)");
}

static void vb_end_log(void)
{
	if (!g_vb.mode)
		return;
	logf_("VERBS END build 20: the interrogated orc's Eliminate by the blade %llu (contacts too weak %llu) "
	      "(SOMVR_VERBS_ASK_KILL=%s); Talion's attack read through a body moment %llu frames; interrogations over by "
	      "the grab's free %llu; intel menus %llu (our Back %llu, closed otherwise %llu)", (unsigned long long)g_vb.ask_kills,
	      (unsigned long long)g_vb.ask_weak,
	      g_vb.ask_kill == VB_ASK_BLADE ? "blade" : "game", (unsigned long long)g_vb.own_reads,
	      (unsigned long long)g_vb.ask_freed, (unsigned long long)g_vb.intels, (unsigned long long)g_vb.intel_backs,
	      (unsigned long long)g_vb.intel_outs);
	logf_("VERBS END: %llu frames; the left hand: grabs %llu, near misses %llu, releases %llu, done %llu, lost %llu; B held %llu "
	      "ticks; the game: holds %llu (completed %llu, out %llu), stuns %llu; pulses %llu; heads by node %llu, estimated %llu; "
	      "squeezes that took nothing %llu; stealth: asked %llu, kills pressed %llu, squeezes by a stealth target %llu, taken by its back %llu; the grab: 123 held %llu ticks, A taps %llu, "
	      "kills pressed %llu, the interrogation's choices %llu, Touch button taps %llu, let go without stereo frames %llu; the view eased back %llu "
	      "frames (its largest step %.2f cm); "
	      "prompts shown %llu, hidden %llu, holds %llu (%d up at the end); test presses %llu (%llu ticks held); %llu lines "
	      "(%llu suppressed)", (unsigned long long)g_vb.frames, (unsigned long long)g_vb.ev[SOMVR_VBE_GRAB],
	      (unsigned long long)g_vb.ev[SOMVR_VBE_MISS], (unsigned long long)g_vb.ev[SOMVR_VBE_RELEASE],
	      (unsigned long long)g_vb.ev[SOMVR_VBE_DONE], (unsigned long long)g_vb.ev[SOMVR_VBE_LOST],
	      (unsigned long long)g_vb.b_ticks, (unsigned long long)g_vb.hog_starts, (unsigned long long)g_vb.hog_completes,
	      (unsigned long long)g_vb.hog_outs, (unsigned long long)g_vb.stuns, (unsigned long long)g_vb.buzzes,
	      (unsigned long long)g_vb.head_node, (unsigned long long)g_vb.head_est, (unsigned long long)g_vb.ev[SOMVR_VBE_NONE],
	      (unsigned long long)g_vb.stealth_asks, (unsigned long long)g_vb.stealth_kills,
	      (unsigned long long)g_vb.back_squeezes, (unsigned long long)g_vb.back_grabs, (unsigned long long)g_vb.grab_ticks,
	      (unsigned long long)g_vb.a_taps, (unsigned long long)g_vb.flurries, (unsigned long long)g_vb.choices,
	      (unsigned long long)g_vb.btn_taps,
	      (unsigned long long)g_vb.stale_lets, (unsigned long long)g_vb.view_frames, (double)g_vb.vmax_step,
	      (unsigned long long)g_vb.shows,
	      (unsigned long long)g_vb.hides, (unsigned long long)g_vb.holds, g_vb.nup, (unsigned long long)g_vb.presses,
	      (unsigned long long)g_vb.press_ticks, (unsigned long long)g_vb.lines, (unsigned long long)g_vb.suppressed);
}
