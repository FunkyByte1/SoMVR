/* somvr_climb.c - see somvr_climb.h */
#include "somvr_climb.h"

#include <math.h>
#include <string.h>

static const char *const g_end_names[SOMVR_CLIMB_NEND] = { "none", "released", "game", "input", "timeout", "top", "lifted",
                                                           "lift-stall", "cmd", "outside", "over", "drop" };
static const char *const g_phase_names[5] = { "none", "hold", "lift", "cmd", "hang" };

const char *somvr_climb_end_name(int why) { return why >= 0 && why < SOMVR_CLIMB_NEND ? g_end_names[why] : "?"; }
const char *somvr_climb_phase_name(int phase) { return phase >= 0 && phase < 5 ? g_phase_names[phase] : "?"; }
const char *somvr_climb_via_name(int via) {
    static const char *const n[5] = { "-", "palm", "fingers", "down", "inside" };
    return via >= 0 && via < 5 ? n[via] : "?";
}

void somvr_climb_cfg_default(somvr_climb_cfg *c) {
    memset(c, 0, sizeof *c);
    c->grip_on = 0.6f, c->grip_off = 0.4f;
    c->reach_cm = 25, c->back_cm = 10;
    c->rays = SOMVR_CLIMB_RAY_PALM | SOMVR_CLIMB_RAY_FWD;
    c->window_s = 0.25f;
    c->step_cm = 8, c->carry_cm = 20, c->jump_cm = 25;
    c->lost_s = 0.3f, c->max_s = 120;
    c->edge_cm = 25, c->edge_far_cm = 15, c->top_cm = 10;
    c->top_mode = SOMVR_CLIMB_TOP_PUSH, c->over_cm = 30, c->down_assist = 1, c->ramp = 1, c->low_cm = 50;
    c->lift_cm = 185, c->lift_fwd_cm = 40, c->lift_s = 1.5f, c->lift_still_s = 0.3f;
    c->cmd_s = 0.6f;
    c->letgo = SOMVR_CLIMB_LETGO_HANG;
    c->inside_cm = 30, c->near_cm = 35, c->ground_cm = 125;
    c->cue = 1, c->cue_s = 1.0f / 30, c->cue_off_s = 0.3f, c->cue_raise_cm = 10;
}

void somvr_climb_init(somvr_climb *s) {
    memset(s, 0, sizeof *s);
    s->driver = -1;
    for (int h = 0; h < 2; h++) s->h[h].armed = 1, s->h[h].lost_t = -1, s->h[h].near_past = -1;
}

int somvr_climb_grip(int *down, float squeeze, float on, float off) {
    if (!(squeeze == squeeze)) squeeze = 0; /* NaN: open */
    if (*down ? squeeze < off : squeeze > on) *down = !*down;
    return *down;
}

sv_vec3 somvr_climb_to_world(sv_vec3 d, float rig_yaw) { return sv_qrot(sv_qyaw(rig_yaw), d); }

/* case-insensitive: s starts with p (pre) or contains it */
static int has_ci(const char *s, const char *p, int pre) {
    for (const char *t = s; *t; t++) {
        const char *a = t, *b = p;
        while (*a && *b && (*a | 0x20) == (*b | 0x20)) a++, b++;
        if (!*b) return 1;
        if (pre) return 0;
    }
    return 0;
}

int somvr_climb_surface_ok(int mode, const char *name, int can) {
    const char *n = name ? name : "";
    if (mode == SOMVR_CLIMB_SURF_GAME) return can != 0;
    if (has_ci(n, "flesh", 1)) return 0;
    if (mode == SOMVR_CLIMB_SURF_ANY) return 1;
    static const char *const pre[] = { "armor", "sky", "air", "invisible", "playerblocker", "water", "liquid", "foliage",
                                       "ice" };
    static const char *const sub[] = { "mud", "slip", "puddle" };
    for (unsigned i = 0; i < sizeof pre / sizeof pre[0]; i++)
        if (has_ci(n, pre[i], 1)) return 0;
    for (unsigned i = 0; i < sizeof sub / sizeof sub[0]; i++)
        if (has_ci(n, sub[i], 0)) return 0;
    return 1;
}

