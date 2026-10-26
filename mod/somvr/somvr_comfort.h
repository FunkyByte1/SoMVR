/* somvr_comfort.h - comfort (docs/re-xr-pipeline.md 11, design-vr.md 2 "Comfort shaping of locomotion"): the vignette
 * while the view moves without the head, the snap turn's blink, seated height. Two parts, C11, no OpenXR/Vulkan:
 *
 * 1. The ABI: a fourth exported object next to `somvr_shared`, `somvr_input` and `somvr_hud` (the smallest compatible
 *    addition, as the HUD's: somvr_shared v2 has no room, and a v2 layer never looks for this one). The preload
 *    (SOMVR_VIGNETTE > 0 or SOMVR_SNAP_BLINK_MS > 0) fills magic, version, size, pid, the two settings and sets
 *    `active` (release); otherwise the object stays zero and a layer that knows it draws nothing.
 *      f[frame % SOMVR_NREC]: the game thread (eye 0 of a stereo frame, after the rig) writes one entry per stereo frame,
 *      keyed by the frame id of the record (CRendererHandle+0x38 at H4): seq 0 while written, the frame id last
 *      (release). The layer reads the entry of the record it submits (somvr_comfort_get), so the blink's black lands on
 *      exactly the frames the game turned on.
 *    Speeds are the rig's artificial motion only: how the world point of the tracked head moves when the head itself
 *    stands still (stick locomotion, smooth turns, a camera base or yaw that the game moves, the body follow's
 *    leftovers). The player's own head motion is never in them; a snap step, a recentre, a teleport-sized jump or a
 *    gap of more than 0.25 s is not motion (flags; the speeds of the last counted frame are kept).
 *
 * 2. The pure functions (somvr_comfort.c; no globals): the speed measurement, the snap blink's gate and the seated
 *    height (game side), the vignette's curve, smoothing, ring geometry and its alpha texture (the layer). Tested by
 *    tests/test_comfort.c.
 */
#ifndef SOMVR_COMFORT_H
#define SOMVR_COMFORT_H

#include "somvr_shared.h"

/* ==== 1. the ABI ================================================================================================= */
#define SOMVR_COMFORT_SYMBOL "somvr_comfort"
#define SOMVR_COMFORT_MAGIC 0x31464d43524d4f53ull /* "SOMRCMF1" */
#define SOMVR_COMFORT_VERSION 1u

enum {
    SOMVR_CFF_COUNTED = 1u,   /* the speeds were measured over this frame */
    SOMVR_CFF_FIRST = 2u,     /* no frame before it (start, or after a gap): speeds 0 */
    SOMVR_CFF_GAP = 4u,       /* more than SOMVR_CF_GAP_S since the last stereo frame: speeds 0 */
    SOMVR_CFF_SNAP = 8u,      /* a snap turn landed in this frame (not motion: the blink's job) */
    SOMVR_CFF_RECENTER = 16u, /* the rig latched a new reference this frame */
    SOMVR_CFF_JUMP = 32u,     /* a teleport-sized change (a load, a cut, a base fallback): not motion */
    SOMVR_CFF_HELD = 64u,     /* the speeds are the last counted frame's */
    SOMVR_CFF_SNAP_HELD = 128u, /* a snap turn is waiting for the blink's black */
    SOMVR_CFF_WRAITH = 256u     /* build 12, the wraith dash (SOMVR_BOW_STRIKE=dash|blink): the ring is the white-blue
                                   wraith tunnel at the amount in bits 16..23 (0..255 = 0..1; at least the vignette's),
                                   the blink is white (a layer without it: the frame's black blink, no tunnel) */
};
#define SOMVR_CFF_TUNNEL_SHIFT 16
static inline uint32_t somvr_cff_tunnel_bits(float a) {
    if (!(a > 0)) return 0;
    return (uint32_t)(a >= 1 ? 255 : (int)(a * 255 + 0.5f)) << SOMVR_CFF_TUNNEL_SHIFT;
}
static inline float somvr_cff_tunnel(uint32_t flags) { return (float)((flags >> SOMVR_CFF_TUNNEL_SHIFT) & 255u) / 255; }

typedef struct {
    _Atomic uint64_t seq; /* the frame id this entry is for; 0 while written; stored last (release) */
    float lin_mps;        /* the head's world point, moved by the rig: m/s */
    float ang_dps;        /* the rig's yaw rate, deg/s (>= 0; snap steps are not in it) */
    float blink;          /* the snap blink's black over this frame, 0..1 (1 = the frame a held snap turn landed) */
    float dt_s;           /* the interval measured over (0: none) */
    uint32_t flags;       /* SOMVR_CFF_* */
    uint32_t snaps;       /* snap turns applied so far */
} somvr_comfort_frame;

