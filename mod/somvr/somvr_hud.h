/* somvr_hud.h - the HUD ABI (H11: docs/re-hud.md 1.4, docs/re-xr-pipeline.md 8): a third exported object next to
 * `somvr_shared` and `somvr_input`. C11, header only, no OpenXR/Vulkan includes.
 *
 * Why not a field of somvr_shared: v2 has no room. The present tag (32 bytes) and the eye info (32 bytes) are full,
 * the record is full, the only free bytes are three scattered 4-byte pads (pad0, xr.pad1, pad2) - not enough for a
 * 64-bit VkImage plus its size and generation under a seqlock, and not per present. Every v2 layer checks
 * `size == 2664` and `version == 2` exactly, so a grown struct or a version 3 would turn it mono. This object is the
 * smallest compatible extension: a v2 layer never looks for it and keeps its stereo; a layer that knows it, with a
 * preload that has none (or an inactive one), adds no HUD quad.
 *
 * Rules
 *   - The preload (SOMVR_HUD=quad, layout B, its hooks in) fills magic, version, size, pid and sets `active` (release);
 *     otherwise the object stays zero. Readers check somvr_hud_ok(): magic, version == 1, size, active.
 *   - info: the render thread (the present composite hook and H9) writes it under the seqlock `seq`; expect_w/h are set
 *     before the HUD texture is made, so the layer can remember VkImages of that size (as for the eye images).
 *   - tag[s % SOMVR_NTAG]: H9 writes one for every present, before it pushes somvr_shared's tag s (seq 0 while being
 *     written, s last, release). gen != 0: the HUD image of info generation gen holds the HUD of this present's stereo
 *     frame (copied from the engine's UI surface at the present composite, after eye 1's HUD pass). The layer reads it
 *     after the core tag s.
 *   - The HUD image is the preload's IdxD3D11Texture2D; after the game's CopyResource Feral leaves it in
 *     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (SOMVR_EYE_IMAGE_LAYOUT, as the eye images); the layer must leave it in it.
 *   - Its pixels are the UI surface's: RGBA with the Scaleform alpha; straight or premultiplied is still open (RT U2),
 *     so the layer has a switch for the quad's blend flags.
 */
#ifndef SOMVR_HUD_H
#define SOMVR_HUD_H

#include "somvr_shared.h"

#define SOMVR_HUD_SYMBOL "somvr_hud"
#define SOMVR_HUD_MAGIC 0x31445548524d4f53ull /* "SOMRHUD1" */
#define SOMVR_HUD_VERSION 1u

enum { SOMVR_HUDK_NONE = 0, SOMVR_HUDK_WORLD = 1, SOMVR_HUDK_VIDEO = 2 }; /* which HUD pass drew it (re-hud.md 1.3) */
/* build 6 (re-xr-pipeline.md 28): flags above the kind's low byte (a reader compares kind & 0xff). MENU = a game menu
 * was open over the world at that HUD pass (the pause menu, the map, the skills: the interface's menu state machine not
 * idle, or the game paused): the layer shows that HUD image on the menu board (SOMVR_XR_MENU=front) */
#define SOMVR_HUDF_MENU 0x100u

typedef struct {
    uint32_t gen;          /* bumped when the texture is (re)made or its VkImage becomes known */
    uint32_t w, h;         /* the UI surface's size (DXGI desc) */
    uint32_t dxgi_format;  /* DXGI_FORMAT of the UI surface (logs; the layer takes the VkFormat it saw created) */
    uint64_t image;        /* VkImage of the HUD texture (Feral's CVulkanTexture+0x108); 0 = not known yet */
} somvr_hud_info;

typedef struct {
    _Atomic uint64_t seq;  /* the somvr_shared tag seq this entry belongs to; stored last (release) */
    uint32_t gen;          /* the info generation the HUD image holds for that present; 0 = no HUD */
    uint32_t kind;         /* SOMVR_HUDK_* of that copy */
} somvr_hud_tag;

typedef struct somvr_hud_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active;          /* 1: the preload copies the HUD; set last (release) */
    int32_t pid;
    _Atomic uint64_t seq;             /* seqlock over info (render thread writes) */
    somvr_hud_info info;
    _Atomic uint32_t expect_w, expect_h; /* the size of the HUD texture about to be / already made (0 = none) */
    somvr_hud_tag tag[SOMVR_NTAG];    /* by somvr_shared tag seq */
    _Atomic uint64_t copies;          /* preload: UI surface copies into the HUD texture */
    _Atomic uint64_t misses;          /* preload: stereo presents without a HUD copy (shared surface, no pass, off) */
    _Atomic uint64_t n_quads;         /* layer: frames submitted with the HUD quad */
} somvr_hud_shared;

_Static_assert(sizeof(somvr_hud_info) == 24 && sizeof(somvr_hud_tag) == 16, "somvr_hud_info / somvr_hud_tag size");
_Static_assert(offsetof(somvr_hud_shared, seq) == 24 && offsetof(somvr_hud_shared, info) == 32 &&
                   offsetof(somvr_hud_shared, expect_w) == 56 && offsetof(somvr_hud_shared, tag) == 64 &&
                   offsetof(somvr_hud_shared, copies) == 192,
               "somvr_hud_shared offsets");
_Static_assert(sizeof(somvr_hud_shared) == 216, "somvr_hud_shared size");
_Static_assert(sizeof(somvr_shared) == 2664 && SOMVR_SHARED_VERSION == 2, "somvr_shared stays v2");

static inline int somvr_hud_ok(const somvr_hud_shared *h) {
    return h && h->magic == SOMVR_HUD_MAGIC && h->version == SOMVR_HUD_VERSION &&
           h->size == (uint32_t)sizeof(somvr_hud_shared) &&
           atomic_load_explicit(&((somvr_hud_shared *)h)->active, memory_order_acquire) == 1;
}

/* render thread: the HUD texture's info (gen, size, VkImage) */
static inline void somvr_hud_publish(somvr_hud_shared *h, const somvr_hud_info *in) {
    somvr_seq_wbegin(&h->seq);
    memcpy((void *)&h->info, in, sizeof *in);
    somvr_seq_wend(&h->seq);
}
/* the layer: a consistent copy; 1 = read (the caller still checks gen and the image) */
static inline int somvr_hud_read(somvr_hud_shared *h, somvr_hud_info *out) {
    return somvr_seq_read(&h->seq, out, &h->info, sizeof *out);
}

/* H9, before it pushes somvr_shared's tag `seq`: what the HUD image holds for that present */
static inline void somvr_hud_tag_put(somvr_hud_shared *h, uint64_t seq, uint32_t gen, uint32_t kind) {
    somvr_hud_tag *t = &h->tag[seq % SOMVR_NTAG];
    atomic_store_explicit(&t->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    t->gen = gen;
    t->kind = kind;
    atomic_store_explicit(&t->seq, seq, memory_order_release);
}
/* the layer, for the core tag `seq` it took: 1 = this present's entry (out->gen 0 = no HUD), 0 = none / overwritten */
static inline int somvr_hud_tag_get(somvr_hud_shared *h, uint64_t seq, somvr_hud_tag *out) {
    if (!seq) return 0;
    somvr_hud_tag *t = &h->tag[seq % SOMVR_NTAG];
    if (atomic_load_explicit(&t->seq, memory_order_acquire) != seq) return 0;
    out->gen = t->gen;
    out->kind = t->kind;
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&t->seq, memory_order_relaxed) != seq) return 0;
    atomic_store_explicit(&out->seq, seq, memory_order_relaxed);
    return 1;
}
#endif
