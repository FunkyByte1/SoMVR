/* test_vision.c - SOMVR_VISION (docs/re-xr-pipeline.md 49): somvr_vision.c against a simulated game toggle - Y held and
 * let go, the quick tap (the off waits for the on), a press with the vision on (ours, the game's own), the game
 * turning it on by itself, a refusing game (retries, then given up), the flag unread (build 15b's blind taps), tap mode,
 * ticks outside the world; and the whole chain of build 15b's session (run 233334): the raw Y through the bug mark's
 * chord (somvr_session.c) and the controls (somvr_ctl.c) into the vision - a press shorter than the chord's window
 * reaches the game as a one-tick press, the release that left the vision on. 85 input ticks a second. */
#include "../somvr_ctl.h"
#include "../somvr_session.h"
#include "../somvr_vision.h"

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

#define DT (1.0 / 85)

/* the game's toggle as runs 233334 and 002021 showed it: the command's press (its rising edge) turns the flag lat_s
 * later; the vision stays on at least dwell_s (an off press before is dropped) and off at least dwell_s (an on press
 * before is held back: it lands dwell_s after the off); refuse_all: every press dropped (a stagger) */
typedef struct {
    int flag, cmd_last, pending, refuse_all, presses, toggles, refused, queued;
    double lat_s, dwell_s, due, changed;
} game;
static void game_init(game *g) {
    memset(g, 0, sizeof *g);
    g->lat_s = 0.05, g->dwell_s = 1.0, g->changed = -10;
}
static void game_tick(game *g, int cmd, double t) {
    if (g->pending && t >= g->due) g->pending = 0, g->flag = !g->flag, g->changed = t, g->toggles++;
    if (cmd && !g->cmd_last) {
        g->presses++;
        if (g->refuse_all || g->pending || (g->flag && t - g->changed < g->dwell_s))
            g->refused++;
        else if (!g->flag && t - g->changed < g->dwell_s)
            g->queued++, g->pending = 1, g->due = g->changed + g->dwell_s;
        else
            g->pending = 1, g->due = t + g->lat_s;
    }
    g->cmd_last = cmd;
}

typedef struct {
    int taps, ticks_cmd, ev_or, giveups, lands, games, dones, press_off, release, blind;
    double first_tap, last_tap;
} tally;
/* sec seconds of ticks from *t with Y = y: the vision and the game */
static void run(somvr_vis *v, const somvr_vis_cfg *c, game *g, double *t, int y, double sec, int world, tally *k) {
    for (double end = *t + sec; *t < end - 1e-9; *t += DT) {
        somvr_vis_out o;
        somvr_vis_tick(v, c, world, y, g ? g->flag : -1, *t, &o);
        if (g) game_tick(g, o.tap, *t);
        if (!k) continue;
        k->ticks_cmd += o.tap, k->ev_or |= (int)o.ev;
        if (o.ev & SOMVR_VIE_TAP) {
            if (!k->taps) k->first_tap = *t;
            k->taps++, k->last_tap = *t;
        }
        k->giveups += !!(o.ev & SOMVR_VIE_GIVEUP), k->lands += !!(o.ev & SOMVR_VIE_LANDED);
        k->games += !!(o.ev & SOMVR_VIE_GAME), k->dones += !!(o.ev & SOMVR_VIE_DONE);
        k->press_off += !!(o.ev & SOMVR_VIE_PRESS_OFF), k->release += !!(o.ev & SOMVR_VIE_RELEASE);
        k->blind += !!(o.ev & SOMVR_VIE_BLIND);
    }
}

static void setup(somvr_vis *v, somvr_vis_cfg *c, game *g, int mode) {
    somvr_vis_cfg_default(c);
    somvr_vis_init(v, mode);
    game_init(g);
}

