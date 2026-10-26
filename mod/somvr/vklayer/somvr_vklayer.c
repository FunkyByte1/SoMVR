/* somvr_vklayer.c - VK_LAYER_SOMVR_openxr: an explicit Vulkan layer that bridges Shadow of Mordor (native Linux Feral
 * build, Vulkan 1.1 on RADV) to OpenXR. From mod/vklayer (phase 1: the presented image as a mono screen); this copy
 * adds the per-eye path of docs/re-xr-pipeline.md section 4: it finds the LD_PRELOAD library's `somvr_shared`
 * (dlsym(RTLD_DEFAULT)), publishes the session, space, xrLocateViews and the predicted display time there, and submits
 * each tagged stereo present as a projection layer with exactly the poses and fovs the game rendered (frame record).
 *
 * README
 *   Build/test: ./build.sh [--check] [--vulkaninfo]   (C11, no libstdc++; links only libc and libm)
 *   Enable (game environment only):
 *     VK_LAYER_PATH=<this dir>  VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
 *     XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json (the harness's HOME has no active_runtime.json)
 *   Switches:
 *     SOMVR_XR=0|off       pure pass-through (no OpenXR, nothing added, present not intercepted)
 *     SOMVR_XR unset|1     OpenXR only for the instance whose engine name is "Feral3D" (the game); others pass through
 *     SOMVR_XR=force       OpenXR for any app (e.g. a vkcube smoke test). Any other value = off.
 *     SOMVR_XR_LOG=FILE    append a log (O_CLOEXEC)
 *     SOMVR_XR_LAYER=quad|proj  for mono presents (no stereo record): quad (default) = a world-locked 2 m wide screen
 *                          2 m ahead; proj = a projection layer, both views = the whole image, the located eye poses
 *     SOMVR_XR_SPACE=local|stage  the reference space for everything (default LOCAL; STAGE falls back to LOCAL)
 *     SOMVR_XR_FLOOR=N     (default 300; 0 = off) in a LOCAL space, N frames into a session at a stereo frame (else at
 *                          10 N; 100 frames after a recentre)
 *                          one line: LOCAL's origin above the runtime's STAGE floor and the last stereo eyes' height
 *                          above it = the player's real eye height (docs/re-xr-pipeline.md 35.1; log only)
 *     SOMVR_XR_LOADER=PATH (tests) OpenXR loader, default /usr/lib64/libopenxr_loader.so.1 (dlopened at runtime)
 *     SOMVR_XR_TIMEOUT_MS  (default 5000) limit for xrCreateInstance..xrGetSystem, run on a helper thread
 *     SOMVR_XR_DUMP=N[,M,...]  (at most 8 positive numbers; off without SOMVR_XR_LOG) after the N-th layout-B stereo
 *                          submit the XR image (both eyes side by side, as released to the runtime) goes to
 *                          <SOMVR_XR_LOG>.stereoN.ppm (P6, the bytes as they are: sRGB-encoded): one more barrier and a
 *                          copy into a host-visible buffer (made at the first eye frame, freed with the device) in that
 *                          submit, then its fence is waited (1 s cap) on the present thread. Unset: none of it runs.
 *                          "mN": the first copied mono frame (menus: the quad's image) from the N-th on, the whole XR
 *                          image -> <SOMVR_XR_LOG>.monoN.ppm (e.g. SOMVR_XR_DUMP=m30,600). "sN": the first copied frame
 *                          (either kind) of the N-th XR swapchain the layer made (1 = the first; each size change makes
 *                          one, logged "(swapchain N, replacing WxH)") -> <SOMVR_XR_LOG>.swapchainN.ppm: SOMVR_EYE_SIZE's
 *                          switch between the menus' 16:9 and the eye size (docs/re-xr-pipeline.md 9), e.g. s2,s3,s4.
 *     SOMVR_XR_PREDICT=auto|off|MS  the time published for the game thread's xrLocateViews (docs/re-xr-pipeline.md
 *                          7.1): auto (default) = predictedDisplayTime + the pipeline lead measured from the submitted
 *                          records (the game renders ~2 frames ahead of the present that submits them); off = the
 *                          wait's own time (before 2026-10-20: views 33 ms early at 60 fps); MS = a fixed lead (0..250).
 *     SOMVR_XR_INPUT=on|off|log  motion controllers (docs/re-xr-input.md section 1). on (default): only with the
 *                          preload's active `somvr_input` (../somvr_input.h) whose want has STATE; off: not a single
 *                          action call; log: also without the object (R0: vkcube + SOMVR_XR=force), button changes and
 *                          poses go to the log. Any other value = off.
 *     SOMVR_XR_HUD=world|head|off  the HUD quad (H11, docs/re-xr-pipeline.md 8), only with the preload's active
 *                          `somvr_hud` (../somvr_hud.h, SOMVR_HUD=quad) in layout B: world (default) = placed once
 *                          SOMVR_XR_HUD_QUAD's distance ahead of the head (level, yaw only), then fixed in the space,
 *                          placed again after a recentre; head = head-locked in a VIEW space; off = no quad.
 *     SOMVR_XR_HUD_QUAD=DIST,WIDTH,DROP  metres (default 1.5,1.2,0.1): the quad's distance, width (its height from the
 *                          HUD image's aspect) and how far below eye level its centre sits.
 *     SOMVR_XR_HUD_ALPHA=straight|premul  the quad's blend: BLEND_TEXTURE_SOURCE_ALPHA always, plus
 *                          UNPREMULTIPLIED_ALPHA for straight (default; the UI surface's alpha mode is RT U2).
 *     SOMVR_XR_POINTER=on|dot|off  the laser pointer (docs/re-xr-pipeline.md 10), only with the preload's somvr_input v2
 *                          whose want has POINTER (SOMVR_POINTER=1): on (default) = the dot and the ray, dot = the dot
 *                          only, off = no pointer (nothing computed or published, no layer).
 *     SOMVR_XR_FADE=on|off  the comfort fade (docs/re-xr-pipeline.md 6.8, ./somvr_fade.h): on (default) = a stereo frame
 *                          whose record carries a fade level (flags bits 24..31: the preload's SOMVR_FALLBACK=auto) ends
 *                          with a black quad over its other layers; off = never drawn.
 *     SOMVR_XR_VRMENU=on|off  the VR settings menu (docs/re-xr-pipeline.md 15, ../somvr_vrmenu.h), only with the
 *                          preload's active `somvr_vrmenu` (SOMVR_VRMENU=1) and its somvr_input: on (default) = the wrist
 *                          panel; off = no panel, nothing published.
 *     SOMVR_XR_THREAT=on|off  the threat cues (docs/re-xr-pipeline.md 18, ../somvr_threat.h), only with the preload's
 *                          active `somvr_threat` (SOMVR_THREAT=1): on (default) = the glows and the edge bands; off = none.
 *     SOMVR_XR_ENV=off|on|world  SoM loading env (docs/re-xr-pipeline.md 14, ./somvr_env.h): off (default) = mono
 *                          frames in the black void as before; on = the procedural Mordor-dusk sky behind every mono
 *                          frame shown as the menu quad; world = also the last stereo frame, kept and dimmed. Any other
 *                          value = off. SOMVR_XR_ENV_DIM=0..1 (default 0.4): the kept frame's brightness.
 *   Input (on, or log): before xrCreateSession the action set "somvr" (12 actions, both hands) and one suggestion per
 *     profile (oculus/touch_controller, valve/index_controller, khr/simple_controller: a rejected one fails only
 *     itself); after the reference space 4 action spaces (grip/aim x L/R) and xrAttachSessionActionSets. At the
 *     session's begin, before somvr_shared xr.state = RUNNING: the spaces and xrLocateSpace go to somvr_input.xr
 *     (want SPACES), then ready = 1. After every xrWaitFrame: xrSyncActions, the action states, grip (+velocity) and
 *     aim located at the predicted display time, one snapshot under somvr_input's seqlock. Each present after xrPollEvent:
 *     a new haptic request number per hand = one xrApplyHapticFeedback (amplitude 0: xrStopHapticFeedback). Any input
 *     error: "xr: input off: <call> <result>" (setup) or one line per kind (per frame); the video path is unaffected.
 *     Teardown: xr.state = DEAD and no xrLocateViews/xrLocateSpace in flight -> ready = 0 -> the 4 spaces -> session.
 *   Stereo (somvr_shared present and active): the session is published once it runs (xr.state = RUNNING); every
 *     xrWaitFrame publishes {predictedDisplayTime + the lead (SOMVR_XR_PREDICT), period}; a present tagged with a
 *     complete record is copied once into the XR swapchain (layout A: one image, the eyes' halves; B: the preload's two
 *     eye images, named in the tag and in somvr_shared.eyes, copied side by side into a 2 x eye-wide swapchain in one
 *     submit) and submitted as a projection layer with the record's XR poses and fovs (fov_cover where H3 did not
 *     apply; the preload's SOMVR_OVERSCAN is already in them); a tag whose record is missing re-submits the last stereo
 *     layer without a copy (the runtime shows the last released image); untagged or frame-0 presents are mono (layout
 *     B: the game image in slot 0, its last column repeated into the 4 columns right of it, the guard). Before the
 *     space/session go away: xr.state = DEAD, wait until no xrLocateViews runs.
 *   HUD (somvr_hud active, layout B): a stereo frame from the eye images whose present has a HUD entry (somvr_hud's tag
 *     for the core tag's seq) naming an image this device made at the announced HUD size also copies that image into
 *     its own XR swapchain (UI size, R8G8B8A8_SRGB for the R8G8B8A8_UNORM source, raw copy) in the same submit as the
 *     eyes (taken from and returned to TRANSFER_DST, as the eye images) and ends the frame with two layers: the
 *     projection, then an XrCompositionLayerQuad with BLEND_TEXTURE_SOURCE_ALPHA (+ UNPREMULTIPLIED_ALPHA). No entry,
 *     gen 0, an unknown image: the projection alone. A reuse frame repeats the last quad with the last projection. Any
 *     HUD failure (no copy format, its swapchain, VIEW space, acquire/wait/release, xrEndFrame rejecting the quad: the
 *     frame is ended again with the projection alone) = the HUD off for the session, stereo goes on.
 *   Pointer (somvr_input v2, want POINTER; ../somvr_pointer.h): every mono frame shown as the menu quad (and, with
 *     want PTR_HUD, every stereo frame with the world-locked HUD quad) runs the pointer on this frame's snapshot (the
 *     aim poses at the frame's display time; the grip without an aim pose): the ray against the quad, hysteresis, a
 *     One Euro filter, the hand = the last one that pulled its trigger, the trigger's clicks; published in
 *     somvr_input.ptr (any other frame publishes "no target"), and drawn as two more quad layers after the frame's own:
 *     a thin ray from the hand (SOMVR_XR_POINTER=on) and a dot at the hit, both from one 64x32 swapchain filled once
 *     (a static image, one buffer upload). Any pointer failure (no format, its swapchain, the upload, xrEndFrame
 *     rejecting its layers: the frame is ended again without them) = no drawing for the session; the published
 *     pointer goes on (the menus still take the clicks).
 *   Fade (an active somvr_shared, SOMVR_XR_FADE on): a stereo frame whose record's fade level is not 0 ends with one
 *     more quad layer, the last: a 256x8 static image filled once (64 premultiplied black alpha levels, then the same in
 *     dark red for a record with SOMVR_REC_FADE_RED: SOMVR_BODYMOMENTS' knockdown dim), the level's
 *     middle 2x2 texels on a 4 m square 0.25 m ahead of the frame's own head (its record's eye poses), turned like it;
 *     a reuse frame repeats the last one. Any fade failure (no format, its swapchain, the upload, xrEndFrame rejecting it:
 *     the frame is ended again without it) = no fade for the session; the frames go on (the base swap is then seen).
 *   Comfort (the preload's active `somvr_comfort`, ../somvr_comfort.h: SOMVR_VIGNETTE and/or SOMVR_SNAP_BLINK_MS on the
 *     game side; docs/re-xr-pipeline.md 11): every stereo frame from a record reads that frame's entry (the rig's
 *     artificial speeds, the snap blink's black), filters the vignette's amount by display time and adds up to two
 *     quads: a black ring (its clear disc from the view's corners inward as the speed rises, nothing while it lies
 *     outside the view) right over the projection and under the HUD quad and the pointer (the HUD stays readable while
 *     moving), and the blink (one uniform black cell over the whole view) over them, under the fade only; both
 *     head-locked in a VIEW space 5 m ahead, from one static 512x528 alpha texture filled once. A reuse frame repeats
 *     the last ones. No object: nothing (not a call). Any failure (format, swapchain, upload, VIEW space, xrEndFrame
 *     rejecting them: the frame is ended again without them) = no comfort layers for the session.
 *   Threat (the preload's active `somvr_threat`, ../somvr_threat.h: SOMVR_THREAT=1; docs/re-xr-pipeline.md 18): every
 *     stereo frame from a record reads that frame's entry and adds up to 3 world-locked warm glows (the attackers'
 *     weapon hands in view, an inbound projectile in view; billboarded to the frame's head) right over the projection,
 *     and up to 2 head-locked edge bands (an attack or a projectile out of view, an engaged orc close behind; 1 m ahead
 *     at the view's left / right edge) over the vignette ring, from one static 544x102 texture filled once (16 intensity
 *     cells per kind, premultiplied, mostly additive). A reuse frame repeats them. Any failure = no threat cues for the
 *     session, the frames go on.
 *   Composition (xr_comp, docs/re-xr-pipeline.md 16), bottom to top: the environment's layers (mono and hold
 *     frames: the sky, the kept frame and its dim quads), the projection (or the mono quad), the threat glows, the
 *     vignette ring, the threat edge bands, the HUD quad, the pointer's ray and dot, the VR menu's panel, ray and dot,
 *     the snap blink, the fade: 24 slots, at most 16 layers at once (the environment's never come with the threat
 *     cues, the ring, the HUD or the blink; a stereo frame's worst case is 15). A rejected frame is ended
 *     again without one part at a time (the environment's, the fade, comfort's, the VR menu's, the pointer's, the
 *     HUD's); only the part whose removal the runtime accepts goes off for the session, the others stay.
 *   Env (SOMVR_XR_ENV on or world, SOMVR_XR_LAYER=quad): at xrCreateInstance XR_KHR_composition_layer_equirect2 is
 *     enabled too if the runtime lists it (refused: again without it). Every mono frame shown as the menu quad starts
 *     with the sky: one equirect2 layer (the whole sphere, 30 m, centred at the space's origin) or, without the
 *     extension, six world-locked quads (a 60 m box, its floor 1.6 m below the origin), from one static image filled
 *     once; world mode: then the last stereo frame (its eye images copied into a static swapchain of ours at the switch,
 *     a projection layer with its own poses and fovs, freed at the next stereo frame) under one black quad per eye over
 *     exactly its view (1 - DIM). A switch between the world and the environment holds the shown side's last layers
 *     while the fade quad darkens (0.2 s), then draws the other side from black (in over 0.3 s); the fade's swapchain
 *     is used (SOMVR_XR_FADE=off: cuts). Any failure (format, swapchain, upload, xrEndFrame rejecting the environment's
 *     layers: ended again without them) = no environment for the session; a kept frame's failure = the sky alone.
 *   VR menu (the preload's active `somvr_vrmenu`, ../somvr_vrmenu.h; docs/re-xr-pipeline.md 15): every frame the off
 *     hand's palm toward the eyes held 0.5 s (or a long press of the menu button, counted by the game side) opens a 20 x
 *     30 cm panel on that wrist (a quad following its grip), closed the same way or by looking away; the main hand's
 *     laser pointer (somvr_pointer.c, a third target SOMVR_PTRQ_VRMENU; its dot and ray from the pointer's image) clicks
 *     its rows; a change is published in the object (the game thread applies it live and saves it), haptic ticks on the
 *     pointing hand. The panel's texture (512x768 since v2, somvr_font.h) is drawn into a mapped staging buffer and uploaded to its
 *     own swapchain only when it changes. The panel's layers come after the pointer's (before comfort's). Any failure
 *     (format, swapchain, buffer, upload, xrEndFrame rejecting them) = no menu for the session.
 *   Flow (XR_KHR_vulkan_enable, version 1):
 *     vkCreateInstance  helper thread: dlopen loader, xrCreateInstance, xrGetSystem(HMD), graphics requirements,
 *                       instance + device extension lists; append the instance ones (no duplicates; raise apiVersion
 *                       only if the runtime's minimum is higher). If that create fails, retry with the app's info.
 *     vkCreateDevice    append the device extensions if the GPU has them all; retry with the app's info on failure.
 *     vkGetDeviceQueue* remember queue -> (device, family, index).
 *     vkCreateSwapchainKHR  add TRANSFER_SRC to the image usage (retry without it on failure: no copy then).
 *     vkCreateImage/vkDestroyImage  remember images of the size the preload announced (somvr_shared.eyes.expect_w/h):
 *                       a layout-B eye image is used only if this device created it (2D, 1 mip, 1 layer, 1 sample,
 *                       TRANSFER_SRC) and it still exists. They sit in TRANSFER_DST_OPTIMAL (Feral's CopyResource) and
 *                       are returned to it after our copy. The 32 candidates keep the images the preload publishes and
 *                       the ones a lookup used (cand_slot): every mode set makes the game's own surfaces at those sizes.
 *     first vkQueuePresentKHR  xrGetVulkanGraphicsDeviceKHR (checked against the game's GPU through the loader's own
 *                       entry points), xrCreateSession with the present queue, LOCAL space. Not earlier: the loader
 *                       holds its global lock inside vkCreateInstance/vkCreateDevice and the runtime calls back into it.
 *     every present     xrPollEvent (begin/end session), xrBeginFrame (after the xrWaitFrame of the previous present),
 *                       acquire+wait the XR image, one submit on the present queue: wait the app's present semaphores,
 *                       copy the image into its slot of the (nslots*w x h) swapchain, signal our semaphore (per game
 *                       image; fence per image); release, xrEndFrame; the real present then waits our semaphore only;
 *                       then xrWaitFrame for the next frame (paces the game) and the time is published.
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
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "somvr_logic.h"
#include "somvr_fade.h"
#include "somvr_env.h"
#include "../somvr_pointer.h"
#include "../somvr_comfort.h"
#include "../somvr_threat.h"
#include "../somvr_vrmenu.h"
#include "../somvr_session.h" /* SOMVR_MARK: a mark's eyes; SOMVR_MARK / SOMVR_QUIT: the menu's Report and Quit game */
#include "../somvr_menus.h" /* build 6: the menu boards, the wrist minimap, touch (./somvr_menus_layer.c) */
#include "../somvr_vrhands.h" /* build 10: our own VR hands, the watch, the panel (./somvr_vrhands_layer.c) */

_Static_assert(sizeof(somvr_posef) == sizeof(XrPosef) && sizeof(somvr_fovf) == sizeof(XrFovf), "ABI pose/fov types");

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
    int mode, layer, timeout_ms, space, input;
    int predict;         /* SOMVR_XR_PREDICT: SOMVR_PREDICT_* */
    int predict_bad;     /* the value was not understood (logged with the session) */
    int64_t predict_ns;  /* its fixed lead */
    int hud, hud_alpha;  /* SOMVR_XR_HUD (SOMVR_HUD_*), SOMVR_XR_HUD_ALPHA */
    int hud_bad;         /* bit 0 SOMVR_XR_HUD, 1 _ALPHA, 2 _QUAD not understood (logged with the session) */
    float hud_quad[3];   /* SOMVR_XR_HUD_QUAD: distance, width, drop (m) */
    int ptr, ptr_bad;    /* SOMVR_XR_POINTER (SOMVR_PTRM_*), not understood (logged with the session) */
    int fade, fade_bad;  /* SOMVR_XR_FADE: 1 on (default), 0 off; not understood (logged with the session) */
    int vm, vm_bad;      /* SOMVR_XR_VRMENU: 1 on (default), 0 off; not understood (logged with the session) */
    int th, th_bad;      /* SOMVR_XR_THREAT: 1 on (default), 0 off; not understood (logged with the session) */
    int floor_frames;    /* SOMVR_XR_FLOOR: the floor line this many frames into a session (0 = none) */
    const char *loader;
} cfg;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
/* SOMVR_XR_DUMP (under X.mx; the buffer belongs to D.ds and goes with it) */
static struct {
    int n;                       /* frames asked for; 0 = off: no buffer, no extra barrier */
    uint64_t at[SOMVR_DUMP_MAX]; /* X.n_eyes after the submit to dump (the N-th layout-B stereo submit), or with
                                    SOMVR_DUMP_MONO: X.n_mono (the first copied mono frame from the N-th on) */
    uint32_t done;               /* bit i: at[i] written */
    char base[1024];             /* SOMVR_XR_LOG: <base>.stereoN.ppm, <base>.monoN.ppm */
    int armed;                   /* this present's eyes_submit/copy_submit also copies the XR image into buf */
    int arm_ix;                  /* the at[] entry it is for */
    struct dev_st *ds;
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;                   /* mapped while it exists (HOST_COHERENT) */
    VkDeviceSize size;
} D;
/* the preload's somvr_session (../somvr_session.h, docs/re-xr-pipeline.md 28; found at the session's setup): a bug mark's
 * eyes (a new dump_req: the next frame's XR image, through D's buffer, into <dump_dir>/eyes.ppm on a thread of its own);
 * the VR menu's Report and Quit game buttons (the armed Quit: a second press within 5 s confirms). X.mx held. */
static struct {
    somvr_session_shared *obj;   /* active, else NULL */
    uint32_t seen;               /* the last dump_req taken */
    uint32_t want;               /* the dump_req being served (0 = none) */
    int dead;                    /* a dump could not be made at all (the format, the buffer): no more tries */
    char dir[SOMVR_SS_DIR_MAX];
    uint64_t n_asked, n_threads;
    int quit_armed;              /* the menu's Quit game was pressed once at quit_t */
    double quit_t;
} SSN;
static void ss_setup(void); /* (with the dump code below) */

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
static void mn_config(void); /* build 6 (./somvr_menus_layer.c, included before vm_frame) */
static void vd_config(void); /* never a void: SOMVR_XR_GAPS (./somvr_void_layer.c) */
static void once_init(void) {
    const char *p = getenv("SOMVR_XR_LOG"), *logp = p;
    if (p && *p) g_log = fopen(p, "ae");
    cfg.mode = somvr_parse_mode(getenv("SOMVR_XR"));
    cfg.layer = somvr_parse_layer(getenv("SOMVR_XR_LAYER"));
    cfg.space = somvr_parse_space(getenv("SOMVR_XR_SPACE"));
    cfg.input = somvr_parse_input(getenv("SOMVR_XR_INPUT"));
    p = getenv("SOMVR_XR_LOADER");
    cfg.loader = p && *p ? p : "/usr/lib64/libopenxr_loader.so.1";
    p = getenv("SOMVR_XR_FLOOR");
    cfg.floor_frames = p && *p ? atoi(p) : 300;
    if (cfg.floor_frames < 0 || cfg.floor_frames > 1000000) cfg.floor_frames = 300;
    p = getenv("SOMVR_XR_TIMEOUT_MS");
    cfg.timeout_ms = p ? atoi(p) : 5000;
    if (cfg.timeout_ms < 50 || cfg.timeout_ms > 60000) cfg.timeout_ms = 5000;
    int bad_predict = 0;
    cfg.predict = somvr_parse_predict(getenv("SOMVR_XR_PREDICT"), &cfg.predict_ns, &bad_predict);
    xr_global_init();
    lg("VK_LAYER_SOMVR_openxr loaded: SOMVR_XR=%s -> %s, mono layer %s, space %s, loader %s, timeout %d ms, input %s",
       getenv("SOMVR_XR") ? getenv("SOMVR_XR") : "(unset)",
       cfg.mode == SOMVR_MODE_OFF ? "pass-through" : cfg.mode == SOMVR_MODE_FORCE ? "force" : "auto (Feral3D only)",
       cfg.layer == SOMVR_LAYER_PROJ ? "proj" : "quad", cfg.space ? "stage" : "local", cfg.loader, cfg.timeout_ms,
       cfg.input == SOMVR_INPUT_OFF ? "off" : cfg.input == SOMVR_INPUT_LOG ? "log" : "on");
    cfg.predict_bad = bad_predict;
    int b0 = 0, b1 = 0, b2 = 0;
    cfg.hud = somvr_parse_hud(getenv("SOMVR_XR_HUD"), &b0);
    cfg.hud_alpha = somvr_parse_hud_alpha(getenv("SOMVR_XR_HUD_ALPHA"), &b1);
    somvr_parse_hud_quad(getenv("SOMVR_XR_HUD_QUAD"), cfg.hud_quad, &b2);
    cfg.hud_bad = b0 | b1 << 1 | b2 << 2;
    cfg.ptr = somvr_ptr_parse_mode(getenv("SOMVR_XR_POINTER"), &cfg.ptr_bad);
    p = getenv("SOMVR_XR_FADE"); /* the comfort fade (SOMVR_FALLBACK's records) */
    cfg.fade = !p || !*p || !strcmp(p, "on") || !strcmp(p, "1") ? 1 : 0;
    cfg.fade_bad = p && *p && strcmp(p, "on") && strcmp(p, "1") && strcmp(p, "off") && strcmp(p, "0");
    p = getenv("SOMVR_XR_VRMENU"); /* the VR settings menu (the preload's SOMVR_VRMENU) */
    cfg.vm = !p || !*p || (strcmp(p, "off") && strcmp(p, "0"));
    cfg.vm_bad = p && *p && strcmp(p, "on") && strcmp(p, "1") && strcmp(p, "off") && strcmp(p, "0");
    p = getenv("SOMVR_XR_THREAT"); /* the threat cues (the preload's SOMVR_THREAT) */
    cfg.th = !p || !*p || (strcmp(p, "off") && strcmp(p, "0"));
    cfg.th_bad = p && *p && strcmp(p, "on") && strcmp(p, "1") && strcmp(p, "off") && strcmp(p, "0");
    p = getenv("SOMVR_XR_DUMP");
    if (p && *p && g_log) { /* the files are named after the log: no log, no dump */
        int n = somvr_parse_dump(p, D.at);
        if (n < 0 || strlen(logp) + 32 > sizeof D.base) {
            lg("xr: dump off: SOMVR_XR_DUMP=%s is not N[,M,...] (at most %d positive numbers, each with an optional m or "
               "s), or the log path is too long", p, SOMVR_DUMP_MAX);
        } else {
            D.n = n;
            snprintf(D.base, sizeof D.base, "%s", logp);
            lg("xr: dump after layout-B stereo submit %s -> %s.stereoN.ppm (mN: the N-th mono frame -> %s.monoN.ppm; sN: "
               "the N-th XR swapchain's first frame -> %s.swapchainN.ppm)", p, D.base, D.base, D.base);
        }
    }
    mn_config(); /* build 6: SOMVR_XR_MENU, _MINIMAP, _CARD, _TOUCH (./somvr_menus_layer.c) */
    vd_config(); /* never a void: SOMVR_XR_GAPS (./somvr_void_layer.c) */
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
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties; /* SOMVR_XR_DUMP */
} inst_st;

#define DEV_FUNCS(X)                                                                                                   \
    X(DestroyDevice) X(GetDeviceQueue) X(GetDeviceQueue2) X(CreateSwapchainKHR) X(DestroySwapchainKHR)                 \
    X(GetSwapchainImagesKHR) X(QueuePresentKHR) X(QueueSubmit) X(CreateCommandPool) X(DestroyCommandPool)            \
    X(AllocateCommandBuffers) X(FreeCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer)   \
    X(CmdPipelineBarrier) X(CmdCopyImage) X(CmdBlitImage) X(CreateFence) X(DestroyFence) X(WaitForFences)            \
    X(ResetFences) X(CreateSemaphore) X(DestroySemaphore) X(CreateImage) X(DestroyImage)                             \
    X(GetFenceStatus) /* SOMVR_XR_COVER: a reading's submit done (polled, never waited for) */                     \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory)                  \
    X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CmdCopyImageToBuffer) /* these two lines: SOMVR_XR_DUMP */ \
    X(CmdCopyBufferToImage) /* the pointer image's one upload */                                                    \
    X(CmdClearColorImage) /* build 6: the HUD image's minimap column */                                             \
    X(QueueSubmit2) X(QueueSubmit2KHR) X(QueueBindSparse) X(QueueWaitIdle) X(DeviceWaitIdle) /* the keep-alive's lock */

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
static void cv_slot_done(const slot_st *sl, int idle); /* SOMVR_XR_COVER (./somvr_void_layer.c): its readings */

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

/* ---- layout B eye images: the images this device created at the size the preload announced -------------------- */
_Static_assert(SOMVR_EYE_IMAGE_LAYOUT == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "eye image layout");
#define MAX_EYE_CAND 32
static somvr_shared *_Atomic g_sh_seen; /* X.sh for the image hooks (any thread) */
static somvr_hud_shared *_Atomic g_hud_seen; /* X.hud: its HUD texture's size is announced too (H11) */
static pthread_mutex_t g_cand_mx = PTHREAD_MUTEX_INITIALIZER;
static struct {
    VkImage img;
    VkDevice dev;
    VkFormat fmt;
    VkImageUsageFlags usage;
    uint32_t w, h;
    int used; /* a lookup found it: one of the preload's eye/HUD textures */
} g_cand[MAX_EYE_CAND];
static uint32_t g_cand_next;
static uint64_t g_cand_evict[4]; /* evictions by the kind of entry evicted (free, plain, used, published), for tests */

/* The slot for a new candidate (g_cand_mx held). With SOMVR_EYE_SIZE's runtime switch (re-xr-pipeline.md 9) every
 * mode set makes the game's own surfaces again at the eye size (rt3D's colour and G-buffer targets) and at the UI size,
 * long after the preload made its eye and HUD textures, so a plain ring pushes those out after a few switches (then
 * "not created by this device at that size": no stereo for the rest of the run). Kept, in this order: the images the
 * preload publishes right now (its eye info, its HUD info), the ones a lookup has used; a free slot first, then the
 * oldest plain candidate in ring order. */
static uint32_t cand_slot(somvr_shared *sh, somvr_hud_shared *hud, int *rank_out) {
    uint64_t pin[3] = {0, 0, 0};
    somvr_eyes_info ei;
    somvr_hud_info hi;
    if (sh && somvr_eyes_read(sh, &ei)) pin[0] = ei.image[0], pin[1] = ei.image[1];
    if (hud && somvr_hud_read(hud, &hi)) pin[2] = hi.image;
    uint32_t best = g_cand_next % MAX_EYE_CAND;
    int best_rank = 4;
    for (uint32_t k = 0; k < MAX_EYE_CAND && best_rank > 0; k++) {
        uint32_t i = (g_cand_next + k) % MAX_EYE_CAND;
        uint64_t v = (uint64_t)(uintptr_t)g_cand[i].img;
        int rank = !v ? 0 : (v == pin[0] || v == pin[1] || v == pin[2]) ? 3 : g_cand[i].used ? 2 : 1;
        if (rank < best_rank) best = i, best_rank = rank;
    }
    g_cand_evict[best_rank < 4 ? best_rank : 3]++;
    g_cand_next = best + 1;
    *rank_out = best_rank;
    return best;
}
typedef struct {
    VkImage img[2];
    VkFormat fmt;
    uint32_t w, h, gen;
} eyes_st;
typedef struct { /* the HUD image a present's somvr_hud entry names (H11) */
    VkImage img;
    VkFormat fmt;
    uint32_t w, h, gen, kind;
} hud_st;
/* build 6 (docs/re-xr-pipeline.md 28; ./somvr_menus_layer.c, included before vm_frame): the menu boards, the wrist
 * minimap, the arrival card, touch */
static void mn_setup(void);
/* build 10: our own VR hands, the watch on the wrist, the panel off the palm (./somvr_vrhands_layer.c) */
static void vh_setup(void);
static void vh_destroy(void);
static void vh_panel_cfg(somvr_vm_gcfg *g);
static int vh_watch_on(void);
static int vh_watch_pose(int h, const somvr_posef *grip, somvr_posef *face);
static int vh_watch_facing(const somvr_posef *face, const float eye[3]);
static void vh_watch_reset(void);
static int vh_touch_tip(int h, float out[3]);
static int vh_touch_pose(int h, const somvr_posef *quad, const float size[2], float u, float v, float front_m,
                         somvr_posef *out);
static void mn_replace(void);
static void mn_set_kind(uint32_t kind);
static void mn_mono_quad(XrCompositionLayerQuad *q, uint32_t w, uint32_t h);
static void mn_hud_quad(XrCompositionLayerQuad *q, const somvr_rec *rec);
static int mn_hud_menu(void);
static uint32_t mn_hud_extra(uint32_t w, uint32_t h);
static int mn_hud_regions(const struct dev_st *ds, const hud_st *hd, VkImageCopy *c, int cap);
static void mn_hud_clear(struct dev_st *ds, VkCommandBuffer cmd, VkImage img);
static VkImageLayout mn_hud_dump_cmds(struct dev_st *ds, VkCommandBuffer cmd, VkImage img);
static const XrCompositionLayerBaseHeader *mn_mm_layer(const somvr_rec *rec);
static const XrCompositionLayerBaseHeader *mn_mm_reuse(void);
static void mn_band_place(const somvr_posef eye[2]);                /* build 8: the prompt band (placed with the HUD) */
static const XrCompositionLayerBaseHeader *mn_band_layer(void);
static const XrCompositionLayerBaseHeader *mn_band_reuse(void);
static int mn_ptr_touch(uint32_t quad, const somvr_ptr_in *in, somvr_ptr_out *o, const float *eye,
                        XrCompositionLayerQuad *q, uint32_t *nq);
static uint32_t mn_downs(void);
static uint32_t mn_ups(void);
static VkDeviceSize mn_dump_extra(void);
static void mn_dump_write(void);
static void mn_xt_config(void);      /* SOMVR_XR_TEST: the test hands (headless boots) */
static uint32_t mn_xt_buttons(int h);
static void mn_xt_apply(void);
static void th_wrap(void);           /* SOMVR_XR_TEST `head X Y Z`: the test head (somvr_testhead_layer.c) */
static int th_xt(const char *line);

