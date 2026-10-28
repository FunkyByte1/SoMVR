/* test_stealth.c - the VR stealth kill's pure part (somvr_stealth.h): the angles, who may be killed, the contact's
 * judge, the stance switch, the cue's ticks */
#include "../somvr_stealth.h"

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

static somvr_stl_who who_behind(void) { return (somvr_stl_who){ 0, 1, 20.0f, 175.0f, 90.0f, 0 }; }
static somvr_stl_hit hit_back(void) { return (somvr_stl_hit){ 170.0f, 40.0f, 1, 0.5f, 0.3f }; }

static void test_angles(void) {
    float o[3] = { 0, 0, 0 }, front[3] = { 0, 0, 100 }, back[3] = { 0, 0, -100 }, right[3] = { 100, 0, 0 };
    CHECK(fabsf(somvr_stl_off_deg(front, o, 0)) < 0.1f, "in front: 0");
    CHECK(fabsf(fabsf(somvr_stl_off_deg(back, o, 0)) - 180.0f) < 0.1f, "behind: 180");
    CHECK(fabsf(somvr_stl_off_deg(right, o, 0) - 90.0f) < 0.1f, "its right: +90 (%f)", somvr_stl_off_deg(right, o, 0));
    CHECK(fabsf(somvr_stl_off_deg(back, o, 3.14159265f)) < 0.1f, "turned round: the back point is in front");
    CHECK(somvr_stl_off_deg(o, o, 1.0f) == 0.0f, "the same point: 0");
}

static void test_possible(void) {
    somvr_stl_cfg c;
    somvr_stl_defaults(&c);
    somvr_stl_who w = who_behind();
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "crouched behind an unaware orc");
    w.aware = 1;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_AWARE, "aware: a normal fight");
    w.aware = -1;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_UNKNOWN, "not readable: never a kill");
    w = who_behind(), w.dead = 1;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_DEAD, "dead");
    w = who_behind(), w.player_off_deg = 100.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_FRONT, "at its side (100 deg)");
    w.player_off_deg = -115.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "behind on its left (-115)");
    w = who_behind(), w.crouched = 0, w.speed_cms = 60.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "standing but slow (either)");
    w.speed_cms = 200.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "standing and running");
    w.speed_cms = NAN;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "standing, no speed");
    c.stance = SOMVR_STL_STANCE_CROUCH, w.speed_cms = 10.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "crouch only: standing still is not enough");
    w.crouched = 1, w.speed_cms = 300.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "crouch only: a fast crouched rush counts");
    c.stance = SOMVR_STL_STANCE_SLOW;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "slow only: the crouched rush does not");
    w.speed_cms = 30.0f, w.crouched = 0;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "slow only: slow");
}

static void test_judge(void) {
    somvr_stl_cfg c;
    somvr_stl_defaults(&c);
    somvr_stl_who w = who_behind();
    somvr_stl_hit h = hit_back();
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_KILL, "a light hit into the back");
    h.tier = 0;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_WEAK, "a slow touch");
    h.axis_mps = 1.0f, h.cos_axis = 0.9f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_KILL, "a slow stab along the blade");
    h.cos_axis = -0.9f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_KILL, "the stab's sign does not matter (the reverse grip)");
    h.cos_axis = 0.5f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_WEAK, "a slow push across the blade");
    h = hit_back(), h.contact_off_deg = 40.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_SIDE, "reached round into its chest");
    h = hit_back(), h.contact_up_cm = -40.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_LOW, "its legs");
    h.contact_up_cm = 120.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_LOW, "over its head");
    h = hit_back(), h.tier = 2, w.aware = 1;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_AWARE, "a heavy hit into an aware orc's back: the ordinary hit");
    w = who_behind(), w.player_off_deg = 10.0f, h.contact_off_deg = 170.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_FRONT, "from the front, reaching round: no");
    CHECK(somvr_stl_stance_of("crouch") == SOMVR_STL_STANCE_CROUCH && somvr_stl_stance_of("either") == 2 &&
          somvr_stl_stance_of("x") == -1 && somvr_stl_stance_of(NULL) == -1, "stance words");
    CHECK(!strcmp(somvr_stl_stance_name(1), "slow") && !strcmp(somvr_stl_why(SOMVR_STL_KILL), "the stealth kill") &&
          !strcmp(somvr_stl_why(99), "?"), "names");
}

