/* somvr_gameopts_game.c - build 10 (docs/re-xr-pipeline.md 34): two of the flat game's own options, set from VR.
 * Included by somvr.c after the minimap's file (it uses the hooks' helpers: check_site, write_code, peek, logf_).
 *
 * SOMVR_MUSIC=0..1 (unset = off: nothing patched) - the music's volume, the VR menu's Music row (settings.conf MUSIC=).
 *   The game's Sound options apply their sliders (0..1, GameOptions +0x18c master, +0x190 music, +0x188 sfx, +0x194
 *   dialogue, +0x198 controller) as global Wwise RTPCs: FUN_00cbe980 (at load, the menu's Apply) and each slider's
 *   change callback call the sound manager's slot 22 (CSoundMgr::vf_22 0x8ed760 = SetRTPC(name, value in xmm0) on the
 *   global game object: FUN_008c9980(&DAT_0466df00, -1, name, value)) with "options_music" etc. GO_RTPC is that slot
 *   (CSoundMgr's vtable 0x346da18, the AUDIO hook's slot 23 next to it): our wrapper passes every call on, the
 *   "options_music" one at the VR menu's share of the game's value. The game's options, its profile and the save are
 *   never written: the game's music slider still works (the share is of its value); the sound effects, the dialogue
 *   and the master volume pass unchanged. A menu change applies at once: the original slot with the game's last value
 *   (or GameOptions +0x190 read through FUN_00c818c0 before the game set it while we watched) x the share, on the
 *   sound manager from DAT_0466afa8's vf_0x68 (GO_APPLY's bytes prove both: the global and the +0x190 load before the
 *   "options_music" call).
 *
 * SOMVR_HINTS=game|log|off (default game = nothing patched) - the flat game's button hints ("press L, T and A to strike
 *   the enemy", session 2026-10-24 mark 9). The HUD's text manager (CUIHUDTextManager) takes them as three events, each
 *   through a TypedDelegate whose slot 2 is the call: GO_HINT = CCharacterProxy's CombatHintParams (OnHintEvent
 *   0xb3cc30: params {record, show byte at +8}; the game's own "Display Combat Hints" option, GameOptions +0x1ff, is
 *   read there and drops a hint when 0), GO_TUTORIAL = CPlayerUI's TutorialParams (CB_OnTutorial 0xb3cd10: {show byte,
 *   record at +8}), GO_PROMPT = BehaviorStatePromptParams (CB_OnBehaviorPrompt 0xb3ccd0: {show byte, record at +8}).
 *   log: every event logged with its record's name (DBRecord_GetName, GO_RECNAME), passed on; off: a combat hint's or
 *   a tutorial's "show" is dropped at that point - what the game does with Display Combat Hints off, at the source, no
 *   picture cut - its "hide" passed on; behaviour prompts pass (logged: the HUD band's prompts are another element).
 *
 * SOMVR_VISION=hold|tap (default unset = build 9's: Y held sends VisionModeToggle 243) - the wraith vision on Y. Run
 *   181931: Y held 6 s sent 243 every tick (`VIN press cmd=243 VisionModeToggle ... event=1`) and the vision never came
 *   on (PlayerVisionMode +0x48 stayed 0, the world unchanged in the dumps): 243 is unbound in the world set
 *   (runtime-probes.md run 3); the game's wraith vision is PC_WraithPing = `CombatSecondary01` 122 (pad LB / V), **a
 *   toggle** (P9), held 0.85 s it is PC_Radiance. hold: Y's press taps 122 (2 input ticks, the step module's tap) when
 *   the vision is off, Y's release taps it again when it is still on and was ours - the vision while Y is held; tap: each
 *   press toggles. 243 is no longer sent for Y. The flag is read as the step module reads it (st_vision); unreadable: the
 *   taps go on press and release regardless. 122 is on the deny list: our tap passes as the phase's does (allow_cmd).
 *
 * Trigger words: "gameopts" (a state line), "hints game|log|off" (live). Fail soft: a site mismatch = that part off,
 * nothing patched. */

