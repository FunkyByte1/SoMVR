/* test_fogpatch.c - build 31 (docs/re-xr-pipeline.md 137): somvr_fogpatch.h's edit on a synthetic module shaped like the
 * fog overlay's translation (rN an int4 register, rN.x loaded, bitcast to uint, stored as the movc's third parameter):
 * the new words in place, %V from the length, the bound up by 4, the rest word for word; every link of the chain checked
 * (a module with the .y loaded, a float load, another call id: nothing written). With two file arguments
 * (MODULE.spv CALL EXPECTED.spv: the layer's dump and shaders/fog_radial.py's output) the C edit must equal the Python. */
#include <stdio.h>
#include "../somvr_fogpatch.h"

static int fails;
#define EXPECT(c, ...)                                                                                                 \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

#define OP(wc, op) (((uint32_t)(wc) << 16) | (op))
static uint32_t mod[128];
static size_t build(uint32_t idx_const, uint32_t load_type) {
    size_t n = 0;
    uint32_t *m = mod;
    m[n++] = 0x07230203, m[n++] = 0x00010300, m[n++] = 0, m[n++] = 31, m[n++] = 0;
    m[n++] = OP(6, 11), m[n++] = 1; /* %1 = OpExtInstImport "GLSL.std.450" */
    memcpy(m + n, "GLSL.std.450\0\0\0", 16), n += 4;
    m[n++] = OP(3, 22), m[n++] = 2, m[n++] = 32;          /* %2 float */
    m[n++] = OP(4, 21), m[n++] = 3, m[n++] = 32, m[n++] = 1; /* %3 int */
    m[n++] = OP(4, 21), m[n++] = 4, m[n++] = 32, m[n++] = 0; /* %4 uint */
    m[n++] = OP(4, 23), m[n++] = 5, m[n++] = 2, m[n++] = 3;  /* %5 vec3 */
    m[n++] = OP(4, 23), m[n++] = 6, m[n++] = 2, m[n++] = 4;  /* %6 vec4 */
    m[n++] = OP(4, 23), m[n++] = 7, m[n++] = 3, m[n++] = 4;  /* %7 ivec4 */
    m[n++] = OP(4, 32), m[n++] = 8, m[n++] = 6, m[n++] = 7;  /* %8 Private ivec4* */
    m[n++] = OP(4, 59), m[n++] = 8, m[n++] = 9, m[n++] = 6;  /* %9 = r3 */
    m[n++] = OP(4, 32), m[n++] = 10, m[n++] = 6, m[n++] = 3; /* %10 Private int* */
    m[n++] = OP(4, 43), m[n++] = 3, m[n++] = 11, m[n++] = 0; /* %11 = 0 */
    m[n++] = OP(4, 43), m[n++] = 3, m[n++] = 21, m[n++] = 1; /* %21 = 1 */
    m[n++] = OP(4, 32), m[n++] = 12, m[n++] = 7, m[n++] = 4; /* %12 Function uint* */
    m[n++] = OP(4, 59), m[n++] = 12, m[n++] = 13, m[n++] = 7; /* %13 = param */
    m[n++] = OP(5, 65), m[n++] = 10, m[n++] = 14, m[n++] = 9, m[n++] = idx_const; /* %14 = &r3[idx] */
    m[n++] = OP(4, 61), m[n++] = load_type, m[n++] = 15, m[n++] = 14;              /* %15 = load */
    m[n++] = OP(4, 124), m[n++] = 4, m[n++] = 16, m[n++] = 15;                     /* %16 = bitcast uint */
    for (int i = 0; i < 3; i++) m[n++] = OP(3, 62), m[n++] = 13, m[n++] = 16;     /* store x3 */
    m[n++] = OP(7, 57), m[n++] = 4, m[n++] = 20, m[n++] = 30, m[n++] = 13, m[n++] = 13, m[n++] = 13; /* %20 = call */
    m[n++] = OP(2, 253), m[n++] = 20; /* (a tail) */
    return n;
}

int main(int argc, char **argv) {
    size_t n = build(11, 3), no = 0;
    const char *why = NULL;
    uint32_t *o = somvr_fogpatch(mod, n, 20, &no, &why);
    EXPECT(o && no == n + 22 && !why, "the edit: %p, %zu words (want %zu), %s", (void *)o, no, n + 22, why ? why : "-");
    if (o) {
        size_t at = 0;
        for (size_t i = 5; i < n; i += mod[i] >> 16)
            if ((mod[i] & 0xffff) == 124) at = i;
        EXPECT(o[3] == 35 && !memcmp(o, mod, 3 * 4) && !memcmp(o + 4, mod + 4, (at - 4) * 4), "the head / the words before");
        const uint32_t want[22] = {OP(4, 61), 7, 31, 9, OP(4, 124), 6, 32, 31, OP(8, 79), 5, 33, 32, 32, 0, 1, 2,
                                   OP(6, 12), 2, 34, 1, 66, 33};
        EXPECT(!memcmp(o + at, want, sizeof want), "the four new instructions");
        EXPECT(o[at + 22] == OP(4, 124) && o[at + 23] == 4 && o[at + 24] == 16 && o[at + 25] == 34,
               "%%16 = OpBitcast %%uint %%34: %#x %u %u %u", o[at + 22], o[at + 23], o[at + 24], o[at + 25]);
        EXPECT(!memcmp(o + at + 26, mod + at + 4, (n - at - 4) * 4), "the rest word for word");
        free(o);
    }
    n = build(21, 3);
    o = somvr_fogpatch(mod, n, 20, &no, &why);
    EXPECT(!o && why && strstr(why, "constant 0"), "the .y loaded: %s", why ? why : "patched");
    free(o);
    n = build(11, 2);
    o = somvr_fogpatch(mod, n, 20, &no, &why);
    EXPECT(!o && why && strstr(why, "OpLoad %int"), "a float load: %s", why ? why : "patched");
    free(o);
    n = build(11, 3);
    o = somvr_fogpatch(mod, n, 16, &no, &why);
    EXPECT(!o && why && strstr(why, "OpFunctionCall"), "another call id: %s", why ? why : "patched");
    free(o);
    o = somvr_fogpatch(mod, 4, 20, &no, &why);
    EXPECT(!o, "4 words patched");
    free(o);
    if (argc == 4) { /* the real module against shaders/fog_radial.py's output */
        FILE *f = fopen(argv[1], "rb"), *g = fopen(argv[3], "rb");
        static uint32_t a[65536], b[65536];
        size_t na = f ? fread(a, 4, 65536, f) : 0, nb = g ? fread(b, 4, 65536, g) : 0;
        if (f) fclose(f);
        if (g) fclose(g);
        o = somvr_fogpatch(a, na, (uint32_t)atoi(argv[2]), &no, &why);
        EXPECT(o && no == nb && !memcmp(o, b, nb * 4), "%s: the C edit (%zu words, %s) against the Python (%zu)", argv[1], no,
               why ? why : "-", nb);
        free(o);
    }
    printf("test_fogpatch: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
