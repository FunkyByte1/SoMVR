/* test_raindxbc.c - win/somvr_raindxbc.h offline (build 37, SOMVR_RAIN_SCALE, docs/re-xr-pipeline.md 173.7):
 *   ./t                  synthetic cases (the rain sheet's tail in a container whose RDEF names the rain: the edit, the
 *                        refusals)
 *   ./t RAIN.dxbc [OUT]  + the edit on a real shader (game data, not in the repository: gpupart_rain.fxo's 4232-byte PS
 *                        ef99d1c325a70575... from son_weather.arch05 sonday03rain.embb (tools/ltar.py), or DLC2.arch05's
 *                        5328-byte sheet 38461aa78d30cac3...); the GS and VS of gpupart_rain are refused */
#include <stdio.h>
#pragma GCC diagnostic ignored "-Wunused-function" /* the fog's edits, unused here */
#include "../somvr_raindxbc.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define T1(op, n) ((uint32_t)(op) | (uint32_t)(n) << 24)

/* RDEF (the names given), SHEX; the checksum filled in */
static unsigned char *wrap(const char *names, const uint32_t *prog, uint32_t nw, size_t *n)
{
	uint32_t nl = (uint32_t)((strlen(names) + 4) & ~3u);
	uint32_t hdr = 32 + 2 * 4, rdef = 8 + nl, shex = 8 + 4 * nw;
	*n = hdr + rdef + shex;
	unsigned char *b = calloc(1, *n);
	memcpy(b, "DXBC", 4), fdx_wr(b + 20, 1), fdx_wr(b + 24, (uint32_t)*n), fdx_wr(b + 28, 2);
	uint32_t o = hdr;
	fdx_wr(b + 32, o), memcpy(b + o, "RDEF", 4), fdx_wr(b + o + 4, nl), memcpy(b + o + 8, names, strlen(names)), o += rdef;
	fdx_wr(b + 36, o), memcpy(b + o, "SHEX", 4), fdx_wr(b + o + 4, 4 * nw);
	for (uint32_t i = 0; i < nw; i++)
		fdx_wr(b + o + 8 + 4 * i, prog[i]);
	somvr_dxbc_checksum(b, *n, b + 4);
	return b;
}

/* the real shader's last four instructions, word for word (45-48): dp3 r0.w, r0.xyzx, cb0[35].xyzx; mov o0.xyz, r0.xyzx;
 * mul o0.w, r0.w, cb0[16].x; ret - after a dcl_temps 3 and one mul */
static uint32_t prog[] = {
	0x00000040, 0,                                               /* ps_4_0 (version), the length */
	T1(104, 2), 3,                                               /* dcl_temps 3 */
	T1(56, 7), 0x001000f2, 0, 0x00100e46, 0, 0x00100e46, 1,     /* mul r0.xyzw, r0.xyzw, r1.xyzw */
	T1(16, 8), 0x00100082, 0, 0x00100246, 0, 0x00208246, 0, 35, /* dp3 r0.w, r0.xyzx, cb0[35].xyzx */
	T1(54, 5), 0x00102072, 0, 0x00100246, 0,                     /* mov o0.xyz, r0.xyzx */
	T1(56, 8), 0x00102082, 0, 0x0010003a, 0, 0x0020800a, 0, 16,  /* mul o0.w, r0.w, cb0[16].x */
	T1(62, 1),                                                   /* ret */
};
#define NP (uint32_t)(sizeof prog / sizeof prog[0])
#define NAMES "k_fRainBrightness k_fMistBrightness k_fRainPanU"

