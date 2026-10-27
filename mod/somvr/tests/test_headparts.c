/* test_headparts.c - unit tests of somvr_headparts.c: the slot list (main = body + cloth), the frame's head-node set, the
 * cloth set's vector and the near-log box distance, the per-draw rule (eye windows, the five camera passes, never the
 * shadows), the game -> render ring (docs/re-xr-pipeline.md 26, 26.7) */
#include "../somvr_headparts.h"

#include <math.h>
#include <pthread.h>
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

static void test_slots(void)
{
    uint32_t m = 99;
    char b[96];
    CHECK(!somvr_hp_parse_slots("1,3,4", &m) && m == SOMVR_HP_DEFAULT, "1,3,4: %#x", m);
    CHECK(SOMVR_HP_DEFAULT == 0x1a, "the default slots %#x", SOMVR_HP_DEFAULT);
    CHECK(!somvr_hp_parse_slots("2-4,14", &m) && m == 0x401c, "2-4,14: %#x", m);
    CHECK(!somvr_hp_parse_slots("none", &m) && m == 0, "none: %#x", m);
    CHECK(!somvr_hp_parse_slots("19", &m) && m == 1u << 19, "19: %#x", m);
    CHECK(!somvr_hp_parse_slots("0", &m) && m == 1, "0: %#x", m);
    CHECK(!somvr_hp_parse_slots("1,1", &m) && m == 2, "a slot twice: %#x", m);
    CHECK(!somvr_hp_parse_slots("1,4 rest", &m) && m == 0x12, "ends at a space: %#x", m);
    CHECK(!somvr_hp_parse_slots("3\n", &m) && m == 8, "ends at a newline: %#x", m);
    CHECK(!somvr_hp_parse_slots("none\r", &m) && m == 0, "none + CR: %#x", m);
    /* main: the model's main renderable (Talion's whole body, bit SOMVR_HP_MAIN = "body") and its cloth (the fur mantle,
     * bit SOMVR_HP_CLOTH = "cloth") */
    CHECK(SOMVR_HP_MAIN_BITS == (1u << 20 | 1u << 22) && SOMVR_HP_CLOTH == 22, "main = body + cloth: %#x", SOMVR_HP_MAIN_BITS);
    CHECK(!somvr_hp_parse_slots("main", &m) && m == SOMVR_HP_MAIN_BITS, "main: %#x", m);
    CHECK(!somvr_hp_parse_slots("main,1,3", &m) && m == (SOMVR_HP_MAIN_BITS | 0xa), "main,1,3: %#x", m);
    CHECK(!somvr_hp_parse_slots("4,main\n", &m) && m == (SOMVR_HP_MAIN_BITS | 0x10), "4,main: %#x", m);
    CHECK(!somvr_hp_parse_slots("body", &m) && m == 1u << SOMVR_HP_MAIN, "body: %#x", m);
    CHECK(!somvr_hp_parse_slots("cloth", &m) && m == 1u << SOMVR_HP_CLOTH, "cloth: %#x", m);
    CHECK(!somvr_hp_parse_slots("cloth,3 x", &m) && m == (1u << SOMVR_HP_CLOTH | 8), "cloth,3: %#x", m);
    CHECK(!somvr_hp_parse_slots("body,cloth", &m) && m == SOMVR_HP_MAIN_BITS, "body,cloth: %#x", m);
    CHECK(!somvr_hp_parse_slots("main,body", &m) && m == SOMVR_HP_MAIN_BITS, "main,body: %#x", m);
    somvr_hp_slots_str(SOMVR_HP_MAIN_BITS | 0xa, b, sizeof b);
    CHECK(!strcmp(b, "main,1,3"), "str main: %s", b);
    somvr_hp_slots_str(SOMVR_HP_MAIN_BITS, b, sizeof b);
    CHECK(!strcmp(b, "main"), "str main alone: %s", b);
    somvr_hp_slots_str(1u << SOMVR_HP_MAIN, b, sizeof b);
    CHECK(!strcmp(b, "body"), "str body: %s", b);
    somvr_hp_slots_str(1u << SOMVR_HP_CLOTH | 0x10, b, sizeof b);
    CHECK(!strcmp(b, "cloth,4"), "str cloth,4: %s", b);
    for (uint32_t t = 0; t < 4; t++) { /* the words round trip */
        uint32_t w = (t & 1 ? 1u << SOMVR_HP_MAIN : 0) | (t & 2 ? 1u << SOMVR_HP_CLOTH : 0) | 0x12, back = 0;
        somvr_hp_slots_str(w, b, sizeof b);
        CHECK(!somvr_hp_parse_slots(b, &back) && back == w, "round trip %#x -> %s -> %#x", w, b, back);
    }
    m = 7;
    const char *bad[] = { "", "x", "20", "1,", ",1", "4-2", "-1", "1;3", "1-", "1-20", "0001", "nonex", "3.5", "main,", "mainx",
                          "mai", "bod", "bodyx", "cloth,", "clothes", "Cloth", NULL };
    for (int i = 0; bad[i]; i++)
        CHECK(somvr_hp_parse_slots(bad[i], &m) && m == 7, "'%s' accepted (%#x)", bad[i], m);
    CHECK(somvr_hp_parse_slots(NULL, &m) && m == 7, "NULL accepted");
    somvr_hp_slots_str(SOMVR_HP_DEFAULT, b, sizeof b);
    CHECK(!strcmp(b, "1,3,4"), "str %s", b);
    somvr_hp_slots_str(0, b, sizeof b);
    CHECK(!strcmp(b, "none"), "str 0: %s", b);
    somvr_hp_slots_str(0xfffff, b, sizeof b);
    CHECK(!strcmp(b, "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19"), "str all: %s", b);
    somvr_hp_slots_str(0xfffff, b, 8); /* cut, still terminated */
    CHECK(strlen(b) < 8, "str cut: '%s'", b);
    CHECK(!strcmp(somvr_hp_slot_name(1), "head") && !strcmp(somvr_hp_slot_name(3), "helm") &&
              !strcmp(somvr_hp_slot_name(4), "hair") && !strcmp(somvr_hp_slot_name(0), "body") &&
              !strcmp(somvr_hp_slot_name(19), "slot19") && !strcmp(somvr_hp_slot_name(20), "main") &&
              !strcmp(somvr_hp_slot_name(21), "weapon") && !strcmp(somvr_hp_slot_name(22), "cloth") &&
              !strcmp(somvr_hp_slot_name(23), "?") &&
              !strcmp(somvr_hp_slot_name(-1), "?"),
          "slot names");
    CHECK(!strcmp(somvr_hp_pass_name(SOMVR_HP_GBUF), "gbuffer") && !strcmp(somvr_hp_pass_name(SOMVR_HP_DEEP), "deep") &&
              !strcmp(somvr_hp_pass_name(SOMVR_HP_VEL), "velocity") &&
              !strcmp(somvr_hp_pass_name(SOMVR_HP_SHADOW), "shadow") && !strcmp(somvr_hp_pass_name(99), "?"),
          "pass names");
}

