/* test_beasts.c - unit tests of somvr_beasts.c: the kinds by the game's names, the bone chain's capsules (the snout,
 * the links, a missing part, the radii per kind), the facing's capsule without a chain, the blade's reach on a caragor
 * that the orc's upright capsule misses (docs/re-xr-pipeline.md 92) */
#include "../somvr_beasts.h"

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

/* the distance from p to the segment a-b */
static float seg_d(const float p[3], const float a[3], const float b[3])
{
    float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
    float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2], u = l2 > 0 ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    float d[3] = { ap[0] - ab[0] * u, ap[1] - ab[1] * u, ap[2] - ab[2] * u };
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

/* 1 = p inside one of the capsules */
static int inside(const somvr_bz_cap *c, int n, const float p[3])
{
    for (int i = 0; i < n; i++)
        if (seg_d(p, c[i].c0, c[i].c1) <= c[i].r)
            return 1;
    return 0;
}

static void test_kinds(void)
{
    CHECK(somvr_bz_kind("Warg_Brown") == SOMVR_BZ_CARAGOR && somvr_bz_kind("DLC1_Warg_Rare_TorvinMount") == SOMVR_BZ_CARAGOR &&
              somvr_bz_kind("warg_stealth") == SOMVR_BZ_CARAGOR, "caragors");
    CHECK(somvr_bz_kind("Goblin") == SOMVR_BZ_GHUL && somvr_bz_kind("DLC1_Goblin_Rare_M1") == SOMVR_BZ_GHUL &&
              somvr_bz_kind("Goblin_Spitter") == SOMVR_BZ_GHUL, "ghuls");
    CHECK(somvr_bz_kind("Troll_Cave") == SOMVR_BZ_GRAUG && somvr_bz_kind("Troll_Legendary") == SOMVR_BZ_GRAUG, "graugs");
    CHECK(somvr_bz_kind("AI_Infantry") == SOMVR_BZ_NONE && somvr_bz_kind("AI_Slave_Human") == SOMVR_BZ_NONE &&
              somvr_bz_kind("Spider_Rare") == SOMVR_BZ_NONE && somvr_bz_kind("") == SOMVR_BZ_NONE && somvr_bz_kind(NULL) == 0,
          "not beasts");
    CHECK(!strcmp(somvr_bz_kind_name(SOMVR_BZ_CARAGOR), "caragor") && !strcmp(somvr_bz_kind_name(SOMVR_BZ_GRAUG), "graug") &&
              !strcmp(somvr_bz_kind_name(9), "?"), "kind names");
}

