/* somvr_prompts_game.c - build 11, the bow's round 2 (docs/re-bow.md 13): the flat game's pad button prompts in VR.
 * Included by somvr.c after the gameopts file (it uses the hooks' helpers: check_site, write_code, peek, logf_, and
 * in_cmd_name, g_inp, g_bow).
 *
 * SOMVR_PAD_PROMPTS=game|log|vr (default game = nothing patched) - the prompt diamond's button prompts ("(Y) Distract",
 *   "Activate Focus" with a stick-click glyph, "Shadow Strike (A)" while the bow is aimed; "B Drop To Hang" on a ledge).
 *   They are not hints (SOMVR_HINTS): every one comes as a CCharacterProxy PromptShow event {+0 key (a record), +8
 *   action id (the command; 349 = none), +0x10 StringId, +0x18 IconId, +0x20 category, +0x24..+0x26 flags} that each of
 *   the HUD's three CUIHUDPrompts (Standard/Quicktime/Keyboard) takes through a TypedDelegate (re-hud.md 4.2): slot 2
 *   of its vtable puts {key, action} into its CUIPromptDiamond (vf_02), whose pad diamond picks the button's place and
 *   picture from the action's binding (FUN_00aaaf50 on the player's input) - so the glyph is always the flat pad's
 *   button for that command, never what our controllers do. PromptHide {key, action} takes it out again (vf_03: an
 *   entry that is not there is skipped), PromptHoldUpdate {key, action, fill at +0xc} fills a hold ring.
 *   log: every show, hide and the first holds logged with the command's name and the StringId, passed on.
 *   vr: a show is dropped where the HUD takes it (nothing drawn, nothing of the game's state touched) unless a VR
 *   control sends that command in the world: Menu 14 (the left menu button), Crouch 15 (the right stick down),
 *   Activate 34 (B), PDA 95 (the right stick click), the bow's aim CombatSecondary03 124 (the left trigger, SOMVR_BOW),
 *   and with SOMVR_GAMEMOVES=on pad A's Jump 16, FreeRun 18, LedgePullUp 27, CombatPrimary04 121 - but never 121 while
 *   the bow is out (in the aim it is the Shadow Strike, a teleport with no VR presentation yet; SOMVR_BOW_STRIKE takes
 *   it out of the input too). So "(Y) Distract" (CombatPrimary02 119), "Activate Focus" (CombatTertiary 126/127),
 *   "Shadow Strike (A)" (121), the strike / counter / stun / drain prompts and "B Drop To Hang" (CombatPrimary03 120) go:
 *   none of those commands is ever sent by our controls (the deny list, re-xr-input.md 3.6). The kept ones get the
 *   Touch's button where it has the pad's picture (SOMVR_PAD_PROMPTS_MAP, default 34:120,95:127 in vr: Activate shown
 *   as the pad's B = our B, PDA as R3 = our right stick click): the show, the hide and the hold carry the shown command
 *   so the diamond finds its entry again; the text stays the game's. A hide is always passed (on its own command when
 *   no map applies), so a dropped show leaves nothing behind.
 *
 * Trigger words: "padprompts game|log|vr" (live; prompts already up stay until the game hides them), "padprompts map
 * A:B,..." (the relabel table, tests), "padprompts" (a state line). Fail soft: a site mismatch = that part off. */

/* PADPROMPTS_SITES_BEGIN (check_sites.py counts this block on its own: three patched K_VSLOT, slot 2 of the
 * CUIHUDPrompts delegates' vtables for PromptShow 0x3515bc8, PromptHide 0x3515bf8, PromptHoldUpdate 0x3515c58; the
 * context: slots 0..3. Order = PPS_*) */
static const struct site_spec g_pp_sites_game[] = {
SITE("PP_SHOW", K_VSLOT, 0x3515bd8, 0x3515bc8, "f0 2a b2 00 00 00 00 00 80 2e b2 00 00 00 00 00 d0 31 b2 00 00 00 00 00 f0 2d b2 00 00 00 00 00", 0xb231d0, "55 53 48 83 ec 08 48 8b 5f 18 48 85 db 74 71 44"),
SITE("PP_HIDE", K_VSLOT, 0x3515c08, 0x3515bf8, "30 2b b2 00 00 00 00 00 d0 2e b2 00 00 00 00 00 40 31 b2 00 00 00 00 00 b0 2d b2 00 00 00 00 00", 0xb23140, "55 53 48 83 ec 08 48 8b 5f 18 48 85 db 74 39 48"),
SITE("PP_HOLD", K_VSLOT, 0x3515c68, 0x3515c58, "b0 2b b2 00 00 00 00 00 20 2f b2 00 00 00 00 00 b0 30 b2 00 00 00 00 00 30 2d b2 00 00 00 00 00", 0xb230b0, "48 8b 4f 18 48 89 d0 48 85 c9 74 1c 8b 52 08 48"),
};
/* PADPROMPTS_SITES_END */
enum { PPS_SHOW, PPS_HIDE, PPS_HOLD, PPS_N };
_Static_assert(sizeof g_pp_sites_game / sizeof g_pp_sites_game[0] == PPS_N, "padprompts sites");
static const struct site_spec *g_pp_sites = g_pp_sites_game; /* selftest: stand-ins */

