/* somvr_crouch.c - crouch by ducking: the duck's state machine and the stealth hold (somvr_crouch.h) */
#include "somvr_crouch.h"

#include <math.h>
#include <string.h>

void somvr_crouch_cfg_default(somvr_crouch_cfg *c) {
    *c = (somvr_crouch_cfg){.depth_cm = 28, .up_cm = 18, .on_s = 0.15f, .off_s = 0.2f, .hold_s = 0.5f, .rise_cm = 5,
                            .rise_s = 1.0f, .jump_cm = 40};
}

void somvr_crouch_init(somvr_crouch *s) { memset(s, 0, sizeof *s); }

void somvr_crouch_stand(somvr_crouch *s, float y_cm) {
    if (!isfinite(y_cm)) return;
    s->have = 1, s->stand = y_cm, s->stands++;
    s->t_below = s->t_above = s->t_rise = 0;
}

float somvr_crouch_depth(const somvr_crouch *s) { return s->have && s->have_y ? s->stand - s->y : 0; }

int somvr_crouch_step(somvr_crouch *s, const somvr_crouch_cfg *c, double t, float y, int ok, int armed) {
    if (!armed || !s->have) {
        s->t_below = s->t_above = s->t_rise = 0;
        if (s->ducked) {
            s->ducked = 0, s->offs++;
            return -1;
        }
        return 0;
    }
    if (!ok || !isfinite(y)) { /* untracked: the state holds, the timers restart */
        s->t_below = s->t_above = s->t_rise = 0;
        return 0;
    }
    int jump = s->have_y && fabsf(y - s->last_y) > c->jump_cm;
    s->y = s->last_y = y, s->have_y = 1;
    if (jump) {
        s->jumps++;
        s->t_below = s->t_above = s->t_rise = 0;
        return 0;
    }
    /* the standing height follows a taller stance */
    if (y > s->stand + c->rise_cm) {
        if (s->t_rise <= 0) s->t_rise = t, s->rise_min = y;
        else if (y < s->rise_min) s->rise_min = y;
        if (t - s->t_rise >= c->rise_s) s->stand = s->rise_min, s->rises++, s->t_rise = 0;
    } else {
        s->t_rise = 0;
    }
    if (!s->ducked) {
        if (y < s->stand - c->depth_cm) {
            if (s->t_below <= 0) s->t_below = t;
            if (t - s->t_below >= c->on_s) {
                s->ducked = 1, s->t_on = t, s->t_below = s->t_above = 0, s->ons++;
                return 1;
            }
        } else {
            s->t_below = 0;
        }
        return 0;
    }
    if (y > s->stand - c->up_cm) {
        if (s->t_above <= 0) s->t_above = t;
        if (t - s->t_above >= c->off_s && t - s->t_on >= c->hold_s) {
            s->ducked = 0, s->t_above = s->t_below = 0, s->offs++;
            return -1;
        }
    } else {
        s->t_above = 0;
    }
    return 0;
}

void somvr_crouch_hold_init(somvr_crouch_hold *h) { memset(h, 0, sizeof *h); }

int somvr_crouch_hold_step(somvr_crouch_hold *h, int edge, int button, int ducked) {
    if (edge) h->latched = 0, h->duck_off = 0; /* a duck's start takes over, its end stands up */
    if (button) {
        int held = h->latched || (ducked && !h->duck_off);
        h->toggles++;
        if (held) h->latched = 0, h->duck_off = ducked;
        else h->latched = 1;
    }
    return h->latched || (ducked && !h->duck_off);
}

int somvr_crouch_hold_release(somvr_crouch_hold *h, int ducked) {
    int held = h->latched || (ducked && !h->duck_off);
    if (held) h->latched = 0, h->duck_off = ducked;
    return held;
}
