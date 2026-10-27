/* somvr_objective.h - the story's way-finding in VR: the HUD's objective marker as our own marker in the world
 * (SOMVR_OBJECTIVE, docs/re-xr-pipeline.md 44; design-hla.md HG-8's "markers in stereo", G19). Two parts, C11, no
 * OpenXR/Vulkan:
 *
 * 1. The ABI: a ninth exported object next to `somvr_shared`, `somvr_input`, `somvr_hud`, `somvr_comfort`,
 *    `somvr_vrmenu`, `somvr_threat`, `somvr_session` and `somvr_vrhands` (the smallest compatible addition, as the
 *    threat's: a layer that does not know it never looks for it). The preload (SOMVR_OBJECTIVE=on|both, a stereo
 *    layout) fills magic, version, size, pid, the marker's sizes and sets `active` (release); otherwise the object
 *    stays zero and a layer that knows it draws nothing.
 *      f[frame % SOMVR_NREC]: the game thread (H4, after the frame's body) writes one entry per stereo frame, keyed by
 *      the frame id of the record (seq 0 while written, the frame id last, release). The layer reads the entry of the
 *      record it submits, so the marker sits on the frame it was made for:
 *        n = 1: the objective the game shows (its nav marker's world position, mapped with that frame's own rig into
 *               the layer's reference space, metres) and its distance from Talion (the game's metres: the text).
 *
 * 2. The pure functions (somvr_objective.c; no globals): the record-name patterns, the distance text, the placement
 *    (in view: a small diamond world-locked over the objective's direction with its distance under it, faint near the
 *    view's centre; out of view: an arrow at the view's edge, head-locked, pointing the way) and the texture. Tested by
 *    tests/test_objective.c. */
#ifndef SOMVR_OBJECTIVE_H
#define SOMVR_OBJECTIVE_H

#include "somvr_shared.h"
#include "somvr_font.h"

/* ==== 1. the ABI ================================================================================================= */
#define SOMVR_OBJECTIVE_SYMBOL "somvr_objective"
#define SOMVR_OBJECTIVE_MAGIC 0x314a424f524d4f53ull /* "SOMROBJ1" */
#define SOMVR_OBJECTIVE_VERSION 2u /* 2: build 17, the frame's top[] (the drop's line) */

enum { SOMVR_OBF_HIDDEN = 1u, /* the game's own marker is hidden (SOMVR_OBJECTIVE=on) */
       SOMVR_OBF_SHOWN = 2u,  /* the game placed it this frame (on screen or as its edge arrow) */
       SOMVR_OBF_LAND = 4u,   /* build 16 (SOMVR_CLIMB_PULLOVER): not the objective but the climb's landing: pos = the
                               * floor point, dist_m = the ring's radius (metres, reference space): a ring flat on it */
       SOMVR_OBF_LINE = 8u,   /* build 17 (SOMVR_CLIMB_DESCENT_MARK, with LAND): and a faint line from top (reference
                               * space) straight down to the ring: the way down from an edge to where he will stand */
       SOMVR_OBF_STRIKE = 16u, /* build 17 (SOMVR_STRIKE_GAZE): not the objective but the Shadow Strike's mark on the
                                * orc looked at: pos = its chest, dist_m = the ring's diameter (metres, reference space):
                                * the ring round it, the button and "Shadow Strike" beside it, facing the head */
       SOMVR_OBF_STRIKE_NO = 32u, /* ... under 2 arrows: the ring grey, "No arrows", no button */
       SOMVR_OBF_STRIKE_GO = 64u }; /* ... A pressed: the ring filled (the strike on its way) */

typedef struct {
    _Atomic uint64_t seq; /* the frame id this entry is for; 0 while written; stored last (release) */
    uint32_t n;           /* 0 = no objective shown, 1 = the one below */
    uint32_t flags;       /* SOMVR_OBF_* */
    float pos[3];         /* its place in the layer's reference space (metres), this frame's rig */
    float dist_m;         /* Talion -> it in the game's world (metres; the text) */
    uint32_t id;          /* the marker's number this session (another marker = another id; logs) */
    float top[3];         /* SOMVR_OBF_LINE: the line's top (reference space, metres; its bottom is pos) */
} somvr_objective_frame;

typedef struct somvr_objective_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active; /* 1: the preload publishes; set last (release) */
    int32_t pid;
    float size_deg;          /* SOMVR_OBJECTIVE_SIZE_DEG: the marker's square (deg) */
    float dist_m;            /* SOMVR_OBJECTIVE_DIST_M: the farthest its quad is placed (metres) */
    float faint_deg;         /* SOMVR_OBJECTIVE_FAINT_DEG: within this of the view's centre the faint picture */
    float edge_deg;          /* SOMVR_OBJECTIVE_EDGE_DEG: the edge arrow's square (deg) */
    _Atomic uint64_t last;   /* the newest frame id written */
    somvr_objective_frame f[SOMVR_NREC];
    _Atomic uint64_t n_frames; /* preload: entries written */
    _Atomic uint64_t n_marker; /* layer: frames submitted with the marker in the world */
    _Atomic uint64_t n_edge;   /* layer: frames submitted with the edge arrow */
} somvr_objective_shared;

