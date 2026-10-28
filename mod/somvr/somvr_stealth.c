/* somvr_stealth.c - the VR stealth kill's pure part (see somvr_stealth.h; docs/re-xr-pipeline.md 67) */
#include "somvr_stealth.h"
#include <math.h>
#include <string.h>

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
}

const char *somvr_stl_why(int v)
{
    static const char *const n[] = { "the stealth kill", "dead already", "its awareness not readable", "it has noticed Talion",
                                     "not behind it", "neither crouched nor slow", "the contact on its front half",
                                     "the contact not on its back or neck", "a touch (neither a hit nor a stab)" };
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

int somvr_stl_judge(const somvr_stl_cfg *c, const somvr_stl_who *w, const somvr_stl_hit *h)
{
    int p = somvr_stl_possible(c, w);
    if (p != SOMVR_STL_KILL) return p;
    if (!(fabsf(h->contact_off_deg) >= c->back_deg)) return SOMVR_STL_SIDE;
    if (!(h->contact_up_cm >= c->low_cm && h->contact_up_cm <= c->high_cm)) return SOMVR_STL_LOW;
    if (h->tier <= 0 && !(h->axis_mps >= c->stab_mps && fabsf(h->cos_axis) >= c->stab_cos)) return SOMVR_STL_WEAK;
    return SOMVR_STL_KILL;
}

int somvr_stl_tier(int tier16, int tier_swing)
{
    return tier16 > 0 ? tier16 : tier_swing > 0 ? tier_swing : 0;
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