static void test_set(void)
{
    somvr_hp_set s;
    somvr_hp_set_init(&s, 42);
    CHECK(s.frame == 42 && s.n == 0, "init");
    CHECK(somvr_hp_match(&s, 0x1000) == -1 && somvr_hp_match(NULL, 0x1000) == -1, "empty set matches");
    CHECK(somvr_hp_set_add(&s, 0x1000, 1) && somvr_hp_set_add(&s, 0x2000, 3) && somvr_hp_set_add(&s, 0x3000, 4), "add");
    CHECK(!somvr_hp_set_add(&s, 0, 4), "a null node added");
    CHECK(!somvr_hp_set_add(&s, 0x2000, 4) && s.n == 3, "a duplicate added (n %u)", s.n);
    CHECK(somvr_hp_match(&s, 0x1000) == 0 && somvr_hp_match(&s, 0x2000) == 1 && somvr_hp_match(&s, 0x3000) == 2 &&
              s.slot[0] == 1 && s.slot[1] == 3 && s.slot[2] == 4,
          "match");
    CHECK(somvr_hp_match(&s, 0x1008) == -1 && somvr_hp_match(&s, 0) == -1 && somvr_hp_match(&s, 0xfff) == -1,
          "near misses / null match");
    for (uintptr_t k = 4; k < SOMVR_HP_MAX + 8; k++) /* entry k - 1 */
        somvr_hp_set_add(&s, 0x10000 * k, (int)k);
    CHECK(SOMVR_HP_MAX == 16 && s.n == SOMVR_HP_MAX && somvr_hp_match(&s, 0x10000 * SOMVR_HP_MAX) == SOMVR_HP_MAX - 1 &&
              somvr_hp_match(&s, 0x10000 * (SOMVR_HP_MAX + 1)) == -1,
          "full at %d (n %u)", SOMVR_HP_MAX, s.n);
    CHECK(s.at[0] == 0 && s.at[1] == 0 && s.at[2] == 0, "init: no head point");
}

