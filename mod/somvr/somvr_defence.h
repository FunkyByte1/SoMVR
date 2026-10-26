/* somvr_defence.h - SOMVR_DEFENCE=1: block, parry and dodge judged by the sword's pose (docs/re-xr-pipeline.md 22;
 * design-vr.md 3 "Defence: blocks, counters, dodges", the ranked plan's item 8 = design-hla.md HG-3's defence part).
 * The user's combat decision "assisted physical": orcs take turns from the front with telegraphs (SOMVR_THREAT) and
 * you parry by putting the sword in the way. Pure, C11, no globals, no game calls; tested by tests/test_defence.c.
 *
 * One attack at Talion (SOMVR_THREAT's track: its start, the key times Combat+0xc8..) is followed frame by frame:
 *   - the guard: the blow's line runs from the attacker's weapon hand (R_Hand) to the nearest point of Talion's body
 *     (a segment below the HMD head); the blade (base -> tip) is IN THE WAY when it passes within way_cm of that line,
 *     in front of the body, at least cross_deg across it (a blade pointing along the blow stops nothing); it is ALIGNED
 *     when it also lies within align_deg of the swing plane's normal (the hand's velocity x the line: a vertical blade
 *     against a side swing, a level blade against an overhead one; a thrust or a still hand: any crossing blade);
 *   - the parry: aligned and in the way inside the parry zone (the last parry_s of the game's parry window BPW..EPW,
 *     ending lead_s before EPW so the game sees the press while its window is open), the game's window byte
 *     (Combat+0x562) not shut and the attack's type counterable: fires once per attack -> the caller presses the stock
 *     counter (or gives the orc its parry recoil);
 *   - the dodge: the head moved dodge_cm sideways (across the orc's direction at the attack's start) or back_cm further
 *     from the orc since the start;
 *   - at the impact (the game's own hit, after its range/angle test) the verdict: parried (the counter did not take:
 *     blocked), in the way within grace_s (a block), out of the path within grace_s (a dodge), else the hit lands.
 * The attack's type is the game's Combat+0x55b (the stock counter's predicates: CanParry FUN_02282e40 rejects 1,
 * CanCounter FUN_02282e10 rejects 1 and 2; re-combat.md 3.3's AttackType): 0 normal, 1 cannot parry, 2 cannot counter,
 * 3 cannot block, 4 ranged. Arrows (thrown spears too) are blocked when the blade lies on their next step. */
#ifndef SOMVR_DEFENCE_H
#define SOMVR_DEFENCE_H

#include <stdint.h>

enum { SOMVR_DF_TYPE_NORMAL = 0, SOMVR_DF_TYPE_NOPARRY = 1, SOMVR_DF_TYPE_NOCOUNTER = 2, SOMVR_DF_TYPE_NOBLOCK = 3,
       SOMVR_DF_TYPE_RANGED = 4 };
/* outcomes: PARRY = the parry fired (the counter pressed); PARRY_BLOCK = parried, but the attack still reached its
 * impact (the counter did not take): stopped like a block; ARROW = a projectile blocked */
enum { SOMVR_DFO_HIT = 0, SOMVR_DFO_PARRY = 1, SOMVR_DFO_BLOCK = 2, SOMVR_DFO_PARRY_BLOCK = 3, SOMVR_DFO_DODGE = 4,
       SOMVR_DFO_ARROW = 5, SOMVR_DFO_N = 6 };

