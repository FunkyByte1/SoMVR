/* somvr_climb.c - see somvr_climb.h */
#include "somvr_climb.h"

#include <math.h>
#include <string.h>

static const char *const g_end_names[SOMVR_CLIMB_NEND] = { "none", "released", "game", "input", "timeout", "top", "lifted",
                                                           "lift-stall", "cmd", "outside", "over", "drop", "blink" };
static const char *const g_phase_names[7] = { "none", "hold", "lift", "cmd", "hang", "fling", "blink" };

const char *somvr_climb_end_name(int why) { return why >= 0 && why < SOMVR_CLIMB_NEND ? g_end_names[why] : "?"; }
const char *somvr_climb_phase_name(int phase) { return phase >= 0 && phase < 7 ? g_phase_names[phase] : "?"; }
const char *somvr_climb_bl_name(int why) {
    static const char *const n[6] = { "-", "a short drop: the fall", "no ground under him: he stays",
                                      "water under him: he stays", "no blink now: the fall", "no edge ahead" };
    return why >= 0 && why < 6 ? n[why] : "?";
}
const char *somvr_climb_fl_name(int why) {
    static const char *const n[7] = { "-", "the arc's top", "blocked above", "out of time", "a catch", "over",
                                      "blocked at the edge" };
    return why >= 0 && why < 7 ? n[why] : "?";
}
const char *somvr_climb_via_name(int via) {
    static const char *const n[6] = { "-", "palm", "fingers", "down", "inside", "wall" };
    return via >= 0 && via < 6 ? n[via] : "?";
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
    c->fling = 0, c->fling_min = 150, c->fling_gain = 1, c->fling_max = 500, c->fling_side = 0.5f, c->fling_g = 981;
    c->fling_win_s = 0.1f, c->fling_s = 1.5f;
    c->descent = 0, c->lip_drop_cm = 150, c->lip_find_cm = 60, c->foot_cm = 102, c->lip_ramp = 2;
    c->blink_cm = 400, c->blink_max_cm = 8000, c->blink_out_cm = 40, c->blink_wait_s = 3, c->edge_look_cm = 400;
    c->overhang = 0, c->overhang_cm = 40;
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

int somvr_climb_surface_wet(const char *name) {
    const char *n = name ? name : "";
    return has_ci(n, "water", 1) || has_ci(n, "liquid", 1) || has_ci(n, "puddle", 0);
}

int somvr_climb_surface_clear(const char *name) {
    const char *n = name ? name : "";
    return has_ci(n, "air", 1) && (n[3] == 0 || n[3] == '_') ? 1 : has_ci(n, "invisible", 1) || has_ci(n, "playerblocker", 1);
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

/* fling: the driving hand's ring of tracking-space positions at the time they were located for (reset when it starts
 * to drive and at a jump) */
static double pose_t(const somvr_climb_in *in) { return in->pose_t > 0 ? in->pose_t : in->t; }
static void vh_reset(somvr_climb_hold *k, double t, sv_vec3 p, int valid) {
    k->vh_n = 0, k->vh_i = 0;
    if (valid && fin3(p)) k->vh_p[0] = p, k->vh_t[0] = t, k->vh_n = 1, k->vh_i = 1;
}
static void vh_push(somvr_climb_hold *k, double t, sv_vec3 p) {
    if (!fin3(p)) return;
    k->vh_p[k->vh_i] = p, k->vh_t[k->vh_i] = t;
    k->vh_i = (k->vh_i + 1) % SOMVR_CLIMB_VH_N;
    if (k->vh_n < SOMVR_CLIMB_VH_N) k->vh_n++;
}
/* the mean velocity (tracking cm/s) from the oldest sample within win x 1.25 of the newest to the newest (the newest:
 * now's p when given, else the ring's own); 0 = under 40 ms of samples */
static int vh_mean(const somvr_climb_hold *k, double t, const sv_vec3 *now, float win, sv_vec3 *v, float *span, int *n) {
    if (k->vh_n < 1) return 0;
    int last = (k->vh_i + SOMVR_CLIMB_VH_N - 1) % SOMVR_CLIMB_VH_N;
    sv_vec3 pn = now ? *now : k->vh_p[last];
    double tn = now ? t : k->vh_t[last];
    int oi = -1, cnt = now ? 1 : 0;
    for (int j = 0; j < k->vh_n; j++) {
        int i = (k->vh_i + SOMVR_CLIMB_VH_N - 1 - j) % SOMVR_CLIMB_VH_N; /* newest first */
        double a = tn - k->vh_t[i];
        if (a < 0 || a > 1.25 * win) break;
        oi = i, cnt++;
    }
    if (oi < 0 || !(tn - k->vh_t[oi] >= 0.04)) return 0;
    float sp = (float)(tn - k->vh_t[oi]);
    *v = mul3(sub3(pn, k->vh_p[oi]), 1.0f / sp), *span = sp, *n = cnt;
    return fin3(*v);
}

int somvr_climb_fling_rule(somvr_climb *s, const somvr_climb_cfg *c, sv_vec3 vw, sv_vec3 toward, sv_vec3 *v0) {
    sv_vec3 tw = { toward.x, 0, toward.z };
    if (len3(tw) < 0.5f) tw = (sv_vec3){ 0, 0, 0 }; else tw = unit3(tw);
    if (!fin3(vw)) vw = (sv_vec3){ 0, 0, 0 };
    float up = vw.y, into = vw.x * tw.x + vw.z * tw.z;
    sv_vec3 side = { vw.x - into * tw.x, 0, vw.z - into * tw.z };
    if (len3(tw) < 0.5f) side = (sv_vec3){ 0, 0, 0 }, into = 0; /* no wall direction: only the upward part */
    s->fl_up = up, s->fl_into = into, s->fl_out = into < 0 ? -into : 0, s->fl_capped = 0;
    s->fl_side = len3(side);
    *v0 = (sv_vec3){ 0, 0, 0 };
    if (!(up >= c->fling_min) || !(up > 0)) return 0;
    side = somvr_climb_clamp(mul3(side, c->fling_side), 0.5f * up);
    float fwd = into > 0 ? fminf(into, up) : 0;
    sv_vec3 v = { side.x + fwd * tw.x, up, side.z + fwd * tw.z };
    if (len3(v) > c->fling_max && c->fling_max > 0) v = somvr_climb_clamp(v, c->fling_max), s->fl_capped = 1;
    s->fl_side = len3(side);
    *v0 = v;
    return 1;
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
    sv_vec3 dirs[4];
    int vias[4], nd = 0;
    if (c->rays & SOMVR_CLIMB_RAY_PALM) dirs[nd] = unit3(hd->palm), vias[nd++] = SOMVR_CLIMB_VIA_PALM;
    if (c->rays & SOMVR_CLIMB_RAY_FWD) dirs[nd] = unit3(hd->fwd), vias[nd++] = SOMVR_CLIMB_VIA_FWD;
    /* descent, not climbing: a hand under the root (reaching down at an edge) also down and along the wall; a raised
     * hand starts a climb as before (run 024500: a hand over a low top took the top, not the face) */
    int low = c->descent && !s->active && in->root_ok && hd->pw.y < in->root.y;
    if (c->down_assist && (s->active || low)) dirs[nd] = (sv_vec3){ 0, -1, 0 }, vias[nd++] = SOMVR_CLIMB_VIA_DOWN;
    if (c->descent && (s->active || low)) { /* along the wall: the climb's (its last hold's toward), else level from
                                             * the hand to the head */
        sv_vec3 wd = s->active ? s->wall_tw : (sv_vec3){ 0, 0, 0 };
        sv_vec3 hh = { in->head_x - hd->pw.x, 0, in->head_z - hd->pw.z };
        if (len3(wd) < 0.5f && in->head_ok && len3(hh) > 15.0f) wd = unit3(hh); /* the hand out past the head */
        if (len3(wd) > 0.5f) dirs[nd] = wd, vias[nd++] = SOMVR_CLIMB_VIA_WALL;
    }
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

/* descent: the face's own out (horizontal, away from the wall) at `at` (a point on the face line, ou the guess): two
 * level rays back from 30 cm out, 15 cm to each side; both hit = the perpendicular of the line through them (on ou's
 * side), else ou */
static sv_vec3 face_out(somvr_climb *s, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray, void *ctx,
                        somvr_climb_out *o, sv_vec3 at, sv_vec3 ou) {
    (void)in;
    sv_vec3 side = { -ou.z, 0, ou.x }, back = mul3(ou, -1.0f), f[2];
    somvr_climb_hit r;
    for (int i = 0; i < 2; i++) {
        sv_vec3 from = add3(add3(at, mul3(ou, 30.0f)), mul3(side, i ? 15.0f : -15.0f));
        if (!cast(s, o, ray, ctx, h, from, back, 60.0f, &r) || !r.hit) return ou;
        f[i] = r.point;
    }
    sv_vec3 tg = { f[1].x - f[0].x, 0, f[1].z - f[0].z }, n = { -tg.z, 0, tg.x };
    if (len3(n) < 5.0f) return ou;
    n = unit3(n);
    if (n.x * ou.x + n.z * ou.z < 0) n = mul3(n, -1.0f);
    return n.x * ou.x + n.z * ou.z > 0.3f ? n : ou; /* not a face across the reach: the guess */
}

/* descent: is the hold at bh (found along dir) a lip: a top's edge within lip_find_cm along the reach (a hold on a top:
 * the ray down, or its surface level with the hand's ray), or a face (a level ray), with the ground beyond the edge at
 * least lip_drop_cm under the hold (a ray down from 30 cm out; none within blink_max_cm counts as a drop). 1 = a lip
 * (*top_y, *edge, *out: away from the wall, *drop). A top: only the one Talion stands on (his feet within 40 cm of it) */
static int lip_test(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                    void *ctx, somvr_climb_out *o, const somvr_climb_hit *bh, sv_vec3 dir, float *top_y, sv_vec3 *edge,
                    sv_vec3 *out, float *drop) {
    const somvr_climb_hand *hd = &in->hand[h];
    sv_vec3 p = bh->point, ou, beyond;
    somvr_climb_hit r;
    float ty = p.y;
    if (fabsf(dir.y) > 0.7f) { /* a top from above: its edge along the reach (the head to the hold, level) */
        if (!in->root_ok || fabsf(in->root.y - c->foot_cm - p.y) > 40.0f)
            return 0; /* not the top he stands on (a top reached from below or beside it: run 022824's low wall) */
        ou = in->head_ok ? (sv_vec3){ p.x - in->head_x, 0, p.z - in->head_z } : (sv_vec3){ 0, 0, 0 };
        if (len3(ou) < 5.0f) ou = (sv_vec3){ hd->fwd.x, 0, hd->fwd.z };
        if (len3(ou) < 0.2f) return 0;
        ou = unit3(ou);
        float e = -1;
        for (float d = 10; d <= c->lip_find_cm + 0.5f; d += 10) {
            sv_vec3 f = add3(add3(p, mul3(ou, d)), (sv_vec3){ 0, 20, 0 });
            if (!cast(s, o, ray, ctx, h, f, (sv_vec3){ 0, -1, 0 }, 40, &r)) return 0;
            if (!r.hit || r.point.y < p.y - 15.0f) {
                e = d;
                break;
            }
        }
        if (e < 0) return 0; /* the top goes on: the floor, not a lip */
        *edge = add3(p, mul3(ou, e - 5.0f));
        ou = face_out(s, in, h, ray, ctx, o, add3(*edge, (sv_vec3){ 0, -20, 0 }), ou); /* the face under the edge */
        beyond = add3(*edge, mul3(ou, 35.0f));
    } else { /* a face: out = back along the ray (level) */
        ou = (sv_vec3){ -dir.x, 0, -dir.z };
        if (len3(ou) < 0.5f) return 0;
        ou = face_out(s, in, h, ray, ctx, o, p, unit3(ou));
        *edge = p;
        beyond = add3(p, mul3(ou, 30.0f));
    }
    beyond.y = p.y + 5.0f;
    if (!cast(s, o, ray, ctx, h, beyond, (sv_vec3){ 0, -1, 0 }, c->blink_max_cm, &r)) return 0;
    float dr = r.hit ? p.y - r.point.y : c->blink_max_cm;
    if (!(dr >= c->lip_drop_cm)) return 0;
    *top_y = ty, *out = ou, *drop = dr;
    return 1;
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
    /* descent: a lip (its toward into the top: over it = back onto the top) */
    k->lip = c->descent && lip_test(s, c, in, h, ray, ctx, o, &bh, bdir, &k->lip_top_y, &k->lip_edge, &k->lip_out,
                                    &k->lip_drop);
    if (k->lip) k->toward = mul3(k->lip_out, -1.0f);
    if (k->lip && fabsf(bdir.y) <= 0.7f && k->edge && k->top_found) k->lip_top_y = k->top_y; /* a face: its top */
    return 1;
}

/* the reach cue of hand h (cue): an open hand (seen open, not holding, not squeezed), raised over the root unless a
 * climb is on, probed every cue_s; E_NEAR on its way into the reach of a surface it could hold (out of reach at least
 * cue_off_s before) */
static void cue_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                     void *ctx, somvr_climb_out *o) {
    somvr_climb_hold *k = &s->h[h];
    const somvr_climb_hand *hd = &in->hand[h];
    int raised = in->root_ok && hd->pw.y >= in->root.y + c->cue_raise_cm;
    int ok = c->cue && ray && in->live && in->allow && !in->nograb && hd->valid && hd->buttons && !k->holding &&
             !k->down && (s->active || raised || (c->descent && in->root_ok)); /* descent: a low hand, for a lip */
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
    if (near && !s->active && !raised) { /* descent: a low hand ticks only at a lip it could start down from */
        float ty, dr;
        sv_vec3 e, ou;
        near = bh.point.y < in->root.y - c->low_cm && lip_test(s, c, in, h, ray, ctx, o, &bh, d, &ty, &e, &ou, &dr) &&
               in->root.y - c->foot_cm >= ty - 30.0f;
    }
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
    s->descending = 0;
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

/* push's edge step: the last move's forward part (toward the wall) that the collision did not apply lifts the body by
 * ramp x that part, so a push over the edge goes up and over at the hand's speed (Talion's capsule steps over only
 * from 40 cm under the top); 0 = none this frame */
static float edge_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_out *o,
                       float cap) {
    if (!s->topped || !(c->ramp > 0) || !in->root_ok || !s->have_root || !in->head_ok || in->head_y >= s->top_y + c->lift_cm)
        return 0;
    sv_vec3 ap = sub3(in->root, s->last_root);
    float af = s->last_req.x * s->top_toward.x + s->last_req.z * s->top_toward.z;
    float gf = ap.x * s->top_toward.x + ap.z * s->top_toward.z;
    if (!(af > 0.2f && gf < 0.5f * af)) return 0;
    float up = fminf(c->ramp * (af - fmaxf(gf, 0.0f)), cap);
    s->ramp_cm += up, o->ramp = up;
    return up;
}

/* push: over the top = the head past the top hold's point toward the wall by over_cm: standing on it */
static int over_top(const somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in) {
    return s->topped && in->head_ok &&
           (in->head_x - s->top_hit.x) * s->top_toward.x + (in->head_z - s->top_hit.z) * s->top_toward.z >= c->over_cm;
}

/* fling: the driving hand h lets go: its release velocity and the hold's wall (before let_go clears the hold);
 * 1 = a fling (s->fl_v0 etc.; launched when no hand holds after the lets), 0 = under fling_min (the parts in s->fl_*),
 * -1 = no velocity (under 40 ms of samples) */
static int fl_release(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h) {
    somvr_climb_hold *k = &s->h[h];
    const somvr_climb_hand *hd = &in->hand[h];
    sv_vec3 vh;
    float span = 0;
    int n = 0, last = (k->vh_i + SOMVR_CLIMB_VH_N - 1) % SOMVR_CLIMB_VH_N;
    /* now's position counts unless it jumped from the ring's newest (a glitch: the ring alone) */
    int now_ok = hd->valid && k->vh_n > 0 && fin3(hd->p) && len3(sub3(hd->p, k->vh_p[last])) <= c->jump_cm;
    s->fl_hand = h, s->fl_n = k->vh_n, s->fl_span = 0, s->fl_up = s->fl_side = s->fl_into = s->fl_out = 0;
    if (!vh_mean(k, pose_t(in), now_ok ? &hd->p : NULL, c->fling_win_s, &vh, &span, &n)) return -1;
    float ks = in->rig_scale >= 0.5f && in->rig_scale <= 2.0f ? in->rig_scale : 1.0f;
    sv_vec3 vw = mul3(somvr_climb_to_world(vh, in->rig_yaw), -ks * c->fling_gain);
    s->fl_hand = h, s->fl_hand_v = vh, s->fl_span = span, s->fl_n = n;
    s->fl_toward = k->toward;
    s->fl_edge = k->edge && k->top_found, s->fl_top_y = k->top_y, s->fl_hit = k->hit;
    return somvr_climb_fling_rule(s, c, vw, k->toward, &s->fl_v0);
}

/* fling: the launch (no hand holds): the phase fling; the head's ray along toward measures the wall for the top in
 * flight (unless the top is known: push topped, or the release hold's found top) */
static void fl_launch(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *o) {
    s->phase = SOMVR_CLIMB_PH_FLING, s->phase_t = in->t;
    s->fl_v = s->fl_v0, s->fl_t0 = in->t, s->fl_frames = 0, s->fl_blocked = 0, s->fl_why = SOMVR_CLIMB_FL_NONE;
    s->fl_glide = 0;
    s->fl_root0 = in->root, s->fl_root_ok = in->root_ok, s->fl_top_by = 0, s->fl_wall_d = -1;
    s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
    s->flings++;
    o->events |= SOMVR_CLIMB_E_FLING;
    somvr_climb_hit r;
    sv_vec3 head = { in->head_x, in->head_y, in->head_z }, tw = s->fl_toward;
    if (!s->topped && !s->fl_edge && c->top_mode == SOMVR_CLIMB_TOP_PUSH && in->head_ok && len3(tw) > 0.5f &&
        cast(s, o, ray, ctx, s->fl_hand, head, tw, 60, &r)) {
        if (r.hit) s->fl_wall_d = r.dist;
        else /* the eyes over the wall already: the face where the hold is (fl_top looks down past it this frame) */
            s->fl_wall_d = fmaxf((s->fl_hit.x - head.x) * tw.x + (s->fl_hit.z - head.z) * tw.z, 0.0f);
    }
}

/* fling: the top in flight (push only): the release hold's found top, or the head's ray along toward losing the wall
 * (then a ray down 10 cm past the face, at most 100 cm under the head, gives the top); the head top_cm over it =
 * topped (as top_reached's push) */
static void fl_top(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                   somvr_climb_out *o) {
    if (s->topped || c->top_mode != SOMVR_CLIMB_TOP_PUSH || !in->head_ok) return;
    sv_vec3 head = { in->head_x, in->head_y, in->head_z }, tw = s->fl_toward;
    int by = 0;
    if (s->fl_edge) {
        if (in->head_y >= s->fl_top_y + c->top_cm) {
            s->top_hit = s->fl_hit, s->top_y = s->fl_top_y, by = 1;
        }
    } else if (s->fl_wall_d >= 0) {
        somvr_climb_hit r, d;
        float far = s->fl_wall_d + c->edge_far_cm;
        if (!cast(s, o, ray, ctx, s->fl_hand, head, tw, far, &r)) return;
        if (r.hit && r.dist <= far) {
            s->fl_wall_d = r.dist; /* the face as it goes on (a slope, a step back) */
            return;
        }
        sv_vec3 from = add3(head, mul3(tw, s->fl_wall_d + 10.0f));
        if (!cast(s, o, ray, ctx, s->fl_hand, from, (sv_vec3){ 0, -1, 0 }, 100, &d) || !d.hit || d.point.y > in->head_y) {
            s->fl_wall_d = -1; /* the wall ended without a top under the eyes (beside it, a drop): stop looking */
            return;
        }
        if (in->head_y < d.point.y + c->top_cm) return; /* the eyes just over it: again next frame */
        s->top_hit = (sv_vec3){ head.x + tw.x * s->fl_wall_d, d.point.y, head.z + tw.z * s->fl_wall_d };
        s->top_y = d.point.y, by = 2;
    }
    if (!by) return;
    s->topped = 1, s->top_toward = tw, s->fl_top_by = by;
    s->tops++, s->fl_tops++;
    o->events |= SOMVR_CLIMB_E_FLING_TOP;
}

/* fling: one frame of the flight (no hand holds): the last frame's piece against the root (blocked above twice = a
 * ceiling: the apex; blocked sideways: that part stops), this frame's piece of the parabola, the top, the edge step
 * while rising, over, the apex (the hang). Topped at the arc's top with speed toward the wall left: the glide, that
 * speed alone (level, gravity still off) until over (the head past the top's hold, or the top under his root within
 * ground_cm: he stands on it) or FL_GLIDE_S; the edge blocking it lifts him by the blocked part (the edge step, as a
 * push: the capsule's foot onto the top) up to FL_GLIDE_LIFT_CM in all, then the hang */
#define FL_GLIDE_S 0.5
#define FL_GLIDE_LIFT_CM 50.0f
#define FL_GLIDE_LIFT_MAX_CM 80.0f /* the lift's cap when the capsule's foot is further under the top */
/* the glide's time is the part not blocked by the edge (a slow frame's lift used up the 0.5 s: the integration's runs
 * 034000, 030713: the foot level with the top at the end, out of time), at most 3 x FL_GLIDE_S in all */
static void fl_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                    void *ctx, somvr_climb_out *o, double dt) {
    int stop = SOMVR_CLIMB_FL_NONE, glide_blocked = 0;
    sv_vec3 tw = s->fl_toward;
    float into = s->fl_v.x * tw.x + s->fl_v.z * tw.z;
    if (s->fl_frames++ > 0 && in->root_ok && s->have_root) {
        sv_vec3 ap = sub3(in->root, s->last_root), rq = s->last_req;
        float rq_up = rq.y - s->last_ramp; /* the edge step's lift is not the arc's */
        if (s->fl_glide) {
            float af = rq.x * tw.x + rq.z * tw.z, gf = ap.x * tw.x + ap.z * tw.z;
            glide_blocked = af > 0.2f && gf < 0.5f * af; /* the capsule's foot not over the edge yet */
        } else if (rq_up > 0.5f && ap.y < 0.5f * rq.y) {
            if (++s->fl_blocked >= 2) stop = SOMVR_CLIMB_FL_BLOCKED;
        } else {
            s->fl_blocked = 0;
        }
        sv_vec3 sd = { s->fl_v.x - into * tw.x, 0, s->fl_v.z - into * tw.z };
        float sl = len3(sd);
        if (sl > 1.0f) {
            sd = mul3(sd, 1.0f / sl);
            float asd = rq.x * sd.x + rq.z * sd.z, gsd = ap.x * sd.x + ap.z * sd.z;
            if (asd > 0.5f && gsd < 0.5f * asd) s->fl_v.x = into * tw.x, s->fl_v.z = into * tw.z; /* a side wall */
        }
    }
    float g = c->fling_g > 0 ? c->fling_g : 981.0f, d = (float)(dt < 0 ? 0 : dt > 0.1 ? 0.1 : dt);
    /* the edge step's cap a frame: step_cm per 50 ms at least (a slow frame lifts as 20 Hz's would: the integration's
     * runs 030713 / 031623, a dump's 9-12 fps flight lifted 3 cm in the rise and the glide's 50 cm took all its 0.5 s) */
    float ecap = fmaxf(c->step_cm, c->step_cm * d / 0.05f);
    float vy0 = s->fl_v.y, vy1 = vy0 - g * d;
    sv_vec3 m = { 0, 0, 0 };
    int glide = s->fl_glide;
    if (stop == SOMVR_CLIMB_FL_NONE && glide) {
        m = (sv_vec3){ s->fl_v.x * d, 0, s->fl_v.z * d };
        if (!glide_blocked) s->fl_glide_run += d;
        if (s->fl_glide_run > FL_GLIDE_S || in->t - s->fl_glide_t > 3 * FL_GLIDE_S) stop = SOMVR_CLIMB_FL_TIME;
        if (glide_blocked) { /* the edge step: the blocked part lifts (at most fl_glide_cap in the glide) */
            double r0 = s->ramp_cm;
            float up = s->fl_glide_lift < s->fl_glide_cap ? edge_step(s, c, in, o, ecap) : 0;
            up = fminf(up, s->fl_glide_cap - s->fl_glide_lift);
            if (up > 0) {
                s->ramp_cm = r0 + up, o->ramp = up, s->fl_glide_lift += up, m.y += up;
            } else {
                s->ramp_cm = r0, o->ramp = 0;
                stop = SOMVR_CLIMB_FL_EDGE, m = (sv_vec3){ 0, 0, 0 };
            }
        }
    } else if (stop == SOMVR_CLIMB_FL_NONE) {
        if (vy1 <= 0) { /* the arc's top inside this frame: up to it */
            float ta = vy0 > 0 ? vy0 / g : 0;
            m = (sv_vec3){ s->fl_v.x * ta, 0.5f * vy0 * ta, s->fl_v.z * ta };
            s->fl_v.y = 0;
            if (s->topped && into > 20.0f) { /* over a top, still going toward it: on level to over (or the edge) */
                s->fl_glide = 1, s->fl_glide_t = in->t, s->fl_glide_lift = 0, s->fl_glide_run = 0, s->fl_glides++;
                /* the lift's cap: the capsule's foot (foot_cm under the root, after this frame's rise) 10 cm over the top */
                float gap = in->root_ok ? s->top_y + 10.0f - (in->root.y + m.y - c->foot_cm) : 0;
                s->fl_glide_cap = fminf(fmaxf(gap, FL_GLIDE_LIFT_CM), FL_GLIDE_LIFT_MAX_CM);
                o->events |= SOMVR_CLIMB_E_GLIDE;
                s->fl_v = mul3(tw, into);
            } else {
                stop = SOMVR_CLIMB_FL_APEX;
            }
        } else {
            m = (sv_vec3){ s->fl_v.x * d, 0.5f * (vy0 + vy1) * d, s->fl_v.z * d };
            s->fl_v.y = vy1;
        }
        if (stop == SOMVR_CLIMB_FL_NONE && in->t - s->fl_t0 > c->fling_s) stop = SOMVR_CLIMB_FL_TIME;
    }
    if (!glide) {
        fl_top(s, c, in, ray, ctx, o);
        if (stop == SOMVR_CLIMB_FL_NONE || stop == SOMVR_CLIMB_FL_APEX) m.y += edge_step(s, c, in, o, ecap);
    }
    float cap = (c->fling_max > 0 ? c->fling_max : 500.0f) * 0.1f;
    request(s, o, somvr_climb_clamp(m, fmaxf(cap, c->step_cm)));
    /* the glide: the top under his root (a ray down from it meets the top within ground_cm) = he stands on it */
    somvr_climb_hit gr;
    int landed = glide && stop == SOMVR_CLIMB_FL_NONE && in->root_ok && c->ground_cm > 0 &&
                 cast(s, o, ray, ctx, s->fl_hand, in->root, (sv_vec3){ 0, -1, 0 }, c->ground_cm, &gr) && gr.hit &&
                 gr.point.y >= s->top_y - 15.0f;
    if (landed || over_top(s, c, in)) {
        s->fl_why = SOMVR_CLIMB_FL_OVER, s->fl_overs++, s->fl_landed = landed;
        end_climb(s, SOMVR_CLIMB_END_OVER, 1, o);
        o->active = 0, o->driver = -1;
        return;
    }
    if (stop != SOMVR_CLIMB_FL_NONE) {
        s->fl_why = stop, s->apexes++;
        if (stop == SOMVR_CLIMB_FL_BLOCKED) s->fl_blocks++;
        s->fl_v = (sv_vec3){ 0, 0, 0 };
        hang(s, in, 0, o);
        o->events |= SOMVR_CLIMB_E_APEX;
    }
}

/* descent: the lip step, on this frame's request *m: descending with the feet still on the lip's top: until the root
 * is LIP_CLEAR_CM past the edge (the capsule clears it) the request's downward part goes out over the edge instead
 * (lip_out, lip_ramp x it, at most step_cm a frame); past it, a downward part the top did not apply last frame does the
 * same. The outward part not applied LIP_BLOCK_N frames in a row = the game will not let him over this edge (its
 * invisible edge walls: the forge tower's 'Air', the meadow bank's, run 015257, where pushing on lifted him 33 cm):
 * lip_blocked, no more lip step (E_LIP_BLOCKED; A is the way down there) */
#define LIP_CLEAR_CM 40.0f
#define LIP_BLOCK_N 3
static void lip_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_out *o, sv_vec3 *m) {
    if (!s->descending || s->lip_blocked || !(c->lip_ramp > 0) || !in->root_ok || !s->have_root) return;
    if (in->root.y - c->foot_cm < s->lip_top_y - 10.0f) return; /* the feet off the top: the face */
    sv_vec3 ap = sub3(in->root, s->last_root), rq = s->last_req;
    float oq = rq.x * s->lip_out.x + rq.z * s->lip_out.z, og = ap.x * s->lip_out.x + ap.z * s->lip_out.z;
    if (oq > 0.5f && og < 0.25f * oq) {
        if (++s->lip_block_n >= LIP_BLOCK_N) {
            s->lip_blocked = 1, s->lip_blocks++;
            o->events2 |= SOMVR_CLIMB_E_LIP_BLOCKED;
            return;
        }
    } else if (oq > 0.5f) {
        s->lip_block_n = 0;
    }
    float past = (in->root.x - s->lip_edge.x) * s->lip_out.x + (in->root.z - s->lip_edge.z) * s->lip_out.z;
    float down = 0;
    if (past < LIP_CLEAR_CM) {
        down = fmaxf(-m->y, 0.0f);
        if (m->y < 0) m->y = 0;
    } else if (rq.y < -0.2f && ap.y > 0.5f * rq.y) {
        down = -rq.y - fmaxf(-ap.y, 0.0f);
    }
    if (!(down > 0)) return;
    float lp = fminf(c->lip_ramp * down, c->step_cm);
    *m = add3(*m, mul3(s->lip_out, lp));
    s->lip_cm += lp, s->lip_steps++, o->lip = lp;
}

