/* somvr_win_host.c - the host's logic module (somvr_win_host.h; docs/re-xr-pipeline.md 156): somvr_logic.dll loaded
 * at the boot from the host's own folder (one code path in every build), the persistent area, and with
 * SOMVR_DEV_RELOAD=1 the swap: a `reload` word or a new file stamp of the watched DLL (SOMVR_DEV_RELOAD_DLL, default
 * the host's folder's somvr_logic.dll) -> a copy under a unique name (Windows keeps a loaded DLL's file locked), loaded;
 * the old module asked whether it is quiet (SOMVR_DEV_RELOAD_WAIT_S, default 10 s; `reload force` skips it), stopped
 * (its writer thread joined), the new one's init staged (somvr_win_hook.c keeps its writes), D3D's objects replayed to
 * it, every slot rewritten at once with the game's threads held, the old one retired (never unloaded). */
#define SOMVR_HOST 1
#include <windows.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "somvr_win.h"
#include "somvr_win_host.h"
#include "somvr_win_hostint.h"
#include "../somvr_shared.h"
#include "../somvr_pose.h"

extern somvr_shared g_sh; /* somvr_win_bridge.c */

struct host_logic *host_live;
static struct host_logic g_mods[256];
static int g_nmods;
static char g_dir[MAX_PATH];   /* the host's folder */
static int g_dev;              /* SOMVR_DEV_RELOAD=1 */
static int g_user;             /* SOMVR_USER_SESSION=1: a session the launcher started for the user (b37) */
static int g_live;             /* SOMVR_LIVE=on: the player's patches (somvr_host_player_swap) */
static struct somvr_logic_boot g_boot; /* the boot's (sha, tag, who kept for the reloads) */
static char g_sha[65], g_tag[32], g_who[200];

static uintptr_t image_size(HMODULE m)
{
	const IMAGE_DOS_HEADER *d = (const IMAGE_DOS_HEADER *)m;
	return ((const IMAGE_NT_HEADERS64 *)((uintptr_t)m + (uintptr_t)d->e_lfanew))->OptionalHeader.SizeOfImage;
}

static int load_logic(const char *path, struct host_logic *L, char *why, size_t cap)
{
	memset(L, 0, sizeof *L);
	HMODULE m = LoadLibraryA(path);
	if (!m)
		return snprintf(why, cap, "LoadLibrary %s: error %lu", path, GetLastError()), -1;
	somvr_logic_abi_fn abi = (somvr_logic_abi_fn)(void *)GetProcAddress(m, "somvr_logic_abi");
	L->init = (somvr_logic_init_fn)(void *)GetProcAddress(m, "somvr_logic_init");
	L->com = (somvr_logic_com_fn)(void *)GetProcAddress(m, "somvr_logic_com");
	L->present = (somvr_logic_present_fn)(void *)GetProcAddress(m, "somvr_logic_present");
	L->idle = (somvr_logic_idle_fn)(void *)GetProcAddress(m, "somvr_logic_idle");
	L->stop = (somvr_logic_stop_fn)(void *)GetProcAddress(m, "somvr_logic_stop");
	L->abitest = (somvr_logic_abitest_fn)(void *)GetProcAddress(m, "somvr_logic_abitest");
	int a = abi ? abi() : -1;
	if (a != SOMVR_LOGIC_ABI || !L->init || !L->com || !L->present || !L->idle || !L->stop) {
		FreeLibrary(m); /* its init never ran: nothing of it is reachable */
		return snprintf(why, cap, "%s: logic ABI %d (want %d) or an entry point missing", path, a, SOMVR_LOGIC_ABI), -1;
	}
	L->mod = m;
	L->lo = (uintptr_t)m, L->hi = (uintptr_t)m + image_size(m);
	return 0;
}

static double now_s(void)
{
	LARGE_INTEGER n, f;
	QueryPerformanceCounter(&n), QueryPerformanceFrequency(&f);
	return (double)n.QuadPart / (double)f.QuadPart;
}

/* the process's committed memory by a walk of its address space (Wine's PrivateUsage is 0): private (heaps, our pages)
 * and image (mapped DLLs - a module's copy-on-write pages count here) */
static void mem_mb(double *priv, double *img)
{
	uint64_t p = 0, im = 0;
	MEMORY_BASIC_INFORMATION m;
	for (uintptr_t a = 0x10000; a < (uintptr_t)0x7fffffff0000ull && VirtualQuery((const void *)a, &m, sizeof m); ) {
		if (m.State == MEM_COMMIT && m.Type == MEM_PRIVATE)
			p += m.RegionSize;
		else if (m.State == MEM_COMMIT)
			im += m.RegionSize;
		uintptr_t n = (uintptr_t)m.BaseAddress + m.RegionSize;
		if (n <= a)
			break;
		a = n;
	}
	*priv = (double)p / 1048576.0, *img = (double)im / 1048576.0;
}

