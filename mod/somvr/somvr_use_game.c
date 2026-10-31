/* somvr_use_game.c - SOMVR_USE=1: what the flat pad's RB and a quest card's buttons do, by our controls (build 14
 * candidate; the story drill, handoff/HANDOFF-story.md, docs/re-xr-pipeline.md 43). Included by somvr.c after the
 * verbs file (it uses g_inp, g_bm, the input's IS_CMD_ACTIVE, in_cmd_name, go_str, logf_).
 *
 * What the game does (runs 20261026-075509, -081246):
 *   - the pick prompts: an herb ('Combat_Activate_Flower'), arrows ('Combat_Activate_Ammo'), ... come as prompt
 *     diamonds on CombatSecondary02 (123, the pad's RB, key 'Sword_ButtonRB') - the grab's command, on our deny list -
 *     so B (Activate 34) picked nothing and SOMVR_PAD_PROMPTS=vr dropped them. 123 at such a prompt = the pick
 *     (`Activate_HealthCollectable`, the view stays the player's). 'Combat_Activate_Ammo_Full' (nothing to take) is on
 *     123 too, where 123 is the grab's swing (`PC_Grab_Attempt_F_Fail`): never pressed there.
 *   - the quests: at a mission's marker "(RB) Start Mission"; QuestConfirmStart (283) there started the Nemesis
 *     mission 'Beast Hunt' (the loading cover, the captain's introduction); a story mission ('Queen of the Shore')
 *     shows a card "(A) Accept (B) Cancel", a failed one "(B) Abandon" - presentations (SOMVR_BODYMOMENTS' window)
 *     while the session says the world (`ctx=world paused=0`), so the controllers are not the pad there: A did
 *     nothing (SOMVR_GAMEMOVES=off), B was Activate.
 *
 * With SOMVR_USE=1:
 *   - a pick prompt (123 with a StringId 'Combat_Activate_*', not '*_Full') is kept and shown with B's glyph (120, as
 *     SOMVR_PAD_PROMPTS_MAP shows Activate); B pressed while one is up holds 123 as well as Activate until B is let go;
 *   - in the world B = the pad's RB for the quest start: the commands the game binds to RB among QuestConfirm (282)
 *     and QuestConfirmStart (283), each only while the game has it active;
 *   - while the window is up (a presentation or a cinematic, SOMVR_BODYMOMENTS), A, B, X, Y and the grips = the pad's
 *     A, B, X, Y, LB, RB for the quest and skip commands only (CinematicSkip 260/261, SkipPresentations 265,
 *     QuestConfirm 282 ... QuestFailedAbandon 286), each when the game has it active: never a move, a jump or a
 *     combat command. Our presses, never the pad's buttons; nothing in a menu (the pad passthrough does it there).
 *   - build 25 (re-xr-pipeline.md 103), SOMVR_USE_DEATH=on|off (code default on): a death in a mission shows the
 *     game's "Mission Failed! ... (A) Retry (B) Abandon" (Reason: You died!) in the world view, and with SOMVR_BM_DEATH=
 *     first (build 22) the body moments open no window then (the death's grey) - so A and B answered nothing and the
 *     player was stuck on that screen (run 20261029-161017, 'Big Game'). The game binds neither command to a pad button
 *     there (`USE bindings`: only RB's; boot 20261029-172042) and its own presses of them work (the test press of 285 =
 *     Retry, boot 161017). While the death's grey holds (the body moments' action GREY, no window) and a presentation
 *     runs (the failed screen; the dying's own camera too), A = QuestFailedRetry 285, B = QuestFailedAbandon 286, pressed
 *     directly (no binding, no active flag: as the result screens' 265), the held-button rule as at a window's edge.
 *   - build 25, SOMVR_USE_SKIP=on|off (code default on): a Nemesis result screen in the window ("Ratlûg the Gluttonous
 *     - Died - ... (B) Back", run 20261029-161017) binds none of our commands to B (`USE bindings`: only RB's), and B
 *     did nothing: the screen stayed 4 min; SkipPresentations 265 (a test press) closed it at once. With the window up,
 *     B pressed while none of B's own commands is pressed = 265 (the game's skip of the presentation; logged with its
 *     active flag): the result screens' Back, a captain's introduction or a cinematic skipped by an explicit B.
 *   - build 27b (som-b27-story3, re-xr-pipeline.md 113), SOMVR_USE_GRAB=on|off (code default on): while the player's
 *     left hand holds (SOMVR_VERBS' grab: Lithariel carried in "The Rescue", an orc held) SOMVR_BODYMOMENTS keeps first
 *     person and masks the game's presentation (somvr_bm_conds' our_grab: the grab's interrogation is one), so a
 *     presentation that comes meanwhile opened no window and the window's buttons answered nothing: "The Rescue - Mission
 *     Failed! Reason: Ally Killed! (A) Retry (B) Abandon" took A only after the grip was let go (boot 20261030-055352:
 *     the presentation up at t=1098411.6, the grip opened at 1098487.5, the window and 285 at once). Now a presentation
 *     (the fallback's raw bit) under the grab, with no window and not the grab's own interrogation (SOMVR_VERBS' ask),
 *     is the window for these buttons (the bindings, the skip, the held-button rule); the view stays as it is.
 * Lines: `CONFIG use=...`, `USE bindings: ...` (the pad buttons of those commands, at each new binding table, 8 at
 * most), `USE t=.. pick prompt up|down ...`, `USE t=.. B at '...': 123 held`, `USE t=.. CMD 'NAME' pressed (pad X,
 * active)`, `USE END ...`. Word: `use on|off|status` (live; the before/after in one boot). */

