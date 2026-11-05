/* somvr_wsound_game.c - build 37 (som-b37-audio, docs/re-xr-pipeline.md 173): the game's sounds where a VR player
 * hears them. Included by somvr.c after somvr_gsound_game.c, before the quiver's file (its bs_post calls wsd_post);
 * uses peek, logf_, mono, env_word, stub_alloc, write_code, check_site, gs_name, g_log_path and the AUDIO hook's
 * listener (wsd_listener, from somvr_aud_c). The placement itself is somvr_wsound.h (pure, tests/test_wsound.c).
 *
 * SOMVR_AUDIO_NEAR=game|vr (default game = nothing patched) - with SOMVR_AUDIO=head, while the head is the listener,
 *   every game sound's position is placed for the head (the sound wrapper's SetPosition: WSD_REWORK, WSD_SETPOS;
 *   somvr_wsound.h): near the head's vertical line (within SOMVR_AUDIO_NEAR_CM, default 50 cm, or steeper than 60 deg
 *   up or down - a stereo mix follows the left-right part, which a few centimetres decide there) its left-right part
 *   is gone: centred, over, under or behind - the music's object at Talion's root, loops overhead, the wraith's fire on
 *   the sword on his back -; a nearer sound than SOMVR_AUDIO_MIN_CM is put that far; such objects are placed again as
 *   the head turns and moves (wsd_listener). Every other sound where the game puts it; the mod's own objects
 *   (0x50a7xxxx) untouched. The game's third-person rework (its 250 cm ring at ear height; off in the Windows world)
 *   keeps its ring with our direction, without its pull toward the camera. Off the head listener (menus, loads, flat
 *   frames, the AUDIO hook off) the game's own positions. Live: "sndnear game|vr|state".
 * SOMVR_AUDIO_MIN_CM=250 - the least distance a game sound is heard at (the game's own 250 cm "near"; Talion's own
 *   sounds - the wraith's fire on the sword on his back 20-40 cm from the ears - were far louder than the flat game's
 *   camera, 3-4 m off, ever heard them); 0 = their own distance.
 * SOMVR_AUDIO_LOG=0|1 (test only) - the posts by name (BS_POST_A/B, shared) with their object's position: SNDPOS lines
 *   (each name at most once a second, 6000 at most), SNDAXIS lines (an object near the head's vertical line: where
 *   the game's rework and ours put it, twice a second per object), SNDSUM every 10 s; the AK SetPosition call patched
 *   (WSD_SETPOS) to see each object's final place.
 * "sndcap start NAME|stop" (Windows, test only) - AK::SoundEngine::Start/StopOutputCapture: the final mix as a WAV
 *   next to the log (NAME.wav), the proof of left and right. */

#include "somvr_wsound.h"

/* WSOUND_SITES_BEGIN (check_sites.py counts this block on its own: the two patched calls in the sound wrapper's
 * SetPosition) */
static SITE_TABLE struct site_spec g_wsd_sites_game[] = {
SITE("WSD_REWORK", K_CALL, 0x8c9bb9, 0x8c9ba6, "42 08 f3 44 0f 10 61 08 89 44 24 0c 74 0a 48 8d 74 24 04 e8 e2 ed ff ff", 0x8c89a0, "f3 44 0f 10 47 0c f3 44 0f 10 57 08 66 0f ef"),
SITE("WSD_SETPOS", K_CALL, 0x8c9c0c, 0x8c9bf9, "f3 44 0f 11 6c 24 20 f3 44 0f 11 64 24 24 48 89 44 24 10 e8 7f 73 b9 00", 0x1460f90, "48 85 ff 74 4b 53 b8 0d 00 00 00 48 83 ec 40"),
};
/* WSOUND_SITES_END */
/* Windows only (Wwise's named exports; the Linux build's are not looked up): the output capture */
static SITE_TABLE struct site_spec g_wsd_sites_cap[] = {
SITE("WSD_CAP_START", K_ENTRY, 0, 0, "", 0, ""),
SITE("WSD_CAP_STOP", K_ENTRY, 0, 0, "", 0, ""),
};

