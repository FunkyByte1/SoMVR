/* test_ctl.c - unit tests of somvr_ctl.c (docs/re-xr-input.md 3.2-3.8): the deny list (no denied command comes out
 * in any context, over 20000 random snapshots), the dead zone and the stick's pre-rotation (90 deg turns forward into
 * strafe right), the button rules (one hold per press, taps between ticks, a release + press between ticks, nothing
 * until seen up after a loss, an inactive hand), snap turn with re-arm, smooth turn with the ease-in, the crouch pulse,
 * the sprint latch, the menu pad (letters, grips, triggers, sticks, clicks, the d-pad flicks and their repeat), the
 * liveness rules (focus, sync, the session, a stalled snapshot), the turn about the head (the head stays put) and the
 * haptic mixer (one post per flat effect, the rumble refreshed every 4th tick, changes, stops, the scale). */
#include "../somvr_ctl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define DEG(x) ((float)(x) * 3.14159265358979f / 180.0f)
#define DT (1.0 / 90)

static sc_ctl_cfg C;
static sc_ctl S;
static somvr_input_state ST;
static double T = 100.0;

static void reset_all(void) {
    sc_ctl_cfg_default(&C);
    sc_ctl_reset(&S);
    memset(&ST, 0, sizeof ST);
    ST.sync_seq = 1;
    ST.flags = SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED | SOMVR_INS_LOCATED;
    for (int h = 0; h < 2; h++) ST.hand[h].active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT;
}
/* one tick at 90 Hz (the layer synced once since the last one) */
static void tick(int ctx, float dpsi, sc_tick_out *o) {
    T += DT;
    ST.sync_seq++;
    int why;
    int live = sc_live(&S, &C, &ST, 1, 1, T, &why);
    sc_tick(&S, &C, &ST, live, ctx, dpsi, T, o);
}
static int has_cmd(const sc_tick_out *o, int cmd) {
    for (int i = 0; i < o->ncmd; i++)
        if (o->cmd[i] == cmd) return 1;
    return 0;
}
static int pad_on(const sc_tick_out *o, int i) { return (o->pad_on >> i) & 1u; }
static void press(int h, uint32_t bit) { /* held now; the layer counts the rising edge */
    if (!(ST.hand[h].buttons & bit)) ST.hand[h].presses[__builtin_ctz(bit)]++;
    ST.hand[h].buttons |= bit;
}
static void release(int h, uint32_t bit) { ST.hand[h].buttons &= ~bit; }

/* ---- deny list and contexts --------------------------------------------------------------------------------------- */
static void test_deny(void) {
    int always[] = {118, 119, 120, 122, 123, 126, 131, 136, 155, 156, 159, 189, 215, 231, 242, 244, 317, 332, 333, 344};
    for (size_t i = 0; i < sizeof always / sizeof always[0]; i++)
        for (int ctx = 0; ctx < 3; ctx++) CHECK(sc_denied(always[i], ctx), "cmd %d not denied in ctx %d", always[i], ctx);
    int world[] = {12, 13, 22, 23, 32};
    for (size_t i = 0; i < sizeof world / sizeof world[0]; i++)
        CHECK(sc_denied(world[i], SC_CTX_WORLD) && sc_denied(world[i], SC_CTX_STRUGGLE) && !sc_denied(world[i], SC_CTX_MENU),
              "camera cmd %d: world/struggle denied, menu passes", world[i]);
    int ok[] = {3, 6, 14, 15, 16, 18, 34, 54, 55, 95, 121, 124, 125, 132, 133, 160, 161, 170, 243, 260};
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++)
        for (int ctx = 0; ctx < 3; ctx++) CHECK(!sc_denied(ok[i], ctx), "cmd %d denied in ctx %d", ok[i], ctx);
    CHECK(sc_denied(-1, 1) && sc_denied(349, 0), "out of range not denied");
    CHECK(sc_context(3, 0, 0, 0, 0) == SC_CTX_WORLD && sc_context(3, 0, 0, 0, 1) == SC_CTX_STRUGGLE &&
              sc_context(3, 0, 1, 0, 0) == SC_CTX_MENU && sc_context(1, 1, 0, 0, 0) == SC_CTX_MENU &&
              sc_context(3, 5, 0, 0, 0) == SC_CTX_MENU && sc_context(3, 0, 0, 1, 0) == SC_CTX_MENU &&
              sc_context(3, 0, 0, -1, 0) == SC_CTX_WORLD && sc_context(4, 0, 0, 0, 0) == SC_CTX_MENU,
          "contexts");
    /* 20000 random snapshots in every context: no command-level output is ever denied */
    reset_all();
    srand(7);
    int bad = 0, n = 0;
    for (int i = 0; i < 20000; i++) {
        for (int h = 0; h < 2; h++) {
            somvr_hand_in *hi = &ST.hand[h];
            uint32_t b = (uint32_t)rand() & 0x3fu;
            for (int k = 0; k < 6; k++)
                if ((b >> k & 1) && !(hi->buttons >> k & 1)) hi->presses[k]++;
            hi->buttons = b;
            hi->stick[0] = (float)rand() / RAND_MAX * 2 - 1, hi->stick[1] = (float)rand() / RAND_MAX * 2 - 1;
            hi->trigger = (float)rand() / RAND_MAX, hi->squeeze = (float)rand() / RAND_MAX;
        }
        sc_tick_out o;
        int ctx = i % 3;
        tick(ctx, DEG(rand() % 360 - 180), &o);
        for (int k = 0; k < o.ncmd; k++, n++) bad += sc_denied(o.cmd[k], ctx);
    }
    CHECK(bad == 0 && n > 1000, "%d denied commands among %d outputs", bad, n);
    printf("deny: 20 always, 5 camera commands in the world only, 20 allowed; contexts; %d random outputs, 0 denied\n", n);
}

