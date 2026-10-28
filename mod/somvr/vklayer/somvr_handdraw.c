/* somvr_handdraw.c - the VR hands' Vulkan drawing (somvr_handdraw.h; docs/re-xr-pipeline.md 33). No globals. */
#include "somvr_handdraw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "somvr_hands_spv.h"
#include "../somvr_vrhands.h" /* build 16: the arrow's mesh (SOMVR_VH_ARROW_*) */

#define HD_DEPTH VK_FORMAT_D16_UNORM /* every implementation can attach it (the spec's required formats) */

const char *hd_vk_load(hd_vk *f, PFN_vkVoidFunction (*get)(void *ud, const char *name), void *ud) {
    memset(f, 0, sizeof *f);
#define HD_LOAD(n)                                                                                                     \
    if (!(f->n = (PFN_vk##n)get(ud, "vk" #n))) return "vk" #n;
    HD_FUNCS(HD_LOAD)
#undef HD_LOAD
    return NULL;
}

const char *hd_vkw_load(hd_vkw *w, PFN_vkVoidFunction (*get)(void *ud, const char *name), void *ud) {
    memset(w, 0, sizeof *w);
#define HD_WLOAD(n)                                                                                                    \
    if (!(w->n = (PFN_vk##n)get(ud, "vk" #n))) return "vk" #n;
    HD_WFUNCS(HD_WLOAD)
#undef HD_WLOAD
    return NULL;
}

static int mem_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits >> i & 1u) && (mp->memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

static int fail(hd_ctx *c, const char *what, VkResult r) {
    snprintf(c->why, sizeof c->why, "%s: %d", what, (int)r);
    return -1;
}

static int image(hd_ctx *c, VkFormat fmt, VkImageUsageFlags use, VkImageAspectFlags aspect, VkImage *img,
                 VkDeviceMemory *mem, VkImageView *view, const char *what) {
    const hd_vk *f = c->f;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = fmt,
                            .extent = {c->w, c->h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = c->samples,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = use,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r = f->CreateImage(c->dev, &ci, NULL, img);
    if (r != VK_SUCCESS) return *img = VK_NULL_HANDLE, fail(c, what, r);
    VkMemoryRequirements mr;
    f->GetImageMemoryRequirements(c->dev, *img, &mr);
    int t = mem_type(&c->mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0) t = mem_type(&c->mp, mr.memoryTypeBits, 0);
    if (t < 0) return fail(c, "no memory type for an image", VK_ERROR_FEATURE_NOT_PRESENT);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = (uint32_t)t};
    if ((r = f->AllocateMemory(c->dev, &ai, NULL, mem)) != VK_SUCCESS) return *mem = VK_NULL_HANDLE, fail(c, what, r);
    c->vram += mr.size;
    if ((r = f->BindImageMemory(c->dev, *img, *mem, 0)) != VK_SUCCESS) return fail(c, what, r);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .image = *img,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = fmt,
                                .subresourceRange = {aspect, 0, 1, 0, 1}};
    if ((r = f->CreateImageView(c->dev, &vi, NULL, view)) != VK_SUCCESS) return *view = VK_NULL_HANDLE, fail(c, what, r);
    return 0;
}

static int buffer(hd_ctx *c, VkDeviceSize size, VkBufferUsageFlags use, VkBuffer *b, VkDeviceMemory *mem, void **map) {
    const hd_vk *f = c->f;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = use,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkResult r = f->CreateBuffer(c->dev, &bi, NULL, b);
    if (r != VK_SUCCESS) return *b = VK_NULL_HANDLE, fail(c, "a buffer", r);
    VkMemoryRequirements mr;
    f->GetBufferMemoryRequirements(c->dev, *b, &mr);
    int t = mem_type(&c->mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (t < 0) return fail(c, "no host-coherent memory type", VK_ERROR_FEATURE_NOT_PRESENT);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = (uint32_t)t};
    if ((r = f->AllocateMemory(c->dev, &ai, NULL, mem)) != VK_SUCCESS) return *mem = VK_NULL_HANDLE, fail(c, "buffer memory", r);
    if ((r = f->BindBufferMemory(c->dev, *b, *mem, 0)) != VK_SUCCESS) return fail(c, "binding a buffer", r);
    if ((r = f->MapMemory(c->dev, *mem, 0, VK_WHOLE_SIZE, 0, map)) != VK_SUCCESS) return *map = NULL, fail(c, "mapping a buffer", r);
    return 0;
}

/* the hands' pipeline with fragment shader frag (and its specialization) in layout pl */
static VkResult pipeline(hd_ctx *c, const uint32_t *frag, size_t frag_bytes, VkPipelineLayout pl,
                         const VkSpecializationInfo *spec, VkPipeline *out) {
    const hd_vk *f = c->f;
    VkShaderModule sm[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkResult r = VK_SUCCESS;
    VkShaderModuleCreateInfo smi[2] = {
        {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof somvr_hands_vert_spv,
         .pCode = somvr_hands_vert_spv},
        {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = frag_bytes, .pCode = frag}};
    for (int i = 0; i < 2; i++)
        if ((r = f->CreateShaderModule(c->dev, &smi[i], NULL, &sm[i])) != VK_SUCCESS) {
            sm[i] = VK_NULL_HANDLE;
            break;
        }
    if (r == VK_SUCCESS) {
        VkPipelineShaderStageCreateInfo st[2] = {
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = sm[0], .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = sm[1], .pName = "main", .pSpecializationInfo = spec}};
        VkVertexInputBindingDescription vb = {0, sizeof(hd_vert), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription va[3] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                   {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
                                                   {2, 0, VK_FORMAT_R32_SFLOAT, 24}};
        VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                   .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
                                                   .vertexAttributeDescriptionCount = 3,
                                                   .pVertexAttributeDescriptions = va};
        VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                     .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkPipelineViewportStateCreateInfo vs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                .viewportCount = 1, .scissorCount = 1};
        /* back faces culled: the mesh is closed, the left hand's triangles are reversed (its mirror). Vulkan's winding is
         * the one seen on the screen (its area formula carries the y-down framebuffer's sign), and the projection
         * mirrors nothing: a face counter-clockwise from outside is front-facing (headless run 181440: CLOCKWISE drew
         * the hands inside out and culled every occluder seen from the front) */
        VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_BACK_BIT,
                                                     .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                     .lineWidth = 1};
        VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                   .rasterizationSamples = c->samples};
        VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                    .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
                                                    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
        VkPipelineColorBlendAttachmentState cb = {
            .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 0xf};
        VkPipelineColorBlendStateCreateInfo cbs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                   .attachmentCount = 1, .pAttachments = &cb};
        VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                               .dynamicStateCount = 2, .pDynamicStates = dyn};
        VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2,
                                           .pStages = st, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
                                           .pViewportState = &vs, .pRasterizationState = &rs, .pMultisampleState = &ms,
                                           .pDepthStencilState = &ds, .pColorBlendState = &cbs, .pDynamicState = &dy,
                                           .layout = pl, .renderPass = c->rp, .subpass = 0};
        if ((r = f->CreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &gp, NULL, out)) != VK_SUCCESS)
            *out = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 2; i++)
        if (sm[i]) f->DestroyShaderModule(c->dev, sm[i], NULL);
    return r;
}

