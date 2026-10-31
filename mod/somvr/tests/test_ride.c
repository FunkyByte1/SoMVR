/* test_ride.c - unit tests of somvr_ride.c (SOMVR_RIDE, docs/re-xr-pipeline.md 105): the kinds, the car-style yaw (a
 * turn followed, never more than the cap a frame, a one-frame lurch spread, a respawn's jump no turn, the wrap), the
 * seat (a stride's bob of the rider's head never reaches it, the offset's slow follow, a stranger's node rejected), the
 * buttons (armed after all up, the trigger's hysteresis, the charge's latch) */
#include "../somvr_ride.h"

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
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))
#define DEG 0.017453293f

static void test_kinds(void)
{
    CHECK(somvr_rd_kind("Warg_Brown") == SOMVR_RD_CARAGOR, "caragor");
    CHECK(somvr_rd_kind("Troll_Cave") == SOMVR_RD_GRAUG, "graug");
    CHECK(somvr_rd_kind("Uruk_Berserker") == -1 && somvr_rd_kind(NULL) == -1, "not a mount");
}

static void test_yaw(void)
{
    somvr_rd_cfg c;
    somvr_rd_cfg_default(&c);
    somvr_rd_yaw y;
    float r, dt = 1.0f / 90, tot = 0, maxstep = 0;
    /* 1. still: no turn */
    somvr_rd_yaw_init(&y);
    for (int i = 0; i < 90; i++)
        tot += somvr_rd_yaw_step(&y, &c, 0.7f, dt, &r);
    CHECK(tot == 0 && y.latches == 1, "still: %g rad, latches %llu", tot, (unsigned long long)y.latches);
    /* 2. the beast turns 90 deg/s for 2 s, then holds: the view turns the same 180 deg, never above the cap */
    somvr_rd_yaw_init(&y);
    float h = 0;
    tot = 0;
    for (int i = 0; i < 90 * 4; i++) {
        if (i < 180)
            h += 90 * DEG * dt;
        float s = somvr_rd_yaw_step(&y, &c, h > (float)M_PI ? h - 2 * (float)M_PI : h, dt, &r); /* wrapped as the game's */
        tot += s, maxstep = fmaxf(maxstep, fabsf(s));
    }
    CHECK(NEAR(tot, 179 * DEG, 0.2 * DEG), "a 180 deg turn followed (its first degree the latch): %g deg", tot / DEG);
    CHECK(maxstep <= 90 * DEG * dt * 1.05f, "no frame faster than the beast: %g deg/frame", maxstep / DEG);
    /* 3. the game's heading lurches 60 deg in one frame: spread over frames, at most yaw_dps */
    somvr_rd_yaw_init(&y);
    somvr_rd_yaw_step(&y, &c, 0, dt, &r);
    tot = 0, maxstep = 0;
    int frames_turning = 0;
    for (int i = 0; i < 180; i++) {
        float s = somvr_rd_yaw_step(&y, &c, 60 * DEG, dt, &r);
        tot += s, maxstep = fmaxf(maxstep, fabsf(s)), frames_turning += fabsf(s) > 1e-5f;
    }
    CHECK(NEAR(tot, 60 * DEG, 0.1 * DEG), "the lurch followed in the end: %g deg", tot / DEG);
    CHECK(maxstep <= c.yaw_dps * DEG * dt + 1e-6f, "the lurch capped: %g deg in a frame (cap %g)", maxstep / DEG,
          c.yaw_dps * dt);
    CHECK(frames_turning >= 36, "the lurch spread over %d frames (0.4 s at least)", frames_turning);
    /* the old way (the view's yaw = the beast's): the whole 60 deg in one frame */
    CHECK(60 * DEG > c.yaw_dps * DEG * dt * 10, "(the old way's step is 10x the cap)");
    /* 4. a respawn / another beast: a 170 deg jump in one frame = re-latched, no turn */
    unsigned long long l0 = y.latches;
    float s = somvr_rd_yaw_step(&y, &c, 60 * DEG + 170 * DEG, dt, &r);
    CHECK(s == 0 && y.latches == l0 + 1, "a jump re-latches: %g deg, latches %llu", s / DEG, (unsigned long long)y.latches);
    /* 5. the wrap: from +175 deg to -175 deg is +10 deg, not -350 */
    somvr_rd_yaw_init(&y);
    somvr_rd_yaw_step(&y, &c, 175 * DEG, dt, &r);
    tot = 0;
    for (int i = 0; i < 180; i++)
        tot += somvr_rd_yaw_step(&y, &c, -175 * DEG, dt, &r);
    CHECK(NEAR(tot, 10 * DEG, 0.1 * DEG), "across the wrap: %g deg", tot / DEG);
    /* 6. a gap (a load) re-latches; a 20 Hz frame (headless) still capped */
    l0 = y.latches;
    s = somvr_rd_yaw_step(&y, &c, 0, 0.5f, &r);
    CHECK(s == 0 && y.latches == l0 + 1, "a gap: %g", s);
    s = somvr_rd_yaw_step(&y, &c, 90 * DEG, 0.05f, &r);
    CHECK(s <= c.yaw_dps * DEG * 0.05f + 1e-6f && s > 0, "20 Hz: %g deg", s / DEG);
    /* 7. the cap off (0): the low-pass alone */
    somvr_rd_cfg c2 = c;
    c2.yaw_dps = 0;
    somvr_rd_yaw_init(&y);
    somvr_rd_yaw_step(&y, &c2, 0, dt, &r);
    s = somvr_rd_yaw_step(&y, &c2, 60 * DEG, dt, &r);
    CHECK(s > 0 && s < 60 * DEG * 0.05f, "the low-pass alone: %g deg in the first frame", s / DEG);
}

