/* somvr_win_main.c - the Windows DLL: the xinput9_1_0 proxy, the exe gate and the start (docs/re-winport.md 4, 7.1).
 *
 * The game imports XINPUT9_1_0.dll (XInputGetState, XInputSetState): this DLL takes its place (Proton: our prefix's
 * system32 with WINEDLLOVERRIDES=xinput9_1_0=n,b; Windows: next to the exe) and forwards the four XInput functions
 * to the system's xinput1_4.dll (or xinput1_3.dll), loaded on the first call.
 *
 * This DLL is the host (somvr_win_host.h, docs/re-xr-pipeline.md 156): the game side proper is somvr_logic.dll in the
 * same folder. DllMain (under the loader lock) does only this: the exe's sha256 against the known exes
 * (somvr_winexes.inc; another exe = nothing at all, no log), D3D's two IAT entries, and the exe's entry point
 * redirected to win_entry. win_entry runs on the main thread before any game code, outside the loader lock: it puts
 * the entry's bytes back, loads the logic module and runs its init (the site tables rewritten to the image, somvr.c's
 * somvr_start: the switches, the checks, the hooks, the threads), writes the bridge file for the Linux layer
 * (SOMVR_BRIDGE), starts the reload thread (SOMVR_DEV_RELOAD=1 only) and jumps to the game's entry.
 *
 * SOMVR_WIN_TEST=1: any exe passes the gate with every site blank (the harness's test program: the bridge, the
 * layer, DXVK's interop without the game). SOMVR_LOG unset: nothing (as on Linux).
 */
#define SOMVR_HOST 1
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"
#include "somvr_win.h"
#include "somvr_win_hook.h"
#include "somvr_win_hostint.h"
#include "somvr_winexes.inc" /* wintables.py: the exes the tables know (tag, sha256) */

uintptr_t somvr_img;
void *somvr_win_module;
int host_boot(const struct somvr_logic_boot *b); /* somvr_win_host.c */
void host_reload_start(void);
void host_reload_selftest(int n);
void somvr_win_d3d_attach(uintptr_t exe, int test); /* somvr_win_d3d.c */
void somvr_win_d3d_test_start(void);

static char g_sha[65], g_exe[MAX_PATH], g_tag[32];
static int g_test, g_sites_off;
static FILE *g_wlog;
static uintptr_t g_entry;
static unsigned char g_entry_bytes[16];

void somvr_win_log(const char *fmt, ...)
{
	if (!g_wlog)
		return;
	LARGE_INTEGER n, f;
	QueryPerformanceCounter(&n), QueryPerformanceFrequency(&f);
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	fprintf(g_wlog, "WIN t=%.6f tid=%lu %s\n", (double)n.QuadPart / (double)f.QuadPart, GetCurrentThreadId(), line);
	fflush(g_wlog);
}

/* b37: a host line the runs' gate counts (`^WARN`, logs/coord/final.sh): "WARN <text> (WIN t=... tid=...)" */
void somvr_win_log_warn(const char *fmt, ...)
{
	if (!g_wlog)
		return;
	LARGE_INTEGER n, f;
	QueryPerformanceCounter(&n), QueryPerformanceFrequency(&f);
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	fprintf(g_wlog, "WARN %s (WIN t=%.6f tid=%lu)\n", line, (double)n.QuadPart / (double)f.QuadPart, GetCurrentThreadId());
	fflush(g_wlog);
}

static int exe_sha(const char *path, char out[65])
{
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return -1;
	static unsigned char buf[1 << 20];
	somvr_sha256 s;
	somvr_sha256_init(&s);
	DWORD got;
	while (ReadFile(h, buf, sizeof buf, &got, NULL) && got)
		somvr_sha256_add(&s, buf, got);
	CloseHandle(h);
	somvr_sha256_hex(&s, out);
	return 0;
}

