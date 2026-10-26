/* test_views.c - the XR views provider (somvr_views.c) against a mock runtime: an xrLocateViews double that checks the
 * call (types, session, space, view configuration, capacity, the time), returns scripted results, and flags any call
 * after "xrDestroySession". Also the fake views. */
#include "../somvr_views.h"

#include <math.h>
#include <openxr/openxr.h>
#include <pthread.h>
#include <stdio.h>
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

#define SESS ((XrSession)(uintptr_t)0x2222)
#define SPACE ((XrSpace)(uintptr_t)0x3333)

static struct {
    XrResult result;
    uint32_t count;
    XrViewStateFlags flags;
    float ipd;
    int calls, bad, destroyed, after_destroy;
    XrTime last_time;
    char first_bad[200];
} R = {.result = XR_SUCCESS, .count = 2, .flags = 0xf, .ipd = 0.064f};

static void bad(const char *m) {
    if (!R.bad++) snprintf(R.first_bad, sizeof R.first_bad, "%s", m);
}

static XrResult XRAPI_CALL mock_locate(XrSession s, const XrViewLocateInfo *li, XrViewState *vs, uint32_t cap,
                                       uint32_t *n, XrView *v) {
    __atomic_add_fetch(&R.calls, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&R.destroyed, __ATOMIC_SEQ_CST)) __atomic_add_fetch(&R.after_destroy, 1, __ATOMIC_SEQ_CST);
    if (s != SESS) bad("session");
    if (!li || li->type != XR_TYPE_VIEW_LOCATE_INFO || li->space != SPACE ||
        li->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        bad("locate info");
    if (!vs || vs->type != XR_TYPE_VIEW_STATE) bad("view state type");
    if (cap != 2 || !v || v[0].type != XR_TYPE_VIEW || v[1].type != XR_TYPE_VIEW) bad("views");
    R.last_time = li->displayTime;
    *n = R.count;
    vs->viewStateFlags = R.flags;
    if (XR_FAILED(R.result)) return R.result;
    for (uint32_t k = 0; k < cap && k < 2; k++) {
        float t = (float)(li->displayTime % 1000000) * 1e-6f; /* poses move with the time */
        v[k].pose = (XrPosef){{0, sinf(0.1f * t), 0, cosf(0.1f * t)}, {k ? 0.5f * R.ipd : -0.5f * R.ipd, 1.6f + t, 0}};
        v[k].fov = (XrFovf){k ? -0.80f : -0.90f, k ? 0.90f : 0.80f, 0.85f, -0.95f};
    }
    return R.result;
}

static somvr_shared S;

static void running(void) {
    S.xr.session = (uint64_t)(uintptr_t)SESS;
    S.xr.space = (uint64_t)(uintptr_t)SPACE;
    S.xr.locate_views = (void *)mock_locate;
    atomic_store(&S.xr.state, SOMVR_XRS_RUNNING);
}

static void test_basic(void) {
    somvr_views_state st;
    memset(&st, 0, sizeof st);
    somvr_views v;
    CHECK(!somvr_views_locate(&S, &st, &v) && R.calls == 0, "called while not running");
    CHECK(!somvr_views_locate(NULL, &st, &v), "NULL struct");
    running();
    CHECK(!somvr_views_locate(&S, &st, &v) && R.calls == 0, "called without a predicted time");
    somvr_time_publish(&S, 5000000000, 11111111);
    CHECK(somvr_views_locate(&S, &st, &v) && v.fresh && !v.extrapolated && v.time == 5000000000 &&
              R.last_time == 5000000000,
          "fresh locate: ok %d time %lld", v.ok, (long long)v.time);
    CHECK(R.bad == 0, "mock: %s", R.first_bad);
    CHECK(NEAR(v.pose[0].p[0], -0.032, 1e-6) && NEAR(v.pose[1].p[0], 0.032, 1e-6) && v.fov[0].l == -0.90f &&
              v.fov[1].r == 0.90f,
          "views copied");
    /* no new xrWaitFrame: one period on, flagged */
    CHECK(somvr_views_locate(&S, &st, &v) && v.extrapolated && v.time == 5000000000 + 11111111, "extrapolated %lld",
          (long long)v.time);
    CHECK(somvr_views_locate(&S, &st, &v) && v.extrapolated && v.time == 5000000000 + 2 * 11111111, "twice");
    /* the layer waited again: its time, even if it is earlier than the extrapolation (monotonic clamp) */
    somvr_time_publish(&S, 5000000000 + 11111111, 11111111);
    CHECK(somvr_views_locate(&S, &st, &v) && !v.extrapolated && v.time == 5000000000 + 2 * 11111111,
          "clamped to monotonic: %lld", (long long)v.time);
    somvr_time_publish(&S, 5000000000 + 3 * 11111111, 11111111);
    CHECK(somvr_views_locate(&S, &st, &v) && !v.extrapolated && v.time == 5000000000 + 3 * 11111111, "fresh again");
    CHECK(atomic_load(&S.n_locates) == 5, "locate counter %llu", (unsigned long long)atomic_load(&S.n_locates));
    CHECK(atomic_load(&S.xr.users) == 0, "users left at %u", atomic_load(&S.xr.users));

    /* failures: last good views for up to SOMVR_VIEWS_MAX_REUSE calls, then not ok */
    somvr_views good = v;
    R.result = XR_ERROR_SESSION_LOST;
    int ok_reused = 0;
    for (int i = 0; i < SOMVR_VIEWS_MAX_REUSE + 5; i++) {
        somvr_time_publish(&S, 6000000000 + (int64_t)i * 11111111, 11111111);
        if (somvr_views_locate(&S, &st, &v) && !v.fresh && !memcmp(v.pose, good.pose, sizeof v.pose)) ok_reused++;
    }
    CHECK(ok_reused == SOMVR_VIEWS_MAX_REUSE && !v.ok, "reused %d times, last ok %d", ok_reused, v.ok);
    R.result = XR_SUCCESS;
    CHECK(somvr_views_locate(&S, &st, &v) && v.fresh && st.fails == 0, "recovers");
    /* orientation not valid: a failure */
    R.flags = XR_VIEW_STATE_POSITION_VALID_BIT;
    CHECK(somvr_views_locate(&S, &st, &v) && !v.fresh, "invalid orientation used as fresh");
    /* position not valid: fresh orientation, last good positions */
    good = v;
    R.flags = XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    somvr_time_publish(&S, 9000000000, 11111111);
    CHECK(somvr_views_locate(&S, &st, &v) && v.fresh && v.pos_reused && !memcmp(v.pose[0].p, good.pose[0].p, 12),
          "position from the last good views");
    R.flags = 0xf;
    /* wrong view count */
    R.count = 1;
    CHECK(somvr_views_locate(&S, &st, &v) && !v.fresh, "one view accepted");
    R.count = 2;
    printf("provider: fresh/extrapolated/clamped/reuse (%d)/invalid flags/one view: as designed; %d runtime calls\n",
           SOMVR_VIEWS_MAX_REUSE, R.calls);
}

