/* somvr_xrloc.h - the published locates of the PE bridge (docs/re-winport.md 3.1, 7.1).
 *
 * Under Proton the game-side code is a Windows DLL and the OpenXR runtime is reached only by the Linux Vulkan layer:
 * no PE code ever calls a Linux function. The layer's locate thread (vklayer, every 1 ms while the session runs)
 * locates the views and the 4 action spaces (grip L/R, aim L/R) at the newest predicted display time and one display
 * period later, and publishes them here under a seqlock. The DLL owns this object (its address is in the bridge
 * file with the other shared objects) and publishes its own xrLocateViews / xrLocateSpace stand-ins, which the layer
 * stores in somvr_shared.xr.locate_views / somvr_input.xr.locate_space in place of the runtime's functions: the
 * game-side code (somvr_views.c, somvr_hands.c) is unchanged and its calls answer from the newest sample (<= ~1 ms
 * old), at the sample time nearest to the one asked.
 */
#ifndef SOMVR_XRLOC_H
#define SOMVR_XRLOC_H
#include "somvr_shared.h"

#define SOMVR_XRLOC_SYMBOL "somvr_xrloc"
#define SOMVR_XRLOC_MAGIC 0x434f4c5258564d53ull /* "SMVXRLOC" */
#define SOMVR_XRLOC_VERSION 2u
#define SOMVR_XRLOC_NSPACE 4 /* grip L, grip R, aim L, aim R (somvr_input.xr.grip_space / aim_space order) */
#define SOMVR_XRLOC_NSAMPLE 2
#define SOMVR_XRLOC_NIMG 16

typedef struct {
    int32_t result;        /* XrResult of xrLocateSpace */
    uint32_t flags;        /* XrSpaceLocationFlags */
    uint32_t vflags;       /* XrSpaceVelocityFlags */
    uint32_t pad;
    somvr_posef pose;
    float lin[3], ang[3];  /* XrSpaceVelocity: linear (m/s), angular (rad/s) */
} somvr_xrloc_space;

typedef struct {
    int64_t time;          /* the XrTime located for; 0 = empty */
    int32_t result;        /* XrResult of xrLocateViews */
    uint32_t view_flags;   /* XrViewStateFlags */
    uint32_t n;            /* views located (2) */
    uint32_t pad;
    somvr_posef pose[2];
    somvr_fovf fov[2];
    somvr_xrloc_space space[SOMVR_XRLOC_NSPACE];
} somvr_xrloc_sample;

typedef struct somvr_xrloc {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t running;      /* layer: 1 while its locate thread publishes */
    uint32_t period_us;            /* layer: the thread's period (1000) */
    uint64_t session, base_space;  /* layer: the XrSession and the XrSpace located in (= somvr_shared.xr.space) */
    uint64_t space[SOMVR_XRLOC_NSPACE]; /* layer: the XrSpace of each space[] slot (0 = not located) */
    _Atomic uint64_t seq;          /* seqlock over sample[] (the layer writes) */
    somvr_xrloc_sample sample[SOMVR_XRLOC_NSAMPLE]; /* [0] at the predicted time, [1] one display period later */
    _Atomic uint64_t ticks, views_fail, space_fail; /* layer */
    _Atomic uint64_t n_views, n_spaces, n_stale, n_nospace; /* DLL: calls answered, answers older than 5 ms or
                                                             * from no sample, spaces not published */
    /* v2, DLL: the VkImages of the mod's own D3D11 textures (eyes, HUD, hands' depth) with the layout DXVK keeps each
     * in while idle (IDXGIVkInteropSurface::GetVulkanImageInfo). The layer's copies leave from and return to that
     * layout where Feral's images sat in TRANSFER_DST_OPTIMAL (SOMVR_EYE_IMAGE_LAYOUT). img_layout[i] is written
     * before n_img counts it (release). */
    _Atomic uint32_t n_img;
    uint32_t pad2;
    uint64_t img[SOMVR_XRLOC_NIMG];
    uint32_t img_layout[SOMVR_XRLOC_NIMG];
    uint64_t nonce;                /* DLL: the bridge file's nonce (the file is this process's iff they match) */
} somvr_xrloc;

_Static_assert(sizeof(somvr_xrloc_space) == 68, "somvr_xrloc_space ABI");
_Static_assert(sizeof(somvr_xrloc_sample) == 24 + 56 + 32 + 4 * 68, "somvr_xrloc_sample ABI");
_Static_assert(offsetof(somvr_xrloc, seq) == 72 && offsetof(somvr_xrloc, sample) == 80, "somvr_xrloc ABI");

static inline int somvr_xrloc_ok(const somvr_xrloc *x) {
    return x && x->magic == SOMVR_XRLOC_MAGIC && x->version == SOMVR_XRLOC_VERSION && x->size == sizeof *x;
}

/* the idle layout the DLL recorded for VkImage img; -1 = not one of the DLL's images */
static inline int64_t somvr_xrloc_layout(const somvr_xrloc *x, uint64_t img) {
    uint32_t n = x ? atomic_load_explicit(&((somvr_xrloc *)x)->n_img, memory_order_acquire) : 0;
    for (uint32_t i = 0; i < n && i < SOMVR_XRLOC_NIMG; i++)
        if (x->img[i] == img) return (int64_t)x->img_layout[i];
    return -1;
}

/* the sample whose time is nearest to t (copied under the seqlock); 0 = none yet */
static inline int somvr_xrloc_pick(somvr_xrloc *x, int64_t t, somvr_xrloc_sample *out) {
    somvr_xrloc_sample s[SOMVR_XRLOC_NSAMPLE];
    if (!somvr_seq_read(&x->seq, s, x->sample, sizeof s)) return 0;
    int best = -1;
    uint64_t bd = 0;
    for (int i = 0; i < SOMVR_XRLOC_NSAMPLE; i++) {
        if (!s[i].time) continue;
        uint64_t d = s[i].time > t ? (uint64_t)(s[i].time - t) : (uint64_t)(t - s[i].time);
        if (best < 0 || d < bd) best = i, bd = d;
    }
    if (best < 0) return 0;
    *out = s[best];
    return 1;
}

/* the bridge file (SOMVR_BRIDGE=<unix path>): text lines "name 0xADDR", written by the DLL once its objects exist,
 * read by the layer in place of dlsym(RTLD_DEFAULT); "nonce X" = somvr_xrloc.nonce: the layer takes the file only
 * if somvr_xrloc at its address is readable here and holds that nonce (another process's stale file never matches).
 * "pid" is informational: Wine opens files in the wineserver, so the DLL's /proc/self is the server's. */
#define SOMVR_BRIDGE_ENV "SOMVR_BRIDGE"
#define SOMVR_BRIDGE_HEAD "somvr-bridge 1"

#endif
