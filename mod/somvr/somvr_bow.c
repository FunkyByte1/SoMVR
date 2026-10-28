/* somvr_bow.c - see somvr_bow.h */
#include "somvr_bow.h"

#include <math.h>
#include <string.h>

static const char *const g_end_names[SOMVR_BOW_NEND] = { "none", "apart", "game", "input", "lost", "outside", "slot" };
static const char *const g_phase_names[4] = { "idle", "aim", "draw", "follow" };
static const char *const g_arrow_names[4] = { "none", "hand", "string", "rest" };

const char *somvr_bow_end_name(int why) { return why >= 0 && why < SOMVR_BOW_NEND ? g_end_names[why] : "?"; }
const char *somvr_bow_phase_name(int phase) { return phase >= 0 && phase < 4 ? g_phase_names[phase] : "?"; }
const char *somvr_bow_arrow_name(int arrow) { return arrow >= 0 && arrow < 4 ? g_arrow_names[arrow] : "?"; }

void somvr_bow_cfg_default(somvr_bow_cfg *c) {
    memset(c, 0, sizeof *c);
    c->bow_hand = 0; /* SOMVR_HAND_L */
    c->nock_r_cm = 15;
    c->grip_on = 0.6f, c->grip_off = 0.4f;
    c->window_s = 0.25f;
    c->min_draw_cm = 20, c->full_draw_cm = 55;
    c->dir_min_cm = 5, c->dir_blend_cm = 10;
    c->tick_cm = 4;
    c->exit_cm = 40, c->exit_s = 0.8f;
    c->lost_s = 0.3f;
    c->aim_min_s = 0.25f, c->fire_s = 0.1f, c->wait_s = 0.6f, c->follow_s = 0.4f;
    c->fire_mode = SOMVR_BOW_FIRE_TAP;
    c->idle_ray = SOMVR_BOW_IDLE_BOW;
    c->idle_nock = 1;
    c->shot_ax[2] = 1;
    c->zone_r_cm = 25, c->zone_half_cm = 12, c->zone_front_cm = 5;
    c->limb_ax[1] = 1;
}

void somvr_bow_init(somvr_bow *s) {
    memset(s, 0, sizeof *s);
    s->lost_t[0] = s->lost_t[1] = -1;
    s->apart_t = -1;
    s->q[0] = s->q[1] = (sv_quat){ 0, 0, 0, 1 };
}

