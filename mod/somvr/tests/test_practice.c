/* test_practice.c - unit tests of somvr_practice.c: the switch, the orc types, the trigger words, the trip (the blink
 * there and back: fade, teleport, arrival, a fall, a timeout, the give-up, a pause, a trip replacing another), the
 * keep-clear rule, the gate's words, the spawn spot and its facing, the night test (docs/re-xr-pipeline.md 27) */
#include "../somvr_practice.h"

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
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

static void test_words(void)
{
    int m = 9;
    CHECK(somvr_pr_mode_parse("auto", &m) && m == SOMVR_PR_AUTO, "auto %d", m);
    CHECK(somvr_pr_mode_parse(" 1 ", &m) && m == SOMVR_PR_ON && somvr_pr_mode_parse("OFF", &m) && m == SOMVR_PR_OFF,
          "1, OFF");
    m = 7;
    CHECK(!somvr_pr_mode_parse("yes", &m) && !somvr_pr_mode_parse("", &m) && !somvr_pr_mode_parse(NULL, &m) &&
              !somvr_pr_mode_parse("on off", &m) && m == 7,
          "bad values leave the mode");
    CHECK(!strcmp(somvr_pr_mode_name(SOMVR_PR_AUTO), "auto") && !strcmp(somvr_pr_mode_name(5), "off"), "mode names");
    CHECK(!strcmp(somvr_pr_orc_template(SOMVR_PRO_INFANTRY), "Orc_Infantry") &&
              !strcmp(somvr_pr_orc_template(SOMVR_PRO_SHIELD), "Orc_Defender") &&
              !strcmp(somvr_pr_orc_template(SOMVR_PRO_BERSERKER), "Orc_Berserker") && !strcmp(somvr_pr_orc_template(6), ""),
          "templates (P9's names)");
    /* build 22: the beasts (the P9 probe's AI/Templates names) */
    CHECK(!strcmp(somvr_pr_orc_template(SOMVR_PRO_CARAGOR), "Warg_Brown") && !strcmp(somvr_pr_orc_template(SOMVR_PRO_GHUL), "Goblin") &&
              !strcmp(somvr_pr_orc_template(SOMVR_PRO_GRAUG), "Troll_Cave") && !strcmp(somvr_pr_orc_label(SOMVR_PRO_GHUL), "Ghul") &&
              somvr_pr_orc_parse("warg") == SOMVR_PRO_CARAGOR && somvr_pr_orc_parse(" Caragor ") == SOMVR_PRO_CARAGOR &&
              somvr_pr_orc_parse("goblin") == SOMVR_PRO_GHUL && somvr_pr_orc_parse("troll") == SOMVR_PRO_GRAUG &&
              somvr_pr_orc_parse("5") == SOMVR_PRO_GRAUG && somvr_pr_orc_parse("6") == -1 && SOMVR_PRO_ORCS == 3,
          "the beasts' templates and words");
    CHECK(!strcmp(somvr_pr_orc_label(SOMVR_PRO_SHIELD), "Shield") && !strcmp(somvr_pr_orc_word(SOMVR_PRO_BERSERKER),
                                                                               "berserker"),
          "labels, words");
    CHECK(somvr_pr_orc_parse("Shield") == SOMVR_PRO_SHIELD && somvr_pr_orc_parse("defender") == SOMVR_PRO_SHIELD &&
              somvr_pr_orc_parse(" inf ") == SOMVR_PRO_INFANTRY && somvr_pr_orc_parse("2") == SOMVR_PRO_BERSERKER &&
              somvr_pr_orc_parse("spider") == -1 && somvr_pr_orc_parse("") == -1 && somvr_pr_orc_parse("shield x") == -1,
          "type words");
    somvr_pr_req r = { -5, -5, 0, { 0, 0, 0 } };
    CHECK(somvr_pr_word("go", &r) && r.action == SOMVR_PRA_GO && r.orc == -1, "go");
    CHECK(somvr_pr_word("go", &r) && !r.have_at, "go: no spot");
    CHECK(somvr_pr_word("go 32500 2000 -24800", &r) && r.action == SOMVR_PRA_GO && r.have_at && r.at[0] == 32500 &&
              r.at[1] == 2000 && r.at[2] == -24800, "go X Y Z: %d %g %g %g", r.have_at, r.at[0], r.at[1], r.at[2]);
    CHECK(somvr_pr_word("  go -1.5 2e3 7  ", &r) && r.have_at && r.at[0] == -1.5f && r.at[1] == 2000 && r.at[2] == 7,
          "go X Y Z with blanks and exponents");
    CHECK(!somvr_pr_word("go 1 2", &r) && !somvr_pr_word("go 1 2 3 4", &r) && !somvr_pr_word("go 1 x 3", &r) &&
              !somvr_pr_word("go 1 2 3e9", &r) && !somvr_pr_word("go nan 2 3", &r) && !somvr_pr_word("back 1 2 3", &r),
          "go X Y Z: bad ones taken");
    CHECK(somvr_pr_word(" back\n", &r) && r.action == SOMVR_PRA_BACK, "back with blanks");
    /* build 17: the refills (the practice page's two buttons) */
    CHECK(somvr_pr_word("arrows", &r) && r.action == SOMVR_PRA_ARROWS && r.orc == -1 && !r.have_at &&
              somvr_pr_word(" refill Health ", &r) && r.action == SOMVR_PRA_HEALTH && somvr_pr_word("health", &r) &&
              r.action == SOMVR_PRA_HEALTH && somvr_pr_word("REFILL arrows", &r) && r.action == SOMVR_PRA_ARROWS &&
              !somvr_pr_word("refill", &r) && !somvr_pr_word("arrows 8", &r) && !somvr_pr_word("health full", &r) &&
              !somvr_pr_word("refill go", &r) && !strcmp(somvr_pr_action_name(SOMVR_PRA_ARROWS), "arrows") &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_HEALTH), "health") && SOMVR_PRA_N >= 8,
          "the refills' words");
    /* build 18: the far orc (the practice page's "Spawn far orc (unaware)") */
    CHECK(somvr_pr_word("far", &r) && r.action == SOMVR_PRA_FAR && r.orc == -1 && somvr_pr_word(" Far berserker ", &r) &&
              r.action == SOMVR_PRA_FAR && r.orc == SOMVR_PRO_BERSERKER && !somvr_pr_word("far spider", &r) &&
              !somvr_pr_word("far 1 2 3", &r) && !somvr_pr_word("refill far", &r) &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_FAR), "far") && SOMVR_PRA_N >= 9,
          "the far orc's words");
    CHECK(somvr_pr_word("spawn", &r) && r.action == SOMVR_PRA_SPAWN && r.orc == -1, "spawn: the current type");
    CHECK(somvr_pr_word("spawn berserker", &r) && r.action == SOMVR_PRA_SPAWN && r.orc == SOMVR_PRO_BERSERKER,
          "spawn berserker");
    CHECK(somvr_pr_word("spawn Shield", &r) && r.orc == SOMVR_PRO_SHIELD && somvr_pr_word("SPAWN infantry", &r) &&
              r.orc == SOMVR_PRO_INFANTRY,
          "spawn shield / infantry");
    CHECK(somvr_pr_word("remove", &r) && r.action == SOMVR_PRA_REMOVE && somvr_pr_word("status", &r) &&
              r.action == SOMVR_PRA_STATUS,
          "remove, status");
    CHECK(somvr_pr_word("status nav", &r) && r.action == SOMVR_PRA_STATUS && r.orc == 1 && somvr_pr_word("status", &r) &&
              r.orc == -1 && !somvr_pr_word("status now", &r) && !somvr_pr_word("go nav", &r),
          "status nav (the nav markers too)");
    CHECK(somvr_pr_word("status ground 57272 2500 -10603", &r) && r.action == SOMVR_PRA_STATUS && r.orc == 2 && r.have_at &&
              r.at[0] == 57272 && r.at[1] == 2500 && r.at[2] == -10603 && !somvr_pr_word("status ground", &r) &&
              !somvr_pr_word("status ground 1 2", &r) && !somvr_pr_word("status ground 1 2 3 4", &r) &&
              !somvr_pr_word("go ground 1 2 3", &r) && somvr_pr_word("status", &r) && !r.have_at,
          "status ground X Y Z (the floor probe)");
    CHECK(somvr_pr_word("status look", &r) && r.action == SOMVR_PRA_STATUS && r.orc == 3 && !r.have_at &&
              !somvr_pr_word("status look 1", &r), "status look");
    r.action = 77;
    CHECK(somvr_pr_word("spawn troll", &r) && r.action == SOMVR_PRA_SPAWN && r.orc == SOMVR_PRO_GRAUG,
          "spawn troll (build 22: the graug)");
    r.action = 77;
    CHECK(!somvr_pr_word("spawn spider", &r) && !somvr_pr_word("go now", &r) && !somvr_pr_word("", &r) &&
              !somvr_pr_word("fly", &r) && !somvr_pr_word("spawn shield 3", &r) && !somvr_pr_word(NULL, &r) &&
              r.action == 77,
          "not taken");
    CHECK(!strcmp(somvr_pr_action_name(SOMVR_PRA_SPAWN), "spawn") && !strcmp(somvr_pr_action_name(99), "?"), "names");
}

