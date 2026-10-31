/* mock_common.h - shared by test_layer.c (fake Vulkan below the layer) and mock_xr.c (fake OpenXR loader + runtime and
 * fake Vulkan loader trampoline). */
#ifndef MOCK_COMMON_H
#define MOCK_COMMON_H
#include <stdint.h>

#define MOCK_NSC 8         /* live swapchains (eyes/game, HUD, pointer, comfort, fade, menu, env sky, kept frame) */
#define MOCK_NQ 16         /* layers per frame it records (all: the layer sends up to sixteen) */
#define MOCK_MAX_LAYERS 16 /* XR_MIN_COMPOSITION_LAYERS_SUPPORTED: more is a violation (and LAYER_LIMIT_EXCEEDED) */
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
    uint32_t create_flags;   /* XrSwapchainCreateFlags */
    int acquires;            /* xrAcquireSwapchainImage calls on it */
} mockxr_swapchain;

typedef struct {
    uint32_t layer_count;
    int type;              /* XrStructureType of layer 0 */
    int32_t rect[2][4];    /* subImage rects (view 0 and 1 for projection; [0] for quad) */
    float size[2], pos[3]; /* quad */
    int same_swapchain;    /* the layer refers to the swapchain that was created */
    float pose[2][7];      /* projection views: orientation x y z w, position x y z */
    float fov[2][4];       /* projection views: left right up down */
    int released;          /* images of the first swapchain released since the previous xrEndFrame */
    int64_t time;          /* displayTime */
    uint64_t flags;        /* layer 0's layerFlags */
    /* layer 1 (the HUD quad, H11), if there is one */
    int type2;             /* its XrStructureType (0: no second layer) */
    int space2;            /* 1 the reference space, 2 the VIEW space, 0 another */
    int sc2;               /* the swapchain it shows: 0 the first, 1 the second, -1 another */
    uint64_t flags2;       /* layerFlags */
    int eyes2;             /* eyeVisibility */
    int32_t rect2[4];      /* subImage rect */
    float size2[2], pose2[7];
    int released2;         /* images of the second swapchain released since the previous xrEndFrame */
    /* every quad layer after layer 0 (the HUD quad, the pointer's ray and dot, the VR menu's panel, ray and dot,
     * comfort's ring and blink, the fade; the loading environment's faces, dim quads and the menu quad after them):
     * q[i] = layer i, i = 1..MOCK_NQ-1 (type only for others) */
    struct {
        int type, space, sc, eyes; /* space: 1 the reference space, 2 a VIEW space; sc: its swapchain slot (-1 none) */
        int view;                  /* which VIEW space (0 the first made, 1 the second), -1 none */
        uint64_t flags;
        int32_t rect[4];
        float size[2], pose[7];
    } q[MOCK_NQ];
    int released_n[MOCK_NSC]; /* images released per swapchain slot since the previous xrEndFrame */
    /* SoM loading env: an equirect2 layer as layer 0; the first projection after layer 0 (the kept world) */
    float eq_radius, eq_angle[3]; /* radius; centralHorizontal, upperVertical, lowerVertical angles */
    int eq_sc;                    /* its swapchain slot (-1 none) */
    int32_t eq_rect[4];
    float eq_pose[7];
    uint64_t eq_flags;
    int pj_index;                 /* the layer index of the first projection after layer 0 (0: none) */
    int pj_sc[2];                 /* its views' swapchain slots */
    int32_t pj_rect[2][4];
    float pj_pose[2][7], pj_fov[2][4];
    uint64_t pj_flags;
} mockxr_end;

