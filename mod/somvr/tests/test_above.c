/* test_above.c - unit tests of somvr_above.c, build 25's kills and brands from above (docs/re-xr-pipeline.md 102): the
 * plunge (a stab down at an orc below; too slow, sideways, a cut, upward, aimed away, a push), the brand hand's aim (out
 * on the line to his head; at the face, beside it), the blink (the press, the black, the game's start, the landing's seat
 * once, back in, held to the kill's end; refused: back in without a seat; no landing: the seat at the black's limit; the
 * ledge kill's held view without a fade) at 90 and 20 Hz. */
#include "../somvr_above.h"

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

/* the wall top's player: the eyes at 0,170,0 facing +z, an orc 300 cm below and 150 cm out (his chest at 0,-160,150) */
static void test_plunge(void)
{
    somvr_fa_cfg c;
    somvr_fa_cfg_default(&c);
    somvr_fa_plunge o;
    float tip[3] = { 20, 60, 70 }, chest[3] = { 0, -160, 150 };
    float to[3] = { chest[0] - tip[0], chest[1] - tip[1], chest[2] - tip[2] };
    /* the blade down and out at him (63 deg under the horizontal), the hand down along it at 3 m/s */
    float ax[3] = { 0, -0.89f, 0.45f }, v[3] = { 0, -267, 135 };
    CHECK(somvr_fa_plunge_judge(&c, v, ax, to, &o), "a plunge at him: %s (speed %.0f down %.0f point %.0f aim %.0f)", o.why,
          o.speed, o.down, o.point_deg, o.aim_deg);
    /* straight down, the blade plumb: 23 deg off the line to him, still his */
    float axd[3] = { 0, -1, 0 }, vd[3] = { 0, -250, 0 };
    CHECK(somvr_fa_plunge_judge(&c, vd, axd, to, &o), "straight down: %s (aim %.0f)", o.why, o.aim_deg);
    /* the same way at 1.2 m/s: a lean, not a plunge */
    float vs[3] = { 0, -107, 54 };
    CHECK(!somvr_fa_plunge_judge(&c, vs, ax, to, &o) && !strcmp(o.why, "too slow"), "slow: %s", o.why);
    /* boot 20261029-220827's lowered sword (the plunge's way over 0.55 s: its peak 1.57 m/s, 1.02 down, the point ~20 deg
     * off the blade) - taken for a plunge at 1.5 / 1.0 */
    float vb[3] = { 0, -102, 119 };
    CHECK(!somvr_fa_plunge_judge(&c, vb, ax, to, &o) && !strcmp(o.why, "too slow"), "the lowered sword: %s (%.0f cm/s)",
          o.why, o.speed);
    /* a cut across (the hand sideways, the blade down): not point first */
    float vc[3] = { 250, -160, 0 };
    CHECK(!somvr_fa_plunge_judge(&c, vc, ax, to, &o) && !strcmp(o.why, "not point first"), "a cut: %s (point %.0f)", o.why,
          o.point_deg);
    /* the blade drawn up (the hand up) */
    float vu[3] = { 0, 250, -120 };
    CHECK(!somvr_fa_plunge_judge(&c, vu, ax, to, &o) && !strcmp(o.why, "not down"), "up: %s", o.why);
    /* a level thrust ahead over his head (the blade level, 2.5 m/s, a little down): not down enough */
    float axl[3] = { 0, 0, 1 }, vl[3] = { 0, -40, 250 };
    CHECK(!somvr_fa_plunge_judge(&c, vl, axl, to, &o) && !strcmp(o.why, "not down"), "level: %s", o.why);
    /* a plunge down behind him (the blade 70 deg off the line to him) */
    float axa[3] = { 0, -0.5f, -0.87f }, va[3] = { 0, -150, -260 };
    CHECK(!somvr_fa_plunge_judge(&c, va, axa, to, &o) && !strcmp(o.why, "not aimed at him"), "away: %s (aim %.0f)", o.why,
          o.aim_deg);
    /* nothing moving: no plunge, no NaN */
    float v0[3] = { 0, 0, 0 };
    CHECK(!somvr_fa_plunge_judge(&c, v0, ax, to, &o) && o.point_deg == 180.0f, "still: %s", o.why);
}

/* the mean over the newest 80-160 ms: a cosine-eased way of L cm in T s sampled at hz, from rest; the largest mean speed
 * any frame sees (and the largest single-frame speed, the old measure) */