static const float g_spot[3] = { 40400, 3900, -21600 }, g_far[3] = { 35400, 5402, -22400 };

/* the trip at 90 Hz from t until it is idle (or n frames); pos follows `land` after a teleport (NULL: never moves) */
static int g_tp, g_arrived, g_done, g_failed;
static double run(somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t, float pos[3], const float *land, int n)
{
    for (int i = 0; i < n && tr->phase != SOMVR_PRT_IDLE; i++, t += 1.0 / 90) {
        int x = somvr_pr_trip_step(tr, c, t, pos, 1, 0);
        if (x == SOMVR_PRX_TELEPORT) {
            g_tp++;
            CHECK(somvr_pr_trip_fade(tr, c, t) == 1.0f, "a teleport at full black (%g)", somvr_pr_trip_fade(tr, c, t));
            if (land)
                memcpy(pos, land, 3 * sizeof *pos);
        }
        g_arrived += x == SOMVR_PRX_ARRIVED, g_done += x == SOMVR_PRX_DONE, g_failed += x == SOMVR_PRX_FAILED;
    }
    return t;
}

static void test_trip(void)
{
    somvr_pr_tcfg c;
    somvr_pr_tcfg_default(&c);
    CHECK(c.fade_out_s == 0.25f && c.fade_in_s == 0.35f && c.tries == 3 && c.arrive_cm == 500, "defaults");
    somvr_pr_trip tr;
    memset(&tr, 0, sizeof tr);
    float pos[3] = { 35400, 5402, -22400 };
    CHECK(somvr_pr_trip_fade(&tr, &c, 5) == 0 && somvr_pr_trip_step(&tr, &c, 5, pos, 1, 0) == SOMVR_PRX_NONE, "idle");
    /* 1. there: the fade, one teleport at black, the arrival after the settle, the hold, the fade-in */
    somvr_pr_trip_start(&tr, &c, g_spot, 10.0);
    CHECK(tr.phase == SOMVR_PRT_OUT && somvr_pr_trip_fade(&tr, &c, 10.0) == 0 &&
              NEAR(somvr_pr_trip_fade(&tr, &c, 10.125), 0.5, 1e-5),
          "fading out: %g", somvr_pr_trip_fade(&tr, &c, 10.125));
    CHECK(somvr_pr_trip_step(&tr, &c, 10.2, pos, 1, 0) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_OUT, "not black yet");
    CHECK(somvr_pr_trip_step(&tr, &c, 10.25, pos, 1, 1) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_OUT,
          "paused at black: no teleport");
    CHECK(somvr_pr_trip_step(&tr, &c, 10.3, pos, 1, 0) == SOMVR_PRX_TELEPORT && tr.phase == SOMVR_PRT_MOVE && tr.tries == 1,
          "the teleport");
    float landed[3] = { 40401, 3901, -21599 };
    memcpy(pos, landed, sizeof pos);
    CHECK(somvr_pr_trip_step(&tr, &c, 10.31, pos, 1, 0) == SOMVR_PRX_NONE && somvr_pr_trip_fade(&tr, &c, 10.31) == 1,
          "settling, black");
    CHECK(somvr_pr_trip_step(&tr, &c, 10.81, pos, 1, 0) == SOMVR_PRX_ARRIVED && tr.phase == SOMVR_PRT_HOLD && tr.arrived,
          "arrived after 0.5 s standing");
    CHECK(somvr_pr_trip_step(&tr, &c, 11.5, pos, 1, 0) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_HOLD &&
              somvr_pr_trip_fade(&tr, &c, 11.5) == 1,
          "the hold (the light under black)");
    CHECK(somvr_pr_trip_step(&tr, &c, 11.81, pos, 1, 0) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_IN, "fading in");
    CHECK(NEAR(somvr_pr_trip_fade(&tr, &c, 11.81 + 0.175), 0.5, 1e-4), "half way in: %g",
          somvr_pr_trip_fade(&tr, &c, 11.81 + 0.175));
    CHECK(somvr_pr_trip_step(&tr, &c, 12.2, pos, 1, 0) == SOMVR_PRX_DONE && tr.phase == SOMVR_PRT_IDLE &&
              somvr_pr_trip_fade(&tr, &c, 12.2) == 0,
          "done");
    /* 1b. the game loading after the arrival (SOMVR_PRACTICE_LOADHOLD): the hold stays black while busy, at most
     * hold_max_s after the arrival; a pause alone never holds it; busy before the hold changes nothing */
    CHECK(c.hold_max_s == 5.5f, "hold max %g", (double)c.hold_max_s);
    memcpy(pos, landed, sizeof pos);
    somvr_pr_trip_start(&tr, &c, g_spot, 30.0);
    CHECK(somvr_pr_trip_step_busy(&tr, &c, 30.3, pos, 1, 0, 1) == SOMVR_PRX_TELEPORT &&
              somvr_pr_trip_step_busy(&tr, &c, 30.31, pos, 1, 0, 1) == SOMVR_PRX_NONE &&
              somvr_pr_trip_step_busy(&tr, &c, 30.81, pos, 1, 0, 1) == SOMVR_PRX_ARRIVED && tr.phase == SOMVR_PRT_HOLD,
          "busy: the teleport and the arrival as before (phase %d)", tr.phase);
    CHECK(somvr_pr_trip_step_busy(&tr, &c, 31.9, pos, 1, 0, 1) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_HOLD &&
              tr.held_busy && somvr_pr_trip_fade(&tr, &c, 31.9) == 1,
          "busy: still black after hold_s (phase %d held %d)", tr.phase, tr.held_busy);
    CHECK(somvr_pr_trip_step_busy(&tr, &c, 32.5, pos, 1, 1, 0) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_IN,
          "not busy (only paused): fading in (phase %d)", tr.phase);
    somvr_pr_trip_start(&tr, &c, g_spot, 40.0);
    somvr_pr_trip_step_busy(&tr, &c, 40.3, pos, 1, 0, 0), somvr_pr_trip_step_busy(&tr, &c, 40.31, pos, 1, 0, 0);
    CHECK(somvr_pr_trip_step_busy(&tr, &c, 40.81, pos, 1, 0, 0) == SOMVR_PRX_ARRIVED &&
              somvr_pr_trip_step_busy(&tr, &c, 46.2, pos, 1, 0, 1) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_HOLD &&
              somvr_pr_trip_step_busy(&tr, &c, 46.32, pos, 1, 0, 1) == SOMVR_PRX_NONE && tr.phase == SOMVR_PRT_IN,
          "busy for ever: in at hold_max_s after the arrival (phase %d)", tr.phase);
    somvr_pr_trip_step(&tr, &c, 47.0, pos, 1, 0);
    CHECK(tr.phase == SOMVR_PRT_IDLE, "busy for ever: done (phase %d)", tr.phase);
    CHECK(somvr_pr_is_wet("SON_Day03_Rain") && somvr_pr_is_wet("Storm_02") && !somvr_pr_is_wet("SON_Day03") &&
              !somvr_pr_is_wet("DayNoRain") && !somvr_pr_is_wet("") && !somvr_pr_is_wet(NULL),
          "wet weather names");
    /* build 36 (som-b36-winlooks): SOMVR_PRACTICE_SKY - the overcast day changed only with clear, in the target's region */
    CHECK(somvr_pr_is_overcast("SON_Day03") && somvr_pr_is_overcast("son_day03") && somvr_pr_is_overcast("Overcast_A") &&
              somvr_pr_is_overcast("Fog01") && !somvr_pr_is_overcast("SON_Day03_Rain") && !somvr_pr_is_overcast("SON_Day01") &&
              !somvr_pr_is_overcast("SON_Day02") && !somvr_pr_is_overcast("") && !somvr_pr_is_overcast(NULL),
          "overcast weather names");
    CHECK(!somvr_pr_sky_change("SON_Day03", SOMVR_PRSKY_DRY, "SON_Day02") &&
              somvr_pr_sky_change("SON_Day03", SOMVR_PRSKY_CLEAR, "TEST_Day_02") &&
              somvr_pr_sky_change("son_day03", SOMVR_PRSKY_CLEAR, "DayNoRain") &&
              !somvr_pr_sky_change("UDN_Day03", SOMVR_PRSKY_CLEAR, "TEST_Day_02") &&
              !somvr_pr_sky_change("Fog01", SOMVR_PRSKY_CLEAR, "TEST_Day_02") &&
              !somvr_pr_sky_change("SON_", SOMVR_PRSKY_CLEAR, "TEST_Day_02") &&
              !somvr_pr_sky_change("SON_Day01", SOMVR_PRSKY_CLEAR, "SON_Day02") &&
              !somvr_pr_sky_change("SON_Day02", SOMVR_PRSKY_CLEAR, "SON_Day02") &&
              somvr_pr_sky_change("SON_Day03_Rain", SOMVR_PRSKY_DRY, "SON_Day02") &&
              somvr_pr_sky_change("SON_Day03_Rain", SOMVR_PRSKY_CLEAR, "DayNoRain") &&
              !somvr_pr_sky_change("son_day02", SOMVR_PRSKY_ALWAYS, "SON_Day02") &&
              somvr_pr_sky_change("SON_Day01", SOMVR_PRSKY_ALWAYS, "SON_Day03") &&
              !somvr_pr_sky_change("", SOMVR_PRSKY_ALWAYS, "SON_Day03") &&
              !somvr_pr_sky_change(NULL, SOMVR_PRSKY_ALWAYS, "SON_Day03") &&
              !somvr_pr_sky_change("SON_Day03", SOMVR_PRSKY_CLEAR, ""),
          "the sky's change by SOMVR_PRACTICE_SKY");
    /* 2. the settle is broken by a step away: it starts again */
    g_tp = g_arrived = g_done = g_failed = 0;
    memcpy(pos, g_far, sizeof pos);
    somvr_pr_trip_start(&tr, &c, g_spot, 20.0);
    double t = run(&tr, &c, 20.0, pos, g_spot, 1000);
    CHECK(g_tp == 1 && g_arrived == 1 && g_done == 1 && !g_failed && tr.phase == SOMVR_PRT_IDLE && t < 22.5,
          "a whole trip at 90 Hz: %d teleports, %.2f s", g_tp, t - 20.0);
    /* 3. never arrives: teleports at 3 s apart, three in all, then the give-up and the fade-in where he is */
    g_tp = g_arrived = g_done = g_failed = 0;
    memcpy(pos, g_far, sizeof pos);
    somvr_pr_trip_start(&tr, &c, g_spot, 30.0);
    t = run(&tr, &c, 30.0, pos, NULL, 2000);
    CHECK(g_tp == 3 && g_failed == 1 && !g_arrived && !g_done && tr.failed && tr.phase == SOMVR_PRT_IDLE &&
              NEAR(t - 30.0, 0.25 + 9.0 + 0.35, 0.05),
          "no arrival: %d teleports, failed %d, %.2f s", g_tp, g_failed, t - 30.0);
    /* 4. a fall below the spot (the ground not there yet): again, at most every 0.5 s */
    float below[3] = { 40400, 3000, -21600 };
    g_tp = g_arrived = g_done = g_failed = 0;
    memcpy(pos, g_far, sizeof pos);
    somvr_pr_trip_start(&tr, &c, g_spot, 40.0);
    CHECK(somvr_pr_trip_step(&tr, &c, 40.3, pos, 1, 0) == SOMVR_PRX_TELEPORT, "4: teleport");
    memcpy(pos, below, sizeof pos);
    CHECK(somvr_pr_trip_step(&tr, &c, 40.5, pos, 1, 0) == SOMVR_PRX_NONE, "4: a fall, but not 0.5 s yet");
    CHECK(somvr_pr_trip_step(&tr, &c, 40.81, pos, 1, 0) == SOMVR_PRX_TELEPORT && tr.tries == 2, "4: again");
    CHECK(somvr_pr_trip_step(&tr, &c, 40.9, pos, 0, 0) == SOMVR_PRX_NONE, "4: no position: nothing");
    memcpy(pos, g_spot, sizeof pos);
    CHECK(somvr_pr_trip_step(&tr, &c, 41.0, pos, 1, 0) == SOMVR_PRX_NONE && somvr_pr_trip_step(&tr, &c, 41.5, pos, 1, 0) ==
                                                                                 SOMVR_PRX_ARRIVED,
          "4: arrived on the second");
    /* 5. a trip started while another fades in goes on from its black (no flash); one in the black stays black */
    tr.phase = SOMVR_PRT_IN, tr.t0 = 50.0, tr.fade_from = 1;
    float f0 = somvr_pr_trip_fade(&tr, &c, 50.1);
    somvr_pr_trip_start(&tr, &c, g_far, 50.1);
    CHECK(NEAR(somvr_pr_trip_fade(&tr, &c, 50.1), f0, 1e-4) && tr.phase == SOMVR_PRT_OUT, "5: on from %g (%g)", f0,
          somvr_pr_trip_fade(&tr, &c, 50.1));
    tr.phase = SOMVR_PRT_HOLD;
    somvr_pr_trip_start(&tr, &c, g_spot, 60.0);
    CHECK(somvr_pr_trip_fade(&tr, &c, 60.0) == 1.0f && somvr_pr_trip_step(&tr, &c, 60.0, pos, 1, 0) == SOMVR_PRX_TELEPORT,
          "5: from the black: the teleport at once");
    /* 6. a paused game in the move phase: no new teleport meanwhile */
    memcpy(pos, g_far, sizeof pos);
    CHECK(somvr_pr_trip_step(&tr, &c, 70.0, pos, 1, 1) == SOMVR_PRX_NONE && tr.tries == 1, "6: paused past the timeout");
    CHECK(somvr_pr_trip_step(&tr, &c, 70.1, pos, 1, 0) == SOMVR_PRX_TELEPORT && tr.tries == 2, "6: unpaused: again");
    CHECK(somvr_pr_at(g_spot, g_spot, 500, 300) && !somvr_pr_at(below, g_spot, 500, 300) && !somvr_pr_at(g_far, g_spot, 500, 300),
          "at");
    CHECK(!strcmp(somvr_pr_phase_name(SOMVR_PRT_HOLD), "holding") && !strcmp(somvr_pr_phase_name(9), "?"), "phase names");
}