static void test_seat(void)
{
    somvr_rd_cfg c;
    somvr_rd_cfg_default(&c);
    somvr_rd_seat st;
    somvr_rd_seat_init(&st, &c, SOMVR_RD_CARAGOR);
    float dt = 1.0f / 90, beast[3] = { 1000, 500, -2000 }, out[3];
    /* 1. no head node: the kind's seat */
    somvr_rd_seat_step(&st, &c, beast, 0, NULL, dt, out);
    CHECK(NEAR(out[1], 500 + c.seat[0][1], 1e-3) && NEAR(out[2], -2000 + c.seat[0][2], 1e-3), "the default seat: %g %g", out[1],
          out[2]);
    /* 2. the beast runs at 10 m/s, turning 30 deg/s; the rider's head node bobs +-10 cm at 2.5 Hz and sways +-6 cm: the
     * seat point's height above the beast's root holds within 1 cm after its first sample, its sideways place within
     * 1 cm; the head node itself would have moved the view 20 cm a stride (the old head-follow) */
    float base_off[3] = { 0, 195, 25 }, minY = 1e9f, maxY = -1e9f, maxX = 0, headspan = 0, hmin = 1e9f, hmax = -1e9f;
    float h = 0;
    for (int i = 0; i < 90 * 6; i++) {
        float t = i * dt, bob = 10 * sinf(2 * (float)M_PI * 2.5f * t), sway = 6 * sinf(2 * (float)M_PI * 1.25f * t);
        h += 30 * DEG * dt;
        beast[0] += 1000 * sinf(h) * dt, beast[2] += 1000 * cosf(h) * dt;
        float l[3] = { base_off[0] + sway, base_off[1] + bob, base_off[2] }, head[3];
        somvr_rd_from_beast(l, beast, h, head);
        somvr_rd_seat_step(&st, &c, beast, h, head, dt, out);
        float lo[3];
        somvr_rd_to_beast(out, beast, h, lo);
        if (i >= 90) {
            minY = fminf(minY, lo[1]), maxY = fmaxf(maxY, lo[1]), maxX = fmaxf(maxX, fabsf(lo[0]));
            hmin = fminf(hmin, head[1] - beast[1]), hmax = fmaxf(hmax, head[1] - beast[1]);
        }
    }
    headspan = hmax - hmin;
    CHECK(maxY - minY <= 1.0f, "the bob kept out: the seat's height varies %.2f cm (the head node %.1f)", maxY - minY, headspan);
    CHECK(maxX <= 1.5f, "the sway kept out: %.2f cm aside", maxX);
    CHECK(headspan >= 19, "(the old head-follow: %.1f cm a stride)", headspan);
    CHECK(NEAR(st.off[1], 195, 1.0) && NEAR(st.off[2], 25, 1.0), "the offset is the rider's mean: %g %g", st.off[1], st.off[2]);
    /* 3. the rider leans: his mean head 30 cm lower - the offset follows at most 10 cm/s */
    somvr_rd_seat_init(&st, &c, SOMVR_RD_CARAGOR);
    float l0[3] = { 0, 200, 10 }, head[3];
    somvr_rd_from_beast(l0, beast, h, head);
    somvr_rd_seat_step(&st, &c, beast, h, head, dt, out);
    float prev = st.off[1], maxv = 0;
    l0[1] = 170;
    somvr_rd_from_beast(l0, beast, h, head);
    for (int i = 0; i < 90 * 8; i++) {
        somvr_rd_seat_step(&st, &c, beast, h, head, dt, out);
        maxv = fmaxf(maxv, fabsf(st.off[1] - prev) / dt), prev = st.off[1];
    }
    CHECK(maxv <= c.off_cmps * 1.001f, "the offset's speed %.2f cm/s", maxv);
    CHECK(NEAR(st.off[1], 170, 1.0), "the offset settles: %g", st.off[1]);
    /* 4. a node far from the mean (another bone, a dismount under way): rejected */
    unsigned long long rj = st.rejected;
    float far[3] = { 0, 20, 0 }, fp[3];
    somvr_rd_from_beast(far, beast, h, fp);
    somvr_rd_seat_step(&st, &c, beast, h, fp, dt, out);
    CHECK(st.rejected == rj + 1 && NEAR(st.off[1], 170, 1.0), "a stranger's node rejected");
    /* 4b. the first node far from the kind's seat (a bigger beast, another saddle) but on the beast: taken */
    somvr_rd_seat_init(&st, &c, SOMVR_RD_CARAGOR);
    float l9[3] = { 0, 300, -40 }, h9[3];
    somvr_rd_from_beast(l9, beast, h, h9);
    somvr_rd_seat_step(&st, &c, beast, h, h9, dt, out);
    CHECK(st.have && NEAR(st.off[1], 300, 0.1) && NEAR(st.off[2], -40, 0.1), "the first node on the beast taken: %g %g",
          st.off[1], st.off[2]);
    float l8[3] = { 0, 560, 0 }, h8[3];
    somvr_rd_seat_init(&st, &c, SOMVR_RD_GRAUG);
    somvr_rd_from_beast(l8, beast, h, h8);
    somvr_rd_seat_step(&st, &c, beast, h, h8, dt, out);
    CHECK(!st.have && st.rejected == 1, "5.6 m over the root is not the rider's: the graug's seat kept");
    /* 5. the graug's seat */
    somvr_rd_seat_init(&st, &c, SOMVR_RD_GRAUG);
    somvr_rd_seat_step(&st, &c, beast, 0, NULL, dt, out);
    CHECK(NEAR(out[1] - beast[1], c.seat[SOMVR_RD_GRAUG][1], 1e-3) && c.y_s[SOMVR_RD_GRAUG] > c.y_s[SOMVR_RD_CARAGOR],
          "the graug's seat %g", out[1] - beast[1]);
    /* 6. the frame conversions round-trip */
    float p[3] = { 123, 45, -67 }, q[3], r2[3];
    somvr_rd_to_beast(p, beast, 1.1f, q), somvr_rd_from_beast(q, beast, 1.1f, r2);
    CHECK(NEAR(r2[0], p[0], 1e-2) && NEAR(r2[1], p[1], 1e-2) && NEAR(r2[2], p[2], 1e-2), "round trip");
    float ahead[3] = { 0, 0, 100 }, w[3];
    somvr_rd_from_beast(ahead, (float[3]){ 0, 0, 0 }, 90 * DEG, w);
    CHECK(NEAR(w[0], 100, 1e-3) && NEAR(w[2], 0, 1e-3), "heading 90 deg = ahead is +x: %g %g", w[0], w[2]);
}

