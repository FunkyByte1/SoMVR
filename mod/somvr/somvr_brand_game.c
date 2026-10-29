/* somvr_brand_game.c - build 23 (som-b23-brand; docs/re-xr-pipeline.md 97, handoff/HANDOFF-story.md "build 23"): the
 * brand by the VR hand, SOMVR_VERBS_BRAND (off | on | log; code default off, scripts/headset-build23.env on).
 *
 * The game's brands (re-xr-pipeline.md 39.1, 89; the player's root nodes, `verbs tree`): the stealth brand
 * 'PC_StealthBrand' (input 'StealthBrand': B pressed - CombatPrimary03, InputType 0, no held action - crouched behind
 * an unaware orc, once PSS_Brand is owned; the pad's "Hold [RT] + tap (B)"), the combat brand 'PC_InstaHOG' (the chord
 * 'InstaBrand_Chord' = CombatPrimary03 + CombatPrimary04, B + A: "Press (A) + (B) with a charged Hit Streak", the skill
 * PSS_Instant_Brand), and the drain from a grab ('PC_Grab_HOG_Into', B held), which brands once PSS_Brand is owned.
 *
 * The VR hand (SOMVR_VERBS, 39.2) already sends B for the left trigger with the open hand on an orc's head (a tap the
 * stun, held the wraith hold) or while it holds an orc (the drain). This adds, the game's own selection asked first (the
 * cue's way, gc_pick, every SOMVR_VERBS_CUE_RECHECK_S for the orc the hand is on or holds):
 *   - the combat brand: the trigger on an orc's head (SOMVR_VERBS_BRAND_COMBAT=any, the default; =stunned: only a
 *     stunned orc's - its attack or state '*Stun*') or on the orc the hand holds, while the game offers 'PC_InstaHOG' for
 *     it = the chord B + A for 150 ms instead of B (no B held after it). Why any: the stun throws the orc 2.7-4.7 m back,
 *     out of the hand's reach, for a short loop (boot 20261029-065745), and the game offers the brand against an orc in a
 *     fight without it (boot 20261029-062035);
 *   - the stealth brand: the trigger on the head of an orc the game offers 'PC_StealthBrand' for = B as the pad's tap
 *     (SOMVR_VERBS_BRAND_STEALTH=tap, 150 ms) or as today (=hold).
 * Every wraith press (on or log) logs what the game offered for that orc at that moment: `VERBS brand t=... press ...`.
 * Nothing moves Talion or the view by our doing: the game's brand plays under the body moments' hold.
 * Test words (SOMVR_HANDS_TEST): `verbs brand status|probe|charge|stealth tap|hold`. */

/* build 24 (som-b24-brand; re-xr-pipeline.md 98): SOMVR_VERBS_BRAND_NODELOG (off | on; a test switch, code default off,
 * never in a build's env) - the game's own combat node log in somvr.log. Combat_LogPrintf (0x2751a50) formats each line
 * only while its file sink DAT_04c459a0 is set (FUN_02751900 makes one only with a log file name: null in the shipped
 * game) and the node rules log only with the byte DAT_04c445d8 on (Init_CombatVariables: 0). With the switch on, its one
 * call of the sink's write FUN_00fbb8f0 (0x2751b73) comes to us, and for a window (`verbs brand nodelog S`, and 3 s from
 * each new stealth brand offer and each brand press) the byte is 1 and the sink is a dummy of ours: every "Node Rejected
 * 'X', <why>" / "Node Accepted 'X'" / "Root node validated: X" of the player's nodes (the predicate FUN_02271890, its
 * wrapper FUN_02272ea0, the window FUN_02264a70, the node loop FUN_00a8e120) logged once per node and again when that
 * node's line changes, with how often the line before came. The byte and the sink go back to 0 when the window ends.
 * Nothing the game decides reads the byte (every reader only logs; FUN_02271890 then answers 1 for its own non-zero). */
/* BRAND_SITES_BEGIN (check_sites.py counts this block on its own: SOMVR_VERBS_BRAND_NODELOG's patched K_CALL) */
static const struct site_spec g_br_sites_game[] = {
SITE("BR_NODELOG", K_CALL, 0x2751b73, 0x2751b62, "48 8b 3d 37 3e 4f 02 48 85 ff 74 0a be 80 51 c4 04 e8 78 9d 86 fe", 0xfbb8f0, "48 83 3f 00 0f 84 8e 00 00 00 53 48 89 fb 48 83"),
SITE("BR_SELECT", K_CALL, 0xa8eb08, 0xa8eb01, "48 8d bd 30 ff ff ff e8 03 31 7d 01", 0x2261c10, "41 57 49 89 ff 41 56 41 55 4d 89 c5 41 54 55 53"),
SITE("BR_GATE_A", K_CALL, 0xa8eba2, 0xa8eb94, "48 8b b8 08 03 00 00 4c 89 9d a0 fe ff ff e8 19 5a fe ff", 0xa745c0, "41 57 45 31 ff 41 56 41 55 41 54 55 53 48 81 ec"),
SITE("BR_GATE_B", K_CALL, 0xa8ebd9, 0xa8ebcb, "48 8b b8 08 03 00 00 48 8b b7 40 02 00 00 e8 b2 65 fe ff", 0xa75190, "48 85 f6 74 23 48 83 ec 10 4c 89 c8 4d 89 c1 49"),
SITE("BR_START", K_CALL, 0xa8eb6f, 0xa8eb63, "4c 89 d9 4c 89 d2 4c 89 e6 48 89 df e8 8c db ff ff", 0xa8c700, "41 57 41 56 41 55 41 54 55 53 48 81 ec 18 01 00"),
};
/* BRAND_SITES_END */
static const struct site_spec *g_br_sites = g_br_sites_game; /* selftest: stand-ins */
#define BR_NL_FLAG 0x4c445d8u  /* the node log's byte */
#define BR_NL_SINK 0x4c459a0u  /* Combat_LogPrintf's sink (a FILE * holder; null) */
#define BR_NL_NODES 192
#define BR_NL_MAXLINES 600
typedef void (*br_sink_write_fn)(uintptr_t sink, const char *text);
static struct {
	int want, ok;                /* the switch; the call patched */
	uintptr_t flag_a, sink_a;    /* the byte and the sink (selftest: its own) */
	uintptr_t *dummy;            /* our sink (calloc: the game's shutdown FUN_02751960 frees a sink it finds) */
	int on;                      /* the byte and our sink set now */
	double until;                /* the window (the game thread) */
	_Atomic int busy;            /* the line table (Combat_LogPrintf may run off the game thread) */
	char filter[32];             /* only lines naming this (default "PC_": the player's nodes) */
	struct { char key[64], text[200]; double t0; uint32_t n; } node[BR_NL_NODES];
	int nnodes;
	uint64_t lines, kept, logged, dropped, busy_drops, windows, frame0;
	double t_open;
} g_brnl;