static float len3(sv_vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static sv_vec3 add3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static sv_vec3 sub3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static sv_vec3 mul3(sv_vec3 a, float k) { return (sv_vec3){ a.x * k, a.y * k, a.z * k }; }
static int fin3(sv_vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }

sv_vec3 somvr_climb_clamp(sv_vec3 v, float max_len) {
    if (!fin3(v) || !(max_len >= 0)) return (sv_vec3){ 0, 0, 0 };
    float l = len3(v);
    return l > max_len && l > 0 ? mul3(v, max_len / l) : v;
}

static sv_vec3 unit3(sv_vec3 v) {
    float l = len3(v);
    return l > 1e-6f && isfinite(l) ? mul3(v, 1.0f / l) : (sv_vec3){ 0, 0, 0 };
}

/* one ray through the callback (counted); 0 = no answer (counted as no hit) */
static int cast(somvr_climb *s, somvr_climb_out *o, somvr_climb_ray_fn ray, void *ctx, int hand, sv_vec3 from, sv_vec3 dir,
                float len, somvr_climb_hit *hit) {
    memset(hit, 0, sizeof *hit);
    if (!ray || !fin3(from) || !fin3(dir) || !(len > 0)) return 0;
    s->rays++, o->rays++;
    if (!ray(ctx, hand, from, dir, len, hit)) {
        memset(hit, 0, sizeof *hit);
        return 0;
    }
    if (!fin3(hit->point) || !(hit->dist >= 0)) memset(hit, 0, sizeof *hit);
    return 1;
}

/* the rays of hand h (palm, fingers, while climbing down; none within reach: the head to the hand), the nearest hit
 * within reach. 1 = a surface (*bh, its ray in *dir from *from, *blen the ray's length, *via), 0 = none. press: this
 * press's near miss (a surface past the reach) kept in the hold; the out's grab log fields */
static int find_hold(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                     void *ctx, somvr_climb_out *o, int press, somvr_climb_hit *bh, sv_vec3 *dir, sv_vec3 *from, float *blen,
                     int *via) {
    const somvr_climb_hand *hd = &in->hand[h];
    somvr_climb_hold *k = &s->h[h];
    sv_vec3 dirs[3];
    int vias[3], nd = 0;
    if (c->rays & SOMVR_CLIMB_RAY_PALM) dirs[nd] = unit3(hd->palm), vias[nd++] = SOMVR_CLIMB_VIA_PALM;
    if (c->rays & SOMVR_CLIMB_RAY_FWD) dirs[nd] = unit3(hd->fwd), vias[nd++] = SOMVR_CLIMB_VIA_FWD;
    if (c->down_assist && s->active) dirs[nd] = (sv_vec3){ 0, -1, 0 }, vias[nd++] = SOMVR_CLIMB_VIA_DOWN;
    float len = c->back_cm + c->reach_cm, ext = len + fmaxf(c->near_cm, 0.0f);
    int best = -1, cast_no = 0;
    somvr_climb_hit r;
    memset(bh, 0, sizeof *bh);
    for (int i = 0; i < nd; i++) {
        if (len3(dirs[i]) < 0.5f) continue;
        sv_vec3 f = sub3(hd->pw, mul3(dirs[i], c->back_cm));
        if (press) o->grab_rays[h]++;
        if (!cast(s, o, ray, ctx, h, f, dirs[i], ext, &r) || !r.hit) continue;
        if (r.dist > len) { /* past the reach: a near miss, not a hold */
            if (press && (k->near_past < 0 || r.dist - len < k->near_past))
                k->near_past = r.dist - len, k->near_via = vias[i];
            continue;
        }
        if (best < 0 || r.dist < bh->dist)
            best = i, *bh = r, *from = f, *dir = dirs[i], *blen = len, *via = vias[i], cast_no = o->rays;
    }
    if (best < 0 && c->inside_cm > 0 && in->head_ok) { /* the hand inside a wall: the head's ray meets it first */
        sv_vec3 head = { in->head_x, in->head_y, in->head_z }, hv = sub3(hd->pw, head);
        float hl = len3(hv);
        if (hl > 5.0f && isfinite(hl)) {
            sv_vec3 d = mul3(hv, 1.0f / hl);
            if (press) o->grab_rays[h]++;
            if (cast(s, o, ray, ctx, h, head, d, hl + 2.0f, &r) && r.hit && r.dist >= hl - c->inside_cm)
                best = nd, *bh = r, *from = head, *dir = d, *blen = hl + 2.0f, *via = SOMVR_CLIMB_VIA_INSIDE,
                cast_no = o->rays;
        }
    }
    if (press) o->grab_hit[h] = *bh, o->grab_cast[h] = best < 0 ? 0 : cast_no;
    return best >= 0;
}

/* the grab test of hand h (find_hold; a press): 1 = a hold (the hold's surface, direction and edge filled), 0 = none
 * yet, -1 = a surface that is not climbable */
static int grab_test(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                     void *ctx, somvr_climb_out *o) {
    const somvr_climb_hand *hd = &in->hand[h];
    somvr_climb_hit bh;
    sv_vec3 bdir = { 0, 0, 0 }, bfrom = { 0, 0, 0 };
    float blen = 0;
    int via = 0;
    if (!find_hold(s, c, in, h, ray, ctx, o, 1, &bh, &bdir, &bfrom, &blen, &via)) return 0;
    if (!bh.climbable) return -1;
    somvr_climb_hold *k = &s->h[h];
    k->hit = bh.point, k->dir = bdir, k->hit_dist = bh.dist, k->anchor = hd->pw, k->via = via;
    k->inside_by = via == SOMVR_CLIMB_VIA_INSIDE ? blen - 2.0f - bh.dist : 0;
    if (via == SOMVR_CLIMB_VIA_INSIDE) s->insides++;
    /* toward the wall: the ray's horizontal direction (into the face), for a ray down onto a top the head's to the
     * hold, else the fingers' */
    sv_vec3 tw = { k->dir.x, 0, k->dir.z };
    if (len3(tw) < 0.5f)
        tw = in->head_ok ? (sv_vec3){ bh.point.x - in->head_x, 0, bh.point.z - in->head_z } : (sv_vec3){ 0, 0, 0 };
    if (len3(tw) < 0.2f) tw = (sv_vec3){ hd->fwd.x, 0, hd->fwd.z };
    k->toward = unit3(tw);
    /* the edge: the same ray edge_cm higher finds no wall, or finds one much farther away */
    somvr_climb_hit e, d;
    sv_vec3 up = { 0, c->edge_cm, 0 };
    k->edge = 0, k->top_found = 0, k->top_y = bh.point.y + c->edge_cm;
    if (cast(s, o, ray, ctx, h, add3(bfrom, up), k->dir, blen + c->edge_far_cm, &e) &&
        (!e.hit || e.dist > bh.dist + c->edge_far_cm)) {
        k->edge = 1;
        /* the top's height: down from above it, a little into the wall */
        sv_vec3 top_from = add3(add3(bh.point, mul3(k->dir, 4.0f)), (sv_vec3){ 0, c->edge_cm + 5.0f, 0 });
        if (cast(s, o, ray, ctx, h, top_from, (sv_vec3){ 0, -1, 0 }, c->edge_cm + 15.0f, &d) && d.hit) {
            k->top_y = fmaxf(d.point.y, bh.point.y);
            k->top_found = 1;
        }
    }
    return 1;
}

/* the reach cue of hand h (cue): an open hand (seen open, not holding, not squeezed), raised over the root unless a
 * climb is on, probed every cue_s; E_NEAR on its way into the reach of a surface it could hold (out of reach at least
 * cue_off_s before) */
static void cue_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                     void *ctx, somvr_climb_out *o) {
    somvr_climb_hold *k = &s->h[h];
    const somvr_climb_hand *hd = &in->hand[h];
    int ok = c->cue && ray && in->live && in->allow && !in->nograb && hd->valid && hd->buttons && !k->holding &&
             !k->down && (s->active || (in->root_ok && hd->pw.y >= in->root.y + c->cue_raise_cm));
    if (!ok) {
        if (k->cue_in) k->cue_in = 0, k->cue_out_t = in->t;
        return;
    }
    if (in->t < k->cue_t) return;
    k->cue_t = in->t + c->cue_s;
    uint64_t r0 = s->rays;
    somvr_climb_hit bh;
    sv_vec3 d, f;
    float bl;
    int via, near = find_hold(s, c, in, h, ray, ctx, o, 0, &bh, &d, &f, &bl, &via) && bh.climbable;
    s->cue_rays += s->rays - r0;
    if (near && !k->cue_in) {
        if (in->t - k->cue_out_t >= c->cue_off_s) {
            s->nears++;
            o->events |= SOMVR_CLIMB_E_NEAR_L << h;
        }
        k->cue_in = 1;
    } else if (!near && k->cue_in) {
        k->cue_in = 0, k->cue_out_t = in->t;
    }
}

