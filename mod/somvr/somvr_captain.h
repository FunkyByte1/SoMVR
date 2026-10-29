/* somvr_captain.h - SOMVR_CAPTAIN: a Nemesis captain in VR, the pure part (docs/re-xr-pipeline.md 88,
 * handoff/HANDOFF-captain.md). No globals, no game reads; tested by tests/test_captain.c. The game side is
 * somvr_captain_game.c (included by somvr.c): the save's own captains brought to the practice meadow, the captain's
 * introduction in the world, his traits' rules for our finishers, his fate logged.
 *
 *   traits   a faction member's trait record names -> the rule per finisher kind (combat = the executions, stealth, ranged)
 *            and the grab's (GrabKnockdown): Immune > Vulnerable > Resistant / DamagedBy > none
 *   pick     the save's hierarchy -> the next captain the practice may bring (in the hierarchy, not a warchief, not in the
 *            world, not dead, with a template), cycling through them press by press
 *   intro    a captain's presentation plays in the world (the player's own view) when the captain is within range of the
 *            head and within half_deg of where it looks, else in the window (the theatre) as before */
#ifndef SOMVR_CAPTAIN_H
#define SOMVR_CAPTAIN_H

#include <stdint.h>

/* ---- traits ---- */
enum { SOMVR_CPF_COMBAT, SOMVR_CPF_STEALTH, SOMVR_CPF_RANGED, SOMVR_CPF_GRAB, SOMVR_CPF_N };
enum { SOMVR_CPT_NONE, SOMVR_CPT_DAMAGEDBY, SOMVR_CPT_RESISTANT, SOMVR_CPT_VULNERABLE, SOMVR_CPT_IMMUNE };
typedef struct {
    uint8_t rule[SOMVR_CPF_N]; /* SOMVR_CPT_*: the strongest of the member's traits of that kind */
    int warchief;              /* Marker_Warchief */
    int fears, hates;          /* Fear_* / Hate_* traits (counted; their names are logged) */
    int n;                     /* traits read */
} somvr_cp_traits;
void somvr_cp_traits_init(somvr_cp_traits *t);
/* one trait record's name (e.g. "Resistant_Combat", "Immune_GrabKnockdown", "Fear_Fire") */
void somvr_cp_traits_add(somvr_cp_traits *t, const char *name);
const char *somvr_cp_kind_name(int kind); /* "combat finishers", "stealth", "ranged", "grab" */
const char *somvr_cp_rule_name(int rule); /* "-", "damaged by", "resistant", "vulnerable", "immune" */
/* our finisher of `kind` (SOMVR_CPF_COMBAT: the execution; SOMVR_CPF_STEALTH: the stealth kill) on a captain:
 * 1 = forceKill (vulnerable), 0 = the record's own damage through the game's rules (resistant, damaged by, none),
 * -1 = no finisher at all (immune: the ordinary hit) */
int somvr_cp_force_kill(const somvr_cp_traits *t, int kind);

/* ---- the pick ---- */
/* the member's flags word (+0x3a4, the Faction.Dump writer FUN_022cdd90's bits) */
#define SOMVR_CP_FL_INWORLD 0x2u      /* bound to a character in the world (FUN_022cf590) */
#define SOMVR_CP_FL_DEAD 0x4u         /* IsDead */
#define SOMVR_CP_FL_KILLEDBYPLAYER 0x8u
#define SOMVR_CP_FL_PRESENTED 0x20u   /* PresentedToPlayer */
#define SOMVR_CP_FL_MOCKDEAD 0x800000u /* IsMockDead */
typedef struct {
    uint32_t flags;
    int in_hierarchy; /* a hierarchy position (+0x88 set) */
    int warchief;
    int has_template;
} somvr_cp_member;
/* 1 = the practice may bring this one; else *why says why not */
int somvr_cp_eligible(const somvr_cp_member *m, const char **why);
/* the next eligible member after index `last` (cycling; -1 = from the first); -1 = none */
int somvr_cp_next(const somvr_cp_member *m, int n, int last);

/* ---- the intro ---- */
typedef struct {
    float range_cm; /* 1500: design-vr.md 6's "within 15 m" */
    float half_deg; /* 60: "within 60 deg of the view" */
} somvr_cp_intro_cfg;
void somvr_cp_intro_default(somvr_cp_intro_cfg *c);
/* the captain at `cap` (his root) seen from the head (pos, forward unit vector): 1 = in the world; *dist_cm the
 * distance head -> his chest (root + 120 cm), *off_deg the angle between the view and him */
int somvr_cp_intro_world(const somvr_cp_intro_cfg *c, const float head[3], const float fwd[3], const float cap[3],
                         float *dist_cm, float *off_deg);

/* ---- build 24: our sword's damage on a captain (SOMVR_CAPTAIN_DAMAGE=cut) ---- */
/* the factor on the game's own damage for one of our hits: a committed cut or stab (committed = 1) cut_x x its k (the
 * contact speed's 0.6..1, clamped to 0..1; not finite = 1), never under 1; a light hit 1 */
float somvr_cp_cut_factor(int committed, float k, float cut_x);

#endif
