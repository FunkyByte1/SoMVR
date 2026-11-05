/* test_struggle.c - build 41's one rule for every struggle, the pure part (somvr_struggle.h; docs/re-xr-pipeline.md 180) */
#include "../somvr_struggle.h"

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

/* the class's own target angles (CStruggleInputDirection's start, L 0xe0d490: the record's direction 0..3) */
#define UP 1.5707963705062866f    /* 0x3fc90fdb */
#define DOWN 4.71238899230957f    /* 0x4096cbe4 */
#define LEFT 3.1415927410125732f  /* 0x40490fdb */
#define RIGHT 0.0f

/* a hand moving from (x0, y0) at (vx, vy) cm/s for dur s, sampled at 90 Hz from t0; returns swipes seen, the last way */
static int swipe_run(somvr_sg_swipe *s, const somvr_sg_swipe_cfg *c, float t0, float x0, float y0, float vx, float vy,
                     float dur, float dir[2], float *t_end)
{
    int n = 0;
    float t = t0, d[2], mps;
    for (int i = 0; t < t0 + dur; i++, t = t0 + (float)i / 90.0f)
        if (somvr_sg_swipe_step(s, c, t, x0 + vx * (t - t0), y0 + vy * (t - t0), d, &mps))
            n++, dir[0] = d[0], dir[1] = d[1];
    *t_end = t;
    return n;
}