#define US_PICK_CMD 123  /* CombatSecondary02 */
#define US_UP 8
/* the window's: skips and quests; build 25 (som-b25-nemesis): 307 IdentifySkip - "(B) Back" on a power struggle's outcome
 * ("Power Increased! ... (A) View Battle Report (B) Back"), which waited 200+ s for it (re-xr-pipeline.md 104.2) */
static const int g_us_quest[] = { 260, 261, 265, 282, 283, 284, 285, 286, 307 };
#define US_NQ ((int)(sizeof g_us_quest / sizeof g_us_quest[0]))
static const uint32_t US_BTNS = (1u << SC_PAD_A) | (1u << SC_PAD_B) | (1u << SC_PAD_X) | (1u << SC_PAD_Y) |
				(1u << SC_PAD_LB) | (1u << SC_PAD_RB);

static struct {
	_Atomic int on;
	int want;                                           /* SOMVR_USE at start */
	struct { uintptr_t key; int n; double t; char str[48]; } up[US_UP]; /* pick prompts up (the game thread) */
	int nup;
	int b_prev, latch;                                  /* B's last state, 123 held for this press */
	uint32_t qprev;                                     /* bit q: g_us_quest[q] pressed last tick */
	int death, skip;                                    /* build 25: SOMVR_USE_DEATH, SOMVR_USE_SKIP */
	int grab, gwin_prev;                                /* build 27b: SOMVR_USE_GRAB; the grab's presentation last tick */
	int win_prev, fresh;                                /* the window last tick; 1 = take the held buttons again */
	uint32_t held;                                      /* buttons held since before the window opened or closed */
	int map_lines, lines;
	uint64_t kept, picks, pick_ticks, quest_presses, quest_ticks, refused_full, skips, deaths, grabs;
} g_us;

static int us_log_ok(void) { return g_us.lines++ < 300; }

/* a pick prompt: 123 with 'Combat_Activate_*', not '*_Full' (str = the PromptShow's StringId, a C string) */
static int us_pick(int act, const char *str, int *full)
{
	char sb[48];
	*full = 0;
	if (act != US_PICK_CMD || !str)
		return 0;
	go_str(str, sb, sizeof sb);
	if (strncmp(sb, "Combat_Activate_", 16))
		return 0;
	size_t n = strlen(sb);
	*full = n >= 5 && !strcmp(sb + n - 5, "_Full");
	return !*full;
}

/* the PP show hook (the game thread, SOMVR_PAD_PROMPTS log|vr): 1 = a pick prompt we serve (kept, shown as B) */
static int us_keep(int act, const char *str)
{
	int full;
	return atomic_load_explicit(&g_us.on, memory_order_relaxed) && us_pick(act, str, &full);
}

