/* test_layer.c - drives the built layer the way the Vulkan loader and the game would: a fake driver below the layer
 * (it checks fence, semaphore and command-buffer use), mock_xr.so as the OpenXR loader + runtime + loader trampoline.
 * Every scenario runs in its own forked process with its own environment. No GPU, no XR runtime.
 *   test_layer LAYER.so MOCK.so TMPDIR [scenario]
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <openxr/openxr.h>

#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "mock_common.h"

static const char *g_layer, *g_mock, *g_tmp;
static char g_log[600];
static int g_fail;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            g_fail++;                                                                                                  \
            fprintf(stderr, "    FAIL line %d: ", __LINE__);                                                           \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fputc('\n', stderr);                                                                                       \
        }                                                                                                              \
    } while (0)

/* ---- fake driver ------------------------------------------------------------------------------------------------ */
#define ICD_MAGIC ((void *)(uintptr_t)0x01CDC0DE)
#define MAXH 512
#define H(id) ((void *)(uintptr_t)(0x100000u + (id)))
#define HID(h) ((int)((uintptr_t)(h) - 0x100000u))
#define APP_SEM(i) ((VkSemaphore)(uintptr_t)(0xA000u + (i)))
#define IS_APP_SEM(s) ((uintptr_t)(s) >= 0xA000u && (uintptr_t)(s) < 0xA100u)
#define GAME_IMG(i) ((VkImage)(uintptr_t)(0x7000u + (i)))
typedef struct { void *disp; } fobj;
static int key_inst[4], key_dev;
static fobj o_inst[4], o_pd[4], o_dev, o_queue, o_cmd[64];
static int cur_inst;

static struct {
    int fail_inst_with_xr, fail_dev_with_xr, dev_lacks_fence_fd, submit_fail_at, sc_rejects_xfer_src;
    VkFormat game_fmt;
    int inst_creates, dev_creates, sc_creates;
    uint32_t inst_n, dev_n, inst_api;
    char inst_list[2048], dev_list[2048];
    VkImageUsageFlags sc_usage;
    int submits, presents, copies, blits, fence_waits, set_loader_data;
    uint32_t submit_nwait, present_nwait;
    VkSemaphore submit_wait[8], submit_signal, present_wait[8];
    VkPipelineStageFlags submit_stage;
    VkImageCopy copy[2];
    uint32_t copy_n;
    VkImage copy_src, copy_dst;
    struct { VkImage img; VkImageLayout o, n; } tr[8];
    int ntr;
    int viol;
    char first[256];
    int next_id;
    uint8_t fence[MAXH], sem[MAXH], app_used[256];
    int cmd_fence[64], ncmd;
    int fences_live, sems_live, pool_live;
} V;

static void vviol(const char *m) {
    if (!V.viol++) snprintf(V.first, sizeof V.first, "%s", m);
}
static void list_names(char *out, size_t sz, uint32_t n, const char *const *names) {
    out[0] = 0;
    for (uint32_t i = 0; i < n; i++) {
        strncat(out, " ", sz - strlen(out) - 1);
        strncat(out, names[i], sz - strlen(out) - 1);
    }
    strncat(out, " ", sz - strlen(out) - 1);
}
static int has_name(const char *list, const char *n) {
    char k[256];
    snprintf(k, sizeof k, " %s ", n);
    return strstr(list, k) != NULL;
}
static int count_name(const char *list, const char *n) {
    char k[256];
    int c = 0;
    snprintf(k, sizeof k, " %s ", n);
    for (const char *p = list; (p = strstr(p, k)); p++) c++;
    return c;
}

