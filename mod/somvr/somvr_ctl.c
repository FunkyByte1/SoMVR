/* somvr_ctl.c - see somvr_ctl.h */
#include "somvr_ctl.h"

#include <math.h>
#include <string.h>

static const char *const g_pad_names[SC_NPAD] = {"UP", "DOWN", "LEFT", "RIGHT", "START", "BACK", "L3", "R3", "LB", "RB",
                                                 "A",  "B",    "X",    "Y",     "RT",    "LT",   "LX", "LY", "RX", "RY"};
const char *sc_pad_name(int idx) { return idx >= 0 && idx < SC_NPAD ? g_pad_names[idx] : "?"; }

int sc_context(int ifs, int menus, int paused, int video, int struggle) {
    if (ifs != 3 || menus != 0 || paused || video > 0) return SC_CTX_MENU;
    return struggle ? SC_CTX_STRUGGLE : SC_CTX_WORLD;
}
const char *sc_ctx_name(int ctx) { return ctx == SC_CTX_WORLD ? "world" : ctx == SC_CTX_STRUGGLE ? "struggle" : "menu"; }

int sc_denied(int cmd, int ctx) {
    if (cmd < 0 || cmd >= SC_NCMD) return 1;
    if ((cmd >= 118 && cmd <= 120) || cmd == 122 || cmd == 123 || (cmd >= 126 && cmd <= 131) || /* combat slots */
        (cmd >= 136 && cmd <= 159) ||                                                         /* KBM, SpecialAttack */
        (cmd >= 189 && cmd <= 215) || (cmd >= 231 && cmd <= 242) || cmd == 244 ||             /* Debug* */
        (cmd >= 317 && cmd <= 344))                                                           /* PhotoMode_*, Debug */
        return 1;
    return ctx != SC_CTX_MENU && (cmd == SC_CMD_PITCHAXIS || cmd == SC_CMD_YAWAXIS || cmd == SC_CMD_PITCHACCEL ||
                                  cmd == SC_CMD_YAWACCEL || cmd == SC_CMD_CENTERVIEW);
}

void sc_deadzone(const float in[2], float dead, float out[2]) {
    float x = in[0], y = in[1], r = sqrtf(x * x + y * y);
    if (!(r > dead) || !isfinite(r)) { /* also NaN */
        out[0] = out[1] = 0;
        return;
    }
    float rc = r > 1 ? 1 : r, k = (rc - dead) / ((1 - dead) * r);
    out[0] = x * k, out[1] = y * k;
}

void sc_rotate(float v[2], float d) {
    float c = cosf(d), s = sinf(d), x = v[0], y = v[1];
    v[0] = x * c + y * s;
    v[1] = -x * s + y * c;
}

sv_vec3 sc_turn_p0(sv_vec3 p0, sv_vec3 h, float dpsi) {
    sv_vec3 r = sv_qrot(sv_qyaw(-dpsi), (sv_vec3){h.x - p0.x, 0, h.z - p0.z});
    return (sv_vec3){h.x - r.x, p0.y, h.z - r.z};
}

void sc_ctl_cfg_default(sc_ctl_cfg *c) {
    memset(c, 0, sizeof *c);
    c->dead = 0.15f;
    c->snap_on = 0.7f, c->snap_off = 0.3f;
    c->smooth_rate = 120.0f * 0.01745329252f, c->smooth_ease = 0.1f;
    c->sprint_r = 0.2f, c->sprint_s = 0.3f;
    c->crouch_on = -0.7f, c->crouch_off = -0.3f;
    c->grip_on = 0.6f, c->grip_off = 0.4f;
    c->trig_on = 0.6f, c->trig_off = 0.4f;
    c->flick_on = 0.7f, c->flick_off = 0.5f;
    c->rep0 = 0.4f, c->rep = 0.12f;
    c->stall_s = 0.05f, c->stall_ticks = 4;
    c->game_moves = 1;
}

