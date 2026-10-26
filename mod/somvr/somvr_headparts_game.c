/* somvr_headparts_game.c - the game side of SOMVR_HIDE_HEAD=parts (somvr_headparts.h; docs/re-xr-pipeline.md 26).
 * NOT a translation unit of its own: somvr.c includes it before the head-view region (it uses somvr.c's statics: g_hb,
 * the SOMVR_HIDE_HEAD piece path's hide_model_of / g_hh / g_hh_sites, the frame g_fr, H3's g_rt_eye / g_rt_frame,
 * peek / vptr_of, the log, WRAPPER, check_site / write_code / stub_reach / hex_of).
 *
 * Talion's head, hood (helm) and hair are their own part renderables in his CSimpleModel (model+0xb8 + 8 x slot: 1, 3,
 * 4), each with its own render node; the eye passes' skeletal draws go through FUN_0081ff80, called from five list
 * builders (HP_DRAW_*), the shadow maps through FUN_0081fd10 (never touched). So:
 *   - hp_frame (game thread, H4, every call): on a stereo frame whose base is the head (not third person, not held
 *     away), the wanted slots' nodes of the player's model (every vtable checked, read again each frame: a rebuild on
 *     an outfit or equipment change is picked up the next frame) published for that renderer frame;
 *   - hp_draw (render thread, the five calls): while an eye renders (H3's eye match to its H8) and the call's node is
 *     one of that frame's head nodes, the call is skipped. Anything else is the game's call, unchanged.
 * The cloth (26.7: "main" left a white fur mantle at the shoulders): Havok cloth display meshes are custom render nodes
 * (kind 2) of their own, drawn by FUN_007ccec0 at eight calls of the same builders (HP_CUST_*): with "cloth" in the list
 * ("main" = body + cloth) hp_frame adds the model's cloth nodes (CSimpleModel+0x88 -> +0x138 -> entries -> visual +0x10
 * -> handle +0x10) to the set and hpc_draw skips them the same way; the shadow maps' three calls (pass 7) keep them.
 * Its own group: a cloth proof or call site that does not match = no cloth (a WARN), the rest of parts stays on.
 * SOMVR_HP_NEARLOG=1|CM (off by default): every node within 60 cm (or CM) of the head point that an eye pass still draws
 * through one of the 13 hooked calls is logged once (HEADPARTS near ...), for finding what else is left in the view.
 * Fail-soft: a site that does not match, a hook that does not go in = no parts (WARN) and SOMVR_HIDE_HEAD=near with
 * section 25's eye offset instead; at run time, the model there but no head node for HP_EMPTY_FRAMES stereo frames
 * running = the same fallback (a WARN, once). The hook-in points: hb_parse / hb_config / hb_start (the word, the
 * config), hv_config (the eye defaults), H4 (hp_frame), H1's near plane (hp_live), the trigger words (headparts LIST),
 * somvr_fini (hp_end_log). */

/* HEADPARTS_SITES_BEGIN (check_sites.py counts this block on its own: the 5 patched calls HP_DRAW_* (only with
 * SOMVR_HIDE_HEAD=parts), 6 read-only proofs, the 8 patched calls HP_CUST_* and the cloth's 10 read-only proofs:
 *   HP_DRAW_DEEP/FWD/LATE/GBUF/VEL  FUN_0081ff80(ctx, node, ...) in the deep-buffer, forward, late, G-buffer and
 *                   velocity builders FUN_00822a40 / 822c70 / 822eb0 / 8230d0 / 823760 (its only five calls);
 *   HP_SLOT_SET     CSimpleModel vtable slot 20 = vf_20(model, slot < 20, mesh): a part's renderable;
 *   HP_SLOT_STORE   ... which it keeps at model+0xb8 + 8 x slot (mov [rbx+rsi*8+0xb8], rbp);
 *   HP_HEAD_SLOT    FUN_023723f0 (the config onto the model): the Head model (config+0xa0) -> slot 1;
 *   HP_HELM_SLOT    ... the Helm, Talion's hood (config+0xb8, tags +0x140) -> slot 3;
 *   HP_HAIR_SLOT    ... the Hair (config+0xc0) -> slot 4;
 *   HP_SUBMIT_SHADOW FUN_0081fd10, the shadow maps' skeletal submitter (FUN_008229a0, FUN_00824570): not patched;
 *   HP_CUST_DEEP/FWD/LATE_A/LATE_B/GBUF_A/GBUF_B/VEL_A/VEL_B  FUN_007ccec0(dl, obj, node = rdx, 6, flag): the custom
 *                   (kind 2) nodes of the same five builders (all its pass-6 calls; the shadows' three pass 7);
 *   HP_CLOTH_STORE  CSimpleModel::vf_14: the CSimpleModelClothNode FUN_02356680 made -> model+0x88;
 *   HP_CLOTH_SET    FUN_0235d6d0 (its init): the cloth set (FUN_02b03450, 0x58 bytes) -> cloth node+0x138;
 *   HP_CLOTH_ENTRY  FUN_02b03560: a display mesh's entry {visual, material, hash, flags} appended (0x18 bytes);
 *   HP_CLOTH_HANDLE FUN_02b21a30: the renderer's vf_05 makes the visual's custom handle -> visual+0x10;
 *   HP_CUST_INLINE  CRendererHandle::vf_05: the handle (0x130 bytes) holds its CRenderNodeMeshCustom at +0x10;
 *   HP_CLOTH_VT, HP_VIS_VT, HP_CHANDLE_VT, HP_CNODE_VT  the four vtables walked (CSimpleModelClothNode slot 3,
 *                   CPhysicsClothVisualization slot 1, TRenderNodeMeshCustomHandle slot 1, CRenderNodeMeshCustom slot 1);
 *   HP_CUST_CREATE  CRendererHandle slot 5 = vf_05 (the only maker of custom handles).
 * Order = HPS_*) */
