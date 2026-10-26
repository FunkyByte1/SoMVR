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
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mock_common.h"

#define EXPORT __attribute__((visibility("default")))

static struct {
    const char *names[64];
    int calls[64];
    int nnames;
    int viol;
    char first[256];
    const char *fail_name;
    int fail_at, hang_ms, exit_at;
    int64_t formats[8];
    uint32_t nformats;
    const char *tramp_name;
    int inst_live, sess_live, space_live, sc_live;
    int got_req, gave_pd, running, ready_sent, exit_stage, frame_begun, waited, img_acq, img_waited, ends;
    uint32_t next_img;
    XrTime t;
    mockxr_binding bind;
    mockxr_swapchain sc;
    mockxr_end end;
} M;

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
    M.tramp_name = getenv("MOCKVK_TRAMP_NAME") ? getenv("MOCKVK_TRAMP_NAME") : MOCK_GPU_NAME;
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

/* count a call; 1 if this call must fail */
static int hit(const char *fn) {
    const char *n = fn + 2; /* "m_xrFoo" -> "xrFoo" */
    int i;
    for (i = 0; i < M.nnames && strcmp(M.names[i], n); i++) {}
    if (i == M.nnames && M.nnames < 64) M.names[M.nnames++] = n;
    if (i >= 64) return 0;
    M.calls[i]++;
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
    *n = 1;
    if (cap) {
        if (p[0].type != XR_TYPE_EXTENSION_PROPERTIES) viol("extension properties without type");
        snprintf(p[0].extensionName, sizeof p[0].extensionName, "%s", XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
        p[0].extensionVersion = 8;
    }
    return XR_SUCCESS;
}

static XrResult m_xrCreateInstance(const XrInstanceCreateInfo *ci, XrInstance *out) {
    if (HIT()) return XR_ERROR_RUNTIME_UNAVAILABLE;
    if (M.hang_ms) usleep((useconds_t)M.hang_ms * 1000);
    int vk = 0;
    for (uint32_t i = 0; i < ci->enabledExtensionCount; i++)
        vk |= !strcmp(ci->enabledExtensionNames[i], XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
    if (!vk) viol("xrCreateInstance without XR_KHR_vulkan_enable");
    if (M.inst_live) viol("second live XrInstance");
    __atomic_store_n(&M.inst_live, 1, __ATOMIC_SEQ_CST); /* read by the test thread after the init thread wrote it */
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
    M.sess_live = 1;
    *out = XR_SESS;
    return XR_SUCCESS;
}
static XrResult m_xrDestroySession(XrSession s) {
    if (HIT()) return FAILR;
    if (s != XR_SESS) viol("xrDestroySession: bad handle");
    if (M.sc_live || M.space_live) viol("xrDestroySession before its swapchain/space");
    M.sess_live = 0;
    M.running = 0;
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
    if (M.sess_live && !M.ready_sent) st = XR_SESSION_STATE_READY, M.ready_sent = 1;
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
    if (ci->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL) viol("space: not LOCAL");
    M.space_live = 1;
    *out = XR_SPACE;
    return XR_SUCCESS;
}
static XrResult m_xrDestroySpace(XrSpace s) {
    (void)s;
    if (HIT()) return FAILR;
    M.space_live = 0;
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
    if (M.sc_live) viol("second live swapchain");
    if (!(ci->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT)) viol("swapchain without TRANSFER_DST");
    if (ci->sampleCount != 1 || ci->faceCount != 1 || ci->arraySize != 1 || ci->mipCount != 1) viol("swapchain shape");
    M.sc = (mockxr_swapchain){ci->width, ci->height, (uint32_t)ci->usageFlags, ci->format};
    M.sc_live = 1;
    *out = XR_SC;
    return XR_SUCCESS;
}
static XrResult m_xrDestroySwapchain(XrSwapchain s) {
    (void)s;
    if (HIT()) return FAILR;
    M.sc_live = 0;
    return XR_SUCCESS;
}
static XrResult m_xrEnumerateSwapchainImages(XrSwapchain s, uint32_t cap, uint32_t *n, XrSwapchainImageBaseHeader *h) {
    (void)s;
    if (HIT()) return FAILR;
    *n = 3;
    XrSwapchainImageVulkanKHR *im = (XrSwapchainImageVulkanKHR *)h;
    for (uint32_t k = 0; k < cap && k < 3; k++) {
        if (im[k].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) viol("swapchain image: bad type");
        im[k].image = (VkImage)(uintptr_t)(0x5000 + k);
    }
    return XR_SUCCESS;
}
static XrResult m_xrAcquireSwapchainImage(XrSwapchain s, const XrSwapchainImageAcquireInfo *ai, uint32_t *i) {
    (void)s, (void)ai;
    if (HIT()) return FAILR;
    if (M.img_acq) viol("acquire while an image is acquired");
    M.img_acq = 1;
    *i = M.next_img++ % 3;
    return XR_SUCCESS;
}
static XrResult m_xrWaitSwapchainImage(XrSwapchain s, const XrSwapchainImageWaitInfo *wi) {
    (void)s;
    if (HIT()) return FAILR;
    if (!M.img_acq || wi->timeout <= 0) viol("wait without acquire / no timeout");
    M.img_waited = 1;
    return XR_SUCCESS;
}
static XrResult m_xrReleaseSwapchainImage(XrSwapchain s, const XrSwapchainImageReleaseInfo *ri) {
    (void)s, (void)ri;
    if (HIT()) return FAILR;
    if (!M.img_waited) viol("release without wait");
    M.img_acq = M.img_waited = 0;
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
static XrResult m_xrEndFrame(XrSession s, const XrFrameEndInfo *ei) {
    (void)s;
    M.ends++;
    if (HIT()) return M.frame_begun = 0, FAILR;
    if (!M.frame_begun) viol("xrEndFrame without xrBeginFrame");
    if (M.img_acq) viol("xrEndFrame with an acquired image");
    if (ei->displayTime != M.t) viol("xrEndFrame: displayTime is not the predicted one");
    if (ei->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE) viol("xrEndFrame: blend mode");
    M.frame_begun = 0;
    memset(&M.end, 0, sizeof M.end);
    M.end.layer_count = ei->layerCount;
    if (ei->layerCount) {
        const XrCompositionLayerBaseHeader *l = ei->layers[0];
        M.end.type = l->type;
        if (l->space != XR_SPACE) viol("layer: wrong space");
        if (l->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)l;
            const XrRect2Di *r = &q->subImage.imageRect;
            M.end.rect[0][0] = r->offset.x, M.end.rect[0][1] = r->offset.y;
            M.end.rect[0][2] = r->extent.width, M.end.rect[0][3] = r->extent.height;
            M.end.size[0] = q->size.width, M.end.size[1] = q->size.height;
            M.end.pos[0] = q->pose.position.x, M.end.pos[1] = q->pose.position.y, M.end.pos[2] = q->pose.position.z;
            M.end.same_swapchain = q->subImage.swapchain == XR_SC;
        } else if (l->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const XrCompositionLayerProjection *p = (const XrCompositionLayerProjection *)l;
            if (p->viewCount != 2) viol("projection: not 2 views");
            M.end.same_swapchain = 1;
            for (uint32_t v = 0; v < p->viewCount && v < 2; v++) {
                const XrRect2Di *r = &p->views[v].subImage.imageRect;
                M.end.rect[v][0] = r->offset.x, M.end.rect[v][1] = r->offset.y;
                M.end.rect[v][2] = r->extent.width, M.end.rect[v][3] = r->extent.height;
                M.end.same_swapchain &= p->views[v].subImage.swapchain == XR_SC;
                if (p->views[v].type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW) viol("projection view type");
            }
        } else {
            viol("unexpected layer type");
        }
    }
    return XR_SUCCESS;
}
static XrResult m_xrLocateViews(XrSession s, const XrViewLocateInfo *li, XrViewState *vs, uint32_t cap, uint32_t *n,
                                XrView *v) {
    (void)s;
    if (HIT()) return FAILR;
    if (li->displayTime != M.t || li->space != XR_SPACE) viol("xrLocateViews: time/space");
    *n = 2;
    vs->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    for (uint32_t k = 0; k < cap && k < 2; k++) {
        v[k].pose = (XrPosef){{0, 0, 0, 1}, {k ? 0.032f : -0.032f, 0, 0}};
        v[k].fov = (XrFovf){-0.8f, 0.8f, 0.8f, -0.8f};
    }
    return XR_SUCCESS;
}

static XrResult m_xrGetInstanceProcAddr(XrInstance inst, const char *name, PFN_xrVoidFunction *fn);
#define FN(n) {#n, (PFN_xrVoidFunction)m_##n},
static const struct {
    const char *n;
    PFN_xrVoidFunction f;
} g_fns[] = {FN(xrEnumerateInstanceExtensionProperties) FN(xrCreateInstance) FN(xrDestroyInstance) FN(xrGetSystem)
                 FN(xrGetVulkanInstanceExtensionsKHR) FN(xrGetVulkanDeviceExtensionsKHR)
                     FN(xrGetVulkanGraphicsRequirementsKHR) FN(xrGetVulkanGraphicsDeviceKHR)
                         FN(xrEnumerateViewConfigurationViews) FN(xrCreateSession) FN(xrDestroySession)
                             FN(xrBeginSession) FN(xrEndSession) FN(xrPollEvent) FN(xrCreateReferenceSpace)
                                 FN(xrDestroySpace) FN(xrEnumerateEnvironmentBlendModes) FN(xrEnumerateSwapchainFormats)
                                     FN(xrCreateSwapchain) FN(xrDestroySwapchain) FN(xrEnumerateSwapchainImages)
                                         FN(xrAcquireSwapchainImage) FN(xrWaitSwapchainImage)
                                             FN(xrReleaseSwapchainImage) FN(xrWaitFrame) FN(xrBeginFrame)
                                                 FN(xrEndFrame) FN(xrLocateViews) FN(xrGetInstanceProcAddr)};

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
    for (int i = 0; i < M.nnames; i++)
        if (!strcmp(M.names[i], name)) return M.calls[i];
    return 0;
}
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
EXPORT const mockxr_swapchain *mockxr_swapchain_get(void) { return &M.sc; }
EXPORT const mockxr_end *mockxr_last_end(void);
EXPORT const mockxr_end *mockxr_last_end(void) { return &M.end; }
EXPORT int mockxr_live(void);
EXPORT int mockxr_live(void) { return M.sess_live + M.space_live + M.sc_live; }
EXPORT int mockxr_inst_live(void);
EXPORT int mockxr_inst_live(void) { return __atomic_load_n(&M.inst_live, __ATOMIC_SEQ_CST); }
EXPORT void *mockxr_tramp_pd(void);
EXPORT void *mockxr_tramp_pd(void) { return TRAMP_PD; }
