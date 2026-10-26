# Shadow of Mordor VR: the motion-controller input path (OpenXR actions -> `somvr_input` -> the game's commands)

**Status (2026-10-19, later, offline; nothing launched):** steps **L1, L2 and A1 are built** (5.2): the ABI header
`mod/somvr/somvr_input.h`, the layer's action set, sync, snapshot and haptics (`mod/somvr/vklayer/`), tests green
(re-xr-pipeline.md 6.2 has the details and counts). **S1-S3 are built too** (re-xr-pipeline.md 6.4: `SOMVR_INPUT=1`;
`mod/somvr/somvr_ctl.c`, `somvr_hands.c`, the input region of `somvr.c`; `test_ctl` 481, `test_hands` 29, `test_combat`
149, the selftest's input part, `check_sites.py` 42/42), with the controller blade of re-combat-vr.md wired to the right
grip. Deviations from this design are marked *built:* below. Not built: R0/R1 (runs), the laser, the arm IK, the
M5/M7 buttons (bow, wraith charge, Shadow Strike, mounts), the shake/shove struggle gestures, the 1 s menu hold. Open
for the probe's owner: `mod/probe/build.sh --check`'s collision test now names the four CWBInput slots (somvr's IN_TICK..
IN_ISON, switch-gated like PM: its EXCLUSIVE list needs them). 2026-10-20: IN_TICK is now also patched with `SOMVR_EYE_SIZE` alone
(its runtime switch, re-xr-pipeline.md 9.1); the pad glyphs without a pad are re-armed from the same tick (3.2's
status note, re-xr-pipeline.md 9.2).

Design only (2026-10-19), offline: nothing launched, no OpenXR app run, no code changed. Sources: `docs/design-vr.md`
(sections 0-3, 9, 11), `re-input.md`, `runtime-probes.md` (register (a), P1 and the run-3 results), `re-xr-pipeline.md`,
`re-m1m2.md`, `re-animation.md` (sections 1-4, 8), `mod/somvr/somvr_shared.h` (ABI v2), `mod/somvr/vklayer/somvr_vklayer.c`
(session setup, `xr_poll`, `xr_frame`, `xr_wait_publish`), `mod/somvr/somvr_views.c` and `somvr_eyes.h` (time pick, rig),
`mod/probe/somvr_probe.c` (the `VIN` injector and its words), the OpenXR registry `xr.xml` of SDK 1.1.61 (externsync), WiVRn
26.6.2's server source (`shared/downloads/wivrn-src-26.6.2`: `server/driver/wivrn_controller.cpp`, `pose_list.cpp`,
`wivrn_session.cpp`), and the interaction profiles logged by Hand of Fate 2 runs (OpenComposite: `oculus/touch_controller`
on the user's Quest 2 under WiVRn, 2026-10-19; `valve/index_controller` under HoF2's Monado remote driver, 2026-10-17).

Conventions as in re-xr-pipeline.md (`m()` = the OpenXR -> game mirror, `R(q)`, `Y(a)`, heading `h(q)`, `+` heading = to
the right); addresses = runtime VAs of BuildID 87ec5d6f... (non-PIE). **[high]** read in code, logs or the spec,
**[med]** inferred, **[low]** a guess; **RT** = needs a run; **needs RE** = static work left.

## 0. Summary

| Question | Decision | Conf. |
|---|---|---|
| Who calls OpenXR | the **layer** owns the action set, the bindings, the attach, `xrSyncActions`, the state reads and the haptics (present thread). The **game thread** (somvr) only locates the two grip spaces at the frame's display time, as it already does with `xrLocateViews` | high (spec), RT (cost) |
| Profiles | `oculus/touch_controller` (the Quest 2 on WiVRn), `valve/index_controller` (the Monado remote test rig of design-vr 11), `khr/simple_controller` (fallback) | high (logs) |
| Attach | in `xr_session_setup` (first `vkQueuePresentKHR`): actions and suggestions before `xrCreateSession`, action spaces and `xrAttachSessionActionSets` right after the reference space; once per process | high |
| Sync | once per present, right after `xrWaitFrame` (`xr_wait_publish`); a snapshot under a seqlock | high |
| Pose time | one display time `T'` per game frame, picked at the input tick; the hand locate and H4's `xrLocateViews` both use it | med (RT: one tick per H4) |
| ABI | `somvr_shared` stays v2 byte for byte; a second exported object `somvr_input` (magic `SOMRINP1`, v1, 480 bytes) carries the controller state, the action spaces and the haptic requests. v2 layers and v2 preloads keep working | high |
| Game injection | the probe's binding-layer path made continuous: `CWBInput` vf_22/vf_23/vf_25 overrides for exact command records plus ON/OFF events through `FUN_00aab660`/`FUN_00aab5d0`, on the game thread, in a post-hook of `CWBInput::vf_04` | high (run 3, held axes), RT (events) |
| Input mode | pad mode forced (`FUN_00a0eeb0(..., 1)`, byte `0x46b0d61 = 0`): pad glyphs, the pad combat map, no flip back on keyboard/mouse activity | high static, RT (I4) |
| Menus | first the controllers act as an Xbox pad (passthrough to the bound, active commands); the laser pointer (mouse mode) later | med |
| Never injected | the stock strike, counter and stun (`CombatPrimary01-03` = pad X, Y, B), the camera axes, `CenterView`, the KBM combat commands; G4 vetoes the rest | high (run 3 bindings) |
| Hands | kept in XR space until used; one rig state per frame for the eyes, the IK targets and the blades; the grip drives `R_Weapon`/`L_Weapon` through the arm IK (G11) | med |

## 1. The OpenXR side in the layer

### 1.1 Threads and owners

| Work | Thread | Where |
|---|---|---|
| action set, actions, suggestions, action spaces, attach | present (= render) thread, `X.mx` held | `xr_session_setup` (the session is made there, lazily) |
| `xrSyncActions`, `xrGetActionState*`, the snapshot locate of grip and aim | present thread | `xr_wait_publish`, right after `xrWaitFrame` |
| `xrApplyHapticFeedback` / `xrStopHapticFeedback` | present thread | `xr_frame`, after `xr_poll` |
| events (interaction profile changed, focus) | present thread | `xr_poll` |
| `xrLocateSpace` of the grip spaces at `T'` | game thread | somvr's input tick, inside `somvr_xr_enter/leave` |

**[high]** `xr.xml` (SDK 1.1.61) marks no parameter of `xrSyncActions`, `xrGetActionState*`, `xrLocateSpace`,
`xrApplyHapticFeedback` or `xrStopHapticFeedback` as externally synchronised (only the `xrDestroy*` calls carry
`true_with_children`). So re-xr-pipeline.md 4.4's argument for `xrLocateViews` on the game thread covers the hand locate
too; the destroy rule is the only constraint, and the existing guard (`users`/`state`) handles it. **[med]** Monado and
WiVRn clients serialise IPC per connection, so a game-thread locate can wait behind the present thread's call in flight
(XI3 measures it).

### 1.2 The action set

One action set `somvr` ("Shadow of Mordor VR", priority 0). Every action has the subaction paths `/user/hand/left` and
`/user/hand/right` (`menu`: left only). All actions exist before the attach: the bindings are frozen for the session's
life at attach (OpenComposite restarts its session to change them, `XrBackend.cpp`; we never need to).

| Action | Type | Use |
|---|---|---|
| `grip_pose` | POSE | hands, weapons, IK, blades |
| `aim_pose` | POSE | laser, bow line |
| `trigger` | FLOAT | 0..1 |
| `squeeze` | FLOAT | 0..1 |
| `stick` | VECTOR2F | x right, y forward, raw |
| `stick_click` | BOOLEAN | |
| `lower` | BOOLEAN | A (right hand), X (left) |
| `upper` | BOOLEAN | B (right), Y (left) |
| `menu` | BOOLEAN (left) | the left menu button |
| `trigger_touch` | BOOLEAN | finger on the trigger |
| `thumb_touch` | BOOLEAN | thumb on any surface (several bindings: the runtime ORs them); with the two above, the "open palm" test |
| `haptic` | VIBRATION_OUTPUT | |

