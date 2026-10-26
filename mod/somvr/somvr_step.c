/* somvr_step.c - see somvr_step.h */
#include "somvr_step.h"

#include <math.h>
#include <string.h>

#include "somvr_loco.h"

#define ST_PI 3.14159265358979f
#define ST_DEG 0.01745329252f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float hlen(sv_vec3 v) { return sqrtf(v.x * v.x + v.z * v.z); }
static float hdot(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.z * b.z; }
static int vok(sv_vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }

static const char *const g_kind[SOMVR_ST_KINDS] = { "none", "step", "dash", "phase" };
const char *somvr_step_kind_name(int k) { return k >= 0 && k < SOMVR_ST_KINDS ? g_kind[k] : "?"; }

static const char *const g_why[SOMVR_STW_N] = { "ok", "off", "no orc", "in reach", "blocking", "the stick points away",
                                               "too short", "no fight", "the stick at rest", "bad input" };
const char *somvr_step_why_name(int w) { return w >= 0 && w < SOMVR_STW_N ? g_why[w] : "?"; }

static const char *const g_end[SOMVR_STE_N] = { "-", "done", "held (the stock evade ended after the motion)",
                                               "hold over (the stock evade outlasted it: the game's again)",
                                               "lost (the game owned its ticks)", "blocked (the body did not move)",
                                               "close (the stop distance)", "a new move", "off",
                                               "a finisher (SOMVR_FINISH started one: its sync owns the bodies)" };
const char *somvr_step_end_name(int e) { return e >= 0 && e < SOMVR_STE_N ? g_end[e] : "?"; }

void somvr_step_cfg_default(somvr_step_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->step = 0; /* off by default (SOMVR_STEP, the menu's row) */
    c->step_max_cm = 50, c->step_s = 0.18f;
    c->reach_cm = 120, c->step_extra_cm = 80, c->stop_cm = 70;
    c->cone_cos = cosf(35 * ST_DEG), c->away_cos = -0.3f, c->stick_cms = 30, c->min_cm = 5;
    c->dash = SOMVR_ST_DASH_OFF, c->dash_cm = 200, c->dash_s = 0.30f, c->dash_stick = 0.5f;
    c->phase = SOMVR_ST_PHASE_OFF, c->phase_range_cm = 300, c->phase_cone_cos = cosf(35 * ST_DEG);
    c->phase_behind_cm = 110, c->phase_min_cm = 150, c->phase_max_cm = 450, c->phase_s = 0.40f, c->phase_dir_stick = 0.5f;
    c->hold_s = 0.6f, c->lost_s = 0.1f, c->block_frac = 0.3f, c->block_cm = 15;
    c->blink_out_s = 0.05f, c->blink_in_s = 0.08f;
}

float somvr_step_pos(float D, float T, float t)
{
    if (!isfinite(D) || !isfinite(t))
        return 0;
    if (!(T > 0))
        return t >= 0 ? D : 0;
    float u = clampf(t / T, 0, 1);
    return D * (u - sinf(2 * ST_PI * u) / (2 * ST_PI));
}

float somvr_step_vel(float D, float T, float t)
{
    if (!(T > 0) || !isfinite(D) || !(t >= 0) || t > T)
        return 0;
    return D / T * (1 - cosf(2 * ST_PI * t / T));
}