_Static_assert(sizeof(somvr_objective_frame) == 48, "somvr_objective_frame size");
_Static_assert(offsetof(somvr_objective_shared, size_deg) == 24 && offsetof(somvr_objective_shared, last) == 40 &&
                   offsetof(somvr_objective_shared, f) == 48 && offsetof(somvr_objective_shared, n_frames) == 432,
               "somvr_objective_shared offsets");
_Static_assert(sizeof(somvr_objective_shared) == 456, "somvr_objective_shared size");

static inline int somvr_objective_ok(const somvr_objective_shared *o) {
    return o && o->magic == SOMVR_OBJECTIVE_MAGIC && o->version == SOMVR_OBJECTIVE_VERSION &&
           o->size == (uint32_t)sizeof(somvr_objective_shared) &&
           atomic_load_explicit(&((somvr_objective_shared *)o)->active, memory_order_acquire) == 1;
}

/* the game thread: frame `frame`'s entry (in->seq is ignored) */
static inline void somvr_objective_put(somvr_objective_shared *o, uint64_t frame, const somvr_objective_frame *in) {
    somvr_objective_frame *e = &o->f[frame % SOMVR_NREC];
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->n = in->n, e->flags = in->flags;
    memcpy(e->pos, in->pos, sizeof e->pos);
    e->dist_m = in->dist_m, e->id = in->id;
    memcpy(e->top, in->top, sizeof e->top);
    atomic_store_explicit(&e->seq, frame, memory_order_release);
    atomic_store_explicit(&o->last, frame, memory_order_release);
    atomic_fetch_add_explicit(&o->n_frames, 1, memory_order_relaxed);
}
/* the layer: frame `frame`'s entry; 1 = found (not overwritten, not being written) */
static inline int somvr_objective_get(somvr_objective_shared *o, uint64_t frame, somvr_objective_frame *out) {
    if (!frame) return 0;
    somvr_objective_frame *e = &o->f[frame % SOMVR_NREC];
    if (atomic_load_explicit(&e->seq, memory_order_acquire) != frame) return 0;
    out->n = e->n, out->flags = e->flags;
    memcpy(out->pos, e->pos, sizeof out->pos);
    out->dist_m = e->dist_m, out->id = e->id;
    memcpy(out->top, e->top, sizeof out->top);
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&e->seq, memory_order_relaxed) != frame) return 0;
    atomic_store_explicit(&out->seq, frame, memory_order_relaxed);
    if (out->n > 1) out->n = 1;
    return 1;
}

/* ==== 2. the pure part (somvr_objective.c) ======================================================================= */
/* a record name against a pattern list "A,B*,C" (',' between, a trailing '*' = any rest; empty = none) */
int somvr_ob_match(const char *pats, const char *name);
/* the distance as the marker writes it: "7 m", "234 m", "1.2 km" (out: 16 bytes) */
void somvr_ob_text(float dist_m, char out[16]);

typedef struct {
    float size_deg;    /* the marker's square (deg; its diamond about 0.4 of it, the text line a quarter) */
    float dist_m;      /* the quad sits at the objective's own distance from the head up to this (stereo: a near
                        * objective is seen where it is; a far one as if this far) */
    float near_m;      /* ... and never nearer than this; nearer than near_hide_m: no marker (Talion is there) */
    float near_hide_m;
    float faint_deg;   /* within this of the view's centre: the faint picture (it never covers what is looked at) */
    float edge_deg;    /* the edge arrow's square (deg) */
    float in_k;        /* in view = its tangents within in_k of the view's edges ... */
    float out_k;       /* ... and it stays in view until out_k (the hysteresis) */
    float edge_k;      /* the edge arrow on the ellipse at edge_k of the view's tangents */
    float edge_dist_m; /* ... in the VIEW space this far ahead */
} somvr_ob_cfg;
void somvr_ob_cfg_default(somvr_ob_cfg *c);

enum { SOMVR_OBK_NONE = 0, SOMVR_OBK_WORLD = 1, SOMVR_OBK_EDGE = 2 };
enum { SOMVR_OB_CELL_FULL = 0, SOMVR_OB_CELL_FAINT = 1, SOMVR_OB_CELL_ARROW = 2, SOMVR_OB_CELL_RING = 3, SOMVR_OB_CELL_LINE = 4,
       SOMVR_OB_CELL_STRIKE = 5, SOMVR_OB_CELL_STRIKE_NO = 6, SOMVR_OB_CELL_STRIKE_GO = 7, SOMVR_OB_CELLS = 8 };