/* GAMEOPTS_SITES_BEGIN (check_sites.py counts this block on its own. Patched with SOMVR_MUSIC: GO_RTPC = CSoundMgr's
 * slot 22 (the context slots 21 and 22 only: SOMVR_AUDIO=head has already written slot 23 when go_start checks it,
 * headless run 175859; AUDIO's own context covers slot 22, so AUDIO installs first, in install_all, before go_start);
 * with SOMVR_HINTS=log|off: GO_HINT / GO_TUTORIAL /
 * GO_PROMPT = slot 2 of the three CUIHUDTextManager delegates' vtables (the context: slots 0..3). Read only: GO_OPTIONS
 * = FUN_00c818c0 (the GameOptions object), GO_APPLY = FUN_00cbe980's start (the sound manager from DAT_0466afa8's
 * vf_0x68, master +0x18c, music +0x190 with "options_music" at 0x353598b, both through slot 0xb0), GO_RECNAME =
 * DBRecord_GetName. Order = GOS_*) */
static const struct site_spec g_go_sites_game[] = {
SITE("GO_RTPC", K_VSLOT, 0x346dac8, 0x346dac0, "70 07 8f 00 00 00 00 00 60 d7 8e 00 00 00 00 00", 0x8ed760, "0f b6 47 08 83 f0 01 48 85 f6 0f 94 c2 08 d0 74"),
SITE("GO_OPTIONS", K_ENTRY, 0xc818c0, 0xc818c0, "48 83 ec 08 48 8b 05 95 7c a2 03 48 8d 78 10 e8", 0xc818c0, "48 83 ec 08 48 8b 05 95 7c a2 03 48 8d 78 10"),
SITE("GO_APPLY", K_ENTRY, 0xcbe980, 0xcbe980, "41 54 55 53 48 89 fb 48 8b 3d 1a c6 9a 03 48 8b 07 ff 50 68 f3 0f 10 83 8c 01 00 00 be 7c 59 53 03 48 8b 10 48 89 c7 ff 92 b0 00 00 00 48 8b 3d f4 c5 9a 03 48 8b 07 ff 50 68 f3 0f 10 83 90 01 00 00 be 8b 59 53 03 48 8b 10 48 89 c7 ff 92 b0 00 00 00", 0xcbe980, "41 54 55 53 48 89 fb 48 8b 3d 1a c6 9a 03 48 8b"),
SITE("GO_HINT", K_VSLOT, 0x351ce10, 0x351ce00, "20 c7 b3 00 00 00 00 00 60 c9 b3 00 00 00 00 00 30 cc b3 00 00 00 00 00 20 c8 b3 00 00 00 00 00", 0xb3cc30, "55 53 48 83 ec 08 48 8b 5f 18 48 85 db 74 52 80"),
SITE("GO_TUTORIAL", K_VSLOT, 0x351cd50, 0x351cd40, "20 c6 b3 00 00 00 00 00 b0 c9 b3 00 00 00 00 00 10 cd b3 00 00 00 00 00 20 c9 b3 00 00 00 00 00", 0xb3cd10, "4c 8b 1d 09 97 02 04 48 8b 4a 08 41 54 55 53 41"),
SITE("GO_PROMPT", K_VSLOT, 0x351cde0, 0x351cdd0, "e0 c6 b3 00 00 00 00 00 a0 ca b3 00 00 00 00 00 d0 cc b3 00 00 00 00 00 60 c8 b3 00 00 00 00 00", 0xb3ccd0, "48 8b 47 18 48 85 c0 74 27 80 3a 00 48 8b b8 e8"),
SITE("GO_RECNAME", K_ENTRY, 0x25aa530, 0x25aa530, "48 85 ff 74 1b f6 47 01 02 74 15 48 8b 47 20 ba", 0x25aa530, "48 85 ff 74 1b f6 47 01 02 74 15 48 8b 47 20"),
};
/* GAMEOPTS_SITES_END */
enum { GOS_RTPC, GOS_OPTIONS, GOS_APPLY, GOS_HINT, GOS_TUTORIAL, GOS_PROMPT, GOS_RECNAME, GOS_N };
_Static_assert(sizeof g_go_sites_game / sizeof g_go_sites_game[0] == GOS_N, "gameopts sites");
static const struct site_spec *g_go_sites = g_go_sites_game; /* selftest: stand-ins */
/* the game's objects (selftest: stand-ins): the sound manager's holder (GO_APPLY's `mov rdi, [0x466afa8]`), the
 * GameOptions music float (GO_APPLY's `movss xmm0, [rbx+0x190]`, the music slider's init FUN_00bd92b0) */
