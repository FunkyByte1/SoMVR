/* test_bow.c - unit tests of somvr_bow.c (SOMVR_BOW=1, the VR bow): the geometry (the nock point, the draw direction,
 * its weight, the ray's direction blended from the bow's forward, the free-aim quaternion with no roll, the bow hand's
 * shortest-arc alignment) and the gesture state machine at 90 Hz: entering by the draw (a press at the nock, a press
 * moved into it inside the window, a press out of it = nothing / a MISS while aiming) or by the button, the draw (armed,
 * full, the tension ticks), the ray (origin at the nock, from the string hand to the bow hand), the release (the ray
 * latched, the fire command once the game's aim is on and the aim held long enough, the tap's window, the input
 * tick's commands, a shot dropped without the game's aim), the let-down, follow-through and a second shot, the exit when
 * the hands separate (the button holds the aim), the stand-downs (input, game, the bow hand lost, from outside; brief
 * losses ride through), hold mode, the right-handed swap, the nock offset, NaN inputs. P9: the default commands (124
 * aims, no fire command: 125 is the stealth mode), our own shot's gates in order, its arrow cost, its fire transform. */
#include "../somvr_bow.h"

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

static float len3(sv_vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static float dot3(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static sv_vec3 unit(sv_vec3 v) {
    float l = len3(v);
    return (sv_vec3){ v.x / l, v.y / l, v.z / l };
}
static sv_quat qaxis(float x, float y, float z, float deg) {
    sv_vec3 a = unit((sv_vec3){ x, y, z });
    float h = 0.5f * deg * 0.01745329252f;
    return (sv_quat){ a.x * sinf(h), a.y * sinf(h), a.z * sinf(h), cosf(h) };
}

/* ---- the geometry ------------------------------------------------------------------------------------------------ */
static void test_geometry(void) {
    somvr_bow_cfg c;
    somvr_bow_cfg_default(&c);
    CHECK(c.bow_hand == 0 && c.nock_r_cm == 15 && c.min_draw_cm == 20 && c.full_draw_cm == 55 && c.exit_cm == 40 &&
              NEAR(c.exit_s, 0.8, 1e-6) && NEAR(c.aim_min_s, 0.25, 1e-6) && NEAR(c.fire_s, 0.1, 1e-6) &&
              NEAR(c.wait_s, 0.6, 1e-6) && NEAR(c.follow_s, 0.4, 1e-6) && c.fire_mode == SOMVR_BOW_FIRE_TAP &&
              c.idle_ray == SOMVR_BOW_IDLE_BOW && c.tick_cm == 4 && c.dir_min_cm == 5 && c.dir_blend_cm == 10,
          "defaults");
    CHECK(!strcmp(somvr_bow_phase_name(SOMVR_BOW_PH_DRAW), "draw") && !strcmp(somvr_bow_end_name(SOMVR_BOW_END_APART), "apart") &&
              !strcmp(somvr_bow_end_name(99), "?") && !strcmp(somvr_bow_phase_name(-1), "?"),
          "names");
    /* the nock: the grip + its offset turned with it */
    sv_vec3 n = somvr_bow_nock(&c, (sv_vec3){ 10, 20, 30 }, (sv_quat){ 0, 0, 0, 1 });
    CHECK(NEAR3(n, 10, 20, 30, 1e-6), "nock without offset %g %g %g", n.x, n.y, n.z);
    c.nock_ofs[0] = 0, c.nock_ofs[1] = 5, c.nock_ofs[2] = 2;
    n = somvr_bow_nock(&c, (sv_vec3){ 10, 20, 30 }, sv_qyaw(1.5707963f)); /* turned 90 deg right: +Z -> +X */
    CHECK(NEAR3(n, 12, 25, 30, 1e-4), "nock with offset %g %g %g", n.x, n.y, n.z);
    c.nock_ofs[0] = c.nock_ofs[1] = c.nock_ofs[2] = 0;
    /* the draw's direction: string -> nock */
    float l = -1;
    sv_vec3 d = somvr_bow_draw_dir((sv_vec3){ 0, 0, 50 }, (sv_vec3){ 0, 0, 20 }, &l);
    CHECK(NEAR3(d, 0, 0, 1, 1e-6) && NEAR(l, 30, 1e-5), "draw dir %g %g %g len %g", d.x, d.y, d.z, l);
    d = somvr_bow_draw_dir((sv_vec3){ 30, 40, 0 }, (sv_vec3){ 0, 0, 0 }, &l);
    CHECK(NEAR3(d, 0.6, 0.8, 0, 1e-6) && NEAR(l, 50, 1e-4), "draw dir 3-4-5");
    d = somvr_bow_draw_dir((sv_vec3){ 1, 1, 1 }, (sv_vec3){ 1, 1, 1 }, &l);
    CHECK(NEAR3(d, 0, 0, 1, 0) && l == 0, "draw dir zero");
    d = somvr_bow_draw_dir((sv_vec3){ NAN, 1, 1 }, (sv_vec3){ 1, 1, 1 }, NULL);
    CHECK(NEAR3(d, 0, 0, 1, 0), "draw dir NaN");
    /* the weight: 0 below 5, 1 from 15, smooth and monotonic in between */
    CHECK(somvr_bow_dir_weight(&c, 0) == 0 && somvr_bow_dir_weight(&c, 5) == 0 && somvr_bow_dir_weight(&c, 15) == 1 &&
              somvr_bow_dir_weight(&c, 60) == 1 && NEAR(somvr_bow_dir_weight(&c, 10), 0.5, 1e-6) &&
              somvr_bow_dir_weight(&c, NAN) == 0,
          "weight");
    float prev = -1;
    int mono = 1;
    for (float x = 0; x <= 20; x += 0.25f) {
        float w = somvr_bow_dir_weight(&c, x);
        mono &= w >= prev && w >= 0 && w <= 1;
        prev = w;
    }
    CHECK(mono, "weight monotonic");
    /* the ray's direction: the bow's forward at the nock, the string's from 15 cm, between in between */
    sv_quat bq = sv_qyaw(0.5f); /* the bow points 28.6 deg right */
    sv_vec3 bf = sv_qrot(bq, (sv_vec3){ 0, 0, 1 }), nk = { 0, 150, 0 };
    float dr;
    d = somvr_bow_ray_dir(&c, bq, nk, (sv_vec3){ 0, 150, -2 }, &dr);
    CHECK(NEAR3(d, bf.x, bf.y, bf.z, 1e-6) && NEAR(dr, 2, 1e-5), "ray dir at the nock = the bow's forward");
    d = somvr_bow_ray_dir(&c, bq, nk, (sv_vec3){ 0, 150, -30 }, &dr);
    CHECK(NEAR3(d, 0, 0, 1, 1e-6) && NEAR(dr, 30, 1e-5), "ray dir drawn = string -> nock");
    d = somvr_bow_ray_dir(&c, bq, nk, (sv_vec3){ 0, 150, -10 }, NULL);
    CHECK(NEAR(len3(d), 1, 1e-5) && d.x > 0 && d.x < bf.x && d.z > bf.z, "ray dir blended %g %g %g", d.x, d.y, d.z);
    d = somvr_bow_ray_dir(&c, sv_qyaw(3.14159265f), nk, (sv_vec3){ 0, 150, -10 }, NULL); /* the bow backwards: string wins */
    CHECK(NEAR(len3(d), 1, 1e-5) && NEAR3(d, 0, 0, 1, 1e-3), "ray dir opposite %g %g %g", d.x, d.y, d.z);
    /* build 16: a hand that nocks beside the nock (10 cm to the side, not back) has drawn nothing: the bow's forward */
    d = somvr_bow_ray_dir(&c, bq, nk, (sv_vec3){ 10 * cosf(0.5f), 150, -10 * sinf(0.5f) }, &dr);
    CHECK(NEAR3(d, bf.x, bf.y, bf.z, 1e-5) && NEAR(dr, 10, 1e-4) &&
              NEAR(somvr_bow_back(&c, bq, nk, (sv_vec3){ 10 * cosf(0.5f), 150, -10 * sinf(0.5f) }), 0, 1e-4),
          "ray dir beside the nock = the bow's forward %g %g %g", d.x, d.y, d.z);
    CHECK(NEAR(somvr_bow_back(&c, bq, nk, (sv_vec3){ -30 * bf.x, 150 - 30 * bf.y, -30 * bf.z }), 30, 1e-4), "back draw 30");
    /* the look quaternion: R (0,0,1) = dir, R (0,1,0) up in dir's vertical plane, many directions */
    int bad = 0;
    for (int yi = -6; yi <= 6; yi++)
        for (int pi = -4; pi <= 4; pi++) {
            float yaw = yi * 0.5f, pitch = pi * 0.35f;
            sv_vec3 dd = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
            sv_quat q = somvr_bow_look(dd, 0);
            sv_vec3 f = sv_qrot(q, (sv_vec3){ 0, 0, 1 }), u = sv_qrot(q, (sv_vec3){ 0, 1, 0 }),
                    r = sv_qrot(q, (sv_vec3){ 1, 0, 0 });
            bad += !NEAR3(f, dd.x, dd.y, dd.z, 1e-5) || !(u.y > 0) || !NEAR(r.y, 0, 1e-5) ||
                   !NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1, 1e-5);
        }
    CHECK(bad == 0, "look: %d of 117 directions wrong", bad);
    sv_quat q = somvr_bow_look((sv_vec3){ 0, 0, 7 }, 0);
    CHECK(NEAR(q.x, 0, 1e-6) && NEAR(q.y, 0, 1e-6) && NEAR(q.z, 0, 1e-6) && NEAR(q.w, 1, 1e-6), "look +Z = identity");
    q = somvr_bow_look((sv_vec3){ 0, 1, 0 }, 1.0f); /* straight up: the hint's heading */
    sv_vec3 f = sv_qrot(q, (sv_vec3){ 0, 0, 1 }), u = sv_qrot(q, (sv_vec3){ 0, 1, 0 });
    CHECK(NEAR3(f, 0, 1, 0, 1e-5) && NEAR(atan2f(-u.x, -u.z), 1.0, 1e-4), "look up: the hint (%g %g %g)", u.x, u.y, u.z);
    q = somvr_bow_look((sv_vec3){ NAN, 0, 0 }, 0);
    f = sv_qrot(q, (sv_vec3){ 0, 0, 1 });
    CHECK(NEAR3(f, 0, 0, 1, 1e-6), "look NaN = forward");
    /* the alignment: the shortest arc from the grip's forward onto dir, by a share; the roll about it kept */
    sv_quat g = sv_qmul(sv_qyaw(0.3f), qaxis(0, 0, 1, 70)); /* rolled 70 deg about its forward, then yawed */
    sv_vec3 target = unit((sv_vec3){ 0.2f, 0.3f, 1 });
    sv_quat a = somvr_bow_align(g, target, 1);
    f = sv_qrot(a, (sv_vec3){ 0, 0, 1 });
    CHECK(NEAR3(f, target.x, target.y, target.z, 1e-5), "align: forward on the target %g %g %g", f.x, f.y, f.z);
    sv_vec3 g0 = sv_qrot(g, (sv_vec3){ 0, 0, 1 }), ax = unit((sv_vec3){ g0.y * target.z - g0.z * target.y,
                                                                         g0.z * target.x - g0.x * target.z,
                                                                         g0.x * target.y - g0.y * target.x });
    sv_vec3 gu = sv_qrot(g, (sv_vec3){ 0, 1, 0 }), au = sv_qrot(a, (sv_vec3){ 0, 1, 0 });
    CHECK(NEAR(dot3(gu, ax), dot3(au, ax), 1e-5), "align: the shortest arc (the up's part along the axis kept)");
    a = somvr_bow_align(g, target, 0);
    CHECK(NEAR(a.x, g.x, 1e-6) && NEAR(a.y, g.y, 1e-6) && NEAR(a.z, g.z, 1e-6) && NEAR(a.w, g.w, 1e-6), "align w 0");
    a = somvr_bow_align(g, target, 0.5f);
    f = sv_qrot(a, (sv_vec3){ 0, 0, 1 });
    float ang_all = acosf(dot3(g0, target)), ang_half = acosf(dot3(f, target));
    CHECK(NEAR(ang_half, 0.5f * ang_all, 1e-4) && NEAR(acosf(dot3(g0, f)), 0.5f * ang_all, 1e-4), "align half: %g of %g",
          ang_half, ang_all);
    a = somvr_bow_align((sv_quat){ 0, 0, 0, 1 }, (sv_vec3){ 0, 0, -1 }, 1); /* half a turn */
    f = sv_qrot(a, (sv_vec3){ 0, 0, 1 });
    CHECK(NEAR3(f, 0, 0, -1, 1e-5), "align opposite %g %g %g", f.x, f.y, f.z);
    a = somvr_bow_align((sv_quat){ 0, 0, 0, 1 }, (sv_vec3){ 0, 0, 3 }, 1);
    CHECK(NEAR(a.w, 1, 1e-6), "align already there");
    a = somvr_bow_align((sv_quat){ NAN, 0, 0, 1 }, target, 1);
    CHECK(a.w == 1 && a.x == 0, "align NaN quaternion");
    a = somvr_bow_align(g, (sv_vec3){ NAN, 0, 0 }, 1);
    CHECK(NEAR(a.w, sv_qnormalize(g).w, 1e-6), "align NaN direction");
}

/* ---- build 16: the bow's frame for a real bow hold (re-xr-pipeline.md 51) ------------------------------------------ */
/* An OpenXR grip from its axes (XR space: x right, y up, -z ahead): the spec's grip has -Z through the fist's tube from
 * the little finger to the thumb, +X the left palm's normal / the right hand's back (to the right for either hand held
 * out), +Y = Z x X. A bow held out with the fist upright: -Z up, +X right, +Y back toward the player. */
static sv_quat quat_of_cols(sv_vec3 X, sv_vec3 Y, sv_vec3 Z) {
    float m00 = X.x, m01 = Y.x, m02 = Z.x, m10 = X.y, m11 = Y.y, m12 = Z.y, m20 = X.z, m21 = Y.z, m22 = Z.z, tr = m00 + m11 + m22;
    sv_quat q;
    if (tr > 0) {
        float k = 0.5f / sqrtf(tr + 1);
        q = (sv_quat){ (m21 - m12) * k, (m02 - m20) * k, (m10 - m01) * k, 0.25f / k };
    } else if (m00 > m11 && m00 > m22) {
        float k = 2 * sqrtf(1 + m00 - m11 - m22);
        q = (sv_quat){ 0.25f * k, (m01 + m10) / k, (m02 + m20) / k, (m21 - m12) / k };
    } else if (m11 > m22) {
        float k = 2 * sqrtf(1 + m11 - m00 - m22);
        q = (sv_quat){ (m01 + m10) / k, 0.25f * k, (m12 + m21) / k, (m02 - m20) / k };
    } else {
        float k = 2 * sqrtf(1 + m22 - m00 - m11);
        q = (sv_quat){ (m02 + m20) / k, (m12 + m21) / k, 0.25f * k, (m10 - m01) / k };
    }
    return sv_qnormalize(q);
}
/* the bow's limbs (+Y) and shot (+Z) in tracking space (game convention: x right, y up, z ahead) for an OpenXR grip
 * q_xr and a hold (the bow's rotation in the grip, as SOMVR_BOW_HOLD converges to) */
static void bow_axes(sv_quat q_xr, sv_quat hold, sv_vec3 *limb, sv_vec3 *shot) {
    float a[4] = { q_xr.x, q_xr.y, q_xr.z, q_xr.w };
    sv_quat b = sv_qmul(sv_xr_to_game_quat(a), hold);
    *limb = sv_qrot(b, (sv_vec3){ 0, 1, 0 }), *shot = sv_qrot(b, (sv_vec3){ 0, 0, 1 });
}
/* the layer's test hands (somvr_xt_head_pose at yaw 0): R = X(rx) Y(ry) Z(rz) in XR space */
static sv_quat xt_hand(float rx, float ry, float rz) {
    return sv_qnormalize(sv_qmul(sv_qmul(qaxis(1, 0, 0, rx), qaxis(0, 1, 0, ry)), qaxis(0, 0, 1, rz)));
}
static void test_hold_anatomy(void) {
    /* the fist upright, the arm straight out ahead: from the spec's axes, and as the drivers' `hand L head ... 90 0 0` */
    sv_quat up = quat_of_cols((sv_vec3){ 1, 0, 0 }, (sv_vec3){ 0, 0, 1 }, (sv_vec3){ 0, -1, 0 }), xt = xt_hand(90, 0, 0);
    CHECK(fabsf(up.x * xt.x + up.y * xt.y + up.z * xt.z + up.w * xt.w) > 0.99999f,
          "the test hand RX 90 is the upright fist: %g %g %g %g vs %g %g %g %g", up.x, up.y, up.z, up.w, xt.x, xt.y, xt.z, xt.w);
    sv_vec3 limb, shot;
    for (int h = 0; h < 2; h++) {
        bow_axes(up, somvr_bow_hold_q(SOMVR_BOW_HOLD_FIST, h, 0, 0), &limb, &shot);
        CHECK(NEAR3(limb, 0, 1, 0, 1e-5) && NEAR3(shot, 0, 0, 1, 1e-5),
              "fist, %s hand upright: the limbs up, the shot ahead, the string toward the face: limb %g %g %g shot %g %g %g",
              h ? "right" : "left", limb.x, limb.y, limb.z, shot.x, shot.y, shot.z);
    }
    /* builds 11-15b (HOLD_AUTO, the identity): the same real hold lays the bow flat along the arm and shoots it at the
     * sky (the user's 10-26 mark 1: "holding my hand straight out, and yet the bow is horizontal"; that session's idle
     * ray (-0.087 0.981 0.176)) */
    bow_axes(up, somvr_bow_hold_q(SOMVR_BOW_HOLD_AUTO, 0, 0, 0), &limb, &shot);
    CHECK(fabsf(limb.y) < 1e-5f && fabsf(fabsf(limb.z) - 1) < 1e-5f && shot.y > 0.9999f,
          "auto on a real hold: the limbs flat along the arm (limb %g %g %g), the shot up (%g %g %g)", limb.x, limb.y, limb.z,
          shot.x, shot.y, shot.z);
    /* ... and right only for the headless sword pose (the tube level and ahead): what builds 11-15b were tested with */
    bow_axes(xt_hand(0, 0, 0), somvr_bow_hold_q(SOMVR_BOW_HOLD_AUTO, 0, 0, 0), &limb, &shot);
    CHECK(NEAR3(limb, 0, 1, 0, 1e-5) && NEAR3(shot, 0, 0, 1, 1e-5), "auto in the sword pose: upright");
    /* canted: the fist rolled 20 deg outward about the arm (ry: the grip's +Y is the arm): the limbs lean, the shot stays
     * ahead, the limbs stay square to it */
    for (int k = 0; k < 6; k++) {
        static const float pose[6][3] = { { 90, 20, 0 }, { 90, -20, 0 }, { 60, 0, 0 }, { 120, 0, 0 }, { 90, 0, 25 }, { 70, 15, -10 } };
        sv_quat g = xt_hand(pose[k][0], pose[k][1], pose[k][2]);
        bow_axes(g, somvr_bow_hold_q(SOMVR_BOW_HOLD_FIST, 0, 0, 0), &limb, &shot);
        /* the grip's own axes in tracking space: the tube (OpenXR -Z) and the arm (OpenXR -Y) */
        float a[4] = { g.x, g.y, g.z, g.w };
        sv_quat gg = sv_xr_to_game_quat(a);
        sv_vec3 tube = sv_qrot(gg, (sv_vec3){ 0, 0, 1 }), arm = sv_qrot(gg, (sv_vec3){ 0, -1, 0 });
        CHECK(NEAR(dot3(limb, tube), 1, 1e-5) && NEAR(dot3(shot, arm), 1, 1e-5) && NEAR(dot3(limb, shot), 0, 1e-5),
              "pose %d (rx %g ry %g rz %g): the limbs along the tube, the shot along the arm: limb.tube %g shot.arm %g", k,
              pose[k][0], pose[k][1], pose[k][2], dot3(limb, tube), dot3(shot, arm));
        if (k == 2 || k == 3) /* low (60: the arm 30 deg down), high (120: 30 deg up): the shot follows, limbs upright-ish */
            CHECK(NEAR(shot.y, k == 2 ? -0.5 : 0.5, 1e-4) && shot.z > 0.86f && limb.y > 0.86f, "pose %d: shot %g %g %g limb "
                  "%g %g %g", k, shot.x, shot.y, shot.z, limb.x, limb.y, limb.z);
    }
    /* the cant and the tilt switches: + cant leans the upper limb to the string hand's side, + tilt toward the target */
    bow_axes(up, somvr_bow_hold_q(SOMVR_BOW_HOLD_FIST, 0, 0, 15), &limb, &shot);
    CHECK(limb.x > 0.25f && NEAR3(shot, 0, 0, 1, 1e-5), "cant 15, left hand: the upper limb to the right %g %g %g", limb.x,
          limb.y, limb.z);
    bow_axes(up, somvr_bow_hold_q(SOMVR_BOW_HOLD_FIST, 1, 0, 15), &limb, &shot);
    CHECK(limb.x < -0.25f && NEAR3(shot, 0, 0, 1, 1e-5), "cant 15, right hand: to the left %g", limb.x);
    bow_axes(up, somvr_bow_hold_q(SOMVR_BOW_HOLD_FIST, 0, 10, 0), &limb, &shot);
    CHECK(limb.z > 0.17f && shot.y < -0.17f, "tilt 10: the upper limb toward the target %g, the shot dips %g", limb.z, shot.y);
    sv_quat i = somvr_bow_hold_q(SOMVR_BOW_HOLD_CAL, 0, 30, 30);
    CHECK(i.w == 1 && i.x == 0, "cal: identity");
    /* the shot axis drives the ray: with HOLD_FIST's (0, -1, 0) an upright fist aims ahead */
    somvr_bow_cfg c;
    somvr_bow_cfg_default(&c);
    CHECK(NEAR(c.shot_ax[2], 1, 0) && c.quiver == 0, "defaults: the shot on +Z, no quiver");
    sv_quat hq = somvr_bow_hold_q(SOMVR_BOW_HOLD_FIST, 0, 0, 0);
    sv_vec3 ax = sv_qrot(hq, (sv_vec3){ 0, 0, 1 });
    c.shot_ax[0] = ax.x, c.shot_ax[1] = ax.y, c.shot_ax[2] = ax.z;
    CHECK(NEAR3(ax, 0, -1, 0, 1e-6), "fist's shot axis %g %g %g", ax.x, ax.y, ax.z);
    float a[4] = { up.x, up.y, up.z, up.w };
    sv_vec3 f = somvr_bow_fwd(&c, sv_xr_to_game_quat(a));
    CHECK(NEAR3(f, 0, 0, 1, 1e-5), "fwd of the upright fist %g %g %g", f.x, f.y, f.z);
    c.shot_ax[0] = c.shot_ax[1] = c.shot_ax[2] = 0;
    f = somvr_bow_fwd(&c, (sv_quat){ 0, 0, 0, 1 });
    CHECK(NEAR3(f, 0, 0, 1, 1e-6), "a zero shot axis reads +Z");
    /* the align about the shot axis: the bow hand turned so its shot axis meets a draw line 30 deg up */
    sv_quat g = sv_xr_to_game_quat(a), al = somvr_bow_align_ax(g, ax, (sv_vec3){ 0, 0.5f, 0.8660254f }, 1);
    sv_vec3 sa = sv_qrot(al, ax);
    CHECK(NEAR3(sa, 0, 0.5, 0.8660254, 1e-5), "align_ax: %g %g %g", sa.x, sa.y, sa.z);
}

/* ---- the state machine at 90 Hz ------------------------------------------------------------------------------------ */
/* the player faces +Z; the bow hand (left) at (0, 140, 40) pointing ahead, the string hand somewhere */
static somvr_bow g_s;
static somvr_bow_cfg g_c;
static somvr_bow_in g_in;
static somvr_bow_out g_o;
static uint32_t g_ev, g_seen;
static int g_aim_ticks, g_fire_ticks, g_fire_edges, g_fire_was;

static void reset(void) {
    somvr_bow_cfg_default(&g_c);
    somvr_bow_init(&g_s);
    memset(&g_in, 0, sizeof g_in);
    g_in.t = 100, g_in.live = 1, g_in.allow = 1;
    g_in.valid[0] = g_in.valid[1] = 1;
    g_in.p[0] = (sv_vec3){ 0, 140, 40 }, g_in.q[0] = (sv_quat){ 0, 0, 0, 1 };
    g_in.p[1] = (sv_vec3){ 30, 120, 0 }, g_in.q[1] = (sv_quat){ 0, 0, 0, 1 };
    g_seen = 0, g_aim_ticks = g_fire_ticks = g_fire_edges = g_fire_was = 0;
}
/* one frame: the step, then the input tick's commands at the same time */
static void step(void) {
    g_in.t += 1.0 / 90;
    somvr_bow_step(&g_s, &g_c, &g_in, &g_o);
    g_ev |= g_o.events;
    int aim, fire;
    somvr_bow_cmds(&g_s, g_in.t, &g_seen, &aim, &fire);
    g_aim_ticks += aim, g_fire_ticks += fire;
    g_fire_edges += fire && !g_fire_was;
    g_fire_was = fire;
}
static void steps(int n) {
    for (int i = 0; i < n; i++) step();
}
static void string_at(float x, float y, float z) { g_in.p[1] = (sv_vec3){ x, y, z }; }
static void nock_and_grip(void) { /* the string hand to the nock, then the grip */
    string_at(0, 140, 35);
    step();
    g_in.string = 1;
    step();
}
static void draw_to(float cm, int frames) { /* straight back from the nock (z 40 - cm) */
    float z0 = g_in.p[1].z, z1 = 40 - cm;
    for (int i = 1; i <= frames; i++) {
        g_in.p[1].z = z0 + (z1 - z0) * (float)i / (float)frames;
        step();
    }
}

static void test_enter(void) {
    reset();
    /* a press away from the nock: nothing (and no MISS while idle) */
    g_ev = 0;
    steps(3);
    g_in.string = 1;
    steps(40);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_ev == 0 && g_aim_ticks == 0, "a far press: phase %d events %#x", g_s.phase, g_ev);
    /* the hand moved to the nock with the grip held (no new press): still nothing */
    string_at(0, 140, 35);
    steps(5);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE, "a held grip at the nock entered");
    /* open, press at the nock: aim and draw at once */
    g_in.string = 0;
    step();
    g_ev = 0;
    g_in.string = 0.7f;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && (g_o.events & SOMVR_BOW_E_ENTER) && (g_o.events & SOMVR_BOW_E_NOCK) && g_o.aim &&
              !g_s.by_button && g_s.aims == 1 && g_s.draws == 1 && NEAR(g_o.nock_dist, 5, 1e-4),
          "the nock: phase %d events %#x dist %g", g_s.phase, g_o.events, g_o.nock_dist);
    /* the hysteresis: 0.5 keeps it closed, 0.3 opens (a let-down at 5 cm) */
    g_in.string = 0.5f;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "0.5 opened the grip");
    g_ev = 0;
    g_in.string = 0.3f;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && (g_ev & SOMVR_BOW_E_LETDOWN) && !(g_ev & SOMVR_BOW_E_RELEASE) && g_s.letdowns == 1,
          "a let-down at the nock: phase %d events %#x", g_s.phase, g_ev);
    /* a press 30 cm out, moved into the nock inside the window (0.2 s): a draw */
    string_at(0, 140, 10);
    step();
    g_in.string = 1;
    steps(9); /* 0.1 s */
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.looking, "looking");
    string_at(0, 140, 30);
    steps(2);
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && g_s.draws == 2, "moved into the nock inside the window: phase %d", g_s.phase);
    g_in.string = 0;
    steps(2);
    /* the window runs out away from the nock: a MISS while aiming, then reaching the nock does nothing */
    string_at(0, 140, 10);
    step();
    g_ev = 0;
    g_in.string = 1;
    steps(30);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && (g_ev & SOMVR_BOW_E_MISS) && g_s.misses == 1 && !g_s.looking, "miss: %#x", g_ev);
    string_at(0, 140, 35);
    steps(3);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "the nock after the window");
    g_in.string = 0;
    steps(2);
    /* not allowed / not live: no entry from idle */
    reset();
    g_in.allow = 0;
    nock_and_grip();
    steps(3);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE, "entered while not allowed");
    g_in.allow = 1; /* allowed again while the press is still inside its window: the draw */
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "allowed inside the window: phase %d", g_s.phase);
    reset();
    g_in.live = 0;
    nock_and_grip();
    steps(30);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE, "entered without live input");
    /* an untracked hand: no entry */
    reset();
    g_in.valid[0] = 0;
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE, "entered with the bow hand untracked");
    /* NaN: the string reads open, a NaN position is no position */
    reset();
    string_at(0, 140, 35);
    g_in.string = NAN;
    steps(3);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && !g_s.down, "NaN string");
    g_in.p[0].x = NAN;
    g_in.string = 1;
    steps(2);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE, "NaN bow hand entered");
}

