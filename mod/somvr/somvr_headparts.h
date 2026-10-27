/* somvr_headparts.h - Talion's head not drawn in the eye passes: SOMVR_HIDE_HEAD=parts (docs/re-xr-pipeline.md 26).
 * Pure: no globals, no game memory. Tested by tests/test_headparts.c.
 *
 * Talion's visible model is one CSimpleModel with a renderable per part: CSimpleModel::vf_20(model, slot, mesh) keeps
 * the part's CSimpleModelRenderable at model+0xb8 + 8 x slot (slot < 20), FUN_023723f0 fills the slots from the
 * SimpleModel config FUN_0236b820 builds: 0 Body, 1 Head (FUN_02361160, config+0xa0), 2 Legs, 3 Helm = the hood
 * (FUN_023615c0, +0xb8), 4 Hair (FUN_023618c0, +0xc0), 5/6 Acc1/Acc2, 9-12 armour. Each renderable has its own render
 * node (handle +0x50, node = handle+0x10). The eye passes' skeletal draws all go through FUN_0081ff80(ctx, node, ...),
 * called by five list builders: deep buffer, forward, late, G-buffer, velocity; the shadow maps use FUN_0081fd10
 * (FUN_008229a0 / FUN_00824570) and never reach it. So:
 *   - the game thread (H4, every stereo frame on the head base) reads the wanted slots' nodes and publishes them for
 *     that renderer frame (a ring: the render thread renders a frame's eyes after its H4);
 *   - the render thread, at each of the five calls, skips the call when an eye renders (H3's eye match -> H8) and the
 *     node is one of that frame's head nodes. Nothing else changes: the shadows keep the head, flat frames show it.
 * The cloth (docs/re-xr-pipeline.md 26.7; the white fur mantle at the shoulders that "main" left in the eyes): the
 * model's Havok cloth is not in any of those renderables. CSimpleModel+0x88 is its CSimpleModelClothNode; its cloth set
 * (+0x138) keeps one 0x18-byte entry per cloth display mesh, entry+0 a CPhysicsClothVisualization whose +0x10 is a
 * TRenderNodeMeshCustomHandle with its CRenderNodeMeshCustom at +0x10 (node kind 2). Custom nodes are not drawn by
 * FUN_0081ff80 but by FUN_007ccec0(dl, obj, node, pass 6, flag) at eight calls of the same five builders (pass 7 at the
 * shadow maps' three calls, never touched): those eight are hooked the same way, the node in rdx. */
#ifndef SOMVR_HEADPARTS_H
#define SOMVR_HEADPARTS_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define SOMVR_HP_SLOTS 20 /* CSimpleModel::vf_20 takes slots 0..19 (cmp esi, 0x13) */
#define SOMVR_HP_MAX 16   /* nodes per frame at most ("main": the body, the put-away weapons' models and parts, the cloth) */
#define SOMVR_HP_MESHES 4 /* SOMVR_HP_AWAY: Talion's own meshes (the body's, the wraith's) a held-away frame skips by mesh */
#define SOMVR_HP_WEAPONS 12 /* SOMVR_HP_COPIES: the weapons' own nodes (and their meshes) per frame at most */
#define SOMVR_HP_HEAD 1
#define SOMVR_HP_HELM 3
#define SOMVR_HP_HAIR 4
#define SOMVR_HP_DEFAULT ((1u << SOMVR_HP_HEAD) | (1u << SOMVR_HP_HELM) | (1u << SOMVR_HP_HAIR))
#define SOMVR_HP_WEAPON (SOMVR_HP_SLOTS + 1) /* not a slot: a put-away weapon's model, hidden with "body" (and "main") */
#define SOMVR_HP_MAIN SOMVR_HP_SLOTS /* not a slot: the model's main renderable (+0xb0), "body" in a list ("main" =
                                     body + cloth): Talion's whole body (headset session 1: he fills no slot, his head is
                                     in it) and the put-away weapons */