/* the cloth set's entry vector; SOMVR_HP_NEARLOG's box distance */
static void test_vec_box(void)
{
    CHECK(somvr_hp_vec_n(0x1000, 0x1000, 0x18, 8) == 0 && somvr_hp_vec_n(0, 0, 0x18, 8) == 0, "empty vectors");
    CHECK(somvr_hp_vec_n(0x1000, 0x1018, 0x18, 8) == 1 && somvr_hp_vec_n(0x1000, 0x1000 + 8 * 0x18, 0x18, 8) == 8,
          "one, eight entries");
    CHECK(somvr_hp_vec_n(0x1000, 0x1000 + 9 * 0x18, 0x18, 8) == -1, "more than max");
    CHECK(somvr_hp_vec_n(0x1018, 0x1000, 0x18, 8) == -1 && somvr_hp_vec_n(0x1000, 0x1010, 0x18, 8) == -1 &&
              somvr_hp_vec_n(0, 0x18, 0x18, 8) == -1 && somvr_hp_vec_n(0x1000, 0x1018, 0, 8) == -1 &&
              somvr_hp_vec_n(0x1000, 0x1018, 0x18, -1) == -1,
          "end before begin, a partial entry, a null begin, no stride, a bad max");
    const float lo[3] = { -10, 0, 5 }, hi[3] = { 10, 20, 15 };
    const float in[3] = { 0, 10, 10 }, side[3] = { 13, 10, 10 }, corner[3] = { 13, 24, 15 };
    CHECK(somvr_hp_box_dist(lo, hi, in) == 0 && somvr_hp_box_dist(lo, hi, side) == 3 && somvr_hp_box_dist(lo, hi, corner) == 5,
          "box distances %g %g %g", somvr_hp_box_dist(lo, hi, in), somvr_hp_box_dist(lo, hi, side),
          somvr_hp_box_dist(lo, hi, corner));
    const float inv[3] = { 11, 0, 5 }, nan[3] = { 0, NAN, 0 }, inf[3] = { 0, INFINITY, 0 };
    CHECK(somvr_hp_box_dist(inv, hi, in) == -1 && somvr_hp_box_dist(lo, nan, in) == -1 && somvr_hp_box_dist(lo, hi, inf) == -1,
          "not boxes");
}

