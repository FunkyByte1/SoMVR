/* test_bubbledxbc.c - win/somvr_bubbledxbc.h offline (docs/re-xr-pipeline.md 166): cc -O2 -Wall -Werror -o t test_bubbledxbc.c
 *   ./t                 synthetic cases: foliage / bug shaped programs (the game's own instruction words) in containers
 *                       with a reflection and signatures; the edits word for word, the refusals, the containers; and the
 *                       inserted code RUN by a small interpreter: the dissolve's band, the fallback, the bugs' fade
 *   ./t FILE ...        + every DXBC blob in each file (game data, not in the repository: the .fxo of veg_*, scattergrass*,
 *                       gpupart_* out of the archives): what each edit makes of it, the rebuilt container checked */
#include <stdio.h>
#include <math.h>
#include "../somvr_bubbledxbc.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the container builder ------------------------------------------------------------------------------------ */
typedef struct { const char *name; uint32_t off; } var;      /* a variable at a byte offset */
typedef struct { const char *name; uint32_t slot; const var *v; int nv; } cbuf;
typedef struct { const char *sem; uint32_t idx, sv, reg; unsigned char mask, rw; } sel;

/* RDEF (RD11): the cbuffers with their variables, each bound at its slot */
static uint32_t rdef(unsigned char *d, const cbuf *cb, int ncb)
{
	memset(d, 0, 4096);
	int nv = 0;
	for (int i = 0; i < ncb; i++)
		nv += cb[i].nv;
	uint32_t bo = 60, cbo = bo + 32 * (uint32_t)ncb, vo = cbo + 24 * (uint32_t)ncb, at = vo + 40 * (uint32_t)nv;
	fdx_wr(d, (uint32_t)ncb), fdx_wr(d + 4, cbo), fdx_wr(d + 8, (uint32_t)ncb), fdx_wr(d + 12, bo), fdx_wr(d + 16, 0xfffe0500u);
	fdx_wr(d + 28, 0x31314452u);
	uint32_t vi = 0;
	for (int i = 0; i < ncb; i++) {
		uint32_t no = at;
		strcpy((char *)d + at, cb[i].name), at += (uint32_t)strlen(cb[i].name) + 1;
		fdx_wr(d + bo + 32 * i, no), fdx_wr(d + bo + 32 * i + 4, 0), fdx_wr(d + bo + 32 * i + 20, cb[i].slot), fdx_wr(d + bo + 32 * i + 24, 1);
		fdx_wr(d + cbo + 24 * i, no), fdx_wr(d + cbo + 24 * i + 4, (uint32_t)cb[i].nv), fdx_wr(d + cbo + 24 * i + 8, vo + 40 * vi);
		fdx_wr(d + cbo + 24 * i + 12, 1024);
		for (int k = 0; k < cb[i].nv; k++, vi++) {
			fdx_wr(d + vo + 40 * vi, at), fdx_wr(d + vo + 40 * vi + 4, cb[i].v[k].off), fdx_wr(d + vo + 40 * vi + 8, 16);
			strcpy((char *)d + at, cb[i].v[k].name), at += (uint32_t)strlen(cb[i].v[k].name) + 1;
		}
	}
	return (at + 3) & ~3u;
}

/* a signature chunk's data (ISGN/OSGN 24-byte elements, OSG5 28 with the stream first) */
static uint32_t sig(unsigned char *d, const sel *e, int ne, int five)
{
	memset(d, 0, 1024);
	uint32_t esz = five ? 28 : 24, at = 8 + esz * (uint32_t)ne;
	fdx_wr(d, (uint32_t)ne), fdx_wr(d + 4, 8);
	for (int i = 0; i < ne; i++) {
		unsigned char *x = d + 8 + esz * i + (five ? 4 : 0);
		fdx_wr(x, at), fdx_wr(x + 4, e[i].idx), fdx_wr(x + 8, e[i].sv), fdx_wr(x + 12, 3), fdx_wr(x + 16, e[i].reg);
		x[20] = e[i].mask, x[21] = e[i].rw;
		strcpy((char *)d + at, e[i].sem), at += (uint32_t)strlen(e[i].sem) + 1;
	}
	return (at + 3) & ~3u;
}

