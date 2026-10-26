/* test_logic.c - unit tests of somvr_logic.c (extension merging, format choice, session actions, config, geometry,
 * SOMVR_XR_DUMP's list, pixel order and memory type; the input tables, press counters and haptic requests; the HUD
 * quad's switches and its world-locked pose). */
#include "../somvr_logic.h"

#include <math.h>
#include <openxr/openxr.h>
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

static int count(const somvr_extlist *l, const char *n) {
    int c = 0;
    for (uint32_t i = 0; i < l->count; i++) c += !strcmp(l->names[i], n);
    return c;
}

static void test_extlist(void) {
    static somvr_extlist l;
    const char *app[] = {"VK_KHR_surface", "VK_KHR_xcb_surface", "VK_KHR_get_physical_device_properties2"};
    CHECK(somvr_extl_init(&l, app, 3) == 0 && l.count == 3, "init");
    CHECK(l.names[0] == app[0], "app pointers kept");
    int added = somvr_extl_add_spaced(&l, "VK_KHR_external_fence_capabilities VK_KHR_external_memory_capabilities "
                                          "VK_KHR_external_semaphore_capabilities VK_KHR_get_physical_device_properties2");
    CHECK(added == 3 && l.count == 6, "added %d count %u", added, l.count);
    CHECK(count(&l, "VK_KHR_get_physical_device_properties2") == 1, "duplicate kept");
    CHECK(!strcmp(l.names[3], "VK_KHR_external_fence_capabilities") && !strcmp(l.names[5],
                                                                               "VK_KHR_external_semaphore_capabilities"),
          "order");
    /* repeated, leading/trailing/multiple spaces, duplicates inside the runtime list, empty */
    CHECK(somvr_extl_add_spaced(&l, "  VK_A   VK_A VK_B  ") == 2 && l.count == 8, "spaces/dups: %u", l.count);
    CHECK(somvr_extl_add_spaced(&l, "") == 0 && somvr_extl_add_spaced(&l, NULL) == 0 && l.count == 8, "empty");
    CHECK(somvr_extl_add_spaced(&l, "VK_A VK_KHR_surface") == 0, "all present");
    CHECK(somvr_extl_has(&l, "VK_B") && !somvr_extl_has(&l, "VK_C") && !somvr_extl_has(&l, "VK_"), "has");
    /* a prefix of an existing name is a different name */
    CHECK(somvr_extl_add_spaced(&l, "VK_KHR_surfac") == 1, "prefix");
    /* app list with a NULL entry, empty app list */
    const char *app2[] = {"VK_X", NULL};
    CHECK(somvr_extl_init(&l, app2, 2) == 0 && l.count == 1, "NULL entry skipped");
    CHECK(somvr_extl_init(&l, NULL, 0) == 0 && l.count == 0, "empty app list");
    CHECK(somvr_extl_init(&l, app, SOMVR_EXTL_MAX + 1) == -1, "too many app names");
    /* overflow: names count and buffer */
    somvr_extl_init(&l, NULL, 0);
    char big[20000];
    size_t u = 0;
    for (int i = 0; i < 200; i++) u += (size_t)snprintf(big + u, sizeof big - u, "VK_EXT_%03d ", i);
    CHECK(somvr_extl_add_spaced(&l, big) == -1, "overflow not reported");
}

static void test_unsupported(void) {
    VkExtensionProperties p[3];
    memset(p, 0, sizeof p);
    strcpy(p[0].extensionName, "VK_KHR_external_memory");
    strcpy(p[1].extensionName, "VK_KHR_external_memory_fd");
    strcpy(p[2].extensionName, "VK_KHR_swapchain");
    char out[256];
    CHECK(somvr_first_unsupported("VK_KHR_external_memory VK_KHR_external_memory_fd", p, 3, out, sizeof out) == NULL,
          "all supported");
    const char *m = somvr_first_unsupported("VK_KHR_external_memory  VK_KHR_external_fence_fd VK_X", p, 3, out, sizeof out);
    CHECK(m && !strcmp(m, "VK_KHR_external_fence_fd"), "first missing: %s", m ? m : "(null)");
    m = somvr_first_unsupported("VK_KHR_external_memory_f", p, 3, out, sizeof out);
    CHECK(m && !strcmp(m, "VK_KHR_external_memory_f"), "prefix is not support");
    CHECK(somvr_first_unsupported("", p, 3, out, sizeof out) == NULL && somvr_first_unsupported(NULL, p, 0, out, 8) == NULL,
          "empty");
    m = somvr_first_unsupported("VK_A_VERY_LONG_NAME", p, 0, out, 5);
    CHECK(m && !strcmp(m, "VK_A"), "truncated copy");
}