/* the rule: eye windows, the five camera passes, the frame's own set, its nodes */
static void test_skip(void)
{
    somvr_hp_set s;
    somvr_hp_set_init(&s, 100);
    somvr_hp_set_add(&s, 0xa000, SOMVR_HP_HEAD);
    somvr_hp_set_add(&s, 0xb000, SOMVR_HP_HAIR);
    for (int p = 0; p < SOMVR_HP_PASSES; p++) {
        CHECK(somvr_hp_skip(0, p, &s, 100, 0xa000) && somvr_hp_skip(1, p, &s, 100, 0xb000), "%s: the head drawn in the eyes",
              somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip(-1, p, &s, 100, 0xa000), "%s: skipped outside an eye window (flat frame, UI scene, after H8)",
              somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip(0, p, &s, 100, 0xc000) && !somvr_hp_skip(0, p, &s, 100, 0),
              "%s: another node skipped (the body, an orc)", somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip(0, p, &s, 101, 0xa000) && !somvr_hp_skip(0, p, &s, 99, 0xa000) && !somvr_hp_skip(0, p, &s, 0, 0xa000),
              "%s: another frame's set used", somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip(0, p, NULL, 100, 0xa000), "%s: no set skipped", somvr_hp_pass_name(p));
    }
    CHECK(!somvr_hp_skip(0, SOMVR_HP_SHADOW, &s, 100, 0xa000) && !somvr_hp_skip(1, SOMVR_HP_SHADOW, &s, 100, 0xb000),
          "the shadow passes lose the head");
    CHECK(!somvr_hp_skip(0, SOMVR_HP_OTHER, &s, 100, 0xa000) && !somvr_hp_skip(0, -1, &s, 100, 0xa000) &&
              !somvr_hp_skip(0, 77, &s, 100, 0xa000),
          "an unknown pass skips");
    somvr_hp_set e;
    somvr_hp_set_init(&e, 100);
    CHECK(!somvr_hp_skip(0, SOMVR_HP_GBUF, &e, 100, 0xa000), "an empty set skips");
    /* SOMVR_HP_POSE: the nodes the hidden body's transforms animate (the wraith over Talion), only those */
    CHECK(!e.pose && !somvr_hp_skip_pose(0, SOMVR_HP_GBUF, &e, 100, 0) && !somvr_hp_skip_pose(0, SOMVR_HP_GBUF, &e, 100, 0x7000),
          "no pose: nothing skipped by it");
    s.pose = 0x7000;
    for (int p = 0; p < SOMVR_HP_PASSES; p++) {
        CHECK(somvr_hp_skip_pose(0, p, &s, 100, 0x7000) && somvr_hp_skip_pose(1, p, &s, 100, 0x7000),
              "%s: a node of the body's pose drawn in the eyes", somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip_pose(0, p, &s, 100, 0x7008) && !somvr_hp_skip_pose(0, p, &s, 100, 0),
              "%s: a node of another pose skipped (a weapon, an orc)", somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip_pose(-1, p, &s, 100, 0x7000) && !somvr_hp_skip_pose(0, p, &s, 101, 0x7000) &&
                  !somvr_hp_skip_pose(0, p, NULL, 100, 0x7000) && !somvr_hp_skip_pose(0, p, &s, 0, 0x7000),
              "%s: outside an eye window, another frame's set, no set", somvr_hp_pass_name(p));
    }
    CHECK(!somvr_hp_skip_pose(0, SOMVR_HP_SHADOW, &s, 100, 0x7000) && !somvr_hp_skip_pose(0, SOMVR_HP_OTHER, &s, 100, 0x7000),
          "the shadow passes keep the pose's nodes");
    e.pose = 0x7000;
    CHECK(somvr_hp_skip_pose(0, SOMVR_HP_FWD, &e, 100, 0x7000), "a pose without nodes in the set still counts");
    /* SOMVR_HP_AWAY: a held-away set skips the skeletal nodes of Talion's own meshes (the strike's wraith copy) */
    somvr_hp_set a;
    somvr_hp_set_init(&a, 100);
    CHECK(somvr_hp_add_mesh(&a, 0x1588) && somvr_hp_add_mesh(&a, 0x1540) && !somvr_hp_add_mesh(&a, 0x1540) &&
              !somvr_hp_add_mesh(&a, 0) && a.nmesh == 2,
          "the meshes: two in, a duplicate and 0 not (%u)", a.nmesh);
    CHECK(!somvr_hp_skip_mesh(0, SOMVR_HP_FWD, &a, 100, 0x1540), "not away: no mesh skipped");
    a.away = 1;
    for (int p = 0; p < SOMVR_HP_PASSES; p++) {
        CHECK(somvr_hp_skip_mesh(0, p, &a, 100, 0x1540) && somvr_hp_skip_mesh(1, p, &a, 100, 0x1588),
              "%s: away, Talion's mesh drawn in the eyes", somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip_mesh(0, p, &a, 100, 0x1548) && !somvr_hp_skip_mesh(0, p, &a, 100, 0),
              "%s: away, another mesh skipped (an orc, a weapon)", somvr_hp_pass_name(p));
        CHECK(!somvr_hp_skip_mesh(-1, p, &a, 100, 0x1540) && !somvr_hp_skip_mesh(0, p, &a, 101, 0x1540) &&
                  !somvr_hp_skip_mesh(0, p, NULL, 100, 0x1540) && !somvr_hp_skip_mesh(0, p, &a, 0, 0x1540),
              "%s: away, outside an eye window, another frame's set, no set", somvr_hp_pass_name(p));
    }
    CHECK(!somvr_hp_skip_mesh(0, SOMVR_HP_SHADOW, &a, 100, 0x1540) && !somvr_hp_skip_mesh(0, SOMVR_HP_OTHER, &a, 100, 0x1540),
          "away: the shadow passes keep Talion's meshes");
    for (uintptr_t k = 0; k < SOMVR_HP_MESHES + 2; k++)
        somvr_hp_add_mesh(&a, 0x2000 + 8 * k);
    CHECK(a.nmesh == SOMVR_HP_MESHES && somvr_hp_skip_mesh(0, SOMVR_HP_FWD, &a, 100, 0x2008) &&
              !somvr_hp_skip_mesh(0, SOMVR_HP_FWD, &a, 100, 0x2000 + 8 * SOMVR_HP_MESHES),
          "the meshes full at %d", SOMVR_HP_MESHES);
}

