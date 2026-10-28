/* somvr_surface.c - the hands and the sword stopped at surfaces (somvr_surface.h) */
#include "somvr_surface.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static sv_vec3 add3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static sv_vec3 sub3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static sv_vec3 mul3(sv_vec3 a, float k) { return (sv_vec3){ a.x * k, a.y * k, a.z * k }; }
static float dot3v(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len3v(sv_vec3 a) { return sqrtf(dot3v(a, a)); }
static int fin3(sv_vec3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }

void somvr_sf_cfg_default(somvr_sf_cfg *c) {
    memset(c, 0, sizeof *c);
    c->palm_r_cm = 3.5f, c->tip_r_cm = 1.0f, c->blade_r_cm = 1.0f, c->blade_max_cm = 25.0f;
    c->ghost_cm = 4.0f, c->ghost_full_cm = 12.0f, c->ghost_a = 0.35f, c->ghost_in_s = 0.10f, c->ghost_out_s = 0.15f;
    c->spring_s = 0.08f, c->jump_cm = 2.0f, c->contact_cm = 0.3f;
    c->tick_cms = 150.0f, c->tick_gap_s = 0.15f, c->clank_cms = 60.0f, c->clank_full_cms = 300.0f, c->fx_cms = 150.0f;
    c->blade_pivot = 0, c->pivot_max_rad = 85.0f * 0.01745329f;
}

static int parse_f(const char *v, float lo, float hi, float *out) {
    if (!v || !*v) return 0;
    char *e = NULL;
    float x = strtof(v, &e);
    if (!e || *e || !isfinite(x) || x < lo || x > hi) return 0;
    *out = x;
    return 1;
}
int somvr_sf_parse_ghost(const char *v, somvr_sf_cfg *c) { return parse_f(v, 0, 1, &c->ghost_a); }
int somvr_sf_parse_blade_max(const char *v, somvr_sf_cfg *c) { return parse_f(v, 0, 100, &c->blade_max_cm); }

void somvr_sf_hand_reset(somvr_sf_hand *h) { memset(h, 0, sizeof *h); }

sv_vec3 somvr_sf_push(const somvr_sf_plane *pl, int npl, const sv_vec3 *s, const float *r, int ns, float *depth, int *which) {
    sv_vec3 T = { 0, 0, 0 };
    float big = 0;
    int w = -1;
    for (int round = 0; round < 4 * npl; round++) {
        float best = 1e-4f;
        int bi = -1;
        for (int k = 0; k < npl; k++)
            for (int i = 0; i < ns; i++) {
                if (!(r[i] > 0)) continue;
                float d = r[i] - dot3v(sub3(add3(s[i], T), pl[k].p), pl[k].n);
                if (d > best) best = d, bi = k;
            }
        if (bi < 0) break;
        T = add3(T, mul3(pl[bi].n, best));
        if (best > big) big = best, w = bi;
    }
    if (depth) *depth = len3v(T);
    if (which) *which = w;
    return T;
}

int somvr_sf_plane_of(sv_vec3 from, sv_vec3 hit, sv_vec3 normal, somvr_sf_plane *out) {
    if (!fin3(from) || !fin3(hit)) return 0;
    sv_vec3 back = sub3(from, hit), n = normal;
    float ln = fin3(n) ? len3v(n) : 0, lb = len3v(back);
    if (ln > 0.5f && ln < 2.0f)
        n = mul3(n, 1.0f / ln);
    else if (lb > 1e-3f)
        n = mul3(back, 1.0f / lb);
    else
        return 0;
    if (lb > 1e-3f && dot3v(n, back) < 0) n = mul3(n, -1.0f); /* the side the ray came from */
    out->p = hit, out->n = n;
    return 1;
}

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

void somvr_sf_step(somvr_sf_hand *h, const somvr_sf_cfg *c, const somvr_sf_in *in, double t, float dt, somvr_sf_out *o) {
    memset(o, 0, sizeof *o);
    o->plane = o->bplane = -1;
    sv_vec3 T = { 0, 0, 0 };
    float hd = 0, bd = 0;
    int vd = 0;
    if (in->free) {
        T = somvr_sf_push(in->pl, in->npl, in->s, in->r, SOMVR_SF_NS, &hd, &o->plane);
        if (in->blade && c->blade_max_cm > 0 && in->nbpl > 0) {
            sv_vec3 bs[2] = { add3(in->base, T), add3(in->tip, T) };
            float br[2] = { c->blade_r_cm, c->blade_r_cm };
            int bw;
            sv_vec3 B = somvr_sf_push(in->bpl, in->nbpl, bs, br, 2, &bd, &bw);
            if (bd > c->blade_max_cm) {
                vd = 1; /* deeper than the sword may push the hand: the blade goes in */
            } else if (bd > 0) {
                T = add3(T, B), o->bplane = bw;
                sv_vec3 s2[SOMVR_SF_NS];
                for (int i = 0; i < SOMVR_SF_NS; i++) s2[i] = add3(in->s[i], T);
                float d2;
                T = add3(T, somvr_sf_push(in->pl, in->npl, s2, in->r, SOMVR_SF_NS, &d2, NULL));
            }
        }
    }
    o->depth = hd, o->bdepth = bd;
    /* the spring: a jump back eases, the shown hand still out of this frame's surfaces */
    sv_vec3 off = T, prev = h->off;
    float jump = len3v(sub3(prev, T));
    if (dt > 0 && c->spring_s > 0 && jump > c->jump_cm && len3v(T) < len3v(prev)) {
        off = add3(T, mul3(sub3(prev, T), expf(-dt / c->spring_s)));
        if (in->free) {
            sv_vec3 s2[SOMVR_SF_NS];
            for (int i = 0; i < SOMVR_SF_NS; i++) s2[i] = add3(in->s[i], off);
            off = add3(off, somvr_sf_push(in->pl, in->npl, s2, in->r, SOMVR_SF_NS, NULL, NULL));
        }
        o->ev |= SOMVR_SF_EV_EASE;
    }
    if (!fin3(off)) off = (sv_vec3){ 0, 0, 0 };
    h->off = o->off = off;
    /* the hand's contact and its tick */
    int touch = in->free && hd > c->contact_cm;
    if (touch && !h->contact) {
        o->ev |= SOMVR_SF_EV_CONTACT;
        h->t_contact = t, h->depth_max = 0;
        if (h->have_last && dt > 0 && o->plane >= 0)
            o->speed = -dot3v(sub3(in->s[0], h->last_s0), in->pl[o->plane].n) / dt;
        if (o->speed < 0) o->speed = 0;
        if (t - h->t_tick >= c->tick_gap_s || h->t_tick == 0) {
            o->tick = 0.15f + 0.55f * clamp01(c->tick_cms > 0 ? o->speed / c->tick_cms : 1);
            h->t_tick = t;
        }
    } else if (!touch && h->contact) {
        o->ev |= SOMVR_SF_EV_RELEASE;
        o->held_s = (float)(t - h->t_contact), o->depth_max = h->depth_max;
    }
    if (touch && hd > h->depth_max) h->depth_max = hd;
    h->contact = touch;
    /* the blade's contact: a clank (or a tick when slow), the sparks when fast */
    int btouch = in->free && in->blade && !vd && bd > c->contact_cm;
    if (btouch && !h->blade) {
        o->ev |= SOMVR_SF_EV_BLADE;
        h->t_blade = t, h->bdepth_max = 0;
        if (h->have_tip && dt > 0 && o->bplane >= 0)
            o->bspeed = -dot3v(sub3(in->tip, h->last_tip), in->bpl[o->bplane].n) / dt;
        if (o->bspeed < 0) o->bspeed = 0;
        if (o->bspeed >= c->clank_cms && (t - h->t_clank >= 0.2 || h->t_clank == 0)) {
            float k = c->clank_full_cms > 0 ? o->bspeed / c->clank_full_cms : 1;
            o->clank = k < 0.25f ? 0.25f : k > 1 ? 1 : k, o->fx = o->bspeed >= c->fx_cms;
            h->t_clank = t;
        } else if (t - h->t_tick >= c->tick_gap_s || h->t_tick == 0) {
            o->tick = 0.2f;
            h->t_tick = t;
        }
    } else if (!btouch && h->blade) {
        o->ev |= SOMVR_SF_EV_BLADE_OFF;
        o->bheld_s = (float)(t - h->t_blade), o->bdepth_max = h->bdepth_max;
    }
    if (btouch && bd > h->bdepth_max) h->bdepth_max = bd;
    h->blade = btouch;
    if (vd && !h->vd) o->ev |= SOMVR_SF_EV_VOID;
    h->vd = vd;
    /* the ghost: the real hand, faint, once the two have parted */
    float sep = len3v(off), span = c->ghost_full_cm - c->ghost_cm;
    float want = !(c->ghost_a > 0) ? 0 : span > 0 ? clamp01((sep - c->ghost_cm) / span) : sep >= c->ghost_cm;
    want = want * want * (3 - 2 * want);
    float g0 = h->ghost;
    if (want > h->ghost)
        h->ghost = c->ghost_in_s > 0 ? fminf(want, h->ghost + dt / c->ghost_in_s) : want;
    else
        h->ghost = c->ghost_out_s > 0 ? fmaxf(want, h->ghost - dt / c->ghost_out_s) : want;
    if (g0 <= 0 && h->ghost > 0) o->ev |= SOMVR_SF_EV_GHOST_ON;
    if (g0 > 0 && h->ghost <= 0) o->ev |= SOMVR_SF_EV_GHOST_OFF;
    o->alpha = h->ghost * c->ghost_a;
    h->last_s0 = in->s[0], h->have_last = 1;
    if (in->blade)
        h->last_tip = in->tip, h->have_tip = 1;
    else
        h->have_tip = 0;
}

static sv_vec3 cross3(sv_vec3 a, sv_vec3 b) {
    return (sv_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

int somvr_sf_pivot(const somvr_sf_plane *pl, sv_vec3 grip, sv_vec3 dir, float len, float margin, float max_rad, sv_vec3 perp,
                   sv_vec3 *rot, float *depth) {
    *rot = (sv_vec3){ 0, 0, 0 }, *depth = 0;
    float dl = len3v(dir);
    if (!pl || !fin3(grip) || !(dl > 0.5f) || !(len > 1.0f)) return 0;
    sv_vec3 d = mul3(dir, 1.0f / dl), n = pl->n;
    float hg = dot3v(sub3(grip, pl->p), n), s = dot3v(d, n), need = (margin - hg) / len; /* the tip's height = hg + len s */
    if (!(s < need)) return 0;                                                          /* out (also NaN) */
    *depth = margin - (hg + len * s);
    if (!(hg > margin) || need > 1.0f) return 2; /* the grip itself at the surface: no turn keeps the blade out */
    sv_vec3 e = sub3(n, mul3(d, s));              /* across the blade toward the normal */
    float el = len3v(e);
    if (el < 0.17f) {                             /* within ~10 deg of straight in: toward perp (made across the blade) */
        sv_vec3 q = sub3(perp, mul3(d, dot3v(perp, d)));
        float ql = len3v(q);
        if (!(ql > 1e-3f)) return 2;
        e = mul3(q, 1.0f / ql);
    } else {
        e = mul3(e, 1.0f / el);
    }
    /* d' = cos th d + sin th e: its height s cos th + c sin th = R sin(th + phi), c = e.n, R = |(s, c)|, phi = atan2(s, c) */
    float c = dot3v(e, n), R = sqrtf(s * s + c * c), phi = atan2f(s, c), k = R > 1e-4f ? need / R : 2.0f;
    if (k > 1.0f) return 2;
    float th = asinf(k < -1 ? -1 : k) - phi;
    if (!(th > 0)) return 0;
    if (th > max_rad) return 2;
    sv_vec3 a = cross3(d, e);
    float al = len3v(a);
    if (!(al > 1e-4f)) return 2;
    *rot = mul3(a, th / al);
    return 1;
}

void somvr_sf_pv_step(somvr_sf_pv *p, const somvr_sf_cfg *c, int on, float depth, float ang, sv_vec3 tip, sv_vec3 n,
                      double t, float dt, somvr_sf_out *o) {
    memset(o, 0, sizeof *o);
    o->plane = o->bplane = -1;
    if (on && !p->on) {
        o->ev |= SOMVR_SF_EV_BLADE;
        p->t_on = t, p->depth_max = 0, p->peak_rad = 0;
        if (p->have_tip && dt > 0) o->bspeed = -dot3v(sub3(tip, p->last_tip), n) / dt;
        if (!(o->bspeed > 0)) o->bspeed = 0;
        if (o->bspeed >= c->clank_cms && (t - p->t_clank >= 0.2 || p->t_clank == 0)) {
            float k = c->clank_full_cms > 0 ? o->bspeed / c->clank_full_cms : 1;
            o->clank = k < 0.25f ? 0.25f : k > 1 ? 1 : k, o->fx = o->bspeed >= c->fx_cms;
            p->t_clank = t;
        } else if (t - p->t_tick >= c->tick_gap_s || p->t_tick == 0) {
            o->tick = 0.2f;
            p->t_tick = t;
        }
    } else if (!on && p->on) {
        o->ev |= SOMVR_SF_EV_BLADE_OFF;
        o->bheld_s = (float)(t - p->t_on), o->bdepth_max = p->depth_max;
    }
    if (on) {
        if (depth > p->depth_max) p->depth_max = depth;
        if (ang > p->peak_rad) p->peak_rad = ang;
        o->bdepth = depth;
    }
    p->on = on;
    p->last_tip = tip, p->have_tip = fin3(tip);
}

static int has_ci(const char *s, const char *p, int pre) {
    for (const char *t = s; *t; t++) {
        const char *a = t, *b = p;
        while (*a && *b && (*a | 0x20) == (*b | 0x20)) a++, b++;
        if (!*b) return 1;
        if (pre) return 0;
    }
    return 0;
}

int somvr_sf_solid(const char *name) {
    const char *n = name ? name : "";
    if (!*n) return 0; /* no surface record: not the world's geometry (an orc's capsule: boot 001811) */
    static const char *const pre[] = { "flesh", "armor", "sky", "air", "invisible", "playerblocker", "water", "liquid",
                                       "foliage", "cloth" };
    for (unsigned i = 0; i < sizeof pre / sizeof pre[0]; i++)
        if (has_ci(n, pre[i], 1)) return 0;
    return !has_ci(n, "puddle", 0);
}
