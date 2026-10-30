/* somvr_story_game.c - build 22 (docs/re-xr-pipeline.md 89, handoff/HANDOFF-story.md "build 22"): a story mission
 * played straight through in VR. Included by somvr.c after the session's file (it uses g_inp, peek, peek_ptr, logf_,
 * mono); sy_tick runs in the input tick (ss_input, every context: the world, a video, a menu).
 *
 * SOMVR_STORY_LOG=on|off (default on: read only, nothing patched, nothing changed) - the subtitle lines the game shows:
 *   CUIHUDSubtitle (the interface manager's im+0x27b0, re-ui-effects.md 6) keeps three channels of 0x210 bytes from
 *   +0xc0, each the line's text as FUN_00b39c40 copies it (wchar16, up to 256, cut at its '|' before the seconds) and
 *   its source pointer at +0x2c0 (a line without its '|' gets no terminator there: the tail of an older, longer line
 *   stays after it in the buffer - the log cuts the tail the last reading had at the same places); ShowSubtitle (FUN_00a10670) is called by the dialogue emitters, the narrator, the
 *   sound FX emitters and the video element (a story video's subtitles). Polled every 50 ms: `STORY t=.. subtitle
 *   ch=N 'TEXT' (ctx C, video V)` when a channel's text changes, `STORY t=.. subtitle ch=N cleared` when it empties.
 *   Word: `story` (a state line: the channels now). END line: `STORY END lines N, changes N, polls N`. */

/* Build 25 (re-xr-pipeline.md 103, som-b25-missions), a test aid only (SOMVR_HANDS_TEST=1): the word `story invisible
 * on|off` calls the game's own SetAISpectatorInvisible (FUN_027acae0(CPlayerObj*, bool): CPlayerObj+0xd10 = the flag, then
 * its vf_0x340; the script function 'SetInvisible' (FUN_027b2460) does the same for "invisible to ai") on every player in
 * the player list, from the input tick (the game's thread): the AI does not see Talion. For the drills only - to see a
 * mission's later steps headless where the harness's straight walks get Talion seen (the Sea Fort's six archers, 98.8);
 * never in a build's env. Lines `STORY t=.. invisible to the AI: on|off ...`; `story` shows the flag. */

/* STORY_SITES_BEGIN (a K_ENTRY called by the test word only). SY_INVIS FUN_027acae0(player, on): `mov [rdi+0xd10], sil;
 * call [rax+0x340]` (the bytes); its callers: the script binding SetAISpectatorInvisible FUN_023fe950, FUN_0268beb0,
 * FUN_0268ce40, FUN_02809630. Order = SYS_* */
static const struct site_spec g_sy_sites_game[] = {
SITE("SY_INVIS", K_ENTRY, 0x27acae0, 0x27acae0, "55 89 f5 53 48 89 fb 48 83 ec 08 48 8b 07 40 88", 0x27acae0, "55 89 f5 53 48 89 fb 48 83 ec 08 48 8b 07 40"),
};
/* STORY_SITES_END */
enum { SYS_INVIS, SYS_N };
_Static_assert(sizeof g_sy_sites_game / sizeof g_sy_sites_game[0] == SYS_N, "story sites");
typedef void (*sy_invis_fn)(uintptr_t player, int on);
#define SY_PLAYERS 0x4c47020u /* the player list (a std::vector of CPlayerObj*) */
#define SY_PL_INVIS 0xd10     /* CPlayerObj: invisible to the AI (SY_INVIS's bytes) */

#define SY_IM_SUB 0x27b0     /* CInterfaceMgr: the CUIHUDSubtitle* (m_GameUI+0xa8) */
#define SY_SUB_TEXT 0xc0     /* CUIHUDSubtitle: channel 0's text (wchar16[256]) */
#define SY_SUB_STRIDE 0x210  /* ... one channel */
#define SY_SUB_CHARS 256
#define SY_SUB_CH 3

