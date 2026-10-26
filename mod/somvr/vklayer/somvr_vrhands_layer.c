/* somvr_vrhands_layer.c - build 10 (docs/re-xr-pipeline.md 33): our own VR hands, the watch on the wrist, the wrist
 * panel off the palm. Included by somvr_vklayer.c after the menus' file (it uses X, cfg, I, VM and MN); the frame code
 * calls in through the vh_* functions declared there. X.mx held, the present thread.
 *   Hands (the preload's active `somvr_vrhands`, ../somvr_vrhands.h: SOMVR_VRHANDS=1; SOMVR_XR_HANDS=off: none): every
 *     stereo frame whose entry says the game hid its own hands is drawn with ours: both hands skinned on the CPU from
 *     the entry's grips and curls (../somvr_vrhands.c), the wrist panel's and the watch's quads as occluders (depth
 *     only: the hand behind them is hidden, the fingers in front of them are not), per eye into a multisampled image
 *     and resolved side by side into an XR swapchain of ours (./somvr_handdraw.c), submitted as a projection layer
 *     with the record's eye poses and fovs right over the VR menu's panel (XL_HANDS), premultiplied alpha. A reuse
 *     frame repeats it. somvr_vrhands.layer_state = DRAWING once the swapchain and the drawing are made (the game
 *     hides its hands from then on), OFF after any failure (the game's hands come back; ours never again this
 *     session). SOMVR_XR_HANDS_MSAA=1|2|4|8 (4), SOMVR_XR_HANDS_SCALE=0.5..1 (1: the eye images' size),
 *     SOMVR_XR_HANDS_LOOK=glove|skin (glove), SOMVR_XR_HANDS_LIGHT=auto|0.2..3 (auto: the light follows the scene's
 *     brightness: eye 0's image blitted to 16x16 every frame, read back when its slot comes round, the mean linear
 *     luminance against the practice meadow's by day, square-rooted, 0.35..1.25, eased over 0.5 s; a number: fixed),
 *     SOMVR_XR_HANDS_CAL=x,y,z,rx,ry,rz (cm, deg in the right grip's frame; the left mirrored).
 *   SOMVR_XR_WATCH=wrist   the watch (SOMVR_XR_MINIMAP=wrist) flat on the back of the off hand's wrist (the hand model's
 *     wrist; SOMVR_XR_WATCH_AT=BACK,GAP cm), reading upright with the wrist raised, shown after 4 facing frames and
 *     hidden after 6 (no one-frame flashes); unset = builds 6-9's (8 cm toward the pinky, 4.5 cm out).
 *   SOMVR_XR_PANEL=palm    the wrist panel's plane off the palm by the hand's reach out of it (any pose) + a gap, its
 *     centre over the hand (SOMVR_XR_PANEL_AT=UP,GAP cm); unset = 15 cm up, 5 cm out (into the curled fingers).
 *   SOMVR_XR_HANDS_DEPTH=world|off (build 11, docs/re-xr-pipeline.md 33.8; default off: ../somvr_vrhands.h)   the world
 *     hides our hands where they go into it: the preload copies each eye's scene depth at that eye's end (somvr_vrhands
 *     v2: two D32S8 textures, the frame each holds, depth_k per entry); each eye samples its own (a second pipeline,
 *     hands.frag's WORLD_DEPTH) and fades the hand out over SOMVR_XR_HANDS_DEPTH_SOFT cm (0.2..5, 1.5) centred
 *     SOMVR_XR_HANDS_DEPTH_BIAS cm behind the surface (-3..5, half the soft width: drawn up to the surface). A frame
 *     without both eyes' depth draws as build 10 (logged when the reason changes). Off = build 10's pipeline alone.
 */
#include "../somvr_vrhands.h"
#include "somvr_handdraw.h"

enum { VH_LOOK_GLOVE, VH_LOOK_SKIN };
/* SOMVR_XR_HANDS_DEPTH_HELD's default: Talion's held items (the sword, the dagger) are drawn into the depth 1/1.17 as far
 * as they are (as if the near plane were 7 cm, not the eyes' 6): runs 010325, 012753 by the two eyes' agreement (the
 * world at 1.00-1.01, his items at 1.16-1.18) and the debug run 011550 (the fist 3-6 cm behind its own hilt). */
#define VH_DEPTH_HELD 1.17f
#define VH_LUM_REF 0.21f /* the practice meadow by day, the sun ahead: eye 0's mean linear luminance 0.20..0.24 (run 183338) */
static struct {
    /* the switches */
    int want, bad;          /* SOMVR_XR_HANDS: 1 on (default), 0 off */
    int msaa, look, cal_bad, misc_bad;
    float scale, light;
    int auto_light;         /* SOMVR_XR_HANDS_LIGHT=auto (default) */
    float lum, lum_light;   /* the scene's mean linear luminance (eased), the light it gives */
    int lum_have;
    double lum_t;
    VkImage lum_img;        /* 16x16 RGBA8 UNORM: eye 0 blitted down */
    VkDeviceMemory lum_mem;
    VkBuffer lum_buf;       /* HD_NSLOT x 16x16x4 bytes, host-coherent */
    VkDeviceMemory lum_bmem;
    uint8_t *lum_map;
    int lum_slot[HD_NSLOT]; /* that slot's submit read the scene */
    somvr_vh_cfg vc;
    int watch, watch_bad;   /* SOMVR_XR_WATCH=wrist */
    somvr_vh_watch_cfg wc;
    somvr_vh_face_cfg fc;
    somvr_vh_face fs;
    int panel, panel_bad;   /* SOMVR_XR_PANEL=palm */
    float panel_up, panel_gap;
    int depth, depth_bad;   /* SOMVR_XR_HANDS_DEPTH: 1 world, 0 off */
    float dsoft, dbias;     /* ..._SOFT, ..._BIAS (m) */
    float dheld;            /* ..._HELD: a hand holding a weapon reads the world's depth as Talion's items' (x this) */
    int ddebug;             /* ..._DEBUG=1: the hands show the depth difference (measures, headless) */
    /* the model (once per process) */
    int model_tried, model_ok;
    somvr_vh_model m;
    /* the session */
    somvr_vrhands_shared *obj;
    int on;                 /* this session draws the hands (the object, the switch): made at the first stereo frame */
    int ready;              /* the swapchain and the drawing are made */
    dev_st *ds;
    hd_vk f;
    hd_ctx hc;
    XrSwapchain sc;
    VkImage img[MAX_XR_IMAGES];
    uint32_t nimg, xi, ew, eh;  /* swapchain images, the acquired one, one eye's size */
    uint32_t src_w, src_h;      /* the eye images' size it was made for */
    int acquired;
    VkFormat fmt;
    VkCommandBuffer cmd[HD_NSLOT];
    VkFence fence[HD_NSLOT];
    int pending[HD_NSLOT];
    uint32_t slot;
    XrCompositionLayerProjection proj;
    XrCompositionLayerProjectionView pv[2];
    int last;               /* the last stereo frame ended with our hands (a reuse repeats them) */
    somvr_vrhands_frame e;  /* the last entry drawn (a frame without one takes its curls) */
    VkBuffer dbuf;          /* SOMVR_XR_DUMP: a dumped frame's hands image (host-visible) */
    VkDeviceMemory dmem;
    void *dmap;
    VkDeviceSize dsize;
    int have_e;
    uint64_t n_frames, n_drawn, n_noentry, n_shown_game, n_occ;
    uint64_t n_ghost;       /* build 13: frames drawn with a ghost (SOMVR_HANDS_SURFACE) */
    int ghost_was;          /* the last frame's ghosts (bit per hand: the on/off lines) */
    uint64_t n_space, n_space_drawn; /* never a void: the loading space's own entries, drawn */
    uint32_t lines, was_hide;
    /* build 11: the world's depth (SOMVR_XR_HANDS_DEPTH=world) */
    hd_vkw wf;
    int world;              /* the world pipeline is made */
    uint32_t dgen;          /* the depth textures' generation the views are for (0 = none yet) */
    VkImage dimg[2];        /* the preload's depth textures (Feral's VkImages) */
    VkImageView dview[2];   /* their depth aspect, sampled */
    VkFormat dfmt;
    VkImageAspectFlags daspect; /* depth (+ stencil): their barriers */
    uint32_t dw, dh;
    const char *dwhy;       /* why the last frame had no world (logged when it changes) */
    uint32_t dlines;
    uint64_t n_dused, n_dnone, n_dnext; /* frames with the world, without, eyes whose depth was the next frame's */
    VkBuffer zbuf;          /* SOMVR_XR_TEST + SOMVR_XR_DUMP: a dumped frame's depth (host-visible, both eyes) */
    VkDeviceMemory zmem;
    void *zmap;
    VkDeviceSize zsize;
    int zdump;              /* this frame's depth was copied into zbuf */
    int zsten;              /* ... and its stencil, after both eyes' depth (each eye padded to 4 bytes) */
    int32_t zbox[2][4];     /* ... and the hands' boxes then */
    float zk;
    /* the GPU time of our submit (timestamps; logs only): with the world's depth and without */
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkCmdResetQueryPool CmdResetQueryPool;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    VkQueryPool qp;
    float ts_ns;            /* ns per timestamp tick */
    int qrec[HD_NSLOT];     /* the slot's submit wrote its pair: 1 plain, 2 with the world */
    double t_sum[2], t_max[2];
    uint64_t t_n[2];
} VH = {.want = 1};