enum { WSD_GAME, WSD_VR };
static const char *const g_wsd_mode_w[] = { "game", "vr" };
#define WSD_OBJS 512
#define WSD_LINES 6000
#define WSD_LISTENER_MS 500 /* the head listener applied this recently = VR */
#define WSD_TRACK 32        /* on-axis objects placed again at each listener update */

typedef void (*wsd_rework_fn)(void *wrap, float *pos);
#define WSD_OURS(obj) (((obj) >> 16) == 0x50a7u) /* the mod's own Wwise objects (0x50a70001..): placed as they are */
typedef uint32_t (*wsd_setpos_fn)(uint64_t obj, const float *akpos);
typedef uint32_t (*wsd_cap_start_fn)(const wchar_t *file);
typedef uint32_t (*wsd_cap_stop_fn)(void);

struct wsd_obj {
	uint64_t obj;
	float in[3], game[3], out[3];
	double t, t_axis, t_name;
	char name[48];
};

/* an object near the head's vertical line: its raw place (the game's, before any rework), its front, the game's ring
 * on for it; where and for which head it was last placed */
struct wsd_trk {
	uint64_t obj;
	float raw[3], front[3], lp[3], lf[3], out[3];
	int ring, used;
	double t_game, t_log;
};

static struct {
	int want, patched, setpos, log;
	_Atomic int mode;
	struct wsd_cfg cfg;
	uintptr_t orig_rework, orig_setpos;
	/* the listener the AUDIO hook gave Wwise (game thread) */
	_Atomic int64_t lis_ms;
	float lis_pos[3], lis_front[3], lis_top[3];
	_Atomic uint64_t calls, vr, near, axis, setposes, placed, replaced, posts, lines;
	_Atomic int64_t next_sum_ms;
	struct wsd_obj objs[WSD_OBJS];
	struct wsd_trk trk[WSD_TRACK];
	atomic_flag mu;
	char seen[96][48];
	double seen_t[96];
	int nseen;
	int capturing;
} g_wsd = { .mu = ATOMIC_FLAG_INIT };

static void wsd_sum_line(const char *head);
/* SOMVR_AUDIO_LOG: the summary every 10 s (whichever thread sets a position first) */
static void wsd_sum_due(void)
{
	int64_t now = (int64_t)(mono() * 1e3), due = atomic_load_explicit(&g_wsd.next_sum_ms, memory_order_relaxed);
	if (now >= due && atomic_compare_exchange_strong(&g_wsd.next_sum_ms, &due, now + 10000))
		wsd_sum_line("SNDSUM");
}

/* one wrapper call as its SetPosition sees it (the same thread): the rework's input, the game's ring on, our placement
 * made there; the game's own rework of it (for the log) */
static __thread struct { int seen, ring, placed; float in[3], game[3]; } t_wsd;

static int wsd_head_now(void)
{
	int64_t t = atomic_load_explicit(&g_wsd.lis_ms, memory_order_acquire);
	return t && (int64_t)(mono() * 1e3) - t < WSD_LISTENER_MS;
}

static struct wsd_obj *wsd_obj_at(uint64_t obj, int make)
{
	uint32_t h = (uint32_t)((obj * 0x9e3779b97f4a7c15ull) >> 40) % WSD_OBJS;
	for (int i = 0; i < 16; i++) {
		struct wsd_obj *o = &g_wsd.objs[(h + (uint32_t)i) % WSD_OBJS];
		if (o->obj == obj && o->t > 0)
			return o;
		if (make && (o->t == 0 || mono() - o->t > 30.0)) {
			memset(o, 0, sizeof *o);
			o->obj = obj, o->t = mono();
			return o;
		}
	}
	return NULL;
}

static void wsd_lock(void)
{
	while (atomic_flag_test_and_set_explicit(&g_wsd.mu, memory_order_acquire))
		;
}
static void wsd_unlock(void) { atomic_flag_clear_explicit(&g_wsd.mu, memory_order_release); }