/* the container: RDEF, ISGN, OSGN/OSG5, SHEX (kind's version word, prog[1] = the length filled), STAT; the checksum */
static unsigned char *wrap(uint32_t *prog, uint32_t nw, const cbuf *cb, int ncb, const sel *in, int nin, const sel *out,
			   int nout, int five, size_t *n)
{
	static unsigned char rd[4096], is[1024], os[1024];
	uint32_t rn = rdef(rd, cb, ncb), in_n = sig(is, in, nin, 0), on = sig(os, out, nout, five);
	prog[1] = nw;
	uint32_t hdr = 32 + 5 * 4;
	*n = hdr + 8 + rn + 8 + in_n + 8 + on + 8 + 4 * nw + 8;
	unsigned char *b = calloc(1, *n);
	memcpy(b, "DXBC", 4), fdx_wr(b + 20, 1), fdx_wr(b + 24, (uint32_t)*n), fdx_wr(b + 28, 5);
	uint32_t o = hdr;
	const char *tags[4] = { "RDEF", "ISGN", five ? "OSG5" : "OSGN", "SHEX" };
	const unsigned char *data[3] = { rd, is, os };
	uint32_t lens[3] = { rn, in_n, on };
	for (int c = 0; c < 3; c++)
		fdx_wr(b + 32 + 4 * c, o), memcpy(b + o, tags[c], 4), fdx_wr(b + o + 4, lens[c]), memcpy(b + o + 8, data[c], lens[c]), o += 8 + lens[c];
	fdx_wr(b + 44, o), memcpy(b + o, "SHEX", 4), fdx_wr(b + o + 4, 4 * nw);
	for (uint32_t i = 0; i < nw; i++)
		fdx_wr(b + o + 8 + 4 * i, prog[i]);
	o += 8 + 4 * nw;
	fdx_wr(b + 48, o), memcpy(b + o, "STAT", 4), fdx_wr(b + o + 4, 0);
	somvr_dxbc_checksum(b, *n, b + 4);
	return b;
}

/* a rebuilt container: sizes, chunk offsets, the program's length word, the checksum */
static void check_container(const unsigned char *b, size_t m, const char *what)
{
	unsigned char ck[16];
	CHECK(fdx_rd(b + 24) == m, "%s: the total size", what);
	uint32_t nch = fdx_rd(b + 28), end = 32 + 4 * nch;
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t o = fdx_rd(b + 32 + 4 * c);
		CHECK(o == end, "%s: chunk %u at %u, the previous ends at %u", what, c, o, end);
		end = o + 8 + fdx_rd(b + o + 4);
	}
	CHECK(end == m, "%s: the chunks end at %u of %zu", what, end, m);
	uint32_t sh = gdx_chunk(b, m, "SHEX");
	CHECK(sh && fdx_rd(b + sh + 12) * 4 == fdx_rd(b + sh + 4), "%s: the program's length word", what);
	somvr_dxbc_checksum(b, m, ck);
	CHECK(!memcmp(ck, b + 4, 16), "%s: the checksum", what);
}

/* ---- a small interpreter: the opcodes the edits insert (and the test programs use) ------------------------------- */
typedef struct { float r[16][4], v[16][4], o[16][4], gv[16][4], cb[16][64][4]; int discarded; } st;

static float *opnd(st *s, const uint32_t *w, uint32_t *k, float tmp[4], int dst, unsigned *mask)
{
	uint32_t t = w[*k], type = (t >> 12) & 0xff, dim = (t >> 20) & 3, mod = 0;
	(*k)++;
	if (t & 0x80000000u)
		mod = (w[(*k)++] >> 6) & 0xff;
	if (type == 4) { /* immediate */
		uint32_t nc = (t & 3) == 1 ? 1 : 4;
		for (uint32_t i = 0; i < 4; i++)
			memcpy(&tmp[i], &w[*k + (nc == 1 ? 0 : i)], 4);
		*k += nc;
		return tmp;
	}
	float *base = NULL;
	if (type == 0)
		base = s->r[w[*k]], (*k)++;
	else if (type == 1 && dim == 1)
		base = s->v[w[*k]], (*k)++;
	else if (type == 1 && dim == 2)
		base = s->gv[w[*k + 1]], *k += 2; /* v[vertex][reg]: vertex 0 only */
	else if (type == 2)
		base = s->o[w[*k]], (*k)++;
	else if (type == 8)
		base = s->cb[w[*k]][w[*k + 1]], *k += 2;
	if (!base)
		return NULL;
	if (dst) {
		*mask = (t >> 4) & 0xf;
		return base;
	}
	uint32_t sm = (t >> 2) & 3, sw = (t >> 4) & 0xff;
	for (int i = 0; i < 4; i++) {
		int c = sm == 1 ? (int)((sw >> (2 * i)) & 3) : sm == 2 ? (int)(sw & 3) : i;
		float x = base[c];
		if (mod & 2)
			x = fabsf(x);
		if (mod & 1)
			x = -x;
		tmp[i] = x;
	}
	return tmp;
}