static void test_cue(void) {
    somvr_stl_cue q;
    memset(&q, 0, sizeof q);
    CHECK(!somvr_stl_cue_step(&q, 0, 0, 0.0, 1.5), "nothing");
    CHECK(somvr_stl_cue_step(&q, 0x100, 1, 1.0, 1.5), "possible: one tick");
    CHECK(!somvr_stl_cue_step(&q, 0x100, 1, 1.1, 1.5) && !somvr_stl_cue_step(&q, 0x100, 1, 3.0, 1.5), "held: quiet");
    CHECK(!somvr_stl_cue_step(&q, 0, 0, 3.1, 1.5), "gone");
    CHECK(!somvr_stl_cue_step(&q, 0x100, 1, 3.5, 1.5), "back after 0.4 s: no tick (a flicker at the arc's edge)");
    CHECK(!somvr_stl_cue_step(&q, 0, 0, 3.6, 1.5) && somvr_stl_cue_step(&q, 0x100, 1, 5.2, 1.5),
          "back after 1.6 s: a tick");
    CHECK(somvr_stl_cue_step(&q, 0x200, 1, 5.3, 1.5), "another orc: a tick");
    CHECK(q.ticks == 3 && q.orc == 0x200, "3 ticks (%llu)", (unsigned long long)q.ticks);
}

static void test_standoff(void) {
    const float orc[3] = { 0, 0, 0 }, r = 118.0f, hz = 8.0f, dt = 0.05f;
    float out[3], p[3] = { 0, 0, -300 }, d[3] = { 0, 0, 7.0f };
    CHECK(!somvr_stl_standoff(p, orc, r, dt, hz, d, out) && out[2] == 0, "far: the step free");
    /* walk straight at it at 140 cm/s (20 Hz): stops short of r, never inside, smoothly */
    float last = 1e9f;
    int cut = 0, mono = 1;
    for (int i = 0; i < 200; i++) {
        float step[3] = { 0, 0, 7.0f };
        if (somvr_stl_standoff(p, orc, r, dt, hz, step, out))
            cut++, step[0] += out[0], step[2] += out[2];
        p[2] += step[2];
        mono &= step[2] <= last + 1e-4f;
        if (p[2] > -r - 20)
            last = step[2];
    }
    CHECK(cut > 0 && -p[2] >= r - 1e-3f && -p[2] < r + 1.0f, "walked in: stopped at %.2f (r %.0f)", -p[2], r);
    CHECK(mono, "the steps only shrink near the edge");
    /* sideways at the edge: free */
    float q[3] = { 0, 0, -r }, side[3] = { 7.0f, 0, 0 };
    CHECK(!somvr_stl_standoff(q, orc, r, dt, hz, side, out), "sideways along the edge: free");
    float back[3] = { 0, 0, -7.0f };
    CHECK(!somvr_stl_standoff(q, orc, r, dt, hz, back, out), "back: free");
    /* inside already (the orc stepped back into him): no push, no step further in */
    float in[3] = { 0, 0, -90 }, fw[3] = { 0, 0, 5.0f };
    CHECK(somvr_stl_standoff(in, orc, r, dt, hz, fw, out) && fabsf(out[2] + 5.0f) < 1e-4f, "inside: the inward step cut to 0");
    CHECK(!somvr_stl_standoff(in, orc, r, dt, hz, back, out) && out[2] == 0, "inside: back out free, no push");
    /* a diagonal at the edge keeps its sideways part */
    float diag[3] = { 5.0f, 0, 5.0f };
    CHECK(somvr_stl_standoff(q, orc, r, dt, hz, diag, out) && fabsf(diag[0] + out[0] - 5.0f) < 1e-4f &&
          fabsf(diag[2] + out[2]) < 1e-4f, "diagonal: the inward half cut, the sideways half kept");
    /* frame-rate independent: 90 Hz stops at the same place */
    float p2[3] = { 0, 0, -300 };
    for (int i = 0; i < 900; i++) {
        float step[3] = { 0, 0, 140.0f / 90.0f };
        if (somvr_stl_standoff(p2, orc, r, 1.0f / 90.0f, hz, step, out))
            step[2] += out[2];
        p2[2] += step[2];
    }
    CHECK(-p2[2] >= r - 1e-3f && -p2[2] < r + 1.0f, "90 Hz: stopped at %.2f", -p2[2]);
    float z[3] = { 0, 0, 0 };
    CHECK(!somvr_stl_standoff(z, orc, r, dt, hz, d, out) && !somvr_stl_standoff(p, orc, 0, dt, hz, d, out), "degenerate");
}

int main(void) {
    test_standoff();
    test_angles();
    test_possible();
    test_judge();
    test_cue();
    printf("test_stealth: %d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