/* overhang (see the header): an upward part of the last request the game did not apply (under half) while the driving
 * hold k is over the eyes: the blocked part x overhang goes out from the wall (k's toward, level, reversed), at most
 * step_cm a frame and overhang_cm under one lip (oh_cm; 0 again once the root is OH_RESET_CM over oh_y0) */
#define OH_RESET_CM 60.0f
#define OH_RETOP_CM 50.0f
#define OH_MANTLE_S 0.4f
#define OH_MANTLE_UP_CM 120.0f
/* overhang: the mantle (see the header): 1 = the blink onto k's top (s->bl_to), 0 = not (yet) */
static int mantle(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                  somvr_climb_ray_fn ray, void *ctx, somvr_climb_out *o, float dt) {
    float rq = s->last_req.y, ap = in->root.y - s->last_root.y;
    int top = k->edge && k->top_found && k->top_y - in->head_y > -10.0f && k->top_y - in->head_y < OH_MANTLE_UP_CM;
    if (!top || (rq > 0.3f && ap >= 0.5f * rq)) { /* no top over him, or the pull went through */
        s->oh_block_s = 0;
        return 0;
    }
    if (!(rq > 0.3f)) return 0; /* no pull now: the time waits */
    s->oh_block_s += dt > 0 && dt < 0.2f ? (float)dt : 0.0f;
    if (s->oh_block_s < OH_MANTLE_S || !in->can_blink) return 0;
    float n = sqrtf(k->toward.x * k->toward.x + k->toward.z * k->toward.z);
    sv_vec3 tw = n > 0.3f ? (sv_vec3){ k->toward.x / n, 0, k->toward.z / n } : (sv_vec3){ 0, 0, 0 };
    sv_vec3 from = { k->hit.x + tw.x * 40.0f, k->top_y + 60.0f, k->hit.z + tw.z * 40.0f };
    somvr_climb_hit f, u;
    s->oh_block_s = 0;
    if (!cast(s, o, ray, ctx, 0, from, (sv_vec3){ 0, -1, 0 }, 120.0f, &f) || !f.hit || f.wet ||
        fabsf(f.point.y - k->top_y) > 30.0f) {
        s->mantle_nofloor++;
        return 0;
    }
    sv_vec3 up0 = { f.point.x, f.point.y + 10.0f, f.point.z };
    if (!cast(s, o, ray, ctx, 0, up0, (sv_vec3){ 0, 1, 0 }, 180.0f, &u) || u.hit) {
        s->mantle_nofloor++;
        return 0;
    }
    s->mt_top = k->hit, s->mt_floor = f.point;
    s->bl_to = (sv_vec3){ f.point.x, f.point.y + c->foot_cm + 5.0f, f.point.z };
    return 1;
}

