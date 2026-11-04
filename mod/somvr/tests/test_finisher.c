/* test_finisher.c - SOMVR_FINISHER's pure part: the snap, the guided step, the path */
#include "../somvr_finisher.h"

#include <math.h>
#include <stdio.h>

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                      \
    do {                                                                                                                   \
        g_n++;                                                                                                             \
        if (!(c)) {                                                                                                        \
            g_fail++;                                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                    \
            printf(__VA_ARGS__);                                                                                           \
            printf("\n");                                                                                                  \
        }                                                                                                                  \
    } while (0)

int main(void)
{
    somvr_fn_cfg c;
    somvr_fn_defaults(&c);
    float ot[3] = {0, 150, 100}, od[3] = {0, 0, 1}, d, a;
    float t1[3] = {5, 152, 98}, ax1[3] = {0.2f, 0, 1};
    CHECK(somvr_fn_aligned(&c, t1, ax1, ot, od, &d, &a) && d < 6 && a < 12, "near and 11 deg off: snaps (%f %f)", d, a);
    float t2[3] = {15, 150, 100};
    CHECK(!somvr_fn_aligned(&c, t2, od, ot, od, &d, &a), "15 cm off: no");
    float ax3[3] = {0.6f, 0, 1};
    CHECK(!somvr_fn_aligned(&c, ot, ax3, ot, od, &d, &a), "31 deg off: no (%f)", a);
    /* the step: a hand at the reference speed along the way earns the top speed */
    float v[3] = {0, 0, 100}, rate, m;
    float p = somvr_fn_step(&c, v, od, 0, 0.1f, &rate, &m);
    CHECK(fabsf(rate - 100) < 1e-3f && fabsf(p - 10.0f / 37.0f) < 1e-4f, "on the way at v_ref: v_max (%f %f)", rate, p);
    float vs[3] = {0, 0, 50};
    somvr_fn_step(&c, vs, od, 0, 0.1f, &rate, &m);
    CHECK(fabsf(rate - 50) < 1e-3f, "half the speed: half the rate (%f)", rate);
    float v15[3] = {0, 0, 150};
    somvr_fn_step(&c, v15, od, 0, 0.1f, &rate, &m);
    CHECK(fabsf(rate - 100) < 1e-3f, "a real thrust's 1.5 m/s: the full rate (%f)", rate);
    float v2[3] = {0, 0, 320};
    somvr_fn_step(&c, v2, od, 0, 0.1f, &rate, &m);
    CHECK(fabsf(rate - 50) < 1e-3f, "twice the band's top: half the rate (%f)", rate);
    float vx[3] = {90, 0, 0};
    somvr_fn_step(&c, vx, od, 0, 0.1f, &rate, &m);
    CHECK(rate == 0, "across the way: nothing");
    float vb[3] = {0, 0, -100};
    p = somvr_fn_step(&c, vb, od, 0.5f, 0.1f, &rate, &m);
    CHECK(rate == 0 && p == 0.5f, "backwards while going in: nothing");
    p = somvr_fn_step(&c, v, od, 0.95f, 0.5f, &rate, &m);
    CHECK(p == 1.0f, "the deepest point is not skipped (%f)", p);
    p = somvr_fn_step(&c, vb, od, 1.0f, 0.1f, &rate, &m);
    CHECK(rate > 99 && p > 1.0f, "out: the way turns (%f %f)", rate, p);
    float fast[3] = {0, 0, 400};
    somvr_fn_step(&c, fast, od, 0, 0.01f, &rate, &m);
    CHECK(fabsf(rate - 40) < 1e-3f, "a flick: no faster - slower (%f)", rate);
    float s0[3] = {0, 0, 0}, q[3];
    somvr_fn_point(&c, s0, od, 1.0f, q);
    CHECK(fabsf(q[2] - 37) < 1e-4f && fabsf(somvr_fn_depth(&c, 1.0f) - 22) < 1e-4f, "deepest: 22 cm in");
    somvr_fn_point(&c, s0, od, 2.0f, q);
    CHECK(fabsf(q[2]) < 1e-4f && somvr_fn_depth(&c, 0) < 0, "back out at the start");
    printf("test_finisher: %d checks, %d failed\n", g_n, g_fail);
    return g_fail != 0;
}
