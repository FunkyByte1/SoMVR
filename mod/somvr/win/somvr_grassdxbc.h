/* somvr_grassdxbc.h - the meadow grass for VR, as DXBC edits (docs/re-xr-pipeline.md 148, SOMVR_XR_GRASS). Header-only;
 * the Windows DLL's ID3D11Device::CreateVertexShader (COM slot 12) / CreatePixelShader (slot 15) hooks and
 * tests/test_grassdxbc.c (native, Linux) include it. Needs somvr_fogdxbc.h (fdx_rd/fdx_wr/fdx_len, the checksum).
 *
 * The game's scatter grass (shaders\shadertrees\solid\scattergrass_parallaxfixed.fxo, scattergrass_fixed.fxo,
 * scattergrassclump.fxo) draws world-fixed cards. Its pixel shader shows two pictures per card from an atlas of
 * k_fNumBillboards slots: the front one, and a "parallax billboard" behind it, each shifted along the card by the
 * eye's direction in the card's tangent space times k_vParallaxDepth (x the back plane, y the front plane: "parallax
 * out"; the materials: 0.3, 0), most at the card's top middle, none at its edges and foot:
 *     mad rA.xy, -rV.xyxx, cb0[27].xxxx, v1.zwzz      the back layer's uv
 *     mad rA.zw, rV.zzzw,  cb0[27].yyyy, v1.xxxy      the front layer's uv
 * Its vertex shader bends each card along its normal by k_fTriangleBend x (the view's forward . the card's up)^2 (up
 * to 25 cm looking straight down: the grass's shape follows the head's pitch) and pushes grass near the view's axis,
 * within 5 m and above the eye - 20 cm down by up to 1 m (keeping a third-person camera clear: follows the gaze).
 * The parts (SOMVR_GRASS_*):
 *   FRONT  the PS's front-plane operand cb0[k_vParallaxDepth].yyyy becomes l(F,F,F,F): the front picture pushed out
 *          toward the eye as the back one goes in (both eyes' own directions: depth inside the card in stereo);
 *   BEND   the VS's `dp3 rD.x, cbV[k_vWorldSpaceCameraDir].xyzx, rB.xyzx` takes normalize(v0 - k_vObjectSpaceEyePos)
 *          instead (the direction from the eye to the tuft: the bend set by where the eye is, not where it looks);
 *          four instructions and one temp inserted before it;
 *   PUSH   the VS's `min rX.c, rX.c, l(100)` (the push's 1 m cap) becomes l(0): no push.
 * Matched by structure and by the reflection's names (RDEF), every link checked before anything is written; a blob
 * that is not the grass's (or not as expected) is left alone with *why set. The container is rebuilt (the program's
 * length, its chunk's size, the later chunks' offsets, the total size, the checksum). */
#ifndef SOMVR_GRASSDXBC_H
#define SOMVR_GRASSDXBC_H
#include "somvr_fogdxbc.h"

#define SOMVR_GRASS_FRONT 1u
#define SOMVR_GRASS_BEND 2u
#define SOMVR_GRASS_PUSH 4u

/* the chunk of tag (4 chars) -> its offset (0 = none) */
static uint32_t gdx_chunk(const unsigned char *blob, size_t n, const char *tag)
{
	uint32_t nch = fdx_rd(blob + 28);
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t o = fdx_rd(blob + 32 + 4 * c);
		if (!memcmp(blob + o, tag, 4))
			return o;
	}
	(void)n;
	return 0;
}

/* a 0-terminated name at offset o of an n-byte table equals s */
static int gdx_name_is(const unsigned char *t, uint32_t n, uint32_t o, const char *s)
{
	size_t l = strlen(s);
	return o < n && (size_t)(n - o) > l && !memcmp(t + o, s, l + 1);
}

/* the reflection (RDEF): a variable's register (offset / 16) and component in the cbuffer named cb -> 1;
 * the cbuffer's bind point in *slot (when slot) */
