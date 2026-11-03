/* somvr_smooth.c - see somvr_smooth.h */
#include "somvr_smooth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318531f

/* ---- quaternions as float[4] (x, y, z, w) ---------------------------------------------------------------------- */
static void qmul(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                  a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                  a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
static void qconj(const float a[4], float o[4]) { o[0] = -a[0], o[1] = -a[1], o[2] = -a[2], o[3] = a[3]; }
static void qnorm(float q[4]) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(n > 1e-12f)) {
        q[0] = q[1] = q[2] = 0, q[3] = 1;
        return;
    }
    for (int i = 0; i < 4; i++) q[i] /= n;
}
/* a unit quaternion -> its rotation vector (the short way) */
static void qlog(const float q[4], float r[3]) {
    float s = q[3] < 0 ? -1.0f : 1.0f, x = s * q[0], y = s * q[1], z = s * q[2], w = s * q[3];
    float n = sqrtf(x * x + y * y + z * z);
    if (n < 1e-9f) {
        r[0] = 2 * x, r[1] = 2 * y, r[2] = 2 * z;
        return;
    }
    float a = 2.0f * atan2f(n, w) / n;
    r[0] = a * x, r[1] = a * y, r[2] = a * z;
}
void swg_qexp(const float r[3], float q[4]) {
    float a = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    float k = a < 1e-9f ? 0.5f : sinf(0.5f * a) / a;
    q[0] = k * r[0], q[1] = k * r[1], q[2] = k * r[2], q[3] = cosf(0.5f * a);
}
static void qrot(const float q[4], const float v[3], float o[3]) {
    /* v + 2 w (u x v) + 2 u x (u x v) */
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float c1[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    float c2[3] = {u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0]};
    for (int i = 0; i < 3; i++) o[i] = v[i] + 2 * w * c1[i] + 2 * c2[i];
}
float hsm_qangle(const float a[4], const float b[4]) {
    float d = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2.0f * acosf(d > 1 ? 1 : d);
}
static float vlen3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static float vdist3(const float a[3], const float b[3]) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return vlen3(d);
}
static int finite4(const float q[4]) { return isfinite(q[0]) && isfinite(q[1]) && isfinite(q[2]) && isfinite(q[3]); }
static int finite3(const float p[3]) { return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]); }

/* ---- the smoothing ------------------------------------------------------------------------------------------------- */
void hsm_defaults(hsm_cfg *c) {
    c->rot_min_hz = HSM_ROT_MIN_HZ, c->rot_beta = HSM_ROT_BETA;
    c->pos_min_hz = HSM_POS_MIN_HZ, c->pos_beta = HSM_POS_BETA;
    c->d_hz = 10.0f;
    c->reset_m = 0.15f, c->reset_rad = 0.45f; /* 15 cm, 26 deg off the filtered pose */
    c->max_gap_s = 0.1f;
}

int hsm_parse_pair(const char *s, float *min_hz, float *beta) {
    if (!s || !*s) return 0;
    char *e;
    float a = strtof(s, &e);
    if (e == s || *e != ',') return 0;
    const char *s2 = e + 1;
    float b = strtof(s2, &e);
    if (e == s2 || *e) return 0;
    if (!(a >= 0.05f && a <= 60.0f) || !(b >= 0 && b <= 1000.0f)) return 0;
    *min_hz = a, *beta = b;
    return 1;
}

void hsm_reset(hsm_state *s) { memset(s, 0, sizeof *s); }

float hsm_tau(float min_hz, float beta, float speed) { return 1.0f / (TWO_PI * (min_hz + beta * speed)); }

float hsm_alpha(float fc, float dt) {
    float x = TWO_PI * fc * dt;
    return x / (1.0f + x);
}

/* the residual of a constant-velocity step from the two poses before (k = this step / the one before) */
static void jit_resid(const float q[4], const float p[3], const float q0[4], const float p0[3], const float q1[4],
                      const float p1[3], float k, float tip_m, float *dpos, float *drot, float *dtip) {
    float c1[4], d[4], r[3], qp[4], pp[3], e[4];
    qconj(q1, c1);
    qmul(q0, c1, d); /* the last step (the space's frame) */
    qnorm(d);
    qlog(d, r);
    for (int i = 0; i < 3; i++) r[i] *= k, pp[i] = p0[i] + k * (p0[i] - p1[i]);
    swg_qexp(r, e);
    qmul(e, q0, qp);
    qnorm(qp);
    *dpos = vdist3(p, pp);
    *drot = hsm_qangle(q, qp);
    float t[3] = {0, 0, -tip_m}, ta[3], tb[3];
    qrot(q, t, ta);
    qrot(qp, t, tb);
    float a[3] = {p[0] + ta[0], p[1] + ta[1], p[2] + ta[2]}, b[3] = {pp[0] + tb[0], pp[1] + tb[1], pp[2] + tb[2]};
    *dtip = vdist3(a, b);
}

static void hsm_init(hsm_state *s, const float rq[4], const float rp[3], int64_t t) {
    memcpy(s->q, rq, 16), memcpy(s->p, rp, 12), memcpy(s->rq, rq, 16), memcpy(s->rp, rp, 12);
    s->t = t, s->have = 1, s->hist = 0, s->ws = s->vs = 0;
}

