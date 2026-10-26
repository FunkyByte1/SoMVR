/* somvr_minimap_game.c - SOMVR_MINIMAP=head (build 6, docs/re-xr-pipeline.md 28.2): the game's minimap turns with the
 * player's head. Included by somvr.c after the practice area's file (it uses the hooks' helpers, g_fr, g_rig).
 *
 * Why it was stuck (headset session 2): CUIHUDCombat::vf_23 (0xafdee0, the HUD's per-frame update) turns the minimap
 * clip by the gameplay camera's yaw when the game option "Rotate Minimap" (GameOptions RotateMinimapwithCamera, byte
 * +0x203 of FUN_00c818c0()'s object) is on: at 0xb0093a it calls the quaternion product FUN_00a498c0(out, cam+0x484,
 * cam+0x458) (re-firstperson.md: the effective camera rotation), flattens its +Z forward to the ground, takes the yaw
 * (FUN_023b9e00) and calls the clip's vf_0x220(-yaw in degrees). In VR the game camera never turns (the right stick
 * turns our rig, the HMD our views: the camera axes are held at 0), so the map kept the camera's first heading; with
 * the option off it is north-up and never turns at all.
 * What this does: MM_CAMQ (that call) is ours: the original product, then - in a stereo frame, the rig's head known -
 * its result replaced by the head's yaw in the game world (somvr_rig_head, yaw only: the same quaternion convention, +Z
 * forward, so the game's own code turns the map: up = where the player looks, the snap/smooth turns included). The
 * option is switched on once per session if it is off (logged; the user's choice afterwards stands). Flat frames,
 * menus and a missing head: the game's product. Fail soft: a site mismatch = nothing patched, the option untouched.
 */

/* MINIMAP_SITES_BEGIN (check_sites.py counts this block on its own. Patched with SOMVR_MINIMAP=head: MM_CAMQ = the
 * call 0xb0093a of FUN_00a498c0(out %rdi = rsp+0x190, a %rsi = cam+0x484, b %rdx = cam+0x458) in CUIHUDCombat::vf_23's
 * rotate-with-camera branch (the context from 0xb00915: P from rsp+0x80, cam = P+0x2d0, the two leas, the call); read
 * only: MM_OPT = the call 0xafec3c of FUN_00c818c0 followed by test/je and cmpb $0,0x203(%rax) (the option's byte,
 * the branch to 0xb00915); called: MM_OPTIONS = FUN_00c818c0 (the GameOptions object, or 0). Order = MMS_*) */
static const struct site_spec g_mm_sites_game[] = {
SITE("MM_CAMQ", K_CALL, 0xb0093a, 0xb00915, "48 8b 84 24 80 00 00 00 48 8d bc 24 90 01 00 00 48 8b 80 d0 02 00 00 48 8d 90 58 04 00 00 48 8d b0 84 04 00 00 e8 81 8f f4 ff", 0xa498c0, "f3 0f 10 46 0c f3 0f 10 3e 48 89 f8 f3 0f 10 22"),
SITE("MM_OPT", K_CALL, 0xafec3c, 0xafec3c, "e8 7f 2c 18 00 48 85 c0 0f 84 fa 1b 00 00 80 b8 03 02 00 00 00 0f 85 be 1c 00 00", 0xc818c0, "48 83 ec 08 48 8b 05 95 7c a2 03 48 8d 78 10 e8"),
SITE("MM_OPTIONS", K_ENTRY, 0xc818c0, 0xc818c0, "48 83 ec 08 48 8b 05 95 7c a2 03 48 8d 78 10 e8", 0xc818c0, "48 83 ec 08 48 8b 05 95 7c a2 03 48 8d 78 10"),
};
/* MINIMAP_SITES_END */
enum { MMS_CAMQ, MMS_OPT, MMS_OPTIONS, MMS_N };
_Static_assert(sizeof g_mm_sites_game / sizeof g_mm_sites_game[0] == MMS_N, "minimap sites");
static const struct site_spec *g_mm_sites = g_mm_sites_game; /* selftest: stand-ins */
#define MM_OPT_ROTATE 0x203 /* GameOptions: RotateMinimapwithCamera (FUN_02431350's save, the HUD's cmpb) */