static void test_clear(void)
{
    somvr_pr_cand c = { 1000, 0, 0, 0, 0, 1, 0 };
    CHECK(somvr_pr_clear_pick(&c, SOMVR_PRC_ARMED, 6000) && somvr_pr_clear_pick(&c, SOMVR_PRC_ALL, 6000) &&
              !somvr_pr_clear_pick(&c, SOMVR_PRC_OFF, 6000),
          "an orc at 10 m");
    c.dist_cm = 6001;
    CHECK(!somvr_pr_clear_pick(&c, SOMVR_PRC_ARMED, 6000), "past 60 m");
    c.dist_cm = NAN;
    CHECK(!somvr_pr_clear_pick(&c, SOMVR_PRC_ALL, 6000), "NaN");
    c.dist_cm = 100;
    somvr_pr_cand o = c, p = c, m = c, d = c, n = c, h = c;
    o.ours = 1, p.player = 1, m.mount = 1, d.dead = 1, n.npc = 0, h.harmless = 1;
    CHECK(!somvr_pr_clear_pick(&o, SOMVR_PRC_ALL, 6000) && !somvr_pr_clear_pick(&p, SOMVR_PRC_ALL, 6000) &&
              !somvr_pr_clear_pick(&m, SOMVR_PRC_ALL, 6000) && !somvr_pr_clear_pick(&d, SOMVR_PRC_ALL, 6000) &&
              !somvr_pr_clear_pick(&n, SOMVR_PRC_ALL, 6000),
          "never ours, the player, his mount, the dead, a non-NPC");
    CHECK(!somvr_pr_clear_pick(&h, SOMVR_PRC_ARMED, 6000) && somvr_pr_clear_pick(&h, SOMVR_PRC_ALL, 6000),
          "a slave: kept unless all");
    CHECK(somvr_pr_harmless("AI_Slave_Human") && somvr_pr_harmless("human_civilian") && somvr_pr_harmless("PRISONER") &&
              !somvr_pr_harmless("AI_Infantry") && !somvr_pr_harmless("Warg_Brown") && !somvr_pr_harmless("AI_Defender") &&
              !somvr_pr_harmless("") && !somvr_pr_harmless(NULL),
          "harmless names");
    char b[160];
    somvr_pr_gate_text(0, b, sizeof b);
    CHECK(!strcmp(b, "nothing"), "gate 0: '%s'", b);
    somvr_pr_gate_text(SOMVR_PRG_PRES | SOMVR_PRG_PAUSED, b, sizeof b);
    CHECK(!strcmp(b, "a presentation+the game paused"), "gate: '%s'", b);
    somvr_pr_gate_text(0x7f, b, 12);
    CHECK(strlen(b) < 12, "a short buffer: '%s'", b);
}

