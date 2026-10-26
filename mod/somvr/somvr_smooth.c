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

/* ---- the weight ---------------------------------------------------------------------------------------------------- */
void swg_defaults(swg_cfg *c, int level, float pos_unit) {
    const float deg = 0.01745329f;
    c->two_k = 1.6f;
    c->gate_lo = 1.5f, c->gate_hi = 3.5f;                       /* rad/s: 86..200 deg/s */
    c->gate_lo_v = 0.6f * pos_unit, c->gate_hi_v = 1.5f * pos_unit; /* m/s */
    c->reset_rad = 0.6f, c->reset_rate = 40.0f; /* 34 deg in one step and faster than 40 rad/s: a jump */
    if (level == SWG_HEAVY)
        c->omega = 26.0f, c->cap_rad = 6.0f * deg, c->cap_pos = 0.010f * pos_unit;
    else
        c->omega = 40.0f, c->cap_rad = 3.0f * deg, c->cap_pos = 0.005f * pos_unit;
}

void swg_reset(swg_state *s) { memset(s, 0, sizeof *s); }

static float smooth01(float lo, float hi, float x) {
    if (x <= lo) return 0;
    if (x >= hi) return 1;
    float t = (x - lo) / (hi - lo);
    return t * t * (3 - 2 * t);
}

/* the critically damped decay of (x, v) toward 0 over dt, exact */
static void cd_step(float *x, float *v, float om, float dt) {
    float ex = expf(-om * dt), b = *v + om * *x;
    float nx = (*x + b * dt) * ex, nv = (*v - om * b * dt) * ex;
    *x = nx, *v = nv;
}

/* |x| <= cap; when it is held at the cap, no rate outward */
static int cap3(float x[3], float v[3], float cap) {
    float n = vlen3(x);
    if (n <= cap || n <= 0) return 0;
    float u[3] = {x[0] / n, x[1] / n, x[2] / n}, dv = v[0] * u[0] + v[1] * u[1] + v[2] * u[2];
    for (int i = 0; i < 3; i++) {
        x[i] = u[i] * cap;
        if (dv > 0) v[i] -= dv * u[i];
    }
    return 1;
}

int swg_step(swg_state *s, const swg_cfg *c, const float q[4], const float p[3], float dt, int two, float rot[3],
             float pos[3]) {
    memset(rot, 0, 12), memset(pos, 0, 12);
    if (!finite4(q) || !finite3(p)) {
        s->have = 0;
        return 0;
    }
    float qn[4];
    memcpy(qn, q, 16);
    qnorm(qn);
    if (s->have && dt <= 0) { /* the same instant: the same offsets */
        memcpy(rot, s->e, 12), memcpy(pos, s->d, 12);
        return 1;
    }
    float cq[4], d[4], r[3] = {0, 0, 0};
    if (s->have) {
        qconj(s->q, cq);
        qmul(qn, cq, d);
        qnorm(d);
        qlog(d, r);
    }
    if (!s->have || dt > 0.1f || (vlen3(r) > c->reset_rad && vlen3(r) > c->reset_rate * dt)) {
        float keep = s->peak_rad, keepp = s->peak_pos;
        uint64_t st = s->steps, cp = s->capped, rs = s->resets + (s->have ? 1 : 0);
        swg_reset(s);
        s->peak_rad = keep, s->peak_pos = keepp, s->steps = st, s->capped = cp, s->resets = rs;
        memcpy(s->q, qn, 16), memcpy(s->p, p, 12);
        s->have = 1;
        return 0;
    }
    float w[3] = {r[0] / dt, r[1] / dt, r[2] / dt}, v[3] = {(p[0] - s->p[0]) / dt, (p[1] - s->p[1]) / dt,
                                                         (p[2] - s->p[2]) / dt};
    float gw = smooth01(c->gate_lo, c->gate_hi, vlen3(w)), gv = smooth01(c->gate_lo_v, c->gate_hi_v, vlen3(v));
    float k = two ? c->two_k : 1.0f, om = c->omega * k, capr = c->cap_rad / k, capp = c->cap_pos / k;
    for (int i = 0; i < 3; i++) {
        float u = w[i] * gw, uv = v[i] * gv;
        s->ve[i] -= u - s->u[i]; /* the hand's change of speed; the blade's own inertia keeps its speed */
        s->vd[i] -= uv - s->uv[i];
        s->u[i] = u, s->uv[i] = uv;
        cd_step(&s->e[i], &s->ve[i], om, dt);
        cd_step(&s->d[i], &s->vd[i], om, dt);
    }
    int capped = cap3(s->e, s->ve, capr);
    capped |= cap3(s->d, s->vd, capp);
    s->capped += (uint64_t)capped;
    memcpy(s->q, qn, 16), memcpy(s->p, p, 12);
    s->steps++;
    float ne = vlen3(s->e), nd = vlen3(s->d);
    if (ne > s->peak_rad) s->peak_rad = ne;
    if (nd > s->peak_pos) s->peak_pos = nd;
    memcpy(rot, s->e, 12), memcpy(pos, s->d, 12);
    return 1;
}
