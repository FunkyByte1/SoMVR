/* somvr_threat.c - see somvr_threat.h (pure functions, no globals) */
#include "somvr_threat.h"

#include <math.h>
#include <string.h>

#define TH_DEG 57.29577951f

void somvr_th_cfg_default(somvr_th_cfg *c) {
    c->view_deg = 110.0f;
    c->glow = 0.7f;
    c->edge = 0.8f;
    c->fade_in_s = 0.15f;
    c->fade_out_s = 0.25f;
    c->base = 0.35f;
    c->edge_base = 0.5f;
    c->edge_ramp_s = 0.8f;
    c->urgent_s = 0.35f;
    c->near_m = 2.5f;
    c->near_level = 0.3f;
    c->proj_r_cm = 60.0f;
    c->proj_t_s = 1.5f;
    c->glow_deg = 9.0f;
    c->glow_min_m = 0.15f;
    c->glow_max_m = 0.9f;
    c->sound_min_s = 0.25f;
}

static float clamp01(float x) { return x > 0 ? (x < 1 ? x : 1) : 0; } /* NaN -> 0 */
static float smooth(float e0, float e1, float x) {
    if (!(e1 > e0)) return x >= e1 ? 1.0f : 0.0f;
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

/* v rotated by the unit quaternion (x y z w), or by its inverse (inv) */
static void qrot(const float q[4], int inv, const float v[3], float out[3]) {
    float qx = inv ? -q[0] : q[0], qy = inv ? -q[1] : q[1], qz = inv ? -q[2] : q[2], qw = q[3];
    float tx = 2 * (qy * v[2] - qz * v[1]), ty = 2 * (qz * v[0] - qx * v[2]), tz = 2 * (qx * v[1] - qy * v[0]);
    out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
    out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
    out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}

void somvr_th_local(const float head[3], const float q[4], const float p[3], float out[3]) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    float u[4] = {0, 0, 0, 1}, d[3] = {p[0] - head[0], p[1] - head[1], p[2] - head[2]};
    if (n > 1e-6f)
        for (int i = 0; i < 4; i++) u[i] = q[i] / n;
    qrot(u, 1, d, out);
}

somvr_th_dir somvr_th_direction(const float l[3]) {
    somvr_th_dir d;
    float h = sqrtf(l[0] * l[0] + l[2] * l[2]);
    d.dist_cm = sqrtf(h * h + l[1] * l[1]);
    d.yaw_deg = atan2f(l[0], l[2]) * TH_DEG;
    d.pitch_deg = atan2f(l[1], h) * TH_DEG;
    float c = d.dist_cm > 1e-6f ? l[2] / d.dist_cm : 1.0f;
    d.off_deg = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * TH_DEG;
    if (d.dist_cm != d.dist_cm) d.off_deg = NAN;
    return d;
}

int somvr_th_in_view(const somvr_th_cfg *c, somvr_th_dir d) { return d.off_deg <= 0.5f * c->view_deg; }

int somvr_th_side(somvr_th_dir d) {
    if (d.yaw_deg != d.yaw_deg) return 0;
    if (fabsf(d.yaw_deg) >= 180.0f - SOMVR_TH_BEHIND_DEG) return SOMVR_TH_BOTH;
    return d.yaw_deg >= 0 ? SOMVR_TH_RIGHT : SOMVR_TH_LEFT;
}

const char *somvr_th_side_name(int side) {
    return side == SOMVR_TH_LEFT ? "L" : side == SOMVR_TH_RIGHT ? "R" : side == SOMVR_TH_BOTH ? "LR" : "-";
}

somvr_th_keys somvr_th_keys_of(const uint16_t ws[SOMVR_TH_KT_N]) {
    somvr_th_keys k;
    k.impact = ws[SOMVR_TH_KT_IMPACT] != 0xffff ? (float)ws[SOMVR_TH_KT_IMPACT] * 1e-3f : -1.0f;
    k.bcol = ws[SOMVR_TH_KT_BCOL] != 0xffff ? (float)ws[SOMVR_TH_KT_BCOL] * 1e-3f : -1.0f;
    int win = ws[SOMVR_TH_KT_BPW] != 0xffff && ws[SOMVR_TH_KT_EPW] != 0xffff && ws[SOMVR_TH_KT_EPW] > ws[SOMVR_TH_KT_BPW];
    k.bpw = win ? (float)ws[SOMVR_TH_KT_BPW] * 1e-3f : -1.0f;
    k.epw = win ? (float)ws[SOMVR_TH_KT_EPW] * 1e-3f : -1.0f;
    k.ranged = ws[SOMVR_TH_KT_SHOOT] != 0xffff;
    return k;
}

