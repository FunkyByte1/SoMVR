/* test_headview.c - unit tests of somvr_headview.c: the eye offset in Talion's body frame, the clamp ahead of the Head
 * node and its ease, the yaw ease, the SOMVR_TUNE parser (docs/re-xr-pipeline.md 25) */
#include "../somvr_headview.h"

#include <math.h>
#include <stdio.h>
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
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define DEG 0.01745329252f

/* run 10's tower top: Talion faces -60 deg, the anchored eye at root + 76.6, the Head node 3.2 cm ahead of it */
static const float YAW = -60 * DEG;
static sv_vec3 fwd(float y) { return (sv_vec3){ sinf(y), 0, cosf(y) }; }
static sv_vec3 along(sv_vec3 b, float y, float d, float up)
{
    sv_vec3 f = fwd(y);
    return (sv_vec3){ b.x + d * f.x, b.y + up, b.z + d * f.z };
}

static void test_offset(void)
{
    somvr_hv_cfg c;
    somvr_hv_cfg_default(&c);
    CHECK(c.fwd_cm == 16 && c.up_cm == 0 && c.min_cm == 10, "defaults %g %g %g", c.fwd_cm, c.up_cm, c.min_cm);
    somvr_hv s;
    somvr_hv_init(&s);
    sv_vec3 base = { 35400, 5478.7f, -22400 };
    somvr_hv_in in = { 1.0, base, YAW, 1, along(base, YAW, 3.2f, -4.6f) };
    somvr_hv_out o;
    somvr_hv_step(&s, &c, &in, &o);
    sv_vec3 w = along(base, YAW, 16, 0);
    CHECK(o.latched && NEAR(o.pos.x, w.x, 1e-3) && NEAR(o.pos.y, w.y, 1e-3) && NEAR(o.pos.z, w.z, 1e-3),
          "the offset %g %g %g, want %g %g %g", o.pos.x, o.pos.y, o.pos.z, w.x, w.y, w.z);
    CHECK(NEAR(o.ahead_cm, 12.8, 1e-3) && o.target_cm == 0 && o.push_cm == 0, "ahead %g target %g push %g", o.ahead_cm,
          o.target_cm, o.push_cm);
    /* the forward is the body's yaw, not the head's pitch: up and the head node's height change nothing ahead */
    c.up_cm = 5;
    in.t += 0.011, in.head.y -= 30;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(NEAR(o.pos.y, base.y + 5, 1e-3) && NEAR(o.ahead_cm, 12.8, 1e-3) && !o.latched, "up %g ahead %g", o.pos.y - base.y,
          o.ahead_cm);
    /* idle sway of the head (+-1 cm around 3.2 ahead of the root's eye): no clamp, the view does not move */
    c.up_cm = 0;
    for (int i = 0; i < 200; i++) {
        in.t += 0.011;
        in.head = along(base, YAW, 3.2f + sinf(i * 0.1f), -4.6f);
        somvr_hv_step(&s, &c, &in, &o);
        CHECK(o.push_cm == 0 && NEAR(o.pos.x, w.x, 1e-3) && NEAR(o.pos.z, w.z, 1e-3), "idle frame %d push %g", i, o.push_cm);
    }
    /* SOMVR_EYE_FWD_CM=0 + no clamp: the base as it was (bit for bit) */
    c.fwd_cm = 0, c.min_cm = 0;
    in.t += 0.011;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(o.pos.x == base.x && o.pos.y == base.y && o.pos.z == base.z, "fwd 0 moved %g %g %g", o.pos.x - base.x,
          o.pos.y - base.y, o.pos.z - base.z);
}

