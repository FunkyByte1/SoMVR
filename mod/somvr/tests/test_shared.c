/* test_shared.c - the somvr_shared ABI: layout (printed for the doc), header check, the record seqlock under a writer
 * and a reader thread, present tags (synchronous and FIFO), and the in-use guard against a concurrent teardown; the
 * HUD object somvr_hud (somvr_hud.h): its header check, the HUD info seqlock, its per-present tags next to the core's. */
#include "../somvr_hud.h"
#include "../somvr_shared.h"

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

static somvr_shared S;

static void test_header(void) {
    CHECK(!somvr_shared_ok(&S), "a zero struct (inert preload) passes the check");
    S.magic = SOMVR_SHARED_MAGIC;
    S.version = SOMVR_SHARED_VERSION;
    S.size = sizeof S;
    CHECK(!somvr_shared_ok(&S), "passes before active");
    atomic_store(&S.active, 1);
    CHECK(somvr_shared_ok(&S), "a valid header fails");
    S.version = SOMVR_SHARED_VERSION + 1;
    CHECK(!somvr_shared_ok(&S), "another version passes");
    S.version = 1; /* the ABI before the eye images */
    CHECK(!somvr_shared_ok(&S), "version 1 passes");
    S.version = SOMVR_SHARED_VERSION;
    S.size = 100;
    CHECK(!somvr_shared_ok(&S), "another size passes");
    S.size = sizeof S;
    CHECK(!somvr_shared_ok(NULL), "NULL passes");
    printf("abi: somvr_shared %zu bytes; xr @%zu, rec_pending @%zu, rec[%d] @%zu (%zu each), tag_seq @%zu, tag[%d] "
           "(%zu each), present_tag @%zu, eyes @%zu\n",
           sizeof S, offsetof(somvr_shared, xr), offsetof(somvr_shared, rec_pending), SOMVR_NREC,
           offsetof(somvr_shared, rec), sizeof(somvr_rec), offsetof(somvr_shared, tag_seq), SOMVR_NTAG,
           sizeof(somvr_tag), offsetof(somvr_shared, present_tag), offsetof(somvr_shared, eyes));
}

/* ---- seqlock: the writer fills a record with values derived from the frame id; the reader must never see a mix ---- */
static _Atomic int g_stop;
static void fill(somvr_rec *r, uint64_t f) {
    r->frame = f;
    r->display_time = (int64_t)f * 1000;
    r->nslots = (uint32_t)f;
    for (int e = 0; e < 2; e++) {
        for (int i = 0; i < 4; i++) r->eye[e].som_quat[i] = (float)(f + (uint64_t)i);
        r->eye[e].hash = (uint32_t)(f * 7u + (uint64_t)e);
        r->eye[e].fov.l = -(float)f;
    }
}
static int consistent(const somvr_rec *r) {
    uint64_t f = r->frame;
    if (r->display_time != (int64_t)f * 1000 || r->nslots != (uint32_t)f) return 0;
    for (int e = 0; e < 2; e++) {
        for (int i = 0; i < 4; i++)
            if (r->eye[e].som_quat[i] != (float)(f + (uint64_t)i)) return 0;
        if (r->eye[e].hash != (uint32_t)(f * 7u + (uint64_t)e) || r->eye[e].fov.l != -(float)f) return 0;
    }
    return 1;
}
static void *writer(void *a) {
    (void)a;
    for (uint64_t f = 1; f <= 200000; f++) {
        somvr_rec *r = somvr_rec_slot(&S, 1); /* always the same slot: maximum contention */
        somvr_seq_wbegin(&r->seq);
        fill(r, f);
        somvr_seq_wend(&r->seq);
    }
    atomic_store(&g_stop, 1);
    return NULL;
}
static void test_seqlock(void) {
    pthread_t t;
    pthread_create(&t, NULL, writer, NULL);
    uint64_t reads = 0, torn = 0, misses = 0;
    while (!atomic_load(&g_stop)) {
        somvr_rec c;
        somvr_rec *r = somvr_rec_slot(&S, 1);
        if (!somvr_seq_read(&r->seq, &c, r, sizeof c)) {
            misses++;
            continue;
        }
        if (!c.frame) continue; /* the writer has not written yet */
        reads++;
        torn += !consistent(&c);
    }
    pthread_join(t, NULL);
    CHECK(torn == 0, "%llu torn reads of %llu", (unsigned long long)torn, (unsigned long long)reads);
    CHECK(reads > 0, "the reader never got a copy");
    somvr_rec c;
    CHECK(somvr_rec_read(&S, 200000 - 200000 % SOMVR_NREC + 1, &c) == 0, "a record read under the wrong frame id");
    somvr_rec *r = somvr_rec_slot(&S, 9);
    somvr_seq_wbegin(&r->seq);
    fill(r, 9);
    somvr_seq_wend(&r->seq);
    atomic_store(&r->applied, 3);
    CHECK(somvr_rec_read(&S, 9, &c) && c.frame == 9 && c.applied == 3 && consistent(&c), "record 9");
    CHECK(!somvr_rec_read(&S, 1, &c), "record 1 is overwritten by 9 (same slot) but still found");
    somvr_seq_wbegin(&r->seq);
    CHECK(!somvr_rec_read(&S, 9, &c), "a record being written was returned");
    somvr_seq_wend(&r->seq);
    CHECK(!somvr_rec_read(&S, 0, &c), "frame 0 found");
    printf("seqlock: %llu consistent reads (%llu gave up after 64 tries) against 200000 writes to the same record, 0 torn\n",
           (unsigned long long)reads, (unsigned long long)misses);
}

