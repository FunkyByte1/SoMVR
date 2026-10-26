/* test_input.c - the somvr_input ABI (docs/re-xr-input.md 2): layout (printed for the doc), the header check (v1, a
 * longer future object, wrong magic, short size, inactive), somvr_shared still v2, the snapshot seqlock and the haptic
 * requests under a writer and a reader thread, and the teardown order against a game thread that locates the hand
 * spaces through the published xrLocateSpace (the core guard + ready). No runtime: a fake xrLocateSpace. */
#include "../somvr_input.h"

#include <openxr/openxr.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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

static somvr_input_shared IN;
static somvr_shared S;

static void test_header(void) {
    CHECK(!somvr_input_ok(&IN), "a zero object (SOMVR_INPUT=off, inert preload) passes");
    IN.magic = SOMVR_INPUT_MAGIC;
    IN.version = SOMVR_INPUT_VERSION;
    IN.size = sizeof IN;
    CHECK(!somvr_input_ok(&IN), "passes before active");
    atomic_store(&IN.active, 1);
    CHECK(somvr_input_ok(&IN), "a valid v1 object fails");
    IN.version = 2, IN.size = SOMVR_INPUT_V1_SIZE + 64;
    CHECK(somvr_input_ok(&IN), "a later version with a longer object fails (readers take v1's part)");
    IN.version = 0;
    CHECK(!somvr_input_ok(&IN), "version 0 passes");
    IN.version = 1, IN.size = SOMVR_INPUT_V1_SIZE - 8;
    CHECK(!somvr_input_ok(&IN), "a short object passes");
    IN.size = sizeof IN, IN.magic = SOMVR_SHARED_MAGIC;
    CHECK(!somvr_input_ok(&IN), "the core struct's magic passes");
    IN.magic = SOMVR_INPUT_MAGIC;
    CHECK(!somvr_input_ok(NULL), "NULL passes");
    CHECK(somvr_input_ok(&IN), "restore");
    /* the core struct is untouched: v2, 2664 bytes, and its check still demands exactly that */
    S.magic = SOMVR_SHARED_MAGIC, S.version = 2, S.size = sizeof S;
    atomic_store(&S.active, 1);
    CHECK(sizeof(somvr_shared) == 2664 && SOMVR_SHARED_VERSION == 2 && somvr_shared_ok(&S), "somvr_shared v2");
    S.version = 3;
    CHECK(!somvr_shared_ok(&S), "somvr_shared v3 passes a v2 check");
    S.version = 2;
    CHECK(SOMVR_INPUT_MAGIC != SOMVR_SHARED_MAGIC && !memcmp(&(uint64_t){SOMVR_INPUT_MAGIC}, "SOMRINP1", 8),
          "magic \"SOMRINP1\"");
    printf("abi: somvr_input %zu bytes; xr @%zu (ready, profiles, grip_space @%zu, aim_space @%zu, locate_space @%zu, "
           "locate_spaces @%zu), seq @%zu, st @%zu (%zu: hand[2] @+%zu, %zu each: buttons @+%zu, presses @+%zu), "
           "hap[2] @%zu (%zu each), counters @%zu\n",
           sizeof IN, offsetof(somvr_input_shared, xr), offsetof(somvr_input_shared, xr.grip_space),
           offsetof(somvr_input_shared, xr.aim_space), offsetof(somvr_input_shared, xr.locate_space),
           offsetof(somvr_input_shared, xr.locate_spaces), offsetof(somvr_input_shared, seq),
           offsetof(somvr_input_shared, st), sizeof(somvr_input_state), offsetof(somvr_input_state, hand),
           sizeof(somvr_hand_in), offsetof(somvr_hand_in, buttons), offsetof(somvr_hand_in, presses),
           offsetof(somvr_input_shared, hap), sizeof IN.hap[0], offsetof(somvr_input_shared, n_syncs));
}