static void overhang_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                          somvr_climb_out *o, sv_vec3 *m) {
    if (!(c->overhang > 0) || s->descending || !in->root_ok || !s->have_root || !in->head_ok) return;
    if (s->oh_cm > 0 && in->root.y > s->oh_y0 + OH_RESET_CM) s->oh_cm = 0; /* past that lip: another may come */
    if (k->hit.y < in->head_y) return; /* the hold under the eyes: no lip over him to pull round */
    float n = sqrtf(k->toward.x * k->toward.x + k->toward.z * k->toward.z);
    if (n < 0.3f) return; /* no wall direction (a straight down ray's own: a hold on a top) */
    sv_vec3 out = { -k->toward.x / n, 0, -k->toward.z / n };
    float rq = s->last_req.y, ap = in->root.y - s->last_root.y;
    if (!(rq > 0.3f) || ap >= 0.5f * rq) return; /* no up asked, or it went through */
    float d = fminf(fminf(c->overhang * (rq - fmaxf(ap, 0.0f)), c->step_cm), c->overhang_cm - s->oh_cm);
    if (!(d > 0)) return;
    if (!(s->oh_cm > 0)) s->oh_y0 = in->root.y, s->oh_lips++, o->events2 |= SOMVR_CLIMB_E_OVERHANG;
    *m = add3(*m, mul3(out, d));
    s->oh_cm += d, s->oh_all_cm += d, s->oh_steps++, o->overhang = d;
}