/* xr_session_setup's config (the switches: once a session) */
static void vh_config(void) {
    const char *p = getenv("SOMVR_XR_HANDS");
    VH.want = !p || !*p || !strcmp(p, "on") || !strcmp(p, "1");
    VH.bad = p && *p && !VH.want && strcmp(p, "off") && strcmp(p, "0");
    VH.misc_bad = 0;
    p = getenv("SOMVR_XR_HANDS_MSAA");
    VH.msaa = p && *p ? atoi(p) : 4;
    if (VH.msaa != 1 && VH.msaa != 2 && VH.msaa != 4 && VH.msaa != 8) VH.misc_bad |= p && *p, VH.msaa = 4;
    p = getenv("SOMVR_XR_HANDS_SCALE");
    VH.scale = p && *p ? strtof(p, NULL) : 1.0f;
    if (!(VH.scale >= 0.5f && VH.scale <= 1.0f)) VH.misc_bad |= 2, VH.scale = 1.0f;
    p = getenv("SOMVR_XR_HANDS_LOOK");
    VH.look = p && !strcmp(p, "skin") ? VH_LOOK_SKIN : VH_LOOK_GLOVE;
    if (p && *p && strcmp(p, "skin") && strcmp(p, "glove")) VH.misc_bad |= 4;
    p = getenv("SOMVR_XR_HANDS_LIGHT");
    VH.auto_light = !p || !*p || !strcmp(p, "auto");
    VH.light = VH.auto_light ? 1.0f : strtof(p, NULL);
    if (!(VH.light >= 0.2f && VH.light <= 3.0f)) VH.misc_bad |= 8, VH.light = 1.0f, VH.auto_light = 1;
    somvr_vh_cfg_default(&VH.vc);
    p = getenv("SOMVR_XR_HANDS_CAL");
    VH.cal_bad = p && *p && !somvr_vh_parse_cal(p, &VH.vc);
    p = getenv("SOMVR_XR_WATCH");
    VH.watch = p && !strcmp(p, "wrist");
    VH.watch_bad = p && *p && !VH.watch && strcmp(p, "old");
    somvr_vh_watch_cfg_default(&VH.wc);
    somvr_vh_face_cfg_default(&VH.fc);
    p = getenv("SOMVR_XR_WATCH_AT");
    float a, b;
    if (p && *p) {
        if (sscanf(p, "%f,%f", &a, &b) == 2 && a >= -5 && a <= 15 && b >= 0 && b <= 5)
            VH.wc.back_m = 0.01f * a, VH.wc.gap_m = 0.01f * b;
        else
            VH.watch_bad = 1;
    }
    p = getenv("SOMVR_XR_PANEL");
    VH.panel = p && !strcmp(p, "palm");
    VH.panel_bad = p && *p && !VH.panel && strcmp(p, "old");
    VH.panel_up = 0.04f, VH.panel_gap = 0.03f;
    p = getenv("SOMVR_XR_PANEL_AT");
    if (p && *p) {
        if (sscanf(p, "%f,%f", &a, &b) == 2 && a >= -20 && a <= 30 && b >= 0 && b <= 15)
            VH.panel_up = 0.01f * a, VH.panel_gap = 0.01f * b;
        else
            VH.panel_bad = 1;
    }
    p = getenv("SOMVR_XR_HANDS_DEPTH");
    VH.depth = somvr_vh_depth_word(p);
    VH.depth_bad = VH.depth < 0;
    if (VH.depth < 0) VH.depth = 0;
    p = getenv("SOMVR_XR_HANDS_DEPTH_SOFT");
    VH.dsoft = p && *p ? 0.01f * strtof(p, NULL) : 0.015f;
    if (!(VH.dsoft >= 0.002f && VH.dsoft <= 0.05f)) VH.depth_bad |= 2, VH.dsoft = 0.015f;
    p = getenv("SOMVR_XR_HANDS_DEPTH_BIAS");
    VH.dbias = p && *p ? 0.01f * strtof(p, NULL) : 0.5f * VH.dsoft;
    if (!(VH.dbias >= -0.03f && VH.dbias <= 0.05f)) VH.depth_bad |= 4, VH.dbias = 0.5f * VH.dsoft;
    p = getenv("SOMVR_XR_HANDS_DEPTH_HELD");
    VH.dheld = p && *p ? strtof(p, NULL) : VH_DEPTH_HELD;
    if (!(VH.dheld >= 1.0f && VH.dheld <= 1.5f)) VH.depth_bad |= 8, VH.dheld = VH_DEPTH_HELD;
    p = getenv("SOMVR_XR_HANDS_DEPTH_DEBUG");
    VH.ddebug = p && !strcmp(p, "1");
    if (!VH.model_tried) {
        VH.model_tried = 1;
        VH.model_ok = somvr_vh_init(&VH.m, &VH.vc) == 0;
        lg("xr: hands: the hand model: %s%s", VH.m.why, VH.model_ok ? "" : " (no hands, the old watch and panel)");
    }
}

static void vh_publish(uint32_t state) {
    if (VH.obj) atomic_store_explicit(&VH.obj->layer_state, state, memory_order_release);
}

/* the drawing's GPU objects and the swapchain (waits our submits first) */
static void vh_free(void) {
    dev_st *ds = VH.ds;
    if (ds) {
        for (int i = 0; i < HD_NSLOT; i++) {
            if (VH.fence[i] && VH.pending[i]) ds->WaitForFences(ds->dev, 1, &VH.fence[i], VK_TRUE, XR_IMAGE_TIMEOUT_NS);
            if (VH.fence[i]) ds->DestroyFence(ds->dev, VH.fence[i], NULL);
            if (VH.cmd[i] && ds->pool) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &VH.cmd[i]);
            VH.fence[i] = VK_NULL_HANDLE, VH.cmd[i] = VK_NULL_HANDLE, VH.pending[i] = 0;
        }
        hd_destroy(&VH.hc);
        if (VH.lum_map) ds->UnmapMemory(ds->dev, VH.lum_bmem);
        if (VH.lum_buf) ds->DestroyBuffer(ds->dev, VH.lum_buf, NULL);
        if (VH.lum_bmem) ds->FreeMemory(ds->dev, VH.lum_bmem, NULL);
        if (VH.lum_img) ds->DestroyImage(ds->dev, VH.lum_img, NULL);
        if (VH.lum_mem) ds->FreeMemory(ds->dev, VH.lum_mem, NULL);
        VH.lum_map = NULL, VH.lum_buf = VK_NULL_HANDLE, VH.lum_bmem = VK_NULL_HANDLE, VH.lum_img = VK_NULL_HANDLE;
        VH.lum_mem = VK_NULL_HANDLE;
        memset(VH.lum_slot, 0, sizeof VH.lum_slot);
        if (VH.dmap) ds->UnmapMemory(ds->dev, VH.dmem);
        if (VH.dbuf) ds->DestroyBuffer(ds->dev, VH.dbuf, NULL);
        if (VH.dmem) ds->FreeMemory(ds->dev, VH.dmem, NULL);
        VH.dbuf = VK_NULL_HANDLE, VH.dmem = VK_NULL_HANDLE, VH.dmap = NULL, VH.dsize = 0;
        for (int e = 0; e < 2; e++)
            if (VH.dview[e]) VH.f.DestroyImageView(ds->dev, VH.dview[e], NULL);
        if (VH.zmap) ds->UnmapMemory(ds->dev, VH.zmem);
        if (VH.zbuf) ds->DestroyBuffer(ds->dev, VH.zbuf, NULL);
        if (VH.zmem) ds->FreeMemory(ds->dev, VH.zmem, NULL);
    }
    if (ds && VH.qp && VH.DestroyQueryPool) VH.DestroyQueryPool(ds->dev, VH.qp, NULL);
    VH.qp = VK_NULL_HANDLE, memset(VH.qrec, 0, sizeof VH.qrec);
    memset(VH.dview, 0, sizeof VH.dview), memset(VH.dimg, 0, sizeof VH.dimg);
    VH.dgen = 0, VH.world = 0, VH.dwhy = NULL, VH.zdump = 0;
    VH.zbuf = VK_NULL_HANDLE, VH.zmem = VK_NULL_HANDLE, VH.zmap = NULL, VH.zsize = 0;
    if (VH.sc) {
        if (VH.acquired) {
            XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            X.xrReleaseSwapchainImage(VH.sc, &ri);
        }
        X.xrDestroySwapchain(VH.sc);
    }
    VH.sc = XR_NULL_HANDLE, VH.acquired = 0, VH.nimg = 0, VH.ready = 0, VH.last = 0, VH.src_w = VH.src_h = 0;
}
static void vh_time_log(const char *when);
static void vh_destroy(void) {
    if (VH.on && VH.ready) vh_time_log("the session's end");
    vh_free();
    if (VH.on) vh_publish(SOMVR_VHL_OFF);
    VH.on = 0, VH.ds = NULL;
}
__attribute__((format(printf, 1, 2))) static void vh_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: hands: off for this session: %s (the game's own hands come back; %llu frames drawn)", why,
       (unsigned long long)VH.n_drawn);
    vh_destroy();
}

