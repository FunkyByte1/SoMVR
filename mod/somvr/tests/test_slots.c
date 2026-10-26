/* test_slots.c - unit tests of somvr_slots.c (SOMVR_SLOTS=1, the body slots): the geometry (the head's heading level,
 * pitched, steeply down and up, rolled; the torso's deadband; the slots' offsets, mirrored for a left main hand, the
 * dagger at the hip, at the back or off) and the state machine at 90 Hz: the approach rule (the head turns over the
 * shoulder, the slot stays; past the deadband it turns with the head; away from the slots the torso is the head's),
 * the enter tick and its hysteresis, a draw and a put-back by presses in the slot, the drop-in (a held grip let go in
 * the slot; not right after a draw there), the quick draw (two presses outside within 0.35 s; too slow; with the sword
 * in the hand), hold mode, the asks (DONE, DENIED, the game's own change), the hand needed elsewhere (the bow: both
 * hands; a climb: busy, then a hold; suspend and restore, restore off), no live input or no allow (no asks, no
 * preference), nograb while a slot's press is held, the blade and keep, the dagger with the off hand only, NaN inputs. */
#include "../somvr_slots.h"

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
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define NEAR3(v, X, Y, Z, e) (NEAR((v).x, X, e) && NEAR((v).y, Y, e) && NEAR((v).z, Z, e))
#define DEG 0.01745329252f

static sv_quat qaxis(float x, float y, float z, float deg) {
    float l = sqrtf(x * x + y * y + z * z), h = 0.5f * deg * DEG;
    return (sv_quat){ x / l * sinf(h), y / l * sinf(h), z / l * sinf(h), cosf(h) };
}
/* a head turned by yaw (+ right), then pitched (+ down: about +X in the left-handed frame), then rolled */
static sv_quat head_q(float yaw, float pitch, float roll) {
    return sv_qmul(sv_qmul(qaxis(0, 1, 0, yaw), qaxis(1, 0, 0, pitch)), qaxis(0, 0, 1, roll));
}

/* ---- a rig: the head at (0, 160, 0) facing +Z, both hands at the sides, 90 Hz -------------------------------------- */
static somvr_slots S;
static somvr_slots_cfg C;
static somvr_slots_in I;
static somvr_slots_out O;
static uint32_t EV[2]; /* the events since the last reset */
static void rig(void) {
    somvr_slots_cfg_default(&C);
    somvr_slots_init(&S);
    memset(&I, 0, sizeof I);
    I.t = 10, I.live = 1, I.allow = 1, I.have[0] = I.have[1] = 1, I.head_ok = 1;
    I.head = (sv_vec3){ 0, 160, 0 }, I.head_q = (sv_quat){ 0, 0, 0, 1 };
    I.valid[0] = I.valid[1] = 1;
    I.p[0] = (sv_vec3){ -30, 100, 20 }, I.p[1] = (sv_vec3){ 30, 100, 20 };
    EV[0] = EV[1] = 0;
}
static void step(void) {
    I.t += 1.0 / 90;
    somvr_slots_step(&S, &C, &I, &O);
    EV[0] |= O.ev[0], EV[1] |= O.ev[1];
}
static void steps(int n) {
    for (int i = 0; i < n; i++) step();
}
/* the game follows the asks (as somvr.c's calls do, a frame later) */
static void game_follows(void) {
    for (int k = 0; k < SOMVR_NSLOT; k++)
        if (O.want[k] >= 0) I.drawn[k] = O.want[k];
}
static void gsteps(int n) {
    for (int i = 0; i < n; i++) step(), game_follows();
}
static sv_vec3 slot_at(int k) { return somvr_slots_pos(&C, k, I.head, S.torso); }
static void hand_to_slot(int h, int k) { I.p[h] = slot_at(k); }
static void press(int h) { I.squeeze[h] = 1; }
static void open_(int h) { I.squeeze[h] = 0; }

