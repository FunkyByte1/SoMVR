/* test_strike.c - the wraith dash's pure part (somvr_strike.h): the profile, the start, the path, the blink */
#include "../somvr_strike.h"

#include <math.h>
#include <stdio.h>

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                      \
    do {                                                                                                                   \
        g_n++;                                                                                                             \
        if (!(c)) {                                                                                                        \
            g_fail++;                                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                    \
            printf(__VA_ARGS__);                                                                                           \
            printf("\n");                                                                                                  \
        }                                                                                                                  \
    } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

static float dist(sv_vec3 a, sv_vec3 b) {
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}

static void test_profile(void) {
    const float T = 0.3f, e = 0.06f;
    CHECK(somvr_sk_profile(0, T, e) == 0 && somvr_sk_profile(T, T, e) == 1 && somvr_sk_profile(-1, T, e) == 0 &&
              somvr_sk_profile(2, T, e) == 1,
          "the ends");
    CHECK(NEAR(somvr_sk_profile(0.15f, T, e), 0.5, 1e-5), "the middle: %g", somvr_sk_profile(0.15f, T, e));
    /* monotonic, continuous; the speed by finite differences matches somvr_sk_speed x vmax */
    float prev = 0, worst = 0, vmax = 1.0f / (T - e);
    int mono = 1;
    for (int i = 1; i <= 3000; i++) {
        float t = T * i / 3000, p = somvr_sk_profile(t, T, e);
        mono &= p >= prev - 1e-6f;
        float fd = (p - prev) / (T / 3000), sp = somvr_sk_speed(t - 0.5f * T / 3000, T, e) * vmax;
        worst = fmaxf(worst, fabsf(fd - sp));
        prev = p;
    }
    CHECK(mono, "monotonic");
    CHECK(worst < 0.02f * vmax, "the speed is the way's derivative: worst %g of %g", worst, vmax);
    CHECK(somvr_sk_speed(0, T, e) == 0 && NEAR(somvr_sk_speed(0.15f, T, e), 1, 1e-6) && NEAR(somvr_sk_speed(e, T, e), 1, 1e-5) &&
              NEAR(somvr_sk_speed(0.5f * e, T, e), 0.5, 1e-5) && somvr_sk_speed(T, T, e) == 0,
          "the speed's shape");
    CHECK(NEAR(somvr_sk_vmax(1000, T, e), 1000 / 0.24, 1e-2), "vmax %g", somvr_sk_vmax(1000, T, e));
    CHECK(NEAR(somvr_sk_profile(0.1f, 0.2f, 0.5f), 0.5, 1e-5), "ends clamped to T/2");
    CHECK(NEAR(somvr_sk_profile(0.1f, 0.3f, 0), 1.0 / 3, 1e-5), "no ease: linear");
}

/* a run: frames at hz, p(t) given, the press at t_press */
static void test_teleport(void) {
    somvr_sk_cfg c;
    somvr_sk_cfg_default(&c);
    c.mode = SOMVR_BSK_DASH;
    somvr_sk s = {0};
    somvr_sk_out o;
    sv_vec3 A = {100, 200, 300}, B = {100, 210, 1300}; /* a 10 m teleport, 10 cm up */
    const double hz = 90;
    int started = 0, ended = 0, frames_moving = 0, f_start = -1;
    float max_step = 0, max_off = 0, max_dev = 0;
    sv_vec3 last = A;
    for (int i = 0; i < 200; i++) {
        double t = i / hz;
        if (i == 10) somvr_sk_press(&s, t);
        sv_vec3 p = i < 40 ? A : B; /* the game teleports at frame 40 */
        somvr_sk_step(&s, &c, t, p, &o);
        started += o.started, ended += o.ended;
        if (o.started) f_start = i;
        if (o.active) {
            frames_moving++;
            /* on the line A -> B */
            float k = ((o.view.x - A.x) * (B.x - A.x) + (o.view.y - A.y) * (B.y - A.y) + (o.view.z - A.z) * (B.z - A.z)) /
                      (dist(A, B) * dist(A, B));
            sv_vec3 q = {A.x + k * (B.x - A.x), A.y + k * (B.y - A.y), A.z + k * (B.z - A.z)};
            max_off = fmaxf(max_off, dist(q, o.view));
            float tau = (float)(t - 40 / hz);
            float want = somvr_sk_profile(tau, c.dash_s, c.ease_s);
            max_dev = fmaxf(max_dev, fabsf(k - want));
        }
        max_step = fmaxf(max_step, dist(o.view, last));
        last = o.view;
        if (i == 39) CHECK(dist(o.view, A) < 1e-4f, "before the jump: at A");
        if (i == 40) CHECK(o.started && dist(o.view, A) < 1e-3f, "the jump's frame: still at A (started %d)", o.started);
    }
    CHECK(started == 1 && ended == 1 && f_start == 40, "one dash from frame 40: %d %d %d", started, ended, f_start);
    CHECK(frames_moving == 27, "0.3 s at 90 Hz: %d frames", frames_moving);
    float vmax = somvr_sk_vmax(dist(A, B), c.dash_s, c.ease_s);
    CHECK(max_step <= vmax / hz * 1.01f && max_step > vmax / hz * 0.95f, "no frame jump: the largest step %g (v %g/frame)",
          max_step, vmax / hz);
    CHECK(max_off < 0.01f && max_dev < 1e-5f, "on the straight line %g, at the designed point %g", max_off, max_dev);
    CHECK(dist(last, B) < 1e-4f && s.phase == SOMVR_BSK_IDLE, "at B, idle");
}

