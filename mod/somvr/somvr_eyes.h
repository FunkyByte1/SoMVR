/* somvr_eyes.h - the rig: recentring, the base (position + yaw) and the per-eye SomView for layouts A and B
 * (docs/re-xr-pipeline.md sections 1.3, 4.5, 4.6). Pure: no globals, no OpenXR calls. Tested by tests/test_eyes.c.
 *
 *   pos  = B + s R(Y(psi - yaw0)) (m(p_eye) - P0)        quat = Y(psi - yaw0) (x) m(q_eye)
 * with m() the OpenXR -> game mirror (somvr_pose.h), (P0, yaw0) the recentre reference, B/psi the frame's base and s
 * the player size (somvr_rig.scale, 1 = 1:1; docs/re-xr-pipeline.md 35: the eye separation, the head's and the hands'
 * room motion grow with it; the eye height's part, (s - 1) x the eye height above the feet, is in B).
 */
#ifndef SOMVR_EYES_H
#define SOMVR_EYES_H

#include "somvr_pose.h"

enum { SOMVR_BASE_CAMERA = 0, SOMVR_BASE_PLAYER = 1, SOMVR_BASE_HEAD = 2 };
enum { SOMVR_YAW_CAMERA = 0, SOMVR_YAW_FIXED = 1 };

typedef struct {
    int layout;     /* SOMVR_LAYOUT_A / _B (_MONO: one full-rect eye) */
    int base;       /* SOMVR_BASE_*: the game camera's position, the player position or Talion's head node, + eye_cm */
    int yaw;        /* SOMVR_YAW_*: follow the game camera's heading, or the heading latched at recentring */
    int floor;      /* 1: keep the tracked height (STAGE space; the base is the feet) */
    float snap_rad; /* one snap step */
    float eye_cm;   /* SOMVR_BASE_PLAYER / _HEAD: added to the body point's y */
} somvr_rig_cfg;

typedef struct {
    int have_ref;
    sv_vec3 p0;        /* the head at recentring, game convention (cm, mirrored tracking space); y = 0 in floor mode */
    float yaw0;        /* its heading */
    float yaw_fixed;   /* SOMVR_YAW_FIXED: the game camera's heading at recentring */
    int32_t snap_steps;
    uint64_t recenters;
    float scale;       /* the player size: world cm per tracked cm / 100 (0 = 1; somvr_rig_scale) */
} somvr_rig;

typedef struct {
    sv_vec3 pos;  /* B, cm */
    float yaw;    /* psi, radians, game heading (+ = right) */
} somvr_base;

typedef struct {
    somvr_view view;       /* the SomView H1 writes */
    float rect[4];         /* viewport x0 y0 x1 y1 (0..1) for H2 */
    uint32_t slot;         /* present slot (layout B: the eye) */
    sv_proj proj;          /* exact tangents + offset for H3 */
    somvr_fovf fov_cover;  /* the symmetric fov the SomView describes */
    uint32_t hash;         /* sv_xf_hash(view.pos..quat) */
} somvr_eye_out;

void somvr_rig_init(somvr_rig *r);
/* the rig's player size: r->scale when it is a size (0.5 .. 2), else 1 (0, NaN, an old caller's zeroed field) */
float somvr_rig_scale(const somvr_rig *r);
/* the head pose from the two eye poses (midpoint, eye orientations averaged) in game convention */
void somvr_head_game(const somvr_posef eye[2], sv_vec3 *pos, sv_quat *quat);
/* Per frame, before the eyes: recentre if asked or if there is no reference yet, add snap steps, return the base.
 * body_pos (cm) is the player position (SOMVR_BASE_PLAYER) or the head node (SOMVR_BASE_HEAD); NULL = the camera
 * position for this frame. */
somvr_base somvr_rig_frame(somvr_rig *r, const somvr_rig_cfg *c, const somvr_posef eye[2], const somvr_view *game,
                           const float *body_pos, int want_recenter, int32_t snap_delta);
/* One eye: the game's SomView with the eye's pose and covering fov, plus rect/slot/projection/hash */
void somvr_rig_eye(const somvr_rig *r, const somvr_rig_cfg *c, somvr_base b, const somvr_view *game,
                   const somvr_posef *xr, const somvr_fovf *fov, int eye, somvr_eye_out *out);

/* The head (the eye centre, somvr_head_game) mapped like the eyes: pos = B + s R(Y(psi - yaw0)) (m(p_head) - P0),
 * quat = Y(psi - yaw0) (x) m(q_head). The audio listener's pose (SOMVR_AUDIO=head). */
void somvr_rig_head(const somvr_rig *r, somvr_base b, const somvr_posef eye[2], sv_vec3 *pos, sv_quat *quat);
/* Room-scale body follow (SOMVR_FOLLOW, docs/re-m1m2.md 3.3): the tracked head's horizontal offset from the base in
 * game space (cm, x s), the move to ask for (0 inside dead_cm, else the offset clamped to step_cm), and the shift that
 * cancels a body move `applied` (game space, cm, y ignored) so the view does not move with the body:
 * P0 += R(Y(psi - yaw0))^-1 applied / s. */
sv_vec3 somvr_rig_offset(const somvr_rig *r, somvr_base b, const somvr_posef eye[2]);
sv_vec3 somvr_follow_step(sv_vec3 offset, float dead_cm, float step_cm);
/* build 18 (re-xr-pipeline.md 69.1): what the game applied of the follow's request q, from the raw measure m (the root's
 * move minus the game's own part of the tick's move): only m's part along q, 0..1 of q (horizontal; *k = the share). A
 * whole move held (the root still, our stick's step in the own part) gives m = -step, against q: 0, not -step (build
 * 17 shifted the rig's origin by it and the view walked on with the stick) */
sv_vec3 somvr_follow_along(sv_vec3 m, sv_vec3 q, float *k);
void somvr_rig_shift(somvr_rig *r, somvr_base b, sv_vec3 applied);

uint32_t somvr_layout_nslots(int layout);
void somvr_layout_rect(int layout, int eye, float rect[4], uint32_t *slot);

#endif