/* run the program from its first non-declaration to ret; 0 when an opcode is not known */
static int run(st *s, const uint32_t *w, uint32_t nw)
{
	for (uint32_t i = 2; i < nw;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff, k = i + 1, m = 0, ui;
		if (!ln)
			return 0;
		if (bdx_is_dcl(op)) {
			i += ln;
			continue;
		}
		if (op == 62)
			return 1;
		float a[4], b[4], c[4], res[4] = { 0, 0, 0, 0 }, *d = NULL, *x, *y, *z;
		if (op == 13) { /* discard_nz / _z */
			x = opnd(s, w, &k, a, 0, &m);
			uint32_t bits;
			memcpy(&bits, &x[0], 4);
			if ((bits != 0) == ((w[i] >> 18) & 1))
				s->discarded = 1;
			i += ln;
			continue;
		}
		d = opnd(s, w, &k, NULL, 1, &m);
		if (op == 77 && k < i + ln && ((w[k] >> 12) & 0xff) == 13)
			k++; /* sincos's cos destination: null */
		x = opnd(s, w, &k, a, 0, &ui);
		y = k < i + ln ? opnd(s, w, &k, b, 0, &ui) : NULL;
		z = k < i + ln ? opnd(s, w, &k, c, 0, &ui) : NULL;
		for (int q = 0; q < 4; q++) {
			uint32_t t1 = 0xffffffffu;
			switch (op) {
			case 54: res[q] = x[q]; break;
			case 0: res[q] = x[q] + y[q]; break;
			case 56: res[q] = x[q] * y[q]; break;
			case 50: res[q] = x[q] * y[q] + z[q]; break;
			case 15: res[q] = x[0] * y[0] + x[1] * y[1]; break;
			case 16: res[q] = x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; break;
			case 17: res[q] = x[0] * y[0] + x[1] * y[1] + x[2] * y[2] + x[3] * y[3]; break;
			case 75: res[q] = sqrtf(x[q]); break;
			case 77: res[q] = sinf(x[q]); break;
			case 26: res[q] = x[q] - floorf(x[q]); break;
			case 65: res[q] = floorf(x[q]); break;
			case 29: memcpy(&res[q], x[q] >= y[q] ? &t1 : &(uint32_t){ 0 }, 4); break; /* ge: NaN -> false */
			case 49: memcpy(&res[q], x[q] < y[q] ? &t1 : &(uint32_t){ 0 }, 4); break;
			case 55: { uint32_t bits; memcpy(&bits, &x[q], 4); res[q] = bits ? y[q] : z[q]; } break;
			default: return 0;
			}
			if (w[i] & 0x2000u)
				res[q] = res[q] < 0 ? 0 : res[q] > 1 ? 1 : res[q] == res[q] ? res[q] : 0;
		}
		for (int q = 0; q < 4; q++)
			if (m & (1u << q))
				d[q] = res[q];
		i += ln;
	}
	return 1;
}

static uint32_t *prog_of(const unsigned char *b, size_t n, uint32_t *nw)
{
	uint32_t sh = gdx_chunk(b, n, "SHEX");
	*nw = fdx_rd(b + sh + 4) / 4;
	uint32_t *w = malloc(*nw * 4);
	for (uint32_t i = 0; i < *nw; i++)
		w[i] = fdx_rd(b + sh + 8 + 4 * i);
	return w;
}

/* ---- the shaders ---------------------------------------------------------------------------------------------- */
#define T1 BDX_T1
static const var g_veg[] = { { "k_mObjectToClip", 0 }, { "k_mObjectToView", 64 }, { "k_vObjectSpaceEyePos", 176 },
			     { "k_fBendability", 368 } };
static const var g_herb[] = { { "k_mObjectToClip", 0 }, { "k_mObjectToView", 64 }, { "k_fBendability", 368 }, { "k_fWraith", 400 } };
static const var g_rock[] = { { "k_mObjectToClip", 0 }, { "k_mObjectToView", 64 } };
static const var g_view[] = { { "k_mWorldToClip", 0 }, { "k_mViewToWorld", 64 }, { "k_vWorldSpaceCameraDir", 416 } };