static void test_reach(void)
{
    float cm, deg;
    /* the test hand's rest (25 left, 30 ahead): reached out at a beast ahead */
    CHECK(somvr_rd_reached(-25, 30, 0, &cm, &deg) && NEAR(cm, 39.05, 0.1), "rest pose ahead: %g cm %g deg", cm, deg);
    CHECK(!somvr_rd_reached(-10, 15, 0, &cm, &deg), "the hand at the chest is not reached out (%g cm)", cm);
    CHECK(!somvr_rd_reached(-20, -40, 0, &cm, &deg), "behind: not toward the view (%g deg)", deg);
    CHECK(somvr_rd_reached(45, 5, 90 * DEG, &cm, &deg) && deg < 10, "the view turned right: the hand to the right is ahead");
    CHECK(!somvr_rd_reached(NAN, 30, 0, &cm, &deg), "no hand");
}

static void test_buttons(void)
{
    somvr_rd_btn_cfg c = { 0.6f, 0.4f };
    somvr_rd_btn b;
    somvr_rd_btn_init(&b);
    /* A still held from on foot (the jump onto the beast): nothing until all up */
    CHECK(somvr_rd_btn_step(&b, &c, 1, 0, 0, 0, 0) == 0, "not armed");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0, 0, 0) == 0 && b.armed, "armed");
    CHECK(somvr_rd_btn_step(&b, &c, 1, 0, 0, 0, 0) == SOMVR_RDB_JUMP, "A = the leap");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0.65f, 0, 0) == SOMVR_RDB_BITE, "RT = the bite");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0.5f, 0, 0) == SOMVR_RDB_BITE, "RT held over the off level");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0.3f, 0, 0) == 0, "RT let go");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0.5f, 0, 0) == 0, "RT under the on level");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 1, NAN, 0, 0) == SOMVR_RDB_OFF, "B = off");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0, 1, 0.9f) == SOMVR_RDB_CHARGE, "the click: the run");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0, 0, 0.9f) == SOMVR_RDB_CHARGE, "latched while pushed");
    CHECK(somvr_rd_btn_step(&b, &c, 0, 0, 0, 0, 0.1f) == 0, "the stick at rest ends it");
}

