/* somvr_climb.c - see somvr_climb.h */
#include "somvr_climb.h"

#include <math.h>
#include <string.h>

static const char *const g_end_names[SOMVR_CLIMB_NEND] = { "none", "released", "game", "input", "timeout", "top", "lifted",
                                                           "lift-stall", "cmd", "outside", "over", "drop", "blink" };
static const char *const g_phase_names[8] = { "none", "hold", "lift", "cmd", "hang", "fling", "blink", "mantle" };

const char *somvr_climb_end_name(int why) { return why >= 0 && why < SOMVR_CLIMB_NEND ? g_end_names[why] : "?"; }
const char *somvr_climb_phase_name(int phase) { return phase >= 0 && phase < 8 ? g_phase_names[phase] : "?"; }
const char *somvr_climb_am_name(int why) {
    static const char *const n[7] = { "-", "the eyes not high enough over the top", "no floor on the top ahead",
                                      "no room for him on the top", "the way onto it not clear", "no blink now",
                                      "the eyes too high over the top" };
    return why >= 0 && why < 7 ? n[why] : "?";
}
const char *somvr_climb_bl_name(int why) {
    static const char *const n[8] = { "-", "a short drop: the fall", "no ground under him: he stays",
                                      "water under him: he stays", "no blink now: the fall", "no edge ahead",
                                      "an enemy at the landing: he stays", "no room to stand at the landing: he stays" };
    return why >= 0 && why < 8 ? n[why] : "?";
}
const char *somvr_climb_fl_name(int why) {
    static const char *const n[8] = { "-", "the arc's top", "blocked above", "out of time", "a catch", "over",
                                      "blocked at the edge", "the wall under the chest" };
    return why >= 0 && why < 8 ? n[why] : "?";
}
const char *somvr_climb_dn_name(int why) {
    static const char *const n[6] = { "ok", "the hand too high over the floor (or not under the eyes)",
                                      "no edge within reach of the hand", "the drop beyond it too short",
                                      "no wall under the lip (none, or it slopes or steps out)", "the way over the edge not clear" };
    return why >= 0 && why < 6 ? n[why] : "?";
}
const char *somvr_climb_thr_name(int why) {
    static const char *const n[6] = { "-", "out of the rock", "no vertical move for 0.5 s", "its most", "the climb's end",
                                      "no root or head" };
    return why >= 0 && why < 6 ? n[why] : "?";
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
    c->overhang = 0, c->overhang_cm = 40, c->oh_mantle = 1;
    c->pullover = 0, c->po_pull_cm = 25, c->po_up_cm = 120, c->po_down_cm = 150, c->po_block_s = 0.3f;
    c->hang_stick_cm = 0;
    c->face_toward = 1, c->through = 0, c->thr_block_s = 0.15f, c->thr_clear_cm = 15, c->thr_side_cm = 10;
    c->thr_max_cm = 300, c->thr_look_cm = 60, c->thr_top = 1;
    c->hug = 0, c->hug_cm = 50, c->hug_in = 0.35f;
    c->stand = 0, c->stand_in_cm = 15, c->stand_up_cm = 15, c->stand_down_cm = 40, c->stand_reach_cm = 35;
    c->stand_walk_cms = 150, c->topout = 0;
    c->fling_up = 0, c->fling_tw = 0.25f, c->fling_wall_up = 0.5f, c->fling_side_max = 0.6f;
    c->drop_mark = 0, c->dm_min_s = 0.3f, c->dm_show_s = 0.6f, c->dm_stick = 0.5f, c->dm_move_cm = 30, c->dm_out_cm = 100;
    c->dm_enemy_cm = 120;
    c->mantle = 0, c->am_head_cm = 25, c->am_cms = 350, c->am_clear_cm = 10, c->am_overhang = 0;
    c->dm_timer = 0, c->unstuck = 0, c->fl_reach_cm = 120, c->round_lip = 0;
    c->down_over = 0, c->dn_low_cm = 40, c->dn_high_cm = 90, c->dn_reach_cm = 45, c->dn_under_cm = 25, c->dn_gap_cm = 45;
}

