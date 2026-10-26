/* somvr_threat.h - HL:A-style attack telegraphs and threat warnings (SOMVR_THREAT=1, docs/re-xr-pipeline.md 18;
 * design-hla.md HG-3's warning part and HG-5's announcement, design-vr.md's ranked plan items 8-9). Two parts, C11,
 * no OpenXR/Vulkan:
 *
 * 1. The ABI: a sixth exported object next to `somvr_shared`, `somvr_input`, `somvr_hud`, `somvr_comfort` and
 *    `somvr_vrmenu` (the smallest compatible addition, as comfort's: a layer that does not know it never looks for it).
 *    The preload (SOMVR_THREAT=1 with SOMVR_COMBAT, a stereo layout and its key-time sites) fills magic, version, size,
 *    pid, the two strengths and sets `active` (release); otherwise the object stays zero and a layer that knows it
 *    draws nothing.
 *      f[frame % SOMVR_NREC]: the game thread (H4, after the frame's body) writes one entry per stereo frame, keyed by
 *      the frame id of the record (CRendererHandle+0x38): seq 0 while written, the frame id last (release). The layer
 *      reads the entry of the record it submits (somvr_threat_get), so the cues sit on the frame they were made for:
 *        g[0..nglow): world-locked warm glows (in view: at an attacker's weapon hand; an inbound projectile in view),
 *                     their centres in the layer's reference space (metres, mapped with that frame's own rig);
 *        edge[0/1]:   the head-locked edge glow on the left / right side of the view (an attack or a projectile out of
 *                     view, an engaged orc close behind), 0..1.
 *
 * 2. The pure functions (somvr_threat.c; no globals): the direction of a threat in the head's frame, in view or not,
 *    its side; an attack's key times (Combat+0xc8.., re-combat-vr.md 3.1) and its lead to the impact; the glow's and
 *    the edge's levels over the attack (fade in at the start, the peak in the parry window, out after the impact); the
 *    cue selection with its haptic and sound schedule per attacker; the parry-window tick; a projectile's closest
 *    approach to the head and the chest; the world -> XR mapping (the rig's inverse); and the layer's side: the glow's
 *    billboard, the edge quad in the VIEW space, the texture (two rows of intensity cells) and its rects. Tested by
 *    tests/test_threat.c. */
#ifndef SOMVR_THREAT_H
#define SOMVR_THREAT_H

#include "somvr_shared.h"

/* ==== 1. the ABI ================================================================================================= */
#define SOMVR_THREAT_SYMBOL "somvr_threat"
#define SOMVR_THREAT_MAGIC 0x31524854524d4f53ull /* "SOMRTHR1" */
#define SOMVR_THREAT_VERSION 1u
#define SOMVR_TH_GLOWS 3 /* world glows per frame (the most urgent ones) */

enum { SOMVR_THK_NONE = 0, SOMVR_THK_MELEE = 1, SOMVR_THK_RANGED = 2, SOMVR_THK_PROJ = 3 }; /* what a glow is on */
enum { SOMVR_THF_WINDOW = 1u, /* the nearest attacker's parry window is open this frame */
       SOMVR_THF_PROJ = 2u,   /* a projectile is inbound this frame */
       SOMVR_THF_NEAR = 4u,   /* an engaged orc is close behind (the edge's low level) */
       SOMVR_THF_HURT = 8u }; /* build 10: Talion was hit: the hurt flash (hurt[]) */

typedef struct {
    float pos[3];  /* its centre in the layer's reference space (metres), this frame's rig */
    float size_m;  /* the quad's side */
    float level;   /* 0..1, the strength applied */
    uint32_t kind; /* SOMVR_THK_* (logs) */
} somvr_th_glow;

typedef struct {
    _Atomic uint64_t seq; /* the frame id this entry is for; 0 while written; stored last (release) */
    uint32_t nglow;       /* 0..SOMVR_TH_GLOWS */
    uint32_t flags;       /* SOMVR_THF_* */
    somvr_th_glow g[SOMVR_TH_GLOWS];
    float edge[2];        /* the left / right edge glow, 0..1 (the strength applied; 0 = none) */
    uint32_t threats;     /* attacks at Talion tracked this frame (logs) */
    uint8_t hurt[2];      /* build 10 (SOMVR_HURT): the red hurt flash on the left / right edge, 0..255 (0 = none); it
                           * takes that side's edge band while it is the stronger (the band's slot, the red row) */
    uint16_t pad;
} somvr_threat_frame;

