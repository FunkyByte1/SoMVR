/* test_crouch.c - crouch by ducking (docs/re-xr-pipeline.md 41): somvr_crouch.c - the duck's lines and timers, the
 * hysteresis, the minimum hold, small bobs, the standing height (a recentre, a taller stance), tracking jumps and
 * losses, disarming (seated play), and the stealth hold (the duck's, the button's toggle, the two together). A head
 * sampled at 90 Hz. */
#include "../somvr_crouch.h"

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

#define DT (1.0 / 90)

/* the head at y for sec seconds from *t: the edges seen (+1 ons, -1 offs), the time of the first edge */
typedef struct {
    int on, off;
    double t_first;
} edges;
static edges run(somvr_crouch *s, const somvr_crouch_cfg *c, double *t, float y, double sec, int ok, int armed) {
    edges e = {0, 0, -1};
    for (double end = *t + sec; *t < end - 1e-9; *t += DT) {
        int k = somvr_crouch_step(s, c, *t, y, ok, armed);
        if (k && e.t_first < 0) e.t_first = *t;
        e.on += k > 0, e.off += k < 0;
    }
    return e;
}

static void test_duck(void) {
    somvr_crouch_cfg c;
    somvr_crouch_cfg_default(&c);
    CHECK(c.depth_cm == 28 && c.up_cm == 18 && c.on_s > 0.1f && c.on_s < 0.2f && c.hold_s == 0.5f, "the defaults");
    somvr_crouch s;
    somvr_crouch_init(&s);
    double t = 10;
    edges e = run(&s, &c, &t, 120, 1, 1, 1);
    CHECK(e.on == 0 && !s.ducked && somvr_crouch_depth(&s) == 0, "no standing height yet: no duck at any head height");
    somvr_crouch_stand(&s, 165); /* the lines: crouch below 137, stand above 147 */
    CHECK(s.have && s.stand == 165 && s.stands == 1, "the standing height at the recentre");
    /* small bobs, a nod, a look at the floor */
    e = run(&s, &c, &t, 160, 1, 1, 1);
    CHECK(e.on == 0, "5 cm down: nothing");
    e = run(&s, &c, &t, 145, 2, 1, 1);
    CHECK(e.on == 0, "20 cm down for 2 s (a look at the floor): nothing");
    e = run(&s, &c, &t, 136, 0.1, 1, 1);
    CHECK(e.on == 0, "below the line for 0.1 s: not yet (on_s 0.15)");
    e = run(&s, &c, &t, 160, 0.5, 1, 1);
    CHECK(e.on == 0 && !s.ducked, "a dip below the line shorter than on_s: nothing");
    double t0 = t;
    e = run(&s, &c, &t, 130, 1, 1, 1);
    CHECK(e.on == 1 && e.off == 0 && s.ducked, "35 cm down held: one crouch");
    CHECK(e.t_first - t0 >= c.on_s - 1e-6 && e.t_first - t0 < c.on_s + 2 * DT, "after on_s (%.3f s)", e.t_first - t0);
    CHECK(fabsf(somvr_crouch_depth(&s) - 35) < 1e-3f, "the depth 35 cm (%g)", somvr_crouch_depth(&s));
    /* inside the hysteresis band (137..147): stays crouched */
    e = run(&s, &c, &t, 142, 3, 1, 1);
    CHECK(e.off == 0 && s.ducked, "the head between the lines (23 cm down): still crouched");
    e = run(&s, &c, &t, 150, 0.1, 1, 1);
    CHECK(e.off == 0 && s.ducked, "up for 0.1 s: still (off_s 0.2)");
    e = run(&s, &c, &t, 142, 0.3, 1, 1);
    t0 = t;
    e = run(&s, &c, &t, 163, 1, 1, 1);
    CHECK(e.off == 1 && e.on == 0 && !s.ducked, "stood up: one stand");
    CHECK(e.t_first - t0 >= c.off_s - 1e-6 && e.t_first - t0 < c.off_s + 2 * DT, "after off_s (%.3f s)", e.t_first - t0);
    /* the minimum hold: a quick duck and up */
    t0 = t;
    e = run(&s, &c, &t, 130, 0.2, 1, 1);
    CHECK(e.on == 1, "a quick duck crouches");
    e = run(&s, &c, &t, 165, 0.25, 1, 1);
    CHECK(e.off == 0 && s.ducked, "up at once: held for the minimum (0.5 s)");
    e = run(&s, &c, &t, 165, 0.5, 1, 1);
    CHECK(e.off == 1 && !s.ducked && e.t_first - t0 >= c.hold_s - 1e-6, "then the stand (%.3f s after the duck began)",
          e.t_first - t0);
    /* a bob at the duck line while crouched never flickers: 132..140 at 3 Hz */
    e = run(&s, &c, &t, 130, 0.5, 1, 1);
    int flips = 0;
    for (int i = 0; i < 270; i++, t += DT) flips += somvr_crouch_step(&s, &c, t, 136 + 4 * sinf((float)i * 0.21f), 1, 1) != 0;
    CHECK(flips == 0 && s.ducked, "a bob around the duck line while crouched: no flicker (%d)", flips);
    e = run(&s, &c, &t, 165, 1, 1, 1);
    CHECK(e.off == 1, "stood");
    /* a bob around the stand line while standing: no crouch */
    flips = 0;
    for (int i = 0; i < 270; i++, t += DT) flips += somvr_crouch_step(&s, &c, t, 147 + 4 * sinf((float)i * 0.21f), 1, 1) != 0;
    CHECK(flips == 0 && !s.ducked, "a bob around the stand line while standing: nothing (%d)", flips);
    CHECK(s.ons == 3 && s.offs == 3, "the counts (%llu on, %llu off)", (unsigned long long)s.ons,
          (unsigned long long)s.offs);
    /* the tunables: a deeper duck */
    c.depth_cm = 40, c.up_cm = 30;
    e = run(&s, &c, &t, 130, 1, 1, 1);
    CHECK(e.on == 0, "35 cm down with SOMVR_CROUCH_DEPTH_CM=40: nothing");
    e = run(&s, &c, &t, 120, 1, 1, 1);
    CHECK(e.on == 1, "45 cm down: the crouch");
}