static void sweep(double hz, double L, double T, float *best_mean, float *best_frame)
{
    double t[64];
    float p[64][3];
    int n = 0;
    *best_mean = *best_frame = 0;
    for (int i = 0; i < (int)((T + 0.4) * hz) && i < 400; i++) {
        double tt = i / hz, u = tt < 0.1 ? 0 : tt > 0.1 + T ? 1 : (tt - 0.1) / T;
        float y = (float)(-L * 0.5 * (1 - cos(3.14159265 * u)));
        if (n == 64) {
            memmove(t, t + 1, sizeof t[0] * 63), memmove(p, p + 1, sizeof p[0] * 63), n = 63;
        }
        t[n] = tt, p[n][0] = 0, p[n][1] = y, p[n][2] = 0, n++;
        float v[3];
        if (somvr_fa_mean_vel(t, (const float(*)[3])p, n, 0.08, 0.16, v) && fabsf(v[1]) > *best_mean)
            *best_mean = fabsf(v[1]);
        if (n >= 2) {
            float f = (float)fabs((p[n - 1][1] - p[n - 2][1]) / (t[n - 1] - t[n - 2]));
            if (f > *best_frame) *best_frame = f;
        }
    }
}

static void test_span(void)
{
    float v[3], bm, bf;
    double t[4] = { 0, 0.05, 0.10, 0.15 };
    float p[4][3] = { { 0, 0, 0 }, { 0, -10, 0 }, { 0, -20, 0 }, { 0, -30, 0 } };
    CHECK(somvr_fa_mean_vel(t, (const float(*)[3])p, 4, 0.08, 0.16, v) && fabsf(v[1] + 200) < 0.5f, "2 m/s steady: %.1f", v[1]);
    CHECK(!somvr_fa_mean_vel(t, (const float(*)[3])p, 2, 0.08, 0.16, v), "50 ms of samples: no span yet");
    static const double hzs[] = { 20, 90 };
    for (int k = 0; k < 2; k++) {
        sweep(hzs[k], 57, 0.18, &bm, &bf); /* the tests' plunge: 57 cm in 180 ms */
        CHECK(bm >= 200, "%g Hz: the plunge's mean %.0f cm/s >= 2 m/s", hzs[k], bm);
        sweep(hzs[k], 57, 1.1, &bm, &bf); /* a sword lowered over 1.1 s */
        CHECK(bm < 140, "%g Hz: a lowered sword's mean %.0f cm/s < 1.4 m/s down", hzs[k], bm);
    }
    /* one tracking glitch (a sample 6 cm off) in a still hand at 90 Hz: one frame reads 5 m/s, the span not 2 */
    double tg[16];
    float pg[16][3];
    for (int i = 0; i < 16; i++) tg[i] = i / 90.0, pg[i][0] = pg[i][1] = pg[i][2] = 0;
    pg[15][1] = -6;
    CHECK(somvr_fa_mean_vel(tg, (const float(*)[3])pg, 16, 0.08, 0.16, v) && fabsf(v[1]) < 140,
          "a 6 cm glitch: the span's %.0f cm/s (one frame's %.0f)", v[1], 6 * 90.0);
}

static void test_aim(void)
{
    somvr_fa_cfg c;
    somvr_fa_cfg_default(&c);
    float eye[3] = { 0, 170, 0 }, head[3] = { 0, -130, 150 }, deg = 0;
    /* the arm out down at his head: the palm 55 cm out on the line */
    float d[3] = { head[0] - eye[0], head[1] - eye[1], head[2] - eye[2] }, l = sqrtf(d[1] * d[1] + d[2] * d[2]);
    float palm[3] = { eye[0] + d[0] / l * 55, eye[1] + d[1] / l * 55, eye[2] + d[2] / l * 55 };
    CHECK(somvr_fa_aimed(&c, eye, palm, head, &deg) && deg < 1, "aimed (%.1f deg)", deg);
    /* the same direction 20 cm out: a hand at the face */
    float near[3] = { eye[0] + d[0] / l * 20, eye[1] + d[1] / l * 20, eye[2] + d[2] / l * 20 };
    CHECK(!somvr_fa_aimed(&c, eye, near, head, &deg), "at the face");
    /* out ahead level: 63 deg off */
    float lev[3] = { 0, 170, 55 };
    CHECK(!somvr_fa_aimed(&c, eye, lev, head, &deg) && deg > 60, "level (%.1f deg)", deg);
    /* 30 deg to the side of him */
    float sd[3] = { 25, 130, 32 };
    CHECK(!somvr_fa_aimed(&c, eye, sd, head, &deg) && deg > 20, "beside (%.1f deg)", deg);
}

/* runs a press at hz: the game starts its kill at t_start (-1 never), Talion lands at t_land (-1 never), it ends at
 * t_over; the frames' seats, the first seat's time, the black's first and last frames, the hold's last frame */
