/* somvr_smooth.h - the hands' smoothing and the sword's weight (build 15b feel round, docs/re-xr-pipeline.md 48).
 * The user after build 15: "Add some smoothing to my hands ... it can look a bit jittery ... mainly when holding the
 * sword, since it amplifies how noticeable shaking is" and "Add some 'weight' to the sword".
 *   - hsm_*: an adaptive (One Euro) low-pass on each located grip, rotation and position apart: the cutoff rises with
 *     the hand's speed (the runtime's angular / linear velocity when it gives them: the gyro sees a swing's first
 *     frame; else a finite difference), so a still hand is steady and a swing is not delayed (the lag of a first-order
 *     low-pass on a ramp is tau = 1 / (2 pi fc): under 5 ms from 3 rad/s with the defaults). Pure, unit-agnostic in
 *     the space it is given (it commutes with the game's mirror). A gap, a jump, an untracked hand: reset, no catch-up.
 *   - hsm_jit_*: the measurement: per frame the second difference (the residual of a constant-velocity step) of the raw
 *     and the filtered grip, on still frames, at the hand (mm, deg) and at the blade's tip (mm), RMS and max.
 *   - swg_*: the sword's weight (build 16, Blade & Sorcery's feel, not its physics): the blade's direction follows
 *     the hand through an acceleration-limited follower (slow cuts 1:1, fast flicks trail up to a rope and catch up,
 *     quick wiggles mostly absorbed); the twist and the hand stay 1:1; stiffer with two hands.
 * Tested by tests/test_smooth.c (recorded-style noisy poses, swings; the weight: wiggles, slow and fast cuts, two hands). */
#ifndef SOMVR_SMOOTH_H
#define SOMVR_SMOOTH_H

#include <stdint.h>

#include "somvr_hands.h"

/* ---- the hands' smoothing ---------------------------------------------------------------------------------------- */
typedef struct {
    float rot_min_hz, rot_beta; /* the rotation's cutoff = min + beta x |w| (Hz, Hz per rad/s) */
    float pos_min_hz, pos_beta; /* the position's = min + beta x |v| (Hz, Hz per m/s) */
    float d_hz;                 /* the finite-difference speed's own low-pass (no runtime velocity) */
    float reset_m, reset_rad;   /* a raw pose this far off the filtered one = a reset (a jump, tracking regained) */
    float max_gap_s;            /* a longer step = a reset */
} hsm_cfg;

#define HSM_ROT_MIN_HZ 1.5f
#define HSM_ROT_BETA 12.0f
#define HSM_POS_MIN_HZ 4.0f
#define HSM_POS_BETA 40.0f

typedef struct {
    uint64_t n;                     /* still frames measured */
    double raw_pos2, raw_rot2, raw_tip2; /* sums of squares: m, rad, m */
    double out_pos2, out_rot2, out_tip2;
    float raw_tip_max, out_tip_max; /* m */
} hsm_jit;

typedef struct {
    int have;
    int64_t t;          /* the last filtered input's time */
    float q[4], p[3];   /* the filtered pose */
    float rq[4], rp[3]; /* the last raw pose */
    float ws, vs;       /* the speeds the cutoffs used (rad/s, m/s) */
    /* the measurement: the last two raw and filtered poses and their step */
    int hist;
    int64_t dt1;
    float hq[2][4], hp[2][3], fq[2][4], fp[2][3];
    hsm_jit jit;
    uint64_t steps, resets, same_time;
} hsm_state;

void hsm_defaults(hsm_cfg *c);
/* "MIN,BETA" -> two positive floats (MIN 0.05..60 Hz, BETA 0..1000); 1 = ok */
int hsm_parse_pair(const char *s, float *min_hz, float *beta);
void hsm_reset(hsm_state *s);
/* The hand's pose in place at its time (velocities kept). on = 0: measured only (the pose untouched). The blade for the
 * tip numbers: tip_m metres along the grip's -Z. 1 = filtered this call. */
int hsm_step(hsm_state *s, const hsm_cfg *c, int on, float tip_m, somvr_hand *h);
/* the time constant (s) at a speed (rad/s or m/s): 1 / (2 pi (min + beta x speed)) */
float hsm_tau(float min_hz, float beta, float speed);
/* the low-pass factor of a step dt (s) at a cutoff fc (Hz) */
float hsm_alpha(float fc, float dt);
/* small helpers (exported for the tests) */
float hsm_qangle(const float a[4], const float b[4]); /* the rotation between two unit quaternions, rad */