/* game thread locating views while the layer tears the session down: no call after the destroy */
static _Atomic int g_stop;
static void *game_thread(void *a) {
    somvr_views_state st;
    memset(&st, 0, sizeof st);
    somvr_views v;
    int *n = a;
    while (!atomic_load(&g_stop)) {
        somvr_views_locate(&S, &st, &v);
        (*n)++;
    }
    return NULL;
}
static void test_teardown_race(void) {
    int loops = 0;
    for (int round = 0; round < 100; round++) {
        R.destroyed = 0;
        running();
        pthread_t t;
        atomic_store(&g_stop, 0);
        pthread_create(&t, NULL, game_thread, &loops);
        usleep(300);
        int quiet = somvr_xr_stop(&S, SOMVR_XRS_DEAD, 1000); /* the layer's teardown */
        CHECK(quiet, "round %d: not quiescent", round);
        __atomic_store_n(&R.destroyed, 1, __ATOMIC_SEQ_CST); /* xrDestroySpace/xrDestroySession */
        usleep(200);
        atomic_store(&g_stop, 1);
        pthread_join(t, NULL);
    }
    CHECK(R.after_destroy == 0, "%d xrLocateViews calls after the destroy", R.after_destroy);
    printf("teardown race: 100 rounds, %d provider calls, none after the destroy\n", loops);
}

static void test_fake(void) {
    somvr_view g = {{0, 0, 0}, {0, 0, 0, 1}, 1.26945388f, 0.785398185f, 27.5f, 100000};
    somvr_views v;
    somvr_views_fake(&g, SOMVR_LAYOUT_A, 0.064f, &v);
    CHECK(v.ok && NEAR(v.pose[0].p[0], -0.032, 1e-7) && NEAR(v.pose[1].p[0], 0.032, 1e-7), "fake eyes");
    CHECK(NEAR(tanf(v.fov[0].r), 0.5 * tan(0.5 * 1.26945388), 1e-6) && NEAR(tanf(v.fov[0].u), tan(0.5 * 0.785398185), 1e-6),
          "fake fov A: half width");
    somvr_views_fake(&g, SOMVR_LAYOUT_B, 0.064f, &v);
    CHECK(NEAR(tanf(v.fov[1].r), tan(0.5 * 1.26945388), 1e-6) && v.fov[1].l == -v.fov[1].r, "fake fov B: full width");
}

/* somvr_views_peek_time (SOMVR_ARMS, phase 2): the time the next locate asks for, without touching the state */
static void test_peek(void) {
    somvr_shared s2;
    memset(&s2, 0, sizeof s2);
    somvr_views_state st;
    memset(&st, 0, sizeof st);
    CHECK(somvr_views_peek_time(&s2, &st) == 0 && somvr_views_peek_time(NULL, &st) == 0, "a time without a publish");
    s2.xr.session = (uint64_t)(uintptr_t)SESS, s2.xr.space = (uint64_t)(uintptr_t)SPACE;
    s2.xr.locate_views = (void *)mock_locate;
    atomic_store(&s2.xr.state, SOMVR_XRS_RUNNING);
    /* fresh, extrapolated twice, a late publish (clamped), fresh again, a 2-period step */
    const int64_t pub[] = {7000000000, 0, 0, 7000000000 + 11111111, 7000000000 + 3 * 11111111, 0, 7000000000 + 6 * 11111111};
    int same = 0, untouched = 0, n = (int)(sizeof pub / sizeof pub[0]);
    for (int i = 0; i < n; i++) {
        if (pub[i]) somvr_time_publish(&s2, pub[i], 11111111);
        somvr_views_state before = st;
        int64_t pk = somvr_views_peek_time(&s2, &st), pk2 = somvr_views_peek_time(&s2, &st);
        untouched += !memcmp(&before, &st, sizeof st) && pk == pk2;
        somvr_views v;
        same += somvr_views_locate(&s2, &st, &v) && v.time == pk;
    }
    CHECK(same == n && untouched == n, "peek = the next locate's time %d/%d, state untouched %d/%d", same, n, untouched, n);
}

int main(void) {
    test_basic();
    test_teardown_race();
    test_fake();
    test_peek();
    CHECK(R.bad == 0, "mock runtime: %d bad calls, first: %s", R.bad, R.first_bad);
    printf("test_views: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
