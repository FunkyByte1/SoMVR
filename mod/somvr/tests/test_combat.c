/* test_combat.c - unit tests of somvr_combat.c: closest points of segments (hand cases and a brute-force comparison),
 * the swept blade against a capsule (through, above, tunnelling between frames, the contact point), the "heading at"
 * test, the windowed swing speed (crossing, travel, the end with hysteresis, stabs), the tiers at their boundaries,
 * the re-hit cooldown and the hit charges, the fake blade (geometry, speeds, velocity = the derivative of the tip) and,
 * end to end, the fake's two swings through an orc capsule: one heavy and one light hit per cycle. */
#include "../somvr_combat.h"
#include "swing_b19b.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define DEG(x) ((float)(x) * 3.14159265358979f / 180.0f)
#define HZ 90.0

static float len3(const float a[3]) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static float dist3(const float a[3], const float b[3]) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return len3(d);
}
static somvr_blade blade(float bx, float by, float bz, float tx, float ty, float tz, double t) {
    somvr_blade b = {1, {bx, by, bz}, {tx, ty, tz}, {0, 0, 0}, t, {0, 0, 0}, {0, 0, 0}};
    return b;
}

/* ---- closest points of two segments ---------------------------------------------------------------------------- */
static void test_seg(void) {
    float s, t, d;
    float a0[3] = {-1, 0, 0}, a1[3] = {1, 0, 0}, b0[3] = {0, 5, -1}, b1[3] = {0, 5, 1};
    d = sc_seg_dist(a0, a1, b0, b1, &s, &t);
    CHECK(NEAR(d, 5, 1e-6) && NEAR(s, 0.5, 1e-6) && NEAR(t, 0.5, 1e-6), "crossing: d %g s %g t %g", d, s, t);
    float p0[3] = {0, 0, 0}, p1[3] = {10, 0, 0}, q0[3] = {2, 3, 0}, q1[3] = {5, 3, 0};
    d = sc_seg_dist(p0, p1, q0, q1, &s, &t);
    CHECK(NEAR(d, 3, 1e-6), "parallel overlapping: d %g", d);
    float e0[3] = {0, 0, 0}, e1[3] = {1, 0, 0}, f0[3] = {3, 0, 0}, f1[3] = {4, 0, 0};
    d = sc_seg_dist(e0, e1, f0, f1, &s, &t);
    CHECK(NEAR(d, 2, 1e-6) && NEAR(s, 1, 1e-6) && NEAR(t, 0, 1e-6), "collinear apart: d %g s %g t %g", d, s, t);
    float g0[3] = {1, 2, 3};
    d = sc_seg_dist(g0, g0, f0, f0, &s, &t);
    CHECK(NEAR(d, sqrtf(4 + 4 + 9), 1e-5) && s == 0 && t == 0, "two points: d %g", d);
    float h0[3] = {-1, 2, 0}, h1[3] = {1, 2, 0};
    d = sc_seg_dist(e0, e0, h0, h1, &s, &t);
    CHECK(NEAR(d, 2, 1e-6) && NEAR(t, 0.5, 1e-6), "point vs segment: d %g t %g", d, t);
    d = sc_seg_dist(h0, h1, e0, e0, &s, &t);
    CHECK(NEAR(d, 2, 1e-6) && NEAR(s, 0.5, 1e-6), "segment vs point: d %g s %g", d, s);
    /* against a brute-force grid: never above it, never more than the grid's resolution below it */
    unsigned rs = 777;
    int worse = 0, below = 0;
    for (int k = 0; k < 300; k++) {
        float v[12];
        for (int i = 0; i < 12; i++) v[i] = (float)((rs = rs * 1103515245u + 12345u) >> 8 & 0xffff) / 655.36f - 50.0f;
        if (k % 10 == 0) memcpy(v + 9, v + 6, 3 * sizeof(float)); /* some degenerate second segments */
        if (k % 15 == 0) /* some parallel ones */
            for (int i = 0; i < 3; i++) v[9 + i] = v[6 + i] + (v[3 + i] - v[i]) * 0.7f;
        d = sc_seg_dist(v, v + 3, v + 6, v + 9, &s, &t);
        float best = 1e30f, step = 1.0f / 200;
        for (int i = 0; i <= 200; i++)
            for (int j = 0; j <= 200; j++) {
                float pa[3], pb[3];
                for (int c = 0; c < 3; c++) {
                    pa[c] = v[c] + (v[3 + c] - v[c]) * (float)i * step;
                    pb[c] = v[6 + c] + (v[9 + c] - v[6 + c]) * (float)j * step;
                }
                float x = dist3(pa, pb);
                best = x < best ? x : best;
            }
        float pa[3], pb[3];
        for (int c = 0; c < 3; c++) pa[c] = v[c] + (v[3 + c] - v[c]) * s, pb[c] = v[6 + c] + (v[9 + c] - v[6 + c]) * t;
        worse += d > best + 1e-3f;
        below += d < best - 0.6f || !NEAR(dist3(pa, pb), d, 1e-3); /* the grid step is <= 0.5 cm per coordinate */
    }
    CHECK(!worse && !below, "brute force: %d above the grid, %d far below it or s/t inconsistent", worse, below);
    printf("segments: crossing, parallel, collinear, points; 300 random pairs agree with a 201x201 grid\n");
}

/* ---- the swept blade against a capsule ------------------------------------------------------------------------- */
static void test_sweep(void) {
    float c0[3] = {0, 0, 110}, c1[3] = {0, 180, 110}; /* an upright orc capsule 110 cm ahead, r 35 */
    sc_contact ct;
    /* the blade level at 120 cm height, rotating from 30 deg right to 30 deg left about a pivot at the origin (both
     * ends 55 cm from the axis: outside) */
    somvr_blade b0 = blade(45 * sinf(DEG(30)), 120, 45 * cosf(DEG(30)), 135 * sinf(DEG(30)), 120, 135 * cosf(DEG(30)), 0);
    somvr_blade b1 = blade(-45 * sinf(DEG(30)), 120, 45 * cosf(DEG(30)), -135 * sinf(DEG(30)), 120, 135 * cosf(DEG(30)), 0.011);
    int hit = sc_sweep_capsule(&b0, &b1, 3, c0, c1, 35, 4, &ct);
    CHECK(hit && ct.u > 0 && ct.u < 1, "through: hit %d at u %g", hit, ct.u);
    float q[3] = {0, ct.point[1], 110};
    CHECK(hit && NEAR(dist3(ct.point, q), 35, 1e-3) && NEAR(ct.point[1], 120, 1e-3), "contact point %g %g %g not on the surface",
          ct.point[0], ct.point[1], ct.point[2]);
    CHECK(hit && ct.dist <= 38 && ct.along > 0.3f && ct.along < 1, "along %g dist %g", ct.along, ct.dist);
    /* the same sweep 2 m higher misses */
    somvr_blade h0 = b0, h1 = b1;
    h0.base[1] = h0.tip[1] = h1.base[1] = h1.tip[1] = 420;
    CHECK(!sc_sweep_capsule(&h0, &h1, 3, c0, c1, 35, 4, &ct), "a blade 2 m above the head hit");
    /* without a previous pose only this frame's pose counts: both ends outside = no contact */
    CHECK(!sc_sweep_capsule(NULL, &b1, 3, c0, c1, 35, 4, &ct), "cur alone (outside) hit");
    somvr_blade in = blade(0, 120, 40, 0, 120, 130, 1);
    CHECK(sc_sweep_capsule(NULL, &in, 3, c0, c1, 35, 4, &ct) && ct.u == 1, "cur alone (inside) missed");
    somvr_blade inv = b0;
    inv.valid = 0;
    CHECK(sc_sweep_capsule(&inv, &in, 3, c0, c1, 35, 4, &ct) && ct.u == 1, "an invalid prev is not 'cur alone'");
    CHECK(!sc_sweep_capsule(&b0, &inv, 3, c0, c1, 35, 4, &ct), "an invalid cur hit");
    /* tunnelling: a thin pole (r 2) the blade jumps across in one big frame (2 m sideways): one sub-step misses, 16 catch it */
    float p0[3] = {0, 0, 100}, p1[3] = {0, 200, 100};
    somvr_blade f0 = blade(-100, 100, 30, -100, 100, 150, 0), f1 = blade(100, 100, 30, 100, 100, 150, 0.011);
    int one = sc_sweep_capsule(&f0, &f1, 0, p0, p1, 2, 1, &ct), many = sc_sweep_capsule(&f0, &f1, 0, p0, p1, 2, 16, &ct);
    CHECK(!one && many, "tunnelling: 1 step %d, 16 steps %d (the sub-steps must matter)", one, many);
    /* the first touching sub-step is reported (not the deepest) */
    somvr_blade s0 = blade(200, 120, 0, 200, 120, 200, 0), s1 = blade(-200, 120, 0, -200, 120, 200, 0.011);
    CHECK(sc_sweep_capsule(&s0, &s1, 3, c0, c1, 35, 8, &ct) && NEAR(ct.u, 0.5, 1e-6), "first sub-step %g", ct.u);
    /* NaN never hits */
    somvr_blade nb = in;
    nb.tip[0] = NAN, nb.base[0] = NAN;
    CHECK(!sc_sweep_capsule(NULL, &nb, 3, c0, c1, 35, 4, &ct), "NaN hit");
    printf("sweep: through (point on the surface), above, cur alone, invalid, tunnelling 1 vs 16 steps, first sub-step\n");
}

static void test_heading(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    float c0[3] = {0, 0, 110}, c1[3] = {0, 180, 110}, d, cs;
    somvr_blade b = blade(43.5f, 120, 11.6f, 130.4f, 120, 34.9f, 0); /* the fake's start, 75 deg right */
    b.vel_tip[0] = -0.259f * 900, b.vel_tip[2] = 0.966f * 900;     /* swinging left */
    CHECK(sc_heading_at(&c, &b, c0, c1, 35, &d, &cs) && NEAR(d, 115.5, 0.5) && NEAR(cs, 0.706, 0.01), "toward: d %g cos %g",
          d, cs);
    b.vel_tip[0] = -b.vel_tip[0], b.vel_tip[2] = -b.vel_tip[2];
    CHECK(!sc_heading_at(&c, &b, c0, c1, 35, &d, &cs) && cs < 0, "away counted (cos %g)", cs);
    b.vel_tip[0] = b.vel_tip[2] = 0, b.vel_tip[1] = -900; /* straight down: no horizontal heading */
    CHECK(!sc_heading_at(&c, &b, c0, c1, 35, &d, &cs) && cs == 0, "vertical counted");
    somvr_blade far = blade(0, 120, -300, 0, 120, -200, 0);
    far.vel_tip[2] = 900;
    CHECK(!sc_heading_at(&c, &far, c0, c1, 35, &d, &cs) && NEAR(d, 275, 1e-3) && NEAR(cs, 1, 1e-6), "far: d %g", d);
    printf("heading: toward (115 cm, cos 0.71), away, vertical, too far\n");
}

/* ---- swing speed and tiers ------------------------------------------------------------------------------------- */
static somvr_blade moving(double t, float v_cmps, int along_axis) {
    somvr_blade b = blade(0, 100, 0, 0, 100, 90, t); /* the blade along +Z */
    if (along_axis)
        b.vel_tip[2] = v_cmps;
    else
        b.vel_tip[0] = v_cmps;
    return b;
}

static int push(sc_speed *s, const sc_cfg *c, somvr_blade b) { return sc_speed_push(s, c, &b); }

