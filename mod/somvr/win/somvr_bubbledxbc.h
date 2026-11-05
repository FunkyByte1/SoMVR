/* somvr_bubbledxbc.h - nothing in the player's face, as DXBC edits (docs/re-xr-pipeline.md 166, SOMVR_NEAR_FOLIAGE_CM,
 * SOMVR_NEAR_BUGS_CM). Header-only; the Windows DLL's ID3D11Device::CreateVertexShader (COM slot 12) /
 * CreateGeometryShader (13) / CreatePixelShader (15) hooks and tests/test_bubbledxbc.c (native, Linux) include it. Needs
 * somvr_grassdxbc.h (gdx_chunk, gdx_rdef_var, gdx_name_is) and somvr_fogdxbc.h (fdx_*, the checksum).
 *
 * FOLIAGE (the game's vegetation: veg_bush_alphatest, veg_bushclump_alphatest(_prlx), veg_billboard_alphatest,
 * veg_treealphatest, scattergrass_parallaxfixed, scattergrassclump - every shader whose $Globals holds k_fBendability,
 * except the herbs' k_fWraith family): each fragment within IN cm of the eye is dissolved, fading back to whole at OUT
 * cm, by the distance from the eye that draws it (each eye its own pass, its own view):
 *   VS  the three outputs TEXCOORD2/3/4 carry .xyz only (the tangent frame); their free .w gets the vertex's view-space
 *       position (k_mObjectToView x the position the shader projects with k_mObjectToClip), three dp4 after the four
 *       that write SV_Position; dcl_output and OSGN masks xyz -> xyzw.
 *   PS  reads the interpolated view-space position (exact: it is linear in the triangle) from those .w, its length =
 *       the fragment's distance from the eye. Checked against SV_Position.w (the clip w = the view depth): when |z|
 *       differs from w by more than 2 % + 2 cm (a vertex shader without the edit: undefined / zero .w) the depth w is
 *       taken instead. s = saturate((r - IN) / (OUT - IN)); the fragment is discarded when a smooth value noise of its
 *       texture position (TEXCOORD0.xy x 48, the corners hashed by frac(sin(.) x 43758.5); in [0, 1)) >= s: holes that
 *       open as round blobs and grow, fixed to the leaf's surface, the same in both eyes, none beyond OUT. Inserted
 *       before the first instruction (+2 temps, the inputs' masks, SV_Position declared if it was not). Only G-buffer shaders (SV_Target2) with an alpha test (a discard) - not the shadow pass.
 * BUGS (the game's translucent GPU particles: gpupart_transvelalignanim(hdr) = flies, moths, mosquitoes, fireflies;
 * gpupart_translucentstreak = fly streaks; gpupart_additive = the floating dust motes - not the additive streaks =
 * sparks and embers): the geometry shader takes the particle's centre in view space (WORLDPOS0 = k_mObjectToView x the
 * particle) and sets COLOR0.w (the alpha both pixel shaders multiply by); the edit multiplies it by
 * saturate((|centre| - IN) / (OUT - IN)): no particle inside IN cm of the eye, whole from OUT.
 * Matched by the reflection's names and by structure, every link checked before anything is written; a blob that is not
 * one of these (or not as expected) is left alone with *why set ("not ..." = not ours at all). The container is rebuilt
 * (the program's length, its chunk, the later chunks' offsets, the total size, the edited signature bytes, the
 * checksum). */
#ifndef SOMVR_BUBBLEDXBC_H
#define SOMVR_BUBBLEDXBC_H
#include "somvr_grassdxbc.h"

#define BDX_T1(op, n) ((uint32_t)(op) | (uint32_t)(n) << 24)
#define BDX_R_MASK(m) (0x00100002u | (uint32_t)(m) << 4)  /* rN.<mask> (a destination) */
#define BDX_R_SWZ(s) (0x00100006u | (uint32_t)(s) << 4)   /* rN.<swizzle> */
#define BDX_R_SEL(c) (0x0010000au | (uint32_t)(c) << 4)   /* rN.c */
#define BDX_V_SWZ(s) (0x00101006u | (uint32_t)(s) << 4)   /* vN.<swizzle> */
#define BDX_V_SEL(c) (0x0010100au | (uint32_t)(c) << 4)   /* vN.c */
#define BDX_O_MASK(m) (0x00102002u | (uint32_t)(m) << 4)  /* oN.<mask> */
#define BDX_ABS 0x80000000u, 0x81u                        /* (after the token's |) the abs modifier's extended token */
#define BDX_L1 0x00004001u                                /* l(a) */
#define BDX_L4 0x00004002u                                /* l(a, b, c, d) */
#define BDX_XYZX 0x24u
#define BDX_XYXX 0x04u