static struct { uintptr_t snd_pp; uintptr_t opt_music; } g_gog = { 0x466afa8, 0x190 };
#define GO_SND_GET 0x68     /* the holder's vf: the sound manager */
#define GO_MUSIC "options_music"
#define GO_SLOT_RTPC 0xb0   /* the sound manager's SetRTPC (slot 22) */

enum { GO_HINTS_GAME, GO_HINTS_LOG, GO_HINTS_OFF };
static const char *const g_go_hint_w[] = { "game", "log", "off" };
enum { GO_EV_HINT, GO_EV_TUTORIAL, GO_EV_PROMPT, GO_EV_N };
static const char *const g_go_ev_name[GO_EV_N] = { "combat hint", "tutorial", "behaviour prompt" };

typedef uint64_t (*go_rtpc_fn)(void *snd, const char *name, float v);
typedef void (*go_dlg_fn)(void *dlg, void *sender, void *params);
typedef const char *(*go_name_fn)(uintptr_t rec);
typedef uintptr_t (*go_opts_fn)(void);
typedef void *(*go_sndget_fn)(void *holder);

static struct {
	int mu_on;                   /* SOMVR_MUSIC set and GO_RTPC installed */
	int mu_want;                 /* SOMVR_MUSIC was set */
	float mu_env;                /* its value (the menu's start value) */
	_Atomic uint32_t mu_share;   /* the share now (float bits): the wrapper's factor */
	_Atomic uint32_t mu_game;    /* the game's last options_music (float bits), valid with mu_have */
	_Atomic int mu_have;
	_Atomic uintptr_t mu_snd;    /* the sound manager the game called it on */
	uintptr_t mu_orig;           /* the slot's original (CSoundMgr::vf_22) */
	int ok_apply, ok_opts;       /* GO_APPLY (the holder, +0x190), GO_OPTIONS matched */
	int mu_dirty;                /* the game thread: apply the share now (a menu change, the start) */
	float mu_applied;            /* the value we last sent (logs) */
	_Atomic uint64_t n_rtpc, n_music, n_ours, n_other_opts;
	_Atomic uint32_t mu_lines, other_lines;
	_Atomic int hint_mode;       /* GO_HINTS_* (the word changes it live) */
	int hint_want;               /* SOMVR_HINTS asked hooks (log|off) */
	int hi_ok[GO_EV_N];          /* that delegate's slot installed */
	uintptr_t hi_orig[GO_EV_N];
	int ok_name;
	_Atomic uint64_t n_ev[GO_EV_N], n_show[GO_EV_N], n_drop[GO_EV_N];
	_Atomic uint32_t hint_lines;
	double next_tick;
	uint32_t tick_lines;
	int vis_mode;                /* SOMVR_VISION: 0 off (build 9's 243), 1 hold, 2 tap */
	int vis_y, vis_tap, vis_ours, vis_asked; /* Y held last tick; tap ticks left; the vision is ours; 1/-1 asked */
	double vis_t;
	uint64_t n_vis_press, n_vis_tap, n_vis_refused;
	uint32_t vis_lines;
} g_go;

static float go_f(_Atomic uint32_t *a) { uint32_t b = atomic_load_explicit(a, memory_order_relaxed); float f; memcpy(&f, &b, 4); return f; }
static void go_setf(_Atomic uint32_t *a, float f) { uint32_t b; memcpy(&b, &f, 4); atomic_store_explicit(a, b, memory_order_relaxed); }

/* a game string for a log (at most 63 characters, readable or "?") */
static void go_str(const char *s, char *buf, size_t cap)
{
	size_t n = 0;
	buf[0] = 0;
	if (!s || !cap)
		return;
	for (; n + 1 < cap && n < 63; n++) {
		char c;
		if (!peek((uintptr_t)(s + n), &c, 1)) {
			snprintf(buf, cap, "?");
			return;
		}
		if (!c)
			break;
		buf[n] = c >= 32 && c < 127 ? c : '.';
	}
	buf[n] = 0;
}