/* ---- the XInput forwards ------------------------------------------------------------------------------------ */
typedef DWORD(WINAPI *xi_get_t)(DWORD, void *);
typedef DWORD(WINAPI *xi_caps_t)(DWORD, DWORD, void *);
typedef DWORD(WINAPI *xi_guids_t)(DWORD, GUID *, GUID *);
static INIT_ONCE g_xi_once = INIT_ONCE_STATIC_INIT;
static struct {
	HMODULE h;
	xi_get_t get, set;
	xi_caps_t caps;
	xi_guids_t guids;
	char name[16];
} X;

static BOOL CALLBACK xi_load(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o, (void)p, (void)c;
	static const char *const names[] = { "xinput1_4.dll", "xinput1_3.dll" };
	for (int i = 0; i < 2 && !X.h; i++)
		if ((X.h = LoadLibraryA(names[i])))
			snprintf(X.name, sizeof X.name, "%s", names[i]);
	if (X.h) {
		X.get = (xi_get_t)(void *)GetProcAddress(X.h, "XInputGetState");
		X.set = (xi_get_t)(void *)GetProcAddress(X.h, "XInputSetState");
		X.caps = (xi_caps_t)(void *)GetProcAddress(X.h, "XInputGetCapabilities");
		X.guids = (xi_guids_t)(void *)GetProcAddress(X.h, "XInputGetDSoundAudioDeviceGuids");
	}
	somvr_win_log("XINPUT forwards to %s (get %p set %p caps %p)", X.h ? X.name : "nothing", (void *)X.get,
		      (void *)X.set, (void *)X.caps);
	return TRUE;
}

#define XI_INIT() InitOnceExecuteOnce(&g_xi_once, xi_load, NULL, NULL)
__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD i, void *st)
{
	XI_INIT();
	return X.get ? X.get(i, st) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD i, void *vib)
{
	XI_INIT();
	return X.set ? X.set(i, vib) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD flags, void *caps)
{
	XI_INIT();
	return X.caps ? X.caps(i, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD i, GUID *r, GUID *c)
{
	XI_INIT();
	if (X.guids)
		return X.guids(i, r, c);
	if (r)
		memset(r, 0, sizeof *r);
	if (c)
		memset(c, 0, sizeof *c);
	return ERROR_DEVICE_NOT_CONNECTED;
}

/* ---- the start: the exe's entry point, redirected once -------------------------------------------------------- */
static int write_bytes(uintptr_t at, const void *src, size_t n)
{
	DWORD old, o2;
	if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old))
		return -1;
	memcpy((void *)at, src, n);
	VirtualProtect((void *)at, n, old, &o2);
	FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
	return 0;
}

/* ---- the crash note (winport-combat, 11-03): a fault in the game's process logged before the game's own handler
 * ends it (kills31's merged boot 033929 exited with 5 and nothing in wine.log: the game's handler exits quietly, and its
 * "Critical Error" asserts write to address 0 on Windows where Linux only stores the message). Read only: the code, the
 * place (exe/DLL RVA), the access, the game's error message (RVA 0x18f5b08, the asserts' store), the exe return
 * addresses on the stack; then the search goes on (the game's handlers decide). At most 8 notes. */