int hd_create(hd_ctx *c, VkDevice dev, const hd_vk *f, const VkPhysicalDeviceMemoryProperties *mp, VkFormat fmt,
              uint32_t w, uint32_t h, uint32_t samples, const uint16_t *idx_r, const uint16_t *idx_l, uint32_t ni,
              uint32_t nv) {
    memset(c, 0, sizeof *c);
    c->dev = dev, c->f = f, c->mp = *mp, c->fmt = fmt, c->w = w, c->h = h, c->ni = ni, c->nv = nv;
    c->samples = samples >= 8 ? VK_SAMPLE_COUNT_8_BIT : samples >= 4 ? VK_SAMPLE_COUNT_4_BIT
                 : samples >= 2 ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
    if (!w || !h || !ni || !nv) return fail(c, "an empty size", VK_ERROR_INITIALIZATION_FAILED);
    VkAttachmentDescription at[2] = {
        {.format = fmt, .samples = c->samples, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
        {.format = HD_DEPTH, .samples = c->samples, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference cr = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                          dr = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1,
                               .pColorAttachments = &cr, .pDepthStencilAttachment = &dr};
    /* the eye before: its resolve read the colour, its tests wrote the depth; after: the resolve reads the colour */
    VkSubpassDependency dep[2] = {
        {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
         .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT},
        {.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT}};
    VkRenderPassCreateInfo rpi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2,
                                  .pAttachments = at, .subpassCount = 1, .pSubpasses = &sp, .dependencyCount = 2,
                                  .pDependencies = dep};
    VkResult r = f->CreateRenderPass(dev, &rpi, NULL, &c->rp);
    if (r != VK_SUCCESS) return c->rp = VK_NULL_HANDLE, fail(c, "vkCreateRenderPass", r);
    if (image(c, fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
              &c->col, &c->col_mem, &c->col_v, "the colour image") ||
        image(c, HD_DEPTH, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &c->dep, &c->dep_mem,
              &c->dep_v, "the depth image"))
        return -1;
    VkImageView views[2] = {c->col_v, c->dep_v};
    VkFramebufferCreateInfo fbi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = c->rp,
                                   .attachmentCount = 2, .pAttachments = views, .width = w, .height = h, .layers = 1};
    if ((r = f->CreateFramebuffer(dev, &fbi, NULL, &c->fb)) != VK_SUCCESS) return c->fb = VK_NULL_HANDLE, fail(c, "vkCreateFramebuffer", r);
    /* the pipeline */
    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
    if ((r = f->CreatePipelineLayout(dev, &pli, NULL, &c->pl)) != VK_SUCCESS) return c->pl = VK_NULL_HANDLE, fail(c, "the pipeline", r);
    if ((r = pipeline(c, somvr_hands_frag_spv, sizeof somvr_hands_frag_spv, c->pl, NULL, &c->pipe)) != VK_SUCCESS)
        return fail(c, "the pipeline", r);
    /* the buffers: the vertices (HD_NSLOT slots), the indices (right, left, the occluders' quads) */
    c->slot_bytes = (VkDeviceSize)(4 * nv + 4 * HD_MAX_OCC + SOMVR_VH_ARROW_NV + SOMVR_VH_STRING_NV) * sizeof(hd_vert);
    /* (build 13: + the two ghosts; build 16: the arrow; build 18: the string) */
    c->slot_bytes = (c->slot_bytes + 255) & ~(VkDeviceSize)255;
    void *map = NULL;
    if (buffer(c, c->slot_bytes * HD_NSLOT, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &c->vb, &c->vb_mem, &map)) return -1;
    c->vb_map = map;
    memset(c->vb_map, 0, (size_t)(c->slot_bytes * HD_NSLOT));
    if (buffer(c, (VkDeviceSize)(2 * ni + HD_OCC_IDX * HD_MAX_OCC + SOMVR_VH_ARROW_NI + SOMVR_VH_STRING_NI) * sizeof(uint16_t),
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &c->ib, &c->ib_mem, &map))
        return -1;
    uint16_t *ix = map;
    memcpy(ix, idx_r, ni * sizeof *ix);
    memcpy(ix + ni, idx_l, ni * sizeof *ix);
    for (int q = 0; q < HD_MAX_OCC; q++) { /* both windings: an occluder hides from either side */
        static const uint16_t quad[HD_OCC_IDX] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
        memcpy(ix + 2 * ni + HD_OCC_IDX * q, quad, sizeof quad);
    }
    memcpy(ix + 2 * ni + HD_OCC_IDX * HD_MAX_OCC, somvr_vh_arrow_indices(), SOMVR_VH_ARROW_NI * sizeof *ix);
    memcpy(ix + 2 * ni + HD_OCC_IDX * HD_MAX_OCC + SOMVR_VH_ARROW_NI, somvr_vh_string_indices(),
           SOMVR_VH_STRING_NI * sizeof *ix);
    snprintf(c->why, sizeof c->why, "%ux%u per eye, %ux MSAA, format %d, %.1f MB of images", w, h, (unsigned)c->samples,
             (int)fmt, (double)c->vram / (1024.0 * 1024.0));
    return 0;
}

