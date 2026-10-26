/* somvr_headbone.h - SOMVR_HIDE_HEAD=bone (docs/re-firstperson.md 3.3, re-xr-pipeline.md 6.7): Talion's Head node and
 * everything below it (the face, jaw, eye and hair nodes) collapsed onto the Head joint, at the top of the neck, in the
 * model-space pose the player's tracker holds after phase 2 (the arms' write point, ARM_TRK_EVAL; runtime-probes P8 A5).
 *
 * The pose is rigid: 0x20 bytes per node {pos[3], pad, quat}, no scale lane, and the skinning masks the 4th lane
 * (CSimpleModelOutputPose::vf_03 0x237ce40 ands every position with {~0, ~0, ~0, 0} at 0x3341230; re-animation.md 1.4,
 * 2.1). So the "scale" can only move joints, not shrink the meshes on them: every node below Head gets
 * pos' = p_head + s (pos - p_head), its rotation kept. Head itself is never written: its position and rotation stay the
 * game's, so SOMVR_BASE=head (GetNodeTransform at H4, after phase 2) reads the same eye base with or without the edit.
 * Pure: no globals, no game calls (somvr.c owns the hook, the skeleton walk and the logs). Tested by tests/test_headbone.c.
 */
#ifndef SOMVR_HEADBONE_H
#define SOMVR_HEADBONE_H

#include <stddef.h>
#include <stdint.h>

#include "somvr_arms.h" /* sa_node, SA_MAXN */

#define HB_H_HEAD 0x72598328u /* somhash("Head"): run 3's node 28 (parent 24 Neck; eyes 32/40) */
#define HB_SCALE 0.001f        /* the collapse: the subtree's joints at 1/1000 of their distance from Head */
#define HB_MAXSUB (SA_MAXN - 1)
#define HB_MAXRUN 32           /* contiguous index runs of the subtree = the write-back blocks (run 3: one, 29..85) */

typedef struct {
    int head;                    /* the Head node's index (-1 = none) */
    int parent;                  /* its parent (run 3: 24, Neck) */
    int nsub;                    /* how many nodes are below it, Head not included (run 3: 57) */
    int16_t sub[HB_MAXSUB];      /* in index order (parents before children) */
    int nrun;
    int16_t run[HB_MAXRUN][2];   /* first, last index of each contiguous run of sub */
} hb_tree;

/* The Head node by its name hash and its whole subtree from the skeleton's parent bytes (n nodes, 0xff = the root, node
 * 0 only; parents must precede their children, as the game's skeletons do). 0 = ok (why = a summary), else -1 and why. */
int hb_tree_build(hb_tree *t, const uint8_t *parent, const uint32_t *hash, int n, uint32_t head_hash, char *why,
                  size_t cap);

/* The collapse in place: every node of t->sub to p_head + s (pos - p_head), pad 0, rotation kept; pose[t->head] is not
 * written. Nothing changes (0) for a tree that does not fit n, a non-finite Head position, or s outside [0, 1].
 * 1 = edited. */
int hb_collapse(sa_node *pose, int n, const hb_tree *t, float s);

#endif
