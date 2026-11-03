/* test_fogdxbc.c - win/somvr_fogdxbc.h offline (docs/re-winport.md 7.5): cc -O2 -Wall -Werror -o t test_fogdxbc.c
 *   ./t                       synthetic cases only (a fog-shaped program in a real container: the edit, the refusals)
 *   ./t EXE [FOG.dxbc ...]    + the checksum against every DXBC blob in EXE (the exe embeds 927) and the edit on the
 *                             real fog shaders (game data, not in the repository: extracted with
 *                             scripts/winport_dxbc.py scan from the archives) */
#include <stdio.h>
#include "../somvr_fogdxbc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); long m = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc((size_t)m);
	if (b && fread(b, 1, (size_t)m, f) != (size_t)m) { free(b); b = NULL; }
	fclose(f);
	*n = (size_t)m;
	return b;
}

/* a container around a program: RDEF (empty), SHEX, STAT (empty); the checksum filled in */
static unsigned char *wrap(const uint32_t *prog, uint32_t nw, size_t *n)
{
	uint32_t hdr = 32 + 3 * 4, rdef = 8 + 4, shex = 8 + 4 * nw, stat = 8 + 4;
	*n = hdr + rdef + shex + stat;
	unsigned char *b = calloc(1, *n);
	memcpy(b, "DXBC", 4), fdx_wr(b + 20, 1), fdx_wr(b + 24, (uint32_t)*n), fdx_wr(b + 28, 3);
	uint32_t o = hdr;
	fdx_wr(b + 32, o), memcpy(b + o, "RDEF", 4), fdx_wr(b + o + 4, 4), o += rdef;
	fdx_wr(b + 36, o), memcpy(b + o, "SHEX", 4), fdx_wr(b + o + 4, 4 * nw);
	for (uint32_t i = 0; i < nw; i++) fdx_wr(b + o + 8 + 4 * i, prog[i]);
	o += shex;
	fdx_wr(b + 40, o), memcpy(b + o, "STAT", 4), fdx_wr(b + o + 4, 4);
	somvr_dxbc_checksum(b, *n, b + 4);
	return b;
}

/* the opaque fog's core, word for word as the compiler emits it (instructions 13, 30, 35, 36, 37, 38 of the real one) */
#define T1(op, n) ((uint32_t)(op) | (uint32_t)(n) << 24)
static uint32_t prog[] = {
	0x50, 0, /* ps_5_0, length (filled) */
	T1(104, 2), 6,                                                  /* dcl_temps 6 */
	T1(0, 7), 0x00100012, 0, 0x00004001, 0x4e17e580, 0x00208ff6, 0,  /* add r0.x, l(6.371e8), r0.w (cb in the real one) */
	T1(54, 5), 0x00100082, 3, 0x00004001, 0x3f800000,               /* mov r3.w, l(1) */
	T1(56, 7), 0x00100062, 3, 0x00100d56, 0, 0x00100006, 3,          /* mul r3.yz, r0.yywy, r3.xxxx */
	T1(24, 7), 0x00100022, 0, 0x0010000a, 3, 0x00004001, 0x4b189680, /* eq r0.y, r3.x, l(1e7) */
	T1(17, 7), 0x00100082, 0, 0x00100e46, 1, 0x00100e46, 3,          /* dp4 r0.w, r1.xyzw, r3.xyzw */
	T1(55, 9), 0x00100012, 0, 0x0010001a, 0, 0x0010000a, 0, 0x0010000a, 3, /* movc r0.x, r0.y, r0.x, r3.x */
	T1(62, 1),                                                      /* ret */
};
#define NP (uint32_t)(sizeof prog / sizeof prog[0])

static void check_out(const unsigned char *o, size_t m, size_t n, const char *what)
{
	unsigned char ck[16];
	CHECK(m == n + 48, "%s: size %zu, want %zu", what, m, n + 48);
	somvr_dxbc_checksum(o, m, ck);
	CHECK(!memcmp(ck, o + 4, 16), "%s: the new checksum does not verify", what);
	CHECK(fdx_rd(o + 24) == m, "%s: the container size", what);
	size_t n2;
	const char *why;
	unsigned char *again = somvr_fogdxbc(o, m, &n2, &why);
	CHECK(!again, "%s: a second edit went in (not idempotent)", what);
	free(again);
}