const char *sc_live_name(int why) {
    static const char *const n[] = {"live", "no snapshot read", "no sync from the layer yet", "the XR session is not running",
                                    "not focused or not synced", "the snapshot stalled"};
    return why >= 0 && why <= SC_LIVE_STALLED ? n[why] : "?";
}

void sc_ctl_reset(sc_ctl *s) { memset(s, 0, sizeof *s); }

int sc_live(sc_ctl *s, const sc_ctl_cfg *c, const somvr_input_state *st, int read_ok, int running, double t, int *why) {
    int w = SC_LIVE_OK;
    if (!read_ok || !st) w = SC_LIVE_NOREAD;
    else if (!st->sync_seq) w = SC_LIVE_NOSYNC;
    else if (!running) w = SC_LIVE_NOTRUNNING;
    else if ((st->flags & (SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED)) != (SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED))
        w = SC_LIVE_UNFOCUSED;
    if (w == SC_LIVE_OK) {
        if (st->sync_seq != s->seq || !s->same_t) {
            s->seq = st->sync_seq, s->same_ticks = 0, s->same_t = t;
        } else if (++s->same_ticks >= c->stall_ticks && t - s->same_t >= c->stall_s) {
            w = SC_LIVE_STALLED; /* the layer stopped syncing while the game ticks */
        }
    } else {
        s->same_ticks = 0, s->same_t = 0;
    }
    *why = w;
    return w == SC_LIVE_OK;
}

/* one hand's logical buttons (LOWER, UPPER, STICK, MENU) this tick, from the held bits and the layer's counters */
static uint32_t buttons(sc_ctl *s, const somvr_hand_in *hi, int h, uint32_t *edges) {
    int act = (hi->active & SOMVR_HACT_INPUT) != 0;
    uint32_t held = act ? hi->buttons : 0, out = 0;
    for (int b = 0; b < 4; b++) {
        uint32_t m = 1u << b;
        uint8_t np = (uint8_t)(hi->presses[b] - s->presses[h][b]);
        s->presses[h][b] = hi->presses[b];
        int hd = (held & m) != 0, prev = (s->down[h] & m) != 0, lg;
        if (!act) {
            s->armed[h] &= ~m, s->again[h] &= ~m, lg = 0;
        } else if (!(s->armed[h] & m)) { /* after a loss (or at the start): only once it has been seen up */
            if (!hd) s->armed[h] |= m;
            lg = 0;
        } else if (s->again[h] & m) { /* the second half of a release + press between two ticks */
            s->again[h] &= ~m, lg = 1;
        } else if (np && prev) {
            s->again[h] |= m, lg = 0, *edges |= SC_E_REPRESS;
        } else if (np && !hd) { /* a tap shorter than a tick: down for this one */
            lg = 1, *edges |= SC_E_TAP;
        } else {
            lg = hd;
        }
        if (lg) out |= m;
    }
    s->down[h] = out;
    return out;
}

static void add_cmd(sc_tick_out *o, int cmd) {
    if (o->ncmd < SC_OUT_CMDS) o->cmd[o->ncmd++] = cmd;
}
static void pad(sc_tick_out *o, int i, float v) {
    if (fabsf(v) < 1e-4f) v = 0; /* a rotation's rounding is no deflection */
    o->pad[i] = v;
    if (v != 0) o->pad_on |= 1u << i;
}
static void stick_of(const somvr_hand_in *hi, float v[2]) {
    int ok = (hi->active & SOMVR_HACT_INPUT) && isfinite(hi->stick[0]) && isfinite(hi->stick[1]);
    v[0] = ok ? hi->stick[0] : 0, v[1] = ok ? hi->stick[1] : 0;
}
static float analog(const somvr_hand_in *hi, float v) { return (hi->active & SOMVR_HACT_INPUT) && v > 0 && v <= 1 ? v : 0; }
static int hyst(int *dn, float v, float on, float off) { return *dn = *dn ? v > off : v > on; }

