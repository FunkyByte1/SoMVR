/* somvr_loco.c - see somvr_loco.h */
#include "somvr_loco.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LO_DEG 0.01745329252f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float hlen(sv_vec3 v) { return sqrtf(v.x * v.x + v.z * v.z); }
static float okf(float v) { return isfinite(v) ? v : 0.0f; }

void somvr_loco_cfg_default(somvr_loco_cfg *c)
{
    memset(c, 0, sizeof *c);
    /* P10 (LR1, 2026-10-20 15:46): the stock Run plateau is 222-232 cm/s at every stick from 0.2 to 1.0 and the stock
     * FreeRun sprint 636-641 cm/s, so run = 230 and the latch's band = 640: the legs' clip speeds, and a hand-off while
     * the stick is held continues at our speed (the game's Run went on at 240-262 cm/s the tick after loco ended). No
     * stock walk band was seen (KBM mode, the InCombat tag): the walk stays a placeholder (open, P11 in pad mode) */
    c->walk_cms = 150, c->run_cms = 230, c->sprint_cms = 640;
    c->speed = 1.0f;
    c->stick_s = 0;
    c->aim_cms = 150;
    c->yaw_rate = 720 * LO_DEG, c->yaw_cone = 100 * LO_DEG, c->yaw_stand = 50 * LO_DEG, c->yaw_done = 3 * LO_DEG;
    c->move_cms = 5;
    c->max_dt = 0.1f, c->max_step_cm = 60;
    c->yaw_mode = SOMVR_LOCO_YAW_OURS, c->latch = SOMVR_LOCO_LATCH_OURS;
    c->guard_ticks = 10, c->guard_s = 1.0f;
}

float somvr_loco_band_speed(const somvr_loco_cfg *c, float m, int sprint)
{
    m = isfinite(m) ? clampf(m, 0, 1) : 0;
    float top = sprint && c->latch == SOMVR_LOCO_LATCH_OURS ? c->sprint_cms : c->run_cms;
    float v = m <= 0.5f ? 2 * m * c->walk_cms : c->walk_cms + (2 * m - 1) * (top - c->walk_cms);
    return v * c->speed;
}

sv_vec3 somvr_loco_stick_velocity(const somvr_loco_cfg *c, const float stick[2], float h, int sprint)
{
    float x = okf(stick[0]), y = okf(stick[1]), m = sqrtf(x * x + y * y);
    if (!(m > 1e-6f) || !isfinite(h))
        return (sv_vec3){ 0, 0, 0 };
    float s = somvr_loco_band_speed(c, m > 1 ? 1 : m, sprint) / m;
    sv_vec3 w = sv_qrot(sv_qyaw(h), (sv_vec3){ x, 0, y });
    return (sv_vec3){ w.x * s, 0, w.z * s };
}

float somvr_loco_leg_mag(const somvr_loco_cfg *c, float v, int sprint)
{
    float top = sprint && c->latch == SOMVR_LOCO_LATCH_OURS ? c->sprint_cms : c->run_cms;
    if (!(v > 0) || !(c->walk_cms > 0))
        return 0;
    if (v <= c->walk_cms)
        return 0.5f * v / c->walk_cms;
    if (!(top > c->walk_cms))
        return 1;
    return clampf(0.5f + 0.5f * (v - c->walk_cms) / (top - c->walk_cms), 0, 1);
}

static const char *const g_why_names[SOMVR_LOCO_W_N] = {
    "off", "input", "stereo", "player", "read", "fsm", "momentum", "lock", "attack", "combatmove",
    "mount", "attach", "dead", "climb", "struggle", "sync", "menu", "fallback", "freerun"
};

const char *somvr_loco_why_name(int bit) { return bit >= 0 && bit < SOMVR_LOCO_W_N ? g_why_names[bit] : "?"; }

const char *somvr_loco_why_str(uint32_t why, char *out, int cap)
{
    if (cap <= 0)
        return out;
    out[0] = 0;
    if (!why) {
        snprintf(out, (size_t)cap, "ours");
        return out;
    }
    int n = 0;
    for (int b = 0; b < SOMVR_LOCO_W_N && n < cap; b++)
        if (why >> b & 1)
            n += snprintf(out + n, (size_t)(cap - n), "%s%s", n ? "+" : "", g_why_names[b]);
    return out;
}