static void test_geometry(void) {
    somvr_slots_cfg_default(&C);
    CHECK(C.main_hand == 1 && C.sword_r_cm == 20 && C.dagger_r_cm == 15 && C.quick_s == 0.35f && C.hold == 0 &&
              C.restore == 1 && C.deadband_deg == 30 && C.approach_cm[0] == 15 && C.approach_cm[1] == 0 && C.reassert == 1 &&
              C.reassert_max == 2 && C.reassert_s == 3.0f && C.undraw == 0,
          "defaults");
    CHECK(somvr_slots_hand(&C, SOMVR_SLOT_SWORD) == 1 && somvr_slots_hand(&C, SOMVR_SLOT_DAGGER) == 0, "hands");
    sv_vec3 h = { 0, 160, 0 }, p = somvr_slots_pos(&C, SOMVR_SLOT_SWORD, h, 0);
    CHECK(NEAR3(p, 10, 150, -15, 1e-4), "the sword at yaw 0: %g %g %g", p.x, p.y, p.z);
    p = somvr_slots_pos(&C, SOMVR_SLOT_DAGGER, h, 0);
    CHECK(NEAR3(p, -18, 98, -5, 1e-4), "the dagger at the left hip: %g %g %g", p.x, p.y, p.z);
    /* yaw 90 (facing +X): right = -Z, ahead = +X */
    p = somvr_slots_pos(&C, SOMVR_SLOT_SWORD, h, 90 * DEG);
    CHECK(NEAR3(p, -15, 150, -10, 1e-3), "the sword at yaw 90: %g %g %g", p.x, p.y, p.z);
    C.main_hand = 0;
    p = somvr_slots_pos(&C, SOMVR_SLOT_SWORD, h, 0);
    CHECK(NEAR3(p, -10, 150, -15, 1e-4) && somvr_slots_hand(&C, SOMVR_SLOT_SWORD) == 0, "left-handed sword: %g %g %g",
          p.x, p.y, p.z);
    p = somvr_slots_pos(&C, SOMVR_SLOT_DAGGER, h, 0);
    CHECK(NEAR3(p, 18, 98, -5, 1e-4) && somvr_slots_hand(&C, SOMVR_SLOT_DAGGER) == 1, "left-handed dagger: %g %g %g", p.x,
          p.y, p.z);
    C.main_hand = 1, C.dagger_at = SOMVR_SLOTS_DAGGER_BACK;
    p = somvr_slots_pos(&C, SOMVR_SLOT_DAGGER, h, 0);
    CHECK(NEAR3(p, -8, 105, -22, 1e-4), "the dagger at the back: %g %g %g", p.x, p.y, p.z);
    sv_vec3 o;
    C.dagger_at = SOMVR_SLOTS_DAGGER_OFF;
    CHECK(!somvr_slots_ofs(&C, SOMVR_SLOT_DAGGER, &o) && somvr_slots_ofs(&C, SOMVR_SLOT_SWORD, &o), "the dagger off");
    /* the heading */
    CHECK(NEAR(somvr_slots_heading(head_q(0, 0, 0)), 0, 1e-5), "level");
    CHECK(NEAR(somvr_slots_heading(head_q(40, 0, 0)), 40 * DEG, 1e-4), "turned 40");
    CHECK(NEAR(somvr_slots_heading(head_q(-120, 0, 0)), -120 * DEG, 1e-4), "turned -120");
    CHECK(NEAR(somvr_slots_heading(head_q(40, 45, 0)), 40 * DEG, 1e-4), "pitched 45 down");
    CHECK(NEAR(somvr_slots_heading(head_q(40, -50, 0)), 40 * DEG, 1e-4), "pitched 50 up");
    CHECK(NEAR(somvr_slots_heading(head_q(40, 85, 0)), 40 * DEG, 1e-4), "pitched 85 down");
    CHECK(NEAR(somvr_slots_heading(head_q(40, 90, 0)), 40 * DEG, 1e-4), "straight down: the up vector");
    CHECK(NEAR(somvr_slots_heading(head_q(40, -90, 0)), 40 * DEG, 1e-4), "straight up: the up vector");
    CHECK(NEAR(somvr_slots_heading(head_q(40, 0, 30)), 40 * DEG, 1e-4), "rolled 30, level");
    CHECK(NEAR(somvr_slots_heading(head_q(40, 40, 25)), 40 * DEG, 1e-4), "rolled 25, pitched 40");
    float a = somvr_slots_heading(head_q(10, 59.9f, 20)), b = somvr_slots_heading(head_q(10, 60.1f, 20));
    CHECK(NEAR(a, b, 0.2 * DEG), "continuous at 60 deg of pitch: %g %g", a / DEG, b / DEG);
    CHECK(NEAR(somvr_slots_heading((sv_quat){ 0, 0, 0, 0 }), 0, 1e-6) ||
              isfinite(somvr_slots_heading((sv_quat){ 0, 0, 0, 0 })),
          "a zero quaternion");
    /* the torso */
    CHECK(NEAR(somvr_slots_torso(0.3f, 1.0f, 0, 30 * DEG), 1.0f, 1e-6), "not held: the head's");
    CHECK(NEAR(somvr_slots_torso(0.3f, 0.6f, 1, 30 * DEG), 0.3f, 1e-6), "held within the deadband");
    CHECK(NEAR(somvr_slots_torso(0, 50 * DEG, 1, 30 * DEG), 20 * DEG, 1e-5), "held past it: head - 30");
    CHECK(NEAR(somvr_slots_torso(0, -50 * DEG, 1, 30 * DEG), -20 * DEG, 1e-5), "held past it, left: head + 30");
    CHECK(NEAR(somvr_slots_torso(175 * DEG, -170 * DEG, 1, 30 * DEG), 175 * DEG, 1e-5), "across +-180, within");
    float w = somvr_slots_torso(150 * DEG, -150 * DEG, 1, 30 * DEG);
    CHECK(NEAR(w, 180 * DEG, 1e-4) || NEAR(w, -180 * DEG, 1e-4), "across +-180, past: %g", w / DEG);
}

