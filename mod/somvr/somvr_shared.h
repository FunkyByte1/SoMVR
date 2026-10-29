/* somvr_shared.h - the in-process ABI between somvr.so (the LD_PRELOAD library: it owns and exports the struct) and
 * VK_LAYER_SOMVR_openxr (the Vulkan layer: it finds it with dlsym(RTLD_DEFAULT, SOMVR_SHARED_SYMBOL)).
 * docs/re-xr-pipeline.md section 4. C11, header only, no OpenXR/Vulkan includes.
 *
 * Rules
 *   - The preload writes the header (magic, version, size, layout) once and then sets `active` (release). Readers check
 *     somvr_shared_ok() first. An inert preload leaves the whole struct zero.
 *   - xr.*: written by the layer. session/space/locate_views/space_type before xr.state = RUNNING (release). The timing
 *     block {predicted_time, predicted_period, waits} is guarded by the seqlock xr.tseq.
 *   - rec[]: the game thread writes record (frame % SOMVR_NREC) between somvr_seq_wbegin/wend; `applied` is OR-ed by
 *     the render thread outside the seqlock. The layer reads with somvr_rec_read().
 *   - tag[]: the render thread pushes one tag per IDXGISwapChain::Present (somvr_tag_push); the layer reads tags by seq.
 *   - eyes: layout B's eye images (re-xr-pipeline.md 4.6 B2). The render thread (H8/H9) owns the block: `info` under
 *     the seqlock eyes.seq, expect_w/h set before the textures are made (the layer remembers VkImages of that size).
 *     A tag's `eyes` bit e says eye e's image holds the tag's frame, `eye_gen` names the info it belongs to.
 *   - Teardown: the layer calls somvr_xr_stop() (state = DEAD, then waits for users == 0) before it destroys the space or
 *     the session; the provider brackets every xrLocateViews with somvr_xr_enter()/somvr_xr_leave().
 */
#ifndef SOMVR_SHARED_H
#define SOMVR_SHARED_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define SOMVR_SHARED_SYMBOL "somvr_shared"
#define SOMVR_SHARED_MAGIC 0x31524853524d4f53ull /* "SOMRSHR1" */
#define SOMVR_SHARED_VERSION 2u /* 2: tags carry eyes/eye_gen, the eyes block (layout B eye images) */
#define SOMVR_NREC 8
#define SOMVR_NTAG 8

enum { SOMVR_LAYOUT_MONO = 0, SOMVR_LAYOUT_A = 1, SOMVR_LAYOUT_B = 2 };
enum { SOMVR_XRS_NONE = 0, SOMVR_XRS_RUNNING = 1, SOMVR_XRS_DEAD = 2 };
enum { SOMVR_REC_EXTRAPOLATED = 1u, SOMVR_REC_REUSED = 2u, SOMVR_REC_FAKE = 4u };
/* SOMVR_FALLBACK's comfort fade (docs/re-xr-pipeline.md 6.8): flags bits 24..31 = how black the record's frame is shown,
 * 0 = not at all (every record without the fallback), 255 = black. The layer draws a black quad over the frame's other
 * layers (SOMVR_XR_FADE). Same ABI version: an older layer ignores the bits (the base swap is then seen), an older
 * preload leaves them 0. */
#define SOMVR_REC_FADE_SHIFT 24
#define SOMVR_REC_FADE_MASK 0xff000000u
/* SOMVR_BODYMOMENTS' knockdown dim (docs/re-xr-pipeline.md 20): the record's fade in dark red, not black. An older
 * layer ignores the bit (black); an older preload leaves it 0. */
#define SOMVR_REC_FADE_RED (1u << 23)
/* build 22 (SOMVR_BM_DEATH=first, docs/re-xr-pipeline.md 90): the record's fade dims the world only - the layer puts it
 * under the HUD, the VR menu and our hands (the menu backdrop's dim slot) instead of over everything: the death's grey
 * leaves the killer's name and line readable. An older layer ignores the bit (over everything); an older preload
 * leaves it 0. */
#define SOMVR_REC_FADE_WORLD (1u << 22)
/* headless tests (SOMVR_XR_DUMP_MARKED=N): the layer dumps the stereo frame after a record with this bit (the strike's
 * moments, SOMVR_HP_AWAY's proof); a player's layer has N = 0 and ignores it, an older one ignores the bit */
#define SOMVR_REC_DUMP (1u << 3)
static inline uint32_t somvr_rec_fade(uint32_t flags) { return (flags & SOMVR_REC_FADE_MASK) >> SOMVR_REC_FADE_SHIFT; }

typedef struct { float q[4]; float p[3]; } somvr_posef; /* = XrPosef: orientation x y z w, position x y z (metres) */
typedef struct { float l, r, u, d; } somvr_fovf;        /* = XrFovf: angleLeft, angleRight, angleUp, angleDown (rad) */

