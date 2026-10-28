/* somvr_combat.c - see somvr_combat.h */
#include "somvr_combat.h"

#include <math.h>
#include <string.h>

void sc_cfg_default(sc_cfg *c) {
    memset(c, 0, sizeof *c);
    c->blade_r = 3.0f;
    c->slash_mps = 3.0f, c->slash_cm = 15.0f;
    c->heavy_mps = 6.0f, c->heavy_cm = 40.0f;
    c->stab_mps = 1.8f, c->stab_cos = 0.8f;
    c->window_s = 0.08f;
    c->end_frac = 0.75f;
    c->cool_s = 0.25f;
    c->charges = 3, c->charge_s = 0.25f;
    c->stim_cm = 150.0f, c->stim_cos = 0.5f;
    c->steps = 4;
    c->cut_cm = 50.0f, c->edge_max_deg = 60.0f;
    c->stroke = 0, c->rev_cos = -0.3f, c->cut_mps = 12.0f, c->full_mps = 20.0f, c->cut_min_k = 0.6f, c->tip_max_mps = 0;
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static void lerp3(const float a[3], const float b[3], float u, float o[3]) {
    for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * u;
}

/* ---- geometry ---------------------------------------------------------------------------------------------------- */

float sc_seg_dist(const float p0[3], const float p1[3], const float q0[3], const float q1[3], float *s, float *t) {
    const float eps = 1e-6f;
    float d1[3], d2[3], r[3];
    for (int i = 0; i < 3; i++) d1[i] = p1[i] - p0[i], d2[i] = q1[i] - q0[i], r[i] = p0[i] - q0[i];
    float a = dot3(d1, d1), e = dot3(d2, d2), f = dot3(d2, r), sv = 0, tv = 0;
    if (a <= eps && e <= eps) {
        sv = tv = 0;
    } else if (a <= eps) {
        tv = clamp01(f / e);
    } else {
        float c = dot3(d1, r);
        if (e <= eps) {
            sv = clamp01(-c / a);
        } else {
            float b = dot3(d1, d2), den = a * e - b * b;
            sv = den > eps * a * e ? clamp01((b * f - c * e) / den) : 0; /* parallel: any s, take 0 */
            tv = (b * sv + f) / e;
            if (tv < 0) tv = 0, sv = clamp01(-c / a);
            else if (tv > 1) tv = 1, sv = clamp01((b - c) / a);
        }
    }
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = p0[i] + d1[i] * sv - (q0[i] + d2[i] * tv);
    if (s) *s = sv;
    if (t) *t = tv;
    return sqrtf(dot3(d, d));
}

int sc_sweep_capsule(const somvr_blade *prev, const somvr_blade *cur, float blade_r, const float c0[3], const float c1[3],
                     float cap_r, int steps, sc_contact *out) {
    if (!cur || !cur->valid) return 0;
    if (steps < 1) steps = 1;
    int from = prev && prev->valid ? 1 : steps; /* without a previous pose: this frame's pose only */
    for (int k = from; k <= steps; k++) {
        float u = (float)k / (float)steps, b[3], tp[3], s, t;
        if (k == steps) {
            memcpy(b, cur->base, sizeof b), memcpy(tp, cur->tip, sizeof tp);
        } else {
            lerp3(prev->base, cur->base, u, b), lerp3(prev->tip, cur->tip, u, tp);
        }
        float d = sc_seg_dist(b, tp, c0, c1, &s, &t);
        if (!(d <= blade_r + cap_r)) continue; /* also NaN */
        float p[3], q[3];
        lerp3(b, tp, s, p), lerp3(c0, c1, t, q);
        out->u = u, out->along = s, out->dist = d;
        for (int i = 0; i < 3; i++) /* on the surface toward the blade; inside the axis itself: the blade point */
            out->point[i] = d > 1e-3f ? q[i] + (p[i] - q[i]) * (cap_r / d) : p[i];
        return 1;
    }
    return 0;
}

int sc_heading_at(const sc_cfg *c, const somvr_blade *b, const float c0[3], const float c1[3], float cap_r, float *dist,
                  float *cosv) {
    float t, q[3];
    sc_seg_dist(b->tip, b->tip, c0, c1, NULL, &t);
    lerp3(c0, c1, t, q);
    float d[3] = {q[0] - b->tip[0], q[1] - b->tip[1], q[2] - b->tip[2]};
    *dist = sqrtf(dot3(d, d)) - cap_r;
    float hv = sqrtf(b->vel_tip[0] * b->vel_tip[0] + b->vel_tip[2] * b->vel_tip[2]), hd = sqrtf(d[0] * d[0] + d[2] * d[2]);
    *cosv = hv > 1e-3f && hd > 1e-3f ? (b->vel_tip[0] * d[0] + b->vel_tip[2] * d[2]) / (hv * hd) : 0;
    return b->valid && *dist <= c->stim_cm && *cosv >= c->stim_cos;
}

/* ---- swing speed ------------------------------------------------------------------------------------------------- */

void sc_speed_reset(sc_speed *s) { memset(s, 0, sizeof *s); }

/* time-weighted mean of field f (0 speed, 1 axis, 2 base) over [t_now - w, t_now]: each sample covers the time since
 * the one before it (the oldest sample in the ring: nothing before it) */
#define WF(x) (f == 2 ? (x).base : f ? (x).axis : (x).speed)
static float window_mean(const sc_speed *s, float w, int f) {
    if (s->n == 0) return 0;
    int i = (s->head + SC_RING - 1) % SC_RING;
    double now = s->s[i].t, lo = now - w, sum = 0, span = 0;
    for (int k = 0; k < s->n - 1; k++) {
        int j = (i + SC_RING - 1) % SC_RING;
        double a = s->s[j].t > lo ? s->s[j].t : lo, dt = s->s[i].t - a;
        if (dt > 0) sum += dt * WF(s->s[i]), span += dt;
        if (s->s[j].t <= lo) break;
        i = j;
    }
    if (span <= 0) return WF(s->s[i]); /* one sample */
    return (float)(sum / span);
}
#undef WF

static void stroke_reset(sc_speed *s) {
    s->reach = s->stroke_cm = 0, s->have_dir = 0, s->rest_s = 0;
    memset(s->disp, 0, sizeof s->disp), memset(s->dir, 0, sizeof s->dir);
}
/* the stroke (build 20): a turn back of the tip past c->rev_cos off its direction, or a rest, begins a new one */
static void stroke_step(sc_speed *s, const sc_cfg *c, const float v[3], float sp, double dt, double t) {
    if (sp < SC_STROKE_MIN_CMS) {
        s->rest_s += (float)dt;
        if (s->rest_s >= SC_STROKE_REST_S && (s->have_dir || s->reach > 0)) stroke_reset(s);
    } else {
        s->rest_s = 0;
        float dl = sqrtf(dot3(s->dir, s->dir));
        if (s->have_dir && dl > 1e-3f && dot3(v, s->dir) / (sp * dl) < c->rev_cos) stroke_reset(s);
        if (!s->have_dir) {
            memcpy(s->dir, v, sizeof s->dir), s->have_dir = 1, s->strokes++, s->stroke_t0 = t;
        } else {
            float a = 1.0f - expf(-(float)dt / SC_STROKE_TAU_S);
            for (int i = 0; i < 3; i++) s->dir[i] += a * (v[i] - s->dir[i]);
        }
    }
    if (!s->have_dir) return; /* a rest before any stroke: nothing to measure */
    for (int i = 0; i < 3; i++) s->disp[i] += (float)(v[i] * dt);
    s->stroke_cm += (float)(sp * dt);
    float r = sqrtf(dot3(s->disp, s->disp));
    if (r > s->reach) s->reach = r;
}

int sc_speed_push(sc_speed *s, const sc_cfg *c, const somvr_blade *b) {
    if (!b->valid || (s->n && !(b->t > s->last_t))) return 0;
    float v[3] = {b->vel_tip[0], b->vel_tip[1], b->vel_tip[2]};
    float ax[3] = {b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2]};
    float sp = sqrtf(dot3(v, v)), la = sqrtf(dot3(ax, ax)), sb = sqrtf(dot3(b->vel_base, b->vel_base));
    float along = la > 1e-3f ? dot3(v, ax) / la : 0;
    if (!isfinite(sp) || !isfinite(along) || !isfinite(sb)) return 0;
    if (c->tip_max_mps > 0 && (sp > 100.0f * c->tip_max_mps || sb > 100.0f * c->tip_max_mps)) {
        s->drops++; /* a tracking jump (the next sample's dt spans it) */
        return 0;
    }
    double dt = s->n ? b->t - s->last_t : 0;
    s->s[s->head].t = b->t, s->s[s->head].speed = sp, s->s[s->head].axis = along, s->s[s->head].base = sb;
    s->head = (s->head + 1) % SC_RING;
    if (s->n < SC_RING) s->n++;
    s->last_t = b->t;
    s->mean_mps = window_mean(s, c->window_s, 0) * 0.01f;
    s->axis_mps = window_mean(s, c->window_s, 1) * 0.01f;
    s->base_mps = window_mean(s, c->window_s, 2) * 0.01f;
    s->cos_axis = sp > 1e-3f && la > 1e-3f ? along / sp : 0;
    stroke_step(s, c, v, sp, dt > 0 && dt <= 0.25 ? dt : 0, b->t);
    if (!s->above && s->mean_mps >= c->slash_mps) {
        s->above = 1, s->travel = 0;
        return 1;
    }
    if (s->above && s->mean_mps < c->end_frac * c->slash_mps) {
        s->above = 0;
        return -1;
    }
    if (s->above) s->travel += (float)(sp * dt);
    return 0;
}