static void test_draw_and_shoot(void) {
    reset();
    g_in.game_aim = 1;
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && g_aim_ticks == 1 && g_fire_ticks == 0, "draw: phase %d", g_s.phase);
    /* the ray at the nock: from the nock along the bow's forward */
    CHECK(g_o.ray && NEAR3(g_o.origin, 0, 140, 40, 1e-4) && NEAR3(g_o.dir, 0, 0, 1, 1e-5), "the ray at the nock");
    /* draw back to 35 cm over 30 frames: armed at 20, the ticks every 4 cm, tension */
    g_ev = 0;
    int ticks = 0, armed_at = -1;
    float z0 = g_in.p[1].z;
    for (int i = 1; i <= 30; i++) {
        g_in.p[1].z = z0 + (5 - 35 - 0) * (float)i / 30.0f; /* 35 -> 5 */
        step();
        ticks += !!(g_o.events & SOMVR_BOW_E_TICK);
        if ((g_o.events & SOMVR_BOW_E_ARMED) && armed_at < 0) armed_at = i;
    }
    CHECK(NEAR(g_o.draw_cm, 35, 1e-3) && NEAR(g_o.tension, 35.0 / 55, 1e-5) && g_s.armed && !g_s.full && ticks == 7 &&
              g_s.ticks == 8 && armed_at == 15,
          "drawn: draw %g tension %g ticks %d (%d) armed at %d", g_o.draw_cm, g_o.tension, ticks, g_s.ticks, armed_at);
    /* the ray: from the nock, from the string hand to the bow hand */
    CHECK(g_o.ray && NEAR3(g_o.origin, 0, 140, 40, 1e-4) && NEAR3(g_o.dir, 0, 0, 1, 1e-5), "drawn ray");
    /* aim to the left and up: the string hand right and low */
    string_at(10, 130, 5);
    step();
    sv_vec3 want = unit((sv_vec3){ -10, 10, 35 });
    CHECK(NEAR3(g_o.dir, want.x, want.y, want.z, 1e-5) && NEAR(g_o.draw_cm, len3((sv_vec3){ 10, 10, 35 }), 1e-3),
          "aimed ray %g %g %g", g_o.dir.x, g_o.dir.y, g_o.dir.z);
    sv_vec3 aimed = g_o.dir;
    CHECK(!g_o.fire && g_fire_ticks == 0, "fire before the release");
    /* release: a shot (the aim held 0.5 s and the game's aim on: fire at once), the ray latched */
    g_ev = 0;
    g_in.string = 0;
    string_at(30, 160, 60); /* the hand flies forward */
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_FOLLOW && (g_o.events & SOMVR_BOW_E_RELEASE) && (g_o.events & SOMVR_BOW_E_FIRE) &&
              g_o.fire && g_s.shots == 1 && g_s.fired == 1 && NEAR(g_s.last_draw, len3((sv_vec3){ 10, 10, 35 }), 1e-3) &&
              NEAR(g_o.wait_s, 0, 1e-9),
          "release: phase %d events %#x fire %d", g_s.phase, g_o.events, g_o.fire);
    CHECK(g_o.ray && NEAR3(g_o.dir, aimed.x, aimed.y, aimed.z, 1e-6) && NEAR3(g_o.origin, 0, 140, 40, 1e-4),
          "the latched ray %g %g %g", g_o.dir.x, g_o.dir.y, g_o.dir.z);
    /* the tap: 0.1 s = 9 frames of fire, one edge; the latched ray all through the follow-through */
    int fire0 = g_fire_ticks;
    steps(7);
    CHECK(g_fire_ticks == 8 && g_fire_edges == 1 && fire0 == 1, "tap: fire ticks %d edges %d", g_fire_ticks, g_fire_edges);
    steps(4); /* 0.1 s = 9 or 10 frames (the float's rounding) */
    CHECK(!g_o.fire && (g_fire_ticks == 9 || g_fire_ticks == 10) && g_s.phase == SOMVR_BOW_PH_FOLLOW &&
              NEAR3(g_o.dir, aimed.x, aimed.y, aimed.z, 1e-6),
          "after the tap: fire %d ticks %d phase %d", g_o.fire, g_fire_ticks, g_s.phase);
    steps(26); /* 0.41 s after the fire: back to aim, the bow's ray */
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_o.aim && NEAR3(g_o.dir, 0, 0, 1, 1e-5), "follow-through over: phase %d",
          g_s.phase);
    /* a second shot: nock again, draw 50 cm, release */
    g_in.string = 0;
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && g_s.draws == 2 && g_s.aims == 1, "second nock");
    draw_to(56, 20);
    CHECK(g_s.full && (g_ev & SOMVR_BOW_E_FULL) && g_s.ticks == 13 && g_o.tension == 1, "full draw: ticks %d", g_s.ticks);
    g_in.string = 0;
    step();
    CHECK(g_s.fired == 2 && g_fire_edges == 2, "second shot: fired %llu edges %d", (unsigned long long)g_s.fired, g_fire_edges);
    string_at(0, 140, 30); /* the string hand back at the bow */
    steps(60);
    /* the hands separate: the aim ends after 0.8 s */
    g_ev = 0;
    string_at(60, 100, 0);
    steps(70);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "exit too early");
    steps(5);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && (g_ev & SOMVR_BOW_E_EXIT) && g_s.last_end == SOMVR_BOW_END_APART &&
              g_s.exits[SOMVR_BOW_END_APART] == 1 && !g_o.aim && !g_o.ray,
          "apart: phase %d end %d", g_s.phase, g_s.last_end);
    int aim0 = g_aim_ticks;
    steps(5);
    CHECK(g_aim_ticks == aim0, "the aim command after the exit");
    /* hands apart then back together inside 0.3 s: no exit */
    reset();
    g_in.game_aim = 1;
    nock_and_grip();
    g_in.string = 0;
    step(); /* let-down at 5 cm */
    string_at(60, 100, 0);
    steps(20);
    string_at(0, 140, 20);
    steps(40);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "back together: phase %d", g_s.phase);
}