/* the line's node: the name in its first quotes, or after "validated: " (the node loop's) */
static void br_nl_key(const char *s, char *key, size_t cap)
{
	const char *q = strchr(s, '\''), *v = strstr(s, "validated: ");
	size_t n = 0;
	if (v) {
		v += 11;
		while (v[n] && v[n] != ' ' && v[n] != '\n' && n + 1 < cap)
			n++;
		memcpy(key, v, n);
	} else if (q) {
		const char *e = strchr(q + 1, '\'');
		n = e ? (size_t)(e - q - 1) : 0;
		n = n + 1 < cap ? n : cap - 1;
		memcpy(key, q + 1, n);
	}
	key[n] = 0;
}

/* one formatted line of Combat_LogPrintf ("%.3lf [COMBAT|NODE]: TEXT\n"): logged when the node's line is new or changed
 * (returns 1 then); the game's clock and the prefix cut */
static int br_nl_line(const char *text, double t)
{
	g_brnl.lines++;
	if (!text)
		return 0;
	const char *s = strstr(text, "]: ");
	s = s ? s + 3 : text;
	if (g_brnl.filter[0] && !strstr(s, g_brnl.filter))
		return 0;
	g_brnl.kept++;
	char key[64], line[200];
	size_t n = strcspn(s, "\r\n");
	n = n < sizeof line - 1 ? n : sizeof line - 1;
	memcpy(line, s, n), line[n] = 0;
	br_nl_key(line, key, sizeof key);
	int i = 0;
	while (i < g_brnl.nnodes && strcmp(g_brnl.node[i].key, key))
		i++;
	if (i < g_brnl.nnodes && !strcmp(g_brnl.node[i].text, line)) {
		g_brnl.node[i].n++;
		return 0;
	}
	if (g_brnl.logged >= BR_NL_MAXLINES) {
		g_brnl.dropped++;
		return 0;
	}
	g_brnl.logged++;
	if (i < g_brnl.nnodes) {
		logf_("VERBS brand node t=%.3f f+%llu %s (before: '%s' %u time(s) from t=%.3f)", t, (unsigned long long)(g_fr.frame -
		      g_brnl.frame0), line, g_brnl.node[i].text, g_brnl.node[i].n, g_brnl.node[i].t0);
	} else {
		logf_("VERBS brand node t=%.3f f+%llu %s", t, (unsigned long long)(g_fr.frame - g_brnl.frame0), line);
		if (g_brnl.nnodes >= BR_NL_NODES)
			return 1; /* not followed (the table is full) */
		i = g_brnl.nnodes++;
		snprintf(g_brnl.node[i].key, sizeof g_brnl.node[i].key, "%s", key);
	}
	snprintf(g_brnl.node[i].text, sizeof g_brnl.node[i].text, "%s", line);
	g_brnl.node[i].t0 = t, g_brnl.node[i].n = 1;
	return 1;
}

/* BR_NODELOG: Combat_LogPrintf's write to its sink (the thread that logs) */
__attribute__((used)) void somvr_w_brnodelog(uintptr_t sink, const char *text)
{
	if (!g_brnl.dummy || sink != (uintptr_t)g_brnl.dummy) {
		((br_sink_write_fn)g_br_sites[0].target)(sink, text); /* the game's own log file (a name set): as before */
		return;
	}
	int z = 0;
	if (!atomic_compare_exchange_strong(&g_brnl.busy, &z, 1)) {
		g_brnl.busy_drops++;
		return;
	}
	br_nl_line(text, cb_now());
	atomic_store(&g_brnl.busy, 0);
}

/* BR_SELECT, BR_GATE_A, BR_GATE_B, BR_START (with the node log; the player's root node loop FUN_00a8e120, the game
 * thread): a validated root node's pick (SharedCombat_SelectAttackFromPool), its target's two gates (FUN_00a745c0(...,
 * 2, target, 0, attack), FUN_00a75190(..., 1, target, 0, attack): either 1 = the loop breaks, nothing starts) and the
 * start FUN_00a8c700 - logged while the window is open; the game's answers passed on unchanged */
