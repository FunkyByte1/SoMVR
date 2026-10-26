/* mock_common.h - shared by test_layer.c (fake Vulkan below the layer) and mock_xr.c (fake OpenXR loader + runtime and
 * fake Vulkan loader trampoline). */
#ifndef MOCK_COMMON_H
#define MOCK_COMMON_H
#include <stdint.h>

#define MOCK_VENDOR 0x1002u
#define MOCK_DEVICE 0x73bfu
#define MOCK_GPU_NAME "AMD Radeon RX 6800 XT (RADV NAVI21)"
#define MOCK_UUID_BYTE 0x5a
/* what WiVRn's libopenxr_wivrn.so returns for XR_KHR_vulkan_enable (strings in the installed binary) */
#define MOCK_INST_EXTS                                                                                                 \
    "VK_KHR_external_fence_capabilities VK_KHR_external_memory_capabilities VK_KHR_external_semaphore_capabilities "  \
    "VK_KHR_get_physical_device_properties2"
#define MOCK_DEV_EXTS                                                                                                  \
    "VK_KHR_dedicated_allocation VK_KHR_external_fence VK_KHR_external_memory VK_KHR_external_semaphore "             \
    "VK_KHR_get_memory_requirements2 VK_KHR_image_format_list VK_KHR_external_memory_fd VK_KHR_external_semaphore_fd " \
    "VK_KHR_external_fence_fd"

typedef struct {
    void *instance, *physical_device, *device;
    uint32_t family, index;
} mockxr_binding;

typedef struct {
    uint32_t width, height, usage;
    int64_t format;
} mockxr_swapchain;

typedef struct {
    uint32_t layer_count;
    int type;              /* XrStructureType of layer 0 */
    int32_t rect[2][4];    /* subImage rects (view 0 and 1 for projection; [0] for quad) */
    float size[2], pos[3]; /* quad */
    int same_swapchain;    /* the layer refers to the swapchain that was created */
} mockxr_end;

typedef int (*mockxr_calls_fn)(const char *name);
typedef int (*mockxr_total_fn)(void);
typedef int (*mockxr_violations_fn)(const char **first);
typedef const mockxr_binding *(*mockxr_binding_get_fn)(void);
typedef const mockxr_swapchain *(*mockxr_swapchain_get_fn)(void);
typedef const mockxr_end *(*mockxr_last_end_fn)(void);
typedef int (*mockxr_live_fn)(void); /* sessions + spaces + swapchains still alive */
typedef int (*mockxr_inst_live_fn)(void);
typedef void *(*mockxr_tramp_pd_fn)(void);
typedef void (*mockvk_loader_lock_fn)(int held);

#endif
