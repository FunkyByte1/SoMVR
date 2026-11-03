/* somvr_fogdxbc.h - the fog by the distance along the eye's ray, as a DXBC edit (docs/re-winport.md 7.5; the SPIR-V
 * edit it replaces: vklayer/somvr_fogpatch.h, docs/re-xr-pipeline.md 137.3). Header-only, no dependencies: the Windows
 * DLL's ID3D11Device::CreatePixelShader hook (COM slot 15) and tests/test_fogdxbc.c (native, Linux) include it.
 *
 * fog_overlay.mat's pixel shaders (the opaque pass and its OIT twin) compute the view position rN = (z, x*z, y*z) from
 * the linear depth z (1e7 = the sky) and take the fog's path length as
 *     eq   rC.c, rN.x, l(1e7)                 the sky test
 *     movc rD.d, rC.c, rE.e, rN.x             sky ? the atmosphere's edge : the depth along the view's axis
 * (global.arch05: the opaque one, 8168 bytes, checksum d7d9be1a..., instructions 36/38, rN = r3; the OIT one, 9048
 * bytes, 14fcc056..., instructions 63/65, rN = r6, a 2-component movc between them). The edit gives the
 * movc the distance from the eye instead (the same fog wherever the head points):
 *     dp3  rT.x, rN.xyzx, rN.xyzx
 *     sqrt rT.x, rT.x
 *     movc rD.d, rC.c, rE.e, rT.x             rT = a new temp (dcl_temps + 1)
 * Matched by structure, every link checked before anything is written: the shader holds l(6.371e8) (the planet's
 * radius in cm); the eq compares a temp's .x with l(1e7) into one component; rN.yz were last written by
 * `mul rN.yz, ?, rN.xxxx`; the first later movc with that condition component and src2 = rN.x has a one-component
 * destination; nothing in between writes rN.x or the condition. Then the container is rebuilt: SHEX/SHDR length +12
 * dwords, its chunk +48 bytes, the later chunks' offsets and the total size +48, the checksum recomputed (the DXBC
 * variant of MD5, checked against all 927 blobs in the exe). STAT is left as it is (the runtimes do not read it). */
#ifndef SOMVR_FOGDXBC_H
#define SOMVR_FOGDXBC_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fdx_rd(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void fdx_wr(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24); }

static void fdx_md5_block(uint32_t st[4], const unsigned char *blk)
{
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8,
		0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
		0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87,
		0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
		0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
		0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
		0xeb86d391 };
	static const unsigned char S[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20,
					     5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
					     6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
	uint32_t M[16], a = st[0], b = st[1], c = st[2], d = st[3];
	for (int i = 0; i < 16; i++)
		M[i] = fdx_rd(blk + 4 * i);
	for (int i = 0; i < 64; i++) {
		uint32_t f, g;
		if (i < 16) f = (b & c) | (~b & d), g = (uint32_t)i;
		else if (i < 32) f = (d & b) | (~d & c), g = (uint32_t)(5 * i + 1) % 16;
		else if (i < 48) f = b ^ c ^ d, g = (uint32_t)(3 * i + 5) % 16;
		else f = c ^ (b | ~d), g = (uint32_t)(7 * i) % 16;
		f += a + K[i] + M[g];
		a = d, d = c, c = b;
		b += (f << S[i]) | (f >> (32 - S[i]));
	}
	st[0] += a, st[1] += b, st[2] += c, st[3] += d;
}

/* the DXBC container's checksum (bytes 4..19) of a blob of n bytes: MD5 of bytes 20.. with DXBC's own last block */
static void somvr_dxbc_checksum(const unsigned char *blob, size_t n, unsigned char out[16])
{
	const unsigned char *p = blob + 20;
	size_t len = n - 20, full = len - len % 64, rest = len % 64;
	uint32_t st[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 }, bits = (uint32_t)(len * 8);
	unsigned char blk[64];
	for (size_t i = 0; i < full; i += 64)
		fdx_md5_block(st, p + i);
	memset(blk, 0, sizeof blk);
	if (rest >= 56) {
		memcpy(blk, p + full, rest);
		blk[rest] = 0x80;
		fdx_md5_block(st, blk);
		memset(blk, 0, sizeof blk);
		fdx_wr(blk, bits);
		fdx_wr(blk + 60, (bits >> 2) | 1);
	} else {
		fdx_wr(blk, bits);
		memcpy(blk + 4, p + full, rest);
		blk[4 + rest] = 0x80;
		fdx_wr(blk + 60, (bits >> 2) | 1);
	}
	fdx_md5_block(st, blk);
	for (int i = 0; i < 4; i++)
		fdx_wr(out + 4 * i, st[i]);
}

