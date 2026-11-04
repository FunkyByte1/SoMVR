/* somvr_win.h - the Windows DLL's own interfaces (win/README.md): the generated tables, the site rebase, the bridge */
#ifndef SOMVR_WIN_H
#define SOMVR_WIN_H
#include <stddef.h>
#include <stdint.h>
#include "../somvr_site.h"

#define SOMVR_WIN_PREFERRED_BASE 0x140000000ull

/* one live row of win/sites-*.tsv (wintables.py: somvr_wintables.inc) */
struct somvr_win_site {
	const char *name;
	int kind;
	uint32_t rva, ctx_rva;
	const char *ctx;
	uint32_t target_rva;
	const char *prologue;
	int confirmed;
	uint64_t ctx_reloc, pro_reloc; /* bit i: a DIR64 relocation at byte i (the loader adds base - preferred) */
};
/* a COM slot row: the D3D11/DXGI interface and the method index (IUnknown's 3 included) */
struct somvr_win_com {
	const char *name;
	const char *iface;
	int slot;
};

/* one exe build the tables know (wintables.py: the Steam-Windows exe, and --extra-exe ones, e.g. GOG's) */
struct somvr_win_exe {
	const char *tag, *sha256;
	const struct somvr_win_site *sites; /* sorted by name */
	int nsites;
	const uint32_t *gaddr;              /* WGI_* order */
};

/* rewrite every SITE_TABLE entry in .somvrst by its name: live rows -> base + RVA, the rest blanked.
 * Returns the number of entries rewritten live; *blank gets the blanked count, *com the COM entries (blank until
 * somvr_win_com_fill). Call once, before any check_site. */
int somvr_win_sites_apply(uintptr_t base, int *blank, int *com, int all_blank);
/* the table entry (in .somvrst) of a SITE name, NULL if none; for the COM fill and the logs */
struct site_spec *somvr_win_site_entry(const char *name);
/* the generated COM rows */
const struct somvr_win_com *somvr_win_com_rows(int *n);
/* fill a COM SITE from a live vtable slot (addr = &vtbl[slot], target = its value, its own 8 bytes as context);
 * 0 = done. The caller (winport-core's install path) then checks and patches it as on Linux. */
int somvr_win_com_fill(const char *name, void **vtbl);

/* the exe gate: the tag of the running exe ("steamwin", "gog", ...) or NULL; sha = its sha256 (hex). Selects that
 * exe's sites and globals for somvr_win_sites_apply and GADDR. */
const char *somvr_win_exe_tag(const char *sha);

/* the bridge (somvr_win_bridge.c): the published locates and the bridge file for the Linux layer */
int somvr_win_bridge_write(const char *exe_sha, char *why, size_t cap);
void somvr_win_bridge_log(void (*logf)(const char *fmt, ...));

/* a module's IAT entry of dll!fn (by name; NULL if not imported) and a pointer written through VirtualProtect
 * (somvr_win_d3d.c) */
void **somvr_win_iat_slot(uintptr_t base, const char *dll, const char *fn);
int somvr_win_put_ptr(void **at, void *v);

/* the DLL's own log before / without somvr_start's (somvr.log format: "WIN ..." lines), and the module */
void somvr_win_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern void *somvr_win_module;

#endif