static void test_hold(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 100;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    run(&v, &c, &g, &t, 0, 0.5, 1, &k);
    CHECK(!k.taps && !g.flag, "idle: no taps (%d)", k.taps);
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 2.0, 1, &k); /* Y held two seconds */
    CHECK(k.taps == 1 && k.ticks_cmd == 2 && g.flag == 1 && v.ours && k.lands == 1 && k.dones == 1,
          "hold: one tap of 2 ticks, on, ours (taps %d ticks %d flag %d ours %d lands %d dones %d)", k.taps, k.ticks_cmd,
          g.flag, v.ours, k.lands, k.dones);
    memset(&k, 0, sizeof k);
    double t_rel = t;
    run(&v, &c, &g, &t, 0, 1.0, 1, &k); /* let go */
    CHECK(k.release == 1 && k.taps == 1 && g.flag == 0 && !v.ours && v.want == -1 && k.first_tap - t_rel < 0.02,
          "let go: off at once (release %d taps %d flag %d ours %d want %d, %.0f ms)", k.release, k.taps, g.flag, v.ours,
          v.want, (k.first_tap - t_rel) * 1e3);
    CHECK(g.refused == 0 && g.queued == 0 && v.n_press == 1, "nothing refused (%d) or held back (%d), presses %llu",
          g.refused, g.queued, (unsigned long long)v.n_press);
    /* held half a second: the off waits for the game's dwell (1.05 s after the on), then lands */
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 0, 1.2, 1, NULL);
    double t_on = t;
    run(&v, &c, &g, &t, 1, 0.5, 1, &k);
    run(&v, &c, &g, &t, 0, 1.5, 1, &k);
    CHECK(k.taps == 2 && g.flag == 0 && g.refused == 0 && g.queued == 0 && k.last_tap - t_on > 1.05 &&
              k.last_tap - t_on < 1.2, "held 0.5 s: on, the off at %.0f ms after the press (taps %d refused %d queued %d)",
          (k.last_tap - t_on) * 1e3, k.taps, g.refused, g.queued);
}

/* a press of one tick (the chord's tap of a short press) and a few: the on lands first, then the off */
static void test_quick_tap(void) {
    for (int n = 1; n <= 12; n += 11) {
        somvr_vis v;
        somvr_vis_cfg c;
        game g;
        tally k = {0};
        double t = 200;
        setup(&v, &c, &g, SOMVR_VIS_HOLD);
        run(&v, &c, &g, &t, 0, 0.2, 1, NULL);
        run(&v, &c, &g, &t, 1, n * DT, 1, &k);
        double t_rel = t;
        run(&v, &c, &g, &t, 0, 2.0, 1, &k);
        CHECK(g.flag == 0 && !v.ours && v.want == -1 && g.toggles == 2 && k.taps == 2 && !k.giveups,
              "quick tap (%d ticks): on then off (flag %d ours %d want %d toggles %d taps %d refused %d giveups %d)", n,
              g.flag, v.ours, v.want, g.toggles, k.taps, g.refused, k.giveups);
        CHECK(k.last_tap - t_rel < 1.25, "quick tap (%d ticks): the off within 1.25 s of the release (%.0f ms)", n,
              (k.last_tap - t_rel) * 1e3);
        CHECK(g.refused == 0 && g.queued == 0, "quick tap (%d ticks): the off waited for the on and its dwell (refused %d, "
              "held back %d)", n, g.refused, g.queued);
    }
    /* a game with a longer dwell than ours: the off is dropped once or twice, tapped again */
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 250;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    g.dwell_s = 1.5;
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    run(&v, &c, &g, &t, 0, 3.0, 1, &k);
    CHECK(g.flag == 0 && g.refused >= 1 && k.taps >= 3 && !k.giveups && !v.ours,
          "a slow game: off after a retry (flag %d refused %d taps %d giveups %d)", g.flag, g.refused, k.taps, k.giveups);
}

