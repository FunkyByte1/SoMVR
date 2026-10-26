/* somvr_input.h - the motion-controller ABI (docs/re-xr-input.md section 2): a second exported object next to
 * `somvr_shared`, so the core struct stays version 2 byte for byte (a v2 layer and a v2 preload keep working).
 * The preload owns and exports it (zero-initialised static); the Vulkan layer finds it with
 * dlsym(RTLD_DEFAULT, SOMVR_INPUT_SYMBOL), exactly like `somvr_shared`. C11, header only, no OpenXR/Vulkan includes.
 *
 * Rules
 *   - The preload fills magic, version, size, want, pid and sets `active` (release) when it activates somvr_shared;
 *     SOMVR_INPUT=off leaves the object zero and the layer then makes no OpenXR action call at all. Absent object,
 *     inactive object or want without SOMVR_INW_STATE = no input (the layer's SOMVR_XR_INPUT=log still logs).
 *   - Readers check somvr_input_ok(): magic, version >= 1, size >= SOMVR_INPUT_V1_SIZE, active. Writers only append
 *     fields (a version 2 grows `size`), never re-purpose one.
 *   - xr: written by the layer before ready = 1 (release), per session. The game thread uses the spaces and
 *     locate_space only inside the core guard (somvr_xr_enter(somvr_shared)) and with ready == 1. Teardown: the core
 *     somvr_xr_stop() (no new users, none in flight) -> ready = 0 -> xrDestroySpace x 4 -> the session. The spaces are
 *     published only if want has SOMVR_INW_SPACES and somvr_shared is active (its guard protects them).
 *   - st: the layer is the only writer, once per xrSyncActions (right after xrWaitFrame), under the seqlock `seq`.
 *     A failed read (64 tries) = no new input this tick (keep the last snapshot; sync_seq unchanged).
 *   - hap[h]: the game thread is the only writer (somvr_haptic_post), the layer the only reader (somvr_haptic_take);
 *     each new request number is applied once, at the layer's next present. Requests posted before the session runs
 *     are dropped (the layer takes the current number as its baseline when the session begins).
 *   - Poses: the XR space = somvr_shared.xr.space (the eye records' space), XrPosef layout (= somvr_eye_rec.pose).
 *   - v2 (SOMVR_POINTER, docs/re-xr-pipeline.md 10): ptr, the laser pointer. The layer is the only writer, once per frame
 *     it computes the pointer for (want has SOMVR_INW_POINTER and the object is v2-sized: somvr_input_has_ptr), under
 *     the seqlock ptr_seq; the game thread reads it (somvr_ptr_read). A v1 layer never touches it; a v2 layer with a
 *     v1 object (480 bytes) never writes it.
 */
#ifndef SOMVR_INPUT_H
#define SOMVR_INPUT_H

#include "somvr_shared.h"

#define SOMVR_INPUT_SYMBOL "somvr_input"
#define SOMVR_INPUT_MAGIC 0x31504e49524d4f53ull /* "SOMRINP1" */
#define SOMVR_INPUT_VERSION 2u /* 2: the laser pointer block (ptr) appended */
#define SOMVR_INPUT_V1_SIZE 480u
#define SOMVR_INPUT_V2_SIZE 616u

enum { SOMVR_HAND_L = 0, SOMVR_HAND_R = 1 };
enum { SOMVR_PROF_NONE = 0, SOMVR_PROF_TOUCH = 1, SOMVR_PROF_INDEX = 2, SOMVR_PROF_SIMPLE = 3, SOMVR_PROF_OTHER = 4 };
enum {                                   /* somvr_hand_in.buttons: held now */
    SOMVR_BTN_LOWER = 1u << 0,           /* A (right) / X (left) */
    SOMVR_BTN_UPPER = 1u << 1,           /* B / Y */
    SOMVR_BTN_STICK = 1u << 2,           /* thumbstick click */
    SOMVR_BTN_MENU = 1u << 3,            /* left hand only */
    SOMVR_BTN_TRIGGER_TOUCH = 1u << 4,
    SOMVR_BTN_THUMB_TOUCH = 1u << 5,
};
#define SOMVR_NBTN 8                     /* bits 0..7 have press counters */
enum { SOMVR_HACT_POSE = 1u, SOMVR_HACT_INPUT = 2u };                            /* somvr_hand_in.active */
enum { SOMVR_INS_FOCUSED = 1u, SOMVR_INS_SYNCED = 2u, SOMVR_INS_LOCATED = 4u };  /* somvr_input_state.flags */
enum { SOMVR_INW_STATE = 1u, SOMVR_INW_HAPTICS = 2u, SOMVR_INW_SPACES = 4u,     /* somvr_input_shared.want */
       SOMVR_INW_POINTER = 8u,   /* v2: the layer computes the laser pointer on the menu quad, publishes ptr, draws it */
       SOMVR_INW_PTR_HUD = 16u }; /* v2: ... also on the world-locked HUD quad (stereo frames) */