static VKAPI_ATTR VkResult VKAPI_CALL d_CreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a,
                                                       VkInstance *out) {
    (void)a;
    for (const VkBaseInStructure *p = ci->pNext; p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            ((const VkLayerInstanceCreateInfo *)p)->function == VK_LAYER_LINK_INFO &&
            ((const VkLayerInstanceCreateInfo *)p)->u.pLayerInfo != NULL)
            vviol("instance chain link not advanced by the layer");
    V.inst_creates++;
    V.inst_n = ci->enabledExtensionCount;
    V.inst_api = ci->pApplicationInfo ? ci->pApplicationInfo->apiVersion : 0;
    list_names(V.inst_list, sizeof V.inst_list, ci->enabledExtensionCount, ci->ppEnabledExtensionNames);
    if (V.fail_inst_with_xr && has_name(V.inst_list, "VK_KHR_external_fence_capabilities"))
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    o_inst[cur_inst].disp = &key_inst[cur_inst];
    o_pd[cur_inst].disp = &key_inst[cur_inst];
    *out = (VkInstance)&o_inst[cur_inst];
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroyInstance(VkInstance i, const VkAllocationCallbacks *a) { (void)i, (void)a; }
static VKAPI_ATTR VkResult VKAPI_CALL d_EnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char *l,
                                                                           uint32_t *n, VkExtensionProperties *p) {
    (void)pd, (void)l;
    static const char *const have[] = {
        "VK_KHR_swapchain", "VK_KHR_maintenance1", "VK_KHR_maintenance2", "VK_KHR_dedicated_allocation",
        "VK_KHR_get_memory_requirements2", "VK_KHR_shader_draw_parameters", "VK_KHR_external_fence",
        "VK_KHR_external_memory", "VK_KHR_external_semaphore", "VK_KHR_image_format_list", "VK_KHR_external_memory_fd",
        "VK_KHR_external_semaphore_fd", "VK_KHR_external_fence_fd"};
    uint32_t total = sizeof have / sizeof have[0] - (V.dev_lacks_fence_fd ? 1 : 0);
    if (!p) return *n = total, VK_SUCCESS;
    uint32_t k = *n < total ? *n : total;
    for (uint32_t i = 0; i < k; i++) {
        memset(&p[i], 0, sizeof p[i]);
        snprintf(p[i].extensionName, sizeof p[i].extensionName, "%s", have[i]);
    }
    *n = k;
    return k < total ? VK_INCOMPLETE : VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_GetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
    (void)pd;
    memset(p, 0, sizeof *p);
    p->vendorID = MOCK_VENDOR;
    p->deviceID = MOCK_DEVICE;
    snprintf(p->deviceName, sizeof p->deviceName, "%s", MOCK_GPU_NAME);
    memset(p->pipelineCacheUUID, MOCK_UUID_BYTE, VK_UUID_SIZE);
}
static VKAPI_ATTR void VKAPI_CALL d_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd, uint32_t *n,
                                                                          VkQueueFamilyProperties *p) {
    (void)pd;
    if (p && *n) p[0] = (VkQueueFamilyProperties){.queueFlags = 7, .queueCount = 1, .minImageTransferGranularity = {1, 1, 1}};
    *n = 1;
}
static VKAPI_ATTR void VKAPI_CALL d_GetPhysicalDeviceFormatProperties(VkPhysicalDevice pd, VkFormat f,
                                                                     VkFormatProperties *p) {
    (void)pd, (void)f;
    p->linearTilingFeatures = 0;
    p->optimalTilingFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    p->bufferFeatures = 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
                                                     const VkAllocationCallbacks *a, VkDevice *out) {
    (void)pd, (void)a;
    V.dev_creates++;
    V.dev_n = ci->enabledExtensionCount;
    list_names(V.dev_list, sizeof V.dev_list, ci->enabledExtensionCount, ci->ppEnabledExtensionNames);
    if (V.fail_dev_with_xr && has_name(V.dev_list, "VK_KHR_external_memory_fd")) return VK_ERROR_EXTENSION_NOT_PRESENT;
    o_dev.disp = &key_dev; /* the loader's terminator sets the dispatch pointer */
    *out = (VkDevice)&o_dev;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroyDevice(VkDevice d, const VkAllocationCallbacks *a) {
    (void)d, (void)a;
    if (V.fences_live || V.sems_live || V.pool_live) vviol("device destroyed with layer objects alive");
}
static VKAPI_ATTR void VKAPI_CALL d_GetDeviceQueue(VkDevice d, uint32_t f, uint32_t i, VkQueue *q) {
    (void)d, (void)f, (void)i;
    o_queue.disp = ICD_MAGIC; /* like a driver: the loader trampoline sets the dispatch later */
    *q = (VkQueue)&o_queue;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *ci,
                                                           const VkAllocationCallbacks *a, VkSwapchainKHR *out) {
    (void)d, (void)a;
    V.sc_creates++;
    V.sc_usage = ci->imageUsage;
    if (V.sc_rejects_xfer_src && (ci->imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return VK_ERROR_INITIALIZATION_FAILED;
    *out = (VkSwapchainKHR)(uintptr_t)0x6000;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroySwapchainKHR(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks *a) {
    (void)d, (void)s, (void)a;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_GetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR s, uint32_t *n, VkImage *img) {
    (void)d, (void)s;
    if (img)
        for (uint32_t i = 0; i < *n && i < 3; i++) img[i] = GAME_IMG(i);
    *n = 3;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    (void)q;
    V.presents++;
    V.present_nwait = pi->waitSemaphoreCount;
    for (uint32_t i = 0; i < pi->waitSemaphoreCount && i < 8; i++) {
        VkSemaphore s = V.present_wait[i] = pi->pWaitSemaphores[i];
        if (IS_APP_SEM(s)) {
            if (V.app_used[(uintptr_t)s - 0xA000u]++) vviol("present waits an app semaphore already waited");
        } else if (HID(s) > 0 && HID(s) < MAXH) {
            if (V.sem[HID(s)] != 1) vviol("present waits a layer semaphore that is not signalled");
            V.sem[HID(s)] = 0;
        } else {
            vviol("present waits an unknown semaphore");
        }
    }
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_QueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *si, VkFence f) {
    (void)q;
    V.submits++;
    if (V.submit_fail_at && V.submits == V.submit_fail_at) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (n != 1) vviol("submit count != 1");
    V.submit_nwait = si->waitSemaphoreCount;
    for (uint32_t i = 0; i < si->waitSemaphoreCount && i < 8; i++) {
        VkSemaphore s = V.submit_wait[i] = si->pWaitSemaphores[i];
        V.submit_stage = si->pWaitDstStageMask[i];
        if (IS_APP_SEM(s) && V.app_used[(uintptr_t)s - 0xA000u]++) vviol("submit waits an app semaphore already waited");
    }
    V.submit_signal = si->signalSemaphoreCount ? si->pSignalSemaphores[0] : VK_NULL_HANDLE;
    for (uint32_t i = 0; i < si->signalSemaphoreCount; i++) {
        int id = HID(si->pSignalSemaphores[i]);
        if (id <= 0 || id >= MAXH) vviol("signal of an unknown semaphore");
        else if (V.sem[id]) vviol("binary semaphore signalled twice without a wait");
        else V.sem[id] = 1;
    }
    int fid = f ? HID(f) : 0;
    if (f && V.fence[fid] != 0) vviol("submit with a fence that is not reset");
    if (f) V.fence[fid] = 1;
    for (uint32_t c = 0; c < si->commandBufferCount; c++) {
        fobj *o = (fobj *)si->pCommandBuffers[c];
        V.cmd_fence[o - o_cmd] = fid;
    }
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo *ci,
                                                          const VkAllocationCallbacks *a, VkCommandPool *p) {
    (void)d, (void)a;
    if (!(ci->flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)) vviol("pool without RESET_COMMAND_BUFFER");
    V.pool_live++;
    *p = (VkCommandPool)H(++V.next_id);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) {
    (void)d, (void)p, (void)a;
    V.pool_live--;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_AllocateCommandBuffers(VkDevice d, const VkCommandBufferAllocateInfo *ai,
                                                               VkCommandBuffer *c) {
    (void)d, (void)ai;
    if (V.ncmd >= 64) return VK_ERROR_OUT_OF_HOST_MEMORY;
    o_cmd[V.ncmd].disp = ICD_MAGIC;
    *c = (VkCommandBuffer)&o_cmd[V.ncmd++];
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_FreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n, const VkCommandBuffer *c) {
    (void)d, (void)p;
    for (uint32_t i = 0; i < n; i++) {
        int f = V.cmd_fence[(fobj *)c[i] - o_cmd];
        if (f && V.fence[f] == 1) vviol("freeing a command buffer in flight");
    }
}
static void cmd_check(VkCommandBuffer c, const char *what) {
    if (((fobj *)c)->disp != &key_dev) vviol("command buffer without loader dispatch (vkSetDeviceLoaderData)");
    int f = V.cmd_fence[(fobj *)c - o_cmd];
    if (f && V.fence[f] == 1) vviol(what);
}
static VKAPI_ATTR VkResult VKAPI_CALL d_ResetCommandBuffer(VkCommandBuffer c, VkCommandBufferResetFlags f) {
    (void)f;
    cmd_check(c, "reset of a command buffer in flight");
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_BeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *bi) {
    (void)bi;
    cmd_check(c, "recording a command buffer in flight");
    V.ntr = 0;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_EndCommandBuffer(VkCommandBuffer c) {
    (void)c;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d,
                                                       VkDependencyFlags df, uint32_t nm, const VkMemoryBarrier *m,
                                                       uint32_t nb, const VkBufferMemoryBarrier *b, uint32_t ni,
                                                       const VkImageMemoryBarrier *im) {
    (void)c, (void)s, (void)d, (void)df, (void)nm, (void)m, (void)nb, (void)b;
    for (uint32_t i = 0; i < ni && V.ntr < 8; i++, V.ntr++)
        V.tr[V.ntr].img = im[i].image, V.tr[V.ntr].o = im[i].oldLayout, V.tr[V.ntr].n = im[i].newLayout;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
                                                 VkImageLayout dl, uint32_t n, const VkImageCopy *r) {
    (void)c;
    V.copies++;
    if (sl != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || dl != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) vviol("copy layouts");
    V.copy_src = s, V.copy_dst = d, V.copy_n = n;
    for (uint32_t i = 0; i < n && i < 2; i++) V.copy[i] = r[i];
}
static VKAPI_ATTR void VKAPI_CALL d_CmdBlitImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
                                                 VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) {
    (void)c, (void)s, (void)sl, (void)d, (void)dl, (void)n, (void)r, (void)f;
    V.blits++;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateFence(VkDevice d, const VkFenceCreateInfo *ci, const VkAllocationCallbacks *a,
                                                    VkFence *f) {
    (void)d, (void)a;
    int id = ++V.next_id;
    V.fence[id] = (ci->flags & VK_FENCE_CREATE_SIGNALED_BIT) ? 2 : 0;
    V.fences_live++;
    *f = (VkFence)H(id);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *a) {
    (void)d, (void)a;
    if (V.fence[HID(f)] == 1) vviol("destroying a fence in flight");
    V.fences_live--;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_WaitForFences(VkDevice d, uint32_t n, const VkFence *f, VkBool32 all,
                                                      uint64_t t) {
    (void)d, (void)all;
    V.fence_waits++;
    if (t == UINT64_MAX) vviol("unbounded fence wait");
    for (uint32_t i = 0; i < n; i++) {
        if (V.fence[HID(f[i])] == 0) vviol("waiting a fence that was never submitted (would time out)");
        V.fence[HID(f[i])] = 2; /* the copy finished */
    }
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_ResetFences(VkDevice d, uint32_t n, const VkFence *f) {
    (void)d;
    for (uint32_t i = 0; i < n; i++) {
        if (V.fence[HID(f[i])] == 1) vviol("reset of a fence in flight");
        V.fence[HID(f[i])] = 0;
    }
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateSemaphore(VkDevice d, const VkSemaphoreCreateInfo *ci,
                                                        const VkAllocationCallbacks *a, VkSemaphore *s) {
    (void)d, (void)ci, (void)a;
    V.sems_live++;
    *s = (VkSemaphore)H(++V.next_id);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroySemaphore(VkDevice d, VkSemaphore s, const VkAllocationCallbacks *a) {
    (void)d, (void)s, (void)a;
    V.sems_live--;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_SetDeviceLoaderData(VkDevice d, void *obj) {
    *(void **)obj = *(void **)d;
    V.set_loader_data++;
    return VK_SUCCESS;
}

#define D(n) {"vk" #n, (PFN_vkVoidFunction)d_##n},
static const struct {
    const char *n;
    PFN_vkVoidFunction f;
} g_inst_fns[] = {D(CreateInstance) D(DestroyInstance) D(EnumerateDeviceExtensionProperties)
                      D(GetPhysicalDeviceProperties) D(GetPhysicalDeviceQueueFamilyProperties)
                          D(GetPhysicalDeviceFormatProperties) D(CreateDevice)},
  g_dev_fns[] = {D(DestroyDevice) D(GetDeviceQueue) D(CreateSwapchainKHR) D(DestroySwapchainKHR) D(GetSwapchainImagesKHR)
                     D(QueuePresentKHR) D(QueueSubmit) D(CreateCommandPool) D(DestroyCommandPool)
                         D(AllocateCommandBuffers) D(FreeCommandBuffers) D(ResetCommandBuffer) D(BeginCommandBuffer)
                             D(EndCommandBuffer) D(CmdPipelineBarrier) D(CmdCopyImage) D(CmdBlitImage) D(CreateFence)
                                 D(DestroyFence) D(WaitForFences) D(ResetFences) D(CreateSemaphore) D(DestroySemaphore)};
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL d_gdpa(VkDevice d, const char *n) {
    (void)d;
    for (size_t i = 0; i < sizeof g_dev_fns / sizeof g_dev_fns[0]; i++)
        if (!strcmp(n, g_dev_fns[i].n)) return g_dev_fns[i].f;
    return NULL;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL d_gipa(VkInstance inst, const char *n) {
    for (size_t i = 0; i < sizeof g_inst_fns / sizeof g_inst_fns[0]; i++)
        if (!strcmp(n, g_inst_fns[i].n)) return g_inst_fns[i].f;
    return inst ? d_gdpa(NULL, n) : NULL;
}

/* ---- the "loader + game" side ----------------------------------------------------------------------------------- */
static void *g_lh, *g_mh;
static PFN_vkGetInstanceProcAddr L_gipa;
static PFN_vkGetDeviceProcAddr L_gdpa;
#define MOCK(fn) ((fn##_fn)dlsym(g_mh, #fn))

/* the loader holds its global lock for the whole of vkCreateInstance/vkCreateDevice: the mock flags any runtime or
 * trampoline call made meanwhile */
static void loader_lock(int held) {
    if (g_mh) MOCK(mockvk_loader_lock)(held);
}

static int load_layer(void) {
    g_lh = dlopen(g_layer, RTLD_NOW | RTLD_LOCAL);
    if (!g_lh) return fprintf(stderr, "    dlopen layer: %s\n", dlerror()), -1;
    PFN_vkNegotiateLoaderLayerInterfaceVersion neg =
        (PFN_vkNegotiateLoaderLayerInterfaceVersion)dlsym(g_lh, "vkNegotiateLoaderLayerInterfaceVersion");
    if (!neg) return fprintf(stderr, "    no vkNegotiateLoaderLayerInterfaceVersion\n"), -1;
    VkNegotiateLayerInterface ni = {.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT, .loaderLayerInterfaceVersion = 2};
    if (neg(&ni) != VK_SUCCESS || !ni.pfnGetInstanceProcAddr || !ni.pfnGetDeviceProcAddr) return -1;
    L_gipa = ni.pfnGetInstanceProcAddr;
    L_gdpa = ni.pfnGetDeviceProcAddr;
    return 0;
}

static const char *const GAME_IEXT[] = {"VK_KHR_surface", "VK_KHR_xcb_surface", "VK_KHR_get_physical_device_properties2"};
static const char *const GAME_DEXT[] = {"VK_KHR_swapchain", "VK_KHR_dedicated_allocation",
                                        "VK_KHR_get_memory_requirements2", "VK_KHR_maintenance1"};

static VkInstance app_instance(const char *engine, int which, VkResult *res) {
    cur_inst = which;
    VkLayerInstanceLink link = {.pfnNextGetInstanceProcAddr = d_gipa};
    VkLayerInstanceCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,
                                     .function = VK_LAYER_LINK_INFO, .u.pLayerInfo = &link};
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "ShadowOfMordor",
                            .pEngineName = engine, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pNext = &lci, .pApplicationInfo = &ai,
                               .enabledExtensionCount = 3, .ppEnabledExtensionNames = GAME_IEXT};
    VkInstance inst = VK_NULL_HANDLE;
    PFN_vkCreateInstance c = (PFN_vkCreateInstance)L_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    loader_lock(1);
    VkResult r = c ? c(&ci, NULL, &inst) : VK_ERROR_INITIALIZATION_FAILED;
    loader_lock(0);
    if (res) *res = r;
    return r == VK_SUCCESS ? inst : VK_NULL_HANDLE;
}

typedef struct {
    VkInstance inst;
    VkDevice dev;
    VkQueue q;
    VkSwapchainKHR sc;
    PFN_vkQueuePresentKHR present;
} app_t;

static int app_setup(app_t *a, const char *engine) {
    memset(a, 0, sizeof *a);
    a->inst = app_instance(engine, 0, NULL);
    if (!a->inst) return -1;
    VkLayerDeviceLink link = {.pfnNextGetInstanceProcAddr = d_gipa, .pfnNextGetDeviceProcAddr = d_gdpa};
    VkLayerDeviceCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
                                  .function = VK_LOADER_DATA_CALLBACK, .u.pfnSetDeviceLoaderData = d_SetDeviceLoaderData};
    VkLayerDeviceCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, .pNext = &cb,
                                   .function = VK_LAYER_LINK_INFO, .u.pLayerInfo = &link};
    float prio = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1,
                                   .pQueuePriorities = &prio};
    VkDeviceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &lci, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci, .enabledExtensionCount = 4, .ppEnabledExtensionNames = GAME_DEXT};
    PFN_vkCreateDevice cd = (PFN_vkCreateDevice)L_gipa(a->inst, "vkCreateDevice");
    loader_lock(1);
    VkResult r = cd ? cd((VkPhysicalDevice)&o_pd[0], &ci, NULL, &a->dev) : VK_ERROR_INITIALIZATION_FAILED;
    loader_lock(0);
    if (r != VK_SUCCESS) return -1;
    if (lci.u.pLayerInfo != NULL) vviol("device chain link not advanced by the layer");
    PFN_vkGetDeviceQueue gq = (PFN_vkGetDeviceQueue)L_gdpa(a->dev, "vkGetDeviceQueue");
    gq(a->dev, 0, 0, &a->q);
    *(void **)a->q = *(void **)a->dev; /* what the loader trampoline does after vkGetDeviceQueue */
    VkSwapchainCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .minImageCount = 3,
                                    .imageFormat = V.game_fmt ? V.game_fmt : VK_FORMAT_B8G8R8A8_UNORM,
                                    .imageExtent = {1920, 1080}, .imageArrayLayers = 1,
                                    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                    .presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR};
    PFN_vkCreateSwapchainKHR cs = (PFN_vkCreateSwapchainKHR)L_gdpa(a->dev, "vkCreateSwapchainKHR");
    if (cs(a->dev, &sci, NULL, &a->sc) != VK_SUCCESS) return -1;
    a->present = (PFN_vkQueuePresentKHR)L_gdpa(a->dev, "vkQueuePresentKHR");
    return a->present ? 0 : -1;
}

static int g_frame;
static void app_present(app_t *a) { /* one game frame: image i % 3, one fresh render semaphore */
    uint32_t idx = (uint32_t)g_frame % 3;
    VkSemaphore s = APP_SEM(g_frame);
    VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &s,
                           .swapchainCount = 1, .pSwapchains = &a->sc, .pImageIndices = &idx};
    g_frame++;
    CHECK(a->present(a->q, &pi) == VK_SUCCESS, "present failed");
}

static void app_teardown(app_t *a) {
    ((PFN_vkDestroySwapchainKHR)L_gdpa(a->dev, "vkDestroySwapchainKHR"))(a->dev, a->sc, NULL);
    ((PFN_vkDestroyDevice)L_gdpa(a->dev, "vkDestroyDevice"))(a->dev, NULL);
    ((PFN_vkDestroyInstance)L_gipa(a->inst, "vkDestroyInstance"))(a->inst, NULL);
}

static int log_has(const char *needle) {
    FILE *f = fopen(g_log, "r");
    char line[4096];
    int found = 0;
    while (f && !found && fgets(line, sizeof line, f)) found = strstr(line, needle) != NULL;
    if (f) fclose(f);
    return found;
}

static void check_mock_clean(void) {
    const char *first = "";
    int v = MOCK(mockxr_violations)(&first);
    CHECK(v == 0, "%d OpenXR protocol violations, first: %s", v, first);
    CHECK(V.viol == 0, "%d Vulkan violations, first: %s", V.viol, V.first);
}

/* the app's lists reached the driver unchanged */
static void check_untouched(void) {
    CHECK(V.inst_n == 3 && !has_name(V.inst_list, "VK_KHR_external_fence_capabilities"), "instance list changed:%s",
          V.inst_list);
    CHECK(V.dev_n == 4 && !has_name(V.dev_list, "VK_KHR_external_memory_fd"), "device list changed:%s", V.dev_list);
    CHECK(!(V.sc_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT), "swapchain usage changed: 0x%x", V.sc_usage);
}

/* presents reached the driver with the app's own semaphore and nothing was submitted */
static void check_present_passthrough(int submits_before) {
    CHECK(V.submits == submits_before, "layer submitted work (%d -> %d)", submits_before, V.submits);
    CHECK(V.present_nwait == 1 && V.present_wait[0] == APP_SEM(g_frame - 1), "present not waiting the app semaphore");
}

/* ---- scenarios ---------------------------------------------------------------------------------------------- */
static int sc_negotiate(void) {
    if (load_layer()) return 1;
    VkNegotiateLayerInterface ni = {.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT, .loaderLayerInterfaceVersion = 5};
    PFN_vkNegotiateLoaderLayerInterfaceVersion neg =
        (PFN_vkNegotiateLoaderLayerInterfaceVersion)dlsym(g_lh, "vkNegotiateLoaderLayerInterfaceVersion");
    CHECK(neg(&ni) == VK_SUCCESS && ni.loaderLayerInterfaceVersion == 2, "negotiate from v5 -> %u",
          ni.loaderLayerInterfaceVersion);
    ni.loaderLayerInterfaceVersion = 1;
    CHECK(neg(&ni) != VK_SUCCESS, "negotiate accepted v1");
    CHECK(L_gipa(VK_NULL_HANDLE, "vkGetInstanceProcAddr") == (PFN_vkVoidFunction)L_gipa, "GIPA(GIPA) differs");
    const char *const hooked[] = {"vkCreateInstance", "vkDestroyInstance", "vkCreateDevice", "vkGetDeviceProcAddr",
                                  "vkDestroyDevice", "vkQueuePresentKHR", "vkGetDeviceQueue", "vkGetDeviceQueue2",
                                  "vkCreateSwapchainKHR", "vkDestroySwapchainKHR"};
    for (size_t i = 0; i < sizeof hooked / sizeof hooked[0]; i++)
        CHECK(L_gipa(VK_NULL_HANDLE, hooked[i]) != NULL, "GIPA(NULL, %s) = NULL", hooked[i]);
    CHECK(L_gipa(VK_NULL_HANDLE, "vkCmdDraw") == NULL, "GIPA(NULL, vkCmdDraw) without an instance");
    CHECK(dlsym(g_lh, "vkGetInstanceProcAddr") && dlsym(g_lh, "vkGetDeviceProcAddr"), "GIPA/GDPA not exported");
    CHECK(dlsym(g_lh, "xr_frame") == NULL && dlsym(g_lh, "somvr_pick_format") == NULL, "internal symbols exported");
    return 0;
}

static int sc_passthrough(void) { /* SOMVR_XR=0: nothing changes, present not even intercepted */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    check_untouched();
    CHECK(L_gdpa(a.dev, "vkQueuePresentKHR") == (PFN_vkVoidFunction)d_QueuePresentKHR, "present is intercepted");
    CHECK(L_gipa(a.inst, "vkQueuePresentKHR") == (PFN_vkVoidFunction)d_QueuePresentKHR, "GIPA present intercepted");
    CHECK(L_gdpa(a.dev, "vkCmdDraw") == NULL, "unknown name not forwarded");
    for (int i = 0; i < 3; i++) app_present(&a);
    check_present_passthrough(0);
    app_teardown(&a);
    CHECK(MOCK(mockxr_total)() == 0, "OpenXR was called %d times", MOCK(mockxr_total)());
    CHECK(V.viol == 0, "Vulkan violations: %s", V.first);
    return 0;
}

static int sc_gate(void) { /* default mode, not the game: no OpenXR at all */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "vkcube") == 0, "setup");
    check_untouched();
    CHECK(L_gdpa(a.dev, "vkQueuePresentKHR") == (PFN_vkVoidFunction)d_QueuePresentKHR, "present is intercepted");
    app_present(&a);
    check_present_passthrough(0);
    app_teardown(&a);
    CHECK(MOCK(mockxr_total)() == 0, "OpenXR was called");
    CHECK(log_has("pass-through (not Feral3D)"), "log lacks the gate line");
    return 0;
}

static int sc_fail_soft_setup(const char *logline) { /* XR fails before the instance exists: app unchanged */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    check_untouched();
    for (int i = 0; i < 3; i++) app_present(&a);
    check_present_passthrough(0);
    app_teardown(&a);
    CHECK(log_has(logline), "log lacks \"%s\"", logline);
    if (g_mh) check_mock_clean();
    if (g_mh) CHECK(MOCK(mockxr_inst_live)() == 0, "XrInstance leaked");
    return 0;
}
static int sc_noloader(void) { return sc_fail_soft_setup("no OpenXR loader"); }
static int sc_nohmd(void) { return sc_fail_soft_setup("xrGetSystem(HMD): XR_ERROR_FORM_FACTOR_UNAVAILABLE"); }
static int sc_realloader(void) { return sc_fail_soft_setup("XR_ERROR_RUNTIME_UNAVAILABLE"); }

static int sc_hang(void) { /* the runtime hangs in xrCreateInstance: abandoned within the time limit */
    if (load_layer()) return 1;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    VkInstance i = app_instance("Feral3D", 0, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    CHECK(i != VK_NULL_HANDLE && s < 1.0, "vkCreateInstance took %.2f s", s);
    CHECK(V.inst_n == 3, "instance list changed");
    CHECK(log_has("abandoned, pass-through"), "log lacks the timeout line");
    usleep(1700 * 1000); /* the thread finishes late and must clean up */
    CHECK(log_has("abandoned init thread finished"), "late thread did not report");
    CHECK(MOCK(mockxr_inst_live)() == 0, "late XrInstance not destroyed");
    return 0;
}

static int sc_full(int proj) {
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    /* extensions: merged, no duplicates */
    CHECK(V.inst_creates == 1 && V.inst_n == 6, "instance: %d creates, %u exts:%s", V.inst_creates, V.inst_n, V.inst_list);
    CHECK(count_name(V.inst_list, "VK_KHR_get_physical_device_properties2") == 1, "duplicate instance extension");
    CHECK(has_name(V.inst_list, "VK_KHR_external_semaphore_capabilities") && has_name(V.inst_list, "VK_KHR_xcb_surface"),
          "instance list:%s", V.inst_list);
    CHECK(V.inst_api == VK_API_VERSION_1_1, "apiVersion changed");
    CHECK(V.dev_creates == 1 && V.dev_n == 11, "device: %d creates, %u exts:%s", V.dev_creates, V.dev_n, V.dev_list);
    CHECK(count_name(V.dev_list, "VK_KHR_dedicated_allocation") == 1 && has_name(V.dev_list, "VK_KHR_swapchain") &&
              has_name(V.dev_list, "VK_KHR_external_semaphore_fd"),
          "device list:%s", V.dev_list);
    CHECK(V.sc_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "no TRANSFER_SRC on the swapchain");
    CHECK(L_gdpa(a.dev, "vkQueuePresentKHR") != (PFN_vkVoidFunction)d_QueuePresentKHR, "present not intercepted");
    CHECK(MOCK(mockxr_calls)("xrCreateSession") == 0, "session before the first present");

    for (int f = 0; f < 7; f++) {
        int sub = V.submits;
        app_present(&a);
        CHECK(V.submits == sub + 1, "frame %d: %d submits", f, V.submits - sub);
        CHECK(V.submit_nwait == 1 && V.submit_wait[0] == APP_SEM(f), "frame %d: copy does not wait the app semaphore", f);
        CHECK(V.submit_stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, "wait stage");
        CHECK(V.present_nwait == 1 && V.present_wait[0] == V.submit_signal && !IS_APP_SEM(V.present_wait[0]),
              "frame %d: present does not wait (only) our semaphore", f);
        CHECK(V.copies == f + 1 && V.copy_n == 2 && V.blits == 0, "frame %d: copies %d regions %u", f, V.copies, V.copy_n);
        CHECK(V.copy_src == GAME_IMG(f % 3), "frame %d: copied the wrong game image", f);
        CHECK(V.copy[0].dstOffset.x == 0 && V.copy[1].dstOffset.x == 1920 && V.copy[1].extent.width == 1920 &&
                  V.copy[1].extent.height == 1080,
              "copy regions");
        CHECK(V.ntr == 4 && V.tr[0].img == GAME_IMG(f % 3) && V.tr[0].o == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
                  V.tr[2].n == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && V.tr[3].n == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              "barriers");
        const mockxr_end *e = MOCK(mockxr_last_end)();
        CHECK(e->layer_count == 1 && e->same_swapchain, "frame %d: %u layers", f, e->layer_count);
        if (!proj) {
            CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->rect[0][0] == 0 && e->rect[0][2] == 1920 &&
                      e->rect[0][3] == 1080,
                  "quad layer");
            CHECK(e->size[0] == 2.0f && e->size[1] > 1.12f && e->size[1] < 1.13f && e->pos[2] == -2.0f, "quad size/pose");
        } else {
            CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->rect[0][0] == 0 &&
                      e->rect[1][0] == 1920 && e->rect[1][2] == 1920 && e->rect[1][3] == 1080,
                  "projection layer");
        }
    }
    CHECK(V.fence_waits >= 4, "image reuse without fence waits (%d)", V.fence_waits);
    CHECK(V.set_loader_data == 3, "vkSetDeviceLoaderData calls %d", V.set_loader_data);
    const mockxr_binding *b = MOCK(mockxr_binding_get)();
    CHECK(b->instance == (void *)a.inst && b->device == (void *)a.dev && b->family == 0 && b->index == 0 &&
              b->physical_device == MOCK(mockxr_tramp_pd)(),
          "session binding");
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
    CHECK(s->width == 3840 && s->height == 1080 && s->format == VK_FORMAT_B8G8R8A8_SRGB, "XR swapchain %ux%u fmt %lld",
          s->width, s->height, (long long)s->format);
    CHECK(MOCK(mockxr_calls)("xrWaitFrame") == 7 && MOCK(mockxr_calls)("xrCreateSession") == 1, "frame calls");
    CHECK(MOCK(mockxr_calls)("xrLocateViews") == (proj ? 7 : 0), "xrLocateViews calls");
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0, "XR session objects alive after vkDestroyDevice");
    CHECK(V.sems_live == 0 && V.fences_live == 0 && V.pool_live == 0, "layer Vulkan objects leaked");
    check_mock_clean();
    return 0;
}
static int sc_quad(void) { return sc_full(0); }
static int sc_proj(void) { return sc_full(1); }

