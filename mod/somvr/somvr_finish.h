/* somvr_finish.h - VR-native finishers (SOMVR_FINISH=1, docs/re-xr-pipeline.md 24; design-hla.md SH-1 + SH-8,
 * design-vr.md's ranked plan item 12). The user's decision (2026-10-20): finishers, executions, brutalize and
 * cutscenes are "VR-native": the player does the motion, the orc plays its animation, the view is never dragged.
 *
 * This is the pure part (C11, no globals, no game calls; somvr_finish_game.c owns the game side): the orc's zones
 * (neck, upper back, a downed orc's torso), "from behind", the gesture judges (an execution's deliberate, fast,
 * committed cut through the neck; a ground execution's downward stab into a downed orc; a stealth kill's dagger stab or
 * slash at the neck or upper back from behind), the ready glow's state, the latched hand (a grip on the orc's neck,
 * then a held trigger = the drain, a held upper button = the interrogation), the classes of the game's own attacks by
 * the names of their node and record (what the first boot's candidate log confirms), the pick among the game's
 * candidates, and the claim (the orc is the finisher's: no other hit lands on it while the moment plays).
 *
 * Units as somvr_combat.h: game space (cm, +Y up, heading + = turn right: R(yaw)(0,0,1) = (sin yaw, 0, cos yaw)),
 * velocities cm/s, speeds in the configuration m/s like the design's, times s (CLOCK_MONOTONIC). The blade and its
 * speed state are somvr_combat.h's (somvr_blade, sc_speed): the sword's is the combat module's own; the dagger's is a
 * second sc_speed fed from the left grip. Tested by tests/test_finish.c. */
#ifndef SOMVR_FINISH_H
#define SOMVR_FINISH_H

#include "somvr_combat.h"

#include <stdint.h>

/* ---- the classes of the game's finisher attacks ------------------------------------------------------------------ */
/* in the order the names are tested (sf_class_of): brutalize's records contain "Stealth", ground executions "Exec" */
enum { SF_CLS_NONE = 0, SF_CLS_BRUTAL, SF_CLS_STEALTH, SF_CLS_GROUND, SF_CLS_EXEC, SF_CLS_BRAND, SF_CLS_DRAIN,
       SF_CLS_INTERROGATE, SF_CLS_N };
const char *sf_class_name(int cls); /* "exec" ... ("none" for SF_CLS_NONE, "?" outside) */
int sf_class_find(const char *name); /* "exec" -> SF_CLS_EXEC, -1 = unknown */

#define SF_PAT_MAX 8   /* patterns per class */
#define SF_PAT_LEN 40  /* bytes per pattern, with the terminator */
typedef struct {
    char pat[SF_CLS_N][SF_PAT_MAX][SF_PAT_LEN];
    int n[SF_CLS_N];
} sf_names;
/* The defaults: the families of the doc's names (re-combat-items.md 4: PC_Bip_SneakInstaKill_*, PC_Bip_Sneak_
 * InstantKill_*, PC_Bip_StealthTorment_*) and guesses for the families no run has named yet (executions, ground
 * executions, drains, brands, interrogations): substrings, case-insensitive. RT: the first boot's FINISH candidate
 * lines name them; SOMVR_FINISH_NAMES_<CLASS> replaces a class's list without a rebuild. */
void sf_names_default(sf_names *n);
/* class cls's patterns from a comma-separated list (blanks around a pattern dropped, empty ones skipped, at most
 * SF_PAT_MAX, each cut to SF_PAT_LEN - 1): the count; "" or "-" = none (the class never matches) */
int sf_names_set(sf_names *n, int cls, const char *csv);
/* 1 = pat occurs in s, ASCII case-insensitive (an empty pattern never does) */
int sf_contains(const char *s, const char *pat);
/* the class of a candidate: the first class (enum order) with a pattern in the attack record's name, else the first
 * with one in the node's name; NULL names are ""; SF_CLS_NONE = no pattern matched */
int sf_class_of(const sf_names *n, const char *node, const char *attack);
/* the candidate to start: the first of cls[0..n) equal to want, else the first equal to alt (SF_CLS_NONE = no alt);
 * -1 = none */
int sf_pick(const int *cls, int n, int want, int alt);

