/* somvr_schema.h - the records DB's schema objects found and checked by name (docs/re-winport.md 7.8, 7.9).
 *
 * A schema object (both builds; the Windows ones constant-initialised in .data, the Linux ones by code):
 *   +0x00 the field array (0x30 bytes per field: +0 its name, +8 its hash; filled when the database loads)
 *   +0x08 u32 the field count, +0x10 the short name, +0x18 the description, +0x20 the full name ("Combat.Tree")
 * The mod's "schema holders" are these objects' addresses (rx_sch_hash reads *(holder) + 0x30 i + 8). Windows lists
 * every schema in one registry function, FUN_140aa4a20: 1208 x `lea rax,[rip+schema]; mov [rcx+8k],rax`, then ret.
 * Found by its full name and field count (identity, at once), confirmed by field names once the database is loaded.
 * Header-only, plain C, every game read through the caller's read function: tests/test_winabi.c runs it on Linux
 * against a stand-in registry (and on the exe's real registry function when the game files are given). */
#ifndef SOMVR_SCHEMA_H
#define SOMVR_SCHEMA_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SOMVR_SCH_FIELDS 0x00
#define SOMVR_SCH_COUNT 0x08
#define SOMVR_SCH_NAME 0x20
#define SOMVR_SCH_FSIZE 0x30
#define SOMVR_SCH_NAMELEN 64

typedef int (*somvr_sch_read_fn)(uintptr_t at, void *out, size_t n); /* 1 = all n bytes read */

struct somvr_sch_spec {
	const char *name;   /* the full name (+0x20) */
	uint32_t count;     /* the field count (+8), as on Linux */
	int nf;             /* fingerprint fields (the mod's own indexes) */
	struct { int ix; const char *name; } f[4];
};

/* FUN_140aa4a20's body from its first byte: `48 8d 05 d32` (lea rax,[rip+d]) then `48 89 01|41 d8|81 d32` (mov
 * [rcx(+d)],rax), pair after pair, up to a ret (c3, or `mov rax,rcx; ret`: 48 8b c1 c3). out[k] = the schema stored
 * at registry offset 8k (0 = a slot the function did not fill). Returns the highest slot + 1, or -1 if the code is not that
 * shape (an unknown instruction, a store not 8-aligned, past max, no ret within n) */
static inline int somvr_sch_reg_decode(const unsigned char *code, size_t n, uintptr_t va, uintptr_t *out, int max)
{
	int top = 0;
	for (int k = 0; k < max; k++)
		out[k] = 0;
	for (size_t i = 0; i < n;) {
		if (code[i] == 0xc3 || (i + 4 <= n && code[i] == 0x48 && code[i + 1] == 0x8b && code[i + 2] == 0xc1 && code[i + 3] == 0xc3))
			return top; /* ret, or `mov rax,rcx; ret` (the registry returns itself) */
		if (i + 10 > n || code[i] != 0x48 || code[i + 1] != 0x8d || code[i + 2] != 0x05)
			return -1;
		int32_t d;
		memcpy(&d, code + i + 3, 4);
		uintptr_t x = va + i + 7 + (uintptr_t)(intptr_t)d;
		i += 7;
		if (code[i] != 0x48 || code[i + 1] != 0x89)
			return -1;
		uint32_t off;
		if (code[i + 2] == 0x01)
			off = 0, i += 3;
		else if (code[i + 2] == 0x41 && !(code[i + 3] & 0x80))
			off = code[i + 3], i += 4; /* (disp8 is signed: the slots up to 0x78 here) */
		else if (code[i + 2] == 0x81 && i + 7 <= n)
			memcpy(&off, code + i + 3, 4), i += 7;
		else
			return -1;
		if (off & 7 || off / 8 >= (uint32_t)max)
			return -1;
		out[off / 8] = x;
		if ((int)(off / 8) + 1 > top)
			top = (int)(off / 8) + 1;
	}
	return -1;
}

/* a C string at `at` (up to cap - 1 bytes, read in 8-byte steps; where 8 bytes do not read - the string's end is
 * within 7 bytes of an unreadable page - one byte at a time from there, so the string is read to its end or the page's;
 * b34: the old byte-path took one byte and ended the string there) */