uint32_t somvr_loco_why(const somvr_loco_own_in *in, const somvr_loco_cfg *c)
{
    if (!in->on)
        return SOMVR_LOCO_W_OFF;
    uint32_t w = 0;
    if (!in->live)
        w |= SOMVR_LOCO_W_INPUT;
    if (!in->stereo)
        w |= SOMVR_LOCO_W_STEREO;
    if (!in->player)
        w |= SOMVR_LOCO_W_PLAYER;
    if (!in->read_ok)
        w |= SOMVR_LOCO_W_READ;
    if (in->menu)
        w |= SOMVR_LOCO_W_MENU;
    if (in->player && in->read_ok) {
        if (in->fsm != 6 && in->fsm != 7)
            w |= SOMVR_LOCO_W_FSM;
        if (!in->rootmotion)
            w |= SOMVR_LOCO_W_MOMENTUM;
        if (in->lock_t)
            w |= SOMVR_LOCO_W_LOCK;
        if (in->attack)
            w |= SOMVR_LOCO_W_ATTACK;
        if (in->combat_move)
            w |= SOMVR_LOCO_W_COMBATMOVE;
        if (in->mounted)
            w |= SOMVR_LOCO_W_MOUNT;
        if (in->attached)
            w |= SOMVR_LOCO_W_ATTACH;
        if (in->life != 2)
            w |= SOMVR_LOCO_W_DEAD;
    }
    if (in->climbing)
        w |= SOMVR_LOCO_W_CLIMB;
    if (in->struggle)
        w |= SOMVR_LOCO_W_STRUGGLE;
    if (in->sync)
        w |= SOMVR_LOCO_W_SYNC;
    if (in->fallback)
        w |= SOMVR_LOCO_W_FALLBACK;
    if (in->sprint && c->latch == SOMVR_LOCO_LATCH_GAME)
        w |= SOMVR_LOCO_W_FREERUN;
    return w;
}

int somvr_loco_yaw_game(const somvr_loco_own_in *in, const somvr_loco_cfg *c)
{
    return c->yaw_mode == SOMVR_LOCO_YAW_GAME || in->fsm == 7 || in->aiming || in->lock_r;
}

int somvr_loco_follow_route(int owned, int asked)
{
    return !asked ? SOMVR_LOCO_FOLLOW_NONE : owned ? SOMVR_LOCO_FOLLOW_DELTA : SOMVR_LOCO_FOLLOW_EXTRA;
}

void somvr_loco_init(somvr_loco *s) { memset(s, 0, sizeof *s); }

float somvr_loco_yaw(const somvr_loco_cfg *c, float yaw, float head_yaw, sv_vec3 v, float dt, int *turning,
                     float *target)
{
    float tgt;
    if (hlen(v) > c->move_cms) { /* moving: face the move within the cone about the head, else the head */
        float dir = atan2f(v.x, v.z);
        tgt = fabsf(sv_wrap_pi(dir - head_yaw)) <= c->yaw_cone ? dir : head_yaw;
        *turning = 0;
    } else { /* standing: lazily toward the head (beyond yaw_stand, until within yaw_done) */
        float d = sv_wrap_pi(head_yaw - yaw);
        if (!*turning && fabsf(d) > c->yaw_stand)
            *turning = 1;
        if (*turning && fabsf(d) <= c->yaw_done)
            *turning = 0;
        tgt = *turning ? head_yaw : yaw;
    }
    if (target)
        *target = tgt;
    float step = sv_wrap_pi(tgt - yaw), lim = c->yaw_rate * dt;
    if (!isfinite(step))
        return 0;
    return clampf(step, -lim, lim);
}