/* ---- OpenXR state (one XR instance per process; all fields under X.mx) ------------------------------------------ */
#define XR_FUNCS(X)                                                                                                    \
    X(xrDestroyInstance) X(xrGetSystem) X(xrGetVulkanInstanceExtensionsKHR) X(xrGetVulkanDeviceExtensionsKHR)          \
    X(xrGetVulkanGraphicsDeviceKHR) X(xrGetVulkanGraphicsRequirementsKHR) X(xrEnumerateViewConfigurationViews)       \
    X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession) X(xrPollEvent)                          \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrEnumerateEnvironmentBlendModes) X(xrEnumerateSwapchainFormats)   \
    X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage)              \
    X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews) \
    X(xrEnumerateReferenceSpaces) /* input (all core 1.0): */ X(xrStringToPath) X(xrPathToString)                    \
    X(xrCreateActionSet) X(xrDestroyActionSet) X(xrCreateAction) X(xrSuggestInteractionProfileBindings)              \
    X(xrCreateActionSpace) X(xrAttachSessionActionSets) X(xrSyncActions) X(xrGetActionStateBoolean)                  \
    X(xrGetActionStateFloat) X(xrGetActionStateVector2f) X(xrGetActionStatePose) X(xrGetCurrentInteractionProfile)   \
    X(xrLocateSpace) X(xrApplyHapticFeedback) X(xrStopHapticFeedback)

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
    uint32_t max_layers;         /* build 8: the system's maxLayerCount (xrGetSystemProperties; else 16) */
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
    VkFormat sc_game_fmt, sc_fmt; /* the source format the swapchain was made for (game or eye images), its format */
    uint32_t sc_w, sc_h, nimg;
    uint64_t n_sc;               /* XR swapchains made so far (the projection/mono one; SOMVR_XR_DUMP's sN) */
    VkImage img[MAX_XR_IMAGES];
    somvr_xfer xfer;
    uint64_t frames, copies, nolayer;
    int warned_queue;
    uint32_t rec_w, rec_h;       /* recommended per-eye size */
    /* the per-eye path */
    somvr_shared *sh;            /* the preload's struct, NULL = none or inactive: mono only */
    XrReferenceSpaceType space_type;
    uint32_t nslots;             /* XR swapchain = nslots x the game image width (2 for layout B) */
    XrFrameState fs;             /* of the last xrWaitFrame */
    int waited;                  /* fs is waited for and not begun yet */
    int frame_open;              /* xrBeginFrame done, xrEndFrame not yet (layout B: across presents) */
    int img_acquired;
    uint32_t xi;                 /* the acquired XR image */
    uint64_t open_frame;         /* the record of the open stereo frame */
    uint32_t open_mask, open_copied;
    somvr_rec open_rec;
    int last_stereo;             /* the swapchain's last released image is the stereo frame of last_pv */
    XrCompositionLayerProjectionView last_pv[2];
    uint64_t tag_last;           /* last present tag consumed */
    uint64_t n_stereo, n_reuse, n_mono;
    uint64_t n_eyes, n_noeyes;   /* layout B frames from the eye images / without them */
    VkFormat eyes_bad_fmt;       /* an eye format the runtime cannot take (logged once) */
    char noeyes_why[256];        /* the last reason a layout B frame had no eye images (logged when it changes) */
    somvr_lead lead;             /* SOMVR_XR_PREDICT: what was published per xrWaitFrame, the measured lead */
    int64_t lead_logged;         /* the estimate last logged (-1: none yet) */
    uint32_t lead_lines;
    /* the HUD quad (H11, docs/re-xr-pipeline.md 8) */
    somvr_hud_shared *hud;       /* the preload's somvr_hud (active, layout B, SOMVR_XR_HUD not off), NULL = no quad */
    int hud_on;                  /* this session may add the quad (hud_off clears it, logged once) */
    XrSwapchain hud_sc;          /* the HUD image's own XR swapchain (its size, an sRGB format, raw copy) */
    VkFormat hud_src_fmt, hud_fmt;
    uint32_t hud_w, hud_h, hud_nimg, hud_xi;
    VkImage hud_img[MAX_XR_IMAGES];
    int hud_acquired;            /* 1 acquired, 2 also waited */
    XrSpace view_space;          /* SOMVR_XR_HUD=head: a VIEW reference space, made with the first quad */
    XrSpace floor_space;         /* SOMVR_XR_FLOOR: a STAGE space next to the LOCAL one (made once, log only) */
    int floor_done;              /* the floor line of this session was written (or not possible) */
    uint64_t floor_frames;       /* waits counted toward it */
    int hud_latched;             /* world: hud_pose is placed (cleared by a recentre: placed again) */
    XrPosef hud_pose;
    int last_hud;                /* the last stereo frame ended with the quad too (its image released): reuse repeats it */
    XrCompositionLayerQuad last_quad;
    uint64_t n_hud, n_nohud;     /* stereo frames from the eye images with / without the quad */
    char nohud_why[256];         /* the last reason a stereo frame had no quad (logged when it changes) */
    /* the laser pointer (docs/re-xr-pipeline.md 10, ../somvr_pointer.h) */
    int ptr_on;                  /* this session runs the pointer (somvr_input v2, want POINTER, SOMVR_XR_POINTER) */
    int ptr_hud;                 /* ... on the world-locked HUD quad too (want PTR_HUD) */
    int ptr_draw;                /* the dot (and ray) may be drawn: cleared for the session by any failure (ptr_off) */
    XrSwapchain ptr_sc;          /* the pointer image (SOMVR_PTR_IMG_W x _H), filled once */
    int ptr_filled;              /* its image is filled and released */
    VkFormat ptr_fmt;
    somvr_ptr_cfg ptr_cfg;
    somvr_ptr_state ptr;
    XrCompositionLayerQuad ptr_q[2]; /* this frame's ray and dot layers */
    uint32_t ptr_nq;             /* how many of them the next xrEndFrame carries (after the frame's own layers) */
    uint32_t ptr_lines;          /* event lines logged */
    uint64_t n_ptr, n_ptr_on, n_ptr_drawn, n_ptr_clicks;
    /* the comfort fade (docs/re-xr-pipeline.md 6.8, ./somvr_fade.h) */
    int fade_on;                 /* this session draws the records' fade (an active somvr_shared, SOMVR_XR_FADE on) */
    XrSwapchain fade_sc;         /* the ramp image (SOMVR_FADE_W x _H), filled once */
    int fade_filled;
    XrCompositionLayerQuad fade_q; /* this frame's black quad */
    uint32_t fade_n;             /* 1: the next xrEndFrame carries it, after the frame's own layers */
    uint32_t last_fade;          /* the last stereo frame's level (a reuse repeats it) */
    int last_fade_red;           /* ... in red (SOMVR_REC_FADE_RED: SOMVR_BODYMOMENTS' knockdown dim) */
    somvr_posef last_fade_eye[2];
    uint64_t n_fade, n_fade_black;
} X = {.mx = PTHREAD_MUTEX_INITIALIZER};

/* One frame's composition (docs/re-xr-pipeline.md 16), bottom to top: the SoM loading environment's layers (mono and
 * hold frames: the sky - one equirect2 layer or six quads -, world mode's kept frame and its two dim quads: XL_ENV_N =
 * 9 slots), the frame's own layer (the projection, or the mono frame's quad/projection), the threat glows (SOMVR_THREAT:
 * world-locked at the attackers, under the vignette like the world), comfort's vignette ring (over the world, under the
 * HUD and the pointer: the HUD stays readable while moving), the threat edge bands (over the ring: a warning is never
 * vignetted away), the HUD quad, the pointer's ray and dot, the VR menu's panel, its ray and dot (the near wrist panel
 * over the far HUD; the game's pointer has no target while it is open), comfort's snap blink and the fallback's fade
 * (the blacks over everything, the fade last). XL_N = 24 slots; at most 16 at once, the 16 every runtime takes
 * (XR_MIN_COMPOSITION_LAYERS_SUPPORTED): the environment's come only in mono and hold frames, without the threat cues,
 * the ring, the HUD and the blink. A part the runtime rejects goes off alone (xr_end_comp). Build 8's prompt band
 * (XL_BAND) makes 17 slots from the projection on: a frame that would carry more than 16 goes without it (xl_cap). */
#define XL_ENV_N 9
enum { XL_ENV0, XL_OWN = XL_ENV0 + XL_ENV_N, XL_THG0, XL_THG1, XL_THG2, XL_RING, XL_THE0, XL_THE1,
       XL_MBD, XL_MBK, /* SOMVR_XR_MENU_BACK: the world's dim and the dark panel behind a game menu's board */
       XL_HUD,
       XL_BAND, /* build 8: the prompt band (a part of the HUD: off with it; the first to go beyond 16 layers) */
       XL_MMAP, /* build 6: the wrist minimap (a part of the HUD: off with it) */
       XL_PTR0, XL_PTR1, XL_VM0, XL_VM1, XL_VM2,
       XL_HANDS, /* build 10: our own VR hands (a projection layer over the wrist panel and the watch) */
       XL_BLINK, XL_FADE, XL_N };
enum { XG_FADE = 1u << 0, XG_CF = 1u << 1, XG_PTR = 1u << 2, XG_HUD = 1u << 3, XG_ENV = 1u << 4, XG_VM = 1u << 5,
       XG_TH = 1u << 6, XG_VH = 1u << 7, XG_MB = 1u << 8 };
_Static_assert(XL_ENV_N + 1 + 2 + 3 + 1 <= 16, "a mono frame's worst case: env, quad, pointer, VR menu, fade");
_Static_assert(XL_N - XL_OWN <= 20, "a stereo frame's worst case: every slot from the projection on, less the band, "
                                    "the hands and the menu backdrop's two (xl_cap)");
#define XL_MAX 16 /* XR_MIN_COMPOSITION_LAYERS_SUPPORTED: a frame with more loses the band (xl_cap) */
_Static_assert(XL_THG2 - XL_THG0 + 1 == SOMVR_TH_GLOWS && XL_THE1 - XL_THE0 == 1, "the threat slots");
typedef struct {
    const XrCompositionLayerBaseHeader *l[XL_N]; /* NULL = not in this frame; no XL_OWN = a frame without layers */
} xr_comp;
static uint32_t xl_group(int i) {
    if (i < XL_OWN) return XG_ENV;
    if (i >= XL_VM0 && i <= XL_VM2) return XG_VM;
    if (i == XL_HANDS) return XG_VH;
    if (i == XL_MBD || i == XL_MBK) return XG_MB;
    if ((i >= XL_THG0 && i <= XL_THG2) || i == XL_THE0 || i == XL_THE1) return XG_TH;
    return i == XL_FADE ? XG_FADE : i == XL_RING || i == XL_BLINK ? XG_CF : i == XL_PTR0 || i == XL_PTR1 ? XG_PTR
                                                                        : i == XL_HUD || i == XL_BAND || i == XL_MMAP ? XG_HUD : 0u;
}

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

/* ---- SoM loading env: the switch and the instance extension (docs/re-xr-pipeline.md 14; the region "SoM loading
 * env" follows comfort's) ---------------------------------------------------------------------------------------- */
static struct {
    int mode, bad, dim_bad;      /* SOMVR_XR_ENV (SOMVR_ENV_*), not understood; SOMVR_XR_ENV_DIM not understood */
    float dim;                   /* SOMVR_XR_ENV_DIM: the kept frame's brightness (0..1) */
    int offered, equirect;       /* the runtime lists XR_KHR_composition_layer_equirect2; the instance has it enabled */
} ENVC;
/* the helper thread, before xrCreateInstance (nothing reads ENVC before init_done) */
static void env_config(const XrExtensionProperties *props, uint32_t n) {
    ENVC.mode = somvr_env_parse(getenv("SOMVR_XR_ENV"), &ENVC.bad);
    ENVC.dim = somvr_env_parse_dim(getenv("SOMVR_XR_ENV_DIM"), &ENVC.dim_bad);
    ENVC.offered = 0;
    for (uint32_t i = 0; props && i < n; i++)
        ENVC.offered |= !strcmp(props[i].extensionName, XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME);
    ENVC.equirect = ENVC.mode != SOMVR_ENV_OFF && ENVC.offered;
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
    env_config(XR_SUCCEEDED(r) ? props : NULL, props ? n : 0); /* SoM loading env: SOMVR_XR_ENV, the equirect extension */
    free(props);
    if (XR_FAILED(r) || !have_vk) {
        lg("xr: runtime extensions: %s, XR_KHR_vulkan_enable %s", xr_str(r), have_vk ? "yes" : "missing");
        return -1;
    }
    const char *ext[2] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME};
    XrInstanceCreateInfo ici = {.type = XR_TYPE_INSTANCE_CREATE_INFO,
                                .applicationInfo = {.applicationName = "ShadowOfMordorVR",
                                                    .applicationVersion = 1,
                                                    .engineName = "SoMVR vklayer",
                                                    .engineVersion = 1,
                                                    .apiVersion = XR_API_VERSION_1_0},
                                .enabledExtensionCount = ENVC.equirect ? 2u : 1u, /* SoM loading env */
                                .enabledExtensionNames = ext};
    r = create(&ici, &X.inst);
    if (XR_FAILED(r) && ENVC.equirect) { /* SoM loading env: the runtime refused the equirect extension: without it */
        lg("xr: env: xrCreateInstance with %s: %s (%d); again without it (the quad fallback)",
           XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME, xr_str(r), r);
        ENVC.equirect = 0;
        ici.enabledExtensionCount = 1;
        r = create(&ici, &X.inst);
    }
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
    th_wrap(); /* SOMVR_XR_TEST: the views through the test head (headless tests only) */
    XrSystemGetInfo sgi = {.type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    if (XR_FAILED(r = X.xrGetSystem(X.inst, &sgi, &X.sys))) {
        lg("xr: xrGetSystem(HMD): %s (headset not connected?)", xr_str(r));
        return -1;
    }
    { /* build 8: the composition's layer limit (the spec's 16 when the runtime does not say) */
        PFN_xrGetSystemProperties gsp = NULL;
        XrSystemProperties sp = {.type = XR_TYPE_SYSTEM_PROPERTIES};
        X.max_layers = XL_MAX;
        if (XR_SUCCEEDED(X.gipa(X.inst, "xrGetSystemProperties", (PFN_xrVoidFunction *)&gsp)) && gsp &&
            XR_SUCCEEDED(r = gsp(X.inst, X.sys, &sp)) && sp.graphicsProperties.maxLayerCount > 0)
            X.max_layers = sp.graphicsProperties.maxLayerCount;
        lg("xr: system \"%s\": at most %u composition layers%s", gsp ? sp.systemName : "?", X.max_layers,
           X.max_layers < XL_MAX ? " (fewer than the spec's 16: frames with more may be rejected)" : "");
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
                                                          &n, vv)) && n == 2) {
        X.rec_w = vv[0].recommendedImageRectWidth, X.rec_h = vv[0].recommendedImageRectHeight;
        lg("xr: stereo views, recommended %ux%u per eye (max %ux%u)", vv[0].recommendedImageRectWidth,
           vv[0].recommendedImageRectHeight, vv[0].maxImageRectWidth, vv[0].maxImageRectHeight);
    }
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
        cv_slot_done(sl, idle); /* SOMVR_XR_COVER: its readings (dropped when it is not idle) */
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

/* ---- OpenXR input (docs/re-xr-input.md section 1; everything under X.mx, on the present thread) ---------------- */
enum { IW_SYNC = 1u, IW_STATE = 2u, IW_LOCATE = 4u, IW_PROFILE = 8u, IW_HAPTIC = 16u }; /* error kinds, logged once */
static struct {
    somvr_input_shared *in;  /* the preload's object (active, want has STATE); NULL = none (log mode may still run) */
    int on;                  /* the action set is attached to X.session: syncs, state reads, haptics */
    int focused;             /* the last session state was FOCUSED */
    XrActionSet set;
    XrAction act[SOMVR_NACT];
    XrPath hand[2];
    XrSpace grip[2], aim[2];
    uint32_t profiles;       /* bit p: the runtime took SOMVR_PROF_p's suggestions */
    somvr_input_state st;    /* the snapshot (sync_seq and the press counters are never reset) */
    uint32_t hap_last[2];    /* the last haptic request number taken, per hand */
    uint64_t n_sync, n_hap, n_logged;
    XrResult last_sync;
    uint32_t warned;         /* IW_* already logged */
} I;
static const char *const hand_path[2] = {"/user/hand/left", "/user/hand/right"};
static const char hand_ch[2] = {'L', 'R'};

static void in_warn(uint32_t kind, const char *what, XrResult r) {
    if (I.warned & kind) return;
    I.warned |= kind;
    lg("xr: input: %s: %s (%d) (logged once; input goes on)", what, xr_str(r), r);
}

/* no live input: buttons up, values 0, nothing active (poses, press counters and sync_seq stay) */
static void in_clear(void) {
    I.st.flags = 0;
    for (int h = 0; h < 2; h++) {
        somvr_hand_in *hi = &I.st.hand[h];
        hi->buttons = 0;
        hi->trigger = hi->squeeze = hi->stick[0] = hi->stick[1] = 0;
        memset(hi->lin_vel, 0, sizeof hi->lin_vel);
        memset(hi->ang_vel, 0, sizeof hi->ang_vel);
        hi->grip_flags = hi->aim_flags = hi->vel_flags = hi->active = 0;
    }
}
static void in_publish(void) {
    if (I.in) somvr_input_publish(I.in, &I.st);
}

/* Destroy whatever input made (X.mx held; the core guard is quiet: no game-thread locate runs). quiet = 0: a provider
 * call is still in flight, so the spaces and the set are left alive (as the session is). */
static void in_destroy(int quiet) {
    if (I.in) atomic_store_explicit(&I.in->xr.ready, 0, memory_order_release);
    if (I.on || I.set) {
        in_clear();
        for (int h = 0; h < 2; h++) I.st.profile[h] = SOMVR_PROF_NONE;
        in_publish();
    }
    for (int h = 0; h < 2; h++) {
        if (quiet && I.grip[h]) X.xrDestroySpace(I.grip[h]);
        if (quiet && I.aim[h]) X.xrDestroySpace(I.aim[h]);
        I.grip[h] = I.aim[h] = XR_NULL_HANDLE;
    }
    if (quiet && I.set) X.xrDestroyActionSet(I.set); /* its actions go with it */
    I.set = XR_NULL_HANDLE;
    memset(I.act, 0, sizeof I.act);
    I.on = I.focused = 0;
    I.profiles = 0;
}

/* a setup error: logged, everything input made is destroyed, XR goes on without input. Returns -1. */
static int in_off(const char *call, XrResult r) {
    lg("xr: input off: %s %s (%d)", call, xr_str(r), r);
    in_destroy(1);
    return -1;
}

/* 1.4 step 2, before xrCreateSession: the paths, the action set, the 12 actions, one suggestion per profile. */
static int in_create(void) {
    XrResult r;
    for (int h = 0; h < 2; h++)
        if (XR_FAILED(r = X.xrStringToPath(X.inst, hand_path[h], &I.hand[h]))) return in_off("xrStringToPath", r);
    XrActionSetCreateInfo si = {.type = XR_TYPE_ACTION_SET_CREATE_INFO, .priority = 0};
    snprintf(si.actionSetName, sizeof si.actionSetName, "somvr");
    snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "Shadow of Mordor VR");
    if (XR_FAILED(r = X.xrCreateActionSet(X.inst, &si, &I.set))) {
        I.set = XR_NULL_HANDLE;
        return in_off("xrCreateActionSet", r);
    }
    for (int a = 0; a < SOMVR_NACT; a++) {
        const somvr_action_def *d = &somvr_actions[a];
        XrPath sub[2];
        uint32_t ns = 0;
        for (int h = 0; h < 2; h++)
            if (d->hands >> h & 1u) sub[ns++] = I.hand[h];
        XrActionCreateInfo ci = {.type = XR_TYPE_ACTION_CREATE_INFO, .actionType = (XrActionType)d->type,
                                 .countSubactionPaths = ns, .subactionPaths = sub};
        snprintf(ci.actionName, sizeof ci.actionName, "%s", d->name);
        snprintf(ci.localizedActionName, sizeof ci.localizedActionName, "%s", d->loc);
        if (XR_FAILED(r = X.xrCreateAction(I.set, &ci, &I.act[a]))) {
            I.act[a] = XR_NULL_HANDLE;
            return in_off("xrCreateAction", r);
        }
    }
    char line[256] = "";
    size_t u = 0;
    for (int p = 0; p < SOMVR_NPROF; p++) {
        const somvr_profile_def *pd = &somvr_profiles[p];
        XrActionSuggestedBinding sb[40];
        XrPath prof = XR_NULL_PATH;
        r = pd->n <= 40 ? X.xrStringToPath(X.inst, pd->path, &prof) : XR_ERROR_SIZE_INSUFFICIENT;
        for (uint32_t i = 0; XR_SUCCEEDED(r) && i < pd->n; i++) {
            char full[XR_MAX_PATH_LENGTH];
            snprintf(full, sizeof full, "%s/%s", hand_path[pd->b[i].hand], pd->b[i].path);
            sb[i].action = I.act[pd->b[i].act];
            r = X.xrStringToPath(X.inst, full, &sb[i].binding);
        }
        if (XR_SUCCEEDED(r)) {
            XrInteractionProfileSuggestedBinding s = {.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
                                                      .interactionProfile = prof,
                                                      .countSuggestedBindings = pd->n,
                                                      .suggestedBindings = sb};
            r = X.xrSuggestInteractionProfileBindings(X.inst, &s);
        }
        if (XR_SUCCEEDED(r)) I.profiles |= 1u << pd->prof;
        if (u < sizeof line)
            u += (size_t)snprintf(line + u, sizeof line - u, "%s%s=%s", p ? " " : "", pd->name,
                                  XR_SUCCEEDED(r) ? "ok" : xr_str(r));
    }
    lg("xr: input: action set somvr, %d actions; profiles %s", SOMVR_NACT, line);
    if (!I.profiles) return in_off("xrSuggestInteractionProfileBindings (no profile taken)", r);
    return 0;
}

/* 1.4 steps 4-5, after the reference space: grip and aim spaces per hand, then the attach (once per session). */
static int in_attach(void) {
    XrResult r;
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 2; k++) {
            XrActionSpaceCreateInfo ci = {.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
                                          .action = I.act[k ? SOMVR_ACT_AIM : SOMVR_ACT_GRIP],
                                          .subactionPath = I.hand[h],
                                          .poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}}};
            XrSpace *sp = k ? &I.aim[h] : &I.grip[h];
            if (XR_FAILED(r = X.xrCreateActionSpace(X.session, &ci, sp))) {
                *sp = XR_NULL_HANDLE;
                return in_off("xrCreateActionSpace", r);
            }
        }
    XrSessionActionSetsAttachInfo ai = {.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, .countActionSets = 1,
                                        .actionSets = &I.set};
    if (XR_FAILED(r = X.xrAttachSessionActionSets(X.session, &ai))) return in_off("xrAttachSessionActionSets", r);
    I.on = 1;
    I.last_sync = XR_SUCCESS;
    lg("xr: input: 4 action spaces, attached (%s)", I.in ? "somvr_input" : "log only, no somvr_input");
    return 0;
}

/* SOMVR_SA_BEGIN, before somvr_shared xr.state = RUNNING: requests posted so far are stale; the spaces for the game
 * thread's locate (want SPACES, and only with somvr_shared's guard), then ready = 1 (release). */
static void in_begin(void) {
    if (!I.on || !I.in) return;
    somvr_haptic_req stale;
    for (int h = 0; h < 2; h++) somvr_haptic_take(I.in, h, &I.hap_last[h], &stale);
    I.in->xr.profiles = I.profiles;
    if (!X.sh || !(I.in->want & SOMVR_INW_SPACES)) return;
    for (int h = 0; h < 2; h++) {
        I.in->xr.grip_space[h] = (uint64_t)(uintptr_t)I.grip[h];
        I.in->xr.aim_space[h] = (uint64_t)(uintptr_t)I.aim[h];
    }
    I.in->xr.locate_space = (void *)X.xrLocateSpace;
    I.in->xr.locate_spaces = NULL;
    atomic_store_explicit(&I.in->xr.ready, 1, memory_order_release);
    lg("xr: input: published to somvr_input: grip/aim spaces L/R, xrLocateSpace (ready)");
}

/* XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: the profile per hand (into the next snapshot) */
static void in_profiles(void) {
    char name[2][XR_MAX_PATH_LENGTH];
    for (int h = 0; h < 2; h++) {
        XrInteractionProfileState ps = {.type = XR_TYPE_INTERACTION_PROFILE_STATE};
        uint32_t n = 0;
        name[h][0] = 0;
        XrResult r = X.xrGetCurrentInteractionProfile(X.session, I.hand[h], &ps);
        if (XR_SUCCEEDED(r) && ps.interactionProfile != XR_NULL_PATH)
            r = X.xrPathToString(X.inst, ps.interactionProfile, sizeof name[h], &n, name[h]);
        if (XR_FAILED(r)) {
            in_warn(IW_PROFILE, "xrGetCurrentInteractionProfile/xrPathToString", r);
            name[h][0] = 0;
        }
        name[h][sizeof name[h] - 1] = 0;
        I.st.profile[h] = (uint16_t)somvr_prof_from_path(name[h]);
    }
    lg("xr: input: profile L=%s R=%s", name[0][0] ? name[0] : "none", name[1][0] ? name[1] : "none");
}

static int in_bool(XrAction a, XrPath sub, int *active, uint32_t *reads) {
    XrActionStateGetInfo gi = {.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = a, .subactionPath = sub};
    XrActionStateBoolean s = {.type = XR_TYPE_ACTION_STATE_BOOLEAN};
    XrResult r = X.xrGetActionStateBoolean(X.session, &gi, &s);
    ++*reads;
    if (XR_FAILED(r)) return in_warn(IW_STATE, "xrGetActionStateBoolean", r), 0;
    *active |= s.isActive != XR_FALSE;
    return s.isActive && s.currentState;
}
static float in_float(XrAction a, XrPath sub, int *active, uint32_t *reads) {
    XrActionStateGetInfo gi = {.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = a, .subactionPath = sub};
    XrActionStateFloat s = {.type = XR_TYPE_ACTION_STATE_FLOAT};
    XrResult r = X.xrGetActionStateFloat(X.session, &gi, &s);
    ++*reads;
    if (XR_FAILED(r)) return in_warn(IW_STATE, "xrGetActionStateFloat", r), 0.0f;
    *active |= s.isActive != XR_FALSE;
    return s.isActive ? s.currentState : 0.0f;
}

/* one hand's space at the snapshot's time: the parts the runtime marks valid are copied, the others keep the last value */
static int in_locate(XrSpace sp, somvr_posef *pose, uint8_t *flags, somvr_hand_in *vel_to) {
    XrSpaceVelocity v = {.type = XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation loc = {.type = XR_TYPE_SPACE_LOCATION, .next = vel_to ? &v : NULL};
    XrResult r = X.xrLocateSpace(sp, X.space, I.st.time, &loc);
    if (XR_FAILED(r)) {
        in_warn(IW_LOCATE, "xrLocateSpace (snapshot)", r);
        *flags = 0;
        if (vel_to) vel_to->vel_flags = 0;
        return 0;
    }
    if (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) memcpy(pose->q, &loc.pose.orientation, 16);
    if (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) memcpy(pose->p, &loc.pose.position, 12);
    *flags = (uint8_t)(loc.locationFlags & 0xfu);
    if (vel_to) {
        vel_to->vel_flags = (uint8_t)(v.velocityFlags & 3u);
        if (v.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) memcpy(vel_to->lin_vel, &v.linearVelocity, 12);
        else memset(vel_to->lin_vel, 0, 12);
        if (v.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) memcpy(vel_to->ang_vel, &v.angularVelocity, 12);
        else memset(vel_to->ang_vel, 0, 12);
    }
    return 1;
}

static void in_log_hand(int h, uint32_t prev) {
    const somvr_hand_in *hi = &I.st.hand[h];
    lg("xr: input: %c btn 0x%x->0x%x presses %u,%u,%u,%u,%u,%u trig %.2f sq %.2f stick %+.2f,%+.2f grip %+.3f,%+.3f,"
       "%+.3f flags 0x%x vel 0x%x active 0x%x profile %u (sync %llu)", hand_ch[h], prev, hi->buttons, hi->presses[0],
       hi->presses[1], hi->presses[2], hi->presses[3], hi->presses[4], hi->presses[5], hi->trigger, hi->squeeze,
       hi->stick[0], hi->stick[1], hi->grip.p[0], hi->grip.p[1], hi->grip.p[2], hi->grip_flags, hi->vel_flags,
       hi->active, I.st.profile[h], (unsigned long long)I.st.sync_seq);
}

/* right after xrWaitFrame (X.fs): sync, read, locate at the predicted display time, publish one snapshot */
static void in_sync(void) {
    XrActiveActionSet as = {I.set, XR_NULL_PATH};
    XrActionsSyncInfo si = {.type = XR_TYPE_ACTIONS_SYNC_INFO, .countActiveActionSets = 1, .activeActionSets = &as};
    XrResult r = X.xrSyncActions(X.session, &si);
    I.n_sync++;
    if (r != I.last_sync || I.n_sync <= 3 || I.n_sync % 600 == 0)
        lg("xr: input: sync n=%llu focused=%d result=%d (%s)", (unsigned long long)I.n_sync, I.focused, r, xr_str(r));
    I.last_sync = r;
    if (XR_FAILED(r)) { /* as not focused: nothing live; the counter stays */
        in_warn(IW_SYNC, "xrSyncActions", r);
        in_clear();
        in_publish();
        return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    I.st.sync_seq++;
    I.st.time = X.fs.predictedDisplayTime;
    I.st.sync_ns = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
    if (r != XR_SUCCESS) { /* XR_SESSION_NOT_FOCUSED: every action is inactive */
        in_clear();
        if (I.in) atomic_fetch_add_explicit(&I.in->n_unfocused, 1, memory_order_relaxed);
        in_publish();
        return;
    }
    uint32_t reads = 0, prev[2];
    int located = 1;
    for (int h = 0; h < 2; h++) {
        somvr_hand_in *hi = &I.st.hand[h];
        XrPath sub = I.hand[h];
        int pose_on = 0, in_on = 0;
        XrActionStateGetInfo gi = {.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = I.act[SOMVR_ACT_GRIP],
                                   .subactionPath = sub};
        XrActionStatePose ps = {.type = XR_TYPE_ACTION_STATE_POSE};
        XrResult pr = X.xrGetActionStatePose(X.session, &gi, &ps);
        reads++;
        if (XR_FAILED(pr)) in_warn(IW_STATE, "xrGetActionStatePose", pr);
        else pose_on = ps.isActive != XR_FALSE;
        hi->trigger = in_float(I.act[SOMVR_ACT_TRIGGER], sub, &in_on, &reads);
        hi->squeeze = in_float(I.act[SOMVR_ACT_SQUEEZE], sub, &in_on, &reads);
        gi.action = I.act[SOMVR_ACT_STICK];
        XrActionStateVector2f vs = {.type = XR_TYPE_ACTION_STATE_VECTOR2F};
        XrResult vr = X.xrGetActionStateVector2f(X.session, &gi, &vs);
        reads++;
        if (XR_FAILED(vr)) in_warn(IW_STATE, "xrGetActionStateVector2f", vr);
        in_on |= XR_SUCCEEDED(vr) && vs.isActive;
        hi->stick[0] = XR_SUCCEEDED(vr) && vs.isActive ? vs.currentState.x : 0.0f;
        hi->stick[1] = XR_SUCCEEDED(vr) && vs.isActive ? vs.currentState.y : 0.0f;
        uint32_t btn = 0;
        for (int a = 0; a < SOMVR_NACT; a++)
            if (somvr_actions[a].button && (somvr_actions[a].hands >> h & 1u) &&
                in_bool(I.act[a], sub, &in_on, &reads))
                btn |= somvr_actions[a].button;
        btn |= mn_xt_buttons(h); /* SOMVR_XR_TEST: the held test buttons */
        prev[h] = hi->buttons;
        hi->buttons = somvr_in_merge(hi->presses, hi->buttons, btn);
        hi->active = (uint8_t)((pose_on ? SOMVR_HACT_POSE : 0) | (in_on ? SOMVR_HACT_INPUT : 0));
        located &= in_locate(I.grip[h], &hi->grip, &hi->grip_flags, hi);
        located &= in_locate(I.aim[h], &hi->aim, &hi->aim_flags, NULL);
    }
    mn_xt_apply(); /* SOMVR_XR_TEST: the test hands over the runtime's */
    I.st.flags = (I.focused ? SOMVR_INS_FOCUSED : 0u) | SOMVR_INS_SYNCED | (located ? SOMVR_INS_LOCATED : 0u);
    if (I.in) {
        atomic_fetch_add_explicit(&I.in->n_syncs, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&I.in->n_state_reads, reads, memory_order_relaxed);
    }
    in_publish();
    if (cfg.input == SOMVR_INPUT_LOG)
        for (int h = 0; h < 2; h++)
            if ((prev[h] != I.st.hand[h].buttons && I.n_logged++ < 2000) || I.n_sync % 600 == 0) in_log_hand(h, prev[h]);
}

/* each present, after xrPollEvent: a new request number per hand = one apply (or stop) */
static void in_haptics(void) {
    if (!I.on || !I.in || !(I.in->want & SOMVR_INW_HAPTICS)) return;
    static const char *const src[] = {"none", "combat", "rumble", "ui", "test"};
    for (int h = 0; h < 2; h++) {
        somvr_haptic_req q;
        if (!somvr_haptic_take(I.in, h, &I.hap_last[h], &q)) continue;
        float amp, freq;
        int64_t dur;
        XrHapticActionInfo hi = {.type = XR_TYPE_HAPTIC_ACTION_INFO, .action = I.act[SOMVR_ACT_HAPTIC],
                                 .subactionPath = I.hand[h]};
        XrResult r;
        int stop = somvr_hap_decide(&q, &amp, &freq, &dur) == SOMVR_HAPD_STOP;
        if (stop) {
            r = X.xrStopHapticFeedback(X.session, &hi);
            if (XR_SUCCEEDED(r)) atomic_fetch_add_explicit(&I.in->n_hap_stopped, 1, memory_order_relaxed);
        } else {
            XrHapticVibration v = {.type = XR_TYPE_HAPTIC_VIBRATION, .duration = dur, .frequency = freq,
                                   .amplitude = amp};
            r = X.xrApplyHapticFeedback(X.session, &hi, (const XrHapticBaseHeader *)&v);
            if (XR_SUCCEEDED(r)) atomic_fetch_add_explicit(&I.in->n_hap_applied, 1, memory_order_relaxed);
        }
        if (XR_FAILED(r)) in_warn(IW_HAPTIC, stop ? "xrStopHapticFeedback" : "xrApplyHapticFeedback", r);
        if (++I.n_hap <= 10 || I.n_hap % 600 == 0) {
            char d[32];
            if (dur > 0) snprintf(d, sizeof d, "%.0f ms", (double)dur / 1e6);
            else snprintf(d, sizeof d, "min");
            if (stop)
                lg("xr: haptic %c n=%u stop -> %s (src %s)", hand_ch[h], q.n, xr_str(r), src[q.source < 5 ? q.source : 0]);
            else
                lg("xr: haptic %c n=%u amp %.2f %s -> %s (src %s, %.0f Hz)", hand_ch[h], q.n, amp, d, xr_str(r),
                   src[q.source < 5 ? q.source : 0], freq);
        }
    }
}

static void vm_setup(void);   /* the VR settings menu (its region follows comfort's, before the XR frame's) */
static void vm_destroy(void);
static int vm_hud_shown(void);         /* ... the HUD quad shown (its HUD switch) */
static void vm_hud_geom(float out[3]); /* ... the HUD quad's distance, width and drop */
static int vm_comfort_live(void);      /* ... comfort may come on later (the menu's vignette and blink) */
static float vm_vignette(float obj);   /* ... the vignette's strength */

/* ---- the HUD quad (H11, docs/re-xr-pipeline.md 8; X.mx held) ---------------------------------------------------- */
/* its swapchain goes (wait: our copies may still write into it first) */
static void hud_sc_destroy(int wait) {
    if (X.hud_sc) {
        if (wait && X.ds) sc_wait_all(X.ds);
        X.xrDestroySwapchain(X.hud_sc);
    }
    X.hud_sc = XR_NULL_HANDLE;
    X.hud_nimg = 0;
    X.hud_acquired = 0;
    X.last_hud = 0; /* no released quad image to repeat */
}
/* ... and its VIEW space */
static void hud_destroy(int wait) {
    hud_sc_destroy(wait);
    if (X.view_space) X.xrDestroySpace(X.view_space);
    X.view_space = XR_NULL_HANDLE;
}

/* no HUD quad for the rest of this session: logged once, the stereo frames go on with the projection alone */
__attribute__((format(printf, 1, 2))) static void hud_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: hud off for this session: %s (stereo frames go on without the HUD quad; %llu quads so far)", why,
       (unsigned long long)X.n_hud);
    X.hud_on = 0;
    hud_destroy(1);
}

