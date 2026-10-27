/* somvr_vrhands.h - our own VR hands (build 10, docs/re-xr-pipeline.md 33; Half-Life: Alyx: the hand you see is the
 * hand you hold up). The layer draws a hand mesh of its own (the WebXR Input Profiles' MIT generic hand,
 * somvr_handmesh.h) on each controller as a projection layer of its own over the wrist panel and the watch; the game's
 * forearms and hands are hidden while it does. Two parts, C11, no OpenXR/Vulkan:
 *
 * 1. The ABI: a seventh exported object next to `somvr_shared` & co. (the smallest compatible addition, as comfort's: the
 *    preload with SOMVR_VRHANDS=1 fills magic, version, size, pid and sets `active` last; otherwise it stays zero and a
 *    layer that knows it draws no hands).
 *      f[frame % SOMVR_NREC]: the game thread writes one entry per stereo frame (eye 0, after the rig), keyed by the
 *      record's frame id: the two grips its arms were solved to (XR space; SOMVR_TWOHAND's left on the hilt), each
 *      finger's curl, what each hand holds and whether the game hid its own hands in that frame's eye passes. The
 *      layer draws the entry of the record it submits, so our hands sit exactly where the game put its sword.
 *      layer_state: the layer -> the preload. The game hides its own hands only while the layer says it draws ours
 *      (1); off or failed (2) = the game's hands come back (a frame of both or neither at the switch, never longer).
 *      v2 (build 11, SOMVR_XR_HANDS_DEPTH=world, docs/re-xr-pipeline.md 33.8): the game's scene depth of each eye, so
 *      the world hides our hands where they go into it. The render thread copies the eye's depth-stencil texture (the
 *      eye target's rt+0x200, D32S8, reused for both eyes) into one of two textures of ours at that eye's end (H8, right
 *      after the eye image) and publishes their VkImages (dinfo) and the frame each holds (dframe); each entry carries
 *      depth_k: an eye's view distance in XR metres = depth_k / the depth value (reversed Z with an infinite far plane:
 *      depth = near / z; depth_k = near cm / (100 cm x the player size)).
 *      v3 (build 13, SOMVR_HANDS_SURFACE=on, docs/re-xr-pipeline.md 38): the grips may be stopped at the world's
 *      surfaces; each entry then also carries where the controllers really are (ghost) and how strongly the layer
 *      shows a faint wraith-blue hand there (ghost_a, 0 = none), drawn over the world (never hidden by its depth).
 *
 * 2. The pure part (somvr_vrhands.c): the hand model from the mesh (its joints' hierarchy, the fist's tube measured
 *    and put on the OpenXR grip's -Z axis through the grip's origin: the sword's hilt), the fingers posed from five
 *    curls, the skin into XR space for either hand (the left = the right mirrored in the grip frame), the wrist's fade,
 *    and the places that hang on the hand: the watch on the back of the wrist, the wrist panel's distance off the palm.
 *    Tested by tests/test_vrhands.c; the pictures by vklayer/tests/hands_lvp.c (lavapipe).
 */
#ifndef SOMVR_VRHANDS_H
#define SOMVR_VRHANDS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "somvr_shared.h"

/* ==== 1. the ABI ================================================================================================= */
#define SOMVR_VRHANDS_SYMBOL "somvr_vrhands"
#define SOMVR_VRHANDS_MAGIC 0x3153444e41485256ull /* "VRHANDS1" */
#define SOMVR_VRHANDS_VERSION 4u

enum {
    SOMVR_VHF_L = 1u,       /* the left grip is valid this frame */
    SOMVR_VHF_R = 2u,       /* the right grip */
    SOMVR_VHF_HIDE = 4u,    /* the game hid its own hands in this frame's eye passes (first person): draw ours */
    SOMVR_VHF_HOLD_L = 8u,  /* the left hand holds something (the dagger, the hilt with both hands): a fist */
    SOMVR_VHF_HOLD_R = 16u, /* the right hand holds the sword: a fist */
    SOMVR_VHF_TWO = 32u     /* SOMVR_TWOHAND: the left grip is the hilt's, not the controller's */
};
enum { SOMVR_VHL_UNKNOWN = 0, SOMVR_VHL_DRAWING = 1, SOMVR_VHL_OFF = 2 }; /* layer_state */

