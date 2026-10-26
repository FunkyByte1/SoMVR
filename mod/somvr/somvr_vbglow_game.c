/* somvr_vbglow_game.c - SOMVR_VERBS_GLOW (build 13, docs/re-xr-pipeline.md 39.7, handoff/HANDOFF-verbs.md): the wraith's
 * blinding flare at the verbs' grab out of the eyes. Included by somvr.c after the HUD FX file (it uses g_vb, mono,
 * check_site, stub_reach, write_code, peek, logf_).
 *
 * Why: with SOMVR_VERBS=on the left hand holds the orc ~30 cm from the eyes (the interrogation's pull: 25 cm), and the
 * game's own wraith FX of the grab, the drain and the interrogation are ClientFX on Talion's left hand (L_Hand, which
 * SOMVR_WISPS=hand puts on our drawn hand) or the orc's head. The flat camera sees them small from behind Talion; in
 * first person they fill the middle of both eyes (run 20261026-041848 frame 1110). Every ClientFX the game makes goes
 * through FUN_00d8bae0 (CClientFXMgr's create; re-combat-items.md 2.1), reached only by the tail jumps of its two thunks:
 * FUN_00d8c7f0 (70 callers: mgr, &handle, cs, start, b, c) and FUN_00d8c810 (the same with start 1, b 1, c 0). The
 * CLIENTFX_CREATESTRUCT (FUN_00a09e70's ctor): +0 the FX file name, +0x104 flags (1 loop), +0x108 the parent HOBJECT,
 * +0x118 the node (-1 = the object), +0x11c the offset, +0x128 its rotation.
 *   SOMVR_VERBS_GLOW=game (default)  nothing patched.
 *   SOMVR_VERBS_GLOW=log             both tail jumps are ours; each FX made on Talion or the held orc in the verbs'
 *                                    moment (the left hand holds, the game's wraith hold, the interrogation; VG_AFTER_S
 *                                    after) and each new name is logged: its name, its parent (Talion, the held orc,
 *                                    other), its node, flags, the caller. Nothing changed.
 *   SOMVR_VERBS_GLOW=hide            as log, and an FX whose file name has one of SOMVR_VERBS_GLOW_FX's words (comma-
 *                                    separated, VG_FX_DEFAULT without it) is not made while the game renders our eyes
 *                                    (a stereo frame within VG_STEREO_S): 0, the game's own answer with ClientFX off
 *                                    (FUN_00d8c7f0's gate byte 0x46b72d1). The other FX of the moment stay.
 * Test word: "vbglow game|log|hide|fx WORDS" (from the next FX on). Lines: `CONFIG verbs_glow=...`, `VBGLOW t=... fx
 * '...' ...` (the moment's FX and every new name, up to VG_LINES), `VBGLOW END ...`. */

/* VBGLOW_SITES_BEGIN (check_sites.py counts this block on its own: the patched tail jumps of ClientFX's two create
 * thunks FUN_00d8c7f0 and FUN_00d8c810 into FUN_00d8bae0) */