/* The HUD image this present names: somvr_hud's entry for the core tag's seq with gen != 0, the info of that
 * generation, the image created by this device at the announced HUD size with TRANSFER_SRC and still alive.
 * 1 = usable (*out filled), 0 = no quad this frame (*why) */
static int hud_lookup(VkDevice dev, const somvr_tag *t, hud_st *out, char *why, size_t cap) {
    somvr_hud_tag ht;
    somvr_hud_info in;
    if (!somvr_hud_tag_get(X.hud, t->seq, &ht))
        return snprintf(why, cap, "no somvr_hud entry for tag %llu", (unsigned long long)t->seq), 0;
    if (!ht.gen) return snprintf(why, cap, "the present names no HUD image"), 0;
    if (!somvr_hud_read(X.hud, &in)) return snprintf(why, cap, "the HUD info is being rewritten"), 0;
    if (in.gen != ht.gen) return snprintf(why, cap, "HUD info gen %u, the present's %u", in.gen, ht.gen), 0;
    *out = (hud_st){.w = in.w, .h = in.h, .gen = in.gen, .kind = ht.kind, .fmt = VK_FORMAT_UNDEFINED};
    pthread_mutex_lock(&g_cand_mx);
    for (int i = 0; in.image && i < MAX_EYE_CAND && !out->img; i++)
        if (g_cand[i].img == (VkImage)(uintptr_t)in.image && g_cand[i].dev == dev && g_cand[i].w == in.w &&
            g_cand[i].h == in.h && (g_cand[i].usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            out->img = g_cand[i].img, out->fmt = g_cand[i].fmt, g_cand[i].used = 1;
    pthread_mutex_unlock(&g_cand_mx);
    if (!out->img)
        return snprintf(why, cap, "HUD vkimage %#llx (%ux%u): %s", (unsigned long long)in.image, in.w, in.h,
                        !in.image ? "not known yet" : "not created by this device at that size"), 0;
    return 1;
}

/* the HUD's XR swapchain fits the image (w x h, an sRGB format with the source's channel order: a raw copy); 0 = it
 * does, -1 = the HUD is off now */
static int hud_swapchain_ensure(VkFormat src, uint32_t w, uint32_t h) {
    if (X.hud_sc && X.hud_w == w && X.hud_h == h && X.hud_src_fmt == src) return 0;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return hud_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    VkFormat f = VK_FORMAT_UNDEFINED;
    int gamma_ok = 0;
    if (somvr_pick_format(src, fmts, n, &f, &gamma_ok) != SOMVR_XFER_COPY)
        return hud_off("no swapchain format the HUD image (format %d) copies into as it is", src), -1;
    hud_sc_destroy(1); /* another size (SetResolution): a new swapchain, at the same place */
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                                              XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = w + mn_hud_extra(w, h), /* builds 6, 8: + the tiles' extra area */
                                .height = h,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &X.hud_sc))) {
        X.hud_sc = XR_NULL_HANDLE;
        return hud_off("xrCreateSwapchain(%ux%u format %d): %s", w, h, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(X.hud_sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return hud_off("xrEnumerateSwapchainImages (HUD): %s, %u images", xr_str(r), n), -1;
    for (uint32_t i = 0; i < n; i++) X.hud_img[i] = im[i].image;
    X.hud_nimg = n, X.hud_w = w, X.hud_h = h, X.hud_src_fmt = src, X.hud_fmt = f;
    lg("xr: hud swapchain %ux%u format %d (%u images) for HUD image format %d: copy%s", ci.width, h, f, n, src,
       gamma_ok ? "" : ", brightness will be off (no matching sRGB format)");
    return 0;
}

/* SOMVR_XR_HUD=head: the VIEW space the quad is locked in; 0 = there is one */
static int hud_view_space(void) {
    if (X.view_space) return 0;
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                     .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
                                     .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
    XrResult r = X.xrCreateReferenceSpace(X.session, &rs, &X.view_space);
    if (XR_FAILED(r)) {
        X.view_space = XR_NULL_HANDLE;
        return hud_off("xrCreateReferenceSpace(VIEW): %s", xr_str(r)), -1;
    }
    lg("xr: hud: VIEW space made (head-locked quad)");
    return 0;
}

static int hud_acquire(void) {
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    XrResult r = X.xrAcquireSwapchainImage(X.hud_sc, &ai, &X.hud_xi);
    if (XR_FAILED(r) || X.hud_xi >= X.hud_nimg)
        return hud_off("xrAcquireSwapchainImage (HUD): %s, image %u of %u", xr_str(r), X.hud_xi, X.hud_nimg), -1;
    X.hud_acquired = 1;
    if ((r = X.xrWaitSwapchainImage(X.hud_sc, &wti)) != XR_SUCCESS)
        return hud_off("xrWaitSwapchainImage (HUD): %s", xr_str(r)), -1;
    X.hud_acquired = 2;
    return 0;
}

static int hud_release(void) {
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    X.hud_acquired = 0;
    XrResult r = X.xrReleaseSwapchainImage(X.hud_sc, &ri);
    if (XR_FAILED(r)) return hud_off("xrReleaseSwapchainImage (HUD): %s", xr_str(r)), -1;
    return 0;
}

/* the quad of a stereo frame: SOMVR_XR_HUD=head in the VIEW space, world at the pose placed from the frame's eyes (the
 * first quad of the session, and the first after a recentre); the whole HUD image, alpha-blended */
static void hud_quad(XrCompositionLayerQuad *q, const somvr_rec *rec) {
    float sz[2], g[3]; /* distance, width, drop: SOMVR_XR_HUD_QUAD's, or the VR menu's HUD distance (scaled) */
    vm_hud_geom(g);
    somvr_quad_size(X.hud_w, X.hud_h, g[1], sz);
    XrPosef pose = {{0, 0, 0, 1}, {0, -g[2], -g[0]}};
    XrSpace sp = X.view_space;
    if (cfg.hud != SOMVR_HUD_HEAD) {
        if (!X.hud_latched) {
            somvr_posef eye[2] = {rec->eye[0].pose, rec->eye[1].pose}, at;
            somvr_hud_world_pose(eye, g[0], g[2], &at);
            memcpy(&X.hud_pose, &at, sizeof X.hud_pose);
            X.hud_latched = 1;
            mn_band_place(eye); /* build 8: the prompt band from the same eyes */
            lg("xr: hud quad placed for frame %llu at %+.3f,%+.3f,%+.3f facing yaw %+.1f deg (%.2f m ahead of the eyes, "
               "%.2f m below them)", (unsigned long long)rec->frame, at.p[0], at.p[1], at.p[2],
               2.0 * atan2(at.q[1], at.q[3]) * 57.29578, g[0], g[2]);
        }
        pose = X.hud_pose;
        sp = X.space;
    }
    *q = (XrCompositionLayerQuad){
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                      (cfg.hud_alpha == SOMVR_HUD_STRAIGHT ? XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT : 0),
        .space = sp,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = {X.hud_sc, {{0, 0}, {(int32_t)X.hud_w, (int32_t)X.hud_h}}, 0},
        .pose = pose,
        .size = {sz[0], sz[1]}};
    mn_hud_quad(q, rec); /* build 6: a game menu on the board in front of the head */
}

static void ptr_setup(void); /* the laser pointer (its region follows the dump's) */
static void ptr_sc_destroy(void);
static void fade_setup(void); /* the comfort fade (its region follows the pointer's) */
static void fade_sc_destroy(void);
static void cf_setup(void);   /* comfort: the vignette and the snap blink (its region follows the fade's) */
static void cf_destroy(void);
static void th_setup(void);   /* the threat cues (their region follows comfort's) */
static void th_destroy(void);
static void env_setup(void);  /* SoM loading env: the sky behind mono frames (its region follows comfort's) */
static void vd_start(void);   /* never a void (./somvr_void_layer.c): the present gaps, the watcher */
static void vd_stop(void);
static void vd_waited(void);
static void vd_ended(void);
static void vd_space_add(dev_st *ds, xr_comp *c, uint32_t n, int stalled, int may_fill); /* the loading space */
static int vd_board_ok(void);
static int vd_env_holdable(void);
static void vd_world_shown(void);
static void vd_floor(float oy);
static int vd_xt(const char *line);
static void vd_hands(dev_st *ds, xr_comp *c, int watcher);
static void vd_theatre(XrCompositionLayerQuad *q, uint32_t w, uint32_t h);
static void cv_cmds(dev_st *ds, VkCommandBuffer cmd, const slot_st *sl, const hud_st *hd); /* SOMVR_XR_COVER */
static void cv_submitted(int ok);
static void cv_poll(dev_st *ds, int mono);
static void cv_destroy(void);
static int cv_hud_hidden(void);
static int cv_space_want(void);
static int cv_space_shown(void);
static void cv_space_up(void);
static void cv_kind(uint32_t kind);
static void cv_space_comp(dev_st *ds, xr_comp *c);
static void mb_frame(dev_st *ds, int menu, const somvr_posef eye[2]); /* SOMVR_XR_MENU_BACK: the menus' backdrop */
static void mb_layers(xr_comp *c);
static void mb_off(const char *why, const char *res);
static void env_destroy(void);

/* ---- XR session ------------------------------------------------------------------------------------------------- */
/* X.mx held. Stop the game thread's xrLocateViews (somvr_shared xr.state; wait until none is in flight). 1 = quiet. */
static int xr_unpublish(uint32_t state) {
    if (!X.sh || atomic_load(&X.sh->xr.state) == state) return 1;
    int quiet = somvr_xr_stop(X.sh, state, 1000);
    lg("xr: somvr_shared xr.state -> %s%s", state == SOMVR_XRS_DEAD ? "dead" : "none",
       quiet ? "" : " - a provider call is still running after 1 s");
    return quiet;
}

/* X.mx held. Wait for our copies, then destroy the XR swapchain, space and session. */
static void xr_teardown_session(void) {
    vd_stop(); /* never a void: the gaps' summary, the watcher joined (before anything goes) */
    int quiet = xr_unpublish(SOMVR_XRS_DEAD);
    in_destroy(quiet); /* somvr_input ready = 0, the 4 action spaces, the action set: before the session */
    if (X.ds) sc_wait_all(X.ds);
    cv_destroy(); /* SOMVR_XR_COVER: the readback buffer (its copies waited) */
    hud_destroy(0); /* the HUD swapchain and VIEW space (ours alone): before the session */
    X.hud_on = X.hud_latched = 0;
    ptr_sc_destroy(); /* the pointer image's swapchain (ours alone): before the session */
    X.ptr_on = 0, X.ptr_nq = 0;
    fade_sc_destroy(); /* the fade image's swapchain (ours alone): before the session */
    X.fade_on = 0, X.fade_n = 0;
    cf_destroy(); /* comfort: the alpha texture's swapchain and its VIEW space (ours alone): before the session */
    th_destroy(); /* the threat cues: their texture's swapchain and VIEW space (ours alone): before the session */
    env_destroy(); /* SoM loading env: the sky's and the kept frame's swapchains (ours alone): before the session */
    vm_destroy(); /* the VR menu: the panel's swapchain, staging buffer, command buffer, fence: before the session */
    vh_destroy(); /* build 10: our hands' swapchain, drawing, command buffers, fences: before the session */
    if (X.sc) X.xrDestroySwapchain(X.sc);
    if (!quiet) {
        lg("xr: leaving the space and the session alive (xrDestroySession is externally synchronised with the caller)");
        X.space = XR_NULL_HANDLE;
        X.floor_space = XR_NULL_HANDLE;
        X.session = XR_NULL_HANDLE;
    }
    if (X.floor_space) X.xrDestroySpace(X.floor_space);
    X.floor_space = XR_NULL_HANDLE;
    X.floor_done = 0, X.floor_frames = 0;
    if (X.space) X.xrDestroySpace(X.space);
    if (X.session) X.xrDestroySession(X.session);
    X.sc = XR_NULL_HANDLE;
    X.space = XR_NULL_HANDLE;
    X.session = XR_NULL_HANDLE;
    X.running = 0;
    X.nimg = 0;
    X.q = VK_NULL_HANDLE;
    X.waited = X.frame_open = X.img_acquired = X.last_stereo = 0;
    X.open_frame = 0;
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
    /* the LD_PRELOAD library's objects: in the global scope if it is loaded (this layer is dlopened RTLD_LOCAL).
     * Lookups only; before the session because the actions and suggestions must precede it (re-xr-input.md 1.4). */
    somvr_shared *sh = (somvr_shared *)dlsym(RTLD_DEFAULT, SOMVR_SHARED_SYMBOL);
    X.sh = somvr_shared_ok(sh) ? sh : NULL;
    atomic_store(&g_sh_seen, X.sh);
    X.nslots = X.sh && X.sh->layout == SOMVR_LAYOUT_B ? 2u : 1u;
    X.tag_last = X.sh ? atomic_load(&X.sh->tag_seq) : 0;
    somvr_lead_init(&X.lead);
    X.lead_logged = -1;
    if (X.sh) {
        lg("xr: somvr_shared found at %p (v%u, preload pid %d, layout %s): per-eye path on, %u slot(s)", (void *)X.sh,
           X.sh->version, X.sh->pid, X.sh->layout == SOMVR_LAYOUT_A ? "A" : X.sh->layout == SOMVR_LAYOUT_B ? "B" : "mono",
           X.nslots);
        if (cfg.predict_bad)
            lg("xr: SOMVR_XR_PREDICT=%s is not auto, off or a lead of 0..250 ms: auto", getenv("SOMVR_XR_PREDICT"));
        lg("xr: prediction %s: the views' time published = predictedDisplayTime + %s (docs/re-xr-pipeline.md 7.1)",
           cfg.predict == SOMVR_PREDICT_OFF ? "off" : cfg.predict == SOMVR_PREDICT_FIXED ? "fixed" : "auto",
           cfg.predict == SOMVR_PREDICT_OFF     ? "0"
           : cfg.predict == SOMVR_PREDICT_FIXED ? getenv("SOMVR_XR_PREDICT")
                                                : "the pipeline lead measured from the submitted records");
    } else
        lg("xr: no active somvr_shared (%s): mono presents only", sh ? "inactive or another version" : "no preload");
    somvr_hud_shared *hud = (somvr_hud_shared *)dlsym(RTLD_DEFAULT, SOMVR_HUD_SYMBOL);
    X.hud = X.nslots == 2 && cfg.hud != SOMVR_HUD_OFF && somvr_hud_ok(hud) ? hud : NULL;
    X.hud_on = X.hud != NULL;
    X.hud_latched = X.last_hud = 0;
    X.n_hud = X.n_nohud = 0;
    X.nohud_why[0] = 0;
    atomic_store(&g_hud_seen, X.hud);
    if (cfg.hud_bad)
        lg("xr: hud: %s%s%snot understood: the defaults (SOMVR_XR_HUD=world|head|off, SOMVR_XR_HUD_ALPHA=straight|premul, "
           "SOMVR_XR_HUD_QUAD=DIST,WIDTH,DROP in metres)", cfg.hud_bad & 1 ? "SOMVR_XR_HUD " : "",
           cfg.hud_bad & 2 ? "SOMVR_XR_HUD_ALPHA " : "", cfg.hud_bad & 4 ? "SOMVR_XR_HUD_QUAD " : "");
    if (X.hud)
        lg("xr: hud: somvr_hud found at %p (v%u, preload pid %d): the HUD quad on, %s-locked, %.2f m wide, %.2f m ahead, "
           "%.2f m below eye level, alpha %s (flags BLEND_TEXTURE_SOURCE_ALPHA%s)", (void *)X.hud, X.hud->version,
           X.hud->pid, cfg.hud == SOMVR_HUD_HEAD ? "head" : "world", cfg.hud_quad[1], cfg.hud_quad[0], cfg.hud_quad[2],
           cfg.hud_alpha == SOMVR_HUD_STRAIGHT ? "straight" : "premultiplied",
           cfg.hud_alpha == SOMVR_HUD_STRAIGHT ? " | UNPREMULTIPLIED_ALPHA" : "");
    else
        lg("xr: hud: no HUD quad (%s)", cfg.hud == SOMVR_HUD_OFF ? "SOMVR_XR_HUD=off"
                                         : !hud                  ? "no somvr_hud: a preload without H11"
                                         : !somvr_hud_ok(hud)    ? "somvr_hud inactive (SOMVR_HUD unset) or another version"
                                                                 : "not layout B");
    somvr_input_shared *in = (somvr_input_shared *)dlsym(RTLD_DEFAULT, SOMVR_INPUT_SYMBOL);
    I.in = somvr_input_ok(in) && (in->want & SOMVR_INW_STATE) ? in : NULL;
    if (cfg.input == SOMVR_INPUT_OFF)
        lg("xr: input: SOMVR_XR_INPUT=off: no actions");
    else if (I.in)
        lg("xr: input: somvr_input found at %p (v%u, %u bytes, want 0x%x)", (void *)I.in, I.in->version, I.in->size,
           I.in->want);
    else
        lg("xr: input: no somvr_input to fill (%s)%s",
           !in ? "no preload object" : somvr_input_ok(in) ? "want lacks STATE" : "inactive or another version",
           cfg.input == SOMVR_INPUT_LOG ? ": log only" : ": no actions");
    if (cfg.input != SOMVR_INPUT_OFF && (I.in || cfg.input == SOMVR_INPUT_LOG)) in_create(); /* fails soft */
    ptr_setup(); /* the laser pointer: somvr_input v2 with want POINTER */
    fade_setup(); /* the comfort fade: an active somvr_shared */
    ss_setup();   /* SOMVR_MARK / SOMVR_QUIT: the preload's somvr_session (before the menu: its buttons) */
    vh_setup();   /* build 10: our own VR hands (SOMVR_VRHANDS=1), the watch's and the panel's places (before the menu) */
    vm_setup();   /* the VR menu: the preload's somvr_vrmenu (SOMVR_VRMENU=1; before comfort, which follows it) */
    cf_setup();   /* comfort: the preload's somvr_comfort (SOMVR_VIGNETTE, SOMVR_SNAP_BLINK_MS) */
    th_setup();   /* the threat cues: the preload's somvr_threat (SOMVR_THREAT=1) */
    env_setup();  /* SoM loading env: SOMVR_XR_ENV */
    mn_setup();   /* build 6: the menu boards, the wrist minimap, the card, touch */
    XrSessionCreateInfo sci = {.type = XR_TYPE_SESSION_CREATE_INFO, .next = &b, .systemId = X.sys};
    if (XR_FAILED(r = X.xrCreateSession(X.inst, &sci, &X.session))) {
        X.session = XR_NULL_HANDLE;
        return xr_fail("xrCreateSession", r), -1;
    }
    X.q = qi->q;
    X.qfamily = qi->family;
    X.space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (cfg.space) { /* STAGE if the runtime has one */
        XrReferenceSpaceType types[16];
        uint32_t nt = 0;
        r = X.xrEnumerateReferenceSpaces(X.session, 16, &nt, types);
        for (uint32_t i = 0; XR_SUCCEEDED(r) && i < nt && i < 16; i++)
            if (types[i] == XR_REFERENCE_SPACE_TYPE_STAGE) X.space_type = XR_REFERENCE_SPACE_TYPE_STAGE;
        if (X.space_type != XR_REFERENCE_SPACE_TYPE_STAGE) lg("xr: no STAGE space (%s, %u types): LOCAL", xr_str(r), nt);
    }
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                     .referenceSpaceType = X.space_type,
                                     .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
    if (XR_FAILED(r = X.xrCreateReferenceSpace(X.session, &rs, &X.space))) {
        X.space = XR_NULL_HANDLE;
        return xr_fail(X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? "xrCreateReferenceSpace(STAGE)"
                                                                     : "xrCreateReferenceSpace(LOCAL)", r), -1;
    }
    if (I.set) in_attach(); /* fails soft */
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
    lg("xr: session created on queue family %u index %u (flags 0x%x), blend mode %d, space %s", qi->family, qi->index,
       qi->family < ds->nqf ? ds->qf[qi->family].queueFlags : 0, X.blend,
       X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE" : "LOCAL");
    vd_start(); /* never a void: the present gaps, the watcher thread */
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
        if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const XrEventDataReferenceSpaceChangePending *rc = (const XrEventDataReferenceSpaceChangePending *)&ev;
            int ours = rc->referenceSpaceType == X.space_type;
            if (ours && X.sh) atomic_fetch_add(&X.sh->xr.recenter_seq, 1);
            lg("xr: reference space %d changes (recentre)%s%s", rc->referenceSpaceType,
               ours && X.sh ? ": the game side re-latches its reference" : "",
               ours && X.hud_on && cfg.hud == SOMVR_HUD_WORLD ? "; the HUD quad is placed again at the next frame" : "");
            if (ours) X.hud_latched = 0, mn_replace();
            if (ours && X.floor_done && cfg.floor_frames > 0) /* SOMVR_XR_FLOOR: once more, 100 frames after it */
                X.floor_done = 0, X.floor_frames = (uint64_t)(cfg.floor_frames > 100 ? cfg.floor_frames - 100 : 0);
            continue;
        }
        if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED && I.on) {
            in_profiles();
            continue;
        }
        if (ev.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            lg("xr: event %d", ev.type);
            continue;
        }
        XrSessionState st = ((const XrEventDataSessionStateChanged *)&ev)->state;
        lg("xr: session state %d", st);
        I.focused = st == XR_SESSION_STATE_FOCUSED;
        switch (somvr_on_session_state(st, X.running)) {
        case SOMVR_SA_BEGIN: {
            XrSessionBeginInfo bi = {.type = XR_TYPE_SESSION_BEGIN_INFO,
                                     .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
            if (XR_FAILED(r = X.xrBeginSession(X.session, &bi))) return xr_fail("xrBeginSession", r), -1;
            X.running = 1;
            X.waited = X.frame_open = X.img_acquired = 0;
            lg("xr: session running");
            in_begin(); /* somvr_input's spaces + ready, before xr.state = RUNNING */
            if (X.sh) { /* the game thread may locate views from now on */
                X.sh->xr.instance = (uint64_t)(uintptr_t)X.inst;
                X.sh->xr.session = (uint64_t)(uintptr_t)X.session;
                X.sh->xr.space = (uint64_t)(uintptr_t)X.space;
                X.sh->xr.locate_views = (void *)X.xrLocateViews;
                X.sh->xr.space_type = (uint32_t)X.space_type;
                X.sh->xr.view_w = X.rec_w, X.sh->xr.view_h = X.rec_h;
                atomic_store(&X.sh->xr.state, SOMVR_XRS_RUNNING);
                lg("xr: published to somvr_shared: session, space, xrLocateViews (state running)");
            }
            break;
        }
        case SOMVR_SA_END:
            X.running = 0;
            xr_unpublish(SOMVR_XRS_NONE);
            if (I.on) in_clear(), in_publish(); /* nothing live until the session runs again */
            if (X.img_acquired) {
                XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                X.xrReleaseSwapchainImage(X.sc, &ri);
            }
            X.waited = X.frame_open = X.img_acquired = X.last_stereo = X.last_hud = 0;
            if (XR_FAILED(r = X.xrEndSession(X.session))) return xr_fail("xrEndSession", r), -1;
            lg("xr: session ended");
            break;
        case SOMVR_SA_LOST: return xr_fail("session exiting/lost (runtime state)", XR_SUCCESS), -1;
        case SOMVR_SA_NONE: break;
        }
    }
}

/* Make sure the XR swapchain fits the source images: nslots*w x h, a format for `src` (the game swapchain's, or the eye
 * images' in layout B). 0 = it fits, -1 = XR died, 1 (soft only) = the runtime has no format for src: nothing changed. */
static int xr_swapchain_ensure(dev_st *ds, VkFormat src, uint32_t w, uint32_t h, int soft, const char *what) {
    if (X.sc && X.sc_w == X.nslots * w && X.sc_h == h && X.sc_game_fmt == src) return 0;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return xr_fail("xrEnumerateSwapchainFormats", r), -1;
    VkFormat f = VK_FORMAT_UNDEFINED;
    int gamma_ok = 0;
    somvr_xfer xfer = somvr_pick_format(src, fmts, n, &f, &gamma_ok);
    if (xfer == SOMVR_XFER_BLIT) {
        VkFormatProperties a, b;
        ds->is->GetPhysicalDeviceFormatProperties(ds->pd, src, &a);
        ds->is->GetPhysicalDeviceFormatProperties(ds->pd, f, &b);
        int gfx = X.qfamily < ds->nqf && (ds->qf[X.qfamily].queueFlags & VK_QUEUE_GRAPHICS_BIT);
        if (!(a.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
            !(b.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) || !gfx) {
            if (soft) return 1;
            return xr_fail("blit support", XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED), -1;
        }
    }
    if (xfer == SOMVR_XFER_NONE) {
        lg("xr: no usable swapchain format for %s format %d among %u", what, src, n);
        if (soft) return 1;
        return xr_fail("swapchain format", XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED), -1;
    }
    X.last_stereo = 0; /* a new swapchain has no released stereo image */
    uint32_t ow = X.sc ? X.sc_w : 0, oh = X.sc ? X.sc_h : 0;
    if (X.sc) {
        sc_wait_all(ds);
        X.xrDestroySwapchain(X.sc);
        X.sc = XR_NULL_HANDLE;
        X.nimg = 0;
    }
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                                              XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = X.nslots * w,
                                .height = h,
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
    X.sc_game_fmt = src;
    X.sc_fmt = f;
    X.xfer = xfer;
    X.n_sc++;
    char was[64] = "";
    if (ow) snprintf(was, sizeof was, ", replacing %ux%u", ow, oh);
    lg("xr: swapchain %ux%u format %d (%u images) for %s format %d: %s%s (swapchain %llu%s)", ci.width, ci.height, f, n,
       what, src, xfer == SOMVR_XFER_COPY ? "copy" : "blit", gamma_ok ? "" : ", brightness will be off (no matching sRGB "
       "format)", (unsigned long long)X.n_sc, was);
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

/* Our command buffer for game image idx, waited and begun; NULL if not available. */
static VkCommandBuffer slot_begin(dev_st *ds, sc_st *s, uint32_t idx) {
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
            return VK_NULL_HANDLE;
        }
    }
    if (slot_wait(ds, sl)) return VK_NULL_HANDLE;
    cv_slot_done(sl, 1); /* SOMVR_XR_COVER: the readings its last submit carried */
    VkResult r = ds->ResetFences(ds->dev, 1, &sl->fence);
    if (r == VK_SUCCESS) r = ds->ResetCommandBuffer(sl->cmd, 0);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (r == VK_SUCCESS) r = ds->BeginCommandBuffer(sl->cmd, &bi);
    if (r != VK_SUCCESS) {
        lg("vk: recording our copy: %d", r);
        return VK_NULL_HANDLE;
    }
    return sl->cmd;
}

/* End and submit it on the present queue: wait the app's present semaphores, signal the slot's semaphore. 1 = submitted
 * (the app's semaphores are consumed). */
static int slot_submit(dev_st *ds, VkQueue q, sc_st *s, uint32_t idx, const VkPresentInfoKHR *pi) {
    slot_st *sl = &s->slot[idx];
    VkResult r;
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

/* one w x h source image (layout `in`, TRANSFER_SRC_OPTIMAL during the copy) -> the XR image at x = x0 */
static void copy_region(dev_st *ds, VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t w, uint32_t h, int32_t x0) {
    VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    if (X.xfer == SOMVR_XFER_COPY) {
        VkImageCopy c = {.srcSubresource = sub, .dstSubresource = sub, .dstOffset = {x0, 0, 0}, .extent = {w, h, 1}};
        ds->CmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    } else {
        VkImageBlit bl = {.srcSubresource = sub,
                          .srcOffsets = {{0, 0, 0}, {(int32_t)w, (int32_t)h, 1}},
                          .dstSubresource = sub,
                          .dstOffsets = {{x0, 0, 0}, {x0 + (int32_t)w, (int32_t)h, 1}}};
        ds->CmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl,
                         VK_FILTER_NEAREST);
    }
}

/* The guard of a mono frame in a multi-slot image (somvr_guard_cols): the source's last column (x = w - 1) into the
 * n columns right of the slot, so a compositor's bilinear tap past the subImage's right edge reads the edge itself,
 * not undefined texels (headset look 1: a light line along the menu screen's right border). */
static void guard_region(dev_st *ds, VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t w, uint32_t h, int32_t x0,
                         uint32_t n) {
    VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    if (!n || !w) return;
    if (X.xfer == SOMVR_XFER_COPY) {
        VkImageCopy c[SOMVR_GUARD_COLS];
        for (uint32_t i = 0; i < n; i++)
            c[i] = (VkImageCopy){.srcSubresource = sub, .srcOffset = {(int32_t)w - 1, 0, 0}, .dstSubresource = sub,
                                 .dstOffset = {x0 + (int32_t)(w + i), 0, 0}, .extent = {1, h, 1}};
        ds->CmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, c);
    } else { /* a blit stretches the one column over the n */
        VkImageBlit bl = {.srcSubresource = sub,
                          .srcOffsets = {{(int32_t)w - 1, 0, 0}, {(int32_t)w, (int32_t)h, 1}},
                          .dstSubresource = sub,
                          .dstOffsets = {{x0 + (int32_t)w, 0, 0}, {x0 + (int32_t)(w + n), (int32_t)h, 1}}};
        ds->CmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl,
                         VK_FILTER_NEAREST);
    }
}

/* D.armed (SOMVR_XR_DUMP): after the copies, the whole XR image (TRANSFER_DST_OPTIMAL) -> D.buf for the host (read
 * after the fence, dump_write). Returns the layout the image is left in. */
static VkImageLayout dump_cmds(dev_st *ds, VkCommandBuffer cmd, VkImage dst) {
    if (!D.armed) return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkImageMemoryBarrier t;
    img_barrier(&t, dst, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                           &t);
    VkBufferImageCopy r = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .imageExtent = {X.sc_w, X.sc_h, 1}}; /* the whole image, tightly packed */
    ds->CmdCopyImageToBuffer(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, D.buf, 1, &r);
    /* a fence makes device writes available, not visible to the host: that needs HOST_READ in a barrier */
    VkBufferMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .buffer = D.buf,
                                .size = VK_WHOLE_SIZE};
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &hb, 0, NULL);
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

/* One submit: wait the app's present semaphores, copy game image -> XR slot `xslot` (x = xslot * w), signal the slot's
 * semaphore. mono: this frame is the slot alone (a quad/mono layer): the rest of the image is discarded and, in a
 * multi-slot image, the guard columns are written. Returns 1 if submitted (the app's semaphores are consumed), 0 if
 * not. */
