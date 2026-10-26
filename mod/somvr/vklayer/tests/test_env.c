/* test_env.c - unit tests of somvr_env.c (SoM loading env, docs/re-xr-pipeline.md 14): the switches, the sky (no NaN,
 * the range, the gradient, the horizon glow, the ridges, Orodruin, the seam behind), the image (both kinds, byte orders,
 * sRGB/linear, the dither's bound, the guard row, the dim strip), the box (closed, the floor at floor height, the
 * quads' orientations), the world mode's dim quads (exactly the eye's frustum), the transitions (timing in frames, holds,
 * a stray frame, a hitch, a reversal). */
#include "../somvr_env.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

#define PI_F 3.14159265358979f

static float lum(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
static void ea_dir(float e, float a, float d[3]) { d[0] = cosf(e) * sinf(a), d[1] = sinf(e), d[2] = -cosf(e) * cosf(a); }
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}
static uint32_t rng = 12345;
static float frand(float lo, float hi) {
    rng = rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(rng >> 8) / 16777216.0f;
}
static void qrot(const float q[4], const float v[3], float out[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void test_parse(void) {
    int bad = 7;
    CHECK(somvr_env_parse(NULL, &bad) == SOMVR_ENV_OFF && !bad, "unset");
    CHECK(somvr_env_parse("", &bad) == SOMVR_ENV_OFF && !bad, "empty");
    CHECK(somvr_env_parse("off", &bad) == SOMVR_ENV_OFF && !bad && somvr_env_parse("0", &bad) == SOMVR_ENV_OFF && !bad,
          "off");
    CHECK(somvr_env_parse("on", &bad) == SOMVR_ENV_ON && !bad && somvr_env_parse("1", &bad) == SOMVR_ENV_ON &&
              somvr_env_parse("sky", &bad) == SOMVR_ENV_ON && !bad,
          "on");
    CHECK(somvr_env_parse("world", &bad) == SOMVR_ENV_WORLD && !bad, "world");
    CHECK(somvr_env_parse("sideways", &bad) == SOMVR_ENV_OFF && bad, "unknown: off + bad");
    CHECK(somvr_env_parse("World", &bad) == SOMVR_ENV_OFF && bad && somvr_env_parse("on ", &bad) == SOMVR_ENV_OFF && bad,
          "case and blanks matter");
    CHECK(somvr_env_parse_dim(NULL, &bad) == SOMVR_ENV_DIM_DEFAULT && !bad &&
              somvr_env_parse_dim("", &bad) == SOMVR_ENV_DIM_DEFAULT && !bad,
          "dim default");
    CHECK(somvr_env_parse_dim("0.25", &bad) == 0.25f && !bad && somvr_env_parse_dim("0", &bad) == 0.0f && !bad &&
              somvr_env_parse_dim("1", &bad) == 1.0f && !bad,
          "dim values");
    static const char *const no[] = {"-0.1", "1.5", "x", "0.3x", "nan", "inf", "-inf", "1e9"};
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++)
        CHECK(somvr_env_parse_dim(no[i], &bad) == SOMVR_ENV_DIM_DEFAULT && bad, "dim \"%s\" taken", no[i]);
}

static void test_sky(void) {
    /* finite, in range, subtle: every direction of a fine sweep and random ones */
    float maxc = 0, minc = 1;
    int finite = 1;
    for (int ie = -90; ie <= 90; ie++)
        for (int ia = -180; ia < 180; ia++) {
            float c[3];
            somvr_env_sky_ea((float)ie * PI_F / 180, (float)ia * PI_F / 180, c);
            for (int k = 0; k < 3; k++) {
                finite &= isfinite(c[k]);
                if (c[k] > maxc) maxc = c[k];
                if (c[k] < minc) minc = c[k];
            }
        }
    for (int i = 0; i < 20000; i++) {
        float d[3] = {frand(-1, 1), frand(-1, 1), frand(-1, 1)}, c[3];
        somvr_env_sky(d, c);
        for (int k = 0; k < 3; k++) {
            finite &= isfinite(c[k]);
            if (c[k] > maxc) maxc = c[k];
            if (c[k] < minc) minc = c[k];
        }
    }
    CHECK(finite, "a NaN or inf colour");
    CHECK(minc >= 0.0f && maxc <= 0.6f && maxc > 0.15f, "the range %.4f .. %.4f (want within 0 .. 0.6, not all black)",
          minc, maxc);
    /* bad inputs: still a colour */
    float c[3], d0[3] = {0, 0, 0}, dn[3] = {NAN, 1, 0}, di[3] = {INFINITY, 0, 0};
    somvr_env_sky(d0, c);
    CHECK(isfinite(c[0]) && isfinite(c[1]) && isfinite(c[2]), "zero direction");
    somvr_env_sky(dn, c);
    CHECK(isfinite(c[0]) && isfinite(c[1]) && isfinite(c[2]), "NaN direction");
    somvr_env_sky(di, c);
    CHECK(isfinite(c[0]) && isfinite(c[1]) && isfinite(c[2]), "inf direction");
    somvr_env_sky_ea(NAN, NAN, c);
    CHECK(isfinite(c[0]) && isfinite(c[1]) && isfinite(c[2]), "NaN elevation/azimuth");
    /* the direction form = the elevation/azimuth form */
    float worst = 0;
    for (int i = 0; i < 2000; i++) {
        float e = frand(-1.55f, 1.55f), a = frand(-3.1f, 3.1f), d[3], c1[3], c2[3];
        ea_dir(e, a, d);
        somvr_env_sky(d, c1);
        somvr_env_sky_ea(e, a, c2);
        for (int k = 0; k < 3; k++) worst = fmaxf(worst, fabsf(c1[k] - c2[k]));
    }
    CHECK(worst < 2e-4f, "somvr_env_sky(dir) vs _ea: %.6f", worst);
    /* the gradient: away from Orodruin, above the ridges, darker with every degree up to the zenith */
    for (float a = -3.0f; a < 0.5f; a += 0.5f) {
        float prev = 1, c2[3];
        int mono = 1;
        for (int ie = 5; ie <= 90; ie++) {
            somvr_env_sky_ea((float)ie * PI_F / 180, a, c2);
            mono &= lum(c2) <= prev + 1e-6f;
            prev = lum(c2);
        }
        CHECK(mono, "azimuth %.1f: the sky not darker toward the zenith", a);
    }
    float zen[3], low[3], mid[3], glow[3];
    somvr_env_sky_ea(PI_F / 2, 0, zen);
    somvr_env_sky_ea(30 * PI_F / 180, -2.0f, mid);
    somvr_env_sky_ea(5 * PI_F / 180, -2.0f, low);
    somvr_env_sky_ea(5 * PI_F / 180, 1.75f + 0.5f, glow); /* near Orodruin: the band is warmer */
    CHECK(lum(zen) < 0.03f && lum(zen) < lum(mid) && lum(mid) < lum(low) && lum(low) < lum(glow) && low[0] > low[2] &&
              low[0] > 1.8f * low[1],
          "zenith %.4f < 30 deg %.4f < 5 deg %.4f < 5 deg near Orodruin %.4f; the horizon red-brown (%.3f %.3f %.3f)",
          lum(zen), lum(mid), lum(low), lum(glow), low[0], low[1], low[2]);
    /* the ground: dim, darker straight down than at the horizon's haze; never brighter than the sky just above */
    float g1[3], g2[3], gn[3];
    somvr_env_sky_ea(-0.02f, -2.0f, g1);
    somvr_env_sky_ea(-0.3f, -2.0f, g2);
    somvr_env_sky_ea(-PI_F / 2, 0, gn);
    CHECK(lum(gn) < lum(g2) && lum(g2) < lum(g1) && lum(g1) < lum(low) && lum(gn) < 0.03f,
          "ground: nadir %.4f < -17 deg %.4f < -1 deg %.4f < the sky at 5 deg %.4f", lum(gn), lum(g2), lum(g1), lum(low));
    /* the ridges: at every azimuth, something darker than the sky right above the ridge line stands on the horizon */
    int ridged = 0, n_az = 0;
    for (float a = -3.1f; a < 3.1f; a += 0.05f, n_az++) {
        float s[3], m[3];
        somvr_env_sky_ea(0.12f, a, s); /* above every ridge (max 0.073, Orodruin 0.10) */
        somvr_env_sky_ea(0.004f, a, m); /* inside every ridge (min 0.006) */
        ridged += lum(m) < lum(s);
    }
    CHECK(ridged == n_az, "ridges darker than the sky above them at %d of %d azimuths", ridged, n_az);
    /* Orodruin: 100 deg to the right, its cone taller than the ridges (all below 0.075), the sky above it glowing */
    float cone[3], sky_off[3], glow_on[3], glow_opp[3];
    somvr_env_sky_ea(0.08f, 1.75f, cone);
    somvr_env_sky_ea(0.08f, 1.75f + 0.5f, sky_off);
    somvr_env_sky_ea(0.12f, 1.75f, glow_on);
    somvr_env_sky_ea(0.12f, 1.75f - PI_F, glow_opp);
    CHECK(lum(cone) < lum(sky_off) && glow_on[0] > glow_opp[0] + 0.1f,
          "Orodruin: the cone %.4f vs the sky beside it %.4f; its glow red %.3f vs opposite %.3f", lum(cone), lum(sky_off),
          glow_on[0], glow_opp[0]);
    /* the seam behind the player: the same colour either side of a = +-pi */
    worst = 0;
    for (int ie = -89; ie <= 89; ie++) {
        float c1[3], c2[3], e = (float)ie * PI_F / 180;
        somvr_env_sky_ea(e, PI_F - 1e-4f, c1);
        somvr_env_sky_ea(e, -PI_F + 1e-4f, c2);
        for (int k = 0; k < 3; k++) worst = fmaxf(worst, fabsf(c1[k] - c2[k]));
    }
    CHECK(worst < 2e-3f, "the seam behind: %.5f", worst);
}

static void test_image(void) {
    const uint32_t W = SOMVR_ENV_EQ_W, SH = SOMVR_ENV_EQ_H, H = somvr_env_img_h(SOMVR_ENV_KIND_EQ);
    CHECK(H == SH + 5 && somvr_env_img_w(SOMVR_ENV_KIND_EQ) == 1024 && somvr_env_img_w(SOMVR_ENV_KIND_CUBE) == 768 &&
              somvr_env_img_h(SOMVR_ENV_KIND_CUBE) == 517,
          "sizes");
    uint8_t *px = malloc((size_t)W * H * 4), *bg = malloc((size_t)W * H * 4), *lin = malloc((size_t)W * H * 4);
    double t0 = now_ms();
    somvr_env_image(px, SOMVR_ENV_KIND_EQ, 0, 1, 0.4f);
    double t_eq = now_ms() - t0;
    somvr_env_image(bg, SOMVR_ENV_KIND_EQ, 1, 1, 0.4f);
    somvr_env_image(lin, SOMVR_ENV_KIND_EQ, 0, 0, 0.4f);
    /* each texel = the sky at its centre's direction, rounded within the dither's +-1; alpha 255 */
    int off = 0, alpha = 1, swap = 1, darker = 1;
    double bias = 0;
    for (uint32_t y = 0; y < SH; y += 3)
        for (uint32_t x = 0; x < W; x += 3) {
            float e = (0.5f - ((float)y + 0.5f) / (float)SH) * PI_F, a = ((float)x + 0.5f) / (float)W * 2 * PI_F - PI_F;
            float c[3];
            somvr_env_sky_ea(e, a, c);
            const uint8_t *p = px + 4 * ((size_t)y * W + x), *q = bg + 4 * ((size_t)y * W + x);
            const uint8_t *l = lin + 4 * ((size_t)y * W + x);
            for (int k = 0; k < 3; k++) {
                float want = c[k] * 255;
                off += fabsf((float)p[k] - want) > 1.0f;
                bias += (double)p[k] - (double)want; /* floor(v + d), d uniform in [0, 1): unbiased */
                darker &= l[k] <= p[k];
            }
            alpha &= p[3] == 255 && q[3] == 255;
            swap &= q[0] == p[2] && q[1] == p[1] && q[2] == p[0];
        }
    double n = (double)(SH / 3 + 1) * (W / 3 + 1) * 3;
    CHECK(off == 0, "%d texel channels off the sky by more than the dither", off);
    CHECK(fabs(bias / n) < 0.03, "the dither's mean offset %.4f LSB", bias / n);
    CHECK(alpha && swap, "alpha 255 in the sky / BGRA = RGBA swapped");
    CHECK(darker, "the linear (UNORM) bytes are not darker than the sRGB ones");
    /* the rows: darker toward the zenith (row 0), the horizon brightest (the dither averages out) */
    double rowl[SOMVR_ENV_EQ_H];
    for (uint32_t y = 0; y < SH; y++) {
        double s = 0;
        for (uint32_t x = 0; x < W; x++) {
            const uint8_t *p = px + 4 * ((size_t)y * W + x);
            s += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        }
        rowl[y] = s / W;
    }
    int mono = 1;
    for (uint32_t y = 1; y < SH / 2 - 10; y++) mono &= rowl[y] >= rowl[y - 1] - 0.05;
    CHECK(mono && rowl[0] < rowl[SH / 4] && rowl[SH / 4] < rowl[SH / 2 - 4], "row brightness: zenith %.2f, 45 deg %.2f, "
          "near the horizon %.2f (monotonic %d)", rowl[0], rowl[SH / 4], rowl[SH / 2 - 4], mono);
    CHECK(rowl[SH - 1] < rowl[SH / 2 + 4], "the ground: nadir row %.2f vs below the horizon %.2f", rowl[SH - 1],
          rowl[SH / 2 + 4]);
    /* the seam: column 0 next to column W-1 */
    int seam = 0;
    for (uint32_t y = 0; y < SH; y++)
        for (int k = 0; k < 3; k++) seam += abs((int)px[4 * ((size_t)y * W) + k] - (int)px[4 * ((size_t)y * W + W - 1) + k]) > 2;
    CHECK(seam == 0, "the seam behind: %d channels differ by more than 2", seam);
    /* the guard row = the last sky row; the strip: premultiplied black at 1 - dim */
    CHECK(!memcmp(px + 4 * (size_t)SH * W, px + 4 * (size_t)(SH - 1) * W, 4 * (size_t)W), "the guard row");
    int strip = 1;
    for (uint32_t y = SH + 1; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            const uint8_t *p = px + 4 * ((size_t)y * W + x);
            strip &= !p[0] && !p[1] && !p[2] && p[3] == 153;
        }
    CHECK(strip, "the dim strip at 0.4: (0, 0, 0, 153)");
    int32_t rc[4];
    somvr_env_dim_rect(SOMVR_ENV_KIND_EQ, rc);
    CHECK(rc[0] >= 1 && rc[0] + rc[2] <= (int32_t)W - 1 && rc[1] >= (int32_t)SH + 2 && rc[1] + rc[3] <= (int32_t)H - 1 &&
              rc[2] == 2 && rc[3] == 2,
          "the dim rect %d,%d %dx%d: inside the strip with a texel to spare", rc[0], rc[1], rc[2], rc[3]);
    somvr_env_sky_rect(SOMVR_ENV_KIND_EQ, 3, rc);
    CHECK(rc[0] == 0 && rc[1] == 0 && rc[2] == (int32_t)W && rc[3] == (int32_t)SH, "the equirect rect");
    static const float dims[4] = {0.25f, 1.0f, 0.0f, NAN};
    static const uint8_t want_a[4] = {191, 0, 255, 153};
    for (int i = 0; i < 4; i++) {
        somvr_env_image(px, SOMVR_ENV_KIND_EQ, 0, 1, dims[i]);
        const uint8_t *p = px + 4 * ((size_t)(SH + 3) * W + 2);
        CHECK(p[3] == want_a[i] && !p[0], "dim %.2f: alpha %u (want %u)", dims[i], p[3], want_a[i]);
    }
    free(px), free(bg), free(lin);

    /* the cube atlas: each tile = its face seen from the centre */
    const uint32_t CW = SOMVR_ENV_CUBE_W, CH = somvr_env_img_h(SOMVR_ENV_KIND_CUBE), T = SOMVR_ENV_TILE;
    uint8_t *cb = malloc((size_t)CW * CH * 4);
    t0 = now_ms();
    somvr_env_image(cb, SOMVR_ENV_KIND_CUBE, 0, 1, 0.4f);
    double t_cube = now_ms() - t0;
    int bad_tile = 0, rect_ok = 1;
    for (int i = 0; i < 6; i++) {
        somvr_env_face f;
        somvr_env_face_get(i, &f);
        somvr_env_sky_rect(SOMVR_ENV_KIND_CUBE, i, rc);
        rect_ok &= rc[0] == (int32_t)((uint32_t)(i % 3) * T + 1) && rc[1] == (int32_t)((uint32_t)(i / 3) * T + 1) &&
                   rc[2] == (int32_t)T - 2 && rc[3] == (int32_t)T - 2;
        for (int k = 0; k < 64; k++) { /* inner texels at random: the colour of the direction through their centre */
            int tx = 1 + (int)frand(0, (float)T - 2.001f), ty = 1 + (int)frand(0, (float)T - 2.001f);
            float s = ((float)(tx - 1) + 0.5f) / (float)(T - 2) * 2 - 1, t = 1 - ((float)(ty - 1) + 0.5f) / (float)(T - 2) * 2;
            float p[3], c[3];
            somvr_env_face_point(&f, s, t, p);
            somvr_env_sky(p, c);
            const uint8_t *q = cb + 4 * ((size_t)(rc[1] - 1 + ty) * CW + (size_t)(rc[0] - 1 + tx));
            for (int ch = 0; ch < 3; ch++) bad_tile += fabsf((float)q[ch] - c[ch] * 255) > 1.0f;
            bad_tile += q[3] != 255;
        }
    }
    CHECK(rect_ok && bad_tile == 0, "cube tiles: rects %d, %d texels off their direction's colour", rect_ok, bad_tile);
    const uint8_t *p = cb + 4 * ((size_t)(CH - 2) * CW + 5);
    CHECK(!p[0] && !p[1] && !p[2] && p[3] == 153, "the cube's dim strip");
    somvr_env_dim_rect(SOMVR_ENV_KIND_CUBE, rc);
    CHECK(rc[1] == (int32_t)(2 * T) + 2, "the cube's dim rect row %d", rc[1]);
    free(cb);
    printf("  image fills: equirect %.1f ms, cube %.1f ms\n", t_eq, t_cube);
    CHECK(t_eq < 400 && t_cube < 800, "the fills are slow: %.1f / %.1f ms", t_eq, t_cube);
}

static void test_box(void) {
    static const float X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
    somvr_env_face f[6];
    for (int i = 0; i < 6; i++) somvr_env_face_get(i, &f[i]);
    for (int i = 0; i < 6; i++) {
        const float *q = f[i].pose.q;
        float nq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], r[3], u[3], n[3], err = 0;
        qrot(q, X, r), qrot(q, Y, u), qrot(q, Z, n);
        for (int k = 0; k < 3; k++)
            err = fmaxf(err, fmaxf(fabsf(r[k] - f[i].r[k]), fmaxf(fabsf(u[k] - f[i].u[k]), fabsf(n[k] - f[i].n[k]))));
        CHECK(fabsf(nq - 1) < 1e-5f && err < 1e-5f, "face %d: |q|^2 %.6f, the axes off by %.6f", i, nq, err);
        CHECK(dot3(f[i].n, f[i].c) < 0 && !memcmp(f[i].pose.p, f[i].c, sizeof f[i].c),
              "face %d faces away from the centre or sits elsewhere", i);
        float cr[3] = {f[i].r[1] * f[i].u[2] - f[i].r[2] * f[i].u[1], f[i].r[2] * f[i].u[0] - f[i].r[0] * f[i].u[2],
                       f[i].r[0] * f[i].u[1] - f[i].r[1] * f[i].u[0]};
        CHECK(dot3(cr, f[i].n) > 0.999f, "face %d: r x u is not n (the image would be mirrored)", i);
    }
    /* the floor at floor height, the walls down to it, ceiling and walls up to the radius */
    float pb[3], pt[3];
    somvr_env_face_point(&f[0], 0, -1, pb);
    somvr_env_face_point(&f[0], 0, 1, pt);
    CHECK(fabsf(f[5].c[1] + SOMVR_ENV_EYE_M) < 1e-6f && fabsf(pb[1] + SOMVR_ENV_EYE_M) < 1e-5f &&
              fabsf(pt[1] - SOMVR_ENV_RADIUS_M) < 1e-4f && fabsf(f[4].c[1] - SOMVR_ENV_RADIUS_M) < 1e-6f &&
              fabsf(pb[2] + SOMVR_ENV_RADIUS_M) < 1e-5f,
          "the box: floor %.3f, a wall from %.3f to %.3f at z %.3f", f[5].c[1], pb[1], pt[1], pb[2]);
    /* closed: every edge point of a face lies on the boundary of another face */
    int open = 0;
    for (int i = 0; i < 6; i++)
        for (int edge = 0; edge < 4; edge++)
            for (int k = 0; k <= 8; k++) {
                float v = -1 + 0.25f * (float)k, s = edge < 2 ? (edge ? 1.0f : -1.0f) : v, t = edge < 2 ? v : (edge == 2 ? -1.0f : 1.0f);
                float p[3];
                somvr_env_face_point(&f[i], s, t, p);
                int on = 0;
                for (int j = 0; j < 6; j++) {
                    if (j == i) continue;
                    float d[3] = {p[0] - f[j].c[0], p[1] - f[j].c[1], p[2] - f[j].c[2]};
                    float pl = dot3(d, f[j].n), sj = dot3(d, f[j].r) / f[j].hw, tj = dot3(d, f[j].u) / f[j].hh;
                    on += fabsf(pl) < 1e-3f && fabsf(sj) <= 1 + 1e-4f && fabsf(tj) <= 1 + 1e-4f &&
                          (fabsf(fabsf(sj) - 1) < 1e-4f || fabsf(fabsf(tj) - 1) < 1e-4f);
                }
                open += on == 0;
            }
    CHECK(open == 0, "the box is open at %d edge points", open);
    /* the face texels' colours meet across the edges (the guard texels continue the face) */
    float worst = 0;
    for (int k = 0; k <= 16; k++) {
        float t = -1 + 0.125f * (float)k, a[3], b[3], ca[3], cbb[3];
        somvr_env_face_point(&f[0], 1, t, a); /* ahead's right edge = right's left edge */
        somvr_env_face_point(&f[1], -1, t, b);
        somvr_env_sky(a, ca), somvr_env_sky(b, cbb);
        for (int c = 0; c < 3; c++) worst = fmaxf(worst, fabsf(ca[c] - cbb[c]) + fabsf(a[c] - b[c]));
    }
    CHECK(worst < 1e-4f, "ahead/right edge: %.6f", worst);
}

