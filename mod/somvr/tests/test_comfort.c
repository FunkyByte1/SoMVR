/* test_comfort.c - the comfort module's pure part (somvr_comfort.c): the ABI object (layout, entries by frame, a torn or
 * overwritten entry), the rig's artificial motion against the real rig (somvr_eyes.c: the head's world point through
 * two frames' mappings; the yaw helper = sv_qyaw), the measurement (the player's own head motion is never in it; stick
 * locomotion, a fall, a smooth turn about the head (sc_turn_p0), a yaw about the origin, the body follow's shift, a
 * snap, a recentre, a teleport, a fast but real move, a gap, the same time twice, NaN, the clock going back), the snap
 * blink's gate (held while fading out, landing on the one black frame, fading in, a second snap during the fade-in, a
 * vanished request, a hitch, 20 Hz), the seated height (fixed, measured, rejected, LOCAL), the vignette's curve, its
 * smoothing (closes faster than it opens, no overshoot, a cut to 0, bounded frame-to-frame change under jitter), the
 * view's extent, the ring geometry (no pop at the onset, always covering the view, the clamp) and the alpha texture. */
#include "../somvr_comfort.h"
#include "../somvr_ctl.h"
#include "../somvr_eyes.h"

#include <math.h>
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
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define DEG 0.01745329252f
#define P 0.0111111111 /* a 90 Hz frame */

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }

/* ---- the ABI ---------------------------------------------------------------------------------------------------- */
static somvr_comfort_shared g_obj;
static void test_abi(void) {
    CHECK(sizeof g_obj == 320 && sizeof(somvr_comfort_frame) == 32 && offsetof(somvr_comfort_shared, f) == 40,
          "layout %zu %zu", sizeof g_obj, sizeof(somvr_comfort_frame));
    CHECK(!memcmp(&(uint64_t){SOMVR_COMFORT_MAGIC}, "SOMRCMF1", 8), "magic is not \"SOMRCMF1\"");
    CHECK(!somvr_comfort_ok(&g_obj) && !somvr_comfort_ok(NULL), "a zero object is ok");
    g_obj.magic = SOMVR_COMFORT_MAGIC, g_obj.version = SOMVR_COMFORT_VERSION, g_obj.size = sizeof g_obj;
    CHECK(!somvr_comfort_ok(&g_obj), "ok before active");
    atomic_store(&g_obj.active, 1);
    CHECK(somvr_comfort_ok(&g_obj), "not ok when active");
    g_obj.version = 2;
    CHECK(!somvr_comfort_ok(&g_obj), "another version is ok");
    g_obj.version = 1, g_obj.size = 312;
    CHECK(!somvr_comfort_ok(&g_obj), "another size is ok");
    g_obj.size = sizeof g_obj, g_obj.magic ^= 1;
    CHECK(!somvr_comfort_ok(&g_obj), "another magic is ok");
    g_obj.magic = SOMVR_COMFORT_MAGIC;
    somvr_comfort_frame in = {.lin_mps = 1.5f, .ang_dps = 45, .blink = 0.25f, .dt_s = 0.011f, .flags = 1, .snaps = 3}, out;
    somvr_comfort_put(&g_obj, 1234, &in);
    CHECK(somvr_comfort_get(&g_obj, 1234, &out) && out.lin_mps == 1.5f && out.ang_dps == 45 && out.blink == 0.25f &&
              out.dt_s == 0.011f && out.flags == 1 && out.snaps == 3 && atomic_load(&out.seq) == 1234 &&
              atomic_load(&g_obj.last) == 1234 && atomic_load(&g_obj.n_frames) == 1,
          "round trip");
    CHECK(!somvr_comfort_get(&g_obj, 1234 + SOMVR_NREC, &out) && !somvr_comfort_get(&g_obj, 1235, &out) &&
              !somvr_comfort_get(&g_obj, 0, &out),
          "another frame's entry was read");
    somvr_comfort_put(&g_obj, 1234 + SOMVR_NREC, &in);
    CHECK(!somvr_comfort_get(&g_obj, 1234, &out), "an overwritten entry was read");
    atomic_store(&g_obj.f[1234 % SOMVR_NREC].seq, 0); /* being written */
    CHECK(!somvr_comfort_get(&g_obj, 1234 + SOMVR_NREC, &out), "an entry being written was read");
}

/* ---- the rig's mapping ------------------------------------------------------------------------------------------ */
static void eyes_at(const float h_xr[3], float yaw_deg, somvr_posef eye[2]) { /* XR space, metres; a head turned yaw */
    float a = 0.5f * yaw_deg * DEG;
    for (int e = 0; e < 2; e++) {
        float c = cosf(2 * a), s = sinf(2 * a), off = e ? 0.032f : -0.032f; /* XR: +yaw turns left; x axis (c, 0, -s) */
        eye[e] = (somvr_posef){{0, sinf(a), 0, cosf(a)}, {h_xr[0] + off * c, h_xr[1], h_xr[2] - off * s}};
    }
}
static void test_map(void) {
    int bad = 0;
    for (int i = 0; i < 200; i++) {
        float a = frand(-7, 7), v[3] = {frand(-300, 300), frand(-300, 300), frand(-300, 300)}, o[3];
        somvr_cf_yaw_rot(a, v, o);
        sv_vec3 r = sv_qrot(sv_qyaw(a), (sv_vec3){v[0], v[1], v[2]});
        bad += !(NEAR(o[0], r.x, 1e-3) && NEAR(o[1], r.y, 1e-4) && NEAR(o[2], r.z, 1e-3));
    }
    CHECK(bad == 0, "the yaw helper differs from sv_qrot(sv_qyaw) in %d of 200", bad);
    /* the map = the rig's own head mapping (somvr_rig_head) for random rigs and heads */
    bad = 0;
    for (int i = 0; i < 200; i++) {
        somvr_rig r;
        somvr_rig_init(&r);
        r.have_ref = 1, r.p0 = (sv_vec3){frand(-50, 50), frand(100, 180), frand(-50, 50)}, r.yaw0 = frand(-3, 3);
        somvr_base b = {{frand(-1e4f, 1e4f), frand(-500, 500), frand(-1e4f, 1e4f)}, frand(-3, 3)};
        float hx[3] = {frand(-1, 1), frand(0.8f, 1.9f), frand(-1, 1)};
        somvr_posef eye[2];
        eyes_at(hx, frand(-180, 180), eye);
        sv_vec3 pos, hp;
        sv_quat q, hq;
        somvr_rig_head(&r, b, eye, &pos, &q);
        somvr_head_game(eye, &hp, &hq);
        somvr_cf_map m = {{b.pos.x, b.pos.y, b.pos.z}, b.yaw - r.yaw0, {r.p0.x, r.p0.y, r.p0.z}, 0};
        float h[3] = {hp.x, hp.y, hp.z}, w[3];
        somvr_cf_map_apply(&m, h, w);
        bad += !(NEAR(w[0], pos.x, 2e-2) && NEAR(w[1], pos.y, 2e-2) && NEAR(w[2], pos.z, 2e-2));
    }
    CHECK(bad == 0, "the map differs from somvr_rig_head in %d of 200", bad);
}

