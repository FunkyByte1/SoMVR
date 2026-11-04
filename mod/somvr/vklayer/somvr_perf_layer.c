/* somvr_perf_layer.c - SOMVR_XR_PERF=on: what a frame costs on the GPU, the game's and ours (build 27, som-b27-perf,
 * docs/re-xr-pipeline.md 115). Included by somvr_vklayer.c (dev_st, X, lg, the keep-alive's vkQueueSubmit hook).
 *   The GPU: a timestamp first and last in each of the game's vkQueueSubmit calls on the session queue (two command
 *     buffers of ours, recorded once per pair of queries: TOP_OF_PIPE first, BOTTOM_OF_PIPE last); one in the game's own
 *     command buffer right before each copy into an eye image (H8's CopyResource = vkCmdCopyImage into eye e's VkImage:
 *     BOTTOM_OF_PIPE = "eye e's pass is done"); two around the layer's present path (our copies, quads, hands, the
 *     runtime's submits from inside xrEndFrame). Read back without waiting, frames later. Per frame: the busy time = the
 *     timed spans in queue order, each from max(its start, the previous end) to its end (idle gaps between submits do not
 *     count); split at the eye ends: eye 0 (the frame's shared work + eye 0's pass: shadows, sky, particles, the scene,
 *     its post), eye 1, the rest (eye 1's HUD, the UI, the composite into the desktop image); ours.
 *   The CPU (the render thread): the present path's wall time (xr_frame), the wait for the next frame (xrWaitFrame) apart.
 *   Every SOMVR_XR_PERF_S seconds (default 10): one `xr: perf:` line (mean / p50 / p95 / max, frames over 11.1 ms) and
 *     the GPU's shader clock and busy percent (sysfs, the first card with pp_dpm_sclk).
 *   Off: nothing hooked (the keep-alive's hooks as before), no line. Any Vulkan failure: logged once, timing off. */

#include "somvr_void.h" /* the switches' parsers */

#define PF_PAIRS 768u /* pairs of timestamp queries: the game's submits, the eye ends, ours */
#define PF_EVS 2048u  /* events waiting for their read-back (a ring) */
#define PF_WIN 2048u  /* frames per log line, at most */
#define PF_MAXCB 62u  /* a batch with more command buffers passes untimed */
#define PF_SEGS 48u   /* timed spans per frame, at most */
#define PF_LATE 24u   /* frames after which an event never read back is dropped */
#define PF_BUDGET_MS 11.111 /* 90 Hz; build 27 (som-b27-perf2): the line counts against the runtime's display period */

enum { PF_GAME = 1, PF_EYE0, PF_EYE1, PF_OURS };
/* how the game's eye images get written (H8's CopyResource): Feral = vkCmdCopyImage; DXVK = vkCmdCopyImage2, or a draw
 * into the image (DXVK's framebuffer copy: vkCmdBeginRendering with a view of it), a blit or a resolve */
enum { PF_EW_COPY, PF_EW_RENDER, PF_EW_BLIT, PF_EW_RESOLVE, PF_EW_N };
static const char *const pf_ew_name[PF_EW_N] = {"copy", "draw (a rendering into it)", "blit", "resolve"};

typedef struct {
    uint8_t kind;
    uint32_t pair;
    uint64_t frame; /* the frame (presents before it) the event belongs to */
} pf_ev;

static struct {
    int on, bad, period_s, period_bad;
    int ready, failed;
    pthread_mutex_t mx;
    dev_st *ds;
    VkQueue q;
    VkQueryPool qp;
    VkCommandPool pool;
    VkCommandBuffer cb_b[PF_PAIRS], cb_e[PF_PAIRS];
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkCmdResetQueryPool CmdResetQueryPool;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    double tick_ns;
    uint64_t mask;
    uint8_t used[PF_PAIRS]; /* 0 free, 1 waiting for its read-back, 2 held back (its event was dropped) */
    uint64_t back_until[PF_PAIRS]; /* a held-back pair is free again at this frame */
    uint32_t next, nback;
    pf_ev ev[PF_EVS];
    uint32_t head, tail; /* head = next free, tail = oldest; empty when equal */
    uint64_t frame;      /* presents on the session queue so far = the frame being built */
    uint32_t ours_pair;  /* this present's bracket (PF_PAIRS = none) */
    double x0;           /* this present's CPU start */
    /* the frame being gathered from the read-back */
    uint64_t cf;
    int cf_have;
    uint32_t nseg, nsub;
    double sb[PF_SEGS], se[PF_SEGS];
    uint8_t sk[PF_SEGS];
    double eye[2];
    int eyes;
    double prev_end;
    /* the window */
    double t0;
    uint32_t n, nsplit, ncpu;
    float game[PF_WIN], ours[PF_WIN], tot[PF_WIN], e0[PF_WIN], e1[PF_WIN], rest[PF_WIN], span[PF_WIN];
    float cpu_x[PF_WIN], cpu_w[PF_WIN], cpu_xc[PF_WIN], cpu_pw[PF_WIN], cpu_pc[PF_WIN], cpu_ac[PF_WIN];
    double xc0;          /* this present's thread CPU at the start */
    uint64_t waits_game; /* the game's timed batches that wait on a semaphore */
    uint64_t subs, sub_rt, untimed, lost, eyes_odd, frames, nsub_w;
    uint64_t ew[PF_EW_N]; /* the eye images' writes seen, by kind (the window's) */
    int ew_seen;          /* kinds logged once */
    char sclk_path[96], busy_path[96];
    char label[40]; /* SOMVR_XR_TEST `perf NAME`: the window's name */
} PF = {.mx = PTHREAD_MUTEX_INITIALIZER, .ours_pair = PF_PAIRS};

static __thread int pf_in_ours; /* the present path's own calls (the runtime's submits from inside xrEndFrame) */

/* the stereo present path by part (pf_sec(k) = the time since the last mark is part k's) */
#define PF_NSEC 9
static const char *const pf_sec_name[PF_NSEC] = {"start", "acquire+copy", "release", "lead+menu+pointer", "layers",
                                                 "fade+menu", "hands", "comfort+threat+objective", "xrEndFrame+rest"};
static struct {
    double t, c;                         /* the last mark */
    double w[PF_NSEC], u[PF_NSEC];       /* this frame: wall, CPU (s) */
    int stereo;                          /* a stereo frame marked its parts */
    double sw[PF_NSEC], su[PF_NSEC];     /* the window's sums (s) */
    float mx[PF_NSEC];                   /* the window's max wall (ms) */
    uint32_t n;
} PS;
static double pf_now(void);
static double pf_tcpu(void);
static void pf_sec(int k) {
    if (!pf_in_ours) return;
    double t = pf_now(), c = pf_tcpu();
    PS.w[k] += t - PS.t, PS.u[k] += c - PS.c;
    PS.t = t, PS.c = c;
    if (k == 1) PS.stereo = 1;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_QueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *si, VkFence f);