/* GO_RTPC (any thread that sets a global RTPC by name): the game's options_music at the menu's share */
__attribute__((used)) uint64_t somvr_w_gortpc(void *snd, const char *name, float v)
{
	atomic_fetch_add_explicit(&g_go.n_rtpc, 1, memory_order_relaxed);
	/* the name read as the original reads it (a literal of the caller's): no syscall on this busy path */
	int opt = name && name[0] == 'o' && !strncmp(name, "options_", 8), music = opt && !strcmp(name + 8, GO_MUSIC + 8);
	if (!music) {
		if (opt) {
			atomic_fetch_add_explicit(&g_go.n_other_opts, 1, memory_order_relaxed);
			if (atomic_fetch_add_explicit(&g_go.other_lines, 1, memory_order_relaxed) < 12) {
				char nm[40];
				go_str(name, nm, sizeof nm);
				logf_("MUSIC the game sets %s %.3f: passed unchanged", nm, v);
			}
		}
		return ((go_rtpc_fn)g_go.mu_orig)(snd, name, v);
	}
	float share = go_f(&g_go.mu_share), out = isfinite(v) ? v * share : v;
	go_setf(&g_go.mu_game, v);
	atomic_store_explicit(&g_go.mu_snd, (uintptr_t)snd, memory_order_relaxed);
	atomic_store_explicit(&g_go.mu_have, 1, memory_order_release);
	uint64_t n = atomic_fetch_add_explicit(&g_go.n_music, 1, memory_order_relaxed) + 1;
	if (atomic_fetch_add_explicit(&g_go.mu_lines, 1, memory_order_relaxed) < 40)
		logf_("MUSIC the game sets options_music %.3f (#%llu): %.3f sent (the VR menu's Music %.0f%%)", v,
		      (unsigned long long)n, out, share * 100);
	return ((go_rtpc_fn)g_go.mu_orig)(snd, name, out);
}

/* a record's name for a log ("" when there is none or it is unreadable) */
static void go_rec_name(uintptr_t rec, char *buf, size_t cap)
{
	unsigned char b[0x28];
	buf[0] = 0;
	if (!g_go.ok_name || !rec || !peek(rec, b, sizeof b))
		return;
	go_str(((go_name_fn)g_go_sites[GOS_RECNAME].target)(rec), buf, cap);
}

/* the three delegates' slot 2 (the game thread): log, drop a hint's or a tutorial's show with SOMVR_HINTS=off */
static void go_dlg(int ev, void *dlg, void *sender, void *params)
{
	unsigned char p[16];
	int ok = params && peek((uintptr_t)params, p, sizeof p);
	uintptr_t rec = 0;
	int show = 0;
	if (ok) { /* the hint: {record, show at +8}; the tutorial and the prompt: {show, record at +8} */
		memcpy(&rec, ev == GO_EV_HINT ? p : p + 8, 8);
		show = (ev == GO_EV_HINT ? p[8] : p[0]) != 0;
	}
	int mode = atomic_load_explicit(&g_go.hint_mode, memory_order_relaxed);
	int drop = ok && show && mode == GO_HINTS_OFF && ev != GO_EV_PROMPT;
	uint64_t n = atomic_fetch_add_explicit(&g_go.n_ev[ev], 1, memory_order_relaxed) + 1;
	atomic_fetch_add_explicit(&g_go.n_show[ev], (uint64_t)show, memory_order_relaxed);
	atomic_fetch_add_explicit(&g_go.n_drop[ev], (uint64_t)drop, memory_order_relaxed);
	if (mode != GO_HINTS_GAME && atomic_fetch_add_explicit(&g_go.hint_lines, 1, memory_order_relaxed) < 300) {
		char nm[64];
		go_rec_name(rec, nm, sizeof nm);
		logf_("HINTS %s #%llu t=%.3f: %s '%s' (record %#lx)%s", g_go_ev_name[ev], (unsigned long long)n, mono(),
		      !ok ? "unreadable" : show ? "show" : "hide", nm, (unsigned long)rec,
		      drop ? ": dropped (SOMVR_HINTS=off: what the game's Display Combat Hints off does)" : ": passed on");
	}
	if (!drop)
		((go_dlg_fn)g_go.hi_orig[ev])(dlg, sender, params);
}
__attribute__((used)) void somvr_w_gohint(void *d, void *s, void *p) { go_dlg(GO_EV_HINT, d, s, p); }
__attribute__((used)) void somvr_w_gotut(void *d, void *s, void *p) { go_dlg(GO_EV_TUTORIAL, d, s, p); }
__attribute__((used)) void somvr_w_goprompt(void *d, void *s, void *p) { go_dlg(GO_EV_PROMPT, d, s, p); }