int sc_tier(const sc_cfg *c, const sc_speed *s) {
    if (s->axis_mps >= c->stab_mps && s->cos_axis >= c->stab_cos) return SC_TIER_STAB;
    if (!s->above) return SC_TIER_NONE;
    if (s->mean_mps >= c->heavy_mps && s->travel >= c->heavy_cm) return SC_TIER_HEAVY;
    if (s->mean_mps >= c->slash_mps && s->travel >= c->slash_cm) return SC_TIER_LIGHT;
    return SC_TIER_NONE;
}

float sc_edge_deg(const somvr_blade *b) {
    float ax[3] = {b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2]}, l = sqrtf(dot3(ax, ax));
    float nn = sqrtf(dot3(b->flat_n, b->flat_n));
    if (!(l > 1e-3f) || !(nn > 0.5f))
        return NAN;
    float a = dot3(b->vel_tip, ax) / (l * l), v[3];
    for (int i = 0; i < 3; i++) v[i] = b->vel_tip[i] - a * ax[i];
    float vl = sqrtf(dot3(v, v));
    if (!(vl > 1.0f)) /* under 1 cm/s across the blade */
        return NAN;
    float c = fabsf(dot3(v, b->flat_n)) / (vl * nn);
    return asinf(c > 1 ? 1 : c) * 57.29578f;
}