/* xr_session_setup, after the menus': are there hands for this session? */
static void vh_setup(void) {
    vh_destroy();
    vh_config();
    VH.n_frames = VH.n_drawn = VH.n_noentry = VH.n_shown_game = VH.n_occ = 0, VH.lines = 0, VH.have_e = 0;
    VH.n_ghost = 0, VH.ghost_was = 0;
    VH.n_dused = VH.n_dnone = VH.n_dnext = 0, VH.dlines = 0;
    memset(&VH.fs, 0, sizeof VH.fs);
    if (VH.bad) lg("xr: hands: SOMVR_XR_HANDS=%s is not on or off: on", getenv("SOMVR_XR_HANDS"));
    if (VH.cal_bad) lg("xr: hands: SOMVR_XR_HANDS_CAL=%s is not x,y,z,rx,ry,rz: 0", getenv("SOMVR_XR_HANDS_CAL"));
    if (VH.misc_bad) lg("xr: hands: a switch not understood (bits 0x%x: MSAA 1/2/4/8, SCALE 0.5..1, LOOK glove|skin, "
                        "LIGHT 0.2..3): its default", VH.misc_bad);
    if (VH.watch_bad || VH.panel_bad)
        lg("xr: hands: SOMVR_XR_WATCH(_AT) / SOMVR_XR_PANEL(_AT) not understood (%d / %d): %s, %s", VH.watch_bad,
           VH.panel_bad, VH.watch ? "wrist" : "old", VH.panel ? "palm" : "old");
    if (VH.depth_bad)
        lg("xr: hands: SOMVR_XR_HANDS_DEPTH(_SOFT/_BIAS/_HELD) not understood (bits 0x%x: world|off, 0.2..5 cm, -3..5 "
           "cm, 1..1.5): %s, %.1f cm, %.1f cm, x%.3f", VH.depth_bad, VH.depth ? "world" : "off", 100 * VH.dsoft,
           100 * VH.dbias, VH.dheld);
    if (!VH.model_ok) VH.watch = VH.panel = 0;
    if (VH.watch)
        lg("xr: hands: the watch on the back of the wrist (%.1f cm toward the elbow from the wrist joint, %.1f cm above "
           "the skin; shown after %d frames within %.0f deg, hidden after %d beyond %.0f deg)", 100 * VH.wc.back_m,
           100 * VH.wc.gap_m, VH.fc.hold_on, acosf(VH.fc.face_on) * 57.29578f, VH.fc.hold_off,
           acosf(VH.fc.face_off) * 57.29578f);
    if (VH.panel)
        lg("xr: hands: the wrist panel %.1f cm off the palm's grip (the hand reaches %.1f cm out of it, + %.1f cm), its "
           "centre %.1f cm up", 100 * (VH.m.palm_reach_m + VH.panel_gap), 100 * VH.m.palm_reach_m, 100 * VH.panel_gap,
           100 * VH.panel_up);
    somvr_vrhands_shared *o = (somvr_vrhands_shared *)dlsym(RTLD_DEFAULT, SOMVR_VRHANDS_SYMBOL);
    const char *no = !X.sh ? "no stereo path" : !o ? "no somvr_vrhands: a preload without it"
                     : !somvr_vrhands_ok(o) ? "somvr_vrhands inactive (SOMVR_VRHANDS unset) or another version"
                     : !VH.want ? "SOMVR_XR_HANDS=off" : !VH.model_ok ? "the hand model is unusable" : NULL;
    if (no) {
        if (o && somvr_vrhands_ok(o)) atomic_store_explicit(&o->layer_state, SOMVR_VHL_OFF, memory_order_release);
        lg("xr: hands: the game's own hands (%s)", no);
        return;
    }
    VH.obj = o, VH.on = 1;
    vh_publish(SOMVR_VHL_UNKNOWN);
    lg("xr: hands: somvr_vrhands found at %p (v%u, preload pid %d): our hands (the MIT WebXR generic hand, %s, %dx MSAA, "
       "%.0f%% of the eye size, light x%.2f, cal %g,%g,%g cm %g,%g,%g deg) as a projection layer over the wrist panel, "
       "made at the first stereo frame", (void *)o, o->version, o->pid, VH.look == VH_LOOK_SKIN ? "skin" : "a glove",
       VH.msaa, 100 * VH.scale, VH.light, VH.vc.cal[0], VH.vc.cal[1], VH.vc.cal[2], VH.vc.cal[3], VH.vc.cal[4],
       VH.vc.cal[5]);
    if (VH.depth)
        lg("xr: hands: SOMVR_XR_HANDS_DEPTH=world: the world hides them (each eye's game depth; the edge %.1f cm wide, "
           "its centre %.1f cm behind the surface; a hand holding a weapon reads it x%.3f)%s", 100 * VH.dsoft,
           100 * VH.dbias, VH.dheld, VH.ddebug ? "; DEBUG: the hands show the depth difference" : "");
    else
        lg("xr: hands: SOMVR_XR_HANDS_DEPTH=off: the world does not hide them (build 10)");
}

/* vm_setup: the panel's place (SOMVR_XR_PANEL=palm): out of the palm past the hand's reach, centred over the hand */
static void vh_panel_cfg(somvr_vm_gcfg *g) {
    if (!VH.panel) return;
    g->out_m = VH.m.palm_reach_m + VH.panel_gap;
    g->up_m = VH.panel_up;
}

/* the watch's face on hand h's grip (SOMVR_XR_WATCH=wrist); 0 = none */
static int vh_watch_pose(int h, const somvr_posef *grip, somvr_posef *face) {
    return somvr_vh_watch_pose(&VH.m, &VH.wc, grip, h, face);
}
/* ... shown this frame (the debounced facing); was = 0 after a frame without a pose */
static int vh_watch_facing(const somvr_posef *face, const float eye[3]) {
    return somvr_vh_face_step(&VH.fs, &VH.fc, face, eye);
}
static void vh_watch_reset(void) { memset(&VH.fs, 0, sizeof VH.fs); }
static int vh_watch_on(void) { return VH.watch; }

/* touch with our hands drawn: the fingertip is the drawn pointing index's tip on the grip (not the aim + 5 cm) */
static int vh_touch_tip(int h, float out[3]) {
    const somvr_hand_in *hp = &I.st.hand[h];
    if (!VH.on || !VH.ready || !(hp->active & SOMVR_HACT_POSE) || (hp->grip_flags & 3u) != 3u) return 0;
    somvr_vh_tip(&VH.m, &hp->grip, h, out);
    return 1;
}
/* SOMVR_XR_TEST's touch lines with our hands: the grip that puts the drawn tip D in front of the quad's point U, V,
 * the index into it, the thumb up the quad (the aim the same pose) */
static int vh_touch_pose(int h, const somvr_posef *quad, const float size[2], float u, float v, float front_m,
                         somvr_posef *out) {
    if (!VH.on || !VH.ready) return 0;
    static const float ex[3] = {0, 0, -1}, ey[3] = {0, 1, 0};
    float lp[3] = {(u - 0.5f) * size[0], (0.5f - v) * size[1], front_m}, w[3], dir[3], up[3];
    mn_qrot(quad->q, lp, w), mn_qrot(quad->q, ex, dir), mn_qrot(quad->q, ey, up);
    for (int i = 0; i < 3; i++) w[i] += quad->p[i];
    return somvr_vh_touch_grip(&VH.m, w, dir, up, h, out);
}