static void pf_config(void) {
    PF.on = somvr_void_parse_onoff(getenv("SOMVR_XR_PERF"), &PF.bad);
    PF.period_s = somvr_void_parse_ms(getenv("SOMVR_XR_PERF_S"), 10, 1, 600, &PF.period_bad);
    if (PF.bad || PF.period_bad)
        lg("WARN xr: perf: SOMVR_XR_PERF=%s / SOMVR_XR_PERF_S=%s not understood (on|off; seconds 1..600): %s, every %d s",
           getenv("SOMVR_XR_PERF") ? getenv("SOMVR_XR_PERF") : "", getenv("SOMVR_XR_PERF_S") ? getenv("SOMVR_XR_PERF_S") : "",
           PF.on ? "on" : "off", PF.period_s);
}

static double pf_tcpu(void) { /* this thread's CPU time (s) */
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static double pf_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* the first card with a shader clock table (amdgpu); none = no clock in the line */
static void pf_sysfs_find(void) {
    for (int c = 0; c < 8; c++) {
        char p[96];
        snprintf(p, sizeof p, "/sys/class/drm/card%d/device/pp_dpm_sclk", c);
        if (access(p, R_OK) == 0) {
            snprintf(PF.sclk_path, sizeof PF.sclk_path, "%s", p);
            snprintf(PF.busy_path, sizeof PF.busy_path, "/sys/class/drm/card%d/device/gpu_busy_percent", c);
            return;
        }
    }
}
/* the current shader clock (MHz; the line marked '*') and the busy percent; -1 = unknown */
static void pf_sysfs_read(int *mhz, int *busy) {
    *mhz = *busy = -1;
    char b[512];
    FILE *fp = PF.sclk_path[0] ? fopen(PF.sclk_path, "r") : NULL;
    if (fp) {
        while (fgets(b, sizeof b, fp))
            if (strchr(b, '*')) {
                char *m = strchr(b, ':');
                if (m) *mhz = atoi(m + 1);
            }
        fclose(fp);
    }
    fp = PF.busy_path[0] ? fopen(PF.busy_path, "r") : NULL;
    if (fp) {
        if (fgets(b, sizeof b, fp)) *busy = atoi(b);
        fclose(fp);
    }
}

__attribute__((format(printf, 1, 2))) static void pf_off(const char *fmt, ...) {
    char why[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    PF.failed = 1, PF.ready = 0;
    lg("WARN xr: perf: off for this run: %s", why);
}

/* once, on the first present of the session queue: the query pool, our pool, the pairs' command buffers */
static void pf_setup(dev_st *ds, VkQueue q, uint32_t family) {
    VkPhysicalDeviceProperties pp;
    ds->is->GetPhysicalDeviceProperties(ds->pd, &pp);
    PF.tick_ns = pp.limits.timestampPeriod;
    uint32_t bits = family < ds->nqf ? ds->qf[family].timestampValidBits : 0;
    if (!(PF.tick_ns > 0) || bits < 32)
        return pf_off("no timestamps on queue family %u (valid bits %u, period %.3f ns)", family, bits, PF.tick_ns);
    PF.mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    PF.CreateQueryPool = (PFN_vkCreateQueryPool)ds->gdpa(ds->dev, "vkCreateQueryPool");
    PF.DestroyQueryPool = (PFN_vkDestroyQueryPool)ds->gdpa(ds->dev, "vkDestroyQueryPool");
    PF.CmdResetQueryPool = (PFN_vkCmdResetQueryPool)ds->gdpa(ds->dev, "vkCmdResetQueryPool");
    PF.CmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)ds->gdpa(ds->dev, "vkCmdWriteTimestamp");
    PF.GetQueryPoolResults = (PFN_vkGetQueryPoolResults)ds->gdpa(ds->dev, "vkGetQueryPoolResults");
    if (!PF.CreateQueryPool || !PF.DestroyQueryPool || !PF.CmdResetQueryPool || !PF.CmdWriteTimestamp ||
        !PF.GetQueryPoolResults)
        return pf_off("the query functions are missing");
    VkQueryPoolCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                 .queryCount = 2 * PF_PAIRS};
    VkResult vr = PF.CreateQueryPool(ds->dev, &qci, NULL, &PF.qp);
    if (vr != VK_SUCCESS) return PF.qp = VK_NULL_HANDLE, pf_off("vkCreateQueryPool: %d", vr);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    if ((vr = ds->CreateCommandPool(ds->dev, &pci, NULL, &PF.pool)) != VK_SUCCESS)
        return PF.pool = VK_NULL_HANDLE, pf_off("vkCreateCommandPool: %d", vr);
    for (int k = 0; k < 2; k++) {
        VkCommandBuffer *cb = k ? PF.cb_e : PF.cb_b;
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = PF.pool,
                                           .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = PF_PAIRS};
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, cb)) != VK_SUCCESS)
            return pf_off("vkAllocateCommandBuffers: %d", vr);
        for (uint32_t i = 0; i < PF_PAIRS; i++) {
            if (ds->set_loader_data) {
                if ((vr = ds->set_loader_data(ds->dev, cb[i])) != VK_SUCCESS) return pf_off("the dispatch: %d", vr);
            } else {
                *(void **)cb[i] = dkey(ds->dev);
            }
            VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            if ((vr = ds->BeginCommandBuffer(cb[i], &bi)) != VK_SUCCESS) return pf_off("vkBeginCommandBuffer: %d", vr);
            if (!k) {
                PF.CmdResetQueryPool(cb[i], PF.qp, 2 * i, 2);
                PF.CmdWriteTimestamp(cb[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, PF.qp, 2 * i);
            } else {
                PF.CmdWriteTimestamp(cb[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, PF.qp, 2 * i + 1);
            }
            if ((vr = ds->EndCommandBuffer(cb[i])) != VK_SUCCESS) return pf_off("vkEndCommandBuffer: %d", vr);
        }
    }
    pf_sysfs_find();
    PF.ds = ds, PF.q = q, PF.ready = 1, PF.t0 = pf_now();
    lg("xr: perf: on (queue %p family %u, %u timestamp pairs, %.3f ns a tick, %u valid bits; clock %s): every %d s the "
       "frame's GPU busy time (the game's submits in queue order; split at the eye images' copies: eye 0 with the frame's "
       "shared work, eye 1, the rest), ours (the present path's submits and the runtime's), the present path's CPU",
       (void *)q, family, PF_PAIRS, PF.tick_ns, bits, PF.sclk_path[0] ? PF.sclk_path : "unknown", PF.period_s);
}

