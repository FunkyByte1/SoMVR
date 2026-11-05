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

    /* ---- build 41 (som-b41-finlib): the library ---- */
    for (int k = 0; k < SOMVR_FNK_N; k++) {
        const somvr_fn_kind *kd = somvr_fn_kind_of(k);
        CHECK(kd && kd->id && kd->name && kd->row && kd->row[0] && kd->node >= 1 && kd->node <= 3 && kd->snd_in &&
                  kd->snd_kill[0] && kd->snd_kill[0][0] && somvr_fn_kind_word(kd->id) == k &&
                  somvr_fn_kind_word(kd->name) == k,
              "kind %d: its table row whole, its word and name read back", k);
        CHECK(kd->motion == SOMVR_FNM_STAB ? kd->depth_cm > 5 : kd->cut_from_deg > 0 && kd->cut_to_deg < 0,
              "kind %d: a stab's depth, a cut through its node (from one side to the other)", k);
    }
    CHECK(!somvr_fn_kind_of(SOMVR_FNK_N) && somvr_fn_kind_word("AUTO") == SOMVR_FNK_N && somvr_fn_kind_word("Behead") ==
              SOMVR_FNK_BEHEAD && somvr_fn_kind_word("nope") == -1 && somvr_fn_kind_word(NULL) == -1,
          "the words: auto, any case, unknown");
    CHECK(somvr_fn_kind_of(SOMVR_FNK_BEHEAD)->sever && somvr_fn_kind_of(SOMVR_FNK_STUNNED)->sever &&
              !somvr_fn_kind_of(SOMVR_FNK_NECK)->sever && somvr_fn_kind_of(SOMVR_FNK_STUNNED)->cut_up > 0.5f &&
              somvr_fn_kind_of(SOMVR_FNK_BEHEAD)->cut_up < 0.5f,
          "the cuts sever (the swipe level, the chop from above); the stabs do not");
    /* the pick: the flat game's choice by state and side */
    int F = SOMVR_FNS_FRONT | SOMVR_FNS_AWARE, B = SOMVR_FNS_BACK | SOMVR_FNS_AWARE;
    CHECK(somvr_fn_pick_kind(F, 0, SOMVR_FNK_N) == SOMVR_FNK_NECK && somvr_fn_pick_kind(F, 1, SOMVR_FNK_N) == SOMVR_FNK_BEHEAD &&
              somvr_fn_pick_kind(F, 2, SOMVR_FNK_N) == SOMVR_FNK_FACE && somvr_fn_pick_kind(F, 3, SOMVR_FNK_N) == SOMVR_FNK_NECK,
          "a fighting orc facing Talion: neck, behead, face in turn");
    CHECK(somvr_fn_pick_kind(B, 0, SOMVR_FNK_N) == SOMVR_FNK_BACK &&
              somvr_fn_pick_kind(SOMVR_FNS_BACK, 0, SOMVR_FNK_N) == -1,
          "its back to Talion: the back stab - not on an unaware back (the stealth kill's)");
    CHECK(somvr_fn_pick_kind(F | SOMVR_FNS_DOWNED, 1, SOMVR_FNK_N) == SOMVR_FNK_DOWNED &&
              somvr_fn_pick_kind(B | SOMVR_FNS_DOWNED, 1, SOMVR_FNK_N) == SOMVR_FNK_DOWNED &&
              somvr_fn_pick_kind(SOMVR_FNS_DOWNED, 1, SOMVR_FNK_N) == SOMVR_FNK_DOWNED,
          "downed: the stab down, from any side");
    CHECK(somvr_fn_pick_kind(F | SOMVR_FNS_STUNNED, 0, SOMVR_FNK_N) == SOMVR_FNK_STUNNED &&
              somvr_fn_pick_kind(B | SOMVR_FNS_STUNNED, 0, SOMVR_FNK_N) == SOMVR_FNK_STUNNED,
          "stunned: the chop, from either side");
    CHECK(somvr_fn_pick_kind(F | SOMVR_FNS_COUNTER, 0, SOMVR_FNK_N) == SOMVR_FNK_COUNTER &&
              somvr_fn_pick_kind(B | SOMVR_FNS_COUNTER, 0, SOMVR_FNK_N) == SOMVR_FNK_BACK,
          "a parry's opening facing Talion: the counter kill (its back: the back stab)");
    CHECK(somvr_fn_pick_kind(0, 0, SOMVR_FNK_N) == -1 && somvr_fn_pick_kind(SOMVR_FNS_AWARE, 0, SOMVR_FNK_N) == -1,
          "side on: none");
    CHECK(somvr_fn_pick_kind(F, 0, SOMVR_FNK_FACE) == SOMVR_FNK_FACE && somvr_fn_pick_kind(F, 0, SOMVR_FNK_DOWNED) == -1 &&
              somvr_fn_pick_kind(SOMVR_FNS_BACK, 0, SOMVR_FNK_BACK) == SOMVR_FNK_BACK &&
              somvr_fn_pick_kind(F, 0, SOMVR_FNK_COUNTER) == -1 &&
              somvr_fn_pick_kind(F | SOMVR_FNS_DOWNED, 0, SOMVR_FNK_NECK) == -1,
          "the test's type: that one when the situation allows it (an unaware back too), else none");
    /* the cut's arc: a hunched fighting orc's neck at the fight's distance (as above: 85 cm under the eyes, ~1 m ahead) */
    somvr_fn_place_cfg pq;
    somvr_fn_place_defaults(&pq);
    float nk1[3] = {0, -70, 115};
    somvr_fn_arc arc;
    int wy = -9;
    CHECK(somvr_fn_arc_place(&pq, nk1, 0, 50, -45, &arc, &wy) && wy == SOMVR_FN_WHY_OK, "the beheading placed at 1.15 m");
    float gg[3], dd[3], wa[3];
    somvr_fn_arc_at(&arc, 0, gg, dd, wa);
    float gr = sqrtf((gg[0] - pq.body[0]) * (gg[0] - pq.body[0]) + (gg[1] - pq.body[1]) * (gg[1] - pq.body[1]) +
                     (gg[2] - pq.body[2]) * (gg[2] - pq.body[2]));
    float hz = sqrtf(gg[0] * gg[0] + gg[2] * gg[2]);
    CHECK(gr <= 50.5f && gg[0] > pq.body[0] && fabsf(dd[1]) < 0.5f && hz < 65,
          "its start: the grip %.0f cm from the shoulder, to the sword side (x %.0f), %.0f cm out from the eyes (the arm's "
          "reach 70), the blade near level (%.2f)", gr, gg[0], hz, dd[1]);
    CHECK(fabsf(dd[0] * wa[0] + dd[1] * wa[1] + dd[2] * wa[2]) < 1e-4f && wa[0] < 0,
          "the way across the blade, toward the other side (%.2f)", wa[0]);
    float ph = somvr_fn_arc_hit_p(&arc);
    somvr_fn_arc_at(&arc, ph, gg, dd, wa);
    float along = (nk1[0] - gg[0]) * dd[0] + (nk1[1] - gg[1]) * dd[1] + (nk1[2] - gg[2]) * dd[2];
    float off = sqrtf(powf(nk1[0] - gg[0] - along * dd[0], 2) + powf(nk1[1] - gg[1] - along * dd[1], 2) +
                      powf(nk1[2] - gg[2] - along * dd[2], 2));
    CHECK(ph > 0.4f && ph < 0.7f && off < 0.5f && along > 30 && along < 90,
          "at p %.2f the blade passes through the neck (%.1f cm off its line, %.0f cm along it)", ph, off, along);
    CHECK(somvr_fn_arc_len(&arc) > 80 && somvr_fn_arc_len(&arc) < 200, "the mid-blade's path %.0f cm",
          somvr_fn_arc_len(&arc));
    float far1[3] = {0, -70, 190}, near1[3] = {10, -40, 25};
    CHECK(!somvr_fn_arc_place(&pq, far1, 0, 50, -45, &arc, &wy) && wy == SOMVR_FN_WHY_REACH, "1.9 m: closer");
    CHECK(!somvr_fn_arc_place(&pq, near1, 0, 50, -45, &arc, &wy) && wy == SOMVR_FN_WHY_NEAR, "at the chest: step back");
    /* the chop: a stunned orc hunched, its neck 80 cm under the eyes a metre ahead - from above, through it, down */
    float nk2[3] = {0, -80, 100};
    CHECK(somvr_fn_arc_place(&pq, nk2, 1, 65, -35, &arc, &wy), "the chop placed");
    somvr_fn_arc_at(&arc, 0, gg, dd, wa);
    CHECK(dd[1] > 0.3f && wa[1] < -0.5f && gg[1] < pq.up_max, "the chop: the blade raised (%.2f), the way down (%.2f)", dd[1],
          wa[1]);
    /* a swing along the arc drives the cut (the follow rule on the mid-blade's path); across the plane: nothing */
    somvr_fn_arc_place(&pq, nk1, 0, 50, -45, &arc, &wy);
    somvr_fn_cfg cc;
    somvr_fn_defaults(&cc);
    cc.follow = 1, cc.back_free = 1, cc.v_max = 1500, cc.start_cm = somvr_fn_arc_len(&arc), cc.depth_cm = 0;
    float pc2 = 0, tt = 0, rr, mm;
    while (pc2 < ph && tt < 2) {
        somvr_fn_arc_at(&arc, pc2, gg, dd, wa);
        float vm[3] = {wa[0] * 300, wa[1] * 300, wa[2] * 300};
        pc2 = somvr_fn_step(&cc, vm, wa, pc2, 1.0f / 90, &rr, &mm), tt += 1.0f / 90;
    }
    CHECK(pc2 >= ph && tt < 0.4f, "a 3 m/s swing along it crosses the neck in %.2f s", tt);
    float vax[3] = {arc.ax[0] * 300, arc.ax[1] * 300, arc.ax[2] * 300};
    somvr_fn_arc_at(&arc, 0.2f, gg, dd, wa);
    CHECK(somvr_fn_step(&cc, vax, wa, 0.2f, 0.1f, &rr, &mm) == 0.2f, "across the plane: nothing");
    /* the downed orc: its chest on the ground (145 cm under the eyes) 80 cm ahead - the reach down places it, the
     * standing stab's does not */
    somvr_fn_place_cfg pdn;
    somvr_fn_place_down(&pdn);
    float ch[3] = {0, -145, 80}, gd[3], dn[3], sd, ed;
    int wd = 0;
    CHECK(somvr_fn_place(&pdn, ch, gd, dn, &sd, &ed, &wd) && dn[1] < -0.6f, "the downed orc: placed, the way down (%.2f)",
          dn[1]);
    CHECK(!somvr_fn_place(&pq, ch, gd, dn, &sd, &ed, &wd), "the standing stab's reach: not");
    printf("test_finisher: %d checks, %d failed\n", g_n, g_fail);
    return g_fail != 0;
}