int hsm_step(hsm_state *s, const hsm_cfg *c, int on, float tip_m, somvr_hand *h) {
    if (!h || !h->valid || !finite4(h->pose.q) || !finite3(h->pose.p)) {
        s->have = 0, s->hist = 0;
        return 0;
    }
    float rq[4], rp[3];
    memcpy(rq, h->pose.q, 16), memcpy(rp, h->pose.p, 12);
    qnorm(rq);
    if (!s->have) {
        hsm_init(s, rq, rp, h->time);
        s->resets++;
        return 0;
    }
    double dt = (double)(h->time - s->t) * 1e-9;
    if (dt <= 0) { /* the same instant again (a repeated views time): the same pose */
        s->same_time++;
        if (on) memcpy(h->pose.q, s->q, 16), memcpy(h->pose.p, s->p, 12);
        return on;
    }
    /* a jump: farther off than the reset distance plus this step's own motion (a fast swing at a low frame rate moves
     * tens of degrees a frame) */
    float wmax = (h->vel_valid & 2) ? vlen3(h->ang) : 0, vmax = (h->vel_valid & 1) ? vlen3(h->lin) : 0;
    if (dt > c->max_gap_s || vdist3(rp, s->p) > c->reset_m + vmax * (float)dt ||
        hsm_qangle(rq, s->q) > c->reset_rad + wmax * (float)dt) {
        hsm_init(s, rq, rp, h->time);
        s->resets++;
        return 0;
    }
    float fdt = (float)dt;
    float ws, vs;
    if (h->vel_valid & 2)
        ws = vlen3(h->ang);
    else
        ws = s->ws + hsm_alpha(c->d_hz, fdt) * (hsm_qangle(rq, s->rq) / fdt - s->ws);
    if (h->vel_valid & 1)
        vs = vlen3(h->lin);
    else
        vs = s->vs + hsm_alpha(c->d_hz, fdt) * (vdist3(rp, s->rp) / fdt - s->vs);
    s->ws = ws, s->vs = vs;
    float ar = hsm_alpha(c->rot_min_hz + c->rot_beta * ws, fdt), ap = hsm_alpha(c->pos_min_hz + c->pos_beta * vs, fdt);
    float cq[4], d[4], r[3], e[4], nq[4];
    qconj(s->q, cq);
    qmul(rq, cq, d);
    qnorm(d);
    qlog(d, r);
    for (int i = 0; i < 3; i++) r[i] *= ar;
    swg_qexp(r, e);
    qmul(e, s->q, nq);
    qnorm(nq);
    memcpy(s->q, nq, 16);
    for (int i = 0; i < 3; i++) s->p[i] += ap * (rp[i] - s->p[i]);
    /* the measurement: regular steps (within 3/4..4/3 of the one before), still frames */
    int64_t dtn = h->time - s->t;
    if (s->hist >= 2 && s->dt1 > 0 && dtn * 4 >= s->dt1 * 3 && dtn * 3 <= s->dt1 * 4 && ws < 0.35f && vs < 0.05f) {
        float k = (float)dtn / (float)s->dt1, a, b, t;
        hsm_jit *j = &s->jit;
        jit_resid(rq, rp, s->hq[0], s->hp[0], s->hq[1], s->hp[1], k, tip_m, &a, &b, &t);
        j->raw_pos2 += (double)a * a, j->raw_rot2 += (double)b * b, j->raw_tip2 += (double)t * t;
        if (t > j->raw_tip_max) j->raw_tip_max = t;
        jit_resid(s->q, s->p, s->fq[0], s->fp[0], s->fq[1], s->fp[1], k, tip_m, &a, &b, &t);
        j->out_pos2 += (double)a * a, j->out_rot2 += (double)b * b, j->out_tip2 += (double)t * t;
        if (t > j->out_tip_max) j->out_tip_max = t;
        j->n++;
    }
    memcpy(s->hq[1], s->hq[0], 16), memcpy(s->hp[1], s->hp[0], 12), memcpy(s->fq[1], s->fq[0], 16);
    memcpy(s->fp[1], s->fp[0], 12);
    memcpy(s->hq[0], rq, 16), memcpy(s->hp[0], rp, 12), memcpy(s->fq[0], s->q, 16), memcpy(s->fp[0], s->p, 12);
    if (s->hist < 2) s->hist++;
    s->dt1 = dtn;
    memcpy(s->rq, rq, 16), memcpy(s->rp, rp, 12);
    s->t = h->time, s->steps++;
    if (on) memcpy(h->pose.q, s->q, 16), memcpy(h->pose.p, s->p, 12);
    return on;
}

/* ---- the weight (build 16) ------------------------------------------------------------------------------------- */
void swg_defaults(swg_cfg *c, int level) {
    const float deg = 0.01745329f;
    c->a_dn = 400.0f, c->zeta = 2.7f, c->w_max = 20.0f, c->two_k = 4.6f;
    c->rope_close = 1.5f;                     /* 86 deg/s */
    c->reset_rad = 0.6f, c->reset_rate = 40.0f; /* 34 deg in one step and faster than 40 rad/s: a jump */
    c->sub_s = 0.004f;
    if (level == SWG_HEAVY)
        c->a_up = 24.0f, c->omega = 25.0f, c->rope0 = 9.0f * deg, c->rope = 16.0f * deg;
    else
        c->a_up = 60.0f, c->omega = 30.0f, c->rope0 = 5.0f * deg, c->rope = 9.0f * deg;
}

void swg_reset(swg_state *s) { memset(s, 0, sizeof *s); }

static float vdot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void vcross3(const float a[3], const float b[3], float o[3]) {
    float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(o, r, sizeof r);
}
static void vunit3(float v[3]) {
    float n = vlen3(v);
    if (n > 1e-12f) v[0] /= n, v[1] /= n, v[2] /= n;
}
/* the shortest rotation taking the unit a onto the unit b, as a rotation vector (perpendicular to both) */
static void minrot(const float a[3], const float b[3], float r[3]) {
    float c[3];
    vcross3(a, b, c);
    float sn = vlen3(c), ang = atan2f(sn, vdot3(a, b));
    if (sn < 1e-9f) {
        r[0] = r[1] = r[2] = 0;
        return;
    }
    for (int i = 0; i < 3; i++) r[i] = c[i] / sn * ang;
}
/* v turned by the rotation vector r */
static void vturn(const float r[3], const float v[3], float o[3]) {
    float q[4];
    swg_qexp(r, q);
    qrot(q, v, o);
}

