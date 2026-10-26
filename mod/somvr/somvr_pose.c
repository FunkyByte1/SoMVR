/* somvr_pose.c - see somvr_pose.h */
#include "somvr_pose.h"

#include <math.h>
#include <string.h>

#define SV_PI 3.14159265358979323846f

sv_quat sv_qmul(sv_quat a, sv_quat b) {
    return (sv_quat){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

sv_quat sv_qconj(sv_quat q) { return (sv_quat){-q.x, -q.y, -q.z, q.w}; }

sv_quat sv_qnormalize(sv_quat q) {
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-12f)) return (sv_quat){0, 0, 0, 1};
    return (sv_quat){q.x / n, q.y / n, q.z / n, q.w / n};
}

/* R(q) v with the matrix of docs/re-xr-pipeline.md (FUN_0072b050's view-to-world rotation); q need not be unit */
sv_vec3 sv_qrot(sv_quat q, sv_vec3 v) {
    float nn = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    float s = nn > 1e-12f ? 2.0f / nn : 0.0f;
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return (sv_vec3){(1 - s * (y * y + z * z)) * v.x + s * (x * y - w * z) * v.y + s * (x * z + w * y) * v.z,
                     s * (x * y + w * z) * v.x + (1 - s * (x * x + z * z)) * v.y + s * (y * z - w * x) * v.z,
                     s * (x * z - w * y) * v.x + s * (y * z + w * x) * v.y + (1 - s * (x * x + y * y)) * v.z};
}

sv_quat sv_qyaw(float a) { return (sv_quat){0, sinf(0.5f * a), 0, cosf(0.5f * a)}; }

float sv_wrap_pi(float a) {
    a = fmodf(a + SV_PI, 2 * SV_PI);
    if (a <= 0) a += 2 * SV_PI;
    return a - SV_PI;
}

float sv_heading(sv_quat q) {
    sv_vec3 f = sv_qrot(q, (sv_vec3){0, 0, 1});
    if (f.x * f.x + f.z * f.z > 1e-8f) return atan2f(f.x, f.z);
    return sv_wrap_pi(2.0f * atan2f(q.y, q.w)); /* looking straight up/down: the twist about +Y */
}

sv_quat sv_quat_of(const float q[4]) { return (sv_quat){q[0], q[1], q[2], q[3]}; }
void sv_quat_to(sv_quat q, float out[4]) {
    out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w;
}

sv_vec3 sv_xr_to_game_pos(const float p[3]) { return (sv_vec3){100.0f * p[0], 100.0f * p[1], -100.0f * p[2]}; }
sv_quat sv_xr_to_game_quat(const float q[4]) { return (sv_quat){-q[0], -q[1], q[2], q[3]}; }
void sv_game_to_xr_pos(sv_vec3 p, float out[3]) {
    out[0] = 0.01f * p.x, out[1] = 0.01f * p.y, out[2] = -0.01f * p.z;
}
void sv_game_to_xr_quat(sv_quat q, float out[4]) {
    out[0] = -q.x, out[1] = -q.y, out[2] = q.z, out[3] = q.w;
}

sv_proj sv_proj_from_fov(const somvr_fovf *f) {
    float tl = tanf(f->l), tr = tanf(f->r), tu = tanf(f->u), td = tanf(f->d);
    return (sv_proj){0.5f * (tr - tl), 0.5f * (tu - td), 0.5f * (tr + tl), 0.5f * (tu + td)};
}

somvr_fovf sv_fov_from_proj(sv_proj p) {
    return (somvr_fovf){atanf(p.cx - p.tx), atanf(p.cx + p.tx), atanf(p.cy + p.ty), atanf(p.cy - p.ty)};
}

somvr_fovf sv_fov_cover(const somvr_fovf *f) {
    float h = fmaxf(fabsf(f->l), fabsf(f->r)), v = fmaxf(fabsf(f->u), fabsf(f->d));
    return (somvr_fovf){-h, h, v, -v};
}

void sv_view_fov_cover(const somvr_fovf *f, float *fov_x, float *fov_y) {
    somvr_fovf c = sv_fov_cover(f);
    *fov_x = 2.0f * c.r;
    *fov_y = 2.0f * c.u;
}

somvr_fovf sv_fov_of_view(float fov_x, float fov_y) {
    return (somvr_fovf){-0.5f * fov_x, 0.5f * fov_x, 0.5f * fov_y, -0.5f * fov_y};
}

somvr_fovf sv_fov_overscan(const somvr_fovf *f, float s) {
    if (!(s > 0)) return *f; /* also NaN */
    if (s > SOMVR_OVERSCAN_MAX) s = SOMVR_OVERSCAN_MAX;
    float tl = tanf(f->l), tr = tanf(f->r), tu = tanf(f->u), td = tanf(f->d);
    float mx = 0.5f * s * (tr - tl), my = 0.5f * s * (tu - td); /* s x the half-extent, per side */
    return (somvr_fovf){atanf(tl - mx), atanf(tr + mx), atanf(tu + my), atanf(td - my)};
}

uint32_t sv_xf_hash(const void *xf28) {
    unsigned char b[28];
    memcpy(b, xf28, sizeof b);
    uint32_t h = 2166136261u;
    for (int i = 0; i < 28; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
