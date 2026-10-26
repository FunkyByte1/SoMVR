/* somvr_loco.h - the locomotion core SOMVR_LOCO=1 (docs/design-locomotion.md 3.1-3.2, LO-2) and the root anchor
 * SOMVR_ANCHOR=root (3.4 and design-hla.md HG-1, LO-1). Pure: no globals, no game reads (somvr.c's LOCO region reads
 * the game, hooks LOCO_ROOTDELTA and the rig). Tested by tests/test_loco.c.
 *
 * Units: world cm, cm/s, seconds, radians; game convention (somvr_pose.h): +Y up, heading h -> forward (sin h, 0, cos h),
 * right (cos h, 0, -sin h).
 *
 * The core, once per game tick (the PM hook, right before PlayerMovement::vf_11):
 *   - who owns the tick (somvr_loco_why): ours only when every rule of 3.1 holds; else the game's root motion passes
 *     untouched and our velocity is kept only as the hand-off speed;
 *   - our velocity = the stick's, at once (3.2: no start clip, no stop slide; SOMVR_LOCO_STICK_MS softens it);
 *   - the delta vf_11 applies = v dt + the room-scale follow's request (one path: the follow's request rides in our delta
 *     on our ticks and never in the extra move as well);
 *   - the body yaw (the delta's rotation) on our ticks unless the yaw stays the game's;
 *   - the leg feed: the virtual stick's magnitude that plays the stock clip nearest |v|.
 * The anchor, once per stereo frame (eye 0): Talion's root + the calibrated eye height (run 3: 76.6 cm above the root =
 * 180.6 cm above World_Ground), horizontally exact, vertically low-passed (80 ms ramp lag; exact in falls and our climb);
 * velocity-continuous across the core's hand-offs (a decaying offset absorbs the body's velocity jump); extrapolated to
 * the frame's display time by the body's velocity (the frame's timing error against the running mean, + an optional
 * constant lead).
 */
#ifndef SOMVR_LOCO_H
#define SOMVR_LOCO_H

#include <stdint.h>

#include "somvr_pose.h"

/* ---- the core ------------------------------------------------------------------------------------------------------ */
enum { SOMVR_LOCO_YAW_OURS = 0, SOMVR_LOCO_YAW_GAME = 1 };     /* SOMVR_LOCO_YAW */
enum { SOMVR_LOCO_LATCH_OURS = 0, SOMVR_LOCO_LATCH_GAME = 1 }; /* SOMVR_LOCO_LATCH: the sprint latch's speed */

typedef struct {
    float walk_cms, run_cms, sprint_cms; /* the stock bands: |s| 0..0.5 -> 0..walk, 0.5..1 -> walk..run (..sprint with
                                            the latch); P10's LR1: run 230, sprint 640; walk 150 a placeholder (no stock
                                            walk band seen) */
    float speed;                         /* SOMVR_LOCO_SPEED: x the bands (0.6..1.3) */
    float stick_s;                       /* SOMVR_LOCO_STICK_MS / 1000: 0 = instant (default) */
    float aim_cms;                       /* the cap while the bow aims */
    float yaw_rate;                      /* body yaw, rad/s (720 deg/s) */
    float yaw_cone;                      /* moving within this of the head's heading: face the move (100 deg) */
    float yaw_stand;                     /* standing: turn to the head beyond this (50 deg) */
    float yaw_done;                      /* ... until within this (3 deg) */
    float move_cms;                      /* |v| above this = moving (yaw) */
    float max_dt;                        /* a tick's dt is clamped to this (0.1 s) */
    float max_step_cm;                   /* a tick's stick delta never longer (60 cm) */
    int yaw_mode;                        /* SOMVR_LOCO_YAW_* */
    int guard_ticks;                     /* SOMVR_LOCO_YAW_GUARD: standing, the game turned the body back this many
                                            ticks in a row (a rail, a perch: build 9's wall-top bug, 720 deg/s of our
                                            yaw for 31 s) = the yaw left to the game for guard_s (10; 0 off) */
    float guard_s;                       /* (1 s; then ours again, and again left if the game still holds it) */
    int latch;                           /* SOMVR_LOCO_LATCH_* */
} somvr_loco_cfg;
void somvr_loco_cfg_default(somvr_loco_cfg *c);