static void test_spawn(void)
{
    float pos[3] = { 500, 100, -50 }, f[3] = { 0, 0, 1 }, at[3], q[4];
    somvr_pr_ahead(pos, f, 350, at);
    CHECK(NEAR(at[0], 500, 1e-3) && NEAR(at[1], 100, 1e-3) && NEAR(at[2], 300, 1e-3), "+Z: %g %g %g", at[0], at[1], at[2]);
    float f2[3] = { 3, 7, 4 }; /* the vertical part ignored */
    somvr_pr_ahead(pos, f2, 350, at);
    CHECK(NEAR(at[0], 500 + 210, 1e-2) && NEAR(at[2], -50 + 280, 1e-2) && at[1] == 100, "3-4-5: %g %g", at[0], at[2]);
    float up[3] = { 0, 1, 0 };
    somvr_pr_ahead(pos, up, 350, at);
    CHECK(NEAR(at[2], 300, 1e-3), "straight up: +Z");
    /* the orc 3.5 m ahead (+Z) faces Talion (-Z): its forward (+Z rotated by q) points at him */
    float o[3] = { 500, 100, 300 };
    somvr_pr_facing(o, pos, q);
    float fx = 2 * (q[0] * q[2] + q[3] * q[1]), fz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    CHECK(NEAR(fx, 0, 1e-5) && NEAR(fz, -1, 1e-5) && NEAR(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1, 1e-6),
          "facing Talion: %g %g", fx, fz);
    float o2[3] = { 150, 100, -50 }; /* west of him: faces +X */
    somvr_pr_facing(o2, pos, q);
    fx = 2 * (q[0] * q[2] + q[3] * q[1]), fz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    CHECK(NEAR(fx, 1, 1e-5) && NEAR(fz, 0, 1e-5), "facing +X: %g %g", fx, fz);
    somvr_pr_facing(pos, pos, q);
    CHECK(q[3] == 1 && q[1] == 0, "the same point: no turn");
    CHECK(somvr_pr_is_night("Night") && somvr_pr_is_night("SON_Night_01") && !somvr_pr_is_night("Day") &&
              !somvr_pr_is_night("") && !somvr_pr_is_night(NULL),
          "night");
}