/* the PP show / hide hooks: track the pick prompts up (a prompt comes once per HUD prompt object: counted) */
static void us_prompt(uintptr_t key, int act, const char *str, int up)
{
	if (act != US_PICK_CMD || !key)
		return;
	int i = 0, full = 0;
	while (i < g_us.nup && g_us.up[i].key != key)
		i++;
	double t = cb_now();
	if (up) {
		if (!us_pick(act, str, &full)) {
			if (full && atomic_load_explicit(&g_us.on, memory_order_relaxed))
				g_us.refused_full++;
			return;
		}
		if (i < g_us.nup) {
			g_us.up[i].n++;
			return;
		}
		if (g_us.nup == US_UP)
			return;
		g_us.up[i].key = key, g_us.up[i].n = 1, g_us.up[i].t = t;
		go_str(str, g_us.up[i].str, sizeof g_us.up[i].str);
		g_us.nup++;
		if (atomic_load_explicit(&g_us.on, memory_order_relaxed)) {
			g_us.kept++;
			if (us_log_ok())
				logf_("USE t=%.3f pick prompt up: '%s' (action %d '%s'): kept, shown with B's glyph; B holds %d too",
				      t, g_us.up[i].str, act, in_cmd_name(act), US_PICK_CMD);
		}
		return;
	}
	if (i == g_us.nup || --g_us.up[i].n > 0)
		return;
	if (atomic_load_explicit(&g_us.on, memory_order_relaxed) && us_log_ok())
		logf_("USE t=%.3f pick prompt down: '%s' (up %.2f s)", t, g_us.up[i].str, t - g_us.up[i].t);
	g_us.up[i] = g_us.up[--g_us.nup];
}

/* the PP hide hook: the command the show was shown as (US_GLYPH for ours), else act */
static int us_hide_to(uintptr_t key, int act)
{
	if (act != US_PICK_CMD || !atomic_load_explicit(&g_us.on, memory_order_relaxed))
		return act;
	for (int i = 0; i < g_us.nup; i++)
		if (g_us.up[i].key == key)
			return US_GLYPH;
	return act;
}

static int us_quest_ix(int cmd)
{
	for (int q = 0; q < US_NQ; q++)
		if (g_us_quest[q] == cmd)
			return q;
	return -1;
}

/* the pad buttons the game binds the quest commands (and 123, Activate) to: logged when they change (8 lines at most) */
static void us_log_bindings(void)
{
	static char last[640];
	char b[640];
	int n = 0;
	b[0] = 0;
	for (int i = 0; i < SC_NPAD && n < (int)sizeof b - 48; i++)
		for (int k = 0; k < g_inp.nmap[i] && n < (int)sizeof b - 48; k++) {
			int c = g_inp.map[i][k].cmd;
			if (us_quest_ix(c) >= 0 || c == US_PICK_CMD || c == SC_CMD_ACTIVATE)
				n += snprintf(b + n, sizeof b - (size_t)n, " %d '%s'=%s", c, in_cmd_name(c), sc_pad_name(i));
		}
	if (!strcmp(b, last) || g_us.map_lines >= 8)
		return;
	memcpy(last, b, sizeof last);
	logf_("USE bindings (%d, ctx %d):%s", ++g_us.map_lines, g_inp.ctx, n ? b : " none of those commands bound to the pad");
}

/* the buttons that count this tick (the window's state win, the buttons held raw): a button held when the window opens
 * or closes (or the context comes back from a menu) counts only after it is let go - run 090034: B held 0.4 s for
 * "Start Mission" was still down when the card came up 0.15 s later and pressed its Cancel */
static uint32_t us_btns(int win, uint32_t raw)
{
	raw &= US_BTNS;
	if (g_us.fresh || win != g_us.win_prev)
		g_us.held = raw;
	g_us.fresh = 0, g_us.win_prev = win;
	g_us.held &= raw;
	return raw & ~g_us.held;
}

/* build 27b, SOMVR_USE_GRAB: a presentation (the fallback's raw bit) while the left hand holds - not its interrogation */
static int us_grab_pres(void)
{
	return g_us.grab && g_vb.mode == VB_ON && g_vb.hand.phase == SOMVR_VBH_GRAB && !g_vb.ask_c &&
	       (g_fb.last_raw & SOMVR_FB_BIT(SOMVR_FB_PRES));
}

