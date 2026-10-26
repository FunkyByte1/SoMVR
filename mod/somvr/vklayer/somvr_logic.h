/* somvr_logic.h - the pure logic of VK_LAYER_SOMVR_openxr: no Vulkan or OpenXR calls, no globals.
 * Unit-tested by tests/test_logic.c; used by somvr_vklayer.c. */
#ifndef SOMVR_LOGIC_H
#define SOMVR_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "../somvr_hud.h"
#include "../somvr_input.h"
#include "../somvr_shared.h"

/* ---- extension lists ---------------------------------------------------------------------------------------- */
#define SOMVR_EXTL_MAX 128
typedef struct {
    const char *names[SOMVR_EXTL_MAX]; /* the app's pointers first, then pointers into buf */
    uint32_t count;
    char buf[8192];                    /* copies of names taken from the runtime's space-separated strings */
    size_t used;
} somvr_extlist;

/* Start from the app's list (pointers are kept, not copied). -1 if it does not fit. */
int somvr_extl_init(somvr_extlist *l, const char *const *names, uint32_t n);
int somvr_extl_has(const somvr_extlist *l, const char *name);
/* Append every name of a space-separated list that is not in l yet. Returns how many were added, -1 on overflow
 * (l is then unusable: the caller drops it and falls back to the app's list). */
int somvr_extl_add_spaced(somvr_extlist *l, const char *spaced);
/* First name of a space-separated list that is missing from props[0..n), copied into out; NULL if all are there. */
const char *somvr_first_unsupported(const char *spaced, const VkExtensionProperties *props, uint32_t n, char *out,
                                    size_t outsz);

/* ---- swapchain format for the OpenXR copy -------------------------------------------------------------------- */
typedef enum { SOMVR_XFER_NONE = 0, SOMVR_XFER_COPY = 1, SOMVR_XFER_BLIT = 2 } somvr_xfer;
/* The game's bytes are display-encoded (sRGB) whether its swapchain format is _UNORM or _SRGB. Best: an _SRGB
 * runtime format with the same channel order and a raw vkCmdCopyImage. Otherwise a blit; *gamma_ok says whether the
 * runtime will show the right brightness. Only 8-bit RGBA/BGRA game formats are handled (else NONE). */
somvr_xfer somvr_pick_format(VkFormat game, const int64_t *offered, uint32_t n, VkFormat *out, int *gamma_ok);

/* ---- OpenXR session state ------------------------------------------------------------------------------------ */
typedef enum { SOMVR_SA_NONE = 0, SOMVR_SA_BEGIN, SOMVR_SA_END, SOMVR_SA_LOST } somvr_session_action;
/* What to do on XrEventDataSessionStateChanged (state is an XrSessionState). */
somvr_session_action somvr_on_session_state(int state, int running);

/* ---- config ---------------------------------------------------------------------------------------------------- */
enum { SOMVR_MODE_OFF = 0, SOMVR_MODE_AUTO = 1, SOMVR_MODE_FORCE = 2 };
/* SOMVR_XR: "0"/"off" = pass-through; unset/"1"/"auto" = XR only for the engine "Feral3D"; "force" = any app. */
int somvr_parse_mode(const char *v);
enum { SOMVR_LAYER_QUAD = 0, SOMVR_LAYER_PROJ = 1 };
int somvr_parse_layer(const char *v); /* SOMVR_XR_LAYER: "proj" or default "quad" */

int somvr_parse_space(const char *v); /* SOMVR_XR_SPACE: "stage" -> 1 (STAGE), default 0 (LOCAL) */