int main(int argc, char **argv)
{
	prog[1] = NP;
	size_t n, m;
	const char *why;
	unsigned char *b = wrap(NAMES, prog, NP, &n), *o = somvr_raindxbc(b, n, 0.5f, &m, &why);
	CHECK(o && m == n + 40, "synthetic: not edited (%s) or size %zu (want %zu)", why ? why : "-", m, n + 40);
	if (o) {
		unsigned char ck[16];
		somvr_dxbc_checksum(o, m, ck);
		CHECK(!memcmp(ck, o + 4, 16) && fdx_rd(o + 24) == m, "synthetic: checksum / size");
		const unsigned char *rd = o + 32 + 8, *p = o + fdx_rd(o + 36) + 8;
		CHECK(!memcmp(rd + 8, "k_fRainBrightness", 17), "synthetic: RDEF moved");
		CHECK(fdx_rd(p + 4) == NP + 10, "synthetic: program length %u", fdx_rd(p + 4));
		static const uint32_t want[10] = { T1(56, 10), 0x001000f2, 0, 0x00100e46, 0, 0x00004002, 0x3f000000, 0x3f000000,
						   0x3f000000, 0x3f000000 };
		uint32_t at = 2 + 2 + 7 + 8; /* after the dp3, before the mov */
		for (int k = 0; k < 10; k++)
			CHECK(fdx_rd(p + 4 * (at + k)) == want[k], "synthetic: inserted word %d = %#x", k, fdx_rd(p + 4 * (at + k)));
		CHECK(fdx_rd(p + 4 * (at + 10)) == T1(54, 5) && fdx_rd(p + 4 * (at + 15)) == T1(56, 8) && fdx_rd(p + 4 * (at + 23)) == T1(62, 1),
		      "synthetic: the tail after it");
		size_t m2;
		unsigned char *again = somvr_raindxbc(o, m, 0.5f, &m2, &why);
		CHECK(!again, "synthetic: a second edit went in");
		free(again);
		free(o);
	}
	free(b);
	/* refusals: no rain names; another output register; another source; a tail that does not end */
	b = wrap("k_fOtherBrightness k_fMistBrightness", prog, NP, &n), o = somvr_raindxbc(b, n, 0.5f, &m, &why);
	CHECK(!o, "no k_fRainBrightness: edited");
	free(o), free(b);
	/* the GPU rain's names: edited; the sparks' streak (k_fStreakLength, no k_fPeakVelocity / k_vSplatInfo): not */
	b = wrap("k_cTintColor k_vParticleScale k_fPeakVelocity k_vSplatInfo k_fStreakLength", prog, NP, &n);
	o = somvr_raindxbc(b, n, 0.5f, &m, &why);
	CHECK(o && m == n + 40, "the GPU rain's names: not edited (%s)", why ? why : "-");
	free(o), free(b);
	b = wrap("k_vParticleScale k_fStreakLength k_fHDRScale", prog, NP, &n), o = somvr_raindxbc(b, n, 0.5f, &m, &why);
	CHECK(!o, "a spark streak edited");
	free(o), free(b);
	struct { uint32_t at, val; const char *what; } bad[] = {
		{ 21, 1, "mov to o1" }, { 23, 2, "mov from r2" }, { 28, 1, "mul from r1" }, { 32, T1(55, 1), "no ret" },
	};
	for (unsigned q = 0; q < sizeof bad / sizeof bad[0]; q++) {
		uint32_t keep = prog[bad[q].at];
		prog[bad[q].at] = bad[q].val;
		b = wrap(NAMES, prog, NP, &n), o = somvr_raindxbc(b, n, 0.5f, &m, &why);
		CHECK(!o, "%s: edited", bad[q].what);
		free(o), free(b);
		prog[bad[q].at] = keep;
	}
	if (argc > 1) { /* the real rain sheet */
		FILE *f = fopen(argv[1], "rb");
		if (f) {
			unsigned char *r = malloc(1 << 20);
			size_t rn = fread(r, 1, 1 << 20, f);
			fclose(f);
			o = somvr_raindxbc(r, rn, 0.5f, &m, &why);
			CHECK(o && m == rn + 40, "%s: not edited: %s", argv[1], why ? why : "?");
			if (o) {
				unsigned char ck[16];
				somvr_dxbc_checksum(o, m, ck);
				CHECK(!memcmp(ck, o + 4, 16), "%s: checksum", argv[1]);
				FILE *g = argc > 2 ? fopen(argv[2], "wb") : NULL;
				if (g)
					fwrite(o, 1, m, g), fclose(g);
			}
			free(o), free(r);
		}
	}
	printf("test_raindxbc: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