int swg_step(swg_state *s, const swg_cfg *c, const float q[4], const float axis[3], float dt, int two, float rot[3]) {
    memset(rot, 0, 12);
    float an[3] = {axis[0], axis[1], axis[2]};
    if (!finite4(q) || !finite3(an) || vlen3(an) < 1e-6f) {
        s->have = 0;
        return 0;
    }
    vunit3(an);
    float qn[4], ah[3];
    memcpy(qn, q, 16);
    qnorm(qn);
    if (s->have && dt <= 0) { /* the same instant: the same offset */
        qrot(qn, an, ah);
        minrot(ah, s->d, rot);
        return 1;
    }
    float cq[4], dq[4], r[3] = {0, 0, 0};
    if (s->have) {
        qconj(s->q, cq);
        qmul(qn, cq, dq);
        qnorm(dq);
        qlog(dq, r);
    }
    float k = two ? c->two_k : 1.0f, rope = c->rope / k;
    if (!s->have || !(dt > 0) || dt > 0.1f || (vlen3(r) > c->reset_rad && vlen3(r) > c->reset_rate * dt)) {
        float keep = s->peak_rad;
        uint64_t st = s->steps, rp = s->roped, rs = s->resets + (s->have ? 1 : 0);
        swg_reset(s);
        s->peak_rad = keep, s->steps = st, s->roped = rp, s->resets = rs;
        memcpy(s->q, qn, 16);
        qrot(qn, an, s->d);
        s->rope = rope, s->have = 1;
        return 0;
    }
    float a_up = c->a_up * k * k, a_dn = c->a_dn * k * k, om = c->omega * k, w_max = c->w_max * (two ? 1.5f : 1.0f);
    float rope0 = c->rope0 / k, kap = 2 * c->zeta * om, kp = om / (2 * c->zeta);
    /* one hand -> two: the rope closes at rope_close; two -> one: open at once */
    s->rope = s->rope - c->rope_close * dt > rope ? s->rope - c->rope_close * dt : rope;
    float wh[3] = {r[0] / dt, r[1] / dt, r[2] / dt};
    int n = (int)ceilf(dt / c->sub_s);
    n = n < 1 ? 1 : n > 64 ? 64 : n;
    float h = dt / (float)n, g = 1.0f - expf(-kap * h);
    for (int i = 1; i <= n; i++) {
        /* the hand at this sub-step (the frame's turn spread evenly) */
        float ri[3] = {r[0] * i / n, r[1] * i / n, r[2] * i / n}, ei[4], qi[4];
        swg_qexp(ri, ei);
        qmul(ei, s->q, qi);
        qrot(qi, an, ah);
        float e[3];
        minrot(s->d, ah, e);
        float ang = vlen3(e);
        /* the speed target: the hand's swing (not its twist about the blade) and the pull toward its direction */
        float wt = vdot3(wh, s->d), want[3], dv[3];
        for (int j = 0; j < 3; j++) want[j] = wh[j] - wt * s->d[j] + kp * e[j], dv[j] = (want[j] - s->wb[j]) * g;
        /* the limits: the part of the change that slows the blade at a_dn (down to a stop), the rest (speeding up, a
         * new direction, a reversal's far side) at a_up, harder from rope0 off the hand */
        float wn = vlen3(s->wb), u[3] = {0, 0, 0}, slow = 0;
        if (wn > 1e-6f) {
            for (int j = 0; j < 3; j++) u[j] = s->wb[j] / wn;
            float par = vdot3(dv, u);
            if (par < 0) {
                float to_stop = -par < wn ? -par : wn;
                slow = to_stop < a_dn * h ? to_stop : a_dn * h;
                /* left for a_up: the sideways part, and past a stop the reversal's far side */
                float far = slow == wn && -par > wn ? par + wn : 0;
                for (int j = 0; j < 3; j++) dv[j] += (far - par) * u[j];
            }
        }
        float lim = a_up;
        if (ang > rope0) {
            float f = (ang - rope0) / (rope > rope0 + 1e-6f ? rope - rope0 : 1e-6f);
            f = f > 1 ? 1 : f;
            lim = a_up + f * f * (4 * a_dn - a_up);
        }
        float dn = vlen3(dv);
        if (dn > lim * h)
            for (int j = 0; j < 3; j++) dv[j] *= lim * h / dn;
        for (int j = 0; j < 3; j++) s->wb[j] += dv[j] - slow * u[j];
        float wd = vdot3(s->wb, s->d);
        for (int j = 0; j < 3; j++) s->wb[j] -= wd * s->d[j];
        wn = vlen3(s->wb);
        if (wn > w_max)
            for (int j = 0; j < 3; j++) s->wb[j] *= w_max / wn;
        float step[3] = {s->wb[0] * h, s->wb[1] * h, s->wb[2] * h}, nd[3];
        vturn(step, s->d, nd);
        vunit3(nd);
        memcpy(s->d, nd, 12);
        /* the rope: never farther off the hand than it; no speed away from the hand there */
        minrot(s->d, ah, e);
        ang = vlen3(e);
        if (ang > s->rope) {
            float eu[3] = {e[0] / ang, e[1] / ang, e[2] / ang}, back[3];
            for (int j = 0; j < 3; j++) back[j] = eu[j] * (ang - s->rope);
            vturn(back, s->d, nd);
            vunit3(nd);
            memcpy(s->d, nd, 12);
            float grow = vdot3(wh, eu) - vdot3(s->wb, eu); /* the angle off grows at this rate */
            if (grow > 0)
                for (int j = 0; j < 3; j++) s->wb[j] += grow * eu[j];
            s->roped++;
        }
    }
    memcpy(s->q, qn, 16), memcpy(s->wh, wh, 12);
    qrot(qn, an, ah);
    minrot(ah, s->d, rot);
    s->off = vlen3(rot);
    if (s->off > s->peak_rad) s->peak_rad = s->off;
    s->steps++;
    return 1;
}

void swg_follow(const float rot[3], const float q[4], float out[4]) {
    float e[4], qn[4];
    memcpy(qn, q, 16);
    qnorm(qn);
    swg_qexp(rot, e);
    qmul(e, qn, out);
    qnorm(out);
}

/* ---- the bite ------------------------------------------------------------------------------------------------------- */
void swb_defaults(swb_cfg *c) {
    c->hold_s = 0.06f, c->ease_s = 0.1f, c->max_rad = 30.0f * 0.01745329f, c->drag = 0;
}

void swb_reset(swb_state *s) { memset(s, 0, sizeof *s); }

void swb_cancel(swb_state *s) {
    s->phase = SWB_IDLE;
    memset(s->b, 0, sizeof s->b);
}

int swb_start(swb_state *s, double t, const float p[3], const float q[4], const float P[3]) {
    if (s->phase != SWB_IDLE || !finite3(p) || !finite4(q) || !finite3(P) || !isfinite(t)) {
        s->refused++;
        return 0;
    }
    float qn[4], ci[4], d[3] = {P[0] - p[0], P[1] - p[1], P[2] - p[2]};
    memcpy(qn, q, 16);
    qnorm(qn);
    if (!(vlen3(d) > 1e-6f)) { /* the point at the grip: nothing to pivot on */
        s->refused++;
        return 0;
    }
    qconj(qn, ci);
    qrot(ci, d, s->l);
    s->phase = SWB_HOLD, s->t0 = s->last_t = t, s->t1 = 0;
    memset(s->b, 0, sizeof s->b), memset(s->b_rel, 0, sizeof s->b_rel);
    s->peak = 0, s->capped = 0, s->held_s = 0;
    s->bites++;
    return 1;
}

/* smoothstep 1 -> 0 over f in 0..1 */
static float swb_fade(float f) { return f <= 0 ? 1.0f : f >= 1 ? 0.0f : 1.0f - f * f * (3 - 2 * f); }

