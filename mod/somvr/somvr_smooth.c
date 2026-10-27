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
