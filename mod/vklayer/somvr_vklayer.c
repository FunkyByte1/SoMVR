/* somvr_vklayer.c - VK_LAYER_SOMVR_openxr: an explicit Vulkan layer that bridges Shadow of Mordor (native Linux Feral
 * build, Vulkan 1.1 on RADV) to OpenXR. Phase 1: the presented image goes to the headset (mono, same image per eye).
 *
 * README
 *   Build/test: ./build.sh [--check] [--vulkaninfo]   (C11, no libstdc++; links only libc)
 *   Enable (game environment only):
 *     VK_LAYER_PATH=<this dir>  VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
 *     XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json (the harness's HOME has no active_runtime.json)
 *   Switches:
 *     SOMVR_XR=0|off       pure pass-through (no OpenXR, nothing added, present not intercepted)
 *     SOMVR_XR unset|1     OpenXR only for the instance whose engine name is "Feral3D" (the game); others pass through
 *     SOMVR_XR=force       OpenXR for any app (e.g. a vkcube smoke test). Any other value = off.
 *     SOMVR_XR_LOG=FILE    append a log (O_CLOEXEC)
 *     SOMVR_XR_LAYER=quad|proj  quad (default): a world-locked 2 m wide screen 2 m ahead in LOCAL space;
 *                          proj: a projection layer, view i = half i of the swapchain with the located eye poses
 *     SOMVR_XR_LOADER=PATH (tests) OpenXR loader, default /usr/lib64/libopenxr_loader.so.1 (dlopened at runtime)
 *     SOMVR_XR_TIMEOUT_MS  (default 5000) limit for xrCreateInstance..xrGetSystem, run on a helper thread
 *   Flow (XR_KHR_vulkan_enable, version 1):
 *     vkCreateInstance  helper thread: dlopen loader, xrCreateInstance, xrGetSystem(HMD), graphics requirements,
 *                       instance + device extension lists; append the instance ones (no duplicates; raise apiVersion
 *                       only if the runtime's minimum is higher). If that create fails, retry with the app's info.
 *     vkCreateDevice    append the device extensions if the GPU has them all; retry with the app's info on failure.
 *     vkGetDeviceQueue* remember queue -> (device, family, index).
 *     vkCreateSwapchainKHR  add TRANSFER_SRC to the image usage (retry without it on failure: no copy then).
 *     first vkQueuePresentKHR  xrGetVulkanGraphicsDeviceKHR (checked against the game's GPU through the loader's own
 *                       entry points), xrCreateSession with the present queue, LOCAL space. Not earlier: the loader
 *                       holds its global lock inside vkCreateInstance/vkCreateDevice and the runtime calls back into it.
 *     every present     xrPollEvent (begin/end session), xrWaitFrame (paces the game), xrBeginFrame, acquire+wait the
 *                       XR image, one submit on the present queue: wait the app's present semaphores, copy the image
 *                       into both halves of a (2w x h) swapchain, signal our semaphore (per game image; fence per
 *                       image); release, xrEndFrame; the real present then waits our semaphore only.
 *   Fail soft: any XR or setup error is logged, the session is torn down and the layer passes through for the rest of
 *   the process. A runtime that does not answer within SOMVR_XR_TIMEOUT_MS is abandoned (its thread is left behind).
 *   Headers: third_party/Vulkan-Headers-1.4.341 (tag v1.4.341), third_party/OpenXR-SDK-1.1.61 (OpenXR SDK 1.1.61).
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "somvr_logic.h"

#define EXPORT __attribute__((visibility("default")))
#define FENCE_TIMEOUT_NS 1000000000ull
#define XR_IMAGE_TIMEOUT_NS 1000000000ll
#define QUAD_DIST_M 2.0f
#define QUAD_WIDTH_M 2.0f
#define MAX_SC_IMAGES 16
#define MAX_XR_IMAGES 8

/* ---- log + config ------------------------------------------------------------------------------------------------ */
static FILE *g_log;
static pthread_mutex_t g_log_mx = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int mode, layer, timeout_ms;
    const char *loader;
} cfg;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

__attribute__((format(printf, 1, 2))) static void lg(const char *fmt, ...) {
    if (!g_log) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&g_log_mx);
    fprintf(g_log, "%ld.%03ld [%d/%ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000, (int)getpid(),
            (long)syscall(SYS_gettid));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    pthread_mutex_unlock(&g_log_mx);
}

static const char *xr_str(XrResult r) {
    switch (r) {
#define XR_ENUM_CASE(name, val) case name: return #name;
        XR_LIST_ENUM_XrResult(XR_ENUM_CASE)
#undef XR_ENUM_CASE
    default: return "XR_?";
    }
}

static void xr_global_init(void);
static void once_init(void) {
    const char *p = getenv("SOMVR_XR_LOG");
    if (p && *p) g_log = fopen(p, "ae");
    cfg.mode = somvr_parse_mode(getenv("SOMVR_XR"));
    cfg.layer = somvr_parse_layer(getenv("SOMVR_XR_LAYER"));
    p = getenv("SOMVR_XR_LOADER");
    cfg.loader = p && *p ? p : "/usr/lib64/libopenxr_loader.so.1";
    p = getenv("SOMVR_XR_TIMEOUT_MS");
    cfg.timeout_ms = p ? atoi(p) : 5000;
    if (cfg.timeout_ms < 50 || cfg.timeout_ms > 60000) cfg.timeout_ms = 5000;
    xr_global_init();
    lg("VK_LAYER_SOMVR_openxr loaded: SOMVR_XR=%s -> %s, layer %s, loader %s, timeout %d ms",
       getenv("SOMVR_XR") ? getenv("SOMVR_XR") : "(unset)",
       cfg.mode == SOMVR_MODE_OFF ? "pass-through" : cfg.mode == SOMVR_MODE_FORCE ? "force" : "auto (Feral3D only)",
       cfg.layer == SOMVR_LAYER_PROJ ? "proj" : "quad", cfg.loader, cfg.timeout_ms);
}
static void init(void) { pthread_once(&g_once, once_init); }

/* ---- object tables (linked lists; a handful of entries) ----------------------------------------------------------- */
static inline void *dkey(const void *dispatchable) { return *(void *const *)dispatchable; }

typedef struct inst_st {
    struct inst_st *next;
    VkInstance inst;
    void *key;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
} inst_st;

#define DEV_FUNCS(X)                                                                                                   \
    X(DestroyDevice) X(GetDeviceQueue) X(GetDeviceQueue2) X(CreateSwapchainKHR) X(DestroySwapchainKHR)                 \
    X(GetSwapchainImagesKHR) X(QueuePresentKHR) X(QueueSubmit) X(CreateCommandPool) X(DestroyCommandPool)            \
    X(AllocateCommandBuffers) X(FreeCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer)   \
    X(CmdPipelineBarrier) X(CmdCopyImage) X(CmdBlitImage) X(CreateFence) X(DestroyFence) X(WaitForFences)            \
    X(ResetFences) X(CreateSemaphore) X(DestroySemaphore)

typedef struct dev_st {
    struct dev_st *next;
    VkDevice dev;
    void *key;
    VkPhysicalDevice pd;
    inst_st *is;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkSetDeviceLoaderData set_loader_data;
    int xr; /* created with the XR device extensions: the hooks are active */
#define DECL_FN(n) PFN_vk##n n;
    DEV_FUNCS(DECL_FN)
#undef DECL_FN
    VkQueueFamilyProperties qf[16];
    uint32_t nqf;
    VkCommandPool pool;
    VkSemaphore *grave; /* semaphores of destroyed swapchains (a present may still wait): freed with the device */
    uint32_t ngrave;
} dev_st;