static void let_go(somvr_climb_hold *k) {
    k->holding = 0, k->looking = 0, k->have_last = 0, k->lost_t = -1, k->edge = 0;
}

static void end_climb(somvr_climb *s, int why, int disarm, somvr_climb_out *o) {
    for (int h = 0; h < 2; h++) {
        let_go(&s->h[h]);
        if (disarm) s->h[h].armed = 0;
    }
    int was = s->active;
    s->active = 0, s->phase = SOMVR_CLIMB_PH_NONE, s->driver = -1, s->carry = (sv_vec3){ 0, 0, 0 }, s->topped = 0;
    if (was) {
        s->last_end = why;
        o->ended = 1, o->end_why = why;
        if (why == SOMVR_CLIMB_END_GAME || why == SOMVR_CLIMB_END_INPUT || why == SOMVR_CLIMB_END_OUTSIDE) s->standdowns++;
        if (why == SOMVR_CLIMB_END_TIMEOUT) s->timeouts++;
        if (why == SOMVR_CLIMB_END_OVER) s->overs++;
    }
}

/* letgo = hang: no hand holds any more (let go, tracking lost, or the input lost: disarm, the grips must open first);
 * the climb stays on, nothing moves until a new hold or the drop */
static void hang(somvr_climb *s, const somvr_climb_in *in, int disarm, somvr_climb_out *o) {
    for (int h = 0; h < 2; h++) {
        if (s->h[h].holding) let_go(&s->h[h]);
        if (disarm) s->h[h].armed = 0, s->h[h].looking = 0;
    }
    s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
    if (s->phase != SOMVR_CLIMB_PH_HANG) {
        s->phase = SOMVR_CLIMB_PH_HANG, s->hang_t = in->t;
        s->hangs++;
        o->events |= SOMVR_CLIMB_E_HANG;
    }
}