/* the boot (win_start, the main thread before the game's entry): the folder's somvr_logic.dll, its init */
int host_boot(const struct somvr_logic_boot *b)
{
	char me[MAX_PATH] = "", path[MAX_PATH + 32], why[400];
	GetModuleFileNameA((HMODULE)somvr_win_module, me, sizeof me);
	char *sl = strrchr(me, '\\');
	if (sl)
		*sl = 0;
	snprintf(g_dir, sizeof g_dir, "%s", me);
	snprintf(path, sizeof path, "%s\\somvr_logic.dll", g_dir);
	g_boot = *b;
	snprintf(g_sha, sizeof g_sha, "%s", b->sha ? b->sha : ""), snprintf(g_tag, sizeof g_tag, "%s", b->tag ? b->tag : "");
	snprintf(g_who, sizeof g_who, "%s", b->who ? b->who : "");
	g_boot.sha = g_sha, g_boot.tag = g_tag, g_boot.who = g_who;
	const char *dv = getenv("SOMVR_DEV_RELOAD"), *us = getenv("SOMVR_USER_SESSION"), *lv = getenv("SOMVR_LIVE");
	g_dev = dv && !strcmp(dv, "1");
	g_user = us && !strcmp(us, "1");
	g_live = lv && (!strcmp(lv, "on") || !strcmp(lv, "1"));
	double t0 = now_s();
	struct host_logic *L = &g_mods[0];
	if (load_logic(path, L, why, sizeof why) != 0) {
		somvr_win_log("LOGIC not loaded: %s - the mod stays off", why);
		return -1;
	}
	g_nmods = 1;
	host_live = L;
	somvr_win_log("LOGIC %s at %#llx (%llu KB) loaded in %.1f ms; reload: %s; the player's swap (Apply / Undo) %s",
		      path, (unsigned long long)L->lo, (unsigned long long)((L->hi - L->lo) >> 10), 1e3 * (now_s() - t0),
		      !g_dev ? "the word and the file watch off (SOMVR_DEV_RELOAD unset)"
		      : g_user ? "the word and the file watch REFUSED - a user's session (SOMVR_USER_SESSION=1), whatever SOMVR_DEV_RELOAD says"
			       : "the word and the file watch on (SOMVR_DEV_RELOAD=1, a developer's game)",
		      g_live || g_dev ? (g_live ? "on (SOMVR_LIVE=on)" : "on (SOMVR_DEV_RELOAD=1)") : "off");
	int rc = L->init(&g_boot);
	char st[300];
	hk_stats(st, sizeof st);
	somvr_win_log("LOGIC init rc=%d: %s", rc, st);
	return rc;
}

/* ---- the persistent area ------------------------------------------------------------------------------------- */
static struct { char name[48]; size_t size; void *p; } g_pb[128];
static int g_npb;
static SRWLOCK g_pmx = SRWLOCK_INIT;

void *somvr_host_persist(const char *name, size_t size, int *fresh)
{
	AcquireSRWLockExclusive(&g_pmx);
	int k = -1;
	for (int i = 0; i < g_npb; i++)
		if (!strncmp(g_pb[i].name, name, sizeof g_pb[i].name - 1))
			k = i;
	void *p = NULL;
	if (k >= 0 && g_pb[k].size == size) {
		p = g_pb[k].p, *fresh = 0;
	} else if (k >= 0 || g_npb < (int)(sizeof g_pb / sizeof g_pb[0])) {
		p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
		if (p) {
			if (k >= 0)
				somvr_win_log("PERSIST %s: %zu bytes now (was %zu: the layout changed) - a new block, the old one left",
					      name, size, g_pb[k].size);
			else
				k = g_npb++;
			snprintf(g_pb[k].name, sizeof g_pb[k].name, "%s", name);
			g_pb[k].size = size, g_pb[k].p = p;
		}
		*fresh = 1;
	}
	ReleaseSRWLockExclusive(&g_pmx);
	return p;
}

int somvr_host_gen(void) { return hk_gen(); }

/* the logic module (any generation, the live one or a retired one) an address lies in: its generation, else -1 */
int host_module_of(uintptr_t a, uintptr_t *base)
{
	for (int i = 0; i < g_nmods; i++)
		if (a >= g_mods[i].lo && a < g_mods[i].hi)
			return *base = g_mods[i].lo, g_mods[i].gen;
	return -1;
}

