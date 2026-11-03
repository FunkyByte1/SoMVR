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
 * the game's icon.
 * Build 31b (re-xr-pipeline.md 139.2, the 11-02 session's "Y prompt"): with the marker kept off the glyph (a gold Y in
 * a ring at the orc's flat-screen point) still reached our HUD quad at each counter window - the marker's set-up before
 * the switch (FUN_026e5f90 with the game's CombatPrompts option +0x1f4 at 2: its record FUN_00ec07f0(marker, +0x4a0) at
 * 0x26e63db, its target, its timed show FUN_00ebf660(marker, windowMs, 0, 0) at 0x26e63fd) shows it on its own.
 * SOMVR_COUNTER_ICON_SETUP=skip|game (code default skip; only with hide): those two calls skipped too (sites CP_REC,
 * CP_SHOW). Words "cprompt setup skip|game" (live), "cprompt dumps N" (a test: the layer's dump of the frame after each
 * of the next N marker calls, SOMVR_XR_DUMP_MARKED). */

/* CPROMPT_SITES_BEGIN (check_sites.py counts this block on its own: four patched K_CALLs in FUN_026e5f90) */
static SITE_TABLE struct site_spec g_cp_sites_game[] = {
SITE("CP_FX", K_CALL, 0x26e67d7, 0x26e67c5, "48 8d b5 68 04 00 00 48 8b b8 78 0c 08 00 48 83 c7 30 e8 14 60 6a fe", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00"),
SITE("CP_MARK", K_CALL, 0x26e6420, 0x26e640f, "48 8b bd 80 04 00 00 48 85 ff 74 0a be 01 00 00 00 e8 cb aa 7d fe", 0xec0ef0, "55 53 48 89 fb 48 83 ec 18 40 84 f6 75 72 80 bf"),
SITE("CP_REC", K_CALL, 0x26e63db, 0x26e63d1, "48 8b b5 a0 04 00 00 4c 89 ef e8 10 a4 7d fe", 0xec07f0, "48 8b 87 30 02 00 00 48 89 c2 48 85 c0 0f 84 2d"),
SITE("CP_SHOW", K_CALL, 0x26e63fd, 0x26e63ef, "48 8b bd 80 04 00 00 31 c9 31 d2 44 89 e6 e8 5e 92 7d fe", 0xebf660, "41 56 41 55 41 89 cd 41 54 41 89 d4 55 89 f5 53"),
};
/* CPROMPT_SITES_END */
static const struct site_spec *g_cp_sites = g_cp_sites_game; /* selftest: stand-ins */

enum { CP_GAME, CP_HIDE };
static const char *const g_cp_mode_w[] = { "game", "hide" };
typedef uint64_t (*cp_fx_fn)(uintptr_t, uintptr_t, uintptr_t, uint64_t, uint64_t, uint64_t);
typedef void (*cp_mark_fn)(uintptr_t, int);
typedef void (*cp_rec_fn)(uintptr_t, uintptr_t);
typedef void (*cp_show_fn)(uintptr_t, int, int, int);

static struct {
	int want, ok_fx, ok_mark, ok_setup;
	_Atomic int setup_skip;         /* build 31b: SOMVR_COUNTER_ICON_SETUP=skip */
	_Atomic int dumps;              /* build 31b (a test): dumps left at the marker's calls */
	_Atomic uint64_t n_rec, n_rec_skip, n_show, n_show_skip;
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
	if (atomic_load(&g_cp.dumps) > 0) { /* a test: the next frame in a picture (the glyph's window is ~0.25 s) */
		atomic_fetch_sub(&g_cp.dumps, 1);
		hf_dump_mark("cprompt marker", cb_now());
	}
}
/* build 31b, CP_REC / CP_SHOW: the marker's record and its timed show (the game thread) - skipped with hide + setup skip */
static int cp_setup_skip(void) { return atomic_load(&g_cp.mode) == CP_HIDE && atomic_load(&g_cp.setup_skip); }
__attribute__((used)) void somvr_w_cprec(uintptr_t marker, uintptr_t rec)
{
	atomic_fetch_add(&g_cp.n_rec, 1);
	if (cp_setup_skip()) {
		atomic_fetch_add(&g_cp.n_rec_skip, 1);
		return;
	}
	((cp_rec_fn)g_cp_sites[2].target)(marker, rec);
}
__attribute__((used)) void somvr_w_cpshow(uintptr_t marker, int ms, int a, int b)
{
	atomic_fetch_add(&g_cp.n_show, 1);
	if (cp_setup_skip()) {
		if (atomic_fetch_add(&g_cp.n_show_skip, 1) < 4)
			logf_("CPROMPT t=%.3f the counter glyph's set-up skipped (marker %#lx, its record and its %d ms show: "
			      "SOMVR_COUNTER_ICON_SETUP=skip)", cb_now(), (unsigned long)marker, ms);
		return;
	}
	((cp_show_fn)g_cp_sites[3].target)(marker, ms, a, b);
}

static void cp_state_line(const char *head)
{
	logf_("%s t=%.3f: SOMVR_COUNTER_ICON %s (CP_FX %s, CP_MARK %s, CP_REC/CP_SHOW %s); prompt FX asked %llu (not made %llu), "
	      "markers on %llu (switched off %llu); build 31b: the glyph's set-up %llu/%llu (skipped %llu/%llu: "
	      "SOMVR_COUNTER_ICON_SETUP %s)", head, cb_now(), g_cp_mode_w[atomic_load(&g_cp.mode)],
	      g_cp.ok_fx ? "hooked" : "not hooked", g_cp.ok_mark ? "hooked" : "not hooked",
	      g_cp.ok_setup ? "hooked" : "not hooked", (unsigned long long)atomic_load(&g_cp.n_fx),
	      (unsigned long long)atomic_load(&g_cp.n_fx_hid), (unsigned long long)atomic_load(&g_cp.n_mark),
	      (unsigned long long)atomic_load(&g_cp.n_mark_hid), (unsigned long long)atomic_load(&g_cp.n_rec),
	      (unsigned long long)atomic_load(&g_cp.n_show), (unsigned long long)atomic_load(&g_cp.n_rec_skip),
	      (unsigned long long)atomic_load(&g_cp.n_show_skip), atomic_load(&g_cp.setup_skip) ? "skip" : "game");
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
	if (!strcmp(w, "cprompt setup skip") || !strcmp(w, "cprompt setup game")) { /* build 31b (live) */
		atomic_store(&g_cp.setup_skip, w[14] == 's');
		logf_("CPROMPT t=%.3f the word: SOMVR_COUNTER_ICON_SETUP %s", cb_now(), w[14] == 's' ? "skip" : "game");
		return "cprompt setup";
	}
	int nd = 0;
	if (sscanf(w, "cprompt dumps %d", &nd) == 1 && nd >= 0 && nd <= 20) /* build 31b (a test) */
		return atomic_store(&g_cp.dumps, nd), "cprompt dumps (the frame after each marker call)";
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
	atomic_store(&g_cp.setup_skip, env_word("SOMVR_COUNTER_ICON_SETUP", (const char *const[]){ "game", "skip" }, 2, 1));
	g_cp.ok_setup = bo_patch_call(&sites[2], (void *)somvr_w_cprec) && bo_patch_call(&sites[3], (void *)somvr_w_cpshow);
	atomic_store(&g_cp.mode, g_cp.ok_fx || g_cp.ok_mark ? g_cp.want : CP_GAME);
	logf_("CONFIG counter icon=%s (SOMVR_COUNTER_ICON: the game's icon over an attacking orc - its flare FX %s, its HUD "
	      "marker %s, the glyph's set-up %s; the VR cues are THREAT's glows and the red warning)", g_cp_mode_w[g_cp.want],
	      g_cp.ok_fx ? "never made" : "NOT hooked (the game's)", g_cp.ok_mark ? "kept off" : "NOT hooked (the game's)",
	      !g_cp.ok_setup ? "NOT hooked (the game's: its glyph shows)" : atomic_load(&g_cp.setup_skip) ?
	      "skipped (SOMVR_COUNTER_ICON_SETUP=skip: no glyph on the HUD)" : "the game's (SOMVR_COUNTER_ICON_SETUP=game)");
}

static void cp_end_log(void)
{
	if (g_cp.ok_fx || g_cp.ok_mark)
		cp_state_line("CPROMPT END");
}