static void test_torso_rule(void) {
    rig();
    step();
    CHECK(NEAR(O.torso_yaw, 0, 1e-6) && !O.torso_held, "start: the head's heading");
    I.head_q = head_q(50, 0, 0);
    step();
    CHECK(NEAR(O.torso_yaw, 50 * DEG, 1e-5) && !O.torso_held, "no hand near: at once with the head: %g", O.torso_yaw / DEG);
    /* the right hand near the sword's slot: the head looks 25 deg over the right shoulder, the slot stays */
    I.head_q = head_q(0, 0, 0);
    step();
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    sv_vec3 s0 = O.slot[SOMVR_SLOT_SWORD];
    I.head_q = head_q(25, 10, 0);
    step();
    CHECK(O.torso_held && NEAR(O.torso_yaw, 0, 1e-6) && NEAR3(O.slot[0], s0.x, s0.y, s0.z, 1e-4) && O.inside[0],
          "over the shoulder: the slot stays (torso %g)", O.torso_yaw / DEG);
    I.head_q = head_q(70, 0, 0);
    step();
    CHECK(O.torso_held && NEAR(O.torso_yaw, 40 * DEG, 1e-4), "past the deadband: head - 30 = %g", O.torso_yaw / DEG);
    I.head_q = head_q(55, 0, 0);
    step();
    CHECK(NEAR(O.torso_yaw, 40 * DEG, 1e-4), "back within it: kept %g", O.torso_yaw / DEG);
    I.p[1] = (sv_vec3){ 30, 100, 60 }; /* away */
    step();
    CHECK(!O.torso_held && NEAR(O.torso_yaw, 55 * DEG, 1e-4), "the hand away: the head's again %g", O.torso_yaw / DEG);
    /* the off hand at the dagger does not hold it (approach 0); with an approach it does */
    I.head_q = head_q(0, 0, 0);
    step();
    hand_to_slot(0, SOMVR_SLOT_DAGGER);
    I.head_q = head_q(-20, 30, 0);
    step();
    CHECK(!O.torso_held && NEAR(O.torso_yaw, -20 * DEG, 1e-4), "the hip: the head's heading (%g)", O.torso_yaw / DEG);
    C.approach_cm[SOMVR_SLOT_DAGGER] = 10;
    I.p[0] = (sv_vec3){ -60, 100, 60 };
    I.head_q = head_q(0, 0, 0);
    step();
    hand_to_slot(0, SOMVR_SLOT_DAGGER);
    I.head_q = head_q(-20, 30, 0);
    step();
    CHECK(O.torso_held && NEAR(O.torso_yaw, 0, 1e-6), "with a dagger approach: kept");
    /* the main hand at the hip does not count (it is not its slot) */
    rig();
    step();
    hand_to_slot(1, SOMVR_SLOT_DAGGER);
    I.p[0] = (sv_vec3){ -60, 100, 60 };
    I.head_q = head_q(20, 0, 0);
    step();
    CHECK(!O.torso_held && !O.inside[1] && NEAR(O.torso_yaw, 20 * DEG, 1e-5), "the wrong hand: not held");
    /* no head: the torso stays */
    I.head_ok = 0;
    step();
    CHECK(NEAR(O.torso_yaw, 20 * DEG, 1e-5), "no head pose: kept");
}