static float len3(sv_vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static sv_vec3 add3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static sv_vec3 sub3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static sv_vec3 mul3(sv_vec3 a, float k) { return (sv_vec3){ a.x * k, a.y * k, a.z * k }; }
static float dot3(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static sv_vec3 cross3(sv_vec3 a, sv_vec3 b) {
    return (sv_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static int fin3(sv_vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }
static int finq(sv_quat q) { return isfinite(q.x) && isfinite(q.y) && isfinite(q.z) && isfinite(q.w); }
static sv_vec3 unit3(sv_vec3 v, sv_vec3 dflt) {
    float l = len3(v);
    return l > 1e-6f && isfinite(l) ? mul3(v, 1.0f / l) : dflt;
}
static float clamp01(float x) { return x > 0 ? (x < 1 ? x : 1) : 0; } /* NaN: 0 */

sv_vec3 somvr_bow_nock(const somvr_bow_cfg *c, sv_vec3 p, sv_quat q) {
    return add3(p, sv_qrot(q, (sv_vec3){ c->nock_ofs[0], c->nock_ofs[1], c->nock_ofs[2] }));
}

sv_vec3 somvr_bow_fwd(const somvr_bow_cfg *c, sv_quat q) {
    sv_vec3 a = unit3((sv_vec3){ c->shot_ax[0], c->shot_ax[1], c->shot_ax[2] }, (sv_vec3){ 0, 0, 1 });
    return unit3(sv_qrot(q, a), (sv_vec3){ 0, 0, 1 });
}

static sv_quat qaxis(int axis, float deg) {
    float h = 0.5f * deg * 0.017453292519943295f, sn = sinf(h);
    return (sv_quat){ axis == 0 ? sn : 0, axis == 1 ? sn : 0, axis == 2 ? sn : 0, cosf(h) };
}

sv_quat somvr_bow_hold_q(int hold, int bow_hand, float tilt_deg, float cant_deg) {
    if (hold != SOMVR_BOW_HOLD_FIST) return (sv_quat){ 0, 0, 0, 1 };
    if (!isfinite(tilt_deg)) tilt_deg = 0;
    if (!isfinite(cant_deg)) cant_deg = 0;
    /* R_x(90): +Y (the upper limb) -> +Z (the tube), +Z (the shot) -> -Y; the cant about the grip's +Y (the shot's axis
     * before the tilt: + takes the upper limb toward +X, the right: the string hand's side of a left-hand bow), then the
     * tilt about +X (+ takes it toward -Y, the target) */
    sv_quat t = qaxis(0, 90);
    t = sv_qmul(qaxis(1, bow_hand ? -cant_deg : cant_deg), t);
    t = sv_qmul(qaxis(0, tilt_deg), t);
    return sv_qnormalize(t);
}

sv_vec3 somvr_bow_draw_dir(sv_vec3 nock, sv_vec3 string, float *len) {
    sv_vec3 d = sub3(nock, string);
    float l = len3(d);
    if (!(l > 1e-6f) || !isfinite(l)) {
        if (len) *len = 0;
        return (sv_vec3){ 0, 0, 1 };
    }
    if (len) *len = l;
    return mul3(d, 1.0f / l);
}

float somvr_bow_dir_weight(const somvr_bow_cfg *c, float draw_cm) {
    float x = c->dir_blend_cm > 0 ? clamp01((draw_cm - c->dir_min_cm) / c->dir_blend_cm) : draw_cm >= c->dir_min_cm;
    return x * x * (3 - 2 * x);
}

int somvr_bow_at_nock(const somvr_bow_cfg *c, sv_vec3 bow_p, sv_quat bow_q, sv_vec3 string, float *zd) {
    sv_vec3 nock = somvr_bow_nock(c, bow_p, bow_q), d = sub3(string, nock);
    float r = len3(d);
    if (!c->nock_zone) {
        if (zd) *zd = isfinite(r) ? r : 999;
        return isfinite(r) && r <= c->nock_r_cm;
    }
    /* the zone: the distance from the string's middle segment, nothing past zone_front_cm toward the target */
    sv_vec3 L = unit3(sv_qrot(bow_q, (sv_vec3){ c->limb_ax[0], c->limb_ax[1], c->limb_ax[2] }), (sv_vec3){ 0, 1, 0 });
    float front = dot3(d, somvr_bow_fwd(c, bow_q)), k = dot3(d, L);
    if (!isfinite(front) || !isfinite(k) || front > c->zone_front_cm) {
        if (zd) *zd = 999;
        return 0;
    }
    k = k > c->zone_half_cm ? c->zone_half_cm : k < -c->zone_half_cm ? -c->zone_half_cm : k;
    /* build 19: the core swept back to the string's rest line (behind the nock: -front) */
    float b = c->zone_back_cm > 0 ? fminf(fmaxf(-front, 0), c->zone_back_cm) : 0;
    float z = len3(add3(sub3(d, mul3(L, k)), mul3(somvr_bow_fwd(c, bow_q), b)));
    if (zd) *zd = z;
    return z <= c->zone_r_cm;
}

float somvr_bow_pull(const somvr_bow_cfg *c, sv_quat bow_q, sv_vec3 nock, sv_vec3 string) {
    float b = dot3(sub3(nock, string), somvr_bow_fwd(c, bow_q));
    return isfinite(b) && b > 0 ? b : 0;
}

float somvr_bow_back(const somvr_bow_cfg *c, sv_quat bow_q, sv_vec3 nock, sv_vec3 string) {
    float b = fabsf(dot3(sub3(nock, string), somvr_bow_fwd(c, bow_q)));
    return isfinite(b) ? b : 0;
}

sv_vec3 somvr_bow_ray_dir(const somvr_bow_cfg *c, sv_quat bow_q, sv_vec3 nock, sv_vec3 string, float *draw) {
    float d;
    sv_vec3 sd = somvr_bow_draw_dir(nock, string, &d), f = somvr_bow_fwd(c, bow_q);
    float w = somvr_bow_dir_weight(c, somvr_bow_back(c, bow_q, nock, string));
    if (draw) *draw = d;
    if (w >= 1) return sd;
    if (w <= 0) return f;
    sv_vec3 m = add3(mul3(f, 1 - w), mul3(sd, w)); /* opposite directions: the string's (it is the shot) */
    return unit3(m, sd);
}

sv_quat somvr_bow_look(sv_vec3 dir, float yaw_hint) {
    sv_vec3 d = unit3(dir, (sv_vec3){ 0, 0, 1 });
    float h = d.x * d.x + d.z * d.z;
    float yaw = h > 1e-8f ? atan2f(d.x, d.z) : (isfinite(yaw_hint) ? yaw_hint : 0);
    float pitch = asinf(d.y > 1 ? 1 : d.y < -1 ? -1 : d.y);
    sv_quat qx = { sinf(-0.5f * pitch), 0, 0, cosf(-0.5f * pitch) }; /* about +X by -pitch: (0,0,1) -> (0, sin p, cos p) */
    return sv_qnormalize(sv_qmul(sv_qyaw(yaw), qx));
}

sv_quat somvr_bow_align(sv_quat q, sv_vec3 dir, float w) { return somvr_bow_align_ax(q, (sv_vec3){ 0, 0, 1 }, dir, w); }

sv_quat somvr_bow_align_ax(sv_quat q, sv_vec3 ax, sv_vec3 dir, float w) {
    if (!finq(q)) return (sv_quat){ 0, 0, 0, 1 };
    q = sv_qnormalize(q);
    w = clamp01(w);
    sv_vec3 f = unit3(sv_qrot(q, unit3(ax, (sv_vec3){ 0, 0, 1 })), (sv_vec3){ 0, 0, 1 }), d = unit3(dir, f);
    if (w <= 0 || !fin3(d)) return q;
    float c = dot3(f, d);
    sv_vec3 a = cross3(f, d);
    float s = len3(a), ang = atan2f(s, c) * w;
    if (s < 1e-6f) {
        if (c > 0) return q;                                                              /* already there */
        a = cross3(f, fabsf(f.y) < 0.9f ? (sv_vec3){ 0, 1, 0 } : (sv_vec3){ 1, 0, 0 }); /* half a turn: any normal */
        s = len3(a);
    }
    a = mul3(a, 1.0f / s);
    sv_quat r = { a.x * sinf(0.5f * ang), a.y * sinf(0.5f * ang), a.z * sinf(0.5f * ang), cosf(0.5f * ang) };
    return sv_qnormalize(sv_qmul(r, q));
}

/* ---- our own shot ------------------------------------------------------------------------------------------------ */

static const char *const g_shot_why[SOMVR_BOW_NSHOT] = {
    "ok",
    "a fire site did not match at start",
    "the game's free aim is not on",
    "the player's server Combat, its owner or the inventory did not check out",
    "no active item",
    "the active item has no RangedWeaponData",
    "the arrow counter is not readable",
    "no arrows",
    "no finite ray",
};

const char *somvr_bow_shot_why(int r) { return r >= 0 && r < SOMVR_BOW_NSHOT ? g_shot_why[r] : "?"; }

int somvr_bow_shot_check(const somvr_bow_shot_in *in) {
    if (!in->sites) return SOMVR_BOW_SHOT_SITES;
    if (!in->game_aim && !in->any_aim) return SOMVR_BOW_SHOT_NO_AIM;
    if (!in->combat) return SOMVR_BOW_SHOT_COMBAT;
    if (!in->item) return SOMVR_BOW_SHOT_NO_ITEM;
    if (!in->ranged) return SOMVR_BOW_SHOT_NOT_RANGED;
    if (!in->unlimited && in->arrows < 0) return SOMVR_BOW_SHOT_ARROWS_UNKNOWN;
    if (!in->unlimited && in->arrows == 0) return SOMVR_BOW_SHOT_NO_ARROWS;
    float l = len3(in->dir);
    if (!fin3(in->origin) || !fin3(in->dir) || !(l > 0.5f && l < 2.0f)) return SOMVR_BOW_SHOT_RAY;
    return SOMVR_BOW_SHOT_OK;
}

int somvr_bow_shot_cost(const somvr_bow_shot_in *in) { return in->unlimited ? 0 : -1; }

void somvr_bow_fire_xf(sv_vec3 origin, sv_vec3 dir, float yaw_hint, float xf[7]) {
    sv_quat q = somvr_bow_look(dir, yaw_hint);
    xf[0] = origin.x, xf[1] = origin.y, xf[2] = origin.z;
    xf[3] = q.x, xf[4] = q.y, xf[5] = q.z, xf[6] = q.w;
}

/* ---- the state machine ------------------------------------------------------------------------------------------- */

static void enter(somvr_bow *s, double t, int by_button, somvr_bow_out *o) {
    s->phase = SOMVR_BOW_PH_AIM, s->phase_t = t, s->aim_t0 = t, s->apart_t = -1;
    s->by_button = by_button;
    s->shot = 0, s->fire_on = 0, s->fire_until = 0;
    s->aims++;
    if (by_button) s->by_btn++;
    o->events |= SOMVR_BOW_E_ENTER;
}

static void begin_draw(somvr_bow *s, const somvr_bow_cfg *c, double t, float d, somvr_bow_out *o) {
    s->phase = SOMVR_BOW_PH_DRAW, s->phase_t = t, s->apart_t = -1;
    s->looking = 0;
    s->draw_cm = d, s->draw_max = d, s->tension = 0;
    s->armed = s->full = 0;
    s->ticks = c->tick_cm > 0 ? (int)(fminf(d, c->full_draw_cm) / c->tick_cm) : 0; /* ticks count new draw */
    s->draws++;
    s->arrow = SOMVR_BOW_ARROW_STRING;
    o->events |= SOMVR_BOW_E_NOCK;
}

static void to_aim(somvr_bow *s, double t) {
    s->phase = SOMVR_BOW_PH_AIM, s->phase_t = t, s->apart_t = -1;
    s->draw_cm = 0, s->tension = 0;
}

static void end_aim(somvr_bow *s, int why, double t, somvr_bow_out *o) {
    if (s->phase == SOMVR_BOW_PH_IDLE) return;
    if (s->shot) s->shot = 0, s->dropped++, o->events |= SOMVR_BOW_E_DROPPED;
    s->phase = SOMVR_BOW_PH_IDLE, s->phase_t = t, s->apart_t = -1;
    s->fire_on = 0, s->fire_until = 0, s->looking = 0;
    s->draw_cm = 0, s->tension = 0, s->have_ray = 0;
    s->arrow = SOMVR_BOW_ARROW_NONE; /* back in the quiver: nothing was spent */
    s->last_end = why;
    if (why >= 0 && why < SOMVR_BOW_NEND) s->exits[why]++;
    o->events |= SOMVR_BOW_E_EXIT;
    o->end_why = why;
}

static void fire_now(somvr_bow *s, double t, somvr_bow_out *o) {
    s->fire_t0 = t, s->fire_seq++, s->fired++;
    o->events |= SOMVR_BOW_E_FIRE;
    o->wait_s = s->last_wait = s->shot ? t - s->shot_t : 0;
    s->shot = 0;
}

void somvr_bow_stop(somvr_bow *s, int why, double t, somvr_bow_out *o) {
    memset(o, 0, sizeof *o);
    end_aim(s, why, t, o);
    o->phase = s->phase;
}

void somvr_bow_cmds(const somvr_bow *s, double t, uint32_t *seen, int *aim, int *fire) {
    *aim = s->phase != SOMVR_BOW_PH_IDLE;
    int f = s->fire_on || (t >= s->fire_t0 && t < s->fire_until);
    if (s->fire_seq != *seen) *seen = s->fire_seq, f = 1; /* a tap shorter than a tick still goes out once */
    *fire = *aim && f;
}

void somvr_bow_step(somvr_bow *s, const somvr_bow_cfg *c, const somvr_bow_in *in, somvr_bow_out *o) {
    memset(o, 0, sizeof *o);
    double t = in->t;
    int B = c->bow_hand ? 1 : 0, S = 1 - B;
    s->frames++;
    for (int h = 0; h < 2; h++) {
        int ok = in->valid[h] && fin3(in->p[h]) && finq(in->q[h]);
        if (ok) {
            s->lost_t[h] = -1, s->have[h] = 1, s->p[h] = in->p[h], s->q[h] = sv_qnormalize(in->q[h]);
        } else if (s->lost_t[h] < 0) {
            s->lost_t[h] = t;
        }
    }
    int vb = s->lost_t[B] < 0, vs = s->lost_t[S] < 0;
    int lost_b = !vb && t - s->lost_t[B] >= c->lost_s, lost_s = !vs && t - s->lost_t[S] >= c->lost_s;
    /* the string (a NaN reads open) and the button's press edge */
    float sq = in->string == in->string ? in->string : 0;
    int was = s->down;
    if (s->down ? sq < c->grip_off : sq > c->grip_on) s->down = !s->down;
    int press = s->down && !was, open = !s->down && was;
    int btn = in->button != 0, btn_press = btn && !s->button_was;
    s->button_was = btn;
    /* build 16, the quiver: a press inside it with the bow out and no arrow anywhere takes one (the press is the
     * quiver's: it looks for no nock); no arrows left: nothing (EMPTY) */
    int qpress = 0;
    if (c->quiver && press && s->phase != SOMVR_BOW_PH_IDLE && in->quiver && s->arrow == SOMVR_BOW_ARROW_NONE) {
        qpress = 1;
        if (in->arrows == 0) s->empties++, o->events |= SOMVR_BOW_E_EMPTY;
        else s->arrow = SOMVR_BOW_ARROW_HAND, s->takes++, o->events |= SOMVR_BOW_E_TAKE;
    }
    if (press) s->qheld = !qpress;
    if (!s->down) s->qheld = 0;
    /* build 19, the grab: the string closed outside the quiver and held into it takes the arrow there */
    if (c->quiver && c->quiver_held && !press && s->qheld && s->phase != SOMVR_BOW_PH_IDLE && in->quiver &&
        s->arrow == SOMVR_BOW_ARROW_NONE) {
        qpress = 1, s->qheld = 0, s->looking = 0;
        if (in->arrows == 0) s->empties++, o->events |= SOMVR_BOW_E_EMPTY;
        else s->arrow = SOMVR_BOW_ARROW_HAND, s->takes++, s->held_takes++, o->events |= SOMVR_BOW_E_TAKE;
    }
    if (press && !qpress) s->looking = 1, s->press_t = t;
    if (!s->down) s->looking = 0;
    if (c->quiver && s->arrow == SOMVR_BOW_ARROW_HAND && (open || lost_s)) /* let go before the nock: gone */
        s->arrow = SOMVR_BOW_ARROW_NONE, s->drops++, o->events |= SOMVR_BOW_E_DROP;
    sv_vec3 nock = s->have[B] ? somvr_bow_nock(c, s->p[B], s->q[B]) : (sv_vec3){ 0, 0, 0 };
    float dist = -1, zd = -1;
    int at = 0;
    if (s->have[B] && s->have[S]) dist = len3(sub3(nock, s->p[S])), at = somvr_bow_at_nock(c, s->p[B], s->q[B], s->p[S], &zd);
    o->nock_dist = dist, o->zone_dist = zd;
    int near = vb && vs && dist >= 0 && at;
    /* the quiver: the held arrow nocks by touching the nock; one resting on the string by a new press there */
    int nock_ok = !c->quiver ? s->looking && near
                             : near && ((s->arrow == SOMVR_BOW_ARROW_HAND && s->down) ||
                                        (s->arrow == SOMVR_BOW_ARROW_REST && s->looking));
    if (c->quiver && s->looking && near && s->arrow == SOMVR_BOW_ARROW_NONE && s->phase == SOMVR_BOW_PH_AIM) {
        if (c->noarrow_take && in->arrows != 0) /* build 17's take: one from the quiver, nocked here at once */
            s->arrow = SOMVR_BOW_ARROW_HAND, s->takes++, o->events |= SOMVR_BOW_E_TAKE, nock_ok = 1;
        else if (c->noarrow_take)
            s->looking = 0, s->empties++, o->events |= SOMVR_BOW_E_EMPTY;
        else
            s->looking = 0, s->noarrows++, o->events |= SOMVR_BOW_E_NOARROW;
    }
    /* build 19: an empty press near the bow but outside the zone - told at once (the 17b session's 30 cm miss) */
    if (c->quiver && c->noarrow_cm > 0 && press && s->looking && !near && s->arrow == SOMVR_BOW_ARROW_NONE &&
        s->phase == SOMVR_BOW_PH_AIM && vb && vs && dist >= 0 && dist <= c->noarrow_cm)
        s->looking = 0, s->noarrows++, o->events |= SOMVR_BOW_E_NOARROW;
    /* build 20, auto: the quiver's empty count at the string (nothing nocked), the press held on toward the string */
    int autoa = !c->quiver && c->auto_arrow;
    if (autoa && nock_ok && in->arrows == 0 && s->phase == SOMVR_BOW_PH_AIM)
        nock_ok = 0, s->looking = 0, s->empties++, o->events |= SOMVR_BOW_E_EMPTY;
    if (s->looking && !nock_ok && !autoa && t - s->press_t > c->window_s) { /* the window ran out away from the nock */
        s->looking = 0;
        if (s->phase == SOMVR_BOW_PH_AIM) s->misses++, o->events |= SOMVR_BOW_E_MISS;
    }

    /* stand-downs in any phase */
    if (s->phase != SOMVR_BOW_PH_IDLE) {
        int why = !in->live ? SOMVR_BOW_END_INPUT : !in->allow ? SOMVR_BOW_END_GAME : lost_b ? SOMVR_BOW_END_LOST : 0;
        if (why) {
            end_aim(s, why, t, o);
            s->looking = 0;
            goto out;
        }
    }

    switch (s->phase) {
    case SOMVR_BOW_PH_IDLE:
        if (!in->live || !in->allow) break;
        if (nock_ok && c->idle_nock && !c->quiver) {
            enter(s, t, 0, o);
            begin_draw(s, c, t, c->nock_zone ? somvr_bow_pull(c, s->q[B], nock, s->p[S]) : dist, o);
        } else if (btn_press && vb) {
            enter(s, t, 1, o);
        }
        break;
    case SOMVR_BOW_PH_AIM:
        if (nock_ok) {
            begin_draw(s, c, t, c->nock_zone ? somvr_bow_pull(c, s->q[B], nock, s->p[S]) : dist, o);
            break;
        }
        if (!btn && !(c->quiver && (s->arrow != SOMVR_BOW_ARROW_NONE || in->quiver)) &&
            (lost_s || dist < 0 || dist > c->exit_cm)) {
            if (s->apart_t < 0) s->apart_t = t;
            if (t - s->apart_t >= c->exit_s) end_aim(s, SOMVR_BOW_END_APART, t, o);
        } else {
            s->apart_t = -1;
        }
        break;
    case SOMVR_BOW_PH_DRAW:
        if (lost_s) { /* the string hand gone: no shot */
            if (s->fire_on) s->fire_on = 0; /* hold mode: the command's OFF (the game may shoot) */
            s->letdowns++, o->events |= SOMVR_BOW_E_LETDOWN;
            s->arrow = c->quiver ? SOMVR_BOW_ARROW_REST : SOMVR_BOW_ARROW_NONE;
            to_aim(s, t);
            break;
        }
        if (open) {
            int shoot = c->fire_mode == SOMVR_BOW_FIRE_HOLD ? s->armed : s->draw_cm >= c->min_draw_cm;
            if (!shoot) {
                s->fire_on = 0;
                s->letdowns++, o->events |= SOMVR_BOW_E_LETDOWN;
                s->arrow = c->quiver ? SOMVR_BOW_ARROW_REST : SOMVR_BOW_ARROW_NONE; /* the quiver's: on the string */
                to_aim(s, t);
                break;
            }
            s->arrow = SOMVR_BOW_ARROW_NONE; /* it flies */
            s->lat_o = s->origin, s->lat_d = s->dir; /* the last held frame's ray */
            if (!s->have_ray) s->lat_o = nock, s->lat_d = somvr_bow_fwd(c, s->q[B]);
            s->shots++, s->last_draw = s->draw_cm;
            o->events |= SOMVR_BOW_E_RELEASE;
            s->phase = SOMVR_BOW_PH_FOLLOW, s->phase_t = t;
            s->shot = 1, s->shot_t = t;
            s->fire_t0 = t; /* FOLLOW's clock until a fire or a drop restarts it */
            if (s->fire_on) { /* hold mode: the hold's end (the command's OFF) is the shot */
                s->fire_on = 0;
                s->fire_t0 = t, s->fire_until = t, s->fired++, s->last_wait = 0, s->shot = 0;
                o->events |= SOMVR_BOW_E_FIRE;
            }
            break;
        }
        if (vs && vb && dist >= 0) {
            if (c->nock_zone) dist = somvr_bow_pull(c, s->q[B], nock, s->p[S]); /* the zone: the pull back alone */
            s->draw_cm = dist;
            s->draw_max = dist > s->draw_max ? dist : s->draw_max;
            s->tension = c->full_draw_cm > 0 ? clamp01(dist / c->full_draw_cm) : 1;
            if (!s->armed && dist >= c->min_draw_cm) s->armed = 1, o->events |= SOMVR_BOW_E_ARMED;
            if (!s->full && dist >= c->full_draw_cm) s->full = 1, o->events |= SOMVR_BOW_E_FULL;
            int k = c->tick_cm > 0 ? (int)(fminf(dist, c->full_draw_cm) / c->tick_cm) : 0;
            if (k > s->ticks) s->ticks = k, o->events |= SOMVR_BOW_E_TICK;
        }
        if (c->fire_mode == SOMVR_BOW_FIRE_HOLD && s->armed && !s->fire_on && in->game_aim &&
            t - s->aim_t0 >= c->aim_min_s)
            s->fire_on = 1, s->fire_seq++, o->events |= SOMVR_BOW_E_HOLD; /* the tick sends it at least once */
        break;
    case SOMVR_BOW_PH_FOLLOW:
        if (!s->shot && t - s->fire_t0 >= c->follow_s) to_aim(s, t);
        break;
    }

    /* the pending shot: the game's aim on and the aim held long enough (or, release_now, the bow ready), else dropped
     * after wait_s */
    if (s->shot && s->phase == SOMVR_BOW_PH_FOLLOW) {
        if ((in->game_aim && t - s->aim_t0 >= c->aim_min_s) || (c->release_now && in->bow_ready)) {
            fire_now(s, t, o);
            s->fire_until = t + c->fire_s;
        } else if (t - s->shot_t >= c->wait_s) {
            s->shot = 0, s->dropped++, o->events |= SOMVR_BOW_E_DROPPED;
            s->fire_t0 = t;
        }
    }

out:
    /* the ray (the last valid grips: a hand untracked for a few frames freezes it) */
    o->ray = 0;
    if (s->phase == SOMVR_BOW_PH_FOLLOW) {
        o->ray = 1, o->origin = s->lat_o, o->dir = s->lat_d;
    } else if (s->phase == SOMVR_BOW_PH_DRAW && s->have[B] && s->have[S]) {
        o->ray = 1, o->origin = nock, o->dir = somvr_bow_ray_dir(c, s->q[B], nock, s->p[S], NULL);
    } else if ((s->phase == SOMVR_BOW_PH_AIM || s->phase == SOMVR_BOW_PH_DRAW) && s->have[B] &&
               c->idle_ray == SOMVR_BOW_IDLE_BOW) {
        o->ray = 1, o->origin = nock, o->dir = somvr_bow_fwd(c, s->q[B]);
    }
    if (o->ray && (!fin3(o->origin) || !fin3(o->dir))) o->ray = 0;
    if (s->phase != SOMVR_BOW_PH_FOLLOW) {
        s->have_ray = o->ray;
        if (o->ray) s->origin = o->origin, s->dir = o->dir;
    }
    if (o->ray) s->ray_frames++;
    o->phase = s->phase;
    o->aim = s->phase != SOMVR_BOW_PH_IDLE;
    o->fire = o->aim && (s->fire_on || (t >= s->fire_t0 && t < s->fire_until));
    o->draw_cm = s->phase == SOMVR_BOW_PH_DRAW ? s->draw_cm : 0;
    o->tension = s->phase == SOMVR_BOW_PH_DRAW ? s->tension : 0;
    o->arrow = s->arrow;
}

int somvr_bow_string_shown(int mode, int phase) {
    if (phase == SOMVR_BOW_PH_IDLE) return 0; /* the bow away */
    return mode == SOMVR_BOW_SDRAW_ON || (mode == SOMVR_BOW_SDRAW_DRAWN && phase == SOMVR_BOW_PH_DRAW);
}
