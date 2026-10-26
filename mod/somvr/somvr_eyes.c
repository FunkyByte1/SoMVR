/* somvr_eyes.c - see somvr_eyes.h */
#include "somvr_eyes.h"

#include <math.h>
#include <string.h>

void somvr_rig_init(somvr_rig *r) { memset(r, 0, sizeof *r); }

float somvr_rig_scale(const somvr_rig *r) { return r->scale >= 0.5f && r->scale <= 2.0f ? r->scale : 1.0f; }

void somvr_head_game(const somvr_posef eye[2], sv_vec3 *pos, sv_quat *quat) {
    sv_vec3 a = sv_xr_to_game_pos(eye[0].p), b = sv_xr_to_game_pos(eye[1].p);
    *pos = (sv_vec3){0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
    sv_quat qa = sv_xr_to_game_quat(eye[0].q), qb = sv_xr_to_game_quat(eye[1].q);
    float d = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    float s = d < 0 ? -1.0f : 1.0f; /* same hemisphere before averaging */
    *quat = sv_qnormalize((sv_quat){qa.x + s * qb.x, qa.y + s * qb.y, qa.z + s * qb.z, qa.w + s * qb.w});
}

somvr_base somvr_rig_frame(somvr_rig *r, const somvr_rig_cfg *c, const somvr_posef eye[2], const somvr_view *game,
                           const float *body_pos, int want_recenter, int32_t snap_delta) {
    sv_quat gq = sv_qnormalize(sv_quat_of(game->quat));
    if (want_recenter || !r->have_ref) {
        sv_vec3 hp;
        sv_quat hq;
        somvr_head_game(eye, &hp, &hq);
        if (c->floor) hp.y = 0;
        r->p0 = hp;
        r->yaw0 = sv_heading(hq);
        r->yaw_fixed = sv_heading(gq);
        r->have_ref = 1;
        r->recenters++;
    }
    r->snap_steps += snap_delta;
    somvr_base b;
    if ((c->base == SOMVR_BASE_PLAYER || c->base == SOMVR_BASE_HEAD) && body_pos)
        b.pos = (sv_vec3){body_pos[0], body_pos[1] + c->eye_cm, body_pos[2]};
    else
        b.pos = (sv_vec3){game->pos[0], game->pos[1], game->pos[2]};
    float yaw = c->yaw == SOMVR_YAW_FIXED ? r->yaw_fixed : sv_heading(gq);
    b.yaw = sv_wrap_pi(yaw + (float)r->snap_steps * c->snap_rad);
    return b;
}

void somvr_rig_head(const somvr_rig *r, somvr_base b, const somvr_posef eye[2], sv_vec3 *pos, sv_quat *quat) {
    sv_vec3 hp;
    sv_quat hq;
    somvr_head_game(eye, &hp, &hq);
    sv_quat qa = sv_qyaw(b.yaw - r->yaw0);
    float k = somvr_rig_scale(r);
    sv_vec3 d = sv_qrot(qa, (sv_vec3){k * (hp.x - r->p0.x), k * (hp.y - r->p0.y), k * (hp.z - r->p0.z)});
    *pos = (sv_vec3){b.pos.x + d.x, b.pos.y + d.y, b.pos.z + d.z};
    *quat = sv_qnormalize(sv_qmul(qa, hq));
}

sv_vec3 somvr_rig_offset(const somvr_rig *r, somvr_base b, const somvr_posef eye[2]) {
    sv_vec3 hp;
    sv_quat hq;
    somvr_head_game(eye, &hp, &hq);
    float k = somvr_rig_scale(r);
    sv_vec3 d = sv_qrot(sv_qyaw(b.yaw - r->yaw0), (sv_vec3){k * (hp.x - r->p0.x), 0, k * (hp.z - r->p0.z)});
    return (sv_vec3){d.x, 0, d.z};
}

sv_vec3 somvr_follow_step(sv_vec3 o, float dead_cm, float step_cm) {
    float l = sqrtf(o.x * o.x + o.z * o.z);
    if (!(l > dead_cm)) return (sv_vec3){0, 0, 0}; /* also NaN */
    float k = l > step_cm ? step_cm / l : 1.0f;
    return (sv_vec3){k * o.x, 0, k * o.z};
}

void somvr_rig_shift(somvr_rig *r, somvr_base b, sv_vec3 a) {
    float k = 1.0f / somvr_rig_scale(r);
    sv_vec3 t = sv_qrot(sv_qyaw(r->yaw0 - b.yaw), (sv_vec3){k * a.x, 0, k * a.z});
    r->p0.x += t.x;
    r->p0.z += t.z;
}

uint32_t somvr_layout_nslots(int layout) { return layout == SOMVR_LAYOUT_B ? 2u : 1u; }

void somvr_layout_rect(int layout, int eye, float rect[4], uint32_t *slot) {
    rect[0] = 0, rect[1] = 0, rect[2] = 1, rect[3] = 1;
    *slot = 0;
    if (layout == SOMVR_LAYOUT_A) {
        rect[0] = eye ? 0.5f : 0.0f;
        rect[2] = eye ? 1.0f : 0.5f;
    } else if (layout == SOMVR_LAYOUT_B) {
        *slot = eye ? 1u : 0u;
    }
}

void somvr_rig_eye(const somvr_rig *r, const somvr_rig_cfg *c, somvr_base b, const somvr_view *game,
                   const somvr_posef *xr, const somvr_fovf *fov, int eye, somvr_eye_out *out) {
    sv_vec3 pm = sv_xr_to_game_pos(xr->p);
    sv_quat qm = sv_qnormalize(sv_xr_to_game_quat(xr->q));
    float k = somvr_rig_scale(r);
    sv_vec3 rel = {k * (pm.x - r->p0.x), k * (pm.y - r->p0.y), k * (pm.z - r->p0.z)};
    sv_quat qa = sv_qyaw(b.yaw - r->yaw0);
    sv_vec3 d = sv_qrot(qa, rel);
    out->view = *game;
    out->view.pos[0] = b.pos.x + d.x;
    out->view.pos[1] = b.pos.y + d.y;
    out->view.pos[2] = b.pos.z + d.z;
    sv_quat_to(sv_qnormalize(sv_qmul(qa, qm)), out->view.quat);
    sv_view_fov_cover(fov, &out->view.fov_x, &out->view.fov_y);
    out->fov_cover = sv_fov_cover(fov);
    out->proj = sv_proj_from_fov(fov);
    somvr_layout_rect(c->layout, eye, out->rect, &out->slot);
    out->hash = sv_xf_hash(&out->view); /* pos[3] then quat[4] */
}