void somvr_climb_stop(somvr_climb *s, int why, somvr_climb_out *out) {
    memset(out, 0, sizeof *out);
    out->driver = -1;
    end_climb(s, why, 1, out);
    for (int h = 0; h < 2; h++) s->h[h].down = 0;
}

/* the newest hold */
static int newest(const somvr_climb *s) {
    int d = -1;
    for (int h = 0; h < 2; h++)
        if (s->h[h].holding && (d < 0 || s->h[h].seq > s->h[d].seq)) d = h;
    return d;
}

static void request(somvr_climb *s, somvr_climb_out *o, sv_vec3 m) {
    if (!fin3(m) || (m.x == 0 && m.y == 0 && m.z == 0)) return;
    o->move = 1, o->move_w = m;
    s->moves++, s->req_cm += len3(m);
}

/* the top reached: what top_mode does */
static void top_reached(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                        somvr_climb_out *o) {
    s->tops++;
    o->events |= SOMVR_CLIMB_E_TOP;
    if (c->top_mode == SOMVR_CLIMB_TOP_LIFT) {
        s->lifts++;
        s->phase = SOMVR_CLIMB_PH_LIFT, s->phase_t = in->t;
        s->lift_y = k->top_y + c->lift_cm, s->lift_up_done = 0;
        s->fwd_left = c->lift_fwd_cm;
        s->lift_dir = k->toward; /* over the top: toward the hold (a hold on the top has a straight down ray) */
        s->lift_best = in->head_y, s->lift_rise_t = in->t;
        s->carry = (sv_vec3){ 0, 0, 0 };
    } else if (c->top_mode == SOMVR_CLIMB_TOP_PUSH) {
        s->topped = 1, s->top_hit = k->hit, s->top_toward = k->toward, s->top_y = k->top_y; /* the hold goes on */
    } else if (c->top_mode == SOMVR_CLIMB_TOP_CMD) {
        s->cmds++;
        s->phase = SOMVR_CLIMB_PH_CMD, s->phase_t = in->t;
        s->carry = (sv_vec3){ 0, 0, 0 };
        o->top_cmd = 1;
    } else {
        end_climb(s, SOMVR_CLIMB_END_TOP, 1, o);
    }
}