/* a free pair (PF.mx held); PF_PAIRS = none (all waiting for their read-back) */
static uint32_t pf_take(void) {
    for (uint32_t k = 0; k < PF_PAIRS; k++) {
        uint32_t p = (PF.next + k) % PF_PAIRS;
        if (!PF.used[p]) {
            PF.used[p] = 1;
            PF.next = (p + 1) % PF_PAIRS;
            return p;
        }
    }
    return PF_PAIRS;
}
static void pf_push(uint8_t kind, uint32_t pair) { /* PF.mx held */
    if ((PF.head + 1) % PF_EVS == PF.tail) { /* full: never with PF_EVS > 2 x PF_PAIRS */
        PF.used[pair] = 0;
        PF.lost++;
        return;
    }
    PF.ev[PF.head] = (pf_ev){.kind = kind, .pair = pair, .frame = PF.frame};
    PF.head = (PF.head + 1) % PF_EVS;
}

/* An eye mark waits for its command buffer's submit, which gives it its frame: DXVK records frame N+1 on its CS
 * thread while its submit thread still submits and presents frame N, so the frame counted when the mark is recorded
 * is not the one that runs it (11-03: every mark fell outside its frame's spans - "eye times outside" one a frame). */
#define PF_PEND 32u
static struct {
    VkCommandBuffer cb;
    uint32_t pair;
    uint8_t kind;
    uint64_t frame;
} pf_pend[PF_PEND];
static uint32_t pf_npend;
static void pf_hold(uint32_t pair) { /* PF.mx held: a query pair whose command buffer may still run */
    PF.used[pair] = 2, PF.back_until[pair] = PF.frame + 4 * PF_LATE, PF.nback++;
}
static void pf_pend_add(VkCommandBuffer cb, uint32_t pair, uint8_t kind) { /* PF.mx held */
    if (pf_npend == PF_PEND) {
        pf_hold(pf_pend[0].pair), PF.lost++;
        memmove(pf_pend, pf_pend + 1, (PF_PEND - 1) * sizeof pf_pend[0]);
        pf_npend--;
    }
    pf_pend[pf_npend].cb = cb, pf_pend[pf_npend].pair = pair, pf_pend[pf_npend].kind = kind;
    pf_pend[pf_npend++].frame = PF.frame;
}
static void pf_pend_cb(VkCommandBuffer cb) { /* PF.mx held: cb was submitted now */
    for (uint32_t i = 0; i < pf_npend;)
        if (pf_pend[i].cb == cb) {
            pf_push(pf_pend[i].kind, pf_pend[i].pair);
            memmove(pf_pend + i, pf_pend + i + 1, (pf_npend - i - 1) * sizeof pf_pend[0]);
            pf_npend--;
        } else
            i++;
}
static void pf_pend_age(void) { /* PF.mx held: recorded but never submitted (dropped or re-recorded) */
    for (uint32_t i = 0; i < pf_npend;)
        if (pf_pend[i].frame + PF_LATE < PF.frame) {
            pf_hold(pf_pend[i].pair), PF.lost++;
            memmove(pf_pend + i, pf_pend + i + 1, (pf_npend - i - 1) * sizeof pf_pend[0]);
            pf_npend--;
        } else
            i++;
}
static void pf_pend_submit(const VkSubmitInfo *si, uint32_t n) {
    if (!pf_npend || !si) return;
    pthread_mutex_lock(&PF.mx);
    for (uint32_t i = 0; i < n && pf_npend; i++)
        for (uint32_t k = 0; k < si[i].commandBufferCount && pf_npend; k++) pf_pend_cb(si[i].pCommandBuffers[k]);
    pthread_mutex_unlock(&PF.mx);
}
static void pf_pend_submit2(const VkSubmitInfo2 *si, uint32_t n) {
    if (!pf_npend || !si) return;
    pthread_mutex_lock(&PF.mx);
    for (uint32_t i = 0; i < n && pf_npend; i++)
        for (uint32_t k = 0; k < si[i].commandBufferInfoCount && pf_npend; k++)
            pf_pend_cb(si[i].pCommandBufferInfos[k].commandBuffer);
    pthread_mutex_unlock(&PF.mx);
}

static int pf_batch_plain(const VkSubmitInfo *s) { /* no device-group or protected submit (their counts follow ours) */
    for (const VkBaseInStructure *p = s->pNext; p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO || p->sType == VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO)
            return 0;
    return s->commandBufferCount <= PF_MAXCB;
}

/* the game's vkQueueSubmit on the session queue (the keep-alive's hook calls it): ours first and last in the call */
static VkResult pf_submit(dev_st *ds, VkQueue q, uint32_t n, const VkSubmitInfo *si, VkFence f) {
    if (PF.ready && q == PF.q && pf_in_ours) { /* the runtime's, inside our bracket: its time is ours */
        PF.sub_rt++;
        return ds->QueueSubmit(q, n, si, f);
    }
    if (!PF.ready || q != PF.q || !n || n > 16 || !si || !pf_batch_plain(&si[0]) || !pf_batch_plain(&si[n - 1])) {
        if (PF.ready && q == PF.q) PF.untimed++;
        VkResult r0 = ds->QueueSubmit(q, n, si, f);
        if (PF.ready && r0 == VK_SUCCESS) pf_pend_submit(si, n);
        return r0;
    }
    pthread_mutex_lock(&PF.mx);
    uint32_t p = pf_take();
    pthread_mutex_unlock(&PF.mx);
    if (p == PF_PAIRS) {
        PF.untimed++;
        VkResult r0 = ds->QueueSubmit(q, n, si, f);
        if (r0 == VK_SUCCESS) pf_pend_submit(si, n);
        return r0;
    }
    VkSubmitInfo m[16];
    memcpy(m, si, n * sizeof *si);
    VkCommandBuffer a[PF_MAXCB + 2], z[PF_MAXCB + 1];
    a[0] = PF.cb_b[p];
    if (si[0].commandBufferCount) memcpy(a + 1, si[0].pCommandBuffers, si[0].commandBufferCount * sizeof *a);
    m[0].pCommandBuffers = a;
    m[0].commandBufferCount = si[0].commandBufferCount + 1;
    if (n == 1) {
        a[m[0].commandBufferCount++] = PF.cb_e[p];
    } else {
        if (si[n - 1].commandBufferCount) memcpy(z, si[n - 1].pCommandBuffers, si[n - 1].commandBufferCount * sizeof *z);
        z[si[n - 1].commandBufferCount] = PF.cb_e[p];
        m[n - 1].pCommandBuffers = z;
        m[n - 1].commandBufferCount = si[n - 1].commandBufferCount + 1;
    }
    VkResult r = ds->QueueSubmit(q, n, m, f);
    pthread_mutex_lock(&PF.mx);
    if (r == VK_SUCCESS) {
        pf_push(PF_GAME, p), PF.subs++;
        for (uint32_t i = 0; i < n; i++) PF.waits_game += si[i].waitSemaphoreCount > 0;
    }
    else
        PF.used[p] = 0;
    pthread_mutex_unlock(&PF.mx);
    if (r == VK_SUCCESS) pf_pend_submit(si, n);
    return r;
}