/* descent: the drop button: the ground under him (from blink_out_cm out from the wall; still on a lip's top: from
 * beyond its edge). 1 = the blink (s->bl_to), 0 = the drop (a short one, no blink now, or no answer from the ray),
 * -1 = no drop at all (no ground within blink_max_cm, water); s->bl_why, bl_h */
static int blink_test(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *o) {
    s->bl_why = SOMVR_CLIMB_BL_NONE, s->bl_h = -1;
    if (!(c->blink_cm > 0) || !in->root_ok) return 0;
    float feet = in->root.y - c->foot_cm;
    sv_vec3 f, ou = { -s->wall_tw.x, 0, -s->wall_tw.z };
    if (s->descending && feet >= s->lip_top_y - 10.0f) { /* on the top: from beyond the edge */
        f = add3(s->lip_edge, mul3(s->lip_out, c->blink_out_cm)), f.y = s->lip_top_y + 30.0f;
        feet = s->lip_top_y;
    } else {
        f = len3(ou) > 0.5f ? add3(in->root, mul3(unit3(ou), c->blink_out_cm)) : in->root;
    }
    somvr_climb_hit g;
    s->bl_from = f;
    if (!cast(s, o, ray, ctx, 0, f, (sv_vec3){ 0, -1, 0 }, f.y - feet + c->blink_max_cm, &g)) {
        s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->bl_cannot++;
        return 0;
    }
    if (!g.hit) {
        s->bl_why = SOMVR_CLIMB_BL_NOGROUND, s->bl_noground++;
        return -1;
    }
    s->bl_ground = g.point, s->bl_h = feet - g.point.y;
    if (g.wet) {
        s->bl_why = SOMVR_CLIMB_BL_WET, s->bl_wet++;
        return -1;
    }
    if (!(s->bl_h > c->blink_cm)) {
        s->bl_why = SOMVR_CLIMB_BL_SHORT, s->bl_short++;
        return 0;
    }
    if (!in->can_blink) {
        s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->bl_cannot++;
        return 0;
    }
    s->bl_to = (sv_vec3){ g.point.x, g.point.y + c->foot_cm + 5.0f, g.point.z };
    return 1;
}

