/* test_execute.c - the execution's pure part (somvr_execute.h): ready, the stock spend, the trigger, the judge, the side,
 * the record turn, the streak's lapse (round 2) */
#include "../somvr_execute.h"

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

static void test_ready_spend(void) {
    somvr_ex_hc h = {1, 7, 7, 8, 0, 1, 0};
    CHECK(!somvr_ex_ready(&h), "7 of 8: not ready");
    h.streak = h.charge = 8;
    CHECK(somvr_ex_ready(&h), "8 of 8: ready");
    h.enabled = 0;
    CHECK(!somvr_ex_ready(&h), "not enabled, not charged: not ready");
    h.charged = 1;
    CHECK(somvr_ex_ready(&h), "the game's flag");
    h.need = 0;
    CHECK(!somvr_ex_ready(&h), "no need: no charge feature");
    h = (somvr_ex_hc){1, 12, 5, 5, 0, 1, 0};
    CHECK(somvr_ex_ready(&h), "5 of 5 (the skill): ready");
    CHECK(!somvr_ex_ready(NULL) && !somvr_ex_ready(&(somvr_ex_hc){0, 9, 9, 8, 0, 1, 1}), "invalid");
    int16_t c;
    uint8_t u;
    CHECK(somvr_ex_spend(&h, &c, &u) == 2 && c == 0 && u == 0, "spent: the charge to 0 (%d %d)", c, u);
    h.uses = 1;
    CHECK(somvr_ex_spend(&h, &c, &u) == 2 && c == 0 && u == 1, "one use: still the charge (cmp 1; jbe)");
    h.uses = 3;
    CHECK(somvr_ex_spend(&h, &c, &u) == 1 && c == 5 && u == 2, "an extra use: uses - 1, the charge kept");
    h.uses = 0, h.charge = 0;
    CHECK(somvr_ex_spend(&h, &c, &u) == 0 && c == 0, "nothing to spend");
    h.need = 0, h.charge = 9;
    CHECK(somvr_ex_spend(&h, &c, &u) == 0 && c == 9, "no need: nothing");
}

static void test_trigger_judge(void) {
    int d = 0;
    d = somvr_ex_trigger(d, 0.5f);
    CHECK(!d, "0.5 from up: not held");
    d = somvr_ex_trigger(d, 0.61f);
    CHECK(d, "0.61: held");
    d = somvr_ex_trigger(d, 0.45f);
    CHECK(d, "0.45 while held: still held");
    d = somvr_ex_trigger(d, 0.39f);
    CHECK(!d, "0.39: released");
    CHECK(!somvr_ex_trigger(1, NAN), "NaN: released");
    CHECK(somvr_ex_judge(1, 1, 2) == SOMVR_EXJ_EXECUTE, "ready, held, heavy");
    CHECK(somvr_ex_judge(0, 1, 2) == SOMVR_EXJ_NOT_READY, "not ready");
    CHECK(somvr_ex_judge(1, 1, 1) == SOMVR_EXJ_NOT_HEAVY && somvr_ex_judge(1, 1, 3) == SOMVR_EXJ_NOT_HEAVY,
          "a light hit or a stab");
    CHECK(somvr_ex_judge(1, 0, 2) == SOMVR_EXJ_NO_TRIGGER && somvr_ex_judge(0, 0, 1) == SOMVR_EXJ_NO_TRIGGER,
          "no trigger: an ordinary hit");
    CHECK(!strcmp(somvr_ex_judge_name(SOMVR_EXJ_EXECUTE), "execute") && !strcmp(somvr_ex_judge_name(9), "?"), "names");
}