static void test_speed(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    sc_speed s;
    sc_speed_reset(&s);
    double t = 0;
    int edge = 0, i, start = -1, light_at = -1, end_at = -1;
    for (i = 0; i < 10; i++, t += 1 / HZ) CHECK(push(&s, &c, moving(t, 0, 0)) == 0, "still: edge");
    CHECK(s.mean_mps == 0 && !s.above && sc_tier(&c, &s) == SC_TIER_NONE, "still: tier");
    for (i = 0; i < 40; i++, t += 1 / HZ) {
        somvr_blade b = moving(t, 450, 0);
        edge = sc_speed_push(&s, &c, &b);
        if (edge == 1 && start < 0) start = i;
        if (sc_tier(&c, &s) == SC_TIER_LIGHT && light_at < 0) light_at = i;
        CHECK(sc_tier(&c, &s) != SC_TIER_HEAVY && edge != -1, "4.5 m/s: heavy or an end at %d", i);
    }
    /* the 80 ms mean crosses 3 m/s once 2/3 of the window is fast (~53 ms = the 5th fast sample), light after 15 cm */
    CHECK(start >= 3 && start <= 5, "4.5 m/s: the crossing at sample %d", start);
    CHECK(light_at > start && (light_at - start) >= 3 && (light_at - start) <= 4, "light %d samples after the crossing",
          light_at - start);
    CHECK(NEAR(s.mean_mps, 4.5, 1e-4) && NEAR(s.travel, 450.0 * (40 - 1 - start) / HZ, 0.5), "mean %g travel %g",
          s.mean_mps, s.travel);
    /* 2.5 m/s: inside the hysteresis (end below 2.25): the swing goes on, no slash tier below 3 m/s */
    for (i = 0; i < 12; i++, t += 1 / HZ) CHECK(push(&s, &c, moving(t, 250, 0)) == 0, "2.5: edge");
    CHECK(s.above && sc_tier(&c, &s) == SC_TIER_NONE, "2.5 m/s: above %d tier %d", s.above, sc_tier(&c, &s));
    for (i = 0; i < 12; i++, t += 1 / HZ)
        if (push(&s, &c, moving(t, 0, 0)) == -1 && end_at < 0) end_at = i;
    CHECK(end_at >= 0 && end_at <= 2 && !s.above, "the end at %d", end_at);
    /* 9 m/s: light from 15 cm, heavy from 40 cm */
    int heavy_at = -1;
    start = light_at = -1;
    for (i = 0; i < 30; i++, t += 1 / HZ) {
        edge = push(&s, &c, moving(t, 900, 0));
        if (edge == 1) start = i;
        int tr = sc_tier(&c, &s);
        if (tr == SC_TIER_LIGHT && light_at < 0) light_at = i;
        if (tr == SC_TIER_HEAVY && heavy_at < 0) heavy_at = i;
    }
    CHECK(start >= 1 && light_at > start && heavy_at > light_at, "9 m/s: crossing %d light %d heavy %d", start, light_at,
          heavy_at);
    CHECK(NEAR((heavy_at - start) * 900 / HZ, 40, 10.1), "heavy after %g cm", (heavy_at - start) * 900 / HZ);
    /* times that do not increase are ignored */
    float m0 = s.mean_mps;
    CHECK(push(&s, &c, moving(t - 1, 0, 0)) == 0 && s.mean_mps == m0, "an older sample counted");
    /* stabs: along the blade at 2.5 m/s (cos 1) vs 45 deg off (cos 0.71) */
    sc_speed_reset(&s);
    t = 100;
    for (i = 0; i < 10; i++, t += 1 / HZ) push(&s, &c, moving(t, 250, 1));
    CHECK(sc_tier(&c, &s) == SC_TIER_STAB && NEAR(s.axis_mps, 2.5, 1e-4) && NEAR(s.cos_axis, 1, 1e-6), "stab: tier %d",
          sc_tier(&c, &s));
    sc_speed_reset(&s);
    for (i = 0; i < 10; i++, t += 1 / HZ) {
        somvr_blade b = moving(t, 0, 0);
        b.vel_tip[0] = b.vel_tip[2] = 250;
        sc_speed_push(&s, &c, &b);
    }
    CHECK(sc_tier(&c, &s) != SC_TIER_STAB && NEAR(s.cos_axis, 0.7071, 1e-3), "45 deg off is a stab (cos %g)", s.cos_axis);
    /* the window: one fast sample among slow ones only counts for its own time */
    sc_speed_reset(&s);
    t = 200;
    for (i = 0; i < 20; i++, t += 1 / HZ) push(&s, &c, moving(t, i == 15 ? 1800 : 0, 0));
    CHECK(NEAR(s.mean_mps, 18.0 / HZ / 0.08, 1e-3) && !s.above, "a single 18 m/s sample 4 samples ago: mean %g, want 2.5",
          s.mean_mps);
    printf("speed: crossing at the 5th fast sample, light 15 cm later, hysteresis, end, heavy at 40 cm, stab, spike\n");
}

static void test_tiers(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    struct { float mean, travel, axis, cosv; int above, want; } T[] = {
        {5.99f, 100, 0, 0, 1, SC_TIER_LIGHT}, {6.0f, 39.9f, 0, 0, 1, SC_TIER_LIGHT}, {6.0f, 40, 0, 0, 1, SC_TIER_HEAVY},
        {3.0f, 15, 0, 0, 1, SC_TIER_LIGHT},   {3.0f, 14.9f, 0, 0, 1, SC_TIER_NONE}, {2.99f, 100, 0, 0, 1, SC_TIER_NONE},
        {9.0f, 100, 0, 0, 0, SC_TIER_NONE},   {0, 0, 1.8f, 0.8f, 0, SC_TIER_STAB},  {0, 0, 1.79f, 1, 0, SC_TIER_NONE},
        {9.0f, 100, 2.0f, 0.79f, 1, SC_TIER_HEAVY}, {9.0f, 100, 2.0f, 0.81f, 1, SC_TIER_STAB},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        sc_speed s;
        sc_speed_reset(&s);
        s.mean_mps = T[i].mean, s.travel = T[i].travel, s.axis_mps = T[i].axis, s.cos_axis = T[i].cosv, s.above = T[i].above;
        CHECK(sc_tier(&c, &s) == T[i].want, "case %zu: tier %s, want %s", i, sc_tier_name(sc_tier(&c, &s)),
              sc_tier_name(T[i].want));
    }
    CHECK(!strcmp(sc_tier_name(SC_TIER_HEAVY), "heavy") && !strcmp(sc_tier_name(9), "?"), "tier names");
    printf("tiers: %zu boundary cases (3/6 m/s, 15/40 cm, stab 1.8 m/s cos 0.8 first)\n", sizeof T / sizeof T[0]);
}

static void test_cool_charges(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    sc_cool k;
    sc_cool_reset(&k);
    sc_cool_mark(&k, 0xA, 10.0);
    CHECK(!sc_cool_ready(&k, 0xA, 10.1, c.cool_s) && !sc_cool_ready(&k, 0xA, 10.249, c.cool_s), "re-hit inside 0.25 s");
    CHECK(sc_cool_ready(&k, 0xA, 10.25, c.cool_s) && sc_cool_ready(&k, 0xB, 10.0, c.cool_s), "after 0.25 s / another orc");
    sc_cool_mark(&k, 0xA, 11.0);
    CHECK(!sc_cool_ready(&k, 0xA, 11.1, c.cool_s), "a new mark on the same key");
    for (uintptr_t i = 1; i <= SC_COOL_N; i++) sc_cool_mark(&k, 0x100 + i, 20.0 + (double)i * 0.001);
    CHECK(sc_cool_ready(&k, 0xA, 11.1, c.cool_s), "full table: the oldest mark (0xA) was not the one evicted");
    CHECK(!sc_cool_ready(&k, 0x100 + SC_COOL_N, 20.1, c.cool_s) && !sc_cool_ready(&k, 0x102, 20.1, c.cool_s),
          "full table: a recent mark got lost");
    CHECK(sc_cool_ready(&k, 0, 0, c.cool_s), "key 0 is never cooling");
    sc_charges ch;
    sc_charges_init(&ch, &c, 0);
    int took = 0;
    for (int i = 0; i < 4; i++) took += sc_charges_take(&ch, &c, 0.01 * i);
    CHECK(took == 3, "3 charges, took %d", took);
    CHECK(!sc_charges_take(&ch, &c, 0.2), "a charge at 0.2 s");
    CHECK(sc_charges_take(&ch, &c, 0.30) && !sc_charges_take(&ch, &c, 0.35), "one back after 0.25 s");
    CHECK(NEAR(sc_charges_now(&ch, &c, 100), 3, 1e-6), "capped at 3 (%g)", sc_charges_now(&ch, &c, 100));
    CHECK(NEAR(sc_charges_now(&ch, &c, 0.1), ch.n, 1e-6), "time going back regrows");
    printf("cooldown: 0.25 s per orc, other orcs free, eviction of the oldest; charges: 3, one back per 0.25 s, cap\n");
}

/* ---- the fake blade -------------------------------------------------------------------------------------------- */
static void test_fake(void) {
    float head[3] = {1000, 250, -500}, rate;
    somvr_blade b;
    somvr_blade_fake(50.1, 50.0, head, 0, &b);
    float pv[3] = {1000, 220, -500};
    CHECK(b.valid && NEAR(b.tip[1], 220, 1e-3) && NEAR(b.base[1], 220, 1e-3) && NEAR(dist3(b.tip, pv), 135, 1e-3) &&
              NEAR(dist3(b.base, pv), 45, 1e-3) && len3(b.vel_tip) == 0 && b.t == 50.1,
          "held: tip %g %g %g", b.tip[0], b.tip[1], b.tip[2]);
    CHECK(NEAR(b.tip[0] - 1000, 135 * sinf(DEG(75)), 1e-3) && NEAR(b.tip[2] + 500, 135 * cosf(DEG(75)), 1e-3),
          "held 75 deg right");
    CHECK(NEAR(somvr_blade_fake_angle(1.0, &rate), DEG(-75), 1e-5) && rate == 0, "after the heavy swing");
    CHECK(NEAR(somvr_blade_fake_angle(2.3, &rate), DEG(75), 1e-5) && rate == 0, "after the light swing");
    float a1 = somvr_blade_fake_angle(0.5, &rate), r1 = rate, a2 = somvr_blade_fake_angle(0.5 + 3 * SC_FAKE_PERIOD, &rate);
    CHECK(NEAR(a1, a2, 1e-4) && NEAR(r1, rate, 1e-4), "periodic");
    /* plateaus: the tip at 9 and 4.5 m/s, the velocity = the derivative of the tip (central difference) */
    double ts[2] = {0.5, 1.8};
    float want[2] = {900, 450};
    for (int k = 0; k < 2; k++) {
        somvr_blade m, p;
        float yaw = DEG(30);
        somvr_blade_fake(ts[k], 0, head, yaw, &b);
        somvr_blade_fake(ts[k] - 1e-4, 0, head, yaw, &m);
        somvr_blade_fake(ts[k] + 1e-4, 0, head, yaw, &p);
        float fd[3];
        for (int i = 0; i < 3; i++) fd[i] = (p.tip[i] - m.tip[i]) / 2e-4f;
        CHECK(NEAR(len3(b.vel_tip), want[k], 0.5) && NEAR(dist3(fd, b.vel_tip), 0, 2.0), "swing %d: |v| %g, fd %g %g %g vs %g %g %g", k,
              len3(b.vel_tip), fd[0], fd[1], fd[2], b.vel_tip[0], b.vel_tip[1], b.vel_tip[2]);
        CHECK(NEAR(b.vel_tip[1], 0, 1e-4), "swing %d: not level", k);
    }
    /* heading 90 deg (facing +X): mid-swing the blade points +X */
    float best = 10;
    double tmid = 0;
    for (double tt = 0.3; tt < 0.8; tt += 1e-4) {
        float a = fabsf(somvr_blade_fake_angle(tt, &rate));
        if (a < best) best = a, tmid = tt;
    }
    somvr_blade_fake(tmid, 0, head, DEG(90), &b);
    CHECK(NEAR(b.tip[0] - 1000, 135, 0.05) && NEAR(b.tip[2] + 500, 0, 0.5) && b.vel_tip[2] > 800, "yaw 90 mid-swing: tip %g %g v %g",
          b.tip[0], b.tip[2], b.vel_tip[2]);
    printf("fake: held 75 deg, plateaus 9.0/4.5 m/s = d(tip)/dt, level, periodic, yaw\n");
}

