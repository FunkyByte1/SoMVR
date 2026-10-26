/* somvr_body.h - the hands and the sword (headset sessions 1-2, 2026-10-22/23: "There shouldn't be an avatar. Just
 * hands.", "I couldn't see my hands.", "When I drew my sword, it was in the wrong orientation and position relative to my
 * hand.", "I would like the ability to one-hand or two-hand the sword."; docs/re-xr-pipeline.md 28).
 *
 * 1. SOMVR_BODY=hands: Talion's body is drawn again in the eyes (SOMVR_HEAD_PARTS=main no longer skips it), but every
 *    skinning transform outside the two hands' subtrees (L_Hand / R_Hand, their fingers and weapon nodes) is replaced
 *    before the render node copies it (CSimpleModelRenderable::vf_02, phase 7, one copy for every pass): "zero" = the
 *    rotation 0 at the side's wrist joint, so the vertices of those bones collapse onto the wrist and a vertex blended
 *    between the hand and the forearm is pulled toward the wrist (the cuff closes); "nan" = NaN (the GPU drops every
 *    triangle that touches them); "far" = 1 km below (the old far-away trick, with stretched seams; a test). Which one
 *    the GPU's skinning honours was a runtime question (section 28.2). A hand the IK does not hold (lost, the arm's weight
 *    0) goes the same way. The pose itself is not touched: GetNodeTransform, physics, the cloth and the weapons'
 *    attachments see the whole body.
 * 2. The grip: the weapon node in the grip's frame (SOMVR_ARMS_CAL_R) and the sword angle (SOMVR_SWORD_ANGLE, the VR
 *    menu's Sword angle row): the blade line in the grip's frame is (0, sin a, cos a) for the drawn sword and the combat
 *    blade alike (sb_blade_dir, sb_cal_angle).
 * 3. SOMVR_TWOHAND=1: the left hand squeezed on the hilt (within grab_r_cm of the segment behind the right grip along
 *    the blade) holds the sword with both hands: the blade turns onto the line from the left grip to the right one, the
 *    left hand's target sits on the hilt at its distance (clamped to the hilt); letting go (or the hands pulled apart)
 *    gives the sword back to the right hand, both eased over blend_s.
 *
 * Space: the grips are in the mirrored tracking space (cm, +y up, +z ahead; somvr_hand_game), whose grip frame has +x
 * right, +y up, +z along the fist from the little finger to the thumb (OpenXR's -Z). Pure: no globals, no game calls
 * (somvr_body_game.c owns the hook, the reads and the logs). Tested by tests/test_body.c.
 */
#ifndef SOMVR_BODY_H
#define SOMVR_BODY_H

#include <stddef.h>
#include <stdint.h>

#include "somvr_arms.h"

/* ---- 1. hands only -------------------------------------------------------------------------------------------------- */

enum { SB_BODY = 0, SB_HAND_L = 1, SB_HAND_R = 2 };                                   /* sb_keep.kind */
enum { SB_HIDE_ZERO = 0, SB_HIDE_NAN = 1, SB_HIDE_FAR = 2, SB_HIDE_SHOW = 3, SB_HIDE_LINE = 4, /* SOMVR_BODY_HIDE */
       SB_HIDE_CUFF = 5,    /* round 3: line, and each shown hand keeps a stub of its forearm: the twist node's vertices
                             * (the lower half) rigid with the hand (its skin transform), the line through both stubs'
                             * ends (the twist nodes' bind points carried by their hands) */
       SB_HIDE_FOREARM = 6, /* ... the whole forearm (the twist and the forearm node) rigid with the hand, the line through
                             * both elbows' bind points carried by their hands */
       SB_HIDE_CUFFIK = 7,  /* ... the twist node with its own (the IK's) transform, moved so that it carries the bind
                             * wrist onto the posed one (an arm stretched past its reach keeps the stub on the hand); the
                             * line through both twist nodes so moved */
       SB_HIDE_N };
#define SB_FAR_CM 100000.0f

typedef struct {
    int n;                    /* the skeleton's nodes */
    int wrist[2];             /* L_Hand, R_Hand */
    int nhand[2], nbody;      /* how many nodes each hand's subtree (the wrist included) and the body hold */
    uint8_t kind[SA_MAXN];    /* per node: SB_BODY, SB_HAND_L, SB_HAND_R */
    uint8_t aside[SA_MAXN];   /* a body node's wrist: 0 = the left one (the left clavicle and arm above the wrist), else 1 */
    int stub[2][2];           /* per side the forearm's twist node and the forearm node (the chain's tf and e; -1 = none):
                               * SB_HIDE_CUFF keeps the first, SB_HIDE_FOREARM both, rigid with the hand */
} sb_keep;