/* build 19: the group - the words, the plan (mixed types, 4-6 m, apart, around), the spots */
static void test_group(void)
{
    somvr_pr_req r;
    CHECK(somvr_pr_word("group", &r) && r.action == SOMVR_PRA_GROUP && r.orc == -1 && somvr_pr_word("group 3", &r) &&
              r.orc == 3 && somvr_pr_word("  Spawn Group 4 ", &r) && r.action == SOMVR_PRA_GROUP && r.orc == 4 &&
              somvr_pr_word("crowd", &r) && r.action == SOMVR_PRA_GROUP && somvr_pr_word("group 8", &r) && r.orc == 8 && !somvr_pr_word("group 9", &r) &&
              !somvr_pr_word("group 2", &r) && !somvr_pr_word("group 34", &r) && !somvr_pr_word("group 3 x", &r) &&
              !somvr_pr_word("refill group", &r) && somvr_pr_word("spawn shield", &r) && r.action == SOMVR_PRA_SPAWN &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_GROUP), "group") && !strcmp(somvr_pr_action_name(8), "far") &&
              SOMVR_PRA_GROUP == 9 && SOMVR_PRA_N == 16 && somvr_pr_word("herb", &r) && r.action == SOMVR_PRA_HERB &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_HERB), "herb") && somvr_pr_word("fintest", &r) &&
              r.action == SOMVR_PRA_FINTEST && somvr_pr_word("Finisher", &r) && r.action == SOMVR_PRA_FINTEST &&
              SOMVR_PRA_FINTEST == 14 && !strcmp(somvr_pr_action_name(SOMVR_PRA_FINTEST), "fintest") && /* build 40 */
              somvr_pr_word("fintype", &r) && r.action == SOMVR_PRA_FINTYPE && SOMVR_PRA_FINTYPE == 15 &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_FINTYPE), "fintype"), /* build 41 */
          "the group's words");
    /* build 22: the Captain */
    CHECK(somvr_pr_word(" Captain ", &r) && r.action == SOMVR_PRA_CAPTAIN && r.orc == -1 && !r.have_at &&
              !somvr_pr_word("captain shield", &r) && !somvr_pr_word("captain 3", &r) && SOMVR_PRA_CAPTAIN == 10 &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_CAPTAIN), "captain") && !strcmp(somvr_pr_action_name(16), "?"),
          "the captain's word");
    /* build 31: "place [TYPE]" (the page's Place orc) */
    CHECK(somvr_pr_word("place", &r) && r.action == SOMVR_PRA_PLACE && r.orc == -1 && somvr_pr_word("place berserker", &r) &&
              r.action == SOMVR_PRA_PLACE && r.orc >= 0 && !somvr_pr_word("place far", &r) && SOMVR_PRA_PLACE == 11 &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_PLACE), "place"),
          "the place word");
    /* build 36: "charge" (the page's Charge execution) */
    CHECK(somvr_pr_word("charge", &r) && r.action == SOMVR_PRA_CHARGE && SOMVR_PRA_CHARGE == 12 &&
              !strcmp(somvr_pr_action_name(SOMVR_PRA_CHARGE), "charge") && !somvr_pr_word("refill charge", &r),
          "the charge word");
    for (int n = 3; n <= 8; n++) { /* build 27: to 8 */
        somvr_pr_group_orc g[SOMVR_PR_GROUP_MAX];
        int k = somvr_pr_group_plan(n, g), types = 0, front = 0, behind = 0, apart = 1, far_ok = 1;
        float pos[3] = {32500, 2100, -24800}, fwd[3] = {0.6f, 0.f, 0.8f}, sp[SOMVR_PR_GROUP_MAX][3];
        for (int i = 0; i < k; i++) {
            types |= 1 << g[i].type;
            front += fabsf(g[i].yaw_deg) <= 55.f, behind += fabsf(g[i].yaw_deg) > 90.f;
            far_ok &= g[i].dist_cm >= 400.f && g[i].dist_cm <= 650.f;
            somvr_pr_group_spot(pos, fwd, g[i].yaw_deg, g[i].dist_cm, sp[i]);
            float dx = sp[i][0] - pos[0], dz = sp[i][2] - pos[2];
            far_ok &= fabsf(sqrtf(dx * dx + dz * dz) - g[i].dist_cm) < 0.5f && sp[i][1] == pos[1];
            /* its bearing off the heading as planned (+ = right: +X of +Z) */
            float b = atan2f(dx * fwd[2] - dz * fwd[0], dx * fwd[0] + dz * fwd[2]) * 57.29578f;
            far_ok &= fabsf(b - g[i].yaw_deg) < 0.1f || fabsf(fabsf(b - g[i].yaw_deg) - 360.f) < 0.1f;
            for (int j = 0; j < i; j++) {
                float ex = sp[i][0] - sp[j][0], ez = sp[i][2] - sp[j][2];
                apart &= sqrtf(ex * ex + ez * ez) >= 250.f;
            }
        }
        int nf = n == 8 ? 3 : 2; /* the 8th: a berserker straight ahead, farther out */
        CHECK(k == n && types == 7 && front == nf && behind == n - nf && far_ok && apart,
              "the group of %d: %d orcs, types %#x, %d in front, %d behind, 4-6.5 m, apart", n, k, types, front, behind);
    }
    somvr_pr_group_orc g[SOMVR_PR_GROUP_MAX];
    CHECK(somvr_pr_group_plan(9, g) == 8 && somvr_pr_group_plan(0, g) == 3, "the count clamped");
    float pos[3] = {0, 0, 0}, z[3] = {0, 0, 0}, o[3];
    somvr_pr_group_spot(pos, z, 90.f, 500.f, o);
    CHECK(fabsf(o[0] - 500.f) < 0.01f && fabsf(o[2]) < 0.01f, "no heading: +Z; +90 deg = +X (%.1f %.1f)", o[0], o[2]);
}