static const struct site_spec g_hp_sites_game[] = {
SITE("HP_DRAW_DEEP", K_CALL, 0x822bcc, 0x822bbd, "00 00 00 ba 01 00 00 00 4c 89 e7 44 89 14 24 e8 af d3 ff ff", 0x81ff80, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56 41"),
SITE("HP_DRAW_FWD", K_CALL, 0x822d19, 0x822d0a, "4d 89 e8 b9 01 00 00 00 4c 89 e7 44 89 14 24 e8 62 d2 ff ff", 0x81ff80, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56 41"),
SITE("HP_DRAW_LATE", K_CALL, 0x823068, 0x823059, "89 f8 b9 01 00 00 00 ba 01 00 00 00 4c 89 e7 e8 13 cf ff ff", 0x81ff80, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56 41"),
SITE("HP_DRAW_GBUF", K_CALL, 0x823591, 0x823582, "e8 31 c9 ba 01 00 00 00 4c 89 ef 48 83 c3 08 e8 ea c9 ff ff", 0x81ff80, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56 41"),
SITE("HP_DRAW_VEL", K_CALL, 0x823a69, 0x823a5a, "00 00 00 4c 89 e7 0f 29 44 24 10 0f 29 14 24 e8 12 c5 ff ff", 0x81ff80, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56 41"),
SITE("HP_SLOT_SET", K_VSLOT, 0x38d8368, 0x38d8360, "20 32 35 02 00 00 00 00 60 7e 35 02 00 00 00 00 d0 82 35 02 00 00 00 00", 0x2357e60, "83 fe 13 0f 87 47 02 00 00 41 57 41 56 41 55 41"),
SITE("HP_SLOT_STORE", K_ENTRY, 0x235801b, 0x235801b, "44 89 ee 48 8b 84 f3 b8 00 00 00 4c 8d a4 f3 b8 00 00 00 48 39 e8 0f 84 c9 00 00 00 8b 55 38 83 fa ff 75 0b 48 c7 05 86 ca 7f 02 f0 22 35 03 83 c2 01 89 55 38 48 89 ac f3 b8 00 00 00", 0x235801b, "44 89 ee 48 8b 84 f3 b8 00 00 00 4c 8d a4 f3"),
SITE("HP_HEAD_SLOT", K_ENTRY, 0x2372589, 0x2372589, "48 8b 93 a0 00 00 00 48 85 d2 74 11 49 8b 07 be 01 00 00 00 4c 89 ff ff 90 a0 00 00 00", 0x2372589, "48 8b 93 a0 00 00 00 48 85 d2 74 11 49 8b 07"),
SITE("HP_HELM_SLOT", K_ENTRY, 0x2372d38, 0x2372d38, "49 8b 07 be 03 00 00 00 4c 89 ff ff 90 a0 00 00 00 89 e8 be 03 00 00 00 4c 89", 0x2372d38, "49 8b 07 be 03 00 00 00 4c 89 ff ff 90 a0 00"),
SITE("HP_HAIR_SLOT", K_ENTRY, 0x2372bb8, 0x2372bb8, "48 8b 93 c0 00 00 00 48 85 d2 0f 84 e6 fa ff ff 49 8b 07 4c 89 ff be 04 00 00 00", 0x2372bb8, "48 8b 93 c0 00 00 00 48 85 d2 0f 84 e6 fa ff"),
SITE("HP_SUBMIT_SHADOW", K_ENTRY, 0x81fd10, 0x81fd10, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56 41", 0x81fd10, "41 57 66 0f ef e4 66 0f ef ed 49 89 f7 41 56"),
SITE("HP_CUST_DEEP", K_CALL, 0x822b17, 0x822b0c, "48 89 fa b9 06 00 00 00 4c 89 ef e8 a4 a3 fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_FWD", K_CALL, 0x822d97, 0x822d8c, "48 89 fa b9 06 00 00 00 4c 89 ef e8 24 a1 fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_LATE_A", K_CALL, 0x822f80, 0x822f6b, "48 89 fa b9 06 00 00 00 4c 89 ff 48 83 c3 08 41 b8 02 00 00 00 e8 3b 9f fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_LATE_B", K_CALL, 0x822fde, 0x822fd0, "48 89 fa 45 31 c0 b9 06 00 00 00 4c 89 ff e8 dd 9e fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_GBUF_A", K_CALL, 0x82347b, 0x82346a, "48 89 fa 41 b8 02 00 00 00 b9 06 00 00 00 48 89 ef e8 40 9a fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_GBUF_B", K_CALL, 0x82361e, 0x823610, "48 89 fa 45 31 c0 b9 06 00 00 00 48 89 ef e8 9d 98 fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_VEL_A", K_CALL, 0x823902, 0x8238f1, "48 89 fa 41 b8 02 00 00 00 b9 06 00 00 00 4c 89 ef e8 b9 95 fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CUST_VEL_B", K_CALL, 0x823a8a, 0x823a7c, "48 89 fa 45 31 c0 b9 06 00 00 00 4c 89 ef e8 31 94 fa ff", 0x7ccec0, "48 8b 06 48 85 c0 0f 84 8c 00 00 00 48 8b 30 48"),
SITE("HP_CLOTH_STORE", K_ENTRY, 0x23597c8, 0x23597c8, "e8 b3 ce ff ff 48 8b 44 24 18 48 8b bb 88 00 00 00 48 39 f8 74 41 48 85 c0 74 0c f0 83 40 08 01 48 8b bb 88 00 00 00 48 89 83 88 00 00 00", 0x23597c8, "e8 b3 ce ff ff 48 8b 44 24 18 48 8b bb 88 00"),
SITE("HP_CLOTH_SET", K_ENTRY, 0x235d6ff, 0x235d6ff, "e8 4c 5d 7a 00 48 89 ab 38 01 00 00", 0x235d6ff, "e8 4c 5d 7a 00 48 89 ab 38 01 00 00"),
SITE("HP_CLOTH_ENTRY", K_ENTRY, 0x2b0373e, 0x2b0373e, "48 8d 42 18 4c 89 74 24 10 f3 0f 7e 44 24 10 48 89 45 08 8b 44 24 18 0f 16 44 24 08 89 5a 10 0f 11 02", 0x2b0373e, "48 8d 42 18 4c 89 74 24 10 f3 0f 7e 44 24 10"),
SITE("HP_CLOTH_HANDLE", K_ENTRY, 0x2b21c9b, 0x2b21c9b, "ff 90 d8 00 00 00 48 8b 08 48 8d 53 10 48 8d 74 24 30 48 89 c7 ff 51 28", 0x2b21c9b, "ff 90 d8 00 00 00 48 8b 08 48 8d 53 10 48 8d"),
SITE("HP_CUST_INLINE", K_ENTRY, 0x71cfd5, 0x71cfd5, "48 c7 00 08 b6 35 03 4c 8d 68 10 48 89 c3 4c 89 ef e8 a5 4c 00 00", 0x71cfd5, "48 c7 00 08 b6 35 03 4c 8d 68 10 48 89 c3 4c"),
SITE("HP_CLOTH_VT", K_VSLOT, 0x38d9f28, 0x38d9f20, "d0 9f 6f 00 00 00 00 00 70 d2 35 02 00 00 00 00 60 cc 35 02 00 00 00 00", 0x235d270, "48 83 bf 58 01 00 00 00 0f 84 32 02 00 00 41 54"),
SITE("HP_VIS_VT", K_VSLOT, 0x3b34830, 0x3b34828, "d0 14 b2 02 00 00 00 00 20 15 b2 02 00 00 00 00 00 00 00 00 00 00 00 00", 0x2b21520, "53 48 89 fb 48 c7 07 28 48 b3 03 e8 20 ff ff ff"),
SITE("HP_CHANDLE_VT", K_VSLOT, 0x335b7d0, 0x335b7c8, "80 d3 71 00 00 00 00 00 e0 d3 71 00 00 00 00 00 e0 85 70 00 00 00 00 00", 0x71d3e0, "53 80 bf 25 01 00 00 00 48 89 fb 48 c7 07 c8 b7"),
SITE("HP_CNODE_VT", K_VSLOT, 0x335eaf8, 0x335eaf0, "e0 df 71 00 00 00 00 00 90 e1 71 00 00 00 00 00 90 e8 71 00 00 00 00 00", 0x71e190, "53 48 89 fb e8 47 fe ff ff 48 89 df be 10 01 00"),
SITE("HP_CUST_CREATE", K_VSLOT, 0x335ca60, 0x335ca58, "d0 4f 71 00 00 00 00 00 b0 cf 71 00 00 00 00 00 90 52 71 00 00 00 00 00", 0x71cfb0, "41 56 bf 30 01 00 00 49 89 d6 41 55 41 54 49 89"),
};
/* HEADPARTS_SITES_END */
enum { HPS_DEEP, HPS_FWD, HPS_LATE, HPS_GBUF, HPS_VEL, HPS_SLOT_SET, HPS_SLOT_STORE, HPS_HEAD, HPS_HELM, HPS_HAIR,
       HPS_SHADOW, HPS_C_DEEP, HPS_C_FWD, HPS_C_LATE_A, HPS_C_LATE_B, HPS_C_GBUF_A, HPS_C_GBUF_B, HPS_C_VEL_A, HPS_C_VEL_B,
       HPS_CLOTH_STORE, HPS_CLOTH_SET, HPS_CLOTH_ENTRY, HPS_CLOTH_HANDLE, HPS_CUST_INLINE, HPS_CLOTH_VT, HPS_VIS_VT,
       HPS_CHANDLE_VT, HPS_CNODE_VT, HPS_CUST_CREATE, HPS_N };
#define HPC_N (HPS_C_VEL_B - HPS_C_DEEP + 1) /* the eight HP_CUST_* calls */
_Static_assert(sizeof g_hp_sites_game / sizeof g_hp_sites_game[0] == HPS_N, "head-parts sites");
_Static_assert((int)HPS_DEEP == (int)SOMVR_HP_DEEP && (int)HPS_FWD == (int)SOMVR_HP_FWD &&
		       (int)HPS_LATE == (int)SOMVR_HP_LATE && (int)HPS_GBUF == (int)SOMVR_HP_GBUF &&
		       (int)HPS_VEL == (int)SOMVR_HP_VEL && SOMVR_HP_PASSES == 5,
	       "the call sites = the passes");
static const struct site_spec *g_hp_sites = g_hp_sites_game; /* selftest: stand-ins */
#define SM_SLOTS 0x0b8         /* CSimpleModel: CSimpleModelRenderable *part[20] (HP_SLOT_STORE) */
#define SM_MAIN 0x0b0          /* CSimpleModel: the main renderable (run 12/headset 1: Talion's whole body; RT) */
#define HP_EMPTY_FRAMES 300     /* stereo frames with the model but no head node: the run-time fallback */
#define SM_CLOTH 0x088         /* CSimpleModel: its CSimpleModelClothNode (HP_CLOTH_STORE; 0 = a model without cloth) */
#define CN_SET 0x138           /* CSimpleModelClothNode: the cloth set (HP_CLOTH_SET): +0 the entries, +8 their end */
#define CN_DESC 0x170          /* ... its display-mesh descriptors (0x10c bytes: the name first), +0x178 their end */
#define CN_DESC_SIZE 0x10c
#define CS_ENTRY 0x18          /* a cloth set entry: +0 the CPhysicsClothVisualization (HP_CLOTH_ENTRY) */
#define VIS_HANDLE 0x10        /* CPhysicsClothVisualization: its TRenderNodeMeshCustomHandle (HP_CLOTH_HANDLE) */
#define CHANDLE_NODE 0x10      /* the custom handle embeds its CRenderNodeMeshCustom here (HP_CUST_INLINE) */
#define HPC_MESHES 8           /* display meshes per model read at most */
#define NODE_BOX 0x10          /* a render node's world box: lo +0x10, hi +0x20 (3 floats each; FUN_008225c0) */
#define NODE_KIND 0xb0         /* 0 rigid, 1 skeletal, 2 custom (re-resolution.md 2.2) */
#define NODE_CAT 0xb1          /* its pass category */
#define SK_POS 0x04c           /* a skeletal node: its position (FUN_0081ff80's LOD distance from the camera) */
#define SK_MESH 0x0c8          /* ... its mesh (FUN_0081ff80: the sections' table) */
#define SK_OVR_N 0x380         /* ... its material overrides (FUN_0081ff80: +0x378 the list, 0x10 bytes each) */
#define SK_POSE 0x0f0          /* ... the CSkeletalTransforms that animate it (also at +0xf8; run 034707's "who" lines) */
#define HP_SKT_VT 0x33606d8    /* CSkeletalTransforms' vtable (classes.tsv; non-PIE: fixed) */
#define HP_POSE_LINES 8        /* SOMVR_HP_POSE: distinct nodes logged at their first skip */
enum { HPC_SLOT_CLOTH = 3, HPC_SLOT_VIS = 1, HPC_SLOT_CHANDLE = 1, HPC_SLOT_CNODE = 1, HPC_SLOT_CREATE = 5 };
#define HP_NEAR_LINES 64       /* SOMVR_HP_NEARLOG: distinct nodes logged at most */
#define HP_NEAR_BIG_CM 500.0f  /* ... a box wider than this on any axis is the world's (terrain, sky, water): not logged */
#define HP_XSKIP 4             /* the trigger word "hpskip ADDR": extra nodes not drawn in the eye passes (a test) */

void somvr_hpdeep_c(struct regs *r);
void somvr_hpfwd_c(struct regs *r);
void somvr_hplate_c(struct regs *r);
void somvr_hpgbuf_c(struct regs *r);
void somvr_hpvel_c(struct regs *r);
void somvr_w_hpdeep(void);
void somvr_w_hpfwd(void);
void somvr_w_hplate(void);
void somvr_w_hpgbuf(void);
void somvr_w_hpvel(void);
WRAPPER("somvr_w_hpdeep", "somvr_hpdeep_c");
WRAPPER("somvr_w_hpfwd", "somvr_hpfwd_c");
WRAPPER("somvr_w_hplate", "somvr_hplate_c");
WRAPPER("somvr_w_hpgbuf", "somvr_hpgbuf_c");
WRAPPER("somvr_w_hpvel", "somvr_hpvel_c");
static struct hook g_hp_h[SOMVR_HP_PASSES] = { { .name = "HP_DRAW_DEEP" }, { .name = "HP_DRAW_FWD" },
					       { .name = "HP_DRAW_LATE" }, { .name = "HP_DRAW_GBUF" },
					       { .name = "HP_DRAW_VEL" } };
/* the cloth: the eight FUN_007ccec0 calls (order = HPS_C_*), each with its builder's pass */
void somvr_hpcdeep_c(struct regs *r);
void somvr_hpcfwd_c(struct regs *r);
void somvr_hpclatea_c(struct regs *r);
void somvr_hpclateb_c(struct regs *r);
void somvr_hpcgbufa_c(struct regs *r);
void somvr_hpcgbufb_c(struct regs *r);
void somvr_hpcvela_c(struct regs *r);
void somvr_hpcvelb_c(struct regs *r);
void somvr_w_hpcdeep(void);
void somvr_w_hpcfwd(void);
void somvr_w_hpclatea(void);
void somvr_w_hpclateb(void);
void somvr_w_hpcgbufa(void);
void somvr_w_hpcgbufb(void);
void somvr_w_hpcvela(void);
void somvr_w_hpcvelb(void);
WRAPPER("somvr_w_hpcdeep", "somvr_hpcdeep_c");
WRAPPER("somvr_w_hpcfwd", "somvr_hpcfwd_c");
WRAPPER("somvr_w_hpclatea", "somvr_hpclatea_c");
WRAPPER("somvr_w_hpclateb", "somvr_hpclateb_c");
WRAPPER("somvr_w_hpcgbufa", "somvr_hpcgbufa_c");
WRAPPER("somvr_w_hpcgbufb", "somvr_hpcgbufb_c");
WRAPPER("somvr_w_hpcvela", "somvr_hpcvela_c");
WRAPPER("somvr_w_hpcvelb", "somvr_hpcvelb_c");
static struct hook g_hpc_h[HPC_N] = { { .name = "HP_CUST_DEEP" },   { .name = "HP_CUST_FWD" },    { .name = "HP_CUST_LATE_A" },
				      { .name = "HP_CUST_LATE_B" }, { .name = "HP_CUST_GBUF_A" }, { .name = "HP_CUST_GBUF_B" },
				      { .name = "HP_CUST_VEL_A" },  { .name = "HP_CUST_VEL_B" } };
static const int g_hpc_pass[HPC_N] = { SOMVR_HP_DEEP, SOMVR_HP_FWD,  SOMVR_HP_LATE, SOMVR_HP_LATE,
				       SOMVR_HP_GBUF, SOMVR_HP_GBUF, SOMVR_HP_VEL,  SOMVR_HP_VEL };

static somvr_hp_ring g_hp_ring;
/* game thread, except `on` (read by the render thread) */
static struct {
	int want;            /* SOMVR_HIDE_HEAD has the word parts (and the head base) */
	_Atomic int on;      /* the sites matched and the five calls are in: hp_frame publishes, the hooks skip */
	int fell_back;       /* parts gave up (start or run time): SOMVR_HIDE_HEAD=near since */
	int sites_ok;        /* the read-only proofs and the HS sites matched (once) */
	uint32_t slots;      /* the wanted slots (SOMVR_HEAD_PARTS, the trigger word headparts LIST) */
	uint32_t seen_req;
	uint32_t occ;        /* the slots with a renderable in the last model read (logged when it changes) */
	uintptr_t occ_model;
	uintptr_t model, node[SOMVR_HP_SLOTS], bad_rend[SOMVR_HP_SLOTS], main_node, bad_main;
	int n_weap;          /* the put-away weapons' nodes in the last set ("main") */
	int weap_why, weap_none_logged; /* the diagnostics: objects logged as not hidden, "none" logged */
	uintptr_t weap_bad[8];
	uint64_t frames, published, flat, nomodel, empty, changes, badvt, nodes;
	int empty_run, lines;
	double next_log;
} g_hp;
static uint64_t g_hp_main_hid; /* the last frame whose eye passes hid the body node (SOMVR_VRHANDS reads it at H4) */
/* SOMVR_HP_POSE=on|off (default on; the word "hppose on|off"): while the body node is hidden in the eyes, every skeletal
 * node animated by the body's own CSkeletalTransforms is too. Run 034707 found four of them besides the body (CModel
 * objects of one mesh at Talion's root, sharing his transforms; the weapons have their own): the wraith over Talion, whose
 * left hand the body edit leaves as a white glove in cover (the ivy by the stone wall: HiddenMatXFormON) and around the
 * bow hand. Read every frame from the body node (+0xf0, its vtable checked); the shadow maps keep them */
static _Atomic int g_hp_pose_on = 1;
static _Atomic uint64_t g_hp_pose_skips[SOMVR_HP_PASSES];
static uintptr_t g_hp_pose_seen[HP_POSE_LINES];
static _Atomic int g_hp_pose_n;
static int g_hp_pose_bad_lines;
static uintptr_t g_hp_pose_last;
/* SOMVR_HP_AWAY=off|hide|hands (default off = build 11; the word "hpaway off|hide|hands"; handoff/HANDOFF-wraith.md
 * round 2): a frame whose view is held away from Talion's body (the Shadow Strike from the press until the dash ends,
 * SOMVR_BOW_STRIKE; a body moment's hold, SOMVR_BODYMOMENTS: the strike's knockdown sync, a counter's lunge; the frame
 * at a teleport, on the camera base) published no set, so the eyes drew him whole (run 072205: the body, the wraith and
 * the bow 1.6 to 9 m ahead in the hold and the dash, and round the view through the sync). hide: such a frame hides him
 * as a first-person frame does (the body node whatever SOMVR_BODY drew, the pose's nodes, the cloth, the put-away
 * weapons), every skeletal node of one of his own meshes too (the body's and the ones the pose rule skipped: the
 * strike's wraith copy has transforms of its own, runs 072205 and 071430), and a weapon in a hand whose arm is not on our
 * controller (the arm to the animation: the weapon is with the body). hands: the same, and a body moment's hold away from
 * the body keeps the rig (fb_after_body), so the arms stay on the controllers (stretched, SOMVR_ARMS_STRETCH) and our VR
 * hands are drawn: the bow stays in our hand. The shadow maps keep everything. */
static const char *const g_hp_away_w[] = { "off", "hide", "hands" };
static _Atomic int g_hp_away = HP_AWAY_OFF;
static _Atomic uintptr_t g_hp_own_mesh[SOMVR_HP_MESHES]; /* the render thread: meshes the pose rule skipped (the wraith's) */
static _Atomic int g_hp_own_n;
static _Atomic uint64_t g_hp_mesh_skips[SOMVR_HP_PASSES];
static uintptr_t g_hp_mesh_seen[HP_POSE_LINES];
static _Atomic int g_hp_mesh_n;
#define HP_AWAY_HAND_CM 5.0f /* a weapon stays in a held-away frame while its hand's arm is on, this near its target */
static struct {
	int was;                  /* the last frame was held away */
	double t0;
	uint64_t frame0, frames, all, weap_hid, weap_kept, nomain;
	/* the last set (the strike's trace reads it) */
	uint64_t frame;
	int away, main, pose, hid, kept;
	uint32_t n, nmesh;
} g_hpa;
static int hp_away_mode(void) { return atomic_load_explicit(&g_hp_away, memory_order_relaxed); }
/* the cloth (game thread, except `on`) */
static struct {
	_Atomic int on;      /* the cloth proofs matched and the eight calls are in */
	uintptr_t vt_cloth, vt_vis, vt_chandle, vt_cnode;
	uintptr_t cnode, node[HPC_MESHES]; /* the last read: the model's cloth node, its meshes' nodes */
	int n, nv, bad;      /* ... how many nodes, entries (-1 = none read), entries without a node */
	uint64_t frames, nodes, none, badvt;
	int lines;
} g_hpc;
static _Atomic int g_hpc_first[HPC_N];
/* SOMVR_HP_NEARLOG (render thread; off = 0): cm around the frame's head point; the nodes logged */
static float g_hp_near_cm;
static uintptr_t g_hp_near_seen[HP_NEAR_LINES];
static _Atomic int g_hp_near_n;
static _Atomic uint64_t g_hp_near_hits, g_hp_near_big;
/* the trigger word "hpskip ADDR" / "hpskip off" (a test: is that node the thing seen?): drawn in no eye pass */
static _Atomic uintptr_t g_hp_xskip[HP_XSKIP];
static _Atomic uint64_t g_hp_xskips;
static _Atomic uint32_t g_req_hp, g_req_hp_seq; /* trigger word "headparts LIST": the new slots (writer thread) */
/* render thread: the set looked up for the frame its eyes render now (per thread: the builders run inside the eye's
 * RenderScene on the render thread, FUN_007dc0b0 and FUN_007d6e20 call them directly; a copy per thread keeps the cache
 * whole even if one ever ran on a worker) */
static __thread struct {
	uint64_t frame;
	int ok;
	somvr_hp_set set;
} g_hp_rt;
static _Atomic int g_hp_first[SOMVR_HP_PASSES];

static int hp_live(void) { return atomic_load_explicit(&g_hp.on, memory_order_relaxed); }
static int hpc_live(void) { return atomic_load_explicit(&g_hpc.on, memory_order_relaxed); }

/* the render thread's set for the frame its eye renders now (looked up once per frame); NULL = none */
static const somvr_hp_set *hp_rt_set(void)
{
	uint64_t fr = g_rt_frame;
	if (g_hp_rt.frame != fr) /* the first eye call of a frame: its set (published at that frame's H4) */
		g_hp_rt.ok = somvr_hp_lookup(&g_hp_ring, fr, &g_hp_rt.set), g_hp_rt.frame = fr;
	return g_hp_rt.ok ? &g_hp_rt.set : NULL;
}

/* a node's world box (render thread: the builder hands it over live); 0 = not a box */
static int hp_node_box(uintptr_t node, float lo[3], float hi[3])
{
	memcpy(lo, (const void *)(node + NODE_BOX), 12), memcpy(hi, (const void *)(node + NODE_BOX + 0x10), 12);
	return somvr_hp_box_dist(lo, hi, lo) >= 0;
}

/* SOMVR_HP_NEARLOG: a node an eye pass still draws, within g_hp_near_cm of the frame's head point: logged once. A custom
 * or rigid node by its world box (+0x10); a skeletal one keeps no box there (run 032059: never logged), so by its
 * position +0x4c (FUN_0081ff80's LOD distance), with its mesh (+0xc8) beside the body's: a node drawing the body's
 * mesh is a copy of Talion (an overlay, a ghost) */
static void hp_near(const somvr_hp_set *s, const char *site, int pass, uintptr_t node, int eye)
{
	float lo[3], hi[3], c[3], d;
	if (!s || !node || (s->at[0] == 0 && s->at[1] == 0 && s->at[2] == 0))
		return;
	const uint8_t *f = (const uint8_t *)node;
	int box = hp_node_box(node, lo, hi), skel = !box && f[NODE_KIND] == 1;
	if (box) {
		d = somvr_hp_box_dist(lo, hi, s->at);
		if (d < 0 || d > g_hp_near_cm)
			return;
		if (hi[0] - lo[0] > HP_NEAR_BIG_CM || hi[1] - lo[1] > HP_NEAR_BIG_CM || hi[2] - lo[2] > HP_NEAR_BIG_CM) {
			atomic_fetch_add_explicit(&g_hp_near_big, 1, memory_order_relaxed); /* run 013938: 32 world boxes filled it */
			return;
		}
	} else if (skel) {
		memcpy(c, (const void *)(node + SK_POS), sizeof c);
		if (!isfinite(c[0]) || !isfinite(c[1]) || !isfinite(c[2]))
			return;
		d = sqrtf((c[0] - s->at[0]) * (c[0] - s->at[0]) + (c[1] - s->at[1]) * (c[1] - s->at[1]) +
			  (c[2] - s->at[2]) * (c[2] - s->at[2]));
		if (d > g_hp_near_cm)
			return;
	} else {
		return;
	}
	atomic_fetch_add_explicit(&g_hp_near_hits, 1, memory_order_relaxed);
	int n = atomic_load_explicit(&g_hp_near_n, memory_order_relaxed);
	for (int i = 0; i < n && i < HP_NEAR_LINES; i++)
		if (g_hp_near_seen[i] == node)
			return;
	if (n >= HP_NEAR_LINES)
		return;
	g_hp_near_seen[n] = node;
	atomic_store_explicit(&g_hp_near_n, n + 1, memory_order_relaxed);
	uintptr_t body = 0;
	for (uint32_t i = 0; i < s->n; i++)
		if (s->slot[i] == SOMVR_HP_MAIN)
			body = s->node[i];
	char what[200];
	if (skel) {
		uintptr_t mesh = *(const uintptr_t *)(node + SK_MESH), bmesh = body ? *(const uintptr_t *)(body + SK_MESH) : 0;
		snprintf(what, sizeof what, "position (%.1f %.1f %.1f), mesh +0xc8 %#lx (%s), material overrides %d", c[0], c[1],
			 c[2], (unsigned long)mesh, !body ? "no body node" : mesh == bmesh ? "THE BODY'S" : "not the body's",
			 *(const int *)(node + SK_OVR_N));
	} else {
		snprintf(what, sizeof what, "box (%.1f %.1f %.1f)-(%.1f %.1f %.1f) size %.0f x %.0f x %.0f", lo[0], lo[1], lo[2],
			 hi[0], hi[1], hi[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
	}
	logf_("HEADPARTS near: node %#lx (vtable %#lx, kind %u, category %u, flags +0x40 %02x %02x %02x) drawn in the %s pass "
	      "(%s) %.1f cm from the head point, %s, frame %llu eye %d t=%.3f", (unsigned long)node,
	      (unsigned long)*(const uintptr_t *)node, f[NODE_KIND], f[NODE_CAT], f[0x40], f[0x41], f[0x42],
	      somvr_hp_pass_name(pass), site, d, what, (unsigned long long)s->frame, eye, mono());
}

/* the trigger word "hpscan" (a test; also once per new model while SOMVR_HP_NEARLOG is on): Talion's model's renderables
 * beyond +0xb0 / +0xb8: every qword of the model's first HP_SCAN bytes that is a CSimpleModelRenderable (by its vtable),
 * or points to an array holding some, logged with its node and the node's mesh */
#define HP_SCAN 0x400
static _Atomic int g_req_hpscan;
static uintptr_t hp_node_of(uintptr_t rend);
static void hp_scan(uintptr_t model, const char *why)
{
	uintptr_t q[HP_SCAN / 8];
	if (!peek(model, q, sizeof q)) {
		logf_("HEADPARTS scan (%s): the model %#lx is not readable", why, (unsigned long)model);
		return;
	}
	int n = 0;
	for (int i = 0; i < HP_SCAN / 8; i++) {
		uintptr_t r = q[i], a[8];
		if (r < 0x10000 || (r & 7))
			continue;
		if (vptr_of((void *)r) == g_hh.vt_rend) {
			uintptr_t nd = hp_node_of(r);
			logf_("HEADPARTS scan: model+%#x = renderable %#lx, node %#lx, mesh %#lx", i * 8, (unsigned long)r,
			      (unsigned long)nd, (unsigned long)(nd ? peek_ptr(nd + SK_MESH) : 0));
			n++;
			continue;
		}
		if (!peek(r, a, sizeof a))
			continue;
		for (int k = 0; k < 8; k++)
			if (a[k] >= 0x10000 && !(a[k] & 7) && vptr_of((void *)a[k]) == g_hh.vt_rend) {
				uintptr_t nd = hp_node_of(a[k]);
				logf_("HEADPARTS scan: model+%#x -> [%d] = renderable %#lx, node %#lx, mesh %#lx", i * 8, k,
				      (unsigned long)a[k], (unsigned long)nd, (unsigned long)(nd ? peek_ptr(nd + SK_MESH) : 0));
				n++;
			}
	}
	logf_("HEADPARTS scan (%s): model %#lx: %d renderable(s) in its first %#x bytes (main +0xb0 %#lx)", why,
	      (unsigned long)model, n, HP_SCAN, (unsigned long)peek_ptr(model + SM_MAIN));
}

/* the trigger word "hpskip ADDR" (render thread reads): this node is drawn in no eye pass; 1 = one of them */
static int hp_xskip(uintptr_t node)
{
	for (int i = 0; i < HP_XSKIP; i++)
		if (node && atomic_load_explicit(&g_hp_xskip[i], memory_order_relaxed) == node) {
			atomic_fetch_add_explicit(&g_hp_xskips, 1, memory_order_relaxed);
			return 1;
		}
	return 0;
}

/* "hpskip ADDR" logs who the node is: every qword of node-0x10 (its handle) .. +0x400 pointing at an object whose vtable
 * lies in the game's .rodata (a class: classes.tsv names it), at most 32 lines */
static void hp_who(uintptr_t node)
{
	uintptr_t q[(0x400 + 0x10) / 8];
	if (!peek(node - 0x10, q, sizeof q)) {
		logf_("HEADPARTS who %#lx: not readable", (unsigned long)node);
		return;
	}
	int n = 0;
	for (int i = 0; i < (int)(sizeof q / 8) && n < 32; i++) {
		uintptr_t v = q[i] >= 0x10000 && !(q[i] & 7) ? vptr_of((void *)q[i]) : 0;
		if (i == 0 || (v >= 0x3300000 && v < 0x3d00000)) {
			logf_("HEADPARTS who %#lx: %s%#x = %#lx%s (vtable %#lx)", (unsigned long)node, i < 2 ? "-" : "+",
			      i < 2 ? 0x10 - i * 8 : i * 8 - 0x10, (unsigned long)q[i], i == 0 ? " = the handle's vtable" : "",
			      (unsigned long)(i == 0 ? q[0] : v));
			n++;
		}
	}
}

/* "hpskip ADDR" (up to HP_XSKIP; the oldest goes) / "hpskip off" (writer thread); the word's note */
static const char *hp_xskip_word(const char *w)
{
	static int next;
	while (*w == ' ')
		w++;
	if (!strncmp(w, "off", 3)) {
		for (int i = 0; i < HP_XSKIP; i++)
			atomic_store(&g_hp_xskip[i], 0);
		logf_("HEADPARTS hpskip off (%llu draws skipped)", (unsigned long long)atomic_load(&g_hp_xskips));
		return "hpskip off";
	}
	char *e;
	unsigned long long a = strtoull(w, &e, 16);
	if (e == w || a < 0x10000)
		return "hpskip (not a node address: ignored)";
	hp_who((uintptr_t)a);
	atomic_store(&g_hp_xskip[next], (uintptr_t)a);
	next = (next + 1) % HP_XSKIP;
	logf_("HEADPARTS hpskip %#llx: not drawn in the eye passes from now on (a test)", a);
	return "hpskip";
}

/* SOMVR_HP_POSE (render thread): a node's first skip logged (HP_POSE_LINES distinct nodes at most) */
static void hp_pose_first(int pass, uintptr_t node, uint64_t fr, int eye)
{
	int n = atomic_load_explicit(&g_hp_pose_n, memory_order_relaxed);
	for (int i = 0; i < n && i < HP_POSE_LINES; i++)
		if (g_hp_pose_seen[i] == node)
			return;
	if (n >= HP_POSE_LINES)
		return;
	g_hp_pose_seen[n] = node;
	atomic_store_explicit(&g_hp_pose_n, n + 1, memory_order_relaxed);
	const uint8_t *f = (const uint8_t *)node;
	uintptr_t mesh = *(const uintptr_t *)(node + SK_MESH); /* SOMVR_HP_AWAY: one of Talion's own meshes */
	int nm = atomic_load_explicit(&g_hp_own_n, memory_order_relaxed), known = 0;
	for (int i = 0; i < nm && i < SOMVR_HP_MESHES; i++)
		known |= atomic_load_explicit(&g_hp_own_mesh[i], memory_order_relaxed) == mesh;
	if (mesh && !known && nm < SOMVR_HP_MESHES) {
		atomic_store_explicit(&g_hp_own_mesh[nm], mesh, memory_order_relaxed);
		atomic_store_explicit(&g_hp_own_n, nm + 1, memory_order_release);
	}
	logf_("HEADPARTS pose: node %#lx (mesh %#lx, category %u) shares the hidden body's skeletal transforms %#lx: not drawn "
	      "in the %s pass (frame %llu eye %d; the first skip of this node)", (unsigned long)node,
	      (unsigned long)*(const uintptr_t *)(node + SK_MESH), f[NODE_CAT], (unsigned long)*(const uintptr_t *)(node + SK_POSE),
	      somvr_hp_pass_name(pass), (unsigned long long)fr, eye);
}

/* "hppose on|off" (writer thread); the word's note */
static const char *hp_pose_word(const char *w)
{
	while (*w == ' ')
		w++;
	int on = !strncmp(w, "on", 2) ? 1 : !strncmp(w, "off", 3) ? 0 : -1;
	if (on < 0)
		return "hppose (not on|off: ignored)";
	atomic_store(&g_hp_pose_on, on);
	logf_("HEADPARTS hppose %s: the nodes of the hidden body's pose %s in the eyes from the next frame", on ? "on" : "off",
	      on ? "are not drawn" : "are drawn again");
	return on ? "hppose on" : "hppose off";
}

/* SOMVR_HP_AWAY (render thread): a node's first skip by its mesh logged (HP_POSE_LINES distinct nodes at most) */
static void hp_mesh_first(int pass, uintptr_t node, uint64_t fr, int eye)
{
	int n = atomic_load_explicit(&g_hp_mesh_n, memory_order_relaxed);
	for (int i = 0; i < n && i < HP_POSE_LINES; i++)
		if (g_hp_mesh_seen[i] == node)
			return;
	if (n >= HP_POSE_LINES)
		return;
	g_hp_mesh_seen[n] = node;
	atomic_store_explicit(&g_hp_mesh_n, n + 1, memory_order_relaxed);
	const uint8_t *f = (const uint8_t *)node;
	logf_("HEADPARTS away: node %#lx (mesh %#lx, category %u, its own transforms %#lx) is one of Talion's meshes: not drawn in "
	      "the %s pass of a held-away frame (frame %llu eye %d; the first skip of this node)", (unsigned long)node,
	      (unsigned long)*(const uintptr_t *)(node + SK_MESH), f[NODE_CAT], (unsigned long)*(const uintptr_t *)(node + SK_POSE),
	      somvr_hp_pass_name(pass), (unsigned long long)fr, eye);
}

/* "hpaway off|hide|hands" (writer thread); the word's note */
static const char *hp_away_word(const char *w)
{
	while (*w == ' ')
		w++;
	int m = -1;
	for (int i = 0; i < 3 && m < 0; i++)
		if (!strncmp(w, g_hp_away_w[i], strlen(g_hp_away_w[i])))
			m = i;
	if (m < 0)
		return "hpaway (not off|hide|hands: ignored)";
	atomic_store(&g_hp_away, m);
	logf_("HEADPARTS hpaway %s (the word): a frame held away from Talion %s from the next frame", g_hp_away_w[m],
	      m == HP_AWAY_OFF ? "draws him (build 11)" : m == HP_AWAY_HIDE ? "hides him" : "hides him, our hands stay ours");
	return m == HP_AWAY_OFF ? "hpaway off" : m == HP_AWAY_HIDE ? "hpaway hide" : "hpaway hands";
}

typedef uint64_t (*hp_sub_fn)(uint64_t ctx, uint64_t node, uint64_t a, uint64_t b, uint64_t list, uint64_t pos);

/* the five calls (render thread): FUN_0081ff80(ctx = rdi, node = rsi, dl, cl, r8, r9), no stack arguments and no
 * return value read by its callers. Skipped for this frame's head nodes while an eye renders */
static void hp_draw(struct regs *r, int pass)
{
	struct hook *h = &g_hp_h[pass];
	atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed);
	int eye = g_rt_eye;
	if (eye >= 0 && hp_live()) {
		uint64_t fr = g_rt_frame;
		const somvr_hp_set *set = hp_rt_set();
		if (hp_xskip((uintptr_t)r->rsi)) {
			r->rax = 0;
			return;
		}
		if (set && set->pose && somvr_hp_skip_pose(eye, pass, set, fr, *(const uintptr_t *)(r->rsi + SK_POSE)) &&
		    somvr_hp_match(set, (uintptr_t)r->rsi) < 0) { /* SOMVR_HP_POSE: not the body itself (counted below) */
			atomic_fetch_add_explicit(&g_hp_pose_skips[pass], 1, memory_order_relaxed);
			hp_pose_first(pass, (uintptr_t)r->rsi, fr, eye);
			r->rax = 0;
			return;
		}
		if (set && set->away && somvr_hp_skip_mesh(eye, pass, set, fr, *(const uintptr_t *)(r->rsi + SK_MESH)) &&
		    somvr_hp_match(set, (uintptr_t)r->rsi) < 0) { /* SOMVR_HP_AWAY: Talion's own mesh, transforms of its own */
			atomic_fetch_add_explicit(&g_hp_mesh_skips[pass], 1, memory_order_relaxed);
			hp_mesh_first(pass, (uintptr_t)r->rsi, fr, eye);
			r->rax = 0;
			return;
		}
		if (somvr_hp_skip(eye, pass, set, fr, (uintptr_t)r->rsi)) {
			atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
			if (!atomic_exchange(&g_hp_first[pass], 1)) {
				int i = somvr_hp_match(set, (uintptr_t)r->rsi);
				logf_("HEADPARTS first skip in the %s pass (%s): node %#lx = slot %d (%s), frame %llu eye %d: not drawn",
				      somvr_hp_pass_name(pass), h->name, (unsigned long)r->rsi, set->slot[i],
				      somvr_hp_slot_name(set->slot[i]), (unsigned long long)fr, eye);
			}
			r->rax = 0;
			return;
		}
		if (g_hp_near_cm > 0)
			hp_near(set, h->name, pass, (uintptr_t)r->rsi, eye);
	}
	r->rax = ((hp_sub_fn)h->orig)(r->rdi, r->rsi, r->rdx, r->rcx, r->r8, r->r9);
}
__attribute__((used)) void somvr_hpdeep_c(struct regs *r) { hp_draw(r, SOMVR_HP_DEEP); }
__attribute__((used)) void somvr_hpfwd_c(struct regs *r) { hp_draw(r, SOMVR_HP_FWD); }
__attribute__((used)) void somvr_hplate_c(struct regs *r) { hp_draw(r, SOMVR_HP_LATE); }
__attribute__((used)) void somvr_hpgbuf_c(struct regs *r) { hp_draw(r, SOMVR_HP_GBUF); }
__attribute__((used)) void somvr_hpvel_c(struct regs *r) { hp_draw(r, SOMVR_HP_VEL); }

typedef void (*hpc_sub_fn)(uint64_t dl, uint64_t obj, uint64_t node, uint64_t pass, uint64_t flag);

/* the eight custom calls (render thread): FUN_007ccec0(dl = rdi, obj = rsi, node = rdx, pass = ecx 6, flag = r8d), no
 * stack arguments, no return value. Skipped for this frame's cloth nodes while an eye renders */
static void hpc_draw(struct regs *r, int site)
{
	struct hook *h = &g_hpc_h[site];
	int pass = g_hpc_pass[site], eye = g_rt_eye;
	atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed);
	if (eye >= 0 && hp_live() && hpc_live()) {
		uint64_t fr = g_rt_frame;
		const somvr_hp_set *set = hp_rt_set();
		if (hp_xskip((uintptr_t)r->rdx)) {
			r->rax = 0;
			return;
		}
		if (somvr_hp_skip(eye, pass, set, fr, (uintptr_t)r->rdx)) {
			atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
			if (!atomic_exchange(&g_hpc_first[site], 1)) {
				int i = somvr_hp_match(set, (uintptr_t)r->rdx);
				float lo[3], hi[3];
				int box = hp_node_box((uintptr_t)r->rdx, lo, hi);
				logf_("HEADPARTS first skip in the %s pass (%s): node %#lx = %s (kind %u, category %u, box %.0f x %.0f x "
				      "%.0f, %.1f cm from the head point), frame %llu eye %d: not drawn", somvr_hp_pass_name(pass), h->name,
				      (unsigned long)r->rdx, somvr_hp_slot_name(set->slot[i]), *(const uint8_t *)(r->rdx + NODE_KIND),
				      *(const uint8_t *)(r->rdx + NODE_CAT), box ? hi[0] - lo[0] : -1.0f, box ? hi[1] - lo[1] : -1.0f,
				      box ? hi[2] - lo[2] : -1.0f, box ? somvr_hp_box_dist(lo, hi, set->at) : -1.0f,
				      (unsigned long long)fr, eye);
			}
			r->rax = 0;
			return;
		}
		if (g_hp_near_cm > 0)
			hp_near(set, h->name, pass, (uintptr_t)r->rdx, eye);
	}
	((hpc_sub_fn)h->orig)(r->rdi, r->rsi, r->rdx, r->rcx, r->r8);
	r->rax = 0;
}
__attribute__((used)) void somvr_hpcdeep_c(struct regs *r) { hpc_draw(r, 0); }
__attribute__((used)) void somvr_hpcfwd_c(struct regs *r) { hpc_draw(r, 1); }
__attribute__((used)) void somvr_hpclatea_c(struct regs *r) { hpc_draw(r, 2); }
__attribute__((used)) void somvr_hpclateb_c(struct regs *r) { hpc_draw(r, 3); }
__attribute__((used)) void somvr_hpcgbufa_c(struct regs *r) { hpc_draw(r, 4); }
__attribute__((used)) void somvr_hpcgbufb_c(struct regs *r) { hpc_draw(r, 5); }
__attribute__((used)) void somvr_hpcvela_c(struct regs *r) { hpc_draw(r, 6); }
__attribute__((used)) void somvr_hpcvelb_c(struct regs *r) { hpc_draw(r, 7); }

/* a part renderable's render node: its vtable CSimpleModelRenderable's, +0x50 the skeletal handle, +0x10 the node (the
 * vtables of HS_REND_VT, HS_HANDLE, HS_NODE); 0 = none (no handle yet: the mesh is still loading; another type) */
static uintptr_t hp_node_of(uintptr_t rend)
{
	if (!rend || vptr_of((void *)rend) != g_hh.vt_rend)
		return 0;
	uintptr_t hd = peek_ptr(rend + REND_HANDLE);
	if (!hd || vptr_of((void *)hd) != g_hh.vt_handle || vptr_of((void *)(hd + HANDLE_NODE)) != g_hh.vt_node)
		return 0;
	return hd + HANDLE_NODE;
}

static void hv_parts_fallback(void); /* the head-view region: section 25's eye offset where not set explicitly */

/* parts off for the rest of the run: SOMVR_HIDE_HEAD=near (and section 25's eye offset) instead */
static void hp_fall_back(const char *why)
{
	atomic_store_explicit(&g_hp.on, 0, memory_order_relaxed);
	if (g_hp.fell_back)
		return;
	g_hp.fell_back = 1;
	g_hb.near = g_rigcfg.base == SOMVR_BASE_HEAD;
	logf_("WARN SOMVR_HIDE_HEAD=parts: %s: Talion's head is drawn again; falling back to near (the head plane%s)", why,
	      g_hb.near ? " and section 25's eye offset where not set" : ": off, not the head base");
	if (g_hb.near)
		hv_parts_fallback();
}

/* the trigger word "headparts LIST" (writer thread): the wanted slots from the next frame on; 1 = taken */
static int hp_word(const char *w)
{
	uint32_t m;
	if (somvr_hp_parse_slots(w, &m))
		return 0;
	atomic_store_explicit(&g_req_hp, m, memory_order_relaxed);
	atomic_fetch_add_explicit(&g_req_hp_seq, 1, memory_order_release);
	return 1;
}

/* the cloth's counters per pass (a builder's two calls summed): "cloth on skipped/calls deep=a/b ..." */
static void hpc_counts(char *out, size_t cap)
{
	uint64_t m[SOMVR_HP_PASSES] = { 0 }, c[SOMVR_HP_PASSES] = { 0 };
	for (int i = 0; i < HPC_N; i++)
		m[g_hpc_pass[i]] += atomic_load(&g_hpc_h[i].mods), c[g_hpc_pass[i]] += atomic_load(&g_hpc_h[i].calls);
	size_t o = (size_t)snprintf(out, cap, "cloth %s skipped/calls deep=%llu/%llu forward=%llu/%llu late=%llu/%llu "
				    "gbuffer=%llu/%llu velocity=%llu/%llu, cloth nodes %d (frames %llu, without %llu)",
				    hpc_live() ? "on" : "off", (unsigned long long)m[0], (unsigned long long)c[0],
				    (unsigned long long)m[1], (unsigned long long)c[1], (unsigned long long)m[2],
				    (unsigned long long)c[2], (unsigned long long)m[3], (unsigned long long)c[3],
				    (unsigned long long)m[4], (unsigned long long)c[4], g_hpc.n, (unsigned long long)g_hpc.frames,
				    (unsigned long long)g_hpc.none);
	if (o < cap)
		o += (size_t)snprintf(out + o, cap - o, "; pose %s skipped deep=%llu forward=%llu late=%llu gbuffer=%llu "
				      "velocity=%llu (%d nodes)", atomic_load(&g_hp_pose_on) ? "on" : "off",
				      (unsigned long long)atomic_load(&g_hp_pose_skips[0]),
				      (unsigned long long)atomic_load(&g_hp_pose_skips[1]),
				      (unsigned long long)atomic_load(&g_hp_pose_skips[2]),
				      (unsigned long long)atomic_load(&g_hp_pose_skips[3]),
				      (unsigned long long)atomic_load(&g_hp_pose_skips[4]), atomic_load(&g_hp_pose_n));
	if (g_hp_near_cm > 0 && o < cap)
		snprintf(out + o, cap - o, "; near-log hits %llu (%d nodes; world boxes %llu)",
			 (unsigned long long)atomic_load(&g_hp_near_hits), atomic_load(&g_hp_near_n),
			 (unsigned long long)atomic_load(&g_hp_near_big));
}

static void hp_log_line(double t, uint64_t frame, const somvr_hp_set *s)
{
	char cl[512];
	hpc_counts(cl, sizeof cl);
	char ns[256] = "";
	size_t o = 0;
	for (uint32_t i = 0; i < s->n && o + 40 < sizeof ns; i++)
		o += (size_t)snprintf(ns + o, sizeof ns - o, "%s%s=%#lx", i ? " " : "", somvr_hp_slot_name(s->slot[i]),
				      (unsigned long)s->node[i]);
	logf_("HEADPARTS t=%.3f frame=%llu model=%#lx nodes %s; skipped/calls deep=%llu/%llu forward=%llu/%llu late=%llu/%llu "
	      "gbuffer=%llu/%llu velocity=%llu/%llu; frames published %llu flat %llu no-model %llu empty %llu bad-vtable %llu; %s",
	      t, (unsigned long long)frame, (unsigned long)g_hp.model, s->n ? ns : "none",
	      (unsigned long long)atomic_load(&g_hp_h[0].mods), (unsigned long long)atomic_load(&g_hp_h[0].calls),
	      (unsigned long long)atomic_load(&g_hp_h[1].mods), (unsigned long long)atomic_load(&g_hp_h[1].calls),
	      (unsigned long long)atomic_load(&g_hp_h[2].mods), (unsigned long long)atomic_load(&g_hp_h[2].calls),
	      (unsigned long long)atomic_load(&g_hp_h[3].mods), (unsigned long long)atomic_load(&g_hp_h[3].calls),
	      (unsigned long long)atomic_load(&g_hp_h[4].mods), (unsigned long long)atomic_load(&g_hp_h[4].calls),
	      (unsigned long long)g_hp.published, (unsigned long long)g_hp.flat, (unsigned long long)g_hp.nomodel,
	      (unsigned long long)g_hp.empty, (unsigned long long)g_hp.badvt, cl);
}

/* H4 (game thread), every call: live = this frame renders eyes on the head base (not third person, not held away).
 * Publishes the frame's head nodes; a flat frame publishes nothing (the head is drawn) */
/* "main": the put-away weapons' models too; each looked up once per game object (hide_model_of reads /proc/self/maps for a
 * new object), a failed one again after 2 s */
#define HP_WC_N 8
static struct {
	uintptr_t go, model;
	double t;
} g_hp_wc[HP_WC_N];
static int g_hp_wc_next;
static uintptr_t hp_weapon_model(uintptr_t go, double t)
{
	for (int i = 0; i < HP_WC_N; i++)
		if (g_hp_wc[i].go == go) {
			if (g_hp_wc[i].model)
				return vptr_of((void *)g_hp_wc[i].model) == g_hh.vt_model ? g_hp_wc[i].model : 0;
			if (t - g_hp_wc[i].t < 2.0)
				return 0;
			g_hp_wc[i].model = hide_model_of(go), g_hp_wc[i].t = t;
			return g_hp_wc[i].model;
		}
	int k = g_hp_wc_next;
	g_hp_wc_next = (k + 1) % HP_WC_N;
	g_hp_wc[k].go = go, g_hp_wc[k].model = hide_model_of(go), g_hp_wc[k].t = t;
	return g_hp_wc[k].model;
}

/* the put-away weapons' nodes into the set (their main renderable and any part slots); how many went in */
static int hp_add_weapons(somvr_hp_set *s, double t, int body_hidden)
{
	uintptr_t wo[6];
	int nwo = sl_away_objs(wo, 6, body_hidden), nw = 0;
	for (int i = 0; i < nwo; i++) {
		uintptr_t wm = hp_weapon_model(wo[i], t), rend[SOMVR_HP_SLOTS], mr = wm ? peek_ptr(wm + SM_MAIN) : 0;
		int got = 0;
		if (wm) {
			got += somvr_hp_set_add(s, hp_node_of(mr), SOMVR_HP_WEAPON);
			if (peek(wm + SM_SLOTS, rend, sizeof rend))
				for (int k = 0; k < SOMVR_HP_SLOTS; k++)
					if (rend[k])
						got += somvr_hp_set_add(s, hp_node_of(rend[k]), SOMVR_HP_WEAPON);
		}
		if (!got && g_hp.weap_why < 8) { /* run 14: none ever went in - which link fails (once per object, 8 at most) */
			int seen = 0;
			for (int j = 0; j < g_hp.weap_why && !seen; j++)
				seen = g_hp.weap_bad[j] == wo[i];
			if (!seen) {
				g_hp.weap_bad[g_hp.weap_why++] = wo[i];
				uintptr_t mi = peek_ptr(wo[i] + OBJ_MODEL);
				logf_("HEADPARTS put-away weapon object %#lx (vtable %#lx, model interface %#lx vtable %#lx): model %#lx "
				      "(vtable %#lx, the player's %#lx), main renderable %#lx (vtable %#lx, the part vtable %#lx, handle "
				      "%#lx): not hidden", (unsigned long)wo[i], (unsigned long)vptr_of((void *)wo[i]), (unsigned long)mi,
				      (unsigned long)vptr_of((void *)mi), (unsigned long)wm, (unsigned long)(wm ? vptr_of((void *)wm) : 0),
				      (unsigned long)g_hh.vt_model, (unsigned long)mr, (unsigned long)(mr ? vptr_of((void *)mr) : 0),
				      (unsigned long)g_hh.vt_rend, (unsigned long)(mr ? peek_ptr(mr + REND_HANDLE) : 0));
			}
		}
		nw += got;
	}
	if (!nwo && sl_away_why()[0] && !g_hp.weap_none_logged) {
		g_hp.weap_none_logged = 1;
		logf_("HEADPARTS no put-away weapon objects: %s", sl_away_why());
	}
	return nw;
}

/* "cloth": the model's cloth display meshes into the set: CSimpleModel+0x88 the CSimpleModelClothNode, +0x138 its cloth
 * set, one 0x18-byte entry per mesh (+0 the CPhysicsClothVisualization, its +0x10 the custom handle, the node at the
 * handle's +0x10), every vtable checked, read again every frame (a new outfit, a cloth created late). How many went in;
 * a change is logged (the cloth node, the entries, the nodes and the descriptors' names) */
static int hp_add_cloth(somvr_hp_set *s, uintptr_t model, uint64_t frame, double t)
{
	uintptr_t cn = peek_ptr(model + SM_CLOTH), node[HPC_MESHES] = { 0 }, v[2] = { 0, 0 };
	int n = 0, nv = -1, bad = 0, added = 0, is_cn = cn && vptr_of((void *)cn) == g_hpc.vt_cloth;
	if (is_cn) {
		uintptr_t cs = peek_ptr(cn + CN_SET);
		if (cs && peek(cs, v, sizeof v))
			nv = somvr_hp_vec_n(v[0], v[1], CS_ENTRY, HPC_MESHES);
		for (int i = 0; i < nv; i++) {
			uintptr_t vis = peek_ptr(v[0] + (uintptr_t)i * CS_ENTRY);
			uintptr_t hd = vis && vptr_of((void *)vis) == g_hpc.vt_vis ? peek_ptr(vis + VIS_HANDLE) : 0;
			if (hd && vptr_of((void *)hd) == g_hpc.vt_chandle && vptr_of((void *)(hd + CHANDLE_NODE)) == g_hpc.vt_cnode) {
				node[n++] = hd + CHANDLE_NODE;
				added += somvr_hp_set_add(s, hd + CHANDLE_NODE, SOMVR_HP_CLOTH);
			} else {
				bad++;
			}
		}
	}
	g_hpc.frames++, g_hpc.nodes += (uint64_t)n, g_hpc.badvt += (uint64_t)bad;
	g_hpc.none += !n;
	if (cn == g_hpc.cnode && nv == g_hpc.nv && n == g_hpc.n && bad == g_hpc.bad && !memcmp(node, g_hpc.node, sizeof node))
		return added;
	g_hpc.cnode = cn, g_hpc.nv = nv, g_hpc.n = n, g_hpc.bad = bad;
	memcpy(g_hpc.node, node, sizeof node);
	if (g_hpc.lines >= 40)
		return added;
	g_hpc.lines++;
	if (!cn) {
		logf_("HEADPARTS t=%.3f frame=%llu the player's model %#lx has no cloth node (+0x88): no cloth to hide", t,
		      (unsigned long long)frame, (unsigned long)model);
		return added;
	}
	if (!is_cn) {
		logf_("HEADPARTS t=%.3f frame=%llu the player's model %#lx: +0x88 %#lx is not a CSimpleModelClothNode (vtable %#lx, "
		      "want %#lx): no cloth hidden", t, (unsigned long long)frame, (unsigned long)model, (unsigned long)cn,
		      (unsigned long)vptr_of((void *)cn), (unsigned long)g_hpc.vt_cloth);
		return added;
	}
	char ns[200] = "", names[400] = "";
	size_t o = 0;
	for (int i = 0; i < n && o + 24 < sizeof ns; i++)
		o += (size_t)snprintf(ns + o, sizeof ns - o, "%s%#lx", i ? " " : "", (unsigned long)node[i]);
	uintptr_t d[2] = { 0, 0 };
	int nd = peek(cn + CN_DESC, d, sizeof d) ? somvr_hp_vec_n(d[0], d[1], CN_DESC_SIZE, HPC_MESHES) : -1;
	o = 0;
	for (int i = 0; i < nd && o + 70 < sizeof names; i++) {
		char nm[64] = "";
		if (!peek(d[0] + (uintptr_t)i * CN_DESC_SIZE, nm, sizeof nm - 1))
			snprintf(nm, sizeof nm, "?");
		nm[sizeof nm - 1] = 0;
		for (char *c = nm; *c; c++)
			if (*c < 0x20 || *c > 0x7e)
				*c = '?';
		o += (size_t)snprintf(names + o, sizeof names - o, "%s'%s'", i ? " " : "", nm);
	}
	logf_("HEADPARTS t=%.3f frame=%llu the player's cloth node %#lx: %d display mesh entr%s (%s), %d node(s) not drawn in "
	      "the eyes%s%s%s; descriptors %d%s%s", t, (unsigned long long)frame, (unsigned long)cn, nv < 0 ? 0 : nv,
	      nv == 1 ? "y" : "ies", nv < 0 ? "the set not read" : "the set read", n, n ? ": " : "", ns,
	      bad ? " (entries without a custom node: drawn while so)" : "", nd < 0 ? 0 : nd, nd > 0 ? " " : "", names);
	return added;
}

/* SOMVR_HP_POSE (game thread): the body node's CSkeletalTransforms, its vtable checked; 0 = none (logged on change) */
static uintptr_t hp_pose_of(uintptr_t main_node)
{
	uintptr_t pose = peek_ptr(main_node + SK_POSE), vt = pose ? vptr_of((void *)pose) : 0;
	if (vt != HP_SKT_VT || peek_ptr(main_node + SK_POSE + 8) != pose) {
		if (g_hp_pose_bad_lines < 4 && pose != g_hp_pose_last) {
			g_hp_pose_bad_lines++;
			logf_("HEADPARTS pose: the body node %#lx's +0xf0 %#lx (vtable %#lx, +0xf8 %#lx) is not its CSkeletalTransforms "
			      "(vtable %#x): SOMVR_HP_POSE hides nothing more while so", (unsigned long)main_node, (unsigned long)pose,
			      (unsigned long)vt, (unsigned long)peek_ptr(main_node + SK_POSE + 8), HP_SKT_VT);
		}
		g_hp_pose_last = pose;
		return 0;
	}
	if (pose != g_hp_pose_last)
		logf_("HEADPARTS pose: the body node %#lx is animated by the skeletal transforms %#lx: every skeletal node they "
		      "animate is hidden in the eyes with it (SOMVR_HP_POSE)", (unsigned long)main_node, (unsigned long)pose);
	g_hp_pose_last = pose;
	return pose;
}

/* SOMVR_HP_AWAY: the arm of hand h is on our controller (fully on, its hand near its target) in this frame's phase 2 */
static int hp_hand_ours(int h)
{
	return g_arm.on && g_arm.state[h] == 2 && g_arm.weight[h] >= 1 && g_arm.rep[h].residual <= HP_AWAY_HAND_CM;
}

/* SOMVR_HP_AWAY: the weapons in the hands (the bow in the bow hand, the drawn sword in the right, the dagger in the
 * left): the ones whose hand is not ours into the set (*hid nodes), the others kept (*kept weapons) */
static void hp_add_held(somvr_hp_set *s, double t, int *hid, int *kept)
{
	*hid = *kept = 0;
	for (int h = 0; h < 2; h++) {
		uintptr_t wo[8];
		int n = sl_held_objs(h, wo, 6);
		if (g_bow.on && g_bow.bow_obj && h == g_bow.cfg.bow_hand && n < 8) /* the bow (drawn only while it is out) */
			wo[n++] = g_bow.bow_obj;
		if (!n)
			continue;
		if (hp_hand_ours(h)) {
			*kept += n;
			continue;
		}
		for (int i = 0; i < n; i++) {
			uintptr_t wm = hp_weapon_model(wo[i], t), rend[SOMVR_HP_SLOTS], mr = wm ? peek_ptr(wm + SM_MAIN) : 0;
			if (!wm)
				continue;
			*hid += somvr_hp_set_add(s, hp_node_of(mr), SOMVR_HP_WEAPON);
			if (peek(wm + SM_SLOTS, rend, sizeof rend))
				for (int k = 0; k < SOMVR_HP_SLOTS; k++)
					if (rend[k])
						*hid += somvr_hp_set_add(s, hp_node_of(rend[k]), SOMVR_HP_WEAPON);
		}
	}
}

/* SOMVR_HP_AWAY's lines: a held-away stretch's start and end (its frames, what was hidden and kept), the last set */
static void hp_away_note(const somvr_hp_set *s, uint64_t frame, double t, int away, int main_hid, int hid, int kept)
{
	g_hpa.frame = frame, g_hpa.away = away, g_hpa.main = main_hid, g_hpa.pose = s->pose != 0, g_hpa.hid = hid;
	g_hpa.kept = kept, g_hpa.n = s->n, g_hpa.nmesh = s->nmesh;
	if (away && !g_hpa.was) {
		g_hpa.t0 = t, g_hpa.frame0 = frame, g_hpa.frames = g_hpa.weap_hid = g_hpa.weap_kept = g_hpa.nomain = 0;
		logf_("HEADPARTS t=%.3f frame=%llu away begins (%s): Talion hidden in the eyes (%u nodes: the body %s, the pose %s, "
		      "%u of his meshes; weapons hidden %d nodes, kept %d in our hands)", t, (unsigned long long)frame,
		      g_hp_away_w[hp_away_mode()], s->n, main_hid ? "hidden" : "NOT hidden", s->pose ? "hidden" : "none", s->nmesh,
		      hid, kept);
	}
	if (away) {
		g_hpa.frames++, g_hpa.all++, g_hpa.weap_hid += hid > 0, g_hpa.weap_kept += kept > 0, g_hpa.nomain += !main_hid;
	} else if (g_hpa.was) {
		uint64_t m = 0;
		for (int p = 0; p < SOMVR_HP_PASSES; p++)
			m += atomic_load(&g_hp_mesh_skips[p]);
		logf_("HEADPARTS t=%.3f frame=%llu away ends after %llu frames (%.3f s): the body not hidden in %llu, weapons hidden "
		      "in %llu (not in our hands), kept in %llu; skips by mesh so far %llu (%d nodes)", t, (unsigned long long)frame,
		      (unsigned long long)g_hpa.frames, t - g_hpa.t0, (unsigned long long)g_hpa.nomain,
		      (unsigned long long)g_hpa.weap_hid, (unsigned long long)g_hpa.weap_kept, (unsigned long long)m,
		      atomic_load(&g_hp_mesh_n));
	}
	g_hpa.was = away;
}

/* away = SOMVR_HP_AWAY: this frame's view is held away from Talion (hp_away_frame); live is then 1 too */
static void hp_frame(uintptr_t pl, int live, uint64_t frame, int away)
{
	if (!hp_live())
		return;
	uint32_t sq = atomic_load_explicit(&g_req_hp_seq, memory_order_acquire);
	if (sq != g_hp.seen_req) {
		char a[96];
		g_hp.seen_req = sq;
		g_hp.slots = atomic_load_explicit(&g_req_hp, memory_order_relaxed);
		somvr_hp_slots_str(g_hp.slots, a, sizeof a);
		logf_("HEADPARTS t=%.3f the slots are now %s (trigger word)", mono(), a);
		g_hp.empty_run = 0;
	}
	g_hp.frames++;
	if (!live || !frame) {
		g_hp.flat++;
		return;
	}
	uintptr_t pm = 0, obj = 0, model = 0, rend[SOMVR_HP_SLOTS];
	if (pl && player_objs(pl, &pm, &obj))
		model = hide_model_of(obj);
	if (!model || !peek(model + SM_SLOTS, rend, sizeof rend)) {
		g_hp.nomodel++;
		return;
	}
	uint32_t occ = 0;
	for (int k = 0; k < SOMVR_HP_SLOTS; k++)
		occ |= (uint32_t)(rend[k] != 0) << k;
	if (occ != g_hp.occ || model != g_hp.occ_model) { /* run 12: slots 1, 3, 4 were empty on Talion's model */
		char a[96], b[96];
		somvr_hp_slots_str(occ, a, sizeof a), somvr_hp_slots_str(g_hp.slots, b, sizeof b);
		logf_("HEADPARTS frame=%llu model %#lx: renderables in slots %s (wanted %s); main renderable +0xb0 %#lx",
		      (unsigned long long)frame, (unsigned long)model, occ ? a : "none", b, (unsigned long)peek_ptr(model + SM_MAIN));
		g_hp.occ = occ, g_hp.occ_model = model;
	}
	somvr_hp_set s;
	somvr_hp_set_init(&s, frame);
	if (g_fr.head_ok)
		memcpy(s.at, g_fr.head, sizeof s.at); /* SOMVR_HP_NEARLOG's point */
	uintptr_t node[SOMVR_HP_SLOTS] = { 0 };
	for (int k = 0; k < SOMVR_HP_SLOTS; k++) {
		if (!(g_hp.slots >> k & 1) || !rend[k])
			continue;
		node[k] = hp_node_of(rend[k]);
		if (!node[k]) {
			g_hp.badvt++;
			if (g_hp.bad_rend[k] != rend[k]) {
				g_hp.bad_rend[k] = rend[k];
				logf_("HEADPARTS model %#lx slot %d (%s): renderable %#lx (vtable %#lx) has no skeletal node yet (handle "
				      "%#lx): not hidden while so", (unsigned long)model, k, somvr_hp_slot_name(k), (unsigned long)rend[k],
				      (unsigned long)vptr_of((void *)rend[k]), (unsigned long)peek_ptr(rend[k] + REND_HANDLE));
			}
			continue;
		}
		somvr_hp_set_add(&s, node[k], k);
	}
	uintptr_t main_node = 0, main_rend = 0; /* "main": the whole body (headset session 1: "just hands") */
	if (g_hp.slots >> SOMVR_HP_MAIN & 1) {
		main_rend = peek_ptr(model + SM_MAIN);
		main_node = hp_node_of(main_rend);
		if (main_node) {
			if (!bd_body_drawn() || away) { /* SOMVR_BODY=hands edited this frame's skin: the body node shows the hands
							 * only (held away: those hands are with the body, not ours) */
				somvr_hp_set_add(&s, main_node, SOMVR_HP_MAIN);
				g_hp_main_hid = frame; /* SOMVR_VRHANDS: the body (the game's hands) hidden in this frame's eyes */
				if (atomic_load_explicit(&g_hp_pose_on, memory_order_relaxed))
					s.pose = hp_pose_of(main_node);
			}
		} else if (main_rend) {
			g_hp.badvt++;
			if (g_hp.bad_main != main_rend) {
				g_hp.bad_main = main_rend;
				logf_("HEADPARTS model %#lx main renderable %#lx (vtable %#lx, the part vtable %#lx) has no skeletal node "
				      "(handle %#lx): the body is drawn while so", (unsigned long)model, (unsigned long)main_rend,
				      (unsigned long)vptr_of((void *)main_rend), (unsigned long)g_hh.vt_rend,
				      (unsigned long)peek_ptr(main_rend + REND_HANDLE));
			}
		}
	}
	double t = mono();
	if (g_hp.slots >> SOMVR_HP_MAIN & 1) {
		int nw = hp_add_weapons(&s, t, main_node && g_hp_main_hid == frame);
		if (nw != g_hp.n_weap) {
			logf_("HEADPARTS t=%.3f frame=%llu put-away weapons not drawn in the eyes: %d node(s) (was %d)", t,
			      (unsigned long long)frame, nw, g_hp.n_weap);
			g_hp.n_weap = nw;
		}
	}
	if ((g_hp.slots >> SOMVR_HP_CLOTH & 1) && hpc_live())
		hp_add_cloth(&s, model, frame, t);
	int hid = 0, kept = 0;
	if (away) { /* SOMVR_HP_AWAY: Talion's own meshes, the weapons not in our hands */
		s.away = 1;
		if (main_node)
			somvr_hp_add_mesh(&s, peek_ptr(main_node + SK_MESH));
		int nm = atomic_load_explicit(&g_hp_own_n, memory_order_acquire);
		for (int i = 0; i < nm && i < SOMVR_HP_MESHES; i++)
			somvr_hp_add_mesh(&s, atomic_load_explicit(&g_hp_own_mesh[i], memory_order_relaxed));
		hp_add_held(&s, t, &hid, &kept);
	}
	hp_away_note(&s, frame, t, away, main_node && g_hp_main_hid == frame, hid, kept);
	somvr_hp_publish(&g_hp_ring, &s);
	g_hp.published++, g_hp.nodes += s.n;
	if (atomic_exchange(&g_req_hpscan, 0) || (g_hp_near_cm > 0 && model != g_hp.model))
		hp_scan(model, model != g_hp.model ? "a new model" : "the word hpscan");
	/* a new model or another node in a slot (the first frame, a rebuild after an outfit or equipment change) */
	int changed = model != g_hp.model || memcmp(node, g_hp.node, sizeof node) || main_node != g_hp.main_node;
	if (changed) {
		g_hp.changes++;
		if (g_hp.lines < 40 || g_hp.changes % 100 == 0) {
			g_hp.lines++;
			char d[400] = "";
			size_t o = 0;
			for (int k = 0; k < SOMVR_HP_SLOTS && o + 60 < sizeof d; k++)
				if ((node[k] || g_hp.node[k]) && (node[k] != g_hp.node[k] || model != g_hp.model))
					o += (size_t)snprintf(d + o, sizeof d - o, "%s slot %d (%s) %#lx -> %#lx", o ? ";" : "", k,
							      somvr_hp_slot_name(k), (unsigned long)g_hp.node[k], (unsigned long)node[k]);
			if (main_node != g_hp.main_node && o + 60 < sizeof d)
				o += (size_t)snprintf(d + o, sizeof d - o, "%s main %#lx -> %#lx", o ? ";" : "",
						      (unsigned long)g_hp.main_node, (unsigned long)main_node);
			logf_("HEADPARTS t=%.3f frame=%llu the player's model %#lx%s:%s (%u nodes hidden in the eye passes)", t,
			      (unsigned long long)frame, (unsigned long)model, model != g_hp.model ? " (new)" : "", o ? d : " -",
			      s.n);
		}
		g_hp.model = model, g_hp.main_node = main_node;
		memcpy(g_hp.node, node, sizeof node);
	}
	if (!s.n)
		g_hp.empty++;
	/* the head slot wanted, the model there, no head node for HP_EMPTY_FRAMES stereo frames: the layout is not what the
	 * sites said */
	if ((g_hp.slots >> SOMVR_HP_HEAD & 1) && !node[SOMVR_HP_HEAD]) {
		if (++g_hp.empty_run == HP_EMPTY_FRAMES) {
			char why[160];
			snprintf(why, sizeof why, "the player's model %#lx had no head node (slot 1) for %d stereo frames running",
				 (unsigned long)model, HP_EMPTY_FRAMES);
			hp_fall_back(why);
			return;
		}
	} else {
		g_hp.empty_run = 0;
	}
	if (t >= g_hp.next_log) {
		g_hp.next_log = t + 5.0;
		hp_log_line(t, frame, &s);
	}
}

/* a group of call sites (the five HP_DRAW_* from HPS_DEEP, the eight HP_CUST_* from HPS_C_DEEP): verified (again:
 * hb_config may run more than once in the selftest; installed ones stay), a stub page of their own, the rel32s written;
 * all of the group or none (one pass drawing it would show it). 1 = all in */
static int hp_install_group(int first, int n, struct hook *hk, void (*const *wr)(void), const int *pass, const char *grp,
			    const char *nword)
{
	enum { HPG_MAX = 8 };
	int all = 1;
	for (int i = 0; i < n; i++)
		all &= hk[i].installed;
	if (all)
		return 1;
	char why[HPG_MAX][512];
	int ok[HPG_MAX], good = 1;
	uintptr_t lo = UINTPTR_MAX, hi = 0;
	for (int i = 0; i < n; i++) {
		const struct site_spec *s = &g_hp_sites[first + i];
		why[i][0] = 0;
		ok[i] = s->kind == K_CALL && check_site(s, why[i], sizeof why[i]) == 0;
		if (s->kind != K_CALL)
			snprintf(why[i], sizeof why[i], "not a call site");
		good &= ok[i];
		lo = s->addr < lo ? s->addr : lo, hi = s->addr > hi ? s->addr : hi;
	}
	unsigned char *pg = NULL;
	for (int i = 0; good && i < 64 + 4 && !pg; i++) { /* as stub_page: below the code first, then the low 2 GB */
		static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
		uintptr_t at = i < 64 ? (uintptr_t)0x3f0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
		void *p = mmap((void *)at, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED)
			continue;
		if (stub_reach((uintptr_t)p, lo) && stub_reach((uintptr_t)p, hi))
			memset(pg = p, 0xcc, 4096);
		else
			munmap(p, 4096);
	}
	if (good && !pg)
		for (int i = 0; i < n; i++)
			ok[i] = 0, snprintf(why[i], sizeof why[i], "no stub page within +-2 GB");
	if (pg) {
		static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
		for (int i = 0; i < n; i++) {
			uintptr_t tgt = (uintptr_t)wr[i];
			memcpy(pg + 16 * i, jmp, 6);
			memcpy(pg + 16 * i + 6, &tgt, 8);
		}
		if (mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			for (int i = 0; i < n; i++)
				ok[i] = 0, snprintf(why[i], sizeof why[i], "stub mprotect rx: %s", strerror(errno));
	}
	int in = 0, any = 0;
	for (int k = 0; k < n; k++)
		in += ok[k];
	all = 1;
	for (int i = 0; i < n; i++) {
		const struct site_spec *s = &g_hp_sites[first + i];
		char err[200] = "", nb[64];
		if (in != n) {
			if (ok[i])
				logf_("HOOK %s SKIPPED %#lx: another %s site did not match (all %s or none)", s->name, (unsigned long)s->addr,
				      grp, nword);
			else
				logf_("HOOK %s SKIPPED %#lx: %s", s->name, (unsigned long)s->addr, why[i]);
			all = 0;
			continue;
		}
		uintptr_t st = (uintptr_t)(pg + 16 * i);
		int32_t rel = (int32_t)((int64_t)st - (int64_t)(s->addr + 5));
		if (write_code(s->addr + 1, &rel, 4, err, sizeof err) != 0) {
			logf_("HOOK %s SKIPPED %#lx: write failed: %s", s->name, (unsigned long)s->addr, err);
			all = 0;
			continue;
		}
		hk[i].orig = s->target;
		hk[i].installed = 1;
		any = 1;
		hex_of((const unsigned char *)s->addr, 5, nb, sizeof nb);
		logf_("HOOK %s ok call %#lx orig=[%s] target=%#lx now=[%s] via=%#lx (the %s pass)", s->name,
		      (unsigned long)s->addr, why[i], (unsigned long)s->target, nb, (unsigned long)st,
		      somvr_hp_pass_name(pass ? pass[i] : i));
	}
	if (!all && pg && !any)
		munmap(pg, 4096);
	return all;
}

/* the five skeletal calls; 1 = all five in */
static int hp_install(void)
{
	void (*const wr[SOMVR_HP_PASSES])(void) = { somvr_w_hpdeep, somvr_w_hpfwd, somvr_w_hplate, somvr_w_hpgbuf,
						    somvr_w_hpvel };
	return hp_install_group(HPS_DEEP, SOMVR_HP_PASSES, g_hp_h, wr, NULL, "HP_DRAW_*", "five");
}

/* the eight custom calls (the cloth); 1 = all eight in */
static int hpc_install(void)
{
	void (*const wr[HPC_N])(void) = { somvr_w_hpcdeep,  somvr_w_hpcfwd,  somvr_w_hpclatea, somvr_w_hpclateb,
					  somvr_w_hpcgbufa, somvr_w_hpcgbufb, somvr_w_hpcvela, somvr_w_hpcvelb };
	_Static_assert(HPC_N == 8, "the eight custom calls");
	return hp_install_group(HPS_C_DEEP, HPC_N, g_hpc_h, wr, g_hpc_pass, "HP_CUST_*", "eight");
}

/* the read-only proofs and the HS sites (the model, the renderable, the handle and the node vtables); 0 = all ok */
static int hp_check(void)
{
	char why[512];
	int bad = 0;
	for (int i = SOMVR_HP_PASSES; i < HPS_C_DEEP; i++) {
		int ok = check_site(&g_hp_sites[i], why, sizeof why) == 0;
		bad += !ok;
		logf_("HEADPARTS site %s %s %#lx -> %#lx: %s", g_hp_sites[i].name, ok ? "ok" : "MISMATCH",
		      (unsigned long)g_hp_sites[i].addr, (unsigned long)g_hp_sites[i].target, why);
	}
	bad += hh_check("HEADPARTS");
	if (g_hp_sites[HPS_SLOT_SET].addr - 8 * 20 != g_hh.vt_model) {
		logf_("HEADPARTS HP_SLOT_SET is not in HEAD_MODEL_VT's vtable");
		bad++;
	}
	return bad;
}

/* the cloth's read-only proofs (one line each) and its four vtables; 0 = all ok */
static int hpc_check(void)
{
	char why[512];
	int bad = 0;
	for (int i = HPS_CLOTH_STORE; i < HPS_N; i++) {
		int ok = check_site(&g_hp_sites[i], why, sizeof why) == 0;
		bad += !ok;
		logf_("HEADPARTS site %s %s %#lx -> %#lx: %s", g_hp_sites[i].name, ok ? "ok" : "MISMATCH",
		      (unsigned long)g_hp_sites[i].addr, (unsigned long)g_hp_sites[i].target, why);
	}
	g_hpc.vt_cloth = g_hp_sites[HPS_CLOTH_VT].addr - 8 * HPC_SLOT_CLOTH;
	g_hpc.vt_vis = g_hp_sites[HPS_VIS_VT].addr - 8 * HPC_SLOT_VIS;
	g_hpc.vt_chandle = g_hp_sites[HPS_CHANDLE_VT].addr - 8 * HPC_SLOT_CHANDLE;
	g_hpc.vt_cnode = g_hp_sites[HPS_CNODE_VT].addr - 8 * HPC_SLOT_CNODE;
	return bad;
}

/* hp_config, parts on: the cloth's proofs and its eight calls; 1 = on, 0 = off (a WARN: the cloth stays in the eyes) */
static int hpc_config(void)
{
	memset(g_hpc_first, 0, sizeof g_hpc_first);
	atomic_store(&g_hpc.on, 0);
	g_hpc.cnode = 0, g_hpc.nv = 0, g_hpc.n = 0, g_hpc.bad = 0, g_hpc.lines = 0;
	memset(g_hpc.node, 0, sizeof g_hpc.node);
	int bad = hpc_check();
	if (bad) {
		logf_("WARN SOMVR_HIDE_HEAD=parts: %d of the cloth's read-only sites did not match: the cloth (the fur mantle) stays "
		      "in the eyes", bad);
		return 0;
	}
	if (!hpc_install()) {
		logf_("WARN SOMVR_HIDE_HEAD=parts: the eight HP_CUST_* calls are not all in: the cloth (the fur mantle) stays in the "
		      "eyes (the ones in pass through)");
		return 0;
	}
	atomic_store(&g_hpc.on, 1);
	return 1;
}

/* hb_config, SOMVR_HIDE_HEAD has parts on the head base: the slots, the sites, the five calls. 1 = parts on; 0 = off
 * (a WARN; the caller falls back to near) */
static int hp_config(const char *hh)
{
	memset(g_hp_first, 0, sizeof g_hp_first);
	g_hp_rt.frame = 0, g_hp_rt.ok = 0; /* this thread's (the selftest's one thread); frames only grow anyway */
	g_hp.want = 1, g_hp.fell_back = 0, g_hp.empty_run = 0;
	atomic_store(&g_hp.on, 0);
	g_hp.slots = SOMVR_HP_DEFAULT;
	const char *sl = getenv("SOMVR_HEAD_PARTS");
	char a[96];
	if (sl && sl[0] && somvr_hp_parse_slots(sl, &g_hp.slots)) {
		g_hp.slots = SOMVR_HP_DEFAULT;
		logf_("WARN SOMVR_HEAD_PARTS=%s is not a slot list (i,j,a-b in 0..19, main, or none): the default 1,3,4", sl);
	}
	somvr_hp_ring_init(&g_hp_ring);
	int bad = hp_check();
	if (bad) {
		logf_("WARN SOMVR_HIDE_HEAD=%s: %d of its read-only sites did not match: no parts", hh, bad);
		return 0;
	}
	if (!hp_install()) {
		logf_("WARN SOMVR_HIDE_HEAD=%s: the five HP_DRAW_* calls are not all in: no parts (the ones in pass through)", hh);
		return 0;
	}
	g_hp.sites_ok = 1;
	atomic_store(&g_hp.on, 1);
	int cloth = hpc_config();
	const char *ps = getenv("SOMVR_HP_POSE");
	atomic_store(&g_hp_pose_on, !(ps && (!strcmp(ps, "off") || !strcmp(ps, "0"))));
	if (ps && ps[0] && strcmp(ps, "on") && strcmp(ps, "1") && strcmp(ps, "off") && strcmp(ps, "0"))
		logf_("WARN SOMVR_HP_POSE=%s is not on or off: on", ps);
	logf_("CONFIG head parts pose: %s (while the body node is hidden in the eyes, every skeletal node its skeletal transforms "
	      "animate is too: the wraith over Talion, its white glove in cover and around the bow hand; SOMVR_HP_POSE=on|off, "
	      "the word hppose on|off)", atomic_load(&g_hp_pose_on) ? "on" : "off");
	atomic_store(&g_hp_away, env_word("SOMVR_HP_AWAY", g_hp_away_w, 3, HP_AWAY_OFF));
	logf_("CONFIG head parts away: %s (SOMVR_HP_AWAY=off|hide|hands, the word hpaway: a frame whose view is held away from "
	      "Talion - the Shadow Strike, a body moment's hold - %s)", g_hp_away_w[hp_away_mode()],
	      hp_away_mode() == HP_AWAY_OFF ? "draws him, as build 11"
	      : hp_away_mode() == HP_AWAY_HIDE ? "hides him in the eyes: the body, the wraith, his meshes, the weapons not in our hands"
					      : "hides him in the eyes and keeps the arms and our VR hands on the controllers");
	const char *nl = getenv("SOMVR_HP_NEARLOG");
	g_hp_near_cm = 0;
	atomic_store(&g_hp_near_n, 0), atomic_store(&g_hp_near_hits, 0);
	if (nl && nl[0] && strcmp(nl, "0")) {
		char *e;
		double v = strtod(nl, &e);
		g_hp_near_cm = !strcmp(nl, "1") ? 60.0f : (*e || !(v > 0) || v > 1000) ? 0.0f : (float)v;
		if (g_hp_near_cm > 0)
			logf_("CONFIG head parts near-log: every node an eye pass still draws through the 13 hooked calls within %.0f "
			      "cm of the head point is logged once (HEADPARTS near: ..., at most %d)", g_hp_near_cm, HP_NEAR_LINES);
		else
			logf_("WARN SOMVR_HP_NEARLOG=%s is not 1 or a distance in cm (0..1000]: no near-log", nl);
	}
	somvr_hp_slots_str(g_hp.slots, a, sizeof a);
	logf_("CONFIG head parts: slots %s (1 head, 3 helm = the hood, 4 hair: CSimpleModel+0xb8 + 8 x slot; body = the main "
	      "renderable +0xb0 and the put-away weapons; cloth = the cloth display meshes +0x88; main = body + cloth) not drawn "
	      "in the eye passes (deep, forward, late, G-buffer, velocity: the five FUN_0081ff80 calls%s) while the base is the "
	      "head; the shadow maps (FUN_0081fd10, the custom calls of pass 7) keep them; read again every frame", a,
	      cloth ? " and the eight FUN_007ccec0 calls" : "; the cloth is off");
	return 1;
}

/* somvr_fini */
static void hp_end_log(void)
{
	if (!g_hp.want)
		return;
	logf_("SUMMARY head parts: %s; frames %llu published %llu (nodes %llu) flat %llu no-model %llu empty %llu bad-vtable %llu "
	      "changes %llu; skipped/calls deep %llu/%llu forward %llu/%llu late %llu/%llu gbuffer %llu/%llu velocity %llu/%llu",
	      g_hp.fell_back ? "fell back to near" : hp_live() ? "on" : "off", (unsigned long long)g_hp.frames,
	      (unsigned long long)g_hp.published, (unsigned long long)g_hp.nodes, (unsigned long long)g_hp.flat,
	      (unsigned long long)g_hp.nomodel, (unsigned long long)g_hp.empty, (unsigned long long)g_hp.badvt,
	      (unsigned long long)g_hp.changes, (unsigned long long)atomic_load(&g_hp_h[0].mods),
	      (unsigned long long)atomic_load(&g_hp_h[0].calls), (unsigned long long)atomic_load(&g_hp_h[1].mods),
	      (unsigned long long)atomic_load(&g_hp_h[1].calls), (unsigned long long)atomic_load(&g_hp_h[2].mods),
	      (unsigned long long)atomic_load(&g_hp_h[2].calls), (unsigned long long)atomic_load(&g_hp_h[3].mods),
	      (unsigned long long)atomic_load(&g_hp_h[3].calls), (unsigned long long)atomic_load(&g_hp_h[4].mods),
	      (unsigned long long)atomic_load(&g_hp_h[4].calls));
	char cl[512];
	hpc_counts(cl, sizeof cl);
	logf_("SUMMARY head parts %s; cloth nodes total %llu, entries without a node %llu", cl, (unsigned long long)g_hpc.nodes,
	      (unsigned long long)g_hpc.badvt);
	uint64_t m[SOMVR_HP_PASSES];
	for (int p = 0; p < SOMVR_HP_PASSES; p++)
		m[p] = atomic_load(&g_hp_mesh_skips[p]);
	logf_("SUMMARY head parts away %s: held-away frames %llu; skipped by mesh deep=%llu forward=%llu late=%llu gbuffer=%llu "
	      "velocity=%llu (%d nodes, %d meshes learned)", g_hp_away_w[hp_away_mode()], (unsigned long long)g_hpa.all,
	      (unsigned long long)m[0], (unsigned long long)m[1], (unsigned long long)m[2], (unsigned long long)m[3],
	      (unsigned long long)m[4], atomic_load(&g_hp_mesh_n), atomic_load(&g_hp_own_n));
}