/* the lift: up (measured by the head), then over the top (as asked); each step at most step_cm */
static void lift_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_out *o) {
    if (in->t - s->phase_t > c->lift_s) {
        end_climb(s, SOMVR_CLIMB_END_LIFT_STALL, 1, o);
        return;
    }
    if (in->head_ok && in->head_y > s->lift_best + 1.0f) s->lift_best = in->head_y, s->lift_rise_t = in->t;
    /* up: measured by the head (each frame's head already holds every earlier request, so nothing is counted twice) */
    if (!s->lift_up_done) {
        if (!in->head_ok) return; /* no head this frame: wait (lift_s bounds it) */
        float up = s->lift_y - in->head_y;
        if (up > 0.5f) {
            if (in->t - s->lift_rise_t > c->lift_still_s) { /* asked to go up, the head stays: blocked */
                end_climb(s, SOMVR_CLIMB_END_LIFT_STALL, 1, o);
                return;
            }
            request(s, o, (sv_vec3){ 0, fminf(c->step_cm, up), 0 });
            return;
        }
        s->lift_up_done = 1; /* then over the top: as asked (the head does not show it) */
    }
    if (s->fwd_left > 0 && len3(s->lift_dir) > 0.5f) {
        float f = fminf(c->step_cm, s->fwd_left);
        s->fwd_left -= f;
        request(s, o, mul3(s->lift_dir, f));
        return;
    }
    end_climb(s, SOMVR_CLIMB_END_LIFTED, 1, o);
}

static void step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                 somvr_climb_out *o);

/* the ground within ground_cm under the root (a ray down from it): he stands (a hang there would hold him in the air) */
static int on_ground(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                     somvr_climb_out *o) {
    somvr_climb_hit g;
    if (!(c->ground_cm > 0) || !in->root_ok || s->phase == SOMVR_CLIMB_PH_HANG || s->topped) return 0; /* at a top: hang */
    return cast(s, o, ray, ctx, 0, in->root, (sv_vec3){ 0, -1, 0 }, c->ground_cm, &g) && g.hit && g.dist <= c->ground_cm;
}

void somvr_climb_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *o) {
    step(s, c, in, ray, ctx, o);
    /* the edge step's memory: this frame's request and the root it starts from */
    s->last_req = o->move && o->active ? o->move_w : (sv_vec3){ 0, 0, 0 };
    s->last_root = in->root, s->have_root = in->root_ok && o->active;
}

