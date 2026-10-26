/* test_headbone.c - unit tests of somvr_headbone.c on run 3's skeleton and pose (tests/run3_skeleton.h): the Head
 * subtree from the parent bytes (against a walk up every node's parent chain, the face, eye, jaw and hair nodes in, the
 * neck, spine, shoulders and arms out, one index run 29..85, disjoint from both arm chains' blocks), broken skeletons
 * (no Head, two, a parent after its child, Head the root or a leaf, too many runs), synthetic skeletons with several
 * runs, and the collapse (Head and every node outside its subtree bit for bit, the subtree at x0.001 of its distance
 * from the Head joint in the same direction with its rotations bit for bit, s = 0 / 1, a moved and turned object, bad
 * scales, a non-finite Head, a NaN child, a tree that does not fit). */
#include "../somvr_headbone.h"

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

static sa_node g_anim[RUN3_N];

static void load_pose(sa_node *p) {
    for (int i = 0; i < RUN3_N; i++) {
        const float *r = RUN3_POSE[i];
        sa_node_set(&p[i], (sa_xf){{r[0], r[1], r[2]}, {r[3], r[4], r[5], r[6]}});
    }
}

/* below Head by walking up the parent chain (independent of the forward pass) */
static int below(const uint8_t *parent, int i, int head) {
    for (int j = parent[i], k = 0; j != 0xff && k < RUN3_N; j = parent[j], k++)
        if (j == head) return 1;
    return 0;
}

static int find(const char *name) {
    for (int i = 0; i < RUN3_N; i++)
        if (!strcmp(RUN3_NAME[i], name)) return i;
    return -1;
}

