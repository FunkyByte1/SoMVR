/* test_winabi.c - the Windows port's ABI helpers checked on Linux (docs/re-winport.md 7.8): the records DB's by-name
 * lookup the DLL does itself (somvr_recdb.h; FUN_02166790 / FUN_02153910 on Linux), and where a WRAPPER hook's
 * arguments are on each ABI (somvr_regs.h: RA_SYSV*, RA_WIN* over the wrapper's frame). Optional: with the game files
 * (argv[1] = the Linux ELF, argv[2] = the Steam-Windows exe) the real fold tables are compared and an attack name is
 * hashed with Linux's own code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../somvr_recdb.h"
#include "../somvr_regs.h"
#include "../somvr_pe.h"
#include "../somvr_schema.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			fails++;                                                                                       \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                    \
			printf(__VA_ARGS__);                                                                           \
			printf("\n");                                                                                  \
		}                                                                                                      \
	} while (0)

/* the reference: Linux FUN_02166790 transcribed instruction by instruction (edx, ecx, edi, r8d as ints) */
static uint32_t ref_hash(const unsigned char *fold, const char *name)
{
	int32_t edx = 0;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++)
		edx = (int32_t)((uint32_t)edx * 0x397u + (uint32_t)(int32_t)(signed char)fold[*p]);
	return (uint32_t)edx;
}
static const unsigned char *ref_find(const unsigned char *cat, uint32_t edx)
{
	int32_t edi = *(const int32_t *)(cat + 0x28) - 1;
	const unsigned char *r9 = *(const unsigned char *const *)(cat + 0x38);
	if (edi < 0)
		return NULL;
	int32_t ecx = edi >> 1, r8 = 0;
	const unsigned char *rax = r9 + (intptr_t)ecx * 0x30;
	uint32_t esi = *(const uint32_t *)(rax + 0x28);
	if (esi == edx)
		return rax;
	for (;;) {
		if (esi > edx) {
			edi = ecx - 1;
			if (edi < r8)
				return NULL;
		} else {
			r8 = ecx + 1;
			if (edi < r8)
				return NULL;
		}
		ecx = (edi + r8) >> 1;
		rax = r9 + (intptr_t)ecx * 0x30;
		esi = *(const uint32_t *)(rax + 0x28);
		if (esi == edx)
			return rax;
	}
}

