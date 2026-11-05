/* somvr_struggle.c - build 41's one rule for every struggle, the pure part (somvr_struggle.h; docs/re-xr-pipeline.md 180) */
#include "somvr_struggle.h"

#include <math.h>
#include <stddef.h>

#define SG_PI 3.14159265358979f
#define SG_2PI 6.2831854820251465f /* the game's own constant (L 0x3471310): the fold's bound */

int somvr_sg_act(int kind, int beast_domination, int anchored)
{
    if (beast_domination && kind > SOMVR_SG_NONE && kind < SOMVR_SG_OTHER)
        return SOMVR_SGA_GRIP;
    switch (kind) {
    case SOMVR_SG_PRESS: return SOMVR_SGA_THRUSTS;
    case SOMVR_SG_QT: return SOMVR_SGA_THRUST;
    case SOMVR_SG_HOLD: return SOMVR_SGA_HOLD;
    case SOMVR_SG_DIR: return SOMVR_SGA_SWIPE;
    case SOMVR_SG_AIM: return anchored ? SOMVR_SGA_STAB : SOMVR_SGA_STEER;
    default: return SOMVR_SGA_NONE;
    }
}

const char *somvr_sg_act_name(int act)
{
    static const char *const w[SOMVR_SGA_N] = {
        "none (a class not known)",
        "repeated thrusts or pulls (each one press, a tick as the game counts it)",
        "one hard thrust while its window is open",
        "a hand closed and kept closed (a fresh close; the ring runs down)",
        "a swipe of either hand or a lean the shown way (the arrow)",
        "the blade laid on the outline at the ring's node, then a thrust (completed as the game's own)",
        "the hand steers its cursor, a thrust inside the ring presses (no node known)",
        "the hand that took the beast kept closed (SOMVR_RIDE's domination; the ring runs down)",
    };
    return act >= 0 && act < SOMVR_SGA_N ? w[act] : "?";
}

/* ---- InputDirection ---- */
float somvr_sg_dir_angle(float x, float y, int *ok)
{
    float l = sqrtf(x * x + y * y);
    *ok = isfinite(l) && l >= 0.5f; /* (the game: under 0.5 = no direction) */
    if (!*ok)
        return 0;
    float c = x / l;
    c = c < -1 ? -1 : c > 1 ? 1 : c;
    float a = acosf(c);
    return y < 0 ? -a : a;
}

int somvr_sg_dir_game(float target, float tol, float a)
{
    float d = fabsf(target - a);
    if (d > SG_2PI)
        d -= SG_2PI;
    return d < tol;
}

int somvr_sg_dir_send(float target, float tol, float v[2])
{
    float a0 = target > SG_PI ? target - SG_2PI : target;
    v[0] = cosf(a0), v[1] = sinf(a0);
    if (!(tol > 0) || !isfinite(target))
        return 0;
    float e = fminf(0.05f, 0.4f * tol);
    static const float k[5] = { 0, -1, 1, -2, 2 };
    for (int i = 0; i < 5; i++) {
        float a = a0 + k[i] * e, x = cosf(a), y = sinf(a);
        int ok;
        float g = somvr_sg_dir_angle(x, y, &ok);
        if (ok && somvr_sg_dir_game(target, tol, g)) {
            v[0] = x, v[1] = y;
            return 1;
        }
    }
    return 0;
}

int somvr_sg_dir_match(const float g[2], float target, float cone_deg, float *off_deg)
{
    float l = sqrtf(g[0] * g[0] + g[1] * g[1]);
    *off_deg = 180;
    if (!(l > 1e-6f) || !isfinite(target))
        return 0;
    float d = atan2f(g[1], g[0]) - target;
    d = fmodf(d + SG_PI, 2 * SG_PI);
    if (d < 0)
        d += 2 * SG_PI;
    d -= SG_PI;
    *off_deg = fabsf(d) * 57.29578f;
    return *off_deg <= cone_deg;
}

const char *somvr_sg_dir_word(float target)
{
    static const char *const w[4] = { "right", "up", "left", "down" };
    float a = fmodf(target, 2 * SG_PI);
    if (a < 0)
        a += 2 * SG_PI;
    int i = (int)floorf(a / (0.5f * SG_PI) + 0.5f) & 3;
    return isfinite(target) ? w[i] : "?";
}

void somvr_sg_swipe_defaults(somvr_sg_swipe_cfg *c) { c->min_mps = 1.4f, c->min_cm = 15.0f; }
void somvr_sg_swipe_init(somvr_sg_swipe *s)
{
    s->n = s->pos = 0, s->armed = 0; /* (a hand moving fast as it began re-arms first: a slow sample) */
}