void somvr_climb_init(somvr_climb *s) {
    memset(s, 0, sizeof *s);
    s->driver = -1, s->po_hand = -1;
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
    s->fl_wallup = 0;
    if (c->fling_up && len3(tw) >= 0.5f && up > 0) { /* build 18 (see the header's fling): up first */
        float fwd = into > 0 ? into : 0, keep = s->topped ? fminf(fwd, up) : fminf(fwd, c->fling_tw * up);
        float extra = s->topped ? 0.0f : (fwd - keep) * c->fling_wall_up; /* into the wall past the kept part: up it */
        float up2 = up + extra;
        s->fl_wallup = extra, s->fl_up = up2;
        if (!(up2 >= c->fling_min)) return 0;
        if (c->fling_max > 0 && up2 > c->fling_max) up2 = c->fling_max, s->fl_capped = 1;
        side = somvr_climb_clamp(mul3(side, c->fling_side), c->fling_side_max * up2);
        sv_vec3 v = { side.x + keep * tw.x, up2, side.z + keep * tw.z };
        s->fl_side = len3(side), s->fl_into = keep;
        *v0 = v;
        return 1;
    }
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

/* the face's level normal at a hold (build 16 round 2, SOMVR_CLIMB_FACE_TOWARD): two level rays from the head's x/z at
 * the hold's height (5 cm under it), FACE_DEG either side of the head's way to the hold; both hit within FACE_NEAR_CM
 * of the hold = the face between them, its normal into the face (the head's side) = *n, 1. A face seen almost edge-on
 * from the head, a corner (the hits apart under 2 cm) or no hits = 0 */
#define FACE_DEG 12.0f
#define FACE_NEAR_CM 40.0f
#define FACE_OFF_DEG 35.0f /* the hold's ray this far off the face's normal: the normal is the hold's toward */
static int face_into(somvr_climb *s, somvr_climb_out *o, somvr_climb_ray_fn ray, void *ctx, int h,
                     const somvr_climb_in *in, sv_vec3 hit, sv_vec3 *n) {
    if (!in->head_ok) return 0;
    sv_vec3 from = { in->head_x, hit.y - 5.0f, in->head_z }, d = { hit.x - in->head_x, 0, hit.z - in->head_z };
    float dl = len3(d);
    if (!(dl > 5.0f) || !isfinite(dl)) return 0;
    d = mul3(d, 1.0f / dl);
    float ca = cosf(FACE_DEG * 0.01745329f), sa = sinf(FACE_DEG * 0.01745329f);
    sv_vec3 da = { d.x * ca - d.z * sa, 0, d.x * sa + d.z * ca }, db = { d.x * ca + d.z * sa, 0, -d.x * sa + d.z * ca };
    somvr_climb_hit ra, rb;
    if (!cast(s, o, ray, ctx, h, from, da, dl + FACE_NEAR_CM, &ra) || !ra.hit ||
        !cast(s, o, ray, ctx, h, from, db, dl + FACE_NEAR_CM, &rb) || !rb.hit)
        return 0;
    sv_vec3 pa = { ra.point.x - hit.x, 0, ra.point.z - hit.z }, pb = { rb.point.x - hit.x, 0, rb.point.z - hit.z };
    sv_vec3 t = { rb.point.x - ra.point.x, 0, rb.point.z - ra.point.z };
    float tl = len3(t);
    if (len3(pa) > FACE_NEAR_CM || len3(pb) > FACE_NEAR_CM || !(tl > 2.0f)) return 0;
    sv_vec3 nn = { -t.z / tl, 0, t.x / tl };
    if (nn.x * d.x + nn.z * d.z < 0) nn = mul3(nn, -1.0f);
    if (nn.x * d.x + nn.z * d.z < 0.2f) return 0;
    *n = nn;
    return 1;
}

/* the grab test of hand h (find_hold; a press): 1 = a hold (the hold's surface, direction and edge filled), 0 = none
 * yet, -1 = a surface that is not climbable */
#define RL_UP_CM 55.0f /* round lip: the second edge test this far over the hold */
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
    /* the face toward: a ray at a slant to the face (the palm or the fingers along the wall) gives a toward along the
     * wall, and then `over` came with the feet under the top (run 032503) and the edge test's raised ray missed the
     * wall above: the face's own normal is the toward, and the edge test runs along it */
    sv_vec3 fn, edir = k->dir, efrom = bfrom;
    float elen = blen, edist = bh.dist;
    k->face = 0;
    float cos_off = cosf(FACE_OFF_DEG * 0.01745329f);
    sv_vec3 hh = in->head_ok ? unit3((sv_vec3){ bh.point.x - in->head_x, 0, bh.point.z - in->head_z }) : (sv_vec3){ 0 };
    if (c->face_toward && via != SOMVR_CLIMB_VIA_DOWN && len3(hh) > 0.5f && /* (the ray along the head's way: kept) */
        hh.x * k->toward.x + hh.z * k->toward.z < cos_off && face_into(s, o, ray, ctx, h, in, bh.point, &fn) &&
        fn.x * k->toward.x + fn.z * k->toward.z < cos_off) {
        k->face = 1, s->faces++;
        k->ray_tw = k->toward, k->toward = fn;
        edir = fn, efrom = sub3(bh.point, mul3(fn, 15.0f)), elen = 15.0f + 1.0f, edist = 15.0f;
    }
    /* the edge: the same ray edge_cm higher finds no wall, or finds one much farther away */
    somvr_climb_hit e, d;
    sv_vec3 up = { 0, c->edge_cm, 0 };
    k->edge = 0, k->top_found = 0, k->top_y = bh.point.y + c->edge_cm;
    if (cast(s, o, ray, ctx, h, add3(efrom, up), edir, elen + c->edge_far_cm, &e) &&
        (!e.hit || e.dist > edist + c->edge_far_cm)) {
        k->edge = 1;
        /* the top's height: down from above it, a little into the wall */
        sv_vec3 top_from = add3(add3(bh.point, mul3(edir, 4.0f)), (sv_vec3){ 0, c->edge_cm + 5.0f, 0 });
        if (cast(s, o, ray, ctx, h, top_from, (sv_vec3){ 0, -1, 0 }, c->edge_cm + 15.0f, &d) && d.hit) {
            k->top_y = fmaxf(d.point.y, bh.point.y);
            k->top_found = 1;
        }
    }
    /* round lip (build 21): no edge by edge_cm; RL_UP_CM higher no wall, and a floor within RL_UP_CM over the hold (a
     * ray down 15 cm into the wall) = an edge with that top (the crest of a rounded lip under the real top) */
    k->rl = 0;
    if (c->round_lip && !k->edge && len3(k->toward) > 0.5f) {
        sv_vec3 up2 = { 0, RL_UP_CM, 0 };
        if (cast(s, o, ray, ctx, h, add3(efrom, up2), edir, elen + c->edge_far_cm, &e) &&
            (!e.hit || e.dist > edist + c->edge_far_cm)) {
            sv_vec3 tf = add3(add3(bh.point, mul3((sv_vec3){ k->toward.x, 0, k->toward.z }, 15.0f)),
                              (sv_vec3){ 0, RL_UP_CM + 5.0f, 0 });
            if (cast(s, o, ray, ctx, h, tf, (sv_vec3){ 0, -1, 0 }, RL_UP_CM + 5.0f, &d) && d.hit && !d.wet &&
                d.point.y > bh.point.y + 2.0f) {
                k->edge = 1, k->top_found = 1, k->top_y = d.point.y, k->rl = 1;
                s->rl_tops++;
            }
        }
    }
    /* descent: a lip (its toward into the top: over it = back onto the top) */
    k->lip = c->descent && lip_test(s, c, in, h, ray, ctx, o, &bh, k->face ? edir : bdir, &k->lip_top_y, &k->lip_edge,
                                    &k->lip_out, &k->lip_drop); /* (at a slant: from the face's normal) */
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
    s->descending = 0, s->stand_walking = 0, s->hug_ok = 0;
    if (s->to_on) s->to_on = 0, s->to_why = SOMVR_CLIMB_THR_END, o->events2 |= SOMVR_CLIMB_E_TOP_THR_OFF;
    s->to_block = 0;
    if (s->thr_on) s->thr_on = 0, s->thr_why = SOMVR_CLIMB_THR_END, o->events2 |= SOMVR_CLIMB_E_THR_OFF;
    s->thr_block = 0;
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
/* unstuck (4): no wall within fl_reach_cm of the chest (the eyes - 40 cm) along toward, level or 20 deg down = gone */
static int fl_reach_lost(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                         void *ctx, somvr_climb_out *o) {
    sv_vec3 tw = { s->fl_toward.x, 0, s->fl_toward.z };
    if (!in->head_ok || len3(tw) < 0.5f || !(c->fl_reach_cm > 0)) return 0;
    tw = unit3(tw);
    sv_vec3 ch = { in->head_x, in->head_y - 40.0f, in->head_z }, dn = unit3((sv_vec3){ tw.x, -0.364f, tw.z });
    somvr_climb_hit r;
    if (!cast(s, o, ray, ctx, s->fl_hand, ch, tw, c->fl_reach_cm, &r) || r.hit) return 0;
    if (!cast(s, o, ray, ctx, s->fl_hand, ch, dn, c->fl_reach_cm, &r) || r.hit) return 0;
    return 1;
}
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
        if (c->fling_up && !s->topped && !s->fl_glide && into > 0) { /* the face takes the push into it: up it alone */
            float aq = rq.x * tw.x + rq.z * tw.z, ga = ap.x * tw.x + ap.z * tw.z;
            if (aq > 0.5f && ga < 0.5f * aq) s->fl_v.x -= into * tw.x, s->fl_v.z -= into * tw.z, into = 0, s->fl_into_cut++;
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
    if (c->unstuck && !glide && stop == SOMVR_CLIMB_FL_NONE && !s->topped && vy0 > 0 && fl_reach_lost(s, c, in, ray, ctx, o)) {
        stop = SOMVR_CLIMB_FL_REACH, m = (sv_vec3){ 0, 0, 0 }; /* unstuck (4): the hands could not reach the wall higher up */
        s->fl_reaches++;
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

/* a landing on a top at ty (the pullover's, the A mantle's): from e (on the top's edge) into the top along tw (level
 * unit), the first of 40, 35, 45 .. 80 cm in with a floor within 30 cm of ty (not water), nothing 180 cm over it and
 * nothing within PO_ROOM_CM round it at 50 and 140 cm (room for Talion's capsule); 1 = found (*fl). dbg (the logs):
 * [1] floors seen, [2] the highest, [3] blocked over them, [4] [5] [6] room rays hit: in, out, sides */
#define PO_ROOM_CM 33.0f
static int land_find(somvr_climb *s, somvr_climb_out *o, somvr_climb_ray_fn ray, void *ctx, int h, sv_vec3 e, sv_vec3 tw,
                     float ty, float *dbg, sv_vec3 *fl_out) {
    static const float ds[] = { 40, 35, 45, 30, 50, 25, 55, 60, 20, 70, 80 };
    sv_vec3 side = { -tw.z, 0, tw.x };
    somvr_climb_hit r, u;
    for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
        sv_vec3 f = add3(e, (sv_vec3){ tw.x * ds[i], 0, tw.z * ds[i] });
        f.y = ty + 60.0f;
        if (!cast(s, o, ray, ctx, h, f, (sv_vec3){ 0, -1, 0 }, 100, &r) || !r.hit || r.wet || fabsf(r.point.y - ty) > 30.0f)
            continue;
        dbg[1]++, dbg[2] = fmaxf(dbg[2], r.point.y);
        sv_vec3 fl = r.point;
        if (!cast(s, o, ray, ctx, h, add3(fl, (sv_vec3){ 0, 10, 0 }), (sv_vec3){ 0, 1, 0 }, 180, &u) || u.hit) {
            dbg[3]++;
            continue;
        }
        int room = 1;
        for (int hh = 0; hh < 2; hh++) {
            sv_vec3 at = add3(fl, (sv_vec3){ 0, hh ? 140.0f : 50.0f, 0 });
            const sv_vec3 dirs[4] = { tw, mul3(tw, -1.0f), side, mul3(side, -1.0f) };
            for (int j = 0; j < 4; j++)
                if (!cast(s, o, ray, ctx, h, at, dirs[j], PO_ROOM_CM, &u) || u.hit) room = 0, dbg[j < 2 ? 4 + j : 6]++;
        }
        if (!room) continue;
        *fl_out = fl;
        return 1;
    }
    return 0;
}

/* pullover (see the header): the top of hold k (rays down over it and 10, 20 cm into the wall) and a landing on it;
 * 1 = found (k->po_*), else k->po_why */
static int po_find(somvr_climb *s, const somvr_climb_in *in, int h, somvr_climb_hold *k, somvr_climb_ray_fn ray,
                   void *ctx, somvr_climb_out *o) {
    sv_vec3 tw = { k->toward.x, 0, k->toward.z };
    if (len3(tw) < 0.3f && in->head_ok) tw = (sv_vec3){ k->hit.x - in->head_x, 0, k->hit.z - in->head_z };
    if (len3(tw) < 0.2f) return k->po_why = SOMVR_CLIMB_PO_NOTOP, 0;
    tw = unit3(tw);
    somvr_climb_hit r;
    float ty = 0;
    int top = 0;
    /* the top: just over the hold, or a little into the wall (round lip: the highest of the rays out to 40 cm in - the
     * crest of a rounded lip is no top when the floor goes on higher behind it) */
    float dmax = s->rl_on ? 40.5f : 20.5f, ty0 = 0;
    for (float d = 0; d <= dmax && (!top || s->rl_on); d += 10) {
        sv_vec3 f = add3(k->hit, (sv_vec3){ tw.x * d, 70, tw.z * d });
        if (cast(s, o, ray, ctx, h, f, (sv_vec3){ 0, -1, 0 }, 110, &r) && r.hit && r.point.y >= k->hit.y - 25.0f &&
            r.point.y <= k->hit.y + 60.0f && !r.wet && (!top || r.point.y > ty)) {
            if (!top) ty0 = r.point.y;
            ty = r.point.y, top = 1;
        }
    }
    if (top && ty > ty0 + 5.0f) s->rl_po++;
    k->po_has_top = top, k->po_top_y = ty;
    if (!top) return k->po_why = SOMVR_CLIMB_PO_NOTOP, 0;
    float *dbg = s->po_dbg;
    memset(s->po_dbg, 0, sizeof s->po_dbg);
    dbg[0] = ty, dbg[2] = -1e9f;
    sv_vec3 fl;
    if (land_find(s, o, ray, ctx, h, k->hit, tw, ty, dbg, &fl)) {
        k->po_top_y = ty, k->po_land = fl, k->po_in = tw;
        return k->po_why = SOMVR_CLIMB_PO_OK, 1;
    }
    return k->po_why = dbg[1] > 0 ? SOMVR_CLIMB_PO_NOROOM : SOMVR_CLIMB_PO_NOFLOOR, 0;
}

/* pullover: arm, show, the pull, the cancel; 1 = the pull is done (s->bl_to: the blink onto the landing) */
static int pullover(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                    somvr_climb_out *o, float dt) {
    if (!c->pullover || s->descending || !in->head_ok) {
        s->po_hand = -1;
        return 0;
    }
    for (int h = 0; h < 2; h++) { /* each new hold: its top and landing, once */
        somvr_climb_hold *k = &s->h[h];
        if (!k->holding || k->po_seq == k->seq) continue;
        k->po_seq = k->seq, k->po_noway = 0, k->po_has_top = 0;
        k->po_ok = po_find(s, in, h, k, ray, ctx, o);
        if (!k->po_ok && k->po_why != SOMVR_CLIMB_PO_NOTOP) {
            s->po_why = k->po_why, s->po_nones++;
            o->events2 |= SOMVR_CLIMB_E_PO_NONE;
        }
    }
    int a = s->po_hand;
#define PO_IN_RANGE(k) ((k)->holding && (k)->po_ok && (k)->po_top_y - in->head_y <= c->po_up_cm && \
                        in->head_y - (k)->po_top_y <= c->po_down_cm)
    if (a >= 0 && !(s->h[a].seq == s->po_aseq && PO_IN_RANGE(&s->h[a]))) { /* let go, or out of range: cancelled */
        s->po_cancels++, s->po_hand = a = -1;
        o->events2 |= SOMVR_CLIMB_E_PO_CANCEL;
    }
#define PO_TOP_RANGE(k) ((k)->holding && (k)->po_has_top && (k)->po_top_y - in->head_y <= c->po_up_cm && \
                         in->head_y - (k)->po_top_y <= c->po_down_cm)
    if (a < 0) { /* not armed: the newest hold on a top in range, and is the 1:1 blocked there? */
        int b = -1;
        for (int h = 0; h < 2; h++)
            if (PO_TOP_RANGE(&s->h[h]) && in->hand[h].valid && (b < 0 || s->h[h].seq > s->h[b].seq)) b = h;
        if (b < 0) {
            s->po_block_s = 0;
            return 0;
        }
        somvr_climb_hold *kb = &s->h[b];
        if (kb->seq != s->po_bseq) s->po_bseq = kb->seq, s->po_block_s = 0;
        /* the last request against the root's move: up, and (the head at the top) into it */
        sv_vec3 rq = s->last_req, ap = sub3(in->root, s->last_root);
        int high = in->head_y >= kb->po_top_y - 10.0f;
        float tx = kb->po_in.x, tz = kb->po_in.z;
        if (!kb->po_ok) { /* no landing: the hold's own toward */
            float n = sqrtf(kb->toward.x * kb->toward.x + kb->toward.z * kb->toward.z);
            tx = n > 0.3f ? kb->toward.x / n : 0, tz = n > 0.3f ? kb->toward.z / n : 0;
        }
        float rin = rq.x * tx + rq.z * tz, ain = ap.x * tx + ap.z * tz;
        float pmin = fmaxf(0.3f, 15.0f * (dt > 0 && dt < 0.2f ? dt : 0.0f)); /* a pull: over 15 cm/s, not the hand's tremor */
        int pull_up = rq.y > pmin, pull_in = high && rin > pmin;
        if (in->root_ok && s->have_root && (pull_up || pull_in)) {
            int blocked = (!pull_up || ap.y < 0.2f * rq.y) && (!pull_in || ain < 0.2f * rin); /* not rising, not going in */
            s->po_block_s = blocked ? s->po_block_s + (dt > 0 && dt < 0.2f ? dt : 0.0f) : 0.0f;
        }
        if (s->po_block_s < c->po_block_s) return 0;
        if (!kb->po_ok) { /* blocked, and nowhere to stand up there: say so once */
            if (!kb->po_noway) kb->po_noway = 1, s->po_noways++, o->events2 |= SOMVR_CLIMB_E_PO_NOWAY;
            return 0;
        }
        if (!PO_IN_RANGE(kb)) return 0;
        s->po_hand = a = b, s->po_aseq = kb->seq, s->po_grab_p = in->hand[b].p, s->po_pull = 0, s->po_block_s = 0;
        s->po_arms++;
        o->events2 |= SOMVR_CLIMB_E_PO_ARM;
    }
#undef PO_TOP_RANGE
#undef PO_IN_RANGE
    const somvr_climb_hold *k = &s->h[a];
    if (in->hand[a].valid) { /* the body's wish: the hand's motion reversed, into the world (down = up, back = in) */
        float ks = in->rig_scale >= 0.5f && in->rig_scale <= 2.0f ? in->rig_scale : 1.0f;
        sv_vec3 d = mul3(somvr_climb_to_world(sub3(s->po_grab_p, in->hand[a].p), in->rig_yaw), ks);
        float up = fmaxf(d.y, 0.0f), into = fmaxf(d.x * k->po_in.x + d.z * k->po_in.z, 0.0f);
        s->po_pull = fin3(d) ? sqrtf(up * up + into * into) : 0.0f;
    }
    o->po_show = 1, o->po_land = k->po_land, o->po_hand = a;
    o->po_pull = fminf(s->po_pull / fmaxf(c->po_pull_cm, 1.0f), 1.0f);
    if (s->po_pull < c->po_pull_cm || !in->can_blink) return 0;
    s->bl_to = (sv_vec3){ k->po_land.x, k->po_land.y + c->foot_cm + 5.0f, k->po_land.z };
    s->mt_top = k->hit, s->mt_floor = k->po_land;
    s->po_goes++, s->po_hand = -1;
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

/* hug (see the header): the driving hold k's face (the plane through its surface point along its toward, level) and
 * the root's axis: the move's part away from it cut past c->hug_cm; beyond it (not at a top, not descending) a part of
 * the pull toward the face, at most to hug_cm */
static void hug_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                     somvr_climb_out *o, sv_vec3 *m) {
    s->hug_ok = 0;
    if (!c->hug || !in->root_ok || s->descending) return;
    float n = sqrtf(k->toward.x * k->toward.x + k->toward.z * k->toward.z);
    if (n < 0.3f) return; /* no wall direction */
    sv_vec3 tw = { k->toward.x / n, 0, k->toward.z / n };
    float d = (k->hit.x - in->root.x) * tw.x + (k->hit.z - in->root.z) * tw.z;
    if (!isfinite(d)) return;
    s->hug_ok = 1, s->hug_d = d;
    if (d > s->hug_max_d) s->hug_max_d = d;
    float into = m->x * tw.x + m->z * tw.z, allow = fmaxf(c->hug_cm - d, 0.0f);
    if (-into > allow) { /* away from the face past hug_cm: cut */
        float cut = -into - allow;
        *m = add3(*m, mul3(tw, cut));
        s->hug_cut_cm += cut, s->hug_cuts++, o->hug_cut = cut;
    }
    if (c->hug_in > 0 && d > c->hug_cm && !s->topped) { /* beyond it: a part of the pull toward the face */
        float add = fminf(c->hug_in * len3(*m), d - c->hug_cm);
        if (add > 0.01f) {
            *m = add3(*m, mul3(tw, add));
            s->hug_in_cm += add, s->hug_ins++, o->hug_in = add;
        }
    }
}

/* stand (see the header): a floor under Talion's capsule at a top: rays down at the root's axis and offs cm toward the
 * top (tw), the floor from down cm under the feet to c->stand_up_cm over them; 1 = found (*fy its height, *at which) */