/* ---- the measurement -------------------------------------------------------------------------------------------- */
static somvr_cf_meas M;
static double T;
static float LIN, ANG, DT;
static uint32_t meas(const somvr_cf_map *m, const float h[3], float snap, int rc) {
    return somvr_cf_measure(&M, m, h, T, snap, rc, &LIN, &ANG, &DT);
}
static void test_measure(void) {
    somvr_cf_meas_reset(&M);
    somvr_cf_map m = {{1000, 200, -300}, 0.3f, {10, 160, -5}, 0};
    float h[3] = {12, 165, -2};
    T = 7.0;
    CHECK(meas(&m, h, 0, 1) == (SOMVR_CFF_FIRST | SOMVR_CFF_RECENTER) && LIN == 0 && ANG == 0, "first frame");
    /* the player's own head: walking in the room, turning, ducking - the rig still: nothing */
    float worst = 0;
    for (int i = 0; i < 30; i++) {
        T += P;
        h[0] += 3, h[1] -= 1, h[2] += 2;
        uint32_t f = meas(&m, h, 0, 0);
        if (f != SOMVR_CFF_COUNTED || LIN > worst) worst = f != SOMVR_CFF_COUNTED ? 99 : LIN;
        if (ANG > worst) worst = ANG;
    }
    CHECK(worst < 1e-3f, "the head's own motion counted as the rig's: %g", worst);
    /* stick locomotion: the base 2 cm a frame -> 1.8 m/s */
    for (int i = 0; i < 5; i++) {
        T += P;
        m.b[2] += 2;
        meas(&m, h, 0, 0);
    }
    CHECK(NEAR(LIN, 1.8, 2e-3) && ANG == 0 && NEAR(DT, P, 1e-6), "walk: %g m/s %g deg/s dt %g", LIN, ANG, DT);
    /* a fall: 10 cm a frame -> 9 m/s */
    T += P;
    m.b[1] -= 10;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 9.0, 5e-3), "fall: %g", LIN);
    /* a smooth turn about the head (in_rig_turn: yaw0 -= d, P0 by sc_turn_p0): 1 deg a frame = 90 deg/s, no linear */
    for (int i = 0; i < 4; i++) {
        T += P;
        sv_vec3 p1 = sc_turn_p0((sv_vec3){m.p0[0], m.p0[1], m.p0[2]}, (sv_vec3){h[0], h[1], h[2]}, 1 * DEG);
        m.p0[0] = p1.x, m.p0[2] = p1.z, m.a += 1 * DEG;
        meas(&m, h, 0, 0);
    }
    CHECK(NEAR(ANG, 90, 0.05) && LIN < 2e-3f, "smooth turn about the head: %g deg/s %g m/s", ANG, LIN);
    /* a yaw about the origin with the head 50 cm from it (SOMVR_YAW=camera turning): the head's point swings too */
    m.p0[0] = h[0] - 50, m.p0[2] = h[2];
    T += P;
    meas(&m, h, 0, 1); /* P0 latched there (a recentre: not motion) */
    T += P;
    m.a += 2 * DEG;
    meas(&m, h, 0, 0);
    double want = 2 * 50 * sin(1 * DEG) * 0.01 / P;
    CHECK(NEAR(ANG, 180, 0.1) && NEAR(LIN, want, 2e-3), "yaw about the origin: %g deg/s %g m/s (want %g)", ANG, LIN, want);
    /* the body follow: Talion moved 20 cm (the base with him), P0 shifted the same in tracking space: nothing */
    {
        somvr_rig r;
        somvr_rig_init(&r);
        r.have_ref = 1, r.p0 = (sv_vec3){m.p0[0], m.p0[1], m.p0[2]}, r.yaw0 = 0.4f;
        somvr_base b0 = {{m.b[0], m.b[1], m.b[2]}, 0.4f + m.a};
        somvr_base b1 = b0;
        sv_vec3 d = {12, 0, -16};
        b1.pos.x += d.x, b1.pos.z += d.z;
        somvr_rig_shift(&r, b1, d);
        m.p0[0] = r.p0.x, m.p0[2] = r.p0.z;
        m.b[0] = b1.pos.x, m.b[2] = b1.pos.z;
        T += P;
        CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && LIN < 2e-3f && ANG < 1e-3f, "the follow's shift: %g m/s", LIN);
    }
    /* stick walk again, then a snap (30 deg) in the same frame as a step: not counted, the walk's speeds kept */
    for (int i = 0; i < 3; i++) {
        T += P;
        m.b[0] += 2;
        meas(&m, h, 0, 0);
    }
    float walk = LIN;
    T += P;
    m.a += 30 * DEG, m.b[0] += 2;
    CHECK(meas(&m, h, 30 * DEG, 0) == (SOMVR_CFF_SNAP | SOMVR_CFF_HELD) && LIN == walk && ANG < 1e-3f,
          "snap: %g m/s %g deg/s", LIN, ANG);
    T += P;
    m.b[0] += 2;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, walk, 2e-3) && ANG < 1e-3f, "after the snap: %g", LIN);
    /* a recentre: P0 and the yaw latched anew */
    T += P;
    m.p0[0] = h[0], m.p0[1] = h[1], m.p0[2] = h[2], m.a -= 0.7f;
    CHECK(meas(&m, h, 0, 1) == (SOMVR_CFF_RECENTER | SOMVR_CFF_HELD) && LIN == walk, "recentre");
    /* a teleport: 5 m at once; the next frame is measured from the new place */
    T += P;
    m.b[2] += 500;
    CHECK(meas(&m, h, 0, 0) == (SOMVR_CFF_JUMP | SOMVR_CFF_HELD) && LIN == walk, "teleport: flags/kept");
    T += P;
    m.b[2] += 2;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 1.8, 2e-3), "after the teleport: %g", LIN);
    /* the base's source changed (SOMVR_FALLBACK's swap to a game camera 20 cm away, its re-latch): below the jump
     * thresholds (18 m/s), but told as a jump (SOMVR_CF_EV_JUMP): not motion; measured from the new place after */
    T += P;
    m.b[1] += 20, m.p0[0] += 7;
    CHECK(meas(&m, h, 0, SOMVR_CF_EV_JUMP) == (SOMVR_CFF_JUMP | SOMVR_CFF_HELD) && NEAR(LIN, 1.8, 2e-3),
          "a base swap: flags/kept (%g)", LIN);
    T += P;
    m.b[2] += 2;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 1.8, 2e-3), "after the base swap: %g", LIN);
    T += 0.00001; /* the same display time again with a swap in it: the reference moves with it */
    m.b[1] -= 20;
    CHECK(meas(&m, h, 0, SOMVR_CF_EV_JUMP) == (SOMVR_CFF_JUMP | SOMVR_CFF_HELD), "a base swap at the same time");
    T += P;
    m.b[2] += 2;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 2.0 * 0.01 / (P + 0.00001), 2e-2), "after it: %g", LIN);
    /* a camera cut: 120 deg in one frame */
    T += P;
    m.a += 120 * DEG;
    CHECK(meas(&m, h, 0, 0) & SOMVR_CFF_JUMP, "a 120 deg cut is not a jump");
    /* a fast real move: 25 m/s (27.8 cm a frame) */
    T += P;
    m.b[0] += 27.7777f;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 25, 0.02), "25 m/s: %g", LIN);
    /* a gap (a menu): start again with 0 */
    T += 0.3;
    m.b[0] += 400;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_GAP && LIN == 0 && ANG == 0, "gap");
    T += P;
    m.b[0] += 1;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 0.9, 2e-3), "after the gap: %g", LIN);
    /* the same time twice: the frame before stays the reference */
    m.b[0] += 1;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_HELD && NEAR(LIN, 0.9, 2e-3), "the same time: %u", M.held != 0);
    T += P;
    m.b[0] += 1;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 1.8, 3e-3), "2 cm over one frame's time: %g", LIN);
    /* NaN: nothing kept from it */
    T += P;
    somvr_cf_map bad = m;
    bad.b[1] = NAN;
    CHECK(meas(&bad, h, 0, 0) == (SOMVR_CFF_JUMP | SOMVR_CFF_HELD), "NaN base");
    float hn[3] = {h[0], NAN, h[2]};
    CHECK(meas(&m, hn, 0, 0) == (SOMVR_CFF_JUMP | SOMVR_CFF_HELD), "NaN head");
    T += P;
    m.b[0] += 1;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_COUNTED && NEAR(LIN, 0.01 / (2 * P), 2e-3) && NEAR(DT, 2 * P, 1e-6),
          "after NaN: %g m/s over %g s (from the last good frame)", LIN, DT);
    /* the clock goes back (another time base): start again */
    T -= 5;
    CHECK(meas(&m, h, 0, 0) == SOMVR_CFF_GAP, "clock back");
    CHECK(M.counted > 40 && M.jumps == 5 && M.gaps == 2, "counters %llu %llu %llu", (unsigned long long)M.counted,
          (unsigned long long)M.jumps, (unsigned long long)M.gaps);
}