typedef struct {
    _Atomic uint64_t seq;  /* the frame id this entry is for; 0 while written; stored last (release) */
    uint32_t flags;        /* SOMVR_VHF_* */
    float depth_k;         /* v2: the eyes' view distance (XR m) = depth_k / their depth value; 0 = no game depth */
    int64_t time;          /* the XrTime the grips were located for (the frame's views' time) */
    somvr_posef grip[2];   /* XR space, metres: left, right */
    float curl[2][5];      /* 0 open .. 1 closed: thumb, index, middle, ring, pinky (somvr_fingers.h, eased) */
    /* v3 (SOMVR_HANDS_SURFACE=on, docs/re-xr-pipeline.md 38): where the controllers are when the grips above were
     * stopped at a surface; ghost_a[h] > 0: the layer draws a faint hand there (the real one), that opaque */
    somvr_posef ghost[2];  /* XR space, metres */
    float ghost_a[2];      /* 0 = none */
    /* v4 (build 16, SOMVR_BOW_QUIVER, docs/re-xr-pipeline.md 51): an arrow the layer draws (arrow_len > 0): the pose's
     * origin at its nock end, its -Z along the shaft to the head (XR space, metres); arrow_glow 0..1 its wraith light */
    somvr_posef arrow;
    float arrow_len, arrow_glow;
} somvr_vrhands_frame;

typedef struct somvr_vrhands_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active;      /* 1: the preload publishes; set last (release) */
    int32_t pid;
    _Atomic uint64_t last;        /* the newest frame id written */
    somvr_vrhands_frame f[SOMVR_NREC];
    _Atomic uint32_t layer_state; /* SOMVR_VHL_*: written by the layer */
    uint32_t pad;
    _Atomic uint64_t n_frames;    /* preload: entries written */
    _Atomic uint64_t n_hidden;    /* preload: frames its hands were hidden for ours */
    _Atomic uint64_t n_drawn;     /* layer: frames submitted with our hands */
    _Atomic uint64_t last_drawn;  /* layer: the last frame id drawn */
    /* v2: the eyes' depth (SOMVR_XR_HANDS_DEPTH=world; render thread). After the game's CopyResource Feral leaves the
     * textures in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (SOMVR_EYE_IMAGE_LAYOUT, as the eye images); the layer returns
     * them to it after sampling. */
    _Atomic uint64_t dseq;        /* seqlock over dinfo */
    somvr_eyes_info dinfo;        /* the two depth textures: gen, size, DXGI format (19: R32G8X24_TYPELESS), VkImages */
    _Atomic uint64_t dframe[2];   /* the frame id eye e's depth texture holds (stored after its copy, release) */
    _Atomic uint64_t n_dcopies;   /* preload: depth copies made */
    _Atomic uint64_t n_dused;     /* layer: frames whose hands were tested against the world */
} somvr_vrhands_shared;

_Static_assert(sizeof(somvr_vrhands_frame) == 224, "somvr_vrhands_frame size");
_Static_assert(offsetof(somvr_vrhands_shared, last) == 24 && offsetof(somvr_vrhands_shared, f) == 32 &&
                   offsetof(somvr_vrhands_shared, layer_state) == 32 + 224 * SOMVR_NREC &&
                   offsetof(somvr_vrhands_shared, dseq) == 32 + 224 * SOMVR_NREC + 40,
               "somvr_vrhands_shared offsets");
_Static_assert(sizeof(somvr_vrhands_shared) == 32 + 224 * SOMVR_NREC + 40 + 72, "somvr_vrhands_shared size");

