/* somvr_vision.c - SOMVR_VISION: Y's wish and the taps that bring the wraith vision's flag to it (somvr_vision.h) */
#include "somvr_vision.h"

#include <string.h>

void somvr_vis_cfg_default(somvr_vis_cfg *c) {
    *c = (somvr_vis_cfg){.wait_s = 0.3f, .settle_s = 1.05f, .tries = 6, .tap_ticks = 2};
}

void somvr_vis_init(somvr_vis *v, int mode) {
    memset(v, 0, sizeof *v);
    v->mode = mode, v->want = -1, v->flag = -1;
}

const char *somvr_vis_mode_name(int mode) {
    return mode == SOMVR_VIS_HOLD ? "hold" : mode == SOMVR_VIS_TAP ? "tap" : "off";
}

static void wish(somvr_vis *v, int want, double t) { v->want = want, v->tries = 0, v->want_t = t; }

static void start_tap(somvr_vis *v, const somvr_vis_cfg *c, int to, double t, somvr_vis_out *o) {
    v->tap = c->tap_ticks > 0 ? c->tap_ticks : 1, v->to = to, v->tap_t = t, v->n_taps++;
    o->ev |= SOMVR_VIE_TAP, o->to = to;
}

void somvr_vis_tick(somvr_vis *v, const somvr_vis_cfg *c, int world, int y, int flag, double t, somvr_vis_out *o) {
    memset(o, 0, sizeof *o);
    o->flag = flag, o->want = v->want;
    if (v->mode != SOMVR_VIS_HOLD && v->mode != SOMVR_VIS_TAP) return;
    y = world && y;
    int press = y && !v->y;
    v->y = y;
    if (flag < 0 || flag > 1) flag = -1;
    /* the flag: a change while our tap waits is the tap's, else the game's own */
    if (flag >= 0 && v->flag >= 0 && flag != v->flag) {
        v->flag_t = t;
        if (v->pending) {
            v->pending = 0, v->n_landed++;
            o->ev |= SOMVR_VIE_LANDED, o->ms = (float)((t - v->tap_t) * 1e3);
        } else {
            v->n_game++;
            o->ev |= SOMVR_VIE_GAME;
        }
    }
    v->flag = flag;
    if (v->pending && (flag < 0 || t - v->tap_t > c->wait_s)) {
        v->pending = 0;
        if (flag >= 0) v->n_late++, o->ev |= SOMVR_VIE_LATE;
    }
    int eff = v->want >= 0 ? v->want : v->pending ? v->to : flag; /* the state it is in or going to (-1 unknown) */
    /* the wish */
    if (press) {
        v->n_press++;
        int on = eff != 1; /* hold and tap alike: on unless it is on (or going on) */
        wish(v, on, t);
        if (v->mode == SOMVR_VIS_HOLD) v->ours = on;
        o->ev |= on ? SOMVR_VIE_PRESS_ON : SOMVR_VIE_PRESS_OFF;
    } else if (v->mode == SOMVR_VIS_HOLD && v->ours && !y && v->want != 0) {
        wish(v, 0, t);
        o->ev |= SOMVR_VIE_RELEASE;
    }
    if (flag < 0) { /* blind: the wish's edge taps once (a tap still running: this wish is lost, as build 15b) */
        if ((o->ev & (SOMVR_VIE_PRESS_ON | SOMVR_VIE_PRESS_OFF | SOMVR_VIE_RELEASE)) && world && !v->tap && !v->gap) {
            start_tap(v, c, v->want, t, o);
            v->n_blind++, o->ev |= SOMVR_VIE_BLIND, o->tries = 1;
        }
        if (v->want == 0) v->ours = 0;
        v->want = -1;
    } else if (v->want >= 0 && !v->pending && !v->tap && !v->gap) {
        if (flag == v->want) {
            o->ev |= SOMVR_VIE_DONE, o->done_ms = (float)((t - v->want_t) * 1e3), o->tries = v->tries;
            if (!v->want) v->ours = 0;
            v->want = -1, v->tries = 0;
        } else if (world && (!v->flag_t || t - v->flag_t >= c->settle_s)) {
            if (v->tries >= c->tries) {
                v->n_giveup++;
                o->ev |= SOMVR_VIE_GIVEUP, o->tries = v->tries;
                v->want = -1, v->tries = 0, v->ours = 0; /* no tapping on and on: the next press tries again */
            } else {
                v->tries++, v->n_retry += v->tries > 1;
                start_tap(v, c, v->want, t, o);
                v->pending = 1, o->tries = v->tries;
            }
        }
    }
    if (v->tap > 0) {
        v->tap--;
        o->tap = world;
        if (!v->tap) v->gap = 1;
    } else if (v->gap > 0 && !(o->ev & SOMVR_VIE_TAP)) {
        v->gap--;
    }
    o->want = v->want;
}