static int stand_floor(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                       void *ctx, somvr_climb_out *o, const float *offs, int noffs, float down, float up, float *fy,
                       int *at) {
    if (!in->root_ok) return 0;
    float n = sqrtf(s->top_toward.x * s->top_toward.x + s->top_toward.z * s->top_toward.z);
    sv_vec3 tw = n > 0.3f ? (sv_vec3){ s->top_toward.x / n, 0, s->top_toward.z / n } : (sv_vec3){ 0, 0, 0 };
    float feet = in->root.y - c->foot_cm;
    for (int i = 0; i < noffs; i++) {
        if (offs[i] > 0 && n <= 0.3f) break; /* no way toward the top: the root's own ray only */
        sv_vec3 f = add3(in->root, mul3(tw, offs[i]));
        f.y = feet + c->stand_up_cm + 45.0f;
        somvr_climb_hit g;
        if (!cast(s, o, ray, ctx, 0, f, (sv_vec3){ 0, -1, 0 }, c->stand_up_cm + 45.0f + down, &g) || !g.hit || g.wet)
            continue;
        if (g.point.y > feet + up || g.point.y < feet - down) continue;
        *fy = g.point.y, *at = i;
        return 1;
    }
    return 0;
}
static void stand_note(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_out *o, float fy,
                       int at, int over) {
    s->stand_h = in->root.y - c->foot_cm - fy, s->stand_at = at, s->stand_over = over;
    s->stands++, s->stand_overs += over != 0;
    o->events2 |= SOMVR_CLIMB_E_STAND;
}

#define STAND_HOLD_UP_CM 5.0f
#define TO_NEAR_CM 20.0f /* topout: the head this far under the driving hold's top counts as at it */
#define STAND_LET_UP_CM 10.0f /* let go: the feet at most this under the floor under his axis */
/* topout (see the header), the A mantle: the head's way along the move m clear: rays from the head (to_clear_way: this
 * frame's) and c->thr_side_cm round it (across the move), c->thr_clear_cm past it; 1 = clear */
static int way_clear(somvr_climb *s, const somvr_climb_cfg *c, sv_vec3 head, sv_vec3 tw, somvr_climb_ray_fn ray,
                     void *ctx, somvr_climb_out *o, sv_vec3 m) {
    float l = len3(m);
    if (!(l > 0.01f)) return 1;
    sv_vec3 d = mul3(m, 1.0f / l), up = { 0, 1, 0 };
    sv_vec3 a = { d.y * up.z - d.z * up.y, d.z * up.x - d.x * up.z, d.x * up.y - d.y * up.x }; /* d x up */
    if (len3(a) < 0.3f) a = (sv_vec3){ -tw.z, 0, tw.x }; /* straight up or down: across = along the edge */
    a = unit3(a);
    sv_vec3 b = unit3((sv_vec3){ a.y * d.z - a.z * d.y, a.z * d.x - a.x * d.z, a.x * d.y - a.y * d.x }); /* a x d */
    sv_vec3 offs[5] = { { 0, 0, 0 }, mul3(a, c->thr_side_cm), mul3(a, -c->thr_side_cm), mul3(b, c->thr_side_cm),
                        mul3(b, -c->thr_side_cm) };
    for (int i = 0; i < 5; i++) {
        somvr_climb_hit r;
        if (!cast(s, o, ray, ctx, -1, add3(head, offs[i]), d, l + c->thr_clear_cm, &r) || r.hit) return 0;
    }
    return 1;
}
static int to_clear_way(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, sv_vec3 tw,
                        somvr_climb_ray_fn ray, void *ctx, somvr_climb_out *o, sv_vec3 m) {
    return way_clear(s, c, (sv_vec3){ in->head_x, in->head_y, in->head_z }, tw, ray, ctx, o, m);
}
static void to_off(somvr_climb *s, somvr_climb_out *o, int why) {
    if (!s->to_on) return;
    s->to_on = 0, s->to_why = why, s->to_block = 0;
    o->events2 |= SOMVR_CLIMB_E_TOP_THR_OFF;
}
static void topout_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                        somvr_climb_ray_fn ray, void *ctx, somvr_climb_out *o, sv_vec3 *m, float dt) {
    if (!c->topout) return;
    /* the top: the push's (topped, the head over it), or the driving hold's own found top with the head at most
     * TO_NEAR_CM under it (run 110721's platform: the pull up held with the head level with the top, not topped yet) */
    sv_vec3 ttw = s->top_toward, thit = s->top_hit;
    float ty = s->top_y;
    int at = s->topped && in->head_ok && in->head_y >= s->top_y + c->top_cm;
    if (!at && k && k->edge && k->top_found && in->head_ok && in->head_y >= k->top_y - TO_NEAR_CM)
        at = 1, ttw = k->toward, thit = k->hit, ty = k->top_y;
    float n = sqrtf(ttw.x * ttw.x + ttw.z * ttw.z);
    if (!at || s->descending || !in->root_ok || !s->have_root || !in->head_ok || n < 0.3f) {
        to_off(s, o, SOMVR_CLIMB_THR_NOREAD);
        s->to_block = 0;
        return;
    }
    sv_vec3 tw = { ttw.x / n, 0, ttw.z / n };
    float dts = dt > 0 && dt < 0.2f ? dt : 0.0f;
    if (!s->to_on) { /* the last push (up or into the top) against what the root did */
        sv_vec3 rq = s->last_req, ap = sub3(in->root, s->last_root);
        float pmin = fmaxf(0.3f, 15.0f * dts), rin = rq.x * tw.x + rq.z * tw.z;
        int blocked = (rq.y > pmin || rin > pmin) && len3(ap) < 0.2f * len3(rq);
        s->to_block = blocked ? s->to_block + dts : 0.0f;
        if (s->to_block < c->thr_block_s) return;
        s->to_on = 1, s->to_block = 0, s->to_idle = 0, s->to_cm = 0, s->to_clear = 0, s->to_refused_once = 0, s->to_in = 0;
        s->to_top_y = ty;
        s->to_ons++;
        o->events2 |= SOMVR_CLIMB_E_TOP_THR_ON;
    }
    float into = m->x * tw.x + m->z * tw.z;
    if (into < 0) *m = add3(*m, mul3(tw, -into)), into = 0; /* never back out over the drop */
    float past = (in->root.x - thit.x) * tw.x + (in->root.z - thit.z) * tw.z;
    if (past > c->over_cm + 40.0f && into > 0) *m = add3(*m, mul3(tw, -into)); /* no deeper into the top */
    /* out of the rock again after being in it (a capsule held at a contact, never in the rock, goes on: run 104918) */
    if (in->rock_ok && in->rock_cm <= -1.0f) s->to_in = 1;
    s->to_clear = s->to_in && in->rock_ok && in->rock_cm > -1.0f ? s->to_clear + 1 : 0;
    if (s->to_clear >= 2 && s->to_cm >= 10.0f) {
        to_off(s, o, SOMVR_CLIMB_THR_CLEAR);
        return;
    }
    if (s->to_cm >= c->thr_max_cm) {
        to_off(s, o, SOMVR_CLIMB_THR_MAX);
        return;
    }
    if (!(len3(*m) > 0.05f)) {
        s->to_idle += dts;
        if (s->to_idle >= 0.5f) to_off(s, o, SOMVR_CLIMB_THR_IDLE);
        return;
    }
    s->to_idle = 0;
    if (m->y < 0) return; /* a move down: the game's collision as ever (the feet never through a ledge under him) */
    if (!to_clear_way(s, c, in, tw, ray, ctx, o, *m)) { /* the head would go into the rock: blocked as ever */
        s->to_refused++;
        if (!s->to_refused_once) s->to_refused_once = 1, o->events2 |= SOMVR_CLIMB_E_TOP_THR_REFUSED;
        return;
    }
    o->through_up = m->y, o->through_lx = m->x, o->through_lz = m->z;
    s->to_cm += len3(*m), s->to_all_cm += len3(*m), s->to_frames++;
}

/* through (SOMVR_CLIMB_THROUGH, build 16 round 2): the stock wall run passes the user's slab, our hand climb's capsule
 * stopped 5 cm under its flat underside (run 041205's `climb trace`: a contact 117 cm over the root, normal straight
 * down). A pull's up part blocked c->thr_block_s under the driving hold k over the eyes = a climb through: from then
 * on each frame's vertical part goes through the rock (o->through_up: somvr.c sets the proxy by it, the game's own
 * collision does the rest), 1:1 with the hand, while the capsule is in the rock (in->rock_cm under -1: the proxy's
 * manifold) - out of it 2 frames (after 10 cm), no vertical move for 0.5 s, thr_max_cm or the climb's end = over.
 * Never with the head's way not clear: rays along the move from the head and from thr_side_cm round it (toward the
 * wall, away, both sides), thr_clear_cm past the head's new place; a hit = this frame blocked as ever (E_THR_REFUSED
 * once). The level part never goes through (the game's collision as ever) */
