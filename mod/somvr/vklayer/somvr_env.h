/* somvr_env.h - SoM loading env: the environment behind mono frames (menus, loading screens, videos, cinematics
 * outside H4), so a flat moment never leaves the player in a black void (docs/re-xr-pipeline.md 14). Pure: no Vulkan or
 * OpenXR calls, no globals; used by somvr_vklayer.c (region "SoM loading env"), checked by tests/test_env.c.
 *
 * The sky (somvr_env_sky): a procedural Mordor dusk as seen from the environment's centre - a dark red-brown gradient
 * (purple-black at the zenith), a faint glow along the horizon (warmer toward Orodruin's distant cone, 100 deg to the
 * right of ahead), low silhouette ridges, a dim ground below the horizon. All of it is subtle: no channel above 0.6
 * (sRGB). One static RGBA image (somvr_env_image) holds it either as an equirectangular map of the whole sphere (for an
 * XR_KHR_composition_layer_equirect2 layer) or as six cube tiles (the fallback: a box of world-locked quads around the
 * centre, its floor at floor height), and below that a strip with the premultiplied black cell that "world" mode dims
 * the kept frame with.
 *
 * Directions: y up, -z ahead, +x right (OpenXR's). Azimuth a = atan2(x, -z): 0 ahead, + to the right. Elevation e =
 * asin(y): + up. The equirect map: column centre u = (x + 0.5) / W -> a = (u - 0.5) 2 pi, row centre v = (y + 0.5) / H
 * -> e = (0.5 - v) pi (row 0 = up). */
#ifndef SOMVR_ENV_H
#define SOMVR_ENV_H

#include <stdint.h>

#include "../somvr_shared.h" /* somvr_posef, somvr_fovf */

/* ---- the switch ---------------------------------------------------------------------------------------------------- */
/* SOMVR_XR_ENV: unset/"off"/"0" = OFF (default: the black void as before); "on"/"1"/"sky" = ON (the sky behind mono
 * frames); "world" = WORLD (also the last stereo frame, kept and dimmed). Anything else = OFF and *bad = 1. */
enum { SOMVR_ENV_OFF = 0, SOMVR_ENV_ON = 1, SOMVR_ENV_WORLD = 2 };
int somvr_env_parse(const char *v, int *bad);
/* SOMVR_XR_ENV_DIM=0..1: the kept frame's brightness in WORLD mode (default 0.4). Anything else = 0.4 and *bad = 1. */
#define SOMVR_ENV_DIM_DEFAULT 0.4f
float somvr_env_parse_dim(const char *v, int *bad);

/* ---- the image ----------------------------------------------------------------------------------------------------- */
enum { SOMVR_ENV_KIND_EQ = 0, SOMVR_ENV_KIND_CUBE = 1 };
#define SOMVR_ENV_EQ_W 1024 /* 2.8 texels per degree: the gradient is smooth, the ridges soft (distant, hazy) */
#define SOMVR_ENV_EQ_H 512
#define SOMVR_ENV_TILE 256  /* a cube tile; its quad shows the inner (TILE - 2)^2 (a 1-texel guard all round) */
#define SOMVR_ENV_CUBE_W (3 * SOMVR_ENV_TILE)
#define SOMVR_ENV_CUBE_H (2 * SOMVR_ENV_TILE)
#define SOMVR_ENV_STRIP 5   /* rows below the sky part: 1 guard row (the sky's last row again), 4 rows of the dim cell */
static inline uint32_t somvr_env_img_w(int kind) { return kind == SOMVR_ENV_KIND_EQ ? SOMVR_ENV_EQ_W : SOMVR_ENV_CUBE_W; }
static inline uint32_t somvr_env_sky_h(int kind) { return kind == SOMVR_ENV_KIND_EQ ? SOMVR_ENV_EQ_H : SOMVR_ENV_CUBE_H; }
static inline uint32_t somvr_env_img_h(int kind) { return somvr_env_sky_h(kind) + SOMVR_ENV_STRIP; }

/* the colour seen in direction d (any length; NaN/zero = ahead): sRGB-encoded 0..1 */
void somvr_env_sky(const float d[3], float rgb[3]);
/* the same by elevation and azimuth (radians; a any value, wrapped) */
void somvr_env_sky_ea(float e, float a, float rgb[3]);
/* Fill px (somvr_env_img_w x _img_h texels, 4 bytes each, tightly packed): the sky part (the map, or the six tiles),
 * the guard row, the dim strip (0, 0, 0, round(255 (1 - dim))) premultiplied. bgra: B G R A byte order; srgb: the
 * bytes are sRGB-encoded (an _SRGB format), else linear (an _UNORM format). The sky is dithered (+-0.5 LSB, a fixed
 * per-texel hash) against banding in the dark gradient; alpha 255. */