typedef struct somvr_comfort_shared {
    uint64_t magic;
    uint32_t version, size;
    _Atomic uint32_t active;       /* 1: the preload publishes; set last (release) */
    int32_t pid;
    float vignette;                /* SOMVR_VIGNETTE: the vignette's strength 0..1 (0 = no vignette) */
    uint32_t blink_ms;             /* SOMVR_SNAP_BLINK_MS (0 = no blink) */
    _Atomic uint64_t last;         /* the newest frame id written */
    somvr_comfort_frame f[SOMVR_NREC];
    _Atomic uint64_t n_frames;     /* preload: entries written */
    _Atomic uint64_t n_vignette;   /* layer: frames submitted with the vignette's ring */
    _Atomic uint64_t n_blink;      /* layer: frames submitted with the blink's black */
} somvr_comfort_shared;

_Static_assert(sizeof(somvr_comfort_frame) == 32, "somvr_comfort_frame size");
_Static_assert(offsetof(somvr_comfort_shared, vignette) == 24 && offsetof(somvr_comfort_shared, last) == 32 &&
                   offsetof(somvr_comfort_shared, f) == 40 && offsetof(somvr_comfort_shared, n_frames) == 296,
               "somvr_comfort_shared offsets");
_Static_assert(sizeof(somvr_comfort_shared) == 320, "somvr_comfort_shared size");
_Static_assert(sizeof(somvr_shared) == 2664 && SOMVR_SHARED_VERSION == 2, "somvr_shared stays v2");

static inline int somvr_comfort_ok(const somvr_comfort_shared *c) {
    return c && c->magic == SOMVR_COMFORT_MAGIC && c->version == SOMVR_COMFORT_VERSION &&
           c->size == (uint32_t)sizeof(somvr_comfort_shared) &&
           atomic_load_explicit(&((somvr_comfort_shared *)c)->active, memory_order_acquire) == 1;
}

/* the game thread: frame `frame`'s entry (in->seq is ignored) */
static inline void somvr_comfort_put(somvr_comfort_shared *c, uint64_t frame, const somvr_comfort_frame *in) {
    somvr_comfort_frame *e = &c->f[frame % SOMVR_NREC];
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->lin_mps = in->lin_mps;
    e->ang_dps = in->ang_dps;
    e->blink = in->blink;
    e->dt_s = in->dt_s;
    e->flags = in->flags;
    e->snaps = in->snaps;
    atomic_store_explicit(&e->seq, frame, memory_order_release);
    atomic_store_explicit(&c->last, frame, memory_order_release);
    atomic_fetch_add_explicit(&c->n_frames, 1, memory_order_relaxed);
}
/* the layer: frame `frame`'s entry; 1 = found (not overwritten, not being written) */
static inline int somvr_comfort_get(somvr_comfort_shared *c, uint64_t frame, somvr_comfort_frame *out) {
    if (!frame) return 0;
    somvr_comfort_frame *e = &c->f[frame % SOMVR_NREC];
    if (atomic_load_explicit(&e->seq, memory_order_acquire) != frame) return 0;
    out->lin_mps = e->lin_mps;
    out->ang_dps = e->ang_dps;
    out->blink = e->blink;
    out->dt_s = e->dt_s;
    out->flags = e->flags;
    out->snaps = e->snaps;
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&e->seq, memory_order_relaxed) != frame) return 0;
    atomic_store_explicit(&out->seq, frame, memory_order_relaxed);
    return 1;
}

/* ==== 2. the pure part (somvr_comfort.c) =========================================================================== */

/* ---- the game side: the rig's artificial motion ---------------------------------------------------------------- */
/* The rig maps the tracked head h (game convention: cm, the mirrored tracking space) to the world:
 * world(h) = b + s Y(a) (h - p0), with b the frame's base, a = psi - yaw0, p0 the recentre reference and s the player
 * size (somvr_eyes.h; s 0 = 1). */
typedef struct { float b[3]; float a; float p0[3]; float s; } somvr_cf_map;
void somvr_cf_yaw_rot(float a, const float v[3], float out[3]); /* = sv_qrot(sv_qyaw(a), v): + = turn right */
void somvr_cf_map_apply(const somvr_cf_map *m, const float h[3], float out[3]);

#define SOMVR_CF_GAP_S 0.25      /* a longer interval (menus, a load, a hitch): start again */
#define SOMVR_CF_MIN_DT_S 1e-4   /* a shorter one (the same display time twice): measure from the frame before */
#define SOMVR_CF_JUMP_MPS 40.0f  /* faster than this is a jump, not motion (Talion sprints at ~8, falls at ~20) */
#define SOMVR_CF_JUMP_CM 200.0f  /* ... and so is more than this in one frame */
#define SOMVR_CF_JUMP_DPS 1000.0f /* a yaw faster than this is a cut (the game camera's), not a turn */

