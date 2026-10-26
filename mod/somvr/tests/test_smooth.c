/* test_smooth.c - the hands' smoothing and the sword's weight (somvr_smooth.c; docs/re-xr-pipeline.md 48): a still
 * hand with tracking noise and a tremor (synthetic, at 90 Hz: the user's session logs one HAND line a second, too
 * sparse to replay), the noise cut at the hand and at a 1 m blade's tip; swings at 3, 6, 10 rad/s and a 1 m/s move:
 * the delay; the onset from rest; without the runtime's velocities; resets (a jump, a gap, untracked), the same time
 * again, measuring without filtering; the weight: nothing still or slow, a trail in a fast swing within its cap and a
 * settle after, stiffer with two hands, Heavy > Light, a jump resets. */
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

/* ---- the weight ---------------------------------------------------------------------------------------------------- */
/* a swing: rest 20 frames, accelerate to w_max over acc frames, hold, decelerate, rest; the hand's pose in metres */
typedef struct {
    float peak_trail, peak_lead; /* deg: the offset against / along the swing's rotation */
    float peak_pos;              /* m */
    float settle_ms;             /* after the stop, until the offset stays under 0.05 deg */
    float still_max;             /* deg: the offset while resting before the swing */
    int sign_changes;            /* after the stop (along the axis) */
} wrun;
static wrun weight_run(int level, int two, float w_max, int acc, float tremor_deg) {
    swg_cfg c;
    swg_defaults(&c, level, 1.0f);
    swg_state s;
    swg_reset(&s);
    wrun r = {0, 0, 0, 0, 0, 0};
    float ang = 0, wv = 0, prev_along = 0;
    int stop_f = 20 + acc + 15 + acc, last_big = stop_f;
    for (int f = 0; f < 140; f++) {
        if (f >= 20 && f < 20 + acc) wv = w_max * (float)(f - 20 + 1) / (float)acc;
        else if (f >= 20 + acc && f < 20 + acc + 15) wv = w_max;
        else if (f >= 20 + acc + 15 && f < stop_f) wv = w_max * (float)(stop_f - f - 1) / (float)acc;
        else wv = 0;
        ang += wv * (float)(DT_NS * 1e-9);
        float tr = tremor_deg * DEG * sinf(6.283185f * 9.0f * (float)(f * DT_NS * 1e-9));
        float q[4], p[3], rot[3], pos[3];
        truth_at(&T0, ang + tr, 0.25f * ang, q, p); /* the hand also moves ~0.25 m per rad (a swing's arc) */
        swg_step(&s, &c, q, p, (float)(DT_NS * 1e-9), two, rot, pos);
        float along = rot[0] * T0.ax[0] + rot[1] * T0.ax[1] + rot[2] * T0.ax[2];
        float mag = sqrtf(rot[0] * rot[0] + rot[1] * rot[1] + rot[2] * rot[2]) / DEG;
        float pm = sqrtf(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);
        if (f < 20 && mag > r.still_max) r.still_max = mag;
        if (-along / DEG > r.peak_trail) r.peak_trail = -along / DEG;
        if (along / DEG > r.peak_lead) r.peak_lead = along / DEG;
        if (pm > r.peak_pos) r.peak_pos = pm;
        if (f >= stop_f) {
            if (mag >= 0.05f) last_big = f;
            if ((along > 1e-5f && prev_along < -1e-5f) || (along < -1e-5f && prev_along > 1e-5f)) r.sign_changes++;
            if (fabsf(along) > 1e-5f) prev_along = along;
        }
    }
    r.settle_ms = (float)((last_big + 1 - stop_f) * DT_NS * 1e-6);
    return r;
}

