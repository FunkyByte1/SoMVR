/* somvr_abil.c - build 27 (som-b27-abilities; docs/re-xr-pipeline.md 114): the pure parts of SOMVR_ABIL (somvr_abil.h) */
#include "somvr_abil.h"
#include <math.h>
#include <string.h>

void somvr_ab_cfg_default(somvr_ab_cfg *c) {
    memset(c, 0, sizeof *c);
    c->push_cms = 220, c->push_out_deg = 50, c->push_aim_deg = 60, c->push_out_cm = 25, c->open_max = 0.5f;
    c->trig_on = 0.6f, c->trig_off = 0.4f;
    c->stab_cms = 180, c->stab_down_cms = 120, c->stab_point_deg = 45, c->stab_vert_deg = 55, c->stab_reach_cm = 70;
}

static float len3(const float *a) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
/* the angle between a and b (deg); 180 when either is zero */
static float ang3(const float *a, const float *b) {
    float la = len3(a), lb = len3(b);
    if (!(la > 1e-6f) || !(lb > 1e-6f)) return 180.0f;
    float c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
    c = c > 1 ? 1 : c < -1 ? -1 : c;
    return acosf(c) * 57.29578f;
}

int somvr_ab_push_judge(const somvr_ab_cfg *c, const float v[3], const float eye[3], const float palm[3],
                        const float orc[3], float squeeze, int charge, somvr_ab_push *o) {
    somvr_ab_push r;
    float out[3] = { palm[0] - eye[0], palm[1] - eye[1], palm[2] - eye[2] };
    float vl[3] = { v[0], 0, v[2] }, to[3] = { orc[0] - palm[0], 0, orc[2] - palm[2] };
    r.speed = len3(v), r.out_deg = ang3(v, out), r.aim_deg = ang3(vl, to), r.out_cm = len3(out);
    r.why = "";
    if (!charge) r.why = "no wraith charge (the right trigger)";
    else if (squeeze >= c->open_max) r.why = "the hand closed";
    else if (!(r.speed >= c->push_cms)) r.why = "too slow";
    else if (!(r.out_deg <= c->push_out_deg)) r.why = "not out from the body";
    else if (!(r.out_cm >= c->push_out_cm)) r.why = "the hand at the body";
    else if (!(r.aim_deg <= c->push_aim_deg)) r.why = "not at the orc";
    if (o) *o = r;
    return !r.why[0];
}

int somvr_ab_stab_judge(const somvr_ab_cfg *c, const float v[3], const float axis[3], const float tip[3],
                        const float body[3], somvr_ab_stab *o) {
    somvr_ab_stab r;
    static const float down[3] = { 0, -1, 0 };
    float d[3] = { body[0] - tip[0], body[1] - tip[1], body[2] - tip[2] };
    r.speed = len3(v), r.down = -v[1], r.point_deg = ang3(v, axis), r.vert_deg = ang3(axis, down), r.reach_cm = len3(d);
    r.why = "";
    if (!(r.speed >= c->stab_cms)) r.why = "too slow";
    else if (!(r.down >= c->stab_down_cms)) r.why = "not down";
    else if (!(r.vert_deg <= c->stab_vert_deg)) r.why = "the blade not pointing down";
    else if (!(r.point_deg <= c->stab_point_deg)) r.why = "not point first";
    else if (!(r.reach_cm <= c->stab_reach_cm)) r.why = "the tip not at him";
    if (o) *o = r;
    return !r.why[0];
}

int somvr_ab_trigger(const somvr_ab_cfg *c, int was, float v) {
    if (!isfinite(v)) return 0;
    return was ? v > c->trig_off : v >= c->trig_on;
}

/* ---- build 27c (som-b27-abilities2; docs/re-xr-pipeline.md 118) ---- */
void somvr_ab2_cfg_default(somvr_ab2_cfg *c) {
    memset(c, 0, sizeof *c);
    c->throw_cms = 300, c->throw_fwd_deg = 50, c->throw_up_deg = 30, c->throw_down_deg = 35, c->throw_low_cm = 25;
    c->throw_win_s = 0.25f;
    c->aim_level_deg = 15, c->aim_pitch_deg = 30, c->aim_min_cm = 100, c->aim_max_cm = 2500;
    c->mouth_cm = 16, c->mouth_s = 0.3f, c->mouth_out_cm = 10;
}

/* the level angle from a to b (deg, + = b right of a: +x right at +z ahead); 180 when either is level-zero */
static float level_deg(const float *a, const float *b) {
    float la = hypotf(a[0], a[2]), lb = hypotf(b[0], b[2]);
    if (!(la > 1e-6f) || !(lb > 1e-6f)) return 180.0f;
    float d = atan2f(b[0], b[2]) - atan2f(a[0], a[2]);
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    return d * 57.29578f;
}
static float pitch_deg(const float *a) { return atan2f(a[1], hypotf(a[0], a[2])) * 57.29578f; }

int somvr_ab_throw_judge(const somvr_ab2_cfg *c, const float v[3], const float eye[3], const float fwd[3],
                         const float hand[3], somvr_ab_throw *o) {
    somvr_ab_throw r;
    r.speed = len3(v), r.fwd_deg = fabsf(level_deg(fwd, v)), r.pitch_deg = pitch_deg(v), r.low_cm = eye[1] - hand[1];
    r.why = "";
    if (!(r.speed >= c->throw_cms)) r.why = "too slow";
    else if (!(r.fwd_deg <= c->throw_fwd_deg)) r.why = "not thrown ahead";
    else if (!(r.pitch_deg <= c->throw_up_deg)) r.why = "thrown up";
    else if (!(r.pitch_deg >= -c->throw_down_deg)) r.why = "thrown down";
    else if (!(r.low_cm <= c->throw_low_cm)) r.why = "not overhand (the hand low)";
    if (o) *o = r;
    return !r.why[0];
}

int somvr_ab_throw_aim(const somvr_ab2_cfg *c, const float hand[3], const float v[3], const float orc[3], float *lv,
                       float *pv, float *dv) {
    float to[3] = { orc[0] - hand[0], orc[1] - hand[1], orc[2] - hand[2] };
    float l = level_deg(v, to), p = pitch_deg(v) - pitch_deg(to), d = len3(to);
    if (lv) *lv = l;
    if (pv) *pv = p;
    if (dv) *dv = d;
    return fabsf(l) <= c->aim_level_deg && fabsf(p) <= c->aim_pitch_deg && d >= c->aim_min_cm && d <= c->aim_max_cm;
}

float somvr_ab_mouth_dist(const float eye[3], const float fwd[3], const float hand[3]) {
    float lf = hypotf(fwd[0], fwd[2]), fx = lf > 1e-6f ? fwd[0] / lf : 0, fz = lf > 1e-6f ? fwd[2] / lf : 1;
    float m[3] = { eye[0] + 7 * fx, eye[1] - 9, eye[2] + 7 * fz };
    float d[3] = { hand[0] - m[0], hand[1] - m[1], hand[2] - m[2] };
    return len3(d);
}