/* SOMVR_HP_COPIES (build 16): a node drawing a weapon's mesh that is none of the weapons' own nodes = a copy (the
 * wraith's see-through sword); the weapons' own nodes, other meshes, the set's off state, the windows */
static void test_copies(void)
{
    somvr_hp_set a;
    somvr_hp_set_init(&a, 200);
    CHECK(somvr_hp_add_weapon(&a, 0x7000, 0x18e0) && somvr_hp_add_weapon(&a, 0x7100, 0x18f0) &&
              somvr_hp_add_weapon(&a, 0x7200, 0x18e0) && !somvr_hp_add_weapon(&a, 0x7000, 0x18e0) &&
              !somvr_hp_add_weapon(&a, 0, 0x18e0) && !somvr_hp_add_weapon(&a, 0x7300, 0) && a.nweap == 3,
          "weapons added: %u", a.nweap);
    CHECK(somvr_hp_copy_of(&a, 0x9000, 0x18e0) == 0 && somvr_hp_copy_of(&a, 0x9000, 0x18f0) == 1,
          "a copy: another node with a weapon's mesh");
    CHECK(somvr_hp_copy_of(&a, 0x7000, 0x18e0) < 0 && somvr_hp_copy_of(&a, 0x7200, 0x18e0) < 0 &&
              somvr_hp_copy_of(&a, 0x7100, 0x18f0) < 0, "the weapons' own nodes are no copies");
    CHECK(somvr_hp_copy_of(&a, 0x9000, 0x1900) < 0 && somvr_hp_copy_of(&a, 0x9000, 0) < 0 && somvr_hp_copy_of(&a, 0, 0x18e0) < 0 &&
              somvr_hp_copy_of(NULL, 0x9000, 0x18e0) < 0, "other meshes, none, no set");
    CHECK(!somvr_hp_skip_copy(0, SOMVR_HP_FWD, &a, 200, 0x9000, 0x18e0), "log: found, not skipped");
    a.copies = 1;
    CHECK(!somvr_hp_skip_copy(0, SOMVR_HP_FWD, &a, 200, 0x9000, 0x18e0) && !somvr_hp_skip_copy(0, SOMVR_HP_FWD, &a, 200, 0x9100, 0x18f0),
          "on: the copies of weapons the set shows (held) are kept");
    somvr_hp_set_add(&a, 0x7000, SOMVR_HP_WEAPON); /* the first weapon put away (hidden), the second held */
    CHECK(somvr_hp_copy_of(&a, 0x9000, 0x18e0) == 0, "the hidden weapon's entry first (%d)", somvr_hp_copy_of(&a, 0x9000, 0x18e0));
    for (int p = 0; p < SOMVR_HP_PASSES; p++) {
        CHECK(somvr_hp_skip_copy(0, p, &a, 200, 0x9000, 0x18e0) && !somvr_hp_skip_copy(1, p, &a, 200, 0x9100, 0x18f0),
              "on: the hidden weapon's copy skipped, the held one's kept in pass %d", p);
        CHECK(!somvr_hp_skip_copy(0, p, &a, 200, 0x7000, 0x18e0) && !somvr_hp_skip_copy(0, p, &a, 200, 0x9000, 0x1900),
              "on: the weapon itself (the head-parts rule hides it) and other meshes not by this rule in pass %d", p);
        CHECK(!somvr_hp_skip_copy(-1, p, &a, 200, 0x9000, 0x18e0) && !somvr_hp_skip_copy(0, p, &a, 201, 0x9000, 0x18e0) &&
                  !somvr_hp_skip_copy(0, p, NULL, 200, 0x9000, 0x18e0) && !somvr_hp_skip_copy(0, p, &a, 0, 0x9000, 0x18e0),
              "outside an eye, another frame, no set: drawn (pass %d)", p);
    }
    /* a mesh both a hidden and a shown weapon draw (one model in two entries): the hidden entry rules */
    somvr_hp_add_weapon(&a, 0x7400, 0x18f0);
    somvr_hp_set_add(&a, 0x7400, SOMVR_HP_WEAPON);
    CHECK(somvr_hp_copy_of(&a, 0x9100, 0x18f0) == 3 && somvr_hp_skip_copy(0, SOMVR_HP_FWD, &a, 200, 0x9100, 0x18f0),
          "a shared mesh: the hidden entry (%d)", somvr_hp_copy_of(&a, 0x9100, 0x18f0));
    CHECK(!somvr_hp_skip_copy(0, SOMVR_HP_SHADOW, &a, 200, 0x9000, 0x18e0) &&
              !somvr_hp_skip_copy(0, SOMVR_HP_OTHER, &a, 200, 0x9000, 0x18e0), "the shadows and other passes keep it");
    for (int k = 0; k < 2 * SOMVR_HP_WEAPONS; k++)
        somvr_hp_add_weapon(&a, 0xa000 + 8 * (uintptr_t)k, 0x2000);
    CHECK(a.nweap == SOMVR_HP_WEAPONS, "the weapons full at %d (%u)", SOMVR_HP_WEAPONS, a.nweap);
}

