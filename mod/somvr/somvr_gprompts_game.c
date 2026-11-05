/* somvr_gprompts_game.c - build 36 (docs/re-xr-pipeline.md 165): none of the flat game's button prompts in VR.
 * Included by somvr.c after the objective's file (it uses g_pp, g_go, g_ob, g_cp, logf_, env_word, mono).
 *
 * SOMVR_GAME_PROMPTS=off|on (code default off = no prompts; on = the game's, as build 35) - every place the flat game
 *   tells the player which pad button does what, in one switch. The VR side never sends most of those buttons, and a
 *   glyph floating in the world is not how a VR game says "you can do this" (HL:A: the hand, the object and the sound
 *   say it). Where the game's prompt told the player an action was possible, our own cue does it if one exists (the
 *   threat glow and the red warning for an attack, the grab glow, the execution's glow on the sword, the brand's glow).
 *   The parts (each its own module's hook; the hook already ran for its own feature):
 *   - the prompt diamond's prompts ("Stealth Drain" on the left trigger, "Subdue", "Grab Flurry", "(B) Pick Up",
 *     "Drop To Hang", the hold rings): every PromptShow dropped where the HUD takes it (PP_SHOW,
 *     somvr_prompts_game.c) after the VR side has read it (the verbs', the pick prompts', the strike's state: they
 *     still know what the game offers) - nothing of the game's state touched, its PromptHide passed as before;
 *   - the behaviour-state prompts (GO_PROMPT, somvr_gameopts_game.c; hooked with SOMVR_HINTS=log|off): their show dropped;
 *   - the world icons on the nav markers (OB_PLACE / OB_EACH, somvr_objective_game.c: the fire pits', the explosive
 *     nests', the caragor baits', the locked gates', the collectibles', the quest givers' icons - the game's
 *     wraith-vision icons - and the clip-only ones over orcs): not placed on screen, so FUN_00ec4a50 hides them as out
 *     of range (its own hide: shown bytes 0, the clip invisible) in our stereo frames; an edge arrow placed anyway is
 *     hidden after (OB_EACH, by its shown bytes); but for the
 *     objective (SOMVR_OBJECTIVE's own) and the records of SOMVR_GAME_PROMPTS_KEEP (default 'TrollHealthBar': the Graug's
 *     health bar is no prompt; a comma list, a trailing * = a prefix);
 *   - the counter glyph stays SOMVR_COUNTER_ICON's (its own VR menu row): its set-up's two Windows sites (CP_REC,
 *     CP_SHOW: win/sites-ui.tsv, som-b31-marks-vault) end the Y the Windows session still showed.
 *   The death struggle's ring and cursor stay (SOMVR_STRUGGLE steers them by the hand).
 * Lines: CONFIG game_prompts=..; GPROMPTS parts ...; GPROMPTS t=.. a world icon '...' hidden (the first of each record);
 *   GPROMPTS END. Word "gprompts on|off|state" (live; on = the game's prompts again from the next show). */

#define GP_NAV_MAX 32
static struct {
	int want_off;                     /* at start */
	char keep[512];                   /* SOMVR_GAME_PROMPTS_KEEP, and build 37's SOMVR_GAME_PROMPTS_ALLOW families */
	char allow[160];
	_Atomic uint64_t n[GP_PART_N];    /* dropped by part */
	struct { char name[48]; uint64_t n; double t_first; } nav[GP_NAV_MAX];
	int nnav;                         /* the game thread only (OB_EACH) */
	uint64_t nav_total, nav_other;
	uint32_t nav_lines;
	char placed[GP_NAV_MAX][48];      /* the records the game placed on screen while on (the first of each logged) */
	int nplaced;
} g_gpr;
static const char *const g_gp_w[] = { "on", "off" };

static void gp_count(int part)
{
	if (part >= 0 && part < GP_PART_N)
		atomic_fetch_add_explicit(&g_gpr.n[part], 1, memory_order_relaxed);
}

/* a world icon kept (SOMVR_GAME_PROMPTS_KEEP): no prompt */
static int gp_nav_keep(const char *name) { return g_gpr.keep[0] && somvr_ob_match(g_gpr.keep, name); }

