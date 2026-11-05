/* somvr_finisher.c - SOMVR_FINISHER, the pure part (somvr_finisher.h; docs/re-xr-pipeline.md 140) */
#include "somvr_finisher.h"

#include <math.h>

void somvr_fn_defaults(somvr_fn_cfg *c)
{
    c->align_cm = 12.0f, c->align_deg = 25.0f, c->start_cm = 15.0f, c->depth_cm = 22.0f;
    c->v_max = 100.0f, c->v_ref = 100.0f, c->acc_min = 0.2f;
    c->out_gain = 0.9f, c->out_break_cm = 1.5f, c->back_free = 0, c->follow = 0, c->min_v = 15.0f;
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
    if (c->follow) { /* build 37: the blade with the hand along the line (1:1 up to v_max), as straight as it moves */
        float total = c->start_cm + c->depth_cm, np = p;
        *match = 0, *rate = 0;
        if (along >= c->min_v) {
            *match = acc, *rate = fminf(along, c->v_max) * acc;
            np = p + (total > 0 ? *rate * dt / total : 0);
            if (p < 1.0f && np > 1.0f)
                np = 1.0f; /* (the deepest point is reached, never skipped) */
        } else if (c->back_free && p < 1.0f && along <= -c->min_v && total > 0) {
            *rate = along, np = p + along * dt / total; /* (a pull back draws it back with the hand) */
            if (np < 0)
                np = 0;
        }
        return np > 2.0f ? 2.0f : np;
    }
    float hi = 1.6f * c->v_ref;
    float spd = along <= 0 ? 0 : along <= c->v_ref ? along / c->v_ref : along <= hi ? 1.0f : fmaxf(0.4f, hi / along);
    *match = acc * spd;
    *rate = c->v_max * *match;
    float total = c->start_cm + c->depth_cm;
    if (c->back_free && p < 1.0f && along < 0 && total > 0) { /* build 37: the hand pulls back before the deepest: with it */
        *rate = along, *match = 0;
        float bp = p + along * dt / total;
        return bp < 0 ? 0 : bp;
    }
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

float somvr_fn_out_p2(const somvr_fn_cfg *c, float retreat_cm, float s_deep)
{
    float total = c->start_cm + c->depth_cm;
    s_deep = !(s_deep > 0) ? 0 : s_deep > 1 ? 1 : s_deep;
    if (!(total > 0) || !isfinite(retreat_cm))
        return 2.0f - s_deep;
    float back = c->out_gain * (retreat_cm - c->out_break_cm), k = s_deep * total;
    back = back < 0 ? 0 : back > k ? k : back;
    return 2.0f - (k - back) / total;
}

float somvr_fn_out_p(const somvr_fn_cfg *c, float retreat_cm) { return somvr_fn_out_p2(c, retreat_cm, 1.0f); }

float somvr_fn_ease(float s)
{
    if (!(s > 0))
        return 0;
    if (s >= 1)
        return 1;
    return s * s * (3.0f - 2.0f * s);
}

int somvr_fn_why(float d_cm, float reach_cm, float view_cos, float view_cos_min, float face_deg, float face_max_deg,
                 int reachable)
{
    if (!isfinite(d_cm))
        return SOMVR_FN_WHY_NONE;
    if (!(d_cm < reach_cm))
        return SOMVR_FN_WHY_FAR;
    if (!(face_deg <= face_max_deg))
        return SOMVR_FN_WHY_BACK;
    if (!(view_cos >= view_cos_min))
        return SOMVR_FN_WHY_VIEW;
    return reachable ? SOMVR_FN_WHY_OK : SOMVR_FN_WHY_REACH;
}

const char *somvr_fn_why_text(int why)
{
    static const char *const w[SOMVR_FN_WHY_N] = { "an orc qualifies", "get within reach of him", "he must face you",
                                                   "look at him", "step closer: his neck out of the arm's reach",
                                                   "no orc near", "step back: too close for the blade" };
    return why >= 0 && why < SOMVR_FN_WHY_N ? w[why] : "?";
}

void somvr_fn_place_defaults(somvr_fn_place_cfg *c)
{
    c->body[0] = 17.0f, c->body[1] = -22.0f, c->body[2] = -4.0f; /* the right shoulder from the eyes */
    c->grip_r = 30.0f, c->blade = 95.0f, c->depth = 22.0f, c->s_min = 6.0f, c->s_max = 30.0f, c->s_pref = 15.0f;
    c->reach = 70.0f, c->ahead_min = -10.0f, c->up_max = 25.0f, c->up_min = -70.0f;
}

int somvr_fn_place(const somvr_fn_place_cfg *c, const float neck[3], float g[3], float dir[3], float *s, float *ext,
                   int *why)
{
    float best = 1e9f, nb[3] = {neck[0] - c->body[0], neck[1] - c->body[1], neck[2] - c->body[2]};
    float nd = sqrtf(nb[0] * nb[0] + nb[1] * nb[1] + nb[2] * nb[2]);
    int found = 0;
    if (why)
        *why = nd > c->blade + c->s_pref ? SOMVR_FN_WHY_REACH : SOMVR_FN_WHY_NEAR;
    if (!isfinite(nd))
        return 0;
    static const float rk[3] = {0.7f, 0.85f, 1.0f};
    const int N = 240; /* the directions: a Fibonacci sphere */
    for (int ri = 0; ri < 3; ri++)
        for (int i = 0; i < N; i++) {
            float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)N, r = sqrtf(1.0f - y * y), a = 2.39996323f * (float)i;
            float u[3] = {r * cosf(a), y, r * sinf(a)}, G[3], v[3];
            for (int k = 0; k < 3; k++)
                G[k] = c->body[k] + rk[ri] * c->grip_r * u[k], v[k] = neck[k] - G[k];
            if (G[2] < c->ahead_min || G[1] > c->up_max || G[1] < c->up_min)
                continue;
            float d = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), gap = d - c->blade;
            if (!(gap >= c->s_min && gap <= c->s_max))
                continue;
            float T = gap + c->depth, e[3];
            for (int k = 0; k < 3; k++)
                e[k] = G[k] + T * v[k] / d - c->body[k];
            float x = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            if (x > c->reach)
                continue;
            float score = fabsf(gap - c->s_pref) + 0.25f * (c->reach - x);
            if (score < best) {
                best = score, found = 1;
                for (int k = 0; k < 3; k++)
                    g[k] = G[k], dir[k] = v[k] / d;
                *s = gap, *ext = x;
            }
        }
    if (found && why)
        *why = SOMVR_FN_WHY_OK;
    return found;
}
