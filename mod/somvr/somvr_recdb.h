/* somvr_recdb.h - the records DB's by-name lookup for the Windows DLL (docs/re-winport.md 7.3, 7.4, 7.8).
 *
 * On Linux the game keeps two out-of-line lookups the mod calls: FUN_02166790 (Combat/Attack by name, CB_ATK_BYNAME) and
 * FUN_02153910 (AI/Templates by name, PR_TMPL_BYNAME). Both are the same 30 instructions: the name hashed through a
 * 256-byte fold table (h = h * 0x397 + (signed char)fold[c], 32 bits), then a binary search of the category's records
 * by that key. MSVC inlined the lookup into every caller of the Windows build (17,643 `imul 0x397` sites) and dropped
 * the two functions, so the DLL does it itself over the same data: the fold table is byte-identical in both files, the
 * categories have one layout on both (count i32 +0x28, the records at *(+0x38), 0x30 bytes each, the u32 key at +0x28).
 * Header-only and plain C so tests/test_winabi.c checks it on Linux. */
#ifndef SOMVR_RECDB_H
#define SOMVR_RECDB_H
#include <stdint.h>

#define SOMVR_RECDB_COUNT 0x28 /* the category: i32 record count */
#define SOMVR_RECDB_RECS 0x38  /* ... the record array */
#define SOMVR_RECDB_SIZE 0x30  /* a record */
#define SOMVR_RECDB_KEY 0x28   /* ... its u32 name hash */

/* FUN_02166790's loop: `imul edx,edx,0x397; movsx eax,byte [rax+fold]; add edx,eax` over the bytes up to the NUL */
static inline uint32_t somvr_recdb_hash(const signed char *fold, const char *name)
{
	uint32_t h = 0;
	for (const unsigned char *p = (const unsigned char *)name; p && *p; p++)
		h = h * 0x397u + (uint32_t)(int32_t)fold[*p];
	return h;
}

/* its search: lo 0, hi count - 1, mid = (lo + hi) >> 1 (signed), the keys ascending (unsigned compare); the record or
 * 0 (no category, an empty one, no such key) */
static inline uintptr_t somvr_recdb_find(uintptr_t cat, uint32_t h)
{
	if (!cat)
		return 0;
	int32_t lo = 0, hi = *(const int32_t *)(cat + SOMVR_RECDB_COUNT) - 1;
	uintptr_t recs = *(const uintptr_t *)(cat + SOMVR_RECDB_RECS);
	while (lo <= hi) {
		int32_t mid = (lo + hi) >> 1;
		uintptr_t r = recs + (uintptr_t)(intptr_t)mid * SOMVR_RECDB_SIZE;
		uint32_t k = *(const uint32_t *)(r + SOMVR_RECDB_KEY);
		if (k == h)
			return r;
		if (k > h)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return 0;
}

static inline uintptr_t somvr_recdb_byname(uintptr_t cat, const signed char *fold, const char *name)
{
	return name && fold ? somvr_recdb_find(cat, somvr_recdb_hash(fold, name)) : 0;
}
#endif
