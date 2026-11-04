/* somvr_fogpatch.h - build 31 (docs/re-xr-pipeline.md 137): the game's fog by the distance along the eye's ray, not the
 * view's depth - a SPIR-V edit of the fog overlay's two pixel shaders as IndirectX translates them (no dependencies: the
 * layer's somvr_fog_layer.c and tests/test_fogpatch.c include it).
 *
 * fog_overlay.mat's pixel shader (and its OIT twin, fog per transparent layer) is DXBC translated register by register:
 * rN = (z, x*z, y*z, ..) is the pixel's position in the view (z = the linear depth from the depth buffer, x, y = the view
 * ray at depth 1, v2.xy / v1.w), and the fog's path length is `movc(sky, the atmosphere's edge, rN.x)` - the depth along
 * the view's axis. On a flat screen that is close to the distance; in a headset's 100-degree view a pixel at the edge
 * gets ~65 % of its fog, so the fog thins where the head is not pointing and thickens where it is: a wall of fog that
 * turns with the head. The edit gives the movc length(rN.xyz) instead (the distance from the eye: the same fog
 * wherever the head points):
 *     %V = OpBitcast %uint %L        (%L = OpLoad %int (OpAccessChain %ptr %rN %c0): rN.x)
 *  -> %a = OpLoad %int4 %rN; %b = OpBitcast %float4 %a; %c = OpVectorShuffle %float3 %b %b 0 1 2;
 *     %d = OpExtInst %float %glsl450 Length %c; %V = OpBitcast %uint %d
 * The call is named by its result id (found offline with scripts/spv_dec.py on the layer's dumps); every link of the
 * chain is checked before anything is written. somvr_fogpatch() returns the new words (malloc'ed, *n_out words) or NULL
 * with *why set (nothing changed). */
#ifndef SOMVR_FOGPATCH_H
#define SOMVR_FOGPATCH_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* the result id's place in instruction `op` (the words after the opcode word), -1 = none known here */
static int fp_result_slot(uint32_t op) {
    switch (op) {
    case 11: case 19: case 20: case 21: case 22: case 23: case 32: return 0;   /* ExtInstImport, Type* */
    case 43: case 59: case 61: case 65: case 57: case 124: case 79: case 12: return 1; /* Constant, Variable, Load, AccessChain, FunctionCall, Bitcast, VectorShuffle, ExtInst */
    default: return -1;
    }
}

/* the index of the instruction defining id (one of the opcodes above), -1 none */
static long fp_def(const uint32_t *w, const uint32_t *off, uint32_t ni, uint32_t id) {
    for (uint32_t k = 0; k < ni; k++) {
        uint32_t op = w[off[k]] & 0xffff, wc = w[off[k]] >> 16;
        int s = fp_result_slot(op);
        if (s >= 0 && (uint32_t)s + 1 < wc && w[off[k] + 1 + s] == id) return (long)k;
    }
    return -1;
}