typedef struct {
    int have;           /* a previous frame */
    somvr_cf_map last;  /* its mapping */
    double t;           /* its time (s) */
    float lin, ang;     /* the speeds published last (kept over frames that do not count) */
    uint64_t frames, counted, held, jumps, gaps;
} somvr_cf_meas;
void somvr_cf_meas_reset(somvr_cf_meas *s);
/* the rig's events of a frame (somvr_cf_measure's rig_ev): it latched a new reference; its base jumped (changed its
 * source - the head node <-> the game camera, the fallback's swap - or was re-latched): not motion, whatever the size */
enum { SOMVR_CF_EV_RECENTER = 1, SOMVR_CF_EV_JUMP = 2 };
/* One stereo frame: its mapping m (after the frame's recentre, snap, turn and body follow), the tracked head h, the
 * frame's time t (s: the views' display time, else the game clock), the snap applied this frame (rad, 0 = none) and
 * the rig's events (SOMVR_CF_EV_*; 1 = recentred, as before). Out: the speeds (m/s, deg/s) and the interval; returns
 * SOMVR_CFF_* flags. NaN inputs: a jump (nothing counted). */
uint32_t somvr_cf_measure(somvr_cf_meas *s, const somvr_cf_map *m, const float h[3], double t, float snap_rad,
                          int rig_ev, float *lin_mps, float *ang_dps, float *dt_s);

/* ---- the game side: the snap blink (SOMVR_SNAP_BLINK_MS) ------------------------------------------------------- */
/* HoF2's quick blink: dark for a moment, turned at black. A snap turn is held while the view fades out (a third of
 * the time), lands on the first fully black frame, then the view fades in (two thirds). */
typedef struct {
    int phase;          /* 0 idle, 1 fading out (the turn held), 2 fading in */
    double t0;          /* phase 1: when it would have started at black 0; phase 2: when the turn landed */
    double t_last;      /* the last frame's time (the first frame of a fade-out is already one frame dark) */
    float alpha;        /* the last frame's black */
    uint64_t blinks, held_frames;
} somvr_blink;
void somvr_blink_times(uint32_t ms, float *out_s, float *in_s); /* ms -> out = ms / 3, in = ms - out (0: off) */
void somvr_blink_reset(somvr_blink *b);
/* Per stereo frame at time t (s), before the rig: want = a snap turn is pending. Returns 1 = apply the pending turn now
 * (*alpha is then 1 with a blink), 0 = keep it pending (or none). out_s = in_s = 0: no blink (returns want, alpha 0). */
int somvr_blink_frame(somvr_blink *b, float out_s, float in_s, int want, double t, float *alpha);

/* ---- the game side: seated height (SOMVR_SEATED, SOMVR_HEIGHT_CM) ------------------------------------------------ */
/* The seated eye height (the rig's vertical reference p0.y) is set at every recentre (the first stereo frame, the
 * trigger word "recenter", the headset's own recentre): the view then shows Talion's eyes (the base) while the head is
 * at that height. With a floor (STAGE space): SOMVR_HEIGHT_CM if given (a fixed height above the floor: never taken
 * from where the head happens to be, the HoF2 lesson), else the head at the recentre if it is a seated head
 * (SOMVR_SEAT_MIN_CM..MAX_CM), else the last good one (else SOMVR_SEAT_DEFAULT_CM). Without a floor (LOCAL: y = 0 is
 * the head at the runtime's recentre) the head at the recentre. */
#define SOMVR_SEAT_MIN_CM 60.0f
#define SOMVR_SEAT_MAX_CM 170.0f
#define SOMVR_SEAT_DEFAULT_CM 120.0f
typedef struct { int on; int has_height; float height_cm; } somvr_seat_cfg;
typedef struct { int have; float y; uint64_t latched, rejected; } somvr_seat;
/* the reference for a recentre with the head at head_y (cm, tracking space); *why: how it was chosen (a log word) */
float somvr_seat_ref(somvr_seat *s, const somvr_seat_cfg *c, float head_y, int floor, const char **why);