/* winport: DXVK submits with vkQueueSubmit2 (synchronization2) and copies with vkCmdCopyImage2: the same timing for
 * them (ours first and last in the call; the copy into an eye image = eye e's pass done) */
static int pf_batch2_plain(const VkSubmitInfo2 *s) {
    for (const VkBaseInStructure *p = s->pNext; p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO || p->sType == VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO)
            return 0;
    return !(s->flags & VK_SUBMIT_PROTECTED_BIT) && s->commandBufferInfoCount <= PF_MAXCB;
}
static VkResult pf_submit2(dev_st *ds, VkQueue q, uint32_t n, const VkSubmitInfo2 *si, VkFence f, int khr) {
    PFN_vkQueueSubmit2 sub = khr ? ds->QueueSubmit2KHR : ds->QueueSubmit2;
    if (PF.ready && q == PF.q && pf_in_ours) {
        PF.sub_rt++;
        return sub(q, n, si, f);
    }
    if (!PF.ready || q != PF.q || !n || n > 16 || !si || !pf_batch2_plain(&si[0]) || !pf_batch2_plain(&si[n - 1])) {
        if (PF.ready && q == PF.q) PF.untimed++;
        VkResult r0 = sub(q, n, si, f);
        if (PF.ready && r0 == VK_SUCCESS) pf_pend_submit2(si, n);
        return r0;
    }
    pthread_mutex_lock(&PF.mx);
    uint32_t p = pf_take();
    pthread_mutex_unlock(&PF.mx);
    if (p == PF_PAIRS) {
        PF.untimed++;
        VkResult r0 = sub(q, n, si, f);
        if (r0 == VK_SUCCESS) pf_pend_submit2(si, n);
        return r0;
    }
    VkSubmitInfo2 m[16];
    memcpy(m, si, n * sizeof *si);
    VkCommandBufferSubmitInfo a[PF_MAXCB + 2], z[PF_MAXCB + 1];
    const VkCommandBufferSubmitInfo cb0 = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = PF.cb_b[p]},
                                    cb1 = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = PF.cb_e[p]};
    a[0] = cb0;
    if (si[0].commandBufferInfoCount) memcpy(a + 1, si[0].pCommandBufferInfos, si[0].commandBufferInfoCount * sizeof *a);
    m[0].pCommandBufferInfos = a;
    m[0].commandBufferInfoCount = si[0].commandBufferInfoCount + 1;
    if (n == 1) {
        a[m[0].commandBufferInfoCount++] = cb1;
    } else {
        if (si[n - 1].commandBufferInfoCount)
            memcpy(z, si[n - 1].pCommandBufferInfos, si[n - 1].commandBufferInfoCount * sizeof *z);
        z[si[n - 1].commandBufferInfoCount] = cb1;
        m[n - 1].pCommandBufferInfos = z;
        m[n - 1].commandBufferInfoCount = si[n - 1].commandBufferInfoCount + 1;
    }
    VkResult r = sub(q, n, m, f);
    pthread_mutex_lock(&PF.mx);
    if (r == VK_SUCCESS) {
        pf_push(PF_GAME, p), PF.subs++;
        for (uint32_t i = 0; i < n; i++) PF.waits_game += si[i].waitSemaphoreInfoCount > 0;
    } else
        PF.used[p] = 0;
    pthread_mutex_unlock(&PF.mx);
    if (r == VK_SUCCESS) pf_pend_submit2(si, n);
    return r;
}
/* the eye of an eye image (H8's targets, the preload's published VkImages), else -1 */
static int pf_eye_of(dev_st *ds, VkImage dst) {
    somvr_eyes_info in;
    if (!PF.ready || ds != PF.ds || !X.sh || !dst || !somvr_eyes_read(X.sh, &in)) return -1;
    return in.image[0] && dst == (VkImage)(uintptr_t)in.image[0] ? 0 : in.image[1] && dst == (VkImage)(uintptr_t)in.image[1] ? 1 : -1;
}
/* a write into eye e's image (outside a render pass): its timestamp first = "eye e's pass is done" */
static void pf_eye_mark(VkCommandBuffer cmd, int e, int kind) {
    if (e < 0) return;
    pthread_mutex_lock(&PF.mx);
    PF.ew[kind]++;
    int first = !(PF.ew_seen & 1 << kind);
    PF.ew_seen |= 1 << kind;
    uint32_t p = pf_take();
    if (p != PF_PAIRS) {
        PF.CmdResetQueryPool(cmd, PF.qp, 2 * p, 2);
        PF.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, PF.qp, 2 * p);
        pf_pend_add(cmd, p, e ? PF_EYE1 : PF_EYE0); /* its frame comes with its command buffer's submit */
    }
    pthread_mutex_unlock(&PF.mx);
    if (first) lg("xr: perf: the game writes eye %d's image by a %s: the eye's GPU time ends there", e, pf_ew_name[kind]);
}
static dev_st *pf_dev(VkCommandBuffer cmd) {
    return PF.ds && dkey(cmd) == PF.ds->key ? PF.ds : LIST_FIND(g_devs, key, dkey(cmd));
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdCopyImage2(VkCommandBuffer cmd, const VkCopyImageInfo2 *ci) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdCopyImage2) return;
    if (ci) pf_eye_mark(cmd, pf_eye_of(ds, ci->dstImage), PF_EW_COPY);
    ds->CmdCopyImage2(cmd, ci);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdCopyImage2KHR(VkCommandBuffer cmd, const VkCopyImageInfo2 *ci) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdCopyImage2KHR) return;
    if (ci) pf_eye_mark(cmd, pf_eye_of(ds, ci->dstImage), PF_EW_COPY);
    ds->CmdCopyImage2KHR(cmd, ci);
}