typedef struct {
    float way_cm;      /* 20: the blade within this of the blow's line = in the way (SOMVR_DEFENCE_WAY_CM) */
    float front_cm;    /* 10: the line starts this far in front of Talion's body (at most half its length) */
    float cross_deg;   /* 35: the blade at least this far off the line's direction (SOMVR_DEFENCE_CROSS_DEG) */
    float align_deg;   /* 50: parry: the blade within this of the swing plane's normal (SOMVR_DEFENCE_ALIGN_DEG) */
    float swing_cmps;  /* 100: the weapon hand's speed above which its swing plane counts */
    float reach_cm;    /* 300: no guard against a weapon hand farther than this from the head (an archer, a thrower) */
    float body_top_cm; /* 15: Talion's body, from this far below the head ... */
    float body_bot_cm; /* 70: ... to this far below it (the pelvis) */
    float parry_s;     /* 0.30: the parry zone = the last parry_s of the window (SOMVR_DEFENCE_PARRY_MS) ... */
    float lead_s;      /* 0.033: ... ending this long before EPW (the press reaches the game one input tick later) */
    float grace_s;     /* 0.05: the verdict at the impact counts the guard / the dodge of the last grace_s */
    float dodge_cm;    /* 30: the head sideways this far since the start = a dodge (0 = no sideways dodge) */
    float back_cm;     /* 40: or this much further from the orc (0 = no backward dodge) */
    float arrow_cm;    /* 12: an arrow's next step within this of the blade = blocked (0 = no arrow blocks) */
    float arrow_step_s;/* 0.025: the arrow's next step = its speed x this (a frame and a half at 60 fps) */
    float arrow_near_cm; /* 150: only arrows passing the head within this */
    int parry_on, block_on; /* 1, 1: SOMVR_DEFENCE_PARRY / _BLOCK not off */
    int red_block;     /* 0: type 1 (cannot parry: the stock red warning) only dodged; 1 = it may be blocked too */
    /* build 10, SOMVR_DEFENCE_BLOCK=contact (headset session 3: "he doesn't even swing and I just hold my sword out and it
     * blocks"): no parry at the wind-up; a block only when the orc's weapon (its R_Weapon -> weapon_tip) and our blade
     * meet inside the impact window [impact - contact_pre_s, impact + contact_post_s] (THREAT's APPLYDAMAGE time), swept
     * frame to frame; at that moment the caller stops the strike (the clank, the recoil); the verdict at the impact is a
     * block only after such a contact */
    int contact;       /* 0 (the guard against the blow's line, build 9) */
    float contact_cm;  /* 10: the blades' closest approach that counts as meeting (half of each blade's width + slack) */
    float contact_pre_s;  /* 0.25: the window opens this long before the impact (the swing's last arc, after its wind-up) */
    float contact_post_s; /* 0.08: ... and closes this long after it */
    float contact_cross_deg; /* 15: the blades at least this far from parallel (a blade lying along the orc's slides) */
    /* build 11 (SOMVR_DEFENCE_CONTACT_MOVE_CM, 0 = off): a contact blocks only when our blade's middle moved at least
     * this far (against the head) since the attack began: a guard moved into the blow; a sword just held still in its
     * path lets it through (headless boot-check-20261025-003131: a still guard across the chest met 11 of 13 strikes) */
    float contact_move_cm;
} somvr_df_cfg;
void somvr_df_cfg_default(somvr_df_cfg *c);
#define SOMVR_DF_SWEEP_STEPS 6

/* the game's rules by the attack's type (Combat+0x55b) */
int somvr_df_can_parry(int type);                        /* the stock counter's predicates: 0 and 3 */
int somvr_df_can_block(const somvr_df_cfg *c, int type); /* 0 and 2 (1 with red_block); never 3 or 4 */
int somvr_df_can_dodge(int type);                        /* melee: 0..3 */
const char *somvr_df_type_name(int type);
const char *somvr_df_outcome_name(int o);

/* the guard at one instant (all cm, game space, Y up) */
typedef struct {
    int valid;         /* the inputs were usable (a blade, a hand off the body, finite) */
    int in_way, aligned, swing_known;
    float dist_cm;     /* the blade's distance from the blow's line */
    float cross_deg;   /* the blade's angle to the line's direction (0 along it, 90 across) */
    float align_deg;   /* the blade's angle to the swing plane's normal (0 = across the swing); -1 = unknown */
    float point[3];    /* the blade's point nearest the line (the contact) */
    float target[3];   /* the blow's target on Talion's body */
} somvr_df_guard;
/* blade base/tip, the attacker's weapon hand and its velocity (cm/s; NULL = unknown), Talion's head */
somvr_df_guard somvr_df_guard_eval(const somvr_df_cfg *c, const float base[3], const float tip[3], const float hand[3],
                                   const float hand_v[3], const float head[3]);

/* the parry zone at ts s after the start: [max(bpw, epw - parry_s), epw - lead_s]; none without a window */
int somvr_df_parry_zone(const somvr_df_cfg *c, float bpw, float epw, float ts);