void somvr_loco_step(somvr_loco *s, const somvr_loco_cfg *c, const somvr_loco_in *in, somvr_loco_out *o)
{
    memset(o, 0, sizeof *o);
    int owned = in->why == 0;
    o->why = in->why, o->why_prev = s->why_last, o->owned = owned;
    float dt = isfinite(in->dt) ? clampf(in->dt, 0, c->max_dt) : 0;
    /* the read-back of the last tick: what the body did (the collision takes the part into a wall) */
    if (in->have_root && s->have_root) {
        sv_vec3 a = { in->root.x - s->root_last.x, 0, in->root.z - s->root_last.z };
        o->have_applied = 1, o->applied = a, o->asked = s->asked_last;
        if (s->dt_last > 0 && isfinite(a.x) && isfinite(a.z)) {
            s->v_hand = (sv_vec3){ a.x / s->dt_last, 0, a.z / s->dt_last };
            s->have_v_hand = 1;
        }
        if (s->owned_last)
            s->asked_cm += hlen(s->asked_last), s->applied_cm += hlen(a);
    }
    o->v_hand = s->v_hand;
    o->handoff = owned && !s->owned_last ? 1 : !owned && s->owned_last ? -1 : 0;
    s->handoffs_in += o->handoff > 0, s->handoffs_out += o->handoff < 0;
    s->asked_last = (sv_vec3){ 0, 0, 0 };
    if (owned) {
        sv_vec3 t = in->stick_ok ? (sv_vec3){ okf(in->stick_v.x), 0, okf(in->stick_v.z) } : (sv_vec3){ 0, 0, 0 };
        float m = hlen(t);
        if (in->cap_cms > 0 && m > in->cap_cms)
            t.x *= in->cap_cms / m, t.z *= in->cap_cms / m;
        if (c->stick_s > 0) { /* the softening option: from the body's own speed at a hand-off */
            if (o->handoff > 0)
                s->v = s->have_v_hand ? s->v_hand : (sv_vec3){ 0, 0, 0 };
            float k = 1 - expf(-dt / c->stick_s);
            s->v.x += (t.x - s->v.x) * k, s->v.z += (t.z - s->v.z) * k, s->v.y = 0;
        } else {
            s->v = t; /* 1:1: the stick's velocity in this very tick, a stop in this tick */
        }
        o->v = s->v;
        sv_vec3 d = { s->v.x * dt, 0, s->v.z * dt };
        float l = hlen(d);
        if (l > c->max_step_cm)
            d.x *= c->max_step_cm / l, d.z *= c->max_step_cm / l;
        o->delta = d, s->asked_last = d;
        /* the guard: our last step undone between the ticks (the game holds his yaw: a rail, a perch) */
        if (c->guard_ticks > 0 && in->have_btw && fabsf(s->last_step) > 0.2f * LO_DEG && isfinite(in->btw_yaw) &&
            hlen(s->v) <= c->move_cms) /* standing (the standing turn): walking, the stock turn may answer ours */
            s->fight_n = fabsf(in->btw_yaw + s->last_step) < 0.5f * fabsf(s->last_step) ? s->fight_n + 1 : 0;
        else if (!in->have_btw || s->last_step == 0)
            s->fight_n = 0;
        if (c->guard_ticks > 0 && s->fight_n >= c->guard_ticks) {
            s->guard_left = c->guard_s, s->fight_n = 0, s->guards++;
            o->guard_start = 1;
        }
        s->last_step = 0;
        if (s->guard_left > 0) {
            s->guard_left -= dt, s->guard_ticks++;
            o->yaw_guard = 1;
            s->turning = 0;
        } else if (!in->yaw_game) {
            o->yaw_ours = 1;
            o->yaw_step = somvr_loco_yaw(c, okf(in->yaw), okf(in->head_yaw), s->v, dt, &s->turning, &o->yaw_target);
            s->last_step = o->yaw_step;
        } else {
            s->turning = 0;
        }
        s->owned_ticks++;
    } else { /* the game's: our velocity is kept only as the hand-off speed */
        s->v = s->have_v_hand ? s->v_hand : (sv_vec3){ 0, 0, 0 };
        s->turning = 0;
        s->last_step = 0, s->fight_n = 0;
    }
    s->owned_last = owned, s->why_last = in->why;
    s->root_last = in->root, s->have_root = in->have_root, s->dt_last = dt;
    s->ticks++;
}

int somvr_loco_take_follow(somvr_loco *s, somvr_loco_out *o, sv_vec3 d)
{
    if (!o->owned || !isfinite(d.x) || !isfinite(d.z)) {
        s->follow_extra++;
        return 0;
    }
    d.y = 0;
    o->delta.x += d.x, o->delta.z += d.z;
    o->follow_taken = 1, o->follow_d = d;
    s->asked_last.x += d.x, s->asked_last.z += d.z;
    s->follow_merged++, s->follow_cm += hlen(d);
    return 1;
}

int somvr_loco_take_assist(somvr_loco *s, somvr_loco_out *o, sv_vec3 d, float dt)
{
    if (!o->owned || !isfinite(d.x) || !isfinite(d.z))
        return 0;
    o->delta.x += d.x, o->delta.z += d.z;
    if (dt > 0)
        o->v.x += d.x / dt, o->v.z += d.z / dt;
    s->asked_last.x += d.x, s->asked_last.z += d.z;
    return 1;
}

void somvr_loco_unapplied(somvr_loco *s)
{
    s->owned_last = 0;
    s->asked_last = (sv_vec3){ 0, 0, 0 };
}

