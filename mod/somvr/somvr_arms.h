/* somvr_arms.h - Talion's arms on the motion controllers (docs/re-arms-ik.md 2.2-2.3 and 4, design-vr.md 1 "The body you
 * see when you look down"): our own analytic two-bone IK per arm on the model-space pose the player's tracker node holds
 * after phase 2 (ARM_TRK_EVAL): 0x20 bytes per node {pos[3], pad = 0, quat x,y,z,w}, absolute model-space transforms,
 * parents before children, so a joint's whole subtree is moved by hand. Plus the grip -> model space conversion with
 * the frame's rig, the weapon-node target and the bone-based blade grip. Pure: no globals, no game calls (somvr.c owns
 * the hook, the skeleton walk, the grips and the logs). Tested by tests/test_arms.c.
 *
 * Units: Talion's model space = his object's frame (cm; +X right, +Y up, +Z forward: run 3's root frame, runtime-probes
 * P1 results), quaternions (x, y, z, w) Hamilton, v_model = R(q) v_local + pos (somvr_pose.h).
 */
#ifndef SOMVR_ARMS_H
#define SOMVR_ARMS_H

#include <stddef.h>
#include <stdint.h>

#include "somvr_combat.h"
#include "somvr_pose.h"

/* one node of the game's pose buffer */
typedef struct { float pos[3], pad, q[4]; } sa_node;
_Static_assert(sizeof(sa_node) == 0x20, "a pose node is 0x20 bytes");

/* a rigid transform: v' = p + R(q) v */
typedef struct { sv_vec3 p; sv_quat q; } sa_xf;

sa_xf sa_xf_mul(sa_xf a, sa_xf b);  /* a (x) b: p = a.p + R(a.q) b.p, q = a.q b.q */
sa_xf sa_xf_inv(sa_xf a);
sa_xf sa_node_xf(const sa_node *n);
void sa_node_set(sa_node *n, sa_xf x); /* the quaternion normalised, pad = 0 */
/* a calibration offset from x, y, z (cm) and rx, ry, rz (deg): the rotation turns about +X by rx, then about the new +Y
 * by ry, then about the new +Z by rz (q = qx (x) qy (x) qz) */
sa_xf sa_xf_euler(const float v[6]);

/* ---- the chains ---------------------------------------------------------------------------------------------------- */

enum { SA_L = 0, SA_R = 1 }; /* = SOMVR_HAND_L / SOMVR_HAND_R */
#define SA_MAXN 256          /* the skeleton's parent index is a byte */
#define SA_SUB 32            /* nodes below one joint (run 3: 20 below the upper arm) */
#define SA_TWSUB 8

/* the name hashes (somhash, docs/re-animation.md 2.3; run 3's nodes-2.tsv has them with somhash(name) = hash) */
#define SA_H_R_ARMU 0x844d7d3du
#define SA_H_R_ARML 0x844d7d34u
#define SA_H_R_HAND 0xc7485ad3u
#define SA_H_R_BICEP 0x644e72d3u
#define SA_H_R_FOREARM 0x439d342cu
#define SA_H_R_WEAPON 0x86cbddd8u
#define SA_H_L_ARMU 0x588cb073u
#define SA_H_L_ARML 0x588cb06au
#define SA_H_L_HAND 0x9b878e09u
#define SA_H_L_BICEP 0x532f49adu
#define SA_H_L_FOREARM 0x9146c6a6u
#define SA_H_L_WEAPON 0x0fef266eu

/* one arm in the skeleton: run 3's indices are R 108 -> 110 -> 112 (twist 111/113, weapon 119, clavicle 26) and
 * L 86 -> 88 -> 90 (89/91, 97, 25) */
typedef struct {
    int side;
    int p, s, e, w;      /* the clavicle (S's parent), the upper arm, the forearm, the hand */
    int tu, tf, wpn;     /* the upper-arm and forearm twist nodes, the weapon node (-1 = none) */
    int nsub[3];         /* how many nodes sub(S), sub(E), sub(W) hold (the joint itself not included) */
    int16_t sub[3][SA_SUB];
    int ntw[2];          /* the twist nodes' own subtrees (empty in run 3) */
    int16_t twsub[2][SA_TWSUB];
    int lo, hi;          /* the lowest and highest index the arm moves (S and its subtree): one block to write back */
} sa_chain;

