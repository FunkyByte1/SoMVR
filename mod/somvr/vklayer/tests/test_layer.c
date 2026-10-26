/* test_layer.c - drives the built layer the way the Vulkan loader and the game would: a fake driver below the layer
 * (it checks fence, semaphore and command-buffer use), mock_xr.so as the OpenXR loader + runtime + loader trampoline.
 * For the stereo scenarios this program also plays the LD_PRELOAD library: it exports `somvr_shared`, locates the
 * views through the layer's published session with the real provider (../somvr_views.c), writes frame records the way
 * H4/H1/H3 do and tags each present the way H9 does - then checks the projection layer carries exactly those poses
 * and fovs. Every scenario runs in its own forked process with its own environment. No GPU, no XR runtime.
 * The input scenarios also load input_obj.so (next to MOCK.so) RTLD_GLOBAL: the preload's `somvr_input` object; the
 * others have no such symbol (a v2 preload). The HUD scenarios likewise load hud_obj.so, the preload's `somvr_hud` (H11):
 * the quad layer, its copy, its absence and its failures; everywhere else there is no such symbol. The pointer
 * scenarios use a v2-sized somvr_input whose want has POINTER: the ray and the dot after the frame's own layers, the
 * published pointer block, the image's one upload, clicks, the hand switch, and the pointer's absence and failures.
 * The comfort scenarios load comfort_obj.so, the preload's `somvr_comfort`: the vignette's head-locked ring only while
 * it reaches into the view (closing with the speed x the strength, opening and gone when still), its alpha texture's
 * one upload, a reuse frame, the snap blink's black frame by frame from each frame's own entry, with the head-locked
 * HUD (two VIEW spaces), the object absent or inactive, and the failures (comfort off, the frames go on).
 * The threat scenarios load threat_obj.so, the preload's `somvr_threat` (../../somvr_threat.c is linked for the expected
 * quads and texture): nothing without a cue, a world-locked glow over the projection billboarded to the head, the
 * texture's one upload byte for byte, three glows and both head-locked edge bands in their order, a reuse frame, cues
 * below a cell or with a NaN point, another frame's entry and the newest one, with comfort's ring (the glows under it,
 * the edges over it), the object absent, inactive or SOMVR_XR_THREAT=off, and a rejected frame (only the cues go off).
 * The SoM loading env scenarios (SOMVR_XR_ENV; ../somvr_env.c is linked here for the expected image, faces, dim quads
 * and transitions): the sky behind mono frames as one equirect2 layer or six quads, its one upload byte for byte, the
 * switches between the world and the menus through hold frames and the fade quad frame by frame, world mode's kept
 * frame (copied once, dimmed, freed), and the environment off, misconfigured, rejected or failing.
 *   test_layer LAYER.so MOCK.so TMPDIR [scenario]
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <openxr/openxr.h>

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../somvr_comfort.h" /* the comfort ABI and SOMVR_VIG_* (its functions are not linked here) */
#include "../../somvr_input.h"
#include "../../somvr_pointer.h" /* SOMVR_PTR_* constants (its functions are not linked here) */
#include "../../somvr_shared.h"
#include "../../somvr_threat.h" /* the threat ABI; its functions are linked: the expected quads and texture */
#include "../../somvr_views.h"
#include "../somvr_env.h"   /* SoM loading env: linked here (the expected image, geometry and transitions) */
#include "../somvr_void.h"  /* never a void: linked here (the expected floor and lights) */
#include "../../somvr_vrmenu.h" /* the VR menu's ABI; its functions (and the font's) are linked: the expected pictures */
#include "../../somvr_menus.h" /* build 8: linked here (the expected tiles, split and watch pose) */
#include "../../somvr_vrhands.h" /* build 10: our VR hands' ABI (its functions are not linked here) */
#include "../../somvr_session.h" /* never a void: the theatre's word (the session object: tests/session_obj.c) */
#include "../somvr_logic.h" /* SOMVR_GUARD_COLS (the layer's own constants; its functions are not linked here) */
#include "mock_common.h"

/* the preload's struct: zero (= an inert preload) unless a scenario turns it on */
__attribute__((visibility("default"))) somvr_shared g_sh __asm__(SOMVR_SHARED_SYMBOL);

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
#define APP_SEMS 4096 /* one fresh render semaphore per app frame (the everything-on scenario runs ~350) */
#define APP_SEM(i) ((VkSemaphore)(uintptr_t)(0xA000u + ((uint32_t)(i) % APP_SEMS)))
#define IS_APP_SEM(s) ((uintptr_t)(s) >= 0xA000u && (uintptr_t)(s) < 0xA000u + APP_SEMS)
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
    struct { VkImage img; VkImageLayout o, n; } tr[32];
    int ntr;
    struct { VkImage s, d; VkImageCopy r; } cp[8]; /* every vkCmdCopyImage since the test last reset ncp */
    int ncp, images, image_destroys;
    VkImageCopy hcp[128]; /* build 8: every region of the last vkCmdCopyImage with more than two (the HUD's split; a
                           * mono frame's guard columns too) */
    int nhcp;
    VkImage hcp_d;
    int clears;           /* vkCmdClearColorImage calls (build 6: the HUD image's extra area) */
    /* build 10: our VR hands' drawing (somvr_handdraw.c): objects alive, render passes begun / open, indexed draws and
     * their indices, resolves (into the image last made TRANSFER_DST), a missing entry point (no_gfx) */
    int gfx_live, rp_begins, rp_open, draws, resolves, no_gfx;
    long draw_idx;
    VkImage resolve_dst;
    VkImage clear_img;
    int viol;
    char first[256];
    int next_id;
    uint8_t fence[MAXH], sem[MAXH], app_used[APP_SEMS];
    int cmd_fence[64], ncmd;
    int fences_live, sems_live, pool_live;
    /* SOMVR_XR_DUMP: buffers and memory over host memory */
    int fail_buffer, fail_alloc;
    VkFormat eye_fmt;
    int buffer_creates, buffers_live, mems_live, alloc_type, img2buf, host_barriers;
    /* the pointer image's upload (vkCmdCopyBufferToImage) */
    int buf2img;
    VkImage up_img;
    uint32_t up_w, up_h;
    /* build 11: the world's depth (the hands' second pipeline): descriptor objects, the views written into the sets in
     * order, the pushed eye.w (depth_k), the views made (their images), a driver without the descriptor entry points */
    int no_world, dsl_made, ds_updates, ds_binds, smp_made;
    VkImageView ds_view[16];
    int nds_view;
    float pc_eyew[16], pc_basea[16];
    int npc;
    struct { VkImageView v; VkImage img; VkImageAspectFlags asp; VkFormat fmt; } views[64];
    int nviews;
    VkImageAspectFlags tr_aspect[32];
    uint8_t up_px[SOMVR_ENV_EQ_W * (SOMVR_ENV_EQ_H + SOMVR_ENV_STRIP) * 4]; /* the largest image: the loading env's
                                                                            * equirect sky (comfort's, the pointer's,
                                                                            * the fade's, the VR menu's smaller) */
} V;

/* image contents, as far as the tests need them: a fill per image, plus the x spans vkCmdCopyImage wrote (newest
 * last); a barrier from UNDEFINED discards them. Pixels are the 4 bytes as they sit in memory. */
#define MAXIMG 32
static struct {
    VkImage img;
    uint8_t fill[4];
    int nsp;
    struct { int32_t x0, x1; uint8_t px[4]; } sp[8];
} g_img[MAXIMG];
static int g_nimg;
static int img_ix(VkImage img) {
    for (int i = 0; i < g_nimg; i++)
        if (g_img[i].img == img) return i;
    if (g_nimg == MAXIMG) return -1;
    memset(&g_img[g_nimg], 0, sizeof g_img[0]);
    g_img[g_nimg].img = img;
    return g_nimg++;
}
static const uint8_t *img_px(VkImage img, int32_t x) {
    static const uint8_t none[4] = {0, 0, 0, 0};
    int i = img_ix(img);
    if (i < 0) return none;
    for (int k = g_img[i].nsp - 1; k >= 0; k--)
        if (x >= g_img[i].sp[k].x0 && x < g_img[i].sp[k].x1) return g_img[i].sp[k].px;
    return g_img[i].fill;
}
/* the memory: what the host sees (p) and what the device wrote (gpu, copied to p when the writing submit's fence is
 * waited) */
#define MAXMEM 16
static struct {
    VkDeviceMemory m;
    VkDeviceSize size;
    uint32_t type;
    uint8_t *p, *gpu;
    int mapped, dirty_cmd;
} g_mem[MAXMEM];
static struct {
    VkBuffer b;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    int mem;
    VkDeviceSize off;
} g_buf[MAXMEM];
/* the memory types, like RADV's on a dGPU: VRAM, GTT, VRAM through the BAR, GTT cached */
static const VkMemoryPropertyFlags MEM_TYPES[4] = {
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT};

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
    if (V.fences_live || V.sems_live || V.pool_live || V.buffers_live || V.mems_live)
        vviol("device destroyed with layer objects alive");
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
    (void)c, (void)s, (void)df, (void)nm, (void)m;
    for (uint32_t i = 0; i < ni; i++) {
        /* in one command buffer an image's barriers chain: each starts from the layout the one before left */
        for (int k = V.ntr - 1; k >= 0; k--)
            if (V.tr[k].img == im[i].image) {
                if (im[i].oldLayout != VK_IMAGE_LAYOUT_UNDEFINED && im[i].oldLayout != V.tr[k].n)
                    vviol("a barrier from a layout the image is not in");
                break;
            }
        int ix = im[i].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? img_ix(im[i].image) : -1;
        if (ix >= 0) g_img[ix].nsp = 0;
        if (V.ntr < 32) V.tr[V.ntr].img = im[i].image, V.tr[V.ntr].o = im[i].oldLayout, V.tr[V.ntr++].n = im[i].newLayout;
        if (V.ntr <= 32 && V.ntr > 0 && V.tr[V.ntr - 1].img == im[i].image) V.tr_aspect[V.ntr - 1] = im[i].subresourceRange.aspectMask;
    }
    for (uint32_t i = 0; i < nb; i++)
        V.host_barriers += (b[i].srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) &&
                           (b[i].dstAccessMask & VK_ACCESS_HOST_READ_BIT) && (d & VK_PIPELINE_STAGE_HOST_BIT);
}
static VKAPI_ATTR void VKAPI_CALL d_CmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
                                                 VkImageLayout dl, uint32_t n, const VkImageCopy *r) {
    (void)c;
    V.copies++;
    if (sl != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || dl != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) vviol("copy layouts");
    V.copy_src = s, V.copy_dst = d, V.copy_n = n;
    for (uint32_t i = 0; i < n && i < 2; i++) V.copy[i] = r[i];
    if (V.ncp < 8 && n) V.cp[V.ncp].s = s, V.cp[V.ncp].d = d, V.cp[V.ncp].r = r[0], V.ncp++;
    if (n > 2) {
        V.nhcp = (int)(n < 128 ? n : 128), V.hcp_d = d;
        memcpy(V.hcp, r, (size_t)V.nhcp * sizeof r[0]);
    }
    for (uint32_t i = 0; i < n; i++) {
        int ix = img_ix(d);
        if (ix < 0) continue;
        if (g_img[ix].nsp == 8) memmove(&g_img[ix].sp[0], &g_img[ix].sp[1], 7 * sizeof g_img[ix].sp[0]), g_img[ix].nsp--;
        int k = g_img[ix].nsp++;
        g_img[ix].sp[k].x0 = r[i].dstOffset.x, g_img[ix].sp[k].x1 = r[i].dstOffset.x + (int32_t)r[i].extent.width;
        memcpy(g_img[ix].sp[k].px, img_px(s, r[i].srcOffset.x), 4);
    }
}
static VKAPI_ATTR void VKAPI_CALL d_CmdClearColorImage(VkCommandBuffer c, VkImage img, VkImageLayout l,
                                                       const VkClearColorValue *v, uint32_t n,
                                                       const VkImageSubresourceRange *rg) {
    (void)c, (void)rg;
    if (l != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || n != 1 || v->float32[3] != 0) vviol("clear: layout, ranges, colour");
    V.clears++, V.clear_img = img;
    int ix = img_ix(img);
    if (ix >= 0) g_img[ix].nsp = 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateImage(VkDevice d, const VkImageCreateInfo *ci, const VkAllocationCallbacks *a,
                                                    VkImage *out) {
    (void)d, (void)a, (void)ci;
    *out = (VkImage)(uintptr_t)(0x9000u + (uint32_t)V.images++);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroyImage(VkDevice d, VkImage i, const VkAllocationCallbacks *a) {
    (void)d, (void)i, (void)a;
    V.image_destroys++;
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
        for (int k = 0; k < MAXMEM; k++) /* its writes into memory reach the host now */
            if (g_mem[k].m && g_mem[k].dirty_cmd >= 0 && V.cmd_fence[g_mem[k].dirty_cmd] == HID(f[i])) {
                memcpy(g_mem[k].p, g_mem[k].gpu, g_mem[k].size);
                g_mem[k].dirty_cmd = -1;
            }
    }
    return VK_SUCCESS;
}
/* SOMVR_XR_COVER polls its readings' fences: the mock's GPU is done at once (as a wait) */
static VKAPI_ATTR VkResult VKAPI_CALL d_GetFenceStatus(VkDevice d, VkFence f) {
    (void)d;
    if (V.fence[HID(f)] == 0) return VK_NOT_READY; /* never submitted, or reset */
    V.fence[HID(f)] = 2;
    for (int k = 0; k < MAXMEM; k++)
        if (g_mem[k].m && g_mem[k].dirty_cmd >= 0 && V.cmd_fence[g_mem[k].dirty_cmd] == HID(f)) {
            memcpy(g_mem[k].p, g_mem[k].gpu, g_mem[k].size);
            g_mem[k].dirty_cmd = -1;
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
/* SOMVR_XR_DUMP's entry points */
static VKAPI_ATTR void VKAPI_CALL d_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd,
                                                                      VkPhysicalDeviceMemoryProperties *p) {
    (void)pd;
    memset(p, 0, sizeof *p);
    p->memoryTypeCount = 4;
    for (int i = 0; i < 4; i++)
        p->memoryTypes[i] = (VkMemoryType){MEM_TYPES[i], (MEM_TYPES[i] & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 0u : 1u};
    p->memoryHeapCount = 2;
    p->memoryHeaps[0] = (VkMemoryHeap){16ull << 30, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
    p->memoryHeaps[1] = (VkMemoryHeap){32ull << 30, 0};
}
static int buf_ix(VkBuffer b) {
    for (int i = 0; i < MAXMEM; i++)
        if (b && g_buf[i].b == b) return i;
    return -1;
}
static int mem_ix(VkDeviceMemory m) {
    for (int i = 0; i < MAXMEM; i++)
        if (m && g_mem[i].m == m) return i;
    return -1;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateBuffer(VkDevice d, const VkBufferCreateInfo *ci,
                                                     const VkAllocationCallbacks *a, VkBuffer *out) {
    (void)d, (void)a;
    V.buffer_creates++;
    if (V.fail_buffer) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!(ci->usage & (VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) || !ci->size)
        vviol("buffer without a TRANSFER, VERTEX or INDEX usage or size");
    int i;
    for (i = 0; i < MAXMEM && g_buf[i].b; i++) {}
    if (i == MAXMEM) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_buf[i] = (__typeof__(g_buf[0])){.b = (VkBuffer)H(++V.next_id), .size = ci->size, .usage = ci->usage, .mem = -1};
    V.buffers_live++;
    *out = g_buf[i].b;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_DestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) {
    (void)d, (void)a;
    int i = buf_ix(b);
    if (i < 0) {
        vviol("destroying an unknown buffer");
        return;
    }
    memset(&g_buf[i], 0, sizeof g_buf[i]);
    V.buffers_live--;
}
static VKAPI_ATTR void VKAPI_CALL d_GetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *r) {
    (void)d;
    int i = buf_ix(b);
    if (i < 0) vviol("requirements of an unknown buffer");
    *r = (VkMemoryRequirements){.size = i < 0 ? 0 : (g_buf[i].size + 255) & ~255ull, .alignment = 256,
                                .memoryTypeBits = 0xf};
}
static VKAPI_ATTR VkResult VKAPI_CALL d_AllocateMemory(VkDevice d, const VkMemoryAllocateInfo *ai,
                                                       const VkAllocationCallbacks *a, VkDeviceMemory *out) {
    (void)d, (void)a;
    if (V.fail_alloc) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (ai->memoryTypeIndex >= 4) vviol("allocation with an unknown memory type");
    int i;
    for (i = 0; i < MAXMEM && g_mem[i].m; i++) {}
    if (i == MAXMEM) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_mem[i] = (__typeof__(g_mem[0])){.m = (VkDeviceMemory)H(++V.next_id), .size = ai->allocationSize,
                                      .type = ai->memoryTypeIndex, .p = malloc(ai->allocationSize),
                                      .gpu = malloc(ai->allocationSize), .dirty_cmd = -1};
    memset(g_mem[i].p, 0xcd, ai->allocationSize); /* what the host would read before the device's writes arrive */
    V.alloc_type = (int)ai->memoryTypeIndex;
    V.mems_live++;
    *out = g_mem[i].m;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_FreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) {
    (void)d, (void)a;
    int i = mem_ix(m);
    if (i < 0) {
        vviol("freeing unknown memory");
        return;
    }
    for (int k = 0; k < MAXMEM; k++)
        if (g_buf[k].b && g_buf[k].mem == i) vviol("memory freed under a live buffer");
    free(g_mem[i].p), free(g_mem[i].gpu);
    memset(&g_mem[i], 0, sizeof g_mem[i]);
    V.mems_live--;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_BindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize off) {
    (void)d;
    int i = buf_ix(b), k = mem_ix(m);
    if (i < 0 || k < 0 || g_buf[i].mem >= 0 || off + g_buf[i].size > g_mem[k].size) {
        vviol("bad vkBindBufferMemory");
        return VK_ERROR_UNKNOWN;
    }
    g_buf[i].mem = k, g_buf[i].off = off;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_MapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize off, VkDeviceSize sz,
                                                  VkMemoryMapFlags fl, void **pp) {
    (void)d, (void)sz, (void)fl;
    int i = mem_ix(m);
    if (i < 0 || g_mem[i].mapped || !(MEM_TYPES[g_mem[i].type] & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        vviol("mapping unknown, mapped or device-only memory");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    g_mem[i].mapped = 1;
    *pp = g_mem[i].p + off;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_UnmapMemory(VkDevice d, VkDeviceMemory m) {
    (void)d;
    int i = mem_ix(m);
    if (i < 0 || !g_mem[i].mapped) vviol("unmapping memory that is not mapped");
    else g_mem[i].mapped = 0;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdCopyImageToBuffer(VkCommandBuffer c, VkImage img, VkImageLayout l, VkBuffer b,
                                                         uint32_t n, const VkBufferImageCopy *r) {
    V.img2buf++;
    int last = -1;
    for (int k = V.ntr - 1; k >= 0 && last < 0; k--)
        if (V.tr[k].img == img) last = (int)V.tr[k].n;
    if (l != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || last != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        vviol("copy to a buffer from an image not in TRANSFER_SRC_OPTIMAL");
    int i = buf_ix(b), k = i >= 0 ? g_buf[i].mem : -1;
    if (k < 0) {
        vviol("copy into an unknown or unbound buffer");
        return;
    }
    for (uint32_t j = 0; j < n; j++) {
        uint32_t w = r[j].imageExtent.width, h = r[j].imageExtent.height;
        uint32_t rl = r[j].bufferRowLength ? r[j].bufferRowLength : w;
        if (!w || !h || r[j].bufferOffset + ((VkDeviceSize)(h - 1) * rl + w) * 4 > g_buf[i].size) {
            vviol("copy beyond the buffer");
            return;
        }
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                memcpy(g_mem[k].gpu + g_buf[i].off + r[j].bufferOffset + ((VkDeviceSize)y * rl + x) * 4,
                       img_px(img, r[j].imageOffset.x + (int32_t)x), 4);
    }
    g_mem[k].dirty_cmd = (int)((fobj *)c - o_cmd);
}

/* the device reads the buffer (HOST_COHERENT: the host's writes are visible at the submit) into an image in
 * TRANSFER_DST_OPTIMAL (by the command buffer's last barrier of it): a copy of the bytes is kept for the checks */
static VKAPI_ATTR void VKAPI_CALL d_CmdCopyBufferToImage(VkCommandBuffer c, VkBuffer b, VkImage img, VkImageLayout l,
                                                         uint32_t n, const VkBufferImageCopy *r) {
    (void)c;
    V.buf2img++;
    int last = -1;
    for (int k = V.ntr - 1; k >= 0 && last < 0; k--)
        if (V.tr[k].img == img) last = (int)V.tr[k].n;
    if (l != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || last != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        vviol("copy from a buffer into an image not in TRANSFER_DST_OPTIMAL");
    int i = buf_ix(b), k = i >= 0 ? g_buf[i].mem : -1;
    if (k < 0 || !(g_buf[i].usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        vviol("copy from an unknown, unbound or non-TRANSFER_SRC buffer");
        return;
    }
    if (n != 1 || r[0].bufferRowLength || r[0].bufferOffset ||
        (VkDeviceSize)r[0].imageExtent.width * r[0].imageExtent.height * 4 > g_buf[i].size) {
        vviol("a buffer-to-image copy that is not one tightly packed region inside the buffer");
        return;
    }
    V.up_img = img, V.up_w = r[0].imageExtent.width, V.up_h = r[0].imageExtent.height;
    size_t sz = (size_t)V.up_w * V.up_h * 4;
    memcpy(V.up_px, g_mem[k].p + g_buf[i].off, sz < sizeof V.up_px ? sz : sizeof V.up_px);
}

/* ---- build 10: the drawing entry points of our VR hands (somvr_handdraw.c): handles, counts, a few rules ---------- */
static VkResult gfx_new(uint64_t *out) {
    V.gfx_live++;
    *out = (uint64_t)(uintptr_t)H(++V.next_id);
    return VK_SUCCESS;
}
#define GFX_NEW(T, out) gfx_new((uint64_t *)(void *)(out))
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateRenderPass(VkDevice d, const VkRenderPassCreateInfo *ci,
                                                         const VkAllocationCallbacks *a, VkRenderPass *o) {
    (void)d, (void)a;
    if (ci->subpassCount != 1 || ci->attachmentCount != 2) vviol("render pass: not one subpass with colour and depth");
    return GFX_NEW(VkRenderPass, o);
}
#define GFX_DEL(n, T)                                                                                                  \
    static VKAPI_ATTR void VKAPI_CALL d_Destroy##n(VkDevice d, T h, const VkAllocationCallbacks *a) {                \
        (void)d, (void)a;                                                                                              \
        if (h) V.gfx_live--;                                                                                           \
    }
GFX_DEL(RenderPass, VkRenderPass)
GFX_DEL(Framebuffer, VkFramebuffer)
GFX_DEL(ImageView, VkImageView)
GFX_DEL(ShaderModule, VkShaderModule)
GFX_DEL(PipelineLayout, VkPipelineLayout)
GFX_DEL(Pipeline, VkPipeline)
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo *ci,
                                                          const VkAllocationCallbacks *a, VkFramebuffer *o) {
    (void)d, (void)a, (void)ci;
    return GFX_NEW(VkFramebuffer, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateImageView(VkDevice d, const VkImageViewCreateInfo *ci,
                                                        const VkAllocationCallbacks *a, VkImageView *o) {
    (void)d, (void)a;
    VkResult r = GFX_NEW(VkImageView, o);
    if (V.nviews < 64)
        V.views[V.nviews].v = *o, V.views[V.nviews].img = ci->image, V.views[V.nviews].fmt = ci->format,
        V.views[V.nviews++].asp = ci->subresourceRange.aspectMask;
    return r;
}
/* build 11: the world's depth: descriptor objects (counted with the drawing's), the views written, the binds */
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateDescriptorSetLayout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *ci,
                                                                  const VkAllocationCallbacks *a, VkDescriptorSetLayout *o) {
    (void)d, (void)a;
    if (ci->bindingCount != 1 || ci->pBindings[0].descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
        ci->pBindings[0].stageFlags != VK_SHADER_STAGE_FRAGMENT_BIT)
        vviol("the world's set layout: not one sampled image for the fragment shader");
    V.dsl_made++;
    return GFX_NEW(VkDescriptorSetLayout, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateDescriptorPool(VkDevice d, const VkDescriptorPoolCreateInfo *ci,
                                                             const VkAllocationCallbacks *a, VkDescriptorPool *o) {
    (void)d, (void)a, (void)ci;
    return GFX_NEW(VkDescriptorPool, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL d_AllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo *ai,
                                                               VkDescriptorSet *o) {
    (void)d;
    for (uint32_t i = 0; i < ai->descriptorSetCount; i++) o[i] = (VkDescriptorSet)(uintptr_t)H(++V.next_id);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_UpdateDescriptorSets(VkDevice d, uint32_t n, const VkWriteDescriptorSet *w, uint32_t nc,
                                                         const VkCopyDescriptorSet *c) {
    (void)d, (void)nc, (void)c;
    for (uint32_t i = 0; i < n; i++) {
        V.ds_updates++;
        if (w[i].pImageInfo && w[i].pImageInfo->imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            vviol("a sampled image written in a layout other than SHADER_READ_ONLY_OPTIMAL");
        if (w[i].pImageInfo && V.nds_view < 16) V.ds_view[V.nds_view++] = w[i].pImageInfo->imageView;
    }
}
static VKAPI_ATTR void VKAPI_CALL d_CmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l,
                                                          uint32_t f, uint32_t n, const VkDescriptorSet *s, uint32_t nd,
                                                          const uint32_t *dy) {
    (void)c, (void)b, (void)l, (void)f, (void)s, (void)nd, (void)dy;
    if (!V.rp_open || n != 1) vviol("descriptor sets bound outside the hands' render pass");
    V.ds_binds++;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateSampler(VkDevice d, const VkSamplerCreateInfo *ci, const VkAllocationCallbacks *a,
                                                      VkSampler *o) {
    (void)d, (void)a, (void)ci;
    V.smp_made++;
    return GFX_NEW(VkSampler, o);
}
GFX_DEL(DescriptorSetLayout, VkDescriptorSetLayout)
GFX_DEL(DescriptorPool, VkDescriptorPool)
GFX_DEL(Sampler, VkSampler)
/* the image a view of ours was made on (0: none known) */
static VkImage view_image(VkImageView v, VkImageAspectFlags *asp) {
    for (int i = 0; i < V.nviews; i++)
        if (V.views[i].v == v) return asp ? *asp = V.views[i].asp : 0, V.views[i].img;
    return VK_NULL_HANDLE;
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *ci,
                                                           const VkAllocationCallbacks *a, VkShaderModule *o) {
    (void)d, (void)a;
    if (ci->codeSize < 20 || ci->codeSize % 4 || ci->pCode[0] != 0x07230203u) vviol("a shader module that is not SPIR-V");
    return GFX_NEW(VkShaderModule, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *ci,
                                                             const VkAllocationCallbacks *a, VkPipelineLayout *o) {
    (void)d, (void)a;
    if (ci->pushConstantRangeCount != 1 || ci->pPushConstantRanges[0].size > 128) vviol("push constants over 128 bytes");
    return GFX_NEW(VkPipelineLayout, o);
}
static VKAPI_ATTR VkResult VKAPI_CALL d_CreateGraphicsPipelines(VkDevice d, VkPipelineCache c, uint32_t n,
                                                                const VkGraphicsPipelineCreateInfo *ci,
                                                                const VkAllocationCallbacks *a, VkPipeline *o) {
    (void)d, (void)c, (void)a, (void)ci;
    for (uint32_t i = 0; i < n; i++) GFX_NEW(VkPipeline, &o[i]);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_GetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *r) {
    (void)d, (void)i;
    *r = (VkMemoryRequirements){.size = 4096, .alignment = 256, .memoryTypeBits = 0xf};
}
static VKAPI_ATTR VkResult VKAPI_CALL d_BindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize off) {
    (void)d, (void)i, (void)off;
    if (mem_ix(m) < 0) vviol("binding unknown memory to an image");
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *bi,
                                                       VkSubpassContents sc) {
    (void)c, (void)bi, (void)sc;
    if (V.rp_open++) vviol("a render pass begun inside another");
    V.rp_begins++;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdEndRenderPass(VkCommandBuffer c) {
    (void)c;
    if (V.rp_open-- != 1) vviol("a render pass ended that was not begun");
}
static VKAPI_ATTR void VKAPI_CALL d_CmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) {
    (void)c, (void)b, (void)p;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n, const VkBuffer *b,
                                                         const VkDeviceSize *o) {
    (void)c, (void)f, (void)o;
    for (uint32_t i = 0; i < n; i++)
        if (buf_ix(b[i]) < 0 || !(g_buf[buf_ix(b[i])].usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) vviol("not a vertex buffer");
}
static VKAPI_ATTR void VKAPI_CALL d_CmdBindIndexBuffer(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkIndexType t) {
    (void)c, (void)o, (void)t;
    if (buf_ix(b) < 0 || !(g_buf[buf_ix(b)].usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) vviol("not an index buffer");
}
static VKAPI_ATTR void VKAPI_CALL d_CmdDrawIndexed(VkCommandBuffer c, uint32_t ni, uint32_t inst, uint32_t first,
                                                   int32_t vo, uint32_t fi) {
    (void)c, (void)first, (void)vo, (void)fi;
    if (!V.rp_open || inst != 1) vviol("a draw outside a render pass");
    V.draws++, V.draw_idx += ni;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s,
                                                     uint32_t off, uint32_t sz, const void *v) {
    (void)c, (void)l, (void)s;
    if (off + sz > 128) vviol("push constants past 128 bytes");
    if (off == 0 && sz >= 112 && V.npc < 16) /* the hands' eye.w (depth_k), base.a (0 occluder, 1 a hand, > 1 holding) */
        memcpy(&V.pc_eyew[V.npc], (const char *)v + 76, 4), memcpy(&V.pc_basea[V.npc++], (const char *)v + 108, 4);
}
static VKAPI_ATTR void VKAPI_CALL d_CmdSetViewport(VkCommandBuffer c, uint32_t f, uint32_t n, const VkViewport *v) {
    (void)c, (void)f, (void)n, (void)v;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdSetScissor(VkCommandBuffer c, uint32_t f, uint32_t n, const VkRect2D *r) {
    (void)c, (void)f, (void)n, (void)r;
}
static VKAPI_ATTR void VKAPI_CALL d_CmdResolveImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
                                                    VkImageLayout dl, uint32_t n, const VkImageResolve *r) {
    (void)c, (void)s, (void)r;
    int last = -1;
    for (int k = V.ntr - 1; k >= 0 && last < 0; k--)
        if (V.tr[k].img == d) last = (int)V.tr[k].n;
    if (sl != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || dl != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
        last != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || n != 1 || V.rp_open)
        vviol("a resolve: layouts, regions or inside a render pass");
    V.resolves++, V.resolve_dst = d;
}

#define D(n) {"vk" #n, (PFN_vkVoidFunction)d_##n},
static const struct {
    const char *n;
    PFN_vkVoidFunction f;
} g_inst_fns[] = {D(CreateInstance) D(DestroyInstance) D(EnumerateDeviceExtensionProperties)
                      D(GetPhysicalDeviceProperties) D(GetPhysicalDeviceQueueFamilyProperties)
                          D(GetPhysicalDeviceFormatProperties) D(CreateDevice) D(GetPhysicalDeviceMemoryProperties)},
  g_dev_fns[] = {D(DestroyDevice) D(GetDeviceQueue) D(CreateSwapchainKHR) D(DestroySwapchainKHR) D(GetSwapchainImagesKHR)
                     D(QueuePresentKHR) D(QueueSubmit) D(CreateCommandPool) D(DestroyCommandPool)
                         D(AllocateCommandBuffers) D(FreeCommandBuffers) D(ResetCommandBuffer) D(BeginCommandBuffer)
                             D(EndCommandBuffer) D(CmdPipelineBarrier) D(CmdCopyImage) D(CmdBlitImage) D(CreateFence)
                                 D(DestroyFence) D(WaitForFences) D(ResetFences) D(GetFenceStatus) D(CreateSemaphore)
                                     D(DestroySemaphore)
                                     D(CreateImage) D(DestroyImage) D(CreateBuffer) D(DestroyBuffer)
                                         D(GetBufferMemoryRequirements) D(AllocateMemory) D(FreeMemory)
                                             D(BindBufferMemory) D(MapMemory) D(UnmapMemory) D(CmdCopyImageToBuffer)
                                                 D(CmdCopyBufferToImage) D(CmdClearColorImage)
    /* build 10: our VR hands' drawing */
    D(CreateRenderPass) D(DestroyRenderPass) D(CreateFramebuffer) D(DestroyFramebuffer) D(CreateImageView)
    D(DestroyImageView) D(GetImageMemoryRequirements) D(BindImageMemory) D(CreateShaderModule) D(DestroyShaderModule)
    D(CreatePipelineLayout) D(DestroyPipelineLayout) D(CreateGraphicsPipelines) D(DestroyPipeline) D(CmdBeginRenderPass)
    D(CmdEndRenderPass) D(CmdBindPipeline) D(CmdBindVertexBuffers) D(CmdBindIndexBuffer) D(CmdDrawIndexed)
    D(CmdPushConstants) D(CmdSetViewport) D(CmdSetScissor) D(CmdResolveImage)
    /* build 11: the world's depth */
    D(CreateDescriptorSetLayout) D(DestroyDescriptorSetLayout) D(CreateDescriptorPool) D(DestroyDescriptorPool)
    D(AllocateDescriptorSets) D(UpdateDescriptorSets) D(CmdBindDescriptorSets) D(CreateSampler) D(DestroySampler)};
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL d_gdpa(VkDevice d, const char *n) {
    (void)d;
    if (V.no_gfx && !strcmp(n, "vkCreateRenderPass")) return NULL; /* build 10: a driver without it */
    if (V.no_world && !strcmp(n, "vkCreateDescriptorSetLayout")) return NULL; /* build 11: without the world's */
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

static int log_count(const char *needle) {
    FILE *f = fopen(g_log, "r");
    char line[4096];
    int n = 0;
    while (f && fgets(line, sizeof line, f)) n += strstr(line, needle) != NULL;
    if (f) fclose(f);
    return n;
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
        CHECK(V.copies == f + 1 && V.copy_n == 1 && V.blits == 0, "frame %d: copies %d regions %u", f, V.copies, V.copy_n);
        CHECK(V.copy_src == GAME_IMG(f % 3), "frame %d: copied the wrong game image", f);
        CHECK(V.copy[0].dstOffset.x == 0 && V.copy[0].extent.width == 1920 && V.copy[0].extent.height == 1080,
              "copy region");
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
            CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->rect[0][0] == 0 && e->rect[0][2] == 1920 &&
                      e->rect[1][0] == 0 && e->rect[1][2] == 1920 && e->rect[1][3] == 1080,
                  "projection layer (mono: both views the whole image)");
        }
    }
    CHECK(V.fence_waits >= 4, "image reuse without fence waits (%d)", V.fence_waits);
    CHECK(V.set_loader_data == 3, "vkSetDeviceLoaderData calls %d", V.set_loader_data);
    const mockxr_binding *b = MOCK(mockxr_binding_get)();
    CHECK(b->instance == (void *)a.inst && b->device == (void *)a.dev && b->family == 0 && b->index == 0 &&
              b->physical_device == MOCK(mockxr_tramp_pd)(),
          "session binding");
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
    CHECK(s->width == 1920 && s->height == 1080 && s->format == VK_FORMAT_B8G8R8A8_SRGB, "XR swapchain %ux%u fmt %lld",
          s->width, s->height, (long long)s->format);
    CHECK(MOCK(mockxr_calls)("xrWaitFrame") == 8 && MOCK(mockxr_calls)("xrCreateSession") == 1,
          "frame calls: %d waits (7 presents, then the wait for the next frame)", MOCK(mockxr_calls)("xrWaitFrame"));
    CHECK(log_has("no active somvr_shared"), "log lacks the mono-only line");
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
    CHECK(MOCK(mockxr_calls)("xrWaitFrame") == 4, "waits before STOPPING: %d (3 frames + the next one's wait)",
          MOCK(mockxr_calls)("xrWaitFrame"));
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


/* ---- the LD_PRELOAD side, for the stereo scenarios (what somvr.so's H4/H1/H3/H9 do in the game) ---------------- */
enum { TAG_SYNC = 0, TAG_OTHER_THREAD = 1, TAG_NONE = 2 };
static int32_t my_tid(void) { return (int32_t)syscall(SYS_gettid); }

static void preload_on(uint32_t layout, uint32_t version) {
    g_sh.magic = SOMVR_SHARED_MAGIC;
    g_sh.version = version;
    g_sh.size = (uint32_t)sizeof g_sh;
    g_sh.layout = layout;
    g_sh.pid = (int32_t)getpid();
    atomic_store(&g_sh.active, 1);
}

/* H4 + H1 + H3: the record of a rendered stereo frame */
static void write_record(uint64_t frame, uint32_t layout, const somvr_views *v, uint32_t applied) {
    somvr_rec *r = somvr_rec_slot(&g_sh, frame);
    atomic_store(&r->applied, 0);
    somvr_seq_wbegin(&r->seq);
    r->frame = frame;
    r->display_time = v->time;
    r->layout = layout;
    r->nslots = layout == SOMVR_LAYOUT_B ? 2 : 1;
    r->flags = v->extrapolated ? SOMVR_REC_EXTRAPOLATED : 0;
    for (int e = 0; e < 2; e++) {
        somvr_eye_rec *er = &r->eye[e];
        memset(er, 0, sizeof *er);
        er->pose = v->pose[e];
        er->fov = v->fov[e];
        er->fov_cover = sv_fov_cover(&v->fov[e]);
        float a[4] = {e ? 0.5f : 0.0f, 0, e ? 1.0f : 0.5f, 1}, full[4] = {0, 0, 1, 1};
        memcpy(er->rect, layout == SOMVR_LAYOUT_A ? a : full, sizeof er->rect);
        er->slot = layout == SOMVR_LAYOUT_B ? (uint32_t)e : 0;
    }
    somvr_seq_wend(&r->seq);
    atomic_store(&r->applied, applied);
}

/* H9: tag the present (inside the IDXGISwapChain::Present bracket for TAG_SYNC), then present */
static void tagged_present_eyes(app_t *a, uint64_t frame, uint32_t slot, int mode, uint32_t eyes, uint32_t gen) {
    int32_t tid = my_tid();
    if (mode != TAG_NONE) {
        uint64_t seq = somvr_tag_push_eyes(&g_sh, frame, slot, mode == TAG_SYNC ? tid : tid + 100000, eyes, gen);
        if (mode == TAG_SYNC) {
            atomic_store(&g_sh.present_tag, seq);
            atomic_store(&g_sh.present_tid, tid);
        }
    }
    app_present(a);
    atomic_store(&g_sh.present_tid, 0);
}
static void tagged_present(app_t *a, uint64_t frame, uint32_t slot, int mode) {
    tagged_present_eyes(a, frame, slot, mode, 0, 0);
}

static somvr_views_state g_vst;
static int locate(somvr_views *v) { return somvr_views_locate(&g_sh, &g_vst, v); }

/* the projection layer (layer 0 of nl) is exactly the record: poses, fovs (exact or covering), rects */
static void check_proj_n(const somvr_views *v, uint32_t applied, const int32_t want_rect[2][4], const char *what,
                         uint32_t nl) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == nl && e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->same_swapchain,
          "%s: layer count %u (want %u) type %d", what, e->layer_count, nl, e->type);
    for (int k = 0; k < 2; k++) {
        float pose[7];
        memcpy(pose, v->pose[k].q, 16);
        memcpy(pose + 4, v->pose[k].p, 12);
        somvr_fovf f = (applied >> k & 1) ? v->fov[k] : sv_fov_cover(&v->fov[k]);
        float fov[4] = {f.l, f.r, f.u, f.d};
        CHECK(!memcmp(e->pose[k], pose, sizeof pose), "%s: view %d pose %g,%g,%g,%g %g,%g,%g is not the record's", what,
              k, e->pose[k][0], e->pose[k][1], e->pose[k][2], e->pose[k][3], e->pose[k][4], e->pose[k][5], e->pose[k][6]);
        CHECK(!memcmp(e->fov[k], fov, sizeof fov), "%s: view %d fov %g %g %g %g, want %g %g %g %g", what, k, e->fov[k][0],
              e->fov[k][1], e->fov[k][2], e->fov[k][3], fov[0], fov[1], fov[2], fov[3]);
        CHECK(!memcmp(e->rect[k], want_rect[k], sizeof want_rect[k]), "%s: view %d rect %d,%d %dx%d", what, k,
              e->rect[k][0], e->rect[k][1], e->rect[k][2], e->rect[k][3]);
    }
}
static void check_proj(const somvr_views *v, uint32_t applied, const int32_t want_rect[2][4], const char *what) {
    check_proj_n(v, applied, want_rect, what, 1);
}

static const int32_t RECT_A[2][4] = {{0, 0, 960, 1080}, {960, 0, 960, 1080}};

static int sc_stereo_a(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    somvr_views v;
    CHECK(!locate(&v), "views before the session runs");
    app_present(&a); /* the first present starts the session; untagged: mono */
    CHECK(atomic_load(&g_sh.xr.state) == SOMVR_XRS_RUNNING && g_sh.xr.locate_views && g_sh.xr.session &&
              g_sh.xr.space && g_sh.xr.view_w == 1832 && g_sh.xr.view_h == 1920 &&
              g_sh.xr.space_type == XR_REFERENCE_SPACE_TYPE_LOCAL,
          "not published: state %u", atomic_load(&g_sh.xr.state));
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD, "untagged present not mono");
    CHECK(g_sh.xr.waits == 2 && g_sh.xr.predicted_time == MOCK(mockxr_time)(), "time not published: waits %llu",
          (unsigned long long)g_sh.xr.waits);
    for (uint64_t f = 1; f <= 5; f++) {
        int copies = V.copies;
        CHECK(locate(&v) && v.fresh && !v.extrapolated && v.time == MOCK(mockxr_time)(), "frame %llu: locate",
              (unsigned long long)f);
        write_record(f, SOMVR_LAYOUT_A, &v, 3);
        tagged_present(&a, f, 0, TAG_SYNC);
        check_proj(&v, 3, RECT_A, "layout A");
        const mockxr_end *e = MOCK(mockxr_last_end)();
        CHECK(e->released == 1 && e->time == v.time, "frame %llu: released %d, end time %lld vs located %lld",
              (unsigned long long)f, e->released, (long long)e->time, (long long)v.time);
        CHECK(V.copies == copies + 1 && V.copy_n == 1 && V.copy[0].dstOffset.x == 0 && V.copy[0].extent.width == 1920,
              "frame %llu: copy", (unsigned long long)f);
        CHECK(V.present_nwait == 1 && V.present_wait[0] == V.submit_signal && !IS_APP_SEM(V.present_wait[0]),
              "frame %llu: the present does not wait our copy", (unsigned long long)f);
    }
    /* the game thread ran ahead (no new xrWaitFrame): extrapolated time; still exactly the record's poses */
    CHECK(locate(&v) && locate(&v) && v.extrapolated, "extrapolated locate");
    write_record(6, SOMVR_LAYOUT_A, &v, 3);
    tagged_present(&a, 6, 0, TAG_SYNC);
    check_proj(&v, 3, RECT_A, "extrapolated");
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
    CHECK(s->width == 1920 && s->height == 1080, "XR swapchain %ux%u", s->width, s->height);
    CHECK(MOCK(mockxr_calls)("xrWaitFrame") == 8 && g_sh.xr.waits == 8, "waits %d / published %llu",
          MOCK(mockxr_calls)("xrWaitFrame"), (unsigned long long)g_sh.xr.waits);
    CHECK(atomic_load(&g_sh.n_stereo) == 6 && atomic_load(&g_sh.n_reuse) == 0, "counters stereo %llu",
          (unsigned long long)atomic_load(&g_sh.n_stereo));
    CHECK(MOCK(mockxr_locate_bad)() == 0, "bad xrLocateViews calls");
    app_teardown(&a);
    CHECK(atomic_load(&g_sh.xr.state) == SOMVR_XRS_DEAD && atomic_load(&g_sh.xr.users) == 0, "not unpublished");
    CHECK(log_has("somvr_shared found") && log_has("published to somvr_shared"), "log lines");
    CHECK(log_has("flags 0x0, display-time error +0.000 ms): views"), "no display-time error in the stereo frame line");
    check_mock_clean();
    return 0;
}

static int sc_stereo_cover(void) { /* H3 applied eye 0 only: eye 1 was rendered with the covering fov */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    somvr_views v;
    CHECK(locate(&v), "locate");
    write_record(1, SOMVR_LAYOUT_A, &v, 1);
    tagged_present(&a, 1, 0, TAG_SYNC);
    check_proj(&v, 1, RECT_A, "eye 1 not applied");
    write_record(2, SOMVR_LAYOUT_A, &v, 0);
    tagged_present(&a, 2, 0, TAG_SYNC);
    check_proj(&v, 0, RECT_A, "no eye applied");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

static int sc_stereo_reuse(void) { /* frame-id mismatch: the last stereo frame again, without a copy */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    somvr_views v, v1;
    CHECK(locate(&v1), "locate");
    write_record(1, SOMVR_LAYOUT_A, &v1, 3);
    tagged_present(&a, 1, 0, TAG_SYNC);
    int copies = V.copies, acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage"), ends = MOCK(mockxr_calls)("xrEndFrame");
    tagged_present(&a, 99, 0, TAG_SYNC); /* no record 99 */
    check_proj(&v1, 3, RECT_A, "reuse of frame 1");
    CHECK(V.copies == copies && MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq &&
              MOCK(mockxr_last_end)()->released == 0 && MOCK(mockxr_calls)("xrEndFrame") == ends + 1,
          "reuse copied or acquired");
    CHECK(V.present_wait[0] == APP_SEM(g_frame - 1), "reuse: the present must wait the app's own semaphore");
    /* an overwritten record (same ring slot) is a mismatch too */
    CHECK(locate(&v), "locate");
    write_record(2, SOMVR_LAYOUT_A, &v, 3);
    write_record(2 + SOMVR_NREC, SOMVR_LAYOUT_A, &v, 3);
    tagged_present(&a, 2, 0, TAG_SYNC);
    check_proj(&v1, 3, RECT_A, "overwritten record -> reuse");
    /* after a mono frame there is nothing to reuse: mono */
    tagged_present(&a, 0, 0, TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD, "frame 0 tag not mono");
    tagged_present(&a, 98, 0, TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD, "mismatch after mono not mono");
    CHECK(atomic_load(&g_sh.n_reuse) == 2 && atomic_load(&g_sh.n_stereo) == 1, "counters reuse %llu stereo %llu",
          (unsigned long long)atomic_load(&g_sh.n_reuse), (unsigned long long)atomic_load(&g_sh.n_stereo));
    app_teardown(&a);
    CHECK(log_has("has no complete record"), "log lacks the reuse line");
    check_mock_clean();
    return 0;
}

static int sc_stereo_untagged(void) { /* an active preload, but presents without a stereo tag: mono */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 3; i++) tagged_present(&a, 0, 0, i == 1 ? TAG_NONE : TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && atomic_load(&g_sh.n_mono) == 3,
          "mono %llu", (unsigned long long)atomic_load(&g_sh.n_mono));
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

static int sc_stereo_badversion(void) { /* another ABI version: ignored, mono */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION + 1);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    tagged_present(&a, 1, 0, TAG_SYNC);
    CHECK(atomic_load(&g_sh.xr.state) == SOMVR_XRS_NONE && MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD,
          "a struct of another version was used");
    app_teardown(&a);
    CHECK(log_has("inactive or another version"), "log line");
    check_mock_clean();
    return 0;
}

/* the preload's side of layout B: textures made by the game's device (vkCreateImage through the layer) after it
 * announced their size, and the info H9 publishes */
static VkImage app_image(app_t *a, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = V.eye_fmt ? V.eye_fmt : VK_FORMAT_R8G8B8A8_UNORM, .extent = {w, h, 1},
                            .mipLevels = 1, .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
    VkImage img = VK_NULL_HANDLE;
    CHECK(((PFN_vkCreateImage)L_gdpa(a->dev, "vkCreateImage"))(a->dev, &ci, NULL, &img) == VK_SUCCESS && img,
          "vkCreateImage");
    return img;
}
static void eyes_info(uint32_t gen, uint32_t w, uint32_t h, VkImage l, VkImage r) {
    somvr_eyes_info in = {gen, w, h, 28, {(uint64_t)(uintptr_t)l, (uint64_t)(uintptr_t)r}};
    somvr_eyes_publish(&g_sh, &in);
}
#define FERAL_USAGE (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | \
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) /* FUN_01ddee60 always ORs TRANSFER_SRC|DST in */
static int barrier_seen(VkImage img, VkImageLayout o, VkImageLayout n) {
    for (int i = 0; i < V.ntr; i++)
        if (V.tr[i].img == img && V.tr[i].o == o && V.tr[i].n == n) return 1;
    return 0;
}
/* one layout-B frame with the eye images: one acquire, both copies side by side, the images back in TRANSFER_DST */
static void check_eyes_frame(const somvr_views *v, VkImage l, VkImage r, uint32_t w, uint32_t h, const char *what) {
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
    CHECK(s->width == 2 * w && s->height == h, "%s: XR swapchain %ux%u", what, s->width, s->height);
    const int32_t rect[2][4] = {{0, 0, (int32_t)w, (int32_t)h}, {(int32_t)w, 0, (int32_t)w, (int32_t)h}};
    check_proj(v, 3, rect, what);
    CHECK(V.ncp == 2 && V.cp[0].s == l && V.cp[0].r.dstOffset.x == 0 && V.cp[1].s == r &&
              V.cp[1].r.dstOffset.x == (int32_t)w && V.cp[0].r.extent.width == w && V.cp[1].r.extent.height == h &&
              V.cp[0].d == V.cp[1].d,
          "%s: %d copies, src %p/%p dst x %d/%d", what, V.ncp, (void *)V.cp[0].s, (void *)V.cp[1].s,
          V.cp[0].r.dstOffset.x, V.cp[1].r.dstOffset.x);
    for (int e = 0; e < 2; e++) {
        VkImage img = e ? r : l;
        CHECK(barrier_seen(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) &&
                  barrier_seen(img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
              "%s: eye %d image not taken from / returned to TRANSFER_DST_OPTIMAL", what, e);
    }
    CHECK(MOCK(mockxr_last_end)()->released == 1, "%s: not released", what);
    CHECK(V.present_nwait == 1 && V.present_wait[0] == V.submit_signal && !IS_APP_SEM(V.present_wait[0]),
          "%s: the present does not wait our copy", what);
}
static int sc_stereo_b(void) { /* layout B: the preload's two eye images, named by the tag, side by side in one frame */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a); /* the session starts; mono: 2 x the game image */
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
    CHECK(s->width == 3840 && s->height == 1080, "mono B swapchain %ux%u", s->width, s->height);
    VkImage early = app_image(&a, 1832, 1920, FERAL_USAGE); /* before the size was announced: not a candidate */
    atomic_store(&g_sh.eyes.expect_w, 1832);
    atomic_store(&g_sh.eyes.expect_h, 1920);
    VkImage L = app_image(&a, 1832, 1920, FERAL_USAGE), R = app_image(&a, 1832, 1920, FERAL_USAGE);
    VkImage other = app_image(&a, 1920, 1080, FERAL_USAGE), nosrc = app_image(&a, 1832, 1920, VK_IMAGE_USAGE_SAMPLED_BIT);
    somvr_views v;
    CHECK(locate(&v), "locate");
    /* the VkImages are not known yet (Feral makes them at the first use): nothing stereo yet -> mono */
    eyes_info(1, 1832, 1920, NULL, NULL);
    write_record(1, SOMVR_LAYOUT_B, &v, 3);
    tagged_present_eyes(&a, 1, 1, TAG_SYNC, 3, 1);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && log_has("not known yet"),
          "no images yet: not mono");
    for (uint64_t f = 2; f <= 4; f++) {
        CHECK(locate(&v), "locate");
        eyes_info(2, 1832, 1920, L, R);
        write_record(f, SOMVR_LAYOUT_B, &v, 3);
        V.ncp = V.ntr = 0;
        int ends = MOCK(mockxr_calls)("xrEndFrame"), acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage");
        tagged_present_eyes(&a, f, 1, TAG_SYNC, 3, 2);
        check_eyes_frame(&v, L, R, 1832, 1920, "layout B");
        CHECK(V.buffer_creates == 0 && V.img2buf == 0 && V.host_barriers == 0 && V.ntr == 6,
              "frame %llu without SOMVR_XR_DUMP: buffers %d, buffer copies %d, %d barriers", (unsigned long long)f,
              V.buffer_creates, V.img2buf, V.ntr);
        CHECK(MOCK(mockxr_calls)("xrEndFrame") == ends + 1 && MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq + 1,
              "frame %llu: ends %d acquires %d", (unsigned long long)f, MOCK(mockxr_calls)("xrEndFrame") - ends,
              MOCK(mockxr_calls)("xrAcquireSwapchainImage") - acq);
    }
    CHECK(atomic_load(&g_sh.eyes.n_submits) == 3 && atomic_load(&g_sh.n_stereo) == 3, "eye submits %llu stereo %llu",
          (unsigned long long)atomic_load(&g_sh.eyes.n_submits), (unsigned long long)atomic_load(&g_sh.n_stereo));
    somvr_views last = v;
    /* fail soft: each of these is the last stereo frame again (no copy, no acquire) */
    struct { uint32_t eyes, tag_gen, info_gen; VkImage l, r; const char *what, *log; } bad[] = {
        {1, 2, 2, L, R, "eye R not copied", "the tag says eyes 0x1"},
        {3, 2, 3, L, R, "another generation", "eye info gen 3"},
        {3, 2, 2, L, early, "an image made before the size was announced", "not created by this device"},
        {3, 2, 2, other, R, "an image of another size", "not created by this device"},
        {3, 2, 2, L, nosrc, "an image without TRANSFER_SRC", "not created by this device"},
        {3, 2, 2, L, (VkImage)(uintptr_t)0xdead, "a handle nobody created", "not created by this device"},
    };
    uint64_t f = 5;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++, f++) {
        CHECK(locate(&v), "locate");
        eyes_info(bad[i].info_gen, 1832, 1920, bad[i].l, bad[i].r);
        write_record(f, SOMVR_LAYOUT_B, &v, 3);
        V.ncp = 0;
        int acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage");
        tagged_present_eyes(&a, f, 1, TAG_SYNC, bad[i].eyes, bad[i].tag_gen);
        check_proj(&last, 3, (const int32_t[2][4]){{0, 0, 1832, 1920}, {1832, 0, 1832, 1920}}, bad[i].what);
        CHECK(V.ncp == 0 && MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq && MOCK(mockxr_last_end)()->released == 0,
              "%s: copied or acquired", bad[i].what);
        CHECK(log_has(bad[i].log), "%s: log lacks \"%s\"", bad[i].what, bad[i].log);
    }
    /* an eye image destroyed: no longer usable */
    ((PFN_vkDestroyImage)L_gdpa(a.dev, "vkDestroyImage"))(a.dev, R, NULL);
    CHECK(locate(&v), "locate");
    eyes_info(2, 1832, 1920, L, R);
    write_record(f, SOMVR_LAYOUT_B, &v, 3);
    V.ncp = 0;
    tagged_present_eyes(&a, f++, 1, TAG_SYNC, 3, 2);
    CHECK(V.ncp == 0 && MOCK(mockxr_last_end)()->released == 0 && V.image_destroys == 1, "a destroyed eye image was used");
    /* new textures (the 3D surface changed size): a new size, new images, a new swapchain */
    atomic_store(&g_sh.eyes.expect_w, 1600);
    atomic_store(&g_sh.eyes.expect_h, 1680);
    VkImage L2 = app_image(&a, 1600, 1680, FERAL_USAGE), R2 = app_image(&a, 1600, 1680, FERAL_USAGE);
    CHECK(locate(&v), "locate");
    eyes_info(9, 1600, 1680, L2, R2);
    write_record(f, SOMVR_LAYOUT_B, &v, 3);
    V.ncp = V.ntr = 0;
    tagged_present_eyes(&a, f++, 1, TAG_SYNC, 3, 9);
    check_eyes_frame(&v, L2, R2, 1600, 1680, "after a resize");
    /* a flat frame in between (tag frame 0): mono from the game image, then stereo again */
    tagged_present(&a, 0, 0, TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && MOCK(mockxr_swapchain_get)()->width == 3840,
          "flat frame: not mono from the game image");
    CHECK(locate(&v), "locate");
    write_record(f, SOMVR_LAYOUT_B, &v, 3);
    V.ncp = V.ntr = 0;
    tagged_present_eyes(&a, f++, 1, TAG_SYNC, 3, 9);
    check_eyes_frame(&v, L2, R2, 1600, 1680, "stereo after a flat frame");
    app_teardown(&a);
    CHECK(log_has("from the eye images") && log_has("an eye image candidate"), "log lines");
    check_mock_clean();
    return 0;
}

/* SOMVR_XR_DUMP: layout B with small eye images of one colour each (the bytes in the eye format's order), stereo
 * frames 1..3, each checked as in stereo-b; frame `dumped` (0 = none) also copies the XR image into the buffer and
 * leaves <log>.stereoN.ppm, the others have exactly stereo-b's barriers. `creates`: buffer creations from frame 1 on. */
#define DUMP_W 64
#define DUMP_H 32
static const uint8_t DUMP_L[3] = {200, 30, 60}, DUMP_R[3] = {20, 180, 90};
static int dump_run(app_t *a, int bgra, int dumped, int creates) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    CHECK(app_setup(a, "Feral3D") == 0, "setup");
    app_present(a); /* the session starts; mono */
    CHECK(V.buffer_creates == 0 && V.img2buf == 0, "a dump buffer before the first eye frame");
    atomic_store(&g_sh.eyes.expect_w, DUMP_W);
    atomic_store(&g_sh.eyes.expect_h, DUMP_H);
    VkImage L = app_image(a, DUMP_W, DUMP_H, FERAL_USAGE), R = app_image(a, DUMP_W, DUMP_H, FERAL_USAGE);
    for (int e = 0; e < 2; e++) {
        const uint8_t *c = e ? DUMP_R : DUMP_L;
        int ix = img_ix(e ? R : L);
        CHECK(ix >= 0, "image table full");
        if (ix >= 0) memcpy(g_img[ix].fill, (const uint8_t[4]){bgra ? c[2] : c[0], c[1], bgra ? c[0] : c[2], 255}, 4);
    }
    char ppm[700];
    snprintf(ppm, sizeof ppm, "%s.stereo%d.ppm", g_log, dumped ? dumped : 2);
    unlink(ppm); /* a TMPDIR used before */
    somvr_views v;
    for (int f = 1; f <= 3; f++) {
        CHECK(locate(&v), "locate");
        eyes_info(1, DUMP_W, DUMP_H, L, R);
        write_record((uint64_t)f, SOMVR_LAYOUT_B, &v, 3);
        V.ncp = V.ntr = 0;
        int i2b = V.img2buf, hb = V.host_barriers, d = f == dumped;
        tagged_present_eyes(a, (uint64_t)f, 1, TAG_SYNC, 3, 1);
        check_eyes_frame(&v, L, R, DUMP_W, DUMP_H, "dump");
        VkImage xr = V.cp[0].d;
        CHECK(V.img2buf == i2b + d && V.host_barriers == hb + d && V.ntr == 6 + d,
              "frame %d: %d buffer copies, %d host-read barriers, %d image barriers", f, V.img2buf - i2b,
              V.host_barriers - hb, V.ntr);
        CHECK(d ? barrier_seen(xr, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) &&
                      barrier_seen(xr, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) &&
                      !barrier_seen(xr, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                : barrier_seen(xr, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) &&
                      !barrier_seen(xr, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
              "frame %d: the XR image's barriers (dumped %d)", f, d);
        CHECK(V.buffer_creates == creates, "frame %d: %d buffer creations, want %d (at the first eye frame)", f,
              V.buffer_creates, creates);
        CHECK((access(ppm, F_OK) == 0) == (dumped && f >= dumped), "frame %d: %s %s", f, ppm,
              dumped && f >= dumped ? "missing" : "exists");
    }
    CHECK(atomic_load(&g_sh.n_stereo) == 3 && atomic_load(&g_sh.eyes.n_submits) == 3, "stereo %llu, eye submits %llu",
          (unsigned long long)atomic_load(&g_sh.n_stereo), (unsigned long long)atomic_load(&g_sh.eyes.n_submits));
    return 0;
}
static int sc_dump_fmt(VkFormat eye_fmt, int bgra, int xr_fmt) { /* SOMVR_XR_DUMP=2: the whole XR image, both eyes */
    V.eye_fmt = eye_fmt;
    app_t a;
    if (dump_run(&a, bgra, 2, 1)) return 1;
    CHECK(V.buffers_live == 1 && V.mems_live == 1 && V.alloc_type == 3, "buffers %d, memory %d of type %d (want 3: "
          "cached, not device-local)", V.buffers_live, V.mems_live, V.alloc_type);
    /* P6, 2W x H: every pixel of the left half is eye L's colour, of the right half eye R's (RGB whatever the format) */
    char ppm[700], want[64];
    snprintf(ppm, sizeof ppm, "%s.stereo2.ppm", g_log);
    int hl = snprintf(want, sizeof want, "P6\n%d %d\n255\n", 2 * DUMP_W, DUMP_H);
    static uint8_t buf[64 + 2 * DUMP_W * DUMP_H * 3 + 1];
    size_t n = (size_t)hl + 2 * DUMP_W * DUMP_H * 3;
    FILE *f = fopen(ppm, "rb");
    size_t got = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    CHECK(got == n && !memcmp(buf, want, (size_t)hl), "%s: %zu bytes (want %zu), header \"%.*s\"", ppm, got, n, hl,
          (const char *)buf);
    int bad = 0;
    for (int y = 0; got == n && y < DUMP_H; y++)
        for (int x = 0; x < 2 * DUMP_W; x++)
            bad += memcmp(buf + hl + ((size_t)y * 2 * DUMP_W + (size_t)x) * 3, x < DUMP_W ? DUMP_L : DUMP_R, 3) != 0;
    const uint8_t *p0 = buf + hl, *pw = buf + hl + DUMP_W * 3;
    CHECK(got == n && bad == 0, "%d pixels not their eye's colour; x = 0: %u,%u,%u, x = W: %u,%u,%u", bad, p0[0], p0[1],
          p0[2], pw[0], pw[1], pw[2]);
    char line[800];
    snprintf(line, sizeof line, "xr: dump stereo frame 2 -> %s (%dx%d format %d, sRGB bytes as is)", ppm, 2 * DUMP_W,
             DUMP_H, xr_fmt);
    CHECK(log_has(line), "log lacks \"%s\"", line);
    CHECK(log_has("xr: dump buffer 16384 bytes, memory type 3") && log_has("xr: dump after layout-B stereo submit 2 ->"),
          "log lacks the dump's config / buffer line");
    CHECK(!log_has("dump off"), "the dump went off");
    app_teardown(&a);
    CHECK(V.buffers_live == 0 && V.mems_live == 0, "the dump buffer outlived the device");
    check_mock_clean();
    return 0;
}
static int sc_dump(void) { return sc_dump_fmt(VK_FORMAT_R8G8B8A8_UNORM, 0, VK_FORMAT_R8G8B8A8_SRGB); }
static int sc_dump_bgra(void) { return sc_dump_fmt(VK_FORMAT_B8G8R8A8_UNORM, 1, VK_FORMAT_B8G8R8A8_SRGB); }
static int sc_dump_fail(int alloc) { /* no buffer, or no memory for it: the dump is off, the frames as without it */
    if (alloc) V.fail_alloc = 1;
    else V.fail_buffer = 1;
    app_t a;
    if (dump_run(&a, 0, 0, 1)) return 1;
    CHECK(V.buffers_live == 0 && V.mems_live == 0, "a half-made dump buffer was left: buffers %d, memory %d",
          V.buffers_live, V.mems_live);
    const char *why = alloc ? "xr: dump off: vkAllocateMemory: -2" : "xr: dump off: vkCreateBuffer: -2";
    CHECK(log_count("dump off") == 1 && log_has(why), "log: %d \"dump off\" lines, want one \"%s\"",
          log_count("dump off"), why);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_dump_nobuffer(void) { return sc_dump_fail(0); }
static int sc_dump_nomemory(void) { return sc_dump_fail(1); }
static int sc_dump_nolog(void) { /* SOMVR_XR_DUMP without SOMVR_XR_LOG: off, nothing made, nothing written */
    app_t a;
    if (chdir(g_tmp)) return 1; /* the file a log-less dump would write (".stereo2.ppm") lands in TMPDIR */
    unlink(".stereo2.ppm");
    if (dump_run(&a, 0, 0, 0)) return 1;
    CHECK(access(".stereo2.ppm", F_OK) != 0 && access(g_log, F_OK) != 0, "a file was written without a log");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* Headset look 1 (re-xr-pipeline.md 7): the game thread locates frame i's views at H4 while the render thread still
 * presents frame i - 2, so the views' time comes from the xrWaitFrame two presents before the one whose frame state
 * ends their frame. Layout B with the eye images; the preload side widens the located fovs by SOMVR_OVERSCAN = 0.1 as
 * eye0_setup does. Each stereo frame: the projection layer has the record's poses (located for the record's time) and
 * the rendered (widened) fov, the runtime's fov strictly inside; xrEndFrame's displayTime is the frame's predicted one
 * (the mock flags anything else). mode 0 = SOMVR_XR_PREDICT=off: the views stay 2 periods early (the -33 ms of the
 * headset look at 60 fps; the mock's period is 11.1 ms); 1 = auto (default): exact from the first record located after
 * the first measurement (record 4: records 2 and 3 were located before it), except the one extrapolated record (+1
 * period); 2 = a fixed 5 ms. */
#define PRED_N 24
static int sc_predict_run(int mode) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a); /* the session starts; mono */
    atomic_store(&g_sh.eyes.expect_w, 1832);
    atomic_store(&g_sh.eyes.expect_h, 1920);
    VkImage L = app_image(&a, 1832, 1920, FERAL_USAGE), R = app_image(&a, 1832, 1920, FERAL_USAGE);
    eyes_info(1, 1832, 1920, L, R);
    const int64_t P = 11111111, lead = mode == 1 ? 2 * P : mode == 2 ? 5000000 : 0;
    const float S = 0.1f;
    static somvr_views v[PRED_N + 1], raw[PRED_N + 1];
    int exact = 0;
    for (int i = 1; i <= PRED_N; i++) {
        /* game thread, H4 of frame i: the latest published time (published = that wait's + the lead) */
        int64_t pub = g_sh.xr.predicted_time;
        CHECK(locate(&v[i]) && v[i].fresh && !v[i].extrapolated && v[i].time == pub, "frame %d: locate", i);
        if (i == 12) { /* the game thread ran ahead once (no new xrWaitFrame): extrapolated, not one of the layer's */
            CHECK(locate(&v[i]) && v[i].extrapolated && v[i].time == pub + P, "frame 12: extrapolated");
        }
        raw[i] = v[i];
        for (int e = 0; e < 2; e++) v[i].fov[e] = sv_fov_overscan(&raw[i].fov[e], S); /* eye0_setup */
        write_record((uint64_t)i, SOMVR_LAYOUT_B, &v[i], 3);
        /* render thread: the present of frame i - 2 (the first two presents are still flat frames) */
        int k = i - 2;
        V.ncp = V.ntr = 0;
        if (k < 1) {
            tagged_present(&a, 0, 0, TAG_SYNC);
            continue;
        }
        tagged_present_eyes(&a, (uint64_t)k, 1, TAG_SYNC, 3, 1);
        char what[64];
        snprintf(what, sizeof what, "predict mode %d frame %d", mode, k);
        check_eyes_frame(&v[k], L, R, 1832, 1920, what);
        const mockxr_end *e = MOCK(mockxr_last_end)();
        for (int s = 0; s < 2; s++) /* what the game rendered (the widened fov) contains the runtime's */
            CHECK(e->fov[s][0] < raw[k].fov[s].l && e->fov[s][1] > raw[k].fov[s].r && e->fov[s][2] > raw[k].fov[s].u &&
                      e->fov[s][3] < raw[k].fov[s].d,
                  "%s: view %d fov %g..%g %g..%g does not contain the runtime's %g..%g %g..%g", what, s, e->fov[s][0],
                  e->fov[s][1], e->fov[s][3], e->fov[s][2], raw[k].fov[s].l, raw[k].fov[s].r, raw[k].fov[s].d,
                  raw[k].fov[s].u);
        int64_t err = v[k].time - e->time, want = lead - 2 * P + (k == 12 ? P : 0);
        if (mode == 1) want = k < 4 ? -2 * P : k == 12 ? P : 0;
        CHECK(err == want, "%s: display-time error %lld ns, want %lld", what, (long long)err, (long long)want);
        exact += err == 0;
    }
    CHECK(g_sh.xr.predicted_time == MOCK(mockxr_time)() + lead, "published %lld = the last wait's %lld + %lld",
          (long long)g_sh.xr.predicted_time, (long long)MOCK(mockxr_time)(), (long long)lead);
    CHECK(MOCK(mockxr_locate_bad)() == 0 && atomic_load(&g_sh.n_stereo) == PRED_N - 2, "locate_bad %d stereo %llu",
          MOCK(mockxr_locate_bad)(), (unsigned long long)atomic_load(&g_sh.n_stereo));
    if (mode == 1)
        CHECK(exact == PRED_N - 2 - 3 - 1, "auto: %d exact frames", exact);
    app_teardown(&a);
    const char *pl = mode == 1 ? "xr: prediction auto: the views' time published = predictedDisplayTime + the pipeline"
                     : mode == 2 ? "xr: prediction fixed: the views' time published = predictedDisplayTime + 5 ("
                                 : "xr: prediction off: the views' time published = predictedDisplayTime + 0 (";
    CHECK(log_has(pl), "log lacks \"%s\"", pl);
    CHECK(log_has("(tag 3, applied 0x3, flags 0x0, display-time error -22.222 ms): views") || mode == 2,
          "the first stereo frame's line: error -22.222 ms");
    CHECK(mode != 1 || log_has("xr: prediction lead +22.222 ms (median of 1; this frame +22.222 ms, 2.00 periods of "
                               "11.111 ms): published from the next xrWaitFrame on"),
          "auto: no lead line");
    CHECK(mode != 0 || log_has("xr: prediction lead +22.222 ms (median of 1; this frame +22.222 ms, 2.00 periods of "
                               "11.111 ms) (not applied)"),
          "off: the lead is measured and logged, not applied");
    CHECK(log_has("fov L/R/U/D -0.9") && log_has("lead +"), "the stereo frame line lacks the full fov / the lead");
    check_mock_clean();
    return 0;
}
static int sc_predict(void) { return sc_predict_run(1); }
static int sc_predict_off(void) { return sc_predict_run(0); }
static int sc_predict_fixed(void) { return sc_predict_run(2); }

/* Headset look 1, "a blue light on the right edge" in the main menu: layout B's menus are mono quads of slot 0 in the
 * 2W-wide XR image, whose right half is never written by a mono frame. The copy now discards the old contents
 * (UNDEFINED) and repeats the game image's last column into the 4 columns right of the slot, so a bilinear tap at
 * the quad's right edge reads the edge; the quad still shows exactly slot 0. SOMVR_XR_DUMP=m3,1: the 3rd mono frame's
 * whole XR image -> <log>.mono3.ppm, the 1st stereo submit -> <log>.stereo1.ppm (the kinds kept apart). A stereo frame
 * from the eye images in between, and a flat frame after it. */
static int sc_quad_guard(void) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    static const uint8_t FILL[4] = {40, 50, 60, 255}, EDGE[4] = {250, 120, 10, 255}; /* B G R A in memory */
    for (int k = 0; k < 3; k++) { /* the game images: one colour, a different last column */
        int ix = img_ix(GAME_IMG(k));
        CHECK(ix >= 0, "image table full");
        if (ix < 0) return 1;
        memcpy(g_img[ix].fill, FILL, 4);
        g_img[ix].nsp = 1;
        g_img[ix].sp[0].x0 = 1919, g_img[ix].sp[0].x1 = 1920;
        memcpy(g_img[ix].sp[0].px, EDGE, 4);
    }
    char ppm[700], other[3][700];
    snprintf(ppm, sizeof ppm, "%s.mono3.ppm", g_log);
    unlink(ppm);
    snprintf(other[0], sizeof other[0], "%s.mono1.ppm", g_log);
    snprintf(other[1], sizeof other[1], "%s.mono2.ppm", g_log);
    snprintf(other[2], sizeof other[2], "%s.stereo1.ppm", g_log);
    for (int i = 0; i < 3; i++) unlink(other[i]);
    for (int f = 1; f <= 5; f++) {
        if (f == 4) { /* a stereo frame from the eye images: unchanged */
            atomic_store(&g_sh.eyes.expect_w, 1920);
            atomic_store(&g_sh.eyes.expect_h, 1080);
            VkImage L = app_image(&a, 1920, 1080, FERAL_USAGE), R = app_image(&a, 1920, 1080, FERAL_USAGE);
            somvr_views v;
            CHECK(locate(&v), "locate");
            eyes_info(1, 1920, 1080, L, R);
            write_record(1, SOMVR_LAYOUT_B, &v, 3);
            V.ncp = V.ntr = 0;
            tagged_present_eyes(&a, 1, 1, TAG_SYNC, 3, 1);
            check_eyes_frame(&v, L, R, 1920, 1080, "stereo between the menu frames");
            continue;
        }
        V.ntr = V.ncp = 0;
        int c0 = V.copies;
        tagged_present(&a, 0, 0, TAG_SYNC); /* a flat frame (menus): mono */
        const mockxr_end *e = MOCK(mockxr_last_end)();
        CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->rect[0][0] == 0 && e->rect[0][1] == 0 &&
                  e->rect[0][2] == 1920 && e->rect[0][3] == 1080 && MOCK(mockxr_swapchain_get)()->width == 3840,
              "frame %d: quad %d,%d %dx%d in a %u wide image", f, e->rect[0][0], e->rect[0][1], e->rect[0][2],
              e->rect[0][3], MOCK(mockxr_swapchain_get)()->width);
        VkImage xr = V.copy_dst;
        /* two vkCmdCopyImage: the image into slot 0, then the guard (one 1-column region per guard column) */
        CHECK(V.copies == c0 + 2 && V.ncp == 2 && V.cp[0].r.dstOffset.x == 0 && V.cp[0].r.extent.width == 1920 &&
                  V.cp[1].s == V.cp[0].s && V.cp[1].d == xr && V.copy_n == SOMVR_GUARD_COLS &&
                  V.copy[0].srcOffset.x == 1919 && V.copy[0].dstOffset.x == 1920 && V.copy[1].srcOffset.x == 1919 &&
                  V.copy[1].dstOffset.x == 1921 && V.copy[0].extent.width == 1 && V.copy[0].extent.height == 1080,
              "frame %d: %d copy calls, %u guard regions, guard src x %d -> dst x %d (%ux%u)", f, V.copies - c0,
              V.copy_n, V.copy[0].srcOffset.x, V.copy[0].dstOffset.x, V.copy[0].extent.width, V.copy[0].extent.height);
        CHECK(barrier_seen(xr, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
                  !barrier_seen(xr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
              "frame %d: a mono frame must not keep the stale right half", f);
        int bad = 0;
        for (int32_t x = 1920; x < 1920 + (int32_t)SOMVR_GUARD_COLS; x++) bad += memcmp(img_px(xr, x), EDGE, 4) != 0;
        CHECK(bad == 0 && memcmp(img_px(xr, 1924), EDGE, 4) != 0 && !memcmp(img_px(xr, 0), FILL, 4),
              "frame %d: the guard columns are not the edge column (%d wrong)", f, bad);
        CHECK((access(ppm, F_OK) == 0) == (f >= 3), "frame %d: %s %s", f, ppm, f >= 3 ? "missing" : "exists");
    }
    CHECK(V.buffer_creates == 1, "dump buffers made %d", V.buffer_creates);
    /* <log>.mono3.ppm: 3840 x 1080, the game image in the left half, the edge colour in x = 1920..1923 (RGB: the
     * XR format is B8G8R8A8_SRGB), the rest of the right half whatever it was (the mock's 0) */
    static uint8_t buf[32 + 3840 * 1080 * 3];
    char want[32];
    int hl = snprintf(want, sizeof want, "P6\n%d %d\n255\n", 3840, 1080);
    FILE *fp = fopen(ppm, "rb");
    size_t got = fp ? fread(buf, 1, sizeof buf, fp) : 0;
    if (fp) fclose(fp);
    CHECK(got == (size_t)hl + 3840 * 1080 * 3 && !memcmp(buf, want, (size_t)hl), "%s: %zu bytes", ppm, got);
    const uint8_t fill_rgb[3] = {FILL[2], FILL[1], FILL[0]}, edge_rgb[3] = {EDGE[2], EDGE[1], EDGE[0]};
    int bad = 0;
    for (int y = 0; got == (size_t)hl + 3840 * 1080 * 3 && y < 1080; y += 1079)
        for (int x = 0; x < 1930; x++) {
            const uint8_t *p = buf + hl + ((size_t)y * 3840 + (size_t)x) * 3;
            bad += x < 1920 ? memcmp(p, fill_rgb, 3) != 0 : x < 1924 ? memcmp(p, edge_rgb, 3) != 0 : !memcmp(p, edge_rgb, 3);
        }
    CHECK(bad == 0, "%s: %d pixels wrong in rows 0 and 1079", ppm, bad);
    char line[800];
    snprintf(line, sizeof line, "xr: dump mono frame 3 -> %s (3840x1080 format %d, sRGB bytes as is)", ppm,
             VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(log_has(line), "log lacks \"%s\"", line);
    CHECK(!log_has("dump off") && log_has("xr: dump stereo frame 1 -> ") && access(other[2], F_OK) == 0 &&
              access(other[0], F_OK) != 0 && access(other[1], F_OK) != 0,
          "the dumps: off, or the stereo entry taken by a mono frame / the mono one by a stereo frame");
    app_teardown(&a);
    unlink(ppm);
    for (int i = 0; i < 3; i++) unlink(other[i]);
    check_mock_clean();
    return 0;
}

static int sc_stereo_fifo(void) { /* presents from another thread (a submit thread): the oldest tag first */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    somvr_views v1, v2;
    CHECK(locate(&v1) && locate(&v2), "locate");
    write_record(1, SOMVR_LAYOUT_A, &v1, 3);
    write_record(2, SOMVR_LAYOUT_A, &v2, 3);
    somvr_tag_push(&g_sh, 1, 0, 4242);
    somvr_tag_push(&g_sh, 2, 0, 4242);
    app_present(&a);
    check_proj(&v1, 3, RECT_A, "fifo 1");
    app_present(&a);
    check_proj(&v2, 3, RECT_A, "fifo 2");
    app_present(&a);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD, "fifo empty: mono");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* a game thread locating views the whole time while the device (and the session) go away */
static _Atomic int g_gt_stop, g_gt_ok, g_gt_calls;
static void *game_thread(void *arg) {
    (void)arg;
    somvr_views_state st;
    memset(&st, 0, sizeof st);
    somvr_views v;
    while (!atomic_load(&g_gt_stop)) {
        atomic_fetch_add(&g_gt_calls, 1);
        if (somvr_views_locate(&g_sh, &st, &v)) atomic_fetch_add(&g_gt_ok, 1);
    }
    return NULL;
}
static int sc_provider_teardown(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    pthread_t t;
    pthread_create(&t, NULL, game_thread, NULL);
    somvr_views v;
    for (uint64_t f = 1; f <= 8; f++) {
        CHECK(locate(&v), "frame %llu: locate", (unsigned long long)f);
        write_record(f, SOMVR_LAYOUT_A, &v, 3);
        tagged_present(&a, f, 0, TAG_SYNC);
        check_proj(&v, 3, RECT_A, "with a concurrent game thread");
    }
    app_teardown(&a); /* vkDestroyDevice: unpublish, wait for the provider, destroy */
    usleep(20000);
    atomic_store(&g_gt_stop, 1);
    pthread_join(t, NULL);
    CHECK(atomic_load(&g_gt_ok) > 0, "the game thread never located views (%d calls)", atomic_load(&g_gt_calls));
    CHECK(MOCK(mockxr_locate_bad)() == 0, "%d xrLocateViews calls without a live session", MOCK(mockxr_locate_bad)());
    CHECK(atomic_load(&g_sh.xr.state) == SOMVR_XRS_DEAD && atomic_load(&g_sh.xr.users) == 0, "state %u users %u",
          atomic_load(&g_sh.xr.state), atomic_load(&g_sh.xr.users));
    check_mock_clean();
    fprintf(stderr, "    game thread: %d calls, %d located, %d runtime calls in all, none after the destroy\n",
            atomic_load(&g_gt_calls), atomic_load(&g_gt_ok), MOCK(mockxr_locates)());
    return 0;
}

static int sc_space(int want) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    CHECK(MOCK(mockxr_space_type)() == want && g_sh.xr.space_type == (uint32_t)want, "space %d published %u, want %d",
          MOCK(mockxr_space_type)(), g_sh.xr.space_type, want);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_stage(void) { return sc_space(XR_REFERENCE_SPACE_TYPE_STAGE); }
static int sc_stage_missing(void) {
    int r = sc_space(XR_REFERENCE_SPACE_TYPE_LOCAL);
    CHECK(log_has("no STAGE space"), "log lacks the fallback");
    return r;
}

/* SOMVR_XR_FLOOR (re-xr-pipeline.md 35.1): in LOCAL, 3 frames in, LOCAL's origin located in a STAGE space made next to
 * it: one line with the player's real eye height; the space destroyed with the session; none in STAGE or without one */
static int sc_floor(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 40; i++) app_present(&a); /* no stereo frame here: at 10 x 3 frames */
    CHECK(MOCK(mockxr_floor_locates)() == 1 && log_has("xr: the floor (STAGE): LOCAL's origin 1.230 m above it"),
          "the floor line once (%d locates)", MOCK(mockxr_floor_locates)());
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_floor_none(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 14; i++) app_present(&a);
    CHECK(MOCK(mockxr_floor_locates)() == 0 && log_has("xr: the floor: no STAGE space"), "no STAGE: a line, no locate");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

static int sc_recenter_event(void) { /* the runtime recentres LOCAL: the game side is told to re-latch */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 4; i++) app_present(&a);
    CHECK(atomic_load(&g_sh.xr.recenter_seq) == 1, "recenter_seq %u", atomic_load(&g_sh.xr.recenter_seq));
    app_teardown(&a);
    CHECK(log_has("changes (recentre)"), "log line");
    check_mock_clean();
    return 0;
}

/* ---- motion controllers (docs/re-xr-input.md section 1): the preload's somvr_input, the mock's action API -------- */
static somvr_input_shared *g_in;
/* the preload's object: a library of its own (input_obj.so next to the mock), in the global scope like LD_PRELOAD */
static int input_obj(uint32_t want, int active, uint32_t size) {
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/input_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (!h || !(g_in = (somvr_input_shared *)dlsym(h, SOMVR_INPUT_SYMBOL)))
        return fprintf(stderr, "    input_obj.so: %s\n", dlerror()), -1;
    g_in->magic = SOMVR_INPUT_MAGIC;
    g_in->version = SOMVR_INPUT_VERSION;
    g_in->size = size;
    g_in->want = want;
    g_in->pid = (int32_t)getpid();
    if (active) atomic_store(&g_in->active, 1);
    return 0;
}
#define INW_ALL (SOMVR_INW_STATE | SOMVR_INW_HAPTICS | SOMVR_INW_SPACES)
static const char *const INPUT_CALLS[] = {
    "xrStringToPath", "xrPathToString", "xrCreateActionSet", "xrDestroyActionSet", "xrCreateAction",
    "xrSuggestInteractionProfileBindings", "xrCreateActionSpace", "xrAttachSessionActionSets", "xrSyncActions",
    "xrGetActionStateBoolean", "xrGetActionStateFloat", "xrGetActionStateVector2f", "xrGetActionStatePose",
    "xrGetCurrentInteractionProfile", "xrLocateSpace", "xrApplyHapticFeedback", "xrStopHapticFeedback"};
static int input_calls(void) {
    int n = 0;
    for (size_t i = 0; i < sizeof INPUT_CALLS / sizeof INPUT_CALLS[0]; i++) n += MOCK(mockxr_calls)(INPUT_CALLS[i]);
    return n;
}
static int calls(const char *n) { return MOCK(mockxr_calls)(n); }
static int first(const char *n) { return MOCK(mockxr_first)(n); }
static const mockxr_input *minfo(void) { return MOCK(mockxr_input_get)(); }
static void hand_set(int h, const mockxr_hand *v) { MOCK(mockxr_input_set)(h, v); }
static somvr_input_state snap(void) {
    somvr_input_state st;
    memset(&st, 0, sizeof st);
    CHECK(somvr_input_read(g_in, &st), "torn snapshot");
    return st;
}
/* one stereo frame of layout A (the input path must leave it exactly as without input) */
static void stereo_frame(app_t *a, uint64_t f, const char *what) {
    somvr_views v;
    CHECK(locate(&v), "%s: locate", what);
    write_record(f, SOMVR_LAYOUT_A, &v, 3);
    tagged_present(a, f, 0, TAG_SYNC);
    check_proj(&v, 3, RECT_A, what);
}
static const mockxr_hand HAND_R = {.buttons = SOMVR_BTN_LOWER | SOMVR_BTN_MENU | SOMVR_BTN_THUMB_TOUCH,
                                   .trigger = 0.8f, .squeeze = 0.25f, .stick = {0.0f, 1.0f},
                                   .grip = {0, 0.258819f, 0, 0.965926f, 0.3f, -0.3f, -0.4f},
                                   .aim = {0.1f, 0.2f, 0.3f, 0.927362f, 0.31f, -0.28f, -0.47f},
                                   .lin_vel = {0.5f, 0, -0.25f}, .ang_vel = {0, 1.5f, 0},
                                   .grip_flags = 0xf, .aim_flags = 0x3, .vel_flags = 3};
static void check_hand(const somvr_hand_in *h, const mockxr_hand *m, uint32_t buttons, const char *what) {
    CHECK(h->buttons == buttons, "%s: buttons 0x%x, want 0x%x", what, h->buttons, buttons);
    CHECK(h->trigger == m->trigger && h->squeeze == m->squeeze && h->stick[0] == m->stick[0] &&
              h->stick[1] == m->stick[1],
          "%s: trigger %g squeeze %g stick %g,%g", what, h->trigger, h->squeeze, h->stick[0], h->stick[1]);
    CHECK(!memcmp(h->grip.q, m->grip, 16) && !memcmp(h->grip.p, m->grip + 4, 12) && !memcmp(h->aim.q, m->aim, 16) &&
              !memcmp(h->aim.p, m->aim + 4, 12),
          "%s: grip %g,%g,%g / aim %g,%g,%g are not the runtime's", what, h->grip.p[0], h->grip.p[1], h->grip.p[2],
          h->aim.p[0], h->aim.p[1], h->aim.p[2]);
    CHECK(!memcmp(h->lin_vel, m->lin_vel, 12) && !memcmp(h->ang_vel, m->ang_vel, 12), "%s: velocities", what);
    CHECK(h->grip_flags == m->grip_flags && h->aim_flags == m->aim_flags && h->vel_flags == m->vel_flags,
          "%s: flags grip 0x%x aim 0x%x vel 0x%x", what, h->grip_flags, h->aim_flags, h->vel_flags);
    CHECK(h->active == (SOMVR_HACT_POSE | SOMVR_HACT_INPUT), "%s: active 0x%x", what, h->active);
}
#define STATE_READS_PER_SYNC 19 /* per hand: pose, trigger, squeeze, stick, 6 buttons (menu left only): 10 + 9 */
#define FULL_FLAGS (SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED | SOMVR_INS_LOCATED)

static int sc_input(void) { /* the whole path: order, sync per frame, values, presses, profile, haptics, teardown */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    somvr_haptic_req stale = {.source = SOMVR_HAP_TEST, .amplitude = 1.0f, .duration_ns = 500000000};
    somvr_haptic_post(g_in, SOMVR_HAND_R, &stale); /* before the session: stale, never applied */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(input_calls() == 0, "input calls before the first present: %d", input_calls());
    hand_set(SOMVR_HAND_R, &HAND_R);
    app_present(&a); /* session + input setup; focused at once; two syncs (the frame's wait, the next frame's) */
    /* the order of re-xr-input.md 1.4 */
    int o[] = {first("xrCreateActionSet"), first("xrCreateAction"), first("xrSuggestInteractionProfileBindings"),
               first("xrCreateSession"), first("xrCreateReferenceSpace"), first("xrCreateActionSpace"),
               first("xrAttachSessionActionSets"), first("xrBeginSession"), first("xrWaitFrame"), first("xrSyncActions")};
    int ordered = 1;
    for (size_t i = 1; i < sizeof o / sizeof o[0]; i++) ordered &= o[i - 1] > 0 && o[i - 1] < o[i];
    CHECK(ordered, "order set %d action %d suggest %d session %d refspace %d actionspace %d attach %d begin %d wait %d "
          "sync %d", o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9]);
    CHECK(calls("xrCreateActionSet") == 1 && calls("xrCreateAction") == 12 &&
              calls("xrSuggestInteractionProfileBindings") == 3 && calls("xrCreateActionSpace") == 4 &&
              calls("xrAttachSessionActionSets") == 1 && minfo()->suggested_ok == 3,
          "setup calls: sets %d actions %d suggestions %d (%d taken) spaces %d attach %d", calls("xrCreateActionSet"),
          calls("xrCreateAction"), calls("xrSuggestInteractionProfileBindings"), minfo()->suggested_ok,
          calls("xrCreateActionSpace"), calls("xrAttachSessionActionSets"));
    CHECK(log_has("xr: input: action set somvr, 12 actions; profiles touch=ok index=ok simple=ok") &&
              log_has("xr: input: 4 action spaces, attached (somvr_input)") &&
              log_has("xr: input: somvr_input found at") && log_has("published to somvr_input"),
          "setup log lines");
    /* the ABI's xr block */
    uint64_t sp[4] = {g_in->xr.grip_space[0], g_in->xr.grip_space[1], g_in->xr.aim_space[0], g_in->xr.aim_space[1]};
    int distinct = 1;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j <= i; j++) distinct &= sp[i] && (i == j || sp[i] != sp[j]);
    CHECK(atomic_load(&g_in->xr.ready) == 1 && distinct && g_in->xr.locate_space && !g_in->xr.locate_spaces &&
              g_in->xr.profiles == ((1u << SOMVR_PROF_TOUCH) | (1u << SOMVR_PROF_INDEX) | (1u << SOMVR_PROF_SIMPLE)),
          "xr block: ready %u spaces distinct %d locate %p profiles 0x%x", atomic_load(&g_in->xr.ready), distinct,
          g_in->xr.locate_space, g_in->xr.profiles);
    /* one sync per xrWaitFrame, the snapshot of the last one */
    somvr_input_state st = snap();
    CHECK(calls("xrSyncActions") == 2 && calls("xrWaitFrame") == 2 && st.sync_seq == 2,
          "syncs %d waits %d sync_seq %llu", calls("xrSyncActions"), calls("xrWaitFrame"),
          (unsigned long long)st.sync_seq);
    CHECK(st.time == MOCK(mockxr_time)() && minfo()->locate_time[1] == st.time && st.sync_ns > 0,
          "snapshot time %lld, predicted %lld, grip located for %lld", (long long)st.time,
          (long long)MOCK(mockxr_time)(), (long long)minfo()->locate_time[1]);
    CHECK(st.flags == FULL_FLAGS, "flags 0x%x", st.flags);
    check_hand(&st.hand[SOMVR_HAND_R], &HAND_R, SOMVR_BTN_LOWER | SOMVR_BTN_THUMB_TOUCH, "R"); /* menu: left only */
    CHECK(st.hand[SOMVR_HAND_R].presses[0] == 1 && st.hand[SOMVR_HAND_R].presses[5] == 1 &&
              st.hand[SOMVR_HAND_R].presses[3] == 0,
          "presses after the first press: %u %u %u", st.hand[1].presses[0], st.hand[1].presses[5], st.hand[1].presses[3]);
    CHECK(st.hand[SOMVR_HAND_L].active == 3 && st.hand[SOMVR_HAND_L].buttons == 0 &&
              st.hand[SOMVR_HAND_L].grip.p[0] < -0.19f && st.hand[SOMVR_HAND_L].grip_flags == 0xf,
          "L at rest: active 0x%x buttons 0x%x x %g", st.hand[0].active, st.hand[0].buttons, st.hand[0].grip.p[0]);
    CHECK(st.profile[0] == SOMVR_PROF_NONE && minfo()->prof_events == 0, "a profile before the event");
    CHECK(atomic_load(&g_in->n_syncs) == 2 && atomic_load(&g_in->n_unfocused) == 0 &&
              atomic_load(&g_in->n_state_reads) == 2 * STATE_READS_PER_SYNC,
          "counters syncs %llu unfocused %llu reads %llu", (unsigned long long)atomic_load(&g_in->n_syncs),
          (unsigned long long)atomic_load(&g_in->n_unfocused), (unsigned long long)atomic_load(&g_in->n_state_reads));
    CHECK(MOCK(mockxr_haptic_get)(SOMVR_HAND_R)->applies == 0, "the stale request was applied");
    /* the profile event (sent after the first focused sync) is read at the next present */
    stereo_frame(&a, 1, "stereo with input");
    st = snap();
    CHECK(minfo()->prof_events == 1 && st.profile[0] == SOMVR_PROF_TOUCH && st.profile[1] == SOMVR_PROF_TOUCH &&
              log_has("xr: input: profile L=/interaction_profiles/oculus/touch_controller "
                      "R=/interaction_profiles/oculus/touch_controller"),
          "profile %u/%u after %d events", st.profile[0], st.profile[1], minfo()->prof_events);
    CHECK(st.sync_seq == 3 && calls("xrSyncActions") == calls("xrWaitFrame"), "sync_seq %llu: syncs %d waits %d",
          (unsigned long long)st.sync_seq, calls("xrSyncActions"), calls("xrWaitFrame"));
    CHECK(st.hand[1].presses[0] == 1, "a held button counted again: %u", st.hand[1].presses[0]);
    /* release, press, hold: one press each time */
    mockxr_hand r = HAND_R;
    r.buttons = 0;
    hand_set(1, &r);
    stereo_frame(&a, 2, "released");
    st = snap();
    CHECK(st.hand[1].buttons == 0 && st.hand[1].presses[0] == 1, "release: buttons 0x%x presses %u", st.hand[1].buttons,
          st.hand[1].presses[0]);
    r.buttons = SOMVR_BTN_LOWER | SOMVR_BTN_UPPER;
    hand_set(1, &r);
    stereo_frame(&a, 3, "pressed again");
    stereo_frame(&a, 4, "held");
    st = snap();
    CHECK(st.hand[1].buttons == (SOMVR_BTN_LOWER | SOMVR_BTN_UPPER) && st.hand[1].presses[0] == 2 &&
              st.hand[1].presses[1] == 1 && st.sync_seq == 6,
          "second press: buttons 0x%x presses %u/%u sync_seq %llu", st.hand[1].buttons, st.hand[1].presses[0],
          st.hand[1].presses[1], (unsigned long long)st.sync_seq);
    /* the left menu button and the left hand's own pose */
    mockxr_hand l = HAND_R;
    l.buttons = SOMVR_BTN_MENU | SOMVR_BTN_STICK;
    l.grip[4] = -0.3f, l.aim[4] = -0.31f, l.stick[0] = -0.5f;
    hand_set(0, &l);
    stereo_frame(&a, 5, "left menu");
    st = snap();
    check_hand(&st.hand[0], &l, SOMVR_BTN_MENU | SOMVR_BTN_STICK, "L");
    CHECK(st.hand[0].presses[3] == 1 && st.hand[0].presses[2] == 1, "L presses menu %u stick %u", st.hand[0].presses[3],
          st.hand[0].presses[2]);
    /* the game thread's locate through the ABI (somvr's S2 does this at its input tick) */
    CHECK(somvr_xr_enter(&g_sh), "the core guard is closed while running");
    XrSpaceVelocity vel = {.type = XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation loc = {.type = XR_TYPE_SPACE_LOCATION, .next = &vel};
    XrResult xr = !g_in->xr.locate_space ? XR_ERROR_FUNCTION_UNSUPPORTED
                  : ((PFN_xrLocateSpace)g_in->xr.locate_space)((XrSpace)(uintptr_t)g_in->xr.grip_space[1],
                                                               (XrSpace)(uintptr_t)g_sh.xr.space, st.time + 5000000,
                                                               &loc);
    somvr_xr_leave(&g_sh);
    CHECK(xr == XR_SUCCESS && loc.locationFlags == 0xf && loc.pose.position.x == r.grip[4] &&
              vel.velocityFlags == 3 && minfo()->locate_time[1] == st.time + 5000000,
          "game-thread locate: %d flags 0x%llx x %g", xr, (unsigned long long)loc.locationFlags, loc.pose.position.x);
    /* haptics: one apply per new request number, a stop for amplitude 0 */
    somvr_haptic_req q = {.source = SOMVR_HAP_TEST, .amplitude = 0.5f, .frequency = 0, .duration_ns = 200000000};
    uint32_t n = somvr_haptic_post(g_in, SOMVR_HAND_R, &q);
    stereo_frame(&a, 6, "haptic");
    const mockxr_haptic *hr = MOCK(mockxr_haptic_get)(SOMVR_HAND_R);
    CHECK(n == 2 && hr->applies == 1 && hr->amplitude == 0.5f && hr->duration == 200000000 && hr->frequency == 0,
          "haptic: n %u applies %d amp %g dur %lld freq %g", n, hr->applies, hr->amplitude, (long long)hr->duration,
          hr->frequency);
    CHECK(log_has("xr: haptic R n=2 amp 0.50 200 ms -> XR_SUCCESS (src test"), "log lacks the haptic line");
    stereo_frame(&a, 7, "haptic taken");
    stereo_frame(&a, 8, "haptic taken");
    CHECK(hr->applies == 1 && hr->stops == 0, "a request applied again: %d", hr->applies);
    q.amplitude = 0;
    somvr_haptic_post(g_in, SOMVR_HAND_R, &q);
    q.amplitude = 2.0f, q.duration_ns = 0; /* the left hand: clamped to 1, the shortest pulse */
    somvr_haptic_post(g_in, SOMVR_HAND_L, &q);
    stereo_frame(&a, 9, "haptic stop");
    const mockxr_haptic *hl = MOCK(mockxr_haptic_get)(SOMVR_HAND_L);
    CHECK(hr->applies == 1 && hr->stops == 1 && hl->applies == 1 && hl->amplitude == 1.0f &&
              hl->duration == XR_MIN_HAPTIC_DURATION,
          "stop R %d/%d, L applies %d amp %g dur %lld", hr->applies, hr->stops, hl->applies, hl->amplitude,
          (long long)hl->duration);
    CHECK(atomic_load(&g_in->n_hap_applied) == 2 && atomic_load(&g_in->n_hap_stopped) == 1 &&
              atomic_load(&g_in->n_hap_posts) == 4 && log_has("xr: haptic R n=3 stop -> XR_SUCCESS"),
          "haptic counters applied %llu stopped %llu posts %llu", (unsigned long long)atomic_load(&g_in->n_hap_applied),
          (unsigned long long)atomic_load(&g_in->n_hap_stopped), (unsigned long long)atomic_load(&g_in->n_hap_posts));
    CHECK(atomic_load(&g_sh.n_stereo) == 9, "stereo frames %llu", (unsigned long long)atomic_load(&g_sh.n_stereo));
    uint64_t seq = snap().sync_seq;
    app_teardown(&a);
    st = snap();
    CHECK(atomic_load(&g_in->xr.ready) == 0 && st.flags == 0 && st.hand[1].buttons == 0 && st.hand[1].active == 0 &&
              st.sync_seq == seq && st.hand[1].presses[0] == 2,
          "after the teardown: ready %u flags 0x%x buttons 0x%x sync_seq %llu", atomic_load(&g_in->xr.ready), st.flags,
          st.hand[1].buttons, (unsigned long long)st.sync_seq);
    CHECK(minfo()->spaces_live == 0 && minfo()->sets_live == 0 && MOCK(mockxr_live)() == 0 &&
              MOCK(mockxr_space_bad)() == 0,
          "left alive: action spaces %d sets %d, live %d, bad locates %d", minfo()->spaces_live, minfo()->sets_live,
          MOCK(mockxr_live)(), MOCK(mockxr_space_bad)());
    check_mock_clean();
    return 0;
}

static int sc_input_unfocused(void) { /* MOCKXR_FOCUS_AT=3 MOCKXR_UNFOCUS_AT=6: nothing live while not focused */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    hand_set(SOMVR_HAND_R, &HAND_R);
    for (int f = 0; f < 3; f++) app_present(&a);
    somvr_input_state st = snap();
    CHECK(st.sync_seq == 4 && st.flags == 0 && st.hand[1].buttons == 0 && st.hand[1].active == 0 &&
              st.hand[1].presses[0] == 0,
          "unfocused: sync_seq %llu flags 0x%x buttons 0x%x active 0x%x", (unsigned long long)st.sync_seq, st.flags,
          st.hand[1].buttons, st.hand[1].active);
    CHECK(atomic_load(&g_in->n_unfocused) == 4 && atomic_load(&g_in->n_syncs) == 0 &&
              calls("xrGetActionStateBoolean") == 0 && calls("xrLocateSpace") == 0,
          "unfocused: %llu unfocused syncs, %llu syncs, %d state reads, %d locates",
          (unsigned long long)atomic_load(&g_in->n_unfocused), (unsigned long long)atomic_load(&g_in->n_syncs),
          calls("xrGetActionStateBoolean"), calls("xrLocateSpace"));
    CHECK(log_has("xr: input: sync n=1 focused=0 result=8 (XR_SESSION_NOT_FOCUSED)"), "log lacks the unfocused sync");
    app_present(&a); /* FOCUSED arrives */
    st = snap();
    CHECK(st.flags == FULL_FLAGS && st.hand[1].buttons == (SOMVR_BTN_LOWER | SOMVR_BTN_THUMB_TOUCH) &&
              st.hand[1].presses[0] == 1 && log_has("focused=1 result=0 (XR_SUCCESS)"),
          "focused: flags 0x%x buttons 0x%x presses %u", st.flags, st.hand[1].buttons, st.hand[1].presses[0]);
    for (int f = 0; f < 2; f++) app_present(&a);
    CHECK(snap().flags == FULL_FLAGS, "focus lost early");
    app_present(&a); /* ends = 6: VISIBLE (a system menu) */
    st = snap();
    CHECK(st.flags == 0 && st.hand[1].buttons == 0 && st.hand[1].active == 0 && st.hand[1].trigger == 0 &&
              atomic_load(&g_in->n_unfocused) == 5, /* syncs 1-4 and 8 */
          "unfocused again: flags 0x%x buttons 0x%x trigger %g unfocused %llu", st.flags, st.hand[1].buttons,
          st.hand[1].trigger, (unsigned long long)atomic_load(&g_in->n_unfocused));
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* one profile's suggestion fails (MOCKXR_FAIL=xrSuggestInteractionProfileBindings:1 = touch): the others are taken */
static int sc_input_profile_fail(int dev_prof, uint32_t want_btn_r, uint32_t want_btn_l) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand r = HAND_R, l = HAND_R;
    r.buttons = SOMVR_BTN_LOWER | SOMVR_BTN_UPPER;
    l.buttons = SOMVR_BTN_MENU;
    hand_set(1, &r);
    hand_set(0, &l);
    app_present(&a);
    stereo_frame(&a, 1, "after a failed suggestion");
    somvr_input_state st = snap();
    CHECK(log_has("profiles touch=XR_ERROR_RUNTIME_FAILURE index=ok simple=ok") &&
              g_in->xr.profiles == ((1u << SOMVR_PROF_INDEX) | (1u << SOMVR_PROF_SIMPLE)) &&
              calls("xrAttachSessionActionSets") == 1 && atomic_load(&g_in->xr.ready) == 1,
          "profiles 0x%x attach %d", g_in->xr.profiles, calls("xrAttachSessionActionSets"));
    CHECK(st.profile[0] == dev_prof && st.profile[1] == dev_prof && st.flags == FULL_FLAGS,
          "profile %u/%u want %d, flags 0x%x", st.profile[0], st.profile[1], dev_prof, st.flags);
    CHECK(st.hand[1].buttons == want_btn_r && st.hand[0].buttons == want_btn_l, "buttons R 0x%x (want 0x%x) L 0x%x "
          "(want 0x%x)", st.hand[1].buttons, want_btn_r, st.hand[0].buttons, want_btn_l);
    if (dev_prof == SOMVR_PROF_SIMPLE)
        CHECK(st.hand[1].squeeze == 0 && st.hand[1].stick[1] == 0 && st.hand[1].trigger == r.trigger &&
                  st.hand[1].active == (SOMVR_HACT_POSE | SOMVR_HACT_INPUT),
              "simple: squeeze %g stick %g trigger %g active 0x%x", st.hand[1].squeeze, st.hand[1].stick[1],
              st.hand[1].trigger, st.hand[1].active);
    else
        CHECK(st.hand[1].squeeze == r.squeeze && st.hand[1].stick[1] == 1.0f, "index: squeeze %g stick %g",
              st.hand[1].squeeze, st.hand[1].stick[1]);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_input_profile_index(void) { /* the Monado remote rig */
    return sc_input_profile_fail(SOMVR_PROF_INDEX, SOMVR_BTN_LOWER | SOMVR_BTN_UPPER, SOMVR_BTN_MENU);
}
static int sc_input_profile_simple(void) { /* Touch controllers, touch rejected: the runtime falls back to simple */
    return sc_input_profile_fail(SOMVR_PROF_SIMPLE, SOMVR_BTN_UPPER, SOMVR_BTN_MENU);
}

static int sc_input_profile_off(void) { /* MOCKXR_PROFILE_OFF_AT=3: the controllers sleep; the profile goes away */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    hand_set(SOMVR_HAND_R, &HAND_R);
    for (int f = 0; f < 2; f++) app_present(&a); /* syncs 1-3, the profile event read at present 2 */
    CHECK(snap().profile[1] == SOMVR_PROF_TOUCH && snap().hand[1].active == 3, "not live before the sleep");
    for (int f = 0; f < 2; f++) app_present(&a); /* the sync after present 3 sees no profile; present 4 reads it */
    somvr_input_state st = snap();
    CHECK(st.profile[0] == SOMVR_PROF_NONE && st.profile[1] == SOMVR_PROF_NONE && st.hand[1].active == 0 &&
              st.hand[1].buttons == 0 && st.hand[1].grip_flags == 0 && st.flags == FULL_FLAGS &&
              minfo()->prof_events == 2 && log_has("xr: input: profile L=none R=none"),
          "asleep: profile %u/%u active 0x%x buttons 0x%x grip 0x%x flags 0x%x events %d", st.profile[0], st.profile[1],
          st.hand[1].active, st.hand[1].buttons, st.hand[1].grip_flags, st.flags, minfo()->prof_events);
    CHECK(!memcmp(st.hand[1].grip.p, HAND_R.grip + 4, 12), "the last pose was not kept for an invalid one");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* a setup step fails: input off, everything made destroyed, the frames as without input */
static int sc_input_setup_fail(const char *logline) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    hand_set(SOMVR_HAND_R, &HAND_R);
    app_present(&a);
    CHECK(log_has(logline), "log lacks \"%s\"", logline);
    CHECK(minfo()->spaces_live == 0 && minfo()->sets_live == 0 && atomic_load(&g_in->xr.ready) == 0 &&
              calls("xrSyncActions") == 0,
          "input not off: action spaces %d sets %d ready %u syncs %d", minfo()->spaces_live, minfo()->sets_live,
          atomic_load(&g_in->xr.ready), calls("xrSyncActions"));
    somvr_haptic_req q = {.source = SOMVR_HAP_TEST, .amplitude = 0.5f, .duration_ns = 100000000};
    somvr_haptic_post(g_in, SOMVR_HAND_R, &q);
    for (uint64_t f = 1; f <= 3; f++) stereo_frame(&a, f, "frames with input off");
    CHECK(calls("xrApplyHapticFeedback") == 0 && calls("xrSyncActions") == 0 && snap().sync_seq == 0 &&
              atomic_load(&g_sh.n_stereo) == 3 && atomic_load(&g_sh.xr.state) == SOMVR_XRS_RUNNING,
          "with input off: haptics %d syncs %d stereo %llu", calls("xrApplyHapticFeedback"), calls("xrSyncActions"),
          (unsigned long long)atomic_load(&g_sh.n_stereo));
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive");
    check_mock_clean();
    return 0;
}
static int sc_input_attach_fail(void) {
    return sc_input_setup_fail("xr: input off: xrAttachSessionActionSets XR_ERROR_RUNTIME_FAILURE");
}
static int sc_input_action_fail(void) {
    int r = sc_input_setup_fail("xr: input off: xrCreateAction XR_ERROR_RUNTIME_FAILURE");
    CHECK(calls("xrSuggestInteractionProfileBindings") == 0 && calls("xrCreateActionSpace") == 0,
          "went on after the failed action");
    return r;
}
static int sc_input_space_fail(void) {
    int r = sc_input_setup_fail("xr: input off: xrCreateActionSpace XR_ERROR_RUNTIME_FAILURE");
    CHECK(calls("xrCreateActionSpace") == 3 && calls("xrDestroySpace") >= 2 && calls("xrAttachSessionActionSets") == 0,
          "spaces %d destroyed %d attach %d", calls("xrCreateActionSpace"), calls("xrDestroySpace"),
          calls("xrAttachSessionActionSets"));
    return r;
}

/* no input: not a single action call, the stereo path as before. mode 0: no object (a v2 preload); 1: want 0;
 * 2: inactive; 3: SOMVR_XR_INPUT=off; 4: a short object (another ABI) */
static int sc_input_none(int mode, const char *logline) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (mode && input_obj(mode == 1 ? 0 : INW_ALL, mode != 2, mode == 4 ? 256 : SOMVR_INPUT_V1_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    hand_set(SOMVR_HAND_R, &HAND_R);
    app_present(&a);
    if (mode) {
        somvr_haptic_req q = {.source = SOMVR_HAP_TEST, .amplitude = 0.5f, .duration_ns = 100000000};
        somvr_haptic_post(g_in, SOMVR_HAND_R, &q);
    }
    for (uint64_t f = 1; f <= 4; f++) stereo_frame(&a, f, "stereo without input");
    app_teardown(&a);
    CHECK(input_calls() == 0, "%d input calls (syncs %d, strings %d)", input_calls(), calls("xrSyncActions"),
          calls("xrStringToPath"));
    CHECK(!mode || (g_in->st.sync_seq == 0 && atomic_load(&g_in->seq) == 0 && atomic_load(&g_in->xr.ready) == 0),
          "the object was written");
    CHECK(log_has(logline), "log lacks \"%s\"", logline);
    CHECK(atomic_load(&g_sh.n_stereo) == 4, "stereo %llu", (unsigned long long)atomic_load(&g_sh.n_stereo));
    check_mock_clean();
    return 0;
}
static int sc_input_absent(void) { return sc_input_none(0, "no somvr_input to fill (no preload object): no actions"); }
static int sc_input_want0(void) { return sc_input_none(1, "no somvr_input to fill (want lacks STATE): no actions"); }
static int sc_input_inactive(void) {
    return sc_input_none(2, "no somvr_input to fill (inactive or another version): no actions");
}
static int sc_input_off(void) { return sc_input_none(3, "xr: input: SOMVR_XR_INPUT=off: no actions"); }
static int sc_input_short(void) {
    return sc_input_none(4, "no somvr_input to fill (inactive or another version): no actions");
}
static int sc_input_future(void) { /* an object of a later version: readers take v1's part */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    g_in->version = 2; /* (its size may grow too; somvr_input_ok takes >= v1's) */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    hand_set(SOMVR_HAND_R, &HAND_R);
    app_present(&a);
    CHECK(snap().flags == FULL_FLAGS && snap().hand[1].buttons == (SOMVR_BTN_LOWER | SOMVR_BTN_THUMB_TOUCH),
          "a v2 object was not used");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

static int sc_input_log(void) { /* R0: vkcube with SOMVR_XR=force SOMVR_XR_INPUT=log, no preload at all */
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "vkcube") == 0, "setup");
    for (int f = 0; f < 2; f++) app_present(&a);
    mockxr_hand r = HAND_R;
    r.buttons = SOMVR_BTN_LOWER;
    hand_set(1, &r);
    app_present(&a);
    CHECK(calls("xrAttachSessionActionSets") == 1 && calls("xrSyncActions") == calls("xrWaitFrame") &&
              calls("xrSyncActions") == 4,
          "log mode: attach %d syncs %d waits %d", calls("xrAttachSessionActionSets"), calls("xrSyncActions"),
          calls("xrWaitFrame"));
    CHECK(log_has("no somvr_input to fill (no preload object): log only") &&
              log_has("xr: input: 4 action spaces, attached (log only, no somvr_input)") &&
              log_has("xr: input: R btn 0x0->0x1 presses 1,0,0,0,0,0 trig 0.80"),
          "log mode lines");
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD, "mono frames changed");
    app_teardown(&a);
    CHECK(minfo()->spaces_live == 0 && minfo()->sets_live == 0, "left alive");
    check_mock_clean();
    return 0;
}

/* a game thread locating both grips through somvr_input the whole time while the device (and the session) go away */
static _Atomic int g_ht_stop, g_ht_ok, g_ht_calls;
static void *hand_thread(void *arg) {
    (void)arg;
    while (!atomic_load(&g_ht_stop)) {
        atomic_fetch_add(&g_ht_calls, 1);
        somvr_time tm;
        if (!somvr_xr_enter(&g_sh)) continue;
        if (atomic_load_explicit(&g_in->xr.ready, memory_order_acquire) && g_in->xr.locate_space &&
            somvr_time_read(&g_sh, &tm) && tm.t > 0) {
            int ok = 1;
            for (int h = 0; h < 2; h++) {
                XrSpaceLocation loc = {.type = XR_TYPE_SPACE_LOCATION};
                ok &= ((PFN_xrLocateSpace)g_in->xr.locate_space)((XrSpace)(uintptr_t)g_in->xr.grip_space[h],
                                                                 (XrSpace)(uintptr_t)g_sh.xr.space, tm.t,
                                                                 &loc) == XR_SUCCESS;
            }
            if (ok) atomic_fetch_add(&g_ht_ok, 1);
        }
        somvr_xr_leave(&g_sh);
    }
    return NULL;
}
static int sc_input_teardown(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V1_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    pthread_t t;
    pthread_create(&t, NULL, hand_thread, NULL);
    for (uint64_t f = 1; f <= 8; f++) stereo_frame(&a, f, "with a game thread locating the hands");
    usleep(5000);
    app_teardown(&a); /* xr.state = DEAD, wait for the users, ready = 0, the action spaces, the session */
    usleep(20000);
    atomic_store(&g_ht_stop, 1);
    pthread_join(t, NULL);
    CHECK(atomic_load(&g_ht_ok) > 0, "the game thread never located the hands (%d tries)", atomic_load(&g_ht_calls));
    CHECK(MOCK(mockxr_space_bad)() == 0, "%d xrLocateSpace calls with a dead space or session",
          MOCK(mockxr_space_bad)());
    CHECK(atomic_load(&g_in->xr.ready) == 0 && minfo()->spaces_live == 0, "ready %u spaces %d",
          atomic_load(&g_in->xr.ready), minfo()->spaces_live);
    check_mock_clean();
    fprintf(stderr, "    hand thread: %d tries, %d located, %d xrLocateSpace calls in all, none after the destroy\n",
            atomic_load(&g_ht_calls), atomic_load(&g_ht_ok), calls("xrLocateSpace"));
    return 0;
}

/* ---- the HUD quad (H11, docs/re-xr-pipeline.md 8): the preload's somvr_hud, a second layer ---------------------- */
static somvr_hud_shared *g_hud;
/* the preload's object: hud_obj.so next to the mock, in the global scope like LD_PRELOAD (absent in other scenarios) */
static int hud_obj(int active) {
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/hud_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (!h || !(g_hud = (somvr_hud_shared *)dlsym(h, SOMVR_HUD_SYMBOL)))
        return fprintf(stderr, "    hud_obj.so: %s\n", dlerror()), -1;
    g_hud->magic = SOMVR_HUD_MAGIC;
    g_hud->version = SOMVR_HUD_VERSION;
    g_hud->size = (uint32_t)sizeof *g_hud;
    g_hud->pid = (int32_t)getpid();
    if (active) atomic_store(&g_hud->active, 1);
    return 0;
}
static void hud_info(uint32_t gen, uint32_t w, uint32_t h, VkImage img) {
    somvr_hud_info in = {gen, w, h, 28, (uint64_t)(uintptr_t)img};
    somvr_hud_publish(g_hud, &in);
}
/* H9 with SOMVR_HUD=quad: somvr_hud's entry for the tag about to be pushed (gen 0: no HUD), then the core tag, present */
static void hud_present(app_t *a, uint64_t frame, uint32_t hud_gen, uint32_t kind) {
    if (g_hud) somvr_hud_tag_put(g_hud, atomic_load(&g_sh.tag_seq) + 1, hud_gen, kind);
    tagged_present_eyes(a, frame, 1, TAG_SYNC, 3, 2);
}
static const uint8_t HUD_PX[4] = {11, 22, 33, 44}; /* the HUD image's texels (RGBA, the alpha the UI surface's) */
typedef struct {
    app_t a;
    VkImage L, R, H;
} hudrig;
/* layout B, the session running (one mono present), the eye images (1832x1920) and the HUD image (1920x1080) made by
 * the game's device after their sizes were announced; obj: 0 none (a preload without H11), 1 inactive, 2 active */
static int hud_setup(hudrig *r, int obj) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (obj && hud_obj(obj == 2)) return 1;
    if (load_layer()) return 1;
    CHECK(app_setup(&r->a, "Feral3D") == 0, "setup");
    app_present(&r->a);
    atomic_store(&g_sh.eyes.expect_w, 1832);
    atomic_store(&g_sh.eyes.expect_h, 1920);
    if (g_hud) atomic_store(&g_hud->expect_w, 1920), atomic_store(&g_hud->expect_h, 1080);
    r->L = app_image(&r->a, 1832, 1920, FERAL_USAGE), r->R = app_image(&r->a, 1832, 1920, FERAL_USAGE);
    r->H = app_image(&r->a, 1920, 1080, FERAL_USAGE);
    eyes_info(2, 1832, 1920, r->L, r->R);
    int ix = img_ix(r->H);
    if (ix >= 0) memcpy(g_img[ix].fill, HUD_PX, 4);
    return 0;
}
/* one layout-B stereo frame from the eye images; the present's HUD entry names hud_gen (0: none) */
static void hud_stereo(hudrig *r, uint64_t f, uint32_t hud_gen, uint32_t kind, somvr_views *v) {
    CHECK(locate(v), "frame %llu: locate", (unsigned long long)f);
    write_record(f, SOMVR_LAYOUT_B, v, 3);
    V.ncp = V.ntr = 0;
    hud_present(&r->a, f, hud_gen, kind);
}
static const int32_t RECT_B[2][4] = {{0, 0, 1832, 1920}, {1832, 0, 1832, 1920}};
#define HUD_FLAGS_STRAIGHT (XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)
/* the world-locked pose expected from a record's eyes (the mock's views turn about +Y only): dist ahead of their
 * midpoint along the head's yaw, drop below, facing back - computed here with the yaw angle, independent of the layer */
static void hud_expect_world(const somvr_views *v, float dist, float drop, float out[7]) {
    float yaw = 2 * atan2f(v->pose[0].q[1], v->pose[0].q[3]);
    float m[3] = {0.5f * (v->pose[0].p[0] + v->pose[1].p[0]), 0.5f * (v->pose[0].p[1] + v->pose[1].p[1]),
                  0.5f * (v->pose[0].p[2] + v->pose[1].p[2])};
    float o[7] = {0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw), m[0] - dist * sinf(yaw), m[1] - drop, m[2] - dist * cosf(yaw)};
    memcpy(out, o, sizeof o);
}
static int pose_near(const float a[7], const float b[7]) {
    for (int i = 0; i < 7; i++)
        if (!(fabsf(a[i] - b[i]) < 2e-5f)) return 0;
    return 1;
}
/* a frame that ended with the projection (both eye images) and the HUD quad: the HUD image copied into the HUD
 * swapchain's image in the same submit, taken from and returned to TRANSFER_DST, the quad as configured */
static void check_hud_quad(const hudrig *r, const somvr_views *v, uint32_t hw, uint32_t hh, VkImage hud_img,
                           const float pose[7], int space, uint64_t flags, float width, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    check_proj_n(v, 3, RECT_B, what, 2);
    CHECK(V.ncp == 3 && V.cp[0].s == r->L && V.cp[1].s == r->R && V.cp[1].r.dstOffset.x == 1832 && V.cp[2].s == hud_img &&
              (uintptr_t)V.cp[2].d >= 0x5100 && (uintptr_t)V.cp[2].d < 0x5103 && V.cp[2].r.dstOffset.x == 0 &&
              V.cp[2].r.extent.width == hw && V.cp[2].r.extent.height == hh && V.cp[2].d != V.cp[0].d,
          "%s: %d copies, the third %p -> %p %ux%u", what, V.ncp, (void *)V.cp[2].s, (void *)V.cp[2].d,
          V.cp[2].r.extent.width, V.cp[2].r.extent.height);
    VkImage xr = V.cp[2].d;
    CHECK(barrier_seen(hud_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) &&
              barrier_seen(hud_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
              barrier_seen(xr, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
              barrier_seen(xr, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) &&
              V.ntr == 10,
          "%s: the HUD image's / its XR image's barriers (%d in the submit)", what, V.ntr);
    CHECK(!memcmp(img_px(xr, 0), HUD_PX, 4) && !memcmp(img_px(xr, (int32_t)hw - 1), HUD_PX, 4),
          "%s: the HUD XR image does not hold the HUD image", what);
    CHECK(e->released == 1 && e->released2 == 1 && V.present_nwait == 1 && V.present_wait[0] == V.submit_signal,
          "%s: released %d/%d, the present does not wait our one submit", what, e->released, e->released2);
    const mockxr_swapchain *s2 = MOCK(mockxr_swapchain2_get)();
    CHECK(MOCK(mockxr_sc2_live)() && s2->width == hw && s2->height == hh && s2->format == VK_FORMAT_R8G8B8A8_SRGB &&
              s2->usage == (XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT),
          "%s: HUD swapchain %ux%u format %lld usage 0x%x", what, s2->width, s2->height, (long long)s2->format, s2->usage);
    float hgt = width * (float)hh / (float)hw;
    CHECK(e->type2 == XR_TYPE_COMPOSITION_LAYER_QUAD && e->sc2 == 1 && e->space2 == space && e->flags2 == flags &&
              e->eyes2 == XR_EYE_VISIBILITY_BOTH && e->rect2[0] == 0 && e->rect2[1] == 0 && e->rect2[2] == (int32_t)hw &&
              e->rect2[3] == (int32_t)hh && e->size2[0] == width && fabsf(e->size2[1] - hgt) < 1e-6f,
          "%s: quad type %d swapchain %d space %d flags 0x%llx eyes %d rect %d,%d %dx%d size %.4f x %.4f", what, e->type2,
          e->sc2, e->space2, (unsigned long long)e->flags2, e->eyes2, e->rect2[0], e->rect2[1], e->rect2[2], e->rect2[3],
          e->size2[0], e->size2[1]);
    CHECK(pose_near(e->pose2, pose), "%s: quad pose %.4f,%.4f,%.4f,%.4f %.4f,%.4f,%.4f, want %.4f,%.4f,%.4f,%.4f "
          "%.4f,%.4f,%.4f", what, e->pose2[0], e->pose2[1], e->pose2[2], e->pose2[3], e->pose2[4], e->pose2[5],
          e->pose2[6], pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6]);
}
/* a stereo frame from the eye images and nothing else: one layer, no HUD copy */
static void check_no_quad(const somvr_views *v, const char *what) {
    check_proj_n(v, 3, RECT_B, what, 1);
    CHECK(V.ncp == 2 && MOCK(mockxr_last_end)()->type2 == 0 && MOCK(mockxr_last_end)()->released2 == 0,
          "%s: %d copies, a second layer of type %d", what, V.ncp, MOCK(mockxr_last_end)()->type2);
}

static int sc_hud(void) { /* world-locked (default): placed once, kept, placed again after a recentre (event at 10) */
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    somvr_views v, v3;
    hud_stereo(&r, 1, 0, SOMVR_HUDK_NONE, &v); /* the present names no HUD image */
    check_no_quad(&v, "no HUD this frame");
    CHECK(!MOCK(mockxr_sc2_live)() && log_has("without the HUD quad: the present names no HUD image"), "no HUD: a swapchain");
    hud_info(1, 1920, 1080, NULL); /* the VkImage is not known yet (Feral makes it at the first copy) */
    hud_stereo(&r, 2, 1, SOMVR_HUDK_WORLD, &v);
    check_no_quad(&v, "HUD image not known yet");
    CHECK(log_has("HUD vkimage 0 (1920x1080): not known yet"), "log lacks the not-known line");
    hud_info(2, 1920, 1080, r.H);
    float at[7];
    for (uint64_t f = 3; f <= 5; f++) {
        int sub = V.submits, acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage");
        hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
        if (f == 3) v3 = v, hud_expect_world(&v3, 1.5f, 0.1f, at); /* placed from frame 3's eyes */
        check_hud_quad(&r, &v, 1920, 1080, r.H, at, 1, HUD_FLAGS_STRAIGHT, 1.2f, "world HUD");
        CHECK(V.submits == sub + 1 && MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq + 2,
              "frame %llu: %d submits, %d acquires", (unsigned long long)f, V.submits - sub,
              MOCK(mockxr_calls)("xrAcquireSwapchainImage") - acq);
    }
    CHECK(fabsf(v.pose[0].q[1] - v3.pose[0].q[1]) > 1e-4f, "the mock's head did not turn between frames 3 and 5");
    CHECK(MOCK(mockxr_calls)("xrCreateSwapchain") == 3 && atomic_load(&g_hud->n_quads) == 3,
          "swapchains made %d (mono, eyes, HUD), quads %llu", MOCK(mockxr_calls)("xrCreateSwapchain"),
          (unsigned long long)atomic_load(&g_hud->n_quads));
    /* a reuse frame (its record is missing): the last projection and the last quad again, no copy, no acquire */
    int acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage");
    V.ncp = 0;
    hud_present(&r.a, 99, 2, SOMVR_HUDK_WORLD);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 2 && e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->type2 == XR_TYPE_COMPOSITION_LAYER_QUAD &&
              e->sc2 == 1 && pose_near(e->pose2, at) && e->released == 0 && e->released2 == 0 && V.ncp == 0 &&
              MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq,
          "reuse: %u layers, released %d/%d, %d copies", e->layer_count, e->released, e->released2, V.ncp);
    /* a menu frame: the mono quad alone; a mismatch after it has nothing to reuse: mono */
    tagged_present(&r.a, 0, 0, TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 1 && MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD &&
              MOCK(mockxr_last_end)()->type2 == 0,
          "menu frame: not the mono quad alone");
    hud_present(&r.a, 98, 2, SOMVR_HUDK_WORLD);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 1 && MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD,
          "a mismatch after a menu frame: not mono");
    /* stereo again (xrEndFrame 10: the recentre comes at the next poll): the quad where it was */
    hud_stereo(&r, 6, 2, SOMVR_HUDK_WORLD, &v);
    check_hud_quad(&r, &v, 1920, 1080, r.H, at, 1, HUD_FLAGS_STRAIGHT, 1.2f, "world HUD after a menu frame");
    /* the runtime recentred: placed again from this frame's eyes */
    hud_stereo(&r, 7, 2, SOMVR_HUDK_WORLD, &v);
    float at7[7];
    hud_expect_world(&v, 1.5f, 0.1f, at7);
    check_hud_quad(&r, &v, 1920, 1080, r.H, at7, 1, HUD_FLAGS_STRAIGHT, 1.2f, "world HUD after the recentre");
    CHECK(!pose_near(at7, at) && log_has("the HUD quad is placed again at the next frame") &&
              log_count("xr: hud quad placed for frame") == 2,
          "recentre: the quad was not placed again (%d placements)", log_count("xr: hud quad placed for frame"));
    /* the UI size changes (SetResolution): a new HUD image, a new HUD swapchain, the quad at the same place */
    atomic_store(&g_hud->expect_w, 1280);
    atomic_store(&g_hud->expect_h, 720);
    VkImage H2 = app_image(&r.a, 1280, 720, FERAL_USAGE);
    int ix = img_ix(H2);
    if (ix >= 0) memcpy(g_img[ix].fill, HUD_PX, 4);
    hud_info(3, 1280, 720, H2);
    hud_stereo(&r, 8, 3, SOMVR_HUDK_WORLD, &v);
    check_hud_quad(&r, &v, 1280, 720, H2, at7, 1, HUD_FLAGS_STRAIGHT, 1.2f, "world HUD, new UI size");
    /* made: mono, eyes, HUD, the menu frame's game size, eyes again, the new HUD; each remake destroyed the old one */
    CHECK(MOCK(mockxr_calls)("xrCreateSwapchain") == 6 && MOCK(mockxr_calls)("xrDestroySwapchain") == 4,
          "resize: swapchains made %d destroyed %d", MOCK(mockxr_calls)("xrCreateSwapchain"),
          MOCK(mockxr_calls)("xrDestroySwapchain"));
    /* a video frame's HUD (kind video) is shown the same way */
    hud_stereo(&r, 9, 3, SOMVR_HUDK_VIDEO, &v);
    check_hud_quad(&r, &v, 1280, 720, H2, at7, 1, HUD_FLAGS_STRAIGHT, 1.2f, "world HUD, video kind");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_sc2_live)(), "XR objects alive after vkDestroyDevice");
    CHECK(log_has("xr: hud: somvr_hud found") && log_has("world-locked, 1.20 m wide, 1.50 m ahead, 0.10 m below eye level") &&
              log_has("xr: hud swapchain 1920x1080 format 43 (3 images) for HUD image format 37: copy") &&
              log_has("xr: hud quad frame 3: HUD image") && log_has("a HUD image candidate"),
          "HUD log lines");
    check_mock_clean();
    return 0;
}

static int sc_hud_head(void) { /* SOMVR_XR_HUD=head: the quad in a VIEW space, 1.5 m ahead, 0.1 m below */
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    const float pose[7] = {0, 0, 0, 1, 0, -0.1f, -1.5f};
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) {
        hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
        check_hud_quad(&r, &v, 1920, 1080, r.H, pose, 2, HUD_FLAGS_STRAIGHT, 1.2f, "head-locked HUD");
    }
    CHECK(MOCK(mockxr_view_live)() == 1 && MOCK(mockxr_calls)("xrCreateReferenceSpace") == 2 &&
              MOCK(mockxr_space_type)() == XR_REFERENCE_SPACE_TYPE_LOCAL,
          "VIEW space: live %d, spaces made %d", MOCK(mockxr_view_live)(), MOCK(mockxr_calls)("xrCreateReferenceSpace"));
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "the VIEW space or a swapchain outlived the session");
    CHECK(log_has("head-locked") && log_has("xr: hud: VIEW space made"), "log lines");
    check_mock_clean();
    return 0;
}

static int sc_hud_premul(void) { /* SOMVR_XR_HUD_ALPHA=premul SOMVR_XR_HUD_QUAD=2.5,1.6,-0.2 */
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    hud_stereo(&r, 1, 2, SOMVR_HUDK_WORLD, &v);
    float at[7];
    hud_expect_world(&v, 2.5f, -0.2f, at);
    check_hud_quad(&r, &v, 1920, 1080, r.H, at, 1, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, 1.6f,
                   "premultiplied, 1.6 m wide 2.5 m ahead 0.2 m above");
    app_teardown(&r.a);
    CHECK(log_has("1.60 m wide, 2.50 m ahead, -0.20 m below eye level, alpha premultiplied (flags BLEND_TEXTURE_SOURCE_ALPHA)") &&
              !log_has("not understood"),
          "log lines");
    check_mock_clean();
    return 0;
}

static int sc_hud_badcfg(void) { /* SOMVR_XR_HUD=sideways SOMVR_XR_HUD_ALPHA=x SOMVR_XR_HUD_QUAD=1,2: the defaults */
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    hud_stereo(&r, 1, 2, SOMVR_HUDK_WORLD, &v);
    float at[7];
    hud_expect_world(&v, 1.5f, 0.1f, at);
    check_hud_quad(&r, &v, 1920, 1080, r.H, at, 1, HUD_FLAGS_STRAIGHT, 1.2f, "bad switches: the defaults");
    app_teardown(&r.a);
    CHECK(log_has("xr: hud: SOMVR_XR_HUD SOMVR_XR_HUD_ALPHA SOMVR_XR_HUD_QUAD not understood"), "log line");
    check_mock_clean();
    return 0;
}

/* no quad: the frames exactly as stereo-b's, one layer, no second swapchain (obj 0 absent, 1 inactive; SOMVR_XR_HUD=off
 * with an active one) */
static int hud_none(int obj, const char *logline) {
    hudrig r;
    if (hud_setup(&r, obj)) return 1;
    if (g_hud) hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) {
        hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
        check_eyes_frame(&v, r.L, r.R, 1832, 1920, "no HUD quad");
        check_no_quad(&v, "no HUD quad");
    }
    CHECK(MOCK(mockxr_calls)("xrCreateSwapchain") == 2 && !MOCK(mockxr_sc2_live)() && !log_has("a HUD image candidate"),
          "a HUD swapchain or candidate without a HUD (%d swapchains)", MOCK(mockxr_calls)("xrCreateSwapchain"));
    app_teardown(&r.a);
    CHECK(log_has(logline), "log lacks \"%s\"", logline);
    check_mock_clean();
    return 0;
}
static int sc_hud_absent(void) { return hud_none(0, "xr: hud: no HUD quad (no somvr_hud: a preload without H11)"); }
static int sc_hud_inactive(void) { return hud_none(1, "xr: hud: no HUD quad (somvr_hud inactive (SOMVR_HUD unset)"); }
static int sc_hud_off(void) { return hud_none(2, "xr: hud: no HUD quad (SOMVR_XR_HUD=off)"); }

/* a HUD failure in frame 1: the HUD off for the session (logged), that frame and the next ones stereo with the
 * projection alone, XR up. copied1: frame 1 had copied the HUD image already (the runtime rejected the quad at the end) */
static int hud_fail(const char *logline, int copied1) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) {
        hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
        if (f == 1 && copied1) { /* the eyes and the HUD copied, the frame ended again with the projection alone */
            const mockxr_end *e = MOCK(mockxr_last_end)();
            check_proj_n(&v, 3, RECT_B, "the rejected frame, ended again", 1);
            CHECK(V.ncp == 3 && e->type2 == 0 && e->released == 1 && e->released2 == 1, "the rejected frame: %d copies, "
                  "a second layer %d, released %d/%d", V.ncp, e->type2, e->released, e->released2);
            continue;
        }
        check_eyes_frame(&v, r.L, r.R, 1832, 1920, "after a HUD failure");
        check_no_quad(&v, "after a HUD failure");
    }
    CHECK(!MOCK(mockxr_sc2_live)() && atomic_load(&g_hud->n_quads) == 0 && log_count("xr: hud off for this session") == 1 &&
              !log_has("pass-through from now on"),
          "HUD failure: a HUD swapchain alive %d, quads %llu, off lines %d", MOCK(mockxr_sc2_live)(),
          (unsigned long long)atomic_load(&g_hud->n_quads), log_count("xr: hud off for this session"));
    app_teardown(&r.a);
    CHECK(log_has(logline), "log lacks \"%s\"", logline);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_hud_swapchain_fail(void) { return hud_fail("xr: hud off for this session: xrCreateSwapchain(1920x1080", 0); }
static int sc_hud_acquire_fail(void) { return hud_fail("xr: hud off for this session: xrAcquireSwapchainImage (HUD)", 0); }
static int sc_hud_reject(void) { /* the runtime rejects the frame with the quad: ended again without it, HUD off */
    int r = hud_fail("xr: hud off for this session: xrEndFrame rejected the frame with the HUD quad: XR_ERROR_LAYER_INVALID",
                     1);
    CHECK(MOCK(mockxr_calls)("xrEndFrame") == 1 + 4, "xrEndFrame calls %d (the mono present, frame 1 twice, frames 2-3)",
          MOCK(mockxr_calls)("xrEndFrame"));
    return r;
}

/* ---- the laser pointer (docs/re-xr-pipeline.md 10): somvr_input v2 with want POINTER, the ray and the dot --------- */
#define INW_PTR (INW_ALL | SOMVR_INW_POINTER)
/* hand h at its rest position (the mock's), its aim (yaw then pitch, no roll) toward p, the trigger at trig */
static void ptr_aim_at(mockxr_hand *m, int h, const float p[3], float trig) {
    float o[3] = {h ? 0.2f : -0.2f, -0.28f, -0.45f};
    float d[3] = {p[0] - o[0], p[1] - o[1], p[2] - o[2]};
    float yaw = atan2f(-d[0], -d[2]), pitch = atan2f(d[1], sqrtf(d[0] * d[0] + d[2] * d[2]));
    float cy = cosf(0.5f * yaw), sy = sinf(0.5f * yaw), cx = cosf(0.5f * pitch), sx = sinf(0.5f * pitch);
    float a[7] = {cy * sx, sy * cx, -sy * sx, cy * cx, o[0], o[1], o[2]}, g[7] = {0, 0, 0, 1, o[0], o[1] - 0.02f, o[2] + 0.05f};
    memset(m, 0, sizeof *m);
    memcpy(m->aim, a, sizeof a);
    memcpy(m->grip, g, sizeof g);
    m->grip_flags = m->aim_flags = 0xf, m->vel_flags = 3;
    m->trigger = trig;
}
/* ... toward the mono menu quad's point (u, v): 2 m ahead, 2 x 1.125 m (a 1920x1080 image) */
static void ptr_aim(mockxr_hand *m, int h, float u, float v, float trig) {
    float p[3] = {(u - 0.5f) * 2.0f, (0.5f - v) * 1.125f, -2.0f};
    ptr_aim_at(m, h, p, trig);
}
/* the hands take effect at the sync after the next present: two presents */
static void ptr_hands(app_t *a, const mockxr_hand *l, const mockxr_hand *r) {
    if (l) hand_set(0, l);
    if (r) hand_set(1, r);
    app_present(a), app_present(a);
}
static somvr_ptr_pub ptr_pub(void) {
    somvr_ptr_pub p;
    memset(&p, 0, sizeof p);
    CHECK(somvr_ptr_read(g_in, &p), "torn pointer block");
    return p;
}
static int rect_is(const int32_t r[4], int32_t x, int32_t y, int32_t w, int32_t h) {
    return r[0] == x && r[1] == y && r[2] == w && r[3] == h;
}
#define PTR_LAYER_FLAGS XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
/* the frame's layers after `first`: [the ray] and [the dot] of the pointer's swapchain; the dot at (u, v) of the menu */
static void check_ptr_layers(uint32_t first, int ray, int dot, float u, float v, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int psc = MOCK(mockxr_sc_find)(SOMVR_PTR_IMG_W, SOMVR_PTR_IMG_H);
    CHECK(e->layer_count == first + (uint32_t)ray + (uint32_t)dot, "%s: %u layers, want %u + %d + %d", what, e->layer_count,
          first, ray, dot);
    uint32_t i = first;
    if (ray && i < 4) {
        CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].sc == psc && psc >= 0 &&
                  rect_is(e->q[i].rect, 32, 0, 32, 32) && e->q[i].flags == PTR_LAYER_FLAGS && e->q[i].space == 1 &&
                  e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && fabsf(e->q[i].size[1] - 0.002f) < 1e-6f,
              "%s: the ray layer %u: type %d sc %d/%d rect %d,%d %dx%d flags 0x%llx size %g x %g", what, i, e->q[i].type,
              e->q[i].sc, psc, e->q[i].rect[0], e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3],
              (unsigned long long)e->q[i].flags, e->q[i].size[0], e->q[i].size[1]);
        i++;
    }
    if (dot && i < 4) {
        float x = (u - 0.5f) * 2.0f, y = (0.5f - v) * 1.125f;
        CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].sc == psc && rect_is(e->q[i].rect, 0, 0, 32, 32) &&
                  e->q[i].flags == PTR_LAYER_FLAGS && fabsf(e->q[i].pose[4] - x) < 2e-3f && fabsf(e->q[i].pose[5] - y) < 2e-3f &&
                  fabsf(e->q[i].pose[6] + 2.0f) < 1e-5f && e->q[i].pose[3] == 1.0f && e->q[i].size[0] == e->q[i].size[1] &&
                  e->q[i].size[0] > 0.012f && e->q[i].size[0] < 0.02f,
              "%s: the dot layer %u: sc %d rect %d,%d %dx%d at %.4f,%.4f,%.4f (want %.4f,%.4f,-2) size %g", what, i,
              e->q[i].sc, e->q[i].rect[0], e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3], e->q[i].pose[4],
              e->q[i].pose[5], e->q[i].pose[6], x, y, e->q[i].size[0]);
    }
}

static int sc_ptr(void) { /* layout B menus: the mono quad, the right hand on it, a click, off it, the left hand, none */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR, 1, SOMVR_INPUT_V2_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R, L;
    ptr_aim(&R, 1, 0.25f, 0.75f, 0);
    float away[3] = {-3, 0, -1};
    ptr_aim_at(&L, 0, away, 0);
    hand_set(1, &R), hand_set(0, &L);
    for (int f = 0; f < 3; f++) app_present(&a);
    somvr_ptr_pub p = ptr_pub();
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(log_has("xr: pointer on: the menu quad; drawn: the dot and the ray"), "log lacks the pointer-on line");
    CHECK(p.frame == (uint64_t)calls("xrEndFrame") && p.quad == SOMVR_PTRQ_MENU &&
              p.flags == (SOMVR_PTRF_ON | SOMVR_PTRF_AIM | SOMVR_PTRF_DRAWN) && p.hand == 1 && fabsf(p.u - 0.25f) < 1e-3f &&
              fabsf(p.v - 0.75f) < 1e-3f && p.consume == 2u && p.downs == 0 && p.ups == 0 && p.img_w == 1920 &&
              p.img_h == 1080 && p.quad_size[0] == 2.0f && fabsf(p.quad_size[1] - 1.125f) < 1e-6f &&
              p.quad_pose.p[2] == -2.0f && p.quad_pose.q[3] == 1.0f && p.time == MOCK(mockxr_time)() - 11111111 &&
              p.mono_ns > 0 && fabsf(p.dist - 1.7007f) < 2e-3f,
          "pointer block: frame %llu (ends %d) quad %u flags 0x%x hand %d uv %.4f,%.4f consume 0x%x clicks %u/%u img %ux%u "
          "size %g x %g dist %g", (unsigned long long)p.frame, calls("xrEndFrame"), p.quad, p.flags, p.hand, p.u, p.v,
          p.consume, p.downs, p.ups, p.img_w, p.img_h, p.quad_size[0], p.quad_size[1], p.dist);
    CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->rect[0][2] == 1920 && e->same_swapchain, "the menu quad first");
    check_ptr_layers(1, 1, 1, 0.25f, 0.75f, "on the menu");
    /* the ray: from 6 cm ahead of the right hand to the dot */
    float o[3] = {0.2f, -0.28f, -0.45f}, hit[3] = {-0.5f, -0.28125f, -2.0f}, dd[3], n = 0;
    for (int i = 0; i < 3; i++) dd[i] = hit[i] - o[i], n += dd[i] * dd[i];
    n = sqrtf(n);
    float mid[3], len = n - 0.06f;
    for (int i = 0; i < 3; i++) mid[i] = o[i] + (0.06f + 0.5f * len) * dd[i] / n;
    CHECK(fabsf(e->q[1].size[0] - len) < 2e-3f && fabsf(e->q[1].pose[4] - mid[0]) < 2e-3f &&
              fabsf(e->q[1].pose[5] - mid[1]) < 2e-3f && fabsf(e->q[1].pose[6] - mid[2]) < 2e-3f,
          "ray %g m at %.4f,%.4f,%.4f, want %g at %.4f,%.4f,%.4f", e->q[1].size[0], e->q[1].pose[4], e->q[1].pose[5],
          e->q[1].pose[6], len, mid[0], mid[1], mid[2]);
    float dot = 2.0f * p.dist * tanf(0.25f * 0.01745329252f);
    CHECK(fabsf(e->q[2].size[0] - dot) < 1e-5f, "dot size %g, want %g", e->q[2].size[0], dot);
    /* the pointer image: one static swapchain, acquired once, filled by one upload, the upload's objects freed */
    int psc = MOCK(mockxr_sc_find)(64, 32);
    const mockxr_swapchain *ps = MOCK(mockxr_swapchain_k)(psc);
    const uint8_t *c = &V.up_px[4 * (16 * 64 + 16)], *k = &V.up_px[0];
    CHECK(psc == 1 && ps->format == VK_FORMAT_R8G8B8A8_SRGB && ps->create_flags == XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT &&
              ps->acquires == 1 && ps->usage == (XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT),
          "pointer swapchain slot %d format %lld flags 0x%x acquires %d usage 0x%x", psc, (long long)ps->format,
          ps->create_flags, ps->acquires, ps->usage);
    CHECK(V.buf2img == 1 && V.up_img == (VkImage)(uintptr_t)(0x5000 + 0x100 * psc) && V.up_w == 64 && V.up_h == 32 &&
              c[0] == 255 && c[1] == 255 && c[2] == 255 && c[3] == 255 && k[3] == 0 && V.up_px[4 * (16 * 64 + 63) + 3] > 150,
          "upload: %d copies into %p %ux%u, centre %u,%u,%u,%u corner a %u", V.buf2img, (void *)V.up_img, V.up_w, V.up_h,
          c[0], c[1], c[2], c[3], k[3]);
    CHECK(V.buffers_live == 0 && V.mems_live == 0 && log_has("xr: pointer: image 64x32 format 43 (a static swapchain, 1 "
                                                             "image(s)) filled once"),
          "upload objects left: %d buffers, %d memories", V.buffers_live, V.mems_live);
    CHECK(atomic_load(&g_in->n_ptr_frames) == 3 && atomic_load(&g_in->n_ptr_on) == 3 && atomic_load(&g_in->n_ptr_drawn) == 3 &&
              MOCK(mockxr_locates)() == 3,
          "counters frames %llu on %llu drawn %llu, head locates %d", (unsigned long long)atomic_load(&g_in->n_ptr_frames),
          (unsigned long long)atomic_load(&g_in->n_ptr_on), (unsigned long long)atomic_load(&g_in->n_ptr_drawn),
          MOCK(mockxr_locates)());
    /* a click: the right trigger, then its release */
    R.trigger = 0.8f, ptr_hands(&a, NULL, &R);
    p = ptr_pub();
    CHECK(p.downs == 1 && p.ups == 0 && (p.flags & SOMVR_PTRF_DOWN) && log_has("edges 0x4 "), "press: clicks %u/%u flags 0x%x",
          p.downs, p.ups, p.flags);
    R.trigger = 0, ptr_hands(&a, NULL, &R);
    p = ptr_pub();
    CHECK(p.downs == 1 && p.ups == 1 && !(p.flags & SOMVR_PTRF_DOWN), "release: clicks %u/%u", p.downs, p.ups);
    /* off the quad: the ray alone (1 m), nothing consumed */
    ptr_aim(&R, 1, 1.6f, 0.5f, 0), ptr_hands(&a, NULL, &R);
    p = ptr_pub();
    e = MOCK(mockxr_last_end)();
    CHECK(p.flags == (SOMVR_PTRF_AIM | SOMVR_PTRF_DRAWN) && p.consume == 0 && p.quad == SOMVR_PTRQ_MENU,
          "off the quad: flags 0x%x consume 0x%x", p.flags, p.consume);
    check_ptr_layers(1, 1, 0, 0, 0, "off the quad");
    CHECK(fabsf(e->q[1].size[0] - 0.94f) < 1e-4f, "the ray off the quad: %g m", e->q[1].size[0]);
    /* the left hand pulls its trigger on the quad: it points now, and clicks */
    ptr_aim(&L, 0, 0.5f, 0.5f, 0.9f), ptr_hands(&a, &L, NULL);
    p = ptr_pub();
    CHECK(p.hand == 0 && (p.flags & SOMVR_PTRF_ON) && (p.flags & SOMVR_PTRF_DOWN) && p.downs == 2 && (p.consume & 1u) &&
              fabsf(p.u - 0.5f) < 2e-3f && fabsf(p.v - 0.5f) < 2e-3f,
          "left hand: hand %d flags 0x%x clicks %u uv %.4f,%.4f", p.hand, p.flags, p.downs, p.u, p.v);
    check_ptr_layers(1, 1, 1, 0.5f, 0.5f, "the left hand");
    L.trigger = 0, ptr_hands(&a, &L, NULL);
    CHECK(ptr_pub().ups == 2, "left release");
    /* no pose on either hand: nothing to draw, nothing on */
    R.aim_flags = R.grip_flags = 0, L.aim_flags = L.grip_flags = 0;
    ptr_hands(&a, &L, &R);
    p = ptr_pub();
    CHECK(p.flags == 0 && p.consume == 0 && MOCK(mockxr_last_end)()->layer_count == 1 && p.quad == SOMVR_PTRQ_MENU,
          "no hands: flags 0x%x, %u layers", p.flags, MOCK(mockxr_last_end)()->layer_count);
    CHECK(ps->acquires == 1 && V.buf2img == 1, "the image was filled again (%d acquires, %d uploads)", ps->acquires, V.buf2img);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && MOCK(mockxr_sc_find)(64, 32) < 0, "XR objects alive after vkDestroyDevice");
    CHECK(V.sems_live == 0 && V.fences_live == 0 && V.buffers_live == 0 && V.mems_live == 0, "layer Vulkan objects leaked");
    check_mock_clean();
    return 0;
}

static int sc_ptr_dot(void) { /* SOMVR_XR_POINTER=dot: the dot alone, no head locate */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR, 1, SOMVR_INPUT_V2_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R;
    ptr_aim(&R, 1, 0.75f, 0.25f, 0);
    hand_set(1, &R);
    for (int f = 0; f < 3; f++) app_present(&a);
    check_ptr_layers(1, 0, 1, 0.75f, 0.25f, "dot only");
    CHECK(MOCK(mockxr_locates)() == 0 && log_has("drawn: the dot;"), "dot only: %d head locates", MOCK(mockxr_locates)());
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* the pointer is not on: the frames as without it, the block never written */
static int ptr_none(uint32_t want, uint32_t size, const char *logline) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(want, 1, size)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R;
    ptr_aim(&R, 1, 0.5f, 0.5f, 0.9f);
    hand_set(1, &R);
    for (int f = 0; f < 3; f++) app_present(&a);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 1 && MOCK(mockxr_sc_find)(64, 32) < 0 && V.buf2img == 0,
          "no pointer: %u layers", MOCK(mockxr_last_end)()->layer_count);
    CHECK(atomic_load(&g_in->ptr_seq) == 0 && atomic_load(&g_in->n_ptr_frames) == 0, "the pointer block was written");
    CHECK(logline ? log_has(logline) : !log_has("xr: pointer"), "log: %s", logline ? logline : "a pointer line");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_ptr_unwanted(void) { return ptr_none(INW_ALL, SOMVR_INPUT_V2_SIZE, NULL); }
static int sc_ptr_v1(void) {
    return ptr_none(INW_PTR, SOMVR_INPUT_V1_SIZE,
                    "xr: pointer: the preload wants it, but somvr_input has no pointer block (v1-sized): no pointer");
}
static int sc_ptr_off(void) {
    return ptr_none(INW_PTR, SOMVR_INPUT_V2_SIZE, "xr: pointer: the preload wants it, but SOMVR_XR_POINTER=off: no pointer");
}

/* a pointer failure: no drawing for the session (logged once), the frames go on, the pointer is still published */
static int ptr_fail(const char *logline, int ends_extra) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR, 1, SOMVR_INPUT_V2_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R;
    ptr_aim(&R, 1, 0.5f, 0.5f, 0);
    hand_set(1, &R);
    for (int f = 0; f < 4; f++) app_present(&a);
    somvr_ptr_pub p = ptr_pub();
    CHECK(MOCK(mockxr_last_end)()->layer_count == 1 && MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD &&
              p.flags == (SOMVR_PTRF_ON | SOMVR_PTRF_AIM) && atomic_load(&g_in->n_ptr_frames) == 4,
          "after the failure: %u layers, flags 0x%x, %llu frames", MOCK(mockxr_last_end)()->layer_count, p.flags,
          (unsigned long long)atomic_load(&g_in->n_ptr_frames));
    CHECK(log_has(logline) && log_count("xr: pointer: drawing off for this session") == 1 && !log_has("pass-through from now on"),
          "log lacks \"%s\" (once)", logline);
    CHECK(calls("xrEndFrame") == 4 + ends_extra, "xrEndFrame calls %d", calls("xrEndFrame"));
    CHECK(MOCK(mockxr_sc_find)(64, 32) < 0, "the pointer's swapchain is alive");
    R.trigger = 0.9f, ptr_hands(&a, NULL, &R);
    CHECK(ptr_pub().downs == 1, "no click without the drawing");
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && V.buffers_live == 0 && V.mems_live == 0 && V.fences_live == 0, "objects left");
    check_mock_clean();
    return 0;
}
static int sc_ptr_acquire_fail(void) {
    return ptr_fail("xr: pointer: drawing off for this session: xrAcquireSwapchainImage (pointer): XR_ERROR_RUNTIME_FAILURE", 0);
}
static int sc_ptr_reject(void) {
    return ptr_fail("drawing off for this session: xrEndFrame rejected the frame with the pointer's 2 layer(s): "
                    "XR_ERROR_LAYER_INVALID; ended again without them", 1);
}
static int sc_ptr_nostatic(void) { /* the runtime refuses a static swapchain: an ordinary one, filled once all the same */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR, 1, SOMVR_INPUT_V2_SIZE)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R;
    ptr_aim(&R, 1, 0.5f, 0.5f, 0);
    hand_set(1, &R);
    for (int f = 0; f < 4; f++) app_present(&a);
    check_ptr_layers(1, 1, 1, 0.5f, 0.5f, "an ordinary swapchain");
    const mockxr_swapchain *ps = MOCK(mockxr_swapchain_k)(MOCK(mockxr_sc_find)(64, 32));
    CHECK(ps->create_flags == 0 && ps->acquires == 1 && V.buf2img == 1 &&
              log_has("format 43 (an ordinary swapchain, 3 image(s)) filled once"),
          "ordinary: flags 0x%x acquires %d uploads %d", ps->create_flags, ps->acquires, V.buf2img);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* layout B stereo with the world-locked HUD quad: the pointer on it with want PTR_HUD, else no target */
static int ptr_hud(int on_hud) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR | (on_hud ? SOMVR_INW_PTR_HUD : 0u), 1, SOMVR_INPUT_V2_SIZE)) return 1;
    mockxr_hand R;
    ptr_aim(&R, 1, 0.5f, 0.5f, 0);
    hand_set(1, &R);
    if (hud_setup(&r, 2)) return 1; /* its one mono present: the pointer on the menu quad */
    CHECK(ptr_pub().quad == SOMVR_PTRQ_MENU && MOCK(mockxr_last_end)()->layer_count == 3, "the mono present: quad %u, %u layers",
          ptr_pub().quad, MOCK(mockxr_last_end)()->layer_count);
    CHECK(log_has(on_hud ? "xr: pointer on: the menu quad and the world-locked HUD quad" : "xr: pointer on: the menu quad;"),
          "pointer-on line");
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    float at[7];
    hud_stereo(&r, 1, 2, SOMVR_HUDK_WORLD, &v);
    hud_expect_world(&v, 1.5f, 0.1f, at);
    float centre[3] = {at[4], at[5], at[6]};
    ptr_aim_at(&R, 1, centre, 0);
    hand_set(1, &R);
    for (uint64_t f = 2; f <= 4; f++) hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
    somvr_ptr_pub p = ptr_pub();
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int hsc = MOCK(mockxr_sc_find)(1920, 1080);
    if (on_hud) {
        CHECK(p.quad == SOMVR_PTRQ_HUD && (p.flags & SOMVR_PTRF_ON) && (p.flags & SOMVR_PTRF_DRAWN) && fabsf(p.u - 0.5f) < 2e-3f &&
                  fabsf(p.v - 0.5f) < 2e-3f && p.img_w == 1920 && p.img_h == 1080 && p.quad_size[0] == 1.2f &&
                  fabsf(p.quad_pose.p[2] - at[6]) < 1e-5f,
              "on the HUD: quad %u flags 0x%x uv %.4f,%.4f img %ux%u size %g", p.quad, p.flags, p.u, p.v, p.img_w, p.img_h,
              p.quad_size[0]);
        int psc = MOCK(mockxr_sc_find)(64, 32);
        CHECK(e->layer_count == 4 && e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->q[1].sc == hsc && hsc >= 0 &&
                  e->q[2].sc == psc && rect_is(e->q[2].rect, 32, 0, 32, 32) && e->q[3].sc == psc &&
                  rect_is(e->q[3].rect, 0, 0, 32, 32) && fabsf(e->q[3].pose[4] - at[4]) < 2e-3f &&
                  fabsf(e->q[3].pose[6] - at[6]) < 2e-3f && fabsf(e->q[3].pose[1] - at[1]) < 1e-5f,
              "HUD frame layers %u: HUD sc %d/%d, ray sc %d, dot sc %d at %.3f,%.3f,%.3f", e->layer_count, e->q[1].sc, hsc,
              e->q[2].sc, e->q[3].sc, e->q[3].pose[4], e->q[3].pose[5], e->q[3].pose[6]);
    } else {
        CHECK(p.quad == SOMVR_PTRQ_NONE && p.flags == SOMVR_PTRF_AIM && e->layer_count == 2 && e->q[1].sc == hsc,
              "no HUD pointer: quad %u flags 0x%x, %u layers", p.quad, p.flags, e->layer_count);
    }
    hud_stereo(&r, 5, 0, SOMVR_HUDK_NONE, &v); /* a stereo frame without the HUD: no target */
    p = ptr_pub();
    CHECK(p.quad == SOMVR_PTRQ_NONE && !(p.flags & SOMVR_PTRF_ON) && MOCK(mockxr_last_end)()->layer_count == 1,
          "a stereo frame without the HUD: quad %u flags 0x%x, %u layers", p.quad, p.flags,
          MOCK(mockxr_last_end)()->layer_count);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_ptr_hud(void) { return ptr_hud(1); }
static int sc_ptr_nohud(void) { return ptr_hud(0); }

/* ---- SOMVR_EYE_SIZE's runtime switch (docs/re-xr-pipeline.md 9): stereo at the eye size, menus at the game's ------ */
/* the game's own surfaces at one of its mode sets: n images at the 3D size (rt3D's colour and G-buffer targets) and n
 * at the UI size, the previous mode set's destroyed first. With the eye and HUD sizes announced long before, every one
 * of them is an image candidate (the layer cannot tell them from the preload's textures by size) */
#define ES_EW 200
#define ES_EH 220
static VkImage g_ms_img[64];
static int g_ms_n, g_ms_made;
static void es_mode_set(app_t *a, uint32_t w3, uint32_t h3, int n) {
    for (int i = 0; i < g_ms_n; i++) ((PFN_vkDestroyImage)L_gdpa(a->dev, "vkDestroyImage"))(a->dev, g_ms_img[i], NULL);
    g_ms_n = 0;
    for (int i = 0; i < n && g_ms_n + 2 <= 64; i++, g_ms_made += 2) {
        g_ms_img[g_ms_n++] = app_image(a, w3, h3, FERAL_USAGE);
        g_ms_img[g_ms_n++] = app_image(a, 1920, 1080, FERAL_USAGE);
    }
}
static int ppm_size(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    int ok = f && fscanf(f, "P6 %d %d 255", w, h) == 2;
    if (f) fclose(f);
    return ok;
}
static int sc_eyesize_switch(void) { /* SOMVR_XR_DUMP=s2,s3,s4: each new swapchain's first frame */
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (hud_obj(1) || load_layer()) return 1;
    CHECK(app_setup(&r.a, "Feral3D") == 0, "setup");
    app_present(&r.a); /* the session starts: the front end, mono, swapchain 1 = 2 x the game image */
    CHECK(MOCK(mockxr_swapchain_get)()->width == 3840 && MOCK(mockxr_calls)("xrCreateSwapchain") == 1, "mono swapchain");
    char ppm[3][700];
    for (int k = 0; k < 3; k++) snprintf(ppm[k], sizeof ppm[k], "%s.swapchain%d.ppm", g_log, k + 2), unlink(ppm[k]);
    /* the preload made its eye textures at the first stereo frame (the eye size announced first) and its HUD texture;
     * they stay for the whole run: the switch back to the game's size never touches them */
    atomic_store(&g_sh.eyes.expect_w, ES_EW);
    atomic_store(&g_sh.eyes.expect_h, ES_EH);
    atomic_store(&g_hud->expect_w, 1920), atomic_store(&g_hud->expect_h, 1080);
    es_mode_set(&r.a, ES_EW, ES_EH, 6); /* the first switch to the eye size: the game's surfaces come first */
    r.L = app_image(&r.a, ES_EW, ES_EH, FERAL_USAGE), r.R = app_image(&r.a, ES_EW, ES_EH, FERAL_USAGE);
    r.H = app_image(&r.a, 1920, 1080, FERAL_USAGE);
    int ix = img_ix(r.H);
    if (ix >= 0) memcpy(g_img[ix].fill, HUD_PX, 4);
    eyes_info(2, ES_EW, ES_EH, r.L, r.R);
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    uint64_t f = 1;
    int sc0 = MOCK(mockxr_calls)("xrCreateSwapchain");
    for (int cycle = 0; cycle < 3; cycle++) {
        if (cycle) es_mode_set(&r.a, ES_EW, ES_EH, 12); /* the switch to the eye size */
        for (int i = 0; i < 3; i++, f++) {
            hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
            const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
            const mockxr_end *e = MOCK(mockxr_last_end)();
            CHECK(s->width == 2 * ES_EW && s->height == ES_EH && V.ncp == 3 && V.cp[0].s == r.L && V.cp[1].s == r.R &&
                      V.cp[1].r.dstOffset.x == ES_EW && V.cp[2].s == r.H && e->layer_count == 2 &&
                      e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->type2 == XR_TYPE_COMPOSITION_LAYER_QUAD,
                  "cycle %d stereo frame %llu: swapchain %ux%u, %d copies (%p %p %p), %u layers", cycle,
                  (unsigned long long)f, s->width, s->height, V.ncp, (void *)V.cp[0].s, (void *)V.cp[1].s,
                  (void *)V.cp[2].s, e->layer_count);
        }
        es_mode_set(&r.a, 1920, 1080, 12); /* the hold ran out: the game's size, 3D = UI (one surface, HUD-sized) */
        for (int i = 0; i < 3; i++) {
            somvr_hud_tag_put(g_hud, atomic_load(&g_sh.tag_seq) + 1, 0, SOMVR_HUDK_NONE);
            tagged_present(&r.a, 0, 0, TAG_SYNC);
            const mockxr_end *e = MOCK(mockxr_last_end)();
            CHECK(MOCK(mockxr_swapchain_get)()->width == 3840 && MOCK(mockxr_swapchain_get)()->height == 1080 &&
                      e->layer_count == 1 && e->type == XR_TYPE_COMPOSITION_LAYER_QUAD,
                  "cycle %d menu frame %d: swapchain %ux%u, %u layers type %d", cycle, i,
                  MOCK(mockxr_swapchain_get)()->width, MOCK(mockxr_swapchain_get)()->height, e->layer_count, e->type);
        }
    }
    /* per cycle a projection swapchain at the eye size and one at the game's size again (the old one destroyed first),
     * the HUD's once; the candidates far more than the ring holds, the preload's three images kept */
    CHECK(MOCK(mockxr_calls)("xrCreateSwapchain") == sc0 + 7 && MOCK(mockxr_calls)("xrDestroySwapchain") == 6,
          "swapchains made %d destroyed %d (want %d, 6)", MOCK(mockxr_calls)("xrCreateSwapchain"),
          MOCK(mockxr_calls)("xrDestroySwapchain"), sc0 + 7);
    CHECK(g_ms_made >= 120 && !log_has("not created by this device") && !log_has("image candidates full") &&
              !log_has("without the HUD quad: HUD vkimage"),
          "%d game images: the eye or HUD images were lost among them", g_ms_made);
    CHECK(log_has("for eye image format 37: copy (swapchain 2, replacing 3840x1080)") &&
              log_has("for game format 44: copy (swapchain 3, replacing 400x220)") &&
              log_has("for eye image format 37: copy (swapchain 6, replacing 3840x1080)") &&
              log_has("for game format 44: copy (swapchain 7, replacing 400x220)"),
          "log lacks the swapchain lines with their number and the size they replace");
    /* the dumps: each new swapchain's first frame, at its own size */
    static const int want[3][2] = {{2 * ES_EW, ES_EH}, {3840, 1080}, {2 * ES_EW, ES_EH}};
    for (int k = 0; k < 3; k++) {
        int w = 0, h = 0;
        CHECK(ppm_size(ppm[k], &w, &h) && w == want[k][0] && h == want[k][1], "%s: %dx%d, want %dx%d", ppm[k], w, h,
              want[k][0], want[k][1]);
    }
    CHECK(log_has("xr: dump swapchain frame 2 -> ") && log_has("(400x220 format 43, sRGB bytes as is; its first frame: "
                                                                  "stereo)") &&
              log_has("xr: dump swapchain frame 3 -> ") && log_has("sRGB bytes as is; its first frame: mono)") &&
              !log_has("dump off"),
          "log lacks the swapchain dump lines");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* ---- the comfort fade (docs/re-xr-pipeline.md 6.8, ../somvr_fade.h): a record's fade level (SOMVR_FALLBACK) -> a
 * black quad, the frame's last layer -------------------------------------------------------------------------------- */
#include "../somvr_fade.h"
/* the record of frame `frame` with its fade level (flags bits 24..31), as somvr.so's fb_frame leaves it */
static void write_record_fade(uint64_t frame, uint32_t layout, const somvr_views *v, uint32_t level) {
    write_record(frame, layout, v, 3);
    somvr_rec *r = somvr_rec_slot(&g_sh, frame);
    somvr_seq_wbegin(&r->seq);
    r->flags = (r->flags & ~SOMVR_REC_FADE_MASK) | level << SOMVR_REC_FADE_SHIFT;
    somvr_seq_wend(&r->seq);
}
/* layer i (the last) of the last frame is ramp level k's quad (in the black rows 1, or the red rows 5), 0.25 m ahead of
 * v's eyes along the head's forward */
static void check_fade_layer_row(uint32_t i, int k, int row, const somvr_views *v, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int fsc = MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H);
    CHECK(e->layer_count == i + 1 && i < 16, "%s: %u layers, the fade quad as layer %u", what, e->layer_count, i);
    if (i >= 16) return;
    float m[3], f[3], d[3], q[4];
    memcpy(q, v->pose[0].q, sizeof q);
    f[0] = -2 * (q[0] * q[2] + q[3] * q[1]), f[1] = -2 * (q[1] * q[2] - q[3] * q[0]), f[2] = -(1 - 2 * (q[0] * q[0] + q[1] * q[1]));
    for (int c = 0; c < 3; c++) m[c] = 0.5f * (v->pose[0].p[c] + v->pose[1].p[c]), d[c] = e->q[i].pose[4 + c] - m[c];
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), along = (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / dist;
    CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && fsc >= 0 && e->q[i].sc == fsc &&
              rect_is(e->q[i].rect, k * SOMVR_FADE_TEX + 1, row, 2, 2) &&
              e->q[i].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT && e->q[i].space == 1 &&
              e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && e->q[i].size[0] == SOMVR_FADE_SIZE_M &&
              e->q[i].size[1] == SOMVR_FADE_SIZE_M,
          "%s: the fade layer %u: type %d sc %d/%d rect %d,%d %dx%d flags 0x%llx space %d size %g x %g", what, i,
          e->q[i].type, e->q[i].sc, fsc, e->q[i].rect[0], e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3],
          (unsigned long long)e->q[i].flags, e->q[i].space, e->q[i].size[0], e->q[i].size[1]);
    CHECK(fabsf(dist - SOMVR_FADE_DIST_M) < 1e-5f && along > 0.99999f && fabsf(e->q[i].pose[0] - q[0]) < 1e-6f &&
              fabsf(e->q[i].pose[1] - q[1]) < 1e-6f && fabsf(e->q[i].pose[2] - q[2]) < 1e-6f &&
              fabsf(e->q[i].pose[3] - q[3]) < 1e-6f,
          "%s: the fade quad %.4f m from the eyes (cos %.6f to the head's forward), turned %.4f,%.4f,%.4f,%.4f", what, dist,
          along, e->q[i].pose[0], e->q[i].pose[1], e->q[i].pose[2], e->q[i].pose[3]);
}
static void check_fade_layer(uint32_t i, int k, const somvr_views *v, const char *what) {
    check_fade_layer_row(i, k, 1, v, what);
}
static int sc_fade(void) { /* layout A: levels 0, 128, 1, 200, 255; a reuse repeats it; red; level 0 and mono: none */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a); /* the session; untagged: mono */
    somvr_views v;
    int up0 = V.buf2img;
    CHECK(locate(&v), "locate");
    write_record_fade(1, SOMVR_LAYOUT_A, &v, 0);
    tagged_present(&a, 1, 0, TAG_SYNC);
    check_proj(&v, 3, RECT_A, "level 0");
    CHECK(MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H) < 0 && V.buf2img == up0, "level 0 made the fade image");
    static const uint32_t lv[4] = {128, 1, 200, 255};
    static const int ramp[4] = {32, 1, 49, 63};
    for (int j = 0; j < 4; j++) {
        char what[64];
        snprintf(what, sizeof what, "level %u", lv[j]);
        CHECK(locate(&v), "locate");
        write_record_fade(2 + (uint64_t)j, SOMVR_LAYOUT_A, &v, lv[j]);
        tagged_present(&a, 2 + (uint64_t)j, 0, TAG_SYNC);
        check_proj_n(&v, 3, RECT_A, what, 2);
        check_fade_layer(1, ramp[j], &v, what);
        CHECK(somvr_fade_index(lv[j]) == ramp[j], "%s: ramp %d", what, somvr_fade_index(lv[j]));
    }
    /* the image: uploaded once into the fade swapchain's one (static) image, 64 premultiplied black levels */
    int fsc = MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H), ramp_ok = 1;
    for (int x = 0; x < SOMVR_FADE_W; x++) {
        const uint8_t *p = V.up_px + 4 * x, *p3 = V.up_px + 4 * (3 * SOMVR_FADE_W + x);
        ramp_ok &= !p[0] && !p[1] && !p[2] && p[3] == somvr_fade_alpha(x / SOMVR_FADE_TEX) && !memcmp(p, p3, 4);
        ramp_ok &= x == 0 || p[3] >= p[-1];
    }
    CHECK(V.buf2img == up0 + 1 && fsc >= 0 && V.up_img == (VkImage)(uintptr_t)(0x5000 + 0x100 * fsc) &&
              V.up_w == SOMVR_FADE_W && V.up_h == SOMVR_FADE_H && ramp_ok && somvr_fade_alpha(0) == 0 &&
              somvr_fade_alpha(SOMVR_FADE_LEVELS - 1) == 255 && MOCK(mockxr_swapchain_k)(fsc)->acquires == 1 &&
              (MOCK(mockxr_swapchain_k)(fsc)->create_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT),
          "the fade image: uploads %d, swapchain slot %d, image %p %ux%u, ramp %d, acquires %d", V.buf2img - up0, fsc,
          (void *)V.up_img, V.up_w, V.up_h, ramp_ok, fsc >= 0 ? MOCK(mockxr_swapchain_k)(fsc)->acquires : -1);
    /* the red rows (format 37 = R8G8B8A8: red first): (alpha x SOMVR_FADE_RED, 0, 0, alpha) */
    int red_ok = 1;
    for (int y = 4; y < SOMVR_FADE_H; y++)
        for (int x = 0; x < SOMVR_FADE_W; x++) {
            const uint8_t *p = V.up_px + 4 * (y * SOMVR_FADE_W + x);
            uint8_t al = somvr_fade_alpha(x / SOMVR_FADE_TEX);
            red_ok &= p[0] == (uint8_t)(al * SOMVR_FADE_RED + 0.5f) && !p[1] && !p[2] && p[3] == al;
        }
    CHECK(red_ok && SOMVR_FADE_H == 8, "the fade image's red rows");
    /* a present whose record is missing: the last stereo frame again, with its fade */
    somvr_views vl = v;
    tagged_present(&a, 99, 0, TAG_SYNC);
    check_proj_n(&vl, 3, RECT_A, "reuse", 2);
    check_fade_layer(1, 63, &vl, "reuse");
    /* SOMVR_REC_FADE_RED (SOMVR_BODYMOMENTS' knockdown dim): the same level from the red rows; a reuse keeps it */
    CHECK(locate(&v), "locate");
    write_record_fade(8, SOMVR_LAYOUT_A, &v, 128);
    somvr_rec *rr = somvr_rec_slot(&g_sh, 8);
    somvr_seq_wbegin(&rr->seq);
    rr->flags |= SOMVR_REC_FADE_RED;
    somvr_seq_wend(&rr->seq);
    tagged_present(&a, 8, 0, TAG_SYNC);
    check_proj_n(&v, 3, RECT_A, "red", 2);
    check_fade_layer_row(1, 32, 5, &v, "red");
    vl = v;
    tagged_present(&a, 98, 0, TAG_SYNC);
    check_fade_layer_row(1, 32, 5, &vl, "red reuse");
    CHECK(locate(&v), "locate");
    write_record_fade(9, SOMVR_LAYOUT_A, &v, 128);
    tagged_present(&a, 9, 0, TAG_SYNC);
    check_fade_layer_row(1, 32, 1, &v, "black again");
    /* level 0 again: the projection alone; a mono present: no fade */
    CHECK(locate(&v), "locate");
    write_record_fade(6, SOMVR_LAYOUT_A, &v, 0);
    tagged_present(&a, 6, 0, TAG_SYNC);
    check_proj(&v, 3, RECT_A, "level 0 again");
    write_record_fade(7, SOMVR_LAYOUT_A, &v, 255);
    tagged_present(&a, 0, 0, TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && MOCK(mockxr_last_end)()->layer_count == 1,
          "mono: %u layers", MOCK(mockxr_last_end)()->layer_count);
    app_teardown(&a);
    CHECK(log_has("xr: fade: on: a record with a fade level gets a black quad") &&
              log_has("xr: fade: image 256x8 format 37 (a static swapchain, 1 image(s)) filled once: 64 black alpha levels") &&
              log_has("xr: fade level 128 (ramp 32 of 63, alpha 130)"),
          "log lacks the fade lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_fade_reject(void) { /* MOCKXR_REJECT_HUD=1: the first frame with two layers is rejected: fade off */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) {
        CHECK(locate(&v), "locate");
        write_record_fade(f, SOMVR_LAYOUT_A, &v, 255);
        tagged_present(&a, f, 0, TAG_SYNC);
        check_proj(&v, 3, RECT_A, "fade rejected / off");
    }
    CHECK(MOCK(mockxr_calls)("xrEndFrame") == 1 + 4 && MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H) < 0,
          "xrEndFrame calls %d (the mono present, frame 1 twice, frames 2-3); the fade swapchain alive",
          MOCK(mockxr_calls)("xrEndFrame"));
    app_teardown(&a);
    CHECK(log_count("xr: fade off for this session: xrEndFrame rejected the frame with the fade quad: "
                    "XR_ERROR_LAYER_INVALID; ended again without it") == 1,
          "log lacks the fade-off line (or has it twice)");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_fade_off(void) { /* SOMVR_XR_FADE=off: faded records, no quad, no image */
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    somvr_views v;
    int up0 = V.buf2img;
    for (uint64_t f = 1; f <= 2; f++) {
        CHECK(locate(&v), "locate");
        write_record_fade(f, SOMVR_LAYOUT_A, &v, f == 1 ? 128 : 255);
        tagged_present(&a, f, 0, TAG_SYNC);
        check_proj(&v, 3, RECT_A, "SOMVR_XR_FADE=off");
    }
    CHECK(V.buf2img == up0 && MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H) < 0, "SOMVR_XR_FADE=off made the image");
    app_teardown(&a);
    CHECK(log_has("xr: fade: off (SOMVR_XR_FADE=off)"), "log lacks the fade-off line");
    check_mock_clean();
    return 0;
}
static int sc_fade_hud(void) { /* layout B with the HUD quad: the projection, the HUD quad, then the fade quad */
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(1, 1920, 1080, r.H);
    somvr_views v;
    CHECK(locate(&v), "locate");
    write_record_fade(1, SOMVR_LAYOUT_B, &v, 128);
    V.ncp = V.ntr = 0;
    hud_present(&r.a, 1, 1, SOMVR_HUDK_WORLD);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    check_proj_n(&v, 3, RECT_B, "layout B + HUD + fade", 3);
    CHECK(e->q[1].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[1].sc == 1 && e->q[1].rect[2] == 1920,
          "layout B + HUD + fade: layer 1 is not the HUD quad (sc %d)", e->q[1].sc);
    check_fade_layer(2, 32, &v, "layout B + HUD + fade");
    /* the reuse repeats both quads */
    hud_present(&r.a, 77, 1, SOMVR_HUDK_WORLD);
    check_proj_n(&v, 3, RECT_B, "layout B reuse", 3);
    check_fade_layer(2, 32, &v, "layout B reuse");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* ---- comfort (docs/re-xr-pipeline.md 11): the preload's somvr_comfort, the vignette's ring and the snap blink ---- */
static somvr_comfort_shared *g_cfo;
/* comfort_obj.so next to the mock, in the global scope like LD_PRELOAD (absent in every other scenario) */
static int comfort_obj(int active, float vignette, uint32_t blink_ms) {
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/comfort_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (!h || !(g_cfo = (somvr_comfort_shared *)dlsym(h, SOMVR_COMFORT_SYMBOL)))
        return fprintf(stderr, "    comfort_obj.so: %s\n", dlerror()), -1;
    g_cfo->magic = SOMVR_COMFORT_MAGIC;
    g_cfo->version = SOMVR_COMFORT_VERSION;
    g_cfo->size = (uint32_t)sizeof *g_cfo;
    g_cfo->pid = (int32_t)getpid();
    g_cfo->vignette = vignette;
    g_cfo->blink_ms = blink_ms;
    if (active) atomic_store(&g_cfo->active, 1);
    return 0;
}
/* eye0_setup: frame f's entry (the rig's speeds, the blink's black) */
static uint32_t g_cf_xflags; /* the next entries' flags instead of COUNTED (a snap, a recentre) */
static void cf_put(uint64_t f, float lin, float ang, float blink) {
    somvr_comfort_frame e = {.lin_mps = lin, .ang_dps = ang, .blink = blink, .dt_s = 0.0111f,
                             .flags = g_cf_xflags ? g_cf_xflags : SOMVR_CFF_COUNTED};
    somvr_comfort_put(g_cfo, f, &e);
}
/* one layout-B stereo frame (H4: the record and the entry; H9: the tag, the HUD's entry hud_gen) */
static void cf_stereo(hudrig *r, uint64_t f, float lin, float ang, float blink, uint32_t hud_gen, somvr_views *v) {
    CHECK(locate(v), "frame %llu: locate", (unsigned long long)f);
    write_record(f, SOMVR_LAYOUT_B, v, 3);
    if (g_cfo) cf_put(f, lin, ang, blink);
    V.ncp = V.ntr = 0;
    hud_present(&r->a, f, hud_gen, hud_gen ? SOMVR_HUDK_WORLD : SOMVR_HUDK_NONE);
}
#define CF_DIST 5.0f
/* the mock's views (eye 0: -0.90..0.70, up 0.80, down -0.95; eye 1 mirrored): the largest corner and edge */
static float cf_corner_deg(void) { return atanf(sqrtf(tanf(0.9f) * tanf(0.9f) + tanf(0.95f) * tanf(0.95f))) * 57.29578f; }
static float cf_cover(void) { return tanf(0.95f); }
/* the last frame's comfort quad of kind 0 (the ring: the texture's 512x512 top) or 1 (the blink: a 2x6 cell): its layer
 * index (0 = none), its form checked: head-locked (a VIEW space) 5 m ahead, square, both eyes, source alpha */
static int cf_quad(int kind, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int k = MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H), at = 0;
    static const float pose[7] = {0, 0, 0, 1, 0, 0, -CF_DIST};
    for (uint32_t i = 1; k >= 0 && i < e->layer_count && i < MOCK_NQ; i++) {
        if (e->q[i].sc != k || (e->q[i].rect[2] == SOMVR_VIG_RING_PX) != (kind == 0)) continue;
        at = (int)i;
        CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].space == 2 &&
                  e->q[i].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT &&
                  e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && e->q[i].size[0] > 0 && e->q[i].size[0] == e->q[i].size[1] &&
                  pose_near(e->q[i].pose, pose),
              "%s: comfort quad %u: type %d space %d flags 0x%llx eyes %d size %g x %g", what, i, e->q[i].type,
              e->q[i].space, (unsigned long long)e->q[i].flags, e->q[i].eyes, e->q[i].size[0], e->q[i].size[1]);
        if (kind == 0)
            CHECK(e->q[i].rect[0] == 0 && e->q[i].rect[1] == 0 && e->q[i].rect[3] == SOMVR_VIG_RING_PX, "%s: ring rect",
                  what);
    }
    return at;
}
static float cf_size(int i) { return MOCK(mockxr_last_end)()->q[i].size[0]; }
/* the ring's clear half-angle from its quad's size (the texture is clear inside R0 of its half-size) */
static float cf_clear_deg(int i) { return atanf(SOMVR_VIG_R0 * cf_size(i) / (2 * CF_DIST)) * 57.29578f; }
static uint8_t cf_alpha_px(int x, int y) { return V.up_px[4 * ((size_t)y * SOMVR_VIG_IMG_W + x) + 3]; }

static int sc_vignette(void) { /* strength 0.8: still, moving, a reuse frame, a menu, stopping, turning */
    hudrig r;
    if (comfort_obj(1, 0.8f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    /* 1. standing still: the projection alone, no comfort swapchain, nothing uploaded */
    for (; f <= 3; f++) {
        cf_stereo(&r, f, 0, 0, 0, 0, &v);
        check_no_quad(&v, "still");
    }
    CHECK(MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H) < 0 && V.buf2img == 0 && !MOCK(mockxr_view_live)(),
          "still: a comfort swapchain, an upload or a VIEW space");
    CHECK(log_has("xr: comfort: somvr_comfort found") && log_has("vignette strength 0.80") &&
              log_has("snap blink 0 ms (off)"),
          "the comfort setup line");
    /* 2. moving at 3 m/s (full speed): the ring within 3 frames, closing frame by frame to strength 0.8's angle */
    int first = 0, opened = 0, rings = 0;
    float prev = 1e9f, clear = 0;
    for (int i = 0; i < 140; i++, f++) {
        cf_stereo(&r, f, 3.0f, 0, 0, 0, &v);
        int q = cf_quad(0, "moving");
        if (q) {
            rings++;
            if (!first) first = i + 1;
            check_proj_n(&v, 3, RECT_B, "moving", 2);
            opened += cf_size(q) > prev + 1e-4f;
            prev = cf_size(q), clear = cf_clear_deg(q);
        }
    }
    float open = cf_corner_deg() + 2, want = open - (open - 20) * 0.8f;
    CHECK(first >= 1 && first <= 3 && !opened && rings == 141 - first, "moving: the ring from frame %d, %d frames opened",
          first, opened);
    CHECK(fabsf(clear - want) < 0.05f, "moving: clear %.3f deg at the end, want %.3f (corner %.2f)", clear, want,
          cf_corner_deg());
    /* the texture: one static swapchain, one upload of the image (the ring's alpha, the blink cells, black) */
    int k = MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H);
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_k)(k);
    CHECK(k >= 1 && s->create_flags == XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT && s->acquires == 1 &&
              s->format == VK_FORMAT_R8G8B8A8_SRGB && V.buf2img == 1 &&
              V.up_img == (VkImage)(uintptr_t)(0x5000 + 0x100 * k) && V.up_w == SOMVR_VIG_IMG_W && V.up_h == SOMVR_VIG_IMG_H,
          "texture: slot %d flags 0x%x acquires %d format %lld uploads %d into %p %ux%u", k, s->create_flags, s->acquires,
          (long long)s->format, V.buf2img, (void *)V.up_img, V.up_w, V.up_h);
    int colour = 0, mono = 1, last = -1;
    for (size_t i = 0; i < (size_t)SOMVR_VIG_IMG_W * SOMVR_VIG_IMG_H; i++)
        colour |= V.up_px[4 * i] | V.up_px[4 * i + 1] | V.up_px[4 * i + 2];
    for (int x = 256; x < 512; x++) mono &= cf_alpha_px(x, 256) >= last, last = cf_alpha_px(x, 256);
    int cells = 1;
    for (int c = 0; c < SOMVR_VIG_CELLS; c++) cells &= cf_alpha_px(4 * c + 1, SOMVR_VIG_CELL_Y + 3) == lrintf(255.0f * c / 127);
    CHECK(!colour && cf_alpha_px(256, 256) == 0 && cf_alpha_px(255, 255) == 0 && cf_alpha_px(0, 0) == 255 &&
              cf_alpha_px(511, 511) == 255 && cf_alpha_px(256 + 40, 256) == 0 && cf_alpha_px(256 + 72, 256) == 255 &&
              cf_alpha_px(256 + 56, 256) > 20 && cf_alpha_px(256 + 56, 256) < 235 && mono && cells &&
              cf_alpha_px(3, SOMVR_VIG_RING_PX + 2) == 255,
          "texture bytes: colour %d centre %u corners %u %u ramp %u %u %u monotonic %d cells %d guard %u", colour,
          cf_alpha_px(256, 256), cf_alpha_px(0, 0), cf_alpha_px(511, 511), cf_alpha_px(296, 256), cf_alpha_px(312, 256),
          cf_alpha_px(328, 256), mono, cells, cf_alpha_px(3, SOMVR_VIG_RING_PX + 2));
    CHECK(MOCK(mockxr_view_live)() == 1, "the VIEW space: %d live", MOCK(mockxr_view_live)());
    /* 3. a reuse frame (no record for the tag): the last frame's ring again, no copy */
    float sz = cf_size(cf_quad(0, "before reuse"));
    V.ncp = 0;
    tagged_present_eyes(&r.a, f++, 1, TAG_SYNC, 3, 2);
    int q = cf_quad(0, "reuse");
    CHECK(q && cf_size(q) == sz && MOCK(mockxr_last_end)()->layer_count == 2 && V.ncp == 0, "reuse: ring %d size %g (was %g)",
          q, q ? cf_size(q) : 0, sz);
    /* 4. a menu (a mono present): the menu quad alone */
    tagged_present(&r.a, 0, 0, TAG_SYNC);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 1 && MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD,
          "menu: %u layers", MOCK(mockxr_last_end)()->layer_count);
    /* 5. stopping: the ring opens frame by frame, goes, and stays gone */
    int gone = 0, closed = 0;
    prev = 0;
    for (int i = 0; i < 400 && !gone; i++, f++) {
        cf_stereo(&r, f, 0, 0, 0, 0, &v);
        if ((q = cf_quad(0, "stopping"))) closed += cf_size(q) < prev - 1e-4f, prev = cf_size(q);
        else gone = i + 1;
    }
    CHECK(gone > 15 && gone < 50 && !closed, "stopping: gone after %d frames (build 6: ~0.35 s = ~30), closed %d times",
          gone, closed);
    for (int i = 0; i < 20; i++, f++) {
        cf_stereo(&r, f, 0.2f, 5, 0, 0, &v); /* a drift below the onset */
        check_no_quad(&v, "stopped");
    }
    /* 6. a smooth turn (120 deg/s) brings it back */
    for (int i = 0; i < 4; i++, f++) cf_stereo(&r, f, 0, 120, 0, 0, &v);
    CHECK(cf_quad(0, "turning"), "turning: no ring");
    /* 7. a snap turn (build 6): standing, the snap's own frame shows the tunnel at the full strength at once, then it
     * opens and goes within ~0.45 s; a recentre's frame (the snap flag with it) none */
    for (int i = 0; i < 120; i++, f++) cf_stereo(&r, f, 0, 0, 0, 0, &v);
    check_no_quad(&v, "before the snap");
    g_cf_xflags = SOMVR_CFF_SNAP | SOMVR_CFF_HELD;
    cf_stereo(&r, f++, 0, 0, 0, 0, &v);
    g_cf_xflags = 0;
    q = cf_quad(0, "snap");
    float swant = cf_corner_deg() + 2 - (cf_corner_deg() + 2 - 20) * 0.8f;
    CHECK(q && fabsf(cf_clear_deg(q) - swant) < 0.2f && log_has("(a snap turn: the tunnel at once)"),
          "snap: ring %d clear %g deg (want %g)", q, q ? cf_clear_deg(q) : 0, swant);
    int sgone = 0;
    for (int i = 0; i < 90 && !sgone; i++, f++) {
        cf_stereo(&r, f, 0, 0, 0, 0, &v);
        if (!cf_quad(0, "after the snap")) sgone = i + 1;
    }
    CHECK(sgone > 5 && sgone < 45, "the snap tunnel gone after %d frames", sgone);
    g_cf_xflags = SOMVR_CFF_SNAP | SOMVR_CFF_RECENTER | SOMVR_CFF_HELD;
    cf_stereo(&r, f++, 0, 0, 0, 0, &v);
    g_cf_xflags = 0;
    check_no_quad(&v, "a recentre with a snap");
    CHECK(atomic_load(&g_cfo->n_vignette) >= (uint64_t)(rings + gone) && atomic_load(&g_cfo->n_blink) == 0,
          "counters: vignette %llu blink %llu", (unsigned long long)atomic_load(&g_cfo->n_vignette),
          (unsigned long long)atomic_load(&g_cfo->n_blink));
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "comfort objects outlived the session");
    CHECK(log_has("xr: comfort: image 512x528 format 43 (a static swapchain, 1 image(s)) filled once") &&
              log_has(": ring (clear") && log_has(": no ring (clear") && !log_has("comfort: off"),
          "log lines");
    check_mock_clean();
    return 0;
}

static int sc_blink(void) { /* blink 90 ms, strength 0: the black frame by frame, exactly the frame's own entry */
    hudrig r;
    if (comfort_obj(1, 0, 90) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    for (int i = 0; i < 10; i++) { /* fast, but strength 0: never a ring */
        cf_stereo(&r, f++, 5.0f, 200, 0, 0, &v);
        check_no_quad(&v, "strength 0");
    }
    CHECK(log_has("vignette strength 0.00 (off)") && log_has("snap blink 90 ms"), "the comfort setup line");
    /* the game is a frame ahead: frame f+1's entry is black, frame f's is not: frame f shows no black */
    CHECK(locate(&v), "locate");
    write_record(f, SOMVR_LAYOUT_B, &v, 3);
    cf_put(f, 0, 0, 0);
    cf_put(f + 1, 0, 0, 1);
    V.ncp = V.ntr = 0;
    hud_present(&r.a, f++, 0, 0);
    check_no_quad(&v, "the next frame's black");
    static const float seq[] = {0.37f, 0.74f, 1.0f, 0.815f, 0.63f, 0.444f, 0.259f, 0.074f, 0.0f, 0.0f};
    int shown = 0;
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++, f++) {
        cf_stereo(&r, f, 0, 0, seq[i], 0, &v);
        int cell = (int)lrintf(seq[i] * 127), q = cf_quad(1, "blink");
        if (!cell) {
            check_no_quad(&v, "no black");
            continue;
        }
        const mockxr_end *e = MOCK(mockxr_last_end)();
        shown++;
        check_proj_n(&v, 3, RECT_B, "blink", 2);
        CHECK(q == 1 && e->q[1].rect[0] == 4 * cell + 1 && e->q[1].rect[1] == SOMVR_VIG_CELL_Y + 1 && e->q[1].rect[2] == 2 &&
                  e->q[1].rect[3] == 6 && fabsf(cf_size(1) - 2 * CF_DIST * 1.1f * cf_cover()) < 1e-3f &&
                  !cf_quad(0, "blink"),
              "blink %.3f: layer %d rect %d,%d %dx%d (cell %d) size %g", seq[i], q, e->q[1].rect[0], e->q[1].rect[1],
              e->q[1].rect[2], e->q[1].rect[3], cell, cf_size(1));
    }
    CHECK(shown == 8 && atomic_load(&g_cfo->n_blink) == 8 && atomic_load(&g_cfo->n_vignette) == 0,
          "blink frames %d (counter %llu), vignette %llu", shown, (unsigned long long)atomic_load(&g_cfo->n_blink),
          (unsigned long long)atomic_load(&g_cfo->n_vignette));
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* build 12, the wraith dash (SOMVR_CFF_WRAITH, SOMVR_BOW_STRIKE=dash|blink): standing still, a frame with the flag and
 * a 0.4 tunnel shows the ring from a second, white-blue swapchain of the same size (made at its first use), at least
 * the tunnel's amount; the next plain frame no ring; a wraith blink is a cell of the white-blue image */
static int sc_wraith(void) {
    hudrig r;
    if (comfort_obj(1, 0.2f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    for (int i = 0; i < 4; i++) {
        cf_stereo(&r, f++, 0, 0, 0, 0, &v);
        check_no_quad(&v, "standing");
    }
    int black = MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H);
    CHECK(black < 0, "no comfort image before a ring: %d", black);
    g_cf_xflags = SOMVR_CFF_WRAITH | SOMVR_CFF_COUNTED | somvr_cff_tunnel_bits(0.4f);
    cf_stereo(&r, f++, 0, 0, 0, 0, &v);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    black = MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H);
    int ring = 0, sc = -1;
    for (uint32_t i = 1; i < e->layer_count && i < MOCK_NQ; i++)
        if (e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].rect[2] == SOMVR_VIG_RING_PX)
            ring = (int)i, sc = e->q[i].sc;
    CHECK(ring > 0 && black >= 0 && sc >= 0 && sc != black && V.buf2img == 2,
          "the tunnel: a ring (layer %d) from the white-blue swapchain %d, not the black %d (uploads %d)", ring, sc, black,
          V.buf2img);
    /* the uploaded white-blue image: an opaque corner's texel is the wraith's colour, not black */
    CHECK(V.up_px[0] > 150 && V.up_px[2] == 255 && V.up_px[3] == 255, "the last upload is white-blue: %u %u %u %u",
          V.up_px[0], V.up_px[1], V.up_px[2], V.up_px[3]);
    float clear = ring ? atanf(SOMVR_VIG_R0 * e->q[ring].size[0] / (2 * CF_DIST)) * 57.29578f : 0;
    CHECK(clear > 10 && clear < cf_corner_deg(), "the tunnel closes into the view: clear %.1f deg", clear);
    g_cf_xflags = 0;
    cf_stereo(&r, f++, 0, 0, 0, 0, &v);
    check_no_quad(&v, "after the dash");
    g_cf_xflags = SOMVR_CFF_WRAITH | SOMVR_CFF_COUNTED;
    cf_stereo(&r, f++, 0, 0, 1.0f, 0, &v);
    e = MOCK(mockxr_last_end)();
    int cell = 0;
    for (uint32_t i = 1; i < e->layer_count && i < MOCK_NQ; i++)
        if (e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].rect[2] == 2) cell = e->q[i].sc == sc ? 1 : -1;
    CHECK(cell == 1, "the white blink: a cell of the white-blue image (%d)", cell);
    g_cf_xflags = 0;
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "comfort objects outlived the session");
    CHECK(log_has("the wraith tunnel's white-blue image 512x528") && log_has("the wraith dash begins") &&
              !log_has("comfort: off"),
          "log lines");
    check_mock_clean();
    return 0;
}

/* build 10, SOMVR_VIGNETTE_FIT=lens: the user's 40 % while walking with the stick (2.3 m/s) closes to sqrt(0.4) of the
 * way to 15 deg (black inside the lens, not only in the corners); a smooth turn at 60 deg/s is full speed too; the
 * menu's 0 % (the object's strength 0) is no ring at any speed; 100 % a porthole */
static int sc_vignette_lens(void) {
    hudrig r;
    if (comfort_obj(1, 0.4f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    float open = cf_corner_deg() + 2, clear = 0;
    for (int i = 0; i < 120; i++, f++) {
        cf_stereo(&r, f, 2.3f, 0, 0, 0, &v);
        int q = cf_quad(0, "lens walk");
        if (q) clear = cf_clear_deg(q);
    }
    float want = open - (open - 15) * sqrtf(0.4f);
    CHECK(fabsf(clear - want) < 0.1f && clear < 35, "lens walk at 40 %%: clear %.2f deg, want %.2f", clear, want);
    CHECK(log_has("SOMVR_VIGNETTE_FIT=lens") && log_has("the square root of that share") &&
              log_has("at the view's nearest edge"),
          "the lens setup / frame lines");
    for (int i = 0; i < 120; i++, f++) cf_stereo(&r, f, 0, 0, 0, 0, &v); /* stop: it opens and goes */
    check_no_quad(&v, "lens stopped");
    for (int i = 0; i < 120; i++, f++) cf_stereo(&r, f, 0, 60, 0, 0, &v); /* the menu's slowest smooth turn */
    int q = cf_quad(0, "lens turn");
    CHECK(q && fabsf(cf_clear_deg(q) - want) < 0.1f, "lens turn at 60 deg/s: clear %.2f, want %.2f", q ? cf_clear_deg(q) : 0,
          want);
    g_cfo->vignette = 0; /* the menu's Off (the layer reads the strength every frame) */
    for (int i = 0; i < 120; i++, f++) cf_stereo(&r, f, 3.0f, 120, 0, 0, &v);
    check_no_quad(&v, "lens at 0 %");
    uint64_t n0 = atomic_load(&g_cfo->n_vignette);
    for (int i = 0; i < 30; i++, f++) {
        cf_stereo(&r, f, 4.0f, 200, 0, 0, &v);
        check_no_quad(&v, "lens at 0 %, fast");
    }
    CHECK(atomic_load(&g_cfo->n_vignette) == n0, "0 %%: ring frames counted");
    g_cfo->vignette = 1.0f;
    for (int i = 0; i < 120; i++, f++) cf_stereo(&r, f, 2.3f, 0, 0, 0, &v);
    q = cf_quad(0, "lens 100 %");
    CHECK(q && fabsf(cf_clear_deg(q) - 15) < 0.1f, "lens 100 %%: clear %.2f", q ? cf_clear_deg(q) : 0);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "comfort objects outlived the session");
    check_mock_clean();
    return 0;
}

/* SOMVR_XR_HUD=head: the projection, the ring (over the world, under the HUD), the HUD quad, the blink: 4 layers */
static int sc_vignette_hud(void) {
    hudrig r;
    if (comfort_obj(1, 1.0f, 90) || hud_setup(&r, 2)) return 1;
    somvr_views v;
    hud_info(2, 1920, 1080, r.H);
    uint64_t f = 1;
    cf_stereo(&r, f++, 0, 0, 0, 2, &v); /* the HUD's swapchain and VIEW space first */
    for (int i = 0; i < 30; i++, f++) cf_stereo(&r, f, 3.0f, 0, i == 29 ? 0.5f : 0, 2, &v);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    check_proj_n(&v, 3, RECT_B, "with the HUD", 4);
    CHECK(e->q[2].sc == 1 && e->q[2].space == 2 && e->q[2].rect[2] == 1920 && cf_quad(0, "with the HUD") == 1 &&
              cf_quad(1, "with the HUD") == 3 && e->q[2].view >= 0 && e->q[1].view != e->q[2].view &&
              e->q[1].view == e->q[3].view && MOCK(mockxr_view_live)() == 2,
          "layers: HUD sc %d space %d view %d, ring at %d view %d, blink view %d; VIEW spaces %d", e->q[2].sc, e->q[2].space,
          e->q[2].view, cf_quad(0, "x"), e->q[1].view, e->q[3].view, MOCK(mockxr_view_live)());
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* no object / an inactive one: entries say "fast and black", nothing of comfort's is made or drawn */
static int cf_none(int obj, const char *logline) {
    hudrig r;
    if ((obj && comfort_obj(0, 0.8f, 90)) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    for (uint64_t f = 1; f <= 6; f++) {
        cf_stereo(&r, f, 3.0f, 90, 1.0f, 0, &v);
        check_no_quad(&v, "no comfort");
    }
    CHECK(MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H) < 0 && V.buf2img == 0 && !MOCK(mockxr_view_live)() &&
              log_has(logline),
          "comfort without its object: log \"%s\" %d", logline, log_has(logline));
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}
static int sc_vignette_absent(void) {
    return cf_none(0, "xr: comfort: no vignette, no snap blink (no somvr_comfort: a preload without it)");
}
static int sc_vignette_inactive(void) { return cf_none(1, "xr: comfort: no vignette, no snap blink (somvr_comfort inactive"); }

/* a failure: comfort off for the session (logged), the frames go on with the projection alone, XR up */
static int cf_fail(const char *logline, int extra_ends) {
    hudrig r;
    if (comfort_obj(1, 0.8f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    cf_stereo(&r, f++, 0, 0, 0, 0, &v); /* the eye swapchain first */
    int ends = MOCK(mockxr_calls)("xrEndFrame");
    for (int i = 0; i < 12; i++, f++) { /* (the frame that made and filled the texture released its image too) */
        cf_stereo(&r, f, 3.0f, 0, 0, 0, &v);
        check_proj_n(&v, 3, RECT_B, "comfort failed", 1);
        CHECK(V.ncp == 2, "comfort failed: %d copies", V.ncp);
    }
    CHECK(log_has(logline) && log_count("xr: comfort: off for this session") == 1 &&
              MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H) < 0 && !MOCK(mockxr_view_live)() &&
              MOCK(mockxr_calls)("xrEndFrame") == ends + 12 + extra_ends && atomic_load(&g_sh.n_stereo) == 13,
          "comfort failure: log %d, swapchain %d, VIEW %d, ends %d (+%d), stereo %llu", log_has(logline),
          MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H), MOCK(mockxr_view_live)(),
          MOCK(mockxr_calls)("xrEndFrame") - ends, extra_ends, (unsigned long long)atomic_load(&g_sh.n_stereo));
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_vignette_fail(void) {
    return cf_fail("xr: comfort: off for this session: xrEnumerateSwapchainImages (comfort)", 0);
}
static int sc_vignette_reject(void) {
    return cf_fail("xr: comfort: off for this session: xrEndFrame rejected the frame with the comfort layers (1): "
                   "XR_ERROR_LAYER_INVALID",
                   1);
}
static int sc_vignette_nostatic(void) { /* no static swapchain: an ordinary one, filled once all the same */
    hudrig r;
    if (comfort_obj(1, 0.8f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    cf_stereo(&r, f++, 0, 0, 0, 0, &v);
    for (int i = 0; i < 10; i++, f++) cf_stereo(&r, f, 3.0f, 0, 0, 0, &v);
    int k = MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H);
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_k)(k);
    CHECK(k >= 1 && s->create_flags == 0 && s->acquires == 1 && V.buf2img == 1 && cf_quad(0, "ordinary") &&
              log_has("(an ordinary swapchain, 3 image(s)) filled once"),
          "ordinary: slot %d flags 0x%x acquires %d uploads %d", k, s->create_flags, s->acquires, V.buf2img);
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}

/* ---- the threat cues (docs/re-xr-pipeline.md 18): the preload's somvr_threat, the glows and the edge bands --------- */
static somvr_threat_shared *g_tho;
/* threat_obj.so next to the mock, in the global scope like LD_PRELOAD (absent in every other scenario) */
static int threat_obj(int active) {
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/threat_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (!h || !(g_tho = (somvr_threat_shared *)dlsym(h, SOMVR_THREAT_SYMBOL)))
        return fprintf(stderr, "    threat_obj.so: %s\n", dlerror()), -1;
    g_tho->magic = SOMVR_THREAT_MAGIC;
    g_tho->version = SOMVR_THREAT_VERSION;
    g_tho->size = (uint32_t)sizeof *g_tho;
    g_tho->pid = (int32_t)getpid();
    g_tho->glow = 0.7f, g_tho->edge = 0.8f;
    if (active) atomic_store(&g_tho->active, 1);
    return 0;
}
/* one layout-B stereo frame (H4: the record and the threat entry e, NULL = none; H9: the tag, no HUD) */
static void th_stereo(hudrig *r, uint64_t f, const somvr_threat_frame *e, somvr_views *v) {
    CHECK(locate(v), "frame %llu: locate", (unsigned long long)f);
    write_record(f, SOMVR_LAYOUT_B, v, 3);
    if (g_tho && e) somvr_threat_put(g_tho, f, e);
    if (g_cfo) cf_put(f, 3.0f, 0, 0); /* comfort, if loaded: moving at full speed (the ring) */
    V.ncp = V.ntr = 0;
    hud_present(&r->a, f, 0, SOMVR_HUDK_NONE);
}
static somvr_threat_frame th_frame_of(int n, float level, float el, float er) {
    somvr_threat_frame e;
    memset(&e, 0, sizeof e);
    static const float pos[3][3] = {{0.2f, 1.7f, -1.5f}, {-0.6f, 1.4f, -2.5f}, {1.0f, 2.0f, -4.0f}};
    for (int i = 0; i < n && i < SOMVR_TH_GLOWS; i++) {
        memcpy(e.g[i].pos, pos[i], sizeof pos[i]);
        e.g[i].size_m = 0.25f + 0.1f * (float)i, e.g[i].level = level * (1 - 0.2f * (float)i), e.g[i].kind = SOMVR_THK_MELEE;
    }
    e.nglow = (uint32_t)n, e.edge[0] = el, e.edge[1] = er, e.threats = (uint32_t)n;
    return e;
}
/* the last frame's threat quad at layer i: a glow (world-locked in the reference space, billboarded to the record's
 * head, its level's glow cell) or an edge band (head-locked in a VIEW space, 1 m ahead at the view's edge) */
static void th_check_glow(int i, const somvr_views *v, const somvr_th_glow *g, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int k = MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H);
    float head[3], pose[7];
    int32_t rc[4];
    for (int j = 0; j < 3; j++) head[j] = 0.5f * (v->pose[0].p[j] + v->pose[1].p[j]);
    somvr_th_face(g->pos, head, pose);
    memcpy(pose + 4, g->pos, 12);
    somvr_th_rect(SOMVR_TH_ROW_GLOW, somvr_th_cell(g->level), rc);
    CHECK(i < (int)e->layer_count && k >= 1 && e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].sc == k &&
              e->q[i].space == 1 && e->q[i].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT &&
              e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && !memcmp(e->q[i].rect, rc, sizeof rc) &&
              e->q[i].size[0] == g->size_m && e->q[i].size[1] == g->size_m && pose_near(e->q[i].pose, pose),
          "%s: glow at layer %d: type %d sc %d (want %d) space %d flags 0x%llx rect %d,%d %dx%d (want %d,%d) size %g pose "
          "%g,%g,%g,%g %g,%g,%g", what, i, e->q[i].type, e->q[i].sc, k, e->q[i].space, (unsigned long long)e->q[i].flags,
          e->q[i].rect[0], e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3], rc[0], rc[1], e->q[i].size[0],
          e->q[i].pose[0], e->q[i].pose[1], e->q[i].pose[2], e->q[i].pose[3], e->q[i].pose[4], e->q[i].pose[5],
          e->q[i].pose[6]);
}
static void th_check_edge_row(int i, const somvr_views *v, int side, float level, int row, const char *what);
static void th_check_edge(int i, const somvr_views *v, int side, float level, const char *what) {
    th_check_edge_row(i, v, side, level, SOMVR_TH_ROW_EDGE, what);
}
/* build 10: the band in a given row (the hurt flash's red row takes the band's slot) */
static void th_check_edge_row(int i, const somvr_views *v, int side, float level, int row, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int k = MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H);
    float fov[2][4], pose[7], size[2];
    for (int j = 0; j < 2; j++) fov[j][0] = v->fov[j].l, fov[j][1] = v->fov[j].r, fov[j][2] = v->fov[j].u, fov[j][3] = v->fov[j].d;
    somvr_th_edge_quad(fov, side, 1.0f, pose, size);
    int32_t rc[4];
    somvr_th_rect(row, somvr_th_cell(level), rc);
    CHECK(i < (int)e->layer_count && k >= 1 && e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].sc == k &&
              e->q[i].space == 2 && e->q[i].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT &&
              !memcmp(e->q[i].rect, rc, sizeof rc) && fabsf(e->q[i].size[0] - size[0]) < 1e-6f &&
              fabsf(e->q[i].size[1] - size[1]) < 1e-6f && pose_near(e->q[i].pose, pose) &&
              (side == SOMVR_TH_LEFT ? e->q[i].pose[4] < -0.5f : e->q[i].pose[4] > 0.5f),
          "%s: edge %s at layer %d: type %d sc %d space %d rect %d,%d size %g x %g pose %g,%g,%g,%g %g,%g,%g", what,
          side == SOMVR_TH_LEFT ? "L" : "R", i, e->q[i].type, e->q[i].sc, e->q[i].space, e->q[i].rect[0], e->q[i].rect[1],
          e->q[i].size[0], e->q[i].size[1], e->q[i].pose[0], e->q[i].pose[1], e->q[i].pose[2], e->q[i].pose[3],
          e->q[i].pose[4], e->q[i].pose[5], e->q[i].pose[6]);
}

static int sc_threat(void) { /* nothing, a glow, three glows and both edges, a reuse frame, levels below a cell, gone */
    hudrig r;
    if (threat_obj(1) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    uint64_t f = 1;
    /* 1. entries without a cue: the projection alone, no threat swapchain, no upload, no VIEW space */
    somvr_threat_frame none = th_frame_of(0, 0, 0, 0);
    for (; f <= 3; f++) {
        th_stereo(&r, f, &none, &v);
        check_no_quad(&v, "no threat");
    }
    CHECK(MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H) < 0 && V.buf2img == 0 && !MOCK(mockxr_view_live)() &&
              log_has("xr: threat: somvr_threat found") && log_has("glow strength 0.70, edge strength 0.80"),
          "no threat: a swapchain, an upload, a VIEW space, or no setup line");
    /* 2. one glow: a world-locked quad right over the projection; the texture filled once, byte for byte */
    somvr_threat_frame e1 = th_frame_of(1, 0.6f, 0, 0);
    th_stereo(&r, f++, &e1, &v);
    check_proj_n(&v, 3, RECT_B, "one glow", 2);
    th_check_glow(1, &v, &e1.g[0], "one glow");
    int k = MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H);
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_k)(k);
    static uint8_t want[SOMVR_TH_IMG_W * SOMVR_TH_IMG_H * 4];
    somvr_th_image(want, 0);
    CHECK(k >= 1 && s->create_flags == XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT && s->acquires == 1 &&
              s->format == VK_FORMAT_R8G8B8A8_SRGB && V.buf2img == 1 && V.up_w == SOMVR_TH_IMG_W && V.up_h == SOMVR_TH_IMG_H &&
              !memcmp(V.up_px, want, sizeof want) && !MOCK(mockxr_view_live)(),
          "the texture: slot %d flags 0x%x acquires %d format %lld uploads %d %ux%u bytes %d VIEW %d", k, s->create_flags,
          s->acquires, (long long)s->format, V.buf2img, V.up_w, V.up_h, !memcmp(V.up_px, want, sizeof want),
          MOCK(mockxr_view_live)());
    /* 3. three glows and both edges: the glows first (strongest first), then the left and the right band */
    somvr_threat_frame e3 = th_frame_of(3, 0.9f, 0.5f, 0.8f);
    th_stereo(&r, f++, &e3, &v);
    check_proj_n(&v, 3, RECT_B, "three glows, two edges", 6);
    for (int i = 0; i < 3; i++) th_check_glow(1 + i, &v, &e3.g[i], "three glows");
    th_check_edge(4, &v, SOMVR_TH_LEFT, 0.5f, "three glows, two edges");
    th_check_edge(5, &v, SOMVR_TH_RIGHT, 0.8f, "three glows, two edges");
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(MOCK(mockxr_view_live)() == 1 && e->q[4].view == e->q[5].view && V.buf2img == 1 &&
              MOCK(mockxr_swapchain_k)(k)->acquires == 1,
          "edges: VIEW spaces %d, views %d %d, uploads %d", MOCK(mockxr_view_live)(), e->q[4].view, e->q[5].view, V.buf2img);
    /* 4. a reuse frame (no record for the tag): the last frame's cues again, no copy */
    V.ncp = 0;
    tagged_present_eyes(&r.a, f++, 1, TAG_SYNC, 3, 2);
    e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 6 && V.ncp == 0 && e->q[1].sc == k && e->q[5].space == 2, "reuse: %u layers, %d copies",
          e->layer_count, V.ncp);
    /* 5. levels below half a cell, a glow without size or with a NaN point, an entry for another frame: nothing drawn */
    somvr_threat_frame lo = th_frame_of(2, 0.02f, 0.01f, 0);
    lo.g[1].level = 0.9f, lo.g[1].pos[1] = NAN;
    th_stereo(&r, f++, &lo, &v);
    check_no_quad(&v, "below a cell");
    CHECK(locate(&v), "locate");
    write_record(f, SOMVR_LAYOUT_B, &v, 3);
    somvr_threat_put(g_tho, f + 100, &e3); /* far from this frame: not used */
    V.ncp = V.ntr = 0;
    hud_present(&r.a, f++, 0, SOMVR_HUDK_NONE);
    check_no_quad(&v, "another frame's entry");
    /* 6. a near entry (the game wrote the next frame's): used */
    CHECK(locate(&v), "locate");
    write_record(f, SOMVR_LAYOUT_B, &v, 3);
    somvr_threat_put(g_tho, f + 1, &e1);
    V.ncp = V.ntr = 0;
    hud_present(&r.a, f++, 0, SOMVR_HUDK_NONE);
    th_check_glow(1, &v, &e1.g[0], "the newest entry");
    CHECK(atomic_load(&g_tho->n_glow) == 3 && atomic_load(&g_tho->n_edge) == 1, "counters: glow %llu edge %llu",
          (unsigned long long)atomic_load(&g_tho->n_glow), (unsigned long long)atomic_load(&g_tho->n_edge));
    /* 7. build 10: the hurt flash (SOMVR_HURT): the red row takes a side's band slot while it is the stronger; alone on
     * the other side; a fading flash under a strong warning band leaves the band */
    somvr_threat_frame hu = th_frame_of(0, 0, 0.3f, 0);
    hu.hurt[0] = 255, hu.hurt[1] = 128, hu.flags = SOMVR_THF_HURT;
    th_stereo(&r, f++, &hu, &v);
    check_proj_n(&v, 3, RECT_B, "the hurt flash", 3);
    th_check_edge_row(1, &v, SOMVR_TH_LEFT, 1.0f, SOMVR_TH_ROW_HURT, "the hurt flash");
    th_check_edge_row(2, &v, SOMVR_TH_RIGHT, 128 / 255.0f, SOMVR_TH_ROW_HURT, "the hurt flash");
    somvr_threat_frame hw = th_frame_of(0, 0, 0.9f, 0);
    hw.hurt[0] = 40;
    th_stereo(&r, f++, &hw, &v);
    check_proj_n(&v, 3, RECT_B, "a fading flash under a band", 2);
    th_check_edge(1, &v, SOMVR_TH_LEFT, 0.9f, "a fading flash under a band");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "threat objects outlived the session");
    CHECK(log_has("xr: threat: image 544x102 format 43 (a static swapchain, 1 image(s)) filled once: 16 glow and 16 edge "
                  "cells") &&
              log_has("xr: threat frame") && log_has("(newest entry)") && log_has("L hurt red, R hurt red") &&
              !log_has("threat: off"),
          "threat log lines");
    check_mock_clean();
    return 0;
}

/* with comfort's ring: the glows under it, the edge bands over it (docs/re-xr-pipeline.md 16, 17) */
static int sc_threat_vignette(void) {
    hudrig r;
    if (threat_obj(1) || comfort_obj(1, 0.8f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    somvr_threat_frame e2 = th_frame_of(1, 0.9f, 0.7f, 0);
    for (uint64_t f = 1; f <= 8; f++) th_stereo(&r, f, &e2, &v);
    check_proj_n(&v, 3, RECT_B, "with the ring", 4);
    th_check_glow(1, &v, &e2.g[0], "with the ring");
    CHECK(cf_quad(0, "with the threat cues") == 2, "the ring at layer %d, want 2 (over the glow)", cf_quad(0, "x"));
    th_check_edge(3, &v, SOMVR_TH_LEFT, 0.7f, "with the ring");
    CHECK(MOCK(mockxr_view_live)() == 2, "VIEW spaces: %d (comfort's, the threat's)", MOCK(mockxr_view_live)());
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* no object, an inactive one, SOMVR_XR_THREAT=off: entries full of cues, nothing of the threat's made or drawn */
static int th_none(int obj, int active, const char *logline) {
    hudrig r;
    if ((obj && threat_obj(active)) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    somvr_threat_frame e3 = th_frame_of(3, 0.9f, 0.5f, 0.8f);
    for (uint64_t f = 1; f <= 6; f++) {
        th_stereo(&r, f, &e3, &v);
        check_no_quad(&v, "no threat cues");
    }
    CHECK(MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H) < 0 && V.buf2img == 0 && !MOCK(mockxr_view_live)() &&
              log_has(logline),
          "threat without its cues: log \"%s\" %d", logline, log_has(logline));
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}
static int sc_threat_absent(void) { return th_none(0, 0, "xr: threat: no threat cues (no somvr_threat: a preload without it)"); }
static int sc_threat_inactive(void) { return th_none(1, 0, "xr: threat: no threat cues (somvr_threat inactive"); }
static int sc_threat_off(void) { return th_none(1, 1, "xr: threat: no threat cues (SOMVR_XR_THREAT=off)"); }

/* MOCKXR_REJECT_SC=544x102: the runtime never takes a frame with a threat quad. Only the threat cues go off (logged
 * once), that frame is ended again without them, the frames go on with the projection (and comfort's ring) */
static int sc_threat_reject(void) {
    hudrig r;
    if (threat_obj(1) || comfort_obj(1, 0.8f, 0) || hud_setup(&r, 0)) return 1;
    somvr_views v;
    somvr_threat_frame e3 = th_frame_of(2, 0.9f, 0.5f, 0);
    th_stereo(&r, 1, NULL, &v); /* the eye swapchain first */
    int ends = MOCK(mockxr_calls)("xrEndFrame");
    for (uint64_t f = 2; f <= 13; f++) th_stereo(&r, f, &e3, &v);
    CHECK(log_count("xr: threat: off for this session") == 1 &&
              log_has("xr: threat: off for this session: xrEndFrame rejected the frame with the threat cues (3): "
                      "XR_ERROR_LAYER_INVALID; ended again without them") &&
              MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H) < 0 && MOCK(mockxr_rejects)() == 1 &&
              MOCK(mockxr_calls)("xrEndFrame") == ends + 12 + 1 && cf_quad(0, "after the reject") == 1 &&
              !log_has("comfort: off"),
          "threat reject: log %d, swapchain %d, rejects %d, ends %d, ring at %d", log_count("xr: threat: off for this session"),
          MOCK(mockxr_sc_find)(SOMVR_TH_IMG_W, SOMVR_TH_IMG_H), MOCK(mockxr_rejects)(),
          MOCK(mockxr_calls)("xrEndFrame") - ends, cf_quad(0, "x"));
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* ---- every part at once (docs/re-xr-pipeline.md 16): the composition's order and count, the fail-soft ----------- */
/* the last frame's composition by what each quad after layer 0 shows: the ring and the blink (comfort's swapchain, the
 * ring's 512-wide rect or a cell), the HUD, the pointer, the fade. 1 = in the order the layer promises (ring, HUD,
 * pointer x <= 2, blink, fade; each kind at most once), within the 7 layers; kinds[] = how many of each */
enum { AK_RING = 1, AK_HUD, AK_PTR, AK_BLINK, AK_FADE, AK_N };
static int comp_kinds(int kinds[AK_N], const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int kc = MOCK(mockxr_sc_find)(SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H), kh = MOCK(mockxr_sc_find)(1920, 1080);
    int kp = MOCK(mockxr_sc_find)(SOMVR_PTR_IMG_W, SOMVR_PTR_IMG_H), kf = MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H);
    int last = 0, ok = e->layer_count <= 7 && (!e->layer_count || e->same_swapchain);
    char seq[64] = "";
    memset(kinds, 0, AK_N * sizeof kinds[0]);
    for (uint32_t i = 1; i < e->layer_count && i < MOCK_NQ; i++) {
        int sc = e->q[i].sc, k = sc < 0 ? 0 : sc == kc ? (e->q[i].rect[2] == SOMVR_VIG_RING_PX ? AK_RING : AK_BLINK)
                                      : sc == kh ? AK_HUD : sc == kp ? AK_PTR : sc == kf ? AK_FADE : 0;
        ok &= k > 0 && (k > last || (k == AK_PTR && last == AK_PTR && kinds[AK_PTR] < 2));
        kinds[k]++, last = k;
        size_t o = strlen(seq);
        snprintf(seq + o, sizeof seq - o, "%c", "?RHPBF"[k]);
    }
    CHECK(ok, "%s: %u layers (layer 0 type %d, on the projection swapchain %d), then %s: not projection, ring, HUD, "
          "pointer, blink, fade", what, e->layer_count, e->type, e->same_swapchain, seq);
    return ok;
}
/* a layout-B stereo frame: the record (with its fade level), comfort's entry, the HUD's entry, the tag (eye gen) */
static void allon_stereo(app_t *a, uint64_t f, uint32_t eye_gen, uint32_t hud_gen, uint32_t fade, float lin, float blink,
                         somvr_views *v) {
    CHECK(locate(v), "frame %llu: locate", (unsigned long long)f);
    write_record_fade(f, SOMVR_LAYOUT_B, v, fade);
    cf_put(f, lin, 0, blink);
    V.ncp = V.ntr = 0;
    somvr_hud_tag_put(g_hud, atomic_load(&g_sh.tag_seq) + 1, hud_gen, hud_gen ? SOMVR_HUDK_WORLD : SOMVR_HUDK_NONE);
    tagged_present_eyes(a, f, 1, TAG_SYNC, 3, eye_gen);
}
static void allon_mono(app_t *a) { /* a menu present (layout B: frame 0), no HUD entry */
    somvr_hud_tag_put(g_hud, atomic_load(&g_sh.tag_seq) + 1, 0, SOMVR_HUDK_NONE);
    V.ncp = V.ntr = 0;
    tagged_present(a, 0, 0, TAG_SYNC);
}
/* the right hand aimed at the last frame's HUD quad (its centre, as the mock saw it), the left one away */
static void allon_aim_hud(void) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int kh = MOCK(mockxr_sc_find)(1920, 1080);
    for (uint32_t i = 1; kh >= 0 && i < e->layer_count && i < MOCK_NQ; i++)
        if (e->q[i].sc == kh) {
            mockxr_hand R;
            float c[3] = {e->q[i].pose[4], e->q[i].pose[5], e->q[i].pose[6]};
            ptr_aim_at(&R, 1, c, 0);
            hand_set(1, &R);
        }
}
static int sc_allon(void) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR | SOMVR_INW_PTR_HUD, 1, SOMVR_INPUT_V2_SIZE) || comfort_obj(1, 0.8f, 90) || hud_obj(1) ||
        load_layer())
        return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R, L;
    float away[3] = {-3, 0, -1};
    ptr_aim(&R, 1, 0.5f, 0.5f, 0);
    ptr_aim_at(&L, 0, away, 0);
    hand_set(1, &R), hand_set(0, &L);
    int kinds[AK_N], tot[AK_N] = {0}, frames = 0, mono = 0, stereo = 0, reuse = 0, maxl = 0, bad = 0;
    /* the front end: menu quads with the pointer's ray and dot */
    for (int i = 0; i < 12; i++, frames++) {
        allon_mono(&a);
        bad += !comp_kinds(kinds, "front end");
        CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && !kinds[AK_RING] && !kinds[AK_HUD] &&
                  !kinds[AK_BLINK] && !kinds[AK_FADE],
              "front end %d: type %d, extras %d %d %d %d", i, MOCK(mockxr_last_end)()->type, kinds[AK_RING], kinds[AK_HUD],
              kinds[AK_BLINK], kinds[AK_FADE]);
        for (int k = 0; k < AK_N; k++) tot[k] += kinds[k];
        mono++;
    }
    CHECK(tot[AK_PTR] > 0, "the front end's pointer never drawn");
    /* the eye and HUD sizes announced, the world: SOMVR_EYE_SIZE's switch makes the game's surfaces and our textures */
    atomic_store(&g_sh.eyes.expect_w, ES_EW), atomic_store(&g_sh.eyes.expect_h, ES_EH);
    atomic_store(&g_hud->expect_w, 1920), atomic_store(&g_hud->expect_h, 1080);
    es_mode_set(&a, ES_EW, ES_EH, 4);
    VkImage L1 = app_image(&a, ES_EW, ES_EH, FERAL_USAGE), R1 = app_image(&a, ES_EW, ES_EH, FERAL_USAGE);
    VkImage H = app_image(&a, 1920, 1080, FERAL_USAGE);
    VkImage cur[2] = {L1, R1}, gone[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint32_t gen = 2, ew = ES_EW, eh = ES_EH;
    eyes_info(gen, ew, eh, L1, R1);
    hud_info(2, 1920, 1080, H);
    somvr_views v;
    uint64_t f = 1;
    /* SOMVR_BOW's and SOMVR_COMBAT_REACT's haptics (the game side posts them, the combat source): the draw's tension
     * ticks on the string hand, the release's kick on the bow hand and the slap on the string hand, a blocked hit on the
     * sword hand - each one apply at the next present, while every part of the composition is up */
    int hap_posts[2] = {0, 0}, hap0[2] = {MOCK(mockxr_haptic_get)(0)->applies, MOCK(mockxr_haptic_get)(1)->applies};
    float hap_amp[2] = {0, 0};
    for (int cycle = 0; cycle < 2; cycle++) {
        for (int i = 0; i < 140; i++, f++, frames++) {
            float ha[2] = {0, 0}, hd[2] = {0, 0}; /* this frame's posts: amplitude, ms */
            if (i >= 30 && i < 60 && i % 3 == 0) ha[1] = 0.08f + 0.032f * (float)(i - 30) / 3.0f, hd[1] = 15; /* ticks */
            if (i == 62) ha[0] = 0.85f, hd[0] = 60, ha[1] = 0.4f, hd[1] = 30; /* the release */
            if (i == 120) ha[1] = 0.6f, hd[1] = 40;                            /* REACT: a blocked hit */
            for (int hh = 0; hh < 2; hh++)
                if (ha[hh] > 0) {
                    somvr_haptic_req q = {.source = SOMVR_HAP_COMBAT, .amplitude = ha[hh],
                                          .duration_ns = (int64_t)(hd[hh] * 1e6f)};
                    somvr_haptic_post(g_in, hh, &q);
                    hap_posts[hh]++, hap_amp[hh] = ha[hh];
                }
            /* the HUD off now and then (a cinematic hides it), the fallback's fade in and out, moving (the ring), a
             * snap's blink, a reuse frame (a present whose record was overwritten) */
            uint32_t hud = (i / 25) % 3 == 2 ? 0 : 2, fade = i >= 60 && i < 90 ? (uint32_t)(i < 75 ? (i - 59) * 17 : (90 - i) * 17)
                                                                               : 0;
            float lin = i >= 20 && i < 110 ? 3.0f : 0, blink = i >= 80 && i < 86 ? (float)(i - 79) / 6.0f : 0;
            if (i == 100) { /* a reuse: the tag's record is not this frame's */
                V.ncp = V.ntr = 0;
                somvr_hud_tag_put(g_hud, atomic_load(&g_sh.tag_seq) + 1, hud, SOMVR_HUDK_WORLD);
                tagged_present_eyes(&a, f + 1000, 1, TAG_SYNC, 3, gen);
                reuse++;
            } else {
                allon_stereo(&a, f, gen, hud, fade, lin, blink, &v);
                stereo++;
            }
            const mockxr_end *e = MOCK(mockxr_last_end)();
            bad += !comp_kinds(kinds, "world");
            for (int k = 0; k < AK_N; k++) tot[k] += kinds[k];
            maxl = (int)e->layer_count > maxl ? (int)e->layer_count : maxl;
            const mockxr_swapchain *s = MOCK(mockxr_swapchain_get)();
            CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && s->width == 2 * ew && s->height == eh,
                  "cycle %d frame %d: type %d, swapchain %ux%u (eyes %ux%u)", cycle, i, e->type, s->width, s->height, ew, eh);
            if (i != 100) {
                CHECK(!kinds[AK_HUD] == !hud && !kinds[AK_FADE] == !fade && !kinds[AK_BLINK] == !(blink > 0) &&
                          (!kinds[AK_PTR] || kinds[AK_HUD]),
                      "cycle %d frame %d: HUD %d (asked %u) fade %d (level %u) blink %d (%g) pointer %d", cycle, i,
                      kinds[AK_HUD], hud, kinds[AK_FADE], fade, kinds[AK_BLINK], blink, kinds[AK_PTR]);
                CHECK(V.ncp == (hud ? 3 : 2) && V.cp[0].s == cur[0] && V.cp[1].s == cur[1] && (!hud || V.cp[2].s == H),
                      "cycle %d frame %d: %d copies from %p %p (the eye images are %p %p)", cycle, i, V.ncp,
                      (void *)V.cp[0].s, (void *)V.cp[1].s, (void *)cur[0], (void *)cur[1]);
            }
            for (int c = 0; c < V.ncp; c++)
                CHECK(V.cp[c].s != gone[0] && V.cp[c].s != gone[1], "cycle %d frame %d: copied from a destroyed eye image",
                      cycle, i);
            allon_aim_hud(); /* the pointer on the HUD from the next frames on (the quad placed again at the recentre) */
        }
        /* the hold ran out: the game's size; menus (mono, the pointer on the menu quad) */
        es_mode_set(&a, 1920, 1080, 4);
        hand_set(1, &R);
        for (int i = 0; i < 20; i++, frames++, mono++) {
            allon_mono(&a);
            bad += !comp_kinds(kinds, "menus");
            for (int k = 0; k < AK_N; k++) tot[k] += kinds[k];
            CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && !kinds[AK_RING] && !kinds[AK_HUD] &&
                      !kinds[AK_BLINK] && !kinds[AK_FADE] && MOCK(mockxr_swapchain_get)()->width == 3840,
                  "menus %d: type %d swapchain %u, extras", i, MOCK(mockxr_last_end)()->type,
                  MOCK(mockxr_swapchain_get)()->width);
        }
        /* back to the world at a new eye size (the runtime's own published late): new eye textures, the old destroyed;
         * one present still names the old generation (rendered before the mode set): never copied from */
        es_mode_set(&a, ES_EW + 40, ES_EH + 40, 4);
        atomic_store(&g_sh.eyes.expect_w, ES_EW + 40), atomic_store(&g_sh.eyes.expect_h, ES_EH + 40);
        for (int e = 0; e < 2; e++) {
            ((PFN_vkDestroyImage)L_gdpa(a.dev, "vkDestroyImage"))(a.dev, cur[e], NULL);
            gone[e] = cur[e];
        }
        cur[0] = app_image(&a, ES_EW + 40, ES_EH + 40, FERAL_USAGE), cur[1] = app_image(&a, ES_EW + 40, ES_EH + 40, FERAL_USAGE);
        ew = ES_EW + 40, eh = ES_EH + 40;
        eyes_info(gen + 1, ew, eh, cur[0], cur[1]);
        allon_stereo(&a, f++, gen, 2, 0, 0, 0, &v); /* the old generation's tag: shown mono (the game image) */
        frames++;
        int from_gone = 0;
        for (int c = 0; c < V.ncp; c++) from_gone += V.cp[c].s == gone[0] || V.cp[c].s == gone[1];
        CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_QUAD && !from_gone &&
                  log_has(cycle ? "the tag's 3" : "the tag's 2"),
              "the old generation's frame: type %d, %d copies, %d from the destroyed eye images",
              MOCK(mockxr_last_end)()->type, V.ncp, from_gone);
        bad += !comp_kinds(kinds, "old generation");
        gen++;
        hand_set(1, &R);
    }
    CHECK(!bad && maxl == 7, "%d frames out of order; at most %d layers in a frame (want all 7 at once)", bad, maxl);
    for (int hh = 0; hh < 2; hh++) {
        const mockxr_haptic *hp = MOCK(mockxr_haptic_get)(hh);
        CHECK(hp->applies - hap0[hh] == hap_posts[hh] && hp->stops == 0 && fabsf(hp->amplitude - hap_amp[hh]) < 1e-6f,
              "the bow's and REACT's haptics, hand %d: %d applies for %d posts (stops %d), last amplitude %g (want %g)", hh,
              hp->applies - hap0[hh], hap_posts[hh], hp->stops, hp->amplitude, hap_amp[hh]);
    }
    CHECK(tot[AK_RING] > 50 && tot[AK_HUD] > 100 && tot[AK_PTR] > 50 && tot[AK_BLINK] >= 10 && tot[AK_FADE] >= 50 &&
              reuse == 2 && stereo > 250,
          "parts seen: ring %d HUD %d pointer %d blink %d fade %d, reuse %d stereo %d", tot[AK_RING], tot[AK_HUD],
          tot[AK_PTR], tot[AK_BLINK], tot[AK_FADE], reuse, stereo);
    CHECK(log_count("xr: hud quad placed for frame") == 2 && !log_has("off for this session") && !log_has("hud off") &&
              !log_has("not created by this device"),
          "the HUD placed %d times (want 2: the recentre); a part went off", log_count("xr: hud quad placed for frame"));
    fprintf(stderr, "    allon: %d frames (%d stereo, %d reuse, %d mono), at most %d layers; ring %d HUD %d pointer %d blink %d fade "
           "%d; the bow's and REACT's haptics %d+%d applied\n", frames, stereo, reuse, mono, maxl, tot[AK_RING], tot[AK_HUD],
           tot[AK_PTR], tot[AK_BLINK], tot[AK_FADE], hap_posts[0], hap_posts[1]);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && !MOCK(mockxr_view_live)(), "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
/* MOCKXR_REJECT_SC=1920x1080: the runtime never takes the HUD quad. Only the HUD goes off; the fade, the blink and the
 * ring of the same frames stay (before: each was dropped in turn and went off for the session first) */
static int sc_reject_hud_only(void) {
    hudrig r;
    if (comfort_obj(1, 0.8f, 90) || hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    int kinds[AK_N];
    allon_stereo(&r.a, 1, 2, 2, 128, 3.0f, 0.5f, &v);
    CHECK(MOCK(mockxr_rejects)() >= 1 && log_count("xr: hud off for this session: xrEndFrame rejected the frame with the HUD "
                                                     "quad: XR_ERROR_LAYER_INVALID; ended again without it") == 1,
          "the HUD's rejection: %d rejects, no hud-off line", MOCK(mockxr_rejects)());
    comp_kinds(kinds, "the rejected frame, ended again");
    CHECK(!kinds[AK_HUD] && kinds[AK_FADE] == 1 && kinds[AK_BLINK] == 1, "the rejected frame ended with HUD %d fade %d blink %d",
          kinds[AK_HUD], kinds[AK_FADE], kinds[AK_BLINK]);
    for (uint64_t f = 2; f <= 12; f++) allon_stereo(&r.a, f, 2, 2, 200, 3.0f, 0.5f, &v);
    comp_kinds(kinds, "after the HUD went off");
    CHECK(!kinds[AK_HUD] && kinds[AK_RING] == 1 && kinds[AK_FADE] == 1 && kinds[AK_BLINK] == 1 &&
              MOCK(mockxr_last_end)()->layer_count == 4 && !log_has("fade off") && !log_has("comfort: off"),
          "later frames: HUD %d ring %d fade %d blink %d (%u layers); fade/comfort turned off with the HUD", kinds[AK_HUD],
          kinds[AK_RING], kinds[AK_FADE], kinds[AK_BLINK], MOCK(mockxr_last_end)()->layer_count);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* ---- SoM loading env (docs/re-xr-pipeline.md 14, ../somvr_env.h): the sky behind mono frames, the switches through
 * the fade quad, world mode's kept frame. ../somvr_env.c is linked here: the image, the faces, the dim quads and the
 * transition the layer must show are computed with the same code, independently of the layer's use of it. ---------- */
#define EQ_EXT XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME
static int env_sky_slot(int kind) { return MOCK(mockxr_sc_find)(somvr_env_img_w(kind), somvr_env_img_h(kind)); }
static uint32_t env_nsky(int kind) { return kind == SOMVR_ENV_KIND_EQ ? 1u : 6u; }
static void pose7(const somvr_posef *p, float out[7]) {
    memcpy(out, p->q, 4 * sizeof(float));
    memcpy(out + 4, p->p, 3 * sizeof(float));
}

/* the last frame's sky: layer 0 an equirect2 layer (the whole map, 30 m, at the LOCAL origin), or layers 0..5 the
 * box's faces (each its tile's inner rect, its size and pose from somvr_env_face_get), all from the sky swapchain */
static void check_env_sky(int kind, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int k = env_sky_slot(kind);
    int32_t rc[4];
    if (kind == SOMVR_ENV_KIND_EQ) {
        static const float id[7] = {0, 0, 0, 1, 0, 0, 0};
        somvr_env_sky_rect(kind, 0, rc);
        CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR && k >= 1 && e->eq_sc == k &&
                  e->eq_radius == SOMVR_ENV_RADIUS_M && e->eq_angle[0] == 6.2831850f && e->eq_angle[1] == 1.5707962f &&
                  e->eq_angle[2] == -1.5707962f && rect_is(e->eq_rect, 0, 0, SOMVR_ENV_EQ_W, SOMVR_ENV_EQ_H) &&
                  rect_is(rc, 0, 0, SOMVR_ENV_EQ_W, SOMVR_ENV_EQ_H) && e->eq_flags == 0 && pose_near(e->eq_pose, id),
              "%s: layer 0 type %d sc %d/%d radius %g angles %g %g %g rect %d,%d %dx%d flags 0x%llx", what, e->type,
              e->eq_sc, k, e->eq_radius, e->eq_angle[0], e->eq_angle[1], e->eq_angle[2], e->eq_rect[0], e->eq_rect[1],
              e->eq_rect[2], e->eq_rect[3], (unsigned long long)e->eq_flags);
        return;
    }
    for (int i = 0; i < 6; i++) {
        somvr_env_face fc;
        somvr_env_face_get(i, &fc);
        somvr_env_sky_rect(kind, i, rc);
        float pose[7];
        pose7(&fc.pose, pose);
        if (i == 0) {
            CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->flags == 0 && !e->same_swapchain &&
                      rect_is(e->rect[0], rc[0], rc[1], rc[2], rc[3]) && e->size[0] == 2 * fc.hw &&
                      e->size[1] == 2 * fc.hh && e->pos[0] == fc.c[0] && e->pos[1] == fc.c[1] && e->pos[2] == fc.c[2],
                  "%s: face 0: type %d rect %d,%d %dx%d size %g x %g at %g,%g,%g", what, e->type, e->rect[0][0],
                  e->rect[0][1], e->rect[0][2], e->rect[0][3], e->size[0], e->size[1], e->pos[0], e->pos[1], e->pos[2]);
        } else {
            CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && k >= 1 && e->q[i].sc == k && e->q[i].space == 1 &&
                      e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && e->q[i].flags == 0 &&
                      rect_is(e->q[i].rect, rc[0], rc[1], rc[2], rc[3]) && e->q[i].size[0] == 2 * fc.hw &&
                      e->q[i].size[1] == 2 * fc.hh && pose_near(e->q[i].pose, pose),
                  "%s: face %d: type %d sc %d/%d rect %d,%d %dx%d size %g x %g at %g,%g,%g", what, i, e->q[i].type,
                  e->q[i].sc, k, e->q[i].rect[0], e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3], e->q[i].size[0],
                  e->q[i].size[1], e->q[i].pose[4], e->q[i].pose[5], e->q[i].pose[6]);
        }
    }
}
/* layer i of the last frame is the menu quad: the game image (1920x1080) of swapchain slot sc, 2 m ahead */
static void check_env_menu(uint32_t i, int sc, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    if (i < 1 || i >= 16) {
        CHECK(0, "%s: the menu quad as layer %u", what, i);
        return;
    }
    CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && sc >= 0 && e->q[i].sc == sc && e->q[i].space == 1 &&
              e->q[i].flags == 0 && e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && rect_is(e->q[i].rect, 0, 0, 1920, 1080) &&
              e->q[i].size[0] == 2.0f && fabsf(e->q[i].size[1] - 1.125f) < 1e-5f && e->q[i].pose[6] == -2.0f &&
              e->q[i].pose[3] == 1.0f,
          "%s: the menu quad (layer %u): type %d sc %d/%d rect %d,%d %dx%d size %g x %g z %g", what, i, e->q[i].type,
          e->q[i].sc, sc, e->q[i].rect[0], e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3], e->q[i].size[0],
          e->q[i].size[1], e->q[i].pose[6]);
}
/* the last upload is the environment's image byte for byte (somvr_env_image: kind, byte order, encoding, dim) */
static int env_image_is(int kind, int bgra, int srgb, float dim) {
    uint32_t w = somvr_env_img_w(kind), h = somvr_env_img_h(kind);
    uint8_t *px = malloc((size_t)w * h * 4);
    if (!px) return 0;
    somvr_env_image(px, kind, bgra, srgb, dim);
    int ok = V.up_w == w && V.up_h == h && !memcmp(V.up_px, px, (size_t)w * h * 4);
    free(px);
    return ok;
}

/* no preload (as `quad`): five mono frames, each the sky and then the menu quad; one static sky image filled once */
static int env_mono(int kind, int bgra, const char *logline) {
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(MOCK(mockxr_ext_enabled)(EQ_EXT) == (kind == SOMVR_ENV_KIND_EQ), "%s enabled: %d", EQ_EXT,
          MOCK(mockxr_ext_enabled)(EQ_EXT));
    uint32_t ns = env_nsky(kind);
    for (int f = 0; f < 5; f++) {
        char what[32];
        snprintf(what, sizeof what, "mono frame %d", f);
        app_present(&a);
        const mockxr_end *e = MOCK(mockxr_last_end)();
        CHECK(e->layer_count == ns + 1 && V.copies == f + 1 && e->pj_index == 0, "%s: %u layers (want %u), %d copies",
              what, e->layer_count, ns + 1, V.copies);
        check_env_sky(kind, what);
        check_env_menu(ns, 0, what);
    }
    int k = env_sky_slot(kind);
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_k)(k);
    int64_t fmt = bgra ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_R8G8B8A8_SRGB;
    CHECK(k == 1 && s->format == fmt && (s->create_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) && s->acquires == 1 &&
              s->usage == (XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
              V.buf2img == 1 && V.up_img == (VkImage)(uintptr_t)(0x5000 + 0x100 * k) &&
              env_image_is(kind, bgra, 1, SOMVR_ENV_DIM_DEFAULT),
          "the sky: slot %d format %lld flags 0x%x acquires %d; %d uploads into %p %ux%u (or not the image's bytes)", k,
          (long long)s->format, s->create_flags, s->acquires, V.buf2img, (void *)V.up_img, V.up_w, V.up_h);
    CHECK(V.buffers_live == 0 && V.mems_live == 0 && MOCK(mockxr_sc_count)() == 2,
          "the upload's buffer/memory left (%d/%d); %d swapchains", V.buffers_live, V.mems_live,
          MOCK(mockxr_sc_count)());
    app_teardown(&a);
    char sky[256];
    snprintf(sky, sizeof sky, "xr: env: sky image %ux%u format %lld (a static swapchain, 1 image(s)) filled once: %s, "
             "dim cell alpha 153; centre +0.00 m above the LOCAL origin", somvr_env_img_w(kind), somvr_env_img_h(kind),
             (long long)fmt, kind == SOMVR_ENV_KIND_EQ ? "an equirect map 1024x512" : "6 cube tiles 256x256");
    CHECK(log_has(logline) && log_has(sky) && log_has("SOMVR_XR_FADE off or no stereo: cuts") &&
              log_has(kind == SOMVR_ENV_KIND_EQ ? "xr: env frame 1: 1 layer(s) behind the menu quad (the equirect)"
                                                : "xr: env frame 1: 6 layer(s) behind the menu quad (6 quads)"),
          "log lacks the env lines (\"%s\", \"%s\")", logline, sky);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_env_equirect(void) {
    return env_mono(SOMVR_ENV_KIND_EQ, 0, "xr: env: on: the Mordor-dusk sky behind mono frames as an "
                                          "XR_KHR_composition_layer_equirect2 layer (radius 30 m); switches");
}
static int sc_env_cube(void) { /* MOCKXR_FORMATS=50: the sky in B8G8R8A8_SRGB */
    return env_mono(SOMVR_ENV_KIND_CUBE, 1,
                    "xr: env: on: the Mordor-dusk sky behind mono frames as 6 world-locked quads (the runtime has no "
                    "XR_KHR_composition_layer_equirect2: a 60 m box, its floor 1.6 m below the centre)");
}

/* the frames of a session with a preload: menus (untagged presents: mono) and stereo frames from records, each checked
 * against the test's own transition (somvr_env_step on the frame's display time) */
typedef struct {
    app_t *a;
    uint32_t layout;    /* SOMVR_LAYOUT_A, or _B with the eye images L, R (1832x1920) */
    int kind, mode;     /* SOMVR_ENV_KIND_*, SOMVR_ENV_ON / _WORLD */
    float dim;          /* SOMVR_XR_ENV_DIM */
    VkImage L, R;
    uint64_t f;         /* the next record's frame id */
    somvr_env_trans tr; /* the transition expected */
    somvr_views last;   /* the last stereo frame drawn */
    int have_last, kept; /* kept: world mode's copy of it is behind the menus (layout B) */
    int holds, kept_n;
} envrig;
static const uint8_t ENV_LPX[4] = {200, 10, 20, 255}, ENV_RPX[4] = {10, 200, 20, 255};
/* the mock's xrLocateViews at display time t (mock_xr.c): the eyes the layer locates for the fade of mono and hold
 * frames */
static void mock_head(int64_t t, somvr_views *v) {
    memset(v, 0, sizeof *v);
    float a = (float)(t % 1000000007) * 1e-9f;
    for (int k = 0; k < 2; k++) {
        v->pose[k].q[1] = sinf(0.25f * a), v->pose[k].q[3] = cosf(0.25f * a);
        v->pose[k].p[0] = k ? 0.032f : -0.032f, v->pose[k].p[1] = 1.6f + 0.01f * a, v->pose[k].p[2] = -0.1f;
    }
}
static int env_mono_sc(const envrig *g) { /* the menu's swapchain: the game image (layout B: 2 x its width) */
    return MOCK(mockxr_sc_find)(g->layout == SOMVR_LAYOUT_B ? 3840 : 1920, 1080);
}
static const int32_t (*env_rect(const envrig *g))[4] { return g->layout == SOMVR_LAYOUT_B ? RECT_B : RECT_A; }
/* the environment's layers of the last frame: the sky, then (a kept frame) its projection with the last stereo frame's
 * views and the two dim quads over exactly them; returns how many */
static uint32_t check_env_scene(const envrig *g, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    uint32_t n = env_nsky(g->kind);
    check_env_sky(g->kind, what);
    if (!g->kept) {
        CHECK(e->pj_index == 0, "%s: a projection after layer 0 (layer %d)", what, e->pj_index);
        return n;
    }
    int kw = MOCK(mockxr_sc_find)(2 * 1832, 1920);
    CHECK(e->pj_index == (int)n && kw >= 0 && e->pj_sc[0] == kw && e->pj_sc[1] == kw && e->pj_flags == 0,
          "%s: the kept frame: layer %d (want %u), swapchains %d/%d (want %d)", what, e->pj_index, n, e->pj_sc[0],
          e->pj_sc[1], kw);
    for (int v = 0; v < 2; v++) {
        float pose[7], fov[4] = {g->last.fov[v].l, g->last.fov[v].r, g->last.fov[v].u, g->last.fov[v].d};
        pose7(&g->last.pose[v], pose);
        CHECK(!memcmp(e->pj_pose[v], pose, sizeof pose) && !memcmp(e->pj_fov[v], fov, sizeof fov) &&
                  !memcmp(e->pj_rect[v], RECT_B[v], sizeof RECT_B[v]),
              "%s: the kept frame's view %d is not the last stereo frame's (rect %d,%d %dx%d)", what, v,
              e->pj_rect[v][0], e->pj_rect[v][1], e->pj_rect[v][2], e->pj_rect[v][3]);
    }
    n++;
    if (g->dim >= 1.0f) return n;
    int ks = env_sky_slot(g->kind);
    int32_t rc[4];
    somvr_env_dim_rect(g->kind, rc);
    for (int v = 0; v < 2; v++, n++) {
        somvr_posef qp;
        float sz[2], pose[7];
        somvr_env_dim_quad(&g->last.pose[v], &g->last.fov[v], SOMVR_ENV_DIM_DIST_M, SOMVR_ENV_DIM_MARGIN, &qp, sz);
        pose7(&qp, pose);
        CHECK(e->q[n].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[n].sc == ks && e->q[n].space == 1 &&
                  e->q[n].eyes == (v ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT) &&
                  e->q[n].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT &&
                  rect_is(e->q[n].rect, rc[0], rc[1], rc[2], rc[3]) && e->q[n].size[0] == sz[0] &&
                  e->q[n].size[1] == sz[1] && pose_near(e->q[n].pose, pose),
              "%s: dim quad %d (layer %u): sc %d/%d eyes %d flags 0x%llx rect %d,%d %dx%d size %g x %g (want %g x %g)",
              what, v, n, e->q[n].sc, ks, e->q[n].eyes, (unsigned long long)e->q[n].flags, e->q[n].rect[0],
              e->q[n].rect[1], e->q[n].rect[2], e->q[n].rect[3], e->q[n].size[0], e->q[n].size[1], sz[0], sz[1]);
    }
    return n;
}
/* world mode's switch to the menus (layout B): the eye images copied side by side into a new static swapchain, once
 * (its barriers are in a command buffer of its own, before the menu's copy: the fake driver chain-checks them there) */
static void check_env_kept_copy(const envrig *g, const char *what) {
    int kw = MOCK(mockxr_sc_find)(2 * 1832, 1920);
    const mockxr_swapchain *s = MOCK(mockxr_swapchain_k)(kw);
    VkImage wi = (VkImage)(uintptr_t)(0x5000 + 0x100 * kw);
    CHECK(kw >= 0 && (s->create_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) && s->acquires == 1 &&
              s->format == VK_FORMAT_R8G8B8A8_SRGB && V.ncp >= 2 && V.cp[0].s == g->L && V.cp[0].d == wi &&
              V.cp[0].r.dstOffset.x == 0 && V.cp[0].r.extent.width == 1832 && V.cp[0].r.extent.height == 1920 &&
              V.cp[1].s == g->R && V.cp[1].d == wi && V.cp[1].r.dstOffset.x == 1832 &&
              !memcmp(img_px(wi, 0), ENV_LPX, 4) && !memcmp(img_px(wi, 1832), ENV_RPX, 4),
          "%s: the kept copy: slot %d flags 0x%x acquires %d format %lld; %d copies, the first %p -> %p", what, kw,
          kw >= 0 ? s->create_flags : 0, kw >= 0 ? s->acquires : 0, kw >= 0 ? (long long)s->format : 0LL, V.ncp,
          (void *)V.cp[0].s, (void *)V.cp[0].d);
}
/* n presents of one side: SOMVR_ENV_SHOW_ENV = untagged (mono), _WORLD = a stereo frame from a record. A hold frame
 * shows the shown side's last layers again (no copy, no submit, no acquire, the present waits the app's semaphore);
 * a drawn one its own; the fade quad is the last layer whenever the expected black has a ramp level */
static void env_frames(envrig *g, int want, int n, const char *what) {
    for (int i = 0; i < n; i++) {
        char w[96];
        snprintf(w, sizeof w, "%s, frame %d", what, i);
        somvr_views v, head;
        int acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage"), ends = MOCK(mockxr_calls)("xrEndFrame");
        int sub = V.submits, cps = V.copies;
        V.ncp = V.ntr = 0;
        if (want == SOMVR_ENV_SHOW_ENV) {
            app_present(g->a);
        } else {
            CHECK(locate(&v), "%s: locate", w);
            write_record(g->f, g->layout, &v, 3);
            if (g->layout == SOMVR_LAYOUT_B) tagged_present_eyes(g->a, g->f, 1, TAG_SYNC, 3, 2);
            else tagged_present(g->a, g->f, 0, TAG_SYNC);
            g->f++;
        }
        const mockxr_end *e = MOCK(mockxr_last_end)();
        int sw = 0, act = somvr_env_step(&g->tr, want, 1, e->time, &sw);
        int k = somvr_fade_index(somvr_env_level(&g->tr));
        CHECK(MOCK(mockxr_calls)("xrEndFrame") == ends + 1, "%s: %d xrEndFrame calls", w,
              MOCK(mockxr_calls)("xrEndFrame") - ends);
        mock_head(e->time, &head);
        uint32_t nl = 0;
        const somvr_views *fv = &head;
        if (act == SOMVR_ENVT_HOLD) {
            g->holds++;
            CHECK(V.ncp == 0 && V.copies == cps && V.submits == sub &&
                      MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq && V.present_nwait == 1 &&
                      V.present_wait[0] == APP_SEM(g_frame - 1),
                  "%s: a hold frame copied (%d), submitted (%d), acquired (%d) or did not present with the app's "
                  "semaphore", w, V.copies - cps, V.submits - sub, MOCK(mockxr_calls)("xrAcquireSwapchainImage") - acq);
            if (g->tr.shown == SOMVR_ENV_SHOW_ENV) {
                nl = check_env_scene(g, w);
                check_env_menu(nl++, env_mono_sc(g), w);
            } else {
                check_proj_n(&g->last, 3, env_rect(g), w, 1u + (k != 0));
                nl = 1;
            }
        } else if (want == SOMVR_ENV_SHOW_ENV) {
            if (sw && g->tr.switches && g->mode == SOMVR_ENV_WORLD && g->layout == SOMVR_LAYOUT_B && g->have_last) {
                g->kept = 1, g->kept_n++;
                check_env_kept_copy(g, w);
            } else {
                int kw = MOCK(mockxr_sc_find)(2 * 1832, 1920);
                for (int c = 0; c < V.ncp && kw >= 0; c++)
                    CHECK(V.cp[c].d != (VkImage)(uintptr_t)(0x5000 + 0x100 * kw), "%s: the kept frame copied again", w);
            }
            CHECK(V.copies > cps, "%s: the menu not copied", w);
            nl = check_env_scene(g, w);
            check_env_menu(nl++, env_mono_sc(g), w);
        } else {
            if (sw) g->kept = 0;
            check_proj_n(&v, 3, env_rect(g), w, 1u + (k != 0));
            nl = 1;
            g->last = v, g->have_last = 1;
            fv = &v; /* a drawn stereo frame's fade: at its record's eyes */
        }
        CHECK(e->layer_count == nl + (k != 0), "%s: %u layers, want %u + the fade %d", w, e->layer_count, nl, k);
        if (k) check_fade_layer(nl, k, fv, w);
    }
}

/* SOMVR_XR_ENV=on, the box (no equirect2), layout A: the menus fade in from the session's black; to the world: 17 hold
 * frames of the menu darkening, the world from black; back the same way; a stray stereo frame only dips */
static int sc_env_stereo(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_A, .kind = SOMVR_ENV_KIND_CUBE, .mode = SOMVR_ENV_ON,
                .dim = SOMVR_ENV_DIM_DEFAULT, .f = 1};
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 30, "menus");
    CHECK(g.holds == 0 && g.tr.black == 0 && g.tr.switches == 0, "menus: %d holds, black %g", g.holds, g.tr.black);
    env_frames(&g, SOMVR_ENV_SHOW_WORLD, 50, "to the world");
    CHECK(g.holds == 17 && g.tr.switches == 1 && g.tr.black == 0 && g.tr.shown == SOMVR_ENV_SHOW_WORLD,
          "to the world: %d holds, %llu switches, black %g", g.holds, (unsigned long long)g.tr.switches, g.tr.black);
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 50, "to the menus");
    CHECK(g.holds == 34 && g.tr.switches == 2 && g.tr.black == 0, "to the menus: %d holds", g.holds);
    env_frames(&g, SOMVR_ENV_SHOW_WORLD, 1, "a stray stereo frame");
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 5, "the menus again");
    CHECK(g.holds == 35 && g.tr.switches == 2 && g.tr.black == 0, "a stray frame: %d holds, %llu switches", g.holds,
          (unsigned long long)g.tr.switches);
    CHECK(MOCK(mockxr_sc_count)() == 3 && MOCK(mockxr_swapchain_k)(env_sky_slot(SOMVR_ENV_KIND_CUBE))->acquires == 1,
          "%d swapchains (the game's, the sky's, the fade's); the sky acquired again", MOCK(mockxr_sc_count)());
    app_teardown(&a);
    CHECK(log_has("xr: env: environment -> world after 17 hold frame(s) (switch 1)") &&
              log_has("xr: env: world -> environment after 17 hold frame(s) (switch 2)") &&
              log_has("switches through the fade quad (out 200 ms, in 300 ms)") && !log_has("cuts"),
          "log lacks the switch lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* SOMVR_XR_ENV=world SOMVR_XR_ENV_DIM=0.25, the equirect (offered with another extension), layout B: the last stereo
 * frame copied once at the switch, shown behind the menus with its own views under two dim quads, freed at the next
 * stereo frame; a new copy at the next switch */
static int sc_env_world(void) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(MOCK(mockxr_ext_enabled)(EQ_EXT) && !MOCK(mockxr_ext_enabled)("XR_EXT_hand_tracking"),
          "the instance's extensions: equirect2 %d, hand tracking %d", MOCK(mockxr_ext_enabled)(EQ_EXT),
          MOCK(mockxr_ext_enabled)("XR_EXT_hand_tracking"));
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_B, .kind = SOMVR_ENV_KIND_EQ, .mode = SOMVR_ENV_WORLD, .dim = 0.25f,
                .f = 1};
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 30, "menus");
    atomic_store(&g_sh.eyes.expect_w, 1832);
    atomic_store(&g_sh.eyes.expect_h, 1920);
    g.L = app_image(&a, 1832, 1920, FERAL_USAGE), g.R = app_image(&a, 1832, 1920, FERAL_USAGE);
    int il = img_ix(g.L), ir = img_ix(g.R);
    if (il >= 0) memcpy(g_img[il].fill, ENV_LPX, 4);
    if (ir >= 0) memcpy(g_img[ir].fill, ENV_RPX, 4);
    eyes_info(2, 1832, 1920, g.L, g.R);
    env_frames(&g, SOMVR_ENV_SHOW_WORLD, 50, "to the world");
    CHECK(g.holds == 17 && g.kept_n == 0 && MOCK(mockxr_sc_count)() == 3, "to the world: %d holds, %d swapchains",
          g.holds, MOCK(mockxr_sc_count)());
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 50, "to the menus, the world kept");
    CHECK(g.holds == 34 && g.kept_n == 1 && g.kept && MOCK(mockxr_sc_count)() == 4,
          "to the menus: %d holds, %d kept, %d swapchains (the menu's, the sky's, the fade's, the kept frame's)",
          g.holds, g.kept_n, MOCK(mockxr_sc_count)());
    env_frames(&g, SOMVR_ENV_SHOW_WORLD, 50, "back to the world");
    CHECK(g.holds == 51 && !g.kept && MOCK(mockxr_sc_count)() == 3 && MOCK(mockxr_sc_find)(2 * 1832, 1920) == 0,
          "back: %d holds, %d swapchains (the kept frame's not freed?)", g.holds, MOCK(mockxr_sc_count)());
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 25, "the menus again, a new copy");
    CHECK(g.kept_n == 2 && g.tr.switches == 4, "%d kept, %llu switches", g.kept_n, (unsigned long long)g.tr.switches);
    app_teardown(&a);
    CHECK(log_has("xr: env: world: the Mordor-dusk sky behind mono frames as an XR_KHR_composition_layer_equirect2 "
                  "layer (radius 30 m); world: the last stereo frame kept behind them at 25 %") &&
              log_has("dim cell alpha 191") && log_count("xr: env: world kept: the eye images") == 2 &&
              log_has("copied into a 3664x1920 swapchain (static, format 43), at 25 % behind mono frames until the "
                      "next stereo frame (2 kept so far)"),
          "log lacks the world lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* SOMVR_XR_ENV=world, layout A (the stereo frames are not from the eye images): the sky alone, logged once */
static int sc_env_world_a(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_A, .kind = SOMVR_ENV_KIND_EQ, .mode = SOMVR_ENV_WORLD,
                .dim = SOMVR_ENV_DIM_DEFAULT, .f = 1};
    for (int r = 0; r < 2; r++) { /* 50 frames: 17 holds, the switch, faded in after 27 more */
        env_frames(&g, SOMVR_ENV_SHOW_ENV, 50, "menus");
        env_frames(&g, SOMVR_ENV_SHOW_WORLD, 50, "the world");
    }
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 50, "menus");
    CHECK(g.kept_n == 0 && g.tr.switches == 4 && g.holds == 4 * 17 && MOCK(mockxr_sc_count)() == 3,
          "%d holds, %llu switches, %d swapchains", g.holds, (unsigned long long)g.tr.switches,
          MOCK(mockxr_sc_count)());
    app_teardown(&a);
    CHECK(log_count("xr: env: world: the last stereo frame did not come from the eye images (layout A, or none yet): "
                    "the sky alone (logged once)") == 1 &&
              !log_has("xr: env: world kept") && !log_has("world off"),
          "log: the layout-A line not once, or a kept frame");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* the environment goes off for the session at its first frame (rejected, a failed upload): every frame is the menu
 * quad alone, its swapchain is gone, XR goes on */
static int env_gone(int ends_extra, const char *logline) {
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int f = 0; f < 4; f++) {
        app_present(&a);
        const mockxr_end *e = MOCK(mockxr_last_end)();
        CHECK(e->layer_count == 1 && e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->same_swapchain &&
                  e->rect[0][2] == 1920 && V.copies == f + 1,
              "frame %d: %u layers, layer 0 type %d (same swapchain %d), %d copies", f, e->layer_count, e->type,
              e->same_swapchain, V.copies);
    }
    CHECK(MOCK(mockxr_calls)("xrEndFrame") == 4 + ends_extra && MOCK(mockxr_sc_count)() == 1 &&
              V.buffers_live == 0 && V.mems_live == 0,
          "%d xrEndFrame calls (want %d), %d swapchains, buffers/memory %d/%d", MOCK(mockxr_calls)("xrEndFrame"),
          4 + ends_extra, MOCK(mockxr_sc_count)(), V.buffers_live, V.mems_live);
    app_teardown(&a);
    CHECK(log_count(logline) == 1, "log lacks \"%s\" (or has it twice)", logline);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_env_reject(void) { /* MOCKXR_REJECT_ENV=1: the first frame with the six faces: ended again without them */
    return env_gone(1, "xr: env: off for this session: xrEndFrame rejected the frame with the environment's 6 "
                       "layer(s): XR_ERROR_LAYER_INVALID; ended again without them (mono frames go on in the black "
                       "void; 1 environment frames so far)");
}
static int sc_env_fail(void) { /* no buffer for the sky's upload */
    V.fail_buffer = 1;
    int r = env_gone(0, "xr: env: off for this session: the sky's upload: vkCreateBuffer: -2 (mono frames go on in the "
                        "black void; 0 environment frames so far)");
    CHECK(V.buf2img == 0 && V.buffer_creates == 1, "%d uploads, %d buffer creations", V.buf2img, V.buffer_creates);
    return r;
}

/* the environment off (the runtime offers equirect2): not enabled, nothing made; mono, stereo, mono as before */
static int env_none(const char *logline, const char *logline2) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    app_present(&a);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 1 && e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->same_swapchain, "mono: %u layers",
          e->layer_count);
    somvr_views v;
    CHECK(locate(&v), "locate");
    write_record(1, SOMVR_LAYOUT_A, &v, 3);
    tagged_present(&a, 1, 0, TAG_SYNC);
    check_proj(&v, 3, RECT_A, "stereo at once (no hold frames)");
    app_present(&a);
    CHECK(e->layer_count == 1 && e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->same_swapchain,
          "mono at once: %u layers, type %d", e->layer_count, e->type);
    CHECK(!MOCK(mockxr_ext_enabled)(EQ_EXT) && MOCK(mockxr_sc_count)() == 1 && V.buf2img == 0 &&
              MOCK(mockxr_calls)("xrEndFrame") == 3,
          "equirect2 enabled %d, %d swapchains, %d uploads, %d ends", MOCK(mockxr_ext_enabled)(EQ_EXT),
          MOCK(mockxr_sc_count)(), V.buf2img, MOCK(mockxr_calls)("xrEndFrame"));
    app_teardown(&a);
    CHECK(log_has(logline) && (!logline2 || log_has(logline2)) &&
              log_has("xr: env: off (SOMVR_XR_ENV unset or off: the black void behind mono frames)"),
          "log lacks \"%s\"", logline);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_env_off(void) { return env_none("xr: env: off (SOMVR_XR_ENV unset or off", NULL); }
/* SOMVR_XR_ENV=on (the six faces) under menu frames with the pointer's ray and dot, MOCKXR_REJECT_SC=64x32: the runtime
 * never takes the pointer's quads. The composition is the environment's faces, the menu quad, the pointer (docs/
 * re-xr-pipeline.md 16); each part is dropped alone, so only the pointer goes off and the sky stays (before: the
 * environment's layers were dropped first and went off for the session with the pointer) */
static int sc_env_reject_ptr(void) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR, 1, SOMVR_INPUT_V2_SIZE) || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R, L;
    float away[3] = {-3, 0, -1};
    ptr_aim(&R, 1, 0.5f, 0.5f, 0);
    ptr_aim_at(&L, 0, away, 0);
    hand_set(1, &R), hand_set(0, &L);
    int faces = 0;
    for (int i = 0; i < 12; i++) {
        V.ncp = V.ntr = 0;
        tagged_present(&a, 0, 0, TAG_SYNC); /* a menu present (layout B: frame 0) */
        const mockxr_end *e = MOCK(mockxr_last_end)();
        int ptr = 0, kp = MOCK(mockxr_sc_find)(64, 32); /* the six faces (layers 0-5), the menu quad (6, the game's swapchain), no pointer quad */
        for (uint32_t k = 1; k < e->layer_count && k < MOCK_NQ; k++) ptr |= kp >= 0 && e->q[k].sc == kp;
        faces += e->layer_count >= 7 && e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[6].sc == 0 && !ptr;
    }
    CHECK(MOCK(mockxr_rejects)() >= 1 && log_has("xrEndFrame rejected the frame with the pointer's") &&
              !log_has("env: off for this session") && faces >= 11,
          "rejections %d, pointer off %d, env off %d; %d frames with the six faces under the menu quad",
          MOCK(mockxr_rejects)(), log_has("xrEndFrame rejected the frame with the pointer's"),
          log_has("env: off for this session"), faces);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_env_badcfg(void) { /* SOMVR_XR_ENV=sideways SOMVR_XR_ENV_DIM=2: off, both logged */
    return env_none("xr: env: SOMVR_XR_ENV=sideways is not off, on or world: off",
                    "xr: env: SOMVR_XR_ENV_DIM=2 is not 0..1: 0.40");
}

/* ---- never a void (HG-9, docs/re-xr-pipeline.md 36): the gaps' log, the keep-alive frames, the loading space ------ */
_Static_assert(SOMVR_VOID_IMG_W == 1024 && SOMVR_VOID_IMG_H == 1952, "the loading space's image (its log line below)");
static void void_sleep_ms(int ms) {
    struct timespec sl = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&sl, &sl) && errno == EINTR) {}
}
static void void_stereo(envrig *g) { /* one stereo frame from a record (layout A) */
    somvr_views v;
    CHECK(locate(&v), "void: locate");
    write_record(g->f, SOMVR_LAYOUT_A, &v, 3);
    tagged_present(g->a, g->f, 0, TAG_SYNC);
    g->last = v, g->have_last = 1;
    g->f++;
}
/* the default: the gaps logged (a 350 ms stall: its line, the head sampled), no frames of ours meanwhile, the game's
 * vkQueueSubmit not hooked (the keep-alive is off) */
static int sc_void_gaps(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(L_gdpa(a.dev, "vkQueueSubmit") == (PFN_vkVoidFunction)d_QueueSubmit, "vkQueueSubmit hooked with the keep-alive off");
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_A, .kind = SOMVR_ENV_KIND_CUBE, .mode = SOMVR_ENV_ON,
                .dim = SOMVR_ENV_DIM_DEFAULT, .f = 1};
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 10, "menus");
    int ends = MOCK(mockxr_calls)("xrEndFrame");
    void_sleep_ms(350);
    CHECK(MOCK(mockxr_calls)("xrEndFrame") == ends, "%d frames ended while the game was away (the keep-alive is off)",
          MOCK(mockxr_calls)("xrEndFrame") - ends);
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 3, "menus again");
    app_teardown(&a);
    CHECK(log_has("xr: gaps: game presents 100 ms or more apart are logged (the head sampled every 100 ms meanwhile)") &&
              log_has("xr: keep-alive: off (SOMVR_XR_KEEPALIVE unset or off: a stall sends nothing); loadspace: off") &&
              log_count("xr: gap 1: 3") == 1 && !log_has("xr: gap 2:") &&
              log_has("; M -> M; the environment shown): 0 keep-alive frame(s) meanwhile, the longest time between two "
                      "xrEndFrame calls 3") && log_has("located, yaw") &&
              log_has("xr: gaps (the session's end): 12 game presents in") &&
              log_has("gaps 100..250 ms 0, 250..500 1, 500..1000 0, 1..2 s 0, >= 2 s 0; the longest: 3"),
          "log lacks the gap lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_GAPS=off: no lines, no thread work */
static int sc_void_gaps_off(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 5; i++) app_present(&a);
    void_sleep_ms(250);
    app_present(&a);
    app_teardown(&a);
    CHECK(log_has("xr: gaps: off (SOMVR_XR_GAPS=off)") && !log_has("xr: gap 1:") && !log_has("xr: gaps (") &&
              !log_has("keep-alive (the session's end)"), "log: the gaps not off");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_KEEPALIVE=on in the world (layout A, the equirect): a 600 ms stall = frames of ours at the runtime's pace,
 * each the sky, then the last stereo frame's projection (its own views); none acquires or submits anything; the game's
 * queue calls are hooked; the next stereo frames go on as before (no transition: the world was shown) */
static int sc_void_keep_world(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    PFN_vkQueueSubmit qs = (PFN_vkQueueSubmit)L_gdpa(a.dev, "vkQueueSubmit");
    CHECK(qs && qs != d_QueueSubmit, "the game's vkQueueSubmit not hooked with the keep-alive on");
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_A, .kind = SOMVR_ENV_KIND_EQ, .mode = SOMVR_ENV_ON,
                .dim = SOMVR_ENV_DIM_DEFAULT, .f = 1};
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 30, "menus");
    env_frames(&g, SOMVR_ENV_SHOW_WORLD, 50, "to the world");
    int sub = V.submits, before = V.submits;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    CHECK(qs(a.q, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS && V.submits == sub + 1, "the hooked vkQueueSubmit does not reach "
          "the driver");
    sub = V.submits;
    int ends = MOCK(mockxr_calls)("xrEndFrame"), acq = MOCK(mockxr_calls)("xrAcquireSwapchainImage");
    void_sleep_ms(600);
    int ka = MOCK(mockxr_calls)("xrEndFrame") - ends;
    CHECK(ka >= 20 && ka <= 60, "%d frames of ours in 600 ms (want about 45: from 100 ms on, every 11 ms)", ka);
    CHECK(MOCK(mockxr_calls)("xrAcquireSwapchainImage") == acq && V.submits == sub, "the keep-alive acquired (%d) or "
          "submitted (%d)", MOCK(mockxr_calls)("xrAcquireSwapchainImage") - acq, V.submits - sub);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    check_env_sky(SOMVR_ENV_KIND_EQ, "a keep-alive frame");
    CHECK(e->layer_count == 2 && e->pj_index == 1 && e->pj_sc[0] == 0 && e->pj_sc[1] == 0,
          "a keep-alive frame: %u layers, the projection at %d (sc %d/%d)", e->layer_count, e->pj_index, e->pj_sc[0],
          e->pj_sc[1]);
    for (int v = 0; v < 2; v++) {
        float pose[7];
        pose7(&g.last.pose[v], pose);
        CHECK(!memcmp(e->pj_pose[v], pose, sizeof pose), "a keep-alive frame: view %d is not the last stereo frame's", v);
    }
    env_frames(&g, SOMVR_ENV_SHOW_WORLD, 5, "the world again");
    CHECK(g.holds == 17 && g.tr.switches == 1, "after the stall: %d holds, %llu switches", g.holds,
          (unsigned long long)g.tr.switches);
    (void)before;
    app_teardown(&a);
    CHECK(log_has("xr: keep-alive: on: our own frames when the game is away 100 ms (the game's queue calls under the "
                  "keep-alive's lock); loadspace: off") &&
              log_has("our own frames (the world held over the sky; frame 1 of ours)") &&
              log_count("xr: keep-alive: the game presented again after ") == 1 &&
              log_has("; S -> S; the world shown): ") && log_has("xr: keep-alive (the session's end): 1 stall(s) filled"),
          "log lacks the keep-alive lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
/* the keep-alive on a menu (mono): the sky and the last board again */
static int sc_void_keep_menu(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_A, .kind = SOMVR_ENV_KIND_CUBE, .mode = SOMVR_ENV_ON,
                .dim = SOMVR_ENV_DIM_DEFAULT, .f = 1};
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 30, "menus");
    int ends = MOCK(mockxr_calls)("xrEndFrame"), cps = V.copies;
    void_sleep_ms(400);
    int ka = MOCK(mockxr_calls)("xrEndFrame") - ends;
    CHECK(ka >= 12 && ka <= 45, "%d frames of ours in 400 ms", ka);
    CHECK(V.copies == cps, "the keep-alive copied the menu");
    const mockxr_end *e = MOCK(mockxr_last_end)();
    check_env_sky(SOMVR_ENV_KIND_CUBE, "a keep-alive frame");
    CHECK(e->layer_count == 7, "a keep-alive frame on a menu: %u layers (want the 6 faces and the board)", e->layer_count);
    check_env_menu(6, env_mono_sc(&g), "a keep-alive frame");
    env_frames(&g, SOMVR_ENV_SHOW_ENV, 3, "menus again");
    app_teardown(&a);
    CHECK(log_has("our own frames (the board in the environment; frame 1 of ours)"), "log lacks the menu keep-alive");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_LOADSPACE=on (the equirect): every menu frame = the sky, the floor (our image, uploaded once byte for byte),
 * the board; a 1 s stall in the world: after 200 ms (SOMVR_XR_LOADSPACE_MS) the world is held darkening, then the
 * loading space fades in - the sky, the floor, the progress lights - and its own layer is the top one (no board); the
 * game's stereo frames then hold the space darkening and switch back to the world */
static int sc_void_space(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    envrig g = {.a = &a, .layout = SOMVR_LAYOUT_A, .kind = SOMVR_ENV_KIND_EQ, .mode = SOMVR_ENV_ON,
                .dim = SOMVR_ENV_DIM_DEFAULT, .f = 1};
    for (int i = 0; i < 40; i++) app_present(&a);
    int ks = MOCK(mockxr_sc_find)(SOMVR_VOID_IMG_W, SOMVR_VOID_IMG_H);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int32_t fr[4];
    somvr_void_floor_rect(fr);
    CHECK(ks >= 0 && e->layer_count == 3 && e->q[1].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[1].sc == ks &&
              rect_is(e->q[1].rect, fr[0], fr[1], fr[2], fr[3]) && e->q[1].size[0] == SOMVR_VOID_FLOOR_M &&
              e->q[1].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT && fabsf(e->q[1].pose[5] + 1.6f) < 1e-5f &&
              fabsf(e->q[1].pose[0] + 0.70710678f) < 1e-5f,
          "a menu frame: %u layers; the floor (layer 1): sc %d/%d rect %d,%d %dx%d size %g y %g", e->layer_count,
          e->q[1].sc, ks, e->q[1].rect[0], e->q[1].rect[1], e->q[1].rect[2], e->q[1].rect[3], e->q[1].size[0],
          e->q[1].pose[5]);
    check_env_menu(2, env_mono_sc(&g), "a menu frame");
    const mockxr_swapchain *sw = MOCK(mockxr_swapchain_k)(ks);
    CHECK(ks >= 0 && (sw->create_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) && sw->acquires == 1 &&
              sw->format == VK_FORMAT_R8G8B8A8_SRGB, "the space's image: acquired %d times (the image: test_void)",
          ks >= 0 ? sw->acquires : -1);
    CHECK(V.buffers_live == 0 && V.mems_live == 0, "the upload's buffer/memory left (%d/%d)", V.buffers_live, V.mems_live);
    for (int i = 0; i < 50; i++) void_stereo(&g);
    e = MOCK(mockxr_last_end)();
    CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->layer_count == 1, "the world: %u layers, type %d",
          e->layer_count, e->type);
    int ends = MOCK(mockxr_calls)("xrEndFrame"), up = V.buf2img;
    void_sleep_ms(1000);
    CHECK(MOCK(mockxr_calls)("xrEndFrame") - ends >= 40 && V.buf2img == up, "%d frames of ours in 1 s, %d uploads",
          MOCK(mockxr_calls)("xrEndFrame") - ends, V.buf2img - up);
    e = MOCK(mockxr_last_end)();
    int32_t lr[4];
    check_env_sky(SOMVR_ENV_KIND_EQ, "the loading space");
    CHECK(e->layer_count == 3 && e->q[1].sc == ks && rect_is(e->q[1].rect, fr[0], fr[1], fr[2], fr[3]) &&
              e->q[2].sc == ks && e->q[2].rect[1] >= SOMVR_VOID_FLOOR_PX && e->q[2].size[0] == SOMVR_VOID_LIGHTS_W_M &&
              e->q[2].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT && e->pj_index == 0,
          "the loading space: %u layers (want the sky, the floor, the lights); layer 2 sc %d rect y %d size %g",
          e->layer_count, e->q[2].sc, e->q[2].rect[1], e->q[2].size[0]);
    somvr_void_lights_rect(0, SOMVR_VOID_LEVELS, lr);
    CHECK(e->q[2].rect[2] == lr[2] && e->q[2].rect[3] == lr[3] && e->q[2].rect[1] >= lr[1],
          "the lights at full level after 0.8 s away (rect %d,%d)", e->q[2].rect[0], e->q[2].rect[1]);
    float lp = e->q[2].pose[5];
    CHECK(fabsf(lp - (-1.6f + SOMVR_VOID_LIGHTS_LIFT_M)) < 1e-4f, "the lights %g m up (want %g)", lp,
          -1.6f + SOMVR_VOID_LIGHTS_LIFT_M);
    ends = MOCK(mockxr_calls)("xrEndFrame");
    for (int i = 0; i < 60; i++) void_stereo(&g);
    e = MOCK(mockxr_last_end)();
    CHECK(e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->layer_count == 1 &&
              MOCK(mockxr_calls)("xrEndFrame") - ends == 60, "back in the world: %u layers, type %d", e->layer_count,
          e->type);
    app_teardown(&a);
    CHECK(log_has("loadspace: on: a floor under the sky, the progress lights while the game is away, a stall in the "
                  "world goes to the space") &&
              log_has("xr: loadspace: a stall in the world goes to the space after 200 ms (SOMVR_XR_LOADSPACE_MS)") &&
              log_has("xr: loadspace: image 1024x1952 format 43 (a static swapchain, 1 image(s)) filled once") &&
              log_has("xr: env: world -> the loading space after ") && log_has("(switch 2, the keep-alive: the game away") &&
              log_has("xr: env: environment -> world after ") &&
              log_has(", the loading space shown (stall 1)") && log_has("xr: loadspace: placed: the floor at -1.60 m (1.6 m "
                                                                         "below LOCAL's origin)"),
          "log lacks the loading space's lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_THEATRE=on with the preload's somvr_session: menus on the board (2 m wide 2 m ahead: SOMVR_XR_MENU unset);
 * the session's mono word = a video: the flat frames on a 5 m screen 5 m ahead at eye height, world-locked where the head
 * faced; the cinematic window: 3 m at 3 m, placed again; back to the menu: the board */
static somvr_session_shared *void_session(void) { /* session_obj.so next to the mock, active (the preload's) */
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/session_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *hso = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    somvr_session_shared *ss = hso ? (somvr_session_shared *)dlsym(hso, SOMVR_SESSION_SYMBOL) : NULL;
    if (!ss) return fprintf(stderr, "    session_obj.so: %s\n", dlerror()), NULL;
    ss->magic = SOMVR_SESSION_MAGIC, ss->version = SOMVR_SESSION_VERSION, ss->size = (uint32_t)sizeof *ss;
    ss->pid = (int32_t)getpid();
    atomic_store(&ss->active, 1);
    return ss;
}
static int sc_void_theatre(void) {
    somvr_session_shared *ss = void_session();
    if (!ss || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 5; i++) app_present(&a);
    check_env_menu(1, 0, "a menu");
    atomic_store(&ss->mono, SOMVR_SSM_VIDEO);
    somvr_views hv;
    for (int i = 0; i < 5; i++) app_present(&a);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    mock_head(e->time, &hv);
    int k = 1;
    float dx = e->q[k].pose[4] - 0.5f * (hv.pose[0].p[0] + hv.pose[1].p[0]),
          dz = e->q[k].pose[6] - 0.5f * (hv.pose[0].p[2] + hv.pose[1].p[2]);
    float d = sqrtf(dx * dx + dz * dz);
    CHECK(e->q[k].type == XR_TYPE_COMPOSITION_LAYER_QUAD && fabsf(e->q[k].size[0] - SOMVR_VOID_THEATRE_M) < 1e-4f &&
              fabsf(e->q[k].size[1] - SOMVR_VOID_THEATRE_M * 1080.0f / 1920.0f) < 1e-3f && fabsf(d - 5.0f) < 0.05f &&
              fabsf(e->q[k].pose[5] - 0.5f * (hv.pose[0].p[1] + hv.pose[1].p[1])) < 0.05f,
          "a video: the screen %g x %g m, %.2f m ahead at y %.2f", e->q[k].size[0], e->q[k].size[1], d, e->q[k].pose[5]);
    float vpos[3] = {e->q[k].pose[4], e->q[k].pose[5], e->q[k].pose[6]};
    for (int i = 0; i < 20; i++) app_present(&a); /* the head turns (the mock's): the screen stays */
    e = MOCK(mockxr_last_end)();
    CHECK(e->q[k].pose[4] == vpos[0] && e->q[k].pose[5] == vpos[1] && e->q[k].pose[6] == vpos[2],
          "a video: the screen moved with the head");
    atomic_store(&ss->mono, SOMVR_SSM_WINDOW);
    app_present(&a);
    e = MOCK(mockxr_last_end)();
    CHECK(fabsf(e->q[k].size[0] - SOMVR_VOID_WINDOW_M) < 1e-4f, "the cinematic window: %g m wide", e->q[k].size[0]);
    atomic_store(&ss->mono, SOMVR_SSM_MENU);
    app_present(&a);
    check_env_menu(1, 0, "the menu again");
    app_teardown(&a);
    CHECK(log_has("xr: theatre: on: a video's flat frames on a 5 m screen 5 m ahead") && log_has("(the preload's "
              "somvr_session: found)") && log_has("xr: theatre 1: a video on a 5.0 m screen at ") &&
              log_has("xr: theatre 2: the cinematic window on a 3.0 m screen"),
          "log lacks the theatre lines");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_LOADSPACE with the session's word = a load (on mono frames): the loading screen on a 2.4 m panel 2.4 m ahead
 * over the floor, the progress lights rising through the load (no stall); a menu after it: the board, the lights
 * fading; the theatre switch is not needed for it */
static int sc_void_loadpanel(void) {
    somvr_session_shared *ss = void_session();
    if (!ss || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    for (int i = 0; i < 5; i++) app_present(&a);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 3, "a menu: %u layers (the sky, the floor, the board)", e->layer_count);
    check_env_menu(2, 0, "a menu");
    atomic_store(&ss->mono, SOMVR_SSM_LOAD);
    for (int i = 0; i < 40; i++) app_present(&a); /* 0.44 s of a load */
    e = MOCK(mockxr_last_end)();
    int ks = MOCK(mockxr_sc_find)(SOMVR_VOID_IMG_W, SOMVR_VOID_IMG_H);
    int32_t lr[4];
    somvr_void_lights_rect(0, SOMVR_VOID_LEVELS, lr);
    CHECK(e->layer_count == 4 && e->q[2].sc == ks && e->q[2].size[0] == SOMVR_VOID_LIGHTS_W_M && e->q[2].rect[1] >= lr[1] &&
              e->q[3].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[3].sc == 0 &&
              fabsf(e->q[3].size[0] - SOMVR_VOID_LOAD_M) < 1e-4f,
          "a load: %u layers; the lights (layer 2) sc %d/%d at full level (rect y %d); the screen %g m wide", e->layer_count,
          e->q[2].sc, ks, e->q[2].rect[1], e->q[3].size[0]);
    atomic_store(&ss->mono, SOMVR_SSM_MENU);
    for (int i = 0; i < 30; i++) app_present(&a); /* 0.33 s: the level 0.78 */
    e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 4 && e->q[2].rect[1] < lr[1], "a menu after it: the lights fading (%u layers, rect y %d)",
          e->layer_count, e->q[2].rect[1]);
    check_env_menu(3, 0, "a menu after the load");
    for (int i = 0; i < 130; i++) app_present(&a); /* 1.8 s in all: the lights gone */
    CHECK(MOCK(mockxr_last_end)()->layer_count == 3, "the lights gone: %u layers", MOCK(mockxr_last_end)()->layer_count);
    app_teardown(&a);
    CHECK(log_has("xr: theatre 1: a loading screen on a 2.4 m screen at "), "log lacks the load panel line");
    check_mock_clean();
    return 0;
}
/* bad values: logged, off (the gaps' 100 ms) */
static int sc_void_badcfg(void) {
    preload_on(SOMVR_LAYOUT_A, SOMVR_SHARED_VERSION);
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    CHECK(L_gdpa(a.dev, "vkQueueSubmit") == (PFN_vkVoidFunction)d_QueueSubmit, "hooked with a bad SOMVR_XR_KEEPALIVE");
    for (int i = 0; i < 3; i++) app_present(&a);
    app_teardown(&a);
    CHECK(log_has("xr: gaps: SOMVR_XR_GAPS=3 is not off or 20..60000 (ms): 100") &&
              log_has("xr: keep-alive: SOMVR_XR_KEEPALIVE=maybe is not on or off: off") &&
              log_has("xr: keep-alive: SOMVR_XR_KEEPALIVE_MS=5 is not 50..1000: 100") &&
              log_has("xr: loadspace: SOMVR_XR_LOADSPACE=x is not on or off: off") &&
              log_has("xr: loadspace: SOMVR_XR_LOADSPACE_MS=-1 is not 0..10000: 1000") &&
              log_has("xr: gaps: game presents 100 ms or more apart are logged"),
          "log lacks the bad values");
    check_mock_clean();
    return 0;
}

/* ---- round 2 (docs/re-xr-pipeline.md 36.7, 36.8): the game's in-world loading cover, the in-world menus' backdrop ---- */
/* stereo frames from the eye images with the HUD entry (kind) until ok() holds for the frame's end (max frames);
 * 1 = it did */
static int cover_until(hudrig *r, uint64_t *f, uint32_t kind, int (*ok)(void), int max) {
    somvr_views v;
    for (int i = 0; i < max; i++) {
        hud_stereo(r, ++*f, 2, kind, &v);
        if (ok()) return 1;
    }
    return 0;
}
static int is_world_hud(void) { /* the projection and the HUD quad */
    const mockxr_end *e = MOCK(mockxr_last_end)();
    return e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->layer_count == 2 && e->type2 == XR_TYPE_COMPOSITION_LAYER_QUAD;
}
static int is_world_bare(void) { /* the projection alone */
    const mockxr_end *e = MOCK(mockxr_last_end)();
    return e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->layer_count == 1;
}
static int is_space(void) { /* the sky (equirect), the floor, the lights: no projection */
    const mockxr_end *e = MOCK(mockxr_last_end)();
    return e->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR && e->layer_count == 3 && e->pj_index == 0;
}
static const uint8_t COVER_CLEAR[4] = {0, 0, 0, 0}, COVER_BLACK[4] = {0, 0, 0, 255};
static void qrot_t(const float q[4], const float v[3], float o[3]) { /* v rotated by q (x y z w) */
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float c1[3] = {y * v[2] - z * v[1], z * v[0] - x * v[2], x * v[1] - y * v[0]};
    float c2[3] = {y * c1[2] - z * c1[1], z * c1[0] - x * c1[2], x * c1[1] - y * c1[0]};
    for (int i = 0; i < 3; i++) o[i] = v[i] + 2.0f * (w * c1[i] + c2[i]);
}

/* SOMVR_XR_COVER=on (SOMVR_XR_ENV, SOMVR_XR_LOADSPACE): the HUD image's grid read back every stereo frame; the cover
 * hides the HUD at once, held 300 ms it is a load (the loading space over the game's frames, the readings going on),
 * lifted it is the world again; a menu's black is never a cover */
static int sc_cover(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    int ix = img_ix(r.H);
    memcpy(g_img[ix].fill, COVER_CLEAR, 4);
    uint64_t f = 0;
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 60), "the world with its HUD (frame %llu)",
          (unsigned long long)f);
    int i2b = V.img2buf;
    somvr_views v;
    for (int i = 0; i < 5; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    CHECK(V.img2buf - i2b == 5 && is_world_hud() && !log_has("xr: cover 1:"), "a clear HUD: 5 readings (%d), the HUD shown",
          V.img2buf - i2b);
    /* the cover: hidden at the next frame's poll; a load 300 ms (wall time) later; the space through the fade */
    memcpy(g_img[ix].fill, COVER_BLACK, 4);
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    CHECK(is_world_bare() && log_has("xr: cover 1: the HUD hidden (the grid 1.00 dark, 1.00 opaque black"),
          "the cover's first reading: the world without the HUD (%u layers)", MOCK(mockxr_last_end)()->layer_count);
    void_sleep_ms(350);
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    int ends = MOCK(mockxr_calls)("xrEndFrame");
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_space, 60) && log_has("xr: cover 1: the game's cover held 0.3") &&
              log_has("s (1.00 opaque black): a load - the loading space with the progress lights") &&
              log_has("xr: env: world -> environment after "),
          "the cover's load: the loading space");
    int ks = MOCK(mockxr_sc_find)(SOMVR_VOID_IMG_W, SOMVR_VOID_IMG_H);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(ks >= 0 && e->q[1].sc == ks && e->q[2].sc == ks && e->q[2].size[0] == SOMVR_VOID_LIGHTS_W_M &&
              MOCK(mockxr_calls)("xrEndFrame") - ends >= 10,
          "the space: the floor and the lights (sc %d %d / %d) after the world's hold frames", e->q[1].sc, e->q[2].sc, ks);
    i2b = V.img2buf;
    int cp = V.copies;
    float fl0[7], li0[7], yaw0 = 2 * atan2f(v.pose[0].q[1], v.pose[0].q[3]);
    memcpy(fl0, e->q[1].pose, sizeof fl0), memcpy(li0, e->q[2].pose, sizeof li0);
    for (int i = 0; i < 40; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    int32_t lr[4];
    somvr_void_lights_rect(0, SOMVR_VOID_LEVELS, lr);
    e = MOCK(mockxr_last_end)();
    CHECK(is_space() && V.img2buf - i2b == 40 && V.copies > cp && e->q[2].rect[2] == lr[2] && e->q[2].rect[1] >= lr[1],
          "40 frames in the space: the game's frames still copied and read (%d readings), the lights at full level (rect y %d)",
          V.img2buf - i2b, e->q[2].rect[1]);
    CHECK(log_has("xr: cover: frame 1 of the loading space over the game's stereo frames") &&
              log_has("xr: cover 1: the loading space placed for the load: the head at "), "the space's lines");
    float yaw1 = 2 * atan2f(v.pose[0].q[1], v.pose[0].q[3]);
    CHECK(pose_near(e->q[1].pose, fl0) && pose_near(e->q[2].pose, li0) && fabsf(yaw1 - yaw0) > 1e-3f,
          "the space placed once for the load: the floor and the lights where they were while the head turned %.3f rad "
          "(the game's frames go on under it)", yaw1 - yaw0);
    /* a reuse present (its record missing) in the space: the space, not the world */
    hud_present(&r.a, 999, 2, SOMVR_HUDK_WORLD);
    CHECK(is_space(), "a reuse in the space: the space (%u layers, type %d)", MOCK(mockxr_last_end)()->layer_count,
          MOCK(mockxr_last_end)()->type);
    /* lifted: back to the world through the fade, the HUD again */
    memcpy(g_img[ix].fill, COVER_CLEAR, 4);
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 80) && log_has("xr: cover 1: lifted (0.00 opaque black") &&
              log_has("xr: env: environment -> world after ") && log_has("xr: cover 1: the HUD shown again (0.00 dark)"),
          "lifted: the world and its HUD");
    /* a menu's dark (not pure black) background is never a cover; nor a video's black */
    static const uint8_t MENU_DARK[4] = {20, 20, 20, 255};
    memcpy(g_img[ix].fill, MENU_DARK, 4);
    for (int i = 0; i < 30; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU, &v);
    memcpy(g_img[ix].fill, COVER_BLACK, 4);
    for (int i = 0; i < 30; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_VIDEO, &v);
    CHECK(log_count("the HUD hidden") == 1 && is_world_hud(), "a menu's dark and a video's black: no cover (%d)",
          log_count("the HUD hidden"));
    /* a short black (less than 300 ms): the HUD hidden, no load */
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    memcpy(g_img[ix].fill, COVER_CLEAR, 4);
    for (int i = 0; i < 5; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    CHECK(log_count("the HUD hidden") == 2 && log_count("a load - ") == 1 && is_world_hud(),
          "a short black: hidden once more, no second load");
    /* a load's end into the cover (a mono stretch, then the game's black): the space all through, never the world */
    for (int i = 0; i < 40; i++) tagged_present(&r.a, 0, 0, TAG_SYNC); /* (the world darkens, the environment) */
    CHECK(MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR, "the mono stretch: the environment");
    memcpy(g_img[ix].fill, COVER_BLACK, 4);
    int world = 0, spaced = 0;
    float flm[7];
    memcpy(flm, MOCK(mockxr_last_end)()->q[1].pose, sizeof flm); /* the mono stretch's floor */
    for (int i = 0; i < 12; i++) {
        hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
        world += MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION, spaced += is_space();
    }
    void_sleep_ms(350);
    for (int i = 0; i < 12; i++) {
        hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
        world += MOCK(mockxr_last_end)()->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION, spaced += is_space();
    }
    CHECK(pose_near(MOCK(mockxr_last_end)()->q[1].pose, flm), "the stretch's floor kept through the wait and the load");
    CHECK(!world && spaced >= 20 && log_count("a load - ") == 2 &&
              log_has("xr: cover: the game's first stereo frames after a mono stretch: the game's cover: its load after "),
          "a load's end into the cover: %d world frames (want 0), %d in the space", world, spaced);
    memcpy(g_img[ix].fill, COVER_CLEAR, 4);
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 80), "... lifted: the world and its HUD");
    /* the map's board at a fast travel: the game's pure black on a menu frame is the cover too (hidden, then a load) */
    memcpy(g_img[ix].fill, COVER_BLACK, 4);
    for (int i = 0; i < 3; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU, &v);
    CHECK(is_world_bare() && log_count("the HUD hidden") == 4, "the cover on the map's board: the board hidden (%u layers)",
          MOCK(mockxr_last_end)()->layer_count);
    void_sleep_ms(350);
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU, &v);
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_space, 60) && log_count("a load - ") == 3, "... then its load");
    memcpy(g_img[ix].fill, COVER_CLEAR, 4);
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 80), "... lifted");
    app_teardown(&r.a);
    CHECK(log_has("xr: cover: on: the HUD image's 16 x 9 grid read each stereo frame; the HUD, the band and the wrist "
                  "minimap hidden while most of it is dark; the game's black cover held 300 ms is a load: the loading "
                  "space with the progress lights") &&
              log_has("xr: cover (the session's end): ") && log_has("the HUD hidden 4 time(s)") &&
              log_has("3 load(s) in the space"),
          "the cover's lines");
    CHECK(MOCK(mockxr_live)() == 0 && V.buffers_live == 0 && V.mems_live == 0, "XR objects %d, buffers %d, memory %d left",
          MOCK(mockxr_live)(), V.buffers_live, V.mems_live);
    check_mock_clean();
    return 0;
}

/* SOMVR_XR_COVER unset: no readback, the black HUD shown as before (build 12) */
static int sc_cover_off(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    int ix = img_ix(r.H);
    memcpy(g_img[ix].fill, COVER_BLACK, 4);
    uint64_t f = 0;
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 60), "the world with its HUD");
    void_sleep_ms(350);
    for (int i = 0; i < 20; i++) {
        somvr_views v;
        hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    }
    CHECK(is_world_hud() && V.img2buf == 0 && !log_has("xr: cover"), "the black HUD as before, %d readbacks", V.img2buf);
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}

static int is_board_back(void) { /* the projection, the dim, the panel, the board */
    const mockxr_end *e = MOCK(mockxr_last_end)();
    return e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->layer_count == 4;
}
/* SOMVR_XR_MENU=front SOMVR_XR_MENU_BACK=on: a game menu's board over the world gets the dim and the dark panel under
 * it, rising at the open and falling after the close; the world's HUD none */
static int sc_menu_back(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    uint64_t f = 0;
    somvr_views v;
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 60), "the world with its HUD");
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU, &v); /* the pause menu opens */
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int kb = MOCK(mockxr_sc_find)(SOMVR_BACK_IMG_W, SOMVR_BACK_IMG_H), kf = MOCK(mockxr_sc_find)(SOMVR_FADE_W, SOMVR_FADE_H);
    int32_t rc[4];
    CHECK(is_board_back() && kb >= 0 && kf >= 0 && e->q[1].sc == kf && e->q[2].sc == kb && e->q[3].sc == 1 &&
              e->q[2].rect[0] < SOMVR_BACK_CELL_W * (SOMVR_BACK_LEVELS - 1),
          "the open: %u layers; the dim sc %d/%d, the panel sc %d/%d at cell x %d (a low level), the board sc %d",
          e->layer_count, e->q[1].sc, kf, e->q[2].sc, kb, e->q[2].rect[0], e->q[3].sc);
    for (int i = 0; i < 20; i++) hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU, &v);
    e = MOCK(mockxr_last_end)();
    somvr_back_rect(SOMVR_BACK_LEVELS, rc);
    int32_t fr[4];
    somvr_fade_rect(somvr_fade_index((uint32_t)lrintf(255.0f * SOMVR_BACK_DIM)), fr);
    float bw = e->q[3].size[0], bh = e->q[3].size[1];
    float n[3], z[3] = {0, 0, 1};
    qrot_t(e->q[3].pose, z, n);
    CHECK(is_board_back() && rect_is(e->q[2].rect, rc[0], rc[1], rc[2], rc[3]) && rect_is(e->q[1].rect, fr[0], fr[1], fr[2], fr[3]) &&
              fabsf(e->q[2].size[0] - (bw + 2 * SOMVR_BACK_MARGIN_M)) < 1e-5f &&
              fabsf(e->q[2].size[1] - (bh + 2 * SOMVR_BACK_MARGIN_M)) < 1e-5f && e->q[1].size[0] == SOMVR_FADE_SIZE_M &&
              e->q[2].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT &&
              fabsf(e->q[2].pose[4] - (e->q[3].pose[4] - SOMVR_BACK_BEHIND_M * n[0])) < 1e-5f &&
              fabsf(e->q[2].pose[6] - (e->q[3].pose[6] - SOMVR_BACK_BEHIND_M * n[2])) < 1e-5f &&
              !memcmp(e->q[2].pose, e->q[3].pose, 4 * sizeof(float)),
          "the full level: the panel's cell %d,%d (want %d), %.3f x %.3f m behind the %.3f x %.3f board, the dim's ramp %d "
          "(want %d)", e->q[2].rect[0], e->q[2].rect[1], rc[0], e->q[2].size[0], e->q[2].size[1], bw, bh, e->q[1].rect[0], fr[0]);
    /* a reuse present: that frame's backdrop again */
    hud_present(&r.a, 999, 2, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU);
    CHECK(is_board_back(), "a reuse during the menu: %u layers", MOCK(mockxr_last_end)()->layer_count);
    /* the close: the panel falls over 0.15 s at its place, then the world's HUD alone */
    hud_stereo(&r, ++f, 2, SOMVR_HUDK_WORLD, &v);
    e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 4 && e->q[2].sc == kb && e->q[2].rect[0] < rc[0], "the close: falling (cell x %d)", e->q[2].rect[0]);
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD, is_world_hud, 30) && f < 70, "gone after the fall (frame %llu)",
          (unsigned long long)f);
    app_teardown(&r.a);
    CHECK(log_has("xr: menu backdrop: on: a dark panel 6 cm beyond the menu board on each side (its rim soft over 5 cm, "
                  "alpha 0.90), the world dimmed 40 % behind it") && !log_has("NEVER SHOWN") &&
              log_has("xr: menu backdrop: image 1536x120 format 43 (a static swapchain, 1 image(s)) filled once") &&
              log_has("xr: menu backdrop 1: a game menu on the board") &&
              log_has("xr: menu backdrop (the session's end): 1 menu open(s)"),
          "the backdrop's lines");
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_MENU_BACK=on without SOMVR_XR_MENU=front, bad values: never shown, logged */
static int sc_menu_back_nomenu(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    uint64_t f = 0;
    CHECK(cover_until(&r, &f, SOMVR_HUDK_WORLD | SOMVR_HUDF_MENU, is_world_hud, 60), "a menu's HUD without the board: 2 layers");
    app_teardown(&r.a);
    CHECK(log_has("NEVER SHOWN: needs SOMVR_XR_MENU=front") &&
              log_has("xr: menu backdrop: SOMVR_XR_MENU_BACK_DIM=2 is not 0..0.9: 0.40") &&
              log_has("xr: cover: SOMVR_XR_COVER=yes is not on or off: off") &&
              log_has("xr: cover: SOMVR_XR_COVER_MS=x is not 0..5000: 300"),
          "the lines");
    check_mock_clean();
    return 0;
}

/* ---- the VR settings menu (docs/re-xr-pipeline.md 15, ../../somvr_vrmenu.h): the wrist panel ------------------- */
_Static_assert(SOMVR_ENV_EQ_W * (SOMVR_ENV_EQ_H + SOMVR_ENV_STRIP) >= SOMVR_VM_TEX_W * SOMVR_VM_TEX_H,
               "the fake driver keeps a whole menu picture");
static somvr_vrmenu_shared *g_vm;
/* vrmenu_obj.so next to the mock, in the global scope like LD_PRELOAD (absent in every other scenario); the preload's
 * settings (s, or the defaults) published before `active` */
static int vrmenu_obj(int active, uint32_t avail, const somvr_settings *s) {
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/vrmenu_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (!h || !(g_vm = (somvr_vrmenu_shared *)dlsym(h, SOMVR_VRMENU_SYMBOL)))
        return fprintf(stderr, "    vrmenu_obj.so: %s\n", dlerror()), -1;
    g_vm->magic = SOMVR_VRMENU_MAGIC;
    g_vm->version = SOMVR_VRMENU_VERSION;
    g_vm->size = (uint32_t)sizeof *g_vm;
    g_vm->pid = (int32_t)getpid();
    g_vm->avail = avail;
    g_vm->blink_on_ms = 140;
    somvr_settings d;
    somvr_set_default(&d);
    somvr_vrmenu_publish(g_vm, s ? s : &d);
    if (active) atomic_store(&g_vm->active, 1);
    return 0;
}
static somvr_settings vm_set(void) {
    somvr_settings s;
    memset(&s, 0, sizeof s);
    CHECK(somvr_vrmenu_read(g_vm, &s), "torn menu settings");
    return s;
}
/* the geometry, head-local (the mock's head at 0,1.6,-0.1 looking along -Z when its yaw is 0), turned with the head's
 * yaw at the next sync (the mock's head turns 0.5 rad/s about its own position): the left grip 46 cm ahead of and
 * below the eyes, the right hand at the chest */
static const float VM_GRIP_L[3] = {-0.05f, 1.30f, -0.45f}, VM_HEAD[3] = {0, 1.6f, -0.1f}, VM_HAND_R[3] = {0.15f, 1.35f, -0.2f};
static float vm_yaw_next(void) { return 0.5f * (float)((MOCK(mockxr_time)() + 11111111) % 1000000007) * 1e-9f; }
static void vm_qmul(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
static void vm_qrot(const float q[4], const float v[3], float o[3]) {
    float p[4] = {v[0], v[1], v[2], 0}, c[4] = {-q[0], -q[1], -q[2], q[3]}, t[4], r[4];
    vm_qmul(q, p, t), vm_qmul(t, c, r);
    memcpy(o, r, 3 * sizeof o[0]);
}
/* a local pose (q, p) turned by yaw about the head's vertical axis -> a mock pose (x y z w, x y z) */
static void vm_turn(float yaw, const float q[4], const float p[3], float out[7]) {
    float qy[4] = {0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw)}, rel[3] = {p[0] - VM_HEAD[0], 0, p[2] - VM_HEAD[2]}, w[3];
    vm_qmul(qy, q, out);
    vm_qrot(qy, rel, w);
    out[4] = VM_HEAD[0] + w[0], out[5] = p[1], out[6] = VM_HEAD[2] + w[2];
}
/* the left grip's local orientation: palm (its +X) toward the eyes, or (palm 0) the identity: the palm to the right */
static void vm_grip_q(int palm, float q[4]) {
    float n[3] = {VM_HEAD[0] - VM_GRIP_L[0], VM_HEAD[1] - VM_GRIP_L[1], VM_HEAD[2] - VM_GRIP_L[2]};
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int i = 0; i < 3; i++) n[i] /= l;
    float r[4] = {0, -n[2], n[1], 1 + n[0]}, m = sqrtf(r[1] * r[1] + r[2] * r[2] + r[3] * r[3]); /* +X -> n */
    float id[4] = {0, 0, 0, 1};
    for (int i = 0; i < 4; i++) q[i] = palm ? r[i] / m : id[i];
}
/* the panel's local pose for the palm grip (as the layer makes it: somvr_vm_panel_pose, world up) */
static somvr_posef vm_panel_local(void) {
    somvr_posef g, out;
    vm_grip_q(1, g.q);
    memcpy(g.p, VM_GRIP_L, sizeof g.p);
    somvr_vm_gcfg c;
    somvr_vm_gcfg_default(&c);
    CHECK(somvr_vm_panel_pose(&g, SOMVR_HAND_L, NULL, &c, &out), "the panel's local pose");
    return out;
}
/* the panel's centre (u, v): its rect's centre in uv */
static void vm_uv(int item, int part, float uv[2]) {
    int rc[4];
    CHECK(somvr_vm_rect(item, part, rc), "no rect for item %d part %d", item, part);
    uv[0] = (rc[0] + 0.5f * rc[2]) / SOMVR_VM_TEX_W, uv[1] = (rc[1] + 0.5f * rc[3]) / SOMVR_VM_TEX_H;
}
typedef struct {
    int palm;      /* the left palm toward the eyes */
    int left_ok;   /* the left hand tracked */
    float left_far; /* > 0: the left hand that far to the left and behind instead (the panel out of the gaze) */
    int right_ok;  /* the right hand tracked */
    float uv[2];   /* where on the panel the right hand aims (outside 0..1: beside it) */
    float trig;    /* the right trigger */
    float sq;      /* the right grip (squeeze) */
} vmpose;
/* both hands for the next sync, turned with the head */
static void vm_hands(const vmpose *s) {
    float yaw = vm_yaw_next();
    mockxr_hand L, R;
    memset(&L, 0, sizeof L), memset(&R, 0, sizeof R);
    float gq[4], gl[7], gp[3] = {VM_GRIP_L[0], VM_GRIP_L[1], VM_GRIP_L[2]};
    vm_grip_q(s->palm, gq);
    if (s->left_far > 0) gp[0] = -s->left_far, gp[2] = 0.3f;
    vm_turn(yaw, gq, gp, gl);
    memcpy(L.grip, gl, sizeof gl), memcpy(L.aim, gl, sizeof gl);
    L.grip_flags = L.aim_flags = s->left_ok ? 0xf : 0, L.vel_flags = 3;
    somvr_posef pl = vm_panel_local();
    float off[3] = {(s->uv[0] - 0.5f) * SOMVR_VM_W_M, (0.5f - s->uv[1]) * SOMVR_VM_H_M, 0}, w[3], P[3], d[3];
    vm_qrot(pl.q, off, w);
    for (int i = 0; i < 3; i++) P[i] = pl.p[i] + w[i], d[i] = P[i] - VM_HAND_R[i];
    float ya = atan2f(-d[0], -d[2]), pi = atan2f(d[1], sqrtf(d[0] * d[0] + d[2] * d[2]));
    float cy = cosf(0.5f * ya), sy = sinf(0.5f * ya), cx = cosf(0.5f * pi), sx = sinf(0.5f * pi);
    float aq[4] = {cy * sx, sy * cx, -sy * sx, cy * cx}, al[7];
    vm_turn(yaw, aq, VM_HAND_R, al);
    memcpy(R.aim, al, sizeof al), memcpy(R.grip, al, sizeof al);
    R.grip_flags = R.aim_flags = s->right_ok ? 0xf : 0, R.vel_flags = 3;
    R.trigger = s->trig, R.squeeze = s->sq;
    hand_set(0, &L), hand_set(1, &R);
}
static void vm_mono(app_t *a, const vmpose *s, int n) {
    for (int i = 0; i < n; i++) vm_hands(s), app_present(a);
}
/* the last frame's panel layer (its index; 0 = none), its form checked */
static int vm_layer(const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int k = MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H);
    for (uint32_t i = 1; k >= 0 && i < e->layer_count && i < MOCK_NQ; i++) {
        if (e->q[i].sc != k) continue;
        CHECK(e->q[i].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[i].space == 1 &&
                  e->q[i].flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT &&
                  e->q[i].eyes == XR_EYE_VISIBILITY_BOTH && rect_is(e->q[i].rect, 0, 0, SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) &&
                  fabsf(e->q[i].size[0] - SOMVR_VM_W_M) < 1e-6f && fabsf(e->q[i].size[1] - SOMVR_VM_H_M) < 1e-6f,
              "%s: panel layer %u: type %d space %d flags 0x%llx eyes %d rect %d,%d %dx%d size %g x %g", what, i,
              e->q[i].type, e->q[i].space, (unsigned long long)e->q[i].flags, e->q[i].eyes, e->q[i].rect[0],
              e->q[i].rect[1], e->q[i].rect[2], e->q[i].rect[3], e->q[i].size[0], e->q[i].size[1]);
        return (int)i;
    }
    return 0;
}
/* the last upload is the picture of (s, avail, hover) exactly (somvr_vm_draw into RGBA: the format is R8G8B8A8_SRGB) */
static uint8_t g_vm_px[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4];
static int vm_picture_is(const somvr_settings *s, uint32_t avail, int item, int part) {
    static somvr_font f;
    static int init;
    if (!init) init = 1, somvr_font_init(&f);
    somvr_vm_view v;
    memset(&v, 0, sizeof v);
    v.set = *s, v.avail = avail, v.hover = (somvr_vm_hit){item, part};
    somvr_vm_draw(&f, g_vm_px, 0, &v);
    return V.up_w == SOMVR_VM_TEX_W && V.up_h == SOMVR_VM_TEX_H && !memcmp(V.up_px, g_vm_px, sizeof g_vm_px);
}
#define VMA_MONO (SOMVR_VMA_VIEW | SOMVR_VMA_COMFORT) /* what the layer leaves of VMA_ALL without a HUD quad, in LOCAL */
#define VMA_ALL (SOMVR_VMA_VIEW | SOMVR_VMA_HUD | SOMVR_VMA_COMFORT)

static int sc_vrmenu(void) { /* mono frames (menus): the gesture, the pointer, the clicks, the closes, the hand swap */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrmenu_obj(1, VMA_ALL, NULL)) return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 0, .uv = {2, 2}};
    vm_mono(&a, &rest, 4);
    CHECK(log_has("xr: vrmenu: somvr_vrmenu found at") && log_has("the wrist panel on (20 x 30 cm on the left wrist, the "
                                                                   "right hand points"),
          "log lacks the menu-on line");
    CHECK(MOCK(mockxr_last_end)()->layer_count == 1 && !atomic_load(&g_vm->open) &&
              MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) < 0 && V.buf2img == 0,
          "at rest: %u layers, open %u", MOCK(mockxr_last_end)()->layer_count, atomic_load(&g_vm->open));
    /* the palm toward the eyes: nothing before 0.5 s, then the panel (the right hand untracked: no ray yet) */
    vmpose palm = rest;
    palm.palm = 1;
    vm_mono(&a, &palm, 30);
    CHECK(!atomic_load(&g_vm->open) && MOCK(mockxr_last_end)()->layer_count == 1, "open after 0.33 s");
    int n = 0;
    while (!atomic_load(&g_vm->open) && n++ < 40) vm_mono(&a, &palm, 1);
    CHECK(atomic_load(&g_vm->open) == 1 && n > 5 && n < 30 && atomic_load(&g_vm->n_opens) == 1 &&
              log_has("xr: vrmenu: opened by the palm gesture on the left wrist (opens 1)"),
          "the palm gesture: open %u after %d more frames, opens %llu", atomic_load(&g_vm->open), n,
          (unsigned long long)atomic_load(&g_vm->n_opens));
    vm_mono(&a, &palm, 2);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int pl = vm_layer("open");
    CHECK(e->layer_count == 2 && pl == 1 && e->type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->rect[0][2] == 1920,
          "open: %u layers, the panel at %d", e->layer_count, pl);
    somvr_posef loc = vm_panel_local();
    float want[7], yaw = 0.5f * (float)(MOCK(mockxr_time)() % 1000000007) * 1e-9f;
    vm_turn(yaw - 0.5f * 0.0111111f, loc.q, loc.p, want); /* the grip sampled at the previous sync */
    CHECK(fabsf(e->q[1].pose[4] - want[4]) < 3e-3f && fabsf(e->q[1].pose[5] - want[5]) < 1e-4f &&
              fabsf(e->q[1].pose[6] - want[6]) < 3e-3f && fabsf(fabsf(e->q[1].pose[3]) - fabsf(want[3])) < 3e-3f,
          "the panel at %.4f,%.4f,%.4f q %.4f, want %.4f,%.4f,%.4f q %.4f", e->q[1].pose[4], e->q[1].pose[5],
          e->q[1].pose[6], e->q[1].pose[3], want[4], want[5], want[6], want[3]);
    int k = MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H);
    const mockxr_swapchain *ps = MOCK(mockxr_swapchain_k)(k);
    CHECK(k == 1 && ps->format == VK_FORMAT_R8G8B8A8_SRGB && ps->create_flags == 0 &&
              ps->usage == (XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
              ps->acquires == 1 && V.buf2img == 1 && atomic_load(&g_vm->n_uploads) == 1,
          "panel swapchain slot %d format %lld flags 0x%x usage 0x%x acquires %d, uploads %d/%llu", k,
          (long long)ps->format, ps->create_flags, ps->usage, ps->acquires, V.buf2img,
          (unsigned long long)atomic_load(&g_vm->n_uploads));
    somvr_settings s0 = vm_set();
    CHECK(vm_picture_is(&s0, VMA_MONO, -1, SOMVR_VMP_NONE), "the panel's first picture is not somvr_vm_draw's");
    CHECK(V.buffers_live == 1 && V.mems_live == 1 && log_has("xr: vrmenu: panel swapchain 512x768 format 43 (3 images)"),
          "the staging buffer: %d buffers, %d memories", V.buffers_live, V.mems_live);
    /* the right hand on the Snap angle's > box: hovered (a new picture, a tick), the trigger is the panel's */
    int hap0 = MOCK(mockxr_haptic_get)(1)->applies;
    vmpose aim = palm;
    aim.right_ok = 1;
    vm_uv(SOMVR_VMI_SNAP, SOMVR_VMP_MORE, aim.uv);
    vm_mono(&a, &aim, 4);
    e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 4 && vm_layer("hover") == 1 && e->q[2].sc == MOCK(mockxr_sc_find)(64, 32) &&
              rect_is(e->q[2].rect, 32, 0, 32, 32) && rect_is(e->q[3].rect, 0, 0, 32, 32) &&
              fabsf(e->q[2].size[1] - 0.0015f) < 1e-6f,
          "hover: %u layers (the panel, the ray, the dot): sc %d rect %d,%d", e->layer_count, e->q[2].sc, e->q[2].rect[0],
          e->q[3].rect[0]);
    CHECK(atomic_load(&g_vm->consume) == 2u && MOCK(mockxr_haptic_get)(1)->applies == hap0 + 1 &&
              MOCK(mockxr_haptic_get)(1)->amplitude > 0.11f && MOCK(mockxr_haptic_get)(1)->amplitude < 0.13f,
          "hover: consume 0x%x, haptics %d -> %d amp %.2f", atomic_load(&g_vm->consume), hap0,
          MOCK(mockxr_haptic_get)(1)->applies, MOCK(mockxr_haptic_get)(1)->amplitude);
    int ups = V.buf2img;
    /* the dot on the box's centre (the panel's local point, turned) */
    float c[3], w3[3], off[3] = {(aim.uv[0] - 0.5f) * SOMVR_VM_W_M, (0.5f - aim.uv[1]) * SOMVR_VM_H_M, 0};
    vm_qrot(loc.q, off, w3);
    for (int i = 0; i < 3; i++) c[i] = loc.p[i] + w3[i];
    float qd[4] = {0, 0, 0, 1}, wd[7];
    vm_turn(yaw - 0.5f * 0.0111111f + 0.5f * 4 * 0.0111111f, qd, c, wd);
    CHECK(fabsf(e->q[3].pose[4] - wd[4]) < 6e-3f && fabsf(e->q[3].pose[5] - wd[5]) < 3e-3f &&
              fabsf(e->q[3].pose[6] - wd[6]) < 6e-3f,
          "the dot at %.4f,%.4f,%.4f, want about %.4f,%.4f,%.4f", e->q[3].pose[4], e->q[3].pose[5], e->q[3].pose[6], wd[4],
          wd[5], wd[6]);
    /* a click: 30 -> 45 deg, published (gen + 1), a stronger tick, a new picture; held: no second click */
    uint32_t gen0 = atomic_load(&g_vm->gen);
    aim.trig = 0.9f;
    vm_mono(&a, &aim, 4);
    somvr_settings s1 = vm_set();
    CHECK(s1.snap_deg == 45 && atomic_load(&g_vm->gen) == gen0 + 1 && atomic_load(&g_vm->n_clicks) == 1 &&
              log_has("xr: vrmenu: click Snap angle >: changed 30\x7f -> 45\x7f (gen") &&
              MOCK(mockxr_haptic_get)(1)->amplitude > 0.34f && V.buf2img > ups,
          "the click: snap %g, gen %u -> %u, clicks %llu, amp %.2f", s1.snap_deg, gen0, atomic_load(&g_vm->gen),
          (unsigned long long)atomic_load(&g_vm->n_clicks), MOCK(mockxr_haptic_get)(1)->amplitude);
    CHECK(vm_picture_is(&s1, VMA_MONO, SOMVR_VMI_SNAP, SOMVR_VMP_MORE), "the picture after the click");
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(atomic_load(&g_vm->gen) == gen0 + 1 && vm_set().snap_deg == 45, "a second change without a second press");
    /* the Turn row (its label): snap -> smooth; then the Snap angle row is disabled: a click there changes nothing */
    vm_uv(SOMVR_VMI_TURN, SOMVR_VMP_ROW, aim.uv);
    aim.uv[0] = 0.15f, vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(vm_set().turn == 1 && atomic_load(&g_vm->gen) == gen0 + 2, "Turn: %u", vm_set().turn);
    vm_uv(SOMVR_VMI_SNAP, SOMVR_VMP_MORE, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(vm_set().snap_deg == 45 && atomic_load(&g_vm->gen) == gen0 + 2 && atomic_load(&g_vm->n_clicks) == 2 &&
              log_has("xr: vrmenu: click Snap angle >: nothing"),
          "a disabled row: snap %g gen %u clicks %llu", vm_set().snap_deg, atomic_load(&g_vm->gen),
          (unsigned long long)atomic_load(&g_vm->n_clicks));
    /* Recentre: the game side's counter */
    vm_uv(SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(atomic_load(&g_vm->recenters) == 1 && atomic_load(&g_vm->n_clicks) == 3 && atomic_load(&g_vm->open) == 1,
          "recentre: %u, clicks %llu", atomic_load(&g_vm->recenters), (unsigned long long)atomic_load(&g_vm->n_clicks));
    /* v3, the practice page (SOMVR_PRACTICE, re-xr-pipeline.md 27): no tab without it; with it (and the preload's live
     * state: in the area, a place to go back to) the tab turns the page, Spawn posts its request word (the orc type
     * from the settings), the tab turns it back */
    vm_uv(SOMVR_VMI_TAB, SOMVR_VMP_BUTTON, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(atomic_load(&g_vm->n_clicks) == 3 && log_has("xr: vrmenu: click Practice button: nothing"),
          "the tab without SOMVR_PRACTICE: clicks %llu", (unsigned long long)atomic_load(&g_vm->n_clicks));
    uint32_t avail0 = g_vm->avail;
    g_vm->avail = avail0 | SOMVR_VMA_PRACTICE;
    atomic_store(&g_vm->pstate, SOMVR_VMA_PR_HERE | SOMVR_VMA_PR_BACK);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(log_has("xr: vrmenu: click Practice button: the practice page"), "the tab: no page turn");
    vm_uv(SOMVR_VMI_PR_SPAWN, SOMVR_VMP_BUTTON, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    uint32_t pw = atomic_load(&g_vm->practice);
    CHECK(somvr_vm_pr_seq(pw) == 1 && somvr_vm_pr_action(pw) == SOMVR_VMPR_SPAWN && somvr_vm_pr_orc(pw) == 0 &&
              log_has("xr: vrmenu: click Spawn an orc button: practice request"),
          "Spawn: the request word %#x", pw);
    /* build 6: the tab strip: Controls, then Settings back */
    vm_uv(SOMVR_VMI_TAB_CTL, SOMVR_VMP_BUTTON, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(log_has("xr: vrmenu: click Controls button: the controls page"), "the Controls tab");
    vm_uv(SOMVR_VMI_TAB_SET, SOMVR_VMP_BUTTON, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(log_has("xr: vrmenu: click Settings button: the settings page") && atomic_load(&g_vm->practice) == pw,
          "the tab back");
    /* v4 (re-xr-pipeline.md 29): the Sword angle row, dimmed without SOMVR_VMA_SWORD; with it its > box turns the blade
     * 5 deg up, published for the game side (SOMVR_SWORD_ANGLE live) */
    vm_uv(SOMVR_VMI_SWORD, SOMVR_VMP_MORE, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(vm_set().sword_deg == 0 && log_has("xr: vrmenu: click Sword angle >: nothing"), "the sword row without its bit");
    g_vm->avail = avail0 | SOMVR_VMA_SWORD;
    uint32_t gs = atomic_load(&g_vm->gen);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    aim.trig = 0, vm_mono(&a, &aim, 3);
    CHECK(vm_set().sword_deg == 5 && atomic_load(&g_vm->gen) == gs + 1 &&
              log_has("xr: vrmenu: click Sword angle >: changed 0\x7f -> +5\x7f (gen"),
          "the sword row: %g deg, gen %u -> %u", vm_set().sword_deg, gs, atomic_load(&g_vm->gen));
    g_vm->avail = avail0;
    atomic_store(&g_vm->pstate, 0);
    atomic_store(&g_vm->n_clicks, 3);
    /* its close box: closed, the palm still up does not open it again until it has been turned away */
    vm_uv(SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    CHECK(!atomic_load(&g_vm->open) && MOCK(mockxr_last_end)()->layer_count == 1 &&
              log_has("xr: vrmenu: closed: its close box (closes 1"),
          "the close box: open %u, %u layers", atomic_load(&g_vm->open), MOCK(mockxr_last_end)()->layer_count);
    aim.trig = 0, vm_mono(&a, &aim, 60);
    CHECK(!atomic_load(&g_vm->open), "re-opened without turning the palm away");
    /* the menu button (the game side's toggles): open, close */
    atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &rest, 2);
    CHECK(atomic_load(&g_vm->open) == 1 && log_has("xr: vrmenu: opened by the menu button"), "toggle: not open");
    atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &rest, 2);
    CHECK(!atomic_load(&g_vm->open) && log_has("xr: vrmenu: closed: the menu button"), "toggle: not closed");
    /* looked away: the panel more than 60 deg off the gaze for 0.5 s */
    atomic_fetch_add(&g_vm->toggles, 1);
    vmpose far = rest;
    far.left_far = 0.6f;
    vm_mono(&a, &far, 30);
    CHECK(atomic_load(&g_vm->open) == 1, "looked away: closed before 0.5 s");
    vm_mono(&a, &far, 30);
    CHECK(!atomic_load(&g_vm->open) && log_has("xr: vrmenu: closed: looked away"), "looked away: still open");
    /* the hand lost 1 s: closed (nothing drawn meanwhile) */
    atomic_fetch_add(&g_vm->toggles, 1);
    vmpose lost = palm;
    lost.left_ok = 0;
    vm_mono(&a, &lost, 60);
    CHECK(atomic_load(&g_vm->open) == 1 && MOCK(mockxr_last_end)()->layer_count == 1, "the hand lost: closed early");
    vm_mono(&a, &lost, 40);
    CHECK(!atomic_load(&g_vm->open) && log_has("xr: vrmenu: closed: the hand lost"), "the hand lost: still open");
    /* the main hand: a click on Main hand moves the panel to the right wrist; the left hand points */
    atomic_fetch_add(&g_vm->toggles, 1);
    aim = palm, aim.right_ok = 1;
    vm_uv(SOMVR_VMI_HAND, SOMVR_VMP_MORE, aim.uv);
    vm_mono(&a, &aim, 3);
    aim.trig = 0.9f, vm_mono(&a, &aim, 3);
    vm_mono(&a, &aim, 2);
    CHECK(vm_set().hand == 1 && log_has("xr: vrmenu: the left hand points now, the panel is on the right wrist"),
          "Main hand: %u", vm_set().hand);
    CHECK(V.viol == 0, "%d Vulkan violations, first: %s", V.viol, V.first);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) < 0,
          "XR objects alive after vkDestroyDevice");
    CHECK(V.sems_live == 0 && V.fences_live == 0 && V.buffers_live == 0 && V.mems_live == 0 && !atomic_load(&g_vm->open) &&
              !atomic_load(&g_vm->consume),
          "layer objects leaked: %d fences %d buffers %d memories; open %u", V.fences_live, V.buffers_live, V.mems_live,
          atomic_load(&g_vm->open));
    CHECK(atomic_load(&g_vm->n_uploads) == (uint64_t)V.buf2img - 1 && atomic_load(&g_vm->n_frames) > 50,
          "counters: uploads %llu (copies %d, one the pointer's), frames %llu",
          (unsigned long long)atomic_load(&g_vm->n_uploads), V.buf2img, (unsigned long long)atomic_load(&g_vm->n_frames));
    check_mock_clean();
    return 0;
}

static int sc_card(void) { /* build 7: the arrival card (SOMVR_XR_CARD=on) holds the blade off (open), so a grip
                             * squeezed after it came up (a draw at the shoulder, a grab) puts it away; a grip held
                             * through its arrival does not */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrmenu_obj(1, VMA_ALL, NULL)) return 1;
    g_vm->avail |= SOMVR_VMA_PRACTICE;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 1, .uv = {2, 2}, .sq = 0.9f};
    vm_mono(&a, &rest, 4);
    CHECK(!log_has("xr: card: shown") && !atomic_load(&g_vm->open), "the card before the practice area");
    atomic_store(&g_vm->pstate, SOMVR_VMA_PR_HERE);
    vm_mono(&a, &rest, 10);
    CHECK(log_has("xr: card: shown") && atomic_load(&g_vm->open) == 1, "the card at the arrival: open %u",
          atomic_load(&g_vm->open));
    CHECK(!log_has("xr: card: put away"), "a grip held through the arrival put the card away");
    rest.sq = 0.2f, vm_mono(&a, &rest, 3);
    CHECK(atomic_load(&g_vm->open) == 1 && !log_has("xr: card: put away"), "a grip let go put the card away");
    rest.sq = 0.9f, vm_mono(&a, &rest, 3);
    CHECK(log_has("xr: card: put away (the right grip squeezed)") && !atomic_load(&g_vm->open),
          "the squeeze: open %u", atomic_load(&g_vm->open));
    rest.sq = 0, vm_mono(&a, &rest, 3);
    rest.sq = 0.9f, vm_mono(&a, &rest, 3);
    CHECK(!atomic_load(&g_vm->open), "the card came back (once a session)");
    atomic_store(&g_vm->pstate, 0);
    CHECK(V.viol == 0, "%d Vulkan violations, first: %s", V.viol, V.first);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

static int sc_vrmenu_stereo(void) { /* layout B with the HUD quad and comfort: the menu's HUD distance, vignette, order */
    hudrig r;
    somvr_settings s;
    somvr_set_default(&s);
    s.hud_dist_m = 3.0f, s.vignette = 0.8f;
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || comfort_obj(1, 0, 0) || vrmenu_obj(1, VMA_ALL, &s) ||
        hud_setup(&r, 2))
        return 1;
    CHECK(log_has("the VR menu sets both live") && log_has("vignette strength 0.80"), "comfort is not on for the menu");
    somvr_views v;
    hud_info(2, 1920, 1080, r.H);
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 0, .uv = {2, 2}};
    uint64_t f = 1;
    vm_hands(&rest);
    cf_stereo(&r, f++, 0, 0, 0, 2, &v);
    for (int i = 0; i < 3; i++, f++) vm_hands(&rest), cf_stereo(&r, f, 0, 0, 0, 2, &v);
    float at[7];
    hud_expect_world(&v, 3.0f, 0.2f, at); /* placed from the first quad's eyes: compare the distance only */
    const mockxr_end *e = MOCK(mockxr_last_end)();
    float m[3] = {0.5f * (v.pose[0].p[0] + v.pose[1].p[0]), 0.5f * (v.pose[0].p[1] + v.pose[1].p[1]),
                  0.5f * (v.pose[0].p[2] + v.pose[1].p[2])};
    float dx = e->pose2[4] - m[0], dz = e->pose2[6] - m[2];
    CHECK(e->type2 == XR_TYPE_COMPOSITION_LAYER_QUAD && fabsf(sqrtf(dx * dx + dz * dz) - 3.0f) < 0.02f &&
              fabsf(e->pose2[5] - (m[1] - 0.2f)) < 0.01f && fabsf(e->size2[0] - 2.4f) < 1e-5f,
          "the HUD quad with the menu's 3 m: %.3f m ahead, %.3f below, %.3f wide", sqrtf(dx * dx + dz * dz),
          m[1] - e->pose2[5], e->size2[0]);
    /* moving: the ring (the menu's 0.8 strength; the object's is 0), right over the projection, under the HUD */
    for (int i = 0; i < 20; i++, f++) vm_hands(&rest), cf_stereo(&r, f, 3.0f, 0, 0, 2, &v);
    CHECK(cf_quad(0, "the menu's vignette") == 1, "no ring with the menu's vignette (or not under the HUD)");
    /* the panel (the menu button): over the HUD quad, comfort's ring under both (docs/re-xr-pipeline.md 16.8) */
    atomic_fetch_add(&g_vm->toggles, 1);
    vmpose aim = {.palm = 1, .left_ok = 1, .right_ok = 1};
    vm_uv(SOMVR_VMI_HUD, SOMVR_VMP_MORE, aim.uv);
    for (int i = 0; i < 4; i++, f++) vm_hands(&aim), cf_stereo(&r, f, 3.0f, 0, 0, 2, &v);
    e = MOCK(mockxr_last_end)();
    int pl = vm_layer("stereo");
    CHECK(atomic_load(&g_vm->open) && e->q[2].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[2].sc == 1 && pl == 3 &&
              e->layer_count == 6 && cf_quad(0, "after the panel") == 1,
          "stereo layers: %u, the HUD's swapchain at 2 %d, the panel at %d, the ring at %d", e->layer_count, e->q[2].sc,
          pl, cf_quad(0, "x"));
    /* a click on HUD >: off; the next frames have no HUD quad (layer 1 the ring, 2 the panel) */
    aim.trig = 0.9f;
    for (int i = 0; i < 3; i++, f++) vm_hands(&aim), cf_stereo(&r, f, 3.0f, 0, 0, 2, &v);
    aim.trig = 0;
    for (int i = 0; i < 3; i++, f++) vm_hands(&aim), cf_stereo(&r, f, 3.0f, 0, 0, 2, &v);
    e = MOCK(mockxr_last_end)();
    CHECK(vm_set().hud == 0 && vm_layer("HUD off") == 2 && e->q[1].sc != 1 && cf_quad(0, "HUD off") == 1,
          "HUD off: hud %u, the panel at %d, layer 1's swapchain %d", vm_set().hud, vm_layer("x"), e->q[1].sc);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && V.buffers_live == 0 && V.mems_live == 0 && V.fences_live == 0,
          "objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* no object, an inactive one, SOMVR_XR_VRMENU=off, no somvr_input: no panel, nothing made, nothing published */
static int vm_none(int obj, const char *logline) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if ((obj != 3 && input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE)) || (obj && vrmenu_obj(obj != 1, VMA_ALL, NULL)))
        return 1;
    if (load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    vmpose palm = {.palm = 1, .left_ok = 1, .right_ok = 1, .uv = {0.5f, 0.5f}};
    vm_mono(&a, &palm, 3);
    if (g_vm) atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &palm, 70);
    CHECK(log_has(logline) && MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) < 0 &&
              MOCK(mockxr_last_end)()->layer_count == 1 && (!g_vm || (!atomic_load(&g_vm->open) && !g_vm->n_frames)),
          "no menu: log \"%s\" %d, %u layers", logline, log_has(logline), MOCK(mockxr_last_end)()->layer_count);
    app_teardown(&a);
    check_mock_clean();
    return 0;
}
static int sc_vrmenu_absent(void) { return vm_none(0, "xr: vrmenu: no VR menu (no somvr_vrmenu: a preload without it)"); }
static int sc_vrmenu_inactive(void) { return vm_none(1, "xr: vrmenu: no VR menu (somvr_vrmenu inactive"); }
static int sc_vrmenu_off(void) { return vm_none(2, "but SOMVR_XR_VRMENU=off: no wrist panel (its settings hold: TURN=snap"); }
static int sc_vrmenu_noinput(void) { return vm_none(3, "xr: vrmenu: no VR menu (no somvr_input (the menu needs SOMVR_INPUT=1))"); }

/* a failure: the menu off for the session (logged), the frames go on with the menu quad alone, XR up */
static int vm_fail(const char *logline, int extra_ends) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrmenu_obj(1, VMA_ALL, NULL) || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 0, .uv = {2, 2}};
    vm_mono(&a, &rest, 3);
    int ends = calls("xrEndFrame");
    atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &rest, 10);
    atomic_fetch_add(&g_vm->toggles, 2); /* closed and open again: nothing */
    vm_mono(&a, &rest, 10);
    CHECK(log_has(logline) && log_count("xr: vrmenu: off for this session") == 1 &&
              MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H) < 0 && MOCK(mockxr_last_end)()->layer_count == 1 &&
              calls("xrEndFrame") == ends + 20 + extra_ends && !atomic_load(&g_vm->open) && V.buffers_live == 0 &&
              V.mems_live == 0,
          "menu failure: log %d, swapchain %d, %u layers, ends +%d (want +%d), open %u, %d buffers",
          log_has(logline), MOCK(mockxr_sc_find)(SOMVR_VM_TEX_W, SOMVR_VM_TEX_H), MOCK(mockxr_last_end)()->layer_count,
          calls("xrEndFrame") - ends, 20 + extra_ends, atomic_load(&g_vm->open), V.buffers_live);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && V.fences_live == 0, "objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_vrmenu_reject(void) {
    return vm_fail("xr: vrmenu: off for this session: xrEndFrame rejected the frame with the menu's 1 layer(s): "
                   "XR_ERROR_LAYER_INVALID", 1);
}
static int sc_vrmenu_swapchain_fail(void) {
    return vm_fail("xr: vrmenu: off for this session: xrCreateSwapchain(512x768 format 43): XR_ERROR_RUNTIME_FAILURE", 0);
}

/* the gesture's gate (docs/re-xr-pipeline.md 15.7): the preload's block bits (the bow, a climb hold, a swing, an attack
 * incoming) hold the palm gesture off; a palm held through them opens nothing after them, a fresh gesture does; the
 * menu button still toggles */
static int sc_vrmenu_gate(void) {
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrmenu_obj(1, VMA_ALL, NULL) || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 0, .uv = {2, 2}}, palm = rest;
    palm.palm = 1;
    vm_mono(&a, &rest, 4);
    /* the bow out: the palm toward the eyes 1.5 s opens nothing (one "held off" line) */
    atomic_store(&g_vm->block, SOMVR_VMG_BOW);
    vm_mono(&a, &palm, 135);
    CHECK(!atomic_load(&g_vm->open) && atomic_load(&g_vm->n_opens) == 0 && MOCK(mockxr_last_end)()->layer_count == 1 &&
              log_count("xr: vrmenu: the palm gesture held off: the bow (1 so far;") == 1,
          "the bow out: open %u opens %llu", atomic_load(&g_vm->open), (unsigned long long)atomic_load(&g_vm->n_opens));
    /* the bow put away, the palm still up: still nothing (1.5 s) */
    atomic_store(&g_vm->block, 0);
    vm_mono(&a, &palm, 135);
    CHECK(!atomic_load(&g_vm->open) && atomic_load(&g_vm->n_opens) == 0, "held through the gate: opened");
    /* a fresh gesture: the palm away 0.4 s, toward the eyes: open within 0.4 .. 0.6 s */
    vm_mono(&a, &rest, 36);
    int n = 0;
    while (!atomic_load(&g_vm->open) && n++ < 90) vm_mono(&a, &palm, 1);
    CHECK(atomic_load(&g_vm->open) == 1 && n >= 36 && n <= 56 &&
              log_has("xr: vrmenu: opened by the palm gesture on the left wrist (opens 1)"),
          "the fresh gesture: open %u after %d frames", atomic_load(&g_vm->open), n);
    /* open: a climb hold starts - the panel stays; closed by the button, the palm up under the hold opens nothing */
    atomic_store(&g_vm->block, SOMVR_VMG_CLIMB | SOMVR_VMG_THREAT);
    vm_mono(&a, &rest, 10);
    CHECK(atomic_load(&g_vm->open) == 1, "the open panel closed by the gate");
    atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &rest, 30);
    CHECK(!atomic_load(&g_vm->open) && log_has("xr: vrmenu: closed: the menu button"), "the button did not close it");
    vm_mono(&a, &palm, 90);
    CHECK(!atomic_load(&g_vm->open) && log_has("xr: vrmenu: the palm gesture held off: a climb hold+an attack incoming (2 so far"),
          "the climb: open %u", atomic_load(&g_vm->open));
    /* the menu button while gated: opens */
    atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &rest, 2);
    CHECK(atomic_load(&g_vm->open) == 1 && log_has("xr: vrmenu: opened by the menu button"), "the button under the gate");
    atomic_store(&g_vm->block, 0);
    CHECK(V.viol == 0, "%d Vulkan violations, first: %s", V.viol, V.first);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && V.buffers_live == 0 && V.mems_live == 0, "objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

/* VR build 3's layers in one stereo frame (docs/re-xr-pipeline.md 19.3): somvr_input, the HUD quad, comfort at 0 / 0 with
 * the menu's vignette turned up in the headset, the threat's 3 glows and both edges, the VR menu's panel with the main
 * hand's ray and dot, the fallback's fade: bottom to top projection, glows, ring, edges, HUD, panel, ray, dot, fade */
static int sc_build3(void) {
    hudrig r;
    somvr_settings s;
    somvr_set_default(&s);
    s.vignette = 0.5f; /* the build's SOMVR_VIGNETTE=0, the player's menu choice */
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || comfort_obj(1, 0, 0) || vrmenu_obj(1, VMA_ALL, &s) || threat_obj(1) ||
        hud_setup(&r, 2))
        return 1;
    somvr_views v;
    hud_info(2, 1920, 1080, r.H);
    somvr_threat_frame e3 = th_frame_of(3, 0.9f, 0.6f, 0.8f);
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 0, .uv = {2, 2}};
    vmpose aim = {.palm = 1, .left_ok = 1, .right_ok = 1};
    vm_uv(SOMVR_VMI_SNAP, SOMVR_VMP_MORE, aim.uv);
    uint64_t f = 1;
    for (int i = 0; i < 24; i++, f++) { /* moving (the ring), the threats, the HUD; the panel by the menu button at 8 */
        if (i == 8) atomic_fetch_add(&g_vm->toggles, 1);
        vm_hands(i < 8 ? &rest : &aim);
        CHECK(locate(&v), "frame %llu: locate", (unsigned long long)f);
        write_record_fade(f, SOMVR_LAYOUT_B, &v, i < 16 ? 0 : 128); /* the fallback's fade from frame 16 */
        somvr_threat_put(g_tho, f, &e3);
        cf_put(f, 3.0f, 0, 0);
        V.ncp = V.ntr = 0;
        hud_present(&r.a, f, 2, SOMVR_HUDK_WORLD);
    }
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int k = MOCK(mockxr_sc_find)(64, 32), pl = vm_layer("build 3");
    CHECK(atomic_load(&g_vm->open) == 1 && e->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && e->layer_count == 12,
          "build 3: open %u, layer 0 type %d, %u layers (want 12)", atomic_load(&g_vm->open), e->type, e->layer_count);
    for (int i = 0; i < 3; i++) th_check_glow(1 + i, &v, &e3.g[i], "build 3");
    CHECK(cf_quad(0, "build 3") == 4, "the ring at layer %d, want 4 (over the glows)", cf_quad(0, "x"));
    th_check_edge(5, &v, SOMVR_TH_LEFT, 0.6f, "build 3");
    th_check_edge(6, &v, SOMVR_TH_RIGHT, 0.8f, "build 3");
    CHECK(e->q[7].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[7].sc == 1, "the HUD quad at 7: type %d sc %d", e->q[7].type,
          e->q[7].sc);
    CHECK(pl == 8 && k >= 0 && e->q[9].sc == k && e->q[10].sc == k, "the panel at %d (want 8), its ray and dot at 9, 10: %d %d (%d)",
          pl, e->q[9].sc, e->q[10].sc, k);
    check_fade_layer(11, somvr_fade_index(128), &v, "build 3");
    CHECK(V.viol == 0, "%d Vulkan violations, first: %s", V.viol, V.first);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && V.buffers_live == 0 && V.mems_live == 0, "objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

static int sc_vrmenu_env(void) { /* SOMVR_XR_ENV=on (section 14): the sky first, the menu quad, then the panel */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrmenu_obj(1, VMA_ALL, NULL) || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    vmpose rest = {.palm = 0, .left_ok = 1, .right_ok = 0, .uv = {2, 2}};
    vm_mono(&a, &rest, 60); /* the sky's fade-in done */
    atomic_fetch_add(&g_vm->toggles, 1);
    vm_mono(&a, &rest, 3);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int pl = vm_layer("with the sky");
    CHECK(atomic_load(&g_vm->open) && e->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR &&
              e->q[1].type == XR_TYPE_COMPOSITION_LAYER_QUAD && e->q[1].rect[2] == 1920 && pl == 2 &&
              e->layer_count == 3,
          "with the sky: open %u, layer 0 type %d, layer 1 type %d width %d, the panel at %d of %u",
          atomic_load(&g_vm->open), e->type, e->q[1].type, e->q[1].rect[2], pl, e->layer_count);
    app_teardown(&a);
    CHECK(MOCK(mockxr_live)() == 0 && V.buffers_live == 0 && V.mems_live == 0, "objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}

static int sc_vrmenu_pointer(void) { /* the game's pointer on the menu quad: no target while the panel is open */
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_PTR, 1, SOMVR_INPUT_V2_SIZE) || vrmenu_obj(1, VMA_ALL, NULL) || load_layer()) return 1;
    app_t a;
    CHECK(app_setup(&a, "Feral3D") == 0, "setup");
    mockxr_hand R, L;
    ptr_aim(&R, 1, 0.25f, 0.75f, 0);
    float away[3] = {-3, 0, -1};
    ptr_aim_at(&L, 0, away, 0);
    hand_set(1, &R), hand_set(0, &L);
    for (int f = 0; f < 3; f++) app_present(&a);
    CHECK(ptr_pub().quad == SOMVR_PTRQ_MENU && (ptr_pub().flags & SOMVR_PTRF_ON), "the game's pointer on the menu");
    atomic_fetch_add(&g_vm->toggles, 1);
    vmpose aim = {.palm = 1, .left_ok = 1, .right_ok = 1};
    vm_uv(SOMVR_VMI_VIGNETTE, SOMVR_VMP_MORE, aim.uv);
    vm_mono(&a, &aim, 4);
    somvr_ptr_pub p = ptr_pub();
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(atomic_load(&g_vm->open) && p.quad == SOMVR_PTRQ_NONE && p.consume == 0 && e->layer_count == 4 &&
              vm_layer("pointer") == 1 && atomic_load(&g_vm->consume) == 2u,
          "the panel open: the game's pointer quad %u consume 0x%x, %u layers, menu consume 0x%x", p.quad, p.consume,
          e->layer_count, atomic_load(&g_vm->consume));
    int psc = MOCK(mockxr_sc_find)(64, 32);
    CHECK(psc >= 0 && e->q[2].sc == psc && e->q[3].sc == psc && MOCK(mockxr_swapchain_k)(psc)->acquires == 1,
          "the menu's dot and ray from the pointer's one image: sc %d/%d/%d", psc, e->q[2].sc, e->q[3].sc);
    atomic_fetch_add(&g_vm->toggles, 1);
    hand_set(1, &R), hand_set(0, &L);
    for (int f = 0; f < 3; f++) app_present(&a);
    CHECK(!atomic_load(&g_vm->open) && ptr_pub().quad == SOMVR_PTRQ_MENU, "closed: the game's pointer back on the menu");
    app_teardown(&a);
    check_mock_clean();
    return 0;
}

/* ---- build 8: the HUD's tiles (docs/re-xr-pipeline.md 31): the wrist minimap, the wrist HUD, the prompt band ----- */
/* the left grip held like a watch read (the forearm across the chest, palm down): the watch face up toward the eyes;
 * away: palm up (the face down) */
static void wr_hands(int facing) {
    mockxr_hand L, R;
    memset(&L, 0, sizeof L), memset(&R, 0, sizeof R);
    float q[4], yawq[4] = {0, sinf(-0.25f * 3.14159265f), 0, cosf(-0.25f * 3.14159265f)}; /* fingers to the right */
    float rollq[4] = {0, 0, sinf((facing ? -0.25f : 0.25f) * 3.14159265f), cosf(0.25f * 3.14159265f)}; /* palm down/up */
    vm_qmul(yawq, rollq, q);
    float g[7] = {q[0], q[1], q[2], q[3], -0.05f, 1.25f, -0.45f};
    memcpy(L.grip, g, sizeof g), memcpy(L.aim, g, sizeof g);
    L.grip_flags = L.aim_flags = 0xf, L.vel_flags = 3;
    hand_set(0, &L), hand_set(1, &R);
}
/* the expected watch quad for that grip: the face (somvr_mm_pose), moved by dx metres along its own X */
static void wr_expect(int facing, float dx, float out[7]) {
    float q[4], yawq[4] = {0, sinf(-0.25f * 3.14159265f), 0, cosf(-0.25f * 3.14159265f)};
    float rollq[4] = {0, 0, sinf((facing ? -0.25f : 0.25f) * 3.14159265f), cosf(0.25f * 3.14159265f)};
    vm_qmul(yawq, rollq, q);
    somvr_posef g = {{q[0], q[1], q[2], q[3]}, {-0.05f, 1.25f, -0.45f}}, f;
    somvr_mm_cfg c;
    somvr_mm_cfg_default(&c);
    CHECK(somvr_mm_pose(&g, SOMVR_HAND_L, &c, &f), "the expected watch pose");
    float ex[3] = {1, 0, 0}, ax[3];
    vm_qrot(f.q, ex, ax);
    float o[7] = {f.q[0], f.q[1], f.q[2], f.q[3], f.p[0] + dx * ax[0], f.p[1] + dx * ax[1], f.p[2] + dx * ax[2]};
    memcpy(out, o, sizeof o);
}
/* the last HUD copy was the split of layout l (after a clear of the HUD swapchain image), into the image xr */
static int split_is(const somvr_hud_layout *l, const char *what) {
    int32_t g[SOMVR_HUD_SPLIT_MAX][6];
    int n = somvr_hud_split(1920, 1080, l, g, SOMVR_HUD_SPLIT_MAX), same = n == V.nhcp && n > 0;
    for (int i = 0; same && i < n; i++)
        same = V.hcp[i].srcOffset.x == g[i][0] && V.hcp[i].srcOffset.y == g[i][1] && V.hcp[i].dstOffset.x == g[i][2] &&
               V.hcp[i].dstOffset.y == g[i][3] && (int32_t)V.hcp[i].extent.width == g[i][4] &&
               (int32_t)V.hcp[i].extent.height == g[i][5] && V.hcp[i].extent.depth == 1;
    CHECK(same && V.clears > 0 && V.clear_img == V.hcp_d, "%s: the HUD copy is not the split (%d regions, want %d; %d "
          "clears of %p, the copy into %p)", what, V.nhcp, n, V.clears, (void *)V.clear_img, (void *)V.hcp_d);
    return same;
}
/* the layer of the last frame that shows swapchain slot sc with that rect (its index; 0 = none) */
static int layer_with(int sc, int32_t x, int32_t y, int32_t w, int32_t h) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    for (uint32_t i = 1; sc >= 0 && i < e->layer_count && i < MOCK_NQ; i++)
        if (e->q[i].sc == sc && rect_is(e->q[i].rect, x, y, w, h)) return (int)i;
    return 0;
}
static int pose7_near(const float a[7], const float b[7], float eps) {
    for (int i = 0; i < 7; i++)
        if (!(fabsf(a[i] - b[i]) < eps)) return 0;
    return 1;
}
/* build 6's SOMVR_XR_MINIMAP=wrist (gauntlet 0) and build 8's SOMVR_XR_WRIST_HUD=on (1): the HUD swapchain's extra
 * width, a clear and the split every frame, the watch quad on the left wrist while it faces the eyes, the minimap's
 * centre at the face's place (the column toward the elbow); a video frame: one plain copy, no watch */
static int wrist_run(int gauntlet) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE)) return 1;
    wr_hands(1);
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    int mm[4] = SOMVR_MM_RECT_DEFAULT;
    somvr_ht_set wt, ct;
    somvr_ht_parse(NULL, SOMVR_WH_RECTS_DEFAULT, &wt, NULL), somvr_ht_parse(NULL, SOMVR_WH_CLEAR_DEFAULT, &ct, NULL);
    somvr_hud_layout l;
    CHECK(somvr_hud_layout_make(1920, 1080, mm, gauntlet ? &wt : NULL, 0, &ct, NULL, &l) && l.nclear == (gauntlet ? 1 : 0),
          "the expected layout");
    uint32_t extra = gauntlet ? 380 + SOMVR_HT_GAP + 300 : 300;
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) wr_hands(1), hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
    int hsc = MOCK(mockxr_sc_find)(1920 + extra, 1080);
    CHECK(hsc >= 0 && l.extra_w == extra, "the HUD swapchain %ux1080 (slot %d), the layout's extra %u", 1920 + extra, hsc,
          l.extra_w);
    split_is(&l, gauntlet ? "the wrist HUD" : "the wrist minimap");
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int hq = layer_with(hsc, 0, 0, 1920, 1080), wq = layer_with(hsc, l.watch[0], l.watch[1], l.watch[2], l.watch[3]);
    float k = 0.10f / 300, dx = k * (0.5f * (float)l.watch[2] - (float)(l.mm[0] - l.watch[0]) - 150), want[7];
    wr_expect(1, dx, want);
    CHECK(e->layer_count == 3 && hq == 1 && wq == 2 && e->q[2].space == 1 && e->q[2].flags == HUD_FLAGS_STRAIGHT &&
              fabsf(e->q[2].size[0] - k * (float)l.watch[2]) < 1e-5f && fabsf(e->q[2].size[1] - k * 310) < 1e-5f &&
              pose7_near(e->q[2].pose, want, 2e-4f),
          "the watch: %u layers, the HUD's %d, the watch's %d (rect %d,%d %dx%d) size %.4f x %.4f pose %.4f,%.4f,%.4f,%.4f "
          "%.4f,%.4f,%.4f want %.4f,%.4f,%.4f", e->layer_count, hq, wq, e->q[2].rect[0], e->q[2].rect[1], e->q[2].rect[2],
          e->q[2].rect[3], e->q[2].size[0], e->q[2].size[1], e->q[2].pose[0], e->q[2].pose[1], e->q[2].pose[2],
          e->q[2].pose[3], e->q[2].pose[4], e->q[2].pose[5], e->q[2].pose[6], want[4], want[5], want[6]);
    CHECK(gauntlet ? dx < -0.06f && dx > -0.07f : fabsf(dx) < 1e-6f, "the column's shift toward the elbow: %.4f m", dx);
    /* the wrist turned over (palm up): no watch; back: the watch again */
    wr_hands(0), hud_stereo(&r, 4, 2, SOMVR_HUDK_WORLD, &v);
    wr_hands(0), hud_stereo(&r, 5, 2, SOMVR_HUDK_WORLD, &v);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 2 && layer_with(hsc, 0, 0, 1920, 1080) == 1 &&
              log_has("xr: minimap: the watch face away"),
          "palm up: %u layers", MOCK(mockxr_last_end)()->layer_count);
    wr_hands(1), hud_stereo(&r, 6, 2, SOMVR_HUDK_WORLD, &v);
    wr_hands(1), hud_stereo(&r, 7, 2, SOMVR_HUDK_WORLD, &v);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 3 && log_count("xr: minimap: the watch face seen") == 2,
          "turned back: %u layers, %d 'seen' lines", MOCK(mockxr_last_end)()->layer_count,
          log_count("xr: minimap: the watch face seen"));
    /* a video frame: the HUD image copied whole (one region), no watch */
    int cl = V.clears;
    V.nhcp = 0;
    wr_hands(1), hud_stereo(&r, 8, 2, SOMVR_HUDK_VIDEO, &v);
    CHECK(V.nhcp == 0 && V.clears == cl && MOCK(mockxr_last_end)()->layer_count == 2 && V.ncp == 3 &&
              V.cp[2].r.extent.width == 1920 && V.cp[2].r.extent.height == 1080,
          "a video frame: %d split regions, %d clears, %u layers", V.nhcp, V.clears - cl,
          MOCK(mockxr_last_end)()->layer_count);
    CHECK(log_has("xr: minimap: on the wrist (the rect 28,770 300x310") &&
              log_has(gauntlet ? "xr: wrist hud: on (the rects 0,214 260x96; 1540,14 380x104 beside the map, toward the "
                                 "elbow; 1400,14 140x104 cleared from the HUD)"
                               : "xr: wrist hud: off") &&
              log_has("xr: prompts: off"),
          "the switches' lines");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
static int sc_wrist(void) { return wrist_run(0); }
static int sc_wrist_hud(void) { return wrist_run(1); }

/* the band's expected pose from a record's eyes (yaw about +Y only, as the mock's): its middle d cos a ahead of the eyes'
 * midpoint and d sin a below, turned to face it (the head's yaw, then tilted back by a) */
static void pb_expect(const somvr_views *v, float d, float a, float out[7]) {
    float yaw = 2 * atan2f(v->pose[0].q[1], v->pose[0].q[3]);
    float m[3] = {0.5f * (v->pose[0].p[0] + v->pose[1].p[0]), 0.5f * (v->pose[0].p[1] + v->pose[1].p[1]),
                  0.5f * (v->pose[0].p[2] + v->pose[1].p[2])};
    float qy[4] = {0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw)}, qx[4] = {-sinf(0.5f * a), 0, 0, cosf(0.5f * a)}, q[4];
    vm_qmul(qy, qx, q);
    float h = d * cosf(a);
    float o[7] = {q[0], q[1], q[2], q[3], m[0] - h * sinf(yaw), m[1] - d * sinf(a), m[2] - h * cosf(yaw)};
    memcpy(out, o, sizeof o);
}
/* SOMVR_XR_PROMPTS=on (no minimap): the band's tiles in the extra area (the split), the band quad after the HUD quad,
 * world-locked from the HUD's first frame, kept while the head turns, again in a reuse frame, none in a video frame,
 * placed again with the HUD at the runtime's recentre (MOCKXR_RECENTER_AT=10) */
static int sc_prompts(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_ht_set bt;
    somvr_ht_parse(NULL, SOMVR_PB_RECTS_DEFAULT, &bt, NULL);
    somvr_hud_layout l;
    int bh = 110 + SOMVR_HT_GAP + 180 + SOMVR_HT_GAP + 220;
    CHECK(somvr_hud_layout_make(1920, 1080, NULL, NULL, 0, NULL, &bt, &l) && l.extra_w == 1098 &&
              rect_is(l.band, 1920, 0, 1098, bh),
          "the expected band: %d,%d %dx%d", l.band[0], l.band[1], l.band[2], l.band[3]);
    float d = 1.3f, a = 15 * 3.14159265f / 180, wm = somvr_pb_width(1098, 0.85f, 1.3f), at[7], v3at[7];
    somvr_views v, v3;
    for (uint64_t f = 1; f <= 3; f++) {
        hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
        if (f == 1) v3 = v, pb_expect(&v3, d, a, v3at), hud_expect_world(&v3, 1.5f, 0.1f, at);
    }
    int hsc = MOCK(mockxr_sc_find)(1920 + 1098, 1080);
    split_is(&l, "the band");
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int bq = layer_with(hsc, l.band[0], l.band[1], l.band[2], l.band[3]);
    CHECK(hsc >= 0 && e->layer_count == 3 && layer_with(hsc, 0, 0, 1920, 1080) == 1 && bq == 2 && e->q[2].space == 1 &&
              e->q[2].flags == HUD_FLAGS_STRAIGHT && e->q[2].eyes == XR_EYE_VISIBILITY_BOTH &&
              fabsf(e->q[2].size[0] - wm) < 1e-5f && fabsf(e->q[2].size[1] - wm * (float)bh / 1098) < 1e-5f &&
              pose7_near(e->q[2].pose, v3at, 2e-5f) && pose_near(e->q[1].pose, at),
          "the band: %u layers, at %d (sc %d) size %.4f x %.4f pose %.4f,%.4f,%.4f,%.4f %.4f,%.4f,%.4f want %.4f,%.4f,%.4f,"
          "%.4f %.4f,%.4f,%.4f", e->layer_count, bq, hsc, e->q[2].size[0], e->q[2].size[1], e->q[2].pose[0],
          e->q[2].pose[1], e->q[2].pose[2], e->q[2].pose[3], e->q[2].pose[4], e->q[2].pose[5], e->q[2].pose[6], v3at[0],
          v3at[1], v3at[2], v3at[3], v3at[4], v3at[5], v3at[6]);
    CHECK(fabsf(v.pose[0].q[1] - v3.pose[0].q[1]) > 1e-4f, "the mock's head did not turn between frames 1 and 3");
    /* 18 px of the band at its middle: 0.91 deg (the HUD quad's: 0.42) */
    float mid = 2 * atanf(0.5f * 18 * wm / 1098 / d) * 57.29578f, hud18 = 2 * atanf(0.5f * 18 * 1.2f / 1920 / 1.5f) * 57.29578f;
    CHECK(mid > 0.85f && hud18 < 0.43f, "18 px: %.3f deg in the band, %.3f in the HUD", mid, hud18);
    /* a reuse frame: both again, no copy */
    V.ncp = 0;
    hud_present(&r.a, 99, 2, SOMVR_HUDK_WORLD);
    e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 3 && e->q[2].sc == hsc && pose7_near(e->q[2].pose, v3at, 2e-5f) && V.ncp == 0,
          "reuse: %u layers, %d copies", e->layer_count, V.ncp);
    /* a video frame: no band, one plain copy */
    hud_stereo(&r, 4, 2, SOMVR_HUDK_VIDEO, &v);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 2 && V.cp[2].r.extent.width == 1920, "a video frame: %u layers",
          MOCK(mockxr_last_end)()->layer_count);
    /* frames past the recentre (after xrEndFrame 10): placed again with the HUD from one frame's eyes */
    float now[7], fat[6][7];
    for (uint64_t f = 5; f <= 10; f++) hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v), pb_expect(&v, d, a, fat[f - 5]);
    e = MOCK(mockxr_last_end)();
    int from = -1;
    for (int i = 0; i < 6; i++)
        if (pose7_near(e->q[2].pose, fat[i], 2e-5f)) from = i + 5, memcpy(now, fat[i], sizeof now);
    CHECK(e->layer_count == 3 && from >= 8 && !pose7_near(now, v3at, 1e-3f) &&
              log_count("xr: menus: the prompt band placed at") == 2 && log_count("xr: hud quad placed for frame") == 2,
          "after the recentre: the band placed %d times (the HUD %d), from frame %d's eyes",
          log_count("xr: menus: the prompt band placed at"),
          log_count("xr: hud quad placed for frame"), from);
    CHECK(log_has("xr: prompts: on (the rects 640,10 640x110 | 440,600 1040x180 | 30,390 290x190; 1120,860 800x220 in one "
                  "band 1.30 m from the eyes, 15 deg below them, an 18-px line 0.85 deg") &&
              log_has("xr: prompts: the band shown (frame") && log_has("xr: wrist hud: off") &&
              log_has("xr: minimap: off (it stays in the HUD)"),
          "the band's lines");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0, "XR objects alive after vkDestroyDevice");
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_HUD=head SOMVR_XR_PROMPTS_QUAD=1.0,1.0,20: the band in the HUD's VIEW space, 20 deg below the view */
static int sc_prompts_head(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    float a = 20 * 3.14159265f / 180, wm = somvr_pb_width(1098, 1.0f, 1.0f);
    float want[7] = {-sinf(0.5f * a), 0, 0, cosf(0.5f * a), 0, -sinf(a), -cosf(a)};
    CHECK(e->layer_count == 3 && e->q[1].space == 2 && e->q[2].space == 2 && e->q[2].view == e->q[1].view &&
              fabsf(e->q[2].size[0] - wm) < 1e-5f && pose7_near(e->q[2].pose, want, 1e-5f) &&
              !log_has("the prompt band placed at"),
          "head-locked band: %u layers, spaces %d/%d, size %.4f, pose %.4f,%.4f,%.4f,%.4f %.4f,%.4f,%.4f", e->layer_count,
          e->q[1].space, e->q[2].space, e->q[2].size[0], e->q[2].pose[0], e->q[2].pose[1], e->q[2].pose[2],
          e->q[2].pose[3], e->q[2].pose[4], e->q[2].pose[5], e->q[2].pose[6]);
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}
/* MOCKXR_MAX_LAYERS=2: the system takes two layers; the projection, the HUD and the band would be three: the band goes
 * (logged), the frame is accepted (no rejection, nothing off) */
static int sc_prompts_cap(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    for (uint64_t f = 1; f <= 4; f++) hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    CHECK(e->layer_count == 2 && layer_with(MOCK(mockxr_sc_find)(1920 + 1098, 1080), 0, 0, 1920, 1080) == 1 &&
              log_has("xr: system \"mock HMD\": at most 2 composition layers (fewer than the spec's 16") &&
              log_has("xr: prompts: a frame of 3 layers (the runtime takes 2): without the band (1 so far)") &&
              !log_has("hud off") && !log_has("rejected the frame"),
          "capped: %u layers", e->layer_count);
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}
/* bad values: SOMVR_XR_PROMPTS=yes, SOMVR_XR_WRIST_HUD=2, SOMVR_XR_PROMPTS_RECTS=1,2: logged, both off, the HUD as
 * without them (one plain copy, no extra width) */
static int sc_prompts_badcfg(void) {
    hudrig r;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) hud_stereo(&r, f, 2, SOMVR_HUDK_WORLD, &v);
    float at[7];
    hud_expect_world(&v, 1.5f, 0.1f, at);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 2 && MOCK(mockxr_sc_find)(1920, 1080) >= 0 && V.clears == 0, "bad "
          "switches: %u layers, the HUD swapchain %d, %d clears (a split)", MOCK(mockxr_last_end)()->layer_count,
          MOCK(mockxr_sc_find)(1920, 1080), V.clears);
    CHECK(
              log_has("xr: prompts: SOMVR_XR_PROMPTS=yes is not on or off: off") &&
              log_has("xr: wrist hud: SOMVR_XR_WRIST_HUD=2 is not on or off: off") &&
              log_has("xr: prompts: SOMVR_XR_PROMPTS_RECTS=1,2 is not X,Y,W,H") && log_has("xr: system \"mock HMD\": at "
                                                                                         "most 16 composition layers"),
          "bad switches: %u layers", MOCK(mockxr_last_end)()->layer_count);
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}

/* ---- build 10: our VR hands (docs/re-xr-pipeline.md 33): the preload's somvr_vrhands, a projection layer of ours -- */
static somvr_vrhands_shared *g_vho;
/* vrhands_obj.so next to the mock, in the global scope like LD_PRELOAD (absent in every other scenario) */
static int vrhands_obj(int active) {
    char p[4200];
    const char *slash = strrchr(g_mock, '/');
    snprintf(p, sizeof p, "%.*s/vrhands_obj.so", slash ? (int)(slash - g_mock) : 1, slash ? g_mock : ".");
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (!h || !(g_vho = (somvr_vrhands_shared *)dlsym(h, SOMVR_VRHANDS_SYMBOL)))
        return fprintf(stderr, "    vrhands_obj.so: %s\n", dlerror()), -1;
    g_vho->magic = SOMVR_VRHANDS_MAGIC;
    g_vho->version = SOMVR_VRHANDS_VERSION;
    g_vho->size = (uint32_t)sizeof *g_vho;
    g_vho->pid = (int32_t)getpid();
    if (active) atomic_store(&g_vho->active, 1);
    return 0;
}
static float g_vh_k; /* build 11: the entries' depth_k (0: none) */
static float g_vh_ghost; /* build 13: the right hand's ghost opacity in the entries (0: none) */
/* one layout-B stereo frame whose H4 published our hands' entry (flags; put 0: no entry for it) */
static void vh_stereo(hudrig *r, uint64_t f, uint32_t flags, int put, somvr_views *v) {
    CHECK(locate(v), "frame %llu: locate", (unsigned long long)f);
    write_record(f, SOMVR_LAYOUT_B, v, 3);
    somvr_vrhands_frame e;
    memset(&e, 0, sizeof e);
    e.flags = flags, e.time = v->time, e.depth_k = g_vh_k;
    for (int h = 0; h < 2; h++) {
        e.grip[h].q[3] = 1;
        e.grip[h].p[0] = v->pose[0].p[0] + (h ? 0.15f : -0.15f), e.grip[h].p[1] = v->pose[0].p[1] - 0.3f;
        e.grip[h].p[2] = v->pose[0].p[2] - 0.35f;
        for (int k = 0; k < 5; k++) e.curl[h][k] = 0.5f;
    }
    if (g_vh_ghost > 0) { /* build 13: the real right hand 12 cm past the drawn one (into a wall) */
        e.ghost[1] = e.grip[1], e.ghost[1].p[2] -= 0.12f, e.ghost_a[1] = g_vh_ghost;
    }
    if (put) somvr_vrhands_put(g_vho, f, &e);
    V.ncp = V.ntr = 0, V.draws = V.rp_begins = V.resolves = 0, V.draw_idx = 0, V.blits = 0;
    V.nds_view = V.npc = V.ds_binds = 0;
    hud_present(&r->a, f, 2, SOMVR_HUDK_WORLD);
}
/* the last frame: the projection, the HUD quad and our hands' projection (its own swapchain, the record's views,
 * premultiplied), drawn twice per eye (both hands) and resolved into it once per eye */
static void vh_check(const somvr_views *v, int hands, const char *what) {
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int sc = e->pj_sc[0] >= 2 ? e->pj_sc[0] : -1; /* (0 the eyes', 1 the HUD's: the same size as the eyes' is ours) */
    if (!hands) {
        CHECK(e->layer_count == 2 && e->pj_index == 0 && V.draws == 0, "%s: no hands: %u layers, a projection at %d, %d "
              "draws", what, e->layer_count, e->pj_index, V.draws);
        return;
    }
    CHECK(e->layer_count == 3 && e->pj_index == 2 && sc >= 0 && e->pj_sc[0] == sc && e->pj_sc[1] == sc &&
              e->pj_flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
          "%s: %u layers, our projection at %d on swapchain %d/%d (ours %d) flags 0x%llx", what, e->layer_count,
          e->pj_index, e->pj_sc[0], e->pj_sc[1], sc, (unsigned long long)e->pj_flags);
    for (int k = 0; k < 2; k++) {
        float pose[7];
        memcpy(pose, v->pose[k].q, 16), memcpy(pose + 4, v->pose[k].p, 12);
        CHECK(!memcmp(e->pj_pose[k], pose, sizeof pose) && !memcmp(e->pj_rect[k], RECT_B[k], sizeof RECT_B[k]),
              "%s: view %d: the record's pose and the eye's rect (%d,%d %dx%d)", what, k, e->pj_rect[k][0],
              e->pj_rect[k][1], e->pj_rect[k][2], e->pj_rect[k][3]);
    }
    CHECK(V.rp_begins == 2 && V.draws == 2 * hands && V.resolves == 2 && V.draw_idx > 2 * hands * 6000 && !V.rp_open,
          "%s: %d render passes, %d draws (%ld indices), %d resolves", what, V.rp_begins, V.draws, V.draw_idx, V.resolves);
    CHECK(V.blits == 1, "%s: eye 0 blitted down once for the scene's brightness (%d blits)", what, V.blits);
}
static int sc_vrhands(void) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrhands_obj(1)) return 1;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    CHECK(atomic_load(&g_vho->layer_state) == SOMVR_VHL_UNKNOWN && log_has("xr: hands: somvr_vrhands found at"),
          "set up: state %u", atomic_load(&g_vho->layer_state));
    somvr_views v;
    /* the first stereo frame makes the swapchain and the drawing; the game had not hidden its hands yet */
    vh_stereo(&r, 1, SOMVR_VHF_L | SOMVR_VHF_R, 1, &v);
    vh_check(&v, 0, "the game's hands");
    CHECK(atomic_load(&g_vho->layer_state) == SOMVR_VHL_DRAWING && log_has("xr: hands: ready: a 3664x1920 swapchain"),
          "ready: state %u", atomic_load(&g_vho->layer_state));
    /* from then on the game hides its hands: ours, both, every frame */
    vh_stereo(&r, 2, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
    vh_check(&v, 2, "both hands");
    CHECK(V.dsl_made == 0 && V.ds_binds == 0 && V.npc == 4 && V.pc_eyew[3] == 1.0f &&
              log_has("xr: hands: SOMVR_XR_HANDS_DEPTH=off: the world does not hide them (build 10)"),
          "build 11's switch unset: build 10's pipeline alone (%d set layouts, %d binds, %d pushes)", V.dsl_made,
          V.ds_binds, V.npc);
    vh_stereo(&r, 3, SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
    vh_check(&v, 1, "the right hand alone");
    /* a frame without an entry: the last one's hands; a reuse frame: the last layer again, nothing drawn */
    vh_stereo(&r, 4, 0, 0, &v);
    vh_check(&v, 1, "no entry");
    V.draws = 0;
    hud_present(&r.a, 77, 1, SOMVR_HUDK_WORLD);
    CHECK(MOCK(mockxr_last_end)()->layer_count == 3 && MOCK(mockxr_last_end)()->pj_index == 2 && V.draws == 0,
          "reuse: %u layers, our projection at %d, %d draws", MOCK(mockxr_last_end)()->layer_count,
          MOCK(mockxr_last_end)()->pj_index, V.draws);
    /* third person (the entry without HIDE): the game's hands */
    vh_stereo(&r, 5, SOMVR_VHF_L | SOMVR_VHF_R, 1, &v);
    vh_check(&v, 0, "third person");
    vh_stereo(&r, 6, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
    vh_check(&v, 2, "first person again");
    CHECK(atomic_load(&g_vho->n_drawn) == 4 && atomic_load(&g_vho->last_drawn) == 6, "drawn %llu, last %llu",
          (unsigned long long)atomic_load(&g_vho->n_drawn), (unsigned long long)atomic_load(&g_vho->last_drawn));
    CHECK(log_has("xr: hands: frame 2: ours (the game hid its hands)") &&
              log_has("xr: hands: frame 5: the game's own (not first person, or not yet)"),
          "the switch lines");
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && V.gfx_live == 0 && atomic_load(&g_vho->layer_state) == SOMVR_VHL_OFF,
          "after vkDestroyDevice: XR objects %d, drawing objects %d, state %u", MOCK(mockxr_live)(), V.gfx_live,
          atomic_load(&g_vho->layer_state));
    check_mock_clean();
    return 0;
}
/* build 13 (SOMVR_HANDS_SURFACE, re-xr-pipeline.md 38): an entry with the right hand's ghost (somvr_vrhands v3): the
 * ghost skinned at the real grip and drawn once more per eye after the hands; its on/off lines; without it as before */
static int sc_vrhands_ghost(void) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrhands_obj(1)) return 1;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    vh_stereo(&r, 1, SOMVR_VHF_L | SOMVR_VHF_R, 1, &v);
    g_vh_ghost = 0.3f;
    vh_stereo(&r, 2, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
    CHECK(V.rp_begins == 2 && V.draws == 6 && V.resolves == 2 &&
              log_has("xr: hands: frame 2: ghost L off R on (opacity 0.00 0.30; SOMVR_HANDS_SURFACE)"),
          "the ghost: %d render passes, %d draws", V.rp_begins, V.draws);
    g_vh_ghost = 0;
    vh_stereo(&r, 3, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
    vh_check(&v, 2, "no ghost again");
    CHECK(log_has("xr: hands: frame 3: ghost L off R off"), "the ghost's off line");
    app_teardown(&r.a);
    check_mock_clean();
    return 0;
}
/* never a void: SOMVR_XR_LOADSPACE with SOMVR_VRHANDS - every menu frame gets our hands from the layer's own input
 * (the game publishes no hands outside first person): a projection layer from the hands' swapchain over the board,
 * drawn each frame at the located eyes */
static int sc_void_hands(void) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrhands_obj(1)) return 1;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    vh_stereo(&r, 1, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v); /* the drawing made */
    mockxr_hand right = HAND_R, left = HAND_R; /* in front of the mock's eyes (1.6 m up, 0.1 m back) */
    right.grip[4] = 0.2f, right.grip[5] = 1.3f, right.grip[6] = -0.5f;
    left.grip[4] = -0.2f, left.grip[5] = 1.3f, left.grip[6] = -0.5f, left.buttons = 0, left.trigger = 0, left.squeeze = 1;
    hand_set(0, &left);
    hand_set(1, &right);
    int draws = V.draws;
    for (int i = 0; i < 40; i++) app_present(&r.a);
    const mockxr_end *e = MOCK(mockxr_last_end)();
    int ks = MOCK(mockxr_sc_find)(SOMVR_VOID_IMG_W, SOMVR_VOID_IMG_H);
    CHECK(ks >= 0 && e->q[1].sc == ks && e->pj_index >= 3 && e->pj_sc[0] >= 2 && e->pj_sc[0] == e->pj_sc[1] &&
              e->pj_flags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT && V.draws > draws + 20,
          "a menu frame: the floor in %d (want %d), our hands' projection at %d (sc %d/%d, flags 0x%llx), %d draws",
          e->q[1].sc, ks, e->pj_index, e->pj_sc[0], e->pj_sc[1], (unsigned long long)e->pj_flags, V.draws - draws);
    app_teardown(&r.a);
    CHECK(log_has("xr: hands in the loading space: LR hand(s) from the layer's own input"), "log lacks the hands line");
    CHECK(MOCK(mockxr_live)() == 0 && V.gfx_live == 0, "after vkDestroyDevice: XR objects %d, drawing objects %d",
          MOCK(mockxr_live)(), V.gfx_live);
    check_mock_clean();
    return 0;
}
/* SOMVR_XR_HANDS=off (1), a driver without vkCreateRenderPass (2), an inactive object (3): the game's hands, nothing of
 * ours, the frames go on */
static int vh_none(int how, const char *line) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrhands_obj(how != 3)) return 1;
    V.no_gfx = how == 2;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    for (uint64_t f = 1; f <= 3; f++) {
        vh_stereo(&r, f, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
        vh_check(&v, 0, "no hands");
    }
    CHECK(log_has(line), "the line '%s'", line);
    CHECK(how == 3 || atomic_load(&g_vho->layer_state) == SOMVR_VHL_OFF, "state %u", atomic_load(&g_vho->layer_state));
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && V.gfx_live == 0, "after vkDestroyDevice: XR %d, drawing %d", MOCK(mockxr_live)(),
          V.gfx_live);
    check_mock_clean();
    return 0;
}
static int sc_vrhands_off(void) { return vh_none(1, "xr: hands: the game's own hands (SOMVR_XR_HANDS=off)"); }

/* ---- build 11: the world hides our hands (SOMVR_XR_HANDS_DEPTH=world; docs/re-xr-pipeline.md 33.8) ------------------- */
#define VH_DEPTH_USAGE ((FERAL_USAGE & ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
/* the preload's two depth textures (made by the game's device at the eye size, D32S8) */
static void vh_depth_images(hudrig *r, VkImage out[2], VkImageUsageFlags use) {
    V.eye_fmt = VK_FORMAT_D32_SFLOAT_S8_UINT;
    out[0] = app_image(&r->a, 1832, 1920, use), out[1] = app_image(&r->a, 1832, 1920, use);
    V.eye_fmt = VK_FORMAT_UNDEFINED;
}
static void vh_depth_info(uint32_t gen, VkImage d0, VkImage d1, uint64_t frame) {
    somvr_eyes_info in = {gen, 1832, 1920, 19, {(uint64_t)(uintptr_t)d0, (uint64_t)(uintptr_t)d1}};
    somvr_vrhands_depth_publish(g_vho, &in);
    atomic_store(&g_vho->dframe[0], frame), atomic_store(&g_vho->dframe[1], frame);
}
/* this frame sampled the depth textures: each taken from TRANSFER_DST to SHADER_READ_ONLY (depth and stencil) and given
 * back; eye e's sets name a depth view of D[e]; the hands' pushes carry k (w 1 = with the world: k, else 0) */
static void vh_depth_check(const VkImage D[2], int world, float k, const char *what) {
    int moved = 1, back = 1, aspects = 1;
    for (int e = 0; e < 2; e++) {
        moved &= barrier_seen(D[e], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        back &= barrier_seen(D[e], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for (int i = 0; i < V.ntr; i++)
            if (V.tr[i].img == D[e])
                aspects &= V.tr_aspect[i] == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    }
    if (!world) {
        int touched = 0;
        for (int i = 0; i < V.ntr; i++) touched += V.tr[i].img == D[0] || V.tr[i].img == D[1];
        CHECK(touched == 0, "%s: the depth textures untouched (%d barriers)", what, touched);
    } else
        CHECK(moved && back && aspects, "%s: the depth textures sampled and returned to TRANSFER_DST (depth+stencil "
              "barriers: to %d, back %d, aspects %d)", what, moved, back, aspects);
    int views_ok = V.nds_view == 2;
    for (int e = 0; e < 2 && views_ok; e++) {
        VkImageAspectFlags asp = 0;
        VkImage img = view_image(V.ds_view[e], &asp);
        views_ok = world ? img == D[e] && asp == VK_IMAGE_ASPECT_DEPTH_BIT : img != D[0] && img != D[1];
    }
    CHECK(views_ok && V.ds_binds == 2, "%s: each eye's set names %s (%d writes, %d binds)", what,
          world ? "its own depth texture's depth aspect" : "the zero image", V.nds_view, V.ds_binds);
    int pcs = V.npc == 4;
    for (int i = 0; i < V.npc; i++) pcs &= V.pc_eyew[i] == (world ? k : 0.0f);
    CHECK(pcs, "%s: the pushes' eye.w %s (%d pushes: %g %g %g %g)", what, world ? "= depth_k" : "= 0 (no world)", V.npc,
          (double)V.pc_eyew[0], (double)V.pc_eyew[1], (double)V.pc_eyew[2], (double)V.pc_eyew[3]);
}
static int sc_vrhands_depth(void) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrhands_obj(1)) return 1;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    CHECK(log_has("xr: hands: SOMVR_XR_HANDS_DEPTH=world: the world hides them (each eye's game depth; the edge 1.5 cm "
                  "wide, its centre 0.8 cm behind the surface; a hand holding a weapon"), "the switch's line");
    somvr_views v;
    const uint32_t LRH = SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE;
    g_vh_k = 0.06f;
    vh_stereo(&r, 1, SOMVR_VHF_L | SOMVR_VHF_R, 1, &v);
    CHECK(log_has("; the world's depth sampled per eye") && V.dsl_made == 1 && V.smp_made == 1,
          "made with the world's pipeline (%d set layouts, %d samplers)", V.dsl_made, V.smp_made);
    VkImage D[2], N[2];
    vh_depth_images(&r, D, VH_DEPTH_USAGE);
    /* no textures published yet: build 10's look, the zero image bound */
    vh_stereo(&r, 2, LRH, 1, &v);
    vh_check(&v, 2, "no depth textures yet");
    vh_depth_check(D, 0, 0, "no depth textures yet");
    CHECK(log_has("xr: hands: frame 2: the world does not hide them: the preload has no depth textures yet"), "its line");
    /* published, both eyes copied for frame 3: sampled */
    vh_depth_info(1, D[0], D[1], 3);
    vh_stereo(&r, 3, LRH, 1, &v);
    vh_check(&v, 2, "the world's depth");
    vh_depth_check(D, 1, 0.06f, "frame 3");
    CHECK(log_has("xr: hands: the world's depth: gen 1: 1832x1920 format 130") &&
              log_has("xr: hands: frame 3: the world hides them (depth gen 1, depth_k 0.0600 m") &&
              atomic_load(&g_vho->n_dused) == 1,
          "the views' and the switch's lines, n_dused %llu", (unsigned long long)atomic_load(&g_vho->n_dused));
    /* the textures still hold frame 3: not this frame's world */
    vh_stereo(&r, 4, LRH, 1, &v);
    vh_check(&v, 2, "an old depth");
    vh_depth_check(D, 0, 0, "frame 4 (the depth of frame 3)");
    CHECK(log_has("xr: hands: frame 4: the world does not hide them: the depth textures do not hold this frame"), "its line");
    /* one eye copied only */
    atomic_store(&g_vho->dframe[0], 5);
    vh_stereo(&r, 5, LRH, 1, &v);
    vh_depth_check(D, 0, 0, "frame 5 (eye 1's depth old)");
    /* an asynchronous present: the render thread already ended the next frame's first eye: still sampled */
    atomic_store(&g_vho->dframe[0], 6), atomic_store(&g_vho->dframe[1], 5);
    vh_stereo(&r, 5, LRH, 1, &v);
    vh_depth_check(D, 1, 0.06f, "frame 5 (eye 0's depth the next frame's)");
    atomic_store(&g_vho->dframe[0], 7), atomic_store(&g_vho->dframe[1], 5);
    vh_stereo(&r, 5, LRH, 1, &v);
    vh_depth_check(D, 0, 0, "frame 5 (eye 0's depth two frames on)");
    /* this frame's depth, no depth scale in the entry (an older preload / its switch off) */
    vh_depth_info(1, D[0], D[1], 6);
    g_vh_k = 0;
    vh_stereo(&r, 6, LRH, 1, &v);
    vh_depth_check(D, 0, 0, "frame 6 (no depth_k)");
    CHECK(log_has("the world does not hide them: the entry has no depth scale"), "its line");
    g_vh_k = 0.06f;
    vh_depth_info(1, D[0], D[1], 7);
    vh_stereo(&r, 7, LRH, 1, &v);
    vh_depth_check(D, 1, 0.06f, "frame 7");
    /* a reuse frame repeats the layer, nothing sampled */
    V.ntr = 0, V.draws = 0, V.nds_view = V.npc = V.ds_binds = 0;
    hud_present(&r.a, 77, 1, SOMVR_HUDK_WORLD); /* (no record for it) */
    int touched = 0;
    for (int i = 0; i < V.ntr; i++) touched += V.tr[i].img == D[0] || V.tr[i].img == D[1];
    CHECK(touched == 0 && V.draws == 0 && V.nds_view == 0 && V.npc == 0, "the reuse frame: nothing drawn or sampled (%d "
          "barriers on the depth, %d draws)", touched, V.draws);
    /* new textures without SAMPLED (gen 2): the hands drawn without the world, once logged */
    vh_depth_images(&r, N, VH_DEPTH_USAGE & ~VK_IMAGE_USAGE_SAMPLED_BIT);
    vh_depth_info(2, N[0], N[1], 8);
    vh_stereo(&r, 8, LRH, 1, &v);
    vh_check(&v, 2, "depth textures without SAMPLED");
    vh_depth_check(N, 0, 0, "frame 8 (without SAMPLED)");
    CHECK(log_has("xr: hands: the world's depth: gen 2: usage 0x") && log_has("without SAMPLED: the hands are not hidden"),
          "the no-SAMPLED line");
    /* gen 3 (a resolution change): sampled again */
    vh_depth_info(3, D[0], D[1], 9);
    vh_stereo(&r, 9, LRH, 1, &v);
    vh_depth_check(D, 1, 0.06f, "frame 9 (gen 3)");
    CHECK(atomic_load(&g_vho->n_dused) == 4, "n_dused %llu", (unsigned long long)atomic_load(&g_vho->n_dused));
    /* the right hand holds the sword: its own push reads the world as Talion's held items (x1.17), the left's as is */
    vh_depth_info(3, D[0], D[1], 10);
    vh_stereo(&r, 10, LRH | SOMVR_VHF_HOLD_R, 1, &v);
    CHECK(V.npc == 6 && V.pc_basea[0] == 0 && V.pc_basea[1] == 1.0f && fabsf(V.pc_basea[2] - 1.17f) < 1e-6f &&
              V.pc_basea[3] == 0 && V.pc_basea[4] == 1.0f && fabsf(V.pc_basea[5] - 1.17f) < 1e-6f && V.draws == 4,
          "the holding hand's push (%d pushes: %g %g %g / %g %g %g)", V.npc, (double)V.pc_basea[0], (double)V.pc_basea[1],
          (double)V.pc_basea[2], (double)V.pc_basea[3], (double)V.pc_basea[4], (double)V.pc_basea[5]);
    /* both hold (two hands on the hilt): one push each eye, x1.17 */
    vh_depth_info(3, D[0], D[1], 11);
    vh_stereo(&r, 11, LRH | SOMVR_VHF_HOLD_R | SOMVR_VHF_HOLD_L, 1, &v);
    CHECK(V.npc == 4 && fabsf(V.pc_basea[1] - 1.17f) < 1e-6f && fabsf(V.pc_basea[3] - 1.17f) < 1e-6f,
          "both holding: one push x1.17 (%d pushes)", V.npc);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && V.gfx_live == 0 && atomic_load(&g_vho->layer_state) == SOMVR_VHL_OFF,
          "after vkDestroyDevice: XR objects %d, drawing objects %d (views, sets, sampler), state %u",
          MOCK(mockxr_live)(), V.gfx_live, atomic_load(&g_vho->layer_state));
    check_mock_clean();
    return 0;
}
/* the world wanted, the driver without descriptor sets: the hands as build 10 (drawn, the plain pipeline) */
static int sc_vrhands_depth_nofuncs(void) {
    hudrig r;
    preload_on(SOMVR_LAYOUT_B, SOMVR_SHARED_VERSION);
    if (input_obj(INW_ALL, 1, SOMVR_INPUT_V2_SIZE) || vrhands_obj(1)) return 1;
    V.no_world = 1;
    if (hud_setup(&r, 2)) return 1;
    hud_info(2, 1920, 1080, r.H);
    somvr_views v;
    g_vh_k = 0.06f;
    VkImage D[2];
    vh_depth_images(&r, D, VH_DEPTH_USAGE);
    vh_depth_info(1, D[0], D[1], 2);
    vh_stereo(&r, 1, SOMVR_VHF_L | SOMVR_VHF_R, 1, &v);
    vh_stereo(&r, 2, SOMVR_VHF_L | SOMVR_VHF_R | SOMVR_VHF_HIDE, 1, &v);
    vh_check(&v, 2, "the hands without the world");
    CHECK(log_has("xr: hands: the world's depth: off (the device has no vkCreateDescriptorSetLayout)") &&
              V.dsl_made == 0 && V.ds_binds == 0 && V.npc == 4 && V.pc_eyew[3] == 1.0f &&
              !barrier_seen(D[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
          "the plain pipeline (%d layouts, %d binds, %d pushes)", V.dsl_made, V.ds_binds, V.npc);
    app_teardown(&r.a);
    CHECK(MOCK(mockxr_live)() == 0 && V.gfx_live == 0, "after vkDestroyDevice: XR %d, drawing %d", MOCK(mockxr_live)(),
          V.gfx_live);
    check_mock_clean();
    return 0;
}
static int sc_vrhands_nofuncs(void) {
    return vh_none(2, "xr: hands: off for this session: the device has no vkCreateRenderPass");
}
static int sc_vrhands_inactive(void) {
    return vh_none(3, "xr: hands: the game's own hands (somvr_vrhands inactive (SOMVR_VRHANDS unset)");
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
    {"stereo-a", sc_stereo_a, "SOMVR_XR_LOADER=@MOCK", 1},
    {"stereo-cover", sc_stereo_cover, "SOMVR_XR_LOADER=@MOCK", 1},
    {"stereo-reuse", sc_stereo_reuse, "SOMVR_XR_LOADER=@MOCK", 1},
    {"stereo-untagged", sc_stereo_untagged, "SOMVR_XR_LOADER=@MOCK", 1},
    {"stereo-badversion", sc_stereo_badversion, "SOMVR_XR_LOADER=@MOCK", 1},
    {"stereo-b", sc_stereo_b, "SOMVR_XR_LOADER=@MOCK", 1},
    {"dump", sc_dump, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=2", 1},
    {"dump-bgra", sc_dump_bgra, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=2 MOCKXR_FORMATS=50", 1},
    {"dump-nobuffer", sc_dump_nobuffer, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=2", 1},
    {"dump-nomemory", sc_dump_nomemory, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=2", 1},
    {"dump-nolog", sc_dump_nolog, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=2 SOMVR_XR_LOG=", 1},
    {"predict", sc_predict, "SOMVR_XR_LOADER=@MOCK", 1},
    {"predict-off", sc_predict_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_PREDICT=off", 1},
    {"predict-fixed", sc_predict_fixed, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_PREDICT=5", 1},
    {"quad-guard", sc_quad_guard, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=m3,1", 1},
    {"stereo-fifo", sc_stereo_fifo, "SOMVR_XR_LOADER=@MOCK", 1},
    {"provider-teardown", sc_provider_teardown, "SOMVR_XR_LOADER=@MOCK", 1},
    {"stage", sc_stage, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_SPACE=stage", 1},
    {"stage-missing", sc_stage_missing, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_SPACE=stage MOCKXR_NO_STAGE=1", 1},
    {"floor", sc_floor, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_FLOOR=3 MOCKXR_FLOOR_CM=123", 1},
    {"floor-none", sc_floor_none, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_FLOOR=1 MOCKXR_NO_STAGE=1", 1},
    {"recenter-event", sc_recenter_event, "SOMVR_XR_LOADER=@MOCK MOCKXR_RECENTER_AT=2", 1},
    {"input", sc_input, "SOMVR_XR_LOADER=@MOCK", 1},
    {"input-unfocused", sc_input_unfocused, "SOMVR_XR_LOADER=@MOCK MOCKXR_FOCUS_AT=3 MOCKXR_UNFOCUS_AT=6", 1},
    {"input-profile-index", sc_input_profile_index,
     "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrSuggestInteractionProfileBindings:1 MOCKXR_PROFILE=index", 1},
    {"input-profile-simple", sc_input_profile_simple,
     "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrSuggestInteractionProfileBindings:1 MOCKXR_PROFILE=touch", 1},
    {"input-profile-off", sc_input_profile_off, "SOMVR_XR_LOADER=@MOCK MOCKXR_PROFILE_OFF_AT=3", 1},
    {"input-attach-fail", sc_input_attach_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrAttachSessionActionSets", 1},
    {"input-action-fail", sc_input_action_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrCreateAction:5", 1},
    {"input-space-fail", sc_input_space_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrCreateActionSpace:3", 1},
    {"input-absent", sc_input_absent, "SOMVR_XR_LOADER=@MOCK", 1},
    {"input-want0", sc_input_want0, "SOMVR_XR_LOADER=@MOCK", 1},
    {"input-inactive", sc_input_inactive, "SOMVR_XR_LOADER=@MOCK", 1},
    {"input-off", sc_input_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_INPUT=off", 1},
    {"input-short", sc_input_short, "SOMVR_XR_LOADER=@MOCK", 1},
    {"input-future", sc_input_future, "SOMVR_XR_LOADER=@MOCK", 1},
    {"input-log", sc_input_log, "SOMVR_XR_LOADER=@MOCK SOMVR_XR=force SOMVR_XR_INPUT=log", 1},
    {"input-teardown", sc_input_teardown, "SOMVR_XR_LOADER=@MOCK", 1},
    {"hud", sc_hud, "SOMVR_XR_LOADER=@MOCK MOCKXR_RECENTER_AT=10", 1},
    {"hud-head", sc_hud_head, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HUD=head", 1},
    {"hud-premul", sc_hud_premul, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HUD_ALPHA=premul SOMVR_XR_HUD_QUAD=2.5,1.6,-0.2", 1},
    {"hud-badcfg", sc_hud_badcfg, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HUD=sideways SOMVR_XR_HUD_ALPHA=x SOMVR_XR_HUD_QUAD=1,2", 1},
    {"hud-absent", sc_hud_absent, "SOMVR_XR_LOADER=@MOCK", 1},
    {"hud-inactive", sc_hud_inactive, "SOMVR_XR_LOADER=@MOCK", 1},
    {"hud-off", sc_hud_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HUD=off", 1},
    {"hud-swapchain-fail", sc_hud_swapchain_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrCreateSwapchain:3", 1},
    {"hud-acquire-fail", sc_hud_acquire_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrAcquireSwapchainImage:3", 1},
    {"hud-reject", sc_hud_reject, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_HUD=1", 1},
    {"pointer", sc_ptr, "SOMVR_XR_LOADER=@MOCK", 1},
    {"pointer-dot", sc_ptr_dot, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_POINTER=dot", 1},
    {"pointer-unwanted", sc_ptr_unwanted, "SOMVR_XR_LOADER=@MOCK", 1},
    {"pointer-v1", sc_ptr_v1, "SOMVR_XR_LOADER=@MOCK", 1},
    {"pointer-off", sc_ptr_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_POINTER=off", 1},
    {"pointer-acquire-fail", sc_ptr_acquire_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrAcquireSwapchainImage:2", 1},
    {"pointer-reject", sc_ptr_reject, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_PTR=1", 1},
    {"pointer-nostatic", sc_ptr_nostatic, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrCreateSwapchain:2", 1},
    {"pointer-hud", sc_ptr_hud, "SOMVR_XR_LOADER=@MOCK", 1},
    {"pointer-nohud", sc_ptr_nohud, "SOMVR_XR_LOADER=@MOCK", 1},
    {"eyesize-switch", sc_eyesize_switch, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_DUMP=s2,s3,s4", 1},
    {"fade", sc_fade, "SOMVR_XR_LOADER=@MOCK", 1},
    {"fade-reject", sc_fade_reject, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_HUD=1", 1},
    {"fade-off", sc_fade_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_FADE=off", 1},
    {"fade-hud", sc_fade_hud, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vignette", sc_vignette, "SOMVR_XR_LOADER=@MOCK", 1},
    {"blink", sc_blink, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vignette-hud", sc_vignette_hud, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HUD=head", 1},
    {"wraith", sc_wraith, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vignette-lens", sc_vignette_lens, "SOMVR_XR_LOADER=@MOCK SOMVR_VIGNETTE_FIT=lens", 1},
    {"vignette-absent", sc_vignette_absent, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vignette-inactive", sc_vignette_inactive, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vignette-fail", sc_vignette_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrEnumerateSwapchainImages:3", 1},
    {"vignette-reject", sc_vignette_reject, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_CF=1", 1},
    {"vignette-nostatic", sc_vignette_nostatic, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrCreateSwapchain:3", 1},
    {"threat", sc_threat, "SOMVR_XR_LOADER=@MOCK", 1},
    {"threat-vignette", sc_threat_vignette, "SOMVR_XR_LOADER=@MOCK", 1},
    {"threat-absent", sc_threat_absent, "SOMVR_XR_LOADER=@MOCK", 1},
    {"threat-inactive", sc_threat_inactive, "SOMVR_XR_LOADER=@MOCK", 1},
    {"threat-off", sc_threat_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_THREAT=off", 1},
    {"threat-reject", sc_threat_reject, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_SC=544x102", 1},
    {"allon", sc_allon, "SOMVR_XR_LOADER=@MOCK MOCKXR_RECENTER_AT=80", 1},
    {"reject-hud-only", sc_reject_hud_only, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_SC=1920x1080", 1},
    /* SoM loading env */
    {"env-off", sc_env_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=off MOCKXR_EXTS=" EQ_EXT, 1},
    {"env-equirect", sc_env_equirect, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"env-cube", sc_env_cube, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on MOCKXR_FORMATS=50", 1},
    {"env-stereo", sc_env_stereo, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on", 1},
    {"env-world", sc_env_world,
     "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=world SOMVR_XR_ENV_DIM=0.25 MOCKXR_EXTS=XR_EXT_hand_tracking," EQ_EXT, 1},
    {"env-world-a", sc_env_world_a, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=world MOCKXR_EXTS=" EQ_EXT, 1},
    {"env-reject", sc_env_reject, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on MOCKXR_REJECT_ENV=1", 1},
    {"env-fail", sc_env_fail, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"env-reject-ptr", sc_env_reject_ptr, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on MOCKXR_REJECT_SC=64x32", 1},
    {"env-badcfg", sc_env_badcfg,
     "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=sideways SOMVR_XR_ENV_DIM=2 MOCKXR_EXTS=" EQ_EXT, 1},
    /* never a void */
    {"void-gaps", sc_void_gaps, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on", 1},
    {"void-gaps-off", sc_void_gaps_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_GAPS=off", 1},
    {"void-keep-world", sc_void_keep_world, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_KEEPALIVE=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"void-keep-menu", sc_void_keep_menu, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_KEEPALIVE=on", 1},
    {"void-space", sc_void_space,
     "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_KEEPALIVE=on SOMVR_XR_LOADSPACE=on SOMVR_XR_LOADSPACE_MS=200 "
     "MOCKXR_EXTS=" EQ_EXT, 1},
    {"void-hands", sc_void_hands, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_LOADSPACE=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"void-theatre", sc_void_theatre, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_THEATRE=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"void-loadpanel", sc_void_loadpanel, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_LOADSPACE=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"void-badcfg", sc_void_badcfg,
     "SOMVR_XR_LOADER=@MOCK SOMVR_XR_GAPS=3 SOMVR_XR_KEEPALIVE=maybe SOMVR_XR_KEEPALIVE_MS=5 SOMVR_XR_LOADSPACE=x "
     "SOMVR_XR_LOADSPACE_MS=-1", 1},
    {"cover", sc_cover, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_LOADSPACE=on SOMVR_XR_COVER=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"cover-off", sc_cover_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on SOMVR_XR_LOADSPACE=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"menu-back", sc_menu_back, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_MENU=front SOMVR_XR_MENU_BACK=on", 1},
    {"menu-back-nomenu", sc_menu_back_nomenu,
     "SOMVR_XR_LOADER=@MOCK SOMVR_XR_MENU_BACK=on SOMVR_XR_MENU_BACK_DIM=2 SOMVR_XR_COVER=yes SOMVR_XR_COVER_MS=x", 1},
    /* the VR menu */
    {"vrmenu", sc_vrmenu, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrmenu-stereo", sc_vrmenu_stereo, "SOMVR_XR_LOADER=@MOCK", 1},
    {"card", sc_card, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_CARD=on", 1},
    {"wrist", sc_wrist, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_MINIMAP=wrist", 1},
    {"vrhands", sc_vrhands, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrhands-off", sc_vrhands_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HANDS=off", 1},
    {"vrhands-nofuncs", sc_vrhands_nofuncs, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrhands-inactive", sc_vrhands_inactive, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrhands-depth", sc_vrhands_depth, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HANDS_DEPTH=world", 1},
    {"vrhands-ghost", sc_vrhands_ghost, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrhands-depth-nofuncs", sc_vrhands_depth_nofuncs, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HANDS_DEPTH=world", 1},
    {"wrist-hud", sc_wrist_hud, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_MINIMAP=wrist SOMVR_XR_WRIST_HUD=on", 1},
    {"prompts", sc_prompts, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_PROMPTS=on MOCKXR_RECENTER_AT=10", 1},
    {"prompts-head", sc_prompts_head, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_HUD=head SOMVR_XR_PROMPTS=on "
                                      "SOMVR_XR_PROMPTS_QUAD=1.0,1.0,20", 1},
    {"prompts-cap", sc_prompts_cap, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_PROMPTS=on MOCKXR_MAX_LAYERS=2", 1},
    {"prompts-badcfg", sc_prompts_badcfg, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_PROMPTS=yes SOMVR_XR_WRIST_HUD=2 "
                                          "SOMVR_XR_PROMPTS_RECTS=1,2", 1},
    {"vrmenu-pointer", sc_vrmenu_pointer, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrmenu-env", sc_vrmenu_env, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_ENV=on MOCKXR_EXTS=" EQ_EXT, 1},
    {"vrmenu-gate", sc_vrmenu_gate, "SOMVR_XR_LOADER=@MOCK", 1},
    {"build3", sc_build3, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrmenu-absent", sc_vrmenu_absent, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrmenu-inactive", sc_vrmenu_inactive, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrmenu-off", sc_vrmenu_off, "SOMVR_XR_LOADER=@MOCK SOMVR_XR_VRMENU=off", 1},
    {"vrmenu-noinput", sc_vrmenu_noinput, "SOMVR_XR_LOADER=@MOCK", 1},
    {"vrmenu-reject", sc_vrmenu_reject, "SOMVR_XR_LOADER=@MOCK MOCKXR_REJECT_VM=1", 1},
    {"vrmenu-swapchain-fail", sc_vrmenu_swapchain_fail, "SOMVR_XR_LOADER=@MOCK MOCKXR_FAIL=xrCreateSwapchain:2", 1},
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
    static char lay[4096], mock[4096]; /* absolute: dump-nolog runs in TMPDIR */
    if (realpath(g_layer, lay)) g_layer = lay;
    if (realpath(g_mock, mock)) g_mock = mock;
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
            unsetenv("SOMVR_XR_SPACE");
            unsetenv("SOMVR_XR_DUMP");
            unsetenv("SOMVR_XR_INPUT");
            unsetenv("SOMVR_XR_PREDICT");
            unsetenv("SOMVR_XR_HUD");
            unsetenv("SOMVR_XR_HUD_ALPHA");
            unsetenv("SOMVR_XR_HUD_QUAD");
            unsetenv("SOMVR_XR_POINTER");
            unsetenv("SOMVR_XR_FADE");
            unsetenv("SOMVR_XR_ENV");
            unsetenv("SOMVR_XR_ENV_DIM");
            unsetenv("SOMVR_XR_VRMENU");
            unsetenv("SOMVR_XR_HANDS"); /* build 10 */
            unsetenv("SOMVR_XR_WATCH");
            unsetenv("SOMVR_XR_PANEL");
            unsetenv("SOMVR_XR_HANDS_DEPTH"); /* build 11 */
            unsetenv("SOMVR_XR_HANDS_DEPTH_SOFT");
            unsetenv("SOMVR_XR_HANDS_DEPTH_BIAS");
            unsetenv("SOMVR_XR_HANDS_DEPTH_HELD");
            unsetenv("SOMVR_XR_HANDS_DEPTH_DEBUG");
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
