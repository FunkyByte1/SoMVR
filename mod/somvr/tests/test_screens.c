/* test_screens.c - unit tests of somvr_screens.c (build 22, som-b22-screens): SOMVR_BM_DEATH's parse and the death's
 * rule run against the body moments' own step through the drill's logged death (run 20261025-225245: the death names,
 * the game's death presentation, the Nemesis screens' pause, B, the respawn presentation) - build 21's window during the
 * presentation, none with first; the window opened in the pause; the respawn out of the window; a death without the
 * Nemesis screens re-seating by the slow blink; a death survived (no window ever); off = build 21 frame for frame.
 * SOMVR_POINTER_MOUSE1's parse and rule; the menu stack's top index. */
#include "../somvr_screens.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../somvr_bodymoments.h"

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
#define DT (1.0 / 20) /* the headless H4 rate */
#define CB(c) SOMVR_BMC_BIT(c)

static void test_parse(void) {
    somvr_sd_cfg c;
    CHECK(somvr_sd_parse(NULL, &c) == 0 && !c.on && fabsf(c.blink_black_s - 0.6f) < 1e-6f, "unset: off, the defaults");
    CHECK(somvr_sd_parse("first", &c) == 0 && c.on, "first");
    CHECK(somvr_sd_parse("game", &c) == 0 && !c.on, "game");
    CHECK(somvr_sd_parse("off", &c) == 0 && !c.on, "off");
    CHECK(somvr_sd_parse("first,200,800,900", &c) == 0 && c.on && fabsf(c.blink_out_s - 0.2f) < 1e-6f &&
              fabsf(c.blink_black_s - 0.8f) < 1e-6f && fabsf(c.blink_in_s - 0.9f) < 1e-6f, "first with the blink");
    CHECK(somvr_sd_parse("first,200,800", &c) == -1 && !c.on, "two numbers: bad, off");
    CHECK(somvr_sd_parse("first,200,800,9000", &c) == -1 && !c.on, "out of range: bad");
    CHECK(somvr_sd_parse("firstx", &c) == -1 && !c.on, "a wrong word");
    int bad = 0;
    CHECK(somvr_pm1_parse(NULL, &bad) == SOMVR_PM1_OFF && !bad, "Mouse1 unset: off");
    CHECK(somvr_pm1_parse("nemesis", &bad) == SOMVR_PM1_NEMESIS && somvr_pm1_parse("on", &bad) == SOMVR_PM1_NEMESIS &&
              somvr_pm1_parse("all", &bad) == SOMVR_PM1_ALL && somvr_pm1_parse("off", &bad) == SOMVR_PM1_OFF && !bad,
          "Mouse1 words");
    CHECK(somvr_pm1_parse("yes", &bad) == SOMVR_PM1_OFF && bad, "Mouse1 bad word: off, bad");
}

static void test_pm1(void) {
    CHECK(!somvr_pm1_want(SOMVR_PM1_OFF, 1, 1, 1), "off: never");
    CHECK(somvr_pm1_want(SOMVR_PM1_NEMESIS, 1, 1, 1), "nemesis: a click held on the hierarchy");
    CHECK(!somvr_pm1_want(SOMVR_PM1_NEMESIS, 1, 1, 0), "nemesis: not on another layer (the pause menu's list)");
    CHECK(!somvr_pm1_want(SOMVR_PM1_NEMESIS, 0, 1, 1), "nemesis: no click held");
    CHECK(!somvr_pm1_want(SOMVR_PM1_NEMESIS, 1, 0, 1), "nemesis: not in a menu");
    CHECK(somvr_pm1_want(SOMVR_PM1_ALL, 1, 1, 0) && !somvr_pm1_want(SOMVR_PM1_ALL, 1, 0, 0), "all: every menu click");
    CHECK(somvr_scr_top_index(0) == -1 && somvr_scr_top_index(1) == 0 && somvr_scr_top_index(8) == 7 &&
              somvr_scr_top_index(9) == -1 && somvr_scr_top_index(0xffffffffu) == -1, "the top index");
}

/* the drill's death (run D, logs/boot-check-20261025-225245), relative to the first death name at 0:
 *   0 .. 8.5 s   death (PreDeath, a struggle 3.7 .. 8.5), unpaused
 *   8.6 .. 17.6  pres + death (Player_Death: the game camera on the body, the killer's taunt), unpaused
 *   17.6 .. 17.7 death, unpaused; 17.7 .. 33.2 death, paused (the Nemesis screens)
 *   33.2 .. 33.7 pres (B: PC_DeathRecover), paused; 33.7 .. 34.7 pres, unpaused (the teleport to the tower at 34.5)
 *   34.7 ..      nothing: Talion at the tower */