int main(void)
{
    /* ---- the act by class: one rule ---- */
    CHECK(somvr_sg_act(SOMVR_SG_PRESS, 0, 0) == SOMVR_SGA_THRUSTS, "press count: thrusts");
    CHECK(somvr_sg_act(SOMVR_SG_QT, 0, 1) == SOMVR_SGA_THRUST, "quick time: one thrust");
    CHECK(somvr_sg_act(SOMVR_SG_HOLD, 0, 1) == SOMVR_SGA_HOLD, "hold: the hand held");
    CHECK(somvr_sg_act(SOMVR_SG_DIR, 0, 0) == SOMVR_SGA_SWIPE, "direction: a swipe or a lean");
    CHECK(somvr_sg_act(SOMVR_SG_AIM, 0, 1) == SOMVR_SGA_STAB, "aim with its node: the stab on the outline");
    CHECK(somvr_sg_act(SOMVR_SG_AIM, 0, 0) == SOMVR_SGA_STEER, "aim, no node: build 27's steer");
    CHECK(somvr_sg_act(SOMVR_SG_AIM, 1, 1) == SOMVR_SGA_GRIP, "a beast's domination: the grip (ride's)");
    CHECK(somvr_sg_act(SOMVR_SG_OTHER, 0, 1) == SOMVR_SGA_NONE, "a class not known: none");
    CHECK(somvr_sg_act(SOMVR_SG_OTHER, 1, 1) == SOMVR_SGA_NONE, "a class not known, a beast: none");

    /* ---- the direction: the game's own test ---- */
    int ok;
    float a = somvr_sg_dir_angle(0, 1, &ok);
    CHECK(ok && fabsf(a - UP) < 1e-6f, "up = pi/2 (%f)", a);
    a = somvr_sg_dir_angle(0, -1, &ok);
    CHECK(ok && fabsf(a + UP) < 1e-6f, "down = -pi/2 (%f)", a);
    somvr_sg_dir_angle(0.3f, 0.3f, &ok);
    CHECK(!ok, "under 0.5: no direction");
    float tol = 30.0f * 0.0174532924f;
    CHECK(somvr_sg_dir_game(UP, tol, somvr_sg_dir_angle(0, 1, &ok)), "up taken");
    CHECK(!somvr_sg_dir_game(UP, tol, somvr_sg_dir_angle(1, 0, &ok)), "right for up: no");
    CHECK(somvr_sg_dir_game(LEFT, tol, somvr_sg_dir_angle(-1, 0, &ok)), "left taken");
    /* its fold is one-sided: left takes only the upper half of its cone, down only the left half */
    CHECK(somvr_sg_dir_game(LEFT, tol, somvr_sg_dir_angle(-0.95f, 0.2f, &ok)), "left a little high: taken");
    CHECK(!somvr_sg_dir_game(LEFT, tol, somvr_sg_dir_angle(-0.95f, -0.2f, &ok)), "left a little low: no (the fold)");
    CHECK(somvr_sg_dir_game(RIGHT, tol, somvr_sg_dir_angle(1, 0.1f, &ok)), "right taken");
    /* the game's fold misses its own exact down: |3pi/2 - (-pi/2)| = 2 pi is not over 2 pi */
    CHECK(!somvr_sg_dir_game(DOWN, tol, somvr_sg_dir_angle(0, -1, &ok)), "the exact down: the game says no (its fold)");
    CHECK(!somvr_sg_dir_game(DOWN, tol, somvr_sg_dir_angle(0.1f, -1, &ok)), "down a little right: no (the fold)");
    CHECK(somvr_sg_dir_game(DOWN, tol, somvr_sg_dir_angle(-0.1f, -1, &ok)), "down a little left: taken");
    /* what we send: always a vector the game takes, within 3 degrees of the shown way */
    const float tg[4] = { RIGHT, UP, LEFT, DOWN };
    const char *tw[4] = { "right", "up", "left", "down" };
    for (int i = 0; i < 4; i++) {
        float v[2], off;
        int s = somvr_sg_dir_send(tg[i], tol, v);
        float g = somvr_sg_dir_angle(v[0], v[1], &ok);
        CHECK(s && ok && somvr_sg_dir_game(tg[i], tol, g), "%s: the sent vector (%f %f) is taken", tw[i], v[0], v[1]);
        CHECK(somvr_sg_dir_match(v, tg[i], 3.0f, &off), "%s: sent within 3 deg (%f)", tw[i], off);
        const char *w = somvr_sg_dir_word(tg[i]);
        CHECK(w[0] == tw[i][0], "%s: its word %s", tw[i], w);
        float small = 2.0f * 0.0174532924f; /* a tight record: 2 deg - still a vector inside it */
        s = somvr_sg_dir_send(tg[i], small, v);
        g = somvr_sg_dir_angle(v[0], v[1], &ok);
        CHECK(s && somvr_sg_dir_game(tg[i], small, g), "%s: a 2-deg tolerance still taken", tw[i]);
    }
    float v0[2];
    CHECK(!somvr_sg_dir_send(UP, 0, v0), "tolerance 0 (the record has none): nothing sent");
    /* the gesture's match: a cone round the shown way */
    float off, g1[2] = { 0.9f, 0.2f }, g2[2] = { 0.1f, 1.0f }, g3[2] = { 0.05f, -1.0f };
    CHECK(somvr_sg_dir_match(g1, RIGHT, 45, &off) && off < 15, "a swipe right a little up: right (%f)", off);
    CHECK(!somvr_sg_dir_match(g1, UP, 45, &off), "...: not up (%f)", off);
    CHECK(somvr_sg_dir_match(g2, UP, 45, &off), "up");
    CHECK(somvr_sg_dir_match(g3, DOWN, 45, &off) && off < 5, "down against 3 pi/2 (%f)", off);
    CHECK(!somvr_sg_dir_match(g3, LEFT, 45, &off), "down is not left");

    /* ---- the swipe ---- */
    somvr_sg_swipe_cfg sc;
    somvr_sg_swipe_defaults(&sc);
    somvr_sg_swipe s;
    float d[2] = { 0, 0 }, te;
    somvr_sg_swipe_init(&s);
    int n = swipe_run(&s, &sc, 0, 0, 0, 0, 0, 0.3f, d, &te); /* at rest */
    CHECK(n == 0, "at rest: none (%d)", n);
    n = swipe_run(&s, &sc, te, 0, 0, 200, 0, 0.3f, d, &te); /* 2 m/s right for 0.3 s (60 cm) */
    CHECK(n == 1 && d[0] > 0.99f, "a 2 m/s swipe right: one swipe right (%d, %f %f)", n, d[0], d[1]);
    n = swipe_run(&s, &sc, te, 60, 0, 200, 0, 0.3f, d, &te); /* still fast: not again */
    CHECK(n == 0, "the same motion going on: not twice (%d)", n);
    n = swipe_run(&s, &sc, te, 120, 0, 0, 0, 0.3f, d, &te); /* rests: re-armed */
    n = swipe_run(&s, &sc, te, 120, 0, 0, -220, 0.3f, d, &te); /* down */
    CHECK(n == 1 && d[1] < -0.99f, "then down: a swipe down (%d, %f %f)", n, d[0], d[1]);
    somvr_sg_swipe_init(&s);
    n = swipe_run(&s, &sc, 10, 0, 0, 0, 0, 0.2f, d, &te);
    n = swipe_run(&s, &sc, te, 0, 0, 60, 0, 0.6f, d, &te); /* 0.6 m/s: a drift */
    CHECK(n == 0, "a slow drift: none (%d)", n);
    n = swipe_run(&s, &sc, te, 36, 0, 0, 0, 0.2f, d, &te);
    n = swipe_run(&s, &sc, te, 36, 0, 300, 0, 0.04f, d, &te); /* 3 m/s for 40 ms: 12 cm, under min_cm */
    n += swipe_run(&s, &sc, te, 48, 0, 0, 0, 0.3f, d, &te);
    CHECK(n == 0, "a 12 cm twitch: none (%d)", n);
    somvr_sg_swipe_init(&s);
    n = swipe_run(&s, &sc, 20, 0, 0, 250, 0, 0.3f, d, &te);
    CHECK(n == 0, "fast from the first sample (never slow): not armed (%d)", n);

    /* ---- the lean ---- */
    CHECK(somvr_sg_lean(16, 0, 15, d) && d[0] > 0.99f, "16 cm right: a lean right");
    CHECK(!somvr_sg_lean(10, 5, 15, d), "11 cm: none");
    CHECK(somvr_sg_lean(-2, -18, 15, d) && d[1] < -0.99f, "a duck: down");

    /* ---- the hold ---- */
    somvr_sg_hold h;
    int c0[2] = { 0, 1 }; /* the right hand closed on the sword as it began */
    somvr_sg_hold_init(&h, c0);
    int cl[2] = { 0, 1 };
    CHECK(!somvr_sg_hold_step(&h, cl, 1, 0.05f), "the sword hand closed from the start: no hold");
    cl[0] = 1;
    CHECK(somvr_sg_hold_step(&h, cl, 0, 0.05f) && h.hand == 0, "the free hand closes: held");
    CHECK(h.held == 0, "before the window: no time (%f)", h.held);
    for (int i = 0; i < 10; i++)
        somvr_sg_hold_step(&h, cl, 1, 0.05f);
    CHECK(fabsf(h.held - 0.5f) < 1e-3f, "0.5 s in the window (%f)", h.held);
    cl[0] = 0;
    CHECK(!somvr_sg_hold_step(&h, cl, 1, 0.05f) && h.held == 0, "let go: off, the time again from 0");
    CHECK(!somvr_sg_hold_step(&h, cl, 1, 0.05f), "the right still closed: still off");
    cl[1] = 0;
    somvr_sg_hold_step(&h, cl, 1, 0.05f);
    cl[1] = 1;
    CHECK(somvr_sg_hold_step(&h, cl, 1, 0.05f) && h.hand == 1, "the right opened and closed again: held (fresh)");

    /* ---- the ring's steps ---- */
    CHECK(somvr_sg_steps_hold(0, 1.5f, 12) == 12, "hold 0: full ring");
    CHECK(somvr_sg_steps_hold(0.75f, 1.5f, 12) == 6, "hold half: 6 (%u)", somvr_sg_steps_hold(0.75f, 1.5f, 12));
    CHECK(somvr_sg_steps_hold(1.49f, 1.5f, 12) == 1, "nearly: 1");
    CHECK(somvr_sg_steps_hold(1.5f, 1.5f, 12) == 13, "done: the bright ring");
    CHECK(somvr_sg_steps_count(0, 8, 12) == 12 && somvr_sg_steps_count(4, 8, 12) == 6 && somvr_sg_steps_count(7, 8, 12) == 2 &&
              somvr_sg_steps_count(8, 8, 12) == 13,
          "count 0/4/7/8 of 8: 12/6/2/13 (%u %u %u %u)", somvr_sg_steps_count(0, 8, 12), somvr_sg_steps_count(4, 8, 12),
          somvr_sg_steps_count(7, 8, 12), somvr_sg_steps_count(8, 8, 12));
    CHECK(somvr_sg_steps_count(0, 0, 12) == 12, "need 0: full");

    /* ---- the aim's stab on the outline ---- */
    somvr_sg_stab_cfg kc;
    somvr_sg_stab_defaults(&kc);
    somvr_sg_stab k;
    somvr_sg_stab_init(&k);
    float ot[3] = { 0, 150, 60 }, od[3] = { 0, 0, 1 }, ax[3] = { 0, 0.1f, 1 }, still[3] = { 0, 0, 0 }, fwd[3] = { 0, 0, 180 };
    float far[3] = { 40, 150, 60 }, on[3] = { 3, 148, 58 }, dc, ag, al;
    CHECK(somvr_sg_stab_step(&k, &kc, 0, far, ax, fwd, ot, od, &dc, &ag, &al) == SOMVR_SGS_NONE, "a thrust 40 cm off: nothing");
    CHECK(somvr_sg_stab_step(&k, &kc, 0.1f, on, ax, still, ot, od, &dc, &ag, &al) == SOMVR_SGS_LAID, "laid on it: laid (%f %f)",
          dc, ag);
    CHECK(somvr_sg_stab_step(&k, &kc, 0.2f, on, ax, still, ot, od, &dc, &ag, &al) == SOMVR_SGS_STILL, "held there: still");
    float in[3] = { 3, 148, 75 };
    CHECK(somvr_sg_stab_step(&k, &kc, 0.3f, in, ax, fwd, ot, od, &dc, &ag, &al) == SOMVR_SGS_STAB && al > 1.7f,
          "then a 1.8 m/s thrust along it: the stab (%f)", al);
    somvr_sg_stab_init(&k);
    somvr_sg_stab_step(&k, &kc, 1.0f, on, ax, still, ot, od, &dc, &ag, &al);
    float back[3] = { 0, 0, -180 };
    CHECK(somvr_sg_stab_step(&k, &kc, 1.1f, on, ax, back, ot, od, &dc, &ag, &al) != SOMVR_SGS_STAB, "a pull back: no stab");
    float side[3] = { 0.8f, 0, 0.6f };
    somvr_sg_stab_init(&k);
    CHECK(somvr_sg_stab_step(&k, &kc, 2.0f, on, side, fwd, ot, od, &dc, &ag, &al) == SOMVR_SGS_NONE,
          "the blade across (37 deg off): not laid (%f)", ag);
    somvr_sg_stab_init(&k);
    somvr_sg_stab_step(&k, &kc, 3.0f, on, ax, still, ot, od, &dc, &ag, &al);
    float away[3] = { 30, 150, 60 };
    somvr_sg_stab_step(&k, &kc, 3.2f, away, ax, still, ot, od, &dc, &ag, &al);
    CHECK(somvr_sg_stab_step(&k, &kc, 4.0f, away, ax, fwd, ot, od, &dc, &ag, &al) == SOMVR_SGS_NONE,
          "laid, moved off 30 cm, a thrust there 1 s later: nothing (the lay ran out)");
    CHECK(somvr_sg_stab_step(&k, &kc, 4.1f, on, ax, fwd, ot, od, &dc, &ag, &al) == SOMVR_SGS_STAB,
          "a thrust that passes the outline's point lined up: the stab");

    printf("test_struggle: %d checks, %d failed\n", g_n, g_fail);
    return g_fail != 0;
}