#define SOMVR_HP_CLOTH (SOMVR_HP_SLOTS + 2) /* not a slot: the model's cloth display meshes (custom render nodes: the fur
                                              mantle, the cloak), "cloth" in a list; "main" = body + cloth */
#define SOMVR_HP_MAIN_BITS (1u << SOMVR_HP_MAIN | 1u << SOMVR_HP_CLOTH) /* "main": no avatar, only the hands */

/* the five camera-pass call sites of FUN_0081ff80 (their order = the HP_DRAW_* sites), then what never reaches them */
enum {
    SOMVR_HP_DEEP,   /* FUN_00822a40: deep buffer (category 2: hair, OIT) */
    SOMVR_HP_FWD,    /* FUN_00822c70: forward (categories 1 and 3) */
    SOMVR_HP_LATE,   /* FUN_00822eb0: late pass (category 4) */
    SOMVR_HP_GBUF,   /* FUN_008230d0: G-buffer (category 1) */
    SOMVR_HP_VEL,    /* FUN_00823760: velocity (motion blur) */
    SOMVR_HP_PASSES, /* the hooked ones */
    SOMVR_HP_SHADOW = SOMVR_HP_PASSES, /* FUN_0081fd10's: sky shadow, sun cascades, local lights (never skipped) */
    SOMVR_HP_OTHER   /* anything else (never skipped) */
};
const char *somvr_hp_pass_name(int pass);

/* the slot names for the log ("body", "head", ...; "slotN" past the known ones; "main", "weapon", "cloth") */
const char *somvr_hp_slot_name(int slot);

/* "1,3,4" / "2-4,14" / "main" / "main,1" / "body" / "cloth" / "none" (slots 0..19; body = bit SOMVR_HP_MAIN, cloth =
 * bit SOMVR_HP_CLOTH, main = both; ends at the string's end or white space) -> mask. 0 = ok */
int somvr_hp_parse_slots(const char *s, uint32_t *out);
/* the mask as "1,3,4" / "main,1" / "body" / "cloth" ("none" when empty) */
void somvr_hp_slots_str(uint32_t mask, char *out, size_t cap);

/* the entries of a [begin, end) vector of stride-byte entries: 0..max, -1 = not a vector (end < begin, a partial entry,
 * a null begin with entries, more than max) */
int somvr_hp_vec_n(uintptr_t begin, uintptr_t end, uint32_t stride, int max);

/* SOMVR_HP_NEARLOG: the distance from p to a render node's world box lo..hi (0 inside); -1 = not a box (not finite,
 * inverted) */
float somvr_hp_box_dist(const float lo[3], const float hi[3], const float p[3]);

/* one frame's head nodes */
typedef struct {
    uint64_t frame;              /* the renderer frame (H4's g_fr.frame); 0 = none */
    uint32_t n;
    uintptr_t node[SOMVR_HP_MAX];
    uint8_t slot[SOMVR_HP_MAX];
    float at[3];                 /* the frame's head base point (world cm; SOMVR_HP_NEARLOG measures from it) */
    uintptr_t pose;              /* SOMVR_HP_POSE: the hidden body's CSkeletalTransforms (0 = none): every skeletal node
                                    it animates is skipped too (the wraith over Talion: run 034707) */
    uint32_t away;               /* SOMVR_HP_AWAY: the view is held away from Talion this frame (the strike, a body
                                    moment's hold): every skeletal node of one of his own meshes is skipped too (the
                                    strike's wraith copy has transforms of its own: run 072205) */
    uint32_t nmesh;
    uintptr_t mesh[SOMVR_HP_MESHES]; /* ... those meshes (the body's, the ones the pose rule skipped) */
    uint32_t copies;             /* SOMVR_HP_COPIES=on: a node drawing the mesh of a weapon this set hides (put away, not
                                    in our hands) that is none of the weapons' own nodes is skipped: the wraith's glowing
                                    copy of the sheathed sword (build 15b's session, re-xr-pipeline.md 49) */
    uint32_t nweap;
    uintptr_t wnode[SOMVR_HP_WEAPONS]; /* the weapons' own nodes (held, put away, the bow: main renderable and parts) */
    uintptr_t wmesh[SOMVR_HP_WEAPONS]; /* ... and each one's mesh (+0xc8) */
} somvr_hp_set;
void somvr_hp_set_init(somvr_hp_set *s, uint64_t frame);
/* add a slot's node (0 = nothing; a duplicate is not added again); 1 = added, 0 = not (0, a duplicate, full) */
int somvr_hp_set_add(somvr_hp_set *s, uintptr_t node, int slot);
/* the entry holding node, or -1 */
int somvr_hp_match(const somvr_hp_set *s, uintptr_t node);