/* build 37 (som-b37-practrip, SOMVR_PRACTICE_GROUND): the trip with the world's collision streaming in under the target.
 * A 90 Hz world: gravity 2000 cm/s^2 (win-session-20261104-160647: 331 cm in 0.55 s from rest), the ground at gy from
 * ground_t on (NaN: never; gone between gone0 and gone1), Talion's root 103 cm over it when he stands; the ray's view
 * as pr_trip_ground: nothing under root + 40 = NONE, the root within 250 cm over it = ON, else OVER. rules = 0: the
 * rules before build 37 (somvr_pr_trip_step_busy, the ground not passed) */
struct gsim {
    double ground_t, gone0, gone1;
    float gy, v, low;      /* low: the lowest root while the ground was there and he was under its top (a fall through) */
    int tp, pins, arrived, done, failed, fade_in_air, below_ground;
    double arrived_t, done_t, failed_t;
    float pos_at_fade[3];
};
static int gs_present(const struct gsim *g, double t)
{
    return !isnan(g->ground_t) && t >= g->ground_t && !(t >= g->gone0 && t < g->gone1);
}
static int gs_ray(const struct gsim *g, double t, const float pos[3])
{
    if (!gs_present(g, t) || pos[1] + 40.0f < g->gy)
        return SOMVR_PRGD_NONE;
    return pos[1] - g->gy <= 250.0f ? SOMVR_PRGD_ON : SOMVR_PRGD_OVER;
}
static double gs_run(struct gsim *g, somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t, float pos[3], int rules, int n)
{
    const double dt = 1.0 / 90;
    int ph0 = tr->phase;
    for (int i = 0; i < n && tr->phase != SOMVR_PRT_IDLE; i++, t += dt) {
        /* the game's physics this frame */
        if (gs_present(g, t) && pos[1] >= g->gy + 103.0f - 1.0f && pos[1] - g->v * (float)dt <= g->gy + 103.0f) {
            pos[1] = g->gy + 103.0f, g->v = 0; /* lands / stands */
        } else if (gs_present(g, t) && pos[1] < g->gy + 103.0f - 1.0f && pos[1] + 40.0f >= g->gy) {
            pos[1] = g->gy + 103.0f, g->v = 0; /* a capsule partly in the ground: pushed up on it */
        } else {
            g->v += 2000.0f * (float)dt, pos[1] -= g->v * (float)dt;
        }
        if (gs_present(g, t) && pos[1] + 40.0f < g->gy)
            g->below_ground = 1;
        int gnd = gs_ray(g, t, pos);
        int x = rules ? somvr_pr_trip_step_ground(tr, c, t, pos, 1, 0, 0, gnd) : somvr_pr_trip_step_busy(tr, c, t, pos, 1, 0, 0);
        if (x == SOMVR_PRX_TELEPORT) {
            g->tp++;
            CHECK(somvr_pr_trip_fade(tr, c, t) == 1.0f, "ground: a teleport at full black (%g)", somvr_pr_trip_fade(tr, c, t));
            memcpy(pos, tr->target, 3 * sizeof *pos), g->v = 0;
        }
        if (ph0 != SOMVR_PRT_IN && tr->phase == SOMVR_PRT_IN) { /* the fade-in starts: where is he? */
            memcpy(g->pos_at_fade, pos, sizeof g->pos_at_fade);
            g->fade_in_air = gs_ray(g, t, pos) != SOMVR_PRGD_ON;
        }
        ph0 = tr->phase;
        if (x == SOMVR_PRX_ARRIVED)
            g->arrived++, g->arrived_t = t;
        if (x == SOMVR_PRX_DONE)
            g->done++, g->done_t = t;
        if (x == SOMVR_PRX_FAILED)
            g->failed++, g->failed_t = t;
    }
    return t;
}