/* ---- sticks ---------------------------------------------------------------------------------------------------------- */
static void test_sticks(void) {
    float o[2];
    sc_deadzone((float[2]){0, 0.1f}, 0.15f, o);
    CHECK(o[0] == 0 && o[1] == 0, "inside the dead zone");
    sc_deadzone((float[2]){0, 1}, 0.15f, o);
    CHECK(NEAR(o[1], 1, 1e-6) && o[0] == 0, "full: %g", o[1]);
    sc_deadzone((float[2]){0.575f, 0}, 0.15f, o);
    CHECK(NEAR(o[0], 0.5, 1e-5), "rescale: %g", o[0]);
    sc_deadzone((float[2]){1.2f, 1.6f}, 0.15f, o);
    CHECK(NEAR(hypotf(o[0], o[1]), 1, 1e-5) && NEAR(o[0] / o[1], 0.75, 1e-5), "over 1: %g %g", o[0], o[1]);
    sc_deadzone((float[2]){NAN, 0}, 0.15f, o);
    CHECK(o[0] == 0 && o[1] == 0, "NaN");
    float v[2] = {0, 1};
    sc_rotate(v, DEG(90));
    CHECK(NEAR(v[0], 1, 1e-6) && NEAR(v[1], 0, 1e-6), "90 deg: forward -> strafe right (%g %g)", v[0], v[1]);
    v[0] = 0, v[1] = 0.6f;
    sc_rotate(v, DEG(-30));
    CHECK(NEAR(v[0], -0.3, 1e-5) && NEAR(v[1], 0.6 * cos(DEG(30)), 1e-5), "-30 deg: %g %g", v[0], v[1]);
    v[0] = 0.3f, v[1] = -0.4f;
    sc_rotate(v, DEG(77));
    CHECK(NEAR(hypotf(v[0], v[1]), 0.5, 1e-6), "the magnitude is kept");
    /* through a tick: the left stick forward with the head 90 deg right of the game camera = StrafeAxis 1 */
    reset_all();
    sc_tick_out t;
    ST.hand[0].stick[1] = 1;
    tick(SC_CTX_WORLD, DEG(90), &t);
    CHECK(t.live && NEAR(t.pad[SC_PAD_LX], 1, 1e-6) && NEAR(t.pad[SC_PAD_LY], 0, 1e-6) && pad_on(&t, SC_PAD_LX) &&
              !pad_on(&t, SC_PAD_LY) && NEAR(t.move[0], 1, 1e-6),
          "tick: LX %g LY %g", t.pad[SC_PAD_LX], t.pad[SC_PAD_LY]);
    ST.hand[0].stick[1] = 0.1f;
    tick(SC_CTX_WORLD, 0, &t);
    CHECK(!pad_on(&t, SC_PAD_LX) && !pad_on(&t, SC_PAD_LY), "a small deflection moved");
    ST.hand[0].active = SOMVR_HACT_POSE, ST.hand[0].stick[1] = 1;
    tick(SC_CTX_WORLD, 0, &t);
    CHECK(!pad_on(&t, SC_PAD_LY), "an inactive hand's stick moved");
    printf("sticks: dead zone 0.15 + rescale, over 1, NaN; 90 deg = strafe right, magnitude kept; inactive hand: 0\n");
}

