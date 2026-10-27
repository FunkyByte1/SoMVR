/* somvr_objective_layer.c - SOMVR_OBJECTIVE's marker (docs/re-xr-pipeline.md 44, ../somvr_objective.h; X.mx held, the
 * present thread; included by somvr_vklayer.c after the void's file). Every stereo frame from a record reads that
 * frame's entry of the preload's `somvr_objective` (or the newest within 4 frames) and adds one quad (XL_OBJ: over
 * the ring, under the HUD; the first to go after the backdrop when a frame would carry more than the runtime takes):
 * in view the diamond with the distance, world-locked over the objective's direction in the reference space
 * (billboarded to the frame's head, at the objective's distance up to SOMVR_OBJECTIVE_DIST_M, its diamond on the
 * direction), faint within SOMVR_OBJECTIVE_FAINT_DEG of the view's centre; out of view the arrow at the view's edge in
 * a VIEW space 1 m ahead, parallel to the view's plane, rolled to point the way (somvr_ob_locate). The picture (390 x
 * 130: the marker, its faint copy, the arrow) is drawn by somvr_ob_image when the distance's text changes and uploaded
 * into the next image of an ordinary swapchain (one staging buffer, its fence waited before the next write). A reuse
 * frame repeats the last quad. No object or SOMVR_XR_OBJECTIVE=off: nothing. Any failure: no marker for the session
 * (logged once); the frames go on. */
static struct {
    somvr_objective_shared *obj; /* the preload's somvr_objective (active, stereo path on), NULL = none */
    int on;                      /* this session may draw (ob_off clears it) */
    somvr_ob_cfg cfg;
    somvr_font font;
    XrSwapchain sc;
    uint32_t nimg;
    VkImage img[MAX_XR_IMAGES];
    VkFormat fmt;
    int bgra;
    dev_st *ds;
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkCommandBuffer cmd;
    VkFence fence;
    int pending, released;
    XrSpace view;                /* its own VIEW space (the edge arrow) */
    char text[16];               /* the distance in the released picture */
    int in_view;                 /* somvr_ob_locate's hysteresis */
    XrCompositionLayerQuad q, last_q;
    int has, last_has, kind_was;
    uint32_t id_was;
    uint64_t n_frames, n_marker, n_edge, n_faint, n_uploads, n_missing, n_none, n_land;
    uint32_t lines;
} OB;

static void ob_free(void) {
    dev_st *ds = OB.ds;
    if (ds && OB.pending && OB.fence && ds->WaitForFences(ds->dev, 1, &OB.fence, VK_TRUE, FENCE_TIMEOUT_NS) == VK_SUCCESS)
        OB.pending = 0;
    if (ds && !OB.pending) {
        if (OB.cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &OB.cmd);
        if (OB.fence) ds->DestroyFence(ds->dev, OB.fence, NULL);
        if (OB.map) ds->UnmapMemory(ds->dev, OB.mem);
        if (OB.buf) ds->DestroyBuffer(ds->dev, OB.buf, NULL);
        if (OB.mem) ds->FreeMemory(ds->dev, OB.mem, NULL);
    }
    OB.cmd = VK_NULL_HANDLE, OB.fence = VK_NULL_HANDLE, OB.map = NULL, OB.buf = VK_NULL_HANDLE;
    OB.mem = VK_NULL_HANDLE, OB.pending = 0, OB.ds = NULL;
    if (OB.sc) X.xrDestroySwapchain(OB.sc);
    OB.sc = XR_NULL_HANDLE, OB.nimg = 0, OB.released = 0, OB.text[0] = 0;
}
static void ob_destroy(void) { /* the swapchain, the staging buffer, the VIEW space (ours alone): before the session */
    ob_free();
    if (OB.view) X.xrDestroySpace(OB.view);
    OB.view = XR_NULL_HANDLE;
    OB.has = OB.last_has = 0;
}

/* no marker for the rest of this session (logged once); the stereo frames go on without it */
__attribute__((format(printf, 1, 2))) static void ob_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    if (OB.on)
        lg("xr: objective: off for this session: %s (the frames go on without the marker; %llu marker, %llu edge frames so far)",
           why, (unsigned long long)OB.n_marker, (unsigned long long)OB.n_edge);
    OB.on = 0;
    ob_destroy();
}

