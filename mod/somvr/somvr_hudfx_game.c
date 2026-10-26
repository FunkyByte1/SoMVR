/* somvr_hudfx_game.c - SOMVR_HUD_FX (build 12 HUD round, docs/re-xr-pipeline.md 31.5, handoff/HANDOFF-hud.md): the flat
 * HUD's 3D flourishes out of the eyes. Included by somvr.c after the execution's file (it uses mono, check_site,
 * stub_reach, write_code, peek, logf_).
 *
 * Why the hit streak's flare and the XP bar's cyan line reach both eyes (static RE, som-linux, the decompiles in
 * re-xr-pipeline.md 31.5): the HUD's Scaleform parts are in the UI surface (our HUD quad, the watch, the band: build 8;
 * the streak's `x N` is on the watch with SOMVR_XR_WRIST_HUD=on), but an element's flourish is a "UI FX group":
 * ClientFX sequences (CClientFXSequence, FUN_00d8f450) made for an element's state (CBaseUI::CreateUIFXGroup 0xcbcad0 ->
 * FUN_00cbbe60(manager = *(ui+8) + 0x68, desc), its log line "CreateUIFXGroup: GROUPID %d" = desc+0x18) and drawn in the
 * 3D world, with its bloom. Every view places them again (FUN_00cbd260 per view -> FUN_00cbcae0 -> per FX FUN_00cba630):
 * the manager's anchor object takes the view's transform (objsys vtbl+0xb0) and each FX sits at its clip's screen point
 * unprojected at its depth (FUN_00cba450). Each eye is a view: the same pixel in both eyes = head-locked at infinity.
 *   SOMVR_HUD_FX=game (default)  as build 11: nothing patched.
 *   SOMVR_HUD_FX=hide            UF_CREATE (the thunk's tail jump 0xcbcad8) is ours: a group asked for while the game
 *                                renders our eyes (a stereo H4 frame, or a SOMVR_BODYMOMENTS window: its groups
 *                                would outlive it, within the last UF_STEREO_S) is not made: 0, the game's own answer with UI FX off (FUN_00cbbe60's gates: the manager +0x60, the static
 *                                DAT_046b614c, the manager's byte +0x68). Menus, loading and flat frames make theirs.
 * Test word: "hudfx game|hide" (from the next group on; a group already made plays out). Lines: `CONFIG hud_fx=...`,
 * `HUDFX group N ...` (the first UF_LINES and every 50th), `HUDFX END ...`. */

/* HUDFX_SITES_BEGIN (check_sites.py counts this block on its own: the patched tail jump of CBaseUI::CreateUIFXGroup,
 * FUN_00cbcad0 = mov 0x8(%rdi),%rdi; add $0x68,%rdi; jmp FUN_00cbbe60) */
static const struct site_spec g_uf_sites_game[] = {
SITE("UF_CREATE", K_JMP, 0xcbcad8, 0xcbcad0, "48 8b 7f 08 48 83 c7 68 e9 83 f3 ff ff", 0xcbbe60, "41 57 41 56 41 55 41 54 55 53 48 81 ec f8 09 00 00 48 83 7f 60 00"),
};
/* HUDFX_SITES_END */
static const struct site_spec *g_uf_sites = g_uf_sites_game; /* selftest: a stand-in */
typedef uint64_t (*uf_create_fn)(uintptr_t mgr, uintptr_t desc);
#define UF_DESC_GROUP 0x18 /* the desc's GROUPID (FUN_00cbbe60's log line) */
#define UF_STEREO_S 0.5    /* a stereo H4 frame this recent = the eyes are ours */
#define UF_LINES 40

static struct {
	int installed;
	_Atomic int mode;           /* 0 game, 1 hide (the word may change it) */
	uintptr_t orig;
	_Atomic double stereo_t;    /* the last stereo H4 frame or window (mono), 0 = a flat frame since */
	uint64_t calls, hidden, made, refused; /* refused: the game's own 0 */
	int32_t last_group;
	uint32_t lines;
} g_uf;
static const char *const g_uf_w[] = { "game", "hide" };

/* H4 (the game thread): this frame renders our eyes or a body moment's window (1) or the flat game (0) */
static void uf_frame(int play)
{
	if (g_uf.installed)
		atomic_store_explicit(&g_uf.stereo_t, play ? mono() : 0.0, memory_order_relaxed);
}