int swb_step(swb_state *s, const swb_cfg *c, double t, const float p[3], const float qw[4], const float P[3], float b[3]) {
    memset(b, 0, 12);
    if (s->phase == SWB_IDLE)
        return SWB_IDLE;
    if (!isfinite(t) || t - s->last_t > SWB_GAP_S || !finite3(p) || !finite4(qw) || !finite3(P)) {
        s->gaps++;
        swb_cancel(s);
        return SWB_IDLE;
    }
    if (t < s->last_t) /* (the same clock: never backwards; held as it was) */
        t = s->last_t;
    s->last_t = t, s->steps++;
    if (s->phase == SWB_HOLD) {
        float qn[4], v[3], d[3] = {P[0] - p[0], P[1] - p[1], P[2] - p[2]}, r[3];
        memcpy(qn, qw, 16);
        qnorm(qn);
        qrot(qn, s->l, v); /* where the held point would be drawn now */
        float lv = vlen3(v), ld = vlen3(d);
        if (lv > 1e-6f && ld > 1e-6f) {
            for (int i = 0; i < 3; i++) v[i] /= lv, d[i] /= ld;
            minrot(v, d, r);
        } else {
            memcpy(r, s->b, 12); /* the grip on the point: keep the last turn */
        }
        float a = vlen3(r);
        int over = a > c->max_rad, end = (over && !c->drag) || !(t - s->t0 < c->hold_s);
        if (over) { /* torn free (or, dragging, held there): the largest turn the hold allows */
            for (int i = 0; i < 3; i++) r[i] *= c->max_rad / a;
            a = c->max_rad, s->caps += !s->capped, s->capped = 1; /* (once a bite) */
        }
        memcpy(s->b, r, 12);
        if (a > s->peak) s->peak = a;
        if (!end) {
            memcpy(b, r, 12);
            return SWB_HOLD;
        }
        s->held_s = (float)(t - s->t0);
        memcpy(s->b_rel, r, 12);
        s->phase = SWB_EASE, s->t1 = t;
        if (!(c->ease_s > 0)) {
            swb_cancel(s);
            return SWB_IDLE;
        }
        memcpy(b, r, 12); /* the ease's first frame: where the hold left it */
        return SWB_EASE;
    }
    float f = (float)((t - s->t1) / c->ease_s), k = swb_fade(f);
    if (!(k > 0)) {
        swb_cancel(s);
        return SWB_IDLE;
    }
    for (int i = 0; i < 3; i++) s->b[i] = b[i] = s->b_rel[i] * k;
    return SWB_EASE;
}

/* ---- build 31: the sword as a weighted body on a joint to the hand (swp_*; docs/re-xr-pipeline.md 134) --------------- */
void swp_defaults(swp_cfg *c, int level) {
    const float deg = 0.01745329f;
    memset(c, 0, sizeof *c);
    /* Urfael as an arming sword: 1.3 kg, the balance a hand's width over the guard (15 cm from the fist's centre);
     * across the blade 0.055 kg m^2 about the balance (0.084 about the fist), about the blade 0.0012 */
    c->mass = 1.3f, c->com = 0.15f, c->i_perp = 0.055f, c->i_axis = 0.0012f;
    /* the hand: in place 8 Hz with the sword (3000 N/m, critically damped, 400 N at most); in turn 3.2 Hz about the
     * fist (34 N m/rad, damping 0.75: a little follow-through), 9 N m at most (107 rad/s^2 about the fist: a full swing
     * from rest reaches ~18 rad/s over 90 deg, a real arming sword's 10-15 rad/s in a committed cut; a wrist flick that
     * would need 400 rad/s^2 cannot whip the tip); the twist about the blade nearly 1:1 */
    c->k_lin = 8000.0f, c->zeta_lin = 1.0f, c->f_max = 600.0f;
    c->k_ang = 55.0f, c->zeta_ang = 1.0f, c->t_max = 36.0f, c->w_hill = 28.0f, c->hill_min = 0.08f, c->t_stop = 50.0f;
    c->t_twist = 4.0f;
    c->rope_rad = 55.0f * deg, c->rope_m = 0.10f, c->rope_close = 3.0f, c->rope_close_m = 0.8f, c->w_max = 30.0f;
    c->two_k = 2.5f, c->two_f = 2.8f, c->guard_k = 2.0f, c->guard_f = 2.2f;
    c->sub_s = 0.0005f;
    c->reset_m = 0.30f, c->reset_rate_m = 12.0f, c->reset_rad = 1.0f, c->reset_rate_rad = 45.0f;
    c->s_guard = 0.12f, c->s_tip = 1.0f;
    c->k_w = 20000.0f, c->zeta_w = 0.7f;
    c->y_cut = 130.0f, c->y_cut_s = 160.0f, c->v_slice = 0.8f, c->core_k = 2.2f, c->y_back = 25.0f, c->y_slide = 40.0f;
    c->y_stab_lat = 700.0f, c->y_pull = 400.0f, c->y_pull_kin = 200.0f, c->y_push = 70.0f, c->skin_m = 0.10f;
    c->stab_cut_m = 0.15f, c->stab_lever_rad = 35.0f * deg;
    c->core_frac = 0.45f;
    c->tear_m = 0.30f, c->tear_rad = 80.0f * deg;
    c->max_s = 10.0f, c->pend_s = 0.4f, c->enter_m = 0.01f, c->wound_k = 2.0f;
    if (level != SWG_HEAVY) /* a lighter blade's feel: the hand stronger against it */
        c->k_ang *= 1.8f, c->t_max *= 1.7f, c->rope_rad = 35.0f * deg;
}

void swp_reset(swp_state *s) { memset(s, 0, sizeof *s); }

const char *swp_end_name(int why) {
    static const char *const w[SWP_END_N] = {"-", "out (drawn out along the blade)", "through (out the far side)",
                                             "back (out the way it came)", "torn free", "left in too long",
                                             "cancelled", "the blade never reached the body"};
    return why >= 0 && why < SWP_END_N ? w[why] : "?";
}

float swp_seg_seg(const float a0[3], const float a1[3], const float b0[3], const float b1[3], float *ua, float *ub) {
    float d1[3], d2[3], r[3];
    for (int i = 0; i < 3; i++) d1[i] = a1[i] - a0[i], d2[i] = b1[i] - b0[i], r[i] = a0[i] - b0[i];
    float a = vdot3(d1, d1), e = vdot3(d2, d2), f = vdot3(d2, r), s = 0, t = 0;
    if (a <= 1e-12f && e <= 1e-12f) {
        s = t = 0;
    } else if (a <= 1e-12f) {
        t = f / e, t = t < 0 ? 0 : t > 1 ? 1 : t;
    } else {
        float cc = vdot3(d1, r);
        if (e <= 1e-12f) {
            s = -cc / a, s = s < 0 ? 0 : s > 1 ? 1 : s;
        } else {
            float b = vdot3(d1, d2), den = a * e - b * b;
            s = den > 1e-12f ? (b * f - cc * e) / den : 0;
            s = s < 0 ? 0 : s > 1 ? 1 : s;
            t = (b * s + f) / e;
            if (t < 0) t = 0, s = -cc / a, s = s < 0 ? 0 : s > 1 ? 1 : s;
            else if (t > 1) t = 1, s = (b - cc) / a, s = s < 0 ? 0 : s > 1 ? 1 : s;
        }
    }
    float x[3];
    for (int i = 0; i < 3; i++) x[i] = a0[i] + d1[i] * s - b0[i] - d2[i] * t;
    if (ua) *ua = s;
    if (ub) *ub = t;
    return vlen3(x);
}

/* x (the body's frame) against the body: the signed distance to its surface (< 0 inside), the link, its axis distance */
static float swp_body_sd(const swp_state *s, const float x[3], int *link, float *axd) {
    float best = 1e9f;
    for (int l = 0; l + 1 < s->body.n; l++) {
        float u, d = swp_seg_seg(x, x, s->nodel[l], s->nodel[l + 1], &u, NULL) - s->body.r[l];
        (void)u;
        if (d < best) {
            best = d;
            if (link) *link = l;
            if (axd) *axd = d + s->body.r[l];
        }
    }
    return best;
}