/* xr_session_setup: is there an objective marker for this session? */
static void ob_setup(void) {
    ob_destroy();
    somvr_objective_shared *o = (somvr_objective_shared *)dlsym(RTLD_DEFAULT, SOMVR_OBJECTIVE_SYMBOL);
    const char *e = getenv("SOMVR_XR_OBJECTIVE");
    int want = !e || !*e || (strcmp(e, "off") && strcmp(e, "0"));
    OB.n_frames = OB.n_marker = OB.n_edge = OB.n_faint = OB.n_uploads = OB.n_missing = OB.n_none = 0, OB.lines = 0;
    OB.in_view = 0, OB.kind_was = -1, OB.id_was = 0;
    OB.obj = X.sh && want && somvr_objective_ok(o) ? o : NULL;
    OB.on = OB.obj != NULL;
    if (e && *e && strcmp(e, "on") && strcmp(e, "1") && strcmp(e, "off") && strcmp(e, "0"))
        lg("xr: objective: SOMVR_XR_OBJECTIVE=%s is not on or off: on", e);
    if (!OB.on) {
        if (o || !want)
            lg("xr: objective: no marker (%s)", !X.sh ? "no stereo path" : !want ? "SOMVR_XR_OBJECTIVE=off"
                                                    : "somvr_objective inactive (SOMVR_OBJECTIVE off or log) or another version");
        return;
    }
    somvr_ob_cfg_default(&OB.cfg);
    if (OB.obj->size_deg >= 1 && OB.obj->size_deg <= 15) OB.cfg.size_deg = OB.obj->size_deg;
    if (OB.obj->dist_m >= 2 && OB.obj->dist_m <= 100) OB.cfg.dist_m = OB.obj->dist_m;
    if (OB.obj->faint_deg >= 0 && OB.obj->faint_deg <= 30) OB.cfg.faint_deg = OB.obj->faint_deg;
    if (OB.obj->edge_deg >= 1 && OB.obj->edge_deg <= 15) OB.cfg.edge_deg = OB.obj->edge_deg;
    if (somvr_font_init(&OB.font) != 0) lg("xr: objective: the font's art is malformed (a glyph blank)");
    lg("xr: objective: somvr_objective found at %p (v%u, preload pid %d): the marker %.1f deg world-locked over the "
       "objective (its quad at most %.0f m away, faint within %.0f deg of the view's centre), out of view an arrow %.1f deg "
       "at the view's edge %.1f m ahead", (void *)OB.obj, OB.obj->version, OB.obj->pid, OB.cfg.size_deg, OB.cfg.dist_m,
       OB.cfg.faint_deg, OB.cfg.edge_deg, OB.cfg.edge_dist_m);
}

/* the swapchain (an ordinary one: the picture changes with the distance), the staging buffer, the command buffer, the
 * fence, the VIEW space. 0 = ready, -1 = off */
