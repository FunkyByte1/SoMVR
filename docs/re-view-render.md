# Shadow of Mordor (Feral Linux build): frame loop, camera, render submission, Vulkan - the map for a VR port

Static analysis only (2026-10-18): Ghidra project `som-linux` (read-only queries through `scripts/ghidra_query.sh`),
the TSV indexes in `reference/index/` and the raw query batches of an earlier helper. Nothing was launched.
`SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`. Scratch (batch files `bNN.txt`, Ghidra answers `qNN.txt`, whole
decompiles `f_*.txt`): `$SOM/reference/work-view/` (git-ignored). The player/input/combat side is in
`docs/re-gameplay.md`; the strings overview in `docs/re-strings-map.md`.

Conventions (same as re-gameplay.md)
- Addresses are som-linux virtual addresses (non-PIE ELF, base 0x400000 = the address in the running game).
- `FUN_xxxxxxxx` = Ghidra's default name, `Class::vf_NN` = slot NN of Class's primary vtable (a slot number, not a
  meaning). `+0x2d0` = byte offset into an object. `vtbl+0x128` = the slot at byte 0x128 (= slot 37).
- Confidence: **[high]** read in the decompile/disassembly, unambiguous; **[med]** read in the code, meaning inferred
  from strings, call pattern or engine conventions; **[low]** inference, check at runtime first.