/* ---- the swap ------------------------------------------------------------------------------------------------ */
static HANDLE g_ev;
static SRWLOCK g_rmx = SRWLOCK_INIT;
static char g_req[256];                       /* the word's arguments */
static char g_req_dll[MAX_PATH + 64], g_req_why[96]; /* the player's swap */
static int g_req_kind;                        /* RQ_*: what is queued */
static int g_running;                         /* a swap under way (the reload thread) */
enum { RQ_NONE, RQ_WORD, RQ_PLAYER };

void somvr_host_reload_request(const char *word)
{
	if (!g_dev) {
		somvr_win_log("RELOAD refused: the word '%.80s' - SOMVR_DEV_RELOAD is not 1 (one module for the run)",
			      word ? word : "");
		return;
	}
	if (g_user) {
		somvr_win_log("RELOAD refused: the word '%.80s' - a session the launcher started for the user (SOMVR_USER_SESSION=1): "
			      "only the player's own Apply / Undo swaps there", word ? word : "");
		return;
	}
	AcquireSRWLockExclusive(&g_rmx);
	snprintf(g_req, sizeof g_req, "%s", word && *word ? word : "reload");
	g_req_kind = RQ_WORD;
	ReleaseSRWLockExclusive(&g_rmx);
	SetEvent(g_ev);
}

/* a module's path as the host's file calls need it (b37; somvr_path converts a Linux absolute path only and hands any
 * other back unchanged - its result was ignored, so a Windows path reached CopyFile as an uninitialised buffer): a
 * Linux absolute path (the harness's, the live loop's) made the Windows one under Wine (Z:\...), a Windows absolute path
 * (X:\... or \\server\...) taken as it is ('/' as '\'); refused, with why, when it is neither, too long, not converted
 * (not under Wine), or no file is there. 0 = out holds the path */
static int host_path(const char *in, char *out, size_t cap, char *why, size_t wcap)
{
	if (!in || !*in)
		return snprintf(why, wcap, "no path given"), -1;
	if (in[0] == '/' && in[1] != '/') {
		if (somvr_path(in, out, cap) != out)
			return snprintf(why, wcap, "the Linux path %.200s could not be made a Windows one (%s)", in,
					somvr_under_wine() ? "too long" : "not under Wine"), -1;
	} else {
		int drive = ((in[0] | 0x20) >= 'a' && (in[0] | 0x20) <= 'z') && in[1] == ':' && (in[2] == '\\' || in[2] == '/');
		if (!drive && !(in[0] == '\\' && in[1] == '\\'))
			return snprintf(why, wcap, "'%.200s' is not an absolute path (a Linux /... or a Windows X:\\...)", in), -1;
		if (strlen(in) >= cap)
			return snprintf(why, wcap, "the path is too long (%zu characters, at most %zu)", strlen(in), cap - 1), -1;
		snprintf(out, cap, "%s", in);
		for (char *q = out; *q; q++)
			if (*q == '/')
				*q = '\\';
	}
	DWORD a = GetFileAttributesA(out);
	if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY))
		return snprintf(why, wcap, "%.250s: no such file (given as %.200s)", out, in), -1;
	return 0;
}

int somvr_host_player_swap(const char *dll, const char *why)
{
	char path[MAX_PATH + 64], pwhy[600] = "";
	const char *no = !(g_live || g_dev) ? "neither SOMVR_LIVE=on nor SOMVR_DEV_RELOAD=1"
			 : !g_ev		    ? "no reload thread"
			 : host_path(dll, path, sizeof path, pwhy, sizeof pwhy) ? pwhy
						    : NULL;
	AcquireSRWLockExclusive(&g_rmx);
	if (!no && (g_req_kind || g_running))
		no = "a swap is already queued or running";
	if (!no) {
		snprintf(g_req_dll, sizeof g_req_dll, "%s", path);
		snprintf(g_req_why, sizeof g_req_why, "%s", why && *why ? why : "the player's press");
		g_req_kind = RQ_PLAYER;
	}
	ReleaseSRWLockExclusive(&g_rmx);
	if (no) {
		somvr_win_log("RELOAD refused: the player's swap (%.80s) to %.200s - %s", why ? why : "", dll ? dll : "", no);
		return -1;
	}
	somvr_win_log("RELOAD the player's swap queued (%s): %s", g_req_why, g_req_dll);
	SetEvent(g_ev);
	return 0;
}