/* ---- tags ---------------------------------------------------------------------------------------------------- */
static void test_tags(void) {
    uint64_t last = 0;
    somvr_tag t;
    CHECK(!somvr_tag_for_present(&S, 100, &last, &t), "a tag before any push");
    /* synchronous: the render thread is inside Present (present_tid = its tid) */
    uint64_t s1 = somvr_tag_push(&S, 41, 0, 100);
    atomic_store(&S.present_tag, s1);
    atomic_store(&S.present_tid, 100);
    CHECK(somvr_tag_for_present(&S, 100, &last, &t) && t.frame == 41 && t.seq == s1 && last == s1, "sync tag");
    CHECK(!somvr_tag_for_present(&S, 100, &last, &t), "the same tag twice");
    atomic_store(&S.present_tid, 0);
    /* a skipped present: the next synchronous tag is still the right one */
    somvr_tag_push(&S, 42, 0, 100);
    uint64_t s3 = somvr_tag_push(&S, 43, 0, 100);
    atomic_store(&S.present_tag, s3);
    atomic_store(&S.present_tid, 100);
    CHECK(somvr_tag_for_present(&S, 100, &last, &t) && t.frame == 43, "sync after a skipped present: frame %llu",
          (unsigned long long)t.frame);
    atomic_store(&S.present_tid, 0);
    /* async submit thread (tid 200): oldest unconsumed first */
    somvr_tag_push(&S, 44, 0, 100);
    somvr_tag_push(&S, 45, 1, 100);
    CHECK(somvr_tag_for_present(&S, 200, &last, &t) && t.frame == 44, "fifo 1: %llu", (unsigned long long)t.frame);
    CHECK(somvr_tag_for_present(&S, 200, &last, &t) && t.frame == 45 && t.slot == 1, "fifo 2");
    CHECK(!somvr_tag_for_present(&S, 200, &last, &t), "fifo empty");
    /* the consumer fell behind by more than the ring: it resynchronises to the oldest tag still there */
    for (int i = 0; i < 20; i++) somvr_tag_push(&S, (uint64_t)(100 + i), 0, 100);
    CHECK(somvr_tag_for_present(&S, 200, &last, &t) && t.frame == 100 + 19 - (SOMVR_NTAG - 1), "resync: %llu",
          (unsigned long long)t.frame);
    CHECK(somvr_tag_get(&S, atomic_load(&S.tag_seq), &t) && t.frame == 119 && t.eyes == 0 && t.eye_gen == 0, "newest");
    CHECK(!somvr_tag_get(&S, 1, &t), "an overwritten tag");
    /* layout B: the eye bits and the generation travel with the tag; the eye info round-trips through its seqlock */
    uint64_t se = somvr_tag_push_eyes(&S, 200, 1, 100, 3, 7);
    CHECK(somvr_tag_get(&S, se, &t) && t.frame == 200 && t.slot == 1 && t.eyes == 3 && t.eye_gen == 7, "eye tag %u %u",
          t.eyes, t.eye_gen);
    somvr_tag_push(&S, 201, 0, 100);
    CHECK(somvr_tag_get(&S, atomic_load(&S.tag_seq), &t) && t.eyes == 0 && t.eye_gen == 0, "a plain tag after an eye tag");
    somvr_eyes_info in = {7, 1832, 1920, 28, {0xE000, 0xE001}}, out;
    memset(&out, 0, sizeof out);
    somvr_eyes_publish(&S, &in);
    CHECK(somvr_eyes_read(&S, &out) && !memcmp(&in, &out, sizeof in) && !(atomic_load(&S.eyes.seq) & 1), "eyes info");
}