/* ---- the snap blink --------------------------------------------------------------------------------------------- */
static void test_blink(void) {
    float o, in;
    somvr_blink_times(90, &o, &in);
    CHECK(NEAR(o, 0.03, 1e-6) && NEAR(in, 0.06, 1e-6), "90 ms -> %g + %g", o, in);
    somvr_blink_times(0, &o, &in);
    CHECK(o == 0 && in == 0, "0 ms");
    somvr_blink b;
    somvr_blink_reset(&b);
    float a;
    CHECK(somvr_blink_frame(&b, 0, 0, 1, 1.0, &a) == 1 && a == 0, "off: the turn at once, no black");
    somvr_blink_times(90, &o, &in);
    double t = 10.0;
    somvr_blink_reset(&b);
    for (int i = 0; i < 3; i++, t += P) CHECK(somvr_blink_frame(&b, o, in, 0, t, &a) == 0 && a == 0, "idle");
    /* the request: held, darker each frame, the turn on the one black frame */
    int go = 0, held = 0, black = 0;
    float prev = 0;
    for (int i = 0; i < 4 && !go; i++, t += P) {
        go = somvr_blink_frame(&b, o, in, 1, t, &a);
        CHECK(a > prev || a == 1, "fade out: %g after %g", a, prev);
        prev = a;
        held += !go;
        black += a == 1;
    }
    CHECK(go && a == 1 && held == 2 && black == 1, "landed after %d held frames, black %g", held, a);
    /* the fade in: lighter each frame, 0 after 60 ms */
    int frames = 0;
    for (; frames < 20; frames++, t += P) {
        CHECK(somvr_blink_frame(&b, o, in, 0, t, &a) == 0 && a < prev, "fade in: %g after %g", a, prev);
        prev = a;
        if (a == 0) break;
    }
    CHECK(a == 0 && frames == 5 && b.phase == 0, "faded in after %d frames (%g)", frames, a);
    /* a second snap while the first fades in: it darkens again from where it is (never lighter), lands on black */
    for (go = 0; !go; t += P) go = somvr_blink_frame(&b, o, in, 1, t, &a);
    for (int i = 0; i < 2; i++, t += P) somvr_blink_frame(&b, o, in, 0, t, &a);
    float mid = a;
    go = somvr_blink_frame(&b, o, in, 1, t, &a);
    t += P;
    CHECK(mid > 0.5f && mid < 0.8f && (a > mid || (go && a == 1)), "a snap during the fade-in: %g -> %g", mid, a);
    for (int i = 0; i < 4 && !go; i++, t += P) go = somvr_blink_frame(&b, o, in, 1, t, &a);
    CHECK(go && a == 1, "the second snap did not land on black");
    for (int i = 0; i < 10; i++, t += P) somvr_blink_frame(&b, o, in, 0, t, &a);
    CHECK(a == 0 && b.phase == 0 && b.blinks == 3, "blinks %llu", (unsigned long long)b.blinks);
    /* the request goes away while held (a recentre took it): no turn, the black fades back */
    go = somvr_blink_frame(&b, o, in, 1, t, &a);
    t += P;
    float h1 = a;
    int go2 = somvr_blink_frame(&b, o, in, 0, t, &a);
    t += P;
    CHECK(!go && !go2 && h1 > 0 && a <= h1 && b.phase == 2, "vanished request: %d %d %g %g phase %d", go, go2, h1, a, b.phase);
    for (int i = 0; i < 10; i++, t += P) somvr_blink_frame(&b, o, in, 0, t, &a);
    CHECK(a == 0, "after a vanished request: %g", a);
    /* a hitch while held: the turn lands at the next frame */
    somvr_blink_frame(&b, o, in, 1, t, &a);
    t += 1.0;
    CHECK(somvr_blink_frame(&b, o, in, 1, t, &a) == 1 && a == 1, "hitch");
    for (int i = 0; i < 10; i++) t += P, somvr_blink_frame(&b, o, in, 0, t, &a);
    /* 20 Hz (Monado's remote driver): the request frame is already black */
    for (int i = 0; i < 3; i++) t += 0.05, somvr_blink_frame(&b, o, in, 0, t, &a);
    t += 0.05;
    CHECK(somvr_blink_frame(&b, o, in, 1, t, &a) == 1 && a == 1, "20 Hz: %g", a);
    /* a long blink: 600 ms = 200 out + 400 in */
    somvr_blink_times(600, &o, &in);
    somvr_blink_reset(&b);
    t = 50;
    int n = 0;
    for (go = 0; !go && n < 100; n++, t += P) go = somvr_blink_frame(&b, o, in, 1, t, &a);
    CHECK(go && NEAR(n, 0.2 / P, 1.5), "600 ms: landed after %d frames", n);
}

