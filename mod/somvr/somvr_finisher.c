/* somvr_finisher.c - SOMVR_FINISHER, the pure part (somvr_finisher.h; docs/re-xr-pipeline.md 140) */
#include "somvr_finisher.h"

#include <math.h>

void somvr_fn_defaults(somvr_fn_cfg *c)
{
    c->align_cm = 12.0f, c->align_deg = 25.0f, c->start_cm = 15.0f, c->depth_cm = 22.0f;
    c->v_max = 100.0f, c->v_ref = 100.0f, c->acc_min = 0.2f;
}

static float len3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

int somvr_fn_aligned(const somvr_fn_cfg *c, const float tip[3], const float axis[3], const float otip[3],
                     const float odir[3], float *d_cm, float *ang_deg)
{
    float d[3] = {tip[0] - otip[0], tip[1] - otip[1], tip[2] - otip[2]}, la = len3(axis), lo = len3(odir);
    *d_cm = len3(d);
    float cs = la > 1e-6f && lo > 1e-6f ? (axis[0] * odir[0] + axis[1] * odir[1] + axis[2] * odir[2]) / (la * lo) : -1;
    cs = cs > 1 ? 1 : cs < -1 ? -1 : cs;
    *ang_deg = acosf(cs) * 57.29578f;
    return *d_cm <= c->align_cm && *ang_deg <= c->align_deg;
}

float somvr_fn_step(const somvr_fn_cfg *c, const float v[3], const float dir[3], float p, float dt, float *rate,
                    float *match)
{
    float sgn = p < 1.0f ? 1.0f : -1.0f, vl = len3(v), dl = len3(dir);
    float along = dl > 1e-6f ? sgn * (v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2]) / dl : 0;
    float cs = vl > 1e-3f ? along / vl : 0; /* how well the motion follows the way */
    float acc = cs <= c->acc_min ? 0 : (cs - c->acc_min) / (1.0f - c->acc_min);
    /* the speed's match: the hand's own below the reference (the blade 1:1 with it at v_max = v_ref), full up to 1.6 x it
     * (a real thrust runs 1-1.6 m/s), past that the closer the better (no less than 0.4: a flick is slower, not faster) */
    float hi = 1.6f * c->v_ref;
    float spd = along <= 0 ? 0 : along <= c->v_ref ? along / c->v_ref : along <= hi ? 1.0f : fmaxf(0.4f, hi / along);
    *match = acc * spd;
    *rate = c->v_max * *match;
    float total = c->start_cm + c->depth_cm;
    float np = p + (total > 0 ? *rate * dt / total : 0);
    if (p < 1.0f && np > 1.0f) np = 1.0f; /* the deepest point is reached, never skipped (the way turns there) */
    return np > 2.0f ? 2.0f : np;
}

void somvr_fn_point(const somvr_fn_cfg *c, const float start[3], const float dir[3], float p, float out[3])
{
    float s = p <= 1.0f ? p : 2.0f - p, k = (c->start_cm + c->depth_cm) * (s < 0 ? 0 : s);
    for (int i = 0; i < 3; i++) out[i] = start[i] + dir[i] * k;
}

float somvr_fn_depth(const somvr_fn_cfg *c, float p)
{
    float s = p <= 1.0f ? p : 2.0f - p;
    return (c->start_cm + c->depth_cm) * (s < 0 ? 0 : s) - c->start_cm;
}