static void test_timing(void) {
    /* a snap shot: released before the aim was held 0.25 s: the fire waits for it */
    reset();
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(30, 10); /* 0.11 s */
    g_ev = 0;
    g_in.string = 0;
    step();
    CHECK((g_ev & SOMVR_BOW_E_RELEASE) && !(g_ev & SOMVR_BOW_E_FIRE) && g_s.shot && !g_o.fire, "snap: fired at once");
    int n = 0;
    while (!(g_o.events & SOMVR_BOW_E_FIRE) && n < 60) step(), n++;
    CHECK(n > 0 && NEAR(g_in.t - g_s.aim_t0, 0.25, 0.012) && g_o.fire && NEAR(g_o.wait_s, n / 90.0, 1e-6),
          "snap: fired after %d frames (aim held %g, waited %g)", n, g_in.t - g_s.aim_t0, g_o.wait_s);
    /* the game's aim late: the fire waits for it */
    reset();
    nock_and_grip();
    draw_to(30, 40);
    g_in.string = 0;
    steps(20);
    CHECK(g_s.shot && g_s.fired == 0 && !g_o.fire, "waiting for the game's aim");
    g_in.game_aim = 1;
    step();
    CHECK(g_s.fired == 1 && (g_o.events & SOMVR_BOW_E_FIRE) && NEAR(g_o.wait_s, 20 / 90.0, 1e-6), "late aim: waited %g",
          g_o.wait_s);
    /* never: dropped after 0.6 s, no fire; follow 0.4 s; then aim */
    reset();
    nock_and_grip();
    draw_to(30, 40);
    g_ev = 0;
    g_in.string = 0;
    steps(52);
    CHECK(g_s.shot && !(g_ev & SOMVR_BOW_E_DROPPED), "dropped too early");
    n = 0;
    while (!(g_ev & SOMVR_BOW_E_DROPPED) && n < 10) step(), n++;
    CHECK(!g_s.shot && (g_ev & SOMVR_BOW_E_DROPPED) && g_s.dropped == 1 && g_s.fired == 0 && g_fire_ticks == 0 &&
              g_s.phase == SOMVR_BOW_PH_FOLLOW && n >= 2 && n <= 4,
          "dropped: events %#x after %d more", g_ev, n);
    steps(35);
    CHECK(g_s.phase == SOMVR_BOW_PH_FOLLOW, "after the drop, early: phase %d", g_s.phase);
    steps(3);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "after the drop: phase %d", g_s.phase);
    /* an exit while a shot waits: dropped */
    reset();
    nock_and_grip();
    draw_to(30, 40);
    g_in.string = 0;
    step();
    g_ev = 0;
    g_in.allow = 0;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && (g_ev & SOMVR_BOW_E_DROPPED) && (g_ev & SOMVR_BOW_E_EXIT) &&
              g_s.last_end == SOMVR_BOW_END_GAME && g_s.dropped == 1,
          "an exit with a pending shot: events %#x", g_ev);
    /* the cmds: a tap shorter than a tick still goes out once */
    reset();
    g_in.game_aim = 1;
    g_c.fire_s = 0.001f;
    nock_and_grip();
    draw_to(30, 40);
    g_in.string = 0;
    step();
    steps(3);
    CHECK(g_fire_ticks == 1 && g_fire_edges == 1, "a short tap: %d ticks", g_fire_ticks);
    uint32_t seen = g_s.fire_seq - 1;
    int aim, fire;
    somvr_bow_cmds(&g_s, g_in.t, &seen, &aim, &fire);
    CHECK(aim && fire && seen == g_s.fire_seq, "cmds: an unseen fire");
    somvr_bow_cmds(&g_s, g_in.t, &seen, &aim, &fire);
    CHECK(aim && !fire, "cmds: seen");
}