static void test_clamp(void)
{
    somvr_hv_cfg c;
    somvr_hv_cfg_default(&c);
    somvr_hv s;
    somvr_hv_init(&s);
    sv_vec3 base = { 0, 180, 0 };
    somvr_hv_in in = { 5.0, base, 0, 1, along(base, 0, 3, -5) };
    somvr_hv_out o;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(o.push_cm == 0, "start push %g", o.push_cm);
    /* a lean: the head 17 cm further ahead over 0.2 s (combat stance): the eye pushed 14 cm, 10 cm ahead of it */
    float last = o.pos.z, vmax = 0, amax = 0, vlast = 0;
    for (int i = 1; i <= 120; i++) {
        float k = i < 20 ? i / 20.0f : 1;
        in.t += 0.011;
        in.head = along(base, 0, 3 + 17 * k, -5);
        somvr_hv_step(&s, &c, &in, &o);
        float v = (o.pos.z - last) / 0.011f;
        if (i > 1)
            vmax = fmaxf(vmax, fabsf(v)), amax = fmaxf(amax, fabsf(v - vlast) / 0.011f);
        CHECK(o.push_cm >= 0 && o.push_cm <= o.target_cm + 0.5f, "frame %d push %g target %g", i, o.push_cm, o.target_cm);
        last = o.pos.z, vlast = v;
    }
    CHECK(NEAR(o.target_cm, 14, 1e-3) && NEAR(o.push_cm, 14, 0.05) && NEAR(o.pos.z - in.head.z, 10, 0.05),
          "the clamp: target %g push %g ahead %g", o.target_cm, o.push_cm, o.pos.z - in.head.z);
    /* eased: no step; the speed stays within the lean's own (85 cm/s) plus the ease's catch-up */
    CHECK(vmax < 120 && amax < 6000, "the clamp's motion: v max %g cm/s, a max %g cm/s^2", vmax, amax);
    CHECK(s.clamped > 0 && s.x_max >= 13.9f, "counters %llu %g", (unsigned long long)s.clamped, s.x_max);
    /* back upright: the push eases out to exactly 0 (never below) */
    for (int i = 0; i < 120; i++) {
        in.t += 0.011;
        in.head = along(base, 0, 3, -5);
        somvr_hv_step(&s, &c, &in, &o);
        CHECK(o.push_cm >= 0, "back: push %g", o.push_cm);
    }
    CHECK(o.push_cm < 0.01f && NEAR(o.pos.z, 16, 0.01), "back: push %g z %g", o.push_cm, o.pos.z);
    /* no head this frame: no clamp target, the push eases out */
    in.has_head = 0, in.t += 0.011;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(o.target_cm == 0, "no head: target %g", o.target_cm);
    /* a gap (a load): re-latched, the push at its target at once */
    in.has_head = 1, in.head = along(base, 0, 30, 0), in.t += 1.0;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(o.latched && NEAR(o.push_cm, 24, 1e-3) && NEAR(o.pos.z, 40, 1e-3), "gap: push %g z %g", o.push_cm, o.pos.z);
    /* the clock going back: re-latched too */
    in.t -= 0.5;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(o.latched, "clock back not latched");
    /* NaN in: no NaN out */
    in.t += 0.6, in.head.x = NAN, in.body_yaw = NAN;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(isfinite(o.pos.x) && isfinite(o.pos.z) && isfinite(o.push_cm), "NaN: %g %g %g", o.pos.x, o.pos.z, o.push_cm);
}

