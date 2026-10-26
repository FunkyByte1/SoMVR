/* somvr_menus.h - the game's menus in VR, the wrist minimap, the controls card's place and touch (build 6,
 * docs/re-xr-pipeline.md 28). Pure: no globals, no OpenXR/Vulkan calls; used by the Vulkan layer (the board's and the
 * watch's poses, the HUD image's copy regions, the fingertip's state machine) and tested by tests/test_menus.c.
 *
 * Conventions as somvr_pointer.h: poses in the layer's XR space (LOCAL or STAGE, +Y up, a head looking along -Z at
 * rest), XrPosef layout (somvr_posef), metres; a quad is seen from its +Z side, uv (0,0) its top left corner.
 *
 * 1. The game's menus (SOMVR_XR_MENU=front): the menu quad (mono frames: the front end, loading, modal screens) and
 *    the HUD quad while a game menu is open over the world (the pause menu, the map, the skills: the HUD image) are
 *    shown on a "board" placed at each open in front of the head (its yaw, eye height minus a drop), tilted to face the
 *    eyes, and kept there while it stays open: within reach of a fingertip (HoF2's menu board: 0.55 m, 61 deg).
 * 2. The wrist minimap (SOMVR_XR_MINIMAP=wrist): the minimap (with its health ring) cut out of the HUD image into a
 *    watch face on the back of the off hand's wrist (the VR menu opens on the palm side), shown while it faces the eyes.
 * 3. Touch (SOMVR_XR_TOUCH=on): a pointing index fingertip (the trigger untouched) pokes the boards, the wrist menu and
 *    the card: hover in front, a press 5 mm through the surface after a front approach, the release on withdrawal
 *    (HoF2 interaction-design.md 2.2 / 3.6.5); the point on the surface is smoothed (a One Euro filter, sideways only).
 */
#ifndef SOMVR_MENUS_H
#define SOMVR_MENUS_H

#include <stdint.h>

#include "somvr_input.h" /* somvr_posef, SOMVR_HAND_L / _R */

/* ==== 1. the menu board ========================================================================================== */
/* SOMVR_XR_MENU: unset/"off"/"0" = OFF (the menu quad 2 m ahead of the space's origin, the HUD where it was placed),
 * "front"/"on"/"1" = FRONT. Anything else = OFF and *bad = 1 */
enum { SOMVR_MN_OFF = 0, SOMVR_MN_FRONT = 1 };
int somvr_mn_parse(const char *v, int *bad);
/* SOMVR_XR_MENU_QUAD=DIST,WIDTH,DROP in metres (DIST 0.3..3 ahead, WIDTH 0.2..4, DROP -1..1 below eye level) -> out;
 * unset/empty = the defaults; a bad value = the defaults and *bad = 1. 0.72 m at 0.6 m = 62 deg wide, 36 deg high:
 * 18-pixel game text (1080 lines) is 0.6 deg, a fingertip reaches it */
#define SOMVR_MN_DIST_M 0.60f
#define SOMVR_MN_WIDTH_M 0.72f
#define SOMVR_MN_DROP_M 0.10f
void somvr_mn_parse_quad(const char *v, float out[3], int *bad);
/* The board's pose from two eye poses: `dist` ahead of their midpoint along the head's levelled forward (straight up or
 * down: the head's up vector decides, as somvr_hud_world_pose), `drop` below it, upright about the world's up, then
 * tilted back about its own X so that its +Z points at the eyes' midpoint. NaN poses: straight ahead along -Z. */
void somvr_mn_board_pose(const somvr_posef eye[2], float dist, float drop, somvr_posef *out);

/* ==== 2. the wrist minimap ======================================================================================= */
/* SOMVR_XR_MINIMAP: unset/"off"/"0" = OFF (the minimap stays in the HUD quad), "wrist"/"on"/"1" = WRIST; anything else
 * = OFF and *bad = 1 */
enum { SOMVR_MM_OFF = 0, SOMVR_MM_WRIST = 1 };
int somvr_mm_parse(const char *v, int *bad);
/* SOMVR_XR_MINIMAP_RECT=X,Y,W,H: the minimap with its rings in a 1920x1080 UI image (pixels; scaled to another size by
 * somvr_mm_rect_for); unset = the default (the bottom-left corner, run 15's HUD dump), a bad value = it and *bad = 1 */