static void test_draw_put(void) {
    rig();
    step();
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_ENTER) && O.inside[0] && O.keep[0] == 0 && !O.blade, "entered: the tick, keep sheathed");
    step();
    CHECK(!(O.ev[0] & SOMVR_SL_E_ENTER), "one tick");
    /* hysteresis: 3 cm out stays in, 5 cm out leaves, back in: a new tick */
    sv_vec3 s = slot_at(0);
    I.p[1] = (sv_vec3){ s.x, s.y, s.z - 23 };
    step();
    CHECK(O.inside[0] && !(O.ev[0] & (SOMVR_SL_E_ENTER | SOMVR_SL_E_LEAVE)), "3 cm past the radius: still in");
    I.p[1] = (sv_vec3){ s.x, s.y, s.z - 25 };
    step();
    CHECK(!O.inside[0] && (O.ev[0] & SOMVR_SL_E_LEAVE), "5 cm past: out");
    I.p[1] = (sv_vec3){ s.x, s.y, s.z - 21 };
    step();
    CHECK(!O.inside[0], "1 cm past the radius from outside: not in");
    I.p[1] = s;
    step();
    CHECK(O.inside[0] && (O.ev[0] & SOMVR_SL_E_ENTER), "back in: a tick");
    /* the draw */
    press(1);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DRAW) && O.want[0] == 1 && O.nograb && O.keep[0] == -1 && !O.blade, "a press: draw asked");
    game_follows();
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DONE) && O.want[0] == -1 && O.blade && O.keep[0] == 1 && O.nograb, "drawn: blade, keep");
    /* the grip let go in the slot right after the draw: not a drop-in */
    open_(1);
    step();
    CHECK(!(O.ev[0] & SOMVR_SL_E_PUT) && O.blade && !O.nograb, "released after the draw: still in the hand");
    /* stays in the hand away from the slot (D14 a) */
    I.p[1] = (sv_vec3){ 30, 120, 50 };
    steps(90);
    CHECK(O.blade && O.keep[0] == 1 && S.want[0] == -1, "a second later, away: in the hand");
    /* a press away from the slot: nothing (not a slot press; the climb may have it) */
    press(1);
    step();
    CHECK(!O.ev[0] && !O.nograb && O.blade, "a press away: nothing");
    open_(1);
    step();
    /* the put-back by a press in the slot */
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    press(1);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_PUT) && O.want[0] == 0 && !O.blade && O.nograb, "a press in the slot: put back");
    game_follows();
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DONE) && !O.blade && O.keep[0] == 0, "put back");
    open_(1);
    step();
    CHECK(S.puts[0] == 1 && S.draws[0] == 1 && S.done[0] == 2, "totals %llu %llu %llu", (unsigned long long)S.puts[0],
          (unsigned long long)S.draws[0], (unsigned long long)S.done[0]);
}

static void test_dropin(void) {
    rig();
    I.drawn[0] = 1;
    step();
    /* the grip closed away from the slot (holding the sword), carried in, let go: put back */
    press(1);
    step();
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    CHECK(O.inside[0] && !(O.ev[0] & SOMVR_SL_E_PUT), "carried in closed: nothing yet");
    open_(1);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_PUT) && (O.ev[0] & SOMVR_SL_E_DROPIN) && O.want[0] == 0, "let go in the slot: put back");
    game_follows();
    step();
    /* the draw by a press in the slot, out with it held, back in held, let go: put back again */
    press(1);
    step();
    game_follows();
    step();
    CHECK(O.blade, "drawn again");
    I.p[1] = (sv_vec3){ 30, 120, 50 };
    steps(10);
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    open_(1);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DROPIN), "out and back in held, let go: dropped in");
    game_follows();
    step();
    CHECK(S.dropins[0] == 2, "drop-ins %llu", (unsigned long long)S.dropins[0]);
}