/* under the lock: the object's entry in the on-axis table (make: a free one, else the one the game set longest ago) */
static struct wsd_trk *wsd_trk_at(uint64_t obj, int make)
{
	struct wsd_trk *old = &g_wsd.trk[0];
	for (int i = 0; i < WSD_TRACK; i++) {
		struct wsd_trk *k = &g_wsd.trk[i];
		if (k->used && k->obj == obj)
			return k;
		if (!k->used || k->t_game < old->t_game)
			old = !k->used || old->used ? k : old;
	}
	if (!make)
		return NULL;
	memset(old, 0, sizeof *old);
	old->obj = obj, old->used = 1;
	return old;
}

/* WSD_REWORK: the call of the game's rework in its sound wrapper's SetPosition (wrap, pos in place; obj = the object,
 * passed by the stub from the caller's rsi (Windows) / r12 (Linux)). In VR every game sound is placed here for the head
 * (somvr_wsound.h; the game's ring where it is on), so the wrapper's gp_distance is the distance it is heard at; the
 * mod's own objects and everything off the head listener get the game's own rework. Windows calls this for every
 * position, Linux only with the ring on (the rest: wsd_setpos) */
static void wsd_rework(void *wrap, float *pos, uint64_t obj)
{
	atomic_fetch_add_explicit(&g_wsd.calls, 1, memory_order_relaxed);
	int vr = atomic_load_explicit(&g_wsd.mode, memory_order_relaxed) == WSD_VR && wsd_head_now() && !WSD_OURS(obj);
	/* the wrapper is the game's global (SND_WRAP) and pos its stack array: the game reads both right here */
	const unsigned char *wp = (const unsigned char *)wrap;
	unsigned char on = wp[0x14];
	float A[3], B[3];
	memcpy(A, wp + 0x08, 12), memcpy(B, wp + 0x18, 12);
	t_wsd.seen = 1, t_wsd.ring = on != 0, t_wsd.placed = 0;
	memcpy(t_wsd.in, pos, 12), memcpy(t_wsd.game, pos, 12);
	if (on)
		wsd_game_rework(A, B, t_wsd.game);
	if (!vr) {
		((wsd_rework_fn)g_wsd.orig_rework)(wrap, pos);
		return;
	}
	atomic_fetch_add_explicit(&g_wsd.vr, 1, memory_order_relaxed);
	if (wsd_vr_place(&g_wsd.cfg, A, g_wsd.lis_front, g_wsd.lis_top, t_wsd.in, pos, on))
		atomic_fetch_add_explicit(&g_wsd.placed, 1, memory_order_relaxed);
	t_wsd.placed = 1;
}

/* WSD_SETPOS: AK::SoundEngine::SetPosition(obj, AkSoundPosition: the position, then the front; the wrapper's stack).
 * In VR: a position the rework did not place (Linux with the ring off) is placed here; every game object the
 * placement moves (near the head's vertical line, or nearer than the least distance) is kept in the table to be
 * placed again as the head turns and moves (wsd_listener); SOMVR_AUDIO_LOG: logged */
