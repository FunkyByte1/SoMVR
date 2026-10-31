/* somvr_map.c - see somvr_map.h */
#include "somvr_map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *const somvr_map_region_names[4] = { "pan", "A (the waypoint)", "X (Fast Travel)", "Y (Advance Time)" };
const char *const somvr_map_phase_names[5] = { "idle", "down", "steer", "pan", "press" };

void somvr_map_cfg_default(somvr_map_cfg *c) {
    memset(c, 0, sizeof *c);
    c->pan_pxs = 310.0f; /* boot 20261030-084650: MapMoveX 1.0 for 1.0 s = 375 px, for 0.51 s = 223 px: the pan */
    c->lag_s = -0.20f;   /* runs on ~65 px after the stick is let go (a negative start: the planned time shorter) */
    c->steer_px = 360.0f;
    c->click_s = 0.45f; /* a trigger pulled and let go; longer = a held press (steering) */
    c->centre_px = 30.0f;
    c->min_px = 8.0f;
    c->ysign = 1.0f;
}

int somvr_map_parse(const char *v, somvr_map_cfg *c) {
    somvr_map_cfg_default(c);
    if (!v || !v[0]) return 0;
    const char *rest = NULL;
    if (!strncmp(v, "on", 2) && (!v[2] || v[2] == ',')) c->on = 1, rest = v + 2;
    else if (!strncmp(v, "off", 3) && (!v[3] || v[3] == ',')) rest = v + 3;
    else return -1;
    if (!*rest) return 0;
    char *e;
    float n[3];
    const char *p = rest;
    for (int i = 0; i < 3; i++) {
        if (*p != ',') return somvr_map_cfg_default(c), -1;
        n[i] = strtof(p + 1, &e);
        if (e == p + 1) return somvr_map_cfg_default(c), -1;
        p = e;
    }
    if (*p || !(n[0] >= 50 && n[0] <= 5000) || !(n[1] >= -500 && n[1] <= 1000) || !(n[2] >= 50 && n[2] <= 1000))
        return somvr_map_cfg_default(c), -1;
    c->pan_pxs = n[0], c->lag_s = n[1] / 1000.0f, c->steer_px = n[2];
    return 0;
}

int somvr_map_region(const somvr_map_cfg *c, const float px[2], int travel) {
    if (!isfinite(px[0]) || !isfinite(px[1])) return SOMVR_MAPR_PAN;
    if (px[0] * px[0] + px[1] * px[1] <= c->centre_px * c->centre_px) return SOMVR_MAPR_A;
    if (travel && px[0] >= SOMVR_MAP_POP_X0 && px[0] <= SOMVR_MAP_POP_X1 && px[1] >= SOMVR_MAP_POP_Y0 &&
        px[1] <= SOMVR_MAP_POP_Y1)
        return px[1] < SOMVR_MAP_POP_YM ? SOMVR_MAPR_X : SOMVR_MAPR_Y;
    return SOMVR_MAPR_PAN;
}

void somvr_map_init(somvr_map_st *s) { memset(s, 0, sizeof *s); }

/* the stick's values for a direction in UI pixels (+y down): MapMoveX right, MapMoveY up by ysign */
static void axis_of(const somvr_map_cfg *c, float dx, float dy, float k, somvr_map_out *o) {
    o->naxis = 2;
    o->axis[0] = dx * k;
    o->axis[1] = -dy * k * c->ysign;
}

static void press(somvr_map_st *s, int region, double t) {
    s->phase = SOMVR_MAPP_PRESS, s->t0 = t, s->end = t + 0.12;
    s->ncmd = 0;
    if (region == SOMVR_MAPR_A) s->cmd[s->ncmd++] = SOMVR_MAP_CMD_ENTER, s->cmd[s->ncmd++] = SOMVR_MAP_CMD_SELECT, s->a++;
    else if (region == SOMVR_MAPR_X) s->cmd[s->ncmd++] = SOMVR_MAP_CMD_TRAVEL, s->x++;
    else s->cmd[s->ncmd++] = SOMVR_MAP_CMD_TIME, s->y++;
}