int somvr_step_swing_pick(const somvr_step_cfg *c, const somvr_step_swing_in *in, somvr_step_pick *o)
{
    memset(o, 0, sizeof *o);
    o->target = -1;
    if (!(c->step > 0))
        return o->why = SOMVR_STW_OFF;
    if (!vok(in->root) || !isfinite(in->head_yaw))
        return o->why = SOMVR_STW_BAD;
    if (in->blocking)
        return o->why = SOMVR_STW_BLOCK;
    sv_vec3 gaze = { sinf(in->head_yaw), 0, cosf(in->head_yaw) };
    sv_vec3 tv = { in->tip_v.x, 0, in->tip_v.z };
    float tvl = vok(in->tip_v) && vok(in->tip) ? hlen(tv) : 0;
    int best = -1;
    float bs = -2, bd = 0, bcg = 0, bct = 0;
    for (int i = 0; i < in->n; i++) {
        sv_vec3 to = { in->orc[i].pos.x - in->root.x, 0, in->orc[i].pos.z - in->root.z };
        float d = hlen(to);
        if (!vok(in->orc[i].pos) || !(d >= 1) || d > c->reach_cm + c->step_extra_cm)
            continue;
        sv_vec3 u = { to.x / d, 0, to.z / d };
        float cg = hdot(u, gaze), ct = -1;
        if (tvl > 1) { /* the tip travels toward the orc (from the tip) */
            sv_vec3 tt = { in->orc[i].pos.x - in->tip.x, 0, in->orc[i].pos.z - in->tip.z };
            float tl = hlen(tt);
            if (tl > 1)
                ct = (tt.x * tv.x + tt.z * tv.z) / (tl * tvl);
        }
        float s = fmaxf(cg, ct);
        if (s < c->cone_cos)
            continue;
        if (best < 0 || s > bs + 1e-3f || (fabsf(s - bs) <= 1e-3f && d < bd))
            best = i, bs = s, bd = d, bcg = cg, bct = ct;
    }
    if (best < 0)
        return o->why = SOMVR_STW_NO_ORC;
    sv_vec3 to = { in->orc[best].pos.x - in->root.x, 0, in->orc[best].pos.z - in->root.z };
    o->target = best, o->dist_cm = bd, o->cos_gaze = bcg, o->cos_tip = bct;
    o->dir = (sv_vec3){ to.x / bd, 0, to.z / bd };
    if (bd <= c->reach_cm)
        return o->why = SOMVR_STW_IN_REACH;
    float sl = vok(in->stick_v) ? hlen(in->stick_v) : 0;
    if (sl >= c->stick_cms && hdot(in->stick_v, o->dir) / sl < c->away_cos)
        return o->why = SOMVR_STW_STICK_AWAY;
    float d = fminf(bd - c->reach_cm, c->step_max_cm * clampf(c->step, 0, 1));
    d = fminf(d, bd - c->stop_cm);
    if (!(d >= c->min_cm))
        return o->why = SOMVR_STW_SHORT;
    o->d_cm = d;
    return o->why = SOMVR_STW_OK;
}

int somvr_step_evade_pick(const somvr_step_cfg *c, const somvr_step_evade_in *in, somvr_step_evade *o)
{
    memset(o, 0, sizeof *o);
    o->target = -1;
    if (c->phase == SOMVR_ST_PHASE_OFF && c->dash == SOMVR_ST_DASH_OFF)
        return o->why = SOMVR_STW_OFF;
    if (!vok(in->root) || !isfinite(in->head_yaw))
        return o->why = SOMVR_STW_BAD;
    float sl = vok(in->stick_dir) ? hlen(in->stick_dir) : 0, mag = isfinite(in->stick_mag) ? in->stick_mag : 0;
    sv_vec3 sd = sl > 0.5f ? (sv_vec3){ in->stick_dir.x / sl, 0, in->stick_dir.z / sl } : (sv_vec3){ 0, 0, 0 };
    int stick = sl > 0.5f;
    sv_vec3 ask = stick && mag >= c->phase_dir_stick ? sd : (sv_vec3){ sinf(in->head_yaw), 0, cosf(in->head_yaw) };
    /* an orc in the direction asked: the phase (or, with the phase off, the stock A: no dash into an orc) */
    int best = -1;
    float bd = 0;
    for (int i = 0; i < in->n; i++) {
        sv_vec3 to = { in->orc[i].pos.x - in->root.x, 0, in->orc[i].pos.z - in->root.z };
        float d = hlen(to);
        if (!vok(in->orc[i].pos) || !(d >= 1) || d > c->phase_range_cm || hdot(to, ask) / d < c->phase_cone_cos)
            continue;
        if (best < 0 || d < bd)
            best = i, bd = d;
    }
    if (best >= 0) {
        if (c->phase == SOMVR_ST_PHASE_OFF)
            return o->target = best, o->dist_cm = bd, o->why = SOMVR_STW_OFF;
        if (!in->fight) /* out of a fight A is the stock jump / free-run: no phase into an unaware orc */
            return o->target = best, o->dist_cm = bd, o->why = SOMVR_STW_NO_FIGHT;
        sv_vec3 to = { in->orc[best].pos.x - in->root.x, 0, in->orc[best].pos.z - in->root.z };
        o->kind = SOMVR_ST_PHASE, o->target = best, o->dist_cm = bd;
        o->dir = (sv_vec3){ to.x / bd, 0, to.z / bd };
        o->D = clampf(bd + c->phase_behind_cm, c->phase_min_cm, c->phase_max_cm);
        o->T = c->phase_s;
    } else {
        if (c->dash == SOMVR_ST_DASH_OFF)
            return o->why = SOMVR_STW_OFF;
        if (!in->fight)
            return o->why = SOMVR_STW_NO_FIGHT;
        if (!stick || mag < c->dash_stick)
            return o->why = SOMVR_STW_NO_STICK;
        o->kind = SOMVR_ST_DASH, o->dir = sd, o->D = c->dash_cm, o->T = c->dash_s;
    }
    o->land = (sv_vec3){ in->root.x + o->dir.x * o->D, in->root.y, in->root.z + o->dir.z * o->D };
    return o->why = SOMVR_STW_OK;
}

