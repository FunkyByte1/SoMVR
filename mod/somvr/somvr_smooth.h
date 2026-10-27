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

#endif