/* ---- the controller blade ------------------------------------------------------------------------------------------ */
static sc_grip grip_at(float x, float y, float z, float yaw, double xr_t) { /* a grip turned yaw about +Y */
    sc_grip g = {1, {x, y, z}, {0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw)}, 0, {0, 0, 0}, {0, 0, 0}, xr_t};
    return g;
}
static void test_controller(void) {
    sc_blade_cfg c;
    sc_blade_cfg_default(&c);
    CHECK(c.back_cm == 8 && c.len_cm == 100 && c.pitch_rad == 0 && !c.vel_runtime, "defaults (the tip at the model's)");
    c.len_cm = 90; /* the geometry below with a 90 cm tip */
    sc_rigxf id = {{0, 0, 0}, {0, 0, 0}, 0, 0};
    sc_blade_hist h = {0};
    somvr_blade b;
    /* identity grip at the origin: the blade along +Z, base 8 behind, tip 90 ahead; no velocity on the first sample */
    sc_grip g = grip_at(0, 0, 0, 0, 10.0);
    CHECK(somvr_blade_controller(&c, &g, &id, 5.0, &h, &b) && NEAR(b.base[2], -8, 1e-5) && NEAR(b.tip[2], 90, 1e-5) &&
              NEAR(b.tip[0], 0, 1e-5) && NEAR(b.tip[1], 0, 1e-5) && len3(b.vel_tip) == 0 && b.t == 5.0,
          "identity: base %g tip %g %g %g v %g", b.base[2], b.tip[0], b.tip[1], b.tip[2], len3(b.vel_tip));
    /* pitch +30 deg: the tip up; an offset in the grip's frame; a grip turned 90 deg right */
    sc_blade_cfg cp = c;
    cp.pitch_rad = DEG(30);
    h.have = 0;
    somvr_blade_controller(&cp, &g, &id, 5.0, &h, &b);
    CHECK(NEAR(b.tip[1], 90 * sinf(DEG(30)), 1e-4) && NEAR(b.tip[2], 90 * cosf(DEG(30)), 1e-4) &&
              NEAR(b.base[1], -8 * sinf(DEG(30)), 1e-4),
          "pitch 30: tip %g %g", b.tip[1], b.tip[2]);
    cp = c;
    cp.ofs_cm[0] = 2, cp.ofs_cm[2] = 5;
    g = grip_at(10, 100, -20, DEG(90), 10.0);
    h.have = 0;
    somvr_blade_controller(&cp, &g, &id, 5.0, &h, &b);
    CHECK(NEAR(b.tip[0], 10 + 5 + 90, 1e-3) && NEAR(b.tip[2], -20 - 2, 1e-3) && NEAR(b.tip[1], 100, 1e-4) &&
              NEAR(b.base[0], 10 + 5 - 8, 1e-3),
          "turned 90 + offset: tip %g %g %g base %g", b.tip[0], b.tip[1], b.tip[2], b.base[0]);
    /* the rig: tracking (p - p0) turned by yaw, then the base */
    sc_rigxf rig = {{500, 100, -50}, {0, 0, -30}, DEG(90), 0};
    g = grip_at(0, 0, 0, 0, 10.0);
    h.have = 0;
    somvr_blade_controller(&c, &g, &rig, 5.0, &h, &b);
    CHECK(NEAR(b.tip[0], 500 + 120, 1e-3) && NEAR(b.tip[2], -50, 1e-3) && NEAR(b.tip[1], 100, 1e-4) &&
              NEAR(b.base[0], 500 + 22, 1e-3),
          "rig: tip %g %g %g base %g", b.tip[0], b.tip[1], b.tip[2], b.base[0]);
    /* tracking-space deltas: 1 cm per 1/90 s along +X -> 90 cm/s, turned by the rig (+X -> -Z) */
    g = grip_at(1, 0, 0, 0, 10.0 + 1 / HZ);
    somvr_blade_controller(&c, &g, &rig, 5.0 + 1 / HZ, &h, &b);
    CHECK(NEAR(b.vel_tip[0], 0, 1e-2) && NEAR(b.vel_tip[2], -90, 0.05) && NEAR(b.vel_tip[1], 0, 1e-3),
          "delta: v %g %g %g", b.vel_tip[0], b.vel_tip[1], b.vel_tip[2]);
    /* the same locate time again (reused views): the last velocity; a gap > 0.1 s: none */
    somvr_blade_controller(&c, &g, &rig, 5.0 + 2 / HZ, &h, &b);
    CHECK(NEAR(b.vel_tip[2], -90, 0.05), "same time: v %g", b.vel_tip[2]);
    g.xr_t += 0.2;
    somvr_blade_controller(&c, &g, &rig, 5.3, &h, &b);
    CHECK(len3(b.vel_tip) == 0, "gap: v %g", len3(b.vel_tip));
    /* a still grip while the rig turns (snap), moves (walking) and recentres: the world tip moves, the velocity is 0 */
    float worst = 0;
    h.have = 0;
    for (int f = 0; f < 30; f++) {
        sc_rigxf r2 = {{500 + 5.0f * f, 100, -50}, {0, 0, f < 15 ? -30.0f : 12.0f}, DEG(90) + (f >= 10 ? DEG(45) : 0), 0};
        g = grip_at(0, 0, 0, 0, 20.0 + f / HZ);
        somvr_blade_controller(&c, &g, &r2, 6.0 + f / HZ, &h, &b);
        worst = fmaxf(worst, len3(b.vel_tip));
    }
    CHECK(worst == 0, "a still sword under a turning/walking/recentred rig: |v| up to %g", worst);
    /* runtime velocities: v + w x r (w = 2 pi rad/s about +Y, the blade along +Z: the tip at 2 pi 0.9 m/s along +X) */
    sc_blade_cfg cr = c;
    cr.vel_runtime = 1;
    g = grip_at(0, 0, 0, 0, 30.0);
    g.vel_valid = 1, g.vel[1] = 10, g.avel[1] = 6.2831853f;
    somvr_blade_controller(&cr, &g, &id, 7.0, &h, &b);
    CHECK(NEAR(b.vel_tip[0], 6.2831853 * 90, 0.05) && NEAR(b.vel_tip[1], 10, 1e-4) && NEAR(b.vel_tip[2], 0, 1e-3),
          "runtime: v %g %g %g", b.vel_tip[0], b.vel_tip[1], b.vel_tip[2]);
    g.vel_valid = 0; /* not valid: back to deltas (still: 0) */
    g.xr_t += 1 / HZ;
    somvr_blade_controller(&cr, &g, &id, 7.0 + 1 / HZ, &h, &b);
    CHECK(len3(b.vel_tip) == 0, "runtime invalid -> delta: v %g", len3(b.vel_tip));
    /* invalid grips: none, and the history is dropped (the next one starts without a velocity) */
    g.valid = 0;
    CHECK(!somvr_blade_controller(&c, &g, &id, 8.0, &h, &b) && !b.valid && !h.have, "an invalid grip gave a blade");
    g = grip_at(NAN, 0, 0, 0, 40.0);
    CHECK(!somvr_blade_controller(&c, &g, &id, 8.0, &h, &b), "a NaN grip gave a blade");
    g = grip_at(0, 0, 0, 0, 40.0), g.quat[3] = 0;
    CHECK(!somvr_blade_controller(&c, &g, &id, 8.0, &h, &b), "a zero quaternion gave a blade");
    CHECK(!somvr_blade_controller(&c, NULL, &id, 8.0, &h, &b), "no grip gave a blade");
    printf("controller: +Z blade 8 back / 90 ahead, pitch, offset, rig; delta 90 cm/s rotated, reuse, gap; still sword under "
           "turn/walk/recentre = 0; v + w x r; invalid/NaN/zero quaternion: none\n");
}

/* end to end: a tracked sword swung through an orc capsule (the grip turning about the shoulder) at 90 Hz, then the
 * same pose held while the rig turns 45 deg (snap) and the player walks: a heavy hit per fast swing, none otherwise */
static void test_controller_fight(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    sc_blade_cfg bc;
    sc_blade_cfg_default(&bc);
    bc.len_cm = 90; /* this swing's step start: with the 100 cm tip the first contact (20 deg in) has 35 cm of travel, a
                     * light hit; the 90 cm one meets the orc a frame later, past the heavy rule's 40 cm */
    sc_speed s;
    sc_cool k;
    sc_charges ch;
    sc_speed_reset(&s), sc_cool_reset(&k), sc_charges_init(&ch, &c, 0);
    sc_blade_hist h = {0};
    float c0[3] = {0, 50, 110}, c1[3] = {0, 230, 110};
    somvr_blade prev = {0}, b;
    int hits[SC_TIER_N] = {0}, starts = 0;
    for (int f = 0; f < 3 * 90; f++) {
        double t = f / HZ;
        float ph = (float)fmod(t, 1.5), a; /* 0.5 s held right, a 120 deg swing in 0.2 s (~9 m/s at the tip), held left */
        a = ph < 0.5f ? DEG(60) : ph < 0.7f ? DEG(60) - DEG(120) * (ph - 0.5f) / 0.2f : DEG(-60);
        sc_rigxf rig = {{0, 140, 0}, {0, 0, 0}, 0, 0};
        if (t >= 1.5) /* the second half: the sword held where the swing ended, the rig snapping and walking */
            a = DEG(-60), rig.yaw = t >= 2.0 ? DEG(-45) : 0, rig.base[2] = (float)(t - 1.5) * 300.0f;
        sc_grip g = grip_at(0, 0, 0, a, 100.0 + t);
        somvr_blade_controller(&bc, &g, &rig, t, &h, &b);
        starts += sc_speed_push(&s, &c, &b) == 1;
        sc_contact ct;
        if (sc_sweep_capsule(&prev, &b, c.blade_r, c0, c1, 35, c.steps, &ct) && sc_cool_ready(&k, 0x1, t, c.cool_s)) {
            sc_cool_mark(&k, 0x1, t);
            int tr = sc_tier(&c, &s);
            if (tr != SC_TIER_NONE && sc_charges_take(&ch, &c, t)) hits[tr]++;
        }
        prev = b;
    }
    CHECK(hits[SC_TIER_HEAVY] == 1 && hits[SC_TIER_LIGHT] == 0 && starts == 1,
          "controller fight: heavy %d light %d swings %d (one swing, one heavy hit)", hits[SC_TIER_HEAVY],
          hits[SC_TIER_LIGHT], starts);
    printf("controller fight: one 120 deg swing -> one heavy hit; held under a snap turn and a walk: no swing\n");
}

