/* test_hands.c - the game thread's grip locate (somvr_hands.c) against a mock xrLocateSpace: the call (the published
 * grip spaces, the eye records' space as the base, the asked time, a chained XrSpaceVelocity), the values, the
 * fallback to the layer's snapshot moved to the time with its velocities (not ready, a failed locate, the session not
 * running, SOMVR_HANDS=layer: not a single call), the extrapolation limits, the OpenXR -> game mirror of poses and
 * velocities (the angular velocity as an axial vector, checked by finite differences), the rig mapping = the eyes'
 * (somvr_rig_eye), and 100 teardown rounds against a game thread locating both hands (never a destroyed space). */
#include "../somvr_hands.h"

#include <math.h>
#include <openxr/openxr.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

#define REF 0x3333u
#define GRIP(h) (0x7100u + (h))
static somvr_shared SH;
static somvr_input_shared IN;
static struct {
    XrResult result[2];
    XrSpaceLocationFlags flags;
    XrSpaceVelocityFlags vflags;
    int calls, bad, no_velocity;
    XrTime time;
    _Atomic int live[2], after_destroy, located;
} M = {{XR_SUCCESS, XR_SUCCESS}, 0xf, 3, 0, 0, 0, 0, {0, 0}, 0, 0};

static XrResult XRAPI_CALL mock_locate(XrSpace sp, XrSpace base, XrTime t, XrSpaceLocation *loc) {
    uintptr_t h = (uintptr_t)sp - GRIP(0);
    M.calls++;
    if (h > 1 || base != (XrSpace)(uintptr_t)REF || !loc || loc->type != XR_TYPE_SPACE_LOCATION) return M.bad++, XR_ERROR_VALIDATION_FAILURE;
    if (!atomic_load(&M.live[h])) atomic_fetch_add(&M.after_destroy, 1);
    XrSpaceVelocity *v = (XrSpaceVelocity *)loc->next;
    if (!v || v->type != XR_TYPE_SPACE_VELOCITY) M.no_velocity++;
    M.time = t;
    if (!atomic_load(&M.live[h])) atomic_fetch_add(&M.after_destroy, 1); /* destroyed while inside */
    atomic_fetch_add(&M.located, 1);
    if (XR_FAILED(M.result[h])) return M.result[h];
    loc->locationFlags = M.flags;
    float s = h ? 1.0f : -1.0f;
    loc->pose = (XrPosef){{0, sinf(0.2f), 0, cosf(0.2f)}, {0.3f * s, 1.2f, -0.4f}};
    if (v && v->type == XR_TYPE_SPACE_VELOCITY) {
        v->velocityFlags = M.vflags;
        v->linearVelocity = (XrVector3f){1.0f, 2.0f, 3.0f};
        v->angularVelocity = (XrVector3f){0.1f, 0.2f, 0.3f};
    }
    return XR_SUCCESS;
}

static void layer_begin(void) { /* what the layer does at SOMVR_SA_BEGIN */
    for (int h = 0; h < 2; h++) {
        atomic_store(&M.live[h], 1);
        IN.xr.grip_space[h] = GRIP(h);
    }
    IN.xr.locate_space = (void *)mock_locate;
    atomic_store_explicit(&IN.xr.ready, 1, memory_order_release);
    SH.xr.space = REF;
    atomic_store(&SH.xr.state, SOMVR_XRS_RUNNING);
}
static void layer_end(void) { /* core stop -> ready 0 -> the spaces */
    somvr_xr_stop(&SH, SOMVR_XRS_DEAD, 1000);
    atomic_store_explicit(&IN.xr.ready, 0, memory_order_release);
    for (int h = 0; h < 2; h++) atomic_store(&M.live[h], 0);
}
static somvr_input_state snap_at(int64_t t) {
    somvr_input_state st;
    memset(&st, 0, sizeof st);
    st.sync_seq = 9, st.time = t, st.flags = SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED | SOMVR_INS_LOCATED;
    for (int h = 0; h < 2; h++) {
        somvr_hand_in *hi = &st.hand[h];
        hi->grip = (somvr_posef){{0, 0, 0, 1}, {h ? 0.25f : -0.25f, 1.0f, -0.5f}};
        hi->lin_vel[0] = 0.5f, hi->ang_vel[1] = 2.0f; /* 0.5 m/s along +X, 2 rad/s about +Y */
        hi->grip_flags = 0xf, hi->vel_flags = 3, hi->active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT;
    }
    return st;
}

