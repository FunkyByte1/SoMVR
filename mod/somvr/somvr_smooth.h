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
 *   - swg_*: the sword's weight (Blade & Sorcery's feel, not its physics): the blade follows the hand through a
 *     critically damped spring-damper on the relative motion, rotation about the grip and a little position, each
 *     capped; it is driven only by the hand's speed above a gate, so a still or slowly moving hand has nothing (no
 *     wobble, no lag) and a fast swing trails a few degrees and settles after; stiffer with two hands.
 * Tested by tests/test_smooth.c (recorded-style noisy poses, swings, the weight's caps and gates). */
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

/* ---- the sword's weight -------------------------------------------------------------------------------------------- */
enum { SWG_OFF = 0, SWG_LIGHT = 1, SWG_HEAVY = 2 };

typedef struct {
    float omega;             /* rad/s: the spring's natural frequency (critically damped) */
    float cap_rad;           /* the blade's largest turn off the hand about the grip */
    float cap_pos;           /* the hilt's largest offset (the caller's length unit) */
    float gate_lo, gate_hi;  /* rad/s: the hand's angular speed where the weight starts / is full */
    float gate_lo_v, gate_hi_v; /* the hand's linear speed (unit/s) for the position */
    float two_k;             /* two hands on the hilt: omega x two_k, the caps / two_k */
    float reset_rad, reset_rate; /* a step of the hand larger than this and faster (rad/s): a jump, the spring reset */
} swg_cfg;

typedef struct {
    int have;
    float e[3], ve[3];  /* the blade's rotation off the hand (a rotation vector, rad, the hand's space) and its rate */
    float d[3], vd[3];  /* the hilt's offset and its rate */
    float u[3], uv[3];  /* the last gated hand velocities (angular, linear) */
    float q[4], p[3];   /* the last hand pose */
    float peak_rad, peak_pos; /* the largest offsets since the caller cleared them */
    uint64_t steps, capped, resets;
} swg_state;

/* level SWG_LIGHT / SWG_HEAVY; pos_unit = 1 for metres, 100 for cm */
void swg_defaults(swg_cfg *c, int level, float pos_unit);
void swg_reset(swg_state *s);
/* One frame: the hand's (grip's) pose q (unit), p and dt (s); two = both hands on the hilt. Out: the blade's rotation
 * off the hand (a rotation vector in the hand pose's space: blade q = exp(rot) x hand q, about the grip point) and the
 * hilt's offset (that space, the caller's unit). 0 = nothing (no offset, e.g. the first frame or a reset). */
int swg_step(swg_state *s, const swg_cfg *c, const float q[4], const float p[3], float dt, int two, float rot[3],
             float pos[3]);
/* exp of a rotation vector -> a unit quaternion (x, y, z, w) */
void swg_qexp(const float r[3], float q[4]);

#endif
