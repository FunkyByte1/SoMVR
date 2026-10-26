/* test_fingers.c - unit tests of somvr_fingers.c on run 3's skeleton and pose (tests/run3_skeleton.h): both hands'
 * finger joints by hash (run 3's indices; a missing joint, a wrong parent, an extra child, bad input), the rig's mirror
 * (each left joint's offset in its parent is minus the right one's: equal local rotations make a mirrored hand), the
 * open pose = run 3's right idle, the targets (a held weapon, the grip, the trigger and its touch, the thumb's touch,
 * no touch sensing, NaN), the ease (the first step, tau, 95 % within 90 ms, no overshoot, the dt clamp, the bow hand's
 * weight out and in), and the pose (the open and the fist locals exactly, the offsets and every other node kept, a
 * half weight, weight 0 and a broken pose untouched, the fingertips toward the palm, both hands mirrored in their own
 * frames, continuity). */
#include "../somvr_fingers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "run3_skeleton.h"

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

static void load_pose(sa_node *p) {
    for (int i = 0; i < RUN3_N; i++) {
        const float *r = RUN3_POSE[i];
        sa_node_set(&p[i], (sa_xf){{r[0], r[1], r[2]}, {r[3], r[4], r[5], r[6]}});
    }
}
static float qang(sv_quat a, sv_quat b) { /* the angle between two rotations (atan2: exact near 0, unlike acos) */
    sv_quat r = sv_qmul(sv_qconj(sv_qnormalize(a)), sv_qnormalize(b));
    return 2 * atan2f(sqrtf(r.x * r.x + r.y * r.y + r.z * r.z), fabsf(r.w));
}
static float vdist(sv_vec3 a, sv_vec3 b) {
    return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
static sv_vec3 npos(const sa_node *n) { return (sv_vec3){n->pos[0], n->pos[1], n->pos[2]}; }
static int find(const char *name) {
    for (int i = 0; i < RUN3_N; i++)
        if (!strcmp(RUN3_NAME[i], name)) return i;
    return -1;
}

static fg_hand g_f[2];

static void test_build(void) {
    char why[300];
    static const char *const fn[FG_NF] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
    for (int s = 0; s < 2; s++) {
        int hand = find(s ? "R_Hand" : "L_Hand");
        int r = fg_hand_build(&g_f[s], s, hand, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why);
        CHECK(r == 0 && g_f[s].hand == hand, "side %d: %s", s, why);
        int names = 1;
        for (int a = 0; a < FG_NF; a++)
            for (int k = 0; k < FG_NJ; k++) {
                char n[24];
                snprintf(n, sizeof n, "%s_%s%d", s ? "R" : "L", fn[a], k + 1);
                names &= g_f[s].j[a][k] == find(n);
            }
        CHECK(names, "side %d: every joint is the node of its name (%s)", s, why);
    }
    CHECK(g_f[1].j[FG_THUMB][0] == 114 && g_f[1].j[FG_PINKY][2] == 129 && g_f[0].j[FG_THUMB][0] == 92 &&
              g_f[0].j[FG_PINKY][2] == 107 && g_f[1].j[FG_INDEX][1] == 122 && g_f[0].j[FG_RING][2] == 105,
          "run 3's indices");
    fg_hand b;
    uint32_t hash[RUN3_N];
    uint8_t par[RUN3_N];
    memcpy(hash, RUN3_HASH, sizeof hash), memcpy(par, RUN3_PARENT, sizeof par);
    hash[find("R_Middle2")] ^= 0x100;
    CHECK(fg_hand_build(&b, SA_R, find("R_Hand"), par, hash, RUN3_N, why, sizeof why) < 0 && strstr(why, "no R middle joint 2") &&
              b.hand < 0,
          "a missing joint: %s", why);
    memcpy(hash, RUN3_HASH, sizeof hash);
    par[find("R_Index3")] = (uint8_t)find("R_Index1");
    CHECK(fg_hand_build(&b, SA_R, find("R_Hand"), par, hash, RUN3_N, why, sizeof why) < 0 && strstr(why, "hangs below"),
          "a wrong parent: %s", why);
    memcpy(par, RUN3_PARENT, sizeof par);
    par[find("R_Weapon")] = (uint8_t)find("R_Index3"); /* the weapon node below a fingertip: it would be left behind */
    CHECK(fg_hand_build(&b, SA_R, find("R_Hand"), par, hash, RUN3_N, why, sizeof why) < 0 && strstr(why, "not supported"),
          "an extra child: %s", why);
    memcpy(par, RUN3_PARENT, sizeof par);
    CHECK(fg_hand_build(&b, 2, find("R_Hand"), par, hash, RUN3_N, why, sizeof why) < 0 &&
              fg_hand_build(&b, SA_L, find("R_Hand"), par, hash, RUN3_N, why, sizeof why) < 0 &&
              fg_hand_build(&b, SA_R, 0, par, hash, RUN3_N, why, sizeof why) < 0 &&
              fg_hand_build(&b, SA_R, find("R_Hand"), par, hash, 1, why, sizeof why) < 0 &&
              fg_hand_build(&b, SA_R, find("R_Hand"), NULL, hash, RUN3_N, why, sizeof why) < 0,
          "a bad side, the other side's hand, no hand, too few nodes, no parents");
}

static void test_mirror(void) {
    static sa_node p[RUN3_N];
    load_pose(p);
    float dmax = 0, omax = 0;
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++) {
            sa_xf l, r;
            CHECK(fg_local(p, &g_f[0], a, k, &l) && fg_local(p, &g_f[1], a, k, &r), "locals %d %d", a, k);
            dmax = fmaxf(dmax, vdist(l.p, (sv_vec3){-r.p.x, -r.p.y, -r.p.z}));
            omax = fmaxf(omax, qang(r.q, fg_pose_open.q[a][k]));
        }
    CHECK(dmax < 0.01f, "the rig's mirror: each left offset = minus the right one (max %.4f cm)", dmax);
    CHECK(omax < 1e-3f, "the open pose is run 3's right idle (max %.5f rad)", omax);
    /* the idle's middle and tip joints turn about +Z on both sides (the curl axis the fist uses) */
    int z = 1;
    for (int s = 0; s < 2; s++)
        for (int a = FG_INDEX; a < FG_NF; a++) {
            sa_xf l;
            fg_local(p, &g_f[s], a, 1, &l);
            z &= l.q.z > 0.2f && fabsf(l.q.x) < 0.01f && fabsf(l.q.y) < 0.01f;
        }
    CHECK(z, "the middle joints curl about +Z on both sides");
}