/* ---- seated height ---------------------------------------------------------------------------------------------- */
static void test_seat(void) {
    somvr_seat s = {0};
    somvr_seat_cfg c = {1, 0, 0};
    const char *why = "";
    CHECK(NEAR(somvr_seat_ref(&s, &c, 112, 1, &why), 112, 1e-6) && strstr(why, "the head now"), "STAGE: the head %s", why);
    CHECK(NEAR(somvr_seat_ref(&s, &c, 30, 1, &why), 112, 1e-6) && strstr(why, "kept"), "the headset on the desk: %s", why);
    CHECK(NEAR(somvr_seat_ref(&s, &c, 185, 1, &why), 112, 1e-6) && s.rejected == 2, "standing up: kept");
    somvr_seat s2 = {0};
    CHECK(NEAR(somvr_seat_ref(&s2, &c, 20, 1, &why), SOMVR_SEAT_DEFAULT_CM, 1e-6) && strstr(why, "default"),
          "no seated head yet: the default");
    c.has_height = 1, c.height_cm = 118;
    CHECK(NEAR(somvr_seat_ref(&s, &c, 140, 1, &why), 118, 1e-6) && strstr(why, "fixed"), "SOMVR_HEIGHT_CM fixed");
    CHECK(NEAR(somvr_seat_ref(&s, &c, 3, 0, &why), 3, 1e-6) && strstr(why, "not used"), "LOCAL: the head, %s", why);
    c.has_height = 0;
    CHECK(NEAR(somvr_seat_ref(&s, &c, -4, 0, &why), -4, 1e-6), "LOCAL: the head");
    CHECK(NEAR(somvr_seat_ref(&s, &c, NAN, 0, &why), -4, 1e-6), "LOCAL: NaN keeps the last");
    /* the rig with that reference: the head at the seated height shows at the base (Talion's eyes) */
    somvr_rig r;
    somvr_rig_init(&r);
    somvr_rig_cfg rc = {.layout = SOMVR_LAYOUT_B, .base = SOMVR_BASE_HEAD, .snap_rad = 30 * DEG, .floor = 1};
    float hx[3] = {0.1f, 1.12f, -0.2f};
    somvr_posef eye[2];
    eyes_at(hx, 0, eye);
    somvr_view game = {{0, 0, 0}, {0, 0, 0, 1}, 1.5f, 0.9f, 7, 1e5f};
    float body[3] = {500, 176.6f, 900};
    somvr_base b = somvr_rig_frame(&r, &rc, eye, &game, body, 1, 0);
    CHECK(r.p0.y == 0, "floor mode latched y %g", r.p0.y); /* without seated: the real height above the base */
    somvr_seat s3 = {0};
    c.has_height = 0;
    r.p0.y = somvr_seat_ref(&s3, &c, 112, 1, &why);
    sv_vec3 pos;
    sv_quat q;
    somvr_rig_head(&r, b, eye, &pos, &q);
    CHECK(NEAR(pos.y, 176.6, 1e-3), "seated: the view at %g, want Talion's 176.6", pos.y);
    hx[1] = 1.02f; /* leaning down 10 cm: the view 10 cm lower */
    eyes_at(hx, 0, eye);
    somvr_rig_head(&r, b, eye, &pos, &q);
    CHECK(NEAR(pos.y, 166.6, 1e-3), "seated, leaning: %g", pos.y);
}

