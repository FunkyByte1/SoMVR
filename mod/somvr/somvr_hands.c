/* somvr_hands.c - see somvr_hands.h */
#include "somvr_hands.h"

#include <math.h>
#include <openxr/openxr.h>
#include <string.h>
#include <time.h>

_Static_assert(sizeof(somvr_posef) == sizeof(XrPosef) && offsetof(XrPosef, position) == 16, "somvr_posef = XrPosef");

int somvr_hand_extrapolate(const somvr_hand_in *hi, int64_t from, int64_t to, int64_t max_dt, somvr_hand *out) {
    memset(out, 0, sizeof *out);
    out->time = to;
    int64_t d = to - from;
    if (!(hi->active & SOMVR_HACT_POSE) || (hi->grip_flags & 3u) != 3u || d > max_dt || d < -max_dt) return 0;
    float dt = (float)((double)d * 1e-9);
    out->pose = hi->grip;
    out->flags = hi->grip_flags;
    out->vel_valid = hi->vel_flags & 3;
    if (hi->vel_flags & 1u) {
        memcpy(out->lin, hi->lin_vel, sizeof out->lin);
        for (int i = 0; i < 3; i++) out->pose.p[i] += hi->lin_vel[i] * dt;
    }
    if (hi->vel_flags & 2u) {
        memcpy(out->ang, hi->ang_vel, sizeof out->ang);
        float w = sqrtf(hi->ang_vel[0] * hi->ang_vel[0] + hi->ang_vel[1] * hi->ang_vel[1] + hi->ang_vel[2] * hi->ang_vel[2]);
        float th = w * dt;
        if (w > 1e-9f && fabsf(th) > 1e-9f) {
            float s = sinf(0.5f * th) / w;
            sv_quat dq = {hi->ang_vel[0] * s, hi->ang_vel[1] * s, hi->ang_vel[2] * s, cosf(0.5f * th)};
            sv_quat q = sv_qnormalize(sv_qmul(dq, sv_quat_of(hi->grip.q)));
            sv_quat_to(q, out->pose.q);
        }
    }
    int ok = 1;
    for (int i = 0; i < 4; i++) ok &= isfinite(out->pose.q[i]);
    for (int i = 0; i < 3; i++) ok &= isfinite(out->pose.p[i]);
    out->src = SOMVR_HSRC_SNAPSHOT;
    out->tracked = (hi->grip_flags & 0xcu) == 0xcu;
    return out->valid = ok;
}

void somvr_hands_locate(somvr_shared *sh, somvr_input_shared *in, const somvr_input_state *snap, int mode, int64_t time,
                        int64_t max_dt, somvr_hands_state *st, somvr_hand out[2]) {
    int done[2] = {0, 0}, res[2] = {0, 0};
    memset(out, 0, 2 * sizeof *out);
    st->calls++;
    if (mode == SOMVR_HANDS_GAME && sh && in && time > 0) {
        if (!somvr_xr_enter(sh)) {
            st->guard++; /* no session (or the layer is tearing it down): never a call */
        } else {
            if (!atomic_load_explicit(&in->xr.ready, memory_order_acquire) || !in->xr.locate_space || !sh->xr.space) {
                st->notready++;
            } else {
                PFN_xrLocateSpace loc = (PFN_xrLocateSpace)in->xr.locate_space;
                struct timespec a, b;
                clock_gettime(CLOCK_MONOTONIC, &a);
                for (int h = 0; h < 2; h++) {
                    XrSpaceVelocity v = {.type = XR_TYPE_SPACE_VELOCITY};
                    XrSpaceLocation l = {.type = XR_TYPE_SPACE_LOCATION, .next = &v};
                    XrResult r = loc((XrSpace)(uintptr_t)in->xr.grip_space[h], (XrSpace)(uintptr_t)sh->xr.space,
                                     (XrTime)time, &l);
                    atomic_fetch_add_explicit(&in->n_hand_locates, 1, memory_order_relaxed);
                    res[h] = (int)r;
                    if (XR_FAILED(r) || (l.locationFlags & 3u) != 3u) {
                        atomic_fetch_add_explicit(&in->n_hand_fail, 1, memory_order_relaxed);
                        st->fail++;
                        continue;
                    }
                    somvr_hand *o = &out[h];
                    memcpy(o->pose.q, &l.pose.orientation, sizeof o->pose.q);
                    memcpy(o->pose.p, &l.pose.position, sizeof o->pose.p);
                    o->flags = (uint32_t)l.locationFlags;
                    o->tracked = (l.locationFlags & 0xcu) == 0xcu;
                    o->vel_valid = (int)(v.velocityFlags & 3u);
                    if (v.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) memcpy(o->lin, &v.linearVelocity, 12);
                    if (v.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) memcpy(o->ang, &v.angularVelocity, 12);
                    o->time = time, o->src = SOMVR_HSRC_LOCATED, o->valid = 1;
                    done[h] = 1;
                    st->located++;
                }
                clock_gettime(CLOCK_MONOTONIC, &b);
                st->last_us = (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
            }
            somvr_xr_leave(sh);
        }
    }
    for (int h = 0; h < 2; h++) {
        if (done[h]) continue;
        if (snap && somvr_hand_extrapolate(&snap->hand[h], snap->time, time, max_dt, &out[h])) st->snapshot++;
        else st->none++;
        out[h].result = res[h];
    }
}

void somvr_hand_game(const somvr_hand *h, sv_vec3 *p, sv_quat *q, sv_vec3 *v, sv_vec3 *w) {
    if (p) *p = sv_xr_to_game_pos(h->pose.p);
    if (q) *q = sv_qnormalize(sv_xr_to_game_quat(h->pose.q));
    if (v) *v = (sv_vec3){100.0f * h->lin[0], 100.0f * h->lin[1], -100.0f * h->lin[2]};
    if (w) *w = (sv_vec3){-h->ang[0], -h->ang[1], h->ang[2]};
}

sv_vec3 somvr_rig_vec(const somvr_rig *r, somvr_base b, sv_vec3 v) {
    float k = somvr_rig_scale(r);
    return sv_qrot(sv_qyaw(b.yaw - r->yaw0), (sv_vec3){k * v.x, k * v.y, k * v.z});
}

sv_vec3 somvr_rig_point(const somvr_rig *r, somvr_base b, sv_vec3 p) {
    sv_vec3 d = somvr_rig_vec(r, b, (sv_vec3){p.x - r->p0.x, p.y - r->p0.y, p.z - r->p0.z});
    return (sv_vec3){b.pos.x + d.x, b.pos.y + d.y, b.pos.z + d.z};
}

sv_quat somvr_rig_quat(const somvr_rig *r, somvr_base b, sv_quat q) {
    return sv_qnormalize(sv_qmul(sv_qyaw(b.yaw - r->yaw0), q));
}
