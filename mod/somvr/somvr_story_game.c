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
} g_sy;

static void sy_start(void)
{
	const char *v = getenv("SOMVR_STORY_LOG");
	g_sy.log = !(v && (!strcmp(v, "off") || !strcmp(v, "0")));
	logf_("CONFIG story_log=%s (SOMVR_STORY_LOG: the game's subtitle lines logged, read only)", g_sy.log ? "on" : "off");
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
