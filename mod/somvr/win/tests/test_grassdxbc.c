/* test_grassdxbc.c - win/somvr_grassdxbc.h offline (docs/re-xr-pipeline.md 148): cc -O2 -Wall -Werror -o t test_grassdxbc.c
 *   ./t                 synthetic cases (grass-shaped programs in containers with a real reflection: the edits, the refusals)
 *   ./t FILE.dxbc ...   + each real blob (game data, not in the repository: the VS/PS of
 *                       shaders\shadertrees\solid\scattergrass_parallaxfixed.fxo and scattergrassclump.fxo out of the
 *                       archives): the parts made, the container's size and checksum */
#include <stdio.h>
#include "../somvr_grassdxbc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* a reflection: $Globals (cb slot 0) with the variables at the registers the game's shader has, CBuffer_View (slot 12)
 * with k_vWorldSpaceCameraDir at c26 */
static size_t rdef(unsigned char *d, int with_wind, int with_bend, int with_par)
{
	memset(d, 0, 1024);
	const char *names[] = { "$Globals", "CBuffer_View", "k_vObjectSpaceEyePos", "k_vWindPhase", "k_fTriangleBend",
				"k_vParallaxDepth", "k_vWorldSpaceCameraDir", "k_vNothing" };
	uint32_t no[8], at = 60 + 2 * 32 + 2 * 24 + 6 * 40 + 1 * 40;
	for (int i = 0; i < 8; i++) {
		no[i] = at;
		strcpy((char *)d + at, names[i]);
		at += (uint32_t)strlen(names[i]) + 1;
	}
	fdx_wr(d, 2), fdx_wr(d + 4, 60 + 64), fdx_wr(d + 8, 2), fdx_wr(d + 12, 60), fdx_wr(d + 16, 0xfffe0500u);
	fdx_wr(d + 28, 0x31314452u); /* RD11 */
	/* the bindings */
	fdx_wr(d + 60, no[0]), fdx_wr(d + 60 + 20, 0), fdx_wr(d + 60 + 24, 1);
	fdx_wr(d + 92, no[1]), fdx_wr(d + 92 + 20, 12), fdx_wr(d + 92 + 24, 1);
	/* the cbuffers */
	uint32_t vo = 60 + 64 + 48;
	fdx_wr(d + 124, no[0]), fdx_wr(d + 124 + 4, 4), fdx_wr(d + 124 + 8, vo), fdx_wr(d + 124 + 12, 464);
	fdx_wr(d + 148, no[1]), fdx_wr(d + 148 + 4, 1), fdx_wr(d + 148 + 8, vo + 6 * 40), fdx_wr(d + 148 + 12, 816);
	/* $Globals' variables: k_vObjectSpaceEyePos c12, k_vWindPhase c24, k_fTriangleBend c23, k_vParallaxDepth c27 */
	uint32_t v[4][2] = { { no[2], 12 * 16 }, { with_wind ? no[3] : no[7], 24 * 16 }, { with_bend ? no[4] : no[7], 23 * 16 },
			     { with_par ? no[5] : no[7], 27 * 16 } };
	for (int i = 0; i < 4; i++)
		fdx_wr(d + vo + 40 * i, v[i][0]), fdx_wr(d + vo + 40 * i + 4, v[i][1]), fdx_wr(d + vo + 40 * i + 8, 12);
	fdx_wr(d + vo + 6 * 40, no[6]), fdx_wr(d + vo + 6 * 40 + 4, 26 * 16), fdx_wr(d + vo + 6 * 40 + 8, 12);
	return at;
}

/* a container: RDEF, SHEX, STAT (empty); the checksum filled in */
static unsigned char *wrap(const uint32_t *prog, uint32_t nw, int wind, int bend, int par, size_t *n)
{
	unsigned char rd[1024];
	uint32_t rn = (uint32_t)((rdef(rd, wind, bend, par) + 3) & ~(size_t)3);
	uint32_t hdr = 32 + 3 * 4, shex = 8 + 4 * nw;
	*n = hdr + 8 + rn + shex + 8;
	unsigned char *b = calloc(1, *n);
	memcpy(b, "DXBC", 4), fdx_wr(b + 20, 1), fdx_wr(b + 24, (uint32_t)*n), fdx_wr(b + 28, 3);
	uint32_t o = hdr;
	fdx_wr(b + 32, o), memcpy(b + o, "RDEF", 4), fdx_wr(b + o + 4, rn), memcpy(b + o + 8, rd, rn), o += 8 + rn;
	fdx_wr(b + 36, o), memcpy(b + o, "SHEX", 4), fdx_wr(b + o + 4, 4 * nw);
	for (uint32_t i = 0; i < nw; i++)
		fdx_wr(b + o + 8 + 4 * i, prog[i]);
	fdx_wr(b + o + 12, nw);
	o += shex;
	fdx_wr(b + 40, o), memcpy(b + o, "STAT", 4), fdx_wr(b + o + 4, 0);
	somvr_dxbc_checksum(b, *n, b + 4);
	return b;
}