/* the vision on (ours with Y held, or the game's): a press turns it off */
static void test_press_while_on(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 300;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    g.flag = 1; /* on before we look (a load into wraith vision) */
    run(&v, &c, &g, &t, 0, 0.5, 1, &k);
    CHECK(!k.taps && g.flag == 1 && !v.ours, "the game's vision left alone (taps %d)", k.taps);
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 0.6, 1, &k); /* press and hold */
    CHECK(k.press_off == 1 && k.taps == 1 && g.flag == 0 && !v.ours, "a press while on: off (press_off %d taps %d flag %d)",
          k.press_off, k.taps, g.flag);
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 0, 1.0, 1, &k);
    CHECK(!k.taps && g.flag == 0, "its release: nothing (taps %d flag %d)", k.taps, g.flag);
    /* ours, Y held, then a quick release and press again (the off's tap still waiting): on again, held */
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 0.8, 1, &k);
    run(&v, &c, &g, &t, 0, 2 * DT, 1, &k);
    run(&v, &c, &g, &t, 1, 1.5, 1, &k);
    CHECK(g.flag == 1 && v.ours && v.want == -1 && g.toggles == 2,
          "held, let go 2 ticks, held again: on and ours, no off between (flag %d ours %d want %d toggles %d)", g.flag,
          v.ours, v.want, g.toggles);
    run(&v, &c, &g, &t, 0, 1.5, 1, &k);
    CHECK(g.flag == 0 && !v.ours, "then let go: off (flag %d ours %d)", g.flag, v.ours);
}

/* the game turns it on by itself (a phase's look, a story moment): no wish of ours; Y turns it off */
static void test_game_on(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 400;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    run(&v, &c, &g, &t, 0, 0.3, 1, &k);
    g.flag = 1; /* the game's own */
    run(&v, &c, &g, &t, 0, 2.0, 1, &k);
    CHECK(k.games == 1 && !k.taps && g.flag == 1 && v.want == -1 && !v.ours, "the game's on: seen (%d), kept (taps %d)",
          k.games, k.taps);
    g.flag = 0; /* and off by itself */
    run(&v, &c, &g, &t, 0, 0.5, 1, &k);
    CHECK(k.games == 2 && !k.taps, "the game's off: seen, no taps (%d %d)", k.games, k.taps);
    g.flag = 1;
    memset(&k, 0, sizeof k);
    g.changed = t;
    run(&v, &c, &g, &t, 0, 0.1, 1, &k);
    run(&v, &c, &g, &t, 1, 0.1, 1, &k);
    run(&v, &c, &g, &t, 0, 1.5, 1, &k);
    CHECK(k.press_off == 1 && g.flag == 0 && k.taps == 1 && g.refused == 0,
          "Y 0.1 s after the game's on: off once its dwell is over (%d %d taps %d refused %d)", k.press_off, g.flag,
          k.taps, g.refused);
}

/* a game that refuses every press: tries taps, then given up (no tapping on and on); the next press tries again */
static void test_refusing(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 500;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    g.refuse_all = 1;
    run(&v, &c, &g, &t, 1, 4.0, 1, &k);
    CHECK(k.taps == c.tries && k.giveups == 1 && !v.ours && v.want == -1,
          "refused: %d taps, given up (taps %d giveups %d ours %d)", c.tries, k.taps, k.giveups, v.ours);
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 3.0, 1, &k);
    run(&v, &c, &g, &t, 0, 3.0, 1, &k);
    CHECK(!k.taps, "given up: no more taps while held, none at the release (%d)", k.taps);
    g.refuse_all = 0;
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 1.0, 1, &k);
    CHECK(g.flag == 1 && v.ours, "the next press: on (%d)", g.flag);
    run(&v, &c, &g, &t, 0, 1.0, 1, &k);
    CHECK(g.flag == 0, "and off at its release (%d)", g.flag);
    /* the taps a tick apart at least (the game sees each press) */
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    g.refuse_all = 1;
    int last = 0, joined = 0;
    for (int i = 0; i < 400; i++, t += DT) {
        somvr_vis_out o;
        somvr_vis_tick(&v, &c, 1, 1, g.flag, t, &o);
        joined += (o.ev & SOMVR_VIE_TAP) && last;
        last = o.tap;
    }
    CHECK(!joined, "no tap starts on the tick after another's (%d)", joined);
}

