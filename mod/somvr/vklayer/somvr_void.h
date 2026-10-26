/* somvr_void.h - never a void (HG-9, docs/re-xr-pipeline.md 36, handoff/HANDOFF-void.md): the pure parts of the
 * keep-alive frames and the loading space. No Vulkan or OpenXR calls, no globals; used by somvr_void_layer.c, checked by
 * tests/test_void.c.
 *
 * The loading space (SOMVR_XR_LOADSPACE=on) adds two things to SOMVR_XR_ENV's sky: a floor (a dark stone disc at the
 * real floor, world-locked under the player: the sky's ground is at infinity, the disc gives the feet a place) and the
 * progress lights (HoF2's user-approved "ember-lit progress": a shallow arc of 12 small ember lights low ahead, about
 * 2.2 m out and 0.3 m above the floor, a slow soft wave running along them while the game is busy: SoM's loads give no
 * progress value). Both live in one static RGBA image (the floor's cell, then the lights' frames: 20 wave phases x 6
 * brightness levels, each 256 x 48), premultiplied alpha, uploaded once; a frame picks its cells by sub-image rect.
 * Directions as somvr_env.h: y up, -z ahead, +x right; yaw = atan2(x, -z), + to the right. */
#ifndef SOMVR_VOID_H
#define SOMVR_VOID_H

#include <stdint.h>

#include "../somvr_shared.h" /* somvr_posef */

/* ---- the switches ------------------------------------------------------------------------------------------------ */
/* on/off switches: unset/"off"/"0" = 0, "on"/"1" = 1; anything else = 0 and *bad = 1 */
int somvr_void_parse_onoff(const char *v, int *bad);
/* a time in ms within lo..hi; unset/empty = def; anything else = def and *bad = 1 */
int somvr_void_parse_ms(const char *v, int def, int lo, int hi, int *bad);
#define SOMVR_VOID_KEEP_MS 100   /* SOMVR_XR_KEEPALIVE_MS: the game away this long = our frames (50..1000) */
#define SOMVR_VOID_SPACE_MS 1000 /* SOMVR_XR_LOADSPACE_MS: a stall in the world this long goes to the loading space */

/* ---- the image ----------------------------------------------------------------------------------------------------- */
#define SOMVR_VOID_FLOOR_PX 512 /* the floor's cell: (0, 0) .. 512 x 512 */
#define SOMVR_VOID_LIGHT_W 256  /* one lights frame */
#define SOMVR_VOID_LIGHT_H 48
#define SOMVR_VOID_WAVES 20     /* the wave's phases (one period: SOMVR_VOID_WAVE_S) */
#define SOMVR_VOID_LEVELS 6     /* brightness levels 1..6 (6 = full) */
#define SOMVR_VOID_COLS 4       /* lights frames per row of the image */
#define SOMVR_VOID_ROWS (SOMVR_VOID_WAVES * SOMVR_VOID_LEVELS / SOMVR_VOID_COLS)
#define SOMVR_VOID_IMG_W (SOMVR_VOID_COLS * SOMVR_VOID_LIGHT_W)
#define SOMVR_VOID_IMG_H (SOMVR_VOID_FLOOR_PX + SOMVR_VOID_ROWS * SOMVR_VOID_LIGHT_H)
_Static_assert(SOMVR_VOID_WAVES * SOMVR_VOID_LEVELS % SOMVR_VOID_COLS == 0, "whole rows of lights frames");
_Static_assert(SOMVR_VOID_IMG_W >= SOMVR_VOID_FLOOR_PX, "the floor's cell fits");
/* the whole image (SOMVR_VOID_IMG_W x _H x 4 bytes, rows top down): RGBA (bgra: BGRA), sRGB-encoded (srgb) or linear
 * values, premultiplied alpha */
void somvr_void_image(uint8_t *px, int bgra, int srgb);
/* the floor's colour at (u, v) in its cell (0..1 each; the centre 0.5, 0.5), linear premultiplied rgba */
void somvr_void_floor_texel(float u, float v, float out[4]);
/* the lights frame (wave 0..WAVES-1, level 1..LEVELS) at (u, v) in its cell, linear premultiplied rgba */
void somvr_void_light_texel(int wave, int level, float u, float v, float out[4]);
/* a light's brightness (0..1) in wave frame `wave`: a soft crest running left to right over the 12 */
float somvr_void_light_on(int wave, int i);
/* sub-image rects {x, y, w, h}: the floor (a 1-texel inset), a lights frame */
void somvr_void_floor_rect(int32_t rc[4]);
void somvr_void_lights_rect(int wave, int level, int32_t rc[4]);