int main(int argc, char **argv)
{
	prog[1] = NP;
	size_t n, m;
	const char *why;
	unsigned char *b = wrap(prog, NP, &n), *o = somvr_fogdxbc(b, n, &m, &why);
	CHECK(o, "synthetic: not edited: %s", why ? why : "?");
	if (o) {
		check_out(o, m, n, "synthetic");
		/* the words: dcl_temps 7; before the movc dp3 r6.x, r3.xyzx, r3.xyzx; sqrt r6.x, r6.x; the movc's src2 = r6.x */
		const unsigned char *p = o + 32 + 12 + 12 + 8;
		CHECK(fdx_rd(p + 4) == NP + 12, "synthetic: program length %u", fdx_rd(p + 4));
		CHECK(fdx_rd(p + 4 * 3) == 7, "synthetic: dcl_temps %u", fdx_rd(p + 4 * 3));
		uint32_t mv = 2 + 2 + 7 + 5 + 7 + 7 + 7;
		static const uint32_t want[12] = { T1(16, 7), 0x00100012, 6, 0x00100246, 3, 0x00100246, 3, T1(75, 5), 0x00100012, 6, 0x0010000a, 6 };
		for (int k = 0; k < 12; k++)
			CHECK(fdx_rd(p + 4 * (mv + k)) == want[k], "synthetic: inserted word %d = %#x", k, fdx_rd(p + 4 * (mv + k)));
		CHECK(fdx_rd(p + 4 * (mv + 12)) == T1(55, 9) && fdx_rd(p + 4 * (mv + 12 + 8)) == 6, "synthetic: the movc's src2 not r6");
		CHECK(fdx_rd(o + 40) == fdx_rd(b + 40) + 48, "synthetic: STAT's offset not moved");
		free(o);
	}
	free(b);
	/* refusals: each link broken once -> nothing written */
	struct { uint32_t at, val; const char *what; } bad[] = {
		{ 8, 0x4e17e581, "no planet radius" }, { 29, 0x4b189681, "another sky depth" }, { 21, 0x00100016, "mul rN.yz from rN.y" },
		{ 44, 0x0010001a, "movc src2 = rN.y" }, { 45, 2, "movc src2 = another register" }, { 40, 0x0010000a, "movc condition .x" },
		{ 31, 0x00100012, "dp4 writes r0.x (not the condition: harmless)" },
	};
	for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
		uint32_t keep = prog[bad[k].at];
		prog[bad[k].at] = bad[k].val;
		b = wrap(prog, NP, &n), o = somvr_fogdxbc(b, n, &m, &why);
		if (k == 6)
			CHECK(o != NULL, "an unrelated write refused: %s", why ? why : "?");
		else
			CHECK(!o, "%s: edited anyway", bad[k].what);
		free(o), free(b);
		prog[bad[k].at] = keep;
	}
	/* the condition rewritten between the eq and the movc: dp4 r0.y */
	prog[31] = 0x00100022;
	b = wrap(prog, NP, &n), o = somvr_fogdxbc(b, n, &m, &why);
	CHECK(!o, "the condition rewritten before the movc: edited anyway");
	free(o), free(b);
	prog[31] = 0x00100082;
	int nblob = 0, ok = 0;
	if (argc > 1) {
		size_t en;
		unsigned char *e = slurp(argv[1], &en);
		CHECK(e, "cannot read %s", argv[1]);
		for (size_t i = 0; e && i + 32 < en; i++) {
			if (memcmp(e + i, "DXBC", 4) || fdx_rd(e + i + 20) != 1) continue;
			uint32_t sz = fdx_rd(e + i + 24);
			if (sz < 32 || sz > (4u << 20) || i + sz > en) continue;
			unsigned char ck[16];
			somvr_dxbc_checksum(e + i, sz, ck);
			nblob++, ok += !memcmp(ck, e + i + 4, 16);
		}
		CHECK(nblob > 0 && ok == nblob, "checksums: %d of %d blobs", ok, nblob);
		free(e);
	}
	for (int a = 2; a < argc; a++) {
		size_t fn;
		unsigned char *f = slurp(argv[a], &fn);
		CHECK(f, "cannot read %s", argv[a]);
		if (!f) continue;
		o = somvr_fogdxbc(f, fn, &m, &why);
		CHECK(o, "%s: not edited: %s", argv[a], why ? why : "?");
		if (o) check_out(o, m, fn, argv[a]);
		if (o && argc > 2) {
			char out[4096];
			snprintf(out, sizeof out, "%s.radial", argv[a]);
			FILE *w = fopen(out, "wb");
			if (w) fwrite(o, 1, m, w), fclose(w);
		}
		free(o), free(f);
	}
	printf("test_fogdxbc: %s (synthetic + %d refusals%s; checksums %d/%d; real shaders %d)\n", fails ? "FAIL" : "ok", 8,
	       "", ok, nblob, argc > 2 ? argc - 2 : 0);
	return fails != 0;
}
