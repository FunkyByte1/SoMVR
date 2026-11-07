/* somvr_zprobe.h - build 41 (som-b41-climb4, docs/re-xr-pipeline.md 182): is anything drawn at a landing? The game's
 * collision has tops nothing is drawn on (the 11-04 mark 222445_15: a mantle onto a flat contact 684 cm over the
 * ground, the rays' surface '' with no record - the same as the rocks the user climbs), so the rays cannot tell; the
 * eyes can. The game thread asks for a few points (the shown landing's ring), the layer reads each eye's game depth
 * round each point's pixel in that frame and answers: where the nearest drawn thing along the eye's line to the point
 * lies. Nothing drawn at or before the point (the depth well past it, or the sky) in every point seen = an undrawn
 * landing: never landed on.
 *
 * 1. The ABI: a tenth exported object `somvr_zprobe` (magic "SMZPROB1"), as the others: the preload (the logic on
 *    Windows, the host's object) fills magic, version, size, pid and sets `active` last; zero = no probe (an older
 *    preload; the layer then reads nothing). An older layer ignores the object.
 *      f[frame % SOMVR_NREC]: the game thread writes the frame's request (keyed by its frame id, as somvr_vrhands): up
 *      to SOMVR_ZP_NPT points in the layer's reference space (metres, that frame's rig) and the request's id (a new
 *      landing = a new id). The layer, drawing that frame with its depth (SOMVR_XR_HANDS_DEPTH=world), copies a
 *      SOMVR_ZP_PATCH x SOMVR_ZP_PATCH patch of each eye's depth round each point's pixel and, once its submit is done
 *      (three frames on), publishes `ans` under a seqlock: per eye and point whether it was in the view and read, the
 *      point's view distance and the nearest drawn view distance in the patch (0 = the sky everywhere).
 *
 * 2. The pure part: the projection (the layer's own: hd_eye_matrix's, OpenXR's fov, y down) and the verdict.
 *    Tested by tests/test_zprobe.c.
 */
#ifndef SOMVR_ZPROBE_H
#define SOMVR_ZPROBE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "somvr_shared.h"

#define SOMVR_ZPROBE_SYMBOL "somvr_zprobe"
#define SOMVR_ZPROBE_MAGIC 0x31424f52505a4d53ull /* "SMZPROB1" */
#define SOMVR_ZPROBE_VERSION 1u
#define SOMVR_ZP_NPT 5   /* points a request: the landing and four round it */
#define SOMVR_ZP_PATCH 7 /* the depth patch round a point's pixel (texels a side) */

typedef struct {
    _Atomic uint64_t seq; /* the frame id this request is for; 0 while written; stored last (release) */
    uint32_t n;           /* points (0 = none this frame) */
    uint32_t id;          /* the request's id (another landing = another id) */
    float p[SOMVR_ZP_NPT][3]; /* the layer's reference space, metres, this frame's rig */
    uint32_t pad;
} somvr_zprobe_req;

enum { SOMVR_ZP_NONE = 0, SOMVR_ZP_OUT = 1, SOMVR_ZP_READ = 2 }; /* a point's state in an eye */

typedef struct {
    uint32_t state; /* SOMVR_ZP_* */
    float z;        /* the point's view distance (m, along the eye's forward) */
    float near_z;   /* the nearest drawn view distance in the patch (m); 0 = nothing drawn there (the sky) */
    float far_z;    /* the farthest drawn (m; 0 = none) */
} somvr_zprobe_pt;

typedef struct {
    _Atomic uint64_t seq;  /* seqlock: odd while written */
    uint64_t frame;        /* the frame read */
    uint32_t id, n;        /* the request's id and points */
    somvr_zprobe_pt pt[2][SOMVR_ZP_NPT]; /* per eye */
} somvr_zprobe_ans;

typedef struct somvr_zprobe_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active; /* 1: the preload asks; set last (release) */
    int32_t pid;
    somvr_zprobe_req f[SOMVR_NREC];
    somvr_zprobe_ans ans;    /* the layer's newest answer */
    _Atomic uint64_t n_req;  /* preload: requests written */
    _Atomic uint64_t n_read; /* layer: requests read (copies recorded) */
    _Atomic uint64_t n_ans;  /* layer: answers published */
} somvr_zprobe_shared;