typedef struct {
    int win_frames_pres, win_at_pause, win_after, frames_paused_win, max_fade_pres_x100, released_by, cut_in_window;
    double win_open_t, win_close_t;
    float base_end[3];
} run_out;

static void run_death(int first, int nemesis, int survive, run_out *r) {
    somvr_bm_policy pol;
    somvr_bm_policy_default(&pol);
    somvr_bm_cfg bc;
    somvr_bm_cfg_default(&bc);
    somvr_bm s;
    somvr_bm_init(&s);
    somvr_sd_cfg sc;
    somvr_sd_parse(first ? "first" : "game", &sc);
    somvr_sd d;
    somvr_sd_init(&d);
    memset(r, 0, sizeof *r);
    r->win_open_t = r->win_close_t = -1;
    float live[3] = {0, 0, 0}, pos[3], yaw;
    int cut, prev_win = 0;
    for (double t = -2; t < 42; t += DT) {
        int death = 0, pres = 0, paused = 0, struggle = 0;
        if (survive) {
            death = t >= 0 && t < 4; /* a last chance: the death names, then up again */
        } else if (nemesis) {
            death = t >= 0 && t < 33.2;
            pres = (t >= 8.6 && t < 17.6) || (t >= 33.2 && t < 34.7);
            paused = t >= 17.7 && t < 33.7;
            struggle = t >= 3.7 && t < 8.5;
        } else { /* a fall or a beast: the death, the presentation, no pause; the respawn presentation after it */
            death = t >= 0 && t < 17.6;
            pres = (t >= 8.6 && t < 17.6) || (t >= 18 && t < 19.5);
        }
        if (t >= 34.5 || (!nemesis && t >= 19.0)) live[0] = 5800, live[2] = -20000; /* the respawn's teleport */
        uint32_t conds = (death ? CB(SOMVR_FB_DEATH) : 0) | (pres ? CB(SOMVR_FB_PRES) : 0) |
                         (struggle ? CB(SOMVR_FB_STRUGGLE) : 0);
        int hc, w, wc, act = somvr_bm_actions(&pol, conds, &hc, &w, &wc);
        somvr_sd_out so;
        somvr_sd_step(&d, &sc, death, w, wc, paused, s.win, s.frozen || s.blink != SOMVR_BMB_IDLE, SOMVR_FB_DEATH, t, &so);
        somvr_bm_cfg use = bc;
        if (so.slow_blink)
            use.blink_out_s = sc.blink_out_s, use.blink_black_s = sc.blink_black_s, use.blink_in_s = sc.blink_in_s;
        somvr_bm_out o;
        somvr_bm_step(&s, &use, act, hc, so.win, so.win_cond, so.step_paused, t, &o);
        if (!o.window) somvr_bm_base(&s, &use, live, 0, 0, 0, SOMVR_BMY_PLAYER, t, pos, &yaw, &cut);
        if (o.window && pres && death && !paused) r->win_frames_pres++;
        if (pres && death && !paused && (int)(o.fade * 100) > r->max_fade_pres_x100) r->max_fade_pres_x100 = (int)(o.fade * 100);
        if (o.window && paused) r->frames_paused_win++;
        if (t >= 20 && t < 21) r->win_at_pause |= o.window;
        if (t >= 40) r->win_after |= o.window;
        if (o.window && !prev_win && r->win_open_t < 0) r->win_open_t = t;
        if (!o.window && prev_win) r->win_close_t = t;
        if (o.release) r->released_by = o.release, r->cut_in_window |= o.release == 3 && o.window;
        prev_win = o.window;
    }
    memcpy(r->base_end, pos, sizeof pos);
}