/* the rule, per call: skip this draw? eye = the eye the render thread renders now (-1 outside an eye window), pass =
 * SOMVR_HP_*, s = the set looked up for rt_frame (NULL = none), node = the call's node. 1 only for an eye window, one of
 * the five camera passes, the set of this very frame, and a node in it */
int somvr_hp_skip(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node);
/* SOMVR_HP_POSE's rule: the same windows, passes and frame; node_pose = the call's skeletal node's transforms (+0xf0).
 * 1 = the set has a pose and the node is animated by it */
int somvr_hp_skip_pose(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node_pose);
/* SOMVR_HP_AWAY's rule: the same windows, passes and frame; node_mesh = the call's skeletal node's mesh (+0xc8). 1 = the
 * set is a held-away one and the mesh is one of its meshes */
int somvr_hp_skip_mesh(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node_mesh);
/* add a mesh to the set's list (0 = nothing; a duplicate is not added again); 1 = added */
int somvr_hp_add_mesh(somvr_hp_set *s, uintptr_t mesh);
/* SOMVR_HP_COPIES: a weapon's own node and its mesh (0 either = nothing; a node already in = not again); 1 = added */
int somvr_hp_add_weapon(somvr_hp_set *s, uintptr_t node, uintptr_t mesh);
/* the weapon entry whose mesh node_mesh is (a hidden one first), when node is none of the weapons' own nodes (a copy);
 * -1 = no copy */
int somvr_hp_copy_of(const somvr_hp_set *s, uintptr_t node, uintptr_t node_mesh);
/* SOMVR_HP_COPIES' rule: the same windows, passes and frame; 1 = the set skips copies and the node is a copy of a weapon
 * the set hides (a held weapon's copies are its glowing self in the wraith vision: kept) */
int somvr_hp_skip_copy(int eye, int pass, const somvr_hp_set *s, uint64_t rt_frame, uintptr_t node, uintptr_t node_mesh);

/* game thread -> render thread, per renderer frame (one writer, any readers; an entry is overwritten 4 frames later) */
#define SOMVR_HP_RING 4
typedef struct {
    struct {
        _Atomic uint64_t frame; /* 0 while written */
        _Atomic uint32_t n;
        _Atomic uintptr_t node[SOMVR_HP_MAX];
        _Atomic uint8_t slot[SOMVR_HP_MAX];
        _Atomic uint32_t at[3]; /* the floats' bits */
        _Atomic uintptr_t pose;
        _Atomic uint32_t away, nmesh;
        _Atomic uintptr_t mesh[SOMVR_HP_MESHES];
        _Atomic uint32_t copies, nweap;
        _Atomic uintptr_t wnode[SOMVR_HP_WEAPONS], wmesh[SOMVR_HP_WEAPONS];
    } e[SOMVR_HP_RING];
} somvr_hp_ring;
void somvr_hp_ring_init(somvr_hp_ring *r);
void somvr_hp_publish(somvr_hp_ring *r, const somvr_hp_set *s);
/* the set published for frame (frame != 0); 1 = found (out filled, out->n may be 0), 0 = none or overwritten */
int somvr_hp_lookup(somvr_hp_ring *r, uint64_t frame, somvr_hp_set *out);

#endif