/* end to end: the fake at 90 Hz in front of an orc 110 cm ahead, the speed, the sweep, the cooldown and the charges as
 * somvr.c's combat_frame uses them: per cycle exactly one heavy hit (the right -> left swing) and one light (back) */
static void test_fake_fight(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    sc_speed s;
    sc_cool k;
    sc_charges ch;
    sc_speed_reset(&s);
    sc_cool_reset(&k);
    sc_charges_init(&ch, &c, 0);
    float head[3] = {0, 170, 0}, c0[3] = {0, 50, 110}, c1[3] = {0, 230, 110};
    somvr_blade prev = {0}, b;
    int hits[SC_TIER_N] = {0}, contacts = 0, starts = 0, ends = 0, heading = 0, order[8], no = 0;
    for (int f = 0; f < (int)(2 * SC_FAKE_PERIOD * HZ); f++) {
        double t = f / HZ;
        somvr_blade_fake(t, 0, head, 0, &b);
        int e = sc_speed_push(&s, &c, &b);
        float d, cs;
        if (e == 1) {
            starts++;
            heading += sc_heading_at(&c, &b, c0, c1, 35, &d, &cs);
        }
        ends += e == -1;
        sc_contact ct;
        if (sc_sweep_capsule(&prev, &b, c.blade_r, c0, c1, 35, c.steps, &ct)) {
            contacts++;
            if (sc_cool_ready(&k, 0x1, t, c.cool_s)) {
                sc_cool_mark(&k, 0x1, t);
                int tr = sc_tier(&c, &s);
                if (tr != SC_TIER_NONE && sc_charges_take(&ch, &c, t)) {
                    hits[tr]++;
                    if (no < 8) order[no++] = tr;
                }
            }
        }
        prev = b;
    }
    CHECK(hits[SC_TIER_HEAVY] == 2 && hits[SC_TIER_LIGHT] == 2 && hits[SC_TIER_STAB] == 0, "hits heavy %d light %d stab %d",
          hits[SC_TIER_HEAVY], hits[SC_TIER_LIGHT], hits[SC_TIER_STAB]);
    CHECK(no == 4 && order[0] == SC_TIER_HEAVY && order[1] == SC_TIER_LIGHT && order[2] == SC_TIER_HEAVY &&
              order[3] == SC_TIER_LIGHT,
          "order of the hits");
    CHECK(starts == 4 && ends == 4 && heading == 4, "swings: %d starts, %d ends, %d heading at the orc", starts, ends, heading);
    CHECK(contacts > 4, "contacts %d (several frames per pass, the cooldown keeps one hit)", contacts);
    printf("fake fight: 2 cycles, %d contact frames -> heavy, light, heavy, light; 4 swings toward the orc\n", contacts);
}

/* ---- orc reactions (SOMVR_COMBAT_REACT): the block-table chance, the game's comparison, one roll per swing per orc */
static void test_react(void) {
    const uintptr_t X = 0x1000, L = 0x2000, H = 0x3000;
    int ent = 7;
    sc_block_entry e2[2] = {{X, 0.9f}, {L, 0.3f}}, e3[3] = {{X, 0.9f}, {0, 0.7f}, {L, 0.3f}}, e1n[1] = {{0, 0.6f}};
    uintptr_t cl[1] = {L}, clx[2] = {L, X}, ch[1] = {H};
    float c = sc_block_chance(e2, 0, 1, cl, 1, &ent);
    CHECK(c < 0 && ent == -1, "no entries: %g (entry %d), want no roll", c, ent);
    CHECK(sc_block_chance(NULL, 2, 1, cl, 1, &ent) < 0 && ent == -1, "no table rolled");
    c = sc_block_chance(e2, 2, 0, cl, 1, &ent);
    CHECK(c == SC_BLOCK_DEFAULT && ent == -1, "entries that are not records: %g (entry %d), want 0.5", c, ent);
    c = sc_block_chance(e2, 2, 1, cl, 1, &ent);
    CHECK(c == 0.3f && ent == 1, "the light category: %g (entry %d)", c, ent);
    c = sc_block_chance(e3, 3, 1, cl, 1, &ent);
    CHECK(c == 0.7f && ent == 1, "an entry without a category before the match: %g (entry %d)", c, ent);
    c = sc_block_chance(e2, 2, 1, clx, 2, &ent);
    CHECK(c == 0.9f && ent == 0, "two categories: the first entry that matches (%g, entry %d)", c, ent);
    c = sc_block_chance(e2, 2, 1, ch, 1, &ent);
    CHECK(c < 0 && ent == -1, "no entry matches: %g (entry %d)", c, ent);
    c = sc_block_chance(e2, 2, 1, NULL, 0, &ent);
    CHECK(c < 0 && ent == -1, "an attack without categories against categorised entries: %g", c);
    c = sc_block_chance(e1n, 1, 1, NULL, 0, &ent);
    CHECK(c == 0.6f && ent == 0, "an attack without categories, an entry without one: %g (entry %d)", c, ent);
    /* the game's comparison: notify only when chance > roll */
    CHECK(sc_block_won(0.5f, 0.49f) && !sc_block_won(0.5f, 0.5f) && !sc_block_won(0.5f, 0.51f), "0.5 against 0.49/0.5/0.51");
    CHECK(!sc_block_won(-1.0f, 0.0f) && !sc_block_won(0.0f, 0.0f) && sc_block_won(1.0f, 0.999f), "no roll / 0 / 1");
    CHECK(!sc_block_won(0.5f, NAN) && !sc_block_won(NAN, 0.1f), "NaN won");
    /* one roll per swing per orc, in the swing's first SC_REACT_WINDOW_S */
    sc_react r;
    sc_react_reset(&r);
    CHECK(!sc_react_take(&r, 0xa, 10) && !sc_react_open(&r, 10), "a roll without a swing");
    sc_react_start(&r, 10);
    CHECK(sc_react_take(&r, 0xa, 10), "the first orc at the swing's start");
    CHECK(!sc_react_take(&r, 0xa, 10.05) && !sc_react_take(&r, 0xa, 10.2), "the same orc twice in one swing");
    CHECK(sc_react_take(&r, 0xb, 10.2), "a second orc later in the window");
    CHECK(!sc_react_take(&r, 0, 10.1), "key 0 rolled");
    CHECK(sc_react_open(&r, 10 + SC_REACT_WINDOW_S) && !sc_react_open(&r, 10 + SC_REACT_WINDOW_S + 1e-4) &&
              !sc_react_open(&r, 9.99),
          "the window's edges");
    CHECK(!sc_react_take(&r, 0xc, 10 + SC_REACT_WINDOW_S + 0.01), "a roll after the window");
    sc_react_end(&r);
    CHECK(!sc_react_take(&r, 0xd, 10.1) && !sc_react_open(&r, 10.1), "a roll after the swing's end");
    sc_react_start(&r, 11);
    CHECK(sc_react_take(&r, 0xa, 11) && sc_react_take(&r, 0xb, 11), "the next swing: the same orcs roll again");
    sc_react_start(&r, 12);
    int k = 0;
    for (uintptr_t key = 1; key <= SC_REACT_N + 1; key++)
        k += sc_react_take(&r, key, 12);
    CHECK(k == SC_REACT_N && r.n == SC_REACT_N, "capacity: %d of %d orcs rolled", k, SC_REACT_N + 1);
    printf("react: chance (none, not records = 0.5, first match, an entry without a category, no match), the game's "
           "chance > roll, one roll per swing per orc, the %.0f ms window, the end, the next swing, %d orcs\n",
           SC_REACT_WINDOW_S * 1e3, SC_REACT_N);
}

/* the no-table path (P9: AI_Defender and AI_Berserker have no AIBlockTable): the archetype, the counter goal's gate, the
 * chance per archetype, the game's comparison */
static void test_react_direct(void) {
    sc_react_cfg c;
    sc_react_cfg_default(&c);
    CHECK(c.chance[SC_ARCH_DEFENDER] == 0.6f && c.chance[SC_ARCH_BERSERKER] == 0.4f && c.chance[SC_ARCH_OTHER] == 0.5f,
          "defaults %g %g %g", c.chance[0], c.chance[1], c.chance[2]);
    CHECK(sc_archetype("AI_Defender") == SC_ARCH_DEFENDER && sc_archetype("AI_Berserker") == SC_ARCH_BERSERKER &&
              sc_archetype("AI_Defender_Elite") == SC_ARCH_DEFENDER && sc_archetype("AI_Berserker_X") == SC_ARCH_BERSERKER,
          "the two archetypes");
    CHECK(sc_archetype("AI_Infantry") == SC_ARCH_OTHER && sc_archetype("AI_Defenders") == SC_ARCH_OTHER &&
              sc_archetype("Orc_Defender") == SC_ARCH_OTHER && sc_archetype("ai_defender") == SC_ARCH_OTHER &&
              sc_archetype("") == SC_ARCH_OTHER && sc_archetype(NULL) == SC_ARCH_OTHER,
          "other names");
    CHECK(!strcmp(sc_archetype_name(SC_ARCH_DEFENDER), "defender") && !strcmp(sc_archetype_name(SC_ARCH_OTHER), "other") &&
              !strcmp(sc_archetype_name(7), "?") && !strcmp(sc_archetype_name(-1), "?"),
          "archetype names");
    /* the gate: an attack with both cancel bytes 0 = in its own attack; no attack, or a cancel window open = out of it
     * (P9: 'AI_Defender_Cone' cancel=0/0 -> no goal; '' -> block_go) */
    CHECK(sc_in_own_attack(0x1234, 0, 0) && !sc_in_own_attack(0, 0, 0) && !sc_in_own_attack(0x1234, 1, 0) &&
              !sc_in_own_attack(0x1234, 0, 1) && !sc_in_own_attack(0, 1, 1),
          "the counter goal's gate");
    CHECK(sc_react_direct_chance(&c, SC_ARCH_DEFENDER, 0) == 0.6f && sc_react_direct_chance(&c, SC_ARCH_BERSERKER, 0) == 0.4f &&
              sc_react_direct_chance(&c, SC_ARCH_OTHER, 0) == 0.5f,
          "out of its attack: the archetype's chance");
    CHECK(sc_react_direct_chance(&c, SC_ARCH_DEFENDER, 1) < 0 && sc_react_direct_chance(&c, SC_ARCH_BERSERKER, 1) < 0,
          "in its own attack: no call");
    CHECK(sc_react_direct_chance(&c, SC_ARCH_N, 0) < 0 && sc_react_direct_chance(&c, -1, 0) < 0, "an unknown archetype");
    sc_react_cfg z = c;
    z.chance[SC_ARCH_DEFENDER] = 0, z.chance[SC_ARCH_BERSERKER] = NAN, z.chance[SC_ARCH_OTHER] = 1.5f;
    CHECK(sc_react_direct_chance(&z, SC_ARCH_DEFENDER, 0) < 0 && sc_react_direct_chance(&z, SC_ARCH_BERSERKER, 0) < 0 &&
              sc_react_direct_chance(&z, SC_ARCH_OTHER, 0) == 1.0f,
          "0 = never, NaN = never, above 1 = 1");
    /* with the game's comparison: the defender's 0.6 wins at 0.3 and 0.59, loses at 0.6 and 0.8; force (roll 0) wins */
    float d = sc_react_direct_chance(&c, SC_ARCH_DEFENDER, 0), bz = sc_react_direct_chance(&c, SC_ARCH_BERSERKER, 0);
    CHECK(sc_block_won(d, 0.3f) && sc_block_won(d, 0.59f) && !sc_block_won(d, 0.6f) && !sc_block_won(d, 0.8f) &&
              sc_block_won(d, 0.0f),
          "defender rolls");
    CHECK(sc_block_won(bz, 0.3f) && !sc_block_won(bz, 0.5f) && !sc_block_won(sc_react_direct_chance(&c, SC_ARCH_BERSERKER, 1), 0.0f),
          "berserker rolls; in its attack even force does not call");
    printf("react direct: archetypes by record (AI_Defender, AI_Berserker, other), the counter goal's gate (attack + cancel "
           "0/0), chances %.1f/%.1f/%.1f, 0/NaN = never, in its own attack = no call\n",
           c.chance[0], c.chance[1], c.chance[2]);
}