typedef struct {
    somvr_posef pose;      /* the XR-space pose the eye was rendered with (the layer's reference space) */
    somvr_fovf fov;        /* exact: what H3 puts into the renderer */
    somvr_fovf fov_cover;  /* symmetric covering fov: what renders when H3 did not apply */
    float rect[4];         /* viewport in the slot's image: x0 y0 x1 y1 in 0..1 */
    uint32_t slot;         /* which present of the frame carries this eye (layout A: 0; B: the eye) */
    uint32_t hash;         /* FNV-1a of the SomView pos+quat (28 bytes) handed to BeginPerspectiveViewport */
    float som_pos[3];      /* the SomView pose (cm, game world): logs only */
    float som_quat[4];
} somvr_eye_rec;

typedef struct {
    _Atomic uint64_t seq;      /* seqlock: odd while the game thread writes */
    uint64_t frame;            /* game frame id: CRendererHandle+0x38 at H4 */
    int64_t display_time;      /* XrTime the views were located for */
    uint32_t layout, nslots;
    uint32_t flags;            /* SOMVR_REC_* */
    _Atomic uint32_t applied;  /* bit e: H3 applied eye e's exact projection (render thread; outside the seqlock) */
    somvr_eye_rec eye[2];
} somvr_rec;

typedef struct {
    _Atomic uint64_t seq;      /* this tag's sequence number (1, 2, ...), stored last (release) */
    uint64_t frame;            /* record to show; 0 = no stereo frame since the previous present (mono) */
    uint32_t slot;
    int32_t tid;               /* the render thread that pushed it */
    uint32_t eyes;             /* layout B: bit e = eye image e holds this frame (H8 copied it at eye e's end) */
    uint32_t eye_gen;          /* the eyes.info generation those images belong to */
} somvr_tag;

/* layout B: the preload's two eye textures (IdxD3D11Texture2D with the 3D surface's desc). After the game's
 * CopyResource Feral leaves a destination in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (CVulkanTexture::vf_14/vf_11 at
 * 0x1de0e60/0x1de0140: FUN_01ddf580(dst, TRANSFER, TRANSFER_WRITE, 7)); nothing else touches them, so the layer finds
 * them in that layout and must leave them in it. */
#define SOMVR_EYE_IMAGE_LAYOUT 7 /* VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL */
typedef struct {
    uint32_t gen;              /* bumped when the textures are (re)made or a VkImage becomes known */
    uint32_t w, h;             /* their size = the 3D surface's (DXGI desc) */
    uint32_t dxgi_format;      /* DXGI_FORMAT of the 3D surface (logs; the layer takes the VkFormat it saw created) */
    uint64_t image[2];         /* VkImage of eye e (Feral's CVulkanTexture+0x108); 0 = not known yet */
} somvr_eyes_info;
typedef struct somvr_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active;   /* 1: the preload runs; set last (release) */
    uint32_t layout;           /* SOMVR_LAYOUT_*: what the preload renders */
    int32_t pid;
    uint32_t pad0;
    struct {
        _Atomic uint32_t state;        /* SOMVR_XRS_*: the provider calls only while RUNNING */
        _Atomic uint32_t users;        /* provider calls in flight */
        uint64_t instance, session, space; /* XrInstance, XrSession, XrSpace */
        void *locate_views;            /* PFN_xrLocateViews */
        uint32_t space_type;           /* XrReferenceSpaceType */
        uint32_t view_w, view_h;       /* recommended per-eye image size */
        uint32_t pad1;
        _Atomic uint64_t tseq;         /* seqlock over the next three */
        int64_t predicted_time, predicted_period; /* the time to locate the views for = the last xrWaitFrame's
                                                     predictedDisplayTime + the layer's pipeline lead (SOMVR_XR_PREDICT,
                                                     docs/re-xr-pipeline.md 7.1); its display period */
        uint64_t waits;                /* xrWaitFrame calls so far */
        _Atomic uint32_t recenter_seq; /* bumped on XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING */
        _Atomic int32_t snap_steps;    /* later: controller snap turn (+ = right) */
    } xr;
    _Atomic uint64_t rec_pending;      /* last frame id H4 completed; taken (exchanged with 0) by H9 */
    somvr_rec rec[SOMVR_NREC];
    _Atomic uint64_t tag_seq;          /* last tag pushed */
    somvr_tag tag[SOMVR_NTAG];
    _Atomic int32_t present_tid;       /* the render thread inside IDXGISwapChain::Present, else 0 */
    uint32_t pad2;
    _Atomic uint64_t present_tag;      /* the tag seq of that present */
    _Atomic uint64_t n_records, n_applied, n_tags, n_locates, n_locate_fail, n_stereo, n_reuse, n_mono;
    struct {
        _Atomic uint64_t seq;          /* seqlock over info (render thread writes) */
        somvr_eyes_info info;
        _Atomic uint32_t expect_w, expect_h; /* the size of the eye textures about to be / already made (0 = none) */
        _Atomic uint64_t copies, misses;     /* H8: eye images copied; eye ends without a usable texture */
        _Atomic uint64_t n_submits;          /* layer: frames submitted from the eye images */
    } eyes;
} somvr_shared;

