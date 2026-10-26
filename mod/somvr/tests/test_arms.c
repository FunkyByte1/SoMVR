/* test_arms.c - unit tests of somvr_arms.c on run 3's skeleton and pose (tests/run3_skeleton.h): the chains (indices,
 * subtrees, broken skeletons), run 3's elbow hinge, the reach (hand and weapon node on their targets, rigid bones and
 * subtrees, a natural bend), the clamping (far, near, at the shoulder), the pole plane (the hint's side, mirrored for the
 * left, the animated elbow's share), no NaNs (bad targets, bad poses, degenerate hints), continuity along paths at 90 Hz
 * (through the overhead, across the body, through the hint's own direction, out of reach) and over the weight, the
 * round trips (transforms, the rig, the grip -> weapon node -> bone grip, an FK pose solved back onto itself,
 * idempotence), the twist shares, the weight's ease and the held target, the bone blade against the controller blade,
 * and the default grip calibration. */
#include "../somvr_arms.h"

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
#define DEG 0.01745329252f
#define DT (1.0f / 90.0f)

static sv_vec3 V(float x, float y, float z) { return (sv_vec3){x, y, z}; }
static sv_vec3 sub3(sv_vec3 a, sv_vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static sv_vec3 add3(sv_vec3 a, sv_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static sv_vec3 mul3(sv_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }
static float dot3(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static sv_vec3 cross3(sv_vec3 a, sv_vec3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float len3(sv_vec3 a) { return sqrtf(dot3(a, a)); }
static sv_vec3 norm3(sv_vec3 a) { return mul3(a, 1.0f / len3(a)); }
static sv_vec3 P(const sa_node *n) { return V(n->pos[0], n->pos[1], n->pos[2]); }
static sv_quat Q(const sa_node *n) { return sv_qnormalize(sv_quat_of(n->q)); }
static float qang(sv_quat a, sv_quat b) { /* the angle between two rotations, rad (a^-1 b in double: exact near 0) */
    double ax = -a.x, ay = -a.y, az = -a.z, aw = a.w;
    double x = aw * b.x + ax * b.w + ay * b.z - az * b.y, y = aw * b.y - ax * b.z + ay * b.w + az * b.x,
           z = aw * b.z + ax * b.y - ay * b.x + az * b.w, w = aw * b.w - ax * b.x - ay * b.y - az * b.z;
    return (float)(2.0 * atan2(sqrt(x * x + y * y + z * z), fabs(w)));
}
static sv_quat qax(sv_vec3 u, float a) {
    float s = sinf(0.5f * a);
    return (sv_quat){u.x * s, u.y * s, u.z * s, cosf(0.5f * a)};
}
static unsigned g_rs = 12345;
static float rnd(void) { /* 0..1 */
    g_rs = g_rs * 1103515245u + 12345u;
    return (float)((g_rs >> 8) & 0xffff) / 65535.0f;
}
static sv_vec3 rdir(void) {
    for (;;) {
        sv_vec3 v = V(2 * rnd() - 1, 2 * rnd() - 1, 2 * rnd() - 1);
        float l = len3(v);
        if (l > 0.1f && l < 1) return mul3(v, 1 / l);
    }
}
static sv_quat rquat(void) { return sv_qnormalize((sv_quat){2 * rnd() - 1, 2 * rnd() - 1, 2 * rnd() - 1, 2 * rnd() - 1}); }

static sa_node g_pose0[RUN3_N];
static sa_chain g_ch[2];
static void load(sa_node *pose) {
    for (int i = 0; i < RUN3_N; i++) {
        const float *r = RUN3_POSE[i];
        sa_node_set(&pose[i], (sa_xf){V(r[0], r[1], r[2]), (sv_quat){r[3], r[4], r[5], r[6]}});
    }
}
static sa_cfg cfg_plain(void) { /* the hint only, no rate limit: the geometric tests */
    sa_cfg c;
    sa_cfg_default(&c);
    c.pole_anim = 0, c.pole_dps = 0;
    return c;
}
static int solve_fresh(sa_node *pose, int side, const sa_cfg *c, sa_xf t, float w, sa_report *rep) {
    sa_arm_state st;
    memset(&st, 0, sizeof st);
    return sa_solve(pose, RUN3_N, &g_ch[side], c, t, w, DT, &st, rep);
}
/* the local transform of node j in its parent */
static sa_xf local(const sa_node *pose, int j) {
    return sa_xf_mul(sa_xf_inv(sa_node_xf(&pose[RUN3_PARENT[j]])), sa_node_xf(&pose[j]));
}
/* every node of the arm keeps its local position; all but S, E, W and the twist nodes keep their local rotation */
static void check_rigid(const sa_node *pose, int side, const char *what) {
    const sa_chain *c = &g_ch[side];
    float dp = 0, dq = 0;
    for (int j = c->lo; j <= c->hi; j++) {
        sa_xf a = local(g_pose0, j), b = local(pose, j);
        float e = len3(sub3(a.p, b.p));
        dp = e > dp ? e : dp;
        if (j != c->s && j != c->e && j != c->w && j != c->tu && j != c->tf) {
            float r = qang(a.q, b.q);
            dq = r > dq ? r : dq;
        }
    }
    CHECK(dp < 5e-3f && dq < 2e-4f, "%s: the arm is not rigid: local positions off by %g cm, local rotations by %g rad",
          what, dp, dq);
}
static int finite_arm(const sa_node *pose, int side) {
    const sa_chain *c = &g_ch[side];
    for (int j = c->lo; j <= c->hi; j++)
        for (int k = 0; k < 3; k++)
            if (!isfinite(pose[j].pos[k]) || !isfinite(pose[j].q[k]) || !isfinite(pose[j].q[3])) return 0;
    return 1;
}

/* ---- the chains ------------------------------------------------------------------------------------------------- */
static void test_chain(void) {
    char why[200];
    int r = sa_chain_build(&g_ch[SA_R], SA_R, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why);
    CHECK(r == 0, "right chain: %s", why);
    const sa_chain *c = &g_ch[SA_R];
    CHECK(c->s == 108 && c->e == 110 && c->w == 112 && c->tu == 111 && c->tf == 113 && c->wpn == 119 && c->p == 26,
          "right: S %d E %d W %d twist %d/%d weapon %d parent %d", c->s, c->e, c->w, c->tu, c->tf, c->wpn, c->p);
    CHECK(c->nsub[0] == 20 && c->nsub[1] == 18 && c->nsub[2] == 16 && c->lo == 108 && c->hi == 129 && c->ntw[0] == 0 &&
              c->ntw[1] == 0,
          "right subtrees %d/%d/%d range %d..%d twist subtrees %d/%d (re-arms-ik.md 2.2: 20/18/16)", c->nsub[0], c->nsub[1],
          c->nsub[2], c->lo, c->hi, c->ntw[0], c->ntw[1]);
    r = sa_chain_build(&g_ch[SA_L], SA_L, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why);
    c = &g_ch[SA_L];
    CHECK(r == 0 && c->s == 86 && c->e == 88 && c->w == 90 && c->tu == 89 && c->tf == 91 && c->wpn == 97 && c->p == 25 &&
              c->nsub[0] == 20 && c->nsub[1] == 18 && c->nsub[2] == 16 && c->lo == 86 && c->hi == 107,
          "left: %s", why);
    for (int i = 0; i < 16; i++) CHECK(g_ch[SA_R].sub[2][i] == 114 + i, "right sub(W)[%d] = %d", i, g_ch[SA_R].sub[2][i]);

    uint8_t par[RUN3_N];
    uint32_t h[RUN3_N];
    sa_chain x;
    memcpy(h, RUN3_HASH, sizeof h);
    h[110] = 0x1234;
    CHECK(sa_chain_build(&x, SA_R, RUN3_PARENT, h, RUN3_N, why, sizeof why) < 0 && strstr(why, "R_Arml"),
          "no R_Arml: %s", why);
    memcpy(h, RUN3_HASH, sizeof h);
    h[5] = SA_H_R_HAND;
    CHECK(sa_chain_build(&x, SA_R, RUN3_PARENT, h, RUN3_N, why, sizeof why) < 0 && strstr(why, "two nodes"),
          "two R_Hand: %s", why);
    memcpy(par, RUN3_PARENT, sizeof par);
    par[110] = 26;
    CHECK(sa_chain_build(&x, SA_R, par, RUN3_HASH, RUN3_N, why, sizeof why) < 0 && strstr(why, "parent chain"),
          "R_Arml under the clavicle: %s", why);
    memcpy(par, RUN3_PARENT, sizeof par);
    par[50] = 120;
    CHECK(sa_chain_build(&x, SA_R, par, RUN3_HASH, RUN3_N, why, sizeof why) < 0 && strstr(why, "precede"),
          "a parent after its child: %s", why);
    memcpy(h, RUN3_HASH, sizeof h);
    h[119] = 1, h[111] = 2;
    CHECK(sa_chain_build(&x, SA_R, RUN3_PARENT, h, RUN3_N, why, sizeof why) == 0 && x.wpn == -1 && x.tu == -1 && x.tf == 113,
          "no weapon node, no bicep: still an arm (%s)", why);
    CHECK(sa_chain_build(&x, SA_R, RUN3_PARENT, RUN3_HASH, 1, why, sizeof why) < 0 &&
              sa_chain_build(&x, 5, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why) < 0 &&
              sa_chain_build(&x, SA_R, NULL, RUN3_HASH, RUN3_N, why, sizeof why) < 0,
          "bad arguments accepted");
    load(g_pose0);
    /* run 3's elbows: a pure hinge about the upper arm's local -Z, and upper arm x forearm along R(q_S)(0, 0, -1) */
    for (int s = 0; s < 2; s++) {
        c = &g_ch[s];
        sv_vec3 u = sub3(P(&g_pose0[c->e]), P(&g_pose0[c->s])), f = sub3(P(&g_pose0[c->w]), P(&g_pose0[c->e]));
        sv_vec3 k = norm3(cross3(u, f)), hz = sv_qrot(Q(&g_pose0[c->s]), V(SA_HINGE_X, SA_HINGE_Y, SA_HINGE_Z));
        CHECK(dot3(k, hz) > 0.999f, "%s: the bend axis is not R(q_S)(0,0,-1): cos %g", s ? "right" : "left", dot3(k, hz));
        CHECK(NEAR(len3(u), 33.92, 0.02) && NEAR(len3(f), 27.42, 0.02), "%s: bones %g %g (33.92, 27.42)", s ? "R" : "L",
              len3(u), len3(f));
    }
}

/* ---- reach, clamping, the pole plane ---------------------------------------------------------------------------- */
static void test_reach(void) {
    sa_cfg cf = cfg_plain();
    sa_node pose[RUN3_N];
    int bad_hand = 0, bad_rot = 0, bad_len = 0, bad_bend = 0, bad_wpn = 0, n = 0;
    for (int s = 0; s < 2; s++) {
        const sa_chain *c = &g_ch[s];
        sv_vec3 pS = P(&g_pose0[c->s]);
        for (int k = 0; k < 400; k++) {
            float L = 7.0f + rnd() * (61.34f - 8.0f);
            sa_xf t = {add3(pS, mul3(rdir(), L)), rquat()};
            memcpy(pose, g_pose0, sizeof pose);
            sa_report rep;
            if (!solve_fresh(pose, s, &cf, t, 1, &rep)) {
                bad_hand++;
                continue;
            }
            n++;
            bad_hand += !(len3(sub3(P(&pose[c->w]), t.p)) < 1e-3f) || !rep.reached || rep.clamp;
            bad_rot += !(qang(Q(&pose[c->w]), t.q) < 1e-3f);
            bad_len += !(NEAR(len3(sub3(P(&pose[c->e]), P(&pose[c->s]))), 33.92, 0.02) &&
                         NEAR(len3(sub3(P(&pose[c->w]), P(&pose[c->e]))), 27.42, 0.02) &&
                         len3(sub3(P(&pose[c->s]), pS)) < 1e-4f);
            /* the elbow bends the natural way: E's local rotation is about S's -Z by a positive angle */
            sv_quat le = sv_qmul(sv_qconj(Q(&pose[c->s])), Q(&pose[c->e]));
            if (le.w < 0) le = (sv_quat){-le.x, -le.y, -le.z, -le.w};
            float sz = sqrtf(le.x * le.x + le.y * le.y + le.z * le.z);
            bad_bend += sz > 1e-3f && !(-le.z / sz > 0.999f);
            if (k % 20 == 0) check_rigid(pose, s, "a reachable target");
            /* the weapon node onto its own target */
            memcpy(pose, g_pose0, sizeof pose);
            sa_xf tw = {add3(pS, mul3(rdir(), 20 + rnd() * 25)), rquat()}; /* the hand within 12 cm of it: in reach */
            if (solve_fresh(pose, s, &cf, sa_hand_target(pose, c, tw), 1, &rep))
                bad_wpn += !(len3(sub3(P(&pose[c->wpn]), tw.p)) < 5e-3f && qang(Q(&pose[c->wpn]), tw.q) < 1e-3f);
        }
    }
    CHECK(n == 800 && !bad_hand && !bad_rot && !bad_len && !bad_bend && !bad_wpn,
          "reach: %d solved; hand off %d, rotation off %d, bones %d, unnatural bends %d, weapon node off %d", n, bad_hand,
          bad_rot, bad_len, bad_bend, bad_wpn);

    /* clamping: out of reach, inside the minimum, at the shoulder */
    memcpy(pose, g_pose0, sizeof pose);
    const sa_chain *c = &g_ch[SA_R];
    sv_vec3 pS = P(&g_pose0[c->s]), dir = norm3(V(0.2f, -0.1f, 1));
    sa_report rep;
    sa_xf far = {add3(pS, mul3(dir, 200)), (sv_quat){0, 0, 0, 1}};
    solve_fresh(pose, SA_R, &cf, far, 1, &rep);
    sv_vec3 hw = P(&pose[c->w]);
    CHECK(rep.solved && rep.clamp == SA_CLAMP_FAR && !rep.reached && NEAR(len3(sub3(hw, pS)), 33.92 + 27.42 - 0.5, 0.02) &&
              dot3(norm3(sub3(hw, pS)), dir) > 0.9999f && NEAR(rep.residual, 200 - (61.34 - 0.5), 0.05),
          "far: clamp %d reached %d |S-W| %g residual %g", rep.clamp, rep.reached, len3(sub3(hw, pS)), rep.residual);
    /* the stretch (the body unseen): the same far target, the hand and its subtree on it, the elbow as before */
    {
        sa_node sp[RUN3_N];
        memcpy(sp, g_pose0, sizeof sp);
        sa_cfg cs = cf;
        cs.stretch = 1;
        sa_report rs;
        solve_fresh(sp, SA_R, &cs, far, 1, &rs);
        sv_vec3 hs = P(&sp[c->w]), d = sub3(hs, hw);
        int sub_ok = 1;
        for (int k = 0; k < c->nsub[2]; k++)
            sub_ok &= len3(sub3(sub3(P(&sp[c->sub[2][k]]), P(&pose[c->sub[2][k]])), d)) < 1e-3f;
        CHECK(rs.solved && rs.clamp == SA_CLAMP_FAR && rs.reached && rs.residual < 0.01f &&
                  NEAR(rs.stretch_cm, 200 - (61.34 - 0.5), 0.05) && len3(sub3(P(&sp[c->e]), P(&pose[c->e]))) < 1e-3f &&
                  sub_ok && rep.stretch_cm == 0,
              "stretch: residual %g stretched %g cm, the elbow kept %g, the subtree with it %d, none without %g",
              rs.residual, rs.stretch_cm, len3(sub3(P(&sp[c->e]), P(&pose[c->e]))), sub_ok, rep.stretch_cm);
    }
    memcpy(pose, g_pose0, sizeof pose);
    sa_xf nearT = {add3(pS, mul3(dir, 2)), (sv_quat){0, 0, 0, 1}};
    solve_fresh(pose, SA_R, &cf, nearT, 1, &rep);
    hw = P(&pose[c->w]);
    CHECK(rep.solved && rep.clamp == SA_CLAMP_NEAR && NEAR(len3(sub3(hw, pS)), 33.92 - 27.42 + 0.5, 0.02),
          "near: clamp %d |S-W| %g", rep.clamp, len3(sub3(hw, pS)));
    memcpy(pose, g_pose0, sizeof pose);
    sa_xf atS = {pS, (sv_quat){0, 0, 0, 1}};
    CHECK(solve_fresh(pose, SA_R, &cf, atS, 1, &rep) && finite_arm(pose, SA_R) &&
              NEAR(len3(sub3(P(&pose[c->w]), pS)), 6.5 + 0.5, 0.02),
          "the target on the shoulder: solved %d finite %d", rep.solved, finite_arm(pose, SA_R));

    /* the pole: the elbow on the hint's side of the shoulder -> hand line (the left arm mirrored in x) */
    int off = 0;
    for (int s = 0; s < 2; s++) {
        c = &g_ch[s];
        pS = P(&g_pose0[c->s]);
        sv_vec3 hint = norm3(V((s == SA_R ? 1 : -1) * cf.pole[0], cf.pole[1], cf.pole[2]));
        for (int k = 0; k < 200; k++) {
            sv_vec3 d = rdir();
            if (fabsf(dot3(d, hint)) > 0.95f) continue;
            memcpy(pose, g_pose0, sizeof pose);
            sa_xf t = {add3(pS, mul3(d, 25 + rnd() * 30)), rquat()};
            solve_fresh(pose, s, &cf, t, 1, &rep);
            sv_vec3 e = sub3(P(&pose[c->e]), pS), ep = sub3(e, mul3(d, dot3(e, d)));
            sv_vec3 hp = sub3(hint, mul3(d, dot3(hint, d)));
            off += !(dot3(norm3(ep), norm3(hp)) > 0.9999f);
        }
    }
    CHECK(!off, "pole: %d elbows off the hint's plane or side", off);
    /* the animated elbow's share: pole_anim = 1 bends toward the animated elbow */
    sa_cfg ca = cf;
    ca.pole_anim = 1;
    c = &g_ch[SA_R];
    pS = P(&g_pose0[c->s]);
    memcpy(pose, g_pose0, sizeof pose);
    sv_vec3 d = norm3(V(0.3f, -0.2f, 1));
    sa_xf t = {add3(pS, mul3(d, 45)), (sv_quat){0, 0, 0, 1}};
    solve_fresh(pose, SA_R, &ca, t, 1, &rep);
    sv_vec3 ua = sub3(P(&g_pose0[c->e]), pS), ue = sub3(P(&pose[c->e]), pS);
    sv_vec3 pa = norm3(sub3(ua, mul3(d, dot3(ua, d)))), pe = norm3(sub3(ue, mul3(d, dot3(ue, d))));
    CHECK(dot3(pa, pe) > 0.9999f, "pole_anim 1: the elbow is not on the animated elbow's side (cos %g)", dot3(pa, pe));
}

/* ---- no NaNs ------------------------------------------------------------------------------------------------------ */
static void test_nan(void) {
    sa_cfg cf;
    sa_cfg_default(&cf);
    sa_node pose[RUN3_N];
    sa_report rep;
    const sa_chain *c = &g_ch[SA_R];
    sv_vec3 pS = P(&g_pose0[c->s]);
    const sa_xf bad[] = {
        {V(NAN, 0, 0), (sv_quat){0, 0, 0, 1}}, {V(0, INFINITY, 0), (sv_quat){0, 0, 0, 1}},
        {V(0, 0, 0), (sv_quat){NAN, 0, 0, 1}}, {V(0, 0, 0), (sv_quat){0, 0, 0, 0}}, {V(1e9f, 0, 0), (sv_quat){0, 0, 0, 1}},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(pose, g_pose0, sizeof pose);
        int r = solve_fresh(pose, SA_R, &cf, bad[i], 1, &rep);
        CHECK(!r && !rep.solved && rep.why && !memcmp(pose, g_pose0, sizeof pose), "bad target %zu: solved %d (%s)", i, r,
              rep.why ? rep.why : "-");
    }
    memcpy(pose, g_pose0, sizeof pose);
    pose[c->e].pos[1] = NAN;
    sa_xf ok = {add3(pS, V(10, -10, 30)), (sv_quat){0, 0, 0, 1}};
    CHECK(!solve_fresh(pose, SA_R, &cf, ok, 1, &rep) && rep.why, "a NaN forearm accepted");
    memcpy(pose, g_pose0, sizeof pose);
    memcpy(pose[c->e].pos, pose[c->s].pos, sizeof pose[c->e].pos);
    CHECK(!solve_fresh(pose, SA_R, &cf, ok, 1, &rep) && strstr(rep.why, "bone"), "a zero-length upper arm accepted");
    memcpy(pose, g_pose0, sizeof pose);
    CHECK(!solve_fresh(pose, SA_R, &cf, ok, 0, &rep) && !memcmp(pose, g_pose0, sizeof pose), "weight 0 changed the pose");
    CHECK(!sa_solve(pose, 100, c, &cf, ok, 1, DT, &(sa_arm_state){0}, &rep), "a chain beyond n accepted");
    /* degenerate geometry: the target along the hint (no plane from it), straight down, straight up, with and without
     * a remembered elbow */
    sv_vec3 hint = norm3(V(cf.pole[0], cf.pole[1], cf.pole[2]));
    const sv_vec3 dirs[] = {hint, mul3(hint, -1), V(0, -1, 0), V(0, 1, 0), V(0, 0, 1)};
    int nan = 0;
    for (int m = 0; m < 2; m++)
        for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
            for (float L = 0; L < 70; L += 7) {
                sa_arm_state st = {0};
                if (m) st.have_m = 1, st.m = norm3(V(1, 0, 0));
                memcpy(pose, g_pose0, sizeof pose);
                sa_xf t = {add3(pS, mul3(dirs[i], L)), rquat()};
                nan += !sa_solve(pose, RUN3_N, c, &cf, t, 1, DT, &st, &rep) || !finite_arm(pose, SA_R);
            }
    CHECK(!nan, "degenerate targets: %d unsolved or not finite", nan);
}

/* ---- continuity ----------------------------------------------------------------------------------------------------- */
typedef sv_vec3 (*path_fn)(float u, sv_vec3 pS, int side);
static sv_vec3 path_circle(float u, sv_vec3 pS, int s) { /* a vertical circle in front: below, ahead, overhead, behind */
    float a = u * 2 * 3.14159265f;
    return add3(pS, V((s ? 1 : -1) * 8, 45 * cosf(a + 3.14159265f), 30 + 25 * sinf(a)));
}
static sv_vec3 path_across(float u, sv_vec3 pS, int s) { /* from out at the side across the chest to the other shoulder */
    float a = (-0.5f + 1.3f * u) * 3.14159265f, x = s ? 1 : -1;
    return add3(pS, V(x * 45 * cosf(a), -10, 45 * sinf(a) + 10));
}
static sv_vec3 path_hint(float u, sv_vec3 pS, int s) { /* straight through the hint's own direction (the hint gives no plane) */
    sa_cfg c;
    sa_cfg_default(&c);
    sv_vec3 h = norm3(V((s ? 1 : -1) * c.pole[0], c.pole[1], c.pole[2])), side = norm3(cross3(h, V(0, 0, 1)));
    return add3(pS, add3(mul3(h, 40), mul3(side, -30 + 60 * u)));
}
static sv_vec3 path_radial(float u, sv_vec3 pS, int s) { /* from the shoulder out beyond the reach and back */
    float r = 3 + 87 * (u < 0.5f ? 2 * u : 2 - 2 * u);
    return add3(pS, mul3(norm3(V((s ? 1 : -1) * 0.3f, -0.4f, 1)), r));
}
static void test_continuity(void) {
    sa_cfg cf;
    sa_cfg_default(&cf);
    const path_fn paths[] = {path_circle, path_across, path_hint, path_radial};
    const char *const names[] = {"circle", "across", "hint", "radial"};
    sa_node pose[RUN3_N], prev[RUN3_N];
    for (size_t pi = 0; pi < sizeof paths / sizeof paths[0]; pi++)
        for (int s = 0; s < 2; s++) {
            const sa_chain *c = &g_ch[s];
            sv_vec3 pS = P(&g_pose0[c->s]);
            sa_arm_state st = {0};
            float maxd = 0, maxr = 0, maxm = 0, maxh = 0;
            int steps = 0, bad = 0;
            sv_vec3 mprev = V(0, 0, 0);
            const int N = 600; /* ~2 m/s along a 1.3-2.8 m path at 90 Hz */
            for (int k = 0; k <= N; k++) {
                float u = (float)k / N;
                sv_vec3 tp = paths[pi](u, pS, s);
                sa_xf t = {tp, sv_qnormalize(sv_qmul(qax(V(0, 1, 0), u * 2.0f), qax(V(1, 0, 0), 0.5f * u)))};
                memcpy(pose, g_pose0, sizeof pose);
                sa_report rep;
                bad += !sa_solve(pose, RUN3_N, c, &cf, t, 1, DT, &st, &rep) || !finite_arm(pose, s);
                if (k) {
                    float step = len3(sub3(paths[pi](u, pS, s), paths[pi]((float)(k - 1) / N, pS, s)));
                    for (int j = c->lo; j <= c->hi; j++) {
                        float dd = len3(sub3(P(&pose[j]), P(&prev[j]))) - step, r = qang(Q(&pose[j]), Q(&prev[j]));
                        maxd = dd > maxd ? dd : maxd, maxr = r > maxr ? r : maxr;
                    }
                    float dh = len3(sub3(P(&pose[c->w]), P(&prev[c->w])));
                    maxh = dh > maxh ? dh : maxh;
                    float cm = dot3(mprev, st.m);
                    float am = acosf(cm > 1 ? 1 : cm < -1 ? -1 : cm);
                    maxm = am > maxm ? am : maxm;
                    steps++;
                }
                mprev = st.m;
                memcpy(prev, pose, sizeof prev);
            }
            /* a node moves at most ~ the target's step plus the elbow plane's limited turn (8 deg per frame at 720 deg/s,
             * at most ~61 cm from the shoulder: 8.5 cm); a rotation at most ~ that turn plus the path's own */
            CHECK(!bad && maxd < 9.0f && maxr < 20 * DEG && maxm <= cf.pole_dps * DEG * DT + 2 * DEG && maxh < 4.0f,
                  "continuity %s %s: %d bad of %d, max extra move %.2f cm, max turn %.1f deg, elbow plane %.1f deg/frame, "
                  "hand %.2f cm/frame", names[pi], s ? "R" : "L", bad, steps, maxd, maxr / DEG, maxm / DEG, maxh);
            printf("  continuity %-6s %s: worst per frame: a node %.2f cm beyond the target's step, a turn %.1f deg, the "
                   "elbow plane %.1f deg, the hand %.2f cm\n", names[pi], s ? "R" : "L", maxd, maxr / DEG, maxm / DEG, maxh);
        }
    /* over the weight: a fixed far-off target, w 0 -> 1 in 50 steps: small steps, rigid bones at every weight */
    const sa_chain *c = &g_ch[SA_R];
    sv_vec3 pS = P(&g_pose0[c->s]);
    sa_xf t = {add3(pS, V(10, 20, 45)), qax(V(0, 0, 1), 1.2f)};
    float maxd = 0;
    int rigid = 1;
    for (int k = 0; k <= 50; k++) {
        memcpy(pose, g_pose0, sizeof pose);
        sa_report rep;
        sa_arm_state st = {0};
        sa_solve(pose, RUN3_N, c, &cf, t, (float)k / 50, DT, &st, &rep);
        rigid &= NEAR(len3(sub3(P(&pose[c->e]), P(&pose[c->s]))), 33.92, 0.02) &&
                 NEAR(len3(sub3(P(&pose[c->w]), P(&pose[c->e]))), 27.42, 0.02);
        if (k)
            for (int j = c->lo; j <= c->hi; j++) {
                float dd = len3(sub3(P(&pose[j]), P(&prev[j])));
                maxd = dd > maxd ? dd : maxd;
            }
        if (k == 50)
            CHECK(len3(sub3(P(&pose[c->w]), t.p)) < 1e-3f, "weight 1: the hand is %g cm off", len3(sub3(P(&pose[c->w]), t.p)));
        memcpy(prev, pose, sizeof prev);
    }
    CHECK(rigid && maxd < 4.0f, "over the weight: rigid %d, max step %.2f cm", rigid, maxd);
}

/* ---- round trips ---------------------------------------------------------------------------------------------------- */
static void test_roundtrip(void) {
    int bad = 0;
    for (int k = 0; k < 200; k++) {
        sa_xf x = {mul3(rdir(), 1000 * rnd()), rquat()}, y = {mul3(rdir(), 100 * rnd()), rquat()};
        sa_xf i1 = sa_xf_mul(x, sa_xf_inv(x)), r = sa_xf_mul(sa_xf_inv(x), sa_xf_mul(x, y));
        bad += !(len3(i1.p) < 1e-3f && qang(i1.q, (sv_quat){0, 0, 0, 1}) < 1e-3f && len3(sub3(r.p, y.p)) < 1e-3f &&
                 qang(r.q, y.q) < 1e-3f);
        sc_rigxf rig = {{rnd() * 5000, rnd() * 500, rnd() * 5000}, {rnd() * 50, 150 + rnd() * 30, rnd() * 50}, 6 * rnd() - 3, 0};
        sa_xf g = {V(rnd() * 100 - 50, 100 + rnd() * 80, rnd() * 100 - 50), rquat()};
        sa_xf m = sa_grip_to_model(&rig, V(0, 0, 0), x, g), w = sa_xf_mul(x, m), back = sa_world_to_track(&rig, w);
        bad += !(len3(sub3(back.p, g.p)) < 5e-2f && qang(back.q, g.q) < 1e-3f);
        /* the rig's map as somvr_hands.h has it: world = base + R(Y(yaw)) (p - p0) */
        sv_vec3 dw = sv_qrot(sv_qyaw(rig.yaw), sub3(g.p, V(rig.p0[0], rig.p0[1], rig.p0[2])));
        bad += !(len3(sub3(w.p, add3(V(rig.base[0], rig.base[1], rig.base[2]), dw))) < 5e-2f);
        sa_xf sh = sa_grip_to_model(&rig, V(5, -3, 2), x, g);
        bad += !(len3(sub3(sa_xf_mul(x, sh).p, add3(w.p, V(5, -3, 2)))) < 5e-2f);
    }
    CHECK(!bad, "transform / rig round trips: %d off", bad);

    /* the grip -> the weapon node's target -> the IK -> the bone grip = the grip again, both arms */
    sa_cfg cf;
    sa_cfg_default(&cf);
    sa_node pose[RUN3_N];
    float cal_v[6] = {0, 0, 0, 90, 0, 0};
    sa_xf cal = sa_xf_euler(cal_v), X = {V(35400, 5402.5f, -22400), (sv_quat){0, 0.5f, 0, -0.866025f}};
    sc_rigxf rig = {{35390, 5480, -22395}, {10, 170, -20}, 0.7f, 0};
    int off = 0;
    for (int s = 0; s < 2; s++) {
        const sa_chain *c = &g_ch[s];
        for (int k = 0; k < 100; k++) {
            /* a grip 25-50 cm from the shoulder, in tracking space */
            sv_vec3 pSm = P(&g_pose0[c->s]);
            sa_xf want_model = {add3(pSm, mul3(rdir(), 25 + 15 * rnd())), rquat()};
            sa_xf world = sa_xf_mul(X, want_model), g = sa_world_to_track(&rig, world);
            sa_xf G = sa_grip_to_model(&rig, V(0, 0, 0), X, g), tw = sa_xf_mul(G, cal);
            memcpy(pose, g_pose0, sizeof pose);
            sa_report rep;
            solve_fresh(pose, s, &cf, sa_hand_target(pose, c, tw), 1, &rep);
            sa_xf bg = sa_bone_grip(&rig, X, sa_node_xf(&pose[c->wpn]), cal);
            off += !(len3(sub3(bg.p, g.p)) < 5e-2f && qang(bg.q, g.q) < 1e-3f);
        }
    }
    CHECK(!off, "grip -> weapon node -> bone grip: %d of 200 off", off);

    /* an FK pose solved back onto its own hand (the animated elbow plane, pole_anim 1): nothing moves */
    sa_cfg c1 = cfg_plain();
    c1.pole_anim = 1;
    int moved = 0;
    for (int s = 0; s < 2; s++) {
        const sa_chain *c = &g_ch[s];
        for (int k = 0; k < 60; k++) {
            memcpy(pose, g_pose0, sizeof pose);
            /* bend the elbow about its hinge by -10..+60 deg from run 3's 27 (a valid FK pose within the reach) */
            sa_arm_state st = {0};
            sa_report rep;
            sv_vec3 hz = sv_qrot(Q(&pose[c->s]), V(SA_HINGE_X, SA_HINGE_Y, SA_HINGE_Z));
            sa_xf E = sa_node_xf(&pose[c->e]);
            sv_quat db = qax(hz, (-10 + 70 * rnd()) * DEG);
            for (int j = c->e; j <= c->hi; j++) {
                int in = j == c->e;
                for (int i = 0; i < c->nsub[1]; i++) in |= c->sub[1][i] == j;
                if (!in) continue;
                sa_xf x = sa_node_xf(&pose[j]);
                sa_node_set(&pose[j], (sa_xf){add3(E.p, sv_qrot(db, sub3(x.p, E.p))), sv_qmul(db, x.q)});
            }
            sa_node fk[RUN3_N];
            memcpy(fk, pose, sizeof fk);
            sa_xf t = sa_node_xf(&pose[c->w]);
            sa_solve(pose, RUN3_N, c, &c1, t, 1, DT, &st, &rep);
            float dmax = 0, rmax = 0;
            for (int j = c->lo; j <= c->hi; j++) {
                float dd = len3(sub3(P(&pose[j]), P(&fk[j]))), r = qang(Q(&pose[j]), Q(&fk[j]));
                dmax = dd > dmax ? dd : dmax, rmax = r > rmax ? r : rmax;
            }
            moved += !(dmax < 0.02f && rmax < 2e-3f);
            if (k == 0 && !(dmax < 0.02f && rmax < 2e-3f))
                printf("  FK round trip %s: moved %g cm, %g rad\n", s ? "R" : "L", dmax, rmax);
        }
    }
    CHECK(!moved, "FK pose -> IK onto its own hand: %d of 120 moved", moved);

    /* idempotence: the pose solved again (as when the tracker hands back last frame's edited buffer) */
    int drift = 0;
    for (int k = 0; k < 100; k++) {
        int s = k & 1;
        const sa_chain *c = &g_ch[s];
        sa_xf t = {add3(P(&g_pose0[c->s]), mul3(rdir(), 20 + 40 * rnd())), rquat()};
        sa_cfg cp = cfg_plain();
        sa_arm_state st = {0};
        sa_report rep;
        memcpy(pose, g_pose0, sizeof pose);
        sa_solve(pose, RUN3_N, c, &cp, t, 1, DT, &st, &rep);
        sa_node once[RUN3_N];
        memcpy(once, pose, sizeof once);
        sa_solve(pose, RUN3_N, c, &cp, t, 1, DT, &st, &rep);
        float dmax = 0;
        for (int j = c->lo; j <= c->hi; j++) {
            float dd = len3(sub3(P(&pose[j]), P(&once[j]))) + 10 * qang(Q(&pose[j]), Q(&once[j]));
            dmax = dd > dmax ? dd : dmax;
        }
        drift += !(dmax < 0.01f);
    }
    CHECK(!drift, "solving the solved pose again moved it %d of 100 times", drift);
    /* with the animated elbow's share the repeats converge (each one closer) */
    {
        const sa_chain *c = &g_ch[SA_R];
        sa_xf t = {add3(P(&g_pose0[c->s]), V(5, -5, 40)), (sv_quat){0, 0, 0, 1}};
        sa_cfg cd;
        sa_cfg_default(&cd);
        sa_arm_state st = {0};
        sa_report rep;
        memcpy(pose, g_pose0, sizeof pose);
        float last = 1e9f, first = 0;
        int mono = 1;
        for (int k = 0; k < 30; k++) {
            sv_vec3 e0 = P(&pose[c->e]);
            sa_solve(pose, RUN3_N, c, &cd, t, 1, DT, &st, &rep);
            float dd = len3(sub3(P(&pose[c->e]), e0));
            if (k == 1) first = dd;
            if (k > 1) mono &= dd <= last + 1e-4f;
            last = dd;
        }
        CHECK(mono && last < 0.05f * (first + 1e-3f) + 1e-3f, "repeated solves with pole_anim: not converging (%g -> %g)",
              first, last);
    }
}

/* ---- the twist shares ---------------------------------------------------------------------------------------------- */
static float twist_about(sv_quat q, sv_vec3 a) {
    float p = q.x * a.x + q.y * a.y + q.z * a.z;
    return sv_wrap_pi(2 * atan2f(p, q.w));
}
static void test_twist(void) {
    sa_node pose[RUN3_N];
    sa_report rep;
    for (int s = 0; s < 2; s++) {
        const sa_chain *c = &g_ch[s];
        /* the hand rolled by 60 deg about the forearm, the arm otherwise where it is: the forearm twist node turns by
         * twist_fore x 60 about the forearm */
        sv_vec3 fa = norm3(sub3(P(&g_pose0[c->w]), P(&g_pose0[c->e])));
        sa_xf t = {P(&g_pose0[c->w]), sv_qmul(qax(fa, 60 * DEG), Q(&g_pose0[c->w]))};
        const float shares[] = {0, 0.5f, 1};
        for (int i = 0; i < 3; i++) {
            sa_cfg cf = cfg_plain();
            cf.pole_anim = 1, cf.twist_fore = shares[i];
            memcpy(pose, g_pose0, sizeof pose);
            solve_fresh(pose, s, &cf, t, 1, &rep);
            sv_quat d = sv_qmul(Q(&pose[c->tf]), sv_qconj(Q(&g_pose0[c->tf])));
            float ang = twist_about(d, fa);
            CHECK(NEAR(ang, shares[i] * 60 * DEG, 0.01) && NEAR(rep.twist_f_deg, 60, 0.2) &&
                      qang(Q(&pose[c->e]), Q(&g_pose0[c->e])) < 2e-3f,
                  "%s forearm twist share %g: the node turned %.2f deg (want %.2f), hand roll %.2f", s ? "R" : "L", shares[i],
                  ang / DEG, shares[i] * 60, rep.twist_f_deg);
        }
        /* the upper arm rolled by the hint's plane: the twist node keeps twist_upper of S's roll change */
        sa_cfg cf = cfg_plain();
        cf.pole[0] = -1, cf.pole[1] = 0, cf.pole[2] = 0.3f; /* the elbow inward and forward: S rolls a lot */
        memcpy(pose, g_pose0, sizeof pose);
        sa_xf t2 = {add3(P(&g_pose0[c->s]), V((s ? 1 : -1) * 5, -20, 35)), Q(&g_pose0[c->w])};
        solve_fresh(pose, s, &cf, t2, 1, &rep);
        sv_vec3 aS = norm3(sv_qrot(sv_qconj(Q(&g_pose0[c->s])), sub3(P(&g_pose0[c->e]), P(&g_pose0[c->s]))));
        sv_quat qp = sv_qconj(Q(&g_pose0[c->p]));
        float ds = sv_wrap_pi(twist_about(sv_qmul(qp, Q(&pose[c->s])), aS) - twist_about(sv_qmul(qp, Q(&g_pose0[c->s])), aS));
        float dt = sv_wrap_pi(twist_about(sv_qmul(qp, Q(&pose[c->tu])), aS) - twist_about(sv_qmul(qp, Q(&g_pose0[c->tu])), aS));
        CHECK(fabsf(ds) > 20 * DEG && NEAR(dt, 0.5f * ds, 0.02) && NEAR(rep.twist_u_deg, ds / DEG, 0.2) &&
                  len3(sub3(P(&pose[c->tu]), add3(P(&pose[c->s]), mul3(norm3(sub3(P(&pose[c->e]), P(&pose[c->s]))), 16.96f)))) < 0.02f,
              "%s upper-arm twist: S rolled %.1f deg, its twist node %.1f deg (want half), report %.1f", s ? "R" : "L", ds / DEG,
              dt / DEG, rep.twist_u_deg);
    }
}

/* ---- the weight and the held target ---------------------------------------------------------------------------------- */
static void test_weight(void) {
    sa_cfg cf;
    sa_cfg_default(&cf);
    sa_arm_state st = {0};
    st.have_m = 1;
    float w = 0, last = 0;
    int n = 0, mono = 1;
    while (w < 1 && n < 100) {
        w = sa_weight_step(&st, &cf, 1, DT);
        mono &= w >= last;
        last = w, n++;
    }
    CHECK(n == 14 && mono, "ease in: %d frames at 90 Hz (want 14 for 0.15 s), monotonic %d", n, mono);
    st.ramp = 0.5f;
    CHECK(NEAR(sa_weight_step(&st, &cf, 1, 0), 0.5, 1e-6), "the ease is not symmetric at the middle");
    st.ramp = 1;
    n = 0;
    while (w > 0 && n < 100) w = sa_weight_step(&st, &cf, 0, DT), n++;
    CHECK(n == 14 && st.have_m == 0, "ease out: %d frames, the elbow memory %d (dropped at 0)", n, st.have_m);
    CHECK(NEAR(sa_weight_step(&st, &cf, 1, 1.0f), 0.1f / 0.15f * 0.1f / 0.15f * (3 - 2 * 0.1f / 0.15f), 1e-5),
          "a 1 s hitch counts more than 0.1 s");
    st.ramp = 0;
    CHECK(sa_weight_step(&st, &cf, 1, NAN) == 0 && sa_weight_step(&st, &cf, 1, -1) == 0, "a NaN or negative dt moved it");
    sa_cfg c0 = cf;
    c0.blend_s = 0;
    CHECK(sa_weight_step(&st, &c0, 1, DT) == 1 && sa_weight_step(&st, &c0, 0, DT) == 0, "blend 0 is not instant");

    /* the hand lost: the last target held while the weight goes down, the hand back to the animation's, continuously */
    const sa_chain *c = &g_ch[SA_R];
    sa_node pose[RUN3_N], prev[RUN3_N];
    sa_arm_state as = {0};
    sv_vec3 pS = P(&g_pose0[c->s]);
    float maxd = 0;
    int k;
    for (k = 0; k < 60; k++) {
        int valid = k < 30;
        if (valid) as.target = (sa_xf){add3(pS, V(10, -5, 45 + 0.1f * k)), (sv_quat){0, 0, 0, 1}}, as.have_target = 1;
        float ww = sa_weight_step(&as, &cf, valid, DT);
        memcpy(pose, g_pose0, sizeof pose);
        sa_report rep;
        if (ww > 0) sa_solve(pose, RUN3_N, c, &cf, as.target, ww, DT, &as, &rep);
        if (k)
            for (int j = c->lo; j <= c->hi; j++) {
                float dd = len3(sub3(P(&pose[j]), P(&prev[j])));
                maxd = dd > maxd ? dd : maxd;
            }
        if (k == 29) CHECK(len3(sub3(P(&pose[c->w]), as.target.p)) < 1e-3f, "held: the hand is not on the target");
        memcpy(prev, pose, sizeof prev);
    }
    CHECK(!memcmp(pose, g_pose0, sizeof pose) && maxd < 16.0f, "the hand lost: back to the animation %d, max step %.2f cm",
          !memcmp(pose, g_pose0, sizeof pose), maxd);
}

/* ---- the bone blade ------------------------------------------------------------------------------------------------- */
static void test_blade(void) {
    sa_cfg cf;
    sa_cfg_default(&cf);
    sc_blade_cfg bc;
    sc_blade_cfg_default(&bc);
    float cal_v[6] = {0, 0, 0, 90, 0, 0};
    sa_xf cal = sa_xf_euler(cal_v), X = {V(1000, 50, -300), sv_qyaw(0.6f)};
    sc_rigxf rig = {{1000, 125, -300}, {0, 160, 0}, 0.3f, 0};
    const sa_chain *c = &g_ch[SA_R];
    sa_node pose[RUN3_N];
    /* reached: the bone blade is the controller blade */
    sa_xf world = sa_xf_mul(X, (sa_xf){add3(P(&g_pose0[c->s]), V(5, -10, 40)), qax(V(1, 0, 0), -0.4f)});
    sa_xf g = sa_world_to_track(&rig, world);
    sa_xf tw = sa_xf_mul(sa_grip_to_model(&rig, V(0, 0, 0), X, g), cal);
    memcpy(pose, g_pose0, sizeof pose);
    sa_report rep;
    solve_fresh(pose, SA_R, &cf, sa_hand_target(pose, c, tw), 1, &rep);
    sa_xf bg = sa_bone_grip(&rig, X, sa_node_xf(&pose[c->wpn]), cal);
    sc_grip gc = {1, {g.p.x, g.p.y, g.p.z}, {g.q.x, g.q.y, g.q.z, g.q.w}, 0, {0}, {0}, 1.0};
    sc_grip gb = gc;
    gb.pos[0] = bg.p.x, gb.pos[1] = bg.p.y, gb.pos[2] = bg.p.z;
    sv_quat_to(bg.q, gb.quat);
    sc_blade_hist h1 = {0}, h2 = {0};
    somvr_blade b1, b2;
    somvr_blade_controller(&bc, &gc, &rig, 1.0, &h1, &b1);
    somvr_blade_controller(&bc, &gb, &rig, 1.0, &h2, &b2);
    float db = sqrtf((b1.base[0] - b2.base[0]) * (b1.base[0] - b2.base[0]) + (b1.base[1] - b2.base[1]) * (b1.base[1] - b2.base[1]) +
                     (b1.base[2] - b2.base[2]) * (b1.base[2] - b2.base[2])),
          dtp = sqrtf((b1.tip[0] - b2.tip[0]) * (b1.tip[0] - b2.tip[0]) + (b1.tip[1] - b2.tip[1]) * (b1.tip[1] - b2.tip[1]) +
                      (b1.tip[2] - b2.tip[2]) * (b1.tip[2] - b2.tip[2]));
    CHECK(rep.reached && b1.valid && b2.valid && db < 0.05f && dtp < 0.1f, "reached: bone blade vs controller blade base %g "
          "tip %g cm", db, dtp);
    /* out of reach: the bone blade is the drawn sword, its grip within the arm's reach of the shoulder */
    world = sa_xf_mul(X, (sa_xf){add3(P(&g_pose0[c->s]), V(0, 0, 120)), (sv_quat){0, 0, 0, 1}});
    g = sa_world_to_track(&rig, world);
    tw = sa_xf_mul(sa_grip_to_model(&rig, V(0, 0, 0), X, g), cal);
    memcpy(pose, g_pose0, sizeof pose);
    solve_fresh(pose, SA_R, &cf, sa_hand_target(pose, c, tw), 1, &rep);
    bg = sa_bone_grip(&rig, X, sa_node_xf(&pose[c->wpn]), cal);
    sa_xf bgw = sa_xf_mul(sa_xf_inv(X), sa_xf_mul(X, sa_xf_mul(sa_node_xf(&pose[c->wpn]), sa_xf_inv(cal))));
    CHECK(!rep.reached && rep.clamp == SA_CLAMP_FAR && len3(sub3(bg.p, g.p)) > 50 &&
              len3(sub3(bgw.p, P(&pose[c->s]))) < 61.34f + 13,
          "clamped: the bone grip %g cm from the controller, %g cm from the shoulder", len3(sub3(bg.p, g.p)),
          len3(sub3(bgw.p, P(&pose[c->s]))));
}

/* ---- build 10: the weapon node held in the hand (SOMVR_ARMS_LW=held) ------------------------------------------------ */
static void test_lw(void) {
    sa_cfg cf = cfg_plain();
    cf.stretch = 1; /* build 9's switches: the hand always on its target */
    for (int side = 0; side < 2; side++) {
        const sa_chain *c = &g_ch[side];
        sa_node pose[RUN3_N], anim[RUN3_N];
        memcpy(pose, g_pose0, sizeof pose);
        sa_xf lw0 = sa_weapon_local(pose, c);
        sa_xf back = sa_xf_mul(sa_node_xf(&pose[c->w]), lw0);
        float dg = 0, d = sa_xf_dist(back, sa_node_xf(&pose[c->wpn]), &dg);
        CHECK(d < 1e-3f && dg < 0.05f && NEAR(len3(lw0.p), side ? 12.1 : len3(lw0.p), 0.2),
              "%s: lw = W^-1 WPN (back %g cm %g deg; |lw| %g cm)", side ? "R" : "L", d, dg, len3(lw0.p));
        sa_xf tw = {add3(P(&pose[c->s]), V(side ? 10.0f : -10.0f, -15, 35)), qax(V(1, 0, 0), -0.5f)};
        sa_xf h1 = sa_hand_target(pose, c, tw), h2 = sa_hand_target_lw(tw, lw0);
        CHECK(sa_xf_dist(h1, h2, &dg) < 1e-4f && dg < 0.01f, "%s: sa_hand_target = sa_hand_target_lw", side ? "R" : "L");
        /* an animation moves the weapon node in the hand: 9 cm and 40 deg (a draw's flourish) */
        memcpy(anim, g_pose0, sizeof anim);
        sa_xf moved = {add3(lw0.p, V(6, -5, 4.5f)), sv_qnormalize(sv_qmul(qax(norm3(V(0.3f, 1, 0.2f)), 40 * DEG), lw0.q))};
        sa_weapon_set(anim, c, moved);
        sa_xf lwa = sa_weapon_local(anim, c);
        CHECK(sa_xf_dist(lwa, moved, &dg) < 1e-3f && dg < 0.05f, "%s: sa_weapon_set / sa_weapon_local round trip",
              side ? "R" : "L");
        /* as animated (build 9): the hand off the rigid target, the weapon node on the grip */
        sa_report rep;
        sa_node a1[RUN3_N];
        memcpy(a1, anim, sizeof a1);
        solve_fresh(a1, side, &cf, sa_hand_target(a1, c, tw), 1, &rep);
        float off_anim = len3(sub3(P(&a1[c->w]), sa_hand_target_lw(tw, lw0).p));
        float wpn_anim = len3(sub3(P(&a1[c->wpn]), tw.p));
        CHECK(off_anim > 3 && wpn_anim < 0.05f, "%s: animated lw: the hand %g cm off the rigid target (weapon node %g cm)",
              side ? "R" : "L", off_anim, wpn_anim);
        /* held: the hand on the rigid target, the weapon node put back on the grip */
        sa_node a2[RUN3_N];
        memcpy(a2, anim, sizeof a2);
        solve_fresh(a2, side, &cf, sa_hand_target_lw(tw, lw0), 1, &rep);
        int set = sa_weapon_set(a2, c, lw0);
        float off_held = len3(sub3(P(&a2[c->w]), sa_hand_target_lw(tw, lw0).p));
        float dw = sa_xf_dist(sa_node_xf(&a2[c->wpn]), tw, &dg);
        CHECK(set && off_held < 0.05f && dw < 0.05f && dg < 0.1f,
              "%s: held lw: the hand %g cm off the rigid target, the weapon node %g cm / %g deg off the grip",
              side ? "R" : "L", off_held, dw, dg);
        CHECK(finite_arm(a2, side), "%s: held: finite", side ? "R" : "L");
    }
    /* the capture: only while allowed, after n steady frames, once */
    sa_lw_hold h;
    sa_lw_reset(&h);
    sa_xf a = {V(5, -8, 6), qax(V(0, 0, 1), 0.3f)}, b = {V(9, -3, 8), qax(V(0, 1, 0), 0.9f)};
    int caps = 0;
    for (int i = 0; i < 40; i++) caps += sa_lw_step(&h, a, 0, 10);
    CHECK(!h.have && caps == 0 && h.steady == 39, "not allowed: no capture (steady %d)", h.steady);
    for (int i = 0; i < 40; i++) /* moving: never steady */
        caps += sa_lw_step(&h, (sa_xf){V(5.2f + 0.2f * (float)i, -8, 6), a.q}, 1, 10);
    CHECK(!h.have && caps == 0, "moving: no capture");
    for (int i = 0; i < 11; i++) caps += sa_lw_step(&h, a, 1, 10);
    float dg = 0;
    CHECK(h.have && caps == 1 && sa_xf_dist(h.ref, a, &dg) < 1e-5f, "steady 10 frames: captured");
    for (int i = 0; i < 40; i++) caps += sa_lw_step(&h, b, 1, 10);
    CHECK(caps == 1 && sa_xf_dist(h.ref, a, &dg) < 1e-5f, "kept: no second capture");
    sa_xf nanx = {V(NAN, 0, 0), a.q};
    CHECK(!sa_lw_step(&h, nanx, 1, 10) && h.steady == 0 && h.have, "NaN: ignored, the capture kept");
    sa_lw_reset(&h);
    CHECK(!h.have, "reset");
    CHECK(sa_weapon_set(NULL, &g_ch[0], a) == 0, "no pose: nothing set");
    sa_chain nw = g_ch[SA_R];
    nw.wpn = -1;
    sa_xf id = sa_weapon_local(g_pose0, &nw);
    CHECK(len3(id.p) == 0 && id.q.w == 1, "no weapon node: identity");
}

/* ---- the default calibration ----------------------------------------------------------------------------------------- */
static void test_cal(void) {
    float v[6] = {0, 0, 0, 90, 0, 0};
    sa_xf c = sa_xf_euler(v);
    sv_vec3 y = sv_qrot(c.q, V(0, 1, 0));
    CHECK(NEAR(y.x, 0, 1e-5) && NEAR(y.y, 0, 1e-5) && NEAR(y.z, 1, 1e-5), "90 deg about x: y -> %g %g %g", y.x, y.y, y.z);
    float v2[6] = {1, 2, 3, 0, 90, 0}, v3[6] = {0, 0, 0, 30, 40, 50};
    sa_xf c2 = sa_xf_euler(v2);
    sv_vec3 x = sv_qrot(c2.q, V(1, 0, 0));
    CHECK(NEAR(x.z, -1, 1e-5) && c2.p.x == 1 && c2.p.z == 3, "90 deg about y: x -> %g %g %g", x.x, x.y, x.z);
    sa_xf c3 = sa_xf_euler(v3);
    sv_quat q3 = sv_qmul(sv_qmul(qax(V(1, 0, 0), 30 * DEG), qax(V(0, 1, 0), 40 * DEG)), qax(V(0, 0, 1), 50 * DEG));
    CHECK(qang(c3.q, q3) < 1e-5f, "X then Y then Z");
    /* with run 3's weapon nodes the default lays both forearms along the grip's +Y (the wrist side of an OpenXR grip)
     * and the hand's thumb side (-Y) along the blade (+Z) */
    for (int s = 0; s < 2; s++) {
        const sa_chain *ch = &g_ch[s];
        sa_xf lw = sa_xf_mul(sa_xf_inv(sa_node_xf(&g_pose0[ch->w])), sa_node_xf(&g_pose0[ch->wpn]));
        sa_xf hand = sa_xf_mul(c, sa_xf_inv(lw)); /* the hand in the grip's frame */
        float m = s == SA_R ? 1.0f : -1.0f; /* the left skeleton is mirrored: its chain runs along -X, its thumb side +Y */
        sv_vec3 fore = sv_qrot(hand.q, V(-m, 0, 0)), thumb = sv_qrot(hand.q, V(0, -m, 0));
        CHECK(fore.y > 0.95f && thumb.z > 0.95f, "%s: the forearm %.2f %.2f %.2f, the thumb side %.2f %.2f %.2f in the grip",
              s ? "R" : "L", fore.x, fore.y, fore.z, thumb.x, thumb.y, thumb.z);
    }
}

int main(void) {
    test_chain();
    test_reach();
    test_nan();
    test_continuity();
    test_roundtrip();
    test_twist();
    test_weight();
    test_blade();
    test_cal();
    test_lw();
    printf("test_arms: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