enum { SOMVR_HAP_NONE = 0, SOMVR_HAP_COMBAT = 1, SOMVR_HAP_RUMBLE = 2, SOMVR_HAP_UI = 3, SOMVR_HAP_TEST = 4 };

typedef struct {
    somvr_posef grip;          /*   0: XR space = somvr_shared.xr.space (the eye records' space), metres (= XrPosef) */
    somvr_posef aim;           /*  28 */
    float lin_vel[3];          /*  56: grip, m/s, XR space */
    float ang_vel[3];          /*  68: grip, rad/s, XR space */
    float trigger, squeeze;    /*  80, 84: 0..1 */
    float stick[2];            /*  88: x right, y forward, -1..1 as the runtime gives it (no dead zone) */
    uint32_t buttons;          /*  96: SOMVR_BTN_* */
    uint8_t grip_flags;        /* 100: XrSpaceLocationFlags of grip (1 orientation valid, 2 position valid, 4/8 tracked) */
    uint8_t aim_flags;         /* 101 */
    uint8_t vel_flags;         /* 102: XrSpaceVelocityFlags of grip (1 linear, 2 angular) */
    uint8_t active;            /* 103: SOMVR_HACT_*: the pose / the button actions are bound and active */
    uint8_t presses[SOMVR_NBTN]; /* 104: rising edges per button bit, counted by the layer, never reset (wrap at 256) */
} somvr_hand_in;               /* 112 */

typedef struct {
    uint64_t sync_seq;         /*   0: successful xrSyncActions so far (XR_SUCCESS or XR_SESSION_NOT_FOCUSED); 0 = none */
    int64_t time;              /*   8: XrTime the poses are located for (the prediction of the xrWaitFrame before) */
    int64_t sync_ns;           /*  16: CLOCK_MONOTONIC at the sync (latency logs) */
    uint32_t flags;            /*  24: SOMVR_INS_* */
    uint16_t profile[2];       /*  28: SOMVR_PROF_* per hand */
    somvr_hand_in hand[2];     /*  32 */
} somvr_input_state;           /* 256 */

typedef struct {
    uint32_t n;                /*   0: request number, +1 per new request (0 = none yet) */
    uint32_t source;           /*   4: SOMVR_HAP_* (logs) */
    float amplitude;           /*   8: 0..1; 0 = stop now */
    float frequency;           /*  12: Hz; 0 = XR_FREQUENCY_UNSPECIFIED */
    int64_t duration_ns;       /*  16: > 0, or -1 = XR_MIN_HAPTIC_DURATION */
} somvr_haptic_req;            /* 24 */

/* v2: the laser pointer (docs/re-ui-pointer.md 3-4, re-xr-pipeline.md 10; ../somvr_pointer.h), layer -> preload */
enum { SOMVR_PTRQ_NONE = 0, SOMVR_PTRQ_MENU = 1, SOMVR_PTRQ_HUD = 2, /* somvr_ptr_pub.quad: what the pointer aims at */
       SOMVR_PTRQ_VRMENU = 3 }; /* the VR settings menu's wrist panel (somvr_vrmenu.h): the layer's own pointer state,
                                   never published in ptr (the game's pointer has no target while it has the hand) */