/* the body's frame at f (0 = the last frame's, 1 = this one's) */
static void swp_body_at(const swp_state *s, float f, float o[3], float q[4]) {
    const float *qa = s->body0.q, *qb = s->body.q;
    float sg = vdot3(qa, qb) + qa[3] * qb[3] < 0 ? -1.0f : 1.0f;
    for (int i = 0; i < 3; i++) o[i] = s->body0.o[i] + f * (s->body.o[i] - s->body0.o[i]);
    for (int i = 0; i < 4; i++) q[i] = qa[i] + f * (sg * qb[i] - qa[i]);
    qnorm(q);
}
static void swp_to_local(const float o[3], const float q[4], const float x[3], float out[3]) {
    float ci[4], d[3] = {x[0] - o[0], x[1] - o[1], x[2] - o[2]};
    qconj(q, ci);
    qrot(ci, d, out);
}
static void swp_to_world(const float o[3], const float q[4], const float x[3], float out[3]) {
    qrot(q, x, out);
    for (int i = 0; i < 3; i++) out[i] += o[i];
}

void swp_wound_end(swp_state *s, int why) {
    if (!s->wound && !s->pend) return;
    s->end = why, s->end_kind = s->wound ? s->wound : s->pend;
    if (s->wound) memcpy(s->end_P, s->Wt, sizeof s->end_P);
    /* the ropes open to where the sword is (they close again at rope_close: the sword swings back, never snaps) */
    s->rope_now = fmaxf(s->rope_now, s->off_rad + 0.05f), s->rope_m_now = fmaxf(s->rope_m_now, s->stretch + 0.02f);
    if (why >= 0 && why < SWP_END_N) s->ends[why]++;
    if (!s->wound) s->misses++;
    s->wound = s->pend = 0, s->sliding = 0, s->ev |= SWP_EV_END;
}

static void swp_set_body(swp_state *s, const swp_body *b) {
    int n = b->n < 2 ? 2 : b->n > SWP_NODES ? SWP_NODES : b->n;
    s->body0 = s->body_have ? s->body : *b;
    s->body = *b, s->body.n = n;
    qnorm(s->body.q);
    if (!s->body_have) s->body0 = s->body;
    for (int k = 0; k < n; k++) swp_to_local(s->body.o, s->body.q, s->body.node[k], s->nodel[k]);
    s->body_have = 1;
}

int swp_wound_hit(swp_state *s, const swp_cfg *c, int kind, const swp_body *body) {
    (void)c;
    if (s->wound || s->pend || !body || body->n < 2 || (kind != SWP_W_CUT && kind != SWP_W_STAB) || !s->have) return 0;
    for (int k = 0; k < body->n && k < SWP_NODES; k++)
        if (!finite3(body->node[k])) return 0;
    if (!finite3(body->o) || !finite4(body->q)) return 0;
    s->body_have = 0;
    swp_set_body(s, body);
    s->pend = kind, s->pend_t = s->t, s->pend_pen = -1.0f;
    return 1;
}

/* a stab's blade turned in the body since it went in (rad): levering it */
static float swp_lever(const swp_state *s, const float a[3], const float o[3], const float qb[4]) {
    float ci[4], al[3];
    (void)o;
    qconj(qb, ci), qrot(ci, a, al);
    float d = vdot3(al, s->a0l);
    return acosf(d > 1 ? 1 : d < -1 ? -1 : d);
}

/* the blade (the sword at p, a) against the body now (o, qb): the deepest link it is in; 1 = in by enter_m */
static int swp_blade_in(const swp_state *s, const swp_cfg *c, const float p[3], const float a[3], const float o[3],
                        const float qb[4], int *link, float *s_c, float *dist, float *pen_out) {
    float b0[3], b1[3], l0[3], l1[3], best = -1;
    for (int i = 0; i < 3; i++) b0[i] = p[i] + a[i] * c->s_guard, b1[i] = p[i] + a[i] * c->s_tip;
    swp_to_local(o, qb, b0, l0), swp_to_local(o, qb, b1, l1);
    for (int l = 0; l + 1 < s->body.n; l++) {
        float u, d = swp_seg_seg(l0, l1, s->nodel[l], s->nodel[l + 1], &u, NULL), pen = s->body.r[l] - d;
        if (l == 0 || pen > best) best = pen, *link = l, *s_c = c->s_guard + u * (c->s_tip - c->s_guard), *dist = d;
    }
    *pen_out = best;
    return best > c->enter_m;
}

