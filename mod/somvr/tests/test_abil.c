/* test_abil.c - unit tests of somvr_abil.c, build 27's abilities by the hands (docs/re-xr-pipeline.md 114): the wraith
 * hand's push (the wraith flash: the open palm thrust out at an orc with the right trigger held; no charge, a fist, too
 * slow, a pull, a sweep across, at the body, beside the orc), the sword's stab into a downed orc (the ground finisher;
 * too slow, a level cut, the blade flat, handle first, beside him, from above out of reach), the trigger's hysteresis. */
#include "../somvr_abil.h"

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

/* the eyes at 0,170,0 facing +z; an orc's chest 180 cm ahead at 0,120,180; the palm 40 cm out, at chest height */
static void test_push(void)
{
    somvr_ab_cfg c;
    somvr_ab_cfg_default(&c);
    somvr_ab_push o;
    float eye[3] = { 0, 170, 0 }, palm[3] = { -15, 140, 40 }, orc[3] = { 0, 120, 180 }, nan = NAN;
    float out[3] = { 30, -40, 300 };    /* 3 m/s out and a little down toward him */
    CHECK(somvr_ab_push_judge(&c, out, eye, palm, orc, nan, 1, &o), "the push at him: %s", o.why);
    CHECK(somvr_ab_push_judge(&c, out, eye, palm, orc, 0.2f, 1, &o), "a loose hand pushes: %s", o.why);
    CHECK(!somvr_ab_push_judge(&c, out, eye, palm, orc, nan, 0, &o) && !strcmp(o.why, "no wraith charge (the right trigger)"),
          "no trigger: %s", o.why);
    CHECK(!somvr_ab_push_judge(&c, out, eye, palm, orc, 0.9f, 1, &o) && !strcmp(o.why, "the hand closed"), "a fist: %s",
          o.why);
    float slow[3] = { 10, -10, 150 };
    CHECK(!somvr_ab_push_judge(&c, slow, eye, palm, orc, nan, 1, &o) && !strcmp(o.why, "too slow"), "1.5 m/s: %s", o.why);
    float pull[3] = { 0, 20, -300 };    /* toward the eyes */
    CHECK(!somvr_ab_push_judge(&c, pull, eye, palm, orc, nan, 1, &o) && !strcmp(o.why, "not out from the body"),
          "a pull: %s (%.0f deg)", o.why, o.out_deg);
    float sweep[3] = { 300, 0, 20 };    /* across the body */
    CHECK(!somvr_ab_push_judge(&c, sweep, eye, palm, orc, nan, 1, &o) && !strcmp(o.why, "not out from the body"),
          "a sweep: %s (%.0f deg)", o.why, o.out_deg);
    float near[3] = { -5, 160, 12 };    /* the palm at the chin */
    CHECK(!somvr_ab_push_judge(&c, out, eye, near, orc, nan, 1, &o) && !strcmp(o.why, "the hand at the body"),
          "at the body: %s (%.0f cm)", o.why, o.out_cm);
    float beside[3] = { 250, 120, 60 }; /* the orc off to the right */
    CHECK(!somvr_ab_push_judge(&c, out, eye, palm, beside, nan, 1, &o) && !strcmp(o.why, "not at the orc"),
          "the orc to the right: %s (%.0f deg)", o.why, o.aim_deg);
    float side_push[3] = { 250, 0, 120 }; /* pushed out to the right at it */
    float side_palm[3] = { 30, 140, 25 };
    CHECK(somvr_ab_push_judge(&c, side_push, eye, side_palm, beside, nan, 1, &o), "pushed at the orc on the right: %s",
          o.why);
}