int somvr_sg_swipe_step(somvr_sg_swipe *s, const somvr_sg_swipe_cfg *c, float t, float x, float y, float dir[2],
                        float *mps)
{
    *mps = 0;
    if (!isfinite(t + x + y)) {
        s->n = 0;
        return 0;
    }
    int now = s->pos;
    s->t[now] = t, s->x[now] = x, s->y[now] = y;
    s->pos = (now + 1) % SOMVR_SG_HIST, s->n += s->n < SOMVR_SG_HIST;
    int k = -1;
    for (int j = 1; j < s->n; j++) { /* the newest sample at least 80 ms older (at most 250) */
        int ix = (now - j + SOMVR_SG_HIST) % SOMVR_SG_HIST;
        float age = t - s->t[ix];
        if (age > 0.25f)
            break;
        if (age >= 0.08f) {
            k = ix;
            break;
        }
    }
    if (k < 0)
        return 0;
    float age = t - s->t[k], dx = x - s->x[k], dy = y - s->y[k], d = sqrtf(dx * dx + dy * dy), sp = d / age;
    if (sp < c->min_mps * 100.0f / 3.0f)
        s->armed = 1;
    if (!s->armed || sp < c->min_mps * 100.0f || d < c->min_cm || sp > 1500.0f) /* (faster: a tracking jump) */
        return 0;
    s->armed = 0, dir[0] = dx / d, dir[1] = dy / d, *mps = sp * 0.01f;
    return 1;
}

int somvr_sg_lean(float dx, float dy, float lean_cm, float dir[2])
{
    float d = sqrtf(dx * dx + dy * dy);
    if (!isfinite(d) || !(lean_cm > 0) || d < lean_cm)
        return 0;
    dir[0] = dx / d, dir[1] = dy / d;
    return 1;
}

/* ---- Hold ---- */
void somvr_sg_hold_init(somvr_sg_hold *h, const int closed_at_start[2])
{
    for (int k = 0; k < 2; k++)
        h->armed[k] = !closed_at_start[k];
    h->on = 0, h->hand = -1, h->held = 0;
}

int somvr_sg_hold_step(somvr_sg_hold *h, const int closed[2], int in_window, float dt)
{
    for (int k = 0; k < 2; k++)
        if (!closed[k])
            h->armed[k] = 1;
    if (h->on && (h->hand < 0 || !closed[h->hand])) /* let go: the hold starts again at the next close */
        h->on = 0, h->hand = -1, h->held = 0;
    for (int k = 0; !h->on && k < 2; k++)
        if (h->armed[k] && closed[k])
            h->on = 1, h->hand = k, h->armed[k] = 0;
    if (h->on && in_window && dt > 0 && dt < 0.5f)
        h->held += dt;
    return h->on;
}

unsigned somvr_sg_steps_hold(float held_s, float need_s, int steps)
{
    if (!(need_s > 0))
        return (unsigned)steps;
    if (held_s >= need_s)
        return (unsigned)steps + 1;
    int k = (int)ceilf((1.0f - (held_s > 0 ? held_s : 0) / need_s) * (float)steps - 1e-3f);
    return (unsigned)(k < 0 ? 0 : k > steps ? steps : k);
}

unsigned somvr_sg_steps_count(int made, int need, int steps)
{
    if (need <= 0)
        return (unsigned)steps;
    if (made >= need)
        return (unsigned)steps + 1;
    int k = (int)ceilf((float)(need - (made > 0 ? made : 0)) * (float)steps / (float)need - 1e-3f);
    return (unsigned)(k < 0 ? 0 : k > steps ? steps : k);
}

/* ---- Aim ---- */
void somvr_sg_stab_defaults(somvr_sg_stab_cfg *c) { c->align_cm = 16.0f, c->align_deg = 30.0f, c->min_mps = 1.2f, c->laid_s = 0.6f; }
void somvr_sg_stab_init(somvr_sg_stab *s) { s->laid_until = -1, s->laid = 0, s->n_laid = 0; }

int somvr_sg_stab_step(somvr_sg_stab *s, const somvr_sg_stab_cfg *c, float t, const float tip[3], const float axis[3],
                       const float vel[3], const float otip[3], const float odir[3], float *d_cm, float *ang_deg,
                       float *along_mps)
{
    float d[3] = { tip[0] - otip[0], tip[1] - otip[1], tip[2] - otip[2] };
    float la = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    float lo = sqrtf(odir[0] * odir[0] + odir[1] * odir[1] + odir[2] * odir[2]);
    *d_cm = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float cs = la > 1e-6f && lo > 1e-6f ? (axis[0] * odir[0] + axis[1] * odir[1] + axis[2] * odir[2]) / (la * lo) : -1;
    cs = cs > 1 ? 1 : cs < -1 ? -1 : cs;
    *ang_deg = acosf(cs) * 57.29578f;
    *along_mps = lo > 1e-6f ? 0.01f * (vel[0] * odir[0] + vel[1] * odir[1] + vel[2] * odir[2]) / lo : 0;
    if (!isfinite(*d_cm + *ang_deg + *along_mps))
        return SOMVR_SGS_NONE;
    int aligned = *d_cm <= c->align_cm && *ang_deg <= c->align_deg;
    int near = *d_cm <= 3.0f * c->align_cm && *ang_deg <= 2.0f * c->align_deg; /* (the thrust carries the point past it) */
    int was = s->laid;
    if (aligned)
        s->laid_until = t + c->laid_s, s->laid = 1;
    else if (t > s->laid_until)
        s->laid = 0;
    if (s->laid && near && *along_mps >= c->min_mps) {
        s->laid = 0, s->laid_until = -1;
        return SOMVR_SGS_STAB;
    }
    if (s->laid && !was) {
        s->n_laid++;
        return SOMVR_SGS_LAID;
    }
    return s->laid ? SOMVR_SGS_STILL : SOMVR_SGS_NONE;
}