static uint32_t *somvr_fogpatch(const uint32_t *w, size_t nw, uint32_t call, size_t *n_out, const char **why) {
    *n_out = 0;
    *why = "not SPIR-V";
    if (nw < 6 || w[0] != 0x07230203u) return NULL;
    uint32_t *off = malloc(nw * sizeof *off), ni = 0;
    if (!off) return (*why = "no memory", NULL);
    for (size_t i = 5; i < nw;) {
        uint32_t wc = w[i] >> 16;
        if (!wc || i + wc > nw) return (free(off), *why = "a broken instruction", NULL);
        off[ni++] = (uint32_t)i;
        i += wc;
    }
    uint32_t tf = 0, ti = 0, tu = 0, glsl = 0, f3 = 0, f4 = 0;
    for (uint32_t k = 0; k < ni; k++) {
        const uint32_t *a = w + off[k] + 1, op = w[off[k]] & 0xffff, wc = w[off[k]] >> 16;
        if (op == 22 && wc == 3 && a[1] == 32) tf = a[0];
        else if (op == 21 && wc == 4 && a[1] == 32) *(a[2] ? &ti : &tu) = a[0];
        else if (op == 11 && wc >= 5 && !memcmp(a + 1, "GLSL.std.450", 12)) glsl = a[0];
    }
    for (uint32_t k = 0; k < ni; k++) {
        const uint32_t *a = w + off[k] + 1, op = w[off[k]] & 0xffff;
        if (op == 23 && tf && a[1] == tf && a[2] == 3) f3 = a[0];
        if (op == 23 && tf && a[1] == tf && a[2] == 4) f4 = a[0];
    }
    long ci = fp_def(w, off, ni, call), vi = -1;
    *why = "no float/int/uint/vec3/vec4 types or no GLSL.std.450";
    if (!tf || !ti || !tu || !glsl || !f3 || !f4) goto fail;
    *why = "no OpFunctionCall with that id";
    if (ci < 0 || (w[off[ci]] & 0xffff) != 57) goto fail;
    /* the third parameter: the last OpStore before the call */
    *why = "no store before the call";
    long k = ci - 1;
    while (k >= 0 && (w[off[k]] & 0xffff) != 62) k--;
    if (k < 0) goto fail;
    uint32_t v = w[off[k] + 2];
    vi = fp_def(w, off, ni, v);
    *why = "the third argument is not OpBitcast %uint";
    if (vi < 0 || (w[off[vi]] & 0xffff) != 124 || w[off[vi] + 1] != tu) goto fail;
    long li = fp_def(w, off, ni, w[off[vi] + 3]);
    *why = "the bitcast's value is not OpLoad %int";
    if (li < 0 || (w[off[li]] & 0xffff) != 61 || w[off[li] + 1] != ti) goto fail;
    long ai = fp_def(w, off, ni, w[off[li] + 3]);
    *why = "the load is not through OpAccessChain %ptr %reg %c";
    if (ai < 0 || (w[off[ai]] & 0xffff) != 65 || (w[off[ai]] >> 16) != 5) goto fail;
    uint32_t reg = w[off[ai] + 3];
    long cix = fp_def(w, off, ni, w[off[ai] + 4]);
    *why = "the access chain's index is not the constant 0 (.x)";
    if (cix < 0 || (w[off[cix]] & 0xffff) != 43 || w[off[cix] + 3] != 0) goto fail;
    long ri = fp_def(w, off, ni, reg);
    *why = "the register is not an OpVariable";
    if (ri < 0 || (w[off[ri]] & 0xffff) != 59) goto fail;
    long pi = fp_def(w, off, ni, w[off[ri] + 1]);
    *why = "the register's type is not a pointer to a 4-vector of int";
    if (pi < 0 || (w[off[pi]] & 0xffff) != 32) goto fail;
    uint32_t i4 = w[off[pi] + 3];
    long i4i = fp_def(w, off, ni, i4);
    if (i4i < 0 || (w[off[i4i]] & 0xffff) != 23 || w[off[i4i] + 2] != ti || w[off[i4i] + 3] != 4) goto fail;
    /* write: the header (bound + 4), everything before %V, the four new, %V from the length, the rest */
    size_t add = 4 + 4 + 8 + 6, n = nw + add;
    uint32_t *o = malloc(n * sizeof *o), b = w[3];
    *why = "no memory";
    if (!o) goto fail;
    size_t at = off[vi], j = 0;
    memcpy(o, w, at * sizeof *o);
    o[3] = b + 4;
    j = at;
    o[j++] = (4u << 16) | 61, o[j++] = i4, o[j++] = b, o[j++] = reg;                          /* %a = OpLoad %int4 %rN */
    o[j++] = (4u << 16) | 124, o[j++] = f4, o[j++] = b + 1, o[j++] = b;                       /* %b = OpBitcast %float4 %a */
    o[j++] = (8u << 16) | 79, o[j++] = f3, o[j++] = b + 2, o[j++] = b + 1, o[j++] = b + 1;    /* %c = OpVectorShuffle */
    o[j++] = 0, o[j++] = 1, o[j++] = 2;
    o[j++] = (6u << 16) | 12, o[j++] = tf, o[j++] = b + 3, o[j++] = glsl, o[j++] = 66, o[j++] = b + 2; /* Length */
    memcpy(o + j, w + at, (nw - at) * sizeof *o);
    o[j + 3] = b + 3; /* %V = OpBitcast %uint %d */
    free(off);
    *n_out = n;
    *why = NULL;
    return o;
fail:
    free(off);
    return NULL;
}