/* ---- the orc's combat state (Combat+0x268: a Combat/States/Definitions record, re-combat.md 2.2) ----------------- */
enum { SF_ST_NONE = 0, SF_ST_STUN, SF_ST_DOWN, SF_ST_OTHER };
/* from the state record's name (data: RT): "Stun" -> stunned; "KnockDown"/"Knockdown"/"KnockedDown"/"KD_"/"_KD"/
 * "Down" -> on the ground; another name -> other; NULL or "" -> none */
int sf_state_kind(const char *name);
const char *sf_state_name(int kind);

/* ---- configuration ----------------------------------------------------------------------------------------------- */
typedef struct {
    /* the zones about the orc's root, which sits at pelvis height (re-xr-pipeline.md 6.3: SOMVR_ORC_UP_CM 90 is the
     * capsule's top); the Head node replaces the estimate when the game side has it */
    float neck_up_cm;      /* 62: the neck above the root of a standing orc */
    float head_up_cm;      /* 24: the neck zone's capsule runs from the neck to this far above it (the head) */
    float back_cm;         /* 14: the upper back = the neck moved this far back along the orc's facing ... */
    float back_down_cm;    /* 16: ... and this far down */
    float torso_up_cm;     /* 18: a downed orc's torso above its root (the root lies near the ground then) */
    /* executions (SH-1) */
    int exec_streak;       /* 8: the hit streak that readies an execution (design-vr.md 3: 8, 5 with the skill; RT) */
    int exec_stunned;      /* 1: a stunned orc is ready too (the stock flurry's execution; RT whether the game offers it) */
    float exec_mps;        /* 4.5: the swing's mean tip speed when the blade reaches the neck ("fast") */
    float exec_cm;         /* 35: the tip's travel since the swing began ("committed") */
    float exec_below_cm;   /* 15: the sword meets the body this far below the neck at most ("at the head or neck") */
    float exec_above_cm;   /* 12: ... and this far above the head's top at most */
    float exec_cut_cos;    /* 0.8: |cos(tip velocity, blade axis)| at most this: a cut, not a poke */
    float exec_lead_s;     /* 0.15: the swing may have begun at most this long before the execution was ready */
    /* ground executions */
    float ground_mps;      /* 1.8: the tip's mean speed along the blade (a stab, the combat module's stab rule) */
    float ground_axis_cos; /* 0.8: cos(tip velocity, blade axis) at least this */
    float ground_down_cos; /* 0.819 = cos 35 deg: the blade within 35 deg of straight down */
    float ground_zone_cm;  /* 30: the torso sphere's radius */
    float ground_hold_mps; /* 1.0: a downward blade moving along itself this fast holds back the capsule's hit */
    /* stealth kills (SH-8): the dagger */
    float stab_mps;        /* 1.5: a stab: the dagger tip's mean speed along its blade ... */
    float stab_cos;        /* 0.7: ... with cos(velocity, blade) at least this */
    float slash_mps;       /* 2.5: or a slash: the mean tip speed */
    float stab_zone_cm;    /* 14: the neck capsule's and the back sphere's radius for the dagger */
    float back_below_cm;   /* 30: the sword's stab from behind meets the body at most this far below the upper back */
    float stab_along;      /* 0.45: the contact on the dagger's front part (0 base .. 1 tip) */
    float behind_deg;      /* 110: "from behind": the angle between the orc's facing and the direction to the player */
    /* the latched hand (drains, brands, interrogations) */
    float latch_cm;        /* 25: a grip closing within this of the neck latches (design-vr.md 3 "Grab") */
    float unlatch_cm;      /* 45: the hand this far from the neck lets go (the orc moved away, or the hand) */
    float squeeze_on;      /* 0.7 */
    float squeeze_off;     /* 0.4 */
    float trigger_on;      /* 0.6 */
    float hold_s;          /* 0.4: a trigger (drain, brand) or upper-button (interrogation) hold this long acts */
    /* the moment */
    float cooldown_s;      /* 1.0: no other finisher this soon after one */
    float claim_s;         /* 4.0: the orc is the finisher's at most this long (or until dead, or its sync ends) */
    float glow_off_s;      /* 0.25: the ready glow lingers this long after the ready state ends (no flicker) */
    int steps;             /* 4: sub-steps of a frame's sweep */
} sf_cfg;
void sf_cfg_default(sf_cfg *c);

