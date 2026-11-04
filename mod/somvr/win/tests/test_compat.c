/* test_compat.c - win/somvr_win_compat.c's POSIX stand-ins as the W1 layer and the DLL use them (a Windows console
 * program: win/build.sh --check builds build/win/test_compat.exe, `scripts/headless_win.sh MOD --test compat` runs it
 * under Proton in its own prefix; prints "test_compat: ..." lines, exit 0 = all passed).
 *  - dlopen(name, RTLD_NOLOAD) takes a reference as glibc's does, so its dlclose drops only that one (somvr_vklayer.c
 *    same_gpu: dlopen("vulkan-1.dll", RTLD_NOLOAD) ... dlclose - without it, a native Windows player's session setup
 *    released DXVK's own reference to the Vulkan loader; somvr_void_layer.c xe_pin's pin was none)
 *  - dladdr's dli_fname stays the module's name after another dladdr (xe_pin asks twice, then opens the first's name)
 *  - somvr_safe_read: a readable buffer, an unmapped page, a page freed after it was read (the cached region's fault) */
#include <windows.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int g_fails;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			printf("test_compat: FAIL " __VA_ARGS__);                                                      \
			printf("\n");                                                                                  \
			g_fails++;                                                                                     \
		}                                                                                                      \
	} while (0)

static void test_dlopen_noload(void)
{
	const char *dll = "version.dll"; /* not loaded by a console program */
	CHECK(!GetModuleHandleA(dll), "%s is loaded before the test (pick another DLL)", dll);
	CHECK(!dlopen(dll, RTLD_NOW | RTLD_NOLOAD), "dlopen(%s, RTLD_NOLOAD) of a module not loaded is not NULL", dll);
	HMODULE own = LoadLibraryA(dll); /* the reference DXVK holds on vulkan-1.dll */
	CHECK(own != NULL, "LoadLibrary(%s) failed (%lu)", dll, GetLastError());
	if (!own)
		return;
	void *h = dlopen(dll, RTLD_NOW | RTLD_NOLOAD);
	CHECK(h == (void *)own, "dlopen(%s, RTLD_NOLOAD) = %p, not the loaded module %p", dll, h, (void *)own);
	if (h)
		dlclose(h);
	CHECK(GetModuleHandleA(dll) == own, "%s unloaded by dlopen(RTLD_NOLOAD) + dlclose: the owner's reference was dropped",
	      dll);
	if (GetModuleHandleA(dll) == own) {
		FreeLibrary(own); /* the owner lets go: now it goes (the platform unloads at 0 - the check above means it) */
		CHECK(!GetModuleHandleA(dll), "%s still loaded after its only reference was released (the test proves nothing)",
		      dll);
	}
	printf("test_compat: dlopen RTLD_NOLOAD done\n");
}

static void test_dladdr(void)
{
	Dl_info a, b;
	void *k32 = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount");
	CHECK(k32 && dladdr(k32, &a) && a.dli_fname, "dladdr(kernel32!GetTickCount) failed");
	if (!k32 || !a.dli_fname)
		return;
	char first[MAX_PATH];
	snprintf(first, sizeof first, "%s", a.dli_fname);
	CHECK(dladdr((void *)test_dladdr, &b) && b.dli_fname, "dladdr(the exe) failed");
	CHECK(!strcmp(a.dli_fname, first), "the first dladdr's dli_fname changed to '%s' (was '%s') after a second dladdr",
	      a.dli_fname, first);
	CHECK(b.dli_fname && strcmp(a.dli_fname, b.dli_fname), "two modules, one dli_fname '%s'", a.dli_fname);
	CHECK(a.dli_fbase == (void *)GetModuleHandleA(strrchr(first, '\\') ? strrchr(first, '\\') + 1 : first),
	      "dli_fbase %p is not %s's", a.dli_fbase, first);
	printf("test_compat: dladdr '%s' then '%s'\n", a.dli_fname, b.dli_fname ? b.dli_fname : "?");
}

static void test_safe_read(void)
{
	char src[64] = "readable", dst[64] = "";
	CHECK(somvr_safe_read(dst, (uintptr_t)src, sizeof src) && !strcmp(dst, "readable"), "a stack buffer not read");
	unsigned char *p = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	CHECK(p != NULL, "VirtualAlloc failed");
	if (!p)
		return;
	memset(p, 0x5a, 65536);
	unsigned char v = 0;
	CHECK(somvr_safe_read(&v, (uintptr_t)p + 100, 1) && v == 0x5a, "a committed page not read");
	VirtualFree(p, 0, MEM_RELEASE); /* the region stays in the cache: the guarded copy's fault, then forget */
	CHECK(!somvr_safe_read(&v, (uintptr_t)p + 100, 1), "a freed page read as readable");
	CHECK(!somvr_safe_read(&v, (uintptr_t)p + 200, 1), "a freed page read as readable (after the fault)");
	CHECK(!somvr_safe_read(&v, 0x1000, 1), "page 1 read");
	printf("test_compat: somvr_safe_read done\n");
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0); /* every line out before a crash */
	test_dlopen_noload();
	test_dladdr();
	test_safe_read();
	printf("test_compat: %s (%d failed)\n", g_fails ? "FAILED" : "ok", g_fails);
	return g_fails != 0;
}