/* one frame's inputs for one attack */
typedef struct {
    float ts;          /* s since the attack's start (SOMVR_THREAT's clock) */
    float bpw, epw;    /* the parry window, s from the start; < 0 = none */
    int window;        /* the game's window byte Combat+0x562: 1 open, 0 shut, -1 unknown (keys only) */
    int type;          /* Combat+0x55b */
    int blade;         /* 1 = base/tip hold this frame's blade */
    float base[3], tip[3];
    float hand[3];     /* the attacker's weapon hand */
    float orc[3];      /* the attacker's root */
    float head[3];     /* Talion's head (the HMD) */
    float moved[3];    /* the game's own move of Talion since the last step (cm; a knockback, a combat move, a sync's
                        * re-seat: not the player's), carried into the start so it is no dodge; 0 = none */
    /* build 10 (contact): the attack's impact (s from its start, < 0 = none) and the orc's weapons this frame (base =
     * the weapon's model object at R_Weapon, tip = its weapon_tip node; a berserker has two) */
    float impact;
    int norc;
    float ob[2][3], ot[2][3];
    /* build 11 (contact_move_cm): the sword hand's grip in the tracking space (cm, sc_grip; a snap turn or the stick does not
     * move it); have_grip 0 = the blade's middle against the head (cm) instead */
    float grip[3];
    int have_grip;
} somvr_df_in;

/* one attack, followed from its first step */
typedef struct {
    int started;
    double t0;
    float head0[3], side0[3]; /* the head at the start; the horizontal axis across the orc's direction */
    float dist0;       /* the head's horizontal distance from the orc at the start */
    float orc0[3];     /* the orc's root at the start: "back" is measured from there (its own steps are no dodge) */
    float moved_cm;    /* the game's moves of Talion carried into the start so far (horizontal cm, logs) */
    float hand[3], hand_v[3];
    double hand_t;
    int have_hand, have_v;
    double way_t;      /* the last step in the way (< 0: never) */
    double dodge_t;    /* the last step out of the blow's path (< 0: never) */
    float lateral_cm, back_cm; /* the head's displacement at the last step */
    int parried;       /* the parry fired (once per attack) */
    double parry_t;
    somvr_df_guard g;  /* the last step's guard */
    /* build 10 (contact): the last frame's blades, the contact, the closest approach inside the window (logs) */
    int have_prev, prev_norc;
    float pb[3], pt[3], pob[2][3], pot[2][3];
    double contact_t;  /* < 0: none */
    float contact_cm, contact_cross, contact_ts, contact_pt[3];
    int windowed;      /* frames judged inside the window */
    float best_cm, best_ts, best_cross; /* the closest approach in the window (a near miss), its time and angle */
    /* build 11 (contact_move_cm): our blade's middle against the head at the attack's first frame, how far it moved
     * since; a contact refused for a still guard (1) and its distance */
    int have_m0, still;
    float m0[3], blade_cm, still_cm;
} somvr_df_track;
void somvr_df_track_init(somvr_df_track *tr);
/* one frame at time t: the guard, the hand's velocity, the dodge state; 1 = the parry fires now, 2 (contact) = the
 * blades meet now (the block: the caller's clank and recoil) */
int somvr_df_step(const somvr_df_cfg *c, somvr_df_track *tr, double t, const somvr_df_in *in);
/* contact: the impact window at ts (1 = inside; no impact: never) */
int somvr_df_contact_window(const somvr_df_cfg *c, float impact, float ts);
/* contact: two moving segments (our blade b0-t0 -> b1-t1, the orc's o0-p0 -> o1-p1 over one frame; have_prev 0 = only
 * the current poses), their closest approach over `steps` linear sub-steps: the distance (cm), the angle between them
 * there (deg, 0..90) and the point on our blade; returns the distance */
float somvr_df_sweep(const float b0[3], const float t0[3], const float b1[3], const float t1[3], const float o0[3],
                     const float p0[3], const float o1[3], const float p1[3], int have_prev, int steps, float *cross_deg,
                     float point[3]);
/* the outcome when the game's hit comes at time t: PARRY_BLOCK, BLOCK, DODGE or HIT */
int somvr_df_verdict(const somvr_df_cfg *c, const somvr_df_track *tr, double t, int type);

/* the stock counter's press: one command held for `ticks` input ticks; a new press waits cool_s; a press whose ticks
 * have not come within max_s of its start (the input not live: a menu, a lost tracker) is dropped, never late */
typedef struct {
    int cmd, left;
    double t0, next_ok;
    uint64_t presses, refused, stale;
} somvr_df_press;
int somvr_df_press_start(somvr_df_press *p, int cmd, int ticks, double t, float cool_s); /* 1 = started */
int somvr_df_press_tick(somvr_df_press *p, double t, float max_s); /* the command to hold this input tick (0 = none) */