/* the game's music value (0..1) and the sound manager for our own call; 0 = not known yet */
static int go_music_target(float *game, void **snd, const char **from)
{
	if (atomic_load_explicit(&g_go.mu_have, memory_order_acquire)) {
		*game = go_f(&g_go.mu_game), *snd = (void *)atomic_load(&g_go.mu_snd), *from = "its last options_music";
		return *snd != NULL;
	}
	if (!g_go.ok_apply || !g_go.ok_opts)
		return 0;
	uintptr_t o = ((go_opts_fn)g_go_sites[GOS_OPTIONS].target)(), holder = peek_ptr(g_gog.snd_pp), vt;
	float v;
	if (!o || !holder || !peek(o + g_gog.opt_music, &v, 4) || !isfinite(v) || !(vt = peek_ptr(holder)) ||
	    !peek_ptr(vt + GO_SND_GET))
		return 0;
	void *s = ((go_sndget_fn)peek_ptr(vt + GO_SND_GET))((void *)holder);
	/* the sound manager is the class whose slot GO_RTPC is (its vtable = the slot - 0xb0) */
	if (!s || peek_ptr((uintptr_t)s) != g_go_sites[GOS_RTPC].addr - GO_SLOT_RTPC)
		return 0;
	*game = v, *snd = s, *from = "GameOptions +0x190";
	return 1;
}

/* the input tick (game thread): a menu change or the start's share applied once the game's value is known */
static void go_tick(double t)
{
	if (!g_go.mu_on || !g_go.mu_dirty || t < g_go.next_tick)
		return;
	g_go.next_tick = t + 0.25;
	float game, share = go_f(&g_go.mu_share);
	void *snd;
	const char *from;
	if (!go_music_target(&game, &snd, &from)) {
		if (g_go.tick_lines++ < 3)
			logf_("MUSIC the game's music volume is not known yet: the VR menu's %.0f%% waits", share * 100);
		return;
	}
	g_go.mu_dirty = 0;
	g_go.mu_applied = game * share;
	atomic_fetch_add_explicit(&g_go.n_ours, 1, memory_order_relaxed);
	((go_rtpc_fn)g_go.mu_orig)(snd, GO_MUSIC, g_go.mu_applied);
	logf_("MUSIC the VR menu's Music %.0f%%: options_music %.3f sent (the game's %.3f from %s; sound effects, dialogue "
	      "and master untouched)", share * 100, g_go.mu_applied, game, from);
}