int sc_hit_kind(int tier, float edge_deg, float edge_max_deg) {
    if (tier == SC_TIER_HEAVY) return SC_KIND_CUT;
    if (tier == SC_TIER_STAB) return SC_KIND_STAB;
    if (tier != SC_TIER_LIGHT) return SC_KIND_NONE;
    return edge_deg > edge_max_deg ? SC_KIND_SLAP : SC_KIND_TAP;
}

const char *sc_kind_name(int kind) {
    static const char *const n[SC_KIND_N] = {"none", "cut", "slap", "tap", "stab"};
    return kind > 0 && kind < SC_KIND_N ? n[kind] : n[0];
}

int sc_kind_bites(int kind, int glance, int blocked) { return kind == SC_KIND_CUT && !glance && !blocked; }

int sc_tier_cut(const sc_cfg *c, const sc_speed *s, float edge_deg) {
    if (s->axis_mps >= c->stab_mps && s->cos_axis >= c->stab_cos) return SC_TIER_STAB;
    if (!s->above || !(s->mean_mps >= c->slash_mps)) return SC_TIER_NONE;
    if (s->travel >= c->cut_cm && !(edge_deg > c->edge_max_deg)) return SC_TIER_HEAVY;
    if (s->travel >= c->slash_cm) return SC_TIER_LIGHT;
    return SC_TIER_NONE;
}

float sc_contact_mps(const sc_speed *s, float along) {
    float u = isfinite(along) ? clamp01(along) : 1;
    return s->base_mps + (s->mean_mps - s->base_mps) * u;
}

int sc_tier_swing(const sc_cfg *c, const sc_speed *s, float edge_deg, float along) {
    if (s->axis_mps >= c->stab_mps && s->cos_axis >= c->stab_cos) return SC_TIER_STAB;
    if (!s->above || !(s->mean_mps >= c->slash_mps)) return SC_TIER_NONE;
    float v = sc_contact_mps(s, along);
    if (s->reach >= c->cut_cm && v >= c->cut_mps && !(edge_deg > c->edge_max_deg)) return SC_TIER_HEAVY;
    if (s->reach >= c->slash_cm && v >= c->slash_mps) return SC_TIER_LIGHT;
    return SC_TIER_NONE;
}

int sc_stroke_entered(const sc_speed *s, float touch_age, double now) {
    return s->have_dir && isfinite(touch_age) && touch_age <= (float)(now - s->stroke_t0) + 1e-3f;
}

float sc_cut_k(const sc_cfg *c, float v) {
    float lo = c->cut_min_k;
    if (!(c->full_mps > c->cut_mps)) return 1.0f;
    if (!isfinite(v)) return lo;
    float k = lo + (1.0f - lo) * (v - c->cut_mps) / (c->full_mps - c->cut_mps);
    return k < lo ? lo : k > 1 ? 1 : k;
}

int sc_space_hold(const float root[3], const float was[3], float xf[3], float space, float *d_want, float *d_got) {
    float dx = xf[0] - root[0], dz = xf[2] - root[2];
    *d_want = *d_got = NAN;
    if (!(space > 0) || !isfinite(dx) || !isfinite(dz) || fabsf(dx) >= space || fabsf(dz) >= space) return 0;
    float d = sqrtf(dx * dx + dz * dz), d0 = hypotf(was[0] - root[0], was[2] - root[2]), lim = fminf(space, d0);
    *d_want = d;
    if (!(d < lim) || !isfinite(d0)) return 0;
    if (d > 1.0f)
        xf[0] = root[0] + dx * (lim / d), xf[2] = root[2] + dz * (lim / d);
    else
        xf[0] = was[0], xf[2] = was[2];
    *d_got = lim;
    return 1;
}