static int copy_submit(dev_st *ds, VkQueue q, sc_st *s, uint32_t idx, VkImage dst, const VkPresentInfoKHR *pi,
                       uint32_t xslot, int mono) {
    VkCommandBuffer cmd = slot_begin(ds, s, idx);
    if (!cmd) return 0;
    VkImage src = s->img[idx];
    VkImageMemoryBarrier b[2];
    img_barrier(&b[0], src, 0, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    /* one slot is written: with one slot (or a mono frame) the old contents do not matter; a stereo slot of a multi-slot
     * frame keeps the other slot (COLOR_ATTACHMENT_OPTIMAL, as released or as left by us) */
    img_barrier(&b[1], dst, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                X.nslots > 1 && !mono ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                           NULL, 2, b);
    copy_region(ds, cmd, src, dst, s->ext.width, s->ext.height, (int32_t)(xslot * s->ext.width));
    if (mono)
        guard_region(ds, cmd, src, dst, s->ext.width, s->ext.height, (int32_t)(xslot * s->ext.width),
                     somvr_guard_cols(X.sc_w, s->ext.width, xslot));
    VkImageLayout xr_old = dump_cmds(ds, cmd, dst);
    img_barrier(&b[0], src, VK_ACCESS_TRANSFER_READ_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    /* XR_KHR_vulkan_enable: released images must be COLOR_ATTACHMENT_OPTIMAL */
    img_barrier(&b[1], dst, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, xr_old,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                           NULL, 2, b);
    return slot_submit(ds, q, s, idx, pi);
}

/* Layout B: both eye images -> the two halves of the XR image, in one submit (same semaphores as copy_submit). The eye
 * images sit in TRANSFER_DST_OPTIMAL after the game's CopyResource (SOMVR_EYE_IMAGE_LAYOUT) and go back to it, so
 * Feral's own layout tracking stays right; the game's queue order covers its copy (we wait its present semaphores on
 * the same queue). D.armed (SOMVR_XR_DUMP): the finished XR image is also copied into D.buf for the host. hd (H11):
 * the HUD image (in TRANSFER_DST_OPTIMAL like the eye images, and back) -> the acquired image of the HUD swapchain,
 * a raw copy, in the same submit. */
static int eyes_submit(dev_st *ds, VkQueue q, sc_st *s, uint32_t idx, VkImage dst, const VkPresentInfoKHR *pi,
                       const eyes_st *ey, const hud_st *hd) {
    VkCommandBuffer cmd = slot_begin(ds, s, idx);
    if (!cmd) return 0;
    VkImageMemoryBarrier b[5];
    uint32_t nb = hd ? 5 : 3;
    VkImage hud_dst = hd ? X.hud_img[X.hud_xi] : VK_NULL_HANDLE;
    for (int e = 0; e < 2; e++)
        img_barrier(&b[e], ey->img[e], VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    img_barrier(&b[2], dst, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, /* both halves are written */
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (hd) {
        img_barrier(&b[3], hd->img, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        img_barrier(&b[4], hud_dst, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, /* all of it is written */
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                           NULL, nb, b);
    for (int e = 0; e < 2; e++) copy_region(ds, cmd, ey->img[e], dst, ey->w, ey->h, (int32_t)((uint32_t)e * ey->w));
    VkImageLayout hud_old = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (hd) {
        VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        VkImageCopy c[SOMVR_HUD_SPLIT_MAX] = {{.srcSubresource = sub, .dstSubresource = sub, .extent = {hd->w, hd->h, 1}}};
        int nc = mn_hud_regions(ds, hd, c, SOMVR_HUD_SPLIT_MAX); /* builds 6, 8: the tiles into the extra area */
        if (nc) mn_hud_clear(ds, cmd, hud_dst);
        ds->CmdCopyImage(cmd, hd->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hud_dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         nc ? (uint32_t)nc : 1u, c);
        hud_old = mn_hud_dump_cmds(ds, cmd, hud_dst); /* SOMVR_XR_DUMP: the HUD image too (before the host barrier) */
        cv_cmds(ds, cmd, &s->slot[idx], hd); /* SOMVR_XR_COVER: the HUD image's grid -> the readback buffer */
    }
    VkImageLayout xr_old = dump_cmds(ds, cmd, dst); /* the XR image as it is released -> D.buf (SOMVR_XR_DUMP) */
    for (int e = 0; e < 2; e++)
        img_barrier(&b[e], ey->img[e], VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    img_barrier(&b[2], dst, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, xr_old,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (hd) {
        img_barrier(&b[3], hd->img, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        img_barrier(&b[4], hud_dst, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, hud_old,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                           NULL, nb, b);
    int ok = slot_submit(ds, q, s, idx, pi);
    cv_submitted(ok);
    return ok;
}

/* The eye images a layout-B tag names: both bits set, the info of the same generation, both images created by this
 * device at the announced size with one format and TRANSFER_SRC, and still alive. 1 = usable (*out filled). */
static int eyes_lookup(VkDevice dev, const somvr_tag *t, eyes_st *out, char *why, size_t cap) {
    somvr_eyes_info in;
    if (t->eyes != 3u) return snprintf(why, cap, "the tag says eyes 0x%x", t->eyes), 0;
    if (!somvr_eyes_read(X.sh, &in)) return snprintf(why, cap, "the eye info is being rewritten"), 0;
    if (in.gen != t->eye_gen) return snprintf(why, cap, "eye info gen %u, the tag's %u", in.gen, t->eye_gen), 0;
    *out = (eyes_st){.w = in.w, .h = in.h, .gen = in.gen, .fmt = VK_FORMAT_UNDEFINED};
    pthread_mutex_lock(&g_cand_mx);
    int found = 0;
    for (int e = 0; e < 2; e++)
        for (int i = 0; i < MAX_EYE_CAND; i++)
            if (in.image[e] && g_cand[i].img == (VkImage)(uintptr_t)in.image[e] && g_cand[i].dev == dev &&
                g_cand[i].w == in.w && g_cand[i].h == in.h && (g_cand[i].usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                (e == 0 || g_cand[i].fmt == out->fmt)) {
                out->img[e] = g_cand[i].img;
                out->fmt = g_cand[i].fmt;
                g_cand[i].used = 1; /* kept by cand_slot from now on */
                found |= 1 << e;
                break;
            }
    pthread_mutex_unlock(&g_cand_mx);
    if (found != 3)
        return snprintf(why, cap, "vkimage %#llx/%#llx (%ux%u): %s", (unsigned long long)in.image[0],
                        (unsigned long long)in.image[1], in.w, in.h,
                        !in.image[0] || !in.image[1] ? "not known yet" : "not created by this device at that size"), 0;
    if (out->fmt == X.eyes_bad_fmt) return snprintf(why, cap, "the runtime has no format for eye format %d", out->fmt), 0;
    return 1;
}

/* ---- SOMVR_XR_DUMP: the XR image of chosen layout-B stereo frames -> PPM (X.mx held) ----------------------------- */
/* Any dump error: logged once, the dump is off for the rest of the process; XR goes on as before. */
__attribute__((format(printf, 1, 2))) static void dump_off(const char *fmt, ...) {
    char why[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: dump off: %s", why);
    D.n = D.armed = 0;
}

/* The buffer is idle here: every armed copy was waited for, or the device is being destroyed. */
static void dump_free(void) {
    dev_st *ds = D.ds;
    if (!ds) return;
    if (D.map) ds->UnmapMemory(ds->dev, D.mem);
    if (D.buf) ds->DestroyBuffer(ds->dev, D.buf, NULL);
    if (D.mem) ds->FreeMemory(ds->dev, D.mem, NULL);
    D.ds = NULL, D.buf = VK_NULL_HANDLE, D.mem = VK_NULL_HANDLE, D.map = NULL, D.size = 0;
}

/* A mapped host-visible TRANSFER_DST buffer of `need` bytes: made at the first eye frame, again if the XR image grew.
 * 0 = ready; else the dump is off and nothing is left behind. */
static int dump_buffer(dev_st *ds, VkDeviceSize need) {
    if (D.buf && D.ds == ds && D.size >= need) return 0;
    if (!ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements || !ds->AllocateMemory ||
        !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory || !ds->CmdCopyImageToBuffer ||
        !ds->is->GetPhysicalDeviceMemoryProperties)
        return dump_off("the driver lacks a buffer entry point"), -1;
    dump_free();
    D.ds = ds;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = need,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    const char *what = "vkCreateBuffer";
    VkResult r = ds->CreateBuffer(ds->dev, &bci, NULL, &D.buf);
    if (r != VK_SUCCESS) D.buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    VkPhysicalDeviceMemoryProperties mp = {0};
    int type = -1;
    if (r == VK_SUCCESS) {
        ds->GetBufferMemoryRequirements(ds->dev, D.buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the buffer";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) r = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (r == VK_SUCCESS) {
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                   .allocationSize = mr.size,
                                   .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((r = ds->AllocateMemory(ds->dev, &ai, NULL, &D.mem)) != VK_SUCCESS) D.mem = VK_NULL_HANDLE;
    }
    if (r == VK_SUCCESS) {
        what = "vkBindBufferMemory";
        r = ds->BindBufferMemory(ds->dev, D.buf, D.mem, 0);
    }
    if (r == VK_SUCCESS) {
        what = "vkMapMemory";
        if ((r = ds->MapMemory(ds->dev, D.mem, 0, VK_WHOLE_SIZE, 0, &D.map)) != VK_SUCCESS) D.map = NULL;
    }
    if (r != VK_SUCCESS) {
        dump_free();
        return dump_off("%s: %d", what, r), -1;
    }
    D.size = need;
    lg("xr: dump buffer %llu bytes, memory type %d (flags 0x%x)", (unsigned long long)mr.size, type,
       mp.memoryTypes[type].propertyFlags);
    return 0;
}

/* ---- a bug mark's eyes (SSN; X.mx held, the present thread) ---- */
static double ss_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
/* a new request from the preload (taken once: its folder copied now); 1 = one is waiting for a frame */
static int ss_pending(void) {
    if (!SSN.obj || SSN.dead) return 0;
    if (!SSN.want) {
        uint32_t r = atomic_load_explicit(&SSN.obj->dump_req, memory_order_acquire);
        if (r == SSN.seen) return 0;
        SSN.seen = r;
        memcpy(SSN.dir, SSN.obj->dump_dir, sizeof SSN.dir);
        SSN.dir[sizeof SSN.dir - 1] = 0;
        if (!SSN.dir[0]) { /* nowhere to write: answered at once */
            atomic_fetch_add_explicit(&SSN.obj->dump_fail, 1, memory_order_relaxed);
            atomic_store_explicit(&SSN.obj->dump_done, r, memory_order_release);
            return 0;
        }
        SSN.want = r, SSN.n_asked++;
    }
    return 1;
}
/* this request (or every later one: dead) cannot be served */
__attribute__((format(printf, 2, 3))) static void ss_fail(int dead, const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: mark: no eyes for %s: %s%s", SSN.dir, why, dead ? " (no more mark pictures this session)" : "");
    atomic_fetch_add_explicit(&SSN.obj->dump_fail, 1, memory_order_relaxed);
    atomic_store_explicit(&SSN.obj->dump_done, SSN.want, memory_order_release);
    SSN.want = 0, SSN.dead |= dead;
}
typedef struct {
    char path[SOMVR_SS_DIR_MAX + 32];
    uint32_t w, h, req;
    int bgra;
    somvr_session_shared *obj;
    uint8_t px[]; /* w x h x 4, as the XR image held it */
} ss_job;
/* the writer thread: RGB rows into eyes.ppm.part, renamed to eyes.ppm; then dump_done (the preload's second buzz) */
static void *ss_writer(void *arg) {
    ss_job *j = arg;
    char part[sizeof j->path + 8];
    snprintf(part, sizeof part, "%s.part", j->path);
    FILE *f = fopen(part, "we");
    uint8_t *row = f ? malloc((size_t)j->w * 3) : NULL;
    int ok = row && fprintf(f, "P6\n%u %u\n255\n", j->w, j->h) > 0;
    for (uint32_t y = 0; ok && y < j->h; y++) {
        somvr_dump_row(j->px + (size_t)y * j->w * 4, row, j->w, j->bgra);
        ok = fwrite(row, 3, j->w, f) == j->w;
    }
    int e = errno;
    free(row);
    if (f && fclose(f) && ok) ok = 0, e = errno;
    if (ok && rename(part, j->path)) ok = 0, e = errno;
    if (!ok) {
        unlink(part);
        atomic_fetch_add_explicit(&j->obj->dump_fail, 1, memory_order_relaxed);
    }
    lg("xr: mark: eyes %ux%u -> %s%s%s", j->w, j->h, j->path, ok ? "" : ": FAILED: ", ok ? "" : strerror(e ? e : EIO));
    atomic_store_explicit(&j->obj->dump_done, j->req, memory_order_release);
    free(j);
    return NULL;
}
/* dump_arm's first step: a mark's request arms this frame's copy (D's buffer, made if needed) */
static void ss_arm(dev_st *ds) {
    int srgb;
    if (somvr_dump_order(X.sc_fmt, &srgb) < 0)
        ss_fail(1, "the XR swapchain format %d is not 8-bit RGBA/BGRA", X.sc_fmt);
    else if (dump_buffer(ds, (VkDeviceSize)X.sc_w * X.sc_h * 4))
        ss_fail(1, "no host-visible buffer (see the dump line above)");
    else
        D.armed = 1, D.arm_ix = -1;
}
/* after the armed submit: its fence (1 s cap), a copy of the pixels, the writer thread (detached) */
static void ss_write(dev_st *ds, slot_st *sl, int mono) {
    D.armed = 0;
    VkResult r = ds->WaitForFences(ds->dev, 1, &sl->fence, VK_TRUE, FENCE_TIMEOUT_NS);
    if (r != VK_SUCCESS) {
        ss_fail(0, "vkWaitForFences(the copy): %d", r);
        return;
    }
    sl->pending = 0;
    int srgb = 0, bgra = somvr_dump_order(X.sc_fmt, &srgb);
    size_t n = (size_t)X.sc_w * X.sc_h * 4;
    ss_job *j = malloc(sizeof *j + n);
    if (!j) {
        ss_fail(0, "no memory for %zu bytes", n);
        return;
    }
    snprintf(j->path, sizeof j->path, "%s/eyes.ppm", SSN.dir);
    j->w = X.sc_w, j->h = X.sc_h, j->req = SSN.want, j->bgra = bgra, j->obj = SSN.obj;
    memcpy(j->px, D.map, n);
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int e = pthread_create(&th, &at, ss_writer, j);
    pthread_attr_destroy(&at);
    if (e) {
        free(j);
        ss_fail(0, "pthread_create: %s", strerror(e));
        return;
    }
    SSN.n_threads++;
    lg("xr: mark: %s frame (%s) copied for %s (request %u; written on a thread of its own)", mono ? "a mono" : "a stereo",
       mono ? "the menus' screen" : "both eyes", SSN.dir, SSN.want);
    SSN.want = 0;
}
/* the session's setup: the preload's somvr_session (SOMVR_MARK / SOMVR_QUIT); a request older than the session is
 * answered (the preload's buzz does not wait for it) */
static void ss_setup(void) {
    SSN.obj = NULL, SSN.want = 0, SSN.dead = 0, SSN.quit_armed = 0;
    somvr_session_shared *s = (somvr_session_shared *)dlsym(RTLD_DEFAULT, SOMVR_SESSION_SYMBOL);
    if (!s || !somvr_session_ok(s)) {
        lg("xr: session: %s (no bug-mark pictures, no Report / Quit game buttons)",
           !s ? "no somvr_session (a preload without it)" : "somvr_session inactive (SOMVR_MARK and SOMVR_QUIT off) or another version");
        return;
    }
    SSN.obj = s;
    SSN.seen = atomic_load_explicit(&s->dump_req, memory_order_acquire);
    atomic_store_explicit(&s->dump_done, SSN.seen, memory_order_release);
    lg("xr: session: somvr_session found at %p (v%u, preload pid %d): marks %s, quit %s", (void *)s, s->version, s->pid,
       (s->avail & SOMVR_SSA_MARK) ? "on (the eyes of the next frame on request; the menu's Report)" : "off",
       (s->avail & SOMVR_SSA_QUIT) ? "on (the menu's Quit game, pressed twice)" : "off");
}

/* Before a layout-B eye submit (mono = 0, count = the submit's number X.n_eyes + 1) or a mono frame's copy (mono = 1,
 * count = X.n_mono) into the acquired XR image (X.sc_w x X.sc_h, X.sc_fmt): while entries of that kind are pending,
 * the buffer exists from the first such frame on; armed if this frame is the first one at or after a pending N. */
static void dump_arm(dev_st *ds, int mono, uint64_t count) {
    int want = -1, pending = 0, srgb;
    if (ss_pending()) { /* a bug mark's eyes first (SSN); SOMVR_XR_DUMP's frames wait for the next one */
        ss_arm(ds);
        if (D.armed) return;
    }
    for (int i = 0; i < D.n; i++) { /* sN: this frame is the N-th swapchain's first copied one (either kind) */
        if ((D.done >> i & 1) || !(D.at[i] & SOMVR_DUMP_SC)) continue;
        if ((D.at[i] & ~SOMVR_DUMP_SC) < X.n_sc) { /* that swapchain is gone without a copied frame */
            D.done |= 1u << i;
            lg("xr: dump of swapchain %llu skipped: no frame was copied into it", (unsigned long long)(D.at[i] & ~SOMVR_DUMP_SC));
            continue;
        }
        pending = 1;
        if (want < 0 && (D.at[i] & ~SOMVR_DUMP_SC) == X.n_sc) want = i;
    }
    for (int i = 0; i < D.n; i++) {
        if ((D.done >> i & 1) || (D.at[i] & SOMVR_DUMP_SC) || !(D.at[i] & SOMVR_DUMP_MONO) != !mono) continue;
        pending = 1;
        if (want < 0 && (D.at[i] & ~SOMVR_DUMP_MONO) <= count) want = i;
    }
    D.armed = 0;
    if (!pending) return;
    if (somvr_dump_order(X.sc_fmt, &srgb) < 0)
        dump_off("XR swapchain format %d is not 8-bit RGBA/BGRA", X.sc_fmt);
    else if (dump_buffer(ds, (VkDeviceSize)X.sc_w * X.sc_h * 4 + (mono ? 0 : mn_dump_extra())) == 0)
        D.armed = want >= 0, D.arm_ix = want;
}

/* a dump of the next stereo frame (the test file's `dump`, build 10's SOMVR_XR_DUMP_HURT): "ok" or why not */
static const char *dump_next(void) {
    if (!D.n || !D.base[0]) return "no dump (SOMVR_XR_DUMP and SOMVR_XR_LOG off)";
    int i = 0;
    while (i < D.n && !(D.done >> i & 1)) i++;
    if (i == D.n && D.n < SOMVR_DUMP_MAX) D.n++;
    if (i >= D.n) return "every dump slot is pending";
    D.at[i] = X.n_eyes + 1, D.done &= ~(1u << i);
    return "ok";
}

/* After the armed submit (X.n_eyes / X.n_mono count it): wait its fence (1 s cap; the slot is idle then, as after
 * slot_wait), write D.map as <base>.stereoN.ppm or <base>.monoN.ppm. */
static void dump_write(dev_st *ds, slot_st *sl, int mono) {
    if (D.arm_ix < 0) { /* a bug mark's (ss_arm) */
        ss_write(ds, sl, mono);
        return;
    }
    D.armed = 0;
    D.done |= 1u << D.arm_ix;
    VkResult r = ds->WaitForFences(ds->dev, 1, &sl->fence, VK_TRUE, FENCE_TIMEOUT_NS);
    if (r != VK_SUCCESS) {
        dump_off("vkWaitForFences(the dump's copy): %d", r);
        return;
    }
    sl->pending = 0;
    int srgb = 0, bgra = somvr_dump_order(X.sc_fmt, &srgb);
    uint32_t w = X.sc_w, h = X.sc_h;
    int sc = (D.at[D.arm_ix] & SOMVR_DUMP_SC) != 0;
    const char *kind = sc ? "swapchain" : mono ? "mono" : "stereo";
    unsigned long long nr = (unsigned long long)(sc ? X.n_sc : mono ? X.n_mono : X.n_eyes);
    char path[sizeof D.base + 40];
    snprintf(path, sizeof path, "%s.%s%llu.ppm", D.base, kind, nr);
    FILE *f = fopen(path, "we");
    uint8_t *row = f ? malloc((size_t)w * 3) : NULL;
    int ok = row && fprintf(f, "P6\n%u %u\n255\n", w, h) > 0;
    for (uint32_t y = 0; ok && y < h; y++) {
        somvr_dump_row((const uint8_t *)D.map + (size_t)y * w * 4, row, w, bgra);
        ok = fwrite(row, 3, w, f) == w;
    }
    int e = errno;
    free(row);
    if (f && fclose(f) && ok) ok = 0, e = errno;
    if (!ok) {
        dump_off("%s: %s", path, strerror(e ? e : EIO));
        return;
    }
    lg("xr: dump %s frame %llu -> %s (%ux%u format %d%s%s)", kind, nr, path, w, h, X.sc_fmt,
       srgb ? ", sRGB bytes as is" : "", sc ? (mono ? "; its first frame: mono" : "; its first frame: stereo") : "");
    if (!mono && !sc) mn_dump_write(); /* build 6: the HUD swapchain image of that frame, if it was copied */
}

/* ---- the laser pointer (docs/re-xr-pipeline.md 10, ../somvr_pointer.h; X.mx held, the present thread) ---------- */
/* the pointer image's swapchain goes (nothing of ours is in flight on it: its one upload was waited) */
static void ptr_sc_destroy(void) {
    if (X.ptr_sc) X.xrDestroySwapchain(X.ptr_sc);
    X.ptr_sc = XR_NULL_HANDLE;
    X.ptr_filled = 0;
}

/* no drawing for the rest of this session (logged once); the pointer itself is still computed and published */
__attribute__((format(printf, 1, 2))) static void ptr_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: pointer: drawing off for this session: %s (the pointer is still published: the menus take its clicks; %llu "
       "frames drawn)", why, (unsigned long long)X.n_ptr_drawn);
    X.ptr_draw = 0;
    X.ptr_nq = 0;
    ptr_sc_destroy();
}

/* xr_session_setup, after somvr_input is looked up: is the pointer on for this session? */
static void ptr_setup(void) {
    X.ptr_on = X.ptr_hud = X.ptr_draw = 0;
    X.ptr_nq = X.ptr_lines = 0;
    X.n_ptr = X.n_ptr_on = X.n_ptr_drawn = X.n_ptr_clicks = 0;
    somvr_ptr_cfg_default(&X.ptr_cfg);
    somvr_ptr_reset(&X.ptr);
    if (cfg.ptr_bad) lg("xr: pointer: SOMVR_XR_POINTER=%s is not on, dot or off: on", getenv("SOMVR_XR_POINTER"));
    if (!I.in || !(I.in->want & SOMVR_INW_POINTER)) return; /* no SOMVR_POINTER on the game side: silent */
    const char *no = !somvr_input_has_ptr(I.in) ? "somvr_input has no pointer block (v1-sized)"
                     : cfg.ptr == SOMVR_PTRM_OFF ? "SOMVR_XR_POINTER=off"
                     : cfg.input == SOMVR_INPUT_OFF ? "SOMVR_XR_INPUT=off (no controllers)" : NULL;
    if (no) {
        lg("xr: pointer: the preload wants it, but %s: no pointer", no);
        return;
    }
    X.ptr_on = X.ptr_draw = 1;
    X.ptr_hud = (I.in->want & SOMVR_INW_PTR_HUD) != 0;
    const somvr_ptr_cfg *c = &X.ptr_cfg;
    lg("xr: pointer on: the menu quad%s; drawn: %s; hysteresis +%.2f uv, One Euro %.1f Hz + %.1f/(uv/s), trigger %.2f/%.2f, "
       "a click survives %d frames without the hand", X.ptr_hud ? " and the world-locked HUD quad" : "",
       cfg.ptr == SOMVR_PTRM_DOT ? "the dot" : "the dot and the ray", c->leave, c->min_cutoff, c->beta, c->trig_on,
       c->trig_off, c->lose_frames);
}

/* The pointer image's swapchain (a static image if the runtime takes one), filled once from a host buffer and
 * released: the layers then show its one released image for the rest of the session. 0 = ready, -1 = drawing off. */
static int ptr_fill(dev_st *ds) {
    if (X.ptr_filled) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return ptr_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return ptr_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return ptr_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    int bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM, stat = 1;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = SOMVR_PTR_IMG_W,
                                .height = SOMVR_PTR_IMG_H,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &X.ptr_sc))) { /* no static images: an ordinary one */
        ci.createFlags = 0, stat = 0;
        r = X.xrCreateSwapchain(X.session, &ci, &X.ptr_sc);
    }
    if (XR_FAILED(r)) {
        X.ptr_sc = XR_NULL_HANDLE;
        return ptr_off("xrCreateSwapchain(%ux%u format %d): %s", SOMVR_PTR_IMG_W, SOMVR_PTR_IMG_H, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(X.ptr_sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return ptr_off("xrEnumerateSwapchainImages (pointer): %s, %u images", xr_str(r), n), -1;
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(X.ptr_sc, &ai, &xi)) || xi >= n)
        return ptr_off("xrAcquireSwapchainImage (pointer): %s, image %u of %u", xr_str(r), xi, n), -1;
    if ((r = X.xrWaitSwapchainImage(X.ptr_sc, &wti)) != XR_SUCCESS)
        return ptr_off("xrWaitSwapchainImage (pointer): %s", xr_str(r)), -1;
    /* the upload: a host buffer with the image, one copy into the acquired image, waited here (4 KB, once) */
    VkDeviceSize sz = (VkDeviceSize)SOMVR_PTR_IMG_W * SOMVR_PTR_IMG_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = NULL;
    int type = -1, in_flight = 0;
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &buf);
    if (vr != VK_SUCCESS) buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the upload";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &mem)) != VK_SUCCESS) mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, buf, mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, mem, 0, VK_WHOLE_SIZE, 0, &map)) != VK_SUCCESS)
        map = NULL;
    if (vr == VK_SUCCESS) {
        somvr_ptr_image(map, bgra); /* HOST_COHERENT: visible to the device at the submit */
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd)) != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, im[xi].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                               1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_PTR_IMG_W, SOMVR_PTR_IMG_H, 1}}; /* tightly packed */
        ds->CmdCopyBufferToImage(cmd, buf, im[xi].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, im[xi].image, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its buffer, command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
        if (map) ds->UnmapMemory(ds->dev, mem);
        if (buf) ds->DestroyBuffer(ds->dev, buf, NULL);
        if (mem) ds->FreeMemory(ds->dev, mem, NULL);
    }
    if (vr != VK_SUCCESS) return ptr_off("the image's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(X.ptr_sc, &ri)))
        return ptr_off("xrReleaseSwapchainImage (pointer): %s", xr_str(r)), -1;
    X.ptr_filled = 1, X.ptr_fmt = f;
    lg("xr: pointer: image %ux%u format %d (%s swapchain, %u image(s)) filled once: the dot and the ray, premultiplied",
       SOMVR_PTR_IMG_W, SOMVR_PTR_IMG_H, f, stat ? "a static" : "an ordinary", n);
    return 0;
}

/* one of the pointer's quad layers: part `ray` of the image, at pose, w x h, alpha-blended (premultiplied) */
static XrCompositionLayerQuad ptr_layer(int ray, const somvr_posef *pose, float w, float h) {
    int32_t rc[4];
    somvr_ptr_rect(ray, rc);
    XrCompositionLayerQuad q = {.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                .space = X.space,
                                .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                .subImage = {X.ptr_sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                .size = {w, h}};
    memcpy(&q.pose, pose, sizeof q.pose);
    return q;
}

/* the head for a mono frame's ray (the views at the frame's display time, their midpoint); 0 = unknown */
static int ptr_head(float out[3]) {
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = X.fs.predictedDisplayTime,
                           .space = X.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t nv = 0;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
    if (XR_FAILED(r) || nv != 2 || !(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) return 0;
    out[0] = 0.5f * (v[0].pose.position.x + v[1].pose.position.x);
    out[1] = 0.5f * (v[0].pose.position.y + v[1].pose.position.y);
    out[2] = 0.5f * (v[0].pose.position.z + v[1].pose.position.z);
    return 1;
}

/* One frame's pointer, right before its xrEndFrame (the snapshot I.st is this frame's: located at its display time).
 * quad: what the frame shows to point at (SOMVR_PTRQ_NONE: nothing) at qpose (X.space), qsize metres, img_w x img_h
 * its image; eye: the head the ray faces (NULL: unknown). Publishes somvr_input.ptr; X.ptr_q[0 .. X.ptr_nq) = the
 * frame's extra layers (the ray, the dot) when drawing is on and there is something to draw. */
static void ptr_frame(dev_st *ds, uint32_t quad, const XrPosef *qpose, const float qsize[2], uint32_t img_w,
                      uint32_t img_h, const float *eye) {
    X.ptr_nq = 0;
    if (!X.ptr_on || !I.in) return;
    somvr_ptr_in in;
    memset(&in, 0, sizeof in);
    if (I.on) somvr_ptr_in_from(&I.st, &in); /* no actions: no rays, no triggers */
    in.time = I.st.time ? I.st.time : X.fs.predictedDisplayTime;
    in.quad = quad;
    if (quad != SOMVR_PTRQ_NONE) {
        memcpy(&in.quad_pose, qpose, sizeof in.quad_pose);
        in.quad_size[0] = qsize[0], in.quad_size[1] = qsize[1];
    }
    somvr_ptr_out o;
    somvr_ptr_update(&X.ptr, &X.ptr_cfg, &in, &o);
    X.n_ptr++;
    uint32_t tq = 0; /* build 6: a fingertip on the menu wins over the ray (o rewritten, its markers in X.ptr_q) */
    int touch = mn_ptr_touch(quad, &in, &o, eye, X.ptr_q, &tq);
    int draw = X.ptr_draw && quad != SOMVR_PTRQ_NONE && (o.flags & (SOMVR_PTRF_ON | SOMVR_PTRF_AIM));
    if (draw && ptr_fill(ds)) draw = 0, tq = 0;
    uint32_t nq = 0;
    if (touch) nq = draw ? tq : 0;
    if (!touch && draw && cfg.ptr == SOMVR_PTRM_ON && (o.flags & SOMVR_PTRF_AIM)) { /* the ray: from 6 cm ahead */
        float a[3], b[3], len = (o.flags & SOMVR_PTRF_ON) ? o.dist : 1.0f;
        for (int i = 0; i < 3; i++) a[i] = o.origin[i] + 0.06f * o.dir[i], b[i] = o.origin[i] + len * o.dir[i];
        if (o.flags & SOMVR_PTRF_ON) memcpy(b, o.hit, sizeof b); /* to the dot (the filtered point) */
        somvr_posef rp;
        float rs[2];
        if (len > 0.1f && somvr_ptr_ray_quad(a, b, eye, 0.002f, &rp, rs)) X.ptr_q[nq++] = ptr_layer(1, &rp, rs[0], rs[1]);
    }
    if (!touch && draw && (o.flags & SOMVR_PTRF_ON)) { /* the dot: on the quad, turned like it, 0.5 deg from the hand */
        somvr_posef dp;
        memcpy(dp.q, in.quad_pose.q, sizeof dp.q);
        memcpy(dp.p, o.hit, sizeof dp.p);
        float d = somvr_ptr_dot_size(o.dist, 0.5f);
        X.ptr_q[nq++] = ptr_layer(0, &dp, d, d);
    }
    X.ptr_nq = nq;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    somvr_ptr_pub p = {.frame = X.frames + 1,
                       .time = in.time,
                       .mono_ns = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec,
                       .quad = quad,
                       .flags = o.flags | (nq ? SOMVR_PTRF_DRAWN : 0u),
                       .hand = o.hand,
                       .consume = o.consume,
                       .u = o.uv[0],
                       .v = o.uv[1],
                       .downs = X.ptr.downs + mn_downs(), /* (+ the fingertip's, build 6) */
                       .ups = X.ptr.ups + mn_ups(),
                       .dist = o.dist,
                       .img_w = quad != SOMVR_PTRQ_NONE ? img_w : 0,
                       .img_h = quad != SOMVR_PTRQ_NONE ? img_h : 0,
                       .quad_pose = in.quad_pose,
                       .quad_size = {in.quad_size[0], in.quad_size[1]}};
    somvr_ptr_publish(I.in, &p);
    atomic_fetch_add_explicit(&I.in->n_ptr_frames, 1, memory_order_relaxed);
    if (o.flags & SOMVR_PTRF_ON) X.n_ptr_on++, atomic_fetch_add_explicit(&I.in->n_ptr_on, 1, memory_order_relaxed);
    if (nq) X.n_ptr_drawn++, atomic_fetch_add_explicit(&I.in->n_ptr_drawn, 1, memory_order_relaxed);
    X.n_ptr_clicks += (o.edges & SOMVR_PTRE_PRESS) != 0;
    static const char *const qn[] = {"none", "menu", "hud"};
    if ((o.edges && X.ptr_lines < 200) || X.n_ptr <= 3 || X.n_ptr % 600 == 0) {
        X.ptr_lines += o.edges != 0;
        lg("xr: pointer frame %llu on %s (%ux%u): hand %c%s flags 0x%x edges 0x%x uv %.4f,%.4f dist %.3f m consume 0x%x "
           "clicks %u/%u raw L %d %.3f,%.3f R %d %.3f,%.3f layers %u (frames %llu, on %llu, drawn %llu)",
           (unsigned long long)p.frame, qn[quad <= SOMVR_PTRQ_HUD ? quad : 0], p.img_w, p.img_h, hand_ch[o.hand & 1],
           (o.flags & SOMVR_PTRF_GRIP) ? " (grip)" : "", p.flags, o.edges, o.uv[0], o.uv[1], o.dist, o.consume, p.downs,
           p.ups, o.raw_hit[0], o.raw_uv[0][0], o.raw_uv[0][1], o.raw_hit[1], o.raw_uv[1][0], o.raw_uv[1][1], nq,
           (unsigned long long)X.n_ptr, (unsigned long long)X.n_ptr_on, (unsigned long long)X.n_ptr_drawn);
    }
}

/* ---- the comfort fade (docs/re-xr-pipeline.md 6.8, ./somvr_fade.h; X.mx held, the present thread) ------------- */
/* the fade image's swapchain goes (nothing of ours is in flight on it: its one upload was waited) */
static void fade_sc_destroy(void) {
    if (X.fade_sc) X.xrDestroySwapchain(X.fade_sc);
    X.fade_sc = XR_NULL_HANDLE;
    X.fade_filled = 0;
}

/* no fade for the rest of this session (logged once); the frames go on without it */
__attribute__((format(printf, 1, 2))) static void fade_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: fade off for this session: %s (stereo frames go on without it: the base swaps are seen; %llu faded frames)",
       why, (unsigned long long)X.n_fade);
    X.fade_on = 0;
    X.fade_n = 0;
    fade_sc_destroy();
}

/* xr_session_setup, after somvr_shared is looked up: is the fade on for this session? */
static void fade_setup(void) {
    X.fade_on = X.sh && cfg.fade;
    X.fade_n = X.last_fade = 0, X.last_fade_red = 0;
    X.n_fade = X.n_fade_black = 0;
    if (cfg.fade_bad) lg("xr: fade: SOMVR_XR_FADE=%s is not on or off: on", getenv("SOMVR_XR_FADE"));
    if (X.sh)
        lg("xr: fade: %s", X.fade_on ? "on: a record with a fade level gets a black quad over its layers"
                                     : "off (SOMVR_XR_FADE=off)");
}

/* The ramp image's swapchain (a static image if the runtime takes one), filled once from a host buffer and released,
 * like the pointer's. 0 = ready, -1 = the fade is off now. */
static int fade_fill(dev_st *ds) {
    if (X.fade_filled) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return fade_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return fade_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB,
                                     VK_FORMAT_B8G8R8A8_SRGB}; /* black and a linear alpha in all four */
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return fade_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    int stat = 1;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = SOMVR_FADE_W,
                                .height = SOMVR_FADE_H,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &X.fade_sc))) { /* no static images: an ordinary one */
        ci.createFlags = 0, stat = 0;
        r = X.xrCreateSwapchain(X.session, &ci, &X.fade_sc);
    }
    if (XR_FAILED(r)) {
        X.fade_sc = XR_NULL_HANDLE;
        return fade_off("xrCreateSwapchain(%ux%u format %d): %s", SOMVR_FADE_W, SOMVR_FADE_H, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(X.fade_sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return fade_off("xrEnumerateSwapchainImages (fade): %s, %u images", xr_str(r), n), -1;
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(X.fade_sc, &ai, &xi)) || xi >= n)
        return fade_off("xrAcquireSwapchainImage (fade): %s, image %u of %u", xr_str(r), xi, n), -1;
    if ((r = X.xrWaitSwapchainImage(X.fade_sc, &wti)) != XR_SUCCESS)
        return fade_off("xrWaitSwapchainImage (fade): %s", xr_str(r)), -1;
    VkDeviceSize sz = (VkDeviceSize)SOMVR_FADE_W * SOMVR_FADE_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = NULL;
    int type = -1, in_flight = 0;
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &buf);
    if (vr != VK_SUCCESS) buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the upload";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &mem)) != VK_SUCCESS) mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, buf, mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, mem, 0, VK_WHOLE_SIZE, 0, &map)) != VK_SUCCESS)
        map = NULL;
    if (vr == VK_SUCCESS) {
        somvr_fade_image_fmt(map, f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB); /* HOST_COHERENT */
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd)) != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, im[xi].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                               1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_FADE_W, SOMVR_FADE_H, 1}}; /* tightly packed */
        ds->CmdCopyBufferToImage(cmd, buf, im[xi].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, im[xi].image, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its buffer, command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
        if (map) ds->UnmapMemory(ds->dev, mem);
        if (buf) ds->DestroyBuffer(ds->dev, buf, NULL);
        if (mem) ds->FreeMemory(ds->dev, mem, NULL);
    }
    if (vr != VK_SUCCESS) return fade_off("the image's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(X.fade_sc, &ri)))
        return fade_off("xrReleaseSwapchainImage (fade): %s", xr_str(r)), -1;
    X.fade_filled = 1;
    lg("xr: fade: image %ux%u format %d (%s swapchain, %u image(s)) filled once: %d black alpha levels, premultiplied, "
       "and the dark red ones (SOMVR_REC_FADE_RED)", SOMVR_FADE_W, SOMVR_FADE_H, f, stat ? "a static" : "an ordinary", n,
       SOMVR_FADE_LEVELS);
    return 0;
}

/* the record's own level in red (SOMVR_REC_FADE_RED), not a darker one of the environment's transition */
static int fade_red(uint32_t level) { return X.last_fade_red && level == X.last_fade; }

/* a stereo frame (or its reuse) about to end: its fade level (0..255) and eye poses -> X.fade_q for the next
 * xrEndFrame (X.fade_n = 1); nothing at level 0; red: the dark red ramp */
static void fade_frame(dev_st *ds, uint32_t level, const somvr_posef eye[2], int red) {
    X.fade_n = 0;
    int k = somvr_fade_index(level);
    if (!X.fade_on || !k || fade_fill(ds)) return;
    int32_t rc[4];
    somvr_posef p;
    somvr_fade_rect_tint(k, red, rc);
    somvr_fade_pose(eye, &p);
    X.fade_q = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                        .space = X.space,
                                        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                        .subImage = {X.fade_sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                        .size = {SOMVR_FADE_SIZE_M, SOMVR_FADE_SIZE_M}};
    memcpy(&X.fade_q.pose, &p, sizeof X.fade_q.pose);
    X.fade_n = 1;
    if (X.n_fade < 3 || X.n_fade % 600 == 0)
        lg("xr: fade level %u (ramp %d of %d, alpha %u) at %+.3f,%+.3f,%+.3f (%llu faded frames so far, %llu black)", level,
           k, SOMVR_FADE_LEVELS - 1, somvr_fade_alpha(k), p.p[0], p.p[1], p.p[2], (unsigned long long)X.n_fade,
           (unsigned long long)X.n_fade_black);
}

/* ---- comfort: the vignette and the snap blink (../somvr_comfort.h, docs/re-xr-pipeline.md 11; X.mx held, the
 * present thread). Every stereo frame from a record reads that frame's somvr_comfort entry: the rig's artificial
 * speeds -> the vignette's target x SOMVR_VIGNETTE -> filtered by display time -> a head-locked black ring (a quad in
 * a VIEW space, sized so the clear disc has the angle wanted; nothing while the ring lies outside the view), and the
 * entry's blink black (the game holds a snap turn until its frame is black) -> a second head-locked quad showing one
 * uniform cell. Both from one static alpha texture, filled once. Any failure: no comfort layers for the session. ---- */
static struct {
    somvr_comfort_shared *obj;   /* the preload's somvr_comfort (active, stereo path on), NULL = none: nothing drawn */
    int on;                      /* this session may draw (cf_off clears it, logged once) */
    somvr_vig_cfg vc;
    XrSwapchain sc;              /* the alpha texture, SOMVR_VIG_IMG_W x _H */
    int filled, stat;
    uint32_t nimg;
    VkFormat fmt;
    XrSpace view;                /* its own VIEW space (head-locked, composed at display time) */
    somvr_vig_state vs;
    int64_t t_last;              /* the display time of the last update (0: none yet) */
    float target, amount, blink, lin, ang, clear_deg;
    XrCompositionLayerQuad q[2]; /* this frame's comfort layers: [0] the ring (over the world, under the HUD and the
                                    pointer), [1] the blink (over them: only the fade's quad is above it); xr_comp */
    uint32_t has;                /* bit 0 the ring, bit 1 the blink: in this frame's composition */
    XrCompositionLayerQuad last_q[2];
    uint32_t last_has;           /* the last stereo frame's: a reuse frame repeats them with its image */
    int ring_was, blink_was;     /* the last frame had them (event lines when that changes) */
    int snap_tunnel;             /* SOMVR_VIGNETTE_SNAP (default 1): a snap turn's frame shows the tunnel at once */
    XrSwapchain sc_w;            /* build 12: the wraith tunnel's white-blue image (SOMVR_CFF_WRAITH), made at its first use */
    int filled_w, stat_w, wraith_was;
    int dump_wraith;             /* SOMVR_XR_DUMP_WRAITH=N (headless tests): a dump of the next frame at the first N dashes */
    int dump_marked;             /* SOMVR_XR_DUMP_MARKED=N (headless tests): a dump of the frame after each of the first N
                                    records the preload marked (SOMVR_REC_DUMP: the strike's moments) */
    uint32_t nimg_w;
    uint64_t n_wraith;
    uint64_t n_frames, n_ring, n_blink, n_missing, n_near, n_snap;
    uint32_t lines;
} CF;

static void cf_destroy(void) { /* its swapchain (a static image, its one upload waited) and VIEW space */
    if (CF.sc) X.xrDestroySwapchain(CF.sc);
    if (CF.sc_w) X.xrDestroySwapchain(CF.sc_w);
    if (CF.view) X.xrDestroySpace(CF.view);
    CF.sc = CF.sc_w = XR_NULL_HANDLE, CF.view = XR_NULL_HANDLE;
    CF.filled = CF.filled_w = 0, CF.has = CF.last_has = 0;
}

/* no comfort layers for the rest of this session (logged once); the stereo frames go on without them */
__attribute__((format(printf, 1, 2))) static void cf_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: comfort: off for this session: %s (the frames go on without the vignette and the blink; %llu ring, %llu "
       "blink frames so far)", why, (unsigned long long)CF.n_ring, (unsigned long long)CF.n_blink);
    CF.on = 0;
    cf_destroy();
}