/* ---- time, level, places ------------------------------------------------------------------------------------------ */
#define SOMVR_VOID_WAVE_S 2.4f   /* the wave's period */
#define SOMVR_VOID_RISE_S 0.3f   /* the lights' level: up while the game is away */
#define SOMVR_VOID_FALL_S 1.5f   /* ... down once it presents again */
#define SOMVR_VOID_NLIGHTS 12
#define SOMVR_VOID_LIGHTS_W_M 1.6f /* the lights' quad: 1.6 x 0.3 m, its normal toward the eyes */
#define SOMVR_VOID_LIGHTS_H_M 0.3f
#define SOMVR_VOID_LIGHTS_AHEAD_M 2.2f
#define SOMVR_VOID_LIGHTS_LIFT_M 0.3f
#define SOMVR_VOID_FLOOR_M 6.0f    /* the floor disc's quad: 6 x 6 m (solid to 1.8 m, gone at 3 m) */
#define SOMVR_VOID_THEATRE_M 5.0f   /* SOMVR_XR_THEATRE: a video's screen, 5 m wide 5 m ahead (design-vr.md 6) */
#define SOMVR_VOID_WINDOW_M 3.0f    /* ... the cinematic window's, 3 m at 3 m */
#define SOMVR_VOID_LOAD_M 2.4f      /* SOMVR_XR_LOADSPACE: a loading screen's panel, 2.4 m wide 2.4 m ahead (53 deg) */
#define SOMVR_VOID_EYE_M 1.6f      /* the floor below LOCAL's origin when the real floor is not known (as the env box) */
/* the wave frame at time t (seconds, any origin) */
int somvr_void_wave(double t);
/* the lights' level (0..1) after dt seconds toward on (1) or off (0) */
float somvr_void_level_step(float level, int on, float dt);
/* the level's cell: 0 = nothing drawn, 1..LEVELS */
int somvr_void_level_index(float level);
/* the floor disc: flat (its +Z up), centred under head (x, z) at floor_y */
void somvr_void_floor_pose(const float head[3], float floor_y, somvr_posef *out);
/* the lights: AHEAD_M ahead of head along yaw (radians, + right), LIFT_M above floor_y, tilted so its normal points at
 * the eye height (head's y) */
void somvr_void_lights_pose(const float head[3], float yaw, float floor_y, somvr_posef *out);
/* the yaw (radians, + right) a quaternion (x, y, z, w) looks along; 0 straight ahead (-z) */
float somvr_void_yaw(const float q[4]);

/* ---- the game's in-world loading cover (SOMVR_XR_COVER=on; docs/re-xr-pipeline.md 36.7) ------------------------- */
/* After a fast travel (4-6 s) and a far respawn (40-60 s) the game draws a full-screen opaque black cover with a small
 * spinner on its UI surface while the session says `ctx=world paused=0` (re-xr-pipeline.md 37). The layer reads a
 * 16 x 9 grid of the HUD image's texels each stereo frame (a copy to a host buffer, read when its submit is done): the
 * HUD (the band, the wrist minimap) is hidden as soon as most of the grid is dark, and a cover held SOMVR_XR_COVER_MS
 * is a load - the loading space with the progress lights - until the game lifts it. */
#define SOMVR_COVER_NX 16
#define SOMVR_COVER_NY 9
#define SOMVR_COVER_N (SOMVR_COVER_NX * SOMVR_COVER_NY)
#define SOMVR_COVER_MS 300         /* SOMVR_XR_COVER_MS: a cover held this long is a load (0..5000) */
#define SOMVR_COVER_BLACK_ON 0.75f /* a reading is the cover: this share of the grid opaque black */
#define SOMVR_COVER_LIFT 0.5f      /* the load is over: the opaque black share below this (the game lifts its cover) */
#define SOMVR_COVER_DARK_ON 0.5f   /* the HUD hidden: this share of the grid dark (black at alpha 40 or more) */
#define SOMVR_COVER_DARK_OFF 0.2f  /* ... shown again below this */
#define SOMVR_COVER_STALE_S 1.0f   /* no reading this long (no HUD, menus, mono frames): all off */
#define SOMVR_COVER_AWAIT_S 1.5f   /* after a mono stretch the space waits for a clear reading at most this long */
/* the k-th grid point (0 .. N-1, row by row) of a w x h image: the centres of a 16 x 9 grid of cells */
void somvr_cover_point(int k, uint32_t w, uint32_t h, int32_t *x, int32_t *y);
/* n texels of 4 bytes, 8 bits a channel, alpha the 4th byte (RGBA or BGRA, straight or premultiplied: black either
 * way) -> the shares of opaque black (alpha >= 200, every colour <= 40) and of dark (alpha >= 40, colours <= 40) */
void somvr_cover_measure(const uint8_t *px, int n, float *black, float *dark);
/* ... the share of pure black (alpha >= 250, every colour <= 8): a game menu's HUD is the cover only when nearly all
 * of it is (SOMVR_COVER_MENU_PURE; run 011448: the map's board showed the cover with its spinner for the 1.5 s between X
 * and the pause's end; a menu screen's own dark background is never pure black over 95 % of the grid) */
