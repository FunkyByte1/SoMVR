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
 *   toggle** (P9), held 0.85 s it is PC_Radiance. hold: the vision while Y is held; tap: each press toggles. 243 is no
 *   longer sent for Y. 122 is on the deny list: our tap passes as the phase's does (allow_cmd). Build 16
 *   (re-xr-pipeline.md 49; build 15b's session left the player in it): Y gives a wish and taps of 122 bring the flag
 *   (read as the step module reads it, st_vision) to it - somvr_vision.c: a press with the vision on (ours, the game's,
 *   a phase's) turns it off; Y not held while it is ours = off (the level: a release the game side never saw - the bug
 *   mark's chord gives a press shorter than 0.12 s as one tick - cannot leave it on); the off of a quick tap waits until
 *   the on has landed and the game's ~1 s dwell has passed (run 002021: an off tap within it is dropped, an on tap held
 *   back); a tap not answered within 0.3 s is tapped again (6 at most, then logged). Every
 *   change of the flag is logged (ours with its delay, or the game's own). Unreadable: the edges tap blind, as build 15b.
 *   Test words: "vision" (a state line), "vision y MS" (Y held MS ms at the snapshot, before the chord: 1 = the
 *   shortest press), "vision taps N MS" (N raw taps of 122, MS apart: the game's own answer, no wish of ours).
 *
 * SOMVR_LOAD_VOICES=game|off (default game = nothing; build 15b, re-xr-pipeline.md 47) - the voices of a loading screen
 *   (session 2026-10-26 mark 11: "The voices I hear in the loading screen are kind of weird ... You might just want to
 *   turn them off"; the game's LoadingNarration database records, played by its narrator emitter). off: through a load
 *   on flat frames (the session's SOMVR_SSM_LOAD state - interface state 3, no menu, paused - with no stereo frame for
 *   LV_STILL_S: the pause menu and the map share the state but render stereo) the game's dialogue volume, the global
 *   RTPC "options_dialog" (the Sound options' Dialogue slider, GameOptions +0x194, FUN_00cbe980 at 0xcbea06), is sent
 *   as 0 through GO_RTPC's original (and a set of the game's during it is held at 0); at the load's end (the state
 *   left or stereo frames again) the game's own last value goes back. Music, sound effects and master are untouched;
 *   the game's options, profile and save are never written. Its value is known from the game's own set (its start's
 *   FUN_00cbe980 passes our wrapper): not seen yet = no mute (logged).
 *
 * Trigger words: "gameopts" (a state line), "hints game|log|off" (live). Fail soft: a site mismatch = that part off,
 * nothing patched. */

/* GAMEOPTS_SITES_BEGIN (check_sites.py counts this block on its own. Patched with SOMVR_MUSIC or
 * SOMVR_LOAD_VOICES=off: GO_RTPC = CSoundMgr's slot 22 (the context slots 21 and 22 only: SOMVR_AUDIO=head has
 * already written slot 23 when go_start checks it,
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
#define GO_DIALOG "options_dialog" /* SOMVR_LOAD_VOICES: FUN_00cbe980's `movss xmm0, [rbx+0x194]; mov esi, 0x3535999` */
#define GO_SLOT_RTPC 0xb0   /* the sound manager's SetRTPC (slot 22) */
#define LV_STILL_S 0.25     /* SOMVR_LOAD_VOICES: no stereo frame for this long in the load's state = a load */

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
	int rtpc_on;                 /* GO_RTPC installed (SOMVR_MUSIC or SOMVR_LOAD_VOICES=off; mu_orig is its original) */
	int lv_want, lv_on;          /* SOMVR_LOAD_VOICES=off asked; and GO_RTPC installed */
	_Atomic int lv_muted;        /* a load's mute is on: the game's options_dialog sets are held at 0 */
	_Atomic uint32_t lv_game;    /* the game's last options_dialog (float bits), valid with lv_have */
	_Atomic int lv_have;
	_Atomic uintptr_t lv_snd;    /* the sound manager the game called it on */
	_Atomic uint64_t n_lv_held;  /* the game's own sets held at 0 during a mute */
	uint64_t lv_ns, n_lv_loads, n_lv_unknown; /* the stereo frame count at the last tick; mutes; loads not muted */
	double lv_ns_t, lv_t0, lv_s; /* when the count last moved; the mute's start; seconds muted in all */
	int lv_unknown;              /* this load was logged as not muted (the game's value unknown) */
	_Atomic int hint_mode;       /* GO_HINTS_* (the word changes it live) */
	int hint_want;               /* SOMVR_HINTS asked hooks (log|off) */
	int hi_ok[GO_EV_N];          /* that delegate's slot installed */
	uintptr_t hi_orig[GO_EV_N];
	int ok_name;
	_Atomic uint64_t n_ev[GO_EV_N], n_show[GO_EV_N], n_drop[GO_EV_N];
	_Atomic uint32_t hint_lines;
	/* build 24 (som-b24-brand, re-xr-pipeline.md 98): SOMVR_HINTS_KEEP_TUT=PREFIX[,...] - a tutorial text whose record name
	 * starts so passes although SOMVR_HINTS=off drops tutorials (the story missions' 'GP_*': the Sea Fort's archers'
	 * lesson comes while the game has every combat command off - dropped, nothing in VR said why a press did nothing);
	 * each passed tutorial's record properties logged once */
	char tut_keep[4][32];
	int ntut_keep;
	uintptr_t tut_seen[16];
	_Atomic int ntut_seen;
	_Atomic uint64_t n_tut_kept;
	double next_tick;
	uint32_t tick_lines;
	int vis_mode;                /* SOMVR_VISION: 0 off (build 9's 243), 1 hold, 2 tap */
	somvr_vis vis;               /* Y's wish and its taps (somvr_vision.c) */
	somvr_vis_cfg vis_cfg;
	int vis_flag;                /* the flag last tick (the lines) */
	int raw_n, raw_tap, raw_gap; /* the word "vision taps N MS": taps left, ticks of this one left, ticks to the next */
	double raw_next, raw_ms;
	uint32_t vis_lines;
	/* build 17 (re-xr-pipeline.md 61.6): the vision record's ramps (SOMVR_VISION_RAMP_MS) */
	float ramp_ms[2];            /* in, out (< 0: the game's) */
	uintptr_t ramp_rec[4];       /* the records seen (logged once each), patched when ramp_ms is set */
	int ramp_nrec;
	double ramp_next;
} g_go;

/* build 17: the wraith vision's look ramps in and out over its record's RampInDuration / RampOutDuration
 * (re-xr-pipeline.md 21's "wraith-world look": FUN_00ad3330 -> FUN_009e62d0 -> FUN_0266a8d0 reads them at each switch
 * from the record at PlayerVisionMode+0x28, by the schema *0x4c25d20's field hashes at +0xf8 / +0x128). A record
 * (byte *r): r[0] its fields, r[1] & 1 the layout, *(r+8) the 16-byte entries (sorted by the hash at +4, the real entry
 * at entries[+9]; or by the hash at +0 with layout 1), *(r+0x10) the 8-byte values: the real entry's u16 +0xc nonzero
 * = set, its u16 +0xe the value's slot (a float for a duration). The value's address, or 0 (not set / unreadable) */
#define GO_VSCHEMA 0x4c25d20 /* DAT_04c25d20: the vision record's schema (FUN_02608950 fills it) */
#define GO_VM_REC 0x28       /* PlayerVisionMode: the vision record */
static uintptr_t go_rec_field(uintptr_t rec, uint32_t hash)
{
	uint8_t hd[2];
	uintptr_t ents = 0, vals = 0;
	if (!rec || !peek(rec, hd, 2) || !peek(rec + 8, &ents, 8) || !peek(rec + 0x10, &vals, 8) || !ents || !vals)
		return 0;
	for (int i = 0; i < hd[0]; i++) {
		uint8_t e[16];
		if (!peek(ents + 16u * (uintptr_t)i, e, 16))
			return 0;
		uint32_t h;
		memcpy(&h, e + ((hd[1] & 1) ? 0 : 4), 4);
		if (h != hash)
			continue;
		if (!(hd[1] & 1) && !peek(ents + 16u * (uintptr_t)e[9], e, 16))
			return 0;
		uint16_t set, slot;
		memcpy(&set, e + 0xc, 2), memcpy(&slot, e + 0xe, 2);
		return set ? vals + 8u * slot : 0;
	}
	return 0;
}
/* the vision record now (PlayerVisionMode+0x28; 0 = none) */
static uintptr_t go_vis_rec(void)
{
	if (!g_step.ok_vis || !g_step.pl)
		return 0;
	uintptr_t vm = peek_ptr(g_step.pl + ST_P_VISION);
	if (!vm || vptr_of((void *)vm) != g_stepg.vt_vm || peek_ptr(vm + ST_VM_OWNER) != g_step.pl)
		return 0;
	return peek_ptr(vm + GO_VM_REC);
}
/* a record's ramps (s; < 0 = not set / unreadable) and, with ramp_ms set, ours written (only over a plausible 0..10 s) */
static void go_ramp_apply(uintptr_t rec, float out_s[2], int write, int *wrote)
{
	uintptr_t sch = peek_ptr(GO_VSCHEMA);
	*wrote = 0;
	for (int k = 0; k < 2; k++) {
		uint32_t h = 0;
		out_s[k] = -1;
		uintptr_t a = sch && peek(sch + (k ? 0x128 : 0xf8), &h, 4) ? go_rec_field(rec, h) : 0;
		float v;
		if (!a || !peek(a, &v, 4) || !(v >= 0 && v <= 10))
			continue;
		out_s[k] = v;
		if (write && g_go.ramp_ms[k] >= 0 && fabsf(v - g_go.ramp_ms[k] * 1e-3f) > 1e-4f) {
			float nv = g_go.ramp_ms[k] * 1e-3f;
			char err[160] = "";
			if (write_code(a, &nv, 4, err, sizeof err) == 0)
				(*wrote)++;
		}
	}
}
/* each tick at most twice a second: the record at PlayerVisionMode+0x28 - a new one logged, ours written (the game reads
 * the ramps at the next switch) */
static void go_ramp_tick(double t, int force)
{
	if (!force && t < g_go.ramp_next)
		return;
	g_go.ramp_next = t + 0.5;
	uintptr_t rec = go_vis_rec();
	if (!rec)
		return;
	int known = 0;
	for (int i = 0; i < g_go.ramp_nrec; i++)
		known |= g_go.ramp_rec[i] == rec;
	int want = g_go.ramp_ms[0] >= 0 || g_go.ramp_ms[1] >= 0;
	if (known && !want && !force)
		return;
	float v[2];
	int wrote;
	go_ramp_apply(rec, v, want, &wrote);
	if (!known && g_go.ramp_nrec < 4)
		g_go.ramp_rec[g_go.ramp_nrec++] = rec;
	if (!known || wrote || force)
		logf_("VISION t=%.3f the vision record %#lx: RampInDuration %.3f s, RampOutDuration %.3f s (the game's at the "
		      "next switch)%s", t, (unsigned long)rec, (double)v[0], (double)v[1],
		      wrote ? "; SOMVR_VISION_RAMP_MS written" : want ? "; as wished already" : "");
}

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
	if (opt && g_go.lv_on && !strcmp(name + 8, GO_DIALOG + 8)) { /* SOMVR_LOAD_VOICES: its value kept, held during a load */
		go_setf(&g_go.lv_game, v);
		atomic_store_explicit(&g_go.lv_snd, (uintptr_t)snd, memory_order_relaxed);
		atomic_store_explicit(&g_go.lv_have, 1, memory_order_release);
		if (atomic_load_explicit(&g_go.lv_muted, memory_order_relaxed)) {
			atomic_fetch_add_explicit(&g_go.n_lv_held, 1, memory_order_relaxed);
			if (atomic_fetch_add_explicit(&g_go.other_lines, 1, memory_order_relaxed) < 12)
				logf_("LOADVOICES the game sets options_dialog %.3f during a load: 0 sent (its value back after the load)", v);
			return ((go_rtpc_fn)g_go.mu_orig)(snd, name, 0.0f);
		}
	}
	if (!music || !g_go.mu_on) {
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

static void co_props(uintptr_t tbl, char *s, size_t cap, char *key, size_t kcap); /* somvr_collect_game.c */
/* build 24, SOMVR_HINTS_KEEP_TUT: 1 = this tutorial passes (its record's name starts with a prefix); its properties
 * logged at its first pass */
static int go_tut_keep(uintptr_t rec)
{
	char nm[64];
	go_rec_name(rec, nm, sizeof nm);
	int k = 0;
	for (int i = 0; nm[0] && i < g_go.ntut_keep && !k; i++)
		k = !strncmp(nm, g_go.tut_keep[i], strlen(g_go.tut_keep[i]));
	if (!k)
		return 0;
	atomic_fetch_add(&g_go.n_tut_kept, 1);
	int n = atomic_load(&g_go.ntut_seen), seen = 0;
	for (int i = 0; i < n && i < 16; i++)
		seen |= g_go.tut_seen[i] == rec;
	if (!seen && n < 16) {
		char s[1000];
		g_go.tut_seen[n] = rec, atomic_store(&g_go.ntut_seen, n + 1);
		co_props(rec, s, sizeof s, NULL, 0);
		logf_("HINTS tutorial record %#lx '%s' (kept):%s", (unsigned long)rec, nm, s);
	}
	return 1;
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
	/* somvr_collect_game.c: SOMVR_HINTS_KEEP's hints pass (the collectibles' wraith-vision tutorial) */
	int kept = drop && ev == GO_EV_HINT && co_hint_keep(rec);
	if (drop && ev == GO_EV_TUTORIAL && g_go.ntut_keep) /* build 24: SOMVR_HINTS_KEEP_TUT */
		kept = go_tut_keep(rec);
	drop = drop && !kept;
	uint64_t n = atomic_fetch_add_explicit(&g_go.n_ev[ev], 1, memory_order_relaxed) + 1;
	atomic_fetch_add_explicit(&g_go.n_show[ev], (uint64_t)show, memory_order_relaxed);
	atomic_fetch_add_explicit(&g_go.n_drop[ev], (uint64_t)drop, memory_order_relaxed);
	if (mode != GO_HINTS_GAME && atomic_fetch_add_explicit(&g_go.hint_lines, 1, memory_order_relaxed) < 300) {
		char nm[64];
		go_rec_name(rec, nm, sizeof nm);
		if (!nm[0] && ok && ev == GO_EV_HINT) /* (the hint records have no name: their text key, somvr_collect_game.c) */
			snprintf(nm, sizeof nm, "%s", co_hint_key(rec));
		logf_("HINTS %s #%llu t=%.3f: %s '%s' (record %#lx)%s", g_go_ev_name[ev], (unsigned long long)n, mono(),
		      !ok ? "unreadable" : show ? "show" : "hide", nm, (unsigned long)rec,
		      drop ? ": dropped (SOMVR_HINTS=off: what the game's Display Combat Hints off does)"
		      : kept ? (ev == GO_EV_TUTORIAL ? ": kept (SOMVR_HINTS_KEEP_TUT)" : ": kept (SOMVR_HINTS_KEEP)") : ": passed on");
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

/* SOMVR_LOAD_VOICES=off (the input tick, game thread): a load on flat frames = options_dialog 0, its end = the game's
 * own value back (see the top) */
static void lv_tick(double t)
{
	if (!g_go.lv_on)
		return;
	uint64_t ns = atomic_load_explicit(&g_sh.n_stereo, memory_order_relaxed);
	/* (SOMVR_INSPECT's flat menu - an artifact's view, its memory's voice - shares the state with no stereo frame: not a
	 * load, nor the eye size's switch back after it; somvr_collect_game.c) */
	if (ns != g_go.lv_ns || !(g_go.lv_ns_t > 0) || co_flat_now())
		g_go.lv_ns = ns, g_go.lv_ns_t = t;
	int state = g_inp.on && g_inp.ifs == 3 && g_inp.menus == 0 && g_inp.paused == 1 && !co_flat_now();
	int load = state && t - g_go.lv_ns_t >= LV_STILL_S, muted = atomic_load_explicit(&g_go.lv_muted, memory_order_relaxed);
	if (!load)
		g_go.lv_unknown = 0;
	if (load == muted)
		return;
	if (!atomic_load_explicit(&g_go.lv_have, memory_order_acquire)) { /* (only a load gets here: muted needs it) */
		if (!g_go.lv_unknown)
			g_go.lv_unknown = 1, g_go.n_lv_unknown++,
			logf_("LOADVOICES t=%.3f a load, but the game's options_dialog was not seen yet: its voices stay", t);
		return;
	}
	void *snd = (void *)atomic_load_explicit(&g_go.lv_snd, memory_order_relaxed);
	if (load) {
		atomic_store_explicit(&g_go.lv_muted, 1, memory_order_relaxed);
		float game = go_f(&g_go.lv_game);
		uint64_t r = ((go_rtpc_fn)g_go.mu_orig)(snd, GO_DIALOG, 0.0f);
		g_go.lv_t0 = t, g_go.n_lv_loads++;
		logf_("LOADVOICES t=%.3f a load (interface state 3, no menu, paused, no stereo frame for %.2f s): options_dialog 0 "
		      "sent (the game's %.3f; result %llu): the loading screen's voices off; music and sound effects untouched",
		      t, t - g_go.lv_ns_t, game, (unsigned long long)r);
		return;
	}
	atomic_store_explicit(&g_go.lv_muted, 0, memory_order_relaxed);
	float game = go_f(&g_go.lv_game);
	uint64_t r = ((go_rtpc_fn)g_go.mu_orig)(snd, GO_DIALOG, game);
	g_go.lv_s += t - g_go.lv_t0;
	logf_("LOADVOICES t=%.3f the load ended after %.1f s (%s): options_dialog %.3f sent back (the game's own; result %llu)",
	      t, t - g_go.lv_t0, !state ? "its state left" : "stereo frames again", game, (unsigned long long)r);
}

/* the input tick (game thread): a menu change or the start's share applied once the game's value is known */
static void go_tick(double t)
{
	lv_tick(t); /* SOMVR_LOAD_VOICES */
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
#define GO_VIS_LINES 200
/* the word "vision y MS" (writer thread -> the input tick): Y held MS ms in the snapshot, before the chord */
static _Atomic uint32_t g_go_ty_seq, g_go_ty_ms;
static struct {
	uint32_t seen;
	double until;
	uint8_t extra, raw_p, wrote_p; /* the test's presses; the layer's count under ours; what we wrote */
	int have;
} g_go_ty;
/* "vision taps N MS" (writer thread -> the input tick) */
static _Atomic uint32_t g_go_rt_seq, g_go_rt_n, g_go_rt_ms;
static _Atomic uint32_t g_go_ramp_seq, g_go_ramp_seen; /* "vision ramp [IN OUT]" (build 17; writer -> the input tick) */
static _Atomic int g_go_ramp_req[2];
static uint32_t g_go_rt_seen;

/* in_tick, right after the snapshot's read (and the test stick): the word's Y press over the layer's (the left hand's
 * upper button; its presses counter kept rising over the layer's own, a failed read's kept snapshot not counted twice) */
static void go_test_y(double t)
{
	uint32_t sq = atomic_load_explicit(&g_go_ty_seq, memory_order_acquire);
	somvr_hand_in *hi = &g_inp.st.hand[SOMVR_HAND_L];
	if (sq == g_go_ty.seen && !g_go_ty.have)
		return;
	uint8_t cur = hi->presses[1];
	if (!g_go_ty.have || cur != g_go_ty.wrote_p)
		g_go_ty.raw_p = cur; /* a fresh snapshot from the layer */
	g_go_ty.have = 1;
	if (sq != g_go_ty.seen) {
		g_go_ty.seen = sq;
		double ms = atomic_load_explicit(&g_go_ty_ms, memory_order_relaxed);
		g_go_ty.until = t + ms * 1e-3, g_go_ty.extra++;
		logf_("VISION t=%.3f test: Y pressed for %.0f ms (the word; at the snapshot, before the bug mark's chord)", t, ms);
	}
	hi->presses[1] = g_go_ty.wrote_p = (uint8_t)(g_go_ty.raw_p + g_go_ty.extra);
	hi->active |= SOMVR_HACT_INPUT; /* the hand's input on from the word on (headless: no test line for it may have come) */
	if (t < g_go_ty.until)
		hi->buttons |= SOMVR_BTN_UPPER;
	g_inp.have_st = 1;
}

static const char *go_vis_name(int f) { return f == 1 ? "on" : f == 0 ? "off" : "unread"; }

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
	int world = o->live && o->ctx == SC_CTX_WORLD, vis = st_vision();
	uint32_t rq = atomic_load(&g_go_ramp_seq);
	int rforce = rq != atomic_load(&g_go_ramp_seen);
	if (rforce) {
		atomic_store(&g_go_ramp_seen, rq);
		g_go.ramp_ms[0] = (float)atomic_load(&g_go_ramp_req[0]), g_go.ramp_ms[1] = (float)atomic_load(&g_go_ramp_req[1]);
	}
	go_ramp_tick(t, rforce || (vis != g_go.vis_flag && vis >= 0 && g_go.vis_flag >= 0));
	somvr_vis_out vo;
	somvr_vis_tick(&g_go.vis, &g_go.vis_cfg, world, held, vis, t, &vo);
	int lines = g_go.vis_lines < GO_VIS_LINES;
	if (vis != g_go.vis_flag && vis >= 0 && g_go.vis_flag >= 0 && lines) {
		g_go.vis_lines++;
		if (vo.ev & SOMVR_VIE_LANDED)
			logf_("VISION t=%.3f the flag %d -> %d: our tap's, %.0f ms after it", t, g_go.vis_flag, vis, (double)vo.ms);
		else
			logf_("VISION t=%.3f the flag %d -> %d: no tap of ours waiting (the game's own, a phase's, a test tap)", t,
			      g_go.vis_flag, vis);
	}
	g_go.vis_flag = vis;
	if ((vo.ev & ~(uint32_t)(SOMVR_VIE_LANDED | SOMVR_VIE_GAME)) && g_go.vis_lines < GO_VIS_LINES) {
		char w[200];
		size_t k = 0;
		w[0] = 0;
#define GO_VW(...) (k += (size_t)snprintf(w + k, k < sizeof w ? sizeof w - k : 0, __VA_ARGS__), k = k < sizeof w ? k : sizeof w)
		if (vo.ev & SOMVR_VIE_RELIGHT_STOP) /* build 18: the game will not keep it (a fight): no blinking */
			GO_VW("; the game ended the relit vision within %.0f ms: no more relights until Y is pressed again (%llu)",
			      g_go.vis_cfg.relight_min_s * 1e3, (unsigned long long)g_go.vis.n_relight_stop);
		if (vo.ev & SOMVR_VIE_RELIGHT) /* build 18: SOMVR_VISION_RELIGHT */
			GO_VW("; the game ended it while Y holds it: on wished again (after the game's %.0f ms dwell; relight %llu)",
			      g_go.vis_cfg.settle_s * 1e3, (unsigned long long)g_go.vis.n_relight);
		if (vo.ev & SOMVR_VIE_PRESS_ON)
			GO_VW("; Y pressed: on wished%s", g_go.vis_mode == SOMVR_VIS_HOLD ? " (ours while held)" : "");
		if (vo.ev & SOMVR_VIE_PRESS_OFF)
			GO_VW("; Y pressed with the vision on: off wished");
		if (vo.ev & SOMVR_VIE_RELEASE)
			GO_VW("; Y not held and the vision ours: off wished");
		if (vo.ev & SOMVR_VIE_LATE)
			GO_VW("; our tap not answered within %.0f ms", g_go.vis_cfg.wait_s * 1e3);
		if (vo.ev & SOMVR_VIE_TAP)
			GO_VW("; a tap of %d for %d ticks (%s, try %d)%s", GO_CMD_VISION, g_go.vis_cfg.tap_ticks, vo.to ? "on" : "off",
			      vo.tries, (vo.ev & SOMVR_VIE_BLIND) ? " blind: the flag unread" : "");
		if (vo.ev & SOMVR_VIE_DONE)
			GO_VW("; the vision %s as wished (%.0f ms, %d tap(s))", go_vis_name(vis), (double)vo.done_ms, vo.tries);
		if (vo.ev & SOMVR_VIE_GIVEUP)
			GO_VW("; NOT reached after %d taps: given up (the vision %s; the next press tries again)", vo.tries,
			      go_vis_name(vis));
#undef GO_VW
		if (w[0]) {
			g_go.vis_lines++;
			logf_("VISION t=%.3f the flag %s%s", t, go_vis_name(vis), w);
		}
	}
	int tap = vo.tap;
	/* the word "vision taps N MS": raw taps, no wish of ours (the game's answer measured by the flag's lines) */
	uint32_t rs = atomic_load_explicit(&g_go_rt_seq, memory_order_acquire);
	if (rs != g_go_rt_seen) {
		g_go_rt_seen = rs;
		g_go.raw_n = (int)atomic_load_explicit(&g_go_rt_n, memory_order_relaxed);
		g_go.raw_ms = atomic_load_explicit(&g_go_rt_ms, memory_order_relaxed);
		g_go.raw_next = t, g_go.raw_tap = 0;
		logf_("VISION t=%.3f test: %d raw tap(s) of %d, %.0f ms apart (the word; the flag %s)", t, g_go.raw_n,
		      GO_CMD_VISION, g_go.raw_ms, go_vis_name(vis));
	}
	if (g_go.raw_tap > 0) {
		g_go.raw_tap--;
		tap = 1;
	} else if (g_go.raw_n > 0 && t >= g_go.raw_next && !vo.tap) {
		g_go.raw_n--, g_go.raw_tap = g_go.vis_cfg.tap_ticks - 1, g_go.raw_next = t + g_go.raw_ms * 1e-3;
		tap = 1;
		logf_("VISION t=%.3f test: a raw tap of %d (the flag %s; %d more)", t, GO_CMD_VISION, go_vis_name(vis), g_go.raw_n);
	}
	if (tap && world && o->ncmd < SC_OUT_CMDS) {
		o->cmd[o->ncmd++] = GO_CMD_VISION;
		g_inp.allow_cmd = GO_CMD_VISION; /* on the deny list (re-xr-input.md 3.6): our tap passes this tick */
	}
}

/* the word "vision [y MS|taps N MS]" (writer thread) */
static const char *go_vision_word(const char *w)
{
	if (!g_go.vis_mode)
		return "vision (SOMVR_VISION is off: ignored)";
	int nn;
	float ms;
	float ri, ro;
	if (sscanf(w, "vision ramp %f %f", &ri, &ro) == 2) { /* build 17: the ramps from now (ms; -1 = leave the record's) */
		if (!(ri >= -1 && ri <= 5000 && ro >= -1 && ro <= 5000))
			return "vision ramp (bad: -1..5000 ms: ignored)";
		atomic_store(&g_go_ramp_req[0], (int)ri), atomic_store(&g_go_ramp_req[1], (int)ro);
		atomic_fetch_add(&g_go_ramp_seq, 1);
		return "vision ramp (written at the next tick)";
	}
	if (!strncmp(w, "vision ramp", 11)) {
		atomic_fetch_add(&g_go_ramp_seq, 1); /* a state line (the same values) */
		atomic_store(&g_go_ramp_req[0], (int)g_go.ramp_ms[0]), atomic_store(&g_go_ramp_req[1], (int)g_go.ramp_ms[1]);
		return "vision ramp (the record's line at the next tick)";
	}
	if (sscanf(w, "vision y %f", &ms) == 1) {
		if (!(ms >= 1 && ms <= 60000))
			return "vision y (bad: 1..60000 ms: ignored)";
		atomic_store_explicit(&g_go_ty_ms, (uint32_t)ms, memory_order_relaxed);
		atomic_fetch_add_explicit(&g_go_ty_seq, 1, memory_order_release);
		return "vision y";
	}
	if (sscanf(w, "vision taps %d %f", &nn, &ms) == 2) {
		if (nn < 1 || nn > 8 || !(ms >= 30 && ms <= 5000))
			return "vision taps (bad: 1..8 taps, 30..5000 ms: ignored)";
		atomic_store_explicit(&g_go_rt_n, (uint32_t)nn, memory_order_relaxed);
		atomic_store_explicit(&g_go_rt_ms, (uint32_t)ms, memory_order_relaxed);
		atomic_fetch_add_explicit(&g_go_rt_seq, 1, memory_order_release);
		return "vision taps";
	}
	const somvr_vis *v = &g_go.vis;
	logf_("VISION t=%.3f state: %s, the flag %s, Y %s, wish %s, ours %d, tap %d pending %d tries %d; presses %llu taps %llu "
	      "(again %llu) landed %llu late %llu the game's own %llu given up %llu blind %llu", mono(),
	      somvr_vis_mode_name(g_go.vis_mode), go_vis_name(st_vision()), v->y ? "held" : "up",
	      v->want < 0 ? "none" : v->want ? "on" : "off", v->ours, v->tap, v->pending, v->tries,
	      (unsigned long long)v->n_press, (unsigned long long)v->n_taps, (unsigned long long)v->n_retry,
	      (unsigned long long)v->n_landed, (unsigned long long)v->n_late, (unsigned long long)v->n_game,
	      (unsigned long long)v->n_giveup, (unsigned long long)v->n_blind);
	return "vision";
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
	      "the wraith vision %s; load voices %s (loads muted %llu, not known %llu, muted now %d, the game's sets held %llu)",
	      mono(), g_go.mu_on ? "on" : "off", go_f(&g_go.mu_share) * 100, go_f(&g_go.mu_game),
	      atomic_load(&g_go.mu_have) ? "seen" : "not seen yet", (unsigned long long)atomic_load(&g_go.n_music),
	      (unsigned long long)atomic_load(&g_go.n_ours), (unsigned long long)atomic_load(&g_go.n_rtpc),
	      g_go_hint_w[atomic_load(&g_go.hint_mode)], (unsigned long long)atomic_load(&g_go.n_ev[GO_EV_HINT]),
	      (unsigned long long)atomic_load(&g_go.n_show[GO_EV_HINT]), (unsigned long long)atomic_load(&g_go.n_drop[GO_EV_HINT]),
	      (unsigned long long)atomic_load(&g_go.n_ev[GO_EV_TUTORIAL]),
	      (unsigned long long)atomic_load(&g_go.n_show[GO_EV_TUTORIAL]),
	      (unsigned long long)atomic_load(&g_go.n_drop[GO_EV_TUTORIAL]), (unsigned long long)atomic_load(&g_go.n_ev[GO_EV_PROMPT]),
	      (unsigned long long)atomic_load(&g_go.n_show[GO_EV_PROMPT]),
	      st_vision() == 1 ? "on" : st_vision() == 0 ? "off" : "unread (the step module's sites)",
	      g_go.lv_on ? "off" : "game", (unsigned long long)g_go.n_lv_loads, (unsigned long long)g_go.n_lv_unknown,
	      atomic_load(&g_go.lv_muted), (unsigned long long)atomic_load(&g_go.n_lv_held));
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
	{ /* build 24: SOMVR_HINTS_KEEP_TUT=PREFIX[,PREFIX...] (unset = every tutorial dropped with SOMVR_HINTS=off, build 23) */
		const char *tk = getenv("SOMVR_HINTS_KEEP_TUT");
		for (const char *p = tk; p && *p && g_go.ntut_keep < 4;) {
			size_t n = strcspn(p, ",");
			if (n && n < sizeof g_go.tut_keep[0])
				memcpy(g_go.tut_keep[g_go.ntut_keep], p, n), g_go.tut_keep[g_go.ntut_keep++][n] = 0;
			p += n + (p[n] == ',');
		}
	}
	static const char *const vis_w[] = { "off", "hold", "tap" };
	g_go.vis_mode = env_word("SOMVR_VISION", vis_w, 3, 0);
	somvr_vis_cfg_default(&g_go.vis_cfg);
	/* build 18: an orc's hit (the game) ends the vision while Y is held for it: on again after the dwell (default on) */
	g_go.vis_cfg.relight = env_word("SOMVR_VISION_RELIGHT", (const char *const[]){ "off", "on" }, 2, 1);
	somvr_vis_init(&g_go.vis, g_go.vis_mode);
	g_go.vis_flag = -1;
	if (g_go.vis_mode)
		logf_("CONFIG vision=%s (SOMVR_VISION: Y %s the wraith vision with taps of CombatSecondary01 122, PC_WraithPing's "
		      "toggle; VisionModeToggle 243 no longer sent for Y; build 16: %s, a tap answered within %.0f ms or tapped "
		      "again, %.0f ms after the flag's last change at the earliest, %d taps at most)", vis_w[g_go.vis_mode],
		      g_go.vis_mode == 1 ? "held shows" : "pressed toggles",
		      g_go.vis_mode == 1 ? "a press with the vision on turns it off, Y not held while it is ours turns it off"
					 : "a press = the other state", g_go.vis_cfg.wait_s * 1e3, g_go.vis_cfg.settle_s * 1e3,
		      g_go.vis_cfg.tries);
	if (g_go.vis_mode == SOMVR_VIS_HOLD)
		logf_("CONFIG vision relight=%s (SOMVR_VISION_RELIGHT%s): %s", g_go.vis_cfg.relight ? "on" : "off",
		      g_go.vis_cfg.relight ? ", the default" : "", g_go.vis_cfg.relight ? "the game's own off while Y holds the vision "
		      "= on again after its dwell" : "the game's own off is left until the next Y press (build 17b)");
	/* build 17 (re-xr-pipeline.md 61.6): SOMVR_VISION_RAMP_MS=IN,OUT - the vision record's look ramps (the game's
	 * RampInDuration / RampOutDuration, written into the record at PlayerVisionMode+0x28 as soon as it is seen; the
	 * game reads them at its next switch); unset = the game's */
	g_go.ramp_ms[0] = g_go.ramp_ms[1] = -1;
	if (getenv("SOMVR_VISION_RAMP_MS") && getenv("SOMVR_VISION_RAMP_MS")[0]) {
		float rv[2] = { -1, -1 };
		env_vec("SOMVR_VISION_RAMP_MS", rv, 2, 0, 5000, bad);
		g_go.ramp_ms[0] = rv[0], g_go.ramp_ms[1] = rv[1];
	}
	if (g_go.vis_mode)
		logf_("CONFIG vision ramp=%s (SOMVR_VISION_RAMP_MS: the wraith look's ramp in %.0f ms, out %.0f ms; -1 = the "
		      "game's record)", g_go.ramp_ms[0] >= 0 || g_go.ramp_ms[1] >= 0 ? "ours" : "game", (double)g_go.ramp_ms[0],
		      (double)g_go.ramp_ms[1]);
	static const char *const lv_w[] = { "game", "off" };
	g_go.lv_want = env_word("SOMVR_LOAD_VOICES", lv_w, 2, 0) == 1;
	if (!g_go.mu_want && !g_go.lv_want && g_go.hint_want == GO_HINTS_GAME)
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
	if ((g_go.mu_want || g_go.lv_want) && ok[GOS_RTPC])
		g_go.rtpc_on = go_slot(&sites[GOS_RTPC], (void *)somvr_w_gortpc, &g_go.mu_orig);
	if (g_go.lv_want) {
		g_go.lv_on = g_go.rtpc_on;
		logf_("CONFIG load_voices=off (SOMVR_LOAD_VOICES: through a load on flat frames the game's options_dialog RTPC "
		      "is sent as 0, its own value back after the load; music, sound effects, master untouched; nothing of the "
		      "game's options or save written): %s", g_go.lv_on ? "on" : "off: GO_RTPC did not match or not written");
	}
	if (g_go.mu_want) {
		g_go.mu_on = g_go.rtpc_on;
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
		if (g_go.ntut_keep)
			logf_("CONFIG hints_keep_tut=%s%s%s%s%s%s%s (SOMVR_HINTS_KEEP_TUT): tutorial texts whose record name starts so pass "
			      "with SOMVR_HINTS=off (a story mission's own lesson; the archers' comes while the game has every combat "
			      "command off)%s", g_go.tut_keep[0], g_go.ntut_keep > 1 ? "," : "", g_go.ntut_keep > 1 ? g_go.tut_keep[1] : "",
			      g_go.ntut_keep > 2 ? "," : "", g_go.ntut_keep > 2 ? g_go.tut_keep[2] : "", g_go.ntut_keep > 3 ? "," : "",
			      g_go.ntut_keep > 3 ? g_go.tut_keep[3] : "", g_go.ok_name ? "" : " - NOT matched: no record names (GO_RECNAME)");
	}
}

static void go_end_log(void)
{
	if (g_go.vis_mode)
		logf_("VISION END Y presses %llu, taps %llu (again %llu), landed %llu, not answered %llu, given up %llu, blind %llu; "
		      "the game's own changes %llu; the vision %s at the end", (unsigned long long)g_go.vis.n_press,
		      (unsigned long long)g_go.vis.n_taps, (unsigned long long)g_go.vis.n_retry,
		      (unsigned long long)g_go.vis.n_landed, (unsigned long long)g_go.vis.n_late,
		      (unsigned long long)g_go.vis.n_giveup, (unsigned long long)g_go.vis.n_blind,
		      (unsigned long long)g_go.vis.n_game, go_vis_name(g_go.vis_flag));
	if (g_go.lv_on)
		logf_("LOADVOICES END loads muted %llu (%.1f s in all), not known %llu, the game's sets held %llu; %s",
		      (unsigned long long)g_go.n_lv_loads, g_go.lv_s, (unsigned long long)g_go.n_lv_unknown,
		      (unsigned long long)atomic_load(&g_go.n_lv_held),
		      atomic_load(&g_go.lv_muted) ? "MUTED at the end (the game's own value not sent back)" : "not muted at the end");
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