/* the vertex shader: the bush's (veg_bush_alphatest 6cb5cc83: instructions 1, 12-17, 18, 92-95, 126), r2 = the position */
static uint32_t vs_words[] = {
	0x10050, 0,
	T1(89, 4), 0x00208e46, 0, 0x1d,                       /* dcl_constantbuffer cb0[29] */
	T1(103, 4), 0x001020f2, 0, 1,                         /* dcl_output_siv o0.xyzw, position */
	T1(101, 3), 0x00102072, 1,                            /* dcl_output o1.xyz */
	T1(101, 3), 0x00102072, 2,                            /* o2.xyz */
	T1(101, 3), 0x00102072, 3,                            /* o3.xyz */
	T1(101, 3), 0x00102072, 4,                            /* o4.xyz */
	T1(104, 2), 4,                                        /* dcl_temps 4 */
	T1(17, 8), 0x00102012, 0, 0x00208e46, 0, 0, 0x00100e46, 2,   /* dp4 o0.x, cb0[0].xyzw, r2.xyzw */
	T1(17, 8), 0x00102022, 0, 0x00208e46, 0, 1, 0x00100e46, 2,   /* dp4 o0.y, cb0[1] */
	T1(17, 8), 0x00102042, 0, 0x00208e46, 0, 2, 0x00100e46, 2,   /* dp4 o0.z, cb0[2] */
	T1(17, 8), 0x00102082, 0, 0x00208e46, 0, 3, 0x00100e46, 2,   /* dp4 o0.w, cb0[3] */
	T1(54, 5), 0x00102072, 2, 0x00100246, 3,              /* mov o2.xyz, r3.xyzx (the frame) */
	T1(62, 1) };
static const sel vs_out[] = { { "SV_POSITION", 0, 1, 0, 0xf, 0 }, { "TEXCOORD", 0, 0, 1, 7, 8 }, { "TEXCOORD", 2, 0, 2, 7, 8 },
			      { "TEXCOORD", 3, 0, 3, 7, 8 }, { "TEXCOORD", 4, 0, 4, 7, 8 } };
static const sel vs_in[] = { { "SV_POSITION", 0, 0, 0, 7, 7 } };

/* the pixel shader: the bush's G-buffer one (65a0b009: 7-11 the inputs, 16 temps, 21-22 its alpha test) */
static uint32_t ps_words[] = {
	0x50, 0,
	T1(98, 3) | 2u << 11, 0x00101072, 1,                  /* dcl_input_ps linear v1.xyz */
	T1(98, 3) | 2u << 11, 0x00101072, 2,                  /* v2.xyz */
	T1(98, 3) | 2u << 11, 0x00101072, 3,                  /* v3.xyz */
	T1(98, 3) | 2u << 11, 0x00101072, 4,                  /* v4.xyz */
	T1(101, 3), 0x001020f2, 0,                            /* dcl_output o0.xyzw */
	T1(104, 2), 2,                                        /* dcl_temps 2 */
	T1(54, 5), 0x00100082, 0, 0x0010102a, 1,              /* mov r0.w, v1.z (the game's fade) */
	T1(0, 7), 0x00100082, 0, 0x0010003a, 0, 0x00004001, 0xbec0c0c1, /* add r0.w, r0.w, l(-0.376471) */
	T1(49, 7), 0x00100012, 0, 0x0010003a, 0, 0x00004001, 0, /* lt r0.x, r0.w, l(0) */
	0x0304000d, 0x0010000a, 0,                            /* discard_nz r0.x */
	T1(54, 8), 0x001020f2, 0, 0x00004002, 0x3f800000, 0, 0, 0x3f800000, /* mov o0.xyzw, l(1,0,0,1) */
	T1(62, 1) };
static const sel ps_in[] = { { "SV_POSITION", 0, 1, 0, 0xf, 0 }, { "TEXCOORD", 0, 0, 1, 7, 7 }, { "TEXCOORD", 2, 0, 2, 7, 7 },
			     { "TEXCOORD", 3, 0, 3, 7, 7 }, { "TEXCOORD", 4, 0, 4, 7, 7 } };
static const sel ps_out[] = { { "SV_Target", 0, 0, 0, 0xf, 0 }, { "SV_Target", 1, 0, 1, 0xf, 0 }, { "SV_Target", 2, 0, 2, 0xf, 0 } };

/* the geometry shader: the flies' (gpupart_transvelalignanim eac83fa2: 9, 15, 49, 62-63, 77) */
static uint32_t gs_words[] = {
	0x20050, 0,
	T1(95, 4), 0x00201072, 1, 0,                          /* dcl_input v[1][0].xyz */
	T1(101, 3), 0x001020f2, 2,                            /* dcl_output o2.xyzw */
	T1(104, 2), 7,                                        /* dcl_temps 7 */
	T1(54, 6), 0x00100012, 0, 0x0020803a, 0, 12,          /* mov r0.x, cb0[12].w (k_vObjectColor.w) */
	T1(0, 8), 0x00100072, 4, 0x00100246, 6, 0x00201246, 0, 0,   /* add r4.xyz, r6.xyzx, v0[0].xyzx */
	T1(17, 8), 0x00100012, 5, 0x00208e46, 12, 4, 0x00100e46, 4, /* dp4 r5.x, cb12[4].xyzw, r4.xyzw */
	T1(54, 5), 0x00102082, 2, 0x0010000a, 0,              /* mov o2.w, r0.x */
	T1(62, 1) };