/* an arrow (pos cm, dir unit, speed cm/s) coming at the head: its next step against the blade now (b1-t1) and last
 * frame (b0-t0, have_prev; their midpoint too); 1 = blocked (*dist_cm, point = the blade's point) */
int somvr_df_arrow(const somvr_df_cfg *c, const float pos[3], const float dir[3], float speed, const float head[3],
                   const float b1[3], const float t1[3], const float b0[3], const float t0[3], int have_prev,
                   float *dist_cm, float point[3]);

/* the pulse for an outcome on each hand (sword = the main hand's index, 0 or 1), amplitudes scaled by scale (0..1) */
typedef struct {
    float amp[2], dur_s[2];
} somvr_df_haptic;
somvr_df_haptic somvr_df_haptic_of(int outcome, int sword, float scale);

/* ---- the parry's clank: SOMVR_DEFENCE_CLANK (build 8 candidate, docs/re-xr-pipeline.md 30.5) -------------------------
 * Build 7's recoil parry only skipped the hit: the game refused the orc's recoil during its strike, so the orc swung
 * on through the blade (boot-check-20261024-031532: 16 recoils, none took). The parts (a comma list; "on"/"all" = every
 * part, "off"/"0" = none, the default):
 *   recoil  the orc's recoil with an attack its strike lets interrupt it (somvr_df_pick_interrupter below);
 *   late    a recoil refused at the parry is tried again when the parried attack ends (the next one may allow it);
 *   stop    the orc's hit-stop at the parry (its own timer frozen SOMVR_DEFENCE_CLANK_MS, as a heavy hit's);
 *   haptic  the clank pulse instead of the parry's: a crack in the sword hand and its ring, a knock in the other;
 *   fx      the game's spark FX on the sword (SOMVR_DEFENCE_CLANK_FX, a ClientFX file) */
enum { SOMVR_DFC_RECOIL = 1u, SOMVR_DFC_LATE = 2u, SOMVR_DFC_STOP = 4u, SOMVR_DFC_HAPTIC = 8u, SOMVR_DFC_FX = 16u,
       SOMVR_DFC_ALL = 31u };
/* the switch's value -> *bits; 1 = understood (blanks around the words ignored, case-insensitive; NULL or "" = none) */
int somvr_df_clank_parse(const char *v, unsigned *bits);
/* the parts' words ("recoil,stop,..."; "off" for none) */
const char *somvr_df_clank_str(unsigned bits, char *out, unsigned cap);

/* The recoil's permission, FUN_0280fe60(the orc's Combat, attacker, attack, 0, ...) read from the game (docs/re-xr-
 * pipeline.md 30.5): by the orc's current attack's AttackData. CanInterrupt (field 23) absent or 1: any attack may
 * interrupt it unless one of its categories (Combat/Attack field 5) is in CanInterruptExceptions (26); CanInterrupt 0:
 * only an attack with a category in CannotInterruptExceptions (27) may (or an attacker with the buff of
 * CannotInterruptExceptionsTargetBuff, 28: not modelled). Each list = a record whose field 0 lists the categories. */
#define SOMVR_DF_IR_CATS 16
typedef struct {
    int known;      /* the orc's attack and its AttackData were read */
    int can;        /* CanInterrupt: 1 (also when absent), 0 */
    int nexc;       /* the categories of the list that applies (can 1: those that cannot; can 0: those that can) */
    uintptr_t exc[SOMVR_DF_IR_CATS];
    int buff;       /* can 0: CannotInterruptExceptionsTargetBuff is set (an attacker's buff could still allow it) */
} somvr_df_irules;
/* 1 = an attack with the categories cats[0..n) may interrupt (an unknown rule set: 1, the game decides) */
int somvr_df_may_interrupt(const somvr_df_irules *r, const uintptr_t *cats, int n);
/* the attack for the recoil: the first candidate that may interrupt (cand_cats[i][0..cand_n[i]) its categories); the
 * light record (index 0 by convention) first. -1 = none may */
int somvr_df_pick_interrupter(const somvr_df_irules *r, const uintptr_t *const *cand_cats, const int *cand_n, int n);

/* the clank's pulses, scaled (0..1): the sword hand's crack (flat) and its ring (a ramp down to 0), the other hand's
 * knock */
typedef struct {
    float crack_amp, crack_s, ring_amp, ring_s; /* the sword hand */
    float knock_amp, knock_s;                   /* the other hand */
} somvr_df_clank_haptic;
somvr_df_clank_haptic somvr_df_clank_haptic_of(float scale);

#endif