typedef struct q_st {
    struct q_st *next;
    VkQueue q;
    dev_st *ds;
    uint32_t family, index;
} q_st;

typedef struct {
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore sem;
    int pending; /* fence submitted, not yet waited */
} slot_st;

typedef struct sc_st {
    struct sc_st *next;
    VkSwapchainKHR sc;
    dev_st *ds;
    VkFormat fmt;
    VkExtent2D ext;
    int copyable;
    uint32_t nimg;
    VkImage img[MAX_SC_IMAGES];
    slot_st slot[MAX_SC_IMAGES];
} sc_st;

static pthread_mutex_t g_tab = PTHREAD_MUTEX_INITIALIZER;
static inst_st *g_insts;
static dev_st *g_devs;
static q_st *g_queues;
static sc_st *g_scs;

#define LIST_FIND(head, field, val)                                                                                    \
    ({                                                                                                                 \
        __typeof__(head) it_;                                                                                          \
        pthread_mutex_lock(&g_tab);                                                                                    \
        for (it_ = (head); it_ && it_->field != (val); it_ = it_->next) {}                                             \
        pthread_mutex_unlock(&g_tab);                                                                                  \
        it_;                                                                                                           \
    })
#define LIST_ADD(head, node)                                                                                           \
    do {                                                                                                               \
        pthread_mutex_lock(&g_tab);                                                                                    \
        (node)->next = (head);                                                                                         \
        (head) = (node);                                                                                               \
        pthread_mutex_unlock(&g_tab);                                                                                  \
    } while (0)
#define LIST_DEL(head, node)                                                                                           \
    do {                                                                                                               \
        pthread_mutex_lock(&g_tab);                                                                                    \
        for (__typeof__(head) *pp_ = &(head); *pp_; pp_ = &(*pp_)->next)                                               \
            if (*pp_ == (node)) { *pp_ = (node)->next; break; }                                                        \
        pthread_mutex_unlock(&g_tab);                                                                                  \
    } while (0)

/* ---- OpenXR state (one XR instance per process; all fields under X.mx) ------------------------------------------ */
#define XR_FUNCS(X)                                                                                                    \
    X(xrDestroyInstance) X(xrGetSystem) X(xrGetVulkanInstanceExtensionsKHR) X(xrGetVulkanDeviceExtensionsKHR)          \
    X(xrGetVulkanGraphicsDeviceKHR) X(xrGetVulkanGraphicsRequirementsKHR) X(xrEnumerateViewConfigurationViews)       \
    X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession) X(xrPollEvent)                          \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrEnumerateEnvironmentBlendModes) X(xrEnumerateSwapchainFormats)   \
    X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage)              \
    X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews)

enum { P_NONE, P_PENDING, P_DEAD, P_READY, P_INST, P_DEV, P_SESSION };
static const char *const phase_name[] = {"none", "pending", "dead", "ready", "instance", "device", "session"};

static struct {
    pthread_mutex_t mx;
    pthread_cond_t cv;
    int phase;
    int init_done, init_ok, abandoned;
    void *lib;
    PFN_xrGetInstanceProcAddr gipa;
    XrInstance inst;
    XrSystemId sys;
    XrGraphicsRequirementsVulkanKHR req;
    char inst_exts[4096], dev_exts[4096];
#define DECL_XFN(n) PFN_##n n;
    XR_FUNCS(DECL_XFN)
#undef DECL_XFN
    inst_st *is;
    dev_st *ds;
    VkQueue q;
    uint32_t qfamily;
    XrSession session;
    XrSpace space;
    int running;
    XrEnvironmentBlendMode blend;
    XrSwapchain sc;
    VkFormat sc_game_fmt, sc_fmt;
    uint32_t sc_w, sc_h, nimg;
    VkImage img[MAX_XR_IMAGES];
    somvr_xfer xfer;
    uint64_t frames, copies, nolayer;
    int warned_queue;
} X = {.mx = PTHREAD_MUTEX_INITIALIZER};

static void xr_global_init(void) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&X.cv, &a);
    pthread_condattr_destroy(&a);
}

static void xr_set_phase(int p) {
    if (X.phase != p) lg("xr: phase %s -> %s", phase_name[X.phase], phase_name[p]);
    X.phase = p;
}

/* Fill buf with a two-call OpenXR string getter. */
static XrResult xr_get_string(PFN_xrGetVulkanInstanceExtensionsKHR fn, char *buf, uint32_t cap) {
    uint32_t n = 0;
    XrResult r = fn(X.inst, X.sys, 0, &n, NULL);
    if (XR_FAILED(r)) return r;
    if (n > cap) return XR_ERROR_SIZE_INSUFFICIENT;
    r = fn(X.inst, X.sys, cap, &n, buf);
    buf[cap - 1] = 0;
    return r;
}

/* Runs on the helper thread: loader, instance, system, extension lists. Writes only fields nobody else reads before
 * init_done. Returns 0 on success. */