static void thr_off(somvr_climb *s, somvr_climb_out *o, int why) {
    if (!s->thr_on) return;
    s->thr_on = 0, s->thr_why = why, s->thr_block = 0;
    o->events2 |= SOMVR_CLIMB_E_THR_OFF;
}
static int thr_clear(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                     somvr_climb_ray_fn ray, void *ctx, somvr_climb_out *o, float v, float look) {
    sv_vec3 tw = { k->toward.x, 0, k->toward.z };
    if (len3(tw) < 0.3f) tw = (sv_vec3){ s->wall_tw.x, 0, s->wall_tw.z };
    tw = len3(tw) > 0.3f ? unit3(tw) : (sv_vec3){ 0, 0, 1 };
    sv_vec3 sd = { -tw.z, 0, tw.x }, head = { in->head_x, in->head_y, in->head_z }, dir = { 0, v > 0 ? 1.0f : -1.0f, 0 };
    sv_vec3 offs[5] = { { 0, 0, 0 }, mul3(tw, c->thr_side_cm), mul3(tw, -c->thr_side_cm), mul3(sd, c->thr_side_cm),
                        mul3(sd, -c->thr_side_cm) };
    float len = fabsf(v) + c->thr_clear_cm;
    for (int i = 0; i < 5; i++) {
        somvr_climb_hit r;
        if (!cast(s, o, ray, ctx, -1, add3(head, offs[i]), dir, i ? len : fmaxf(len, look), &r)) return 0; /* no answer */
        if (r.hit) return 0;
    }
    return 1;
}
static void through_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, const somvr_climb_hold *k,
                         somvr_climb_ray_fn ray, void *ctx, somvr_climb_out *o, const sv_vec3 *m, float dt) {
    if (!c->through) return;
    if (s->descending || !in->root_ok || !s->have_root || !in->head_ok) {
        thr_off(s, o, SOMVR_CLIMB_THR_NOREAD);
        s->thr_block = 0;
        return;
    }
    float dts = dt > 0 && dt < 0.2f ? dt : 0.0f;
    if (!s->thr_on) {
        float rq = s->last_req.y, ap = in->root.y - s->last_root.y, pmin = fmaxf(0.3f, 15.0f * dts);
        /* under a hold over the eyes, or (thr_top) at a top with the head over it, the push into it lifting him (the
         * edge step): not rising */
        int top_lift = c->thr_top && s->topped && in->head_y > s->top_y + c->top_cm && s->last_ramp > 0.2f;
        int blocked = (k->hit.y > in->head_y || top_lift) && rq > pmin && ap < 0.2f * rq;
        s->thr_block = blocked ? s->thr_block + dts : 0.0f;
        if (s->thr_block < c->thr_block_s) return;
        if (!thr_clear(s, c, in, k, ray, ctx, o, 1.0f, c->thr_look_cm)) { /* rock right over the head: no climb through */
            s->thr_refused++, s->thr_block = 0;
            o->events2 |= SOMVR_CLIMB_E_THR_REFUSED;
            return;
        }
        s->thr_on = 1, s->thr_block = 0, s->thr_idle = 0, s->thr_cm = 0, s->thr_clear = 0, s->thr_refused_once = 0;
        s->thr_y0 = in->root.y, s->thr_ons++;
        o->events2 |= SOMVR_CLIMB_E_THR_ON;
    }
    s->thr_clear = in->rock_ok && in->rock_cm > -1.0f ? s->thr_clear + 1 : 0;
    if (s->thr_clear >= 2 && s->thr_cm >= 10.0f) { /* through it and out of the rock */
        thr_off(s, o, SOMVR_CLIMB_THR_CLEAR);
        return;
    }
    if (s->thr_cm >= c->thr_max_cm) {
        thr_off(s, o, SOMVR_CLIMB_THR_MAX);
        return;
    }
    float v = m->y;
    if (!(fabsf(v) > 0.05f)) {
        s->thr_idle += dts;
        if (s->thr_idle >= 0.5f) thr_off(s, o, SOMVR_CLIMB_THR_IDLE);
        return;
    }
    s->thr_idle = 0;
    if (!thr_clear(s, c, in, k, ray, ctx, o, v, 0)) { /* the head would go into the rock: blocked as ever */
        s->thr_refused++;
        if (!s->thr_refused_once) s->thr_refused_once = 1, o->events2 |= SOMVR_CLIMB_E_THR_REFUSED;
        return;
    }
    o->through_up = v;
    s->thr_cm += fabsf(v), s->thr_all_cm += fabsf(v), s->thr_frames++;
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

/* drop mark: room for Talion standing on floor point fl (nothing within 180 cm over it, nothing within PO_ROOM_CM round
 * him at 50 and 140 cm; u: the way out, level unit or 0) */
static int dm_room(somvr_climb *s, somvr_climb_out *o, somvr_climb_ray_fn ray, void *ctx, sv_vec3 fl, sv_vec3 u) {
    somvr_climb_hit r;
    if (!cast(s, o, ray, ctx, 0, add3(fl, (sv_vec3){ 0, 10, 0 }), (sv_vec3){ 0, 1, 0 }, 180, &r) || r.hit) return 0;
    if (len3(u) < 0.5f) u = (sv_vec3){ 1, 0, 0 };
    sv_vec3 side = { -u.z, 0, u.x };
    const sv_vec3 dirs[4] = { u, mul3(u, -1.0f), side, mul3(side, -1.0f) };
    for (int hh = 0; hh < 2; hh++)
        for (int j = 0; j < 4; j++)
            if (!cast(s, o, ray, ctx, 0, add3(fl, (sv_vec3){ 0, hh ? 140.0f : 50.0f, 0 }), dirs[j], PO_ROOM_CM, &r) || r.hit)
                return 0;
    return 1;
}

/* drop mark: the landing for an A now (see the header's "mark"): climbing (from the wall out, or beyond the lip while
 * still on its top) or on foot (the edge ahead along the view, as edge_blink). The first spot (blink_out_cm out)
 * decides: no ground, water, the cast's no answer = none; a short drop = on foot none, climbing the fall (the ring on
 * the ground under the root); else that spot and every 20 cm out to dm_out_cm, the first with room for Talion and as
 * high a drop, and no enemy there. s->dm_kind, dm_land, dm_top, dm_to, dm_out; bl_why (NONE: shown), bl_h, bl_from,
 * bl_ground. Returns dm_kind */
static int dm_find0(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                    somvr_climb_out *o, int climbing) {
    s->dm_kind = SOMVR_CLIMB_DM_NONE, s->bl_why = SOMVR_CLIMB_BL_NONE, s->bl_h = -1, s->dm_out = 0;
    s->dm_tests++;
    if (!in->root_ok || !(c->blink_cm > 0)) {
        s->bl_why = climbing ? SOMVR_CLIMB_BL_CANNOT : SOMVR_CLIMB_BL_NOEDGE;
        return 0;
    }
    sv_vec3 f0, u = { 0, 0, 0 };
    float feet, y0;
    if (climbing) {
        sv_vec3 ou = { -s->wall_tw.x, 0, -s->wall_tw.z };
        feet = in->root.y - c->foot_cm;
        if (s->descending && feet >= s->lip_top_y - 10.0f) { /* on the top: from beyond the edge */
            f0 = s->lip_edge, u = s->lip_out, feet = s->lip_top_y, y0 = s->lip_top_y + 30.0f;
        } else {
            f0 = in->root, y0 = in->root.y;
            if (len3(ou) > 0.5f) u = unit3(ou);
        }
    } else { /* the edge ahead along the view (as edge_blink) */
        sv_vec3 f = { in->head_fwd.x, 0, in->head_fwd.z };
        s->bl_why = SOMVR_CLIMB_BL_NOEDGE;
        if (len3(f) < 0.5f || !(c->edge_look_cm > 0)) return 0;
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
        s->bl_why = SOMVR_CLIMB_BL_NONE;
        f0 = add3(in->root, mul3(f, e)), u = f, feet = fl, y0 = fl + 60.0f;
    }
    float d0 = len3(u) > 0.5f ? c->blink_out_cm : 0.0f, d1 = len3(u) > 0.5f ? fmaxf(d0, c->dm_out_cm) : 0.0f;
    int first = 1;
    for (float d = d0; d <= d1 + 0.5f; d += 20.0f) {
        sv_vec3 p = add3(f0, mul3(u, d));
        p.y = y0;
        somvr_climb_hit g;
        int ok = cast(s, o, ray, ctx, 0, p, (sv_vec3){ 0, -1, 0 }, y0 - feet + c->blink_max_cm, &g);
        if (first) {
            first = 0;
            s->bl_from = p;
            if (!ok) {
                s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->bl_cannot++;
                return 0;
            }
            if (!g.hit) {
                s->bl_why = SOMVR_CLIMB_BL_NOGROUND, s->bl_noground++;
                return 0;
            }
            s->bl_ground = g.point, s->bl_h = feet - g.point.y;
            if (g.wet) {
                s->bl_why = SOMVR_CLIMB_BL_WET, s->bl_wet++;
                return 0;
            }
            if (!(s->bl_h > c->blink_cm)) { /* a short drop: on foot nothing; climbing the fall, straight down */
                s->bl_why = SOMVR_CLIMB_BL_SHORT, s->bl_short++;
                if (!climbing) return 0;
                sv_vec3 land = g.point;
                somvr_climb_hit r;
                if (cast(s, o, ray, ctx, 0, in->root, (sv_vec3){ 0, -1, 0 }, c->foot_cm + c->blink_cm + 60.0f, &r) && r.hit &&
                    !r.wet)
                    land = r.point;
                if (in->enemy_near && in->enemy_near(in->enemy_ctx, add3(land, (sv_vec3){ 0, c->foot_cm, 0 }), c->dm_enemy_cm)) {
                    s->bl_why = SOMVR_CLIMB_BL_ENEMY, s->dm_enemies++;
                    return 0;
                }
                s->dm_land = land, s->dm_top = (sv_vec3){ land.x, fmaxf(feet, land.y), land.z };
                return s->dm_kind = SOMVR_CLIMB_DM_FALL;
            }
        } else if (!ok || !g.hit || g.wet || !(feet - g.point.y > c->blink_cm)) {
            continue; /* further out: only a like landing */
        }
        if (!dm_room(s, o, ray, ctx, g.point, u)) continue;
        sv_vec3 to = { g.point.x, g.point.y + c->foot_cm + 5.0f, g.point.z };
        s->bl_ground = g.point, s->bl_h = feet - g.point.y;
        if (in->enemy_near && in->enemy_near(in->enemy_ctx, to, c->dm_enemy_cm)) {
            s->bl_why = SOMVR_CLIMB_BL_ENEMY, s->dm_enemies++;
            return 0;
        }
        if (!in->can_blink) {
            s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->bl_cannot++;
            return 0;
        }
        s->dm_land = g.point, s->dm_top = (sv_vec3){ g.point.x, feet, g.point.z }, s->dm_to = to, s->dm_out = d - d0;
        s->bl_to = to;
        return s->dm_kind = SOMVR_CLIMB_DM_BLINK;
    }
    s->bl_why = SOMVR_CLIMB_BL_NOROOM, s->dm_norooms++;
    return 0;
}
/* unstuck (2): climbing with no blink landing, the plain fall straight down (the ground under the root, not water) */
static int dm_find(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                   somvr_climb_out *o, int climbing) {
    int k = dm_find0(s, c, in, ray, ctx, o, climbing);
    if (k || !climbing || !c->unstuck || !in->root_ok || !(c->blink_cm > 0)) return k;
    somvr_climb_hit g;
    float feet = in->root.y - c->foot_cm;
    if (!cast(s, o, ray, ctx, 0, in->root, (sv_vec3){ 0, -1, 0 }, c->foot_cm + c->blink_max_cm, &g) || !g.hit || g.wet)
        return 0; /* no ground under him either (or water): nothing, as before */
    s->dm_land = g.point, s->dm_top = (sv_vec3){ g.point.x, fmaxf(feet, g.point.y), g.point.z }, s->dm_out = 0;
    s->dm_plain_falls++;
    return s->dm_kind = SOMVR_CLIMB_DM_FALL;
}

static void dm_cancel(somvr_climb *s, int why, somvr_climb_out *o) {
    s->dm = 0, s->dm_kind = SOMVR_CLIMB_DM_NONE, s->dm_cancel_why = why, s->dm_cancels++;
    if (why == SOMVR_CLIMB_DMC_STICK) s->dm_stick_wait = 1;
    o->events2 |= SOMVR_CLIMB_E_DM_CANCEL;
}

/* drop mark (see the header's "mark"): press = the landing shown, held = cancels and (climbing) the landing kept up to
 * date, let go = go (o->blink, or the fall: the climb's end DROP). 1 = climbing, the go's blink: the phase blink */
static int dm_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                   somvr_climb_out *o, int press, int held) {
    int climbing = s->active && (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG ||
                                 s->phase == SOMVR_CLIMB_PH_FLING);
    o->dm_armed_s = -1.0f;
    if (s->dm_stick_wait && in->stick < 0.3f) s->dm_stick_wait = 0;
    if (s->dm_stick_held && in->stick < 0.3f) s->dm_stick_held = 0; /* unstuck: back: a tilt from now cancels */
    if (s->dm == 2 && (press || in->t - s->dm_t0 >= c->dm_show_s || (s->dm_climb ? !climbing : s->active)))
        s->dm = 0, s->dm_kind = SOMVR_CLIMB_DM_NONE; /* a tap's ring: its time is up, or he moved on */
    if (s->dm == 1) { /* A held: the cancels */
        int why = SOMVR_CLIMB_DMC_NONE;
        uint32_t seq = 0;
        for (int h = 0; h < 2; h++)
            if (s->h[h].holding && s->h[h].seq > seq) seq = s->h[h].seq;
        if (!in->live || !in->allow || in->nograb) why = SOMVR_CLIMB_DMC_GAME;
        else if (s->dm_climb ? !climbing : s->active) why = SOMVR_CLIMB_DMC_END;
        else if (s->dm_climb && seq > s->dm_seq) why = SOMVR_CLIMB_DMC_GRAB;
        else if (in->stick >= c->dm_stick && !s->dm_stick_held) why = SOMVR_CLIMB_DMC_STICK;
        else if (!s->dm_climb && in->root_ok && len3(sub3(in->root, s->dm_root)) > c->dm_move_cm) why = SOMVR_CLIMB_DMC_MOVED;
        if (why) dm_cancel(s, why, o);
    }
    if (s->dm == 1 && !held) { /* the let-go */
        int kind = s->dm_kind;
        if (in->t - s->dm_t0 < c->dm_min_s) { /* a tap: nothing; its ring stays a moment */
            s->dm = kind ? 2 : 0;
            if (kind) s->dm_taps++, o->events2 |= SOMVR_CLIMB_E_DM_TAP;
        } else if (kind == SOMVR_CLIMB_DM_BLINK && in->can_blink) {
            s->dm = 0, s->dm_goes++, s->blinks++;
            s->bl_to = s->dm_to;
            o->events2 |= SOMVR_CLIMB_E_DM_GO;
            o->events |= SOMVR_CLIMB_E_BLINK | (s->dm_climb ? 0u : SOMVR_CLIMB_E_EDGE);
            o->blink = 1, o->blink_to = s->dm_to;
            if (s->dm_climb) {
                for (int h = 0; h < 2; h++) let_go(&s->h[h]), s->h[h].armed = 0;
                s->phase = SOMVR_CLIMB_PH_BLINK, s->phase_t = in->t, s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
                o->active = 1, o->driver = -1;
                return 1;
            }
            s->edge_blinks++; /* on foot: nothing else changes (the practice trip's blink moves him) */
            return 0;
        } else if (kind == SOMVR_CLIMB_DM_FALL) {
            s->dm = 0, s->dm_goes++, s->dm_falls++, s->drops++;
            o->events2 |= SOMVR_CLIMB_E_DM_GO;
            o->events |= SOMVR_CLIMB_E_DROP;
            end_climb(s, SOMVR_CLIMB_END_DROP, 1, o);
            return 0;
        } else { /* no landing now (gone while held, or no blink now): nothing */
            s->dm = 0, s->dm_kind = SOMVR_CLIMB_DM_NONE;
            if (kind == SOMVR_CLIMB_DM_BLINK) s->bl_why = SOMVR_CLIMB_BL_CANNOT, s->dm_nos++, o->events2 |= SOMVR_CLIMB_E_DM_NO;
        }
    }
    if (s->dm == 1 && s->dm_climb && in->root_ok && len3(sub3(in->root, s->dm_root)) > 10.0f &&
        in->t - s->dm_test_t >= 0.1) { /* climbing: the landing under him again */
        int k0 = s->dm_kind;
        sv_vec3 l0 = s->dm_land;
        s->dm_root = in->root, s->dm_test_t = in->t;
        int k = dm_find(s, c, in, ray, ctx, o, 1);
        if (k != k0 || len3(sub3(s->dm_land, l0)) > 5.0f) o->events2 |= SOMVR_CLIMB_E_DM_MOVE;
        if (!k && k0) s->dm_nos++, o->events2 |= SOMVR_CLIMB_E_DM_NO; /* gone: the buzz (a let-go now does nothing) */
        if (!k) s->dm_armed = 0;
        if (c->dm_timer && k && !k0) s->dm_t0 = in->t, s->dm_armed = 0, s->dm_restarts++; /* timer: back: from the start */
    }
    if (press && s->dm != 1 && (climbing || (!s->active && in->allow && !in->nograb))) { /* A's press: the landing */
        s->dm_presses++;
        s->dm_climb = climbing, s->dm_t0 = in->t, s->dm_test_t = in->t, s->dm_root = in->root, s->dm_seq = 0;
        s->dm_armed = 0, s->dm_stick_held = c->unstuck && in->stick >= 0.3f; /* unstuck: a stick already held is no cancel */
        s->dm_stick_kept += s->dm_stick_held;
        for (int h = 0; h < 2; h++)
            if (s->h[h].holding && s->h[h].seq > s->dm_seq) s->dm_seq = s->h[h].seq;
        int k = dm_find(s, c, in, ray, ctx, o, climbing);
        if (!climbing) o->events |= SOMVR_CLIMB_E_EDGE;
        if (k) {
            s->dm = 1, s->dm_shows++;
            o->events2 |= SOMVR_CLIMB_E_DM_SHOW;
        } else if (climbing || (s->bl_why != SOMVR_CLIMB_BL_NOEDGE && s->bl_why != SOMVR_CLIMB_BL_SHORT)) {
            s->dm = 1, s->dm_nos++; /* held with nothing to show: a let-go does nothing; the cancels as ever */
            o->events2 |= SOMVR_CLIMB_E_DM_NO;
        } else {
            o->events |= SOMVR_CLIMB_E_BLINK_NONE; /* on foot: no edge or a short one: nothing, as before */
        }
    }
    if (s->dm == 1 && s->dm_kind && !s->dm_armed && in->t - s->dm_t0 >= c->dm_min_s) { /* timer: full: armed */
        s->dm_armed = 1, s->dm_armeds++;
        o->events2 |= SOMVR_CLIMB_E_DM_ARMED;
    }
    if ((s->dm == 1 || s->dm == 2) && s->dm_kind) {
        o->dm_show = 1, o->dm_land = s->dm_land, o->dm_top = s->dm_top;
        o->dm_held = s->dm == 1 ? fminf((float)(in->t - s->dm_t0) / fmaxf(c->dm_min_s, 1e-3f), 1.0f) : 0.0f;
        if (s->dm == 1 && s->dm_armed) o->dm_armed_s = (float)(in->t - s->dm_t0 - c->dm_min_s);
    }
    return 0;
}