/* the input tick (in_apply, not in a menu): the commands to hold this tick (the deny list's 123 too: our press) */
static int us_press_cmds(const sc_tick_out *o, int out[12])
{
	if (!atomic_load_explicit(&g_us.on, memory_order_relaxed) || !o->live || o->ctx == SC_CTX_MENU) {
		g_us.b_prev = g_us.latch = 0, g_us.qprev = 0, g_us.fresh = 1;
		return 0;
	}
	double t = cb_now();
	int n = 0, b = 0;
	for (int k = 0; k < o->ncmd; k++)
		b |= o->cmd[k] == SC_CMD_ACTIVATE;
	if (b && !g_us.b_prev && g_us.nup) {
		g_us.latch = 1, g_us.picks++;
		if (us_log_ok())
			logf_("USE t=%.3f B at '%s': %d '%s' held with Activate (pick %llu)", t, g_us.up[0].str, US_PICK_CMD,
			      in_cmd_name(US_PICK_CMD), (unsigned long long)g_us.picks);
	}
	if (!b)
		g_us.latch = 0;
	g_us.b_prev = b;
	if (g_us.latch)
		out[n++] = US_PICK_CMD, g_us.pick_ticks++;
	int win = g_bm.last_win;
	int dwin = !win && g_us.death && g_bm.last_act == SOMVR_BMA_GREY && /* build 25: the death's failed screen */
		   (g_bm.last_conds & SOMVR_BMC_BIT(SOMVR_FB_PRES));
	int gwin = !win && !dwin && us_grab_pres(); /* build 27b: the grab's presentation = the window for these buttons */
	if (gwin != g_us.gwin_prev && us_log_ok())
		logf_("USE t=%.3f %s", t, gwin ? "a presentation while the left hand holds (no window: the grab keeps first "
		      "person): A B X Y and the grips answer it as in the window (SOMVR_USE_GRAB)" : "the grab's presentation is over");
	g_us.gwin_prev = gwin;
	uint32_t btn = us_btns(win || dwin || gwin, o->raw);
	if (!g_inp.map_ok)
		return n;
	us_log_bindings();
	int w = win || gwin;
	uint32_t want = w ? btn : !dwin && b && (btn >> SC_PAD_B & 1) ? 1u << SC_PAD_RB : 0, now = 0;
	in_active_fn active = IN_FN(IS_CMD_ACTIVE, in_active_fn);
	for (int i = 0; i < SC_NPAD && want; i++) {
		if (!(want >> i & 1))
			continue;
		for (int k = 0; k < g_inp.nmap[i] && n < 12; k++) {
			int c = g_inp.map[i][k].cmd, q = us_quest_ix(c);
			if (q < 0 || (!w && c != 282 && c != 283) || !(active(g_inp.pi, (unsigned)c) & 0xff) || (now >> q & 1))
				continue;
			now |= 1u << q, out[n++] = c, g_us.quest_ticks++;
			if (!(g_us.qprev >> q & 1)) {
				g_us.quest_presses++, g_us.grabs += gwin;
				if (us_log_ok())
					logf_("USE t=%.3f %d '%s' pressed (the pad's %s, active%s)", t, c, in_cmd_name(c), sc_pad_name(i),
					      win ? "; a presentation is up" : gwin ? "; a presentation while the left hand holds" :
					      "; B in the world");
			}
		}
	}
	for (int k = 0; dwin && k < 2 && n < 12; k++) { /* build 25: A = 285, B = 286 on the death's failed screen */
		int c = k ? 286 : 285, q = us_quest_ix(c);
		if (!(btn >> (k ? SC_PAD_B : SC_PAD_A) & 1) || (now >> q & 1))
			continue;
		now |= 1u << q, out[n++] = c, g_us.quest_ticks++;
		if (!(g_us.qprev >> q & 1)) {
			g_us.quest_presses++, g_us.deaths++;
			if (us_log_ok())
				logf_("USE t=%.3f %d '%s' pressed by %s (the death's failed screen, no window; the game's active flag %d; "
				      "SOMVR_USE_DEATH)", t, c, in_cmd_name(c), k ? "B" : "A", !!(active(g_inp.pi, (unsigned)c) & 0xff));
		}
	}
	if (w && g_us.skip && (btn >> SC_PAD_B & 1) && !now && n < 12) { /* build 25: B = 265 there */
		int q = us_quest_ix(265), act = !!(active(g_inp.pi, 265u) & 0xff);
		now |= 1u << q, out[n++] = 265, g_us.quest_ticks++;
		if (!(g_us.qprev >> q & 1)) {
			g_us.quest_presses++, g_us.skips++, g_us.grabs += gwin;
			if (us_log_ok())
				logf_("USE t=%.3f 265 'SkipPresentations' pressed by B (nothing of B's own bound; the game's active flag "
				      "%d; SOMVR_USE_SKIP)", t, act);
		}
	}
	g_us.qprev = now;
	return n;
}

static const char *us_word(const char *w)
{
	if (!strcmp(w, "on") || !strcmp(w, "off")) {
		atomic_store(&g_us.on, w[1] == 'n');
		logf_("USE now %s (the word)", w);
		return w[1] == 'n' ? "use on" : "use off";
	}
	if (!strcmp(w, "status")) {
		logf_("USE status: %s; pick prompts up %d%s%s; picks %llu, quest presses %llu", g_us.on ? "on" : "off", g_us.nup,
		      g_us.nup ? " (first '" : "", g_us.nup ? g_us.up[0].str : "", (unsigned long long)g_us.picks,
		      (unsigned long long)g_us.quest_presses);
		return "use status";
	}
	return "use (not on|off|status: ignored)";
}

