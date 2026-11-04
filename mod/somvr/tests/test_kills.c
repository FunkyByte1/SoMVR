/* test_kills.c - SOMVR_KILLS' pure part (somvr_kills.h): the side, the kind, the part, the pick, the placement, the
 * ease, the death's moment */
#include "../somvr_kills.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                      \
    do {                                                                                                                   \
        g_n++;                                                                                                             \
        if (!(c)) {                                                                                                        \
            g_fail++;                                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                    \
            printf(__VA_ARGS__);                                                                                           \
            printf("\n");                                                                                                  \
        }                                                                                                                  \
    } while (0)

static const somvr_kl_row g_rows[] = {
    {"AI_F_cut", NULL, SOMVR_KL_FRONT, SOMVR_KL_CUT | SOMVR_KL_OVER | SOMVR_KL_RISE, SOMVR_KL_BODY, SOMVR_KL_FIGHT, 0, 0, 2.0f, 0},
    {"AI_F_stab", NULL, SOMVR_KL_FRONT, SOMVR_KL_STAB, SOMVR_KL_BODY, SOMVR_KL_FIGHT, 0, 1.2f, 0, 0},
    {"AI_F_sever", NULL, SOMVR_KL_FRONT, SOMVR_KL_CUT, SOMVR_KL_NECK, SOMVR_KL_FIGHT, 1, 0, 0, 0},
    {"AI_B_any", NULL, SOMVR_KL_BACK, SOMVR_KL_ANYKIND, SOMVR_KL_ANYPART, SOMVR_KL_FIGHT, 0, 0, 0, 0},
    {"AI_B_sneak", NULL, SOMVR_KL_BACK, SOMVR_KL_ANYKIND, SOMVR_KL_ANYPART, SOMVR_KL_UNAWARE, 0, 0, 0, 0},
    {"AI_B_sneak2", NULL, SOMVR_KL_BACK, SOMVR_KL_ANYKIND, SOMVR_KL_ANYPART, SOMVR_KL_UNAWARE, 0, 0, 0, 0},
};
#define NR ((int)(sizeof g_rows / sizeof g_rows[0]))

static void test_side_kind_part(void)
{
    float o[3] = {0, 0, 0}, off = 0;
    float front[3] = {0, 100, 50}, back[3] = {0, 100, -50}, side[3] = {50, 100, 0};
    CHECK(somvr_kl_side(front, o, 0, 110, &off) == SOMVR_KL_FRONT && fabsf(off) < 1, "front (%f)", off);
    CHECK(somvr_kl_side(back, o, 0, 110, &off) == SOMVR_KL_BACK && fabsf(fabsf(off) - 180) < 1, "back (%f)", off);
    CHECK(somvr_kl_side(side, o, 0, 110, &off) == SOMVR_KL_FRONT && fabsf(off - 90) < 1, "the side is its front (%f)", off);
    CHECK(somvr_kl_side(front, o, 3.14159265f, 110, NULL) == SOMVR_KL_BACK, "turned round: its back");
    float ax[3] = {0, 0, 1};
    float stab[3] = {0, -1, 8}, level[3] = {8, 0, 0}, over[3] = {1, -8, 1}, rise[3] = {1, 8, 0};
    CHECK(somvr_kl_kind(stab, ax, 0.5f) == SOMVR_KL_STAB, "along the blade: a stab");
    CHECK(somvr_kl_kind(level, ax, 0.5f) == SOMVR_KL_CUT, "across, level: a cut");
    CHECK(somvr_kl_kind(over, ax, 0.5f) == SOMVR_KL_OVER, "down: overhead");
    CHECK(somvr_kl_kind(rise, ax, 0.5f) == SOMVR_KL_RISE, "up: rising");
    float back_dir[3] = {0, 0, -8};
    CHECK(somvr_kl_kind(back_dir, ax, 0.5f) == SOMVR_KL_CUT, "drawn back (the pommel leading): no stab");
    /* mark 18's stab (the user, 11-02): cos 0.55 to the axis */
    float m18[3] = {0.835f, 0, 0.55f};
    CHECK(somvr_kl_kind(m18, ax, 0.5f) == SOMVR_KL_STAB, "mark 18's real stab (cos 0.55): a stab at 60 deg");
    CHECK(somvr_kl_part(150, 140, NAN, 12) == SOMVR_KL_NECK, "high: the neck");
    CHECK(somvr_kl_part(100, 140, NAN, 12) == SOMVR_KL_BODY, "the chest: the body");
    CHECK(somvr_kl_part(100, 140, 8, 12) == SOMVR_KL_NECK, "by the neck node: the neck");
}