/* SM4/5 operand tokens used here */
#define FDX_OP_TYPE(t) (((t) >> 12) & 0xff)  /* 0 temp, 4 immediate32 */
#define FDX_OP_SEL1(t) ((((t) & 3) == 2) && (((t) >> 2) & 3) == 2) /* a 4-component operand selecting one component */
#define FDX_OP_COMP(t) (((t) >> 4) & 3)
#define FDX_OP_MASK(t) (((t) >> 4) & 0xf)   /* a dest's write mask (selection mode 0) */
#define FDX_OP_PLAIN_1D(t) (((t) & 0x80000000u) == 0 && (((t) >> 20) & 3) == 1 && (((t) >> 22) & 7) == 0)

/* the instruction at word i: its opcode and length (0 = broken) */
static uint32_t fdx_len(const uint32_t *w, uint32_t i, uint32_t nw)
{
	uint32_t op = w[i] & 0x7ff, ln = (w[i] >> 24) & 0x7f;
	if (op == 53 && i + 1 < nw) /* customdata: the length in the next word */
		ln = w[i + 1];
	return ln && i + ln <= nw ? ln : 0;
}

/* a destination operand at word k (no extended opcode tokens before it): temp index, mask -> 1 if one plain temp */
static int fdx_dst_temp(const uint32_t *w, uint32_t k, uint32_t *reg, uint32_t *mask)
{
	if (FDX_OP_TYPE(w[k]) != 0 || !FDX_OP_PLAIN_1D(w[k]) || (w[k] & 3) != 2 || ((w[k] >> 2) & 3) != 0)
		return 0;
	*reg = w[k + 1], *mask = FDX_OP_MASK(w[k]);
	return 1;
}