static void test_timeout_and_modes(void) {
    somvr_sk_cfg c;
    somvr_sk_cfg_default(&c);
    c.mode = SOMVR_BSK_DASH;
    somvr_sk s = {0};
    somvr_sk_out o;
    sv_vec3 A = {0, 0, 0};
    int to = 0, st = 0;
    for (int i = 0; i < 400; i++) {
        double t = i / 90.0;
        if (i == 5) somvr_sk_press(&s, t);
        sv_vec3 p = {A.x + 1.5f * i / 90.0f * 100, 0, 0}; /* walking 1.5 m/s: no strike */
        somvr_sk_step(&s, &c, t, p, &o);
        to += o.timed_out, st += o.started;
        CHECK(!o.active && dist(o.view, p) < 1e-5f, "a walk is passed through (frame %d)", i);
        if (!(dist(o.view, p) < 1e-5f)) break;
    }
    CHECK(to == 1 && st == 0 && s.timeouts == 1, "no move: one timeout (%d), no start (%d)", to, st);
    /* block / game: never moves, never arms */
    c.mode = SOMVR_BSK_BLOCK;
    somvr_sk s2 = {0};
    somvr_sk_press(&s2, 0);
    somvr_sk_step(&s2, &c, 0.01, A, &o);
    somvr_sk_step(&s2, &c, 0.02, (sv_vec3){0, 0, 1000}, &o);
    CHECK(!o.started && !o.active && o.view.z == 1000 && s2.phase == SOMVR_BSK_IDLE, "block passes through");
    CHECK(somvr_sk_mode_parse("dash") == SOMVR_BSK_DASH && somvr_sk_mode_parse("blink\n") == SOMVR_BSK_BLINK &&
              somvr_sk_mode_parse("game") == SOMVR_BSK_GAME && somvr_sk_mode_parse("block") == SOMVR_BSK_BLOCK &&
              somvr_sk_mode_parse("dashx") < 0 && somvr_sk_mode_parse(NULL) < 0,
          "the mode words");
}

/* a carried move with no teleport: the game moves him 8 m over 0.5 s (16 m/s): the view held, then after hold_max_s a
 * dash from the held point to where he is (no step over the dash's own) */
static void test_carried(void) {
    somvr_sk_cfg c;
    somvr_sk_cfg_default(&c);
    c.mode = SOMVR_BSK_DASH;
    somvr_sk s = {0};
    somvr_sk_out o;
    float max_step = 0, last_z = 0;
    int started = 0, carried = 0, held = 0;
    for (int i = 0; i < 150; i++) {
        double t = i / 90.0;
        if (i == 2) somvr_sk_press(&s, t);
        float z = i < 20 ? 0 : i < 65 ? 800.0f * (i - 20) / 45 : 800;
        somvr_sk_step(&s, &c, t, (sv_vec3){0, 0, z}, &o);
        started += o.started, carried += o.carried, held += o.held;
        if (i >= 21 && i < 20 + 72) CHECK(o.view.z == 0 || s.phase == SOMVR_BSK_MOVE || o.ended, "frame %d: held at the start (%g)", i, o.view.z);
        if (i > 0) max_step = fmaxf(max_step, fabsf(o.view.z - last_z));
        last_z = o.view.z;
        if (!(o.view.z <= z + 1e-3f && o.view.z >= -1e-3f)) {
            CHECK(0, "frame %d: the view %g not between the start and the target %g", i, o.view.z, z);
            break;
        }
    }
    CHECK(started == 1 && (held == 72 || held == 73) && carried == 45, "one dash after 0.8 s held (72 frames): %d %d %d", started, held, carried);
    CHECK(max_step <= somvr_sk_vmax(800, c.dash_s, c.ease_s) / 90 * 1.01f, "the largest step %g cm (the dash's %g)", max_step,
          somvr_sk_vmax(800, c.dash_s, c.ease_s) / 90);
    CHECK(last_z == 800, "at the target");
}