float somvr_th_lead(const somvr_th_keys *k, float t) { return k->impact >= 0 ? k->impact - t : NAN; }

int somvr_th_window(const somvr_th_keys *k, float t) { return k->bpw >= 0 && t >= k->bpw && t <= k->epw; }

/* after the impact: 1 at it, 0 fade_out_s later; -1 = not after the impact */
static float after_impact(const somvr_th_cfg *c, const somvr_th_keys *k, float t) {
    if (!(k->impact >= 0) || !(t > k->impact)) return -1.0f;
    return c->fade_out_s > 0 ? clamp01(1 - (t - k->impact) / c->fade_out_s) : 0.0f;
}

float somvr_th_glow_level(const somvr_th_cfg *c, const somvr_th_keys *k, float t) {
    if (!(t >= 0)) return 0;
    float fin = c->fade_in_s > 0 ? smooth(0, c->fade_in_s, t) : 1.0f, out = after_impact(c, k, t), l;
    if (out >= 0) return clamp01(fin * out);
    if (k->bpw >= 0 && (k->impact < 0 || k->bpw < k->impact)) { /* the peak inside the window: at 80 % of it */
        float peak = k->bpw + 0.8f * (k->epw - k->bpw);
        l = t < k->bpw ? c->base : c->base + (1 - c->base) * smooth(k->bpw, peak, t);
    } else if (k->impact > 0) {
        l = c->base + (1 - c->base) * smooth(0, k->impact, t);
    } else {
        l = c->base;
    }
    return clamp01(fin * l);
}

float somvr_th_edge_level(const somvr_th_cfg *c, const somvr_th_keys *k, float t) {
    if (!(t >= 0)) return 0;
    float fin = c->fade_in_s > 0 ? smooth(0, 0.5f * c->fade_in_s, t) : 1.0f, out = after_impact(c, k, t), l;
    if (out >= 0) return clamp01(fin * out);
    if (k->impact >= 0) {
        float lead = k->impact - t, x = c->edge_ramp_s > 0 ? clamp01(1 - lead / c->edge_ramp_s) : 1.0f;
        l = c->edge_base + (1 - c->edge_base) * smooth(0, 1, x);
    } else {
        l = c->edge_base;
    }
    return clamp01(fin * l);
}

int somvr_th_expired(const somvr_th_cfg *c, const somvr_th_keys *k, float t) {
    return k->impact >= 0 ? t > k->impact + c->fade_out_s : t > 4.0f;
}

const char *somvr_th_cue_name(int cue) {
    static const char *const n[] = {"none", "glow", "edge", "near", "proj-glow", "proj-edge"};
    return cue >= 0 && cue <= SOMVR_THC_PROJ_EDGE ? n[cue] : "?";
}
const char *somvr_th_haptic_name(int h) {
    static const char *const n[] = {"-", "announce", "urgent", "tick"};
    return h >= 0 && h <= SOMVR_THH_TICK ? n[h] : "?";
}

somvr_th_pick somvr_th_select(const somvr_th_cfg *c, somvr_th_dir d, const somvr_th_keys *k, float t, somvr_th_state *s) {
    somvr_th_pick p;
    memset(&p, 0, sizeof p);
    p.lead = somvr_th_lead(k, t);
    if (!(t >= 0) || somvr_th_expired(c, k, t)) return p;
    if (somvr_th_in_view(c, d)) {
        p.cue = SOMVR_THC_GLOW;
        p.level = clamp01(c->glow) * somvr_th_glow_level(c, k, t);
        return p;
    }
    p.cue = SOMVR_THC_EDGE;
    p.side = somvr_th_side(d);
    if (!p.side) p.side = SOMVR_TH_BOTH;
    p.level = clamp01(c->edge) * somvr_th_edge_level(c, k, t);
    int before = !(k->impact >= 0) || t <= k->impact;
    if (before && !s->announced) {
        s->announced = 1;
        p.haptic = SOMVR_THH_ANNOUNCE;
        p.sound = !(p.lead < c->sound_min_s);
        p.late = p.lead < 1.0f;
        if (p.lead <= c->urgent_s) s->urgent = 1; /* the announcement is the urgent pulse too */
    } else if (before && !s->urgent && p.lead <= c->urgent_s && p.lead >= 0) {
        s->urgent = 1;
        p.haptic = SOMVR_THH_URGENT;
    }
    return p;
}

somvr_th_pick somvr_th_near(const somvr_th_cfg *c, somvr_th_dir d) {
    somvr_th_pick p;
    memset(&p, 0, sizeof p);
    p.lead = NAN;
    float m = d.dist_cm * 0.01f;
    if (!(c->near_m > 0) || somvr_th_in_view(c, d) || !(m <= c->near_m)) return p;
    float l = c->near_m > 1.0f ? 1 - smooth(1.0f, c->near_m, m) : 1.0f;
    p.cue = SOMVR_THC_NEAR;
    p.side = somvr_th_side(d);
    if (!p.side) p.side = SOMVR_TH_BOTH;
    p.level = clamp01(c->edge) * clamp01(c->near_level) * l;
    return p;
}