enum {                                   /* somvr_ptr_pub.flags */
    SOMVR_PTRF_ON = 1u,                  /* the pointing hand's ray is on the quad (hysteresis), or drags a held click */
    SOMVR_PTRF_DOWN = 2u,                /* a click is held (the pointing hand's trigger) */
    SOMVR_PTRF_AIM = 4u,                 /* the pointing hand has a ray this frame */
    SOMVR_PTRF_GRIP = 8u,                /* ... from its grip pose (no valid aim pose) */
    SOMVR_PTRF_DRAWN = 16u,              /* the layer drew the dot (and the ray) this frame */
};
typedef struct {
    uint64_t frame;            /*   0: the layer's xrEndFrame count it was computed for (1, 2, ...; 0 = never) */
    int64_t time;              /*   8: XrTime of the aim poses (that frame's predicted display time) */
    int64_t mono_ns;           /*  16: CLOCK_MONOTONIC when written (the reader's staleness test) */
    uint32_t quad;             /*  24: SOMVR_PTRQ_*: the quad of that frame the pointer was computed against */
    uint32_t flags;            /*  28: SOMVR_PTRF_* */
    int32_t hand;              /*  32: the pointing hand (SOMVR_HAND_*): the last one that pulled its trigger */
    uint32_t consume;          /*  36: bit h: hand h's trigger is the pointer's now (its ray on the quad, its click held) */
    float u, v;                /*  40: with ON: the point on the quad, smoothed, 0..1 (u right, v down = the image's x, y) */
    uint32_t downs, ups;       /*  48: clicks begun / ended (never reset; an end follows every begin) */
    float dist;                /*  56: with ON: metres from the ray's origin to the point */
    uint32_t img_w, img_h;     /*  60: the image the quad shows (the presented game image / the HUD image) */
    somvr_posef quad_pose;     /*  68: that quad's pose (XR space = somvr_shared.xr.space) */
    float quad_size[2];        /*  96: its width and height (m) */
} somvr_ptr_pub;               /* 104 */

typedef struct somvr_input_shared {
    uint64_t magic;                      /*   0 */
    uint32_t version, size;              /*   8, 12 */
    _Atomic uint32_t active;             /*  16: set last by the preload (release) */
    uint32_t want;                       /*  20: SOMVR_INW_* */
    int32_t pid;                         /*  24 */
    uint32_t pad0;                       /*  28 */
    struct {                             /*  32: layer -> preload, per session, written before ready */
        _Atomic uint32_t ready;          /*  32: 1 = the spaces are valid while somvr_shared.xr.state == RUNNING */
        uint32_t profiles;               /*  36: bit p = the runtime took the suggestions of SOMVR_PROF_p */
        uint64_t grip_space[2];          /*  40: XrSpace */
        uint64_t aim_space[2];           /*  56 */
        void *locate_space;              /*  72: PFN_xrLocateSpace */
        void *locate_spaces;             /*  80: PFN_xrLocateSpacesKHR or NULL (XR_KHR_locate_spaces, optional) */
    } xr;
    _Atomic uint64_t seq;                /*  88: seqlock over st (the layer writes once per sync) */
    somvr_input_state st;                /*  96 */
    struct {
        _Atomic uint64_t seq;            /* 352 / 384: seqlock (the game thread writes) */
        somvr_haptic_req req;
    } hap[2];
    /* 416: n_syncs (XR_SUCCESS syncs), n_unfocused (XR_SESSION_NOT_FOCUSED syncs), n_state_reads (xrGetActionState*
     * calls): the layer; n_hand_locates, n_hand_fail: the game thread's grip locates; n_hap_posts: the game thread;
     * n_hap_applied (xrApplyHapticFeedback), n_hap_stopped (xrStopHapticFeedback): the layer */
    _Atomic uint64_t n_syncs, n_unfocused, n_state_reads, n_hand_locates, n_hand_fail, n_hap_posts, n_hap_applied,
        n_hap_stopped;
    /* v2 (480): the laser pointer (want SOMVR_INW_POINTER) */
    _Atomic uint64_t ptr_seq;            /* 480: seqlock over ptr (the layer writes once per frame it computes) */
    somvr_ptr_pub ptr;                   /* 488 */
    /* 592: the layer's frames with the pointer computed / on the quad / with the dot drawn */
    _Atomic uint64_t n_ptr_frames, n_ptr_on, n_ptr_drawn;
} somvr_input_shared;                    /* 616 */

_Static_assert(sizeof(somvr_hand_in) == 112 && offsetof(somvr_hand_in, buttons) == 96 &&
                   offsetof(somvr_hand_in, presses) == 104, "somvr_hand_in layout");
_Static_assert(sizeof(somvr_input_state) == 256 && offsetof(somvr_input_state, hand) == 32, "somvr_input_state");
_Static_assert(sizeof(somvr_haptic_req) == 24, "somvr_haptic_req size");
_Static_assert(offsetof(somvr_input_shared, xr) == 32 && offsetof(somvr_input_shared, seq) == 88 &&
                   offsetof(somvr_input_shared, st) == 96 && offsetof(somvr_input_shared, hap) == 352 &&
                   offsetof(somvr_input_shared, n_syncs) == 416, "somvr_input_shared offsets");