/* ---- buttons ---------------------------------------------------------------------------------------------------------- */
static void test_buttons(void) {
    reset_all();
    sc_tick_out o;
    tick(SC_CTX_WORLD, 0, &o); /* seen up: armed */
    /* a press held for 3 ticks: pad A on exactly then (one ON, one OFF at the binding layer) */
    int on[8] = {0};
    press(1, SOMVR_BTN_LOWER);
    for (int i = 0; i < 3; i++) tick(SC_CTX_WORLD, 0, &o), on[i] = pad_on(&o, SC_PAD_A);
    release(1, SOMVR_BTN_LOWER);
    for (int i = 3; i < 5; i++) tick(SC_CTX_WORLD, 0, &o), on[i] = pad_on(&o, SC_PAD_A);
    CHECK(on[0] && on[1] && on[2] && !on[3] && !on[4], "hold: %d%d%d%d%d", on[0], on[1], on[2], on[3], on[4]);
    /* a tap between two ticks (the counter moved, the button reads up): down for one tick */
    ST.hand[1].presses[0]++;
    tick(SC_CTX_WORLD, 0, &o);
    on[0] = pad_on(&o, SC_PAD_A);
    int tap = (o.edges & SC_E_TAP) != 0;
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(on[0] && tap && !pad_on(&o, SC_PAD_A), "tap: %d then %d", on[0], pad_on(&o, SC_PAD_A));
    /* held, then a release + a new press between two ticks: one tick up, then down again */
    press(1, SOMVR_BTN_LOWER);
    tick(SC_CTX_WORLD, 0, &o);
    on[0] = pad_on(&o, SC_PAD_A);
    ST.hand[1].presses[0]++; /* released and pressed again before the next tick */
    tick(SC_CTX_WORLD, 0, &o);
    on[1] = pad_on(&o, SC_PAD_A);
    int rep = (o.edges & SC_E_REPRESS) != 0;
    tick(SC_CTX_WORLD, 0, &o);
    on[2] = pad_on(&o, SC_PAD_A);
    tick(SC_CTX_WORLD, 0, &o);
    on[3] = pad_on(&o, SC_PAD_A);
    CHECK(on[0] && !on[1] && rep && on[2] && on[3], "re-press: %d %d %d %d", on[0], on[1], on[2], on[3]);
    /* input lost while held: everything off; back with the button still held: nothing until it has been seen up */
    ST.flags = 0;
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(!o.live && !o.pad_on && !o.ncmd, "loss: live %d pad %#x cmds %d", o.live, o.pad_on, o.ncmd);
    ST.flags = SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED;
    ST.hand[1].presses[0] += 3; /* presses during the loss never fire */
    tick(SC_CTX_WORLD, 0, &o);
    on[0] = pad_on(&o, SC_PAD_A);
    tick(SC_CTX_WORLD, 0, &o);
    on[1] = pad_on(&o, SC_PAD_A);
    release(1, SOMVR_BTN_LOWER);
    tick(SC_CTX_WORLD, 0, &o);
    on[2] = pad_on(&o, SC_PAD_A);
    press(1, SOMVR_BTN_LOWER);
    tick(SC_CTX_WORLD, 0, &o);
    on[3] = pad_on(&o, SC_PAD_A);
    CHECK(o.live && !on[0] && !on[1] && !on[2] && on[3], "after the loss: %d %d %d %d", on[0], on[1], on[2], on[3]);
    release(1, SOMVR_BTN_LOWER);
    /* an inactive hand: its buttons up and disarmed */
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[1].active = SOMVR_HACT_POSE;
    press(1, SOMVR_BTN_LOWER);
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(!pad_on(&o, SC_PAD_A), "an inactive hand pressed A");
    ST.hand[1].active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT;
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(!pad_on(&o, SC_PAD_A), "held across the inactive spell: pressed without being seen up");
    release(1, SOMVR_BTN_LOWER);
    tick(SC_CTX_WORLD, 0, &o);
    /* the world's command buttons: B = Activate (never pad B), Y = wraith vision, menu = Menu, R stick click = PDA */
    press(1, SOMVR_BTN_UPPER), press(0, SOMVR_BTN_UPPER), press(0, SOMVR_BTN_MENU), press(1, SOMVR_BTN_STICK);
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(has_cmd(&o, SC_CMD_ACTIVATE) && has_cmd(&o, SC_CMD_VISION) && has_cmd(&o, SC_CMD_MENU) && has_cmd(&o, SC_CMD_PDA) &&
              !pad_on(&o, SC_PAD_B) && !pad_on(&o, SC_PAD_Y) && !pad_on(&o, SC_PAD_X),
          "world buttons: cmds %d pad %#x", o.ncmd, o.pad_on);
    /* the same buttons in a struggle: the stock prompt's pad buttons */
    tick(SC_CTX_STRUGGLE, 0, &o);
    CHECK(pad_on(&o, SC_PAD_B) && pad_on(&o, SC_PAD_Y) && !has_cmd(&o, SC_CMD_ACTIVATE) && !has_cmd(&o, SC_CMD_VISION) &&
              has_cmd(&o, SC_CMD_MENU),
          "struggle: pad %#x", o.pad_on);
    release(1, SOMVR_BTN_UPPER), release(0, SOMVR_BTN_UPPER), release(0, SOMVR_BTN_MENU), release(1, SOMVR_BTN_STICK);
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(!o.ncmd && !o.pad_on, "all released: %d cmds pad %#x", o.ncmd, o.pad_on);
    printf("buttons: hold = the ticks held; tap = 1 tick; release+press = 1 up then down; nothing after a loss until seen "
           "up; inactive hand; B = Activate, Y = vision, menu, PDA; struggle = pad B/X/Y\n");
}