/* xr_session_setup, after the other lookups: is there a vignette or a blink for this session? */
static void cf_setup(void) {
    cf_destroy();
    somvr_comfort_shared *c = (somvr_comfort_shared *)dlsym(RTLD_DEFAULT, SOMVR_COMFORT_SYMBOL);
    memset(&CF.vs, 0, sizeof CF.vs);
    CF.t_last = 0, CF.target = CF.amount = CF.blink = CF.lin = CF.ang = 0, CF.clear_deg = 0;
    CF.ring_was = CF.blink_was = 0;
    CF.n_frames = CF.n_ring = CF.n_blink = CF.n_missing = CF.n_near = CF.n_snap = 0, CF.lines = 0;
    const char *sv = getenv("SOMVR_VIGNETTE_SNAP");
    CF.snap_tunnel = !(sv && !strcmp(sv, "0"));
    const char *dw = getenv("SOMVR_XR_DUMP_WRAITH");
    CF.dump_wraith = dw && dw[0] ? atoi(dw) : 0;
    const char *dm = getenv("SOMVR_XR_DUMP_MARKED");
    CF.dump_marked = dm && dm[0] ? atoi(dm) : 0;
    somvr_vig_cfg_default(&CF.vc);
    const char *fv = getenv("SOMVR_VIGNETTE_FIT"); /* lens (build 10) | corner (build 9's, the default) */
    somvr_vig_cfg_fit(&CF.vc, fv && !strcmp(fv, "lens") ? SOMVR_VIG_FIT_LENS : SOMVR_VIG_FIT_CORNER);
    CF.obj = X.sh && somvr_comfort_ok(c) ? c : NULL;
    float s = CF.obj ? vm_vignette(CF.obj->vignette) : 0; /* the VR menu's strength, if it has one */
    CF.on = CF.obj && (s > 0 || CF.obj->blink_ms > 0 || vm_comfort_live()); /* the menu may turn them on later */
    if (!CF.on) {
        lg("xr: comfort: no vignette, no snap blink (%s)", !X.sh ? "no stereo path"
                                                           : !c ? "no somvr_comfort: a preload without it"
                                                           : !CF.obj ? "somvr_comfort inactive (SOMVR_VIGNETTE and SOMVR_SNAP_BLINK_MS unset) or another version"
                                                                     : "both settings 0");
        return;
    }
    const somvr_vig_cfg *v = &CF.vc;
    lg("xr: comfort: somvr_comfort found at %p (v%u, preload pid %d): vignette strength %.2f%s (at full speed, %.1f m/s or "
       "%.0f deg/s, the clear disc closes from the view's corners by %s of the way to %.1f deg; SOMVR_VIGNETTE_FIT=%s; "
       "nothing below %.1f m/s / %.0f deg/s; time constants %.2f s closing, %.2f s opening; a snap turn %s), snap blink "
       "%u ms%s; head-locked quads %.1f m ahead%s", (void *)CF.obj, CF.obj->version, CF.obj->pid, s, s > 0 ? "" : " (off)",
       v->lin_hi, v->ang_hi, v->fit == SOMVR_VIG_FIT_LENS ? "the square root of that share" : "that share", v->min_deg,
       v->fit == SOMVR_VIG_FIT_LENS ? "lens" : "corner", v->lin_lo, v->ang_lo, v->tau_in, v->tau_out,
       CF.snap_tunnel ? "closes it at once (SOMVR_VIGNETTE_SNAP=0: not)" : "does not (SOMVR_VIGNETTE_SNAP=0)",
       CF.obj->blink_ms, CF.obj->blink_ms ? "" : " (off)", v->dist_m, vm_comfort_live() ? "; the VR menu sets both live" : "");
}

/* The alpha texture's swapchain (a static image if the runtime takes one), filled once from a host buffer and
 * released (as the pointer's image). 0 = ready, -1 = comfort off. */
static int cf_fill_img(dev_st *ds, int w) { /* w = 1: the wraith tunnel's image (CF.sc_w), else the black one */
    if (w ? CF.filled_w : CF.filled) return 0;
    XrSwapchain *psc = w ? &CF.sc_w : &CF.sc;
    int *pstat = w ? &CF.stat_w : &CF.stat;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return cf_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return cf_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return cf_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    int bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = SOMVR_VIG_IMG_W,
                                .height = SOMVR_VIG_IMG_H,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    *pstat = 1;
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, psc))) { /* no static images: an ordinary one */
        ci.createFlags = 0, *pstat = 0;
        r = X.xrCreateSwapchain(X.session, &ci, psc);
    }
    if (XR_FAILED(r)) {
        *psc = XR_NULL_HANDLE;
        return cf_off("xrCreateSwapchain(%ux%u format %d): %s", SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(*psc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return cf_off("xrEnumerateSwapchainImages (comfort): %s, %u images", xr_str(r), n), -1;
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(*psc, &ai, &xi)) || xi >= n)
        return cf_off("xrAcquireSwapchainImage (comfort): %s, image %u of %u", xr_str(r), xi, n), -1;
    if ((r = X.xrWaitSwapchainImage(*psc, &wti)) != XR_SUCCESS)
        return cf_off("xrWaitSwapchainImage (comfort): %s", xr_str(r)), -1;
    /* the upload: a host buffer with the image, one copy into the acquired image, waited here (1 MB, once) */
    VkDeviceSize sz = (VkDeviceSize)SOMVR_VIG_IMG_W * SOMVR_VIG_IMG_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = NULL;
    int type = -1, in_flight = 0;
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &buf);
    if (vr != VK_SUCCESS) buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the upload";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &mem)) != VK_SUCCESS) mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, buf, mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, mem, 0, VK_WHOLE_SIZE, 0, &map)) != VK_SUCCESS)
        map = NULL;
    if (vr == VK_SUCCESS) {
        if (w) /* HOST_COHERENT: visible to the device at the submit */
            somvr_vig_image_wraith(map, bgra);
        else
            somvr_vig_image(map, bgra);
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd)) != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, im[xi].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                               1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_VIG_IMG_W, SOMVR_VIG_IMG_H, 1}}; /* tightly packed */
        ds->CmdCopyBufferToImage(cmd, buf, im[xi].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, im[xi].image, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its buffer, command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
        if (map) ds->UnmapMemory(ds->dev, mem);
        if (buf) ds->DestroyBuffer(ds->dev, buf, NULL);
        if (mem) ds->FreeMemory(ds->dev, mem, NULL);
    }
    if (vr != VK_SUCCESS) return cf_off("the image's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(*psc, &ri)))
        return cf_off("xrReleaseSwapchainImage (comfort): %s", xr_str(r)), -1;
    if (w)
        CF.filled_w = 1, CF.nimg_w = n;
    else
        CF.filled = 1, CF.fmt = f, CF.nimg = n;
    lg("xr: comfort: %simage %ux%u format %d (%s swapchain, %u image(s)) filled once: the ring (clear inside %.2f of the "
       "half-size, opaque from %.2f) and %d blink cells", w ? "the wraith tunnel's white-blue " : "", SOMVR_VIG_IMG_W,
       SOMVR_VIG_IMG_H, f, *pstat ? "a static" : "an ordinary", n, SOMVR_VIG_R0, SOMVR_VIG_R0 * SOMVR_VIG_K,
       SOMVR_VIG_CELLS);
    return 0;
}
static int cf_fill(dev_st *ds) { return cf_fill_img(ds, 0); }

static int cf_space(void) {
    if (CF.view) return 0;
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                     .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
                                     .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
    XrResult r = X.xrCreateReferenceSpace(X.session, &rs, &CF.view);
    if (XR_FAILED(r)) {
        CF.view = XR_NULL_HANDLE;
        return cf_off("xrCreateReferenceSpace(VIEW): %s", xr_str(r)), -1;
    }
    return 0;
}

/* a head-locked black quad: half-size h (tangent units) at the distance, showing rect rc of the texture */
static XrCompositionLayerQuad cf_quad(XrSwapchain sc, float h, const int32_t rc[4]) {
    float d = CF.vc.dist_m, w = 2 * h * d;
    return (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, /* premultiplied (black: either) */
                                    .space = CF.view,
                                    .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                    .subImage = {sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                    .pose = {{0, 0, 0, 1}, {0, 0, -d}},
                                    .size = {w, w}};
}

/* A stereo frame from record `rec`, right before its xrEndFrame: its comfort layers into the frame's composition (the
 * ring at XL_RING: over the world, under the HUD and the pointer; the blink at XL_BLINK: over them, under the fade) */
static void cf_layers(dev_st *ds, const somvr_rec *rec, xr_comp *c) {
    CF.has = 0;
    if (!CF.on) return;
    somvr_comfort_frame e;
    int have = somvr_comfort_get(CF.obj, rec->frame, &e), near = 0;
    if (!have) { /* overwritten or not written: the newest entry, if it is about this frame */
        uint64_t last = atomic_load_explicit(&CF.obj->last, memory_order_acquire);
        near = have = last && last + 4 >= rec->frame && rec->frame + 4 >= last && somvr_comfort_get(CF.obj, last, &e);
        CF.n_missing++, CF.n_near += near;
    }
    CF.n_frames++;
    float s = vm_vignette(CF.obj->vignette); /* the VR menu's strength, if it has one */
    int64_t now = X.fs.predictedDisplayTime;
    float dt = CF.t_last && now > CF.t_last ? (float)((double)(now - CF.t_last) * 1e-9) : 0;
    CF.t_last = now;
    if (have) CF.lin = e.lin_mps, CF.ang = e.ang_dps;
    CF.blink = have && e.blink > 0 ? (e.blink < 1 ? e.blink : 1) : 0;
    CF.target = s > 0 ? s * somvr_vig_target(&CF.vc, CF.lin, CF.ang) : 0;
    int snapped = have && (e.flags & SOMVR_CFF_SNAP) && !(e.flags & SOMVR_CFF_RECENTER) && CF.snap_tunnel && s > 0 &&
                  !CF.obj->blink_ms && !(CF.blink > 0); /* the snap blink's black hides it already */
    CF.amount = somvr_vig_step(&CF.vc, &CF.vs, CF.target, dt);
    if (snapped) somvr_vig_snap(&CF.vs, s), CF.amount = CF.vs.amount, CF.n_snap++;
    /* build 12: the wraith dash (SOMVR_CFF_WRAITH): the ring at least the tunnel's amount, white-blue; its blink white */
    int wr = have && (e.flags & SOMVR_CFF_WRAITH);
    float tun = wr ? somvr_cff_tunnel(e.flags) : 0, amt = tun > CF.amount ? tun : CF.amount;
    float fov[2][4], cov, cor;
    for (int k = 0; k < 2; k++) {
        somvr_fovf f = somvr_submit_fov(&rec->eye[k], rec->applied, k); /* exactly what the projection carries */
        fov[k][0] = f.l, fov[k][1] = f.r, fov[k][2] = f.u, fov[k][3] = f.d;
    }
    somvr_vig_extent(fov, &cov, &cor);
    somvr_vig_ring g = somvr_vig_geom(&CF.vc, amt, cov, cor);
    CF.clear_deg = g.clear_rad * 57.29578f;
    int ring = amt > 0 && g.clear_rad < cor; /* the ring reaches into the view */
    int cell = somvr_vig_cell(CF.blink), blink = cell > 0;
    if ((ring || blink) && (cf_fill(ds) || cf_space())) return; /* comfort is off now (logged) */
    if (wr && (ring || blink) && cf_fill_img(ds, 1)) return;     /* the wraith's image (its first use) */
    if (wr && (ring || blink)) CF.n_wraith++;
    if (wr != CF.wraith_was && CF.lines < 200)
        lg("xr: comfort frame %llu: the wraith dash %s (tunnel %.2f, blink %.2f; frames %llu)", (unsigned long long)rec->frame,
           wr ? "begins: the ring white-blue" : "ends", tun, CF.blink, (unsigned long long)CF.n_wraith);
    if (wr && !CF.wraith_was && CF.dump_wraith > 0) { /* SOMVR_XR_DUMP_WRAITH: the dash in a picture (xr_compose.py) */
        CF.dump_wraith--;
        lg("xr: comfort: the wraith dash at frame %llu: a dump of the next frame: %s", (unsigned long long)rec->frame,
           dump_next());
    }
    CF.wraith_was = wr;
    uint32_t nq = (uint32_t)ring + (uint32_t)blink;
    int32_t rc[4];
    if (ring) {
        somvr_vig_rect_ring(rc);
        CF.q[0] = cf_quad(wr ? CF.sc_w : CF.sc, g.half_tan, rc);
    }
    if (blink) {
        somvr_vig_rect_cell(cell, rc);
        CF.q[1] = cf_quad(wr ? CF.sc_w : CF.sc, g.cover_tan, rc);
    }
    CF.has = CF.last_has = (ring ? 1u : 0u) | (blink ? 2u : 0u);
    memcpy(CF.last_q, CF.q, sizeof CF.q);
    CF.n_ring += ring, CF.n_blink += blink;
    if (ring) atomic_fetch_add_explicit(&CF.obj->n_vignette, 1, memory_order_relaxed);
    if (blink) atomic_fetch_add_explicit(&CF.obj->n_blink, 1, memory_order_relaxed);
    int edge = ring != CF.ring_was || blink != CF.blink_was || snapped;
    CF.ring_was = ring, CF.blink_was = blink;
    if ((edge && CF.lines < 200) || CF.n_frames <= 2 || CF.n_frames % 600 == 0) {
        CF.lines += edge;
        float rim = somvr_vig_edge(fov);
        lg("xr: comfort frame %llu%s: speeds %.2f m/s %.1f deg/s (flags 0x%x) -> target %.3f, amount %.3f%s: %s (clear "
           "%.1f deg, black %.0f%% at the view's nearest edge %.1f deg and %.0f%% at 35 deg, view corner %.1f deg, quad "
           "%.1f m%s), blink %.2f%s (cell %d); layers %u (frames %llu, ring %llu, blink %llu, missing %llu, snap tunnels "
           "%llu)", (unsigned long long)rec->frame,
           !have ? " (no entry)" : near ? " (newest entry)" : "", CF.lin, CF.ang, have ? e.flags : 0u, CF.target, CF.amount,
           snapped ? " (a snap turn: the tunnel at once)" : "", ring ? "ring" : "no ring", CF.clear_deg,
           ring ? 100 * somvr_vig_dark(&g, rim) : 0.0f, rim * 57.29578f,
           ring ? 100 * somvr_vig_dark(&g, 35.0f / 57.29578f) : 0.0f, cor * 57.29578f,
           2 * g.half_tan * CF.vc.dist_m, g.clamped ? ", clamped" : "", CF.blink, blink ? " black" : "", cell, nq,
           (unsigned long long)CF.n_frames, (unsigned long long)CF.n_ring, (unsigned long long)CF.n_blink,
           (unsigned long long)CF.n_missing, (unsigned long long)CF.n_snap);
    }
    if (ring) c->l[XL_RING] = (const XrCompositionLayerBaseHeader *)&CF.q[0];
    if (blink) c->l[XL_BLINK] = (const XrCompositionLayerBaseHeader *)&CF.q[1];
}

/* a reuse frame (the last stereo frame's image again): its comfort layers again, unchanged, in their places */
static void cf_reuse_layers(xr_comp *c) {
    CF.has = CF.on ? CF.last_has : 0;
    memcpy(CF.q, CF.last_q, sizeof CF.q);
    if (CF.has & 1) c->l[XL_RING] = (const XrCompositionLayerBaseHeader *)&CF.q[0];
    if (CF.has & 2) c->l[XL_BLINK] = (const XrCompositionLayerBaseHeader *)&CF.q[1];
}

/* ---- threat cues (SOMVR_THREAT, docs/re-xr-pipeline.md 18, ../somvr_threat.h; X.mx held, the present thread): every
 * stereo frame from a record reads that frame's entry of the preload's `somvr_threat` (or the newest within 4 frames)
 * and adds up to five quads from one static 544x102 texture filled once (three rows of 16 intensity cells, warm,
 * premultiplied, mostly additive; the third row the hurt red, build 10): the in-view glows - world-locked in the reference space at the entry's points (the
 * frame's own rig mapped them), billboarded to the frame's head (its record's eye poses), right over the projection
 * and under comfort's ring - and the edge bands - head-locked in a VIEW space 1 m ahead at the view's left / right
 * edge (the record's fovs), over the ring and under the HUD quad. A reuse frame repeats the last ones. No object or
 * SOMVR_XR_THREAT=off: nothing (not a call). Any failure (format, swapchain, upload, VIEW space, xrEndFrame rejecting
 * them: the frame is ended again without them) = no threat cues for the session; the frames go on. ---------------- */
#define TH_EDGE_DIST_M 1.0f
static struct {
    somvr_threat_shared *obj;    /* the preload's somvr_threat (active, stereo path on), NULL = none: nothing drawn */
    int on;                      /* this session may draw (th_off clears it, logged once) */
    XrSwapchain sc;              /* the texture, SOMVR_TH_IMG_W x _H */
    int filled, stat;
    uint32_t nimg;
    VkFormat fmt;
    XrSpace view;                /* its own VIEW space (the edge bands) */
    XrCompositionLayerQuad q[SOMVR_TH_GLOWS + 2]; /* this frame's: [0..2] the glows (XL_THG0..), [3] [4] the edge bands */
    uint32_t has;                /* bit i: q[i] in this frame's composition */
    XrCompositionLayerQuad last_q[SOMVR_TH_GLOWS + 2];
    uint32_t last_has;           /* the last stereo frame's: a reuse frame repeats them */
    uint32_t was;                /* the last frame's `has` (event lines when it changes) */
    uint64_t n_frames, n_glow, n_edge, n_missing, n_near, n_hurt; /* n_hurt: frames with a red hurt band (build 10) */
    int dump_hurt;               /* SOMVR_XR_DUMP_HURT=N (headless tests): a dump of the next frame at each of the first N
                                  * hurt flashes (the red band's first frame) */
    int hurt_was;
    uint32_t lines;
} TH;

static void th_destroy(void) { /* its swapchain (a static image, its one upload waited) and VIEW space */
    if (TH.sc) X.xrDestroySwapchain(TH.sc);
    if (TH.view) X.xrDestroySpace(TH.view);
    TH.sc = XR_NULL_HANDLE, TH.view = XR_NULL_HANDLE;
    TH.filled = 0, TH.has = TH.last_has = TH.was = 0;
}

/* no threat cues for the rest of this session (logged once); the stereo frames go on without them */
__attribute__((format(printf, 1, 2))) static void th_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: threat: off for this session: %s (the frames go on without the glows and the edge bands; %llu glow, %llu "
       "edge frames so far)", why, (unsigned long long)TH.n_glow, (unsigned long long)TH.n_edge);
    TH.on = 0;
    th_destroy();
}

/* xr_session_setup, after comfort's: are there threat cues for this session? */
static void th_setup(void) {
    th_destroy();
    somvr_threat_shared *t = (somvr_threat_shared *)dlsym(RTLD_DEFAULT, SOMVR_THREAT_SYMBOL);
    TH.n_frames = TH.n_glow = TH.n_edge = TH.n_missing = TH.n_near = TH.n_hurt = 0, TH.lines = 0, TH.hurt_was = 0;
    const char *dh = getenv("SOMVR_XR_DUMP_HURT");
    TH.dump_hurt = dh && dh[0] ? atoi(dh) : 0;
    TH.obj = X.sh && cfg.th && somvr_threat_ok(t) ? t : NULL;
    TH.on = TH.obj != NULL;
    if (cfg.th_bad) lg("xr: threat: SOMVR_XR_THREAT=%s is not on or off: on", getenv("SOMVR_XR_THREAT"));
    if (!TH.on) {
        lg("xr: threat: no threat cues (%s)", !X.sh ? "no stereo path" : !cfg.th ? "SOMVR_XR_THREAT=off"
                                             : !t ? "no somvr_threat: a preload without it"
                                                  : "somvr_threat inactive (SOMVR_THREAT unset) or another version");
        return;
    }
    lg("xr: threat: somvr_threat found at %p (v%u, preload pid %d): glow strength %.2f, edge strength %.2f; up to %d "
       "world-locked glows over the projection, the edge bands head-locked %.1f m ahead over the ring",
       (void *)TH.obj, TH.obj->version, TH.obj->pid, TH.obj->glow, TH.obj->edge, SOMVR_TH_GLOWS, TH_EDGE_DIST_M);
}

/* The texture's swapchain (a static image if the runtime takes one), filled once from a host buffer and released (as
 * comfort's). 0 = ready, -1 = threat cues off. */
static int th_fill(dev_st *ds) {
    if (TH.filled) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return th_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return th_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return th_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    int bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = SOMVR_TH_IMG_W,
                                .height = SOMVR_TH_IMG_H,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    TH.stat = 1;
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &TH.sc))) { /* no static images: an ordinary one */
        ci.createFlags = 0, TH.stat = 0;
        r = X.xrCreateSwapchain(X.session, &ci, &TH.sc);
    }
    if (XR_FAILED(r)) {
        TH.sc = XR_NULL_HANDLE;
        return th_off("xrCreateSwapchain(%ux%u format %d): %s", SOMVR_TH_IMG_W, SOMVR_TH_IMG_H, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(TH.sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return th_off("xrEnumerateSwapchainImages (threat): %s, %u images", xr_str(r), n), -1;
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(TH.sc, &ai, &xi)) || xi >= n)
        return th_off("xrAcquireSwapchainImage (threat): %s, image %u of %u", xr_str(r), xi, n), -1;
    if ((r = X.xrWaitSwapchainImage(TH.sc, &wti)) != XR_SUCCESS)
        return th_off("xrWaitSwapchainImage (threat): %s", xr_str(r)), -1;
    /* the upload: a host buffer with the image, one copy into the acquired image, waited here (148 KB, once) */
    VkDeviceSize sz = (VkDeviceSize)SOMVR_TH_IMG_W * SOMVR_TH_IMG_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = NULL;
    int type = -1, in_flight = 0;
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &buf);
    if (vr != VK_SUCCESS) buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the upload";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &mem)) != VK_SUCCESS) mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, buf, mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, mem, 0, VK_WHOLE_SIZE, 0, &map)) != VK_SUCCESS)
        map = NULL;
    if (vr == VK_SUCCESS) {
        somvr_th_image(map, bgra); /* HOST_COHERENT: visible to the device at the submit */
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd)) != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, im[xi].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                               1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_TH_IMG_W, SOMVR_TH_IMG_H, 1}}; /* tightly packed */
        ds->CmdCopyBufferToImage(cmd, buf, im[xi].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, im[xi].image, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its buffer, command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
        if (map) ds->UnmapMemory(ds->dev, mem);
        if (buf) ds->DestroyBuffer(ds->dev, buf, NULL);
        if (mem) ds->FreeMemory(ds->dev, mem, NULL);
    }
    if (vr != VK_SUCCESS) return th_off("the image's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(TH.sc, &ri)))
        return th_off("xrReleaseSwapchainImage (threat): %s", xr_str(r)), -1;
    TH.filled = 1, TH.fmt = f, TH.nimg = n;
    lg("xr: threat: image %ux%u format %d (%s swapchain, %u image(s)) filled once: %d glow and %d edge cells", SOMVR_TH_IMG_W,
       SOMVR_TH_IMG_H, f, TH.stat ? "a static" : "an ordinary", n, SOMVR_TH_LEVELS, SOMVR_TH_LEVELS);
    return 0;
}

static int th_space(void) {
    if (TH.view) return 0;
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                     .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
                                     .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
    XrResult r = X.xrCreateReferenceSpace(X.session, &rs, &TH.view);
    if (XR_FAILED(r)) {
        TH.view = XR_NULL_HANDLE;
        return th_off("xrCreateReferenceSpace(VIEW): %s", xr_str(r)), -1;
    }
    return 0;
}

