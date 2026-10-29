/* somvr_stealth.c - the VR stealth kill's pure part (see somvr_stealth.h; docs/re-xr-pipeline.md 67) */
#include "somvr_stealth.h"
#include <math.h>
#include <string.h>

#define SC_TIER_HEAVY_ 2 /* somvr_combat.h SC_TIER_HEAVY (this file stays free of it) */

void somvr_stl_defaults(somvr_stl_cfg *c)
{
    c->behind_deg = 110.0f;
    c->back_deg = 90.0f;
    c->slow_cms = 90.0f;
    c->stance = SOMVR_STL_STANCE_EITHER;
    c->low_cm = -10.0f;
    c->high_cm = 100.0f;
    c->stab_mps = 0.8f;
    c->stab_cos = 0.7f;
    c->near_cm = 250.0f;
    c->thrust = 0;
    c->thrust_mps = 1.5f;
    c->thrust_cos = 0.866f;
    c->depth_cm = 15.0f;
    c->grab_depth_cm = 10.0f;
    c->grab_mps = 0.5f;
    c->side_deg = 60.0f;
    c->grace_s = 0.3f;
}

const char *somvr_stl_why(int v)
{
    static const char *const n[] = { "the stealth kill", "dead already", "its awareness not readable", "it has noticed Talion",
                                     "not behind it", "neither crouched nor slow", "the contact on its front half",
                                     "the contact not on its back or neck", "a touch (neither a hit nor a stab)",
                                     "in the body but no thrust (no damage; the alert after the grace)",
                                     "combat's capsule but not the body (nothing)" };
    return v >= 0 && v < SOMVR_STL_N ? n[v] : "?";
}

const char *somvr_stl_stance_name(int s)
{
    return s == SOMVR_STL_STANCE_CROUCH ? "crouch" : s == SOMVR_STL_STANCE_SLOW ? "slow" : "either";
}

int somvr_stl_stance_of(const char *w)
{
    if (!w) return -1;
    if (!strcmp(w, "crouch")) return SOMVR_STL_STANCE_CROUCH;
    if (!strcmp(w, "slow")) return SOMVR_STL_STANCE_SLOW;
    if (!strcmp(w, "either")) return SOMVR_STL_STANCE_EITHER;
    return -1;
}

float somvr_stl_off_deg(const float p[3], const float o[3], float yaw)
{
    float dx = p[0] - o[0], dz = p[2] - o[2];
    if (dx * dx + dz * dz <= 1e-6f) return 0.0f;
    return remainderf(atan2f(dx, dz) - yaw, 6.2831853f) * 57.29578f;
}

int somvr_stl_possible(const somvr_stl_cfg *c, const somvr_stl_who *w)
{
    if (w->dead) return SOMVR_STL_DEAD;
    if (w->aware < 0) return SOMVR_STL_UNKNOWN;
    if (w->aware) return SOMVR_STL_AWARE;
    if (!(fabsf(w->player_off_deg) >= c->behind_deg)) return SOMVR_STL_FRONT;
    int slow = w->speed_cms == w->speed_cms && w->speed_cms < c->slow_cms;
    int ok = c->stance == SOMVR_STL_STANCE_CROUCH ? w->crouched : c->stance == SOMVR_STL_STANCE_SLOW ? slow
                                                                                                   : w->crouched || slow;
    return ok ? SOMVR_STL_KILL : SOMVR_STL_STANCE;
}

/* build 21: the thrust's rule (the orc a target: unaware, Talion behind it, crouched or slow) */
static int judge_thrust(const somvr_stl_cfg *c, const somvr_stl_hit *h)
{
    if (!(h->depth_cm >= 0)) return SOMVR_STL_AIR;
    int point = h->cos_axis >= c->thrust_cos;
    if (h->held)
        return point && h->axis_mps >= c->grab_mps && h->depth_cm >= c->grab_depth_cm ? SOMVR_STL_KILL : SOMVR_STL_TOUCH;
    if (h->neck && h->tier_cut == SC_TIER_HEAVY_) return SOMVR_STL_KILL; /* a full-speed cut to the neck from behind */
    if (!(fabsf(h->contact_off_deg) >= c->side_deg)) return SOMVR_STL_SIDE;
    if (!(h->contact_up_cm >= c->low_cm && h->contact_up_cm <= c->high_cm)) return SOMVR_STL_LOW;
    return point && h->axis_mps >= c->thrust_mps && h->depth_cm >= c->depth_cm ? SOMVR_STL_KILL : SOMVR_STL_TOUCH;
}