typedef struct somvr_threat_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active;  /* 1: the preload publishes; set last (release) */
    int32_t pid;
    float glow, edge;         /* SOMVR_THREAT_GLOW / _EDGE (logs; the entries carry them applied) */
    _Atomic uint64_t last;    /* the newest frame id written */
    somvr_threat_frame f[SOMVR_NREC];
    _Atomic uint64_t n_frames; /* preload: entries written */
    _Atomic uint64_t n_glow;   /* layer: frames submitted with at least one glow */
    _Atomic uint64_t n_edge;   /* layer: frames submitted with an edge glow */
} somvr_threat_shared;

_Static_assert(sizeof(somvr_th_glow) == 24 && sizeof(somvr_threat_frame) == 104, "somvr_threat_frame size");
_Static_assert(offsetof(somvr_threat_shared, glow) == 24 && offsetof(somvr_threat_shared, last) == 32 &&
                   offsetof(somvr_threat_shared, f) == 40 && offsetof(somvr_threat_shared, n_frames) == 872,
               "somvr_threat_shared offsets");
_Static_assert(sizeof(somvr_threat_shared) == 896, "somvr_threat_shared size");
_Static_assert(sizeof(somvr_shared) == 2664 && SOMVR_SHARED_VERSION == 2, "somvr_shared stays v2");

static inline int somvr_threat_ok(const somvr_threat_shared *t) {
    return t && t->magic == SOMVR_THREAT_MAGIC && t->version == SOMVR_THREAT_VERSION &&
           t->size == (uint32_t)sizeof(somvr_threat_shared) &&
           atomic_load_explicit(&((somvr_threat_shared *)t)->active, memory_order_acquire) == 1;
}

/* the game thread: frame `frame`'s entry (in->seq is ignored) */
static inline void somvr_threat_put(somvr_threat_shared *t, uint64_t frame, const somvr_threat_frame *in) {
    somvr_threat_frame *e = &t->f[frame % SOMVR_NREC];
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->nglow = in->nglow;
    e->flags = in->flags;
    memcpy(e->g, in->g, sizeof e->g);
    e->edge[0] = in->edge[0], e->edge[1] = in->edge[1];
    e->threats = in->threats;
    e->hurt[0] = in->hurt[0], e->hurt[1] = in->hurt[1];
    e->pad = 0;
    atomic_store_explicit(&e->seq, frame, memory_order_release);
    atomic_store_explicit(&t->last, frame, memory_order_release);
    atomic_fetch_add_explicit(&t->n_frames, 1, memory_order_relaxed);
}
/* the layer: frame `frame`'s entry; 1 = found (not overwritten, not being written) */
static inline int somvr_threat_get(somvr_threat_shared *t, uint64_t frame, somvr_threat_frame *out) {
    if (!frame) return 0;
    somvr_threat_frame *e = &t->f[frame % SOMVR_NREC];
    if (atomic_load_explicit(&e->seq, memory_order_acquire) != frame) return 0;
    out->nglow = e->nglow;
    out->flags = e->flags;
    memcpy(out->g, e->g, sizeof out->g);
    out->edge[0] = e->edge[0], out->edge[1] = e->edge[1];
    out->threats = e->threats;
    out->hurt[0] = e->hurt[0], out->hurt[1] = e->hurt[1];
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&e->seq, memory_order_relaxed) != frame) return 0;
    atomic_store_explicit(&out->seq, frame, memory_order_relaxed);
    if (out->nglow > SOMVR_TH_GLOWS) out->nglow = SOMVR_TH_GLOWS;
    return 1;
}

/* ==== 2. the pure part (somvr_threat.c) ============================================================================ */

