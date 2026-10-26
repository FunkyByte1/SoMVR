/* test_session.c - unit tests of somvr_session.c: the B + Y chord (a lone press passes after the window, a tap, the
 * chord's mark / too short / too long, a passed button joining, an old hold not joining, swallowed releases, presses
 * between ticks, input loss) and the text helpers (docs/re-xr-pipeline.md 28) */
#include "../somvr_session.h"

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

/* a fake controller pair: held bits and the layer's press counters */
static somvr_chord g_c;
static somvr_chord_cfg g_cfg;
static somvr_chord_in g_in;
static double g_t;
static somvr_chord_out tick(double dt) {
    somvr_chord_out o;
    g_t += dt;
    somvr_chord_tick(&g_c, &g_cfg, &g_in, g_t, &o);
    return o;
}
static void down(int b) { g_in.held[b] = 1, g_in.presses[b]++; }
static void up(int b) { g_in.held[b] = 0; }
static void fresh(void) {
    somvr_chord_reset(&g_c);
    somvr_chord_cfg_default(&g_cfg);
    memset(&g_in, 0, sizeof g_in);
    g_in.act[0] = g_in.act[1] = 1;
    g_in.presses[0] = 7, g_in.presses[1] = 250; /* any start values: the first tick is the baseline */
    g_t = 100;
    tick(0.011);
}

static void test_lone(void) {
    fresh();
    uint8_t p0 = g_c.out_p[SOMVR_CHB_B];
    down(SOMVR_CHB_B);
    somvr_chord_out o = tick(0.011);
    CHECK(!o.held[SOMVR_CHB_B] && o.presses[SOMVR_CHB_B] == p0 && !o.ev, "B held back at its press");
    for (int i = 0; i < 9; i++) o = tick(0.011); /* 0.1 s */
    CHECK(!o.held[SOMVR_CHB_B], "still held back at 0.1 s");
    o = tick(0.011);
    o = tick(0.011); /* 0.12 s */
    CHECK(o.held[SOMVR_CHB_B] && o.presses[SOMVR_CHB_B] == (uint8_t)(p0 + 1), "passed after the window: one press");
    o = tick(0.011);
    CHECK(o.held[SOMVR_CHB_B] && o.presses[SOMVR_CHB_B] == (uint8_t)(p0 + 1), "held, no new press");
    up(SOMVR_CHB_B);
    o = tick(0.011);
    CHECK(!o.held[SOMVR_CHB_B] && g_c.st[SOMVR_CHB_B] == SOMVR_CHS_UP, "released at once");
    CHECK(!o.held[SOMVR_CHB_Y] && o.presses[SOMVR_CHB_Y] == 7, "Y untouched");
    /* a tap shorter than a tick (the counter moved, never seen held): passed as a tap after the window */
    g_in.presses[SOMVR_CHB_Y]++;
    o = tick(0.011);
    CHECK(!o.held[SOMVR_CHB_Y] && o.presses[SOMVR_CHB_Y] == 7, "a tap waits too");
    for (int i = 0; i < 11; i++) o = tick(0.011);
    CHECK(!o.held[SOMVR_CHB_Y] && o.presses[SOMVR_CHB_Y] == 8 && g_c.st[SOMVR_CHB_Y] == SOMVR_CHS_UP,
          "the tap arrives as a press with the button up (the controls' tap path)");
    /* a re-press between two ticks while passed */
    down(SOMVR_CHB_B);
    for (int i = 0; i < 12; i++) o = tick(0.011);
    uint8_t pb = o.presses[SOMVR_CHB_B];
    g_in.presses[SOMVR_CHB_B]++; /* up and down again between ticks, still held */
    o = tick(0.011);
    CHECK(o.held[SOMVR_CHB_B] && o.presses[SOMVR_CHB_B] == (uint8_t)(pb + 1), "a re-press passes through");
    up(SOMVR_CHB_B);
    tick(0.011);
}

static void test_mark(void) {
    fresh();
    uint8_t py = g_c.out_p[0], pb = g_c.out_p[1];
    down(SOMVR_CHB_Y);
    somvr_chord_out o = tick(0.011);
    down(SOMVR_CHB_B); /* 30 ms later */
    tick(0.011);
    o = tick(0.011);
    CHECK(g_c.chord && !o.held[0] && !o.held[1] && !o.ev, "a chord: both held back");
    int armed = 0, at = 0;
    for (int i = 0; i < 60; i++) {
        o = tick(0.011);
        if (o.ev == SOMVR_CHE_ARMED) armed++, at = i;
        CHECK(!o.held[0] && !o.held[1], "nothing reaches the game (tick %d)", i);
    }
    CHECK(armed == 1 && at >= 43 && at <= 46, "armed once at 0.5 s (%d at %d)", armed, at);
    up(SOMVR_CHB_B); /* let go */
    o = tick(0.011);
    CHECK(o.ev == SOMVR_CHE_MARK && o.held_s > 0.6f && o.held_s < 0.75f, "a mark at the let-go (%d, %.3f s)", o.ev,
          (double)o.held_s);
    o = tick(0.011);
    CHECK(!o.ev && !o.held[0] && g_c.chord, "Y still held: swallowed");
    down(SOMVR_CHB_B); /* pressed again while Y is still held: part of the same chord */
    o = tick(0.011);
    CHECK(!o.ev && !o.held[1], "a new B before both are up: swallowed");
    up(SOMVR_CHB_B), up(SOMVR_CHB_Y);
    o = tick(0.011);
    CHECK(!g_c.chord && o.presses[0] == py && o.presses[1] == pb && g_c.n_marks == 1, "both up: over, no press ever arrived");
    /* afterwards a lone press works as usual */
    down(SOMVR_CHB_Y);
    for (int i = 0; i < 12; i++) o = tick(0.011);
    CHECK(o.held[SOMVR_CHB_Y] && o.presses[SOMVR_CHB_Y] == (uint8_t)(py + 1), "a lone Y after the chord");
}