/* the hands' subtrees from both chains (sa_chain_build's) and the skeleton's parent bytes (n nodes, parents first).
 * 0 = ok (why = a summary), else -1 and why. */
int sb_keep_build(sb_keep *k, const sa_chain ch[2], const uint8_t *parent, int n, char *why, size_t cap);

/* the skinning transforms (n nodes, skeleton order, {pos, pad, quat}) edited in place: every node not in a shown hand
 * (show bit 0 = the left hand, bit 1 = the right) by `mode` with the side's wrist joint from the model pose `model`
 * (same order); SB_HIDE_SHOW edits nothing. SB_HIDE_CUFF / _FOREARM: a shown hand's stub nodes get its wrist's skin
 * transform (their vertices move with the hand as in the bind pose: a straight forearm), the other nodes the line
 * through both stubs' ends (a hidden hand's side: its wrist, as SB_HIDE_LINE). SB_HIDE_CUFFIK: the twist node keeps its
 * rotation, its translation moved by the wrist's gap. How many nodes were written; 0 for a
 * keep set that does not fit n, a non-finite wrist, a bad mode. */
int sb_skin_edit(sa_node *skin, const sa_node *model, int n, const sb_keep *k, unsigned show, int mode);
const char *sb_hide_name(int mode);
int sb_hide_parse(const char *s); /* "zero", "nan", "far", "show", "line"; -1 = none of them */

/* ---- 2. the grip ---------------------------------------------------------------------------------------------------- */

/* the blade's direction for a grip q (mirrored tracking space) at the sword angle a: R(q) (0, sin a, cos a) */
sv_vec3 sb_blade_dir(sv_quat q, float angle_rad);
/* the weapon node in the grip's frame tilted with the blade: Rx(-a) (x) cal (a > 0 = the blade up toward the grip's +y,
 * as somvr_blade_controller's pitch) */
sa_xf sb_cal_angle(sa_xf cal, float angle_rad);

/* ---- 3. two hands --------------------------------------------------------------------------------------------------- */

typedef struct {
    float grab_r_cm;               /* the left grip point within this of the hilt segment grabs (12) */
    float hilt_lo_cm, hilt_hi_cm;  /* the hilt segment: this far behind the right grip point along the blade (3 .. 28) */
    float hold_lo_cm, hold_hi_cm;  /* where the left hand sits on it: its distance from the right grip, clamped (8 .. 24) */
    float break_cm;                /* the grips further apart than this: one hand again (45) */
    float squeeze_on, squeeze_off; /* the left grip's hysteresis (0.6 / 0.35) */
    float blend_s;                 /* the ease in and out (0.1 s) */
    float hold_d_cm;               /* below this distance the blade keeps its last two-hand direction (4) */
} sb_tw_cfg;
void sb_tw_cfg_default(sb_tw_cfg *c);

typedef struct {
    int active, sq, have_d;
    float ramp;                    /* 0..1 linear; the weight is its smoothstep */
    sv_vec3 d;                     /* the last two-hand blade direction */
    uint64_t grabs, releases, breaks, lost, misses;
} sb_tw_state;

typedef struct {
    int sword;                     /* the sword is in the right hand */
    int allow;                     /* the left hand is free (no bow, climb hold, dagger) */
    int valid[2];
    sa_xf g[2];                    /* the grips (mirrored tracking space, cm) */
    float squeeze_l;               /* the left grip 0..1 */
    float angle_rad;               /* the sword angle */
    float dt;
} sb_tw_in;

enum { SB_TW_E_GRAB = 1, SB_TW_E_RELEASE = 2, SB_TW_E_BREAK = 4, SB_TW_E_LOST = 8, SB_TW_E_MISS = 16 };
typedef struct {
    sa_xf g[2];                    /* the grips the arms use: the right one turned so its blade line (the sword angle's)
                                    * lies on the two-hand line, the left on the hilt with its own fist axis (+z) along it
                                    * (both = the input while the weight is 0) */
    float w;                       /* the eased weight */
    float dist_cm;                 /* the grips' distance; the left hand's place on the hilt (cm behind the right grip) */
    float hold_cm;
    unsigned ev;                   /* SB_TW_E_* this step */
} sb_tw_out;

void sb_tw_step(sb_tw_state *s, const sb_tw_cfg *c, const sb_tw_in *in, sb_tw_out *out);

#endif
