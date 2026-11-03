/* somvr_win_main.c - the Windows DLL: the xinput9_1_0 proxy, the exe gate and the start (docs/re-winport.md 4, 7.1).
 *
 * The game imports XINPUT9_1_0.dll (XInputGetState, XInputSetState): this DLL takes its place (Proton: our prefix's
 * system32 with WINEDLLOVERRIDES=xinput9_1_0=n,b; Windows: next to the exe) and forwards the four XInput functions
 * to the system's xinput1_4.dll (or xinput1_3.dll), loaded on the first call.
 *
 * DllMain (under the loader lock) does only this: the exe's sha256 against the tables' exe (somvr_wintables.inc;
 * another exe = nothing at all, no log), the site tables rewritten to the image (base + RVA), and the exe's entry
 * point redirected to win_entry. win_entry runs on the main thread before any game code, outside the loader lock:
 * it puts the entry's bytes back, runs somvr_win_start (somvr.c's somvr_start: the switches, the checks, the hooks,
 * the threads), writes the bridge file for the Linux layer (SOMVR_BRIDGE) and jumps to the game's entry.
 *
 * SOMVR_WIN_TEST=1: any exe passes the gate with every site blank (the harness's test program: the bridge, the
 * layer, DXVK's interop without the game). SOMVR_LOG unset: nothing (as on Linux).
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"
#include "somvr_win.h"
#include "somvr_win_hook.h"

uintptr_t somvr_img;
void *somvr_win_module;
int somvr_win_start(const char *who, int game); /* somvr.c */
void somvr_win_d3d_attach(uintptr_t exe, int test); /* somvr_win_d3d.c */
void somvr_win_d3d_test_start(void);

static char g_sha[65], g_exe[MAX_PATH], g_tag[32];
static int g_test;
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

static void hook_test(void);
static void win_start(void)
{
	char who[160], why[200] = "";
	snprintf(who, sizeof who, "exe sha256 %.16s (%s)", g_sha, g_tag);
	LARGE_INTEGER a, b, f;
	QueryPerformanceCounter(&a);
	int rc = somvr_win_start(who, !g_test);
	QueryPerformanceCounter(&b), QueryPerformanceFrequency(&f);
	somvr_win_log("START somvr_start rc=%d in %.1f ms", rc, 1e3 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart);
	int br = somvr_win_bridge_write(g_sha, why, sizeof why);
	somvr_win_log("BRIDGE %s: %s", br == 0 ? "written" : "not written", why);
	if (g_test) {
		somvr_win_d3d_test_start(); /* the stand-in: the game thread's locates through the PE stand-ins */
		hook_test();
	}
}

/* the stand-in's hook test: an entry trampoline on d3dtest_f1, a mid-function detour on d3dtest_f2 */
static int (*g_f1)(int, int);
static int hook_f1(int a, int b) { return g_f1(a, b) + 1000; }
static void mid_f2(struct somvr_win_regs *r) { r->rcx += 5; }
int somvr_win_abi_test(char *out, size_t cap); /* somvr.c (winport-stereo): the WRAPPER hooks' Win64 reads */
static void hook_test(void)
{
	char why[200], abi[400];
	int c = somvr_win_abi_test(abi, sizeof abi);
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
	win_start();
	return ((entry_t)g_entry)(peb);
}

static void win_attach(void)
{
	const char *lp = getenv("SOMVR_LOG");
	g_test = getenv("SOMVR_WIN_TEST") && !strcmp(getenv("SOMVR_WIN_TEST"), "1");
	if (!lp || !lp[0])
		return; /* as on Linux: no log path, nothing changes */
	GetModuleFileNameA(NULL, g_exe, sizeof g_exe);
	if (exe_sha(g_exe, g_sha) != 0)
		return;
	const char *tag = somvr_win_exe_tag(g_sha);
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
	/* the stand-in and SOMVR_WIN_SITES=0 (the inert first boot): every SITE blank, nothing patched */
	const char *ws = getenv("SOMVR_WIN_SITES");
	int none = !tag || (ws && !strcmp(ws, "0"));
	int blank = 0, com = 0, live = somvr_win_sites_apply(none ? 0 : somvr_img, &blank, &com, none);
	extern int somvr_win_sysv_blanked;
	somvr_win_log("SITES live=%d blank=%d (%d of them: a live row but a SysV-register WRAPPER hook, not ported) com=%d%s (win/sites-*.tsv; blank = no Windows site: that feature stays off)",
		      live, blank, somvr_win_sysv_blanked, com, none ? (tag ? " SOMVR_WIN_SITES=0: all blank" : " the stand-in: all blank") : "");
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