/* build 27: the domination's struggle - the class by its RTTI name; the grip carried over from the mount's grab is let
 * go for the gap (a fresh press after the struggle began: the build 26 hold never let go, its On came before the
 * struggle's delegate); Hold = held while gripping; PressCount = one short press per fresh close */
static void test_struggle(void)
{
    CHECK(somvr_rd_strug_kind("13CStruggleHold") == SOMVR_RDS_HOLD, "hold");
    CHECK(somvr_rd_strug_kind("19CStrugglePressCount") == SOMVR_RDS_COUNT, "count");
    CHECK(somvr_rd_strug_kind("18CStruggleQuickTime") == SOMVR_RDS_QT, "qt");
    CHECK(somvr_rd_strug_kind("12CStruggleAim") == SOMVR_RDS_AIM && somvr_rd_strug_kind("23CStruggleInputDirection") ==
          SOMVR_RDS_DIR, "aim, dir");
    CHECK(somvr_rd_strug_kind("CFoo") == SOMVR_RDS_NONE && somvr_rd_strug_kind(NULL) == SOMVR_RDS_NONE, "none");
    somvr_rd_strug s;
    float dt = 0.05f; /* the input tick, 20 Hz */
    /* Hold: the left hand closed since the grab - let go for the gap, then held: exactly one fresh press */
    somvr_rd_strug_init(&s, SOMVR_RDS_HOLD, 0);
    int on0 = somvr_rd_strug_step(&s, 1, dt), ons = 0, edges = 0, was = on0;
    for (int i = 1; i < 100; i++) {
        int on = somvr_rd_strug_step(&s, 1, dt);
        ons += on, edges += on && !was, was = on;
    }
    CHECK(!on0 && edges == 1 && s.presses == 1 && ons >= 96, "hold: let go first (%d), then held (%d ticks, %d edges)",
          on0, ons, edges);
    /* let go mid-way: off, and a new close holds anew */
    int a = somvr_rd_strug_step(&s, 0, dt), b = somvr_rd_strug_step(&s, 1, dt);
    CHECK(!a && b && s.presses == 2, "hold: let go, closed anew");
    /* PressCount: kept closed = one press; each fresh close one short press */
    somvr_rd_strug_init(&s, SOMVR_RDS_COUNT, 0);
    ons = 0;
    for (int i = 0; i < 40; i++)
        ons += somvr_rd_strug_step(&s, 1, dt);
    CHECK(s.presses == 1 && ons >= 1 && ons <= 2, "count: one press while held (%d ticks)", ons);
    edges = 0, was = 0;
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < 6; i++) {
            int on = somvr_rd_strug_step(&s, i < 3 ? 0 : 1, dt);
            edges += on && !was, was = on;
        }
    CHECK(s.presses == 6 && edges == 5, "count: five closes, five presses (%llu, %d)", (unsigned long long)s.presses, edges);
    /* Aim (the caragor's domination, boot 20261030-032747): no press; held on 1.5 s without letting go = won */
    somvr_rd_strug_init(&s, SOMVR_RDS_AIM, 1.5f);
    ons = 0;
    int won_at = -1;
    for (int i = 0; i < 60; i++) {
        ons += somvr_rd_strug_step(&s, 1, dt);
        if (s.won && won_at < 0)
            won_at = i;
    }
    CHECK(ons == 0 && won_at >= 30 && won_at <= 33, "aim: no press, won after 1.5 s held (tick %d)", won_at);
    /* let go at 1.0 s: the count starts again - won 1.5 s after the new close, not before */
    somvr_rd_strug_init(&s, SOMVR_RDS_AIM, 1.5f);
    won_at = -1;
    for (int i = 0; i < 100; i++) {
        somvr_rd_strug_step(&s, !(i >= 22 && i < 26), dt);
        if (s.won && won_at < 0)
            won_at = i;
    }
    CHECK(won_at >= 55 && won_at <= 57, "aim: let go mid-way, held anew (tick %d)", won_at);
    /* never held (build 26's answer to an aim was none: the game's 5 s ran out): never won */
    somvr_rd_strug_init(&s, SOMVR_RDS_AIM, 1.5f);
    for (int i = 0; i < 100; i++)
        somvr_rd_strug_step(&s, 0, dt);
    CHECK(!s.won, "aim: not held, not won");
    /* win_s 0: never */
    somvr_rd_strug_init(&s, SOMVR_RDS_AIM, 0);
    for (int i = 0; i < 100; i++)
        somvr_rd_strug_step(&s, 1, dt);
    CHECK(!s.won, "aim: win_s 0 never wins");
}