static void test_format(void) {
    VkFormat f = VK_FORMAT_UNDEFINED;
    int g = -1;
    const int64_t monado[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_UNORM, monado, 5, &f, &g) == SOMVR_XFER_COPY &&
              f == VK_FORMAT_B8G8R8A8_SRGB && g == 1,
          "BGRA unorm game -> BGRA srgb copy (%d)", f);
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_SRGB, monado, 5, &f, &g) == SOMVR_XFER_COPY &&
              f == VK_FORMAT_B8G8R8A8_SRGB && g == 1,
          "BGRA srgb game -> BGRA srgb copy");
    CHECK(somvr_pick_format(VK_FORMAT_R8G8B8A8_UNORM, monado, 5, &f, &g) == SOMVR_XFER_COPY &&
              f == VK_FORMAT_R8G8B8A8_SRGB && g == 1,
          "RGBA unorm game -> RGBA srgb copy");
    const int64_t rgba_srgb[] = {VK_FORMAT_R8G8B8A8_SRGB};
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_SRGB, rgba_srgb, 1, &f, &g) == SOMVR_XFER_BLIT &&
              f == VK_FORMAT_R8G8B8A8_SRGB && g == 1,
          "srgb game, other order: blit, right gamma");
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_UNORM, rgba_srgb, 1, &f, &g) == SOMVR_XFER_BLIT && g == 0,
          "unorm game, other order: blit, gamma off");
    const int64_t unorm_first[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB};
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_SRGB, unorm_first, 2, &f, &g) == SOMVR_XFER_BLIT &&
              f == VK_FORMAT_R8G8B8A8_SRGB && g == 1,
          "right gamma beats a raw copy into unorm");
    const int64_t unorm_only[] = {VK_FORMAT_B8G8R8A8_UNORM};
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_UNORM, unorm_only, 1, &f, &g) == SOMVR_XFER_COPY && g == 0,
          "unorm only: copy, gamma off");
    const int64_t hdr[] = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_A2B10G10R10_UNORM_PACK32};
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_UNORM, hdr, 2, &f, &g) == SOMVR_XFER_NONE, "no 8-bit format");
    CHECK(somvr_pick_format(VK_FORMAT_A2B10G10R10_UNORM_PACK32, monado, 5, &f, &g) == SOMVR_XFER_NONE, "10-bit game");
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_UNORM, monado, 0, &f, &g) == SOMVR_XFER_NONE, "empty list");
    const int64_t both_srgb[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
    CHECK(somvr_pick_format(VK_FORMAT_B8G8R8A8_UNORM, both_srgb, 2, &f, &g) == SOMVR_XFER_COPY, "ties");
}

static void test_session(void) {
    CHECK(somvr_on_session_state(XR_SESSION_STATE_READY, 0) == SOMVR_SA_BEGIN, "ready");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_READY, 1) == SOMVR_SA_NONE, "ready twice");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_SYNCHRONIZED, 1) == SOMVR_SA_NONE, "sync");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_FOCUSED, 1) == SOMVR_SA_NONE, "focused");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_STOPPING, 1) == SOMVR_SA_END, "stopping");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_STOPPING, 0) == SOMVR_SA_NONE, "stopping, not running");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_IDLE, 0) == SOMVR_SA_NONE, "idle");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_EXITING, 0) == SOMVR_SA_LOST, "exiting");
    CHECK(somvr_on_session_state(XR_SESSION_STATE_LOSS_PENDING, 1) == SOMVR_SA_LOST, "loss pending");
}

static void test_config(void) {
    CHECK(somvr_parse_mode(NULL) == SOMVR_MODE_AUTO && somvr_parse_mode("") == SOMVR_MODE_AUTO, "unset");
    CHECK(somvr_parse_mode("1") == SOMVR_MODE_AUTO && somvr_parse_mode("auto") == SOMVR_MODE_AUTO, "auto");
    CHECK(somvr_parse_mode("0") == SOMVR_MODE_OFF && somvr_parse_mode("off") == SOMVR_MODE_OFF, "off");
    CHECK(somvr_parse_mode("force") == SOMVR_MODE_FORCE, "force");
    CHECK(somvr_parse_mode("yes") == SOMVR_MODE_OFF && somvr_parse_mode("2") == SOMVR_MODE_OFF, "unknown = off");
    CHECK(somvr_parse_layer(NULL) == SOMVR_LAYER_QUAD && somvr_parse_layer("proj") == SOMVR_LAYER_PROJ &&
              somvr_parse_layer("x") == SOMVR_LAYER_QUAD,
          "layer");
}

static void test_geometry(void) {
    int32_t r[4];
    somvr_eye_rect(1920, 1080, 0, r);
    CHECK(r[0] == 0 && r[1] == 0 && r[2] == 1920 && r[3] == 1080, "left");
    somvr_eye_rect(1920, 1080, 1, r);
    CHECK(r[0] == 1920 && r[2] == 1920 && r[3] == 1080, "right");
    float s[2];
    somvr_quad_size(1920, 1080, 2.0f, s);
    CHECK(s[0] == 2.0f && s[1] == 1.125f, "quad %f x %f", s[0], s[1]);
    somvr_quad_size(0, 0, 2.0f, s);
    CHECK(s[0] == 2.0f && s[1] == 2.0f, "zero size");
    /* layout A: the halves of one 3664 x 1920 image; the game rounds (int)(f * size + 0.5) */
    const float ra0[4] = {0, 0, 0.5f, 1}, ra1[4] = {0.5f, 0, 1, 1}, full[4] = {0, 0, 1, 1};
    somvr_eye_pixel_rect(3664, 1920, 0, ra0, r);
    CHECK(r[0] == 0 && r[1] == 0 && r[2] == 1832 && r[3] == 1920, "A left %d %d %d %d", r[0], r[1], r[2], r[3]);
    somvr_eye_pixel_rect(3664, 1920, 0, ra1, r);
    CHECK(r[0] == 1832 && r[2] == 1832 && r[3] == 1920, "A right %d %d %d %d", r[0], r[1], r[2], r[3]);
    /* layout B: slot 1 of a 2 x 1832 wide swapchain */
    somvr_eye_pixel_rect(1832, 1920, 1, full, r);
    CHECK(r[0] == 1832 && r[2] == 1832 && r[3] == 1920, "B slot 1 %d %d %d %d", r[0], r[1], r[2], r[3]);
    /* odd width: the halves meet without a gap or an overlap */
    int32_t a[4], b[4];
    somvr_eye_pixel_rect(1921, 1080, 0, ra0, a);
    somvr_eye_pixel_rect(1921, 1080, 0, ra1, b);
    CHECK(a[0] + a[2] == b[0] && b[0] + b[2] == 1921, "odd width %d+%d / %d+%d", a[0], a[2], b[0], b[2]);
    const float bad[4] = {-0.5f, 0.2f, 1.5f, 0.1f};
    somvr_eye_pixel_rect(100, 100, 0, bad, r);
    CHECK(r[0] == 0 && r[2] == 100 && r[3] == 0, "clamped / empty %d %d %d %d", r[0], r[1], r[2], r[3]);
    /* the guard columns right of a mono frame's slot */
    CHECK(somvr_guard_cols(3840, 1920, 0) == SOMVR_GUARD_COLS && SOMVR_GUARD_COLS == 4, "layout B menu: 4 columns");
    CHECK(somvr_guard_cols(1920, 1920, 0) == 0, "one slot: the image's own edge, no guard");
    CHECK(somvr_guard_cols(3840, 1920, 1) == 0, "the last slot: no guard");
    CHECK(somvr_guard_cols(1922, 1920, 0) == 2, "only 2 columns left");
    CHECK(somvr_guard_cols(3840, 0, 0) == 0 && somvr_guard_cols(1000, 1920, 0) == 0, "degenerate sizes");
}