/* ---- turning, crouch, sprint ---------------------------------------------------------------------------------------- */
static void test_turn(void) {
    reset_all();
    sc_tick_out o;
    tick(SC_CTX_WORLD, 0, &o);
    int snaps = 0, sum = 0;
    ST.hand[1].stick[0] = 0.9f;
    for (int i = 0; i < 180; i++) tick(SC_CTX_WORLD, 0, &o), snaps += o.snap != 0, sum += o.snap; /* held 2 s */
    CHECK(snaps == 1 && sum == 1, "held right 2 s: %d snaps", snaps);
    ST.hand[1].stick[0] = 0.5f;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[1].stick[0] = 0.9f;
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(o.snap == 0, "re-armed at 0.5 (needs < 0.3)");
    ST.hand[1].stick[0] = 0.1f;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[1].stick[0] = -0.95f;
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(o.snap == -1 && (o.edges & SC_E_SNAP), "left snap %d", o.snap);
    ST.hand[1].stick[0] = 0;
    tick(SC_CTX_MENU, 0, &o);
    ST.hand[1].stick[0] = 0.9f;
    tick(SC_CTX_MENU, 0, &o);
    CHECK(o.snap == 0 && pad_on(&o, SC_PAD_RX), "menus: the right stick is RX, no snap");
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(o.snap == 0, "a stick held from the menu into the world snapped (must be re-armed)");
    /* smooth: 120 deg/s at full deflection, eased in over 0.1 s */
    reset_all();
    C.smooth = 1;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[1].stick[0] = 1;
    float total = 0, first = 0;
    for (int i = 0; i < 90; i++) {
        tick(SC_CTX_WORLD, 0, &o);
        if (!i) first = o.turn;
        total += o.turn;
        CHECK(o.snap == 0, "smooth mode snapped");
    }
    CHECK(NEAR(total, DEG(120) - DEG(120) * 0.05f, DEG(1.5)) && first > 0 && first < DEG(120) * (float)DT * 0.2f,
          "smooth 1 s: %g deg (first tick %g deg)", total * 57.29578, first * 57.29578);
    ST.hand[1].stick[0] = -0.575f; /* half rate to the left */
    for (int i = 0; i < 20; i++) tick(SC_CTX_WORLD, 0, &o);
    CHECK(NEAR(o.turn, -DEG(60) * DT, DEG(0.01)), "half deflection: %g deg/tick", o.turn * 57.29578);
    ST.hand[1].stick[0] = 0.1f;
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(o.turn == 0, "inside the dead zone turned");
    /* crouch: down past -0.7 once (the larger axis), re-armed above -0.3 */
    reset_all();
    tick(SC_CTX_WORLD, 0, &o);
    int cr[5];
    ST.hand[1].stick[1] = -0.8f, ST.hand[1].stick[0] = 0.75f; /* x 0.75 < |y|: crouch, not a turn */
    tick(SC_CTX_WORLD, 0, &o);
    cr[0] = has_cmd(&o, SC_CMD_CROUCH);
    CHECK(o.snap == 0, "the larger axis is y: no snap");
    tick(SC_CTX_WORLD, 0, &o);
    cr[1] = has_cmd(&o, SC_CMD_CROUCH);
    ST.hand[1].stick[1] = -0.5f, ST.hand[1].stick[0] = 0;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[1].stick[1] = -0.9f;
    tick(SC_CTX_WORLD, 0, &o);
    cr[2] = has_cmd(&o, SC_CMD_CROUCH);
    ST.hand[1].stick[1] = -0.2f;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[1].stick[1] = -0.9f;
    tick(SC_CTX_WORLD, 0, &o);
    cr[3] = has_cmd(&o, SC_CMD_CROUCH);
    tick(SC_CTX_WORLD, 0, &o);
    cr[4] = has_cmd(&o, SC_CMD_CROUCH);
    CHECK(cr[0] && !cr[1] && !cr[2] && cr[3] && !cr[4], "crouch pulses %d%d%d%d%d", cr[0], cr[1], cr[2], cr[3], cr[4]);
    /* sprint: the left stick click latches FreeRun until the stick rests under 0.2 for 0.3 s */
    reset_all();
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[0].stick[1] = 1;
    press(0, SOMVR_BTN_STICK);
    tick(SC_CTX_WORLD, 0, &o);
    int latched = has_cmd(&o, SC_CMD_FREERUN) && (o.edges & SC_E_SPRINT);
    release(0, SOMVR_BTN_STICK);
    for (int i = 0; i < 90; i++) tick(SC_CTX_WORLD, 0, &o);
    int held = has_cmd(&o, SC_CMD_FREERUN);
    ST.hand[0].stick[1] = 0.1f;
    for (int i = 0; i < 18; i++) tick(SC_CTX_WORLD, 0, &o); /* 0.2 s at rest */
    int still = has_cmd(&o, SC_CMD_FREERUN);
    ST.hand[0].stick[1] = 0.8f;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[0].stick[1] = 0;
    int k = 0;
    for (; k < 60 && has_cmd(&o, SC_CMD_FREERUN); k++) tick(SC_CTX_WORLD, 0, &o);
    CHECK(latched && held && still && k >= 26 && k <= 29, "sprint: latched %d held %d after 0.2 s %d, off after %d ticks",
          latched, held, still, k);
    /* SOMVR_GAMEMOVES=off: A and the latch reach no game move in the world; A stays in menus and struggles */
    reset_all();
    C.game_moves = 0;
    tick(SC_CTX_WORLD, 0, &o);
    ST.hand[0].stick[1] = 1;
    press(0, SOMVR_BTN_STICK);
    press(1, SOMVR_BTN_LOWER);
    tick(SC_CTX_WORLD, 0, &o);
    int gm_world = o.pad[SC_PAD_A] == 0 && !has_cmd(&o, SC_CMD_FREERUN) && o.sprint && (o.edges & SC_E_SPRINT);
    tick(SC_CTX_STRUGGLE, 0, &o);
    int gm_struggle = o.pad[SC_PAD_A] == 1;
    tick(SC_CTX_MENU, 0, &o);
    int gm_menu = o.pad[SC_PAD_A] == 1;
    release(0, SOMVR_BTN_STICK);
    release(1, SOMVR_BTN_LOWER);
    C.game_moves = 1;
    CHECK(gm_world && gm_struggle && gm_menu, "game moves off: world A/FreeRun none + sprint kept %d, struggle A %d, menu A %d",
          gm_world, gm_struggle, gm_menu);
    printf("turn: one snap per push (re-arm < 0.3), menus no turn, re-armed after a menu; smooth %g deg/s eased; crouch "
           "pulse, larger axis; sprint latch released after 0.3 s at rest\n", 120.0);
}