/* ---- the sword's weight (build 16: Blade & Sorcery's feel; docs/re-xr-pipeline.md 52) ---------------------------------
 * The blade's direction follows the hand's through an acceleration-limited follower: its angular speed tracks the
 * hand's (plus a pull back toward the hand's angle) but may grow only at a_up (a heavy blade is slow to get going) and
 * shrink at a_dn (quick to stop: no run-on past the hand, no ringing). A slow cut (under a_up) is 1:1; a fast flick
 * leaves the blade behind until the rope (from rope0 the pull hardens, at rope it is held) and it catches up in about
 * 150 ms after the stop; a quick small wiggle (its reversals need far more than a_up) moves the blade a third as much.
 * Only the swing (the blade's direction) lags: the twist about the blade's own axis (the edge's turn) is the hand's,
 * and the hand itself is never moved. Both hands on the hilt: stiffer (the limits x two_k^2, the loop x two_k, the
 * ropes / two_k), near 1:1. */
enum { SWG_OFF = 0, SWG_LIGHT = 1, SWG_HEAVY = 2 };

typedef struct {
    float a_up, a_dn;     /* rad/s^2: the blade's angular speed grows / shrinks at most this fast */
    float omega, zeta;    /* the follow loop: the speed target = the hand's + omega / (2 zeta) x the angle off, reached
                           * at the rate 2 zeta omega (1/s) within the limits */
    float w_max;          /* rad/s: the blade's angular speed cap (both hands: x 1.5) */
    float rope0, rope;    /* rad: from rope0 off the hand the speeding-up limit grows (to 4 a_dn at rope); rope = the
                           * largest angle off the hand */
    float two_k;          /* both hands on the hilt */
    float rope_close;     /* rad/s: a rope that shrinks (one hand -> two) closes at most this fast */
    float reset_rad, reset_rate; /* a step of the hand larger than this and faster (rad/s): a jump, the blade reset */
    float sub_s;          /* the integration's sub-step (s) */
} swg_cfg;

typedef struct {
    int have;
    float d[3];          /* the blade's direction (unit, the hand pose's space) */
    float wb[3];         /* its angular speed (rad/s, that space; no part about d) */
    float q[4];          /* the last hand orientation */
    float wh[3];         /* the hand's angular speed over the last step (rad/s) */
    float rope;          /* the rope now (rad) */
    float off;           /* the blade's angle off the hand after the last step (rad) */
    float peak_rad;      /* the largest off since the caller cleared it */
    uint64_t steps, roped, resets;
} swg_state;

/* level SWG_LIGHT / SWG_HEAVY (anything else: light) */
void swg_defaults(swg_cfg *c, int level);
void swg_reset(swg_state *s);
/* One frame: the hand's (grip's) orientation q (unit) and dt (s); axis = the blade's direction in the grip's own frame
 * (unit); two = both hands on the hilt. Out: the blade's rotation off the hand, a rotation vector in the hand pose's
 * space (the blade's q = exp(rot) x the hand's q, about the grip point; it has no part along the blade). 0 = none (the
 * first frame, a reset). */
int swg_step(swg_state *s, const swg_cfg *c, const float q[4], const float axis[3], float dt, int two, float rot[3]);
/* exp of a rotation vector -> a unit quaternion (x, y, z, w) */
void swg_qexp(const float r[3], float q[4]);
/* SOMVR_SWORD_WEIGHT_HAND=follow: a drawn hand's orientation turned with the blade, about its own grip point (its
 * position is not touched): out = exp(rot) x q, rot = swg_step's (no part along the blade: the twist stays the hand's) */
void swg_follow(const float rot[3], const float q[4], float out[4]);