static int sc_format(VkFormat game, int64_t want, int copies, int blits, const char *logline) {
    V.game_fmt = game;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    app_present(&a);
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
    CHECK(s->format == want && V.copies == copies && V.blits == blits, "game fmt %d: XR fmt %lld, copies %d, blits %d",
          game, (long long)s->format, V.copies, V.blits);
    CHECK(V.present_wait[0] == V.submit_signal, "present does not wait our semaphore");
    CHECK(!logline || log_has(logline), "log lacks \"%s\"", logline);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_srgb_game(void) { return sc_format(VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, 2, 0, ": copy"); }
static int sc_blit(void) { /* BGRA game, the runtime only offers RGBA: blit (and the log warns about brightness) */
    return sc_format(VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, 0, 2, "brightness will be off");
}

/* XR dies mid-run: from the next present on the layer passes through */
static int sc_midrun(const char *logline, int dies_after, int copied_in_dying_frame) {
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int f = 0; f < dies_after; f++) app_present(&a);
    int sub = V.submits;
    app_present(&a); /* the frame in which it dies */
    if (copied_in_dying_frame)
        CHECK(V.present_wait[0] == V.submit_signal && !IS_APP_SEM(V.present_wait[0]), "dying frame: semaphore lost");
    else
        CHECK(V.present_wait[0] == APP_SEM(g_frame - 1), "dying frame: present lost the app semaphore");
    CHECK(MOCK(mockxr_live)() == 0, "session not torn down");
    sub = V.submits;
    for (int f = 0; f < 3; f++) app_present(&a);
    check_present_passthrough(sub);
    app_teardown(&a);
    CHECK(log_has(logline), "log lacks \"%s\"", logline);
    CHECK(V.sems_live == 0 && V.fences_live == 0 && V.pool_live == 0, "layer Vulkan objects leaked");
    check_mock_clean();
    CHECK(MOCK(mockxr_inst_live)() == 0, "XrInstance not destroyed");
    return 0;
}
static int sc_endframe_fail(void) { return sc_midrun("xrEndFrame failed", 2, 1); }
static int sc_submit_fail(void) {
    V.submit_fail_at = 3;
    return sc_midrun("our copy submit failed", 2, 0);
}
static int sc_exit_event(void) { /* the runtime stops the session after 3 frames (user quits in the headset) */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int f = 0; f < 6; f++) app_present(&a);
    CHECK(MOCK(mockxr_calls)("xrEndSession") == 1, "xrEndSession calls %d", MOCK(mockxr_calls)("xrEndSession"));
    CHECK(MOCK(mockxr_calls)("xrWaitFrame") == 3, "frames after STOPPING: %d", MOCK(mockxr_calls)("xrWaitFrame"));
    CHECK(V.present_wait[0] == APP_SEM(g_frame - 1), "present after exit not passed through");
    app_teardown(&a);
    CHECK(log_has("session exiting/lost"), "log lacks the exit");
    check_mock_clean();
    return 0;
}
static int sc_gpu_mismatch(void) {
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    CHECK(MOCK(mockxr_calls)("xrCreateSession") == 0, "session on another GPU");
    check_present_passthrough(0);
    app_teardown(&a);
    CHECK(log_has("DIFFERENT"), "log lacks the GPU mismatch");
    check_mock_clean();
    return 0;
}
static int sc_inst_retry(void) { /* the driver refuses the XR instance extensions: retried with the app's list */
    V.fail_inst_with_xr = 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(V.inst_creates == 2 && V.inst_n == 3, "retry: %d creates, %u exts", V.inst_creates, V.inst_n);
    check_untouched();
    app_present(&a);
    check_present_passthrough(0);
    app_teardown(&a);
    check_mock_clean();
    CHECK(MOCK(mockxr_inst_live)() == 0, "XrInstance not destroyed");
    return 0;
}
static int sc_dev_retry(void) {
    V.fail_dev_with_xr = 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(V.dev_creates == 2 && V.dev_n == 4, "retry: %d creates, %u exts", V.dev_creates, V.dev_n);
    CHECK(!(V.sc_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT), "swapchain changed on a non-XR device");
    app_present(&a);
    check_present_passthrough(0);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_dev_lacks(void) {
    V.dev_lacks_fence_fd = 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(V.dev_creates == 1 && V.dev_n == 4, "device list changed although an extension is missing");
    app_present(&a);
    check_present_passthrough(0);
    app_teardown(&a);
    CHECK(log_has("lacks the runtime's device extension VK_KHR_external_fence_fd"), "log lacks the missing extension");
    check_mock_clean();
    return 0;
}
static int sc_sc_retry(void) { /* the driver refuses TRANSFER_SRC: swapchain as asked, XR frames without a layer */
    V.sc_rejects_xfer_src = 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(V.sc_creates == 2 && !(V.sc_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT), "swapchain retry");
    for (int f = 0; f < 3; f++) app_present(&a);
    check_present_passthrough(0);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 0 && MOCK(mockxr_calls)("xrEndFrame") == 3, "frames without a layer");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_rebind(void) { /* Feral-style probe instance first, destroyed, then the real one */
    if (load_layer()) return 1;
    VkInstance probe = app_instance("Feral3D", 1, NULL);
    CHECK(probe && V.inst_n == 6, "probe instance did not get the XR extensions");
    ((PFN_vkDestroyInstance)L_gipa(probe, "vkDestroyInstance"))(probe, NULL);
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(V.inst_n == 6 && V.dev_n == 11, "second instance/device without XR");
    app_present(&a);
    CHECK(V.copies == 1 && MOCK(mockxr_calls)("xrCreateInstance") == 1, "rebind: copies %d", V.copies);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

static const struct {
    const char *name;
    int (*fn)(void);
    const char *env; /* space-separated K=V; @MOCK = mock .so path, @TMP = temp dir */
    int mock;        /* load the mock (as libvulkan.so.1 + XR loader) */
} g_scen[] = {
    {"negotiate", sc_negotiate, "", 0},
    {"passthrough", sc_passthrough, "SOMVR_XR=0 SOMVR_XR_LOADER=@MOCK", 1},
    {"gate", sc_gate, "SOMVR_XR_LOADER=@MOCK", 1},
    {"noloader", sc_noloader, "SOMVR_XR_LOADER=@TMP/none/libopenxr_loader.so.1", 0},
    {"nohmd", sc_nohmd, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrGetSystem", 1},
    {"hang", sc_hang, "SOMVR_XR_LOADER=@MOCK MOCKXR_HANG_MS=1500 SOMVR_XR_TIMEOUT_MS=300", 1},
    {"quad", sc_quad, "SOMVR_XR_LOADER=@MOCK", 1},
    {"proj", sc_proj, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_LAYER=proj", 1},
    {"srgb-game", sc_srgb_game, "SOMVR_XR_LOADER=@MOCK", 1},
    {"blit", sc_blit, "SOMVR_XR_LOADER=@MOCK MOCKXR_FORMATS=43", 1},
    {"endframe-fail", sc_endframe_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrEndFrame:3", 1},
    {"submit-fail", sc_submit_fail, "SOMVR_XR_LOADER=@MOCK", 1},
    {"exit-event", sc_exit_event, "SOMVR_XR_LOADER=@MOCK MOCKXR_EXIT_AT=3", 1},
    {"gpu-mismatch", sc_gpu_mismatch, "SOMVR_XR_LOADER=@MOCK MOCKVK_TRAMP_NAME=llvmpipe", 1},
    {"instance-retry", sc_inst_retry, "SOMVR_XR_LOADER=@MOCK", 1},
    {"device-retry", sc_dev_retry, "SOMVR_XR_LOADER=@MOCK", 1},
    {"device-lacks-ext", sc_dev_lacks, "SOMVR_XR_LOADER=@MOCK", 1},
    {"swapchain-retry", sc_sc_retry, "SOMVR_XR_LOADER=@MOCK", 1},
    {"rebind", sc_rebind, "SOMVR_XR_LOADER=@MOCK", 1},
    /* the real /usr/lib64/libopenxr_loader.so.1 with no runtime it could find or reach (missing runtime JSON, empty
     * config/data dirs, empty runtime dir): must fail in xrCreateInstance and pass through */
    {"real-loader-no-runtime", sc_realloader,
     "XR_RUNTIME_JSON=@TMP/none/no_runtime.json HOME=@TMP/empty XDG_CONFIG_HOME=@TMP/empty XDG_CONFIG_DIRS=@TMP/empty "
     "XDG_DATA_HOME=@TMP/empty XDG_DATA_DIRS=@TMP/empty XDG_RUNTIME_DIR=@TMP/empty XR_API_LAYER_PATH=@TMP/empty",
     0},
};

static void set_env(const char *spec) {
    char buf[2048], val[1024];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        val[0] = 0;
        for (const char *p = eq + 1; *p;) {
            if (!strncmp(p, "@MOCK", 5)) strncat(val, g_mock, sizeof val - strlen(val) - 1), p += 5;
            else if (!strncmp(p, "@TMP", 4)) strncat(val, g_tmp, sizeof val - strlen(val) - 1), p += 4;
            else strncat(val, p++, 1);
        }
        setenv(tok, val, 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) return fprintf(stderr, "usage: test_layer LAYER.so MOCK.so TMPDIR [scenario]\n"), 2;
    g_layer = argv[1], g_mock = argv[2], g_tmp = argv[3];
    int failed = 0, run = 0;
    for (size_t i = 0; i < sizeof g_scen / sizeof g_scen[0]; i++) {
        if (argc > 4 && strcmp(argv[4], g_scen[i].name)) continue;
        run++;
        snprintf(g_log, sizeof g_log, "%s/%s.log", g_tmp, g_scen[i].name);
        unlink(g_log);
        pid_t pid = fork();
        if (pid == 0) {
            unsetenv("SOMVR_XR");
            unsetenv("SOMVR_XR_LAYER");
            unsetenv("SOMVR_XR_LOADER");
            unsetenv("XR_RUNTIME_JSON");
            setenv("SOMVR_XR_LOG", g_log, 1);
            set_env(g_scen[i].env);
            alarm(30);
            if (g_scen[i].mock && !(g_mh = dlopen(g_mock, RTLD_NOW | RTLD_LOCAL)))
                fprintf(stderr, "    dlopen mock: %s\n", dlerror()), _exit(1);
            int r = g_scen[i].fn();
            fflush(stderr);
            _exit(r || g_fail ? 1 : 0);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
        printf("%-24s %s", g_scen[i].name, ok ? "PASS" : "FAIL");
        if (!WIFEXITED(st)) printf(" (signal %d)", WTERMSIG(st));
        printf("\n");
        fflush(stdout);
        if (!ok) {
            failed++;
            char cmd[1024];
            snprintf(cmd, sizeof cmd, "sed 's/^/    log: /' '%s' 2>/dev/null | tail -25", g_log);
            if (system(cmd)) {}
        }
    }
    printf("%d/%d scenarios passed\n", run - failed, run);
    return failed ? 1 : 0;
}