`lower`/`upper` per hand instead of A/B/X/Y actions make the left-handed option (design-vr 8) a plain hand swap in somvr.

### 1.3 Suggested bindings

One `xrSuggestInteractionProfileBindings` per profile: a path the runtime rejects fails only that profile (logged; the
others are still suggested). L/R = `/user/hand/left|right`; paths below start after `/user/hand/*/`.

| Action | `oculus/touch_controller` (1st) | `valve/index_controller` (2nd) | `khr/simple_controller` (3rd) |
|---|---|---|---|
| grip_pose / aim_pose | L, R `input/grip/pose`, `input/aim/pose` | same | same |
| trigger | L, R `input/trigger/value` | `input/trigger/value` | `input/select/click` |
| squeeze | L, R `input/squeeze/value` | `input/squeeze/value` | - |
| stick | L, R `input/thumbstick` | `input/thumbstick` | - |
| stick_click | L, R `input/thumbstick/click` | `input/thumbstick/click` | - |
| lower | L `input/x/click`, R `input/a/click` | L, R `input/a/click` | - |
| upper | L `input/y/click`, R `input/b/click` | L, R `input/b/click` | R `input/menu/click` (B = back) |
| menu | L `input/menu/click` | L `input/system/click` [med: Monado accepts it; xrsim has `btn l system`] | L `input/menu/click` |
| trigger_touch | L, R `input/trigger/touch` | `input/trigger/touch` | - |
| thumb_touch | L `x/touch`, `y/touch`, `thumbstick/touch`, `thumbrest/touch`; R `a/touch`, `b/touch`, `thumbstick/touch`, `thumbrest/touch` (all `input/`) | `a/touch`, `b/touch`, `thumbstick/touch`, `trackpad/touch` | - |
| haptic | L, R `output/haptic` | same | same |

- The right Touch's system (Oculus) button belongs to the runtime (WiVRn's recentre); never bound.
- **[high]** The Quest 2 under WiVRn reports `/interaction_profiles/oculus/touch_controller` for both hands (HoF2
  OpenComposite logs, 98 lines through 2026-10-19 22:00), with a transient "No interaction profile detected" while the
  controllers wake (22:00:40). WiVRn maps every Touch variant to `XRT_DEVICE_TOUCH_CONTROLLER` (`wivrn_session.cpp`
  522-527). HoF2's Monado remote driver gives `/interaction_profiles/valve/index_controller` (HoF2 `logs/stdout-20261017-*`).
  Monado 25.1's simulated driver has `SIMULATED_LEFT/RIGHT` controllers (`simulated_controller.c` in the installed
  `monado-service`); their profile **RT** [low: simple].
- Not used: hand tracking, `meta/touch_controller_plus`, eye tracking.

### 1.4 When: creation, attach, the first sync