#define SOMVR_MM_RECT_DEFAULT {28, 770, 300, 310}
void somvr_mm_parse_rect(const char *v, int out[4], int *bad);
/* the rect for a w x h HUD image (scaled from 1920x1080, rounded, clamped inside; at least 1 x 1); 0 = no usable rect */
int somvr_mm_rect_for(const int r1080[4], uint32_t w, uint32_t h, int out[4]);
/* (the copy into the HUD swapchain image, the minimap to (w, 0) of the extra area: somvr_hud_split, 2b) */
typedef struct {
    float size_m;    /* the watch face: its width (its height from the rect's aspect); 0.10 */
    float back_m;    /* its centre behind the grip toward the elbow (the grip's +Z): 0.08 */
    float out_m;     /* ... and off the back of the hand (minus the palm's normal): 0.045 */
    float face_on;   /* cos: shown once its face turns toward the eyes within acos(face_on) (0.40 = 66 deg) */
    float face_off;  /* cos: hidden again beyond acos(face_off) (0.20 = 78 deg) */
} somvr_mm_cfg;
void somvr_mm_cfg_default(somvr_mm_cfg *c);
/* SOMVR_XR_MINIMAP_QUAD=SIZE,BACK,OUT in metres (0.03..0.3, -0.2..0.3, -0.1..0.2); a bad value = the defaults, *bad */
void somvr_mm_parse_quad(const char *v, somvr_mm_cfg *c, int *bad);
/* The watch face on hand h's wrist (h = the off hand): facing out of the back of the hand (the left grip's -X, the
 * right grip's +X), its image's right edge toward the fingers (left hand) / the elbow (right hand) and its top away
 * from the thumb, so a forearm held across the chest, palm down, reads it like a watch with its top away from the
 * body. 0 = no pose (a bad grip quaternion). */
int somvr_mm_pose(const somvr_posef *grip, int hand, const somvr_mm_cfg *c, somvr_posef *out);
/* The face toward the eye: 1 shown, 0 hidden, with hysteresis (was = the last answer) */
int somvr_mm_facing(const somvr_posef *face, const float eye[3], const somvr_mm_cfg *c, int was);

/* ==== 2b. the HUD's tiles: the wrist HUD and the prompt band (build 8, docs/re-xr-pipeline.md 31) ================= */
/* The health ring, the focus arc and the arrow count are parts of the minimap (its rect holds them: they went to the
 * wrist with it in build 6). More of the HUD image is cut out of the head-level HUD the same way, into the HUD
 * swapchain's extra area (right of the HUD image), and shown elsewhere:
 *   SOMVR_XR_WRIST_HUD=on   the hit streak and the target panel join the minimap on the watch: a column beside the map
 *                           toward the elbow (needs SOMVR_XR_MINIMAP=wrist): a gauntlet. The target panel's backdrop
 *                           left of its tile is cleared from the head-level HUD (SOMVR_XR_WRIST_CLEAR);
 *   SOMVR_XR_PROMPTS=on     the banners (place names, the top middle), the hints and notifications (the middle of the
 *                           lower half), the XP gains (left) and the button prompts (lower right) gathered into one band
 *                           in three rows, shown larger than the HUD (an 18-px line at 0.85 deg instead of 0.42) below
 *                           the view's middle, placed with the HUD quad. */
#define SOMVR_HT_MAX 6 /* tiles per set */
#define SOMVR_HT_GAP 8 /* px between the parts of the extra area (cleared: transparent) */
/* SOMVR_XR_WRIST_HUD / SOMVR_XR_PROMPTS: unset/"off"/"0" = 0, "on"/"1" = 1; anything else = 0 and *bad = 1 */
int somvr_wh_parse(const char *v, int *bad);
int somvr_pb_parse(const char *v, int *bad);
/* the rects (1920x1080 pixels, measured in the HUD dumps of logs/boot-check-20261024-033424, -035653, -041645): the watch's
 * column top to bottom; what is cleared with it; the band's rows ('|') of tiles (';') */