static int xr_init_work(void) {
    X.lib = dlopen(cfg.loader, RTLD_NOW | RTLD_LOCAL);
    if (!X.lib) {
        lg("xr: no OpenXR loader (%s): %s", cfg.loader, dlerror());
        return -1;
    }
    X.gipa = (PFN_xrGetInstanceProcAddr)dlsym(X.lib, "xrGetInstanceProcAddr");
    PFN_xrEnumerateInstanceExtensionProperties enum_ext = NULL;
    PFN_xrCreateInstance create = NULL;
    if (!X.gipa || XR_FAILED(X.gipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
                                    (PFN_xrVoidFunction *)&enum_ext)) ||
        XR_FAILED(X.gipa(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&create)) || !enum_ext || !create) {
        lg("xr: the loader has no xrGetInstanceProcAddr/xrCreateInstance");
        return -1;
    }
    uint32_t n = 0;
    XrResult r = enum_ext(NULL, 0, &n, NULL);
    XrExtensionProperties *props = n ? calloc(n, sizeof *props) : NULL;
    int have_vk = 0;
    if (XR_SUCCEEDED(r) && props) {
        for (uint32_t i = 0; i < n; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        r = enum_ext(NULL, n, &n, props);
        for (uint32_t i = 0; XR_SUCCEEDED(r) && i < n; i++)
            have_vk |= !strcmp(props[i].extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
    }
    free(props);
    if (XR_FAILED(r) || !have_vk) {
        lg("xr: runtime extensions: %s, XR_KHR_vulkan_enable %s", xr_str(r), have_vk ? "yes" : "missing");
        return -1;
    }
    const char *ext = XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
    XrInstanceCreateInfo ici = {.type = XR_TYPE_INSTANCE_CREATE_INFO,
                                .applicationInfo = {.applicationName = "ShadowOfMordorVR",
                                                    .applicationVersion = 1,
                                                    .engineName = "SoMVR vklayer",
                                                    .engineVersion = 1,
                                                    .apiVersion = XR_API_VERSION_1_0},
                                .enabledExtensionCount = 1,
                                .enabledExtensionNames = &ext};
    r = create(&ici, &X.inst);
    if (XR_FAILED(r)) {
        lg("xr: xrCreateInstance: %s (%d)", xr_str(r), r);
        X.inst = XR_NULL_HANDLE;
        return -1;
    }
#define LOAD_XFN(n)                                                                                                    \
    if (XR_FAILED(X.gipa(X.inst, #n, (PFN_xrVoidFunction *)&X.n)) || !X.n) {                                           \
        lg("xr: no %s", #n);                                                                                           \
        return -1;                                                                                                     \
    }
    XR_FUNCS(LOAD_XFN)
#undef LOAD_XFN
    XrSystemGetInfo sgi = {.type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    if (XR_FAILED(r = X.xrGetSystem(X.inst, &sgi, &X.sys))) {
        lg("xr: xrGetSystem(HMD): %s (headset not connected?)", xr_str(r));
        return -1;
    }
    X.req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
    if (XR_FAILED(r = X.xrGetVulkanGraphicsRequirementsKHR(X.inst, X.sys, &X.req))) {
        lg("xr: xrGetVulkanGraphicsRequirementsKHR: %s", xr_str(r));
        return -1;
    }
    if (XR_FAILED(r = xr_get_string(X.xrGetVulkanInstanceExtensionsKHR, X.inst_exts, sizeof X.inst_exts)) ||
        XR_FAILED(r = xr_get_string(X.xrGetVulkanDeviceExtensionsKHR, X.dev_exts, sizeof X.dev_exts))) {
        lg("xr: Vulkan extension lists: %s", xr_str(r));
        return -1;
    }
    XrViewConfigurationView vv[2] = {{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                     {.type = XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    n = 0;
    if (XR_SUCCEEDED(X.xrEnumerateViewConfigurationViews(X.inst, X.sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                                          &n, vv)) && n == 2)
        lg("xr: stereo views, recommended %ux%u per eye (max %ux%u)", vv[0].recommendedImageRectWidth,
           vv[0].recommendedImageRectHeight, vv[0].maxImageRectWidth, vv[0].maxImageRectHeight);
    lg("xr: system %llu; Vulkan %d.%d..%d.%d; instance exts \"%s\"; device exts \"%s\"", (unsigned long long)X.sys,
       (int)XR_VERSION_MAJOR(X.req.minApiVersionSupported), (int)XR_VERSION_MINOR(X.req.minApiVersionSupported),
       (int)XR_VERSION_MAJOR(X.req.maxApiVersionSupported), (int)XR_VERSION_MINOR(X.req.maxApiVersionSupported),
       X.inst_exts, X.dev_exts);
    return 0;
}

static void *xr_init_thread(void *arg) {
    (void)arg;
    int ok = xr_init_work() == 0;
    pthread_mutex_lock(&X.mx);
    X.init_ok = ok;
    X.init_done = 1;
    if ((!ok || X.abandoned) && X.inst) {
        if (X.xrDestroyInstance) X.xrDestroyInstance(X.inst);
        X.inst = XR_NULL_HANDLE;
    }
    if (X.abandoned) lg("xr: the abandoned init thread finished (%s); its instance is destroyed", ok ? "ok" : "failed");
    pthread_cond_signal(&X.cv);
    pthread_mutex_unlock(&X.mx);
    return NULL;
}

/* Called with X.mx held, phase P_NONE: run the init thread with a time limit. Leaves P_READY or P_DEAD. */
static void xr_init_locked(void) {
    pthread_t t;
    pthread_attr_t a;
    xr_set_phase(P_PENDING);
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    int e = pthread_create(&t, &a, xr_init_thread, NULL);
    pthread_attr_destroy(&a);
    if (e) {
        lg("xr: pthread_create: %s", strerror(e));
        xr_set_phase(P_DEAD);
        return;
    }
    struct timespec dl;
    clock_gettime(CLOCK_MONOTONIC, &dl);
    dl.tv_sec += cfg.timeout_ms / 1000;
    dl.tv_nsec += (long)(cfg.timeout_ms % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) dl.tv_sec++, dl.tv_nsec -= 1000000000L;
    while (!X.init_done)
        if (pthread_cond_timedwait(&X.cv, &X.mx, &dl) == ETIMEDOUT) break;
    if (!X.init_done) {
        X.abandoned = 1;
        lg("xr: the runtime did not answer within %d ms: abandoned, pass-through", cfg.timeout_ms);
        xr_set_phase(P_DEAD);
        return;
    }
    xr_set_phase(X.init_ok ? P_READY : P_DEAD);
}

/* ---- swapchain slots ------------------------------------------------------------------------------------------- */
static int slot_wait(dev_st *ds, slot_st *s) {
    if (!s->pending) return 0;
    VkResult r = ds->WaitForFences(ds->dev, 1, &s->fence, VK_TRUE, FENCE_TIMEOUT_NS);
    if (r != VK_SUCCESS) {
        lg("vk: vkWaitForFences(our copy): %d", r);
        return -1;
    }
    s->pending = 0;
    return 0;
}

static void sc_wait_all(dev_st *ds) {
    pthread_mutex_lock(&g_tab);
    sc_st *list = g_scs;
    pthread_mutex_unlock(&g_tab);
    for (sc_st *s = list; s; s = s->next)
        if (s->ds == ds)
            for (uint32_t i = 0; i < s->nimg; i++) slot_wait(ds, &s->slot[i]);
}

/* Free a swapchain's slots (X.mx held). keep_sems: the swapchain is going but presents may still wait on the
 * semaphores, so they go to the device's grave list instead. */
static void sc_free_slots(sc_st *s, int keep_sems) {
    dev_st *ds = s->ds;
    for (uint32_t i = 0; i < s->nimg; i++) {
        slot_st *sl = &s->slot[i];
        int idle = slot_wait(ds, sl) == 0;
        if (sl->cmd && idle) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &sl->cmd);
        if (sl->fence && idle) ds->DestroyFence(ds->dev, sl->fence, NULL);
        if (sl->sem) {
            VkSemaphore *g = keep_sems ? realloc(ds->grave, (ds->ngrave + 1) * sizeof *g) : NULL;
            if (g) {
                ds->grave = g;
                g[ds->ngrave++] = sl->sem;
            } else if (!keep_sems || idle) {
                ds->DestroySemaphore(ds->dev, sl->sem, NULL);
            }
        }
        memset(sl, 0, sizeof *sl);
    }
}

/* ---- XR session ------------------------------------------------------------------------------------------------- */
/* X.mx held. Wait for our copies, then destroy the XR swapchain, space and session. */
static void xr_teardown_session(void) {
    if (X.ds) sc_wait_all(X.ds);
    if (X.sc) X.xrDestroySwapchain(X.sc);
    if (X.space) X.xrDestroySpace(X.space);
    if (X.session) X.xrDestroySession(X.session);
    X.sc = XR_NULL_HANDLE;
    X.space = XR_NULL_HANDLE;
    X.session = XR_NULL_HANDLE;
    X.running = 0;
    X.nimg = 0;
    X.q = VK_NULL_HANDLE;
}

/* X.mx held: give up for the rest of the process. */
static void xr_fail(const char *what, XrResult r) {
    lg("xr: %s failed: %s (%d) -> pass-through from now on (frames %llu, copies %llu)", what, xr_str(r), r,
       (unsigned long long)X.frames, (unsigned long long)X.copies);
    xr_teardown_session();
    if (X.inst) X.xrDestroyInstance(X.inst);
    X.inst = XR_NULL_HANDLE;
    xr_set_phase(P_DEAD);
}

/* Is the runtime's VkPhysicalDevice (a loader trampoline handle) the GPU the game created its device on (a handle
 * one level down)? Uses the loader's exported entry points; never runs inside vkCreate* (loader lock). */
static int same_gpu(dev_st *ds, VkPhysicalDevice t) {
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
    PFN_vkGetInstanceProcAddr tg = h ? (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr") : NULL;
    PFN_vkEnumeratePhysicalDevices tenum =
        tg ? (PFN_vkEnumeratePhysicalDevices)tg(ds->is->inst, "vkEnumeratePhysicalDevices") : NULL;
    PFN_vkGetPhysicalDeviceProperties tprops =
        tg ? (PFN_vkGetPhysicalDeviceProperties)tg(ds->is->inst, "vkGetPhysicalDeviceProperties") : NULL;
    int ok = 0;
    if (tenum && tprops) {
        VkPhysicalDevice list[16];
        uint32_t n = 16, known = 0;
        VkResult r = tenum(ds->is->inst, &n, list);
        for (uint32_t i = 0; (r == VK_SUCCESS || r == VK_INCOMPLETE) && i < n; i++) known |= list[i] == t;
        if (known) {
            VkPhysicalDeviceProperties a, b;
            tprops(t, &a);
            ds->is->GetPhysicalDeviceProperties(ds->pd, &b);
            ok = a.vendorID == b.vendorID && a.deviceID == b.deviceID && !strcmp(a.deviceName, b.deviceName) &&
                 !memcmp(a.pipelineCacheUUID, b.pipelineCacheUUID, VK_UUID_SIZE);
            lg("xr: runtime GPU \"%s\" %04x:%04x, game GPU \"%s\" %04x:%04x -> %s", a.deviceName, a.vendorID,
               a.deviceID, b.deviceName, b.vendorID, b.deviceID, ok ? "same" : "DIFFERENT");
        } else {
            lg("xr: the runtime's VkPhysicalDevice %p is not one of the loader's %u", (void *)t, n);
        }
    } else {
        lg("xr: cannot reach the loader's vkGetInstanceProcAddr (libvulkan.so.1 not loaded?)");
    }
    if (h) dlclose(h);
    return ok;
}

static int xr_session_setup(dev_st *ds, q_st *qi) {
    VkPhysicalDevice t = VK_NULL_HANDLE;
    XrResult r = X.xrGetVulkanGraphicsDeviceKHR(X.inst, X.sys, ds->is->inst, &t);
    if (XR_FAILED(r)) return xr_fail("xrGetVulkanGraphicsDeviceKHR", r), -1;
    if (!same_gpu(ds, t)) return xr_fail("runtime GPU check", XR_ERROR_RUNTIME_FAILURE), -1;
    XrGraphicsBindingVulkanKHR b = {.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,
                                    .instance = ds->is->inst,
                                    .physicalDevice = t,
                                    .device = ds->dev,
                                    .queueFamilyIndex = qi->family,
                                    .queueIndex = qi->index};
    XrSessionCreateInfo sci = {.type = XR_TYPE_SESSION_CREATE_INFO, .next = &b, .systemId = X.sys};
    if (XR_FAILED(r = X.xrCreateSession(X.inst, &sci, &X.session))) {
        X.session = XR_NULL_HANDLE;
        return xr_fail("xrCreateSession", r), -1;
    }
    X.q = qi->q;
    X.qfamily = qi->family;
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                     .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
                                     .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
    if (XR_FAILED(r = X.xrCreateReferenceSpace(X.session, &rs, &X.space))) {
        X.space = XR_NULL_HANDLE;
        return xr_fail("xrCreateReferenceSpace(LOCAL)", r), -1;
    }
    XrEnvironmentBlendMode modes[8];
    uint32_t n = 0;
    r = X.xrEnumerateEnvironmentBlendModes(X.inst, X.sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &n, modes);
    if (XR_FAILED(r) || n == 0) return xr_fail("xrEnumerateEnvironmentBlendModes", r), -1;
    X.blend = modes[0];
    if (!ds->pool) {
        VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                                                VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                       .queueFamilyIndex = qi->family};
        VkResult vr = ds->CreateCommandPool(ds->dev, &pci, NULL, &ds->pool);
        if (vr != VK_SUCCESS) {
            ds->pool = VK_NULL_HANDLE;
            lg("vk: vkCreateCommandPool: %d", vr);
            return xr_fail("command pool", XR_ERROR_RUNTIME_FAILURE), -1;
        }
    }
    lg("xr: session created on queue family %u index %u (flags 0x%x), blend mode %d", qi->family, qi->index,
       qi->family < ds->nqf ? ds->qf[qi->family].queueFlags : 0, X.blend);
    xr_set_phase(P_SESSION);
    return 0;
}

/* Returns -1 when XR died. */
static int xr_poll(void) {
    for (;;) {
        XrEventDataBuffer ev = {.type = XR_TYPE_EVENT_DATA_BUFFER};
        XrResult r = X.xrPollEvent(X.inst, &ev);
        if (r == XR_EVENT_UNAVAILABLE) return 0;
        if (XR_FAILED(r)) return xr_fail("xrPollEvent", r), -1;
        if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) return xr_fail("instance loss pending", r), -1;
        if (ev.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            lg("xr: event %d", ev.type);
            continue;
        }
        XrSessionState st = ((const XrEventDataSessionStateChanged *)&ev)->state;
        lg("xr: session state %d", st);
        switch (somvr_on_session_state(st, X.running)) {
        case SOMVR_SA_BEGIN: {
            XrSessionBeginInfo bi = {.type = XR_TYPE_SESSION_BEGIN_INFO,
                                     .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
            if (XR_FAILED(r = X.xrBeginSession(X.session, &bi))) return xr_fail("xrBeginSession", r), -1;
            X.running = 1;
            lg("xr: session running");
            break;
        }
        case SOMVR_SA_END:
            X.running = 0;
            if (XR_FAILED(r = X.xrEndSession(X.session))) return xr_fail("xrEndSession", r), -1;
            lg("xr: session ended");
            break;
        case SOMVR_SA_LOST: return xr_fail("session exiting/lost (runtime state)", XR_SUCCESS), -1;
        case SOMVR_SA_NONE: break;
        }
    }
}

/* Make sure the XR swapchain matches the game swapchain (2w x h, a format for its format). -1: XR died. */
static int xr_swapchain_for(sc_st *s) {
    if (X.sc && X.sc_w == 2 * s->ext.width && X.sc_h == s->ext.height && X.sc_game_fmt == s->fmt) return 0;
    dev_st *ds = s->ds;
    if (X.sc) {
        sc_wait_all(ds);
        X.xrDestroySwapchain(X.sc);
        X.sc = XR_NULL_HANDLE;
        X.nimg = 0;
    }
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return xr_fail("xrEnumerateSwapchainFormats", r), -1;
    VkFormat f = VK_FORMAT_UNDEFINED;
    int gamma_ok = 0;
    somvr_xfer xfer = somvr_pick_format(s->fmt, fmts, n, &f, &gamma_ok);
    if (xfer == SOMVR_XFER_NONE) {
        lg("xr: no usable swapchain format for game format %d among %u", s->fmt, n);
        return xr_fail("swapchain format", XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED), -1;
    }
    if (xfer == SOMVR_XFER_BLIT) {
        VkFormatProperties a, b;
        ds->is->GetPhysicalDeviceFormatProperties(ds->pd, s->fmt, &a);
        ds->is->GetPhysicalDeviceFormatProperties(ds->pd, f, &b);
        int gfx = X.qfamily < ds->nqf && (ds->qf[X.qfamily].queueFlags & VK_QUEUE_GRAPHICS_BIT);
        if (!(a.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
            !(b.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) || !gfx)
            return xr_fail("blit support", XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED), -1;
    }
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                                              XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = 2 * s->ext.width,
                                .height = s->ext.height,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &X.sc))) {
        X.sc = XR_NULL_HANDLE;
        return xr_fail("xrCreateSwapchain", r), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++)
        im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(X.sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return xr_fail("xrEnumerateSwapchainImages", r), -1;
    for (uint32_t i = 0; i < n; i++) X.img[i] = im[i].image;
    X.nimg = n;
    X.sc_w = ci.width;
    X.sc_h = ci.height;
    X.sc_game_fmt = s->fmt;
    X.sc_fmt = f;
    X.xfer = xfer;
    lg("xr: swapchain %ux%u format %d (%u images) for game format %d: %s%s", ci.width, ci.height, f, n, s->fmt,
       xfer == SOMVR_XFER_COPY ? "copy" : "blit", gamma_ok ? "" : ", brightness will be off (no matching sRGB format)");
    return 0;
}

static void img_barrier(VkImageMemoryBarrier *b, VkImage img, VkAccessFlags sa, VkAccessFlags da, VkImageLayout ol,
                        VkImageLayout nl) {
    *b = (VkImageMemoryBarrier){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                .srcAccessMask = sa,
                                .dstAccessMask = da,
                                .oldLayout = ol,
                                .newLayout = nl,
                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .image = img,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
}

/* One submit on the present queue: wait the app's present semaphores, copy game image -> both halves of the XR
 * image, signal the slot's semaphore. Returns 1 if submitted (the app's semaphores are consumed), 0 if not. */
static int copy_submit(dev_st *ds, VkQueue q, sc_st *s, uint32_t idx, VkImage dst, const VkPresentInfoKHR *pi) {
    slot_st *sl = &s->slot[idx];
    if (!sl->cmd) {
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                          .commandPool = ds->pool,
                                          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                          .commandBufferCount = 1};
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkResult r = ds->AllocateCommandBuffers(ds->dev, &ai, &sl->cmd);
        if (r != VK_SUCCESS) sl->cmd = VK_NULL_HANDLE;
        if (r == VK_SUCCESS) {
            /* dispatchable object made by a layer: give it the loader's dispatch pointer */
            if (ds->set_loader_data) r = ds->set_loader_data(ds->dev, sl->cmd);
            else *(void **)sl->cmd = dkey(ds->dev);
        }
        if (r == VK_SUCCESS) r = ds->CreateFence(ds->dev, &fci, NULL, &sl->fence);
        if (r == VK_SUCCESS) r = ds->CreateSemaphore(ds->dev, &sci, NULL, &sl->sem);
        if (r != VK_SUCCESS) {
            lg("vk: slot %u objects: %d", idx, r);
            return 0;
        }
    }
    if (slot_wait(ds, sl)) return 0;
    VkResult r = ds->ResetFences(ds->dev, 1, &sl->fence);
    if (r == VK_SUCCESS) r = ds->ResetCommandBuffer(sl->cmd, 0);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (r == VK_SUCCESS) r = ds->BeginCommandBuffer(sl->cmd, &bi);
    if (r != VK_SUCCESS) {
        lg("vk: recording our copy: %d", r);
        return 0;
    }
    VkImage src = s->img[idx];
    VkImageMemoryBarrier b[2];
    img_barrier(&b[0], src, 0, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    /* the whole XR image is overwritten: its old contents (and layout) do not matter */
    img_barrier(&b[1], dst, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ds->CmdPipelineBarrier(sl->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                           NULL, 2, b);
    uint32_t w = s->ext.width, h = s->ext.height;
    VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    if (X.xfer == SOMVR_XFER_COPY) {
        VkImageCopy c[2];
        for (int e = 0; e < 2; e++)
            c[e] = (VkImageCopy){.srcSubresource = sub,
                                 .dstSubresource = sub,
                                 .dstOffset = {e ? (int32_t)w : 0, 0, 0},
                                 .extent = {w, h, 1}};
        ds->CmdCopyImage(sl->cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2,
                         c);
    } else {
        VkImageBlit bl[2];
        for (int e = 0; e < 2; e++) {
            int32_t x0 = e ? (int32_t)w : 0;
            bl[e] = (VkImageBlit){.srcSubresource = sub,
                                  .srcOffsets = {{0, 0, 0}, {(int32_t)w, (int32_t)h, 1}},
                                  .dstSubresource = sub,
                                  .dstOffsets = {{x0, 0, 0}, {x0 + (int32_t)w, (int32_t)h, 1}}};
        }
        ds->CmdBlitImage(sl->cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2,
                         bl, VK_FILTER_NEAREST);
    }
    img_barrier(&b[0], src, VK_ACCESS_TRANSFER_READ_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    /* XR_KHR_vulkan_enable: released images must be COLOR_ATTACHMENT_OPTIMAL */
    img_barrier(&b[1], dst, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ds->CmdPipelineBarrier(sl->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                           NULL, 2, b);
    if ((r = ds->EndCommandBuffer(sl->cmd)) != VK_SUCCESS) {
        lg("vk: vkEndCommandBuffer: %d", r);
        return 0;
    }
    VkPipelineStageFlags stages[16];
    uint32_t nw = pi->waitSemaphoreCount;
    if (nw > 16) {
        lg("vk: %u present wait semaphores (max 16)", nw);
        return 0;
    }
    for (uint32_t i = 0; i < nw; i++) stages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                       .waitSemaphoreCount = nw,
                       .pWaitSemaphores = pi->pWaitSemaphores,
                       .pWaitDstStageMask = stages,
                       .commandBufferCount = 1,
                       .pCommandBuffers = &sl->cmd,
                       .signalSemaphoreCount = 1,
                       .pSignalSemaphores = &sl->sem};
    if ((r = ds->QueueSubmit(q, 1, &si, sl->fence)) != VK_SUCCESS) {
        lg("vk: vkQueueSubmit(our copy): %d", r);
        return 0;
    }
    sl->pending = 1;
    return 1;
}

/* X.mx held, X.ds == ds. Returns 1 if our submit consumed the app's semaphores (*sem = what the present must wait). */
static int xr_frame(dev_st *ds, q_st *qi, const VkPresentInfoKHR *pi, VkSemaphore *sem) {
    if (X.phase == P_DEV && xr_session_setup(ds, qi)) return 0;
    if (X.phase != P_SESSION) return 0;
    if (qi->q != X.q) {
        if (!X.warned_queue++)
            lg("xr: a present on another queue (%p, session queue %p): passed through", (void *)qi->q, (void *)X.q);
        return 0;
    }
    if (xr_poll() || !X.running) return 0;
    sc_st *s = LIST_FIND(g_scs, sc, pi->pSwapchains[0]);
    uint32_t idx = pi->pImageIndices[0];
    int can_copy = s && s->copyable && idx < s->nimg;
    if (can_copy && xr_swapchain_for(s)) return 0;
    XrFrameWaitInfo wi = {.type = XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs = {.type = XR_TYPE_FRAME_STATE};
    XrResult r = X.xrWaitFrame(X.session, &wi, &fs);
    if (XR_FAILED(r)) return xr_fail("xrWaitFrame", r), 0;
    XrFrameBeginInfo fbi = {.type = XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(r = X.xrBeginFrame(X.session, &fbi))) return xr_fail("xrBeginFrame", r), 0;

    int replaced = 0;
    if (fs.shouldRender && can_copy) {
        uint32_t xi = 0;
        XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
        XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(r = X.xrAcquireSwapchainImage(X.sc, &ai, &xi)) || xi >= X.nimg)
            return xr_fail("xrAcquireSwapchainImage", r), 0;
        if ((r = X.xrWaitSwapchainImage(X.sc, &wti)) != XR_SUCCESS) return xr_fail("xrWaitSwapchainImage", r), 0;
        replaced = copy_submit(ds, qi->q, s, idx, X.img[xi], pi);
        if (replaced) *sem = s->slot[idx].sem;
        if (XR_FAILED(r = X.xrReleaseSwapchainImage(X.sc, &ri))) return xr_fail("xrReleaseSwapchainImage", r), replaced;
    }

    const XrCompositionLayerBaseHeader *layers[1];
    uint32_t nl = 0;
    XrCompositionLayerQuad quad;
    XrCompositionLayerProjection proj;
    XrCompositionLayerProjectionView pv[2];
    uint32_t w = s ? s->ext.width : 0, h = s ? s->ext.height : 0;
    if (replaced && cfg.layer == SOMVR_LAYER_QUAD) {
        int32_t rc[4];
        float sz[2];
        somvr_eye_rect(w, h, 0, rc);
        somvr_quad_size(w, h, QUAD_WIDTH_M, sz);
        quad = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                        .space = X.space,
                                        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                        .subImage = {X.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                        .pose = {{0, 0, 0, 1}, {0, 0, -QUAD_DIST_M}},
                                        .size = {sz[0], sz[1]}};
        layers[nl++] = (const XrCompositionLayerBaseHeader *)&quad;
    } else if (replaced) {
        XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                               .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                               .displayTime = fs.predictedDisplayTime,
                               .space = X.space};
        XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
        XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
        uint32_t nv = 0;
        const XrViewStateFlags need = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
        if (XR_SUCCEEDED(r) && nv == 2 && (vs.viewStateFlags & need) == need) {
            for (int e = 0; e < 2; e++) {
                int32_t rc[4];
                somvr_eye_rect(w, h, e, rc);
                pv[e] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                                                           .pose = v[e].pose,
                                                           .fov = v[e].fov,
                                                           .subImage = {X.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0}};
            }
            proj = (XrCompositionLayerProjection){
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION, .space = X.space, .viewCount = 2, .views = pv};
            layers[nl++] = (const XrCompositionLayerBaseHeader *)&proj;
        }
    }
    XrFrameEndInfo ei = {.type = XR_TYPE_FRAME_END_INFO,
                         .displayTime = fs.predictedDisplayTime,
                         .environmentBlendMode = X.blend,
                         .layerCount = nl,
                         .layers = nl ? layers : NULL};
    if (XR_FAILED(r = X.xrEndFrame(X.session, &ei))) return xr_fail("xrEndFrame", r), replaced;
    X.frames++;
    X.copies += (uint64_t)replaced;
    X.nolayer += nl == 0;
    if (X.frames <= 3 || X.frames % 600 == 0)
        lg("xr: frame %llu: shouldRender %d, game image %u, copied %d, layers %u (copies %llu, empty frames %llu)",
           (unsigned long long)X.frames, (int)fs.shouldRender, idx, replaced, nl, (unsigned long long)X.copies,
           (unsigned long long)X.nolayer);
    if (!replaced && can_copy && fs.shouldRender) return xr_fail("our copy submit", XR_ERROR_RUNTIME_FAILURE), 0;
    return replaced;
}

/* ---- the hooks ------------------------------------------------------------------------------------------------ */
static VKAPI_ATTR VkResult VKAPI_CALL L_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    q_st *qi = LIST_FIND(g_queues, q, q);
    dev_st *ds = qi ? qi->ds : LIST_FIND(g_devs, key, dkey(q));
    if (!ds) return VK_ERROR_DEVICE_LOST; /* unreachable: the device is always known */
    VkSemaphore sem = VK_NULL_HANDLE;
    int replaced = 0;
    if (qi && pi && pi->swapchainCount) {
        pthread_mutex_lock(&X.mx);
        if (X.ds == ds && (X.phase == P_DEV || X.phase == P_SESSION)) replaced = xr_frame(ds, qi, pi, &sem);
        pthread_mutex_unlock(&X.mx);
    }
    if (!replaced) return ds->QueuePresentKHR(q, pi);
    VkPresentInfoKHR m = *pi;
    m.waitSemaphoreCount = 1;
    m.pWaitSemaphores = &sem;
    return ds->QueuePresentKHR(q, &m);
}

static void remember_queue(dev_st *ds, uint32_t family, uint32_t index, VkQueue q) {
    if (!q || LIST_FIND(g_queues, q, q)) return;
    q_st *n = calloc(1, sizeof *n);
    if (!n) return;
    *n = (q_st){.q = q, .ds = ds, .family = family, .index = index};
    LIST_ADD(g_queues, n);
    lg("vk: queue %p = family %u index %u", (void *)q, family, index);
}

static VKAPI_ATTR void VKAPI_CALL L_GetDeviceQueue(VkDevice dev, uint32_t family, uint32_t index, VkQueue *q) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds) return;
    ds->GetDeviceQueue(dev, family, index, q);
    if (ds->xr && q) remember_queue(ds, family, index, *q);
}

static VKAPI_ATTR void VKAPI_CALL L_GetDeviceQueue2(VkDevice dev, const VkDeviceQueueInfo2 *info, VkQueue *q) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds || !ds->GetDeviceQueue2) return;
    ds->GetDeviceQueue2(dev, info, q);
    if (ds->xr && q && info && !info->flags) remember_queue(ds, info->queueFamilyIndex, info->queueIndex, *q);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateSwapchainKHR(VkDevice dev, const VkSwapchainCreateInfoKHR *ci,
                                                           const VkAllocationCallbacks *alloc, VkSwapchainKHR *out) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds) return VK_ERROR_INITIALIZATION_FAILED;
    if (!ds->xr) return ds->CreateSwapchainKHR(dev, ci, alloc, out);
    VkSwapchainCreateInfoKHR m = *ci;
    m.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    int copyable = 1;
    VkResult r = ds->CreateSwapchainKHR(dev, &m, alloc, out);
    if (r != VK_SUCCESS && !(ci->imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        lg("vk: vkCreateSwapchainKHR with TRANSFER_SRC: %d; retry as the app asked (no XR copy)", r);
        copyable = 0;
        r = ds->CreateSwapchainKHR(dev, ci, alloc, out);
    }
    if (r != VK_SUCCESS) return r;
    sc_st *s = calloc(1, sizeof *s);
    if (!s) return r;
    *s = (sc_st){.sc = *out, .ds = ds, .fmt = ci->imageFormat, .ext = ci->imageExtent, .copyable = copyable};
    uint32_t n = 0;
    if (ds->GetSwapchainImagesKHR(dev, *out, &n, NULL) != VK_SUCCESS || n > MAX_SC_IMAGES ||
        ds->GetSwapchainImagesKHR(dev, *out, &n, s->img) != VK_SUCCESS)
        s->copyable = 0;
    else
        s->nimg = n;
    lg("vk: swapchain %#llx %ux%u format %d usage 0x%x->0x%x present mode %d, %u images%s",
       (unsigned long long)(uintptr_t)*out, ci->imageExtent.width, ci->imageExtent.height, ci->imageFormat,
       ci->imageUsage, copyable ? m.imageUsage : ci->imageUsage, ci->presentMode, n, s->copyable ? "" : " (no copy)");
    LIST_ADD(g_scs, s);
    return r;
}

static VKAPI_ATTR void VKAPI_CALL L_DestroySwapchainKHR(VkDevice dev, VkSwapchainKHR sc,
                                                        const VkAllocationCallbacks *alloc) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds) return;
    sc_st *s = sc ? LIST_FIND(g_scs, sc, sc) : NULL;
    if (s) {
        pthread_mutex_lock(&X.mx);
        sc_free_slots(s, 1);
        pthread_mutex_unlock(&X.mx);
        LIST_DEL(g_scs, s);
        free(s);
    }
    ds->DestroySwapchainKHR(dev, sc, alloc);
}

