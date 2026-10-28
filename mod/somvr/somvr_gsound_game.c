/* somvr_gsound_game.c - build 20 (docs/re-xr-pipeline.md 75): the game's own sword sounds on Talion. Included by
 * somvr.c before the quiver's file (its bs_post asks gs_pre / gs_after; this file uses peek, peek_ptr, logf_, mono,
 * env_word, bs_install_log and the slots' g_sl).
 *
 * SOMVR_SLOTS_GAMESOUND=game|log|drop (default game = nothing patched) - with SOMVR_SLOTS the sword's draw and put-back
 *   sounds are ours (fol_cmn_unsheathe / fol_cmn_sheathe at the hand on our own Wwise object, SOMVR_SLOTS_SOUND), when
 *   the player's hand draws or puts it back. The game plays Talion's own whenever it draws or sheathes his sword by
 *   itself - its draw on the run at an orc (~10 m, the orc unaware) while the player keeps the sword on the back, its
 *   sheathe ~1.3 s after the player's draw out of a fight while the sword stays in the hand (build 19b's session, marks
 *   6 and 10): the animation's sound key 'fol_ply_cbt_swd_unsheath' / 'fol_ply_cbt_swd_sheath' comes 0.05-0.4 s before
 *   its UNSHEATH / SHEATH key, which the slots refuse or answer - so the sword stays where the hand left it, but the
 *   sound was heard (run 20261028-110803); the sheathe starts with Talion's flourish 'fol_ply_cbt_swd_twirl' 0.75 s
 *   before its own sound - 0.2 s after the player's draw out of a fight (run 20261028-112035). Every per-object post of the game goes CSoundMgr::vf_12(mgr, X, name) ->
 *   X's emitter (X+0x68, FUN_008f00e0) -> FUN_008ecd60 -> FUN_008c9870(wrap, emitter+0x14, name, cookie) -> AK's
 *   PostEvent by name: BS_POST_B (SOMVR_BOW_SNDLOG's patch, shared: its callback 0x8c8ba0, the cookie a
 *   WBWeakRef<CSoundEmitter> whose +0x18 = the emitter's +0x78, its position source); ours go BS_POST_A (no callback).
 *   log: the game's posts of the listed names and of every name with "sheath" or "swd" logged, all passed (= build 19b);
 *   drop: the names of SOMVR_SLOTS_GAMESOUND_NAMES (default fol_ply_cbt_swd_unsheath,fol_ply_cbt_swd_sheath,
 *   fol_ply_cbt_swd_twirl: Talion's own, "ply" = the player's; the orcs' draws are fol_cmn_*) posted by the game (a
 *   Talion animation's sword, not the hand's) are not posted (AK's "no playing id",
 *   0) while the slots step runs with the player read (within GS_FRESH_MS: not in a cutscene, a load or the fallback's
 *   flat frames, which keep the game's sounds); ours and every other sound untouched.
 * Lines: CONFIG slots gamesound=..; SLSND t=.. (the first GS_LINES); SLSND END. Trigger word "slsnd log|drop|state"
 * (live; only with the post patched: boot with log or drop). */

static int bs_install_log(void); /* somvr_quiver_game.c (included after this file) */

#define GS_CK_SRC 0x18    /* the post's cookie (FUN_008ecd60's WBWeakRef<CSoundEmitter>): the emitter's +0x78 */
#define GS_CB 0x8c8ba0    /* BS_POST_B's callback: a game object's post (path A, ours and the global ones, has none) */
#define GS_FRESH_MS 1000  /* a slots step this recently = the slots own the sword (SL_GAP_S) */
#define GS_NAMES_MAX 8
#define GS_NAME_LEN 48
#define GS_LINES 400
#define GS_NAMES_DEFAULT "fol_ply_cbt_swd_unsheath,fol_ply_cbt_swd_sheath,fol_ply_cbt_swd_twirl"
enum { GS_GAME, GS_LOG, GS_DROP };
static const char *const g_gs_mode_w[] = { "game", "log", "drop" };

static struct {
	int want, patched;
	_Atomic int mode;
	char names[GS_NAMES_MAX][GS_NAME_LEN];
	int nnames;
	_Atomic int64_t step_ms;   /* the last slots step with the player read (mono ms; the game thread writes) */
	_Atomic uint64_t posts, named, dropped, stale, swd;
	_Atomic uint32_t lines;
} g_gs;