/* end to end with the fake blade: per swing one roll for the orc ahead (at the swing's start; many frames head at it),
 * one for an orc 40 deg left, none for an orc behind; the edges drive the window */
static void test_react_fight(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    sc_speed s;
    sc_react r;
    sc_speed_reset(&s);
    sc_react_reset(&r);
    float head[3] = {0, 170, 0};
    /* A 110 cm ahead, L 120 cm at 40 deg left, B 3 m behind */
    float cap[3][2][3] = {{{0, 50, 110}, {0, 230, 110}},
                          {{-120 * 0.6428f, 50, 120 * 0.7660f}, {-120 * 0.6428f, 230, 120 * 0.7660f}},
                          {{0, 50, -300}, {0, 230, -300}}};
    int rolls[3] = {0}, heading[3] = {0}, starts = 0, first_at_start = 0;
    somvr_blade b;
    for (int f = 0; f < (int)(2 * SC_FAKE_PERIOD * HZ); f++) {
        double t = f / HZ;
        somvr_blade_fake(t, 0, head, 0, &b);
        int e = sc_speed_push(&s, &c, &b);
        if (e == 1)
            sc_react_start(&r, t), starts++;
        else if (e == -1)
            sc_react_end(&r);
        for (int o = 0; o < 3 && sc_react_open(&r, t); o++) {
            float d, cs;
            if (!sc_heading_at(&c, &b, cap[o][0], cap[o][1], 35, &d, &cs))
                continue;
            heading[o]++;
            if (sc_react_take(&r, (uintptr_t)(o + 1), t)) {
                rolls[o]++;
                first_at_start += o == 0 && t == r.t0;
            }
        }
    }
    CHECK(starts == 4 && rolls[0] == 4 && first_at_start == 4, "ahead: %d swings, %d rolls (%d at the start)", starts,
          rolls[0], first_at_start);
    CHECK(heading[0] > rolls[0], "ahead: %d frames heading at it, %d rolls (the limit must matter)", heading[0], rolls[0]);
    CHECK(rolls[1] == 4 && heading[1] > 4, "left: %d rolls (%d frames heading at it)", rolls[1], heading[1]);
    CHECK(rolls[2] == 0 && heading[2] == 0, "behind: %d rolls", rolls[2]);
    printf("react fight: 2 cycles, 4 swings: the orc ahead rolls 4 times (%d frames heading at it), the one at 40 deg "
           "left %d, the one behind 0\n", heading[0], rolls[1]);
}

/* ---- SOMVR_HANDS_TEST's scripted swing (sc_swing_*) ------------------------------------------------------------- */
static void test_swing(void) {
    /* the timing: minimum jerk */
    CHECK(sc_swing_ease(0) == 0 && sc_swing_ease(1) == 1 && NEAR(sc_swing_ease(0.5f), 0.5, 1e-6) &&
              sc_swing_ease(-1) == 0 && sc_swing_ease(2) == 1 && sc_swing_ease(NAN) == 0,
          "ease ends, middle, clamps, NaN");
    float last = 0, peak = 0;
    int mono = 1;
    for (int i = 1; i <= 1000; i++) {
        float k = sc_swing_ease(i / 1000.0f), d = (k - last) * 1000;
        mono &= k >= last - 1e-6f, peak = d > peak ? d : peak, last = k;
    }
    CHECK(mono && NEAR(peak, 1.875, 0.01) && sc_swing_ease(0.01f) < 1e-4f && 1 - sc_swing_ease(0.99f) < 1e-4f,
          "ease monotonic, peak rate %.3f (1.875), still at both ends", peak);
    /* the euler order: X, then Y, then Z (q = qx qy qz) */
    float q[4], e[3] = {90, 0, 0};
    sc_euler_quat(e, q);
    CHECK(NEAR(q[0], sinf(DEG(45)), 1e-6) && q[1] == 0 && q[2] == 0 && NEAR(q[3], cosf(DEG(45)), 1e-6), "90 about X");
    float e2[3] = {30, 40, 50}, sx = sinf(DEG(15)), cx = cosf(DEG(15)), sy = sinf(DEG(20)), cy = cosf(DEG(20)),
          sz = sinf(DEG(25)), cz = cosf(DEG(25));
    sc_euler_quat(e2, q);
    /* qx qy = (sx cy, cx sy, sx sy, cx cy); then x qz */
    float a0 = sx * cy, a1 = cx * sy, a2 = sx * sy, a3 = cx * cy;
    float w0 = a3 * 0 + a0 * cz + a1 * sz - a2 * 0, w1 = a3 * 0 - a0 * sz + a1 * cz + a2 * 0,
          w2 = a3 * sz + a0 * 0 - a1 * 0 + a2 * cz, w3 = a3 * cz - a0 * 0 - a1 * 0 - a2 * sz;
    CHECK(NEAR(q[0], w0, 1e-6) && NEAR(q[1], w1, 1e-6) && NEAR(q[2], w2, 1e-6) && NEAR(q[3], w3, 1e-6),
          "30 40 50: %.4f %.4f %.4f %.4f vs %.4f %.4f %.4f %.4f", q[0], q[1], q[2], q[3], w0, w1, w2, w3);
    /* the arc about the pivot: 40 cm right of it -> 60 cm ahead of it */
    float pv[3] = {18, -25, -8}, pa[3] = {58, -25, -8}, pb[3] = {18, -25, 52}, id[4] = {0, 0, 0, 1}, p[3];
    float qy90[4] = {0, sinf(DEG(45)), 0, cosf(DEG(45))};
    CHECK(sc_swing_pose(pa, id, pb, qy90, pv, 0, p, q) && NEAR(p[0], 58, 1e-4) && NEAR(p[2], -8, 1e-4) && NEAR(q[3], 1, 1e-6),
          "u 0: the start");
    CHECK(sc_swing_pose(pa, id, pb, qy90, pv, 1, p, q) && NEAR(p[0], 18, 1e-3) && NEAR(p[2], 52, 1e-3) &&
              NEAR(q[1], qy90[1], 1e-6) && NEAR(q[3], qy90[3], 1e-6),
          "u 1: the end");
    sc_swing_pose(pa, id, pb, qy90, pv, 0.5f, p, q);
    float r[3] = {p[0] - pv[0], p[1] - pv[1], p[2] - pv[2]};
    CHECK(NEAR(len3(r), 50, 1e-3) && NEAR(atan2f(r[2], r[0]), DEG(45), 1e-4) && NEAR(p[1], -25, 1e-4) &&
              NEAR(q[1], sinf(DEG(22.5f)), 1e-5) && NEAR(q[3], cosf(DEG(22.5f)), 1e-5),
          "u 0.5: on the arc (50 cm, 45 deg), half the turn: %.2f cm %.1f deg", len3(r), atan2f(r[2], r[0]) * 57.3f);
    float qn[4] = {-qy90[0], -qy90[1], -qy90[2], -qy90[3]};
    sc_swing_pose(pa, id, pb, qn, pv, 0.5f, p, q);
    CHECK(NEAR(fabsf(q[1]), sinf(DEG(22.5f)), 1e-5) && NEAR(fabsf(q[3]), cosf(DEG(22.5f)), 1e-5), "the shorter way");
    float po[3] = {-22, -25, -8}; /* opposite the start: a line */
    sc_swing_pose(pa, id, po, id, pv, 0.5f, p, q);
    CHECK(NEAR(p[0], 18, 1e-3) && NEAR(p[2], -8, 1e-3), "opposite: the line's middle %.2f,%.2f", p[0], p[2]);
    sc_swing_pose(pv, id, pb, id, pv, 0.5f, p, q);
    CHECK(NEAR(p[2], 22, 1e-3), "from the pivot itself: the line");
    float zq[4] = {0, 0, 0, 0}, bad[3] = {NAN, 0, 0};
    CHECK(!sc_swing_pose(pa, zq, pb, id, pv, 0.5f, p, q) && !sc_swing_pose(bad, id, pb, id, pv, 0.5f, p, q) &&
              !sc_swing_pose(pa, id, pb, id, bad, 0.5f, p, q),
          "a zero quaternion, NaN: refused");
}

/* the scripted swing end to end (`hands swing R` in a headless boot): the right grip from the fist axis pointing right
 * to pointing left in front of the head (160 cm up, facing +z), through an orc 110 cm ahead, at Monado's 20 Hz and at
 * 90 Hz, through somvr_blade_controller's tracking-space velocity, the speed, the sweep and the tiers as combat_frame */
static int swing_fight(double hz, float ms, int *starts, int *tier) {
    sc_cfg c;
    sc_cfg_default(&c);
    sc_blade_cfg bc;
    sc_blade_cfg_default(&bc);
    sc_speed s;
    sc_cool k;
    sc_charges ch;
    sc_speed_reset(&s), sc_cool_reset(&k), sc_charges_init(&ch, &c, 0);
    sc_blade_hist h = {0};
    float c0[3] = {0, 50, 110}, c1[3] = {0, 230, 110}, pv[3] = {18, -25, -8};
    float pa[3] = {35, -20, 30}, ea[3] = {0, -90, 0}, pb[3] = {-25, -25, 45}, eb[3] = {0, 90, 0}, qa[4], qb[4];
    sc_euler_quat(ea, qa), sc_euler_quat(eb, qb);
    somvr_blade prev = {0}, b;
    int hits = 0;
    *starts = 0, *tier = SC_TIER_NONE;
    sc_rigxf rig = {{0, 0, 0}, {0, 0, 0}, 0, 0};
    for (int f = 0; f < (int)(1.5 * hz); f++) {
        double t = f / hz, u = (t - 0.5) / (ms * 1e-3); /* held 0.5 s, then the swing */
        float p[3], q[4];
        sc_swing_pose(pa, qa, pb, qb, pv, (float)u, p, q);
        sc_grip g = {1, {p[0], 160 + p[1], p[2]}, {-q[0], -q[1], q[2], q[3]}, 0, {0, 0, 0}, {0, 0, 0}, 100 + t};
        somvr_blade_controller(&bc, &g, &rig, t, &h, &b);
        *starts += sc_speed_push(&s, &c, &b) == 1;
        sc_contact ct;
        if (sc_sweep_capsule(&prev, &b, c.blade_r, c0, c1, 35, c.steps, &ct) && sc_cool_ready(&k, 0x1, t, c.cool_s)) {
            sc_cool_mark(&k, 0x1, t);
            int tr = sc_tier(&c, &s);
            if (tr != SC_TIER_NONE && sc_charges_take(&ch, &c, t)) hits++, *tier = tr;
        }
        prev = b;
    }
    return hits;
}
static void test_swing_fight(void) {
    static const struct { double hz; float ms; int tier; } run[] = {
        {20, 300, SC_TIER_HEAVY}, {90, 300, SC_TIER_HEAVY}, {20, 1400, SC_TIER_LIGHT}, {90, 1400, SC_TIER_LIGHT}};
    for (int i = 0; i < 4; i++) {
        int starts, tier, hits = swing_fight(run[i].hz, run[i].ms, &starts, &tier);
        CHECK(hits == 1 && starts == 1 && tier == run[i].tier, "%.0f Hz, %.0f ms: %d hits (tier %s), %d swings", run[i].hz,
              run[i].ms, hits, sc_tier_name(tier), starts);
    }
    printf("swing fight: `hands swing` right -> left through an orc 110 cm ahead: one hit at 20 and 90 Hz (300 ms heavy, "
           "1400 ms light)\n");
}

