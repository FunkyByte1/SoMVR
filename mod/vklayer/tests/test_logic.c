/* test_logic.c - unit tests of somvr_logic.c (extension merging, format choice, session actions, config, geometry). */
#include "../somvr_logic.h"

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
}

int main(void) {
    test_extlist();
    test_unsupported();
    test_format();
    test_session();
    test_config();
    test_geometry();
    printf("logic: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