/* SOMVR_XR_HANDS_LIGHT=auto: the 16x16 image eye 0 is blitted into and the slots' readback buffer; 0 = made */
#define VH_LUM 16
static int vh_lum_make(dev_st *ds) {
    if (!ds->CmdBlitImage || !ds->CmdCopyImageToBuffer) return -1;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {VH_LUM, VH_LUM, 1}, .mipLevels = 1,
                            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkPhysicalDeviceMemoryProperties mp;
    ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
    VkMemoryRequirements mr;
    if (ds->CreateImage(ds->dev, &ci, NULL, &VH.lum_img) != VK_SUCCESS) return VH.lum_img = VK_NULL_HANDLE, -1;
    VH.f.GetImageMemoryRequirements(ds->dev, VH.lum_img, &mr);
    int t = -1;
    for (uint32_t i = 0; i < mp.memoryTypeCount && t < 0; i++)
        if (mr.memoryTypeBits >> i & 1u) t = (int)i;
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = (uint32_t)t};
    if (t < 0 || ds->AllocateMemory(ds->dev, &ai, NULL, &VH.lum_mem) != VK_SUCCESS) return VH.lum_mem = VK_NULL_HANDLE, -1;
    if (VH.f.BindImageMemory(ds->dev, VH.lum_img, VH.lum_mem, 0) != VK_SUCCESS) return -1;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = HD_NSLOT * VH_LUM * VH_LUM * 4,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (ds->CreateBuffer(ds->dev, &bci, NULL, &VH.lum_buf) != VK_SUCCESS) return VH.lum_buf = VK_NULL_HANDLE, -1;
    ds->GetBufferMemoryRequirements(ds->dev, VH.lum_buf, &mr);
    t = somvr_pick_host_memory(&mp, mr.memoryTypeBits);
    ai.allocationSize = mr.size, ai.memoryTypeIndex = (uint32_t)t;
    if (t < 0 || ds->AllocateMemory(ds->dev, &ai, NULL, &VH.lum_bmem) != VK_SUCCESS) return VH.lum_bmem = VK_NULL_HANDLE, -1;
    void *m = NULL;
    if (ds->BindBufferMemory(ds->dev, VH.lum_buf, VH.lum_bmem, 0) != VK_SUCCESS ||
        ds->MapMemory(ds->dev, VH.lum_bmem, 0, VK_WHOLE_SIZE, 0, &m) != VK_SUCCESS)
        return -1;
    VH.lum_map = m;
    return 0;
}
/* the slot's readback (its fence waited): the scene's mean linear luminance -> the eased light */
static void vh_lum_read(uint32_t s, double t) {
    if (!VH.lum_slot[s]) return;
    VH.lum_slot[s] = 0;
    const uint8_t *p = VH.lum_map + (size_t)s * VH_LUM * VH_LUM * 4;
    double sum = 0;
    for (int i = 0; i < VH_LUM * VH_LUM; i++) {
        float c[3];
        for (int k = 0; k < 3; k++) {
            float v = (float)p[4 * i + k] / 255.0f; /* the eye images hold sRGB-encoded bytes (P6) */
            c[k] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
        }
        sum += 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    }
    float y = (float)(sum / (VH_LUM * VH_LUM));
    if (!VH.lum_have) VH.lum = y, VH.lum_have = 1;
    else {
        float dt = t > VH.lum_t ? (float)(t - VH.lum_t) : 0, k = 1 - expf(-dt / 0.5f);
        VH.lum += k * (y - VH.lum);
    }
    VH.lum_t = t;
    float l = sqrtf(VH.lum / VH_LUM_REF);
    VH.lum_light = l < 0.35f ? 0.35f : l > 1.25f ? 1.25f : l;
}
/* the frame's commands: eye 0 (in TRANSFER_DST, as the eyes' submit leaves it) blitted down, read back, returned */
static void vh_lum_cmds(dev_st *ds, VkCommandBuffer cmd, uint32_t s, const eyes_st *ey) {
    if (!VH.auto_light || !VH.lum_map || !ey || !ey->img[0]) return;
    VkImageMemoryBarrier b[2];
    img_barrier(&b[0], ey->img[0], VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    img_barrier(&b[1], VH.lum_img, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, b);
    VkImageBlit bl = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .srcOffsets = {{0, 0, 0}, {(int32_t)ey->w, (int32_t)ey->h, 1}},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .dstOffsets = {{0, 0, 0}, {VH_LUM, VH_LUM, 1}}};
    ds->CmdBlitImage(cmd, ey->img[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VH.lum_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1, &bl, VK_FILTER_LINEAR);
    img_barrier(&b[0], ey->img[0], VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    img_barrier(&b[1], VH.lum_img, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, b);
    VkBufferImageCopy bc = {.bufferOffset = (VkDeviceSize)s * VH_LUM * VH_LUM * 4,
                            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {VH_LUM, VH_LUM, 1}};
    ds->CmdCopyImageToBuffer(cmd, VH.lum_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VH.lum_buf, 1, &bc);
    VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
    VH.lum_slot[s] = 1;
}

static PFN_vkVoidFunction vh_get(void *ud, const char *n) {
    dev_st *ds = ud;
    return ds->gdpa(ds->dev, n);
}

/* the swapchain (side by side, the eye size x SCALE), the command buffers, the drawing; 0 = ready */
static int vh_make(dev_st *ds, uint32_t src_w, uint32_t src_h) {
    vh_free();
    VH.ds = ds;
    const char *miss = hd_vk_load(&VH.f, vh_get, ds);
    if (miss) return vh_off("the device has no %s", miss), -1;
    if (!ds->is || !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool) return vh_off("no memory properties or command pool"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return vh_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    VkFormat f = VK_FORMAT_UNDEFINED;
    static const VkFormat pref[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
    for (int k = 0; k < 2 && !f; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == (int64_t)pref[k]) f = pref[k];
    if (!f) return vh_off("the runtime offers no 8-bit sRGB swapchain format"), -1;
    uint32_t ew = (uint32_t)((float)src_w * VH.scale + 0.5f), eh = (uint32_t)((float)src_h * VH.scale + 0.5f);
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                                              XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT, /* (the dump) */
                                .format = f, .sampleCount = 1, .width = 2 * ew, .height = eh, .faceCount = 1,
                                .arraySize = 1, .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &VH.sc))) {
        VH.sc = XR_NULL_HANDLE;
        return vh_off("xrCreateSwapchain(%ux%u format %d): %s", 2 * ew, eh, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(VH.sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return vh_off("xrEnumerateSwapchainImages (hands): %s, %u images", xr_str(r), n), -1;
    for (uint32_t i = 0; i < n; i++) VH.img[i] = im[i].image;
    VH.nimg = n, VH.ew = ew, VH.eh = eh, VH.fmt = f, VH.src_w = src_w, VH.src_h = src_h;
    for (int i = 0; i < HD_NSLOT; i++) {
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ds->pool,
                                          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkResult vr = ds->AllocateCommandBuffers(ds->dev, &ai, &VH.cmd[i]);
        if (vr != VK_SUCCESS) VH.cmd[i] = VK_NULL_HANDLE;
        if (vr == VK_SUCCESS) { /* dispatchable object made by a layer: the loader's dispatch pointer */
            if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, VH.cmd[i]);
            else *(void **)VH.cmd[i] = dkey(ds->dev);
        }
        if (vr == VK_SUCCESS && (vr = ds->CreateFence(ds->dev, &fci, NULL, &VH.fence[i])) != VK_SUCCESS)
            VH.fence[i] = VK_NULL_HANDLE;
        if (vr != VK_SUCCESS) return vh_off("command buffer / fence %d: %d", i, vr), -1;
    }
    VkPhysicalDeviceMemoryProperties mp;
    ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
    if (hd_create(&VH.hc, ds->dev, &VH.f, &mp, f, ew, eh, (uint32_t)VH.msaa, somvr_vh_indices(&VH.m, 0),
                  somvr_vh_indices(&VH.m, 1), (uint32_t)somvr_vh_nindices(), (uint32_t)somvr_vh_nverts()))
        return vh_off("the drawing: %s", VH.hc.why), -1;
    if (VH.depth) { /* build 11: the world's depth (a failure keeps the hands, without the world) */
        const char *wm = hd_vkw_load(&VH.wf, vh_get, ds);
        if (wm)
            lg("xr: hands: the world's depth: off (the device has no %s)", wm);
        else if (hd_world(&VH.hc, &VH.wf, VH.dsoft, VH.dbias, (float)src_w / (float)ew, VH.ddebug))
            lg("xr: hands: the world's depth: off (%s)", VH.hc.why);
        else
            VH.world = 1;
    }
    { /* the GPU time of our submits (timestamps; none = no times in the log) */
        VkPhysicalDeviceProperties pp;
        ds->is->GetPhysicalDeviceProperties(ds->pd, &pp);
        VH.ts_ns = pp.limits.timestampPeriod;
        VH.CreateQueryPool = (PFN_vkCreateQueryPool)ds->gdpa(ds->dev, "vkCreateQueryPool");
        VH.DestroyQueryPool = (PFN_vkDestroyQueryPool)ds->gdpa(ds->dev, "vkDestroyQueryPool");
        VH.CmdResetQueryPool = (PFN_vkCmdResetQueryPool)ds->gdpa(ds->dev, "vkCmdResetQueryPool");
        VH.CmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)ds->gdpa(ds->dev, "vkCmdWriteTimestamp");
        VH.GetQueryPoolResults = (PFN_vkGetQueryPoolResults)ds->gdpa(ds->dev, "vkGetQueryPoolResults");
        VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                    .queryCount = 2 * HD_NSLOT};
        if (!(VH.ts_ns > 0) || !VH.CreateQueryPool || !VH.DestroyQueryPool || !VH.CmdResetQueryPool ||
            !VH.CmdWriteTimestamp || !VH.GetQueryPoolResults || VH.CreateQueryPool(ds->dev, &qi, NULL, &VH.qp) != VK_SUCCESS)
            VH.qp = VK_NULL_HANDLE;
        memset(VH.t_sum, 0, sizeof VH.t_sum), memset(VH.t_max, 0, sizeof VH.t_max), memset(VH.t_n, 0, sizeof VH.t_n);
    }
    if (VH.auto_light && vh_lum_make(ds)) { /* the scene's brightness: without it the fixed light */
        lg("xr: hands: no scene brightness (its image or buffer): the fixed light");
        VH.auto_light = 0;
    }
    VH.ready = 1;
    vh_publish(SOMVR_VHL_DRAWING);
    lg("xr: hands: ready: a %ux%u swapchain format %d (%u images) for %ux%u eye images; %s%s", 2 * ew, eh, f, VH.nimg,
       src_w, src_h, VH.hc.why, VH.world ? "; the world's depth sampled per eye" : "");
    return 0;
}

/* an occluder from a composition quad in our space (the wrist panel, the card, the watch); 1 = added */
static int vh_occ(const XrCompositionLayerBaseHeader *l, hd_vert *v) {
    if (!l || l->type != XR_TYPE_COMPOSITION_LAYER_QUAD) return 0;
    const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)(const void *)l;
    if (q->space != X.space || q->size.width < 0.04f || q->size.height < 0.04f) return 0; /* not the ray or the dot */
    static const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0};
    float qq[4] = {q->pose.orientation.x, q->pose.orientation.y, q->pose.orientation.z, q->pose.orientation.w};
    float o[3] = {q->pose.position.x, q->pose.position.y, q->pose.position.z}, hx[3], hy[3];
    mn_qrot(qq, ex, hx), mn_qrot(qq, ey, hy);
    for (int i = 0; i < 3; i++) hx[i] *= 0.5f * q->size.width, hy[i] *= 0.5f * q->size.height;
    hd_quad(v, o, hx, hy);
    return 1;
}