static struct {
	int log;
	double t_poll;
	uint32_t h[SY_SUB_CH];   /* the text's hash per channel (0 = empty) */
	char text[SY_SUB_CH][384];
	uint16_t raw[SY_SUB_CH][SY_SUB_CHARS]; /* the channel's last text as read (the copy leaves an older, longer line's
						* tail after a line that had no '|': cut off for the log, see sy_tick) */
	uint64_t polls, changes;
	uint32_t lines;
	int test, invis_ok;      /* build 25: SOMVR_HANDS_TEST; SY_INVIS checked */
	_Atomic int req_invis;   /* `story invisible on|off`: 1 / 0 (-1: none) */
	int invis_calls;
} g_sy;

static void sy_start(void)
{
	const char *v = getenv("SOMVR_STORY_LOG");
	g_sy.log = !(v && (!strcmp(v, "off") || !strcmp(v, "0")));
	logf_("CONFIG story_log=%s (SOMVR_STORY_LOG: the game's subtitle lines logged, read only)", g_sy.log ? "on" : "off");
	const char *ht = getenv("SOMVR_HANDS_TEST");
	g_sy.test = ht && !strcmp(ht, "1");
	atomic_store(&g_sy.req_invis, -1);
	if (g_sy.test) { /* build 25: the test aid's one function */
		char why[400];
		g_sy.invis_ok = check_site(&g_sy_sites_game[SYS_INVIS], why, sizeof why) == 0;
		logf_("STORY site SY_INVIS %s %#lx: %s (the test word `story invisible on|off`)", g_sy.invis_ok ? "ok" : "MISMATCH",
		      (unsigned long)g_sy_sites_game[SYS_INVIS].addr, why);
	}
}

/* build 25: `story invisible on|off` on the game's thread (sy_tick) - every player in the list */
static void sy_invis_tick(double t)
{
	int on = atomic_exchange(&g_sy.req_invis, -1);
	if (on < 0)
		return;
	uintptr_t v[2] = { 0, 0 };
	if (!g_sy.invis_ok || !peek(SY_PLAYERS, v, sizeof v) || !v[0] || v[1] < v[0] || v[1] - v[0] > 8 * 8) {
		logf_("STORY t=%.3f invisible to the AI: %s not done (%s)", t, on ? "on" : "off",
		      g_sy.invis_ok ? "no player list" : "SY_INVIS did not check out");
		return;
	}
	for (uintptr_t a = v[0]; a < v[1]; a += 8) {
		uintptr_t pl = peek_ptr(a);
		uint8_t was = 0, now = 0;
		if (!pl || !peek(pl + SY_PL_INVIS, &was, 1))
			continue;
		((sy_invis_fn)g_sy_sites_game[SYS_INVIS].target)(pl, on);
		peek(pl + SY_PL_INVIS, &now, 1);
		g_sy.invis_calls++;
		logf_("STORY t=%.3f invisible to the AI: %s - player %#lx +0xd10 %u -> %u (a test aid: the game's "
		      "SetAISpectatorInvisible FUN_027acae0; the word)", t, on ? "on" : "off", (unsigned long)pl, was, now);
	}
}

/* wchar16 (BMP; a surrogate = '?') -> UTF-8, at most cap - 1 bytes; the hash of the code units (FNV-1a, 0 = empty) */
static uint32_t sy_utf8(const uint16_t *w, int n, char *out, size_t cap)
{
	uint32_t h = 2166136261u;
	size_t o = 0;
	int i = 0;
	for (; i < n && w[i] && w[i] < 0x3000; i++) { /* (a never-written buffer's garbage: CJK and up ends it) */
		unsigned c = w[i];
		h = (h ^ c) * 16777619u;
		if (c >= 0xd800 && c < 0xe000)
			c = '?';
		if (c < 0x80 && o + 1 < cap) {
			out[o++] = c < 0x20 ? ' ' : (char)c;
		} else if (c < 0x800 && o + 2 < cap) {
			out[o++] = (char)(0xc0 | c >> 6), out[o++] = (char)(0x80 | (c & 0x3f));
		} else if (c >= 0x800 && o + 3 < cap) {
			out[o++] = (char)(0xe0 | c >> 12), out[o++] = (char)(0x80 | (c >> 6 & 0x3f));
			out[o++] = (char)(0x80 | (c & 0x3f));
		}
	}
	out[o] = 0;
	return i ? (h ? h : 1) : 0;
}