static int ob_make(dev_st *ds) {
    if (OB.sc) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->ResetCommandBuffer || !ds->ResetFences || !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return ob_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return ob_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return ob_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = SOMVR_OB_IMG_W,
                                .height = SOMVR_OB_IMG_H,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &OB.sc))) {
        OB.sc = XR_NULL_HANDLE;
        return ob_off("xrCreateSwapchain(%dx%d format %d): %s", SOMVR_OB_IMG_W, SOMVR_OB_IMG_H, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(OB.sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return ob_off("xrEnumerateSwapchainImages (objective): %s, %u images", xr_str(r), n), -1;
    for (uint32_t i = 0; i < n; i++) OB.img[i] = im[i].image;
    OB.nimg = n, OB.fmt = f, OB.bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    OB.ds = ds;
    VkDeviceSize sz = (VkDeviceSize)SOMVR_OB_IMG_W * SOMVR_OB_IMG_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &OB.buf);
    if (vr != VK_SUCCESS) OB.buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    int type = -1;
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, OB.buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the staging buffer";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &OB.mem)) != VK_SUCCESS) OB.mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, OB.buf, OB.mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, OB.mem, 0, VK_WHOLE_SIZE, 0, &OB.map)) != VK_SUCCESS)
        OB.map = NULL;
    if (vr == VK_SUCCESS) {
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &OB.cmd)) != VK_SUCCESS) OB.cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, OB.cmd);
        else *(void **)OB.cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &OB.fence)) != VK_SUCCESS) OB.fence = VK_NULL_HANDLE;
    }
    if (vr != VK_SUCCESS) return ob_off("the staging buffer: %s: %d", what, vr), -1;
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                     .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
                                     .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
    if (!OB.view && XR_FAILED(r = X.xrCreateReferenceSpace(X.session, &rs, &OB.view))) {
        OB.view = XR_NULL_HANDLE;
        return ob_off("xrCreateReferenceSpace(VIEW): %s", xr_str(r)), -1;
    }
    lg("xr: objective: swapchain %dx%d format %d (%u images), a %u KB staging buffer: the picture is uploaded when the "
       "distance's text changes", SOMVR_OB_IMG_W, SOMVR_OB_IMG_H, f, n, (unsigned)(sz >> 10));
    return 0;
}

/* the picture with text into the staging buffer (the last upload waited first), one copy into the next swapchain
 * image, released. 0 = done, -1 = off */
static int ob_upload(dev_st *ds, const char *text) {
    if (ob_make(ds)) return -1;
    VkResult vr;
    if (OB.pending) {
        if ((vr = ds->WaitForFences(ds->dev, 1, &OB.fence, VK_TRUE, FENCE_TIMEOUT_NS)) != VK_SUCCESS)
            return ob_off("vkWaitForFences (the last upload): %d", vr), -1;
        OB.pending = 0;
    }
    if ((vr = ds->ResetFences(ds->dev, 1, &OB.fence)) != VK_SUCCESS) return ob_off("vkResetFences: %d", vr), -1;
    somvr_ob_image((uint8_t *)OB.map, &OB.font, text, OB.bgra); /* HOST_COHERENT: visible at the submit */
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    XrResult r;
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(OB.sc, &ai, &xi)) || xi >= OB.nimg)
        return ob_off("xrAcquireSwapchainImage (objective): %s, image %u of %u", xr_str(r), xi, OB.nimg), -1;
    if ((r = X.xrWaitSwapchainImage(OB.sc, &wti)) != XR_SUCCESS)
        return ob_off("xrWaitSwapchainImage (objective): %s", xr_str(r)), -1;
    const char *what = "vkResetCommandBuffer";
    if ((vr = ds->ResetCommandBuffer(OB.cmd, 0)) == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(OB.cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, OB.img[xi], 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(OB.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_OB_IMG_W, SOMVR_OB_IMG_H, 1}};
        ds->CmdCopyBufferToImage(OB.cmd, OB.buf, OB.img[xi], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, OB.img[xi], VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(OB.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(OB.cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &OB.cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, OB.fence)) == VK_SUCCESS) OB.pending = 1;
    }
    if (vr != VK_SUCCESS) return ob_off("the picture's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(OB.sc, &ri)))
        return ob_off("xrReleaseSwapchainImage (objective): %s", xr_str(r)), -1;
    OB.released = 1;
    snprintf(OB.text, sizeof OB.text, "%s", text);
    if (++OB.n_uploads <= 3 || OB.n_uploads % 200 == 0)
        lg("xr: objective: picture %llu ('%s') uploaded into image %u", (unsigned long long)OB.n_uploads, text, xi);
    return 0;
}