static void test_quick(void) {
    rig();
    step();
    press(1);
    step();
    CHECK(!O.ev[0] && !O.nograb, "the first press: nothing");
    open_(1);
    steps(10);
    press(1);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DRAW) && (O.ev[0] & SOMVR_SL_E_QUICK) && O.nograb && O.want[0] == 1,
          "the second within 0.35 s: quick draw");
    game_follows();
    step();
    CHECK(O.blade, "quick drawn");
    open_(1);
    steps(5);
    /* with the sword in the hand a double squeeze does nothing */
    press(1), step(), open_(1), steps(5), press(1), step();
    CHECK(!(O.ev[0] & SOMVR_SL_E_DRAW) && !(O.ev[0] & SOMVR_SL_E_PUT) && O.blade, "in the hand: nothing");
    open_(1);
    step();
    /* too slow */
    rig();
    step();
    press(1), step(), open_(1), steps(40), press(1), step();
    CHECK(!(EV[0] & SOMVR_SL_E_DRAW), "0.45 s apart: no draw");
    open_(1), steps(10), press(1), step();
    CHECK((O.ev[0] & SOMVR_SL_E_QUICK), "... but that one and the next");
    /* off */
    rig();
    C.quick_s = 0;
    step();
    press(1), step(), open_(1), steps(5), press(1), step();
    CHECK(!(EV[0] & SOMVR_SL_E_DRAW), "quick_s 0: no quick draw");
    /* the off hand's double squeeze is not a quick draw */
    rig();
    step();
    press(0), step(), open_(0), steps(5), press(0), step();
    CHECK(!EV[0] && !EV[1], "the off hand: nothing");
}

static void test_hold(void) {
    rig();
    C.hold = 1;
    step();
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    press(1);
    gsteps(2);
    CHECK(I.drawn[0] && O.blade, "hold: drawn");
    I.p[1] = (sv_vec3){ 30, 120, 50 };
    gsteps(30);
    CHECK(I.drawn[0], "held: in the hand");
    open_(1);
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_PUT) && (O.ev[0] & SOMVR_SL_E_HOLDEND), "the grip opened anywhere: put back");
    game_follows();
    step();
    CHECK(!I.drawn[0] && S.holdends[0] == 1, "back");
    /* a game draw in hold mode is not a hold draw: opening the grip leaves it */
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && O.keep[0] == 1, "the game drew it");
    press(1), step(), open_(1), step();
    CHECK(S.holdends[0] == 1 && O.keep[0] == 1 && I.drawn[0], "no hold end for the game's draw");
}

static void test_asks(void) {
    rig();
    step();
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    press(1);
    step();
    CHECK(O.want[0] == 1, "asked");
    steps(44); /* 0.49 s: still asked */
    CHECK(O.want[0] == 1 && !(EV[0] & SOMVR_SL_E_DENIED), "within ask_s");
    steps(2);
    CHECK((EV[0] & SOMVR_SL_E_DENIED) && O.want[0] == -1 && S.denied[0] == 1 && O.keep[0] == 0, "denied after 0.5 s");
    open_(1);
    step();
    /* the game's own change (no re-assert here: test_reassert) */
    C.reassert = 0;
    EV[0] = 0;
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && O.keep[0] == 1 && O.blade && S.game[0] == 1, "the game drew it");
    I.drawn[0] = 0;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && O.keep[0] == 0 && !O.blade, "the game sheathed it");
    /* no live input: the ask dropped, no preference, no events */
    press(1);
    step();
    CHECK(O.want[0] == 1, "asked again");
    open_(1);
    I.live = 0;
    step();
    CHECK(O.want[0] == -1 && O.keep[0] == -1 && O.keep[1] == -1 && !(O.ev[0] & ~(SOMVR_SL_E_LEAVE | SOMVR_SL_E_ENTER)),
          "not live: dropped");
    I.live = 1, I.allow = 0;
    press(1);
    step();
    CHECK(O.want[0] == -1 && !O.ev[0] && !O.nograb, "not allowed: a press in the slot does nothing");
    open_(1);
    I.allow = 1;
    step();
    /* no sword: nothing at its slot */
    I.have[0] = 0;
    step();
    press(1);
    step();
    CHECK(!O.ev[0] && O.keep[0] == -1 && !O.blade && O.dist[0] < 0, "no sword item: no slot");
}