static VKAPI_ATTR void VKAPI_CALL L_DestroyDevice(VkDevice dev, const VkAllocationCallbacks *alloc) {
    dev_st *ds = dev ? LIST_FIND(g_devs, key, dkey(dev)) : NULL;
    if (!ds) return;
    pthread_mutex_lock(&X.mx);
    if (X.ds == ds) {
        lg("xr: vkDestroyDevice: session down (frames %llu, copies %llu)", (unsigned long long)X.frames,
           (unsigned long long)X.copies);
        xr_teardown_session();
        X.ds = NULL;
        if (X.phase == P_DEV || X.phase == P_SESSION) xr_set_phase(P_INST);
    }
    for (;;) { /* the app destroys the device: all its work is finished */
        sc_st *s;
        pthread_mutex_lock(&g_tab);
        for (s = g_scs; s && s->ds != ds; s = s->next) {}
        pthread_mutex_unlock(&g_tab);
        if (!s) break;
        sc_free_slots(s, 0);
        LIST_DEL(g_scs, s);
        free(s);
    }
    for (uint32_t i = 0; i < ds->ngrave; i++) ds->DestroySemaphore(ds->dev, ds->grave[i], NULL);
    free(ds->grave);
    if (ds->pool) ds->DestroyCommandPool(ds->dev, ds->pool, NULL);
    pthread_mutex_unlock(&X.mx);
    for (;;) {
        q_st *qn;
        pthread_mutex_lock(&g_tab);
        for (qn = g_queues; qn && qn->ds != ds; qn = qn->next) {}
        pthread_mutex_unlock(&g_tab);
        if (!qn) break;
        LIST_DEL(g_queues, qn);
        free(qn);
    }
    PFN_vkDestroyDevice destroy = ds->DestroyDevice;
    LIST_DEL(g_devs, ds);
    free(ds);
    destroy(dev, alloc);
}

