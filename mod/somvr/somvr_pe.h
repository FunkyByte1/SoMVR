/* somvr_pe.h - a PE image's section by name (the Windows DLL: the game exe's .text / .rdata at its load address;
 * docs/re-winport.md 7.8). The headers are mapped at the image base exactly as in the file, so the same code reads a
 * file's bytes (tests/test_winabi.c checks it on the Steam-Windows exe). Header-only, plain C. */
#ifndef SOMVR_PE_H
#define SOMVR_PE_H
#include <stdint.h>
#include <string.h>

/* name: up to 8 characters (".text", ".rdata"); 1 = found: *rva its VirtualAddress, *size its VirtualSize */
static inline int somvr_pe_section(const void *img, const char *name, uint32_t *rva, uint32_t *size)
{
	const unsigned char *b = (const unsigned char *)img;
	if (!b || b[0] != 'M' || b[1] != 'Z')
		return 0;
	uint32_t pe;
	memcpy(&pe, b + 0x3c, 4);
	if (pe > 0x1000 || memcmp(b + pe, "PE\0\0", 4))
		return 0;
	uint16_t ns, opt;
	memcpy(&ns, b + pe + 6, 2), memcpy(&opt, b + pe + 20, 2);
	size_t nl = strlen(name);
	if (nl > 8)
		return 0;
	for (const unsigned char *s = b + pe + 24 + opt; ns--; s += 40)
		if (!memcmp(s, name, nl) && (nl == 8 || !s[nl])) {
			memcpy(size, s + 8, 4), memcpy(rva, s + 12, 4);
			return 1;
		}
	return 0;
}
#endif