enum { PP_GAME, PP_LOG, PP_VR };
static const char *const g_pp_mode_w[] = { "game", "log", "vr" };
#define PP_NONE 349         /* the free entry's / no command */
#define PP_MAP_MAX 8
#define PP_MAP_DEFAULT "34:120,95:127"
typedef void (*pp_dlg_fn)(void *dlg, void *sender, void *params);

static struct {
	int want;                  /* SOMVR_PAD_PROMPTS at start (PP_*) */
	_Atomic int mode;          /* now (the word changes it) */
	int ok[PPS_N];             /* that slot installed */
	uintptr_t orig[PPS_N];
	_Atomic int nmap;          /* the relabel table (vr only): from -> to */
	int map_from[PP_MAP_MAX], map_to[PP_MAP_MAX];
	_Atomic uint64_t n_show, n_drop, n_kept, n_relab, n_hide, n_hold;
	_Atomic uint32_t lines, hold_lines;
	_Atomic uint64_t drop_by[PP_NONE + 1];
} g_pp;

/* vr: may a prompt for this command show? (a VR control sends it in the world; never 121 while the bow is out) */
static int pp_vr_keep(int cmd, int game_moves, int bow_on, int bow_out)
{
	switch (cmd) {
	case 14: case 15: case 34: case 95: /* Menu, Crouch, Activate, PDA: the menu button, the stick down, B, the click */
		return 1;
	case 124: /* CombatSecondary03: the bow's aim (the left trigger) */
		return bow_on;
	case 16: case 18: case 27: /* Jump, FreeRun, LedgePullUp: pad A's (and the sprint latch's) with the game's moves */
		return game_moves;
	case 121: /* CombatPrimary04: pad A's evade; in the aim the Shadow Strike (X with SOMVR_BOW_STRIKE=dash|blink) */
		return bow_out ? sk_x() : game_moves;
	default:
		return cmd == PP_NONE;
	}
}

/* the shown command (vr's relabel), else the command itself */
static int pp_mapped(int cmd)
{
	if (atomic_load_explicit(&g_pp.mode, memory_order_relaxed) != PP_VR)
		return cmd;
	int n = atomic_load_explicit(&g_pp.nmap, memory_order_acquire);
	for (int i = 0; i < n; i++)
		if (g_pp.map_from[i] == cmd)
			return g_pp.map_to[i];
	return cmd;
}

/* "A:B,C:D" -> the table; 0 = bad (the table unchanged) */
static int pp_parse_map(const char *s, int *from, int *to, int *n)
{
	int k = 0;
	while (s && *s && *s != ' ' && *s != '\n') {
		char *e;
		long a = strtol(s, &e, 10), b;
		if (e == s || *e != ':' || a < 0 || a >= PP_NONE)
			return 0;
		s = e + 1;
		b = strtol(s, &e, 10);
		if (e == s || b < 0 || b >= PP_NONE || k >= PP_MAP_MAX)
			return 0;
		from[k] = (int)a, to[k++] = (int)b;
		s = *e == ',' ? e + 1 : e;
	}
	*n = k;
	return 1;
}

static int pp_bow_out(void) { return g_bow.on && g_bow.s.phase != SOMVR_BOW_PH_IDLE; }

/* SOMVR_BOW_STRIKE=dash|blink: the Shadow Strike's prompt (121 with the bow out) is shown with pad X's glyph
 * (CombatPrimary01 118: the Touch's X sends it); its hides follow the key it was shown under (the HUD shows each prompt
 * on three diamonds: three shows, three hides per key - run 065907) */