static inline int somvr_vrhands_ok(const somvr_vrhands_shared *v) {
    return v && v->magic == SOMVR_VRHANDS_MAGIC && v->version == SOMVR_VRHANDS_VERSION &&
           v->size == (uint32_t)sizeof(somvr_vrhands_shared) &&
           atomic_load_explicit(&((somvr_vrhands_shared *)v)->active, memory_order_acquire) == 1;
}
/* the game thread: frame `frame`'s entry (in->seq is ignored) */
static inline void somvr_vrhands_put(somvr_vrhands_shared *v, uint64_t frame, const somvr_vrhands_frame *in) {
    somvr_vrhands_frame *e = &v->f[frame % SOMVR_NREC];
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->flags = in->flags;
    e->depth_k = in->depth_k;
    e->time = in->time;
    e->grip[0] = in->grip[0], e->grip[1] = in->grip[1];
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 5; k++) e->curl[h][k] = in->curl[h][k];
    e->ghost[0] = in->ghost[0], e->ghost[1] = in->ghost[1], e->ghost_a[0] = in->ghost_a[0], e->ghost_a[1] = in->ghost_a[1];
    e->arrow = in->arrow, e->arrow_len = in->arrow_len, e->arrow_glow = in->arrow_glow;
    atomic_store_explicit(&e->seq, frame, memory_order_release);
    atomic_store_explicit(&v->last, frame, memory_order_release);
    atomic_fetch_add_explicit(&v->n_frames, 1, memory_order_relaxed);
}
/* the layer: frame `frame`'s entry; 1 = found (not overwritten, not being written) */
static inline int somvr_vrhands_get(somvr_vrhands_shared *v, uint64_t frame, somvr_vrhands_frame *out) {
    if (!frame) return 0;
    somvr_vrhands_frame *e = &v->f[frame % SOMVR_NREC];
    if (atomic_load_explicit(&e->seq, memory_order_acquire) != frame) return 0;
    out->flags = e->flags;
    out->depth_k = e->depth_k;
    out->time = e->time;
    out->grip[0] = e->grip[0], out->grip[1] = e->grip[1];
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 5; k++) out->curl[h][k] = e->curl[h][k];
    out->ghost[0] = e->ghost[0], out->ghost[1] = e->ghost[1];
    out->ghost_a[0] = e->ghost_a[0], out->ghost_a[1] = e->ghost_a[1];
    out->arrow = e->arrow, out->arrow_len = e->arrow_len, out->arrow_glow = e->arrow_glow;
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&e->seq, memory_order_relaxed) != frame) return 0;
    atomic_store_explicit(&out->seq, frame, memory_order_relaxed);
    return 1;
}

/* v2, the game thread: frame `frame`'s depth_k, known only once both eyes' views are set up (after the entry's put);
 * written under the entry's seq like a put (nothing if the slot holds another frame) */
static inline void somvr_vrhands_set_depth_k(somvr_vrhands_shared *v, uint64_t frame, float k) {
    somvr_vrhands_frame *e = &v->f[frame % SOMVR_NREC];
    if (!frame || atomic_load_explicit(&e->seq, memory_order_relaxed) != frame) return;
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->depth_k = k;
    atomic_store_explicit(&e->seq, frame, memory_order_release);
}
/* v2: the depth textures (render thread) / a consistent copy for the layer (1 = read; it still checks gen, images) */
static inline void somvr_vrhands_depth_publish(somvr_vrhands_shared *v, const somvr_eyes_info *in) {
    somvr_seq_wbegin(&v->dseq);
    memcpy((void *)&v->dinfo, in, sizeof *in);
    somvr_seq_wend(&v->dseq);
}
static inline int somvr_vrhands_depth_read(somvr_vrhands_shared *v, somvr_eyes_info *out) {
    return somvr_seq_read(&v->dseq, out, &v->dinfo, sizeof *out);
}
/* the depth switch both sides read (SOMVR_XR_HANDS_DEPTH=world|off): 1 world, 0 off, -1 not understood (= off) */
#define SOMVR_VH_DEPTH_DEFAULT 0 /* off until a headset look proves it (build 11 sets world) */
static inline int somvr_vh_depth_word(const char *v) {
    if (!v || !*v) return SOMVR_VH_DEPTH_DEFAULT;
    if (!strcmp(v, "world") || !strcmp(v, "on") || !strcmp(v, "1")) return 1;
    if (!strcmp(v, "off") || !strcmp(v, "0")) return 0;
    return -1;
}
/* the world's fade on a hand fragment at view distance z_hand (m) in front of the world's z_world (m): 1 drawn .. 0
 * hidden; soft_m the edge's width, centred bias_m behind the surface (the shader's rule, for the tests) */
