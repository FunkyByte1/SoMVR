/* somvr_logic.h - the pure logic of VK_LAYER_SOMVR_openxr: no Vulkan or OpenXR calls, no globals.
 * Unit-tested by tests/test_logic.c; used by somvr_vklayer.c. */
#ifndef SOMVR_LOGIC_H
#define SOMVR_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

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

/* ---- geometry -------------------------------------------------------------------------------------------------- */
/* Eye i's rect {x, y, w, h} inside the double-wide (2w x h) swapchain made from a w x h game image. */
void somvr_eye_rect(uint32_t w, uint32_t h, int eye, int32_t rect[4]);
/* Quad layer size in metres for a w x h image that is width_m wide. */
void somvr_quad_size(uint32_t w, uint32_t h, float width_m, float out[2]);

#endif