/* the flag unread: build 15b's blind taps (the press's and the release's edges) */
static void test_blind(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    tally k = {0};
    double t = 600;
    somvr_vis_cfg_default(&c);
    somvr_vis_init(&v, SOMVR_VIS_HOLD);
    run(&v, &c, NULL, &t, 1, 0.5, 1, &k);
    CHECK(k.taps == 1 && k.ticks_cmd == 2 && k.blind == 1, "blind press: one tap (%d, %d ticks)", k.taps, k.ticks_cmd);
    run(&v, &c, NULL, &t, 0, 0.5, 1, &k);
    CHECK(k.taps == 2 && k.ticks_cmd == 4 && !v.ours && v.want == -1, "blind release: one tap (%d, %d ticks)", k.taps,
          k.ticks_cmd);
    somvr_vis_init(&v, SOMVR_VIS_TAP);
    memset(&k, 0, sizeof k);
    run(&v, &c, NULL, &t, 1, 0.5, 1, &k);
    run(&v, &c, NULL, &t, 0, 0.5, 1, &k);
    CHECK(k.taps == 1, "blind tap mode: the press alone (%d)", k.taps);
}

static void test_tap_mode(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 700;
    setup(&v, &c, &g, SOMVR_VIS_TAP);
    run(&v, &c, &g, &t, 1, 0.2, 1, &k);
    run(&v, &c, &g, &t, 0, 1.0, 1, &k);
    CHECK(g.flag == 1 && k.taps == 1, "tap: a press = on, kept after the release (%d, taps %d)", g.flag, k.taps);
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    run(&v, &c, &g, &t, 0, 1.0, 1, &k);
    CHECK(g.flag == 0 && k.taps == 2, "tap: the next (one tick) = off (%d, taps %d)", g.flag, k.taps);
    /* two quick presses: on, then off once the on has landed and settled */
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    run(&v, &c, &g, &t, 0, 3 * DT, 1, &k);
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    run(&v, &c, &g, &t, 0, 2.0, 1, &k);
    CHECK(g.flag == 0 && g.refused == 0 && g.queued == 0, "tap: two quick presses = on and off again (%d refused %d "
          "held back %d)", g.flag, g.refused, g.queued);
    /* a press right after the off landed (within the game's dwell): the on waits, never held back by the game */
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    run(&v, &c, &g, &t, 0, 1.5, 1, &k); /* on */
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    for (int i = 0; i < 400 && g.flag; i++) run(&v, &c, &g, &t, 0, DT, 1, &k); /* until the off lands */
    run(&v, &c, &g, &t, 0, 0.2, 1, &k);
    run(&v, &c, &g, &t, 1, DT, 1, &k);
    run(&v, &c, &g, &t, 0, 2.0, 1, &k);
    CHECK(g.flag == 1 && g.queued == 0 && g.refused == 0, "tap: on 0.2 s after an off: on, not held back (%d %d %d)",
          g.flag, g.queued, g.refused);
}

/* outside the world: no command sent; Y outside the world is no press; a wish waits for the world */
static void test_world(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 800;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    run(&v, &c, &g, &t, 1, 0.5, 0, &k);
    CHECK(!k.ticks_cmd && !k.taps && v.want == -1, "Y in a menu: nothing (%d %d)", k.ticks_cmd, k.taps);
    run(&v, &c, &g, &t, 1, 0.5, 1, &k);
    CHECK(g.flag == 1 && v.ours, "back in the world, Y still held: a press, on (%d)", g.flag);
    run(&v, &c, &g, &t, 1, 0.5, 0, &k); /* a menu over the world while held: Y is not held there */
    int cmd0 = k.ticks_cmd;
    CHECK(v.want == 0 && g.flag == 1, "a menu while held: off wished (%d), the flag as it was (%d)", v.want, g.flag);
    run(&v, &c, &g, &t, 0, 1.0, 1, &k);
    CHECK(k.ticks_cmd == cmd0 + 2 && g.flag == 0 && !v.ours, "back in the world: off (%d)", g.flag);
}

/* build 15b's session (run 233334): the raw Y through the mark's chord and the controls; a press shorter than the
 * chord's window reaches the game as a one-tick press; the left palm turned up (the panel opening: the layer takes
 * only the right trigger then, nothing of Y) */