/* ---- the in-use guard: callers never run after the stop returned 1 ------------------------------------------------ */
static _Atomic int g_destroyed, g_after, g_calls;
static void *caller(void *a) {
    (void)a;
    while (!atomic_load(&g_stop)) {
        if (!somvr_xr_enter(&S)) continue;
        if (atomic_load(&g_destroyed)) atomic_fetch_add(&g_after, 1);
        atomic_fetch_add(&g_calls, 1);
        for (volatile int i = 0; i < 200; i++) {}
        if (atomic_load(&g_destroyed)) atomic_fetch_add(&g_after, 1);
        somvr_xr_leave(&S);
    }
    return NULL;
}
static void test_guard(void) {
    atomic_store(&g_stop, 0);
    for (int round = 0; round < 200; round++) {
        atomic_store(&g_destroyed, 0);
        atomic_store(&S.xr.state, SOMVR_XRS_RUNNING);
        pthread_t t[3];
        for (int i = 0; i < 3; i++) pthread_create(&t[i], NULL, caller, NULL);
        usleep(200);
        CHECK(somvr_xr_stop(&S, SOMVR_XRS_DEAD, 1000), "stop not quiescent");
        atomic_store(&g_destroyed, 1); /* "xrDestroySession" */
        usleep(100);
        atomic_store(&g_stop, 1);
        for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);
        atomic_store(&g_stop, 0);
    }
    CHECK(atomic_load(&g_after) == 0, "%d calls ran after the session was destroyed", atomic_load(&g_after));
    CHECK(atomic_load(&g_calls) > 0, "no calls ran");
    CHECK(!somvr_xr_enter(&S), "enter while dead");
    atomic_store(&S.xr.users, 1); /* a stuck caller: the stop gives up */
    CHECK(!somvr_xr_stop(&S, SOMVR_XRS_DEAD, 5), "stop with a stuck caller returned quiescent");
    atomic_store(&S.xr.users, 0);
    printf("guard: 200 teardown rounds against 3 caller threads, %d calls, none after the destroy\n",
           atomic_load(&g_calls));
}

static void test_time(void) {
    somvr_time tm;
    CHECK(somvr_time_read(&S, &tm) && tm.waits == 0, "empty timing");
    somvr_time_publish(&S, 123456789, 11111111);
    CHECK(somvr_time_read(&S, &tm) && tm.t == 123456789 && tm.period == 11111111 && tm.waits == 1, "timing");
}