/* build 11, release_now: the release shoots at once whenever the bow is ready (the game's aim knocked out by a hit, or
 * not on yet); not ready: it waits for the game's aim or the bow as before, and the gate takes any_aim */
static void test_release_now(void) {
    reset();
    g_c.release_now = 1;
    g_in.bow_ready = 1; /* the bow is the active item, the game's aim off */
    nock_and_grip();
    draw_to(30, 40);
    g_ev = 0;
    g_in.string = 0;
    step();
    CHECK((g_ev & SOMVR_BOW_E_RELEASE) && (g_ev & SOMVR_BOW_E_FIRE) && g_s.fired == 1 && NEAR(g_o.wait_s, 0, 1e-9),
          "release_now, ready: fired at the release (events %#x, fired %llu)", g_ev, (unsigned long long)g_s.fired);
    /* a snap shot (0.1 s of aim) with the bow ready: no wait for the aim's hold either */
    reset();
    g_c.release_now = 1;
    g_in.bow_ready = 1;
    nock_and_grip();
    draw_to(30, 10);
    g_ev = 0;
    g_in.string = 0;
    step();
    CHECK((g_ev & SOMVR_BOW_E_FIRE) && g_s.fired == 1, "release_now: a snap shot fired at once (events %#x)", g_ev);
    /* not ready: waits; the bow comes (the mode brought it out) = the shot then */
    reset();
    g_c.release_now = 1;
    nock_and_grip();
    draw_to(30, 40);
    g_in.string = 0;
    steps(10);
    CHECK(g_s.shot && g_s.fired == 0, "release_now, not ready: waiting");
    g_in.bow_ready = 1;
    step();
    CHECK(g_s.fired == 1 && (g_o.events & SOMVR_BOW_E_FIRE) && NEAR(g_o.wait_s, 10 / 90.0, 1e-6),
          "release_now: fired once ready, waited %g", g_o.wait_s);
    /* never ready, no aim: dropped after wait_s as before */
    reset();
    g_c.release_now = 1;
    nock_and_grip();
    draw_to(30, 40);
    g_ev = 0;
    g_in.string = 0;
    steps(60);
    CHECK((g_ev & SOMVR_BOW_E_DROPPED) && g_s.fired == 0, "release_now, never ready: dropped (events %#x)", g_ev);
    /* release_now off: bow_ready alone does not shoot (the old rule) */
    reset();
    g_in.bow_ready = 1;
    nock_and_grip();
    draw_to(30, 40);
    g_in.string = 0;
    steps(10);
    CHECK(g_s.shot && g_s.fired == 0, "wait mode ignores bow_ready");
    /* the gate: no game aim passes with any_aim, fails without */
    somvr_bow_shot_in in = { 1, 0, 1, 1, 1, 5, 0, { 0, 0, 0 }, { 0, 0, 1 }, 0 };
    CHECK(somvr_bow_shot_check(&in) == SOMVR_BOW_SHOT_NO_AIM, "gate: no aim, no any_aim");
    in.any_aim = 1;
    CHECK(somvr_bow_shot_check(&in) == SOMVR_BOW_SHOT_OK, "gate: any_aim passes without the game's aim");
    in.ranged = 0;
    CHECK(somvr_bow_shot_check(&in) == SOMVR_BOW_SHOT_NOT_RANGED, "gate: any_aim still needs the bow active");
}

