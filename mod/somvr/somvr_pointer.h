/* somvr_pointer.h - the VR laser pointer (docs/re-ui-pointer.md 3-4, docs/re-xr-pipeline.md 10): a controller ray on the
 * floating menu quad (and the world-locked HUD quad). Pure: no globals, no OpenXR/Vulkan calls, no other module.
 * Shared by the Vulkan layer (the ray, its hit, hysteresis, smoothing, which hand, the click edges, the dot and ray
 * quads it draws) and somvr.so (the hit's UV -> UI pixels, the trigger's owner per press). Tested by
 * tests/test_pointer.c.
 *
 * Conventions: poses in the layer's XR space (somvr_shared.xr.space: LOCAL or STAGE), XrPosef layout (somvr_posef),
 * metres. A ray leaves its pose along the pose's -Z (OpenXR's aim pose; the grip's -Z as the fallback). A quad
 * (XrCompositionLayerQuad) is centred on its pose, seen from its +Z side, w x h metres along its X and Y. uv: u = 0 at
 * its left edge (-X), 1 at the right; v = 0 at the top (+Y), 1 at the bottom = the image's x and y (a swapchain
 * image's row 0 is the quad's top edge).
 */
#ifndef SOMVR_POINTER_H
#define SOMVR_POINTER_H

#include <stdint.h>

#include "somvr_input.h"

/* ---- geometry ----------------------------------------------------------------------------------------------------- */
/* the ray of a pose: origin = its position, dir = its -Z (unit) */
void somvr_ptr_ray(const somvr_posef *pose, float o[3], float d[3]);
/* The ray (o, unit d) against a quad (pose, size w x h): 1 = it meets the quad's plane in front of o (0 < t <= max_t),
 * coming from the quad's front (+Z) side toward it; uv = the meeting point in the quad's uv, NOT clamped (outside
 * [0,1] = beside the quad), *t = the distance. 0 = parallel, behind, from the back, beyond max_t, or a degenerate quad. */
int somvr_ptr_hit_quad(const float o[3], const float d[3], const somvr_posef *quad, const float size[2], float max_t,
                       float uv[2], float *t);
/* the point uv of a quad, lifted `lift` metres off its plane toward its +Z (the viewer's side) */
void somvr_ptr_quad_point(const somvr_posef *quad, const float size[2], const float uv[2], float lift, float out[3]);
/* A thin quad from a to b (the ray drawn): centred between them, its X along b - a (|b - a| wide), `thickness` high,
 * its face turned about the line toward `eye` (NULL, or eye on the line: toward the world's up instead). 1 = made,
 * 0 = a and b closer than 1 mm (nothing to draw). */
int somvr_ptr_ray_quad(const float a[3], const float b[3], const float *eye, float thickness, somvr_posef *pose,
                       float size[2]);
/* the dot's diameter (m) for a hit `dist` metres from the ray's origin: `deg` degrees seen from there, at least 4 mm */
float somvr_ptr_dot_size(float dist, float deg);

/* ---- the pointer state (the layer, once per frame) ---------------------------------------------------------------- */
typedef struct {
    float leave;        /* uv margin: on stays on until the point leaves [-leave, 1 + leave]^2 (0.02); it comes on
                           only inside [0,1]^2 */
    float max_dist;     /* m: a hit farther away is none (20) */
    float min_cutoff;   /* One Euro filter on uv: the cutoff at rest, Hz (1.2) */
    float beta;         /* ... growing by this per uv/s of speed (10) */
    float d_cutoff;     /* ... its speed estimate's cutoff, Hz (2.5) */
    float trig_on, trig_off; /* the click: trigger above trig_on presses, below trig_off releases (0.6 / 0.4, somvr_ctl's) */
    int lose_frames;    /* frames the pointing hand may lack its pose while a click is held (a tracking gap) before the
                           click ends (6) */
    /* build 20 (SOMVR_XR_PTR_HOLD; 0 = off, build 19's point): the held point - the output stays put while the filtered
     * point moves less than hold_deg (seen from the hand: dist x tan) away from it and follows by the excess beyond
     * (1:1, no lag past the radius); inside it eases toward the filtered point, the faster the farther (settle_s at
     * the radius' edge, (offset / radius)^2 of that inside: a tremor's small offsets barely move it) */
    float hold_deg;     /* 0 (SOMVR_XR_PTR_HOLD=on: 0.6) */
    float settle_s;     /* 0.3 */
} somvr_ptr_cfg;
void somvr_ptr_cfg_default(somvr_ptr_cfg *c);