static void test_chain(void) {
    for (int ms = 40; ms <= 400; ms += 360) {
        somvr_chord ch;
        somvr_chord_cfg cc;
        sc_ctl ctl;
        sc_ctl_cfg cfg;
        somvr_vis v;
        somvr_vis_cfg c;
        game g;
        somvr_chord_reset(&ch);
        somvr_chord_cfg_default(&cc);
        sc_ctl_reset(&ctl);
        sc_ctl_cfg_default(&cfg);
        setup(&v, &c, &g, SOMVR_VIS_HOLD);
        somvr_input_state raw;
        memset(&raw, 0, sizeof raw);
        for (int h = 0; h < 2; h++) raw.hand[h].active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT;
        double t = 900, t_press = t + 0.5, t_rel = t_press + ms * 1e-3;
        int ticks_y = 0, on_seen = 0;
        for (int i = 0; i < 85 * 4; i++, t += DT) {
            int down = t >= t_press && t < t_rel;
            if (down && !(raw.hand[SOMVR_HAND_L].buttons & SOMVR_BTN_UPPER)) raw.hand[SOMVR_HAND_L].presses[1]++;
            raw.hand[SOMVR_HAND_L].buttons = down ? SOMVR_BTN_UPPER : 0;
            raw.sync_seq++;
            /* ss_input: B and Y through the chord */
            somvr_chord_in ci;
            for (int b = 0; b < 2; b++) {
                const somvr_hand_in *hi = &raw.hand[b == SOMVR_CHB_Y ? SOMVR_HAND_L : SOMVR_HAND_R];
                ci.held[b] = (hi->buttons & SOMVR_BTN_UPPER) != 0, ci.presses[b] = hi->presses[1], ci.act[b] = 1;
            }
            somvr_chord_out co;
            somvr_chord_tick(&ch, &cc, &ci, t, &co);
            somvr_input_state f = raw;
            for (int b = 0; b < 2; b++) {
                somvr_hand_in *ho = &f.hand[b == SOMVR_CHB_Y ? SOMVR_HAND_L : SOMVR_HAND_R];
                ho->buttons = (ho->buttons & ~(uint32_t)SOMVR_BTN_UPPER) | (co.held[b] ? (uint32_t)SOMVR_BTN_UPPER : 0u);
                ho->presses[1] = co.presses[b];
            }
            sc_tick_out o;
            sc_tick(&ctl, &cfg, &f, 1, SC_CTX_WORLD, 0, t, &o);
            int y = 0;
            for (int j = 0; j < o.ncmd; j++) y |= o.cmd[j] == SC_CMD_VISION;
            ticks_y += y;
            somvr_vis_out vo;
            somvr_vis_tick(&v, &c, 1, y, g.flag, t, &vo);
            game_tick(&g, vo.tap, t);
            on_seen |= g.flag;
        }
        if (ms < 120)
            CHECK(ticks_y == 1, "chain %d ms: the chord gives the game a one-tick press (%d ticks)", ms, ticks_y);
        CHECK(on_seen && g.flag == 0 && !v.ours && v.want == -1,
              "chain %d ms (Y held %d ticks at the game): on, then off (on %d flag %d ours %d want %d)", ms, ticks_y,
              on_seen, g.flag, v.ours, v.want);
    }
}

/* build 18 (relight): an orc's hit ends the vision while Y holds it ours: on again after the game's dwell, while Y
 * stays held; Y let go first = off and left off; relight off = build 17b; the game's own vision (not ours) stays off */