/* SOMVR_XR_PREDICT and the lead estimator (re-xr-pipeline.md 7.1) */
#define MS 1000000ll
static void test_predict(void) {
    int64_t ns = -1;
    int bad = -1;
    CHECK(somvr_parse_predict(NULL, &ns, &bad) == SOMVR_PREDICT_AUTO && !bad && ns == 0 &&
              somvr_parse_predict("", &ns, &bad) == SOMVR_PREDICT_AUTO && somvr_parse_predict("auto", &ns, &bad) == SOMVR_PREDICT_AUTO,
          "default auto");
    CHECK(somvr_parse_predict("off", &ns, &bad) == SOMVR_PREDICT_OFF && somvr_parse_predict("0", &ns, &bad) == SOMVR_PREDICT_OFF &&
              !bad,
          "off");
    CHECK(somvr_parse_predict("33.4", &ns, &bad) == SOMVR_PREDICT_FIXED && ns == 33400000 && !bad, "33.4 ms -> %lld",
          (long long)ns);
    CHECK(somvr_parse_predict("250", &ns, &bad) == SOMVR_PREDICT_FIXED && ns == 250 * MS, "250 ms");
    CHECK(somvr_parse_predict("0.5", &ns, &bad) == SOMVR_PREDICT_FIXED && ns == 500000, "0.5 ms");
    const char *const bads[] = {"250.1", "251", "-5", "1e3", "x", "33,4", "33.", "33 ", ".5", "0.0", "99999999999999"};
    for (size_t i = 0; i < sizeof bads / sizeof bads[0]; i++) {
        int m = somvr_parse_predict(bads[i], &ns, &bad);
        CHECK(m == SOMVR_PREDICT_AUTO && bad && ns == 0, "\"%s\" -> mode %d bad %d", bads[i], m, bad);
    }

    /* the pipeline of headset look 1: waits every 16.67 ms (60 fps), the game thread locates frame N with the latest
     * published time, the frame is submitted 2 waits later. OFF: the error stays -33.3 ms; AUTO: 0 after the first
     * measured frame. */
    for (int mode = SOMVR_PREDICT_OFF; mode <= SOMVR_PREDICT_AUTO; mode++) {
        somvr_lead L;
        somvr_lead_init(&L);
        const int64_t P = 16666667, T0 = 5000 * MS;
        int64_t pub[64], err_last = 0, raw = 0;
        int measured = 0, zero_after = 0;
        for (int w = 0; w < 64; w++) {
            pub[w] = somvr_lead_publish(&L, mode, 0, T0 + w * P); /* the w-th xrWaitFrame */
            if (w < 2) continue;
            int64_t located = pub[w - 2], t_frame = T0 + w * P; /* the record located at wait w-2, ends with wait w */
            int r = somvr_lead_measure(&L, located, t_frame, &raw);
            measured += r == 1;
            CHECK(r == 1 && raw == 2 * P, "mode %d wait %d: measure %d raw %lld", mode, w, r, (long long)raw);
            err_last = located - t_frame;
            if (w >= 5) zero_after += err_last == 0;
        }
        CHECK(measured == 62 && L.lead == 2 * P && L.matched == 62 && !L.missed && !L.rejected,
              "mode %d: measured %d lead %lld", mode, measured, (long long)L.lead);
        if (mode == SOMVR_PREDICT_OFF)
            CHECK(err_last == -2 * P && zero_after == 0, "off: the error %lld must stay -2 periods", (long long)err_last);
        else
            CHECK(err_last == 0 && zero_after == 59, "auto: error %lld, %d of 59 frames at 0", (long long)err_last,
                  zero_after);
    }
    /* fixed: + the given lead, measured all the same */
    somvr_lead L;
    somvr_lead_init(&L);
    CHECK(somvr_lead_publish(&L, SOMVR_PREDICT_FIXED, 30 * MS, 1000 * MS) == 1030 * MS, "fixed lead");
    int64_t raw;
    CHECK(somvr_lead_measure(&L, 1030 * MS, 1033 * MS, &raw) == 1 && raw == 33 * MS && L.lead == 33 * MS,
          "fixed: raw %lld", (long long)raw);
    /* not ours: an extrapolated time (published + period), fake views (0), and one older than the ring */
    CHECK(somvr_lead_measure(&L, 1030 * MS + 11111111, 1050 * MS, &raw) == 0 && raw == 0 && L.missed == 1,
          "extrapolated time measured");
    CHECK(somvr_lead_measure(&L, 0, 1050 * MS, &raw) == 0 && L.missed == 2, "fake views measured");
    for (int i = 1; i <= SOMVR_LEAD_RING; i++) somvr_lead_publish(&L, SOMVR_PREDICT_AUTO, 0, (1000 + 10 * i) * MS);
    CHECK(somvr_lead_measure(&L, 1030 * MS, 1200 * MS, &raw) == 0 && L.missed == 3, "a time older than the ring");
    /* out of range: not used */
    int64_t p = somvr_lead_publish(&L, SOMVR_PREDICT_OFF, 0, 2000 * MS);
    CHECK(somvr_lead_measure(&L, p, 2300 * MS, &raw) == -1 && raw == 300 * MS && L.rejected == 1 && L.lead == 33 * MS,
          "300 ms used");
    p = somvr_lead_publish(&L, SOMVR_PREDICT_OFF, 0, 2400 * MS);
    CHECK(somvr_lead_measure(&L, p, 2399 * MS, &raw) == -1 && L.rejected == 2, "a negative lead used");
    /* the median: one hitch does not move the estimate, a lasting change does once it holds the middle of the 9 (here
     * after 4: the hitch's 120 ms, still in the window, sits above it too) */
    somvr_lead_init(&L);
    int64_t t = 1000 * MS;
    for (int i = 0; i < 9; i++, t += 20 * MS) {
        p = somvr_lead_publish(&L, SOMVR_PREDICT_AUTO, 0, t);
        somvr_lead_measure(&L, p, t + (i == 4 ? 120 : 33) * MS, &raw);
    }
    CHECK(L.lead == 33 * MS && L.nwin == 9, "one 120 ms hitch moved the median to %lld", (long long)L.lead);
    int moved_at = -1;
    for (int i = 0; i < 9; i++, t += 20 * MS) {
        p = somvr_lead_publish(&L, SOMVR_PREDICT_AUTO, 0, t);
        somvr_lead_measure(&L, p, t + 50 * MS, &raw);
        if (moved_at < 0 && L.lead == 50 * MS) moved_at = i;
    }
    CHECK(moved_at == 3 && L.lead == 50 * MS, "a lasting 50 ms lead taken after %d measurements", moved_at + 1);
    /* 60 fps on a 72 Hz display: the frame's time is 2 or 3 periods of 13.9 ms after the base wait */
    somvr_lead_init(&L);
    const int64_t P72 = 13888889;
    int64_t worst = 0, pub[64];
    for (int w = 0; w < 120; w++) {
        int64_t tw = 1000 * MS + (w * 6 / 5) * P72; /* 60 waits per 72 refreshes */
        pub[w % 64] = somvr_lead_publish(&L, SOMVR_PREDICT_AUTO, 0, tw);
        if (w < 2) continue;
        int64_t tf = 1000 * MS + (w * 6 / 5) * P72, located = pub[(w - 2) % 64];
        somvr_lead_measure(&L, located, tf, &raw);
        int64_t e = located - tf;
        if (w > 12 && (e < 0 ? -e : e) > worst) worst = e < 0 ? -e : e;
    }
    CHECK(worst <= P72 + 1 && L.lead >= 2 * P72 && L.lead <= 3 * P72, "72 Hz: worst error %lld ns, lead %lld",
          (long long)worst, (long long)L.lead);
    printf("predict: 60 fps / 2 waits deep: off -33.3 ms, auto 0 after one frame; 72 Hz display: |error| <= %.1f ms, "
           "lead %.1f ms\n", (double)worst / 1e6, (double)L.lead / 1e6);
}