#define T1(op, n) ((uint32_t)(op) | (uint32_t)(n) << 24)
/* the vertex shader's links, word for word as in the game's (instructions 6, 21, 45, 131 of scattergrass_parallaxfixed) */
static uint32_t vs[] = {
	0x10050, 0,
	T1(95, 3), 0x00101072, 0,                                                    /* dcl_input v0.xyz */
	T1(104, 2), 8,                                                               /* dcl_temps 8 */
	T1(16, 8), 0x00100012, 0, 0x00208246, 12, 26, 0x00100246, 3,                 /* dp3 r0.x, cb12[26].xyzx, r3.xyzx */
	T1(51, 7), 0x00100012, 1, 0x0010000a, 1, 0x00004001, 0x42c80000,             /* min r1.x, r1.x, l(100) */
	T1(0, 8), 0x00100022, 6, 0x8010000a, 0x41, 1, 0x0010001a, 6,                  /* add r6.y, -r1.x, r6.y */
	T1(62, 1) };
/* the pixel shader's two parallax mads (instructions 26, 27) */
static uint32_t ps[] = {
	0x50, 0,
	T1(104, 2), 3,
	T1(50, 11), 0x00100032, 0, 0x80100046, 0x41, 0, 0x00208006, 0, 27, 0x00101ae6, 1, /* mad r0.xy, -r0.xyxx, cb0[27].xxxx, v1.zwzz */
	T1(50, 10), 0x001000c2, 0, 0x00100ea6, 0, 0x00208556, 0, 27, 0x00101406, 1,       /* mad r0.zw, r0.zzzw, cb0[27].yyyy, v1.xxxy */
	T1(62, 1) };
#define N(a) (uint32_t)(sizeof a / sizeof a[0])

static const uint32_t *prog_of(const unsigned char *b, uint32_t *nw)
{
	uint32_t s = gdx_chunk(b, 0, "SHEX");
	static uint32_t w[512];
	*nw = fdx_rd(b + s + 4) / 4;
	for (uint32_t i = 0; i < *nw && i < 512; i++)
		w[i] = fdx_rd(b + s + 8 + 4 * i);
	return w;
}

static void check_container(const unsigned char *o, size_t m, const char *what)
{
	unsigned char ck[16];
	somvr_dxbc_checksum(o, m, ck);
	CHECK(!memcmp(ck, o + 4, 16), "%s: checksum", what);
	CHECK(fdx_rd(o + 24) == m, "%s: total size %u, want %zu", what, fdx_rd(o + 24), m);
	uint32_t s = gdx_chunk(o, m, "SHEX"), st = gdx_chunk(o, m, "STAT");
	CHECK(st == s + 8 + fdx_rd(o + s + 4), "%s: STAT at %u, want %u", what, st, s + 8 + fdx_rd(o + s + 4));
	CHECK(fdx_rd(o + s + 12) == fdx_rd(o + s + 4) / 4, "%s: the program's length", what);
}