_Static_assert(sizeof(somvr_zprobe_req) == 80, "somvr_zprobe_req size");
_Static_assert(sizeof(somvr_zprobe_ans) == 24 + 2 * SOMVR_ZP_NPT * 16, "somvr_zprobe_ans size");
_Static_assert(offsetof(somvr_zprobe_shared, f) == 24 && offsetof(somvr_zprobe_shared, ans) == 24 + 80 * SOMVR_NREC,
               "somvr_zprobe_shared offsets");
_Static_assert(sizeof(somvr_zprobe_shared) == 24 + 80 * SOMVR_NREC + 184 + 24, "somvr_zprobe_shared size");

static inline int somvr_zprobe_ok(const somvr_zprobe_shared *z) {
    return z && z->magic == SOMVR_ZPROBE_MAGIC && z->version == SOMVR_ZPROBE_VERSION &&
           z->size == (uint32_t)sizeof(somvr_zprobe_shared) &&
           atomic_load_explicit(&((somvr_zprobe_shared *)z)->active, memory_order_acquire) == 1;
}
/* the preload, once: the header, active last */
static inline void somvr_zprobe_init(somvr_zprobe_shared *z, int32_t pid) {
    z->magic = SOMVR_ZPROBE_MAGIC, z->version = SOMVR_ZPROBE_VERSION, z->size = (uint32_t)sizeof *z, z->pid = pid;
    atomic_store_explicit(&z->active, 1, memory_order_release);
}
/* the game thread: frame `frame`'s request (n points of p) */
static inline void somvr_zprobe_put(somvr_zprobe_shared *z, uint64_t frame, uint32_t id, uint32_t n, const float (*p)[3]) {
    somvr_zprobe_req *e = &z->f[frame % SOMVR_NREC];
    if (n > SOMVR_ZP_NPT) n = SOMVR_ZP_NPT;
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->n = n, e->id = id;
    for (uint32_t i = 0; i < n; i++) e->p[i][0] = p[i][0], e->p[i][1] = p[i][1], e->p[i][2] = p[i][2];
    atomic_store_explicit(&e->seq, frame, memory_order_release);
    atomic_fetch_add_explicit(&z->n_req, 1, memory_order_relaxed);
}
/* the layer: frame `frame`'s request; 1 = found with points (not overwritten, not being written) */
static inline int somvr_zprobe_get(somvr_zprobe_shared *z, uint64_t frame, somvr_zprobe_req *out) {
    if (!frame) return 0;
    somvr_zprobe_req *e = &z->f[frame % SOMVR_NREC];
    if (atomic_load_explicit(&e->seq, memory_order_acquire) != frame) return 0;
    out->n = e->n > SOMVR_ZP_NPT ? SOMVR_ZP_NPT : e->n, out->id = e->id;
    memcpy(out->p, e->p, sizeof out->p);
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&e->seq, memory_order_relaxed) != frame) return 0;
    atomic_store_explicit(&out->seq, frame, memory_order_relaxed);
    return out->n > 0;
}
/* the layer: an answer (one writer) */
static inline void somvr_zprobe_answer(somvr_zprobe_shared *z, const somvr_zprobe_ans *a) {
    uint64_t s = atomic_load_explicit(&z->ans.seq, memory_order_relaxed);
    atomic_store_explicit(&z->ans.seq, s + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    z->ans.frame = a->frame, z->ans.id = a->id, z->ans.n = a->n;
    memcpy(z->ans.pt, a->pt, sizeof z->ans.pt);
    atomic_store_explicit(&z->ans.seq, s + 2, memory_order_release);
    atomic_fetch_add_explicit(&z->n_ans, 1, memory_order_relaxed);
}
/* the game thread: the newest answer; 1 = read whole (0: none yet, or torn - try the next frame) */
static inline int somvr_zprobe_read(somvr_zprobe_shared *z, somvr_zprobe_ans *out) {
    uint64_t s0 = atomic_load_explicit(&z->ans.seq, memory_order_acquire);
    if (!s0 || (s0 & 1)) return 0;
    out->frame = z->ans.frame, out->id = z->ans.id, out->n = z->ans.n;
    memcpy(out->pt, z->ans.pt, sizeof out->pt);
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&z->ans.seq, memory_order_relaxed) == s0;
}