static XrCompositionLayerQuad th_quad(XrSpace space, int row, int cell, const float pose[7], float w, float h) {
    int32_t rc[4];
    somvr_th_rect(row, cell, rc);
    return (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, /* premultiplied */
                                    .space = space,
                                    .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                    .subImage = {TH.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                    .pose = {{pose[0], pose[1], pose[2], pose[3]}, {pose[4], pose[5], pose[6]}},
                                    .size = {w, h}};
}

/* A stereo frame from record `rec`, right before its xrEndFrame: its threat cues into the frame's composition (the
 * glows at XL_THG0..: over the world, under comfort's ring; the edge bands at XL_THE0/1: over the ring, under the HUD) */
static void th_layers(dev_st *ds, const somvr_rec *rec, xr_comp *c) {
    TH.has = 0;
    if (!TH.on) return;
    somvr_threat_frame e;
    int have = somvr_threat_get(TH.obj, rec->frame, &e), near = 0;
    if (!have) { /* overwritten or not written: the newest entry, if it is about this frame */
        uint64_t last = atomic_load_explicit(&TH.obj->last, memory_order_acquire);
        near = have = last && last + 4 >= rec->frame && rec->frame + 4 >= last && somvr_threat_get(TH.obj, last, &e);
        TH.n_missing++, TH.n_near += near;
    }
    TH.n_frames++;
    uint32_t has = 0;
    int gcell[SOMVR_TH_GLOWS] = {0}, ecell[2] = {0}, hurt[2] = {0, 0};
    for (uint32_t i = 0; have && i < e.nglow && i < SOMVR_TH_GLOWS; i++)
        if ((gcell[i] = somvr_th_cell(e.g[i].level)) > 0 && e.g[i].size_m > 0 && isfinite(e.g[i].pos[0]) &&
            isfinite(e.g[i].pos[1]) && isfinite(e.g[i].pos[2]))
            has |= 1u << i;
    for (int s = 0; have && s < 2; s++) { /* build 10: the hurt flash takes the side's band slot while it is the stronger */
        hurt[s] = somvr_th_hurt_side(e.hurt[s], e.edge[s], &ecell[s]);
        if (ecell[s] > 0) has |= 1u << (SOMVR_TH_GLOWS + s);
    }
    if (has && th_fill(ds)) return;                                       /* off now (logged) */
    if ((has >> SOMVR_TH_GLOWS) && th_space()) return;                    /* ... the edges' VIEW space */
    float head[3];
    for (int k = 0; k < 3; k++) head[k] = 0.5f * (rec->eye[0].pose.p[k] + rec->eye[1].pose.p[k]);
    for (int i = 0; i < SOMVR_TH_GLOWS; i++) {
        if (!(has & 1u << i)) continue;
        float pose[7];
        somvr_th_face(e.g[i].pos, head, pose);
        memcpy(pose + 4, e.g[i].pos, sizeof e.g[i].pos);
        TH.q[i] = th_quad(X.space, SOMVR_TH_ROW_GLOW, gcell[i], pose, e.g[i].size_m, e.g[i].size_m);
    }
    float fov[2][4];
    for (int k = 0; k < 2; k++) {
        somvr_fovf f = somvr_submit_fov(&rec->eye[k], rec->applied, k); /* exactly what the projection carries */
        fov[k][0] = f.l, fov[k][1] = f.r, fov[k][2] = f.u, fov[k][3] = f.d;
    }
    for (int s = 0; s < 2; s++) {
        if (!(has & 1u << (SOMVR_TH_GLOWS + s))) continue;
        float pose[7], size[2];
        somvr_th_edge_quad(fov, s ? SOMVR_TH_RIGHT : SOMVR_TH_LEFT, TH_EDGE_DIST_M, pose, size);
        TH.q[SOMVR_TH_GLOWS + s] = th_quad(TH.view, hurt[s] ? SOMVR_TH_ROW_HURT : SOMVR_TH_ROW_EDGE, ecell[s], pose, size[0],
                                           size[1]);
    }
    int hurt_now = (hurt[0] && (has & 1u << SOMVR_TH_GLOWS)) || (hurt[1] && (has & 1u << (SOMVR_TH_GLOWS + 1)));
    TH.n_hurt += hurt_now;
    if (hurt_now && !TH.hurt_was && TH.dump_hurt > 0) { /* SOMVR_XR_DUMP_HURT: the flash in a picture (xr_compose.py) */
        TH.dump_hurt--;
        lg("xr: threat: the hurt flash at frame %llu: a dump of the next frame: %s", (unsigned long long)rec->frame, dump_next());
    }
    TH.hurt_was = hurt_now;
    TH.has = TH.last_has = has;
    memcpy(TH.last_q, TH.q, sizeof TH.q);
    int glow = (has & ((1u << SOMVR_TH_GLOWS) - 1)) != 0, edge = (has >> SOMVR_TH_GLOWS) != 0;
    TH.n_glow += glow, TH.n_edge += edge;
    if (glow) atomic_fetch_add_explicit(&TH.obj->n_glow, 1, memory_order_relaxed);
    if (edge) atomic_fetch_add_explicit(&TH.obj->n_edge, 1, memory_order_relaxed);
    int change = has != TH.was;
    TH.was = has;
    if ((change && TH.lines < 300) || TH.n_frames <= 2 || TH.n_frames % 600 == 0) {
        TH.lines += change;
        lg("xr: threat frame %llu%s: %u glow(s) (cells %d %d %d, first at %+.2f,%+.2f,%+.2f m, %.2f m), edges L %.2f R %.2f "
           "(cells %d %d%s%s), flags 0x%x, threats %u -> layers 0x%x (frames %llu, glow %llu, edge %llu, hurt %llu, missing %llu)",
           (unsigned long long)rec->frame, !have ? " (no entry)" : near ? " (newest entry)" : "", have ? e.nglow : 0u,
           gcell[0], gcell[1], gcell[2], have && e.nglow ? e.g[0].pos[0] : 0.0f, have && e.nglow ? e.g[0].pos[1] : 0.0f,
           have && e.nglow ? e.g[0].pos[2] : 0.0f, have && e.nglow ? e.g[0].size_m : 0.0f, have ? e.edge[0] : 0.0f,
           have ? e.edge[1] : 0.0f, ecell[0], ecell[1], hurt[0] ? ", L hurt red" : "", hurt[1] ? ", R hurt red" : "",
           have ? e.flags : 0u, have ? e.threats : 0u, has, (unsigned long long)TH.n_frames, (unsigned long long)TH.n_glow,
           (unsigned long long)TH.n_edge, (unsigned long long)TH.n_hurt, (unsigned long long)TH.n_missing);
    }
    for (int i = 0; i < SOMVR_TH_GLOWS; i++)
        if (has & 1u << i) c->l[XL_THG0 + i] = (const XrCompositionLayerBaseHeader *)&TH.q[i];
    for (int s = 0; s < 2; s++)
        if (has & 1u << (SOMVR_TH_GLOWS + s)) c->l[XL_THE0 + s] = (const XrCompositionLayerBaseHeader *)&TH.q[SOMVR_TH_GLOWS + s];
}

/* a reuse frame (the last stereo frame's image again): its threat cues again, unchanged, in their places */
static void th_reuse_layers(xr_comp *c) {
    TH.has = TH.on ? TH.last_has : 0;
    memcpy(TH.q, TH.last_q, sizeof TH.q);
    for (int i = 0; i < SOMVR_TH_GLOWS; i++)
        if (TH.has & 1u << i) c->l[XL_THG0 + i] = (const XrCompositionLayerBaseHeader *)&TH.q[i];
    for (int s = 0; s < 2; s++)
        if (TH.has & 1u << (SOMVR_TH_GLOWS + s))
            c->l[XL_THE0 + s] = (const XrCompositionLayerBaseHeader *)&TH.q[SOMVR_TH_GLOWS + s];
}
/* ---- threat cues end ------------------------------------------------------------------------------------------- */

/* ---- SoM loading env (docs/re-xr-pipeline.md 14, ./somvr_env.h; X.mx held, the present thread): a mono frame shown
 * as the menu quad gets the procedural Mordor-dusk sky behind it - one XR_KHR_composition_layer_equirect2 layer if the
 * instance has the extension, else six world-locked quads (a 60 m box, its floor at floor height) - and in "world"
 * mode the last stereo frame too: its eye images copied into a static swapchain of ours at the switch, a projection
 * layer with that frame's poses and fovs (so it stays where it was while the head turns, cropped at its fov into the
 * sky), dimmed by one black quad per eye over exactly its view. The switches between the world and the environment go
 * through the fade quad: the shown side's last layers again while it darkens, then the other side from black. Any
 * failure: no environment for the session (the black void as before); a kept frame's failure: the sky alone. ------- */
static int xr_wait_publish(void);
static int xr_begin(void);
static int xr_end_comp(const xr_comp *c);
static void fade_comp(xr_comp *c);
static const XrCompositionLayerBaseHeader *proj_layer(XrCompositionLayerProjection *proj,
                                                      const XrCompositionLayerProjectionView *pv);
static struct {
    int on;                      /* this session draws the environment (env_off clears it, logged once) */
    int mode;                    /* SOMVR_ENV_ON or _WORLD */
    int kind;                    /* SOMVR_ENV_KIND_EQ (the instance has the equirect extension) or _CUBE */
    XrSwapchain sky_sc;          /* the static sky image (+ the dim strip), filled once */
    int sky_filled, sky_stat;
    VkFormat sky_fmt;
    XrCompositionLayerEquirect2KHR eq;
    XrCompositionLayerQuad face[6];
    /* world mode */
    int world_on;                /* frames may be kept this session (env_world_off clears it) */
    int src_ok;                  /* the last stereo frame came from the eye images: src, src_pv describe it */
    eyes_st src;
    somvr_xfer src_xfer;
    VkFormat src_fmt;
    XrCompositionLayerProjectionView src_pv[2];
    XrSwapchain world_sc;        /* the kept frame (2 x the eye width), made at the switch, gone at the next stereo one */
    int world_ok;                /* world_sc's one released image is the kept frame */
    XrCompositionLayerProjectionView world_pv[2];
    XrCompositionLayerProjection world_proj;
    XrCompositionLayerQuad dim[2];
    /* the transitions */
    somvr_env_trans tr;
    uint32_t level;              /* this frame's black for the fade quad (0..255) */
    XrCompositionLayerQuad last_quad; /* the last mono frame's menu quad (its image: X.sc's last released one) */
    int quad_ok;
    uint64_t quad_nsc;           /* X.n_sc when it was shown */
    somvr_posef head[2];         /* the last located eyes (the fade quad of mono and hold frames) */
    int have_head;
    uint32_t warned, lines;
    uint64_t n_frames, n_hold, n_kept;
} EV;

static void env_world_free(void) { /* the kept frame's swapchain (its one copy was waited) */
    if (EV.world_sc) X.xrDestroySwapchain(EV.world_sc);
    EV.world_sc = XR_NULL_HANDLE;
    EV.world_ok = 0;
}
static void env_destroy(void) { /* both swapchains (the sky's one upload was waited); nothing drawn until env_setup */
    env_world_free();
    if (EV.sky_sc) X.xrDestroySwapchain(EV.sky_sc);
    EV.sky_sc = XR_NULL_HANDLE;
    EV.sky_filled = EV.quad_ok = 0;
    EV.on = EV.world_on = 0;
}

/* no environment for the rest of this session (logged once); mono frames go on as before */
__attribute__((format(printf, 1, 2))) static void env_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: env: off for this session: %s (mono frames go on in the black void; %llu environment frames so far)", why,
       (unsigned long long)EV.n_frames);
    env_destroy();
}
/* no kept frames for the rest of this session (logged once); the sky goes on */
__attribute__((format(printf, 1, 2))) static int env_world_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: env: world off for this session: %s (the sky alone behind mono frames; %llu frames kept so far)", why,
       (unsigned long long)EV.n_kept);
    EV.world_on = 0;
    env_world_free();
    return -1;
}

/* xr_session_setup, after the other lookups: is the environment on for this session? */
static void env_setup(void) {
    env_destroy();
    memset(&EV.tr, 0, sizeof EV.tr);
    EV.mode = ENVC.mode, EV.kind = ENVC.equirect ? SOMVR_ENV_KIND_EQ : SOMVR_ENV_KIND_CUBE;
    EV.on = EV.mode != SOMVR_ENV_OFF && cfg.layer == SOMVR_LAYER_QUAD;
    EV.world_on = EV.on && EV.mode == SOMVR_ENV_WORLD;
    EV.src_ok = EV.have_head = 0, EV.level = 0, EV.warned = EV.lines = 0;
    EV.n_frames = EV.n_hold = EV.n_kept = 0;
    if (ENVC.bad) lg("xr: env: SOMVR_XR_ENV=%s is not off, on or world: off", getenv("SOMVR_XR_ENV"));
    if (ENVC.dim_bad) lg("xr: env: SOMVR_XR_ENV_DIM=%s is not 0..1: %.2f", getenv("SOMVR_XR_ENV_DIM"), ENVC.dim);
    if (!EV.on) {
        lg("xr: env: off (%s)", EV.mode == SOMVR_ENV_OFF ? "SOMVR_XR_ENV unset or off: the black void behind mono frames"
                                                         : "SOMVR_XR_LAYER=proj: the mono frames cover the view");
        return;
    }
    char kept[96] = "";
    if (EV.world_on)
        snprintf(kept, sizeof kept, "; world: the last stereo frame kept behind them at %.0f %%", 100.0 * ENVC.dim);
    lg("xr: env: %s: the Mordor-dusk sky behind mono frames as %s%s; switches through the fade quad (out %.0f ms, in %.0f "
       "ms%s)", EV.world_on ? "world" : "on",
       EV.kind == SOMVR_ENV_KIND_EQ ? "an XR_KHR_composition_layer_equirect2 layer (radius 30 m)"
       : ENVC.offered               ? "6 world-locked quads (the instance refused XR_KHR_composition_layer_equirect2)"
                                    : "6 world-locked quads (the runtime has no XR_KHR_composition_layer_equirect2: a "
                                      "60 m box, its floor 1.6 m below the centre)",
       kept, 1000.0 * SOMVR_ENV_OUT_S, 1000.0 * SOMVR_ENV_IN_S, X.fade_on ? "" : "; SOMVR_XR_FADE off or no stereo: cuts");
}

/* The sky's swapchain (a static image if the runtime takes one), filled once from a host buffer and released (as the
 * pointer's image), and its layers: the equirect or the six faces, in the space, centred at its origin (STAGE: at the
 * eye height above the floor). 0 = ready, -1 = the environment is off now. */
static int env_sky_fill(dev_st *ds) {
    if (EV.sky_filled) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return env_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return env_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return env_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    int bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    int srgb = f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_SRGB;
    const uint32_t w = somvr_env_img_w(EV.kind), h = somvr_env_img_h(EV.kind);
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = w,
                                .height = h,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    EV.sky_stat = 1;
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &EV.sky_sc))) { /* no static images: an ordinary one */
        ci.createFlags = 0, EV.sky_stat = 0;
        r = X.xrCreateSwapchain(X.session, &ci, &EV.sky_sc);
    }
    if (XR_FAILED(r)) {
        EV.sky_sc = XR_NULL_HANDLE;
        return env_off("xrCreateSwapchain(%ux%u format %d): %s", w, h, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(EV.sky_sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return env_off("xrEnumerateSwapchainImages (sky): %s, %u images", xr_str(r), n), -1;
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(EV.sky_sc, &ai, &xi)) || xi >= n)
        return env_off("xrAcquireSwapchainImage (sky): %s, image %u of %u", xr_str(r), xi, n), -1;
    if ((r = X.xrWaitSwapchainImage(EV.sky_sc, &wti)) != XR_SUCCESS)
        return env_off("xrWaitSwapchainImage (sky): %s", xr_str(r)), -1;
    /* the upload: a host buffer with the image, one copy into the acquired image, waited here (2 MB, once) */
    VkDeviceSize sz = (VkDeviceSize)w * h * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = NULL;
    int type = -1, in_flight = 0;
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &buf);
    if (vr != VK_SUCCESS) buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the upload";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &mem)) != VK_SUCCESS) mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, buf, mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, mem, 0, VK_WHOLE_SIZE, 0, &map)) != VK_SUCCESS)
        map = NULL;
    if (vr == VK_SUCCESS) {
        somvr_env_image(map, EV.kind, bgra, srgb, ENVC.dim); /* HOST_COHERENT: visible to the device at the submit */
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd)) != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, im[xi].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                               1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {w, h, 1}}; /* tightly packed */
        ds->CmdCopyBufferToImage(cmd, buf, im[xi].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, im[xi].image, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its buffer, command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
        if (map) ds->UnmapMemory(ds->dev, mem);
        if (buf) ds->DestroyBuffer(ds->dev, buf, NULL);
        if (mem) ds->FreeMemory(ds->dev, mem, NULL);
    }
    if (vr != VK_SUCCESS) return env_off("the sky's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(EV.sky_sc, &ri)))
        return env_off("xrReleaseSwapchainImage (sky): %s", xr_str(r)), -1;
    EV.sky_filled = 1, EV.sky_fmt = f;
    /* the layers: world-locked in the space, the centre at its origin (a STAGE origin is on the floor) */
    float cy = X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? SOMVR_ENV_EYE_M : 0.0f;
    int32_t rc[4];
    if (EV.kind == SOMVR_ENV_KIND_EQ) {
        somvr_env_sky_rect(EV.kind, 0, rc);
        /* the angles just inside 2 pi and pi/2 (float rounding must not land outside the extension's ranges) */
        EV.eq = (XrCompositionLayerEquirect2KHR){.type = XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR,
                                                 .space = X.space,
                                                 .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                                 .subImage = {EV.sky_sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                                 .pose = {{0, 0, 0, 1}, {0, cy, 0}},
                                                 .radius = SOMVR_ENV_RADIUS_M,
                                                 .centralHorizontalAngle = 6.2831850f,
                                                 .upperVerticalAngle = 1.5707962f,
                                                 .lowerVerticalAngle = -1.5707962f};
    } else {
        for (int i = 0; i < 6; i++) {
            somvr_env_face fc;
            somvr_env_face_get(i, &fc);
            somvr_env_sky_rect(EV.kind, i, rc);
            EV.face[i] = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                                  .space = X.space,
                                                  .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                                  .subImage = {EV.sky_sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                                  .size = {2 * fc.hw, 2 * fc.hh}};
            memcpy(&EV.face[i].pose, &fc.pose, sizeof EV.face[i].pose);
            EV.face[i].pose.position.y += cy;
        }
    }
    lg("xr: env: sky image %ux%u format %d (%s swapchain, %u image(s)) filled once: %s, dim cell alpha %u; centre "
       "%+.2f m above the %s origin", w, h, f, EV.sky_stat ? "a static" : "an ordinary", n,
       EV.kind == SOMVR_ENV_KIND_EQ ? "an equirect map 1024x512" : "6 cube tiles 256x256", (unsigned)lrintf(255 * (1 - ENVC.dim)),
       cy, X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE" : "LOCAL");
    return 0;
}

/* the eye images of the last stereo frame: still made by this device at that size (not destroyed meanwhile) */
static int env_eyes_alive(VkDevice dev, const eyes_st *ey) {
    int found = 0;
    pthread_mutex_lock(&g_cand_mx);
    for (int e = 0; e < 2; e++)
        for (int i = 0; i < MAX_EYE_CAND; i++)
            if (ey->img[e] && g_cand[i].img == ey->img[e] && g_cand[i].dev == dev && g_cand[i].w == ey->w &&
                g_cand[i].h == ey->h) {
                found |= 1 << e;
                break;
            }
    pthread_mutex_unlock(&g_cand_mx);
    return found == 3;
}

/* a whole stereo frame was just submitted (its views pv; ey = its eye images, NULL: layout A): world mode keeps its
 * description for the next switch to the environment */
static void env_stereo_frame(const eyes_st *ey, const XrCompositionLayerProjectionView pv[2]) {
    if (!EV.world_on) return;
    EV.src_ok = ey != NULL;
    if (ey) EV.src = *ey, EV.src_xfer = X.xfer, EV.src_fmt = X.sc_fmt;
    memcpy(EV.src_pv, pv, sizeof EV.src_pv);
}

/* World mode, the switch to the environment: the last stereo frame's eye images -> a new static swapchain of ours (2 x
 * the eye width, the XR image's format; copy or blit as that frame), one submit waited here (the eye images taken from
 * and returned to TRANSFER_DST, as eyes_submit); then its projection layer and the dim quads. A frame not from the eye
 * images, or images gone: the sky alone this time. Any XR/Vulkan failure: env_world_off. */
static int env_world_freeze(dev_st *ds) {
    if (!EV.world_on) return 0;
    if (!EV.src_ok) {
        if (!(EV.warned & 1))
            lg("xr: env: world: the last stereo frame did not come from the eye images (layout A, or none yet): the sky "
               "alone (logged once)");
        EV.warned |= 1;
        return 0;
    }
    if (!env_eyes_alive(ds->dev, &EV.src)) {
        lg("xr: env: world: the last stereo frame's eye images %#llx/%#llx are gone: the sky alone this time",
           (unsigned long long)(uintptr_t)EV.src.img[0], (unsigned long long)(uintptr_t)EV.src.img[1]);
        EV.src_ok = 0;
        return 0;
    }
    if (!ds->pool || !ds->CmdCopyImage || !ds->CmdBlitImage) return env_world_off("the driver lacks a copy entry point");
    const uint32_t ew = EV.src.w, w = 2 * ew, h = EV.src.h;
    env_world_free();
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = EV.src_fmt,
                                .sampleCount = 1,
                                .width = w,
                                .height = h,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    int stat = 1;
    XrResult r = X.xrCreateSwapchain(X.session, &ci, &EV.world_sc);
    if (XR_FAILED(r)) ci.createFlags = 0, stat = 0, r = X.xrCreateSwapchain(X.session, &ci, &EV.world_sc);
    if (XR_FAILED(r)) {
        EV.world_sc = XR_NULL_HANDLE;
        return env_world_off("xrCreateSwapchain(%ux%u format %d): %s", w, h, EV.src_fmt, xr_str(r));
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    uint32_t n = 0, xi = 0;
    r = X.xrEnumerateSwapchainImages(EV.world_sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return env_world_off("xrEnumerateSwapchainImages (world): %s, %u images", xr_str(r), n);
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(EV.world_sc, &ai, &xi)) || xi >= n)
        return env_world_off("xrAcquireSwapchainImage (world): %s, image %u of %u", xr_str(r), xi, n);
    if ((r = X.xrWaitSwapchainImage(EV.world_sc, &wti)) != XR_SUCCESS)
        return env_world_off("xrWaitSwapchainImage (world): %s", xr_str(r));
    VkImage dst = im[xi].image;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int in_flight = 0;
    const char *what = "vkAllocateCommandBuffers";
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ds->pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkResult vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd);
    if (vr != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b[3];
        for (int e = 0; e < 2; e++)
            img_barrier(&b[e], EV.src.img[e], VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        img_barrier(&b[2], dst, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                               NULL, 3, b);
        VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        for (int e = 0; e < 2; e++) {
            int32_t x0 = (int32_t)((uint32_t)e * ew);
            if (EV.src_xfer == SOMVR_XFER_COPY) {
                VkImageCopy c = {.srcSubresource = sub, .dstSubresource = sub, .dstOffset = {x0, 0, 0}, .extent = {ew, h, 1}};
                ds->CmdCopyImage(cmd, EV.src.img[e], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            } else {
                VkImageBlit bl = {.srcSubresource = sub,
                                  .srcOffsets = {{0, 0, 0}, {(int32_t)ew, (int32_t)h, 1}},
                                  .dstSubresource = sub,
                                  .dstOffsets = {{x0, 0, 0}, {x0 + (int32_t)ew, (int32_t)h, 1}}};
                ds->CmdBlitImage(cmd, EV.src.img[e], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_NEAREST);
            }
        }
        for (int e = 0; e < 2; e++)
            img_barrier(&b[e], EV.src.img[e], VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        img_barrier(&b[2], dst, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 3, b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
    }
    if (vr != VK_SUCCESS) return env_world_off("the kept frame's copy: %s: %d", what, vr);
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(EV.world_sc, &ri)))
        return env_world_off("xrReleaseSwapchainImage (world): %s", xr_str(r));
    /* its layers: the frame's own views (the rects as in the stereo image: the same layout), the dim quads */
    memcpy(EV.world_pv, EV.src_pv, sizeof EV.world_pv);
    for (int e = 0; e < 2; e++) EV.world_pv[e].subImage.swapchain = EV.world_sc;
    int32_t rc[4];
    somvr_env_dim_rect(EV.kind, rc);
    for (int e = 0; e < 2; e++) {
        somvr_posef ep, qp;
        somvr_fovf fv;
        float sz[2];
        memcpy(&ep, &EV.src_pv[e].pose, sizeof ep);
        memcpy(&fv, &EV.src_pv[e].fov, sizeof fv);
        somvr_env_dim_quad(&ep, &fv, SOMVR_ENV_DIM_DIST_M, SOMVR_ENV_DIM_MARGIN, &qp, sz);
        EV.dim[e] = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                             .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                             .space = X.space,
                                             .eyeVisibility = e ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT,
                                             .subImage = {EV.sky_sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                             .size = {sz[0], sz[1]}};
        memcpy(&EV.dim[e].pose, &qp, sizeof EV.dim[e].pose);
    }
    EV.world_ok = 1;
    EV.n_kept++;
    lg("xr: env: world kept: the eye images %#llx/%#llx (%ux%u format %d, gen %u) %s into a %ux%u swapchain (%s, format "
       "%d), at %.0f %% behind mono frames until the next stereo frame (%llu kept so far)",
       (unsigned long long)(uintptr_t)EV.src.img[0], (unsigned long long)(uintptr_t)EV.src.img[1], ew, h, EV.src.fmt,
       EV.src.gen, EV.src_xfer == SOMVR_XFER_COPY ? "copied" : "blitted", w, h, stat ? "static" : "ordinary", EV.src_fmt,
       100.0 * ENVC.dim, (unsigned long long)EV.n_kept);
    return 0;
}

/* the environment's layers for this frame into the composition's XL_ENV slots, under the frame's own (returns how
 * many; xr_end_comp drops them as one part, XG_ENV): the sky, then (world mode, a kept frame) its projection and the
 * dim quads */
static uint32_t env_scene(dev_st *ds, xr_comp *c) {
    if (!EV.on || env_sky_fill(ds)) return 0;
    const XrCompositionLayerBaseHeader **out = c->l + XL_ENV0;
    uint32_t n = 0;
    if (EV.kind == SOMVR_ENV_KIND_EQ) out[n++] = (const XrCompositionLayerBaseHeader *)&EV.eq;
    else
        for (int i = 0; i < 6; i++) out[n++] = (const XrCompositionLayerBaseHeader *)&EV.face[i];
    if (EV.world_ok) {
        out[n++] = proj_layer(&EV.world_proj, EV.world_pv);
        if (ENVC.dim < 1.0f)
            for (int e = 0; e < 2; e++) {
                EV.dim[e].subImage.swapchain = EV.sky_sc;
                out[n++] = (const XrCompositionLayerBaseHeader *)&EV.dim[e];
            }
    }
    return n; /* at most 6 + 1 + 2 = XL_ENV_N */
}

/* the eyes at this frame's display time (the fade quad of mono and hold frames); 1 = EV.head holds them (or the last
 * good ones) */
static int env_head(void) {
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = X.fs.predictedDisplayTime,
                           .space = X.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t nv = 0;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
    if (XR_SUCCEEDED(r) && nv == 2 && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        int pos = (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
        for (int e = 0; e < 2; e++) {
            memcpy(EV.head[e].q, &v[e].pose.orientation, sizeof EV.head[e].q);
            if (pos || !EV.have_head) memcpy(EV.head[e].p, &v[e].pose.position, sizeof EV.head[e].p);
        }
        EV.have_head = 1;
    }
    return EV.have_head;
}

/* a mono or hold frame's black: the fade quad at the head (nothing at 0, or with the fade off) */
static void env_fade(dev_st *ds) {
    if (EV.level && X.fade_on && env_head()) fade_frame(ds, EV.level, EV.head, 0);
}

/* a stereo or reuse frame's fade level: its record's, or the transition's if darker */
static uint32_t env_level(uint32_t rec_level) { return EV.on && EV.level > rec_level ? EV.level : rec_level; }

/* a mono frame shown as the menu quad (its image released, the quad built): its environment layers into the
 * composition (under the quad), its fade; the quad is remembered for the hold frames */
static uint32_t env_mono_layers(dev_st *ds, const XrCompositionLayerQuad *quad, xr_comp *c) {
    EV.last_quad = *quad, EV.quad_ok = 1, EV.quad_nsc = X.n_sc;
    uint32_t n = env_scene(ds, c);
    vd_space_add(ds, c, n, 0, 1); /* never a void: the loading space's floor, the lights fading (SOMVR_XR_LOADSPACE) */
    env_fade(ds);
    EV.n_frames++;
    if (n && (EV.n_frames <= 2 || EV.n_frames % 600 == 0))
        lg("xr: env frame %llu: %u layer(s) behind the menu quad (%s%s), black %u (holds %llu, switches %llu, kept %llu)",
           (unsigned long long)EV.n_frames, n, EV.kind == SOMVR_ENV_KIND_EQ ? "the equirect" : "6 quads",
           EV.world_ok ? ", the kept frame, 2 dim quads" : "", EV.level, (unsigned long long)EV.n_hold,
           (unsigned long long)EV.tr.switches, (unsigned long long)EV.n_kept);
    return n;
}

/* hold frames: the shown side's last layers again (no copy; the present goes on with the app's semaphores) */
static void env_hold_world(dev_st *ds) { /* as a reuse frame: the last stereo frame, its HUD quad, comfort's layers */
    if (xr_begin()) return;
    XrCompositionLayerProjection proj;
    xr_comp c = {.l = {[XL_OWN] = proj_layer(&proj, X.last_pv)}};
    if (X.last_hud && X.hud_sc) c.l[XL_HUD] = (const XrCompositionLayerBaseHeader *)&X.last_quad;
    mb_layers(&c); /* SOMVR_XR_MENU_BACK: a menu board's backdrop */
    uint32_t level = env_level(X.last_fade);
    if (level && X.fade_on) fade_frame(ds, level, env_head() ? EV.head : X.last_fade_eye, fade_red(level));
    cf_reuse_layers(&c);
    fade_comp(&c);
    xr_end_comp(&c);
}
static void env_hold_env(dev_st *ds) { /* the environment and the last menu quad (or the loading space without one) */
    if (xr_begin()) return;
    xr_comp c = {.l = {[XL_OWN] = vd_board_ok() ? (const XrCompositionLayerBaseHeader *)&EV.last_quad : NULL}};
    vd_space_add(ds, &c, env_scene(ds, &c), 0, 1); /* never a void: the floor, the lights fading; no board: the top layer */
    vd_hands(ds, &c, 0);                           /* never a void: our hands from the layer's input */
    env_fade(ds);
    fade_comp(&c);
    xr_end_comp(&c);
}

/* xr_frame, once this present's kind is known (before any copy): the transition's step. 1 = this present was a hold
 * frame (or XR died): nothing more to do; 0 = draw it as usual, at EV.level's black. A switch to the environment keeps
 * the last stereo frame (world mode); a switch to the world frees it. */
static int env_decide(dev_st *ds, somvr_submit_kind kind) {
    if (!X.waited && xr_wait_publish()) return 1; /* xr_fail: pass-through from here */
    int cover = kind != SOMVR_SUBMIT_MONO && cv_space_want(); /* SOMVR_XR_COVER: the game's in-world cover = a load */
    int want = kind == SOMVR_SUBMIT_MONO || cover ? SOMVR_ENV_SHOW_ENV : SOMVR_ENV_SHOW_WORLD;
    int shown = EV.tr.started ? EV.tr.shown : want;
    int can_hold = shown == SOMVR_ENV_SHOW_WORLD ? X.last_stereo
                                                 : vd_env_holdable(); /* the board, or the keep-alive's loading space */
    int sw = 0, act = somvr_env_step(&EV.tr, want, can_hold, X.fs.predictedDisplayTime, &sw);
    EV.level = somvr_env_level(&EV.tr);
    if (sw && EV.tr.switches) { /* not the session's first frame */
        if (EV.lines < 200)
            lg("xr: env: %s -> %s after %u hold frame(s)%s (switch %llu)",
               EV.tr.shown == SOMVR_ENV_SHOW_ENV ? "world" : "environment",
               EV.tr.shown == SOMVR_ENV_SHOW_ENV ? "environment" : "world", EV.tr.holds,
               can_hold ? "" : " (nothing to show again: at once, from black)", (unsigned long long)EV.tr.switches);
        EV.lines++;
        if (EV.tr.shown == SOMVR_ENV_SHOW_ENV && cover) cv_space_up(); /* the loading space, no board */
        else if (EV.tr.shown == SOMVR_ENV_SHOW_ENV) env_world_freeze(ds);
        else env_world_free(), vd_world_shown();
    }
    if (act != SOMVR_ENVT_HOLD) return 0;
    EV.n_hold++;
    if (EV.tr.shown == SOMVR_ENV_SHOW_WORLD) env_hold_world(ds);
    else env_hold_env(ds);
    return 1;
}

/* ---- the VR settings menu (../somvr_vrmenu.h, docs/re-xr-pipeline.md 15; X.mx held, the present thread). Every frame
 * (stereo, reuse, mono) with the preload's active somvr_vrmenu and live input: the open/close gesture on the off hand
 * (somvr_vm_gesture: the palm toward the eyes held 0.5 s, the menu button's long press the game side counts, looking
 * away, the hand lost, the close box), the panel's pose on that wrist, the main hand's laser pointer on it (a pointer
 * state of its own, target SOMVR_PTRQ_VRMENU; the game's pointer has no target meanwhile), the clicks (somvr_vm_apply;
 * a change published with gen + 1: the game thread applies it and saves it), haptic ticks on the pointing hand. The
 * panel's picture (somvr_vm_draw) goes into a mapped staging buffer and is uploaded into its own swapchain only when it
 * changes. Its layers (the panel, the ray, the dot from the pointer's image) come after the pointer's, before comfort's.
 * Any failure: no panel for the rest of the session (the settings chosen so far stay). ---- */
static struct {
    somvr_vrmenu_shared *obj;    /* the preload's object (active, with somvr_input), NULL = none: no menu at all */
    int on;                      /* this session shows the panel (vm_off clears it, logged once) */
    somvr_settings set;          /* the menu's values: the object's, changed here by the clicks */
    uint32_t gen_seen, toggles_seen;
    int main_hand;               /* the pointing hand (set.hand 0: the right); the panel is on the other wrist */
    somvr_font font;
    somvr_vm_gcfg gc;
    somvr_vm_gstate gs;
    somvr_ptr_cfg pc;
    somvr_ptr_state ptr;
    somvr_vm_hit hover;
    int page;                    /* SOMVR_VMPG_*: the settings or the practice page (its tab; v3) */
    /* the panel's texture: its swapchain, the staging buffer (mapped, HOST_COHERENT), the upload's command buffer */
    dev_st *ds;
    XrSwapchain sc;
    uint32_t nimg;
    VkImage img[MAX_XR_IMAGES];
    VkFormat fmt;
    int bgra;
    int released;                /* an image showing `drawn` is released */
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkCommandBuffer cmd;
    VkFence fence;
    int pending;                 /* the fence's upload is submitted and not waited yet */
    somvr_vm_view drawn;
    XrCompositionLayerQuad q[3]; /* this frame's layers: the panel, the ray, the dot */
    uint32_t nq;                 /* how many of them the next xrEndFrame carries (xr_end_comp clears it) */
    uint64_t n_frames, n_uploads, n_clicks, n_haptics;
    uint32_t lines;
} VM;

static const char *const vm_part_name[] = {"-", "row", "<", ">", "button"};

/* the panel's GPU and XR objects go (an upload still in flight is waited, 1 s cap; if that fails its objects leak) */
static void vm_free(void) {
    dev_st *ds = VM.ds;
    if (ds && VM.pending && VM.fence && ds->WaitForFences(ds->dev, 1, &VM.fence, VK_TRUE, FENCE_TIMEOUT_NS) == VK_SUCCESS)
        VM.pending = 0;
    if (ds && !VM.pending) {
        if (VM.cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &VM.cmd);
        if (VM.fence) ds->DestroyFence(ds->dev, VM.fence, NULL);
        if (VM.map) ds->UnmapMemory(ds->dev, VM.mem);
        if (VM.buf) ds->DestroyBuffer(ds->dev, VM.buf, NULL);
        if (VM.mem) ds->FreeMemory(ds->dev, VM.mem, NULL);
    }
    VM.cmd = VK_NULL_HANDLE, VM.fence = VK_NULL_HANDLE, VM.map = NULL, VM.buf = VK_NULL_HANDLE;
    VM.mem = VK_NULL_HANDLE, VM.pending = 0, VM.ds = NULL;
    if (VM.sc) X.xrDestroySwapchain(VM.sc); /* its acquired image, if any, goes with it */
    VM.sc = XR_NULL_HANDLE, VM.nimg = 0, VM.released = 0, VM.nq = 0;
}
static void vm_publish_closed(void) {
    if (!VM.obj) return;
    atomic_store_explicit(&VM.obj->consume, 0, memory_order_release);
    atomic_store_explicit(&VM.obj->open, 0, memory_order_release);
}
/* the session goes: the panel's objects (ours alone, before the session), nothing published */
static void vm_destroy(void) {
    vm_free();
    vm_publish_closed();
    VM.on = 0, VM.obj = NULL, VM.gs.open = 0;
}

/* no panel for the rest of this session (logged once); the settings chosen so far stay in the object */
__attribute__((format(printf, 1, 2))) static void vm_off(const char *fmt, ...) {
    char why[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    lg("xr: vrmenu: off for this session: %s (no wrist panel; the settings stay as they are; %llu opens, %llu frames "
       "shown, %llu clicks)", why, (unsigned long long)VM.gs.opens, (unsigned long long)VM.n_frames,
       (unsigned long long)VM.n_clicks);
    VM.on = 0, VM.gs.open = 0;
    vm_publish_closed();
    vm_free();
}

static void vm_set_line(const somvr_settings *s, char *buf, size_t cap) {
    size_t u = 0;
    buf[0] = 0;
    for (int k = 0; k < SOMVR_SK_N && u < cap; k++) {
        char v[32];
        somvr_set_get(s, k, v, sizeof v);
        u += (size_t)snprintf(buf + u, cap - u, "%s%s=%s", k ? " " : "", somvr_set_key(k), v);
    }
}

/* xr_session_setup, after somvr_input, the HUD, the pointer and the fade, before comfort (which asks vm_comfort_live) */
static void vm_setup(void) {
    vm_free();
    VM.obj = NULL, VM.on = 0, VM.nq = 0, VM.lines = 0;
    VM.n_frames = VM.n_uploads = VM.n_clicks = VM.n_haptics = 0;
    if (cfg.vm_bad) lg("xr: vrmenu: SOMVR_XR_VRMENU=%s is not on or off: on", getenv("SOMVR_XR_VRMENU"));
    somvr_vrmenu_shared *m = (somvr_vrmenu_shared *)dlsym(RTLD_DEFAULT, SOMVR_VRMENU_SYMBOL);
    const char *no = !m ? "no somvr_vrmenu: a preload without it"
                     : !somvr_vrmenu_ok(m) ? "somvr_vrmenu inactive (SOMVR_VRMENU unset) or another version"
                     : !I.in ? "no somvr_input (the menu needs SOMVR_INPUT=1)" : NULL;
    if (no) {
        lg("xr: vrmenu: no VR menu (%s)", no);
        return;
    }
    somvr_settings s;
    int tries = 0;
    while (!somvr_vrmenu_read(m, &s) && ++tries < 100) {}
    if (tries == 100) {
        lg("xr: vrmenu: no VR menu (the settings in somvr_vrmenu are being rewritten)");
        return;
    }
    VM.obj = m, VM.set = s;
    VM.gen_seen = atomic_load_explicit(&m->gen, memory_order_acquire);
    VM.toggles_seen = atomic_load_explicit(&m->toggles, memory_order_acquire); /* earlier presses toggle nothing */
    vm_publish_closed();
    char line[512];
    vm_set_line(&VM.set, line, sizeof line);
    const char *off = !cfg.vm ? "SOMVR_XR_VRMENU=off" : cfg.input == SOMVR_INPUT_OFF ? "SOMVR_XR_INPUT=off" : NULL;
    int bad = off ? 0 : somvr_font_init(&VM.font);
    if (off || bad) {
        lg("xr: vrmenu: somvr_vrmenu found at %p (v%u, preload pid %d), but %s: no wrist panel (its settings hold: %s)",
           (void *)m, m->version, m->pid, off ? off : "the font's art is malformed", line);
        if (bad) lg("xr: vrmenu: font glyph %d malformed", bad);
        return;
    }
    VM.on = 1;
    somvr_vm_gcfg_default(&VM.gc);
    vh_panel_cfg(&VM.gc); /* build 10: SOMVR_XR_PANEL=palm */
    somvr_vm_greset(&VM.gs);
    somvr_ptr_cfg_default(&VM.pc);
    somvr_ptr_reset(&VM.ptr);
    VM.main_hand = VM.set.hand ? SOMVR_HAND_L : SOMVR_HAND_R;
    VM.ptr.hand = VM.main_hand;
    VM.hover = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
    if (!X.ptr_on) X.ptr_draw = 1; /* the menu draws its dot and ray from the pointer's image (ptr_fill) */
    const somvr_vm_gcfg *g = &VM.gc;
    lg("xr: vrmenu: somvr_vrmenu found at %p (v%u, preload pid %d, avail 0x%x, blink on %u ms): the wrist panel on (%.0f x "
       "%.0f cm on the %s wrist, the %s hand points; opened and closed by the palm toward the eyes within %.0f deg held "
       "%.1f s, or the menu button's long press; closed by looking away %.1f s, the hand lost %.1f s or its X); "
       "settings %s", (void *)m, m->version, m->pid, m->avail, m->blink_on_ms, SOMVR_VM_W_M * 100, SOMVR_VM_H_M * 100,
       VM.main_hand == SOMVR_HAND_R ? "left" : "right", VM.main_hand == SOMVR_HAND_R ? "right" : "left",
       acosf(g->face_on) * 57.29578f, g->hold_s, g->away_s, g->lost_s, line);
}

/* the hooks into the HUD quad and comfort (the menu's own switches; without the object: the configured ones) */
static int vm_hud_shown(void) { return !VM.obj || VM.set.hud; }
static void vm_hud_geom(float out[3]) {
    memcpy(out, cfg.hud_quad, 3 * sizeof out[0]);
    if (!VM.obj || !(VM.set.hud_dist_m > 0) || !(cfg.hud_quad[0] > 0)) return;
    float k = VM.set.hud_dist_m / cfg.hud_quad[0]; /* the width and the drop scale with the distance */
    out[0] = VM.set.hud_dist_m, out[1] = cfg.hud_quad[1] * k, out[2] = cfg.hud_quad[2] * k;
}
static int vm_comfort_live(void) { return VM.obj && (VM.obj->avail & SOMVR_VMA_COMFORT); }
static float vm_vignette(float obj) {
    float s = VM.obj && (VM.obj->avail & SOMVR_VMA_COMFORT) ? VM.set.vignette : obj;
    return s > 0 ? (s < 1 ? s : 1) : 0;
}
/* what the menu can change in this session: the preload's, the HUD only with its quad, the seat height with a floor */
static uint32_t vm_avail(void) {
    uint32_t a = VM.obj->avail & ~(uint32_t)(SOMVR_VMA_FLOOR | SOMVR_VMA_PR_LIVE);
    if (a & SOMVR_VMA_PRACTICE) /* the practice area's live state (the preload, every input tick) */
        a |= atomic_load_explicit(&VM.obj->pstate, memory_order_acquire) & SOMVR_VMA_PR_LIVE;
    if (!X.hud || !X.hud_on) a &= ~(uint32_t)SOMVR_VMA_HUD;
    if (X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE) a |= SOMVR_VMA_FLOOR;
    if (SSN.obj) /* the preload's somvr_session: the Report and Quit game buttons */
        a |= ((SSN.obj->avail & SOMVR_SSA_MARK) ? (uint32_t)SOMVR_VMA_MARK : 0u) |
             ((SSN.obj->avail & SOMVR_SSA_QUIT) && !atomic_load_explicit(&SSN.obj->quitting, memory_order_relaxed)
                  ? (uint32_t)SOMVR_VMA_QUIT
                  : 0u);
    return a;
}

/* the head at this frame's display time: the views' midpoint, view 0's orientation; 0 = not located */
static int vm_head(somvr_posef *out) {
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = X.fs.predictedDisplayTime,
                           .space = X.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t nv = 0;
    const XrViewStateFlags need = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
    if (XR_FAILED(r) || nv != 2 || (vs.viewStateFlags & need) != need) return 0;
    memcpy(out->q, &v[0].pose.orientation, sizeof out->q);
    out->p[0] = 0.5f * (v[0].pose.position.x + v[1].pose.position.x);
    out->p[1] = 0.5f * (v[0].pose.position.y + v[1].pose.position.y);
    out->p[2] = 0.5f * (v[0].pose.position.z + v[1].pose.position.z);
    return 1;
}

/* a tick on hand h (the menu's haptics strength applied), straight to the runtime */
static void vm_haptic(int h, float amp, float sec) {
    if (!I.on || !I.act[SOMVR_ACT_HAPTIC]) return;
    amp *= VM.set.haptics;
    if (!(amp > 0.005f)) return;
    XrHapticActionInfo hi = {.type = XR_TYPE_HAPTIC_ACTION_INFO, .action = I.act[SOMVR_ACT_HAPTIC],
                             .subactionPath = I.hand[h & 1]};
    XrHapticVibration v = {.type = XR_TYPE_HAPTIC_VIBRATION, .duration = (XrDuration)(sec * 1e9f),
                           .frequency = XR_FREQUENCY_UNSPECIFIED, .amplitude = amp < 1 ? amp : 1};
    XrResult r = X.xrApplyHapticFeedback(X.session, &hi, (const XrHapticBaseHeader *)&v);
    if (XR_FAILED(r)) in_warn(IW_HAPTIC, "xrApplyHapticFeedback (VR menu)", r);
    else VM.n_haptics++;
}

/* The panel's swapchain (an ordinary one: its picture changes), the staging buffer (mapped), the upload's command
 * buffer and fence, made at the first upload of the session. 0 = ready, -1 = the menu is off now. */
static int vm_make(dev_st *ds) {
    if (VM.sc) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->ResetCommandBuffer || !ds->ResetFences || !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return vm_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return vm_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM}; /* the picture's bytes are sRGB-encoded */
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return vm_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f,
                                .sampleCount = 1,
                                .width = SOMVR_VM_TEX_W,
                                .height = SOMVR_VM_TEX_H,
                                .faceCount = 1,
                                .arraySize = 1,
                                .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &VM.sc))) {
        VM.sc = XR_NULL_HANDLE;
        return vm_off("xrCreateSwapchain(%dx%d format %d): %s", SOMVR_VM_TEX_W, SOMVR_VM_TEX_H, f, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(VM.sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return vm_off("xrEnumerateSwapchainImages (VR menu): %s, %u images", xr_str(r), n), -1;
    for (uint32_t i = 0; i < n; i++) VM.img[i] = im[i].image;
    VM.nimg = n, VM.fmt = f, VM.bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    VM.ds = ds;
    VkDeviceSize sz = (VkDeviceSize)SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &VM.buf);
    if (vr != VK_SUCCESS) VM.buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    int type = -1;
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, VM.buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the staging buffer";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &VM.mem)) != VK_SUCCESS) VM.mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, VM.buf, VM.mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, VM.mem, 0, VK_WHOLE_SIZE, 0, &VM.map)) != VK_SUCCESS)
        VM.map = NULL;
    if (vr == VK_SUCCESS) {
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &VM.cmd)) != VK_SUCCESS) VM.cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, VM.cmd);
        else *(void **)VM.cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &VM.fence)) != VK_SUCCESS) VM.fence = VK_NULL_HANDLE;
    }
    if (vr != VK_SUCCESS) return vm_off("the staging buffer: %s: %d", what, vr), -1;
    lg("xr: vrmenu: panel swapchain %dx%d format %d (%u images), a %u KB staging buffer (memory type %d, mapped): the "
       "picture is uploaded when it changes", SOMVR_VM_TEX_W, SOMVR_VM_TEX_H, f, n, (unsigned)(sz >> 10), type);
    return 0;
}