struct cat {
	unsigned char head[0x40];
	unsigned char recs[64][0x30];
};
static void cat_set(struct cat *c, int n, const uint32_t *keys)
{
	memset(c, 0, sizeof *c);
	*(int32_t *)(c->head + 0x28) = n;
	unsigned char *recs = &c->recs[0][0];
	memcpy(c->head + 0x38, &recs, sizeof recs);
	for (int i = 0; i < n; i++)
		*(uint32_t *)(c->recs[i] + 0x28) = keys[i];
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

static void test_recdb(void)
{
	/* a fold table with negative entries (the sign extension) and a high half */
	signed char fold[256];
	for (int i = 0; i < 256; i++)
		fold[i] = (signed char)((i * 37 + 11) & 0xff);
	const char *names[] = { "", "a", "PC_Sword_01", "Combat_Attack_Heavy", "\xc3\xa9t\xc3\xa9", "zzzzzzzzzzzzzzzzzzzzzzzz" };
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		CHECK(somvr_recdb_hash(fold, names[i]) == ref_hash((const unsigned char *)fold, names[i]), "hash '%s'", names[i]);
	CHECK(somvr_recdb_hash(fold, "") == 0, "empty name -> 0");

	/* every count 0..64, every key found at its index, keys between and outside not found */
	struct cat c;
	uint32_t keys[64];
	srand(7);
	for (int n = 0; n <= 64; n++) {
		for (int i = 0; i < n; i++)
			keys[i] = (uint32_t)rand() * 2654435761u | 1u; /* odd keys: key+1 is never one */
		qsort(keys, (size_t)n, sizeof keys[0], cmp_u32);
		int dup = 0;
		for (int i = 1; i < n; i++)
			dup |= keys[i] == keys[i - 1];
		if (dup)
			continue;
		cat_set(&c, n, keys);
		uintptr_t cp = (uintptr_t)c.head;
		for (int i = 0; i < n; i++) {
			uintptr_t r = somvr_recdb_find(cp, keys[i]);
			CHECK(r == (uintptr_t)c.recs[i], "n=%d key %d found at %#lx", n, i, (unsigned long)r);
			CHECK(r == (uintptr_t)ref_find(c.head, keys[i]), "n=%d key %d as Linux's", n, i);
			CHECK(!somvr_recdb_find(cp, keys[i] + 1), "n=%d key %d+1 not found", n, i);
			CHECK(!ref_find(c.head, keys[i] + 1), "ref n=%d key %d+1", n, i);
		}
		CHECK(!somvr_recdb_find(cp, 0), "n=%d key 0", n);
		CHECK(!somvr_recdb_find(cp, 0xffffffffu), "n=%d key max", n);
	}
	/* the high keys compare unsigned (Linux: ja) */
	uint32_t hk[3] = { 5, 0x7fffffffu, 0x90000001u };
	cat_set(&c, 3, hk);
	CHECK(somvr_recdb_find((uintptr_t)c.head, 0x90000001u) == (uintptr_t)c.recs[2], "unsigned compare");
	CHECK(!somvr_recdb_find(0, 5), "no category");
	CHECK(!somvr_recdb_byname((uintptr_t)c.head, NULL, "x") && !somvr_recdb_byname((uintptr_t)c.head, fold, NULL),
	      "no fold / no name");
	/* by name: a record keyed with the name's hash */
	uint32_t k3[3] = { 1, somvr_recdb_hash(fold, "PC_Sword_01"), 0xfffffff0u };
	qsort(k3, 3, sizeof k3[0], cmp_u32);
	cat_set(&c, 3, k3);
	uintptr_t r = somvr_recdb_byname((uintptr_t)c.head, fold, "PC_Sword_01");
	CHECK(r && *(const uint32_t *)(r + 0x28) == somvr_recdb_hash(fold, "PC_Sword_01"), "by name");
	CHECK(!somvr_recdb_byname((uintptr_t)c.head, fold, "PC_Sword_02"), "another name");
}

/* ---- the schema checker over a stand-in registry (somvr_schema.h) ---- */
/* the stand-in's strings live in one arena with an unreadable "page" after it (rd_mem fails a read running past its end),
 * each 8-byte padded: the checker reads strings in 8-byte steps, as game memory allows (b34: ASan, the literals over-read) */
static struct {
	char str[512];
	char page[64]; /* "unreadable" */
} g_mem;
#define g_str g_mem.str
static size_t g_nstr;
static const char *S(const char *lit)
{
	size_t n = strlen(lit) + 1;
	char *p = g_str + g_nstr;
	memcpy(p, lit, n);
	g_nstr += (n + 7) & ~(size_t)7;
	return p;
}
static int rd_mem(uintptr_t at, void *out, size_t n)
{
	uintptr_t p0 = (uintptr_t)g_mem.page, p1 = p0 + sizeof g_mem.page;
	if (!at || (at < p1 && at + n > p0))
		return 0;
	memcpy(out, (const void *)at, n);
	return 1;
}
struct fake_sch {
	uintptr_t fields;
	uint32_t count, pad;
	const char *sname, *desc, *name;
	unsigned char rest[0x18];
};
static struct fake_sch g_fs[5];
static unsigned char g_ffields[5][4][0x30];
static unsigned char g_code[256];
static size_t code_pair(size_t i, const void *target, uint32_t off)
{
	g_code[i] = 0x48, g_code[i + 1] = 0x8d, g_code[i + 2] = 0x05;
	int32_t d = (int32_t)((intptr_t)target - (intptr_t)(g_code + i + 7));
	memcpy(g_code + i + 3, &d, 4);
	i += 7;
	g_code[i] = 0x48, g_code[i + 1] = 0x89;
	if (off == 0)
		return g_code[i + 2] = 0x01, i + 3;
	if (off < 0x80)
		return g_code[i + 2] = 0x41, g_code[i + 3] = (unsigned char)off, i + 4;
	g_code[i + 2] = 0x81;
	memcpy(g_code + i + 3, &off, 4);
	return i + 7;
}
static void set_field(int s, int k, const char *name)
{
	name = S(name);
	memcpy(g_ffields[s][k], &name, sizeof name);
}
static void test_schema(void)
{
	static const char *const names[5] = { "Combat.Tree", "Combat.Node", "Inventory.Item", "Dup.Name", "Dup.Name" };
	static const uint32_t counts[5] = { 3, 2, 4, 1, 1 };
	for (int k = 0; k < 5; k++)
		g_fs[k] = (struct fake_sch){ 0, counts[k], 0, "short", "desc", S(names[k]), { 0 } };
	_Static_assert(offsetof(struct fake_sch, name) == SOMVR_SCH_NAME && offsetof(struct fake_sch, count) == SOMVR_SCH_COUNT,
		       "the stand-in's layout");
	/* the registry: slots 0, 1, 0x30 (disp8), 0x100 (disp32), 2 (the duplicate); slot 3.. unfilled */
	size_t i = 0;
	i = code_pair(i, &g_fs[0], 0), i = code_pair(i, &g_fs[1], 8), i = code_pair(i, &g_fs[2], 0x30);
	i = code_pair(i, &g_fs[3], 0x100), i = code_pair(i, &g_fs[4], 0x10);
	g_code[i++] = 0xc3;
	uintptr_t reg[64];
	int n = somvr_sch_reg_decode(g_code, i, (uintptr_t)g_code, reg, 64);
	CHECK(n == 0x100 / 8 + 1 && reg[0] == (uintptr_t)&g_fs[0] && reg[1] == (uintptr_t)&g_fs[1] && reg[6] == (uintptr_t)&g_fs[2] &&
		      reg[32] == (uintptr_t)&g_fs[3] && reg[2] == (uintptr_t)&g_fs[4] && reg[3] == 0,
	      "the registry decoded (%d slots)", n);
	CHECK(somvr_sch_reg_decode(g_code, i, (uintptr_t)g_code, reg, 16) == -1, "a slot past max");
	CHECK(somvr_sch_reg_decode(g_code, i - 1, (uintptr_t)g_code, reg, 64) == -1, "no ret");
	unsigned char bad[16] = { 0x48, 0x8d, 0x05, 0, 0, 0, 0, 0x48, 0x89, 0x41, 0x04, 0xc3 };
	CHECK(somvr_sch_reg_decode(bad, 12, 0, reg, 64) == -1, "a store not 8-aligned");
	unsigned char tail[16] = { 0x48, 0x8d, 0x05, 0, 0, 0, 0, 0x48, 0x89, 0x01, 0x48, 0x8b, 0xc1, 0xc3 };
	CHECK(somvr_sch_reg_decode(tail, 14, 0x1000, reg, 64) == 1 && reg[0] == 0x1007, "`mov rax,rcx; ret` ends it");
	unsigned char other[4] = { 0x90, 0xc3 };
	CHECK(somvr_sch_reg_decode(other, 2, 0, reg, 64) == -1, "another instruction");
	n = somvr_sch_reg_decode(g_code, i, (uintptr_t)g_code, reg, 64);

	struct somvr_sch_spec tree = { "Combat.Tree", 3, 2, { { 0, "Weapons" }, { 1, "Nodes" } } };
	struct somvr_sch_spec tree_n = { "Combat.Tree", 4, 0, { { 0, NULL } } };
	struct somvr_sch_spec dup = { "Dup.Name", 1, 0, { { 0, NULL } } }, none = { "Nope", 1, 0, { { 0, NULL } } };
	int m = 0;
	CHECK(somvr_sch_find(rd_mem, reg, n, &tree, &m) == (uintptr_t)&g_fs[0] && m == 1, "found by name and count");
	CHECK(!somvr_sch_find(rd_mem, reg, n, &tree_n, &m) && m == 1, "the name with another count: none");
	CHECK(!somvr_sch_find(rd_mem, reg, n, &dup, &m) && m == 2, "two with the name: none (never a guess)");
	CHECK(!somvr_sch_find(rd_mem, reg, n, &none, &m) && m == 0, "no such name");
	char why[200];
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[0], &tree, why, sizeof why) == SOMVR_SCH_UNLOADED, "no field array yet");
	const char *nm0 = g_fs[0].name;
	g_fs[0].name = NULL;
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[0], &tree, why, sizeof why) == SOMVR_SCH_UNLOADED, "no name yet");
	g_fs[0].name = nm0;
	g_fs[0].fields = (uintptr_t)g_ffields[0];
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[0], &tree, why, sizeof why) == SOMVR_SCH_UNLOADED, "empty names yet");
	set_field(0, 0, "Weapons"), set_field(0, 1, "Nodes"), set_field(0, 2, "RecoilNodes");
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[0], &tree, why, sizeof why) == SOMVR_SCH_OK, "confirmed: %s", why);
	set_field(0, 1, "Weapons2");
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[0], &tree, why, sizeof why) == SOMVR_SCH_DIFFERS && strstr(why, "field 1"),
	      "a field differs: %s", why);
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[1], &tree, why, sizeof why) == SOMVR_SCH_DIFFERS, "another schema");
	struct somvr_sch_spec far = { "Combat.Tree", 3, 1, { { 5, "X" } } };
	set_field(0, 1, "Nodes");
	CHECK(somvr_sch_check(rd_mem, (uintptr_t)&g_fs[0], &far, why, sizeof why) == SOMVR_SCH_DIFFERS, "a fingerprint past the count");
	char longname[100];
	memset(longname, 'a', sizeof longname - 1), longname[sizeof longname - 1] = 0;
	char got[SOMVR_SCH_NAMELEN];
	CHECK(somvr_sch_str(rd_mem, (uintptr_t)longname, got, sizeof got) && strlen(got) == sizeof got - 1, "a long name cut");
	/* b34: a name ending within 7 bytes of an unreadable page reads to its end (the old byte path kept one byte of the
	 * last 8 and ended the string there: 'Combat.Tr'); one running into the unreadable page is no name */
	char *edge = g_str + sizeof g_str - 12;
	CHECK(g_nstr + 12 <= sizeof g_str, "the arena (%zu bytes used)", g_nstr);
	memcpy(edge, "Combat.Tree", 12);
	CHECK(somvr_sch_str(rd_mem, (uintptr_t)edge, got, sizeof got) && !strcmp(got, "Combat.Tree"), "a name at a page's end: '%s'",
	      got);
	memcpy(edge, "Combat.Treee", 12);
	CHECK(!somvr_sch_str(rd_mem, (uintptr_t)edge, got, sizeof got), "a name into an unreadable page: none ('%s')", got);
}