static void test_button(void) {
    reset();
    /* the button with the hands apart: aim by the button, the bow's ray; held: no exit */
    g_ev = 0;
    g_in.button = 1;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.by_button && (g_o.events & SOMVR_BOW_E_ENTER) && g_s.by_btn == 1 && g_o.ray &&
              NEAR3(g_o.dir, 0, 0, 1, 1e-6) && NEAR3(g_o.origin, 0, 140, 40, 1e-4),
          "button: phase %d", g_s.phase);
    g_in.q[0] = sv_qyaw(-0.4f); /* the bow turned left: the ray follows it */
    steps(60);
    sv_vec3 f = sv_qrot(sv_qyaw(-0.4f), (sv_vec3){ 0, 0, 1 });
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && NEAR3(g_o.dir, f.x, f.y, f.z, 1e-5), "button held: phase %d", g_s.phase);
    /* a draw inside the button's aim */
    g_in.q[0] = (sv_quat){ 0, 0, 0, 1 };
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && g_s.aims == 1, "a draw in the button's aim");
    g_in.string = 0;
    step();
    /* the button released with the hands together: stays; apart: exits after 0.3 s */
    g_in.button = 0;
    steps(40);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "released together: phase %d", g_s.phase);
    string_at(60, 100, 0);
    steps(70);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "released apart, early: phase %d", g_s.phase);
    steps(5);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.last_end == SOMVR_BOW_END_APART, "released apart: phase %d", g_s.phase);
    /* held through a stand-down: no new aim until it is pressed again */
    reset();
    g_in.button = 1;
    step();
    g_in.live = 0;
    step();
    g_in.live = 1;
    steps(5);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.last_end == SOMVR_BOW_END_INPUT, "held button re-entered");
    g_in.button = 0;
    step();
    g_in.button = 1;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.aims == 2, "pressed again");
    /* the idle ray HEAD: no ray while not drawn, the string's while drawn */
    g_c.idle_ray = SOMVR_BOW_IDLE_HEAD;
    step();
    CHECK(!g_o.ray && g_o.aim, "idle head: a ray");
    nock_and_grip();
    draw_to(30, 10);
    CHECK(g_o.ray && NEAR3(g_o.dir, 0, 0, 1, 1e-5), "idle head, drawn: no ray");
}

/* build 15b (SOMVR_BOW_DRAW=slot): idle_nock 0 - the string grip at the nock brings no bow, the button does; then the
 * nock draws as before; somvr_bow_stop(SLOT) ends it */
static void test_slot(void) {
    reset();
    g_c.idle_nock = 0;
    nock_and_grip();
    steps(10);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.aims == 0, "slot: the nock from idle entered (phase %d)", g_s.phase);
    g_in.string = 0;
    step();
    g_in.button = 1;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.by_button, "slot: the button (the slot's grip): phase %d", g_s.phase);
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "slot: a draw in the slot's aim: phase %d", g_s.phase);
    g_in.string = 0;
    step();
    somvr_bow_out o;
    somvr_bow_stop(&g_s, SOMVR_BOW_END_SLOT, g_in.t, &o);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.last_end == SOMVR_BOW_END_SLOT && (o.events & SOMVR_BOW_E_EXIT) &&
              !strcmp(somvr_bow_end_name(SOMVR_BOW_END_SLOT), "slot"),
          "slot: put back: phase %d end %d", g_s.phase, g_s.last_end);
    g_c.idle_nock = 1;
}