/* ==== build 33 (docs/re-xr-pipeline.md 146.3): SOMVR_XR_FOG=ray - the fog's density averaged along the eye's ray =====
 * The same edit as win/somvr_fogdxbc.h's somvr_fogdxbc_ray, on IndirectX's translation (one store per DXBC register
 * write): after the height's movc (`rH = shuffle(rH, movc(cond, sky arm, pixel height))`) dh = max(h1,0) - max(h0,0)
 * (0 in the sky: the cond's component set); after each density's `rA.a = exp2(rA.a)` (Rayleigh and Mie, found from the
 * two `rX.x = -rH.c / H` divisions after `rH.c = max(rH.c, 0)`): rA.a *= f(x), x = clamp(dh / H, -80, 80),
 * f = (2^(x log2 e) - 1) / x, 1 + x/2 for |x| <= 0.01. h0 = the shader's own sum cb0[11].y + cb12[18].w. Every link is
 * checked first; NULL (and *why) leaves the module as it was. New ids from the bound, seven float constants added before
 * the first function; the rest of the module word for word. */
static long fp_def_any(const uint32_t *w, const uint32_t *off, uint32_t ni, uint32_t id) {
    for (uint32_t k = 0; k < ni; k++) {
        uint32_t op = w[off[k]] & 0xffff, wc = w[off[k]] >> 16;
        int s = -1;
        switch (op) {
        case 21: case 22: case 23: case 20: case 32: case 11: case 59: s = op == 59 ? 1 : 0; break;
        case 43: case 61: case 65: case 124: case 79: case 12: case 81: case 57: case 127: case 129: case 131:
        case 133: case 136: case 169: case 171: case 184: s = 1; break;
        default: s = -1;
        }
        if (s >= 0 && (uint32_t)s + 1 < wc && w[off[k] + 1 + s] == id) return (long)k;
    }
    return -1;
}
#define FP_OP(k) (w[off[k]] & 0xffff)
#define FP_WC(k) (w[off[k]] >> 16)
#define FP_A(k, i) (w[off[k] + 1 + (i)])
/* a constant's 32-bit value; 0 = not an OpConstant */
static int fp_cval(const uint32_t *w, const uint32_t *off, uint32_t ni, uint32_t id, uint32_t *v) {
    long k = fp_def_any(w, off, ni, id);
    if (k < 0 || FP_OP(k) != 43 || FP_WC(k) != 4) return 0;
    *v = FP_A(k, 2);
    return 1;
}
/* id = OpBitcast %float (OpLoad (OpAccessChain %ptr %reg %c)): reg, c's value, the chain's pointer type */
static int fp_regload(const uint32_t *w, const uint32_t *off, uint32_t ni, uint32_t id, uint32_t tf, uint32_t *reg,
                      uint32_t *cval) {
    long b = fp_def_any(w, off, ni, id);
    if (b < 0 || FP_OP(b) != 124 || FP_A(b, 0) != tf) return 0;
    long l = fp_def_any(w, off, ni, FP_A(b, 2));
    if (l < 0 || FP_OP(l) != 61) return 0;
    long a = fp_def_any(w, off, ni, FP_A(l, 2));
    if (a < 0 || FP_OP(a) != 65 || FP_WC(a) != 5) return 0;
    *reg = FP_A(a, 2);
    return fp_cval(w, off, ni, FP_A(a, 3), cval);
}
/* id = OpLoad %float (OpAccessChain %ptr %var %i0 %i1 %i2) with i1, i2 these values */
static int fp_cbload(const uint32_t *w, const uint32_t *off, uint32_t ni, uint32_t id, uint32_t i1, uint32_t i2) {
    long l = fp_def_any(w, off, ni, id);
    if (l < 0 || FP_OP(l) != 61) return 0;
    long a = fp_def_any(w, off, ni, FP_A(l, 2));
    uint32_t v1, v2;
    return a >= 0 && FP_OP(a) == 65 && FP_WC(a) == 7 && fp_cval(w, off, ni, FP_A(a, 4), &v1) && v1 == i1 &&
           fp_cval(w, off, ni, FP_A(a, 5), &v2) && v2 == i2;
}
/* the store at k writes reg component c (OpStore (OpAccessChain %ptr reg %c) value): 1 */
static int fp_store_to(const uint32_t *w, const uint32_t *off, uint32_t ni, uint32_t k, uint32_t reg, uint32_t c) {
    if (FP_OP(k) != 62) return 0;
    long a = fp_def_any(w, off, ni, FP_A(k, 0));
    uint32_t v;
    return a >= 0 && FP_OP(a) == 65 && FP_WC(a) == 5 && FP_A(a, 2) == reg && fp_cval(w, off, ni, FP_A(a, 3), &v) && v == c;
}