/* the edit; returns the new blob (malloc'ed, *n_out bytes) or NULL with *why set (nothing changed) */
static unsigned char *somvr_fogdxbc(const unsigned char *blob, size_t n, size_t *n_out, const char **why)
{
	*n_out = 0;
	*why = "not DXBC";
	if (n < 32 || memcmp(blob, "DXBC", 4) || fdx_rd(blob + 20) != 1 || fdx_rd(blob + 24) != n)
		return NULL;
	uint32_t nch = fdx_rd(blob + 28), shex = 0;
	*why = "a broken chunk table";
	if (!nch || nch > 32 || 32 + 4 * (size_t)nch > n)
		return NULL;
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t o = fdx_rd(blob + 32 + 4 * c);
		if ((size_t)o + 8 > n || (size_t)o + 8 + fdx_rd(blob + o + 4) > n)
			return NULL;
		if (!memcmp(blob + o, "SHEX", 4) || !memcmp(blob + o, "SHDR", 4))
			shex = o;
	}
	*why = "no SHEX/SHDR chunk";
	if (!shex)
		return NULL;
	uint32_t clen = fdx_rd(blob + shex + 4);
	const unsigned char *pb = blob + shex + 8;
	uint32_t nw = clen / 4;
	*why = "not a pixel shader";
	if (nw < 2 || (fdx_rd(pb) >> 16) != 0 || fdx_rd(pb + 4) != nw)
		return NULL;
	uint32_t *w = malloc(nw * sizeof *w);
	*why = "no memory";
	if (!w)
		return NULL;
	for (uint32_t i = 0; i < nw; i++)
		w[i] = fdx_rd(pb + 4 * i);
	/* the planet's radius among the immediates */
	const uint32_t R = 0x4e17e580u /* 6.371e8f */, SKY = 0x4b189680u /* 1e7f */;
	int planet = 0;
	uint32_t temps_at = 0, ntemps = 0, eq_at = 0, mov_at = 0, rN = 0, rC = 0, cC = 0, src2_at = 0, yz_ok = 0;
	*why = "a broken instruction stream";
	for (uint32_t i = 2; i < nw;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff;
		if (!ln)
			goto fail;
		for (uint32_t k = i + 1; k + 1 < i + ln && op != 53; k++)
			if (FDX_OP_TYPE(w[k]) == 4 && (w[k] & 3) == 1 && w[k + 1] == R)
				planet = 1;
		if (op == 104 /* dcl_temps */ && ln == 2)
			temps_at = i, ntemps = w[i + 1];
		i += ln;
	}
	*why = "no l(6.371e8) (not the fog overlay)";
	if (!planet || !temps_at)
		goto fail;
	/* the sky test, then its movc */
	for (uint32_t i = 2; i < nw && !mov_at;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff, reg, mask;
		if (!ln)
			goto fail;
		if (op == 56 /* mul */ && !(w[i] & 0x80000000u) && fdx_dst_temp(w, i + 1, &reg, &mask) && mask == 6 && ln == 7 &&
		    FDX_OP_TYPE(w[i + 5]) == 0 && FDX_OP_PLAIN_1D(w[i + 5]) && w[i + 6] == reg && (w[i + 5] & 3) == 2 &&
		    ((w[i + 5] >> 2) & 3) == 1 && ((w[i + 5] >> 4) & 0xff) == 0)
			yz_ok = reg + 1; /* mul rN.yz, ?, rN.xxxx (the last one seen wins) */
		else if (!eq_at && op == 24 /* eq */ && ln == 7 && !(w[i] & 0x80000000u) && fdx_dst_temp(w, i + 1, &reg, &mask) &&
			 (mask == 1 || mask == 2 || mask == 4 || mask == 8) && FDX_OP_TYPE(w[i + 3]) == 0 &&
			 FDX_OP_PLAIN_1D(w[i + 3]) && FDX_OP_SEL1(w[i + 3]) && FDX_OP_COMP(w[i + 3]) == 0 &&
			 FDX_OP_TYPE(w[i + 5]) == 4 && (w[i + 5] & 3) == 1 && w[i + 6] == SKY && yz_ok == w[i + 4] + 1) {
			eq_at = i, rN = w[i + 4], rC = reg;
			cC = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
		} else if (eq_at && i > eq_at) {
			/* nothing may write rN.x or the condition before the movc */
			if (op == 55 /* movc */ && ln == 9 && !(w[i] & 0x80000000u) && fdx_dst_temp(w, i + 1, &reg, &mask) &&
			    (mask == 1 || mask == 2 || mask == 4 || mask == 8) && FDX_OP_TYPE(w[i + 3]) == 0 &&
			    FDX_OP_PLAIN_1D(w[i + 3]) && FDX_OP_SEL1(w[i + 3]) && w[i + 4] == rC && FDX_OP_COMP(w[i + 3]) == cC &&
			    FDX_OP_TYPE(w[i + 7]) == 0 && FDX_OP_PLAIN_1D(w[i + 7]) && FDX_OP_SEL1(w[i + 7]) &&
			    FDX_OP_COMP(w[i + 7]) == 0 && w[i + 8] == rN) {
				mov_at = i, src2_at = i + 8;
				break;
			}
			if (op < 88 && ln >= 3 && !(w[i] & 0x80000000u) && fdx_dst_temp(w, i + 1, &reg, &mask) &&
			    ((reg == rN && (mask & 1)) || (reg == rC && (mask & (1u << cC))))) {
				*why = "rN.x or the sky test rewritten before its movc";
				goto fail;
			}
		}
		i += ln;
	}
	*why = "no `eq rC.c, rN.x, l(1e7)` after `mul rN.yz, ?, rN.xxxx`";
	if (!eq_at)
		goto fail;
	*why = "no movc with that condition and src2 = rN.x";
	if (!mov_at)
		goto fail;
	/* rebuild */
	size_t add = 12 * 4, m = n + add;
	unsigned char *o = malloc(m);
	*why = "no memory";
	if (!o)
		goto fail;
	uint32_t T = ntemps, ins[12] = {
		(16u /* dp3 */) | (7u << 24), 0x00100012u, T, 0x00100246u, rN, 0x00100246u, rN,
		(75u /* sqrt */) | (5u << 24), 0x00100012u, T, 0x0010000au, T };
	size_t at = (size_t)(pb - blob) + 4 * (size_t)mov_at;
	memcpy(o, blob, at);
	for (int k = 0; k < 12; k++)
		fdx_wr(o + at + 4 * k, ins[k]);
	memcpy(o + at + add, blob + at, n - at);
	fdx_wr(o + (pb - blob) + 4 * (size_t)(temps_at + 1), ntemps + 1);      /* dcl_temps + 1 */
	fdx_wr(o + (pb - blob) + 4, nw + 12);                                     /* the program's length */
	fdx_wr(o + at + add + 4 * (size_t)(src2_at - mov_at), T);                 /* movc src2: rN -> rT */
	fdx_wr(o + shex + 4, clen + (uint32_t)add);                               /* the chunk's size */
	fdx_wr(o + 24, (uint32_t)m);                                              /* the container's */
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t co = fdx_rd(blob + 32 + 4 * c);
		if (co > shex)
			fdx_wr(o + 32 + 4 * c, co + (uint32_t)add);
	}
	somvr_dxbc_checksum(o, m, o + 4);
	free(w);
	*n_out = m;
	*why = NULL;
	return o;
fail:
	free(w);
	return NULL;
}
#endif