#define SOMVR_WH_RECTS_DEFAULT "0,214,260,96;1540,14,380,104" /* the hit streak, the target panel */
#define SOMVR_WH_CLEAR_DEFAULT "1400,14,140,104"              /* the target panel's backdrop left of it (9% dark) */
/* the band: the banners | the hints | the XP gains; the button prompts */
#define SOMVR_PB_RECTS_DEFAULT "640,10,640,110|440,600,1040,180|30,390,290,190;1120,860,800,220"
typedef struct {
    int n;                   /* tiles */
    int r[SOMVR_HT_MAX][4];  /* rects: 1920x1080 pixels (parsed), the image's pixels (somvr_ht_scale) */
    int row[SOMVR_HT_MAX];   /* the band's row of each (0 = the top; the watch's column ignores it) */
} somvr_ht_set;
/* "X,Y,W,H;X,Y,W,H|X,Y,W,H..." (SOMVR_XR_WRIST_RECTS, _WRIST_CLEAR, SOMVR_XR_PROMPTS_RECTS): 1..SOMVR_HT_MAX rects inside
 * 1920x1080, each at least 8 x 8, ';' between the tiles of a row, '|' between rows -> out; returns how many. Unset/empty
 * = def; a bad value = def and *bad = 1 */
int somvr_ht_parse(const char *v, const char *def, somvr_ht_set *out, int *bad);
/* the set for a w x h HUD image (each rect as somvr_mm_rect_for); 0 = a rect is unusable (out->n = 0) */
int somvr_ht_scale(const somvr_ht_set *in, uint32_t w, uint32_t h, somvr_ht_set *out);
typedef struct {
    float dist_m;   /* the band's distance from the eyes (1.3 m; it scales with the VR menu's HUD distance) */
    float line_deg; /* an 18-px line of the HUD image in it, on average over its width: 0.85 deg (0.91 in its middle,
                       0.75 at its ends; the HUD quad: 0.42) */
    float down_deg; /* its middle below the eyes' level (15 deg: the banners at ~6, the hints at ~13, the XP and the
                       prompts at ~23) */
} somvr_pb_cfg;
void somvr_pb_cfg_default(somvr_pb_cfg *c);
/* SOMVR_XR_PROMPTS_QUAD=DIST,LINE_DEG,DOWN_DEG (0.4..3 m, 0.3..2 deg, -30..50 deg); a bad value = the defaults, *bad */
void somvr_pb_parse_quad(const char *v, somvr_pb_cfg *c, int *bad);
/* the band's width (m) for a texture w_px wide at line_deg per 18 px, dist away (flat: 2 dist tan(half the angle)) */
float somvr_pb_width(int w_px, float line_deg, float dist);
/* The HUD swapchain image's layout for a w x h HUD image: the image at (0, 0), the extra area right of it: the watch
 * (the minimap, its column toward the elbow: left of the map for the left wrist, right of it with mirror), below it the
 * band (beside it when the height does not allow): its rows top to bottom, a row's one tile centred, two or more spread
 * from its left end to its right end. */
#define SOMVR_HL_MAX (1 + 2 * SOMVR_HT_MAX)
typedef struct {
    int ntile;
    int src[SOMVR_HL_MAX][4]; /* each tile's rect in the HUD image */
    int dst[SOMVR_HL_MAX][2]; /* ... its place in the swapchain image */
    int nclear;
    int clear[SOMVR_HT_MAX][4]; /* more rects of the HUD image left out of the head-level HUD (shown nowhere) */
    int watch[4];             /* the watch's region in the swapchain image (w 0: none) */
    int mm[4];                /* the minimap's place (inside watch) */
    int band[4];              /* the band's region (w 0: none) */
    uint32_t extra_w;         /* the extra area's width (0: no tiles) */
} somvr_hud_layout;
/* mm: the minimap's rect in the HUD image (NULL: none); wt: the watch's column and clear what goes with it (both need
 * mm; NULL: none); bt: the band's tiles (NULL: none); all in this image's pixels. Returns 0 when nothing fits (no tiles
 * then). */
int somvr_hud_layout_make(uint32_t w, uint32_t h, const int *mm, const somvr_ht_set *wt, int mirror,
                          const somvr_ht_set *clear, const somvr_ht_set *bt, somvr_hud_layout *out);
/* The copy into the cleared swapchain image: every pixel of the HUD image outside the tiles and the clear rects to the
 * same place (bands between their edges, merged downwards), then each tile to its place. out[i] = {sx, sy, dx, dy,
 * width, height}; returns how many, 0 if cap is too small (build 6's minimap alone: at most 4 rects and its copy). */
