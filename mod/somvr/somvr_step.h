/* somvr_step.h - combat movement on the LOCO core (docs/re-xr-pipeline.md 23; design-locomotion.md 3.7-3.8 LO-5 and
 * LO-6, design-vr.md 3 "The assisted step" and "Evade (A)"): the assisted step (SOMVR_STEP), the dash (SOMVR_DASH)
 * and the wraith phase (SOMVR_PHASE, the user's vault idea 2). Pure: no globals, no game reads (somvr_step_game.c,
 * included by somvr.c, reads the game and feeds the LOCO core). Tested by tests/test_step.c.
 *
 * Units: world cm, cm/s, seconds, radians; game convention (somvr_pose.h): +Y up, heading h -> forward (sin h, 0, cos h).
 *
 * All three are short moves of Talion's body along the floor, started by the player at that moment (L1) and the same
 * every time (L6): a raised-cosine velocity v(t) = (D / T)(1 - cos 2 pi t / T) over T, so the velocity and the
 * acceleration start and end at 0 (L3: no jerk at either end), the peak is 2 D / T, the distance exactly D. Each tick
 * moves by s(t1) - s(t0) of the closed form (no integration error at any tick rate). The move rides in LOCO's delta
 * (somvr_loco_take_assist): one owner of the root translation; the stick stays 1:1 on top of it.
 *   - the step: a swing of the tracked blade that just started (the combat module's swing edge: the tip's mean speed past
 *     3 m/s) at an orc the player looks or swings at, beyond the ideal reach: d = clamp(dist - reach, 0, 0.5 m x
 *     strength) over 0.18 s, never closer than 0.7 m to the orc's axis (clamped every tick against the orc's position
 *     then); none when in reach, while blocking, with the stick pointing away;
 *   - the dash: A with the stick away from the orcs in a fight: 2.0 m in 0.3 s along the stick;
 *   - the phase: A with an orc in front within 3 m (the stick's direction, or the gaze with the stick at rest): through
 *     the orc's axis to 1.1 m beyond it in 0.4 s; the stock evade (vault) that A starts runs under it, so its ticks are
 *     LOCO's (the ATTACK owner bit overridden) and the game's own "collision off" (the attack's EnableCollision,
 *     Combat+0x56c) is held off; Talion's yaw is ours (he lands facing where the player looks, no 180 deg re-face of the
 *     view unless SOMVR_PHASE=reface); the wraith vision's look for its duration.
 */
#ifndef SOMVR_STEP_H
#define SOMVR_STEP_H

#include <stdint.h>

#include "somvr_pose.h"

enum { SOMVR_ST_NONE = 0, SOMVR_ST_STEP = 1, SOMVR_ST_DASH = 2, SOMVR_ST_PHASE = 3, SOMVR_ST_KINDS = 4 };
const char *somvr_step_kind_name(int kind); /* "none", "step", "dash", "phase" */

enum { SOMVR_ST_DASH_OFF = 0, SOMVR_ST_DASH_ON = 1, SOMVR_ST_DASH_BLINK = 2 };   /* SOMVR_DASH=off|on|blink */
enum { SOMVR_ST_PHASE_OFF = 0, SOMVR_ST_PHASE_ON = 1, SOMVR_ST_PHASE_REFACE = 2, /* SOMVR_PHASE=off|on|reface|blink */
       SOMVR_ST_PHASE_BLINK = 3 };