static const struct site_spec g_vg_sites_game[] = {
SITE("VG_FXC", K_JMP, 0xd8c807, 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00 0f b6 c9 45 0f b6 c0 e9 d4 f2 ff ff", 0xd8bae0, "41 57 41 56 41 55 41 54 49 89 f4 55 48 89 d5 53 48 89 fb 48 89 d7 48 81"),
SITE("VG_FXC1", K_JMP, 0xd8c834, 0xd8c810, "80 3d ba aa 92 03 00 48 89 d0 75 04 31 c0 c3 90 48 89 f2 45 31 c9 b9 01 00 00 00 48 89 c6 41 b8 01 00 00 00 e9 a7 f2 ff ff", 0xd8bae0, "41 57 41 56 41 55 41 54 49 89 f4 55 48 89 d5 53 48 89 fb 48 89 d7 48 81"),
};
/* VBGLOW_SITES_END */
static const struct site_spec *g_vg_sites = g_vg_sites_game; /* selftest: stand-ins */
typedef uint64_t (*vg_create_fn)(uintptr_t mgr, uintptr_t out, uintptr_t cs, uint64_t start, uint64_t b, uint64_t c);
#define VG_CS_NAME 0x000   /* CLIENTFX_CREATESTRUCT: the FX file name (0x104 chars) */
#define VG_CS_FLAGS 0x104  /* 1 = loop, 2 = no smooth shutdown */
#define VG_CS_PARENT 0x108 /* the parent HOBJECT */
#define VG_CS_NODE 0x118   /* the parent's node (-1 = its root) */
#define VG_CS_OFF 0x11c    /* the offset from it (cm, 3 floats) */
#define VG_STEREO_S 0.5    /* a stereo H4 frame this recent = the eyes are ours */
#define VG_AFTER_S 1.5     /* the verbs' moment's FX logged this long after it */
#define VG_LINES 300
#define VG_WORDS 8
#define VG_NAMES 64        /* the names seen (each logged once outside the moment) */
/* the flare: Talion's own FX of the interrogation (and the brand), on his left hand (run 20261026-061644: made at the pull
 * and at its Idle, flags 2 / 1 loop); the orc's domination_brand_in/_loop, the wraith world's pres_wraith_* and the drains'
 * hog_* stay */
#define VG_FX_DEFAULT "domination_in.fxp,domination_loop.fxp"

enum { VG_GAME, VG_LOG, VG_HIDE };
static struct {
	int installed;
	_Atomic int mode;              /* VG_LOG / VG_HIDE (the word may change it) */
	uintptr_t orig;
	_Atomic double stereo_t;       /* the last stereo H4 frame, 0 = a flat frame since */
	_Atomic double moment_t;       /* the verbs' moment, last seen (the game thread's H4) */
	_Atomic uintptr_t me, held;    /* Talion's and the held orc's HOBJECTs (H4) */
	char words[VG_WORDS][48];      /* the hidden FX's name words (the writer thread's word: a seqlock-free swap of n) */
	_Atomic int nwords;
	uint32_t names[VG_NAMES];      /* the names' hashes seen */
	int nnames;
	uint64_t calls, made, hidden, moment, lines;
	char last_hidden[96];
} g_vg;
static const char *const g_vg_w[] = { "game", "log", "hide" };

static char vg_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static void vg_set_words(const char *s)
{
	int n = 0;
	atomic_store(&g_vg.nwords, 0);
	while (s && *s && n < VG_WORDS) {
		while (*s == ',' || *s == ' ')
			s++;
		size_t k = strcspn(s, ", ");
		if (k && k < sizeof g_vg.words[0]) {
			for (size_t i = 0; i < k; i++)
				g_vg.words[n][i] = vg_lower(s[i]);
			g_vg.words[n++][k] = 0;
		}
		s += k;
	}
	atomic_store(&g_vg.nwords, n);
}

/* the FX file name has one of the words (case-blind); its index + 1, 0 = none */
static int vg_match(const char *name)
{
	char low[0x104];
	size_t i = 0;
	for (; name[i] && i < sizeof low - 1; i++)
		low[i] = vg_lower(name[i]);
	low[i] = 0;
	int n = atomic_load(&g_vg.nwords);
	for (int k = 0; k < n; k++)
		if (g_vg.words[k][0] && strstr(low, g_vg.words[k]))
			return k + 1;
	return 0;
}

/* H4 (the game thread): this frame renders our eyes (1) or the flat game (0); the verbs' moment and its HOBJECTs */
static void vg_frame(int play)
{
	if (!g_vg.installed)
		return;
	double t = mono();
	atomic_store_explicit(&g_vg.stereo_t, play ? t : 0.0, memory_order_relaxed);
	if (!play)
		return;
	int k = g_vb.kind;
	if (g_vb.mode == VB_ON && (g_vb.hand.phase != SOMVR_VBH_IDLE || g_vb.ask_c || k == SOMVR_VBK_GRAB ||
				   k == SOMVR_VBK_HOG || k == SOMVR_VBK_ASK))
		atomic_store_explicit(&g_vg.moment_t, t, memory_order_relaxed);
	uintptr_t pl = g_fr.pl, pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	uintptr_t held = g_vb.held_c ? g_vb.held_c : g_vb.ask_c;
	atomic_store_explicit(&g_vg.me, pc ? peek_ptr(pc + C_HOBJ) : 0, memory_order_relaxed);
	atomic_store_explicit(&g_vg.held, held ? peek_ptr(held + C_HOBJ) : 0, memory_order_relaxed);
}

/* both tail jumps (any thread the game makes FX on): the create's own registers */
__attribute__((used)) uint64_t somvr_w_vbglow(uintptr_t mgr, uintptr_t out, uintptr_t cs, uint64_t start, uint64_t b,
					      uint64_t c)
{
	double t = mono(), st = atomic_load_explicit(&g_vg.stereo_t, memory_order_relaxed);
	int eyes = st > 0 && t - st < VG_STEREO_S, mode = atomic_load(&g_vg.mode);
	int moment = t - atomic_load_explicit(&g_vg.moment_t, memory_order_relaxed) < VG_AFTER_S;
	unsigned char cs_[VG_CS_OFF + 12] = { 0 }; /* the struct's head, one read (a stack struct >= 0x1c8 bytes) */
	if (cs)
		(void)peek(cs, cs_, sizeof cs_);
	char name[0x104];
	memcpy(name, cs_ + VG_CS_NAME, sizeof name - 1);
	name[sizeof name - 1] = 0;
	uint32_t flags;
	int32_t node;
	float off[3];
	uintptr_t parent;
	memcpy(&flags, cs_ + VG_CS_FLAGS, sizeof flags), memcpy(&node, cs_ + VG_CS_NODE, sizeof node);
	memcpy(off, cs_ + VG_CS_OFF, sizeof off), memcpy(&parent, cs_ + VG_CS_PARENT, sizeof parent);
	int hit = mode == VG_HIDE && eyes ? vg_match(name) : 0;
	g_vg.calls++;
	uint64_t r = 0;
	if (hit)
		g_vg.hidden++, snprintf(g_vg.last_hidden, sizeof g_vg.last_hidden, "%.95s", name);
	else
		r = ((vg_create_fn)g_vg.orig)(mgr, out, cs, start, b, c), g_vg.made++;
	g_vg.moment += moment;
	/* the line: every FX not made, the moment's FX on Talion or the held orc, and each name once */
	uint32_t h = 2166136261u;
	for (const char *p = name; *p; p++)
		h = (h ^ (unsigned char)*p) * 16777619u;
	int seen = g_vg.nnames >= VG_NAMES; /* (the table full: no more first sightings) */
	for (int i = 0; i < g_vg.nnames && !seen; i++)
		seen = g_vg.names[i] == h;
	if (!seen)
		g_vg.names[g_vg.nnames++] = h;
	uintptr_t me = atomic_load_explicit(&g_vg.me, memory_order_relaxed);
	uintptr_t held = atomic_load_explicit(&g_vg.held, memory_order_relaxed);
	int ours = parent && (parent == me || parent == held); /* (the moment's dust and grass: their first sighting only) */
	if ((hit || (moment && ours) || !seen) && g_vg.lines < VG_LINES) {
		g_vg.lines++;
		logf_("VBGLOW t=%.3f fx '%s' on %s %#lx node %d off %.1f,%.1f,%.1f flags %#x start %d (caller %#lx)%s: %s", t,
		      name, !parent ? "nothing" : parent == me ? "Talion" : parent == held ? "the held orc" : "an object",
		      (unsigned long)parent, node, (double)off[0], (double)off[1], (double)off[2], flags, (int)(start & 0xff),
		      (unsigned long)(uintptr_t)__builtin_return_address(0), moment ? " in the verbs' moment" : "",
		      hit ? "not made (our eyes: hide)" : r ? "made" : "the game's own 0");
	}
	return r;
}

/* one stub page for both tail jumps (as uf_install's), their rel32s written; the number hooked */
static int vg_install(const struct site_spec *s, int n)
{
	char why[512] = "", err[200] = "", nb[64];
	int ok = 1;
	for (int i = 0; i < n && ok; i++)
		if (s[i].kind != K_JMP || s[i].target != s[0].target)
			ok = 0, snprintf(why, sizeof why, "%s: not a jmp site into the create", s[i].name);
		else if (check_site(&s[i], why, sizeof why) != 0) {
			char t[sizeof why];
			snprintf(t, sizeof t, "%s: %s", s[i].name, why);
			memcpy(why, t, sizeof why), ok = 0;
		}
	unsigned char *pg = NULL;
	for (int i = 0; ok && i < 64 + 4 && !pg; i++) {
		static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
		uintptr_t at = i < 64 ? (uintptr_t)0x3a0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
		void *p = mmap((void *)at, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED)
			continue;
		int reach = 1;
		for (int k = 0; k < n; k++)
			reach &= stub_reach((uintptr_t)p, s[k].addr);
		if (reach)
			memset(pg = p, 0xcc, 4096);
		else
			munmap(p, 4096);
	}
	if (ok && !pg)
		ok = 0, snprintf(why, sizeof why, "no stub page within +-2 GB");
	if (ok) {
		static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
		uintptr_t tgt = (uintptr_t)somvr_w_vbglow;
		memcpy(pg, jmp, 6);
		memcpy(pg + 6, &tgt, 8);
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			ok = 0, snprintf(why, sizeof why, "stub mprotect rx: %s", strerror(errno));
	}
	if (!ok) {
		logf_("HOOK VG_FXC, VG_FXC1 SKIPPED: %s", why);
		if (pg)
			munmap(pg, 4096);
		return 0;
	}
	g_vg.orig = s[0].target; /* before a jump can reach somvr_w_vbglow */
	int done = 0;
	for (int i = 0; i < n; i++) {
		int32_t rel = (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(s[i].addr + 5));
		if (write_code(s[i].addr + 1, &rel, 4, err, sizeof err) != 0) {
			logf_("HOOK %s SKIPPED %#lx: write failed: %s", s[i].name, (unsigned long)s[i].addr, err);
			continue;
		}
		done++;
		hex_of((const unsigned char *)s[i].addr, 5, nb, sizeof nb);
		logf_("HOOK %s ok jmp %#lx target=%#lx now=[%s] via=%#lx (ClientFX's create: the verbs' glow)", s[i].name,
		      (unsigned long)s[i].addr, (unsigned long)s[i].target, nb, (unsigned long)pg);
	}
	return done;
}

/* somvr_start (after vb_start): the switch, the sites, the config line */
static void vg_start(const struct site_spec *sites, int *bad)
{
	memset(&g_vg, 0, sizeof g_vg);
	g_vg_sites = sites;
	int want = env_word("SOMVR_VERBS_GLOW", g_vg_w, 3, VG_GAME);
	if (!want)
		return;
	const char *fx = getenv("SOMVR_VERBS_GLOW_FX");
	vg_set_words(fx ? fx : VG_FX_DEFAULT);
	if (g_vb.mode != VB_ON)
		logf_("CONFIG verbs_glow: SOMVR_VERBS is not on (its FX are still logged and hidden by name)");
	int n = vg_install(sites, 2);
	if (n != 2) {
		logf_("WARN SOMVR_VERBS_GLOW=%s: %d of 2 ClientFX create jumps hooked%s", g_vg_w[want], n,
		      n ? " (the FX through the other thunk stay the game's)" : ": the verbs' glow stays the game's");
		if (bad)
			++*bad;
		if (!n)
			return;
	}
	g_vg.installed = 1;
	atomic_store(&g_vg.mode, want);
	char ws[VG_WORDS * 50] = "";
	for (int i = 0; i < atomic_load(&g_vg.nwords); i++)
		snprintf(ws + strlen(ws), sizeof ws - strlen(ws), "%s'%s'", i ? ", " : "", g_vg.words[i]);
	logf_("CONFIG verbs_glow=%s (SOMVR_VERBS_GLOW: ClientFX's create %s; the verbs' moment's FX logged%s%s)", g_vg_w[want],
	      want == VG_HIDE ? "watched" : "logged only", want == VG_HIDE ? "; not made in our eyes: the FX named " : "",
	      want == VG_HIDE ? (ws[0] ? ws : "(no words: none)") : "");
}

/* the writer thread: "vbglow game|log|hide|fx WORDS" */
static const char *vg_word(const char *w)
{
	static char msg[200];
	if (!g_vg.installed)
		return "vbglow (SOMVR_VERBS_GLOW is game: nothing patched)";
	if (!strncmp(w, "fx ", 3)) {
		vg_set_words(w + 3);
		snprintf(msg, sizeof msg, "vbglow fx: %d words (%s)", atomic_load(&g_vg.nwords), w + 3);
		return msg;
	}
	for (int i = 0; i < 3; i++)
		if (!strncmp(w, g_vg_w[i], strlen(g_vg_w[i]))) {
			atomic_store(&g_vg.mode, i ? i : VG_LOG);
			return i == VG_HIDE ? "vbglow hide (the next named FX in our eyes not made)" : "vbglow log (the next FX made)";
		}
	return "vbglow (not game|log|hide|fx WORDS: ignored)";
}

static void vg_end_log(void)
{
	if (!g_vg.installed)
		return;
	logf_("VBGLOW END fx asked %llu: made %llu, not made in our eyes %llu (the last '%s'), in the verbs' moment %llu; "
	      "%d names; now %s", (unsigned long long)g_vg.calls, (unsigned long long)g_vg.made,
	      (unsigned long long)g_vg.hidden, g_vg.last_hidden, (unsigned long long)g_vg.moment, g_vg.nnames,
	      g_vg_w[atomic_load(&g_vg.mode)]);
}