#define PP_X_CMD 118
static uintptr_t g_pp_xkeys[8];
static int g_pp_xn[8];
static int pp_xkey(uintptr_t key, int put)
{
	for (int i = 0; i < 8; i++)
		if (g_pp_xkeys[i] == key && g_pp_xn[i] > 0) {
			if (put > 0)
				g_pp_xn[i]++;
			else if (put < 0 && --g_pp_xn[i] == 0)
				g_pp_xkeys[i] = 0;
			return 1;
		}
	for (int i = 0; put > 0 && i < 8; i++)
		if (!g_pp_xn[i]) {
			g_pp_xkeys[i] = key, g_pp_xn[i] = 1;
			return 1;
		}
	return 0;
}

/* PP_SHOW (the game thread): log; vr: drop the show of a command no VR control sends, relabel a kept one */
__attribute__((used)) void somvr_w_ppshow(void *dlg, void *sender, void *params)
{
	unsigned char p[0x28];
	int ok = params && peek((uintptr_t)params, p, sizeof p), mode = atomic_load_explicit(&g_pp.mode, memory_order_relaxed);
	if (!ok || mode == PP_GAME) {
		((pp_dlg_fn)g_pp.orig[PPS_SHOW])(dlg, sender, params);
		return;
	}
	int32_t act;
	uintptr_t key, str, icon;
	uint32_t cat;
	memcpy(&key, p, 8), memcpy(&act, p + 8, 4), memcpy(&str, p + 0x10, 8), memcpy(&icon, p + 0x18, 8), memcpy(&cat, p + 0x20, 4);
	uint64_t n = atomic_fetch_add_explicit(&g_pp.n_show, 1, memory_order_relaxed) + 1;
	int bow_out = pp_bow_out(), drop = mode == PP_VR && act >= 0 && act <= PP_NONE &&
							    !pp_vr_keep(act, g_inp.cfg.game_moves, g_bow.on, bow_out) &&
							    !vb_keep(act, key) && /* SOMVR_VERBS: the grab's own prompts */
							    !us_keep(act, (const char *)str); /* SOMVR_USE: the pick prompts (B) */
	int to = drop ? act : us_keep(act, (const char *)str) ? US_GLYPH : pp_mapped(act);
	us_prompt(key, act, (const char *)str, 1); /* SOMVR_USE: the pick prompts up */
	sk_prompt(act, 1); /* SOMVR_BOW_STRIKE: the Shadow Strike's prompt is up (kept or dropped) */
	vb_prompt(key, act, (const char *)str, 1); /* SOMVR_VERBS: the verbs the game offers now */
	if (!drop && act == 121 && bow_out && sk_x() && pp_xkey(key, 1))
		to = PP_X_CMD;
	if (drop) {
		atomic_fetch_add_explicit(&g_pp.n_drop, 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&g_pp.drop_by[act], 1, memory_order_relaxed);
	} else {
		atomic_fetch_add_explicit(&g_pp.n_kept, 1, memory_order_relaxed);
		atomic_fetch_add_explicit(&g_pp.n_relab, (uint64_t)(to != act), memory_order_relaxed);
	}
	if (atomic_fetch_add_explicit(&g_pp.lines, 1, memory_order_relaxed) < 400) {
		char sb[64], ib[64];
		go_str((const char *)str, sb, sizeof sb), go_str((const char *)icon, ib, sizeof ib);
		char how[96];
		if (drop)
			snprintf(how, sizeof how, "dropped (no VR control sends it%s)", act == 121 && bow_out ? "; the bow is out" : "");
		else if (to != act)
			snprintf(how, sizeof how, "shown as %d '%s' (the Touch's button)", to, in_cmd_name(to));
		else
			snprintf(how, sizeof how, "passed on");
		logf_("PROMPTS show #%llu t=%.3f: action %d '%s' '%s' icon '%s' category %u key %#lx%s: %s", (unsigned long long)n,
		      mono(), act, in_cmd_name(act), sb, ib, cat, (unsigned long)key, bow_out ? " (the bow out)" : "", how);
	}
	if (drop)
		return;
	const char *vt = vb_text(act, (const char *)str); /* SOMVR_VERBS: the verb's VR words (NULL = the game's) */
	if (to != act || vt) {
		memcpy(p + 8, &to, 4);
		if (vt)
			memcpy(p + 0x10, &vt, sizeof vt);
		((pp_dlg_fn)g_pp.orig[PPS_SHOW])(dlg, sender, p);
		return;
	}
	((pp_dlg_fn)g_pp.orig[PPS_SHOW])(dlg, sender, params);
}