static void test_reassert(void) {
    rig();
    I.drawn[0] = 1;
    steps(2);
    CHECK(O.keep[0] == 1, "kept in the hand");
    I.drawn[0] = 0; /* the game's own sheathe (FUN_023115f0, a state change) */
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && (O.ev[0] & SOMVR_SL_E_DRAW) && (O.ev[0] & SOMVR_SL_E_REASSERT) && O.want[0] == 1,
          "the game took it: drawn again");
    game_follows();
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DONE) && O.keep[0] == 1, "back in the hand");
    I.drawn[0] = 0;
    gsteps(2);
    CHECK(I.drawn[0] && S.reasserts[0] == 2, "the second time: again");
    steps(5);
    I.drawn[0] = 0;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_INSIST) && !(O.ev[0] & SOMVR_SL_E_DRAW) && O.want[0] == -1 && O.keep[0] == 0 && !O.blade,
          "the third time within 3 s: the game's state stands");
    steps(300); /* 3.3 s later the player draws it; the game takes it again: re-asserted */
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    press(1);
    gsteps(2);
    open_(1);
    I.p[1] = (sv_vec3){ 30, 100, 20 };
    gsteps(3);
    CHECK(I.drawn[0] && O.keep[0] == 1, "drawn by the player");
    I.drawn[0] = 0;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_REASSERT), "a new window: re-asserted");
    game_follows();
    step();
    /* busy (the bow): the game's sheathe is not fought */
    I.busy[0] = I.busy[1] = 1;
    step();
    I.drawn[0] = 0;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && !(O.ev[0] & SOMVR_SL_E_REASSERT) && O.keep[0] == -1, "busy: accepted");
    /* off */
    rig();
    C.reassert = 0;
    I.drawn[0] = 1;
    steps(2);
    I.drawn[0] = 0;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && !(O.ev[0] & SOMVR_SL_E_DRAW) && O.keep[0] == 0, "reassert off: accepted");
    /* the game's own draw (a finisher) is kept from then on */
    rig();
    step();
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && O.keep[0] == 1, "the game's draw: now kept");
    /* the dagger is not re-asserted (its state is ours: a load forgets it) */
    rig();
    I.drawn[1] = 1;
    steps(2);
    CHECK(O.keep[1] == 1, "the dagger kept");
    I.drawn[1] = 0;
    step();
    CHECK((O.ev[1] & SOMVR_SL_E_GAME) && !(O.ev[1] & SOMVR_SL_E_DRAW) && O.keep[1] == 0, "the dagger: not re-asserted");
}

/* build 10: the game's own draw of a sword the player keeps away (headset session 3: a spawned orc, the combat stance
 * drew Urfael) is put back with undraw; without it the draw is kept (the default) */
static void test_undraw(void) {
    rig();
    C.undraw = 1;
    steps(2);
    CHECK(O.keep[0] == 0 && !O.blade, "kept away");
    I.drawn[0] = 1; /* the game's draw (a path the hook cannot refuse) */
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && (O.ev[0] & SOMVR_SL_E_PUT) && (O.ev[0] & SOMVR_SL_E_UNDRAW) && O.want[0] == 0 &&
              !O.blade && S.undraws[0] == 1 && S.reasserts[0] == 0,
          "the game drew it: put back (ev %#x want %d)", O.ev[0], O.want[0]);
    game_follows();
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_DONE) && O.keep[0] == 0 && !O.blade, "back at the slot");
    I.drawn[0] = 1;
    gsteps(2);
    CHECK(!I.drawn[0] && S.undraws[0] == 2, "the second time: again");
    steps(5);
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_INSIST) && !(O.ev[0] & SOMVR_SL_E_PUT) && O.want[0] == -1 && O.keep[0] == 1 && O.blade,
          "the third time within 3 s: the game's state stands");
    steps(300); /* 3.3 s later: put back by the player, then the game draws it: a new window */
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    press(1);
    gsteps(2);
    open_(1);
    I.p[1] = (sv_vec3){ 30, 100, 20 };
    gsteps(3);
    CHECK(!I.drawn[0] && O.keep[0] == 0, "put back by the player");
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_UNDRAW) && O.want[0] == 0, "a new window: put back again");
    game_follows();
    step();
    /* the player's own draw is never undone */
    steps(300);
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    step();
    press(1);
    gsteps(2);
    open_(1);
    I.p[1] = (sv_vec3){ 30, 100, 20 };
    gsteps(3);
    CHECK(I.drawn[0] && O.keep[0] == 1 && O.blade && S.undraws[0] == 3,
          "the player's draw stands (undraws %llu)", (unsigned long long)S.undraws[0]);
    gsteps(30);
    CHECK(I.drawn[0] && O.blade, "still in the hand");
    /* busy (the bow): the game's draw is not fought */
    rig();
    C.undraw = 1;
    steps(2);
    I.busy[0] = I.busy[1] = 1;
    step();
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && !(O.ev[0] & SOMVR_SL_E_UNDRAW), "busy: accepted");
    /* off (the default): kept */
    rig();
    steps(2);
    I.drawn[0] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_GAME) && !(O.ev[0] & SOMVR_SL_E_PUT) && O.keep[0] == 1 && S.undraws[0] == 0,
          "undraw off: the game's draw is kept");
    /* the dagger is never undrawn */
    rig();
    C.undraw = 1;
    steps(2);
    I.drawn[1] = 1;
    step();
    CHECK((O.ev[1] & SOMVR_SL_E_GAME) && !(O.ev[1] & SOMVR_SL_E_PUT) && O.keep[1] == 1, "the dagger: not undrawn");
}