void somvr_step_run_init(somvr_step_run *r) { memset(r, 0, sizeof *r); }

static void st_end(somvr_step_run *r, int e)
{
    if (r->kind == SOMVR_ST_NONE)
        return;
    r->ends[e >= 0 && e < SOMVR_STE_N ? e : 0]++;
    r->cm[r->kind] += r->done_cm;
    r->kind = SOMVR_ST_NONE;
}

void somvr_step_start(somvr_step_run *r, const somvr_step_cfg *c, int kind, int target, uintptr_t key, float D, float T,
                      sv_vec3 dir, sv_vec3 root, float orc_along)
{
    (void)c;
    if (kind <= SOMVR_ST_NONE || kind >= SOMVR_ST_KINDS || !(D > 0) || !isfinite(T) || !vok(dir) || !vok(root))
        return;
    st_end(r, SOMVR_STE_NEW);
    float l = hlen(dir);
    if (!(l > 0.5f))
        return;
    r->kind = kind, r->target = target, r->key = key, r->D = D, r->T = T > 0 ? T : 0, r->t = 0;
    r->dir = (sv_vec3){ dir.x / l, 0, dir.z / l }, r->start = root;
    r->orc_along = isfinite(orc_along) ? orc_along : 0;
    r->peaked = r->reface = r->attack_seen = 0;
    r->done_cm = r->asked_cm = r->body_cm = r->lost_t = r->last_d = 0;
    r->have_last = 0;
    r->ticks = r->owned_ticks = r->game_ticks = r->hold_ticks = 0;
    r->n[kind]++;
}

int somvr_step_stop(somvr_step_run *r, int e)
{
    int k = r->kind;
    st_end(r, e);
    return k;
}

static int st_evade(const somvr_step_run *r) { return r->kind == SOMVR_ST_DASH || r->kind == SOMVR_ST_PHASE; }

uint32_t somvr_step_mask(const somvr_step_run *r, const somvr_step_cfg *c, uint32_t own_bits, int attack)
{
    if (!st_evade(r))
        return 0;
    if (r->t < r->T || (attack && r->t < r->T + c->hold_s))
        return own_bits;
    return 0;
}

int somvr_step_yaw_ours(const somvr_step_run *r, const somvr_step_cfg *c, int attack)
{
    return r->kind == SOMVR_ST_PHASE && (r->t < r->T || (attack && r->t < r->T + c->hold_s));
}