/* the downed orc's body at 0,15,110 (lying, its root on the ground at y 0); the tip 30 cm over it */
static void test_stab(void)
{
    somvr_ab_cfg c;
    somvr_ab_cfg_default(&c);
    somvr_ab_stab o;
    float body[3] = { 0, 15, 110 }, tip[3] = { 0, 45, 100 }, axis[3] = { 0, -90, 20 };
    float v[3] = { 0, -260, 40 };       /* down at 2.6 m/s, point first */
    CHECK(somvr_ab_stab_judge(&c, v, axis, tip, body, &o), "the stab: %s", o.why);
    float slow[3] = { 0, -100, 20 };
    CHECK(!somvr_ab_stab_judge(&c, slow, axis, tip, body, &o) && !strcmp(o.why, "too slow"), "lowered: %s", o.why);
    float cut[3] = { 250, -40, 0 };     /* a level cut across */
    float cut_axis[3] = { 90, -20, 10 };
    CHECK(!somvr_ab_stab_judge(&c, cut, cut_axis, tip, body, &o) && !strcmp(o.why, "not down"), "a cut: %s", o.why);
    float flat[3] = { 0, -15, 90 };     /* the blade level, the hand chopping down */
    CHECK(!somvr_ab_stab_judge(&c, v, flat, tip, body, &o) && !strcmp(o.why, "the blade not pointing down"),
          "a chop with the blade level: %s (%.0f deg)", o.why, o.vert_deg);
    float slant[3] = { 0, -60, -60 };   /* pointing down and back, the hand going down and forward: the flat leads */
    float fwd[3] = { 0, -200, 200 };
    CHECK(!somvr_ab_stab_judge(&c, fwd, slant, tip, body, &o) && !strcmp(o.why, "not point first"),
          "the flat leading: %s (%.0f deg)", o.why, o.point_deg);
    float away[3] = { 90, 40, 100 };    /* 90 cm to the side */
    CHECK(!somvr_ab_stab_judge(&c, v, axis, away, body, &o) && !strcmp(o.why, "the tip not at him"), "beside him: %s",
          o.why);
    float chest[3] = { 0, 30, 47 };     /* 64 cm along him from the pelvis point: his chest (run 124054's refused 63 cm) */
    CHECK(somvr_ab_stab_judge(&c, v, axis, chest, body, &o), "into his chest, 64 cm from the pelvis: %s (%.0f cm)", o.why,
          o.reach_cm);
    float high[3] = { 0, 110, 105 };    /* the tip at the waist of a standing player: 95 cm over him */
    CHECK(!somvr_ab_stab_judge(&c, v, axis, high, body, &o) && !strcmp(o.why, "the tip not at him"),
          "out of reach above: %s (%.0f cm)", o.why, o.reach_cm);
}

static void test_trigger(void)
{
    somvr_ab_cfg c;
    somvr_ab_cfg_default(&c);
    CHECK(!somvr_ab_trigger(&c, 0, 0.5f) && somvr_ab_trigger(&c, 0, 0.7f), "the press at 0.6");
    CHECK(somvr_ab_trigger(&c, 1, 0.5f) && !somvr_ab_trigger(&c, 1, 0.3f), "held down to 0.4");
    CHECK(!somvr_ab_trigger(&c, 1, NAN), "let go (NaN)");
}


/* build 27c (docs 118): the dagger's quick throw - an overhand throw ahead (6 m/s, a little up) passes; too slow, across,
 * up, down, from the hip do not; the orc on the throw's line (and one 30 deg aside, one too near, one out of range); the
 * hand at the mouth (the whistle) and at rest */
