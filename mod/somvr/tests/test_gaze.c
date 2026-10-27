/* test_gaze.c - the Shadow Strike by gaze, the pure part (somvr_gaze.h): the angle, the pick's hysteresis (a sweep, a
 * rest, jitter at the cone's edge, a neighbour, range, sight), the cancels (look away, Y let go), the press */
#include "../somvr_gaze.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

#define DT (1.0 / 90.0)
static const float HEAD[3] = {0, 170, 0}; /* the eyes 170 cm over the ground; Talion's root at 0,100,0 */

/* the head's forward at yaw (deg, + = right = +x) and pitch (deg, + = up); +z = ahead */
static void fwd_of(float yaw, float pitch, float f[3]) {
    float y = yaw * 0.01745329f, p = pitch * 0.01745329f;
    f[0] = sinf(y) * cosf(p), f[1] = sinf(p), f[2] = cosf(y) * cosf(p);
}
/* an orc d cm away at bearing yaw (deg), its root (the pelvis) 100 cm over the ground */
static somvr_gz_orc orc_at(uint64_t id, float d, float yaw) {
    somvr_gz_orc o = {id, {d * sinf(yaw * 0.01745329f), 100, d * cosf(yaw * 0.01745329f)}, 1};
    return o;
}
/* the pitch that looks at the orc's chest (root + 40) d cm away */
static float chest_pitch(float d) { return atanf((140.0f - 170.0f) / d) * 57.29578f; }

typedef struct {
    uint32_t ev;
    uint64_t marks, unmarks, switches;
    int why;
} tally;
static void run(somvr_gz *g, const somvr_gz_cfg *c, double *t, double secs, int on, float yaw, float pitch,
                const somvr_gz_orc *o, int n, tally *ty) {
    float f[3];
    fwd_of(yaw, pitch, f);
    for (double e = 0; e < secs - 1e-9; e += DT) {
        somvr_gz_out out;
        *t += DT;
        somvr_gz_pick(g, c, *t, on, HEAD, f, o, n, &out);
        if (ty) {
            ty->ev |= out.ev, ty->marks += !!(out.ev & SOMVR_GZE_MARK), ty->unmarks += !!(out.ev & SOMVR_GZE_UNMARK);
            ty->switches += !!(out.ev & SOMVR_GZE_SWITCH);
            if (out.ev & SOMVR_GZE_UNMARK) ty->why = out.why;
        }
    }
}

static void test_angle(void) {
    somvr_gz_cfg c;
    somvr_gz_cfg_default(&c);
    float f[3], d;
    somvr_gz_orc o = orc_at(1, 800, 0);
    fwd_of(0, chest_pitch(800), f);
    CHECK(somvr_gz_angle(&c, HEAD, f, o.root, &d) == 0 && NEAR(d, 800.6, 1), "the ray at its chest: 0 deg, %.1f cm", d);
    fwd_of(2.0f, chest_pitch(800), f); /* 28 cm right of the centre: within its 35 cm half-width */
    CHECK(somvr_gz_angle(&c, HEAD, f, o.root, NULL) == 0, "its shoulder counts as on it");
    fwd_of(8.0f, chest_pitch(800), f);
    float a = somvr_gz_angle(&c, HEAD, f, o.root, NULL);
    CHECK(NEAR(a, 8.0 - 2.5, 0.2), "8 deg right of the chest: %.2f (its half-width 2.5 deg off)", a);
    fwd_of(0, -4.0f, f); /* the ray at its legs (the body's lower part) */
    CHECK(somvr_gz_angle(&c, HEAD, f, o.root, NULL) == 0, "the ray at its legs: on its body");
    fwd_of(0, 12.0f, f); /* above its head: the head top (root + 80) is 0.7 deg over the eyes' level at 8 m */
    a = somvr_gz_angle(&c, HEAD, f, o.root, NULL);
    CHECK(a > 8.5f && a < 11.5f, "12 deg up: %.2f off its head", a);
    fwd_of(180, 0, f);
    CHECK(somvr_gz_angle(&c, HEAD, f, o.root, NULL) == 180, "behind the head: 180");
}