/* PP_HIDE: always passed (on the shown command when the relabel applies) */
__attribute__((used)) void somvr_w_pphide(void *dlg, void *sender, void *params)
{
	unsigned char p[0x10];
	int ok = params && peek((uintptr_t)params, p, sizeof p), mode = atomic_load_explicit(&g_pp.mode, memory_order_relaxed);
	if (!ok || mode == PP_GAME) {
		((pp_dlg_fn)g_pp.orig[PPS_HIDE])(dlg, sender, params);
		return;
	}
	int32_t act, to;
	uintptr_t key;
	memcpy(&key, p, 8), memcpy(&act, p + 8, 4);
	to = us_hide_to(key, act) != act ? US_GLYPH : pp_mapped(act); /* SOMVR_USE: shown as B */
	us_prompt(key, act, NULL, 0);
	sk_prompt(act, 0);
	vb_prompt(key, act, NULL, 0); /* SOMVR_VERBS */
	if (act == 121 && pp_xkey(key, -1)) /* shown with X's glyph: hidden as that */
		to = PP_X_CMD;
	uint64_t n = atomic_fetch_add_explicit(&g_pp.n_hide, 1, memory_order_relaxed) + 1;
	if (atomic_fetch_add_explicit(&g_pp.lines, 1, memory_order_relaxed) < 400)
		logf_("PROMPTS hide #%llu t=%.3f: action %d '%s' key %#lx: passed on%s", (unsigned long long)n, mono(), act,
		      in_cmd_name(act), (unsigned long)key, to != act ? " as the shown command" : "");
	if (to != act) {
		memcpy(p + 8, &to, 4);
		((pp_dlg_fn)g_pp.orig[PPS_HIDE])(dlg, sender, p);
		return;
	}
	((pp_dlg_fn)g_pp.orig[PPS_HIDE])(dlg, sender, params);
}

/* PP_HOLD: a hold ring's fill, passed (on the shown command when the relabel applies) */
__attribute__((used)) void somvr_w_pphold(void *dlg, void *sender, void *params)
{
	unsigned char p[0x10];
	int ok = params && peek((uintptr_t)params, p, sizeof p), mode = atomic_load_explicit(&g_pp.mode, memory_order_relaxed);
	if (!ok || mode == PP_GAME) {
		((pp_dlg_fn)g_pp.orig[PPS_HOLD])(dlg, sender, params);
		return;
	}
	int32_t act, to;
	float fill;
	memcpy(&act, p + 8, 4), memcpy(&fill, p + 0xc, 4);
	to = pp_mapped(act);
	atomic_fetch_add_explicit(&g_pp.n_hold, 1, memory_order_relaxed);
	vb_prompt_hold(act, fill); /* SOMVR_VERBS */
	if (atomic_fetch_add_explicit(&g_pp.hold_lines, 1, memory_order_relaxed) < 20)
		logf_("PROMPTS hold t=%.3f: action %d '%s' fill %.2f: passed on%s", mono(), act, in_cmd_name(act), fill,
		      to != act ? " as the shown command" : "");
	if (to != act) {
		memcpy(p + 8, &to, 4);
		((pp_dlg_fn)g_pp.orig[PPS_HOLD])(dlg, sender, p);
		return;
	}
	((pp_dlg_fn)g_pp.orig[PPS_HOLD])(dlg, sender, params);
}

static void pp_state_line(const char *head)
{
	char top[160] = "";
	size_t o = 0;
	for (int c = 0; c <= PP_NONE && o + 24 < sizeof top; c++) {
		uint64_t k = atomic_load(&g_pp.drop_by[c]);
		if (k)
			o += (size_t)snprintf(top + o, sizeof top - o, "%s%d %s x%llu", o ? ", " : "", c, in_cmd_name(c),
					      (unsigned long long)k);
	}
	logf_("%s %s: shows %llu (dropped %llu, kept %llu, relabelled %llu), hides %llu, holds %llu; dropped by command: %s", head,
	      g_pp_mode_w[atomic_load(&g_pp.mode)], (unsigned long long)atomic_load(&g_pp.n_show),
	      (unsigned long long)atomic_load(&g_pp.n_drop), (unsigned long long)atomic_load(&g_pp.n_kept),
	      (unsigned long long)atomic_load(&g_pp.n_relab), (unsigned long long)atomic_load(&g_pp.n_hide),
	      (unsigned long long)atomic_load(&g_pp.n_hold), o ? top : "none");
}

