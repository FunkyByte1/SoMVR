/* somvr_pointer.c - see somvr_pointer.h */
#include "somvr_pointer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- small vector math (the layer links no other module) -------------------------------------------------------- */
static void qrot(const float q[4], const float v[3], float out[3]) { /* v' = v + 2w (u x v) + 2 u x (u x v) */
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float t[3] = {2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
    float x[3] = {v[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]), v[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]),
                  v[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0])};
    memcpy(out, x, sizeof x);
}
static void qrot_inv(const float q[4], const float v[3], float out[3]) {
    float c[4] = {-q[0], -q[1], -q[2], q[3]};
    qrot(c, v, out);
}
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float a[3], const float b[3], float out[3]) {
    float x[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(out, x, sizeof x);
}
static float norm3(const float a[3]) { return sqrtf(dot3(a, a)); }
static float clamp01(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; } /* NaN -> 0 */
static int finite_pose(const somvr_posef *p) {
    float n = 0;
    for (int i = 0; i < 4; i++) n += p->q[i] * p->q[i];
    return isfinite(n) && n > 0.5f && n < 1.5f && isfinite(p->p[0]) && isfinite(p->p[1]) && isfinite(p->p[2]);
}

/* ---- geometry ------------------------------------------------------------------------------------------------------ */
void somvr_ptr_ray(const somvr_posef *pose, float o[3], float d[3]) {
    static const float fwd[3] = {0, 0, -1};
    memcpy(o, pose->p, 3 * sizeof *o);
    qrot(pose->q, fwd, d);
    float n = norm3(d);
    if (n > 0) d[0] /= n, d[1] /= n, d[2] /= n;
}

int somvr_ptr_hit_quad(const float o[3], const float d[3], const somvr_posef *quad, const float size[2], float max_t,
                       float uv[2], float *t) {
    if (!(size[0] > 0 && size[1] > 0)) return 0;
    float rel[3] = {o[0] - quad->p[0], o[1] - quad->p[1], o[2] - quad->p[2]}, lo[3], ld[3];
    qrot_inv(quad->q, rel, lo);
    qrot_inv(quad->q, d, ld);
    if (!(lo[2] > 0) || !(ld[2] < -1e-6f)) return 0; /* behind the quad, or pointing away / along it */
    float tt = -lo[2] / ld[2];
    if (!(tt > 0 && tt <= max_t)) return 0;
    float x = lo[0] + tt * ld[0], y = lo[1] + tt * ld[1];
    float u = x / size[0] + 0.5f, v = 0.5f - y / size[1];
    if (!isfinite(u) || !isfinite(v)) return 0;
    uv[0] = u, uv[1] = v, *t = tt;
    return 1;
}

void somvr_ptr_quad_point(const somvr_posef *quad, const float size[2], const float uv[2], float lift, float out[3]) {
    float l[3] = {(uv[0] - 0.5f) * size[0], (0.5f - uv[1]) * size[1], lift}, w[3];
    qrot(quad->q, l, w);
    for (int i = 0; i < 3; i++) out[i] = quad->p[i] + w[i];
}

/* the unit quaternion of the rotation whose matrix has columns x, y, z (orthonormal, right-handed) */
static void quat_of_basis(const float x[3], const float y[3], const float z[3], float q[4]) {
    float m00 = x[0], m11 = y[1], m22 = z[2], tr = m00 + m11 + m22;
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q[3] = 0.25f * s, q[0] = (y[2] - z[1]) / s, q[1] = (z[0] - x[2]) / s, q[2] = (x[1] - y[0]) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2;
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2;
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2;
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) q[i] /= n;
}