/* ---- the bite (build 17: Blade & Sorcery's catch; docs/re-xr-pipeline.md 59) ------------------------------------------
 * A full cut that lands stops the drawn blade in the body for a moment: from the next frame the blade turns about the
 * grip so that the point of it that was in the body at the hit stays there (the hand moves on 1:1: the handle turns in
 * the fist, the blade pivots on the contact) for at most hold_s, then the extra turn eases out over ease_s (smoothstep)
 * onto the blade as it would be drawn (the hand's, with the weight's offset). The hold ends early once the extra turn
 * would pass max_rad (a fast cut tears the blade free sooner). Only the blade's pose: the caller applies exp(b) before
 * the drawn orientation; the hits read that drawn blade, so nothing is detected where nothing is drawn. One bite at a
 * time (a hit during a bite does not start another); a gap over SWB_GAP_S between steps ends it at once. Units: any
 * (the point and the grip in the same space); t in seconds. */
enum { SWB_IDLE = 0, SWB_HOLD = 1, SWB_EASE = 2 };
#define SWB_GAP_S 0.1

typedef struct {
    float hold_s;  /* the blade held at the contact at most this long (SOMVR_COMBAT_BITE_MS, 60 ms) */
    float ease_s;  /* then eased back over this (SOMVR_COMBAT_BITE_EASE_MS, 100 ms) */
    float max_rad; /* the hold ends once the blade would be this far off its drawn direction (SOMVR_COMBAT_BITE_DEG, 30) */
    int drag;      /* build 18 (SOMVR_COMBAT_BITE_DRAG): at max_rad the blade is not torn free but drags through, held that
                    * far behind (toward the held point) until hold_s, then eases: a fast cut's bite lasts its time too */
} swb_cfg;

typedef struct {
    int phase;       /* SWB_* */
    double t0, t1;   /* the bite's start, its ease's start */
    double last_t;   /* the last step */
    float l[3];      /* the held point in the drawn blade's own frame at the hit: q^-1 (P - p) */
    float b[3];      /* the extra turn after the last step (a rotation vector; 0 = none) */
    float b_rel[3];  /* at the ease's start */
    float peak;      /* rad: this bite's largest extra turn */
    int capped;      /* this bite's hold ended at max_rad */
    float held_s;    /* how long this bite held */
    uint64_t bites, refused, caps, gaps, steps;
} swb_state;

void swb_defaults(swb_cfg *c); /* 60 ms, 100 ms, 30 deg, no drag */
void swb_reset(swb_state *s);  /* idle, the counters cleared */
/* A bite at t: the grip's position p and the drawn blade's orientation q (unit) at the hit's frame, the held point P (on
 * that drawn blade, in the body). 1 = started; 0 = refused (one is on, or not finite). */
int swb_start(swb_state *s, double t, const float p[3], const float q[4], const float P[3]);
/* One frame at t (after the start): the grip p, the blade's orientation as it would be drawn without the bite qw, the
 * held point P now (the same space; the world's point mapped through this frame's rig). Out: b, the extra turn about the
 * grip (the drawn blade = exp(b) x qw; 0 when idle). Returns the phase after the step. */
int swb_step(swb_state *s, const swb_cfg *c, double t, const float p[3], const float qw[4], const float P[3], float b[3]);
/* ends a bite at once (the sword gone, a surface holds the hand, the switch off): idle, b 0 */
void swb_cancel(swb_state *s);

/* ---- build 31: the sword as a weighted body on a joint to the hand (Blade & Sorcery's; docs/re-xr-pipeline.md 134) ----
 * The sword is a rigid body - its mass, its centre of mass along the blade, its moments of inertia across and about the
 * blade - held to the controller's grip by a spring-damper in place and in turn whose force and torque are limited (the
 * hand's strength): a slow, committed swing carries it fully; a fast flick of the wrist asks more torque than the hand
 * has, so the tip cannot whip (it trails, then swings through to the hand); the hand's push off the centre of mass turns
 * it too (an arm's sweep leaves the tip behind). Both hands on the hilt: stronger and stiffer; a blow coming at Talion
 * (the guard): stronger, so a block is not slowed. The twist about the blade has almost no inertia: the edge follows the
 * wrist. Integrated in 0.5 ms sub-steps (the frame's controller motion spread over them): the same at 20 and 90 Hz.
 * The flesh: a landed cut or stab opens a wound in the struck body (a chain of capsules: the pelvis, torso, neck, head):
 * the blade is held at the wound by a stiff spring whose force is limited by the flesh - past that limit the wound gives
 * (an elastic-plastic pin): a cut's edge moves on through the body against y_cut (y_core near the spine), back out the way
 * it came against y_back, along the blade against y_slide; a stab's blade is held across (the wound pivots it and carries
 * it with the body) and slides out only when pulled along the blade harder than y_pull (in, against y_push, never past
 * the guard). The wound ends when a cut's point leaves the body (through the far side, or back out), the blade slides out
 * past its tip, the joint stretches past tear_m or turns past tear_rad (torn free), after max_s, or by the caller. A hit
 * landed before the blade reaches the body waits for it (pend_s). The wound is kept in the body's own frame (the caller's
 * origin and turn each frame), so it moves and reels with the body. Units: m, kg, s, N; the caller's space (the grips'). */