int somvr_th_parry_tick(const somvr_th_keys *k, float t, somvr_th_state *s) {
    if (s->ticked || !somvr_th_window(k, t)) return 0;
    s->ticked = 1;
    return 1;
}

somvr_th_proj somvr_th_proj_eval(const somvr_th_cfg *c, const float pos[3], const float dir[3], float speed_cms,
                                 const float head[3], const float chest[3]) {
    somvr_th_proj r = {0, NAN, NAN, 0};
    float n = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(n > 1e-6f) || !(speed_cms > 0)) return r;
    float u[3] = {dir[0] / n, dir[1] / n, dir[2] / n};
    const float *tg[2] = {head, chest};
    for (int i = 0; i < 2; i++) {
        float w[3] = {tg[i][0] - pos[0], tg[i][1] - pos[1], tg[i][2] - pos[2]};
        float s = w[0] * u[0] + w[1] * u[1] + w[2] * u[2];
        if (!(s > 0)) continue; /* already past (or level with) it */
        float e[3] = {w[0] - s * u[0], w[1] - s * u[1], w[2] - s * u[2]};
        float miss = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        if (!(miss < r.miss_cm) && r.miss_cm == r.miss_cm) continue;
        r.miss_cm = miss, r.tca_s = s / speed_cms, r.at_head = i == 0;
    }
    r.inbound = r.miss_cm <= c->proj_r_cm && r.tca_s <= c->proj_t_s;
    return r;
}

somvr_th_pick somvr_th_proj_pick(const somvr_th_cfg *c, somvr_th_dir d, const somvr_th_proj *pr, int *announced) {
    somvr_th_pick p;
    memset(&p, 0, sizeof p);
    p.lead = pr->tca_s;
    if (!pr->inbound) return p;
    float l = 0.6f + 0.4f * (1 - clamp01(c->proj_t_s > 0 ? pr->tca_s / c->proj_t_s : 1.0f));
    int in = somvr_th_in_view(c, d);
    p.cue = in ? SOMVR_THC_PROJ_GLOW : SOMVR_THC_PROJ_EDGE;
    p.level = clamp01(in ? c->glow : c->edge) * l;
    p.side = somvr_th_side(d); /* the edge's side; in view too: the pulse's */
    if (!p.side) p.side = SOMVR_TH_BOTH;
    if (!*announced) {
        *announced = 1;
        p.haptic = SOMVR_THH_ANNOUNCE;
    }
    return p;
}

void somvr_th_to_xr(const somvr_th_map *m, const float w[3], float out[3]) {
    float k = m->s >= 0.5f && m->s <= 2.0f ? 1.0f / m->s : 1.0f; /* the player size */
    float v[3] = {k * (w[0] - m->b[0]), k * (w[1] - m->b[1]), k * (w[2] - m->b[2])}, ca = cosf(m->a), sa = sinf(m->a);
    float x = v[0] * ca - v[2] * sa, z = v[0] * sa + v[2] * ca; /* Y(-a) v */
    out[0] = (x + m->p0[0]) * 0.01f;
    out[1] = (v[1] + m->p0[1]) * 0.01f;
    out[2] = -(z + m->p0[2]) * 0.01f;
}

float somvr_th_glow_size(const somvr_th_cfg *c, float dist_cm) {
    float s = 2.0f * dist_cm * 0.01f * tanf(0.5f * c->glow_deg / TH_DEG);
    return !(s >= c->glow_min_m) ? c->glow_min_m : s > c->glow_max_m ? c->glow_max_m : s;
}

void somvr_th_face(const float pos[3], const float eye[3], float q[4]) {
    float d[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 1e-6f)) {
        q[0] = q[1] = q[2] = 0, q[3] = 1;
        return;
    }
    float y = d[1] / n, yaw = atan2f(d[0], d[2]), pitch = -asinf(y > 1 ? 1 : y < -1 ? -1 : y);
    float sy = sinf(0.5f * yaw), cy = cosf(0.5f * yaw), sx = sinf(0.5f * pitch), cx = cosf(0.5f * pitch);
    q[0] = cy * sx, q[1] = sy * cx, q[2] = -sy * sx, q[3] = cy * cx; /* Y(yaw) (x) X(pitch) */
}

