/* somvr_size.h - the player size (SOMVR_PLAYER_SIZE, the VR menu's "Player size"; docs/re-xr-pipeline.md 35). The
 * user (build 9): "I just feel very small ... I need to be bigger for the sake of VR". A size s > 1 makes the player
 * bigger and so the world smaller, scaled about Talion's feet, in the rig (somvr_eyes.h somvr_rig.scale) only:
 *   - the eye height above the feet: the base rises by (s - 1) x the eye height at 100 % (somvr_size_lift);
 *   - the eye separation and the head's room motion: s x the tracked offsets from the recentre's head;
 *   - the hands: the same map (somvr_rig_point, sc_rigxf.s), so they stay where the controllers are in the eyes, the
 *     wrist menu, the watch and the HUD (XR space quads of the layer) line up untouched;
 *   - the blade keeps the sword model's world length (sc_rigxf.s divides it in tracking space), the climb carries
 *     s x the hand's move, the threat glows map back by / s.
 * Talion's body is not scaled (the engine has no bone scale: re-firstperson.md 3.1); with SOMVR_BODY=hands only his
 * hands are drawn, at his size. A change lands only on a black frame of a short blink (the record's fade), never
 * while a climb hold is on; at the first frame the start value holds without a blink.
 * Pure: no globals, no game reads. Tested by tests/test_size.c; the game side is somvr_size_game.c. */
#ifndef SOMVR_SIZE_H
#define SOMVR_SIZE_H

#include <stdint.h>

#include "somvr_comfort.h" /* somvr_blink */

#define SOMVR_SIZE_MIN_PCT 100   /* the menu's range: 100 .. 140 % in 5 % steps */
#define SOMVR_SIZE_MAX_PCT 140
#define SOMVR_SIZE_STEP_PCT 5
#define SOMVR_SIZE_ROOT_CM 104.0f /* Talion's root above his feet (run 3: World_Ground 104.0 cm below the root; the
                                   * session 165018's SIZE lines: the ground 102.9 .. 104.0 cm below it) */
#define SOMVR_SIZE_OUT_S 0.10f    /* the blink: 0.10 s to black, 0.25 s back */
#define SOMVR_SIZE_IN_S 0.25f

/* a size from percent (clamped to 100 .. 140; NaN: 100) */
float somvr_size_of_pct(double pct);
/* the base's rise for size s with the eyes eye_h_cm above the feet at 100 % (the feet stay where they are) */
float somvr_size_lift(float s, float eye_h_cm);

typedef struct {
    float out_s, in_s; /* the blink */
} somvr_size_cfg;
void somvr_size_cfg_default(somvr_size_cfg *c);

typedef struct {
    float cur;          /* the size shown (1 = 100 %) */
    float want;         /* the size asked for (the menu, the file, the trigger word) */
    int landed;         /* this frame: cur changed (a jump of the rig, not motion) */
    float from;         /* the last landing's old size */
    somvr_blink blink;
    float alpha;        /* this frame's black */
    uint64_t asks, lands, held_frames, frames;
} somvr_size;
void somvr_size_init(somvr_size *z, float s); /* cur = want = s (the start: no blink) */
/* a new size asked for (clamped); 1 = it differs from what was asked last (a new ask) */
int somvr_size_ask(somvr_size *z, float s);
/* One stereo frame at time t (s): hold = no change may land now (a climb hold: a size change would move the held
 * wall point under the hand). Fades out while a change is pending, lands it on the black frame (cur = want,
 * landed = 1), fades in. A pending change dropped by hold fades back in. Returns the frame's black (0..1). */
float somvr_size_frame(somvr_size *z, const somvr_size_cfg *c, int hold, double t);

/* ---- the size probe (docs/re-xr-pipeline.md 35.1): the steps in a ground profile ------------------------------- */
/* h[0..n-1]: the ground's height (cm) every dx_cm along a line (NaN = no hit). The rises and drops between neighbours
 * of at least min_cm (a riser, a ledge, a kerb): up to cap of them in out[] (the jump, + = up), at idx[] (the first
 * sample's index). Returns how many were found (more than cap: cap stored). */
int somvr_size_steps(const float *h, int n, float min_cm, float *out, int *idx, int cap);

#endif