static uint32_t wsd_setpos(uint64_t obj, const float *akpos)
{
	atomic_fetch_add_explicit(&g_wsd.setposes, 1, memory_order_relaxed);
	int vr = atomic_load_explicit(&g_wsd.mode, memory_order_relaxed) == WSD_VR && wsd_head_now() && !WSD_OURS(obj);
	float P[6];
	memcpy(P, akpos, sizeof P);
	int seen = t_wsd.seen, placed = seen && t_wsd.placed, ring = seen && t_wsd.ring;
	float raw[3], game[3];
	memcpy(raw, seen ? t_wsd.in : P, 12), memcpy(game, seen ? t_wsd.game : P, 12);
	t_wsd.seen = t_wsd.placed = 0;
	if (vr && !placed && wsd_vr_place(&g_wsd.cfg, g_wsd.lis_pos, g_wsd.lis_front, g_wsd.lis_top, raw, P, 0))
		atomic_fetch_add_explicit(&g_wsd.placed, 1, memory_order_relaxed);
	uint32_t r = ((wsd_setpos_fn)g_wsd.orig_setpos)(obj, vr ? P : akpos);
	if (!vr && !g_wsd.log)
		return r;
	int need = !WSD_OURS(obj) && wsd_head_now() && wsd_needs_place(&g_wsd.cfg, g_wsd.lis_pos, g_wsd.lis_front, g_wsd.lis_top, raw, ring);
	double t = mono();
	wsd_lock();
	struct wsd_trk *k = wsd_trk_at(obj, need);
	if (k && need) {
		memcpy(k->raw, raw, 12), memcpy(k->front, akpos + 3, 12), memcpy(k->out, P, 12);
		memcpy(k->lp, g_wsd.lis_pos, 12), memcpy(k->lf, g_wsd.lis_front, 12);
		k->ring = ring, k->t_game = t;
	} else if (k)
		k->used = 0; /* the placement no longer moves it: the game's place stands */
	if (g_wsd.log) {
		struct wsd_obj *o = wsd_obj_at(obj, 1);
		if (o)
			memcpy(o->in, raw, 12), memcpy(o->game, game, 12), memcpy(o->out, P, 12), o->t = t;
	}
	wsd_unlock();
	if (g_wsd.log)
		wsd_sum_due();
	return r;
}

/* somvr_aud_c, the head given to listener 0 (the game thread, after the game's listener call): the tracked objects
 * placed again for this head when it turned 1 deg or moved 2 cm since their last placement; SOMVR_AUDIO_LOG: each
 * twice a second, where its raw place and ours are heard (the pan: sin of the bearing, -1 left .. +1 right) */
static void wsd_listener(const float pos[3], const float front[3], const float top[3])
{
	memcpy(g_wsd.lis_pos, pos, 12), memcpy(g_wsd.lis_front, front, 12), memcpy(g_wsd.lis_top, top, 12);
	atomic_store_explicit(&g_wsd.lis_ms, (int64_t)(mono() * 1e3), memory_order_release);
	int vr = atomic_load_explicit(&g_wsd.mode, memory_order_relaxed) == WSD_VR;
	if (!g_wsd.orig_setpos || (!vr && !g_wsd.log))
		return;
	struct { uint64_t obj; float p[6]; } go[WSD_TRACK];
	int ngo = 0;
	char lines[4][400];
	int nl = 0;
	double t = mono();
	wsd_lock();
	for (int i = 0; i < WSD_TRACK; i++) {
		struct wsd_trk *k = &g_wsd.trk[i];
		if (!k->used)
			continue;
		int axis = wsd_needs_place(&g_wsd.cfg, pos, front, top, k->raw, k->ring);
		float dx = pos[0] - k->lp[0], dy = pos[1] - k->lp[1], dz = pos[2] - k->lp[2];
		int moved = front[0] * k->lf[0] + front[1] * k->lf[1] + front[2] * k->lf[2] < 0.99985f || dx * dx + dy * dy + dz * dz > 4.0f;
		if (vr && (moved || !axis)) {
			if (axis)
				wsd_vr_place(&g_wsd.cfg, pos, front, top, k->raw, k->out, k->ring);
			else
				memcpy(k->out, k->raw, 12), k->used = 0; /* no longer moved: its own place again, untracked */
			memcpy(k->lp, pos, 12), memcpy(k->lf, front, 12);
			go[ngo].obj = k->obj, memcpy(go[ngo].p, k->out, 12), memcpy(go[ngo].p + 3, k->front, 12), ngo++;
		}
		if (g_wsd.log && t - k->t_log >= 0.5 && nl < 4 && atomic_load_explicit(&g_wsd.lines, memory_order_relaxed) < WSD_LINES) {
			k->t_log = t;
			float br, er, dr, bh, eh, dh;
			wsd_bearing(pos, front, top, k->raw, &br, &er, &dr);
			wsd_bearing(pos, front, top, vr ? k->out : k->raw, &bh, &eh, &dh);
			struct wsd_obj *o = wsd_obj_at(k->obj, 0);
			float hx = k->raw[0] - pos[0], hz = k->raw[2] - pos[2];
			snprintf(lines[nl++], sizeof lines[0], "SNDAXIS t=%.3f obj %llu '%s': raw %.0f cm, %.0f across, elev %+.0f -> "
				 "bearing %+.0f pan %+.2f; heard bearing %+.0f pan %+.2f elev %+.0f at %.0f cm (%s%s; the game set it "
				 "%.1f s ago)", t, (unsigned long long)k->obj, o && o->name[0] ? o->name : "?", dr,
				 sqrtf(hx * hx + hz * hz), er, br, sinf(br / 57.29578f), bh, sinf(bh / 57.29578f), eh, dh,
				 vr ? "ours" : "the game's", k->ring ? ", ring" : "", t - k->t_game);
			atomic_fetch_add_explicit(&g_wsd.lines, 1, memory_order_relaxed);
			atomic_fetch_add_explicit(&g_wsd.axis, 1, memory_order_relaxed);
		}
	}
	wsd_unlock();
	for (int i = 0; i < ngo; i++)
		((wsd_setpos_fn)g_wsd.orig_setpos)(go[i].obj, go[i].p);
	if (ngo)
		atomic_fetch_add_explicit(&g_wsd.replaced, (uint64_t)ngo, memory_order_relaxed);
	for (int i = 0; i < nl; i++)
		logf_("%s", lines[i]);
}