typedef struct {
    /* the assisted step */
    float step;            /* SOMVR_STEP: 0..1, the strength (x step_max_cm); 0 = off */
    float step_max_cm;     /* 50 */
    float step_s;          /* 0.18 */
    float reach_cm;        /* the ideal reach, root to the orc's axis (120) */
    float step_extra_cm;   /* candidates within reach + this (80: the design's "reach + 0.8 m") */
    float stop_cm;         /* never closer than this to the orc's axis (70) */
    float cone_cos;        /* the orc within acos of the gaze or of the tip's travel (cos 35 deg) */
    float away_cos;        /* the stick points away when cos(stick, to the orc) is below this (-0.3) */
    float stick_cms;       /* the stick's speed below this points nowhere (30 cm/s) */
    float min_cm;          /* a shorter step is none (5) */
    /* the dash */
    int dash;              /* SOMVR_ST_DASH_* */
    float dash_cm, dash_s; /* 200, 0.30 */
    float dash_stick;      /* the stick's deflection that asks for it (0.5) */
    /* the phase */
    int phase;             /* SOMVR_ST_PHASE_* */
    float phase_range_cm;  /* an orc within this of the root (300) */
    float phase_cone_cos;  /* ... and within acos of the direction (cos 35 deg) */
    float phase_behind_cm; /* the landing beyond the orc's axis (110) */
    float phase_min_cm, phase_max_cm; /* the path's length (150..450) */
    float phase_s;         /* 0.40 (LR6: match the stock vault's duration) */
    float phase_dir_stick; /* the stick's deflection that gives the direction, else the gaze (0.5) */
    /* both evades */
    float hold_s;          /* after the motion, the stock evade's ticks stay ours (the stick 1:1) at most this long (0.6) */
    float lost_s;          /* the game owning the move's ticks this long in a row ends it (0.1) */
    float block_frac;      /* the body did less than this share of the asked move ... (0.3) */
    float block_cm;        /* ... over at least this much asked: blocked, the move ends (15) */
    float blink_out_s, blink_in_s; /* the blink option: black within 0.05 s, back over 0.08 s after the motion */
} somvr_step_cfg;
void somvr_step_cfg_default(somvr_step_cfg *c);

/* the raised-cosine profile over T for D: the distance at t (0..D, clamped outside 0..T) and the velocity */
float somvr_step_pos(float D, float T, float t);
float somvr_step_vel(float D, float T, float t);

typedef struct { sv_vec3 pos; uintptr_t key; } somvr_step_orc; /* an orc: its axis (cm; y = its root), its Combat* */

/* why a pick gave nothing (0 = a move) */
enum {
    SOMVR_STW_OK = 0, SOMVR_STW_OFF, SOMVR_STW_NO_ORC, SOMVR_STW_IN_REACH, SOMVR_STW_BLOCK, SOMVR_STW_STICK_AWAY,
    SOMVR_STW_SHORT, SOMVR_STW_NO_FIGHT, SOMVR_STW_NO_STICK, SOMVR_STW_BAD, SOMVR_STW_N
};
const char *somvr_step_why_name(int why);

/* ---- the assisted step's pick: a swing just started -------------------------------------------------------------- */
typedef struct {
    sv_vec3 root;          /* Talion's root (cm) */
    float head_yaw;        /* the gaze's heading */
    sv_vec3 tip, tip_v;    /* the blade's tip and its velocity (cm, cm/s) at the swing's start */
    sv_vec3 stick_v;       /* the stick's world velocity (cm/s, y 0) */
    int blocking;          /* the player blocks (the defence's; 0 until it exists) */
    const somvr_step_orc *orc;
    int n;
} somvr_step_swing_in;
typedef struct {
    int why;               /* SOMVR_STW_* */
    int target;            /* the orc's index (-1 = none) */
    float d_cm;            /* the step (0 = none) */
    float dist_cm;         /* root to the orc's axis, horizontally */
    float cos_gaze, cos_tip; /* the orc's direction against the gaze, against the tip's travel */
    sv_vec3 dir;           /* unit, horizontal: toward the orc */
} somvr_step_pick;
/* the orc the swing is at: within reach + step_extra_cm, within the cone of the gaze or of the direction the tip travels
 * (from the tip, horizontally; a slash across the body heads at the orc only near it, so the gaze leads); the best
 * cosine, then the nearest. d = clamp(dist - reach, 0, step_max_cm x step), at most dist - stop_cm. */
int somvr_step_swing_pick(const somvr_step_cfg *c, const somvr_step_swing_in *in, somvr_step_pick *o);