/* one post as gs_pre saw it (bs_post's stack) */
struct gs_post {
	int log, listed;           /* listed: 0 no, 1 passed (log), 2 passed (no slots step), 3 dropped */
	uint64_t obj;
	uintptr_t src;
	char name[GS_NAME_LEN];
};

static int gs_listed(const char *n)
{
	for (int i = 0; i < g_gs.nnames; i++)
		if (!strcmp(g_gs.names[i], n))
			return 1;
	return 0;
}

/* "a,b,c" -> the name list; the count (0 = none) */
static int gs_parse_names(const char *s)
{
	g_gs.nnames = 0;
	while (s && *s && g_gs.nnames < GS_NAMES_MAX) {
		size_t k = strcspn(s, ", ");
		if (k && k < GS_NAME_LEN) {
			memcpy(g_gs.names[g_gs.nnames], s, k);
			g_gs.names[g_gs.nnames++][k] = 0;
		}
		s += k;
		s += strspn(s, ", ");
	}
	return g_gs.nnames;
}

/* the event's name (a game string): one read, else byte by byte near the end of a mapping */
static void gs_name(const char *name, char *out)
{
	out[0] = 0;
	if (!name)
		return;
	if (peek((uintptr_t)name, out, GS_NAME_LEN - 1)) {
		out[GS_NAME_LEN - 1] = 0;
		return;
	}
	size_t k = 0;
	for (; k + 1 < GS_NAME_LEN && peek((uintptr_t)name + k, &out[k], 1) && out[k]; k++)
		;
	out[k] = 0;
}

/* bs_post, before the game's post (any thread): 1 = not posted (a listed name of the game's in drop mode) */
static int gs_pre(struct gs_post *p, const char *name, uint64_t obj, void *cb, void *cookie)
{
	p->log = 0, p->listed = 0, p->obj = obj, p->src = 0, p->name[0] = 0;
	int mode = atomic_load_explicit(&g_gs.mode, memory_order_relaxed);
	if (mode == GS_GAME || (uintptr_t)cb != GS_CB)
		return 0;
	atomic_fetch_add_explicit(&g_gs.posts, 1, memory_order_relaxed);
	gs_name(name, p->name);
	p->listed = gs_listed(p->name);
	if (!p->listed) {
		if (mode == GS_LOG && (strstr(p->name, "sheath") || strstr(p->name, "swd"))) {
			atomic_fetch_add_explicit(&g_gs.swd, 1, memory_order_relaxed);
			p->log = 1, p->src = cookie ? peek_ptr((uintptr_t)cookie + GS_CK_SRC) : 0;
		}
		return 0;
	}
	atomic_fetch_add_explicit(&g_gs.named, 1, memory_order_relaxed);
	p->log = 1, p->src = cookie ? peek_ptr((uintptr_t)cookie + GS_CK_SRC) : 0;
	if (mode != GS_DROP)
		return 0;
	int64_t now = (int64_t)(mono() * 1e3), step = atomic_load_explicit(&g_gs.step_ms, memory_order_relaxed);
	if (!step || now - step > GS_FRESH_MS) {
		atomic_fetch_add_explicit(&g_gs.stale, 1, memory_order_relaxed);
		p->listed = 2; /* passed: no slots step (a cutscene, a load, flat frames) */
		return 0;
	}
	atomic_fetch_add_explicit(&g_gs.dropped, 1, memory_order_relaxed);
	p->listed = 3;
	return 1;
}

/* bs_post, after (or instead of) the game's post: the line (playing = AK's id; 0 for a dropped one) */
static void gs_after(const struct gs_post *p, uint32_t playing)
{
	if (!p->log || atomic_fetch_add_explicit(&g_gs.lines, 1, memory_order_relaxed) >= GS_LINES)
		return;
	const char *what = p->listed == 3 ? "DROPPED (the game's own sword sound: no hand drew or put back)"
			 : p->listed == 2 ? "passed (no slots step within 1 s)"
			 : p->listed == 1 ? "passed (SOMVR_SLOTS_GAMESOUND=log)" : "passed (not listed)";
	logf_("SLSND t=%.3f '%s' obj %llu (source %#lx): %s -> playing %u", mono(), p->name, (unsigned long long)p->obj,
	      (unsigned long)p->src, what, playing);
}