static void test_side_pick(void) {
    float o[3] = {100, 0, 200}, off;
    float front[3] = {100, 50, 260}, back[3] = {100, 50, 140}, right[3] = {160, 50, 200};
    CHECK(!somvr_ex_behind(front, o, 0, 100, &off) && fabsf(off) < 1e-3f, "front: %g", off);
    CHECK(somvr_ex_behind(back, o, 0, 100, &off) && fabsf(fabsf(off) - 180) < 1e-3f, "back: %g", off);
    CHECK(!somvr_ex_behind(right, o, 0, 100, &off) && fabsf(off - 90) < 1e-3f, "the side: %g", off);
    CHECK(somvr_ex_behind(front, o, 3.14159265f, 100, &off), "front of a turned orc = its back: %g", off);
    for (int k = 0; k < 360; k += 5) { /* the orc turned k: the contact straight ahead of it is never behind */
        float y = (float)k * 3.14159265f / 180, c[3] = {o[0] + 60 * sinf(y), 50, o[2] + 60 * cosf(y)};
        float cb[3] = {o[0] - 60 * sinf(y), 50, o[2] - 60 * cosf(y)};
        CHECK(!somvr_ex_behind(c, o, y, 100, &off) && somvr_ex_behind(cb, o, y, 100, NULL), "yaw %d: %g", k, off);
    }
    char nm[32];
    CHECK(somvr_ex_pick("A,B,C", 0, nm, sizeof nm) && !strcmp(nm, "A"), "0: A");
    CHECK(somvr_ex_pick("A,B,C", 4, nm, sizeof nm) && !strcmp(nm, "B"), "4: B");
    CHECK(somvr_ex_pick("A,,B", 1, nm, sizeof nm) && !strcmp(nm, "B"), "an empty name skipped");
    CHECK(!somvr_ex_pick("", 0, nm, sizeof nm) && !nm[0] && !somvr_ex_pick(NULL, 0, nm, sizeof nm), "none");
    CHECK(somvr_ex_pick("ABCDEFGH", 0, nm, 4) && !strcmp(nm, "ABC"), "cut to the buffer");
}

static void test_lapse(void) {
    somvr_ex_hc h = {1, 3, 3, 8, 0, 1, 0};
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 6, 0) == 6, "building: the game's 3 s -> 6");
    CHECK(somvr_ex_lapse(&h, 1, 6.0, 6, 0) == 0, "already ours: nothing to write");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 0, 0) == 0, "off: the game's");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 0, 9) == 0, "ready_s alone does nothing while building");
    CHECK(somvr_ex_lapse(&h, 1, 8.0, 6, 0) == 0, "never shorter than the game's own (a skill's 8 s)");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 2, 0) == 0, "a lapse shorter than the game's: the game's");
    CHECK(somvr_ex_lapse(&h, 0, 3.0, 6, 0) == 0, "the timer not running: leave it");
    CHECK(somvr_ex_lapse(&h, 1, 0.0, 6, 0) == 0, "delay 0 (the game's expiry wrote it): leave it");
    CHECK(somvr_ex_lapse(&h, 1, NAN, 6, 0) == 0 && somvr_ex_lapse(&h, 1, INFINITY, 6, 0) == 0, "not a delay");
    h.streak = 0;
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 6, 0) == 0, "no streak: nothing to keep");
    h.streak = h.charge = 8;
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 6, 0) == 6, "ready, no ready_s: the lapse");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 6, 8) == 8 && somvr_ex_lapse(&h, 1, 6.0, 6, 8) == 8, "ready: ready_s");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 0, 8) == 8, "ready_s without a lapse");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 6, 4) == 4, "ready: ready_s even when shorter than the lapse (still over the game's 3)");
    h.charge = 0; /* spent: the streak stays, the delay ours until the game's next hit */
    CHECK(somvr_ex_lapse(&h, 1, 8.0, 6, 8) == 0, "after the spend: never shortened");
    CHECK(somvr_ex_lapse(&h, 1, 3.0, 6, 8) == 6, "the next hit: the lapse again");
    CHECK(somvr_ex_lapse(NULL, 1, 3.0, 6, 0) == 0 && somvr_ex_lapse(&(somvr_ex_hc){0, 3, 3, 8, 0, 1, 0}, 1, 3, 6, 0) == 0,
          "invalid");
}

int main(void) {
    test_ready_spend();
    test_lapse();
    test_trigger_judge();
    test_side_pick();
    printf("test_execute: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