/* A stereo frame from record `rec`, right before its xrEndFrame: the marker into the frame's composition (XL_OBJ) */
static void ob_layers(dev_st *ds, const somvr_rec *rec, xr_comp *c) {
    OB.has = 0;
    if (!OB.on) return;
    somvr_objective_frame e;
    int have = somvr_objective_get(OB.obj, rec->frame, &e);
    if (!have) { /* overwritten or not written: the newest entry, if it is about this frame */
        uint64_t last = atomic_load_explicit(&OB.obj->last, memory_order_acquire);
        have = last && last + 4 >= rec->frame && rec->frame + 4 >= last && somvr_objective_get(OB.obj, last, &e);
        OB.n_missing++;
    }
    OB.n_frames++;
    somvr_ob_place p;
    memset(&p, 0, sizeof p);
    int land = have && e.n == 1 && (e.flags & SOMVR_OBF_LAND);
    if (land) { /* build 16: the climb's landing (SOMVR_CLIMB_PULLOVER): a ring flat on the floor where he will stand */
        p = somvr_ob_floor(e.pos, e.dist_m);
        OB.in_view = 0;
    } else if (have && e.n == 1 && isfinite(e.pos[0] + e.pos[1] + e.pos[2])) {
        float head[3], fov[4];
        for (int k = 0; k < 3; k++) head[k] = 0.5f * (rec->eye[0].pose.p[k] + rec->eye[1].pose.p[k]);
        for (int k = 0; k < 2; k++) {
            somvr_fovf f = somvr_submit_fov(&rec->eye[k], rec->applied, k); /* exactly what the projection carries */
            fov[0] = k == 0 || f.l < fov[0] ? f.l : fov[0], fov[1] = k == 0 || f.r > fov[1] ? f.r : fov[1];
            fov[2] = k == 0 || f.u > fov[2] ? f.u : fov[2], fov[3] = k == 0 || f.d < fov[3] ? f.d : fov[3];
        }
        p = somvr_ob_locate(&OB.cfg, head, rec->eye[0].pose.q, fov, e.pos, &OB.in_view);
    } else {
        OB.in_view = 0;
    }
    if (p.kind != SOMVR_OBK_NONE) {
        char text[16];
        if (land) snprintf(text, sizeof text, "%s", OB.text); /* the picture as it is: the ring is in every upload */
        else somvr_ob_text(e.dist_m, text);
        if ((!OB.released || strcmp(text, OB.text)) && ob_upload(ds, text)) return; /* off now (logged) */
        if (p.kind == SOMVR_OBK_EDGE && !OB.view) return;
        int32_t rc[4];
        somvr_ob_rect(p.cell, rc);
        OB.q = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, /* premultiplied */
                                        .space = p.kind == SOMVR_OBK_EDGE ? OB.view : X.space,
                                        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                        .subImage = {OB.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                        .pose = {{p.pose[0], p.pose[1], p.pose[2], p.pose[3]}, {p.pose[4], p.pose[5], p.pose[6]}},
                                        .size = {p.size[0], p.size[1]}};
        OB.has = 1;
        OB.n_land += land;
        OB.n_marker += p.kind == SOMVR_OBK_WORLD && !land, OB.n_edge += p.kind == SOMVR_OBK_EDGE;
        OB.n_faint += p.cell == SOMVR_OB_CELL_FAINT;
        if (!land)
            atomic_fetch_add_explicit(p.kind == SOMVR_OBK_WORLD ? &OB.obj->n_marker : &OB.obj->n_edge, 1, memory_order_relaxed);
    } else {
        OB.n_none++;
    }
    OB.last_has = OB.has, OB.last_q = OB.q;
    int kind = p.kind * 4 + (p.kind ? p.cell : 0);
    if (land && kind != OB.kind_was && OB.lines < 400) {
        OB.lines++;
        lg("xr: objective frame %llu: the climb's landing: a ring flat on the floor at %+.2f,%+.2f,%+.2f m, %.2f m across "
           "(frames with it %llu)", (unsigned long long)rec->frame, (double)p.pose[4], (double)p.pose[5], (double)p.pose[6],
           (double)p.size[0], (unsigned long long)OB.n_land);
        OB.kind_was = kind, OB.id_was = 0;
        if (OB.has) c->l[XL_OBJ] = (const XrCompositionLayerBaseHeader *)&OB.q;
        return;
    }
    if (((kind != OB.kind_was || (have && e.n && e.id != OB.id_was)) && OB.lines < 400) || OB.n_frames <= 2 ||
        OB.n_frames % 600 == 0) {
        OB.lines += kind != OB.kind_was;
        lg("xr: objective frame %llu%s: %s%s, the objective %s away (marker %u), %.1f deg off the view's centre (yaw "
           "%+.1f) -> %s at %+.2f,%+.2f,%+.2f m%s, %.2f m square (frames %llu: marker %llu, edge %llu, none %llu, "
           "uploads %llu)",
           (unsigned long long)rec->frame, !have ? " (no entry)" : "",
           have && e.n ? "an objective" : "no objective",
           have && e.n ? (e.flags & SOMVR_OBF_HIDDEN ? " (the game's icon hidden)" : " (the game's icon kept)") : "",
           have && e.n ? OB.text : "-", have ? e.id : 0u, (double)p.off_deg, (double)p.yaw_deg,
           p.kind == SOMVR_OBK_WORLD ? (p.cell == SOMVR_OB_CELL_FAINT ? "the faint marker world-locked" : "the marker world-locked")
           : p.kind == SOMVR_OBK_EDGE ? "the edge arrow head-locked" : "nothing",
           (double)p.pose[4], (double)p.pose[5], (double)p.pose[6],
           p.kind == SOMVR_OBK_EDGE ? " in the view" : "", (double)p.size[0],
           (unsigned long long)OB.n_frames, (unsigned long long)OB.n_marker,
           (unsigned long long)OB.n_edge, (unsigned long long)OB.n_none, (unsigned long long)OB.n_uploads);
        if (p.kind == SOMVR_OBK_EDGE && kind != OB.kind_was)
            lg("xr: objective: the arrow points %.0f deg (0 = right, 90 = up)", (double)p.roll_deg);
    }
    OB.kind_was = kind, OB.id_was = have && e.n ? e.id : 0;
    if (OB.has) c->l[XL_OBJ] = (const XrCompositionLayerBaseHeader *)&OB.q;
}