static void test_relight(void) {
    somvr_vis v;
    somvr_vis_cfg c;
    game g;
    tally k = {0};
    double t = 900;
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    CHECK(c.relight == 1, "relight on by default");
    run(&v, &c, &g, &t, 1, 1.5, 1, &k); /* Y held: on */
    CHECK(g.flag == 1 && v.ours, "held: on (%d)", g.flag);
    memset(&k, 0, sizeof k);
    g.flag = 0, g.changed = t; /* the hit: the game's own off */
    double hit = t;
    run(&v, &c, &g, &t, 1, 0.9, 1, &k);
    CHECK((k.ev_or & SOMVR_VIE_RELIGHT) && !k.taps && g.flag == 0, "the hit: on wished again, no tap within the dwell (taps %d)",
          k.taps);
    run(&v, &c, &g, &t, 1, 0.6, 1, &k);
    CHECK(g.flag == 1 && k.taps == 1 && g.refused == 0 && k.first_tap - hit >= 1.05 - 1e-6 && k.first_tap - hit < 1.1,
          "on again %.3f s after the hit, one tap, none refused (taps %d refused %d)", k.first_tap - hit, k.taps, g.refused);
    run(&v, &c, &g, &t, 0, 1.6, 1, &k); /* let go: off (after its dwell) */
    CHECK(g.flag == 0 && !v.ours, "let go after the relight: off (%d)", g.flag);
    /* three hits 3 s apart while held: on again after each (each relit vision kept 1.9 s) */
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 1.5, 1, &k);
    for (int i = 0; i < 3; i++) {
        g.flag = 0, g.changed = t;
        run(&v, &c, &g, &t, 1, 3.0, 1, &k);
    }
    CHECK(g.flag == 1 && k.taps == 4 && g.refused == 0 && !(k.ev_or & SOMVR_VIE_RELIGHT_STOP),
          "three hits 3 s apart held: on again each time (taps %d refused %d)", k.taps, g.refused);
    run(&v, &c, &g, &t, 0, 1.6, 1, &k);
    /* a fight where the game ends each relit vision after its 1 s minimum: one relight, then stopped - no blinking */
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 1.5, 1, &k);
    int ons = 0;
    for (int i = 0; i < 4; i++) {
        g.flag = 0, g.changed = t;
        for (double e = 0; e < 2.5; e += DT) {
            int was = g.flag;
            run(&v, &c, &g, &t, 1, DT, 1, &k);
            if (g.flag && !was) ons++;
            if (g.flag && t - g.changed >= 1.0) { g.flag = 0, g.changed = t; } /* the game: off after its minimum */
        }
    }
    CHECK(ons == 1 && (k.ev_or & SOMVR_VIE_RELIGHT_STOP) && k.taps == 2 && g.flag == 0,
          "a fight ending each relight after 1 s: one relight, then stopped (ons %d taps %d)", ons, k.taps);
    run(&v, &c, &g, &t, 0, 1.6, 1, &k);
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 1.6, 1, &k);
    CHECK(g.flag == 1 && k.taps == 1, "Y pressed again: on, relights allowed again (taps %d)", k.taps);
    run(&v, &c, &g, &t, 0, 1.6, 1, &k);
    /* the hit, then Y let go within the dwell: no relight tap, off stays */
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 1.5, 1, &k);
    g.flag = 0, g.changed = t;
    run(&v, &c, &g, &t, 1, 0.4, 1, &k);
    run(&v, &c, &g, &t, 0, 2.0, 1, &k);
    CHECK(g.flag == 0 && k.taps == 1 && !v.ours && v.want == -1, "hit, let go within the dwell: stays off (taps %d)", k.taps);
    /* relight off: build 17b - left off until Y */
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    c.relight = 0;
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 1, 1.5, 1, &k);
    g.flag = 0, g.changed = t;
    run(&v, &c, &g, &t, 1, 2.0, 1, &k);
    CHECK(g.flag == 0 && k.taps == 1 && !(k.ev_or & SOMVR_VIE_RELIGHT), "relight off: left off (taps %d)", k.taps);
    /* the game's own vision (not ours) ending while Y is not held: nothing */
    setup(&v, &c, &g, SOMVR_VIS_HOLD);
    memset(&k, 0, sizeof k);
    run(&v, &c, &g, &t, 0, 0.3, 1, &k);
    g.flag = 1, g.changed = t;
    run(&v, &c, &g, &t, 0, 1.2, 1, &k);
    g.flag = 0, g.changed = t;
    run(&v, &c, &g, &t, 0, 2.0, 1, &k);
    CHECK(!k.taps && !(k.ev_or & SOMVR_VIE_RELIGHT), "the game's own on and off: no relight (taps %d)", k.taps);
}

int main(void) {
    test_relight();
    test_hold();
    test_quick_tap();
    test_press_while_on();
    test_game_on();
    test_refusing();
    test_blind();
    test_tap_mode();
    test_world();
    test_chain();
    printf("test_vision: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