typedef struct {
    int ok[2];               /* hand h has a ray this frame */
    int grip[2];             /* ... taken from its grip pose (its aim pose not valid) */
    somvr_posef pose[2];     /* the ray's pose per hand */
    float trigger[2];        /* 0..1 (0 when the hand's input is not active) */
    int64_t time;            /* XrTime of the poses */
    uint32_t quad;           /* SOMVR_PTRQ_*: the target this frame (NONE: nothing to hit; a held click ends) */
    somvr_posef quad_pose;
    float quad_size[2];
} somvr_ptr_in;
/* hand h's ray from a layer snapshot: the aim pose with the pose action active and orientation + position valid, else
 * the grip pose (grip[h] = 1) on the same terms, else none; the trigger only with the hand's input active. time, quad,
 * quad_pose and quad_size are left to the caller. */
void somvr_ptr_in_from(const somvr_input_state *st, somvr_ptr_in *in);

typedef struct {
    int hand;                /* the pointing hand: the last one that pulled its trigger (right at the start) */
    int on, down;            /* the pointer is on the quad (hysteresis) / a click is held */
    int trig[2];             /* each hand's trigger, debounced */
    int lost;                /* frames the pointing hand has had no ray */
    uint32_t quad;           /* the target of the last update */
    int have_f;              /* the filter holds a value */
    float f[2], df[2];       /* the One Euro state: uv and its speed (uv/s) */
    int64_t t;               /* XrTime of the last filtered sample */
    float uv[2];             /* the last output point (smoothed, clamped; build 20: held) */
    int have_h;              /* build 20: the held point holds a value */
    float h[2];              /* ... the held point (uv) */
    int64_t ht;              /* ... XrTime of its last step */
    float dist;              /* ... its distance from the ray's origin */
    uint32_t downs, ups, enters, switches, cancels;
} somvr_ptr_state;
void somvr_ptr_reset(somvr_ptr_state *s);

enum {                       /* somvr_ptr_out.edges: what happened this frame */
    SOMVR_PTRE_ENTER = 1u,   /* the pointer came on the quad */
    SOMVR_PTRE_LEAVE = 2u,   /* ... went off it */
    SOMVR_PTRE_PRESS = 4u,   /* a click began (downs + 1) */
    SOMVR_PTRE_RELEASE = 8u, /* a click ended (ups + 1) */
    SOMVR_PTRE_SWITCH = 16u, /* the other hand pulled its trigger: it points now */
    SOMVR_PTRE_CANCEL = 32u, /* the click ended without its trigger's release (the ray, the hand or the quad lost) */
};
typedef struct {
    uint32_t flags;          /* SOMVR_PTRF_ON | _DOWN | _AIM | _GRIP (as published; the layer adds _DRAWN) */
    uint32_t edges;          /* SOMVR_PTRE_* */
    int hand;
    float uv[2];             /* with ON: the smoothed point, clamped to [0,1] */
    float hit[3];            /* with ON: that point on the quad's plane (XR space) */
    float origin[3], dir[3]; /* with AIM: the pointing hand's ray */
    float dist;              /* with ON: metres from origin to hit */
    uint32_t consume;        /* bit h: hand h's trigger is the pointer's (its ray inside the quad + leave, or it points
                                and is on / holds a click) */
    int raw_hit[2];          /* hand h's ray inside the quad + leave this frame */
    float raw_uv[2][2];      /* ... where (unsmoothed, unclamped) */
} somvr_ptr_out;

