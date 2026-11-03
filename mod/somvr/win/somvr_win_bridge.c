/* somvr_win_bridge.c - the DLL's side of the bridge to the Linux Vulkan layer (somvr_xrloc.h, docs/re-winport.md 3.1):
 * the published-locate object, the xrLocateViews / xrLocateSpace stand-ins the game-side code calls through the
 * pointers the layer stores, and the bridge file (SOMVR_BRIDGE) that replaces the layer's dlsym(RTLD_DEFAULT). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <openxr/openxr.h>
#include "../somvr_xrloc.h"
#include "../somvr_input.h"
#include "../somvr_hud.h"
#include "../somvr_comfort.h"
#include "../somvr_vrmenu.h"
#include "../somvr_threat.h"
#include "../somvr_objective.h"
#include "../somvr_session.h"
#include "../somvr_vrhands.h"
#include "somvr_win.h"

/* the shared objects somvr.c defines under their dlsym names (the Linux preload's exports) */
extern char sym_shared __asm__(SOMVR_SHARED_SYMBOL);
extern char sym_hud __asm__(SOMVR_HUD_SYMBOL);
extern char sym_input __asm__(SOMVR_INPUT_SYMBOL);
extern char sym_comfort __asm__(SOMVR_COMFORT_SYMBOL);
extern char sym_vrmenu __asm__(SOMVR_VRMENU_SYMBOL);
extern char sym_threat __asm__(SOMVR_THREAT_SYMBOL);
extern char sym_objective __asm__(SOMVR_OBJECTIVE_SYMBOL);
extern char sym_session __asm__(SOMVR_SESSION_SYMBOL);
extern char sym_vrhands __asm__(SOMVR_VRHANDS_SYMBOL);

somvr_xrloc g_xrloc __asm__(SOMVR_XRLOC_SYMBOL) = { .magic = SOMVR_XRLOC_MAGIC, .version = SOMVR_XRLOC_VERSION,
						      .size = sizeof(somvr_xrloc) };

#define STALE_NS 5000000 /* an answer whose sample time is more than 5 ms from the time asked */

static void count_stale(int64_t asked, int64_t got)
{
	int64_t d = asked > got ? asked - got : got - asked;
	if (d > STALE_NS)
		atomic_fetch_add_explicit(&g_xrloc.n_stale, 1, memory_order_relaxed);
}

/* xrLocateViews from the newest published sample (the layer stores this in somvr_shared.xr.locate_views) */
static XrResult XRAPI_CALL pe_locate_views(XrSession s, const XrViewLocateInfo *li, XrViewState *vs, uint32_t cap,
					   uint32_t *n, XrView *v)
{
	(void)s;
	atomic_fetch_add_explicit(&g_xrloc.n_views, 1, memory_order_relaxed);
	somvr_xrloc_sample x;
	if (!atomic_load_explicit(&g_xrloc.running, memory_order_acquire) || !li ||
	    !somvr_xrloc_pick(&g_xrloc, li->displayTime, &x)) {
		atomic_fetch_add_explicit(&g_xrloc.n_stale, 1, memory_order_relaxed);
		if (n)
			*n = 0;
		return XR_ERROR_TIME_INVALID;
	}
	count_stale(li->displayTime, x.time);
	if (n)
		*n = x.n;
	if (vs)
		vs->viewStateFlags = x.view_flags;
	if (XR_FAILED(x.result))
		return (XrResult)x.result;
	if (cap < x.n)
		return cap ? XR_ERROR_SIZE_INSUFFICIENT : XR_SUCCESS;
	for (uint32_t e = 0; e < x.n && e < 2; e++) {
		memcpy(&v[e].pose, &x.pose[e], sizeof v[e].pose);
		memcpy(&v[e].fov, &x.fov[e], sizeof v[e].fov);
	}
	return XR_SUCCESS;
}

/* xrLocateSpace for the four published action spaces in the published base space */
static XrResult XRAPI_CALL pe_locate_space(XrSpace sp, XrSpace base, XrTime t, XrSpaceLocation *l)
{
	atomic_fetch_add_explicit(&g_xrloc.n_spaces, 1, memory_order_relaxed);
	int k = -1;
	for (int i = 0; i < SOMVR_XRLOC_NSPACE; i++)
		if (g_xrloc.space[i] && g_xrloc.space[i] == (uint64_t)(uintptr_t)sp)
			k = i;
	somvr_xrloc_sample x;
	if (k < 0 || (uint64_t)(uintptr_t)base != g_xrloc.base_space) {
		atomic_fetch_add_explicit(&g_xrloc.n_nospace, 1, memory_order_relaxed);
		return XR_ERROR_HANDLE_INVALID;
	}
	if (!l || !atomic_load_explicit(&g_xrloc.running, memory_order_acquire) || !somvr_xrloc_pick(&g_xrloc, t, &x)) {
		atomic_fetch_add_explicit(&g_xrloc.n_stale, 1, memory_order_relaxed);
		return XR_ERROR_TIME_INVALID;
	}
	count_stale(t, x.time);
	const somvr_xrloc_space *o = &x.space[k];
	l->locationFlags = o->flags;
	memcpy(&l->pose, &o->pose, sizeof l->pose);
	for (XrBaseOutStructure *nx = (XrBaseOutStructure *)l->next; nx; nx = nx->next)
		if (nx->type == XR_TYPE_SPACE_VELOCITY) {
			XrSpaceVelocity *vel = (XrSpaceVelocity *)nx;
			vel->velocityFlags = o->vflags;
			memcpy(&vel->linearVelocity, o->lin, 12);
			memcpy(&vel->angularVelocity, o->ang, 12);
		}
	return (XrResult)o->result;
}