/* ---- the evade's pick: A was pressed ----------------------------------------------------------------------------- */
typedef struct {
    sv_vec3 root;
    float head_yaw;
    sv_vec3 stick_dir;     /* the stick's world direction (unit, y 0; 0 = at rest) */
    float stick_mag;       /* its deflection 0..1 */
    int fight;             /* Talion in a fight (FSM 7) */
    const somvr_step_orc *orc;
    int n;
} somvr_step_evade_in;
typedef struct {
    int kind;              /* SOMVR_ST_NONE / _DASH / _PHASE */
    int why;               /* SOMVR_STW_* when none */
    int target;            /* the phase's orc (-1) */
    float D, T;            /* the path (cm) and its time (s) */
    float dist_cm;         /* the phase: root to the orc's axis */
    sv_vec3 dir;           /* unit, horizontal */
    sv_vec3 land;          /* the planned end (root + dir x D; y = the root's) */
} somvr_step_evade;
/* The direction asked: the stick's (deflected at least phase_dir_stick) or the gaze. The nearest orc within
 * phase_range_cm and phase_cone_cos of it in a fight = the phase (with SOMVR_PHASE): through its axis, D = dist + behind
 * clamped to min..max. Else the stick deflected at least dash_stick in a fight = the dash along the stick (with
 * SOMVR_DASH). Else none: the stock A as it is (the game's evade, jump or free-run). */
int somvr_step_evade_pick(const somvr_step_cfg *c, const somvr_step_evade_in *in, somvr_step_evade *o);

/* ---- build 17: the dodge (SOMVR_DODGE: A in a fight with GAMEMOVES off; the user: "a way to dodge them") ----------
 * a fixed distance, always the same: along the stick (deflected at least dash_stick), else straight away from the
 * attacker (have_att: its root), else straight back from the gaze. The direction (unit, y 0) into *dir; *src 1 the
 * stick, 2 away from the attacker, 3 back from the gaze; 0 = none (NaN inputs) */
enum { SOMVR_STD_NONE = 0, SOMVR_STD_STICK = 1, SOMVR_STD_AWAY = 2, SOMVR_STD_BACK = 3 };
int somvr_step_dodge_dir(const somvr_step_cfg *c, sv_vec3 root, float head_yaw, sv_vec3 stick_dir, float stick_mag,
                         int have_att, sv_vec3 att, sv_vec3 *dir);
const char *somvr_step_dodge_src_name(int src); /* "-", "the stick", "away from the attacker", "back from the gaze" */

/* ---- build 17: whose A's press it is - one owner per press, so one press never does two things -------------------
 * Three build 17 moves share A (the right controller's lower button): the Shadow Strike (SOMVR_STRIKE_GAZE: an orc
 * marked in the wraith vision), the dodge (SOMVR_DODGE: in a fight) and the climb's drop (SOMVR_CLIMB: A in a hang,
 * SOMVR_CLIMB_DESCENT[_MARK]: A held at an edge). Decided at the press, kept until A is let go: the strike first (its
 * mark is the player's explicit choice), then the dodge (in a fight on foot: the dodge's refusals - the cooldown, a
 * ledge, the bow - are its own double buzz, never a drop), then the climb (it decides itself whether anything goes).
 * A climb that holds is the climb's even in a fight (the dodge never runs from a hang). Not in the world (a menu's
 * A): none (the climb keeps its own guard for a press held out of a menu). */
enum { SOMVR_AO_NONE = 0, SOMVR_AO_STRIKE = 1, SOMVR_AO_DODGE = 2, SOMVR_AO_DROP = 3, SOMVR_AO_N = 4 };
typedef struct {
    int world;    /* the press is in the world (the input live, no menu) */
    int strike;   /* SOMVR_STRIKE_GAZE: an orc marked (or its strike under way) */
    int dodge;    /* SOMVR_DODGE on (with GAMEMOVES off) */
    int fight;    /* the dodge's fight: an attack at Talion tracked lately, or his FSM in Combat */
    int climbing; /* a climb holds (the hang) */
    int drop;     /* SOMVR_CLIMB on (A drops from a hang; with the descent, A at an edge) */
} somvr_step_a_in;
int somvr_step_a_owner(const somvr_step_a_in *in);
const char *somvr_step_a_owner_name(int o); /* "nobody", "the strike", "the dodge", "the climb" */

/* ---- the runner: one move at a time, once per LOCO tick ------------------------------------------------------------ */
enum { SOMVR_STE_NONE = 0, SOMVR_STE_DONE, SOMVR_STE_HELD, SOMVR_STE_HOLD_OVER, SOMVR_STE_LOST, SOMVR_STE_BLOCKED,
       SOMVR_STE_CLOSE, SOMVR_STE_NEW, SOMVR_STE_OFF, SOMVR_STE_FINISH, SOMVR_STE_N };
const char *somvr_step_end_name(int e); /* done, held (the evade ended after the motion), hold over, lost, ... */