/* descent: A on foot: the edge ahead along the view's heading (where the floor ends: down rays every 20 cm from 60 cm
 * over the floor, a hit more than 30 cm under it or none; a hit 50 cm over it = a wall: no edge), then the ground under
 * it as blink_test. 1 = the blink (s->bl_to), 0 = none (bl_why: no edge, short, no blink now), -1 = no ground, water */
static int edge_blink(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *o) {
    s->bl_why = SOMVR_CLIMB_BL_NOEDGE, s->bl_h = -1;
    sv_vec3 f = { in->head_fwd.x, 0, in->head_fwd.z };
    if (!in->root_ok || len3(f) < 0.5f || !(c->blink_cm > 0) || !(c->edge_look_cm > 0)) return 0;
    f = unit3(f);
    float fl = in->root.y - c->foot_cm, e = -1;
    somvr_climb_hit r;
    for (float d = 20; d <= c->edge_look_cm + 0.5f; d += 20) {
        sv_vec3 from = add3(in->root, mul3(f, d));
        from.y = fl + 60.0f;
        if (!cast(s, o, ray, ctx, 0, from, (sv_vec3){ 0, -1, 0 }, 60.0f + c->lip_drop_cm, &r)) return 0;
        if (r.hit && r.point.y > fl + 50.0f) break; /* a wall ahead */
        if (!r.hit || r.point.y < fl - 30.0f) {
            e = d;
            break;
        }
    }
    if (e < 0) {
        s->bl_noedge++;
        return 0;
    }
    sv_vec3 from = add3(in->root, mul3(f, e + c->blink_out_cm));
    from.y = fl + 60.0f;
    somvr_climb_hit g;
    s->bl_from = from, s->bl_why = SOMVR_CLIMB_BL_NONE;
    if (!cast(s, o, ray, ctx, 0, from, (sv_vec3){ 0, -1, 0 }, 60.0f + c->blink_max_cm, &g)) {
        s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->bl_cannot++;
        return 0;
    }
    if (!g.hit) {
        s->bl_why = SOMVR_CLIMB_BL_NOGROUND, s->bl_noground++;
        return -1;
    }
    s->bl_ground = g.point, s->bl_h = fl - g.point.y;
    if (g.wet) {
        s->bl_why = SOMVR_CLIMB_BL_WET, s->bl_wet++;
        return -1;
    }
    if (!(s->bl_h > c->blink_cm)) {
        s->bl_why = SOMVR_CLIMB_BL_SHORT, s->bl_short++;
        return 0;
    }
    if (!in->can_blink) {
        s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->bl_cannot++;
        return 0;
    }
    s->bl_to = (sv_vec3){ g.point.x, g.point.y + c->foot_cm + 5.0f, g.point.z };
    return 1;
}

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
    s->last_ramp = o->move && o->active ? o->ramp : 0;
    s->last_root = in->root, s->have_root = in->root_ok && o->active;
}