/* the watched file: SOMVR_DEV_RELOAD_DLL (the harness's build/win/somvr_logic.dll), else the host's folder's; 0 = ok */
static int watched_path(char *out, size_t cap, char *why, size_t wcap)
{
	const char *w = getenv("SOMVR_DEV_RELOAD_DLL");
	char d[MAX_PATH + 32];
	if (!w || !*w)
		snprintf(d, sizeof d, "%s\\somvr_logic.dll", g_dir), w = d;
	return host_path(w, out, cap, why, wcap);
}

static int stamp_of(const char *p, ULONGLONG *t, ULONGLONG *n)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExA(p, GetFileExInfoStandard, &a))
		return -1;
	*t = (ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime;
	*n = (ULONGLONG)a.nFileSizeHigh << 32 | a.nFileSizeLow;
	return 0;
}

/* ---- the swap's check (b37): the live module's frames around it ----------------------------------------------- */
struct vsamp {
	uint64_t frame;
	int gen, ht, hv;
	float o[3], yaw;      /* the mapping (somvr_where) */
	float tp[3], ty;      /* Talion */
	float vp[3], vyaw, vpitch; /* the rendered view: the record's eye 0 (HMD motion included) */
	double t;             /* the host's clock at the read */
};
static struct somvr_where *where_block(void)
{
	struct somvr_where *w = NULL;
	AcquireSRWLockShared(&g_pmx);
	for (int i = 0; i < g_npb; i++)
		if (!strcmp(g_pb[i].name, SOMVR_WHERE_BLOCK) && g_pb[i].size == sizeof *w)
			w = g_pb[i].p;
	ReleaseSRWLockShared(&g_pmx);
	return w;
}
static int where_read(struct somvr_where *w, struct vsamp *s)
{
	struct somvr_where c;
	if (!w || !somvr_seq_read(&w->seq, &c, w, sizeof c) || !c.frame)
		return 0;
	memset(s, 0, sizeof *s);
	s->frame = c.frame, s->gen = c.gen, s->ht = c.have_talion, s->yaw = c.yaw, s->ty = c.talion_yaw, s->t = now_s();
	memcpy(s->o, c.o, sizeof s->o), memcpy(s->tp, c.talion, sizeof s->tp);
	for (int i = 0; i < SOMVR_NREC && !s->hv; i++) { /* the same frame's record: what was rendered */
		somvr_rec *r = &g_sh.rec[i];
		float pp[3], qq[4];
		uint64_t s0 = atomic_load(&r->seq);
		if ((s0 & 1) || r->frame != c.frame)
			continue;
		memcpy(pp, r->eye[0].som_pos, 12), memcpy(qq, r->eye[0].som_quat, 16);
		if (atomic_load(&r->seq) != s0 || r->frame != c.frame)
			continue;
		sv_quat q = sv_qnormalize(sv_quat_of(qq));
		sv_vec3 f = sv_qrot(q, (sv_vec3){ 0, 0, 1 });
		memcpy(s->vp, pp, sizeof s->vp);
		s->vyaw = sv_heading(q), s->vpitch = asinf(fmaxf(-1.0f, fminf(1.0f, f.y))), s->hv = 1;
	}
	return 1;
}
/* the frames written until `until` (the host's clock), polled every 2 ms; the newest cap kept */
static int sample(struct vsamp *buf, int n, int cap, double until)
{
	struct somvr_where *w = where_block();
	uint64_t last = n ? buf[n - 1].frame : 0;
	while (now_s() < until) {
		struct vsamp s;
		if (w && where_read(w, &s) && s.frame != last) {
			if (n == cap)
				memmove(buf, buf + 1, (size_t)(cap - 1) * sizeof *buf), n--;
			buf[n++] = s, last = s.frame;
		}
		Sleep(2);
	}
	return n;
}
static float wrapf(float a)
{
	while (a > 3.14159265f)
		a -= 6.2831853f;
	while (a < -3.14159265f)
		a += 6.2831853f;
	return a;
}
/* c against b carried on by a -> b's motion per frame: the jump (cm, deg) of the mapping (view = 1) or of Talion */
static void jump_of(const struct vsamp *a, const struct vsamp *b, const struct vsamp *c, int view, double *cm, double *deg)
{
	const float *pa = view ? a->o : a->tp, *pb = view ? b->o : b->tp, *pc = view ? c->o : c->tp;
	float ya = view ? a->yaw : a->ty, yb = view ? b->yaw : b->ty, yc = view ? c->yaw : c->ty;
	double k = b->frame > a->frame ? (double)(c->frame - b->frame) / (double)(b->frame - a->frame) : 0, d2 = 0;
	for (int i = 0; i < 3; i++) {
		double p = pb[i] + (pb[i] - pa[i]) * k - pc[i];
		d2 += p * p;
	}
	*cm = sqrt(d2);
	*deg = fabs(wrapf((float)(yc - (yb + wrapf(yb - ya) * k)))) * 57.29577951;
}
static void samp_txt(const struct vsamp *s, char *out, size_t cap)
{
	int o = snprintf(out, cap, "frame %llu (gen %d): ", (unsigned long long)s->frame, s->gen);
	if (o < 0 || (size_t)o >= cap)
		return;
	if (s->hv)
		o += snprintf(out + o, cap - (size_t)o, "the view yaw %.2f pitch %.2f deg at %.1f,%.1f,%.1f", s->vyaw * 57.29578f,
			      s->vpitch * 57.29578f, s->vp[0], s->vp[1], s->vp[2]);
	else
		o += snprintf(out + o, cap - (size_t)o, "the view not in the records");
	if ((size_t)o < cap)
		o += snprintf(out + o, cap - (size_t)o, ", the mapping %.2f deg at %.1f,%.1f,%.1f", s->yaw * 57.29578f, s->o[0],
			      s->o[1], s->o[2]);
	if ((size_t)o < cap && s->ht)
		snprintf(out + o, cap - (size_t)o, ", Talion %.2f deg at %.1f,%.1f,%.1f", s->ty * 57.29578f, s->tp[0], s->tp[1], s->tp[2]);
}