static void test_mark_dwell_and_sweep(void) {
    somvr_gz_cfg c;
    somvr_gz_cfg_default(&c);
    somvr_gz g;
    somvr_gz_reset(&g);
    somvr_gz_orc o[1] = {orc_at(7, 800, 0)};
    double t = 100;
    tally ty = {0};
    /* a sweep across it at 90 deg/s: inside the cone (+-8.5 deg with its width) for ~0.19 s ... with a 0.12 s dwell
     * it would mark: a fast sweep at 180 deg/s (0.09 s inside) marks nothing */
    for (float yaw = -40; yaw <= 40; yaw += 180.0f * (float)DT) run(&g, &c, &t, DT, 1, yaw, chest_pitch(800), o, 1, &ty);
    CHECK(ty.marks == 0 && !g.mark, "a 180 deg/s sweep across it marks nothing (marks %llu)", (unsigned long long)ty.marks);
    /* the head rests on it: marked after the dwell, not before */
    run(&g, &c, &t, 0.08, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(!g.mark, "not marked after 0.08 s");
    run(&g, &c, &t, 0.10, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 7 && ty.marks == 1, "marked after the dwell (mark %llu)", (unsigned long long)g.mark);
    CHECK(NEAR(g.aim[1], 140, 0.01) && NEAR(g.aim[2], 800, 0.01), "the aim point is its chest (%.0f %.0f %.0f)", g.aim[0],
          g.aim[1], g.aim[2]);
}

static void test_hysteresis(void) {
    somvr_gz_cfg c;
    somvr_gz_cfg_default(&c);
    somvr_gz g;
    somvr_gz_reset(&g);
    somvr_gz_orc o[1] = {orc_at(7, 800, 0)};
    double t = 100;
    tally ty = {0};
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 7, "marked");
    /* jitter around the cone's edge (6 deg + its half-width 2.5): +-2 deg noise for 3 s: no event at all */
    memset(&ty, 0, sizeof ty);
    unsigned s = 12345;
    for (int i = 0; i < 270; i++) {
        s = s * 1103515245u + 12345u;
        float j = ((float)(s >> 8 & 0xffff) / 65535.0f - 0.5f) * 4.0f;
        run(&g, &c, &t, DT, 1, 8.5f + j, chest_pitch(800) + 0.5f * j, o, 1, &ty);
    }
    CHECK(g.mark == 7 && ty.ev == 0, "3 s of jitter at the cone's edge: kept, no event (ev %#x)", ty.ev);
    /* between the cone and keep (12 deg + 2.5): kept */
    run(&g, &c, &t, 1.0, 1, 13.5f, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 7 && ty.ev == 0, "13.5 deg off (11 deg past its edge): kept");
    /* a glance away (40 deg) for 0.2 s and back: kept (lose 0.25 s) */
    run(&g, &c, &t, 0.2, 1, 40, chest_pitch(800), o, 1, &ty);
    run(&g, &c, &t, 0.1, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 7 && ty.ev == 0, "a 0.2 s glance away: kept");
    /* the cancel: looked away 0.3 s = unmarked, why: looked away */
    run(&g, &c, &t, 0.3, 1, 40, chest_pitch(800), o, 1, &ty);
    CHECK(!g.mark && ty.unmarks == 1 && ty.why == SOMVR_GZW_AWAY, "looked away 0.3 s: unmarked (why %s)",
          somvr_gz_why_name(ty.why));
    /* then A: nothing */
    CHECK(somvr_gz_press(&g, &c, t, 8) == SOMVR_GZE_NOMARK && g.phase == SOMVR_GZP_OFF, "A after looking away: nothing");
    /* the cancel by Y: marked again, then off = unmarked at once */
    memset(&ty, 0, sizeof ty);
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 7 && ty.marks == 1, "marked again");
    run(&g, &c, &t, DT, 0, 0, chest_pitch(800), o, 1, &ty);
    CHECK(!g.mark && ty.unmarks == 1 && ty.why == SOMVR_GZW_OFF, "Y let go: unmarked in the same frame (why %s)",
          somvr_gz_why_name(ty.why));
    CHECK(somvr_gz_press(&g, &c, t, 8) == SOMVR_GZE_NOMARK, "A after letting go of Y: nothing");
    /* Y held again while looking at it: the dwell again (no instant mark from before) */
    run(&g, &c, &t, 0.05, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(!g.mark, "Y again: not marked before the dwell");
}