/* ---- the vignette ----------------------------------------------------------------------------------------------- */
static void test_curve(void) {
    somvr_vig_cfg c;
    somvr_vig_cfg_default(&c);
    CHECK(somvr_vig_target(&c, 0, 0) == 0 && somvr_vig_target(&c, 0.29f, 9.9f) == 0, "below the onset");
    CHECK(somvr_vig_target(&c, 3.0f, 0) == 1 && somvr_vig_target(&c, 8, 0) == 1 && somvr_vig_target(&c, 0, 90) == 1 &&
              somvr_vig_target(&c, 0, 500) == 1,
          "full");
    float prev = -1;
    int mono = 1;
    for (float v = 0; v <= 4; v += 0.05f) {
        float t = somvr_vig_target(&c, v, 0);
        mono &= t >= prev && t >= 0 && t <= 1;
        prev = t;
    }
    CHECK(mono, "the linear curve is not monotonic in 0..1");
    float mid = somvr_vig_target(&c, 1.65f, 0);
    CHECK(NEAR(mid, 0.5, 1e-3), "the midpoint %g", mid);
    CHECK(somvr_vig_target(&c, 1.0f, 50) == fmaxf(somvr_vig_target(&c, 1.0f, 0), somvr_vig_target(&c, 0, 50)),
          "the larger of the two");
    CHECK(somvr_vig_target(&c, NAN, NAN) == 0 && somvr_vig_target(&c, INFINITY, 0) == 0, "NaN/inf");
    /* walking at 1.4 m/s: a light vignette (design: walking off/light), a jog full */
    CHECK(somvr_vig_target(&c, 1.4f, 0) > 0.2f && somvr_vig_target(&c, 1.4f, 0) < 0.5f, "walk: %g",
          somvr_vig_target(&c, 1.4f, 0));
}

static void test_smooth(void) {
    somvr_vig_cfg c;
    somvr_vig_cfg_default(&c);
    somvr_vig_state s = {0, 0};
    float a = 0, t95 = -1, t50 = -1, prev = 0;
    int over = 0, mono = 1;
    for (int i = 1; i <= 90; i++) {
        a = somvr_vig_step(&c, &s, 1, (float)P);
        over |= a > 1;
        mono &= a >= prev;
        prev = a;
        if (t50 < 0 && a >= 0.5f) t50 = (float)(i * P);
        if (t95 < 0 && a >= 0.95f) t95 = (float)(i * P);
    }
    /* build 6, HL:A-like: in over ~0.25 s (no pop: not half way in the first 50 ms), out over ~0.35 s */
    CHECK(!over && mono && t50 > 0.05f && t50 < 0.15f && t95 > 0 && t95 < 0.3f, "closing: 50 %% at %g s, 95 %% at %g s",
          t50, t95);
    float o50 = -1, o95 = -1;
    int zero = -1;
    for (int i = 1; i <= 400; i++) {
        a = somvr_vig_step(&c, &s, 0, (float)P);
        mono &= a <= prev;
        prev = a;
        if (o50 < 0 && a <= 0.5f) o50 = (float)(i * P);
        if (o95 < 0 && a <= 0.05f) o95 = (float)(i * P);
        if (zero < 0 && a == 0) zero = i;
    }
    CHECK(mono && o50 > t50 * 1.25f && o50 < 0.25f && o95 > 0 && o95 < 0.45f,
          "opening: 50 %% at %g s, 95 %% at %g s (closing %g)", o50, o95, t50);
    CHECK(zero > 0 && zero * P < 3.0, "not cut to 0 (%d frames)", zero);
    CHECK(somvr_vig_step(&c, &s, 1, 0) == 0 && somvr_vig_step(&c, &s, 1, -1) == 0 && somvr_vig_step(&c, &s, 1, NAN) == 0,
          "dt <= 0 changed it");
    /* jitter: a target of 0.5 +- 0.15 every frame (a bobbing head base): a steady ring */
    srand(7);
    float maxd = 0, sum = 0;
    for (int i = 0; i < 900; i++) {
        float b = somvr_vig_step(&c, &s, 0.5f + frand(-0.15f, 0.15f), (float)P);
        if (i > 180) {
            maxd = fmaxf(maxd, fabsf(b - a));
            sum += b;
        }
        a = b;
    }
    CHECK(maxd < 0.012f && NEAR(sum / 719, 0.5, 0.03), "jitter: frame-to-frame %g, mean %g", maxd, sum / 719);
    /* a 1 s hitch: no overshoot past the target */
    a = somvr_vig_step(&c, &s, 0, 5.0f);
    CHECK(a >= 0 && a < 0.5f, "hitch %g", a);
    /* a snap turn's tunnel: the full strength in its own frame, then open within 0.45 s; never lowers a deeper one; a
     * strength of 0 or NaN does nothing */
    somvr_vig_state z = {0, 0};
    somvr_vig_snap(&z, 0.5f);
    CHECK(z.amount == 0.5f && z.f == 0.5f, "snap: %g %g", z.amount, z.f);
    int open = -1;
    for (int i = 1; i <= 90 && open < 0; i++)
        if (somvr_vig_step(&c, &z, 0, (float)P) <= 0.025f) open = i;
    CHECK(open > 3 && open * P < 0.45, "snap tunnel open after %d frames", open);
    somvr_vig_state d = {0.9f, 0.8f};
    somvr_vig_snap(&d, 0.5f);
    CHECK(d.amount == 0.8f && d.f == 0.9f, "snap kept the deeper %g %g", d.amount, d.f);
    somvr_vig_state n = {0, 0};
    somvr_vig_snap(&n, 0), somvr_vig_snap(&n, NAN), somvr_vig_snap(&n, -1);
    CHECK(n.amount == 0 && n.f == 0, "snap at 0/NaN: %g", n.amount);
    somvr_vig_snap(&n, 3);
    CHECK(n.amount == 1, "snap clamped %g", n.amount);
}