/* build 16: the quiver (the string = the right trigger; the bow from its slot = the button) */
static void quiver_at(int in) { g_in.quiver = in; }
static void test_quiver(void) {
    reset();
    g_c.quiver = 1, g_c.idle_nock = 0;
    g_in.arrows = 5, g_in.game_aim = 1;
    g_in.button = 1;
    steps(2);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.arrow == SOMVR_BOW_ARROW_NONE, "quiver: the bow out, no arrow");
    /* a press at the nock with no arrow: NOARROW, no draw */
    g_ev = 0;
    nock_and_grip();
    steps(5);
    CHECK((g_ev & SOMVR_BOW_E_NOARROW) && g_s.phase == SOMVR_BOW_PH_AIM && g_s.noarrows == 1,
          "no arrow: no nock (events %#x phase %d)", g_ev, g_s.phase);
    g_in.string = 0;
    step();
    /* the trigger in the quiver: an arrow in the hand; held to the nock: nocked without a new press */
    string_at(25, 175, 20);
    quiver_at(1);
    step();
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_TAKE) && g_s.arrow == SOMVR_BOW_ARROW_HAND && g_o.arrow == SOMVR_BOW_ARROW_HAND && g_s.takes == 1,
          "take: events %#x arrow %d", g_ev, g_s.arrow);
    quiver_at(0);
    string_at(20, 150, 20); /* on the way: far from the nock */
    steps(30);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.arrow == SOMVR_BOW_ARROW_HAND, "carried: still in the hand");
    g_in.button = 0; /* the hands apart while an arrow is held end nothing */
    string_at(40, 160, -10);
    steps(120);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "apart with an arrow: no exit (phase %d)", g_s.phase);
    g_in.button = 1;
    g_ev = 0;
    string_at(0, 140, 30);
    step();
    CHECK((g_ev & SOMVR_BOW_E_NOCK) && g_s.phase == SOMVR_BOW_PH_DRAW && g_s.arrow == SOMVR_BOW_ARROW_STRING,
          "touched to the nock: nocked (events %#x phase %d arrow %d)", g_ev, g_s.phase, g_s.arrow);
    draw_to(45, 40);
    g_ev = 0;
    g_in.string = 0; /* let go of the trigger: the shot */
    steps(30);
    CHECK((g_ev & SOMVR_BOW_E_RELEASE) && (g_ev & SOMVR_BOW_E_FIRE) && g_s.fired == 1 && g_s.arrow == SOMVR_BOW_ARROW_NONE,
          "released: shot (events %#x fired %llu arrow %d)", g_ev, (unsigned long long)g_s.fired, g_s.arrow);
    steps(40);
    /* an arrow taken and let go before the nock: gone, no shot */
    string_at(25, 175, 20), quiver_at(1);
    step();
    g_in.string = 1;
    step();
    quiver_at(0), string_at(15, 150, 15);
    steps(10);
    g_ev = 0;
    g_in.string = 0;
    steps(10);
    CHECK((g_ev & SOMVR_BOW_E_DROP) && !(g_ev & (SOMVR_BOW_E_RELEASE | SOMVR_BOW_E_FIRE)) && g_s.arrow == SOMVR_BOW_ARROW_NONE &&
              g_s.drops == 1 && g_s.fired == 1, "let go before the nock: dropped (events %#x)", g_ev);
    /* nocked, drawn 10 cm and let go: no shot, the arrow rests on the string; a press there draws it again */
    string_at(25, 175, 20), quiver_at(1);
    step();
    g_in.string = 1;
    step();
    quiver_at(0), string_at(0, 140, 32);
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "nocked again");
    draw_to(12, 10);
    g_ev = 0;
    g_in.string = 0;
    step();
    CHECK((g_ev & SOMVR_BOW_E_LETDOWN) && g_s.arrow == SOMVR_BOW_ARROW_REST && g_s.phase == SOMVR_BOW_PH_AIM && g_s.fired == 1,
          "let down: resting on the string (events %#x arrow %d)", g_ev, g_s.arrow);
    string_at(25, 175, 20), quiver_at(1);
    step();
    g_ev = 0;
    g_in.string = 1; /* the quiver gives nothing while one rests on the string */
    step();
    CHECK(!(g_ev & SOMVR_BOW_E_TAKE) && g_s.arrow == SOMVR_BOW_ARROW_REST, "rest: no second arrow (events %#x)", g_ev);
    g_in.string = 0, quiver_at(0);
    string_at(0, 140, 34);
    step();
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_NOCK) && g_s.arrow == SOMVR_BOW_ARROW_STRING, "the resting arrow drawn again (events %#x)", g_ev);
    draw_to(40, 30);
    g_in.string = 0;
    steps(30);
    CHECK(g_s.fired == 2 && g_s.arrow == SOMVR_BOW_ARROW_NONE, "the second shot (fired %llu)", (unsigned long long)g_s.fired);
    /* the quiver empty: nothing in the hand, EMPTY (the buzz) */
    g_in.arrows = 0;
    string_at(25, 175, 20), quiver_at(1);
    step();
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_EMPTY) && g_s.arrow == SOMVR_BOW_ARROW_NONE && g_s.empties == 1, "empty quiver (events %#x)", g_ev);
    g_in.string = 0;
    step();
    /* unknown count: an arrow (the shot's gate decides) */
    g_in.arrows = -1;
    g_in.string = 1;
    step();
    CHECK(g_s.arrow == SOMVR_BOW_ARROW_HAND, "unknown count: an arrow");
    /* the bow put back with the arrow in the hand: the arrow goes back too */
    somvr_bow_out o;
    somvr_bow_stop(&g_s, SOMVR_BOW_END_SLOT, g_in.t, &o);
    CHECK(g_s.arrow == SOMVR_BOW_ARROW_NONE && g_s.phase == SOMVR_BOW_PH_IDLE, "put back: no arrow");
    /* the bow away: the quiver gives nothing (the right shoulder is the sword's then) */
    g_in.button = 0, g_in.string = 0;
    step();
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK(!(g_ev & (SOMVR_BOW_E_TAKE | SOMVR_BOW_E_EMPTY | SOMVR_BOW_E_ENTER)) && g_s.arrow == SOMVR_BOW_ARROW_NONE,
          "the bow away: nothing from the quiver (events %#x)", g_ev);
    CHECK(!strcmp(somvr_bow_arrow_name(SOMVR_BOW_ARROW_REST), "rest") && !strcmp(somvr_bow_arrow_name(9), "?"), "arrow names");
    /* without the quiver: any press at the nock draws (an implicit arrow, STRING while drawn, none after) */
    reset();
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && g_s.arrow == SOMVR_BOW_ARROW_STRING, "no quiver: drawn");
    draw_to(10, 10);
    g_in.string = 0;
    step();
    CHECK(g_s.arrow == SOMVR_BOW_ARROW_NONE, "no quiver: a let-down keeps no arrow");
}

/* build 17: the nock's zone (SOMVR_BOW_NOCK_ZONE=string): anywhere near the string's middle and the riser, the draw only
 * the pull back behind the nock; and SOMVR_BOW_NOARROW_CUE=take. The bow hand at (0,140,40), identity: the shot +Z,
 * the upper limb +Y, the nock the grip */