/* the blade resting in an orc for 2 s at 20 and 90 Hz: one glancing tick at the start, rubs only while the tip moves
 * (at most one per SC_TOUCH_RUB_S), a new tick after a gap; two orcs apart; key 0; a full table */
static void test_touch(void) {
    static const double hz[2] = {20, 90};
    for (int k = 0; k < 2; k++) {
        sc_touch tc;
        sc_touch_reset(&tc);
        int starts = 0, rubs = 0, rubs_still = 0;
        for (int f = 0; f <= (int)(2 * hz[k]); f++) {
            double t = 5.0 + f / hz[k];
            float mps = t < 6.0 ? 0.0f : 1.2f; /* held still for 1 s, then scraped along */
            int r = sc_touch_step(&tc, 0xA, t, mps, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS);
            starts += r == SC_TOUCH_START, rubs += r == SC_TOUCH_RUB, rubs_still += r == SC_TOUCH_RUB && t < 6.0;
        }
        int want = (int)(1.0 / SC_TOUCH_RUB_S); /* about 8 in the scraped second */
        CHECK(starts == 1 && rubs_still == 0 && rubs >= want - 1 && rubs <= want + 1,
              "%.0f Hz: a 2 s touch = 1 start (%d), no rub while still (%d), %d rubs scraping (want ~%d)", hz[k], starts,
              rubs_still, rubs, want);
        CHECK(NEAR(sc_touch_age(&tc, 0xA, 7.0, SC_TOUCH_GAP_S), 2.0, 1e-6) && sc_touch_age(&tc, 0xB, 7.0, SC_TOUCH_GAP_S) == 0,
              "%.0f Hz: the age of the touch 2 s, none for another orc", hz[k]);
        int r1 = sc_touch_step(&tc, 0xA, 7.0 + SC_TOUCH_GAP_S * 0.9, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS);
        int r2 = sc_touch_step(&tc, 0xA, 7.3, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS);
        CHECK(r1 == SC_TOUCH_ON && r2 == SC_TOUCH_START && sc_touch_age(&tc, 0xA, 7.3, SC_TOUCH_GAP_S) == 0,
              "%.0f Hz: within the gap the touch goes on (%d), after 0.2 s apart a new one (%d)", hz[k], r1, r2);
        CHECK(sc_touch_step(&tc, 0xB, 7.3, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS) == SC_TOUCH_START &&
                  sc_touch_step(&tc, 0xA, 7.32, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS) == SC_TOUCH_ON,
              "%.0f Hz: a second orc's touch starts on its own, the first one's goes on", hz[k]);
    }
    sc_touch tc;
    sc_touch_reset(&tc);
    CHECK(sc_touch_step(&tc, 0, 1.0, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS) == SC_TOUCH_START &&
              sc_touch_step(&tc, 0, 1.01, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS) == SC_TOUCH_START,
          "key 0: always a start, nothing kept");
    CHECK(sc_touch_step(&tc, 0xC, 1.0, 9, SC_TOUCH_GAP_S, 0, SC_TOUCH_RUB_MPS) == SC_TOUCH_START &&
              sc_touch_step(&tc, 0xC, 1.05, 9, SC_TOUCH_GAP_S, 0, SC_TOUCH_RUB_MPS) == SC_TOUCH_ON,
          "rub_s 0: no rubs at all");
    for (uintptr_t i = 1; i <= SC_TOUCH_N; i++)
        sc_touch_step(&tc, 0x100 + i, 2.0 + (double)i * 0.001, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS);
    CHECK(sc_touch_step(&tc, 0x101, 2.05, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS) == SC_TOUCH_ON &&
              sc_touch_step(&tc, 0xC, 2.06, 0, SC_TOUCH_GAP_S, SC_TOUCH_RUB_S, SC_TOUCH_RUB_MPS) == SC_TOUCH_START,
          "a full table: the oldest touch (0xC) was forgotten, the newer ones kept");
    printf("touch: a resting blade = one glancing tick, rubs only while it scrapes (at most %.0f a second)\n",
           1.0 / SC_TOUCH_RUB_S);
}

/* ---- SOMVR_COMBAT_CLASS=cut: the edge's angle and the committed cut (build 16) ------------------------------------ */
static void test_cut(void) {
    somvr_blade b = blade(0, 0, 0, 0, 0, 100, 0);
    b.flat_n[0] = 1; /* the blade along +z, its flat facing +x: the edges along y */
    float v[][3] = {{0, 500, 0}, {500, 0, 0}, {500, 500, 0}, {0, 300, 400}, {0, 0, 500}, {0, 0.5f, 0}};
    float want[] = {0, 90, 45, 0, NAN, NAN};
    for (int i = 0; i < 6; i++) {
        memcpy(b.vel_tip, v[i], sizeof v[i]);
        float e = sc_edge_deg(&b);
        CHECK(isnan(want[i]) ? isnan(e) : NEAR(e, want[i], 0.01), "edge case %d: %g, want %g", i, e, want[i]);
    }
    memset(b.flat_n, 0, sizeof b.flat_n);
    b.vel_tip[1] = 500;
    CHECK(isnan(sc_edge_deg(&b)), "no flat known: NAN");
    sc_cfg c;
    sc_cfg_default(&c);
    CHECK(c.cut_cm == 50 && c.edge_max_deg == 60, "the defaults: %g cm, %g deg", c.cut_cm, c.edge_max_deg);
    sc_speed s;
    memset(&s, 0, sizeof s);
    s.above = 1, s.mean_mps = 3.2f, s.travel = 55;
    CHECK(sc_tier_cut(&c, &s, 10) == SC_TIER_HEAVY, "a slow committed cut, the edge leading: heavy");
    CHECK(sc_tier(&c, &s) == SC_TIER_LIGHT, "... the speed rule called it light");
    CHECK(sc_tier_cut(&c, &s, NAN) == SC_TIER_HEAVY, "no flat known: the cut alone");
    CHECK(sc_tier_cut(&c, &s, 75) == SC_TIER_LIGHT, "the flat leading: light");
    s.travel = 30;
    CHECK(sc_tier_cut(&c, &s, 0) == SC_TIER_LIGHT, "30 cm: a tap, light");
    s.mean_mps = 25, s.travel = 30;
    CHECK(sc_tier_cut(&c, &s, 0) == SC_TIER_LIGHT && sc_tier(&c, &s) == SC_TIER_LIGHT, "fast and short: light both ways");
    s.mean_mps = 2.5f, s.travel = 80;
    CHECK(sc_tier_cut(&c, &s, 0) == SC_TIER_NONE, "under the slash floor: nothing");
    s.above = 0, s.mean_mps = 25, s.travel = 300;
    CHECK(sc_tier_cut(&c, &s, 0) == SC_TIER_NONE, "no swing: nothing");
    s.axis_mps = 2.5f, s.cos_axis = 0.95f;
    CHECK(sc_tier_cut(&c, &s, 0) == SC_TIER_STAB, "a stab stays a stab");
    printf("cut: the edge's angle (0 edge .. 90 flat), a committed cut = %.0f cm with the edge within %.0f deg\n", c.cut_cm,
           c.edge_max_deg);
}

/* ---- SOMVR_FIGHT_SPACE_CM's hold (build 16) ------------------------------------------------------------------- */
static void test_space(void) {
    float root[3] = {100, 50, -200}, w, g;
    struct { float was[3], to[3], want[3]; int changed; } T[] = {
        {{100, 50, -50}, {100, 50, -80}, {100, 50, -80}, 0},     /* far: 120 cm, no hold */
        {{100, 50, -100}, {100, 52, -120}, {100, 52, -110}, 1},  /* 100 -> 80: held at 90, y as moved */
        {{100, 50, -130}, {100, 50, -140}, {100, 50, -130}, 1},  /* inside at 70 -> 60: stays at 70, not pushed out */
        {{100, 50, -130}, {100, 50, -125}, {100, 50, -125}, 0},  /* inside, moving away: untouched */
        {{160, 50, -200}, {100.5f, 50, -200}, {160, 50, -200}, 1}, /* onto his root: back where it was (at 60 cm) */
        {{190, 50, -200}, {170, 50, -200}, {190, 50, -200}, 1},  /* 90 -> 70 on x: held at 90 */
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        float xf[3] = {T[i].to[0], T[i].to[1], T[i].to[2]};
        int ch = sc_space_hold(root, T[i].was, xf, 90, &w, &g);
        CHECK(ch == T[i].changed && NEAR(xf[0], T[i].want[0], 0.01) && NEAR(xf[1], T[i].want[1], 0.01) &&
                  NEAR(xf[2], T[i].want[2], 0.01),
              "space case %zu: changed %d -> %g %g %g, want %d -> %g %g %g", i, ch, xf[0], xf[1], xf[2], T[i].changed,
              T[i].want[0], T[i].want[1], T[i].want[2]);
    }
    float xf[3] = {100, 50, -120}, was[3] = {100, 50, -100};
    CHECK(!sc_space_hold(root, was, xf, 0, &w, &g) && xf[2] == -120, "space 0: off");
    printf("space: an orc never moves nearer than the space (or where it was) to Talion's root\n");
    /* two places: the head 40 cm ahead of the root (toward the orc on -z) */
    float r2[3] = {0, 0, 0}, h2[3] = {0, 160, -40};
    struct { float was[3], to[3], want[3]; int changed; } U[] = {
        {{0, 0, -200}, {0, 0, -150}, {0, 0, -150}, 0},   /* 110 from the head: free */
        {{0, 0, -200}, {0, 0, -120}, {0, 0, -130}, 1},   /* 80 from the head: held at 90 from the head (130 from the root) */
        {{0, 0, 200}, {0, 0, 70}, {0, 0, 90}, 1},        /* behind: the root is the nearer, held at 90 from it */
        {{100, 0, -40}, {80, 0, -40}, {90, 0, -40}, 1},  /* beside the head: 90 from it */
        {{0, 0, -100}, {0, 0, -90}, {0, 0, -100}, 1},    /* already inside (60 from the head): stays at 60, not pushed */
    };
    for (size_t i = 0; i < sizeof U / sizeof U[0]; i++) {
        float xf2[3] = {U[i].to[0], U[i].to[1], U[i].to[2]};
        int ch = sc_space_hold2(r2, h2, U[i].was, xf2, 90, &w, &g);
        CHECK(ch == U[i].changed && NEAR(xf2[0], U[i].want[0], 0.05) && NEAR(xf2[2], U[i].want[2], 0.05),
              "space2 case %zu: changed %d -> %g %g, want %d -> %g %g", i, ch, xf2[0], xf2[2], U[i].changed, U[i].want[0],
              U[i].want[2]);
    }
    float a[3] = {0, 0, -80}, wa[3] = {0, 0, -200};
    CHECK(sc_space_hold2(r2, NULL, wa, a, 90, &w, &g) && NEAR(a[2], -90, 0.05), "no head: the root alone");
    /* the two circles overlap: a move into their middle from the side keeps both (the stop is outside both) */
    float m[3] = {45, 0, -20}, wm[3] = {150, 0, -20};
    sc_space_hold2(r2, h2, wm, m, 90, &w, &g);
    CHECK(hypotf(m[0], m[2]) >= 89.9f && hypotf(m[0], m[2] + 40) >= 89.9f, "both kept: %g %g", m[0], m[2]);
    printf("space2: the nearer of the head and the root keeps the space\n");
}