static void test_death(void) {
    run_out g, f;
    run_death(0, 1, 0, &g);
    CHECK(g.win_frames_pres > 100, "build 21: the game's death presentation in the window (%d frames)", g.win_frames_pres);
    CHECK(g.win_open_t > 8.5 && g.win_open_t < 9.0, "build 21: the window at the presentation (%.2f s)", g.win_open_t);
    CHECK(g.win_at_pause && g.frames_paused_win > 250, "build 21: the Nemesis screens in the window (%d)", g.frames_paused_win);
    run_death(1, 1, 0, &f);
    CHECK(f.win_frames_pres == 0, "first: never the window while dying and the game runs (%d frames)", f.win_frames_pres);
    CHECK(f.max_fade_pres_x100 >= 79, "first: the world greyed through the presentation (fade %.2f)", f.max_fade_pres_x100 / 100.0);
    CHECK(f.win_open_t >= 17.7 && f.win_open_t < 18.0, "first: the window opens in the pause (%.2f s)", f.win_open_t);
    CHECK(f.win_at_pause && f.frames_paused_win > 250, "first: the Nemesis screens in the window (%d)", f.frames_paused_win);
    CHECK(f.win_close_t > 34.7 && f.win_close_t < 36.5 && !f.win_after, "first: the respawn out of the window (%.2f s)",
          f.win_close_t);
    CHECK(fabsf(f.base_end[0] - 5800) < 1 && fabsf(f.base_end[2] + 20000) < 1, "first: the view at the respawn (%.0f %.0f)",
          f.base_end[0], f.base_end[2]);
    CHECK(f.cut_in_window || f.released_by == 3, "first: the death's release under the window (the layer fades back), %d",
          f.released_by);

    /* no Nemesis screens: the hold's release by the slow blink, the view at the respawn */
    somvr_bm_policy pol;
    somvr_bm_policy_default(&pol);
    somvr_bm_cfg bc;
    somvr_bm_cfg_default(&bc);
    somvr_bm s;
    somvr_bm_init(&s);
    somvr_sd_cfg sc;
    somvr_sd_parse("first", &sc);
    somvr_sd d;
    somvr_sd_init(&d);
    float live[3] = {0, 0, 0}, pos[3], yaw;
    int cut, black_frames = 0, blinks = 0, win_any_dying = 0;
    double black_from = -1, black_to = -1;
    for (double t = -1; t < 30; t += DT) {
        int death = t >= 0 && t < 17.6, pres = t >= 8.6 && t < 17.6;
        if (t >= 17.65) live[0] = 4000;
        uint32_t conds = (death ? CB(SOMVR_FB_DEATH) : 0) | (pres ? CB(SOMVR_FB_PRES) : 0);
        int hc, w, wc, act = somvr_bm_actions(&pol, conds, &hc, &w, &wc);
        somvr_sd_out so;
        somvr_sd_step(&d, &sc, death, w, wc, 0, s.win, s.frozen || s.blink != SOMVR_BMB_IDLE, SOMVR_FB_DEATH, t, &so);
        somvr_bm_cfg use = bc;
        if (so.slow_blink)
            use.blink_out_s = sc.blink_out_s, use.blink_black_s = sc.blink_black_s, use.blink_in_s = sc.blink_in_s;
        somvr_bm_out o;
        somvr_bm_step(&s, &use, act, hc, so.win, so.win_cond, so.step_paused, t, &o);
        somvr_bm_base(&s, &use, live, 0, 0, 0, SOMVR_BMY_PLAYER, t, pos, &yaw, &cut);
        win_any_dying |= o.window && death;
        if (o.release == 2) blinks++;
        if (s.blink == SOMVR_BMB_BLACK) {
            black_frames++;
            if (black_from < 0) black_from = t;
            black_to = t;
        }
    }
    CHECK(!win_any_dying && blinks == 1, "no Nemesis: no window, one blink (%d)", blinks);
    CHECK(black_to - black_from >= 0.55 && black_frames >= 11, "no Nemesis: the slow blink's black %.2f s (%d frames)",
          black_to - black_from, black_frames);
    CHECK(fabsf(pos[0] - 4000) < 1 && d.phase == SOMVR_SD_IDLE && d.recovered == 1, "no Nemesis: the view at the place "
          "(%.0f), idle (%d)", pos[0], d.phase);

    /* a last chance: the death names for 4 s, up again: no window, idle */
    run_out v;
    run_death(1, 0, 1, &v);
    CHECK(v.win_open_t < 0 && !v.win_after, "survived: no window ever");

    /* off = build 21: the rule passes everything through */
    somvr_sd_init(&d);
    somvr_sd_parse("game", &sc);
    somvr_sd_out so;
    somvr_sd_step(&d, &sc, 1, 1, SOMVR_FB_PRES, 1, 0, 1, SOMVR_FB_DEATH, 1.0, &so);
    CHECK(so.win == 1 && so.win_cond == SOMVR_FB_PRES && so.step_paused == 1 && !so.slow_blink && d.phase == SOMVR_SD_IDLE,
          "off: as asked");
}

int main(void) {
    test_parse();
    test_pm1();
    test_death();
    printf("test_screens: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