static void test_throw(void)
{
    somvr_ab2_cfg c;
    somvr_ab2_cfg_default(&c);
    somvr_ab_throw o;
    float eye[3] = { 0, 170, 0 }, fwd[3] = { 0, 0, 1 }, hand[3] = { -20, 160, 30 };
    float v[3] = { 0, 50, 600 };
    CHECK(somvr_ab_throw_judge(&c, v, eye, fwd, hand, &o), "an overhand throw ahead: %s", o.why);
    float slow[3] = { 0, 20, 200 };
    CHECK(!somvr_ab_throw_judge(&c, slow, eye, fwd, hand, &o) && !strcmp(o.why, "too slow"), "2 m/s: %s", o.why);
    float across[3] = { 600, 0, 100 };
    CHECK(!somvr_ab_throw_judge(&c, across, eye, fwd, hand, &o) && !strcmp(o.why, "not thrown ahead"), "across: %s", o.why);
    float up[3] = { 0, 600, 200 };
    CHECK(!somvr_ab_throw_judge(&c, up, eye, fwd, hand, &o) && !strcmp(o.why, "thrown up"), "up: %s", o.why);
    float down[3] = { 0, -500, 200 };
    CHECK(!somvr_ab_throw_judge(&c, down, eye, fwd, hand, &o) && !strcmp(o.why, "thrown down"), "down: %s", o.why);
    float hip[3] = { -20, 100, 30 };
    CHECK(!somvr_ab_throw_judge(&c, v, eye, fwd, hip, &o) && !strcmp(o.why, "not overhand (the hand low)"), "from the hip: %s",
          o.why);
    float toss_h[3] = { -14, 142, 45 }, toss_v[3] = { 0, 230, 420 }; /* run 145019's underhand toss: 28 cm under, +29 deg */
    CHECK(!somvr_ab_throw_judge(&c, toss_v, eye, fwd, toss_h, &o), "the underhand toss of run 145019: %s", o.why);
    float near2[3] = { -10, 140, 150 }; /* a fighting orc 1.2 m off: in range */
    float v_near[3] = { 0, -60, 600 };
    CHECK(somvr_ab_throw_aim(&c, hand, v_near, near2, NULL, NULL, NULL), "an orc in the fight, 1.2 m");
        float fwd_l[3] = { -0.7071f, 0, 0.7071f }, v_l[3] = { -420, 40, 420 }; /* looking 45 deg left, thrown that way */
    CHECK(somvr_ab_throw_judge(&c, v_l, eye, fwd_l, hand, &o), "thrown where the head looks: %s", o.why);
    float lv, pv, dv, orc[3] = { 0, 140, 1000 };
    CHECK(somvr_ab_throw_aim(&c, hand, v, orc, &lv, &pv, &dv), "the orc on the line: %.1f %.1f %.0f", lv, pv, dv);
    float aside[3] = { 560, 140, 970 };
    CHECK(!somvr_ab_throw_aim(&c, hand, v, aside, &lv, &pv, &dv) && lv > 25, "30 deg right: %.1f", lv);
    float near[3] = { -15, 150, 90 };
    CHECK(!somvr_ab_throw_aim(&c, hand, v, near, &lv, &pv, &dv), "0.6 m away: %.0f", dv);
    float far[3] = { 0, 140, 3200 };
    CHECK(!somvr_ab_throw_aim(&c, hand, v, far, &lv, &pv, &dv), "32 m away: %.0f", dv);
    float mouth[3] = { 3, 158, 12 }, rest[3] = { -22, 135, 25 };
    CHECK(somvr_ab_mouth_dist(eye, fwd, mouth) <= c.mouth_cm, "at the mouth: %.1f", somvr_ab_mouth_dist(eye, fwd, mouth));
    CHECK(somvr_ab_mouth_dist(eye, fwd, rest) > c.mouth_cm + c.mouth_out_cm, "at rest: %.1f",
          somvr_ab_mouth_dist(eye, fwd, rest));
}

/* build 27d (docs/re-xr-pipeline.md 121): taunt's beckon - the open hand out, palm up, pulled in twice within 1.5 s;
 * one pull, a slow drift in, pulls with the hand near the body, pulls too far apart, a closed hand, the hover then pull */