static volatile LONG g_crash_notes;
extern __declspec(thread) int somvr_win_in_guard; /* somvr_win_compat.c: inside a guarded copy (its fault is handled) */
static const char *crash_where(uintptr_t a, char *buf, size_t cap)
{
	uintptr_t dll = (uintptr_t)somvr_win_module, lb = 0;
	int gen;
	const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(somvr_img + (uintptr_t)((const IMAGE_DOS_HEADER *)somvr_img)->e_lfanew);
	const IMAGE_NT_HEADERS64 *dn = dll ? (const IMAGE_NT_HEADERS64 *)(dll + (uintptr_t)((const IMAGE_DOS_HEADER *)dll)->e_lfanew) : NULL;
	if (a >= somvr_img && a < somvr_img + nt->OptionalHeader.SizeOfImage)
		snprintf(buf, cap, "exe+%#llx", (unsigned long long)(a - somvr_img));
	else if (dn && a >= dll && a < dll + dn->OptionalHeader.SizeOfImage)
		snprintf(buf, cap, "dll+%#llx", (unsigned long long)(a - dll));
	else if ((gen = host_module_of(a, &lb)) >= 0) /* the logic (docs/re-xr-pipeline.md 156): somvr_logic.pdb's RVAs */
		snprintf(buf, cap, "logic%d+%#llx%s", gen, (unsigned long long)(a - lb), host_live && gen != host_live->gen ? " (retired)" : "");
	else
		snprintf(buf, cap, "%#llx", (unsigned long long)a);
	return buf;
}
static LONG CALLBACK crash_note(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
	    code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_IN_PAGE_ERROR)
		return EXCEPTION_CONTINUE_SEARCH;
	if (somvr_win_in_guard && (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR))
		return EXCEPTION_CONTINUE_SEARCH; /* a safe read's or write's: its own handler takes it (no crash, no note) */
	if (InterlockedIncrement(&g_crash_notes) > 8)
		return EXCEPTION_CONTINUE_SEARCH;
	char w[48], acc[96] = "", msg[120] = "-", st[400] = "";
	uintptr_t pc = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
	if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
		snprintf(acc, sizeof acc, " %s %#llx", ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" :
			 ep->ExceptionRecord->ExceptionInformation[0] == 8 ? "executing" : "reading",
			 (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
	uintptr_t m = 0;
	MEMORY_BASIC_INFORMATION mb;
	if (VirtualQuery((const void *)(somvr_img + 0x18f5b08), &mb, sizeof mb) && mb.State == MEM_COMMIT)
		m = *(const uintptr_t *)(somvr_img + 0x18f5b08);
	if (m && VirtualQuery((const void *)m, &mb, sizeof mb) && mb.State == MEM_COMMIT && !(mb.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		snprintf(msg, sizeof msg, "'%.100s'", (const char *)m);
	const uintptr_t *sp = (const uintptr_t *)ep->ContextRecord->Rsp;
	size_t o = 0;
	for (int i = 0, n = 0; i < 96 && n < 10 && VirtualQuery(sp + i, &mb, sizeof mb) && mb.State == MEM_COMMIT; i++) {
		char b[48];
		crash_where(sp[i], b, sizeof b);
		if (!strncmp(b, "exe+", 4) || !strncmp(b, "dll+", 4) || !strncmp(b, "logic", 5))
			o += (size_t)snprintf(st + o, o < sizeof st ? sizeof st - o : 0, " %s", b), n++;
		if (o >= sizeof st)
			break;
	}
	somvr_win_log("CRASH code %#lx at %s%s; the game's error message %s; rsp %#llx; on the stack:%s (logged; the game's "
		      "handlers decide)", code, crash_where(pc, w, sizeof w), acc, msg, (unsigned long long)ep->ContextRecord->Rsp, st);
	return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- the exit note (som-b33-soak, 11-03; docs/re-winport.md 7.13): who ends the process. The recheck's game exited
 * with 0 after 9 minutes - no fault, no CRASH note, no quit request: an exit by a call. Entry hooks (MinHook) log the
 * caller and the stack before the original runs: ntdll's RtlExitUserProcess (every ExitProcess / CRT exit / a return
 * from WinMain), kernelbase's TerminateProcess on this process, user32's PostQuitMessage and DestroyWindow (a main
 * loop that ends on WM_QUIT; the game's window destroyed). Read only; SOMVR_WIN_EXITLOG=0 = none. */
static void exit_stack(char *st, size_t cap)
{
	void *fr[28];
	USHORT n = RtlCaptureStackBackTrace(2, 28, fr, NULL);
	size_t o = 0;
	st[0] = 0;
	for (USHORT i = 0; i < n && o + 64 < cap; i++) {
		char b[48];
		HMODULE m = NULL;
		crash_where((uintptr_t)fr[i], b, sizeof b);
		if (strncmp(b, "exe+", 4) && strncmp(b, "dll+", 4) &&
		    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				       (LPCSTR)fr[i], &m) && m) {
			char path[MAX_PATH], *base;
			if (GetModuleFileNameA(m, path, sizeof path)) {
				base = strrchr(path, '\\');
				snprintf(b, sizeof b, "%.24s+%#llx", base ? base + 1 : path, (unsigned long long)((uintptr_t)fr[i] - (uintptr_t)m));
			}
		}
		o += (size_t)snprintf(st + o, cap - o, " %s", b);
	}
}
static volatile LONG g_exit_notes;
typedef void(NTAPI *rtl_exit_t)(DWORD);
typedef BOOL(WINAPI *term_t)(HANDLE, UINT);
typedef void(WINAPI *pqm_t)(int);
typedef BOOL(WINAPI *dw_t)(HWND);
static rtl_exit_t g_rtl_exit;
static term_t g_term;
static pqm_t g_pqm;
static dw_t g_dw;
static void exit_note(const char *what, unsigned long long code, uintptr_t caller)
{
	if (InterlockedIncrement(&g_exit_notes) > 12)
		return;
	char w[48], st[1400];
	exit_stack(st, sizeof st);
	somvr_win_log("EXIT %s %llu (%#llx) called from %s; thread %lu; the stack:%s", what, code, code,
		      crash_where(caller, w, sizeof w), GetCurrentThreadId(), st);
}
static void NTAPI exit_rtl(DWORD code)
{
	exit_note("RtlExitUserProcess", code, (uintptr_t)__builtin_return_address(0));
	g_rtl_exit(code);
}
static BOOL WINAPI exit_term(HANDLE h, UINT code)
{
	if (h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId())
		exit_note("TerminateProcess(this process)", code, (uintptr_t)__builtin_return_address(0));
	return g_term(h, code);
}
static void WINAPI exit_pqm(int code)
{
	exit_note("PostQuitMessage", (unsigned)code, (uintptr_t)__builtin_return_address(0));
	g_pqm(code);
}
static BOOL WINAPI exit_dw(HWND h)
{
	char cls[64] = "";
	GetClassNameA(h, cls, sizeof cls);
	if (!GetParent(h) && IsWindowVisible(h)) { /* a top-level visible window: the game's (not a hidden helper's) */
		char what[120];
		snprintf(what, sizeof what, "DestroyWindow(top-level '%.40s' %#llx)", cls, (unsigned long long)(uintptr_t)h);
		exit_note(what, 0, (uintptr_t)__builtin_return_address(0));
	}
	return g_dw(h);
}
static void exit_hooks(void)
{
	const char *e = getenv("SOMVR_WIN_EXITLOG");
	if (e && !strcmp(e, "0")) {
		somvr_win_log("EXIT notes off (SOMVR_WIN_EXITLOG=0)");
		return;
	}
	static const struct {
		const char *mod, *fn;
		void *hook, **orig;
	} h[] = {
		{ "ntdll.dll", "RtlExitUserProcess", (void *)exit_rtl, (void **)&g_rtl_exit },
		{ "kernelbase.dll", "TerminateProcess", (void *)exit_term, (void **)&g_term },
		{ "user32.dll", "PostQuitMessage", (void *)exit_pqm, (void **)&g_pqm },
		{ "user32.dll", "DestroyWindow", (void *)exit_dw, (void **)&g_dw },
	};
	char ok[200] = "";
	size_t o = 0;
	for (size_t i = 0; i < sizeof h / sizeof h[0]; i++) {
		HMODULE m = GetModuleHandleA(h[i].mod);
		if (!m)
			m = LoadLibraryA(h[i].mod);
		FARPROC f = m ? GetProcAddress(m, h[i].fn) : NULL;
		char why[160] = "no export";
		int r = f ? somvr_win_hook_entry((uintptr_t)f, h[i].hook, h[i].orig, why, sizeof why) : -1;
		o += (size_t)snprintf(ok + o, o < sizeof ok ? sizeof ok - o : 0, " %s %s", h[i].fn, r == 0 ? "ok" : why);
		if (o >= sizeof ok)
			break;
	}
	somvr_win_log("EXIT notes (who ends the process; SOMVR_WIN_EXITLOG=0 = none):%s", ok);
}

static void hook_test(void);
static void win_start(void)
{
	char who[160], why[200] = "";
	snprintf(who, sizeof who, "exe sha256 %.16s (%s)", g_sha, g_tag);
	LARGE_INTEGER a, b, f;
	QueryPerformanceCounter(&a);
	if (!g_test) {
		AddVectoredExceptionHandler(1, crash_note); /* first: before the game's own (it ends the process quietly) */
		exit_hooks(); /* who ends the process (7.13) */
	}
	struct somvr_logic_boot bt = { .size = sizeof bt, .reason = 0, .gen = 0, .test = g_test, .sites_off = g_sites_off,
				       .img = somvr_img, .sha = g_sha, .tag = g_tag, .who = who };
	int rc = host_boot(&bt);
	QueryPerformanceCounter(&b), QueryPerformanceFrequency(&f);
	somvr_win_log("START somvr_start rc=%d in %.1f ms", rc, 1e3 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart);
	int br = somvr_win_bridge_write(g_sha, why, sizeof why);
	somvr_win_log("BRIDGE %s: %s", br == 0 ? "written" : "not written", why);
	host_reload_start();
	if (g_test) {
		somvr_win_d3d_test_start(); /* the stand-in: the game thread's locates through the PE stand-ins */
		hook_test();
		const char *rt = getenv("SOMVR_WIN_TEST_RELOAD"); /* docs/re-xr-pipeline.md 156: N swaps of the logic */
		if (rt && atoi(rt) > 0)
			host_reload_selftest(atoi(rt));
	}
}

/* the stand-in's hook test: an entry trampoline on d3dtest_f1, a mid-function detour on d3dtest_f2 */
static int (*g_f1)(int, int);
static int hook_f1(int a, int b) { return g_f1(a, b) + 1000; }
static void mid_f2(struct somvr_win_regs *r) { r->rcx += 5; }
static void hook_test(void)
{
	char why[200], abi[400] = "no logic module";
	int c = host_live && host_live->abitest ? host_live->abitest(abi, sizeof abi) : -1; /* somvr.c (winport-stereo) */
	somvr_win_log("HOOKTEST wrapper-abi rc=%d %s%s", c, c ? "FAILED: " : "", abi);
	uintptr_t f1 = (uintptr_t)GetProcAddress(GetModuleHandleA(NULL), "d3dtest_f1");
	uintptr_t f2 = (uintptr_t)GetProcAddress(GetModuleHandleA(NULL), "d3dtest_f2");
	if (!f1 || !f2)
		return;
	int a = somvr_win_hook_entry(f1, (void *)hook_f1, (void **)&g_f1, why, sizeof why);
	somvr_win_log("HOOKTEST entry rc=%d %s", a, why);
	int b = somvr_win_hook_mid(f2, mid_f2, why, sizeof why);
	somvr_win_log("HOOKTEST mid rc=%d %s", b, why);
}

typedef int(WINAPI *entry_t)(void *peb);
static int WINAPI win_entry(void *peb)
{
	if (write_bytes(g_entry, g_entry_bytes, sizeof g_entry_bytes) != 0) {
		somvr_win_log("ENTRY restore failed (%lu): exiting", GetLastError());
		ExitProcess(3);
	}
	somvr_win_log("ENTRY main thread before the game's entry %#llx", (unsigned long long)(g_entry - somvr_img));
	/* the game's main thread (the game thread) named for the layer's per-thread CPU (Wine copies a thread's description
	 * into its Linux comm; /proc/self/task/<tid>/schedstat is the only fine CPU clock under Wine) */
	pthread_setname_np(pthread_self(), "somvr-game");
	win_start();
	return ((entry_t)g_entry)(peb);
}

/* the folder of this DLL + "\\somvr" (a native Windows player's package: win/package.sh) */
static void mod_dir(char *out, size_t cap)
{
	char me[MAX_PATH] = "";
	GetModuleFileNameA((HMODULE)somvr_win_module, me, sizeof me);
	char *sl = strrchr(me, '\\');
	if (sl)
		*sl = 0;
	snprintf(out, cap, "%s\\somvr", me);
}

/* W1, a native player: somvr\somvr.env's K=V lines into the process environment (a variable already set wins:
 * tests), SOMVR_LOG defaulting to somvr\somvr.log; returns the lines taken */
static int load_env_file(const char *dir)
{
	char path[MAX_PATH + 32], line[1024];
	snprintf(path, sizeof path, "%s\\somvr.env", dir);
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	int n = 0;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *eq = strchr(line, '=');
		if (line[0] == '#' || !eq || eq == line)
			continue;
		*eq = 0;
		if (getenv(line))
			continue;
		_putenv_s(line, eq + 1);           /* the CRT's copy (getenv) */
		SetEnvironmentVariableA(line, eq + 1); /* the process's (the layer's CRT, the loaders) */
		n++;
	}
	fclose(f);
	if (!getenv("SOMVR_LOG")) {
		char lp[MAX_PATH + 32];
		snprintf(lp, sizeof lp, "%s\\somvr.log", dir);
		_putenv_s("SOMVR_LOG", lp), SetEnvironmentVariableA("SOMVR_LOG", lp);
	}
	/* build 34: the VR menu's settings kept between starts (Handedness takes effect at the next start): the package's
	 * own file; a SOMVR_SETTINGS already set (the user's launcher, somvr.env) wins */
	if (!getenv("SOMVR_SETTINGS")) {
		char sp[MAX_PATH + 32];
		snprintf(sp, sizeof sp, "%s\\settings.conf", dir);
		_putenv_s("SOMVR_SETTINGS", sp), SetEnvironmentVariableA("SOMVR_SETTINGS", sp);
	}
	return n;
}

/* W1: our Windows Vulkan layer under the Khronos loader (the package's vulkan-1.dll), before d3d11.dll (DXVK) makes
 * its instance: native Windows, or SOMVR_LAYER=win under Wine (the test); under Wine the Linux layer is the default */
static char g_w1[700];

/* The W1 test under Wine: DXVK (vulkan_loader.cpp) tries LoadLibraryA("winevulkan.dll") before "vulkan-1.dll", so
 * under Wine it skips the Khronos loader and our layer. d3d11.dll's and dxgi.dll's LoadLibraryA (their IAT entries)
 * refuse that name, as on Windows where it does not exist; the Khronos loader loads winevulkan.dll as its ICD itself
 * (its own LoadLibraryExW, untouched). Nothing of this on Windows. */
typedef HMODULE(WINAPI *lla_t)(LPCSTR);
static lla_t g_lla;
static HMODULE WINAPI w1_load_library(LPCSTR name)
{
	const char *b = name, *q;
	for (q = name; q && *q; q++)
		if (*q == '\\' || *q == '/')
			b = q + 1;
	if (b && !_stricmp(b, "winevulkan.dll")) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return NULL;
	}
	return g_lla(name);
}

/* The Khronos loader ignores VK_ADD_LAYER_PATH (and VK_LOADER_DEBUG) in an elevated process ("Loader is running with
 * elevated permissions"): the token's elevation and integrity level, for the log */
static void w1_token(char *out, size_t cap)
{
	HANDLE t;
	TOKEN_ELEVATION e = { 0 };
	DWORD n = 0, rid = 0;
	unsigned char il[64];
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
		snprintf(out, cap, "token ?");
		return;
	}
	GetTokenInformation(t, TokenElevation, &e, sizeof e, &n);
	if (GetTokenInformation(t, TokenIntegrityLevel, il, sizeof il, &n)) {
		PSID sid = ((TOKEN_MANDATORY_LABEL *)il)->Label.Sid;
		rid = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
	}
	CloseHandle(t);
	snprintf(out, cap, "elevated %llu, integrity %#llx%s", (unsigned long long)e.TokenIsElevated, (unsigned long long)rid,
		 rid < 0x3000 ? "" : somvr_under_wine() ? " (high, Wine's token: the Vulkan loader ignores VK_ADD_LAYER_PATH; the layer must be registered, HKLM ExplicitLayers)"
		 : " (high: the Vulkan loader ignores VK_ADD_LAYER_PATH; start the game normally, not as administrator)");
}