static int gdx_rdef_var(const unsigned char *blob, size_t n, const char *cb, const char *var, uint32_t *reg,
			uint32_t *comp, uint32_t *slot)
{
	uint32_t r = gdx_chunk(blob, n, "RDEF");
	if (!r)
		return 0;
	const unsigned char *d = blob + r + 8;
	uint32_t dn = fdx_rd(blob + r + 4);
	if (dn < 28)
		return 0;
	uint32_t ncb = fdx_rd(d), cbo = fdx_rd(d + 4), nbind = fdx_rd(d + 8), bo = fdx_rd(d + 12);
	uint32_t vsz = fdx_rd(d + 28) == 0x31314452u /* RD11 */ ? 40 : 24;
	if ((uint64_t)cbo + 24ull * ncb > dn || (uint64_t)bo + 32ull * nbind > dn)
		return 0;
	for (uint32_t i = 0; i < ncb; i++) {
		const unsigned char *c = d + cbo + 24 * i;
		if (!gdx_name_is(d, dn, fdx_rd(c), cb))
			continue;
		uint32_t nv = fdx_rd(c + 4), vo = fdx_rd(c + 8);
		if ((uint64_t)vo + (uint64_t)vsz * nv > dn)
			return 0;
		for (uint32_t j = 0; j < nv; j++) {
			const unsigned char *v = d + vo + vsz * j;
			if (!gdx_name_is(d, dn, fdx_rd(v), var))
				continue;
			uint32_t so = fdx_rd(v + 4);
			*reg = so / 16, *comp = (so % 16) / 4;
			if (slot) {
				*slot = 0xffffffffu;
				for (uint32_t k = 0; k < nbind; k++) {
					const unsigned char *bd = d + bo + 32 * k;
					if (fdx_rd(bd + 4) == 0 /* cbuffer */ && gdx_name_is(d, dn, fdx_rd(bd), cb))
						*slot = fdx_rd(bd + 20);
				}
				if (*slot == 0xffffffffu)
					return 0;
			}
			return 1;
		}
	}
	return 0;
}

/* an operand at word k: its length in words (0 = a form this edit does not read) */
static uint32_t gdx_oplen(const uint32_t *w, uint32_t k, uint32_t end)
{
	if (k >= end)
		return 0;
	uint32_t t = w[k], ln = 1 + (t >> 31), type = (t >> 12) & 0xff, dim = (t >> 20) & 3;
	if (type == 4 /* immediate32 */)
		return (t & 3) == 1 ? 2 : (t & 3) == 2 ? 5 : 0;
	for (uint32_t i = 0; i < dim; i++) {
		uint32_t rep = (t >> (22 + 3 * i)) & 7;
		if (rep != 0)
			return 0; /* only immediate indices */
		ln++;
	}
	return k + ln <= end ? ln : 0;
}

/* a constant buffer operand cb<slot>[reg] with the 4-component swizzle swz, no modifier: its 3 words at k */
static int gdx_is_cb(const uint32_t *w, uint32_t k, uint32_t slot, uint32_t reg, uint32_t swz)
{
	return w[k] == (0x00208002u | 4u /* swizzle mode */ | swz << 4) && w[k + 1] == slot && w[k + 2] == reg;
}

/* the edit; parts = SOMVR_GRASS_*; front = the front plane's depth (FRONT). Returns the new blob (malloc'ed,
 * *n_out bytes) with *done = the parts made, or NULL with *why set (nothing changed) */