/* OB_EACH (the game thread): a world icon the game placed was hidden - counted by its record, the first logged */
static void gp_nav_hid(const char *name, double t)
{
	g_gpr.nav_total++;
	for (int i = 0; i < g_gpr.nnav; i++)
		if (!strcmp(g_gpr.nav[i].name, name)) {
			g_gpr.nav[i].n++;
			return;
		}
	if (g_gpr.nnav >= GP_NAV_MAX) {
		g_gpr.nav_other++;
		return;
	}
	int i = g_gpr.nnav++;
	snprintf(g_gpr.nav[i].name, sizeof g_gpr.nav[i].name, "%s", name);
	g_gpr.nav[i].n = 1, g_gpr.nav[i].t_first = t;
	if (g_gpr.nav_lines++ < 64)
		logf_("GPROMPTS t=%.3f the game's world icon '%s' hidden (SOMVR_GAME_PROMPTS=off: the game's own hide in our "
		      "stereo frames)", t, name);
}

/* OB_PLACE (the game thread) with SOMVR_GAME_PROMPTS=on: a world icon the game placed on screen - the first of each
 * record logged (what the prompts' "on" shows: the names for the pictures) */
static void gp_nav_placed(const char *name, double t)
{
	for (int i = 0; i < g_gpr.nplaced; i++)
		if (!strcmp(g_gpr.placed[i], name))
			return;
	if (g_gpr.nplaced >= GP_NAV_MAX)
		return;
	snprintf(g_gpr.placed[g_gpr.nplaced++], sizeof g_gpr.placed[0], "%s", name);
	logf_("GPROMPTS t=%.3f the game places the world icon '%s' on screen (SOMVR_GAME_PROMPTS=on)", t, name);
}

static void gp_start(int *bad)
{
	(void)bad;
	memset(&g_gpr, 0, sizeof g_gpr);
	g_gpr.want_off = env_word("SOMVR_GAME_PROMPTS", g_gp_w, 2, 1);
	const char *k = getenv("SOMVR_GAME_PROMPTS_KEEP");
	snprintf(g_gpr.keep, sizeof g_gpr.keep, "%s", k ? (strcmp(k, "none") ? k : "") : "TrollHealthBar");
	/* build 37 (som-b37-cues, docs/re-xr-pipeline.md 169.8): SOMVR_GAME_PROMPTS_ALLOW=FAMILY[,FAMILY..] - whole families of
	 * the game's world icons back, one word each (their record patterns joined to the keep list):
	 *   awareness   an orc's awareness and alarm icons ('*Aware*', '*Alarm*': none came through OB_PLACE in builds 36-37 -
	 *               the triangles over orcs are drawn by another path and were never hidden; the family is here for the
	 *               day one is)
	 *   health      the health bars ('TrollHealthBar', 'OrcHealthBar')
	 *   captains    the Nemesis captains' icons ('Hierarchy*', 'OrcHealthBar')
	 *   collectibles  'Collectible_*', 'DLC1_Collectible_*', 'RessurrectionStone'
 *   herbs       'Collectible_Flower*' (the game's herb icon over each herb in reach of its range)
	 *   world       'Firepit', 'Explosive', 'Ammo', 'LockedGate', 'HangingBait', 'MountTarget*'
	 *   quests      'goldenM', '*QuestGiver*', 'powerStruggle', 'CharacterpowerStruggle', 'FT_*' */
	static const struct { const char *fam, *recs; } F[] = {
		{ "awareness", "*Aware*,*Alarm*" }, { "health", "TrollHealthBar,OrcHealthBar" },
		{ "captains", "Hierarchy*,OrcHealthBar" }, { "collectibles", "Collectible_*,DLC1_Collectible_*,RessurrectionStone" },
		{ "herbs", "Collectible_Flower*" },
		{ "world", "Firepit,Explosive,Ammo,LockedGate,HangingBait,MountTarget*" },
		{ "quests", "goldenM,*QuestGiver*,powerStruggle,CharacterpowerStruggle,FT_*" },
	};
	const char *al = getenv("SOMVR_GAME_PROMPTS_ALLOW");
	snprintf(g_gpr.allow, sizeof g_gpr.allow, "%s", al && strcmp(al, "none") ? al : "");
	for (size_t i = 0; i < sizeof F / sizeof F[0]; i++)
		if (somvr_cu_match(g_gpr.allow, F[i].fam)) { /* (the family's exact word in the list) */
			size_t l = strlen(g_gpr.keep);
			snprintf(g_gpr.keep + l, sizeof g_gpr.keep - l, "%s%s", l ? "," : "", F[i].recs);
		}
	atomic_store(&g_gp_off, g_gpr.want_off);
	logf_("CONFIG game_prompts=%s (SOMVR_GAME_PROMPTS: %s); world icons kept: %s (SOMVR_GAME_PROMPTS_KEEP + the families of "
	      "SOMVR_GAME_PROMPTS_ALLOW '%s')",
	      g_gp_w[g_gpr.want_off], g_gpr.want_off ? "none of the flat game's button prompts in VR - the prompt diamond's, "
	      "the behaviour prompts', the world icons'; the VR side still reads them" : "the game's prompts (build 35)",
	      g_gpr.keep[0] ? g_gpr.keep : "none", g_gpr.allow);
}

