/* somvr_screens.c - see somvr_screens.h */
#include "somvr_screens.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const somvr_sd_phase_names[3] = { "idle", "dying", "nemesis" };
const char *const somvr_pm1_names[3] = { "off", "nemesis", "all" };

/* ==== 1. the death ==================================================================================================== */
void somvr_sd_cfg_default(somvr_sd_cfg *c) {
    memset(c, 0, sizeof *c);
    c->blink_out_s = 0.35f, c->blink_black_s = 0.6f, c->blink_in_s = 0.6f;
}

int somvr_sd_parse(const char *v, somvr_sd_cfg *c) {
    somvr_sd_cfg_default(c);
    if (!v || !*v) return 0;
    const char *rest = NULL;
    if (!strncmp(v, "first", 5) && (!v[5] || v[5] == ',')) c->on = 1, rest = v + 5;
    else if (!strncmp(v, "game", 4) && (!v[4] || v[4] == ',')) c->on = 0, rest = v + 4;
    else if (!strcmp(v, "off") || !strcmp(v, "0")) return 0;
    else return -1;
    if (!*rest) return 0;
    float ms[3];
    char *e;
    const char *p = rest;
    for (int i = 0; i < 3; i++) {
        if (*p != ',') goto bad;
        ms[i] = strtof(p + 1, &e);
        if (e == p + 1 || !(ms[i] >= 0 && ms[i] <= 5000)) goto bad;
        p = e;
    }
    if (*p) goto bad;
    c->blink_out_s = ms[0] / 1000, c->blink_black_s = ms[1] / 1000, c->blink_in_s = ms[2] / 1000;
    return 0;
bad:
    somvr_sd_cfg_default(c);
    return -1;
}

void somvr_sd_init(somvr_sd *d) { memset(d, 0, sizeof *d); }

void somvr_sd_step(somvr_sd *d, const somvr_sd_cfg *c, int death, int win, int win_cond, int paused, int bm_win,
                   int bm_frozen, int death_cond, double t, somvr_sd_out *o) {
    memset(o, 0, sizeof *o);
    o->win = win, o->win_cond = win_cond, o->step_paused = paused;
    d->had_win = win;
    if (!c->on) {
        if (d->phase != SOMVR_SD_IDLE) d->phase = SOMVR_SD_IDLE, o->changed = 1;
        return;
    }
    int ph0 = d->phase;
    switch (d->phase) {
    case SOMVR_SD_IDLE:
        if (death) d->phase = SOMVR_SD_DYING, d->t0 = t, d->deaths++;
        break;
    case SOMVR_SD_DYING:
        if (bm_win) { /* the window opened (the Nemesis screens' pause) */
            d->phase = SOMVR_SD_NEMESIS, d->t0 = t, d->nemesis++, d->paused_opens += paused != 0;
        } else if (!death && !bm_frozen) { /* the death hold is over without the window: alive again (a last chance) */
            d->phase = SOMVR_SD_IDLE, d->t0 = t, d->recovered++;
        }
        break;
    case SOMVR_SD_NEMESIS:
        if (!bm_win && !death) d->phase = SOMVR_SD_IDLE, d->t0 = t, d->respawns++; /* back in the world */
        break;
    }
    if (d->phase == SOMVR_SD_DYING) {
        if (paused) { /* the Nemesis screens (the death's hold goes on into the pause): the window, the game paused */
            o->win = 1, o->win_cond = death_cond, o->step_paused = 0;
        } else if (death && win) { /* the game's death presentation: the view stays the player's */
            o->win = 0, o->win_cond = -1, o->dropped = 1, d->dropped_frames++;
        }
        o->slow_blink = 1;
    }
    o->changed = d->phase != ph0;
}

/* ==== 2. the pointer's Mouse1 ========================================================================================= */
int somvr_pm1_parse(const char *v, int *bad) {
    if (!v || !*v || !strcmp(v, "off") || !strcmp(v, "0")) return SOMVR_PM1_OFF;
    if (!strcmp(v, "nemesis") || !strcmp(v, "on") || !strcmp(v, "1")) return SOMVR_PM1_NEMESIS;
    if (!strcmp(v, "all")) return SOMVR_PM1_ALL;
    if (bad) *bad = 1;
    return SOMVR_PM1_OFF;
}

int somvr_pm1_want(int mode, int click_held, int in_menu, int top_is_nemesis) {
    if (!click_held || !in_menu) return 0;
    return mode == SOMVR_PM1_ALL || (mode == SOMVR_PM1_NEMESIS && top_is_nemesis);
}

int somvr_scr_top_index(uint32_t count) { return count >= 1 && count <= 8 ? (int)count - 1 : -1; }