static void w1_wine_dxvk(char *out, size_t cap)
{
	const char *dll[] = { "d3d11.dll", "dxgi.dll" };
	size_t o = 0;
	for (int i = 0; i < 2; i++) {
		HMODULE m = GetModuleHandleA(dll[i]);
		void **at = m ? somvr_win_iat_slot((uintptr_t)m, "KERNEL32.dll", "LoadLibraryA") : NULL;
		const char *st = !m ? "not loaded" : !at ? "no LoadLibraryA import" : "refused";
		if (at) {
			if (!g_lla)
				g_lla = (lla_t)*at;
			if (*at != (void *)w1_load_library && somvr_win_put_ptr(at, (void *)w1_load_library) != 0)
				st = "NOT patched";
		}
		o += snprintf(out + o, o < cap ? cap - o : 0, "%s%s %s", i ? ", " : "", dll[i], st);
	}
}
static void w1_layer(const char *dir)
{
	const char *m = getenv("SOMVR_LAYER");
	int want = m ? !strcmp(m, "win") : !somvr_under_wine();
	char json[MAX_PATH + 64];
	snprintf(json, sizeof json, "%s\\VkLayer_somvr_openxr.json", dir);
	if (!want) {
		snprintf(g_w1, sizeof g_w1, "off (%s)", m ? "SOMVR_LAYER" : "under Wine: the Linux layer");
		return;
	}
	char tk[200];
	w1_token(tk, sizeof tk);
	if (GetFileAttributesA(json) == INVALID_FILE_ATTRIBUTES) {
		snprintf(g_w1, sizeof g_w1, "wanted but no %s (%s)", json, tk);
		return;
	}
	char v[2048] = "";
	DWORD k = GetEnvironmentVariableA("VK_ADD_LAYER_PATH", v, sizeof v);
	char nv[2400];
	snprintf(nv, sizeof nv, "%s%s%s", dir, k && k < sizeof v ? ";" : "", k && k < sizeof v ? v : "");
	SetEnvironmentVariableA("VK_ADD_LAYER_PATH", nv);
	k = GetEnvironmentVariableA("VK_INSTANCE_LAYERS", v, sizeof v);
	snprintf(nv, sizeof nv, "VK_LAYER_SOMVR_openxr%s%s", k && k < sizeof v ? ";" : "", k && k < sizeof v ? v : "");
	SetEnvironmentVariableA("VK_INSTANCE_LAYERS", nv);
	if (!GetEnvironmentVariableA("SOMVR_XR", v, sizeof v))
		SetEnvironmentVariableA("SOMVR_XR", "1");
	char wd[260] = "";
	if (somvr_under_wine()) {
		w1_wine_dxvk(wd, sizeof wd);
		/* XR_RUNTIME_JSON with a Unix path is the host's (wineopenxr's libopenxr_loader.so reads it from the Unix
		 * environment, which this does not change); openxr_loader.dll would take it before the registry's runtime */
		DWORD xk = GetEnvironmentVariableA("XR_RUNTIME_JSON", v, sizeof v);
		if (xk && xk < sizeof v && v[0] == '/') {
			SetEnvironmentVariableA("XR_RUNTIME_JSON", NULL);
			size_t o = strlen(wd);
			snprintf(wd + o, sizeof wd - o, "; XR_RUNTIME_JSON (the host's) hidden from openxr_loader.dll");
		}
	}
	snprintf(g_w1, sizeof g_w1, "on: VK_ADD_LAYER_PATH=%s, VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr; %s%s%s%s", dir, tk,
		 wd[0] ? "; under Wine DXVK's LoadLibraryA(\"winevulkan.dll\"): " : "", wd,
		 wd[0] ? " (it takes vulkan-1.dll as on Windows)" : "");
}