/* quiet: the old module says nothing of the game is held in a state only it can end; polled every 10 ms until
 * deadline (the host's clock); 1 = quiet */
static int wait_quiet(double deadline, int *polls, char *busy, size_t cap)
{
	int q;
	while (!(q = host_live->idle(busy, cap)) && now_s() < deadline)
		Sleep(10), (*polls)++;
	return q;
}

/* `reload [force] [KEY=VALUE ...]`: the switches into the process's environment (the new module's CRT copies it at its
 * load), then the swap. src: the player's swap's module (the word's: the watched file); why: the player's reason */
static void do_reload(const char *word, const char *src_in, const char *why_in)
{
	char w[256], src[MAX_PATH + 64], dst[MAX_PATH + 96], why[400] = "";
	snprintf(w, sizeof w, "%s", word);
	int force = 0, nkv = 0;
	char kvs[200] = "";
	for (char *save = NULL, *t = src_in ? NULL : strtok_s(w, " \t\r\n", &save); t; t = strtok_s(NULL, " \t\r\n", &save)) {
		char *eq = strchr(t, '=');
		if (!strcmp(t, "force"))
			force = 1;
		else if (eq && eq != t) {
			*eq = 0;
			SetEnvironmentVariableA(t, eq + 1);
			_putenv_s(t, eq + 1);
			size_t o = strlen(kvs);
			snprintf(kvs + o, sizeof kvs - o, "%s%s=%s", nkv++ ? " " : "", t, eq + 1);
		}
	}
	int gen = hk_gen() + 1;
	if (g_nmods >= (int)(sizeof g_mods / sizeof g_mods[0])) {
		somvr_win_log("RELOAD gen %d refused: %d modules loaded (the most for one run)", gen, g_nmods);
		return;
	}
	char who[140] = "";
	if (src_in)
		snprintf(who, sizeof who, " (the player's: %s)", why_in);
	double t0 = now_s(), mp0, mw0;
	mem_mb(&mp0, &mw0);
	if (src_in ? host_path(src_in, src, sizeof src, why, sizeof why) : watched_path(src, sizeof src, why, sizeof why)) {
		somvr_win_log("RELOAD gen %d refused%s: the module's path - %s", gen, who, why);
		return;
	}
	char tmp[MAX_PATH] = "";
	GetTempPathA(sizeof tmp, tmp);
	snprintf(dst, sizeof dst, "%ssomvr-reload", tmp);
	CreateDirectoryA(dst, NULL);
	static int tries; /* a called-off swap's copy stays loaded (locked): every try its own name */
	snprintf(dst + strlen(dst), sizeof dst - strlen(dst), "\\somvr_logic.g%d.%lu.%d.dll", gen, GetCurrentProcessId(), ++tries);
	if (!CopyFileA(src, dst, FALSE)) {
		somvr_win_log("RELOAD gen %d refused%s: copy %s -> %s: error %lu", gen, who, src, dst, GetLastError());
		return;
	}
	struct host_logic *L = &g_mods[g_nmods];
	if (load_logic(dst, L, why, sizeof why) != 0) {
		somvr_win_log("RELOAD gen %d refused%s: %s", gen, who, why);
		return;
	}
	double t_load = now_s();
	const char *wv = getenv("SOMVR_DEV_RELOAD_WAIT_S");
	double wait_s = wv && *wv ? atof(wv) : 10.0, t_idle = t_load;
	int quiet = 0, polls = 0;
	char busy[200] = "";
	quiet = force || wait_quiet(t_load + wait_s, &polls, busy, sizeof busy);
	t_idle = now_s();
	if (!quiet) {
		somvr_win_log("RELOAD gen %d refused%s: not quiet for %.1f s (%s) - the old module stays%s", gen, who, wait_s, busy,
			      src_in ? "" : "; `reload force` swaps anyway");
		FreeLibrary((HMODULE)L->mod); /* its init never ran */
		return;
	}
	double pmax0;
	uint64_t pn0;
	d3d_present_stats(&pmax0, &pn0);
	host_live->stop(0);
	double t_stop = now_s();
	struct somvr_logic_boot b = g_boot;
	b.reason = 1, b.gen = gen;
	L->gen = gen;
	hk_stage_begin(gen, L->lo, L->hi);
	int rc = L->init(&b);
	d3d_replay(L);
	double t_init = now_s();
	/* the old module's last frames (it still runs the game): the check's "before" */
	static struct vsamp pre[64], post[128];
	int npre = sample(pre, 0, 64, now_s() + 0.2);
	/* busy again since the check (a kill begun during the init): wait for quiet again; never in time = called off */
	char late[200] = "";
	int lpolls = 0;
	double t_late = now_s();
	if (!force && !wait_quiet(t_late + wait_s, &lpolls, late, sizeof late)) {
		hk_stage_abort();
		L->stop(0), L->stop(1); /* the new module's writer joined, retired: never called again (its init ran: mapped) */
		g_mods[g_nmods++] = *L; /* (known: a crash note names its generation) */
		host_live->stop(2);    /* the old module runs on */
		somvr_win_log("RELOAD gen %d refused%s: called off at the swap - the game busy again after the check (%s) and not "
			      "quiet for %.1f s; nothing of the new module went live, the old module runs on", gen, who, late, wait_s);
		return;
	}
	if (lpolls)
		npre = sample(pre, npre, 64, now_s() + 0.15); /* its newest frames before the swap */
	struct hk_commit_stats cs;
	hk_commit(&cs);
	double t_commit = now_s();
	struct host_logic *old = host_live;
	g_nmods++;
	host_live = L;
	old->stop(1);
	int npost = sample(post, 0, 128, t_commit + 0.75);
	/* the check: the new module's first frame against the old one's last, carried on by its motion before */
	const struct vsamp *a = npre >= 2 ? &pre[npre - 2] : NULL, *bb = npre >= 1 ? &pre[npre - 1] : NULL, *c = NULL, *d = NULL;
	for (int i = 0; i < npost; i++)
		if (post[i].gen == gen) {
			if (!c)
				c = &post[i];
			if (post[i].t - t_commit <= 0.55)
				d = &post[i];
		}
	char vj[300], vl[900] = "";
	double vcm = 0, vdeg = 0, ccm = 0, cdeg = 0, tcm = 0, tdeg = 0;
	int measured = a && bb && c, moved = 0, body = 0;
	if (measured) {
		jump_of(a, bb, c, 1, &vcm, &vdeg);
		if (npre >= 3)
			jump_of(&pre[npre - 3], a, bb, 1, &ccm, &cdeg); /* the same measure across two old frames */
		if (a->ht && bb->ht && c->ht)
			jump_of(a, bb, c, 0, &tcm, &tdeg);
		moved = vdeg > 1.0 || vcm > 2.0;
		body = tdeg > 1.0 || tcm > 2.0; /* Talion: the same rule (a state that turns or moves the body: the bow's aim) */
		snprintf(vj, sizeof vj, "the view across the swap moved %.1f cm and turned %.2f deg (frames %llu -> %llu, the "
			 "mapping, its motion before taken out) (without a swap just before: %.1f cm, %.2f deg); Talion %.1f cm, %.2f deg",
			 vcm, vdeg, (unsigned long long)bb->frame, (unsigned long long)c->frame, ccm, cdeg, tcm, tdeg);
		char s1[300], s2[300], s3[300] = "";
		samp_txt(bb, s1, sizeof s1), samp_txt(c, s2, sizeof s2);
		if (d && d != c)
			samp_txt(d, s3, sizeof s3);
		snprintf(vl, sizeof vl, "the old module's last %s; the new module's first %s%s%s", s1, s2, s3[0] ? "; 0.5 s after: " : "", s3);
	} else {
		snprintf(vj, sizeof vj, "the view not measured (%s)", !bb ? "no stereo frame of the old module in the last 0.2 s"
							  : !c ? "no stereo frame of the new module in 0.75 s"
							       : "one frame before the swap only");
	}
	/* the frame-time hitch: the longest frame interval over the swap and the second after it */
	double rest = 1.0 - (now_s() - t_commit);
	if (rest > 0)
		Sleep((DWORD)(rest * 1e3));
	double pmax;
	uint64_t pn;
	d3d_present_stats(&pmax, &pn);
	double mp1, mw1;
	mem_mb(&mp1, &mw1);
	char st[300];
	hk_stats(st, sizeof st);
	somvr_win_log("RELOAD gen %d ok%s%s%s%s%s%s: %s (%llu KB at %#llx), init rc=%d; copy+load %.0f ms, quiet %.0f ms%s (%d polls%s%s), "
		      "stop %.0f ms, init %.0f ms, swap %.1f ms (%d threads held %.2f ms: %d slots claimed, %d back to the "
		      "game's, %d kept, %d new, %d failed, %d pages); the longest frame over the swap %.1f ms (%llu presents; "
		      "before it %.1f ms); %s; memory +%.1f MB private (%.1f MB), +%.1f MB image (%.1f MB); %s",
		      gen, who, nkv ? " with " : "", kvs, force ? " (forced)" : "", lpolls ? "; the old module busy again at the swap, quiet after " : "",
		      lpolls ? late : "", dst, (unsigned long long)((L->hi - L->lo) >> 10), (unsigned long long)L->lo, rc,
		      1e3 * (t_load - t0), 1e3 * (t_idle - t_load), quiet ? "" : " NOT QUIET", polls, busy[0] ? ": " : "", busy,
		      1e3 * (t_stop - t_idle), 1e3 * (t_init - t_stop), 1e3 * (t_commit - t_init), cs.threads, cs.freeze_ms,
		      cs.claimed, cs.released, cs.kept, cs.added, cs.failed, cs.pages, pmax, (unsigned long long)pn, pmax0, vj,
		      mp1 - mp0, mp1, mw1 - mw0, mw1, st);
	if (measured)
		somvr_win_log("RELOAD gen %d view: %s", gen, vl);
	if (moved)
		somvr_win_log_warn("RELOAD view moved at gen %d's swap: %.1f cm, %.2f deg (more than 2 cm / 1 deg; Talion %.1f cm, "
			      "%.2f deg) - a state that decides where the view points did not survive the swap (somvr_reload_game.c vk_*)",
			      gen, vcm, vdeg, tcm, tdeg);
	if (body)
		somvr_win_log_warn("RELOAD body moved at gen %d's swap: Talion %.1f cm, %.2f deg (more than 2 cm / 1 deg; the view "
				   "%.1f cm, %.2f deg) - a state that turns or moves Talion did not survive the swap", gen, tcm, tdeg, vcm, vdeg);
}

