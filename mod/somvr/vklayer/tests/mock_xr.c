/* mock_xr.c - test double, built as a shared object with soname libvulkan.so.1:
 *   - a fake OpenXR loader + runtime (xrGetInstanceProcAddr) that checks the call protocol the layer must follow
 *   - a fake Vulkan loader trampoline (vkGetInstanceProcAddr: vkEnumeratePhysicalDevices, vkGetPhysicalDeviceProperties)
 *     that the layer finds with dlopen("libvulkan.so.1", RTLD_NOLOAD)
 * No GPU, no runtime, no files. Behaviour from the environment:
 *   MOCKXR_FAIL=xrName[:N]  the Nth call (default 1st) of xrName fails (xrGetSystem: FORM_FACTOR_UNAVAILABLE)
 *   MOCKXR_HANG_MS=n        xrCreateInstance sleeps n ms
 *   MOCKXR_EXIT_AT=n        after n xrEndFrame calls the runtime stops the session (STOPPING, then IDLE + EXITING)
 *   MOCKVK_TRAMP_NAME=s     the GPU name the trampoline reports for the runtime's VkPhysicalDevice
 *   MOCKXR_FORMATS=a,b,..   the swapchain formats offered (VkFormat numbers; default the usual five)
 *   MOCKXR_NO_STAGE=1       xrEnumerateReferenceSpaces offers VIEW and LOCAL only
 *   MOCKXR_FLOOR_CM=n       the LOCAL origin n cm above the STAGE floor (default 152): a STAGE space made next to the
 *                           LOCAL one (SOMVR_XR_FLOOR) is handle 0x3334, located against LOCAL only
 *   MOCKXR_RECENTER_AT=n    after n xrEndFrame calls: one XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING
 *   MOCKXR_FOCUS_AT=n       the running session goes SYNCHRONIZED -> VISIBLE -> FOCUSED once n xrEndFrame calls are
 *                           done (default 0: right after xrBeginSession)
 *   MOCKXR_UNFOCUS_AT=n     back to VISIBLE after n xrEndFrame calls (a system menu)
 *   MOCKXR_PROFILE=touch|index|simple|none  the controllers' device profile (default touch); the current profile of
 *                           a hand is the device's if its suggestions were taken, else simple if taken, else none
 *   MOCKXR_PROFILE_OFF_AT=n the controllers go to sleep after n xrEndFrame calls (profile none at the next sync)
 *   MOCKXR_REJECT_HUD=n     the n-th xrEndFrame with two layers returns XR_ERROR_LAYER_INVALID and leaves the frame
 *                           as it was (not ended, nothing consumed), as a runtime that validates the layers first
 *   MOCKXR_REJECT_PTR=n     likewise the n-th xrEndFrame with a layer showing the 64x32 swapchain (the pointer's)
 *   MOCKXR_REJECT_CF=n      likewise the n-th xrEndFrame with a layer showing the 512x528 swapchain (comfort's)
 *   MOCKXR_REJECT_SC=WxH    every xrEndFrame with a layer showing a live W x H swapchain returns XR_ERROR_LAYER_INVALID
 *                           (a runtime that never takes that quad; counted, mockxr_rejects)
 *   MOCKXR_REJECT_VM=n      likewise the n-th xrEndFrame with a layer showing the 512x768 swapchain (the VR menu's, v2)
 *   MOCKXR_EXTS=a[,b]       SoM loading env: instance extensions offered besides XR_KHR_vulkan_enable (at most 4); an
 *                           enabled one not offered is a violation; an equirect2 layer needs
 *                           XR_KHR_composition_layer_equirect2 enabled
 *   MOCKXR_REJECT_ENV=n     likewise the n-th xrEndFrame with an equirect2 layer or a quad showing the environment's
 *                           cube atlas (SOMVR_ENV_CUBE_W x the image height)
 *   MOCKXR_MAX_LAYERS=n     build 8: xrGetSystemProperties reports maxLayerCount n (default 16); more in a frame is a
 *                           violation (LAYER_LIMIT_EXCEEDED)
 *   MOCKXR_UNLOAD_AT_EXIT=1 build 16: the first xrCreateInstance registers an exit handler that "unloads the runtime"
 *                           (as the OpenXR loader's static destructor dlcloses it at exit): every XR call after it is
 *                           counted (mockxr_after_unload) and a violation - the real runtime's code would be unmapped
 * Up to MOCK_NSC (8) swapchains live at once (the eye/game one, the HUD's (H11), the pointer's, comfort's, the fade's,
 * the VR menu's, the environment's sky and kept frame): each has its own acquire/wait/release state; the first free
 * slot is taken (0x4444 + k, images 0x5000 + 0x100 k + j; a static one has one image and is acquired once). Two VIEW
 * reference spaces may be made too (the HUD's, comfort's: 0x3335, 0x3336). Up to MOCK_MAX_LAYERS (16, the spec's
 * minimum every runtime takes) layers per frame, more is a violation; all MOCK_NQ (16) are recorded; layer 0 a quad,
 * a projection or an equirect2 layer, the others quads or projections.
 * xrLocateViews may be called from any thread (the game thread's provider): it counts atomically and flags calls
 * without a live session; its poses follow the display time, its fovs are asymmetric per eye. So may xrLocateSpace
 * (the game thread's hand locate): it flags dead or unknown spaces.
 * The action API checks the order the spec demands (actions and suggestions before the attach, the attach once, a sync
 * only when attached, state reads with the action's type and subaction paths) and every binding path against the
 * profile's components: a path the profile lacks or a type mismatch is a violation (a table bug in the layer).
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mock_common.h"
#include "../somvr_env.h" /* SoM loading env: the cube atlas' size (MOCKXR_REJECT_ENV) */

#define EXPORT __attribute__((visibility("default")))

static struct {
    const char *names[64];
    int calls[64], first_at[64], seq;
    int nnames;
    int viol;
    char first[512];
    const char *fail_name;
    int fail_at, hang_ms, exit_at;
    int unload_exit, unload_reg; /* MOCKXR_UNLOAD_AT_EXIT */
    _Atomic int unloaded, after_unload;
    int64_t formats[8];
    uint32_t nformats;
    const char *tramp_name;
    int inst_live, sess_live, space_live, view_live; /* view_live: bit k = VIEW space 0x3335 + k lives */
    int floor_live, floor_locates, floor_cm;         /* the STAGE space next to LOCAL (0x3334) */
    int got_req, gave_pd, running, ready_sent, exit_stage, frame_begun, waited, ends;
    int no_stage, recenter_at, recenter_sent, space_type, reject_hud_at, hud_ends, reject_ptr_at, ptr_ends;
    int reject_cf_at, cf_ends, reject_vm_at, vm_ends;
    int wait_to_at, wait_to_n, wait_calls, wait_timeouts; /* build 27: MOCKXR_WAIT_TIMEOUT=N:K */
    uint32_t reject_sc_w, reject_sc_h; /* MOCKXR_REJECT_SC: every frame showing a swapchain of that size */
    int reject_sc_n;
    int reject_env_at, env_ends;          /* SoM loading env */
    uint32_t max_layers;                  /* build 8: MOCKXR_MAX_LAYERS, the system's maxLayerCount */
    char exts[4][XR_MAX_EXTENSION_NAME_SIZE]; /* MOCKXR_EXTS: offered */
    int nexts, ext_on[4], ext_eq;         /* enabled (by index); XR_KHR_composition_layer_equirect2 enabled */
    int locates, locate_bad, locates_space;
    struct {
        int live, acq, waited, released_ever, released_frame;
        uint32_t next_img;
        mockxr_swapchain info;
    } scs[MOCK_NSC];
    XrTime t;
    mockxr_binding bind;
    mockxr_end end;
} M;

/* the action API's state (present thread, except the atomics xrLocateSpace uses) */
#define MAXPATH 256
#define MAXACT 32
#define MAXASP 16
#define XR_SET ((XrActionSet)(uintptr_t)0x7000)
#define ACT_H(i) ((XrAction)(uintptr_t)(0x7100 + (i)))
#define ASP_H(i) ((XrSpace)(uintptr_t)(0x7200 + (i)))
static struct {
    char path[MAXPATH][XR_MAX_PATH_LENGTH];
    int npath;
    int set_made, set_live, attached;
    struct { int live, type, hands; char name[XR_MAX_ACTION_NAME_SIZE], loc[XR_MAX_LOCALIZED_ACTION_NAME_SIZE]; } act[MAXACT];
    int nact;
    struct { XrPath prof; uint32_t n; struct { int act; XrPath b; } b[64]; } sug[8]; /* the profiles taken */
    int nsug;
    struct { int live, act, hand; } sp[MAXASP]; /* live is read by any thread (atomics) */
    int nsp, space_bad;
    int focus_at, unfocus_at, focus_stage, prof_off_at, prof_event;
    XrPath dev_prof, cur[2];
    mockxr_hand h[2], s[2]; /* scripted by the test; sampled at the last focused sync */
    mockxr_haptic hap[2];
    mockxr_input info;
    const char *dev_prof_name;
} Q;

static void viol(const char *msg);
static int g_loader_lock; /* set by the test while it is "inside" the loader's vkCreateInstance/vkCreateDevice */
static void lock_check(const char *what) {
    if (g_loader_lock) viol(what);
}
static struct { void *disp; } g_tramp_pd_obj; /* the runtime's VkPhysicalDevice (a "trampoline" handle) */
#define TRAMP_PD ((VkPhysicalDevice)&g_tramp_pd_obj)
#define XR_INST ((XrInstance)(uintptr_t)0x1111)
#define XR_SESS ((XrSession)(uintptr_t)0x2222)
#define XR_SPACE ((XrSpace)(uintptr_t)0x3333)
#define XR_SC ((XrSwapchain)(uintptr_t)0x4444)
#define XR_VIEW_SPACE ((XrSpace)(uintptr_t)0x3335)
#define XR_FLOOR_SPACE ((XrSpace)(uintptr_t)0x3334) /* a STAGE space next to a live LOCAL one (the floor probe) */
/* a live VIEW space (either of the two) */
static int is_view(XrSpace s);
/* the slot of a live swapchain handle (0x4444 + slot), -1 = none */
static int sc_slot(XrSwapchain s) {
    uintptr_t i = (uintptr_t)s - (uintptr_t)XR_SC;
    return i < MOCK_NSC && M.scs[i].live ? (int)i : -1;
}
/* how many swapchains live (acq 0) / have an image acquired (acq 1) */
static int sc_any(int acq) {
    int n = 0;
    for (int k = 0; k < MOCK_NSC; k++) n += acq ? M.scs[k].live && M.scs[k].acq : M.scs[k].live;
    return n;
}