static void test_ring(void)
{
    static somvr_hp_ring r;
    somvr_hp_ring_init(&r);
    somvr_hp_set s, o;
    CHECK(!somvr_hp_lookup(&r, 0, &o) && !somvr_hp_lookup(&r, 1, &o), "an empty ring answers");
    for (uint64_t f = 1; f <= 10; f++) {
        somvr_hp_set_init(&s, f);
        for (uint64_t k = 0; k < f % 4; k++)
            somvr_hp_set_add(&s, 0x1000 * f + k * 8, (int)k);
        s.at[0] = (float)f, s.at[1] = -2.5f * (float)f, s.at[2] = 1e4f;
        s.pose = f % 2 ? 0x5000 + f : 0;
        s.away = f % 3 == 0;
        for (uint64_t k = 0; k < f % 3; k++)
            somvr_hp_add_mesh(&s, 0x1500 + 8 * f + k);
        s.copies = f % 2 == 0;
        for (uint64_t k = 0; k < f % 5; k++)
            somvr_hp_add_weapon(&s, 0x6000 + 0x100 * f + 8 * k, 0x1800 + f);
        somvr_hp_publish(&r, &s);
    }
    for (uint64_t f = 7; f <= 10; f++) { /* the last four */
        memset(&o, 0x55, sizeof o);
        int ok = somvr_hp_lookup(&r, f, &o);
        CHECK(ok && o.frame == f && o.n == f % 4 && o.at[0] == (float)f && o.at[1] == -2.5f * (float)f && o.at[2] == 1e4f &&
                  o.pose == (f % 2 ? 0x5000 + f : 0) && o.away == (f % 3 == 0) && o.nmesh == f % 3 &&
                  (o.nmesh < 1 || o.mesh[0] == 0x1500 + 8 * f) && (o.nmesh < 2 || o.mesh[1] == 0x1500 + 8 * f + 1),
              "frame %llu: ok %d n %u at %g %g %g away %u meshes %u", (unsigned long long)f, ok, o.n, o.at[0], o.at[1],
              o.at[2], o.away, o.nmesh);
        for (uint32_t k = 0; ok && k < o.n; k++)
            CHECK(o.node[k] == 0x1000 * f + k * 8 && o.slot[k] == k, "frame %llu entry %u", (unsigned long long)f, k);
        CHECK(ok && o.copies == (f % 2 == 0) && o.nweap == f % 5, "frame %llu: copies %u weapons %u", (unsigned long long)f,
              o.copies, o.nweap);
        for (uint32_t k = 0; ok && k < o.nweap; k++)
            CHECK(o.wnode[k] == 0x6000 + 0x100 * f + 8 * k && o.wmesh[k] == 0x1800 + f, "frame %llu weapon %u",
                  (unsigned long long)f, k);
    }
    for (uint64_t f = 1; f <= 6; f++)
        CHECK(!somvr_hp_lookup(&r, f, &o), "frame %llu: overwritten but found", (unsigned long long)f);
    CHECK(!somvr_hp_lookup(&r, 14, &o), "a future frame found");
    somvr_hp_set_init(&s, 0);
    somvr_hp_set_add(&s, 0x9000, 1);
    somvr_hp_publish(&r, &s); /* frame 0: never published */
    CHECK(somvr_hp_lookup(&r, 8, &o) && o.n == 0, "frame 0 published over frame 8");
}