static float hdist(const float a[3], const float b[3]) { return hypotf(a[0] - b[0], a[2] - b[2]); }
int sc_space_hold2(const float root[3], const float *head, const float was[3], float xf[3], float space, float *d_want,
                   float *d_got) {
    const float *c[2] = {root, head};
    int n = head ? 2 : 1;
    float lim[2], d[2];
    *d_want = *d_got = NAN;
    if (!(space > 0) || !isfinite(xf[0]) || !isfinite(xf[2])) return 0;
    int bad = 0;
    for (int i = 0; i < n; i++) {
        if (!isfinite(c[i][0]) || !isfinite(c[i][2])) return 0;
        d[i] = hdist(xf, c[i]), lim[i] = fminf(space, hdist(was, c[i]));
        bad |= d[i] < lim[i];
    }
    *d_want = n == 2 ? fminf(d[0], d[1]) : d[0];
    if (!bad) return 0;
    float best[3] = {was[0], xf[1], was[2]}, bd = hdist(xf, was);
    for (int i = 0; i < n; i++) {
        if (!(d[i] < lim[i]) || !(d[i] > 1.0f)) continue;
        float p[3] = {c[i][0] + (xf[0] - c[i][0]) * (lim[i] / d[i]), xf[1], c[i][2] + (xf[2] - c[i][2]) * (lim[i] / d[i])};
        int ok = 1;
        for (int j = 0; j < n; j++) ok &= !(hdist(p, c[j]) < lim[j] - 0.01f);
        float pd = hdist(xf, p);
        if (ok && pd < bd) memcpy(best, p, sizeof best), bd = pd;
    }
    xf[0] = best[0], xf[2] = best[2];
    *d_got = n == 2 ? fminf(hdist(xf, c[0]), hdist(xf, c[1])) : hdist(xf, c[0]);
    return 1;
}

int sc_reel_cap(const float anchor[3], const float *talion, const float was[3], float xf[3], float cap, float *cut) {
    *cut = 0;
    if (!(cap > 0) || !isfinite(xf[0]) || !isfinite(xf[2]) || !isfinite(was[0]) || !isfinite(was[2]) ||
        !isfinite(anchor[0]) || !isfinite(anchor[2]))
        return 0;
    if (talion && isfinite(talion[0]) && isfinite(talion[2]) && hdist(xf, talion) < hdist(was, talion)) return 0;
    float sx = xf[0] - was[0], sz = xf[2] - was[2], rx = was[0] - anchor[0], rz = was[2] - anchor[2];
    float d = hypotf(rx, rz), knee = 0.5f * cap;
    if (d > 1e-3f) {
        float ux = rx / d, uz = rz / d, r = sx * ux + sz * uz; /* the part away from the anchor */
        if (r > 0 && d > knee) {
            float k = d < cap ? (cap - d) / (cap - knee) : 0.0f;
            sx -= (1 - k) * r * ux, sz -= (1 - k) * r * uz;
        }
    }
    float nx = was[0] + sx, nz = was[2] + sz, e = hypotf(nx - anchor[0], nz - anchor[2]), lim = fmaxf(cap, d);
    if (e > lim)
        nx = anchor[0] + (nx - anchor[0]) * (lim / e), nz = anchor[2] + (nz - anchor[2]) * (lim / e);
    float c = hypotf(xf[0] - nx, xf[2] - nz);
    if (!(c > 0.01f)) return 0;
    *cut = c, xf[0] = nx, xf[2] = nz;
    return 1;
}

float sc_body_need(const float eye[3], const float root[3], const float (*bone)[3], int n, float body, float max_cm,
                   int *worst) {
    *worst = -1;
    float ux = root[0] - eye[0], uz = root[2] - eye[2], r = hypotf(ux, uz), best = 0;
    if (!(body > 0) || !(r > 1.0f) || !isfinite(r)) return 0;
    ux /= r, uz /= r;
    for (int i = 0; i < n; i++) {
        float bx = bone[i][0] - eye[0], by = bone[i][1] - eye[1], bz = bone[i][2] - eye[2];
        if (!isfinite(bx) || !isfinite(by) || !isfinite(bz)) continue;
        float req2 = body * body - by * by, h2 = bx * bx + bz * bz; /* the distance across it needs at its height */
        if (!(req2 > h2)) continue;                                /* out already */
        float bu = bx * ux + bz * uz, d = -bu + sqrtf(bu * bu - h2 + req2); /* |b + d u| = sqrt(req2), d > 0 */
        if (d > best) best = d, *worst = i;
    }
    return best > 0 ? r + fminf(best, max_cm) : 0;
}

int sc_body_push(const float eye[3], const float was[3], float xf[3], float need, float push, float *pushed) {
    *pushed = 0;
    if (!(need > 0) || !isfinite(xf[0]) || !isfinite(xf[2]) || !isfinite(was[0]) || !isfinite(was[2]) ||
        !isfinite(eye[0]) || !isfinite(eye[2]))
        return 0;
    float dx = xf[0] - eye[0], dz = xf[2] - eye[2], rx = hypotf(dx, dz), rw = hdist(was, eye);
    if (!(rx < need)) return 0;
    if (!(rx >= 1.0f)) { /* onto the eyes: out the way it came */
        if (!(rw >= 1.0f)) return 0;
        dx = was[0] - eye[0], dz = was[2] - eye[2];
    }
    float l = hypotf(dx, dz), to = fmaxf(rx, fminf(need, rw + (push > 0 ? push : 0)));
    if (!(to - rx > 0.01f)) return 0;
    xf[0] = eye[0] + dx / l * to, xf[2] = eye[2] + dz / l * to;
    *pushed = to - rx;
    return 1;
}