/* ---- SOMVR_XR_DUMP ------------------------------------------------------------------------------------------- */
#define SOMVR_DUMP_MAX 8
#define SOMVR_DUMP_MONO (1ull << 63) /* at[i] flag: "mN" = the N-th mono frame (the quad), not the N-th stereo submit */
#define SOMVR_DUMP_SC (1ull << 62)   /* at[i] flag: "sN" = the first frame (stereo or mono) of the N-th XR swapchain the
                                        layer made (1 = the first): shows every size change (SOMVR_EYE_SIZE's switch) */
/* "N[,M,...]": at most SOMVR_DUMP_MAX positive numbers (up to 12 digits), each optionally prefixed with "m" (a mono
 * frame: at[i] = N | SOMVR_DUMP_MONO) or "s" (a swapchain's first frame: N | SOMVR_DUMP_SC), into at[]. Returns how
 * many; 0 = unset or empty; -1 = anything else (a 0, a blank, an empty element, a lone "m"/"s", one number too many). */
int somvr_parse_dump(const char *v, uint64_t at[SOMVR_DUMP_MAX]);
/* The byte order of an 8-bit XR swapchain format: 0 = RGBA, 1 = BGRA, -1 = not one of the four; *srgb = _SRGB */
int somvr_dump_order(VkFormat f, int *srgb);
/* n pixels of 4 bytes -> n RGB triplets (PPM P6): alpha dropped, BGRA swapped, the bytes as they are (no gamma) */
void somvr_dump_row(const uint8_t *src, uint8_t *dst, uint32_t n, int bgra);
/* The memory type of a buffer the host reads back: HOST_VISIBLE | HOST_COHERENT among `bits`; best also HOST_CACHED
 * and not DEVICE_LOCAL (fast reads, no VRAM); ties = the lowest index. -1 = none. */
int somvr_pick_host_memory(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits);

/* ---- geometry -------------------------------------------------------------------------------------------------- */
/* Slot s's rect {x, y, w, h} in the XR swapchain (nslots x w wide) made from w x h game images: slot s at x = s*w. */
void somvr_eye_rect(uint32_t w, uint32_t h, int slot, int32_t rect[4]);
/* An eye's subImage rect {x, y, w, h}: its viewport rect (x0 y0 x1 y1 in 0..1) inside its slot, rounded like the
 * game's own (int)(f * size + 0.5) (FUN_0072c400). */
void somvr_eye_pixel_rect(uint32_t w, uint32_t h, uint32_t slot, const float rect[4], int32_t out[4]);
/* A mono frame in a multi-slot swapchain (layout B's menus: the game image in slot 0 of a 2w-wide image) leaves the
 * texels right of the slot undefined, and a compositor's bilinear sample at the quad's (or view's) right edge reads the
 * neighbouring column. The layer copies the slot's last column into the next SOMVR_GUARD_COLS columns: how many fit
 * right of slot `slot` (0 = none needed: the slot ends at the image's edge). */
#define SOMVR_GUARD_COLS 4u
uint32_t somvr_guard_cols(uint32_t sc_w, uint32_t w, uint32_t slot);

/* ---- prediction lead (docs/re-xr-pipeline.md 7.1) ------------------------------------------------------------- */
/* The game thread locates a frame's views ~2 frames before the present that submits them: the views are for the
 * predictedDisplayTime of the latest xrWaitFrame, the frame ends with a later one. The layer publishes
 * `predictedDisplayTime + lead` (somvr_shared xr.predicted_time) and measures the lead from the records it submits:
 * raw = the submitting frame's predictedDisplayTime - the xrWaitFrame time that record's time was published from. */
enum { SOMVR_PREDICT_OFF = 0, SOMVR_PREDICT_AUTO = 1, SOMVR_PREDICT_FIXED = 2 };
/* SOMVR_XR_PREDICT: unset/"auto" = AUTO; "off"/"0" = OFF (the wait's own time, as before); a number of ms in
 * (0, 250] = FIXED (*fixed_ns). Returns the mode; anything else = AUTO and *bad = 1. */
int somvr_parse_predict(const char *v, int64_t *fixed_ns, int *bad);
#define SOMVR_LEAD_RING 16             /* published times remembered (a record older than that is not measured) */
#define SOMVR_LEAD_WIN 9               /* the estimate = the median of the last 9 measurements */
#define SOMVR_LEAD_MAX_NS 250000000ll  /* a raw lead outside 0..250 ms is not used (rejected) */
typedef struct {
    struct { int64_t pub, wait; } ring[SOMVR_LEAD_RING];
    uint32_t npub;                     /* published so far; ring[npub % SOMVR_LEAD_RING] is the next */
    int64_t win[SOMVR_LEAD_WIN];       /* the last measurements (win[nwin % SOMVR_LEAD_WIN] is the next) */
    uint32_t nwin;                     /* measurements so far */
    int64_t lead;                      /* the estimate (ns); 0 before the first measurement */
    uint64_t matched, missed, rejected;
} somvr_lead;
void somvr_lead_init(somvr_lead *l);
/* what to publish for an xrWaitFrame's predictedDisplayTime t_wait: + the estimate (AUTO), + fixed_ns (FIXED), + 0
 * (OFF); remembered for somvr_lead_measure */
int64_t somvr_lead_publish(somvr_lead *l, int mode, int64_t fixed_ns, int64_t t_wait);
/* A stereo record whose views were located for display_time is submitted in a frame with predictedDisplayTime t_frame.
 * 1 = measured (display_time is one of the remembered publishes, *raw = t_frame - its wait time, the estimate updated);
 * 0 = not one of ours (an extrapolated or clamped time, fake views, too old: *raw = 0); -1 = *raw outside 0..250 ms
 * (not used). */
int somvr_lead_measure(somvr_lead *l, int64_t display_time, int64_t t_frame, int64_t *raw);

/* ---- stereo submit (docs/re-xr-pipeline.md 4.7) --------------------------------------------------------------- */
typedef enum { SOMVR_SUBMIT_MONO = 0, SOMVR_SUBMIT_STEREO = 1, SOMVR_SUBMIT_REUSE = 2 } somvr_submit_kind;
/* have_tag: this present has a tag; tag_frame: its record (0 = mono); have_rec: that record is complete and fits;
 * last_stereo: the XR swapchain's last released image is a stereo frame whose layer was kept */
somvr_submit_kind somvr_submit_decide(int have_tag, uint64_t tag_frame, int have_rec, int last_stereo);
/* the fov eye e was rendered with: exact when H3 applied it (applied bit e), else the covering symmetric one */
somvr_fovf somvr_submit_fov(const somvr_eye_rec *e, uint32_t applied, int eye);
/* Quad layer size in metres for a w x h image that is width_m wide. */
void somvr_quad_size(uint32_t w, uint32_t h, float width_m, float out[2]);

/* ---- the HUD quad (H11, docs/re-xr-pipeline.md 8): the preload's HUD image (somvr_hud) as a second layer ----------- */
/* SOMVR_XR_HUD: unset/"world" = WORLD (default: placed once in front of the head, level, then fixed in the room, again
 * after a recentre; docs/design-vr.md D6 "nothing follows the head"); "head" = HEAD (head-locked, in a VIEW space);
 * "off"/"0" = OFF (no quad even when the preload names a HUD image). Anything else = WORLD and *bad = 1 */
enum { SOMVR_HUD_OFF = 0, SOMVR_HUD_WORLD = 1, SOMVR_HUD_HEAD = 2 };
int somvr_parse_hud(const char *v, int *bad);
/* SOMVR_XR_HUD_ALPHA: unset/"straight" = STRAIGHT (the quad's flags get UNPREMULTIPLIED_ALPHA) or "premul" (only
 * BLEND_TEXTURE_SOURCE_ALPHA); RT U2 decides. Anything else = STRAIGHT and *bad = 1 */
enum { SOMVR_HUD_STRAIGHT = 0, SOMVR_HUD_PREMUL = 1 };
int somvr_parse_hud_alpha(const char *v, int *bad);
/* SOMVR_XR_HUD_QUAD=DIST,WIDTH,DROP in metres (DIST 0.3..10 ahead, WIDTH 0.1..10, DROP -2..2 below eye level) -> out:
 * dist, width, drop. Unset = the defaults below; anything else = the defaults and *bad = 1 */
#define SOMVR_HUD_DIST_M 1.5f
#define SOMVR_HUD_WIDTH_M 1.2f
#define SOMVR_HUD_DROP_M 0.1f
void somvr_parse_hud_quad(const char *v, float out[3], int *bad);
/* The world-locked quad's pose in the reference space from a frame's two eye poses: `dist` ahead of the eyes' midpoint
 * in the direction the head faces (its forward levelled: yaw only; looking straight up or down: the head's up vector
 * decides), `drop` below it, upright and facing the head. */
void somvr_hud_world_pose(const somvr_posef eye[2], float dist, float drop, somvr_posef *out);

/* ---- OpenXR input (docs/re-xr-input.md 1.2-1.6) ------------------------------------------------------------- */
/* SOMVR_XR_INPUT: "off"/"0" = no action ever; unset/"on"/"1" = with an active somvr_input that wants STATE; "log" =
 * also without one (log only; R0, vkcube); anything else = off */
enum { SOMVR_INPUT_OFF = 0, SOMVR_INPUT_ON = 1, SOMVR_INPUT_LOG = 2 };
int somvr_parse_input(const char *v);
/* the actions of the set "somvr", in this order */
enum {
    SOMVR_ACT_GRIP = 0, SOMVR_ACT_AIM, SOMVR_ACT_TRIGGER, SOMVR_ACT_SQUEEZE, SOMVR_ACT_STICK, SOMVR_ACT_STICK_CLICK,
    SOMVR_ACT_LOWER, SOMVR_ACT_UPPER, SOMVR_ACT_MENU, SOMVR_ACT_TRIGGER_TOUCH, SOMVR_ACT_THUMB_TOUCH, SOMVR_ACT_HAPTIC,
    SOMVR_NACT
};
typedef struct {
    const char *name, *loc; /* actionName, localizedActionName */
    int type;               /* XrActionType */
    uint8_t hands;          /* subaction paths: bit h = /user/hand/left (0), right (1) */
    uint32_t button;        /* BOOLEAN actions: the SOMVR_BTN_* bit */
} somvr_action_def;
extern const somvr_action_def somvr_actions[SOMVR_NACT];
typedef struct {
    uint8_t act, hand;      /* SOMVR_ACT_*, SOMVR_HAND_* */
    const char *path;       /* after /user/hand/<left|right>/ */
} somvr_binding_def;
#define SOMVR_NPROF 3
typedef struct {
    int prof;               /* SOMVR_PROF_* */
    const char *name;       /* short name for logs: touch, index, simple */
    const char *path;       /* /interaction_profiles/... */
    const somvr_binding_def *b;
    uint32_t n;
} somvr_profile_def;
extern const somvr_profile_def somvr_profiles[SOMVR_NPROF]; /* suggestion order: touch, index, simple */
/* an interaction profile path -> SOMVR_PROF_*: NULL or "" = NONE, one of ours, else OTHER */
int somvr_prof_from_path(const char *path);
/* the press counters: one rising edge per bit that is held now and was not held at the previous sync. Returns now. */
uint32_t somvr_in_merge(uint8_t presses[SOMVR_NBTN], uint32_t prev, uint32_t now);
/* what a new haptic request becomes: STOP (amplitude 0, negative or NaN), or APPLY with the amplitude clamped to 1,
 * the frequency (<= 0 or NaN = XR_FREQUENCY_UNSPECIFIED) and the duration (<= 0 = XR_MIN_HAPTIC_DURATION) */
typedef enum { SOMVR_HAPD_STOP = 0, SOMVR_HAPD_APPLY = 1 } somvr_hap_decision;
somvr_hap_decision somvr_hap_decide(const somvr_haptic_req *req, float *amp, float *freq, int64_t *dur_ns);

#endif