static void test_reset(void) {
    rig();
    I.drawn[0] = 1;
    steps(2);
    I.busy[0] = I.busy[1] = I.away[0] = I.away[1] = 1;
    gsteps(2);
    CHECK(!I.drawn[0] && S.suspended[0], "suspended");
    float torso = S.torso;
    uint64_t puts = S.puts[0];
    somvr_slots_reset(&S);
    I.busy[0] = I.busy[1] = I.away[0] = I.away[1] = 0;
    gsteps(3);
    CHECK(!I.drawn[0] && !(EV[0] & SOMVR_SL_E_RESTORE) && S.puts[0] == puts && S.torso == torso, "reset: no restore");
    I.drawn[0] = 1;
    steps(2);
    somvr_slots_reset(&S);
    I.drawn[0] = 0; /* a load: the new inventory's sword on the back */
    EV[0] = 0;
    step();
    CHECK(!(EV[0] & (SOMVR_SL_E_GAME | SOMVR_SL_E_REASSERT)) && O.keep[0] == 0, "reset: no re-assert");
}

static void test_busy(void) {
    /* the bow out: both hands busy and away; the sword and the dagger go back; after it they return */
    rig();
    I.drawn[0] = I.drawn[1] = 1;
    step();
    I.busy[0] = I.busy[1] = I.away[0] = I.away[1] = 1;
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_PUT) && (O.ev[0] & SOMVR_SL_E_SUSPEND) && (O.ev[1] & SOMVR_SL_E_SUSPEND) && O.want[0] == 0 &&
              O.want[1] == 0 && !O.blade && O.keep[0] == -1,
          "the bow: both put back");
    game_follows();
    steps(30);
    CHECK(!I.drawn[0] && !I.drawn[1] && O.keep[0] == -1, "away while the bow is out");
    /* presses in the slot while busy: nothing */
    hand_to_slot(1, SOMVR_SLOT_SWORD);
    press(1);
    step();
    CHECK(!(O.ev[0] & SOMVR_SL_E_DRAW) && !O.nograb, "busy: no draw");
    open_(1);
    I.busy[0] = I.busy[1] = I.away[0] = I.away[1] = 0;
    I.p[1] = (sv_vec3){ 30, 100, 20 };
    step();
    CHECK((O.ev[0] & SOMVR_SL_E_RESTORE) && (O.ev[1] & SOMVR_SL_E_RESTORE) && O.want[0] == 1 && O.want[1] == 1,
          "after the bow: both back to the hands");
    game_follows();
    step();
    CHECK(I.drawn[0] && I.drawn[1] && O.blade && S.restores[0] == 1, "restored");
    /* restore off */
    rig();
    C.restore = 0;
    I.drawn[0] = 1;
    step();
    I.busy[0] = I.busy[1] = I.away[0] = I.away[1] = 1;
    gsteps(3);
    I.busy[0] = I.busy[1] = I.away[0] = I.away[1] = 0;
    gsteps(3);
    CHECK(!I.drawn[0] && !(EV[0] & SOMVR_SL_E_RESTORE), "restore off: the hand stays empty");
    /* a climb: the left hand grabs (both busy, the left away): the sword stays; the right grabs: it goes back; the climb
     * ends: it returns */
    rig();
    I.drawn[0] = 1;
    step();
    I.busy[0] = I.busy[1] = 1, I.away[0] = 1;
    gsteps(5);
    CHECK(I.drawn[0] && !(EV[0] & SOMVR_SL_E_PUT), "the off hand climbs: the sword stays");
    I.away[1] = 1;
    gsteps(2);
    CHECK(!I.drawn[0] && (EV[0] & SOMVR_SL_E_SUSPEND), "the sword hand grabs: put back");
    I.away[1] = 0;
    gsteps(5);
    CHECK(!I.drawn[0], "it lets go, the climb goes on: still away");
    I.busy[0] = I.busy[1] = I.away[0] = 0;
    gsteps(2);
    CHECK(I.drawn[0] && (EV[0] & SOMVR_SL_E_RESTORE), "the climb ends: back in the hand");
    /* a suspended weapon the player draws by hand meanwhile is no longer suspended */
    rig();
    I.drawn[0] = 1;
    step();
    I.away[1] = 1, I.busy[1] = 1;
    gsteps(2);
    I.away[1] = 0, I.busy[1] = 0;
    I.live = 0;
    gsteps(3);
    CHECK(!I.drawn[0] && S.suspended[0], "not live: the restore waits");
    I.live = 1;
    gsteps(2);
    CHECK(I.drawn[0], "live again: restored");
}