/* a WRAPPER frame: struct regs, then the entry rsp at +0x150 (COFF), pointing at the caller's stack */
static void test_regs(void)
{
	struct {
		struct regs r;
		uint64_t rsp;
	} f;
	uint64_t stack[8] = { 0x1401234567ull /* the return address */, 0, 0, 0, 0 /* the home area */, 0x55, 0x66, 0x77 };
	_Static_assert(sizeof(struct regs) == 0x150, "frame");
	memset(&f, 0, sizeof f);
	f.r.rdi = 1, f.r.rsi = 2, f.r.rdx = 3, f.r.rcx = 4, f.r.r8 = 5, f.r.r9 = 6;
	f.rsp = (uint64_t)(uintptr_t)stack;
	CHECK(RA_SYSV1(&f.r) == 1 && RA_SYSV2(&f.r) == 2 && RA_SYSV3(&f.r) == 3 && RA_SYSV4(&f.r) == 4 &&
		      RA_SYSV5(&f.r) == 5 && RA_SYSV6(&f.r) == 6,
	      "SysV rdi rsi rdx rcx r8 r9");
	CHECK(RA_WIN1(&f.r) == 4 && RA_WIN2(&f.r) == 3 && RA_WIN3(&f.r) == 5 && RA_WIN4(&f.r) == 6, "Win64 rcx rdx r8 r9");
	CHECK(somvr_win_regs_rsp(&f.r) == (uint64_t)(uintptr_t)stack, "the entry rsp at +0x150");
	CHECK(RA_WIN5(&f.r) == 0x55 && RA_WIN6(&f.r) == 0x66 && RA_WIN7(&f.r) == 0x77, "Win64 5-7 past the home area");
	CHECK(RA_STK(&f.r, 5) == RA_WIN5(&f.r) && RA_STK(&f.r, 7) == RA_WIN7(&f.r), "RA_STK(r, k) = RA_WINk");
	float fl = 2.5f;
	memcpy(&stack[6], &fl, sizeof fl);
	CHECK(RF_STK(&f.r, 6) == 2.5f, "a float argument on the stack: the slot's low 4 bytes");
	/* DF_IMPACT's Windows read: the bool kill is argument 5's low byte (MSVC stores a byte: the rest is stale) */
	stack[5] = 0xdeadbe01ull;
	CHECK((int)(RA_WIN5(&f.r) & 0xff) == 1, "a bool argument's low byte");
#ifdef _WIN32 /* the Windows build's RA1..RA7 are the Win64 family (somvr_regs.h; win/wintests.py runs this under wine) */
	CHECK(RA1(&f.r) == 4 && RA2(&f.r) == 3 && RA4(&f.r) == 6 && RA5(&f.r) == 0xdeadbe01ull && RA7(&f.r) == 0x77,
	      "the build's RA = Win64 on Windows");
#else /* Linux's RA1..RA6 are the SysV family */
	CHECK(RA1(&f.r) == 1 && RA2(&f.r) == 2 && RA5(&f.r) == 5, "the build's RA = SysV on Linux");
#endif
}