/* ---- ABI checks (the layer and the preload must agree; change SOMVR_SHARED_VERSION with any of these) ---------- */
_Static_assert(sizeof(somvr_posef) == 28 && sizeof(somvr_fovf) == 16, "pose/fov types");
_Static_assert(sizeof(somvr_eye_rec) == 112, "somvr_eye_rec size");
_Static_assert(offsetof(somvr_rec, eye) == 40 && sizeof(somvr_rec) == 264, "somvr_rec layout");
_Static_assert(sizeof(somvr_tag) == 32 && sizeof(somvr_eyes_info) == 32, "somvr_tag / somvr_eyes_info size");
_Static_assert(offsetof(somvr_shared, xr) == 32 && offsetof(somvr_shared, rec_pending) == 128 &&
                   offsetof(somvr_shared, rec) == 136 && offsetof(somvr_shared, tag_seq) == 2248 &&
                   offsetof(somvr_shared, present_tag) == 2520 && offsetof(somvr_shared, eyes) == 2592,
               "somvr_shared offsets");
_Static_assert(sizeof(somvr_shared) == 2664, "somvr_shared size");

static inline int somvr_shared_ok(const somvr_shared *s) {
    return s && s->magic == SOMVR_SHARED_MAGIC && s->version == SOMVR_SHARED_VERSION &&
           s->size == (uint32_t)sizeof(somvr_shared) &&
           atomic_load_explicit(&((somvr_shared *)s)->active, memory_order_acquire) == 1;
}

/* ---- seqlock ---------------------------------------------------------------------------------------------------- */
static inline void somvr_seq_wbegin(_Atomic uint64_t *s) {
    uint64_t v = atomic_load_explicit(s, memory_order_relaxed);
    atomic_store_explicit(s, (v | 1u) == v ? v + 2 : v + 1, memory_order_relaxed); /* odd */
    atomic_thread_fence(memory_order_release);
}
static inline void somvr_seq_wend(_Atomic uint64_t *s) {
    uint64_t v = atomic_load_explicit(s, memory_order_relaxed);
    atomic_store_explicit(s, (v & 1u) ? v + 1 : v + 2, memory_order_release); /* even */
}
/* copy n bytes from src (guarded by *s) into dst; 1 = a consistent copy */
static inline int somvr_seq_read(_Atomic uint64_t *s, void *dst, const volatile void *src, size_t n) {
    for (int tries = 0; tries < 64; tries++) {
        uint64_t a = atomic_load_explicit(s, memory_order_acquire);
        if (a & 1u) continue;
        memcpy(dst, (const void *)src, n);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(s, memory_order_relaxed) == a) return 1;
    }
    return 0;
}

/* ---- timing (layer writes after xrWaitFrame; provider reads) ----------------------------------------------------- */
static inline void somvr_time_publish(somvr_shared *sh, int64_t t, int64_t period) {
    somvr_seq_wbegin(&sh->xr.tseq);
    sh->xr.predicted_time = t;
    sh->xr.predicted_period = period;
    sh->xr.waits++;
    somvr_seq_wend(&sh->xr.tseq);
}
typedef struct { int64_t t, period; uint64_t waits; } somvr_time;
static inline int somvr_time_read(somvr_shared *sh, somvr_time *out) {
    return somvr_seq_read(&sh->xr.tseq, out, &sh->xr.predicted_time, sizeof *out);
}
_Static_assert(offsetof(somvr_shared, xr.predicted_period) == offsetof(somvr_shared, xr.predicted_time) + 8 &&
                   offsetof(somvr_shared, xr.waits) == offsetof(somvr_shared, xr.predicted_time) + 16,
               "timing block order = somvr_time");