static void test_yaw_ease(void)
{
    somvr_hv_cfg c;
    somvr_hv_cfg_default(&c);
    c.min_cm = 0;
    somvr_hv s;
    somvr_hv_init(&s);
    somvr_hv_in in = { 0, { 0, 0, 0 }, 170 * DEG, 0, { 0, 0, 0 } };
    somvr_hv_out o;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(NEAR(o.yaw, 170 * DEG, 1e-5), "latched yaw %g", o.yaw / DEG);
    /* a standing turn across +-180 (720 deg/s for 40 deg): the forward eases the short way, no jump */
    float prev = o.yaw, maxstep = 0;
    for (int i = 1; i <= 60; i++) {
        in.t += 0.011;
        float k = i < 5 ? i / 5.0f : 1;
        in.body_yaw = 170 * DEG + 40 * DEG * k;
        if (in.body_yaw > 3.14159265f)
            in.body_yaw -= 6.28318531f;
        somvr_hv_step(&s, &c, &in, &o);
        float d = o.yaw - prev;
        d = d > 3.14159265f ? d - 6.28318531f : d < -3.14159265f ? d + 6.28318531f : d;
        maxstep = fmaxf(maxstep, fabsf(d));
        prev = o.yaw;
    }
    CHECK(NEAR(o.yaw, -150 * DEG, 0.2 * DEG) && maxstep < 6 * DEG, "the turn: yaw %g, the largest frame step %g deg",
          o.yaw / DEG, maxstep / DEG);
    sv_vec3 f = fwd(o.yaw);
    CHECK(NEAR(o.pos.x, 16 * f.x, 1e-2) && NEAR(o.pos.z, 16 * f.z, 1e-2), "the offset along the eased yaw");
    /* yaw_s 0: the body's yaw as it is */
    c.yaw_s = 0, in.t += 0.011, in.body_yaw = 10 * DEG;
    somvr_hv_step(&s, &c, &in, &o);
    CHECK(NEAR(o.yaw, 10 * DEG, 1e-6), "yaw_s 0: %g", o.yaw / DEG);
}

static void test_tune(void)
{
    somvr_tune t;
    somvr_tune_parse("eye_fwd_cm=12\n# a comment\n\n  near_cm = 6.5 \r\neye_up_cm=-2\neye_min_cm=.5\n", &t);
    CHECK(t.set == 0xf && t.v[SOMVR_TUNE_EYE_FWD] == 12 && t.v[SOMVR_TUNE_NEAR] == 6.5f && t.v[SOMVR_TUNE_EYE_UP] == -2 &&
              t.v[SOMVR_TUNE_EYE_MIN] == 0.5f && !t.bad && !t.unknown,
          "parse: set %#x %g %g %g %g bad %d unknown %d", t.set, t.v[0], t.v[1], t.v[2], t.v[3], t.bad, t.unknown);
    somvr_tune_parse("eye_fwd_cm=10\neye_fwd_cm=14\n", &t);
    CHECK(t.set == 1 && t.v[0] == 14, "the last line wins: %g", t.v[0]);
    somvr_tune_parse("eye_fwd_cm=1e3\nnear_cm=0\nnear_cm=abc\neye_up_cm=1,5\nfoo=1\neye_fwd_cm\neye_fwd_cm=99\n", &t);
    CHECK(t.set == 0 && t.bad == 6 && t.unknown == 1 && !strcmp(t.first_bad, "eye_fwd_cm=1e3"),
          "bad lines: set %#x bad %d unknown %d first '%s'", t.set, t.bad, t.unknown, t.first_bad);
    somvr_tune_parse("eye_fwd_cm=8\nnear_cm=0.5\n", &t);
    CHECK(t.set == 1 && t.v[0] == 8 && t.bad == 1, "a bad line keeps the good ones: %#x %d", t.set, t.bad);
    somvr_tune_parse("", &t);
    CHECK(t.set == 0 && !t.bad && !t.unknown, "empty");
    somvr_tune_parse("eye_fwd_cm=-30\neye_up_cm=+30", &t); /* the ends, no final newline */
    CHECK(t.set == 3 && t.v[0] == -30 && t.v[1] == 30, "ends: %#x %g %g", t.set, t.v[0], t.v[1]);
    char longl[200];
    memset(longl, 'x', sizeof longl - 1);
    longl[sizeof longl - 1] = 0;
    somvr_tune_parse(longl, &t);
    CHECK(t.unknown == 1 && strlen(t.first_bad) == sizeof t.first_bad - 1, "a long line cut: %zu", strlen(t.first_bad));
}

int main(void)
{
    test_offset();
    test_clamp();
    test_yaw_ease();
    test_tune();
    printf("test_headview: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