void somvr_env_image(uint8_t *px, int kind, int bgra, int srgb, float dim);
/* the sky part's rect {x, y, w, h}: EQ = the whole map (i ignored); CUBE = tile i's inner rect */
void somvr_env_sky_rect(int kind, int i, int32_t rc[4]);
/* the dim cell's rect: the middle 2 x 2 of the strip's 4 rows at x 1..2 (the filter never reaches the guard row) */
void somvr_env_dim_rect(int kind, int32_t rc[4]);

/* ---- the geometry -------------------------------------------------------------------------------------------------- */
#define SOMVR_ENV_RADIUS_M 30.0f   /* the equirect's sphere; the box's walls and ceiling from the centre */
#define SOMVR_ENV_EYE_M 1.6f       /* the box's floor below the centre (LOCAL's origin: the head at the recentre) */
#define SOMVR_ENV_DIM_DIST_M 20.0f /* world mode: the dim quads, one per eye, cover that eye's kept view at 20 m */
#define SOMVR_ENV_DIM_MARGIN 1.01f /* ... 1 % larger (a dark sliver of sky rather than an undimmed one of the world) */
/* one face of the box, relative to the centre: its centre c, unit right r / up u (the image's +x / -row directions),
 * normal n (toward the centre, the quad's +Z), half extents hw x hh (m), and the quad's pose (orientation: local X, Y, Z
 * -> r, u, n; position c). Faces: 0 ahead (-z), 1 right (+x), 2 behind, 3 left, 4 the ceiling, 5 the floor (at
 * -SOMVR_ENV_EYE_M; the walls reach down to it). */
typedef struct {
    float c[3], r[3], u[3], n[3], hw, hh;
    somvr_posef pose;
} somvr_env_face;
void somvr_env_face_get(int i, somvr_env_face *f);
/* the point (relative to the centre) at face coordinates s, t in -1..1 (s = -1 the image's left, t = +1 its top) */
void somvr_env_face_point(const somvr_env_face *f, float s, float t, float out[3]);
/* World mode: the quad over eye view (pose, fov) at `dist` ahead: centred on the view's frustum at that distance,
 * turned like the eye, `margin` x the frustum's size: from the eye it covers exactly what the projection view shows. */
void somvr_env_dim_quad(const somvr_posef *eye, const somvr_fovf *fov, float dist, float margin, somvr_posef *out,
                        float size[2]);

/* ---- the transitions ----------------------------------------------------------------------------------------------- */
/* What is on screen (shown): the world (stereo frames) or the environment (mono frames). When the frame wanted is the
 * other side, the shown side is held (its last layers again) while the fade quad goes to black over SOMVR_ENV_OUT_S;
 * on the black frame it switches and the new side fades in over SOMVR_ENV_IN_S. A side that cannot be held (nothing to
 * show again) switches at once, at black. A stray frame of the other side only dips a little. Times are the frames'
 * predicted display times; a step is at most SOMVR_ENV_DT_MAX_S (a hitch does not skip the fade). */
#define SOMVR_ENV_OUT_S 0.2f
#define SOMVR_ENV_IN_S 0.3f
#define SOMVR_ENV_DT_MAX_S 0.1f
enum { SOMVR_ENV_SHOW_WORLD = 0, SOMVR_ENV_SHOW_ENV = 1 };
enum { SOMVR_ENVT_DRAW = 0, SOMVR_ENVT_HOLD = 1 };
typedef struct {
    int started;       /* a first frame was seen (it starts at black: the session fades in) */
    int shown;         /* SOMVR_ENV_SHOW_* */
    float black;       /* 0..1, the fade quad's black */
    int64_t t_last;    /* the display time of the last step (ns) */
    uint32_t holds;    /* hold frames since the last switch */
    uint64_t switches;
} somvr_env_trans;
/* One frame: want = the side this present is (SOMVR_ENV_SHOW_*), can_hold = the shown side's last layers can be shown
 * again, t_ns = the frame's predicted display time. Returns HOLD (show the shown side's last layers again, fading out)
 * or DRAW (draw `want`; *switched = 1 on the frame the shown side changed: it is drawn at black 1). */
int somvr_env_step(somvr_env_trans *s, int want, int can_hold, int64_t t_ns, int *switched);
/* the fade quad's level for the frame (0..255) */
uint32_t somvr_env_level(const somvr_env_trans *s);

#endif