/* SOMVR_XR_DUMP: the readback buffer for a dumped frame's hands (made at the first dump); 0 = ready */
static int vh_dump_buffer(dev_st *ds, VkDeviceSize need) {
    if (VH.dbuf && VH.dsize >= need) return 0;
    if (VH.dbuf || !ds->CmdCopyImageToBuffer) return -1; /* (one size a swapchain: a remake frees it) */
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = need,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkMemoryRequirements mr;
    VkPhysicalDeviceMemoryProperties mp;
    if (ds->CreateBuffer(ds->dev, &bci, NULL, &VH.dbuf) != VK_SUCCESS) return VH.dbuf = VK_NULL_HANDLE, -1;
    ds->GetBufferMemoryRequirements(ds->dev, VH.dbuf, &mr);
    ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
    int type = somvr_pick_host_memory(&mp, mr.memoryTypeBits);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = (uint32_t)type};
    if (type < 0 || ds->AllocateMemory(ds->dev, &ai, NULL, &VH.dmem) != VK_SUCCESS) return VH.dmem = VK_NULL_HANDLE, -1;
    if (ds->BindBufferMemory(ds->dev, VH.dbuf, VH.dmem, 0) != VK_SUCCESS ||
        ds->MapMemory(ds->dev, VH.dmem, 0, VK_WHOLE_SIZE, 0, &VH.dmap) != VK_SUCCESS)
        return VH.dmap = NULL, -1;
    VH.dsize = need;
    return 0;
}
/* ... its fence waited: <base>.handsN.ppm (premultiplied RGB as stored) + .alpha.pgm, and the layers line */
static void vh_dump_write(uint64_t n) {
    int srgb = 0, bgra = somvr_dump_order(VH.fmt, &srgb);
    uint32_t w = 2 * VH.ew, h = VH.eh;
    char path[sizeof D.base + 48], apath[sizeof D.base + 48];
    snprintf(path, sizeof path, "%s.hands%llu.ppm", D.base, (unsigned long long)n);
    snprintf(apath, sizeof apath, "%s.hands%llu.alpha.pgm", D.base, (unsigned long long)n);
    FILE *f = fopen(path, "we"), *a = fopen(apath, "we");
    uint8_t *row = f && a ? malloc((size_t)w * 3) : NULL, *arow = row ? malloc(w) : NULL;
    int ok = bgra >= 0 && arow && fprintf(f, "P6\n%u %u\n255\n", w, h) > 0 && fprintf(a, "P5\n%u %u\n255\n", w, h) > 0;
    for (uint32_t y = 0; ok && y < h; y++) {
        const uint8_t *s = (const uint8_t *)VH.dmap + (size_t)y * w * 4;
        somvr_dump_row(s, row, w, bgra);
        for (uint32_t x = 0; x < w; x++) arow[x] = s[4 * x + 3];
        ok = fwrite(row, 3, w, f) == w && fwrite(arow, 1, w, a) == w;
    }
    free(row), free(arow);
    if (f) ok &= fclose(f) == 0;
    if (a) ok &= fclose(a) == 0;
    lg("xr: dump layers frame %llu: hands %ux%u per eye slot %d (premultiplied, the eyes' poses and fovs) -> %s%s",
       (unsigned long long)n, VH.ew, VH.eh, XL_HANDS, path, ok ? "" : ": write failed");
}