static void test_neighbours(void) {
    somvr_gz_cfg c;
    somvr_gz_cfg_default(&c);
    somvr_gz g;
    somvr_gz_reset(&g);
    somvr_gz_orc o[2] = {orc_at(1, 800, -5), orc_at(2, 800, 5)}; /* 10 deg apart */
    double t = 100;
    tally ty = {0};
    run(&g, &c, &t, 0.3, 1, -5, chest_pitch(800), o, 2, &ty);
    CHECK(g.mark == 1, "the left one marked");
    /* the head at the middle with jitter: no ping-pong (both 5 deg off the centre: 2.5 past their edges) */
    unsigned s = 777;
    for (int i = 0; i < 270; i++) {
        s = s * 1103515245u + 12345u;
        float j = ((float)(s >> 8 & 0xffff) / 65535.0f - 0.5f) * 3.0f;
        run(&g, &c, &t, DT, 1, j, chest_pitch(800), o, 2, &ty);
    }
    CHECK(g.mark == 1 && ty.switches == 0, "3 s at the middle with jitter: the mark stays (switches %llu)",
          (unsigned long long)ty.switches);
    /* the head on the right one: the switch after the dwell, once */
    run(&g, &c, &t, 0.08, 1, 5, chest_pitch(800), o, 2, &ty);
    CHECK(g.mark == 1, "not switched before the dwell");
    run(&g, &c, &t, 0.2, 1, 5, chest_pitch(800), o, 2, &ty);
    CHECK(g.mark == 2 && ty.switches == 1, "switched to the right one once");
    /* two orcs in line (one behind the other): the nearer ray-wise wins; equal angles: the first stays */
    somvr_gz_reset(&g);
    somvr_gz_orc l[2] = {orc_at(3, 1200, 0), orc_at(4, 600, 0)};
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(900), l, 2, &ty);
    CHECK(g.mark == 3 || g.mark == 4, "one of two in line marked");
    uint64_t m = g.mark;
    run(&g, &c, &t, 1.0, 1, 0, chest_pitch(900), l, 2, &ty);
    CHECK(g.mark == m, "and kept");
}

static void test_range_sight_gone(void) {
    somvr_gz_cfg c;
    somvr_gz_cfg_default(&c);
    somvr_gz g;
    somvr_gz_reset(&g);
    double t = 100;
    tally ty = {0};
    somvr_gz_orc far[1] = {orc_at(5, 3000, 0)};
    run(&g, &c, &t, 0.5, 1, 0, chest_pitch(3000), far, 1, &ty);
    CHECK(!g.mark, "30 m (over the 25 m range): not marked");
    somvr_gz_orc near[1] = {orc_at(5, 100, 0)};
    run(&g, &c, &t, 0.5, 1, 0, chest_pitch(100), near, 1, &ty);
    CHECK(!g.mark, "1 m (under 1.5 m): not marked");
    somvr_gz_orc o[1] = {orc_at(5, 2400, 0)};
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(2400), o, 1, &ty);
    CHECK(g.mark == 5, "24 m: marked");
    o[0] = orc_at(5, 2650, 0);
    run(&g, &c, &t, 1.0, 1, 0, chest_pitch(2650), o, 1, &ty);
    CHECK(g.mark == 5, "it walks to 26.5 m: kept (the keep margin)");
    o[0] = orc_at(5, 2800, 0);
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(2800), o, 1, &ty);
    CHECK(!g.mark && ty.why == SOMVR_GZW_RANGE, "28 m for 0.3 s: unmarked (why %s)", somvr_gz_why_name(ty.why));
    /* sight */
    o[0] = orc_at(5, 800, 0);
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 5, "marked at 8 m");
    o[0].los = 0;
    run(&g, &c, &t, 0.15, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 5, "behind a post for 0.15 s: kept");
    o[0].los = -1;
    run(&g, &c, &t, 0.5, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 5, "sight unknown: kept");
    o[0].los = 0;
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(!g.mark && ty.why == SOMVR_GZW_SIGHT, "out of sight 0.3 s: unmarked (why %s)", somvr_gz_why_name(ty.why));
    run(&g, &c, &t, 0.5, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(!g.mark, "out of sight: never marked");
    o[0].los = 1;
    run(&g, &c, &t, 0.3, 1, 0, chest_pitch(800), o, 1, &ty);
    CHECK(g.mark == 5, "in sight again: marked");
    run(&g, &c, &t, DT, 1, 0, chest_pitch(800), o, 0, &ty);
    CHECK(!g.mark && ty.why == SOMVR_GZW_GONE, "gone from the list (dead): unmarked at once");
}