static unsigned char *readall(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = sz > 0 ? malloc((size_t)sz) : NULL;
	if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz)
		free(b), b = NULL;
	fclose(f);
	*n = b ? (size_t)sz : 0;
	return b;
}
static const unsigned char *elf_at(const unsigned char *b, size_t n, uint64_t va, size_t len)
{
	uint64_t phoff = *(const uint64_t *)(b + 0x20);
	uint16_t phes = *(const uint16_t *)(b + 0x36), phn = *(const uint16_t *)(b + 0x38);
	for (int i = 0; i < phn; i++) {
		const unsigned char *ph = b + phoff + (uint64_t)i * phes;
		uint64_t off = *(const uint64_t *)(ph + 8), vaddr = *(const uint64_t *)(ph + 16), fsz = *(const uint64_t *)(ph + 32);
		if (*(const uint32_t *)ph == 1 && va >= vaddr && va + len <= vaddr + fsz && off + (va - vaddr) + len <= n)
			return b + off + (va - vaddr);
	}
	return NULL;
}
static const unsigned char *pe_at(const unsigned char *b, size_t n, uint32_t rva, size_t len)
{
	uint32_t pe = *(const uint32_t *)(b + 0x3c);
	uint16_t ns = *(const uint16_t *)(b + pe + 6), opt = *(const uint16_t *)(b + pe + 20);
	const unsigned char *s = b + pe + 24 + opt;
	for (int i = 0; i < ns; i++, s += 40) {
		uint32_t va = *(const uint32_t *)(s + 12), rs = *(const uint32_t *)(s + 16), rp = *(const uint32_t *)(s + 20);
		if (rva >= va && rva + len <= va + rs && rp + (rva - va) + len <= n)
			return b + rp + (rva - va);
	}
	return NULL;
}