/* ---- geometry ---------------------------------------------------------------------------------------------------- */
typedef struct {
    float neck[3], head[3]; /* the neck capsule: neck .. head */
    float back[3];          /* the upper back point */
    float torso[3];         /* a downed orc's torso point */
} sf_zones;
/* the zones of an orc: its root (cm), its heading (rad); head_node = its Head node's position or NULL (then the
 * estimate: the neck neck_up_cm above the root); down = it lies on the ground (no neck zone then: the neck and head
 * points collapse to the torso) */
void sf_zones_of(const sf_cfg *c, const float root[3], float yaw, const float *head_node, int down, sf_zones *z);
/* the angle (deg, 0..180) between the orc's facing and the horizontal direction from the orc to the player: 0 = in
 * front of it, 180 = straight behind; -1 = the player on the orc's axis (closer than 1 cm horizontally) */
float sf_behind_deg(const float orc[3], float yaw, const float player[3]);
/* the heading (rad) of a game quaternion (x y z w): the yaw of its +Z, as somvr_pose's sv_heading */
float sf_yaw_of_quat(const float q[4]);

/* ---- the judges -------------------------------------------------------------------------------------------------- */
enum { SF_WHY_OK = 0, SF_WHY_NOBLADE, SF_WHY_NOTREADY, SF_WHY_NOSWING, SF_WHY_SLOW, SF_WHY_SHORT, SF_WHY_EARLY,
       SF_WHY_POKE, SF_WHY_MISS, SF_WHY_UP, SF_WHY_NOTSTAB, SF_WHY_FRONT, SF_WHY_TIP, SF_WHY_INSIDE, SF_WHY_LOW,
       SF_WHY_HIGH, SF_WHY_N };
/* The dagger's judge counts only the frame it enters its zone (the sweep's first touching sub-step after the previous
 * pose; the previous pose already touching = SF_WHY_INSIDE): one verdict per pass, at the moment of reaching it. */
const char *sf_why_name(int why);

typedef struct {
    float u, along, dist; /* sc_contact's: the sub-step, where on the blade, the axis distance */
    float point[3];       /* the contact point on the zone's surface */
    int zone;             /* 0 the neck capsule, 1 the upper back (stabs), 2 the torso (ground) */
} sf_hit;

/* The sword's judges run at its contact with the orc's body capsule (combat_frame's sweep, the first frame a hit would
 * land): the sword always meets the body before any point inside it, so the contact itself is the gesture's moment,
 * and a finisher takes the place of that hit. point = the contact point, along = where on the blade (0 base .. 1 tip).
 *
 * An execution (SH-1): ready = the execution is ready on this orc (since ready_since); the swing is on (sp->above,
 * begun at swing_t0 >= ready_since - exec_lead_s: a swing that began well before the ready state is not this
 * deliberate one); the contact at the head or neck (point[1] from neck - exec_below_cm to head + exec_above_cm); the
 * mean tip speed >= exec_mps, the travel since the swing began >= exec_cm and a cut (|cos_axis| <= exec_cut_cos).
 * Returns SF_WHY_* (the first rule that failed, in this order). */
int sf_judge_exec_at(const sf_cfg *c, const sc_speed *sp, double swing_t0, const float point[3], const sf_zones *z,
                     int ready, double ready_since);
/* A ground execution: the orc is down. The body capsule stands up from the root whatever the orc's pose, so this one
 * is judged at its own zone: a stab (the mean speed along the blade >= ground_mps, cos >= ground_axis_cos) with the
 * blade within acos(ground_down_cos) of straight down, its front half (along >= 0.5) swept into the torso sphere
 * (radius ground_zone_cm). The entry frame only (SF_WHY_INSIDE after it). */
int sf_judge_ground(const sf_cfg *c, const sc_speed *sp, const somvr_blade *prev, const somvr_blade *cur,
                    const sf_zones *z, int down, sf_hit *out);
/* 1 = the sword is on its way down into a downed orc (pointing down within the cone, moving along the blade at least
 * hold_mps): its contact with the standing body capsule is held back so the ground judge decides at the torso */
