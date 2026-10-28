/* somvr_cprompt_game.c - build 18 (docs/re-xr-pipeline.md 62.8): the game's counter icon in VR. Included by somvr.c
 * after the reticle's file (it uses bo_patch_call, logf_, env_word, cb_now).
 *
 * SOMVR_COUNTER_ICON=game|hide (default game = nothing patched) - the icon over an attacking orc (a white or red flare
 *   with the counter's button, "Y") is made by FUN_026e5f90 at the attack's BPW key (re-combat.md 3.3):
 *   - the prompt FX (the orc's AttackTypeFX by its AttackType, drawn in the 3D world: the flare in both eyes) by its call
 *     of the ClientFX create FUN_00d8c7f0 at 0x26e67d7 into attacker+0x468;
 *   - the nav marker at attacker+0x480 (the HUD's button glyph at the orc's screen point: on our HUD quad), switched on
 *     by its call FUN_00ec0ef0(marker, 1) at 0x26e6420.
 *   hide: the create answers 0 (FUN_00d8c7f0's own answer with ClientFX off: no handle, nothing to end later) and the
 *   marker is switched with 0 (its off path: unlinked if it was on, else nothing). The parry window, the counter's own
 *   rules, DEFENCE and THREAT's warnings (the VR cues: the warm glow, the red warning) are untouched. The user's
 *   decision (10-27): the counter icon hidden for now.
 * Lines: CONFIG counter icon=..; CPROMPT t=.. (the first 8 of each kind); CPROMPT END. Trigger word "cprompt
 * game|hide|state" (live; only with the calls hooked: boot with SOMVR_COUNTER_ICON=hide). Fail soft: a site mismatch =
 * the game's icon. */