typedef struct {
    float view_deg;   /* the cone around the head's view direction counted as "in view" (HG-5: 110 deg) */
    int view_full;    /* SOMVR_THREAT_VIEW=full: the cone = the full angle off the view direction (round 1's); 0 =
                       * bearing (the default): |yaw| in the head's frame, the pitch ignored. The orc's points are its
                       * root (+0xd2c) and its weapon hand, 50-77 deg below the eyes when it stands at Talion's feet in
                       * plain view (re-xr-pipeline.md 42.10/42.11); SOMVR_FRONT judges with the same test */
    float glow;       /* SOMVR_THREAT_GLOW: the in-view glow's strength 0..1 */
    float edge;       /* SOMVR_THREAT_EDGE: the edge glow's strength 0..1 */
    float fade_in_s;  /* the glow fades in over this at the attack's start */
    float fade_out_s; /* ... and out over this after the impact */
    float base;       /* the glow's level before the parry window (and without one: its start) */
    float edge_base;  /* the edge's level at the announcement; it rises to 1 at the impact */
    float edge_ramp_s;/* ... over the last this many seconds */
    float urgent_s;   /* a second, stronger pulse when the attack is still out of view this close to the impact */
    float near_m;     /* SOMVR_THREAT_NEAR_M: an engaged orc out of view within this gets the edge's low level (0 off) */
    float near_level; /* ... at 1 m or closer (falling to 0 at near_m) */
    float proj_r_cm;  /* a projectile passing the head or the chest within this is inbound */
    float proj_t_s;   /* ... within this time */
    float glow_deg;   /* the glow's angular size (its side at the distance) */
    float glow_min_m, glow_max_m; /* ... clamped */
    float sound_min_s;/* the warning sound only with at least this much lead */
    int keyless;      /* SOMVR_THREAT_KEYLESS=1: an attack with no key at all (no impact, window, BCOL or SHOOT) gets
                       * cues too (round 1's: the base glow, or out of view the edge, a pulse and the sound); 0 (the
                       * default) = none: those are the orcs' moves between blows (run 092140: 200 of 254 starts in a
                       * five-orc fight, none of them a blow; 114 of 148 out-of-view announcements) */
} somvr_th_cfg;
void somvr_th_cfg_default(somvr_th_cfg *c);

/* ---- where a threat is, relative to the head (game convention: +x right, +y up, +z forward, cm) ---------------- */
/* R(q)^-1 (p - head): a world point in the head's frame (q = the head's orientation, x y z w, Hamilton) */
void somvr_th_local(const float head[3], const float q[4], const float p[3], float out[3]);
typedef struct {
    float yaw_deg;   /* + = right of the view direction, -180..180 */
    float pitch_deg; /* + = above */
    float off_deg;   /* the angle from the view direction, 0..180 */
    float dist_cm;
} somvr_th_dir;
somvr_th_dir somvr_th_direction(const float local[3]);
/* the angle the in-view test compares with view_deg / 2: |yaw| (bearing, the default; off_deg when the point is more
 * than SOMVR_TH_BEARING_PITCH above or below: its yaw means nothing there) or off_deg (view_full); NaN = unknown. The
 * one in-view test of THREAT's cues and SOMVR_FRONT's veto. */
#define SOMVR_TH_BEARING_PITCH 85.0f
float somvr_th_view_off(const somvr_th_cfg *c, somvr_th_dir d);
int somvr_th_in_view(const somvr_th_cfg *c, somvr_th_dir d); /* view_off <= view_deg / 2 (NaN: not in view) */
enum { SOMVR_TH_LEFT = 1, SOMVR_TH_RIGHT = 2, SOMVR_TH_BOTH = 3 };
#define SOMVR_TH_BEHIND_DEG 8.0f /* within this of straight behind: both sides */
int somvr_th_side(somvr_th_dir d);
const char *somvr_th_side_name(int side); /* "L", "R", "LR", "-" */

/* ---- an attack's key times ---------------------------------------------------------------------------------------- */
/* Combat+0xc8.. (re-combat-vr.md 3.1): ushort ms from the animation start, 0xffff = no such key:
 * [0] EAL [1] BCOL [2] APPLYDAMAGE [3] STARTFACING [4] STOPFACING [5] BEFACING [6] BPW [7] EPW [8] SHOOT [9] STIMULUS */
enum { SOMVR_TH_KT_BCOL = 1, SOMVR_TH_KT_IMPACT = 2, SOMVR_TH_KT_BPW = 6, SOMVR_TH_KT_EPW = 7, SOMVR_TH_KT_SHOOT = 8,
       SOMVR_TH_KT_N = 10 };