/* H8's copy into an eye image (the game's command buffer, outside a render pass): "eye e's pass is done" */
static VKAPI_ATTR void VKAPI_CALL L_PfCmdCopyImage(VkCommandBuffer cmd, VkImage src, VkImageLayout sl, VkImage dst,
                                                    VkImageLayout dl, uint32_t nr, const VkImageCopy *rg) {
    dev_st *ds = pf_dev(cmd);
    if (!ds) return;
    pf_eye_mark(cmd, pf_eye_of(ds, dst), PF_EW_COPY);
    ds->CmdCopyImage(cmd, src, sl, dst, dl, nr, rg);
}

/* winport: the other ways an eye image can be written. A blit or a resolve names the image; a rendering names views:
 * the views of the eye image candidates (vkCreateImageView of an image in g_cand) are kept in a small table */
#define PF_VIEWS 64u
static struct {
    VkImageView v;
    VkImage img;
} pf_view[PF_VIEWS];
static uint32_t pf_view_next;
static VKAPI_ATTR VkResult VKAPI_CALL L_PfCreateImageView(VkDevice dev, const VkImageViewCreateInfo *ci,
                                                          const VkAllocationCallbacks *a, VkImageView *out) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds || !ds->CreateImageView) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = ds->CreateImageView(dev, ci, a, out);
    if (r != VK_SUCCESS || !ci || !out) return r;
    int cand = 0;
    pthread_mutex_lock(&g_cand_mx);
    for (int i = 0; i < MAX_EYE_CAND && !cand; i++) cand = g_cand[i].img == ci->image && g_cand[i].dev == dev;
    pthread_mutex_unlock(&g_cand_mx);
    if (cand) {
        pthread_mutex_lock(&PF.mx);
        uint32_t k = pf_view_next++ % PF_VIEWS;
        pf_view[k].v = *out, pf_view[k].img = ci->image;
        pthread_mutex_unlock(&PF.mx);
    }
    return r;
}
static VKAPI_ATTR void VKAPI_CALL L_PfDestroyImageView(VkDevice dev, VkImageView v, const VkAllocationCallbacks *a) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds || !ds->DestroyImageView) return;
    if (v) {
        pthread_mutex_lock(&PF.mx);
        for (uint32_t k = 0; k < PF_VIEWS; k++)
            if (pf_view[k].v == v) pf_view[k].v = VK_NULL_HANDLE, pf_view[k].img = VK_NULL_HANDLE;
        pthread_mutex_unlock(&PF.mx);
    }
    ds->DestroyImageView(dev, v, a);
}
static VkImage pf_view_image(VkImageView v) {
    VkImage img = VK_NULL_HANDLE;
    if (!v) return img;
    pthread_mutex_lock(&PF.mx);
    for (uint32_t k = 0; k < PF_VIEWS && !img; k++)
        if (pf_view[k].v == v) img = pf_view[k].img;
    pthread_mutex_unlock(&PF.mx);
    return img;
}
static void pf_rendering_mark(dev_st *ds, VkCommandBuffer cmd, const VkRenderingInfo *ri) {
    if (!ri || !PF.ready || ds != PF.ds || !pf_view_next) return;
    for (uint32_t i = 0; i < ri->colorAttachmentCount; i++) {
        const VkRenderingAttachmentInfo *at = &ri->pColorAttachments[i];
        int e = pf_eye_of(ds, pf_view_image(at->imageView));
        if (e < 0) e = pf_eye_of(ds, pf_view_image(at->resolveImageView));
        if (e >= 0) {
            pf_eye_mark(cmd, e, PF_EW_RENDER);
            return;
        }
    }
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo *ri) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdBeginRendering) return;
    pf_rendering_mark(ds, cmd, ri);
    ds->CmdBeginRendering(cmd, ri);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdBeginRenderingKHR(VkCommandBuffer cmd, const VkRenderingInfo *ri) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdBeginRenderingKHR) return;
    pf_rendering_mark(ds, cmd, ri);
    ds->CmdBeginRenderingKHR(cmd, ri);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdBlitImage(VkCommandBuffer cmd, VkImage src, VkImageLayout sl, VkImage dst,
                                                    VkImageLayout dl, uint32_t nr, const VkImageBlit *rg, VkFilter f) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdBlitImage) return;
    pf_eye_mark(cmd, pf_eye_of(ds, dst), PF_EW_BLIT);
    ds->CmdBlitImage(cmd, src, sl, dst, dl, nr, rg, f);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdBlitImage2(VkCommandBuffer cmd, const VkBlitImageInfo2 *bi) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdBlitImage2) return;
    if (bi) pf_eye_mark(cmd, pf_eye_of(ds, bi->dstImage), PF_EW_BLIT);
    ds->CmdBlitImage2(cmd, bi);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdBlitImage2KHR(VkCommandBuffer cmd, const VkBlitImageInfo2 *bi) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdBlitImage2KHR) return;
    if (bi) pf_eye_mark(cmd, pf_eye_of(ds, bi->dstImage), PF_EW_BLIT);
    ds->CmdBlitImage2KHR(cmd, bi);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdResolveImage(VkCommandBuffer cmd, VkImage src, VkImageLayout sl, VkImage dst,
                                                       VkImageLayout dl, uint32_t nr, const VkImageResolve *rg) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdResolveImage) return;
    pf_eye_mark(cmd, pf_eye_of(ds, dst), PF_EW_RESOLVE);
    ds->CmdResolveImage(cmd, src, sl, dst, dl, nr, rg);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdResolveImage2(VkCommandBuffer cmd, const VkResolveImageInfo2 *ri) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdResolveImage2) return;
    if (ri) pf_eye_mark(cmd, pf_eye_of(ds, ri->dstImage), PF_EW_RESOLVE);
    ds->CmdResolveImage2(cmd, ri);
}
static VKAPI_ATTR void VKAPI_CALL L_PfCmdResolveImage2KHR(VkCommandBuffer cmd, const VkResolveImageInfo2 *ri) {
    dev_st *ds = pf_dev(cmd);
    if (!ds || !ds->CmdResolveImage2KHR) return;
    if (ri) pf_eye_mark(cmd, pf_eye_of(ds, ri->dstImage), PF_EW_RESOLVE);
    ds->CmdResolveImage2KHR(cmd, ri);
}