const char *sc_tier_name(int tier) {
    static const char *const n[SC_TIER_N] = {"none", "light", "heavy", "stab"};
    return tier >= 0 && tier < SC_TIER_N ? n[tier] : "?";
}

/* ---- re-hit cooldown and hit charges ----------------------------------------------------------------------------- */

void sc_cool_reset(sc_cool *c) { memset(c, 0, sizeof *c); }

int sc_cool_ready(const sc_cool *c, uintptr_t key, double now, float cool_s) {
    for (int i = 0; i < SC_COOL_N; i++)
        if (c->key[i] == key && key) return now - c->t[i] >= cool_s;
    return 1;
}

void sc_cool_mark(sc_cool *c, uintptr_t key, double now) {
    int pick = -1;
    for (int i = 0; i < SC_COOL_N && pick < 0; i++)
        if (c->key[i] == key) pick = i;
    for (int i = 0; i < SC_COOL_N && pick < 0; i++)
        if (!c->key[i]) pick = i;
    if (pick < 0) { /* full: the oldest mark goes */
        pick = 0;
        for (int i = 1; i < SC_COOL_N; i++)
            if (c->t[i] < c->t[pick]) pick = i;
    }
    c->key[pick] = key, c->t[pick] = now;
}

void sc_charges_init(sc_charges *ch, const sc_cfg *c, double now) { ch->n = (float)c->charges, ch->t = now; }

float sc_charges_now(const sc_charges *ch, const sc_cfg *c, double now) {
    double n = ch->n + (now > ch->t && c->charge_s > 0 ? (now - ch->t) / c->charge_s : 0);
    return n > c->charges ? (float)c->charges : (float)n;
}

int sc_charges_take(sc_charges *ch, const sc_cfg *c, double now) {
    float n = sc_charges_now(ch, c, now);
    ch->t = now > ch->t ? now : ch->t;
    if (n < 1.0f) {
        ch->n = n;
        return 0;
    }
    ch->n = n - 1.0f;
    return 1;
}

/* ---- the blade's touch on an orc --------------------------------------------------------------------------------- */

void sc_touch_reset(sc_touch *tc) { memset(tc, 0, sizeof *tc); }

static int touch_slot(const sc_touch *tc, uintptr_t key) {
    for (int i = 0; i < SC_TOUCH_N; i++)
        if (key && tc->key[i] == key) return i;
    return -1;
}

int sc_touch_step(sc_touch *tc, uintptr_t key, double now, float mps, float gap_s, float rub_s, float rub_mps) {
    if (!key) return SC_TOUCH_START;
    int i = touch_slot(tc, key);
    if (i >= 0 && now >= tc->last[i] && now - tc->last[i] <= gap_s) { /* the touch goes on */
        tc->last[i] = now;
        if (rub_s > 0 && mps >= rub_mps && now - tc->rub[i] >= rub_s) {
            tc->rub[i] = now;
            return SC_TOUCH_RUB;
        }
        return SC_TOUCH_ON;
    }
    if (i < 0) { /* a free slot, else the oldest touch goes */
        for (int k = 0; k < SC_TOUCH_N && i < 0; k++)
            if (!tc->key[k]) i = k;
        if (i < 0) {
            i = 0;
            for (int k = 1; k < SC_TOUCH_N; k++)
                if (tc->last[k] < tc->last[i]) i = k;
        }
    }
    tc->key[i] = key, tc->t0[i] = tc->last[i] = tc->rub[i] = now;
    return SC_TOUCH_START;
}

float sc_touch_age(const sc_touch *tc, uintptr_t key, double now, float gap_s) {
    int i = touch_slot(tc, key);
    return i >= 0 && now >= tc->last[i] && now - tc->last[i] <= gap_s ? (float)(now - tc->t0[i]) : 0.0f;
}

/* ---- blade providers --------------------------------------------------------------------------------------------- */

/* one ramped swing of `total` rad at plateau rate w: the angle done and the rate at time tau since its start */
static float ramp_swing(float tau, float total, float w, float *rate) {
    float r = SC_FAKE_RAMP_S, T = total / w + r;
    if (tau <= 0) return *rate = 0, 0.0f;
    if (tau >= T) return *rate = 0, total;
    if (tau < r) return *rate = w * tau / r, 0.5f * w * tau * tau / r;
    if (tau <= T - r) return *rate = w, w * (0.5f * r + (tau - r));
    float e = T - tau;
    return *rate = w * e / r, total - 0.5f * w * e * e / r;
}