/* the arm of `side` from the skeleton's parent bytes and name hashes (n nodes): S, E, W by hash with E = child of S and
 * W = child of E; the twist nodes (children of S / E) and the weapon node (below W) when present; parents must precede
 * their children. 0 = ok, else -1 and why. */
int sa_chain_build(sa_chain *c, int side, const uint8_t *parent, const uint32_t *hash, int n, char *why, size_t cap);

/* ---- the solver ---------------------------------------------------------------------------------------------------- */

/* the elbow bends about S's local -Z (run 3: R_Arml 27.1 deg and L_Arml 25.2 deg about -Z in their upper arms; both
 * chains: upper arm x forearm points along R(q_S) (0, 0, -1)), so the solver rolls S until that axis is the bend axis */
#define SA_HINGE_X 0.0f
#define SA_HINGE_Y 0.0f
#define SA_HINGE_Z (-1.0f)

typedef struct {
    float eps_cm;               /* the reach clamp: |S -> hand| in [|a - b| + eps, a + b - eps] (0.5) */
    float reach_cm;             /* "reached": the hand within this of its target (1) */
    float pole[3];              /* the elbow hint for the right arm in Talion's frame (x out, y up, z forward), the left
                                 * mirrors x: down, out and back (0.4, -1, -0.3); the elbow bends toward it */
    float pole_anim;            /* 0..1: the share of the animated elbow in the hint (0.25) */
    float pole_dps;             /* the elbow's plane turns about the shoulder -> hand line at most this fast (720 deg/s):
                                 * no flip when the hand passes the hint's own direction */
    float twist_upper;          /* the upper-arm twist node keeps this share of S's roll change (0.5) */
    float twist_fore;           /* the forearm twist node takes this share of the hand's roll change (0.5) */
    float blend_s;              /* the weight's ease in and out (0.15 s) */
    int stretch;                /* 1: a hand the arm cannot reach (clamped) is moved onto its target, its subtree (the
                                 * weapon node) with it: the forearm stretches. Only with the body unseen in the eyes
                                 * (SOMVR_HEAD_PARTS=main; headset session 1: the sword stayed at the shoulder) (0) */
} sa_cfg;
void sa_cfg_default(sa_cfg *c);

/* per arm, kept between frames */
typedef struct {
    float ramp;                 /* the weight's linear ramp 0..1 (eased: smoothstep) */
    int have_target;
    sa_xf target;               /* the last valid hand target (held while the weight goes down) */
    int have_m;
    sv_vec3 m;                  /* the last elbow direction (unit, from the shoulder -> hand line toward the elbow) */
    int have_tw;
    float tw[2][2];             /* the upper arm's and the hand's roll [animated, solved], unwrapped frame to frame (rad):
                                 * a roll through +-180 deg turns the twist nodes on, no flip; their change from the
                                 * animation is capped at +-SA_TW_MAX (it saturates, it never jumps) */
} sa_arm_state;
#define SA_TW_MAX 4.712389f     /* 1.5 pi */

/* one frame's weight: up while `valid`, down otherwise, over blend_s (dt clamped to 0..0.1 s); returns the eased weight.
 * At 0 the elbow and roll memories are dropped (a new start takes the hint at once). */
float sa_weight_step(sa_arm_state *st, const sa_cfg *c, int valid, float dt);

enum { SA_CLAMP_NONE = 0, SA_CLAMP_FAR = 1, SA_CLAMP_NEAR = 2 };
typedef struct {
    int solved;                 /* the pose was edited */
    const char *why;            /* not solved: why */
    int reached, clamp;
    float weight, residual;     /* cm between the target and the hand this frame (the weight's share included) */
    float stretch_cm;           /* how far the stretch moved the hand (0 = none) */
    float reach;                /* |S -> hand target| (cm) */
    float bend_deg;             /* the elbow's angle between the upper arm and the forearm (0 = straight) */
    float twist_u_deg, twist_f_deg; /* S's roll change, the hand's roll change about the forearm */
    float elbow[3], hand[3];    /* where they ended (model space) */
} sa_report;