/* SOMVR_VISION (the input tick, after st_tick): Y -> taps of 122 instead of 243 */
#define GO_CMD_VISION 122   /* CombatSecondary01 = PC_WraithPing's toggle (= the step module's ST_CMD_VISION) */
#define GO_CMD_VMT 243      /* VisionModeToggle: what somvr_ctl sends for Y (unbound in the world) */
static void go_vision_tick(sc_tick_out *o, double t)
{
	if (!g_go.vis_mode)
		return;
	int n = 0, held = 0;
	for (int i = 0; i < o->ncmd; i++) /* build 9's Y: VisionModeToggle, never bound: dropped (Y held = it is there:
					   * in the world somvr_ctl gives Y as this command only, no pad Y; run 185949) */
		if (o->cmd[i] != GO_CMD_VMT)
			o->cmd[n++] = o->cmd[i];
		else
			held = 1;
	o->ncmd = n;
	int world = o->live && o->ctx == SC_CTX_WORLD, y = world && held, vis = st_vision();
	int press = y && !g_go.vis_y, release = !y && g_go.vis_y;
	g_go.vis_y = y;
	if (g_go.vis_asked && t - g_go.vis_t > ST_VIS_WAIT_S) {
		if (vis >= 0 && vis != (g_go.vis_asked > 0)) {
			g_go.n_vis_refused++;
			if (g_go.vis_lines++ < 20)
				logf_("VISION t=%.3f the wraith vision did not turn %s within %.0f ms of our tap (flag %d): refused", t,
				      g_go.vis_asked > 0 ? "on" : "off", ST_VIS_WAIT_S * 1e3, vis);
			if (g_go.vis_asked > 0)
				g_go.vis_ours = 0;
		} else if (g_go.vis_lines++ < 40) {
			logf_("VISION t=%.3f the wraith vision %s (flag %d)", t, g_go.vis_asked > 0 ? "on" : "off", vis);
		}
		g_go.vis_asked = 0;
	}
	int tap = 0, ask = 0;
	if (press) {
		g_go.n_vis_press++;
		if (g_go.vis_mode == 2 || vis != 1) /* tap: a toggle; hold: on unless it is on already (the player's own) */
			tap = 1, ask = vis == 1 ? -1 : 1, g_go.vis_ours = g_go.vis_mode == 1;
	} else if (release && g_go.vis_mode == 1 && g_go.vis_ours) {
		g_go.vis_ours = 0;
		if (vis != 0)
			tap = 1, ask = -1;
	}
	if (tap && !g_go.vis_tap) {
		g_go.vis_tap = ST_TAP_TICKS, g_go.vis_asked = ask, g_go.vis_t = t, g_go.n_vis_tap++;
		if (g_go.vis_lines++ < 40)
			logf_("VISION t=%.3f Y %s: a tap of %d for %d ticks (the vision %s -> %s)", t, press ? "pressed" : "let go",
			      GO_CMD_VISION, ST_TAP_TICKS, vis == 1 ? "on" : vis == 0 ? "off" : "unread", ask > 0 ? "on" : "off");
	}
	if (g_go.vis_tap > 0) {
		g_go.vis_tap--;
		if (world && o->ncmd < SC_OUT_CMDS) {
			o->cmd[o->ncmd++] = GO_CMD_VISION;
			g_inp.allow_cmd = GO_CMD_VISION; /* on the deny list (re-xr-input.md 3.6): our tap passes this tick */
		}
	}
}

/* the VR menu (game thread, vm_apply_key): the Music row; the VRMENU apply line's note */
static const char *go_vm_set(int k, const somvr_settings *s)
{
	if (k != SOMVR_SK_MUSIC)
		return "";
	if (!g_go.mu_on)
		return " (no SOMVR_MUSIC hook: not used)";
	go_setf(&g_go.mu_share, s->music);
	g_go.mu_dirty = 1, g_go.next_tick = 0;
	return " (the game's music volume x this, at once)";
}
static void go_vm_settings(somvr_settings *s) { s->music = g_go.mu_on ? g_go.mu_env : 1.0f; }
static uint32_t go_vm_avail(void) { return g_go.mu_on ? SOMVR_VMA_MUSIC : 0u; }