static inline float somvr_vh_depth_fade(float z_hand, float z_world, float soft_m, float bias_m) {
    float t = (z_world + bias_m + 0.5f * soft_m - z_hand) / (soft_m > 1e-4f ? soft_m : 1e-4f);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* ==== 2. the pure part (somvr_vrhands.c) =========================================================================== */
#define SOMVR_VH_NJ 25   /* the mesh's joints (somvr_handmesh.h: the WebXR order) */
#define SOMVR_VH_NF 5    /* thumb, index, middle, ring, pinky (= somvr_fingers.h FG_*) */
enum { SOMVR_VHJ_WRIST = 0, SOMVR_VHJ_THUMB = 1, SOMVR_VHJ_INDEX = 5, SOMVR_VHJ_MIDDLE = 10, SOMVR_VHJ_RING = 15,
       SOMVR_VHJ_PINKY = 20 }; /* each finger's first joint (the metacarpal); its tip is first + 4 (the thumb's + 3) */

typedef struct {
    float open[SOMVR_VH_NF][3];  /* deg: each finger's three bending joints in the relaxed open hand */
    float fist[SOMVR_VH_NF][3];  /* ... and closed round a sword's grip */
    float thumb_opp_deg;         /* the thumb's metacarpal turned across the palm at a full curl */
    float fade_from_m;           /* the wrist's fade: opaque up to this far from the wrist joint toward the fingers */
    float fade_to_m;             /* ... clear this far past it toward the elbow */
    float cal[6];                /* SOMVR_XR_HANDS_CAL: cm x y z, deg rx ry rz in the right grip's frame (mirrored) */
    float size;                  /* build 17, SOMVR_XR_HANDS_SIZE: the hand's size about the fist's tube (1 = the generic
                                    hand: 17.8 cm wrist to middle tip, the fist's tube 3.1 cm across) */
} somvr_vh_cfg;
void somvr_vh_cfg_default(somvr_vh_cfg *c);
/* "x,y,z,rx,ry,rz" (cm, deg; |cm| <= 10, |deg| <= 90) into c->cal; 0 = not understood (c unchanged) */
int somvr_vh_parse_cal(const char *v, somvr_vh_cfg *c);
/* "S" (0.8 .. 1.5) into c->size; 0 = not understood (c unchanged). NULL / "" = 1 (kept) */
int somvr_vh_parse_size(const char *v, somvr_vh_cfg *c);

typedef struct {
    int ok;
    float local[SOMVR_VH_NJ][7];  /* each joint's rest transform in its parent's (q x y z w, t): the hierarchy */
    float ibind[SOMVR_VH_NJ][12]; /* the inverse rest transforms, 3x4 row-major (model -> joint) */
    float gq[4], gp[3];           /* the right hand's model space -> its grip frame (the fist's tube on -Z) + cal */
    float gk;                     /* ... scaled by this about the tube's centre (cfg size; p_g = gk gq p + gp) */
    float tube_r_m;               /* the fist's tube: the finger joints' radius round its axis (m) */
    float tube_tilt_deg;          /* its axis against the knuckles' line (logs) */
    float wrist_g[3];             /* the wrist joint at rest in the right grip's frame */
    float arm_g[3];               /* ... the forearm's direction there (toward the elbow, unit) */
    float back_g[3];              /* ... the back of the wrist's normal (unit) */
    float wrist_r_m;              /* the back of the wrist's height above the wrist joint (m) */
    float palm_reach_m;           /* the farthest any pose reaches out of the palm, from the grip's origin (m) */
    float tip_g[3];               /* the index tip of the pointing hand (the others closed) in the right grip's frame */
    float tipdir_g[3];            /* ... the index's direction there (its knuckle to its tip, unit) */
    float fade[1400];             /* per vertex: the wrist's fade (1 opaque .. 0 clear) */
    uint16_t idx_mirror[7000];    /* the triangles reversed (the left hand) */
    char why[320];
} somvr_vh_model;
/* the model from the mesh and c; 0 = ok (m->why: a summary), -1 = unusable (m->why says why) */
int somvr_vh_init(somvr_vh_model *m, const somvr_vh_cfg *c);
/* the skinning matrices (3x4 row-major, rest model space -> posed model space) of five curls (0..1) */
void somvr_vh_pose(const somvr_vh_model *m, const somvr_vh_cfg *c, const float curl[SOMVR_VH_NF],
                   float skin[SOMVR_VH_NJ][12]);
/* the posed joint j's position in the right grip's frame (the joint's rest point through skin, then the grip map) */
void somvr_vh_joint_g(const somvr_vh_model *m, const float skin[SOMVR_VH_NJ][12], int j, float out[3]);

typedef struct { float p[3], n[3], a; } somvr_vh_vert; /* XR space: position, normal, the fade's alpha (28 bytes) */
_Static_assert(sizeof(somvr_vh_vert) == 28, "somvr_vh_vert size");
int somvr_vh_nverts(void);
int somvr_vh_nindices(void);
/* the triangles (ccw front faces seen from outside for the right hand; mirror = 1: the left hand's, reversed) */
const uint16_t *somvr_vh_indices(const somvr_vh_model *m, int mirror);
/* hand `side` (SOMVR_HAND_L 0 / _R 1) skinned with skin and placed on its grip (XR space): somvr_vh_nverts() vertices */
void somvr_vh_skin(const somvr_vh_model *m, const float skin[SOMVR_VH_NJ][12], int side, const somvr_posef *grip,
                   somvr_vh_vert *out);
/* build 16 (vrhands v4, docs/re-xr-pipeline.md 51): the arrow the layer draws in the string hand or on the string - a
 * hexagonal shaft 9 mm thick with its nock end capped, a four-sided head 5 cm long, three vanes 10 cm long near the nock.
 * Its triangles come in both windings (the layer culls back faces; the shader lights the side it sees) */
#define SOMVR_VH_ARROW_NV 59
#define SOMVR_VH_ARROW_NI 180
const uint16_t *somvr_vh_arrow_indices(void);
/* the arrow's vertices in XR space: `pose` at its nock end, its -Z along the shaft to the head; len_m long (clamped to
 * 0.2 .. 1.5 m) */
void somvr_vh_arrow(const somvr_posef *pose, float len_m, somvr_vh_vert *out);
/* the arrow's parts near the eyes fade (drawn at full draw the nock and the vanes are at the cheek, a few cm from the
 * eye): each vertex's alpha = smoothstep over near_m .. far_m of its distance to the nearer eye */
void somvr_vh_arrow_fade(somvr_vh_vert *v, int n, const float eye0[3], const float eye1[3], float near_m, float far_m);
/* a point given in the right grip's frame, on hand `side`'s grip (the left mirrored: x negated) -> XR space */
void somvr_vh_grip_point(const somvr_posef *grip, int side, const float pg[3], float out[3]);
/* touch: the drawn pointing hand's index tip on hand `side`'s grip (XR space) */
void somvr_vh_tip(const somvr_vh_model *m, const somvr_posef *grip, int side, float out[3]);
/* the grip that puts that tip at `point` (XR space), the index pointing along `dir` (unit: into a quad, its -Z) with the
 * thumb toward `up` (the quad's +Y); 0 = no frame (dir along up) */
int somvr_vh_touch_grip(const somvr_vh_model *m, const float point[3], const float dir[3], const float up[3], int side,
                        somvr_posef *grip);

/* ---- what hangs on the hand ------------------------------------------------------------------------------------- */
/* The watch: flat on the back of the wrist (back_m toward the elbow from the wrist joint, gap_m above the skin), its
 * face up the back of the wrist's normal, reading the right way up with the wrist raised to look (its top away from
 * the body: the side opposite the thumb's, its right toward the fingers). In XR space (an XrPosef), hand `side`. */
typedef struct { float back_m, gap_m; } somvr_vh_watch_cfg;
/* 0.015 m behind the wrist joint (run 191837: at 0.035 it floated apart from the faded hand), 0.008 m above the skin */
void somvr_vh_watch_cfg_default(somvr_vh_watch_cfg *c);
int somvr_vh_watch_pose(const somvr_vh_model *m, const somvr_vh_watch_cfg *c, const somvr_posef *grip, int side,
                        somvr_posef *out);
/* The watch shown: seen within the facing angle for hold_on frames in a row, hidden after hold_off frames past the
 * hysteresis (no one-frame flashes at the edge); 1 = show this frame */
typedef struct { float face_on, face_off; int hold_on, hold_off; } somvr_vh_face_cfg;
void somvr_vh_face_cfg_default(somvr_vh_face_cfg *c); /* cos 0.15 / 0.08 (81 / 85 deg: edge-on), 2 frames in, 3 out */
typedef struct { int shown, run; } somvr_vh_face;
int somvr_vh_face_step(somvr_vh_face *s, const somvr_vh_face_cfg *c, const somvr_posef *face, const float eye[3]);

#endif