typedef struct {
    float impact;   /* s from the start (the table's APPLYDAMAGE; P8: the hit lands one frame later), < 0 = none */
    float bpw, epw; /* the parry window, s; < 0 = none (also when EPW <= BPW) */
    float bcol;     /* the first BCOL, s; < 0 = none */
    int ranged;     /* the attack has a SHOOT key (an archer's release, a thrown spear) */
} somvr_th_keys;
somvr_th_keys somvr_th_keys_of(const uint16_t ws[SOMVR_TH_KT_N]);
float somvr_th_lead(const somvr_th_keys *k, float t); /* the time to the impact at t s after the start (NaN: none) */
int somvr_th_keyless(const somvr_th_keys *k);          /* no impact, no window, no BCOL, no SHOOT: not a blow */
int somvr_th_window(const somvr_th_keys *k, float t);  /* 1 inside [bpw, epw] */
/* the glow over the attack, 0..1 before the strength: fades in from 0 at the start over fade_in_s at `base`, rises to 1
 * through the parry window (1 at EPW), holds to the impact, fades out over fade_out_s; without a window it rises from
 * base at the start to 1 at the impact; without an impact it stays at base. */
float somvr_th_glow_level(const somvr_th_cfg *c, const somvr_th_keys *k, float t);
/* the edge's level, 0..1 before the strength: edge_base from the start, rising to 1 over the last edge_ramp_s before
 * the impact, out over fade_out_s after it; no impact: edge_base */
float somvr_th_edge_level(const somvr_th_cfg *c, const somvr_th_keys *k, float t);
int somvr_th_expired(const somvr_th_cfg *c, const somvr_th_keys *k, float t); /* past the impact's fade (or 4 s) */

/* ---- the cue per attacker ------------------------------------------------------------------------------------------ */
enum { SOMVR_THC_NONE = 0, SOMVR_THC_GLOW = 1, SOMVR_THC_EDGE = 2, SOMVR_THC_NEAR = 3, SOMVR_THC_PROJ_GLOW = 4,
       SOMVR_THC_PROJ_EDGE = 5 };
const char *somvr_th_cue_name(int cue); /* "none", "glow", "edge", "near", "proj-glow", "proj-edge" */
enum { SOMVR_THH_NONE = 0, SOMVR_THH_ANNOUNCE = 1, SOMVR_THH_URGENT = 2, SOMVR_THH_TICK = 3 };
const char *somvr_th_haptic_name(int h); /* "-", "announce", "urgent", "tick" */
typedef struct {
    int announced;   /* the out-of-view announcement went (its pulse, its sound) */
    int urgent;      /* the second pulse went */
    int ticked;      /* the parry helper's tick went */
    int cue;         /* the last cue (logs on a change) */
} somvr_th_state;
typedef struct {
    int cue;         /* SOMVR_THC_* */
    int side;        /* SOMVR_TH_*: the edge cues' side and the pulse's (a glow: 0; a projectile: its side in view too) */
    float level;     /* 0..1, the cue's strength applied */
    int haptic;      /* SOMVR_THH_*: a pulse to give this frame on `side`'s controller(s) */
    int sound;       /* 1: post the warning sound at the attacker this frame */
    int late;        /* the announcement came less than 1 s before the impact (the attack's own timing) */
    float lead;      /* s to the impact (NaN: none) */
} somvr_th_pick;
/* an attack at Talion, t s after its start, at direction d (the orc's body: in view by somvr_th_in_view): in view =
 * the glow; out of view = the edge, its announcement (one pulse on that side, the sound with at least sound_min_s of
 * lead) the first frame out of view before the impact, the urgent pulse once inside urgent_s; after the impact the cue
 * fades without pulses. A keyless attack (somvr_th_keyless): no cue unless cfg keyless. */
somvr_th_pick somvr_th_select(const somvr_th_cfg *c, somvr_th_dir d, const somvr_th_keys *k, float t, somvr_th_state *s);
/* an engaged orc (its target is Talion) without an attack: the edge's low level out of view within near_m */
somvr_th_pick somvr_th_near(const somvr_th_cfg *c, somvr_th_dir d);
/* the parry helper: 1 = tick now (the window has opened at t, once per attack) */
int somvr_th_parry_tick(const somvr_th_keys *k, float t, somvr_th_state *s);

/* ---- projectiles (re-combat-items.md 1: straight along R(q)+Z at +0x210 cm/s, no homing in P8) ------------------ */
typedef struct {
    int inbound;     /* passes the head or the chest within proj_r_cm within proj_t_s */
    float tca_s;     /* the time to its closest approach (to the nearer of the two) */
    float miss_cm;   /* the distance there */
    int at_head;     /* the head is the nearer pass (else the chest) */
} somvr_th_proj;
somvr_th_proj somvr_th_proj_eval(const somvr_th_cfg *c, const float pos[3], const float dir[3], float speed_cms,
                                 const float head[3], const float chest[3]);