typedef int (*mockxr_calls_fn)(const char *name);
typedef int (*mockxr_total_fn)(void);
typedef int (*mockxr_violations_fn)(const char **first);
typedef const mockxr_binding *(*mockxr_binding_get_fn)(void);
typedef const mockxr_swapchain *(*mockxr_swapchain_get_fn)(void);
typedef const mockxr_swapchain *(*mockxr_swapchain2_get_fn)(void); /* the second live swapchain (the HUD's) */
typedef int (*mockxr_sc2_live_fn)(void);                            /* 1 while a second swapchain lives */
typedef const mockxr_swapchain *(*mockxr_swapchain_k_fn)(int k);     /* slot k (0..MOCK_NSC - 1) */
typedef int (*mockxr_sc_live_k_fn)(int k);                          /* 1 while slot k lives */
typedef int (*mockxr_sc_find_fn)(uint32_t w, uint32_t h);           /* the live slot of that size, -1 none */
typedef int (*mockxr_view_live_fn)(void);                           /* VIEW reference spaces alive */
typedef int (*mockxr_rejects_fn)(void);                             /* MOCKXR_REJECT_SC's rejections so far */
typedef int (*mockxr_floor_locates_fn)(void);                       /* SOMVR_XR_FLOOR: LOCAL located in its STAGE */
typedef const mockxr_end *(*mockxr_last_end_fn)(void);
typedef int (*mockxr_live_fn)(void); /* sessions + spaces + swapchains still alive */
typedef int (*mockxr_wait_timeouts_fn)(void); /* build 27: XR_TIMEOUT_EXPIRED results given (MOCKXR_WAIT_TIMEOUT) */
typedef int (*mockxr_after_unload_fn)(void); /* MOCKXR_UNLOAD_AT_EXIT: XR calls after the exit-time unload */
typedef int (*mockxr_unloaded_fn)(void);
typedef int (*mockxr_inst_live_fn)(void);
typedef void *(*mockxr_tramp_pd_fn)(void);
typedef void (*mockvk_loader_lock_fn)(int held);
typedef int (*mockxr_locates_fn)(void);        /* xrLocateViews calls (any thread) */
typedef int (*mockxr_locate_bad_fn)(void);     /* xrLocateViews without a live session, or a bad argument */
typedef int (*mockxr_space_type_fn)(void);     /* the XrReferenceSpaceType created */
typedef int64_t (*mockxr_time_fn)(void);       /* the last predicted display time */
typedef int (*mockxr_ext_enabled_fn)(const char *name); /* SoM loading env: the instance enabled that extension */
typedef int (*mockxr_sc_count_fn)(void);       /* swapchains alive */

/* ---- the action API (motion controllers, docs/re-xr-input.md) ---- */
typedef struct {
    uint32_t buttons;            /* held, by action name: lower 1, upper 2, stick_click 4, menu 8, trigger_touch 16,
                                    thumb_touch 32 (= SOMVR_BTN_*) */
    float trigger, squeeze, stick[2];
    float grip[7], aim[7];       /* orientation x y z w, position x y z */
    float lin_vel[3], ang_vel[3];
    uint32_t grip_flags, aim_flags, vel_flags; /* what xrLocateSpace reports while the pose action is active */
} mockxr_hand;
typedef struct {
    int applies, stops;          /* xrApplyHapticFeedback / xrStopHapticFeedback calls */
    float amplitude, frequency;  /* of the last apply */
    int64_t duration;
} mockxr_haptic;
typedef struct {
    int sets_live, actions, spaces_live; /* action sets alive, actions created, action spaces alive */
    int attached, suggested_ok, focused, syncs; /* syncs: focused ones (XR_SUCCESS) */
    int64_t locate_time[2];      /* the time of the last xrLocateSpace of grip L / R */
    int prof_events;             /* XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED sent */
} mockxr_input;
typedef void (*mockxr_input_set_fn)(int hand, const mockxr_hand *h);
typedef const mockxr_haptic *(*mockxr_haptic_get_fn)(int hand);
typedef const mockxr_input *(*mockxr_input_get_fn)(void);
typedef int (*mockxr_first_fn)(const char *name); /* global order of the first call of name (1, 2, ...; 0 = never) */
typedef int (*mockxr_space_bad_fn)(void);         /* xrLocateSpace with a dead space/session or a bad argument */
/* build 27 (re-xr-pipeline.md 119): XR_FB_display_refresh_rate (MOCKXR_REFRESH): the requests taken or refused, the
 * last rate requested, the display's rate now, the change events delivered */
typedef struct { int requests, refused, events, enumerates; float last, now; } mockxr_refresh_info;
typedef const mockxr_refresh_info *(*mockxr_refresh_get_fn)(void);

#endif