/* the tail jump (the game thread, an element's state change): rdi = the manager, rsi = the desc */
__attribute__((used)) uint64_t somvr_w_hudfx(uintptr_t mgr, uintptr_t desc)
{
	double t = mono(), st = atomic_load_explicit(&g_uf.stereo_t, memory_order_relaxed);
	int eyes = st > 0 && t - st < UF_STEREO_S, hide = eyes && atomic_load(&g_uf.mode) == 1;
	int32_t gid = -1;
	(void)peek(desc + UF_DESC_GROUP, &gid, sizeof gid);
	uint64_t n = ++g_uf.calls, r = 0;
	if (hide)
		g_uf.hidden++;
	else
		r = ((uf_create_fn)g_uf.orig)(mgr, desc), (r & 0xff) ? g_uf.made++ : g_uf.refused++;
	g_uf.last_group = gid;
	if (g_uf.lines < UF_LINES || n % 50 == 0)
		g_uf.lines++, logf_("HUDFX group %d (call %llu, manager %#lx): %s", gid, (unsigned long long)n, (unsigned long)mgr,
				    hide ? "not made (our eyes: hide)" : (r & 0xff) ? (eyes ? "made (in our eyes: game)" : "made (a flat frame)")
								    : "the game's own 0 (UI FX off)");
	return r;
}

/* the stub page for the tail jump (as sl_install's), the rel32 written; 1 = in */
static int uf_install(const struct site_spec *s)
{
	char why[512] = "", err[200] = "", nb[64];
	int ok = s->kind == K_JMP && check_site(s, why, sizeof why) == 0;
	if (s->kind != K_JMP)
		snprintf(why, sizeof why, "not a jmp site");
	unsigned char *pg = NULL;
	for (int i = 0; ok && i < 64 + 4 && !pg; i++) {
		static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
		uintptr_t at = i < 64 ? (uintptr_t)0x3c0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
		void *p = mmap((void *)at, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED)
			continue;
		if (stub_reach((uintptr_t)p, s->addr))
			memset(pg = p, 0xcc, 4096);
		else
			munmap(p, 4096);
	}
	if (ok && !pg)
		ok = 0, snprintf(why, sizeof why, "no stub page within +-2 GB");
	if (ok) {
		static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
		uintptr_t tgt = (uintptr_t)somvr_w_hudfx;
		memcpy(pg, jmp, 6);
		memcpy(pg + 6, &tgt, 8);
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			ok = 0, snprintf(why, sizeof why, "stub mprotect rx: %s", strerror(errno));
	}
	g_uf.orig = s->target; /* before the jump can reach somvr_w_hudfx */
	int32_t rel = ok ? (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(s->addr + 5)) : 0;
	if (!ok || write_code(s->addr + 1, &rel, 4, err, sizeof err) != 0) {
		logf_("HOOK %s SKIPPED %#lx: %s%s", s->name, (unsigned long)s->addr, ok ? "write failed: " : "", ok ? err : why);
		if (pg)
			munmap(pg, 4096);
		return 0;
	}
	hex_of((const unsigned char *)s->addr, 5, nb, sizeof nb);
	logf_("HOOK %s ok jmp %#lx target=%#lx now=[%s] via=%#lx (CBaseUI::CreateUIFXGroup: the HUD's 3D flourishes)", s->name,
	      (unsigned long)s->addr, (unsigned long)s->target, nb, (unsigned long)pg);
	return 1;
}

/* somvr_start: the switch, the site, the config line */
static void uf_start(const struct site_spec *sites, int *bad)
{
	(void)bad;
	memset(&g_uf, 0, sizeof g_uf);
	g_uf_sites = sites;
	int want = env_word("SOMVR_HUD_FX", g_uf_w, 2, 0);
	if (!want)
		return;
	if (!uf_install(&sites[0])) {
		logf_("WARN SOMVR_HUD_FX=hide: UF_CREATE not hooked: the HUD's flourishes stay in the eyes");
		return;
	}
	g_uf.installed = 1;
	atomic_store(&g_uf.mode, want);
	logf_("CONFIG hud_fx=hide (SOMVR_HUD_FX: the HUD's UI FX groups - the hit streak's flare, the XP bar's line - not made "
	      "while the game renders our eyes (a stereo frame within %.1f s); menus and flat frames keep theirs)", UF_STEREO_S);
}

/* the writer thread: "hudfx game|hide" */
static const char *uf_word(const char *w)
{
	if (!g_uf.installed)
		return "hudfx (SOMVR_HUD_FX is off: nothing patched)";
	for (int i = 0; i < 2; i++)
		if (!strncmp(w, g_uf_w[i], strlen(g_uf_w[i]))) {
			atomic_store(&g_uf.mode, i);
			return i ? "hudfx hide (the next groups in our eyes not made)" : "hudfx game (the next groups made)";
		}
	return "hudfx (not game|hide: ignored)";
}

static void uf_end_log(void)
{
	if (!g_uf.installed)
		return;
	logf_("HUDFX END groups asked %llu: not made in our eyes %llu, made %llu, the game's own 0 %llu; the last GROUPID %d; "
	      "now %s", (unsigned long long)g_uf.calls, (unsigned long long)g_uf.hidden, (unsigned long long)g_uf.made,
	      (unsigned long long)g_uf.refused, g_uf.last_group, g_uf_w[atomic_load(&g_uf.mode)]);
}