void sc_tick(sc_ctl *s, const sc_ctl_cfg *c, const somvr_input_state *st, int live, int ctx, float dpsi, double t,
             sc_tick_out *o) {
    memset(o, 0, sizeof *o);
    o->ctx = ctx;
    float dt = s->last_t > 0 && t > s->last_t ? (float)(t - s->last_t) : 0;
    if (dt > 0.1f) dt = 0.1f;
    s->last_t = t;
    if (!live || !st) {
        if (s->was_live || s->have) { /* everything up and disarmed; the liveness tracking (sc_live's) stays */
            uint64_t seq = s->seq;
            int same = s->same_ticks;
            double same_t = s->same_t;
            sc_ctl_reset(s);
            s->seq = seq, s->same_ticks = same, s->same_t = same_t, s->last_t = t;
        }
        return;
    }
    if (!s->have) { /* baseline: presses before this tick never fire */
        for (int h = 0; h < 2; h++) memcpy(s->presses[h], st->hand[h].presses, sizeof s->presses[h]);
        s->have = 1;
    }
    s->was_live = 1, o->live = 1;
    uint32_t prev[2] = {s->down[0], s->down[1]}, dn[2];
    for (int h = 0; h < 2; h++) dn[h] = buttons(s, &st->hand[h], h, &o->edges);
    const somvr_hand_in *L = &st->hand[SOMVR_HAND_L], *R = &st->hand[SOMVR_HAND_R];
    float ls[2], rs[2];
    stick_of(L, ls), stick_of(R, rs);
    o->move_raw[0] = ls[0], o->move_raw[1] = ls[1];
    o->raw = ((dn[1] & SOMVR_BTN_LOWER) ? 1u << SC_PAD_A : 0) | ((dn[1] & SOMVR_BTN_UPPER) ? 1u << SC_PAD_B : 0) |
             ((dn[0] & SOMVR_BTN_LOWER) ? 1u << SC_PAD_X : 0) | ((dn[0] & SOMVR_BTN_UPPER) ? 1u << SC_PAD_Y : 0) |
             (analog(L, L->squeeze) >= c->grip_on ? 1u << SC_PAD_LB : 0) | (analog(R, R->squeeze) >= c->grip_on ? 1u << SC_PAD_RB : 0);

    if (ctx == SC_CTX_MENU) { /* the controllers are an Xbox pad (passthrough to the bound, active commands) */
        s->sprint = 0, s->snap_armed = 0, s->crouch_armed = 0, s->ease_t = 0;
        float lv[2], rv[2];
        sc_deadzone(ls, c->dead, lv), sc_deadzone(rs, c->dead, rv);
        pad(o, SC_PAD_LX, lv[0]), pad(o, SC_PAD_LY, lv[1]), pad(o, SC_PAD_RX, rv[0]), pad(o, SC_PAD_RY, rv[1]);
        pad(o, SC_PAD_A, (dn[1] & SOMVR_BTN_LOWER) ? 1.0f : 0), pad(o, SC_PAD_B, (dn[1] & SOMVR_BTN_UPPER) ? 1.0f : 0);
        pad(o, SC_PAD_X, (dn[0] & SOMVR_BTN_LOWER) ? 1.0f : 0), pad(o, SC_PAD_Y, (dn[0] & SOMVR_BTN_UPPER) ? 1.0f : 0);
        pad(o, SC_PAD_START, (dn[0] & SOMVR_BTN_MENU) ? 1.0f : 0);
        pad(o, SC_PAD_L3, (dn[0] & SOMVR_BTN_STICK) ? 1.0f : 0), pad(o, SC_PAD_BACK, (dn[1] & SOMVR_BTN_STICK) ? 1.0f : 0);
        pad(o, SC_PAD_LB, hyst(&s->grip_dn[0], analog(L, L->squeeze), c->grip_on, c->grip_off) ? 1.0f : 0);
        pad(o, SC_PAD_RB, hyst(&s->grip_dn[1], analog(R, R->squeeze), c->grip_on, c->grip_off) ? 1.0f : 0);
        for (int h = 0; h < 2; h++) { /* the triggers: pressed = the digital state, then with the analog value */
            const somvr_hand_in *hi = h ? R : L;
            float v = analog(hi, hi->trigger);
            if (hyst(&s->trig_dn[h], v, c->trig_on, c->trig_off)) pad(o, h ? SC_PAD_RT : SC_PAD_LT, v);
        }
        float r = sqrtf(ls[0] * ls[0] + ls[1] * ls[1]);
        int dir = fabsf(ls[0]) > fabsf(ls[1]) ? (ls[0] > 0 ? SC_PAD_RIGHT : SC_PAD_LEFT) : (ls[1] > 0 ? SC_PAD_UP : SC_PAD_DOWN);
        if (r < c->flick_off) {
            s->flick_dir = 0;
        } else if (r > c->flick_on) { /* a d-pad edge on the flick, repeated while held */
            int fire = 0;
            if (s->flick_dir != dir + 1) s->flick_dir = dir + 1, s->flick_next = t + c->rep0, fire = 1;
            else if (t >= s->flick_next) s->flick_next = t + c->rep, fire = 1;
            if (fire) pad(o, dir, 1.0f), o->edges |= SC_E_DPAD;
        }
        return;
    }
    s->flick_dir = 0, s->grip_dn[0] = s->grip_dn[1] = s->trig_dn[0] = s->trig_dn[1] = 0;

    /* the world (and a struggle): locomotion, the stock A, the M2 buttons; the right stick turns the rig or crouches */
    sc_deadzone(ls, c->dead, o->move);
    sc_rotate(o->move, dpsi);
    pad(o, SC_PAD_LX, o->move[0]), pad(o, SC_PAD_LY, o->move[1]);
    if (c->game_moves || ctx == SC_CTX_STRUGGLE) /* Jump + FreeRun + LedgePullUp + CombatPrimary04, the stock A */
        pad(o, SC_PAD_A, (dn[1] & SOMVR_BTN_LOWER) ? 1.0f : 0);
    if (ctx == SC_CTX_STRUGGLE) { /* the stock prompt's buttons until the shake/shove gestures land */
        pad(o, SC_PAD_B, (dn[1] & SOMVR_BTN_UPPER) ? 1.0f : 0);
        pad(o, SC_PAD_X, (dn[0] & SOMVR_BTN_LOWER) ? 1.0f : 0), pad(o, SC_PAD_Y, (dn[0] & SOMVR_BTN_UPPER) ? 1.0f : 0);
    } else {
        if (dn[1] & SOMVR_BTN_UPPER) add_cmd(o, SC_CMD_ACTIVATE); /* B = interact (the stock RB), never pad B */
        if (dn[0] & SOMVR_BTN_UPPER) add_cmd(o, SC_CMD_VISION);   /* Y hold = wraith vision (RT) */
    }
    if (dn[0] & SOMVR_BTN_MENU) add_cmd(o, SC_CMD_MENU);
    if (dn[1] & SOMVR_BTN_STICK) add_cmd(o, SC_CMD_PDA); /* the map (the stock BACK) */
    if ((dn[0] & SOMVR_BTN_STICK) && !(prev[0] & SOMVR_BTN_STICK)) { /* sprint: latched until the stick rests */
        if (!s->sprint) o->edges |= SC_E_SPRINT;
        s->sprint = 1, s->sprint_low = 0;
    } else if (s->sprint) {
        s->sprint_low = sqrtf(ls[0] * ls[0] + ls[1] * ls[1]) < c->sprint_r ? s->sprint_low + dt : 0;
        if (s->sprint_low >= c->sprint_s) s->sprint = 0;
    }
    if (s->sprint && c->game_moves) add_cmd(o, SC_CMD_FREERUN);
    o->sprint = s->sprint;

    float x = rs[0], y = rs[1], ax = fabsf(x), ay = fabsf(y);
    if (ax < c->snap_off) s->snap_armed = 1;
    if (y > c->crouch_off) s->crouch_armed = 1;
    if (ay > ax) { /* the larger axis wins: down crouches */
        s->ease_t = 0;
        if (s->crouch_armed && y < c->crouch_on) {
            s->crouch_armed = 0;
            add_cmd(o, SC_CMD_CROUCH); /* one tick: ON now, OFF at the next */
            o->edges |= SC_E_CROUCH;
        }
    } else if (c->smooth) {
        if (ax > c->dead) {
            s->ease_t += dt;
            float k = c->smooth_ease > 0 ? fminf(1.0f, s->ease_t / c->smooth_ease) : 1.0f;
            o->turn = copysignf(c->smooth_rate * (fminf(ax, 1.0f) - c->dead) / (1 - c->dead) * k * dt, x);
        } else {
            s->ease_t = 0;
        }
    } else if (s->snap_armed && ax > c->snap_on) {
        s->snap_armed = 0;
        o->snap = x > 0 ? 1 : -1;
        o->edges |= SC_E_SNAP;
    }
}