float somvr_blade_fake_angle(double dt, float *rate) {
    const float half = SC_FAKE_HALF_DEG * 0.01745329252f, total = 2 * half;
    float ph = (float)fmod(dt, SC_FAKE_PERIOD), w;
    if (ph < 0) ph += (float)SC_FAKE_PERIOD;
    if (ph < SC_FAKE_LIGHT_T0) { /* the heavy swing, right -> left */
        float a = ramp_swing(ph - (float)SC_FAKE_HEAVY_T0, total, SC_FAKE_HEAVY_MPS * 100.0f / SC_FAKE_TIP_CM, &w);
        *rate = -w;
        return half - a;
    }
    float a = ramp_swing(ph - (float)SC_FAKE_LIGHT_T0, total, SC_FAKE_LIGHT_MPS * 100.0f / SC_FAKE_TIP_CM, &w);
    *rate = w;
    return -half + a;
}

void somvr_blade_fake(double t, double t0, const float head[3], float yaw, somvr_blade *out) {
    float rate, th = somvr_blade_fake_angle(t - t0, &rate), a = yaw + th;
    float d[3] = {sinf(a), 0, cosf(a)}, dd[3] = {cosf(a), 0, -sinf(a)}; /* blade direction and d/dtheta of it */
    float pv[3] = {head[0], head[1] - SC_FAKE_DROP_CM, head[2]};
    for (int i = 0; i < 3; i++) {
        out->base[i] = pv[i] + SC_FAKE_BASE_CM * d[i];
        out->tip[i] = pv[i] + SC_FAKE_TIP_CM * d[i];
        out->vel_tip[i] = SC_FAKE_TIP_CM * rate * dd[i];
        out->vel_base[i] = SC_FAKE_BASE_CM * rate * dd[i];
    }
    out->t = t;
    out->valid = isfinite(out->tip[0]) && isfinite(out->tip[1]) && isfinite(out->tip[2]);
}

void sc_blade_cfg_default(sc_blade_cfg *c) {
    memset(c, 0, sizeof *c);
    /* the tip where the drawn sword's own tip is: boot-check-20261024-040553 measured Urfael's sword_tip node 100.7 cm
     * along the controller blade from its grip point, 0.0 cm off its line (build 7's 90 cm reached 10 cm short of what
     * the player sees) */
    c->back_cm = 8.0f, c->len_cm = 100.0f;
}

/* R(q) v for a quaternion x y z w (Hamilton; q unit) */
static void qrot3(const float q[4], const float v[3], float o[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty);
    o[1] = v[1] + w * ty + (z * tx - x * tz);
    o[2] = v[2] + w * tz + (x * ty - y * tx);
}
static void yawrot3(float a, const float v[3], float o[3]) { /* R(Y(a)): (0,0,1) -> (sin a, 0, cos a) */
    float c = cosf(a), s = sinf(a), x = v[0], z = v[2];
    o[0] = c * x + s * z, o[1] = v[1], o[2] = -s * x + c * z;
}

float sc_rig_scale(const sc_rigxf *rig) { return rig->s >= 0.5f && rig->s <= 2.0f ? rig->s : 1.0f; }