static VkLayerDeviceCreateInfo *dev_chain(const VkDeviceCreateInfo *ci, VkLayerFunction f) {
    for (const VkBaseInStructure *p = ci->pNext; p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && ((const VkLayerDeviceCreateInfo *)p)->function == f)
            return (VkLayerDeviceCreateInfo *)(uintptr_t)p;
    return NULL;
}

static VkLayerInstanceCreateInfo *inst_chain(const VkInstanceCreateInfo *ci, VkLayerFunction f) {
    for (const VkBaseInStructure *p = ci->pNext; p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            ((const VkLayerInstanceCreateInfo *)p)->function == f)
            return (VkLayerInstanceCreateInfo *)(uintptr_t)p;
    return NULL;
}

static void log_exts(const char *what, uint32_t n, const char *const *names) {
    char line[2048];
    size_t u = 0;
    line[0] = 0;
    for (uint32_t i = 0; i < n && u < sizeof line - 1; i++)
        u += (size_t)snprintf(line + u, sizeof line - u, "%s%s", i ? " " : "", names[i]);
    lg("vk: %s (%u): %s", what, n, line);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
                                                     const VkAllocationCallbacks *alloc, VkDevice *out) {
    init();
    inst_st *is = LIST_FIND(g_insts, key, dkey(pd));
    VkLayerDeviceCreateInfo *link = dev_chain(ci, VK_LAYER_LINK_INFO);
    VkLayerDeviceCreateInfo *cb = dev_chain(ci, VK_LOADER_DATA_CALLBACK);
    if (!is || !link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkLayerDeviceLink *mine = link->u.pLayerInfo;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(is->inst, "vkCreateDevice");
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;

    pthread_mutex_lock(&X.mx);
    int use_xr = X.phase == P_INST && X.is == is;
    somvr_extlist *el = use_xr ? malloc(sizeof *el) : NULL;
    VkDeviceCreateInfo m = *ci;
    if (use_xr) {
        uint32_t n = 0;
        VkExtensionProperties *props = NULL;
        VkResult r = is->EnumerateDeviceExtensionProperties(pd, NULL, &n, NULL);
        if (r == VK_SUCCESS && n) props = calloc(n, sizeof *props);
        if (props) r = is->EnumerateDeviceExtensionProperties(pd, NULL, &n, props);
        char miss[VK_MAX_EXTENSION_NAME_SIZE];
        const char *missing = props && r == VK_SUCCESS ? somvr_first_unsupported(X.dev_exts, props, n, miss, sizeof miss)
                                                       : "(extension query failed)";
        free(props);
        if (!el || missing) {
            lg("xr: the GPU lacks the runtime's device extension %s -> pass-through", missing ? missing : "(no memory)");
            use_xr = 0;
        } else if (somvr_extl_init(el, ci->ppEnabledExtensionNames, ci->enabledExtensionCount) ||
                   somvr_extl_add_spaced(el, X.dev_exts) < 0) {
            lg("xr: device extension list too long -> pass-through");
            use_xr = 0;
        } else {
            m.enabledExtensionCount = el->count;
            m.ppEnabledExtensionNames = el->names;
            log_exts("device extensions, app", ci->enabledExtensionCount, ci->ppEnabledExtensionNames);
            log_exts("device extensions, with XR", el->count, el->names);
        }
        if (!use_xr) xr_fail("device extensions", XR_ERROR_RUNTIME_FAILURE);
    }
    VkResult r = create(pd, use_xr ? &m : ci, alloc, out);
    if (r != VK_SUCCESS && use_xr) {
        lg("vk: vkCreateDevice with the XR extensions: %d; retry as the app asked", r);
        xr_fail("vkCreateDevice with the XR extensions", XR_ERROR_RUNTIME_FAILURE);
        use_xr = 0;
        link->u.pLayerInfo = mine;
        r = create(pd, ci, alloc, out);
    }
    free(el);
    dev_st *ds = r == VK_SUCCESS ? calloc(1, sizeof *ds) : NULL;
    if (ds) {
        ds->dev = *out;
        ds->key = dkey(*out);
        ds->pd = pd;
        ds->is = is;
        ds->gdpa = gdpa;
        ds->set_loader_data = cb ? cb->u.pfnSetDeviceLoaderData : NULL;
        ds->xr = use_xr;
#define LOAD_FN(n) ds->n = (PFN_vk##n)gdpa(*out, "vk" #n);
        DEV_FUNCS(LOAD_FN)
#undef LOAD_FN
        ds->nqf = 16;
        is->GetPhysicalDeviceQueueFamilyProperties(pd, &ds->nqf, ds->qf);
        if (use_xr && (!ds->QueueSubmit || !ds->CmdCopyImage || !ds->QueuePresentKHR || !ds->CreateSwapchainKHR)) {
            xr_fail("device functions", XR_ERROR_RUNTIME_FAILURE);
            ds->xr = 0;
        }
        LIST_ADD(g_devs, ds);
        if (ds->xr) {
            X.ds = ds;
            xr_set_phase(P_DEV);
        }
        lg("vk: device %p created (XR %s)", (void *)*out, ds->xr ? "on" : "off");
    } else if (r == VK_SUCCESS) {
        lg("vk: out of memory for the device record");
    }
    pthread_mutex_unlock(&X.mx);
    return r;
}

static VKAPI_ATTR void VKAPI_CALL L_DestroyInstance(VkInstance inst, const VkAllocationCallbacks *alloc) {
    inst_st *is = inst ? LIST_FIND(g_insts, key, dkey(inst)) : NULL;
    if (!is) return;
    pthread_mutex_lock(&X.mx);
    if (X.ds && X.ds->is == is) { /* the app skipped vkDestroyDevice: no session on a dead instance */
        xr_teardown_session();
        X.ds = NULL;
        if (X.phase == P_DEV || X.phase == P_SESSION) xr_set_phase(P_INST);
    }
    if (X.is == is) {
        X.is = NULL;
        if (X.phase == P_INST) xr_set_phase(P_READY); /* e.g. a probe instance: the next one may take XR */
    }
    pthread_mutex_unlock(&X.mx);
    PFN_vkDestroyInstance destroy = is->DestroyInstance;
    LIST_DEL(g_insts, is);
    free(is);
    destroy(inst, alloc);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateInstance(const VkInstanceCreateInfo *ci,
                                                       const VkAllocationCallbacks *alloc, VkInstance *out) {
    init();
    VkLayerInstanceCreateInfo *link = inst_chain(ci, VK_LAYER_LINK_INFO);
    if (!link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkLayerInstanceLink *mine = link->u.pLayerInfo;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;

    const VkApplicationInfo *ai = ci->pApplicationInfo;
    const char *engine = ai && ai->pEngineName ? ai->pEngineName : "";
    uint32_t api = ai && ai->apiVersion ? ai->apiVersion : VK_API_VERSION_1_0;
    int eligible = cfg.mode == SOMVR_MODE_FORCE || (cfg.mode == SOMVR_MODE_AUTO && !strcmp(engine, "Feral3D"));
    lg("vk: vkCreateInstance app \"%s\" engine \"%s\" api %u.%u -> %s",
       ai && ai->pApplicationName ? ai->pApplicationName : "", engine, VK_API_VERSION_MAJOR(api),
       VK_API_VERSION_MINOR(api),
       cfg.mode == SOMVR_MODE_OFF ? "pass-through (SOMVR_XR=0)"
       : eligible                 ? "XR candidate"
                                  : "pass-through (not Feral3D)");

    pthread_mutex_lock(&X.mx);
    if (eligible && X.phase == P_NONE) xr_init_locked();
    int use_xr = eligible && X.phase == P_READY;
    somvr_extlist *el = use_xr ? malloc(sizeof *el) : NULL;
    VkInstanceCreateInfo m = *ci;
    VkApplicationInfo mai;
    if (use_xr) {
        if (!el || somvr_extl_init(el, ci->ppEnabledExtensionNames, ci->enabledExtensionCount) ||
            somvr_extl_add_spaced(el, X.inst_exts) < 0) {
            xr_fail("instance extension list", XR_ERROR_RUNTIME_FAILURE);
            use_xr = 0;
        } else {
            m.enabledExtensionCount = el->count;
            m.ppEnabledExtensionNames = el->names;
            uint32_t minv = VK_MAKE_API_VERSION(0, (uint32_t)XR_VERSION_MAJOR(X.req.minApiVersionSupported),
                                                (uint32_t)XR_VERSION_MINOR(X.req.minApiVersionSupported), 0);
            if (api < minv) {
                mai = ai ? *ai : (VkApplicationInfo){.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
                mai.apiVersion = minv;
                m.pApplicationInfo = &mai;
                lg("vk: apiVersion raised to %u.%u for the runtime", VK_API_VERSION_MAJOR(minv),
                   VK_API_VERSION_MINOR(minv));
            }
            log_exts("instance extensions, app", ci->enabledExtensionCount, ci->ppEnabledExtensionNames);
            log_exts("instance extensions, with XR", el->count, el->names);
        }
    }
    VkResult r = create(use_xr ? &m : ci, alloc, out);
    if (r != VK_SUCCESS && use_xr) {
        lg("vk: vkCreateInstance with the XR extensions: %d; retry as the app asked", r);
        xr_fail("vkCreateInstance with the XR extensions", XR_ERROR_RUNTIME_FAILURE);
        use_xr = 0;
        link->u.pLayerInfo = mine;
        r = create(ci, alloc, out);
    }
    free(el);
    inst_st *is = r == VK_SUCCESS ? calloc(1, sizeof *is) : NULL;
    if (is) {
        is->inst = *out;
        is->key = dkey(*out);
        is->gipa = gipa;
        is->DestroyInstance = (PFN_vkDestroyInstance)gipa(*out, "vkDestroyInstance");
        is->EnumerateDeviceExtensionProperties =
            (PFN_vkEnumerateDeviceExtensionProperties)gipa(*out, "vkEnumerateDeviceExtensionProperties");
        is->GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gipa(*out, "vkGetPhysicalDeviceProperties");
        is->GetPhysicalDeviceQueueFamilyProperties =
            (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(*out, "vkGetPhysicalDeviceQueueFamilyProperties");
        is->GetPhysicalDeviceFormatProperties =
            (PFN_vkGetPhysicalDeviceFormatProperties)gipa(*out, "vkGetPhysicalDeviceFormatProperties");
        LIST_ADD(g_insts, is);
        if (use_xr) {
            X.is = is;
            xr_set_phase(P_INST);
        }
    } else if (r == VK_SUCCESS) {
        /* cannot forward anything without the record: undo and fail rather than crash later */
        PFN_vkDestroyInstance d = (PFN_vkDestroyInstance)gipa(*out, "vkDestroyInstance");
        if (d) d(*out, alloc);
        if (use_xr) xr_fail("instance record", XR_ERROR_OUT_OF_MEMORY);
        r = VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    pthread_mutex_unlock(&X.mx);
    return r;
}

/* ---- entry points (internal references use the static versions: never the global symbol, which could be the
 * loader's own vkGetInstanceProcAddr if libvulkan sits in the global scope; also linked with -Bsymbolic) ---------- */
static PFN_vkVoidFunction dev_hook(const char *n) {
    static const struct {
        const char *n;
        PFN_vkVoidFunction f;
    } t[] = {
        {"vkQueuePresentKHR", (PFN_vkVoidFunction)L_QueuePresentKHR},
        {"vkGetDeviceQueue", (PFN_vkVoidFunction)L_GetDeviceQueue},
        {"vkGetDeviceQueue2", (PFN_vkVoidFunction)L_GetDeviceQueue2},
        {"vkCreateSwapchainKHR", (PFN_vkVoidFunction)L_CreateSwapchainKHR},
        {"vkDestroySwapchainKHR", (PFN_vkVoidFunction)L_DestroySwapchainKHR},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(n, t[i].n)) return t[i].f;
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL L_GetDeviceProcAddr(VkDevice dev, const char *name) {
    if (!name) return NULL;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)L_GetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)L_DestroyDevice;
    dev_st *ds = dev ? LIST_FIND(g_devs, key, dkey(dev)) : NULL;
    if (!ds) return NULL;
    PFN_vkVoidFunction f = ds->xr ? dev_hook(name) : NULL;
    if (f && !ds->gdpa(dev, name)) return NULL; /* not enabled below us (e.g. vkGetDeviceQueue2 on 1.0) */
    return f ? f : ds->gdpa(dev, name);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL L_GetInstanceProcAddr(VkInstance inst, const char *name) {
    init();
    if (!name) return NULL;
    static const struct {
        const char *n;
        PFN_vkVoidFunction f;
    } t[] = {
        {"vkGetInstanceProcAddr", (PFN_vkVoidFunction)L_GetInstanceProcAddr},
        {"vkCreateInstance", (PFN_vkVoidFunction)L_CreateInstance},
        {"vkDestroyInstance", (PFN_vkVoidFunction)L_DestroyInstance},
        {"vkCreateDevice", (PFN_vkVoidFunction)L_CreateDevice},
        {"vkGetDeviceProcAddr", (PFN_vkVoidFunction)L_GetDeviceProcAddr},
        {"vkDestroyDevice", (PFN_vkVoidFunction)L_DestroyDevice},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(name, t[i].n)) return t[i].f;
    if (cfg.mode != SOMVR_MODE_OFF) {
        PFN_vkVoidFunction f = dev_hook(name); /* each hook forwards for devices without XR */
        if (f) return f;
    }
    inst_st *is = inst ? LIST_FIND(g_insts, key, dkey(inst)) : NULL;
    return is ? is->gipa(inst, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst, const char *name);
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *name);
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst, const char *name) {
    return L_GetInstanceProcAddr(inst, name);
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *name) {
    return L_GetDeviceProcAddr(dev, name);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *p) {
    init();
    if (!p || p->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || p->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    p->loaderLayerInterfaceVersion = 2;
    p->pfnGetInstanceProcAddr = L_GetInstanceProcAddr;
    p->pfnGetDeviceProcAddr = L_GetDeviceProcAddr;
    p->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