static void test_stand(void) {
    somvr_crouch_cfg c;
    somvr_crouch_cfg_default(&c);
    somvr_crouch s;
    somvr_crouch_init(&s);
    double t = 0;
    /* recentred while bent (150): standing up to 168 raises the standing height after rise_s */
    somvr_crouch_stand(&s, 150);
    edges e = run(&s, &c, &t, 168, 0.5, 1, 1);
    CHECK(s.stand == 150, "over it for 0.5 s: not yet");
    e = run(&s, &c, &t, 168, 0.8, 1, 1);
    CHECK(s.stand == 168 && s.rises == 1, "over it for 1 s: the new standing height (%g)", s.stand);
    e = run(&s, &c, &t, 145, 1, 1, 1);
    CHECK(e.on == 0, "23 cm under the new height: nothing (the old one would have crouched at 122 only)");
    e = run(&s, &c, &t, 136, 1, 1, 1);
    CHECK(e.on == 1, "32 cm under it: the crouch");
    /* a tall bob (a stretch) shorter than rise_s does not raise it; a rise takes the lowest head of its time */
    e = run(&s, &c, &t, 168, 1, 1, 1);
    CHECK(e.off == 1, "up");
    e = run(&s, &c, &t, 180, 0.5, 1, 1);
    e = run(&s, &c, &t, 168, 0.5, 1, 1);
    CHECK(s.stand == 168, "on tiptoes for 0.5 s: no change (%g)", s.stand);
    e = run(&s, &c, &t, 176, 0.5, 1, 1);
    e = run(&s, &c, &t, 174, 0.6, 1, 1);
    CHECK(s.stand == 174 && s.rises == 2, "6-8 cm over it for 1.1 s: the lowest of that time (%g)", s.stand);
    /* a recentre while ducked: that height is the standing one now, he stands; the stance rises it again */
    e = run(&s, &c, &t, 130, 1, 1, 1);
    CHECK(e.on == 1, "a duck");
    somvr_crouch_stand(&s, 130);
    e = run(&s, &c, &t, 130, 1, 1, 1);
    CHECK(e.off == 1 && s.stand == 130, "recentred while ducked: the stand (the head is at the standing height now)");
    e = run(&s, &c, &t, 170, 1.2, 1, 1);
    CHECK(s.stand == 170 && e.on == 0, "standing up: the height follows (%g)", s.stand);
    /* a tracking jump: the timers restart, no edge from the jump itself */
    e = run(&s, &c, &t, 170, 0.5, 1, 1);
    uint64_t j = s.jumps;
    e = run(&s, &c, &t, 100, 0.1, 1, 1);
    CHECK(s.jumps == j + 1 && e.on == 0, "a 70 cm jump: counted (%llu), nothing yet", (unsigned long long)s.jumps);
    e = run(&s, &c, &t, 100, 0.3, 1, 1);
    CHECK(e.on == 1, "the head stays down: after on_s the crouch");
    /* untracked: the state holds */
    e = run(&s, &c, &t, 170, 2, 0, 1);
    CHECK(e.off == 0 && s.ducked, "untracked: nothing changes");
    e = run(&s, &c, &t, NAN, 1, 1, 1);
    CHECK(e.off == 0 && s.ducked, "a NaN head: nothing changes");
    /* disarmed (seated play, the menu's button) while crouched: one stand, then nothing at any height */
    e = run(&s, &c, &t, 100, 1, 1, 0);
    CHECK(e.off == 1 && !s.ducked, "disarmed while ducked: one stand");
    e = run(&s, &c, &t, 100, 1, 1, 0);
    CHECK(e.on == 0 && e.off == 0, "disarmed: no duck");
    e = run(&s, &c, &t, 100, 1, 1, 1);
    CHECK(e.on == 1, "armed again with the head down: the crouch after on_s");
    somvr_crouch_stand(&s, NAN);
    CHECK(s.stand == 170, "a NaN standing height is ignored");
}