int swp_step(swp_state *s, const swp_cfg *c, double t, const float p[3], const float q[4], const float axis[3], float dt,
             int two, int guard, const swp_body *body, float op[3], float oq[4]) {
    s->ev = 0;
    memcpy(op, p, 12), memcpy(oq, q, 16);
    float an[3] = {axis[0], axis[1], axis[2]}, qn[4];
    memcpy(qn, q, 16);
    if (!finite3(p) || !finite4(q) || !finite3(an) || vlen3(an) < 1e-6f || !isfinite(t)) {
        swp_wound_end(s, SWP_END_CANCEL);
        s->have = 0;
        return 0;
    }
    vunit3(an), qnorm(qn);
    memcpy(oq, qn, 16);
    if (body) swp_set_body(s, body);
    else if ((s->wound || s->pend) && s->body_have) s->body0 = s->body; /* (no motion this frame) */
    if ((s->wound || s->pend) && !body && t - s->t > 0.15) swp_wound_end(s, SWP_END_CANCEL);
    if (s->have && dt <= 0) { /* the same instant: as it was */
        memcpy(op, s->p, 12), memcpy(oq, s->q, 16);
        return 1;
    }
    float r[3] = {0, 0, 0}, dp[3] = {0, 0, 0}, cq[4], dq[4];
    if (s->have) {
        qconj(s->tq, cq);
        qmul(qn, cq, dq);
        qnorm(dq);
        qlog(dq, r);
        for (int i = 0; i < 3; i++) dp[i] = p[i] - s->tp[i];
    }
    int jump = s->have && ((vlen3(dp) > c->reset_m && vlen3(dp) > c->reset_rate_m * dt) ||
                           (vlen3(r) > c->reset_rad && vlen3(r) > c->reset_rate_rad * dt));
    if (!s->have || !(dt > 0) || dt > 0.1f || jump) {
        swp_wound_end(s, SWP_END_CANCEL);
        int had = s->have;
        memcpy(s->p, p, 12), memcpy(s->q, qn, 16), memcpy(s->tp, p, 12), memcpy(s->tq, qn, 16);
        memset(s->vc, 0, 12), memset(s->w, 0, 12), memset(s->tw_last, 0, 12);
        s->have = 1, s->t = t, s->resets += had, s->ev |= had ? SWP_EV_RESET : 0, s->rope_now = 0, s->rope_m_now = 0;
        s->stretch = s->off_rad = 0;
        return 0;
    }
    /* the frame's gains: one hand, both, a blow coming */
    float kf = two ? c->two_k : 1.0f, ff = two ? c->two_f : 1.0f;
    if (guard) kf = fmaxf(kf, c->guard_k), ff = fmaxf(ff, c->guard_f);
    if (s->wound) kf *= c->wound_k, ff *= c->wound_k; /* the blade in flesh: the wrist braces (the arm drives it) */
    float k_l = c->k_lin * sqrtf(kf), f_max = c->f_max * sqrtf(ff), k_a = c->k_ang * kf, t_max = c->t_max * ff;
    float t_stop = c->t_stop * ff, t_tw = c->t_twist * ff, rope = c->rope_rad / ff, w_max = c->w_max * (two || guard ? 1.4f : 1.0f);
    float i_grip = c->i_perp + c->mass * c->com * c->com, m_l = 1.0f / (1.0f / c->mass + c->com * c->com / c->i_perp);
    float c_l = 2 * c->zeta_lin * sqrtf(k_l * m_l), c_a = 2 * c->zeta_ang * sqrtf(k_a * i_grip);
    float c_tw = 2 * 0.9f * sqrtf(k_a * c->i_axis);
    float tv[3], tw[3];
    for (int i = 0; i < 3; i++) tv[i] = dp[i] / dt, tw[i] = r[i] / dt;
    memcpy(s->tw_last, tw, 12);
    int n = (int)ceilf(dt / c->sub_s);
    n = n < 1 ? 1 : n > 400 ? 400 : n;
    float h = dt / (float)n, s0 = s->s, fw_max = 0, flow = 0;
    for (int it = 1; it <= n; it++) {
        float f = (float)it / (float)n, tpi[3], ri[3], ei[4], tqi[4], a[3], cm[3], rp[3], vp[3], e[3];
        for (int j = 0; j < 3; j++) tpi[j] = s->tp[j] + dp[j] * f, ri[j] = r[j] * f;
        swg_qexp(ri, ei);
        qmul(ei, s->tq, tqi);
        qrot(s->q, an, a);
        for (int j = 0; j < 3; j++) cm[j] = s->p[j] + a[j] * c->com, rp[j] = -a[j] * c->com;
        vcross3(s->w, rp, vp);
        for (int j = 0; j < 3; j++) vp[j] += s->vc[j];
        /* the joint: in place at the grip point, in turn about it (across the blade and about it apart) */
        float F[3], T[3], wr[3], sq[4], dq2[4];
        for (int j = 0; j < 3; j++) F[j] = k_l * (tpi[j] - s->p[j]) + c_l * (tv[j] - vp[j]);
        float fl = vlen3(F);
        if (fl > f_max)
            for (int j = 0; j < 3; j++) F[j] *= f_max / fl;
        qconj(s->q, sq);
        qmul(tqi, sq, dq2);
        qnorm(dq2);
        qlog(dq2, e);
        for (int j = 0; j < 3; j++) wr[j] = tw[j] - s->w[j];
        float ea = vdot3(e, a), wa = vdot3(wr, a), Tp[3], Ta[3];
        for (int j = 0; j < 3; j++)
            Tp[j] = k_a * (e[j] - ea * a[j]) + c_a * (wr[j] - wa * a[j]), Ta[j] = (k_a * ea + c_tw * wa) * a[j];
        /* the limits: the part that brakes the sword's own swing at t_stop (the hand stops a swing harder than it starts
         * one: no run-on past the hand), the rest - speeding up, turning it a new way - at t_max */
        float wpv0[3], wpa0 = vdot3(s->w, a);
        for (int j = 0; j < 3; j++) wpv0[j] = s->w[j] - wpa0 * a[j];
        float wn0 = vlen3(wpv0), tbr = wn0 > 1e-3f ? vdot3(Tp, wpv0) / wn0 : 0, Tb[3] = {0, 0, 0};
        if (tbr < 0) {
            float k = (tbr < -t_stop ? -t_stop : tbr) / wn0;
            for (int j = 0; j < 3; j++) Tb[j] = k * wpv0[j], Tp[j] -= tbr / wn0 * wpv0[j];
        }
        /* the hand's strength falls with the speed it turns at (the muscles' force-velocity curve): strong when slow or
         * held, weak when the sword already swings fast - the tip cannot be whipped past what a real cut reaches */
        float hv = wn0 / c->w_hill, hill = hv >= 1 ? 0.0f : (1.0f - hv) / (1.0f + hv / 0.25f);
        float t_now = t_max * (hill > c->hill_min ? hill : c->hill_min), tl = vlen3(Tp), tal = vlen3(Ta);
        if (tl > t_now)
            for (int j = 0; j < 3; j++) Tp[j] *= t_now / tl;
        for (int j = 0; j < 3; j++) Tp[j] += Tb[j];
        if (tal > t_tw)
            for (int j = 0; j < 3; j++) Ta[j] *= t_tw / tal;
        for (int j = 0; j < 3; j++) T[j] = Tp[j] + Ta[j];
        float tq_[3];
        vcross3(rp, F, tq_);
        for (int j = 0; j < 3; j++) T[j] += tq_[j];
        /* the body this sub-step */
        float bo[3], bq[4], bo0[3], bq0[4];
        int inbody = (s->wound || s->pend) && s->body_have;
        if (inbody) {
            swp_body_at(s, f, bo, bq);
            swp_body_at(s, (float)(it - 1) / (float)n, bo0, bq0);
        }
        /* a pending hit: the wound opens where the blade is in the body */
        if (s->pend && inbody) {
            int lk = 0;
            float sc = 0, dd = 0, pen = -1;
            int in = swp_blade_in(s, c, s->p, a, bo, bq, &lk, &sc, &dd, &pen);
            s->pend_pen = pen > s->pend_pen ? pen : s->pend_pen;
            if (in) {
                float sw = sc, B[3], vB[3], vW[3], Wp0[3], Bl[3], rb[3];
                if (s->pend == SWP_W_STAB) { /* the stab: at the surface along the blade */
                    float L[3], al[3], ci[4], x[3], rr = s->body.r[lk];
                    for (int j = 0; j < 3; j++) L[j] = s->nodel[lk + 1][j] - s->nodel[lk][j];
                    vunit3(L);
                    qconj(bq, ci), qrot(ci, a, al), vcross3(al, L, x);
                    float sn = vlen3(x), half = sqrtf(fmaxf(rr * rr - dd * dd, 0.0f)) / fmaxf(sn, 0.3f);
                    sw = sc - half, sw = sw < c->s_guard ? c->s_guard : sw;
                }
                for (int j = 0; j < 3; j++) B[j] = s->p[j] + a[j] * sw, rb[j] = B[j] - cm[j];
                vcross3(s->w, rb, vB);
                swp_to_local(bo0, bq0, B, Bl), swp_to_world(bo, bq, Bl, Wp0); /* the body's own motion at B */
                for (int j = 0; j < 3; j++) vB[j] += s->vc[j], vW[j] = (Wp0[j] - B[j]) / h, vB[j] -= vW[j];
                s->in_v = vlen3(vB);
                float va = vdot3(vB, a), vpp[3];
                for (int j = 0; j < 3; j++) vpp[j] = vB[j] - va * a[j];
                if (vlen3(vpp) > 0.2f) {
                    float ci[4];
                    qconj(bq, ci), qrot(ci, vpp, s->din);
                } else { /* slow: inward, toward the link's axis */
                    float u, Bl2[3], cp[3];
                    swp_to_local(bo, bq, B, Bl2);
                    swp_seg_seg(Bl2, Bl2, s->nodel[lk], s->nodel[lk + 1], NULL, &u);
                    for (int j = 0; j < 3; j++) cp[j] = s->nodel[lk][j] + u * (s->nodel[lk + 1][j] - s->nodel[lk][j]),
                                                s->din[j] = cp[j] - Bl2[j];
                }
                vunit3(s->din);
                s->wound = s->pend, s->pend = 0, s->s = s0 = sw, s->link = lk, s->t0 = t, s->sliding = 0;
                s->cutting = s->in_v > c->v_slice, s->tear_why = 0; /* a moving edge goes in slicing */
                swp_to_local(bo, bq, B, s->Wl);
                memcpy(s->W0l, s->Wl, 12), memcpy(s->Wt, B, 12);
                float ci2[4];
                qconj(bq, ci2), qrot(ci2, a, s->a0l); /* the blade's way in (the body's frame) */
                s->wounds++, s->ev |= SWP_EV_START;
            } else if (t - s->pend_t > c->pend_s) {
                swp_wound_end(s, SWP_END_MISS);
            }
        }
        /* the wound's force on the blade */
        float Fw[3] = {0, 0, 0}, B[3] = {0, 0, 0};
        if (s->wound && inbody) {
            float Wt[3], Wp[3], vW[3], vB[3], rb[3], dl[3], u[3];
            swp_to_world(bo, bq, s->Wl, Wt);
            swp_to_world(bo0, bq0, s->Wl, Wp);
            for (int j = 0; j < 3; j++) B[j] = s->p[j] + a[j] * s->s, rb[j] = B[j] - cm[j], vW[j] = (Wt[j] - Wp[j]) / h;
            vcross3(s->w, rb, vB);
            for (int j = 0; j < 3; j++) vB[j] += s->vc[j], dl[j] = B[j] - Wt[j], u[j] = vB[j] - vW[j];
            float da = vdot3(dl, a), ua = vdot3(u, a), dpp[3], up[3];
            for (int j = 0; j < 3; j++) dpp[j] = dl[j] - da * a[j], up[j] = u[j] - ua * a[j];
            float sb = vdot3(rb, a), m_w = 1.0f / (1.0f / c->mass + sb * sb / c->i_perp);
            float c_wp = 2 * c->zeta_w * sqrtf(c->k_w * m_w), c_wa = 2 * c->zeta_w * sqrtf(c->k_w * c->mass);
            /* across the blade */
            float Yp = c->y_stab_lat;
            if (s->wound == SWP_W_CUT) {
                float dint[3], axd = 0;
                int lk = 0;
                qrot(bq, s->din, dint);
                swp_body_sd(s, s->Wl, &lk, &axd);
                int fwd = vdot3(dpp, dint) >= 0, core = axd < c->core_frac * s->body.r[lk];
                /* the edge slices while it moves (kinetic; the first cm under the skin easier), a slow push does not
                 * cut (static) */
                float dep = s->body.r[lk] - axd, skin = 0.3f + 0.7f * fminf(1.0f, fmaxf(0.0f, dep) / c->skin_m);
                Yp = !fwd ? c->y_back : (s->cutting ? c->y_cut : c->y_cut_s) * skin * (core ? c->core_k : 1.0f);
            }
            float kd = c->k_w * vlen3(dpp), Fp[3];
            if (kd > Yp) { /* the wound gives: it moves on with the blade */
                float g = 1.0f - Yp / kd;
                flow += vlen3(dpp) * g;
                s->cutting = vlen3(dpp) * g / h > c->v_slice ? 1 : s->cutting && vlen3(dpp) * g / h > 0.3f * c->v_slice;
                for (int j = 0; j < 3; j++) Wt[j] += dpp[j] * g, dpp[j] *= 1.0f - g;
                swp_to_local(bo, bq, Wt, s->Wl);
                for (int j = 0; j < 3; j++) Fp[j] = -c->k_w * dpp[j];
            } else {
                if (vlen3(up) < 0.3f * c->v_slice) s->cutting = 0; /* stalled: a slow push does not cut */
                for (int j = 0; j < 3; j++) Fp[j] = -c->k_w * dpp[j] - c_wp * up[j];
                float fpl = vlen3(Fp);
                if (fpl > 1.5f * Yp)
                    for (int j = 0; j < 3; j++) Fp[j] *= 1.5f * Yp / fpl;
            }
            /* along the blade: da < 0 = the sword drawn back (out of the wound), > 0 pushed in */
            float Ya = s->wound == SWP_W_CUT ? c->y_slide : da < 0 ? (s->sliding ? c->y_pull_kin : c->y_pull) : c->y_push;
            int at_guard = s->s <= c->s_guard + 1e-4f && da > 0;
            float ka = c->k_w * fabsf(da), Fa;
            if (ka > Ya && !at_guard) {
                float ds = -da * (1.0f - Ya / ka);
                if (s->s + ds < c->s_guard) ds = c->s_guard - s->s;
                s->s += ds, da += ds;
                Fa = -c->k_w * da;
                s->sliding = fabsf(ds) / h > 0.02f ? 1.0f : 0.0f;
            } else {
                Fa = -c->k_w * da - c_wa * ua;
                if (fabsf(Fa) > 1.5f * Ya && !at_guard) Fa *= 1.5f * Ya / fabsf(Fa);
                if (fabsf(ua) < 0.02f) s->sliding = 0;
            }
            for (int j = 0; j < 3; j++) Fw[j] = Fp[j] + Fa * a[j];
            memcpy(s->Wt, Wt, 12);
            float fwl = vlen3(Fw);
            fw_max = fwl > fw_max ? fwl : fw_max;
            float tb[3];
            vcross3(rb, Fw, tb);
            for (int j = 0; j < 3; j++) T[j] += tb[j];
        }
        /* integrate (semi-implicit Euler about the centre of mass) */
        float Iw[3], gy[3], wa2 = vdot3(s->w, a);
        for (int j = 0; j < 3; j++) Iw[j] = c->i_axis * wa2 * a[j] + c->i_perp * (s->w[j] - wa2 * a[j]);
        vcross3(s->w, Iw, gy);
        for (int j = 0; j < 3; j++) T[j] -= gy[j], s->vc[j] += (F[j] + Fw[j]) / c->mass * h;
        float Ta2 = vdot3(T, a);
        for (int j = 0; j < 3; j++) s->w[j] += (Ta2 * a[j] / c->i_axis + (T[j] - Ta2 * a[j]) / c->i_perp) * h;
        float wpa = vdot3(s->w, a), wp[3];
        for (int j = 0; j < 3; j++) wp[j] = s->w[j] - wpa * a[j];
        float wpl = vlen3(wp);
        if (wpl > w_max)
            for (int j = 0; j < 3; j++) s->w[j] = wp[j] * w_max / wpl + wpa * a[j];
        float stp[3] = {s->w[0] * h, s->w[1] * h, s->w[2] * h}, es[4], nq[4];
        for (int j = 0; j < 3; j++) cm[j] += s->vc[j] * h;
        swg_qexp(stp, es);
        qmul(es, s->q, nq);
        qnorm(nq);
        memcpy(s->q, nq, 16);
        qrot(s->q, an, a);
        for (int j = 0; j < 3; j++) s->p[j] = cm[j] - a[j] * c->com;
        /* free: the ropes (never farther off the controller than rope / rope_m; no speed away there) */
        if (!s->wound) {
            float e2[3], dq3[4];
            qconj(s->q, sq);
            qmul(tqi, sq, dq3);
            qnorm(dq3);
            qlog(dq3, e2);
            float el = vlen3(e2);
            s->rope_now = s->rope_now - c->rope_close * h > rope ? s->rope_now - c->rope_close * h : rope;
            if (el > s->rope_now) {
                float eu[3] = {e2[0] / el, e2[1] / el, e2[2] / el}, bk[3], eb[4];
                for (int j = 0; j < 3; j++) bk[j] = eu[j] * (el - s->rope_now);
                swg_qexp(bk, eb);
                qmul(eb, s->q, nq);
                qnorm(nq);
                memcpy(s->q, nq, 16); /* about the grip point (p kept) */
                float grow = vdot3(tw, eu) - vdot3(s->w, eu);
                if (grow > 0)
                    for (int j = 0; j < 3; j++) s->w[j] += grow * eu[j];
                s->roped++;
            }
            float dd[3] = {s->p[0] - tpi[0], s->p[1] - tpi[1], s->p[2] - tpi[2]}, dl2 = vlen3(dd);
            s->rope_m_now = s->rope_m_now - c->rope_close_m * h > c->rope_m ? s->rope_m_now - c->rope_close_m * h : c->rope_m;
            if (dl2 > s->rope_m_now) {
                for (int j = 0; j < 3; j++) s->p[j] = tpi[j] + dd[j] * s->rope_m_now / dl2;
                float du[3] = {dd[0] / dl2, dd[1] / dl2, dd[2] / dl2}, out = vdot3(s->vc, du) - vdot3(tv, du);
                if (out > 0)
                    for (int j = 0; j < 3; j++) s->vc[j] -= out * du[j];
            }
        }
        /* the wound's ends */
        if (s->wound) {
            float dd[3] = {s->p[0] - tpi[0], s->p[1] - tpi[1], s->p[2] - tpi[2]}, dq4[4];
            qconj(s->q, sq);
            qmul(tqi, sq, dq4);
            qnorm(dq4);
            float ang = 2.0f * acosf(fminf(1.0f, fabsf(dq4[3])));
            s->off_rad = ang, s->stretch = vlen3(dd);
            if (s->s > c->s_tip - 0.005f) {
                swp_wound_end(s, SWP_END_OUT);
            } else if (vlen3(dd) > c->tear_m || ang > c->tear_rad) {
                s->tear_why = vlen3(dd) > c->tear_m ? 1 : 2;
                swp_wound_end(s, SWP_END_TEAR);
            } else if (t - s->t0 > c->max_s) {
                swp_wound_end(s, SWP_END_TIME);
            } else if (s->wound == SWP_W_STAB && (vdist3(s->Wl, s->W0l) > c->stab_cut_m || swp_lever(s, a, bo, bq) > c->stab_lever_rad)) {
                s->tear_why = vdist3(s->Wl, s->W0l) > c->stab_cut_m ? 4 : 3;
                s->dbg_lever = swp_lever(s, a, bo, bq), s->dbg_cut = vdist3(s->Wl, s->W0l);
                swp_wound_end(s, SWP_END_TEAR); /* levered out sideways: the edge cut its way out */
            } else if (s->wound == SWP_W_CUT && swp_body_sd(s, s->Wl, NULL, NULL) > c->enter_m) {
                float mv[3] = {s->Wl[0] - s->W0l[0], s->Wl[1] - s->W0l[1], s->Wl[2] - s->W0l[2]};
                swp_wound_end(s, vdot3(mv, s->din) >= 0 ? SWP_END_THROUGH : SWP_END_BACK);
            }
        }
        s->steps++;
    }
    memcpy(s->tp, p, 12), memcpy(s->tq, qn, 16);
    s->t = t;
    /* this frame's numbers */
    float dd[3] = {s->p[0] - p[0], s->p[1] - p[1], s->p[2] - p[2]}, a[3], dq5[4], sq2[4];
    s->stretch = vlen3(dd);
    qconj(s->q, sq2);
    qmul(qn, sq2, dq5);
    qnorm(dq5);
    s->off_rad = 2.0f * acosf(fminf(1.0f, fabsf(dq5[3])));
    qrot(s->q, an, a);
    float wa = vdot3(s->w, a), wpv[3], vt[3], rt[3] = {a[0] * (c->s_tip - c->com), a[1] * (c->s_tip - c->com),
                                                        a[2] * (c->s_tip - c->com)};
    for (int j = 0; j < 3; j++) wpv[j] = s->w[j] - wa * a[j];
    vcross3(s->w, rt, vt);
    for (int j = 0; j < 3; j++) vt[j] += s->vc[j];
    s->tip_v = vlen3(vt), s->f_wound = fw_max;
    s->slide_v = s->wound ? (s->s - s0) / dt : 0, s->flow_v = flow / dt;
    s->depth = 0;
    if (s->wound == SWP_W_STAB) {
        s->depth = c->s_tip - s->s;
    } else if (s->wound == SWP_W_CUT) {
        float axd = 0;
        int lk = 0;
        swp_body_sd(s, s->Wl, &lk, &axd);
        s->depth = s->body.r[lk] - axd;
    }
    float twl = vlen3(tw);
    if (s->off_rad > s->peak_off) s->peak_off = s->off_rad;
    if (s->stretch > s->peak_stretch) s->peak_stretch = s->stretch;
    if (vlen3(wpv) > s->peak_w) s->peak_w = vlen3(wpv);
    if (twl > s->peak_tw) s->peak_tw = twl;
    if (s->tip_v > s->peak_tip) s->peak_tip = s->tip_v;
    if (s->depth > s->peak_depth) s->peak_depth = s->depth;
    memcpy(op, s->p, 12), memcpy(oq, s->q, 16);
    return 1;
}