static void step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                 somvr_climb_out *o) {
    memset(o, 0, sizeof *o);
    o->driver = -1;
    double dt = s->last_t > 0 && in->t > s->last_t ? in->t - s->last_t : 0;
    s->last_t = in->t;
    if (in->live && in->frozen) { /* a menu or the pause: hold everything, the clocks stop */
        s->t0 += dt, s->phase_t += dt, s->lift_rise_t += dt, s->fl_t0 += dt, s->fl_glide_t += dt;
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
    if (s->active && s->phase == SOMVR_CLIMB_PH_BLINK) { /* descent: still until the teleport (somvr_climb_stop) */
        if (!in->allow) {
            end_climb(s, SOMVR_CLIMB_END_GAME, 1, o);
            return;
        }
        if (in->t - s->phase_t > c->blink_wait_s) { /* it did not come: the hang where he is */
            s->bl_waits++;
            hang(s, in, 1, o);
        }
        o->active = s->active, o->driver = -1;
        return;
    }
    int hangs = c->letgo == SOMVR_CLIMB_LETGO_HANG &&
                (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG ||
                 s->phase == SOMVR_CLIMB_PH_FLING); /* not lift's or cmd's tops */
    /* stand down (letgo = hang: the input lost hangs, no timeout) */
    if (s->active && !in->live && hangs) hang(s, in, 1, o);
    else if (s->active && !in->live) end_climb(s, SOMVR_CLIMB_END_INPUT, 1, o);
    else if (s->active && !in->allow) end_climb(s, SOMVR_CLIMB_END_GAME, 1, o);
    else if (s->active && !hangs && in->t - s->t0 > c->max_s) end_climb(s, SOMVR_CLIMB_END_TIMEOUT, 1, o);
    else if (s->active && drop &&
             (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG || s->phase == SOMVR_CLIMB_PH_FLING)) {
        int b = c->descent ? blink_test(s, c, in, ray, ctx, o) : 0;
        if (b > 0) { /* descent: the blink to the ground; nothing moves until its teleport */
            for (int h = 0; h < 2; h++) let_go(&s->h[h]), s->h[h].armed = 0;
            s->phase = SOMVR_CLIMB_PH_BLINK, s->phase_t = in->t, s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
            s->blinks++;
            o->events |= SOMVR_CLIMB_E_BLINK;
            o->blink = 1, o->blink_to = s->bl_to;
            o->active = 1, o->driver = -1;
            return;
        }
        if (c->descent && s->bl_why) o->events |= SOMVR_CLIMB_E_BLINK_NONE;
        if (b == 0) {
            s->drops++; /* let go of the wall: the grips must open before a new hold */
            o->events |= SOMVR_CLIMB_E_DROP;
            end_climb(s, SOMVR_CLIMB_END_DROP, 1, o);
        }
    }
    if (o->ended) return;
    if (!s->active && drop && c->descent && in->allow && !in->nograb) { /* A on foot: the edge ahead, down */
        int b = edge_blink(s, c, in, ray, ctx, o);
        o->events |= SOMVR_CLIMB_E_EDGE;
        if (b > 0) {
            s->blinks++, s->edge_blinks++;
            o->events |= SOMVR_CLIMB_E_BLINK;
            o->blink = 1, o->blink_to = s->bl_to;
        } else {
            o->events |= SOMVR_CLIMB_E_BLINK_NONE;
        }
    }
    if (in->walk_blocked) s->walk_free_t = -1;
    else if (s->walk_free_t < 0) s->walk_free_t = in->t;
    if (s->edge_cued && !in->walk_blocked && s->walk_free_t >= 0 && in->t - s->walk_free_t >= 2.0)
        s->edge_cued = 0; /* not stopped for 2 s: the next stop may cue again */
    if (!s->active && c->descent && c->cue && in->walk_blocked && !s->edge_cued && in->live && in->allow && !in->nograb &&
        in->can_blink) { /* the cue: A here would blink down */
        int why = s->bl_why;
        float bh = s->bl_h;
        sv_vec3 bf = s->bl_from, bg = s->bl_ground, bt = s->bl_to;
        uint64_t n0[6] = { s->bl_short, s->bl_noground, s->bl_wet, s->bl_cannot, s->bl_noedge, 0 };
        s->edge_cued = 1, s->edge_cue_t = in->t;
        if (edge_blink(s, c, in, ray, ctx, o) > 0) {
            s->edge_cues++, s->cue_h = s->bl_h;
            o->events2 |= SOMVR_CLIMB_E_EDGE_CUE;
        }
        /* (a probe, not an A: the last A's numbers and the counts as they were) */
        s->bl_why = why, s->bl_h = bh, s->bl_from = bf, s->bl_ground = bg, s->bl_to = bt;
        s->bl_short = n0[0], s->bl_noground = n0[1], s->bl_wet = n0[2], s->bl_cannot = n0[3], s->bl_noedge = n0[4];
    }
    if (s->active && !in->live) { /* hanging without input: nothing more until it is back */
        o->active = 1, o->driver = -1;
        return;
    }
    int lifting = s->phase == SOMVR_CLIMB_PH_LIFT, waiting = s->phase == SOMVR_CLIMB_PH_CMD;
    int fl = -3; /* fling: the release (fl_release: 1 a fling, 0 slow, -1 no velocity; -2 not the driving hand's or not
                  * while holding; -3 no grip opened) */
    /* lets: the grip opened, or the tracking gone too long (not while lifting or waiting: the top is ours then) */
    for (int h = 0; h < 2 && !lifting && !waiting; h++) {
        somvr_climb_hold *k = &s->h[h];
        if (!k->holding) continue;
        if (!k->down) {
            if (c->fling && c->letgo == SOMVR_CLIMB_LETGO_HANG)
                fl = s->phase == SOMVR_CLIMB_PH_HOLD && h == s->driver ? fl_release(s, c, in, h) : -2;
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
        int lipstart = g > 0 && !s->active && k->lip && in->root_ok && in->root.y - c->foot_cm >= k->lip_top_y - 30.0f;
        if (g > 0 && !s->active && c->low_cm > 0 && in->root_ok && k->hit.y < in->root.y - c->low_cm && !lipstart) {
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
            if (c->descent) s->wall_tw = k->toward; /* the wall ray's direction from now on */
            if (k->lip) s->lips++, o->events |= SOMVR_CLIMB_E_LIP_L << h;
            if (c->overhang > 0 && s->active && s->topped && !s->descending && k->hit.y > s->top_y + OH_RETOP_CM) {
                s->topped = 0, s->retops++; /* a ledge passed on the way up: the next top is this hold's wall's */
                o->events2 |= SOMVR_CLIMB_E_RETOP;
            }
            if (lipstart) { /* the way down from the top he stands on */
                s->descending = 1, s->lip_top_y = k->lip_top_y, s->lip_edge = k->lip_edge, s->lip_out = k->lip_out;
                s->lip_blocked = 0, s->lip_block_n = 0;
                s->descents++;
                o->events |= SOMVR_CLIMB_E_DESCENT;
            }
        }
    }
    if (!lifting && !waiting)
        for (int h = 0; h < 2; h++) cue_step(s, c, in, h, ray, ctx, o);
    int any = s->h[0].holding || s->h[1].holding;
    if (!s->active && any) {
        s->active = 1, s->phase = SOMVR_CLIMB_PH_HOLD, s->t0 = in->t, s->carry = (sv_vec3){ 0, 0, 0 };
        s->driver = -1, s->oh_cm = 0;
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
    } else if (!any && s->phase == SOMVR_CLIMB_PH_FLING) {
        fl_step(s, c, in, ray, ctx, o, dt); /* the flight goes on */
    } else if (!any && hangs && !on_ground(s, c, in, ray, ctx, o)) {
        if (fl > 0) { /* the last hold let go while its hand moved fast up: the fling */
            fl_launch(s, c, in, ray, ctx, o);
            fl_step(s, c, in, ray, ctx, o, dt);
        } else {
            if (fl == 0 && (s->fl_up > 50 || s->fl_out > 50 || s->fl_side > 50))
                s->fl_nones++; /* moving, not fast up enough (or only away, down, sideways): the hang */
            if (fl > -3) { /* the log's line: why no fling (s->fl_none) */
                s->fl_none = fl;
                o->events |= SOMVR_CLIMB_E_FLING_NONE;
            }
            hang(s, in, 0, o); /* both hands open: Talion stays where he is (the grips are armed: open hands may grab) */
        }
    } else if (!any) {
        end_climb(s, SOMVR_CLIMB_END_RELEASED, 0, o);
    } else {
        if (s->phase == SOMVR_CLIMB_PH_HANG) { /* a new hold while hanging: the climb goes on */
            s->phase = SOMVR_CLIMB_PH_HOLD;
            s->resumes++;
            o->events |= SOMVR_CLIMB_E_RESUME;
        } else if (s->phase == SOMVR_CLIMB_PH_FLING) { /* a hold in flight: the flight stops at once, the hold drives */
            s->phase = SOMVR_CLIMB_PH_HOLD;
            s->fl_why = SOMVR_CLIMB_FL_CATCH, s->fl_v = (sv_vec3){ 0, 0, 0 };
            s->catches++;
            o->events |= SOMVR_CLIMB_E_CATCH;
        }
        /* the newest hold drives */
        int d = newest(s);
        somvr_climb_hold *k = &s->h[d];
        const somvr_climb_hand *hd = &in->hand[d];
        if (d != s->driver) vh_reset(k, pose_t(in), hd->p, hd->valid); /* fling: its velocity from its own drive */
        s->driver = d; /* a new driver goes on from its own last position (kept while it held) */
        if (hd->valid && k->have_last) {
            sv_vec3 dp = sub3(hd->p, k->last_p);
            if (!fin3(dp) || len3(dp) > c->jump_cm) {
                s->jumps++;
                o->events |= SOMVR_CLIMB_E_JUMP;
                vh_reset(k, pose_t(in), hd->p, 1);
            } else {
                float ks = in->rig_scale >= 0.5f && in->rig_scale <= 2.0f ? in->rig_scale : 1.0f; /* the player size */
                s->carry = add3(s->carry, mul3(somvr_climb_to_world(dp, in->rig_yaw), -ks));
                vh_push(k, pose_t(in), hd->p);
            }
        }
        sv_vec3 m = somvr_climb_clamp(s->carry, c->step_cm);
        s->carry = sub3(s->carry, m);
        if (len3(s->carry) > c->carry_cm) {
            s->carry = somvr_climb_clamp(s->carry, c->carry_cm);
            s->cuts++;
            o->events |= SOMVR_CLIMB_E_CARRY_CUT;
        }
        m.y += edge_step(s, c, in, o, c->step_cm); /* push: a forward push the edge blocks lifts him */
        lip_step(s, c, in, o, &m); /* descent: a push down while on the top goes out over the edge */
        if (c->overhang > 0 && !s->descending && in->root_ok && s->have_root && in->head_ok &&
            mantle(s, c, in, k, ray, ctx, o, (float)dt)) { /* the game holds him under the top: the blink onto it */
            for (int h = 0; h < 2; h++) let_go(&s->h[h]), s->h[h].armed = 0;
            s->phase = SOMVR_CLIMB_PH_BLINK, s->phase_t = in->t, s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
            s->mantles++, s->blinks++;
            o->events2 |= SOMVR_CLIMB_E_MANTLE;
            o->blink = 1, o->blink_to = s->bl_to;
            o->active = 1, o->driver = -1;
            return;
        }
        overhang_step(s, c, in, k, o, &m); /* overhang: a pull blocked under a lip goes out round its edge */
        request(s, o, m);
        for (int h = 0; h < 2; h++) /* every holding hand's last position, for its turn as the driver */
            if (s->h[h].holding && in->hand[h].valid) s->h[h].last_p = in->hand[h].p, s->h[h].have_last = 1;
        /* push: over the top = the head past the top hold's point toward the wall: standing on it */
        if (over_top(s, c, in)) {
            end_climb(s, SOMVR_CLIMB_END_OVER, 1, o);
            o->active = 0, o->driver = -1;
            return;
        }
        /* the top: an edge hold with the head above it, or the other hand reaching over it (within 1 m of the hold) */
        if (s->descending && in->head_ok && in->head_y < s->lip_top_y - 5.0f)
            s->descending = 0; /* the head under the lip's top: on the face (a top from now on is one climbed up to) */
        for (int h = 0; h < 2 && !s->topped && !s->descending; h++) {
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