static void test_weight(void) {
    const char *nm[] = {"", "Light", "Heavy"};
    for (int lv = SWG_LIGHT; lv <= SWG_HEAVY; lv++) {
        swg_cfg c;
        swg_defaults(&c, lv, 1.0f);
        float cap = c.cap_rad / DEG;
        /* a still hand with a tremor and a slow turn: nothing */
        wrun a = weight_run(lv, 0, 1.0f, 10, 0.3f);
        CHECK(a.still_max == 0 && a.peak_trail == 0 && a.peak_lead == 0 && a.peak_pos == 0,
              "%s: a tremor and a 1 rad/s turn move nothing (%.4f/%.4f deg)", nm[lv], a.peak_trail, a.peak_lead);
        /* a fast swing: 8 rad/s reached in 6 frames (67 ms) */
        wrun b = weight_run(lv, 0, 8.0f, 6, 0.3f);
        printf("weight %s (omega %.0f/s, cap %.1f deg, %.1f cm): an 8 rad/s swing trails %.2f deg, leads %.2f deg at the "
               "stop, settles in %.0f ms, the hilt off by at most %.2f cm; still %.3f deg\n",
               nm[lv], c.omega, cap, c.cap_pos * 100, b.peak_trail, b.peak_lead, b.settle_ms, b.peak_pos * 100,
               b.still_max);
        CHECK(b.still_max == 0, "%s: still before the swing", nm[lv]);
        CHECK(b.peak_trail > 1.0f && b.peak_trail <= cap + 1e-3f, "%s: trail %.2f deg in (1, %.1f]", nm[lv],
              b.peak_trail, cap);
        CHECK(b.peak_lead > 0.5f && b.peak_lead <= cap + 1e-3f, "%s: the settle's lead %.2f deg", nm[lv], b.peak_lead);
        CHECK(b.settle_ms <= (lv == SWG_HEAVY ? 300.0f : 200.0f), "%s: settled in %.0f ms", nm[lv], b.settle_ms);
        CHECK(b.sign_changes <= 1, "%s: no ringing after the stop (%d sign changes)", nm[lv], b.sign_changes);
        CHECK(b.peak_pos <= c.cap_pos + 1e-6f, "%s: the hilt within %.2f cm (%.3f)", nm[lv], c.cap_pos * 100,
              b.peak_pos * 100);
        /* both hands on the hilt: stiffer */
        wrun t = weight_run(lv, 1, 8.0f, 6, 0.3f);
        CHECK(t.peak_trail < b.peak_trail && t.peak_trail <= cap / c.two_k + 1e-3f,
              "%s two hands: trail %.2f < %.2f and within %.2f", nm[lv], t.peak_trail, b.peak_trail, cap / c.two_k);
        CHECK(t.settle_ms <= b.settle_ms, "%s two hands settle %.0f <= %.0f ms", nm[lv], t.settle_ms, b.settle_ms);
    }
    wrun l = weight_run(SWG_LIGHT, 0, 5.0f, 8, 0), h = weight_run(SWG_HEAVY, 0, 5.0f, 8, 0);
    CHECK(h.peak_trail > l.peak_trail, "Heavy %.2f > Light %.2f deg", h.peak_trail, l.peak_trail);
    /* a jump of the hand (40 deg in a frame): reset, no kick */
    swg_cfg c;
    swg_defaults(&c, SWG_LIGHT, 1.0f);
    swg_state s;
    swg_reset(&s);
    float q[4], p[3], rot[3], pos[3];
    truth_at(&T0, 0, 0, q, p);
    swg_step(&s, &c, q, p, 0.0111f, 0, rot, pos);
    truth_at(&T0, 40 * DEG, 0, q, p);
    int r = swg_step(&s, &c, q, p, 0.0111f, 0, rot, pos);
    CHECK(r == 0 && rot[0] == 0 && rot[1] == 0 && rot[2] == 0 && s.resets == 1, "a jump resets");
    /* a 20 Hz frame of a fast swing (40 deg in 50 ms = 14 rad/s): no reset */
    swg_reset(&s);
    truth_at(&T0, 0, 0, q, p);
    swg_step(&s, &c, q, p, 0.05f, 0, rot, pos);
    truth_at(&T0, 40 * DEG, 0, q, p);
    CHECK(swg_step(&s, &c, q, p, 0.05f, 0, rot, pos) == 1 && s.resets == 0, "20 Hz, 40 deg a frame: no reset");
    /* the same time: the same offsets */
    swg_step(&s, &c, q, p, 0.0111f, 0, rot, pos);
    CHECK(swg_step(&s, &c, q, p, 0, 0, rot, pos) == 1, "dt 0: kept");
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
    printf("test_smooth: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