/* A mantle (see the header's "A mantle"): the top Talion is at - the push's (topped) or the newest hold's own found
 * top - its edge straight ahead of him (his root put on the edge's line) and into the top (level unit); 1 = a top */
#define AM_HI_CM 230.0f /* the eyes at most this over the top (the feet ~50 cm over it: no mantle, he stands) */
#define AM_OH_LO_CM 120.0f  /* build 22, the overhang: the eyes at most this under its top (the hand on its lip in reach) */
#define AM_OH_DEPTH_CM 15.0f /* ... the face under the lip at least this behind the lip's edge (an overhang, not a ledge) */
#define AM_OH_OUT_CM 45.0f  /* ... the root this far out of the edge before it rises (his capsule round the lip) */
#define AM_OH_R_CM 35.0f    /* ... his capsule's radius (PO_ROOM_CM 33 and 2 to spare) */
#define AM_LAG_CM 25.0f /* the glide: the root this far behind the plan ... */
#define AM_LAG_S 0.25f  /* ... this long = blocked */
#define AM_LAG_FRAMES 2.5f /* build 22: ... or this many frames' worth of the plan, the larger (the game takes a request a
                            * frame late: headless at 13 fps that is 27 cm - run 20261029-003708's down over was "held"
                            * after 0.39 s with nothing in the way; at 90 Hz 25 cm stays the larger) */
static int am_top(const somvr_climb *s, const somvr_climb_in *in, float *ty, sv_vec3 *e, sv_vec3 *tw) {
    sv_vec3 hit, t;
    float y;
    if (s->topped) {
        hit = s->top_hit, t = s->top_toward, y = s->top_y;
    } else {
        int d = -1;
        for (int h = 0; h < 2; h++)
            if (s->h[h].holding && s->h[h].edge && s->h[h].top_found && (d < 0 || s->h[h].seq > s->h[d].seq)) d = h;
        if (d < 0) return 0;
        hit = s->h[d].hit, t = s->h[d].toward, y = s->h[d].top_y;
    }
    t.y = 0;
    if (len3(t) < 0.3f && in->head_ok) t = (sv_vec3){ hit.x - in->head_x, 0, hit.z - in->head_z }; /* a hold on the top */
    if (len3(t) < 0.2f || !in->root_ok) return 0;
    t = unit3(t);
    sv_vec3 side = { -t.z, 0, t.x };
    float a = (in->root.x - hit.x) * side.x + (in->root.z - hit.z) * side.z;
    *e = (sv_vec3){ hit.x + side.x * a, y, hit.z + side.z * a }, *tw = t, *ty = y;
    return 1;
}

/* build 22: an overhang's lip held from below - the face under it (a ray in along tw from AM_OH_OUT_CM out of the edge,
 * 20 cm under the eyes, at most 60 cm under the top) at least AM_OH_DEPTH_CM behind the edge, and the eyes under the top
 * by no more than AM_OH_LO_CM. 1 = an overhang */
static int am_oh_test(somvr_climb *s, const somvr_climb_in *in, float ty, sv_vec3 e, sv_vec3 tw, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *o) {
    if (!in->head_ok || in->head_y < ty - AM_OH_LO_CM) return 0;
    somvr_climb_hit r;
    sv_vec3 from = { e.x - tw.x * AM_OH_OUT_CM, fminf(in->head_y - 20.0f, ty - 60.0f), e.z - tw.z * AM_OH_OUT_CM };
    if (!cast(s, o, ray, ctx, -1, from, tw, AM_OH_OUT_CM + AM_OH_DEPTH_CM, &r)) return 0;
    return !r.hit;
}

/* build 22: the overhang's glide - out level from the root p0 until his capsule (AM_OH_R_CM round the root) is
 * AM_OH_OUT_CM - AM_OH_R_CM out of the lip's edge (one quadratic: out first, then 10 cm up), then a cubic up and in to the
 * landing's root p2 whose two control points are straight over the out point at one height h: it rises straight up first
 * (no corner where they meet) and comes in late (u - uo grows as t^3), h so high that his feet are am_clear_cm over the
 * top when the capsule's front reaches the lip's edge */
static void am_plan_oh(somvr_climb *s, const somvr_climb_cfg *c, sv_vec3 p0, sv_vec3 p2) {
    float u0 = (p0.x - s->am_edge.x) * s->am_in.x + (p0.z - s->am_edge.z) * s->am_in.z;
    float uo = fminf(u0, -AM_OH_OUT_CM), u2 = (p2.x - s->am_edge.x) * s->am_in.x + (p2.z - s->am_edge.z) * s->am_in.z;
    sv_vec3 a = { p0.x + s->am_in.x * (uo - u0), p0.y, p0.z + s->am_in.z * (uo - u0) }; /* out, level */
    sv_vec3 m = { a.x, p0.y + 10.0f, a.z };
    float need = s->am_top_y + c->am_clear_cm + c->foot_cm, h1 = fmaxf(p2.y, need) + 20.0f;
    if (u2 > -AM_OH_R_CM && uo < -AM_OH_R_CM) { /* the capsule's front at the edge: uo + t^3 (u2 - uo) = -R */
        float t = cbrtf((-AM_OH_R_CM - uo) / (u2 - uo)), w0 = (1 - t) * (1 - t) * (1 - t), w3 = t * t * t;
        float b = 3 * (1 - t) * t;
        if (b > 0.05f) h1 = fmaxf(h1, (need - w0 * m.y - w3 * p2.y) / b);
    }
    h1 = fminf(h1, fmaxf(m.y, p2.y) + 300.0f);
    sv_vec3 q = { a.x, h1, a.z }, q2 = { a.x, h1, a.z };
    const int H = SOMVR_CLIMB_AM_N / 4; /* the out leg's pieces (short), the rest up and in */
    s->am_len[0] = 0;
    for (int i = 0; i <= SOMVR_CLIMB_AM_N; i++) {
        sv_vec3 pt;
        if (i <= H) {
            float t = (float)i / H, aa = (1 - t) * (1 - t), b = 2 * (1 - t) * t, d = t * t;
            pt = add3(add3(mul3(p0, aa), mul3(a, b)), mul3(m, d)); /* control a: out first, then up */
        } else {
            float t = (float)(i - H) / (SOMVR_CLIMB_AM_N - H), w0 = (1 - t) * (1 - t) * (1 - t);
            float w1 = 3 * (1 - t) * (1 - t) * t, w2 = 3 * (1 - t) * t * t, w3 = t * t * t;
            pt = add3(add3(mul3(m, w0), mul3(q, w1)), add3(mul3(q2, w2), mul3(p2, w3)));
        }
        s->am_pt[i] = pt;
        if (i) s->am_len[i] = s->am_len[i - 1] + len3(sub3(s->am_pt[i], s->am_pt[i - 1]));
    }
}

/* the glide's curve from the root p0 to the landing's root p2: a quadratic one whose control point is over p0 (up first,
 * then in), so high that the feet cross the edge (u = 0 along tw from e) am_clear_cm over the top; s->am_pt, am_len */
static void am_plan(somvr_climb *s, const somvr_climb_cfg *c, sv_vec3 p0, sv_vec3 p2) {
    float u0 = (p0.x - s->am_edge.x) * s->am_in.x + (p0.z - s->am_edge.z) * s->am_in.z;
    float u2 = (p2.x - s->am_edge.x) * s->am_in.x + (p2.z - s->am_edge.z) * s->am_in.z;
    float need = s->am_top_y + c->am_clear_cm + c->foot_cm, h1 = fmaxf(fmaxf(p0.y, p2.y), need);
    if (u0 < 0 && u2 > 0) { /* the crossing at t^2 = -u0 / (u2 - u0): h(t) there = need */
        float t = sqrtf(-u0 / (u2 - u0)), a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, d = t * t;
        if (b > 0.05f) h1 = fmaxf(fmaxf(p0.y, p2.y), (need - a * p0.y - d * p2.y) / b);
    }
    h1 = fminf(h1, fmaxf(p0.y, p2.y) + 250.0f);
    sv_vec3 p1 = { p0.x, h1, p0.z };
    s->am_len[0] = 0;
    for (int i = 0; i <= SOMVR_CLIMB_AM_N; i++) {
        float t = (float)i / SOMVR_CLIMB_AM_N, a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, d = t * t;
        s->am_pt[i] = add3(add3(mul3(p0, a), mul3(p1, b)), mul3(p2, d));
        if (i) s->am_len[i] = s->am_len[i - 1] + len3(sub3(s->am_pt[i], s->am_pt[i - 1]));
    }
}
/* the point dist cm along the curve */
static sv_vec3 am_at(const somvr_climb *s, float dist) {
    for (int i = 1; i <= SOMVR_CLIMB_AM_N; i++) {
        float l0 = s->am_len[i - 1], l1 = s->am_len[i];
        if (dist <= l1 || i == SOMVR_CLIMB_AM_N) {
            float f = l1 > l0 ? fminf(fmaxf((dist - l0) / (l1 - l0), 0.0f), 1.0f) : 1.0f;
            return add3(s->am_pt[i - 1], mul3(sub3(s->am_pt[i], s->am_pt[i - 1]), f));
        }
    }
    return s->am_pt[SOMVR_CLIMB_AM_N];
}

/* the search: the eyes high enough, a landing straight ahead on the top, the way there clear (the head along the curve,
 * his capsule at the knees and the hips over the top from the edge's crossing); 1 = available (s->am_land, am_edge,
 * am_in, am_top_y and the plan in am_pt), else s->am_why. A top at all: am_top (the caller) */
static int am_find(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                   somvr_climb_out *o) {
    float ty, dbg[8] = { 0 };
    sv_vec3 e, tw, fl;
    memset(s->am_dbg, 0, sizeof s->am_dbg);
    s->am_oh = 0;
    if (!in->head_ok || !am_top(s, in, &ty, &e, &tw)) return s->am_why = SOMVR_CLIMB_AM_NOFLOOR, 0;
    s->am_dbg[0] = in->head_y - ty;
    if (in->head_y < ty + c->am_head_cm && c->am_overhang && am_oh_test(s, in, ty, e, tw, ray, ctx, o))
        s->am_oh = 1; /* build 22: an overhang's lip from below */
    else if (in->head_y < ty + c->am_head_cm) return s->am_why = SOMVR_CLIMB_AM_LOW, 0;
    if (in->head_y > ty + AM_HI_CM) return s->am_why = SOMVR_CLIMB_AM_HIGH, 0;
    dbg[2] = -1e9f;
    int found = land_find(s, o, ray, ctx, -1, e, tw, ty, dbg, &fl);
    s->am_dbg[1] = dbg[1], s->am_dbg[2] = dbg[3] + dbg[4] + dbg[5] + dbg[6];
    if (!found) return s->am_why = dbg[1] > 0 ? SOMVR_CLIMB_AM_NOROOM : SOMVR_CLIMB_AM_NOFLOOR, 0;
    s->am_top_y = ty, s->am_edge = e, s->am_in = tw, s->am_land = fl;
    if (s->am_oh)
        am_plan_oh(s, c, in->root, (sv_vec3){ fl.x, fl.y + c->foot_cm + 1.0f, fl.z });
    else
        am_plan(s, c, in->root, (sv_vec3){ fl.x, fl.y + c->foot_cm + 1.0f, fl.z });
    /* the head's way along the curve (four pieces, the head as far off the root as now) */
    sv_vec3 hoff = { in->head_x - in->root.x, in->head_y - in->root.y, in->head_z - in->root.z };
    int bad = 0;
    for (int i = 0; i < SOMVR_CLIMB_AM_N && !bad; i += SOMVR_CLIMB_AM_N / 4)
        if (!way_clear(s, c, add3(s->am_pt[i], hoff), tw, ray, ctx, o,
                       sub3(s->am_pt[i + SOMVR_CLIMB_AM_N / 4], s->am_pt[i])))
            bad = 1, s->am_dbg[3]++;
    /* the capsule over the top: from where the curve crosses the edge (or from the root, over the top already) in to the
     * landing and his capsule's radius past it, at the knees and the hips */
    int ic = 0;
    for (int i = 0; i <= SOMVR_CLIMB_AM_N; i++)
        if ((s->am_pt[i].x - e.x) * tw.x + (s->am_pt[i].z - e.z) * tw.z >= 0) {
            ic = i;
            break;
        }
    sv_vec3 pc = s->am_pt[ic], p2 = s->am_pt[SOMVR_CLIMB_AM_N];
    float run = (p2.x - pc.x) * tw.x + (p2.z - pc.z) * tw.z + PO_ROOM_CM;
    for (int k = 0; k < 2 && !bad && run > 1.0f; k++) {
        somvr_climb_hit r;
        sv_vec3 from = { pc.x, pc.y - c->foot_cm + (k ? c->foot_cm : 40.0f), pc.z };
        if (!cast(s, o, ray, ctx, -1, from, tw, run, &r) || r.hit) bad = 1, s->am_dbg[4]++;
    }
    if (bad) return s->am_why = SOMVR_CLIMB_AM_NOWAY, 0;
    return s->am_why = SOMVR_CLIMB_AM_OK, 1;
}