/* the picture v into the staging buffer (the last upload waited first), one copy into the next swapchain image on the
 * session's queue (its fence is waited before the buffer is written again), released. 0 = done, -1 = the menu is off. */
static int vm_upload(dev_st *ds, const somvr_vm_view *v) {
    if (vm_make(ds)) return -1;
    VkResult vr;
    if (VM.pending) {
        if ((vr = ds->WaitForFences(ds->dev, 1, &VM.fence, VK_TRUE, FENCE_TIMEOUT_NS)) != VK_SUCCESS)
            return vm_off("vkWaitForFences (the last upload): %d", vr), -1;
        VM.pending = 0;
    }
    if ((vr = ds->ResetFences(ds->dev, 1, &VM.fence)) != VK_SUCCESS) return vm_off("vkResetFences: %d", vr), -1;
    somvr_vm_draw(&VM.font, (uint8_t *)VM.map, VM.bgra, v); /* HOST_COHERENT: visible to the device at the submit */
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    XrResult r;
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(VM.sc, &ai, &xi)) || xi >= VM.nimg)
        return vm_off("xrAcquireSwapchainImage (VR menu): %s, image %u of %u", xr_str(r), xi, VM.nimg), -1;
    if ((r = X.xrWaitSwapchainImage(VM.sc, &wti)) != XR_SUCCESS)
        return vm_off("xrWaitSwapchainImage (VR menu): %s", xr_str(r)), -1;
    const char *what = "vkResetCommandBuffer";
    if ((vr = ds->ResetCommandBuffer(VM.cmd, 0)) == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(VM.cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, VM.img[xi], 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(VM.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_VM_TEX_W, SOMVR_VM_TEX_H, 1}}; /* tightly packed */
        ds->CmdCopyBufferToImage(VM.cmd, VM.buf, VM.img[xi], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, VM.img[xi], VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(VM.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(VM.cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &VM.cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, VM.fence)) == VK_SUCCESS) VM.pending = 1;
    }
    if (vr != VK_SUCCESS) return vm_off("the picture's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(VM.sc, &ri)))
        return vm_off("xrReleaseSwapchainImage (VR menu): %s", xr_str(r)), -1;
    VM.released = 1, VM.drawn = *v;
    VM.n_uploads++;
    atomic_fetch_add_explicit(&VM.obj->n_uploads, 1, memory_order_relaxed);
    if (VM.n_uploads <= 2 || VM.n_uploads % 100 == 0)
        lg("xr: vrmenu: picture %llu uploaded into image %u (hover %d/%s)", (unsigned long long)VM.n_uploads, xi,
           v->hover.item, vm_part_name[v->hover.part >= 0 && v->hover.part <= SOMVR_VMP_BUTTON ? v->hover.part : 0]);
    return 0;
}

/* the panel quad at pose p: the whole picture, premultiplied (BLEND_TEXTURE_SOURCE_ALPHA alone) */
static XrCompositionLayerQuad vm_quad(const somvr_posef *p) {
    XrCompositionLayerQuad q = {.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                .space = X.space,
                                .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                .subImage = {VM.sc, {{0, 0}, {SOMVR_VM_TEX_W, SOMVR_VM_TEX_H}}, 0},
                                .size = {SOMVR_VM_W_M, SOMVR_VM_H_M}};
    memcpy(&q.pose, p, sizeof q.pose);
    return q;
}

/* a click on the hovered part (the pointing hand's trigger press): the value changes (published: gen + 1), the
 * Recentre button (the game side recentres), the close box */
static void vm_click(const somvr_vm_hit *hit, uint32_t avail) {
    somvr_settings before = VM.set;
    int r = somvr_vm_apply(&VM.set, avail, VM.obj->blink_on_ms, hit->item, hit->part);
    const char *what = "nothing";
    if (r == SOMVR_VMR_CHANGED) {
        somvr_vrmenu_publish(VM.obj, &VM.set);
        VM.gen_seen = atomic_load_explicit(&VM.obj->gen, memory_order_acquire);
        if (hit->item == SOMVR_VMI_HUD_DIST || hit->item == SOMVR_VMI_HUD) X.hud_latched = 0; /* placed again */
        what = "changed", vm_haptic(VM.main_hand, 0.35f, 0.015f);
    } else if (r == SOMVR_VMR_RECENTER) {
        atomic_fetch_add_explicit(&VM.obj->recenters, 1, memory_order_release);
        X.hud_latched = 0; /* the world-locked HUD quad is placed again ahead of the new forward */
        what = "recentre", vm_haptic(VM.main_hand, 0.3f, 0.012f);
    } else if (r == SOMVR_VMR_CLOSE) {
        VM.gs.open = 0, VM.gs.why = SOMVR_VMW_CLOSE_BOX, VM.gs.closes++, VM.gs.armed = 0;
        what = "close", vm_haptic(VM.main_hand, 0.3f, 0.012f);
    } else if (r == SOMVR_VMR_PAGE) { /* a tab: its page (build 6: Settings, Practice, Controls) */
        VM.page = somvr_vm_tab_page(hit->item);
        VM.hover = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
        what = VM.page == SOMVR_VMPG_PRACTICE ? "the practice page"
               : VM.page == SOMVR_VMPG_CONTROLS ? "the controls page"
                                                : "the settings page";
        vm_haptic(VM.main_hand, 0.3f, 0.012f);
    } else if (r == SOMVR_VMR_PRACTICE) { /* a practice button: the request for the preload's game thread */
        uint32_t w = atomic_load_explicit(&VM.obj->practice, memory_order_relaxed);
        w = somvr_vm_pr_pack(somvr_vm_pr_seq(w) + 1, somvr_vm_practice_action(hit->item), (int)VM.set.orc);
        atomic_store_explicit(&VM.obj->practice, w, memory_order_release);
        what = "practice request", vm_haptic(VM.main_hand, 0.35f, 0.015f);
    } else if (r == SOMVR_VMR_REPORT) { /* a bug mark: the preload's game thread makes it (its buzzes confirm) */
        atomic_fetch_add_explicit(&SSN.obj->report_req, 1, memory_order_release);
        what = "report (a bug mark)", vm_haptic(VM.main_hand, 0.3f, 0.012f);
    } else if (r == SOMVR_VMR_QUIT && SSN.quit_armed && ss_now() - SSN.quit_t <= 5.0) { /* the second press: confirmed */
        SSN.quit_armed = 0;
        atomic_fetch_add_explicit(&SSN.obj->quit_req, 1, memory_order_release);
        what = "quit game (confirmed)", vm_haptic(VM.main_hand, 0.6f, 0.08f);
    } else if (r == SOMVR_VMR_QUIT) { /* the first press: the button reads Confirm for 5 s */
        SSN.quit_armed = 1, SSN.quit_t = ss_now();
        what = "quit game armed (press Confirm within 5 s)", vm_haptic(VM.main_hand, 0.35f, 0.015f);
    }
    if (r != SOMVR_VMR_QUIT) SSN.quit_armed = 0; /* any other click: Quit is back to normal */
    if (r != SOMVR_VMR_NONE) {
        VM.n_clicks++;
        atomic_fetch_add_explicit(&VM.obj->n_clicks, 1, memory_order_relaxed);
    }
    char was[32], now[32], chg[80] = "";
    somvr_vm_value(&before, hit->item, was, sizeof was);
    somvr_vm_value(&VM.set, hit->item, now, sizeof now);
    if (r == SOMVR_VMR_CHANGED) snprintf(chg, sizeof chg, " %s -> %s", was, now);
    if (VM.lines < 400) {
        VM.lines++;
        lg("xr: vrmenu: click %s %s: %s%s (gen %u, clicks %llu)", somvr_vm_label(hit->item),
           vm_part_name[hit->part >= 0 && hit->part <= SOMVR_VMP_BUTTON ? hit->part : 0], what, chg,
           atomic_load(&VM.obj->gen), (unsigned long long)VM.n_clicks);
    }
}

#include "somvr_menus_layer.c" /* build 6: the menu boards, the wrist minimap, the arrival card, touch */
#include "somvr_vrhands_layer.c" /* build 10: our own VR hands, the watch on the wrist, the panel off the palm */
#include "somvr_void_layer.c" /* never a void (HG-9): the present gaps, the keep-alive frames, the loading space */
#include "somvr_testhead_layer.c" /* SOMVR_XR_TEST `head X Y Z [s SEC]`: a duck for headless tests */

/* One frame (stereo, reuse or mono), before the game's pointer (it has no target while the panel is open) and before
 * the frame's layers are put together: the gesture, the pointer on the panel, the clicks, the picture; VM.q[0 .. nq)
 * = the panel, the ray, the dot. Published: open, consume. */
static void vm_frame(dev_st *ds) {
    VM.nq = 0;
    if (!VM.on) return;
    somvr_vrmenu_shared *m = VM.obj;
    uint32_t g = atomic_load_explicit(&m->gen, memory_order_acquire);
    somvr_settings s;
    if (g != VM.gen_seen && somvr_vrmenu_read(m, &s)) VM.set = s, VM.gen_seen = g; /* (only this layer writes it) */
    int main = VM.set.hand ? SOMVR_HAND_L : SOMVR_HAND_R, ph = 1 - main;
    if (main != VM.main_hand) { /* the main hand changed: the panel goes to the other wrist, the other hand points */
        VM.main_hand = main;
        somvr_ptr_reset(&VM.ptr);
        VM.ptr.hand = main;
        lg("xr: vrmenu: the %s hand points now, the panel is on the %s wrist", main == SOMVR_HAND_R ? "right" : "left",
           main == SOMVR_HAND_R ? "left" : "right");
    }
    somvr_vm_gin gi;
    memset(&gi, 0, sizeof gi);
    gi.t = (double)X.fs.predictedDisplayTime * 1e-9;
    gi.head_ok = vm_head(&gi.head);
    const somvr_hand_in *hp = &I.st.hand[ph];
    gi.hand_ok = I.on && (hp->active & SOMVR_HACT_POSE) && (hp->grip_flags & 3u) == 3u;
    gi.grip = hp->grip;
    gi.hand = ph;
    uint32_t tg = atomic_load_explicit(&m->toggles, memory_order_acquire);
    gi.toggles = tg - VM.toggles_seen;
    VM.toggles_seen = tg;
    gi.busy = VM.gs.open && (VM.ptr.on || VM.ptr.down);
    gi.block = atomic_load_explicit(&m->block, memory_order_acquire); /* the hands are busy: no gesture open */
    somvr_posef panel;
    int have = 0;
    uint64_t held0 = VM.gs.held_off;
    int ev = somvr_vm_gesture(&VM.gs, &VM.gc, &gi, &panel, &have);
    if (VM.gs.held_off != held0 && VM.gs.held_off <= 50) {
        char w[96];
        somvr_vm_gate_text(VM.gs.gate_why, w, sizeof w);
        lg("xr: vrmenu: the palm gesture held off: %s (%llu so far; a fresh gesture after it opens the panel)", w,
           (unsigned long long)VM.gs.held_off);
    }
    if (ev > 0) {
        somvr_ptr_reset(&VM.ptr);
        VM.ptr.hand = main;
        VM.hover = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
        atomic_fetch_add_explicit(&m->n_opens, 1, memory_order_relaxed);
        lg("xr: vrmenu: opened by %s on the %s wrist (opens %llu)", somvr_vm_why(VM.gs.why),
           ph == SOMVR_HAND_L ? "left" : "right", (unsigned long long)VM.gs.opens);
    } else if (ev < 0) {
        lg("xr: vrmenu: closed: %s (closes %llu, %llu frames shown)", somvr_vm_why(VM.gs.why),
           (unsigned long long)VM.gs.closes, (unsigned long long)VM.n_frames);
    }
    if (VM.gs.open && mn_watch_over(ph, &gi)) { /* build 6: the panel's wrist turned over: the watch, not its back */
        VM.gs.open = 0, VM.gs.why = SOMVR_VMW_TURNED, VM.gs.closes++, VM.gs.armed = 0, have = 0, ev = -1;
        lg("xr: vrmenu: closed: %s (closes %llu, %llu frames shown)", somvr_vm_why(VM.gs.why),
           (unsigned long long)VM.gs.closes, (unsigned long long)VM.n_frames);
    }
    int card = mn_card(ev, &gi, &panel, &have); /* build 6: the arrival card (the Controls page) in front instead */
    uint32_t consume = 0, avail = vm_avail(), nq = 0;
    somvr_ptr_out o;
    memset(&o, 0, sizeof o);
    float psz[2];
    mn_card_size(card, psz);
    if ((VM.gs.open || card) && have) { /* the main hand's ray on the panel */
        somvr_ptr_in in;
        memset(&in, 0, sizeof in);
        if (I.on) somvr_ptr_in_from(&I.st, &in);
        in.ok[ph] = 0, in.trigger[ph] = 0; /* only the main hand points (the panel's own hand cannot) */
        in.time = I.st.time ? I.st.time : X.fs.predictedDisplayTime;
        in.quad = SOMVR_PTRQ_VRMENU;
        memcpy(&in.quad_pose, &panel, sizeof panel);
        in.quad_size[0] = psz[0], in.quad_size[1] = psz[1];
        VM.ptr.hand = main;
        somvr_ptr_update(&VM.ptr, &VM.pc, &in, &o);
        mn_vm_touch(card, &panel, psz, &o); /* build 6: a fingertip on it wins over the ray */
        somvr_vm_hit hit = {-1, SOMVR_VMP_NONE};
        if (VM.page == SOMVR_VMPG_PRACTICE && !(avail & SOMVR_VMA_PRACTICE)) VM.page = SOMVR_VMPG_SETTINGS; /* (no tab) */
        if (o.flags & SOMVR_PTRF_ON) /* the rows this build shows (somvr_vm_shown) */
            hit = somvr_vm_hit_test_av(avail, card ? SOMVR_VMPG_CONTROLS : VM.page, o.uv[0], o.uv[1]);
        if (card && hit.item != SOMVR_VMI_CLOSE) hit = (somvr_vm_hit){-1, SOMVR_VMP_NONE}; /* the card: its X alone */
        if (hit.item >= 0 && hit.item != VM.hover.item && somvr_vm_enabled(&VM.set, avail, hit.item))
            vm_haptic(MN.vtouch ? MN.tvo.hand : main, 0.12f, 0.008f); /* a light tick on a new row */
        VM.hover = hit;
        if ((o.edges & SOMVR_PTRE_PRESS) && hit.item >= 0 && card) { /* the card's X: put away */
            mn_card_close(MN.vtouch ? "its X box, touched" : "its X box");
            vm_haptic(MN.vtouch ? MN.tvo.hand : main, 0.3f, 0.012f);
            card = have = 0, VM.hover = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
        } else if ((o.edges & SOMVR_PTRE_PRESS) && hit.item >= 0) {
            vm_click(&hit, avail);
        }
        if (!VM.gs.open && !card && have) { /* its close box */
            VM.hover = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
            lg("xr: vrmenu: closed: %s (closes %llu, %llu frames shown)", somvr_vm_why(VM.gs.why),
               (unsigned long long)VM.gs.closes, (unsigned long long)VM.n_frames);
        }
        consume = o.consume & (1u << main);
    } else if (VM.ptr.on || VM.ptr.down || VM.ptr.quad != SOMVR_PTRQ_NONE) { /* no panel: a held click ends */
        somvr_ptr_in in;
        memset(&in, 0, sizeof in);
        in.time = X.fs.predictedDisplayTime;
        somvr_ptr_update(&VM.ptr, &VM.pc, &in, &o);
        memset(&o, 0, sizeof o);
        VM.hover = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
    }
    if (!((VM.gs.open || card) && have)) mn_vm_reset_touch();
    if ((VM.gs.open || card) && have) { /* the picture, then the layers */
        somvr_vm_view v;
        memset(&v, 0, sizeof v);
        if (SSN.quit_armed && ss_now() - SSN.quit_t > 5.0) SSN.quit_armed = 0; /* not confirmed in time */
        v.set = VM.set, v.avail = avail, v.hover = VM.hover, v.page = card ? SOMVR_VMPG_CONTROLS : VM.page, v.card = card;
        v.quit_armed = card ? 0 : SSN.quit_armed;
        if ((!VM.released || memcmp(&v, &VM.drawn, sizeof v)) && vm_upload(ds, &v)) return; /* off now (logged) */
        VM.q[nq] = vm_quad(&panel);
        VM.q[nq].size.width = psz[0], VM.q[nq++].size.height = psz[1];
        int draw = X.ptr_draw && (o.flags & (SOMVR_PTRF_ON | SOMVR_PTRF_AIM)) && !ptr_fill(ds);
        if (draw && MN.vtouch) nq += mn_touch_marks(&MN.tvo, &panel, NULL, &VM.q[nq]); /* the fingertip's markers */
        if (draw && !MN.vtouch && cfg.ptr != SOMVR_PTRM_DOT && (o.flags & SOMVR_PTRF_AIM)) { /* the ray: 6 cm ahead */
            float a[3], b[3], len = (o.flags & SOMVR_PTRF_ON) ? o.dist : 0.5f;
            for (int i = 0; i < 3; i++) a[i] = o.origin[i] + 0.06f * o.dir[i], b[i] = o.origin[i] + len * o.dir[i];
            if (o.flags & SOMVR_PTRF_ON) memcpy(b, o.hit, sizeof b);
            somvr_posef rp;
            float rs[2];
            if (len > 0.08f && somvr_ptr_ray_quad(a, b, gi.head_ok ? gi.head.p : NULL, 0.0015f, &rp, rs))
                VM.q[nq++] = ptr_layer(1, &rp, rs[0], rs[1]);
        }
        if (draw && !MN.vtouch && (o.flags & SOMVR_PTRF_ON)) { /* the dot: on the panel, turned like it */
            somvr_posef dp;
            memcpy(dp.q, panel.q, sizeof dp.q);
            memcpy(dp.p, o.hit, sizeof dp.p);
            float d = somvr_ptr_dot_size(o.dist, 0.5f);
            VM.q[nq++] = ptr_layer(0, &dp, d, d);
        }
        VM.n_frames++;
        atomic_fetch_add_explicit(&m->n_frames, 1, memory_order_relaxed);
        if (VM.n_frames <= 2 || VM.n_frames % 600 == 0 || (o.edges && VM.lines < 400)) {
            VM.lines += o.edges != 0;
            lg("xr: vrmenu frame %llu: panel at %+.3f,%+.3f,%+.3f, pointer flags 0x%x edges 0x%x uv %.3f,%.3f hover %d/%s "
               "consume 0x%x layers %u (uploads %llu, clicks %llu)", (unsigned long long)VM.n_frames, panel.p[0],
               panel.p[1], panel.p[2], o.flags, o.edges, o.uv[0], o.uv[1], VM.hover.item,
               vm_part_name[VM.hover.part >= 0 && VM.hover.part <= SOMVR_VMP_BUTTON ? VM.hover.part : 0], consume, nq,
               (unsigned long long)VM.n_uploads, (unsigned long long)VM.n_clicks);
        }
    }
    VM.nq = nq;
    atomic_store_explicit(&m->consume, consume, memory_order_release);
    atomic_store_explicit(&m->open, VM.gs.open || card ? 1u : 0u, memory_order_release); /* (the card: its trigger) */
}
/* the panel has the main hand: the game's pointer gets no target this frame */
static int vm_has_hand(void) { return VM.on && VM.gs.open; }
/* this frame's menu layers into the composition (XL_VM0..: over the pointer's, under the blink); VM.nq of them */
static void vm_comp(xr_comp *c) {
    for (uint32_t i = 0; i < VM.nq && i <= XL_VM2 - XL_VM0; i++)
        c->l[XL_VM0 + i] = (const XrCompositionLayerBaseHeader *)&VM.q[i];
}

/* ---- the XR frame (X.mx held) --------------------------------------------------------------------------------- */
/* SOMVR_XR_FLOOR (docs/re-xr-pipeline.md 35.1): in a LOCAL space (y = 0 at the head where the runtime set it) the
 * player's real height is not in the game's logs; once per session the LOCAL origin located in the runtime's STAGE
 * space (its floor at y = 0) and the last stereo frame's eyes give it. Log only; a failure is one line, never retried. */
static void fl_probe(void) {
    if (X.floor_done || cfg.floor_frames <= 0 || X.space_type != XR_REFERENCE_SPACE_TYPE_LOCAL ||
        ++X.floor_frames < (uint64_t)cfg.floor_frames ||
        (!X.last_stereo && X.floor_frames < 10ull * (uint64_t)cfg.floor_frames)) /* a stereo frame's eyes, if any */
        return;
    X.floor_done = 1;
    XrResult r = XR_SUCCESS;
    if (!X.floor_space) {
        XrReferenceSpaceType types[16];
        uint32_t nt = 0;
        int stage = 0;
        r = X.xrEnumerateReferenceSpaces(X.session, 16, &nt, types);
        for (uint32_t i = 0; XR_SUCCEEDED(r) && i < nt && i < 16; i++) stage |= types[i] == XR_REFERENCE_SPACE_TYPE_STAGE;
        if (!stage) {
            lg("xr: the floor: no STAGE space (%s, %u types): the player's real eye height is not known here", xr_str(r), nt);
            return;
        }
        XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
                                         .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE,
                                         .poseInReferenceSpace = {.orientation = {0, 0, 0, 1}}};
        if (XR_FAILED(r = X.xrCreateReferenceSpace(X.session, &rs, &X.floor_space))) {
            X.floor_space = XR_NULL_HANDLE;
            lg("xr: the floor: xrCreateReferenceSpace(STAGE) %s", xr_str(r));
            return;
        }
    }
    XrSpaceLocation loc = {.type = XR_TYPE_SPACE_LOCATION};
    r = X.xrLocateSpace(X.space, X.floor_space, X.fs.predictedDisplayTime, &loc);
    if (XR_FAILED(r) || !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        lg("xr: the floor: LOCAL in STAGE not located (%s, flags 0x%llx)", xr_str(r), (unsigned long long)loc.locationFlags);
        return;
    }
    float oy = loc.pose.position.y, ey = X.last_stereo ? 0.5f * (X.last_pv[0].pose.position.y + X.last_pv[1].pose.position.y)
                                                         : 0.0f;
    lg("xr: the floor (STAGE): LOCAL's origin %.3f m above it; %s %.3f m in LOCAL = %.3f m above the floor: the player's "
       "real eye height (Talion's eyes, where the rig puts them: 1.806 m above his feet x SOMVR_PLAYER_SIZE)", oy,
       X.last_stereo ? "the last stereo frame's eyes" : "no stereo frame yet: the origin", ey, oy + ey);
    vd_floor(oy); /* never a void: the loading space's floor at the real floor */
}

static int xr_wait_publish(void) {
    XrFrameWaitInfo wi = {.type = XR_TYPE_FRAME_WAIT_INFO};
    X.fs = (XrFrameState){.type = XR_TYPE_FRAME_STATE};
    XrResult r = X.xrWaitFrame(X.session, &wi, &X.fs);
    if (XR_FAILED(r)) return xr_fail("xrWaitFrame", r), -1;
    X.waited = 1;
    vd_waited(); /* never a void: when this wait returned (the watcher's head times) */
    if (X.sh) /* the time the game thread's next views are for: this wait's + the pipeline lead (7.1) */
        somvr_time_publish(X.sh, somvr_lead_publish(&X.lead, cfg.predict, cfg.predict_ns, X.fs.predictedDisplayTime),
                           X.fs.predictedDisplayPeriod);
    if (I.on) in_sync(); /* once per frame, at its start (re-xr-input.md 1.5) */
    fl_probe();          /* SOMVR_XR_FLOOR: the player's real eye height, once (log only) */
    return 0;
}

static int xr_begin(void) {
    if (!X.waited && xr_wait_publish()) return -1;
    XrFrameBeginInfo fbi = {.type = XR_TYPE_FRAME_BEGIN_INFO};
    XrResult r = X.xrBeginFrame(X.session, &fbi);
    if (XR_FAILED(r)) return xr_fail("xrBeginFrame", r), -1;
    X.waited = 0;
    X.frame_open = 1;
    return 0;
}

static int xr_acquire(void) {
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    XrResult r;
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(X.sc, &ai, &X.xi)) || X.xi >= X.nimg)
        return xr_fail("xrAcquireSwapchainImage", r), -1;
    X.img_acquired = 1;
    if ((r = X.xrWaitSwapchainImage(X.sc, &wti)) != XR_SUCCESS) return xr_fail("xrWaitSwapchainImage", r), -1;
    return 0;
}

static int xr_release(void) {
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    X.img_acquired = 0;
    XrResult r = X.xrReleaseSwapchainImage(X.sc, &ri);
    if (XR_FAILED(r)) return xr_fail("xrReleaseSwapchainImage", r), -1;
    return 0;
}

/* A runtime that rejects the frame with a layer error (a quad's values) has not consumed it [med: Monado validates the
 * layers before it ends anything]: the same frame is ended again without one part. */
static int layer_rejected(XrResult r) {
    return r == XR_ERROR_LAYER_INVALID || r == XR_ERROR_LAYER_LIMIT_EXCEEDED || r == XR_ERROR_SWAPCHAIN_RECT_INVALID ||
           r == XR_ERROR_POSE_INVALID || r == XR_ERROR_VALIDATION_FAILURE;
}
/* the composition without the parts in `skip` (XG_*), in its order; *n = how many layers went */
static XrResult xr_end_try(const xr_comp *c, uint32_t skip, uint32_t *n) {
    const XrCompositionLayerBaseHeader *ls[XL_N];
    *n = 0;
    for (int i = 0; c->l[XL_OWN] && i < XL_N; i++) /* no frame layer: no extras either (an empty frame) */
        if (c->l[i] && !(xl_group(i) & skip)) ls[(*n)++] = c->l[i];
    XrFrameEndInfo ei = {.type = XR_TYPE_FRAME_END_INFO,
                         .displayTime = X.fs.predictedDisplayTime,
                         .environmentBlendMode = X.blend,
                         .layerCount = *n,
                         .layers = *n ? ls : NULL};
    return X.xrEndFrame(X.session, &ei);
}
/* End the open frame with its composition. A rejected frame is ended again without one part at a time - the
 * environment's layers (the SoM loading env: under the frame's), the fade, comfort's (ring and blink), the VR menu's,
 * the pointer's, the HUD quad - and only the part whose removal the runtime accepts goes off for the session (logged),
 * the others stay; when no single part is to blame, all of them go off (the frame layer alone). Any other failure, or
 * a frame layer the runtime rejects alone, is xr_fail as before. */