static void us_config(int *bad)
{
	(void)bad;
	g_us.want = env_word("SOMVR_USE", (const char *const[]){ "0", "1" }, 2, 0);
	if (g_us.want && !g_inp.on) {
		logf_("WARN SOMVR_USE=1 needs SOMVR_INPUT=1: off");
		g_us.want = 0;
	}
	if (g_us.want && g_pp.want == PP_GAME)
		logf_("WARN SOMVR_USE=1 without SOMVR_PAD_PROMPTS=log|vr: the pick prompts are not seen (the quests' buttons work)");
	atomic_store(&g_us.on, g_us.want);
	logf_("CONFIG use=%d (SOMVR_USE%s): %s", g_us.want, getenv("SOMVR_USE") ? "" : " unset",
	      g_us.want ? "B at a pick prompt ('Combat_Activate_*', not '_Full': herbs, arrows, ...) holds CombatSecondary02 "
			  "123 too and the prompt is shown with B's glyph (SOMVR_PAD_PROMPTS=vr kept it out); B in the world = the "
			  "pad's RB for QuestConfirm 282 / QuestConfirmStart 283 (\"(RB) Start Mission\"); with the window up (a "
			  "presentation, a cinematic) A B X Y and the grips = the pad's A B X Y LB RB for the skips and the quest "
			  "cards (260 261 265 282-286), each only while the game has it active"
			: "B = Activate only (build 13)");
	g_us.skip = env_word("SOMVR_USE_SKIP", (const char *const[]){ "off", "on" }, 2, 1);
	if (g_us.want)
		logf_("CONFIG use_skip=%s (SOMVR_USE_SKIP, build 25%s): %s", g_us.skip ? "on" : "off",
		      getenv("SOMVR_USE_SKIP") ? "" : "; unset: on", g_us.skip ? "with the window up, B with none of its own "
		      "commands bound = SkipPresentations 265 (a Nemesis result screen's Back, a captain's introduction skipped)"
		      : "B in the window presses only what the game binds to it (build 24)");
	g_us.death = env_word("SOMVR_USE_DEATH", (const char *const[]){ "off", "on" }, 2, 1);
	if (g_us.want)
		logf_("CONFIG use_death=%s (SOMVR_USE_DEATH, build 25%s): %s", g_us.death ? "on" : "off",
		      getenv("SOMVR_USE_DEATH") ? "" : "; unset: on", g_us.death ? "while the death's grey holds (no window) and "
		      "a presentation runs (a mission's 'You died!' screen), A = QuestFailedRetry 285, B = QuestFailedAbandon "
		      "286, pressed directly (the game binds neither to the pad there)" : "the death's failed screen answers no button (build 24)");
	g_us.grab = env_word("SOMVR_USE_GRAB", (const char *const[]){ "off", "on" }, 2, 1);
	if (g_us.want)
		logf_("CONFIG use_grab=%s (SOMVR_USE_GRAB, build 27b%s): %s", g_us.grab ? "on" : "off",
		      getenv("SOMVR_USE_GRAB") ? "" : "; unset: on", g_us.grab ? "a presentation while the left hand holds (no "
		      "window: the grab keeps first person; not its interrogation) is the window for A B X Y and the grips - a "
		      "failed screen's Retry with the grip still closed" : "a presentation under the grab answers no button until "
		      "the grip opens (build 27)");
}

/* vm_start: the Controls page's "Use: B (pick, missions)", Menu and Pause in one line, Sprint kept (somvr_vrmenu.c) */
static uint32_t us_vm_avail(void) { return g_us.want ? SOMVR_VMA_USE : 0u; }

static void us_end_log(void)
{
	logf_("USE END on=%d pick prompts kept %llu (refused '_Full' %llu), picks %llu (123 held %llu ticks), quest presses "
	      "%llu (%llu ticks; B's skips %llu, the death's screen %llu, under the grab %llu), binding tables logged %d", g_us.on,
	      (unsigned long long)g_us.kept,
	      (unsigned long long)g_us.refused_full, (unsigned long long)g_us.picks, (unsigned long long)g_us.pick_ticks,
	      (unsigned long long)g_us.quest_presses, (unsigned long long)g_us.quest_ticks, (unsigned long long)g_us.skips,
	      (unsigned long long)g_us.deaths, (unsigned long long)g_us.grabs, g_us.map_lines);
}