/* while holding or hanging: the landing shown when A would mantle (searched again when the root moved 5 cm, at most
 * every 0.1 s, and every 0.5 s) */
static void am_avail(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                     somvr_climb_out *o) {
    float ty;
    sv_vec3 e, tw;
    if (!in->head_ok || !in->root_ok || s->dm == 1 || !am_top(s, in, &ty, &e, &tw) ||
        (in->head_y < ty + c->am_head_cm && !(c->am_overhang && in->head_y >= ty - AM_OH_LO_CM)) ||
        in->head_y > ty + AM_HI_CM) {
        s->am_ok = 0, s->am_test_t = 0;
        return;
    }
    int was = s->am_ok;
    if (s->am_test_t <= 0 || in->t - s->am_test_t >= 0.5 ||
        (in->t - s->am_test_t >= 0.1 && len3(sub3(in->root, s->am_test_root)) >= 5.0f)) {
        s->am_test_t = in->t, s->am_test_root = in->root;
        s->am_ok = am_find(s, c, in, ray, ctx, o);
    }
    if (!s->am_ok) return;
    o->am_show = 1, o->am_land = s->am_land;
    if (!was) s->am_shows++, s->am_oh_shows += s->am_oh, o->events2 |= SOMVR_CLIMB_E_AM_SHOW;
}

/* A's press while climbing: at a top (the eyes top_cm over it) = the mantle (1: the glide or the blink started) or, no
 * way, nothing but the why (-1: E_AM_NO); not at a top = 0 (A as ever) */
static int am_press(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                    somvr_climb_out *o) {
    float ty;
    sv_vec3 e, tw;
    if (s->descending || !in->head_ok || !am_top(s, in, &ty, &e, &tw)) return 0;
    /* under the top: A as ever (the drop mark) - unless it is an overhang's lip held from below (build 22) */
    if (in->head_y < ty + c->top_cm && !(c->am_overhang && am_oh_test(s, in, ty, e, tw, ray, ctx, o))) return 0;
    s->am_presses++;
    int ok = am_find(s, c, in, ray, ctx, o);
    if (ok && c->mantle == 2 && !in->can_blink) ok = 0, s->am_why = SOMVR_CLIMB_AM_NOBLINK;
    if (!ok) {
        s->am_nos++;
        s->am_lows += s->am_why == SOMVR_CLIMB_AM_LOW || s->am_why == SOMVR_CLIMB_AM_HIGH;
        s->am_nofloors += s->am_why == SOMVR_CLIMB_AM_NOFLOOR || s->am_why == SOMVR_CLIMB_AM_NOROOM;
        s->am_noways += s->am_why == SOMVR_CLIMB_AM_NOWAY;
        o->events2 |= SOMVR_CLIMB_E_AM_NO;
        return -1;
    }
    for (int h = 0; h < 2; h++) let_go(&s->h[h]), s->h[h].armed = 0; /* the grips must open before a new hold */
    s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1, s->po_hand = -1, s->am_ok = 0, s->am_test_t = 0;
    s->phase_t = in->t, s->am_goes++, s->am_oh_goes += s->am_oh;
    o->events2 |= SOMVR_CLIMB_E_AM_GO;
    o->active = 1, o->driver = -1;
    if (c->mantle == 2) { /* the pullover's quick blink onto the landing */
        s->bl_to = (sv_vec3){ s->am_land.x, s->am_land.y + c->foot_cm + 5.0f, s->am_land.z };
        s->mt_top = s->am_edge, s->mt_floor = s->am_land;
        s->phase = SOMVR_CLIMB_PH_BLINK, s->blinks++, s->am_blinks++;
        o->blink = 1, o->blink_to = s->bl_to;
        return 1;
    }
    s->phase = SOMVR_CLIMB_PH_MANTLE, s->am_t0 = in->t, s->am_lag_s = 0, s->am_end = SOMVR_CLIMB_AME_NONE;
    return 1;
}

/* the glide: each frame the root toward its place on the curve (am_cms along it from the press), the move through the
 * rock 1:1 while the head's way is clear (as topout's); at the curve's end = over (he stands); blocked = he stands
 * if the top is under him, else the hang */
static void am_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                    somvr_climb_out *o, double dt) {
    float L = s->am_len[SOMVR_CLIMB_AM_N], v = fmaxf(c->am_cms, 50.0f);
    float d = fminf((float)(in->t - s->am_t0) * v, L);
    if (!in->root_ok) return; /* nothing this frame (the clock goes on: the next one catches up) */
    sv_vec3 off = sub3(am_at(s, d), in->root);
    float lag = len3(off), dts = dt > 0 && dt < 0.2 ? (float)dt : 0.0f;
    int done = d >= L && (lag < 1.0f || in->t - s->am_t0 > L / v + 0.3);
    s->am_lag_s = lag > fmaxf(AM_LAG_CM, AM_LAG_FRAMES * v * dts) ? s->am_lag_s + dts : 0.0f;
    if (done || s->am_lag_s >= AM_LAG_S) {
        float o1[1] = { 0 }, fy;
        int at;
        o->events2 |= SOMVR_CLIMB_E_AM_END;
        if (done || stand_floor(s, c, in, ray, ctx, o, o1, 1, c->stand_down_cm, STAND_LET_UP_CM, &fy, &at)) {
            s->am_end = done ? SOMVR_CLIMB_AME_DONE : SOMVR_CLIMB_AME_BLOCKED;
            s->am_dones += done, s->am_blocks += !done;
            end_climb(s, SOMVR_CLIMB_END_OVER, 1, o);
        } else {
            s->am_end = SOMVR_CLIMB_AME_HANG, s->am_blocks++;
            hang(s, in, 1, o);
        }
        return;
    }
    sv_vec3 m = somvr_climb_clamp(off, 40.0f);
    if (!(len3(m) > 0.01f)) return;
    request(s, o, m);
    if (o->move && way_clear(s, c, (sv_vec3){ in->head_x, in->head_y, in->head_z }, s->am_in, ray, ctx, o, m))
        o->through_up = m.y, o->through_lx = m.x, o->through_lz = m.z;
    s->am_frames++, s->am_cm += len3(m);
}

/* down (see the header): the grip of hand h on foot reaching down over an edge: the edge along the line from the root
 * through the hand (down rays every 5 cm: the last floor within 25 cm of the feet), the drop beyond it, the face under
 * the lip (a ray back toward it 60 cm under the top), the hang's spot (dn_gap_cm out from the face, the eyes dn_under_cm
 * under the top), the way (the head out over the edge, then down: way_clear); 1 = the glide planned (s->am_pt), else
 * s->dn_why */
#define DN_FACE_SPAN_CM 30.0f /* down: the face under the lip within this (out) of itself and of the edge: a wall */
static int dn_find(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                   void *ctx, somvr_climb_out *o) {
    const somvr_climb_hand *hd = &in->hand[h];
    sv_vec3 p = hd->pw;
    float fl = in->root.y - c->foot_cm;
    s->dn_why_cm = p.y - fl;
    if (!in->head_ok || !in->root_ok || !fin3(p) || p.y > in->head_y - c->dn_low_cm || p.y - fl > c->dn_high_cm)
        return s->dn_why = SOMVR_CLIMB_DN_HIGH, 0;
    sv_vec3 u = { p.x - in->root.x, 0, p.z - in->root.z };
    if (len3(u) < 10.0f) u = (sv_vec3){ in->head_fwd.x, 0, in->head_fwd.z }; /* the hand over the feet: the view's way */
    if (len3(u) < 0.3f) return s->dn_why = SOMVR_CLIMB_DN_NOEDGE, 0;
    u = unit3(u);
    float hd0 = (p.x - in->root.x) * u.x + (p.z - in->root.z) * u.z, last = -1, ty = fl, d;
    somvr_climb_hit r;
    for (d = 10; d <= hd0 + c->dn_reach_cm + 0.5f; d += 5) { /* the floor he stands on, out along u, until it ends */
        sv_vec3 f = add3(in->root, mul3(u, d));
        f.y = fl + 40.0f;
        if (!cast(s, o, ray, ctx, h, f, (sv_vec3){ 0, -1, 0 }, 70, &r)) return s->dn_why = SOMVR_CLIMB_DN_NOEDGE, 0;
        if (r.hit && fabsf(r.point.y - fl) <= 25.0f) {
            last = d, ty = r.point.y;
            continue;
        }
        if (r.hit && r.point.y > fl + 25.0f) return s->dn_why = SOMVR_CLIMB_DN_NOEDGE, 0; /* a step up, a wall */
        break;
    }
    float ed = last + 2.5f;
    if (last < 0 || d > hd0 + c->dn_reach_cm + 0.5f || fabsf(ed - hd0) > c->dn_reach_cm)
        return s->dn_why = SOMVR_CLIMB_DN_NOEDGE, 0;
    sv_vec3 e = add3(in->root, mul3(u, ed));
    e.y = ty;
    sv_vec3 f = add3(e, mul3(u, 30.0f));
    f.y = ty + 30.0f;
    if (cast(s, o, ray, ctx, h, f, (sv_vec3){ 0, -1, 0 }, 30.0f + c->lip_drop_cm, &r) && r.hit)
        return s->dn_why_cm = ty - r.point.y, s->dn_why = SOMVR_CLIMB_DN_SHORT, 0;
    /* the face under the lip, from 40 cm under the top down to the hang's feet: a wall to hang on (its points within
     * DN_FACE_SPAN_CM out of each other; run 222900: the meadow bank's face slopes out under its lip, the glide went 37
     * cm down into it and held him in the air beside the edge) - the hang dn_gap_cm out from its farthest point */
    float hang_eyes = ty - c->dn_under_cm, feet_end = hang_eyes - (in->head_y - in->root.y) - c->foot_cm;
    float fo = -1e9f, fo_min = 1e9f;
    for (float y = ty - 40.0f; y >= feet_end - 0.5f; y -= 40.0f) {
        f = add3(e, mul3(u, 120.0f));
        f.y = y;
        if (!cast(s, o, ray, ctx, h, f, mul3(u, -1.0f), 200, &r)) return s->dn_why = SOMVR_CLIMB_DN_NOFACE, 0;
        if (!r.hit) { /* no face at this height (an undercut): the first one must be there */
            if (y > ty - 41.0f) return s->dn_why = SOMVR_CLIMB_DN_NOFACE, 0;
            continue;
        }
        float fk = ed + 120.0f - r.dist; /* the face's distance out along u from the root */
        if (fk > fo) fo = fk, s->dn_face = r.point;
        fo_min = fminf(fo_min, fk);
    }
    if (!(fo > -1e8f)) return s->dn_why = SOMVR_CLIMB_DN_NOFACE, 0;
    if (fo - fo_min > DN_FACE_SPAN_CM || fo > ed + DN_FACE_SPAN_CM)
        return s->dn_why_cm = fo - fminf(fo_min, ed), s->dn_why = SOMVR_CLIMB_DN_NOFACE, 0; /* a slope or a ledge under it */
    sv_vec3 to = add3(in->root, mul3(u, fo + c->dn_gap_cm));
    to.y = ty - c->dn_under_cm - (in->head_y - in->root.y);
    sv_vec3 head = { in->head_x, in->head_y, in->head_z }, m1 = sub3((sv_vec3){ to.x, in->root.y, to.z }, in->root);
    sv_vec3 head1 = add3(head, m1), m2 = { 0, to.y - in->root.y, 0 };
    sv_vec3 feet1 = { to.x, fl + 5.0f, to.z }; /* the feet's way down too (the capsule passes there) */
    if (!way_clear(s, c, head, u, ray, ctx, o, m1) || !way_clear(s, c, head1, u, ray, ctx, o, m2) ||
        !way_clear(s, c, feet1, u, ray, ctx, o, (sv_vec3){ 0, feet_end - fl - 5.0f, 0 }))
        return s->dn_why = SOMVR_CLIMB_DN_NOWAY, 0;
    s->dn_lip = (sv_vec3){ p.x - u.x * (hd0 - ed), ty, p.z - u.z * (hd0 - ed) }, s->dn_out = u, s->dn_top_y = ty;
    s->dn_to = to;
    /* the way: out level over the edge (the feet at the top's height: never into it), then straight down */
    sv_vec3 p0 = in->root, q = { to.x, in->root.y, to.z };
    const int n1 = SOMVR_CLIMB_AM_N / 2;
    s->am_len[0] = 0;
    for (int i = 0; i <= SOMVR_CLIMB_AM_N; i++) {
        s->am_pt[i] = i <= n1 ? add3(p0, mul3(sub3(q, p0), (float)i / n1))
                              : add3(q, mul3(sub3(to, q), (float)(i - n1) / (SOMVR_CLIMB_AM_N - n1)));
        if (i) s->am_len[i] = s->am_len[i - 1] + len3(sub3(s->am_pt[i], s->am_pt[i - 1]));
    }
    return s->dn_why = SOMVR_CLIMB_DN_OK, 1;
}
static int dn_try(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, int h, somvr_climb_ray_fn ray,
                  void *ctx, somvr_climb_out *o) {
    s->dn_tries++, s->dn_hand = h;
    if (!dn_find(s, c, in, h, ray, ctx, o)) {
        s->dn_nos += s->dn_why != SOMVR_CLIMB_DN_HIGH;
        return 0;
    }
    for (int j = 0; j < 2; j++) s->h[j].looking = 0, s->h[j].armed = j == h ? s->h[j].armed : 0;
    s->h[h].looking = 0;
    s->active = 1, s->phase = SOMVR_CLIMB_PH_MANTLE, s->phase_t = in->t, s->t0 = in->t, s->carry = (sv_vec3){ 0, 0, 0 };
    s->driver = -1, s->oh_cm = 0, s->hug_max_d = 0, s->topped = 0, s->descending = 0, s->po_hand = -1;
    s->dn = 1, s->dn_hand = h, s->dn_end = 0, s->am_t0 = in->t, s->am_lag_s = 0;
    s->climbs++, s->dn_goes++;
    o->events2 |= SOMVR_CLIMB_E_DN_GO;
    return 1;
}
/* down: the glide: as am_step's (am_cms along the curve, through the rock while the head's way is clear); that grip
 * opened = the cancel (the hang where he is); the curve's end = that hand holds the lip (1:1 from its place now);
 * blocked = the hang */