/* SOMVR_FIGHT_BODY_CM (build 19b): the root's distance a leaning body needs; the hook's eased hold */
static void test_body(void) {
    float eye[3] = {0, 170, 0}, root[3] = {0, 0, -88}, w;
    int k = -2;
    /* a bash: the head 30 cm ahead of the eyes at their height, the chest 40 cm ahead and 25 down, a shoulder aside */
    float b[3][3] = {{0, 170, -30}, {0, 145, -40}, {20, 150, -45}};
    float need = sc_body_need(eye, root, (const float (*)[3])b, 3, 55, 60, &k);
    CHECK(k == 0 && NEAR(need, 88 + 25, 0.05), "body need: the head 30 -> 55 cm: the root at %g (want 113), worst %d", need,
          k);
    float out[2][3] = {{0, 170, -80}, {0, 100, -30}}; /* 80 cm ahead; 30 ahead but 70 below: both out */
    CHECK(sc_body_need(eye, root, (const float (*)[3])out, 2, 55, 60, &k) == 0 && k == -1, "body need: all out: none");
    float far[1][3] = {{0, 170, -1}};
    CHECK(NEAR(sc_body_need(eye, root, (const float (*)[3])far, 1, 55, 20, &k), 108, 0.05), "body need: capped at +max");
    /* a point aside: the root moved straight back, the point's distance across grows as |b + d u| */
    float side[1][3] = {{30, 170, -20}};
    float ns = sc_body_need(eye, root, (const float (*)[3])side, 1, 55, 60, &k), d = ns - 88;
    CHECK(d > 0 && NEAR(hypotf(30, -20 - d), 55, 0.05), "body need: aside: %g cm back puts it at %g", d,
          hypotf(30, -20 - d));
    CHECK(sc_body_need(eye, root, (const float (*)[3])b, 3, 0, 60, &k) == 0, "body need: 0 cm = none");
    /* the hook: an orc standing at 88 (its own step none) eased out 5 cm a step to 113, then held there */
    float was[3] = {0, 0, -88}, steps = 0, p = 0;
    int moved = 0, n = 0;
    for (; n < 10; n++) {
        float xf[3] = {was[0], 2, was[2]};
        moved += sc_body_push(eye, was, xf, need, 5, &p);
        CHECK(xf[1] == 2 && hypotf(xf[0], xf[2]) <= 113.01f && hypotf(xf[0], xf[2]) >= hypotf(was[0], was[2]) - 1e-3f,
              "body push step %d: %g", n, hypotf(xf[0], xf[2]));
        steps += p, memcpy(was, xf, sizeof was);
    }
    CHECK(moved == 5 && NEAR(was[2], -113, 0.01) && NEAR(steps, 25, 0.01), "body push: 5 steps of 5 cm to 113 (%d, %g, %g)",
          moved, was[2], steps);
    float in[3] = {0, 0, -100};
    CHECK(sc_body_push(eye, was, in, need, 5, &p) && NEAR(in[2], -113, 0.01), "body push: its step in from 113: held at 113");
    float own[3] = {0, 0, -90}, w0[3] = {0, 0, -88};
    CHECK(sc_body_push(eye, w0, own, need, 5, &p) && NEAR(own[2], -93, 0.01) && NEAR(p, 3, 0.01),
          "body push: its own step out 2 cm: eased to 5 (%g, %g)", own[2], p);
    float o2[3] = {0, 0, -96};
    CHECK(!sc_body_push(eye, w0, o2, need, 5, &p) && NEAR(o2[2], -96, 0.01) && p == 0,
          "body push: its own step farther than the ease: as it is (%g, %g)", o2[2], p);
    float x2[3] = {0, 0, -120};
    CHECK(!sc_body_push(eye, w0, x2, need, 5, &p) && x2[2] == -120, "body push: out already: untouched");
    float z2[3] = {0.5f, 0, 0}, wz[3] = {0, 0, -60};
    CHECK(sc_body_push(eye, wz, z2, need, 5, &p) && NEAR(z2[2], -65, 0.01) && NEAR(z2[0], 0, 0.01),
          "body push: onto the eyes: out the way it came (%g %g)", z2[0], z2[2]);
    float q[3] = {0, 0, -80};
    CHECK(!sc_body_push(eye, w0, q, 0, 5, &p) && q[2] == -80, "body push: no need: untouched");
    (void)w;
    printf("body: a leaning body's points kept %.0f cm from the eyes by its root, eased out %.0f cm a step\n", 55.0, 5.0);
}

/* SOMVR_COMBAT_STAGGER_CM (build 17): the reel kept under the cap, eased (never a dead stop), free toward Talion */
static float reel_nat(float u) { return 145.0f * (1.0f - (1.0f - u) * (1.0f - u)); } /* 145 cm, slowing (an L100) */
static void test_reel(void) {
    float an[3] = {0, 0, -120}, tal[3] = {0, 0, 0}, was[3] = {0, 0, -120}, cut = 0, prev = 0, at20 = -1;
    int monot = 1, steps = 0;
    for (int f = 1; f <= 102; f++) { /* 1.7 s at 60 Hz, straight back (away from Talion) */
        float nat0 = reel_nat((f - 1) / 102.0f), nat1 = reel_nat(f / 102.0f), xf[3] = {was[0], 0, was[2] - (nat1 - nat0)};
        steps += sc_reel_cap(an, tal, was, xf, 55, &cut);
        float d = hypotf(xf[0] - an[0], xf[2] - an[2]);
        monot &= d >= prev - 1e-3f;
        if (at20 < 0 && nat1 >= 20) at20 = d - nat1;
        prev = d, memcpy(was, xf, sizeof was);
    }
    CHECK(prev <= 55.01f && prev >= 50, "a 145 cm reel capped at 55: ends %g cm from the anchor", prev);
    CHECK(monot && steps > 0, "the reel only goes back (monotonic), %d steps eased", steps);
    CHECK(NEAR(at20, 0, 0.5), "its first 20 cm 1:1 (off by %g)", at20);
    /* a short reaction (24 cm, under the knee): untouched */
    float w2[3] = {0, 0, -120}, x2[3] = {0, 0, -144};
    CHECK(!sc_reel_cap(an, tal, w2, x2, 55, &cut) && x2[2] == -144, "a 24 cm reaction passes");
    /* a step toward Talion (the walk back, or past the anchor): free */
    float w3[3] = {0, 0, -170}, x3[3] = {0, 0, -100};
    CHECK(!sc_reel_cap(an, tal, w3, x3, 55, &cut) && x3[2] == -100, "a step nearer Talion passes");
    /* beyond the cap already (switched on mid-reel): not pulled in, not further out */
    float w4[3] = {0, 0, -220}, x4[3] = {0, 0, -230};
    CHECK(sc_reel_cap(an, tal, w4, x4, 55, &cut) && NEAR(x4[2], -220, 0.01), "beyond the cap: it stays (%g)", x4[2]);
    /* sideways at the cap: slides along it, never past it */
    float w5[3] = {0, 0, -175}, x5[3] = {30, 0, -175};
    sc_reel_cap(an, tal, w5, x5, 55, &cut);
    CHECK(hypotf(x5[0] - an[0], x5[2] - an[2]) <= 55.01f, "sideways at the cap: %g %g", x5[0], x5[2]);
    /* a warp-like absolute path (xf = the anchor + the reel's offset each frame, whatever was): still under the cap */
    float w6[3] = {0, 0, -120};
    for (int f = 1; f <= 102; f++) {
        float x6[3] = {0, 0, -120 - reel_nat(f / 102.0f)};
        sc_reel_cap(an, tal, w6, x6, 55, &cut);
        memcpy(w6, x6, sizeof w6);
    }
    CHECK(hypotf(w6[0] - an[0], w6[2] - an[2]) <= 55.01f, "an absolute path: %g", w6[2]);
    float x7[3] = {0, 0, -300}, w7[3] = {0, 0, -120};
    CHECK(!sc_reel_cap(an, tal, w7, x7, 0, &cut) && x7[2] == -300, "cap 0: off");
    printf("reel: a 145 cm stagger ends at %.1f cm (the cap 55), 1:1 for its first 20 cm\n", prev);
}

/* build 17: the hit's kind for its feedback, and which bite (a cut that landed only; no bite on light) */
static void test_hit_kind(void) {
    CHECK(sc_hit_kind(SC_TIER_HEAVY, 10, 60) == SC_KIND_CUT && sc_hit_kind(SC_TIER_HEAVY, NAN, 60) == SC_KIND_CUT,
          "heavy = a cut");
    CHECK(sc_hit_kind(SC_TIER_STAB, 87, 60) == SC_KIND_STAB, "stab");
    CHECK(sc_hit_kind(SC_TIER_LIGHT, 85, 60) == SC_KIND_SLAP && sc_hit_kind(SC_TIER_LIGHT, 20, 60) == SC_KIND_TAP &&
              sc_hit_kind(SC_TIER_LIGHT, NAN, 60) == SC_KIND_TAP && sc_hit_kind(SC_TIER_LIGHT, 60, 60) == SC_KIND_TAP,
          "light: flat leading = a slap, else a tap");
    CHECK(sc_hit_kind(SC_TIER_NONE, 10, 60) == SC_KIND_NONE && !strcmp(sc_kind_name(SC_KIND_SLAP), "slap") &&
              !strcmp(sc_kind_name(99), "none"),
          "none; names");
    CHECK(sc_kind_bites(SC_KIND_CUT, 0, 0) && !sc_kind_bites(SC_KIND_CUT, 1, 0) && !sc_kind_bites(SC_KIND_CUT, 0, 1),
          "a landed cut bites; a glance or a blocked cut does not");
    CHECK(!sc_kind_bites(SC_KIND_TAP, 0, 0) && !sc_kind_bites(SC_KIND_SLAP, 0, 0) && !sc_kind_bites(SC_KIND_STAB, 0, 0),
          "no bite on a tap, a slap or a stab");
}

