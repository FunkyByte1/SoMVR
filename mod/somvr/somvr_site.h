/* somvr_site.h - the game binary's patch/proof sites, struct offsets and globals, for both builds (win/README.md 2).
 *
 *   SITE_TABLE     every `struct site_spec` table: `static SITE_TABLE struct site_spec g_x_sites_game[] = {...}`.
 *                  Linux: const. Windows: the writable section .somvrst, rewritten by name from win/sites-*.tsv at
 *                  start (somvr_win_sites_apply: base + RVA; a name with no live Windows row is blanked, so its
 *                  check fails "no Windows site" and its feature stays off).
 *   OFS(N, v)      a field / vtable offset or size that may differ: Linux (v); Windows WOFS_N (win/offsets-*.tsv,
 *                  else the Linux value, counted unchecked). An integer constant on both.
 *   GADDR(N, v)    a game global: Linux the ELF address v; Windows the image base + the running exe's RVA of N
 *                  (win/offsets-*.tsv, carried to other exes by scripts/winport_exe_map.py), 0 if unknown.
 */
#ifndef SOMVR_SITE_H
#define SOMVR_SITE_H
#include <stdint.h>

/* K_JMP: an E9 rel32 tail jump at addr (patched like a call); K_ENTRY: a function we only call (addr = target, its
 * first bytes checked; never patched) */
enum { K_CALL = 1, K_VSLOT = 2, K_JMP = 3, K_ENTRY = 4 };
struct site_spec {
	const char *name;
	int kind;
	uintptr_t addr;         /* K_CALL/K_JMP: the E8/E9 byte; K_VSLOT: the 8-byte slot; K_ENTRY: the function */
	uintptr_t ctx;          /* start of the bytes checked (contains addr) */
	const char *ctx_hex;    /* expected bytes from ctx on */
	uintptr_t target;       /* expected call target / slot value */
	const char *prologue_hex; /* expected first bytes of the target */
};
#define SITE(n, k, a, c, h, t, p) { n, k, a, c, h, t, p }

#ifdef _WIN32
#include "somvr_winofs.h" /* generated into build/win by win/wintables.py */
extern uintptr_t somvr_img;     /* the exe's image base (GetModuleHandle(NULL)) */
extern const uint32_t *somvr_win_gaddr; /* the running exe's global RVAs (WGI_* order; 0 = unknown) */
#define SITE_TABLE __attribute__((section(".somvrst$m"), used))
#define OFS(n, v) (WOFS_##n)
#define GADDR(n, v) (somvr_win_gaddr[WGI_##n] ? somvr_img + (uintptr_t)somvr_win_gaddr[WGI_##n] : (uintptr_t)0)
#else
#define SITE_TABLE const
#define OFS(n, v) (v)
#define GADDR(n, v) ((uintptr_t)(v))
#endif

#endif