static uint32_t bdx_f(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

/* a signature element sem<idx> in the chunk tag (ISGN/OSGN: 24-byte elements; OSG5: 28, the stream first): its offset
 * in the blob (the name's word; +16 the register, +20 the mask byte, +21 the read/write byte), 0 = none. *nreg = how
 * many elements of that chunk share its register (packing) */
static uint32_t bdx_sig(const unsigned char *b, size_t n, const char *tag, const char *sem, uint32_t idx, uint32_t sv,
			uint32_t *nreg)
{
	uint32_t c = gdx_chunk(b, n, tag), hit = 0;
	if (!c)
		return 0;
	uint32_t len = fdx_rd(b + c + 4), cnt = fdx_rd(b + c + 8), esz = tag[3] == '5' ? 28 : 24, sk = esz == 28 ? 4 : 0;
	const unsigned char *d = b + c + 8;
	if (len < 8 || 8 + (uint64_t)esz * cnt > len)
		return 0;
	for (uint32_t i = 0; i < cnt && !hit; i++) {
		const unsigned char *e = d + 8 + esz * i + sk;
		if ((sem ? gdx_name_is(d, len, fdx_rd(e), sem) && fdx_rd(e + 4) == idx : 1) && (sv == 0xffffffffu || fdx_rd(e + 8) == sv))
			hit = (uint32_t)(e - b);
	}
	if (hit && nreg) {
		*nreg = 0;
		for (uint32_t i = 0; i < cnt; i++)
			*nreg += fdx_rd(d + 8 + esz * i + sk + 16) == fdx_rd(b + hit + 16);
	}
	return hit;
}

/* the container checked as somvr_grassdxbc does; the program's words copied out (malloc'ed) -> 1 */
static int bdx_load(const unsigned char *blob, size_t n, uint32_t **w, uint32_t *nw, uint32_t *shex, uint32_t *kind,
		    const char **why)
{
	*why = "not DXBC";
	if (n < 32 || memcmp(blob, "DXBC", 4) || fdx_rd(blob + 20) != 1 || fdx_rd(blob + 24) != n)
		return 0;
	uint32_t nch = fdx_rd(blob + 28);
	*why = "not a sound container (its chunk table)";
	if (!nch || nch > 32 || 32 + 4 * (size_t)nch > n)
		return 0;
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t o = fdx_rd(blob + 32 + 4 * c);
		if ((size_t)o + 8 > n || (size_t)o + 8 + fdx_rd(blob + o + 4) > n)
			return 0;
	}
	*shex = gdx_chunk(blob, n, "SHEX");
	if (!*shex)
		*shex = gdx_chunk(blob, n, "SHDR");
	*why = "not a program (no SHEX/SHDR chunk)";
	if (!*shex)
		return 0;
	uint32_t clen = fdx_rd(blob + *shex + 4);
	const unsigned char *pb = blob + *shex + 8;
	*nw = clen / 4;
	*why = "a broken program header";
	if (*nw < 2 || fdx_rd(pb + 4) != *nw || clen % 4)
		return 0;
	*kind = fdx_rd(pb) >> 16; /* 0 pixel, 1 vertex, 2 geometry */
	*w = malloc(*nw * sizeof **w);
	*why = "no memory";
	if (!*w)
		return 0;
	for (uint32_t i = 0; i < *nw; i++)
		(*w)[i] = fdx_rd(pb + 4 * i);
	return 1;
}

/* a declaration (or the icb's customdata among them): the code goes after them */
static int bdx_is_dcl(uint32_t op)
{
	return op == 53 || (op >= 88 && op <= 106) || (op >= 143 && op <= 162) || op == 206;
}

/* the new container: the program o[mw] in place of the old nw words; npat signature bytes (blob offsets, new values) */
static unsigned char *bdx_build(const unsigned char *blob, size_t n, uint32_t shex, uint32_t nw, const uint32_t *o, uint32_t mw,
				const uint32_t *pat_at, const unsigned char *pat_v, int npat, size_t *n_out)
{
	int32_t add = (int32_t)(mw - nw) * 4;
	size_t m = n + (size_t)(int64_t)add, at = shex + 8;
	unsigned char *b = malloc(m);
	if (!b)
		return NULL;
	memcpy(b, blob, at);
	for (uint32_t i = 0; i < mw; i++)
		fdx_wr(b + at + 4 * i, o[i]);
	fdx_wr(b + at + 4, mw);
	memcpy(b + at + 4 * (size_t)mw, blob + at + 4 * (size_t)nw, n - at - 4 * (size_t)nw);
	fdx_wr(b + shex + 4, fdx_rd(blob + shex + 4) + (uint32_t)add);
	fdx_wr(b + 24, (uint32_t)m);
	uint32_t nch = fdx_rd(blob + 28);
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t co = fdx_rd(blob + 32 + 4 * c);
		if (co > shex)
			fdx_wr(b + 32 + 4 * c, co + (uint32_t)add);
	}
	for (int k = 0; k < npat; k++)
		b[pat_at[k] < at ? pat_at[k] : pat_at[k] + (uint32_t)add] = pat_v[k];
	somvr_dxbc_checksum(b, m, b + 4);
	*n_out = m;
	return b;
}

/* the foliage family by the reflection: $Globals holds k_fBendability, not k_fWraith (the herbs: they glow to be found) */
static int bdx_foliage(const unsigned char *blob, size_t n)
{
	uint32_t r, c;
	return gdx_rdef_var(blob, n, "$Globals", "k_fBendability", &r, &c, NULL) &&
	       !gdx_rdef_var(blob, n, "$Globals", "k_fWraith", &r, &c, NULL);
}

/* TEXCOORD2/3/4 in the chunk tag, each .xyz alone on its register: their element offsets -> 1 */
static int bdx_frame3(const unsigned char *blob, size_t n, const char *tag, uint32_t e[3])
{
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t nr = 0;
		e[k] = bdx_sig(blob, n, tag, "TEXCOORD", 2 + k, 0xffffffffu, &nr);
		if (!e[k] || nr != 1 || blob[e[k] + 20] != 7)
			return 0;
	}
	return fdx_rd(blob + e[0] + 16) != fdx_rd(blob + e[1] + 16) && fdx_rd(blob + e[0] + 16) != fdx_rd(blob + e[2] + 16) &&
	       fdx_rd(blob + e[1] + 16) != fdx_rd(blob + e[2] + 16);
}