static void dn_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                    somvr_climb_out *o, double dt) {
    int h = s->dn_hand;
    if (!s->h[h].down) {
        s->dn = 0, s->dn_end = 2, s->dn_hangs++;
        o->events2 |= SOMVR_CLIMB_E_DN_END;
        hang(s, in, 1, o);
        return;
    }
    float L = s->am_len[SOMVR_CLIMB_AM_N], v = fmaxf(c->am_cms, 50.0f);
    float d = fminf((float)(in->t - s->am_t0) * v, L);
    if (!in->root_ok) return;
    sv_vec3 off = sub3(am_at(s, d), in->root);
    float lag = len3(off), dts = dt > 0 && dt < 0.2 ? (float)dt : 0.0f;
    int done = d >= L && (lag < 2.0f || in->t - s->am_t0 > L / v + 0.3);
    s->am_lag_s = lag > fmaxf(AM_LAG_CM, AM_LAG_FRAMES * v * dts) ? s->am_lag_s + dts : 0.0f;
    if (done) { /* that hand holds: the lip is its hold (the top's edge for top / push / the pullover / the mantle) */
        somvr_climb_hold *k = &s->h[h];
        const somvr_climb_hand *hd = &in->hand[h];
        k->holding = 1, k->looking = 0, k->armed = 1, k->seq = ++s->seq, k->hit = s->dn_lip, k->hit_dist = 0;
        k->dir = mul3(s->dn_out, -1.0f), k->toward = k->dir, k->anchor = hd->pw, k->via = 0, k->inside_by = 0;
        k->edge = 1, k->top_found = 1, k->top_y = s->dn_top_y, k->lip = 0, k->face = 0, k->rl = 0;
        k->last_p = hd->p, k->have_last = hd->valid, k->lost_t = -1, k->cue_in = 1;
        vh_reset(k, pose_t(in), hd->p, hd->valid);
        s->phase = SOMVR_CLIMB_PH_HOLD, s->driver = h, s->carry = (sv_vec3){ 0, 0, 0 }, s->wall_tw = k->toward;
        s->dn = 0, s->dn_end = 1, s->dn_holds++, s->grabs++;
        o->events2 |= SOMVR_CLIMB_E_DN_END;
        o->active = 1, o->driver = h;
        return;
    }
    if (s->am_lag_s >= AM_LAG_S) { /* blocked: the hang where he is (A, a grip on the face) */
        s->dn = 0, s->dn_end = 3, s->dn_blocks++;
        o->events2 |= SOMVR_CLIMB_E_DN_END;
        hang(s, in, 1, o);
        return;
    }
    sv_vec3 m = somvr_climb_clamp(off, 40.0f);
    if (!(len3(m) > 0.01f)) return;
    request(s, o, m);
    if (o->move && way_clear(s, c, (sv_vec3){ in->head_x, in->head_y, in->head_z }, s->dn_out, ray, ctx, o, m))
        o->through_up = m.y, o->through_lx = m.x, o->through_lz = m.z;
}

/* the ground within ground_cm under the root (a ray down from it): he stands (a hang there would hold him in the air) */
static int on_ground(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray, void *ctx,
                     somvr_climb_out *o) {
    somvr_climb_hit g;
    if (s->topped && c->stand && in->root_ok && s->phase != SOMVR_CLIMB_PH_HANG) { /* stand: the top under his capsule */
        float o1[1] = { 0 }, fy; /* the floor under his axis (run 110721: from 15 cm out the capsule slid off the edge) */
        int at;
        if (stand_floor(s, c, in, ray, ctx, o, o1, 1, c->stand_down_cm, STAND_LET_UP_CM, &fy, &at)) {
            stand_note(s, c, in, o, fy, at, 0);
            return 1;
        }
        return 0;
    }
    if (!(c->ground_cm > 0) || !in->root_ok || s->phase == SOMVR_CLIMB_PH_HANG || s->topped) return 0; /* at a top: hang */
    return cast(s, o, ray, ctx, 0, in->root, (sv_vec3){ 0, -1, 0 }, c->ground_cm, &g) && g.hit && g.dist <= c->ground_cm;
}