static void test_geom(void) {
    somvr_vig_cfg c;
    somvr_vig_cfg_default(&c);
    const float fov[2][4] = {{-0.9f, 0.7f, 0.8f, -0.95f}, {-0.7f, 0.9f, 0.8f, -0.95f}}; /* the mock runtime's */
    float cov, cor;
    somvr_vig_extent(fov, &cov, &cor);
    float wcor = atanf(sqrtf(tanf(0.9f) * tanf(0.9f) + tanf(0.95f) * tanf(0.95f)));
    CHECK(NEAR(cov, tanf(0.95f), 1e-5) && NEAR(cor, wcor, 1e-5), "extent %g %g (want %g %g)", cov, cor, tanf(0.95f), wcor);
    /* the onset: no texel of the ring darker than 0 anywhere in either eye's view */
    somvr_vig_ring g = somvr_vig_geom(&c, 0.003f, cov, cor);
    float worst = 0;
    for (int e = 0; e < 2; e++)
        for (int i = 0; i <= 20; i++)
            for (int j = 0; j <= 20; j++) {
                float x = tanf(fov[e][0]) + (tanf(fov[e][1]) - tanf(fov[e][0])) * (float)i / 20;
                float y = tanf(fov[e][3]) + (tanf(fov[e][2]) - tanf(fov[e][3])) * (float)j / 20;
                worst = fmaxf(worst, somvr_vig_profile(sqrtf(x * x + y * y) / g.half_tan));
            }
    CHECK(worst == 0 && !g.clamped, "the onset darkens the view: %g", worst);
    /* closing: the clear angle falls with the amount, the quad always reaches past the view */
    float prev = 10;
    int mono = 1, covers = 1;
    for (int k = 0; k <= 100; k++) {
        somvr_vig_ring r = somvr_vig_geom(&c, k / 100.0f, cov, cor);
        mono &= r.clear_rad < prev || (k == 0);
        prev = r.clear_rad;
        covers &= r.half_tan >= 1.1f * cov - 1e-5f && r.cover_tan >= 1.1f * cov - 1e-5f;
    }
    CHECK(mono && covers, "closing: monotonic %d, covering %d", mono, covers);
    somvr_vig_ring full = somvr_vig_geom(&c, 1, cov, cor);
    CHECK(NEAR(full.clear_rad, 20 * DEG, 1e-5) && !full.clamped && NEAR(full.half_tan, tanf(20 * DEG) / SOMVR_VIG_R0, 1e-4),
          "full: %g deg, half %g", full.clear_rad / DEG, full.half_tan);
    /* at full strength the view's corners are black, the centre clear */
    CHECK(somvr_vig_profile(tanf(cor) / full.half_tan) == 1 && somvr_vig_profile(tanf(10 * DEG) / full.half_tan) == 0,
          "full: corners %g centre %g", somvr_vig_profile(tanf(cor) / full.half_tan),
          somvr_vig_profile(tanf(10 * DEG) / full.half_tan));
    /* a very wide headset: the ring cannot close past the clamp; the quad still covers */
    somvr_vig_ring w = somvr_vig_geom(&c, 1, 3.0f, 80 * DEG);
    CHECK(w.clamped && NEAR(w.half_tan, 3.3, 1e-5) && NEAR(w.clear_rad, atanf(SOMVR_VIG_R0 * 3.3f), 1e-5),
          "wide: %d %g %g", w.clamped, w.half_tan, w.clear_rad / DEG);
    somvr_vig_ring nan = somvr_vig_geom(&c, NAN, cov, cor);
    CHECK(NEAR(nan.clear_rad, g.clear_rad, 0.01), "NaN amount: open");
    const float nofov[2][4] = {{0}};
    somvr_vig_extent(nofov, &cov, &cor);
    CHECK(cov == 1.5f && NEAR(cor, 62 * DEG, 1e-5), "no fov: the defaults");
}

/* build 10: SOMVR_VIGNETTE_FIT=lens on the Quest 2's own fovs (WiVRn, session 2026-10-24 16:50): the user's 40 % while
 * walking must darken inside the lens's picture (~46-50 deg), 0 % nothing, 100 % a porthole */
