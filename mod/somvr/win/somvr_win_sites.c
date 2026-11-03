/* somvr_win_sites.c - the SITE tables on Windows: every SITE_TABLE entry (the section .somvrst) rewritten by name
 * from the generated rows (win/README.md 2, 3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "somvr_win.h"
#include "somvr_winofs.h"
#include "somvr_wintables.inc"

/* the section's bounds: $a and $z sort around the tables' $m (the linker orders grouped sections by suffix). Each
 * table is its own 16-aligned piece (clang aligns arrays; -fdata-sections), so 8 zero bytes may sit between two
 * tables (an entry is 56 bytes): the walk steps over zero qwords 8 bytes at a time (no entry has a NULL name). */
__attribute__((section(".somvrst$a"), used)) static uint64_t st_begin = 0;
__attribute__((section(".somvrst$z"), used)) static uint64_t st_end = 0;

static struct site_spec *walk_next(uintptr_t *p)
{
	uintptr_t e = (uintptr_t)&st_end;
	__asm__("" : "+r"(e)); /* not the address of an object the compiler may reason about */
	while (*p + sizeof(struct site_spec) <= e) {
		if (!*(const uint64_t *)*p) {
			*p += 8;
			continue;
		}
		struct site_spec *s = (struct site_spec *)*p;
		*p += sizeof *s;
		return s;
	}
	return NULL;
}
#define WALK(s) for (uintptr_t wp_ = (uintptr_t)&st_begin + 8; ((s) = walk_next(&wp_));)

/* The SITEs whose hook is a WRAPPER (somvr.c's register-passing asm wrapper): their C side reads the Linux (SysV)
 * argument registers, so on Windows they stay blank even with a live row - patched they would crash the game. A porter
 * who makes a hook's C side read the Win64 registers (rcx, rdx, r8, r9, the stack from +0x28) removes its name here
 * (win/README.md 2). */
static const char *const g_sysv[] = {
	"", /* none left (an empty name matches no SITE): H9 (base); H1-H8, AUDIO, PM, RUM1/2 and the EX sites
	     * (winport-stereo, docs/re-winport.md 7.7); LP_GUARD_HOVER/CLICK, BOW_FA_CAMXF, LOCO_ROOTDELTA, SL_SHEATH_JMP,
	     * DF_IMPACT, HP_DRAW_* (5), HP_CUST_* (8), SCR_NEM_PAD (winport-hooks, 7.8). A new WRAPPER hook goes here
	     * until its C side reads RA1..RA7 (somvr_regs.h) */
};
int somvr_win_sysv_blanked;

static int sysv_guarded(const char *name)
{
	for (size_t i = 0; i < sizeof g_sysv / sizeof g_sysv[0]; i++)
		if (!strcmp(g_sysv[i], name))
			return 1;
	return 0;
}

static int cmp_row(const void *k, const void *r)
{
	return strcmp((const char *)k, ((const struct somvr_win_site *)r)->name);
}

static const struct somvr_win_exe *g_exe; /* somvr_win_exe_tag's pick */
static const uint32_t g_gaddr_none[SOMVR_WIN_NGADDR + 1];
const uint32_t *somvr_win_gaddr = g_gaddr_none;

static const struct somvr_win_site *row_of(const char *name)
{
	return name && g_exe ? bsearch(name, g_exe->sites, (size_t)g_exe->nsites, sizeof g_exe->sites[0], cmp_row) : NULL;
}

static int is_com(const char *name)
{
	for (int i = 0; i < SOMVR_WIN_NCOM; i++)
		if (!strcmp(g_win_com[i].name, name))
			return 1;
	return 0;
}

/* hex with the relocated qwords moved by delta (an image not at its preferred base) */
static const char *reloc_hex(const char *hex, uint64_t mask, int64_t delta)
{
	if (!mask || !delta)
		return hex;
	unsigned char b[64];
	int n = 0;
	for (const char *p = hex; *p && n < 64;) {
		while (*p == ' ')
			p++;
		if (!p[0] || !p[1])
			break;
		char t[3] = { p[0], p[1], 0 };
		b[n++] = (unsigned char)strtoul(t, NULL, 16);
		p += 2;
	}
	for (int i = 0; i + 8 <= n; i++)
		if (mask >> i & 1) {
			uint64_t v;
			memcpy(&v, b + i, 8);
			v += (uint64_t)delta;
			memcpy(b + i, &v, 8);
		}
	char *out = malloc((size_t)n * 3 + 1);
	if (!out)
		return hex;
	for (int i = 0; i < n; i++)
		snprintf(out + 3 * i, 4, i + 1 < n ? "%02x " : "%02x", b[i]);
	return out;
}