int somvr_blade_controller(const sc_blade_cfg *c, const sc_grip *g, const sc_rigxf *rig, double t, sc_blade_hist *h,
                           somvr_blade *out) {
    /* once the arm IK (G11) exists: R_Weapon's world transform x the sword's blade instead (what you see is what hits) */
    memset(out, 0, sizeof *out);
    float n = g ? sqrtf(dot3(g->quat, g->quat) + g->quat[3] * g->quat[3]) : 0;
    if (!g || !g->valid || !(n > 0.5f && n < 2.0f) || !isfinite(g->pos[0]) || !isfinite(g->pos[1]) ||
        !isfinite(g->pos[2])) {
        h->have = 0;
        return 0;
    }
    float q[4] = {g->quat[0] / n, g->quat[1] / n, g->quat[2] / n, g->quat[3] / n}, ks = sc_rig_scale(rig), ki = 1.0f / ks;
    float dl[3] = {0, sinf(c->pitch_rad), cosf(c->pitch_rad)}, d[3], o[3], gp[3], base[3], tip[3], v[3] = {0, 0, 0};
    float ofs[3] = {ki * c->ofs_cm[0], ki * c->ofs_cm[1], ki * c->ofs_cm[2]}; /* world cm -> tracked cm */
    qrot3(q, dl, d);
    qrot3(q, ofs, o);
    for (int i = 0; i < 3; i++) {
        gp[i] = g->pos[i] + o[i];
        base[i] = gp[i] - ki * c->back_cm * d[i];
        tip[i] = gp[i] + ki * c->len_cm * d[i];
    }
    double dt = h->have ? g->xr_t - h->xr_t : -1;
    float vb[3] = {0, 0, 0}; /* the base's the same way (build 20) */
    if (c->vel_runtime && g->vel_valid) {
        float r[3] = {tip[0] - g->pos[0], tip[1] - g->pos[1], tip[2] - g->pos[2]}; /* v + w x r */
        float rb[3] = {base[0] - g->pos[0], base[1] - g->pos[1], base[2] - g->pos[2]};
        v[0] = g->vel[0] + g->avel[1] * r[2] - g->avel[2] * r[1];
        v[1] = g->vel[1] + g->avel[2] * r[0] - g->avel[0] * r[2];
        v[2] = g->vel[2] + g->avel[0] * r[1] - g->avel[1] * r[0];
        vb[0] = g->vel[0] + g->avel[1] * rb[2] - g->avel[2] * rb[1];
        vb[1] = g->vel[1] + g->avel[2] * rb[0] - g->avel[0] * rb[2];
        vb[2] = g->vel[2] + g->avel[0] * rb[1] - g->avel[1] * rb[0];
    } else if (h->have && dt > 0 && dt <= 0.1) {
        for (int i = 0; i < 3; i++)
            v[i] = (float)((tip[i] - h->tip[i]) / dt), vb[i] = (float)((base[i] - h->base[i]) / dt);
    } else if (h->have && dt == 0) {
        memcpy(v, h->vel, sizeof v); /* the same locate time again (reused views): the last velocity */
        memcpy(vb, h->vbase, sizeof vb);
    }
    memcpy(h->tip, tip, sizeof h->tip), memcpy(h->vel, v, sizeof h->vel);
    memcpy(h->base, base, sizeof h->base), memcpy(h->vbase, vb, sizeof h->vbase);
    h->xr_t = g->xr_t, h->have = 1;
    float bw[3], tw[3], vw[3], db[3] = {ks * (base[0] - rig->p0[0]), ks * (base[1] - rig->p0[1]), ks * (base[2] - rig->p0[2])},
                                  dtp[3] = {ks * (tip[0] - rig->p0[0]), ks * (tip[1] - rig->p0[1]), ks * (tip[2] - rig->p0[2])},
                                  vs[3] = {ks * v[0], ks * v[1], ks * v[2]}, vbs[3] = {ks * vb[0], ks * vb[1], ks * vb[2]};
    yawrot3(rig->yaw, db, bw), yawrot3(rig->yaw, dtp, tw), yawrot3(rig->yaw, vs, vw);
    yawrot3(rig->yaw, vbs, out->vel_base);
    float xl[3] = {1, 0, 0}, xg[3];
    qrot3(q, xl, xg);
    yawrot3(rig->yaw, xg, out->flat_n);
    for (int i = 0; i < 3; i++) {
        out->base[i] = rig->base[i] + bw[i];
        out->tip[i] = rig->base[i] + tw[i];
        out->vel_tip[i] = vw[i];
    }
    out->t = t;
    out->valid = isfinite(out->tip[0]) && isfinite(out->tip[1]) && isfinite(out->tip[2]) && isfinite(out->vel_tip[0]) &&
                 isfinite(out->vel_tip[1]) && isfinite(out->vel_tip[2]);
    if (!(isfinite(out->vel_base[0]) && isfinite(out->vel_base[1]) && isfinite(out->vel_base[2])))
        memset(out->vel_base, 0, sizeof out->vel_base);
    return out->valid;
}

/* ---- orc reactions to a swing (SOMVR_COMBAT_REACT) --------------------------------------------------------------- */

float sc_block_chance(const sc_block_entry *e, int n, int records, const uintptr_t *cats, int ncat, int *ent) {
    *ent = -1;
    if (!e || n <= 0)
        return -1.0f;
    if (!records)
        return SC_BLOCK_DEFAULT;
    for (int i = 0; i < n; i++) {
        int hit = !e[i].cat;
        for (int k = 0; k < ncat && !hit; k++)
            hit = cats[k] == e[i].cat;
        if (hit) {
            *ent = i;
            return e[i].chance;
        }
    }
    return -1.0f;
}

int sc_block_won(float chance, float roll) { return chance >= 0 && chance > roll; } /* NaN: no */

void sc_react_reset(sc_react *r) { memset(r, 0, sizeof *r); }

void sc_react_start(sc_react *r, double t) {
    sc_react_reset(r);
    r->on = 1, r->t0 = t;
}

void sc_react_end(sc_react *r) { r->on = 0; }

int sc_react_open(const sc_react *r, double t) { return r->on && t >= r->t0 && t - r->t0 <= SC_REACT_WINDOW_S; }

int sc_react_take(sc_react *r, uintptr_t key, double t) {
    if (!key || !sc_react_open(r, t))
        return 0;
    for (int i = 0; i < r->n; i++)
        if (r->key[i] == key)
            return 0;
    if (r->n >= SC_REACT_N)
        return 0;
    r->key[r->n++] = key;
    return 1;
}

void sc_react_cfg_default(sc_react_cfg *c) {
    c->chance[SC_ARCH_DEFENDER] = SC_REACT_DEFENDER;
    c->chance[SC_ARCH_BERSERKER] = SC_REACT_BERSERKER;
    c->chance[SC_ARCH_OTHER] = SC_REACT_OTHER;
}