/* the trigger words (the writer thread): "gameopts" (a state line), "hints game|log|off" */
static const char *go_word(const char *w)
{
	if (!strncmp(w, "hints ", 6)) {
		int m = -1;
		for (int i = 0; i < 3; i++)
			if (!strncmp(w + 6, g_go_hint_w[i], strlen(g_go_hint_w[i])))
				m = i;
		if (m < 0)
			return "hints (not game|log|off: ignored)";
		if (m != GO_HINTS_GAME && !(g_go.hi_ok[GO_EV_HINT] || g_go.hi_ok[GO_EV_TUTORIAL]))
			return "hints (no SOMVR_HINTS=log|off at start: the delegates are not hooked)";
		atomic_store(&g_go.hint_mode, m);
		logf_("HINTS now %s (the word)", g_go_hint_w[m]);
		return "hints";
	}
	logf_("GAMEOPTS t=%.3f music: %s, share %.0f%%, the game's options_music %.3f (%s), sets %llu (ours %llu), RTPC calls "
	      "%llu; hints: %s, combat hints %llu (shown %llu, dropped %llu), tutorials %llu (%llu, %llu), prompts %llu (%llu); "
	      "the wraith vision %s", mono(), g_go.mu_on ? "on" : "off", go_f(&g_go.mu_share) * 100, go_f(&g_go.mu_game),
	      atomic_load(&g_go.mu_have) ? "seen" : "not seen yet", (unsigned long long)atomic_load(&g_go.n_music),
	      (unsigned long long)atomic_load(&g_go.n_ours), (unsigned long long)atomic_load(&g_go.n_rtpc),
	      g_go_hint_w[atomic_load(&g_go.hint_mode)], (unsigned long long)atomic_load(&g_go.n_ev[GO_EV_HINT]),
	      (unsigned long long)atomic_load(&g_go.n_show[GO_EV_HINT]), (unsigned long long)atomic_load(&g_go.n_drop[GO_EV_HINT]),
	      (unsigned long long)atomic_load(&g_go.n_ev[GO_EV_TUTORIAL]),
	      (unsigned long long)atomic_load(&g_go.n_show[GO_EV_TUTORIAL]),
	      (unsigned long long)atomic_load(&g_go.n_drop[GO_EV_TUTORIAL]), (unsigned long long)atomic_load(&g_go.n_ev[GO_EV_PROMPT]),
	      (unsigned long long)atomic_load(&g_go.n_show[GO_EV_PROMPT]),
	      st_vision() == 1 ? "on" : st_vision() == 0 ? "off" : "unread (the step module's sites)");
	return "gameopts";
}

/* one slot: its original kept, our function written; 1 = installed */
static int go_slot(const struct site_spec *s, void *fn, uintptr_t *orig)
{
	char err[200] = "";
	*orig = s->target;
	if (write_code(s->addr, &fn, 8, err, sizeof err) != 0) {
		logf_("HOOK %s SKIPPED %#lx: write failed: %s", s->name, (unsigned long)s->addr, err);
		return 0;
	}
	logf_("HOOK %s ok slot %#lx orig=%#lx now=%p", s->name, (unsigned long)s->addr, (unsigned long)s->target, fn);
	return 1;
}