_Static_assert(offsetof(somvr_input_shared, xr.grip_space) == 40 && offsetof(somvr_input_shared, xr.aim_space) == 56 &&
                   offsetof(somvr_input_shared, xr.locate_space) == 72 &&
                   offsetof(somvr_input_shared, xr.locate_spaces) == 80 && offsetof(somvr_input_shared, hap[1]) == 384,
               "somvr_input_shared inner offsets");
_Static_assert(sizeof(somvr_ptr_pub) == 104 && offsetof(somvr_ptr_pub, u) == 40 && offsetof(somvr_ptr_pub, quad_pose) == 68,
               "somvr_ptr_pub layout");
_Static_assert(offsetof(somvr_input_shared, ptr_seq) == SOMVR_INPUT_V1_SIZE && offsetof(somvr_input_shared, ptr) == 488 &&
                   offsetof(somvr_input_shared, n_ptr_frames) == 592,
               "somvr_input_shared v2 offsets (v1's part unchanged)");
_Static_assert(sizeof(somvr_input_shared) == SOMVR_INPUT_V2_SIZE, "somvr_input_shared size");
_Static_assert(sizeof(somvr_shared) == 2664 && SOMVR_SHARED_VERSION == 2, "somvr_shared stays v2");

/* active first (acquire), then the header the preload wrote before it */
static inline int somvr_input_ok(const somvr_input_shared *in) {
    return in && atomic_load_explicit(&((somvr_input_shared *)in)->active, memory_order_acquire) == 1 &&
           in->magic == SOMVR_INPUT_MAGIC && in->version >= 1 && in->size >= SOMVR_INPUT_V1_SIZE;
}

/* v2's pointer block exists in this object (a v1 object of 480 bytes has none; check somvr_input_ok first) */
static inline int somvr_input_has_ptr(const somvr_input_shared *in) {
    return in->version >= 2 && in->size >= SOMVR_INPUT_V2_SIZE;
}
/* the layer, once per frame it computes the pointer for */
static inline void somvr_ptr_publish(somvr_input_shared *in, const somvr_ptr_pub *p) {
    somvr_seq_wbegin(&in->ptr_seq);
    memcpy((void *)&in->ptr, p, sizeof *p);
    somvr_seq_wend(&in->ptr_seq);
}
/* the game thread: a consistent copy of the last pointer block; 1 = read */
static inline int somvr_ptr_read(somvr_input_shared *in, somvr_ptr_pub *out) {
    return somvr_seq_read(&in->ptr_seq, out, &in->ptr, sizeof *out);
}

/* the layer, once per sync */
static inline void somvr_input_publish(somvr_input_shared *in, const somvr_input_state *st) {
    somvr_seq_wbegin(&in->seq);
    memcpy((void *)&in->st, st, sizeof *st);
    somvr_seq_wend(&in->seq);
}
/* the game thread: a consistent copy of the last snapshot; 1 = read */
static inline int somvr_input_read(somvr_input_shared *in, somvr_input_state *out) {
    return somvr_seq_read(&in->seq, out, &in->st, sizeof *out);
}

/* the game thread (the only writer of hap[h]): a new request; its n = the previous + 1 (0 is skipped). Returns n. */
static inline uint32_t somvr_haptic_post(somvr_input_shared *in, int h, const somvr_haptic_req *req) {
    somvr_haptic_req r = *req;
    r.n = in->hap[h].req.n + 1u;
    if (!r.n) r.n = 1;
    somvr_seq_wbegin(&in->hap[h].seq);
    memcpy((void *)&in->hap[h].req, &r, sizeof r);
    somvr_seq_wend(&in->hap[h].seq);
    atomic_fetch_add_explicit(&in->n_hap_posts, 1, memory_order_relaxed);
    return r.n;
}
/* the layer: 1 = a request newer than *last_n (copied to *out; *last_n = its n). 0 = none, or a torn read (retried at
 * the next present). */
static inline int somvr_haptic_take(somvr_input_shared *in, int h, uint32_t *last_n, somvr_haptic_req *out) {
    somvr_haptic_req r;
    if (!somvr_seq_read(&in->hap[h].seq, &r, &in->hap[h].req, sizeof r) || r.n == *last_n) return 0;
    *last_n = r.n;
    *out = r;
    return 1;
}
#endif
