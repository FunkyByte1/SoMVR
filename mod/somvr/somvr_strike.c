/* somvr_strike.c - the wraith dash's pure part (somvr_strike.h) */
#include "somvr_strike.h"

#include <math.h>
#include <string.h>

static const char *const g_sk_modes[SOMVR_BSK_NMODE] = { "block", "game", "dash", "blink" };

void somvr_sk_cfg_default(somvr_sk_cfg *c) {
    memset(c, 0, sizeof *c);
    c->mode = SOMVR_BSK_BLOCK;
    c->press_s = 0.15f, c->window_s = 2.0f, c->start_cm = 100, c->start_cms = 600, c->hold_max_s = 0.8f;
    c->dash_s = 0.30f, c->ease_s = 0.06f, c->tunnel = 0.4f;
    c->blink_in_s = 0.03f, c->blink_out_s = 0.07f;
}

const char *somvr_sk_mode_name(int mode) { return mode >= 0 && mode < SOMVR_BSK_NMODE ? g_sk_modes[mode] : "?"; }

int somvr_sk_mode_parse(const char *s) {
    for (int i = 0; s && i < SOMVR_BSK_NMODE; i++) {
        size_t n = strlen(g_sk_modes[i]);
        if (!strncmp(s, g_sk_modes[i], n) && (!s[n] || s[n] == ' ' || s[n] == '\n'))
            return i;
    }
    return -1;
}

static float sk_clampe(float T, float e) { return e < 0 ? 0 : e > 0.5f * T ? 0.5f * T : e; }

float somvr_sk_vmax(float D, float T, float e) {
    e = sk_clampe(T, e);
    return T - e > 1e-6f ? D / (T - e) : 0;
}

/* the speed rises as v (1 - cos(pi t / e)) / 2 over e: way v e / 2; constant v; falls the same way. T = 1 way. */
float somvr_sk_profile(float tau, float T, float e) {
    if (!(T > 0) || !(tau > 0)) return 0;
    if (tau >= T) return 1;
    e = sk_clampe(T, e);
    float v = 1.0f / (T - e); /* the way's share per second at constant speed */
    const float pi = 3.14159265f;
    if (e > 0 && tau < e) return v * (0.5f * tau - e / (2 * pi) * sinf(pi * tau / e));
    if (e > 0 && tau > T - e) return 1 - somvr_sk_profile(T - tau, T, e);
    return v * (0.5f * e + (tau - e));
}

float somvr_sk_speed(float tau, float T, float e) {
    if (!(T > 0) || !(tau > 0) || tau >= T) return 0;
    e = sk_clampe(T, e);
    if (e > 0 && tau < e) return 0.5f * (1 - cosf(3.14159265f * tau / e));
    if (e > 0 && tau > T - e) return somvr_sk_speed(T - tau, T, e);
    return 1;
}

void somvr_sk_reset(somvr_sk *s) {
    uint64_t p = s->presses, m = s->moves, to = s->timeouts, f = s->frames, cf = s->carried_frames;
    memset(s, 0, sizeof *s);
    s->presses = p, s->moves = m, s->timeouts = to, s->frames = f, s->carried_frames = cf;
}

void somvr_sk_press(somvr_sk *s, double t) {
    s->presses++;
    if (s->phase == SOMVR_BSK_MOVE) return;
    s->phase = SOMVR_BSK_ARMED, s->press_t = t, s->carried = 0;
}

static float sk_dist(sv_vec3 a, sv_vec3 b) {
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}

void somvr_sk_step(somvr_sk *s, const somvr_sk_cfg *c, double t, sv_vec3 p, somvr_sk_out *o) {
    memset(o, 0, sizeof *o);
    o->view = p;
    s->frames++;
    int moving_modes = c->mode == SOMVR_BSK_DASH || c->mode == SOMVR_BSK_BLINK;
    if (!moving_modes || !isfinite(p.x + p.y + p.z)) {
        if (s->phase != SOMVR_BSK_IDLE) s->phase = SOMVR_BSK_IDLE, o->ended = 1;
        s->have_last = s->have_vlast = 0;
        return;
    }
    float d = s->have_last ? sk_dist(p, s->last) : 0, dt = s->have_last ? (float)(t - s->last_t) : 0;
    int jump = s->have_last && d > c->start_cm, fast = s->have_last && dt > 1e-4f && d / dt > c->start_cms;
    sv_vec3 from = s->have_vlast ? s->vlast : s->last;
    if (s->phase == SOMVR_BSK_ARMED) {
        if (jump) {
            s->phase = SOMVR_BSK_MOVE, s->t0 = t, s->a0 = from;
        } else if (fast) { /* the game's lunge: the view stays */
            s->phase = SOMVR_BSK_HOLD, s->t0 = t, s->held = from;
        } else if (t - s->press_t > c->window_s) {
            s->phase = SOMVR_BSK_IDLE, s->timeouts++, o->timed_out = 1;
        }
        if (s->phase == SOMVR_BSK_MOVE) s->dist0 = sk_dist(p, s->a0), s->jumped = d, s->jumped_once = 0, s->moves++, o->started = 1;
    } else if (s->phase == SOMVR_BSK_HOLD) {
        if (jump || t - s->t0 >= c->hold_max_s) {
            s->phase = SOMVR_BSK_MOVE, s->t0 = t, s->a0 = s->held, s->dist0 = sk_dist(p, s->a0), s->jumped = d;
            s->jumped_once = 0, s->moves++, o->started = 1;
        }
    }
    if (s->phase == SOMVR_BSK_HOLD) {
        s->carried++, s->carried_frames++, o->carried = fast, o->held = 1;
        o->view = s->held, o->active = 1;
    } else if (s->phase == SOMVR_BSK_MOVE) {
        float tau = (float)(t - s->t0);
        if (c->mode == SOMVR_BSK_DASH) {
            if (tau >= c->dash_s) {
                s->phase = SOMVR_BSK_IDLE, o->ended = 1, o->f = 1;
            } else {
                float f = somvr_sk_profile(tau, c->dash_s, c->ease_s), sp = somvr_sk_speed(tau, c->dash_s, c->ease_s);
                o->view = (sv_vec3){ s->a0.x + (p.x - s->a0.x) * f, s->a0.y + (p.y - s->a0.y) * f,
                                     s->a0.z + (p.z - s->a0.z) * f };
                o->active = 1, o->f = f, o->speed = sp, o->tunnel = c->tunnel * sp;
            }
        } else { /* the blink: up over blink_in_s at A0, the jump under full white, down over blink_out_s */
            float in = c->blink_in_s > 0 ? c->blink_in_s : 0, out = c->blink_out_s > 0 ? c->blink_out_s : 0;
            if (tau < in) {
                o->view = s->a0, o->active = 1, o->blink = tau / in;
            } else if (!s->jumped_once) { /* the jump's frame: full white whatever the frame rate (20 Hz headless) */
                s->jumped_once = 1, s->t_jump = t, o->blink = 1, o->f = 1;
            } else if (t - s->t_jump < out) {
                o->blink = 1 - (float)(t - s->t_jump) / out, o->f = 1;
            } else {
                s->phase = SOMVR_BSK_IDLE, o->ended = 1, o->f = 1;
            }
        }
    }
    s->last = p, s->last_t = t, s->have_last = 1;
    s->vlast = o->view, s->have_vlast = 1;
}
