/* somvr_handdraw.h - the VR hands' Vulkan drawing (docs/re-xr-pipeline.md 33): the two skinned hand meshes (CPU
 * vertices, ../somvr_vrhands.h) and the occluders (the wrist panel's and the watch's quads: depth only) drawn per eye
 * into a multisampled image of ours, resolved side by side into a destination image (the hands' XR swapchain image).
 * No globals: the layer (somvr_vrhands_layer.c, through the next layer's device functions) and tests/hands_lvp.c
 * (lavapipe, pictures offline) both drive it. Shaders: shaders/hands.vert, hands.frag -> somvr_hands_spv.h. */
#ifndef SOMVR_HANDDRAW_H
#define SOMVR_HANDDRAW_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#define HD_FUNCS(X)                                                                                                    \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateImageView)             \
    X(DestroyImageView) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory)               \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) X(AllocateMemory)             \
    X(FreeMemory) X(MapMemory) X(UnmapMemory) X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout)    \
    X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) X(CmdBeginRenderPass) X(CmdEndRenderPass)   \
    X(CmdBindPipeline) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) X(CmdDrawIndexed) X(CmdPushConstants)            \
    X(CmdSetViewport) X(CmdSetScissor) X(CmdPipelineBarrier) X(CmdResolveImage) X(CmdCopyImage) X(CmdClearColorImage)
typedef struct {
#define HD_DECL(n) PFN_vk##n n;
    HD_FUNCS(HD_DECL)
#undef HD_DECL
} hd_vk;
/* every function from get(ud, "vkName"); NULL = all there, else the first missing name */
const char *hd_vk_load(hd_vk *f, PFN_vkVoidFunction (*get)(void *ud, const char *name), void *ud);
/* build 11 (SOMVR_XR_HANDS_DEPTH=world): what sampling the world's depth needs, loaded apart (missing = no world) */
#define HD_WFUNCS(X)                                                                                                   \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool)        \
    X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdBindDescriptorSets) X(CreateSampler) X(DestroySampler)
typedef struct {
#define HD_WDECL(n) PFN_vk##n n;
    HD_WFUNCS(HD_WDECL)
#undef HD_WDECL
} hd_vkw;
const char *hd_vkw_load(hd_vkw *w, PFN_vkVoidFunction (*get)(void *ud, const char *name), void *ud);

#define HD_MAX_OCC 4    /* occluder quads a frame (the wrist panel, the card, the watch) */
#define HD_NSLOT 3      /* vertex buffer slots (the caller waits a slot's fence before refilling it) */
#define HD_OCC_IDX 12   /* an occluder quad's indices: its two triangles in both windings */
typedef struct {
    float p[3], n[3], a; /* = somvr_vh_vert */
} hd_vert;

typedef struct {
    VkDevice dev;
    const hd_vk *f;
    VkPhysicalDeviceMemoryProperties mp;
    VkFormat fmt;
    VkSampleCountFlagBits samples;
    uint32_t w, h;          /* one eye */
    uint32_t nv;            /* one hand's vertices */
    uint32_t ni;            /* one hand's indices */
    VkRenderPass rp;
    VkPipelineLayout pl;
    VkPipeline pipe;
    VkImage col, dep;
    VkDeviceMemory col_mem, dep_mem;
    VkImageView col_v, dep_v;
    VkFramebuffer fb;
    VkBuffer vb, ib;
    VkDeviceMemory vb_mem, ib_mem;
    uint8_t *vb_map;
    VkDeviceSize slot_bytes;
    VkDeviceSize vram;      /* the images' memory (logs) */
    char why[200];
    /* build 11: the world's depth (hd_world; none = build 10's pipeline alone) */
    const hd_vkw *wf;
    VkDescriptorSetLayout dsl;
    VkDescriptorPool dpool;
    VkDescriptorSet dset[HD_NSLOT][2]; /* per slot and eye: the eye's depth (or the zero image) */
    VkSampler smp;
    VkPipelineLayout pl_w;
    VkPipeline pipe_w;      /* the hands with the world's depth (hands.frag's WORLD_DEPTH) */
    VkImage zero;           /* 1x1 R32_SFLOAT 0 = nothing there: bound when an eye has no depth */
    VkDeviceMemory zero_mem;
    VkImageView zero_v;
    int zero_ready;
    float soft_m, bias_m, texel;
    int debug;              /* the world's pipeline shows the depth difference (SOMVR_XR_HANDS_DEPTH_DEBUG) */
} hd_ctx;