static uint32_t *somvr_fogpatch_ray(const uint32_t *w, size_t nw, size_t *n_out, const char **why) {
    *n_out = 0;
    *why = "not SPIR-V";
    if (nw < 6 || w[0] != 0x07230203u) return NULL;
    uint32_t *off = malloc(nw * sizeof *off), ni = 0;
    if (!off) return (*why = "no memory", NULL);
    for (size_t i = 5; i < nw;) {
        uint32_t wc = w[i] >> 16;
        if (!wc || i + wc > nw) return (free(off), *why = "a broken instruction", NULL);
        off[ni++] = (uint32_t)i;
        i += wc;
    }
    uint32_t tf = 0, tu = 0, tb = 0, glsl = 0, f0 = 0, u0 = 0, firstfn = 0;
    for (uint32_t k = 0; k < ni; k++) {
        const uint32_t *a = w + off[k] + 1;
        uint32_t op = FP_OP(k), wc = FP_WC(k);
        if (op == 22 && wc == 3 && a[1] == 32) tf = a[0];
        else if (op == 21 && wc == 4 && a[1] == 32 && !a[2]) tu = a[0];
        else if (op == 20 && wc == 2) tb = a[0];
        else if (op == 11 && wc >= 5 && !memcmp(a + 1, "GLSL.std.450", 12)) glsl = a[0];
        else if (op == 54 && !firstfn) firstfn = k;
    }
    for (uint32_t k = 0; k < firstfn; k++)
        if (FP_OP(k) == 43 && FP_WC(k) == 4) {
            if (FP_A(k, 0) == tf && FP_A(k, 2) == 0) f0 = FP_A(k, 1);
            if (FP_A(k, 0) == tu && FP_A(k, 2) == 0) u0 = FP_A(k, 1);
        }
    *why = "no float/uint/bool types, GLSL.std.450, 0.0 or 0u";
    if (!tf || !tu || !tb || !glsl || !firstfn || !f0 || !u0) goto fail;
    /* the eye's height: FAdd(load cb[.][11][1], load cb[.][18][3]) */
    uint32_t eyeh = 0;
    for (uint32_t k = firstfn; k < ni && !eyeh; k++)
        if (FP_OP(k) == 129 && FP_WC(k) == 5 && FP_A(k, 0) == tf && fp_cbload(w, off, ni, FP_A(k, 2), 11, 1) &&
            fp_cbload(w, off, ni, FP_A(k, 3), 18, 3))
            eyeh = FP_A(k, 1);
    *why = "no eye height (cb0[11].y + cb12[18].w)";
    if (!eyeh) goto fail;
    /* rH.c = max(rH.c, 0.0), then the two -rH.c / H divisions stored to rA.a / rB.b */
    long mx = -1;
    uint32_t rH = 0, hc = 0, dreg[2], dcomp[2], dH[2];
    for (uint32_t k = firstfn; k < ni && mx < 0; k++) {
        if (FP_OP(k) != 12 || FP_WC(k) != 7 || FP_A(k, 0) != tf || FP_A(k, 2) != glsl || FP_A(k, 3) != 40 || FP_A(k, 5) != f0)
            continue;
        uint32_t reg, c;
        if (!fp_regload(w, off, ni, FP_A(k, 4), tf, &reg, &c)) continue;
        /* its store: bitcast, access chain, store to reg.c within 3 */
        int stored = 0;
        for (uint32_t q = k + 1; q < ni && q < k + 4 && !stored; q++) stored = fp_store_to(w, off, ni, q, reg, c);
        if (!stored) continue;
        int nd = 0;
        for (uint32_t q = k + 1; q < ni && q < k + 60 && nd < 2; q++) {
            if (FP_OP(q) != 136 || FP_WC(q) != 5 || FP_A(q, 0) != tf) continue;
            long ng = fp_def_any(w, off, ni, FP_A(q, 2));
            uint32_t r2, c2;
            if (ng < 0 || FP_OP(ng) != 127 || !fp_regload(w, off, ni, FP_A(ng, 2), tf, &r2, &c2) || r2 != reg || c2 != c) continue;
            /* the division's store target */
            for (uint32_t s = q + 1; s < ni && s < q + 4; s++)
                if (FP_OP(s) == 62) {
                    long a = fp_def_any(w, off, ni, FP_A(s, 0));
                    uint32_t v;
                    if (a >= 0 && FP_OP(a) == 65 && FP_WC(a) == 5 && fp_cval(w, off, ni, FP_A(a, 3), &v)) {
                        dreg[nd] = FP_A(a, 2), dcomp[nd] = v, dH[nd] = FP_A(q, 3), nd++;
                    }
                    break;
                }
        }
        if (nd == 2) mx = (long)k, rH = reg, hc = c;
    }
    *why = "no `rH.c = max(rH.c, 0)` with the two densities' `-rH.c / H` after it";
    if (mx < 0) goto fail;
    /* each density's exp2 store: rA.a = bits(exp2(bits(rA.a))) */
    long es[2] = {-1, -1};
    uint32_t e1[2] = {0, 0}, ptrT = 0, tint = 0;
    for (int d = 0; d < 2; d++)
        for (uint32_t k = (uint32_t)mx + 1; k < ni && k < (uint32_t)mx + 120 && es[d] < 0; k++) {
            uint32_t reg, c;
            if (FP_OP(k) != 12 || FP_WC(k) != 6 || FP_A(k, 2) != glsl || FP_A(k, 3) != 29 ||
                !fp_regload(w, off, ni, FP_A(k, 4), tf, &reg, &c) || reg != dreg[d] || c != dcomp[d])
                continue;
            for (uint32_t s = k + 1; s < ni && s < k + 4; s++)
                if (fp_store_to(w, off, ni, s, reg, c)) {
                    long a = fp_def_any(w, off, ni, FP_A(s, 0)), b = fp_def_any(w, off, ni, FP_A(s, 1));
                    if (a < 0 || b < 0 || FP_OP(b) != 124 || FP_A(b, 2) != FP_A(k, 1)) break;
                    es[d] = (long)s, e1[d] = FP_A(k, 1), ptrT = FP_A(a, 0), tint = FP_A(b, 0);
                    break;
                }
        }
    *why = "a density's exp2 not found";
    if (es[0] < 0 || es[1] < 0) goto fail;
    /* the height's movc: the last store into rH before the max: OpStore %rH (shuffle(load rH, bitcast(call))) */
    long ms = -1;
    uint32_t v0 = 0, v2 = 0, kk = 0, vec = 0;
    for (long k = mx - 1; k > (long)firstfn && k > mx - 60 && ms < 0; k--) {
        if (FP_OP(k) != 62) continue;
        uint32_t ptr = FP_A(k, 0), val = FP_A(k, 1), call = 0;
        long sh = fp_def_any(w, off, ni, val);
        if (ptr == rH && sh >= 0 && FP_OP(sh) == 79 && FP_WC(sh) == 5 + 4) {
            uint32_t lit = FP_A(sh, 4 + hc), nb;
            long bc = fp_def_any(w, off, ni, FP_A(sh, 3));
            if (lit < 4 || bc < 0 || FP_OP(bc) != 124) break;
            kk = lit - 4, vec = 1, call = FP_A(bc, 2);
            (void)nb;
        } else if (fp_store_to(w, off, ni, (uint32_t)k, rH, hc) && sh >= 0 && FP_OP(sh) == 124) {
            kk = 0, vec = 0, call = FP_A(sh, 2);
        } else if (fp_store_to(w, off, ni, (uint32_t)k, rH, hc) || ptr == rH)
            break; /* rH.c written by something else first */
        else
            continue;
        long cl = fp_def_any(w, off, ni, call);
        if (cl < 0 || FP_OP(cl) != 57 || FP_WC(cl) != 7) break;
        /* the params' last stores before the call */
        for (int p = 0; p < 3; p += 2) {
            uint32_t pv = FP_A(cl, 3 + p), got = 0;
            for (long s = cl - 1; s > (long)firstfn && s > cl - 20 && !got; s--)
                if (FP_OP(s) == 62 && FP_A(s, 0) == pv) got = FP_A(s, 1);
            if (p == 0) v0 = got; else v2 = got;
        }
        if (!v0 || !v2) break;
        ms = k;
    }
    *why = "no movc picking the height before its max";
    if (ms < 0) goto fail;
    /* ---- emit ---- */
    uint32_t b = w[3], nb = b;
    uint32_t cm80 = nb++, c80 = nb++, clog = nb++, c1 = nb++, chalf = nb++, c001 = nb++;
    uint32_t cons[6][4] = {{(4u << 16) | 43, tf, cm80, 0xc2a00000u}, {(4u << 16) | 43, tf, c80, 0x42a00000u},
                           {(4u << 16) | 43, tf, clog, 0x3fb8aa3bu}, {(4u << 16) | 43, tf, c1, 0x3f800000u},
                           {(4u << 16) | 43, tf, chalf, 0x3f000000u}, {(4u << 16) | 43, tf, c001, 0x3c23d70au}};
    uint32_t blk[3][96], nblk[3] = {0, 0, 0};
#define FP_E(B, ...) do { uint32_t _t[] = {__VA_ARGS__}; memcpy(blk[B] + nblk[B], _t, sizeof _t); nblk[B] += sizeof _t / 4; } while (0)
    uint32_t c0k = v0, c2k = v2;
    if (vec) {
        c0k = nb++, c2k = nb++;
        FP_E(0, (5u << 16) | 81, tu, c0k, v0, kk);
        FP_E(0, (5u << 16) | 81, tu, c2k, v2, kk);
    }
    uint32_t h1r = nb++, h1 = nb++, h0 = nb++, dd = nb++, sky = nb++, dh = nb++;
    FP_E(0, (4u << 16) | 124, tf, h1r, c2k);
    FP_E(0, (7u << 16) | 12, tf, h1, glsl, 40, h1r, f0);
    FP_E(0, (7u << 16) | 12, tf, h0, glsl, 40, eyeh, f0);
    FP_E(0, (5u << 16) | 131, tf, dd, h1, h0);
    FP_E(0, (5u << 16) | 171, tb, sky, c0k, u0);
    FP_E(0, (6u << 16) | 169, tf, dh, sky, f0, dd);
    for (int d = 0; d < 2; d++) {
        uint32_t x0 = nb++, x1 = nb++, x = nb++, xl = nb++, ex = nb++, num = nb++, q = nb++, xh = nb++, lin = nb++, ax = nb++,
                 big = nb++, fac = nb++, avg = nb++, bi = nb++, pa = nb++;
        FP_E(1 + d, (5u << 16) | 136, tf, x0, dh, dH[d]);
        FP_E(1 + d, (7u << 16) | 12, tf, x1, glsl, 40, x0, cm80);
        FP_E(1 + d, (7u << 16) | 12, tf, x, glsl, 37, x1, c80);
        FP_E(1 + d, (5u << 16) | 133, tf, xl, x, clog);
        FP_E(1 + d, (6u << 16) | 12, tf, ex, glsl, 29, xl);
        FP_E(1 + d, (5u << 16) | 131, tf, num, ex, c1);
        FP_E(1 + d, (5u << 16) | 136, tf, q, num, x);
        FP_E(1 + d, (5u << 16) | 133, tf, xh, x, chalf);
        FP_E(1 + d, (5u << 16) | 129, tf, lin, xh, c1);
        FP_E(1 + d, (6u << 16) | 12, tf, ax, glsl, 4, x);
        FP_E(1 + d, (5u << 16) | 184, tb, big, c001, ax);
        FP_E(1 + d, (6u << 16) | 169, tf, fac, big, q, lin);
        FP_E(1 + d, (5u << 16) | 133, tf, avg, e1[d], fac);
        FP_E(1 + d, (4u << 16) | 124, tint, bi, avg);
        long a = fp_def_any(w, off, ni, FP_A(es[d], 0));
        FP_E(1 + d, (5u << 16) | 65, ptrT, pa, dreg[d], FP_A(a, 3));
        FP_E(1 + d, (3u << 16) | 62, pa, bi);
    }
#undef FP_E
    size_t add = 24 + nblk[0] + nblk[1] + nblk[2], n = nw + add, j = 0;
    uint32_t *o = malloc(n * sizeof *o);
    *why = "no memory";
    if (!o) goto fail;
    uint32_t after[3] = {(uint32_t)ms, (uint32_t)es[0], (uint32_t)es[1]};
    memcpy(o, w, 5 * sizeof *o), j = 5;
    o[3] = nb;
    for (uint32_t k = 0; k < ni; k++) {
        if (k == firstfn) for (int c = 0; c < 6; c++) memcpy(o + j, cons[c], 16), j += 4;
        memcpy(o + j, w + off[k], FP_WC(k) * sizeof *o), j += FP_WC(k);
        for (int q = 0; q < 3; q++)
            if (after[q] == k) memcpy(o + j, blk[q], nblk[q] * sizeof *o), j += nblk[q];
    }
    free(off);
    *n_out = j;
    *why = NULL;
    return o;
fail:
    free(off);
    return NULL;
}
#undef FP_OP
#undef FP_WC
#undef FP_A
#endif