static double dist(const float *a, const float *b) {
    double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

static void test_tree(void) {
    hb_tree t;
    char why[400];
    int rc = hb_tree_build(&t, RUN3_PARENT, RUN3_HASH, RUN3_N, HB_H_HEAD, why, sizeof why);
    CHECK(rc == 0 && t.head == 28 && t.parent == 24 && !strcmp(RUN3_NAME[t.head], "Head") &&
              !strcmp(RUN3_NAME[t.parent], "Neck"),
          "run 3: rc %d head %d parent %d (%s)", rc, t.head, t.parent, why);
    CHECK(t.nsub == 57 && t.nrun == 1 && t.run[0][0] == 29 && t.run[0][1] == 85, "run 3: %d below in %d runs %d..%d", t.nsub,
          t.nrun, t.run[0][0], t.run[0][1]);
    CHECK(!strcmp(why, "Head 28 (in 24), 57 nodes below in 1 run: 29..85"), "summary '%s'", why);
    /* the same set as walking up each node's parents, in index order */
    int k = 0, same = 1;
    for (int i = 0; i < RUN3_N; i++) {
        int in = below(RUN3_PARENT, i, 28);
        if (in) same &= k < t.nsub && t.sub[k++] == i;
    }
    CHECK(same && k == t.nsub, "the subtree is not every node whose parent chain reaches Head (%d of %d)", k, t.nsub);
    static const char *const in_names[] = {"L_Eye_Def",   "R_Eye_Def",  "Jaw_Def",   "Tongue_Def", "L_Brow_Def",
                                           "PivJaw_NB",   "L_hair_F_01", "L_hair_04", "R_hair_B_04", "CurlLowLip_Def",
                                           "L_UpLid_Def", "R_hair_F_04"};
    static const char *const out_names[] = {"Head",    "Neck",   "Spine4_joint", "Upper_Torso", "L_Shoulder", "R_Shoulder",
                                            "L_Armu",  "R_Hand", "Pelvis",       "Null",        "FaceCam",    "Camera",
                                            "Aim_Camera", "scabbard_back", "Horn_Spring_1"};
    for (size_t i = 0; i < sizeof in_names / sizeof in_names[0]; i++) {
        int j = find(in_names[i]);
        int hit = 0;
        for (int m = 0; m < t.nsub; m++) hit |= t.sub[m] == j;
        CHECK(j > 0 && hit, "%s (%d) is not below Head", in_names[i], j);
    }
    for (size_t i = 0; i < sizeof out_names / sizeof out_names[0]; i++) {
        int j = find(out_names[i]);
        int hit = 0;
        for (int m = 0; m < t.nsub; m++) hit |= t.sub[m] == j;
        CHECK(j >= 0 && !hit, "%s (%d) is below Head", out_names[i], j);
    }
    /* every hair node of run 3 (all named *hair*) is in it */
    int hairs = 0, hairs_in = 0;
    for (int i = 0; i < RUN3_N; i++)
        if (strstr(RUN3_NAME[i], "hair")) {
            hairs++;
            hairs_in += below(RUN3_PARENT, i, 28);
        }
    CHECK(hairs == 24 && hairs_in == 24, "hair nodes %d, below Head %d", hairs, hairs_in);
    /* disjoint from both arm chains' write-back blocks (the arms and the head are written in the same phase 2) */
    for (int s = 0; s < 2; s++) {
        sa_chain c;
        char w[200];
        CHECK(sa_chain_build(&c, s, RUN3_PARENT, RUN3_HASH, RUN3_N, w, sizeof w) == 0, "arm %d: %s", s, w);
        CHECK(c.hi < t.run[0][0] || c.lo > t.run[0][1], "arm %d block %d..%d overlaps the head's %d..%d", s, c.lo, c.hi,
              t.run[0][0], t.run[0][1]);
    }

    /* broken skeletons */
    uint8_t par[RUN3_N];
    uint32_t hs[RUN3_N];
    memcpy(hs, RUN3_HASH, sizeof hs);
    hs[28] = 0x1234;
    CHECK(hb_tree_build(&t, RUN3_PARENT, hs, RUN3_N, HB_H_HEAD, why, sizeof why) == -1 && strstr(why, "no Head") &&
              t.head == -1 && t.nsub == 0,
          "no Head: '%s'", why);
    memcpy(hs, RUN3_HASH, sizeof hs);
    hs[100] = HB_H_HEAD;
    CHECK(hb_tree_build(&t, RUN3_PARENT, hs, RUN3_N, HB_H_HEAD, why, sizeof why) == -1 && strstr(why, "two nodes"),
          "two Heads: '%s'", why);
    memcpy(hs, RUN3_HASH, sizeof hs);
    hs[0] = HB_H_HEAD, hs[28] = 1;
    CHECK(hb_tree_build(&t, RUN3_PARENT, hs, RUN3_N, HB_H_HEAD, why, sizeof why) == -1 && strstr(why, "is the root"),
          "Head the root: '%s'", why);
    memcpy(par, RUN3_PARENT, sizeof par);
    par[30] = 31;
    CHECK(hb_tree_build(&t, par, RUN3_HASH, RUN3_N, HB_H_HEAD, why, sizeof why) == -1 && strstr(why, "does not precede"),
          "a parent after its child: '%s'", why);
    memcpy(par, RUN3_PARENT, sizeof par);
    par[0] = 0;
    CHECK(hb_tree_build(&t, par, RUN3_HASH, RUN3_N, HB_H_HEAD, why, sizeof why) == -1, "node 0 with a parent");
    CHECK(hb_tree_build(&t, RUN3_PARENT, RUN3_HASH, 1, HB_H_HEAD, why, sizeof why) == -1 &&
              hb_tree_build(&t, RUN3_PARENT, RUN3_HASH, SA_MAXN + 1, HB_H_HEAD, why, sizeof why) == -1 &&
              hb_tree_build(&t, NULL, RUN3_HASH, RUN3_N, HB_H_HEAD, why, sizeof why) == -1 &&
              hb_tree_build(&t, RUN3_PARENT, NULL, RUN3_N, HB_H_HEAD, why, sizeof why) == -1,
          "bad sizes or pointers accepted");
    /* the first 29 nodes only: Head is a leaf */
    CHECK(hb_tree_build(&t, RUN3_PARENT, RUN3_HASH, 29, HB_H_HEAD, why, sizeof why) == -1 && strstr(why, "no nodes below"),
          "Head a leaf: '%s'", why);
    /* synthetic: Head = 1; its children 2,3, another branch 4, then 5 (child of 3) and 6 (child of 4), 7 (child of 5) */
    {
        uint8_t p[8] = {0xff, 0, 1, 1, 0, 3, 4, 5};
        uint32_t h[8] = {10, HB_H_HEAD, 12, 13, 14, 15, 16, 17};
        CHECK(hb_tree_build(&t, p, h, 8, HB_H_HEAD, why, sizeof why) == 0 && t.head == 1 && t.parent == 0 && t.nsub == 4 &&
                  t.sub[0] == 2 && t.sub[1] == 3 && t.sub[2] == 5 && t.sub[3] == 7 && t.nrun == 3 && t.run[0][0] == 2 &&
                  t.run[0][1] == 3 && t.run[1][0] == 5 && t.run[1][1] == 5 && t.run[2][0] == 7 && t.run[2][1] == 7,
              "synthetic: nsub %d nrun %d (%s)", t.nsub, t.nrun, why);
    }
    /* interleaved: Head = 1, then children of Head and of the root alternate: 99 runs > HB_MAXRUN */
    {
        uint8_t p[200];
        uint32_t h[200];
        for (int i = 0; i < 200; i++) p[i] = i == 0 ? 0xff : i == 1 ? 0 : (i & 1) ? 0 : 1, h[i] = 1000u + (uint32_t)i;
        h[1] = HB_H_HEAD;
        CHECK(hb_tree_build(&t, p, h, 200, HB_H_HEAD, why, sizeof why) == -1 && strstr(why, "index runs"),
              "too many runs: '%s'", why);
        for (int i = 2 + 2 * HB_MAXRUN; i < 200; i++) p[i] = 0; /* exactly HB_MAXRUN runs: fine */
        CHECK(hb_tree_build(&t, p, h, 200, HB_H_HEAD, why, sizeof why) == 0 && t.nrun == HB_MAXRUN && t.nsub == HB_MAXRUN,
              "HB_MAXRUN runs: %d %d (%s)", t.nrun, t.nsub, why);
    }
    printf("tree   run 3: Head 28 (in 24 Neck), 57 nodes below in one run 29..85 = the parent-chain walk; eyes, jaw, "
           "tongue, lids, brows, 24 hair nodes in; neck, spine, shoulders, arms, cameras out; disjoint from both arms; "
           "broken skeletons refused; synthetic runs\n");
}

static void test_collapse(void) {
    hb_tree t;
    char why[400];
    hb_tree_build(&t, RUN3_PARENT, RUN3_HASH, RUN3_N, HB_H_HEAD, why, sizeof why);
    sa_node p[RUN3_N];
    memcpy(p, g_anim, sizeof p);
    CHECK(hb_collapse(p, RUN3_N, &t, HB_SCALE) == 1, "collapse refused");
    int outside_same = 1, rot_same = 1, dir_ok = 1, pad0 = 1;
    double rmax = 0, dmax0 = 0, ratio_err = 0;
    uint8_t in[RUN3_N] = {0};
    for (int k = 0; k < t.nsub; k++) in[t.sub[k]] = 1;
    const float *c = g_anim[28].pos;
    for (int i = 0; i < RUN3_N; i++) {
        if (!in[i]) {
            outside_same &= !memcmp(&p[i], &g_anim[i], sizeof p[i]);
            continue;
        }
        rot_same &= !memcmp(p[i].q, g_anim[i].q, sizeof p[i].q);
        pad0 &= p[i].pad == 0;
        double d0 = dist(g_anim[i].pos, c), d1 = dist(p[i].pos, c);
        dmax0 = fmax(dmax0, d0), rmax = fmax(rmax, d1);
        if (d0 > 1e-3) {
            ratio_err = fmax(ratio_err, fabs(d1 / d0 - HB_SCALE));
            double dot = 0;
            for (int a = 0; a < 3; a++) dot += (g_anim[i].pos[a] - c[a]) * (p[i].pos[a] - c[a]);
            dir_ok &= dot / (d0 * d1) > 0.99;
        }
    }
    CHECK(!memcmp(&p[28], &g_anim[28], sizeof p[28]), "Head itself was written");
    CHECK(outside_same, "a node outside Head's subtree changed");
    CHECK(rot_same && pad0, "the subtree's rotations changed (%d) or pad not 0 (%d)", rot_same, pad0);
    CHECK(dmax0 > 15 && dmax0 < 25 && rmax <= HB_SCALE * dmax0 * 1.001 && ratio_err < 2e-3 && dir_ok,
          "the subtree within %.4f cm of the Head joint (it reached %.2f cm), ratio error %.2g, direction %d", rmax, dmax0,
          ratio_err, dir_ok);
    /* the eyes and the hair ends end up at the joint: well inside any near plane around the eye base */
    int le = find("L_Eye_Def"), rh = find("R_hair_B_04");
    CHECK(dist(p[le].pos, c) < 0.02 && dist(p[rh].pos, c) < 0.02 && dist(g_anim[le].pos, c) > 10,
          "L eye %.4f cm, a hair end %.4f cm from the joint (eye before: %.2f)", dist(p[le].pos, c), dist(p[rh].pos, c),
          dist(g_anim[le].pos, c));
    /* the world: an object turned 70 deg and moved; the model-space collapse is the same in the world (X rigid) */
    {
        sa_xf X = {{1200, -40, 3400}, sv_qnormalize((sv_quat){0, sinf(0.6109f), 0, cosf(0.6109f)})};
        sa_xf hw = sa_xf_mul(X, sa_node_xf(&p[28])), ew = sa_xf_mul(X, sa_node_xf(&p[le]));
        sa_xf hw0 = sa_xf_mul(X, sa_node_xf(&g_anim[28]));
        float a[3] = {hw.p.x, hw.p.y, hw.p.z}, b[3] = {ew.p.x, ew.p.y, ew.p.z}, a0[3] = {hw0.p.x, hw0.p.y, hw0.p.z};
        CHECK(dist(a, a0) == 0 && dist(a, b) < 0.02, "world: Head moved %.4f, eye %.4f from it", dist(a, a0), dist(a, b));
    }
    /* s = 0: all at the joint exactly; s = 1: as animated (rounding) */
    memcpy(p, g_anim, sizeof p);
    CHECK(hb_collapse(p, RUN3_N, &t, 0) == 1, "s 0 refused");
    int at = 1;
    for (int k = 0; k < t.nsub; k++) at &= !memcmp(p[t.sub[k]].pos, c, sizeof p[0].pos);
    CHECK(at && !memcmp(&p[28], &g_anim[28], sizeof p[28]), "s 0: not all at the joint");
    memcpy(p, g_anim, sizeof p);
    CHECK(hb_collapse(p, RUN3_N, &t, 1) == 1, "s 1 refused");
    double e1 = 0;
    for (int i = 0; i < RUN3_N; i++) e1 = fmax(e1, dist(p[i].pos, g_anim[i].pos));
    CHECK(e1 < 1e-4, "s 1 moved a node by %.2g cm", e1);
    /* refused: bad scales, a non-finite Head, a tree that does not fit, no pose/tree; nothing written */
    static const float bad_s[] = {-0.001f, 1.5f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof bad_s / sizeof bad_s[0]; i++) {
        memcpy(p, g_anim, sizeof p);
        CHECK(hb_collapse(p, RUN3_N, &t, bad_s[i]) == 0 && !memcmp(p, g_anim, sizeof p), "scale %g accepted", bad_s[i]);
    }
    memcpy(p, g_anim, sizeof p);
    p[28].pos[1] = NAN;
    sa_node keep[RUN3_N];
    memcpy(keep, p, sizeof keep);
    CHECK(hb_collapse(p, RUN3_N, &t, HB_SCALE) == 0 && !memcmp(p, keep, sizeof p), "a NaN Head collapsed the subtree");
    memcpy(p, g_anim, sizeof p);
    CHECK(hb_collapse(p, 80, &t, HB_SCALE) == 0 && !memcmp(p, g_anim, sizeof p), "a tree past the pose's count accepted");
    CHECK(hb_collapse(NULL, RUN3_N, &t, HB_SCALE) == 0 && hb_collapse(p, RUN3_N, NULL, HB_SCALE) == 0, "NULL accepted");
    hb_tree bad = t;
    bad.head = -1;
    CHECK(hb_collapse(p, RUN3_N, &bad, HB_SCALE) == 0 && !memcmp(p, g_anim, sizeof p), "a tree without Head accepted");
    /* a NaN child joins the joint (the rest as usual) */
    memcpy(p, g_anim, sizeof p);
    p[40].pos[0] = NAN;
    CHECK(hb_collapse(p, RUN3_N, &t, HB_SCALE) == 1 && p[40].pos[0] == c[0] && dist(p[le].pos, c) < 0.02,
          "a NaN child: %g", p[40].pos[0]);
    /* every frame starts from the game's fresh pose: the same input gives the same output (no state, no drift) */
    sa_node p2[RUN3_N];
    memcpy(p, g_anim, sizeof p), memcpy(p2, g_anim, sizeof p2);
    hb_collapse(p, RUN3_N, &t, HB_SCALE), hb_collapse(p2, RUN3_N, &t, HB_SCALE);
    CHECK(!memcmp(p, p2, sizeof p), "the collapse is not a pure function of the pose");
    printf("collapse run 3: Head and the other %d nodes bit for bit; the 57 below at x%.3f (max %.4f of %.1f cm) in the "
           "same directions, rotations kept; s 0/1, the world, bad scales, NaN Head/child, a short pose: as specified\n",
           RUN3_N - 57, (double)HB_SCALE, rmax, dmax0);
}

int main(void) {
    load_pose(g_anim);
    test_tree();
    test_collapse();
    printf("test_headbone: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