/* informational only: Wine opens files in the wineserver, so this reads the server's /proc/self (the bridge file is
 * matched by its nonce instead) */
static long unix_pid(void)
{
	if (!somvr_under_wine())
		return 0;
	FILE *f = fopen("Z:\\proc\\self\\stat", "rb");
	long pid = 0;
	if (f) {
		if (fscanf(f, "%ld", &pid) != 1)
			pid = 0;
		fclose(f);
	}
	return pid;
}

int somvr_win_bridge_write(const char *exe_sha, char *why, size_t cap)
{
	const char *p = getenv(SOMVR_BRIDGE_ENV);
	if (!p || !p[0])
		return snprintf(why, cap, "%s unset: the layer finds no objects (mono)", SOMVR_BRIDGE_ENV), -1;
	char pb[1024], tb[1100];
	const char *path = somvr_path(p, pb, sizeof pb);
	snprintf(tb, sizeof tb, "%s.tmp", path);
	FILE *f = fopen(tb, "wb");
	if (!f)
		return snprintf(why, cap, "cannot write %s (%d)", tb, errno), -1;
	long pid = unix_pid();
	LARGE_INTEGER q;
	QueryPerformanceCounter(&q);
	uint64_t nonce = (uint64_t)q.QuadPart * 0x9e3779b97f4a7c15ull ^ (uint64_t)(uintptr_t)&q ^
			 (uint64_t)GetCurrentProcessId() << 40;
	g_xrloc.nonce = nonce ? nonce : 1;
	fprintf(f, "%s\nnonce %#llx\npid %ld\nexe %s\n", SOMVR_BRIDGE_HEAD, (unsigned long long)g_xrloc.nonce, pid,
		exe_sha);
	const struct { const char *n; const void *a; } o[] = {
		{ SOMVR_SHARED_SYMBOL, &sym_shared },	{ SOMVR_HUD_SYMBOL, &sym_hud },
		{ SOMVR_INPUT_SYMBOL, &sym_input },	{ SOMVR_COMFORT_SYMBOL, &sym_comfort },
		{ SOMVR_VRMENU_SYMBOL, &sym_vrmenu },	{ SOMVR_THREAT_SYMBOL, &sym_threat },
		{ SOMVR_OBJECTIVE_SYMBOL, &sym_objective }, { SOMVR_SESSION_SYMBOL, &sym_session },
		{ SOMVR_VRHANDS_SYMBOL, &sym_vrhands }, { SOMVR_XRLOC_SYMBOL, &g_xrloc },
		{ "locate_views", (const void *)pe_locate_views }, { "locate_space", (const void *)pe_locate_space },
	};
	for (size_t i = 0; i < sizeof o / sizeof o[0]; i++)
		fprintf(f, "%s %#llx\n", o[i].n, (unsigned long long)(uintptr_t)o[i].a);
	fprintf(f, "end\n");
	int bad = ferror(f);
	bad |= fclose(f);
	if (bad || !MoveFileExA(tb, path, MOVEFILE_REPLACE_EXISTING))
		return snprintf(why, cap, "cannot write %s", path), -1;
	snprintf(why, cap, "%s (nonce %#llx, %zu objects)", p, (unsigned long long)g_xrloc.nonce, sizeof o / sizeof o[0]);
	return 0;
}

/* native Windows (W1): the layer is a Windows DLL in this process and asks this export instead of reading a file */
__declspec(dllexport) const void *somvr_win_bridge(const char *name)
{
	if (!name)
		return NULL;
	if (!strcmp(name, SOMVR_XRLOC_SYMBOL))
		return &g_xrloc;
	if (!strcmp(name, "locate_views"))
		return (const void *)pe_locate_views;
	if (!strcmp(name, "locate_space"))
		return (const void *)pe_locate_space;
	const struct { const char *n; const void *a; } o[] = {
		{ SOMVR_SHARED_SYMBOL, &sym_shared },	{ SOMVR_HUD_SYMBOL, &sym_hud },
		{ SOMVR_INPUT_SYMBOL, &sym_input },	{ SOMVR_COMFORT_SYMBOL, &sym_comfort },
		{ SOMVR_VRMENU_SYMBOL, &sym_vrmenu },	{ SOMVR_THREAT_SYMBOL, &sym_threat },
		{ SOMVR_OBJECTIVE_SYMBOL, &sym_objective }, { SOMVR_SESSION_SYMBOL, &sym_session },
		{ SOMVR_VRHANDS_SYMBOL, &sym_vrhands },
	};
	for (size_t i = 0; i < sizeof o / sizeof o[0]; i++)
		if (!strcmp(name, o[i].n))
			return o[i].a;
	return NULL;
}