static void go_start(const struct site_spec *sites, int *bad)
{
	memset(&g_go, 0, sizeof g_go);
	g_go_sites = sites;
	const char *mv = getenv("SOMVR_MUSIC");
	g_go.mu_want = mv && *mv;
	g_go.mu_env = env_float("SOMVR_MUSIC", 1.0f, 0, 1, bad);
	go_setf(&g_go.mu_share, g_go.mu_env);
	g_go.hint_want = env_word("SOMVR_HINTS", g_go_hint_w, 3, GO_HINTS_GAME);
	static const char *const vis_w[] = { "off", "hold", "tap" };
	g_go.vis_mode = env_word("SOMVR_VISION", vis_w, 3, 0);
	if (g_go.vis_mode)
		logf_("CONFIG vision=%s (SOMVR_VISION: Y %s the wraith vision with taps of CombatSecondary01 122, PC_WraithPing's "
		      "toggle; VisionModeToggle 243 no longer sent for Y)", vis_w[g_go.vis_mode],
		      g_go.vis_mode == 1 ? "held shows" : "pressed toggles");
	if (!g_go.mu_want && g_go.hint_want == GO_HINTS_GAME)
		return;
	int ok[GOS_N];
	char why[512];
	for (int i = 0; i < GOS_N; i++) {
		ok[i] = check_site(&sites[i], why, sizeof why) == 0;
		if (!ok[i])
			logf_("GAMEOPTS site %s %#lx -> %#lx MISMATCH: %s", sites[i].name, (unsigned long)sites[i].addr,
			      (unsigned long)sites[i].target, why);
	}
	g_go.ok_apply = ok[GOS_APPLY], g_go.ok_opts = ok[GOS_OPTIONS], g_go.ok_name = ok[GOS_RECNAME];
	if (g_go.mu_want) {
		if (ok[GOS_RTPC])
			g_go.mu_on = go_slot(&sites[GOS_RTPC], (void *)somvr_w_gortpc, &g_go.mu_orig);
		g_go.mu_dirty = g_go.mu_on && g_go.mu_env != 1.0f; /* the start's share, once the game's value is known */
		logf_("CONFIG music=%.2f (SOMVR_MUSIC: the game's options_music RTPC x %.2f at every set; the VR menu's Music "
		      "row changes it live; sound effects, dialogue, master untouched; nothing of the game's options or save "
		      "written): %s%s", g_go.mu_env, g_go.mu_env, g_go.mu_on ? "on" : "off: GO_RTPC did not match or not written",
		      g_go.mu_on && !(g_go.ok_apply && g_go.ok_opts) ? " (GO_APPLY/GO_OPTIONS off: a menu change waits for the "
								       "game's next options_music)" : "");
	}
	if (g_go.hint_want != GO_HINTS_GAME) {
		static void *const fns[GO_EV_N] = { (void *)somvr_w_gohint, (void *)somvr_w_gotut, (void *)somvr_w_goprompt };
		for (int e = 0; e < GO_EV_N; e++)
			if (ok[GOS_HINT + e])
				g_go.hi_ok[e] = go_slot(&sites[GOS_HINT + e], fns[e], &g_go.hi_orig[e]);
		atomic_store(&g_go.hint_mode, g_go.hint_want);
		logf_("CONFIG hints=%s (SOMVR_HINTS: %s): combat hints %s, tutorials %s, behaviour prompts %s; record names %s",
		      g_go_hint_w[g_go.hint_want],
		      g_go.hint_want == GO_HINTS_OFF ? "the flat game's combat hints and tutorial texts are dropped where the HUD's "
						       "text manager takes them, as its Display Combat Hints option does; prompts "
						       "pass" : "every hint, tutorial and prompt logged and passed on",
		      g_go.hi_ok[0] ? "hooked" : "NOT hooked", g_go.hi_ok[1] ? "hooked" : "NOT hooked",
		      g_go.hi_ok[2] ? "hooked (logged)" : "NOT hooked", g_go.ok_name ? "on" : "off (GO_RECNAME)");
	}
}

static void go_end_log(void)
{
	if (g_go.vis_mode)
		logf_("VISION END Y presses %llu, taps %llu, refused %llu", (unsigned long long)g_go.n_vis_press,
		      (unsigned long long)g_go.n_vis_tap, (unsigned long long)g_go.n_vis_refused);
	if (!g_go.mu_on && !g_go.hi_ok[0] && !g_go.hi_ok[1] && !g_go.hi_ok[2])
		return;
	logf_("GAMEOPTS END music %s: share %.0f%%, options_music set by the game %llu times (last %.3f), by the menu %llu "
	      "(last %.3f), other options passed %llu, RTPC calls %llu; hints %s: combat hints %llu (shown %llu, dropped %llu), "
	      "tutorials %llu (shown %llu, dropped %llu), prompts %llu (shown %llu)", g_go.mu_on ? "on" : "off",
	      go_f(&g_go.mu_share) * 100, (unsigned long long)atomic_load(&g_go.n_music), go_f(&g_go.mu_game),
	      (unsigned long long)atomic_load(&g_go.n_ours), g_go.mu_applied, (unsigned long long)atomic_load(&g_go.n_other_opts),
	      (unsigned long long)atomic_load(&g_go.n_rtpc), g_go_hint_w[atomic_load(&g_go.hint_mode)],
	      (unsigned long long)atomic_load(&g_go.n_ev[0]), (unsigned long long)atomic_load(&g_go.n_show[0]),
	      (unsigned long long)atomic_load(&g_go.n_drop[0]), (unsigned long long)atomic_load(&g_go.n_ev[1]),
	      (unsigned long long)atomic_load(&g_go.n_show[1]), (unsigned long long)atomic_load(&g_go.n_drop[1]),
	      (unsigned long long)atomic_load(&g_go.n_ev[2]), (unsigned long long)atomic_load(&g_go.n_show[2]));
}