static void test_hold(void) {
    somvr_crouch_hold h;
    somvr_crouch_hold_init(&h);
    CHECK(!somvr_crouch_hold_step(&h, 0, 0, 0), "standing: nothing held");
    CHECK(somvr_crouch_hold_step(&h, 1, 0, 1) && somvr_crouch_hold_step(&h, 0, 0, 1), "the duck: held while ducked");
    CHECK(!somvr_crouch_hold_step(&h, -1, 0, 0) && !somvr_crouch_hold_step(&h, 0, 0, 0), "the stand: let go");
    /* the button while standing (seated play): a latch, until the button again */
    CHECK(somvr_crouch_hold_step(&h, 0, 1, 0) && somvr_crouch_hold_step(&h, 0, 0, 0), "the button: held (latched)");
    CHECK(!somvr_crouch_hold_step(&h, 0, 1, 0) && !somvr_crouch_hold_step(&h, 0, 0, 0), "the button again: up");
    /* a button crouch, then a duck and a stand: the stand stands him up */
    somvr_crouch_hold_step(&h, 0, 1, 0);
    CHECK(somvr_crouch_hold_step(&h, 1, 0, 1), "latched, then a duck: still held (the duck's now)");
    CHECK(!somvr_crouch_hold_step(&h, -1, 0, 0) && h.latched == 0, "the duck's end: up (the latch gone)");
    /* the button while ducked: up, and the duck holds nothing until its next start */
    CHECK(somvr_crouch_hold_step(&h, 1, 0, 1), "a duck: held");
    CHECK(!somvr_crouch_hold_step(&h, 0, 1, 1) && !somvr_crouch_hold_step(&h, 0, 0, 1), "the button while ducked: up, stays up");
    CHECK(somvr_crouch_hold_step(&h, 0, 1, 1), "the button again while ducked: held (latched)");
    CHECK(!somvr_crouch_hold_step(&h, -1, 0, 0), "the stand: up");
    CHECK(somvr_crouch_hold_step(&h, 1, 0, 1), "the next duck holds again");
    CHECK(!somvr_crouch_hold_step(&h, 0, 1, 1) && !somvr_crouch_hold_step(&h, -1, 0, 0) &&
              somvr_crouch_hold_step(&h, 1, 0, 1),
          "up by the button, the stand, the next duck: held");
    CHECK(h.toggles == 6, "the button's toggles counted (%llu)", (unsigned long long)h.toggles);
}

int main(void) {
    test_duck();
    test_stand();
    test_hold();
    printf("test_crouch: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
