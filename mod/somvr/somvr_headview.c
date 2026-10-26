/* somvr_headview.c - see somvr_headview.h */
#include "somvr_headview.h"

#include <math.h>
#include <string.h>

void somvr_hv_cfg_default(somvr_hv_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->fwd_cm = 16, c->up_cm = 0, c->min_cm = 10;
    c->clamp_s = 0.06f, c->yaw_s = 0.08f, c->gap_s = 0.25f;
}

void somvr_hv_init(somvr_hv *s) { memset(s, 0, sizeof *s); }

static float wrap_pi(float a)
{
    if (!isfinite(a))
        return 0;
    while (a > 3.14159265f)
        a -= 6.28318531f;
    while (a < -3.14159265f)
        a += 6.28318531f;
    return a;
}

/* critically damped toward 0 over dt: x'' = -w^2 x - 2 w x' (exact) */
static void damp2(float *x, float *v, float w, float dt)
{
    float e = expf(-w * dt), b = *v + w * *x;
    float nx = (*x + b * dt) * e, nv = (*v - w * b * dt) * e;
    if (!isfinite(nx) || !isfinite(nv))
        nx = nv = 0;
    *x = nx, *v = nv;
}

void somvr_hv_step(somvr_hv *s, const somvr_hv_cfg *c, const somvr_hv_in *in, somvr_hv_out *o)
{
    memset(o, 0, sizeof *o);
    float by = isfinite(in->body_yaw) ? in->body_yaw : 0;
    double dtd = in->t - s->t;
    int latch = !s->have || !(dtd >= 0) || dtd > c->gap_s;
    float dt = latch ? 0 : (float)dtd;
    if (latch || !(c->yaw_s > 0)) {
        s->yaw = by, s->yaw_v = 0;
    } else {
        float x = wrap_pi(s->yaw - by); /* the eased yaw's offset from the body's, toward 0 */
        damp2(&x, &s->yaw_v, 1 / c->yaw_s, dt);
        s->yaw = wrap_pi(by + x);
    }
    sv_vec3 f = { sinf(s->yaw), 0, cosf(s->yaw) };
    sv_vec3 p = { in->base.x + c->fwd_cm * f.x, in->base.y + c->up_cm, in->base.z + c->fwd_cm * f.z };
    float t = 0;
    if (in->has_head && c->min_cm > 0) {
        o->ahead_cm = (p.x - in->head.x) * f.x + (p.z - in->head.z) * f.z;
        t = c->min_cm - o->ahead_cm;
        t = isfinite(t) && t > 0 ? t : 0;
    }
    if (latch || !(c->clamp_s > 0)) {
        s->x = t, s->x_v = 0;
    } else {
        float x = s->x - t;
        damp2(&x, &s->x_v, 1 / c->clamp_s, dt);
        s->x = t + x;
        if (s->x < 0)
            s->x = 0, s->x_v = s->x_v < 0 ? 0 : s->x_v;
    }
    s->latches += latch, s->frames++, s->clamped += s->x > 0.05f;
    s->x_max = fmaxf(s->x_max, s->x);
    s->have = 1, s->t = in->t;
    o->pos = (sv_vec3){ p.x + s->x * f.x, p.y, p.z + s->x * f.z };
    o->yaw = s->yaw, o->target_cm = t, o->push_cm = s->x, o->latched = latch;
}

const char *const somvr_tune_keys[SOMVR_TUNE_N] = { "eye_fwd_cm", "eye_up_cm", "near_cm", "eye_min_cm" };
const float somvr_tune_lo[SOMVR_TUNE_N] = { -30, -30, 1, 0 }, somvr_tune_hi[SOMVR_TUNE_N] = { 40, 30, 50, 40 };

/* a plain decimal, locale-free: [+-]digits[.digits] or [+-].digits, nothing after it but spaces */
static int dec(const char *a, const char *b, float *out)
{
    double v = 0, sc = 1;
    int sign = 1, nd = 0, dot = 0;
    if (a < b && (*a == '-' || *a == '+'))
        sign = *a++ == '-' ? -1 : 1;
    for (; a < b; a++) {
        if (*a >= '0' && *a <= '9') {
            if (dot)
                sc *= 0.1, v += (*a - '0') * sc;
            else
                v = 10 * v + (*a - '0');
            nd++;
        } else if (*a == '.' && !dot) {
            dot = 1;
        } else {
            break;
        }
    }
    while (a < b && (*a == ' ' || *a == '\t'))
        a++;
    if (!nd || a != b || nd > 12)
        return 0;
    *out = (float)(sign * v);
    return 1;
}

void somvr_tune_parse(const char *text, somvr_tune *out)
{
    memset(out, 0, sizeof *out);
    for (const char *l = text; l && *l;) {
        const char *e = strchr(l, '\n'), *end = e ? e : l + strlen(l), *a = l, *b = end;
        l = e ? e + 1 : end;
        while (a < b && (*a == ' ' || *a == '\t'))
            a++;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r'))
            b--;
        if (a == b || *a == '#')
            continue;
        const char *eq = memchr(a, '=', (size_t)(b - a)), *ke = eq ? eq : b;
        while (ke > a && (ke[-1] == ' ' || ke[-1] == '\t'))
            ke--;
        int k = -1;
        for (int i = 0; i < SOMVR_TUNE_N; i++)
            if (strlen(somvr_tune_keys[i]) == (size_t)(ke - a) && !memcmp(a, somvr_tune_keys[i], (size_t)(ke - a)))
                k = i;
        float v = 0;
        const char *va = eq ? eq + 1 : b;
        while (va < b && (*va == ' ' || *va == '\t'))
            va++;
        int ok = k >= 0 && eq && dec(va, b, &v) && v >= somvr_tune_lo[k] && v <= somvr_tune_hi[k];
        if (ok) {
            out->set |= 1u << k, out->v[k] = v;
            continue;
        }
        if (k >= 0)
            out->bad++;
        else
            out->unknown++;
        if (!out->first_bad[0]) {
            size_t n = (size_t)(b - a) < sizeof out->first_bad - 1 ? (size_t)(b - a) : sizeof out->first_bad - 1;
            memcpy(out->first_bad, a, n);
            out->first_bad[n] = 0;
        }
    }
}