static void xl_cap(xr_comp *c); /* (the menus' file: the band out of a frame over XL_MAX layers) */
static int xr_end_comp(const xr_comp *c0) {
    xr_comp cc = *c0;
    const xr_comp *c = &cc;
    xl_cap(&cc);
    X.frame_open = 0;
    X.open_frame = 0;
    X.open_mask = X.open_copied = 0;
    uint32_t have = 0, n = 0, off = 0, np = 0, ne = 0, nv = 0;
    for (int i = 0; c->l[XL_OWN] && i < XL_N; i++) have |= c->l[i] && i != XL_OWN ? xl_group(i) : 0u;
    np = (uint32_t)(c->l[XL_PTR0] != NULL) + (uint32_t)(c->l[XL_PTR1] != NULL);
    for (int i = XL_ENV0; i < XL_OWN; i++) ne += c->l[i] != NULL;
    for (int i = XL_VM0; i <= XL_VM2; i++) nv += c->l[i] != NULL;
    uint32_t nt = 0;
    for (int i = XL_THG0; i < XL_N; i++) nt += c->l[i] && xl_group(i) == XG_TH;
    XrResult r0 = xr_end_try(c, 0, &n), r = r0;
    if (have && layer_rejected(r)) {
        static const uint32_t order[] = {XG_ENV, XG_VH, XG_MB, XG_TH, XG_FADE, XG_CF, XG_VM, XG_PTR, XG_HUD};
        for (size_t k = 0; k < sizeof order / sizeof order[0] && !off; k++)
            if ((have & order[k]) && !layer_rejected(r = xr_end_try(c, order[k], &n))) off = order[k];
        if (!off && layer_rejected(r)) r = xr_end_try(c, off = have, &n); /* more than one of them: all off */
    }
    if (off & XG_ENV)
        env_off("xrEndFrame rejected the frame with the environment's %u layer(s): %s; ended again without them", ne,
                xr_str(r0));
    if (off & XG_TH)
        th_off("xrEndFrame rejected the frame with the threat cues (%u): %s; ended again without them", nt, xr_str(r0));
    if (off & XG_FADE)
        fade_off("xrEndFrame rejected the frame with the fade quad: %s; ended again without it", xr_str(r0));
    if (off & XG_CF)
        cf_off("xrEndFrame rejected the frame with the comfort layers (%u): %s; ended again without them",
               (uint32_t)(c->l[XL_RING] != NULL) + (uint32_t)(c->l[XL_BLINK] != NULL), xr_str(r0));
    if (off & XG_VM)
        vm_off("xrEndFrame rejected the frame with the menu's %u layer(s): %s; ended again without them", nv, xr_str(r0));
    if (off & XG_PTR)
        ptr_off("xrEndFrame rejected the frame with the pointer's %u layer(s): %s; ended again without them", np,
                xr_str(r0));
    if (off & XG_HUD) hud_off("xrEndFrame rejected the frame with the HUD quad: %s; ended again without it", xr_str(r0));
    if (off & XG_VH) vh_off("xrEndFrame rejected the frame with our hands' layer: %s; ended again without it", xr_str(r0));
    if (off & XG_MB) mb_off("xrEndFrame rejected the frame with the backdrop's layers (ended again without them)", xr_str(r0));
    X.fade_n = 0, CF.has = 0, X.ptr_nq = 0, VM.nq = 0, TH.has = 0; /* this frame's parts went (or not) with it */
    if (XR_FAILED(r)) return xr_fail("xrEndFrame", r), -1;
    X.frames++;
    vd_ended(); /* never a void: the time between xrEndFrame calls */
    X.nolayer += n == 0;
    if (c->l[XL_FADE] && !(off & XG_FADE)) { /* the fade went out with the frame */
        X.n_fade++;
        X.n_fade_black += X.fade_q.subImage.imageRect.offset.x == (SOMVR_FADE_LEVELS - 1) * SOMVR_FADE_TEX + 1;
    }
    return 0;
}
static int xr_end(const XrCompositionLayerBaseHeader *layer) {
    xr_comp c = {.l = {[XL_OWN] = layer}};
    return xr_end_comp(&c);
}
/* the fade quad of this frame (fade_frame made it) into the composition: the last layer */
static void fade_comp(xr_comp *c) {
    if (X.fade_n) c->l[XL_FADE] = (const XrCompositionLayerBaseHeader *)&X.fade_q;
}

static const XrCompositionLayerBaseHeader *proj_layer(XrCompositionLayerProjection *proj,
                                                      const XrCompositionLayerProjectionView *pv) {
    *proj = (XrCompositionLayerProjection){
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION, .space = X.space, .viewCount = 2, .views = pv};
    return (const XrCompositionLayerBaseHeader *)proj;
}

/* the projection views of a stereo record: exactly its XR poses, the fov each eye was rendered with, its rects */
static void stereo_views(const somvr_rec *rec, uint32_t w, uint32_t h, XrCompositionLayerProjectionView pv[2]) {
    for (int e = 0; e < 2; e++) {
        int32_t rc[4];
        somvr_eye_pixel_rect(w, h, rec->eye[e].slot, rec->eye[e].rect, rc);
        somvr_fovf f = somvr_submit_fov(&rec->eye[e], rec->applied, e);
        pv[e] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                                                   .subImage = {X.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0}};
        memcpy(&pv[e].pose, &rec->eye[e].pose, sizeof pv[e].pose);
        memcpy(&pv[e].fov, &f, sizeof pv[e].fov);
    }
}

/* An open multi-slot frame that this present does not continue: its image holds only some eyes, so it is released
 * and the frame ends without a layer (one empty frame); the last released image is no longer a whole stereo frame. */
static int xr_abort_open(const char *why) {
    lg("xr: stereo frame %llu ended incomplete (slots 0x%x of %u: %s)", (unsigned long long)X.open_frame, X.open_mask,
       X.open_rec.nslots, why);
    if (X.img_acquired && xr_release()) return -1;
    X.last_stereo = 0;
    return xr_end(NULL);
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
    in_haptics();
    sc_st *s = LIST_FIND(g_scs, sc, pi->pSwapchains[0]);
    uint32_t idx = pi->pImageIndices[0];
    int can_copy = s && s->copyable && idx < s->nimg;
    int has_slot = s && idx < s->nimg; /* our command buffer + semaphore for this game image live there */
    uint32_t w = s ? s->ext.width : 0, h = s ? s->ext.height : 0;

    /* what this present shows: the preload's tag and record (docs/re-xr-pipeline.md 4.3, 4.7) */
    somvr_tag tag = {0};
    somvr_rec rec;
    int have_tag = X.sh && somvr_tag_for_present(X.sh, (int32_t)syscall(SYS_gettid), &X.tag_last, &tag);
    int have_rec = have_tag && tag.frame && somvr_rec_read(X.sh, tag.frame, &rec) && rec.nslots == X.nslots &&
                   tag.slot < rec.nslots && rec.eye[0].slot < rec.nslots && rec.eye[1].slot < rec.nslots;
    /* layout B: the eyes come from the preload's two eye images (the tag names them), never from the presented image;
     * without them the frame is not stereo (reuse, or mono when there is nothing to reuse) */
    eyes_st ey = {.fmt = VK_FORMAT_UNDEFINED};
    int have_eyes = 0;
    if (have_rec && rec.layout == SOMVR_LAYOUT_B) {
        char why[256] = "no image index for this present";
        have_eyes = has_slot && eyes_lookup(ds->dev, &tag, &ey, why, sizeof why);
        if (!have_eyes) {
            have_rec = 0;
            X.n_noeyes++;
            if (strcmp(why, X.noeyes_why) || X.n_noeyes % 600 == 0) /* each new reason, then every 600th */
                lg("xr: layout B frame %llu (tag %llu) without its eye images: %s (%llu so far)",
                   (unsigned long long)tag.frame, (unsigned long long)tag.seq, why, (unsigned long long)X.n_noeyes);
            snprintf(X.noeyes_why, sizeof X.noeyes_why, "%s", why);
        }
    }
    somvr_submit_kind kind = somvr_submit_decide(have_tag, have_tag ? tag.frame : 0, have_rec, X.last_stereo);
    if (X.frame_open && !(kind == SOMVR_SUBMIT_STEREO && tag.frame == X.open_frame && !(X.open_mask >> tag.slot & 1)))
        if (xr_abort_open(kind == SOMVR_SUBMIT_STEREO ? "another frame's slot" : "a present without its next slot"))
            return 0;
    cv_poll(ds, kind == SOMVR_SUBMIT_MONO); /* SOMVR_XR_COVER: the HUD readings whose submits are done */
    if (EV.on && env_decide(ds, kind)) return 0; /* SoM loading env: a hold frame (the shown side's layers, darker) */
    /* the XR swapchain must fit this frame's source: the eye images, or the presented image (mono, layout A) */
    if (kind == SOMVR_SUBMIT_STEREO && have_eyes) {
        int rc = xr_swapchain_ensure(ds, ey.fmt, ey.w, ey.h, 1, "eye image");
        if (rc < 0) return 0;
        if (rc == 1) { /* the runtime cannot take the eye format: never try it again */
            X.eyes_bad_fmt = ey.fmt;
            lg("xr: eye image format %d unusable: layout B frames are shown as reuse/mono", ey.fmt);
            have_eyes = have_rec = 0;
            kind = somvr_submit_decide(have_tag, have_tag ? tag.frame : 0, 0, X.last_stereo);
        }
    }
    if (kind != SOMVR_SUBMIT_REUSE && !have_eyes && can_copy && xr_swapchain_ensure(ds, s->fmt, w, h, 0, "game"))
        return 0;
    if (X.phase != P_SESSION) return 0;
    /* H11: a stereo frame from the eye images may carry the HUD image (somvr_hud's entry for this tag) */
    hud_st hd = {.fmt = VK_FORMAT_UNDEFINED};
    int have_hud = 0;
    if (kind == SOMVR_SUBMIT_STEREO && have_eyes && X.hud_on && vm_hud_shown()) { /* (the VR menu's HUD switch) */
        char why[256] = "";
        have_hud = hud_lookup(ds->dev, &tag, &hd, why, sizeof why);
        if (have_hud && (hud_swapchain_ensure(hd.fmt, hd.w, hd.h) || (cfg.hud == SOMVR_HUD_HEAD && hud_view_space())))
            have_hud = 0; /* the HUD is off now (logged) */
        else if (!have_hud && (++X.n_nohud, strcmp(why, X.nohud_why) || X.n_nohud % 600 == 0)) { /* new reason, 600th */
            lg("xr: stereo frame %llu (tag %llu) without the HUD quad: %s (%llu so far)", (unsigned long long)tag.frame,
               (unsigned long long)tag.seq, why, (unsigned long long)X.n_nohud);
            snprintf(X.nohud_why, sizeof X.nohud_why, "%s", why);
        }
        if (have_hud) cv_kind(hd.kind); /* SOMVR_XR_COVER: a video is never the cover (a game menu: its reading) */
    }

    int replaced = 0;
    XrCompositionLayerProjection proj;
    XrCompositionLayerProjectionView pv[2];
    if (kind == SOMVR_SUBMIT_REUSE) { /* frame id mismatch: the last stereo frame again, no copy */
        if (xr_begin()) return 0;
        X.n_reuse++;
        if (X.sh) atomic_fetch_add_explicit(&X.sh->n_reuse, 1, memory_order_relaxed);
        if (X.n_reuse <= 3 || X.n_reuse % 600 == 0)
            lg("xr: present tag %llu frame %llu has no complete record: the last stereo frame again (reuse %llu)",
               (unsigned long long)tag.seq, (unsigned long long)tag.frame, (unsigned long long)X.n_reuse);
        if (cv_space_shown()) { /* SOMVR_XR_COVER's load: the loading space, not the world */
            xr_comp cc;
            memset(&cc, 0, sizeof cc);
            VH.last = 0;
            vm_frame(ds);
            cv_space_comp(ds, &cc);
            vm_comp(&cc);
            fade_comp(&cc);
            xr_end_comp(&cc);
            return 0;
        }
        xr_comp rc = {.l = {[XL_OWN] = proj_layer(&proj, X.last_pv)}};
        if (X.last_hud && X.hud_sc) /* the HUD swapchain's last released image is that frame's */
            rc.l[XL_HUD] = (const XrCompositionLayerBaseHeader *)&X.last_quad, rc.l[XL_MMAP] = mn_mm_reuse(),
            rc.l[XL_BAND] = mn_band_reuse();
        mb_layers(&rc); /* SOMVR_XR_MENU_BACK: that frame's backdrop again */
        vm_frame(ds); /* the VR menu: its own frame (the hand moves on) */
        fade_frame(ds, env_level(X.last_fade), X.last_fade_eye, fade_red(env_level(X.last_fade))); /* that frame's again */
        vm_comp(&rc);                                 /* the VR menu's panel, ray, dot */
        vh_reuse(&rc);                                /* build 10: that frame's hands again */
        cf_reuse_layers(&rc);                         /* comfort: that frame's vignette and blink again */
        th_reuse_layers(&rc);                         /* the threat cues: that frame's again */
        fade_comp(&rc);
        xr_end_comp(&rc);
        return 0;
    }
    if (kind == SOMVR_SUBMIT_STEREO) {
        if (!X.frame_open) {
            if (xr_begin()) return 0;
            X.open_frame = tag.frame;
            X.open_rec = rec;
            X.open_mask = X.open_copied = 0;
        }
        X.open_rec = rec; /* the latest copy: H3's applied bits are final by the last slot */
        uint32_t mask = have_eyes ? 3u : 1u << tag.slot; /* layout B: both slots from the eye images at once */
        int hud_copied = 0;
        if (X.fs.shouldRender && (have_eyes || can_copy)) {
            if (!X.img_acquired && xr_acquire()) return 0;
            if (have_hud && hud_acquire()) have_hud = 0; /* the HUD is off now (logged); the eyes go on */
            if (have_eyes && (D.n || ss_pending())) dump_arm(ds, 0, X.n_eyes + 1); /* (SSN: a mark) */
            replaced = have_eyes ? eyes_submit(ds, qi->q, s, idx, X.img[X.xi], pi, &ey, have_hud ? &hd : NULL)
                                 : copy_submit(ds, qi->q, s, idx, X.img[X.xi], pi, tag.slot, 0);
            if (!replaced) {
                D.armed = 0;
                if (X.img_acquired) xr_release();
                return xr_fail("our copy submit", XR_ERROR_RUNTIME_FAILURE), 0;
            }
            *sem = s->slot[idx].sem;
            X.copies++;
            X.open_copied |= mask;
            hud_copied = have_hud;
            if (have_eyes) {
                X.n_eyes++;
                atomic_fetch_add_explicit(&X.sh->eyes.n_submits, 1, memory_order_relaxed);
                if (X.n_eyes <= 3 || X.n_eyes % 600 == 0)
                    lg("xr: layout B frame %llu from the eye images %#llx/%#llx (%ux%u format %d, gen %u; %llu so far)",
                       (unsigned long long)tag.frame, (unsigned long long)(uintptr_t)ey.img[0],
                       (unsigned long long)(uintptr_t)ey.img[1], ey.w, ey.h, ey.fmt, ey.gen,
                       (unsigned long long)X.n_eyes);
                if (D.armed) dump_write(ds, &s->slot[idx], 0);
            }
        }
        X.open_mask |= mask;
        if (X.open_mask != (1u << X.open_rec.nslots) - 1u) return replaced; /* layout B: the next slot follows */
        int whole = X.open_copied == X.open_mask;
        if (X.img_acquired && xr_release()) return replaced;
        if (X.hud_acquired && hud_release()) hud_copied = 0; /* the HUD is off now: no quad this frame */
        if (whole) {
            stereo_views(&X.open_rec, X.sc_w / X.nslots, X.sc_h, pv); /* one slot's size */
            memcpy(X.last_pv, pv, sizeof pv);
            env_stereo_frame(have_eyes ? &ey : NULL, pv); /* SoM loading env: world mode keeps its description */
            X.last_stereo = 1;
            if (hud_copied) mn_set_kind(hd.kind), hud_quad(&X.last_quad, &X.open_rec);
            int hud_shown = hud_copied && !cv_hud_hidden(); /* SOMVR_XR_COVER: not the game's black cover */
            int space = cv_space_shown();                    /* ... its load: the loading space over this frame */
            X.last_hud = hud_shown;
            somvr_posef mb_eye[2] = {X.open_rec.eye[0].pose, X.open_rec.eye[1].pose};
            mb_frame(ds, hud_shown && mn_hud_menu(), mb_eye); /* SOMVR_XR_MENU_BACK: a menu board's backdrop */
            X.n_stereo++;
            if (X.sh) atomic_fetch_add_explicit(&X.sh->n_stereo, 1, memory_order_relaxed);
            /* the pipeline lead (7.1): how much later this frame displays than the wait its views' time came from */
            int64_t raw = 0;
            int meas = somvr_lead_measure(&X.lead, X.open_rec.display_time, X.fs.predictedDisplayTime, &raw);
            int64_t d = X.lead.lead - (X.lead_logged < 0 ? 0 : X.lead_logged);
            if (meas == 1 && (X.lead_logged < 0 || d > 2000000 || d < -2000000) &&
                (X.lead_lines < 8 || X.n_stereo % 600 == 0)) {
                X.lead_lines++;
                lg("xr: prediction lead %+.3f ms (median of %u; this frame %+.3f ms, %.2f periods of %.3f ms)%s",
                   (double)X.lead.lead / 1e6, X.lead.nwin < SOMVR_LEAD_WIN ? X.lead.nwin : SOMVR_LEAD_WIN,
                   (double)raw / 1e6, X.fs.predictedDisplayPeriod ? (double)raw / (double)X.fs.predictedDisplayPeriod : 0.0,
                   (double)X.fs.predictedDisplayPeriod / 1e6,
                   cfg.predict == SOMVR_PREDICT_AUTO ? ": published from the next xrWaitFrame on" : " (not applied)");
                X.lead_logged = X.lead.lead;
            } else if (meas < 0 && X.lead.rejected <= 3) {
                lg("xr: prediction: frame %llu displays %+.3f ms after its views' wait: not used (0..250 ms)",
                   (unsigned long long)X.open_rec.frame, (double)raw / 1e6);
            }
            if (X.n_stereo <= 3 || X.n_stereo % 600 == 0)
                lg("xr: stereo frame %llu (tag %llu, applied 0x%x, flags 0x%x, display-time error %+.3f ms): views "
                   "%+.4f,%+.4f,%+.4f / %+.4f,%+.4f,%+.4f fov %.4f..%.4f / %.4f..%.4f rects %d+%d / %d+%d "
                   "(stereo %llu, reuse %llu, mono %llu); fov L/R/U/D %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f, lead "
                   "%+.3f ms (%s %+.3f ms), period %.3f ms",
                   (unsigned long long)X.open_rec.frame, (unsigned long long)tag.seq, X.open_rec.applied,
                   X.open_rec.flags, (double)(X.open_rec.display_time - X.fs.predictedDisplayTime) / 1e6,
                   pv[0].pose.position.x, pv[0].pose.position.y, pv[0].pose.position.z,
                   pv[1].pose.position.x, pv[1].pose.position.y, pv[1].pose.position.z, pv[0].fov.angleLeft,
                   pv[0].fov.angleRight, pv[1].fov.angleLeft, pv[1].fov.angleRight, pv[0].subImage.imageRect.offset.x,
                   pv[0].subImage.imageRect.extent.width, pv[1].subImage.imageRect.offset.x,
                   pv[1].subImage.imageRect.extent.width, (unsigned long long)X.n_stereo, (unsigned long long)X.n_reuse,
                   (unsigned long long)X.n_mono, pv[0].fov.angleLeft, pv[0].fov.angleRight, pv[0].fov.angleUp,
                   pv[0].fov.angleDown, pv[1].fov.angleLeft, pv[1].fov.angleRight, pv[1].fov.angleUp,
                   pv[1].fov.angleDown, (double)X.lead.lead / 1e6,
                   meas == 1 ? "this frame" : meas < 0 ? "rejected" : "not measured", (double)raw / 1e6,
                   (double)X.fs.predictedDisplayPeriod / 1e6);
            vm_frame(ds); /* the VR menu first: while its panel is open the game's pointer has no target */
            if (X.ptr_on) { /* the pointer: on the world-locked HUD quad (want PTR_HUD), else no target this frame */
                int on_hud = ((X.ptr_hud && cfg.hud == SOMVR_HUD_WORLD) || mn_hud_menu()) && hud_shown && !vm_has_hand();
                float eye[3], qs[2] = {X.last_quad.size.width, X.last_quad.size.height};
                for (int i = 0; i < 3; i++) eye[i] = 0.5f * (X.open_rec.eye[0].pose.p[i] + X.open_rec.eye[1].pose.p[i]);
                ptr_frame(ds, on_hud ? SOMVR_PTRQ_HUD : SOMVR_PTRQ_NONE, &X.last_quad.pose, qs, X.hud_w, X.hud_h, eye);
            }
            xr_comp sc = {.l = {[XL_OWN] = space ? NULL : proj_layer(&proj, pv)}};
            if (space) VH.last = 0; /* no game hands over the loading space (ours: cv_space_comp) */
            else mb_layers(&sc);
            if (hud_shown && !space) { /* the pointer's layers come only with the HUD quad */
                sc.l[XL_HUD] = (const XrCompositionLayerBaseHeader *)&X.last_quad;
                sc.l[XL_MMAP] = mn_mm_layer(&X.open_rec); /* build 6: the wrist minimap */
                sc.l[XL_BAND] = mn_band_layer();          /* build 8: the prompt band */
                for (uint32_t i = 0; i < X.ptr_nq && i < 2; i++)
                    sc.l[XL_PTR0 + i] = (const XrCompositionLayerBaseHeader *)&X.ptr_q[i];
            }
            X.last_fade = somvr_rec_fade(X.open_rec.flags); /* the comfort fade (SOMVR_FALLBACK): the last layer */
            X.last_fade_red = (X.open_rec.flags & SOMVR_REC_FADE_RED) != 0;
            if ((X.open_rec.flags & SOMVR_REC_DUMP) && CF.dump_marked > 0) { /* SOMVR_XR_DUMP_MARKED (tests) */
                CF.dump_marked--;
                lg("xr: dump mark: record frame %llu (stereo %llu): a dump of the next stereo frame %llu: %s",
                   (unsigned long long)X.open_rec.frame, (unsigned long long)X.n_eyes,
                   (unsigned long long)X.n_eyes + 1, dump_next());
            }
            X.last_fade_eye[0] = X.open_rec.eye[0].pose, X.last_fade_eye[1] = X.open_rec.eye[1].pose;
            if (space) cv_space_comp(ds, &sc); /* SOMVR_XR_COVER's load: the sky, the floor, the lights, our hands */
            else fade_frame(ds, env_level(X.last_fade), X.last_fade_eye, fade_red(env_level(X.last_fade))); /* (env) */
            vm_comp(&sc);                    /* the VR menu's panel, ray, dot */
            if (!space) {
                vh_frame(ds, qi->q, &X.open_rec, pv, &sc, have_eyes ? &ey : NULL); /* build 10: our hands over the panel
                                                                                     * and the watch (occluders) */
                cf_layers(ds, &X.open_rec, &sc); /* comfort: the ring under the HUD, the blink over it */
                th_layers(ds, &X.open_rec, &sc); /* the threat cues: the glows under the ring, the edge bands over it */
            }
            fade_comp(&sc);
            mn_dump_layers(pv, &sc); /* build 6: a dumped frame's quads (SOMVR_XR_DUMP), before the end clears them */
            xr_end_comp(&sc);
            if (hud_shown && !space && X.hud_on) { /* the frame ended with it (a rejected quad turned the HUD off) */
                X.n_hud++;
                atomic_fetch_add_explicit(&X.hud->n_quads, 1, memory_order_relaxed);
                if (X.n_hud <= 3 || X.n_hud % 600 == 0)
                    lg("xr: hud quad frame %llu: HUD image %#llx %ux%u format %d gen %u (%s) -> swapchain image %u, "
                       "%s-locked %.3f x %.3f m at %+.3f,%+.3f,%+.3f, flags 0x%llx (%llu so far)",
                       (unsigned long long)X.open_rec.frame, (unsigned long long)(uintptr_t)hd.img, hd.w, hd.h, hd.fmt,
                       hd.gen, (hd.kind & 0xffu) == SOMVR_HUDK_VIDEO ? "video" : hd.kind & SOMVR_HUDF_MENU ? "world, a menu" : "world", X.hud_xi,
                       cfg.hud == SOMVR_HUD_HEAD ? "head" : "world", X.last_quad.size.width, X.last_quad.size.height,
                       X.last_quad.pose.position.x, X.last_quad.pose.position.y, X.last_quad.pose.position.z,
                       (unsigned long long)X.last_quad.layerFlags, (unsigned long long)X.n_hud);
            }
        } else {
            xr_end(NULL);
        }
        return replaced;
    }

    /* mono: the whole image as a screen (quad) or as both eyes of a projection at the located poses */
    VH.last = 0; /* build 10: no hands on a mono frame (nor on a reuse of it) */
    if (xr_begin()) return 0;
    X.n_mono++;
    if (X.sh) atomic_fetch_add_explicit(&X.sh->n_mono, 1, memory_order_relaxed);
    if (X.fs.shouldRender && can_copy) {
        if (xr_acquire()) return 0;
        if (D.n || ss_pending()) dump_arm(ds, 1, X.n_mono); /* (SSN: a mark) */
        replaced = copy_submit(ds, qi->q, s, idx, X.img[X.xi], pi, 0, 1);
        if (replaced) {
            *sem = s->slot[idx].sem;
            X.copies++;
            X.last_stereo = X.last_hud = 0; /* the released image is mono now */
            if (D.armed) dump_write(ds, &s->slot[idx], 1);
        }
        D.armed = 0;
        if (xr_release()) return replaced;
        if (!replaced) return xr_fail("our copy submit", XR_ERROR_RUNTIME_FAILURE), 0;
    }
    const XrCompositionLayerBaseHeader *layer = NULL;
    XrCompositionLayerQuad quad;
    int32_t rc[4];
    somvr_eye_rect(w, h, 0, rc);
    if (replaced && cfg.layer == SOMVR_LAYER_QUAD) {
        float sz[2];
        somvr_quad_size(w, h, QUAD_WIDTH_M, sz);
        quad = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                        .space = X.space,
                                        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                        .subImage = {X.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                        .pose = {{0, 0, 0, 1}, {0, 0, -QUAD_DIST_M}},
                                        .size = {sz[0], sz[1]}};
        mn_mono_quad(&quad, (uint32_t)rc[2], (uint32_t)rc[3]); /* build 6: on the board in front of the head */
        vd_theatre(&quad, (uint32_t)rc[2], (uint32_t)rc[3]);  /* never a void: a video, the cinematic window */
        layer = (const XrCompositionLayerBaseHeader *)&quad;
    } else if (replaced) {
        XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                               .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                               .displayTime = X.fs.predictedDisplayTime,
                               .space = X.space};
        XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
        XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
        uint32_t nv = 0;
        const XrViewStateFlags need = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
        if (XR_SUCCEEDED(r) && nv == 2 && (vs.viewStateFlags & need) == need) {
            for (int e = 0; e < 2; e++)
                pv[e] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                                                           .pose = v[e].pose,
                                                           .fov = v[e].fov,
                                                           .subImage = {X.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0}};
            layer = proj_layer(&proj, pv);
        }
    }
    if (X.frames < 3 || X.n_mono % 600 == 0)
        lg("xr: mono frame (tag %s, frame %llu): shouldRender %d, game image %u, copied %d, layer %s (stereo %llu, "
           "reuse %llu, mono %llu)", have_tag ? "yes" : "no", (unsigned long long)tag.frame, (int)X.fs.shouldRender, idx,
           replaced, layer ? (cfg.layer == SOMVR_LAYER_QUAD ? "quad" : "proj") : "none", (unsigned long long)X.n_stereo,
           (unsigned long long)X.n_reuse, (unsigned long long)X.n_mono);
    vm_frame(ds); /* the VR menu first: while its panel is open the game's pointer has no target */
    if (X.ptr_on) { /* the pointer: on the menu quad, else no target this frame */
        int is_quad = layer == (const XrCompositionLayerBaseHeader *)&quad && !vm_has_hand();
        float eye[3], qs[2] = {is_quad ? quad.size.width : 0, is_quad ? quad.size.height : 0};
        int have_eye = is_quad && X.ptr_draw && cfg.ptr == SOMVR_PTRM_ON && ptr_head(eye);
        ptr_frame(ds, is_quad ? SOMVR_PTRQ_MENU : SOMVR_PTRQ_NONE, is_quad ? &quad.pose : NULL, qs,
                  is_quad ? (uint32_t)rc[2] : 0, is_quad ? (uint32_t)rc[3] : 0, have_eye ? eye : NULL);
    }
    xr_comp mc = {.l = {[XL_OWN] = layer}}; /* the menu quad (or the projection), the pointer's ray and dot over it */
    if (EV.on && layer && layer == (const XrCompositionLayerBaseHeader *)&quad)
        env_mono_layers(ds, &quad, &mc); /* SoM loading env: its layers under the quad, its fade */
    for (uint32_t i = 0; layer && i < X.ptr_nq && i < 2; i++)
        mc.l[XL_PTR0 + i] = (const XrCompositionLayerBaseHeader *)&X.ptr_q[i];
    if (layer) vm_comp(&mc); /* the VR menu's panel, ray, dot (over the pointer's) */
    if (EV.on && layer && layer == (const XrCompositionLayerBaseHeader *)&quad)
        vd_hands(ds, &mc, 0); /* never a void (SOMVR_XR_LOADSPACE): our hands in the loading space */
    fade_comp(&mc);
    xr_end_comp(&mc);
    return replaced;
}

/* After the real present: wait for the next frame (this paces the game) and publish its predicted time. */
static void xr_after_present(void) {
    if (X.phase == P_SESSION && X.running && !X.frame_open && !X.waited) xr_wait_publish();
}

/* ---- the hooks ------------------------------------------------------------------------------------------------ */
static VKAPI_ATTR VkResult VKAPI_CALL L_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    q_st *qi = LIST_FIND(g_queues, q, q);
    dev_st *ds = qi ? qi->ds : LIST_FIND(g_devs, key, dkey(q));
    if (!ds) return VK_ERROR_DEVICE_LOST; /* unreachable: the device is always known */
    VkSemaphore sem = VK_NULL_HANDLE;
    int replaced = 0;
    if (qi && pi && pi->swapchainCount) {
        int64_t t0 = vd_now(); /* never a void: the gap before this present */
        atomic_store(&VD.presenting, 1); /* the watcher leaves X.mx to it */
        pthread_mutex_lock(&X.mx);
        if (X.ds == ds && (X.phase == P_DEV || X.phase == P_SESSION)) {
            int sess = X.phase == P_SESSION;
            vd_before();
            replaced = xr_frame(ds, qi, pi, &sem);
            if (sess && X.phase == P_SESSION) vd_after(t0);
        }
        pthread_mutex_unlock(&X.mx);
    }
    VkResult r;
    int ql = ka_qlock(q); /* SOMVR_XR_KEEPALIVE: not while the watcher ends a frame */
    if (!replaced) {
        r = ds->QueuePresentKHR(q, pi);
    } else {
        VkPresentInfoKHR m = *pi;
        m.waitSemaphoreCount = 1;
        m.pWaitSemaphores = &sem;
        r = ds->QueuePresentKHR(q, &m);
    }
    ka_qunlock(ql);
    if (qi && pi && pi->swapchainCount) {
        pthread_mutex_lock(&X.mx);
        if (X.ds == ds) xr_after_present();
        pthread_mutex_unlock(&X.mx);
        atomic_store(&VD.presenting, 0);
        vd_test_stall(); /* SOMVR_XR_TEST `stall MS`: a load that stops presenting */
    }
    return r;
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
    if (D.ds == ds) dump_free();
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
        is->GetPhysicalDeviceMemoryProperties =
            (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(*out, "vkGetPhysicalDeviceMemoryProperties");
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
/* layout B: remember the images made at the size the preload announced for its eye textures, and (H11) for its HUD
 * texture (any thread) */
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateImage(VkDevice dev, const VkImageCreateInfo *ci,
                                                    const VkAllocationCallbacks *alloc, VkImage *out) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = ds->CreateImage(dev, ci, alloc, out);
    somvr_shared *sh = atomic_load(&g_sh_seen);
    somvr_hud_shared *hud = atomic_load(&g_hud_seen);
    if (r != VK_SUCCESS || !sh || !ci || ci->imageType != VK_IMAGE_TYPE_2D || ci->mipLevels != 1 ||
        ci->arrayLayers != 1 || ci->samples != VK_SAMPLE_COUNT_1_BIT || !ci->extent.width)
        return r;
    int eye = ci->extent.width == atomic_load(&sh->eyes.expect_w) && ci->extent.height == atomic_load(&sh->eyes.expect_h);
    int hd = hud && ci->extent.width == atomic_load(&hud->expect_w) && ci->extent.height == atomic_load(&hud->expect_h);
    if (!eye && !hd) return r;
    pthread_mutex_lock(&g_cand_mx);
    int rank = 0;
    uint32_t i = cand_slot(sh, hud, &rank);
    VkImage old = g_cand[i].img;
    g_cand[i].img = *out, g_cand[i].dev = dev, g_cand[i].fmt = ci->format, g_cand[i].usage = ci->usage;
    g_cand[i].w = ci->extent.width, g_cand[i].h = ci->extent.height, g_cand[i].used = 0;
    pthread_mutex_unlock(&g_cand_mx);
    if (rank >= 2) /* 32 entries all in use by the preload's textures: cannot happen with two eyes and one HUD */
        lg("vk: image candidates full: %#llx (%s) dropped for %#llx", (unsigned long long)(uintptr_t)old,
           rank == 3 ? "published now" : "used before", (unsigned long long)(uintptr_t)*out);
    lg("vk: image %#llx %ux%u format %d usage 0x%x: %s", (unsigned long long)(uintptr_t)*out, ci->extent.width,
       ci->extent.height, ci->format, ci->usage,
       eye && hd ? "an eye image candidate and a HUD image candidate" : eye ? "an eye image candidate"
                                                                         : "a HUD image candidate");
    return r;
}

static VKAPI_ATTR void VKAPI_CALL L_DestroyImage(VkDevice dev, VkImage img, const VkAllocationCallbacks *alloc) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds) return;
    if (img) {
        pthread_mutex_lock(&g_cand_mx);
        for (int i = 0; i < MAX_EYE_CAND; i++)
            if (g_cand[i].img == img && g_cand[i].dev == dev) memset(&g_cand[i], 0, sizeof g_cand[i]);
        pthread_mutex_unlock(&g_cand_mx);
    }
    ds->DestroyImage(dev, img, alloc);
}

static PFN_vkVoidFunction dev_hook(const char *n) {
    PFN_vkVoidFunction kf = ka_dev_hook(n); /* SOMVR_XR_KEEPALIVE: the game's queue calls under the keep-alive's lock */
    if (kf) return kf;
    static const struct {
        const char *n;
        PFN_vkVoidFunction f;
    } t[] = {
        {"vkQueuePresentKHR", (PFN_vkVoidFunction)L_QueuePresentKHR},
        {"vkGetDeviceQueue", (PFN_vkVoidFunction)L_GetDeviceQueue},
        {"vkGetDeviceQueue2", (PFN_vkVoidFunction)L_GetDeviceQueue2},
        {"vkCreateSwapchainKHR", (PFN_vkVoidFunction)L_CreateSwapchainKHR},
        {"vkDestroySwapchainKHR", (PFN_vkVoidFunction)L_DestroySwapchainKHR},
        {"vkCreateImage", (PFN_vkVoidFunction)L_CreateImage},
        {"vkDestroyImage", (PFN_vkVoidFunction)L_DestroyImage},
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