/* ---- somvr_hud (H11): a third object; somvr_shared stays v2, its tags unchanged ---------------------------------- */
static somvr_shared HS;
static somvr_hud_shared HUD;
static void test_hud(void) {
    CHECK(!somvr_hud_ok(&HUD) && !somvr_hud_ok(NULL), "a zero HUD object (SOMVR_HUD off) passes the check");
    HUD.magic = SOMVR_HUD_MAGIC;
    HUD.version = SOMVR_HUD_VERSION;
    HUD.size = sizeof HUD;
    CHECK(!somvr_hud_ok(&HUD), "passes before active");
    atomic_store(&HUD.active, 1);
    CHECK(somvr_hud_ok(&HUD), "a valid HUD header fails");
    HUD.version = 2;
    CHECK(!somvr_hud_ok(&HUD), "another HUD version passes");
    HUD.version = SOMVR_HUD_VERSION, HUD.size = 100;
    CHECK(!somvr_hud_ok(&HUD), "another HUD size passes");
    HUD.size = sizeof HUD;
    HUD.magic = SOMVR_SHARED_MAGIC;
    CHECK(!somvr_hud_ok(&HUD), "somvr_shared's magic passes");
    HUD.magic = SOMVR_HUD_MAGIC;
    CHECK(!memcmp(&HUD.magic, "SOMRHUD1", 8) && sizeof HUD == 216 && sizeof(somvr_shared) == 2664, "magic / sizes");
    /* the info round-trips through its own seqlock */
    somvr_hud_info in = {3, 1920, 1080, 28, 0xE00A}, out;
    memset(&out, 0, sizeof out);
    somvr_hud_publish(&HUD, &in);
    CHECK(somvr_hud_read(&HUD, &out) && !memcmp(&in, &out, sizeof in) && !(atomic_load(&HUD.seq) & 1), "HUD info");
    /* per present: H9 puts the HUD entry for the seq it is about to push, then pushes the core tag */
    somvr_hud_tag ht;
    CHECK(!somvr_hud_tag_get(&HUD, 0, &ht) && !somvr_hud_tag_get(&HUD, 1, &ht), "an entry before any put");
    uint64_t want = atomic_load(&HS.tag_seq) + 1;
    somvr_hud_tag_put(&HUD, want, 3, SOMVR_HUDK_WORLD);
    uint64_t seq = somvr_tag_push_eyes(&HS, 77, 1, 100, 3, 9);
    somvr_tag t;
    CHECK(seq == want && somvr_tag_get(&HS, seq, &t) && t.eyes == 3 && t.eye_gen == 9 && somvr_hud_tag_get(&HUD, seq, &ht) &&
              ht.gen == 3 && ht.kind == SOMVR_HUDK_WORLD && atomic_load(&ht.seq) == seq,
          "the HUD entry of tag %llu: gen %u kind %u", (unsigned long long)seq, ht.gen, ht.kind);
    /* a mono present: an entry naming no HUD; a present H9 wrote no entry for: none (the ring holds an older seq) */
    somvr_hud_tag_put(&HUD, seq + 1, 0, SOMVR_HUDK_NONE);
    somvr_tag_push(&HS, 0, 0, 100);
    CHECK(somvr_hud_tag_get(&HUD, seq + 1, &ht) && ht.gen == 0 && ht.kind == SOMVR_HUDK_NONE, "a mono present's entry");
    for (uint64_t k = 2; k < 2 + SOMVR_NTAG; k++) somvr_hud_tag_put(&HUD, seq + k, (uint32_t)k, SOMVR_HUDK_VIDEO);
    CHECK(!somvr_hud_tag_get(&HUD, seq, &ht) && !somvr_hud_tag_get(&HUD, seq + 1, &ht), "overwritten entries");
    CHECK(somvr_hud_tag_get(&HUD, seq + 2 + SOMVR_NTAG - 1, &ht) && ht.gen == 1 + SOMVR_NTAG && ht.kind == SOMVR_HUDK_VIDEO,
          "the newest entry: gen %u", ht.gen);
    CHECK(!somvr_hud_tag_get(&HUD, seq + 2 + SOMVR_NTAG, &ht), "an entry not written yet");
    printf("abi: somvr_hud %zu bytes; info @%zu, expect_w @%zu, tag[%d] @%zu (%zu each), counters @%zu; somvr_shared stays "
           "v%u %zu bytes\n",
           sizeof HUD, offsetof(somvr_hud_shared, info), offsetof(somvr_hud_shared, expect_w), SOMVR_NTAG,
           offsetof(somvr_hud_shared, tag), sizeof(somvr_hud_tag), offsetof(somvr_hud_shared, copies),
           SOMVR_SHARED_VERSION, sizeof(somvr_shared));
}

int main(void) {
    test_header();
    test_seqlock();
    test_tags();
    test_guard();
    test_time();
    test_hud();
    printf("test_shared: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