/* stand: the move stick in a topped hang (see the header): 1 = it acted (a walk this frame, or the end: he stands) */
static int stand_walk(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *o, double dt) {
    if (!c->stand || !s->topped || s->descending || !in->root_ok || !(in->stick >= 0.3f)) return 0;
    float n = sqrtf(s->top_toward.x * s->top_toward.x + s->top_toward.z * s->top_toward.z);
    if (n < 0.3f) return 0;
    sv_vec3 tw = { s->top_toward.x / n, 0, s->top_toward.z / n };
    float fy;
    int at;
    float o1[1] = { 0 };
    if (stand_floor(s, c, in, ray, ctx, o, o1, 1, c->stand_down_cm, STAND_LET_UP_CM, &fy, &at)) { /* on it now: he stands */
        stand_note(s, c, in, o, fy, at, 0);
        end_climb(s, SOMVR_CLIMB_END_RELEASED, 0, o);
        return 1;
    }
    float o3[3] = { c->stand_in_cm, 0.5f * (c->stand_in_cm + c->stand_reach_cm), c->stand_reach_cm };
    if (!stand_floor(s, c, in, ray, ctx, o, o3, 3, c->stand_down_cm, c->stand_up_cm, &fy, &at)) return 0; /* no top within his rim */
    sv_vec3 w = { in->stick_w.x, 0, in->stick_w.z };
    float into = w.x * tw.x + w.z * tw.z;
    if (into < 0) w = add3(w, mul3(tw, -into)); /* never away from the top */
    float d = (float)(dt > 0 && dt < 0.1 ? dt : 0.0);
    sv_vec3 m = somvr_climb_clamp(mul3(w, c->stand_walk_cms * d), c->step_cm);
    if (!(len3(m) > 0.01f)) return 0;
    m.y += edge_step(s, c, in, o, c->step_cm); /* the capsule's foot against the edge: up by the blocked part */
    if (!s->stand_walking) s->stand_walking = 1, s->stand_walks++, o->events2 |= SOMVR_CLIMB_E_STAND_WALK;
    s->stand_walk_cm += len3(m), o->stand_walk = len3(m);
    request(s, o, m);
    return 1;
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
    s->rl_on = c->round_lip;
    double dt = s->last_t > 0 && in->t > s->last_t ? in->t - s->last_t : 0;
    s->last_t = in->t;
    if (in->live && in->frozen) { /* a menu or the pause: hold everything, the clocks stop */
        s->t0 += dt, s->phase_t += dt, s->lift_rise_t += dt, s->fl_t0 += dt, s->fl_glide_t += dt, s->am_t0 += dt;
        for (int h = 0; h < 2; h++) {
            s->h[h].press_t += dt;
            if (s->h[h].lost_t >= 0) s->h[h].lost_t += dt;
            s->h[h].have_last = 0; /* the hands may move while the menu is up: no pull from that */
        }
        s->drop_down = in->drop; /* the menu's A is the menu's: no drop from a press held into the world */
        if (s->dm == 1) dm_cancel(s, SOMVR_CLIMB_DMC_GAME, o); /* drop mark: a menu cancels the choice */
        s->dm = 0;
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
    int mark = c->descent && c->drop_mark; /* build 17: A held = the landing shown, let go = go (dm_step) */
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
    if (s->active && s->phase == SOMVR_CLIMB_PH_MANTLE) { /* A mantle: the glide onto the top (A, the grips: nothing) */
        if (!in->allow || !in->live) { /* the game's stand down: the end; the input lost: the hang where he is */
            if (s->dn) { /* down: the same */
                s->dn = 0, s->dn_end = 2, s->dn_hangs++;
                o->events2 |= SOMVR_CLIMB_E_DN_END;
            } else {
                s->am_end = in->allow ? SOMVR_CLIMB_AME_HANG : SOMVR_CLIMB_AME_GAME, s->am_blocks++;
                o->events2 |= SOMVR_CLIMB_E_AM_END;
            }
            if (in->allow) hang(s, in, 1, o);
            else end_climb(s, SOMVR_CLIMB_END_GAME, 1, o);
        } else if (s->dn) { /* down: the glide over the edge (the grips are read above: that one open = the cancel) */
            dn_step(s, c, in, ray, ctx, o, dt);
        } else {
            am_step(s, c, in, ray, ctx, o, dt);
        }
        o->active = s->active, o->driver = -1;
        return;
    }
    /* A mantle (build 20): A's press at a top = onto it (no way: nothing but the why, no drop there); elsewhere as ever */
    if (drop && c->mantle && s->active && in->live && in->allow &&
        (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG || s->phase == SOMVR_CLIMB_PH_FLING)) {
        int a = am_press(s, c, in, ray, ctx, o);
        if (a > 0) return;
        if (a < 0 && c->unstuck && mark) s->am_no_marks++; /* unstuck (3): the why, and A held = the way down */
        else if (a < 0) drop = 0;
    }
    int hangs = c->letgo == SOMVR_CLIMB_LETGO_HANG &&
                (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG ||
                 s->phase == SOMVR_CLIMB_PH_FLING); /* not lift's or cmd's tops */
    /* stand down (letgo = hang: the input lost hangs, no timeout) */
    if (s->active && !in->live && hangs) hang(s, in, 1, o);
    else if (s->active && !in->live) end_climb(s, SOMVR_CLIMB_END_INPUT, 1, o);
    else if (s->active && !in->allow) end_climb(s, SOMVR_CLIMB_END_GAME, 1, o);
    else if (s->active && !hangs && in->t - s->t0 > c->max_s) end_climb(s, SOMVR_CLIMB_END_TIMEOUT, 1, o);
    else if (s->active && drop && !mark &&
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
    if (mark && dm_step(s, c, in, ray, ctx, o, drop, s->drop_down)) return; /* the go's blink: still until its teleport */
    if (o->ended) return;
    if (!s->active && drop && !mark && c->descent && in->allow && !in->nograb) { /* A on foot: the edge ahead, down */
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
        if (c->down_over && !s->active && !was_down[h] && in->live && dn_try(s, c, in, h, ray, ctx, o)) {
            o->active = 1, o->driver = -1, o->started = 1; /* down: the glide over the edge (gravity off from now) */
            return;
        }
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
            /* a ledge passed on the way up: the next top is this hold's wall's (build 17: also with stand / topout -
             * the session's cliff kept the narrow ledge's top 9.7 m under the real one: no edge step, no over there) */
            if ((c->overhang > 0 || c->stand || c->topout) && s->active && s->topped && !s->descending &&
                k->hit.y > s->top_y + OH_RETOP_CM) {
                s->topped = 0, s->retops++;
                to_off(s, o, SOMVR_CLIMB_THR_END);
                if (c->stand || c->topout) /* the other hand's hold on that ledge tops him there no more */
                    for (int j = 0; j < 2; j++)
                        if (j != h && s->h[j].holding && s->h[j].edge && s->h[j].top_y < k->hit.y - OH_RETOP_CM)
                            s->h[j].edge = 0;
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
    if (s->po_hand >= 0 && (!s->h[s->po_hand].holding || s->h[s->po_hand].seq != s->po_aseq)) { /* pullover: let go */
        s->po_cancels++, s->po_hand = -1;
        o->events2 |= SOMVR_CLIMB_E_PO_CANCEL;
    }
    int any = s->h[0].holding || s->h[1].holding;
    if (!s->active && any) {
        s->active = 1, s->phase = SOMVR_CLIMB_PH_HOLD, s->t0 = in->t, s->carry = (sv_vec3){ 0, 0, 0 };
        s->driver = -1, s->oh_cm = 0, s->hug_max_d = 0;
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
            somvr_climb_hit g;
            if (s->dm != 1 && !s->dm_stick_wait && stand_walk(s, c, in, ray, ctx, o, dt)) {
                /* stand: the stick walks him onto the top (or he stands on it now) */
            } else if (c->hang_stick_cm > 0 && in->stick >= 0.5f && in->root_ok && s->dm != 1 && !s->dm_stick_wait &&
                cast(s, o, ray, ctx, 0, in->root, (sv_vec3){ 0, -1, 0 }, c->foot_cm + c->hang_stick_cm, &g) && g.hit &&
                !g.wet) { /* the stick in a low hang: he stands on what is under him and walks */
                s->stick_h = g.dist - c->foot_cm, s->stick_drops++;
                o->events2 |= SOMVR_CLIMB_E_STICK_DROP;
                end_climb(s, SOMVR_CLIMB_END_RELEASED, 0, o);
            }
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
        hug_step(s, c, in, k, o, &m); /* hug: never away from the wall past hug_cm */
        m.y += edge_step(s, c, in, o, c->step_cm); /* push: a forward push the edge blocks lifts him */
        lip_step(s, c, in, o, &m); /* descent: a push down while on the top goes out over the edge */
        if (c->overhang > 0 && c->oh_mantle && !s->descending && in->root_ok && s->have_root && in->head_ok &&
            mantle(s, c, in, k, ray, ctx, o, (float)dt)) { /* the game holds him under the top: the blink onto it */
            for (int h = 0; h < 2; h++) let_go(&s->h[h]), s->h[h].armed = 0;
            s->phase = SOMVR_CLIMB_PH_BLINK, s->phase_t = in->t, s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
            s->mantles++, s->blinks++;
            o->events2 |= SOMVR_CLIMB_E_MANTLE;
            o->blink = 1, o->blink_to = s->bl_to;
            o->active = 1, o->driver = -1;
            return;
        }
        if (s->dm != 1 && /* drop mark: A held: the drop's ring, the pullover waits */
            pullover(s, c, in, ray, ctx, o, (float)dt)) { /* pullover: the pull on a shown landing: the blink onto it */
            for (int h = 0; h < 2; h++) let_go(&s->h[h]), s->h[h].armed = 0;
            s->phase = SOMVR_CLIMB_PH_BLINK, s->phase_t = in->t, s->carry = (sv_vec3){ 0, 0, 0 }, s->driver = -1;
            s->blinks++;
            o->events2 |= SOMVR_CLIMB_E_PO_GO;
            o->blink = 1, o->blink_to = s->bl_to;
            o->active = 1, o->driver = -1, o->po_show = 0;
            return;
        }
        overhang_step(s, c, in, k, o, &m); /* overhang: a pull blocked under a lip goes out round its edge */
        topout_step(s, c, in, k, ray, ctx, o, &m, (float)dt); /* topout: a push held at a top goes through 1:1 */
        if (!s->to_on) through_step(s, c, in, k, ray, ctx, o, &m, (float)dt); /* through: a pull blocked under a lip */
        request(s, o, m);
        if (!o->move) o->through_up = 0, o->through_lx = o->through_lz = 0;
        for (int h = 0; h < 2; h++) /* every holding hand's last position, for its turn as the driver */
            if (s->h[h].holding && in->hand[h].valid) s->h[h].last_p = in->hand[h].p, s->h[h].have_last = 1;
        /* stand: the push brought his feet onto the top (the floor under his capsule within stand_up_cm of them) */
        if (c->stand && s->topped && !s->descending) {
            float o1[1] = { 0 }, fy;
            int at;
            /* the floor under his axis, the feet at most STAND_HOLD_UP_CM under it (the capsule not buried in it), at
             * most stand_up_cm over it */
            if (stand_floor(s, c, in, ray, ctx, o, o1, 1, c->stand_up_cm, STAND_HOLD_UP_CM, &fy, &at)) {
                stand_note(s, c, in, o, fy, at, 1);
                end_climb(s, SOMVR_CLIMB_END_OVER, 1, o);
                o->active = 0, o->driver = -1;
                return;
            }
        }
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
    if (c->mantle && s->active && (s->phase == SOMVR_CLIMB_PH_HOLD || s->phase == SOMVR_CLIMB_PH_HANG) && !s->descending)
        am_avail(s, c, in, ray, ctx, o); /* A mantle: the landing shown where A would take him */
    else
        s->am_ok = 0, s->am_test_t = 0;
    o->active = s->active, o->driver = s->driver;
}

/* ==== SOMVR_CLIMB_UNPIN (see the header) ==== */
void somvr_unpin_cfg_default(somvr_unpin_cfg *c) {
    c->hold_s = 0.15f, c->min_cm = 0.2f, c->frac = 0.2f, c->pen_cm = 1.0f, c->touch_cm = 2.0f, c->step_cm = 8.0f;
    c->radius_cm = 35.0f, c->drift_cms = 4.0f;
}
void somvr_unpin_init(somvr_unpin *s) {
    memset(s, 0, sizeof *s);
    s->t0 = -1;
}
const char *somvr_unpin_why_name(int why) {
    static const char *const n[] = { "free", "wait", "step", "not in a surface", "into a surface", "a wall ahead", "rays" };
    return why >= 0 && why <= SOMVR_UNPIN_RAYS ? n[why] : "?";
}
void somvr_unpin_step(somvr_unpin *s, const somvr_unpin_cfg *c, const somvr_unpin_in *in, somvr_unpin_out *o) {
    memset(o, 0, sizeof *o);
    float ax = in->asked.x, az = in->asked.z, la = sqrtf(ax * ax + az * az);
    float pa = sqrtf(in->applied.x * in->applied.x + in->applied.z * in->applied.z);
    o->deepest = 1e9f; /* the deepest wall or ceiling (a floor he sinks into a little is not a pin) */
    for (int i = 0; i < in->nc && i < SOMVR_UNPIN_MAXC; i++)
        if (in->c[i].n.y < 0.7f && in->c[i].d < o->deepest) o->deepest = in->c[i].d;
    if (!(la >= c->min_cm) || !(pa < c->frac * la) || !isfinite(la + pa)) { /* no move asked, or it went through */
        s->t0 = -1, s->pinned = 0;
        o->why = SOMVR_UNPIN_FREE;
        return;
    }
    o->dir = (sv_vec3){ ax / la, 0, az / la };
    if (in->clear_cm < 0) s->blocked_ticks++;
    if (s->t0 < 0 || in->t < s->t0) s->t0 = in->t;
    if (in->t - s->t0 < c->hold_s) {
        o->why = SOMVR_UNPIN_WAIT;
        return;
    }
    if (!(o->deepest <= -c->pen_cm)) { /* blocked, but not inside anything: a wall, the game's edge (its own rule) */
        if (in->clear_cm < 0) s->notin++;
        o->why = SOMVR_UNPIN_NOTIN;
        return;
    }
    /* never deeper into what he touches: the part of the move into each touching wall taken out (he slides along it,
     * as a wall lets him); nothing left (under 0.3 of it) = into it */
    float mx = ax, mz = az;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < in->nc && i < SOMVR_UNPIN_MAXC; i++) {
            const somvr_unpin_contact *k = &in->c[i];
            float nl = sqrtf(k->n.x * k->n.x + k->n.z * k->n.z);
            if (!(k->d < c->touch_cm) || nl < 0.3f) continue; /* far, or a floor / a ceiling */
            float nx = k->n.x / nl, nz = k->n.z / nl, dt = mx * nx + mz * nz;
            if (dt < 0) mx -= dt * nx, mz -= dt * nz;
        }
    float lm = sqrtf(mx * mx + mz * mz);
    if (!(lm >= 0.3f * la)) {
        if (in->clear_cm < 0) s->into++;
        o->why = SOMVR_UNPIN_INTO;
        return;
    }
    /* ... plus a drift out of the rock he is in (the deepest wall's normal; at most drift_cms, only with a move) */
    float best = 1e9f;
    sv_vec3 nd = { 0, 0, 0 };
    for (int i = 0; i < in->nc && i < SOMVR_UNPIN_MAXC; i++) {
        const somvr_unpin_contact *k = &in->c[i];
        float nl = sqrtf(k->n.x * k->n.x + k->n.z * k->n.z);
        if (k->n.y < 0.7f && nl >= 0.3f && k->d <= -c->pen_cm && k->d < best)
            best = k->d, nd = (sv_vec3){ k->n.x / nl, 0, k->n.z / nl };
    }
    o->dir = (sv_vec3){ mx / lm, 0, mz / lm };
    float st = fminf(lm, c->step_cm);
    float dr = best < 0 ? fminf(c->drift_cms * (in->dt > 0 && in->dt < 0.2f ? in->dt : 0.0f), -best) : 0.0f;
    if (in->clear_cm < 0) {
        o->why = SOMVR_UNPIN_RAYS;
        return;
    }
    if (in->clear_cm < c->radius_cm + st) {
        s->walls++;
        o->why = SOMVR_UNPIN_WALL;
        return;
    }
    o->why = SOMVR_UNPIN_STEP, o->d = (sv_vec3){ o->dir.x * st + nd.x * dr, 0, o->dir.z * st + nd.z * dr };
    o->drift = dr;
    o->first = s->pinned == 0;
    if (o->first) s->episodes++;
    s->pinned++, s->steps++, s->step_cm += st, s->drift_cm += dr;
}

/* ==== SOMVR_HEAD_ROCK (see the header) ==== */
void somvr_headrock_cfg_default(somvr_headrock_cfg *c) {
    c->near_cm = 8, c->full_cm = -2, c->out_s = 0.12f, c->in_s = 0.25f, c->back_s = 0.5f;
}
void somvr_headrock_init(somvr_headrock *s) {
    memset(s, 0, sizeof *s);
    s->black_t = -1, s->min_gap = 1e9f;
}
void somvr_headrock_step(somvr_headrock *s, const somvr_headrock_cfg *c, double t, float dt, int ok, float gap,
                         somvr_headrock_out *o) {
    memset(o, 0, sizeof *o);
    s->frames++;
    float span = c->near_cm - c->full_cm, want = 0;
    if (ok && isfinite(gap) && span > 0.1f) want = fminf(fmaxf((c->near_cm - gap) / span, 0.0f), 1.0f);
    int in = ok && isfinite(gap) && gap < c->near_cm;
    if (in && !s->in) s->in = 1, s->episodes++, s->min_gap = gap, o->first = 1;
    if (in && gap < s->min_gap) s->min_gap = gap;
    float d = dt > 0 && dt < 0.2f ? dt : 0.0f;
    if (want > s->level) s->level = fminf(want, s->level + (c->out_s > 0 ? d / c->out_s : 1.0f));
    else s->level = fmaxf(want, s->level - (c->in_s > 0 ? d / c->in_s : 1.0f));
    if (s->level >= 0.999f) {
        s->level = 1.0f;
        if (s->black_t < 0) s->black_t = t;
        s->black++;
        if (in && !s->backed && c->back_s > 0 && t - s->black_t >= c->back_s) s->backed = 1, s->backs++, o->back = 1;
    } else {
        s->black_t = -1;
        if (s->level < 0.5f) s->backed = 0;
    }
    s->faded += s->level > 0;
    if (!in && s->in && s->level <= 0) s->in = 0, o->ended = 1;
    o->level = s->level;
}

/* ==== SOMVR_HEAD_TETHER (see the header) ==== */
void somvr_tether_cfg_default(somvr_tether_cfg *c) {
    c->start_cm = 50, c->black_cm = 90, c->hold_s = 1.0f, c->out_s = 0.15f, c->in_s = 0.3f, c->back_s = 0.3f;
}
void somvr_tether_init(somvr_tether *s) {
    memset(s, 0, sizeof *s);
    s->over_t = s->black_t = -1;
}
void somvr_tether_step(somvr_tether *s, const somvr_tether_cfg *c, double t, float dt, int ok, float off_cm,
                       somvr_tether_out *o) {
    memset(o, 0, sizeof *o);
    s->frames++;
    int over = ok && isfinite(off_cm) && off_cm > c->start_cm;
    float want = 0, span = c->black_cm - c->start_cm;
    if (over) {
        if (!s->in) s->in = 1, s->held = 0, s->episodes++, s->max_cm = off_cm, o->first = 1;
        if (off_cm > s->max_cm) s->max_cm = off_cm;
        if (s->over_t < 0) s->over_t = t;
        want = span > 0.1f ? fminf((off_cm - c->start_cm) / span, 1.0f) : 1.0f;
        if (c->hold_s > 0 && t - s->over_t >= c->hold_s) {
            want = 1;
            if (!s->held) s->held = 1, s->holds++, o->held = 1;
        }
    } else {
        s->over_t = -1;
    }
    float d = dt > 0 && dt < 0.2f ? dt : 0.0f;
    if (want > s->level) s->level = fminf(want, s->level + (c->out_s > 0 ? d / c->out_s : 1.0f));
    else s->level = fmaxf(want, s->level - (c->in_s > 0 ? d / c->in_s : 1.0f));
    if (s->level >= 0.999f) {
        s->level = 1.0f;
        if (s->black_t < 0) s->black_t = t;
        s->black++;
        if (over && !s->backed && c->back_s > 0 && t - s->black_t >= c->back_s) s->backed = 1, s->backs++, o->back = 1;
    } else {
        s->black_t = -1;
        if (s->level < 0.5f) s->backed = 0;
    }
    s->faded += s->level > 0;
    if (!over && s->in && s->level <= 0) s->in = 0, o->ended = 1;
    o->level = s->level;
}