static void test_locate(void) {
    somvr_hands_state st;
    memset(&st, 0, sizeof st);
    somvr_hand hd[2];
    somvr_input_state sn = snap_at(1000000000);
    /* before the session: no call, the snapshot */
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(M.calls == 0 && st.guard == 1 && hd[0].src == SOMVR_HSRC_SNAPSHOT && hd[1].src == SOMVR_HSRC_SNAPSHOT,
          "no session: calls %d guard %llu src %d", M.calls, (unsigned long long)st.guard, hd[0].src);
    layer_begin();
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(M.calls == 2 && M.bad == 0 && M.no_velocity == 0 && M.time == 1011111111 && st.located == 2 &&
              atomic_load(&IN.n_hand_locates) == 2,
          "locate: calls %d bad %d novel %d time %lld", M.calls, M.bad, M.no_velocity, (long long)M.time);
    for (int h = 0; h < 2; h++)
        CHECK(hd[h].valid && hd[h].src == SOMVR_HSRC_LOCATED && hd[h].tracked && hd[h].vel_valid == 3 &&
                  NEAR(hd[h].pose.p[0], h ? 0.3 : -0.3, 1e-7) && NEAR(hd[h].pose.q[1], sinf(0.2f), 1e-7) &&
                  hd[h].lin[2] == 3.0f && hd[h].ang[1] == 0.2f && hd[h].time == 1011111111,
              "hand %d located: valid %d src %d p %g", h, hd[h].valid, hd[h].src, hd[h].pose.p[0]);
    /* valid but not tracked */
    M.flags = 3;
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(hd[0].valid && !hd[0].tracked && hd[0].src == SOMVR_HSRC_LOCATED, "valid, not tracked");
    /* orientation only: not usable -> the snapshot */
    M.flags = 1 | 4;
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(hd[0].src == SOMVR_HSRC_SNAPSHOT && hd[1].src == SOMVR_HSRC_SNAPSHOT && atomic_load(&IN.n_hand_fail) == 2,
          "orientation only: src %d fails %llu", hd[0].src, (unsigned long long)atomic_load(&IN.n_hand_fail));
    M.flags = 0xf;
    /* the right hand's locate fails: that hand from the snapshot, the left located */
    M.result[1] = XR_ERROR_RUNTIME_FAILURE;
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(hd[0].src == SOMVR_HSRC_LOCATED && hd[1].src == SOMVR_HSRC_SNAPSHOT && hd[1].result == XR_ERROR_RUNTIME_FAILURE &&
              hd[1].valid,
          "one failure: src %d %d result %d", hd[0].src, hd[1].src, hd[1].result);
    M.result[1] = XR_SUCCESS;
    /* not ready (the spaces not published, or want lacked SPACES): the snapshot, no call */
    int c0 = M.calls;
    atomic_store(&IN.xr.ready, 0);
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(M.calls == c0 && st.notready == 1 && hd[1].src == SOMVR_HSRC_SNAPSHOT, "not ready: %d calls", M.calls - c0);
    atomic_store(&IN.xr.ready, 1);
    /* SOMVR_HANDS=layer: not a single call */
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_LAYER, 1011111111, 22222222, &st, hd);
    CHECK(M.calls == c0 && hd[0].src == SOMVR_HSRC_SNAPSHOT, "layer mode called the runtime");
    /* no snapshot either / too far from the snapshot's time: nothing */
    atomic_store(&IN.xr.ready, 0);
    somvr_hands_locate(&SH, &IN, NULL, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(!hd[0].valid && !hd[1].valid && hd[0].src == SOMVR_HSRC_NONE, "no snapshot: a hand");
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1050000000, 22222222, &st, hd);
    CHECK(!hd[0].valid, "50 ms from the snapshot (2 periods max): a hand");
    atomic_store(&IN.xr.ready, 1);
    layer_end();
    somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 1011111111, 22222222, &st, hd);
    CHECK(M.calls == c0 && hd[0].src == SOMVR_HSRC_SNAPSHOT, "after the teardown: a call");
    printf("locate: the grip spaces in the reference space at the asked time with velocity; untracked; orientation only, "
           "a failure, not ready, layer mode, no session: the snapshot; too old: none\n");
}

