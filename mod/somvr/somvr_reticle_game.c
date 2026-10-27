/* somvr_reticle_game.c - build 17 (docs/re-xr-pipeline.md 61.3): the game's bow reticle in VR. Included by somvr.c
 * after the quiver's file (it uses check_site, go_slot, write_code, logf_, env_word, cb_now).
 *
 * SOMVR_BOW_RETICLE=game|hide (default game = nothing patched) - the bow's aim reticle is the HUD element
 *   CUIHUDCrosshair (re-ui-effects.md 3.3, "Bow aiming showed two cursors"). CPlayerUI casts CrosshairSetParams
 *   {+0 int state} to it through TypedDelegate<CUIHUDCrosshair, CPlayerUI, CrosshairSetParams, OnCrosshairSet>, whose
 *   vtable slot 2 (0xe6bb60) stores the state at the element's +0x2d0 and, state != 0, shows it (FUN_00e69460,
 *   CUIHUDBase's FUN_00eee370(x, 1), the update list FUN_00c221f0(0, x + 0xe8, 0)), else hides it (FUN_00eee370(x, 0),
 *   FUN_00c223b0(x + 0xe8)). In VR the HUD is a quad in front of the eyes and the reticle sits where the flat camera
 *   projects the aim: a mark that starts in the middle of the view and has nothing to do with the bow in the hand (the
 *   user's build 16 session 20261027-090356, mark 2).
 *   hide: every set reaches the game as state 0 - the game's own hide path, nothing else touched. The sibling delegate
 *   OnCrosshairModifier (slot 2 at 0xe6b6e0: the element's Drawing / Held / None animation and the WRA_BowCharge sound)
 *   still runs on the hidden element. The aim is the arrow on the string, as with a real bow.
 * Lines: CONFIG bow reticle=..; RETICLE t=.. the game sets the crosshair N: shown|hidden (the first 12, then counted);
 * RETICLE END sets N (shows asked N, turned into hides M). Trigger word "reticle game|hide|state" (live; a crosshair
 * already up stays until the game's next set - the next aim). Fail soft: a site mismatch = the game's crosshair. */

/* RETICLE_SITES_BEGIN (check_sites.py counts this block on its own: one patched K_VSLOT, slot 2 of the
 * TypedDelegate<CUIHUDCrosshair, .., CrosshairSetParams, OnCrosshairSet> vtable 0x35a3670; the context: slots 0..3) */
static const struct site_spec g_xh_sites_game[] = {
SITE("XH_SET", K_VSLOT, 0x35a3680, 0x35a3670, "d0 b2 e6 00 00 00 00 00 50 b5 e6 00 00 00 00 00 60 bb e6 00 00 00 00 00 10 b5 e6 00 00 00 00 00", 0xe6bb60, "55 53 48 83 ec 08 48 8b 5f 18 48 85 db 74 61 8b"),
};
/* RETICLE_SITES_END */
static const struct site_spec *g_xh_sites = g_xh_sites_game; /* selftest: stand-ins */

enum { XH_GAME, XH_HIDE };
static const char *const g_xh_mode_w[] = { "game", "hide" };
typedef void (*xh_dlg_fn)(void *dlg, void *sender, void *params);

static struct {
	int want;               /* SOMVR_BOW_RETICLE at start (XH_*) */
	_Atomic int mode;       /* now (the word changes it) */
	int ok;                 /* the slot installed */
	uintptr_t orig;
	int shown;              /* the state last passed on (the element shown) - the HUD's thread */
	_Atomic uint64_t n_set, n_show, n_hid;
	_Atomic uint32_t lines;
} g_xh;