float somvr_cover_pure(const uint8_t *px, int n);
#define SOMVR_COVER_MENU_PURE 0.95f
typedef struct {
    int hide;       /* the HUD quad, the band and the wrist minimap are not shown */
    int space;      /* a load: the loading space instead of the world */
    double t_black; /* the first reading of this run of black ones (< 0: none) */
    double t_last;  /* the last reading (< 0: none) */
    float black, dark; /* the last reading's shares */
    uint32_t n_hide, n_space; /* how many times each began */
} somvr_cover_state;
void somvr_cover_reset(somvr_cover_state *s);
enum { SOMVR_COVER_CH_HIDE = 1, SOMVR_COVER_CH_SPACE = 2 };
/* one reading at t (seconds): its shares; menu = the HUD shows a game menu or a video (never a cover: all off);
 * on_s = SOMVR_XR_COVER_MS in seconds. Returns SOMVR_COVER_CH_* of what changed. */
int somvr_cover_step(somvr_cover_state *s, double t, float black, float dark, int menu, float on_s);
/* no reading since t_last for SOMVR_COVER_STALE_S at t: all off (returns what changed) */
int somvr_cover_age(somvr_cover_state *s, double t);

/* ---- the in-world menus' backdrop (SOMVR_XR_MENU_BACK=on; docs/re-xr-pipeline.md 36.8) -------------------------- */
/* The pause menu, the map and their pages show on SOMVR_XR_MENU's board over the live world (stereo frames): over a
 * bright place (a forge tower's white marble) its text is unreadable. Behind the board a dark panel, the board's size
 * and SOMVR_BACK_MARGIN_M more on each side, its rim soft over SOMVR_BACK_SOFT_M with rounded corners; under it the
 * world dimmed (the fade's black ramp, head-locked, SOMVR_XR_MENU_BACK_DIM). Both rise over SOMVR_BACK_RISE_S when a
 * menu opens and fall over SOMVR_BACK_FALL_S after it. One static image: SOMVR_BACK_LEVELS cells of the panel side by
 * side, cell k at k / LEVELS of the full alpha (a frame picks its cell by sub-image rect). */
#define SOMVR_BACK_CELL_W 192
#define SOMVR_BACK_CELL_H 120
#define SOMVR_BACK_LEVELS 8
#define SOMVR_BACK_IMG_W (SOMVR_BACK_CELL_W * SOMVR_BACK_LEVELS)
#define SOMVR_BACK_IMG_H SOMVR_BACK_CELL_H
#define SOMVR_BACK_MARGIN_M 0.06f /* beyond the board on each side */
#define SOMVR_BACK_SOFT_M 0.05f   /* the rim's soft width (inside the margin: the board's edge is on the solid part) */
#define SOMVR_BACK_RADIUS_M 0.075f /* the corners' radius */
#define SOMVR_BACK_ALPHA 0.9f     /* the panel's alpha at the full level */
#define SOMVR_BACK_BEHIND_M 0.01f /* behind the board along its normal */
#define SOMVR_BACK_DIM 0.4f       /* SOMVR_XR_MENU_BACK_DIM: the world's dimming at the full level (0..0.9) */
#define SOMVR_BACK_RISE_S 0.12f
#define SOMVR_BACK_FALL_S 0.15f
/* the whole image (SOMVR_BACK_IMG_W x _H x 4 bytes) for a panel w_m x h_m metres: RGBA (bgra: BGRA), sRGB-encoded
 * (srgb) or linear, premultiplied */
void somvr_back_image(uint8_t *px, int bgra, int srgb, float w_m, float h_m);
/* the panel's alpha (0 .. SOMVR_BACK_ALPHA) at (x, y) metres from its centre, a panel w_m x h_m */
float somvr_back_alpha(float x, float y, float w_m, float h_m);
/* cell k's sub-image rect {x, y, w, h} (k 1..LEVELS; a 1-texel inset); the level (0..1) -> k (0 = nothing) */
void somvr_back_rect(int k, int32_t rc[4]);
int somvr_back_index(float level);
/* the level after dt seconds toward on/off (a hitch does not jump: dt at most 0.1 s) */
float somvr_back_level_step(float level, int on, float dt);
/* the panel's pose and size from the board's (pose: orientation x y z w, its +Z toward the eyes; size w x h) */
void somvr_back_place(const somvr_posef *board, const float bsize[2], somvr_posef *out, float size[2]);
/* SOMVR_XR_MENU_BACK_DIM: 0..0.9; unset/empty = def; anything else = def and *bad = 1 */
float somvr_back_parse_dim(const char *v, float def, int *bad);

#endif
