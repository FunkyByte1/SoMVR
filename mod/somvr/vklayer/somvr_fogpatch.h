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
#endif