static unsigned char *somvr_grassdxbc(const unsigned char *blob, size_t n, unsigned parts, float front, size_t *n_out,
				      unsigned *done, const char **why)
{
	*n_out = 0, *done = 0;
	*why = "not DXBC";
	if (n < 32 || memcmp(blob, "DXBC", 4) || fdx_rd(blob + 20) != 1 || fdx_rd(blob + 24) != n)
		return NULL;
	uint32_t nch = fdx_rd(blob + 28);
	*why = "a broken chunk table";
	if (!nch || nch > 32 || 32 + 4 * (size_t)nch > n)
		return NULL;
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t o = fdx_rd(blob + 32 + 4 * c);
		if ((size_t)o + 8 > n || (size_t)o + 8 + fdx_rd(blob + o + 4) > n)
			return NULL;
	}
	uint32_t shex = gdx_chunk(blob, n, "SHEX");
	if (!shex)
		shex = gdx_chunk(blob, n, "SHDR");
	*why = "no SHEX/SHDR chunk";
	if (!shex)
		return NULL;
	uint32_t clen = fdx_rd(blob + shex + 4), nw = clen / 4, kind;
	const unsigned char *pb = blob + shex + 8;
	*why = "a broken program header";
	if (nw < 2 || fdx_rd(pb + 4) != nw)
		return NULL;
	kind = fdx_rd(pb) >> 16; /* 0 pixel, 1 vertex */
	uint32_t eye_r, eye_c, par_r, par_c, cam_r, cam_c, cam_s, tb_r, tb_c, wp_r, wp_c;
	*why = "not the scatter grass's (no k_vWindPhase / k_vObjectSpaceEyePos in $Globals)";
	if (!gdx_rdef_var(blob, n, "$Globals", "k_vWindPhase", &wp_r, &wp_c, NULL) ||
	    !gdx_rdef_var(blob, n, "$Globals", "k_vObjectSpaceEyePos", &eye_r, &eye_c, NULL))
		return NULL;
	int is_ps = kind == 0, is_vs = kind == 1;
	int want_front = is_ps && (parts & SOMVR_GRASS_FRONT) &&
			 gdx_rdef_var(blob, n, "$Globals", "k_vParallaxDepth", &par_r, &par_c, NULL) && par_c < 3;
	int want_bend = is_vs && (parts & SOMVR_GRASS_BEND) && eye_c == 0 &&
			gdx_rdef_var(blob, n, "$Globals", "k_fTriangleBend", &tb_r, &tb_c, NULL) &&
			gdx_rdef_var(blob, n, "CBuffer_View", "k_vWorldSpaceCameraDir", &cam_r, &cam_c, &cam_s) && cam_c == 0;
	int want_push = is_vs && (parts & SOMVR_GRASS_PUSH);
	*why = "nothing asked of this shader (a grass shader without the parts asked)";
	if (!want_front && !want_bend && !want_push)
		return NULL;
	uint32_t *w = malloc(nw * sizeof *w);
	*why = "no memory";
	if (!w)
		return NULL;
	for (uint32_t i = 0; i < nw; i++)
		w[i] = fdx_rd(pb + 4 * i);
	/* find the instructions */
	uint32_t temps_at = 0, ntemps = 0, v0_ok = 0, front_at = 0, front_op = 0, n_front = 0, bend_at = 0, n_bend = 0;
	uint32_t push_at = 0, n_push = 0;
	*why = "a broken instruction stream";
	for (uint32_t i = 2; i < nw;) {
		uint32_t ln = fdx_len(w, i, nw), op = w[i] & 0x7ff;
		if (!ln)
			goto fail;
		int ext = (w[i] & 0x80000000u) != 0;
		if (op == 104 /* dcl_temps */ && ln == 2)
			temps_at = i, ntemps = w[i + 1];
		else if (op == 95 /* dcl_input */ && ln == 3 && (w[i + 1] & 0xfffff00fu) == 0x00101002u && ((w[i + 1] >> 4) & 7) == 7 &&
			 w[i + 2] == 0)
			v0_ok = 1; /* dcl_input v0.xyz(w) */
		else if (want_front && op == 50 /* mad */ && !ext) {
			/* dst, src0, src1 = cb0[par].<c><c><c><c>, src2 */
			uint32_t k = i + 1, l0 = gdx_oplen(w, k, i + ln), l1 = l0 ? gdx_oplen(w, k + l0, i + ln) : 0;
			uint32_t c = par_c + 1, swz = c | c << 2 | c << 4 | c << 6;
			if (l0 && l1 && k + l0 + l1 + 3 <= i + ln && gdx_is_cb(w, k + l0 + l1, 0, par_r, swz)) {
				uint32_t l2 = gdx_oplen(w, k + l0 + l1 + 3, i + ln);
				if (l2 && k + l0 + l1 + 3 + l2 == i + ln)
					front_at = i, front_op = k + l0 + l1, n_front++;
			}
		} else if (want_bend && op == 16 /* dp3 */ && !ext && ln == 8 && (w[i + 1] & 0xfffff00fu) == 0x00100002u &&
			   gdx_is_cb(w, i + 3, cam_s, cam_r, 0x24 /* xyzx */) && (w[i + 6] & 0xfffff00fu) == 0x00100006u)
			bend_at = i, n_bend++; /* dp3 rD.c, cbV[cam].xyzx, rB.<swz> */
		else if (want_push && op == 51 /* min */ && !ext && ln == 7 && (w[i + 1] & 0xffffff0fu) == 0x00100002u &&
			 (w[i + 3] & 0xffffff0fu) == 0x0010000au && w[i + 2] == w[i + 4] && ((w[i + 1] >> 4) & 0xf) &&
			 !(((w[i + 1] >> 4) & 0xf) & (((w[i + 1] >> 4) & 0xf) - 1)) &&
			 ((w[i + 3] >> 4) & 3) == (uint32_t)__builtin_ctz((w[i + 1] >> 4) & 0xf) && w[i + 5] == 0x00004001u &&
			 w[i + 6] == 0x42c80000u /* 100.0 */)
			push_at = i, n_push++; /* min rX.c, rX.c, l(100) */
		i += ln;
	}
	if (want_front && n_front != 1) {
		*why = n_front ? "more than one mad with the front plane's operand" : "no mad with cb0[k_vParallaxDepth].yyyy as its src1";
		want_front = 0;
	}
	if (want_bend && (n_bend != 1 || !temps_at || !v0_ok)) {
		*why = !v0_ok ? "no dcl_input v0.xyz" : !temps_at ? "no dcl_temps" :
		       n_bend ? "more than one dp3 with the camera's direction" : "no dp3 rD.c, cbV[k_vWorldSpaceCameraDir].xyzx, rB";
		want_bend = 0;
	}
	if (want_push && n_push != 1) {
		*why = n_push ? "more than one min .., l(100)" : "no min rX.c, rX.c, l(100)";
		want_push = 0;
	}
	if (!want_front && !want_bend && !want_push)
		goto fail;
	/* the new program: FRONT +2 words, BEND +28 - 1 */
	uint32_t mw = nw + (want_front ? 2 : 0) + (want_bend ? 27 : 0), j = 0;
	uint32_t *o = malloc(mw * sizeof *o);
	*why = "no memory";
	if (!o)
		goto fail;
	uint32_t ff;
	memcpy(&ff, &front, 4);
	uint32_t T = ntemps;
	for (uint32_t i = 0; i < nw;) {
		uint32_t ln = i < 2 ? 1 : fdx_len(w, i, nw);
		if (i >= 2 && want_front && i == front_at) {
			uint32_t a = front_op - i; /* words before the operand */
			o[j] = (w[i] & ~(0x7fu << 24)) | (ln + 2) << 24;
			memcpy(o + j + 1, w + i + 1, (a - 1) * 4);
			o[j + a] = 0x00004002u, o[j + a + 1] = ff, o[j + a + 2] = ff, o[j + a + 3] = ff, o[j + a + 4] = ff;
			memcpy(o + j + a + 5, w + front_op + 3, (ln - a - 3) * 4);
			j += ln + 2;
		} else if (i >= 2 && want_bend && i == bend_at) {
			const uint32_t ins[28] = {
				0x09000000u, 0x00100072u, T, 0x00101246u, 0, 0x80208246u, 0x00000041u, 0, eye_r, /* add rT.xyz, v0.xyzx, -cb0[eye].xyzx */
				0x07000010u, 0x00100082u, T, 0x00100246u, T, 0x00100246u, T,                    /* dp3 rT.w, rT.xyzx, rT.xyzx */
				0x05000044u, 0x00100082u, T, 0x0010003au, T,                                     /* rsq rT.w, rT.w */
				0x07000038u, 0x00100072u, T, 0x00100246u, T, 0x00100ff6u, T };                   /* mul rT.xyz, rT.xyzx, rT.wwww */
			memcpy(o + j, ins, sizeof ins);
			j += 28;
			o[j] = (w[i] & ~(0x7fu << 24)) | 7u << 24;                   /* dp3, 7 words */
			o[j + 1] = w[i + 1], o[j + 2] = w[i + 2];                   /* the same destination */
			o[j + 3] = 0x00100246u, o[j + 4] = T;                       /* rT.xyzx for the camera's direction */
			o[j + 5] = w[i + 6], o[j + 6] = w[i + 7];                   /* the same card axis */
			j += 7;
		} else {
			memcpy(o + j, w + i, ln * 4);
			if (i >= 2 && want_bend && i == temps_at)
				o[j + 1] = ntemps + 1;
			if (i >= 2 && want_push && i == push_at)
				o[j + 6] = 0; /* l(100) -> l(0) */
			j += ln;
		}
		i += ln;
	}
	o[1] = mw;
	/* the container */
	int32_t add = (int32_t)(mw - nw) * 4;
	size_t m = n + (size_t)(int64_t)add;
	unsigned char *b = malloc(m);
	*why = "no memory";
	if (!b) {
		free(o);
		goto fail;
	}
	size_t at = (size_t)(pb - blob);
	memcpy(b, blob, at);
	for (uint32_t i = 0; i < mw; i++)
		fdx_wr(b + at + 4 * i, o[i]);
	memcpy(b + at + 4 * (size_t)mw, blob + at + 4 * (size_t)nw, n - at - 4 * (size_t)nw);
	fdx_wr(b + shex + 4, clen + (uint32_t)add);
	fdx_wr(b + 24, (uint32_t)m);
	for (uint32_t c = 0; c < nch; c++) {
		uint32_t co = fdx_rd(blob + 32 + 4 * c);
		if (co > shex)
			fdx_wr(b + 32 + 4 * c, co + (uint32_t)add);
	}
	somvr_dxbc_checksum(b, m, b + 4);
	free(o);
	free(w);
	*done = (want_front ? SOMVR_GRASS_FRONT : 0) | (want_bend ? SOMVR_GRASS_BEND : 0) | (want_push ? SOMVR_GRASS_PUSH : 0);
	*n_out = m;
	*why = NULL;
	return b;
fail:
	free(w);
	return NULL;
}
#endif