enum { SWP_W_NONE = 0, SWP_W_CUT = 1, SWP_W_STAB = 2 };
enum { SWP_END_NONE = 0, SWP_END_OUT, SWP_END_THROUGH, SWP_END_BACK, SWP_END_TEAR, SWP_END_TIME, SWP_END_CANCEL,
       SWP_END_MISS, SWP_END_N };
enum { SWP_EV_START = 1, SWP_EV_END = 2, SWP_EV_RESET = 4 };

typedef struct {
    float mass, com;            /* kg; m from the grip point along the blade to the centre of mass */
    float i_perp, i_axis;       /* kg m^2 about the centre of mass: across the blade, about it */
    float k_lin, zeta_lin, f_max; /* the grip point's spring to the controller (N/m), its damping ratio, its strongest pull */
    float k_ang, zeta_ang, t_max; /* the turn's (N m/rad, about the grip), its damping ratio, the strongest torque (N m) */
    float w_hill, hill_min;     /* the torque t_max falls with the sword's swing speed (Hill's curve) to 0 at w_hill
                                 * (rad/s), never below hill_min x t_max */
    float t_stop;               /* N m: the strongest torque braking the sword's own swing (the hand stops it harder) */
    float t_twist;              /* the strongest torque about the blade (N m) */
    float rope_rad, rope_m;     /* free (no wound): never farther off the controller than these */
    float rope_close, rope_close_m; /* rad/s, m/s: an opened rope (after a wound) closes at most this fast */
    float w_max;                /* rad/s: the safety cap of the sword's swing speed */
    float two_k, two_f;         /* both hands on the hilt: the springs x two_k, the limits x two_f, the ropes / two_f */
    float guard_k, guard_f;     /* a blow coming (the caller's guard): the same, not below both hands' */
    float sub_s;                /* the sub-step (s) */
    float reset_m, reset_rate_m, reset_rad, reset_rate_rad; /* a controller step larger and faster than these: a jump */
    float s_guard, s_tip;       /* m from the grip: the blade's first edge (the guard), its tip */
    /* the flesh */
    float k_w, zeta_w;          /* the wound's spring (N/m) and its damping ratio */
    float y_cut, y_cut_s, v_slice; /* N: a cut's edge on through the body while it slices (faster than v_slice m/s), and
                                 * to start (a slow push does not cut) */
    float core_k, y_back, y_slide; /* near the spine x core_k; back out the way it came; along the blade (N) */
    float skin_m;               /* a cut's yield grows from 0.3 x at the skin to 1 x this deep (m) */
    float stab_cut_m;           /* a stab levered this far across (the edge cutting its way out): torn free (m) */
    float stab_lever_rad;       /* a stab's blade turned this far in the body (levered): torn free */
    float y_stab_lat, y_pull, y_pull_kin, y_push; /* N: a stab across, pulled out (to start, once sliding), pushed in */
    float core_frac;            /* the spine: within this share of a link's radius from its axis */
    float tear_m, tear_rad;     /* the joint stretched / turned past these: torn free */
    float max_s, pend_s;        /* a wound at most this long; a landed hit waits at most this for the blade in the body */
    float enter_m;              /* the blade this far inside a capsule = in the body */
    float wound_k;              /* the blade in a wound: the turn's spring and limits x this (the wrist braces) */
} swp_cfg;

#define SWP_NODES 4
typedef struct {
    int n;                      /* nodes (2..SWP_NODES); links n - 1 */
    float node[SWP_NODES][3];   /* the caller's space, m */
    float r[SWP_NODES - 1];     /* each link's radius, m */
    float o[3], q[4];           /* the body's frame: the wound is kept in it (its origin and turn) */
} swp_body;