/* build 27c (som-b27-ride3, re-xr-pipeline.md 122): the beast's speed - the same at 20 and 90 Hz, the path (not the
 * chord) on a turn, a gap or a teleport starts again, the run's top and distance from the mark */
static void test_speed(void)
{
    somvr_rd_speed s;
    float p[3] = { 0, 0, 0 };
    for (int hz = 20; hz <= 90; hz += 70) {
        somvr_rd_speed_init(&s);
        float v = NAN;
        for (int i = 0; i <= 2 * hz; i++) {
            p[0] = 1000.0f * i / hz, p[2] = 0; /* 10 m/s */
            v = somvr_rd_speed_step(&s, p, 100.0 + (double)i / hz);
            if (i == hz / 2)
                CHECK(isnan(v), "%d Hz: no speed before a full window (%g)", hz, v);
        }
        CHECK(NEAR(v, 10, 0.05), "%d Hz: 10 m/s straight (%g)", hz, v);
        CHECK(NEAR(s.dist_m, 20, 0.05) && NEAR(s.time_s, 2, 1e-6), "%d Hz: 20 m in 2 s (%g %g)", hz, s.dist_m, s.time_s);
    }
    somvr_rd_speed_init(&s); /* a circle of 10 m radius at 8 m/s: the path's speed */
    float v = NAN;
    for (int i = 0; i <= 90 * 3; i++) {
        double a = 0.8 * i / 90.0; /* rad: 8 m/s over a 10 m radius */
        p[0] = (float)(1000 * sin(a)), p[2] = (float)(1000 * cos(a));
        v = somvr_rd_speed_step(&s, p, (double)i / 90);
    }
    CHECK(NEAR(v, 8, 0.05), "a turn: the path's speed (%g)", v);
    somvr_rd_speed_mark(&s);
    CHECK(s.top == 0 && s.dist_m == 0, "the mark");
    double t = 3.0;
    for (int i = 1; i <= 40; i++) { /* 15 m/s for 2 s at 20 Hz */
        p[0] += 75.0f, t += 0.05;
        v = somvr_rd_speed_step(&s, p, t);
    }
    CHECK(NEAR(s.top, 15, 0.3) && NEAR(s.dist_m, 30, 0.01), "the run's top %g and path %g m", s.top, s.dist_m);
    uint64_t r0 = s.resets;
    t += 0.7, p[0] += 1000; /* a window's gap */
    v = somvr_rd_speed_step(&s, p, t);
    CHECK(isnan(v) && s.resets == r0 + 1 && NEAR(s.dist_m, 30, 0.01), "a gap starts again, not counted (%g)", v);
    t += 0.05, p[0] += 3000; /* a teleport */
    v = somvr_rd_speed_step(&s, p, t);
    CHECK(isnan(v) && s.resets == r0 + 2, "a teleport starts again");
    for (int i = 1; i <= 20; i++)
        p[0] += 30.0f, t += 0.05, v = somvr_rd_speed_step(&s, p, t);
    CHECK(NEAR(v, 6, 0.05) && NEAR(s.top, 15, 0.3), "6 m/s again (%g), the top kept (%g)", v, s.top);
    CHECK(somvr_rd_speed_step(&s, p, t) == v, "the same sample twice: no change");
}

int main(void)
{
    test_kinds();
    test_yaw();
    test_seat();
    test_buttons();
    test_reach();
    test_struggle();
    test_speed();
    printf("test_ride: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