/* FOLIAGE, the vertex shader: TEXCOORD2/3/4.w = the vertex's view-space position. NULL + *why when not edited */
/* shape 0 (sphere): the view-space position; 1 (cylinder, build 40): the offset from the eye in the world's axes
 * (k_mObjectToWorld's rotation x (the position - k_vObjectSpaceEyePos), the same object space as the clip matrix's) -
 * its .y is the world's up, so the pixel shader can measure the distance from the eye's vertical line */
static unsigned char *somvr_bubble_vs2(const unsigned char *blob, size_t n, int shape, size_t *n_out, const char **why)
{
	uint32_t *w = NULL, nw, shex, kind, rc, cc, rv, cv, gs, e[3], reg[3], pos_e, pos_r, rw = 0, cw = 0, re = 0, ce = 0;
	*n_out = 0;
	if (!bdx_load(blob, n, &w, &nw, &shex, &kind, why))
		return NULL;
	unsigned char *b = NULL;
	*why = "not a vertex shader";
	if (kind != 1)
		goto out;
	*why = "not the foliage's (no k_fBendability in $Globals, or the herbs' k_fWraith)";
	if (!bdx_foliage(blob, n))
		goto out;
	*why = "not a foliage G-buffer vertex shader (no TEXCOORD2/3/4 .xyz alone on their registers)";
	if (!bdx_frame3(blob, n, "OSGN", e))
		goto out;
	*why = "no k_mObjectToClip / k_mObjectToView in $Globals";
	if (!gdx_rdef_var(blob, n, "$Globals", "k_mObjectToClip", &rc, &cc, &gs) || cc ||
	    !gdx_rdef_var(blob, n, "$Globals", "k_mObjectToView", &rv, &cv, NULL) || cv)
		goto out;
	*why = "no k_mObjectToWorld / k_vObjectSpaceEyePos in $Globals (the cylinder)";
	if (shape && (!gdx_rdef_var(blob, n, "$Globals", "k_mObjectToWorld", &rw, &cw, NULL) || cw ||
		      !gdx_rdef_var(blob, n, "$Globals", "k_vObjectSpaceEyePos", &re, &ce, NULL) || ce))
		goto out;
	pos_e = bdx_sig(blob, n, "OSGN", NULL, 0, 1 /* SV_Position */, NULL);
	*why = "no SV_Position output";
	if (!pos_e)
		goto out;
	pos_r = fdx_rd(blob + pos_e + 16);
	for (int k = 0; k < 3; k++)
		reg[k] = fdx_rd(blob + e[k] + 16);
	/* the declarations of the three outputs; the four dp4 that write SV_Position */
	uint32_t dcl_at[3] = { 0, 0, 0 }, ndcl[3] = { 0, 0, 0 }, dp_at[4] = { 0, 0, 0, 0 }, ndp[4] = { 0, 0, 0, 0 }, P = 0, P_ok = 1;
	uint32_t nposw = 0, cbdcl_at = 0, ncbdcl = 0, temps_at = 0, ntemps = 0;
	*why = "a broken instruction stream";
	for (uint32_t i = 2; i < nw;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff;
		if (!ln)
			goto out;
		if (op == 89 /* dcl_constantbuffer */ && ln == 4 && (w[i + 1] & 0xfffff00fu) == 0x00208006u && w[i + 2] == gs)
			cbdcl_at = i, ncbdcl++;
		if (op == 104 /* dcl_temps */ && ln == 2)
			temps_at = i, ntemps = w[i + 1];
		if (op == 101 /* dcl_output */ && ln == 3)
			for (int k = 0; k < 3; k++)
				if (w[i + 2] == reg[k] && w[i + 1] == BDX_O_MASK(7))
					dcl_at[k] = i, ndcl[k]++;
		if (op != 53 && !bdx_is_dcl(op) && ln >= 3 && (w[i + 1] & 0xfffff00fu) == 0x00102002u && w[i + 2] == pos_r) {
			nposw++; /* a write of SV_Position */
			uint32_t m = (w[i + 1] >> 4) & 0xf;
			if (op == 17 && ln == 8 && !(w[i] & 0x80003800u) && (m == 1 || m == 2 || m == 4 || m == 8) &&
			    w[i + 3] == 0x00208e46u && w[i + 4] == gs && w[i + 6] == 0x00100e46u) {
				uint32_t c = (uint32_t)__builtin_ctz(m);
				if (w[i + 5] == rc + c) {
					dp_at[c] = i, ndp[c]++;
					if (c == 0)
						P = w[i + 7];
				}
			}
		}
		i += ln;
	}
	for (int c = 0; c < 4; c++)
		P_ok &= ndp[c] == 1 && w[dp_at[c] + 7] == P;
	*why = "not the three TEXCOORD2/3/4 dcl_output .xyz";
	if (ndcl[0] != 1 || ndcl[1] != 1 || ndcl[2] != 1)
		goto out;
	*why = "SV_Position not by four dp4 o.c, cb[k_mObjectToClip + c].xyzw, rP.xyzw (one each, the same rP, nothing else writing it)";
	if (!P_ok || nposw != 4)
		goto out;
	/* consecutive: rP unchanged among them; the insertion goes after the last */
	uint32_t lo = dp_at[0], hi = dp_at[0];
	for (int c = 1; c < 4; c++)
		lo = dp_at[c] < lo ? dp_at[c] : lo, hi = dp_at[c] > hi ? dp_at[c] : hi;
	*why = "the four dp4 of SV_Position are not consecutive";
	if (hi - lo != 3 * 8)
		goto out;
	uint32_t after = hi + 8, mw = nw + (shape ? 33 : 24), j = 0, X = ntemps;
	*why = "no ret after SV_Position / the $Globals buffer not declared once";
	if (after >= nw || ncbdcl != 1)
		goto out;
	*why = "no dcl_temps (the cylinder's offset needs a temp)";
	if (shape && (!temps_at || ntemps > 4094))
		goto out;
	uint32_t need = shape ? (rw + 3 > re + 1 ? rw + 3 : re + 1) : rv + 3; /* the registers the buffer must declare */
	uint32_t *o = malloc(mw * sizeof *o);
	*why = "no memory";
	if (!o)
		goto out;
	for (uint32_t i = 0; i < nw;) {
		uint32_t ln = i < 2 ? 1 : fdx_len(w, i, nw);
		if (i == after && !shape)
			for (uint32_t k = 0; k < 3; k++) { /* dp4 o<reg k>.w, cb<gs>[k_mObjectToView + k].xyzw, rP.xyzw */
				const uint32_t ins[8] = { BDX_T1(17, 8), BDX_O_MASK(8), reg[k], 0x00208e46u, gs, rv + k, 0x00100e46u, P };
				memcpy(o + j, ins, sizeof ins), j += 8;
			}
		if (i == after && shape) {
			/* add rX.xyz, rP.xyzx, -cb<gs>[k_vObjectSpaceEyePos].xyzx: the eye to the vertex, object space */
			const uint32_t add[9] = { BDX_T1(0, 9), BDX_R_MASK(7), X, BDX_R_SWZ(BDX_XYZX), P, 0x00208246u | 0x80000000u, 0x41u, gs, re };
			memcpy(o + j, add, sizeof add), j += 9;
			for (uint32_t k = 0; k < 3; k++) { /* dp3 o<reg k>.w, cb<gs>[k_mObjectToWorld + k].xyzx, rX.xyzx: world x, y, z */
				const uint32_t ins[8] = { BDX_T1(16, 8), BDX_O_MASK(8), reg[k], 0x00208246u, gs, rw + k, BDX_R_SWZ(BDX_XYZX), X };
				memcpy(o + j, ins, sizeof ins), j += 8;
			}
		}
		memcpy(o + j, w + i, ln * 4);
		for (int k = 0; k < 3; k++)
			if (i == dcl_at[k])
				o[j + 1] = BDX_O_MASK(0xf);
		if (i == cbdcl_at && o[j + 3] < need)
			o[j + 3] = need; /* the declared size covers what the edit reads */
		if (shape && i == temps_at)
			o[j + 1] = ntemps + 1;
		j += ln, i += ln;
	}
	*why = "the new program's length (a bug)";
	if (j != mw) {
		free(o);
		goto out;
	}
	/* the OSGN: TEXCOORD2/3/4 mask xyzw, nothing never-written */
	uint32_t pat_at[6];
	unsigned char pat_v[6];
	for (int k = 0; k < 3; k++)
		pat_at[2 * k] = e[k] + 20, pat_v[2 * k] = 0xf, pat_at[2 * k + 1] = e[k] + 21, pat_v[2 * k + 1] = 0;
	b = bdx_build(blob, n, shex, nw, o, mw, pat_at, pat_v, 6, n_out);
	free(o);
	*why = b ? NULL : "no memory";
out:
	free(w);
	return b;
}