/* the stick's speed for magnitude m (0..1, after the dead zone) and the latch (3.2), x c->speed */
float somvr_loco_band_speed(const somvr_loco_cfg *c, float m, int sprint);
/* the stick's world velocity: magnitude m along heading h + the stick's own angle; stick = (x right, y forward) in the
 * frame of heading h (cm/s, y = 0) */
sv_vec3 somvr_loco_stick_velocity(const somvr_loco_cfg *c, const float stick[2], float h, int sprint);
/* the leg feed: the virtual stick's magnitude whose stock band speed (without c->speed) is |v| (0..1) */
float somvr_loco_leg_mag(const somvr_loco_cfg *c, float speed_cms, int sprint);

/* why the game owns a tick (bits; 0 = ours) */
enum {
    SOMVR_LOCO_W_OFF = 1u << 0,       /* SOMVR_LOCO off, its sites or hooks not in */
    SOMVR_LOCO_W_INPUT = 1u << 1,     /* the controllers not live, no stick this tick */
    SOMVR_LOCO_W_STEREO = 1u << 2,    /* no fresh head heading (flat frames) */
    SOMVR_LOCO_W_PLAYER = 1u << 3,    /* not the player's PlayerMovement, another thread */
    SOMVR_LOCO_W_READ = 1u << 4,      /* a read failed (fail-soft: the game's) */
    SOMVR_LOCO_W_FSM = 1u << 5,       /* FSM not 6 (Supported) or 7 (Combat) */
    SOMVR_LOCO_W_MOMENTUM = 1u << 6,  /* the solver is not in root-motion mode (pm+0x1a2) */
    SOMVR_LOCO_W_LOCK = 1u << 7,      /* an animation locks the translation (mgr+0x62a) */
    SOMVR_LOCO_W_ATTACK = 1u << 8,    /* an attack runs (pm+0x38 -> FUN_026d4510) */
    SOMVR_LOCO_W_COMBATMOVE = 1u << 9,/* combat owns the movement (FUN_00a85590) */
    SOMVR_LOCO_W_MOUNT = 1u << 10,
    SOMVR_LOCO_W_ATTACH = 1u << 11,   /* an attach-to-object move (pm+0x1a6) */
    SOMVR_LOCO_W_DEAD = 1u << 12,     /* not alive (P+0x4c4 != 2) */
    SOMVR_LOCO_W_CLIMB = 1u << 13,    /* SOMVR_CLIMB holds: the climb owns the motion */
    SOMVR_LOCO_W_STRUGGLE = 1u << 14,
    SOMVR_LOCO_W_SYNC = 1u << 15,     /* a sync action (the server Combat's +0x188) */
    SOMVR_LOCO_W_MENU = 1u << 16,
    SOMVR_LOCO_W_FALLBACK = 1u << 17, /* SOMVR_FALLBACK: third person, or one of its conditions */
    SOMVR_LOCO_W_FREERUN = 1u << 18,  /* the sprint latch with SOMVR_LOCO_LATCH=game: the stock free-run carries */
    SOMVR_LOCO_W_N = 19
};
const char *somvr_loco_why_name(int bit);
/* "fsm+lock" for the bits (out, cap); "ours" for 0 */
const char *somvr_loco_why_str(uint32_t why, char *out, int cap);

typedef struct {
    int on, live, stereo, player, read_ok;
    int fsm, rootmotion, lock_t, lock_r, attack, combat_move, mounted, attached, life;
    int climbing, struggle, sync, menu, fallback, sprint;
    int aiming; /* the bow aims: the velocity stays ours (capped), the yaw the game's */
} somvr_loco_own_in;
uint32_t somvr_loco_why(const somvr_loco_own_in *in, const somvr_loco_cfg *c);
/* on our ticks: the yaw stays the game's (a fight faces the engaged orc, the bow its aim, the rotation lock) */
int somvr_loco_yaw_game(const somvr_loco_own_in *in, const somvr_loco_cfg *c);