static void test_targets(void) {
    fg_cfg c;
    fg_cfg_default(&c);
    CHECK(NEAR(c.tau_s, 0.03, 1e-6) && NEAR(c.blend_s, 0.1, 1e-6) && NEAR(c.index_touch, 0.2, 1e-6) &&
              NEAR(c.thumb_rest, 0.6, 1e-6),
          "the defaults");
    float t[FG_NF];
    fg_in in = {.squeeze = 0, .trigger = 0, .hold = FG_HOLD_FIST};
    fg_targets(&c, &in, t);
    CHECK(t[0] == 1 && t[1] == 1 && t[2] == 1 && t[3] == 1 && t[4] == 1, "a held weapon: a fist");
    in = (fg_in){.squeeze = 0.7f, .trigger = 0.4f, .touch_ok = 1};
    fg_targets(&c, &in, t);
    CHECK(NEAR(t[FG_MIDDLE], 0.7, 1e-6) && NEAR(t[FG_RING], 0.7, 1e-6) && NEAR(t[FG_PINKY], 0.7, 1e-6) &&
              NEAR(t[FG_INDEX], 0.4, 1e-6) && t[FG_THUMB] == 0,
          "the grip, the trigger, the thumb lifted: %g %g %g", t[FG_MIDDLE], t[FG_INDEX], t[FG_THUMB]);
    in.thumb_touch = 1;
    fg_targets(&c, &in, t);
    CHECK(NEAR(t[FG_THUMB], 0.6 + 0.4 * 0.7, 1e-6), "the thumb resting: %g", t[FG_THUMB]);
    in = (fg_in){.squeeze = 1, .trigger = 0, .touch_ok = 1, .thumb_touch = 1};
    fg_targets(&c, &in, t);
    CHECK(t[FG_INDEX] == 0 && t[FG_THUMB] == 1 && t[FG_MIDDLE] == 1, "pointing: the index out, the thumb down");
    in = (fg_in){.squeeze = 1, .trigger = 1, .touch_ok = 1, .thumb_touch = 0};
    fg_targets(&c, &in, t);
    CHECK(t[FG_INDEX] == 1 && t[FG_THUMB] == 0, "a thumb up");
    in = (fg_in){.squeeze = 0, .trigger = 0.05f, .trigger_touch = 1, .touch_ok = 1};
    fg_targets(&c, &in, t);
    CHECK(NEAR(t[FG_INDEX], 0.2, 1e-6), "the trigger touched: the index a little (%g)", t[FG_INDEX]);
    in.trigger = 0.9f;
    fg_targets(&c, &in, t);
    CHECK(NEAR(t[FG_INDEX], 0.9, 1e-6), "touched and pulled: the trigger (%g)", t[FG_INDEX]);
    in = (fg_in){.squeeze = 0.8f, .trigger = 0, .touch_ok = 0, .thumb_touch = 0};
    fg_targets(&c, &in, t);
    CHECK(NEAR(t[FG_THUMB], 0.8, 1e-6), "no touch sensing: the thumb follows the grip (%g)", t[FG_THUMB]);
    in = (fg_in){.squeeze = NAN, .trigger = 7, .touch_ok = 1};
    fg_targets(&c, &in, t);
    CHECK(t[FG_MIDDLE] == 0 && t[FG_INDEX] == 1, "NaN and out of range clamped");
    in = (fg_in){.squeeze = 0.3f, .hold = FG_HOLD_GAME};
    fg_targets(&c, &in, t);
    CHECK(NEAR(t[FG_MIDDLE], 0.3, 1e-6), "the bow hand: the targets still follow (its weight goes)");
    CHECK(!strcmp(fg_finger_name(FG_THUMB), "thumb") && !strcmp(fg_finger_name(FG_PINKY), "pinky") &&
              !strcmp(fg_finger_name(9), "?"),
          "the names");
}