int somvr_win_sites_apply(uintptr_t base, int *blank, int *com, int all_blank)
{
	int live = 0, nb = 0, nc = 0;
	int64_t delta = (int64_t)(base - SOMVR_WIN_PREFERRED_BASE);
	struct site_spec *s;
	WALK(s) {
		const struct somvr_win_site *r = all_blank ? NULL : row_of(s->name);
		if (r && sysv_guarded(s->name))
			r = NULL, somvr_win_sysv_blanked++;
		if (r && r->kind == s->kind) {
			s->addr = base + r->rva, s->ctx = base + r->ctx_rva, s->target = base + r->target_rva;
			s->ctx_hex = reloc_hex(r->ctx, r->ctx_reloc, delta);
			s->prologue_hex = reloc_hex(r->prologue, r->pro_reloc, delta);
			live++;
			continue;
		}
		s->addr = s->ctx = s->target = 0;
		s->ctx_hex = s->prologue_hex = ""; /* check_site: "no Windows site" */
		if (is_com(s->name))
			nc++;
		else
			nb++;
	}
	if (blank)
		*blank = nb;
	if (com)
		*com = nc;
	return live;
}

struct site_spec *somvr_win_site_entry(const char *name)
{
	struct site_spec *s;
	WALK(s)
		if (!strcmp(s->name, name))
			return s;
	return NULL;
}

const struct somvr_win_com *somvr_win_com_rows(int *n)
{
	*n = SOMVR_WIN_NCOM;
	return g_win_com;
}

int somvr_win_com_fill(const char *name, void **vtbl)
{
	int slot = -1;
	for (int i = 0; i < SOMVR_WIN_NCOM; i++)
		if (!strcmp(g_win_com[i].name, name))
			slot = g_win_com[i].slot;
	struct site_spec *s = somvr_win_site_entry(name);
	const char *ws = getenv("SOMVR_WIN_SITES"); /* =0: the inert boot, no COM site either */
	if (slot < 0 || !s || !vtbl || s->kind != K_VSLOT || sysv_guarded(name) || (ws && !strcmp(ws, "0")))
		return -1;
	static char hex[256][24]; /* one context string per filled slot */
	static int nhex;
	if (nhex >= 256)
		return -1;
	uintptr_t v = (uintptr_t)vtbl[slot];
	unsigned char b[8];
	memcpy(b, &v, 8);
	char *h = hex[nhex++];
	snprintf(h, 24, "%02x %02x %02x %02x %02x %02x %02x %02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
	s->addr = s->ctx = (uintptr_t)&vtbl[slot];
	s->ctx_hex = h;
	s->target = v;
	/* the target's first byte as its prologue: the COM method is DXVK's (or the driver's), not the game's */
	static char pro[256][4];
	snprintf(pro[nhex - 1], 4, "%02x", *(const unsigned char *)v);
	s->prologue_hex = pro[nhex - 1];
	return 0;
}

/* a K_ENTRY SITE the DLL provides itself (a game function MSVC inlined away: somvr.c win_provide_all): the entry
 * points at fn, its first 16 bytes are the context and the prologue, so check_site passes and the feature calls fn as
 * it calls the game's on Linux. Not with SOMVR_WIN_SITES=0 (the inert boot). 0 = done */
int somvr_win_provide(const char *name, void *fn);
int somvr_win_provide(const char *name, void *fn)
{
	struct site_spec *s = somvr_win_site_entry(name);
	const char *ws = getenv("SOMVR_WIN_SITES");
	static char hex[16][3 * 16];
	static int nhex;
	if (!s || !fn || s->kind != K_ENTRY || (ws && !strcmp(ws, "0")) || nhex >= 16)
		return -1;
	const unsigned char *b = (const unsigned char *)fn;
	char *h = hex[nhex++];
	for (int i = 0; i < 16; i++)
		snprintf(h + 3 * i, 4, i < 15 ? "%02x " : "%02x", b[i]);
	s->addr = s->ctx = s->target = (uintptr_t)fn;
	s->ctx_hex = s->prologue_hex = h;
	return 0;
}

const char *somvr_win_exe_tag(const char *sha)
{
	for (int i = 0; sha && i < SOMVR_WIN_NEXES; i++)
		if (!strcmp(sha, g_win_exes[i].sha256)) {
			g_exe = &g_win_exes[i];
			somvr_win_gaddr = g_exe->gaddr;
			return g_exe->tag;
		}
	return NULL;
}