static void carry_arrow(void) { /* an arrow from the quiver, the trigger held */
    g_in.string = 0, string_at(25, 175, 20), quiver_at(1);
    step();
    g_in.string = 1;
    step();
    quiver_at(0);
}
static void test_zone(void) {
    float zd;
    somvr_bow_cfg c;
    somvr_bow_cfg_default(&c);
    sv_vec3 bp = { 0, 140, 40 };
    sv_quat bq = { 0, 0, 0, 1 };
    /* the geometry: point (build 16) vs the zone */
    CHECK(!somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 18, 140, 40 }, &zd) && NEAR(zd, 18, 1e-3), "point: 18 cm beside is out (%.1f)", zd);
    c.nock_zone = 1;
    CHECK(somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 18, 140, 40 }, &zd) && NEAR(zd, 18, 1e-3), "zone: 18 cm beside is in (%.1f)", zd);
    CHECK(somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 0, 140, 16 }, &zd) && NEAR(zd, 24, 1e-3), "zone: 24 cm behind is in");
    CHECK(!somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 26, 140, 40 }, &zd), "zone: 26 cm beside is out (%.1f)", zd);
    CHECK(somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 15, 160, 40 }, &zd) && NEAR(zd, 17, 1e-3),
          "zone: 20 cm up the limb, 15 beside: 17 cm from the segment's end (%.1f)", zd);
    CHECK(somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 0, 175, 40 }, &zd) && NEAR(zd, 23, 1e-3), "zone: 35 cm up: 23 from its end");
    CHECK(!somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 0, 175 + 3, 40 }, &zd) && NEAR(zd, 26, 1e-3), "zone: 38 cm up: out");
    CHECK(somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 0, 140, 44 }, &zd), "zone: 4 cm in front: in");
    CHECK(!somvr_bow_at_nock(&c, bp, bq, (sv_vec3){ 0, 140, 50 }, &zd) && zd == 999, "zone: 10 cm in front: out (%.0f)", zd);
    CHECK(NEAR(somvr_bow_pull(&c, bq, bp, (sv_vec3){ 18, 140, 40 }), 0, 1e-4) &&
              NEAR(somvr_bow_pull(&c, bq, bp, (sv_vec3){ 18, 140, 10 }), 30, 1e-3) &&
              NEAR(somvr_bow_pull(&c, bq, bp, (sv_vec3){ 0, 140, 45 }), 0, 1e-4),
          "pull: beside 0, 30 back 30, in front 0");
    /* the fist hold (build 16): the limbs on the grip's +Z, the shot on -Y */
    somvr_bow_cfg f = c;
    f.shot_ax[0] = 0, f.shot_ax[1] = -1, f.shot_ax[2] = 0, f.limb_ax[0] = 0, f.limb_ax[1] = 0, f.limb_ax[2] = 1;
    CHECK(somvr_bow_at_nock(&f, bp, bq, (sv_vec3){ 0, 160, 50 }, &zd) && NEAR(zd, 20, 1e-3) &&
              !somvr_bow_at_nock(&f, bp, bq, (sv_vec3){ 0, 130, 40 }, &zd) &&
              NEAR(somvr_bow_pull(&f, bq, bp, (sv_vec3){ 0, 170, 40 }), 30, 1e-3),
          "fist: 20 behind along the arm in, 10 in front out, the pull along +Y");

    /* the user's near miss (session 090356: the hand 18-22 cm from the grip, no nock): build 16 ignores it */
    reset();
    g_c.quiver = 1, g_c.idle_nock = 0;
    g_in.arrows = 5, g_in.game_aim = 1, g_in.button = 1;
    steps(2);
    carry_arrow();
    g_ev = 0;
    string_at(14, 140, 28); /* 18.4 cm from the nock, 12 behind */
    steps(5);
    CHECK(!(g_ev & SOMVR_BOW_E_NOCK) && g_s.arrow == SOMVR_BOW_ARROW_HAND, "point: 18 cm, no nock (events %#x)", g_ev);
    /* the zone: nocked there, the draw starts at the pull (12), not the 18 cm */
    g_c.nock_zone = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_NOCK) && g_s.phase == SOMVR_BOW_PH_DRAW && NEAR(g_s.draw_cm, 12, 1e-3) && !g_s.armed,
          "zone: nocked at 18 cm, the draw 12 (%.1f), not armed", g_s.draw_cm);
    /* pulled straight back 30 cm more: drawn 42, armed; the ray along the draw */
    draw_to(42, 20);
    CHECK(NEAR(g_s.draw_cm, 42, 1e-3) && g_s.armed, "zone: drawn 42 (%.1f)", g_s.draw_cm);
    g_ev = 0;
    g_in.string = 0;
    steps(30);
    CHECK((g_ev & SOMVR_BOW_E_RELEASE) && g_s.fired == 1 && NEAR(g_s.last_draw, 42, 1e-3), "zone: the shot (events %#x)", g_ev);
    steps(40); /* (FOLLOW's 0.4 s) */
    /* nocked from the side and let go at once: no pull, no shot - the arrow rests on the string */
    carry_arrow();
    g_ev = 0;
    string_at(20, 140, 40);
    step();
    CHECK((g_ev & SOMVR_BOW_E_NOCK) && NEAR(g_s.draw_cm, 0, 1e-4), "zone: nocked 20 cm beside, no draw (%.1f)", g_s.draw_cm);
    g_ev = 0;
    g_in.string = 0;
    step();
    CHECK((g_ev & SOMVR_BOW_E_LETDOWN) && !(g_ev & SOMVR_BOW_E_RELEASE) && g_s.arrow == SOMVR_BOW_ARROW_REST && g_s.fired == 1,
          "zone: let go beside the bow: resting, no shot (events %#x)", g_ev);
    /* the resting arrow drawn again by a press anywhere in the zone */
    string_at(0, 150, 20);
    step();
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_NOCK) && g_s.arrow == SOMVR_BOW_ARROW_STRING && NEAR(g_s.draw_cm, 20, 1e-3),
          "zone: the resting arrow drawn again 22 cm off (draw %.1f)", g_s.draw_cm);
    g_in.string = 0;
    steps(40);
    CHECK(g_s.fired == 2, "zone: pulled 20 behind and let go: a shot (fired %llu)", (unsigned long long)g_s.fired);
    /* past the front: no nock with the arrow carried there */
    carry_arrow();
    g_ev = 0;
    string_at(0, 140, 52);
    steps(5);
    CHECK(!(g_ev & SOMVR_BOW_E_NOCK) && g_s.arrow == SOMVR_BOW_ARROW_HAND, "zone: 12 cm in front, no nock (events %#x)", g_ev);
    g_in.string = 0;
    steps(3);
    /* an empty hand pressed in the zone: NOARROW (the weak/buzz cue) */
    string_at(15, 140, 30);
    step();
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_NOARROW) && g_s.phase == SOMVR_BOW_PH_AIM, "zone: an empty press 18 cm off: NOARROW (events %#x)", g_ev);
    g_in.string = 0;
    step();
    /* SOMVR_BOW_NOARROW_CUE=take: the same press takes one and nocks it at once */
    g_c.noarrow_take = 1;
    uint64_t takes = g_s.takes, fired = g_s.fired;
    g_ev = 0;
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_TAKE) && (g_ev & SOMVR_BOW_E_NOCK) && !(g_ev & SOMVR_BOW_E_NOARROW) && g_s.takes == takes + 1 &&
              g_s.phase == SOMVR_BOW_PH_DRAW && g_s.arrow == SOMVR_BOW_ARROW_STRING,
          "take: taken and nocked at the string (events %#x phase %d)", g_ev, g_s.phase);
    draw_to(40, 20);
    g_in.string = 0;
    steps(30);
    CHECK(g_s.fired == fired + 1, "take: shot (fired %llu)", (unsigned long long)g_s.fired);
    steps(40);
    g_in.arrows = 0;
    g_ev = 0;
    string_at(15, 140, 30);
    step();
    g_in.string = 1;
    step();
    CHECK((g_ev & SOMVR_BOW_E_EMPTY) && !(g_ev & (SOMVR_BOW_E_TAKE | SOMVR_BOW_E_NOCK)) && g_s.arrow == SOMVR_BOW_ARROW_NONE,
          "take with none left: EMPTY (events %#x)", g_ev);
}

static void test_standdowns(void) {
    /* the bow hand lost 0.1 s: nothing; 0.3 s: the aim ends */
    reset();
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(25, 10);
    g_in.valid[0] = 0;
    steps(9);
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && g_o.ray && NEAR3(g_o.origin, 0, 140, 40, 1e-4), "a brief loss: phase %d",
          g_s.phase);
    g_in.valid[0] = 1;
    steps(2);
    g_in.valid[0] = 0;
    steps(26);
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "bow hand lost, early");
    steps(3);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.last_end == SOMVR_BOW_END_LOST && !g_o.ray, "bow hand lost: phase %d end %d",
          g_s.phase, g_s.last_end);
    /* the string hand lost while drawing: a let-down after 0.3 s, no shot */
    reset();
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(30, 10);
    g_in.valid[1] = 0;
    steps(20);
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && NEAR(g_s.draw_cm, 30, 1e-3), "a brief string loss");
    steps(9);
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM && g_s.letdowns == 1 && g_s.shots == 0, "string hand lost: phase %d", g_s.phase);
    /* the input lost while drawing: the aim ends at once, no shot */
    reset();
    nock_and_grip();
    draw_to(30, 10);
    g_in.live = 0;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.last_end == SOMVR_BOW_END_INPUT && !g_o.aim && g_s.shots == 0, "input lost");
    g_in.live = 1;
    g_in.string = 0;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.shots == 0, "the release after the input loss shot");
    /* from outside */
    reset();
    nock_and_grip();
    somvr_bow_out o;
    somvr_bow_stop(&g_s, SOMVR_BOW_END_OUTSIDE, g_in.t, &o);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && (o.events & SOMVR_BOW_E_EXIT) && o.end_why == SOMVR_BOW_END_OUTSIDE &&
              g_s.exits[SOMVR_BOW_END_OUTSIDE] == 1,
          "stop");
    somvr_bow_stop(&g_s, SOMVR_BOW_END_OUTSIDE, g_in.t, &o);
    CHECK(o.events == 0 && g_s.exits[SOMVR_BOW_END_OUTSIDE] == 1, "stop twice");
    g_in.string = 0;
    step();
    nock_and_grip();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "after the stop: a new draw");
}

static void test_hold_mode(void) {
    reset();
    g_c.fire_mode = SOMVR_BOW_FIRE_HOLD;
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(15, 10);
    CHECK(!g_o.fire && g_fire_ticks == 0, "hold: on before armed");
    g_ev = 0;
    draw_to(30, 30);
    CHECK((g_ev & SOMVR_BOW_E_HOLD) && g_o.fire && g_s.fire_on, "hold: the fire command held from armed");
    int t0 = g_fire_ticks;
    steps(10);
    CHECK(g_fire_ticks == t0 + 10, "hold: held");
    g_ev = 0;
    g_in.string = 0;
    step();
    CHECK((g_ev & SOMVR_BOW_E_RELEASE) && (g_ev & SOMVR_BOW_E_FIRE) && !g_o.fire && g_s.fired == 1 && g_fire_edges == 1 &&
              g_s.phase == SOMVR_BOW_PH_FOLLOW,
          "hold: the release lets it go: fire %d edges %d", g_o.fire, g_fire_edges);
    /* hold mode without the game's aim: never held; the release waits like a tap */
    reset();
    g_c.fire_mode = SOMVR_BOW_FIRE_HOLD;
    nock_and_grip();
    draw_to(30, 30);
    CHECK(!g_s.fire_on, "hold without the game's aim");
    g_in.string = 0;
    step();
    CHECK(g_s.shot, "hold: a pending shot");
    g_in.game_aim = 1;
    step();
    CHECK(g_s.fired == 1 && g_o.fire, "hold: the pending shot tapped");
    /* hold mode: back under the minimum keeps it armed (the release still shoots) */
    reset();
    g_c.fire_mode = SOMVR_BOW_FIRE_HOLD;
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(30, 30);
    draw_to(10, 10);
    g_in.string = 0;
    step();
    CHECK(g_s.shots == 1 && g_s.fired == 1, "hold: armed then eased back");
    /* tap mode: eased back under the minimum = a let-down */
    reset();
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(30, 30);
    draw_to(10, 10);
    g_in.string = 0;
    step();
    CHECK(g_s.shots == 0 && g_s.letdowns == 1, "tap: eased back = let-down");
}