static void test_trip_ground(void)
{
    static const float spot[3] = { 32500, 2100, -24800 }, start[3] = { -3501, 2712, 4001 };
    const float gy = 1775.0f; /* the meadow's ground: the root stands at 1878 (every arrival there in the logs) */
    somvr_pr_tcfg c;
    somvr_pr_tcfg_default(&c);
    CHECK(c.ground_wait_s == 5.0f && c.pin_cm == 50.0f && c.need_on == 1, "ground: the defaults");
    somvr_pr_trip tr;
    float pos[3];
    struct gsim g;
    /* 1. the bug (win-session-20261104-160647): the ground streams in 4 s after the first teleport. The rules before
     * build 37: three teleports in ~1.2 s, the give-up, the fade-in with no ground under him - and through the world */
    memset(&g, 0, sizeof g), g.ground_t = 10.25 + 4.0, g.gone0 = g.gone1 = 1e9, g.gy = gy;
    memcpy(pos, start, sizeof pos);
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 10.0);
    gs_run(&g, &tr, &c, 10.0, pos, 0, 3000);
    CHECK(g.failed + g.arrived == 1 && g.fade_in_air && pos[1] < gy - 1000.0f,
          "ground 1 (the rules before): failed %d / arrived %d in the air after %d teleports, faded in over nothing %d, the "
          "root %.0f", g.failed, g.arrived, g.tp, g.fade_in_air, pos[1]);
    /* ... build 37: the black holds, pins keep him at the target, the arrival on the ground after it came */
    memset(&g, 0, sizeof g), g.ground_t = 10.25 + 4.0, g.gone0 = g.gone1 = 1e9, g.gy = gy;
    memcpy(pos, start, sizeof pos);
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 10.0);
    gs_run(&g, &tr, &c, 10.0, pos, 1, 3000);
    CHECK(!g.failed && g.arrived == 1 && g.done == 1 && !g.fade_in_air && !g.below_ground && tr.tries == 1 && tr.pins >= 8 &&
              g.arrived_t >= g.ground_t + c.settle_s && g.arrived_t < g.ground_t + 1.5 && NEAR(pos[1], gy + 103.0f, 1.0) &&
              tr.ground_t >= g.ground_t && tr.ground_t < g.ground_t + 0.1,
          "ground 1 (build 37): failed %d arrived %d (+%.2f s after the ground) done %d, faded in on the ground %d, under "
          "it %d, tries %d pins %d, the root %.1f, the ground seen at +%.2f",
          g.failed, g.arrived, g.arrived_t - g.ground_t, g.done, !g.fade_in_air, g.below_ground, tr.tries, tr.pins, pos[1],
          tr.ground_t - g.ground_t);
    /* 2. the ground there at once (the trips from 38 m: every arrival in the logs): one teleport, no pin, as quick */
    memset(&g, 0, sizeof g), g.ground_t = 0, g.gone0 = g.gone1 = 1e9, g.gy = gy;
    memcpy(pos, start, sizeof pos);
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 20.0);
    double t = gs_run(&g, &tr, &c, 20.0, pos, 1, 3000);
    CHECK(g.arrived == 1 && g.done == 1 && !g.failed && g.tp == 1 && !tr.pins && !g.fade_in_air && t - 20.0 < 0.25 + 1.2 + 1.0 + 0.35 + 0.1,
          "ground 2: arrived %d done %d, %d teleports, %d pins, %.2f s", g.arrived, g.done, g.tp, tr.pins, t - 20.0);
    /* 3. the ground never comes: the black for ground_wait_s, then FAILED with noground (the game side goes back) */
    memset(&g, 0, sizeof g), g.ground_t = NAN, g.gone0 = g.gone1 = 1e9, g.gy = gy;
    memcpy(pos, start, sizeof pos);
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 30.0);
    gs_run(&g, &tr, &c, 30.0, pos, 1, 90 * 40);
    CHECK(g.failed == 1 && tr.noground && !g.arrived && NEAR(g.failed_t - 30.25, c.ground_wait_s, 0.05) && tr.tries == 1 &&
              tr.pins > 10 && g.pos_at_fade[1] > spot[1] - 100.0f,
          "ground 3: failed %d noground %d at +%.2f s, tries %d pins %d, the root %.0f at the give-up (kept near the "
          "target)", g.failed, tr.noground, g.failed_t - 30.25, tr.tries, tr.pins, g.pos_at_fade[1]);
    /* 4. the ground goes in the hold (after the arrival): no fade-in until it is back, he is kept at the target */
    memset(&g, 0, sizeof g), g.ground_t = 0, g.gone0 = 40.25 + 1.5, g.gone1 = 40.25 + 4.5, g.gy = gy;
    memcpy(pos, start, sizeof pos);
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 40.0);
    gs_run(&g, &tr, &c, 40.0, pos, 1, 3000);
    CHECK(g.arrived == 1 && g.done == 1 && !g.failed && !g.fade_in_air && !g.below_ground && tr.pins >= 4 &&
              g.done_t > g.gone1 && NEAR(pos[1], gy + 103.0f, 1.0),
          "ground 4: arrived %d done %d at +%.2f s (the ground back at +%.2f), pins %d, in the air at the fade %d",
          g.arrived, g.done, g.done_t - 40.0, g.gone1 - 40.0, tr.pins, g.fade_in_air);
    /* 5. a target 10 m over the ground (a mid-air place): he lands, the tries run out on the ground, the fade-in there */
    static const float high[3] = { 32500, 2100 + 1000, -24800 };
    memset(&g, 0, sizeof g), g.ground_t = 0, g.gone0 = g.gone1 = 1e9, g.gy = gy;
    memcpy(pos, start, sizeof pos);
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, high, 50.0);
    t = gs_run(&g, &tr, &c, 50.0, pos, 1, 90 * 40);
    CHECK(g.failed == 1 && !tr.noground && g.tp == 3 && !g.fade_in_air && tr.phase == SOMVR_PRT_IDLE && t - 50.0 < 12.0,
          "ground 5: failed %d noground %d, %d teleports, in the air at the fade %d, %.2f s", g.failed, tr.noground, g.tp,
          g.fade_in_air, t - 50.0);
    /* 6. the climb's blinks (need_on 0): ground under him is enough (OVER: a ledge the centre's ray passes) */
    somvr_pr_tcfg q = c;
    q.need_on = 0, q.settle_s = 0.1f, q.hold_s = 0;
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &q, spot, 60.0);
    pos[0] = spot[0], pos[1] = spot[1], pos[2] = spot[2];
    CHECK(somvr_pr_trip_step_ground(&tr, &q, 60.3, pos, 1, 0, 0, SOMVR_PRGD_UNKNOWN) == SOMVR_PRX_TELEPORT &&
              somvr_pr_trip_step_ground(&tr, &q, 60.31, pos, 1, 0, 0, SOMVR_PRGD_OVER) == SOMVR_PRX_NONE &&
              somvr_pr_trip_step_ground(&tr, &q, 60.42, pos, 1, 0, 0, SOMVR_PRGD_OVER) == SOMVR_PRX_ARRIVED,
          "ground 6: the climb's blink arrives over ground");
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 70.0);
    CHECK(somvr_pr_trip_step_ground(&tr, &c, 70.3, pos, 1, 0, 0, SOMVR_PRGD_UNKNOWN) == SOMVR_PRX_TELEPORT &&
              somvr_pr_trip_step_ground(&tr, &c, 70.31, pos, 1, 0, 0, SOMVR_PRGD_OVER) == SOMVR_PRX_NONE &&
              somvr_pr_trip_step_ground(&tr, &c, 70.9, pos, 1, 0, 0, SOMVR_PRGD_OVER) == SOMVR_PRX_NONE &&
              tr.phase == SOMVR_PRT_MOVE,
          "ground 6: a trip there does not arrive in the air over ground (phase %d)", tr.phase);
    /* 7. no position (have 0) while no ground: nothing (no pin from an unknown height) */
    memset(&tr, 0, sizeof tr);
    somvr_pr_trip_start(&tr, &c, spot, 80.0);
    CHECK(somvr_pr_trip_step_ground(&tr, &c, 80.3, pos, 1, 0, 0, SOMVR_PRGD_UNKNOWN) == SOMVR_PRX_TELEPORT &&
              somvr_pr_trip_step_ground(&tr, &c, 80.9, pos, 0, 0, 0, SOMVR_PRGD_UNKNOWN) == SOMVR_PRX_NONE && !tr.pins,
          "ground 7: no position, no pin");
}

int main(void)
{
    test_words();
    test_trip();
    test_trip_ground();
    test_clear();
    test_spawn();
    test_group();
    printf("test_practice: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
