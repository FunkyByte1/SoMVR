/* somvr_above.c - build 25 (som-b25-above; docs/re-xr-pipeline.md 102): the pure parts of SOMVR_ABOVE (somvr_above.h) */
#include "somvr_above.h"

#include <math.h>
#include <string.h>

const char *const somvr_fa_blink_names[SOMVR_FAB_N] = { "idle", "wait", "out", "black", "in", "held" };

void somvr_fa_cfg_default(somvr_fa_cfg *c) {
    c->plunge_cms = 200, c->down_cms = 140, c->point_deg = 40, c->aim_deg = 40;
    c->brand_deg = 20, c->brand_out_cm = 30;
    c->out_s = 0.12f, c->in_s = 0.20f, c->start_s = 0.6f, c->black_max_s = 2.5f;
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

int somvr_fa_plunge_judge(const somvr_fa_cfg *c, const float v[3], const float axis[3], const float to[3], somvr_fa_plunge *o) {
    somvr_fa_plunge r;
    r.speed = len3(v), r.down = -v[1];
    r.point_deg = ang3(v, axis), r.aim_deg = ang3(axis, to);
    r.why = "";
    if (!(r.speed >= c->plunge_cms)) r.why = "too slow";
    else if (!(r.down >= c->down_cms)) r.why = "not down";
    else if (!(r.point_deg <= c->point_deg)) r.why = "not point first";
    else if (!(r.aim_deg <= c->aim_deg)) r.why = "not aimed at him";
    if (o) *o = r;
    return !r.why[0];
}

int somvr_fa_mean_vel(const double *t, const float (*p)[3], int n, double min_s, double max_s, float v[3]) {
    if (n < 2) return 0;
    int old = -1;
    for (int i = n - 2; i >= 0; i--) {
        double dt = t[n - 1] - t[i];
        if (dt > max_s) break;
        if (dt >= min_s) old = i;
    }
    if (old < 0) return 0;
    float dt = (float)(t[n - 1] - t[old]);
    for (int k = 0; k < 3; k++) v[k] = (p[n - 1][k] - p[old][k]) / dt;
    return 1;
}

int somvr_fa_aimed(const somvr_fa_cfg *c, const float eye[3], const float palm[3], const float head[3], float *deg) {
    float p[3] = { palm[0] - eye[0], palm[1] - eye[1], palm[2] - eye[2] };
    float h[3] = { head[0] - eye[0], head[1] - eye[1], head[2] - eye[2] };
    float d = ang3(p, h);
    if (deg) *deg = d;
    return len3(p) >= c->brand_out_cm && d <= c->brand_deg;
}

void somvr_fa_blink_press(somvr_fa_blink *b, double t, int black) {
    memset(b, 0, sizeof *b);
    b->black = black != 0, b->t_press = b->t0 = t;
    b->phase = b->black ? SOMVR_FAB_WAIT : SOMVR_FAB_HELD; /* the view held; the black only once the game's kill starts */
}

void somvr_fa_blink_step(somvr_fa_blink *b, const somvr_fa_cfg *c, double t, int started, int landed, int over,
                         somvr_fa_blink_out *o) {
    memset(o, 0, sizeof *o);
    int ph0 = b->phase;
    if (b->phase == SOMVR_FAB_IDLE) return;
    if (started) b->started = 1;
    if (b->started && over) b->over = 1;
    int late = !b->started && t - b->t_press > c->start_s; /* the game took nothing: back in where he stands */
    switch (b->phase) {
    case SOMVR_FAB_WAIT:
        if (late) b->refused = 1, b->phase = SOMVR_FAB_IDLE;
        else if (b->started) b->phase = SOMVR_FAB_OUT, b->t0 = t;
        break;
    case SOMVR_FAB_OUT:
        if (late) {
            b->refused = 1, b->phase = SOMVR_FAB_IN;
            b->t0 = c->in_s > 0 ? t - (1.0 - b->alpha) * c->in_s : t; /* back in from this level */
            break;
        }
        b->alpha = c->out_s > 0 ? (float)((t - b->t0) / c->out_s) : 1.0f;
        if (b->alpha >= 1) b->alpha = 1, b->phase = SOMVR_FAB_BLACK, b->t0 = t;
        break;
    case SOMVR_FAB_BLACK:
        b->alpha = 1;
        if (late) {
            b->refused = 1, b->phase = SOMVR_FAB_IN, b->t0 = t;
        } else if (b->started && (landed || b->over || t - b->t0 >= c->black_max_s)) {
            o->seat = 1, b->seated = 1, b->phase = SOMVR_FAB_IN, b->t0 = t; /* this frame still black */
        }
        break;
    case SOMVR_FAB_IN:
        b->alpha = c->in_s > 0 ? 1.0f - (float)((t - b->t0) / c->in_s) : 0.0f;
        if (b->alpha > 1) b->alpha = 1;
        if (b->alpha <= 0) b->alpha = 0, b->phase = b->started && !b->over && !b->refused ? SOMVR_FAB_HELD : SOMVR_FAB_IDLE;
        break;
    case SOMVR_FAB_HELD:
        b->alpha = 0;
        if (late) b->refused = 1, b->phase = SOMVR_FAB_IDLE;
        else if (b->over) b->phase = SOMVR_FAB_IDLE;
        break;
    default:
        b->phase = SOMVR_FAB_IDLE;
    }
    o->fade = b->alpha;
    o->hold = b->phase != SOMVR_FAB_IDLE && !b->refused;
    o->changed = b->phase != ph0;
    o->done = b->phase == SOMVR_FAB_IDLE;
}