/* one writer publishing frames, one reader looking them up: a found set is always whole (all its nodes from its frame) */
static somvr_hp_ring g_r;
static _Atomic int g_stop;
static _Atomic uint64_t g_wf;
static void *writer(void *a)
{
    (void)a;
    for (uint64_t f = 1; f <= 200000; f++) {
        somvr_hp_set s;
        somvr_hp_set_init(&s, f);
        for (uint32_t k = 0; k < 1 + f % SOMVR_HP_MAX; k++)
            somvr_hp_set_add(&s, (uintptr_t)(f << 8 | k), (int)k);
        somvr_hp_publish(&g_r, &s);
        atomic_store(&g_wf, f);
    }
    atomic_store(&g_stop, 1);
    return NULL;
}
static void test_threads(void)
{
    somvr_hp_ring_init(&g_r);
    pthread_t t;
    pthread_create(&t, NULL, writer, NULL);
    uint64_t found = 0, torn = 0, looks = 0;
    while (!atomic_load(&g_stop)) {
        uint64_t f = atomic_load(&g_wf);
        for (uint64_t d = 0; d < 3 && f > d; d++) {
            somvr_hp_set o;
            looks++;
            if (!somvr_hp_lookup(&g_r, f - d, &o))
                continue;
            found++;
            int ok = o.frame == f - d && o.n == 1 + (f - d) % SOMVR_HP_MAX;
            for (uint32_t k = 0; ok && k < o.n; k++)
                ok = o.node[k] == (uintptr_t)((f - d) << 8 | k) && o.slot[k] == k;
            torn += !ok;
        }
    }
    pthread_join(t, NULL);
    CHECK(torn == 0 && found > 0, "two threads: %llu looks, %llu found, %llu torn", (unsigned long long)looks,
          (unsigned long long)found, (unsigned long long)torn);
}

int main(void)
{
    test_slots();
    test_set();
    test_vec_box();
    test_skip();
    test_copies();
    test_ring();
    test_threads();
    printf("test_headparts: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