static const unsigned char *g_exe;
static size_t g_nexe;
static int rd_exe(uintptr_t at, void *out, size_t n) /* a VA of the exe at its preferred base, from the file */
{
	if (at < 0x140000000ull || at - 0x140000000ull > 0x7fffffff)
		return 0;
	const unsigned char *p = pe_at(g_exe, g_nexe, (uint32_t)(at - 0x140000000ull), n);
	return p ? (memcpy(out, p, n), 1) : 0;
}

/* with the game files: the two fold tables (Linux 0x3898a10, Windows RVA 0x13bad70) byte for byte, and the lookup's
 * hash loop on Linux's table against the code's own constants (0x397 in FUN_02166790's imul) */
static void test_files(const char *elf, const char *exe)
{
	size_t nl, nw;
	unsigned char *l = readall(elf, &nl), *w = readall(exe, &nw);
	if (!l || !w) {
		printf("test_winabi: game files not readable (%s, %s): skipped\n", elf, exe);
		free(l), free(w);
		return;
	}
	const unsigned char *fl = elf_at(l, nl, 0x3898a10, 256), *fw = pe_at(w, nw, 0x13bad70, 256);
	const unsigned char *code = elf_at(l, nl, 0x21667a0, 16);
	CHECK(fl && fw && !memcmp(fl, fw, 256), "the fold tables are identical");
	static const unsigned char imul[] = { 0x69, 0xd2, 0x97, 0x03, 0x00, 0x00 }; /* imul edx, edx, 0x397 */
	CHECK(code && !memcmp(code, imul, sizeof imul), "FUN_02166790's loop starts with imul edx,edx,0x397");
	if (fl)
		CHECK(somvr_recdb_hash((const signed char *)fl, "PC_Sword_01") ==
			      ref_hash(fl, "PC_Sword_01"),
		      "the real table");
	/* somvr_pe.h on the exe's headers (mapped at the image base as in the file): the sections the DLL's game_range uses,
	 * and RVAs the tables rely on inside them */
	uint32_t trva = 0, tsz = 0, rrva = 0, rsz = 0, x = 0, y = 0;
	CHECK(somvr_pe_section(w, ".text", &trva, &tsz) && somvr_pe_section(w, ".rdata", &rrva, &rsz), ".text and .rdata");
	CHECK(trva <= 0xe90860 && 0xe90860 < trva + tsz && trva <= 0xcdb3d0 && 0xcdb3d0 < trva + tsz,
	      "BR_LOGPRINTF / BOW_TOKENS_SET in .text");
	CHECK(rrva <= 0x1485598 && 0x1485598 < rrva + rsz && rrva <= 0x13bfe48 && 0x13bfe48 < rrva + rsz,
	      "CEngineTimer's and CBaseTarget's vtables in .rdata");
	CHECK(!somvr_pe_section(w, ".tex", &x, &y) && !somvr_pe_section(w, ".textbss", &x, &y) && !somvr_pe_section(l, ".text", &x, &y),
	      "no prefix match, no section on an ELF");
	/* the exe's own registry FUN_140aa4a20 decoded, every holder the mod reads found by its name and count, at the
	 * RVAs in offsets-hooks.tsv */
	g_exe = w, g_nexe = nw;
	const unsigned char *rc = pe_at(w, nw, 0xaa4a20, 0x2600 * 2);
	static uintptr_t reg[1400];
	int nreg = rc ? somvr_sch_reg_decode(rc, 0x2600 * 2, 0x140aa4a20, reg, 1400) : -1;
	int filled = 0;
	for (int i = 0; i < nreg; i++)
		filled += reg[i] != 0;
	CHECK(nreg == 1208 && filled == 1208, "the exe's registry: %d slots, %d filled", nreg, filled);
	static const struct { const char *name; uint32_t count; uint32_t rva; } want[] = {
		{ "Combat.Tree", 106, 0x1846d80 }, { "Combat.Node", 11, 0x1846d40 }, { "Combat.Attack", 7, 0x1846c60 },
		{ "CombatAttackAnimationData", 34, 0x18453f0 }, { "Combat.AIBlockTable", 1, 0x1845580 },
		{ "CombatAIBlockChance", 2, 0x1845510 }, { "CombatAttackData", 104, 0x1845430 },
		{ "Combat.CategoryList", 1, 0x1843f30 }, { "Inventory.Item", 27, 0x1804f70 },
		{ "CombatNodePredicate", 3, 0x18451f0 }, { "Combat.Chord", 3, 0x183f8d0 }, { "Combat.Input", 10, 0x183f600 },
	};
	int found = 0;
	for (size_t i = 0; nreg > 0 && i < sizeof want / sizeof want[0]; i++) {
		struct somvr_sch_spec sp = { want[i].name, want[i].count, 0, { { 0, NULL } } };
		int m = 0;
		uintptr_t o = somvr_sch_find(rd_exe, reg, nreg, &sp, &m);
		found += o == 0x140000000ull + want[i].rva;
		CHECK(o == 0x140000000ull + want[i].rva && m == 1, "%s: %#lx (%d named so), the table %#x", want[i].name,
		      (unsigned long)o, m, want[i].rva);
	}
	printf("test_winabi: the exe's schema registry: %d slots, %d of 12 holders found by name at the table's RVAs\n", nreg, found);
	printf("test_winabi: the game files: fold tables %s; .text %#x+%#x, .rdata %#x+%#x\n",
	       fl && fw && !memcmp(fl, fw, 256) ? "identical" : "DIFFER", trva, tsz, rrva, rsz);
	free(l), free(w);
}

int main(int argc, char **argv)
{
	test_recdb();
	test_regs();
	test_schema();
	if (argc > 2)
		test_files(argv[1], argv[2]);
	printf("test_winabi: %s\n", fails ? "FAILED" : "ok");
	return fails != 0;
}