typedef void *(*mm_qmul_fn)(float *out, const float *a, const float *b);
typedef uintptr_t (*mm_options_fn)(void);
static struct hook g_mm_h = { .name = "MM_CAMQ" };
static struct {
	int on;                 /* SOMVR_MINIMAP=head and MM_CAMQ installed */
	int forced;             /* the option was switched on (once a session) */
	int opt_seen;           /* the option's byte was read once */
	double next_opt;        /* the option checked again then (the HUD pass, game thread) */
	uint64_t calls, ours, game;
	float yaw_ours, yaw_game; /* the last head yaw / the camera's (deg, logs) */
	uint32_t lines;
	int last_ours;          /* the last call's choice (a change is logged) */
	float yaw_logged;       /* the head's yaw in the last logged line (a turn of 30 deg or more is logged) */
	uint32_t turn_lines;
	int c_ok;               /* the last eye-1 HUD pass's head yaw (mm_frame): c_yaw deg at c_t (mono) */
	float c_yaw;
	double c_t;
	uint64_t n_cached;      /* calls outside the eye passes that took it */
} g_mm;

/* the game-world yaw quaternion of the head of the stereo frame being rendered (this thread); 0 = none */
static int mm_head_quat(float q[4], float *yaw_deg)
{
	if (!g_fr.on || g_fr.frame == 0)
		return 0;
	sv_vec3 p, f;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &p, &hq);
	f = sv_qrot(hq, (sv_vec3){ 0, 0, 1 });
	float n = sqrtf(f.x * f.x + f.z * f.z);
	if (!(n > 1e-4f)) /* straight up or down (or NaN): no heading */
		return 0;
	float yaw = atan2f(f.x / n, f.z / n); /* R(Y(a)) (0,0,1) = (sin a, 0, cos a) */
	q[0] = 0, q[1] = sinf(0.5f * yaw), q[2] = 0, q[3] = cosf(0.5f * yaw);
	*yaw_deg = yaw * 57.29578f;
	return 1;
}
static float mm_yaw_of(const float *q) /* the yaw of a game quaternion's +Z (deg) */
{
	float fx = 2 * (q[0] * q[2] + q[1] * q[3]), fz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
	return atan2f(fx, fz) * 57.29578f;
}

/* MM_CAMQ (game thread, the HUD pass): the camera product, replaced by the head's yaw in a stereo frame */
__attribute__((used)) void *somvr_w_mmcamq(float *out, const float *a, const float *b)
{
	void *r = ((mm_qmul_fn)g_mm_h.orig)(out, a, b);
	uint64_t n = ++g_mm.calls;
	float q[4], yaw = 0;
	g_mm.yaw_game = mm_yaw_of(out);
	int ours = g_mm.on && mm_head_quat(q, &yaw), cached = 0;
	if (g_mm.on && !ours && g_mm.c_ok && mono() - g_mm.c_t < 0.5) { /* outside the eye passes: the last one's head */
		yaw = g_mm.c_yaw, ours = cached = 1;
		q[0] = 0, q[1] = sinf(0.5f * yaw / 57.29578f), q[2] = 0, q[3] = cosf(0.5f * yaw / 57.29578f);
	}
	g_mm.n_cached += cached;
	if (ours) {
		memcpy(out, q, sizeof q);
		g_mm.ours++, g_mm.yaw_ours = yaw;
		atomic_fetch_add_explicit(&g_mm_h.mods, 1, memory_order_relaxed);
	} else {
		g_mm.game++;
	}
	atomic_fetch_add_explicit(&g_mm_h.calls, 1, memory_order_relaxed);
	/* logged: the first 5 calls, every change between the head's and the game's, the head's yaw at each 30 deg of
	 * turn (60 lines), every 600th call (30 s headless) */
	int changed = n > 1 && ours != g_mm.last_ours;
	int turned = ours && !changed && n > 1 && g_mm.turn_lines < 60 &&
		     fabsf(remainderf(yaw - g_mm.yaw_logged, 360.0f)) >= 30.0f;
	g_mm.last_ours = ours;
	g_mm.turn_lines += turned;
	if (n <= 5 || changed || turned || n % 600 == 0) {
		if (ours)
			g_mm.yaw_logged = yaw;
		logf_("MINIMAP n=%llu frame=%llu: the camera's yaw %+.1f deg -> %s %+.1f deg (ours %llu, of them from the last eye "
		      "pass %llu, the game's %llu)%s",
		      (unsigned long long)n, (unsigned long long)g_fr.frame, g_mm.yaw_game, ours ? "the head's" : "kept",
		      ours ? yaw : g_mm.yaw_game, (unsigned long long)g_mm.ours, (unsigned long long)g_mm.n_cached,
		      (unsigned long long)g_mm.game,
		      changed ? (ours ? ": the head's yaw from here" : ": the game's from here") : turned ? ": a turn" : "");
	}
	return r;
}

/* the HUD pass (eye 1, game thread): the head's yaw kept for MM_CAMQ calls outside the eye passes (the HUD's update
 * may run between them: then g_fr.on is 0); the option, read once a second; switched on once a session if it is off */
