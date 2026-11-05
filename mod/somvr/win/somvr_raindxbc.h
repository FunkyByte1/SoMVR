/* somvr_raindxbc.h - build 37 (som-b37-audio, docs/re-xr-pipeline.md 173.7): SOMVR_RAIN_SCALE, the rain's sheet at a
 * share of its strength, as a DXBC edit (the fog's helpers: somvr_fogdxbc.h). Header-only: the Windows DLL's
 * CreatePixelShader hook (win/somvr_win_d3d.c) and tests/test_raindxbc.c include it.
 *
 * Two rain materials end the same way:
 * - the falling rain of the Sea of Nurnen's rain weathers: the GPU particle system weather\udunnight01rain\
 *   rainfallinginair\rainfallinggpu01a (son_weather.arch05 sonday03rain.embb: its render material draws with
 *   shaders\shadertrees\translucent\gpupart_rain.fxo - VS 7308, GS 6140, PS 4232 bytes, checksum ef99d1c325a70575...,
 *   used by this rain only; its $Globals k_cTintColor, k_vParticleScale, k_fPeakVelocity, k_vSplatInfo,
 *   k_fStreakLength, ..): the streak texture x the tint x the object colour x the particle's alpha from the GS
 *   (COLOR0.w: its age fade), premultiplied; o0.w = its luminance x a scale;
 * - a rain sheet (DLC2.arch05, 5328 bytes, 38461aa78d30cac3...; k_fRainBrightness, k_fRainPanU/V, k_fMistBrightness,
 *   k_vLightningColor - not made in the Nurnen boots): the panned streaks x the rain's brightness plus the mist, the
 *   lightning's tint, a mask, the fade by the depth behind it; premultiplied.
 * Both write
 *     mov o0.xyz, rX.xyzx
 *     mul o0.w, rX.w, cb0[..].x
 *     ret
 * The edit, right before that mov:
 *     mul rX.xyzw, rX.xyzw, l(s, s, s, s)
 * (10 dwords): the whole premultiplied output at s of its strength - each streak (and the sheet's mist) at half its
 * brightness with 0.5; the splashes, the drips and the ground's wet look (other materials) untouched. Matched by the
 * blob's own names (k_fStreakLength + k_fPeakVelocity + k_vSplatInfo, or k_fRainBrightness + k_fMistBrightness) and
 * that tail; anything else is left as it is. */
#ifndef SOMVR_RAINDXBC_H
#define SOMVR_RAINDXBC_H
#include "somvr_fogdxbc.h"

static int rdx_has(const unsigned char *b, size_t n, const char *s)
{
	size_t k = strlen(s);
	for (size_t i = 0; i + k <= n; i++)
		if (b[i] == (unsigned char)s[0] && !memcmp(b + i, s, k))
			return 1;
	return 0;
}

static unsigned char *somvr_raindxbc(const unsigned char *blob, size_t n, float scale, size_t *n_out, const char **why)
{
	*n_out = 0;
	*why = "not DXBC";
	if (n < 32 || memcmp(blob, "DXBC", 4) || fdx_rd(blob + 20) != 1 || fdx_rd(blob + 24) != n)
		return NULL;
	*why = "not the rain (no k_fStreakLength + k_fPeakVelocity + k_vSplatInfo, no k_fRainBrightness + k_fMistBrightness)";
	if (!(rdx_has(blob, n, "k_fStreakLength") && rdx_has(blob, n, "k_fPeakVelocity") && rdx_has(blob, n, "k_vSplatInfo")) &&
	    !(rdx_has(blob, n, "k_fRainBrightness") && rdx_has(blob, n, "k_fMistBrightness")))
		return NULL;
	uint32_t nch = fdx_rd(blob + 28), shex = 0;
	*why = "a broken container";
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
	uint32_t nw = fdx_rd(blob + shex + 4) / 4;
	const unsigned char *pb = blob + shex + 8;
	*why = "not a pixel shader";
	if (nw < 2 || (fdx_rd(pb) >> 16) != 0 || fdx_rd(pb + 4) != nw)
		return NULL;
	uint32_t *w = malloc(nw * sizeof *w), at[4096], nat = 0;
	*why = "no memory";
	if (!w)
		return NULL;
	for (uint32_t i = 0; i < nw; i++)
		w[i] = fdx_rd(pb + 4 * i);
	*why = "a broken instruction stream";
	for (uint32_t i = 2; i < nw && nat < 4096;) {
		uint32_t ln = fdx_len(w, i, nw);
		if (!ln)
			goto fail;
		at[nat++] = i, i += ln;
	}
	/* the tail: mov o0.xyz, rX.xyzx; mul o0.w, rX.w, <any>; ret */
	*why = "no `mov o0.xyz, rX; mul o0.w, rX.w, ..; ret` tail";
	int mv = -1;
	uint32_t X = 0;
	for (uint32_t q = 1; q + 2 < nat && mv < 0; q++) {
		uint32_t i = at[q], j = at[q + 1], k = at[q + 2];
		if ((w[i] & 0x7ff) == 54 /* mov */ && fdx_len(w, i, nw) == 5 && !(w[i] & 0x80000000u) && w[i + 1] == 0x00102072u &&
		    w[i + 2] == 0 && FDX_OP_TYPE(w[i + 3]) == 0 && FDX_OP_PLAIN_1D(w[i + 3]) && ((w[i + 3] >> 2) & 3) == 1 &&
		    (w[j] & 0x7ff) == 56 /* mul */ && !(w[j] & 0x80000000u) && w[j + 1] == 0x00102082u && w[j + 2] == 0 &&
		    w[j + 3] == FDX_SRC(3) && w[j + 4] == w[i + 4] && (w[k] & 0x7ff) == 62 /* ret */)
			mv = (int)q, X = w[i + 4];
	}
	if (mv < 0)
		goto fail;
	/* not twice: the instruction before already ours */
	uint32_t p = at[mv - 1];
	*why = "already edited";
	if ((w[p] & 0x7ff) == 56 && fdx_len(w, p, nw) == 10 && w[p + 1] == 0x001000f2u && w[p + 2] == X && w[p + 5] == 0x00004002u)
		goto fail;
	uint32_t sb;
	memcpy(&sb, &scale, 4);
	struct fdx_ins ins = { p, 10, { FDX_OPC(56, 10), 0x001000f2u, X, 0x00100e46u, X, 0x00004002u, sb, sb, sb, sb } };
	*why = "no memory";
	unsigned char *o = fdx_rebuild(blob, n, shex, w, nw, &ins, 1, 0xfffffffeu, 0, n_out);
	if (!o)
		goto fail;
	free(w);
	*why = NULL;
	return o;
fail:
	free(w);
	return NULL;
}

#endif