/* the slot's GPU time (its fence waited) into the sums: [0] plain, [1] with the world's depth */
static void vh_time_read(dev_st *ds, uint32_t s) {
    if (!VH.qp || !VH.qrec[s]) return;
    int k = VH.qrec[s] - 1;
    VH.qrec[s] = 0;
    uint64_t t[2];
    if (VH.GetQueryPoolResults(ds->dev, VH.qp, 2 * s, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS || t[1] < t[0])
        return;
    double ms = (double)(t[1] - t[0]) * VH.ts_ns * 1e-6;
    VH.t_sum[k] += ms, VH.t_n[k]++;
    if (ms > VH.t_max[k]) VH.t_max[k] = ms;
}
static void vh_time_log(const char *when) {
    if (!VH.qp || !(VH.t_n[0] + VH.t_n[1])) return;
    lg("xr: hands: GPU time of our submit (%s): with the world's depth %.3f ms mean, %.3f max (%llu frames); without "
       "%.3f ms mean, %.3f max (%llu frames)", when, VH.t_n[1] ? VH.t_sum[1] / (double)VH.t_n[1] : 0.0, VH.t_max[1],
       (unsigned long long)VH.t_n[1], VH.t_n[0] ? VH.t_sum[0] / (double)VH.t_n[0] : 0.0, VH.t_max[0],
       (unsigned long long)VH.t_n[0]);
}

/* ---- build 11: the world's depth (SOMVR_XR_HANDS_DEPTH=world) ------------------------------------------------------ */
static VkImageAspectFlags vh_depth_aspects(VkFormat f) {
    switch (f) {
    case VK_FORMAT_D16_UNORM: case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_D32_SFLOAT: return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT: case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default: return 0;
    }
}
/* the sampled views on the preload's depth textures of generation in->gen (our submits waited first: the old views
 * die); 0 = ready, else VH.dwhy says why (tried once a generation) */
static int vh_depth_views(dev_st *ds, const somvr_eyes_info *in) {
    static char why[200];
    for (int i = 0; i < HD_NSLOT; i++)
        if (VH.pending[i]) {
            if (ds->WaitForFences(ds->dev, 1, &VH.fence[i], VK_TRUE, XR_IMAGE_TIMEOUT_NS) != VK_SUCCESS)
                return VH.dwhy = "our submit did not finish (the old views kept)", -1;
            VH.pending[i] = 0;
        }
    for (int e = 0; e < 2; e++) {
        if (VH.dview[e]) VH.f.DestroyImageView(ds->dev, VH.dview[e], NULL);
        VH.dview[e] = VK_NULL_HANDLE, VH.dimg[e] = VK_NULL_HANDLE;
    }
    VH.dgen = in->gen;
    VkFormat fmt[2] = {VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
    VkImageUsageFlags use[2] = {0, 0};
    pthread_mutex_lock(&g_cand_mx);
    for (int e = 0; e < 2; e++)
        for (int i = 0; i < MAX_EYE_CAND; i++)
            if (in->image[e] && g_cand[i].img == (VkImage)(uintptr_t)in->image[e] && g_cand[i].dev == ds->dev &&
                g_cand[i].w == in->w && g_cand[i].h == in->h) {
                fmt[e] = g_cand[i].fmt, use[e] = g_cand[i].usage, g_cand[i].used = 1;
                VH.dimg[e] = g_cand[i].img;
            }
    pthread_mutex_unlock(&g_cand_mx);
    VkImageAspectFlags asp = vh_depth_aspects(fmt[0]);
    why[0] = 0;
    if (!VH.dimg[0] || !VH.dimg[1])
        snprintf(why, sizeof why, "gen %u: the textures %#llx, %#llx were not created by this device at %ux%u", in->gen,
                 (unsigned long long)in->image[0], (unsigned long long)in->image[1], in->w, in->h);
    else if (!asp || fmt[1] != fmt[0])
        snprintf(why, sizeof why, "gen %u: formats %d, %d are not one depth format", in->gen, fmt[0], fmt[1]);
    else if (!(use[0] & use[1] & VK_IMAGE_USAGE_SAMPLED_BIT))
        snprintf(why, sizeof why, "gen %u: usage 0x%x, 0x%x without SAMPLED", in->gen, use[0], use[1]);
    for (int e = 0; e < 2 && !why[0]; e++) {
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = VH.dimg[e],
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt[0],
                                    .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
        VkResult r = VH.f.CreateImageView(ds->dev, &vi, NULL, &VH.dview[e]);
        if (r != VK_SUCCESS) VH.dview[e] = VK_NULL_HANDLE, snprintf(why, sizeof why, "gen %u: its view %d: %d", in->gen, e, r);
    }
    if (why[0]) {
        for (int e = 0; e < 2; e++)
            if (VH.dview[e]) VH.f.DestroyImageView(ds->dev, VH.dview[e], NULL), VH.dview[e] = VK_NULL_HANDLE;
        VH.dimg[0] = VH.dimg[1] = VK_NULL_HANDLE;
        lg("xr: hands: the world's depth: %s: the hands are not hidden by the world", why);
        return VH.dwhy = why, -1;
    }
    VH.dfmt = fmt[0], VH.daspect = asp, VH.dw = in->w, VH.dh = in->h;
    lg("xr: hands: the world's depth: gen %u: %ux%u format %d usage 0x%x (DXGI %u), views %#llx, %#llx made", in->gen,
       in->w, in->h, fmt[0], use[0], in->dxgi_format, (unsigned long long)(uintptr_t)VH.dimg[0],
       (unsigned long long)(uintptr_t)VH.dimg[1]);
    return 0;
}
/* both depth textures hold this frame's depth, or the next frame's: a present handled after the render thread already
 * ended the next frame's first eye (run 022715's first 22 s: then the GPU has not run that copy yet, or our submit sees
 * a depth one frame newer; never older) */
static int vh_depth_holds(uint64_t frame) {
    for (int e = 0; e < 2; e++) {
        uint64_t d = atomic_load_explicit(&VH.obj->dframe[e], memory_order_acquire);
        if (d < frame || d > frame + 1) return 0;
        VH.n_dnext += d == frame + 1;
    }
    return 1;
}
/* this frame's world: both eyes' depth copied for it -> fr->depth_k, fr->world[]; 1 = used (the images to move) */
static int vh_depth_frame(dev_st *ds, const somvr_rec *rec, const somvr_vrhands_frame *e, hd_frame *fr) {
    if (!VH.world) return 0;
    const char *why = NULL;
    somvr_eyes_info in;
    if (!(e->depth_k > 0))
        why = "the entry has no depth scale (the preload's SOMVR_XR_HANDS_DEPTH is off, or an older preload)";
    else if (!somvr_vrhands_depth_read(VH.obj, &in) || !in.gen || !in.image[0] || !in.image[1])
        why = "the preload has no depth textures yet";
    else if (!vh_depth_holds(rec->frame))
        why = "the depth textures do not hold this frame";
    else if (in.gen != VH.dgen && vh_depth_views(ds, &in))
        why = VH.dwhy;
    else if (!VH.dview[0] || !VH.dview[1])
        why = VH.dwhy ? VH.dwhy : "no views";
    if (why) {
        VH.n_dnone++;
        if ((!VH.dwhy || strcmp(why, VH.dwhy)) && VH.dlines++ < 40)
            lg("xr: hands: frame %llu: the world does not hide them: %s", (unsigned long long)rec->frame, why);
        VH.dwhy = why;
        return 0;
    }
    if (VH.dwhy && VH.dlines++ < 40)
        lg("xr: hands: frame %llu: the world hides them (depth gen %u, depth_k %.4f m: 1 m = depth %.4f)",
           (unsigned long long)rec->frame, VH.dgen, (double)e->depth_k, (double)e->depth_k);
    VH.dwhy = NULL;
    fr->depth_k = e->depth_k;
    fr->world[0] = VH.dview[0], fr->world[1] = VH.dview[1];
    VH.n_dused++;
    atomic_fetch_add_explicit(&VH.obj->n_dused, 1, memory_order_relaxed);
    return 1;
}
/* the depth textures between the preload's layout (TRANSFER_DST: the copy left them so) and ours */
static void vh_depth_barrier(dev_st *ds, VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b[2];
    VkAccessFlags acc[2];
    VkPipelineStageFlags st[2];
    VkImageLayout l[2] = {from, to};
    for (int k = 0; k < 2; k++)
        acc[k] = l[k] == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_ACCESS_TRANSFER_WRITE_BIT
                 : l[k] == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT,
        st[k] = l[k] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                                  : VK_PIPELINE_STAGE_TRANSFER_BIT;
    for (int e = 0; e < 2; e++) {
        img_barrier(&b[e], VH.dimg[e], acc[0], acc[1], from, to);
        b[e].subresourceRange.aspectMask = VH.daspect;
    }
    /* coming from the preload's copy: everything before (its queue's earlier work) */
    ds->CmdPipelineBarrier(cmd, from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : st[0],
                           st[1], 0, 0, NULL, 0, NULL, 2, b);
}
/* SOMVR_XR_TEST + SOMVR_XR_DUMP: the depth readback buffer (made at the first dump); 0 = ready */
static int vh_zbuf(dev_st *ds, VkDeviceSize need) {
    if (VH.zbuf && VH.zsize >= need) return 0;
    if (VH.zbuf) return -1;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = need,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkMemoryRequirements mr;
    VkPhysicalDeviceMemoryProperties mp;
    if (ds->CreateBuffer(ds->dev, &bci, NULL, &VH.zbuf) != VK_SUCCESS) return VH.zbuf = VK_NULL_HANDLE, -1;
    ds->GetBufferMemoryRequirements(ds->dev, VH.zbuf, &mr);
    ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
    int type = somvr_pick_host_memory(&mp, mr.memoryTypeBits);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = (uint32_t)type};
    if (type < 0 || ds->AllocateMemory(ds->dev, &ai, NULL, &VH.zmem) != VK_SUCCESS) return VH.zmem = VK_NULL_HANDLE, -1;
    if (ds->BindBufferMemory(ds->dev, VH.zbuf, VH.zmem, 0) != VK_SUCCESS ||
        ds->MapMemory(ds->dev, VH.zmem, 0, VK_WHOLE_SIZE, 0, &VH.zmap) != VK_SUCCESS)
        return VH.zmap = NULL, -1;
    VH.zsize = need;
    return 0;
}
/* ... its fence waited: <base>.worldN.pgm (both eyes side by side, 16 bits: the world's view distance in mm, 0 = the sky
 * or past 65.5 m) and the distances where the hands are */
static void vh_zdump_write(uint64_t n) {
    VH.zdump = 0;
    uint32_t w = VH.dw, h = VH.dh;
    char path[sizeof D.base + 48];
    snprintf(path, sizeof path, "%s.world%llu.pgm", D.base, (unsigned long long)n);
    FILE *f = fopen(path, "we");
    uint8_t *row = f ? malloc((size_t)4 * w) : NULL;
    int ok = row && fprintf(f, "P5\n%u %u\n65535\n", 2 * w, h) > 0;
    const float *z = VH.zmap;
    for (uint32_t y = 0; ok && y < h; y++) {
        for (int e = 0; e < 2; e++)
            for (uint32_t x = 0; x < w; x++) {
                float d = z[((size_t)e * h + y) * w + x], mm = d > 0 ? 1000.0f * VH.zk / d : 0;
                uint32_t v = mm > 0 && mm < 65535.0f ? (uint32_t)(mm + 0.5f) : 0;
                row[2 * ((size_t)e * w + x)] = (uint8_t)(v >> 8), row[2 * ((size_t)e * w + x) + 1] = (uint8_t)v;
            }
        ok = fwrite(row, 2, 2 * w, f) == 2 * w;
    }
    free(row);
    if (f) ok &= fclose(f) == 0;
    char at[2][120];
    for (int e = 0; e < 2; e++) { /* the hands' box (our pixels -> the depth's): its centre, its nearest world */
        const int32_t *b = VH.zbox[e];
        float sx = (float)w / (float)VH.ew, sy = (float)h / (float)VH.eh, near = 1e9f;
        if (b[2] <= b[0]) {
            snprintf(at[e], sizeof at[e], "no hands");
            continue;
        }
        for (int32_t y = b[1]; y < b[3]; y += 2)
            for (int32_t x = b[0]; x < b[2]; x += 2) {
                uint32_t px = (uint32_t)((float)x * sx), py = (uint32_t)((float)y * sy);
                float d = px < w && py < h ? z[((size_t)e * h + py) * w + px] : 0;
                if (d > 0 && VH.zk / d < near) near = VH.zk / d;
            }
        uint32_t cx = (uint32_t)((float)(b[0] + b[2]) * 0.5f * sx), cy = (uint32_t)((float)(b[1] + b[3]) * 0.5f * sy);
        float dc = cx < w && cy < h ? z[((size_t)e * h + cy) * w + cx] : 0;
        snprintf(at[e], sizeof at[e], "box %d,%d..%d,%d: centre %.3f m, nearest %.3f m", b[0], b[1], b[2], b[3],
                 dc > 0 ? (double)(VH.zk / dc) : -1.0, near < 1e8f ? (double)near : -1.0);
    }
    if (VH.zsten) { /* <base>.worldstencilN.pgm: the stencil, both eyes side by side */
        char sp[sizeof D.base + 48];
        snprintf(sp, sizeof sp, "%s.worldstencil%llu.pgm", D.base, (unsigned long long)n);
        FILE *g = fopen(sp, "we");
        size_t zs = ((size_t)w * h + 3) & ~(size_t)3;
        const uint8_t *st = (const uint8_t *)VH.zmap + (size_t)2 * w * h * 4;
        int sok = g && fprintf(g, "P5\n%u %u\n255\n", 2 * w, h) > 0;
        for (uint32_t y = 0; sok && y < h; y++)
            for (int e = 0; e < 2 && sok; e++) sok = fwrite(st + (size_t)e * zs + (size_t)y * w, 1, w, g) == w;
        if (g) sok &= fclose(g) == 0;
        if (!sok) lg("xr: dump layers frame %llu: the stencil's file failed", (unsigned long long)n);
    }
    lg("xr: dump layers frame %llu: the world's depth (depth_k %.4f m) -> %s%s; eye 0 %s; eye 1 %s", (unsigned long long)n,
       (double)VH.zk, path, ok ? "" : ": write failed", at[0], at[1]);
}

/* the stereo frame's hands (after vm_frame and the HUD's quads are in c, before the comfort and the fade): the entry
 * of rec's frame, drawn and submitted, the layer into c->l[XL_HANDS] */
/* own: never a void's loading space (./somvr_void_layer.c, vd_hands): an entry the layer made from its own input (a
 * mono or keep-alive frame: the game publishes none there), drawn at the swapchain's present size, a fixed light */
#define VH_SPACE_LIGHT 0.7f
static void vh_frame_e(dev_st *ds, VkQueue q, const somvr_rec *rec, const XrCompositionLayerProjectionView pv[2],
                       xr_comp *c, const eyes_st *ey, const somvr_vrhands_frame *own) {
    VH.last = 0;
    if (!VH.on) return;
    if (own) {
        if ((!VH.ready || ds != VH.ds) && vh_make(ds, VH.src_w ? VH.src_w : X.rec_w, VH.src_h ? VH.src_h : X.rec_h)) return;
    } else if (!VH.ready || ds != VH.ds || VH.src_w != X.sc_w / 2 || VH.src_h != X.sc_h)
        if (vh_make(ds, X.sc_w / 2, X.sc_h)) return;
    somvr_vrhands_frame e;
    if (own) {
        e = *own, VH.n_space++;
    } else {
        VH.n_frames++;
        int have = somvr_vrhands_get(VH.obj, rec->frame, &e);
        if (!have) {
            VH.n_noentry++;
            if (!VH.have_e) return;
            e = VH.e; /* the last entry's hands (a frame the game published none for) */
        }
        if (VH.was_hide != (e.flags & SOMVR_VHF_HIDE) && VH.lines++ < 40)
            lg("xr: hands: frame %llu: %s", (unsigned long long)rec->frame,
               (e.flags & SOMVR_VHF_HIDE) ? "ours (the game hid its hands)" : "the game's own (not first person, or not yet)");
        VH.was_hide = e.flags & SOMVR_VHF_HIDE;
        if (!(e.flags & SOMVR_VHF_HIDE)) {
            VH.n_shown_game++;
            return;
        }
        VH.e = e, VH.have_e = 1;
    }
    uint32_t s = VH.slot++ % HD_NSLOT;
    dev_st *d = ds;
    if (VH.pending[s]) {
        VkResult w = d->WaitForFences(d->dev, 1, &VH.fence[s], VK_TRUE, XR_IMAGE_TIMEOUT_NS);
        if (w != VK_SUCCESS) return vh_off("our submit %u did not finish (%d)", s, w);
        VH.pending[s] = 0;
    }
    vh_lum_read(s, (double)rec->display_time * 1e-9); /* that slot's scene brightness, three frames old */
    vh_time_read(d, s);
    /* the vertices: the hands, then the occluders */
    hd_vert *v = hd_slot_verts(&VH.hc, s);
    int nv = somvr_vh_nverts(), hands[2] = {0, 0}, ghosts = 0;
    float ghost_a[2] = {0, 0};
    for (int h = 0; h < 2; h++) {
        if (!(e.flags & (h ? SOMVR_VHF_R : SOMVR_VHF_L))) continue;
        float skin[SOMVR_VH_NJ][12];
        somvr_vh_pose(&VH.m, &VH.vc, e.curl[h], skin);
        somvr_vh_skin(&VH.m, (const float(*)[12])skin, h, &e.grip[h], (somvr_vh_vert *)(void *)(v + h * nv));
        hands[h] = 1;
        if (!own && e.ghost_a[h] > 0.004f) { /* build 13: the real hand where a surface stopped ours */
            somvr_vh_skin(&VH.m, (const float(*)[12])skin, h, &e.ghost[h],
                          (somvr_vh_vert *)(void *)hd_slot_ghost(&VH.hc, s, h));
            ghost_a[h] = e.ghost_a[h], ghosts |= 1 << h;
        }
    }
    if (!hands[0] && !hands[1]) return;
    if (ghosts != VH.ghost_was && VH.lines++ < 200)
        lg("xr: hands: frame %llu: ghost L %s R %s (opacity %.2f %.2f; SOMVR_HANDS_SURFACE)", (unsigned long long)rec->frame,
           ghosts & 1 ? "on" : "off", ghosts & 2 ? "on" : "off", (double)ghost_a[0], (double)ghost_a[1]);
    VH.ghost_was = ghosts;
    VH.n_ghost += ghosts != 0;
    uint32_t nocc = 0;
    static const int occ_slots[] = {XL_VM0, XL_MMAP};
    for (size_t k = 0; k < sizeof occ_slots / sizeof occ_slots[0] && nocc < HD_MAX_OCC; k++)
        nocc += (uint32_t)vh_occ(c->l[occ_slots[k]], v + 2 * nv + 4 * nocc);
    VH.n_occ += nocc > 0;
    hd_frame fr;
    memset(&fr, 0, sizeof fr);
    for (int i = 0; i < 2; i++) {
        float fov[4] = {pv[i].fov.angleLeft, pv[i].fov.angleRight, pv[i].fov.angleUp, pv[i].fov.angleDown};
        hd_eye_matrix(rec->eye[i].pose.q, rec->eye[i].pose.p, fov, 0.02f, 20.0f, fr.vp[i]);
        memcpy(fr.eye[i], rec->eye[i].pose.p, sizeof fr.eye[i]);
    }
    fr.hand[0] = hands[0], fr.hand[1] = hands[1], fr.nocc = nocc;
    int world = own ? 0 : vh_depth_frame(d, rec, &e, &fr); /* build 11: both eyes' depth (else build 10's look) */
    fr.held[0] = e.flags & SOMVR_VHF_HOLD_L ? VH.dheld : 1, fr.held[1] = e.flags & SOMVR_VHF_HOLD_R ? VH.dheld : 1;
    fr.light[0] = 0.27f, fr.light[1] = 0.86f, fr.light[2] = 0.43f; /* from above, a little behind the start's forward */
    fr.opacity = 1;
    fr.ghost_a[0] = ghost_a[0], fr.ghost_a[1] = ghost_a[1];
    fr.ghost_base[0] = 0.30f, fr.ghost_base[1] = 0.58f, fr.ghost_base[2] = 1.0f; /* the wraith's blue */
    if (VH.look == VH_LOOK_SKIN)
        fr.base[0] = 0.50f, fr.base[1] = 0.33f, fr.base[2] = 0.24f, fr.spec = 0.05f;
    else
        fr.base[0] = 0.13f, fr.base[1] = 0.085f, fr.base[2] = 0.06f, fr.spec = 0.10f;
    float light = own ? VH_SPACE_LIGHT : VH.auto_light && VH.lum_have ? VH.lum_light : VH.light;
    fr.key = 0.9f * light, fr.ambient = 0.45f * light, fr.rim = 0.25f;
    /* the image: acquire, draw, submit, release */
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wi = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    XrResult r = X.xrAcquireSwapchainImage(VH.sc, &ai, &VH.xi);
    if (XR_FAILED(r) || VH.xi >= VH.nimg) return vh_off("xrAcquireSwapchainImage (hands): %s", xr_str(r));
    VH.acquired = 1;
    if ((r = X.xrWaitSwapchainImage(VH.sc, &wi)) != XR_SUCCESS) return vh_off("xrWaitSwapchainImage (hands): %s", xr_str(r));
    VkCommandBuffer cmd = VH.cmd[s];
    uint64_t dump_n = mn_dump_frame; /* SOMVR_XR_DUMP: this frame's eyes were dumped (its quads are logged at the end) */
    int dump = !own && dump_n && D.base[0] && vh_dump_buffer(d, (VkDeviceSize)2 * VH.ew * VH.eh * 4) == 0;
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VkResult vr = d->ResetFences(d->dev, 1, &VH.fence[s]);
    if (vr == VK_SUCCESS) vr = d->ResetCommandBuffer(cmd, 0);
    if (vr == VK_SUCCESS) vr = d->BeginCommandBuffer(cmd, &bi);
    if (vr == VK_SUCCESS) {
        vh_lum_cmds(d, cmd, s, ey); /* SOMVR_XR_HANDS_LIGHT=auto: the scene's brightness, read back 3 frames on */
        if (VH.qp) {
            VH.CmdResetQueryPool(cmd, VH.qp, 2 * s, 2);
            VH.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VH.qp, 2 * s);
        }
        /* build 11: the depth textures sampled (a test boot's dump reads them back first), then returned */
        VkDeviceSize zs = ((VkDeviceSize)VH.dw * VH.dh + 3) & ~(VkDeviceSize)3; /* an eye's stencil bytes, padded */
        int sten = VH.dfmt == VK_FORMAT_D32_SFLOAT_S8_UINT;
        VH.zdump = world && dump && XT.on && VH.daspect && (VH.dfmt == VK_FORMAT_D32_SFLOAT || sten) &&
                   vh_zbuf(d, (VkDeviceSize)2 * VH.dw * VH.dh * 4 + 2 * zs) == 0;
        VH.zsten = VH.zdump && sten;
        if (world && VH.zdump) {
            vh_depth_barrier(d, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            for (int k = 0; k < 2; k++) {
                VkBufferImageCopy bc = {.bufferOffset = (VkDeviceSize)k * VH.dw * VH.dh * 4,
                                        .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                                        .imageExtent = {VH.dw, VH.dh, 1}};
                d->CmdCopyImageToBuffer(cmd, VH.dimg[k], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VH.zbuf, 1, &bc);
                if (VH.zsten) { /* and its stencil (which draws marked what) */
                    bc.bufferOffset = (VkDeviceSize)2 * VH.dw * VH.dh * 4 + (VkDeviceSize)k * zs;
                    bc.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
                    d->CmdCopyImageToBuffer(cmd, VH.dimg[k], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VH.zbuf, 1, &bc);
                }
            }
            VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
            d->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
            vh_depth_barrier(d, cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VH.zk = fr.depth_k;
        } else if (world)
            vh_depth_barrier(d, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        hd_record(&VH.hc, cmd, s, &fr, VH.img[VH.xi], VK_IMAGE_LAYOUT_UNDEFINED,
                  dump ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (world)
            vh_depth_barrier(d, cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        if (VH.qp) {
            VH.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VH.qp, 2 * s + 1);
            VH.qrec[s] = world ? 2 : 1;
        }
        if (VH.zdump) memcpy(VH.zbox, fr.box, sizeof VH.zbox);
        if (dump) {
            VkBufferImageCopy bc = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                    .imageExtent = {2 * VH.ew, VH.eh, 1}};
            d->CmdCopyImageToBuffer(cmd, VH.img[VH.xi], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VH.dbuf, 1, &bc);
            VkImageMemoryBarrier b;
            img_barrier(&b, VH.img[VH.xi], VK_ACCESS_TRANSFER_READ_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
            d->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 1, &b);
        }
        vr = d->EndCommandBuffer(cmd);
    }
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    if (vr == VK_SUCCESS) vr = d->QueueSubmit(q, 1, &si, VH.fence[s]);
    if (vr != VK_SUCCESS) return vh_off("recording or submitting our drawing: %d", vr);
    VH.pending[s] = 1;
    if (dump && d->WaitForFences(d->dev, 1, &VH.fence[s], VK_TRUE, XR_IMAGE_TIMEOUT_NS) == VK_SUCCESS) {
        VH.pending[s] = 0, vh_dump_write(dump_n);
        if (VH.zdump) vh_zdump_write(dump_n);
        vh_time_read(d, s);
        vh_time_log("a dump");
        vh_lum_read(s, (double)rec->display_time * 1e-9);
        lg("xr: hands: the scene's mean linear luminance %.4f (eased %.4f, the meadow's by day %.3f): the light x%.2f%s",
           VH.lum, VH.lum, (double)VH_LUM_REF, (double)light, VH.auto_light ? "" : " (fixed)");
    }
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    VH.acquired = 0;
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(VH.sc, &ri))) return vh_off("xrReleaseSwapchainImage (hands): %s", xr_str(r));
    for (int i = 0; i < 2; i++) {
        VH.pv[i] = pv[i];
        VH.pv[i].subImage = (XrSwapchainSubImage){VH.sc, {{(int32_t)((uint32_t)i * VH.ew), 0}, {(int32_t)VH.ew, (int32_t)VH.eh}}, 0};
    }
    VH.proj = (XrCompositionLayerProjection){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                                             .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                             .space = X.space, .viewCount = 2, .views = VH.pv};
    c->l[XL_HANDS] = (const XrCompositionLayerBaseHeader *)&VH.proj;
    VH.last = 1;
    if (own) { /* the loading space's: not the game's frames */
        if (VH.n_space_drawn++ < 2 || VH.n_space_drawn % 1800 == 0)
            lg("xr: hands in the loading space: %s%s hand(s) from the layer's own input (%llu drawn so far)",
               hands[0] ? "L" : "", hands[1] ? "R" : "", (unsigned long long)VH.n_space_drawn);
        return;
    }
    VH.n_drawn++;
    atomic_fetch_add_explicit(&VH.obj->n_drawn, 1, memory_order_relaxed);
    atomic_store_explicit(&VH.obj->last_drawn, rec->frame, memory_order_relaxed);
    if (VH.n_drawn % 1800 == 0) vh_time_log("every 1800 frames");
    if (VH.n_drawn <= 3 || VH.n_drawn % 1800 == 0)
        lg("xr: hands frame %llu: %s%s hand(s), %u occluder(s), the world %s (%llu of %llu; %llu eyes the next frame's), boxes %dx%d / %dx%d of %ux%u, light x%.2f (scene %.4f), curls L %.2f %.2f %.2f %.2f %.2f R %.2f %.2f %.2f %.2f "
           "%.2f, flags 0x%x (drawn %llu of %llu stereo frames; no entry %llu, the game's hands %llu; with a ghost %llu)",
           (unsigned long long)rec->frame, hands[0] ? "L" : "", hands[1] ? "R" : "", nocc,
           !VH.world ? "off" : world ? "hides them" : "not this frame", (unsigned long long)VH.n_dused,
           (unsigned long long)(VH.n_dused + VH.n_dnone), (unsigned long long)VH.n_dnext, fr.box[0][2] - fr.box[0][0],
           fr.box[0][3] - fr.box[0][1], fr.box[1][2] - fr.box[1][0], fr.box[1][3] - fr.box[1][1], VH.ew, VH.eh, (double)light,
           (double)VH.lum,
           e.curl[0][0], e.curl[0][1],
           e.curl[0][2], e.curl[0][3], e.curl[0][4], e.curl[1][0], e.curl[1][1], e.curl[1][2], e.curl[1][3],
           e.curl[1][4], e.flags, (unsigned long long)VH.n_drawn, (unsigned long long)VH.n_frames,
           (unsigned long long)VH.n_noentry, (unsigned long long)VH.n_shown_game, (unsigned long long)VH.n_ghost);
}

static void vh_frame(dev_st *ds, VkQueue q, const somvr_rec *rec, const XrCompositionLayerProjectionView pv[2], xr_comp *c,
                     const eyes_st *ey) {
    vh_frame_e(ds, q, rec, pv, c, ey, NULL);
}

/* a reuse frame repeats the last stereo frame's hands (the swapchain's last released image is theirs) */
static void vh_reuse(xr_comp *c) {
    if (VH.on && VH.last && VH.sc) c->l[XL_HANDS] = (const XrCompositionLayerBaseHeader *)&VH.proj;
}