/* ---- the snapshot: the layer writes values derived from sync_seq; the game thread must never see a mix ---------- */
static _Atomic int g_stop;
static void fill(somvr_input_state *st, uint64_t n) {
    st->sync_seq = n;
    st->time = (int64_t)n * 11111111;
    st->sync_ns = (int64_t)n * 7;
    st->flags = (uint32_t)(n & 7u);
    st->profile[0] = st->profile[1] = (uint16_t)(n % 5u);
    for (int h = 0; h < 2; h++) {
        somvr_hand_in *hi = &st->hand[h];
        for (int i = 0; i < 4; i++) hi->grip.q[i] = hi->aim.q[i] = (float)(n + (uint64_t)i);
        for (int i = 0; i < 3; i++)
            hi->grip.p[i] = hi->aim.p[i] = hi->lin_vel[i] = hi->ang_vel[i] = (float)n * 0.5f + (float)(h + i);
        hi->trigger = hi->squeeze = hi->stick[0] = hi->stick[1] = (float)(n % 1000u);
        hi->buttons = (uint32_t)n;
        hi->grip_flags = hi->aim_flags = hi->vel_flags = hi->active = (uint8_t)n;
        for (int b = 0; b < SOMVR_NBTN; b++) hi->presses[b] = (uint8_t)(n + (uint64_t)b);
    }
}
static int consistent(const somvr_input_state *st) {
    somvr_input_state want;
    memset(&want, 0, sizeof want);
    fill(&want, st->sync_seq);
    return !memcmp(st, &want, sizeof want);
}
static void *snap_writer(void *a) {
    (void)a;
    somvr_input_state st;
    memset(&st, 0, sizeof st);
    for (uint64_t n = 1; n <= 200000; n++) {
        fill(&st, n);
        somvr_input_publish(&IN, &st);
    }
    atomic_store(&g_stop, 1);
    return NULL;
}
static void test_snapshot(void) {
    somvr_input_state st;
    memset(&st, 0, sizeof st);
    fill(&st, 0);
    somvr_input_publish(&IN, &st);
    atomic_store(&g_stop, 0);
    pthread_t t;
    pthread_create(&t, NULL, snap_writer, NULL);
    long reads = 0, torn = 0, failed = 0, backwards = 0;
    uint64_t last = 0;
    while (!atomic_load(&g_stop)) {
        if (!somvr_input_read(&IN, &st)) {
            failed++;
            continue;
        }
        reads++;
        torn += !consistent(&st);
        backwards += st.sync_seq < last;
        last = st.sync_seq;
    }
    pthread_join(t, NULL);
    CHECK(somvr_input_read(&IN, &st) && st.sync_seq == 200000 && consistent(&st), "the last snapshot");
    CHECK(torn == 0 && backwards == 0 && reads > 0, "%ld torn, %ld backwards of %ld reads (%ld failed)", torn, backwards,
          reads, failed);
    printf("snapshot: 200000 writes, %ld reads, %ld torn, %ld retried out\n", reads, torn, failed);
}

/* ---- haptic requests: the game thread posts, the layer takes; each number at most once, never torn ----------- */
static void *hap_poster(void *a) {
    (void)a;
    for (uint32_t i = 1; i <= 200000; i++) {
        uint32_t n = IN.hap[1].req.n + 1u; /* what post will assign */
        somvr_haptic_req r = {.source = n % 5u, .amplitude = (float)(n % 1000u) / 1000.0f,
                              .frequency = (float)n, .duration_ns = (int64_t)n * 1000};
        somvr_haptic_post(&IN, SOMVR_HAND_R, &r);
    }
    atomic_store(&g_stop, 1);
    return NULL;
}
static void test_haptics(void) {
    memset((void *)IN.hap, 0, sizeof IN.hap);
    atomic_store(&IN.n_hap_posts, 0);
    atomic_store(&g_stop, 0);
    pthread_t t;
    pthread_create(&t, NULL, hap_poster, NULL);
    uint32_t last = 0;
    long taken = 0, torn = 0, repeats = 0;
    somvr_haptic_req q;
    for (int done = 0; !done;) {
        done = atomic_load(&g_stop);
        uint32_t before = last;
        if (!somvr_haptic_take(&IN, SOMVR_HAND_R, &last, &q)) continue;
        taken++;
        repeats += q.n <= before;
        torn += q.source != q.n % 5u || q.amplitude != (float)(q.n % 1000u) / 1000.0f || q.frequency != (float)q.n ||
                q.duration_ns != (int64_t)q.n * 1000;
    }
    pthread_join(t, NULL);
    somvr_haptic_take(&IN, SOMVR_HAND_R, &last, &q);
    CHECK(last == 200000 && atomic_load(&IN.n_hap_posts) == 200000, "the last request %u, posts %llu", last,
          (unsigned long long)atomic_load(&IN.n_hap_posts));
    CHECK(torn == 0 && repeats == 0 && taken > 0, "%ld torn, %ld repeated of %ld taken", torn, repeats, taken);
    CHECK(!somvr_haptic_take(&IN, SOMVR_HAND_R, &last, &q) && !somvr_haptic_take(&IN, SOMVR_HAND_L, &(uint32_t){0}, &q),
          "taken again / the other hand");
    printf("haptics: 200000 posts, %ld taken (each once, none torn)\n", taken);
}