/* ---- haptics -------------------------------------------------------------------------------------------------------- */
void sc_hap_reset(sc_hap *h) { memset(h, 0, sizeof *h); }

static float fx_level(const sc_hap_fx *f, double t) {
    if (!(f->dur > 0) || t < f->t0 || t >= f->t0 + f->dur) return 0;
    return f->ramp ? f->amp * (float)(1.0 - (t - f->t0) / f->dur) : f->amp;
}

void sc_hap_add(sc_hap *h, double t, float amp, float dur, int ramp, uint32_t source) {
    int pick = 0;
    for (int i = 1; i < SC_HAP_FX; i++)
        if (fx_level(&h->fx[i], t) < fx_level(&h->fx[pick], t)) pick = i;
    h->fx[pick] = (sc_hap_fx){t, dur, amp < 0 ? 0 : amp, ramp, source};
}

float sc_hap_level(const sc_hap *h, double t, float scale, uint32_t *src, float *remaining, int *continuous) {
    float best = 0, rem = 0;
    uint32_t s = SOMVR_HAP_NONE;
    int cont = 0;
    for (int i = 0; i < SC_HAP_FX; i++) {
        float l = fx_level(&h->fx[i], t);
        if (l > best) best = l, s = h->fx[i].source, rem = (float)(h->fx[i].t0 + h->fx[i].dur - t), cont = h->fx[i].ramp;
    }
    if (h->rumble > best) best = h->rumble, s = SOMVR_HAP_RUMBLE, rem = 0, cont = 1;
    best *= scale > 0 ? scale : 0;
    if (!(best >= 0)) best = 0;
    if (best > 1) best = 1;
    if (src) *src = s;
    if (remaining) *remaining = rem;
    if (continuous) *continuous = cont;
    return best;
}