static inline int somvr_sch_str(somvr_sch_read_fn rd, uintptr_t at, char *s, size_t cap)
{
	size_t k = 0;
	s[0] = 0;
	if (!at)
		return 0;
	while (k + 1 < cap) {
		char b[8];
		int nb = 8;
		if (!rd(at + k, b, 8)) {
			if (!rd(at + k, b, 1)) {
				s[k] = 0; /* the page ended before the string did: not a string */
				return 0;
			}
			nb = 1;
		}
		for (int j = 0; j < nb && k + 1 < cap; j++, k++) {
			s[k] = b[j];
			if (!b[j])
				return 1;
		}
	}
	s[cap - 1] = 0;
	return 1;
}

/* the schema object's full name and count; 1 = both read */
static inline int somvr_sch_ident(somvr_sch_read_fn rd, uintptr_t obj, char *name, size_t cap, uint32_t *count)
{
	uintptr_t np = 0;
	return obj && rd(obj + SOMVR_SCH_NAME, &np, sizeof np) && rd(obj + SOMVR_SCH_COUNT, count, sizeof *count) &&
	       somvr_sch_str(rd, np, name, cap);
}

/* the one object among objs[0..n) with s's full name and count; *nmatch = how many had the name (0: none; > 1, or
 * the name with another count: 0 is returned - never a guess) */
static inline uintptr_t somvr_sch_find(somvr_sch_read_fn rd, const uintptr_t *objs, int n, const struct somvr_sch_spec *s,
				       int *nmatch)
{
	uintptr_t hit = 0;
	int m = 0, bad = 0;
	char nm[SOMVR_SCH_NAMELEN];
	for (int i = 0; i < n; i++) {
		uint32_t c = 0;
		if (!objs[i] || !somvr_sch_ident(rd, objs[i], nm, sizeof nm, &c) || strcmp(nm, s->name))
			continue;
		m++;
		if (c == s->count && objs[i] != hit)
			hit = hit ? (bad = 1, hit) : objs[i];
		else if (c != s->count)
			bad = 1;
	}
	if (nmatch)
		*nmatch = m;
	return bad ? 0 : hit;
}

enum { SOMVR_SCH_UNLOADED = -1, SOMVR_SCH_DIFFERS = 0, SOMVR_SCH_OK = 1 };
/* obj's identity (name, count) and, once the database has filled its field array, the fingerprint fields' names.
 * SOMVR_SCH_UNLOADED: no name pointer yet, no array yet or an empty name at a fingerprint index (try again later);
 * SOMVR_SCH_DIFFERS: a readable name or count that is not s's, or a fingerprint field named otherwise (why: what) */
static inline int somvr_sch_check(somvr_sch_read_fn rd, uintptr_t obj, const struct somvr_sch_spec *s, char *why, size_t cap)
{
	char nm[SOMVR_SCH_NAMELEN] = ""; /* (b34: printed below even when the count read failed before the name's) */
	uint32_t c = 0;
	uintptr_t arr = 0;
	if (why && cap)
		why[0] = 0;
	uintptr_t np = 0;
	if (!obj || !rd(obj + SOMVR_SCH_NAME, &np, sizeof np) || !np)
		return SOMVR_SCH_UNLOADED; /* (no name yet: Linux before the game's static initialisers ran) */
	if (!somvr_sch_ident(rd, obj, nm, sizeof nm, &c) || strcmp(nm, s->name) || c != s->count) {
		if (why)
			snprintf(why, cap, "the object is '%s' with %u fields, not '%s' with %u", nm, c, s->name, s->count);
		return SOMVR_SCH_DIFFERS;
	}
	if (!rd(obj + SOMVR_SCH_FIELDS, &arr, sizeof arr) || !arr)
		return SOMVR_SCH_UNLOADED;
	for (int i = 0; i < s->nf; i++) {
		uintptr_t fp = 0;
		if (s->f[i].ix < 0 || (uint32_t)s->f[i].ix >= s->count) {
			if (why)
				snprintf(why, cap, "fingerprint field %d outside the %u fields", s->f[i].ix, s->count);
			return SOMVR_SCH_DIFFERS;
		}
		if (!rd(arr + (uintptr_t)s->f[i].ix * SOMVR_SCH_FSIZE, &fp, sizeof fp) || !fp ||
		    !somvr_sch_str(rd, fp, nm, sizeof nm) || !nm[0])
			return SOMVR_SCH_UNLOADED;
		if (strcmp(nm, s->f[i].name)) {
			if (why)
				snprintf(why, cap, "field %d is '%s', not '%s'", s->f[i].ix, nm, s->f[i].name);
			return SOMVR_SCH_DIFFERS;
		}
	}
	return SOMVR_SCH_OK;
}
#endif