/* H4's first call (the game thread, after the game's start named its threads): the layer's per-thread CPU finds the
 * game thread by its Linux name (/proc/self/task/N/comm); Wine copies a thread's description there when the thread
 * sets its own. The name the game gave it goes into the log. */
void somvr_win_name_game_thread(void)
{
	typedef HRESULT(WINAPI * get_t)(HANDLE, PWSTR *);
	get_t get = (get_t)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
	PWSTR w = NULL;
	char old[64] = "?";
	if (get && SUCCEEDED(get(GetCurrentThread(), &w)) && w) {
		WideCharToMultiByte(CP_UTF8, 0, w, -1, old, sizeof old, NULL, NULL);
		LocalFree(w);
	}
	pthread_setname_np(pthread_self(), "somvr-game");
	somvr_win_log("THREAD the game thread (H4, Windows id %lu): '%s' -> somvr-game", GetCurrentThreadId(), old);
}

static void win_attach(void)
{
	char mdir[MAX_PATH + 16];
	mod_dir(mdir, sizeof mdir);
	int env_lines = load_env_file(mdir);
	const char *lp = getenv("SOMVR_LOG");
	g_test = getenv("SOMVR_WIN_TEST") && !strcmp(getenv("SOMVR_WIN_TEST"), "1");
	if (!lp || !lp[0])
		return; /* as on Linux: no log path, nothing changes */
	GetModuleFileNameA(NULL, g_exe, sizeof g_exe);
	if (exe_sha(g_exe, g_sha) != 0)
		return;
	const char *tag = NULL;
	for (int i = 0; i < SOMVR_HOST_NEXES; i++)
		if (!strcmp(g_sha, g_host_exes[i].sha256))
			tag = g_host_exes[i].tag;
	if (!tag && !g_test)
		return; /* another exe: nothing */
	snprintf(g_tag, sizeof g_tag, "%s", tag ? tag : "test");
	char pb[1024];
	g_wlog = fopen(somvr_path(lp, pb, sizeof pb), "ab");
	somvr_img = (uintptr_t)GetModuleHandleA(NULL);
	char me[MAX_PATH] = "";
	GetModuleFileNameA((HMODULE)somvr_win_module, me, sizeof me);
	somvr_win_log("ATTACH dll=%s exe=%s sha256=%s tag=%s base=%#llx wine=%d", me, g_exe, g_sha, g_tag,
		      (unsigned long long)somvr_img, somvr_under_wine());
	w1_layer(mdir);
	somvr_win_log("W1 %s; settings %s (%d lines)", g_w1, env_lines >= 0 ? "somvr\\somvr.env" : "the environment only",
		      env_lines);
	/* the stand-in and SOMVR_WIN_SITES=0 (the inert first boot): every SITE blank, nothing patched (the logic's init) */
	const char *ws = getenv("SOMVR_WIN_SITES");
	g_sites_off = !tag || (ws && !strcmp(ws, "0"));
	somvr_win_d3d_attach(somvr_img, g_test);
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)somvr_img;
	const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(somvr_img + (uintptr_t)dos->e_lfanew);
	g_entry = somvr_img + nt->OptionalHeader.AddressOfEntryPoint;
	memcpy(g_entry_bytes, (const void *)g_entry, sizeof g_entry_bytes);
	unsigned char jmp[16] = { 0x48, 0xb8 }; /* mov rax, imm64; jmp rax; int3 x4 */
	uintptr_t to = (uintptr_t)win_entry;
	memcpy(jmp + 2, &to, 8);
	jmp[10] = 0xff, jmp[11] = 0xe0;
	memset(jmp + 12, 0xcc, 4);
	if (write_bytes(g_entry, jmp, sizeof jmp) != 0)
		somvr_win_log("ENTRY redirect failed (%lu): the mod stays off", GetLastError());
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
	(void)r;
	if (why == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(h);
		somvr_win_module = h;
		win_attach();
	}
	return TRUE;
}
