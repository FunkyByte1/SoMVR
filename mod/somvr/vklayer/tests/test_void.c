/* test_void.c - never a void (HG-9): somvr_void.c's switches, image, wave, level and places. */
#include "../somvr_void.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_n, g_fail;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        g_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            g_fail++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static float lin(uint8_t v) { /* sRGB byte -> linear */
    float s = v / 255.0f;
    return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
}
/* rotate v by q (x y z w) */
static void qrot(const float q[4], const float v[3], float o[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float c1[3] = {y * v[2] - z * v[1], z * v[0] - x * v[2], x * v[1] - y * v[0]};
    float c2[3] = {y * c1[2] - z * c1[1], z * c1[0] - x * c1[2], x * c1[1] - y * c1[0]};
    for (int i = 0; i < 3; i++) o[i] = v[i] + 2.0f * (w * c1[i] + c2[i]);
}

static void t_switches(void) {
    int bad = 0;
    CHECK(somvr_void_parse_onoff(NULL, &bad) == 0 && !bad, "unset");
    CHECK(somvr_void_parse_onoff("", &bad) == 0 && !bad, "empty");
    CHECK(somvr_void_parse_onoff("off", &bad) == 0 && somvr_void_parse_onoff("0", &bad) == 0 && !bad, "off");
    CHECK(somvr_void_parse_onoff("on", &bad) == 1 && somvr_void_parse_onoff("1", &bad) == 1 && !bad, "on");
    CHECK(somvr_void_parse_onoff("yes", &bad) == 0 && bad, "bad");
    bad = 0;
    CHECK(somvr_void_parse_ms(NULL, 100, 50, 1000, &bad) == 100 && !bad, "ms unset");
    CHECK(somvr_void_parse_ms("250", 100, 50, 1000, &bad) == 250 && !bad, "ms 250");
    CHECK(somvr_void_parse_ms("50", 100, 50, 1000, &bad) == 50 && somvr_void_parse_ms("1000", 100, 50, 1000, &bad) == 1000 &&
              !bad, "ms bounds");
    CHECK(somvr_void_parse_ms("49", 100, 50, 1000, &bad) == 100 && bad, "ms low");
    bad = 0;
    CHECK(somvr_void_parse_ms("1001", 100, 50, 1000, &bad) == 100 && bad, "ms high");
    bad = 0;
    CHECK(somvr_void_parse_ms("12x", 100, 50, 1000, &bad) == 100 && bad, "ms junk");
    bad = 0;
    CHECK(somvr_void_parse_ms("0", 1000, 0, 10000, &bad) == 0 && !bad, "space 0 (never)");
}

static void t_image(void) {
    size_t sz = (size_t)SOMVR_VOID_IMG_W * SOMVR_VOID_IMG_H * 4;
    uint8_t *a = malloc(sz), *b = malloc(sz), *c = malloc(sz);
    somvr_void_image(a, 0, 1);
    somvr_void_image(b, 1, 1);
    somvr_void_image(c, 0, 0);
    int premul = 1, swap = 1, nan = 0;
    for (size_t i = 0; i < sz; i += 4) {
        if (a[i] > a[i + 3] + 1 || a[i + 1] > a[i + 3] + 1 || a[i + 2] > a[i + 3] + 1) {
            /* sRGB-encoded premultiplied colour may exceed alpha in bytes; check in linear */
            float al = a[i + 3] / 255.0f;
            if (lin(a[i]) > al + 0.01f || lin(a[i + 1]) > al + 0.01f || lin(a[i + 2]) > al + 0.01f) {
                /* the lights: additive glow (rgb up to 1 at alpha 0.5): only there */
                if (i / 4 / SOMVR_VOID_IMG_W < SOMVR_VOID_FLOOR_PX) premul = 0;
            }
        }
        if (a[i] != b[i + 2] || a[i + 2] != b[i] || a[i + 1] != b[i + 1] || a[i + 3] != b[i + 3]) swap = 0;
    }
    CHECK(premul, "the floor is premultiplied (rgb <= alpha in linear)");
    CHECK(swap, "BGRA is RGBA with R and B swapped");
    (void)nan;
    /* the floor: solid in the middle, gone at the corners, dark (an average stone about sRGB 52 42 36), the rim fades */
    size_t mid = ((size_t)256 * SOMVR_VOID_IMG_W + 256) * 4, corner = ((size_t)2 * SOMVR_VOID_IMG_W + 2) * 4;
    CHECK(a[mid + 3] == 255, "the floor's centre is solid (%u)", a[mid + 3]);
    CHECK(a[corner + 3] == 0 && a[corner] == 0, "the floor's corner is transparent (%u)", a[corner + 3]);
    double sum[3] = {0}, n = 0;
    int maxc = 0;
    for (int y = 156; y < 356; y++)
        for (int x = 156; x < 356; x++) {
            const uint8_t *p = a + ((size_t)y * SOMVR_VOID_IMG_W + x) * 4;
            for (int k = 0; k < 3; k++) sum[k] += p[k], maxc = p[k] > maxc ? p[k] : maxc;
            n++;
        }
    CHECK(sum[0] / n > 35 && sum[0] / n < 65 && sum[0] > sum[1] && sum[1] > sum[2], "the floor's mean colour %.0f %.0f %.0f",
          sum[0] / n, sum[1] / n, sum[2] / n);
    CHECK(maxc < 90, "the floor stays dark (max %d)", maxc);
    /* the rim: alpha falls from the solid part (1.8 m) to nothing (3 m) along a radius */
    int mono = 1, prev = 256;
    for (int x = 256 + 154; x < 512; x++) { /* 1.8 m = 153.6 px */
        int al = a[((size_t)256 * SOMVR_VOID_IMG_W + x) * 4 + 3];
        if (al > prev) mono = 0;
        prev = al;
    }
    CHECK(mono && prev == 0, "the rim fades monotonically to 0 (%d)", prev);
    /* joints: a stone joint is darker than the stone next to it (x = 0 is a joint on an even row: z in 0..0.45) */
    float fl[4], st[4];
    somvr_void_floor_texel(0.5f, 0.5f + 0.2f / SOMVR_VOID_FLOOR_M, fl); /* x = 0: on a joint (an even row) */
    somvr_void_floor_texel(0.5f + 0.3f / SOMVR_VOID_FLOOR_M, 0.5f + 0.2f / SOMVR_VOID_FLOOR_M, st);
    CHECK(fl[0] < 0.6f * st[0], "a joint is darker than its stone (%.4f vs %.4f)", fl[0], st[0]);
    /* the linear image: the same alpha */
    int same_a = 1;
    for (size_t i = 3; i < sz; i += 4) same_a &= a[i] == c[i];
    CHECK(same_a, "sRGB and linear images share their alpha");
    /* the lights: every frame's guard border is transparent; brighter levels are brighter; the crest moves right */
    int guard = 1;
    double lv[SOMVR_VOID_LEVELS + 1] = {0};
    for (int level = 1; level <= SOMVR_VOID_LEVELS; level++)
        for (int wave = 0; wave < SOMVR_VOID_WAVES; wave++) {
            int32_t rc[4];
            somvr_void_lights_rect(wave, level, rc);
            CHECK(rc[0] >= 1 && rc[1] >= SOMVR_VOID_FLOOR_PX + 1 && rc[0] + rc[2] <= SOMVR_VOID_IMG_W - 1 &&
                      rc[1] + rc[3] <= SOMVR_VOID_IMG_H - 1 && rc[2] == SOMVR_VOID_LIGHT_W - 2 && rc[3] == SOMVR_VOID_LIGHT_H - 2,
                  "lights rect %d %d: %d,%d %dx%d", wave, level, rc[0], rc[1], rc[2], rc[3]);
            int x0 = rc[0] - 1, y0 = rc[1] - 1;
            for (int x = 0; x < SOMVR_VOID_LIGHT_W; x++)
                guard &= a[((size_t)y0 * SOMVR_VOID_IMG_W + x0 + x) * 4 + 3] == 0 &&
                         a[((size_t)(y0 + SOMVR_VOID_LIGHT_H - 1) * SOMVR_VOID_IMG_W + x0 + x) * 4 + 3] == 0;
            for (int y = 0; y < SOMVR_VOID_LIGHT_H; y++)
                guard &= a[((size_t)(y0 + y) * SOMVR_VOID_IMG_W + x0) * 4 + 3] == 0 &&
                         a[((size_t)(y0 + y) * SOMVR_VOID_IMG_W + x0 + SOMVR_VOID_LIGHT_W - 1) * 4 + 3] == 0;
            for (int y = 1; y < SOMVR_VOID_LIGHT_H - 1; y++)
                for (int x = 1; x < SOMVR_VOID_LIGHT_W - 1; x++) lv[level] += a[((size_t)(y0 + y) * SOMVR_VOID_IMG_W + x0 + x) * 4];
        }
    CHECK(guard, "every lights frame keeps a transparent 1-texel border");
    int up = 1;
    for (int k = 2; k <= SOMVR_VOID_LEVELS; k++) up &= lv[k] > lv[k - 1];
    CHECK(up && lv[1] > 0, "the levels get brighter (%.0f .. %.0f)", lv[1], lv[SOMVR_VOID_LEVELS]);
    int bright0 = 0, bright1 = 0;
    float best0 = 0, best1 = 0;
    for (int i = 0; i < SOMVR_VOID_NLIGHTS; i++) {
        float o0 = somvr_void_light_on(SOMVR_VOID_WAVES / 4, i), o1 = somvr_void_light_on(SOMVR_VOID_WAVES / 2, i);
        if (o0 > best0) best0 = o0, bright0 = i;
        if (o1 > best1) best1 = o1, bright1 = i;
    }
    CHECK(bright1 > bright0, "the crest runs left to right (%d then %d)", bright0, bright1);
    float txl[4];
    somvr_void_light_texel(0, SOMVR_VOID_LEVELS, 0.5f, 0.02f, txl);
    CHECK(txl[3] < 0.02f, "between and above the lights: nothing (%.3f)", txl[3]);
    int32_t fr[4];
    somvr_void_floor_rect(fr);
    CHECK(fr[0] == 1 && fr[1] == 1 && fr[2] == SOMVR_VOID_FLOOR_PX - 2 && fr[3] == SOMVR_VOID_FLOOR_PX - 2, "floor rect");
    free(a), free(b), free(c);
}

static void t_time(void) {
    CHECK(somvr_void_wave(0.0) == 0, "wave at 0");
    CHECK(somvr_void_wave(SOMVR_VOID_WAVE_S * 0.5) == SOMVR_VOID_WAVES / 2, "wave at half a period");
    CHECK(somvr_void_wave(SOMVR_VOID_WAVE_S * 1.0 + 0.01) == 0, "wave wraps");
    CHECK(somvr_void_wave(-0.01) == SOMVR_VOID_WAVES - 1, "wave before 0");
    CHECK(somvr_void_wave(NAN) == 0 && somvr_void_wave(INFINITY) == 0, "wave of nonsense");
    CHECK(somvr_void_wave(1e9 + 0.3) >= 0 && somvr_void_wave(1e9 + 0.3) < SOMVR_VOID_WAVES, "wave far out");
    float l = 0;
    int n = 0;
    while (l < 1.0f && n < 100) l = somvr_void_level_step(l, 1, 0.011f), n++;
    CHECK(n >= 26 && n <= 29, "the lights rise in 0.3 s (%d frames at 90 Hz)", n);
    n = 0;
    while (l > 0.0f && n < 400) l = somvr_void_level_step(l, 0, 0.011f), n++;
    CHECK(n >= 134 && n <= 138, "they fall in 1.5 s (%d frames)", n);
    CHECK(somvr_void_level_step(0.5f, 1, 5.0f) < 0.9f, "a hitch does not jump (%.2f)", somvr_void_level_step(0.5f, 1, 5.0f));
    CHECK(somvr_void_level_step(0.5f, 1, -1.0f) == 0.5f && somvr_void_level_step(NAN, 1, 0.0f) == 0.0f, "no time");
    CHECK(somvr_void_level_index(0.0f) == 0 && somvr_void_level_index(0.001f) == 1 &&
              somvr_void_level_index(1.0f) == SOMVR_VOID_LEVELS && somvr_void_level_index(0.5f) == SOMVR_VOID_LEVELS / 2,
          "level index");
}

static void t_places(void) {
    float head[3] = {0.3f, 0.05f, -0.2f};
    somvr_posef f, l;
    somvr_void_floor_pose(head, -1.6f, &f);
    float z[3] = {0, 0, 1}, y[3] = {0, 1, 0}, o[3];
    qrot(f.q, z, o);
    CHECK(fabsf(o[1] - 1.0f) < 1e-5f, "the floor faces up (%.3f %.3f %.3f)", o[0], o[1], o[2]);
    qrot(f.q, y, o);
    CHECK(fabsf(o[2] + 1.0f) < 1e-5f, "its top is ahead");
    CHECK(f.p[0] == head[0] && f.p[2] == head[2] && f.p[1] == -1.6f, "under the head");
    for (int k = -3; k <= 3; k++) {
        float yaw = 0.7f * (float)k;
        somvr_void_lights_pose(head, yaw, -1.6f, &l);
        float d = sqrtf((l.p[0] - head[0]) * (l.p[0] - head[0]) + (l.p[2] - head[2]) * (l.p[2] - head[2]));
        float ya = atan2f(l.p[0] - head[0], -(l.p[2] - head[2]));
        CHECK(fabsf(d - SOMVR_VOID_LIGHTS_AHEAD_M) < 1e-4f && fabsf(remainderf(ya - yaw, 6.2831853f)) < 1e-4f &&
                  fabsf(l.p[1] - (-1.6f + SOMVR_VOID_LIGHTS_LIFT_M)) < 1e-5f,
              "the lights %.1f m ahead along yaw %.2f (%.3f m at %.3f)", (double)SOMVR_VOID_LIGHTS_AHEAD_M, yaw, d, ya);
        float qn = l.q[0] * l.q[0] + l.q[1] * l.q[1] + l.q[2] * l.q[2] + l.q[3] * l.q[3];
        qrot(l.q, z, o);
        float e[3] = {head[0] - l.p[0], head[1] - l.p[1], head[2] - l.p[2]};
        float el = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        CHECK(fabsf(qn - 1.0f) < 1e-4f && fabsf(o[0] * e[0] / el + o[1] * e[1] / el + o[2] * e[2] / el - 1.0f) < 1e-4f,
              "the lights face the eyes (yaw %.2f: %.3f %.3f %.3f)", yaw, o[0], o[1], o[2]);
        float x[3] = {1, 0, 0};
        qrot(l.q, x, o);
        CHECK(fabsf(o[1]) < 1e-4f, "their row is level (%.4f)", o[1]);
        float q[4] = {0, sinf(-0.5f * yaw), 0, cosf(-0.5f * yaw)}; /* a head turned right by yaw (about -y... +y is left) */
        CHECK(fabsf(remainderf(somvr_void_yaw(q) - yaw, 6.2831853f)) < 1e-4f, "the yaw of a turned head (%.3f for %.3f)",
              somvr_void_yaw(q), yaw);
    }
}

/* the game's in-world loading cover: the grid, the shares, the hide/space machine */
static void t_cover(void) {
    int32_t x, y, xs[SOMVR_COVER_N], ys[SOMVR_COVER_N];
    int inside = 1;
    for (int k = 0; k < SOMVR_COVER_N; k++) {
        somvr_cover_point(k, 1920, 1080, &x, &y);
        xs[k] = x, ys[k] = y;
        inside &= x >= 0 && x < 1920 && y >= 0 && y < 1080;
    }
    CHECK(inside && xs[0] == 60 && ys[0] == 60 && xs[SOMVR_COVER_N - 1] == 1860 && ys[SOMVR_COVER_N - 1] == 1020 &&
              xs[1] - xs[0] == 120 && ys[SOMVR_COVER_NX] - ys[0] == 120,
          "the 16 x 9 grid's centres of a 1920 x 1080 image (%d,%d .. %d,%d)", xs[0], ys[0], xs[SOMVR_COVER_N - 1],
          ys[SOMVR_COVER_N - 1]);
    uint8_t px[SOMVR_COVER_N * 4];
    float b, d;
    memset(px, 0, sizeof px); /* the HUD's clear: transparent black */
    somvr_cover_measure(px, SOMVR_COVER_N, &b, &d);
    CHECK(b == 0 && d == 0, "transparent black is no cover (%.2f %.2f)", b, d);
    for (int i = 0; i < SOMVR_COVER_N; i++) px[4 * i + 3] = 255;
    for (int i = 0; i < 6; i++) px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 230; /* the spinner's white */
    somvr_cover_measure(px, SOMVR_COVER_N, &b, &d);
    CHECK(fabsf(b - 138.0f / 144) < 1e-6f && fabsf(d - b) < 1e-6f, "the cover with its spinner: %.3f / %.3f", b, d);
    for (int i = 0; i < SOMVR_COVER_N; i++) px[4 * i + 3] = 80; /* fading: 31 % black */
    somvr_cover_measure(px, SOMVR_COVER_N, &b, &d);
    CHECK(b == 0 && fabsf(d - 138.0f / 144) < 1e-6f, "a fading cover is dark, not opaque (%.2f %.2f)", b, d);
    memset(px, 0, sizeof px);
    for (int i = 0; i < SOMVR_COVER_N; i++) px[4 * i] = 200, px[4 * i + 3] = 255; /* an opaque red UI: not black */
    somvr_cover_measure(px, SOMVR_COVER_N, &b, &d);
    CHECK(b == 0 && d == 0, "opaque colour is no cover (%.2f %.2f)", b, d);
    for (int i = 0; i < SOMVR_COVER_N; i++) px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 0, px[4 * i + 3] = 255;
    for (int i = 0; i < 6; i++) px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 230; /* the spinner */
    CHECK(fabsf(somvr_cover_pure(px, SOMVR_COVER_N) - 138.0f / 144) < 1e-6f &&
              somvr_cover_pure(px, SOMVR_COVER_N) >= SOMVR_COVER_MENU_PURE,
          "the cover is pure black (%.3f)", somvr_cover_pure(px, SOMVR_COVER_N));
    for (int i = 0; i < SOMVR_COVER_N; i++) px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 20; /* a menu's dark backdrop */
    somvr_cover_measure(px, SOMVR_COVER_N, &b, &d);
    CHECK(somvr_cover_pure(px, SOMVR_COVER_N) == 0 && b == 1.0f, "a menu's dark (20) is opaque black, not pure (%.2f)", b);

    somvr_cover_state s;
    memset(&s, 0, sizeof s);
    somvr_cover_reset(&s);
    CHECK(!s.hide && !s.space && s.t_black < 0 && s.t_last < 0, "reset");
    CHECK(somvr_cover_step(&s, 10.0, 0, 0, 0, 0.3f) == 0 && !s.hide, "a clear HUD: nothing");
    CHECK(somvr_cover_step(&s, 10.1, 0.96f, 0.96f, 0, 0.3f) == SOMVR_COVER_CH_HIDE && s.hide && !s.space && s.n_hide == 1,
          "the first black reading hides the HUD at once");
    CHECK(somvr_cover_step(&s, 10.3, 0.96f, 0.96f, 0, 0.3f) == 0 && !s.space, "0.2 s of black: no load yet");
    CHECK(somvr_cover_step(&s, 10.4, 0.96f, 0.96f, 0, 0.3f) == SOMVR_COVER_CH_SPACE && s.space && s.n_space == 1,
          "0.3 s of black: a load");
    CHECK(somvr_cover_step(&s, 12.0, 0.6f, 0.96f, 0, 0.3f) == 0 && s.space, "lifting a little (0.6): still the load");
    CHECK(somvr_cover_step(&s, 12.1, 0.3f, 0.9f, 0, 0.3f) == SOMVR_COVER_CH_SPACE && !s.space && s.hide,
          "the cover lifting (0.3 opaque): back to the world, the HUD still hidden (0.9 dark)");
    CHECK(somvr_cover_step(&s, 12.3, 0.0f, 0.3f, 0, 0.3f) == 0 && s.hide, "still 0.3 dark: hidden");
    CHECK(somvr_cover_step(&s, 12.4, 0.0f, 0.1f, 0, 0.3f) == SOMVR_COVER_CH_HIDE && !s.hide, "gone: the HUD again");
    /* a short black (a 0.2 s cut) hides the HUD and is no load */
    somvr_cover_step(&s, 20.0, 0.9f, 0.9f, 0, 0.3f);
    somvr_cover_step(&s, 20.2, 0.9f, 0.9f, 0, 0.3f);
    CHECK(s.hide && !s.space, "a 0.2 s black: hidden, no load");
    somvr_cover_step(&s, 20.3, 0.0f, 0.0f, 0, 0.3f);
    CHECK(!s.hide && !s.space && s.t_black < 0, "... and over");
    /* a run broken by a lifted reading starts again */
    somvr_cover_step(&s, 30.0, 0.9f, 0.9f, 0, 0.3f);
    somvr_cover_step(&s, 30.2, 0.4f, 0.9f, 0, 0.3f);
    somvr_cover_step(&s, 30.4, 0.9f, 0.9f, 0, 0.3f);
    CHECK(!s.space, "the run starts again after a lifted reading");
    CHECK(somvr_cover_step(&s, 30.7, 0.9f, 0.9f, 0, 0.3f) == SOMVR_COVER_CH_SPACE && s.space, "0.3 s of the new run");
    /* a game menu: all off at once */
    CHECK(somvr_cover_step(&s, 31.0, 0, 0, 1, 0.3f) == (SOMVR_COVER_CH_HIDE | SOMVR_COVER_CH_SPACE) && !s.hide && !s.space,
          "a menu ends it");
    /* SOMVR_XR_COVER_MS=0: the first black reading is a load */
    CHECK(somvr_cover_step(&s, 40.0, 0.8f, 0.8f, 0, 0.0f) == (SOMVR_COVER_CH_HIDE | SOMVR_COVER_CH_SPACE) && s.space,
          "0 ms: a load at once");
    /* the age: no reading for 1 s ends it; nothing to end, nothing changes */
    CHECK(somvr_cover_age(&s, 40.9) == 0 && s.space, "0.9 s without a reading: kept");
    CHECK(somvr_cover_age(&s, 41.0) == (SOMVR_COVER_CH_HIDE | SOMVR_COVER_CH_SPACE) && !s.space && !s.hide,
          "1 s without a reading: off");
    CHECK(somvr_cover_age(&s, 50.0) == 0, "off stays off");
    CHECK(s.n_space == 3 && s.n_hide == 4, "the counts: %u loads, %u hides", s.n_space, s.n_hide);
    int bad = 0;
    CHECK(somvr_void_parse_ms("250", SOMVR_COVER_MS, 0, 5000, &bad) == 250 && !bad &&
              somvr_void_parse_ms("9000", SOMVR_COVER_MS, 0, 5000, &bad) == SOMVR_COVER_MS && bad,
          "SOMVR_XR_COVER_MS");
}

/* the in-world menus' backdrop: the panel's alpha, its image, the levels, its place */
static void t_back(void) {
    const float W = 0.72f + 2 * SOMVR_BACK_MARGIN_M, H = 0.405f + 2 * SOMVR_BACK_MARGIN_M;
    CHECK(fabsf(somvr_back_alpha(0, 0, W, H) - SOMVR_BACK_ALPHA) < 1e-6f, "the middle at full alpha");
    CHECK(fabsf(somvr_back_alpha(0.36f, 0.2025f, W, H) - SOMVR_BACK_ALPHA) < 1e-6f &&
              fabsf(somvr_back_alpha(-0.36f, 0.0f, W, H) - SOMVR_BACK_ALPHA) < 1e-6f &&
              fabsf(somvr_back_alpha(0.0f, -0.2025f, W, H) - SOMVR_BACK_ALPHA) < 1e-6f,
          "the board's corner and edges on the solid part (%.3f)", somvr_back_alpha(0.36f, 0.2025f, W, H));
    CHECK(somvr_back_alpha(0.5f * W, 0, W, H) < 1e-4f && somvr_back_alpha(0, 0.5f * H, W, H) < 1e-4f &&
              somvr_back_alpha(0.5f * W - 0.01f, 0.5f * H - 0.01f, W, H) == 0,
          "the outer edge and the rounded corner transparent");
    float prev = 1;
    int mono = 1;
    for (int i = 0; i <= 20; i++) { /* from the board's edge out: never brighter */
        float a = somvr_back_alpha(0.36f + 0.003f * (float)i, 0, W, H);
        mono &= a <= prev + 1e-6f;
        prev = a;
    }
    float mid = somvr_back_alpha(0.5f * W - 0.5f * SOMVR_BACK_SOFT_M, 0, W, H);
    CHECK(mono && mid > 0.3f * SOMVR_BACK_ALPHA && mid < 0.7f * SOMVR_BACK_ALPHA, "a soft rim (half way %.3f)", mid);
    uint8_t *img = malloc((size_t)SOMVR_BACK_IMG_W * SOMVR_BACK_IMG_H * 4);
    if (!img) return;
    somvr_back_image(img, 0, 1, W, H);
    int guard = 1, lvl = 1, dark = 1, prem = 1;
    uint8_t ca[SOMVR_BACK_LEVELS + 1] = {0};
    for (int k = 1; k <= SOMVR_BACK_LEVELS; k++) {
        int32_t rc[4];
        somvr_back_rect(k, rc);
        int x0 = rc[0] - 1, cx = x0 + SOMVR_BACK_CELL_W / 2, cy = SOMVR_BACK_CELL_H / 2;
        const uint8_t *c = img + ((size_t)cy * SOMVR_BACK_IMG_W + (size_t)cx) * 4;
        ca[k] = c[3];
        lvl &= fabsf(c[3] / 255.0f - SOMVR_BACK_ALPHA * (float)k / SOMVR_BACK_LEVELS) < 0.01f;
        dark &= lin(c[0]) <= 0.02f && lin(c[1]) <= 0.02f && lin(c[2]) <= 0.02f && c[2] >= c[0];
        for (int y = 0; y < SOMVR_BACK_CELL_H; y++)
            for (int x = 0; x < SOMVR_BACK_CELL_W; x++) {
                const uint8_t *p = img + ((size_t)y * SOMVR_BACK_IMG_W + (size_t)(x0 + x)) * 4;
                if (x == 0 || y == 0 || x == SOMVR_BACK_CELL_W - 1 || y == SOMVR_BACK_CELL_H - 1) guard &= p[3] == 0;
                prem &= lin(p[0]) <= p[3] / 255.0f + 1e-3f && lin(p[2]) <= p[3] / 255.0f + 1e-3f;
            }
        CHECK(rc[1] == 1 && rc[2] == SOMVR_BACK_CELL_W - 2 && rc[3] == SOMVR_BACK_CELL_H - 2, "cell %d's rect", k);
    }
    CHECK(guard, "every cell's border transparent (no bleeding between levels)");
    CHECK(lvl && ca[1] < ca[4] && ca[4] < ca[8], "the levels' alpha k/8 of %.2f (%u %u %u)", (double)SOMVR_BACK_ALPHA,
          ca[1], ca[4], ca[8]);
    CHECK(dark && prem, "a dark slate, premultiplied");
    uint8_t c0[4], c1[4];
    memcpy(c0, img + ((size_t)60 * SOMVR_BACK_IMG_W + 96) * 4, 4);
    somvr_back_image(img, 1, 1, W, H);
    memcpy(c1, img + ((size_t)60 * SOMVR_BACK_IMG_W + 96) * 4, 4);
    CHECK(c0[0] == c1[2] && c0[2] == c1[0] && c0[3] == c1[3], "BGRA = RGBA swapped");
    free(img);
    CHECK(somvr_back_index(0) == 0 && somvr_back_index(0.01f) == 1 && somvr_back_index(0.5f) == 4 &&
              somvr_back_index(1) == SOMVR_BACK_LEVELS && somvr_back_index(NAN) == 0,
          "the level's cell");
    float l = somvr_back_level_step(0, 1, 0.06f);
    CHECK(fabsf(l - 0.5f) < 1e-5f && somvr_back_level_step(l, 1, 0.1f) == 1 && somvr_back_level_step(0.0f, 1, 0.5f) < 0.84f,
          "the rise: 0.12 s, a hitch at most 0.1 s (%.3f)", l);
    CHECK(fabsf(somvr_back_level_step(1, 0, 0.075f) - 0.5f) < 1e-5f && somvr_back_level_step(0.1f, 0, 0.1f) == 0,
          "the fall: 0.15 s");
    somvr_posef b = {{0, sinf(0.3f), 0, cosf(0.3f)}, {0.1f, 1.5f, -0.6f}}, o;
    float bs[2] = {0.72f, 0.405f}, sz[2];
    somvr_back_place(&b, bs, &o, sz);
    float z[3] = {0, 0, 1}, n[3];
    qrot(b.q, z, n);
    CHECK(fabsf(sz[0] - W) < 1e-6f && fabsf(sz[1] - H) < 1e-6f && !memcmp(o.q, b.q, sizeof o.q) &&
              fabsf(o.p[0] - (b.p[0] - SOMVR_BACK_BEHIND_M * n[0])) < 1e-6f &&
              fabsf(o.p[2] - (b.p[2] - SOMVR_BACK_BEHIND_M * n[2])) < 1e-6f && fabsf(o.p[1] - b.p[1]) < 1e-6f,
          "the panel: the board's turn, 1 cm behind it, %.0f cm larger (%.3f x %.3f)", 200.0 * SOMVR_BACK_MARGIN_M, sz[0],
          sz[1]);
    int bad = 0;
    CHECK(somvr_back_parse_dim(NULL, SOMVR_BACK_DIM, &bad) == SOMVR_BACK_DIM && !bad &&
              somvr_back_parse_dim("0.6", SOMVR_BACK_DIM, &bad) == 0.6f && !bad &&
              somvr_back_parse_dim("0", SOMVR_BACK_DIM, &bad) == 0.0f && !bad,
          "SOMVR_XR_MENU_BACK_DIM");
    CHECK(somvr_back_parse_dim("1", SOMVR_BACK_DIM, &bad) == SOMVR_BACK_DIM && bad, "... 1 is too dark: the default");
}

int main(void) {
    t_switches();
    t_image();
    t_time();
    t_places();
    t_cover();
    t_back();
    printf("test_void: %d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