static void test_lens(void) {
    somvr_vig_cfg c;
    somvr_vig_cfg_default(&c);
    CHECK(c.fit == SOMVR_VIG_FIT_CORNER, "the default fit is build 9's corner");
    somvr_vig_cfg_fit(&c, SOMVR_VIG_FIT_LENS);
    CHECK(c.fit == SOMVR_VIG_FIT_LENS && c.min_deg == 15.0f && c.lin_hi == 2.0f && c.ang_hi == 60.0f, "the lens profile");
    const float q2[2][4] = {{-0.7998f, 0.8315f, 0.8865f, -0.9176f}, {-0.8315f, 0.7998f, 0.8865f, -0.9176f}};
    float cov, cor, edge = somvr_vig_edge(q2);
    somvr_vig_extent(q2, &cov, &cor);
    CHECK(NEAR(edge, 0.7998f, 1e-5) && NEAR(cor / DEG, 59.6f, 0.1f), "Quest 2: edge %g deg corner %g deg", edge / DEG,
          cor / DEG);
    /* the speeds: the stick's walk and the menu's slowest smooth turn are full speed; a drift nothing */
    CHECK(somvr_vig_target(&c, 2.0f, 0) == 1 && somvr_vig_target(&c, 2.3f, 0) == 1 && somvr_vig_target(&c, 0, 60) == 1 &&
              somvr_vig_target(&c, 0.15f, 5) == 0 && somvr_vig_target(&c, 1.0f, 0) > 0.3f,
          "lens speeds: walk %g, 1 m/s %g", somvr_vig_target(&c, 2.3f, 0), somvr_vig_target(&c, 1.0f, 0));
    /* the onset: at a tiny amount nothing visible in the lens, at most a trace in the panel's corners */
    somvr_vig_ring g0 = somvr_vig_geom(&c, 0.003f, cov, cor);
    CHECK(somvr_vig_dark(&g0, edge) == 0 && somvr_vig_dark(&g0, cor) < 0.01f && !g0.clamped, "onset: rim %g corner %g",
          somvr_vig_dark(&g0, edge), somvr_vig_dark(&g0, cor));
    /* the strengths at full speed: how black the lens's rim (46 deg), 40, 35, 30 and 20 deg are */
    somvr_vig_ring g20 = somvr_vig_geom(&c, 0.2f, cov, cor), g40 = somvr_vig_geom(&c, 0.4f, cov, cor),
                   g70 = somvr_vig_geom(&c, 0.7f, cov, cor), g100 = somvr_vig_geom(&c, 1.0f, cov, cor);
    CHECK(somvr_vig_dark(&g20, edge) > 0.1f && somvr_vig_dark(&g20, 50 * DEG) > 0.4f && somvr_vig_dark(&g20, 35 * DEG) == 0,
          "20 %%: rim %g, 50 deg %g, 35 deg %g", somvr_vig_dark(&g20, edge), somvr_vig_dark(&g20, 50 * DEG),
          somvr_vig_dark(&g20, 35 * DEG));
    CHECK(somvr_vig_dark(&g40, edge) > 0.9f && somvr_vig_dark(&g40, 40 * DEG) > 0.4f && somvr_vig_dark(&g40, 30 * DEG) == 0,
          "40 %%: rim %g, 40 deg %g, 30 deg %g", somvr_vig_dark(&g40, edge), somvr_vig_dark(&g40, 40 * DEG),
          somvr_vig_dark(&g40, 30 * DEG));
    CHECK(somvr_vig_dark(&g70, 37 * DEG) == 1 && somvr_vig_dark(&g70, 30 * DEG) > 0.4f && somvr_vig_dark(&g70, 20 * DEG) == 0,
          "70 %%: 37 deg %g, 30 deg %g, 20 deg %g", somvr_vig_dark(&g70, 37 * DEG), somvr_vig_dark(&g70, 30 * DEG),
          somvr_vig_dark(&g70, 20 * DEG));
    CHECK(NEAR(g100.clear_rad, 15 * DEG, 1e-5) && !g100.clamped && somvr_vig_dark(&g100, 26 * DEG) == 1 &&
              somvr_vig_dark(&g100, 12 * DEG) == 0,
          "100 %%: clear %g deg, 26 deg %g", g100.clear_rad / DEG, somvr_vig_dark(&g100, 26 * DEG));
    /* closing: monotonic, the quad always past the view, and the soft edge at least 10 deg wide everywhere */
    float prev = 10;
    int mono = 1, covers = 1, soft = 1;
    for (int k = 0; k <= 100; k++) {
        somvr_vig_ring r = somvr_vig_geom(&c, k / 100.0f, cov, cor);
        mono &= r.clear_rad < prev || k == 0;
        prev = r.clear_rad;
        covers &= r.half_tan >= 1.1f * cov - 1e-5f;
        soft &= atanf(SOMVR_VIG_R0 * SOMVR_VIG_K * r.half_tan) - r.clear_rad > 10 * DEG;
    }
    CHECK(mono && covers && soft, "lens closing: monotonic %d covering %d soft %d", mono, covers, soft);
    /* the corner fit unchanged by it: 40 % at full speed leaves the rim clear (the build-9 report) */
    somvr_vig_cfg b9;
    somvr_vig_cfg_default(&b9);
    somvr_vig_ring o = somvr_vig_geom(&b9, 0.4f * somvr_vig_target(&b9, 2.3f, 0), cov, cor);
    CHECK(somvr_vig_dark(&o, edge) == 0 && somvr_vig_dark(&o, cor) > 0, "build 9 at 40 %% walking: rim %g corner %g",
          somvr_vig_dark(&o, edge), somvr_vig_dark(&o, cor));
    somvr_vig_cfg_fit(&c, 7); /* an unknown fit: the corner's */
    CHECK(c.fit == SOMVR_VIG_FIT_CORNER, "unknown fit");
    const float nofov[2][4] = {{0}};
    CHECK(NEAR(somvr_vig_edge(nofov), 46 * DEG, 1e-5), "no fov: 46 deg");
}