/* ---- menus ------------------------------------------------------------------------------------------------------------ */
static void test_menu(void) {
    reset_all();
    sc_tick_out o;
    tick(SC_CTX_MENU, 0, &o);
    press(1, SOMVR_BTN_LOWER), press(1, SOMVR_BTN_UPPER), press(0, SOMVR_BTN_LOWER), press(0, SOMVR_BTN_UPPER);
    press(0, SOMVR_BTN_MENU), press(0, SOMVR_BTN_STICK), press(1, SOMVR_BTN_STICK);
    ST.hand[0].squeeze = 0.7f, ST.hand[1].squeeze = 0.5f, ST.hand[0].trigger = 0.9f, ST.hand[1].trigger = 0.3f;
    ST.hand[1].stick[1] = -0.5f;
    tick(SC_CTX_MENU, 0, &o);
    uint32_t want = 1u << SC_PAD_A | 1u << SC_PAD_B | 1u << SC_PAD_X | 1u << SC_PAD_Y | 1u << SC_PAD_START | 1u << SC_PAD_L3 |
                    1u << SC_PAD_BACK | 1u << SC_PAD_LB | 1u << SC_PAD_LT | 1u << SC_PAD_RY;
    CHECK(o.pad_on == want && !o.ncmd && NEAR(o.pad[SC_PAD_LT], 0.9, 1e-6) && o.pad[SC_PAD_RT] == 0 &&
              NEAR(o.pad[SC_PAD_RY], -(0.5 - 0.15) / 0.85, 1e-5),
          "menu pad %#x want %#x LT %g RY %g", o.pad_on, want, o.pad[SC_PAD_LT], o.pad[SC_PAD_RY]);
    ST.hand[0].squeeze = 0.5f, ST.hand[0].trigger = 0.5f; /* inside the hysteresis: still pressed */
    tick(SC_CTX_MENU, 0, &o);
    CHECK(pad_on(&o, SC_PAD_LB) && pad_on(&o, SC_PAD_LT), "hysteresis");
    ST.hand[0].squeeze = 0.3f, ST.hand[0].trigger = 0.3f;
    tick(SC_CTX_MENU, 0, &o);
    CHECK(!pad_on(&o, SC_PAD_LB) && !pad_on(&o, SC_PAD_LT), "released below 0.4");
    for (int h = 0; h < 2; h++) ST.hand[h].buttons = 0;
    ST.hand[1].stick[1] = 0;
    /* d-pad: a flick down = one edge at once, the repeat after 0.4 s, then every 0.12 s */
    ST.hand[0].stick[1] = -0.9f;
    double t0 = T + DT, ts[8];
    int n = 0, dirs = 1;
    for (int i = 0; i < 90 && n < 8; i++) {
        tick(SC_CTX_MENU, 0, &o);
        if (pad_on(&o, SC_PAD_DOWN)) ts[n++] = T;
        dirs &= !pad_on(&o, SC_PAD_UP) && !pad_on(&o, SC_PAD_LEFT);
        if (pad_on(&o, SC_PAD_DOWN)) {
            tick(SC_CTX_MENU, 0, &o); /* a pulse is one tick */
            dirs &= !pad_on(&o, SC_PAD_DOWN);
            i++;
        }
    }
    CHECK(n >= 5 && NEAR(ts[0], t0, 1e-9) && NEAR(ts[1] - ts[0], 0.4, DT + 1e-9) && NEAR(ts[2] - ts[1], 0.12, DT + 1e-9) &&
              NEAR(ts[3] - ts[2], 0.12, DT + 1e-9) && dirs,
          "d-pad repeat: %d edges, %g %g %g", n, n > 1 ? ts[1] - ts[0] : 0, n > 2 ? ts[2] - ts[1] : 0, n > 3 ? ts[3] - ts[2] : 0);
    ST.hand[0].stick[1] = 0.6f; /* between 0.5 and 0.7: no new direction */
    tick(SC_CTX_MENU, 0, &o);
    CHECK(!pad_on(&o, SC_PAD_UP) && !pad_on(&o, SC_PAD_DOWN), "between the flick thresholds");
    ST.hand[0].stick[0] = 0.8f, ST.hand[0].stick[1] = 0.1f;
    tick(SC_CTX_MENU, 0, &o);
    CHECK(pad_on(&o, SC_PAD_RIGHT) && (o.edges & SC_E_DPAD), "flick right");
    printf("menu: A/B/X/Y/START/L3/BACK, LB/RB and LT/RT with hysteresis, sticks with the dead zone; d-pad flick at once, "
           "repeat %.2f s then %.2f s\n", ts[1] - ts[0], ts[2] - ts[1]);
}