static PFN_vkVoidFunction pf_dev_hook(const char *n) {
    if (!PF.on) return NULL;
    if (!strcmp(n, "vkCmdCopyImage")) return (PFN_vkVoidFunction)L_PfCmdCopyImage;
    if (!strcmp(n, "vkCmdCopyImage2")) return (PFN_vkVoidFunction)L_PfCmdCopyImage2;
    if (!strcmp(n, "vkCmdCopyImage2KHR")) return (PFN_vkVoidFunction)L_PfCmdCopyImage2KHR;
    static const struct {
        const char *n;
        PFN_vkVoidFunction f;
    } ew[] = {
        {"vkCreateImageView", (PFN_vkVoidFunction)L_PfCreateImageView},
        {"vkDestroyImageView", (PFN_vkVoidFunction)L_PfDestroyImageView},
        {"vkCmdBeginRendering", (PFN_vkVoidFunction)L_PfCmdBeginRendering},
        {"vkCmdBeginRenderingKHR", (PFN_vkVoidFunction)L_PfCmdBeginRenderingKHR},
        {"vkCmdBlitImage", (PFN_vkVoidFunction)L_PfCmdBlitImage},
        {"vkCmdBlitImage2", (PFN_vkVoidFunction)L_PfCmdBlitImage2},
        {"vkCmdBlitImage2KHR", (PFN_vkVoidFunction)L_PfCmdBlitImage2KHR},
        {"vkCmdResolveImage", (PFN_vkVoidFunction)L_PfCmdResolveImage},
        {"vkCmdResolveImage2", (PFN_vkVoidFunction)L_PfCmdResolveImage2},
        {"vkCmdResolveImage2KHR", (PFN_vkVoidFunction)L_PfCmdResolveImage2KHR},
    };
    for (size_t i = 0; i < sizeof ew / sizeof ew[0]; i++)
        if (!strcmp(n, ew[i].n)) return ew[i].f;
    if (!strcmp(n, "vkQueueSubmit")) return (PFN_vkVoidFunction)L_QueueSubmit; /* calls pf_submit */
    return NULL;
}

static int pf_cmp(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
/* "mean/p50/p95/max" of n values (sorted in place) */
static void pf_stat(char *o, size_t cap, float *v, uint32_t n) {
    if (!n) {
        snprintf(o, cap, "-");
        return;
    }
    double s = 0;
    for (uint32_t i = 0; i < n; i++) s += v[i];
    qsort(v, n, sizeof *v, pf_cmp);
    snprintf(o, cap, "%.2f/%.2f/%.2f/%.2f", s / n, v[n / 2], v[(n * 95) / 100 < n ? (n * 95) / 100 : n - 1], v[n - 1]);
}
static uint32_t pf_over(const float *v, uint32_t n, double ms) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) c += v[i] > ms;
    return c;
}

/* winport (docs/re-winport.md 7.1): every thread's CPU time from /proc/self/task/<tid>/schedstat (ns on the CPU), by
 * its name. Under Wine the Windows side has no usable per-thread CPU clock (GetThreadTimes ticks 10 ms; a raw Linux
 * syscall from PE code is caught by the syscall user dispatch), so the layer measures the threads the DLL names
 * (somvr-game = the game's main thread, somvr-render = the thread calling Present) and DXVK's own (dxvk-cs records
 * the Vulkan commands, dxvk-submit, dxvk-queue, the rest dxvk-*). One scan a window: the per-frame CPU per group. */
enum { PFT_GAME, PFT_RENDER, PFT_CS, PFT_SUBMIT, PFT_QUEUE, PFT_DXVK, PFT_ALL, PFT_N };
static const char *const pft_name[PFT_N] = {"somvr-game", "somvr-render", "dxvk-cs", "dxvk-submit", "dxvk-queue",
                                            "other dxvk-*", "all threads"};
static struct {
    double ns[PFT_N]; /* the last scan's sums */
    int have, nthreads;
} PT;
static void pft_scan(double out[PFT_N], int *nthreads) {
    memset(out, 0, sizeof(double) * PFT_N);
    *nthreads = 0;
    DIR *d = opendir("/proc/self/task");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[300], comm[32] = "", st[96] = "";
        snprintf(p, sizeof p, "/proc/self/task/%s/comm", e->d_name);
        FILE *f = fopen(p, "re");
        if (f) {
            if (!fgets(comm, sizeof comm, f)) comm[0] = 0;
            fclose(f);
        }
        comm[strcspn(comm, "\n")] = 0;
        snprintf(p, sizeof p, "/proc/self/task/%s/schedstat", e->d_name);
        if (!(f = fopen(p, "re"))) continue;
        double ns = fgets(st, sizeof st, f) ? strtod(st, NULL) : 0;
        fclose(f);
        int g = !strcmp(comm, "somvr-game")     ? PFT_GAME
                : !strcmp(comm, "somvr-render") ? PFT_RENDER
                : !strcmp(comm, "dxvk-cs")      ? PFT_CS
                : !strcmp(comm, "dxvk-submit")  ? PFT_SUBMIT
                : !strcmp(comm, "dxvk-queue")   ? PFT_QUEUE
                : !strncmp(comm, "dxvk-", 5)    ? PFT_DXVK
                                                : -1;
        if (g >= 0) out[g] += ns;
        out[PFT_ALL] += ns;
        (*nthreads)++;
    }
    closedir(d);
}
static void pft_log(uint32_t frames) { /* PF.mx held: the window's per-frame CPU of each thread group */
    double now_ns[PFT_N];
    int nt;
    pft_scan(now_ns, &nt);
    if (PT.have && frames) {
        char o[512];
        size_t k = 0;
        for (int g = 0; g < PFT_N && k < sizeof o; g++)
            k += (size_t)snprintf(o + k, sizeof o - k, "%s%s %.2f", g ? ", " : "", pft_name[g],
                                  (now_ns[g] - PT.ns[g]) * 1e-6 / frames);
        lg("xr: perf%s%s: the threads' CPU per frame (ms; /proc schedstat, %u frames, %d threads): %s", PF.label[0] ? " " : "",
           PF.label, frames, nt, o);
    }
    memcpy(PT.ns, now_ns, sizeof PT.ns);
    PT.have = 1, PT.nthreads = nt;
}

