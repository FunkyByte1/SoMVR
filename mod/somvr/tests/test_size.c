/* test_size.c - the player size (docs/re-xr-pipeline.md 35): somvr_size.c (the percent, the lift, the blink and the
 * landing, the steps of a ground profile) and the size in every map that takes tracking space to the world: the rig's
 * eyes, head, follow and hands (somvr_eyes.c, somvr_hands.c), the controller blade (somvr_combat.c: the sword's world
 * length kept), the arms' grip <-> model space (somvr_arms.c), the comfort map (somvr_comfort.c) and the threat glows'
 * way back (somvr_threat.c). The size about Talion's feet is a similarity: from the eyes every tracked point keeps its
 * direction, distances grow by s, the eye separation too, so the hands stay where the controllers are. */
#include "../somvr_size.h"
#include "../somvr_arms.h"
#include "../somvr_combat.h"
#include "../somvr_comfort.h"
#include "../somvr_eyes.h"
#include "../somvr_hands.h"
#include "../somvr_threat.h"

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

static sv_vec3 sub(sv_vec3 a, sv_vec3 b) { return (sv_vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static float len(sv_vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

/* ---- the pure part ---------------------------------------------------------------------------------------------- */
static void test_pure(void) {
    CHECK(somvr_size_of_pct(115) == 1.15f && somvr_size_of_pct(90) == 1.0f && somvr_size_of_pct(170) == 1.4f &&
              somvr_size_of_pct(NAN) == 1.0f,
          "percent: clamped to 100 .. 140, NaN 100");
    CHECK(NEAR(somvr_size_lift(1.3f, 180.6f), 54.18, 1e-3) && somvr_size_lift(1.0f, 180.6f) == 0 &&
              somvr_size_lift(NAN, 180) == 0,
          "the lift: (s - 1) x the eye height (%g)", somvr_size_lift(1.3f, 180.6f));
    float h[12] = {0, 0, 0.5f, 17, 17, 17.5f, 34, 34, NAN, 34, 20, 20};
    float st[4];
    int at[4], n = somvr_size_steps(h, 12, 5, st, at, 4);
    CHECK(n == 3 && NEAR(st[0], 16.5, 1e-4) && at[0] == 2 && NEAR(st[1], 16.5, 1e-4) && at[1] == 5 &&
              NEAR(st[2], -14, 1e-4) && at[2] == 9,
          "steps: two risers of 16.5 cm and a 14 cm drop (n %d: %g@%d %g@%d %g@%d)", n, st[0], at[0], st[1], at[1], st[2],
          at[2]);
    CHECK(somvr_size_steps(h, 12, 5, st, at, 1) == 3 && NEAR(st[0], 16.5, 1e-4), "more than cap: counted, cap stored");
}

/* the blink: a change waits for the black (a third of 350 ms), lands there, fades back in; a hold keeps it back */
static void test_blink(void) {
    somvr_size_cfg c;
    somvr_size_cfg_default(&c);
    somvr_blink_times(350, &c.out_s, &c.in_s);
    somvr_size z;
    somvr_size_init(&z, 1.15f);
    CHECK(NEAR(z.cur, 1.15, 1e-6) && NEAR(z.want, 1.15, 1e-6) && !z.lands, "the start: 115 %% at once, no blink");
    double t = 100;
    float a = somvr_size_frame(&z, &c, 0, t);
    CHECK(a == 0 && !z.landed, "nothing asked: no black");
    CHECK(somvr_size_ask(&z, 1.3f) && !somvr_size_ask(&z, 1.3f) && z.asks == 1, "an ask counts once");
    int frames = 0, landed_at = -1;
    float amax = 0, aland = 0;
    for (int i = 0; i < 90; i++) {
        t += 1.0 / 90;
        a = somvr_size_frame(&z, &c, 0, t);
        if (a > amax) amax = a;
        if (z.landed) landed_at = i, aland = a, frames++;
        if (landed_at < 0) CHECK(NEAR(z.cur, 1.15, 1e-6), "frame %d: the old size until the black", i);
    }
    CHECK(frames == 1 && landed_at >= 9 && landed_at <= 11 && aland == 1.0f && NEAR(z.cur, 1.3, 1e-6) && z.lands == 1 &&
              NEAR(z.from, 1.15, 1e-6),
          "one landing at frame %d (~117 ms) on full black (%g)", landed_at, aland);
    CHECK(a == 0 && amax == 1.0f, "faded back in (%g)", a);
    /* a hold (a climb hold): the fade-out stops and comes back, nothing lands; after it the change lands */
    somvr_size_ask(&z, 1.0f);
    for (int i = 0; i < 5; i++) t += 1.0 / 90, somvr_size_frame(&z, &c, 0, t);
    float mid = z.alpha;
    for (int i = 0; i < 60; i++) {
        t += 1.0 / 90;
        somvr_size_frame(&z, &c, 1, t);
        CHECK(!z.landed, "held: no landing");
    }
    CHECK(mid > 0 && mid < 1 && z.alpha == 0 && NEAR(z.cur, 1.3, 1e-6) && z.held_frames == 60,
          "the hold: from %g back to 0, still 130 %%", mid);
    int l = 0;
    for (int i = 0; i < 40; i++) t += 1.0 / 90, somvr_size_frame(&z, &c, 0, t), l += z.landed;
    CHECK(l == 1 && NEAR(z.cur, 1.0, 1e-6), "released: it lands (%d)", l);
    /* no blink configured: it lands at once */
    somvr_size_cfg c0 = {0, 0};
    somvr_size_ask(&z, 1.2f);
    a = somvr_size_frame(&z, &c0, 0, t + 1);
    CHECK(z.landed && a == 0 && NEAR(z.cur, 1.2, 1e-6), "no blink: at once");
}

/* ---- the rig ------------------------------------------------------------------------------------------------------ */
static void pose(somvr_posef *p, float x, float y, float z, float yaw) {
    p->p[0] = x, p->p[1] = y, p->p[2] = z;
    p->q[0] = 0, p->q[1] = sinf(yaw / 2), p->q[2] = 0, p->q[3] = cosf(yaw / 2);
}

static void test_rig(void) {
    somvr_rig_cfg c = {.layout = SOMVR_LAYOUT_B, .base = SOMVR_BASE_HEAD, .snap_rad = 0.5235988f};
    somvr_rig r;
    somvr_rig_init(&r);
    CHECK(somvr_rig_scale(&r) == 1.0f, "a zeroed rig: 1");
    r.scale = NAN;
    CHECK(somvr_rig_scale(&r) == 1.0f, "NaN: 1");
    r.scale = 3;
    CHECK(somvr_rig_scale(&r) == 1.0f, "3: not a size, 1");
    r.scale = 0;
    /* the head 0.1 m up in LOCAL, the eyes 6.8 cm apart, turned 30 deg left */
    somvr_posef eye[2];
    pose(&eye[0], -0.034f, 0.10f, 0, 0), pose(&eye[1], 0.034f, 0.10f, 0, 0);
    somvr_view game = {{0, 0, 0}, {0, 0, 0, 1}, 1.2f, 1.0f, 10, 0};
    const float H = 180.6f, root[3] = {1000, 2000 + 76.6f, -500}; /* the anchor's eye point (root + 76.6) */
    somvr_base b = somvr_rig_frame(&r, &c, eye, &game, root, 1, 0);
    somvr_fovf fov = {-0.8f, 0.83f, 0.88f, -0.9f};
    somvr_eye_out o1[2], o2[2];
    for (int e = 0; e < 2; e++) somvr_rig_eye(&r, &c, b, &game, &eye[e], &fov, e, &o1[e]);
    /* a later frame: the head 20 cm forward, 5 up, the left hand 40 down / 30 ahead / 20 left */
    pose(&eye[0], -0.034f, 0.15f, -0.20f, 0), pose(&eye[1], 0.034f, 0.15f, -0.20f, 0);
    somvr_hand hand = {0};
    hand.valid = 1;
    pose(&hand.pose, -0.20f, -0.30f, -0.50f, 0);
    hand.lin[0] = 1.0f, hand.lin[1] = 0, hand.lin[2] = -2.0f;
    sv_vec3 hp, hv;
    sv_quat hq;
    somvr_hand_game(&hand, &hp, &hq, &hv, NULL);
    sv_vec3 e1[2], e2[2], w1, w2, v1, v2, h1, h2;
    sv_quat q1;
    for (int e = 0; e < 2; e++) somvr_rig_eye(&r, &c, b, &game, &eye[e], &fov, e, &o1[e]);
    for (int e = 0; e < 2; e++) e1[e] = (sv_vec3){o1[e].view.pos[0], o1[e].view.pos[1], o1[e].view.pos[2]};
    w1 = somvr_rig_point(&r, b, hp), v1 = somvr_rig_vec(&r, b, hv);
    somvr_rig_head(&r, b, eye, &h1, &q1);
    sv_vec3 off1 = somvr_rig_offset(&r, b, eye);
    /* 130 %: the base rises by 0.3 x the eye height above the feet, the tracked offsets x 1.3 */
    const float S = 1.3f;
    r.scale = S;
    somvr_base b2 = b;
    b2.pos.y += somvr_size_lift(S, H);
    for (int e = 0; e < 2; e++) somvr_rig_eye(&r, &c, b2, &game, &eye[e], &fov, e, &o2[e]);
    for (int e = 0; e < 2; e++) e2[e] = (sv_vec3){o2[e].view.pos[0], o2[e].view.pos[1], o2[e].view.pos[2]};
    w2 = somvr_rig_point(&r, b2, hp), v2 = somvr_rig_vec(&r, b2, hv);
    somvr_rig_head(&r, b2, eye, &h2, &q1);
    sv_vec3 off2 = somvr_rig_offset(&r, b2, eye);
    CHECK(NEAR(len(sub(e1[1], e1[0])), 6.8, 1e-3) && NEAR(len(sub(e2[1], e2[0])), 6.8 * S, 1e-3),
          "the eye separation: %g -> %g cm", len(sub(e1[1], e1[0])), len(sub(e2[1], e2[0])));
    float feet = root[1] - H; /* the feet stay: the head's world height above them x s */
    CHECK(NEAR(h2.y - feet, S * (h1.y - feet), 1e-3), "the eyes above the feet x 1.3: %g -> %g", h1.y - feet, h2.y - feet);
    CHECK(NEAR(h1.y - root[1], 5, 1e-3) && NEAR(h2.y - root[1], 0.3f * H + 5 * S, 1e-3),
          "the head 5 cm over the recentre's: %g at 100 %%, %g at 130 %%", h1.y - root[1], h2.y - root[1]);
    CHECK(NEAR(len(sub(h2, b2.pos)), S * len(sub(h1, b.pos)), 1e-3) && NEAR(len(off2), S * len(off1), 1e-3),
          "the head's room motion x 1.3 (%g -> %g), the follow's offset too", len(sub(h1, b.pos)), len(sub(h2, b2.pos)));
    sv_vec3 d1 = sub(w1, h1), d2 = sub(w2, h2);
    float l1 = len(d1), l2 = len(d2);
    CHECK(NEAR(l2, S * l1, 1e-3) && NEAR(d1.x / l1, d2.x / l2, 1e-5) && NEAR(d1.y / l1, d2.y / l2, 1e-5) &&
              NEAR(d1.z / l1, d2.z / l2, 1e-5),
          "the hand from the eyes: the same direction, x 1.3 as far (%g -> %g), as the eye separation: it stays at the "
          "controller", l1, l2);
    CHECK(NEAR(len(v2), S * len(v1), 1e-3), "the hand's velocity x 1.3 (%g -> %g cm/s)", len(v1), len(v2));
    /* the pivot: a tracked point H below the recentre's head (the floor of a player with Talion's eye height) lands on
     * his feet at every size */
    sv_vec3 fl = {r.p0.x, r.p0.y - H, r.p0.z};
    sv_vec3 fw = somvr_rig_point(&r, b2, fl), fw1;
    r.scale = 0;
    fw1 = somvr_rig_point(&r, b, fl);
    r.scale = S;
    CHECK(NEAR(fw.y, feet, 1e-3) && NEAR(fw1.y, feet, 1e-3), "H below the head: the feet at 130 and 100 %% (%g, %g vs %g)",
          fw.y, fw1.y, feet);
    /* the follow's shift cancels a body move: P0 moves by applied / s, the head's world point back by applied */
    sv_vec3 before = somvr_rig_offset(&r, b2, eye);
    somvr_rig_shift(&r, b2, (sv_vec3){13, 0, -26});
    sv_vec3 after = somvr_rig_offset(&r, b2, eye);
    CHECK(NEAR(after.x, before.x - 13, 1e-3) && NEAR(after.z, before.z + 26, 1e-3),
          "the shift at 130 %%: the offset moves by the applied (%g %g -> %g %g)", before.x, before.z, after.x, after.z);
}

/* ---- the blade, the arms, the comfort map, the threat glows -------------------------------------------------------- */
static void test_maps(void) {
    sc_blade_cfg bc;
    sc_blade_cfg_default(&bc);
    sc_grip g = {.valid = 1, .pos = {10, -30, 40}, .quat = {0, 0, 0, 1}, .xr_t = 5.0};
    sc_blade_hist h1 = {0}, h2 = {0};
    sc_rigxf r1 = {{100, 200, 300}, {0, 0, 0}, 0.7f, 0}, r2 = r1;
    r2.s = 1.3f;
    somvr_blade b1, b2;
    CHECK(somvr_blade_controller(&bc, &g, &r1, 1.0, &h1, &b1) && somvr_blade_controller(&bc, &g, &r2, 1.0, &h2, &b2),
          "two blades");
    float L1 = len(sub((sv_vec3){b1.tip[0], b1.tip[1], b1.tip[2]}, (sv_vec3){b1.base[0], b1.base[1], b1.base[2]}));
    float L2 = len(sub((sv_vec3){b2.tip[0], b2.tip[1], b2.tip[2]}, (sv_vec3){b2.base[0], b2.base[1], b2.base[2]}));
    CHECK(NEAR(L1, bc.back_cm + bc.len_cm, 1e-3) && NEAR(L2, L1, 1e-3),
          "the blade keeps the sword's world length at 130 %% (%g, %g)", L1, L2);
    /* the grip point = the hand's world point (the rig's map x 1.3) */
    float gp2[3], ax[3];
    for (int i = 0; i < 3; i++) ax[i] = (b2.tip[i] - b2.base[i]) / L2, gp2[i] = b2.base[i] + ax[i] * bc.back_cm;
    float c = cosf(0.7f), s = sinf(0.7f), dx = 1.3f * 10, dy = 1.3f * -30, dz = 1.3f * 40;
    CHECK(NEAR(gp2[0], 100 + c * dx + s * dz, 1e-2) && NEAR(gp2[1], 200 + dy, 1e-2) && NEAR(gp2[2], 300 - s * dx + c * dz, 1e-2),
          "the grip at the hand's world point (%g %g %g)", gp2[0], gp2[1], gp2[2]);
    /* a move: the tip's velocity x 1.3 (tracking deltas) */
    sc_grip g2 = g;
    g2.pos[0] += 2, g2.xr_t += 0.01;
    somvr_blade_controller(&bc, &g2, &r1, 1.01, &h1, &b1), somvr_blade_controller(&bc, &g2, &r2, 1.01, &h2, &b2);
    float s1 = sqrtf(b1.vel_tip[0] * b1.vel_tip[0] + b1.vel_tip[2] * b1.vel_tip[2]),
          s2 = sqrtf(b2.vel_tip[0] * b2.vel_tip[0] + b2.vel_tip[2] * b2.vel_tip[2]);
    CHECK(NEAR(s1, 200, 1e-1) && NEAR(s2, 1.3 * 200, 2e-1), "the tip's speed: %g -> %g cm/s", s1, s2);
    sc_rigxf r7 = r1;
    r7.s = 9;
    CHECK(sc_rig_scale(&r7) == 1.0f && sc_rig_scale(&r1) == 1.0f && sc_rig_scale(&r2) == 1.3f, "sc_rig_scale");

    /* the arms: grip -> model -> back to tracking with the size */
    sa_xf X = {{100, 150, 300}, sv_qnormalize((sv_quat){0, 0.3f, 0, 1})}, grip = {{10, -30, 40}, {0, 0, 0, 1}};
    sa_xf m = sa_grip_to_model(&r2, (sv_vec3){0, 0, 0}, X, grip);
    sa_xf w = sa_xf_mul(X, m), back = sa_world_to_track(&r2, w);
    CHECK(NEAR3(back.p, 10, -30, 40, 1e-3), "the arms' map and its inverse at 130 %% (%g %g %g)", back.p.x, back.p.y,
          back.p.z);
    CHECK(NEAR(w.p.x, 100 + c * dx + s * dz, 1e-2) && NEAR(w.p.y, 200 + dy, 1e-2), "the grip's world point as the blade's");

    /* the comfort map, the threat's way back */
    somvr_cf_map cm = {{100, 200, 300}, 0.7f, {0, 10, 0}, 1.3f};
    float hh[3] = {10, 10 - 30 / 1.3f, 40}, ow[3];
    somvr_cf_map_apply(&cm, hh, ow);
    CHECK(NEAR(ow[1], 170, 1e-3) && NEAR(ow[0], 100 + c * 13 + s * 52, 1e-2), "the comfort map x 1.3 (%g %g)", ow[0], ow[1]);
    somvr_th_map tm = {{100, 200, 300}, 0.7f, {0, 10, 0}, 1.3f};
    float xr[3];
    somvr_th_to_xr(&tm, ow, xr);
    CHECK(NEAR(xr[0], 0.10, 1e-4) && NEAR(xr[1], hh[1] * 0.01, 1e-4) && NEAR(xr[2], -0.40, 1e-4),
          "the glow's world point back to the room at 130 %% (%g %g %g m)", xr[0], xr[1], xr[2]);
    somvr_cf_map c0 = {{0, 0, 0}, 0, {0, 0, 0}, 0};
    float p1[3] = {5, 6, 7}, q[3];
    somvr_cf_map_apply(&c0, p1, q);
    CHECK(q[0] == 5 && q[1] == 6 && q[2] == 7, "an old caller's zero size: 1");
}

int main(void) {
    test_pure();
    test_blink();
    test_rig();
    test_maps();
    printf("test_size: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