typedef struct { int seats, refused, idle; double seat_t, black0, black1, hold1, end_t; float max_fade, fade_at_seat; } run_r;
static run_r run(double hz, double t_start, double t_land, double t_over, int black)
{
    somvr_fa_cfg c;
    somvr_fa_cfg_default(&c);
    somvr_fa_blink b;
    somvr_fa_blink_out o;
    run_r r;
    memset(&r, 0, sizeof r);
    r.seat_t = r.black0 = r.black1 = r.hold1 = r.end_t = -1;
    somvr_fa_blink_press(&b, 10.0, black);
    for (int i = 1; i < (int)(hz * 8); i++) {
        double t = 10.0 + i / hz, s = t - 10.0;
        int started = t_start >= 0 && s >= t_start, landed = t_land >= 0 && s >= t_land, over = t_over >= 0 && s >= t_over;
        somvr_fa_blink_step(&b, &c, t, started, landed, over, &o);
        if (o.seat) {
            r.seats++;
            if (r.seat_t < 0) r.seat_t = s, r.fade_at_seat = o.fade;
        }
        if (o.fade >= 1) {
            if (r.black0 < 0) r.black0 = s;
            r.black1 = s;
        }
        if (o.fade > r.max_fade) r.max_fade = o.fade;
        if (o.hold) r.hold1 = s;
        if (o.done && r.end_t < 0) r.end_t = s;
    }
    r.refused = b.refused, r.idle = b.phase == SOMVR_FAB_IDLE;
    return r;
}

static void test_blink(void)
{
    static const double hzs[] = { 90, 20 };
    for (int k = 0; k < 2; k++) {
        double hz = hzs[k], fr = 1.0 / hz;
        /* the drop: the kill starts 0.1 s after the press, Talion lands at 1.0 s, the kill ends at 3.0 s */
        run_r r = run(hz, 0.1, 1.0, 3.0, 1);
        CHECK(r.seats == 1 && r.seat_t >= 1.0 - 1e-9 && r.seat_t < 1.0 + 2 * fr, "%g Hz: one seat at the landing (%d at %.3f)",
              hz, r.seats, r.seat_t);
        CHECK(r.fade_at_seat >= 1, "%g Hz: the seat's frame is black (%.2f)", hz, r.fade_at_seat);
        CHECK(r.black0 >= 0.1 + 0.12 - 1e-9 && r.black0 < 0.1 + 0.12 + 3 * fr, "%g Hz: black after the start and the fade out "
              "(%.3f)", hz, r.black0);
        CHECK(r.black1 >= r.seat_t - 1e-9 && r.black1 < r.seat_t + fr + 1e-9, "%g Hz: back in from the seat (black to %.3f)",
              hz, r.black1);
        CHECK(r.hold1 >= 3.0 - 1e-9 - fr && r.hold1 < 3.0 + 2 * fr && r.end_t >= 3.0 - 1e-9 && !r.refused,
              "%g Hz: held on the ground to the kill's end (hold to %.3f, idle at %.3f)", hz, r.hold1, r.end_t);
        /* refused: nothing started - back in at 0.6 s, no seat, no hold after */
        r = run(hz, -1, -1, -1, 1);
        CHECK(r.refused && r.seats == 0 && r.idle && r.max_fade == 0 && r.end_t <= 0.6 + 2 * fr && r.hold1 <= 0.6 + fr + 1e-9,
              "%g Hz: refused - no black at all, no seat (fade %.2f, end %.3f, hold to %.3f)", hz, r.max_fade, r.end_t, r.hold1);
        /* no landing seen: the seat at the black's limit (2.5 s of black) */
        r = run(hz, 0.1, -1, 6.0, 1);
        CHECK(r.seats == 1 && r.seat_t >= 0.1 + 0.12 + 2.5 - 1e-9 && r.seat_t < 0.1 + 0.12 + 2.5 + 4 * fr, "%g Hz: the black's "
              "limit (%.3f)", hz, r.seat_t);
        /* the kill over before the landing was seen: seated at once, back in, idle (no hold left) */
        r = run(hz, 0.1, -1, 0.5, 1);
        CHECK(r.seats == 1 && r.seat_t < 0.5 + 2 * fr && r.idle && r.end_t < 0.5 + 0.2 + 3 * fr, "%g Hz: over in the black (%.3f, "
              "idle at %.3f)", hz, r.seat_t, r.end_t);
        /* the ledge kill: no fade, the view held from the press to the kill's end */
        r = run(hz, 0.1, -1, 2.0, 0);
        CHECK(r.max_fade == 0 && r.seats == 0 && r.hold1 >= 2.0 - 1e-9 - fr && r.end_t < 2.0 + 2 * fr, "%g Hz: the ledge kill "
              "held, no black (fade %.2f, hold to %.3f)", hz, r.max_fade, r.hold1);
        r = run(hz, -1, -1, -1, 0);
        CHECK(r.refused && r.max_fade == 0 && r.end_t <= 0.6 + 2 * fr, "%g Hz: the ledge kill refused", hz);
    }
}

int main(void)
{
    test_plunge();
    test_span();
    test_aim();
    test_blink();
    printf("test_above: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
