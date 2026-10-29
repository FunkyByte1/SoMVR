/* test_captain.c - unit tests of somvr_captain.c: the traits' rules on the save's real trait sets (P9's Faction.Dump),
 * the finishers' kill rule, the pick (the hierarchy, warchiefs, the dead, the ones in the world, cycling), the intro's
 * in-the-world test (docs/re-xr-pipeline.md 88) */
#include "../somvr_captain.h"

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

static void add_all(somvr_cp_traits *t, const char *const *names)
{
    somvr_cp_traits_init(t);
    for (; *names; names++)
        somvr_cp_traits_add(t, *names);
}

static void test_traits(void)
{
    /* Ghâm the Hook (P9's dump, row 1 column 3): the common set */
    static const char *const gham[] = { "Resistant_Combat", "Marker_In_Hierarchy", "Block_Immune_Combat",
                                        "DamagedBy_Combat", "Resistant_GrabKnockdown", "Immune_Ranged",
                                        "Counters_Your_Counter_1", "Jump_Attack", "AntiJumpOver", "Fear_Fire", NULL };
    somvr_cp_traits t;
    add_all(&t, gham);
    CHECK(t.n == 10 && t.rule[SOMVR_CPF_COMBAT] == SOMVR_CPT_RESISTANT && t.rule[SOMVR_CPF_RANGED] == SOMVR_CPT_IMMUNE &&
              t.rule[SOMVR_CPF_GRAB] == SOMVR_CPT_RESISTANT && t.rule[SOMVR_CPF_STEALTH] == SOMVR_CPT_NONE &&
              !t.warchief && t.fears == 1 && t.hates == 0,
          "Gham: combat %d ranged %d grab %d stealth %d (Block_Immune_Combat is no rule)", t.rule[0], t.rule[2], t.rule[3],
          t.rule[1]);
    CHECK(somvr_cp_force_kill(&t, SOMVR_CPF_COMBAT) == 0 && somvr_cp_force_kill(&t, SOMVR_CPF_STEALTH) == 0 &&
              somvr_cp_force_kill(&t, SOMVR_CPF_RANGED) == -1,
          "Gham: the execution and the stealth kill deal the record's damage, not a kill");
    static const char *const vuln[] = { "Vulnerable_Combat", "Immune_Stealth", "Quirk_HasGang", NULL };
    add_all(&t, vuln);
    CHECK(somvr_cp_force_kill(&t, SOMVR_CPF_COMBAT) == 1 && somvr_cp_force_kill(&t, SOMVR_CPF_STEALTH) == -1,
          "vulnerable to combat finishers: the execution kills; immune to stealth: no stealth kill");
    static const char *const both[] = { "Resistant_Stealth", "Vulnerable_Stealth", "Immune_Stealth", "Marker_Warchief",
                                        "Immune_GrabKnockdown", "Hate_Trolls", NULL };
    add_all(&t, both);
    CHECK(t.rule[SOMVR_CPF_STEALTH] == SOMVR_CPT_IMMUNE && t.warchief && t.rule[SOMVR_CPF_GRAB] == SOMVR_CPT_IMMUNE &&
              t.hates == 1,
          "immune wins over vulnerable and resistant; the warchief marker");
    CHECK(somvr_cp_force_kill(NULL, SOMVR_CPF_COMBAT) == 0 && somvr_cp_force_kill(&t, 9) == 0 &&
              !strcmp(somvr_cp_rule_name(SOMVR_CPT_DAMAGEDBY), "damaged by") &&
              !strcmp(somvr_cp_kind_name(SOMVR_CPF_COMBAT), "combat finishers") && !strcmp(somvr_cp_rule_name(9), "?"),
          "no traits, an unknown kind: the record's own; the names");
    somvr_cp_traits_add(&t, NULL);
    somvr_cp_traits_add(&t, "");
    somvr_cp_traits_add(&t, "Immune_CombatX");
    CHECK(t.n == 7 && t.rule[SOMVR_CPF_COMBAT] == SOMVR_CPT_NONE, "empty names skipped, a longer word is no rule");
}

static void test_pick(void)
{
    somvr_cp_member m[6];
    memset(m, 0, sizeof m);
    for (int i = 0; i < 6; i++)
        m[i].in_hierarchy = 1, m[i].has_template = 1;
    m[0].warchief = 1;                     /* row 0 */
    m[1].flags = SOMVR_CP_FL_DEAD;         /* killed */
    m[3].flags = SOMVR_CP_FL_INWORLD;      /* already out */
    m[4].in_hierarchy = 0;                 /* a member without a position (row 255) */
    const char *why = NULL;
    CHECK(!somvr_cp_eligible(&m[0], &why) && !strcmp(why, "a warchief (his bodyguards)") &&
              !somvr_cp_eligible(&m[1], &why) && !strcmp(why, "dead") && somvr_cp_eligible(&m[2], &why) &&
              !strcmp(why, "eligible") && !somvr_cp_eligible(&m[3], &why) && !strcmp(why, "in the world now") &&
              !somvr_cp_eligible(&m[4], &why),
          "eligible: only in the hierarchy, no warchief, alive, not out");
    CHECK(somvr_cp_next(m, 6, -1) == 2 && somvr_cp_next(m, 6, 2) == 5 && somvr_cp_next(m, 6, 5) == 2 &&
              somvr_cp_next(m, 6, 99) == 2,
          "the next: 2, 5, around to 2");
    m[2].flags = SOMVR_CP_FL_MOCKDEAD, m[5].has_template = 0;
    CHECK(somvr_cp_next(m, 6, -1) == -1 && somvr_cp_next(NULL, 3, -1) == -1 && somvr_cp_next(m, 0, -1) == -1,
          "none left: -1");
}

static void test_intro(void)
{
    somvr_cp_intro_cfg c;
    somvr_cp_intro_default(&c);
    float head[3] = { 0, 170, 0 }, fwd[3] = { 0, 0, 1 }, d = 0, off = 0;
    float ahead[3] = { 0, 50, 1000 }, side[3] = { 1000, 50, 0 }, far[3] = { 0, 50, 1600 }, behind[3] = { 0, 50, -500 };
    CHECK(somvr_cp_intro_world(&c, head, fwd, ahead, &d, &off) && fabsf(d - 1000.0f) < 0.1f && off < 1.0f,
          "10 m ahead: in the world (%.1f cm, %.1f deg)", d, off);
    CHECK(!somvr_cp_intro_world(&c, head, fwd, side, &d, &off) && fabsf(off - 90.0f) < 0.5f, "to the side: the window (%.1f)",
          off);
    CHECK(!somvr_cp_intro_world(&c, head, fwd, far, &d, &off) && d > 1500.0f, "16 m: the window");
    CHECK(!somvr_cp_intro_world(&c, head, fwd, behind, &d, &off) && off > 170.0f, "behind: the window");
    float at55[3] = { sinf(55.0f / 57.29578f) * 800.0f, 50, cosf(55.0f / 57.29578f) * 800.0f };
    float at65[3] = { sinf(65.0f / 57.29578f) * 800.0f, 50, cosf(65.0f / 57.29578f) * 800.0f };
    CHECK(somvr_cp_intro_world(&c, head, fwd, at55, &d, &off) && !somvr_cp_intro_world(&c, head, fwd, at65, &d, &off),
          "55 deg in, 65 deg out");
    float zero[3] = { 0, 0, 0 };
    CHECK(somvr_cp_intro_world(&c, head, zero, ahead, NULL, NULL), "no forward known: by the distance alone");
}

int main(void)
{
    test_traits();
    test_pick();
    test_intro();
    printf("test_captain: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