static int beckon_run(const somvr_ab3_cfg *c, const float *d, const int *ok, int n, double dt, int *at)
{
    somvr_ab_beckon b;
    memset(&b, 0, sizeof b);
    int hits = 0;
    for (int i = 0; i < n; i++) {
        const char *why;
        if (somvr_ab_beckon_step(c, &b, i * dt, d[i], ok ? ok[i] : 1, &why))
            hits++, *at = i;
    }
    return hits;
}
static void test_beckon(void)
{
    somvr_ab3_cfg c;
    somvr_ab3_cfg_default(&c);
    float d[200];
    int ok[200], n, at = -1;
    /* 90 Hz: out at 45, in to 33 in 0.15 s, out to 44 in 0.2 s, in to 32 in 0.15 s, held */
    n = 0;
    for (int i = 0; i < 20; i++) d[n++] = 45;
    for (int i = 1; i <= 14; i++) d[n++] = 45 - 12 * i / 14.0f;
    for (int i = 1; i <= 18; i++) d[n++] = 33 + 11 * i / 18.0f;
    for (int i = 1; i <= 14; i++) d[n++] = 44 - 12 * i / 14.0f;
    for (int i = 0; i < 40; i++) d[n++] = 32;
    CHECK(beckon_run(&c, d, NULL, n, 1 / 90.0, &at) == 1 && at >= 58 && at <= 64, "the beckon: once, 7 cm into the 2nd pull (%d)", at);
    /* a third pull right after: not another (the hand must go out and pull twice again) */
    int n3 = n;
    for (int i = 1; i <= 18; i++) d[n3++] = 32 + 12 * i / 18.0f;
    for (int i = 1; i <= 14; i++) d[n3++] = 44 - 12 * i / 14.0f;
    CHECK(beckon_run(&c, d, NULL, n3, 1 / 90.0, &at) == 1, "a 3rd pull: no second beckon");
    /* one pull only */
    float d1[120];
    int n1 = 0;
    for (int i = 0; i < 52; i++) d1[n1++] = d[i];
    for (int i = 0; i < 60; i++) d1[n1++] = 44;
    CHECK(beckon_run(&c, d1, NULL, n1, 1 / 90.0, &at) == 0, "one pull: nothing");
    /* the same in a closed hand (not ok) */
    for (int i = 0; i < n; i++) ok[i] = 1;
    for (int i = 30; i < 50; i++) ok[i] = 0;
    CHECK(beckon_run(&c, d, ok, n, 1 / 90.0, &at) == 0, "the hand closed between the pulls: nothing");
    /* slow: the same way at a quarter of the speed (pulls of 12 cm in 0.6 s = 20 cm/s) */
    CHECK(beckon_run(&c, d, NULL, n, 4 / 90.0, &at) == 0, "a slow drift in and out: nothing");
    /* near the body: the same pulls 20 cm closer (out at 25) */
    float dn[200];
    for (int i = 0; i < n; i++) dn[i] = d[i] - 20;
    CHECK(beckon_run(&c, dn, NULL, n, 1 / 90.0, &at) == 0, "the hand near the body: nothing");
    /* the two pulls 2 s apart */
    int m = 0;
    float dg[400];
    for (int i = 0; i < 20; i++) dg[m++] = 45;
    for (int i = 1; i <= 14; i++) dg[m++] = 45 - 12 * i / 14.0f;
    for (int i = 1; i <= 18; i++) dg[m++] = 33 + 11 * i / 18.0f;
    for (int i = 0; i < 160; i++) dg[m++] = 44;
    for (int i = 1; i <= 14; i++) dg[m++] = 44 - 12 * i / 14.0f;
    for (int i = 0; i < 20; i++) dg[m++] = 32;
    CHECK(beckon_run(&c, dg, NULL, m, 1 / 90.0, &at) == 0, "two pulls 2.2 s apart: nothing");
    /* held out 1 s at the top before the beckon (the pull timed from its last moment there) */
    m = 0;
    for (int i = 0; i < 90; i++) dg[m++] = 45 + 0.4f * (i % 3);
    for (int i = 1; i <= 14; i++) dg[m++] = 45 - 12 * i / 14.0f;
    for (int i = 1; i <= 18; i++) dg[m++] = 33 + 11 * i / 18.0f;
    for (int i = 1; i <= 14; i++) dg[m++] = 44 - 12 * i / 14.0f;
    for (int i = 0; i < 20; i++) dg[m++] = 32;
    CHECK(beckon_run(&c, dg, NULL, m, 1 / 90.0, &at) == 1, "held out a second, then the beckon");
    /* a headless frame rate (20 fps) */
    float d20[40];
    int k = 0;
    float seq[] = { 45, 45, 45, 41, 37, 33, 37, 41, 44, 40, 36, 32, 32, 32 };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) d20[k++] = seq[i];
    CHECK(beckon_run(&c, d20, NULL, k, 0.05, &at) == 1, "the beckon at 20 fps");
}

int main(void)
{
    test_push();
    test_stab();
    test_trigger();
    test_throw();
    test_beckon();
    printf("test_abil: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