#define SOMVR_HUD_SPLIT_MAX 160
int somvr_hud_split(uint32_t w, uint32_t h, const somvr_hud_layout *l, int32_t out[][6], int cap);

/* ==== 3. touch ===================================================================================================== */
/* SOMVR_XR_TOUCH: unset/"off"/"0" = OFF, "on"/"1" = ON (the laser stays); anything else = OFF and *bad = 1 */
enum { SOMVR_TC_OFF = 0, SOMVR_TC_ON = 1 };
int somvr_tc_parse(const char *v, int *bad);
typedef struct {
    float reach_m;      /* the fingertip: this far ahead of the aim pose along its -Z (HoF2's 0.055 past the tip) 0.05 */
    float hover_m;      /* hover while the fingertip is less than this in front of the surface (3 cm) */
    float from_m;       /* ... and was at least this far in front (2 cm) within `window_s` (0.4 s): a front approach */
    float press_m;      /* a press: this far through the surface (5 mm) */
    float release_m;    /* the release (and re-arm): back this far in front (1.5 cm) */
    float behind_m;     /* deeper than this behind the surface: no target (the hand went through it: 8 cm) */
    float window_s;
    float dwell_s;      /* hovered at least this long before a press (60 ms) */
    float refractory_s; /* after a release, no press for this long (0.25 s) */
    float min_in_mps;   /* a press needs at least this speed into the surface (0.05 m/s) */
    float max_tan_mps;  /* ... and at most this sideways (0.4 m/s: a swipe is no press) */
    float margin;       /* uv: the target counts inside [-margin, 1 + margin]^2 (a press latched keeps it wherever) */
    float min_cutoff;   /* One Euro on the point (metres on the surface): the cutoff at rest (1.0 Hz) */
    float beta;         /* ... growing by this per m/s (30) */
    float d_cutoff;     /* ... its speed's cutoff (1.0 Hz) */
} somvr_tc_cfg;
void somvr_tc_cfg_default(somvr_tc_cfg *c);
/* the fingertip of an aim pose: reach_m along its -Z */
void somvr_tc_tip(const somvr_posef *aim, float reach_m, float out[3]);

enum { SOMVR_TCS_IDLE = 0, SOMVR_TCS_HOVER = 1, SOMVR_TCS_DOWN = 2 };
typedef struct {
    int state;          /* SOMVR_TCS_* */
    double front_t;     /* the last time the tip was from_m or more in front of the target (-1 = never) */
    double cross_t;     /* the last time the tip was in front of the surface (-1 = never): a press must cross it within
                           window_s (resting behind it after a swipe, then pushing on, is no press) */
    double hover_t;     /* hovering since */
    double free_t;      /* no press before this (the refractory) */
    double t;           /* the last update */
    float depth;        /* the last depth (m, + in front) */
    int have_f;         /* the filter holds a value */
    float f[2], df[2];  /* the One Euro state: the point on the surface (m, from its top left) and its speed */
    float uv[2];        /* the last smoothed point */
    uint32_t quad;      /* the target of the last update (a change resets the hand) */
} somvr_tc_hand;
typedef struct {
    somvr_tc_hand h[2];
    int hand;           /* the hand that hovers or presses (-1 = none) */
    uint32_t downs, ups, cancels;
} somvr_tc_state;
void somvr_tc_reset(somvr_tc_state *s);
typedef struct {
    double t;           /* s, the frame's display time */
    int ok[2];          /* hand h may touch: tracked, pointing (the trigger untouched and released), its grip open */
    float tip[2][3];    /* the fingertips */
    uint32_t quad;      /* the target's id (0 = none: a press held ends) */
    somvr_posef quad_pose;
    float quad_size[2];
} somvr_tc_in;
enum { SOMVR_TCE_HOVER = 1u, SOMVR_TCE_LEAVE = 2u, SOMVR_TCE_PRESS = 4u, SOMVR_TCE_RELEASE = 8u, SOMVR_TCE_CANCEL = 16u };
typedef struct {
    int hand;           /* the touching hand (-1 = none) */
    int on, down;       /* a fingertip hovers the target / presses it */
    uint32_t edges;     /* SOMVR_TCE_* this frame */
    float uv[2];        /* with on: the smoothed point, clamped to [0,1] */
    float depth;        /* with on: the fingertip in front of the surface (m) */
    float tip[3];       /* with on: the fingertip; surf: its point on the surface (both XR space) */
    float surf[3];
    int near_[2];       /* hand h's tip is within hover_m in front .. behind_m behind, inside: draw its marker */
} somvr_tc_out;
/* One frame. Per hand: the depth = the tip's distance along the target's +Z (in front > 0); inside = its uv within the
 * margin. IDLE -> HOVER: inside, 0 < depth < hover_m, the tip was from_m in front within window_s. HOVER -> DOWN (PRESS,
 * once): hovered dwell_s, depth <= -press_m, the depth falling faster than min_in_mps, the point moving slower than
 * max_tan_mps, past the refractory, and the other hand not touching. DOWN -> HOVER (RELEASE): depth >= release_m; the
 * point follows the finger meanwhile (a drag, uv clamped). A hand not ok, outside, deeper than behind_m or another
 * target: back to IDLE (a held press ends: CANCEL | RELEASE). The touching hand = the pressing one, else the hoverer. */