/* ---- teardown: the layer's order (core guard stop -> ready = 0 -> destroy the spaces) against a game thread ---- */
#define FAKE_SPACE(i) ((XrSpace)(uintptr_t)(0x7200u + (i)))
static _Atomic int g_live[4], g_bad, g_located;
static XrResult fake_locate(XrSpace sp, XrSpace base, XrTime t, XrSpaceLocation *loc) {
    uintptr_t i = (uintptr_t)sp - 0x7200u;
    (void)base;
    if (i >= 4 || !atomic_load(&g_live[i]) || t <= 0) return atomic_fetch_add(&g_bad, 1), XR_ERROR_HANDLE_INVALID;
    for (volatile int k = 0; k < 500; k++) {}
    if (!atomic_load(&g_live[i])) atomic_fetch_add(&g_bad, 1); /* destroyed while inside */
    loc->locationFlags = 0xf;
    atomic_fetch_add(&g_located, 1);
    return XR_SUCCESS;
}
static void *game_thread(void *a) { /* somvr's input tick: the hands at the frame's time */
    (void)a;
    while (!atomic_load(&g_stop)) {
        if (!somvr_xr_enter(&S)) continue;
        if (atomic_load_explicit(&IN.xr.ready, memory_order_acquire))
            for (int h = 0; h < 2; h++) {
                XrSpaceLocation loc = {.type = XR_TYPE_SPACE_LOCATION};
                ((PFN_xrLocateSpace)IN.xr.locate_space)((XrSpace)(uintptr_t)IN.xr.grip_space[h], XR_NULL_HANDLE, 1000,
                                                       &loc);
            }
        somvr_xr_leave(&S);
    }
    return NULL;
}
static void test_teardown(void) {
    atomic_store(&g_stop, 0);
    pthread_t t[2];
    for (int i = 0; i < 2; i++) pthread_create(&t[i], NULL, game_thread, NULL);
    int quiet = 1;
    for (int round = 0; round < 100; round++) {
        /* the layer at the session's begin: spaces, ready, then the core state */
        for (int i = 0; i < 4; i++) atomic_store(&g_live[i], 1);
        for (int h = 0; h < 2; h++) {
            IN.xr.grip_space[h] = (uint64_t)(uintptr_t)FAKE_SPACE(h);
            IN.xr.aim_space[h] = (uint64_t)(uintptr_t)FAKE_SPACE(2 + h);
        }
        IN.xr.locate_space = (void *)fake_locate;
        atomic_store_explicit(&IN.xr.ready, 1, memory_order_release);
        atomic_store(&S.xr.state, SOMVR_XRS_RUNNING);
        usleep(200);
        /* the teardown */
        quiet &= somvr_xr_stop(&S, SOMVR_XRS_DEAD, 1000);
        atomic_store_explicit(&IN.xr.ready, 0, memory_order_release);
        for (int i = 0; i < 4; i++) atomic_store(&g_live[i], 0);
    }
    atomic_store(&g_stop, 1);
    for (int i = 0; i < 2; i++) pthread_join(t[i], NULL);
    CHECK(quiet && atomic_load(&g_bad) == 0 && atomic_load(&g_located) > 0,
          "teardown: quiet %d, %d locates of a dead space, %d located", quiet, atomic_load(&g_bad),
          atomic_load(&g_located));
    printf("teardown: 100 rounds against 2 game threads, %d hand locates, none of a destroyed space\n",
           atomic_load(&g_located));
}