static void test_step(void) {
    fg_cfg c;
    fg_cfg_default(&c);
    fg_state s;
    memset(&s, 0, sizeof s);
    fg_in in = {.squeeze = 0, .touch_ok = 1};
    float w = fg_step(&s, &c, &in, 0.011f);
    CHECK(s.have && w == 1 && s.c[FG_MIDDLE] == 0, "the first step: at the targets, the weight on");
    in.squeeze = 1;
    w = fg_step(&s, &c, &in, 0.011f);
    float k1 = 1 - expf(-0.011f / 0.03f);
    CHECK(NEAR(s.c[FG_MIDDLE], k1, 1e-5) && w == 1, "one 11 ms step toward a closed grip: %g (want %g)", s.c[FG_MIDDLE], k1);
    float prev = s.c[FG_MIDDLE], tt = 0.011f;
    int mono = 1, over = 0;
    for (int i = 0; i < 7; i++) {
        fg_step(&s, &c, &in, 0.011f), tt += 0.011f;
        mono &= s.c[FG_MIDDLE] > prev, over |= s.c[FG_MIDDLE] > 1;
        prev = s.c[FG_MIDDLE];
    }
    CHECK(mono && !over && s.c[FG_MIDDLE] > 0.94f && s.c[FG_MIDDLE] < 1, "closing: smooth, %.0f ms -> %.3f, no overshoot",
          tt * 1e3, s.c[FG_MIDDLE]);
    /* no snap: the largest change per 11 ms frame from fully open to closed */
    memset(&s, 0, sizeof s);
    in.squeeze = 0;
    fg_step(&s, &c, &in, 0.011f);
    in.squeeze = 1;
    float dmax = 0, last = 0;
    for (int i = 0; i < 30; i++) {
        fg_step(&s, &c, &in, 0.011f);
        dmax = fmaxf(dmax, s.c[FG_MIDDLE] - last), last = s.c[FG_MIDDLE];
    }
    CHECK(dmax < 0.32f, "the largest step of one frame: %.3f of the full curl", dmax);
    /* the dt clamp: a 1 s hitch moves as 0.1 s */
    memset(&s, 0, sizeof s);
    in.squeeze = 0;
    fg_step(&s, &c, &in, 0.011f);
    in.squeeze = 1;
    fg_step(&s, &c, &in, 1.0f);
    CHECK(NEAR(s.c[FG_MIDDLE], 1 - expf(-0.1f / 0.03f), 1e-5), "a 1 s hitch counts 0.1 s: %g", s.c[FG_MIDDLE]);
    /* the bow hand: the weight eases out over blend_s and back in */
    in.hold = FG_HOLD_GAME;
    float wa = fg_step(&s, &c, &in, 0.05f), wb = fg_step(&s, &c, &in, 0.05f);
    CHECK(NEAR(wa, 0.5, 1e-5) && wb == 0, "the bow: the weight 1 -> %.3f -> %.3f in 0.1 s", wa, wb);
    in.hold = FG_HOLD_NONE;
    wa = fg_step(&s, &c, &in, 0.025f);
    CHECK(NEAR(wa, 0.15625, 1e-4), "back: smoothstep(0.25) = %.4f", wa);
    memset(&s, 0, sizeof s);
    in.hold = FG_HOLD_GAME;
    CHECK(fg_step(&s, &c, &in, 0.011f) == 0, "a first step on the bow hand: the game's fingers");
    /* tau 0: at once */
    c.tau_s = 0;
    memset(&s, 0, sizeof s);
    in = (fg_in){.squeeze = 0};
    fg_step(&s, &c, &in, 0.011f);
    in.squeeze = 1;
    fg_step(&s, &c, &in, 0.011f);
    CHECK(s.c[FG_MIDDLE] == 1, "tau 0: no ease");
}