/* sl_frame (the game thread): a slots step with the player read (g) or without (NULL) */
static void gs_frame(const struct sl_game *g)
{
	if (g_gs.patched)
		atomic_store_explicit(&g_gs.step_ms, g && g->own ? (int64_t)(mono() * 1e3) : 0, memory_order_relaxed);
}

static void gs_state_line(const char *head)
{
	logf_("%s t=%.3f: SOMVR_SLOTS_GAMESOUND %s (the post %s); the game objects' posts %llu; the listed names %llu: "
	      "dropped %llu, passed with no slots step %llu; other sword sounds logged %llu", head, mono(),
	      g_gs_mode_w[atomic_load(&g_gs.mode)], g_gs.patched ? "patched" : "NOT patched",
	      (unsigned long long)atomic_load(&g_gs.posts), (unsigned long long)atomic_load(&g_gs.named),
	      (unsigned long long)atomic_load(&g_gs.dropped), (unsigned long long)atomic_load(&g_gs.stale),
	      (unsigned long long)atomic_load(&g_gs.swd));
}

/* "slsnd log|drop|state" (the writer thread) */
static const char *gs_word(const char *w0)
{
	char w[32];
	snprintf(w, sizeof w, "%s", w0);
	for (size_t n = strlen(w); n && (w[n - 1] == '\n' || w[n - 1] == ' ' || w[n - 1] == '\r');)
		w[--n] = 0;
	if (!strcmp(w, "slsnd") || !strcmp(w, "slsnd state")) {
		gs_state_line("SLSND state");
		return "slsnd (a state line)";
	}
	int m = !strcmp(w, "slsnd log") ? GS_LOG : !strcmp(w, "slsnd drop") ? GS_DROP : -1;
	if (m < 0)
		return "slsnd (not log|drop|state: ignored)";
	if (!g_gs.patched)
		return "slsnd (the post is not patched: boot with SOMVR_SLOTS_GAMESOUND=log|drop)";
	atomic_store(&g_gs.mode, m);
	logf_("SLSND t=%.3f the word: %s from the next post", mono(), g_gs_mode_w[m]);
	return "slsnd";
}

/* after sl_start (needs SOMVR_SLOTS; the post's patch is SOMVR_BOW_SNDLOG's, shared) */
static void gs_start(int *bad)
{
	memset(&g_gs, 0, sizeof g_gs);
	g_gs.want = env_word("SOMVR_SLOTS_GAMESOUND", g_gs_mode_w, 3, GS_GAME);
	const char *nv = getenv("SOMVR_SLOTS_GAMESOUND_NAMES");
	if (!gs_parse_names(nv && nv[0] ? nv : GS_NAMES_DEFAULT)) {
		logf_("WARN SOMVR_SLOTS_GAMESOUND_NAMES='%s' names nothing: the defaults", nv);
		(*bad)++;
		gs_parse_names(GS_NAMES_DEFAULT);
	}
	if (g_gs.want == GS_GAME)
		return;
	if (!g_sl.on) {
		logf_("CONFIG slots gamesound=%s (SOMVR_SLOTS_GAMESOUND): off - SOMVR_SLOTS is off (the game's sword sounds are "
		      "the only ones)", g_gs_mode_w[g_gs.want]);
		return;
	}
	g_gs.patched = bs_install_log() == 2;
	atomic_store(&g_gs.mode, g_gs.patched ? g_gs.want : GS_GAME);
	char nm[GS_NAMES_MAX * GS_NAME_LEN] = "";
	for (int i = 0, o = 0; i < g_gs.nnames; i++)
		o += snprintf(nm + o, sizeof nm - (size_t)o, "%s%s", i ? "," : "", g_gs.names[i]);
	logf_("CONFIG slots gamesound=%s (SOMVR_SLOTS_GAMESOUND: the game's own %s (Talion's sword drawn or sheathed by the "
	      "game) %s while the slots run; ours at the hand stay): the post %s", g_gs_mode_w[g_gs.want], nm,
	      g_gs.want == GS_DROP ? "not posted" : "logged", g_gs.patched ? "patched (BS_POST_A/B)" : "NOT patched: off");
}

static void gs_end_log(void)
{
	if (g_gs.patched)
		gs_state_line("SLSND END");
}