static DWORD WINAPI reload_main(void *p)
{
	(void)p;
	char path[MAX_PATH + 64], why[600] = "";
	ULONGLONG t0 = 0, n0 = 0, tc = 0, nc = 0;
	int watch = g_dev && !g_user, seen = 0;
	if (watch && watched_path(path, sizeof path, why, sizeof why)) {
		somvr_win_log("RELOAD the file watch is off: the watched path - %s (the word works; its reload is refused the same way)", why);
		watch = 0;
	}
	if (watch) {
		stamp_of(path, &t0, &n0);
		somvr_win_log("RELOAD watching %s (stamp %#llx, %llu bytes); the `reload` word works too", path, t0, n0);
	}
	for (;;) {
		DWORD r = WaitForSingleObject(g_ev, 250);
		char word[256] = "", dll[MAX_PATH + 64] = "", pwhy[96] = "";
		int kind = RQ_NONE;
		if (r == WAIT_OBJECT_0) {
			AcquireSRWLockExclusive(&g_rmx);
			kind = g_req_kind;
			if (kind == RQ_WORD)
				snprintf(word, sizeof word, "%s", g_req);
			else if (kind == RQ_PLAYER)
				snprintf(dll, sizeof dll, "%s", g_req_dll), snprintf(pwhy, sizeof pwhy, "%s", g_req_why);
			g_req[0] = 0, g_req_kind = RQ_NONE, g_running = kind != RQ_NONE;
			ReleaseSRWLockExclusive(&g_rmx);
		} else if (watch) {
			ULONGLONG t, n;
			if (stamp_of(path, &t, &n) == 0 && (t != t0 || n != n0)) {
				if (seen && t == tc && n == nc) /* the same new stamp twice (250 ms apart): the file is complete */
					snprintf(word, sizeof word, "reload"), kind = RQ_WORD;
				seen = 1, tc = t, nc = n;
			}
		}
		if (kind == RQ_WORD) {
			ULONGLONG t, n;
			if (stamp_of(path, &t, &n) == 0)
				t0 = t, n0 = n;
			seen = 0;
			const char *args = strncmp(word, "reload", 6) ? word : word + 6;
			do_reload(args, NULL, NULL);
		} else if (kind == RQ_PLAYER) {
			do_reload("", dll, pwhy);
		}
		AcquireSRWLockExclusive(&g_rmx);
		g_running = 0;
		ReleaseSRWLockExclusive(&g_rmx);
	}
	return 0;
}