static unsigned char *somvr_bubble_vs(const unsigned char *blob, size_t n, size_t *n_out, const char **why)
{
	return somvr_bubble_vs2(blob, n, 0, n_out, why);
}

/* FOLIAGE, the pixel shader: the dissolve within in_cm..out_cm of the eye. NULL + *why when not edited */
/* shape 0 (sphere): r = the fragment's distance from the eye. shape 1 (cylinder, build 40): r = its distance from the
 * eye's vertical line (the VS's world offset: x, z), and only from below_cm under the eye to above_cm over it (20 cm
 * fades back to whole beyond either) - the grass round the feet and the leaves at the face alike, not the canopy */
static unsigned char *somvr_bubble_ps2(const unsigned char *blob, size_t n, float in_cm, float out_cm, int shape,
				       float below_cm, float above_cm, size_t *n_out, const char **why)
{
	uint32_t *w = NULL, nw, shex, kind, e[3], reg[3];
	*n_out = 0;
	if (!bdx_load(blob, n, &w, &nw, &shex, &kind, why))
		return NULL;
	unsigned char *b = NULL;
	*why = "not a pixel shader";
	if (kind != 0)
		goto out;
	*why = "not the foliage's (no k_fBendability in $Globals, or the herbs' k_fWraith)";
	if (!bdx_foliage(blob, n))
		goto out;
	*why = "not a foliage G-buffer pixel shader (no SV_Target2)";
	if (!bdx_sig(blob, n, "OSGN", "SV_Target", 2, 0xffffffffu, NULL))
		goto out;
	*why = "no TEXCOORD2/3/4 .xyz alone on their registers among the inputs";
	if (!bdx_frame3(blob, n, "ISGN", e))
		goto out;
	uint32_t pe = bdx_sig(blob, n, "ISGN", NULL, 0, 1 /* SV_Position */, NULL), np = 0;
	uint32_t ue = bdx_sig(blob, n, "ISGN", "TEXCOORD", 0, 0xffffffffu, NULL);
	*why = "no SV_Position .xyzw / TEXCOORD0 .xy input";
	if (!pe || blob[pe + 20] != 0xf || !ue || (blob[ue + 20] & 3) != 3 ||
	    !bdx_sig(blob, n, "ISGN", NULL, 0, 1, &np) || np != 1)
		goto out;
	uint32_t p = fdx_rd(blob + pe + 16), u = fdx_rd(blob + ue + 16);
	for (int k = 0; k < 3; k++)
		reg[k] = fdx_rd(blob + e[k] + 16);
	uint32_t temps_at = 0, ntemps = 0, dcl_at[3] = { 0, 0, 0 }, ndcl[3] = { 0, 0, 0 }, pos_at = 0, npos = 0, u_ok = 0;
	uint32_t first_in = 0, code_at = 0, ndisc = 0;
	*why = "a broken instruction stream";
	for (uint32_t i = 2; i < nw;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff;
		if (!ln)
			goto out;
		if (!code_at && !bdx_is_dcl(op))
			code_at = i;
		if (op == 104 /* dcl_temps */ && ln == 2)
			temps_at = i, ntemps = w[i + 1];
		else if (op >= 98 && op <= 100 /* dcl_input_ps(_sgv/_siv) */ && ln >= 3 && (w[i + 1] & 0xffffff0fu) == 0x00101002u) {
			if (!first_in)
				first_in = i;
			for (int k = 0; k < 3; k++)
				if (op == 98 && w[i + 2] == reg[k] && w[i + 1] == 0x00101072u)
					dcl_at[k] = i, ndcl[k]++;
			if (w[i + 2] == p)
				pos_at = i, npos++;
			if (op == 98 && w[i + 2] == u && ((w[i + 1] >> 4) & 3) == 3)
				u_ok = 1;
		} else if (op == 13 /* discard */)
			ndisc++;
		i += ln;
	}
	*why = "no alpha test (no discard): not foliage cards";
	if (!ndisc)
		goto out;
	*why = "not the three TEXCOORD2/3/4 dcl_input_ps linear .xyz, or TEXCOORD0.xy not read";
	if (ndcl[0] != 1 || ndcl[1] != 1 || ndcl[2] != 1 || !u_ok || !first_in)
		goto out;
	*why = "SV_Position declared in an unexpected way";
	if (npos > 1 || (npos && (w[pos_at] & 0x7ff) != 100))
		goto out;
	*why = "no dcl_temps / no code";
	if (!temps_at || !code_at || ntemps > 4094)
		goto out;
	const float k = 1.0f / (out_cm - in_cm), m = -in_cm / (out_cm - in_cm);
	const uint32_t T = ntemps, U = ntemps + 1, NUL = 0x0000d000u;
	const float F = 48.0f, A = 12.9898f, B = 78.233f; /* the noise: cells per uv; the corners' hash */
	const uint32_t code[] = {
		/* n = smooth value noise of the texture position (TEXCOORD0.xy x F): fixed to the leaf, the same in both eyes */
		BDX_T1(56, 10), BDX_R_MASK(3), T, BDX_V_SWZ(BDX_XYXX), u, BDX_L4, bdx_f(F), bdx_f(F), 0, 0, /* mul rT.xy, vU.xyxx, l(F,F,0,0) */
		BDX_T1(65, 5), BDX_R_MASK(3), U, BDX_R_SWZ(BDX_XYXX), T,                                    /* round_ni rU.xy, rT.xyxx (the cell) */
		BDX_T1(0, 8), BDX_R_MASK(3), T, BDX_R_SWZ(BDX_XYXX), T, BDX_R_SWZ(BDX_XYXX) | 0x80000000u, 0x41u, U, /* add rT.xy, rT, -rU (f) */
		BDX_T1(50, 15), BDX_R_MASK(0xc), T, BDX_R_SWZ(0x40), T, BDX_L4, 0, 0, bdx_f(-2.0f), bdx_f(-2.0f), BDX_L4, 0, 0,
		bdx_f(3.0f), bdx_f(3.0f),                                                                   /* mad rT.zw, rT.xxxy, -2, 3 */
		BDX_T1(56, 7), BDX_R_MASK(0xc), T, BDX_R_SWZ(0xea), T, BDX_R_SWZ(0x40), T,                 /* mul rT.zw, rT.zzzw, rT.xxxy */
		BDX_T1(56, 7), BDX_R_MASK(3), T, BDX_R_SWZ(0xae), T, BDX_R_SWZ(BDX_XYXX), T,               /* mul rT.xy, rT.zwzz, rT.xyxx (u) */
		BDX_T1(15, 10), BDX_R_MASK(1), U, BDX_R_SWZ(BDX_XYXX), U, BDX_L4, bdx_f(A), bdx_f(B), 0, 0, /* dp2 rU.x, rU.xyxx, (A, B) */
		BDX_T1(0, 10), BDX_R_MASK(0xf), U, BDX_R_SWZ(0x00), U, BDX_L4, 0, bdx_f(A), bdx_f(B), bdx_f(A + B), /* the 4 corners */
		0x0600004du, BDX_R_MASK(0xf), U, NUL, BDX_R_SWZ(0xe4), U,                                  /* sincos rU.xyzw, null, rU.xyzw */
		BDX_T1(56, 10), BDX_R_MASK(0xf), U, BDX_R_SWZ(0xe4), U, BDX_L4, bdx_f(43758.5453f), bdx_f(43758.5453f),
		bdx_f(43758.5453f), bdx_f(43758.5453f),                                                    /* mul rU, rU, l(43758.5453) */
		BDX_T1(26, 5), BDX_R_MASK(0xf), U, BDX_R_SWZ(0xe4), U,                                     /* frc rU.xyzw (h00 h10 h01 h11) */
		BDX_T1(0, 8), BDX_R_MASK(0xa), U, BDX_R_SWZ(0xd5), U, BDX_R_SWZ(0x80) | 0x80000000u, 0x41u, U, /* add rU.yw, rU.yyyw, -rU.xxxz */
		BDX_T1(50, 9), BDX_R_MASK(5), U, BDX_R_SWZ(0xf5), U, BDX_R_SWZ(0x00), T, BDX_R_SWZ(0x20), U, /* mad rU.xz, rU.yyww, rT.xxxx, rU.xxzx */
		BDX_T1(0, 8), BDX_R_MASK(4), U, BDX_R_SEL(2), U, BDX_R_SEL(0) | 0x80000000u, 0x41u, U,     /* add rU.z, rU.z, -rU.x */
		BDX_T1(50, 9), BDX_R_MASK(1), U, BDX_R_SEL(2), U, BDX_R_SEL(1), T, BDX_R_SEL(0), U,        /* mad rU.x, rU.z, rT.y, rU.x (n) */
		};
	const uint32_t sph[] = {
		/* r = the fragment's distance from the eye */
		BDX_T1(54, 5), BDX_R_MASK(1), T, BDX_V_SEL(3), reg[0],                                      /* mov rT.x, vA.w */
		BDX_T1(54, 5), BDX_R_MASK(2), T, BDX_V_SEL(3), reg[1],                                      /* mov rT.y, vB.w */
		BDX_T1(54, 5), BDX_R_MASK(4), T, BDX_V_SEL(3), reg[2],                                      /* mov rT.z, vC.w */
		BDX_T1(16, 7), BDX_R_MASK(8), T, BDX_R_SWZ(BDX_XYZX), T, BDX_R_SWZ(BDX_XYZX), T,          /* dp3 rT.w, rT.xyzx, rT.xyzx */
		BDX_T1(75, 5), BDX_R_MASK(8), T, BDX_R_SEL(3), T,                                           /* sqrt rT.w, rT.w */
		BDX_T1(0, 9), BDX_R_MASK(1), T, BDX_R_SEL(2) | BDX_ABS, T, BDX_V_SEL(3) | 0x80000000u, 0x41u, p, /* add rT.x, |rT.z|, -vP.w */
		BDX_T1(50, 9), BDX_R_MASK(2), T, BDX_V_SEL(3), p, BDX_L1, bdx_f(0.02f), BDX_L1, bdx_f(2.0f), /* mad rT.y, vP.w, l(0.02), l(2) */
		BDX_T1(29, 8), BDX_R_MASK(1), T, BDX_R_SEL(1), T, BDX_R_SEL(0) | BDX_ABS, T,               /* ge rT.x, rT.y, |rT.x| */
		BDX_T1(55, 9), BDX_R_MASK(8), T, BDX_R_SEL(0), T, BDX_R_SEL(3), T, BDX_V_SEL(3), p,         /* movc rT.w, rT.x, rT.w, vP.w */
		BDX_T1(50, 9) | 0x2000u, BDX_R_MASK(8), T, BDX_R_SEL(3), T, BDX_L1, bdx_f(k), BDX_L1, bdx_f(m), /* mad_sat rT.w: s */
		BDX_T1(29, 7), BDX_R_MASK(1), T, BDX_R_SEL(0), U, BDX_R_SEL(3), T,                          /* ge rT.x, rU.x, rT.w */
		0x0304000du, BDX_R_SEL(0), T };                                                             /* discard_nz rT.x */
	const float F2 = 20.0f; /* the height window's fades */
	const uint32_t cyl[] = {
		/* the offset from the eye in the world's axes (the VS's cylinder edit) */
		BDX_T1(54, 5), BDX_R_MASK(1), T, BDX_V_SEL(3), reg[0],                                      /* mov rT.x, vA.w */
		BDX_T1(54, 5), BDX_R_MASK(2), T, BDX_V_SEL(3), reg[1],                                      /* mov rT.y, vB.w (up) */
		BDX_T1(54, 5), BDX_R_MASK(4), T, BDX_V_SEL(3), reg[2],                                      /* mov rT.z, vC.w */
		BDX_T1(16, 7), BDX_R_MASK(8), T, BDX_R_SWZ(BDX_XYZX), T, BDX_R_SWZ(BDX_XYZX), T,          /* dp3 rT.w: d^2 */
		BDX_T1(75, 5), BDX_R_MASK(8), T, BDX_R_SEL(3), T,                                           /* sqrt rT.w: d */
		/* plausible (d between the depth and twice it, 2 % + 2 cm slack), else a VS without the edit */
		BDX_T1(50, 9), BDX_R_MASK(2), U, BDX_V_SEL(3), p, BDX_L1, bdx_f(0.98f), BDX_L1, bdx_f(-2.0f), /* mad rU.y, vP.w, .98, -2 */
		BDX_T1(50, 9), BDX_R_MASK(4), U, BDX_V_SEL(3), p, BDX_L1, bdx_f(2.0f), BDX_L1, bdx_f(2.0f), /* mad rU.z, vP.w, 2, 2 */
		BDX_T1(29, 7), BDX_R_MASK(2), U, BDX_R_SEL(3), T, BDX_R_SEL(1), U,                          /* ge rU.y, rT.w, rU.y */
		BDX_T1(29, 7), BDX_R_MASK(4), U, BDX_R_SEL(2), U, BDX_R_SEL(3), T,                          /* ge rU.z, rU.z, rT.w */
		BDX_T1(1, 7), BDX_R_MASK(2), U, BDX_R_SEL(1), U, BDX_R_SEL(2), U,                           /* and rU.y: valid */
		/* h = the distance from the eye's vertical line */
		BDX_T1(56, 7), BDX_R_MASK(4), U, BDX_R_SEL(0), T, BDX_R_SEL(0), T,                          /* mul rU.z, rT.x, rT.x */
		BDX_T1(50, 9), BDX_R_MASK(4), U, BDX_R_SEL(2), T, BDX_R_SEL(2), T, BDX_R_SEL(2), U,        /* mad rU.z, rT.z, rT.z, rU.z */
		BDX_T1(75, 5), BDX_R_MASK(4), U, BDX_R_SEL(2), U,                                           /* sqrt rU.z: h */
		BDX_T1(55, 9), BDX_R_MASK(4), U, BDX_R_SEL(1), U, BDX_R_SEL(2), U, BDX_V_SEL(3), p,         /* movc rU.z, valid, h, vP.w */
		BDX_T1(55, 9), BDX_R_MASK(2), T, BDX_R_SEL(1), U, BDX_R_SEL(1), T, BDX_L1, 0,               /* movc rT.y, valid, up, 0 */
		BDX_T1(50, 9) | 0x2000u, BDX_R_MASK(4), U, BDX_R_SEL(2), U, BDX_L1, bdx_f(k), BDX_L1, bdx_f(m), /* mad_sat rU.z: s */
		/* the height window: whole again above above_cm (+F2) and below below_cm (+F2) */
		BDX_T1(50, 9) | 0x2000u, BDX_R_MASK(8), U, BDX_R_SEL(1), T, BDX_L1, bdx_f(1.0f / F2), BDX_L1,
		bdx_f(-above_cm / F2),                                                                      /* mad_sat rU.w, up, 1/F, -A/F */
		BDX_T1(52, 7), BDX_R_MASK(4), U, BDX_R_SEL(2), U, BDX_R_SEL(3), U,                          /* max rU.z, rU.z, rU.w */
		BDX_T1(50, 9) | 0x2000u, BDX_R_MASK(8), U, BDX_R_SEL(1), T, BDX_L1, bdx_f(-1.0f / F2), BDX_L1,
		bdx_f(-below_cm / F2),                                                                      /* mad_sat rU.w, up, -1/F, -B/F */
		BDX_T1(52, 7), BDX_R_MASK(4), U, BDX_R_SEL(2), U, BDX_R_SEL(3), U,                          /* max rU.z, rU.z, rU.w */
		BDX_T1(29, 7), BDX_R_MASK(1), T, BDX_R_SEL(0), U, BDX_R_SEL(2), U,                          /* ge rT.x, rU.x (n), rU.z */
		0x0304000du, BDX_R_SEL(0), T };                                                             /* discard_nz rT.x */
	const uint32_t *dist = shape ? cyl : sph;
	const uint32_t ndist = shape ? (uint32_t)(sizeof cyl / sizeof cyl[0]) : (uint32_t)(sizeof sph / sizeof sph[0]);
	const uint32_t ncode = (uint32_t)(sizeof code / sizeof code[0]) + ndist; /* the noise, then the distance */

	const uint32_t posdcl[4] = { BDX_T1(100, 4) | 4u << 11, 0x001010f2u, p, 1 }; /* dcl_input_ps_siv linear noperspective vP.xyzw, position */
	uint32_t mw = nw + ncode + (npos ? 0 : 4), j = 0;
	uint32_t *o = malloc(mw * sizeof *o);
	*why = "no memory";
	if (!o)
		goto out;
	for (uint32_t i = 0; i < nw;) {
		uint32_t ln = i < 2 ? 1 : fdx_len(w, i, nw);
		if (i == first_in && !npos)
			memcpy(o + j, posdcl, sizeof posdcl), j += 4;
		if (i == code_at) {
			memcpy(o + j, code, sizeof code), j += (uint32_t)(sizeof code / sizeof code[0]);
			memcpy(o + j, dist, ndist * sizeof dist[0]), j += ndist;
		}
		memcpy(o + j, w + i, ln * 4);
		for (int c = 0; c < 3; c++)
			if (i == dcl_at[c])
				o[j + 1] = 0x001010f2u; /* .xyz -> .xyzw */
		if (npos && i == pos_at)
			o[j + 1] |= 0x80u; /* SV_Position's .w read */
		if (i == temps_at)
			o[j + 1] = ntemps + 2;
		j += ln, i += ln;
	}
	*why = "the new program's length (a bug)";
	if (j != mw) {
		free(o);
		goto out;
	}
	/* the ISGN: TEXCOORD2/3/4 xyzw, always read; SV_Position's w read */
	uint32_t pat_at[7];
	unsigned char pat_v[7];
	for (int c = 0; c < 3; c++)
		pat_at[2 * c] = e[c] + 20, pat_v[2 * c] = 0xf, pat_at[2 * c + 1] = e[c] + 21, pat_v[2 * c + 1] = 0xf;
	pat_at[6] = pe + 21, pat_v[6] = (unsigned char)(blob[pe + 21] | 8);
	b = bdx_build(blob, n, shex, nw, o, mw, pat_at, pat_v, 7, n_out);
	free(o);
	*why = b ? NULL : "no memory";
out:
	free(w);
	return b;
}

