/* somvr_pose.h - pose and projection math between OpenXR and the game's SomView (docs/re-xr-pipeline.md sections 1-2).
 * Pure functions, no globals, no OpenXR include. Unit-tested by tests/test_pose.c.
 *
 * Game convention (FUN_0072b050, checked on the run-1 samples): left-handed world, +Y up, centimetres; a camera's local
 * +X = screen right, +Y = up, +Z = forward; quaternion (x, y, z, w) w last, Hamilton, v_world = R(q) v_local + pos.
 * OpenXR: right-handed, +Y up, -Z forward, metres. The two differ by the mirror M = diag(1, 1, -1).
 */
#ifndef SOMVR_POSE_H
#define SOMVR_POSE_H

#include <stdint.h>

#include "somvr_shared.h"

typedef struct { float x, y, z; } sv_vec3;
typedef struct { float x, y, z, w; } sv_quat;
/* FUN_0072b050 units: tanHalf (tx, ty) and centerOffset (cx, cy); frustum [cx-tx, cx+tx] x [cy-ty, cy+ty] in x/z, y/z */
typedef struct { float tx, ty, cx, cy; } sv_proj;

/* The SomView: FUN_00a5a820's output record, 0x2c bytes (re-view-render.md section 2; run 1) */
typedef struct {
    float pos[3];     /* cm */
    float quat[4];    /* x y z w */
    float fov_x, fov_y; /* full angles of a symmetric frustum, radians */
    float near_z, far_z;
} somvr_view;
_Static_assert(sizeof(somvr_view) == 0x2c, "SomView is 0x2c bytes");

/* quaternions (Hamilton; R(q) is the column-vector rotation matrix) */
sv_quat sv_qmul(sv_quat a, sv_quat b);
sv_quat sv_qconj(sv_quat q);
sv_quat sv_qnormalize(sv_quat q);
sv_vec3 sv_qrot(sv_quat q, sv_vec3 v);
sv_quat sv_qyaw(float a);   /* rotation by a about +Y: R·(0,0,1) = (sin a, 0, cos a) (game: + = turn right) */
float sv_heading(sv_quat q); /* atan2(f.x, f.z) of f = R(q)·(0,0,1); the twist about +Y when f is (nearly) vertical */
float sv_wrap_pi(float a);   /* to (-pi, pi] */
sv_quat sv_quat_of(const float q[4]);
void sv_quat_to(sv_quat q, float out[4]);

/* OpenXR <-> game (the mirror M, metres <-> cm) */
sv_vec3 sv_xr_to_game_pos(const float p_m[3]);  /* (100x, 100y, -100z) */
sv_quat sv_xr_to_game_quat(const float q[4]);   /* (-x, -y, z, w): R(result) = M R(q) M */
void sv_game_to_xr_pos(sv_vec3 p_cm, float out_m[3]);
void sv_game_to_xr_quat(sv_quat q, float out[4]);

/* projection */
sv_proj sv_proj_from_fov(const somvr_fovf *f);   /* tx = (tR-tL)/2, cx = (tR+tL)/2, ty, cy likewise */
somvr_fovf sv_fov_from_proj(sv_proj p);           /* the XrFovf a (tx, ty, cx, cy) frustum covers */
somvr_fovf sv_fov_cover(const somvr_fovf *f);     /* smallest symmetric fov containing f */
void sv_view_fov_cover(const somvr_fovf *f, float *fov_x, float *fov_y); /* its SomView full angles */
somvr_fovf sv_fov_of_view(float fov_x, float fov_y); /* the symmetric XrFovf of SomView full angles */
/* FOV overscan (SOMVR_OVERSCAN, docs/re-xr-pipeline.md 7.2): f widened on every side by s x its half-extent in tangent
 * units, about the frustum's own centre: tx' = (1 + s) tx, ty' = (1 + s) ty, cx, cy kept. The eye then renders a margin
 * the compositor can reproject into; the projection layer must carry the widened fov. s <= 0 or NaN: f unchanged
 * (bit for bit); s above SOMVR_OVERSCAN_MAX is capped. */
#define SOMVR_OVERSCAN_MAX 0.5f
somvr_fovf sv_fov_overscan(const somvr_fovf *f, float s);

/* FNV-1a over the 28 bytes pos[3] + quat[4] (spike1's xf_hash: H1 output = H2 input = H3 input in run 1) */
uint32_t sv_xf_hash(const void *xf28);

#endif