/* the stock strike (run 064438): a 1.6 m lunge over 0.2 s from 0.16 s after the press, the 7.8 m teleport at 0.46 s:
 * the view held at the press point through the lunge, then one dash from there to the arrival (no frame jump) */
static void test_stock(void) {
    somvr_sk_cfg c;
    somvr_sk_cfg_default(&c);
    c.mode = SOMVR_BSK_DASH;
    somvr_sk s = {0};
    somvr_sk_out o;
    sv_vec3 A = {0, 0, 0}, B = {0, 0, 160}, C = {0, -33, 940}, last = A;
    int started = 0, held = 0, f_start = -1;
    float max_step = 0, max_off = 0;
    for (int i = 0; i < 160; i++) { /* 90 Hz */
        double t = i / 90.0;
        if (i == 0) somvr_sk_press(&s, t);
        sv_vec3 p = i < 14 ? A : i < 32 ? (sv_vec3){0, 0, 160.0f * (i - 14) / 18} : i < 41 ? B : C;
        somvr_sk_step(&s, &c, t, p, &o);
        started += o.started, held += o.held;
        if (o.started) f_start = i;
        max_step = fmaxf(max_step, dist(o.view, last));
        last = o.view;
        if (o.active && !o.held) { /* on the line A -> C */
            float k = o.view.z / C.z;
            sv_vec3 q = {0, C.y * k, C.z * k};
            max_off = fmaxf(max_off, dist(q, o.view));
        }
        if (i > 14 && i < 41) CHECK(dist(o.view, A) < 1e-3f, "frame %d: held at the press point", i);
    }
    CHECK(started == 1 && f_start == 41 && held == 26, "the dash from the teleport frame: started %d at %d, held %d", started,
          f_start, held);
    CHECK(max_step <= somvr_sk_vmax(dist(A, C), c.dash_s, c.ease_s) / 90 * 1.01f && max_off < 0.01f,
          "the largest step %g (the dash's %g), off the line %g", max_step, somvr_sk_vmax(dist(A, C), c.dash_s, c.ease_s) / 90,
          max_off);
    CHECK(dist(last, C) < 1e-4f, "at the arrival");
}

static void test_blink(void) {
    somvr_sk_cfg c;
    somvr_sk_cfg_default(&c);
    c.mode = SOMVR_BSK_BLINK;
    somvr_sk s = {0};
    somvr_sk_out o;
    sv_vec3 A = {0, 0, 0}, B = {0, 0, 900};
    float peak = 0;
    int jumped_at_white = 1, ended = 0;
    for (int i = 0; i < 100; i++) {
        double t = i / 90.0;
        if (i == 1) somvr_sk_press(&s, t);
        sv_vec3 p = i < 10 ? A : B;
        somvr_sk_step(&s, &c, t, p, &o);
        peak = fmaxf(peak, o.blink);
        ended += o.ended;
        if (i > 10 && dist(o.view, B) < 1e-3f && i < 14) jumped_at_white &= o.blink > 0.5f;
        if (i == 10) CHECK(dist(o.view, A) < 1e-3f && o.blink == 0, "the jump's frame: at A, no white yet");
        if (i == 11) CHECK(dist(o.view, A) < 1e-3f && o.blink > 0.3f, "rising: at A (blink %g)", o.blink);
    }
    CHECK(peak > 0.85f && jumped_at_white && ended == 1, "the white blink: peak %g, jumped under white %d, ended %d", peak,
          jumped_at_white, ended);
}

int main(void) {
    test_profile();
    test_teleport();
    test_timeout_and_modes();
    test_carried();
    test_stock();
    test_blink();
    printf("test_strike: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