__attribute__((used)) void somvr_w_xhset(void *dlg, void *sender, void *params)
{
	int state = params ? *(const int *)params : 0, hide = state != 0 && atomic_load(&g_xh.mode) == XH_HIDE;
	atomic_fetch_add(&g_xh.n_set, 1);
	if (state)
		atomic_fetch_add(&g_xh.n_show, 1);
	if (hide)
		atomic_fetch_add(&g_xh.n_hid, 1);
	if (atomic_fetch_add(&g_xh.lines, 1) < 12)
		logf_("RETICLE t=%.3f the game sets the crosshair %d: %s", cb_now(), state,
		      hide ? "hidden (SOMVR_BOW_RETICLE=hide: passed on as 0, the game's own hide)" : state ? "shown" : "hidden");
	if (hide || (state == 0 && atomic_load(&g_xh.mode) == XH_HIDE)) {
		/* hidden already (every set since the start was passed on as 0): nothing to undo, the call skipped - the hide
		 * path never runs twice on a hidden element; otherwise (a crosshair shown before the word) the game's hide */
		if (!g_xh.shown)
			return;
		int zero[4] = { 0, 0, 0, 0 }; /* (the delegate reads only +0; the game's own params stay untouched) */
		((xh_dlg_fn)g_xh.orig)(dlg, sender, zero);
		g_xh.shown = 0;
		return;
	}
	((xh_dlg_fn)g_xh.orig)(dlg, sender, params);
	g_xh.shown = state != 0;
}

static void xh_state_line(const char *head)
{
	logf_("%s t=%.3f: SOMVR_BOW_RETICLE %s (the slot %s); the game's sets %llu (shows asked %llu, turned into hides %llu)",
	      head, cb_now(), g_xh_mode_w[atomic_load(&g_xh.mode)], g_xh.ok ? "hooked" : "not hooked",
	      (unsigned long long)atomic_load(&g_xh.n_set), (unsigned long long)atomic_load(&g_xh.n_show),
	      (unsigned long long)atomic_load(&g_xh.n_hid));
}

/* "reticle game|hide|state" (writer thread) */
static const char *xh_word(const char *w0)
{
	char w[32];
	snprintf(w, sizeof w, "%s", w0);
	for (size_t n = strlen(w); n && (w[n - 1] == '\n' || w[n - 1] == ' ' || w[n - 1] == '\r');)
		w[--n] = 0;
	if (!strcmp(w, "reticle state") || !strcmp(w, "reticle")) {
		xh_state_line("RETICLE state");
		return "reticle (a state line)";
	}
	int m = !strcmp(w, "reticle game") ? XH_GAME : !strcmp(w, "reticle hide") ? XH_HIDE : -1;
	if (m < 0)
		return "reticle (not game|hide|state: ignored)";
	if (!g_xh.ok)
		return "reticle (the slot is not hooked: boot with SOMVR_BOW_RETICLE=hide)";
	atomic_store(&g_xh.mode, m);
	logf_("RETICLE t=%.3f the word: %s from the game's next set", cb_now(), g_xh_mode_w[m]);
	return "reticle (from the game's next set)";
}

static void xh_start(const struct site_spec *sites, int *bad)
{
	(void)bad;
	memset(&g_xh, 0, sizeof g_xh);
	g_xh_sites = sites;
	g_xh.want = env_word("SOMVR_BOW_RETICLE", g_xh_mode_w, 2, XH_GAME);
	if (g_xh.want == XH_GAME)
		return;
	char why[512];
	if (check_site(&sites[0], why, sizeof why) != 0)
		logf_("RETICLE site %s %#lx -> %#lx MISMATCH: %s", sites[0].name, (unsigned long)sites[0].addr,
		      (unsigned long)sites[0].target, why);
	else
		g_xh.ok = go_slot(&sites[0], (void *)somvr_w_xhset, &g_xh.orig);
	atomic_store(&g_xh.mode, g_xh.ok ? g_xh.want : XH_GAME);
	logf_("CONFIG bow reticle=%s (SOMVR_BOW_RETICLE: the game's bow crosshair, a HUD element at the flat camera's "
	      "projection, %s): OnCrosshairSet %s", g_xh_mode_w[g_xh.want],
	      g_xh.want == XH_HIDE ? "never shown - every set passed on as the game's hide; the arrow is the aim" : "the game's",
	      g_xh.ok ? "hooked" : "NOT hooked (the game's crosshair)");
}

static void xh_end_log(void)
{
	if (g_xh.ok)
		xh_state_line("RETICLE END");
}