/* ---- build 20: SOMVR_COMBAT_SWING=stroke - a cut needs a real swing ------------------------------------------- */
/* a blade about a still hand at the origin turned by q (the replay's lay-out: the grip's +Z the blade, +x the flat) */
static void qrot3t(const float q[4], const float v[3], float o[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty), o[1] = v[1] + w * ty + (z * tx - x * tz), o[2] = v[2] + w * tz + (x * ty - y * tx);
}
static void stroke_blade(const float q[4], double t, const somvr_blade *prev, somvr_blade *b) {
    static const float zt[3] = {0, 0, 100}, zb[3] = {0, 0, -8}, xf[3] = {1, 0, 0};
    memset(b, 0, sizeof *b);
    qrot3t(q, zt, b->tip), qrot3t(q, zb, b->base), qrot3t(q, xf, b->flat_n);
    b->t = t, b->valid = 1;
    double dt = prev && prev->valid ? t - prev->t : 0;
    if (dt > 0 && dt <= 0.1)
        for (int i = 0; i < 3; i++)
            b->vel_tip[i] = (float)((b->tip[i] - prev->tip[i]) / dt), b->vel_base[i] = (float)((b->base[i] - prev->base[i]) / dt);
}
/* a still blade along +z moved by v (cm/s) for dt: the next sample */
static somvr_blade moved(const somvr_blade *p, const float v[3], double dt) {
    somvr_blade b = *p;
    for (int i = 0; i < 3; i++) b.base[i] += (float)(v[i] * dt), b.tip[i] += (float)(v[i] * dt), b.vel_tip[i] = v[i], b.vel_base[i] = v[i];
    b.t += dt;
    return b;
}

static void test_stroke(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    CHECK(c.stroke == 0 && c.tip_max_mps == 0 && NEAR(c.cut_mps, 12, 0) && NEAR(c.full_mps, 20, 0) && NEAR(c.rev_cos, -0.3, 1e-6),
          "the defaults: off, no drop, 12 / 20 m/s, a turn past -0.3");
    c.stroke = 1, c.tip_max_mps = 60;
    sc_speed s;
    sc_speed_reset(&s);
    /* one stroke at 10 m/s along +x for 18 samples: 17 of them move it, 189 cm reached */
    somvr_blade b = blade(0, 0, 0, 0, 0, 100, 0);
    b.flat_n[1] = 1; /* the flat facing +y: an x motion is edge-on */
    float vx[3] = {1000, 0, 0}, vmx[3] = {-1000, 0, 0};
    for (int k = 0; k < 18; k++) b = moved(&b, vx, 1 / HZ), sc_speed_push(&s, &c, &b);
    CHECK(s.above && NEAR(s.reach, 1700 / HZ * 10, 0.5) && NEAR(s.stroke_cm, s.reach, 0.01) && s.strokes == 1,
          "one stroke: reach %g path %g strokes %llu",
          s.reach, s.stroke_cm, (unsigned long long)s.strokes);
    CHECK(sc_tier_swing(&c, &s, 0, 1) == SC_TIER_LIGHT, "10 m/s at the tip: under the cut's floor, light");
    for (int k = 0; k < 6; k++) b = moved(&b, vx, 1 / HZ), b.vel_tip[0] = 1600, sc_speed_push(&s, &c, &b);
    CHECK(sc_tier_swing(&c, &s, 0, 1) == SC_TIER_HEAVY, "16 m/s at the tip, a 2 m stroke: a cut (mean %g)", s.mean_mps);
    CHECK(sc_tier_swing(&c, &s, 75, 1) == SC_TIER_LIGHT, "... the flat leading: light");
    CHECK(sc_tier_swing(&c, &s, 0, 0) == SC_TIER_LIGHT, "... at the base (the hand at 10 m/s): light (contact %g m/s)",
          sc_contact_mps(&s, 0));
    float k1 = sc_cut_k(&c, 12), k2 = sc_cut_k(&c, 16), k3 = sc_cut_k(&c, 30);
    CHECK(NEAR(k1, 0.6, 1e-5) && NEAR(k2, 0.8, 1e-5) && NEAR(k3, 1, 0) && NEAR(sc_cut_k(&c, NAN), 0.6, 1e-6),
          "the damage factor: %g %g %g", k1, k2, k3);
    /* the blade came into the orc in this stroke: a touch 0.05 s old in a stroke 0.2 s old yes, 0.3 s old no */
    double now = s.last_t;
    CHECK(NEAR(now - s.stroke_t0, 23 / HZ, 1e-6) && sc_stroke_entered(&s, 0.05f, now) && sc_stroke_entered(&s, 0, now) &&
              !sc_stroke_entered(&s, 0.3f, now),
          "entered in the stroke (%.3f s old): a 0.05 s touch yes, a 0.3 s one no", now - s.stroke_t0);
    /* the tip turns back: a new stroke; its reach starts again */
    b = moved(&b, vmx, 1 / HZ), b.vel_tip[0] = -1600, sc_speed_push(&s, &c, &b);
    CHECK(s.strokes == 2 && s.reach < 20, "a turn back: stroke %llu, reach %g", (unsigned long long)s.strokes, s.reach);
    CHECK(sc_tier_swing(&c, &s, 0, 1) != SC_TIER_HEAVY, "fast but just turned: no cut");
    CHECK(!sc_stroke_entered(&s, 0.05f, s.last_t) && sc_stroke_entered(&s, 0, s.last_t),
          "the new stroke began after a touch 0.05 s old: not brought in by it");
    /* a wiggle: +-20 deg of a 100 cm blade about the hand at 4 Hz, 2 s: fast at the tip (~9 m/s peak), short strokes */
    sc_speed_reset(&s);
    sc_cfg old;
    sc_cfg_default(&old);
    sc_speed so;
    sc_speed_reset(&so);
    somvr_blade pv = {0}, w;
    int cuts_new = 0, cuts_old = 0, light_new = 0;
    float reach_max = 0;
    for (int k = 0; k <= 180; k++) {
        double t = k / HZ;
        float a = DEG(20) * sinf((float)(2 * 3.14159265358979 * 4 * t)), q[4] = {sinf(a / 2), 0, 0, cosf(a / 2)}; /* edge on */
        stroke_blade(q, t, &pv, &w);
        sc_speed_push(&s, &c, &w), sc_speed_push(&so, &old, &w);
        if (k > 20) {
            float e = sc_edge_deg(&w);
            cuts_new += sc_tier_swing(&c, &s, e, 1) == SC_TIER_HEAVY, light_new += sc_tier_swing(&c, &s, e, 1) == SC_TIER_LIGHT;
            cuts_old += sc_tier_cut(&old, &so, e) == SC_TIER_HEAVY;
            reach_max = fmaxf(reach_max, s.reach);
        }
        pv = w;
    }
    CHECK(cuts_new == 0 && cuts_old > 100, "a 4 Hz +-20 deg wiggle: cuts on %d frames (build 16's rule %d), light %d, the reach "
          "%.0f cm at most", cuts_new, cuts_old, light_new, reach_max);
    /* a tracking jump: a sample at 130 m/s is dropped, not a stab */
    sc_speed_reset(&s);
    b = blade(0, 0, 0, 0, 0, 100, 0);
    float still[3] = {0, 0, 0};
    for (int k = 0; k < 10; k++) b = moved(&b, still, 1 / HZ), sc_speed_push(&s, &c, &b);
    somvr_blade j = moved(&b, still, 1 / HZ);
    j.vel_tip[2] = 13000, j.vel_base[2] = 13000;
    CHECK(sc_speed_push(&s, &c, &j) == 0 && s.drops == 1 && s.axis_mps < 1 && sc_tier_swing(&c, &s, NAN, 1) == SC_TIER_NONE,
          "a 130 m/s jump along the blade: dropped (drops %llu, axis %g)", (unsigned long long)s.drops, s.axis_mps);
    sc_speed_reset(&so);
    for (int k = 0; k < 10; k++) sc_speed_push(&so, &old, &b);
    sc_speed_push(&so, &old, &j);
    CHECK(sc_tier_cut(&old, &so, NAN) == SC_TIER_STAB, "... build 16's rule: a stab (the session's 132 m/s kill)");
    printf("stroke: one-way reach, the contact point's speed (cut >= %.0f m/s, full damage from %.0f), jumps over %.0f m/s "
           "dropped; a 4 Hz wiggle: 0 cuts (build 16: %d frames)\n", c.cut_mps, c.full_mps, c.tip_max_mps, cuts_old);
}

/* the user's own motion (tests/swing_b19b.h): their real cuts stay cuts, the flail of mark 15 does not cut */
static void test_stroke_replay(void) {
    sc_cfg c, old;
    sc_cfg_default(&c), sc_cfg_default(&old);
    c.stroke = 1, c.tip_max_mps = 60;
    sc_speed s, so;
    sc_speed_reset(&s), sc_speed_reset(&so);
    int nf = (int)(sizeof g_swing_b19b / sizeof g_swing_b19b[0]), nh = (int)(sizeof g_swing_b19b_hits / sizeof g_swing_b19b_hits[0]);
    int hi = 0, real = 0, real_cut = 0, flail = 0, flail_cut = 0, flail_old_cut = 0;
    somvr_blade pv = {0}, b;
    for (int k = 0; k < nf && hi < nh; k++) {
        const float *r = g_swing_b19b[k];
        if (k && r[0] - g_swing_b19b[k - 1][0] > 0.1f) /* a new window */
            sc_speed_reset(&s), sc_speed_reset(&so), pv.valid = 0;
        stroke_blade(r + 1, r[0], &pv, &b);
        sc_speed_push(&s, &c, &b), sc_speed_push(&so, &old, &b);
        pv = b;
        while (hi < nh && (k + 1 >= nf || g_swing_b19b[k + 1][0] > g_swing_b19b_hits[hi].t + 1e-4f)) {
            float e = sc_edge_deg(&b), al = g_swing_b19b_hits[hi].along, v = sc_contact_mps(&s, al);
            int tn = sc_tier_swing(&c, &s, e, al), to = sc_tier_cut(&old, &so, e);
            char kd = g_swing_b19b_hits[hi].kind;
            printf("  replay %-38s %s now %-5s (build 16: %-5s) tip %5.1f m/s (logged %5.2f) at %.2f: %5.1f m/s, stroke %4.0f cm, "
                   "edge %3.0f, k %.2f\n", g_swing_b19b_hits[hi].what, kd == 'R' ? "real " : kd == 'W' ? "flail" : "info ",
                   sc_tier_name(tn), sc_tier_name(to), s.mean_mps, g_swing_b19b_hits[hi].logged, al, v, s.reach, e,
                   tn == SC_TIER_HEAVY ? sc_cut_k(&c, v) : 0);
            if (kd == 'R') {
                real++, real_cut += tn == SC_TIER_HEAVY;
                CHECK(tn == SC_TIER_HEAVY, "%s: still a cut", g_swing_b19b_hits[hi].what);
            } else if (kd == 'W') {
                flail++, flail_cut += tn == SC_TIER_HEAVY, flail_old_cut += to == SC_TIER_HEAVY;
                CHECK(tn != SC_TIER_HEAVY, "%s: no cut", g_swing_b19b_hits[hi].what);
            }
            hi++;
        }
    }
    CHECK(hi == nh && real == 7 && flail == 4, "every contact replayed (%d of %d)", hi, nh);
    CHECK(flail_old_cut >= 1, "build 16's rule made the flail cut on the replay too (%d)", flail_old_cut);
    printf("stroke replay (the build 19b session's own grip, %d frames): real cuts %d of %d stay cuts; the flail's %d contacts: "
           "%d cuts (build 16's rule: %d)\n", nf, real_cut, real, flail, flail_cut, flail_old_cut);
}

int main(void) {
    test_space();
    test_body();
    test_reel();
    test_cut();
    test_touch();
    test_seg();
    test_sweep();
    test_heading();
    test_speed();
    test_tiers();
    test_cool_charges();
    test_fake();
    test_fake_fight();
    test_controller();
    test_controller_fight();
    test_react();
    test_react_direct();
    test_react_fight();
    test_swing();
    test_swing_fight();
    test_hit_kind();
    test_stroke();
    test_stroke_replay();
    printf("test_combat: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