static void test_chain(void)
{
    somvr_bz_shape s;
    somvr_bz_shape_default(SOMVR_BZ_CARAGOR, &s);
    CHECK(s.r_head > 0 && s.r_body > s.r_head && s.snout > 0 && s.fb_r > 0, "a caragor's shape");
    /* a caragor facing +Z: its head 120 cm ahead of the pelvis, low (the body at 90 cm, the head at 100) */
    float root[3] = { 1000, 0, 2000 }, fwd[3] = { 0, 0, 1 };
    float pos[SOMVR_BZP_N][3] = { { 1000, 100, 2130 }, { 1000, 100, 2090 }, { 1000, 90, 2040 }, { 1000, 90, 1960 },
                                  { 1000, 80, 1880 } };
    int have[SOMVR_BZP_N] = { 1, 1, 1, 1, 1 };
    somvr_bz_cap c[SOMVR_BZ_CAPS];
    int n = somvr_bz_capsules(&s, (const float (*)[3])pos, have, root, fwd, c);
    CHECK(n == 5 && c[0].part == -1 && c[1].part == SOMVR_BZP_HEAD && c[4].part == SOMVR_BZP_PELVIS, "the snout + 4 links: %d", n);
    CHECK(NEAR(c[0].c1[2], 2130 + s.snout, 0.1) && NEAR(c[0].r, s.r_head, 0) && NEAR(c[4].r, s.r_tail, 0) &&
              NEAR(c[2].r, s.r_body, 0), "the snout along neck -> head, the radii");
    /* the blade where a caragor is: its back (110 cm up, over the chest) and its snout - in; over its back at 180 cm
     * (an orc's chest height) - out */
    float back[3] = { 1000, 125, 2040 }, nose[3] = { 1000, 100, 2150 }, over[3] = { 1000, 180, 2000 }, tail_tip[3] = { 1000, 80, 1885 };
    CHECK(inside(c, n, back) && inside(c, n, nose) && inside(c, n, tail_tip) && !inside(c, n, over),
          "its back, its snout, its tail in; over its back out");
    /* the orc's upright capsule about the root (r 35, 90 down and up): the snout 1.3 m ahead is out of it */
    float o0[3] = { 1000, -90, 2000 }, o1[3] = { 1000, 90, 2000 };
    CHECK(seg_d(nose, o0, o1) > 35, "the orc's capsule misses the snout (the bug)");
    /* a missing chest: neck -> pelvis */
    have[SOMVR_BZP_CHEST] = 0;
    n = somvr_bz_capsules(&s, (const float (*)[3])pos, have, root, fwd, c);
    CHECK(n == 4 && c[2].part == SOMVR_BZP_NECK && NEAR(c[2].c1[2], 1960, 0.1) && inside(c, n, back),
          "no chest: neck -> pelvis (%d)", n);
    /* head and pelvis only */
    int hp[SOMVR_BZP_N] = { 1, 0, 0, 1, 0 };
    n = somvr_bz_capsules(&s, (const float (*)[3])pos, hp, root, fwd, c);
    CHECK(n == 2 && c[0].part == -1 && NEAR(c[0].c1[2], 2130 + s.snout * (170.f / sqrtf(170 * 170 + 10 * 10)), 0.5) &&
              c[1].part == SOMVR_BZP_HEAD, "head + pelvis: the snout along pelvis -> head (%d)", n);
    /* one node: the facing's capsule */
    int one[SOMVR_BZP_N] = { 1, 0, 0, 0, 0 };
    float fx[3] = { 1, 0, 0 };
    n = somvr_bz_capsules(&s, (const float (*)[3])pos, one, root, fx, c);
    CHECK(n == 1 && c[0].part == -2 && NEAR(c[0].c0[0], 1000 - s.fb_back, 0.1) && NEAR(c[0].c1[0], 1000 + s.fb_front, 0.1) &&
              NEAR(c[0].c0[1], s.fb_up, 0.1) && NEAR(c[0].r, s.fb_r, 0), "one node: along the facing");
    n = somvr_bz_capsules(&s, (const float (*)[3])pos, NULL, root, NULL, c);
    CHECK(n == 1 && c[0].part == -2 && NEAR(c[0].c1[2], 2000 + s.fb_front, 0.1), "no nodes, no facing: +Z");
    /* a non-finite node is not there */
    float bad[SOMVR_BZP_N][3];
    memcpy(bad, pos, sizeof bad);
    bad[SOMVR_BZP_NECK][1] = NAN;
    int all[SOMVR_BZP_N] = { 1, 1, 1, 1, 1 };
    n = somvr_bz_capsules(&s, (const float (*)[3])bad, all, root, fwd, c);
    CHECK(n == 4 && c[1].part == SOMVR_BZP_HEAD && NEAR(c[1].c1[2], 2040, 0.1), "a NaN neck is skipped (%d)", n);
    /* the other kinds; none = nothing */
    somvr_bz_shape g, h, z;
    somvr_bz_shape_default(SOMVR_BZ_GRAUG, &g), somvr_bz_shape_default(SOMVR_BZ_GHUL, &h), somvr_bz_shape_default(0, &z);
    CHECK(g.r_body > s.r_body && h.r_body < s.r_body && g.snout == 0 && h.snout == 0 && z.fb_r == 0, "graug, ghul, none");
    n = somvr_bz_capsules(&z, (const float (*)[3])pos, one, root, fwd, c);
    CHECK(n == 0, "kind none: no capsule");
    n = somvr_bz_capsules(&g, (const float (*)[3])pos, all, root, fwd, c);
    CHECK(n == 3 && c[0].part == SOMVR_BZP_HEAD && NEAR(c[0].r, g.r_head, 0), "a graug: no snout, no tail link (%d)", n);
}