/* One frame. The rules (re-ui-pointer.md 3.1):
 *   - hands: a trigger's press (rising past trig_on) by the hand that does not point makes it the pointing hand (a click
 *     the other hand held ends: RELEASE | CANCEL); the pointer is then off until the new hand's ray comes on;
 *   - on: comes on when the pointing hand's ray meets the quad inside [0,1]^2, stays on inside [-leave, 1 + leave]^2;
 *     while a click is held it stays on wherever the ray meets the quad's plane (a drag; uv clamped), through a gap of
 *     up to lose_frames frames without the hand's pose; the plane missed, a longer gap or another target: the click ends
 *     (CANCEL) and the pointer goes off;
 *   - uv: a One Euro filter (min_cutoff, beta, d_cutoff) over the frames' times, restarted at the raw point when the
 *     pointer comes on (or the time jumps by more than 0.25 s); output clamped to [0,1]; build 20: with hold_deg the
 *     output is the held point over that (restarted with the filter);
 *   - click: the pointing hand's trigger press while on = PRESS (down); its release = RELEASE. A press while off is no
 *     click (the trigger stays the pad's; somvr routes it by consume). */
void somvr_ptr_update(somvr_ptr_state *s, const somvr_ptr_cfg *c, const somvr_ptr_in *in, somvr_ptr_out *out);

/* ---- the game side (somvr.so) ------------------------------------------------------------------------------------ */
/* uv on the quad -> the UI surface pixel the CInterfaceMgr mouse calls take (re-ui-pointer.md 4): x = u iw + vx,
 * y = v ih + vy, rounded to the nearest pixel, kept inside [vx, vx + iw - 1] x [vy, vy + ih - 1]. iw or ih < 1: 0. */
int somvr_ptr_pixel(const float uv[2], int iw, int ih, int vx, int vy, int out[2]);

/* The trigger's owner from its press to its release, per hand: at the press (down rising) it becomes the pointer's if
 * `consume` (the layer's bit for that hand) is set, else it stays the pad's (LT/RT through the binding layer) for the
 * whole hold. Returns 1 while the pointer owns it (the caller drops the pad's trigger). */
typedef struct { int held[2], mine[2]; } somvr_ptr_route;
int somvr_ptr_route_tick(somvr_ptr_route *r, int h, int down, int consume);

/* build 20: SOMVR_XR_PTR_HOLD (the layer): unset/"off"/"0" = 0 (build 19's point); "on" = 0.6 deg, 0.3 s; "DEG" or
 * "DEG,MS" (0 < DEG <= 3, 0 <= MS <= 2000); anything else = off and *bad = 1. Sets c->hold_deg, c->settle_s. */
void somvr_ptr_parse_hold(const char *v, somvr_ptr_cfg *c, int *bad);

/* SOMVR_XR_POINTER (the layer): unset/"on" = ON (the dot and the ray), "dot" = DOT (the dot only), "off"/"0" = OFF (no
 * pointer at all, nothing published); anything else = ON and *bad = 1 */
enum { SOMVR_PTRM_OFF = 0, SOMVR_PTRM_ON = 1, SOMVR_PTRM_DOT = 2 };
int somvr_ptr_parse_mode(const char *v, int *bad);

/* The pointer image (the layer's XR swapchain, filled once): w x h = SOMVR_PTR_IMG_W x SOMVR_PTR_IMG_H RGBA8, alpha
 * premultiplied (the layers use BLEND_TEXTURE_SOURCE_ALPHA only), the bytes as an _SRGB format stores them. Left
 * square: the dot (a white disc in a dark ring, soft edge); right square: the ray (along x from transparent at x = 32
 * to bright at x = 63, a soft profile across y). bgra: B and R swapped (a B8G8R8A8 format). */
#define SOMVR_PTR_IMG_W 64u
#define SOMVR_PTR_IMG_H 32u
void somvr_ptr_image(uint8_t *px, int bgra);
/* the subImage rects {x, y, w, h} of the two parts */
void somvr_ptr_rect(int ray, int32_t out[4]);

#endif
