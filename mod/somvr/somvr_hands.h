/* somvr_hands.h - the motion controllers' grips on the game thread (docs/re-xr-input.md 1.5, 4.1-4.2): xrLocateSpace
 * through the spaces the layer published in somvr_input, at the eye views' display time, inside the core guard and only
 * while xr.ready; the layer's snapshot (moved to that time with its velocities) as the fallback, or the only source with
 * SOMVR_HANDS=layer. Plus the conversions: the OpenXR -> game mirror for poses and velocities, and the frame's rig
 * (somvr_eyes.h) from tracking space to the world, exactly as the eyes are mapped. Tested against a mock xrLocateSpace
 * by tests/test_hands.c. */
#ifndef SOMVR_HANDS_H
#define SOMVR_HANDS_H

#include "somvr_eyes.h"
#include "somvr_input.h"

enum { SOMVR_HANDS_GAME = 0, SOMVR_HANDS_LAYER = 1 };                        /* SOMVR_HANDS=game|layer */
enum { SOMVR_HSRC_NONE = 0, SOMVR_HSRC_LOCATED = 1, SOMVR_HSRC_SNAPSHOT = 2 }; /* somvr_hand.src */

typedef struct {
    int valid;            /* orientation and position usable */
    int src;              /* SOMVR_HSRC_*: this frame's locate, or the snapshot moved to `time` */
    int tracked;          /* both TRACKED bits (a pose can be valid while the runtime only infers it) */
    int vel_valid;        /* bit 0 linear, bit 1 angular */
    uint32_t flags;       /* XrSpaceLocationFlags */
    int result;           /* XrResult of the game-thread locate (0 = success or not called) */
    int64_t time;         /* the XrTime it is for */
    somvr_posef pose;     /* XR space = somvr_shared.xr.space (the eye records' space), metres */
    float lin[3], ang[3]; /* m/s, rad/s, XR space */
} somvr_hand;

typedef struct {
    uint64_t calls, located, fail, snapshot, none, notready, guard;
    double last_us; /* the last game-thread locate of both hands, microseconds */
} somvr_hands_state;

/* Both grips at `time` (XrTime: the frame's eye views' time). mode GAME: xrLocateSpace(grip_space[h], xr.space, time)
 * with an XrSpaceVelocity chained, inside somvr_xr_enter/leave (the core guard), only with xr.ready (acquire) and a
 * published locate_space; a hand that fails (or not ready, or the session not RUNNING) comes from the snapshot `snap`
 * (may be NULL) moved to `time` (somvr_hand_extrapolate, at most max_dt ns). mode LAYER: the snapshot only, no runtime
 * call. Bumps in->n_hand_locates / n_hand_fail (the game thread's counters). */
void somvr_hands_locate(somvr_shared *sh, somvr_input_shared *in, const somvr_input_state *snap, int mode, int64_t time,
                        int64_t max_dt, somvr_hands_state *st, somvr_hand out[2]);

/* a snapshot hand moved from the snapshot's time `from` to `to`: p += v dt, q = exp(w dt / 2) (x) q (w in the base
 * space); 1 = valid (the pose action active, orientation + position valid, |dt| <= max_dt) */
int somvr_hand_extrapolate(const somvr_hand_in *hi, int64_t from, int64_t to, int64_t max_dt, somvr_hand *out);

/* game convention, mirrored tracking space: m(p) = 100 (x, y, -z) cm, m(q) = (-x, -y, z, w), m(v) = 100 (vx, vy, -vz)
 * cm/s, w' = (-wx, -wy, wz) rad/s (an axial vector: det(M) M w). Any output may be NULL. */
void somvr_hand_game(const somvr_hand *h, sv_vec3 *p, sv_quat *q, sv_vec3 *v, sv_vec3 *w);

/* tracking space (game convention) -> the world with the frame's rig, as somvr_rig_eye maps the eyes:
 * p_w = B + s R(Y(psi - yaw0)) (p - P0), q_w = Y(psi - yaw0) (x) q; a vector (a velocity) is rotated and scaled by s
 * (s = somvr_rig_scale: the player size, docs/re-xr-pipeline.md 35) */
sv_vec3 somvr_rig_point(const somvr_rig *r, somvr_base b, sv_vec3 p);
sv_quat somvr_rig_quat(const somvr_rig *r, somvr_base b, sv_quat q);
sv_vec3 somvr_rig_vec(const somvr_rig *r, somvr_base b, sv_vec3 v);

#endif