/* the stand-in's reload test (SOMVR_WIN_TEST_RELOAD=N, win_start before the program's main): N swaps at once, the last
 * one without the logic's f3 hook (SOMVR_TEST_NO_F3=1: f3 must go back to the original) */
void host_reload_selftest(int n)
{
	/* b37: the module's path - a Linux one made the Windows one, a Windows one as it is, the rest refused */
	char out[MAX_PATH + 64], why[600], lin[MAX_PATH + 64] = "", win[MAX_PATH + 64] = "", lng[MAX_PATH + 300];
	int bad = 0;
	const char *dl = getenv("SOMVR_DEV_RELOAD_DLL");
	if (dl && dl[0] == '/')
		snprintf(lin, sizeof lin, "%s", dl);
	if (watched_path(win, sizeof win, why, sizeof why))
		somvr_win_log("RELOADTEST path FAIL: the watched path - %s", why), bad++;
	memset(lng, 'a', sizeof lng - 1), lng[0] = '/', lng[sizeof lng - 1] = 0;
	struct { const char *in; int ok; const char *what; } c[] = {
		{ lin, 1, "the harness's Linux path" }, { win, 1, "its Windows form" }, { "", 0, "an empty path" },
		{ "somvr_logic.dll", 0, "a relative path" }, { "C:\\no\\such\\somvr_logic.dll", 0, "a Windows path to no file" },
		{ "/no/such/somvr_logic.dll", 0, "a Linux path to no file" }, { lng, 0, "a Linux path too long" },
	};
	for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
		if (c[i].ok && !c[i].in[0])
			continue; /* (no Linux path given to this run) */
		why[0] = 0;
		int r = host_path(c[i].in, out, sizeof out, why, sizeof why) == 0;
		bad += r != c[i].ok;
		somvr_win_log("RELOADTEST path %s: %s %s%s%s", r == c[i].ok ? "ok" : "FAIL", c[i].what, r ? "-> " : "refused: ",
			      r ? out : why, r && c[i].in == win && strcmp(out, win) ? " (changed!)" : "");
	}
	if (g_ev || g_live || g_dev) { /* the player's entry refuses a bad path at once (-1), before anything is queued */
		int r = somvr_host_player_swap("C:\\no\\such\\somvr_logic.dll", "the selftest's bad path");
		bad += r != -1;
		somvr_win_log("RELOADTEST path %s: the player's swap to no file returned %d", r == -1 ? "ok" : "FAIL", r);
	}
	/* the swaps: the word's (forced), then the player's by the Linux path, the last (without f3) by the Windows path */
	for (int i = 1; i <= n; i++) {
		if (i == n) {
			SetEnvironmentVariableA("SOMVR_TEST_NO_F3", "1");
			_putenv_s("SOMVR_TEST_NO_F3", "1");
		}
		if (i == 1 || !win[0])
			do_reload(i == n ? " force SOMVR_TEST_NO_F3=1" : " force", NULL, NULL);
		else
			do_reload("", i == n || !lin[0] ? win : lin, i == n || !lin[0] ? "the selftest, a Windows path" : "the selftest, a Linux path");
	}
	somvr_win_log("RELOADTEST path: %s (%d failed)", bad ? "FAIL" : "all ok", bad);
}

/* after the boot's init (win_start): the reload thread, with SOMVR_DEV_RELOAD=1 (a developer's game: the word, the
 * file watch - not in a user's session - and the player's swap) or SOMVR_LIVE=on (the player's swap only) */
void host_reload_start(void)
{
	if (!(g_dev || g_live) || !host_live)
		return;
	g_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
	HANDLE h = g_ev ? CreateThread(NULL, 0, reload_main, NULL, 0, NULL) : NULL;
	if (h)
		CloseHandle(h);
	else
		somvr_win_log("RELOAD thread not started (%lu): no reload this run", GetLastError());
}