static const sel gs_in[] = { { "WORLDPOS", 0, 0, 0, 7, 7 }, { "TEXCOORD", 0, 0, 1, 7, 7 } };
static const sel gs_out[] = { { "SV_POSITION", 0, 1, 0, 0xf, 0 }, { "TEXCOORD", 0, 0, 1, 3, 0xc }, { "COLOR", 0, 0, 2, 0xf, 0 } };

static unsigned char *make(int which, int variant, size_t *n)
{
	uint32_t p[128];
	if (which == 'V') {
		memcpy(p, vs_words, sizeof vs_words);
		uint32_t nw = (uint32_t)(sizeof vs_words / 4);
		cbuf cb[1] = { { "$Globals", 0, variant == 1 ? g_rock : variant == 2 ? g_herb : g_veg, variant == 1 ? 2 : 4 } };
		sel out[5];
		memcpy(out, vs_out, sizeof out);
		if (variant == 3) /* TEXCOORD3 shares its register with another element */
			out[1].reg = 3;
		if (variant == 4) /* SV_Position by a mov, not the four dp4 */
			p[24] = T1(54, 5), p[25] = 0x00102012, p[26] = 0, p[27] = 0x0010000a, p[28] = 2, p[29] = T1(58, 1), p[30] = T1(58, 1),
			p[31] = T1(58, 1);
		return wrap(p, nw, cb, 1, vs_in, 1, out, 5, 0, n);
	}
	if (which == 'P') {
		memcpy(p, ps_words, sizeof ps_words);
		uint32_t nw = (uint32_t)(sizeof ps_words / 4);
		cbuf cb[1] = { { "$Globals", 0, variant == 2 ? g_herb : g_veg, 4 } };
		if (variant == 1) { /* no alpha test: the discard a nop */
			for (uint32_t i = 0; i < nw; i++)
				if (p[i] == 0x0304000d)
					p[i] = T1(58, 1), p[i + 1] = T1(58, 1), p[i + 2] = T1(58, 1);
		}
		int nout = variant == 3 ? 1 : 3; /* 3: not the G-buffer (one target: a shadow / depth pass) */
		if (variant == 4) { /* SV_Position already declared (.xy): its decl extended, none added */
			uint32_t q[128], j = 0;
			for (uint32_t i = 0; i < nw; i++) {
				if (i == 2)
					q[j++] = T1(100, 4) | 4u << 11, q[j++] = 0x00101032, q[j++] = 0, q[j++] = 1;
				q[j++] = p[i];
			}
			return wrap(q, j, cb, 1, ps_in, 5, ps_out, nout, 0, n);
		}
		return wrap(p, nw, cb, 1, ps_in, 5, ps_out, nout, 0, n);
	}
	memcpy(p, gs_words, sizeof gs_words);
	uint32_t nw = (uint32_t)(sizeof gs_words / 4);
	static const var g_fly[] = { { "k_vParticleScale", 0 }, { "k_mObjectToView", 96 }, { "k_vObjectColor", 192 }, { "k_fAlphaAmount", 396 } };
	static const var g_spark[] = { { "k_vParticleScale", 368 }, { "k_fStreakLength", 388 }, { "k_fHDRScale", 400 } };
	cbuf cb[2] = { { "$Globals", 0, variant == 1 ? g_spark : g_fly, variant == 1 ? 3 : 4 }, { "CBuffer_View", 12, g_view, 3 } };
	if (variant == 2) { /* a second write of COLOR0.w */
		uint32_t q[128], j = 0;
		for (uint32_t i = 0; i < nw; i++) {
			if (i == nw - 1)
				q[j++] = T1(54, 5), q[j++] = 0x00102082, q[j++] = 2, q[j++] = 0x0010001a, q[j++] = 0;
			q[j++] = p[i];
		}
		return wrap(q, j, cb, 2, gs_in, 2, gs_out, 3, 1, n);
	}
	return wrap(p, nw, cb, 2, gs_in, 2, gs_out, 3, 1, n);
}