/* ---- liveness ---------------------------------------------------------------------------------------------------------- */
static void test_live(void) {
    reset_all();
    int why;
    T += DT;
    CHECK(!sc_live(&S, &C, &ST, 0, 1, T, &why) && why == SC_LIVE_NOREAD, "no read");
    ST.sync_seq = 0;
    CHECK(!sc_live(&S, &C, &ST, 1, 1, T, &why) && why == SC_LIVE_NOSYNC, "no sync (a v2 layer)");
    ST.sync_seq = 5;
    CHECK(!sc_live(&S, &C, &ST, 1, 0, T, &why) && why == SC_LIVE_NOTRUNNING, "not running");
    ST.flags = SOMVR_INS_SYNCED;
    CHECK(!sc_live(&S, &C, &ST, 1, 1, T, &why) && why == SC_LIVE_UNFOCUSED, "unfocused");
    ST.flags = SOMVR_INS_FOCUSED;
    CHECK(!sc_live(&S, &C, &ST, 1, 1, T, &why) && why == SC_LIVE_UNFOCUSED, "not synced");
    ST.flags = SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED;
    CHECK(sc_live(&S, &C, &ST, 1, 1, T, &why) && why == SC_LIVE_OK, "live");
    /* the same sync_seq: live for 4 ticks at 90 Hz, lost when both 4 ticks and 50 ms passed */
    int lost_at = -1;
    for (int i = 1; i <= 8 && lost_at < 0; i++) {
        T += DT;
        if (!sc_live(&S, &C, &ST, 1, 1, T, &why)) lost_at = i;
    }
    CHECK(lost_at == 5 && why == SC_LIVE_STALLED, "stall: lost at tick %d (%s)", lost_at, sc_live_name(why));
    ST.sync_seq++;
    T += DT;
    CHECK(sc_live(&S, &C, &ST, 1, 1, T, &why), "back after a new sync");
    /* a fast game loop (1 ms ticks): 4 ticks are not enough, 50 ms are */
    lost_at = -1;
    for (int i = 1; i <= 80 && lost_at < 0; i++) {
        T += 0.001;
        if (!sc_live(&S, &C, &ST, 1, 1, T, &why)) lost_at = i;
    }
    CHECK(lost_at == 50 || lost_at == 51, "1 kHz ticks: lost after %d ms", lost_at);
    printf("live: no read / no sync / not running / unfocused / not synced; a stalled snapshot after 4 ticks and 50 ms\n");
}

/* ---- the turn about the head ----------------------------------------------------------------------------------------- */
static sv_vec3 world(sv_vec3 p, sv_vec3 p0, float a) { /* the rig without the base: R(Y(a)) (p - p0) */
    return sv_qrot(sv_qyaw(a), (sv_vec3){p.x - p0.x, p.y - p0.y, p.z - p0.z});
}
static void test_turn_p0(void) {
    srand(11);
    float worst = 0;
    for (int i = 0; i < 200; i++) {
        sv_vec3 p0 = {(float)(rand() % 400 - 200), (float)(rand() % 50), (float)(rand() % 400 - 200)};
        sv_vec3 h = {(float)(rand() % 400 - 200), 160.0f + (float)(rand() % 20), (float)(rand() % 400 - 200)};
        float a = DEG(rand() % 360 - 180), d = DEG(rand() % 180 - 90);
        sv_vec3 w0 = world(h, p0, a), p1 = sc_turn_p0(p0, h, d), w1 = world(h, p1, a + d);
        float e = sqrtf((w1.x - w0.x) * (w1.x - w0.x) + (w1.y - w0.y) * (w1.y - w0.y) + (w1.z - w0.z) * (w1.z - w0.z));
        worst = fmaxf(worst, e);
        CHECK(p1.y == p0.y, "y changed");
    }
    CHECK(worst < 1e-3f, "the head moved %g cm across a turn", worst);
    sv_vec3 p1 = sc_turn_p0((sv_vec3){0, 0, 0}, (sv_vec3){0, 170, 0}, DEG(45));
    CHECK(p1.x == 0 && p1.z == 0, "a head on P0: P0 stays");
    printf("turn about the head: 200 random turns, the head's world position moves < %.4f cm\n", worst);
}