/* bs_post, after the game's post by name (any thread; SOMVR_AUDIO_LOG) */
static void wsd_post(const char *name, uint64_t obj, uint32_t playing)
{
	if (!g_wsd.log)
		return;
	atomic_fetch_add_explicit(&g_wsd.posts, 1, memory_order_relaxed);
	char n[GS_NAME_LEN];
	gs_name(name, n);
	double t = mono();
	char line[480] = "";
	wsd_lock();
	int i = 0, fresh = 1;
	for (; i < g_wsd.nseen; i++)
		if (!strcmp(g_wsd.seen[i], n))
			break;
	if (i < g_wsd.nseen)
		fresh = t - g_wsd.seen_t[i] >= 1.0;
	else if (g_wsd.nseen < 96)
		snprintf(g_wsd.seen[g_wsd.nseen], 48, "%s", n), g_wsd.nseen++;
	else
		i = -1; /* the name table full: logged once a second all together */
	struct wsd_obj *o = obj != (uint64_t)-1 && obj ? wsd_obj_at(obj, 1) : NULL; /* the name sticks before a position */
	int pos_seen = o && (o->in[0] != 0 || o->in[1] != 0 || o->in[2] != 0);
	if (o)
		snprintf(o->name, sizeof o->name, "%s", n);
	if (fresh && atomic_load_explicit(&g_wsd.lines, memory_order_relaxed) < WSD_LINES) {
		if (i >= 0)
			g_wsd.seen_t[i] = t;
		atomic_fetch_add_explicit(&g_wsd.lines, 1, memory_order_relaxed);
		if (obj == (uint64_t)-1 || obj == 0)
			snprintf(line, sizeof line, "SNDPOS t=%.3f '%s' obj %lld (global: no position) -> playing %u", t, n,
				 (long long)obj, playing);
		else if (!o || !pos_seen)
			snprintf(line, sizeof line, "SNDPOS t=%.3f '%s' obj %llu (no position seen yet) -> playing %u", t, n,
				 (unsigned long long)obj, playing);
		else {
			float bi, ei, di, bg, eg, dg, bo, eo, dO;
			wsd_bearing(g_wsd.lis_pos, g_wsd.lis_front, g_wsd.lis_top, o->in, &bi, &ei, &di);
			wsd_bearing(g_wsd.lis_pos, g_wsd.lis_front, g_wsd.lis_top, o->game, &bg, &eg, &dg);
			wsd_bearing(g_wsd.lis_pos, g_wsd.lis_front, g_wsd.lis_top, o->out, &bo, &eo, &dO);
			snprintf(line, sizeof line, "SNDPOS t=%.3f '%s' obj %llu: the source bearing %+.0f elev %+.0f at %.0f cm "
				 "(%.2f s ago); the game's rework bearing %+.0f at %.0f cm; heard bearing %+.0f elev %+.0f at %.0f cm "
				 "-> playing %u%s", t, n, (unsigned long long)obj, bi, ei, di, t - o->t, bg, dg, bo, eo, dO, playing,
				 wsd_head_now() ? "" : " (not the head listener)");
		}
	}
	wsd_unlock();
	if (line[0])
		logf_("%s", line);
}