/* ---- v2: the pointer block (the layer writes it per frame, the game thread reads it) ----------------------------- */
static void *ptr_writer(void *a) {
    (void)a;
    for (uint32_t n = 1; !atomic_load(&g_stop); n++) {
        somvr_ptr_pub p = {.frame = n, .time = (int64_t)n * 7, .mono_ns = (int64_t)n * 3, .quad = n % 3, .flags = n & 31,
                           .hand = (int32_t)(n & 1), .consume = n & 3, .u = (float)n, .v = (float)n, .downs = n, .ups = n,
                           .dist = (float)n, .img_w = n, .img_h = n, .quad_size = {(float)n, (float)n}};
        for (int i = 0; i < 4; i++) p.quad_pose.q[i] = (float)n;
        for (int i = 0; i < 3; i++) p.quad_pose.p[i] = (float)n;
        somvr_ptr_publish(&IN, &p);
    }
    return NULL;
}
static void test_ptr_block(void) {
    CHECK(SOMVR_INPUT_VERSION == 2 && sizeof IN == SOMVR_INPUT_V2_SIZE && offsetof(somvr_input_shared, ptr_seq) == 480,
          "v2: version %u, %zu bytes, ptr_seq @%zu", SOMVR_INPUT_VERSION, sizeof IN, offsetof(somvr_input_shared, ptr_seq));
    IN.version = 2, IN.size = SOMVR_INPUT_V2_SIZE;
    CHECK(somvr_input_ok(&IN) && somvr_input_has_ptr(&IN), "a v2 object has no pointer block");
    IN.size = SOMVR_INPUT_V1_SIZE;
    CHECK(somvr_input_ok(&IN) && !somvr_input_has_ptr(&IN), "a v1-sized object (480 bytes) has a pointer block");
    IN.version = 1, IN.size = SOMVR_INPUT_V2_SIZE;
    CHECK(!somvr_input_has_ptr(&IN), "a version-1 object has a pointer block");
    IN.version = SOMVR_INPUT_VERSION, IN.size = sizeof IN;
    somvr_ptr_pub p = {0}, q;
    CHECK(somvr_ptr_read(&IN, &q) && q.frame == 0, "an unwritten block");
    p.frame = 5, p.quad = SOMVR_PTRQ_MENU, p.flags = SOMVR_PTRF_ON | SOMVR_PTRF_AIM, p.u = 0.25f, p.v = 0.75f;
    somvr_ptr_publish(&IN, &p);
    CHECK(somvr_ptr_read(&IN, &q) && !memcmp(&p, &q, sizeof p) && !(atomic_load(&IN.ptr_seq) & 1), "round trip");
    memset(&p, 0, sizeof p); /* frame 0 until the writer's first block (not counted) */
    somvr_ptr_publish(&IN, &p);
    atomic_store(&g_stop, 0);
    pthread_t t;
    pthread_create(&t, NULL, ptr_writer, NULL);
    int reads = 0, torn = 0;
    for (int i = 0; i < 200000; i++) {
        if (!somvr_ptr_read(&IN, &q)) continue;
        reads++;
        uint32_t n = (uint32_t)q.frame;
        int ok = q.time == (int64_t)n * 7 && q.mono_ns == (int64_t)n * 3 && q.downs == n && q.ups == n && q.img_h == n &&
                 q.u == (float)n && q.quad_size[1] == (float)n && q.quad_pose.p[2] == (float)n && q.flags == (n & 31);
        torn += !ok && n;
    }
    atomic_store(&g_stop, 1);
    pthread_join(t, NULL);
    CHECK(reads > 0 && torn == 0, "pointer block under a writer: %d reads, %d torn", reads, torn);
    printf("abi v2: the pointer block @%zu (%zu bytes: u @+%zu, quad_pose @+%zu), counters @%zu; %d consistent reads under "
           "a writer\n", offsetof(somvr_input_shared, ptr), sizeof(somvr_ptr_pub), offsetof(somvr_ptr_pub, u),
           offsetof(somvr_ptr_pub, quad_pose), offsetof(somvr_input_shared, n_ptr_frames), reads);
}

int main(void) {
    test_header();
    test_ptr_block();
    test_snapshot();
    test_haptics();
    test_teardown();
    printf("input abi: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