/* ---- the anchor ---------------------------------------------------------------------------------------------------- */
void somvr_loco_spring3(float *x, float *v, float *a, float w, float dt)
{
    if (!(w > 0) || !(dt > 0))
        return;
    float A = *x, B = *v + w * A, C = 0.5f * (*a + 2 * w * *v + w * w * A), e = expf(-w * dt);
    float p = A + (B + C * dt) * dt, dp = B + 2 * C * dt, ddp = 2 * C;
    float nx = p * e, nv = (dp - w * p) * e, na = (ddp - 2 * w * dp + w * w * p) * e;
    if (!isfinite(nx) || !isfinite(nv) || !isfinite(na))
        nx = nv = na = 0;
    *x = nx, *v = nv, *a = na;
}

void somvr_anchor_cfg_default(somvr_anchor_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->eye_cm = 76.6f; /* run 3: the eyes' midpoint 180.6 cm above World_Ground, the root 104 cm above it */
    c->y_s = 0.15f, c->blend_s = 0.04f, c->dip_cm = 30; /* y 150 ms (build 6 comfort: "the terrain is too unstable"; 80 ms
                                                         * before: half the peak vertical acceleration on crests and steps,
                                                         * a ramp lag y_s x v = 7 cm on a 20 % slope at a run) */
    c->jump_cm = 100, c->gap_s = 0.25f, c->max_dt = 0.1f;
    c->lead_s = 0, c->jit_max_s = 0.025f, c->age_tau_s = 1.0f;
    c->x_max_cm = 10, c->x_acc = 6000, c->e_max_cm = 50;
}

void somvr_anchor_init(somvr_anchor *a) { memset(a, 0, sizeof *a); }

static float med3(float a, float b, float c)
{
    return a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b);
}

/* the frame's timing error: its age (display - now) against the running mean; 0 without a display time */
static float anchor_jitter(somvr_anchor *a, const somvr_anchor_cfg *c, const somvr_anchor_in *in, float dt)
{
    if (in->display_ns <= 0)
        return 0;
    int64_t age = in->display_ns - in->now_ns;
    double d = (double)(age - a->age0) * 1e-9;
    if (!a->have_age || fabs(d - a->age_mean) > 0.25) { /* the first, or the clocks jumped (a new session) */
        a->age0 = age, a->age_mean = 0, a->have_age = 1;
        return 0;
    }
    float j = clampf((float)(d - a->age_mean), -c->jit_max_s, c->jit_max_s);
    float k = c->age_tau_s > 0 && dt > 0 ? 1 - expf(-dt / c->age_tau_s) : 1;
    a->age_mean += (d - a->age_mean) * k;
    return j;
}

