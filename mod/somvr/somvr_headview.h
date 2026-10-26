/* somvr_headview.h - the head out of the view (docs/re-xr-pipeline.md 25): the eye point moved in Talion's body frame
 * (SOMVR_EYE_FWD_CM along the body's yaw, SOMVR_EYE_UP_CM up), a clamp that keeps it at least min_cm ahead of the Head
 * node along that forward (eased: no sudden motion), and the live tuning file's parser (SOMVR_TUNE). Pure: no globals.
 * Tested by tests/test_headview.c.
 *
 *   f = (sin yaw_e, 0, cos yaw_e)       yaw_e = the body yaw through a critically damped ease (yaw_s)
 *   p = base + fwd f + (0, up, 0)       base = the anchor's eye point (root + 76.6 cm) or the Head node (+ SOMVR_EYE_CM)
 *   t = max(0, min - (p - head) . f)    the clamp's target; x eases to t (clamp_s, never below 0); pos = p + x f
 */
#ifndef SOMVR_HEADVIEW_H
#define SOMVR_HEADVIEW_H

#include <stdint.h>

#include "somvr_pose.h"

typedef struct {
    float fwd_cm;  /* the eye ahead of the base along the body's forward (16) */
    float up_cm;   /* and above it (0) */
    float min_cm;  /* the eye at least this far ahead of the Head node along that forward (10; <= 0: no clamp) */
    float clamp_s; /* the clamp's ease, 1 / omega (0.06 s) */
    float yaw_s;   /* the forward's yaw ease, 1 / omega (0.08 s; 0 = the body yaw as it is) */
    float gap_s;   /* frames further apart: re-latched (0.25 s) */
} somvr_hv_cfg;
void somvr_hv_cfg_default(somvr_hv_cfg *c);

typedef struct {
    int have;
    double t;
    float yaw, yaw_v; /* the eased yaw and its velocity */
    float x, x_v;     /* the clamp's push and its velocity */
    uint64_t frames, latches, clamped;
    float x_max;
} somvr_hv;
void somvr_hv_init(somvr_hv *s);

typedef struct {
    double t;
    sv_vec3 base;   /* the frame's eye point before the offset (cm) */
    float body_yaw; /* Talion's root heading (rad, sv_heading) */
    int has_head;
    sv_vec3 head;   /* the Head node (cm) */
} somvr_hv_in;

typedef struct {
    sv_vec3 pos;     /* the eye point */
    float yaw;       /* the forward's (eased) yaw */
    float ahead_cm;  /* (p - head) . f before the clamp (has_head) */
    float target_cm; /* the clamp's target push */
    float push_cm;   /* the push applied */
    int latched;
} somvr_hv_out;
void somvr_hv_step(somvr_hv *s, const somvr_hv_cfg *c, const somvr_hv_in *in, somvr_hv_out *o);

/* SOMVR_TUNE: the file's text, lines KEY=VALUE ('#' comments, blanks, spaces, CR ignored). Known keys and ranges in
 * somvr_tune_keys / _lo / _hi; a known key with a bad value or out of range = bad (counted, not set); an unknown key =
 * unknown. The last good line of a key wins. */
enum { SOMVR_TUNE_EYE_FWD, SOMVR_TUNE_EYE_UP, SOMVR_TUNE_NEAR, SOMVR_TUNE_EYE_MIN, SOMVR_TUNE_N };
typedef struct {
    uint32_t set; /* bit k: key k given */
    float v[SOMVR_TUNE_N];
    int bad, unknown;
    char first_bad[48]; /* the first bad / unknown line (cut) */
} somvr_tune;
extern const char *const somvr_tune_keys[SOMVR_TUNE_N]; /* eye_fwd_cm eye_up_cm near_cm eye_min_cm */
extern const float somvr_tune_lo[SOMVR_TUNE_N], somvr_tune_hi[SOMVR_TUNE_N];
void somvr_tune_parse(const char *text, somvr_tune *out);

#endif