static void test_apply(void) {
    static sa_node p0[RUN3_N], p[RUN3_N];
    load_pose(p0);
    float curl[FG_NF];
    /* open, full weight: the open locals exactly, offsets kept, every other node bit for bit */
    memcpy(p, p0, sizeof p);
    for (int a = 0; a < FG_NF; a++) curl[a] = 0;
    CHECK(fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1), "applied");
    int other = 1;
    for (int i = 0; i < RUN3_N; i++) {
        int fj = 0;
        for (int a = 0; a < FG_NF; a++)
            for (int k = 0; k < FG_NJ; k++) fj |= g_f[1].j[a][k] == i;
        if (!fj) other &= !memcmp(&p[i], &p0[i], sizeof p[i]);
    }
    CHECK(other, "the hand, the weapon node and the rest untouched");
    float qe = 0, te = 0;
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++) {
            sa_xf l, l0;
            fg_local(p, &g_f[1], a, k, &l), fg_local(p0, &g_f[1], a, k, &l0);
            qe = fmaxf(qe, qang(l.q, fg_pose_open.q[a][k]));
            te = fmaxf(te, vdist(l.p, l0.p));
        }
    CHECK(qe < 1e-3f && te < 2e-3f, "open: the locals %.5f rad, the offsets %.5f cm", qe, te);
    /* the fist */
    memcpy(p, p0, sizeof p);
    for (int a = 0; a < FG_NF; a++) curl[a] = 1;
    fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1);
    qe = 0, te = 0;
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++) {
            sa_xf l, l0;
            fg_local(p, &g_f[1], a, k, &l), fg_local(p0, &g_f[1], a, k, &l0);
            qe = fmaxf(qe, qang(l.q, fg_pose_fist.q[a][k]));
            te = fmaxf(te, vdist(l.p, l0.p));
        }
    CHECK(qe < 1e-3f && te < 2e-3f, "a fist: the locals %.5f rad, the offsets %.5f cm", qe, te);
    /* the fingers folded: each finger's tip joint at least 1 cm closer to its knuckle than open */
    static sa_node po[RUN3_N];
    memcpy(po, p0, sizeof po);
    for (int a = 0; a < FG_NF; a++) curl[a] = 0;
    fg_apply(po, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1);
    int folded = 1;
    for (int a = FG_INDEX; a < FG_NF; a++) {
        const int *j = g_f[1].j[a];
        float df = vdist(npos(&p[j[2]]), npos(&p[j[0]])), dop = vdist(npos(&po[j[2]]), npos(&po[j[0]]));
        folded &= df < dop - 1.0f;
        if (df >= dop - 1.0f) printf("  %s: fist %.1f open %.1f cm\n", fg_finger_name(a), df, dop);
    }
    CHECK(folded, "a fist: every finger folded (its tip joint 1 cm closer to its knuckle)");
    /* half the weight: halfway (slerp) from the game's locals to the target */
    memcpy(p, p0, sizeof p);
    fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, (float[FG_NF]){1, 1, 1, 1, 1}, 0.5f);
    float he = 0;
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++) {
            sa_xf l, l0;
            fg_local(p, &g_f[1], a, k, &l), fg_local(p0, &g_f[1], a, k, &l0);
            float full = qang(l0.q, fg_pose_fist.q[a][k]);
            he = fmaxf(he, fabsf(qang(l0.q, l.q) - 0.5f * full) + fabsf(qang(l.q, fg_pose_fist.q[a][k]) - 0.5f * full));
        }
    CHECK(he < 2e-3f, "half the weight: halfway (%.5f rad off)", he);
    /* nothing written: weight 0, a broken pose, a bad hand */
    memcpy(p, p0, sizeof p);
    CHECK(!fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 0) && !memcmp(p, p0, sizeof p), "weight 0");
    p[g_f[1].j[FG_RING][1]].pos[1] = NAN;
    static sa_node pb[RUN3_N];
    memcpy(pb, p, sizeof pb);
    CHECK(!fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1) && !memcmp(p, pb, sizeof p),
          "a NaN joint: untouched");
    memcpy(p, p0, sizeof p);
    p[g_f[1].hand].q[0] = p[g_f[1].hand].q[1] = p[g_f[1].hand].q[2] = p[g_f[1].hand].q[3] = 0;
    memcpy(pb, p, sizeof pb);
    CHECK(!fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1) && !memcmp(p, pb, sizeof p),
          "a zero hand rotation: untouched");
    fg_hand bad = g_f[1];
    bad.hand = -1;
    CHECK(!fg_apply(p, RUN3_N, &bad, &fg_pose_open, &fg_pose_fist, curl, 1) &&
              !fg_apply(p, 100, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1),
          "no hand, a joint past n");
    /* the same locals on both hands: each hand's joints mirrored in its own frame (p_L = -p_R in the hand's frame) */
    for (int ci = 0; ci <= 4; ci++) {
        float c = 0.25f * (float)ci, m = 0;
        memcpy(p, p0, sizeof p);
        for (int a = 0; a < FG_NF; a++) curl[a] = c;
        fg_apply(p, RUN3_N, &g_f[0], &fg_pose_open, &fg_pose_fist, curl, 1);
        fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1);
        sa_xf hl = sa_xf_inv(sa_node_xf(&p[g_f[0].hand])), hr = sa_xf_inv(sa_node_xf(&p[g_f[1].hand]));
        for (int a = 0; a < FG_NF; a++)
            for (int k = 0; k < FG_NJ; k++) {
                sv_vec3 l = sa_xf_mul(hl, sa_node_xf(&p[g_f[0].j[a][k]])).p, r = sa_xf_mul(hr, sa_node_xf(&p[g_f[1].j[a][k]])).p;
                m = fmaxf(m, vdist(l, (sv_vec3){-r.x, -r.y, -r.z}));
            }
        CHECK(m < 0.02f, "curl %.2f: the left hand the right one mirrored (%.4f cm)", c, m);
    }
    /* continuity: the fingertips move little per 1 % of curl */
    float jump = 0;
    static sa_node pl[RUN3_N];
    for (int i = 0; i <= 100; i++) {
        memcpy(p, p0, sizeof p);
        for (int a = 0; a < FG_NF; a++) curl[a] = 0.01f * (float)i;
        fg_apply(p, RUN3_N, &g_f[1], &fg_pose_open, &fg_pose_fist, curl, 1);
        if (i)
            for (int a = 0; a < FG_NF; a++) jump = fmaxf(jump, vdist(npos(&p[g_f[1].j[a][2]]), npos(&pl[g_f[1].j[a][2]])));
        memcpy(pl, p, sizeof pl);
    }
    CHECK(jump < 0.3f, "continuous: at most %.3f cm per 1 %% of curl", jump);
}

int main(void) {
    test_build();
    test_mirror();
    test_targets();
    test_step();
    test_apply();
    printf("test_fingers: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