void hd_destroy(hd_ctx *c) {
    const hd_vk *f = c->f;
    if (!f || !c->dev) return;
    if (c->vb_map) f->UnmapMemory(c->dev, c->vb_mem);
    if (c->ib_mem) f->UnmapMemory(c->dev, c->ib_mem);
    if (c->pipe) f->DestroyPipeline(c->dev, c->pipe, NULL);
    if (c->pl) f->DestroyPipelineLayout(c->dev, c->pl, NULL);
    if (c->pipe_w) f->DestroyPipeline(c->dev, c->pipe_w, NULL);
    if (c->pl_w) f->DestroyPipelineLayout(c->dev, c->pl_w, NULL);
    if (c->wf) { /* the world's sampling (hd_world) */
        if (c->dpool) c->wf->DestroyDescriptorPool(c->dev, c->dpool, NULL); /* (frees the sets) */
        if (c->dsl) c->wf->DestroyDescriptorSetLayout(c->dev, c->dsl, NULL);
        if (c->smp) c->wf->DestroySampler(c->dev, c->smp, NULL);
    }
    if (c->zero_v) f->DestroyImageView(c->dev, c->zero_v, NULL);
    if (c->zero) f->DestroyImage(c->dev, c->zero, NULL);
    if (c->zero_mem) f->FreeMemory(c->dev, c->zero_mem, NULL);
    if (c->fb) f->DestroyFramebuffer(c->dev, c->fb, NULL);
    if (c->col_v) f->DestroyImageView(c->dev, c->col_v, NULL);
    if (c->dep_v) f->DestroyImageView(c->dev, c->dep_v, NULL);
    if (c->col) f->DestroyImage(c->dev, c->col, NULL);
    if (c->dep) f->DestroyImage(c->dev, c->dep, NULL);
    if (c->rp) f->DestroyRenderPass(c->dev, c->rp, NULL);
    if (c->vb) f->DestroyBuffer(c->dev, c->vb, NULL);
    if (c->ib) f->DestroyBuffer(c->dev, c->ib, NULL);
    VkDeviceMemory mem[4] = {c->col_mem, c->dep_mem, c->vb_mem, c->ib_mem};
    for (int i = 0; i < 4; i++)
        if (mem[i]) f->FreeMemory(c->dev, mem[i], NULL);
    VkDevice d = c->dev;
    memset(c, 0, sizeof *c);
    (void)d;
}

