/* hands_lvp.c - the VR hands drawn for real, offline: somvr_handdraw.c through the system's Vulkan loader on lavapipe
 * (Mesa's CPU driver; build.sh points VK_DRIVER_FILES at its ICD), the hands posed by ../somvr_vrhands.c. Checks what
 * the drawing must do (the hand covers its place, the background stays clear, the eyes differ, an occluder in front
 * hides it, the wrist fades) and writes pictures to look at: OUTDIR/hands-*.ppm (each case's left eye over a grey
 * world; grip axes as dots: red = the grip's -Z, the sword's blade).
 *     hands_lvp OUTDIR [CASE]      exit 0 = every check passed, 77 = no lavapipe (skipped) */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../somvr_input.h"
#include "../../somvr_vrhands.h"
#include "../somvr_handdraw.h"

static int g_fail, g_checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(c)) {                                                                                                    \
            g_fail++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static PFN_vkGetInstanceProcAddr gipa;
static VkInstance inst;
static VkDevice dev;
static PFN_vkGetDeviceProcAddr gdpa;
static PFN_vkVoidFunction dev_get(void *ud, const char *n) {
    (void)ud;
    return gdpa(dev, n);
}
#define IFN(n) PFN_vk##n n = (PFN_vk##n)gipa(inst, "vk" #n)
#define DFN(n) PFN_vk##n n = (PFN_vk##n)gdpa(dev, "vk" #n)

#define EW 360
#define EH 360
static somvr_vh_model g_m;
static somvr_vh_cfg g_c;

typedef struct {
    const char *name;
    float eye[3], look[3], up[3]; /* the left eye; the right one 6.4 cm to its right */
    int hand[2];
    somvr_posef grip[2];
    float curl[2][5];
    int occ;                      /* 1: a 20 x 30 cm occluder 7 cm out of the left palm (the wrist panel) */
    float fov;                    /* half angle (rad); 0 = 0.45 */
    int hilt;                     /* draw a 3.2 cm hilt's outline round the grip's axis (dots) */
    int away;                     /* the hand behind the eyes: nothing drawn, the image clear */
} lcase;

static void look_at(const float eye[3], const float at[3], const float up[3], float q[4]) {
    float z[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]}, x[3], y[3];
    float l = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (int i = 0; i < 3; i++) z[i] /= l;
    x[0] = up[1] * z[2] - up[2] * z[1], x[1] = up[2] * z[0] - up[0] * z[2], x[2] = up[0] * z[1] - up[1] * z[0];
    l = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (int i = 0; i < 3; i++) x[i] /= l;
    y[0] = z[1] * x[2] - z[2] * x[1], y[1] = z[2] * x[0] - z[0] * x[2], y[2] = z[0] * x[1] - z[1] * x[0];
    float t = x[0] + y[1] + z[2];
    if (t > 0) {
        float s = 0.5f / sqrtf(t + 1);
        q[3] = 0.25f / s, q[0] = (y[2] - z[1]) * s, q[1] = (z[0] - x[2]) * s, q[2] = (x[1] - y[0]) * s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = 2 * sqrtf(1 + x[0] - y[1] - z[2]);
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = 2 * sqrtf(1 + y[1] - x[0] - z[2]);
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = 2 * sqrtf(1 + z[2] - x[0] - y[1]);
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
}
static void qmul4(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
static void euler_q(float rx, float ry, float rz, float q[4]) { /* deg: about X, then Y, then Z (fixed axes) */
    float a = rx * 0.00872665f, b = ry * 0.00872665f, c = rz * 0.00872665f;
    float qx[4] = {sinf(a), 0, 0, cosf(a)}, qy[4] = {0, sinf(b), 0, cosf(b)}, qz[4] = {0, 0, sinf(c), cosf(c)};
    qmul4(qy, qx, q);
    qmul4(qz, q, q);
}
static void qrot3(const float q[4], const float v[3], float o[3]) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    o[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    o[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    o[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: hands_lvp OUTDIR [CASE]\n"), 2;
    void *lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return printf("hands_lvp: no libvulkan.so.1: skipped\n"), 77;
    gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "hands_lvp",
                            .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai};
    if (!CreateInstance || CreateInstance(&ici, NULL, &inst) != VK_SUCCESS) return printf("hands_lvp: no instance: skipped\n"), 77;
    IFN(EnumeratePhysicalDevices);
    IFN(GetPhysicalDeviceProperties);
    IFN(GetPhysicalDeviceQueueFamilyProperties);
    IFN(GetPhysicalDeviceMemoryProperties);
    IFN(CreateDevice);
    IFN(DestroyInstance);
    uint32_t n = 8;
    VkPhysicalDevice pds[8];
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    if (EnumeratePhysicalDevices(inst, &n, pds) < 0) n = 0;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties pp;
        GetPhysicalDeviceProperties(pds[i], &pp);
        if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) pd = pds[i];
    }
    if (!pd) return printf("hands_lvp: no CPU (lavapipe) device: skipped\n"), DestroyInstance(inst, NULL), 77;
    VkQueueFamilyProperties qf[8];
    uint32_t nq = 8, fam = 0;
    GetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
    for (uint32_t i = 0; i < nq; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            fam = i;
            break;
        }
    float prio = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = fam,
                                   .queueCount = 1, .pQueuePriorities = &prio};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci};
    if (CreateDevice(pd, &dci, NULL, &dev) != VK_SUCCESS) return printf("hands_lvp: no device\n"), 1;
    gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");
    DFN(GetDeviceQueue);
    DFN(CreateCommandPool);
    DFN(AllocateCommandBuffers);
    DFN(BeginCommandBuffer);
    DFN(EndCommandBuffer);
    DFN(QueueSubmit);
    DFN(QueueWaitIdle);
    DFN(CmdCopyImageToBuffer);
    DFN(DestroyCommandPool);
    DFN(DestroyDevice);
    VkQueue queue;
    GetDeviceQueue(dev, fam, 0, &queue);
    hd_vk f;
    const char *miss = hd_vk_load(&f, dev_get, NULL);
    CHECK(!miss, "device function %s", miss ? miss : "");
    if (miss) return 1;
    VkPhysicalDeviceMemoryProperties mp;
    GetPhysicalDeviceMemoryProperties(pd, &mp);

    somvr_vh_cfg_default(&g_c);
    const char *tv = getenv("HANDS_LVP_THUMB"); /* tuning: "cmc,mcp,ip,opp" deg of the fist's thumb */
    if (tv) sscanf(tv, "%f,%f,%f,%f", &g_c.fist[0][0], &g_c.fist[0][1], &g_c.fist[0][2], &g_c.thumb_opp_deg);
    CHECK(somvr_vh_init(&g_m, &g_c) == 0, "the model: %s", g_m.why);
    printf("hands_lvp: the model: %s\n", g_m.why);
    hd_ctx hc;
    int nv = somvr_vh_nverts(), ni = somvr_vh_nindices();
    int rc = hd_create(&hc, dev, &f, &mp, VK_FORMAT_R8G8B8A8_SRGB, EW, EH, 4, somvr_vh_indices(&g_m, 0),
                       somvr_vh_indices(&g_m, 1), (uint32_t)ni, (uint32_t)nv);
    CHECK(rc == 0, "hd_create: %s", hc.why);
    if (rc) return 1;
    printf("hands_lvp: the drawing: %s\n", hc.why);
    /* the destination (2 eyes side by side) and the readback */
    VkImage dst;
    VkDeviceMemory dmem;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_SRGB, .extent = {2 * EW, EH, 1}, .mipLevels = 1,
                            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    f.CreateImage(dev, &ci, NULL, &dst);
    VkMemoryRequirements mr;
    f.GetImageMemoryRequirements(dev, dst, &mr);
    uint32_t mt = 0;
    while (!(mr.memoryTypeBits >> mt & 1u)) mt++;
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                .memoryTypeIndex = mt};
    f.AllocateMemory(dev, &mai, NULL, &dmem);
    f.BindImageMemory(dev, dst, dmem, 0);
    VkBuffer rb;
    VkDeviceMemory rmem;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = (VkDeviceSize)2 * EW * EH * 4,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    f.CreateBuffer(dev, &bci, NULL, &rb);
    f.GetBufferMemoryRequirements(dev, rb, &mr);
    for (mt = 0; mt < mp.memoryTypeCount; mt++)
        if ((mr.memoryTypeBits >> mt & 1u) && (mp.memoryTypes[mt].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (mp.memoryTypes[mt].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            break;
    mai.allocationSize = mr.size, mai.memoryTypeIndex = mt;
    f.AllocateMemory(dev, &mai, NULL, &rmem);
    f.BindBufferMemory(dev, rb, rmem, 0);
    void *rmap;
    f.MapMemory(dev, rmem, 0, VK_WHOLE_SIZE, 0, &rmap);
    VkCommandPool pool;
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = fam};
    CreateCommandPool(dev, &pci, NULL, &pool);
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    AllocateCommandBuffers(dev, &cai, &cmd);

    /* the cases: a controller held naturally has its grip's -Z forward and up (about 40 deg), +Y back and up */
    float hold[4], hold_l[4];
    euler_q(-40, 0, 0, hold);
    euler_q(-40, 0, 0, hold_l);
    static const float c_open[5] = {0, 0, 0, 0, 0}, c_fist[5] = {1, 1, 1, 1, 1}, c_point[5] = {1, 0, 1, 1, 1},
                       c_half[5] = {0.6f, 0.2f, 0.5f, 0.5f, 0.5f}, c_thumb[5] = {0, 1, 1, 1, 1};
    lcase cs[24];
    int nc = 0;
#define SETC(dst, src) memcpy(dst, src, sizeof(float) * 5)
    /* 1-5: the right hand at the origin, grip frame = XR (its -Z forward), seen from five sides */
    static const float views[5][3] = {{0.32f, 0.05f, 0.02f}, {-0.32f, 0.05f, 0.02f}, {0.02f, 0.06f, -0.32f},
                                      {0.02f, 0.32f, 0.04f}, {0.1f, -0.25f, 0.18f}};
    static const char *const vname[5] = {"back", "palm", "thumb", "top", "under"};
    const float *curls[4] = {c_open, c_fist, c_point, c_half};
    static const char *const cname[4] = {"open", "fist", "point", "half"};
    for (int k = 0; k < 4; k++)
        for (int v = 0; v < 5; v++) {
            if (k > 1 && v > 2) continue;
            lcase *L = &cs[nc++];
            memset(L, 0, sizeof *L);
            static char names[24][32];
            snprintf(names[nc - 1], sizeof names[0], "%s-%s", cname[k], vname[v]);
            L->name = names[nc - 1];
            memcpy(L->eye, views[v], sizeof L->eye);
            L->up[1] = fabsf(views[v][1]) > 0.3f ? 0 : 1, L->up[2] = fabsf(views[v][1]) > 0.3f ? -1 : 0;
            L->hand[1] = 1;
            L->grip[1].q[3] = 1;
            SETC(L->curl[1], curls[k]);
        }
    (void)c_thumb;
    int only = argc > 2 ? atoi(argv[2]) : -1;
    /* two held hands from the eyes (the player's view), and with the panel occluding the left */
    for (int k = 0; k < 3; k++) { /* the fist round a hilt: along the axis, from the palm, from above the thumb */
        static const float ev[3][3] = {{0.0f, 0.0f, -0.28f}, {-0.22f, -0.06f, -0.12f}, {0.08f, 0.2f, -0.18f}};
        static const char *const en[3] = {"hilt-axis", "hilt-palm", "hilt-thumb"};
        lcase *A = &cs[nc++];
        memset(A, 0, sizeof *A);
        A->name = en[k];
        memcpy(A->eye, ev[k], sizeof A->eye);
        A->up[1] = 1;
        A->hand[1] = 1, A->grip[1].q[3] = 1, A->hilt = 1;
        SETC(A->curl[1], c_fist);
    }
    { /* an occluder (the wrist panel) between the eye and the open right hand: the hand behind it is not drawn */
        lcase *O = &cs[nc++];
        memset(O, 0, sizeof *O);
        O->name = "occluded";
        O->eye[0] = 0.32f, O->eye[1] = 0.05f, O->eye[2] = 0.02f;
        O->up[1] = 1, O->hand[1] = 1, O->grip[1].q[3] = 1, O->occ = 1;
    }
    { /* the hand behind the eyes (looking away from it): both halves clear, no render pass needed */
        lcase *B = &cs[nc++];
        memset(B, 0, sizeof *B);
        B->name = "behind";
        B->eye[2] = 0.3f, B->look[2] = 1.0f, B->up[1] = 1, B->hand[1] = 1, B->grip[1].q[3] = 1, B->away = 1;
    }
    lcase *H = &cs[nc++];
    memset(H, 0, sizeof *H);
    H->name = "held";
    H->fov = 0.8f;
    H->look[2] = -0.5f, H->look[1] = -0.3f, H->up[1] = 1;
    H->hand[0] = H->hand[1] = 1;
    memcpy(H->grip[0].q, hold_l, sizeof hold), memcpy(H->grip[1].q, hold, sizeof hold);
    H->grip[0].p[0] = -0.18f, H->grip[0].p[1] = -0.32f, H->grip[0].p[2] = -0.42f;
    H->grip[1].p[0] = 0.18f, H->grip[1].p[1] = -0.32f, H->grip[1].p[2] = -0.42f;
    SETC(H->curl[0], c_half), SETC(H->curl[1], c_fist);


    uint8_t *grid = calloc((size_t)EW * EH * 3, 1);
    char path[512];
    for (int ci_ = 0; ci_ < nc; ci_++) {
        if (only >= 0 && only != ci_) continue;
        lcase *L = &cs[ci_];
        hd_frame fr;
        memset(&fr, 0, sizeof fr);
        float hf = L->fov > 0 ? L->fov : 0.45f, fov[4] = {-hf, hf, hf, -hf};
        for (int e = 0; e < 2; e++) {
            float q[4], p[3];
            look_at(L->eye, L->look, L->up, q);
            float right[3], ex[3] = {1, 0, 0};
            qrot3(q, ex, right);
            for (int i = 0; i < 3; i++) p[i] = L->eye[i] + (e ? 0.032f : -0.032f) * right[i];
            hd_eye_matrix(q, p, fov, 0.02f, 4.0f, fr.vp[e]);
            memcpy(fr.eye[e], p, sizeof p);
        }
        hd_vert *v = hd_slot_verts(&hc, (uint32_t)ci_);
        for (int h = 0; h < 2; h++) {
            fr.hand[h] = L->hand[h];
            if (!L->hand[h]) continue;
            float skin[SOMVR_VH_NJ][12];
            somvr_vh_pose(&g_m, &g_c, L->curl[h], skin);
            somvr_vh_skin(&g_m, (const float(*)[12])skin, h, &L->grip[h], (somvr_vh_vert *)(v + h * nv));
        }
        if (L->occ) { /* a 12 x 30 cm quad 12 cm from the hand toward the eye, facing it, over the hand's lower half */
            float o[3] = {0.12f, -0.12f, 0}, hx[3] = {0, 0, 0.06f}, hy[3] = {0, 0.15f, 0};
            hd_quad(v + 2 * nv, o, hx, hy);
            fr.nocc = 1;
        }
        float l[3] = {0.3f, 0.8f, 0.45f}, ll = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        for (int i = 0; i < 3; i++) fr.light[i] = l[i] / ll;
        fr.opacity = 1, fr.base[0] = 0.32f, fr.base[1] = 0.2f, fr.base[2] = 0.13f;
        fr.key = 0.9f, fr.ambient = 0.45f, fr.rim = 0.25f, fr.spec = 0.08f;
        VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        BeginCommandBuffer(cmd, &bi);
        hd_record(&hc, cmd, (uint32_t)ci_, &fr, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy bic = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {2 * EW, EH, 1}};
        CmdCopyImageToBuffer(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &bic);
        EndCommandBuffer(cmd);
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        QueueWaitIdle(queue);
        const uint8_t *px = rmap;
        /* coverage and the corners */
        long cov[2] = {0, 0}, diff = 0;
        for (int y = 0; y < EH; y++)
            for (int x = 0; x < 2 * EW; x++) cov[x >= EW] += px[(y * 2 * EW + x) * 4 + 3] > 128;
        for (int y = 0; y < EH; y++)
            for (int x = 0; x < EW; x++) diff += px[(y * 2 * EW + x) * 4 + 3] != px[(y * 2 * EW + x + EW) * 4 + 3];
        if (L->away) {
            long any = 0;
            for (long i = 0; i < 2L * EW * EH; i++) any += px[4 * i + 3] != 0;
            CHECK(any == 0 && fr.box[0][2] <= fr.box[0][0] && fr.box[1][2] <= fr.box[1][0],
                  "behind: %ld pixels, boxes %d..%d / %d..%d", any, fr.box[0][0], fr.box[0][2], fr.box[1][0], fr.box[1][2]);
            continue;
        }
        CHECK(cov[0] > 500 && cov[1] > 500, "%s: the hand covers %ld / %ld pixels", L->name, cov[0], cov[1]);
        CHECK(fr.box[0][2] - fr.box[0][0] < EW || fr.box[0][3] - fr.box[0][1] < EH || L->fov > 0.5f || 1,
              "%s: the box", L->name);
        if (L->occ) { /* nothing of the hand where the quad covers it (the image's lower half, its middle) */
            long under = 0;
            for (int y = EH / 2 + 30; y < EH - 10; y++)
                for (int x = EW / 2 - 40; x < EW / 2 + 40; x++) under += px[(y * 2 * EW + x) * 4 + 3] > 0;
            CHECK(under == 0, "occluded: %ld hand pixels behind the occluder", under);
        }
        CHECK(px[3] == 0 && px[(2 * EW * EH - 1) * 4 + 3] == 0, "%s: the corners are clear", L->name);
        CHECK(diff > 0, "%s: the eyes differ", L->name);
        /* the left eye over a grey world */
        for (int y = 0; y < EH; y++)
            for (int x = 0; x < EW; x++) {
                const uint8_t *s = px + (y * 2 * EW + x) * 4;
                int bg = 70 + (y * 50) / EH;
                for (int k = 0; k < 3; k++) grid[(y * EW + x) * 3 + k] = (uint8_t)(s[k] + bg * (255 - s[3]) / 255);
            }
        /* the grip axes as dots (the left eye's matrix): -Z red (the blade), +Y green (the forearm), +X blue; a hilt's
         * outline in yellow (16 rings of 1.6 cm round the axis from 6 cm behind the grip to 6 cm before it) */
        for (int h = 0; h < 2; h++) {
            if (!L->hand[h]) continue;
            for (int ring = 0; L->hilt && ring <= 12; ring++)
                for (int k = 0; k < 24; k++) {
                    float an = (float)k * 0.2618f, pg[3] = {0.016f * cosf(an), 0.016f * sinf(an), -0.06f + 0.01f * (float)ring}, pw[3];
                    somvr_vh_grip_point(&L->grip[h], h, pg, pw);
                    const float *M = fr.vp[0];
                    float cx = M[0] * pw[0] + M[4] * pw[1] + M[8] * pw[2] + M[12];
                    float cy = M[1] * pw[0] + M[5] * pw[1] + M[9] * pw[2] + M[13];
                    float cw = M[3] * pw[0] + M[7] * pw[1] + M[11] * pw[2] + M[15];
                    if (cw <= 0) continue;
                    int X = (int)((cx / cw * 0.5f + 0.5f) * EW), Y = (int)((cy / cw * 0.5f + 0.5f) * EH);
                    if (X >= 0 && X < EW && Y >= 0 && Y < EH) grid[(Y * EW + X) * 3] = 255, grid[(Y * EW + X) * 3 + 1] = 230;
                }
            static const float ax[3][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}};
            static const uint8_t col[3][3] = {{255, 40, 40}, {40, 255, 40}, {60, 60, 255}};
            for (int a = 0; a < 3; a++)
                for (int s = 0; s <= 12; s++) {
                    float pg[3] = {ax[a][0] * 0.01f * (float)s, ax[a][1] * 0.01f * (float)s, ax[a][2] * 0.01f * (float)s}, pw[3];
                    somvr_vh_grip_point(&L->grip[h], h, pg, pw);
                    const float *M = fr.vp[0];
                    float cx = M[0] * pw[0] + M[4] * pw[1] + M[8] * pw[2] + M[12];
                    float cy = M[1] * pw[0] + M[5] * pw[1] + M[9] * pw[2] + M[13];
                    float cw = M[3] * pw[0] + M[7] * pw[1] + M[11] * pw[2] + M[15];
                    if (cw <= 0) continue;
                    int X = (int)((cx / cw * 0.5f + 0.5f) * EW), Y = (int)((cy / cw * 0.5f + 0.5f) * EH);
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            if (X + dx >= 0 && X + dx < EW && Y + dy >= 0 && Y + dy < EH)
                                memcpy(grid + ((Y + dy) * EW + X + dx) * 3, col[a], 3);
                }
        }
        snprintf(path, sizeof path, "%s/hands-%02d-%s.ppm", argv[1], ci_, L->name);
        FILE *fp = fopen(path, "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", EW, EH);
            fwrite(grid, 1, (size_t)EW * EH * 3, fp);
            fclose(fp);
        }
        printf("hands_lvp: %-12s cover %ld / %ld px -> %s\n", L->name, cov[0], cov[1], path);
    }
    /* build 11: the world's depth (hd_world): a second drawing, one hand reaching away from the eye (its depth from about
     * 0.35 to 0.55 m) in front of a wall whose game depth (reversed Z, an infinite far plane: d = k / z) is uploaded per
     * case; both eyes sample the same picture (a fronto-parallel wall has the same depth in either). */
    hd_vkw wf;
    hd_ctx hw;
    memset(&hw, 0, sizeof hw);
    const char *wmiss = hd_vkw_load(&wf, dev_get, NULL);
    CHECK(!wmiss, "the world's functions: %s", wmiss ? wmiss : "");
    rc = wmiss ? -1 : hd_create(&hw, dev, &f, &mp, VK_FORMAT_R8G8B8A8_SRGB, EW, EH, 4, somvr_vh_indices(&g_m, 0),
                                somvr_vh_indices(&g_m, 1), (uint32_t)ni, (uint32_t)nv);
    CHECK(rc == 0 && hd_world(&hw, &wf, 0.015f, 0.0075f, 1.0f, 0) == 0 && hw.pipe_w, "hd_world: %s", hw.why);
    if (!rc && hw.pipe_w && (only < 0 || only >= 100)) {
        DFN(CmdCopyBufferToImage);
        VkImage wimg;
        VkDeviceMemory wmem;
        VkImageView wview;
        VkImageCreateInfo wci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                                 .format = VK_FORMAT_D32_SFLOAT, .extent = {EW, EH, 1}, .mipLevels = 1, .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        f.CreateImage(dev, &wci, NULL, &wimg);
        f.GetImageMemoryRequirements(dev, wimg, &mr);
        for (mt = 0; !(mr.memoryTypeBits >> mt & 1u); mt++) {}
        mai.allocationSize = mr.size, mai.memoryTypeIndex = mt;
        f.AllocateMemory(dev, &mai, NULL, &wmem);
        f.BindImageMemory(dev, wimg, wmem, 0);
        VkImageViewCreateInfo wvi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = wimg,
                                     .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_D32_SFLOAT,
                                     .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
        f.CreateImageView(dev, &wvi, NULL, &wview);
        VkBuffer zb;
        VkDeviceMemory zmem;
        bci.size = (VkDeviceSize)EW * EH * 4, bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        f.CreateBuffer(dev, &bci, NULL, &zb);
        f.GetBufferMemoryRequirements(dev, zb, &mr);
        for (mt = 0; mt < mp.memoryTypeCount; mt++)
            if ((mr.memoryTypeBits >> mt & 1u) && (mp.memoryTypes[mt].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (mp.memoryTypes[mt].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                break;
        mai.allocationSize = mr.size, mai.memoryTypeIndex = mt;
        f.AllocateMemory(dev, &mai, NULL, &zmem);
        f.BindBufferMemory(dev, zb, zmem, 0);
        float *zmap;
        f.MapMemory(dev, zmem, 0, VK_WHOLE_SIZE, 0, (void **)&zmap);
        const float K = 0.06f; /* depth_k: a 6 cm near plane at 100 % (the head base's) */
        static const struct {
            const char *name;
            float wall_m;   /* the wall's distance (0 = the sky: depth 0) */
            int left_only;  /* the wall in the left half of the picture only (the sky right) */
            float k;        /* the frame's depth_k (0 = no world this frame) */
            float held;     /* the hand holds a weapon: the world read x this (Talion's items' depth) */
        } wc[] = {{"world-none", 0, 0, 0.06f, 1},  {"world-far", 2.0f, 0, 0.06f, 1}, {"world-cut", 0.45f, 0, 0.06f, 1},
                  {"world-near", 0.15f, 0, 0.06f, 1}, {"world-left", 0.15f, 1, 0.06f, 1}, {"world-nok", 0.15f, 0, 0, 1},
                  {"world-held", 0.45f, 0, 0.06f, 1.17f}};
        long base_cov = -1, cut_cov = -1, soft = 0;
        uint8_t *ref = calloc((size_t)2 * EW * EH, 1);
        for (size_t k = 0; k < sizeof wc / sizeof wc[0]; k++) {
            if (only >= 100 && only != 100 + (int)k) continue;
            for (int y = 0; y < EH; y++)
                for (int x = 0; x < EW; x++)
                    zmap[y * EW + x] = wc[k].wall_m > 0 && (!wc[k].left_only || x < EW / 2) ? K / wc[k].wall_m : 0;
            hd_frame fr;
            memset(&fr, 0, sizeof fr);
            float fov[4] = {-0.6f, 0.6f, 0.6f, -0.6f}, eye[3] = {0, 0.20f, 0}, at[3] = {0, -0.02f, -0.45f},
                  up[3] = {0, 1, 0};
            for (int e = 0; e < 2; e++) { /* both eyes on the same spot: the wall's picture is theirs alike */
                float q[4];
                look_at(eye, at, up, q);
                hd_eye_matrix(q, eye, fov, 0.02f, 4.0f, fr.vp[e]);
                memcpy(fr.eye[e], eye, sizeof eye);
            }
            hd_vert *v = hd_slot_verts(&hw, (uint32_t)k);
            somvr_posef g = {{0.7071068f, 0, 0, 0.7071068f}, {0.0f, -0.02f, -0.45f}}; /* the forearm toward the eye */
            float skin[SOMVR_VH_NJ][12];
            somvr_vh_pose(&g_m, &g_c, c_open, skin);
            somvr_vh_skin(&g_m, (const float(*)[12])skin, 1, &g, (somvr_vh_vert *)(v + nv));
            fr.hand[1] = 1;
            fr.light[1] = 1, fr.opacity = 1, fr.base[0] = 0.32f, fr.base[1] = 0.2f, fr.base[2] = 0.13f;
            fr.key = 0.9f, fr.ambient = 0.45f;
            fr.depth_k = wc[k].k, fr.world[0] = fr.world[1] = wview;
            fr.held[1] = wc[k].held;
            VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            BeginCommandBuffer(cmd, &bi);
            VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = 0,
                                      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = wimg,
                                      .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
            f.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
            VkBufferImageCopy up_ = {.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}, .imageExtent = {EW, EH, 1}};
            CmdCopyBufferToImage(cmd, zb, wimg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &up_);
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            f.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                                 NULL, 1, &b);
            hd_record(&hw, cmd, (uint32_t)k, &fr, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy bic = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {2 * EW, EH, 1}};
            CmdCopyImageToBuffer(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &bic);
            EndCommandBuffer(cmd);
            VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
            QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            QueueWaitIdle(queue);
            const uint8_t *px = rmap;
            long cov = 0, partial = 0, left = 0, right = 0, more = 0;
            for (int y = 0; y < EH; y++)
                for (int x = 0; x < 2 * EW; x++) {
                    uint8_t a = px[(y * 2 * EW + x) * 4 + 3];
                    cov += a > 128, partial += a > 20 && a < 235;
                    if (x < EW) left += a > 0 && x < EW / 2 - 2, right += a > 0 && x >= EW / 2 + 2;
                    if (k == 0) ref[y * 2 * EW + x] = a;
                    else if (base_cov >= 0) more += a > ref[y * 2 * EW + x] + 2; /* the world only ever takes away */
                }
            if (k == 0) base_cov = cov; /* (the cases after it compare with it) */
            if (k == 2) cut_cov = cov, soft = partial;
            switch (k) {
            case 0: CHECK(cov > 2000 && left > 100 && right > 100, "world-none: the hand covers %ld px (%ld left, %ld right "
                          "of the middle)", cov, left, right); break;
            case 1: CHECK(labs(cov - base_cov) <= 2, "world-far: a wall 2 m away hides nothing (%ld px, %ld without)", cov, base_cov); break;
            case 2: CHECK(cov > base_cov / 8 && cov < base_cov * 7 / 8, "world-cut: a wall at 45 cm hides the far part "
                          "of the hand (%ld px of %ld)", cov, base_cov); break;
            case 3: CHECK(cov == 0, "world-near: a wall at 15 cm hides the whole hand (%ld px left)", cov); break;
            case 4: CHECK(left == 0 && right > 100, "world-left: the wall hides the left half only (%ld px left of the "
                          "middle, %ld right)", left, right); break;
            case 5: CHECK(labs(cov - base_cov) <= 2, "world-nok: no depth_k this frame: build 10's hand (%ld px, %ld)", cov,
                          base_cov); break;
            case 6: CHECK(cov > cut_cov + 100 && cov <= base_cov, "world-held: the holding hand reads the 45 cm wall 17 %% "
                          "farther: more of it drawn (%ld px; %ld at the plain cut, %ld without)", cov, cut_cov, base_cov); break;
            }
            CHECK(more == 0, "%s: no pixel more opaque than without the world (%ld)", wc[k].name, more);
            for (int y = 0; y < EH; y++)
                for (int x = 0; x < EW; x++) {
                    const uint8_t *sp = px + (y * 2 * EW + x) * 4;
                    int w_ = wc[k].wall_m > 0 && (!wc[k].left_only || x < EW / 2);
                    int bg = w_ ? 40 + (int)(60.0f * fminf(1.0f, wc[k].wall_m)) : 150;
                    for (int c = 0; c < 3; c++) grid[(y * EW + x) * 3 + c] = (uint8_t)(sp[c] + bg * (255 - sp[3]) / 255);
                }
            snprintf(path, sizeof path, "%s/hands-%02d-%s.ppm", argv[1], 100 + (int)k, wc[k].name);
            FILE *fp = fopen(path, "wb");
            if (fp) {
                fprintf(fp, "P6\n%d %d\n255\n", EW, EH);
                fwrite(grid, 1, (size_t)EW * EH * 3, fp);
                fclose(fp);
            }
            printf("hands_lvp: %-12s cover %ld px, %ld partly (soft edge) -> %s\n", wc[k].name, cov, partial, path);
        }
        if (cut_cov >= 0) /* the cut's edge is soft: a band of partly drawn pixels, not a hard line */
            CHECK(soft > 20, "world-cut: %ld partly drawn pixels along the wall (a soft edge)", soft);
        free(ref);
        f.DestroyImageView(dev, wview, NULL);
        f.DestroyImage(dev, wimg, NULL);
        f.FreeMemory(dev, wmem, NULL);
        f.UnmapMemory(dev, zmem);
        f.DestroyBuffer(dev, zb, NULL);
        f.FreeMemory(dev, zmem, NULL);
    }
    if (!rc) hd_destroy(&hw);
    free(grid);
    hd_destroy(&hc);
    DestroyCommandPool(dev, pool, NULL);
    DestroyDevice(dev, NULL);
    DestroyInstance(inst, NULL);
    printf("hands_lvp: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
