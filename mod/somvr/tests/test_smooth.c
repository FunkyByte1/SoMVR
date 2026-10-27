/* test_smooth.c - the hands' smoothing and the sword's weight (somvr_smooth.c; docs/re-xr-pipeline.md 48): a still
 * hand with tracking noise and a tremor (synthetic, at 90 Hz: the user's session logs one HAND line a second, too
 * sparse to replay), the noise cut at the hand and at a 1 m blade's tip; swings at 3, 6, 10 rad/s and a 1 m/s move:
 * the delay; the onset from rest; without the runtime's velocities; resets (a jump, a gap, untracked), the same time
 * again, measuring without filtering; the weight (build 16): recorded-style cuts and wiggles - the lag, the settle,
 * the wiggle's share, the speed cap, the twist kept, two hands near 1:1, 20 Hz, the left hand taking the hilt, resets. */
#include "../somvr_smooth.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

#define DEG 0.01745329f
#define DT_NS 11111111LL /* 90 Hz */

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static double urand(void) {
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return ((rng >> 11) + 0.5) / 9007199254740992.0;
}
static double gauss(void) { return sqrt(-2.0 * log(urand())) * cos(6.283185307179586 * urand()); }

static void qmul(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                  a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                  a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
static void qrot(const float q[4], const float v[3], float o[3]) {
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float c1[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    float c2[3] = {u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0]};
    for (int i = 0; i < 3; i++) o[i] = v[i] + 2 * w * c1[i] + 2 * c2[i];
}
/* the 1 m tip's distance between two poses */
static float tipd(const float qa[4], const float pa[3], const float qb[4], const float pb[3]) {
    float t[3] = {0, 0, -1}, a[3], b[3];
    qrot(qa, t, a), qrot(qb, t, b);
    float d[3] = {pa[0] + a[0] - pb[0] - b[0], pa[1] + a[1] - pb[1] - b[1], pa[2] + a[2] - pb[2] - b[2]};
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}
static float pdist(const float a[3], const float b[3]) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

/* the truth: a grip held 30 cm ahead, tilted, turning about axis ax at rate w(t) (rad/s), moving at v (m/s) along x */
typedef struct {
    float q0[4], p0[3], ax[3];
} truth;
static void truth_at(const truth *T, float ang, float x, float q[4], float p[3]) {
    float r[3] = {T->ax[0] * ang, T->ax[1] * ang, T->ax[2] * ang}, e[4];
    swg_qexp(r, e);
    qmul(e, T->q0, q);
    p[0] = T->p0[0] + x, p[1] = T->p0[1], p[2] = T->p0[2];
}
/* the measured hand: the truth + white noise (rot_sd per axis, rad; pos_sd per axis, m); the runtime's angular velocity
 * = the true one + gyro noise; vel_valid 3 or 0 */
static void noisy(somvr_hand *h, const float q[4], const float p[3], float rot_sd, float pos_sd, const float w[3],
                  const float v[3], int vel, int64_t t) {
    memset(h, 0, sizeof *h);
    float r[3] = {(float)(gauss() * rot_sd), (float)(gauss() * rot_sd), (float)(gauss() * rot_sd)}, e[4];
    swg_qexp(r, e);
    qmul(e, q, h->pose.q);
    for (int i = 0; i < 3; i++) {
        h->pose.p[i] = p[i] + (float)(gauss() * pos_sd);
        h->ang[i] = w[i] + (float)(gauss() * 0.02);
        h->lin[i] = v[i] + (float)(gauss() * 0.005);
    }
    h->valid = 1, h->tracked = 1, h->vel_valid = vel, h->time = t, h->src = SOMVR_HSRC_LOCATED;
}

static truth T0;

/* a still hand: noise + a 9 Hz tremor of amp_deg; returns the tip RMS error vs the truth (raw, filtered) and fills
 * the state's measurement */
static void still_run(float rot_sd, float pos_sd, float amp_deg, int vel, float *raw_rms, float *out_rms,
                      hsm_state *st) {
    hsm_cfg c;
    hsm_defaults(&c);
    hsm_reset(st);
    double sr = 0, so = 0;
    int n = 0;
    for (int f = 0; f < 3000; f++) {
        double ts = f * DT_NS * 1e-9;
        float ang = amp_deg * DEG * (float)sin(6.283185 * 9.0 * ts), q[4], p[3];
        float wr = amp_deg * DEG * 6.283185f * 9.0f * (float)cos(6.283185 * 9.0 * ts);
        float w[3] = {T0.ax[0] * wr, T0.ax[1] * wr, T0.ax[2] * wr}, v[3] = {0, 0, 0};
        truth_at(&T0, ang, 0, q, p);
        somvr_hand h;
        noisy(&h, q, p, rot_sd, pos_sd, w, v, vel, 1000000000LL + f * DT_NS);
        float rq[4], rp[3];
        memcpy(rq, h.pose.q, 16), memcpy(rp, h.pose.p, 12);
        hsm_step(st, &c, 1, 1.0f, &h);
        if (f < 200) continue; /* settled */
        /* against the still pose (the tremor is the hand's shake: steadier is the aim) */
        float qs[4], ps[3];
        truth_at(&T0, 0, 0, qs, ps);
        float a = tipd(rq, rp, qs, ps), b = tipd(h.pose.q, h.pose.p, qs, ps);
        sr += (double)a * a, so += (double)b * b, n++;
    }
    *raw_rms = (float)sqrt(sr / n), *out_rms = (float)sqrt(so / n);
}

static void test_still(void) {
    hsm_state st;
    float a, b;
    /* tracking noise alone: 0.1 deg per axis, 0.5 mm per axis */
    still_run(0.1f * DEG, 0.0005f, 0, 3, &a, &b, &st);
    printf("still, noise 0.1 deg + 0.5 mm per axis: the 1 m tip %.2f -> %.2f mm RMS (x%.1f); 2nd diff at the tip %.2f -> "
           "%.2f mm RMS over %llu still frames\n",
           a * 1e3, b * 1e3, a / b, sqrt(st.jit.raw_tip2 / st.jit.n) * 1e3, sqrt(st.jit.out_tip2 / st.jit.n) * 1e3,
           (unsigned long long)st.jit.n);
    CHECK(a / b >= 3.0f, "the still tip's noise cut x%.2f (want >= 3)", a / b);
    CHECK(st.jit.n > 2500, "still frames measured %llu", (unsigned long long)st.jit.n);
    double jr = sqrt(st.jit.raw_tip2 / st.jit.n), jo = sqrt(st.jit.out_tip2 / st.jit.n);
    CHECK(jr / jo >= 8.0, "the frame-to-frame jitter (2nd difference) cut x%.1f (want >= 8)", jr / jo);
    /* the measurement's raw number = the injected noise: 2nd difference of white noise = sqrt(6) sd; the tip's sd from
     * two rotation axes (1 m) and three position axes */
    double tip_sd = sqrt(2.0 * (0.1 * DEG) * (0.1 * DEG) + 3.0 * 0.0005 * 0.0005);
    CHECK(fabs(jr / (sqrt(6.0) * tip_sd) - 1.0) < 0.15, "measured raw jitter %.3f mm vs the injected %.3f mm",
          jr * 1e3, sqrt(6.0) * tip_sd * 1e3);
    /* plus a 9 Hz tremor of 0.15 deg (the hand's own shake) */
    still_run(0.1f * DEG, 0.0005f, 0.15f, 3, &a, &b, &st);
    printf("still + 9 Hz tremor 0.15 deg: the 1 m tip %.2f -> %.2f mm RMS (x%.1f)\n", a * 1e3, b * 1e3, a / b);
    CHECK(a / b >= 2.5f, "with a tremor: cut x%.2f (want >= 2.5)", a / b);
    /* no runtime velocity: the finite-difference speed */
    still_run(0.1f * DEG, 0.0005f, 0, 0, &a, &b, &st);
    printf("still, no runtime velocity: the 1 m tip %.2f -> %.2f mm RMS (x%.1f)\n", a * 1e3, b * 1e3, a / b);
    CHECK(a / b >= 1.8f, "no velocities: cut x%.2f (want >= 1.8)", a / b);
}

/* a constant swing at w rad/s (+ the noise): the steady delay = the angle behind the truth / w, ms; the onset's worst
 * angle behind (deg) in the first 6 frames from rest */
static void swing_run(float w, int vel, float *delay_ms, float *onset_deg, float *max_tip_mm) {
    hsm_cfg c;
    hsm_defaults(&c);
    hsm_state st;
    hsm_reset(&st);
    double sum = 0;
    int n = 0;
    float worst = 0, tipmax = 0;
    for (int f = 0; f < 60; f++) {
        int moving = f >= 20;
        float ang = moving ? w * (float)((f - 20) * DT_NS * 1e-9) : 0, q[4], p[3];
        float wv[3] = {T0.ax[0] * (moving ? w : 0), T0.ax[1] * (moving ? w : 0), T0.ax[2] * (moving ? w : 0)},
              v[3] = {0, 0, 0};
        truth_at(&T0, ang, 0, q, p);
        somvr_hand h;
        noisy(&h, q, p, 0, 0, wv, v, vel, 1000000000LL + f * DT_NS);
        hsm_step(&st, &c, 1, 1.0f, &h);
        float lag = hsm_qangle(h.pose.q, q);
        if (f >= 20 && f < 26 && lag > worst) worst = lag;
        if (f >= 20) {
            float tdd = tipd(h.pose.q, h.pose.p, q, p);
            if (tdd > tipmax) tipmax = tdd;
        }
        if (f >= 40) sum += lag, n++;
    }
    *delay_ms = (float)(sum / n / w * 1e3), *onset_deg = worst / DEG, *max_tip_mm = tipmax * 1e3f;
}

static void test_swing(void) {
    float rates[] = {3, 6, 10};
    for (int i = 0; i < 3; i++) {
        float d, o, tm;
        swing_run(rates[i], 3, &d, &o, &tm);
        float tau = hsm_tau(HSM_ROT_MIN_HZ, HSM_ROT_BETA, rates[i]) * 1e3f;
        printf("swing %.0f rad/s (%.0f deg/s): delay %.2f ms (tau %.2f ms), onset worst %.2f deg, the tip behind at most "
               "%.1f mm\n",
               rates[i], rates[i] / DEG, d, tau, o, tm);
        CHECK(d < 5.0f, "swing %.0f rad/s: delay %.2f ms (want < 5)", rates[i], d);
        CHECK(fabsf(d - tau) < 0.3f, "swing %.0f: the delay %.2f = tau %.2f", rates[i], d, tau);
        CHECK(o < rates[i] * 5.0e-3f / DEG + 0.01f, "swing %.0f: onset %.2f deg (want under 5 ms of the swing)", rates[i], o);
    }
    float d, o, tm;
    swing_run(6, 0, &d, &o, &tm);
    printf("swing 6 rad/s without runtime velocities: delay %.2f ms, onset worst %.2f deg\n", d, o);
    CHECK(d < 5.0f, "no velocities, 6 rad/s: steady delay %.2f ms", d);
    /* a 1 m/s move of the hand: the position's delay */
    hsm_cfg c;
    hsm_defaults(&c);
    hsm_state st;
    hsm_reset(&st);
    double sum = 0;
    int n = 0;
    for (int f = 0; f < 60; f++) {
        float x = f >= 20 ? 1.0f * (float)((f - 20) * DT_NS * 1e-9) : 0, q[4], p[3];
        float wv[3] = {0, 0, 0}, v[3] = {f >= 20 ? 1.0f : 0, 0, 0};
        truth_at(&T0, 0, x, q, p);
        somvr_hand h;
        noisy(&h, q, p, 0, 0, wv, v, 3, 1000000000LL + f * DT_NS);
        hsm_step(&st, &c, 1, 1.0f, &h);
        if (f >= 40) sum += pdist(h.pose.p, p), n++;
    }
    float pd = (float)(sum / n / 1.0 * 1e3);
    printf("a 1 m/s move: delay %.2f ms (tau %.2f ms)\n", pd, hsm_tau(HSM_POS_MIN_HZ, HSM_POS_BETA, 1.0f) * 1e3f);
    CHECK(pd < 5.0f, "1 m/s: delay %.2f ms", pd);
}

static void test_resets(void) {
    hsm_cfg c;
    hsm_defaults(&c);
    hsm_state st;
    hsm_reset(&st);
    float q[4], p[3], z[3] = {0, 0, 0};
    truth_at(&T0, 0, 0, q, p);
    somvr_hand h;
    for (int f = 0; f < 10; f++) noisy(&h, q, p, 0, 0, z, z, 3, 1000000000LL + f * DT_NS), hsm_step(&st, &c, 1, 1, &h);
    /* a jump of 30 cm: the raw pose at once */
    float p2[3] = {p[0] + 0.3f, p[1], p[2]};
    noisy(&h, q, p2, 0, 0, z, z, 3, 1000000000LL + 10 * DT_NS);
    int r0 = (int)st.resets;
    hsm_step(&st, &c, 1, 1, &h);
    CHECK(pdist(h.pose.p, p2) < 1e-6f && (int)st.resets == r0 + 1, "a 30 cm jump: raw at once (%.4f m off)",
          pdist(h.pose.p, p2));
    /* a 40 deg turn in one frame: raw at once */
    float q2[4], p3[3];
    truth_at(&T0, 40 * DEG, 0.3f, q2, p3);
    noisy(&h, q2, p3, 0, 0, z, z, 3, 1000000000LL + 11 * DT_NS);
    hsm_step(&st, &c, 1, 1, &h);
    CHECK(hsm_qangle(h.pose.q, q2) < 1e-4f, "a 40 deg jump: raw at once (%.3f deg)", hsm_qangle(h.pose.q, q2) / DEG);
    /* a 20 Hz swing at 15 rad/s (43 deg a frame, the runtime's velocity says so): filtered, not reset */
    hsm_reset(&st);
    for (int f = 0; f < 12; f++) {
        float qq[4], pp[3], wv[3] = {T0.ax[0] * 15, T0.ax[1] * 15, T0.ax[2] * 15};
        truth_at(&T0, f >= 4 ? 15.0f * (f - 4) * 0.05f : 0, 0, qq, pp);
        noisy(&h, qq, pp, 0, 0, f >= 4 ? wv : z, z, 3, 3000000000LL + f * 50000000LL);
        hsm_step(&st, &c, 1, 1, &h);
    }
    CHECK(st.resets == 1, "a 20 Hz swing of 43 deg a frame: no reset (%llu)", (unsigned long long)st.resets);
    truth_at(&T0, 40 * DEG, 0.3f, q2, p3);
    noisy(&h, q2, p3, 0, 0, z, z, 3, 1000000000LL + 11 * DT_NS);
    hsm_reset(&st);
    hsm_step(&st, &c, 1, 1, &h);
    /* a gap of 0.2 s: raw */
    float p4[3] = {p3[0] + 0.05f, p3[1], p3[2]};
    noisy(&h, q2, p4, 0, 0, z, z, 3, 1000000000LL + 11 * DT_NS + 200000000LL);
    hsm_step(&st, &c, 1, 1, &h);
    CHECK(pdist(h.pose.p, p4) < 1e-6f, "after a 0.2 s gap: raw");
    /* the same time again: the same pose as the last output */
    float lastq[4], lastp[3];
    memcpy(lastq, h.pose.q, 16), memcpy(lastp, h.pose.p, 12);
    float p5[3] = {p4[0] + 0.01f, p4[1], p4[2]};
    noisy(&h, q2, p5, 0, 0, z, z, 3, 1000000000LL + 11 * DT_NS + 200000000LL);
    hsm_step(&st, &c, 1, 1, &h);
    CHECK(pdist(h.pose.p, lastp) < 1e-6f && hsm_qangle(h.pose.q, lastq) < 1e-5f && st.same_time == 1,
          "the same time: the same pose");
    /* untracked: passes, the next valid one starts over */
    h.valid = 0;
    CHECK(hsm_step(&st, &c, 1, 1, &h) == 0 && !st.have, "invalid: reset");
    /* on = 0: measured, the pose untouched */
    hsm_reset(&st);
    for (int f = 0; f < 50; f++) {
        noisy(&h, q, p, 0.1f * DEG, 0.0005f, z, z, 3, 2000000000LL + f * DT_NS);
        somvr_hand raw = h;
        hsm_step(&st, &c, 0, 1, &h);
        CHECK(!memcmp(&raw, &h, sizeof h), "off: untouched (frame %d)", f);
    }
    CHECK(st.jit.n > 40 && st.jit.out_tip2 < st.jit.raw_tip2, "off: still measured (%llu)", (unsigned long long)st.jit.n);
    /* the parser */
    float a = 0, b = 0;
    CHECK(hsm_parse_pair("1.5,12", &a, &b) && a == 1.5f && b == 12.0f, "parse 1.5,12");
    CHECK(!hsm_parse_pair("0,12", &a, &b) && !hsm_parse_pair("2", &a, &b) && !hsm_parse_pair("2,x", &a, &b) &&
              !hsm_parse_pair("2,3,4", &a, &b),
          "parse rejects");
}

/* ---- the weight (build 16) ----------------------------------------------------------------------------------------
 * Recorded-style hand motion at 90 Hz (the sessions log no per-frame hands): the grip turned about an axis across the
 * blade by a profile - a minimum-jerk cut (the shape of a human reach or cut), a sinusoidal wiggle - plus a 9 Hz
 * tremor of 0.1 deg and 0.05 deg of tracking noise per axis. The blade's angle along the swing = the hand's + the
 * offset's component on the swing axis; the lag = the offset's size. */
static const float BLADE[3] = {0, 0.34202f, 0.93969f}; /* the blade in the grip: SOMVR_SWORD_ANGLE 20 deg */
static float g_bw[3];                                   /* the swing axis (world, across the blade) */
static float g_q0[4];

enum { P_CUT, P_WIGGLE, P_ROLL };
typedef struct {
    int kind;
    float amp_deg, T_s; /* a cut: amp_deg over T_s; a wiggle: +-amp_deg at 1 / T_s Hz for 1 s; a roll: amp_deg over T_s */
    float t0;           /* the start (s) */
} prof;
static float prof_at(const prof *P, float t) {
    float x = t - P->t0;
    if (x < 0) return 0;
    if (P->kind == P_WIGGLE) return x > 1.0f ? 0 : P->amp_deg * DEG * sinf(6.2831853f * x / P->T_s);
    float u = x / P->T_s;
    u = u > 1 ? 1 : u;
    return P->amp_deg * DEG * (10 * u * u * u - 15 * u * u * u * u + 6 * u * u * u * u * u);
}

typedef struct {
    float lag;       /* deg: the largest angle off the hand */
    float run_on;    /* deg: the most the blade went past the hand after the cut's end (along the swing) */
    float settle_ms; /* after the cut's end until it stays within 1 deg */
    float amp_pct;   /* a wiggle: the blade's swing amplitude / the hand's (the middle 0.6 s) */
    float w_blade, w_hand; /* rad/s: the peak angular speeds along the swing */
    float twist;     /* deg: the largest offset part along the blade (none: the twist is the hand's) */
    float still;     /* deg: the largest offset before the motion (the tremor alone) */
    float step_max;  /* deg: the largest change of the offset in one frame */
} wres;

static void hand_q(float ang, float roll, float q[4]) {
    float r[3] = {g_bw[0] * ang, g_bw[1] * ang, g_bw[2] * ang}, e[4], qr[4], t[4], bl[3];
    qrot(g_q0, BLADE, bl);
    float rr[3] = {bl[0] * roll, bl[1] * roll, bl[2] * roll};
    swg_qexp(rr, qr);
    qmul(qr, g_q0, t);
    swg_qexp(r, e);
    qmul(e, t, q);
}

static wres wrun(int level, int two, const prof *P, int fps, int noise, int two_from_f) {
    swg_cfg c;
    swg_defaults(&c, level);
    swg_state s;
    swg_reset(&s);
    wres r;
    memset(&r, 0, sizeof r);
    float dt = 1.0f / (float)fps, end = P->t0 + (P->kind == P_WIGGLE ? 1.0f : P->T_s), last_big = end;
    float bmin = 1e9f, bmax = -1e9f, hmin = 1e9f, hmax = -1e9f, pb = 0, ph = 0, prot[3] = {0, 0, 0};
    int nf = (int)(1.8f * (float)fps);
    for (int f = 0; f <= nf; f++) {
        float t = (float)f * dt, th = P->kind == P_ROLL ? 0 : prof_at(P, t), roll = P->kind == P_ROLL ? prof_at(P, t) : 0;
        float tr = noise ? 0.1f * DEG * sinf(6.2831853f * 9.0f * t) : 0, q[4], rot[3];
        hand_q(th + tr, roll, q);
        if (noise) {
            float nz[3] = {(float)gauss() * 0.05f * DEG, (float)gauss() * 0.05f * DEG, (float)gauss() * 0.05f * DEG}, e[4],
                  t2[4];
            swg_qexp(nz, e);
            qmul(e, q, t2);
            memcpy(q, t2, 16);
        }
        int tw = two || (two_from_f > 0 && f >= two_from_f);
        if (!swg_step(&s, &c, q, BLADE, f ? dt : 0.0111f, tw, rot)) memset(rot, 0, 12);
        float off = sqrtf(rot[0] * rot[0] + rot[1] * rot[1] + rot[2] * rot[2]) / DEG;
        float along = (rot[0] * g_bw[0] + rot[1] * g_bw[1] + rot[2] * g_bw[2]) / DEG, bl[3], hb[3];
        qrot(q, BLADE, hb);
        float tw_deg = fabsf(rot[0] * hb[0] + rot[1] * hb[1] + rot[2] * hb[2]) / DEG;
        if (tw_deg > r.twist) r.twist = tw_deg;
        (void)bl;
        float bang = th / DEG + along, hang = th / DEG;
        if (t < P->t0 && off > r.still) r.still = off;
        if (off > r.lag) r.lag = off;
        float d[3] = {rot[0] - prot[0], rot[1] - prot[1], rot[2] - prot[2]}, dd = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / DEG;
        if (f && dd > r.step_max) r.step_max = dd;
        memcpy(prot, rot, 12);
        if (f) {
            float wb = fabsf(bang - pb) * DEG / dt, wh = fabsf(hang - ph) * DEG / dt;
            if (wb > r.w_blade) r.w_blade = wb;
            if (wh > r.w_hand) r.w_hand = wh;
        }
        pb = bang, ph = hang;
        if (P->kind == P_CUT && t >= end) {
            float past = (P->amp_deg > 0 ? along : -along);
            if (past > r.run_on) r.run_on = past;
            if (off > 1.0f) last_big = t;
        }
        if (P->kind == P_WIGGLE && t >= P->t0 + 0.3f && t <= P->t0 + 0.9f) {
            bmin = fminf(bmin, bang), bmax = fmaxf(bmax, bang), hmin = fminf(hmin, hang), hmax = fmaxf(hmax, hang);
        }
    }
    r.settle_ms = (last_big - end) * 1000.0f + (last_big > end ? 1000.0f * dt : 0);
    if (P->kind == P_WIGGLE) r.amp_pct = 100.0f * (bmax - bmin) / (hmax - hmin);
    return r;
}

static void test_weight(void) {
    const char *nm[] = {"", "Light", "Heavy"};
    /* the swing axis: across the blade's start direction */
    float r0[3] = {0.4f, -0.3f, 0.2f}, bl[3], up[3] = {0, 1, 0};
    swg_qexp(r0, g_q0);
    qrot(g_q0, BLADE, bl);
    g_bw[0] = bl[1] * up[2] - bl[2] * up[1], g_bw[1] = bl[2] * up[0] - bl[0] * up[2], g_bw[2] = bl[0] * up[1] - bl[1] * up[0];
    float n = sqrtf(g_bw[0] * g_bw[0] + g_bw[1] * g_bw[1] + g_bw[2] * g_bw[2]);
    for (int i = 0; i < 3; i++) g_bw[i] /= n;
    const prof slow = {P_CUT, 60, 0.8f, 0.1f}, med = {P_CUT, 90, 0.35f, 0.1f}, fast = {P_CUT, 120, 0.25f, 0.1f},
               flick = {P_CUT, -90, 0.15f, 0.1f}, w5 = {P_WIGGLE, 10, 0.2f, 0.1f}, w4 = {P_WIGGLE, 15, 0.25f, 0.1f},
               w3 = {P_WIGGLE, 20, 0.3333f, 0.1f}, w2 = {P_WIGGLE, 25, 0.5f, 0.1f}, roll = {P_ROLL, 90, 0.2f, 0.1f};
    wres R[3][2][9];
    const prof *ps[9] = {&slow, &med, &fast, &flick, &w5, &w4, &w3, &w2, &roll};
    const char *pn[9] = {"slow cut 60 deg/0.8 s", "cut 90 deg/0.35 s", "fast cut 120 deg/0.25 s", "flick 90 deg/0.15 s",
                         "wiggle +-10 deg 5 Hz", "wiggle +-15 deg 4 Hz", "wiggle +-20 deg 3 Hz", "wiggle +-25 deg 2 Hz",
                         "roll 90 deg/0.2 s"};
    for (int lv = SWG_LIGHT; lv <= SWG_HEAVY; lv++)
        for (int two = 0; two < 2; two++) {
            printf("weight %s, %s:\n", nm[lv], two ? "two hands" : "one hand");
            for (int i = 0; i < 9; i++) {
                wres w = R[lv][two][i] = wrun(lv, two, ps[i], 90, 1, 0);
                if (ps[i]->kind == P_WIGGLE)
                    printf("  %-24s the blade %3.0f %% of the hand's swing, off by up to %4.1f deg\n", pn[i], w.amp_pct, w.lag);
                else
                    printf("  %-24s lag %4.1f deg, run-on %4.1f, settled (1 deg) %3.0f ms after the stop, speed %4.1f "
                           "(hand %4.1f) rad/s, twist off %.3f deg\n", pn[i], w.lag, w.run_on, w.settle_ms, w.w_blade,
                           w.w_hand, w.twist);
            }
        }
    swg_cfg H, L;
    swg_defaults(&H, SWG_HEAVY), swg_defaults(&L, SWG_LIGHT);
    wres *h = R[SWG_HEAVY][0], *l = R[SWG_LIGHT][0], *h2 = R[SWG_HEAVY][1], *l2 = R[SWG_LIGHT][1];
    /* heavy, one hand: slow 1:1, a clear lag in fast cuts that settles, quick wiggles absorbed */
    CHECK(h[0].lag < 1.5f && h[0].settle_ms <= 50, "heavy: a slow cut 1:1 (%.2f deg, %.0f ms)", h[0].lag, h[0].settle_ms);
    CHECK(h[2].lag >= 10 && h[2].lag <= 20 && h[3].lag >= 10 && h[3].lag <= H.rope / DEG + 0.01f,
          "heavy: fast cuts lag 10..20 deg (%.1f, %.1f)", h[2].lag, h[3].lag);
    CHECK(h[1].lag > 5 && h[1].lag < h[2].lag + 0.5f, "heavy: a medium cut lags less (%.1f < %.1f)", h[1].lag, h[2].lag);
    CHECK(h[2].settle_ms <= 250 && h[3].settle_ms <= 300, "heavy: settled %.0f / %.0f ms after the stop", h[2].settle_ms,
          h[3].settle_ms);
    CHECK(h[4].amp_pct <= 45 && h[5].amp_pct <= 60 && h[6].amp_pct <= 80, "heavy: wiggles absorbed (%.0f %%, %.0f %%, %.0f %%)",
          h[4].amp_pct, h[5].amp_pct, h[6].amp_pct);
    for (int i = 0; i < 9; i++) {
        CHECK(h[i].lag <= H.rope / DEG + 0.01f && l[i].lag <= L.rope / DEG + 0.01f, "%s: within the ropes (%.2f, %.2f)",
              pn[i], h[i].lag, l[i].lag);
        CHECK(R[SWG_HEAVY][0][i].w_blade <= H.w_max * 1.02f + 0.3f, "%s: the blade's speed cap (%.1f)", pn[i], h[i].w_blade);
        CHECK(h[i].still < 0.25f && l[i].still < 0.25f && h2[i].still < 0.25f,
              "%s: the tremor and noise before it: nothing added (%.3f deg)", pn[i], h[i].still);
        CHECK(h[i].twist < 0.01f && l[i].twist < 0.01f && h2[i].twist < 0.01f, "%s: no twist off (%.4f deg)", pn[i],
              h[i].twist);
        CHECK(h[i].run_on <= 2.0f && l[i].run_on <= 2.0f, "%s: no run-on past the hand (%.2f, %.2f deg)", pn[i], h[i].run_on,
              l[i].run_on);
        CHECK(h[i].step_max <= 8.0f, "%s: the offset changes at most 8 deg a frame (%.2f)", pn[i], h[i].step_max);
    }
    wres hr = wrun(SWG_HEAVY, 0, &roll, 90, 0, 0), lr = wrun(SWG_LIGHT, 0, &roll, 90, 0, 0);
    CHECK(hr.lag < 0.01f && lr.lag < 0.01f, "a 90 deg roll in 0.2 s (no noise): the blade's direction the hand's (%.4f, "
          "%.4f deg)", hr.lag, lr.lag);
    /* light: less than heavy, still felt in fast cuts */
    CHECK(l[0].lag < 1.5f && l[2].lag >= 4 && l[2].lag < h[2].lag && l[5].amp_pct > h[5].amp_pct && l[5].amp_pct <= 90,
          "light: slow %.1f, fast %.1f < %.1f deg, the 4 Hz wiggle %.0f %% (heavy %.0f %%)", l[0].lag, l[2].lag, h[2].lag,
          l[5].amp_pct, h[5].amp_pct);
    /* two hands: near 1:1 */
    for (int i = 0; i < 4; i++)
        CHECK(h2[i].lag <= 4.0f && l2[i].lag <= 2.5f, "two hands, %s: lag %.1f / %.1f deg", pn[i], h2[i].lag, l2[i].lag);
    for (int i = 4; i < 8; i++)
        CHECK(h2[i].amp_pct >= 90 && h2[i].amp_pct <= 112, "two hands, %s: %.0f %% of the hand's", pn[i], h2[i].amp_pct);
    /* 20 Hz (the headless runtime): the sub-steps give the same follow */
    wres f20 = wrun(SWG_HEAVY, 0, &fast, 20, 0, 0), f90 = wrun(SWG_HEAVY, 0, &fast, 90, 0, 0);
    printf("weight Heavy at 20 Hz: the fast cut lags %.1f deg (90 Hz %.1f), settled %.0f ms\n", f20.lag, f90.lag, f20.settle_ms);
    CHECK(fabsf(f20.lag - f90.lag) <= 3.0f, "20 Hz: %.1f vs %.1f deg", f20.lag, f90.lag);
    /* the left hand takes the hilt mid-cut: the lag closes smoothly (the rope closes at rope_close) */
    wres tk = wrun(SWG_HEAVY, 0, &fast, 90, 0, 9 + 13);
    printf("weight Heavy, the left hand on the hilt mid-cut: lag %.1f deg, the most in one frame %.2f deg (one hand "
           "%.2f), settled %.0f ms after the stop\n", tk.lag, tk.step_max, f90.step_max, tk.settle_ms);
    CHECK(tk.step_max <= f90.step_max + 0.5f && tk.settle_ms < f90.settle_ms,
          "two hands mid-cut: no jump (%.2f deg a frame, one hand %.2f), settled sooner (%.0f < %.0f ms)", tk.step_max,
          f90.step_max, tk.settle_ms, f90.settle_ms);
    /* a jump of the hand (40 deg in a frame): reset, no kick; a 20 Hz frame of 40 deg: no reset; the same time: kept */
    swg_state s;
    swg_reset(&s);
    float q[4], rot[3];
    hand_q(0, 0, q);
    swg_step(&s, &H, q, BLADE, 0.0111f, 0, rot);
    swg_step(&s, &H, q, BLADE, 0.0111f, 0, rot);
    hand_q(40 * DEG, 0, q);
    int rr = swg_step(&s, &H, q, BLADE, 0.0111f, 0, rot);
    CHECK(rr == 0 && rot[0] == 0 && rot[1] == 0 && rot[2] == 0 && s.resets == 1, "a jump resets");
    swg_reset(&s);
    hand_q(0, 0, q);
    swg_step(&s, &H, q, BLADE, 0.05f, 0, rot);
    hand_q(40 * DEG, 0, q);
    CHECK(swg_step(&s, &H, q, BLADE, 0.05f, 0, rot) == 1 && s.resets == 0, "20 Hz, 40 deg a frame: no reset");
    float keep[3];
    memcpy(keep, rot, 12);
    CHECK(swg_step(&s, &H, q, BLADE, 0, 0, rot) == 1 && !memcmp(keep, rot, 12), "dt 0: the same offset");
    float bad[4] = {NAN, 0, 0, 1};
    CHECK(swg_step(&s, &H, bad, BLADE, 0.0111f, 0, rot) == 0 && !s.have, "a NaN hand: none, reset");
}

/* SOMVR_SWORD_WEIGHT_HAND=follow: the drawn hand turned with the blade (swg_follow) - through a heavy fast cut, a flick
 * and a wiggle, one hand and two: the hand's turn off the controller = the blade's offset, the hand's blade axis = the
 * blade's direction, no twist about the blade (all within 0.1 deg; the position is not an input) */
static void test_follow(void) {
    const prof cuts[3] = {{P_CUT, 120, 0.25f, 0.1f}, {P_CUT, -90, 0.15f, 0.1f}, {P_WIGGLE, 15, 0.25f, 0.1f}};
    swg_cfg c;
    swg_defaults(&c, SWG_HEAVY);
    float worst_lag = 0, worst_axis = 0, worst_twist = 0, peak = 0;
    for (int two = 0; two < 2; two++)
        for (int k = 0; k < 3; k++) {
            swg_state s;
            swg_reset(&s);
            for (int f = 0; f <= 160; f++) {
                float t = (float)f / 90.0f, q[4], rot[3], oq[4];
                hand_q(prof_at(&cuts[k], t), 0, q);
                if (!swg_step(&s, &c, q, BLADE, f ? 1.0f / 90.0f : 0.0111f, two, rot)) memset(rot, 0, 12);
                swg_follow(rot, q, oq);
                float off = sqrtf(rot[0] * rot[0] + rot[1] * rot[1] + rot[2] * rot[2]);
                /* the hand's turn off the controller */
                float cq0[4] = {-q[0], -q[1], -q[2], q[3]}, rq[4];
                qmul(oq, cq0, rq); /* atan2, not acos: well conditioned near 0 */
                float turn = 2.0f * atan2f(sqrtf(rq[0] * rq[0] + rq[1] * rq[1] + rq[2] * rq[2]), fabsf(rq[3]));
                worst_lag = fmaxf(worst_lag, fabsf(turn - off) / DEG), peak = fmaxf(peak, off / DEG);
                /* its blade axis against the blade's direction (s.d) */
                float ha[3];
                qrot(oq, BLADE, ha);
                float cx[3] = {ha[1] * s.d[2] - ha[2] * s.d[1], ha[2] * s.d[0] - ha[0] * s.d[2], ha[0] * s.d[1] - ha[1] * s.d[0]};
                worst_axis = fmaxf(worst_axis, atan2f(sqrtf(cx[0] * cx[0] + cx[1] * cx[1] + cx[2] * cx[2]),
                                                      ha[0] * s.d[0] + ha[1] * s.d[1] + ha[2] * s.d[2]) / DEG);
                /* no twist: oq q^-1's axis lies across the controller's blade axis */
                float cq[4] = {-q[0], -q[1], -q[2], q[3]}, r[4], ca[3];
                qmul(oq, cq, r);
                qrot(q, BLADE, ca);
                float tw = 2.0f * fabsf(r[0] * ca[0] + r[1] * ca[1] + r[2] * ca[2]);
                worst_twist = fmaxf(worst_twist, tw / DEG);
            }
        }
    printf("follow: the hand's turn = the blade's offset within %.4f deg (offsets up to %.1f deg), its blade axis on the "
           "blade within %.4f deg, twist %.4f deg\n", worst_lag, peak, worst_axis, worst_twist);
    CHECK(peak > 10 && worst_lag < 0.1f && worst_axis < 0.1f && worst_twist < 0.1f,
          "follow: the hand turns with the blade (%.4f / %.4f / %.4f deg)", worst_lag, worst_axis, worst_twist);
    float q[4], z[3] = {0, 0, 0}, oq[4];
    hand_q(0.3f, 0.2f, q);
    swg_follow(z, q, oq);
    CHECK(fabsf(oq[0] - q[0]) + fabsf(oq[1] - q[1]) + fabsf(oq[2] - q[2]) + fabsf(oq[3] - q[3]) < 1e-6f,
          "follow: no offset, the controller's orientation");
}

/* SWG_REPLAY=FILE (SOMVR_SWORD_REC's <log>.swordrec): every level on that recorded motion - the blade's angle off the hand
 * (p50 / p95 / max), while the hand swings (3+ rad/s), at the rope; the recorded level's offsets against the replay's */
static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
static void replay(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("replay: %s not opened\n", path);
        return;
    }
    int cap = 1 << 16, n = 0;
    double *t = malloc(sizeof(double) * (size_t)cap);
    float(*q)[4] = malloc(sizeof(float[4]) * (size_t)cap), *ang = malloc(sizeof(float) * (size_t)cap),
          *off = malloc(sizeof(float) * (size_t)cap);
    int *two = malloc(sizeof(int) * (size_t)cap), *lvl = malloc(sizeof(int) * (size_t)cap);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (n == cap) {
            cap *= 2;
            t = realloc(t, sizeof(double) * (size_t)cap), q = realloc(q, sizeof(float[4]) * (size_t)cap);
            ang = realloc(ang, sizeof(float) * (size_t)cap), off = realloc(off, sizeof(float) * (size_t)cap);
            two = realloc(two, sizeof(int) * (size_t)cap), lvl = realloc(lvl, sizeof(int) * (size_t)cap);
        }
        if (sscanf(line, "%lf %f %f %f %f %d %d %f %f", &t[n], &q[n][0], &q[n][1], &q[n][2], &q[n][3], &two[n], &lvl[n],
                   &ang[n], &off[n]) == 9)
            n++;
    }
    fclose(f);
    printf("replay %s: %d frames over %.1f s\n", path, n, n ? t[n - 1] - t[0] : 0.0);
    float *o = malloc(sizeof(float) * (size_t)(n ? n : 1)), *os = malloc(sizeof(float) * (size_t)(n ? n : 1));
    for (int lv = SWG_LIGHT; lv <= SWG_HEAVY; lv++) {
        swg_cfg c;
        swg_defaults(&c, lv);
        swg_state s;
        swg_reset(&s);
        int k = 0, ks = 0, same = 0;
        float diff = 0, hmax = 0;
        uint64_t roped = 0;
        for (int i = 0; i < n; i++) {
            float a = ang[i] * DEG, axis[3] = {0, sinf(a), cosf(a)}, rot[3];
            float dt = i ? (float)(t[i] - t[i - 1]) : 0.0111f;
            uint64_t r0 = s.roped;
            int ok = swg_step(&s, &c, q[i], axis, dt, two[i], rot);
            roped += s.roped - r0;
            float od = ok ? s.off / DEG : 0, wh = sqrtf(s.wh[0] * s.wh[0] + s.wh[1] * s.wh[1] + s.wh[2] * s.wh[2]);
            o[k++] = od;
            if (ok && wh >= 3.0f) os[ks++] = od;
            if (ok && wh > hmax) hmax = wh;
            if (lvl[i] == lv) same++, diff = fmaxf(diff, fabsf(od - off[i]));
        }
        qsort(o, (size_t)k, sizeof *o, cmpf), qsort(os, (size_t)ks, sizeof *os, cmpf);
        printf("  %s: the blade off the hand p50 %.2f p95 %.2f max %.2f deg; while the hand swings (3+ rad/s, %d frames, "
               "the hand up to %.1f rad/s) p50 %.2f p95 %.2f; at the rope %llu sub-steps",
               lv == SWG_HEAVY ? "heavy" : "light", k ? o[k / 2] : 0, k ? o[k * 95 / 100] : 0, k ? o[k - 1] : 0, ks, hmax,
               ks ? os[ks / 2] : 0, ks ? os[ks * 95 / 100] : 0, (unsigned long long)roped);
        if (same) printf("; the recorded %s frames (%d) off the replay by up to %.3f deg", lv == SWG_HEAVY ? "heavy" : "light", same, diff);
        printf("\n");
    }
    free(t), free(q), free(ang), free(off), free(two), free(lvl), free(o), free(os);
}

int main(void) {
    /* the grip 30 cm ahead, 20 cm down, turned and tilted; the swing about a tilted axis */
    float r0[3] = {0.4f, -0.3f, 0.2f};
    swg_qexp(r0, T0.q0);
    T0.p0[0] = 0.15f, T0.p0[1] = -0.2f, T0.p0[2] = -0.3f;
    float n = sqrtf(0.2f * 0.2f + 1.0f + 0.1f * 0.1f);
    T0.ax[0] = 0.2f / n, T0.ax[1] = 1.0f / n, T0.ax[2] = 0.1f / n;
    test_still();
    test_swing();
    test_resets();
    test_weight();
    test_follow();
    const char *rp = getenv("SWG_REPLAY");
    if (rp && rp[0]) replay(rp);
    printf("test_smooth: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