void somvr_tc_update(somvr_tc_state *s, const somvr_tc_cfg *c, const somvr_tc_in *in, somvr_tc_out *o);

/* ==== 4. test hands for headless boots (SOMVR_XR_TEST=PATH, the layer; never in a player's build) ================= */
/* One line of the test file (scripts/menus-drive.sh writes it; the layer runs each new file's lines once, in order):
 *   dump                                   a stereo dump at the next frame (SOMVR_XR_DUMP must be on): the eyes, the
 *                                          HUD swapchain image, the VR menu's picture and the frame's quads
 *   hand L|R off                           that hand untracked again
 *   hand L|R head X Y Z [RX RY RZ] [s SEC] the grip and the aim at X right, Y up, Z ahead (cm) of the head (its yaw
 *                                          frame at this line), turned RX, RY, RZ deg about the grip's X, Y, Z (0 0 0 =
 *                                          pointing ahead); moved there in SEC seconds (default 0: at once)
 *   hand L|R touch board|panel|watch U V D [s SEC]  the fingertip (the aim + reach) at the point U, V (0..1, from the
 *                                          top left) of that quad, D cm in front of it (negative: behind), the aim
 *                                          into it; the quad's pose at this line; moved there in SEC seconds
 *   btn L|R menu|a|b|stick|trigger|squeeze V      held (V 1 / 0; trigger and squeeze 0..1)
 *   stick L|R X Y                          held (-1..1)
 * '#' and blank lines are skipped. */
enum { SOMVR_XT_NONE = 0, SOMVR_XT_DUMP, SOMVR_XT_OFF, SOMVR_XT_HEAD, SOMVR_XT_TOUCH, SOMVR_XT_BTN, SOMVR_XT_STICK };
enum { SOMVR_XTS_BOARD = 0, SOMVR_XTS_PANEL = 1, SOMVR_XTS_WATCH = 2 };
enum { SOMVR_XTB_MENU = 0, SOMVR_XTB_A, SOMVR_XTB_B, SOMVR_XTB_STICK, SOMVR_XTB_TRIGGER, SOMVR_XTB_SQUEEZE };
typedef struct {
    int op;        /* SOMVR_XT_* */
    int hand;      /* SOMVR_HAND_L / _R */
    int what;      /* touch: SOMVR_XTS_*; btn: SOMVR_XTB_* */
    float v[6];    /* head: X Y Z RX RY RZ; touch: U V D; btn: V; stick: X Y */
    float sec;     /* the move's time */
} somvr_xt_cmd;
/* 1 = a command in *c, 0 = nothing (blank, comment), -1 = not understood */
int somvr_xt_parse(const char *line, somvr_xt_cmd *c);
/* the head command's pose: head = the head (its position, its orientation's levelled forward), cm and deg as above */
void somvr_xt_head_pose(const somvr_posef *head, const float cm[3], const float deg[3], somvr_posef *out);
/* the touch command's aim pose: its -Z into the quad (the quad's orientation), its fingertip reach_m ahead at the
 * quad's point (u, v) lifted front_m along the quad's +Z */
void somvr_xt_touch_pose(const somvr_posef *quad, const float size[2], float u, float v, float front_m, float reach_m,
                         somvr_posef *out);
/* the pose k (0..1) of the way from a to b (the position linear, the orientation normalised lerp, the short way) */
void somvr_xt_lerp(const somvr_posef *a, const somvr_posef *b, float k, somvr_posef *out);

#endif