/* after every part's start: which are hooked (a part not hooked = the game's prompts there) */
static void gp_parts_line(void)
{
	logf_("GPROMPTS parts (SOMVR_GAME_PROMPTS=%s): the prompt diamond %s; behaviour prompts %s; world icons %s; the "
	      "counter glyph %s; the death struggle's ring and cursor kept (SOMVR_STRUGGLE)", g_gp_w[g_gpr.want_off],
	      g_pp.ok[PPS_SHOW] ? "hooked (PP_SHOW)" : "NOT hooked (PP_SHOW)",
	      g_go.hi_ok[GO_EV_PROMPT] ? "hooked (GO_PROMPT)" : "NOT hooked (GO_PROMPT: SOMVR_HINTS=game, or no site)",
	      g_ob.place_on ? "hooked (OB_PLACE, OB_EACH)" : g_ob.on ? "OB_EACH only (OB_PLACE not hooked: the clip-only icons stay)"
	      : "NOT hooked (OB_EACH)",
	      !g_cp.ok_mark ? "the game's (SOMVR_COUNTER_ICON=game or no site)"
	      : atomic_load(&g_cp.mode) != CP_HIDE ? "the game's (SOMVR_COUNTER_ICON)"
	      : g_cp.ok_setup && atomic_load(&g_cp.setup_skip) ? "hidden with its set-up (SOMVR_COUNTER_ICON=hide, _SETUP=skip)"
							       : "hidden, its set-up NOT (CP_REC/CP_SHOW)");
}

static void gp_state_line(const char *head)
{
	char top[512] = "";
	size_t o = 0;
	for (int i = 0; i < g_gpr.nnav && o + 64 < sizeof top; i++)
		o += (size_t)snprintf(top + o, sizeof top - o, "%s'%s' x%llu", o ? ", " : "", g_gpr.nav[i].name,
				      (unsigned long long)g_gpr.nav[i].n);
	logf_("%s t=%.3f: SOMVR_GAME_PROMPTS %s; dropped: diamond shows %llu, behaviour shows %llu, hold fills %llu; world "
	      "icons hidden %llu (by record: %s%s)", head, mono(), g_gp_w[atomic_load(&g_gp_off) ? 1 : 0],
	      (unsigned long long)atomic_load(&g_gpr.n[GP_PART_DIAMOND]), (unsigned long long)atomic_load(&g_gpr.n[GP_PART_BEHAVIOUR]),
	      (unsigned long long)atomic_load(&g_gpr.n[GP_PART_HOLD]), (unsigned long long)g_gpr.nav_total, o ? top : "none",
	      g_gpr.nav_other ? ", more" : "");
}

/* "gprompts on|off|state" (the writer thread) */
static const char *gp_word(const char *w)
{
	while (*w == ' ')
		w++;
	for (int i = 0; i < 2; i++)
		if (!strncmp(w, g_gp_w[i], strlen(g_gp_w[i]))) {
			atomic_store(&g_gp_off, i);
			logf_("GPROMPTS t=%.3f now %s (the word)", mono(), g_gp_w[i]);
			return i ? "gprompts off" : "gprompts on";
		}
	gp_state_line("GPROMPTS state");
	return "gprompts";
}

static void gp_end_log(void) { gp_state_line("GPROMPTS END"); }