typedef uintptr_t (*br_pass6_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static struct { uintptr_t node, atk; } g_brsel;
static void br_gate_line(const char *what, uintptr_t node, uintptr_t atk, uintptr_t target, long ans)
{
	char nn[96], an[96];
	if (!g_brnl.on || g_brnl.logged >= BR_NL_MAXLINES)
		return;
	g_brnl.logged++;
	logf_("VERBS brand gate t=%.3f f+%llu root '%s': %s -> %ld (the attack '%s', the target %#lx)", cb_now(),
	      (unsigned long long)(g_fr.frame - g_brnl.frame0), vb_name(node, nn, sizeof nn), what, ans, vb_name(atk, an, sizeof an),
	      (unsigned long)target);
}
__attribute__((used)) uintptr_t somvr_w_brsel(uintptr_t a, uintptr_t node, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f)
{
	uintptr_t r = ((br_pass6_fn)g_br_sites[1].target)(a, node, c, d, e, f);
	g_brsel.node = node, g_brsel.atk = r;
	if (r)
		br_gate_line("the pick", node, r, 0, 1);
	return r;
}
__attribute__((used)) uint8_t somvr_w_brgatea(uintptr_t a, uintptr_t b, uintptr_t target, uintptr_t d, uintptr_t atk, uintptr_t f)
{
	uint8_t r = (uint8_t)((br_pass6_fn)g_br_sites[2].target)(a, b, target, d, atk, f);
	br_gate_line("gate A FUN_00a745c0 (1 = refused)", g_brsel.node, atk, target, r);
	return r;
}
__attribute__((used)) uint8_t somvr_w_brgateb(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t target, uintptr_t e, uintptr_t atk)
{
	uint8_t r = (uint8_t)((br_pass6_fn)g_br_sites[3].target)(a, b, c, target, e, atk);
	br_gate_line("gate B FUN_00a75190 (1 = refused)", g_brsel.node, atk, target, r);
	return r;
}
__attribute__((used)) void somvr_w_brstart(uintptr_t pc, uintptr_t node, uintptr_t atk, uintptr_t target, uintptr_t e, uintptr_t f)
{
	br_gate_line("started (FUN_00a8c700)", node, atk, target, 1);
	((br_pass6_fn)g_br_sites[4].target)(pc, node, atk, target, e, f);
}

/* the game thread (br_frame): the byte and our sink set while the window is open, put back after */
static void br_nl_frame(double t)
{
	if (!g_brnl.ok)
		return;
	int want = t < g_brnl.until;
	if (want == g_brnl.on)
		return;
	uint8_t *flag = (uint8_t *)g_brnl.flag_a;
	uintptr_t *sink = (uintptr_t *)g_brnl.sink_a;
	if (want) {
		if (*sink && *sink != (uintptr_t)g_brnl.dummy) { /* the game's own sink (a log file named): leave it alone */
			logf_("VERBS brand node log t=%.3f: the game's sink is set (%#lx): not opened", t, (unsigned long)*sink);
			g_brnl.until = 0;
			return;
		}
		g_brnl.nnodes = 0, g_brnl.logged = 0, g_brnl.frame0 = g_fr.frame, g_brnl.t_open = t, g_brnl.windows++;
		*sink = (uintptr_t)g_brnl.dummy, *flag = 1, g_brnl.on = 1;
		logf_("VERBS brand node log t=%.3f: open for %.1f s (the game's combat node log, lines naming '%s')", t,
		      g_brnl.until - t, g_brnl.filter);
		return;
	}
	*flag = 0, *sink = 0, g_brnl.on = 0;
	int z = 0;
	while (!atomic_compare_exchange_weak(&g_brnl.busy, &z, 1))
		z = 0;
	char runs[1400];
	int k = 0;
	runs[0] = 0;
	for (int i = 0; i < g_brnl.nnodes && k < (int)sizeof runs - 1; i++)
		if (strstr(g_brnl.node[i].key, "Brand") || strstr(g_brnl.node[i].key, "Torment"))
			k += snprintf(runs + k, sizeof runs - (size_t)k, " '%s' %u x since t=%.3f;", g_brnl.node[i].text,
				      g_brnl.node[i].n, g_brnl.node[i].t0);
	logf_("VERBS brand node log t=%.3f: closed after %.1f s, %llu frames: %llu lines formatted, %llu naming '%s', %llu "
	      "logged, %llu not (the cap), %llu busy; the brand nodes' last lines:%s", t, t - g_brnl.t_open,
	      (unsigned long long)(g_fr.frame - g_brnl.frame0), (unsigned long long)g_brnl.lines,
	      (unsigned long long)g_brnl.kept, g_brnl.filter, (unsigned long long)g_brnl.logged,
	      (unsigned long long)g_brnl.dropped, (unsigned long long)g_brnl.busy_drops, runs[0] ? runs : " none");
	g_brnl.lines = g_brnl.kept = g_brnl.dropped = g_brnl.busy_drops = 0;
	atomic_store(&g_brnl.busy, 0);
}

/* a window of s seconds from t (the game thread or the word's; only with the call patched) */
static void br_nl_open(double t, double s)
{
	if (g_brnl.ok && t + s > g_brnl.until)
		g_brnl.until = t + s;
}

static void br_nl_start(const struct site_spec *sites, uintptr_t flag_a, uintptr_t sink_a, int *bad)
{
	memset(&g_brnl, 0, sizeof g_brnl);
	g_br_sites = sites;
	static const char *const ow[] = { "off", "on" };
	g_brnl.want = env_word("SOMVR_VERBS_BRAND_NODELOG", ow, 2, 0);
	const char *f = getenv("SOMVR_VERBS_BRAND_NODELOG_FILTER");
	snprintf(g_brnl.filter, sizeof g_brnl.filter, "%s", f ? f : "PC_");
	if (!g_brnl.want)
		return;
	uint8_t fl = 0xff;
	uintptr_t sk = 1;
	if (!peek(flag_a, &fl, 1) || !peek(sink_a, &sk, sizeof sk) || fl > 1 || sk) {
		logf_("WARN SOMVR_VERBS_BRAND_NODELOG=on: the node log's byte (%#x) or sink (%#lx) is not the shipped game's 0: off",
		      fl, (unsigned long)sk);
		*bad = 1;
		return;
	}
	g_brnl.flag_a = flag_a, g_brnl.sink_a = sink_a;
	g_brnl.dummy = calloc(2, sizeof *g_brnl.dummy); /* [0] = 0: the game's own write and close would do nothing with it */
	g_brnl.ok = g_brnl.dummy && bo_patch_call(&sites[0], (void *)somvr_w_brnodelog);
	int gates = g_brnl.ok && bo_patch_call(&sites[1], (void *)somvr_w_brsel);
	gates = gates && bo_patch_call(&sites[2], (void *)somvr_w_brgatea);
	gates = gates && bo_patch_call(&sites[3], (void *)somvr_w_brgateb);
	gates = gates && bo_patch_call(&sites[4], (void *)somvr_w_brstart); /* (a partial set logs what it has) */
	if (!g_brnl.ok || !gates)
		*bad = 1;
	logf_("CONFIG verbs brand node log=%s (SOMVR_VERBS_BRAND_NODELOG, a test switch): the game's combat node log into "
	      "somvr.log (lines naming '%s', SOMVR_VERBS_BRAND_NODELOG_FILTER) for 3 s from each new stealth brand offer and each "
	      "brand press, and `verbs brand nodelog S`; the root loop's pick, its two target gates and the start %s", g_brnl.ok ? "on" :
	      "NOT hooked", g_brnl.filter, gates ? "logged too" : "NOT all hooked");
}

#define BR_C_BLOCKING 0xdc /* Combat: the current attack is a blocking one (AttackData.IsBlock; re-combat.md 3.2) */
/* PlayerCombat's input tracker (PlayerCombat+0x80: FUN_00a8e120 passes it to the predicate test FUN_02272ea0 ->
 * FUN_02271890): +0x08/+0x10 its tracked commands (12-byte entries {u32 a, u32 b, u16 cmd}; b < a = held), +0x90 the
 * blocking command - an Input record with IgnoreIfBlocking=1 is rejected ("Node Rejected '%s', IgnoreIfBlocking")
 * while that command's entry has b < a. Read only. */
#define BR_PC_TRACK 0x80
#define BR_TR_VEC 0x08
#define BR_TR_BLOCK 0x90
static const char *const g_br_w[] = { "off", "on", "log" };
enum { BR_OFF = 0, BR_ON = 1, BR_LOG = 2 };
enum { BRK_NONE = 0, BRK_STEALTH, BRK_COMBAT };
static const char *const g_brk_w[] = { "none", "the stealth brand", "the combat brand" };
static struct {
	int mode;              /* BR_* */
	int stealth_tap;       /* SOMVR_VERBS_BRAND_STEALTH: 1 tap (150 ms), 0 hold (as the wraith press) */
	int combat_any;        /* SOMVR_VERBS_BRAND_COMBAT: 1 any orc the game offers it for (default), 0 a stunned or held one */
	float press_s;         /* the chord's / the tap's length (0.15 s) */
	float fresh_s;         /* the game's answer this young counts at the press (0.3 s) */
	/* vb_hands -> gc_frame: the orc the open hand is on (or holds) this frame */
	uintptr_t reach_c;
	int reach_held;
	uint64_t reach_frame;
	/* gc_frame: the game's answer for it */
	uintptr_t ask_c;
	int ask_kind;          /* BRK_* */
	double ask_t;
	char ask_atk[96];
	const char *ask_why;
	/* the press's line (gc_frame logs the game's picks for that orc then) */
	_Atomic uintptr_t req_c;
	_Atomic int req_probe, req_charge;
	uintptr_t said_c;
	int said_kind;
	/* the game's blocking command (its tracker, read each frame) and our let-go of it for a brand's press */
	int unblock;           /* SOMVR_VERBS_BRAND_UNBLOCK (on): a command of ours the game counts as blocking, let go */
	uint32_t block_cmd;
	int block_held;
	double block_t;
	_Atomic int unblock_cmd;
	double unblock_until;
	uint64_t unblocks;
	/* a test aid (`verbs brand auto on`, SOMVR_HANDS_TEST): the left trigger pressed for 0.25 s in the frame the game's
	 * stealth brand prompt comes up (no test-input latency); the press's aftermath logged for 1 s */
	int autop;             /* 1 at the stealth brand's prompt; 2 (`auto combat`) at the combat brand's offer, Talion free */
	int talion_recoil;     /* gc_frame: Talion's attack is a recoil now (PC_Recoil_*: the game's window shut) */
	double auto_until, after_until, after_next;
	uintptr_t after_c;
	uint64_t asks, stealth_yes, combat_yes, chords, taps, presses, holds, lines;
	/* build 24 (re-xr-pipeline.md 98): the game's own "is B enabled now" - FUN_02271890's first test, the vtable slot 3 of
	 * the input mode object at *(PlayerCombat+0x80) (FUN_0247e570 names the modes: Playing, PausingTutorial, Showdown,
	 * Cinematic, ..., DisableCombat). From the Sea Fort's video until "Ability Unlocked: Brand" is closed every combat
	 * command is off ("Node Rejected 'PC_StealthBrand', Command 'CombatPrimary03' is not enabled.", boots
	 * 20261029-112050, -115309): every refused stealth brand press of builds 22-23 fell in it */
	int b_on;              /* 1 on, 0 off, -1 not read */
	double b_off_t;        /* since when off */
	uintptr_t text_lo, text_hi; /* the game's code (the slot's function must be in it) */
	uint64_t lock_presses, locks;
} g_br;

static void br_start(int *bad)
{
	memset(&g_br, 0, sizeof g_br);
	g_br.mode = env_word("SOMVR_VERBS_BRAND", g_br_w, 3, BR_OFF);
	static const char *const sw[] = { "hold", "tap" };
	g_br.stealth_tap = env_word("SOMVR_VERBS_BRAND_STEALTH", sw, 2, 1);
	g_br.press_s = 0.15f, g_br.fresh_s = 0.3f;
	g_br.b_on = -1, g_br.text_lo = 0x400000, g_br.text_hi = 0x3400000;
	static const char *const ow[] = { "off", "on" };
	g_br.unblock = env_word("SOMVR_VERBS_BRAND_UNBLOCK", ow, 2, 0);
	static const char *const cw[] = { "stunned", "any" };
	g_br.combat_any = env_word("SOMVR_VERBS_BRAND_COMBAT", cw, 2, 1);
	if (!g_br.mode) {
		memset(&g_brnl, 0, sizeof g_brnl);
		return;
	}
	if (g_vb.mode != VB_ON || !g_vb.ok_tree) {
		logf_("CONFIG verbs_brand=%s: %s - off", g_br_w[g_br.mode], g_vb.mode != VB_ON ? "SOMVR_VERBS is not on"
		      : "FINISH's selection sites did not match (no game's answer)");
		g_br.mode = BR_OFF;
		return;
	}
	br_nl_start(g_br_sites_game, BR_NL_FLAG, BR_NL_SINK, bad); /* build 24: SOMVR_VERBS_BRAND_NODELOG (tests) */
	logf_("CONFIG verbs_brand=%s (SOMVR_VERBS_BRAND): the left trigger on %s or on the orc the left hand holds, while the "
	      "game's selection offers 'PC_InstaHOG' for it (a charged hit streak, PSS_Instant_Brand) = the combat brand's chord "
	      "B + A (120 + 121) for %.0f ms instead of B (SOMVR_VERBS_BRAND_COMBAT=%s); on an orc it offers 'PC_StealthBrand' for "
	      "= B %s; asked every %.2f s for the orc the hand is on (the answer counts %.2f s); each wraith press logs the "
	      "game's offer; our blocking command let go %s (SOMVR_VERBS_BRAND_UNBLOCK)", g_br_w[g_br.mode],
	      g_br.combat_any ? "an orc's head" : "a stunned orc's head", g_br.press_s * 1e3, g_br.combat_any ? "any" : "stunned",
	      g_br.stealth_tap ? "as a tap (SOMVR_VERBS_BRAND_STEALTH=tap)" : "held as the trigger is (=hold)", g_gc.recheck_s,
	      g_br.fresh_s, g_br.unblock ? "on" : "off");
}

/* a command's state in what we hold (in_apply's records): "held N ticks" / "not held" */
static const char *br_cmd_state(int cmd, char *out, size_t cap)
{
	for (int i = 0; i < g_inp.niv; i++)
		if (g_inp.iv[i].cmd == cmd) {
			snprintf(out, cap, "%s %llu ticks%s", g_inp.iv[i].on ? "held" : "off", (unsigned long long)(g_inp.ticks -
				 g_inp.iv[i].since), g_inp.iv[i].deny ? " (denied)" : "");
			return out;
		}
	snprintf(out, cap, "not held");
	return out;
}

/* the tracker's entry for cmd: "a/b" (b < a = held), "untracked" */
static const char *br_track(uintptr_t pcomb, uint32_t cmd, char *out, size_t cap)
{
	uintptr_t v0 = pcomb ? peek_ptr(pcomb + BR_PC_TRACK + BR_TR_VEC) : 0, v1 = pcomb ? peek_ptr(pcomb + BR_PC_TRACK + BR_TR_VEC + 8) : 0;
	snprintf(out, cap, "untracked");
	for (uintptr_t e = v0; v0 && v1 > v0 && v1 - v0 < 12 * 512 && e + 12 <= v1; e += 12) {
		uint32_t w[3];
		if (!peek(e, w, sizeof w))
			break;
		if ((w[2] & 0xffff) == cmd) {
			snprintf(out, cap, "%u/%u%s", w[0], w[1], w[1] < w[0] ? " held" : "");
			break;
		}
	}
	return out;
}

/* build 24: where Talion is for an orc - its distance, Talion's angle off its facing (180 = right behind it), the height */
static const char *br_geom(const struct cb_orc *o, char *out, size_t cap)
{
	float q[4] = { 0, 0, 0, 1 }, yaw = 0;
	if (!g_vb.have_root) {
		snprintf(out, cap, "%.0f cm", o->dist);
		return out;
	}
	if (peek(o->hobj + OBJ_ROT, q, sizeof q) && vec_ok(q, 2.0f))
		yaw = sv_heading(sv_qnormalize(sv_quat_of(q)));
	float dx = g_vb.root[0] - o->pos[0], dz = g_vb.root[2] - o->pos[2];
	snprintf(out, cap, "%.0f cm, Talion %.0f deg off its facing, %+.0f cm above it", o->dist,
		 fabsf(sv_wrap_pi(atan2f(dx, dz) - yaw)) * 57.29578f, g_vb.root[1] - o->pos[1]);
	return out;
}

/* the orc stunned: its attack or state named '*Stun*' (AI_Bip_Recoil_F_ToYStun_STR, AI_Bip_StunLoop, Quick_Stunned) */
static int br_stunned(uintptr_t c)
{
	char a[96], s[96];
	vb_name(peek_ptr(c + VB_C_ATTACK), a, sizeof a), vb_name(peek_ptr(c + VB_C_STATE), s, sizeof s);
	return strstr(a, "Stun") || strstr(s, "Stun");
}

/* vb_hands, before the step: the orc the open, idle left hand is on (the grab's rule) or the one it holds */
static void br_hands(const somvr_vb_hand_in *in)
{
	if (g_br.mode == BR_OFF)
		return;
	uintptr_t c = 0;
	int held = 0;
	if (g_vb.hand.phase == SOMVR_VBH_GRAB && g_vb.held_c)
		c = g_vb.held_c, held = 1;
	else if (g_vb.hand.phase == SOMVR_VBH_IDLE) {
		int keep = -1, bb = 0;
		for (int k = 0; k < in->norc && k < g_vb.nheads; k++)
			if (g_br.reach_c && g_vb.head_c[k] == g_br.reach_c)
				keep = k;
		int i = somvr_vb_reach(&g_vb.cfg, &g_vb.hand, in, keep, 8, 5, &bb);
		c = i >= 0 && i < g_vb.nheads ? g_vb.head_c[i] : 0;
	}
	g_br.reach_c = c, g_br.reach_held = held, g_br.reach_frame = g_fr.frame;
}

/* vb_hands, a wraith press (the trigger's new press, the open hand on orc c or holding it): 1 = the brand's press is
 * set (B held is not) */
static int br_press(uintptr_t c, double t)
{
	if (g_br.mode == BR_OFF || !c)
		return 0;
	g_br.presses++;
	atomic_store(&g_br.req_c, c); /* the game's offer at this moment, logged by gc_frame */
	br_nl_open(t, 3.0);           /* build 24: the game's node log around the press (SOMVR_VERBS_BRAND_NODELOG) */
	if (g_vb.test)
		g_br.after_c = c, g_br.after_until = t + 1.0, g_br.after_next = t + 0.05; /* the aftermath (tests) */
	/* the combat brand's chord only on an answer of the last 0.15 s: its A alone (the game's chord refused, a recoil come
	 * in between) would be the game's own A - so the window the selection saw must still be the one at the press */
	int fresh = g_br.ask_c == c && t - g_br.ask_t <= (g_br.ask_kind == BRK_COMBAT ? 0.15 : g_br.fresh_s);
	int k = fresh ? g_br.ask_kind : BRK_NONE;
	if (g_br.b_on == 0) { /* build 24: the game's own lock (its input mode): the press does nothing */
		g_br.lock_presses++;
		logf_("VERBS brand t=%.3f press: orc %#lx - the game has its combat commands off now (B 120 not enabled for %.1f s: "
		      "its input mode - a mission's scripted intro, a video, a menu): the game takes no press until it is back", t,
		      (unsigned long)c, t - g_br.b_off_t);
	}
	if (k == BRK_COMBAT && g_br.talion_recoil) { /* hit meanwhile: the game takes no attack now (boot 20261029-072843) */
		logf_("VERBS brand t=%.3f press: orc %#lx - the combat brand offered %.2f s ago, but Talion is in a recoil now: B as "
		      "before (no chord)", t, (unsigned long)c, t - g_br.ask_t);
		k = BRK_NONE;
	}
	if (g_br.mode != BR_ON || k == BRK_NONE || (k == BRK_STEALTH && !g_br.stealth_tap)) {
		if (k == BRK_STEALTH)
			g_br.holds++;
		return 0;
	}
	/* the game rejects a B input with IgnoreIfBlocking=1 (the brands', the stun's) while its blocking command is held: if
	 * that is one of ours (a command-level hold: the crouch's 125, ...), it is let go for the press */
	uint32_t bc = g_br.block_cmd;
	int ours = 0;
	for (int i = 0; i < g_inp.niv && g_br.unblock && g_br.block_held && t - g_br.block_t < 0.25 && bc > 0 && bc < SC_NCMD; i++)
		ours |= g_inp.iv[i].cmd == (int)bc && g_inp.iv[i].on;
	if (ours) {
		atomic_store(&g_br.unblock_cmd, (int)bc), g_br.unblock_until = t + g_br.press_s + 0.05, g_br.unblocks++;
		logf_("VERBS brand t=%.3f press: the game's blocking command %u '%s' is held by us: let go for %.0f ms", t, bc,
		      in_cmd_name((int)bc), (g_br.press_s + 0.05) * 1e3);
	}
	g_vb.pcmd[0] = VB_CMD_B, g_vb.pcmd[1] = k == BRK_COMBAT ? VB_CMD_A : 0, g_vb.press_until = t + g_br.press_s;
	g_vb.hand.b = 0, g_vb.hand.b_done = 1; /* no B held from this press: the trigger counts from its next press */
	g_vb.press_b = 0;
	k == BRK_COMBAT ? g_br.chords++ : g_br.taps++;
	if (g_vb.haptic > 0)
		in_haptic_event(SOMVR_HAND_L, 0.6f * g_vb.haptic, 0.08f, SOMVR_HAP_COMBAT);
	logf_("VERBS brand t=%.3f press: orc %#lx - the game offers '%s' (%s, asked %.2f s ago): %s for %.0f ms instead of B "
	      "held", t, (unsigned long)c, g_br.ask_atk, g_brk_w[k], t - g_br.ask_t, k == BRK_COMBAT ?
	      "the chord B + A (120 + 121)" : "B (120) tapped", g_br.press_s * 1e3);
	return 1;
}

/* build 24: the game's "is B enabled" each frame (the game thread, as FUN_02271890 asks it); a line on each change */
typedef uint8_t (*br_cmd_on_fn)(uintptr_t mode, uint32_t cmd);
static void br_b_on_frame(uintptr_t pcomb, double t)
{
	uintptr_t mo = pcomb ? peek_ptr(pcomb + BR_PC_TRACK) : 0, vt = mo ? peek_ptr(mo) : 0, fn = vt ? peek_ptr(vt + 0x18) : 0;
	int on = fn >= g_br.text_lo && fn < g_br.text_hi ? ((br_cmd_on_fn)fn)(mo, VB_CMD_B) != 0 : -1;
	if (on == g_br.b_on)
		return;
	if (on == 0)
		g_br.b_off_t = t, g_br.locks++;
	if (g_br.b_on >= 0 || on == 0)
		logf_("VERBS brand t=%.3f the game's combat commands %s (B 120 %s by the input mode %#lx's slot 3 %#lx)%s", t,
		      on == 1 ? "on again" : on == 0 ? "OFF" : "not readable", on == 1 ? "enabled" : on == 0 ? "not enabled" : "-",
		      (unsigned long)mo, (unsigned long)fn, on == 1 && g_br.b_on == 0 ? "" : on == 0 ?
		      ": the game takes no attack press now (its input mode: a mission's scripted intro, a video, a menu)" : "");
	if (on == 1 && g_br.b_on == 0)
		logf_("VERBS brand t=%.3f ... off for %.1f s", t, t - g_br.b_off_t);
	g_br.b_on = on;
}

/* gc_frame (the game thread): the game's answer for the orc the hand is on; the press's line; the test words */
static void br_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, double t)
{
	if (g_br.mode == BR_OFF)
		return;
	char a[96], s[96], an[96], sn[96], cs[48], bs[48], as[48];
	const char *why;
	br_nl_frame(t); /* build 24: SOMVR_VERBS_BRAND_NODELOG's window */
	br_b_on_frame(pcomb, t); /* build 24: the game's combat commands on or off */
	if (atomic_exchange(&g_br.req_charge, 0) && g_xc.ok_hc) { /* a test aid: Talion's hit counter charged (XC_CHARGE) */
		uintptr_t hc = peek_ptr(pc + FI_C_HITCTR);
		int16_t need = 0;
		if (hc && peek(hc + XC_HC_NEED, &need, sizeof need) && need > 0 && need < 100) {
			XC_FN(XCS_CHARGE, xc_charge_fn)(hc, need);
			logf_("VERBS brand t=%.3f Talion's hit counter charged to %d (a test aid: `verbs brand charge`)", t, need);
		}
	}
	uintptr_t pc_ = atomic_exchange(&g_br.req_c, 0), probe = atomic_exchange(&g_br.req_probe, 0);
	for (int i = 0; i < n && (pc_ || probe); i++) {
		if (!probe && o[i].c != pc_)
			continue;
		static const char *const nodes[] = { "PC_StealthBrand", "PC_StealthTorment", "PC_InstaHOG", "PC_HoG_Standing_Start",
						     "PC_Stun_Root", "PC_Grab_HOG_Into", "PC_Grab_Attempt_Stealth" };
		char line[1900], ta[96], ts[96], tb[40], t120[40], t125[40];
		uint8_t blk = 0;
		uint32_t bcmd = 0;
		peek(pc + BR_C_BLOCKING, &blk, 1), peek(pcomb + BR_PC_TRACK + BR_TR_BLOCK, &bcmd, sizeof bcmd);
		int k0 = snprintf(line, sizeof line, "VERBS brand t=%.3f %s: orc %#lx at %.0f cm (attack '%s' state '%s', health "
				  "%.1f; Talion's attack '%s' state '%s' blocking %d, pending %d; the crouch 125 %s, B 120 %s, A 121 "
				  "%s, the hand %s; the game's tracker: the blocking command %u '%s' %s, 120 %s, 125 %s):", t,
				  probe ? "probe" : "the press", (unsigned long)o[i].c, o[i].dist,
				  vb_name(peek_ptr(o[i].c + VB_C_ATTACK), an, sizeof an),
				  vb_name(peek_ptr(o[i].c + VB_C_STATE), sn, sizeof sn), vb_health(o[i].c),
				  vb_name(peek_ptr(pc + VB_C_ATTACK), ta, sizeof ta), vb_name(peek_ptr(pc + VB_C_STATE), ts, sizeof ts),
				  blk, peek_ptr(pcomb + FI_PC_PENDING) != 0, br_cmd_state(125, cs, sizeof cs),
				  br_cmd_state(VB_CMD_B, bs, sizeof bs), br_cmd_state(VB_CMD_A, as, sizeof as),
				  g_vb.hand.phase == SOMVR_VBH_GRAB ? "holds" : g_vb.hand.phase == SOMVR_VBH_WRAITH ? "presses" : "is idle",
				  bcmd, bcmd < SC_NCMD ? in_cmd_name((int)bcmd) : "?", br_track(pcomb, bcmd, tb, sizeof tb),
				  br_track(pcomb, VB_CMD_B, t120, sizeof t120), br_track(pcomb, 125, t125, sizeof t125));
		for (size_t j = 0; j < sizeof nodes / sizeof nodes[0] && k0 < (int)sizeof line - 1; j++) {
			uintptr_t at = gc_pick(pcomb, pc, &o[i], nodes[j], a, sizeof a, s, sizeof s, &why);
			k0 += snprintf(line + k0, sizeof line - (size_t)k0, " %s -> %s%s%s%s%s;", nodes[j], at ? "'" : "none (",
				       at ? a : why, at ? "' sync '" : ")", at ? s : "", at ? "'" : "");
		}
		logf_("%s", line);
		g_br.lines++;
		if (!probe)
			break;
	}
	{ /* the game's blocking command and whether it is held now (each frame) */
		uint32_t bc = 0;
		char tb[40];
		if (peek(pcomb + BR_PC_TRACK + BR_TR_BLOCK, &bc, sizeof bc)) {
			int held = strstr(br_track(pcomb, bc, tb, sizeof tb), " held") != NULL;
			if ((bc != g_br.block_cmd || held != g_br.block_held) && g_vb.test && vb_log_ok())
				logf_("VERBS brand t=%.3f the game's blocking command %u '%s' %s", t, bc, bc < SC_NCMD ?
				      in_cmd_name((int)bc) : "?", held ? "held" : "not held");
			g_br.block_cmd = bc, g_br.block_held = held, g_br.block_t = t;
		}
	}
	if (g_br.after_c && t >= g_br.after_next) { /* a test's press: what followed it, every 0.1 s for 1 s */
		char tb[40], an2[96], ta[96];
		g_br.after_next = t + 0.1;
		uintptr_t ac = g_br.after_c;
		logf_("VERBS brand t=%.3f after the press: Talion's attack '%s' sync '%s', the orc's attack '%s', the tracker's 120 %s, "
		      "pending %d", t, vb_name(peek_ptr(pc + VB_C_ATTACK), ta, sizeof ta), vb_name(vb_sync_rec(pc), an, sizeof an),
		      vb_name(peek_ptr(ac + VB_C_ATTACK), an2, sizeof an2), br_track(pcomb, VB_CMD_B, tb, sizeof tb),
		      peek_ptr(pcomb + FI_PC_PENDING) != 0);
		if (t >= g_br.after_until)
			g_br.after_c = 0;
	}
	{
		char ra[96];
		g_br.talion_recoil = strstr(vb_name(peek_ptr(pc + VB_C_ATTACK), ra, sizeof ra), "Recoil") != NULL;
		if (g_br.autop == 2 && g_vb.test && !g_br.talion_recoil && !ra[1] && g_br.ask_kind == BRK_COMBAT &&
		    t - g_br.ask_t < 0.12 && t >= g_br.auto_until + 1.0 && g_vb.hand.phase == SOMVR_VBH_IDLE) {
			g_br.auto_until = t + 0.25; /* a test aid: the trigger now (Talion's attack '-', the offer fresh) */
			logf_("VERBS brand t=%.3f auto: the combat brand offered, Talion free: the left trigger pressed for 0.25 s (a test "
			      "aid)", t);
		}
	}
	uintptr_t c = g_fr.frame - g_br.reach_frame <= 2 ? g_br.reach_c : 0;
	if (!c || (c == g_br.ask_c && t - g_br.ask_t < g_gc.recheck_s))
		return;
	const struct cb_orc *oc = NULL;
	for (int i = 0; i < n && !oc; i++)
		if (o[i].c == c)
			oc = &o[i];
	if (!oc)
		return;
	g_br.asks++;
	int kind = BRK_NONE;
	uintptr_t at = 0;
	why = "";
	a[0] = 0;
	if (g_br.reach_held || g_br.combat_any || br_stunned(c)) { /* the combat brand (=stunned: a held or stunned orc) */
		if ((at = gc_pick(pcomb, pc, oc, "PC_InstaHOG", a, sizeof a, s, sizeof s, &why)))
			kind = BRK_COMBAT, g_br.combat_yes++;
	}
	if (!kind && !g_br.reach_held && (at = gc_pick(pcomb, pc, oc, "PC_StealthBrand", a, sizeof a, s, sizeof s, &why)))
		kind = BRK_STEALTH, g_br.stealth_yes++;
	g_br.ask_c = c, g_br.ask_kind = kind, g_br.ask_t = t, g_br.ask_why = why;
	snprintf(g_br.ask_atk, sizeof g_br.ask_atk, "%s", kind ? a : "");
	if (kind == BRK_STEALTH && (c != g_br.said_c || kind != g_br.said_kind))
		br_nl_open(t, 3.0); /* build 24: the node log from a new stealth brand offer (SOMVR_VERBS_BRAND_NODELOG) */
	if ((c != g_br.said_c || kind != g_br.said_kind) && vb_log_ok()) {
		g_br.said_c = c, g_br.said_kind = kind;
		char geo[96];
		logf_("VERBS brand t=%.3f orc %#lx (%s, attack '%s'): %s%s%s (%s)", t, (unsigned long)c, g_br.reach_held ? "held" :
		      br_stunned(c) ? "stunned" : "on its head", vb_name(peek_ptr(c + VB_C_ATTACK), an, sizeof an),
		      kind ? "the game offers " : "no brand offered", kind ? g_brk_w[kind] : "", kind ? "" : "",
		      br_geom(oc, geo, sizeof geo));
	}
}