static void synthetic(void)
{
	size_t n, m;
	unsigned done;
	const char *why;
	/* the vertex shader: BEND + PUSH */
	unsigned char *b = wrap(vs, N(vs), 1, 1, 1, &n);
	unsigned char *o = somvr_grassdxbc(b, n, SOMVR_GRASS_BEND | SOMVR_GRASS_PUSH | SOMVR_GRASS_FRONT, 0.3f, &m, &done, &why);
	CHECK(o && done == (SOMVR_GRASS_BEND | SOMVR_GRASS_PUSH), "vs: done %u why %s", done, why ? why : "-");
	if (o) {
		uint32_t nw;
		const uint32_t *w = prog_of(o, &nw);
		CHECK(m == n + 27 * 4 && nw == N(vs) + 27, "vs: size %zu (from %zu), words %u", m, n, nw);
		CHECK(w[6] == 9, "vs: dcl_temps %u, want 9", w[6]);
		CHECK(w[7] == T1(0, 9) && w[9] == 8 && w[10] == 0x00101246u && w[15] == 12, "vs: the add");
		CHECK(w[35] == T1(16, 7) && w[36] == 0x00100012u && w[38] == 0x00100246u && w[39] == 8 && w[40] == 0x00100246u &&
		      w[41] == 3, "vs: the dp3 takes r8.xyzx");
		CHECK(w[42] == T1(51, 7) && w[48] == 0, "vs: min .., l(0)");
		check_container(o, m, "vs");
		free(o);
	}
	/* only PUSH asked */
	o = somvr_grassdxbc(b, n, SOMVR_GRASS_PUSH, 0.3f, &m, &done, &why);
	CHECK(o && done == SOMVR_GRASS_PUSH && m == n, "vs push only: done %u", done);
	if (o) {
		uint32_t nw;
		const uint32_t *w = prog_of(o, &nw);
		CHECK(w[6] == 8 && w[7] == vs[7] && w[21] == 0, "vs push only: the rest the game's");
		check_container(o, m, "vs push");
		free(o);
	}
	free(b);
	/* no k_fTriangleBend (the clump): PUSH only */
	b = wrap(vs, N(vs), 1, 0, 0, &n);
	o = somvr_grassdxbc(b, n, SOMVR_GRASS_BEND | SOMVR_GRASS_PUSH, 0.3f, &m, &done, &why);
	CHECK(o && done == SOMVR_GRASS_PUSH, "clump-like vs: done %u", done);
	free(o), free(b);
	/* not the grass (no k_vWindPhase): untouched */
	b = wrap(vs, N(vs), 0, 1, 1, &n);
	o = somvr_grassdxbc(b, n, 7, 0.3f, &m, &done, &why);
	CHECK(!o && done == 0 && why, "not grass: refused");
	free(o), free(b);
	/* two pushes: PUSH refused, BEND made */
	uint32_t vs2[N(vs) + 7];
	memcpy(vs2, vs, sizeof vs);
	memcpy(vs2 + N(vs) - 1, vs + 15, 7 * 4);
	vs2[N(vs2) - 1] = T1(62, 1);
	b = wrap(vs2, N(vs2), 1, 1, 1, &n);
	o = somvr_grassdxbc(b, n, SOMVR_GRASS_BEND | SOMVR_GRASS_PUSH, 0.3f, &m, &done, &why);
	CHECK(o && done == SOMVR_GRASS_BEND, "two mins: done %u", done);
	free(o), free(b);
	/* the pixel shader: FRONT */
	b = wrap(ps, N(ps), 1, 1, 1, &n);
	o = somvr_grassdxbc(b, n, 7, 0.25f, &m, &done, &why);
	CHECK(o && done == SOMVR_GRASS_FRONT, "ps: done %u why %s", done, why ? why : "-");
	if (o) {
		uint32_t nw, f;
		const uint32_t *w = prog_of(o, &nw);
		float fv = 0.25f;
		memcpy(&f, &fv, 4);
		CHECK(m == n + 8 && nw == N(ps) + 2, "ps: size");
		CHECK(!memcmp(w + 4, ps + 4, 11 * 4), "ps: the back plane's mad unchanged");
		CHECK(w[15] == T1(50, 12) && w[16] == 0x001000c2u && w[18] == 0x00100ea6u && w[20] == 0x00004002u &&
		      w[21] == f && w[24] == f && w[25] == 0x00101406u && w[26] == 1 && w[27] == T1(62, 1),
		      "ps: mad r0.zw, r0.zzzw, l(0.25,..), v1.xxxy");
		check_container(o, m, "ps");
		free(o);
	}
	/* a PS without k_vParallaxDepth (the clump's): nothing */
	free(b);
	b = wrap(ps, N(ps), 1, 1, 0, &n);
	o = somvr_grassdxbc(b, n, 7, 0.25f, &m, &done, &why);
	CHECK(!o, "clump-like ps: refused");
	free(o), free(b);
	/* a PS whose front operand is another register: refused */
	uint32_t ps2[N(ps)];
	memcpy(ps2, ps, sizeof ps);
	ps2[22] = 26;
	b = wrap(ps2, N(ps2), 1, 1, 1, &n);
	o = somvr_grassdxbc(b, n, 7, 0.25f, &m, &done, &why);
	CHECK(!o && why, "ps cb0[26]: refused (%s)", why ? why : "-");
	free(o), free(b);
}

int main(int argc, char **argv)
{
	(void)somvr_fogdxbc; /* (the shared header's other edit) */
	synthetic();
	for (int a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb");
		if (!f) {
			CHECK(0, "%s: unreadable", argv[a]);
			continue;
		}
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		unsigned char *b = malloc((size_t)n);
		if (fread(b, 1, (size_t)n, f) != (size_t)n)
			n = 0;
		fclose(f);
		size_t m;
		unsigned done;
		const char *why;
		unsigned char ck[16];
		somvr_dxbc_checksum(b, (size_t)n, ck);
		CHECK(!memcmp(ck, b + 4, 16), "%s: the game's checksum", argv[a]);
		unsigned char *o = somvr_grassdxbc(b, (size_t)n, 7, 0.3f, &m, &done, &why);
		printf("%s: %ld bytes -> %s%s%s%s (%zu bytes) %s\n", argv[a], n, o ? "" : "unchanged",
		       done & SOMVR_GRASS_FRONT ? " front" : "", done & SOMVR_GRASS_BEND ? " bend" : "",
		       done & SOMVR_GRASS_PUSH ? " push" : "", m, why ? why : "");
		if (o) {
			check_container(o, m, argv[a]);
			if (getenv("GRASS_OUT")) {
				char p[4096];
				snprintf(p, sizeof p, "%s/%s.edit", getenv("GRASS_OUT"), strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a]);
				FILE *g = fopen(p, "wb");
				if (g)
					fwrite(o, 1, m, g), fclose(g);
			}
		}
		free(o), free(b);
	}
	printf("test_grassdxbc: %s\n", fails ? "FAILED" : "ok");
	return fails != 0;
}