static void test_pick(void)
{
    somvr_kl_blow b = {SOMVR_KL_FRONT, SOMVR_KL_CUT, SOMVR_KL_BODY, SOMVR_KL_FIGHT};
    CHECK(somvr_kl_pick(g_rows, NR, &b, 0, NULL) == 0, "front level cut: the cut row");
    b.kind = SOMVR_KL_STAB;
    CHECK(somvr_kl_pick(g_rows, NR, &b, 0, NULL) == 1, "front stab: the stab row");
    b.kind = SOMVR_KL_CUT, b.part = SOMVR_KL_NECK;
    CHECK(somvr_kl_pick(g_rows, NR, &b, 0, NULL) == 2, "front neck cut: the sever row");
    unsigned char off[NR] = {0};
    off[2] = 1;
    int p = somvr_kl_pick(g_rows, NR, &b, 0, off);
    CHECK(p == 0 || p == 1, "no sever row: a body row (%d)", p);
    b.part = SOMVR_KL_BODY, b.kind = SOMVR_KL_STAB;
    off[1] = 1, off[2] = 0;
    p = somvr_kl_pick(g_rows, NR, &b, 0, off);
    CHECK(p == 0, "no stab row: the front body row, never the sever (%d)", p);
    b.side = SOMVR_KL_BACK, b.state = SOMVR_KL_UNAWARE;
    int a = somvr_kl_pick(g_rows, NR, &b, 0, NULL), c = somvr_kl_pick(g_rows, NR, &b, 1, NULL);
    CHECK(a == 4 && c == 5, "unaware from behind: the sneak rows in turn (%d %d)", a, c);
    b.state = SOMVR_KL_FIGHT;
    CHECK(somvr_kl_pick(g_rows, NR, &b, 7, NULL) == 3, "fighting, from behind: the back row");
    b.state = SOMVR_KL_DOWNED;
    CHECK(somvr_kl_pick(g_rows, NR, &b, 0, NULL) == -1, "downed: no row here");
    CHECK(somvr_kl_find(g_rows, NR, "AI_B_sneak2") == 5 && somvr_kl_find(g_rows, NR, "nope") == -1, "find");
}

static void test_align(void)
{
    float o[3] = {0, 0, 0}, t[3] = {0, 0, 100}, dyaw = 9, dp[2] = {9, 9};
    CHECK(somvr_kl_align(o, 0, t, SOMVR_KL_FRONT, 55, 30, 45, &dyaw, dp) && fabsf(dyaw) < 1e-4f && fabsf(dp[1]) < 1e-4f,
          "facing him at 1 m: nothing (%f %f)", dyaw, dp[1]);
    CHECK(somvr_kl_align(o, 0.5f, t, SOMVR_KL_FRONT, 55, 30, 45, &dyaw, dp) && fabsf(dyaw + 0.5f) < 1e-4f, "29 deg off: turned back");
    CHECK(somvr_kl_align(o, 1.2f, t, SOMVR_KL_FRONT, 55, 30, 45, &dyaw, dp) && fabsf(dyaw + 0.7853982f) < 1e-4f,
          "69 deg off: 45 of it (%f)", dyaw);
    CHECK(!somvr_kl_align(o, 2.0f, t, SOMVR_KL_FRONT, 55, 30, 45, &dyaw, dp), "115 deg off: too far, no alignment");
    CHECK(somvr_kl_align(o, 3.14159265f, t, SOMVR_KL_BACK, 55, 30, 45, &dyaw, dp) && fabsf(dyaw) < 1e-3f, "its back to him: kept");
    float near[3] = {0, 0, 30};
    CHECK(somvr_kl_align(o, 0, near, SOMVR_KL_FRONT, 55, 30, 45, &dyaw, dp) && fabsf(dp[1] + 25) < 1e-3f,
          "30 cm: slid 25 cm away (%f)", dp[1]);
    float touch[3] = {0, 0, 5};
    CHECK(somvr_kl_align(o, 0, touch, SOMVR_KL_FRONT, 75, 30, 45, &dyaw, dp) == 0, "70 cm short: too far off");
    CHECK(somvr_kl_ease(0, 0.15) == 0 && somvr_kl_ease(0.15, 0.15) == 1 && fabsf(somvr_kl_ease(0.075, 0.15) - 0.5f) < 1e-4f,
          "ease");
    CHECK(somvr_kl_death_at(&g_rows[1], 0.1f) == 1.2f && fabsf(somvr_kl_death_at(&g_rows[0], 0.1f) - 1.9f) < 1e-5f &&
              isnan(somvr_kl_death_at(&g_rows[3], 0.1f)), "the death's moment");
}