void somvr_map_step(somvr_map_st *s, const somvr_map_cfg *c, const somvr_map_in *in, somvr_map_out *o) {
    memset(o, 0, sizeof *o);
    if (!c->on) return;
    double t = in->t;
    if (!in->map) {
        if (s->phase != SOMVR_MAPP_IDLE) o->event = SOMVR_MAPE_CLOSED;
        s->phase = SOMVR_MAPP_IDLE;
        return;
    }
    if (in->down && in->ptr && s->phase != SOMVR_MAPP_PRESS) { /* a press: what it means is decided at once */
        if (s->phase == SOMVR_MAPP_PAN || s->phase == SOMVR_MAPP_STEER) s->cancels++;
        s->down[0] = in->px[0], s->down[1] = in->px[1], s->t_down = t;
        s->clicks++, o->event = SOMVR_MAPE_CLICK;
        s->region = somvr_map_region(c, s->down, in->travel);
        if (s->region != SOMVR_MAPR_PAN) {
            press(s, s->region, t);
            o->ncmd = s->ncmd, o->cmd[0] = s->cmd[0], o->cmd[1] = s->cmd[1];
            return;
        }
        float L = sqrtf(s->down[0] * s->down[0] + s->down[1] * s->down[1]);
        if (!(L >= c->min_px)) {
            s->phase = SOMVR_MAPP_DOWN, s->plan_px = 0, s->plan_s = 0; /* at the cross already: held = steering */
            return;
        }
        s->dir[0] = s->down[0] / L, s->dir[1] = s->down[1] / L;
        s->plan_px = L, s->plan_s = L / c->pan_pxs + c->lag_s;
        if (s->plan_s < 0.05f) s->plan_s = 0.05f; /* the shortest stick touch: a nudge (the game's snap takes it on) */
        s->phase = SOMVR_MAPP_PAN, s->t0 = t, s->end = t + s->plan_s, s->pans++;
        axis_of(c, s->dir[0], s->dir[1], 1.0f, o);
        s->ticks_axis++;
        return;
    }
    switch (s->phase) {
    case SOMVR_MAPP_DOWN: /* a press at the cross, no pan: held long enough it steers */
    case SOMVR_MAPP_PAN:
        if (s->phase == SOMVR_MAPP_PAN && t < s->end) {
            axis_of(c, s->dir[0], s->dir[1], 1.0f, o);
            s->ticks_axis++;
            return;
        }
        if (in->pressing && t - s->t_down >= c->click_s) {
            s->phase = SOMVR_MAPP_STEER, s->t0 = t, s->steers++, o->event = SOMVR_MAPE_STEER_ON;
            goto steer;
        }
        if (!in->pressing) {
            if (s->phase == SOMVR_MAPP_PAN) o->event = SOMVR_MAPE_PAN_END;
            s->phase = SOMVR_MAPP_IDLE;
        }
        return; /* the pan done, still held: steering from click_s on */
    case SOMVR_MAPP_STEER:
        if (!in->pressing) {
            s->phase = SOMVR_MAPP_IDLE, o->event = SOMVR_MAPE_STEER_OFF;
            return;
        }
    steer:
        if (in->ptr && isfinite(in->px[0]) && isfinite(in->px[1])) {
            float dx = in->px[0] / c->steer_px, dy = in->px[1] / c->steer_px, m = sqrtf(dx * dx + dy * dy);
            if (m > 1.0f) dx /= m, dy /= m;
            if (m * c->steer_px >= c->min_px) {
                axis_of(c, dx, dy, 1.0f, o);
                s->ticks_axis++;
            }
        }
        return;
    case SOMVR_MAPP_PRESS:
        if (t >= s->end) {
            s->phase = SOMVR_MAPP_IDLE;
            return;
        }
        o->ncmd = s->ncmd, o->cmd[0] = s->cmd[0], o->cmd[1] = s->cmd[1];
        return;
    default:
        return;
    }
}