__attribute__((constructor)) static void mock_env(void) {
    static char fn[128];
    const char *f = getenv("MOCKXR_FAIL");
    if (f && *f) {
        snprintf(fn, sizeof fn, "%s", f);
        char *c = strchr(fn, ':');
        M.fail_at = c ? atoi(c + 1) : 1;
        if (c) *c = 0;
        M.fail_name = fn;
    }
    M.hang_ms = getenv("MOCKXR_HANG_MS") ? atoi(getenv("MOCKXR_HANG_MS")) : 0;
    M.exit_at = getenv("MOCKXR_EXIT_AT") ? atoi(getenv("MOCKXR_EXIT_AT")) : 0;
    M.unload_exit = getenv("MOCKXR_UNLOAD_AT_EXIT") != NULL;
    M.no_stage = getenv("MOCKXR_NO_STAGE") != NULL;
    M.floor_cm = getenv("MOCKXR_FLOOR_CM") ? atoi(getenv("MOCKXR_FLOOR_CM")) : 152;
    M.recenter_at = getenv("MOCKXR_RECENTER_AT") ? atoi(getenv("MOCKXR_RECENTER_AT")) : 0;
    M.reject_hud_at = getenv("MOCKXR_REJECT_HUD") ? atoi(getenv("MOCKXR_REJECT_HUD")) : 0;
    M.reject_ptr_at = getenv("MOCKXR_REJECT_PTR") ? atoi(getenv("MOCKXR_REJECT_PTR")) : 0;
    M.reject_cf_at = getenv("MOCKXR_REJECT_CF") ? atoi(getenv("MOCKXR_REJECT_CF")) : 0;
    if (getenv("MOCKXR_REJECT_SC") && sscanf(getenv("MOCKXR_REJECT_SC"), "%ux%u", &M.reject_sc_w, &M.reject_sc_h) != 2)
        M.reject_sc_w = M.reject_sc_h = 0;
    M.reject_vm_at = getenv("MOCKXR_REJECT_VM") ? atoi(getenv("MOCKXR_REJECT_VM")) : 0;
    /* build 27: MOCKXR_WAIT_TIMEOUT=N:K - xrWaitSwapchainImage calls N..N+K-1 (any swapchain) return XR_TIMEOUT_EXPIRED,
     * the image still acquired and not waited (a compositor stall longer than the wait's timeout) */
    if (getenv("MOCKXR_WAIT_TIMEOUT") && sscanf(getenv("MOCKXR_WAIT_TIMEOUT"), "%d:%d", &M.wait_to_at, &M.wait_to_n) != 2)
        M.wait_to_at = M.wait_to_n = 0;
    M.reject_env_at = getenv("MOCKXR_REJECT_ENV") ? atoi(getenv("MOCKXR_REJECT_ENV")) : 0;
    M.max_layers = getenv("MOCKXR_MAX_LAYERS") ? (uint32_t)atoi(getenv("MOCKXR_MAX_LAYERS")) : MOCK_MAX_LAYERS;
    const char *ex = getenv("MOCKXR_EXTS"); /* SoM loading env: "a,b" */
    for (; ex && *ex && M.nexts < 4; ex += *ex == ',') {
        size_t L = strcspn(ex, ",");
        snprintf(M.exts[M.nexts++], sizeof M.exts[0], "%.*s", (int)L, ex);
        ex += L;
    }
    M.tramp_name = getenv("MOCKVK_TRAMP_NAME") ? getenv("MOCKVK_TRAMP_NAME") : MOCK_GPU_NAME;
    Q.focus_at = getenv("MOCKXR_FOCUS_AT") ? atoi(getenv("MOCKXR_FOCUS_AT")) : 0;
    Q.unfocus_at = getenv("MOCKXR_UNFOCUS_AT") ? atoi(getenv("MOCKXR_UNFOCUS_AT")) : 0;
    Q.prof_off_at = getenv("MOCKXR_PROFILE_OFF_AT") ? atoi(getenv("MOCKXR_PROFILE_OFF_AT")) : 0;
    Q.dev_prof_name = getenv("MOCKXR_PROFILE") ? getenv("MOCKXR_PROFILE") : "touch";
    for (int k = 0; k < 2; k++) { /* hands at rest, 20 cm left/right of the origin, tracked */
        mockxr_hand *hh = &Q.h[k];
        memset(hh, 0, sizeof *hh);
        float g[7] = {0, 0, 0, 1, k ? 0.2f : -0.2f, -0.3f, -0.4f}, a[7] = {0, 0, 0, 1, k ? 0.2f : -0.2f, -0.28f, -0.45f};
        memcpy(hh->grip, g, sizeof g);
        memcpy(hh->aim, a, sizeof a);
        hh->grip_flags = hh->aim_flags = 0xf;
        hh->vel_flags = 3;
    }
    M.t = 1000000000;
    static const int64_t fm[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
    const char *fl = getenv("MOCKXR_FORMATS");
    if (fl && *fl)
        for (char *e; *fl && M.nformats < 8; fl = *e ? e + 1 : e) M.formats[M.nformats++] = strtoll(fl, &e, 10);
    else
        for (; M.nformats < 5; M.nformats++) M.formats[M.nformats] = fm[M.nformats];
}

static void viol(const char *msg) {
    if (!M.viol++) snprintf(M.first, sizeof M.first, "%s", msg);
}

/* MOCKXR_UNLOAD_AT_EXIT: the loader's static destructor at exit (the runtime's code gone from here on) */
static void mock_unload(void) { atomic_store(&M.unloaded, 1); }
/* a call after it (any thread): counted; the first one named */
static void after_unload(const char *n) {
    if (!atomic_load(&M.unloaded)) return;
    if (!atomic_fetch_add(&M.after_unload, 1)) {
        char b[128];
        snprintf(b, sizeof b, "%s after the runtime's unload at exit", n);
        viol(b);
    }
}

/* count a call; 1 if this call must fail */
static int hit(const char *fn) {
    const char *n = fn + 2; /* "m_xrFoo" -> "xrFoo" */
    after_unload(n);
    int i;
    for (i = 0; i < M.nnames && strcmp(M.names[i], n); i++) {}
    if (i == M.nnames && M.nnames < 64) M.names[M.nnames++] = n;
    if (i >= 64) return 0;
    M.calls[i]++;
    M.seq++;
    if (!M.first_at[i]) M.first_at[i] = M.seq;
    if (strcmp(n, "xrGetInstanceProcAddr") && strcmp(n, "xrEnumerateInstanceExtensionProperties") &&
        strcmp(n, "xrCreateInstance") && !M.inst_live)
        viol("call without a live XrInstance");
    return M.fail_name && !strcmp(n, M.fail_name) && M.calls[i] == M.fail_at;
}
#define HIT() hit(__func__)
#define FAILR XR_ERROR_RUNTIME_FAILURE

static XrResult m_xrEnumerateInstanceExtensionProperties(const char *layer, uint32_t cap, uint32_t *n,
                                                         XrExtensionProperties *p) {
    (void)layer;
    if (HIT()) return FAILR;
    *n = 1 + (uint32_t)M.nexts;
    if (cap && cap < *n) return XR_ERROR_SIZE_INSUFFICIENT;
    for (uint32_t k = 0; cap && k < *n; k++) {
        if (p[k].type != XR_TYPE_EXTENSION_PROPERTIES) viol("extension properties without type");
        snprintf(p[k].extensionName, sizeof p[k].extensionName, "%s",
                 k ? M.exts[k - 1] : XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
        p[k].extensionVersion = k ? 1 : 8;
    }
    return XR_SUCCESS;
}

static XrResult m_xrCreateInstance(const XrInstanceCreateInfo *ci, XrInstance *out) {
    if (HIT()) return XR_ERROR_RUNTIME_UNAVAILABLE;
    if (M.hang_ms) usleep((useconds_t)M.hang_ms * 1000);
    int vk = 0;
    for (uint32_t i = 0; i < ci->enabledExtensionCount; i++) {
        const char *e = ci->enabledExtensionNames[i];
        int known = !strcmp(e, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
        vk |= known;
        for (int k = 0; k < M.nexts; k++)
            if (!strcmp(e, M.exts[k])) known = 1, M.ext_on[k] = 1;
        if (!known)
            return viol("xrCreateInstance with an extension the runtime does not offer"), XR_ERROR_EXTENSION_NOT_PRESENT;
        M.ext_eq |= !strcmp(e, XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME);
    }
    if (!vk) viol("xrCreateInstance without XR_KHR_vulkan_enable");
    if (M.inst_live) viol("second live XrInstance");
    __atomic_store_n(&M.inst_live, 1, __ATOMIC_SEQ_CST); /* read by the test thread after the init thread wrote it */
    if (M.unload_exit && !M.unload_reg) M.unload_reg = 1, atexit(mock_unload);
    *out = XR_INST;
    return XR_SUCCESS;
}

static XrResult m_xrDestroyInstance(XrInstance i) {
    if (HIT()) return FAILR;
    if (i != XR_INST) viol("xrDestroyInstance: bad handle");
    if (M.sess_live) viol("xrDestroyInstance with a live session");
    __atomic_store_n(&M.inst_live, 0, __ATOMIC_SEQ_CST);
    return XR_SUCCESS;
}

static XrResult m_xrGetSystem(XrInstance i, const XrSystemGetInfo *gi, XrSystemId *s) {
    (void)i;
    if (HIT()) return XR_ERROR_FORM_FACTOR_UNAVAILABLE;
    if (gi->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) viol("xrGetSystem: not HMD");
    *s = 7;
    return XR_SUCCESS;
}

/* build 8: the system's graphics limits (MOCKXR_MAX_LAYERS: maxLayerCount, default 16; MOCKXR_FAIL works on it) */
static XrResult m_xrGetSystemProperties(XrInstance i, XrSystemId sys, XrSystemProperties *p) {
    (void)i;
    if (HIT()) return XR_ERROR_RUNTIME_FAILURE;
    if (sys != 7 || !p || p->type != XR_TYPE_SYSTEM_PROPERTIES) viol("xrGetSystemProperties: bad arguments");
    p->systemId = 7, p->vendorId = 0x5a5a;
    snprintf(p->systemName, sizeof p->systemName, "mock HMD");
    p->graphicsProperties.maxSwapchainImageWidth = p->graphicsProperties.maxSwapchainImageHeight = 8192;
    p->graphicsProperties.maxLayerCount = M.max_layers;
    p->trackingProperties.orientationTracking = p->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

static XrResult str_out(const char *s, uint32_t cap, uint32_t *n, char *buf) {
    *n = (uint32_t)strlen(s) + 1;
    if (!cap) return XR_SUCCESS;
    if (cap < *n) return XR_ERROR_SIZE_INSUFFICIENT;
    memcpy(buf, s, *n);
    return XR_SUCCESS;
}
static XrResult m_xrGetVulkanInstanceExtensionsKHR(XrInstance i, XrSystemId s, uint32_t cap, uint32_t *n, char *b) {
    (void)i, (void)s;
    if (HIT()) return FAILR;
    return str_out(MOCK_INST_EXTS, cap, n, b);
}
static XrResult m_xrGetVulkanDeviceExtensionsKHR(XrInstance i, XrSystemId s, uint32_t cap, uint32_t *n, char *b) {
    (void)i, (void)s;
    if (HIT()) return FAILR;
    return str_out(MOCK_DEV_EXTS, cap, n, b);
}
static XrResult m_xrGetVulkanGraphicsRequirementsKHR(XrInstance i, XrSystemId s, XrGraphicsRequirementsVulkanKHR *r) {
    (void)i, (void)s;
    if (HIT()) return FAILR;
    if (r->type != XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR) viol("requirements: bad type");
    r->minApiVersionSupported = XR_MAKE_VERSION(1, 0, 0);
    r->maxApiVersionSupported = XR_MAKE_VERSION(1, 3, 0);
    M.got_req = 1;
    return XR_SUCCESS;
}
static XrResult m_xrGetVulkanGraphicsDeviceKHR(XrInstance i, XrSystemId s, VkInstance vi, VkPhysicalDevice *pd) {
    (void)i, (void)s;
    if (HIT()) return FAILR;
    if (!vi) viol("xrGetVulkanGraphicsDeviceKHR: no VkInstance");
    lock_check("xrGetVulkanGraphicsDeviceKHR inside vkCreate* (the runtime would deadlock on the loader lock)");
    *pd = TRAMP_PD;
    M.gave_pd = 1;
    return XR_SUCCESS;
}
static XrResult m_xrEnumerateViewConfigurationViews(XrInstance i, XrSystemId s, XrViewConfigurationType t, uint32_t cap,
                                                     uint32_t *n, XrViewConfigurationView *v) {
    (void)i, (void)s, (void)t;
    if (HIT()) return FAILR;
    *n = 2;
    for (uint32_t k = 0; k < cap && k < 2; k++) {
        v[k].recommendedImageRectWidth = 1832, v[k].recommendedImageRectHeight = 1920;
        v[k].maxImageRectWidth = 4096, v[k].maxImageRectHeight = 4096;
    }
    return XR_SUCCESS;
}

static XrResult m_xrCreateSession(XrInstance i, const XrSessionCreateInfo *ci, XrSession *out) {
    (void)i;
    if (HIT()) return FAILR;
    if (!M.got_req) return XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING;
    lock_check("xrCreateSession inside vkCreate* (loader lock)");
    const XrGraphicsBindingVulkanKHR *b = ci->next;
    if (!b || b->type != XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    if (!M.gave_pd || b->physicalDevice != TRAMP_PD) viol("binding: not the runtime's VkPhysicalDevice");
    M.bind = (mockxr_binding){b->instance, b->physicalDevice, b->device, b->queueFamilyIndex, b->queueIndex};
    __atomic_store_n(&M.sess_live, 1, __ATOMIC_SEQ_CST);
    *out = XR_SESS;
    return XR_SUCCESS;
}
static XrResult m_xrDestroySession(XrSession s) {
    if (HIT()) return FAILR;
    if (s != XR_SESS) viol("xrDestroySession: bad handle");
    if (sc_any(0) || M.space_live || M.view_live)
        viol("xrDestroySession before its swapchain/space");
    if (Q.info.spaces_live) viol("xrDestroySession before its action spaces");
    __atomic_store_n(&M.sess_live, 0, __ATOMIC_SEQ_CST);
    M.running = 0;
    Q.attached = 0; /* a new session would need its own attach */
    Q.focus_stage = 0;
    return XR_SUCCESS;
}
static XrResult m_xrBeginSession(XrSession s, const XrSessionBeginInfo *bi) {
    (void)s;
    if (HIT()) return FAILR;
    if (!M.ready_sent || M.running) viol("xrBeginSession not after READY");
    if (bi->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) viol("begin: not stereo");
    M.running = 1;
    return XR_SUCCESS;
}
static XrResult m_xrEndSession(XrSession s) {
    (void)s;
    if (HIT()) return FAILR;
    if (M.exit_stage != 1) viol("xrEndSession not after STOPPING");
    M.running = 0;
    M.exit_stage = 2;
    return XR_SUCCESS;
}
static XrResult m_xrPollEvent(XrInstance i, XrEventDataBuffer *ev) {
    (void)i;
    if (HIT()) return FAILR;
    if (ev->type != XR_TYPE_EVENT_DATA_BUFFER) viol("xrPollEvent: buffer type");
    XrSessionState st = XR_SESSION_STATE_UNKNOWN;
    if (M.recenter_at && M.ends >= M.recenter_at && !M.recenter_sent && M.running) {
        M.recenter_sent = 1;
        XrEventDataReferenceSpaceChangePending *rc = (XrEventDataReferenceSpaceChangePending *)ev;
        *rc = (XrEventDataReferenceSpaceChangePending){.type = XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING,
                                                        .session = XR_SESS, .referenceSpaceType = M.space_type,
                                                        .changeTime = M.t, .poseValid = XR_TRUE,
                                                        .poseInPreviousSpace = {{0, 0, 0, 1}, {0, 0, 0}}};
        return XR_SUCCESS;
    }
    if (Q.prof_event && Q.attached && M.sess_live) { /* after a focused sync that changed a hand's profile */
        Q.prof_event = 0;
        Q.info.prof_events++;
        *(XrEventDataInteractionProfileChanged *)ev =
            (XrEventDataInteractionProfileChanged){.type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED,
                                                   .session = XR_SESS};
        return XR_SUCCESS;
    }
    if (M.sess_live && !M.ready_sent) st = XR_SESSION_STATE_READY, M.ready_sent = 1;
    else if (M.running && M.exit_stage == 0 && Q.focus_stage < 3 && M.ends >= Q.focus_at)
        st = ++Q.focus_stage == 1 ? XR_SESSION_STATE_SYNCHRONIZED
             : Q.focus_stage == 2 ? XR_SESSION_STATE_VISIBLE : XR_SESSION_STATE_FOCUSED;
    else if (M.running && M.exit_stage == 0 && Q.focus_stage == 3 && Q.unfocus_at && M.ends >= Q.unfocus_at)
        st = XR_SESSION_STATE_VISIBLE, Q.focus_stage = 4;
    else if (M.exit_at && M.ends >= M.exit_at && M.exit_stage == 0) st = XR_SESSION_STATE_STOPPING, M.exit_stage = 1;
    else if (M.exit_stage == 2) st = XR_SESSION_STATE_IDLE, M.exit_stage = 3;
    else if (M.exit_stage == 3) st = XR_SESSION_STATE_EXITING, M.exit_stage = 4;
    if (st == XR_SESSION_STATE_UNKNOWN) return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged *e = (XrEventDataSessionStateChanged *)ev;
    *e = (XrEventDataSessionStateChanged){.type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED, .session = XR_SESS,
                                          .state = st, .time = M.t};
    return XR_SUCCESS;
}
static XrResult m_xrCreateReferenceSpace(XrSession s, const XrReferenceSpaceCreateInfo *ci, XrSpace *out) {
    (void)s;
    if (HIT()) return FAILR;
    if (ci->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW) { /* the head-locked quads' (the HUD's, comfort's) */
        int k = !(M.view_live & 1) ? 0 : !(M.view_live & 2) ? 1 : -1;
        if (k < 0) return viol("a third live VIEW space"), FAILR;
        M.view_live |= 1 << k;
        *out = (XrSpace)((uintptr_t)XR_VIEW_SPACE + (uintptr_t)k);
        return XR_SUCCESS;
    }
    if (ci->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE && M.space_live &&
        M.space_type == XR_REFERENCE_SPACE_TYPE_LOCAL && !M.no_stage) { /* the floor probe's, next to LOCAL */
        if (M.floor_live) return viol("a second live STAGE space next to LOCAL"), FAILR;
        M.floor_live = 1;
        *out = XR_FLOOR_SPACE;
        return XR_SUCCESS;
    }
    if (ci->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL &&
        !(ci->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE && !M.no_stage))
        viol("space: not LOCAL or an offered STAGE");
    M.space_type = ci->referenceSpaceType;
    M.space_live = 1;
    *out = XR_SPACE;
    return XR_SUCCESS;
}
static XrResult m_xrEnumerateReferenceSpaces(XrSession s, uint32_t cap, uint32_t *n, XrReferenceSpaceType *t) {
    (void)s;
    if (HIT()) return FAILR;
    static const XrReferenceSpaceType all[3] = {XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
                                                XR_REFERENCE_SPACE_TYPE_STAGE};
    *n = M.no_stage ? 2 : 3;
    for (uint32_t k = 0; k < cap && k < *n; k++) t[k] = all[k];
    return XR_SUCCESS;
}
static XrResult m_xrDestroySpace(XrSpace s) {
    if (HIT()) return FAILR;
    uintptr_t i = (uintptr_t)s - 0x7200u;
    if (s == XR_SPACE) {
        M.space_live = 0;
    } else if (s == XR_FLOOR_SPACE && M.floor_live) {
        M.floor_live = 0;
    } else if (is_view(s)) {
        M.view_live &= ~(1 << (int)((uintptr_t)s - (uintptr_t)XR_VIEW_SPACE));
    } else if (i < (uintptr_t)Q.nsp && __atomic_load_n(&Q.sp[i].live, __ATOMIC_SEQ_CST)) {
        __atomic_store_n(&Q.sp[i].live, 0, __ATOMIC_SEQ_CST);
        Q.info.spaces_live--;
    } else {
        viol("xrDestroySpace: unknown or dead space");
    }
    return XR_SUCCESS;
}
static XrResult m_xrEnumerateEnvironmentBlendModes(XrInstance i, XrSystemId s, XrViewConfigurationType t, uint32_t cap,
                                                    uint32_t *n, XrEnvironmentBlendMode *m) {
    (void)i, (void)s, (void)t;
    if (HIT()) return FAILR;
    *n = 1;
    if (cap) m[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    return XR_SUCCESS;
}
static XrResult m_xrEnumerateSwapchainFormats(XrSession s, uint32_t cap, uint32_t *n, int64_t *f) {
    (void)s;
    if (HIT()) return FAILR;
    *n = M.nformats;
    for (uint32_t k = 0; k < cap && k < M.nformats; k++) f[k] = M.formats[k];
    return XR_SUCCESS;
}
static XrResult m_xrCreateSwapchain(XrSession s, const XrSwapchainCreateInfo *ci, XrSwapchain *out) {
    (void)s;
    if (HIT()) return FAILR;
    int k = -1;
    for (int j = MOCK_NSC - 1; j >= 0; j--)
        if (!M.scs[j].live) k = j;
    if (k < 0) return viol("more live swapchains than the mock holds"), FAILR;
    if (ci->createFlags & ~(XrSwapchainCreateFlags)XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) viol("swapchain create flags");
    if (!(ci->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT)) viol("swapchain without TRANSFER_DST");
    if (ci->sampleCount != 1 || ci->faceCount != 1 || ci->arraySize != 1 || ci->mipCount != 1) viol("swapchain shape");
    memset(&M.scs[k], 0, sizeof M.scs[k]);
    M.scs[k].info = (mockxr_swapchain){ci->width, ci->height, (uint32_t)ci->usageFlags, ci->format,
                                       (uint32_t)ci->createFlags, 0};
    M.scs[k].live = 1;
    *out = (XrSwapchain)((uintptr_t)XR_SC + (uintptr_t)k);
    return XR_SUCCESS;
}
static XrResult m_xrDestroySwapchain(XrSwapchain s) {
    if (HIT()) return FAILR;
    int k = sc_slot(s);
    if (k < 0) return viol("xrDestroySwapchain: unknown or dead swapchain"), XR_ERROR_HANDLE_INVALID;
    M.scs[k].live = M.scs[k].acq = M.scs[k].waited = 0; /* its acquired image (if any) goes with it */
    return XR_SUCCESS;
}
static XrResult m_xrEnumerateSwapchainImages(XrSwapchain s, uint32_t cap, uint32_t *n, XrSwapchainImageBaseHeader *h) {
    if (HIT()) return FAILR;
    int k = sc_slot(s);
    if (k < 0) return viol("xrEnumerateSwapchainImages: unknown swapchain"), XR_ERROR_HANDLE_INVALID;
    uint32_t ni = (M.scs[k].info.create_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) ? 1 : 3;
    *n = ni;
    XrSwapchainImageVulkanKHR *im = (XrSwapchainImageVulkanKHR *)h;
    for (uint32_t j = 0; j < cap && j < ni; j++) {
        if (im[j].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) viol("swapchain image: bad type");
        im[j].image = (VkImage)(uintptr_t)(0x5000 + 0x100 * k + j);
    }
    return XR_SUCCESS;
}
static XrResult m_xrAcquireSwapchainImage(XrSwapchain s, const XrSwapchainImageAcquireInfo *ai, uint32_t *i) {
    (void)ai;
    if (HIT()) return FAILR;
    int k = sc_slot(s);
    if (k < 0) return viol("xrAcquireSwapchainImage: unknown swapchain"), XR_ERROR_HANDLE_INVALID;
    if (M.scs[k].acq) viol("acquire while an image is acquired");
    int stat = (M.scs[k].info.create_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) != 0;
    if (stat && M.scs[k].info.acquires) viol("a static swapchain acquired twice");
    M.scs[k].info.acquires++;
    M.scs[k].acq = 1;
    *i = stat ? 0 : M.scs[k].next_img++ % 3;
    return XR_SUCCESS;
}
static XrResult m_xrWaitSwapchainImage(XrSwapchain s, const XrSwapchainImageWaitInfo *wi) {
    if (HIT()) return FAILR;
    int k = sc_slot(s);
    if (k < 0) return viol("xrWaitSwapchainImage: unknown swapchain"), XR_ERROR_HANDLE_INVALID;
    if (!M.scs[k].acq || wi->timeout <= 0) viol("wait without acquire / no timeout");
    M.wait_calls++;
    if (M.wait_to_n > 0 && M.wait_calls >= M.wait_to_at && M.wait_calls < M.wait_to_at + M.wait_to_n)
        return M.wait_timeouts++, XR_TIMEOUT_EXPIRED;
    M.scs[k].waited = 1;
    return XR_SUCCESS;
}
static XrResult m_xrReleaseSwapchainImage(XrSwapchain s, const XrSwapchainImageReleaseInfo *ri) {
    (void)ri;
    if (HIT()) return FAILR;
    int k = sc_slot(s);
    if (k < 0) return viol("xrReleaseSwapchainImage: unknown swapchain"), XR_ERROR_HANDLE_INVALID;
    if (!M.scs[k].waited) viol("release without wait");
    M.scs[k].acq = M.scs[k].waited = 0;
    M.scs[k].released_ever = 1;
    M.scs[k].released_frame++;
    return XR_SUCCESS;
}
static XrResult m_xrWaitFrame(XrSession s, const XrFrameWaitInfo *wi, XrFrameState *fs) {
    (void)s, (void)wi;
    if (HIT()) return FAILR;
    if (!M.running) viol("xrWaitFrame while not running");
    if (M.frame_begun) viol("xrWaitFrame inside a frame");
    M.t += 11111111;
    fs->predictedDisplayTime = M.t;
    fs->predictedDisplayPeriod = 11111111;
    fs->shouldRender = XR_TRUE;
    M.waited = 1;
    return XR_SUCCESS;
}
static XrResult m_xrBeginFrame(XrSession s, const XrFrameBeginInfo *bi) {
    (void)s, (void)bi;
    if (HIT()) return FAILR;
    if (!M.waited) viol("xrBeginFrame without xrWaitFrame");
    M.waited = 0;
    M.frame_begun = 1;
    return XR_SUCCESS;
}
/* a layer's swapchain: live and with a released image, else a violation; its slot or -1 */
static int layer_sc(XrSwapchain sc) {
    int k = sc_slot(sc);
    if (k < 0) viol("a layer references a swapchain that does not live");
    else if (!M.scs[k].released_ever) viol("a layer references a swapchain that never released an image");
    return k;
}
/* layer 1: the HUD quad (H11) - its space, swapchain, rect, size, flags */
static void end_layer2(const XrCompositionLayerBaseHeader *l) {
    M.end.type2 = l->type;
    M.end.space2 = l->space == XR_SPACE ? 1 : is_view(l->space) ? 2 : 0;
    M.end.flags2 = l->layerFlags;
    if (!M.end.space2) viol("layer 1: not the reference space or a live VIEW space");
    if (l->layerFlags & ~(uint64_t)(XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT |
                                    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT))
        viol("layer 1: unknown layer flags");
    if (l->type != XR_TYPE_COMPOSITION_LAYER_QUAD) { /* a projection (the loading env's kept world): end_quad has it */
        if (l->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) viol("layer 1: not a quad or a projection");
        return;
    }
    const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)l;
    const XrRect2Di *r = &q->subImage.imageRect;
    M.end.sc2 = layer_sc(q->subImage.swapchain);
    M.end.eyes2 = q->eyeVisibility;
    M.end.rect2[0] = r->offset.x, M.end.rect2[1] = r->offset.y, M.end.rect2[2] = r->extent.width, M.end.rect2[3] = r->extent.height;
    M.end.size2[0] = q->size.width, M.end.size2[1] = q->size.height;
    const XrPosef *ps = &q->pose;
    float pp[7] = {ps->orientation.x, ps->orientation.y, ps->orientation.z, ps->orientation.w,
                   ps->position.x, ps->position.y, ps->position.z};
    memcpy(M.end.pose2, pp, sizeof pp);
    if (!(q->size.width > 0 && q->size.height > 0)) viol("layer 1: quad size");
    if (M.end.sc2 >= 0 && (r->offset.x < 0 || r->offset.y < 0 || r->extent.width <= 0 || r->extent.height <= 0 ||
                           (uint32_t)(r->offset.x + r->extent.width) > M.scs[M.end.sc2].info.width ||
                           (uint32_t)(r->offset.y + r->extent.height) > M.scs[M.end.sc2].info.height))
        viol("layer 1: quad rect outside its swapchain");
    float n = pp[0] * pp[0] + pp[1] * pp[1] + pp[2] * pp[2] + pp[3] * pp[3];
    if (!(n > 0.999f && n < 1.001f)) viol("layer 1: the quad's orientation is not a unit quaternion");
}
static int shows_sc(const XrFrameEndInfo *ei, uint32_t w, uint32_t h);
/* SoM loading env: a projection after layer 0 (the kept world): the first one recorded in M.end.pj_*, all checked */
static void end_proj(uint32_t i, const XrCompositionLayerBaseHeader *l) {
    const XrCompositionLayerProjection *p = (const XrCompositionLayerProjection *)l;
    if (p->space != XR_SPACE) viol("a projection after layer 0: not the reference space");
    if (p->viewCount != 2) return viol("a projection after layer 0: not 2 views");
    int first = !M.end.pj_index;
    if (first) M.end.pj_index = (int)i, M.end.pj_flags = l->layerFlags;
    for (uint32_t v = 0; v < 2; v++) {
        const XrCompositionLayerProjectionView *pv = &p->views[v];
        const XrRect2Di *r = &pv->subImage.imageRect;
        int k = layer_sc(pv->subImage.swapchain);
        if (pv->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW) viol("projection view type (after layer 0)");
        if (k >= 0 && (r->offset.x < 0 || r->offset.y < 0 || r->extent.width <= 0 || r->extent.height <= 0 ||
                       (uint32_t)(r->offset.x + r->extent.width) > M.scs[k].info.width ||
                       (uint32_t)(r->offset.y + r->extent.height) > M.scs[k].info.height))
            viol("a projection view's rect outside its swapchain (after layer 0)");
        if (!first) continue;
        M.end.pj_sc[v] = k;
        M.end.pj_rect[v][0] = r->offset.x, M.end.pj_rect[v][1] = r->offset.y;
        M.end.pj_rect[v][2] = r->extent.width, M.end.pj_rect[v][3] = r->extent.height;
        const XrPosef *ps = &pv->pose;
        float pp[7] = {ps->orientation.x, ps->orientation.y, ps->orientation.z, ps->orientation.w,
                       ps->position.x, ps->position.y, ps->position.z};
        float ff[4] = {pv->fov.angleLeft, pv->fov.angleRight, pv->fov.angleUp, pv->fov.angleDown};
        memcpy(M.end.pj_pose[v], pp, sizeof pp);
        memcpy(M.end.pj_fov[v], ff, sizeof ff);
    }
}
/* SoM loading env: layer 0 as an equirect2 layer (the sky): the extension enabled, its values in range */
static void end_equirect(const XrCompositionLayerBaseHeader *l) {
    const XrCompositionLayerEquirect2KHR *q = (const XrCompositionLayerEquirect2KHR *)l;
    const XrRect2Di *r = &q->subImage.imageRect;
    if (!M.ext_eq) viol("an equirect2 layer without XR_KHR_composition_layer_equirect2 enabled");
    int k = M.end.eq_sc = layer_sc(q->subImage.swapchain);
    M.end.eq_radius = q->radius;
    M.end.eq_angle[0] = q->centralHorizontalAngle, M.end.eq_angle[1] = q->upperVerticalAngle;
    M.end.eq_angle[2] = q->lowerVerticalAngle;
    M.end.eq_flags = l->layerFlags;
    M.end.eq_rect[0] = r->offset.x, M.end.eq_rect[1] = r->offset.y;
    M.end.eq_rect[2] = r->extent.width, M.end.eq_rect[3] = r->extent.height;
    const XrPosef *ps = &q->pose;
    float pp[7] = {ps->orientation.x, ps->orientation.y, ps->orientation.z, ps->orientation.w,
                   ps->position.x, ps->position.y, ps->position.z};
    memcpy(M.end.eq_pose, pp, sizeof pp);
    if (!(q->radius >= 0)) viol("equirect2: a negative radius");
    if (!((double)q->centralHorizontalAngle >= 0 && (double)q->centralHorizontalAngle <= 2 * M_PI))
        viol("equirect2: centralHorizontalAngle outside [0, 2 pi]");
    if (!((double)q->upperVerticalAngle <= M_PI / 2 && (double)q->lowerVerticalAngle >= -M_PI / 2 &&
          q->upperVerticalAngle > q->lowerVerticalAngle))
        viol("equirect2: vertical angles outside [-pi/2, pi/2] or upper <= lower");
    if (q->eyeVisibility != XR_EYE_VISIBILITY_BOTH) viol("equirect2: not both eyes");
    if (k >= 0 && (r->offset.x < 0 || r->offset.y < 0 || r->extent.width <= 0 || r->extent.height <= 0 ||
                   (uint32_t)(r->offset.x + r->extent.width) > M.scs[k].info.width ||
                   (uint32_t)(r->offset.y + r->extent.height) > M.scs[k].info.height))
        viol("equirect2: the rect outside its swapchain");
    float n = pp[0] * pp[0] + pp[1] * pp[1] + pp[2] * pp[2] + pp[3] * pp[3];
    if (!(n > 0.999f && n < 1.001f)) viol("equirect2: the orientation is not a unit quaternion");
}
/* does any layer show the loading env's sky (an equirect2 layer, or a quad on the cube atlas)? */
static int shows_env(const XrFrameEndInfo *ei) {
    for (uint32_t i = 0; i < ei->layerCount; i++)
        if (ei->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) return 1;
    return shows_sc(ei, SOMVR_ENV_CUBE_W, somvr_env_img_h(SOMVR_ENV_KIND_CUBE));
}
/* layer i >= 1: every quad after layer 0 is recorded in M.end.q[i] (and checked like layer 1) */
static void end_quad(uint32_t i, const XrCompositionLayerBaseHeader *l) {
    M.end.q[i].type = l->type;
    M.end.q[i].space = l->space == XR_SPACE ? 1 : is_view(l->space) ? 2 : 0;
    M.end.q[i].view = is_view(l->space) ? (int)((uintptr_t)l->space - (uintptr_t)XR_VIEW_SPACE) : -1;
    M.end.q[i].flags = l->layerFlags;
    M.end.q[i].sc = -1;
    if (!M.end.q[i].space) viol("a quad layer: not the reference space or a live VIEW space");
    if (l->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) return end_proj(i, l); /* the loading env's kept world */
    if (l->type != XR_TYPE_COMPOSITION_LAYER_QUAD) return viol("a layer after layer 0 is not a quad or a projection");
    const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)l;
    const XrRect2Di *r = &q->subImage.imageRect;
    int k = M.end.q[i].sc = layer_sc(q->subImage.swapchain);
    M.end.q[i].eyes = q->eyeVisibility;
    M.end.q[i].rect[0] = r->offset.x, M.end.q[i].rect[1] = r->offset.y;
    M.end.q[i].rect[2] = r->extent.width, M.end.q[i].rect[3] = r->extent.height;
    M.end.q[i].size[0] = q->size.width, M.end.q[i].size[1] = q->size.height;
    const XrPosef *ps = &q->pose;
    float pp[7] = {ps->orientation.x, ps->orientation.y, ps->orientation.z, ps->orientation.w,
                   ps->position.x, ps->position.y, ps->position.z};
    memcpy(M.end.q[i].pose, pp, sizeof pp);
    if (!(q->size.width > 0 && q->size.height > 0)) viol("a quad layer's size");
    if (k >= 0 && (r->offset.x < 0 || r->offset.y < 0 || r->extent.width <= 0 || r->extent.height <= 0 ||
                   (uint32_t)(r->offset.x + r->extent.width) > M.scs[k].info.width ||
                   (uint32_t)(r->offset.y + r->extent.height) > M.scs[k].info.height))
        viol("a quad layer's rect outside its swapchain");
    float n = pp[0] * pp[0] + pp[1] * pp[1] + pp[2] * pp[2] + pp[3] * pp[3];
    if (!(n > 0.999f && n < 1.001f)) viol("a quad layer's orientation is not a unit quaternion");
}
/* does any layer show a w x h swapchain (the pointer's 64x32, comfort's 512x528)? */
static int shows_sc(const XrFrameEndInfo *ei, uint32_t w, uint32_t h) {
    for (uint32_t i = 0; i < ei->layerCount; i++) {
        if (ei->layers[i]->type != XR_TYPE_COMPOSITION_LAYER_QUAD) continue;
        int k = sc_slot(((const XrCompositionLayerQuad *)ei->layers[i])->subImage.swapchain);
        if (k >= 0 && M.scs[k].info.width == w && M.scs[k].info.height == h) return 1;
    }
    return 0;
}
static int shows_ptr(const XrFrameEndInfo *ei) { return shows_sc(ei, 64, 32); }
static int is_view(XrSpace s) {
    uintptr_t k = (uintptr_t)s - (uintptr_t)XR_VIEW_SPACE;
    return k < 2 && (M.view_live >> k & 1);
}
static XrResult m_xrEndFrame(XrSession s, const XrFrameEndInfo *ei) {
    (void)s;
    M.ends++;
    if (HIT()) return M.frame_begun = 0, FAILR;
    if (ei->layerCount >= 2 && M.reject_hud_at && ++M.hud_ends == M.reject_hud_at)
        return XR_ERROR_LAYER_INVALID; /* validated first: the frame stays begun, nothing consumed */
    if (M.reject_ptr_at && shows_ptr(ei) && ++M.ptr_ends == M.reject_ptr_at) return XR_ERROR_LAYER_INVALID;
    if (M.reject_cf_at && shows_sc(ei, 512, 528) && ++M.cf_ends == M.reject_cf_at) return XR_ERROR_LAYER_INVALID;
    if (M.reject_sc_w && shows_sc(ei, M.reject_sc_w, M.reject_sc_h)) return M.reject_sc_n++, XR_ERROR_LAYER_INVALID;
    if (M.reject_vm_at && shows_sc(ei, 512, 768) && ++M.vm_ends == M.reject_vm_at) return XR_ERROR_LAYER_INVALID;
    if (M.reject_env_at && shows_env(ei) && ++M.env_ends == M.reject_env_at) return XR_ERROR_LAYER_INVALID;
    if (!M.frame_begun) viol("xrEndFrame without xrBeginFrame");
    if (sc_any(1)) viol("xrEndFrame with an acquired image");
    if (ei->displayTime != M.t) viol("xrEndFrame: displayTime is not the predicted one");
    if (ei->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE) viol("xrEndFrame: blend mode");
    if (ei->layerCount > MOCK_MAX_LAYERS) /* the spec minimum every runtime takes: the layer must stay within it */
        return viol("xrEndFrame: more layers than XR_MIN_COMPOSITION_LAYERS_SUPPORTED"), XR_ERROR_LAYER_LIMIT_EXCEEDED;
    if (ei->layerCount > M.max_layers) /* MOCKXR_MAX_LAYERS: the system's maxLayerCount */
        return viol("xrEndFrame: more layers than the system's maxLayerCount"), XR_ERROR_LAYER_LIMIT_EXCEEDED;
    if (ei->layerCount > MOCK_NQ) viol("xrEndFrame: more layers than the mock records");
    M.frame_begun = 0;
    memset(&M.end, 0, sizeof M.end);
    M.end.layer_count = ei->layerCount;
    M.end.released = M.scs[0].released_frame;
    M.end.released2 = M.scs[1].released_frame;
    for (int k = 0; k < MOCK_NSC; k++) M.end.released_n[k] = M.scs[k].released_frame, M.scs[k].released_frame = 0;
    M.end.time = ei->displayTime;
    M.end.sc2 = -1;
    M.end.eq_sc = -1;
    for (uint32_t i = 1; i < ei->layerCount && i < MOCK_NQ; i++) end_quad(i, ei->layers[i]);
    if (ei->layerCount >= 2) end_layer2(ei->layers[1]);
    if (ei->layerCount) {
        const XrCompositionLayerBaseHeader *l = ei->layers[0];
        M.end.type = l->type;
        M.end.flags = l->layerFlags;
        if (l->space != XR_SPACE) viol("layer: wrong space");
        if (l->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)l;
            const XrRect2Di *r = &q->subImage.imageRect;
            M.end.rect[0][0] = r->offset.x, M.end.rect[0][1] = r->offset.y;
            M.end.rect[0][2] = r->extent.width, M.end.rect[0][3] = r->extent.height;
            M.end.size[0] = q->size.width, M.end.size[1] = q->size.height;
            M.end.pos[0] = q->pose.position.x, M.end.pos[1] = q->pose.position.y, M.end.pos[2] = q->pose.position.z;
            M.end.same_swapchain = layer_sc(q->subImage.swapchain) == 0;
        } else if (l->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const XrCompositionLayerProjection *p = (const XrCompositionLayerProjection *)l;
            if (p->viewCount != 2) viol("projection: not 2 views");
            M.end.same_swapchain = 1;
            for (uint32_t v = 0; v < p->viewCount && v < 2; v++)
                M.end.same_swapchain &= layer_sc(p->views[v].subImage.swapchain) == 0;
            for (uint32_t v = 0; v < p->viewCount && v < 2; v++) {
                const XrRect2Di *r = &p->views[v].subImage.imageRect;
                M.end.rect[v][0] = r->offset.x, M.end.rect[v][1] = r->offset.y;
                M.end.rect[v][2] = r->extent.width, M.end.rect[v][3] = r->extent.height;
                if (p->views[v].type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW) viol("projection view type");
                const XrPosef *ps = &p->views[v].pose;
                float pp[7] = {ps->orientation.x, ps->orientation.y, ps->orientation.z, ps->orientation.w,
                               ps->position.x, ps->position.y, ps->position.z};
                memcpy(M.end.pose[v], pp, sizeof pp);
                float ff[4] = {p->views[v].fov.angleLeft, p->views[v].fov.angleRight, p->views[v].fov.angleUp,
                               p->views[v].fov.angleDown};
                memcpy(M.end.fov[v], ff, sizeof ff);
                if (r->offset.x < 0 || r->offset.y < 0 || r->extent.width <= 0 || r->extent.height <= 0 ||
                    (uint32_t)(r->offset.x + r->extent.width) > M.scs[0].info.width ||
                    (uint32_t)(r->offset.y + r->extent.height) > M.scs[0].info.height)
                    viol("projection view rect outside the swapchain");
            }
        } else if (l->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) { /* SoM loading env: the sky */
            end_equirect(l);
        } else {
            viol("unexpected layer type");
        }
    }
    return XR_SUCCESS;
}
/* any thread: no HIT() (not thread-safe); atomic counters only */
static XrResult m_xrLocateViews(XrSession s, const XrViewLocateInfo *li, XrViewState *vs, uint32_t cap, uint32_t *n,
                                XrView *v) {
    __atomic_add_fetch(&M.locates, 1, __ATOMIC_SEQ_CST);
    after_unload("xrLocateViews");
    if (!__atomic_load_n(&M.sess_live, __ATOMIC_SEQ_CST) || s != XR_SESS || li->space != XR_SPACE ||
        li->displayTime <= 0 || li->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        __atomic_add_fetch(&M.locate_bad, 1, __ATOMIC_SEQ_CST);
    *n = 2;
    vs->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    float a = (float)(li->displayTime % 1000000007) * 1e-9f; /* a slow head turn: the pose depends on the time */
    for (uint32_t k = 0; k < cap && k < 2; k++) {
        v[k].pose = (XrPosef){{0, sinf(0.25f * a), 0, cosf(0.25f * a)}, {k ? 0.032f : -0.032f, 1.6f + 0.01f * a, -0.1f}};
        v[k].fov = k ? (XrFovf){-0.70f, 0.90f, 0.80f, -0.95f} : (XrFovf){-0.90f, 0.70f, 0.80f, -0.95f};
    }
    for (volatile int i = 0; i < 2000; i++) {} /* a little time inside, for the teardown race */
    if (!__atomic_load_n(&M.sess_live, __ATOMIC_SEQ_CST)) __atomic_add_fetch(&M.locate_bad, 1, __ATOMIC_SEQ_CST);
    return XR_SUCCESS;
}

/* ---- the action API --------------------------------------------------------------------------------------------- */
/* the components of the three profiles (OpenXR 1.0 spec, "Interaction Profile Paths") */
#define TOUCH_BOTH                                                                                                     \
    " input/squeeze/value input/trigger/value input/trigger/touch input/thumbstick input/thumbstick/x"                \
    " input/thumbstick/y input/thumbstick/click input/thumbstick/touch input/thumbrest/touch input/grip/pose"          \
    " input/aim/pose output/haptic "
static const struct {
    const char *prof, *left, *right;
} g_comp[] = {
    {"/interaction_profiles/oculus/touch_controller",
     TOUCH_BOTH "input/x/click input/x/touch input/y/click input/y/touch input/menu/click ",
     TOUCH_BOTH "input/a/click input/a/touch input/b/click input/b/touch input/system/click "},
    {"/interaction_profiles/valve/index_controller",
     " input/system/click input/system/touch input/a/click input/a/touch input/b/click input/b/touch input/squeeze/value"
     " input/squeeze/force input/trigger/click input/trigger/value input/trigger/touch input/thumbstick"
     " input/thumbstick/x input/thumbstick/y input/thumbstick/click input/thumbstick/touch input/trackpad"
     " input/trackpad/x input/trackpad/y input/trackpad/force input/trackpad/touch input/grip/pose input/aim/pose"
     " output/haptic ",
     NULL},
    {"/interaction_profiles/khr/simple_controller", " input/select/click input/menu/click input/grip/pose input/aim/pose"
                                                     " output/haptic ", NULL},
};
static const char *path_str(XrPath p) { return p >= 1 && p <= (XrPath)Q.npath ? Q.path[p - 1] : NULL; }
static XrPath path_find(const char *s) {
    for (int i = 0; i < Q.npath; i++)
        if (!strcmp(Q.path[i], s)) return (XrPath)(i + 1);
    return XR_NULL_PATH;
}
static int hand_of(XrPath p) { /* 0 left, 1 right, -1 another path */
    const char *s = path_str(p);
    return !s ? -1 : !strcmp(s, "/user/hand/left") ? 0 : !strcmp(s, "/user/hand/right") ? 1 : -1;
}
static int act_ix(XrAction a) {
    uintptr_t i = (uintptr_t)a - 0x7100u;
    return i < (uintptr_t)Q.nact && Q.act[i].live ? (int)i : -1;
}
/* is action `a` bound on hand h in the taken suggestions of profile p? */
static int bound(XrPath p, int a, int h) {
    char pre[32];
    snprintf(pre, sizeof pre, "/user/hand/%s/", h ? "right" : "left");
    for (int k = 0; k < Q.nsug; k++)
        if (Q.sug[k].prof == p)
            for (uint32_t i = 0; i < Q.sug[k].n; i++)
                if (Q.sug[k].b[i].act == a && !strncmp(path_str(Q.sug[k].b[i].b), pre, strlen(pre))) return 1;
    return 0;
}
static int focused(void) { return M.running && Q.focus_stage == 3; }
static int act_active(int a, int h) { return focused() && Q.cur[h] && bound(Q.cur[h], a, h); }
static int act_named(int a, const char *n) { return a >= 0 && !strcmp(Q.act[a].name, n); }
static uint32_t btn_bit(int a) {
    static const char *const n[] = {"lower", "upper", "stick_click", "menu", "trigger_touch", "thumb_touch"};
    for (int i = 0; i < 6; i++)
        if (act_named(a, n[i])) return 1u << i;
    return 0;
}

static XrResult m_xrStringToPath(XrInstance i, const char *s, XrPath *out) {
    (void)i;
    if (HIT()) return FAILR;
    size_t n = strlen(s);
    int ok = n > 1 && n < XR_MAX_PATH_LENGTH && s[0] == '/' && s[n - 1] != '/' && !strstr(s, "//");
    for (size_t k = 0; ok && k < n; k++) ok = strchr("abcdefghijklmnopqrstuvwxyz0123456789-_./", s[k]) != NULL;
    if (!ok) return viol("xrStringToPath: a malformed path"), XR_ERROR_PATH_FORMAT_INVALID;
    if (!(*out = path_find(s))) {
        if (Q.npath == MAXPATH) return XR_ERROR_PATH_COUNT_EXCEEDED;
        snprintf(Q.path[Q.npath++], XR_MAX_PATH_LENGTH, "%s", s);
        *out = (XrPath)Q.npath;
    }
    return XR_SUCCESS;
}
static XrResult m_xrPathToString(XrInstance i, XrPath p, uint32_t cap, uint32_t *n, char *buf) {
    (void)i;
    if (HIT()) return FAILR;
    const char *s = path_str(p);
    if (!s) return viol("xrPathToString: unknown path"), XR_ERROR_PATH_INVALID;
    return str_out(s, cap, n, buf);
}
static XrResult m_xrCreateActionSet(XrInstance i, const XrActionSetCreateInfo *ci, XrActionSet *out) {
    (void)i;
    if (HIT()) return FAILR;
    if (ci->type != XR_TYPE_ACTION_SET_CREATE_INFO || !ci->actionSetName[0] || !ci->localizedActionSetName[0])
        viol("xrCreateActionSet: type or names");
    if (Q.set_live) viol("a second live action set");
    Q.set_made++, Q.set_live = 1, Q.info.sets_live = 1;
    *out = XR_SET;
    return XR_SUCCESS;
}
static XrResult m_xrDestroyActionSet(XrActionSet s) {
    if (HIT()) return FAILR;
    if (s != XR_SET || !Q.set_live) return viol("xrDestroyActionSet: not a live set"), XR_ERROR_HANDLE_INVALID;
    Q.set_live = 0, Q.info.sets_live = 0;
    for (int k = 0; k < Q.nact; k++) Q.act[k].live = 0;
    return XR_SUCCESS;
}
static XrResult m_xrCreateAction(XrActionSet s, const XrActionCreateInfo *ci, XrAction *out) {
    if (HIT()) return FAILR;
    if (s != XR_SET || !Q.set_live) return viol("xrCreateAction: not a live set"), XR_ERROR_HANDLE_INVALID;
    if (Q.attached) return viol("xrCreateAction after the attach"), XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    if (ci->type != XR_TYPE_ACTION_CREATE_INFO || Q.nact == MAXACT) return viol("xrCreateAction: type"), FAILR;
    for (int k = 0; k < Q.nact; k++)
        if (Q.act[k].live && (!strcmp(Q.act[k].name, ci->actionName) || !strcmp(Q.act[k].loc, ci->localizedActionName)))
            return viol("xrCreateAction: a duplicate name"), XR_ERROR_NAME_DUPLICATED;
    int hands = 0;
    for (uint32_t k = 0; k < ci->countSubactionPaths; k++) {
        int h = hand_of(ci->subactionPaths[k]);
        if (h < 0) return viol("xrCreateAction: a subaction path that is not a hand"), XR_ERROR_PATH_UNSUPPORTED;
        hands |= 1 << h;
    }
    int t = ci->actionType;
    if (t != XR_ACTION_TYPE_BOOLEAN_INPUT && t != XR_ACTION_TYPE_FLOAT_INPUT && t != XR_ACTION_TYPE_VECTOR2F_INPUT &&
        t != XR_ACTION_TYPE_POSE_INPUT && t != XR_ACTION_TYPE_VIBRATION_OUTPUT)
        viol("xrCreateAction: action type");
    int k = Q.nact++;
    Q.act[k].live = 1, Q.act[k].type = t, Q.act[k].hands = hands;
    snprintf(Q.act[k].name, sizeof Q.act[k].name, "%s", ci->actionName);
    snprintf(Q.act[k].loc, sizeof Q.act[k].loc, "%s", ci->localizedActionName);
    Q.info.actions++;
    *out = ACT_H(k);
    return XR_SUCCESS;
}
static XrResult m_xrSuggestInteractionProfileBindings(XrInstance i, const XrInteractionProfileSuggestedBinding *sb) {
    (void)i;
    if (HIT()) return FAILR;
    if (sb->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING) viol("suggestion: type");
    if (Q.attached) return viol("a suggestion after the attach"), XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    const char *prof = path_str(sb->interactionProfile);
    int c = -1;
    for (int k = 0; prof && k < 3; k++)
        if (!strcmp(prof, g_comp[k].prof)) c = k;
    if (c < 0) return viol("suggestion: an unknown profile"), XR_ERROR_PATH_UNSUPPORTED;
    if (!sb->countSuggestedBindings || sb->countSuggestedBindings > 64) return viol("suggestion: count"), FAILR;
    for (uint32_t k = 0; k < sb->countSuggestedBindings; k++) {
        int a = act_ix(sb->suggestedBindings[k].action);
        const char *b = path_str(sb->suggestedBindings[k].binding);
        if (a < 0 || !b) return viol("suggestion: a dead action or an unknown path"), XR_ERROR_HANDLE_INVALID;
        int h = !strncmp(b, "/user/hand/left/", 16) ? 0 : !strncmp(b, "/user/hand/right/", 17) ? 1 : -1;
        if (h < 0) return viol("suggestion: not a hand path"), XR_ERROR_PATH_UNSUPPORTED;
        char comp[XR_MAX_PATH_LENGTH + 2];
        snprintf(comp, sizeof comp, " %s ", b + (h ? 17 : 16));
        const char *list = h && g_comp[c].right ? g_comp[c].right : g_comp[c].left;
        if (!strstr(list, comp)) {
            char m[300];
            snprintf(m, sizeof m, "suggestion: %s has no %s", prof, b);
            return viol(m), XR_ERROR_PATH_UNSUPPORTED;
        }
        size_t L = strlen(comp) - 1; /* comp = " x/y/z ": compare its tail */
        int pose = L > 5 && !strncmp(comp + L - 5, "/pose", 5), hap = strstr(comp, " output/haptic ") != NULL;
        int vec = !strcmp(comp, " input/thumbstick ") || !strcmp(comp, " input/trackpad ");
        int t = Q.act[a].type;
        if ((t == XR_ACTION_TYPE_POSE_INPUT) != pose || (t == XR_ACTION_TYPE_VIBRATION_OUTPUT) != hap ||
            (t == XR_ACTION_TYPE_VECTOR2F_INPUT) != vec)
            return viol("suggestion: the action's type does not fit the component"), XR_ERROR_PATH_UNSUPPORTED;
        if (!(Q.act[a].hands >> h & 1)) return viol("suggestion: a hand the action has no subaction path for"), FAILR;
    }
    int k;
    for (k = 0; k < Q.nsug && Q.sug[k].prof != sb->interactionProfile; k++) {}
    if (k == Q.nsug) Q.nsug++, Q.info.suggested_ok++;
    Q.sug[k].prof = sb->interactionProfile;
    Q.sug[k].n = sb->countSuggestedBindings;
    for (uint32_t j = 0; j < sb->countSuggestedBindings; j++)
        Q.sug[k].b[j].act = act_ix(sb->suggestedBindings[j].action), Q.sug[k].b[j].b = sb->suggestedBindings[j].binding;
    return XR_SUCCESS;
}
static XrResult m_xrCreateActionSpace(XrSession s, const XrActionSpaceCreateInfo *ci, XrSpace *out) {
    if (HIT()) return FAILR;
    if (s != XR_SESS || !M.sess_live) return viol("xrCreateActionSpace: no live session"), XR_ERROR_HANDLE_INVALID;
    int a = act_ix(ci->action), h = hand_of(ci->subactionPath);
    if (ci->type != XR_TYPE_ACTION_SPACE_CREATE_INFO || a < 0 || Q.act[a].type != XR_ACTION_TYPE_POSE_INPUT)
        return viol("xrCreateActionSpace: not a live pose action"), XR_ERROR_ACTION_TYPE_MISMATCH;
    if (h < 0 || !(Q.act[a].hands >> h & 1)) return viol("xrCreateActionSpace: subaction path"), XR_ERROR_PATH_UNSUPPORTED;
    if (ci->poseInActionSpace.orientation.w != 1.0f) viol("xrCreateActionSpace: not the identity pose");
    if (Q.nsp == MAXASP) return FAILR;
    int k = Q.nsp++;
    Q.sp[k].act = a, Q.sp[k].hand = h;
    __atomic_store_n(&Q.sp[k].live, 1, __ATOMIC_SEQ_CST);
    Q.info.spaces_live++;
    *out = ASP_H(k);
    return XR_SUCCESS;
}
static XrResult m_xrAttachSessionActionSets(XrSession s, const XrSessionActionSetsAttachInfo *ai) {
    if (HIT()) return FAILR;
    if (s != XR_SESS || !M.sess_live) return viol("attach: no live session"), XR_ERROR_HANDLE_INVALID;
    if (Q.attached) return viol("a second attach"), XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    if (ai->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO || ai->countActionSets != 1 ||
        ai->actionSets[0] != XR_SET || !Q.set_live)
        return viol("attach: not our live set"), XR_ERROR_HANDLE_INVALID;
    Q.attached = Q.info.attached = 1;
    return XR_SUCCESS;
}
static XrPath prof_path(const char *shortname) {
    for (int k = 0; k < 3; k++)
        if (strstr(g_comp[k].prof, shortname)) return path_find(g_comp[k].prof);
    return XR_NULL_PATH;
}
static int prof_taken(XrPath p) {
    for (int k = 0; p && k < Q.nsug; k++)
        if (Q.sug[k].prof == p) return 1;
    return 0;
}
static XrResult m_xrSyncActions(XrSession s, const XrActionsSyncInfo *si) {
    if (HIT()) return FAILR;
    if (s != XR_SESS || !M.sess_live) return viol("xrSyncActions: no live session"), XR_ERROR_HANDLE_INVALID;
    if (!Q.attached) return viol("xrSyncActions before the attach"), XR_ERROR_ACTIONSET_NOT_ATTACHED;
    if (si->type != XR_TYPE_ACTIONS_SYNC_INFO || si->countActiveActionSets != 1 ||
        si->activeActionSets[0].actionSet != XR_SET || si->activeActionSets[0].subactionPath != XR_NULL_PATH)
        viol("xrSyncActions: not our set");
    if (!M.running) viol("xrSyncActions while the session does not run");
    if (!focused()) return XR_SESSION_NOT_FOCUSED;
    int asleep = !strcmp(Q.dev_prof_name, "none") || (Q.prof_off_at && M.ends >= Q.prof_off_at);
    XrPath dev = asleep ? XR_NULL_PATH : prof_path(Q.dev_prof_name);
    XrPath cur = prof_taken(dev) ? dev : !asleep && prof_taken(prof_path("simple")) ? prof_path("simple") : XR_NULL_PATH;
    for (int h = 0; h < 2; h++) {
        if (Q.cur[h] != cur) Q.prof_event = 1;
        Q.cur[h] = cur;
        Q.s[h] = Q.h[h];
    }
    Q.info.syncs++;
    return XR_SUCCESS;
}
/* a state read: our attached action of type t, a subaction path the action has. -> the hand, or -1 (error in *r) */
static int state_check(XrSession s, const XrActionStateGetInfo *gi, int t, const char *what, XrResult *r) {
    int a = act_ix(gi->action), h = hand_of(gi->subactionPath);
    *r = XR_SUCCESS;
    if (s != XR_SESS || !M.sess_live) *r = XR_ERROR_HANDLE_INVALID;
    else if (!Q.attached) *r = XR_ERROR_ACTIONSET_NOT_ATTACHED;
    else if (gi->type != XR_TYPE_ACTION_STATE_GET_INFO || a < 0) *r = XR_ERROR_HANDLE_INVALID;
    else if (Q.act[a].type != t) *r = XR_ERROR_ACTION_TYPE_MISMATCH;
    else if (h < 0 || !(Q.act[a].hands >> h & 1)) *r = XR_ERROR_PATH_UNSUPPORTED;
    if (*r != XR_SUCCESS) viol(what);
    return *r == XR_SUCCESS ? h : -1;
}
static XrResult m_xrGetActionStateBoolean(XrSession s, const XrActionStateGetInfo *gi, XrActionStateBoolean *st) {
    if (HIT()) return FAILR;
    XrResult r;
    int h = state_check(s, gi, XR_ACTION_TYPE_BOOLEAN_INPUT, "xrGetActionStateBoolean: bad action/type/path", &r);
    if (h < 0) return r;
    int a = act_ix(gi->action);
    st->isActive = act_active(a, h);
    st->currentState = st->isActive && (Q.s[h].buttons & btn_bit(a));
    st->changedSinceLastSync = XR_FALSE;
    st->lastChangeTime = 0;
    return XR_SUCCESS;
}
static XrResult m_xrGetActionStateFloat(XrSession s, const XrActionStateGetInfo *gi, XrActionStateFloat *st) {
    if (HIT()) return FAILR;
    XrResult r;
    int h = state_check(s, gi, XR_ACTION_TYPE_FLOAT_INPUT, "xrGetActionStateFloat: bad action/type/path", &r);
    if (h < 0) return r;
    int a = act_ix(gi->action);
    st->isActive = act_active(a, h);
    st->currentState = !st->isActive ? 0.0f : act_named(a, "trigger") ? Q.s[h].trigger : Q.s[h].squeeze;
    st->changedSinceLastSync = XR_FALSE;
    st->lastChangeTime = 0;
    return XR_SUCCESS;
}
static XrResult m_xrGetActionStateVector2f(XrSession s, const XrActionStateGetInfo *gi, XrActionStateVector2f *st) {
    if (HIT()) return FAILR;
    XrResult r;
    int h = state_check(s, gi, XR_ACTION_TYPE_VECTOR2F_INPUT, "xrGetActionStateVector2f: bad action/type/path", &r);
    if (h < 0) return r;
    st->isActive = act_active(act_ix(gi->action), h);
    st->currentState = st->isActive ? (XrVector2f){Q.s[h].stick[0], Q.s[h].stick[1]} : (XrVector2f){0, 0};
    st->changedSinceLastSync = XR_FALSE;
    st->lastChangeTime = 0;
    return XR_SUCCESS;
}
static XrResult m_xrGetActionStatePose(XrSession s, const XrActionStateGetInfo *gi, XrActionStatePose *st) {
    if (HIT()) return FAILR;
    XrResult r;
    int h = state_check(s, gi, XR_ACTION_TYPE_POSE_INPUT, "xrGetActionStatePose: bad action/type/path", &r);
    if (h < 0) return r;
    st->isActive = act_active(act_ix(gi->action), h);
    return XR_SUCCESS;
}
static XrResult m_xrGetCurrentInteractionProfile(XrSession s, XrPath user, XrInteractionProfileState *st) {
    if (HIT()) return FAILR;
    if (s != XR_SESS || !M.sess_live) return viol("xrGetCurrentInteractionProfile: no session"), XR_ERROR_HANDLE_INVALID;
    if (!Q.attached) return viol("xrGetCurrentInteractionProfile before the attach"), XR_ERROR_ACTIONSET_NOT_ATTACHED;
    int h = hand_of(user);
    if (h < 0 || st->type != XR_TYPE_INTERACTION_PROFILE_STATE)
        return viol("xrGetCurrentInteractionProfile: path/type"), XR_ERROR_PATH_UNSUPPORTED;
    st->interactionProfile = Q.cur[h];
    return XR_SUCCESS;
}
/* any thread (the game thread's hand locate): no HIT(), atomics only */
static XrResult m_xrLocateSpace(XrSpace sp, XrSpace base, XrTime t, XrSpaceLocation *loc) {
    __atomic_add_fetch(&M.locates_space, 1, __ATOMIC_SEQ_CST);
    after_unload("xrLocateSpace");
    if (base == XR_FLOOR_SPACE) { /* the floor probe: LOCAL in STAGE (the present thread) */
        if (sp != XR_SPACE || !M.floor_live || !M.space_live || t <= 0 || loc->type != XR_TYPE_SPACE_LOCATION)
            return viol("xrLocateSpace in the STAGE space: not LOCAL, dead or no time"), XR_ERROR_HANDLE_INVALID;
        M.floor_locates++;
        loc->locationFlags = 0xf;
        loc->pose = (XrPosef){{0, 0, 0, 1}, {0, (float)M.floor_cm * 0.01f, 0}};
        return XR_SUCCESS;
    }
    uintptr_t i = (uintptr_t)sp - 0x7200u;
    int ok = i < (uintptr_t)MAXASP && __atomic_load_n(&Q.sp[i].live, __ATOMIC_SEQ_CST) && base == XR_SPACE &&
             __atomic_load_n(&M.sess_live, __ATOMIC_SEQ_CST) && t > 0 && loc->type == XR_TYPE_SPACE_LOCATION;
    if (!ok) {
        __atomic_add_fetch(&Q.space_bad, 1, __ATOMIC_SEQ_CST);
        return XR_ERROR_HANDLE_INVALID;
    }
    int h = Q.sp[i].hand, a = Q.sp[i].act, grip = act_named(a, "grip_pose");
    const mockxr_hand *hh = &Q.s[h];
    int on = act_active(a, h);
    const float *p = grip ? hh->grip : hh->aim;
    loc->locationFlags = on ? (grip ? hh->grip_flags : hh->aim_flags) : 0;
    loc->pose = (XrPosef){{p[0], p[1], p[2], p[3]}, {p[4], p[5], p[6]}};
    XrSpaceVelocity *v = (XrSpaceVelocity *)loc->next;
    if (v && v->type == XR_TYPE_SPACE_VELOCITY) {
        v->velocityFlags = on ? hh->vel_flags : 0;
        v->linearVelocity = (XrVector3f){hh->lin_vel[0], hh->lin_vel[1], hh->lin_vel[2]};
        v->angularVelocity = (XrVector3f){hh->ang_vel[0], hh->ang_vel[1], hh->ang_vel[2]};
    } else if (v) {
        __atomic_add_fetch(&Q.space_bad, 1, __ATOMIC_SEQ_CST);
    }
    if (grip) __atomic_store_n(&Q.info.locate_time[h], t, __ATOMIC_SEQ_CST);
    for (volatile int k = 0; k < 2000; k++) {} /* a little time inside, for the teardown race */
    if (!__atomic_load_n(&Q.sp[i].live, __ATOMIC_SEQ_CST) || !__atomic_load_n(&M.sess_live, __ATOMIC_SEQ_CST))
        __atomic_add_fetch(&Q.space_bad, 1, __ATOMIC_SEQ_CST);
    return XR_SUCCESS;
}
static int haptic_check(XrSession s, const XrHapticActionInfo *hi) {
    int a = act_ix(hi->action), h = hand_of(hi->subactionPath);
    if (s != XR_SESS || !M.sess_live || !Q.attached || hi->type != XR_TYPE_HAPTIC_ACTION_INFO || a < 0 ||
        Q.act[a].type != XR_ACTION_TYPE_VIBRATION_OUTPUT || h < 0 || !(Q.act[a].hands >> h & 1))
        return viol("haptics: no session/attach, or not our vibration action and a hand"), -1;
    return h;
}
static XrResult m_xrApplyHapticFeedback(XrSession s, const XrHapticActionInfo *hi, const XrHapticBaseHeader *hb) {
    if (HIT()) return FAILR;
    int h = haptic_check(s, hi);
    if (h < 0) return XR_ERROR_HANDLE_INVALID;
    const XrHapticVibration *v = (const XrHapticVibration *)hb;
    if (v->type != XR_TYPE_HAPTIC_VIBRATION || !(v->amplitude > 0 && v->amplitude <= 1) ||
        (v->duration <= 0 && v->duration != XR_MIN_HAPTIC_DURATION) || v->frequency < 0)
        return viol("xrApplyHapticFeedback: type or values"), XR_ERROR_VALIDATION_FAILURE;
    Q.hap[h].applies++;
    Q.hap[h].amplitude = v->amplitude, Q.hap[h].frequency = v->frequency, Q.hap[h].duration = v->duration;
    return focused() ? XR_SUCCESS : XR_SESSION_NOT_FOCUSED;
}
static XrResult m_xrStopHapticFeedback(XrSession s, const XrHapticActionInfo *hi) {
    if (HIT()) return FAILR;
    int h = haptic_check(s, hi);
    if (h < 0) return XR_ERROR_HANDLE_INVALID;
    Q.hap[h].stops++;
    return focused() ? XR_SUCCESS : XR_SESSION_NOT_FOCUSED;
}

static XrResult m_xrGetInstanceProcAddr(XrInstance inst, const char *name, PFN_xrVoidFunction *fn);
#define FN(n) {#n, (PFN_xrVoidFunction)m_##n},
static const struct {
    const char *n;
    PFN_xrVoidFunction f;
} g_fns[] = {FN(xrEnumerateInstanceExtensionProperties) FN(xrCreateInstance) FN(xrDestroyInstance) FN(xrGetSystem) FN(xrGetSystemProperties)
                 FN(xrGetVulkanInstanceExtensionsKHR) FN(xrGetVulkanDeviceExtensionsKHR)
                     FN(xrGetVulkanGraphicsRequirementsKHR) FN(xrGetVulkanGraphicsDeviceKHR)
                         FN(xrEnumerateViewConfigurationViews) FN(xrCreateSession) FN(xrDestroySession)
                             FN(xrBeginSession) FN(xrEndSession) FN(xrPollEvent) FN(xrCreateReferenceSpace)
                                 FN(xrDestroySpace) FN(xrEnumerateEnvironmentBlendModes) FN(xrEnumerateSwapchainFormats)
                                     FN(xrEnumerateReferenceSpaces)
                                     FN(xrCreateSwapchain) FN(xrDestroySwapchain) FN(xrEnumerateSwapchainImages)
                                         FN(xrAcquireSwapchainImage) FN(xrWaitSwapchainImage)
                                             FN(xrReleaseSwapchainImage) FN(xrWaitFrame) FN(xrBeginFrame)
                                                 FN(xrEndFrame) FN(xrLocateViews) FN(xrGetInstanceProcAddr)
    FN(xrStringToPath) FN(xrPathToString) FN(xrCreateActionSet) FN(xrDestroyActionSet) FN(xrCreateAction)
    FN(xrSuggestInteractionProfileBindings) FN(xrCreateActionSpace) FN(xrAttachSessionActionSets) FN(xrSyncActions)
    FN(xrGetActionStateBoolean) FN(xrGetActionStateFloat) FN(xrGetActionStateVector2f) FN(xrGetActionStatePose)
    FN(xrGetCurrentInteractionProfile) FN(xrLocateSpace) FN(xrApplyHapticFeedback) FN(xrStopHapticFeedback)};

static XrResult m_xrGetInstanceProcAddr(XrInstance inst, const char *name, PFN_xrVoidFunction *fn) {
    HIT();
    (void)inst;
    for (size_t i = 0; i < sizeof g_fns / sizeof g_fns[0]; i++)
        if (!strcmp(name, g_fns[i].n)) return *fn = g_fns[i].f, XR_SUCCESS;
    *fn = NULL;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

EXPORT XrResult xrGetInstanceProcAddr(XrInstance inst, const char *name, PFN_xrVoidFunction *fn);
EXPORT XrResult xrGetInstanceProcAddr(XrInstance inst, const char *name, PFN_xrVoidFunction *fn) {
    return m_xrGetInstanceProcAddr(inst, name, fn);
}

/* ---- the fake Vulkan loader trampoline ---- */
static VKAPI_ATTR VkResult VKAPI_CALL t_EnumeratePhysicalDevices(VkInstance i, uint32_t *n, VkPhysicalDevice *pd) {
    (void)i;
    lock_check("trampoline vkEnumeratePhysicalDevices inside vkCreate* (loader lock: deadlock)");
    if (pd && *n >= 1) pd[0] = TRAMP_PD;
    *n = 1;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL t_GetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
    if (pd != TRAMP_PD) viol("trampoline called with a handle that is not a trampoline handle (the loader would abort)");
    memset(p, 0, sizeof *p);
    p->vendorID = MOCK_VENDOR;
    p->deviceID = MOCK_DEVICE;
    snprintf(p->deviceName, sizeof p->deviceName, "%s", M.tramp_name);
    memset(p->pipelineCacheUUID, MOCK_UUID_BYTE, VK_UUID_SIZE);
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i, const char *n);
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i, const char *n) {
    (void)i;
    if (!strcmp(n, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)t_EnumeratePhysicalDevices;
    if (!strcmp(n, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)t_GetPhysicalDeviceProperties;
    return NULL;
}

/* ---- accessors for the test ---- */
EXPORT void mockvk_loader_lock(int held);
EXPORT void mockvk_loader_lock(int held) { g_loader_lock = held; }
EXPORT int mockxr_calls(const char *name);
EXPORT int mockxr_calls(const char *name) {
    if (!strcmp(name, "xrLocateViews")) return __atomic_load_n(&M.locates, __ATOMIC_SEQ_CST);
    if (!strcmp(name, "xrLocateSpace")) return __atomic_load_n(&M.locates_space, __ATOMIC_SEQ_CST);
    for (int i = 0; i < M.nnames; i++)
        if (!strcmp(M.names[i], name)) return M.calls[i];
    return 0;
}
EXPORT int mockxr_after_unload(void);
EXPORT int mockxr_after_unload(void) { return atomic_load(&M.after_unload); }
EXPORT int mockxr_unloaded(void);
EXPORT int mockxr_unloaded(void) { return atomic_load(&M.unloaded); }
EXPORT int mockxr_total(void);
EXPORT int mockxr_total(void) {
    int t = 0;
    for (int i = 0; i < M.nnames; i++) t += M.calls[i];
    return t;
}
EXPORT int mockxr_violations(const char **first);
EXPORT int mockxr_violations(const char **first) {
    *first = M.first;
    return M.viol;
}
EXPORT const mockxr_binding *mockxr_binding_get(void);
EXPORT const mockxr_binding *mockxr_binding_get(void) { return &M.bind; }
EXPORT const mockxr_swapchain *mockxr_swapchain_get(void);
EXPORT const mockxr_swapchain *mockxr_swapchain_get(void) { return &M.scs[0].info; }
EXPORT const mockxr_swapchain *mockxr_swapchain2_get(void);
EXPORT const mockxr_swapchain *mockxr_swapchain2_get(void) { return &M.scs[1].info; }
EXPORT int mockxr_sc2_live(void);
EXPORT int mockxr_sc2_live(void) { return M.scs[1].live; }
EXPORT const mockxr_swapchain *mockxr_swapchain_k(int k);
EXPORT const mockxr_swapchain *mockxr_swapchain_k(int k) { return &M.scs[k >= 0 && k < MOCK_NSC ? k : 0].info; }
EXPORT int mockxr_sc_live_k(int k);
EXPORT int mockxr_sc_live_k(int k) { return k >= 0 && k < MOCK_NSC && M.scs[k].live; }
EXPORT int mockxr_sc_find(uint32_t w, uint32_t h);
EXPORT int mockxr_sc_find(uint32_t w, uint32_t h) {
    for (int k = 0; k < MOCK_NSC; k++)
        if (M.scs[k].live && M.scs[k].info.width == w && M.scs[k].info.height == h) return k;
    return -1;
}
EXPORT int mockxr_sc_count(void);
EXPORT int mockxr_sc_count(void) { /* SoM loading env: swapchains alive */
    int n = 0;
    for (int k = 0; k < MOCK_NSC; k++) n += M.scs[k].live;
    return n;
}
EXPORT int mockxr_ext_enabled(const char *name);
EXPORT int mockxr_ext_enabled(const char *name) { /* SoM loading env: the last xrCreateInstance enabled it */
    for (int k = 0; k < M.nexts; k++)
        if (!strcmp(M.exts[k], name)) return M.ext_on[k];
    return 0;
}
EXPORT int mockxr_view_live(void);
EXPORT int mockxr_view_live(void) { return (M.view_live & 1) + (M.view_live >> 1 & 1); }
EXPORT const mockxr_end *mockxr_last_end(void);
EXPORT const mockxr_end *mockxr_last_end(void) { return &M.end; }
EXPORT int mockxr_wait_timeouts(void);
EXPORT int mockxr_wait_timeouts(void) { return M.wait_timeouts; }
EXPORT int mockxr_live(void);
EXPORT int mockxr_live(void) {
    return M.sess_live + M.space_live + M.floor_live + (M.view_live & 1) + (M.view_live >> 1 & 1) + sc_any(0) +
           Q.info.spaces_live;
}
EXPORT int mockxr_floor_locates(void);
EXPORT int mockxr_floor_locates(void) { return M.floor_locates; }
EXPORT int mockxr_rejects(void);
EXPORT int mockxr_rejects(void) { return M.reject_sc_n; } /* MOCKXR_REJECT_SC's rejections so far */
EXPORT int mockxr_inst_live(void);
EXPORT int mockxr_inst_live(void) { return __atomic_load_n(&M.inst_live, __ATOMIC_SEQ_CST); }
EXPORT void *mockxr_tramp_pd(void);
EXPORT void *mockxr_tramp_pd(void) { return TRAMP_PD; }
EXPORT int mockxr_locates(void);
EXPORT int mockxr_locates(void) { return __atomic_load_n(&M.locates, __ATOMIC_SEQ_CST); }
EXPORT int mockxr_locate_bad(void);
EXPORT int mockxr_locate_bad(void) { return __atomic_load_n(&M.locate_bad, __ATOMIC_SEQ_CST); }
EXPORT int mockxr_space_type(void);
EXPORT int mockxr_space_type(void) { return M.space_type; }
EXPORT int64_t mockxr_time(void);
EXPORT int64_t mockxr_time(void) { return M.t; }

/* ---- the action API's accessors ---- */
EXPORT int mockxr_first(const char *name);
EXPORT int mockxr_first(const char *name) {
    for (int i = 0; i < M.nnames; i++)
        if (!strcmp(M.names[i], name)) return M.first_at[i];
    return 0;
}
EXPORT void mockxr_input_set(int hand, const mockxr_hand *h);
EXPORT void mockxr_input_set(int hand, const mockxr_hand *h) { Q.h[hand & 1] = *h; }
EXPORT const mockxr_haptic *mockxr_haptic_get(int hand);
EXPORT const mockxr_haptic *mockxr_haptic_get(int hand) { return &Q.hap[hand & 1]; }
EXPORT const mockxr_input *mockxr_input_get(void);
EXPORT const mockxr_input *mockxr_input_get(void) {
    Q.info.focused = focused();
    return &Q.info;
}
EXPORT int mockxr_space_bad(void);
EXPORT int mockxr_space_bad(void) { return __atomic_load_n(&Q.space_bad, __ATOMIC_SEQ_CST); }