int somvr_ptr_ray_quad(const float a[3], const float b[3], const float *eye, float thickness, somvr_posef *pose,
                       float size[2]) {
    float x[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, len = norm3(x);
    if (!(len >= 0.001f)) return 0;
    for (int i = 0; i < 3; i++) x[i] /= len;
    float c[3] = {0.5f * (a[0] + b[0]), 0.5f * (a[1] + b[1]), 0.5f * (a[2] + b[2])}, z[3];
    static const float up[3] = {0, 1, 0}, back[3] = {0, 0, 1};
    const float *want[3];
    float to_eye[3];
    int nw = 0;
    if (eye) {
        for (int i = 0; i < 3; i++) to_eye[i] = eye[i] - c[i];
        want[nw++] = to_eye;
    }
    want[nw++] = up, want[nw++] = back;
    for (int k = 0; k < nw; k++) { /* the wanted normal without its part along the line; the next one if nothing is left */
        float p = dot3(want[k], x);
        for (int i = 0; i < 3; i++) z[i] = want[k][i] - p * x[i];
        float n = norm3(z);
        if (n > 1e-4f || k == nw - 1) { /* up and back are never both along the line */
            for (int i = 0; i < 3; i++) z[i] /= n;
            break;
        }
    }
    float y[3];
    cross3(z, x, y);
    quat_of_basis(x, y, z, pose->q);
    memcpy(pose->p, c, sizeof c);
    size[0] = len, size[1] = thickness;
    return 1;
}

float somvr_ptr_dot_size(float dist, float deg) {
    float d = 2.0f * dist * tanf(0.5f * deg * 0.01745329252f);
    return d > 0.004f ? d : 0.004f; /* NaN -> 4 mm */
}

/* ---- the pointer state ----------------------------------------------------------------------------------------- */
void somvr_ptr_cfg_default(somvr_ptr_cfg *c) {
    *c = (somvr_ptr_cfg){.leave = 0.02f, .max_dist = 20.0f, .min_cutoff = 1.2f, .beta = 10.0f, .d_cutoff = 2.5f,
                         .trig_on = 0.6f, .trig_off = 0.4f, .lose_frames = 6, .hold_deg = 0, .settle_s = 0.3f};
}

void somvr_ptr_in_from(const somvr_input_state *st, somvr_ptr_in *in) {
    for (int h = 0; h < 2; h++) {
        const somvr_hand_in *hi = &st->hand[h];
        int pose_on = (hi->active & SOMVR_HACT_POSE) != 0;
        in->ok[h] = in->grip[h] = 0;
        if (pose_on && (hi->aim_flags & 3u) == 3u && finite_pose(&hi->aim)) {
            in->pose[h] = hi->aim, in->ok[h] = 1;
        } else if (pose_on && (hi->grip_flags & 3u) == 3u && finite_pose(&hi->grip)) {
            in->pose[h] = hi->grip, in->ok[h] = 1, in->grip[h] = 1;
        } else {
            memset(&in->pose[h], 0, sizeof in->pose[h]);
            in->pose[h].q[3] = 1;
        }
        in->trigger[h] = (hi->active & SOMVR_HACT_INPUT) && isfinite(hi->trigger) ? hi->trigger : 0.0f;
    }
}

void somvr_ptr_reset(somvr_ptr_state *s) {
    memset(s, 0, sizeof *s);
    s->hand = SOMVR_HAND_R;
}

static int inside(const float uv[2], float m) { return uv[0] >= -m && uv[0] <= 1 + m && uv[1] >= -m && uv[1] <= 1 + m; }
static float alpha_of(float fc, float dt) {
    float tau = 1.0f / (6.2831853f * fc);
    return 1.0f / (1.0f + tau / dt);
}
/* One Euro step on the raw point (restart: the raw point itself) */
static void filter(somvr_ptr_state *s, const somvr_ptr_cfg *c, const float raw[2], int64_t time) {
    double dt = s->have_f ? (double)(time - s->t) * 1e-9 : -1;
    if (s->have_f && dt == 0) return; /* the same sample again (a frame that reused its poses) */
    if (!s->have_f || dt < 0 || dt > 0.25) {
        memcpy(s->f, raw, sizeof s->f);
        s->df[0] = s->df[1] = 0;
        s->have_f = 1, s->t = time;
        s->have_h = 0; /* build 20: the held point starts again with it */
        return;
    }
    float fdt = (float)dt, ad = alpha_of(c->d_cutoff, fdt);
    for (int i = 0; i < 2; i++) s->df[i] += ad * ((raw[i] - s->f[i]) / fdt - s->df[i]);
    float a = alpha_of(c->min_cutoff + c->beta * sqrtf(s->df[0] * s->df[0] + s->df[1] * s->df[1]), fdt);
    for (int i = 0; i < 2; i++) s->f[i] += a * (raw[i] - s->f[i]);
    s->t = time;
}
/* build 20: the held point's step toward the filtered point f (clamped uv) on a quad of size[2] m, dist m from the hand */
static void hold(somvr_ptr_state *s, const somvr_ptr_cfg *c, const float f[2], const float size[2], float dist,
                 int64_t time) {
    float r = dist * tanf(c->hold_deg * 0.01745329252f);
    if (!s->have_h || !(r > 1e-4f) || !(size[0] > 0 && size[1] > 0)) { /* (NaN: no hold) */
        memcpy(s->h, f, sizeof s->h);
        s->have_h = 1, s->ht = time;
        return;
    }
    double dt = (double)(time - s->ht) * 1e-9;
    s->ht = time;
    float d[2] = {(f[0] - s->h[0]) * size[0], (f[1] - s->h[1]) * size[1]}, dm = sqrtf(d[0] * d[0] + d[1] * d[1]);
    if (dm > r) { /* beyond the radius: follows by the excess */
        float k = (dm - r) / dm;
        for (int i = 0; i < 2; i++) s->h[i] += k * (f[i] - s->h[i]);
        dm = r;
    }
    if (c->settle_s > 0 && dt > 0 && dt <= 0.25) { /* inside: eases toward it, (offset / radius)^2 of the edge's rate */
        float w = dm / r, a = 1.0f - expf(-(float)dt * w * w / c->settle_s);
        for (int i = 0; i < 2; i++) s->h[i] += a * (f[i] - s->h[i]);
    }
}

static void end_click(somvr_ptr_state *s, somvr_ptr_out *out) {
    if (!s->down) return;
    s->down = 0, s->ups++, s->cancels++;
    out->edges |= SOMVR_PTRE_RELEASE | SOMVR_PTRE_CANCEL;
}

void somvr_ptr_update(somvr_ptr_state *s, const somvr_ptr_cfg *c, const somvr_ptr_in *in, somvr_ptr_out *out) {
    memset(out, 0, sizeof *out);
    uint32_t q = in->quad;
    int was_on = s->on;
    if (q != s->quad) { /* another target, or none: a held click ends, the pointer starts again */
        end_click(s, out);
        s->on = 0, s->have_f = 0, s->quad = q;
    }
    int plane[2] = {0, 0};
    float o[2][3], d[2][3], t[2] = {0, 0};
    for (int h = 0; h < 2; h++) {
        if (!in->ok[h]) continue;
        somvr_ptr_ray(&in->pose[h], o[h], d[h]);
        if (q != SOMVR_PTRQ_NONE &&
            somvr_ptr_hit_quad(o[h], d[h], &in->quad_pose, in->quad_size, c->max_dist, out->raw_uv[h], &t[h]))
            plane[h] = 1, out->raw_hit[h] = inside(out->raw_uv[h], c->leave);
    }
    int rise[2];
    for (int h = 0; h < 2; h++) {
        int was = s->trig[h];
        float v = in->trigger[h];
        if (!isfinite(v)) v = 0;
        s->trig[h] = was ? !(v < c->trig_off) : v > c->trig_on;
        rise[h] = !was && s->trig[h];
    }
    int other = 1 - s->hand;
    if (rise[other] && !rise[s->hand]) { /* the other hand pulled its trigger: it points from now on */
        end_click(s, out);
        s->hand = other, s->switches++;
        s->on = 0, s->have_f = 0, s->lost = 0;
        out->edges |= SOMVR_PTRE_SWITCH;
    }
    int H = s->hand;
    s->lost = in->ok[H] ? 0 : s->lost + 1;
    if (in->ok[H] && plane[H]) {
        const float *r = out->raw_uv[H];
        s->on = s->down ? 1 : s->on ? inside(r, c->leave) : inside(r, 0);
        if (s->on) filter(s, c, r, in->time);
    } else if (s->down && !in->ok[H] && s->lost <= c->lose_frames && q != SOMVR_PTRQ_NONE) {
        /* a short tracking gap while a click is held: the last point stays */
    } else {
        end_click(s, out);
        s->on = 0;
    }
    if (!s->on) s->have_f = 0, s->have_h = 0;
    if (s->on && !s->down && rise[H]) {
        s->down = 1, s->downs++;
        out->edges |= SOMVR_PTRE_PRESS;
    } else if (s->down && !s->trig[H]) {
        s->down = 0, s->ups++;
        out->edges |= SOMVR_PTRE_RELEASE;
    }
    if (s->on && !was_on) out->edges |= SOMVR_PTRE_ENTER, s->enters++;
    if (!s->on && was_on) out->edges |= SOMVR_PTRE_LEAVE;
    out->hand = H;
    if (s->on) {
        if (s->have_f) s->uv[0] = clamp01(s->f[0]), s->uv[1] = clamp01(s->f[1]);
        if (s->have_f && c->hold_deg > 0) { /* build 20: the held point over the filtered one */
            float fu[2] = {s->uv[0], s->uv[1]};
            hold(s, c, fu, in->quad_size, in->ok[H] && plane[H] ? t[H] : s->dist, in->time);
            s->uv[0] = clamp01(s->h[0]), s->uv[1] = clamp01(s->h[1]);
        }
        memcpy(out->uv, s->uv, sizeof out->uv);
        somvr_ptr_quad_point(&in->quad_pose, in->quad_size, s->uv, 0, out->hit);
        if (in->ok[H]) {
            float e[3] = {out->hit[0] - o[H][0], out->hit[1] - o[H][1], out->hit[2] - o[H][2]};
            s->dist = norm3(e);
        }
        out->dist = s->dist;
    }
    if (in->ok[H]) {
        memcpy(out->origin, o[H], sizeof out->origin);
        memcpy(out->dir, d[H], sizeof out->dir);
    }
    out->flags = (s->on ? SOMVR_PTRF_ON : 0u) | (s->down ? SOMVR_PTRF_DOWN : 0u) | (in->ok[H] ? SOMVR_PTRF_AIM : 0u) |
                 (in->ok[H] && in->grip[H] ? SOMVR_PTRF_GRIP : 0u);
    for (int h = 0; h < 2; h++)
        if (out->raw_hit[h] || (h == H && (s->on || s->down))) out->consume |= 1u << h;
}

/* ---- the game side ---------------------------------------------------------------------------------------------- */
int somvr_ptr_pixel(const float uv[2], int iw, int ih, int vx, int vy, int out[2]) {
    if (iw < 1 || ih < 1) return 0;
    int x = (int)floorf(clamp01(uv[0]) * (float)iw), y = (int)floorf(clamp01(uv[1]) * (float)ih);
    out[0] = vx + (x < iw ? x : iw - 1);
    out[1] = vy + (y < ih ? y : ih - 1);
    return 1;
}

int somvr_ptr_route_tick(somvr_ptr_route *r, int h, int down, int consume) {
    if (h < 0 || h > 1) return 0;
    if (down && !r->held[h]) r->mine[h] = consume != 0; /* the press decides for the whole hold */
    if (!down) r->mine[h] = 0;
    r->held[h] = down != 0;
    return r->mine[h] && down;
}

void somvr_ptr_parse_hold(const char *v, somvr_ptr_cfg *c, int *bad) {
    c->hold_deg = 0, c->settle_s = 0.3f;
    if (!v || !*v || !strcmp(v, "off") || !strcmp(v, "0")) return;
    if (!strcmp(v, "on")) {
        c->hold_deg = 0.6f;
        return;
    }
    char *e = NULL;
    float deg = strtof(v, &e), ms = 300;
    if (e && *e == ',') {
        const char *m = e + 1;
        ms = strtof(m, &e);
        if (e == m) e = NULL; /* "DEG," */
    }
    if (!e || *e || !(deg > 0 && deg <= 3) || !(ms >= 0 && ms <= 2000)) {
        *bad = 1;
        return;
    }
    c->hold_deg = deg, c->settle_s = ms * 0.001f;
}

int somvr_ptr_parse_mode(const char *v, int *bad) {
    if (!v || !*v || !strcmp(v, "on") || !strcmp(v, "1")) return SOMVR_PTRM_ON;
    if (!strcmp(v, "dot")) return SOMVR_PTRM_DOT;
    if (!strcmp(v, "off") || !strcmp(v, "0")) return SOMVR_PTRM_OFF;
    *bad = 1;
    return SOMVR_PTRM_ON;
}

/* ---- the pointer image -------------------------------------------------------------------------------------------- */
static void put(uint8_t *p, float r, float g, float b, float a, int bgra) { /* straight sRGB bytes -> premultiplied */
    a = clamp01(a);
    uint8_t R = (uint8_t)lroundf(r * a), G = (uint8_t)lroundf(g * a), B = (uint8_t)lroundf(b * a);
    p[0] = bgra ? B : R, p[1] = G, p[2] = bgra ? R : B, p[3] = (uint8_t)lroundf(a * 255.0f);
}
void somvr_ptr_image(uint8_t *px, int bgra) {
    const uint32_t W = SOMVR_PTR_IMG_W, S = SOMVR_PTR_IMG_H;
    for (uint32_t y = 0; y < S; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *p = px + 4 * (y * W + x);
            float fy = (float)y + 0.5f - 0.5f * (float)S;
            if (x < S) { /* the dot: a white disc (r < 9.5) in a dark ring, its outer edge soft (r 12.5..14) */
                float fx = (float)x + 0.5f - 0.5f * (float)S, r = sqrtf(fx * fx + fy * fy);
                float a = clamp01((14.0f - r) / 1.5f), w = clamp01(10.0f - r);
                put(p, 40 + 215 * w, 42 + 213 * w, 50 + 205 * w, a, bgra);
            } else { /* the ray: dim at the hand (x = 32), brighter toward the hit (x = 63); a soft core across y */
                float along = ((float)(x - S) + 0.5f) / (float)S, prof = clamp01(1.0f - fabsf(fy) / 14.0f);
                put(p, 150, 220, 255, (0.15f + 0.65f * along) * prof * prof, bgra);
            }
        }
}

void somvr_ptr_rect(int ray, int32_t out[4]) {
    out[0] = ray ? (int32_t)SOMVR_PTR_IMG_H : 0, out[1] = 0;
    out[2] = (int32_t)SOMVR_PTR_IMG_H, out[3] = (int32_t)SOMVR_PTR_IMG_H;
}