static void wsd_sum_line(const char *head)
{
	int ntrk = 0;
	for (int i = 0; i < WSD_TRACK; i++)
		ntrk += g_wsd.trk[i].used;
	logf_("%s t=%.3f: SOMVR_AUDIO_NEAR %s (the rework %s, SetPosition %s; ring %.0f cm, axis %.0f cm); reworks %llu, "
	      "ours %llu (on the ring %llu); SetPosition %llu, placed for the head %llu, placed again as it turned %llu; on "
	      "the axis now %d; posts by name %llu; axis lines %llu; the head listener %s", head, mono(),
	      g_wsd_mode_w[atomic_load(&g_wsd.mode)], g_wsd.patched ? "patched" : "NOT patched",
	      g_wsd.setpos ? "patched" : "NOT patched", g_wsd.cfg.ring_cm, g_wsd.cfg.axis_cm,
	      (unsigned long long)atomic_load(&g_wsd.calls), (unsigned long long)atomic_load(&g_wsd.vr),
	      (unsigned long long)atomic_load(&g_wsd.near), (unsigned long long)atomic_load(&g_wsd.setposes),
	      (unsigned long long)atomic_load(&g_wsd.placed), (unsigned long long)atomic_load(&g_wsd.replaced), ntrk,
	      (unsigned long long)atomic_load(&g_wsd.posts), (unsigned long long)atomic_load(&g_wsd.axis),
	      wsd_head_now() ? "on" : "off");
}


/* the rework's call site through a stub of our own that passes the object as wsd_rework's third argument:
 * Windows (the wrapper's rsi = obj; it reads rcx after the call - MSVC knew the callee keeps it):
 *   push rcx; sub rsp,0x20; mov r8,rsi; call [rip+6]; add rsp,0x20; pop rcx; ret; dq fn
 * Linux (the wrapper's r12 = obj; rbx/r12 callee-saved): mov rdx,r12; jmp [rip+0]; dq fn */
static int wsd_patch_rework(const struct site_spec *s, void *fn)
{
	char why[512] = "", err[200] = "";
	int ok = s->kind == K_CALL && check_site(s, why, sizeof why) == 0;
	unsigned char *pg = ok ? stub_alloc(s->addr, s->addr, 0x3e0000) : NULL;
	if (ok && !pg)
		ok = 0, snprintf(why, sizeof why, "no stub page within +-2 GB");
	if (ok) {
#ifdef _WIN32
		static const unsigned char th[] = { 0x51, 0x48, 0x83, 0xec, 0x20, 0x49, 0x89, 0xf0, 0xff, 0x15, 0x06, 0, 0, 0,
						    0x48, 0x83, 0xc4, 0x20, 0x59, 0xc3 };
#else
		static const unsigned char th[] = { 0x4c, 0x89, 0xe2, 0xff, 0x25, 0, 0, 0, 0 };
#endif
		uintptr_t tgt = (uintptr_t)fn;
		memcpy(pg, th, sizeof th), memcpy(pg + sizeof th, &tgt, 8);
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			ok = 0, snprintf(why, sizeof why, "stub mprotect rx: %s", strerror(errno));
	}
	int32_t rel = ok ? (int32_t)((int64_t)(uintptr_t)pg - (int64_t)(s->addr + 5)) : 0;
	if (!ok || write_code(s->addr + 1, &rel, 4, err, sizeof err) != 0) {
		logf_("HOOK %s SKIPPED %#llx: %s%s", s->name, (unsigned long long)s->addr, ok ? "write failed: " : "", ok ? err : why);
		return 0;
	}
	logf_("HOOK %s ok call %#llx -> stub %#llx (the object passed%s) -> %p (the original %#llx)", s->name,
	      (unsigned long long)(uintptr_t)s->addr, (unsigned long long)(uintptr_t)pg,
#ifdef _WIN32
	      ", rcx kept",
#else
	      "",
#endif
	      fn, (unsigned long long)(uintptr_t)s->target);
	return 1;
}