typedef struct {
    int kind;              /* SOMVR_ST_* (NONE = idle) */
    int target;            /* the pick's orc index (logs) */
    uintptr_t key;         /* its Combat* (the step's clamp follows it) */
    float D, T, t;         /* the path, its time, the time run */
    sv_vec3 dir, start;    /* unit direction; the root at the start */
    float orc_along;       /* the phase: the orc's axis along the path from the start (the peak) */
    int peaked;            /* the phase passed the orc's axis */
    int reface;            /* the phase's re-face is due (taken once by the game side) */
    int attack_seen;       /* the stock evade (an attack) ran under the move */
    float done_cm;         /* moved by the profile so far (after the step's clamp) */
    float asked_cm, body_cm; /* asked and done by the body (the read-back), along the path */
    float lost_t;          /* the game has owned the move's ticks this long in a row */
    uint64_t ticks, owned_ticks, game_ticks, hold_ticks;
    float last_d;          /* the last tick's asked distance along the path (for the read-back) */
    int have_last;
    /* totals */
    uint64_t n[SOMVR_ST_KINDS], ends[SOMVR_STE_N];
    double cm[SOMVR_ST_KINDS];
} somvr_step_run;
void somvr_step_run_init(somvr_step_run *r);
/* a new move from a pick (an older one ends as NEW); kind NONE does nothing */
void somvr_step_start(somvr_step_run *r, const somvr_step_cfg *c, int kind, int target, uintptr_t key, float D, float T,
                      sv_vec3 dir, sv_vec3 root, float orc_along);
/* end it now with e (the off switch, a load ...); returns the kind that ran (0 = none) */
int somvr_step_stop(somvr_step_run *r, int e);

/* which of LOCO's owner bits the move overrides on this tick (before somvr_loco_why's result is used): the dash and the
 * phase own the stock evade's ticks (own_bits: SOMVR_LOCO_W_ATTACK by default) while the motion runs, then while the
 * evade still runs within hold_s; the step none (our swing is no stock attack). attack = an attack runs this tick. */
uint32_t somvr_step_mask(const somvr_step_run *r, const somvr_step_cfg *c, uint32_t own_bits, int attack);
/* the phase turns Talion to the gaze (the yaw ours) while it owns its ticks */
int somvr_step_yaw_ours(const somvr_step_run *r, const somvr_step_cfg *c, int attack);

typedef struct {
    float dt;              /* this tick (LOCO's: Talion's timer) */
    int owned;             /* LOCO owns this tick (after the mask) */
    int attack;            /* an attack runs (the stock evade) */
    int have_root;
    sv_vec3 root;          /* Talion's root before this tick */
    int have_orc;
    sv_vec3 orc;           /* the step's target now */
    sv_vec3 stick_d;       /* LOCO's own delta this tick (the stick, the follow): the evades take its part along their
                              path out (the move sets that speed: the same every time, L6), the rest steers; the step's
                              stop distance counts it */
} somvr_step_tick_in;
typedef struct {
    int active;            /* a move ran this tick */
    int kind;
    sv_vec3 d;             /* this tick's displacement (cm, world, y 0) added to LOCO's delta (the evades: less the
                              stick's forward part) */
    float dt;
    int ended;             /* SOMVR_STE_* when the move ended this tick (0 = still on or idle) */
    int peak;              /* the phase crossed the orc's axis this tick */
    int clamped;           /* the step was clamped by the stop distance this tick */
} somvr_step_tick_out;
/* one LOCO tick: the read-back of the last one (blocked?), the profile's displacement on our ticks, the step's clamp,
 * the peak, the end (the motion over and, for the evades, the stock evade over or hold_s run out) */
void somvr_step_tick(somvr_step_run *r, const somvr_step_cfg *c, const somvr_step_tick_in *in, somvr_step_tick_out *o);

/* the blink option's black (0..1) at the move's time: dash=blink or phase=blink only */
float somvr_step_blink(const somvr_step_run *r, const somvr_step_cfg *c);
/* the wraith look is wanted: the phase's motion runs */
int somvr_step_look(const somvr_step_run *r);

/* "attack,fsm,..." -> LOCO owner bits (somvr_loco_why_name's names); *bad = a word not understood */
uint32_t somvr_step_own_bits(const char *s, int *bad);

#endif