static void pf_log(double now) {
    char g[64], o[64], t[64], a[64], b[64], c[64], sp[64], x[64], w[64], xc[64], pw[64], pc[64], ac[64];
    double bud = X.fs.predictedDisplayPeriod > 0 ? (double)X.fs.predictedDisplayPeriod * 1e-6 : PF_BUDGET_MS; /* 80 Hz: 12.5 */
    uint32_t over = pf_over(PF.tot, PF.n, bud), over_g = pf_over(PF.game, PF.n, bud);
    uint32_t n = PF.n, ns = PF.nsplit, nc = PF.ncpu;
    pf_stat(g, sizeof g, PF.game, n);
    pf_stat(o, sizeof o, PF.ours, n);
    pf_stat(t, sizeof t, PF.tot, n);
    pf_stat(sp, sizeof sp, PF.span, n);
    pf_stat(a, sizeof a, PF.e0, ns);
    pf_stat(b, sizeof b, PF.e1, ns);
    pf_stat(c, sizeof c, PF.rest, ns);
    pf_stat(x, sizeof x, PF.cpu_x, nc);
    pf_stat(w, sizeof w, PF.cpu_w, nc);
    pf_stat(xc, sizeof xc, PF.cpu_xc, nc);
    pf_stat(pw, sizeof pw, PF.cpu_pw, nc);
    pf_stat(pc, sizeof pc, PF.cpu_pc, nc);
    pf_stat(ac, sizeof ac, PF.cpu_ac, nc);
    int mhz, busy;
    pf_sysfs_read(&mhz, &busy);
    lg("xr: perf%s%s: %.1f s, %u frames (mean/p50/p95/max ms): GPU total %s (over %.1f ms: %u; the game alone: %u) = the game "
       "%s + ours %s; the game split at the eye copies (%u frames): eye 0 + shared %s, eye 1 %s, the rest %s; the game's "
       "first submit to its last end %s; submits %.1f a frame (with a semaphore wait %llu, the runtime's %llu, untimed "
       "%llu, lost %llu, eye times outside %llu; the eye images written by copy %llu, draw %llu, blit %llu, resolve %llu); "
       "the render thread's present path: ours (xr_frame) wall %s cpu %s, the "
       "driver's present wall %s cpu %s, xrWaitFrame wall %s, after it (the input sync) cpu %s; the shader clock %d MHz, "
       "busy %d%%",
       PF.label[0] ? " " : "", PF.label, now - PF.t0, n, t, bud, over, over_g, g, o, ns, a, b, c, sp, n ? (double)PF.nsub_w / n : 0.0,
       (unsigned long long)PF.waits_game, (unsigned long long)PF.sub_rt, (unsigned long long)PF.untimed, (unsigned long long)PF.lost, (unsigned long long)PF.eyes_odd,
       (unsigned long long)PF.ew[PF_EW_COPY], (unsigned long long)PF.ew[PF_EW_RENDER], (unsigned long long)PF.ew[PF_EW_BLIT],
       (unsigned long long)PF.ew[PF_EW_RESOLVE], x, xc, pw, pc, w, ac, mhz, busy);
    memset(PF.ew, 0, sizeof PF.ew);
    if (PS.n) { /* the stereo present path by part */
        char pb[1024];
        size_t po = 0;
        for (int k = 0; k < PF_NSEC && po < sizeof pb; k++)
            po += (size_t)snprintf(pb + po, sizeof pb - po, "%s%s %.3f/%.3f/%.2f", k ? ", " : "", pf_sec_name[k],
                                   PS.sw[k] * 1e3 / PS.n, PS.su[k] * 1e3 / PS.n, PS.mx[k]);
        lg("xr: perf%s%s: the stereo present path by part (%u frames; wall mean / CPU mean / wall max ms): %s",
           PF.label[0] ? " " : "", PF.label, PS.n, pb);
    }
    pft_log(n); /* winport: the threads' CPU (the only per-thread CPU clock under Wine) */
    memset(&PS.sw, 0, sizeof PS.sw), memset(&PS.su, 0, sizeof PS.su), memset(&PS.mx, 0, sizeof PS.mx), PS.n = 0;
    PF.n = PF.nsplit = PF.ncpu = 0;
    PF.nsub_w = 0;
    PF.t0 = now;
}

static double pf_ov(double s, double e, double lo, double hi) { /* [s, e] inside [lo, hi] */
    double a = s > lo ? s : lo, b = e < hi ? e : hi;
    return b > a ? b - a : 0.0;
}
/* the gathered frame's sums into the window (PF.mx held) */
static void pf_finish(void) {
    if (!PF.cf_have || !PF.nseg) {
        PF.cf_have = 0;
        return;
    }
    double game = 0, ours = 0, e0 = 0, e1 = 0, rest = 0, first = -1, last = 0;
    double pe = PF.prev_end;
    int split = PF.eyes == 3 && PF.eye[0] <= PF.eye[1];
    for (uint32_t i = 0; i < PF.nseg; i++) {
        double s = PF.sb[i] > pe ? PF.sb[i] : pe, e = PF.se[i];
        if (e > pe) pe = e;
        if (e <= s) continue;
        if (PF.sk[i] == PF_GAME) {
            game += e - s;
            if (first < 0 || PF.sb[i] < first) first = PF.sb[i];
            if (e > last) last = e;
            if (split) {
                e0 += pf_ov(s, e, s, PF.eye[0]);
                e1 += pf_ov(s, e, PF.eye[0], PF.eye[1]);
                rest += pf_ov(s, e, PF.eye[1], e);
            }
        } else {
            ours += e - s;
        }
    }
    if (split && (PF.eye[0] < first || PF.eye[1] > last)) split = 0, PF.eyes_odd++;
    PF.prev_end = pe;
    if (PF.n < PF_WIN) {
        uint32_t k = PF.n++;
        PF.game[k] = (float)(game * 1e-6), PF.ours[k] = (float)(ours * 1e-6), PF.tot[k] = (float)((game + ours) * 1e-6);
        PF.span[k] = first >= 0 ? (float)((last - first) * 1e-6) : 0.0f;
        PF.nsub_w += PF.nsub;
        if (split && PF.nsplit < PF_WIN) {
            uint32_t j = PF.nsplit++;
            PF.e0[j] = (float)(e0 * 1e-6), PF.e1[j] = (float)(e1 * 1e-6), PF.rest[j] = (float)(rest * 1e-6);
        }
    }
    PF.frames++;
    PF.cf_have = 0, PF.nseg = PF.nsub = 0, PF.eyes = 0;
}