somvr_th_pick somvr_th_proj_pick(const somvr_th_cfg *c, somvr_th_dir d, const somvr_th_proj *p, int *announced);

/* ---- the world -> the layer's reference space ------------------------------------------------------------------ */
/* the rig (somvr_eyes.h): world = b + s Y(a) (m(xr) x 100 - p0), a = psi - yaw0, s the player size (0 = 1): so
 * xr = m(p0 + Y(-a) (world - b) / s) / 100 */
typedef struct { float b[3]; float a; float p0[3]; float s; } somvr_th_map;
void somvr_th_to_xr(const somvr_th_map *m, const float world_cm[3], float out_m[3]);
float somvr_th_glow_size(const somvr_th_cfg *c, float dist_cm); /* the quad's side (m) for the glow at that distance */

/* ---- the layer: quads and the texture ------------------------------------------------------------------------------ */
/* the glow's billboard: its +Z toward the eye (XR, right-handed), level (no roll) */
void somvr_th_face(const float pos[3], const float eye[3], float q[4]);
/* the edge band in the VIEW space at dist_m: the frame's two eye fovs (rad: left right up down, as XrFovf); side
 * SOMVR_TH_LEFT or _RIGHT. The band covers the view's outer SOMVR_TH_EDGE_IN of that half (tangent units) and reaches
 * SOMVR_TH_EDGE_OUT past it, the full height + 10 %; the right one is the left one turned 180 deg about the view axis
 * (the texture's bright column outward). pose = orientation x y z w, position x y z; size = width, height (m). */
#define SOMVR_TH_EDGE_IN 0.30f
#define SOMVR_TH_EDGE_OUT 0.10f
void somvr_th_edge_quad(const float fov[2][4], int side, float dist_m, float pose[7], float size[2]);

/* The texture (made once): three rows of SOMVR_TH_LEVELS cells, each SOMVR_TH_CELL px square inside a 1 px guard (a
 * cell's rect is its interior, so filtering never reaches a neighbour): row 0 the radial glow, row 1 the edge band
 * (bright at u = 0, fading to 0 at u = 1, soft ends at the top and bottom), row 2 (build 10) the same band in the hurt
 * red. Cell k (1..LEVELS) holds the profile x k / LEVELS in the warm colour, premultiplied, its alpha SOMVR_TH_ALPHA x
 * the colour's weight (mostly additive: the scene behind keeps most of its light); the red row's alpha is
 * SOMVR_TH_HURT_ALPHA (it tints the edge red). Cell 0 is never drawn (level 0 = no quad). */
#define SOMVR_TH_LEVELS 16
#define SOMVR_TH_CELL 32
#define SOMVR_TH_STRIDE (SOMVR_TH_CELL + 2)
#define SOMVR_TH_IMG_W (SOMVR_TH_LEVELS * SOMVR_TH_STRIDE)
#define SOMVR_TH_IMG_H (3 * SOMVR_TH_STRIDE)
#define SOMVR_TH_ALPHA 0.35f
#define SOMVR_TH_R 1.00f /* the warm colour (linear weights; the swapchain is sRGB when the runtime has one) */
#define SOMVR_TH_G 0.55f
#define SOMVR_TH_B 0.15f
#define SOMVR_TH_HURT_R 1.00f /* the hurt red */
#define SOMVR_TH_HURT_G 0.04f
#define SOMVR_TH_HURT_B 0.02f
#define SOMVR_TH_HURT_ALPHA 0.75f
enum { SOMVR_TH_ROW_GLOW = 0, SOMVR_TH_ROW_EDGE = 1, SOMVR_TH_ROW_HURT = 2 };
/* the layer, per side: the hurt flash (0..255) against the edge band's level (0..1): 1 = draw the red row with *cell,
 * 0 = the band (its own cell), as before */
int somvr_th_hurt_side(uint8_t hurt, float edge, int *cell);
float somvr_th_glow_profile(float r);          /* r = distance from the centre / half-size: 1 at 0, 0 from 1 on */
float somvr_th_edge_profile(float u, float v); /* u, v in 0..1 across the cell */
void somvr_th_image(uint8_t *px, int bgra);    /* SOMVR_TH_IMG_W x _H x 4 bytes, rows top to bottom */
int somvr_th_cell(float level);                /* 0..LEVELS: round(level x LEVELS), 0 below half a step / NaN */
void somvr_th_rect(int row, int cell, int32_t rc[4]); /* offset x y, extent w h of cell `cell` (1..LEVELS) */

#endif