/* the hand target for a weapon-node target t_wpn: t_wpn (x) L_w^-1 with L_w = the weapon node's transform in the hand in
 * this pose (it may be animated); no weapon node: t_wpn itself */
sa_xf sa_hand_target(const sa_node *pose, const sa_chain *c, sa_xf t_wpn);

/* ---- the weapon node held in the hand (build 10, SOMVR_ARMS_LW=held; headset session 3: "whenever I draw my sword my
 * hands fly out") -------------------------------------------------------------------------------------------------------
 * sa_hand_target puts the hand at t_wpn (x) L_w^-1 with L_w = the weapon node's transform in the hand *as animated*: an
 * animation that moves R_Weapon in the hand (the armed stance, a draw) moves the hand away from the controller by as
 * much while the sword stays on it. Held: L_w is taken once from a steady pose (the idle hand, the sword sheathed: the
 * socket the default calibration was measured on, run 3) and kept; after the solve the weapon node is put back on the
 * hand at that L_w, so the hand and the sword both follow the controller rigidly. */
sa_xf sa_weapon_local(const sa_node *pose, const sa_chain *c); /* L_w = W^-1 (x) WPN; identity without a weapon node */
sa_xf sa_hand_target_lw(sa_xf t_wpn, sa_xf lw);                 /* t_wpn (x) lw^-1 */
/* the weapon node (a leaf in run 3's skeleton: nothing below R_Weapon / L_Weapon) set to W (x) lw; 0 = no weapon node */
int sa_weapon_set(sa_node *pose, const sa_chain *c, sa_xf lw);
/* how far apart two transforms are: the positions (cm) and the rotation between them (deg, 0..180) */
float sa_xf_dist(sa_xf a, sa_xf b, float *deg);
typedef struct {
    int have;                   /* ref is set */
    sa_xf ref;                  /* the held L_w */
    int have_last, steady;      /* the frames lw stayed within SA_LW_STEADY_CM / _DEG of the last one */
    sa_xf last;
} sa_lw_hold;
#define SA_LW_STEADY_CM 0.05f
#define SA_LW_STEADY_DEG 0.2f
/* one frame's animated lw: while may_capture (somvr.c: the arm valid, the sword sheathed) and lw has been steady for
 * `frames` frames, the first capture (later ones: never, until sa_lw_reset: a new skeleton). Returns 1 on the capture */
int sa_lw_step(sa_lw_hold *h, sa_xf lw, int may_capture, int frames);
void sa_lw_reset(sa_lw_hold *h);

/* Solve one arm in place: the hand to t_hand (position clamped to the reach, rotation exactly), at weight w (0 = the
 * pose untouched, 1 = the full solution; in between the joints' local rotations are slerped, so the bones stay rigid).
 * The shoulder stays where the animation put it; the elbow bends toward the hint (st->m rate-limited by pole_dps * dt);
 * S, E and W carry their whole subtrees (fingers, the weapon node); the twist nodes get their shares. Any NaN in the
 * input or the result: the pose is left as it was. 1 = edited. */
int sa_solve(sa_node *pose, int n, const sa_chain *c, const sa_cfg *cfg, sa_xf t_hand, float w, float dt, sa_arm_state *st,
             sa_report *rep);

/* ---- the grip <-> model space --------------------------------------------------------------------------------------- */

/* the grip (mirrored tracking space: cm, somvr_hand_game) -> the world with the rig (world = base + shift + s R(Y(yaw))
 * (p - p0), the eyes' map with the player size s, re-xr-input.md 4.2 step 3) -> Talion's model space X^-1 (x) world
 * (step 4) */
sa_xf sa_grip_to_model(const sc_rigxf *rig, sv_vec3 shift, sa_xf X, sa_xf grip);
/* the world -> mirrored tracking space with the rig (its inverse: / s) */
sa_xf sa_world_to_track(const sc_rigxf *rig, sa_xf w);
/* the blade's grip from the bone (re-arms-ik.md 4): X (x) the weapon node (x) cal^-1 in tracking space, so that
 * somvr_blade_controller lays the blade on the drawn sword; equal to the controller grip whenever the arm reaches */
sa_xf sa_bone_grip(const sc_rigxf *rig, sa_xf X, sa_xf wpn_model, sa_xf cal);

#endif