int sf_ground_hold(const sf_cfg *c, const sc_speed *sp, const somvr_blade *b);
/* The sword's stab or slash from behind (a stealth kill when the game offers one): behind >= behind_deg, the front
 * part of the blade (along >= stab_along), the contact at most back_below_cm below the upper back, a stab or slash
 * (stab_mps along the blade with stab_cos, or slash_mps). */
int sf_judge_back_at(const sf_cfg *c, const sc_speed *sp, const float point[3], float along, const sf_zones *z,
                     float behind);
/* The dagger's stealth stab: the player behind the orc (behind >= behind_deg), the dagger's stab or slash, its front part
 * (along >= stab_along) swept into the neck capsule or the upper-back sphere (radius stab_zone_cm). */
int sf_judge_stab(const sf_cfg *c, const sc_speed *sp, const somvr_blade *prev, const somvr_blade *cur,
                  const sf_zones *z, float behind, sf_hit *out);

/* ---- the ready glow ---------------------------------------------------------------------------------------------- */
typedef struct {
    int on;         /* the glow is shown */
    double since;   /* on since (s) */
    double last;    /* the last frame the ready state held (s) */
    uint64_t ons;   /* times it came on */
} sf_glow;
void sf_glow_reset(sf_glow *g);
/* One frame: ready now or not. +1 = the glow came on (start the FX, one pulse), -1 = it went off (glow_off_s after
 * the ready state ended, or at once with force_off: a finisher started, the sword went away), 0 = no change. */
int sf_glow_step(sf_glow *g, const sf_cfg *c, int ready, int force_off, double t);

/* ---- the latched hand -------------------------------------------------------------------------------------------- */
enum { SF_LEV_NONE = 0, SF_LEV_LATCH, SF_LEV_DRAIN, SF_LEV_INTERROGATE, SF_LEV_RELEASE, SF_LEV_LOST };
const char *sf_lev_name(int ev);
typedef struct {
    int held;             /* latched */
    uintptr_t key;        /* the orc's Combat* */
    double t0;            /* latched at */
    double trig_t0;       /* the trigger held since (-1 = not) */
    double btn_t0;        /* the upper button held since (-1 = not) */
    int fired;            /* a hold acted in this latch (one action per latch) */
    int armed;            /* the grip was open (below squeeze_off) since the last latch: only a new squeeze latches */
    uint64_t latches, drains, interrogations, releases, losses;
} sf_latch;
void sf_latch_reset(sf_latch *l);
typedef struct {
    double t;
    int valid;            /* the hand is tracked */
    float hand[3];        /* the grip, game space (cm) */
    float squeeze, trigger;
    int upper;            /* the upper face button (Y on the left hand) held */
    uintptr_t key;        /* the nearest orc that may be latched (0 = none): stunned, or the game offers its grab */
    float neck[3];        /* its neck point */
    uintptr_t held_key_alive; /* while latched: the latched orc's key if it is still valid this frame, else 0 */
    float held_neck[3];   /* its neck point now */
} sf_latch_in;
/* One frame (SF_LEV_*): IDLE + a new squeeze (armed) within latch_cm of in->key's neck = LATCH; LATCHED: the grip
 * opens = RELEASE; the orc gone or the hand beyond unlatch_cm = LOST; the trigger held hold_s = DRAIN, the upper
 * button held hold_s = INTERROGATE (once per latch; the trigger first when both). */
int sf_latch_step(sf_latch *l, const sf_cfg *c, const sf_latch_in *in);

/* ---- the moment: cooldown and claim ------------------------------------------------------------------------------ */
typedef struct {
    uintptr_t key;   /* the claimed orc's Combat* (0 = none) */
    int cls;         /* what was started */
    double t0, until;
    double last_t;   /* the last start (any orc): the cooldown */
} sf_moment;
void sf_moment_reset(sf_moment *m);
int sf_moment_cool(const sf_moment *m, const sf_cfg *c, double t);   /* 1 = a finisher may start now */
void sf_moment_start(sf_moment *m, const sf_cfg *c, uintptr_t key, int cls, double t);
/* the claimed orc now (0 = none): the claim ends at `until`, or with end = 1 (the orc died, its sync ended) */
uintptr_t sf_moment_claim(sf_moment *m, double t, int end);

#endif