int somvr_stl_judge(const somvr_stl_cfg *c, const somvr_stl_who *w, const somvr_stl_hit *h)
{
    int p = somvr_stl_possible(c, w);
    if (p != SOMVR_STL_KILL) return p;
    if (c->thrust) return judge_thrust(c, h);
    if (!(fabsf(h->contact_off_deg) >= c->back_deg)) return SOMVR_STL_SIDE;
    if (!(h->contact_up_cm >= c->low_cm && h->contact_up_cm <= c->high_cm)) return SOMVR_STL_LOW;
    if (h->tier <= 0 && !(h->axis_mps >= c->stab_mps && fabsf(h->cos_axis) >= c->stab_cos)) return SOMVR_STL_WEAK;
    return SOMVR_STL_KILL;
}

int somvr_stl_tier(int tier16, int tier_swing)
{
    return tier16 > 0 ? tier16 : tier_swing > 0 ? tier_swing : 0;
}

int somvr_stl_kill_by(const somvr_stl_cfg *c, const somvr_stl_hit *h)
{
    if (!c->thrust) return SOMVR_STL_BY_HIT;
    if (h->held) return SOMVR_STL_BY_GRAB;
    int thrust = h->cos_axis >= c->thrust_cos && h->axis_mps >= c->thrust_mps && h->depth_cm >= c->depth_cm;
    return !thrust && h->neck && h->tier_cut == SC_TIER_HEAVY_ ? SOMVR_STL_BY_NECK : SOMVR_STL_BY_THRUST;
}

const char *somvr_stl_by_name(int by)
{
    return by == SOMVR_STL_BY_THRUST ? "a thrust" : by == SOMVR_STL_BY_NECK ? "a cut to the neck"
           : by == SOMVR_STL_BY_GRAB ? "a stab into the held orc" : "a hit or a stab (build 20c)";
}

static float seg_d2(const float p[3], const float a[3], const float b[3])
{
    float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
    float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float k = l2 > 1e-6f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    float d[3] = { ap[0] - k * ab[0], ap[1] - k * ab[1], ap[2] - k * ab[2] };
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}

/* how far inside the body p is (> 0 inside; cm, the deepest link), *link = that link */
static float body_in(const float p[3], const float (*node)[3], const float *r, int n, int *link)
{
    float best = -1e9f;
    for (int k = 0; k + 1 < n; k++) {
        float in = r[k] - sqrtf(seg_d2(p, node[k], node[k + 1]));
        if (in > best) best = in, *link = k;
    }
    return best;
}

float somvr_stl_body_depth(const float base[3], const float tip[3], const float (*node)[3], const float *r, int n,
                           float entry[3], int *link)
{
    float d[3] = { tip[0] - base[0], tip[1] - base[1], tip[2] - base[2] }, p[3];
    float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    int lk = -1, steps = L > 1.0f ? (int)ceilf(L) : 1;
    if (n < 2 || !(L == L)) return -1.0f;
    steps = steps > 400 ? 400 : steps;
    float lo = 0, hi = -1;
    for (int i = 0; i <= steps; i++) {
        float u = (float)i / (float)steps;
        for (int j = 0; j < 3; j++) p[j] = base[j] + u * d[j];
        if (body_in(p, node, r, n, &lk) >= 0) {
            hi = u;
            break;
        }
        lo = u;
    }
    if (hi < 0) return -1.0f;
    if (hi > 0)
        for (int it = 0; it < 8; it++) { /* the surface between the last point outside and the first inside */
            float u = 0.5f * (lo + hi);
            for (int j = 0; j < 3; j++) p[j] = base[j] + u * d[j];
            if (body_in(p, node, r, n, &lk) >= 0) hi = u;
            else lo = u;
        }
    for (int j = 0; j < 3; j++) p[j] = base[j] + hi * d[j];
    body_in(p, node, r, n, &lk);
    if (entry) memcpy(entry, p, sizeof p);
    if (link) *link = lk;
    return (1.0f - hi) * L;
}