static void test_dim_quad(void) {
    float worst = 0, worst_q = 0;
    for (int i = 0; i < 500; i++) {
        float q[4] = {frand(-1, 1), frand(-1, 1), frand(-1, 1), frand(-1, 1)};
        float nq = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (int k = 0; k < 4; k++) q[k] /= nq;
        somvr_posef eye = {{q[0] * 3, q[1] * 3, q[2] * 3, q[3] * 3}, {frand(-2, 2), frand(0, 2), frand(-2, 2)}};
        somvr_fovf fov = {frand(-0.95f, -0.6f), frand(0.6f, 0.95f), frand(0.6f, 0.95f), frand(-0.95f, -0.6f)};
        float margin = i & 1 ? 1.01f : 1.0f, dist = i & 2 ? 20.0f : 5.0f, size[2];
        somvr_posef out;
        somvr_env_dim_quad(&eye, &fov, dist, margin, &out, size); /* an unnormalised eye quaternion is normalised */
        for (int k = 0; k < 4; k++) worst_q = fmaxf(worst_q, fabsf(out.q[k] - q[k]));
        float conj[4] = {-q[0], -q[1], -q[2], q[3]};
        float tl = tanf(fov.l), tr = tanf(fov.r), tu = tanf(fov.u), td = tanf(fov.d);
        float cx = 0.5f * (tl + tr), cy = 0.5f * (tu + td), hx = 0.5f * (tr - tl) * margin, hy = 0.5f * (tu - td) * margin;
        for (int c = 0; c < 4; c++) { /* each corner, seen from the eye: the fov's tangents (x margin about the centre) */
            float sx = c & 1 ? 1.0f : -1.0f, sy = c & 2 ? 1.0f : -1.0f;
            float loc[3] = {sx * 0.5f * size[0], sy * 0.5f * size[1], 0}, w[3], rel[3], e[3];
            qrot(out.q, loc, w);
            for (int k = 0; k < 3; k++) rel[k] = out.p[k] + w[k] - eye.p[k];
            qrot(conj, rel, e);
            worst = fmaxf(worst, fabsf(e[2] + dist));
            worst = fmaxf(worst, fabsf(e[0] / dist - (cx + sx * hx)));
            worst = fmaxf(worst, fabsf(e[1] / dist - (cy + sy * hy)));
        }
    }
    CHECK(worst < 2e-4f && worst_q < 1e-5f, "the dim quad's corners off the eye's frustum by %.6f (tangent units); "
          "turned %.6f off the eye", worst, worst_q);
    /* a broken fov: still a finite quad */
    somvr_posef eye = {{0, 0, 0, 1}, {0, 0, 0}}, out;
    somvr_fovf bad = {NAN, 3.0f, INFINITY, -2.0f};
    float size[2];
    somvr_env_dim_quad(&eye, &bad, 20, 1, &out, size);
    CHECK(isfinite(size[0]) && isfinite(size[1]) && size[0] > 0 && size[1] > 0 && isfinite(out.p[0]), "broken fov");
}