static const char *wsd_cap_word(const char *w)
{
#ifdef _WIN32
	char why[256];
	const struct site_spec *a = &g_wsd_sites_cap[0], *b = &g_wsd_sites_cap[1];
	if (!strncmp(w, "start", 5)) {
		if (check_site(a, why, sizeof why) != 0)
			return logf_("SNDCAP no capture: %s", why), "sndcap (no WSD_CAP_START)";
		char nm[64] = "sndcap";
		sscanf(w + 5, " %63[A-Za-z0-9_.-]", nm);
		char p[1200];
		const char *sl = strrchr(g_log_path, '/');
		snprintf(p, sizeof p, "%s%.*s/%s.wav", g_log_path[0] == '/' ? "Z:" : "", sl ? (int)(sl - g_log_path) : 0,
			 g_log_path, nm); /* a Linux path (Wine's Z: = /) or already a Windows one */
		wchar_t wp[1200];
		size_t k = 0;
		for (; p[k] && k + 1 < sizeof wp / sizeof wp[0]; k++)
			wp[k] = p[k] == '/' ? L'\\' : (wchar_t)(unsigned char)p[k];
		wp[k] = 0;
		uint32_t r = ((wsd_cap_start_fn)a->addr)(wp);
		g_wsd.capturing = r == 1;
		logf_("SNDCAP t=%.3f start '%s' -> AK %u (1 = success); SOMVR_AUDIO_NEAR %s", mono(), p, r,
		      g_wsd_mode_w[atomic_load(&g_wsd.mode)]);
		return "sndcap start";
	}
	if (!strncmp(w, "stop", 4)) {
		if (check_site(b, why, sizeof why) != 0)
			return "sndcap (no WSD_CAP_STOP)";
		uint32_t r = ((wsd_cap_stop_fn)b->addr)();
		g_wsd.capturing = 0;
		logf_("SNDCAP t=%.3f stop -> AK %u", mono(), r);
		return "sndcap stop";
	}
	return "sndcap (start NAME|stop)";
#else
	(void)w;
	return "sndcap (Windows only)";
#endif
}

/* "sndnear game|vr|state", "sndcap start NAME|stop" (the writer thread) */
static const char *wsd_word(const char *w0)
{
	char w[96];
	snprintf(w, sizeof w, "%s", w0);
	for (size_t n = strlen(w); n && (w[n - 1] == '\n' || w[n - 1] == ' ' || w[n - 1] == '\r');)
		w[--n] = 0;
	if (!strncmp(w, "sndcap", 6))
		return wsd_cap_word(w[6] == ' ' ? w + 7 : w + 6);
	const char *a = w[7] == ' ' ? w + 8 : w + 7;
	if (!*a || !strcmp(a, "state")) {
		wsd_sum_line("SNDNEAR state");
		return "sndnear (a state line)";
	}
	int m = !strcmp(a, "game") ? WSD_GAME : !strcmp(a, "vr") ? WSD_VR : -1;
	if (m < 0)
		return "sndnear (not game|vr|state: ignored)";
	if (!g_wsd.patched || !g_wsd.setpos)
		return "sndnear (the rework or SetPosition is not patched: boot with SOMVR_AUDIO_NEAR=vr, or SOMVR_AUDIO_LOG=1)";
	atomic_store(&g_wsd.mode, m);
	logf_("SNDNEAR t=%.3f the word: %s from the next sound", mono(), g_wsd_mode_w[m]);
	return "sndnear";
}