typedef struct {
    int have;
    float p[3], q[4];           /* the sword's grip pose (where the hand drawn on the hilt is) */
    float vc[3], w[3];          /* the centre of mass's velocity, the angular velocity */
    float tp[3], tq[4];         /* the controller's last pose */
    float tw_last[3];           /* the controller's angular velocity over the last frame */
    /* the wound */
    int wound, pend;            /* SWP_W_* (pend: a landed hit of that kind waits for the blade in the body) */
    double t, t0, pend_t;       /* now, the wound's start, the pending hit's */
    float pend_pen;             /* m: the deepest the blade came into the body while the hit waited (< 0: outside by) */
    float Wl[3];                /* the wound in the body's frame */
    float W0l[3];               /* where it went in (the body's frame) */
    float s;                    /* the blade at the wound (m from the grip) */
    float din[3];               /* a cut's way in (the body's frame, unit) */
    float a0l[3];               /* the blade's direction as it went in (the body's frame) */
    int link;                   /* the link it went in at */
    float sliding;              /* a stab: 1 = sliding (the kinetic pull), 0 = held */
    int cutting;                /* a cut: the edge slicing on (the kinetic yield) */
    float rope_now, rope_m_now; /* the ropes now (opened after a wound, closing) */
    swp_body body, body0;       /* this frame's body and the last's */
    int body_have;
    float nodel[SWP_NODES][3];  /* this frame's nodes in the body's frame */
    float Wt[3];                /* the wound now (the caller's space) */
    /* this frame's numbers */
    float stretch, off_rad;     /* the grip off the controller (m), the sword's turn off it (rad) */
    float depth;                /* m: a cut's edge under the surface / a stab's blade past the wound */
    float slide_v, flow_v;      /* m/s: the blade along the wound, the wound through the body */
    float f_wound;              /* N: the wound's force on the blade (the frame's largest) */
    float tip_v;                /* m/s: the tip's speed */
    float in_v;                 /* m/s: the blade point's speed against the body as it went in */
    int ev, end, end_kind;      /* SWP_EV_* this frame; the last wound's end (SWP_END_*) and kind */
    int tear_why;               /* a tear's rule: 1 the joint stretched, 2 turned, 3 a stab levered, 4 cut out sideways */
    float dbg_lever, dbg_cut;   /* a stab's tear: the blade's turn in the body (rad), the wound's slide in it (m) */
    float end_P[3];             /* where it came out (the caller's space) */
    /* the window's peaks (the caller clears them) */
    float peak_off, peak_stretch, peak_w, peak_tw, peak_tip, peak_depth;
    uint64_t steps, resets, roped, wounds, misses, ends[SWP_END_N];
} swp_state;

/* level SWG_HEAVY (Urfael: 1.3 kg, the balance a hand's width over the guard) or SWG_LIGHT (a lighter step) */
void swp_defaults(swp_cfg *c, int level);
void swp_reset(swp_state *s);
/* One frame at t (s): the controller's grip p (m), q (unit) after dt (s); axis = the blade's direction in the grip's own
 * frame (unit); two = both hands on the hilt; guard = a blow coming; body = the wounded (or pending) body this frame
 * (NULL: none - a wound without its body for over 0.15 s is cancelled). Out: the sword's grip pose (op, oq). 0 = reset:
 * the sword on the controller (the first frame, a jump, a gap). */
int swp_step(swp_state *s, const swp_cfg *c, double t, const float p[3], const float q[4], const float axis[3], float dt,
             int two, int guard, const swp_body *body, float op[3], float oq[4]);
/* A landed hit of this kind (SWP_W_CUT / _STAB) on this body: the wound opens where the blade is in the body now, or
 * when it gets there within pend_s. 1 = taken (open or pending), 0 = refused (one is on, no body, not a cut or stab) */
int swp_wound_hit(swp_state *s, const swp_cfg *c, int kind, const swp_body *body);
/* ends a wound (or a pending hit) now: why = SWP_END_* */
void swp_wound_end(swp_state *s, int why);
const char *swp_end_name(int why);
/* the closest points of the segments a0-a1 and b0-b1: their parameters (0..1) and the distance */
float swp_seg_seg(const float a0[3], const float a1[3], const float b0[3], const float b1[3], float *ua, float *ub);

#endif