static void test_limbs_blows(void)
{
    somvr_bz_shape s;
    somvr_bz_shape_default(SOMVR_BZ_CARAGOR, &s);
    CHECK(somvr_bz_limb_r(SOMVR_BZ_GRAUG, SOMVR_BZL_R_THIGH) > somvr_bz_limb_r(SOMVR_BZ_GRAUG, SOMVR_BZL_R_SHIN) &&
              somvr_bz_limb_r(SOMVR_BZ_GRAUG, SOMVR_BZL_L_ARM) > 0 && somvr_bz_limb_r(SOMVR_BZ_CARAGOR, SOMVR_BZL_R_THIGH) == 0 &&
              somvr_bz_limb_r(SOMVR_BZ_GRAUG, SOMVR_BZL_N) == 0, "limb radii");
    somvr_bz_cap c[SOMVR_BZ_CAPS_ALL];
    /* a graug's shin 1.1 m above the ground: the blade at 1.0 m across it is in; its chest 2.5 m up is out of reach */
    float knee[3] = { 0, 110, 0 }, foot[3] = { 0, 25, 10 }, nan3[3] = { NAN, 0, 0 };
    int n = somvr_bz_limb(0, c, knee, foot, 38, SOMVR_BZL_R_SHIN);
    n = somvr_bz_limb(n, c, knee, nan3, 38, SOMVR_BZL_L_SHIN);
    n = somvr_bz_limb(n, c, knee, foot, 0, SOMVR_BZL_L_ARM);
    float blade[3] = { 30, 100, 2 };
    CHECK(n == 1 && c[0].part == -10 - SOMVR_BZL_R_SHIN && inside(c, n, blade), "a shin: in; a NaN or r 0: none (%d)", n);
    /* the blows: a caragor's jaws from the neck past the head; a graug's fists; nothing for a ghul */
    float head[3] = { 0, 90, 140 }, neck[3] = { 0, 100, 100 }, arm[2][3] = { { 50, 150, 0 }, { -50, 150, 0 } },
          hand[2][3] = { { 60, 60, 40 }, { -60, 60, 40 } }, b[2][3], t[2][3];
    int fist[2] = { 1, 0 };
    int k = somvr_bz_blows(SOMVR_BZ_CARAGOR, &s, head, neck, 1, (const float (*)[3])arm, (const float (*)[3])hand, fist, b, t);
    float l = sqrtf(10 * 10 + 40 * 40);
    CHECK(k == 1 && NEAR(b[0][2], 100, 0.01) && NEAR(t[0][2], 140 + 40 * s.snout / l, 0.01) && NEAR(t[0][1], 90 - 10 * s.snout / l, 0.01),
          "the caragor's jaws: neck -> past the snout (%d)", k);
    k = somvr_bz_blows(SOMVR_BZ_CARAGOR, &s, head, neck, 0, (const float (*)[3])arm, (const float (*)[3])hand, fist, b, t);
    CHECK(k == 0, "no head or neck: no jaws");
    k = somvr_bz_blows(SOMVR_BZ_GRAUG, &s, head, neck, 1, (const float (*)[3])arm, (const float (*)[3])hand, fist, b, t);
    CHECK(k == 1 && NEAR(b[0][0], 50, 0) && NEAR(t[0][1], 60, 0), "a graug: the right fist only (%d)", k);
    fist[1] = 1;
    k = somvr_bz_blows(SOMVR_BZ_GRAUG, &s, head, neck, 1, (const float (*)[3])arm, (const float (*)[3])hand, fist, b, t);
    CHECK(k == 2 && NEAR(t[1][0], -60, 0), "both fists (%d)", k);
    k = somvr_bz_blows(SOMVR_BZ_GHUL, &s, head, neck, 1, (const float (*)[3])arm, (const float (*)[3])hand, fist, b, t);
    CHECK(k == 0 && SOMVR_BZ_HOLD_MAX_S > 13.0, "a ghul: none (its claws: the game's); the hold covers run 235931's 13 s takedown");
}

int main(void)
{
    test_kinds();
    test_chain();
    test_limbs_blows();
    printf("test_beasts: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