/* everything for eyes of w x h in format fmt (the destination's; samples 1, 2, 4 or 8), the hands' indices (the right
 * hand's and the mirrored left's, ni each, for nv vertices each). 0 = ready, -1 = not (c->why; hd_destroy cleans up) */
int hd_create(hd_ctx *c, VkDevice dev, const hd_vk *f, const VkPhysicalDeviceMemoryProperties *mp, VkFormat fmt,
              uint32_t w, uint32_t h, uint32_t samples, const uint16_t *idx_r, const uint16_t *idx_l, uint32_t ni,
              uint32_t nv);
void hd_destroy(hd_ctx *c);
/* build 11: a second pipeline that tests the hands against the world's depth (each eye's sampled image: reversed Z,
 * an infinite far plane; hd_frame's depth_k and world[]), its edge soft_m wide centred bias_m behind the surface, texel =
 * the depth image's pixels per pixel of ours; debug: the hands show (their distance - the world's) instead (red 0.5 =
 * at the surface, +-10 cm; green: the sky; blue: the fade). 0 = ready, -1 = not (c->why; the plain pipeline draws) */
int hd_world(hd_ctx *c, const hd_vkw *wf, float soft_m, float bias_m, float texel, int debug);
/* slot s's vertices: [the left hand: nv][the right hand: nv][the occluders: 4 per quad, HD_MAX_OCC][build 13: the
 * ghosts of the left and the right hand: nv each] (host-coherent) */
hd_vert *hd_slot_verts(hd_ctx *c, uint32_t s);
/* build 13 (SOMVR_HANDS_SURFACE): slot s's ghost of hand h (nv vertices) */
hd_vert *hd_slot_ghost(hd_ctx *c, uint32_t s, int h);
/* an occluder quad (centre, its two half-axes) into v[0..3] */
void hd_quad(hd_vert v[4], const float centre[3], const float half_x[3], const float half_y[3]);

typedef struct {
    float vp[2][16];        /* per eye: clip <- XR (column-major, Vulkan clip) */
    float eye[2][3];        /* per eye: its position (XR) */
    int hand[2];            /* draw the left / right hand */
    uint32_t nocc;          /* occluder quads in the slot */
    float light[3];         /* toward the key light (unit, XR) */
    float opacity;
    float base[3];          /* the albedo, linear */
    float key, ambient, rim, spec;
    int32_t box[2][4];      /* out: the eyes' drawn boxes (hd_record) */
    /* build 11 (hd_world): an eye's view distance (m) = depth_k / its depth value; 0 = no world this frame. world[e]:
     * eye e's depth (a sampled view in SHADER_READ_ONLY_OPTIMAL; VK_NULL_HANDLE = none) */
    float depth_k;
    VkImageView world[2];
    float held[2];          /* per hand holding a weapon: the world's depth read as Talion's held items' (their depth
                             * as if the near plane were this times the eyes'; 1 = as the world's) */
    /* build 13 (SOMVR_HANDS_SURFACE): the real hands' ghosts where a surface stopped the drawn ones: ghost_a[h] > 0
     * draws hd_slot_ghost(h) that opaque in ghost_base with a strong rim, after the hands and never against the world's
     * depth (it is in the wall); our own depth (the hands, the occluders) still hides it */
    float ghost_a[2];
    float ghost_base[3];
} hd_frame;
/* the screen box (pixels of a w x h eye, padded by pad) of n vertices under the clip matrix vp; 0 = none on the screen,
 * 1 = box, 2 = a vertex behind the eye (the whole eye) */
int hd_box(const float vp[16], const hd_vert *v, uint32_t n, uint32_t w, uint32_t h, int pad, int32_t box[4]);
/* an eye's matrix: its pose (q x y z w, p) and fov (tangents' angles: left right up down, rad), near/far (m) */
void hd_eye_matrix(const float q[4], const float p[3], const float fov[4], float zn, float zf, float out[16]);
/* Record one frame: dst cleared (transparent), then per eye only the hands' box on the screen (their vertices projected,
 * padded; the whole eye when a vertex is behind it; nothing when they are off it): our image cleared there, the
 * occluders (depth), the hands (with the world's depth when hd_world made its pipeline), resolved (or copied) into dst
 * at x = e * w + the box's. dst goes from dst_old
 * (UNDEFINED is fine: it is cleared) to dst_new. fr->box gets each eye's box (x0 y0 x1 y1; empty: x1 <= x0). */
void hd_record(hd_ctx *c, VkCommandBuffer cmd, uint32_t slot, hd_frame *fr, VkImage dst, VkImageLayout dst_old,
               VkImageLayout dst_new);

#endif