/* ==== 2. the pure part ============================================================================================ */
/* a point (reference space) in an eye (its pose: q x,y,z,w, p; its fov: left, right, up, down angles - radians) of a
 * w x h image: 1 = in front and inside; px, py its pixel (y down), z its view distance (m) - hd_eye_matrix's mapping */
static inline int somvr_zp_project(const float q[4], const float p[3], const float fov[4], uint32_t w, uint32_t h,
                                   const float pt[3], float *px, float *py, float *z) {
    float x = q[0], y = q[1], zq = q[2], ww = q[3];
    float R[3][3] = {{1 - 2 * (y * y + zq * zq), 2 * (x * y - zq * ww), 2 * (x * zq + y * ww)},
                     {2 * (x * y + zq * ww), 1 - 2 * (x * x + zq * zq), 2 * (y * zq - x * ww)},
                     {2 * (x * zq - y * ww), 2 * (y * zq + x * ww), 1 - 2 * (x * x + y * y)}};
    float d[3] = {pt[0] - p[0], pt[1] - p[1], pt[2] - p[2]}, v[3];
    for (int r = 0; r < 3; r++) v[r] = R[0][r] * d[0] + R[1][r] * d[1] + R[2][r] * d[2]; /* R^T d */
    if (!(v[2] < -0.01f) || !w || !h) return 0;
    float a = v[0] / -v[2], b = v[1] / -v[2];
    float tl = tanf(fov[0]), tr = tanf(fov[1]), tu = tanf(fov[2]), td = tanf(fov[3]);
    if (!(tr > tl) || !(tu > td)) return 0;
    float u = (a - tl) / (tr - tl), vv = (tu - b) / (tu - td);
    *z = -v[2], *px = u * (float)w, *py = vv * (float)h;
    return u >= 0 && u < 1 && vv >= 0 && vv < 1;
}

enum { SOMVR_ZP_UNKNOWN = 0, SOMVR_ZP_DRAWN = 1, SOMVR_ZP_UNDRAWN = 2 }; /* a verdict */
/* the answer's verdict: a point read in an eye is drawn there when the nearest drawn in its patch lies at most
 * margin_m past it (anything drawn in front of it or at it); DRAWN = some point drawn in some eye; UNDRAWN = at least
 * min_read points read (in either eye) and none drawn in any; else UNKNOWN (out of the view, not read). *read, *drawn:
 * the points read / drawn; *gap: the smallest (nearest drawn - the point) over the points read (m; 1e9 = the sky) */
static inline int somvr_zp_verdict(const somvr_zprobe_ans *a, float margin_m, int min_read, int *read, int *drawn,
                                   float *gap) {
    int nr = 0, nd = 0;
    float g = 1e9f;
    for (uint32_t i = 0; i < a->n && i < SOMVR_ZP_NPT; i++) {
        int r = 0, dr = 0;
        for (int e = 0; e < 2; e++) {
            const somvr_zprobe_pt *t = &a->pt[e][i];
            if (t->state != SOMVR_ZP_READ) continue;
            r = 1;
            float gi = t->near_z > 0 ? t->near_z - t->z : 1e9f;
            if (gi < g) g = gi;
            if (t->near_z > 0 && t->near_z <= t->z + margin_m) dr = 1;
        }
        nr += r, nd += dr;
    }
    if (read) *read = nr;
    if (drawn) *drawn = nd;
    if (gap) *gap = g;
    return nd > 0 ? SOMVR_ZP_DRAWN : nr >= min_read && nr > 0 ? SOMVR_ZP_UNDRAWN : SOMVR_ZP_UNKNOWN;
}

#endif