/* ---- haptics ---------------------------------------------------------------------------------------------------------- */
static void test_haptics(void) {
    sc_hap h;
    sc_hap_reset(&h);
    somvr_haptic_req r;
    double t = 10;
    float P = 1.0f / 90;
    int posts = 0;
    CHECK(!sc_hap_tick(&h, t, 1, P, &r), "nothing: a post");
    sc_hap_add(&h, t, 1.0f, 0.2f, 0, SOMVR_HAP_COMBAT);
    CHECK(sc_hap_tick(&h, t, 1, P, &r) && r.amplitude == 1.0f && NEAR(r.duration_ns, 2e8, 1e3) && r.source == SOMVR_HAP_COMBAT,
          "hit: amp %g dur %lld", r.amplitude, (long long)r.duration_ns);
    for (int i = 1; i < 30; i++) posts += sc_hap_tick(&h, t + i * P, 1, P, &r);
    CHECK(posts == 0, "a flat effect was re-posted %d times (it runs on its own, and ends on its own)", posts);
    /* the rumble: 0.5 -> a 6-period pulse refreshed every 4th tick; a change of 0.01: nothing new; 0.05: a post */
    t += 1;
    h.rumble = 0.5f;
    posts = 0;
    int first = sc_hap_tick(&h, t, 1, P, &r);
    CHECK(first && r.amplitude == 0.5f && NEAR(r.duration_ns, 6e9 / 90, 1e3) && r.source == SOMVR_HAP_RUMBLE, "rumble %g %lld",
          r.amplitude, (long long)r.duration_ns);
    for (int i = 1; i <= 12; i++) posts += sc_hap_tick(&h, t + i * P, 1, P, &r);
    CHECK(posts == 3, "refreshes in 12 ticks: %d", posts);
    h.rumble = 0.51f;
    int small = sc_hap_tick(&h, t + 13 * P, 1, P, &r);
    h.rumble = 0.56f;
    int big = sc_hap_tick(&h, t + 14 * P, 1, P, &r);
    CHECK(!small && big && NEAR(r.amplitude, 0.56, 1e-6), "changes: 0.01 %d, 0.05 %d", small, big);
    /* a hit on top of the rumble wins; when it ends, back to the rumble's level */
    sc_hap_add(&h, t + 15 * P, 0.9f, 0.1f, 0, SOMVR_HAP_COMBAT);
    CHECK(sc_hap_tick(&h, t + 15 * P, 1, P, &r) && NEAR(r.amplitude, 0.9, 1e-6), "hit over rumble");
    int back = 0;
    for (int i = 16; i < 30; i++)
        if (sc_hap_tick(&h, t + i * P, 1, P, &r) && NEAR(r.amplitude, 0.56, 1e-6)) back++;
    CHECK(back >= 1, "the rumble's level after the hit");
    h.rumble = 0;
    CHECK(sc_hap_tick(&h, t + 31 * P, 1, P, &r) && r.amplitude == 0 && r.duration_ns == -1, "rumble off: a stop");
    CHECK(!sc_hap_tick(&h, t + 32 * P, 1, P, &r), "a second stop");
    /* the option: 150 % clamps to 1, 0 = never a post; a ramp is continuous (refreshed) */
    sc_hap_add(&h, t + 40 * P, 0.8f, 0.5f, 0, SOMVR_HAP_TEST);
    CHECK(sc_hap_tick(&h, t + 40 * P, 1.5f, P, &r) && r.amplitude == 1.0f, "scale 1.5: %g", r.amplitude);
    sc_hap_reset(&h);
    sc_hap_add(&h, t, 0.8f, 0.5f, 0, SOMVR_HAP_TEST);
    CHECK(!sc_hap_tick(&h, t, 0, P, &r), "scale 0 posted");
    sc_hap_reset(&h);
    sc_hap_add(&h, t, 1.0f, 0.6f, 1, SOMVR_HAP_COMBAT);
    posts = 0;
    for (int i = 0; i < 54; i++) posts += sc_hap_tick(&h, t + i * P, 1, P, &r);
    CHECK(posts >= 10 && posts <= 30, "a 0.6 s ramp: %d posts", posts);
    /* the strongest of 4 slots is kept when a 5th comes */
    sc_hap_reset(&h);
    for (int i = 0; i < 5; i++) sc_hap_add(&h, t, 0.2f + 0.1f * (float)i, 1.0f, 0, SOMVR_HAP_TEST);
    CHECK(NEAR(sc_hap_level(&h, t, 1, NULL, NULL, NULL), 0.6, 1e-6), "slots: level %g", sc_hap_level(&h, t, 1, NULL, NULL, NULL));
    /* build 17: a double (and a triple) pulse scheduled at once plays every pulse (the second used to take the first's
     * slot: it had ended at the second's start, in pulse time, before it played) */
    sc_hap_reset(&h);
    for (int k = 0; k < 3; k++) sc_hap_add_at(&h, t, t + k * 0.1, 0.7f, 0.045f, 0, SOMVR_HAP_COMBAT);
    CHECK(NEAR(sc_hap_level(&h, t + 0.02, 1, NULL, NULL, NULL), 0.7, 1e-6) &&
              NEAR(sc_hap_level(&h, t + 0.07, 1, NULL, NULL, NULL), 0, 1e-6) &&
              NEAR(sc_hap_level(&h, t + 0.12, 1, NULL, NULL, NULL), 0.7, 1e-6) &&
              NEAR(sc_hap_level(&h, t + 0.22, 1, NULL, NULL, NULL), 0.7, 1e-6),
          "three pulses scheduled at once: all three play");
    printf("haptics: a flat hit = 1 post (amp 1, 200 ms), the rumble a 6-period pulse every 4th tick, 0.02 threshold, "
           "hit over rumble, stop, scale clamp, scale 0, ramp refreshed\n");
}