static void test_hands(void) {
    /* the right-handed swap: the bow in the right hand, the left draws */
    reset();
    g_c.bow_hand = 1;
    g_in.game_aim = 1;
    g_in.p[1] = (sv_vec3){ 0, 140, 40 };
    g_in.p[0] = (sv_vec3){ 0, 140, 34 };
    step();
    g_in.string = 1;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW, "bow right: phase %d", g_s.phase);
    for (int i = 1; i <= 20; i++) {
        g_in.p[0].z = 34 - 30 * (float)i / 20;
        step();
    }
    CHECK(NEAR(g_o.draw_cm, 36, 1e-3) && NEAR3(g_o.dir, 0, 0, 1, 1e-5) && NEAR3(g_o.origin, 0, 140, 40, 1e-4), "bow right drawn");
    /* the nock offset: 6 cm up in the bow's frame, the bow yawed 90 deg (its +Y stays up) */
    reset();
    g_c.nock_ofs[1] = 6;
    g_in.q[0] = sv_qyaw(1.5707963f);
    string_at(-5, 146, 40);
    step();
    g_in.string = 1;
    step();
    CHECK(g_s.phase == SOMVR_BOW_PH_DRAW && NEAR(g_o.nock_dist, 5, 1e-3) && NEAR3(g_o.origin, 0, 146, 40, 1e-4),
          "nock offset: dist %g origin %g %g %g", g_o.nock_dist, g_o.origin.x, g_o.origin.y, g_o.origin.z);
    for (int i = 1; i <= 20; i++) {
        g_in.p[1].x = -5 - 25 * (float)i / 20;
        step();
    }
    CHECK(NEAR3(g_o.dir, 1, 0, 0, 1e-5), "nock offset: shot along +X %g %g %g", g_o.dir.x, g_o.dir.y, g_o.dir.z);
}

/* a whole session at 90 Hz: three draws (one let down, two shot), the second while the game's aim comes on late, then
 * the hands apart */
static void test_sequence(void) {
    reset();
    g_in.game_aim = 0;
    nock_and_grip();
    draw_to(10, 20);
    g_in.string = 0;
    steps(5); /* let down */
    g_in.game_aim = 1;
    nock_and_grip();
    draw_to(45, 45);
    steps(30);
    g_in.string = 0;
    steps(20);
    for (int i = 0; i < 30; i++) { /* the string hand back to the bow */
        g_in.p[1].z += 1.2f;
        step();
    }
    nock_and_grip();
    draw_to(50, 30);
    g_in.string = 0;
    steps(100); /* the string hand stays back: the aim ends 0.8 s after the follow-through (0.4 s) */
    CHECK(g_s.phase == SOMVR_BOW_PH_AIM, "sequence: the exit too early");
    steps(15);
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.last_end == SOMVR_BOW_END_APART, "sequence: the exit after the last shot");
    CHECK(g_s.phase == SOMVR_BOW_PH_IDLE && g_s.aims == 1 && g_s.draws == 3 && g_s.shots == 2 && g_s.fired == 2 &&
              g_s.letdowns == 1 && g_s.dropped == 0 && g_fire_edges == 2 && g_s.exits[SOMVR_BOW_END_APART] == 1,
          "sequence: aims %llu draws %llu shots %llu fired %llu letdowns %llu edges %d", (unsigned long long)g_s.aims,
          (unsigned long long)g_s.draws, (unsigned long long)g_s.shots, (unsigned long long)g_s.fired,
          (unsigned long long)g_s.letdowns, g_fire_edges);
    CHECK(g_fire_ticks >= 18 && g_fire_ticks <= 20, "sequence: fire ticks %d", g_fire_ticks);
}

/* P9: the default commands (124 aims; 125 = PC_Stealth is never the fire), our own shot's gates in order, its arrow
 * cost, its fire transform */
static void test_shot(void) {
    CHECK(SOMVR_BOW_AIM_CMD_DEFAULT == 124 && SOMVR_BOW_FIRE_CMD_DEFAULT == 0 &&
              SOMVR_BOW_FIRE_CMD_DEFAULT != SOMVR_BOW_STEALTH_CMD && SOMVR_BOW_STEALTH_CMD == 125,
          "defaults: aim %d fire %d (125 is the stealth mode's command)", SOMVR_BOW_AIM_CMD_DEFAULT,
          SOMVR_BOW_FIRE_CMD_DEFAULT);
    somvr_bow_shot_in ok = { 1, 1, 1, 1, 1, 10, 0, { 90, 180, 340 }, unit((sv_vec3){ -5, 5, 30 }), 0 };
    CHECK(somvr_bow_shot_check(&ok) == SOMVR_BOW_SHOT_OK && somvr_bow_shot_cost(&ok) == -1, "all gates pass, one arrow");
    /* each gate alone, and the first one that fails wins */
    somvr_bow_shot_in s = ok;
    s.sites = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_SITES, "sites");
    s = ok, s.game_aim = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_NO_AIM, "the game's aim");
    s = ok, s.combat = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_COMBAT, "combat");
    s = ok, s.item = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_NO_ITEM, "item");
    s = ok, s.ranged = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_NOT_RANGED, "not ranged");
    s = ok, s.arrows = -1;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_ARROWS_UNKNOWN, "arrows unknown");
    s = ok, s.arrows = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_NO_ARROWS, "no arrows");
    s.unlimited = 1;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_OK && somvr_bow_shot_cost(&s) == 0, "unlimited: no arrow needed or taken");
    s.arrows = -1;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_OK, "unlimited with the count unknown");
    s = ok, s.dir = (sv_vec3){ 0, 0, 0 };
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_RAY, "a zero direction");
    s = ok, s.origin.y = NAN;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_RAY, "a NaN origin");
    s = ok, s.sites = 0, s.ranged = 0, s.arrows = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_SITES, "the first failing gate");
    s = ok, s.ranged = 0, s.arrows = 0;
    CHECK(somvr_bow_shot_check(&s) == SOMVR_BOW_SHOT_NOT_RANGED, "not ranged before no arrows");
    CHECK(!strcmp(somvr_bow_shot_why(SOMVR_BOW_SHOT_NO_ARROWS), "no arrows") && !strcmp(somvr_bow_shot_why(SOMVR_BOW_SHOT_OK), "ok") &&
              !strcmp(somvr_bow_shot_why(SOMVR_BOW_SHOT_NOT_RANGED), "the active item has no RangedWeaponData") &&
              !strcmp(somvr_bow_shot_why(SOMVR_BOW_NSHOT), "?") && !strcmp(somvr_bow_shot_why(-1), "?"),
          "reasons");
    /* the fire transform: the origin, +Z along the shot, +Y up in its vertical plane (no roll), a unit quaternion */
    float xf[7];
    somvr_bow_fire_xf(ok.origin, ok.dir, 0, xf);
    sv_quat q = { xf[3], xf[4], xf[5], xf[6] };
    sv_vec3 f = sv_qrot(q, (sv_vec3){ 0, 0, 1 }), u = sv_qrot(q, (sv_vec3){ 0, 1, 0 }), r = sv_qrot(q, (sv_vec3){ 1, 0, 0 });
    CHECK(xf[0] == 90 && xf[1] == 180 && xf[2] == 340 && NEAR3(f, ok.dir.x, ok.dir.y, ok.dir.z, 1e-5) && u.y > 0.9f &&
              fabsf(r.y) < 1e-5f && NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1, 1e-5),
          "fire xf: pos %g %g %g fwd %g %g %g up %g %g %g", xf[0], xf[1], xf[2], f.x, f.y, f.z, u.x, u.y, u.z);
    somvr_bow_fire_xf((sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 1, 0 }, 1.0f, xf); /* straight up: the hint's heading */
    q = (sv_quat){ xf[3], xf[4], xf[5], xf[6] };
    f = sv_qrot(q, (sv_vec3){ 0, 0, 1 }), u = sv_qrot(q, (sv_vec3){ 0, 1, 0 });
    CHECK(NEAR3(f, 0, 1, 0, 1e-5) && NEAR(atan2f(-u.x, -u.z), 1.0f, 1e-4), "fire xf straight up: fwd %g %g %g up %g %g %g", f.x,
          f.y, f.z, u.x, u.y, u.z);
}

int main(void) {
    test_geometry();
    test_shot();
    test_release_now();
    test_enter();
    test_draw_and_shoot();
    test_timing();
    test_button();
    test_slot();
    test_hold_anatomy();
    test_quiver();
    test_zone();
    test_standdowns();
    test_hold_mode();
    test_hands();
    test_sequence();
    printf("test_bow: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
