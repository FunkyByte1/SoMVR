/* somvr_beasts.h - SOMVR_BEASTS: caragors, ghuls and graugs in VR (build 22; docs/re-xr-pipeline.md 92,
 * handoff/HANDOFF-beasts.md). Pure: no globals, no game reads, no OpenXR. Tested by tests/test_beasts.c; the game side
 * is somvr_beasts_game.c (included by somvr.c).
 *
 * The game's own names (the P9 probe's AI/Templates list, the P7 character records at Combat+0x4c8): a caragor is a
 * "Warg" ('Warg_Brown', 'Warg_Rare', 'Warg_Stealth', 'DLC1_Warg_Rare_TorvinMount'), a ghul a "Goblin" ('Goblin',
 * 'Goblin_Spitter', 'DLC1_Goblin_Rare'), a graug a "Troll" ('Troll_Cave', 'Troll_Poison', 'Troll_Legendary').
 *
 *   kinds     a character record's name -> caragor, ghul, graug (or none: an orc, a human, a spider)
 *   capsules  the blade's targets on a beast: its bone chain (snout -> head -> neck -> chest -> pelvis -> tail, the
 *             nodes the model has, world cm) as capsules, a radius per part and kind; the snout out past the head along
 *             the neck->head line; no chain (fewer than two nodes) = one capsule along the root's facing at the kind's
 *             body height (the orcs keep combat's upright capsule) */
#ifndef SOMVR_BEASTS_H
#define SOMVR_BEASTS_H

enum { SOMVR_BZ_NONE = 0, SOMVR_BZ_CARAGOR = 1, SOMVR_BZ_GHUL = 2, SOMVR_BZ_GRAUG = 3, SOMVR_BZ_N = 4 };

/* a character record's (or template's) name -> SOMVR_BZ_*: '*Warg*' caragor, '*Goblin*' ghul, '*Troll*' graug
 * (case-insensitive; a rider's 'AI_Warg_Ridden_...' attack names are not characters: callers pass a character record) */
int somvr_bz_kind(const char *name);
const char *somvr_bz_kind_name(int kind); /* "none", "caragor", "ghul", "graug" */

/* SOMVR_COMBAT_HOLD_TALION with SOMVR_BEASTS: a beast's sync on Talion (a caragor's takedown ran 13 s in run 235931)
 * is held this long at most (the orcs' paired syncs: 8 s) */
#define SOMVR_BZ_HOLD_MAX_S 20.0

/* ---- the bone chain ---- */
enum { SOMVR_BZP_HEAD = 0, SOMVR_BZP_NECK, SOMVR_BZP_CHEST, SOMVR_BZP_PELVIS, SOMVR_BZP_TAIL, SOMVR_BZP_N };
#define SOMVR_BZ_CAPS (SOMVR_BZP_N + 1) /* the snout, then one per link of the chain */

typedef struct {
    float r_head;  /* the head's and the snout's radius (cm) */
    float r_body;  /* the neck .. pelvis links */
    float r_tail;  /* pelvis -> tail */
    float snout;   /* the snout: this far past the head node along neck -> head (0 = none) */
    /* no chain: one capsule along the root's facing */
    float fb_up;   /* its axis this far above the root (cm) */
    float fb_back; /* from this far behind the root ... */
    float fb_front;/* ... to this far in front of it */
    float fb_r;
} somvr_bz_shape;
void somvr_bz_shape_default(int kind, somvr_bz_shape *s); /* kind none: zeros */

typedef struct {
    float c0[3], c1[3];
    float r;
    int part; /* SOMVR_BZP_* of the link's front end; -1 the snout; -2 the facing's fallback */
} somvr_bz_cap;

/* the capsules from the parts the model has (have[p] != 0: pos[p] in world cm), the root and its facing (unit, the
 * ground plane); returns the count (0..SOMVR_BZ_CAPS). Links join consecutive parts that are present (a missing chest
 * joins neck -> pelvis). Fewer than two parts: the facing's single capsule. */
int somvr_bz_capsules(const somvr_bz_shape *s, const float pos[SOMVR_BZP_N][3], const int have[SOMVR_BZP_N],
                      const float root[3], const float fwd[3], somvr_bz_cap out[SOMVR_BZ_CAPS]);

/* ---- the limbs (build 22): a graug's legs and arms - the blade reaches its legs, not its chest 2.5 m up ---- */
enum { SOMVR_BZL_R_THIGH = 0, SOMVR_BZL_L_THIGH, SOMVR_BZL_R_SHIN, SOMVR_BZL_L_SHIN, SOMVR_BZL_R_ARM, SOMVR_BZL_L_ARM,
       SOMVR_BZL_N };
#define SOMVR_BZ_CAPS_ALL (SOMVR_BZ_CAPS + SOMVR_BZL_N)
/* the limb's radius for kind (0 = the kind has none: a caragor's legs are under its body, a ghul has no nodes) */
float somvr_bz_limb_r(int kind, int limb);
/* appends the capsule a-b (part -10 - limb) when both are finite and r > 0; returns the new count */
int somvr_bz_limb(int n, somvr_bz_cap out[SOMVR_BZ_CAPS_ALL], const float a[3], const float b[3], float r, int limb);

/* ---- the blow for DEFENCE (build 22): what of a beast meets the blade - a caragor's jaws (the neck -> the snout's
 * tip), a graug's fists (the forearm -> the hand, each); returns the count (0..2) ---- */
int somvr_bz_blows(int kind, const somvr_bz_shape *s, const float head[3], const float neck[3], int have_jaws,
                   const float arm[2][3], const float hand[2][3], const int have_fist[2], float base[2][3], float tip[2][3]);

#endif