void somvr_stl_stick_start(somvr_stl_stick *s, const float E[3], const float a[3], float len)
{
    memset(s, 0, sizeof *s);
    float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(l > 1e-6f) || !(len > 0)) return;
    for (int j = 0; j < 3; j++) s->E[j] = E[j], s->a[j] = a[j] / l;
    s->len = len, s->on = 1;
}

int somvr_stl_stick_step(somvr_stl_stick *s, const somvr_stl_stick_cfg *c, const float E[3], const float G[3], float Gs[3])
{
    if (!s->on) return SOMVR_STL_STK_OFF;
    memcpy(s->E, E, sizeof s->E);
    float v[3] = { G[0] - E[0], G[1] - E[1], G[2] - E[2] };
    float depth = v[0] * s->a[0] + v[1] * s->a[1] + v[2] * s->a[2] + s->len;
    s->depth = depth;
    if (depth <= 0) {
        s->on = 0, s->depth = 0;
        return SOMVR_STL_STK_OUT;
    }
    float most = s->len - c->guard_cm, dc = depth < most ? depth : most;
    for (int j = 0; j < 3; j++) Gs[j] = E[j] + s->a[j] * (dc - s->len);
    float o[3] = { G[0] - Gs[0], G[1] - Gs[1], G[2] - Gs[2] };
    s->off = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    s->depth = dc;
    if (dc > s->peak) s->peak = dc;
    if (s->off > c->tear_cm) {
        s->on = 0;
        return SOMVR_STL_STK_TEAR;
    }
    return SOMVR_STL_STK_HOLD;
}

int somvr_stl_cue_step(somvr_stl_cue *q, uintptr_t orc, int possible, double t, double rearm_s)
{
    if (!possible || !orc) {
        if (q->on) q->on = 0, q->off_t = t;
        return 0;
    }
    int tick = q->orc != orc || (!q->on && t - q->off_t >= rearm_s);
    q->on = 1;
    if (tick) q->orc = orc, q->ticks++;
    return tick;
}

int somvr_stl_standoff(const float p[3], const float c[3], float r, float dt, float ease_hz, const float d[3],
                       float out[3])
{
    out[0] = out[1] = out[2] = 0;
    float nx = p[0] - c[0], nz = p[2] - c[2], dist = sqrtf(nx * nx + nz * nz);
    if (!(r > 0) || !(dist > 1e-3f) || !isfinite(d[0] + d[2]))
        return 0;
    nx /= dist, nz /= dist;
    float in = -(d[0] * nx + d[2] * nz); /* cm toward the orc this tick */
    if (!(in > 0))
        return 0;
    float room = dist - r, k = dt > 0 && ease_hz > 0 ? 1.0f - expf(-dt * ease_hz) : 1.0f;
    float allow = room > 0 ? room * k : 0;
    if (in <= allow)
        return 0;
    float cut = in - allow;
    out[0] = cut * nx, out[2] = cut * nz;
    return 1;
}

int somvr_stl_standoff_brake(const float p[3], const float c[3], float r, float dt, float decel, const float d[3],
                             float out[3])
{
    out[0] = out[1] = out[2] = 0;
    float nx = p[0] - c[0], nz = p[2] - c[2], dist = sqrtf(nx * nx + nz * nz);
    if (!(r > 0) || !(dist > 1e-3f) || !isfinite(d[0] + d[2]) || !(decel > 0))
        return 0;
    nx /= dist, nz /= dist;
    float in = -(d[0] * nx + d[2] * nz); /* cm toward the orc this tick */
    if (!(in > 0))
        return 0;
    float room = dist - r, allow = 0;
    if (room > 0) {
        allow = dt > 0 ? sqrtf(2.0f * decel * room) * dt : room;
        if (allow > room)
            allow = room;
    }
    if (in <= allow)
        return 0;
    float cut = in - allow;
    out[0] = cut * nx, out[2] = cut * nz;
    return 1;
}