/* the edited PS run on one fragment: the view position (vx, vy, vz) in TEXCOORD2/3/4.w, the clip w, the uv -> discarded */
static int ps_frag(const uint32_t *w, uint32_t nw, float vx, float vy, float vz, float clip_w, float u, float v)
{
	st s;
	memset(&s, 0, sizeof s);
	s.v[0][3] = clip_w;
	s.v[1][0] = u, s.v[1][1] = v, s.v[1][2] = 1.0f; /* v1.z the game's fade: whole */
	s.v[2][3] = vx, s.v[3][3] = vy, s.v[4][3] = vz;
	CHECK(run(&s, w, nw), "the interpreter knows every opcode");
	return s.discarded;
}

/* the share of 4096 texture cells a fragment at this distance keeps */
static double kept(const uint32_t *w, uint32_t nw, float r, int valid)
{
	int keep = 0;
	for (int i = 0; i < 4096; i++) {
		float u = (float)(i % 64) * 0.0371f + 0.3f, v = (float)(i / 64) * 0.0293f + 0.1f; /* ~100 noise cells each way */
		float vz = r * 0.8f, vx = r * 0.6f; /* 37 degrees off the axis */
		keep += !ps_frag(w, nw, valid ? vx : 0, 0, valid ? vz : 0, vz, u, v); /* !valid: zero .w (a VS without the edit) */
	}
	return keep / 4096.0;
}