static int bs_install_log(void); /* somvr_quiver_game.c (included after this file) */

/* after the AUDIO hook's config (needs SOMVR_AUDIO=head for vr) */
static void wsd_start(int *bad)
{
	g_wsd.want = env_word("SOMVR_AUDIO_NEAR", g_wsd_mode_w, 2, WSD_GAME);
	g_wsd.log = env_word("SOMVR_AUDIO_LOG", (const char *const[]){ "0", "1" }, 2, 0);
	g_wsd.cfg = (struct wsd_cfg){ WSD_GAME_RING_CM, 50.0f, 0.5f, WSD_GAME_RING_CM };
	const char *v = getenv("SOMVR_AUDIO_NEAR_CM");
	if (v && v[0]) {
		char *e;
		float f = strtof(v, &e);
		if (*e || !(f >= 0 && f <= 200))
			logf_("WARN SOMVR_AUDIO_NEAR_CM='%s' (0..200 cm): 50", v), (*bad)++;
		else
			g_wsd.cfg.axis_cm = f;
	}
	v = getenv("SOMVR_AUDIO_MIN_CM");
	if (v && v[0]) {
		char *e;
		float f = strtof(v, &e);
		if (*e || !(f >= 0 && f <= 1000))
			logf_("WARN SOMVR_AUDIO_MIN_CM='%s' (0..1000 cm): 250", v), (*bad)++;
		else
			g_wsd.cfg.min_cm = f;
	}
	if (g_wsd.want == WSD_GAME && !g_wsd.log)
		return;
	if (g_wsd.want == WSD_VR && !g_audio)
		logf_("CONFIG audio near=vr (SOMVR_AUDIO_NEAR): SOMVR_AUDIO is not head - the listener is the game's, so the "
		      "game's rework stays");
	g_wsd.orig_rework = g_wsd_sites_game[0].target, g_wsd.orig_setpos = g_wsd_sites_game[1].target;
	g_wsd.patched = wsd_patch_rework(&g_wsd_sites_game[0], (void *)wsd_rework);
	g_wsd.setpos = bo_patch_call(&g_wsd_sites_game[1], (void *)wsd_setpos);
	if (g_wsd.log) {
		bs_install_log(); /* the posts by name (SOMVR_BOW_SNDLOG's patch, shared) */
		atomic_store(&g_wsd.next_sum_ms, (int64_t)(mono() * 1e3) + 10000);
	}
	atomic_store(&g_wsd.mode, g_wsd.patched && g_wsd.setpos && g_audio ? g_wsd.want : WSD_GAME);
	logf_("CONFIG audio near=%s (SOMVR_AUDIO_NEAR: while the head is the listener a game sound near the head's vertical "
	      "line (%.0f cm, or steeper than 60 deg) loses its left-right part - centred, over, under or behind -, one nearer "
	      "than %.0f cm (SOMVR_AUDIO_MIN_CM) is put that far, both placed again as the head turns; the mod's own sounds and "
	      "everything else where the game puts them): the rework %s, SetPosition %s; log=%d (SOMVR_AUDIO_LOG, test only)",
	      g_wsd_mode_w[atomic_load(&g_wsd.mode)], g_wsd.cfg.axis_cm, g_wsd.cfg.min_cm,
	      g_wsd.patched ? "patched (WSD_REWORK)" : "NOT patched", g_wsd.setpos ? "patched (WSD_SETPOS)" : "NOT patched",
	      g_wsd.log);
}

static void wsd_end_log(void)
{
#ifdef _WIN32
	if (g_wsd.capturing) {
		char why[64];
		if (check_site(&g_wsd_sites_cap[1], why, sizeof why) == 0)
			((wsd_cap_stop_fn)g_wsd_sites_cap[1].addr)(), g_wsd.capturing = 0;
	}
#endif
	if (g_wsd.patched || g_wsd.log)
		wsd_sum_line("SNDNEAR END");
}