int hd_world(hd_ctx *c, const hd_vkw *wf, float soft_m, float bias_m, float texel, int debug) {
    const hd_vk *f = c->f;
    if (!f || !c->dev || !c->pipe) return fail(c, "the world: no drawing", VK_ERROR_INITIALIZATION_FAILED);
    c->wf = wf, c->soft_m = soft_m, c->bias_m = bias_m, c->texel = texel, c->debug = debug;
    VkResult r;
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo dli = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .bindingCount = 1, .pBindings = &b};
    if ((r = wf->CreateDescriptorSetLayout(c->dev, &dli, NULL, &c->dsl)) != VK_SUCCESS)
        return c->dsl = VK_NULL_HANDLE, fail(c, "the world: its descriptor set layout", r);
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * HD_NSLOT};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2 * HD_NSLOT,
                                      .poolSizeCount = 1, .pPoolSizes = &ps};
    if ((r = wf->CreateDescriptorPool(c->dev, &dpi, NULL, &c->dpool)) != VK_SUCCESS)
        return c->dpool = VK_NULL_HANDLE, fail(c, "the world: its descriptor pool", r);
    VkDescriptorSetLayout ls[2 * HD_NSLOT];
    for (int i = 0; i < 2 * HD_NSLOT; i++) ls[i] = c->dsl;
    VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = c->dpool,
                                       .descriptorSetCount = 2 * HD_NSLOT, .pSetLayouts = ls};
    if ((r = wf->AllocateDescriptorSets(c->dev, &dai, &c->dset[0][0])) != VK_SUCCESS)
        return fail(c, "the world: its descriptor sets", r);
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
                              .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0};
    if ((r = wf->CreateSampler(c->dev, &si, NULL, &c->smp)) != VK_SUCCESS)
        return c->smp = VK_NULL_HANDLE, fail(c, "the world: its sampler", r);
    /* the zero image: 1x1, 0 = the sky (nothing hides the hands) */
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R32_SFLOAT, .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if ((r = f->CreateImage(c->dev, &ci, NULL, &c->zero)) != VK_SUCCESS) return c->zero = VK_NULL_HANDLE, fail(c, "the world: the zero image", r);
    VkMemoryRequirements mr;
    f->GetImageMemoryRequirements(c->dev, c->zero, &mr);
    int t = mem_type(&c->mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (t < 0) t = mem_type(&c->mp, mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = (uint32_t)(t < 0 ? 0 : t)};
    if (t < 0 || (r = f->AllocateMemory(c->dev, &ai, NULL, &c->zero_mem)) != VK_SUCCESS)
        return c->zero_mem = VK_NULL_HANDLE, fail(c, "the world: the zero image's memory", t < 0 ? VK_ERROR_FEATURE_NOT_PRESENT : r);
    if ((r = f->BindImageMemory(c->dev, c->zero, c->zero_mem, 0)) != VK_SUCCESS) return fail(c, "the world: the zero image", r);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = c->zero,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R32_SFLOAT,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if ((r = f->CreateImageView(c->dev, &vi, NULL, &c->zero_v)) != VK_SUCCESS)
        return c->zero_v = VK_NULL_HANDLE, fail(c, "the world: the zero image's view", r);
    /* the pipeline: the same push constants, the depth in set 0; SOFT, BIAS, TEXEL specialised */
    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                      .pSetLayouts = &c->dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
    if ((r = f->CreatePipelineLayout(c->dev, &pli, NULL, &c->pl_w)) != VK_SUCCESS)
        return c->pl_w = VK_NULL_HANDLE, fail(c, "the world: its pipeline layout", r);
    struct { float f[3]; int32_t dbg; } sc = {{soft_m, bias_m, texel}, debug ? 1 : 0};
    VkSpecializationMapEntry me[4] = {{0, 0, 4}, {1, 4, 4}, {2, 8, 4}, {3, 12, 4}};
    VkSpecializationInfo spec = {4, me, sizeof sc, &sc};
    if ((r = pipeline(c, somvr_hands_world_frag_spv, sizeof somvr_hands_world_frag_spv, c->pl_w, &spec, &c->pipe_w)) != VK_SUCCESS)
        return fail(c, "the world: its pipeline", r);
    return 0;
}