int main(int argc, char **argv)
{
	size_t n, m;
	const char *why;
	unsigned char *b, *e;
	uint32_t nw, *w;
	(void)somvr_fogdxbc, (void)somvr_grassdxbc; /* (the shared header's other edits: not tested here) */

	/* ---- VS: the three dp4 after SV_Position's, the outputs' masks ---- */
	b = make('V', 0, &n);
	e = somvr_bubble_vs(b, n, &m, &why);
	CHECK(e && m == n + 96, "VS edited (%s), %zu -> %zu", why ? why : "-", n, m);
	if (e) {
		check_container(e, m, "VS");
		w = prog_of(e, m, &nw);
		const uint32_t want[24] = { T1(17, 8), 0x00102082, 2, 0x00208e46, 0, 4, 0x00100e46, 2,
					    T1(17, 8), 0x00102082, 3, 0x00208e46, 0, 5, 0x00100e46, 2,
					    T1(17, 8), 0x00102082, 4, 0x00208e46, 0, 6, 0x00100e46, 2 };
		CHECK(!memcmp(w + 56, want, sizeof want), "VS: dp4 o2/3/4.w, cb0[4/5/6].xyzw, r2.xyzw right after o0.w's");
		CHECK(w[14] == 0x001020f2 && w[17] == 0x001020f2 && w[20] == 0x001020f2 && w[11] == 0x00102072,
		      "VS: dcl_output o2/3/4 .xyzw, o1 still .xyz");
		uint32_t e2 = bdx_sig(e, m, "OSGN", "TEXCOORD", 3, 0xffffffffu, NULL);
		CHECK(e2 && e[e2 + 20] == 0xf && e[e2 + 21] == 0, "VS: OSGN TEXCOORD3 xyzw, all written");
		/* run it: r2 = (10, 20, 30, 1), k_mObjectToView rows = (1,0,0,5) (0,1,0,6) (0,0,1,7) */
		st s;
		memset(&s, 0, sizeof s);
		s.r[2][0] = 10, s.r[2][1] = 20, s.r[2][2] = 30, s.r[2][3] = 1;
		for (int k = 0; k < 3; k++)
			s.cb[0][4 + k][k] = 1, s.cb[0][4 + k][3] = 5.0f + (float)k;
		CHECK(run(&s, w, nw) && s.o[2][3] == 15 && s.o[3][3] == 26 && s.o[4][3] == 37, "VS run: the view position %g %g %g",
		      (double)s.o[2][3], (double)s.o[3][3], (double)s.o[4][3]);
		free(w), free(e);
	}
	free(b);
	const char *vs_no[] = { "not the foliage's", "not the foliage's", "not a foliage G-buffer vertex shader", "SV_Position not by four dp4" };
	for (int v = 1; v <= 4; v++) {
		b = make('V', v, &n);
		e = somvr_bubble_vs(b, n, &m, &why);
		CHECK(!e && why && !strncmp(why, vs_no[v - 1], strlen(vs_no[v - 1])), "VS variant %d refused (%s)", v, why ? why : "edited");
		free(e), free(b);
	}

	/* ---- PS: the dissolve; SV_Position declared; run per fragment ---- */
	for (int v = 0; v <= 4; v += 4) {
		b = make('P', v, &n);
		e = somvr_bubble_ps(b, n, 20, 35, &m, &why);
		CHECK(e && m == n + 4 * (208 + (v ? 0 : 4)), "PS %d edited (%s), %zu -> %zu", v, why ? why : "-", n, m);
		if (!e) {
			free(b);
			continue;
		}
		check_container(e, m, "PS");
		w = prog_of(e, m, &nw);
		if (!v)
			CHECK(w[2] == (T1(100, 4) | 4u << 11) && w[3] == 0x001010f2 && w[4] == 0 && w[5] == 1,
			      "PS: dcl_input_ps_siv linear noperspective v0.xyzw, position first");
		else
			CHECK(w[2] == (T1(100, 4) | 4u << 11) && w[3] == 0x001010b2, "PS: the existing SV_Position decl .xyw");
		uint32_t at = 2 + 4; /* after SV_Position's decl (added, or the existing one) */
		CHECK(w[at + 3 * 3 + 1] == 0x001010f2 && w[at + 3 * 1 + 1] == 0x001010f2 && w[at + 1] == 0x00101072,
		      "PS: v2/3/4 .xyzw, v1 .xyz");
		CHECK(w[at + 12 + 3 + 1] == 4, "PS: dcl_temps 2 -> 4");
		uint32_t pe = bdx_sig(e, m, "ISGN", NULL, 0, 1, NULL), e4 = bdx_sig(e, m, "ISGN", "TEXCOORD", 4, 0xffffffffu, NULL);
		CHECK(pe && e[pe + 21] == 8 && e4 && e[e4 + 20] == 0xf && e[e4 + 21] == 0xf, "PS: ISGN SV_Position w read, TEXCOORD4 xyzw");
		/* the band: gone inside 20 cm, whole from 35, by the radial distance (37 degrees off the axis) */
		double k10 = kept(w, nw, 10, 1), k19 = kept(w, nw, 19.5f, 1), k23 = kept(w, nw, 23, 1), k27 = kept(w, nw, 27.5f, 1),
		       k31 = kept(w, nw, 31, 1), k36 = kept(w, nw, 36, 1), k200 = kept(w, nw, 200, 1);
		CHECK(k10 == 0 && k19 == 0, "PS: nothing kept inside 20 cm (%.3f %.3f)", k10, k19);
		CHECK(k36 == 1 && k200 == 1, "PS: all kept from 35 cm (%.3f %.3f)", k36, k200);
		CHECK(k23 < 0.3 && fabs(k27 - 0.5) < 0.15 && k31 > 0.7 && k31 < 1 && k23 < k27 && k27 < k31,
		      "PS: the band grows - 23 cm %.3f, 27.5 cm %.3f (about half), 31 cm %.3f", k23, k27, k31);
		if (!v)
			printf("PS band (kept): 10 cm %.2f, 19.5 %.2f, 23 %.2f, 27.5 %.2f, 31 %.2f, 36 %.2f, 200 %.2f\n", k10, k19, k23, k27,
			       k31, k36, k200);
		/* the fallback: a vertex shader without the edit (zero .w) -> the clip w (the depth), not the zero */
		double f_near = kept(w, nw, 22, 0), f_far = kept(w, nw, 60, 0);
		CHECK(f_far == 1 && f_near < 0.5, "PS: without the VS edit, the depth decides (60 cm %.3f, 22 cm %.3f)", f_far, f_near);
		/* stereo: two eyes see one cell at one distance along different rays -> one verdict (the hash takes the uv only) */
		int same = 1;
		for (int c = 0; c < 256; c++) {
			float u = (float)c / 256.0f, r = 27.0f, a = 0.3f;
			same &= ps_frag(w, nw, 0, 0, r, r, u, 0.7f) == ps_frag(w, nw, r * sinf(a), 0, r * cosf(a), r * cosf(a), u, 0.7f);
		}
		CHECK(same, "PS: the dissolve is fixed to the surface (the same cells kept along two eyes' rays)");
		/* the game's own alpha test still runs: its fade 0 discards at any distance */
		st s;
		memset(&s, 0, sizeof s);
		s.v[0][3] = 500, s.v[4][3] = 500, s.v[1][2] = 0.0f;
		CHECK(run(&s, w, nw) && s.discarded, "PS: the game's alpha test still discards");
		free(w), free(e), free(b);
	}
	const char *ps_no[] = { "", "no alpha test", "not the foliage's", "not a foliage G-buffer pixel shader" };
	for (int v = 1; v <= 3; v++) {
		b = make('P', v, &n);
		e = somvr_bubble_ps(b, n, 20, 35, &m, &why);
		CHECK(!e && why && !strncmp(why, ps_no[v], strlen(ps_no[v])), "PS variant %d refused (%s)", v, why ? why : "edited");
		free(e), free(b);
	}

	/* ---- GS: the fade on COLOR0.w ---- */
	b = make('G', 0, &n);
	e = somvr_bubble_gs(b, n, 75, 100, &m, &why);
	CHECK(e && m == n + 4 * 25, "GS edited (%s), %zu -> %zu", why ? why : "-", n, m);
	if (e) {
		check_container(e, m, "GS");
		w = prog_of(e, m, &nw);
		CHECK(w[10] == 8, "GS: dcl_temps 8");
		for (int t = 0; t < 6; t++) {
			const float d[6] = { 10, 74, 80, 87.5f, 99, 150 }, want[6] = { 0, 0, 0.2f, 0.5f, 0.96f, 1 };
			st s;
			memset(&s, 0, sizeof s);
			s.gv[0][0] = d[t] * 0.6f, s.gv[0][2] = d[t] * 0.8f; /* the view-space centre */
			s.cb[0][12][3] = 0.9f;                            /* k_vObjectColor.w: the particle's own alpha */
			CHECK(run(&s, w, nw) && fabsf(s.o[2][3] - 0.9f * want[t]) < 1e-3f, "GS run: at %g cm alpha %g (want %g)", (double)d[t],
			      (double)s.o[2][3], (double)(0.9f * want[t]));
		}
		free(w), free(e);
	}
	free(b);
	const char *gs_no[] = { "", "not a translucent GPU particle", "COLOR0.w not written by one mov" };
	for (int v = 1; v <= 2; v++) {
		b = make('G', v, &n);
		e = somvr_bubble_gs(b, n, 75, 100, &m, &why);
		CHECK(!e && why && !strncmp(why, gs_no[v], strlen(gs_no[v])), "GS variant %d refused (%s)", v, why ? why : "edited");
		free(e), free(b);
	}
	/* the kinds cross-checked: a VS edit refuses a PS, ... */
	b = make('P', 0, &n);
	e = somvr_bubble_vs(b, n, &m, &why);
	CHECK(!e && why && !strncmp(why, "not ", 4), "a PS is not a VS's (%s)", why ? why : "edited");
	free(e);
	e = somvr_bubble_gs(b, n, 75, 100, &m, &why);
	CHECK(!e && why && !strncmp(why, "not ", 4), "a PS is not a GS's (%s)", why ? why : "edited");
	free(e), free(b);

	/* ---- the real blobs, when given ---- */
	for (int a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb");
		CHECK(f, "%s: unreadable", argv[a]);
		if (!f)
			continue;
		fseek(f, 0, SEEK_END);
		long fn = ftell(f);
		fseek(f, 0, SEEK_SET);
		unsigned char *d = malloc((size_t)fn);
		CHECK(fread(d, 1, (size_t)fn, f) == (size_t)fn, "%s: read", argv[a]);
		fclose(f);
		for (long o = 0; o + 32 <= fn; o++) {
			uint32_t sz = fdx_rd(d + o + 24);
			if (memcmp(d + o, "DXBC", 4) || fdx_rd(d + o + 20) != 1 || sz < 32 || o + (long)sz > fn)
				continue;
			const unsigned char *bl = d + o;
			uint32_t sh = gdx_chunk(bl, sz, "SHEX"), kind = sh ? fdx_rd(bl + sh + 8) >> 16 : 9;
			e = kind == 1 ? somvr_bubble_vs(bl, sz, &m, &why) : kind == 0 ? somvr_bubble_ps(bl, sz, 20, 35, &m, &why)
			  : kind == 2 ? somvr_bubble_gs(bl, sz, 75, 100, &m, &why) : NULL;
			if (e) {
				check_container(e, m, argv[a]);
				printf("%s: %s %02x%02x%02x%02x %u -> %zu\n", strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a],
				       kind == 0 ? "PS" : kind == 1 ? "VS" : "GS", bl[4], bl[5], bl[6], bl[7], sz, m);
			} else if (why && strncmp(why, "not ", 4))
				printf("%s: %s %02x%02x%02x%02x stays: %s\n", argv[a], kind == 0 ? "PS" : kind == 1 ? "VS" : "GS", bl[4], bl[5],
				       bl[6], bl[7], why);
			free(e);
			o += sz - 1;
		}
		free(d);
	}
	printf("test_bubbledxbc: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
	return fails != 0;
}