static void test_submit(void) {
    CHECK(somvr_submit_decide(0, 7, 1, 1) == SOMVR_SUBMIT_MONO, "no tag: mono");
    CHECK(somvr_submit_decide(1, 0, 0, 1) == SOMVR_SUBMIT_MONO, "frame 0: mono");
    CHECK(somvr_submit_decide(1, 7, 1, 0) == SOMVR_SUBMIT_STEREO, "record: stereo");
    CHECK(somvr_submit_decide(1, 7, 0, 1) == SOMVR_SUBMIT_REUSE, "mismatch after stereo: reuse");
    CHECK(somvr_submit_decide(1, 7, 0, 0) == SOMVR_SUBMIT_MONO, "mismatch, nothing to reuse: mono");
    somvr_eye_rec e;
    memset(&e, 0, sizeof e);
    e.fov = (somvr_fovf){-0.9f, 0.7f, 0.8f, -0.95f};
    e.fov_cover = (somvr_fovf){-0.9f, 0.9f, 0.95f, -0.95f};
    CHECK(somvr_submit_fov(&e, 3, 1).r == 0.7f && somvr_submit_fov(&e, 1, 1).r == 0.9f &&
              somvr_submit_fov(&e, 1, 0).r == 0.7f && somvr_submit_fov(&e, 0, 0).u == 0.95f,
          "fov choice by the applied bits");
    CHECK(somvr_parse_space(NULL) == 0 && somvr_parse_space("stage") == 1 && somvr_parse_space("local") == 0 &&
              somvr_parse_space("x") == 0,
          "space");
}