static void mm_frame(double t)
{
	float q[4], yaw;
	if (g_mm.on && mm_head_quat(q, &yaw))
		g_mm.c_yaw = yaw, g_mm.c_t = t, g_mm.c_ok = 1;
	if (!g_mm.on || t < g_mm.next_opt)
		return;
	g_mm.next_opt = t + 1.0;
	uintptr_t o = ((mm_options_fn)g_mm_sites[MMS_OPTIONS].target)();
	unsigned char b = 0xff;
	if (!o || !peek(o + MM_OPT_ROTATE, &b, 1))
		return;
	if (!g_mm.opt_seen) {
		g_mm.opt_seen = 1;
		logf_("MINIMAP the game option Rotate Minimap is %s%s", b ? "on" : "off",
		      b ? "" : ": switched on for this session (the head turns the map; the save copy may keep it)");
	}
	if (!b && !g_mm.forced) {
		*(volatile unsigned char *)(o + MM_OPT_ROTATE) = 1;
		g_mm.forced = 1;
	} else if (!b && g_mm.lines++ < 3) {
		logf_("MINIMAP the option was switched off again (the player's choice): the map stays north-up");
	}
}

/* MM_CAMQ through a stub page within +-2 GB (as DF_IMPACT) */
static int mm_install(const struct site_spec *s)
{
	char why[512] = "", err[200] = "", nb[64];
	int ok = s->kind == K_CALL && check_site(s, why, sizeof why) == 0;
	unsigned char *pg = NULL;
	for (int i = 0; ok && i < 64 + 4 && !pg; i++) {
		static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
		uintptr_t at = i < 64 ? (uintptr_t)0x3d0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
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
		uintptr_t tgt = (uintptr_t)somvr_w_mmcamq;
		memcpy(pg, jmp, 6);
		memcpy(pg + 6, &tgt, 8);
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			ok = 0, snprintf(why, sizeof why, "stub mprotect rx: %s", strerror(errno));
	}
	int32_t rel = ok ? (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(s->addr + 5)) : 0;
	if (!ok || write_code(s->addr + 1, &rel, 4, err, sizeof err) != 0) {
		logf_("HOOK %s SKIPPED %#lx: %s%s", s->name, (unsigned long)s->addr, ok ? "write failed: " : "", ok ? err : why);
		if (pg)
			munmap(pg, 4096);
		return 0;
	}
	g_mm_h.orig = s->target;
	g_mm_h.installed = 1;
	hex_of((const unsigned char *)s->addr, 5, nb, sizeof nb);
	logf_("HOOK %s ok call %#lx target=%#lx now=[%s] via=%#lx (the minimap's camera rotation: the head's yaw)", s->name,
	      (unsigned long)s->addr, (unsigned long)s->target, nb, (unsigned long)pg);
	return 1;
}

static void mm_start(const struct site_spec *sites, int *bad)
{
	static const char *const w[] = { "0", "head" };
	(void)bad;
	memset(&g_mm, 0, sizeof g_mm);
	g_mm_sites = sites;
	if (!env_word("SOMVR_MINIMAP", w, 2, 0))
		return;
	char why[512];
	int ok = 1;
	for (int i = MMS_OPT; i < MMS_N; i++) { /* read only first: a mismatch patches nothing */
		int k = check_site(&sites[i], why, sizeof why) == 0;
		logf_("MINIMAP site %s %s %#lx -> %#lx: %s", sites[i].name, k ? "ok" : "MISMATCH", (unsigned long)sites[i].addr,
		      (unsigned long)sites[i].target, why);
		ok &= k;
	}
	if (!ok || !mm_install(&sites[MMS_CAMQ])) {
		logf_("WARN SOMVR_MINIMAP=head: %s: the minimap stays the game's", ok ? "MM_CAMQ not hooked" : "a read-only site differs");
		return;
	}
	g_mm.on = 1;
	logf_("CONFIG minimap=head (the minimap turns with the head's yaw in stereo frames; the game option Rotate Minimap on "
	      "once a session)");
}

static void mm_end_log(void)
{
	if (!g_mm_h.installed)
		return;
	logf_("MINIMAP END calls %llu: the head's yaw %llu (%llu from the last eye pass), the game's %llu; the option %s%s", (unsigned long long)g_mm.calls,
	      (unsigned long long)g_mm.ours, (unsigned long long)g_mm.n_cached, (unsigned long long)g_mm.game, g_mm.opt_seen ? "seen" : "never read",
	      g_mm.forced ? " (switched on by us)" : "");
}
