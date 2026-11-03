/* somvr_ovault.c - the vault over an orc by the hands (pure; somvr_ovault.h) */
#include "somvr_ovault.h"

#include <math.h>
#include <string.h>

void somvr_ov_cfg_default(somvr_ov_cfg *c)
{
    c->top_cm = 30, c->below_cm = 5, c->grip_on = 0.7f, c->grip_off = 0.35f;
    c->hold_max_s = 5, c->break_cm = 40, c->rise_max_cm = 90, c->win_s = 0.1f;
    c->over_gain = 1.6f, c->min_up = 100, c->min_over = 120, c->max_cms = 550, c->side = 0.5f, c->g = 981;
    c->past_cm = 50, c->flight_max_s = 1.5f;
}

const char *somvr_ov_why_name(int w)
{
    static const char *const n[SOMVR_OVW_N] = { "-", "the hand opened slowly (no throw)", "the throw would land short of past him",
                                                "held too long", "he moved away from the hold", "a hit on Talion",
                                                "not live", "the ground", "the flight's time" };
    return w >= 0 && w < SOMVR_OVW_N ? n[w] : "?";
}

static float len3(sv_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static sv_vec3 sub3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static sv_vec3 mul3(sv_vec3 a, float k) { return (sv_vec3){ a.x * k, a.y * k, a.z * k }; }
static int fin(sv_vec3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }

int somvr_ov_on_crown(const somvr_ov_cfg *c, sv_vec3 pw, sv_vec3 hd)
{
    return fin(pw) && fin(hd) && len3(sub3(pw, hd)) <= c->top_cm && pw.y >= hd.y - c->below_cm;
}

float somvr_ov_land_cm(float up, float over, float h, float g)
{
    if (!(g > 0))
        return 0;
    float hh = h > 0 ? h : 0, T = (up + sqrtf(up * up + 2 * g * hh)) / g;
    return over * T;
}

void somvr_ov_reset(somvr_ov *s)
{
    memset(s, 0, sizeof *s);
}

static void hist_add(somvr_ov *s, int h, double t, sv_vec3 p)
{
    s->h[h][s->hpos[h]].t = t, s->h[h][s->hpos[h]].p = p;
    s->hpos[h] = (s->hpos[h] + 1) % SOMVR_OV_HIST, s->nh[h] += s->nh[h] < SOMVR_OV_HIST;
}

/* the hand's mean velocity (tracking space, cm/s) over the window: 1 = found */
static int hist_vel(const somvr_ov *s, const somvr_ov_cfg *c, int h, sv_vec3 *v)
{
    if (s->nh[h] < 2)
        return 0;
    int now = (s->hpos[h] - 1 + SOMVR_OV_HIST) % SOMVR_OV_HIST, k = -1;
    for (int j = 1; j < s->nh[h]; j++) {
        int ix = (now - j + SOMVR_OV_HIST) % SOMVR_OV_HIST;
        double age = s->h[h][now].t - s->h[h][ix].t;
        if (age > 1.25 * c->win_s)
            break;
        k = ix;
    }
    if (k < 0)
        return 0;
    double age = s->h[h][now].t - s->h[h][k].t;
    if (age < 0.4 * c->win_s)
        return 0;
    *v = mul3(sub3(s->h[h][now].p, s->h[h][k].p), (float)(1.0 / age));
    return 1;
}

static void to_idle(somvr_ov *s)
{
    s->phase = SOMVR_OV_IDLE, s->holds[0] = s->holds[1] = 0, s->have_prev = s->stuck = s->blocked = 0, s->rise = 0;
}

/* the hold's end: the throw by the rule, or a let-go */
static void release(somvr_ov *s, const somvr_ov_cfg *c, const somvr_ov_in *in, int hand, somvr_ov_out *o)
{
    sv_vec3 vh;
    if (!hist_vel(s, c, hand, &vh)) {
        o->events |= SOMVR_OVE_LETGO, o->why = SOMVR_OVW_OPEN, s->letgos++;
        to_idle(s);
        return;
    }
    sv_vec3 v = mul3(sv_qrot(sv_qyaw(in->rig_yaw), vh), -(in->rig_scale > 0 ? in->rig_scale : 1));
    float up = v.y, over = v.x * s->over.x + v.z * s->over.z;
    sv_vec3 side = { v.x - over * s->over.x, 0, v.z - over * s->over.z };
    o->throw_up = up, o->throw_over = over, o->throw_side = len3(side);
    float up_k = up > 0 ? up : 0, over_k = over > 0 ? over * c->over_gain : 0;
    side = mul3(side, c->side);
    float sl = len3(side);
    if (sl > 0.5f * over_k)
        side = mul3(side, sl > 0 ? 0.5f * over_k / sl : 0);
    if (up_k < c->min_up || over_k < c->min_over) {
        o->events |= SOMVR_OVE_LETGO, o->why = SOMVR_OVW_OPEN, s->letgos++;
        to_idle(s);
        return;
    }
    sv_vec3 v0 = { s->over.x * over_k + side.x, up_k, s->over.z * over_k + side.z };
    float l0 = len3(v0);
    if (l0 > c->max_cms)
        v0 = mul3(v0, c->max_cms / l0);
    sv_vec3 r = in->root_ok ? in->root : s->root0;
    float to_orc = (in->orc_root.x - r.x) * s->over.x + (in->orc_root.z - r.z) * s->over.z;
    o->land_cm = somvr_ov_land_cm(v0.y, v0.x * s->over.x + v0.z * s->over.z, s->rise, c->g);
    o->need_cm = (in->orc_ok ? to_orc : 0) + c->past_cm;
    if (o->land_cm < o->need_cm) {
        o->events |= SOMVR_OVE_LETGO, o->why = SOMVR_OVW_SHORT, s->letgos++;
        to_idle(s);
        return;
    }
    o->events |= SOMVR_OVE_THROW, o->v0 = v0, o->grav_off = 1, s->throws++;
    s->phase = SOMVR_OV_FLIGHT, s->v = v0, s->t0 = in->t, s->t_last = in->t, s->have_prev = 0, s->stuck = 0;
    s->asked = (sv_vec3){ 0, 0, 0 };
}

void somvr_ov_step(somvr_ov *s, const somvr_ov_cfg *c, const somvr_ov_in *in, somvr_ov_out *o)
{
    memset(o, 0, sizeof *o);
    o->hand = -1;
    double tp = in->pose_t > 0 ? in->pose_t : in->t;
    int edge[2] = { 0, 0 };
    for (int h = 0; h < 2; h++) {
        float sq = isfinite(in->hand[h].squeeze) ? in->hand[h].squeeze : 0;
        int was = s->closed[h];
        s->closed[h] = in->hand[h].valid && (was ? sq > c->grip_off : sq >= c->grip_on);
        edge[h] = s->closed[h] && !was;
    }
    if (s->phase == SOMVR_OV_IDLE) {
        for (int h = 0; h < 2; h++)
            s->last_p[h] = in->hand[h].p;
        if (!in->live || !in->orc_ok || !in->root_ok)
            return;
        for (int h = 0; h < 2; h++) {
            if (!edge[h] || !in->free[h] || !in->hand[h].valid || !somvr_ov_on_crown(c, in->hand[h].pw, in->head))
                continue;
            s->phase = SOMVR_OV_HOLD, s->holds[h] = 1, s->t0 = in->t, s->head0 = in->head, s->root0 = in->root;
            s->orc0 = in->orc_root;
            s->ground_y = in->root.y, s->rise = 0, s->nh[0] = s->nh[1] = 0;
            float dx = in->orc_root.x - in->root.x, dz = in->orc_root.z - in->root.z, l = sqrtf(dx * dx + dz * dz);
            s->over = l > 1 ? (sv_vec3){ dx / l, 0, dz / l } : (sv_vec3){ 0, 0, 1 };
            hist_add(s, h, tp, in->hand[h].p);
            o->events |= SOMVR_OVE_GRAB, o->hand = h, o->grav_off = 1, s->grabs++;
            return;
        }
        return;
    }
    if (s->phase == SOMVR_OV_HOLD) {
        int why = !in->live ? SOMVR_OVW_LIVE : in->hurt ? SOMVR_OVW_HIT : in->t - s->t0 > c->hold_max_s ? SOMVR_OVW_MAX
                : !in->orc_ok || hypotf(in->orc_root.x - s->orc0.x, in->orc_root.z - s->orc0.z) > c->break_cm ? SOMVR_OVW_MOVED
                                                                                                         : 0;
        if (why) {
            o->events |= SOMVR_OVE_LETGO, o->why = why, s->letgos++;
            to_idle(s);
            return;
        }
        for (int h = 0; h < 2; h++) /* the other hand joins on his crown */
            if (!s->holds[h] && edge[h] && in->free[h] && in->hand[h].valid && somvr_ov_on_crown(c, in->hand[h].pw, in->head)) {
                s->holds[h] = 1, s->last_p[h] = in->hand[h].p, s->nh[h] = 0;
                o->events |= SOMVR_OVE_JOIN, o->hand = h;
            }
        int last = -1;
        for (int h = 0; h < 2; h++)
            if (s->holds[h] && (!s->closed[h] || !in->hand[h].valid))
                s->holds[h] = 0, last = h;
        if (!s->holds[0] && !s->holds[1]) {
            release(s, c, in, last >= 0 ? last : 0, o);
            return;
        }
        sv_vec3 d = { 0, 0, 0 };
        int n = 0;
        for (int h = 0; h < 2; h++) {
            if (!s->holds[h])
                continue;
            sv_vec3 dd = sub3(in->hand[h].p, s->last_p[h]);
            if (fin(dd) && len3(dd) < 50) /* (a tracking jump is no move) */
                d.x += dd.x, d.y += dd.y, d.z += dd.z, n++;
            s->last_p[h] = in->hand[h].p;
            hist_add(s, h, tp, in->hand[h].p);
        }
        if (n) {
            sv_vec3 m = mul3(sv_qrot(sv_qyaw(in->rig_yaw), mul3(d, 1.0f / n)), -(in->rig_scale > 0 ? in->rig_scale : 1));
            float r1 = s->rise + m.y;
            if (r1 > c->rise_max_cm)
                m.y -= r1 - c->rise_max_cm, r1 = c->rise_max_cm;
            if (r1 < 0)
                m.y -= r1, r1 = 0;
            s->rise = r1;
            o->move = m;
        }
        o->grav_off = 1;
        return;
    }
    /* the flight */
    double dt = in->t - s->t_last;
    if (!(dt > 0))
        dt = 0;
    if (dt > 0.1)
        dt = 0.1;
    s->t_last = in->t;
    if (in->root_ok && s->have_prev) {
        sv_vec3 got = sub3(in->root, s->root_prev);
        float ah = sqrtf(s->asked.x * s->asked.x + s->asked.z * s->asked.z);
        float gh = ah > 0 ? (got.x * s->asked.x + got.z * s->asked.z) / ah : 0;
        if (ah > 1.0f && gh < 0.5f * ah && !s->blocked) { /* the floor's part stopped: by him or a wall */
            if (++s->stuck >= 2) {
                s->blocked = 1, s->v.x = s->v.z = 0, s->blocks++;
                o->events |= SOMVR_OVE_BLOCKED;
            }
        } else if (s->asked.y < -1.0f && got.y > 0.3f * s->asked.y) { /* the fall stopped: the ground */
            if (++s->stuck >= 2) {
                o->events |= SOMVR_OVE_LAND, o->why = SOMVR_OVW_LANDED, s->lands++;
                to_idle(s);
                return;
            }
        } else {
            s->stuck = 0;
        }
    }
    if (in->t - s->t0 > c->flight_max_s || !in->live) {
        o->events |= SOMVR_OVE_LAND, o->why = !in->live ? SOMVR_OVW_LIVE : SOMVR_OVW_TIME, s->lands++;
        to_idle(s);
        return;
    }
    float t1 = (float)dt;
    sv_vec3 step = { s->v.x * t1, s->v.y * t1 - 0.5f * c->g * t1 * t1, s->v.z * t1 };
    s->v.y -= c->g * t1;
    o->move = step, s->asked = step, o->grav_off = 1;
    if (in->root_ok)
        s->root_prev = in->root, s->have_prev = 1;
}