/* the trigger words (the writer thread): "padprompts [game|log|vr|map A:B,...]" */
static const char *pp_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (!*w || *w == '\n') {
		pp_state_line("PROMPTS t=now");
		return "padprompts";
	}
	if (!(g_pp.ok[PPS_SHOW] && g_pp.ok[PPS_HIDE]))
		return "padprompts (no SOMVR_PAD_PROMPTS=log|vr at start: the delegates are not hooked)";
	if (!strncmp(w, "map ", 4)) {
		int f[PP_MAP_MAX], t[PP_MAP_MAX], n;
		if (!pp_parse_map(w + 4, f, t, &n))
			return "padprompts map (not A:B,...: ignored)";
		atomic_store(&g_pp.nmap, 0);
		memcpy(g_pp.map_from, f, sizeof f), memcpy(g_pp.map_to, t, sizeof t);
		atomic_store_explicit(&g_pp.nmap, n, memory_order_release);
		logf_("PROMPTS map now %d pair(s) (the word; vr only)", n);
		return "padprompts map";
	}
	for (int i = 0; i < 3; i++)
		if (!strncmp(w, g_pp_mode_w[i], strlen(g_pp_mode_w[i]))) {
			atomic_store(&g_pp.mode, i);
			logf_("PROMPTS now %s (the word)", g_pp_mode_w[i]);
			return "padprompts";
		}
	return "padprompts (not game|log|vr|map: ignored)";
}

static void pp_start(const struct site_spec *sites, int *bad)
{
	memset(&g_pp, 0, sizeof g_pp);
	g_pp_sites = sites;
	g_pp.want = env_word("SOMVR_PAD_PROMPTS", g_pp_mode_w, 3, PP_GAME);
	if (g_pp.want == PP_GAME)
		return;
	const char *mv = getenv("SOMVR_PAD_PROMPTS_MAP");
	int n = 0;
	if (!pp_parse_map(mv ? mv : PP_MAP_DEFAULT, g_pp.map_from, g_pp.map_to, &n)) {
		logf_("WARN SOMVR_PAD_PROMPTS_MAP='%s' is not A:B,...: no relabel", mv);
		(*bad)++, n = 0;
	}
	atomic_store(&g_pp.nmap, n);
	static void *const fns[PPS_N] = { (void *)somvr_w_ppshow, (void *)somvr_w_pphide, (void *)somvr_w_pphold };
	char why[512];
	for (int i = 0; i < PPS_N; i++) {
		if (check_site(&sites[i], why, sizeof why) != 0) {
			logf_("PROMPTS site %s %#lx -> %#lx MISMATCH: %s", sites[i].name, (unsigned long)sites[i].addr,
			      (unsigned long)sites[i].target, why);
			continue;
		}
		g_pp.ok[i] = go_slot(&sites[i], fns[i], &g_pp.orig[i]);
	}
	/* a show without its hide would leave prompts up: both or neither (the hold alone is harmless) */
	if (g_pp.ok[PPS_SHOW] != g_pp.ok[PPS_HIDE]) {
		int k = g_pp.ok[PPS_SHOW] ? PPS_SHOW : PPS_HIDE;
		char err[200] = "";
		void *orig = (void *)g_pp.orig[k];
		if (write_code(sites[k].addr, &orig, 8, err, sizeof err) == 0)
			g_pp.ok[k] = 0;
		logf_("WARN PROMPTS: %s without %s: %s", sites[k].name, sites[k ^ 1].name,
		      g_pp.ok[k] ? "the slot could not be put back" : "put back, the prompts are the game's");
	}
	atomic_store(&g_pp.mode, g_pp.ok[PPS_SHOW] ? g_pp.want : PP_GAME);
	char mp[96] = "";
	for (int i = 0, o = 0; i < n && o + 12 < (int)sizeof mp; i++)
		o += snprintf(mp + o, sizeof mp - (size_t)o, "%s%d->%d", i ? "," : "", g_pp.map_from[i], g_pp.map_to[i]);
	logf_("CONFIG pad_prompts=%s (SOMVR_PAD_PROMPTS: %s): show %s, hide %s, hold %s; the relabel %s",
	      g_pp_mode_w[g_pp.want],
	      g_pp.want == PP_VR ? "the flat pad's button prompts for commands no VR control sends are dropped where the HUD "
				   "takes them (Distract, Focus, Shadow Strike, strike/counter/stun, Drop To Hang)"
				 : "every prompt logged and passed on",
	      g_pp.ok[PPS_SHOW] ? "hooked" : "NOT hooked", g_pp.ok[PPS_HIDE] ? "hooked" : "NOT hooked",
	      g_pp.ok[PPS_HOLD] ? "hooked" : "NOT hooked", n ? mp : "none");
}

static void pp_end_log(void)
{
	if (!g_pp.ok[PPS_SHOW])
		return;
	pp_state_line("PROMPTS END");
}