void somvr_step_tick(somvr_step_run *r, const somvr_step_cfg *c, const somvr_step_tick_in *in, somvr_step_tick_out *o)
{
    memset(o, 0, sizeof *o);
    if (r->kind == SOMVR_ST_NONE)
        return;
    o->active = 1, o->kind = r->kind;
    float dt = isfinite(in->dt) ? clampf(in->dt, 0, 0.1f) : 0;
    o->dt = dt;
    /* the read-back of our last tick: what the body did along the path */
    if (r->have_last && in->have_root && vok(in->root)) {
        sv_vec3 m = { in->root.x - r->start.x, 0, in->root.z - r->start.z };
        float along = hdot(m, r->dir), prev = r->body_cm;
        r->asked_cm += r->last_d;
        r->body_cm = fmaxf(prev, along);
    }
    r->have_last = 0;
    if (st_evade(r) && r->asked_cm >= c->block_cm && r->body_cm < c->block_frac * r->asked_cm) {
        o->ended = SOMVR_STE_BLOCKED;
        st_end(r, o->ended);
        return;
    }
    float t0 = r->t;
    r->t += dt;
    r->ticks++;
    if (in->attack)
        r->attack_seen = 1;
    if (!in->owned) {
        r->game_ticks++;
        if (t0 < r->T) {
            r->lost_t += dt;
            if (r->lost_t >= c->lost_s || r->kind == SOMVR_ST_STEP) { /* the step is too short to resume */
                o->ended = SOMVR_STE_LOST;
                st_end(r, o->ended);
                return;
            }
        }
    } else {
        r->owned_ticks++;
        r->lost_t = 0;
        if (t0 < r->T) {
            float ds = somvr_step_pos(r->D, r->T, r->t) - somvr_step_pos(r->D, r->T, t0);
            float sa = vok(in->stick_d) ? hdot(in->stick_d, r->dir) : 0; /* the stick's own part along the path */
            sv_vec3 d = { r->dir.x * ds, 0, r->dir.z * ds };
            if (st_evade(r) && sa > 0) /* the move sets the speed along its path; the stick keeps the rest (steering) */
                d.x -= r->dir.x * sa, d.z -= r->dir.z * sa;
            if (r->kind == SOMVR_ST_STEP && in->have_orc && vok(in->orc) && in->have_root && vok(in->root)) {
                sv_vec3 to = { in->orc.x - in->root.x, 0, in->orc.z - in->root.z };
                float allowed = hdot(to, r->dir) - c->stop_cm - fmaxf(sa, 0);
                if (ds > allowed) {
                    ds = fmaxf(allowed, 0);
                    d = (sv_vec3){ r->dir.x * ds, 0, r->dir.z * ds };
                    o->clamped = 1;
                }
            }
            o->d = d;
            r->done_cm += ds;
            r->last_d = ds, r->have_last = 1;
            if (r->kind == SOMVR_ST_PHASE && !r->peaked && r->done_cm >= r->orc_along) {
                r->peaked = 1, o->peak = 1;
                if (c->phase == SOMVR_ST_PHASE_REFACE)
                    r->reface = 1;
            }
            if (o->clamped && !(ds > 0)) {
                o->ended = SOMVR_STE_CLOSE;
                st_end(r, o->ended);
                return;
            }
        } else {
            r->hold_ticks++;
        }
    }
    if (r->t >= r->T) {
        if (!st_evade(r))
            o->ended = SOMVR_STE_DONE;
        else if (!in->attack)
            o->ended = r->hold_ticks ? SOMVR_STE_HELD : SOMVR_STE_DONE;
        else if (r->t >= r->T + c->hold_s)
            o->ended = SOMVR_STE_HOLD_OVER;
        if (o->ended)
            st_end(r, o->ended);
    }
}

float somvr_step_blink(const somvr_step_run *r, const somvr_step_cfg *c)
{
    int on = (r->kind == SOMVR_ST_DASH && c->dash == SOMVR_ST_DASH_BLINK) ||
             (r->kind == SOMVR_ST_PHASE && c->phase == SOMVR_ST_PHASE_BLINK);
    if (!on)
        return 0;
    float t = r->t;
    if (t < r->T)
        return c->blink_out_s > 0 ? clampf(t / c->blink_out_s, 0, 1) : 1;
    if (c->blink_in_s > 0 && t < r->T + c->blink_in_s)
        return 1 - (t - r->T) / c->blink_in_s;
    return 0;
}

int somvr_step_look(const somvr_step_run *r) { return r->kind == SOMVR_ST_PHASE && r->t < r->T; }

uint32_t somvr_step_own_bits(const char *s, int *bad)
{
    uint32_t bits = 0;
    while (s && *s) {
        while (*s == ',' || *s == '+' || *s == ' ')
            s++;
        const char *e = s;
        while (*e && *e != ',' && *e != '+' && *e != ' ')
            e++;
        if (e == s)
            break;
        int found = 0;
        for (int b = 0; b < SOMVR_LOCO_W_N; b++) {
            const char *nm = somvr_loco_why_name(b);
            if (strlen(nm) == (size_t)(e - s) && !strncmp(nm, s, (size_t)(e - s)))
                bits |= 1u << b, found = 1;
        }
        if (!found && bad)
            (*bad)++;
        s = e;
    }
    return bits;
}