static unsigned char *somvr_bubble_ps(const unsigned char *blob, size_t n, float in_cm, float out_cm, size_t *n_out,
				      const char **why)
{
	return somvr_bubble_ps2(blob, n, in_cm, out_cm, 0, 0, 0, n_out, why);
}

/* BUGS, the geometry shader: COLOR0.w x the fade by the particle's distance from the eye. NULL + *why when not edited */
static unsigned char *somvr_bubble_gs(const unsigned char *blob, size_t n, float in_cm, float out_cm, size_t *n_out,
				      const char **why)
{
	uint32_t *w = NULL, nw, shex, kind, r, c, vw_r, vw_c, vw_s;
	*n_out = 0;
	if (!bdx_load(blob, n, &w, &nw, &shex, &kind, why))
		return NULL;
	unsigned char *b = NULL;
	*why = "not a geometry shader";
	if (kind != 2)
		goto out;
	*why = "not a translucent GPU particle (no k_vParticleScale + k_fAlphaAmount / k_fRotationRate in $Globals)";
	if (!gdx_rdef_var(blob, n, "$Globals", "k_vParticleScale", &r, &c, NULL) ||
	    !(gdx_rdef_var(blob, n, "$Globals", "k_fAlphaAmount", &r, &c, NULL) ||
	      gdx_rdef_var(blob, n, "$Globals", "k_fRotationRate", &r, &c, NULL)))
		goto out;
	uint32_t nq = 0, nc = 0, qe = bdx_sig(blob, n, "ISGN", "WORLDPOS", 0, 0xffffffffu, &nq);
	uint32_t ce = bdx_sig(blob, n, "OSG5", "COLOR", 0, 0xffffffffu, &nc);
	if (!ce)
		ce = bdx_sig(blob, n, "OSGN", "COLOR", 0, 0xffffffffu, &nc);
	*why = "no WORLDPOS0 .xyz input / COLOR0 .xyzw output alone on its register";
	if (!qe || nq != 1 || (blob[qe + 20] & 7) != 7 || !ce || nc != 1 || blob[ce + 20] != 0xf)
		goto out;
	*why = "no k_mViewToWorld in CBuffer_View";
	if (!gdx_rdef_var(blob, n, "CBuffer_View", "k_mViewToWorld", &vw_r, &vw_c, &vw_s) || vw_c)
		goto out;
	uint32_t q = fdx_rd(blob + qe + 16), C = fdx_rd(blob + ce + 16);
	uint32_t temps_at = 0, ntemps = 0, code_at = 0, a_at = 0, na = 0, nwr = 0, corner = 0, toworld = 0;
	*why = "a broken instruction stream";
	for (uint32_t i = 2; i < nw;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff;
		if (!ln)
			goto out;
		if (!code_at && !bdx_is_dcl(op))
			code_at = i;
		if (op == 104 && ln == 2)
			temps_at = i, ntemps = w[i + 1];
		else if (!bdx_is_dcl(op) && ln >= 3 && (w[i + 1] & 0xfffff00fu) == 0x00102002u && w[i + 2] == C &&
			 (w[i + 1] & 0x80u)) {
			nwr++; /* a write of COLOR0.w */
			if (op == 54 && ln == 5 && !(w[i] & 0x80003800u) && w[i + 1] == BDX_O_MASK(8) &&
			    (w[i + 3] & 0xffffffcfu) == 0x0010000au)
				a_at = i, na++; /* mov oC.w, rA.c */
		} else if (op == 0 && ln == 8 && !(w[i] & 0x80000000u) && w[i + 1] == BDX_R_MASK(7) &&
			   ((w[i + 3] == 0x00100246u && w[i + 5] == 0x00201246u && w[i + 6] == 0 && w[i + 7] == q) ||
			    (w[i + 3] == 0x00201246u && w[i + 4] == 0 && w[i + 5] == q && w[i + 6] == 0x00100246u)))
			corner = 1; /* add rX.xyz, rY.xyzx, v0[q].xyzx: a corner around the view-space centre */
		else if (op == 17 && ln == 8 && w[i + 3] == 0x00208e46u && w[i + 4] == vw_s && w[i + 5] == vw_r)
			toworld = 1; /* dp4 .., cb<view>[k_mViewToWorld].xyzw, ..: the corner to the world */
		i += ln;
	}
	*why = "the corners are not view-space centre + offset taken to the world by k_mViewToWorld";
	if (!corner || !toworld)
		goto out;
	*why = "COLOR0.w not written by one mov oC.w, rA.c";
	if (na != 1 || nwr != 1)
		goto out;
	*why = "no dcl_temps / no code";
	if (!temps_at || !code_at || ntemps > 4095)
		goto out;
	const float k = 1.0f / (out_cm - in_cm), m = -in_cm / (out_cm - in_cm);
	const uint32_t T = ntemps;
	const uint32_t code[] = {
		BDX_T1(16, 9), BDX_R_MASK(1), T, 0x00201246u, 0, q, 0x00201246u, 0, q,                     /* dp3 rT.x, v0[q].xyzx, v0[q].xyzx */
		BDX_T1(75, 5), BDX_R_MASK(1), T, BDX_R_SEL(0), T,                                           /* sqrt rT.x, rT.x */
		BDX_T1(50, 9) | 0x2000u, BDX_R_MASK(1), T, BDX_R_SEL(0), T, BDX_L1, bdx_f(k), BDX_L1, bdx_f(m) }; /* mad_sat */
	const uint32_t ncode = (uint32_t)(sizeof code / sizeof code[0]);
	uint32_t mw = nw + ncode + 2, j = 0;
	uint32_t *o = malloc(mw * sizeof *o);
	*why = "no memory";
	if (!o)
		goto out;
	for (uint32_t i = 0; i < nw;) {
		uint32_t ln = i < 2 ? 1 : fdx_len(w, i, nw);
		if (i == code_at)
			memcpy(o + j, code, sizeof code), j += ncode;
		if (i == a_at) { /* mul oC.w, rA.c, rT.x */
			const uint32_t ins[7] = { BDX_T1(56, 7), w[i + 1], w[i + 2], w[i + 3], w[i + 4], BDX_R_SEL(0), T };
			memcpy(o + j, ins, sizeof ins), j += 7;
		} else {
			memcpy(o + j, w + i, ln * 4);
			if (i == temps_at)
				o[j + 1] = ntemps + 1;
			j += ln;
		}
		i += ln;
	}
	*why = "the new program's length (a bug)";
	if (j != mw) {
		free(o);
		goto out;
	}
	b = bdx_build(blob, n, shex, nw, o, mw, NULL, NULL, 0, n_out);
	free(o);
	*why = b ? NULL : "no memory";
out:
	free(w);
	return b;
}
#endif