static void test_press(void) {
    somvr_gz_cfg c;
    somvr_gz_cfg_default(&c);
    somvr_gz g;
    somvr_gz_reset(&g);
    somvr_gz_orc o[1] = {orc_at(9, 900, 10)};
    double t = 100;
    tally ty = {0};
    somvr_gz_out out;
    run(&g, &c, &t, 0.3, 1, 10, chest_pitch(900), o, 1, &ty);
    CHECK(g.mark == 9, "marked");
    CHECK(somvr_gz_press(&g, &c, t, 1) == SOMVR_GZE_NOARROWS && g.phase == SOMVR_GZP_OFF, "1 arrow: refused at once");
    CHECK(somvr_gz_press(&g, &c, t, 0) == SOMVR_GZE_NOARROWS && g.mark == 9, "0 arrows: refused, the mark stays");
    CHECK(somvr_gz_press(&g, &c, t, 6) == SOMVR_GZE_PRESS && g.phase == SOMVR_GZP_AIM && g.target == 9, "6 arrows: committed");
    CHECK(somvr_gz_press(&g, &c, t, 6) == 0, "a press while one runs: nothing");
    /* the head turns away and Y is let go: the press runs on its target */
    run(&g, &c, &t, 0.1, 0, -90, 0, o, 1, &ty);
    CHECK(g.mark == 9 && g.phase == SOMVR_GZP_AIM, "looked away after the press: the target kept");
    somvr_gz_tick(&g, &c, t, 0, 0, &out);
    CHECK(out.aim_cmd == 1 && out.strike_cmd == 0 && out.ev == 0, "no prompt yet: the aim held, no strike");
    t += 0.05;
    somvr_gz_tick(&g, &c, t, 1, 0, &out);
    CHECK(out.ev == SOMVR_GZE_SEND && out.aim_cmd && out.strike_cmd && g.phase == SOMVR_GZP_SENT, "the prompt: the strike sent");
    t += 0.10;
    somvr_gz_tick(&g, &c, t, 1, 0, &out);
    CHECK(out.aim_cmd && out.strike_cmd, "0.1 s later: both held");
    t += 0.10;
    somvr_gz_tick(&g, &c, t, 0, 0, &out);
    CHECK(out.aim_cmd && !out.strike_cmd, "0.2 s: the strike command over, the aim held");
    t += 0.05;
    somvr_gz_tick(&g, &c, t, 0, 1, &out);
    CHECK(!out.aim_cmd && !out.strike_cmd && (out.ev & SOMVR_GZE_DONE) && g.phase == SOMVR_GZP_OFF && !g.mark,
          "struck: the aim let go, done, unmarked (ev %#x)", out.ev);
    CHECK(out.why == SOMVR_GZW_STRUCK, "why: struck");
    /* no strike seen: the aim held hold_s */
    somvr_gz_reset(&g);
    run(&g, &c, &t, 0.3, 1, 10, chest_pitch(900), o, 1, &ty);
    somvr_gz_press(&g, &c, t, -1);
    CHECK(g.phase == SOMVR_GZP_AIM, "arrows unknown: committed (the game decides)");
    somvr_gz_tick(&g, &c, t, 1, 0, &out);
    double t0 = t;
    int held = 0;
    for (; t < t0 + 1.0; t += 0.01) {
        somvr_gz_tick(&g, &c, t, 0, 0, &out);
        held += out.aim_cmd;
        if (out.ev & SOMVR_GZE_DONE) break;
    }
    CHECK(NEAR(t - t0, c.hold_s, 0.02) && held >= 40, "no move seen: the aim held %.2f s", t - t0);
    /* refused: no prompt within aim_max_s */
    somvr_gz_reset(&g);
    run(&g, &c, &t, 0.3, 1, 10, chest_pitch(900), o, 1, &ty);
    somvr_gz_press(&g, &c, t, 8);
    t0 = t;
    uint32_t ev = 0;
    int sent = 0;
    for (; t < t0 + 2.0; t += 0.01) {
        somvr_gz_tick(&g, &c, t, 0, 0, &out);
        ev |= out.ev, sent += out.strike_cmd;
        if (out.ev & SOMVR_GZE_DONE) break;
    }
    CHECK((ev & SOMVR_GZE_REFUSED) && !sent && NEAR(t - t0, c.aim_max_s, 0.02) && g.phase == SOMVR_GZP_OFF,
          "no prompt: refused after %.2f s, nothing sent", t - t0);
    CHECK(g.mark == 9, "refused: the mark stays (the orc still looked at)");
    CHECK(g.presses == 1 && g.refused == 1 && g.sends == 0, "the counters");
}

int main(void) {
    test_angle();
    test_mark_dwell_and_sweep();
    test_hysteresis();
    test_neighbours();
    test_range_sight_gone();
    test_press();
    printf("test_gaze: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