static void test_extrapolate(void) {
    somvr_input_state sn = snap_at(2000000000);
    somvr_hand o;
    CHECK(somvr_hand_extrapolate(&sn.hand[1], sn.time, sn.time + 10000000, 22222222, &o) && o.src == SOMVR_HSRC_SNAPSHOT,
          "10 ms ahead");
    CHECK(NEAR(o.pose.p[0], 0.25 + 0.005, 1e-6) && NEAR(o.pose.p[1], 1.0, 1e-7), "p += v dt: %g", o.pose.p[0]);
    float a = 2.0f * 0.01f; /* 2 rad/s for 10 ms about +Y */
    CHECK(NEAR(o.pose.q[1], sinf(0.5f * a), 1e-6) && NEAR(o.pose.q[3], cosf(0.5f * a), 1e-6), "q = exp(w dt/2) q: %g %g",
          o.pose.q[1], o.pose.q[3]);
    /* a rotated start: the base-space rule exp(w dt/2) (x) q, not q (x) exp */
    sn.hand[0].grip.q[0] = sinf(0.5f), sn.hand[0].grip.q[3] = cosf(0.5f); /* 1 rad about +X */
    sn.hand[0].ang_vel[0] = 0, sn.hand[0].ang_vel[1] = 0, sn.hand[0].ang_vel[2] = 3.0f;
    somvr_hand_extrapolate(&sn.hand[0], sn.time, sn.time + 20000000, 22222222, &o);
    sv_quat want = sv_qmul((sv_quat){0, 0, sinf(0.03f), cosf(0.03f)}, (sv_quat){sinf(0.5f), 0, 0, cosf(0.5f)});
    CHECK(NEAR(o.pose.q[0], want.x, 1e-6) && NEAR(o.pose.q[1], want.y, 1e-6) && NEAR(o.pose.q[2], want.z, 1e-6) &&
              NEAR(o.pose.q[3], want.w, 1e-6),
          "base-space rotation");
    CHECK(somvr_hand_extrapolate(&sn.hand[1], sn.time, sn.time - 5000000, 22222222, &o) && NEAR(o.pose.p[0], 0.25 - 0.0025, 1e-6),
          "backwards 5 ms");
    CHECK(!somvr_hand_extrapolate(&sn.hand[1], sn.time, sn.time + 30000000, 22222222, &o), "beyond max_dt");
    sn.hand[1].vel_flags = 0;
    CHECK(somvr_hand_extrapolate(&sn.hand[1], sn.time, sn.time + 10000000, 22222222, &o) && o.pose.p[0] == 0.25f &&
              o.pose.q[3] == 1 && o.vel_valid == 0,
          "no velocity: held");
    sn.hand[1].active = SOMVR_HACT_INPUT;
    CHECK(!somvr_hand_extrapolate(&sn.hand[1], sn.time, sn.time, 22222222, &o), "an inactive pose");
    sn.hand[1].active = SOMVR_HACT_POSE, sn.hand[1].grip_flags = 1;
    CHECK(!somvr_hand_extrapolate(&sn.hand[1], sn.time, sn.time, 22222222, &o), "orientation only");
    printf("extrapolate: p += v dt, exp(w dt/2) (x) q in the base space, backwards, limits, no velocity, inactive\n");
}

/* the mirror: a point moved by v and turned by w for dt in OpenXR, then mirrored = the mirrored point moved by m(v) and
 * turned by w' */