The layer creates the session lazily at the first `vkQueuePresentKHR` (`xr_frame`: phase `P_DEV` -> `xr_session_setup`),
never inside `vkCreateInstance`/`vkCreateDevice` (the loader's lock). Suggestions must precede the attach; the attach must
follow `xrCreateSession` and precede the first `xrSyncActions`. New order of `xr_session_setup`:

1. `dlsym(RTLD_DEFAULT, "somvr_shared")` and `dlsym(RTLD_DEFAULT, "somvr_input")` (moved up from after the space; lookups
   only). Input is set up if `somvr_input_ok(in)` and `in->want & SOMVR_INW_STATE`, or with `SOMVR_XR_INPUT=log` (no
   preload needed: log only, for the vkcube check R0).
2. `xrStringToPath` (hands, profiles, the 68 bindings of 1.3), `xrCreateActionSet`, 12 `xrCreateAction`, 3
   `xrSuggestInteractionProfileBindings` (result per profile -> `in->xr.profiles` and the log).
3. `xrCreateSession`, the reference space (unchanged).
4. 4 `xrCreateActionSpace` (grip and aim per hand, identity `poseInActionSpace`).
5. `xrAttachSessionActionSets(session, {somvr})`. The layer never makes a second session (any XR error = pass-through
   for the rest of the process), so this happens once per process.
6. The rest as today (blend modes, command pool, `P_SESSION`).

A failure in 2, 4 or 5: `xr: input off: <call> <result>`, destroy what was made, go on without input; stereo is not
affected. XR_FUNCS grows by `xrStringToPath`, `xrPathToString`, `xrCreateActionSet`, `xrDestroyActionSet`,
`xrCreateAction`, `xrSuggestInteractionProfileBindings`, `xrCreateActionSpace`, `xrAttachSessionActionSets`,
`xrSyncActions`, `xrGetActionStateBoolean/Float/Vector2f/Pose`, `xrGetCurrentInteractionProfile`, `xrLocateSpace`,
`xrApplyHapticFeedback`, `xrStopHapticFeedback`: all core 1.0, so the hard `LOAD_XFN` rule stays.

Publishing: in `xr_poll`'s `SOMVR_SA_BEGIN`, before `xr.state = RUNNING`: `in->xr.grip_space/aim_space/locate_space`,
then `in->xr.ready = 1` (release). The first sync is the first `xr_wait_publish` after `xrBeginSession`; the session is
not FOCUSED yet (`XR_SESSION_NOT_FOCUSED`, inactive states, said in the snapshot's flags). The profile is known only
after a focused sync: `XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED` -> `xrGetCurrentInteractionProfile` (L, R) ->
`xrPathToString` -> `st.profile[]` and a log line.

*built:* this order, checked call by call by the mock runtime. Additions: no profile's suggestions taken = input off;
at `SOMVR_SA_BEGIN` the haptic requests posted so far are dropped (the current numbers become the baseline: a stale
rumble never fires at the session's start); the spaces are published only with want SPACES and an active
`somvr_shared` (its guard protects them). Per sync only the grip pose's state is read, not the aim pose's (its flags
come from the locate): 19 state reads per focused sync (left 10, right 9), not 22. A part of a pose the runtime marks
invalid keeps its last value (the flags say so); invalid velocities are 0. A failed `xrSyncActions` publishes a
cleared snapshot (as not focused) without bumping `sync_seq`.

### 1.5 Per frame: the sync, the snapshot, and the hands' time vs the eyes'

```
present thread (layer)                                  game thread (somvr)
Present(N-1) -> xrWaitFrame -> T(N) published (tseq)
  xrSyncActions; xrGetActionState* (11 actions x 2 hands)
  xrLocateSpace(grip, aim x L/R; X.space; T(N); +XrSpaceVelocity)
  snapshot {sync_seq, time = T(N), flags, buttons, axes, poses}
                                                         input tick of frame M (post CWBInput::vf_04):
                                                           T' = pick(T, period, waits)   (the H4 rule, moved here)
                                                           read the snapshot (seqlock)
                                                           xrLocateSpace(grip L/R; X.space; T'; +velocity)
                                                           controls -> overrides, edges, turn, haptic requests
                                                         player update (0x9ebcae), phases 2-7 (G11 IK, attachments)
                                                         H4: xrLocateViews(T')   <- the same T'
Present(M): xr_frame -> new haptic requests -> xrApplyHapticFeedback; projection layer of record M
```

- **Sync after `xrWaitFrame`**: once per frame at the frame's start, the OpenXR pattern [high]. A button reaches the game
  at its next tick: up to one frame later than a sync on the game thread would give, on top of the stock pad path's own
  frame (the 16 ms gamepad thread). Accepted: buttons are discrete; the poses, which carry the combat, are located on the
  game thread with the freshest samples.
- **One `T'` per game frame.** `somvr_views_locate` picks the time itself today (fresh `T` if `waits` moved, else the last
  time + period). If the input tick and H4 both picked, the second pick would extrapolate once more (`T' + P`) and the
  hands would be a period behind the eyes. So the pick moves into a `somvr_frame_time()` at the input tick; H4 uses the
  tick's `T'` when a tick ran since the last H4 (else it picks as now). The record's `display_time` keeps its meaning.
  RT: one input tick per H4 (run 3: INPUT once per frame, like PMROOT; the order inside the frame is I5).
  *built:* without moving the pick out of `somvr_views.c`: the grips are located in H4 after the body, at the frame's
  own `g_fr.views.time` (the time the eyes were located for), so the eyes and the hands share T' exactly; the input tick
  (I0) only reads the snapshot and uses the last stereo frame's headings for the stick (at most 2 frames old, else no
  rotation).
- **The snapshot's poses** (at `T(N)`) are the fallback when the game-thread locate fails, and the source with
  `SOMVR_HANDS=layer`: extrapolate to `T'` with the velocities (`p += v dt`, `q = exp(ω dt / 2) ⊗ q`, `dt = T' - time`,
  at most two periods, else invalid). WiVRn supplies both velocities (`pose_list.cpp` 61-85; from the headset's samples)
  [high].
- **Aim on the game thread** is not located: `aim = grip ⊗ (grip⁻¹ ⊗ aim)` with the rigid offset taken from the snapshot.

### 1.6 Haptics

`xrApplyHapticFeedback(session, {haptic, L or R}, XrHapticVibration{duration, frequency, amplitude})`; amplitude 0 =
`xrStopHapticFeedback`. A new apply replaces the running one (spec), so the layer applies each request once: per hand,
a changed request number `n` = one call. WiVRn turns each apply into one stream packet `{id, duration, frequency,
amplitude}` to the headset (`wivrn_controller.cpp` 870-900) [high]. The layer applies at its next present (at most one
frame after the request) [med]. Monado's remote and simulated controllers take and drop outputs [low]; the check is the
layer's `XR_SUCCESS` line. The request policy is somvr's (3.7).

### 1.7 Events, focus and failures

| Condition | Layer | Snapshot and somvr |
|---|---|---|
| not FOCUSED (start, Quest menu, WiVRn dashboard) | syncs anyway (`XR_SESSION_NOT_FOCUSED`) | flags lack FOCUSED/SYNCED; somvr releases everything (3.8) |
| profile changes or goes away ("no interaction profile" while the controllers wake) | log; `profile[h] = NONE` | the hand's `active` bits 0: no input, hands frozen |
| a hand loses tracking | `grip_flags` without the TRACKED bits (the runtime's pose stays "valid") | the IK keeps the last target 0.2 s, then hands back to the animation |
| a sync or locate error | count, log once per kind | as not focused |
| session end/lost | core `somvr_xr_stop` -> `ready = 0` -> destroy the 4 action spaces -> session | input lost (3.8) |
| no `somvr_input` (a v2 preload), or `want = 0` | no action call at all | - |

## 2. The shared ABI v3

### 2.1 Versioning: why a second object

`somvr_shared_ok()` in every v2 layer and preload built so far demands `version == 2 && size == 2664`. Growing the
struct to a version 3 would make a v2 layer reject a v3 preload (mono: stereo lost) and the other way round. So **v3 =
`somvr_shared` unchanged (version 2, 2664 bytes, every static assert as is) + a new exported object `somvr_input`** with
its own magic, version and size. `somvr_shared.xr.snap_steps` stays unused: turning belongs to somvr's rig (3.4), the
layer never reads sticks for the game.

| | v2 preload | v3 preload |
|---|---|---|
| **v2 layer** | as today | stereo as today; nobody looks up `somvr_input`; somvr logs `INPUT no controller state from the layer after 5 s (a v2 layer?)` and injects nothing |
| **v3 layer** | `somvr_input` not found: no action calls, as today | controllers |

From v3 on, readers check `version >= 1 && size >= the bytes they read`, writers only append and never re-purpose a field.
So input v2 can append (the per-frame hand ring for the overlay renderer, 4.1) without a second object. The same rule is
proposed for the next core version.

### 2.2 The structs (proposed `somvr_shared.h` additions; offsets checked with Python ctypes, x86-64 SysV)

*built:* in a header of its own, `mod/somvr/somvr_input.h` (it includes `somvr_shared.h`, which stays byte for byte as
it was), with exactly these fields, sizes and asserts plus a few inner-offset asserts and one that pins `somvr_shared` at
v2/2664 bytes. Clarified there: `sync_seq` counts `XR_SUCCESS` and `XR_SESSION_NOT_FOCUSED` syncs; `presses` are never
reset (also not at a new session); who writes which counter (the layer: syncs, unfocused, state reads, haptics applied/
stopped; the game thread: hand locates/failures, haptic posts).

```c
#define SOMVR_INPUT_SYMBOL "somvr_input"
#define SOMVR_INPUT_MAGIC 0x31504e49524d4f53ull /* "SOMRINP1" */
#define SOMVR_INPUT_VERSION 1u
#define SOMVR_INPUT_V1_SIZE 480u

enum { SOMVR_HAND_L = 0, SOMVR_HAND_R = 1 };
enum { SOMVR_PROF_NONE = 0, SOMVR_PROF_TOUCH = 1, SOMVR_PROF_INDEX = 2, SOMVR_PROF_SIMPLE = 3, SOMVR_PROF_OTHER = 4 };
enum {                                   /* somvr_hand_in.buttons: held now */
    SOMVR_BTN_LOWER = 1u << 0,           /* A (right) / X (left) */
    SOMVR_BTN_UPPER = 1u << 1,           /* B / Y */
    SOMVR_BTN_STICK = 1u << 2,           /* thumbstick click */
    SOMVR_BTN_MENU = 1u << 3,            /* left hand only */
    SOMVR_BTN_TRIGGER_TOUCH = 1u << 4,
    SOMVR_BTN_THUMB_TOUCH = 1u << 5,
};
#define SOMVR_NBTN 8                     /* bits 0..7 have press counters */
enum { SOMVR_HACT_POSE = 1u, SOMVR_HACT_INPUT = 2u };                            /* somvr_hand_in.active */
enum { SOMVR_INS_FOCUSED = 1u, SOMVR_INS_SYNCED = 2u, SOMVR_INS_LOCATED = 4u };  /* somvr_input_state.flags */
enum { SOMVR_INW_STATE = 1u, SOMVR_INW_HAPTICS = 2u, SOMVR_INW_SPACES = 4u };    /* somvr_input_shared.want */
enum { SOMVR_HAP_NONE = 0, SOMVR_HAP_COMBAT = 1, SOMVR_HAP_RUMBLE = 2, SOMVR_HAP_UI = 3, SOMVR_HAP_TEST = 4 };

typedef struct {
    somvr_posef grip;          /*   0: XR space = somvr_shared.xr.space (the eye records' space), metres (= XrPosef) */
    somvr_posef aim;           /*  28 */
    float lin_vel[3];          /*  56: grip, m/s, XR space */
    float ang_vel[3];          /*  68: grip, rad/s, XR space */
    float trigger, squeeze;    /*  80, 84: 0..1 */
    float stick[2];            /*  88: x right, y forward, -1..1 as the runtime gives it (no dead zone) */
    uint32_t buttons;          /*  96: SOMVR_BTN_* */
    uint8_t grip_flags;        /* 100: XrSpaceLocationFlags of grip (1 orientation valid, 2 position valid, 4/8 tracked) */
    uint8_t aim_flags;         /* 101 */
    uint8_t vel_flags;         /* 102: XrSpaceVelocityFlags of grip (1 linear, 2 angular) */
    uint8_t active;            /* 103: SOMVR_HACT_*: the pose / the button actions are bound and active */
    uint8_t presses[SOMVR_NBTN]; /* 104: rising edges per button bit since the session began (wraps at 256) */
} somvr_hand_in;               /* 112 */

typedef struct {
    uint64_t sync_seq;         /*   0: successful xrSyncActions so far; 0 = none yet */
    int64_t time;              /*   8: XrTime the poses are located for (the prediction of the xrWaitFrame before) */
    int64_t sync_ns;           /*  16: CLOCK_MONOTONIC at the sync (latency logs) */
    uint32_t flags;            /*  24: SOMVR_INS_* */
    uint16_t profile[2];       /*  28: SOMVR_PROF_* per hand */
    somvr_hand_in hand[2];     /*  32 */
} somvr_input_state;           /* 256 */

typedef struct {
    uint32_t n;                /*   0: request number, +1 per new request (0 = none yet) */
    uint32_t source;           /*   4: SOMVR_HAP_* (logs) */
    float amplitude;           /*   8: 0..1; 0 = stop now */
    float frequency;           /*  12: Hz; 0 = XR_FREQUENCY_UNSPECIFIED */
    int64_t duration_ns;       /*  16: > 0, or -1 = XR_MIN_HAPTIC_DURATION */
} somvr_haptic_req;            /* 24 */

typedef struct somvr_input_shared {
    uint64_t magic;                      /*   0 */
    uint32_t version, size;              /*   8, 12 */
    _Atomic uint32_t active;             /*  16: set last by the preload (release) */
    uint32_t want;                       /*  20: SOMVR_INW_* */
    int32_t pid;                         /*  24 */
    uint32_t pad0;                       /*  28 */
    struct {                             /*  32: layer -> preload, per session, written before ready */
        _Atomic uint32_t ready;          /*  32: 1 = the spaces are valid while somvr_shared.xr.state == RUNNING */
        uint32_t profiles;               /*  36: bit p = the runtime took the suggestions of SOMVR_PROF_p */
        uint64_t grip_space[2];          /*  40: XrSpace */
        uint64_t aim_space[2];           /*  56 */
        void *locate_space;              /*  72: PFN_xrLocateSpace */
        void *locate_spaces;             /*  80: PFN_xrLocateSpacesKHR or NULL (XR_KHR_locate_spaces, optional) */
    } xr;
    _Atomic uint64_t seq;                /*  88: seqlock over st (the layer writes once per sync) */
    somvr_input_state st;                /*  96 */
    struct {
        _Atomic uint64_t seq;            /* 352 / 384: seqlock (the game thread writes) */
        somvr_haptic_req req;
    } hap[2];
    _Atomic uint64_t n_syncs, n_unfocused, n_state_reads, n_hand_locates, n_hand_fail, n_hap_posts, n_hap_applied,
        n_hap_stopped;                   /* 416 */
} somvr_input_shared;                    /* 480 */

_Static_assert(sizeof(somvr_hand_in) == 112 && offsetof(somvr_hand_in, buttons) == 96 &&
                   offsetof(somvr_hand_in, presses) == 104, "somvr_hand_in layout");
_Static_assert(sizeof(somvr_input_state) == 256 && offsetof(somvr_input_state, hand) == 32, "somvr_input_state");
_Static_assert(sizeof(somvr_haptic_req) == 24, "somvr_haptic_req size");
_Static_assert(offsetof(somvr_input_shared, xr) == 32 && offsetof(somvr_input_shared, seq) == 88 &&
                   offsetof(somvr_input_shared, st) == 96 && offsetof(somvr_input_shared, hap) == 352 &&
                   offsetof(somvr_input_shared, n_syncs) == 416, "somvr_input_shared offsets");
_Static_assert(sizeof(somvr_input_shared) == SOMVR_INPUT_V1_SIZE, "somvr_input_shared size");
```

Helpers, inline in the same header: `somvr_input_ok(in)` (magic, `version >= 1`, `size >= SOMVR_INPUT_V1_SIZE`,
`active == 1` acquire); `somvr_input_publish(in, st)` (layer: `somvr_seq_wbegin`, copy, `wend`); `somvr_input_read(in,
out)` (`somvr_seq_read` over `st`); `somvr_haptic_post(in, h, req)` (game thread: `n` = previous + 1 inside `hap[h].seq`);
`somvr_haptic_take(in, h, &last_n, out)` (layer: 1 = a new request). *built:* as listed; `somvr_input_ok` loads `active`
(acquire) before it reads the header, `somvr_haptic_post` skips `n = 0` when it wraps and returns `n`.

### 2.3 Rules

- The preload owns and exports both objects (zero-initialised statics). It fills `somvr_input` (magic, version, size,
  want, pid) and sets `active = 1` when it activates `somvr_shared`; `SOMVR_INPUT=off` leaves it zero, and the layer then
  makes no actions.
- `xr` block: written by the layer before `ready = 1`; the game thread uses it only inside the core guard
  (`somvr_xr_enter` on `somvr_shared`) and with `ready == 1`. Teardown: core `somvr_xr_stop` (no new users, wait for
  none) -> `ready = 0` -> `xrDestroySpace` x 4 -> the session. A game-thread locate never sees a destroyed space (the
  views' argument).
- `st`: the layer is the only writer. A failed read (64 tries) = no new input this tick (the last snapshot stays,
  `sync_seq` unchanged).
- `hap[h]`: the game thread is the only writer, the layer the only reader.
- Poses: XR space and `XrPosef` layout exactly like `somvr_eye_rec.pose`, so the game converts hands and eyes with one
  rig (4.2).

## 3. The game side in somvr

### 3.1 The input tick and the injection path

The production path is the probe's (runtime-probes I1/I3, `somvr_probe.c` `vin_press`/`p_value`/`p_ison`/`w_pad`/`w_cmd`),
made continuous and owned by somvr:

| Site | Kind | Use | Conf. |
|---|---|---|---|
| I0 `CWBInput::vf_04` slot `0x34798c0` (-> `0x9023b0`), post | K_VSLOT | **the input tick**: read the snapshot, pick `T'`, locate the hands, run the controls, set the overrides, fire the edges, post the haptics | high (run 3: once per frame, game thread `WinMain`) |
| I1 `CWBInput::vf_22` slot `0x3479950` (-> `0x903590`) | K_VSLOT | command value: ours for the records we hold | high (run 3 `move`: `polled=1446` in 241 ticks) |
| I2 `CWBInput::vf_23` slot `0x3479958` (-> `0x903990`) | K_VSLOT | the second value query (camera/aim axes, struggles) | high site, med meaning |
| I3 `CWBInput::vf_25` slot `0x3479968` (-> `0x9011e0`) | K_VSLOT | command is on | high site |
| `FUN_00aab660` / `FUN_00aab5d0` `(PlayerInput*, cmd, input id)` (the third argument as the probe passes it; re-input 1.5 calls it value [med]) | called | ON/OFF events on our edges: UI first, then the player | high static, RT (I1: `cmd` not run yet) |
| `FUN_00aab900(PlayerInput*, cmd)` | called | is the command in the active set (`+0x130`) | high |
| `FUN_00a0eeb0(im, entry, pad)` | called | input mode | high |

- The check data move over unchanged from the probe (`P_INPUT`, `P_VALUE`, `P_VALUE2`, `P_ISON`, `F_CMD_ON`,
  `F_CMD_OFF`, `F_CMD_ACTIVE`, `F_INPUT_MODE`) into `mod/somvr/check_sites.py`.
- **Thread**: the game thread for I0 and every override call (all run-3 `FIRST` lines on `WinMain`) [high];
  `FUN_00aab660` is game-thread only (re-input 3.2). The layer's data comes by seqlock: no lock on the game thread.
- The game's own gate applies to us: `FUN_00aab660`/`FUN_00aab5d0` return at once while `PlayerInput+0x78` (enabled)
  is 0 (first bytes `80 7f 78 00 75 0a c3`) [high]; `FUN_00aab2e0`/`FUN_00aab3a0` branch away on the same byte before
  they reach `CWBInput` (`80 7f 78 00 74 42`) [med: the branch target not read]. Cinematics and loads that disable
  input disable ours.
- Records are re-read each tick (`PlayerInput+0x60`, 0x30-byte entries, `+0x00` = the record) for the ~25 commands in
  use, so command-set and mode changes need nothing special; a player change releases everything (the probe's RESET).
- A hold lasts at least one full tick (the probe's `press_tick + 1` rule). A press counter that moved while the button
  already reads up gives ON at this tick and OFF at the next: a press is never lost between a sync and a tick (a torn
  read, two syncs in one tick).
- **Conflict with the probe**: both want I0-I3. In combined runs somvr owns them; the probe skips `P_INPUT,P_VALUE,
  P_VALUE2,P_ISON` (`SOMVR_PROBE_SKIP`) and needs another tick for its words (proposed: `P_SERVER` when `P_INPUT` is not
  installed). A probe change (5.4).
- Not used: V1 uinput (the coordinator's in-process rule), V2/V3 (SDL or `CSDLController::vf_07`: they need a real
  device), the `PlayerInput.SetSimInput` queue (digital, debug).
- Order against the player update (I5) does not matter as long as one I0 runs per frame: polled values are read in the
  player update, events are dispatched synchronously.
- *built:* I0-I3 as K_VSLOT hooks (`IN_TICK`, `IN_VALUE`, `IN_VALUE2`, `IN_ISON`; I1-I3 plain C, the calls being
  indirect), patched only with `SOMVR_INPUT=1`. The overrides answer only on I0's thread. The records come from one read
  of PlayerInput's entry vector per tick; the binding map (per pad input: the commands and their value-record scale) is
  re-read when a record pointer changes and every 60 ticks. A new PlayerInput drops every hold without an OFF to the old
  one. The deny list's records are held at 0/off in the world and a struggle (not in menus). Events and polls only while
  the snapshot is live. The probe conflict is unchanged: in one boot the second library skips the four slots; the
  probe's build-time collision test needs the four slots in its EXCLUSIVE list (a probe change).

### 3.2 Pad mode

Forced once the local player entry exists (the front end), and again whenever the profile byte `+0x238` reads 0:
`FUN_00a0eeb0(*(shell+0x80ff8), entry, 1)` and `0x46b0d61 = 0` (the probe's `padmode on`). Reasons: (1) the combat input
map is per mode (`PlayerCombat+0x15d` -> `FUN_022765e0`): KBM combat listens to `CombatKBM*`, so injected pad commands
may do nothing in KBM mode [med]; (2) prompts and glyphs in pad style (the Xbox set with no Feral controller present,
re-input 3.4; the Touch's A/B/X/Y sit where an Xbox pad's are); (3) the harness's keys and the mouse no longer flip the
mode back. RT (I4): forced pad mode holds with no pad connected and no "reconnect controller" appears (`FUN_00a0ec30`
runs only on a device removal) [med]. If the game insists on a device: **needs RE**, a fake connected `Win32GamePad`
(the device manager's `+0x1c8` byte) that never touches `XInputGetState`. *built:* forced from I0 at most once a second
while the byte reads 0 or `0x46b0d61` reads 1 (`PADMODE` lines); `SOMVR_PADMODE=0` leaves the mode alone.

**Status (2026-10-20, after run 6; offline):** reason (2) was wrong. Pad mode forced without a pad held (run 6:
`PADMODE on: profile byte 0 -> 1, allow->kbm 1 -> 0`, no reconnect message), but every prompt showed the raw pad ids:
`{2010} Select`, `{2010} Yes {2011} No`, `{2010} Select {2011}/{2004} Exit Menu` (shots 04b, 07, 72, 74). Cause
(re-xr-pipeline.md 9.2, static, high): the two UIs that own the `{id}` -> picture substitutions (CGameUI, CMessageBoxUI)
fill them once when their view is made, and without a Feral controller that pass skips every record whose texture is
one of the 22 pad textures (the Xbox set): nothing to substitute the pad ids with. It is not the pad type. Fix (built,
`SOMVR_INPUT=1`, `SOMVR_PADGLYPHS=0` turns it off): the game's own reconnect path for each new view while no controller
exists (empty the table, `vf_37` for the 22 names, `ui+0x10 = 0`; the game registers every record once the textures
are loaded; then the input mode once more so open menus set their prompts again): `GLYPHS` lines. RT: whether the 22
textures load without a pad (the `loaded L of 22` count). Also seen in run 6, not related to the glyphs and not fixed
here: `INPUT bindings ...: 165 records, 0 pad bindings` (no PlayerInput record carried a pad input id 2000-2019 in pad
mode, unlike the probe's run 3), so the binding layer had no pad map for the menus; the next controller test must look
at that first (`in_map`, re-input.md 1.4).

### 3.3 Locomotion

Left stick -> radial dead zone 0.15 with rescale (`v (r - 0.15) / (0.85 r)`) -> the pre-rotation below -> the virtual
pad's LX/LY (ids 2016/2017) = every active command bound to them, times its binding's scale (the probe's `pad` rule):
`StrafeAxis` 6, `ForwardAxis` 3 and the combat stick `CombatPrimaryAnalogX/Y` 132/133 (run 3: "the two sticks"; which
one RT). This is above the device layer's per-axis dead zones (0.2395), so the game gets our exact value (I3: yes for
1.0; a partial value RT). The digital keys 1/2/4/5 are never pressed.

**Head-relative before G2** (the game camera is not the head yet): the game moves along its own camera heading `hc`
(`FUN_00a3c350`, re-input 4), so the stick is rotated by `Δ = hv - hc`:

```
hv = (ψ - yaw0) + h(m(q_head))     the head's world heading (the rig, re-xr-pipeline 1.3)
hc = h(q_cam)                      the game camera's heading (last H4's game SomView) [med: = FUN_00a498c0(cam+0x484, cam+0x458)]
s' = (x cos Δ + y sin Δ, -x sin Δ + y cos Δ)    (forward (0, 1) -> (sin Δ, cos Δ): + = to the right)
```

With G2 (camera = head) `Δ = 0`. Hand-relative (option): `hv` from the left grip's heading. The magnitude is kept (the
walk/jog blend).

- **Sprint**: left stick click latches `FreeRun` 18 (held: I3 on + the ON event; run 3: the stock FreeRun is pad A) until
  the stick stays under r 0.2 for 0.3 s.
- **Jump, vault, evade**: A = pad A passthrough = `Jump` 16 + `FreeRun` 18 + `CombatPrimary04` 121 while held, exactly
  the stock A.
- **Crouch**: right stick down (y < -0.7, re-armed above -0.3) or the physical crouch -> `Crouch` 15 as an event pulse
  (toggle or hold: RT).
- *built:* as above (the physical crouch not yet); the stick passes the virtual pad's LX/LY to every active command bound
  to them (`MOVE` lines). Hand-relative = `SOMVR_MOVE=hand` (the left grip's world heading).

### 3.4 Turning: through the rig, never the game camera

- The right stick never reaches the game: `PitchAccel`/`YawAccel` 22/23 and `PitchAxis`/`YawAxis` 12/13 are held at 0
  by I1/I2 in the world.
- **Snap**: |x| > 0.7 arms one step, |x| < 0.3 re-arms; `ψ += ±snap` (45 deg default, `SOMVR_SNAP_DEG`) through the
  existing `snap_delta` of `somvr_rig_frame`.
- **Smooth**: `ψ += sign(x) · rate · (|x| - 0.15) / 0.85 · dt`, eased in over 0.1 s; a float `yaw_off` next to
  `snap_steps` in `somvr_rig` (a small rig change).
- **About the head**: for any `Δψ` the recentre point moves so the head keeps its world position:
  `P0' = h - R(Y(-Δψ)) (h - P0)` with `h = m(p_head)`, x and z only (then `R(Y(ψ+Δψ-yaw0))(h - P0') =
  R(Y(ψ-yaw0))(h - P0)`). A new pure function `somvr_rig_turn(rig, dpsi, head)`.
- **Rig mode for controller play**: `SOMVR_YAW=fixed` (ψ changes only by the player's turns; `camera` would let the game
  camera's own motion turn the view, against the house rule) with `SOMVR_BASE=player`; the walking direction then comes
  only from 3.3's `Δ`.
- The right stick's larger axis wins: X turns, Y down crouches.
- *built:* `sc_turn_p0` (somvr_ctl.c) instead of a rig change: the snap steps go through `somvr_rig_frame`'s snap path
  (so the `RIG` line shows them), P0 moves about the head in `eye0_setup` before it, and the smooth turn lowers `yaw0`
  (the rig only ever uses psi - yaw0) - somvr_eyes.c unchanged. The turn waits for the next stereo frame; a stick held
  through a menu must come back to the centre before it turns again. `SOMVR_SNAP_DEG` keeps its default 30 (45 for
  controller play is a launch setting).

### 3.5 Menus

Context, from the state the probe already reads (`ifs` interface state, `menus`, `paused`, `video`; G17 `FUN_00a10f00`,
3 = in game; run-3 values in runtime-probes U5): **world** = `ifs == 3 && menus == 0 && !paused && !video`; anything
else = **menu**.

- **In menus the controllers are an Xbox pad at the binding layer (passthrough)**: A/B/X/Y = `lower`/`upper`; LB/RB =
  the grips (> 0.6); LT/RT = the triggers; both sticks with their clicks; START = the left menu button; BACK = the right
  stick click. A pad input presses every **active** command (`FUN_00aab900`) bound to it, so `MenuEnter`, `MenuEscape`,
  `TabLeft/Right`, `MenuAux*`, `MessageBox*`, `CinematicSkip` work from the game's data, whatever the menu's command set.
  Stick flicks (> 0.7) also give d-pad edges (repeat after 0.4 s, then every 0.12 s) for screens that listen to the
  d-pad only. RT: the menu sets' pad bindings (I2 in pad mode).
- The UI takes every event first (`FUN_00a15330`), so the player does not see menu presses.
- **The laser pointer comes later**: it needs mouse mode in menus (the glyphs flip to KBM style) and the Scaleform mouse
  feed (re-input 7, **needs RE**), plus U6 (pushed SDL mouse events, RT). Until then the menu panel is driven by stick and
  A/B like a console game; no laser is drawn.
- Struggles (`PlayerCombat+0x8d8`/`+0x8e0` != 0, re-m1m2 6.2) use the passthrough too until the shake/shove gestures land:
  the stock prompt's button then works.
- *built:* the pad letters are the printed ones (A/B right, X/Y left); BACK = the right stick click, so there is no R3;
  the d-pad flicks come from the left stick; LT/RT count as pressed above 0.6 (released below 0.4) and then carry their
  analog value; a struggle = the world mapping plus pad B/X/Y (the deny list still applies). The context reads the
  interface manager's two state machines, the game timer's pause byte and `FUN_00a10d30` (videos) as the probe does; a
  struggle comes from the last stereo frame's Player.

### 3.6 Combat: what is pressed, what never is

Assisted physical (design-vr 3): the blade decides contact, the game's own records decide the outcome (G8). The buttons
that remain:

| Touch | Intent | Game command (id) | Path | M |
|---|---|---|---|---|
| A | evade (vault toward an orc / roll); jump or vault while sprinting | pad A passthrough: `CombatPrimary04` 121, `Jump` 16, `FreeRun` 18 | events + hold | M2 |
| left stick click | sprint / free-run latch | `FreeRun` 18 | hold | M2 |
| B | interact, pick up | `Activate` 34 (stock RB; RT that it is the context action) | event | M2 |
| left menu | pause; hold 1 s = VR settings (ours) | `Menu` 14 | event | M2 |
| right stick click | map | `PDA` 95 (stock BACK) | event | M2 |
| right stick down | crouch | `Crouch` 15 | event | M2 |
| Y hold | wraith vision | `VisionModeToggle` 243 (active, unbound in run 3) or the pad-mode combat slot (RT) | event pair | M5 |
| left trigger hold | bow | the stock aim (`CombatSecondary03` 124 = pad LT; RT) | hold | M5 |
| right trigger released at the string | shoot | the stock fire for one tick (`CombatSecondary04` 125 / RT), or `FUN_02332c50` | event | M5 |
| right trigger hold | wraith charge | our own state; `SpecialAttackModifier` 160 only inside a power chord | hold | M5 |
| X with the bow aimed / X hold 1 s mounted | Shadow Strike / dismount | RT / `MNT_GetOff` 182 | event / hold | M5, M7 |
| B mounted | bite, claw | `MNT_Attack_1` 185 | event | M7 |
| shake or shove in a struggle | break free | `CombatStruggle01..10` 161-170 by struggle type (passthrough until then) | events | M4 |
| button counter (D8 b, off by default) | counter in the window | nothing injected: judged in G6 (re-combat 3.6 B) | - | M4 |

**Never injected**; while VR input is on, I1-I3 also hold their records at 0/off against polled reads:

| Command (id) | Stock binding (run 3) | Why |
|---|---|---|
| `CombatPrimary01` 118 | pad X, LMB | stock strike: the blade does it |
| `CombatPrimary02` 119 | pad Y | stock counter: the block bar (G6) |
| `CombatPrimary03` 120 | pad B, E | stock stun: the backhand gesture -> G8 |
| `CombatSecondary01/02` 122/123 | LB / RB | combat slots of unknown meaning (RT `Combat.Debug.Input`); RB's `Crouch` and `Activate` go by command |
| `CombatTertiary01-06` 126-131 | L3, R3, d-pad | unknown slots (RT); the quick menu (X hold) will pick them by command |
| `CombatKBM01-20` 136-155 | keys | KBM combat |
| `SpecialAttack1-4` 156-159 | unbound | |
| `PitchAccel`/`YawAccel` 22/23, `PitchAxis`/`YawAxis` 12/13 | RY/RX, mouse | the HMD and the rig own the view |
| `CenterView` 32 | pad mode only | would fight the rig |
| `Debug*` 189-215, 231-242, 333-344; `PhotoMode_*` 317-332 | | |

*built:* the M2 rows (A, left stick click, B, left menu, right stick click, right stick down) and Y hold ->
`VisionModeToggle` (an event pair; unbound in run 3, so no polled record to hold: RT). Never injected = the table above
plus `DebugMenuActivate3` 244, in every context; the camera axes and `CenterView` in the world and a struggle.

Events from a real keyboard or mouse still reach the game: suppressing them needs the A1 entry hook on `FUN_00ab2af0`
(a relocating trampoline, re-input 3.2). G4 vetoes every player melee start as the net and logs it; VR test runs must
not type combat keys.

### 3.7 Haptics in somvr

A mixer per hand: one-shot envelopes (design-vr 3: own hit 0.2 s 100 %, clank 0.15 s 80 %, parry 0.2 s 100 % both,
execution 0.6 s ramp, stun 0.1 s, denied 2 x 40 ms, hit taken 0.3 s 70 % both) plus the game's rumble. The rumble comes
from post-hooks of the two calls to `FUN_00aaa450(PlayerInput*, ch, v)` (R1 `0xa5af58`, R2 `0xa5af97`, K_CALL,
re-input 6 and 8): ch 0/1 -> both hands at 50 %. Amplitude = the largest active effect x the option (0-150 %, clamped to
1). Posted per tick and hand when it moved by > 0.02, else every 4th tick as a 2-period pulse; 0 = stop. RT: whether the
rumble sum runs with no pad (`FUN_00ad0a50`, the Vibration option; re-input 9.5-9.6).
*built:* the mixer `sc_hap` (4 effects per hand + the rumble share); a flat effect is posted once with its whole
duration (the runtime ends it; no stop), a ramp or the rumble as a 6-period pulse refreshed every 4th tick (2 periods
would leave gaps), a stop only after something continuous. Built effects: the controller blade's hits on the right hand
(heavy 100 % / light 80 % 0.2 s, stab 90 % 0.15 s, a glance 30 % 40 ms) and the trigger word `haptic L|R AMP MS`; the
others (clank, parry, execution, stun, denied, hit taken) wait for their features. `SOMVR_HAPTICS` = the option (0-150 %),
`SOMVR_RUMBLE=0` drops R1/R2 (the hooks `RUM1`/`RUM2`).

### 3.8 Input loss

When the snapshot lacks FOCUSED or SYNCED, a hand's `active` bits drop, `sync_seq` stalls for 4 ticks while the game
ticks, or the core session leaves RUNNING: release every held command (OFF events, overrides gone), stick and turn to 0,
stop the haptics, re-baseline the press counters. After it comes back a button counts only once it has been seen up. The
player never moves without their input.
*built:* `sc_live` + `sc_tick`: a stall needs 4 ticks *and* 50 ms (a 1 kHz menu loop would otherwise drop out between
syncs); a torn read keeps the last snapshot; the sticks re-arm (snap, crouch) like the buttons; a hand whose
`SOMVR_HACT_INPUT` drops gives nothing until its buttons were seen up.

## 4. Hands in the world

### 4.1 What the hand poses drive

| Consumer | From | Where in the frame | M | Conf. |
|---|---|---|---|---|
| logs, the first test (XI3) | grip L/R in the world | H4, with the frame's rig | M2 | high |
| controller ghost, laser (the layer's overlay renderer) | grip/aim in XR space at the record's display time | the layer, per record (input v2: a hand ring keyed by frame id) | M2-M3 | med |
| arm IK (G11) | grip -> the hand targets in Talion's model space | post `CSimpleModelTrackerNode::vf_03` (phase 2, `0x9eae9a`), game thread | M3 | high site (re-animation 3.2), RT visual |
| weapons | follow the IK'd hands: attachments are placed at phase 7 from their parent node (re-animation 4) | phase 7 | M3 | high |
| blades for hit detection | the IK result: `R_Weapon`'s world transform x the sword's blade (base 8 cm behind the grip, tip ~90 cm, RT) | after phase 7 (G1 exit) | M4 | med |
| *built:* the blade now | the right grip itself (no IK yet): `somvr_blade_controller` in H4 after the body, `SOMVR_BLADE_*` offsets, the tip velocity from tracking-space deltas (or `v + w x r`) rotated into the world | H4 | M4 | high (offline) |
| swing speed and tiers | the grip's XR-space velocity (tip: `v + ω x r`), rotated into the world; not a world finite difference, so walking or turning with a still sword never counts as a slash | input tick | M4 | med |
| bow line, Shadow Strike aim | aim, or string hand -> bow hand | G2 frame / `FUN_02332c50` | M5 | med |

### 4.2 Frames and conversions

1. **XR -> mirrored game tracking** (the layer's LOCAL or STAGE space, metres, right-handed, -Z forward -> cm,
   left-handed): `m(p) = 100 (x, y, -z)`, `m(q) = (-qx, -qy, qz, qw)` (re-xr-pipeline 1.3). Velocities: `m(v) = 100
   (vx, vy, -vz)`; the angular velocity is an axial vector: `ω' = det(M) M ω = (-ωx, -ωy, ωz)`, the sign pattern of the
   quaternion's vector part [high].
2. **Grip axes after the mirror**: XR +X and +Y stay; the grip's -Z (through the curled fingers toward the thumb: the
   blade direction of a held sword) becomes the game's local +Z [high from M].
3. **Tracking -> world, with the same rig as the frame's eyes**: `p_w = B + R(Y(ψ - yaw0)) (m(p) - P0)`, `q_w = Y(ψ -
   yaw0) ⊗ m(q)`. One rig state per frame (B, ψ, P0, yaw0, the tick's turn) serves the eyes, the IK and the blades. It is
   computed at H4 today (`eye0_setup`); the IK (phase 2) and the hits need it earlier, so at M3 the rig step moves to
   right after the player update (post the call `0x9ebcae`: a new K_CALL site, context bytes to check) and H4 reuses it.
4. **World -> Talion's model space** (the G11 pose buffer, 0x20 bytes per node): `p_m = R(q_obj)⁻¹ (p_w - p_obj)`,
   `q_m = q_obj⁻¹ ⊗ q_w`, with the character object's transform that the skinning uses (A10: whether physics moves it
   between phase 2 and H1; the probe's `pose` logs `obj_pre`, `obj_post`, `obj_vis`).
5. **Controller -> bones**: the grip is the target of the weapon node: `R_Weapon` 119 (sockets 14-16) for the sword hand,
   `L_Weapon` 97 for the dagger/bow hand [RT A8]: `T(R_Weapon) = grip ⊗ C_h`, `C_h` a fixed per-hand rotation and offset
   that lays the blade on the grip's +Z (RT: one `nodes` dump with the sword drawn, plus a shot). `R_Hand` follows from
   the fixed `R_Hand -> R_Weapon` child offset; the chain `R_Armu -> R_Arml -> R_Hand` (108 -> 110 -> 112, twist nodes
   111/113) is solved for `R_Hand`'s position (re-animation 3.3) and its rotation is set from the target. Left: 86 -> 88
   -> 90, twist 89/91.
6. **The IK spring** (25-50 ms, design-vr 1) runs in tracking space (the anchor's frame), not in the world, so walking
   and turning never drag the hands behind; the wall clamp (a G21 ray from the shoulder to the target) runs in the world.

### 4.3 `IK_LHand` / `IK_RHand` and the RE still needed

Run 3's `nodes` dump has root children 4 `IK_LHand` and 5 `IK_RHand` (and 6/7 `IK_LFoot`/`IK_RFoot`), all exactly at the
root while Talion stands. They look like animation-authored IK targets (helper channels that some animations fill:
ledges, climbing, two-handed moves) for the engine's own two-bone IK (`CAnimationMgr::IK`) or `ProceduralBody::Arm`,
which run as node callbacks inside the animation job on the worker threads (re-animation 3.1) [low: from the names and the
rest values]. **Who writes or reads them is unknown.**

- **needs RE**: the readers: their name hashes (`somhash("IK_LHand")`, `somhash("IK_RHand")`) as immediates or in data;
  the target source of the node-callback types 0x00-0x12 (table `0x46a7e60`), above all `CAnimationMgr::IK`; who reads
  `ProceduralBody.IK.HandGuardDist`.
- **RT**: nodes 4, 5, 90 and 112 in model space per frame over idle, run, a sword swing, a ledge hang, a climb and the
  aimed bow (a probe `pose`/`nodes` extension).
- Consequence: if an engine IK reads them, it either fights our G11 edit (then that callback goes for the player's arms,
  A7) or offers a second write point (a target written before that IK, only possible from inside the job: alternative C).
  G11 stays the primary (re-animation 8).
- Also open: the sword's blade axis and length in its own model (A8, re-animation 4), the attachment offsets
  (`+0x44..+0x5c` per entry), the palm offset of the empty left hand.

## 5. Runtime checks and the implementation plan

### 5.1 The first controller test

Where: **R0** first, before any game boot: vkcube + the layer (`SOMVR_XR=force SOMVR_XR_INPUT=log`) under Monado with the
remote driver (Index profile) and a SoM copy of HoF2's `xrsim.py` (design-vr 11): the whole action path without the game,
Steam or a save. Then **R1**, the game under the same Monado (`SOMVR_STEREO=B SOMVR_BASE=player SOMVR_YAW=fixed
SOMVR_INPUT=on`), with the probe for `state` only (its `VIN` words off, 3.1). Both under the usual slot rules (HoF2's OK,
`status.json`). WiVRn and the user only at M2.

| # | Check | Drive (xrsim) | Pass (log lines) |
|---|---|---|---|
| XI1 | suggestions, attach, profile | start | layer `xr: input: action set somvr, 12 actions; profiles touch=ok index=ok simple=ok`, `xr: input: 4 action spaces, attached`, after focus `xr: input: profile L=/interaction_profiles/valve/index_controller R=...` (WiVRn: `oculus/touch_controller`) |
| XI2 | sync | idle | `xr: input: sync n=N focused=1 result=0` (first 3, then every 600th); `n_syncs` = presents after focus; before focus `n_unfocused > 0` and somvr `INPUT inactive`, no `VIN` lines |
| XI3 | hand poses and time | `rel r 0.3 -0.3 -0.4`, then `move r 0 0 -0.3 300` | somvr `HAND frame=F t=T' us=U L(...) R world=(x,y,z) d_snap=D`: `t` = the same frame's H4 `time`; `U` < 300 us; R sits 30 cm right, 30 cm below and 40 cm ahead of the head in the game frame (within 1 cm); `D` (game locate vs extrapolated snapshot) < 0.5 cm still, about v·Δt while moving |
| XI4 | mirror, handedness | `rot r 30 0 0` (+30 deg about +Y = to the left in OpenXR's right-handed frame; check xrsim's sign first) | the grip's world heading turns 30 deg to the left; moving the hand along XR +X moves it to the game's right |
| XI5 | buttons, edges, axes | `btn r a 150`, `btn r a 120` x 3, `trig r 0.8`, `stick l 0 1 500` | `IN ... R btn=0x1` with `presses[0]` +1 per press; one `VIN press`/`VIN release` pair per press; trigger 0.80; stick (0, 1). (A tap shorter than one sync, 50 ms at Monado's 20 Hz, is invisible to OpenXR itself.) |
| XI6 | locomotion | `turn 0 90 0 0 500`, then `stick l 0 1 3000`; `stick l 0 0.1 1000` | `MOVE stick=(0,1) d=90 -> StrafeAxis 1.00 ForwardAxis 0.00`, `VIN ... polled>0`, Talion runs ~6 m toward the head's heading (`ROOT`/`STATE`); the small deflection moves nothing |
| XI7 | snap turn | `stick r 0.9 0 300`; then held 2 s | one `TURN snap +1`, a `RIG` line with ψ +45 deg, the head's world position moves < 1 mm; no second step while held |
| XI8 | pad mode, menus | start; `btn l system` (= menu), then `btn r b` | `PADMODE on: profile byte 0 -> 1, allow->kbm 1 -> 0`; probe `STATE ... padmode=1` throughout; `paused=1`, then `paused=0`; Xbox glyphs in a shot; no reconnect message |
| XI9 | haptics | trigger word `haptic r 0.5 200` | somvr `HAP R n=1 amp=0.50 dur=200ms src=test`; layer `xr: haptic R n=1 amp 0.50 200 ms -> XR_SUCCESS` |
| XI10 | input loss | xrsim controller `active` off (or end the session) | `VIN release ... why=input-lost` for every held command; Talion stops within one tick |
| XI11 | deny list (with P2's fight) | A presses next to orcs | `CombatPrimary01-03` never in a `VIN press`; G4 vetoes 0 starts of ours |

Probe words alongside: `state on` (pad mode, `ifs`/`menus`/`paused` for the context) and `bindings` once somvr has set pad
mode (I2 in pad mode).

### 5.2 Steps: the layer first, then the ABI, then somvr

| Step | What | Offline test |
|---|---|---|
| **L1** layer logic (*built*) | `somvr_logic.c`: the action and binding tables (data), path -> `SOMVR_PROF_*`, the press-counter merge `somvr_in_merge(prev, raw)`, the haptic decision `somvr_hap_decide(last_n, req)` (*built:* `somvr_hap_decide(req, &amp, &freq, &dur)`; the "new number?" test is `somvr_haptic_take`'s) | `test_logic`: every action has a Touch binding; the Index and simple paths are the spec's; a press and a release within one sync count once; counter wrap; a repeated request number = no call; amplitude 0 = stop |
| **L2** layer session (*built*) | `somvr_vklayer.c`: XR_FUNCS, the 1.4 order in `xr_session_setup`, sync and snapshot in `xr_wait_publish`, the profile and focus events, haptics in `xr_frame`, the teardown order, `SOMVR_XR_INPUT=off\|on\|log` | `test_layer` with `mock_xr` grown an action API: attach before the first sync; one profile rejected, the others attached; unfocused syncs; a profile change; one sync per present; apply/stop; not a single action call without `somvr_input` (a v2 preload) or with `want = 0`; every stereo scenario unchanged |
| **A1** ABI (*built*: `somvr_input.h`, test `tests/test_input.c`) | `somvr_shared.h`: `somvr_input`, helpers, asserts (2.2); the layer publishes into it and takes the haptic requests | `test_shared`: the v2 struct's offsets and size unchanged; the input layout; a 200000-write snapshot race and a haptic race with 0 torn reads; `somvr_input_ok` takes v1 and a longer future size, refuses a wrong magic or a short size; 100 teardown rounds against a game thread locating the hand spaces |
| **S1** somvr input (*built*, re-xr-pipeline.md 6.4) | I0-I3 sites (+ `check_sites.py`), the injector from the probe's `vin_*`, pad mode, a pure `somvr_ctl.c/h` (dead zone, pre-rotation, snap/smooth with re-arm, sprint latch, crouch, the context maps, the deny list, input loss), `somvr_rig_turn` (*built:* `sc_turn_p0`) | `tests/test_ctl.c`: no denied command comes out in any context; `Δ = 90 deg` turns forward into strafe right; the dead zone; one snap per push; one ON/OFF pair per press; input loss releases all; the head stays put across a snap. `selftest`: stand-in CWBInput vtable, records and PlayerInput: our values only for our records, ON/OFF with the right ids, inert with a v2 layer |
| **S2** somvr hands (*built*: `somvr_hands.c`, `tests/test_hands.c`; the views' own time instead of `somvr_frame_time()`) | `somvr_frame_time()` at I0 shared with H4, the game-thread grip locate, the snapshot fallback, world hands at H4, the logs | `tests/test_hands.c` (mock runtime, `test_views` style): the hands' time = the views' time in one frame; guard and teardown races; the extrapolation and the mirrored velocities; `SOMVR_HANDS=layer` |
| **S3** somvr haptics (*built*: `RUM1`/`RUM2`, `sc_hap`, the word) | R1/R2 rumble hooks, the mixer, the trigger word `haptic` | envelopes, max mixing, the option scale, posts only on change or refresh; `selftest` with a stand-in call of `FUN_00aaa450` |
| **R0, R1** | 5.1 | - |

### 5.3 Proposed changes to design-vr.md (not made here)

1. Section 0, "The two binaries": the layer also owns the OpenXR action set, `xrSyncActions` and the haptics; somvr
   locates the grip spaces on the game thread at the frame's display time; the ABI gains `somvr_input` (this doc).
2. Hook register: G3 = I0-I3 (`CWBInput` vf_04 post, vf_22/vf_23/vf_25 slots) + `FUN_00aab660`/`FUN_00aab5d0` (the
   binding-layer virtual pad, proven for held axes in run 3) instead of `CSDLController::vf_07` or SDL interposition
   (they need a device; uinput is ruled out). G20 = the R1/R2 call hooks -> `somvr_input.hap`.
3. Section 2, "Where movement comes from": "the left stick goes to the game unchanged" holds only once G2 lands; before
   that the stick is rotated by `Δ = hv - hc`; controller play uses `SOMVR_YAW=fixed`.
4. Section 2, sprinting: the stock FreeRun is pad A (run 3), not the right trigger; the stock A = `Jump` + `FreeRun` +
   `CombatPrimary04`.
5. Section 3, "Switching off the stock melee": the stock buttons are known (X/Y/B = `CombatPrimary01-03`); pad mode is
   forced (the combat map is per mode); the deny list of 3.6.
6. Section 7, menus: pad-style menus first (passthrough), the laser after U6 and the Scaleform mouse RE.
7. Section 9: B = interact is `Activate` (the stock RB), never pad B (the stock stun); on Index the menu is the left
   system button.
8. Section 11: Monado's remote driver offers `valve/index_controller` (HoF2 logs), WiVRn's Quest 2
   `oculus/touch_controller`; R0 (vkcube + the layer) as the cheap first check.
9. Section 3, hit tiers: speeds from the XR-space velocity, not world differences.

### 5.4 Open items

- **needs RE**: who reads/writes `IK_LHand`/`IK_RHand` and the IK callbacks' targets (4.3); the Scaleform mouse feed (the
  laser); a fake connected pad, only if forced pad mode needs one (3.2).
- **RT**: I1 events (`cmd`); I2 in pad mode and the combat slots (`Combat.Debug.Input`: which `CombatSecondary`/
  `CombatTertiary` are bow, wraith vision, grab, drain); `Activate` = interact; `Crouch` toggle or hold; I4 without a
  device; I5 order and one I0 per H4; the game-thread locate cost on Monado and WiVRn; the simulated controllers'
  profile; `C_h` and the blade length; A10; the rumble sum without a pad.
- **Probe**: a tick fallback when somvr owns `P_INPUT` (3.1); a `nodes`/`pose` extension for nodes 4/5/90/112.
- **Site**: the post-player-update call `0x9ebcae` (the rig step for M3), context bytes for `check_sites.py`.
- Doc fix seen on the way: re-xr-pipeline.md 4.2 said the layer checks `version == 1`; the header checks 2. *Fixed*
  (2026-10-19): the code (`somvr_shared_ok`: `version == SOMVR_SHARED_VERSION` = 2, `size == 2664`) is right, the doc
  now says so.