static void test_dump(void) {
    uint64_t at[SOMVR_DUMP_MAX];
    CHECK(somvr_parse_dump(NULL, at) == 0 && somvr_parse_dump("", at) == 0, "unset");
    CHECK(somvr_parse_dump("60,300,700", at) == 3 && at[0] == 60 && at[1] == 300 && at[2] == 700, "run 5's list");
    CHECK(somvr_parse_dump("2", at) == 1 && at[0] == 2, "one");
    CHECK(somvr_parse_dump("1,2,3,999999999999", at) == 4 && at[3] == 999999999999ull, "four, 12 digits");
    CHECK(somvr_parse_dump("m30", at) == 1 && at[0] == (30 | SOMVR_DUMP_MONO), "a mono frame");
    CHECK(somvr_parse_dump("600,m30,m1,2", at) == 4 && at[0] == 600 && at[1] == (30 | SOMVR_DUMP_MONO) &&
              at[2] == (1 | SOMVR_DUMP_MONO) && at[3] == 2,
          "mixed list");
    /* the swapchain kind (SOMVR_EYE_SIZE's switch, re-xr-pipeline.md 9): sN = the N-th XR swapchain's first frame */
    CHECK(somvr_parse_dump("s2,s3,s4,300", at) == 4 && at[0] == (2 | SOMVR_DUMP_SC) && at[1] == (3 | SOMVR_DUMP_SC) &&
              at[2] == (4 | SOMVR_DUMP_SC) && at[3] == 300 && !(at[0] & SOMVR_DUMP_MONO),
          "swapchain dumps");
    CHECK(somvr_parse_dump("1,2,3,4,5,6,7,m8", at) == 8 && at[7] == (8 | SOMVR_DUMP_MONO), "eight, the most");
    const char *const bad[] = {"1,2,3,4,5,6,7,8,9", "0", "60,", ",60", "60,,300", "6 0", " 60", "-1", "+1", "60,x", "1e3",
                               "1234567890123", "60;300", "m", "m0", "mm3", "3m", "M3", "m 3", "m3,",
                               "m1,2,3,m4,5,6,7,8,9", "s", "s0", "ss2", "ms2", "sm2", "S2", "2s", "s2,"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(somvr_parse_dump(bad[i], at) == -1, "\"%s\" accepted", bad[i]);
    int s = -1;
    CHECK(somvr_dump_order(VK_FORMAT_R8G8B8A8_SRGB, &s) == 0 && s == 1, "RGBA sRGB");
    CHECK(somvr_dump_order(VK_FORMAT_B8G8R8A8_UNORM, &s) == 1 && s == 0, "BGRA unorm");
    CHECK(somvr_dump_order(VK_FORMAT_B8G8R8A8_SRGB, &s) == 1 && s == 1, "BGRA sRGB");
    CHECK(somvr_dump_order(VK_FORMAT_R16G16B16A16_SFLOAT, &s) == -1, "not 8-bit");
    const uint8_t px[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t o[6];
    somvr_dump_row(px, o, 2, 0);
    CHECK(!memcmp(o, (const uint8_t[]){1, 2, 3, 5, 6, 7}, 6), "RGBA row");
    somvr_dump_row(px, o, 2, 1);
    CHECK(!memcmp(o, (const uint8_t[]){3, 2, 1, 7, 6, 5}, 6), "BGRA row");
    /* like RADV on a dGPU: VRAM, GTT write-combined, VRAM through the BAR, GTT cached, GTT cached but not coherent */
    VkPhysicalDeviceMemoryProperties mp;
    memset(&mp, 0, sizeof mp);
    const VkMemoryPropertyFlags dl = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                hc = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ca = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    const VkMemoryPropertyFlags types[5] = {dl, hv | hc, dl | hv | hc, hv | hc | ca, hv | ca};
    mp.memoryTypeCount = 5;
    for (int i = 0; i < 5; i++) mp.memoryTypes[i].propertyFlags = types[i];
    CHECK(somvr_pick_host_memory(&mp, 0x1f) == 3, "cached GTT first");
    CHECK(somvr_pick_host_memory(&mp, 0x17) == 1, "then GTT, before the BAR");
    CHECK(somvr_pick_host_memory(&mp, 0x15) == 2, "the BAR if nothing else");
    CHECK(somvr_pick_host_memory(&mp, 0x11) == -1, "device-local / not coherent only");
    CHECK(somvr_pick_host_memory(&mp, 0x0) == -1, "no bits");
}


/* ---- OpenXR input (docs/re-xr-input.md 1.2-1.6) ---- */
static const char *const HP[2] = {"left", "right"};
static int has_binding(const somvr_profile_def *pd, int act, int hand, const char *path) {
    for (uint32_t i = 0; i < pd->n; i++)
        if (pd->b[i].act == act && pd->b[i].hand == hand && (!path || !strcmp(pd->b[i].path, path))) return 1;
    return 0;
}
static void test_input_tables(void) {
    CHECK(somvr_parse_input(NULL) == SOMVR_INPUT_ON && somvr_parse_input("") == SOMVR_INPUT_ON &&
              somvr_parse_input("on") == SOMVR_INPUT_ON && somvr_parse_input("1") == SOMVR_INPUT_ON,
          "SOMVR_XR_INPUT default on");
    CHECK(somvr_parse_input("log") == SOMVR_INPUT_LOG && somvr_parse_input("off") == SOMVR_INPUT_OFF &&
              somvr_parse_input("0") == SOMVR_INPUT_OFF && somvr_parse_input("yes") == SOMVR_INPUT_OFF,
          "SOMVR_XR_INPUT log/off/unknown");
    /* actions: names unique and valid, the menu on the left only, a button bit for every BOOLEAN action */
    uint32_t bits = 0;
    for (int a = 0; a < SOMVR_NACT; a++) {
        const somvr_action_def *d = &somvr_actions[a];
        CHECK(d->name && strlen(d->name) < 64 && strspn(d->name, "abcdefghijklmnopqrstuvwxyz0123456789_-.") ==
                  strlen(d->name) && d->loc && *d->loc, "action %d: name", a);
        for (int b = 0; b < a; b++)
            CHECK(strcmp(d->name, somvr_actions[b].name) && strcmp(d->loc, somvr_actions[b].loc), "action %d = %d", a, b);
        CHECK((d->type == XR_ACTION_TYPE_BOOLEAN_INPUT) == (d->button != 0), "action %s: button bit", d->name);
        CHECK(!(bits & d->button), "action %s: bit 0x%x twice", d->name, d->button);
        bits |= d->button;
        CHECK(d->hands == (a == SOMVR_ACT_MENU ? 1u : 3u), "action %s: hands 0x%x", d->name, d->hands);
    }
    CHECK(bits == 0x3f, "button bits 0x%x", bits);
    CHECK(somvr_actions[SOMVR_ACT_GRIP].type == XR_ACTION_TYPE_POSE_INPUT &&
              somvr_actions[SOMVR_ACT_AIM].type == XR_ACTION_TYPE_POSE_INPUT &&
              somvr_actions[SOMVR_ACT_STICK].type == XR_ACTION_TYPE_VECTOR2F_INPUT &&
              somvr_actions[SOMVR_ACT_TRIGGER].type == XR_ACTION_TYPE_FLOAT_INPUT &&
              somvr_actions[SOMVR_ACT_HAPTIC].type == XR_ACTION_TYPE_VIBRATION_OUTPUT &&
              somvr_actions[SOMVR_ACT_LOWER].button == SOMVR_BTN_LOWER &&
              somvr_actions[SOMVR_ACT_MENU].button == SOMVR_BTN_MENU,
          "action types / bits");
    /* every action and hand has a Touch binding (the user's Quest 2 under WiVRn) */
    const somvr_profile_def *t = &somvr_profiles[0], *ix = &somvr_profiles[1], *sm = &somvr_profiles[2];
    CHECK(t->prof == SOMVR_PROF_TOUCH && ix->prof == SOMVR_PROF_INDEX && sm->prof == SOMVR_PROF_SIMPLE, "order");
    for (int a = 0; a < SOMVR_NACT; a++)
        for (int h = 0; h < 2; h++)
            if (somvr_actions[a].hands >> h & 1u)
                CHECK(has_binding(t, a, h, NULL), "touch: no binding of %s for %s", somvr_actions[a].name, HP[h]);
    /* each binding: a hand the action has, a path of the right kind for its type */
    int total = 0;
    for (int p = 0; p < SOMVR_NPROF; p++) {
        const somvr_profile_def *pd = &somvr_profiles[p];
        total += (int)pd->n;
        for (uint32_t i = 0; i < pd->n; i++) {
            const somvr_binding_def *b = &pd->b[i];
            const somvr_action_def *d = &somvr_actions[b->act];
            size_t L = strlen(b->path);
            int pose = L > 5 && !strcmp(b->path + L - 5, "/pose"), hap = !strcmp(b->path, "output/haptic");
            int vec = !strcmp(b->path, "input/thumbstick") || !strcmp(b->path, "input/trackpad");
            CHECK(b->act < SOMVR_NACT && b->hand < 2 && (d->hands >> b->hand & 1u), "%s %u: hand", pd->name, i);
            CHECK((d->type == XR_ACTION_TYPE_POSE_INPUT) == pose && (d->type == XR_ACTION_TYPE_VIBRATION_OUTPUT) == hap &&
                      (d->type == XR_ACTION_TYPE_VECTOR2F_INPUT) == vec && !strncmp(b->path, hap ? "output/" : "input/",
                                                                                     hap ? 7 : 6),
                  "%s: %s -> %s: the path does not fit the type", pd->name, d->name, b->path);
            for (uint32_t j = 0; j < i; j++)
                CHECK(!(pd->b[j].act == b->act && pd->b[j].hand == b->hand && !strcmp(pd->b[j].path, b->path)),
                      "%s: %s %s twice", pd->name, d->name, b->path);
        }
    }
    CHECK(t->n == 29 && ix->n == 29 && sm->n == 10 && total == 68, "bindings %u + %u + %u", t->n, ix->n, sm->n);
    /* the spec's paths where the profiles differ */
    CHECK(has_binding(t, SOMVR_ACT_LOWER, 0, "input/x/click") && has_binding(t, SOMVR_ACT_LOWER, 1, "input/a/click") &&
              has_binding(t, SOMVR_ACT_UPPER, 0, "input/y/click") && has_binding(t, SOMVR_ACT_UPPER, 1, "input/b/click") &&
              has_binding(t, SOMVR_ACT_MENU, 0, "input/menu/click") &&
              has_binding(t, SOMVR_ACT_THUMB_TOUCH, 0, "input/thumbrest/touch") &&
              has_binding(t, SOMVR_ACT_THUMB_TOUCH, 1, "input/b/touch") && !has_binding(t, SOMVR_ACT_MENU, 1, NULL),
          "touch paths");
    for (uint32_t i = 0; i < t->n; i++)
        CHECK(strcmp(t->b[i].path, "input/system/click"), "touch: the right system button is the runtime's");
    CHECK(has_binding(ix, SOMVR_ACT_LOWER, 0, "input/a/click") && has_binding(ix, SOMVR_ACT_UPPER, 1, "input/b/click") &&
              has_binding(ix, SOMVR_ACT_MENU, 0, "input/system/click") &&
              has_binding(ix, SOMVR_ACT_THUMB_TOUCH, 1, "input/trackpad/touch") &&
              has_binding(ix, SOMVR_ACT_STICK, 1, "input/thumbstick") &&
              has_binding(ix, SOMVR_ACT_SQUEEZE, 0, "input/squeeze/value"),
          "index paths");
    CHECK(has_binding(sm, SOMVR_ACT_TRIGGER, 0, "input/select/click") &&
              has_binding(sm, SOMVR_ACT_UPPER, 1, "input/menu/click") &&
              has_binding(sm, SOMVR_ACT_MENU, 0, "input/menu/click") && !has_binding(sm, SOMVR_ACT_UPPER, 0, NULL) &&
              !has_binding(sm, SOMVR_ACT_SQUEEZE, 0, NULL) && has_binding(sm, SOMVR_ACT_HAPTIC, 1, "output/haptic"),
          "simple paths");
    CHECK(somvr_prof_from_path("/interaction_profiles/oculus/touch_controller") == SOMVR_PROF_TOUCH &&
              somvr_prof_from_path("/interaction_profiles/valve/index_controller") == SOMVR_PROF_INDEX &&
              somvr_prof_from_path("/interaction_profiles/khr/simple_controller") == SOMVR_PROF_SIMPLE &&
              somvr_prof_from_path("/interaction_profiles/htc/vive_controller") == SOMVR_PROF_OTHER &&
              somvr_prof_from_path("") == SOMVR_PROF_NONE && somvr_prof_from_path(NULL) == SOMVR_PROF_NONE,
          "profile paths");
}
static void test_input_merge(void) {
    uint8_t pr[SOMVR_NBTN] = {0};
    uint32_t b = 0;
    b = somvr_in_merge(pr, b, SOMVR_BTN_LOWER);                   /* press */
    b = somvr_in_merge(pr, b, SOMVR_BTN_LOWER);                   /* held over the next sync */
    b = somvr_in_merge(pr, b, 0);                                 /* release */
    CHECK(b == 0 && pr[0] == 1, "a press and its release count once: %u", pr[0]);
    b = somvr_in_merge(pr, b, SOMVR_BTN_LOWER | SOMVR_BTN_UPPER); /* two at once */
    CHECK(b == 3 && pr[0] == 2 && pr[1] == 1 && pr[2] == 0, "two buttons: %u %u", pr[0], pr[1]);
    b = somvr_in_merge(pr, b, SOMVR_BTN_UPPER);                   /* one released, one held */
    CHECK(pr[0] == 2 && pr[1] == 1, "a release counted");
    b = somvr_in_merge(pr, b, 0x80u);                             /* bit 7 has a counter, bit 8 none */
    b = somvr_in_merge(pr, b, 0x100u);
    CHECK(pr[7] == 1 && b == 0x100u, "bit 7 %u", pr[7]);
    for (int i = 0; i < 300; i++) b = somvr_in_merge(pr, b, i & 1 ? SOMVR_BTN_STICK : 0);
    CHECK(pr[2] == 150 % 256, "150 presses: %u", pr[2]);
    for (int i = 0; i < 300; i++) b = somvr_in_merge(pr, b, i & 1 ? SOMVR_BTN_STICK : 0);
    CHECK(pr[2] == 300 % 256, "the counter wraps at 256: %u", pr[2]);
}
static void test_input_haptics(void) {
    float amp, freq;
    int64_t dur;
    somvr_haptic_req q = {.n = 1, .amplitude = 0.5f, .frequency = 160, .duration_ns = 200000000};
    CHECK(somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_APPLY && amp == 0.5f && freq == 160 &&
              dur == 200000000, "apply %g %g %lld", amp, freq, (long long)dur);
    q.amplitude = 0;
    CHECK(somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_STOP, "amplitude 0 = stop");
    q.amplitude = -0.1f;
    CHECK(somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_STOP, "negative = stop");
    q.amplitude = __builtin_nanf("");
    CHECK(somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_STOP, "NaN = stop");
    q.amplitude = 3, q.frequency = -5, q.duration_ns = 0;
    CHECK(somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_APPLY && amp == 1.0f &&
              freq == XR_FREQUENCY_UNSPECIFIED && dur == XR_MIN_HAPTIC_DURATION,
          "clamped: %g %g %lld", amp, freq, (long long)dur);
    q.frequency = __builtin_nanf(""), q.duration_ns = -1;
    CHECK(somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_APPLY && freq == 0 && dur == -1, "NaN frequency");
    /* the request numbers through the ABI: each new one taken once */
    static somvr_input_shared in;
    uint32_t last = 0;
    somvr_haptic_req got;
    CHECK(!somvr_haptic_take(&in, 1, &last, &got), "nothing posted yet");
    somvr_haptic_req r = {.source = SOMVR_HAP_TEST, .amplitude = 0.7f, .duration_ns = 50000000};
    CHECK(somvr_haptic_post(&in, 1, &r) == 1 && somvr_haptic_take(&in, 1, &last, &got) && got.n == 1 && last == 1 &&
              got.amplitude == 0.7f, "first request");
    CHECK(!somvr_haptic_take(&in, 1, &last, &got), "a repeated request number = no call");
    CHECK(!somvr_haptic_take(&in, 0, &(uint32_t){0}, &got), "the other hand");
    somvr_haptic_post(&in, 1, &r);
    somvr_haptic_post(&in, 1, &r);
    CHECK(somvr_haptic_take(&in, 1, &last, &got) && got.n == 3 && !somvr_haptic_take(&in, 1, &last, &got),
          "two posts between presents: the newest, once");
    in.hap[1].req.n = UINT32_MAX;
    CHECK(somvr_haptic_post(&in, 1, &r) == 1, "the number skips 0 when it wraps");
}

/* SOMVR_XR_HUD, _ALPHA, _QUAD and the world-locked pose (H11, docs/re-xr-pipeline.md 8) */
static int near7(const somvr_posef *p, float qx, float qy, float qz, float qw, float x, float y, float z) {
    const float w[7] = {qx, qy, qz, qw, x, y, z}, g[7] = {p->q[0], p->q[1], p->q[2], p->q[3], p->p[0], p->p[1], p->p[2]};
    for (int i = 0; i < 7; i++)
        if (!(fabsf(g[i] - w[i]) < 1e-5f)) return 0;
    return 1;
}
static void test_hud(void) {
    int bad = 7;
    CHECK(somvr_parse_hud(NULL, &bad) == SOMVR_HUD_WORLD && !bad && somvr_parse_hud("", &bad) == SOMVR_HUD_WORLD &&
              somvr_parse_hud("world", &bad) == SOMVR_HUD_WORLD && !bad && somvr_parse_hud("head", &bad) == SOMVR_HUD_HEAD &&
              somvr_parse_hud("off", &bad) == SOMVR_HUD_OFF && somvr_parse_hud("0", &bad) == SOMVR_HUD_OFF && !bad,
          "SOMVR_XR_HUD");
    CHECK(somvr_parse_hud("Head", &bad) == SOMVR_HUD_WORLD && bad && somvr_parse_hud("1", &bad) == SOMVR_HUD_WORLD && bad,
          "SOMVR_XR_HUD: unknown = world, flagged");
    CHECK(somvr_parse_hud_alpha(NULL, &bad) == SOMVR_HUD_STRAIGHT && !bad &&
              somvr_parse_hud_alpha("straight", &bad) == SOMVR_HUD_STRAIGHT && !bad &&
              somvr_parse_hud_alpha("premul", &bad) == SOMVR_HUD_PREMUL && !bad &&
              somvr_parse_hud_alpha("premultiplied", &bad) == SOMVR_HUD_STRAIGHT && bad,
          "SOMVR_XR_HUD_ALPHA");
    float q[3] = {0, 0, 0};
    somvr_parse_hud_quad(NULL, q, &bad);
    CHECK(!bad && q[0] == 1.5f && q[1] == 1.2f && q[2] == 0.1f, "SOMVR_XR_HUD_QUAD default %g %g %g", q[0], q[1], q[2]);
    somvr_parse_hud_quad("2.5,1.6,-0.2", q, &bad);
    CHECK(!bad && fabsf(q[0] - 2.5f) < 1e-6f && fabsf(q[1] - 1.6f) < 1e-6f && fabsf(q[2] + 0.2f) < 1e-6f, "2.5,1.6,-0.2");
    somvr_parse_hud_quad("1,0.75,0", q, &bad);
    CHECK(!bad && q[0] == 1 && q[1] == 0.75f && q[2] == 0, "1,0.75,0");
    static const char *const nope[] = {"1,2", "1,2,3,4", "0.2,1,0", "11,1,0", "1,0.05,0", "1,1,3", "1,1,-2.5", "a,b,c",
                                       "1.,1,0", ".5,1,0", "1;1;0", "1,1,0 ", "-1,1,0", "1,,0", "1234567,1,0"};
    for (size_t i = 0; i < sizeof nope / sizeof nope[0]; i++) {
        q[0] = q[1] = q[2] = 9;
        somvr_parse_hud_quad(nope[i], q, &bad);
        CHECK(bad && q[0] == 1.5f && q[1] == 1.2f && q[2] == 0.1f, "SOMVR_XR_HUD_QUAD=%s: bad %d -> %g %g %g", nope[i], bad,
              q[0], q[1], q[2]);
    }
    /* the world pose: the eyes' midpoint, dist along the levelled forward, drop below, yaw only */
    somvr_posef e[2] = {{{0, 0, 0, 1}, {-0.032f, 1.6f, 0.2f}}, {{0, 0, 0, 1}, {0.032f, 1.6f, 0.2f}}}, o;
    somvr_hud_world_pose(e, 1.5f, 0.1f, &o);
    CHECK(near7(&o, 0, 0, 0, 1, 0, 1.5f, -1.3f), "ahead: %g %g %g", o.p[0], o.p[1], o.p[2]);
    const float s45 = 0.38268343f, c45 = 0.92387953f; /* a 45 deg turn to the left about +Y */
    for (int k = 0; k < 2; k++) e[k].q[1] = s45, e[k].q[3] = c45;
    somvr_hud_world_pose(e, 2.0f, 0.0f, &o);
    CHECK(near7(&o, 0, s45, 0, c45, -1.41421356f, 1.6f, 0.2f - 1.41421356f), "45 deg left: %g %g %g / %g %g", o.p[0],
          o.p[1], o.p[2], o.q[1], o.q[3]);
    for (int k = 0; k < 2; k++) e[k].q[1] = 1, e[k].q[3] = 0; /* facing +Z (turned around) */
    somvr_hud_world_pose(e, 1.0f, 0.0f, &o);
    CHECK(fabsf(o.p[2] - 1.2f) < 1e-5f && fabsf(o.p[0]) < 1e-5f && fabsf(fabsf(o.q[1]) - 1) < 1e-5f && fabsf(o.q[3]) < 1e-5f,
          "turned around: %g %g / %g %g", o.p[0], o.p[2], o.q[1], o.q[3]);
    /* pitch and roll do not tilt it: 30 deg down about +X, then a roll about -Z: still level, straight ahead */
    const float s15 = 0.25881905f, c15 = 0.96592583f;
    for (int k = 0; k < 2; k++) e[k].q[0] = -s15, e[k].q[1] = 0, e[k].q[2] = 0, e[k].q[3] = c15;
    somvr_hud_world_pose(e, 1.5f, 0.1f, &o);
    CHECK(near7(&o, 0, 0, 0, 1, 0, 1.5f, -1.3f), "pitched down: %g %g %g / %g %g", o.p[0], o.p[1], o.p[2], o.q[1], o.q[3]);
    for (int k = 0; k < 2; k++) e[k].q[0] = 0, e[k].q[1] = 0, e[k].q[2] = s15, e[k].q[3] = c15;
    somvr_hud_world_pose(e, 1.5f, 0.1f, &o);
    CHECK(near7(&o, 0, 0, 0, 1, 0, 1.5f, -1.3f), "rolled: %g %g %g", o.p[0], o.p[1], o.p[2]);
    /* straight down after a 90 deg left turn: the head's up vector points ahead (-X); straight up: it points back */
    const float h = 0.70710678f;
    for (int k = 0; k < 2; k++) e[k].q[0] = -0.5f, e[k].q[1] = 0.5f, e[k].q[2] = 0.5f, e[k].q[3] = 0.5f;
    somvr_hud_world_pose(e, 1.0f, 0.0f, &o);
    CHECK(near7(&o, 0, h, 0, h, -1.0f, 1.6f, 0.2f), "straight down, turned left: %g %g %g / %g %g", o.p[0], o.p[1], o.p[2],
          o.q[1], o.q[3]);
    for (int k = 0; k < 2; k++) e[k].q[0] = h, e[k].q[1] = 0, e[k].q[2] = 0, e[k].q[3] = h; /* 90 deg up */
    somvr_hud_world_pose(e, 1.0f, 0.0f, &o);
    CHECK(near7(&o, 0, 0, 0, 1, 0, 1.6f, -0.8f), "straight up: %g %g %g / %g %g", o.p[0], o.p[1], o.p[2], o.q[1], o.q[3]);
    for (int k = 0; k < 2; k++) e[k].q[0] = NAN, e[k].q[1] = 0, e[k].q[2] = 0, e[k].q[3] = 1;
    somvr_hud_world_pose(e, 1.0f, 0.0f, &o);
    CHECK(near7(&o, 0, 0, 0, 1, 0, 1.6f, -0.8f), "NaN orientation: straight ahead in the space");
}

int main(void) {
    test_extlist();
    test_unsupported();
    test_format();
    test_session();
    test_config();
    test_geometry();
    test_predict();
    test_submit();
    test_dump();
    test_input_tables();
    test_input_merge();
    test_input_haptics();
    test_hud();
    printf("logic: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