/* CPROMPT_SITES_BEGIN (check_sites.py counts this block on its own: two patched K_CALLs in FUN_026e5f90) */
static const struct site_spec g_cp_sites_game[] = {
SITE("CP_FX", K_CALL, 0x26e67d7, 0x26e67c5, "48 8d b5 68 04 00 00 48 8b b8 78 0c 08 00 48 83 c7 30 e8 14 60 6a fe", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00"),
SITE("CP_MARK", K_CALL, 0x26e6420, 0x26e640f, "48 8b bd 80 04 00 00 48 85 ff 74 0a be 01 00 00 00 e8 cb aa 7d fe", 0xec0ef0, "55 53 48 89 fb 48 83 ec 18 40 84 f6 75 72 80 bf"),
};
/* CPROMPT_SITES_END */
static const struct site_spec *g_cp_sites = g_cp_sites_game; /* selftest: stand-ins */

enum { CP_GAME, CP_HIDE };
static const char *const g_cp_mode_w[] = { "game", "hide" };
typedef uint64_t (*cp_fx_fn)(uintptr_t, uintptr_t, uintptr_t, uint64_t, uint64_t, uint64_t);
typedef void (*cp_mark_fn)(uintptr_t, int);

static struct {
	int want, ok_fx, ok_mark;
	_Atomic int mode;
	_Atomic uint64_t n_fx, n_fx_hid, n_mark, n_mark_hid;
	_Atomic uint32_t lines;
} g_cp;

/* CP_FX: the prompt FX's create (the game thread) */
__attribute__((used)) uint64_t somvr_w_cpfx(uintptr_t mgr, uintptr_t out, uintptr_t cs, uint64_t a, uint64_t b, uint64_t c)
{
	atomic_fetch_add(&g_cp.n_fx, 1);
	if (atomic_load(&g_cp.mode) != CP_HIDE)
		return ((cp_fx_fn)g_cp_sites[0].target)(mgr, out, cs, a, b, c);
	atomic_fetch_add(&g_cp.n_fx_hid, 1);
	if (atomic_fetch_add(&g_cp.lines, 1) < 8)
		logf_("CPROMPT t=%.3f the counter icon's FX: not made (SOMVR_COUNTER_ICON=hide: the create's own 0)", cb_now());
	return 0;
}

/* CP_MARK: the prompt's nav marker switched on (the game thread) */
__attribute__((used)) void somvr_w_cpmark(uintptr_t marker, int on)
{
	atomic_fetch_add(&g_cp.n_mark, 1);
	if (on && atomic_load(&g_cp.mode) == CP_HIDE) {
		on = 0;
		atomic_fetch_add(&g_cp.n_mark_hid, 1);
		if (atomic_fetch_add(&g_cp.lines, 1) < 8)
			logf_("CPROMPT t=%.3f the counter icon's marker %#lx: switched off (SOMVR_COUNTER_ICON=hide)", cb_now(),
			      (unsigned long)marker);
	}
	((cp_mark_fn)g_cp_sites[1].target)(marker, on);
}

static void cp_state_line(const char *head)
{
	logf_("%s t=%.3f: SOMVR_COUNTER_ICON %s (CP_FX %s, CP_MARK %s); prompt FX asked %llu (not made %llu), markers on %llu "
	      "(switched off %llu)", head, cb_now(), g_cp_mode_w[atomic_load(&g_cp.mode)], g_cp.ok_fx ? "hooked" : "not hooked",
	      g_cp.ok_mark ? "hooked" : "not hooked", (unsigned long long)atomic_load(&g_cp.n_fx),
	      (unsigned long long)atomic_load(&g_cp.n_fx_hid), (unsigned long long)atomic_load(&g_cp.n_mark),
	      (unsigned long long)atomic_load(&g_cp.n_mark_hid));
}

/* "cprompt game|hide|state" (writer thread) */
static const char *cp_word(const char *w0)
{
	char w[32];
	snprintf(w, sizeof w, "%s", w0);
	for (size_t n = strlen(w); n && (w[n - 1] == '\n' || w[n - 1] == ' ' || w[n - 1] == '\r');)
		w[--n] = 0;
	if (!strcmp(w, "cprompt state") || !strcmp(w, "cprompt")) {
		cp_state_line("CPROMPT state");
		return "cprompt (a state line)";
	}
	int m = !strcmp(w, "cprompt game") ? CP_GAME : !strcmp(w, "cprompt hide") ? CP_HIDE : -1;
	if (m < 0)
		return "cprompt (not game|hide|state: ignored)";
	if (!g_cp.ok_fx && !g_cp.ok_mark)
		return "cprompt (the calls are not hooked: boot with SOMVR_COUNTER_ICON=hide)";
	atomic_store(&g_cp.mode, m);
	logf_("CPROMPT t=%.3f the word: %s from the next counter icon", cb_now(), g_cp_mode_w[m]);
	return "cprompt (from the next icon)";
}

/* build 19 (re-xr-pipeline.md 73): the VR menu's Counter icon row: shown with SOMVR_COUNTER_ICON=hide hooked; Shown =
 * the game's icon again, Hidden = never made (from the next icon, as the word) */
static uint64_t cp_vm_avail(void) { return g_cp.ok_fx || g_cp.ok_mark ? SOMVR_VMA_CICON : 0u; }
static void cp_vm_settings(somvr_settings *s) { s->counter_icon = atomic_load(&g_cp.mode) == CP_GAME; }
static const char *cp_vm_set(const somvr_settings *s)
{
	if (!cp_vm_avail())
		return " (SOMVR_COUNTER_ICON=game: the game's icon, not hooked)";
	atomic_store(&g_cp.mode, s->counter_icon ? CP_GAME : CP_HIDE);
	return s->counter_icon ? " (the game's icon from the next one)" : " (no icon from the next one)";
}

static void cp_start(const struct site_spec *sites, int *bad)
{
	(void)bad;
	memset(&g_cp, 0, sizeof g_cp);
	g_cp_sites = sites;
	g_cp.want = env_word("SOMVR_COUNTER_ICON", g_cp_mode_w, 2, CP_GAME);
	if (g_cp.want == CP_GAME)
		return;
	g_cp.ok_fx = bo_patch_call(&sites[0], (void *)somvr_w_cpfx);
	g_cp.ok_mark = bo_patch_call(&sites[1], (void *)somvr_w_cpmark);
	atomic_store(&g_cp.mode, g_cp.ok_fx || g_cp.ok_mark ? g_cp.want : CP_GAME);
	logf_("CONFIG counter icon=%s (SOMVR_COUNTER_ICON: the game's icon over an attacking orc - its flare FX %s, its HUD "
	      "marker %s; the VR cues are THREAT's glows and the red warning)", g_cp_mode_w[g_cp.want],
	      g_cp.ok_fx ? "never made" : "NOT hooked (the game's)", g_cp.ok_mark ? "kept off" : "NOT hooked (the game's)");
}

static void cp_end_log(void)
{
	if (g_cp.ok_fx || g_cp.ok_mark)
		cp_state_line("CPROMPT END");
}