static void test_short_long(void) {
    fresh();
    down(SOMVR_CHB_Y), down(SOMVR_CHB_B);
    somvr_chord_out o = tick(0.011);
    for (int i = 0; i < 20; i++) o = tick(0.011);
    up(SOMVR_CHB_Y);
    o = tick(0.011);
    CHECK(o.ev == SOMVR_CHE_SHORT && g_c.n_short == 1, "0.23 s: too short (%d)", o.ev);
    up(SOMVR_CHB_B);
    o = tick(0.011);
    CHECK(!g_c.chord && !o.held[0] && !o.held[1] && o.presses[0] == 7 && o.presses[1] == 250, "nothing for the game");
    down(SOMVR_CHB_Y), down(SOMVR_CHB_B);
    int armed = 0;
    for (int i = 0; i < 600; i++) armed += tick(0.011).ev == SOMVR_CHE_ARMED; /* 6.6 s */
    up(SOMVR_CHB_Y), up(SOMVR_CHB_B);
    o = tick(0.011);
    CHECK(armed == 1 && o.ev == SOMVR_CHE_LONG && g_c.n_long == 1 && !g_c.chord, "held 6.6 s: no mark (%d, %d)", armed,
          o.ev);
    /* both tapped within one tick: too short at once, nothing passes */
    g_in.presses[0]++, g_in.presses[1]++;
    o = tick(0.011);
    CHECK(o.ev == SOMVR_CHE_SHORT && !g_c.chord, "a double tap within a tick: short");
    for (int i = 0; i < 20; i++) o = tick(0.011);
    CHECK(o.presses[0] == 7 && o.presses[1] == 250, "... and swallowed");
}

static void test_join(void) {
    fresh();
    down(SOMVR_CHB_Y); /* wraith vision */
    somvr_chord_out o;
    for (int i = 0; i < 20; i++) o = tick(0.011); /* 0.22 s: Y is the game's */
    CHECK(o.held[SOMVR_CHB_Y], "Y passed");
    down(SOMVR_CHB_B);
    o = tick(0.011);
    CHECK(g_c.chord && !o.held[SOMVR_CHB_Y] && !o.held[SOMVR_CHB_B], "B within join_s: a chord, Y goes up for the game");
    for (int i = 0; i < 60; i++) tick(0.011);
    up(0), up(1);
    o = tick(0.011);
    CHECK(o.ev == SOMVR_CHE_MARK, "a mark");
    tick(0.011);
    /* an old hold: Y held 1 s, then B: no chord, B passes after its window */
    down(SOMVR_CHB_Y);
    for (int i = 0; i < 90; i++) o = tick(0.011);
    down(SOMVR_CHB_B);
    for (int i = 0; i < 12; i++) o = tick(0.011);
    CHECK(!g_c.chord && o.held[SOMVR_CHB_Y] && o.held[SOMVR_CHB_B], "Y held 1 s + B: both the game's");
    for (int i = 0; i < 60; i++) o = tick(0.011);
    up(0), up(1);
    o = tick(0.011);
    CHECK(!o.ev && !o.held[0] && !o.held[1], "no mark");
}

static void test_loss(void) {
    fresh();
    down(SOMVR_CHB_Y), down(SOMVR_CHB_B);
    tick(0.011);
    for (int i = 0; i < 30; i++) tick(0.011);
    g_in.act[0] = 0; /* the left controller lost */
    somvr_chord_out o = tick(0.011);
    CHECK(o.ev == SOMVR_CHE_SHORT, "a lost hand ends the chord (0.34 s: short)");
    g_in.act[0] = 1;
    o = tick(0.011);
    CHECK(g_c.chord && !o.held[0], "both still held: swallowed until both are up");
    up(0), up(1);
    tick(0.011);
    CHECK(!g_c.chord, "over");
}

static void test_text(void) {
    char b[64];
    somvr_mark_name(9, 5, 3, 12, b, sizeof b);
    CHECK(!strcmp(b, "090503_12"), "name %s", b);
    CHECK(somvr_mark_word("mark", b, sizeof b) && !strcmp(b, ""), "mark");
    CHECK(somvr_mark_word("mark   the  sword\tis\nbent  ", b, sizeof b) && !strcmp(b, "the sword is bent"), "note '%s'", b);
    CHECK(!somvr_mark_word("marker", b, sizeof b) && !somvr_mark_word("mar", b, sizeof b) && !somvr_mark_word("quit", b, 8),
          "not mark words");
    CHECK(somvr_mark_word("mark \x01\xff ok", b, sizeof b) && !strcmp(b, "ok"), "non-printables dropped '%s'", b);
    char s[6];
    CHECK(somvr_mark_word("mark abcdefgh", s, sizeof s) && !strcmp(s, "abcde"), "cut '%s'", s);
    CHECK(somvr_ss_path_ok("/a/b", 448) && !somvr_ss_path_ok("a/b", 448) && !somvr_ss_path_ok("/a\nb", 448) &&
              !somvr_ss_path_ok("", 448) && !somvr_ss_path_ok(NULL, 448),
          "paths");
    char lng[500];
    memset(lng, 'a', sizeof lng - 1), lng[0] = '/', lng[sizeof lng - 1] = 0;
    CHECK(!somvr_ss_path_ok(lng, SOMVR_SS_DIR_MAX), "too long");
}

int main(void) {
    test_lone();
    test_mark();
    test_short_long();
    test_join();
    test_loss();
    test_text();
    printf("test_session: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