static void test_mirror(void) {
    somvr_hand h;
    memset(&h, 0, sizeof h);
    h.pose = (somvr_posef){{0.1f, 0.2f, 0.3f, 0.927362f}, {0.3f, 1.2f, -0.4f}};
    h.lin[0] = 1, h.lin[1] = -2, h.lin[2] = 3, h.ang[0] = 0.7f, h.ang[1] = -0.4f, h.ang[2] = 1.1f;
    sv_vec3 p, v, w;
    sv_quat q;
    somvr_hand_game(&h, &p, &q, &v, &w);
    CHECK(NEAR(p.x, 30, 1e-4) && NEAR(p.y, 120, 1e-4) && NEAR(p.z, 40, 1e-4), "m(p) %g %g %g", p.x, p.y, p.z);
    CHECK(NEAR(v.x, 100, 1e-4) && NEAR(v.y, -200, 1e-4) && NEAR(v.z, -300, 1e-4), "m(v)");
    CHECK(w.x == -0.7f && w.y == 0.4f && w.z == 1.1f, "w'");
    /* finite difference: rotate the local +Z of the grip by w for dt in XR, mirror, compare with w' applied in game */
    float dt = 1e-3f;
    float an = sqrtf(0.7f * 0.7f + 0.4f * 0.4f + 1.1f * 1.1f), s = sinf(0.5f * an * dt) / an;
    sv_quat dq = {0.7f * s, -0.4f * s, 1.1f * s, cosf(0.5f * an * dt)};
    sv_quat q1 = sv_qmul(dq, sv_quat_of(h.pose.q));
    float q1a[4];
    sv_quat_to(q1, q1a);
    sv_quat g0 = sv_xr_to_game_quat(h.pose.q), g1 = sv_xr_to_game_quat(q1a);
    sv_vec3 f0 = sv_qrot(g0, (sv_vec3){0, 0, 1}), f1 = sv_qrot(g1, (sv_vec3){0, 0, 1});
    sv_vec3 d = {(f1.x - f0.x) / dt, (f1.y - f0.y) / dt, (f1.z - f0.z) / dt};
    sv_vec3 x = {w.y * f0.z - w.z * f0.y, w.z * f0.x - w.x * f0.z, w.x * f0.y - w.y * f0.x}; /* w' x f */
    CHECK(NEAR(d.x, x.x, 2e-3) && NEAR(d.y, x.y, 2e-3) && NEAR(d.z, x.z, 2e-3), "w' x r: fd %g %g %g vs %g %g %g", d.x, d.y,
          d.z, x.x, x.y, x.z);
    /* the rig: the same mapping as the eyes (somvr_rig_eye with this pose as an eye) */
    somvr_rig r;
    somvr_rig_init(&r);
    r.p0 = (sv_vec3){5, 150, -20}, r.yaw0 = 0.4f;
    somvr_base b = {{1000, 50, -300}, 1.3f};
    somvr_rig_cfg cfg = {.layout = SOMVR_LAYOUT_B};
    somvr_view game = {{0, 0, 0}, {0, 0, 0, 1}, 1.5f, 1.0f, 7, 1e5f};
    somvr_fovf fov = {-0.8f, 0.8f, 0.8f, -0.8f};
    somvr_eye_out eo;
    somvr_rig_eye(&r, &cfg, b, &game, &h.pose, &fov, 0, &eo);
    sv_vec3 pw = somvr_rig_point(&r, b, p);
    sv_quat qw = somvr_rig_quat(&r, b, q);
    CHECK(NEAR(pw.x, eo.view.pos[0], 1e-3) && NEAR(pw.y, eo.view.pos[1], 1e-3) && NEAR(pw.z, eo.view.pos[2], 1e-3),
          "rig point %g %g %g vs the eye's %g %g %g", pw.x, pw.y, pw.z, eo.view.pos[0], eo.view.pos[1], eo.view.pos[2]);
    float dotq = qw.x * eo.view.quat[0] + qw.y * eo.view.quat[1] + qw.z * eo.view.quat[2] + qw.w * eo.view.quat[3];
    CHECK(NEAR(fabsf(dotq), 1, 1e-5), "rig quat vs the eye's: dot %g", dotq);
    sv_vec3 vw = somvr_rig_vec(&r, b, (sv_vec3){0, 0, 100});
    CHECK(NEAR(vw.x, 100 * sinf(0.9f), 1e-3) && NEAR(vw.z, 100 * cosf(0.9f), 1e-3) && NEAR(vw.y, 0, 1e-4), "rig vec");
    printf("mirror: m(p), m(v), w' = (-wx,-wy,wz) checked by a finite difference; the rig = the eyes' mapping\n");
}

/* ---- teardown against a game thread ---------------------------------------------------------------------------------- */
static _Atomic int g_stop;
static void *game_thread(void *a) {
    (void)a;
    somvr_hands_state st;
    memset(&st, 0, sizeof st);
    somvr_input_state sn = snap_at(3000000000);
    somvr_hand hd[2];
    while (!atomic_load(&g_stop)) somvr_hands_locate(&SH, &IN, &sn, SOMVR_HANDS_GAME, 3000000000, 22222222, &st, hd);
    return NULL;
}
static void test_teardown(void) {
    M.calls = 0;
    atomic_store(&M.located, 0);
    atomic_store(&M.after_destroy, 0);
    pthread_t t[2];
    for (int i = 0; i < 2; i++) pthread_create(&t[i], NULL, game_thread, NULL);
    for (int round = 0; round < 100; round++) {
        layer_begin();
        usleep(300);
        layer_end();
        usleep(50);
    }
    atomic_store(&g_stop, 1);
    for (int i = 0; i < 2; i++) pthread_join(t[i], NULL);
    CHECK(atomic_load(&M.after_destroy) == 0 && atomic_load(&M.located) > 0, "teardown: %d locates of a destroyed space, %d located",
          atomic_load(&M.after_destroy), atomic_load(&M.located));
    printf("teardown: 100 rounds against 2 game threads, %d hand locates, none of a destroyed space\n", atomic_load(&M.located));
}

int main(void) {
    SH.magic = SOMVR_SHARED_MAGIC, SH.version = SOMVR_SHARED_VERSION, SH.size = sizeof SH;
    atomic_store(&SH.active, 1);
    IN.magic = SOMVR_INPUT_MAGIC, IN.version = SOMVR_INPUT_VERSION, IN.size = sizeof IN;
    IN.want = SOMVR_INW_STATE | SOMVR_INW_SPACES;
    atomic_store(&IN.active, 1);
    test_locate();
    test_extrapolate();
    test_mirror();
    test_teardown();
    printf("test_hands: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