static const char *br_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (g_br.mode == BR_OFF)
		return "verbs brand (SOMVR_VERBS_BRAND is off: ignored)";
	if (!strncmp(w, "status", 6)) {
		logf_("VERBS brand status: mode %s, stealth %s; the game's combat commands %s (off %llu time(s), presses then %llu); "
		      "asks %llu, the game offered the stealth brand %llu / the combat brand "
		      "%llu; wraith presses %llu: chords %llu, taps %llu, the stealth brand held %llu; lines %llu", g_br_w[g_br.mode],
		      g_br.stealth_tap ? "tap" : "hold", g_br.b_on == 1 ? "on" : g_br.b_on == 0 ? "OFF" : "not read",
		      (unsigned long long)g_br.locks, (unsigned long long)g_br.lock_presses, (unsigned long long)g_br.asks, (unsigned long long)g_br.stealth_yes,
		      (unsigned long long)g_br.combat_yes, (unsigned long long)g_br.presses, (unsigned long long)g_br.chords,
		      (unsigned long long)g_br.taps, (unsigned long long)g_br.holds, (unsigned long long)g_br.lines);
		return "verbs brand status";
	}
	if (!g_vb.test)
		return "verbs brand (probe|charge|stealth need SOMVR_HANDS_TEST: ignored)";
	if (!strncmp(w, "probe", 5)) {
		atomic_store(&g_br.req_probe, 1);
		return "verbs brand probe (a test: the game's brand offers for the near orcs)";
	}
	if (!strncmp(w, "charge", 6)) {
		atomic_store(&g_br.req_charge, 1);
		return "verbs brand charge (a test aid: Talion's hit counter charged)";
	}
	if (!strncmp(w, "auto ", 5)) {
		g_br.autop = !strncmp(w + 5, "on", 2) ? 1 : !strncmp(w + 5, "combat", 6) ? 2 : 0;
		return g_br.autop == 1 ? "verbs brand auto on (a test: the trigger at the stealth brand's prompt)"
		       : g_br.autop == 2 ? "verbs brand auto combat (a test: the trigger at the combat brand's offer, Talion free)"
		       : "verbs brand auto off";
	}
	if (!strncmp(w, "nodelog", 7)) { /* build 24: `verbs brand nodelog S` (0 closes it) */
		if (!g_brnl.ok)
			return "verbs brand nodelog (SOMVR_VERBS_BRAND_NODELOG is not on: ignored)";
		double s = atof(w + 7);
		g_brnl.until = s > 0 ? cb_now() + (s < 60 ? s : 60) : 0;
		return s > 0 ? "verbs brand nodelog (open)" : "verbs brand nodelog (closed)";
	}
	if (!strncmp(w, "unblock ", 8)) {
		g_br.unblock = !strncmp(w + 8, "on", 2);
		return g_br.unblock ? "verbs brand unblock on" : "verbs brand unblock off";
	}
	if (!strncmp(w, "stealth ", 8)) {
		g_br.stealth_tap = !strncmp(w + 8, "tap", 3);
		return g_br.stealth_tap ? "verbs brand stealth tap" : "verbs brand stealth hold";
	}
	return "verbs brand (status|probe|charge|auto on|off|stealth tap|hold|unblock on|off|nodelog S: ignored)";
}