static void sy_tick(double t)
{
	if (g_sy.test)
		sy_invis_tick(t);
	if (!g_sy.log || !g_inp.on || !g_inp.im || t - g_sy.t_poll < 0.05)
		return;
	g_sy.t_poll = t;
	uintptr_t sub = peek_ptr(g_inp.im + SY_IM_SUB);
	if (!sub)
		return;
	g_sy.polls++;
	for (int ch = 0; ch < SY_SUB_CH; ch++) {
		uint16_t w[SY_SUB_CHARS];
		if (!peek(sub + SY_SUB_TEXT + (uintptr_t)ch * SY_SUB_STRIDE, w, sizeof w))
			continue;
		char s[sizeof g_sy.text[0]];
		uint32_t h = sy_utf8(w, SY_SUB_CHARS, s, sizeof s);
		if (h == g_sy.h[ch])
			continue;
		int len = 0, k;
		while (len < SY_SUB_CHARS && w[len])
			len++;
		for (k = len; k > 0 && w[k - 1] == g_sy.raw[ch][k - 1]; k--)
			;
		memcpy(g_sy.raw[ch], w, sizeof w);
		if (len - k >= 3 && k > 1) /* an older line's tail at the same places: the new line ends at k (the whole line
					      * matching = the same line again: kept) */
			sy_utf8(w, k, s, sizeof s);
		g_sy.h[ch] = h, g_sy.changes++;
		memcpy(g_sy.text[ch], s, sizeof s);
		if (g_sy.lines++ < 600) {
			if (h)
				logf_("STORY t=%.3f subtitle ch=%d '%s' (ctx %s, video %d, paused %d)", t, ch, s, ss_ctx_name(g_inp.ctx),
				      g_inp.video, g_inp.paused);
			else
				logf_("STORY t=%.3f subtitle ch=%d cleared", t, ch);
		}
	}
}

/* the word "story" (the writer thread): a state line; NULL = not ours */
static const char *sy_word(const char *w)
{
	if (strncmp(w, "story", 5) || (w[5] && w[5] != ' ' && w[5] != '\n'))
		return NULL;
	if (!strncmp(w + 5, " invisible", 10)) { /* build 25: a test aid */
		const char *a = w + 15;
		while (*a == ' ')
			a++;
		int on = !strncmp(a, "on", 2) ? 1 : !strncmp(a, "off", 3) ? 0 : -1;
		if (!g_sy.test || on < 0)
			return "story invisible (SOMVR_HANDS_TEST off, or not on|off: ignored)";
		atomic_store(&g_sy.req_invis, on);
		return on ? "story invisible on (a test aid)" : "story invisible off (a test aid)";
	}
	uint8_t inv = 0xff;
	uintptr_t v0 = 0;
	if (peek(SY_PLAYERS, &v0, sizeof v0) && v0) {
		uintptr_t pl = peek_ptr(v0);
		if (pl)
			peek(pl + SY_PL_INVIS, &inv, 1);
	}
	logf_("STORY status: the first player's invisible-to-the-AI flag %u (255: no player); test aid calls %d", inv,
	      g_sy.invis_calls);
	logf_("STORY status: log %s, im %#lx, the channels: '%s' | '%s' | '%s'; changes %llu, polls %llu", g_sy.log ? "on" : "off",
	      (unsigned long)g_inp.im, g_sy.text[0], g_sy.text[1], g_sy.text[2], (unsigned long long)g_sy.changes,
	      (unsigned long long)g_sy.polls);
	return "story";
}

static void sy_end_log(void)
{
	logf_("STORY END lines %u, changes %llu, polls %llu", g_sy.lines, (unsigned long long)g_sy.changes,
	      (unsigned long long)g_sy.polls);
}
