/* somvr_fade.h - the layer's comfort fade (SOMVR_FALLBACK, docs/re-xr-pipeline.md 6.8): the preload puts how black a
 * stereo frame is shown into its record (somvr_shared.h SOMVR_REC_FADE_*, 0..255); the layer draws a black quad over
 * the frame's other layers. Header only, pure (no Vulkan or OpenXR calls); checked by tests/test_layer.c (sc_fade*).
 *
 * The quad's image is one static premultiplied black ramp: SOMVR_FADE_LEVELS alpha levels side by side, SOMVR_FADE_TEX
 * texels each. A frame's quad shows the middle 2 x 2 texels of its level's block, so bilinear sampling never reaches a
 * neighbouring level: one constant alpha over the whole quad. It is world-locked at the frame's own head (the record's
 * eye poses: the predicted head of the frame, so the compositor's reprojection keeps it in front), SOMVR_FADE_DIST_M
 * ahead, SOMVR_FADE_SIZE_M square (+-82.9 deg at 0.25 m: past any headset's fov), turned like the head. */
#ifndef SOMVR_FADE_INCLUDED
#define SOMVR_FADE_INCLUDED

#include <math.h>
#include <stdint.h>

#include "../somvr_shared.h"

#define SOMVR_FADE_LEVELS 64
#define SOMVR_FADE_TEX 4
#define SOMVR_FADE_W (SOMVR_FADE_LEVELS * SOMVR_FADE_TEX) /* 256 */
#define SOMVR_FADE_H 8   /* rows 0..3 the black ramp, rows 4..7 the dark red one (SOMVR_REC_FADE_RED) */
#define SOMVR_FADE_RED 0.42f /* the red ramp's red at full alpha (premultiplied: red = alpha x this) */
#define SOMVR_FADE_DIST_M 0.25f
#define SOMVR_FADE_SIZE_M 4.0f

/* level k's alpha byte: 0 .. 255 in 63 steps */
static inline uint8_t somvr_fade_alpha(int k) { return (uint8_t)((k * 255 + (SOMVR_FADE_LEVELS - 1) / 2) / (SOMVR_FADE_LEVELS - 1)); }

/* the ramp image, SOMVR_FADE_W x SOMVR_FADE_H, premultiplied: rows 0..3 (0, 0, 0, alpha), rows 4..7 the red ramp
 * (alpha x SOMVR_FADE_RED, 0, 0, alpha) in the channel order of the format (bgra: B8G8R8A8, else R8G8B8A8) */
static inline void somvr_fade_image_fmt(uint8_t *px, int bgra) {
    for (int y = 0; y < SOMVR_FADE_H; y++)
        for (int x = 0; x < SOMVR_FADE_W; x++) {
            uint8_t *p = px + 4 * (y * SOMVR_FADE_W + x), a = somvr_fade_alpha(x / SOMVR_FADE_TEX);
            p[0] = p[1] = p[2] = 0;
            p[3] = a;
            if (y >= 4) p[bgra ? 2 : 0] = (uint8_t)(a * SOMVR_FADE_RED + 0.5f);
        }
}
static inline void somvr_fade_image(uint8_t *px) { somvr_fade_image_fmt(px, 0); }

/* a record's level (0..255) -> the ramp's level: 0 = no quad; any level > 0 at least 1; 255 = the opaque one */
static inline int somvr_fade_index(uint32_t level) {
    if (!level) return 0;
    if (level >= 255) return SOMVR_FADE_LEVELS - 1;
    int k = (int)((level * (SOMVR_FADE_LEVELS - 1) + 127) / 255);
    return k < 1 ? 1 : k;
}

/* the quad's sub-image rect (x, y, w, h) for ramp level k: the middle 2 x 2 of its block */
static inline void somvr_fade_rect(int k, int32_t rc[4]) {
    rc[0] = k * SOMVR_FADE_TEX + 1, rc[1] = 1, rc[2] = 2, rc[3] = 2;
}
/* ... in the red ramp (rows 4..7) when red */
static inline void somvr_fade_rect_tint(int k, int red, int32_t rc[4]) {
    somvr_fade_rect(k, rc);
    if (red) rc[1] = 5;
}

/* the quad's pose in the layer's space from a frame's eye poses: the eyes' midpoint + SOMVR_FADE_DIST_M along the head's
 * forward (-Z of eye 0's orientation), turned like the head (its +Z, the quad's front, toward the eyes) */
static inline void somvr_fade_pose(const somvr_posef eye[2], somvr_posef *out) {
    const float *q = eye[0].q;
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    float x = 0, y = 0, z = 0, w = 1;
    if (n > 1e-6f && isfinite(n)) x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
    /* R(q) (0, 0, -1) = -(the third column of R(q)) */
    float fx = -2 * (x * z + w * y), fy = -2 * (y * z - w * x), fz = -(1 - 2 * (x * x + y * y));
    *out = (somvr_posef){{x, y, z, w},
                         {0.5f * (eye[0].p[0] + eye[1].p[0]) + SOMVR_FADE_DIST_M * fx,
                          0.5f * (eye[0].p[1] + eye[1].p[1]) + SOMVR_FADE_DIST_M * fy,
                          0.5f * (eye[0].p[2] + eye[1].p[2]) + SOMVR_FADE_DIST_M * fz}};
}

#endif