/* the follow merge (one path per tick): ours = into our delta, else the follow's own extra move (re-m1m2.md 3.2) */
enum { SOMVR_LOCO_FOLLOW_NONE = 0, SOMVR_LOCO_FOLLOW_DELTA = 1, SOMVR_LOCO_FOLLOW_EXTRA = 2 };
int somvr_loco_follow_route(int owned, int asked);

typedef struct {
    float dt;           /* this tick (s): Talion's object timer's frame time */
    uint32_t why;       /* somvr_loco_why (0 = ours) */
    int yaw_game;       /* somvr_loco_yaw_game */
    int stick_ok;
    sv_vec3 stick_v;    /* the stick's world velocity (cm/s) */
    float cap_cms;      /* > 0: |v| capped (the bow's aim) */
    int have_root;
    sv_vec3 root;       /* Talion's root before this tick (cm) */
    float yaw;          /* his heading now */
    float head_yaw;     /* the head's world heading */
    int have_btw;       /* btw_yaw: the heading's change since the last tick's vf_11 left its transform (signed, rad):
                         * what turned him outside our delta */
    float btw_yaw;
} somvr_loco_in;

typedef struct {
    int owned;
    int handoff;        /* +1 the game's -> ours, -1 ours -> the game's, 0 none */
    uint32_t why, why_prev;
    sv_vec3 v;          /* our velocity this tick (cm/s) */
    sv_vec3 delta;      /* world cm: v dt (+ the follow's request once somvr_loco_take_follow took it) */
    int follow_taken;
    sv_vec3 follow_d;
    int yaw_ours;
    int yaw_guard;      /* the yaw left to the game this tick: the game turned the body back (guard_ticks) */
    int guard_start;    /* ... from this tick on */
    float yaw_step;     /* rad about +Y (the delta's rotation), on our ticks unless yaw_game */
    float yaw_target;
    int have_applied;
    sv_vec3 applied;    /* the last tick's read-back: what the body really did (cm, horizontal) */
    sv_vec3 asked;      /* what that tick asked (cm, horizontal) */
    sv_vec3 v_hand;     /* the hand-off speed: the body's last velocity (cm/s, horizontal) */
} somvr_loco_out;

typedef struct {
    int have_root, owned_last, turning, have_v_hand;
    sv_vec3 root_last;
    float dt_last;
    sv_vec3 asked_last; /* horizontal part of the last tick's delta, when it was ours and applied */
    sv_vec3 v;          /* the velocity (softened with stick_s) */
    sv_vec3 v_hand;
    uint32_t why_last;
    uint64_t ticks, owned_ticks, handoffs_in, handoffs_out, follow_merged, follow_extra;
    float last_step;    /* our yaw step of the last tick (0: none) */
    int fight_n;        /* ticks in a row the game turned it back */
    float guard_left;   /* s the yaw stays the game's */
    uint64_t guards, guard_ticks;
    double asked_cm, applied_cm, follow_cm;
} somvr_loco;
void somvr_loco_init(somvr_loco *s);
/* one tick (before vf_11): the owner, our velocity and delta, the body yaw, the read-back of the last tick */
void somvr_loco_step(somvr_loco *s, const somvr_loco_cfg *c, const somvr_loco_in *in, somvr_loco_out *o);
/* the follow's request d (world cm) for this tick: ours = into the delta (1, and the extra must not be written), else 0 */
int somvr_loco_take_follow(somvr_loco *s, somvr_loco_out *o, sv_vec3 d);
/* an assist's displacement d (world cm; somvr_step.h: the assisted step, the dash, the wraith phase) for this tick, in
 * the same delta (one owner of the root translation): ours = added to the delta, to v (d / dt: the anchor's velocity) and
 * to what the read-back counts as asked (1); else 0 (nothing: the game's tick) */
int somvr_loco_take_assist(somvr_loco *s, somvr_loco_out *o, sv_vec3 d, float dt);
/* the tick's delta did not reach vf_11 after all (the hook did not run): the read-back skips it */
void somvr_loco_unapplied(somvr_loco *s);
/* the body yaw rule (3.2): the step toward the target this tick (rad); *turning = the standing turn's hysteresis */
float somvr_loco_yaw(const somvr_loco_cfg *c, float yaw, float head_yaw, sv_vec3 v, float dt, int *turning,
                     float *target);