/* ---- the layer: the vignette ---------------------------------------------------------------------------------- */
typedef struct {
    float lin_lo, lin_hi;  /* m/s: nothing below lo, full at hi */
    float ang_lo, ang_hi;  /* deg/s: likewise for the yaw rate */
    float tau_f;           /* s: the target's own low-pass (jitter) */
    float tau_in, tau_out; /* s: the amount follows the filtered target closing in / opening */
    float min_deg;         /* the clear half-angle at strength 1 and full speed */
    float margin_deg;      /* the ring starts this far outside the view's corners (no pop) */
    float dist_m;          /* the quads' distance in the VIEW space (far: the same ring in both eyes, HoF2) */
    int fit;               /* SOMVR_VIG_FIT_*: how the clear disc closes with the amount (build 10: the lens) */
} somvr_vig_cfg;
void somvr_vig_cfg_default(somvr_vig_cfg *c);
/* SOMVR_VIGNETTE_FIT (the layer): CORNER = build 9's: the clear disc closes linearly from just outside the view's
 * corners to min_deg, full at a jog (3 m/s) - on a Quest 2 (corner 59.6 deg, the lens showing ~46-50 deg) a walk at 40 %
 * darkened only the corners nobody sees (session 2026-10-24 16:50). LENS = build 10's: the same start (nothing anywhere
 * at amount 0, no pop) but the disc closes along sqrt(amount) to 15 deg, and a stick walk (2 m/s) or a 60 deg/s turn is
 * full speed: 20 % darkens the lens's rim, 40 % is a clear tunnel (black from ~48 deg), 70 % strong (black from ~36),
 * 100 % a porthole (black from ~25). somvr_vig_cfg_fit sets the profile's numbers. */
enum { SOMVR_VIG_FIT_CORNER = 0, SOMVR_VIG_FIT_LENS = 1 };
void somvr_vig_cfg_fit(somvr_vig_cfg *c, int fit);
/* 0..1 from the speeds (smoothstep of each, the larger wins), before the strength */
float somvr_vig_target(const somvr_vig_cfg *c, float lin_mps, float ang_dps);
typedef struct { float f, amount; } somvr_vig_state; /* the filtered target, the amount shown (x strength) */
/* one frame dt (s) on: target (0..1, x strength); returns the amount shown (0 below the cut-off) */
float somvr_vig_step(const somvr_vig_cfg *c, somvr_vig_state *s, float target, float dt_s);
/* a snap turn landed this frame (the layer, no snap blink): the tunnel at the full strength at once (it hides the
 * periphery's jump in the very frame of the turn), then it opens as after a stop (tau_out) */
void somvr_vig_snap(somvr_vig_state *s, float strength);
/* the frame's eye fovs (radians: left right up down, as XrFovf) -> the largest |tan| of any edge (the square quad
 * must reach it) and the largest angle of a corner from the view axis (rad) */
void somvr_vig_extent(const float fov[2][4], float *cover_tan, float *corner_rad);
/* ... the smallest angle of any edge from the view axis (rad): about where the lens's picture ends (logs, tests) */
float somvr_vig_edge(const float fov[2][4]);
typedef struct {
    float clear_rad;  /* the clear half-angle shown */
    float half_tan;   /* the ring quad's half-size, tangent units (x dist_m = metres) */
    float cover_tan;  /* the blink quad's half-size, tangent units: the view's cover + 10 % */
    int clamped;      /* the ring could not close that far (the quad must still cover the view) */
} somvr_vig_ring;
somvr_vig_ring somvr_vig_geom(const somvr_vig_cfg *c, float amount, float cover_tan, float corner_rad);
/* the ring's black (0..1) at angle a (rad) from the view axis */
float somvr_vig_dark(const somvr_vig_ring *g, float a);

/* The alpha texture (made once): black, rows 0..511 the ring (alpha 0 inside SOMVR_VIG_R0 of the half-size, a
 * smoothstep to 1 at R0 x K, 1 out to the corners), rows 512..519 opaque (a guard), rows 520..527 the blink's 128
 * uniform cells (4 px wide, alpha k/127; a cell's rect is its 2x6 interior, so filtering never reaches a neighbour). */
#define SOMVR_VIG_RING_PX 512
#define SOMVR_VIG_IMG_W 512
#define SOMVR_VIG_IMG_H 528
#define SOMVR_VIG_CELL_Y 520
#define SOMVR_VIG_CELLS 128
#define SOMVR_VIG_R0 0.16f
#define SOMVR_VIG_K 1.75f
float somvr_vig_profile(float r); /* the ring's alpha at r (texel distance from the centre / half-size) */
void somvr_vig_image(uint8_t *px, int bgra); /* SOMVR_VIG_IMG_W x _H x 4 bytes, rows top to bottom */
/* build 12: the wraith tunnel's image (SOMVR_CFF_WRAITH): the same layout and alphas, coloured white-blue (linear
 * 0.62, 0.80, 1.0), premultiplied */
#define SOMVR_VIG_WRAITH_R 0.62f
#define SOMVR_VIG_WRAITH_G 0.80f
#define SOMVR_VIG_WRAITH_B 1.0f
void somvr_vig_image_wraith(uint8_t *px, int bgra);
void somvr_vig_rect_ring(int32_t rc[4]);       /* offset x y, extent w h */
int somvr_vig_cell(float alpha);               /* the cell for a black (0..1): round(alpha x 127) */
void somvr_vig_rect_cell(int cell, int32_t rc[4]);

#endif