static void step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                 somvr_climb_out *o) {
    memset(o, 0, sizeof *o);
    o->driver = -1;
    double dt = s->last_t > 0 && in->t > s->last_t ? in->t - s->last_t : 0;
    s->last_t = in->t;
    if (in->live && in->frozen) { /* a menu or the pause: hold everything, the clocks stop */
        s->t0 += dt, s->phase_t += dt, s->lift_rise_t += dt;
        for (int h = 0; h < 2; h++) {
            s->h[h].press_t += dt;
            if (s->h[h].lost_t >= 0) s->h[h].lost_t += dt;
            s->h[h].have_last = 0; /* the hands may move while the menu is up: no pull from that */
        }
        s->drop_down = in->drop; /* the menu's A is the menu's: no drop from a press held into the world */
        o->active = s->active, o->driver = s->driver;
        return;
    }
    s->frames += s->active;
    /* the grips (an untracked hand keeps its grip state: lost_s decides) */
    int was_down[2];
    for (int h = 0; h < 2; h++) {
        const somvr_climb_hand *hd = &in->hand[h];
        was_down[h] = s->h[h].down;
        if (!in->live || !hd->buttons) { /* read as open, but not seen open: no re-arm (as somvr_ctl's buttons) */
            s->h[h].down = 0, s->h[h].looking = 0;
            continue;
        }
        if (!somvr_climb_grip(&s->h[h].down, hd->squeeze, c->grip_on, c->grip_off)) s->h[h].armed = 1, s->h[h].looking = 0;
    }
    /* the drop button: its press (a rising edge, the input live) */
    int drop = in->live && in->drop && !s->drop_down;
    s->drop_down = in->live && in->drop;
    int hangs = c->letgo == SOMVR_CLIMB_LETGO_HANG &&
                (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG); /* not lift's or cmd's tops */
    /* stand down (letgo = hang: the input lost hangs, no timeout) */
    if (s->active && !in->live && hangs) hang(s, in, 1, o);
    else if (s->active && !in->live) end_climb(s, SOMVR_CLIMB_END_INPUT, 1, o);
    else if (s->active && !in->allow) end_climb(s, SOMVR_CLIMB_END_GAME, 1, o);
    else if (s->active && !hangs && in->t - s->t0 > c->max_s) end_climb(s, SOMVR_CLIMB_END_TIMEOUT, 1, o);
    else if (s->active && drop && (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG)) {
        s->drops++; /* let go of the wall: the grips must open before a new hold */
        o->events |= SOMVR_CLIMB_E_DROP;
        end_climb(s, SOMVR_CLIMB_END_DROP, 1, o);
    }
    if (o->ended) return;
    if (s->active && !in->live) { /* hanging without input: nothing more until it is back */
        o->active = 1, o->driver = -1;
        return;
    }
    int lifting = s->phase == SOMVR_CLIMB_PH_LIFT, waiting = s->phase == SOMVR_CLIMB_PH_CMD;
    /* lets: the grip opened, or the tracking gone too long (not while lifting or waiting: the top is ours then) */
    for (int h = 0; h < 2 && !lifting && !waiting; h++) {
        somvr_climb_hold *k = &s->h[h];
        if (!k->holding) continue;
        if (!k->down) {
            let_go(k);
            s->lets++;
            o->events |= SOMVR_CLIMB_E_LET_L << h;
            continue;
        }
        if (in->hand[h].valid) {
            k->lost_t = -1;
        } else if (k->lost_t < 0) {
            k->lost_t = in->t, k->have_last = 0;
        } else if (in->t - k->lost_t > c->lost_s) {
            let_go(k);
            s->lost++;
            o->events |= SOMVR_CLIMB_E_LOST_L << h;
        }
    }
    /* grabs: a new press (armed) starts looking; it looks while inside its window */
    for (int h = 0; h < 2 && !lifting && !waiting; h++) {
        somvr_climb_hold *k = &s->h[h];
        const somvr_climb_hand *hd = &in->hand[h];
        if (k->holding || !k->down || !k->armed) continue;
        if (in->nograb) { /* the press is someone else's (the bow): spent, open the grip to try again */
            k->looking = 0, k->armed = 0;
            continue;
        }
        if (!was_down[h]) k->looking = 1, k->press_t = in->t, k->near_past = -1, k->near_via = 0;
        if (!k->looking) continue;
        if (in->t - k->press_t > c->window_s) {
            k->looking = 0, k->armed = 0; /* missed: open the grip to try again */
            s->misses++;
            o->events |= SOMVR_CLIMB_E_MISS_L << h;
            o->miss_near[h] = s->active || k->near_past >= 0 || k->cue_in;
            continue;
        }
        if (!hd->valid || !in->allow) continue;
        int g = grab_test(s, c, in, h, ray, ctx, o);
        if (g > 0 && !s->active && c->low_cm > 0 && in->root_ok && k->hit.y < in->root.y - c->low_cm) {
            k->looking = 0, k->armed = 0, k->edge = 0; /* the ground under him: no climb from it */
            s->lows++;
            o->events |= SOMVR_CLIMB_E_LOW_L << h;
        } else if (g < 0) {
            k->looking = 0, k->armed = 0;
            s->noclimb++;
            o->events |= SOMVR_CLIMB_E_NOCLIMB_L << h;
        } else if (g > 0) {
            k->holding = 1, k->looking = 0, k->seq = ++s->seq;
            k->last_p = hd->p, k->have_last = 1, k->lost_t = -1;
            k->cue_in = 1; /* at the wall: no reach cue until the hand has left it */
            s->grabs++;
            o->events |= SOMVR_CLIMB_E_GRAB_L << h;
        }
    }
    if (!lifting && !waiting)
        for (int h = 0; h < 2; h++) cue_step(s, c, in, h, ray, ctx, o);
    int any = s->h[0].holding || s->h[1].holding;
    if (!s->active && any) {
        s->active = 1, s->phase = SOMVR_CLIMB_PH_HOLD, s->t0 = in->t, s->carry = (sv_vec3){ 0, 0, 0 };
        s->driver = -1;
        s->climbs++;
        o->started = 1;
    }
    if (!s->active) {
        o->active = 0;
        return;
    }
    if (lifting) {
        lift_step(s, c, in, o);
    } else if (waiting) {
        if (in->t - s->phase_t > c->cmd_s) end_climb(s, SOMVR_CLIMB_END_CMD, 1, o);
    } else if (!any && hangs && !on_ground(s, c, in, ray, ctx, o)) {
        hang(s, in, 0, o); /* both hands open: Talion stays where he is (the grips are armed: open hands may grab) */
    } else if (!any) {
        end_climb(s, SOMVR_CLIMB_END_RELEASED, 0, o);
    } else {
        if (s->phase == SOMVR_CLIMB_PH_HANG) { /* a new hold while hanging: the climb goes on */
            s->phase = SOMVR_CLIMB_PH_HOLD;
            s->resumes++;
            o->events |= SOMVR_CLIMB_E_RESUME;
        }
        /* the newest hold drives */
        int d = newest(s);
        somvr_climb_hold *k = &s->h[d];
        const somvr_climb_hand *hd = &in->hand[d];
        s->driver = d; /* a new driver goes on from its own last position (kept while it held) */
        if (hd->valid && k->have_last) {
            sv_vec3 dp = sub3(hd->p, k->last_p);
            if (!fin3(dp) || len3(dp) > c->jump_cm) {
                s->jumps++;
                o->events |= SOMVR_CLIMB_E_JUMP;
            } else {
                float ks = in->rig_scale >= 0.5f && in->rig_scale <= 2.0f ? in->rig_scale : 1.0f; /* the player size */
                s->carry = add3(s->carry, mul3(somvr_climb_to_world(dp, in->rig_yaw), -ks));
            }
        }
        sv_vec3 m = somvr_climb_clamp(s->carry, c->step_cm);
        s->carry = sub3(s->carry, m);
        if (len3(s->carry) > c->carry_cm) {
            s->carry = somvr_climb_clamp(s->carry, c->carry_cm);
            s->cuts++;
            o->events |= SOMVR_CLIMB_E_CARRY_CUT;
        }
        /* push's edge step: the last move's forward part (toward the wall) that the collision did not apply lifts the
         * body by ramp x that part, so a push over the edge goes up and over at the hand's speed (Talion's capsule
         * steps over only from 40 cm under the top) */
        if (s->topped && c->ramp > 0 && in->root_ok && s->have_root && in->head_ok && in->head_y < s->top_y + c->lift_cm) {
            sv_vec3 ap = sub3(in->root, s->last_root);
            float af = s->last_req.x * s->top_toward.x + s->last_req.z * s->top_toward.z;
            float gf = ap.x * s->top_toward.x + ap.z * s->top_toward.z;
            if (af > 0.2f && gf < 0.5f * af) {
                float up = fminf(c->ramp * (af - fmaxf(gf, 0.0f)), c->step_cm);
                m.y += up, s->ramp_cm += up, o->ramp = up;
            }
        }
        request(s, o, m);
        for (int h = 0; h < 2; h++) /* every holding hand's last position, for its turn as the driver */
            if (s->h[h].holding && in->hand[h].valid) s->h[h].last_p = in->hand[h].p, s->h[h].have_last = 1;
        /* push: over the top = the head past the top hold's point toward the wall: standing on it */
        if (s->topped && in->head_ok &&
            (in->head_x - s->top_hit.x) * s->top_toward.x + (in->head_z - s->top_hit.z) * s->top_toward.z >= c->over_cm) {
            end_climb(s, SOMVR_CLIMB_END_OVER, 1, o);
            o->active = 0, o->driver = -1;
            return;
        }
        /* the top: an edge hold with the head above it, or the other hand reaching over it (within 1 m of the hold) */
        for (int h = 0; h < 2 && !s->topped; h++) {
            const somvr_climb_hold *e = &s->h[h];
            if (!e->holding || !e->edge) continue;
            int by = in->head_ok && in->head_y >= e->top_y + c->top_cm ? 1 : 0;
            const somvr_climb_hand *oh = &in->hand[1 - h];
            float dx = oh->pw.x - e->hit.x, dz = oh->pw.z - e->hit.z;
            if (!by && oh->valid && oh->pw.y >= e->top_y + c->top_cm && dx * dx + dz * dz < 100.0f * 100.0f) by = 2;
            if (by) {
                o->top_by = by;
                top_reached(s, c, in, e, o);
                break;
            }
        }
    }
    o->active = s->active, o->driver = s->driver;
}