void somvr_anchor_step(somvr_anchor *a, const somvr_anchor_cfg *c, const somvr_anchor_in *in, somvr_anchor_out *o)
{
    memset(o, 0, sizeof *o);
    sv_vec3 r = in->root;
    float target = r.y + c->eye_cm;
    double dtd = in->t - a->t_last;
    float jump = a->have ? sqrtf((r.x - a->root_last.x) * (r.x - a->root_last.x) +
                                 (r.y - a->root_last.y) * (r.y - a->root_last.y) +
                                 (r.z - a->root_last.z) * (r.z - a->root_last.z)) : 0;
    float dt = (float)(dtd > 0 ? dtd : 0);
    o->jitter_s = anchor_jitter(a, c, in, dt);
    if (!a->have || dtd > c->gap_s || dtd < 0 || !(jump <= c->jump_cm)) { /* re-latch: nothing to smooth across */
        o->latched = a->have && dtd <= c->gap_s && dtd >= 0 ? 2 : 1;
        a->teleports += o->latched == 2, a->latches++;
        a->y = target, a->vy = a->ay = 0, a->ey = a->eyv = a->eya = 0, a->vout_y = a->aout_y = 0;
        a->e = a->ev = a->ea = a->v_root = a->vx = (sv_vec3){ 0, 0, 0 };
        a->v_root_prev[0] = a->v_root_prev[1] = (sv_vec3){ 0, 0, 0 };
        a->exact_y = in->exact_y, a->have_vr = 0, a->have = 1;
        o->pos = (sv_vec3){ r.x, target, r.z };
        o->exact_y = a->exact_y;
        a->out_last = o->pos, a->root_last = r, a->t_last = in->t;
        a->frames++;
        return;
    }
    dt = dt > c->max_dt ? c->max_dt : dt;
    sv_vec3 vn = dt > 0 ? (sv_vec3){ (r.x - a->root_last.x) / dt, (r.y - a->root_last.y) / dt, (r.z - a->root_last.z) / dt }
                        : a->v_root;
    float wb = c->blend_s > 0 ? 1 / c->blend_s : 0;
    /* horizontal: exact root + a hand-off offset that absorbs the body's velocity jump and eases back (3rd order: the
     * view's velocity and acceleration continuous) */
    if (in->handoff && a->have_vr) {
        a->ev.x += a->v_root.x - vn.x, a->ev.z += a->v_root.z - vn.z;
        a->blends++, o->blended = 1;
    }
    if (wb > 0) {
        somvr_loco_spring3(&a->e.x, &a->ev.x, &a->ea.x, wb, dt);
        somvr_loco_spring3(&a->e.z, &a->ev.z, &a->ea.z, wb, dt);
    } else {
        a->e = a->ev = a->ea = (sv_vec3){ 0, 0, 0 };
    }
    float el = hlen(a->e);
    if (el > c->e_max_cm)
        a->e.x *= c->e_max_cm / el, a->e.z *= c->e_max_cm / el;
    a->e_cm_max = fmaxf(a->e_cm_max, hlen(a->e));
    /* vertical: low-passed on the ground (steps, slopes), exact in falls and our climb. A switch starts the new mode
     * from the last output's height, velocity and acceleration, so the view's motion is continuous across it (a landing
     * from the fall's speed dips below the eye height and comes back, capped) */
    float tprev = a->root_last.y + c->eye_cm;
    if (in->exact_y != a->exact_y) {
        if (in->exact_y)
            a->ey = a->out_last.y - tprev, a->eyv = a->vout_y - vn.y, a->eya = 0;
        else
            a->y = a->out_last.y, a->vy = a->vout_y, a->ay = a->aout_y;
        a->exact_y = in->exact_y;
        a->y_switches++;
    }
    float yo;
    if (a->exact_y) {
        somvr_loco_spring3(&a->ey, &a->eyv, &a->eya, wb, dt);
        a->ey = clampf(a->ey, -c->e_max_cm, c->e_max_cm);
        yo = target + a->ey;
    } else {
        float x = a->y - target, wy = c->y_s > 0 ? 3 / c->y_s : 0;
        if (wy > 0)
            somvr_loco_spring3(&x, &a->vy, &a->ay, wy, dt);
        else
            x = 0, a->vy = vn.y, a->ay = 0;
        if (x < -c->dip_cm) /* a landing's dip, capped */
            x = -c->dip_cm, a->vy = a->vy < 0 ? 0 : a->vy, a->ay = a->ay < 0 ? 0 : a->ay;
        if (x > c->e_max_cm)
            x = c->e_max_cm;
        a->y = target + x;
        yo = a->y;
    }
    float vo = dt > 0 ? (yo - a->out_last.y) / dt : a->vout_y;
    a->aout_y = dt > 0 ? (vo - a->vout_y) / dt : a->aout_y;
    a->vout_y = vo;
    o->exact_y = a->exact_y;
    o->y_lag = yo - target;
    o->pos = (sv_vec3){ r.x + a->e.x, yo, r.z + a->e.z };
    /* the display time: the body's velocity x the frame's timing error (+ the lead); the velocity's changes limited */
    sv_vec3 vs = in->own ? (sv_vec3){ okf(in->v_own.x), 0, okf(in->v_own.z) }
                         : (sv_vec3){ med3(a->v_root_prev[1].x, a->v_root_prev[0].x, vn.x), 0,
                                      med3(a->v_root_prev[1].z, a->v_root_prev[0].z, vn.z) };
    sv_vec3 dv = { vs.x - a->vx.x, 0, vs.z - a->vx.z };
    float dl = hlen(dv), lim = c->x_acc * dt;
    if (dl > lim && dl > 0)
        dv.x *= lim / dl, dv.z *= lim / dl;
    a->vx.x += dv.x, a->vx.z += dv.z, a->vx.y = 0;
    float lead = o->jitter_s + c->lead_s;
    sv_vec3 x = { a->vx.x * lead, 0, a->vx.z * lead };
    float xl = hlen(x);
    if (xl > c->x_max_cm)
        x.x *= c->x_max_cm / xl, x.z *= c->x_max_cm / xl;
    o->x = x;
    o->pos.x += x.x, o->pos.z += x.z;
    a->x_frames += xl > 0.01f;
    a->x_cm_max = fmaxf(a->x_cm_max, hlen(x));
    a->jit_min = fminf(a->jit_min, o->jitter_s), a->jit_max = fmaxf(a->jit_max, o->jitter_s);
    if (dt > 0)
        o->vel = (sv_vec3){ (o->pos.x - a->out_last.x) / dt, (o->pos.y - a->out_last.y) / dt,
                            (o->pos.z - a->out_last.z) / dt };
    a->v_root_prev[1] = a->v_root_prev[0], a->v_root_prev[0] = vn;
    a->v_root = vn, a->have_vr = 1;
    a->out_last = o->pos, a->root_last = r, a->t_last = in->t;
    a->frames++;
}