/* build 36 (SOMVR_KILLS_QUICK): the session's three late deaths (11-03): 'F' and 'F5' did not play (the head 6.1 and 0.0 cm
 * down in 0.8 s: dead at 0.81 / 0.80 s - build 31's stall rule), 'B4' walked 5 s (its moment at 5.01 s); 'F9' fell at once
 * (50 cm by 0.14 s) and plays on */
static void test_quick(void)
{
    somvr_kl_quick q = {0.15f, 3.0f, 0.6f, 10.0f, 1.5f, 8.0f};
    CHECK(somvr_kl_quick_why(&q, 0.10, 1, 0.0f, 0.0f, 0.0f) == SOMVR_KLQ_GO, "0.10 s, still: not yet");
    CHECK(somvr_kl_quick_why(&q, 0.15, 1, 0.4f, 0.0f, 0.0f) == SOMVR_KLQ_STILL, "0.15 s, the pose still (F5's): the death now");
    CHECK(somvr_kl_quick_why(&q, 0.15, 0, 0.0f, 0.0f, 0.0f) == SOMVR_KLQ_UNSEEN, "0.15 s, no skeleton: the death now");
    CHECK(somvr_kl_quick_why(&q, 0.15, 1, 8.0f, 2.0f, 0.0f) == SOMVR_KLQ_GO, "0.15 s, the pose moving: on");
    CHECK(somvr_kl_quick_why(&q, 0.60, 1, 20.0f, 4.0f, 0.0f) == SOMVR_KLQ_NOFALL, "0.6 s, moving but the head up (B4's walk): the death");
    CHECK(somvr_kl_quick_why(&q, 0.60, 1, 30.0f, 22.0f, 0.0f) == SOMVR_KLQ_GO, "0.6 s, falling: on");
    CHECK(somvr_kl_quick_why(&q, 1.50, 1, 60.0f, 50.0f, 0.0f) == SOMVR_KLQ_LATE, "1.5 s: the death whatever it shows");
    CHECK(somvr_kl_quick_why(&q, 0.14, 1, 50.0f, 50.0f, 0.0f) == SOMVR_KLQ_GO, "F9: 50 cm down by 0.14 s - on");
    somvr_kl_quick off = {0, 0, 0, 0, 0, 0};
    CHECK(somvr_kl_quick_why(&off, 9.0, 0, 0, 0, 0.0f) == SOMVR_KLQ_GO, "every check off: on");
    /* boot 20261103-230240: F6 on an orc in a recoil - the pose moving but its head 10 cm up by 0.15 s (its recoil
     * straightening): not the half; F3's playing half rose 3 cm first, then fell */
    CHECK(somvr_kl_quick_why(&q, 0.15, 1, 10.0f, 0.0f, 10.0f) == SOMVR_KLQ_RISING, "F6: its head 10 cm up - the death");
    CHECK(somvr_kl_quick_why(&q, 0.15, 1, 6.4f, 0.0f, 3.0f) == SOMVR_KLQ_GO, "F3: 3 cm up, moving - on");
    CHECK(somvr_kl_quick_why(&q, 0.70, 1, 104.0f, 72.0f, 3.0f) == SOMVR_KLQ_GO && somvr_kl_quick_why(&q, 0.70, 1, 104.0f, 72.0f, 20.0f) == SOMVR_KLQ_GO,
          "fallen: a rise after it is the old rules' (pose rising)");
    /* the pose against the root in the orc's own frame: a turn of the whole orc is no motion, a head dropping is */
    float a[2][3] = {{0, 0, 10}, {0, 60, 20}}, b[2][3], c[2][3] = {{0, 0, 10}, {0, 45, 30}};
    float y = 1.0f, s = sinf(y), co = cosf(y);
    for (int i = 0; i < 2; i++) /* the same pose turned by y: world = R(y) local */
        b[i][0] = a[i][0] * co + a[i][2] * s, b[i][1] = a[i][1], b[i][2] = -a[i][0] * s + a[i][2] * co;
    CHECK(somvr_kl_pose_moved(a, 0, b, y, 2) < 1e-3f, "turned whole: 0 (%f)", somvr_kl_pose_moved(a, 0, b, y, 2));
    CHECK(fabsf(somvr_kl_pose_moved(a, 0, c, 0, 2) - sqrtf(15 * 15 + 10 * 10)) < 1e-3f, "the head 15 down 10 ahead: %f",
          somvr_kl_pose_moved(a, 0, c, 0, 2));
    CHECK(!strcmp(somvr_kl_quick_name(SOMVR_KLQ_STILL), "its half not started (the pose still)"), "the names");
}

int main(void)
{
    test_side_kind_part();
    test_pick();
    test_align();
    test_quick();
    printf("test_kills: %d checks, %d failed\n", g_n, g_fail);
    return g_fail != 0;
}