static void test_dagger(void) {
    rig();
    step();
    /* the main hand at the hip: nothing; the off hand: the tick and the draw */
    hand_to_slot(1, SOMVR_SLOT_DAGGER);
    step();
    press(1);
    step();
    CHECK(!O.ev[1] && !O.inside[1], "the main hand at the hip: nothing");
    open_(1);
    I.p[1] = (sv_vec3){ 30, 100, 20 };
    hand_to_slot(0, SOMVR_SLOT_DAGGER);
    step();
    CHECK((O.ev[1] & SOMVR_SL_E_ENTER) && O.inside[1], "the off hand: the tick");
    press(0);
    step();
    CHECK((O.ev[1] & SOMVR_SL_E_DRAW) && O.want[1] == 1 && O.nograb && !O.ev[0], "the dagger asked");
    game_follows();
    step();
    CHECK(I.drawn[1] && O.keep[1] == 1 && !O.blade, "the dagger drawn (not the sword's blade)");
    open_(0);
    step();
    press(0);
    step();
    CHECK((O.ev[1] & SOMVR_SL_E_PUT) && O.want[1] == 0, "a press at the hip: put back");
    game_follows();
    open_(0);
    step();
    /* the back slot */
    rig();
    C.dagger_at = SOMVR_SLOTS_DAGGER_BACK;
    step();
    I.p[0] = (sv_vec3){ -8, 105, -22 };
    step();
    CHECK(O.inside[1] && (O.ev[1] & SOMVR_SL_E_ENTER), "the back slot");
    /* off */
    rig();
    C.dagger_at = SOMVR_SLOTS_DAGGER_OFF;
    step();
    I.p[0] = (sv_vec3){ -18, 98, -5 };
    press(0);
    step();
    CHECK(!O.inside[1] && !O.ev[1] && O.dist[1] < 0, "the dagger slot off");
    /* left-handed: the sword at the left shoulder with the left hand, the dagger at the right hip */
    rig();
    C.main_hand = 0;
    step();
    I.p[0] = (sv_vec3){ -10, 150, -15 };
    step();
    press(0);
    step();
    CHECK(O.inside[0] && (O.ev[0] & SOMVR_SL_E_DRAW), "left-handed: the left shoulder");
    open_(0);
    I.p[1] = (sv_vec3){ 18, 98, -5 };
    step();
    CHECK(O.inside[1] && (O.ev[1] & SOMVR_SL_E_ENTER), "left-handed: the right hip");
}

static void test_robust(void) {
    rig();
    step();
    I.p[1] = (sv_vec3){ NAN, 0, 0 };
    I.squeeze[1] = NAN;
    I.head_q = (sv_quat){ NAN, 0, 0, 1 };
    step();
    CHECK(!O.inside[0] && !O.ev[0] && isfinite(O.torso_yaw), "NaN hand, squeeze, head: nothing");
    I.head_q = (sv_quat){ 0, 0, 0, 1 };
    I.valid[1] = 0;
    I.p[1] = (sv_vec3){ 10, 150, -15 };
    I.squeeze[1] = 1;
    step();
    CHECK(!O.inside[0] && !(O.ev[0] & SOMVR_SL_E_DRAW), "an invalid hand at the slot: nothing");
    I.valid[1] = 1, I.squeeze[1] = 0;
    steps(2);
    CHECK(O.inside[0], "valid again: in");
    /* the totals */
    CHECK(S.frames > 0, "frames");
}

int main(void) {
    test_geometry();
    test_torso_rule();
    test_draw_put();
    test_dropin();
    test_quick();
    test_hold();
    test_asks();
    test_reassert();
    test_undraw();
    test_reset();
    test_busy();
    test_dagger();
    test_robust();
    printf("test_slots: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