/* read back what the GPU has done, in submit order, without waiting (PF.mx held) */
static void pf_harvest(dev_st *ds) {
    while (PF.tail != PF.head) {
        pf_ev *v = &PF.ev[PF.tail];
        uint64_t r[4] = {0};
        int two = v->kind != PF_EYE0 && v->kind != PF_EYE1;
        VkResult vr = PF.GetQueryPoolResults(ds->dev, PF.qp, 2 * v->pair, two ? 2 : 1, sizeof r, r, 16,
                                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        int ok = (vr == VK_SUCCESS || vr == VK_NOT_READY) && r[1] && (!two || r[3]);
        if (!ok) {
            if (PF.frame < v->frame + PF_LATE) break; /* not done yet: later */
            PF.lost++; /* never read back (a command buffer the game dropped?): its pair held back 4 x longer */
            PF.used[v->pair] = 2, PF.back_until[v->pair] = PF.frame + 4 * PF_LATE, PF.nback++;
            PF.tail = (PF.tail + 1) % PF_EVS;
            continue;
        }
        if (PF.cf_have && v->frame != PF.cf) pf_finish();
        PF.cf = v->frame, PF.cf_have = 1;
        double b = (double)(r[0] & PF.mask) * PF.tick_ns;
        if (two) {
            double e = (double)(r[2] & PF.mask) * PF.tick_ns;
            if (PF.nseg < PF_SEGS && e >= b) {
                PF.sb[PF.nseg] = b, PF.se[PF.nseg] = e, PF.sk[PF.nseg] = v->kind;
                PF.nseg++;
            }
            PF.nsub += v->kind == PF_GAME;
        } else {
            int e = v->kind == PF_EYE1;
            PF.eye[e] = b, PF.eyes |= 1 << e;
        }
        PF.used[v->pair] = 0;
        PF.tail = (PF.tail + 1) % PF_EVS;
    }
    pf_pend_age();
    for (uint32_t p = 0; PF.nback && p < PF_PAIRS; p++) /* the held-back pairs of dropped events */
        if (PF.used[p] == 2 && PF.back_until[p] <= PF.frame) PF.used[p] = 0, PF.nback--;
}

static void pf_submit_one(dev_st *ds, VkCommandBuffer cb) {
    VkSubmitInfo s = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    VkResult r = ds->QueueSubmit(PF.q, 1, &s, VK_NULL_HANDLE);
    if (r != VK_SUCCESS) pf_off("vkQueueSubmit (our bracket): %d", r);
}

/* the present of the session queue, before xr_frame (X.mx held): the read-back, the bracket's start */
static void pf_present_begin(dev_st *ds, q_st *qi) {
    if (!PF.on || PF.failed || X.phase != P_SESSION || qi->q != X.q) return;
    if (!PF.ready) pf_setup(ds, qi->q, qi->family);
    if (!PF.ready) return;
    PF.x0 = pf_now(), PF.xc0 = pf_tcpu();
    PS.t = PF.x0, PS.c = PF.xc0, PS.stereo = 0;
    memset(PS.w, 0, sizeof PS.w), memset(PS.u, 0, sizeof PS.u);
    pthread_mutex_lock(&PF.mx);
    pf_harvest(ds);
    uint32_t p = pf_take();
    pthread_mutex_unlock(&PF.mx);
    PF.ours_pair = p;
    if (p != PF_PAIRS) pf_submit_one(ds, PF.cb_b[p]);
    pf_in_ours = 1;
}
/* after xr_frame, before the real present: the bracket's end, the frame counted */
static void pf_present_end(dev_st *ds, q_st *qi) {
    if (!PF.ready || qi->q != PF.q || !pf_in_ours) return;
    pf_sec(PF_NSEC - 1); /* the rest: the frame's end */
    if (PS.stereo) {
        for (int k = 0; k < PF_NSEC; k++) {
            PS.sw[k] += PS.w[k], PS.su[k] += PS.u[k];
            if (PS.w[k] * 1e3 > PS.mx[k]) PS.mx[k] = (float)(PS.w[k] * 1e3);
        }
        PS.n++;
    }
    pf_in_ours = 0;
    uint32_t p = PF.ours_pair;
    PF.ours_pair = PF_PAIRS;
    if (p != PF_PAIRS) pf_submit_one(ds, PF.cb_e[p]);
    double now = pf_now(), cpu = pf_tcpu();
    pthread_mutex_lock(&PF.mx);
    if (p != PF_PAIRS) {
        if (PF.ready) pf_push(PF_OURS, p);
        else PF.used[p] = 0;
    }
    PF.frame++;
    if (PF.ncpu < PF_WIN) {
        uint32_t k = PF.ncpu++;
        PF.cpu_x[k] = (float)((now - PF.x0) * 1e3), PF.cpu_xc[k] = (float)((cpu - PF.xc0) * 1e3);
        PF.cpu_w[k] = PF.cpu_pw[k] = PF.cpu_pc[k] = PF.cpu_ac[k] = 0.0f;
    }
    if (now - PF.t0 >= PF.period_s) pf_log(now);
    pthread_mutex_unlock(&PF.mx);
}
/* the real present's call (the driver's WSI) into the last present's sample */
static void pf_present_call(double wall_ms, double cpu_ms) {
    if (!PF.ready) return;
    pthread_mutex_lock(&PF.mx);
    if (PF.ncpu) PF.cpu_pw[PF.ncpu - 1] = (float)wall_ms, PF.cpu_pc[PF.ncpu - 1] = (float)cpu_ms;
    pthread_mutex_unlock(&PF.mx);
}
/* after the real present (the present thread): xrWaitFrame's own time (blocking: the runtime's pace) and the CPU of
 * the rest of the wait's step (the input sync, the views' publish) into the last present's sample */
static void pf_wait_ms(double wait_ms, double rest_cpu_ms) {
    if (!PF.ready) return;
    pthread_mutex_lock(&PF.mx);
    if (PF.ncpu) PF.cpu_w[PF.ncpu - 1] = (float)wait_ms, PF.cpu_ac[PF.ncpu - 1] = (float)rest_cpu_ms;
    pthread_mutex_unlock(&PF.mx);
}

/* SOMVR_XR_TEST `perf NAME` (headless tests): the window so far logged under its name, the next one named NAME */
static int pf_xt(const char *line) {
    if (strncmp(line, "perf ", 5)) return 0;
    char nm[sizeof PF.label];
    snprintf(nm, sizeof nm, "%.*s", (int)strcspn(line + 5, "\r\n"), line + 5);
    if (!PF.ready) return lg("xr: test: 'perf %s': perf is off", nm), 1;
    pthread_mutex_lock(&PF.mx);
    pf_log(pf_now());
    snprintf(PF.label, sizeof PF.label, "%s", nm);
    pthread_mutex_unlock(&PF.mx);
    return 1;
}

static void pf_destroy(dev_st *ds) { /* the device's end (idle): ours go with it */
    if (PF.ds != ds) return;
    PF.ready = 0;
    if (PF.pool) ds->DestroyCommandPool(ds->dev, PF.pool, NULL);
    if (PF.qp && PF.DestroyQueryPool) PF.DestroyQueryPool(ds->dev, PF.qp, NULL);
    PF.pool = VK_NULL_HANDLE, PF.qp = VK_NULL_HANDLE, PF.ds = NULL, PF.q = VK_NULL_HANDLE;
}