- Recurring objects: `DAT_0466afa8` = the client engine interface (ILTClient-like; vtbl+0xd8 returns the renderer,
  checked in `CRendererHandle::vf_107`), `DAT_0466afa0` = the object system (`CObjectSystemInterface`; vtbl+0xb0 =
  set an object's transform), `DAT_046a9560` = `CGameClientShell*` (re-gameplay.md). "The renderer" below is
  `CRendererHandle` (vtable `CRendererHandle::vtable`, ctor `FUN_007062a0`), the game-side `IRenderer`.
- Units: centimetres, +Y up, transforms `{pos[3], quat[4] x,y,z,w}` (28 bytes), angles in radians unless noted.

## 1. Frame loop and threads

**One game thread does update and render submission; a render thread executes the submitted commands one frame
behind; Vulkan submission/present can sit on further threads.** There is no job-graph style frame; animation is the
only big job system found.

### The main loop (game thread "PrimaryEngine")

| Step | Where | Conf. |
|---|---|---|
| Entry (WinMain equivalent on Feral's Win32 layer): names the thread `PrimaryEngine`, creates the window "Shadow of Mordor", reads `projectconfig`/`renderconfig`/`userconfig`/`autoexec.cfg`, then `while(true) { pump; timer FUN_02069e50(); status = FUN_008b50d0(DAT_0466aee0); FUN_008ff140(); ... }` | `FUN_008fe0b0` (3.4 KB) | high |
| Engine frame: video-mode changes (`"Failed to initialize video mode!"`), frame delta from `FUN_02069e30()` (ms), sound mute state, tool events (`FUN_008b21c0`), then the client frame | `FUN_008b50d0` | high (code), med (names) |
| Client frame: `clientShell->vf_03()` (pre-update), world/object simulation `FUN_008c46d0(engine+0x3e0, timer)` unless the object system vf_06 says no, `FUN_008cde8a`, then `clientShell->vf_02()`. `engine+0xba0` = the `IClientShell*` (CGameClientShell's base is `IClientShell`; slots 2/3 match) | `FUN_008b4120` | med |
| Client update + render submission + present | `CGameClientShell::vf_02` (`0x9eabd0`, 4.3 KB) | high |
| Alternative frames: blank frame `CGameClientShell::vf_05` (`0x9e0a90`: BeginFrame, sync, EndFrame, Present(0)); loading screen `FUN_009e3340` (UI into RT `clientShell+0x80dc0`, then Present); not-yet-ready state (`clientShell+0x825b8 != 5`) runs the async init `FUN_009ea7a0` instead | | high |

Order inside `CGameClientShell::vf_02` (decompile, top to bottom) **[high for the order, med for the names]**:
1. Input/profile/services updates; `FUN_00c6c990(clientShell+0xb0, t)` updates the **cinematic camera manager**.
2. Physics world (`DAT_0466afa8` vtbl+0x18 object): `n = vtbl+0x188(dt)` fixed sub-steps, then `vtbl+0x190/0x198`
   n times, `vtbl+0x1a0` (fixed-step simulation loop).
3. `renderer->vf_29()` at `0x9eb0f2` = **BeginFrame** (`CRendererHandle::vf_29` `0x703300`: frame counter `+0x38`,
   a per-frame call on the visibility object `FUN_007b7ad0(&DAT_0462d700)`, queues `FUN_007c9bb0` -> device `FUN_0072a850`).
4. Between `renderer->vf_26`/`vf_27` (command-batch brackets, nesting counter `DAT_0461eb08`): the frame camera is
   fetched once from the game mode (`clientShell+0x81018` vtbl+0x58, a `{xform, fovX, fovY, near, far}` record) and
   handed to the client-FX managers `clientShell+0x80c78/0x80c80/0x80c88/0x80c90` (`FUN_00d8ad50`) and to
   `FUN_00d973e0(clientShell+0xc0)` (4 KB; atmosphere/weather side, not read in detail).
5. If the frame began and the interface state is in-game (`FUN_00a10f00` = state 3): `gameMode->vf_02()` =
   `CGameModeClientHydra::vf_02` (`0x9ef900`), **the world render** (section 3); otherwise sync + menus/UI only.
   The HUD/UI pass follows (`FUN_00a11dc0`, section 6).
6. `renderer->vf_30()` (`0xf0`, end of frame commands), `RenderCinematic` capture (`%s%04d_depth.exr` + screenshot
   through renderer vtbl+0x180/0x1a0), then **Present** `renderer->vf_31(FUN_009f2020())` (`0x7033c0`; the bool is
   the vsync choice).
7. Last: for every local player, copy the camera object's transform and `cam+0x3fc/0x400` (FOV) into
   `cam+0x3c0..0x3e0` and bump the frame counter `cam+0x3e4` (the "previous camera" for motion blur, section 4).

### The render thread ("RenderThread") and the command buffer

- Created by the renderer init `FUN_007a2ff0` (thread fn `FUN_007a2ea0`, name `"RenderThread"`, via the thread
  helper `FUN_01b4e370`). If `DAT_0462c9a1` is set the renderer runs single-threaded: `DAT_0461eaf0 = 1` and every
  command executes at once through `FUN_00702f10` **[high]**.
- Every `CRendererHandle` method (117 slots, list in `work-view/rh_slots.txt`) allocates a command with
  `FUN_00703000(size)`, stores `{tag fn, 0, &queue, executor fn, 0, args...}`, and publishes it
  (`DAT_0461ec10 = DAT_0461eb58`). The thread loop pops commands from the ring (`DAT_0461ebc0` read pointer,
  wrap at `DAT_0461eb68`) and calls the executor (`FUN_007c9xxx` -> the real device code `FUN_0072xxxx`) **[high]**.
  Executors used below: BeginPerspectiveViewport (`vf_37`) `FUN_007c9e00 -> FUN_0072c400`, `vf_39` (same layout
  but a float pair instead of the FOVs, probably the orthographic variant **[med]**) `FUN_007c9e70 -> FUN_0072c0f0`,
  Begin2DViewport (`vf_40`, screen-space, view type 2) `FUN_007c9ee0 -> FUN_0072c260`, EndViewport (`vf_41`) `FUN_007c9ef0`,
  BeginRenderTarget `FUN_007c9d60 -> FUN_0072ab10`, EndRenderTarget `FUN_007c9de0 -> FUN_0072ac00`, RenderScene
  `FUN_007c9f40 -> FUN_007a24d0`, Present `FUN_007c9c90 -> FUN_00742ff0`.
- Synchronisation **[high code, med meaning]**: `vf_23` = SynchronizeRenderThread (waits `FUN_0072cfb0`, stores the
  wait time at `+0x34`, sets `+0x3c = 1`); `vf_24` = sync + `vf_22` + engine vtbl+0x1b8; `vf_22` (`0x703fb0`) =
  full drain (spins until read == write pointer). `vf_31` (Present) clears `+0x3c`, and `vf_33` (BeginRenderTarget)
  asserts "Cannot start rendering again until SynchronizeRenderThread() is called" when it is 0. So the game thread
  records frame N+1 while the render thread executes frame N, and it can never get more than one frame ahead.
- **Where a frame begins and ends**: game thread `renderer->vf_29` (`0x9eb0f2`) to `renderer->vf_31` (end of
  `CGameClientShell::vf_02`); render thread: executor `FUN_007c9bb0` (begin) to `FUN_00742ff0` (present). Below
  that, `FUN_00742ff0` calls `IDXGISwapChain::Present` (`IdxDXGISwapChain::vf_08` `0x1988fd0`) and counts a frame
  index modulo 3 (`+0x22c`), and the Vulkan backend presents in `FUN_01db3890`/`FUN_01db3500` (section 5) **[high]**.

### Other threads found (thread-name strings next to `FUN_01b4e370` calls)

| Thread | Created in | Note |
|---|---|---|
| `EngineAnimationJobThread1..3` | `FUN_00942ec0` | animation job workers (re-gameplay.md section 2) |
| `ResourceLoader`, `ResourceParser`, `ZLib Decompress`, `FileDecompressor(Worker)`, `StorageRequest` | `FUN_008e9a70`, `FUN_0096cfb0`, `FUN_009a4f00`, `FUN_009a69c0/e0`, `FUN_009bda30` | streaming/IO |
| `MovieIOThread` | `FUN_007c79b0` | WebM video (Bink emulation) |
| `Gamepad Update Thread`, `ScaleformInvokeThread`, `OnlineService`, Wwise `AK::*` | `FUN_00904180/450`, `FUN_0090c3f0`, `FUN_009b1fb0`, `FUN_014585e0`... | |
| Vulkan submit thread per queue (`Vk_UseSeparateSubmitThread`), threaded command recording (`Vk_UseThreadedRecording`, `CVulkanThreadedCommandRecorder`) | `FUN_01d94fc0` (device init), queue ctor `FUN_01db8ca0` ("VulkanSubmit") | config-dependent |

Visibility requests: `renderer->vf_107` (`0x702e70`) pushes each view into the lists `DAT_0462d6b0` (once per frame)
and `DAT_0462d6d0` under the lock `DAT_0462d680` (`FUN_007b79f0`) **[high code]**; who consumes them (render thread
or a worker) was not traced **[low]**.

Out of scope, one line: `FUN_00677190` (called from `CFeralGameWindowController::vf_20` and two others) is the
debugger check; not researched.

## 2. Camera to view

### Objects

- `CPlayerCamera* cam = *(Player+0x2d0)` (ctor `FUN_00a59390`, 0x510 bytes; re-gameplay.md). `cam+0x8` = owner
  `Player*`; `cam+0x338` = the **camera engine object** (transform at `+0x8` pos, `+0x14..+0x20` quat); this object's
  transform is what gets rendered **[high]**.
- Cinematic camera manager `clientShell+0xb0` (updated by `FUN_00c6c990` from `CGameClientShell::vf_02`); `mgr+0x20`
  = the active `CClientCinematicCamera` or 0 ("Cinematic rendering works only when a Cinematic Camera is active!" in
  `FUN_00c6c1c0`); its transform comes from `FUN_00fac810`/`FUN_00fac790`, its lens pair from `+0x3c` **[high]**.
- Animation-manager cameras (`IAnimationMgrCameraOwner`): a global intrusive list at `DAT_04c41330`; each entry has an
  "active" byte at `+0x49`, a transform object at `entry[-4]` (`+0x8` pos, `+0x14` quat), vertical FOV in degrees at
  `+0x1c` and an x-scale at `entry[3]`. The ~50 `CAnimationMgr::CameraModifier` subclasses (CameraPitch, CameraYaw,
  CameraCollide, CameraSmoother, CameraLens, `CAnimationMgr::DebugCamera` ...) all use slot 8 as their per-frame
  apply (`CameraPitch::vf_08` `0xcff1f0`, `CameraSmoother::vf_08`, `CameraLens::vf_08` `0xd7aa30`,
  `CAnimationMgr::DebugCamera::vf_08`); they run inside the animation manager's update and write that transform
  **[high for the list read, med for "modifiers write it"]**. CameraSmoother and the camera roll both use the spring
  helper `FUN_00d2bc50`.
- Camera FX (shake): `CCameraShakeFX` is a ClientFX effect (`CBaseFX` + `IClientFXCamera@0xb0`, "ClientFX.CameraShake");
  the client-FX managers `clientShell+0x80c78/0x80c80/0x80c88` keep lists of camera-FX listeners
  (`FUN_00d8b0f0` iterates `mgr+0xd0..`, calling listener vtbl+0x18) **[high]**.

`CPlayerCamera` fields used by the view **[high unless marked]**:

| Offset | Meaning |
|---|---|
| `+0x340..+0x368` | last modifier-stack result (transform, fov pair, flags) written by `FUN_00a5d370` |
| `+0x3a0`, `+0x3b0` | int screen rect of the 3D target and a letterbox inset (aspect = their width/height difference ratio, default 16:9 `DAT_03381090`) |
| `+0x398` | the 3D render target (ref-counted copy of `clientShell+0x80e00`) |
| `+0x3c0..+0x3d8`, `+0x3dc/+0x3e0` | previous frame's camera transform and FOVs (motion blur), `+0x3e4` frame counter, `+0x3e8` counter at last copy |
| `+0x3ec..+0x3f8` | viewport rect in 0..1 (`FUN_00a59960`, `FUN_00a5e0f0`, `FUN_00a5d370`) |
| `+0x3fc`, `+0x400` | **final horizontal / vertical FOV** (radians) |
| `+0x404`, `+0x40c..+0x44b` | `FUN_009f2850` result (8 bytes) and a 4x4 matrix built by `FUN_009f2920` then inverted in place (probably the inverse projection **[med]**) |
| `+0x44c..+0x464`, `+0x478` | final transform copy (`Player.GetPos` reads `+0x44c`) |
| `+0x494..+0x4ac` | transform backup during the render-time roll (`FUN_00a5a3f0` / `FUN_00a5a560`) |
| `+0x4b0..+0x4c8` | last shake offset (pos + quat) |
| `+0x4dc`, `+0x4e0`, `+0x4e4`, `+0x4e8` | camera roll angle, spring state, roll state (0 off, 1 waiting, 2 decaying), timer |

### Per-frame order

1. **Pose** (`Player` client update `FUN_00a35340` -> `FUN_00a5d780(cam)`, skipped while `FUN_009e4770(clientShell)`):
   if a cinematic camera is active, its transform goes to `cam+0x44c`/`+0x478` and to the camera object
   (`DAT_0466afa0` vtbl+0xb0), its lens to `FUN_00a5c6c0`; otherwise `FUN_00a5d370` takes the first active
   animation-manager camera (`FUN_00a5b630`: transform + `FUN_00d2bec0(deg2rad*fovY, xscale, rect, inset)`), handles
   "Cut" transitions, writes the camera object transform and calls `FUN_00a5c6c0` **[high]**.
2. **Lens and projection** `FUN_00a5c6c0(cam, lens[2])` (`0xa5c6c0`) **[high]**:
   - vertical FOV = `Camera.FOVYDegOverride` (int cvar, value `DAT_046b6638`, getter `FUN_00d29360`) x pi/180 when
     > 0, else `lens[1]`; horizontal = `FUN_00d2bec0`: `2*atan(tan(v/2)*aspect) * DAT_046b6640` (constant 1.0), both
     clamped to 0..178 deg (`DAT_035705a0` = 3.1066) and to the minima `DAT_046b2b80/84`;
   - stores `cam+0x3fc/0x400`, then `FUN_009f2850` -> `+0x404`, near plane `FUN_00d28f80()`, matrix -> `+0x40c`.
   - **Near plane** `FUN_00d28f80`: override `DAT_046b65e4` if > 0 (default -1), else a per-level database value,
     fallback **7.0 cm** (`DAT_0352a928`). **Far plane** `FUN_00d29120`: override `DAT_046b65e0` (default -1), else
     database, fallback **100000 cm = 1 km** (`DAT_035058dc`). No cvar names point at these two overrides (plain
     statics written only by `_INIT_356`), so they are memory pokes.
3. **Shake, rumble, roll** `FUN_00a5d250(cam)` (from `FUN_00a35340`) **[high code, med names]**:
   - `FUN_00a5d0a0`: asks the three FX managers (`FUN_00a58e60` -> `FUN_00d8b030`) for a shake offset (pos + quat) and an
     FOV scale pair, then `FUN_00a5cd70` composes the offset onto the camera object transform, multiplies
     `cam+0x3fc/0x400` by the FOV scale and re-runs `FUN_00a5c6c0`. **`FUN_00d8b030` returns identity (zero offset,
     scale 1,1) when the byte `DAT_046b72d2` is set** - a built-in "no camera FX" switch (its writer is not known).
   - `FUN_00a5ad40`: camera FX -> controller rumble (7 channels, `FUN_00aaa450`).
   - roll state machine on `cam+0x4dc` (spring `FUN_00d2bc50`).
4. **Render time** (`FUN_009ee170`, section 3): `FUN_00a5a3f0` backs up the camera object transform to `cam+0x494`
   and, if `|cam+0x4dc| > eps`, applies the roll to the object; `FUN_00a5a820(cam, view, sceneParams)` builds the
   **view record**; after rendering `FUN_00a5a560` writes the backup back. The engine itself already modifies the
   rendered camera only for the duration of the render and restores it afterwards.

The **view record** (`FUN_00a5a820` output, `RSP+0x80` in `FUN_009ee170`; same layout as the game mode's frame camera
and the `FUN_00c6bd80` default) **[high]**:

```c
struct SomView { float pos[3]; float quat[4]; /*0x1c*/ float fovX, fovY; /*0x24*/ float nearZ, farZ; }; // 0x2c bytes
// pos/quat = camera object transform (cam+0x338 +0x8), fov = cam+0x3fc/0x400 (clamped), near/far = FUN_00d28f80/FUN_00d29120
```

If `cam+0x3e4 == cam+0x3e8 + 1`, `FUN_00a5a820` also copies the previous camera (`cam+0x3c0..0x3e3`) into
`sceneParams+0x24c..0x26f` (motion-blur reprojection). In photo mode (`FUN_009fa370(Player+0x310)`) the view comes from
`FUN_00dbf3f0` instead.

### Where the final view matrix and projection are built

- Game thread: `renderer->vf_37` = `CRendererHandle::vf_37` (`0x705500`), **BeginPerspectiveViewport**, called at
  `0x9ef2b5` with `XMM0 = near (view+0x24)`, `XMM1 = far (view+0x28)`, `RSI = &view` (transform), `RDX = &view.fovX`,
  `RCX = rect[4]` (copy of `cam+0x3ec`). It asserts the FOVs (strings "BeginPerspectiveViewport: Horizontal/Vertical
  FOV is at or below zero degrees") and queues the command **[high]**. Signature:
  `bool vf_37(float nearZ, float farZ, CRendererHandle* this, const LTRigidTransform* xf, const float fov[2], const float rect[4])`.
- Render thread: `FUN_0072c400` (executor `FUN_007c9e00`): clamps the FOVs to 179 deg, `Render.Ortho` (`DAT_0461ea58`,
  float cvar at `0x461ea20`) switches to orthographic, `tan(fov/2)`, pixel rect from the target size, then
  `FUN_0072bb20(target, xf, tanHalf[2], centerOffset[2], pixelRect, type)` -> `FUN_0072b050(near, far, viewState
  0x461e870, xf, tanHalf, centerOffset, rect, type)` builds the **view matrix** (inverse of the quat/pos), the
  camera-to-world matrix and the **projection** (type 0 perspective, 1 ortho, 2 2D), and `FUN_0072bb20` uploads them
  to the global constant blocks (`0x4624e60..`, `0x4624ee0..`, `0x4624f60..`, the screen-to-view ray constants at
  `0x4625140..0x462514c`) **[high code, med block names]**.
- **Off-centre projection is already in the math**: `FUN_0072b050`'s `centerOffset` (x, y) shifts the projection
  centre (it enters the third column, and `FUN_0072bb20` adds it to the screen-to-view rays), but `FUN_0072c400` always
  passes `{0, 0}` (`local_30 = 0`). Per-eye asymmetric frusta = make that pair non-zero, plus asymmetric tangents
  (left/right differ): `tanHalf` is one value per axis, so a fully asymmetric OpenXR FOV
  (`angleLeft != -angleRight`) = `tanHalf = (tanR - tanL)/2`, `centerOffset = (tanR + tanL)/2` in tangent units, if the
  offset is in the same units as `tanHalf` (it is subtracted from `tanX` directly in `FUN_0072bb20`) **[med]**.
- Aspect = `(rect width in pixels)/(rect height)` implicit in `fovX/fovY`; nothing else stores an aspect.

### Best place to set position, orientation and projection per eye

1. **Pose: post-hook `FUN_00a5a820`** (`void(CPlayerCamera*, SomView*, SceneParams*)`, one caller `FUN_009ee170`):
   after the original returns, replace `view->pos/quat` with eye pose = camera position (or the player's head) +
   HMD offset, and `view->fovX/fovY` with the eye's FOV. The same record then feeds culling (`renderer->vf_107`), the
   UI projection copy (`gameMode+0x70..0x98`, `FUN_00cbd260(interfaceMgr+0x2708, ...)`) and `vf_37`. The camera object
   itself stays untouched, so gameplay that reads the camera (aim, fight-circle scoring) is unaffected **[high]**.
   Alternative with the engine's own pattern: modify the camera object in `FUN_00a5a3f0` and let `FUN_00a5a560`
   restore it (then everything reading the object during render sees the eye pose).
2. **Projection: hook `FUN_0072c400`** (render thread) or the call `FUN_0072bb20` inside it, to pass a per-eye
   `centerOffset` and tangents. Per-eye state must travel with the command (e.g. key it on the target, or patch the
   command's FOV floats in `vf_37` since the command copies `fov[2]` at `+0x44`) **[med]**.
3. **Near plane**: 7 cm is fine for a head camera; the hand/weapon models will need less: poke `DAT_046b65e4`
   (e.g. 2.0) **[high mechanism]**.
4. **No code at all**: `Camera.FOVYDegOverride` changes the vertical FOV for a test (re-strings-map.md).

### Turning off shake, smoothing, roll

| What | How | Conf. |
|---|---|---|
| Camera shake and FX FOV pulses | set byte `DAT_046b72d2` = 1 (makes `FUN_00d8b030` return identity), or skip `FUN_00a5d0a0` | high (code) |
| Controller rumble from camera FX | skip `FUN_00a5ad40` (or leave it; it is haptics only) | high |
| Camera roll (dutch angle) | keep `cam+0x4dc` = 0 / skip the roll block of `FUN_00a5d250`; `FUN_00a5a3f0` then applies nothing | high |
| Smoothing, collision push-in, pitch/yaw springs | all inside the modifier stack; for VR the HMD replaces orientation after it (step 1 above), so they only move the camera position: take the position from the player's head bone instead (re-gameplay.md `IEngineModel` GetNodeTransform) | med |
| Per-attack camera overrides, slow-mo | `Combat.EnableCameraOverride` `*(bool*)0x4c44298`, `Combat.Enable.SlowMo` `*(bool*)0x4c44318` (re-gameplay.md) | high |
| Debug camera | `CAnimationMgr::DebugCamera` modifier (`DebugCamera.SetPosition` command); data-gated (re-gameplay.md) | med |

## 3. Render submission

### The per-view entry point

`CGameModeClientHydra::vf_02` (`0x9ef900`, the game mode at `clientShell+0x81018`) is the world render of a frame
**[high]**:
- cinematic camera active (`camMgr+0x20`): `FUN_009edba0(gameMode, camMgr, &rt3D, 2)` - view from `FUN_00c6c1c0`;
- in the world (`FUN_00ab41c0/FUN_00ab4270` on the player list): `FUN_009ef390(gameMode)`;
- else: clear the 3D target and draw the UI for each player (`FUN_00a11dc0`).

`FUN_009ef390` (`0x9ef390`) **loops over all local players** (`clientShell+0x80..+0xa0`, the split-screen-capable
list; "Error: reading the transform of a camera object - this code path assumes it is valid."): it collects every
player camera's position (for the streaming listener: engine vtbl+0xa0 object, vtbl+0x28(0, 0, positions[], flag))
and pose (clutter/vegetation streaming via `FUN_023a39e0`; the audio listener is `CSoundMgr::vf_23` (corrected 10-19: re-stereo.md 9.3)), then calls **`FUN_009ee170(gameMode, &rt3D, rect, player, 2, first)`
once per player** (`rt3D = clientShell+0x80e00`) **[high]**. In the shipped single-player game the list holds one player **[med]**.

`FUN_009ee170` (`0x9ee170`, 4.6 KB) = **render one player view** **[high order, med names]**:

| # | Call | What |
|---|---|---|
| 1 | `renderer->vf_92` (`0x2e0`) x3 | three global shader parameters from the player (position/velocity records; e.g. foliage interaction) |
| 2 | `FUN_00a5a3f0(cam)`, `FUN_00a5a820(cam, &view, &sceneParams)`, `FUN_02824890(&sceneParams)` | view record + scene parameters (section 2); `FUN_02824890` passes the params to every entry of the `DAT_04c48c70` list (`FUN_02823ef0`; scene modifiers **[low]**) |
| 3 | copy view to `gameMode+0x70..0x98`; `FUN_00cbd260(interfaceMgr+0x2708, ...)` | the "last view" record and the interface's copy (HUD world projection **[low]**) |
| 4 | `renderer->vf_107(&view, flag)` (`0x358`) | **visibility request** for this view (`FUN_007b79f0` queues it) |
| 5 | if `first`: `renderer->vf_24` (sync) + interface pre-render (`FUN_00a12420`, `vf_56`, `FUN_00e93c20`) | once per frame only |
| 6 | `renderer->vf_33(&rt3D, clear, 0, rect, 0)` | **BeginRenderTarget** |
| 7 | `renderer->vf_37(near, far, &view, &view.fov, cam+0x3ec rect)` (`0x9ef2b5`) | **BeginPerspectiveViewport** |
| 8 | `FUN_009f23c0` then `FUN_00a59a60(cam, &sceneParams)` -> `renderer->vf_43(&sceneParams, 0)` (`0x158`) | per-view budgets on `DAT_046a9238` (one of the render interfaces registered in `_INIT_269`, probably the texture manager **[low]**; only if the view record's flag `+0x324`) and **RenderScene** |
| 9 | `FUN_00ca52d0/FUN_00ca53e0(Player+0x438, rt)` | rain atmosphere state -> lens rain-drop screen effect (`FUN_00adeee0`) |
| 10 | `FUN_009f1d20(&view, screenSize)`, `renderer->vf_41` | world-anchored HUD items projected with this view (`DAT_046b8580` list, `FUN_00e7a490`); EndViewport |
| 11 | `FUN_009f2410`, `FUN_009f2460` | budgets back; **overlay scenes** `FUN_00d8e180` (sorted list of extra scenes drawn with the same camera: each = `vf_37` + `vf_45` + `vf_41`); optional 2D viewport pass `FUN_009f1be0` |
| 12 | `renderer->vf_34(type)` | **EndRenderTarget** |
| 13 | `FUN_00a11dc0(interfaceMgr, player, 1, 1)` | HUD/UI for this player (section 6) |
| 14 | `FUN_00a5a560(cam)` | restore the camera object (undo the render-time roll) |

On the render thread a RenderScene is `FUN_007a24d0` (refused unless the device is in-frame, `DAT_0461e840 == 4`,
and the current viewport is not 2D): `FUN_00810e80` builds the frame context from the current view state
(`0x461e870`) and the scene params, then `FUN_007d7840(&DAT_04625840, ctx)` runs **the whole deferred pipeline**
(lists `FUN_007e91a0`, passes `FUN_007d6e20`: G-buffer, lighting, shadows, transparency, post) for that viewport
**[high for the calls, med for the pass names]**. So every view costs a full pipeline including post-processing.

### Views the engine already renders in one frame

| View | Where | Conf. |
|---|---|---|
| One per local player (split-screen heritage), same target, rect per player (`cam+0x3ec`) | `FUN_009ef390` -> `FUN_009ee170` | high |
| Cinematic camera view (instead of the player) | `FUN_009edba0` | high |
| 3D UI scenes (`UISceneCamera`: menus, Nemesis screens) inside the interface pass | `FUN_00c1e370` -> `FUN_009f2550` (vf_107 + vf_37 + vf_43 + vf_41) | high |
| Interface render-to-texture (own target, clear colour 0xff00ff, aspect from its rect; gated by `DAT_046b4030`) | `FUN_00ad7890` (from `FUN_00a12420`) -> `FUN_009f2550` | high code, low purpose |
| Overlay scenes with the main camera | `FUN_00d8e180` (from `FUN_009f2460`) | med |
| Photo-mode/secondary capture view into `clientShell+0x80db8` | `gameMode->vf_03` from `CGameClientShell::vf_02` (`FUN_009f2670`: BeginRT, `FUN_009f2550`, `FUN_009f2460`, HUD items, EndRT) | med |
| Shadow maps | not separate game-side views: cascades are fitted inside RenderScene (`COutdoorShadowAtmosphereSystemState`: `StaticCascadeDims/Enabled/DepthBias`, `k_vShadowCascadeOffsetX/Z`, sky shadow map `tSkyShadowMap`) | med |
| Sky camera | `SkyCamera` is a level object (`GameBase`; `SkyCamera::vf_10` only registers it with the object system, vtbl+0x70); no separate sky pass exists on the client render path, so the sky is drawn inside RenderScene | low |

`FUN_009f2550(renderer, sceneView)` (`0x9f2550`) is the generic "render this scene view" helper the UI and cinematic
paths use: `vf_107(view, view+0x325)`, `vf_37(view+0x40 near, view+0x44 far, view, view+0x1c fov, view+0x28 rect)`,
`vf_43(view+0x50 params, 0)`, `vf_41`. Its `sceneView` is a 0x328-byte record (transform, FOV pair, rect `{0,0,1,1}`,
near/far, params, layer list at `+0x2c0` (max 10, count `+0x310`)) **[high]**.

### Rendering the world twice per frame with two cameras

Feasible in principle: the renderer is a command stream with no "one view per frame" assumption on the game side,
`FUN_009ef390` already renders N player views per frame with one BeginFrame/Present, and nested views are
sequential state (one current view at `0x461e870`). What it takes **[med]**:
1. A second call of the view body for the other eye: either call `FUN_009ee170` twice (hook `FUN_009ef390` or
   `CGameModeClientHydra::vf_02`), with the eye selected by a global our `FUN_00a5a820` post-hook reads, or
   replay steps 6-12 with a second view record. Pass `first = 0` for the second eye (as the player loop does) so the
   once-per-frame sync and interface pre-render are not repeated.
2. One 3D target per eye. `rt3D` (`clientShell+0x80e00`) is created by `FUN_009e6d20`/`FUN_009ea410` through
   `renderer->vf_62` (`0x1f0`, CreateRenderTarget(w, h, flags 0x37d7)) with the size from `clientShell+0x80df0..0x80dfc`;
   create a second one the same way, or render both eyes into one double-width target with rects `{0,0,.5,1}` /
   `{.5,0,1,1}` (the rect path exists for split screen, but whether every post effect honours a sub-rect is
   untested **[low]**).
3. Per-eye projection (section 2: centre offset in `FUN_0072c400`).
4. The HUD only once (skip step 13 for one eye, or move it to its own target - section 6).
5. Cost: two full deferred pipelines; the game thread work (simulation, animation) is not doubled.

What is in the way **[med/low, verify at runtime]**:
- **Temporal state** that assumes one view per frame: motion blur's previous camera (copied once per frame,
  section 4), eye adaptation, GPU occlusion queries (`occlusionquery.mat`, `CVulkanOcclusionQuery`,
  `CRenderTargetVisQuery`: results are read back a frame later; if they are keyed per object rather than per view,
  two different views per frame will make them disagree -> popping). Turn the temporal effects off first.
- **Visibility**: `vf_107` -> `FUN_007b79f0` pushes the first view of a frame into a "once per frame" list
  (`DAT_0462d6b0`, flag `+0x14`) and every view into `DAT_0462d6d0`; the consumer was not traced. The second eye
  gets its own culling but anything the engine does once per frame (LOD selection, streaming priorities) follows the
  first view **[low]**. Cheaper option: cull once with a widened frustum covering both eyes (submit one combined view
  via `vf_107` and render both eyes from it) - needs the consumer understood.
- **Screen-space gameplay**: the fight circle scores on-screen orcs from the camera (re-gameplay.md section 6); keep
  the camera object = the centre/head pose so that keeps working.
- **Lens effects**: rain drops on the lens (`FUN_00ca53e0`), vignette (`k_fVignetteParams`), lens flares
  (`CLensFlareFX`, `ClientFX.LensFlare`) are screen-space and should go for VR.

### How culling, LOD and streaming depend on the camera

| System | Camera input | Conf. |
|---|---|---|
| Frustum/visibility culling | the view record handed to `renderer->vf_107` (pos, quat, FOVs, near/far) | med |
| GPU occlusion culling | queries rendered with the view, results used next frame | low |
| Texture streaming budget (probably) | `FUN_009f23c0`/`FUN_009f2410`: per-view values `DAT_046a9ec4<<10`, `DAT_046a9ec0<<10` on `DAT_046a9238` (vtbl+0x18/0x20, restored with +0x28/+0x30) when the view flag `+0x324` is set | low |
| World streaming / clutter (`FUN_023a39e0`; the audio listener is `CSoundMgr::vf_23` (corrected 10-19: re-stereo.md 9.3)) | every local player's camera position/pose (`FUN_009ef390`) | med |
| Character LOD | `CharacterProxy.SetLODBias`, `ApplyLODBias Applied(%s)` (`FUN_00d2f2a0`); distance to the view **[low]** | low |
| Cascaded shadows | fitted to the current view inside RenderScene | med |

## 4. Post-processing state carried between frames

There is **no TAA** in this build: no temporal/TAA/reprojection strings, AA is FXAA tied to the motion-blur setting
**[high for "no strings", med for "no TAA"]**. The temporal state found:

| Effect | State between frames | What breaks with two views per frame | Switch | Conf. |
|---|---|---|---|---|
| Motion blur, camera part | previous camera per player `cam+0x3c0..0x3e3` (transform, FOVs), copied once per frame at the end of `CGameClientShell::vf_02`; `FUN_00a5a820` passes it as `sceneParams+0x24c..0x26f` when the frame counter `cam+0x3e4 == cam+0x3e8 + 1` | both eyes reproject against last frame's single camera -> the eye offset itself reads as motion (smear on every head move) | `Render.Setting.MotionBlurQuality` (TRenderSetting at `0x461e540`, 0..3: 0 = off, 1 = FXAA only, 2 = FXAA + camera blur, 3 = FXAA + camera + object blur); or byte `DAT_046b6651` = 0 (getter `FUN_00d292d0`, default 1): `FUN_00a5a5a0` then zeroes the player motion-blur block `sceneParams+0x58` (from `PlayerMotionBlurSettings`, `Player+0x2f8`) | high (data flow), med (meaning of `+0x58`) |
| Motion blur, object part | velocity buffer from current vs previous object transforms (`objmotion_prepdepthvelocity.mat`, `motion_objectvelocity.mat`), tile max / neighbour max (`FUN_007c4d10`: `tMaxVelocityBuffer`, `tNeighborVelocityBuffer`, `k_vMotionBlurParams`) | per-object previous transforms are per frame, not per view: fine for both eyes, but camera velocity is folded in -> same smear | MotionBlurQuality <= 1 | med |
| Eye adaptation / tonemapping | `DynamicExposureBuffer` (GPU), `ComputeDynamicExposure.mat`, `k_vHDRRangeAndExposure`, `k_fExposure/k_fMinimumExposure/k_fMaximumExposure`; per-weather `CHDRAtmosphereSystemState` fields `ToneMappingEnabled`, `DynamicExposureEnabled`, `ExposureAdjustSpeed` | if each RenderScene updates the adapted exposure, two views per frame adapt twice as fast and the eyes can differ by one step (visible brightness mismatch) | `DynamicExposureEnabled` / `ExposureAdjustSpeed` are game-database values of the HDR atmosphere states (`CHDRAtmosphereSystemState::vf_04` reads them): patch with `DBDiff.Apply`, or let only the first eye run the exposure pass | low (needs the pass traced at runtime) |
| Sun rays ("radial blur") | `RadialBlurExposure` in the HDR atmosphere state; screen-space from the sun position | per view, so consistent, but screen-space god rays differ between eyes near the edges | database value -> 0 | low |
| Depth of field | `PlayerDepthOfFieldSettings` (`Player+0x300` -> `sceneParams+0x114`), focus smoothing (`Camera.DOF.Reset` command, `_INIT_281`), cinematic `DepthOfFieldEnabled` | blur in VR is wrong anyway; per-eye focus could differ | `Render.Setting.DepthofFieldToggle` (TRenderSetting at `0x461e6c0`, 0/1); or byte `DAT_046b6652` = 0 (getter `FUN_00d292c0`): `sceneParams+0x114` = 0 | high (data flow) |
| Lens flares | `CLensFlareFX`, `OcclusionQueryFX` (occlusion queries read back later) | queries from two views alternate | ClientFX data; no switch found | low |
| SSAO, bloom, colour grading, vignette | none temporal found (`SSAO_blur*`, `tBloomResult0..9`, `tColorGrade3DLUT1/2` blended over game time, `k_fVignetteParams`) | vignette and rain drops are screen-space: remove for VR | `Render.Setting.AmbientOcclusionQuality` 0..5; vignette/rain: skip `FUN_007eb1d0`/`FUN_00ca53e0` **[low]** | med |

The `Render.Setting.*` cvars (`_INIT_52`, help texts in re-strings-map.md) are config cvars, so they can be set in the
test prefs (`render.cfg` / `ExtraCommandLine`) with no code.

## 5. The Feral3D/Vulkan layer and IndirectX (IdxD3D11)

### How D3D11-style calls become Vulkan

```
game renderer (FUN_0072xxxx, FUN_007xxxxx on the render thread)
  -> IndirectX: IdxD3D11Device (vtable 0x4552938, 71 slots), IdxD3D11DeviceContext (0x45531e8, 160 slots;
     IdxD3D11DeviceContextThreaded 0x45537f0), IdxDXGISwapChain (0x4503008), shaders translated from DXBC
     (IdxDXBCShaderTranslatorGLSL -> glslang -> SPIR-V)
  -> Feral3D: Feral3DDevice (vtable 0x4567708, 131 slots), Feral3DTexture/Buffer/Shader/Query, pipeline warmer
  -> backend CVulkanDevice (derives from Feral3DDevice, vtable 0x4569540), CVulkanQueue, CVulkanSwapchain,
     CVulkanTexture (Feral3DTexture + CVulkanResourceStorage@0x90), CVulkanCommandBuffer, render-pass/framebuffer caches
```

- D3D11 device creation (`D3D11CreateDevice` equivalent) `FUN_0199cb80`: reads the Feral settings (`FUN_019bc320`
  maps the `Vulkan*` prefs to `Vk_*` config keys; `Core_RequireHardware`, `Core_SwapChainBufferCount`), creates the
  Feral3D device through the factory `FUN_01d06d50` ("Could not create Feral3D device"; the GL path exists too)
  **[high]**.
- `IdxD3D11DeviceContext::vf_12/13/14` (`0x1ab7420`, `0x1ab73d0`, `0x1ab6c70`) sit where D3D11 has
  DrawIndexed/Draw/Map **[low: slot order assumed from the D3D11 vtable]**.

### Instance and device creation

| What | Where | Detail | Conf. |
|---|---|---|---|
| Loader | `FUN_01e471d0` | `dlopen("libvulkan.so.1")` + `dlsym("vkGetInstanceProcAddr")` -> `DAT_04b14650`; libvulkan is **not** NEEDED | high |
| `CVulkanInstance` (0x40 B) singleton | `FUN_01da38d0` -> ctor `FUN_01d9e210` | `vkEnumerateInstanceVersion` (`DAT_04b14648`): apiVersion 1.0 (`0x400000`), or 1.1 (`0x401000`) when the loader is > 1.0; engine name `"Feral3D"`; **no layers enabled** (`enabledLayerCount = 0`); extensions from `FUN_01d9de20`: required `VK_KHR_surface` + the WSI one from `FUN_01e477c0` (`VK_KHR_xcb_surface` or `VK_KHR_wayland_surface`, picked from the SDL window), optional `VK_KHR_get_physical_device_properties2`, `VK_KHR_external_memory_capabilities` (table at `0x45fd420`) | high |
| `vkCreateInstance` call | `0x1d9e5c5` (`CALL [0x4b14630]`) | `VkInstanceCreateInfo` on the stack at `RSP+0xd0`, result -> `instance+0x8`; then every instance-level function is loaded with `vkGetInstanceProcAddr` into globals `0x4b14xxx` | high |
| `CVulkanDevice` (0x5230 B) | `FUN_01d964f0` -> ctor `FUN_01d94fc0` | queue families from `Core_QueueRequirements` (graphics, optional separate transfer `Vk_UseSeparateTransferQueue`, a third family), `vkGetDeviceQueue` (`DAT_04b14460`) -> `CVulkanQueue` objects (`FUN_01db8ca0`, 0x58 B, "VulkanSubmit"); settings `Vk_DisableSwapChain` (byte `device+0x5228`), `Vk_UseSeparateSubmitThread`, `Vk_UseThreadedRecording` | high |
| Device extensions | `FUN_01d93c80` | required `VK_KHR_swapchain` ("Required Vulkan device extension %s is not available"), optional `VK_EXT_debug_marker`, `VK_KHR_dedicated_allocation`, `VK_KHR_shader_draw_parameters`, `VK_KHR_maintenance1`, `VK_KHR_maintenance2`, `VK_KHR_get_memory_requirements2`; `VK_KHR_driver_properties` queried in `FUN_01e45b00` | high |
| `vkCreateDevice` call | `0x1d95c49` (`CALL [0x4b14480]`) | `VkDeviceCreateInfo` built at `local_88` (enabled layers from one vector, extensions from another, features `device+0x2f18`); `VkPhysicalDevice` at `device+0x2bb8`, `VkDevice` -> `device+0x2bc0` | high |

**Missing for OpenXR**: Monado/WiVRn's `xrGetVulkanInstanceExtensionsKHR` / `xrGetVulkanDeviceExtensionsKHR` ask for the
external-memory/semaphore/fence family (`VK_KHR_external_memory_capabilities` is the only one the game may enable;
no `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore(_fd)`, `VK_KHR_external_fence(_fd)`, timeline semaphores).
Ways to add them, best first:
1. **A standard Vulkan layer** (explicit via `VK_INSTANCE_LAYERS`/`VK_LOADER_LAYERS_ENABLE`, or an implicit-layer JSON
   enabled by an env var): in `vkCreateInstance` append the XR instance extensions (and raise `apiVersion` to 1.1 if the
   runtime wants it); in `vkCreateDevice` append the XR device extensions, and optionally request one extra queue on the
   graphics family for the XR compositor calls. The layer also sees the instance, physical device, device, queues and
   every present, which is what `XrGraphicsBindingVulkanKHR` needs. Works because the game reaches every Vulkan
   function through the loader (`vkGetInstanceProcAddr`) **[high mechanism]**.
2. **LD_PRELOAD only**: the game imports `dlopen`/`dlsym` dynamically (`readelf`: `dlopen@GLIBC_2.2.5`,
   `dlsym@GLIBC_2.2.5`), so a preload can return its own `vkGetInstanceProcAddr` for `dlsym(h, "vkGetInstanceProcAddr")`
   and wrap `vkCreateInstance`/`vkCreateDevice` the same way. Equivalent, more code, no JSON **[high mechanism]**.
3. With `XR_KHR_vulkan_enable2` the wrappers can instead forward to `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR`
   (the runtime then adds its own extensions) and must make sure the game picks the runtime's physical device
   (`xrGetVulkanGraphicsDevice2KHR`) **[med]**.

### Swapchain and present

| Step | Where | Conf. |
|---|---|---|
| Game thread `renderer->vf_31(vsync)` -> render thread `FUN_00742ff0(device, vsync)`: `IDXGISwapChain::Present(interval, 0)` via vtbl+0x40, DXGI error handling (`0x887A0005` removed, `0x887A0007` reset, `0x087A0001` occluded), frame index `+0x22c` mod 3; an alternative Feral present path `FUN_02068300` when `DAT_04620051` | `0x742ff0` | high |
| `IdxDXGISwapChain::vf_08` = Present -> device `vtbl+0x30` = `IdxD3D11Device::vf_0x40_06` (`0x1ab47b0`) -> `FUN_01ac63b0` or the Feral3D device (`+0x78`) vtbl+0x18; buffer index mod buffer count (`+0x90`) | `0x1988fd0` | high |
| `IdxDXGISwapChain::vf_09` = GetBuffer: one back-buffer texture at `swapchain+0xc0` (as ID3D11/ID3D10Texture2D, or ID3D12Resource) | `0x1988bf0` | high |
| Back buffer setup: `Vk_RenderToBackbuffer` (render straight into the swapchain image) or an intermediate texture copied at present; `Vk_UseSeparatePresentSubmission` | `FUN_01e614c0` | high (keys), med (meaning) |
| `CVulkanSwapchain` (0x90 B, ctor `FUN_01ddd460`) creation: surface formats/present modes (`DAT_04b141c8`), `vkCreateSwapchainKHR` (`DAT_04b141c0`) with retries | `FUN_01ddd750` | high |
| Acquire: `vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, sem +0x58, 0, &index +0x44)` | `FUN_01ddcf80` | high |
| `vkQueuePresentKHR` (`DAT_04b141a0`) with a frame limiter (`VulkanMaxFramesPerSecond`, sleeps in `FUN_02e10ea0`), result to `FUN_01ddd050` | `FUN_01db3890` (direct), `FUN_01db3500` (submit-thread command) | high |

### Best point to take each eye's finished image

1. **First experiments: the Vulkan layer at `vkQueuePresentKHR`.** The presented image is the final composite (3D +
   HUD). Make the game render both eyes **side by side** into its normal target (window/back buffer 2 x eye width,
   eye viewports `{0,0,.5,1}` and `{.5,0,1,1}` through `cam+0x3ec`), then in the layer copy the whole image into one
   double-wide OpenXR swapchain image and submit one `XrCompositionLayerProjection` whose two views use
   `subImage.imageRect` = left/right half. One copy per frame, no engine knowledge of Vulkan objects needed. The
   copy must be recorded after the game's rendering semaphore (wait on the present's wait semaphores, then signal
   the present) **[med]**.
2. **Later: an engine render target per eye.** `renderer->vf_62` creates targets; the eye target's
   `CVulkanTexture` holds the VkImage (`CVulkanResourceStorage@0x90`); copying it right after the eye's
   `EndRenderTarget` keeps the HUD out of the eye images and avoids post effects bleeding across the middle seam
   (bloom/blur at the boundary is the risk of option 1). Needs the target -> VkImage mapping traced at runtime **[low]**.
3. `Vk_DisableSwapChain` (pref `VulkanDisableSwapChain`) runs without a desktop swapchain; useful once the HMD is the
   only output, and for an off-screen test harness **[med]**.

Queue sharing: if the runtime submits on the game's graphics queue (xrEndFrame / layout transitions), that must be
serialised with the game's own submits, which may come from the separate submit thread (`Vk_UseSeparateSubmitThread`).
Simplest: disable that thread in the test prefs, or call the XR frame functions from the layer's
`vkQueuePresentKHR` (same thread as the game's present), or give the runtime its own queue **[med]**.

## 6. UI composition

The HUD and menus are drawn by the interface manager (`interfaceMgr = *(clientShell+0x80ff8)`) in
`FUN_00a11dc0(interfaceMgr, player, beginTarget, clear)` (`0xa11dc0`), called once per player view at the end of
`FUN_009ee170` and from the non-world paths **[high]**:

1. Not in game (`state != 3`) and `clear`: clear the 3D target (`clientShell+0x80e00`, colour `0xff000000`).
2. **Pick the UI target**: `clientShell+0x80dc0` if it exists (sub-rect `clientShell+0x80dc8`), else the 3D target
   `+0x80e00`. The clear flags differ by whether the two are the same object; in the separate-target case the clear
   colour is `0x00000000` (transparent). `renderer->vf_33` (BeginRenderTarget).
3. Otherwise-branch (`beginTarget` and no UI target): BeginRenderTarget on the 3D target and draw the **3D UI scene**
   (`FUN_00c1e370(interfaceMgr+0x5d8)`: `UISceneCamera` menus, full `FUN_009f2550` render into the 3D target).
4. Updates: `FUN_00c294a0`, `FUN_00e539c0`, in-game video (`IGameplayVideoMgr` vf_05).
5. State-machine message 9 (draw) -> `FUN_00a0db40` (states 1, 2) / `FUN_00a0db90` (states 3, 4); then
   `renderer->vf_40({0,0,1,1})` = **Begin2DViewport** -> `FUN_00a11bd0` = **Scaleform display** (`DAT_046a9250`
   vtbl+0x38(`interfaceMgr+0x31c8`), vtbl+0x90(viewport, 1)) -> `renderer->vf_41`.
6. `renderer->vf_34(2)` EndRenderTarget.

Before that, inside the 3D pass, `FUN_009f1d20` projects the world-anchored HUD items (`DAT_046b8580..88`, each
`FUN_00e7a490(item, &view, screenSize)`) with the current view, and `FUN_00cbd260` gives the interface its copy of the
view **[med]**. Render targets: `clientShell+0x80e00` = 3D target (flags `0x37d7`, size `+0x80df0..+0x80dfc`),
`clientShell+0x80dc0` = second target (flags `0x4401`, size `+0x80dc8..+0x80dd4`), both made by `FUN_009e6d20` through
`renderer->vf_62`; which one ends up in the DXGI back buffer was not traced **[low]**.

### HUD into a separate target for a VR panel

- The engine already has the branch: when `clientShell+0x80dc0` is set, the whole HUD/menu pass draws into it, cleared
  to transparent. For VR: create our own target with `renderer->vf_62` (UI resolution, e.g. 1920x1080), and around
  each `FUN_00a11dc0` call (hook it) swap `clientShell+0x80dc0` to that target and back. Draw the HUD once per frame
  (first eye only), copy the target to an OpenXR quad-layer swapchain (section 5, option 2) **[med]**.
- Things that will not follow the panel: the world-anchored HUD items (enemy markers, Nemesis icons, prompts
  projected by `FUN_009f1d20` with the 2D screen projection) and the 3D UI scenes (step 3, drawn into the 3D target).
  The markers need a 3D re-implementation later; the 3D menu scenes can use the same "render to our target" trick.
- No-code toggles for tests: `UI.Draw` (command, `_INIT_276`, handler `0xa0d930`) and `Render.Draw.Overlays` (bool
  cvar at `0x461e1a0`, value `0x461e1d8`, default 1, read by `FUN_00724690`; the renderer's 2D overlay layer
  `CRenderOverlayHandle`) **[med]**.
- Scaleform's own `Scaleform::Render::StereoImplBase` is linked (stereo HUD rendering support), not needed for a panel.

## 7. Hook plan for the first VR experiment

Injection: **an `LD_PRELOAD` shared library** for the engine-side hooks (the ELF is non-PIE, so the addresses below are
the runtime addresses; check the BuildID `87ec5d6f...` and the first bytes of every hooked function before patching,
then write 14-byte absolute-jump trampolines after `mprotect`) plus **an explicit Vulkan layer** (enabled by
`VK_INSTANCE_LAYERS` in the game's environment) for the OpenXR plumbing. Both load as ordinary parts of the process; no
debugger. The boot harness (`scripts/boot_check.sh`) has no option yet to put `LD_PRELOAD`/`VK_INSTANCE_LAYERS` into the
game's environment (it strips `LD_PRELOAD` for gamescope); that is the first harness change needed.

### Ordered hook list

| # | Address | Signature (SysV; floats in XMM) | Change | Thread | Conf. |
|---|---|---|---|---|---|
| 1 | `0xa5a820` `FUN_00a5a820` | `void(CPlayerCamera* cam, SomView* view, SceneParams* sp)` | post-hook: replace `view->pos/quat` with the eye pose (camera or head position + HMD pose + IPD/2 along the head's right axis), `view->fovX/fovY` with the eye FOV; keep the camera object as is | game | high |
| 2 | data pokes at init | bytes `0x46b72d2` = 1 (camera FX/shake off), `0x46b6651` = 0 (player motion blur off), `0x46b6652` = 0 (DoF off); float `0x46b65e4` = 2.0 (near plane, cm); each frame `cam+0x4dc` = 0 (roll) | comfort | game | high (code), med (visual effect) |
| 3 | `0x9ee170` `FUN_009ee170` (callers `FUN_009ef390`, `CGameModeClientHydra::vf_03`) | `void(GameMode* gm, RenderTarget** rt, void* rectOrNull, Player* p, int endFlags, bool first)` | wrapper: call it twice per player, eye 0 with the caller's `first`, eye 1 with `first = 0`, a global "current eye" for hook 1, per-eye target or `cam+0x3ec` rect (side by side); skip the HUD on eye 1 (hook 6) | game | med |
| 4 | `0x72bb20` `FUN_0072bb20` (from `FUN_0072c400`, executor of BeginPerspectiveViewport) | `void(DeviceState* ds /*0x461e840*/, LTRigidTransform* xf, float tanHalf[2], float centerOffset[2], int pixelRect[4], int type)`, near/far in XMM0/XMM1 | for eye views: set `tanHalf` and `centerOffset` from the OpenXR FOV (asymmetric frustum). The eye must be recognised on the render thread: match `xf` against a small ring the game-thread hook 1 fills (pos+quat+frame), since the render thread runs one frame later | render | med |
| 5 | `0x703300` `CRendererHandle::vf_29` (BeginFrame) | `bool(CRendererHandle*)` | pre-hook: `xrWaitFrame`/`xrBeginFrame` bookkeeping and `xrLocateViews` for the predicted display time, before hook 1 runs; store the frame's views in the ring | game | med |
| 6 | `0xa11dc0` `FUN_00a11dc0` | `void(InterfaceMgr*, Player*, bool beginTarget, bool clear)` | once per frame: swap `clientShell+0x80dc0` to our UI target (made with `renderer->vf_62`), call, swap back; HUD -> quad layer | game | med |
| 7 | Vulkan layer: `vkCreateInstance`, `vkCreateDevice` | standard | add the XR instance/device extensions, remember instance/physical device/device/graphics queue family; `xrCreateSession` with `XrGraphicsBindingVulkanKHR` | game (init) | high mechanism |
| 8 | Vulkan layer: `vkQueuePresentKHR` (game calls it from `FUN_01db3890`/`FUN_01db3500`) | standard | copy the presented side-by-side image (or the eye targets) into the XR swapchain, `xrEndFrame` with the views of the frame that is being presented (from the ring, one frame older than the game thread) | render/submit | med |
| 9 | later: the audio listener (`CSoundMgr::vf_23`, slot `0x346dad0` (corrected 10-19: re-stereo.md 9.3)) and the streaming positions (`FUN_023a39e0`) | | listener = HMD pose | game | med |

Frame pacing: the game's own vsync choice (`FUN_009f2020`: `DAT_046a9e78`, -1 = auto) and the Vulkan limiter
(`VulkanMaxFramesPerSecond`) must be off so `xrWaitFrame` paces the loop.

### Minimal first experiment (no OpenXR, flat, off-screen)

"Is `FUN_00a5a820`'s output really the rendered camera, and is the layout right?"
1. A tiny `LD_PRELOAD` library with one post-hook on `0xa5a820`: log `view` (pos, quat, fovX, fovY, near, far), `cam`,
   the thread id and `cam+0x3e4` (frame counter) for 300 frames to a file; then, from a time mark on (env var), add
   **3 cm** along the camera's right axis (`quat` applied to (1,0,0)) to `view->pos` on every **odd** frame. 3 cm is
   invisible in a single still, so the same build also has a test mode with a constant **30 cm** offset that switches on
   at the mark.
2. Boot with the harness in command mode (`--steam`, the user's rule for this game), load into the world, then
   `shot before`, wait past the mark, `shot after`. Expected: the world shifts sideways with parallax (near objects move
   more), the HUD does not move, gameplay (aim, the fight circle) is unchanged because the camera object was not
   touched. The alternate-frame 3 cm mode shows up as a two-frame flicker in a short screen recording or in the log.
3. In the same run log `FUN_0072c400`'s `xf` on the render thread: the same transforms appear one frame later (proves
   the pipelining assumption behind hook 4).
Before that, a no-code check: `Camera.FOVYDegOverride` and `Render.Setting.MotionBlurQuality 0` through the test prefs.

### Biggest risks and unknowns

- **Two full deferred pipelines per frame** at VR resolution and refresh; the GPU cost is the main feasibility question.
- **Temporal effects** (eye adaptation, occlusion queries, motion blur) with two views per frame; switches for most,
  exposure needs runtime tracing.
- **Side-by-side post-processing bleed** at the seam (bloom, blur, FXAA) if one target holds both eyes.
- **Visibility/LOD once per frame** (`DAT_0462d6b0` list) - the second eye may get the first eye's LOD/streaming
  decisions; consumer not traced.
- **Cross-thread eye identification** for the projection hook (render thread one frame behind).
- **Queue sharing** between the game's submit thread and the XR runtime.
- Which target reaches the DXGI back buffer (`0x80dc0` vs `0x80e00`), and whether the HUD-target swap draws cleanly.
- Addresses are for this one build; the game needs the Steam client to boot (boot harness result 2026-10-17).

## 8. For the gameplay map (pointers tripped over; not researched further)

- `FUN_00a35340` = the client `Player`'s per-frame update: camera update `FUN_00a5d780` (unless `FUN_009e4770(clientShell)`),
  later `FUN_00a5d250` (shake/rumble/roll); reads `Player+0x420`, `Player+0x318 -> +0x30 == 3`, `Player+0x2d8 -> +0x1b8`
  (a transform) **[med]**.
- Photo mode: `FUN_00a5a820` asks `FUN_009fa370(*(Player+0x310))` and then takes the view from `FUN_00dbf3f0(*(Player+0x310)+0x8b8)`;
  re-gameplay.md has `PlayerPhotoMode` at `Player+0x318` - one of the two offsets is the photo-mode component, the
  other its state holder **[low]**.
- `FUN_009ee170` feeds three global shader parameters from the player every view (`Player+0x2d8 -> +0x1b8` position,
  `Player+0x2c8 +0xf8..+0x100` a velocity vector normalised, `FUN_00a3aa00(player)` a speed) - likely foliage/water
  interaction **[low]**.
- Camera FX -> controller rumble: `FUN_00a5ad40` -> `FUN_00aaa450(PlayerInput, channel 0..6)` **[med]**; VR haptics can
  hook there.
- `Player+0x438` = the player's atmosphere-state container (`CRainAtmosphereSystemState` lookup in `FUN_00ca52d0`) **[med]**.
- The world-streaming / clutter positions come from the camera in `FUN_009ef390` (`FUN_023a39e0`, engine vtbl+0xa0
  -> +0x28) **[med]**. The audio listener is Wwise via `CSoundMgr::vf_23` (slot `0x346dad0`) (corrected 10-19: re-stereo.md 9.3); in VR it
  should follow the HMD.
- The game mode's frame camera (`clientShell+0x81018` vtbl+0x58) feeds the client-FX managers and `FUN_00d973e0` each
  frame **[med]**.
- Keep `cam+0x338` (the camera object) = the gameplay camera: the fight circle's on-screen scoring and aiming read it
  (re-gameplay.md section 6).

## Next steps

1. Harness: an option in `scripts/boot_check.sh` to pass `LD_PRELOAD` and `VK_INSTANCE_LAYERS`/`VK_LAYER_PATH` to the
   game only (not to gamescope).
2. Run the minimal experiment of section 7; confirm the `SomView` layout, the thread split and the one-frame lag.
3. Runtime: which render target reaches the back buffer; the consumer of the visibility lists (`DAT_0462d6b0/d6d0`);
   whether the exposure pass runs per RenderScene.
4. Static, if needed: `FUN_00810e80` / `FUN_007d6e20` (the deferred passes) for the exposure, occlusion-query and
   motion-blur passes; `FUN_00d8e180`'s overlay-scene list (what registers into `DAT_046b72e0`); the RT -> VkImage path
   (`CVulkanTexture`, `CVulkanResourceStorage@0x90`).