/* a dumped frame with the marker (mn_dump_layers): its picture as <base>.objectiveN.ppm + .alpha.pgm (premultiplied;
 * scripts/xr_compose.py draws the quad with it) */
static void ob_dump(uint64_t frame) {
    if (!OB.on || !OB.map || !OB.released) return;
    char path[sizeof D.base + 48], apath[sizeof D.base + 48];
    snprintf(path, sizeof path, "%s.objective%llu.ppm", D.base, (unsigned long long)frame);
    snprintf(apath, sizeof apath, "%s.objective%llu.alpha.pgm", D.base, (unsigned long long)frame);
    FILE *f = fopen(path, "we"), *a = fopen(apath, "we");
    int ok = f && a && fprintf(f, "P6\n%d %d\n255\n", SOMVR_OB_IMG_W, SOMVR_OB_IMG_H) > 0 &&
             fprintf(a, "P5\n%d %d\n255\n", SOMVR_OB_IMG_W, SOMVR_OB_IMG_H) > 0;
    const uint8_t *px = (const uint8_t *)OB.map;
    for (int i = 0; ok && i < SOMVR_OB_IMG_W * SOMVR_OB_IMG_H; i++) {
        const uint8_t *s = px + 4 * i;
        uint8_t rgb[3] = {s[OB.bgra ? 2 : 0], s[1], s[OB.bgra ? 0 : 2]};
        ok = fwrite(rgb, 1, 3, f) == 3 && fputc(s[3], a) != EOF;
    }
    if (f) ok &= fclose(f) == 0;
    if (a) ok &= fclose(a) == 0;
    lg("xr: dump objective frame %llu -> %s + .alpha.pgm (%dx%d, premultiplied, '%s')%s", (unsigned long long)frame, path,
       SOMVR_OB_IMG_W, SOMVR_OB_IMG_H, OB.text, ok ? "" : ": write failed");
}

/* a reuse frame (the last stereo frame's image again): its marker again, unchanged, in its place */
static void ob_reuse_layers(xr_comp *c) {
    OB.has = OB.on && OB.sc ? OB.last_has : 0;
    OB.q = OB.last_q;
    if (OB.has) c->l[XL_OBJ] = (const XrCompositionLayerBaseHeader *)&OB.q;
}