/* ---- the provider's in-use guard (Dekker-style with seq_cst: either the caller sees DEAD or the layer sees users) */
static inline int somvr_xr_enter(somvr_shared *sh) {
    atomic_fetch_add(&sh->xr.users, 1);
    if (atomic_load(&sh->xr.state) == SOMVR_XRS_RUNNING) return 1;
    atomic_fetch_sub(&sh->xr.users, 1);
    return 0;
}
static inline void somvr_xr_leave(somvr_shared *sh) { atomic_fetch_sub(&sh->xr.users, 1); }
/* layer: no new provider calls; wait (up to timeout_ms) until none is in flight. 1 = quiescent, safe to destroy */
static inline int somvr_xr_stop(somvr_shared *sh, uint32_t new_state, int timeout_ms) {
    atomic_store(&sh->xr.state, new_state);
    struct timespec ts = {0, 200000};
    for (int waited_us = 0; atomic_load(&sh->xr.users) != 0; waited_us += 200) {
        if (waited_us >= timeout_ms * 1000) return 0;
        nanosleep(&ts, NULL);
    }
    return 1;
}

/* ---- records -------------------------------------------------------------------------------------------------- */
static inline somvr_rec *somvr_rec_slot(somvr_shared *sh, uint64_t frame) { return &sh->rec[frame % SOMVR_NREC]; }
/* the layer: a consistent copy of record `frame`; 1 = found and complete */
static inline int somvr_rec_read(somvr_shared *sh, uint64_t frame, somvr_rec *out) {
    somvr_rec *r = somvr_rec_slot(sh, frame);
    if (!frame || !somvr_seq_read(&r->seq, out, r, sizeof *out)) return 0;
    out->applied = atomic_load_explicit(&r->applied, memory_order_acquire);
    return out->frame == frame;
}

/* ---- present tags ---------------------------------------------------------------------------------------------- */
static inline uint64_t somvr_tag_push_eyes(somvr_shared *sh, uint64_t frame, uint32_t slot, int32_t tid, uint32_t eyes,
                                           uint32_t eye_gen) {
    uint64_t seq = atomic_load_explicit(&sh->tag_seq, memory_order_relaxed) + 1;
    somvr_tag *t = &sh->tag[seq % SOMVR_NTAG];
    atomic_store_explicit(&t->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    t->frame = frame;
    t->slot = slot;
    t->tid = tid;
    t->eyes = eyes;
    t->eye_gen = eye_gen;
    atomic_store_explicit(&t->seq, seq, memory_order_release);
    atomic_store_explicit(&sh->tag_seq, seq, memory_order_release);
    atomic_fetch_add_explicit(&sh->n_tags, 1, memory_order_relaxed);
    return seq;
}
static inline uint64_t somvr_tag_push(somvr_shared *sh, uint64_t frame, uint32_t slot, int32_t tid) {
    return somvr_tag_push_eyes(sh, frame, slot, tid, 0, 0);
}
/* copy tag `seq`; 1 = it is still in the ring */
static inline int somvr_tag_get(somvr_shared *sh, uint64_t seq, somvr_tag *out) {
    if (!seq) return 0;
    somvr_tag *t = &sh->tag[seq % SOMVR_NTAG];
    if (atomic_load_explicit(&t->seq, memory_order_acquire) != seq) return 0;
    out->frame = t->frame;
    out->slot = t->slot;
    out->tid = t->tid;
    out->eyes = t->eyes;
    out->eye_gen = t->eye_gen;
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&t->seq, memory_order_relaxed) != seq) return 0;
    out->seq = seq;
    return 1;
}
/* The layer, inside vkQueuePresentKHR on thread `tid`: which tag belongs to this present? The render thread's tag when
 * it presents synchronously (present_tid == tid), else the oldest unconsumed one (an async submit thread). *last is the
 * last tag consumed. 1 = found. */
static inline int somvr_tag_for_present(somvr_shared *sh, int32_t tid, uint64_t *last, somvr_tag *out) {
    uint64_t want;
    if (atomic_load_explicit(&sh->present_tid, memory_order_acquire) == tid && tid)
        want = atomic_load_explicit(&sh->present_tag, memory_order_acquire);
    else {
        uint64_t newest = atomic_load_explicit(&sh->tag_seq, memory_order_acquire);
        if (newest <= *last) return 0;
        want = newest - *last > SOMVR_NTAG - 1 ? newest - (SOMVR_NTAG - 1) : *last + 1;
    }
    if (want <= *last || !somvr_tag_get(sh, want, out)) return 0;
    *last = want;
    return 1;
}

/* ---- layout B eye images ----------------------------------------------------------------------------------------- */
static inline void somvr_eyes_publish(somvr_shared *sh, const somvr_eyes_info *in) { /* render thread */
    somvr_seq_wbegin(&sh->eyes.seq);
    memcpy((void *)&sh->eyes.info, in, sizeof *in);
    somvr_seq_wend(&sh->eyes.seq);
}
/* the layer: a consistent copy; 1 = read (the caller still checks gen and the images) */
static inline int somvr_eyes_read(somvr_shared *sh, somvr_eyes_info *out) {
    return somvr_seq_read(&sh->eyes.seq, out, &sh->eyes.info, sizeof *out);
}
#endif