/* ---- the anchor (LO-1) --------------------------------------------------------------------------------------------- */
typedef struct {
    float eye_cm;       /* the eyes above the root (run 3: 76.6) */
    float y_s;          /* the vertical filter's ramp lag (0.08 s: critically damped 3rd order, omega = 3 / y_s) */
    float blend_s;      /* the hand-off offsets' 1 / omega (0.04 s; 3rd order: a 5 m/s jump eases over ~17 cm) */
    float dip_cm;       /* the vertical filter never more than this below its target (a landing's dip, 30) */
    float jump_cm;      /* a frame's root move beyond this = a teleport: re-latched (100) */
    float gap_s;        /* frames further apart = re-latched (0.25 s) */
    float max_dt;       /* a frame's dt is clamped to this (0.1 s) */
    float lead_s;       /* a constant extrapolation lead (0: every instant stop would step back by v x lead) */
    float jit_max_s;    /* |the frame's timing error| clamp (0.025 s) */
    float age_tau_s;    /* the running mean of the frame's age (1 s) */
    float x_max_cm;     /* |extrapolation| clamp (10 cm) */
    float x_acc;        /* the extrapolation velocity's acceleration limit (6000 cm/s^2) */
    float e_max_cm;     /* |hand-off offset| clamp (50 cm) */
} somvr_anchor_cfg;
void somvr_anchor_cfg_default(somvr_anchor_cfg *c);

typedef struct {
    double t;           /* now (s, monotonic) */
    sv_vec3 root;       /* Talion's root (cm) */
    int exact_y;        /* the vertical exact this frame (a fall, our climb: ballistic or the player's own) */
    int own;            /* the core owned the last tick (our velocity is exact) */
    sv_vec3 v_own;      /* its velocity (cm/s) */
    int handoff;        /* the core's owner changed since the last frame */
    int64_t display_ns; /* the frame's display time (XrTime ns); 0 = none: no extrapolation */
    int64_t now_ns;     /* t in ns */
} somvr_anchor_in;

typedef struct {
    sv_vec3 pos;        /* the eye point (cm) */
    sv_vec3 vel;        /* its velocity (cm/s; 0 on a re-latch) */
    sv_vec3 x;          /* the extrapolation added (cm) */
    float jitter_s, y_lag;  /* the frame's timing error; the vertical filter's offset from its target (cm) */
    int latched;        /* 1 first frame / gap, 2 teleport */
    int blended;        /* a hand-off offset started this frame */
    int exact_y;
} somvr_anchor_out;

typedef struct {
    int have, have_age, exact_y, have_vr;
    double t_last;
    sv_vec3 root_last, v_root, v_root_prev[2], vx, out_last;
    float vout_y, aout_y; /* the output's vertical velocity and acceleration (the mode switches start from them) */
    float y, vy, ay;    /* filtered: the eye height (cm) and its derivatives */
    float ey, eyv, eya; /* exact: the decaying vertical offset */
    sv_vec3 e, ev, ea;  /* the horizontal hand-off offset and its derivatives (x, z) */
    int64_t age0;
    double age_mean;
    uint64_t frames, latches, teleports, blends, y_switches, x_frames;
    float jit_min, jit_max, x_cm_max, e_cm_max;
} somvr_anchor;
void somvr_anchor_init(somvr_anchor *a);
void somvr_anchor_step(somvr_anchor *a, const somvr_anchor_cfg *c, const somvr_anchor_in *in, somvr_anchor_out *o);

/* critically damped 3rd order toward 0 over dt, exact: x''' + 3w x'' + 3w^2 x' + w^3 x = 0. A velocity kick (x 0, v dv,
 * a 0) peaks at 0.839 dv / w; its acceleration starts at 0 and stays within 0.80 dv w (a 2nd order's jumps to 2 dv w
 * at once: the jerk the design avoids) */
void somvr_loco_spring3(float *x, float *v, float *a, float w, float dt);

#endif
