/* somvr_fingers.h - Talion's fingers on the controllers (round 3 of the hands, Half-Life: Alyx: the hand you see does
 * what your hand does; docs/re-xr-pipeline.md 29). Per hand the five fingers' three joints (by name hash, below L_Hand /
 * R_Hand), their local rotations set from two poses - the open hand and the closed fist - blended per finger:
 *   - the grip (squeeze) closes the middle, ring and pinky fingers; the trigger the index (its touch alone curls it a
 *     little); the thumb lies down while it touches a face button, the stick or the thumb rest (a controller without
 *     touch sensing: the thumb follows the grip);
 *   - a held weapon (the sword, the dagger, the hilt with both hands) is a closed fist whatever the buttons;
 *   - the bow hand keeps the game's own fingers (the bow's grip is the game's animation);
 *   - each finger eases to its target (an exponential approach, tau: no snapping), the whole hand blends in and out.
 * The poses are the right hand's local rotations; the left hand takes the same ones: this rig mirrors the left side with
 * every axis negated (run 3: each left joint's offset in its parent is minus the right one's, all three axes, and the
 * idle curls turn about +Z on both sides), and under that mirror a mirrored pose has equal local rotations.
 * Pure: no globals, no game calls (somvr_body_game.c owns the inputs, the hook and the logs). Tested by
 * tests/test_fingers.c on run 3's skeleton. */
#ifndef SOMVR_FINGERS_H
#define SOMVR_FINGERS_H

#include <stddef.h>
#include <stdint.h>

#include "somvr_arms.h"

enum { FG_THUMB, FG_INDEX, FG_MIDDLE, FG_RING, FG_PINKY, FG_NF };
#define FG_NJ 3 /* the knuckle, the middle joint, the tip joint */

/* the first joint's name hash per side and finger (somhash: "X2" and "X3" hash to "X1" + 1 and + 2; run 3's nodes) */
#define FG_H_R_THUMB1 0xc700bd92u
#define FG_H_R_INDEX1 0x00937a36u
#define FG_H_R_MIDDLE1 0x4981ba03u
#define FG_H_R_RING1 0x94ceadd8u
#define FG_H_R_PINKY1 0xce3aa515u
#define FG_H_L_THUMB1 0x50240628u
#define FG_H_L_INDEX1 0x89b6c2ccu
#define FG_H_L_MIDDLE1 0x972b4c7du
#define FG_H_L_RING1 0x83af84b2u
#define FG_H_L_PINKY1 0x575dedabu

typedef struct {
    int side;                 /* SA_L / SA_R */
    int hand;                 /* L_Hand / R_Hand (the chain's W) */
    int j[FG_NF][FG_NJ];      /* the joints, knuckle first: each one's parent is the one before (the first's the hand) */
} fg_hand;

/* the fingers of `side` from the skeleton (parents first) below the hand node `hand`: every joint by hash, the chains
 * hand -> 1 -> 2 -> 3, no joint with another child (it would be left behind). 0 = ok (why: a summary), else -1 and why. */
int fg_hand_build(fg_hand *f, int side, int hand, const uint8_t *parent, const uint32_t *hash, int n, char *why,
                  size_t cap);

typedef struct { sv_quat q[FG_NF][FG_NJ]; } fg_pose; /* each joint's rotation in its parent joint's frame */
extern const fg_pose fg_pose_open;  /* the relaxed open hand (run 3's idle right hand) */
extern const fg_pose fg_pose_fist;  /* the closed fist: ours (the game holds the sword with its idle hand, run 020044) */

enum { FG_HOLD_NONE = 0, FG_HOLD_FIST = 1, FG_HOLD_GAME = 2 };
typedef struct {
    float squeeze, trigger;   /* 0..1 */
    int trigger_touch;        /* the index rests on the trigger */
    int thumb_touch;          /* the thumb rests on a face button, the stick or the thumb rest */
    int touch_ok;             /* the controller senses the thumb (Touch, Index); 0: the thumb follows the grip */
    int hold;                 /* FG_HOLD_*: a held weapon = a fist, the bow hand = the game's fingers */
} fg_in;

typedef struct {
    float tau_s;              /* each finger's ease toward its target (0.03 s: 95 % in 0.09 s) */
    float blend_s;            /* the hand's weight in and out (to and from the game's own fingers, 0.1 s) */
    float index_touch;        /* the index's curl while it only touches the trigger (0.2) */
    float thumb_rest;         /* the thumb's curl resting on a button with the other fingers open (0.6; closed: 1) */
} fg_cfg;
void fg_cfg_default(fg_cfg *c);

typedef struct {
    int have;
    float c[FG_NF];           /* the eased curls 0..1 */
    float t[FG_NF];           /* this frame's targets */
    float ramp;               /* the weight's linear ramp 0..1 (eased: smoothstep) */
} fg_state;

/* the targets: a fist for a held weapon; else middle/ring/pinky = the grip, the index = the trigger (at least
 * index_touch while touched), the thumb = 0 lifted, thumb_rest .. 1 resting (with the grip and the index), the grip
 * without touch sensing */
void fg_targets(const fg_cfg *c, const fg_in *in, float out[FG_NF]);
/* one frame (dt clamped to 0..0.1 s): the curls toward the targets, the weight toward 1 (toward 0 with FG_HOLD_GAME);
 * the first step starts at the targets with the weight on (the arm's own weight eases the hand in). The eased weight. */
float fg_step(fg_state *s, const fg_cfg *c, const fg_in *in, float dt);

/* The finger joints of pose (model-space nodes, sa_node) set: each joint's local rotation = slerp(its rotation in the
 * pose, slerp(open, fist, curl of its finger), w), its offset in its parent kept, then placed from its parent's new
 * transform (the hand itself and its other children, the weapon node, untouched). Nothing written for w <= 0, a bad
 * hand or a non-finite pose. 1 = written. */
int fg_apply(sa_node *pose, int n, const fg_hand *f, const fg_pose *open, const fg_pose *fist, const float curl[FG_NF],
             float w);
/* joint (finger, k)'s local rotation and offset in the pose (its parent's frame); 0 = not finite */
int fg_local(const sa_node *pose, const fg_hand *f, int finger, int k, sa_xf *out);
const char *fg_finger_name(int finger); /* "thumb" ... "pinky" */

#endif