hd_vert *hd_slot_verts(hd_ctx *c, uint32_t s) { return (hd_vert *)(void *)(c->vb_map + c->slot_bytes * (s % HD_NSLOT)); }
#define HD_GHOST_V(c, h) (2 * (c)->nv + 4 * HD_MAX_OCC + (uint32_t)(h) * (c)->nv) /* a ghost's first vertex in a slot */
hd_vert *hd_slot_ghost(hd_ctx *c, uint32_t s, int h) { return hd_slot_verts(c, s) + HD_GHOST_V(c, h); }
#define HD_ARROW_V(c) (4 * (c)->nv + 4 * HD_MAX_OCC) /* build 16: the arrow's first vertex in a slot */
#define HD_ARROW_I(c) (2 * (c)->ni + HD_OCC_IDX * HD_MAX_OCC) /* ... and its first index */
hd_vert *hd_slot_arrow(hd_ctx *c, uint32_t s) { return hd_slot_verts(c, s) + HD_ARROW_V(c); }
#define HD_STRING_V(c) (HD_ARROW_V(c) + SOMVR_VH_ARROW_NV) /* build 18: the string's first vertex in a slot */
#define HD_STRING_I(c) (HD_ARROW_I(c) + SOMVR_VH_ARROW_NI) /* ... and its first index */
hd_vert *hd_slot_string(hd_ctx *c, uint32_t s) { return hd_slot_verts(c, s) + HD_STRING_V(c); }

void hd_quad(hd_vert v[4], const float o[3], const float hx[3], const float hy[3]) {
    static const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 3; i++) v[k].p[i] = o[i] + sx[k] * hx[i] + sy[k] * hy[i], v[k].n[i] = 0;
        v[k].a = 0;
    }
}