void somvr_th_edge_quad(const float fov[2][4], int side, float dist_m, float pose[7], float size[2]) {
    float l = 0, r = 0, u = 0, dn = 0;
    for (int e = 0; e < 2; e++) {
        float tl = tanf(fov[e][0]), tr = tanf(fov[e][1]), tu = tanf(fov[e][2]), td = tanf(fov[e][3]);
        l = e == 0 || tl < l ? tl : l;
        r = e == 0 || tr > r ? tr : r;
        u = e == 0 || tu > u ? tu : u;
        dn = e == 0 || td < dn ? td : dn;
    }
    float edge = side == SOMVR_TH_RIGHT ? r : l;
    float in = edge * (1 - SOMVR_TH_EDGE_IN), out = edge * (1 + SOMVR_TH_EDGE_OUT);
    pose[0] = pose[1] = 0;
    pose[2] = side == SOMVR_TH_RIGHT ? 1.0f : 0.0f; /* the right band: the left one turned 180 deg about the view axis */
    pose[3] = side == SOMVR_TH_RIGHT ? 0.0f : 1.0f;
    pose[4] = dist_m * 0.5f * (in + out);
    pose[5] = dist_m * 0.5f * (u + dn);
    pose[6] = -dist_m;
    size[0] = dist_m * fabsf(out - in);
    size[1] = dist_m * (u - dn) * 1.1f;
}

float somvr_th_glow_profile(float r) {
    if (!(r < 1)) return 0;
    float a = 1 - r * r;
    return a * a;
}

float somvr_th_edge_profile(float u, float v) {
    float h = 1 - smooth(0, 1, u);
    return h * h * smooth(0, 0.2f, v) * smooth(0, 0.2f, 1 - v);
}

void somvr_th_image(uint8_t *px, int bgra) {
    memset(px, 0, (size_t)SOMVR_TH_IMG_W * SOMVR_TH_IMG_H * 4);
    for (int row = 0; row < 3; row++)
        for (int k = 1; k <= SOMVR_TH_LEVELS; k++) {
            int32_t rc[4];
            somvr_th_rect(row, k, rc);
            float lv = (float)k / SOMVR_TH_LEVELS;
            for (int j = 0; j < SOMVR_TH_CELL; j++)
                for (int i = 0; i < SOMVR_TH_CELL; i++) {
                    float u = ((float)i + 0.5f) / SOMVR_TH_CELL, v = ((float)j + 0.5f) / SOMVR_TH_CELL;
                    float p = row == SOMVR_TH_ROW_GLOW ? somvr_th_glow_profile(hypotf(2 * u - 1, 2 * v - 1))
                                                       : somvr_th_edge_profile(u, v);
                    float w = p * lv;
                    uint8_t *o = px + 4 * ((size_t)(rc[1] + j) * SOMVR_TH_IMG_W + (size_t)(rc[0] + i));
                    int hurt = row == SOMVR_TH_ROW_HURT;
                    float R = hurt ? SOMVR_TH_HURT_R : SOMVR_TH_R, G = hurt ? SOMVR_TH_HURT_G : SOMVR_TH_G,
                          B = hurt ? SOMVR_TH_HURT_B : SOMVR_TH_B;
                    uint8_t cr = (uint8_t)lrintf(255.0f * w * R), cg = (uint8_t)lrintf(255.0f * w * G),
                            cb = (uint8_t)lrintf(255.0f * w * B);
                    o[0] = bgra ? cb : cr, o[1] = cg, o[2] = bgra ? cr : cb;
                    o[3] = (uint8_t)lrintf(255.0f * w * (hurt ? SOMVR_TH_HURT_ALPHA : SOMVR_TH_ALPHA));
                }
        }
}

int somvr_th_hurt_side(uint8_t hurt, float edge, int *cell) {
    int hc = somvr_th_cell((float)hurt / 255.0f), ec = somvr_th_cell(edge);
    if (hc > 0 && hc >= ec) {
        *cell = hc;
        return 1;
    }
    *cell = ec;
    return 0;
}

int somvr_th_cell(float level) {
    if (!(level > 0)) return 0;
    long k = lrintf(level * SOMVR_TH_LEVELS);
    return k < 0 ? 0 : k > SOMVR_TH_LEVELS ? SOMVR_TH_LEVELS : (int)k;
}

void somvr_th_rect(int row, int cell, int32_t rc[4]) {
    int k = cell < 1 ? 1 : cell > SOMVR_TH_LEVELS ? SOMVR_TH_LEVELS : cell;
    rc[0] = (k - 1) * SOMVR_TH_STRIDE + 1;
    rc[1] = (row < 0 ? 0 : row > SOMVR_TH_ROW_HURT ? SOMVR_TH_ROW_HURT : row) * SOMVR_TH_STRIDE + 1;
    rc[2] = rc[3] = SOMVR_TH_CELL;
}