/* ---- build 27, SOMVR_ARMY: the right stick's click held = the game's "Hold [>]" (re-xr-pipeline.md 110) ------------- */
static void test_army(void) {
    sc_tick_out o;
    /* off (the default): the map while the click is held, every tick */
    reset_all();
    tick(SC_CTX_WORLD, 0, &o);
    press(1, SOMVR_BTN_STICK);
    int pda = 0, menu = 0;
    for (int i = 0; i < 90; i++) tick(SC_CTX_WORLD, 0, &o), pda += has_cmd(&o, SC_CMD_PDA), menu += has_cmd(&o, SC_CMD_MENU);
    CHECK(pda == 90 && menu == 0, "off: PDA %d ticks (90), Menu %d (0)", pda, menu);
    release(1, SOMVR_BTN_STICK);
    /* on: a 0.2 s click = the map once, at the release; no Menu */
    reset_all();
    C.army_s = 0.5f, C.army_ms = 1200, C.army_cmd = SC_CMD_MENU;
    tick(SC_CTX_WORLD, 0, &o);
    press(1, SOMVR_BTN_STICK);
    pda = menu = 0;
    for (int i = 0; i < 18; i++) tick(SC_CTX_WORLD, 0, &o), pda += has_cmd(&o, SC_CMD_PDA), menu += has_cmd(&o, SC_CMD_MENU);
    CHECK(pda == 0 && menu == 0, "on, held 0.2 s: nothing yet (PDA %d, Menu %d)", pda, menu);
    release(1, SOMVR_BTN_STICK);
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(has_cmd(&o, SC_CMD_PDA) && (o.edges & SC_E_MAP) && !has_cmd(&o, SC_CMD_MENU), "on, the click's release: the map");
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(!has_cmd(&o, SC_CMD_PDA), "on: the map one tick");
    /* on: held 2 s = Menu from 0.5 s for 1.2 s (108 ticks), once; no map at the release */
    press(1, SOMVR_BTN_STICK);
    int first = -1, armies = 0;
    pda = menu = 0;
    for (int i = 0; i < 180; i++) {
        tick(SC_CTX_WORLD, 0, &o);
        if (has_cmd(&o, SC_CMD_MENU)) { menu++; if (first < 0) first = i; }
        pda += has_cmd(&o, SC_CMD_PDA), armies += (o.edges & SC_E_ARMY) != 0;
    }
    release(1, SOMVR_BTN_STICK);
    for (int i = 0; i < 90; i++) tick(SC_CTX_WORLD, 0, &o), pda += has_cmd(&o, SC_CMD_PDA), menu += has_cmd(&o, SC_CMD_MENU);
    CHECK(first >= 43 && first <= 45 && menu >= 107 && menu <= 109 && armies == 1 && pda == 0,
          "on, held 2 s: Menu from tick %d (44) for %d ticks (108), %d army edge(s) (1), PDA %d (0)", first, menu, armies, pda);
    /* on: a hold that opens a menu: nothing carried into it, and the let-go there is no map at the return */
    press(1, SOMVR_BTN_STICK);
    for (int i = 0; i < 50; i++) tick(SC_CTX_WORLD, 0, &o);
    menu = 0;
    for (int i = 0; i < 30; i++) tick(SC_CTX_MENU, 0, &o), menu += has_cmd(&o, SC_CMD_MENU) || pad_on(&o, SC_PAD_START);
    release(1, SOMVR_BTN_STICK);
    tick(SC_CTX_MENU, 0, &o);
    pda = 0;
    for (int i = 0; i < 30; i++) tick(SC_CTX_WORLD, 0, &o), pda += has_cmd(&o, SC_CMD_PDA), menu += has_cmd(&o, SC_CMD_MENU);
    CHECK(menu == 0 && pda == 0, "on, the hold's menu: Menu/START %d (0) there, PDA %d (0) at the return", menu, pda);
    /* the left menu button is untouched: Menu while it is held */
    press(0, SOMVR_BTN_MENU);
    tick(SC_CTX_WORLD, 0, &o);
    CHECK(has_cmd(&o, SC_CMD_MENU), "on: the left menu button still = Menu");
    release(0, SOMVR_BTN_MENU);
    printf("army: off = the map while held; on: a click = the map at the release, held 0.5 s = Menu 1.2 s once, nothing "
           "carried into its menu\n");
}

int main(void) {
    test_deny();
    test_sticks();
    test_buttons();
    test_turn();
    test_menu();
    test_live();
    test_turn_p0();
    test_haptics();
    test_army();
    printf("test_ctl: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