int sc_hap_tick(sc_hap *h, double t, float scale, float period_s, somvr_haptic_req *out) {
    uint32_t src;
    float rem;
    int cont;
    float a = sc_hap_level(h, t, scale, &src, &rem, &cont);
    memset(out, 0, sizeof *out);
    h->ticks++;
    if (a < 0.01f) {
        int running = h->last > 0 && (t < h->last_end || h->last_end == 0);
        h->last = 0, h->last_end = 0;
        if (!running) return 0; /* a flat effect ended on its own: nothing to stop */
        out->amplitude = 0, out->duration_ns = -1, out->source = SOMVR_HAP_NONE;
        h->ticks = 0;
        return 1;
    }
    if (!(fabsf(a - h->last) > 0.02f) && !(cont && h->ticks >= 4)) return 0;
    if (!(period_s > 0)) period_s = 1.0f / 90;
    float d = cont ? 6 * period_s : rem;
    if (cont && rem > 0 && rem < d) d = rem;
    out->amplitude = a, out->frequency = 0, out->source = src;
    out->duration_ns = d > 1e-4f ? (int64_t)((double)d * 1e9) : -1;
    h->last = a, h->ticks = 0;
    h->last_end = cont ? 0 : t + d; /* 0 = continuous: a stop is posted when it ends */
    return 1;
}