/* vb_prompt (a prompt shown): the test aid's press at the stealth brand's prompt */
static void br_prompt_up(int act, const char *str, double t)
{
	if (g_br.mode == BR_OFF || g_br.autop != 1 || !g_vb.test || act != VB_CMD_B || !strstr(str, "Stealth_Brand"))
		return;
	g_br.auto_until = t + 0.25;
	logf_("VERBS brand t=%.3f auto: the stealth brand's prompt '%s' is up: the left trigger pressed for 0.25 s (a test aid)", t,
	      str);
}
static int br_auto_trigger(double t) { return g_br.mode != BR_OFF && t < g_br.auto_until; }

/* in_apply (the input thread): the command let go now for a brand's press (0 = none) */
static int br_unblock_now(double t)
{
	int c = atomic_load(&g_br.unblock_cmd);
	if (c && t >= g_br.unblock_until)
		atomic_store(&g_br.unblock_cmd, 0), c = 0;
	return c;
}

static void br_end_log(void)
{
	if (g_br.mode == BR_OFF)
		return;
	if (g_brnl.on) /* build 24: the node log's byte and sink back to the game's 0 */
		g_brnl.until = 0, br_nl_frame(cb_now());
	logf_("VERBS brand END: the game's combat commands off %llu time(s) (presses then %llu); asks %llu, the game offered the "
	      "stealth brand %llu / the combat brand %llu; wraith presses %llu: chords %llu, taps %llu, the stealth brand held "
	      "%llu; our blocking command let go %llu", (unsigned long long)g_br.locks, (unsigned long long)g_br.lock_presses,
	      (unsigned long long)g_br.asks,
	      (unsigned long long)g_br.stealth_yes, (unsigned long long)g_br.combat_yes, (unsigned long long)g_br.presses,
	      (unsigned long long)g_br.chords, (unsigned long long)g_br.taps, (unsigned long long)g_br.holds,
	      (unsigned long long)g_br.unblocks);
}