#define FRAME_NS 11111111ll
static void test_trans(void) {
    somvr_env_trans s;
    memset(&s, 0, sizeof s);
    int64_t t = 1000000000;
    int sw = 0;
    /* the first frame: its side at black; it fades in over 0.3 s (27 frames at 90 Hz) */
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 0, t, &sw) == SOMVR_ENVT_DRAW && sw && s.shown == SOMVR_ENV_SHOW_ENV &&
              somvr_env_level(&s) == 255,
          "first frame");
    int n = 0, mono = 1;
    uint32_t prev = 255;
    while (somvr_env_level(&s) && n < 100) {
        t += FRAME_NS, n++;
        CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 1, t, &sw) == SOMVR_ENVT_DRAW && !sw, "fading in: not a draw");
        mono &= somvr_env_level(&s) < prev;
        prev = somvr_env_level(&s);
    }
    CHECK(n >= 26 && n <= 28 && mono, "faded in after %d frames (want 27), monotonic %d", n, mono);
    /* the world wanted, the environment can be held: 18 hold frames getting darker, then the switch at black */
    n = 0, mono = 1, prev = 0;
    int r;
    for (;;) {
        t += FRAME_NS;
        r = somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t, &sw);
        if (r != SOMVR_ENVT_HOLD || n > 100) break;
        n++;
        mono &= somvr_env_level(&s) > prev && s.shown == SOMVR_ENV_SHOW_ENV;
        prev = somvr_env_level(&s);
    }
    CHECK(r == SOMVR_ENVT_DRAW && sw && s.shown == SOMVR_ENV_SHOW_WORLD && somvr_env_level(&s) == 255 && n >= 17 &&
              n <= 19 && mono && s.holds == (uint32_t)n && s.switches == 1,
          "out: %d hold frames (want 18), then the switch %d at level %u, monotonic %d", n, sw, somvr_env_level(&s), mono);
    for (int i = 0; i < 30; i++) t += FRAME_NS, somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t, &sw);
    CHECK(somvr_env_level(&s) == 0 && s.holds == 0, "the world faded in");
    /* a stray mono frame: one hold frame, a dip of one frame's worth, back without a switch */
    t += FRAME_NS;
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 1, t, &sw) == SOMVR_ENVT_HOLD && !sw && s.shown == SOMVR_ENV_SHOW_WORLD,
          "stray: not held");
    uint32_t dip = somvr_env_level(&s);
    t += FRAME_NS;
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t, &sw) == SOMVR_ENVT_DRAW && !sw && somvr_env_level(&s) < dip &&
              dip > 10 && dip < 18,
          "stray: dip %u, then %u", dip, somvr_env_level(&s));
    for (int i = 0; i < 3; i++) t += FRAME_NS, somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t, &sw);
    CHECK(somvr_env_level(&s) == 0 && s.switches == 1, "stray: back to 0 without a switch");
    /* nothing to hold: the switch at once, at black */
    t += FRAME_NS;
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 0, t, &sw) == SOMVR_ENVT_DRAW && sw && s.shown == SOMVR_ENV_SHOW_ENV &&
              somvr_env_level(&s) == 255 && s.switches == 2,
          "no hold: not switched at black");
    /* a reversal halfway out: back from where it is, no jump */
    for (int i = 0; i < 30; i++) t += FRAME_NS, somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 1, t, &sw);
    for (int i = 0; i < 9; i++) t += FRAME_NS, somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t, &sw);
    uint32_t half = somvr_env_level(&s);
    t += FRAME_NS;
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 1, t, &sw) == SOMVR_ENVT_DRAW && !sw && half > 115 && half < 140 &&
              somvr_env_level(&s) < half && somvr_env_level(&s) > half - 12,
          "reversal: %u -> %u", half, somvr_env_level(&s));
    /* a hitch: 2 s without a frame advances the fade by 0.1 s only; time going back: no step */
    for (int i = 0; i < 30; i++) t += FRAME_NS, somvr_env_step(&s, SOMVR_ENV_SHOW_ENV, 1, t, &sw);
    t += 2000000000ll;
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t, &sw) == SOMVR_ENVT_HOLD && fabsf(s.black - 0.5f) < 1e-5f,
          "hitch: black %.4f (want 0.5)", s.black);
    float b = s.black;
    CHECK(somvr_env_step(&s, SOMVR_ENV_SHOW_WORLD, 1, t - 50000000, &sw) == SOMVR_ENVT_HOLD && s.black == b,
          "time back: black %.4f -> %.4f", b, s.black);
}

int main(void) {
    test_parse();
    test_sky();
    test_image();
    test_box();
    test_dim_quad();
    test_trans();
    printf("test_env: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