#define SOMVR_OB_SQUARE_CELLS 5 /* cells 0..4 are squares; the strike's (5..7) SOMVR_OB_WIDE strides wide each */
#define SOMVR_OB_WIDE 3
typedef struct {
    int kind;        /* SOMVR_OBK_*: WORLD = pose in the reference space, EDGE = pose in a VIEW space */
    int cell;        /* SOMVR_OB_CELL_* */
    float pose[7];   /* orientation x y z w, position x y z */
    float size[2];   /* width, height (m) */
    float off_deg;   /* the objective's angle from the view's centre (logs) */
    float yaw_deg;   /* ... its bearing in the head's frame, + = right (logs) */
    float roll_deg;  /* EDGE: the arrow's direction in the view, 0 = right, 90 = up */
} somvr_ob_place;
/* where the marker goes this frame: head = the frame's head (the eyes' middle, reference space), hq its orientation
 * (x y z w), fov the views' union (rad, as XrFovf: left < 0, right, up, down < 0), pos the objective (reference
 * space). *in_view carries the hysteresis from frame to frame (0 at the start). */
somvr_ob_place somvr_ob_locate(const somvr_ob_cfg *c, const float head[3], const float hq[4], const float fov[4],
                               const float pos[3], int *in_view);

/* the climb's landing (SOMVR_OBF_LAND): a ring of radius r_m flat on the floor point pos (reference space, +y up), 2 cm
 * over it, the ring's cell */
somvr_ob_place somvr_ob_floor(const float pos[3], float r_m);
/* build 17 (SOMVR_OBF_LINE): the line from top down to bot (reference space), w_m wide, turned about its own axis to
 * face head (the eyes' middle); the line's cell (its top row at top). Under 5 cm long or not finite: none */
somvr_ob_place somvr_ob_line(const float top[3], const float bot[3], const float head[3], float w_m);
/* build 17, the Shadow Strike's mark (SOMVR_OBF_STRIKE): a quad facing the head at pos (the orc's chest, reference
 * space) with the ring's centre on pos, the ring ring_m across (its outer edge), cell one of the strike cells */
somvr_ob_place somvr_ob_strike(const float head[3], const float pos[3], float ring_m, int cell);
/* the ring's diameter for an orc dist_m away: at least min_m, at least deg of the view (the label stays readable) */
float somvr_ob_strike_ring(float dist_m, float min_m, float deg);

/* The texture: SOMVR_OB_CELLS cells of SOMVR_OB_CELL px square inside a 1 px guard (a cell's rect is its interior):
 * cell 0 the diamond (gold, a dark rim) with the distance under it (white, a dark rim), cell 1 the same faint (alpha
 * SOMVR_OB_FAINT), cell 2 the edge arrow (pointing +x = right), cell 3 the landing ring (gold, a dark rim, a faint
 * disc inside, a dot), cell 4 the drop's line (build 17: a faint gold band down the cell's middle with dark edges,
 * fading in over its top 15 %); build 17: cells 5..7 the Shadow Strike's mark, each SOMVR_OB_WIDE strides wide (the
 * ring in its left square, the label right of it): 5 ready (the wraith-blue ring, the A button, "SHADOW STRIKE"), 6 no
 * arrows (the ring grey, "NO ARROWS"), 7 pressed (the ring filled). Premultiplied RGBA8 (bgra: B and R swapped). The
 * diamond's centre is SOMVR_OB_DIAMOND_V of the cell from its top (the quad is shifted so that it, not the cell's
 * centre, sits on the objective's direction). */
#define SOMVR_OB_CELL 128
#define SOMVR_OB_STRIDE (SOMVR_OB_CELL + 2)
#define SOMVR_OB_IMG_W ((SOMVR_OB_SQUARE_CELLS + (SOMVR_OB_CELLS - SOMVR_OB_SQUARE_CELLS) * SOMVR_OB_WIDE) * SOMVR_OB_STRIDE)
#define SOMVR_OB_STRIKE_RING_PX 108 /* the strike ring's outer diameter in its cell (px; the cell is SOMVR_OB_CELL high) */
#define SOMVR_OB_IMG_H SOMVR_OB_STRIDE
#define SOMVR_OB_DIAMOND_V 0.3125f
#define SOMVR_OB_FAINT 0.40f
void somvr_ob_rect(int cell, int32_t r[4]); /* x, y, w, h */
void somvr_ob_image(uint8_t *px, const somvr_font *f, const char *text, int bgra);

#endif