static int name_is(const char *s, const char *stem) { /* stem, then the end or '_' */
    size_t n = strlen(stem);
    return !strncmp(s, stem, n) && (s[n] == 0 || s[n] == '_');
}

int sc_archetype(const char *record) {
    if (!record)
        return SC_ARCH_OTHER;
    if (name_is(record, "AI_Defender"))
        return SC_ARCH_DEFENDER;
    if (name_is(record, "AI_Berserker"))
        return SC_ARCH_BERSERKER;
    return SC_ARCH_OTHER;
}

const char *sc_archetype_name(int arch) {
    static const char *const n[SC_ARCH_N] = {"defender", "berserker", "other"};
    return arch >= 0 && arch < SC_ARCH_N ? n[arch] : "?";
}

int sc_in_own_attack(uintptr_t attack, uint8_t cancel0, uint8_t cancel1) { return attack && !cancel0 && !cancel1; }

float sc_react_direct_chance(const sc_react_cfg *c, int arch, int in_attack) {
    if (in_attack || arch < 0 || arch >= SC_ARCH_N)
        return -1.0f;
    float ch = c->chance[arch];
    return ch > 0 ? (ch < 1 ? ch : 1.0f) : -1.0f; /* NaN: no call */
}

/* ---- SOMVR_HANDS_TEST's scripted swing ---------------------------------------------------------------------------- */

float sc_swing_ease(float u) {
    if (!(u > 0)) return 0; /* also NaN */
    if (u >= 1) return 1;
    return u * u * u * (10 + u * (-15 + 6 * u));
}

static void qmul4(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}

void sc_euler_quat(const float deg[3], float q[4]) {
    const float k = 0.5f * 0.01745329252f;
    float qx[4] = {sinf(deg[0] * k), 0, 0, cosf(deg[0] * k)}, qy[4] = {0, sinf(deg[1] * k), 0, cosf(deg[1] * k)},
          qz[4] = {0, 0, sinf(deg[2] * k), cosf(deg[2] * k)};
    qmul4(qx, qy, q), qmul4(q, qz, q);
}

int sc_swing_pose(const float pa[3], const float qa[4], const float pb[3], const float qb[4], const float pivot[3],
                  float u, float pos[3], float q[4]) {
    float na = sqrtf(dot3(qa, qa) + qa[3] * qa[3]), nb = sqrtf(dot3(qb, qb) + qb[3] * qb[3]);
    for (int i = 0; i < 3; i++)
        if (!isfinite(pa[i]) || !isfinite(pb[i]) || !isfinite(pivot[i])) return 0;
    if (!(na > 1e-6f && nb > 1e-6f) || !isfinite(na) || !isfinite(nb)) return 0;
    float k = sc_swing_ease(u);
    if (k <= 0 || k >= 1) { /* the ends exactly */
        const float *p = k <= 0 ? pa : pb, *r = k <= 0 ? qa : qb, n = k <= 0 ? na : nb;
        for (int i = 0; i < 4; i++) q[i] = r[i] / n;
        memcpy(pos, p, 3 * sizeof *pos);
        return 1;
    }
    /* the position: an arc about the pivot */
    float a[3], b[3];
    for (int i = 0; i < 3; i++) a[i] = pa[i] - pivot[i], b[i] = pb[i] - pivot[i];
    float la = sqrtf(dot3(a, a)), lb = sqrtf(dot3(b, b)), c = la > 1 && lb > 1 ? dot3(a, b) / (la * lb) : -2;
    if (c < -0.99985f) { /* an end at the pivot, or the two directions (nearly) opposite: a line */
        lerp3(pa, pb, k, pos);
    } else {
        float th = acosf(c > 1 ? 1 : c), s = sinf(th), wa = 1 - k, wb = k, r = la + (lb - la) * k;
        if (s > 1e-4f) wa = sinf((1 - k) * th) / s, wb = sinf(k * th) / s;
        float d[3];
        for (int i = 0; i < 3; i++) d[i] = wa * a[i] / la + wb * b[i] / lb;
        float ld = sqrtf(dot3(d, d));
        for (int i = 0; i < 3; i++) pos[i] = pivot[i] + r * d[i] / ld;
    }
    /* the orientation: slerp, the shorter way */
    float x[4] = {qa[0] / na, qa[1] / na, qa[2] / na, qa[3] / na}, y[4] = {qb[0] / nb, qb[1] / nb, qb[2] / nb, qb[3] / nb};
    float cq = dot3(x, y) + x[3] * y[3];
    if (cq < 0) cq = -cq, y[0] = -y[0], y[1] = -y[1], y[2] = -y[2], y[3] = -y[3];
    float wx = 1 - k, wy = k;
    if (cq < 0.9999f) {
        float th = acosf(cq), s = sinf(th);
        wx = sinf((1 - k) * th) / s, wy = sinf(k * th) / s;
    }
    float n = 0;
    for (int i = 0; i < 4; i++) q[i] = wx * x[i] + wy * y[i], n += q[i] * q[i];
    n = sqrtf(n);
    for (int i = 0; i < 4; i++) q[i] /= n;
    return 1;
}