static uint8_t g_px[SOMVR_VIG_IMG_W * SOMVR_VIG_IMG_H * 4], g_px2[sizeof g_px];
static uint8_t A(int x, int y) { return g_px[4 * (y * SOMVR_VIG_IMG_W + x) + 3]; }
static void test_image(void) {
    somvr_vig_image(g_px, 0);
    somvr_vig_image(g_px2, 1);
    CHECK(!memcmp(g_px, g_px2, sizeof g_px), "BGRA differs (black: it must not)");
    int colour = 0;
    for (size_t i = 0; i < sizeof g_px; i += 4) colour |= g_px[i] | g_px[i + 1] | g_px[i + 2];
    CHECK(colour == 0, "a texel is not black");
    CHECK(A(255, 255) == 0 && A(256, 256) == 0 && A(0, 0) == 255 && A(511, 0) == 255 && A(0, 511) == 255 &&
              A(511, 511) == 255 && A(0, 256) == 255 && A(256, 0) == 255,
          "centre %u corners %u %u %u %u edges %u %u", A(255, 255), A(0, 0), A(511, 0), A(0, 511), A(511, 511), A(0, 256),
          A(256, 0));
    /* along a radius: 0 inside R0, rising, 255 outside R0 x K */
    int mono = 1, prev = -1, in0 = 1, out1 = 1;
    for (int x = 256; x < 512; x++) {
        float r = (x + 0.5f - 256) / 256;
        int a = A(x, 256);
        mono &= a >= prev;
        prev = a;
        if (r < SOMVR_VIG_R0 - 0.005f) in0 &= a == 0;
        if (r > SOMVR_VIG_R0 * SOMVR_VIG_K + 0.005f) out1 &= a == 255;
    }
    CHECK(mono && in0 && out1, "the radius: monotonic %d, clear inside %d, opaque outside %d", mono, in0, out1);
    /* round: the same alpha at the same distance in another direction */
    int round_ok = 1;
    for (int k = 0; k < 64; k++) {
        float ang = (float)k * 0.0981747f, r = 0.2f * 256;
        int x = (int)lrintf(256 + r * cosf(ang) - 0.5f), y = (int)lrintf(256 + r * sinf(ang) - 0.5f);
        float u = (x + 0.5f - 256) / 256, v = (y + 0.5f - 256) / 256;
        round_ok &= abs(A(x, y) - (int)lrintf(somvr_vig_profile(sqrtf(u * u + v * v)) * 255)) <= 1;
    }
    CHECK(round_ok, "the ring is not round");
    int guard = 1;
    for (int y = SOMVR_VIG_RING_PX; y < SOMVR_VIG_CELL_Y; y++)
        for (int x = 0; x < SOMVR_VIG_IMG_W; x++) guard &= A(x, y) == 255;
    CHECK(guard, "the guard rows are not opaque");
    int cells = 1;
    for (int k = 0; k < SOMVR_VIG_CELLS; k++) {
        int32_t rc[4];
        somvr_vig_rect_cell(k, rc);
        int want = (int)lrintf(255.0f * k / 127);
        cells &= rc[0] == 4 * k + 1 && rc[1] == SOMVR_VIG_CELL_Y + 1 && rc[2] == 2 && rc[3] == 6 &&
                 rc[0] + rc[2] <= SOMVR_VIG_IMG_W && rc[1] + rc[3] <= SOMVR_VIG_IMG_H;
        for (int y = SOMVR_VIG_CELL_Y; y < SOMVR_VIG_IMG_H; y++) /* the cell, its border texels included */
            for (int x = 4 * k; x < 4 * k + 4; x++) cells &= A(x, y) == want;
    }
    CHECK(cells, "the blink cells");
    CHECK(somvr_vig_cell(0) == 0 && somvr_vig_cell(1) == 127 && somvr_vig_cell(0.5f) == 64 && somvr_vig_cell(2) == 127 &&
              somvr_vig_cell(-1) == 0 && somvr_vig_cell(NAN) == 0,
          "cell picks");
    int32_t rr[4];
    somvr_vig_rect_ring(rr);
    CHECK(rr[0] == 0 && rr[1] == 0 && rr[2] == 512 && rr[3] == 512, "the ring rect");
    CHECK(somvr_vig_profile(0) == 0 && somvr_vig_profile(SOMVR_VIG_R0) == 0 &&
              somvr_vig_profile(SOMVR_VIG_R0 * SOMVR_VIG_K) == 1 && somvr_vig_profile(1.5f) == 1,
          "the profile's ends");
}

/* build 12: the wraith tunnel's image (same alphas, white-blue premultiplied) and the flag's amount bits */
static uint8_t g_pw[sizeof g_px], g_pw2[sizeof g_px];
static void test_wraith(void) {
    somvr_vig_image(g_px, 0);
    somvr_vig_image_wraith(g_pw, 0);
    somvr_vig_image_wraith(g_pw2, 1);
    int alpha_same = 1, order = 1;
    for (size_t i = 0; i < sizeof g_px; i += 4) {
        alpha_same &= g_pw[i + 3] == g_px[i + 3];
        order &= g_pw[i] == g_pw2[i + 2] && g_pw[i + 1] == g_pw2[i + 1] && g_pw[i + 2] == g_pw2[i] && g_pw[i + 3] == g_pw2[i + 3];
    }
    size_t o = 4 * ((size_t)0 * SOMVR_VIG_IMG_W + 0); /* an opaque corner: the full white-blue */
    CHECK(alpha_same && order, "the wraith image: the black one's alphas %d, BGRA swaps R and B %d", alpha_same, order);
    CHECK(g_pw[o + 2] == 255 && g_pw[o] > 190 && g_pw[o] < g_pw[o + 1] && g_pw[o + 1] < 255 && g_pw[o + 3] == 255,
          "the opaque texel is white-blue: %u %u %u", g_pw[o], g_pw[o + 1], g_pw[o + 2]);
    size_t c = 4 * ((size_t)256 * SOMVR_VIG_IMG_W + 256); /* the clear centre: nothing */
    CHECK(!g_pw[c] && !g_pw[c + 1] && !g_pw[c + 2] && !g_pw[c + 3], "the clear centre is empty");
    /* premultiplied in linear light: the blue channel decodes to the alpha (B = 1) at a half-alpha texel */
    int hx = -1;
    for (int x = 256; x < 512 && hx < 0; x++)
        if (g_px[4 * (256 * SOMVR_VIG_IMG_W + x) + 3] >= 120) hx = x;
    size_t h = 4 * ((size_t)256 * SOMVR_VIG_IMG_W + (size_t)(hx < 0 ? 0 : hx));
    float b = g_pw[h + 2] / 255.0f, bl = b <= 0.04045f ? b / 12.92f : powf((b + 0.055f) / 1.055f, 2.4f);
    CHECK(hx > 0 && NEAR(bl, g_pw[h + 3] / 255.0, 0.02), "premultiplied: blue %g (linear) at alpha %g", bl, g_pw[h + 3] / 255.0);
    CHECK(somvr_cff_tunnel_bits(0) == 0 && somvr_cff_tunnel_bits(-1) == 0 && somvr_cff_tunnel_bits(NAN) == 0 &&
              somvr_cff_tunnel_bits(2) == 255u << SOMVR_CFF_TUNNEL_SHIFT,
          "tunnel bits");
    uint32_t f = SOMVR_CFF_WRAITH | SOMVR_CFF_COUNTED | somvr_cff_tunnel_bits(0.4f);
    CHECK(NEAR(somvr_cff_tunnel(f), 0.4, 0.003) && (f & 0xffu) == SOMVR_CFF_COUNTED && (f & SOMVR_CFF_WRAITH),
          "the amount round-trips (%g), the low flags kept", somvr_cff_tunnel(f));
}

int main(void) {
    test_abi();
    test_map();
    test_measure();
    test_blink();
    test_seat();
    test_curve();
    test_smooth();
    test_geom();
    test_lens();
    test_image();
    test_wraith();
    printf("test_comfort: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