void hd_eye_matrix(const float q[4], const float p[3], const float fov[4], float zn, float zf, float out[16]) {
    /* the view: R^T (x - p) */
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                     {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                     {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    float V[16] = {0}; /* column-major */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) V[c * 4 + r] = R[c][r];
        V[12 + r] = -(R[0][r] * p[0] + R[1][r] * p[1] + R[2][r] * p[2]);
    }
    V[15] = 1;
    /* the projection (the OpenXR SDK's XrMatrix4x4f_CreateProjectionFov for Vulkan: y down, z 0..1) */
    float tl = tanf(fov[0]), tr = tanf(fov[1]), tu = tanf(fov[2]), td = tanf(fov[3]);
    float tw = tr - tl, th = td - tu;
    float P[16] = {0};
    P[0] = 2 / tw, P[8] = (tr + tl) / tw;
    P[5] = 2 / th, P[9] = (tu + td) / th;
    P[10] = -zf / (zf - zn), P[14] = -(zf * zn) / (zf - zn);
    P[11] = -1;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += P[k * 4 + r] * V[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

typedef struct {
    float vp[16], eye[4], light[4], base[4], params[4];
} hd_pc;
_Static_assert(sizeof(hd_pc) == 128, "the push constants");

static void barrier(const hd_vk *f, VkCommandBuffer cmd, VkImage img, VkAccessFlags sa, VkAccessFlags da,
                    VkImageLayout ol, VkImageLayout nl, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = sa, .dstAccessMask = da,
                              .oldLayout = ol, .newLayout = nl, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    f->CmdPipelineBarrier(cmd, ss, ds, 0, 0, NULL, 0, NULL, 1, &b);
}

int hd_box(const float vp[16], const hd_vert *v, uint32_t n, uint32_t w, uint32_t h, int pad, int32_t box[4]) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    uint32_t behind = 0;
    for (uint32_t i = 0; i < n; i++) {
        const float *p = v[i].p;
        float cx = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
        float cy = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
        float cw = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
        if (!(cw > 1e-3f)) { /* behind the eye (or at it) */
            behind++;
            continue;
        }
        float x = (cx / cw * 0.5f + 0.5f) * (float)w, y = (cy / cw * 0.5f + 0.5f) * (float)h;
        x0 = fminf(x0, x), x1 = fmaxf(x1, x), y0 = fminf(y0, y), y1 = fmaxf(y1, y);
    }
    if (behind && behind == n) { /* all of it behind: nothing */
        box[0] = box[1] = box[2] = box[3] = 0;
        return 0;
    }
    if (behind) { /* across the eye's plane: the whole eye */
        box[0] = 0, box[1] = 0, box[2] = (int32_t)w, box[3] = (int32_t)h;
        return 2;
    }
    float lo = (float)-pad, hx = (float)w + (float)pad, hy = (float)h + (float)pad;
    if (!(x1 >= lo && x0 <= hx && y1 >= lo && y0 <= hy)) { /* nothing on the screen (no vertex: likewise) */
        box[0] = box[1] = box[2] = box[3] = 0;
        return 0;
    }
    box[0] = (int32_t)fmaxf(0, floorf(x0) - (float)pad), box[1] = (int32_t)fmaxf(0, floorf(y0) - (float)pad);
    box[2] = (int32_t)fminf((float)w, ceilf(x1) + (float)pad), box[3] = (int32_t)fminf((float)h, ceilf(y1) + (float)pad);
    if (box[2] <= box[0] || box[3] <= box[1]) {
        box[0] = box[1] = box[2] = box[3] = 0;
        return 0;
    }
    return 1;
}

void hd_record(hd_ctx *c, VkCommandBuffer cmd, uint32_t slot, hd_frame *fr, VkImage dst, VkImageLayout dst_old,
               VkImageLayout dst_new) {
    const hd_vk *f = c->f;
    VkDeviceSize off = c->slot_bytes * (slot % HD_NSLOT);
    /* dst all transparent (a fast clear), then each eye's box only: the clears, the samples and the resolve stay small */
    barrier(f, cmd, dst, 0, VK_ACCESS_TRANSFER_WRITE_BIT, dst_old, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue zero = {.float32 = {0, 0, 0, 0}};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    f->CmdClearColorImage(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    if (c->pipe_w && !c->zero_ready) { /* the world's zero image, once: 0 = the sky, then sampled */
        barrier(f, cmd, c->zero, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        f->CmdClearColorImage(cmd, c->zero, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
        barrier(f, cmd, c->zero, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        c->zero_ready = 1;
    }
    VkMemoryBarrier tt = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    f->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &tt, 0, NULL, 0, NULL);
    const hd_vert *verts = hd_slot_verts(c, slot);
    for (int e = 0; e < 2; e++) {
        int32_t *b = fr->box[e];
        int got = 0;
        b[0] = b[1] = b[2] = b[3] = 0;
        for (int h = 0; h < 6 && got < 2; h++) { /* the union of the drawn hands' boxes (2, 3: the ghosts; 4: the arrow;
                                                  * 5: the string) */
            int32_t hb[4];
            if (h < 2 ? !fr->hand[h] : h < 4 ? !(fr->ghost_a[h - 2] > 0) : h == 4 ? !(fr->arrow_a > 0) : !(fr->string_a > 0))
                continue;
            int r = h == 5 ? hd_box(fr->vp[e], verts + HD_STRING_V(c), SOMVR_VH_STRING_NV, c->w, c->h, 4, hb)
                  : h == 4 ? hd_box(fr->vp[e], verts + HD_ARROW_V(c), SOMVR_VH_ARROW_NV, c->w, c->h, 4, hb)
                           : hd_box(fr->vp[e], verts + (h < 2 ? (size_t)h * c->nv : HD_GHOST_V(c, h - 2)), c->nv, c->w,
                                    c->h, 4, hb);
            if (!r) continue;
            if (r == 2 || !got) memcpy(b, hb, sizeof hb);
            else b[0] = b[0] < hb[0] ? b[0] : hb[0], b[1] = b[1] < hb[1] ? b[1] : hb[1],
                 b[2] = b[2] > hb[2] ? b[2] : hb[2], b[3] = b[3] > hb[3] ? b[3] : hb[3];
            got = r == 2 ? 2 : 1;
        }
        if (!got) continue; /* no hand on this eye's screen: its half stays clear */
        VkRect2D area = {{b[0], b[1]}, {(uint32_t)(b[2] - b[0]), (uint32_t)(b[3] - b[1])}};
        VkClearValue cv[2] = {{.color = {.float32 = {0, 0, 0, 0}}}, {.depthStencil = {1.0f, 0}}};
        VkRenderPassBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = c->rp,
                                    .framebuffer = c->fb, .renderArea = area, .clearValueCount = 2, .pClearValues = cv};
        f->CmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = {0, 0, (float)c->w, (float)c->h, 0, 1}; /* the whole eye: the projection as the eye's */
        f->CmdSetViewport(cmd, 0, 1, &vp);
        f->CmdSetScissor(cmd, 0, 1, &area);
        VkPipelineLayout pl = c->pl;
        int world = 0;
        if (c->pipe_w) { /* build 11: this eye's depth (or the zero image) into the slot's set */
            world = fr->depth_k > 0 && fr->world[e];
            VkDescriptorImageInfo ii = {c->smp, world ? fr->world[e] : c->zero_v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = c->dset[slot % HD_NSLOT][e],
                                       .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .pImageInfo = &ii};
            c->wf->UpdateDescriptorSets(c->dev, 1, &wr, 0, NULL);
            pl = c->pl_w;
            f->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe_w);
            c->wf->CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &c->dset[slot % HD_NSLOT][e], 0,
                                         NULL);
        } else
            f->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe);
        f->CmdBindVertexBuffers(cmd, 0, 1, &c->vb, &off);
        f->CmdBindIndexBuffer(cmd, c->ib, 0, VK_INDEX_TYPE_UINT16);
        hd_pc pc;
        memcpy(pc.vp, fr->vp[e], sizeof pc.vp);
        pc.eye[0] = fr->eye[e][0], pc.eye[1] = fr->eye[e][1], pc.eye[2] = fr->eye[e][2];
        pc.eye[3] = c->pipe_w ? (world ? fr->depth_k : 0) : 1; /* (the plain shader does not read it) */
        pc.light[0] = fr->light[0], pc.light[1] = fr->light[1], pc.light[2] = fr->light[2], pc.light[3] = fr->opacity;
        pc.params[0] = fr->key, pc.params[1] = fr->ambient, pc.params[2] = fr->rim, pc.params[3] = fr->spec;
        pc.base[0] = pc.base[1] = pc.base[2] = pc.base[3] = 0; /* the occluders first: their depth hides what is behind */
        f->CmdPushConstants(cmd, pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        for (uint32_t q = 0; q < fr->nocc && q < HD_MAX_OCC; q++)
            f->CmdDrawIndexed(cmd, HD_OCC_IDX, 1, 2 * c->ni + HD_OCC_IDX * q, (int32_t)(2 * c->nv + 4 * q), 0);
        pc.base[0] = fr->base[0], pc.base[1] = fr->base[1], pc.base[2] = fr->base[2], pc.base[3] = 1;
        /* the world's pipeline: a holding hand's factor in base.a (>= 1); one push while the hands share it */
        float hm[2] = {world && fr->held[0] > 1 ? fr->held[0] : 1, world && fr->held[1] > 1 ? fr->held[1] : 1};
        int split = hm[0] != hm[1] && fr->hand[0] && fr->hand[1];
        if (!split) {
            pc.base[3] = fr->hand[0] ? hm[0] : hm[1];
            f->CmdPushConstants(cmd, pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        }
        for (int h = 0; h < 2; h++) {
            if (!fr->hand[h]) continue;
            if (split) {
                pc.base[3] = hm[h];
                f->CmdPushConstants(cmd, pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            }
            if (h == 0) f->CmdDrawIndexed(cmd, c->ni, 1, c->ni, 0, 0);        /* the left: mirrored triangles */
            else f->CmdDrawIndexed(cmd, c->ni, 1, 0, (int32_t)c->nv, 0);      /* the right */
        }
        /* build 13: the ghosts, faint, with the plain pipeline (the world's depth would hide them: they are in it) */
        if (fr->ghost_a[0] > 0 || fr->ghost_a[1] > 0) {
            if (c->pipe_w) f->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe);
            pc.base[0] = fr->ghost_base[0], pc.base[1] = fr->ghost_base[1], pc.base[2] = fr->ghost_base[2], pc.base[3] = 1;
            pc.params[0] = 0.35f * fr->key, pc.params[1] = 1.2f * fr->ambient, pc.params[2] = 1.4f, pc.params[3] = 0;
            for (int h = 0; h < 2; h++) {
                if (!(fr->ghost_a[h] > 0)) continue;
                pc.light[3] = fr->ghost_a[h] > 1 ? 1 : fr->ghost_a[h];
                f->CmdPushConstants(cmd, c->pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
                f->CmdDrawIndexed(cmd, c->ni, 1, h == 0 ? c->ni : 0, (int32_t)HD_GHOST_V(c, h), 0);
            }
        }
        /* build 16: the arrow, the wraith's light (the plain pipeline: held in the hand, never in the world's depth) */
        if (fr->arrow_a > 0) {
            if (c->pipe_w && !(fr->ghost_a[0] > 0 || fr->ghost_a[1] > 0))
                f->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe);
            pc.base[0] = fr->arrow_base[0], pc.base[1] = fr->arrow_base[1], pc.base[2] = fr->arrow_base[2], pc.base[3] = 1;
            pc.params[0] = 0.45f * fr->key, pc.params[1] = 0.9f, pc.params[2] = 0.8f, pc.params[3] = 0.15f;
            pc.light[3] = fr->arrow_a > 1 ? 1 : fr->arrow_a;
            f->CmdPushConstants(cmd, c->pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            f->CmdDrawIndexed(cmd, SOMVR_VH_ARROW_NI, 1, HD_ARROW_I(c), (int32_t)HD_ARROW_V(c), 0);
        }
        /* build 18: the bow's string, the same light (the plain pipeline: seen from both sides, the string hand hides it) */
        if (fr->string_a > 0) {
            if (c->pipe_w && !(fr->arrow_a > 0) && !(fr->ghost_a[0] > 0 || fr->ghost_a[1] > 0))
                f->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe);
            pc.base[0] = fr->string_base[0], pc.base[1] = fr->string_base[1], pc.base[2] = fr->string_base[2], pc.base[3] = 1;
            pc.params[0] = 0.45f * fr->key, pc.params[1] = 0.9f, pc.params[2] = 0.8f, pc.params[3] = 0.15f;
            pc.light[3] = fr->string_a > 1 ? 1 : fr->string_a;
            f->CmdPushConstants(cmd, c->pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
            f->CmdDrawIndexed(cmd, SOMVR_VH_STRING_NI, 1, HD_STRING_I(c), (int32_t)HD_STRING_V(c), 0);
        }
        f->CmdEndRenderPass(cmd);
        VkImageSubresourceLayers sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        VkOffset3D so = {b[0], b[1], 0}, dof = {(int32_t)((uint32_t)e * c->w) + b[0], b[1], 0};
        VkExtent3D ex = {area.extent.width, area.extent.height, 1};
        if (c->samples > VK_SAMPLE_COUNT_1_BIT) {
            VkImageResolve rg = {.srcSubresource = sub, .srcOffset = so, .dstSubresource = sub, .dstOffset = dof, .extent = ex};
            f->CmdResolveImage(cmd, c->col, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &rg);
        } else {
            VkImageCopy cp = {.srcSubresource = sub, .srcOffset = so, .dstSubresource = sub, .dstOffset = dof, .extent = ex};
            f->CmdCopyImage(cmd, c->col, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                            &cp);
        }
    }
    barrier(f, cmd, dst, VK_ACCESS_TRANSFER_WRITE_BIT,
            dst_new == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT
                                                            : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_new, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}
