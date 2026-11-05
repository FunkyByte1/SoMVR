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

    /* build 37 (re-xr-pipeline.md 171): the way out follows the hand */
    CHECK(somvr_fn_out_p(&c, 0) == 1.0f && somvr_fn_out_p(&c, 1.0f) == 1.0f, "no pull (or within the breakaway): deepest");
    CHECK(somvr_fn_out_p(&c, -10) == 1.0f, "a push further in: never deeper than the deepest");
    CHECK(somvr_fn_out_p(&c, 500) == 2.0f, "a long pull: back to the start, no further");
    /* a 1 m/s pull at 90 Hz: the point leaves the neck (the depth below 0) within 0.4 s, every frame some way out */
    float pp = 1, t_out = -1;
    int mono_ok = 1;
    for (int f = 1; f <= 90 && t_out < 0; f++) {
        float np = somvr_fn_out_p(&c, 100.0f * f / 90.0f);
        mono_ok &= np >= pp, pp = np;
        if (somvr_fn_depth(&c, np) < 0)
            t_out = f / 90.0f;
    }
    CHECK(t_out > 0 && t_out <= 0.4f && mono_ok, "a 1 m/s pull: out in %.3f s (<= 0.4), never back", t_out);
    /* a turn short of the deepest (0.8 of the line: 7.6 cm into the neck): the way out starts there, the same depth */
    float s8 = 0.8f, p8 = somvr_fn_out_p2(&c, 0, s8);
    CHECK(fabsf(p8 - 1.2f) < 1e-5f && fabsf(somvr_fn_depth(&c, p8) - somvr_fn_depth(&c, s8)) < 1e-4f &&
              somvr_fn_out_p2(&c, 500, s8) == 2.0f && somvr_fn_out_p2(&c, -5, s8) == p8,
          "out from a turn at 0.8: p %.3f, the depth kept (%.2f cm), back to the start, never deeper", p8, somvr_fn_depth(&c, p8));
    /* a pull 45 deg off the wound's axis (the hand back toward the body, not along the stab's line - the 11-04 session's
     * way in ran right, up and ahead): its share along the axis still draws the blade out within half a second; the
     * guided way out (build 33) earned 0.45 of its speed there (out after 0.5 s) */
    float t45 = -1, tg = -1, gp = 1.0f, v45[3] = {0, -70.71f, -70.71f};
    for (int f = 1; f <= 900 && (t45 < 0 || tg < 0); f++) {
        if (t45 < 0 && somvr_fn_depth(&c, somvr_fn_out_p(&c, 70.71f * f / 90.0f)) < 0)
            t45 = f / 90.0f;
        gp = somvr_fn_step(&c, v45, od, gp, 1.0f / 90.0f, &rate, &m);
        if (tg < 0 && somvr_fn_depth(&c, gp) < 0)
            tg = f / 90.0f;
    }
    CHECK(t45 > 0 && t45 <= 0.4f && tg > t45 + 0.1f, "45 deg off at 1 m/s: out in %.3f s by the hand, %.2f s guided", t45, tg);
    CHECK(somvr_fn_ease(0) == 0 && somvr_fn_ease(1) == 1 && fabsf(somvr_fn_ease(0.5f) - 0.5f) < 1e-6f &&
              somvr_fn_ease(0.25f) < 0.25f && somvr_fn_ease(2) == 1 && somvr_fn_ease(NAN) == 0,
          "the pull's ease: 0..1, slow in and out");
    /* build 37: a pull back before the deepest draws the blade back with the hand, no further than the start */
    c.back_free = 1;
    p = somvr_fn_step(&c, vb, od, 0.5f, 0.1f, &rate, &m);
    CHECK(fabsf(p - (0.5f - 10.0f / 37.0f)) < 1e-4f && rate < 0, "back_free: a 1 m/s pull back on the way in, 10 cm back (%f)", p);
    p = somvr_fn_step(&c, vb, od, 0.1f, 0.1f, &rate, &m);
    CHECK(p == 0, "back_free: not past the start (%f)", p);
    p = somvr_fn_step(&c, v, od, 0.5f, 0.1f, &rate, &m);
    CHECK(fabsf(rate - 100) < 1e-3f, "back_free: the way in still guided (%f)", rate);
    c.back_free = 0;
    /* build 37 (the 22:09 session, mark 221801_7): the user's own deliberate thrust, recorded (input.inrec, t 182062.30 ..
     * 182062.72, 90 Hz): the hand's speed along the way in and across it (cm/s, smoothed 40 ms as the game does) - ~60 cm
     * in 0.3 s, a 3 m/s peak. Build 33's rule left the blade at the neck (the hand done, the blade crawling on: "I can't
     * move the thing fast enough"); the follow rule takes it to full depth within the thrust, the start 15 cm short */
    static const float rec[][2] = {{28, 19}, {31, 17}, {38, 17}, {57, 12}, {55, 3}, {86, 14}, {86, 20}, {106, 28},
        {125, 34}, {141, 116}, {199, 107}, {225, 95}, {244, 86}, {217, 72}, {272, 83}, {238, 72}, {288, 162}, {292, 134},
        {296, 114}, {296, 98}, {233, 77}, {244, 78}, {238, 79}, {230, 70}, {206, 138}, {205, 106}, {243, 89}, {208, 84},
        {140, 61}, {114, 54}, {99, 42}, {103, 40}, {65, 40}, {12, 111}, {2, 97}, {3, 80}, {5, 65}, {15, 55}};
    int nrec = (int)(sizeof rec / sizeof rec[0]);
    float pb = 0, pf = 0;
    somvr_fn_cfg cf = c;
    cf.follow = 1, cf.back_free = 1, cf.v_max = 400;
    for (int i = 0; i < nrec; i++) {
        float vr[3] = {rec[i][1], 0, rec[i][0]};
        pb = somvr_fn_step(&c, vr, od, pb, 1.0f / 90, &rate, &m);
        pf = somvr_fn_step(&cf, vr, od, pf, 1.0f / 90, &rate, &m);
    }
    CHECK(pf == 1.0f && pb < 0.8f, "the recorded 3 m/s thrust: follow at full depth (p %.2f); build 33's "
          "rule %.1f cm in (p %.2f)", pf, somvr_fn_depth(&c, pb), pb);
    float drift[3] = {0, 0, 10}, pd = 0.3f;
    for (int i = 0; i < 900; i++)
        pd = somvr_fn_step(&cf, drift, od, pd, 1.0f / 90, &rate, &m);
    CHECK(pd == 0.3f, "follow: a resting hand's 10 cm/s drift for 10 s moves nothing (p %.3f)", pd);
    float slow[3] = {0, 0, 40}, ps = 0;
    ps = somvr_fn_step(&cf, slow, od, ps, 0.5f, &rate, &m);
    CHECK(fabsf(ps * 37 - 20) < 1e-3f && rate == 40, "follow: a slow deliberate push, 1:1 (20 cm in 0.5 s at 40 cm/s: %.2f)", ps * 37);
    float side[3] = {100, 0, 0};
    ps = somvr_fn_step(&cf, side, od, 0, 0.5f, &rate, &m);
    CHECK(ps == 0, "follow: across the line, nothing");
    /* build 37 (171.6; the user's 22:37 marks): the stab's start within a foot of the body, the arm's full extension */
    somvr_fn_place_cfg pc;
    somvr_fn_place_defaults(&pc);
    float g[3], dr[3], gap, ext;
    int why;
    struct { float neck[3]; int ok; const char *what; } pl[] = {
        {{-5, -85, 85}, 1, "a hunched orc at the fight's distance"},
        {{0, -60, 125}, 1, "an orc a step farther"},
        {{0, -50, 190}, 0, "an orc 1.9 m off"},
        {{5, -40, 35}, 0, "an orc in the face"},
    };
    for (int i = 0; i < 4; i++) {
        int ok = somvr_fn_place(&pc, pl[i].neck, g, dr, &gap, &ext, &why);
        float gb = sqrtf((g[0] - pc.body[0]) * (g[0] - pc.body[0]) + (g[1] - pc.body[1]) * (g[1] - pc.body[1]) +
                         (g[2] - pc.body[2]) * (g[2] - pc.body[2]));
        float pt[3] = {g[0] + dr[0] * pc.blade, g[1] + dr[1] * pc.blade, g[2] + dr[2] * pc.blade};
        float pn = sqrtf((pl[i].neck[0] - pt[0]) * (pl[i].neck[0] - pt[0]) + (pl[i].neck[1] - pt[1]) * (pl[i].neck[1] - pt[1]) +
                         (pl[i].neck[2] - pt[2]) * (pl[i].neck[2] - pt[2]));
        if (pl[i].ok)
            CHECK(ok && gb <= 30.01f && gap >= 6 && gap <= 30 && fabsf(pn - gap) < 0.01f && ext <= 70.01f &&
                      ext > (i == 0 ? 40 : 60), /* (at the fight's distance the line angles down: a 95 cm blade a foot out) */
                  "place, %s: the grip %.1f cm from the body, the point %.1f cm short of the neck, the hand's end %.1f cm out "
                  "(the travel %.1f cm)", pl[i].what, gb, gap, ext, gap + pc.depth);
        else
            CHECK(!ok && why == (i == 2 ? SOMVR_FN_WHY_REACH : SOMVR_FN_WHY_NEAR), "place, %s: none (why %d: %s)", pl[i].what,
                  why, somvr_fn_why_text(why));
    }
    /* build 33's rule for the 1.9 m orc: the guard on the 110 cm sphere round the neck nearest the natural grip (15
     * right, 30 under, 35 ahead of the eyes) - the grip ~70 cm out, the arm at full reach before the stab began (its
     * reach rule let the grip be 70 cm out from the eyes) */
    float nat[3] = {15, -30, 35}, *nk = pl[2].neck, vn[3] = {nat[0] - nk[0], nat[1] - nk[1], nat[2] - nk[2]};
    float ln = sqrtf(vn[0] * vn[0] + vn[1] * vn[1] + vn[2] * vn[2]), g33[3];
    for (int k = 0; k < 3; k++)
        g33[k] = nk[k] + vn[k] / ln * 110.0f;
    float gb33 = sqrtf((g33[0] - pc.body[0]) * (g33[0] - pc.body[0]) + (g33[1] - pc.body[1]) * (g33[1] - pc.body[1]) +
                       (g33[2] - pc.body[2]) * (g33[2] - pc.body[2]));
    CHECK(gb33 > 60, "build 33's start for the 1.9 m orc: the grip %.0f cm from the body", gb33);
    /* the hint's reason: the first that fails, in the order the player fixes them */
    float cmin = cosf(40 * 0.01745329f);
    CHECK(somvr_fn_why(1000, 230, 1, cmin, 170, 70, 1) == SOMVR_FN_WHY_FAR, "10 m, back turned: too far first");
    CHECK(somvr_fn_why(150, 230, 1, cmin, 170, 70, 1) == SOMVR_FN_WHY_BACK, "in reach, back turned: he must face you");
    CHECK(somvr_fn_why(150, 230, 0.2f, cmin, 10, 70, 1) == SOMVR_FN_WHY_VIEW, "in reach, facing, beside: look at him");
    CHECK(somvr_fn_why(150, 230, 1, cmin, 10, 70, 0) == SOMVR_FN_WHY_REACH, "the neck out of the arm's reach: closer");
    CHECK(somvr_fn_why(150, 230, 1, cmin, 10, 70, 1) == SOMVR_FN_WHY_OK, "all of it: it qualifies");
    CHECK(somvr_fn_why(INFINITY, 230, 1, cmin, 10, 70, 1) == SOMVR_FN_WHY_NONE &&
              somvr_fn_why_text(SOMVR_FN_WHY_NONE)[0] == 'n' && somvr_fn_why_text(99)[0] == '?',
          "no orc: its own reason; an unknown one '?'");
    printf("test_finisher: %d checks, %d failed\n", g_n, g_fail);
    return g_fail != 0;
}
