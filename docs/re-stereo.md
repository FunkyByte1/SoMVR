# Shadow of Mordor (Feral Linux build): two eye views per frame - stereo rendering plan

Static analysis only (2026-10-18): read-only `scripts/ghidra_query.sh` batches on `som-linux` (2 batches, run when the
gate was open), `objdump` of address ranges of `reference/bin/linux/ShadowOfMordor`, the TSV indexes and the earlier
raw batches in `reference/work-view/`. Nothing launched, nothing written into the install. Scratch:
`$SOM/reference/work-stereo/` (git-ignored): `b1/b2.txt` + `q1/q2.txt` (Ghidra), `d_*.txt` (objdump ranges),
`init82_named.tsv` (the shader-constant table: name -> data address), `fn_of.py` (address -> containing function).
Second static pass 2026-10-19 after spike1 run 2 (exposure, history state, audio listener): **section 9**, scratch
`reference/work-stereo/r2/`; the exposure row (section 3), H7 (section 6) and the risk rows (section 7) follow it.

Builds on `docs/re-view-render.md` (sections 1-3, 7). Where the two differ, the runtime facts in NOTES.md (spike1 run 1,
2026-10-18 15:35) win, and this document follows them:
- the `SomView` layout `{pos cm, quat x y z w, fovX, fovY (rad), near, far}` is right;
- the render thread executes a command ~5 us after the game thread queues it, **in the same frame** (not one frame
  behind as re-view-render.md section 1 says);
- BeginPerspectiveViewport runs once per frame at the menu; `FUN_00a5a820` (player view) only with a world loaded.

Conventions as in re-view-render.md (addresses = runtime addresses, `vf_NN` = vtable slot, **[high]/[med]/[low]**).
New names used below: `device` = the renderer's device object at `0x461fa40` (its D3D11 context pointer is
`0x461fa48`), `ds` = the device state at `0x461e840` (`ds+0x30` = the current view state `0x461e870`),
`rt3D` = `clientShell+0x80e00`, `rtUI` = `clientShell+0x80dc0`, `sp` = the scene-parameter record built per view.

## 1. FUN_009ee170, the per-view body

```c
void FUN_009ee170(CGameModeClientHydra* gm, RenderTarget** rt, const int origin[2] /*may be NULL*/,
                  Player* p, int endFlags, bool first);
// callers: FUN_009ef390 at 0x9ef51a (rt = &copy of rt3D, origin = {0,0}, endFlags 2, first = first rendered player)
//          CGameModeClientHydra::vf_03 (0x9efb30, the capture path: caller's RT, origin {0,0}, endFlags 1, first 0)
```

The third argument is **not a rect**: `CRendererHandle::vf_33` (BeginRenderTarget) reads two ints from it (or 0,0 for
NULL) and the device code adds them to the pixel rect - it is the **pixel origin of the target inside the device's
composite surface** (section 4) **[high]**.

Step by step (addresses = the call sites inside FUN_009ee170):

| # | What | Per call? | Conf. |
|---|---|---|---|
| 1 | Player shader parameters: `renderer->vf_92` x3 (handles `gm+0x28/0x30/0x38`): player position (`Player+0x2d8 -> +0x1b8`), normalised velocity (`Player+0x2c8 +0xf8`), and a smoothed "movement" value computed from the displacement since the **previous call** (static state `DAT_046a9de0..0x46a9e0f`, updated every call) | yes, with hidden state | high (code) |
| 2 | `FUN_00a5a3f0(cam)` roll backup (`0x9ee7bc`), `FUN_00a5a820(cam, &view, &sp)` (`0x9ee7d4`, = spike1's H1), `FUN_02824890(&sp)` scene modifiers; gate bytes `DAT_046a9300/01/02` mask three `sp` flags | yes | high |
| 3 | One-shot cut flag `gm+0x20`: if set, `sp+0x5c` (player motion-blur scale) = 0 and the flag is cleared - **the first view of a frame consumes it** | first view only, in effect | high |
| 4 | `rect = cam+0x3ec..0x3f8` copied at `0x9ee855`, **after** the H1 call: a post-hook on `FUN_00a5a820` can still change the viewport | yes | high |
| 5 | "Last view" copies: `gm+0x70..0x98` = view; `FUN_00cbd260(interfaceMgr+0x2708, {view, rect, ...})` at `0x9ef00e` (the interface's copy) - last call wins | yes | high |
| 6 | `renderer->vf_107(&view, 0)` visibility request (section 3) | yes | high |
| 7 | **if `first`**: `renderer->vf_24` (wait for the render thread, sets `renderer+0x3c` = 1, which BeginRenderTarget asserts), `FUN_00a12420(interfaceMgr, 0)` at `0x9ef070` (interface pre-render: `FUN_00c291e0`, the interface render-to-texture scenes `FUN_00ad7890`, state-machine message 10), `vf_56` (returns 0 in this build) -> `FUN_00e93c20` (gated by `DAT_046b8b41`) | once per frame | high |
| 8 | `FUN_00a10d30(interfaceMgr)` (= `FUN_00c24c30(+0x2af0)`, a UI state that hides the world): if true, no 3D at all, only `FUN_00a11dc0(im, NULL, 1, 1)` (`0x9ef20a`), then step 13 | yes | high (code), med (meaning) |
| 9 | **BeginRenderTarget** `vf_33(renderer, rt, &clearFlags = 0, colour 0, origin, 0)`: **no clear**. The pipeline overwrites its viewport; pixels outside the viewport should keep their content (the experiment checks it) | yes | high (no clear), med (the rest) |
| 10 | if `fovX`, `fovY` > eps: **BeginPerspectiveViewport** `vf_37(near, far, &view, &view.fovX, &rect)` at `0x9ef2b5` (return address `0x9ef2bc`); if it returns true: `FUN_009f23c0` (texture-streaming budget for this view, only if `sp+0x324`), `FUN_00a59a60(cam, &sp)` = `vf_43` **RenderScene**`(&sp, 0)` + a copy of `sp` into `cam+0xb0..`, lens rain `FUN_00ca52d0/FUN_00ca53e0(Player+0x438, rt)`, world-anchored HUD items `FUN_009f1d20(&view, UI size clientShell+0x80dc8..0x80dd4)` (`0x9ef354`), `vf_41` EndViewport | yes | high |
| 11 | `FUN_009f2410` (budget back), `FUN_009f2460` (overlay scenes `FUN_00d8e180` with the same camera: each `vf_37 + vf_45 + vf_41`; optional 2D pass `FUN_009f1be0` if `sp+0x323`) | yes | high |
| 12 | **EndRenderTarget** `vf_34(endFlags)`, then the HUD **`FUN_00a11dc0(im, p, 1, 1)`** at `0x9ef1e0` | yes | high |
| 13 | `FUN_00a5a560(cam)` restore the camera object | yes | high |

**Which target**: `*rt`. From the player loop that is `rt3D` (flags `0x37d7`), which has no colour texture of its own:
it is a window onto the device's **3D composite surface** (`device+0x1c0`, section 4). From the capture path it is any
target the caller passes - precedent that the player view can be rendered into a target of our choice **[high for the
calls, med for the "window" reading]**.

**Rect to pixels** (render thread, `FUN_0072c400`) **[high]**:
- `rect` (0..1 fractions, clamped to 0..1) times the **current RT's** size (`ds+0x10/0x14`, set by BeginRenderTarget),
  `+ 0.5`, truncated; an empty rect drops the view;
- `+ origin` (`FUN_007439e0`: only for targets with flag `0x400`, otherwise 0);
- the result is the view state's pixel rect (`0x461e874..0x461e880`), the hardware viewport
  (`FUN_00743a00` -> `ID3D11DeviceContext::RSSetViewports`, vtbl+0x160) and `k_vScene_TexCoordScale` =
  0.5 x (viewport size / RT size). There is **no viewport-offset constant** (section 5).
- `cam+0x3ec` itself is `FUN_00d286a0(w, h, inset = cam+0x3b0)` = `{l/w, t/h, 1 + r/w, 1 + b/h}` of an integer inset
  (written by `FUN_00a59960` from `FUN_009e6d20`, and by `FUN_00a5e0f0`, 4 callers) - normally `{0,0,1,1}`.
  The FOV pair is not adapted to the rect: whoever shrinks the rect must also change `fovX`.

**`first`** changes only step 7 (sync + interface pre-render). It does not touch clears, post-processing, the HUD or
present **[high]**.

**`endFlags`** goes to EndRenderTarget (`vf_34` -> `FUN_0072ac00` -> `FUN_0074bff0(rt, origin, flags)`) **[high code,
med names]**:
- 0 = unbind only (`OMSetRenderTargets(0, ...)`);
- 1 = resolve: for multisampled targets (flag `0x10`) a shader resolve `FUN_007a9c20` or a `CopySubresourceRegion`;
- **2 = "mark for present"**: for `0x400` targets sets the device byte `+0x1fa` (3D surface) or `+0x1f9` (UI surface,
  flag `0x4000`); the present composite `FUN_00742d50` draws only marked surfaces;
- 3 = for `0x400 | 0x20` targets: copy the target's region of the composite surface into the target's own texture
  (`rt+0x178`), i.e. "make what I drew readable".
- Post-processing is never optional here: it runs inside every RenderScene (step 10). The HUD runs on every call
  (step 12). Present never happens inside FUN_009ee170.

**Once per frame, outside FUN_009ee170** **[high unless marked]**:
- `renderer->vf_29` BeginFrame (`0x703300`): frame counter `renderer+0x38`, visibility frame swap `FUN_007b7ad0`
  (`0x462d700`: toggles the request buffer, clears the "first view done" byte `+0x14`, frame number `+0x24`), and on
  the render thread `FUN_0072a850`: **`k_fTime` (`0x46254c0`), `k_fRealTime` (`0x4625460`), `k_fDeltaTime`
  (`0x4625400` = dt, `0x4625404` = 1/dt)** and per-frame resource work (`FUN_007b4b20(0x462ce60)`, `FUN_0072e2b0`);
- camera FX and the game mode's frame camera to the FX managers (CGameClientShell::vf_02 step 4);
- in `FUN_009ef390`: world streaming, physics viewers, vegetation/clutter working sets (not audio: section 9.3) and
  `FUN_00e861d0` (section 2);
- `vf_30` end of frame, `vf_31` Present -> `FUN_00742ff0` -> composite `FUN_00742d50` -> swapchain (section 4);
- end of CGameClientShell::vf_02: previous camera `cam+0x3c0..0x3e3` copied from the camera (motion blur).

## 2. The split-screen path, FUN_009ef390

`FUN_009ef390(gm)` (`0x9ef390`, from `CGameModeClientHydra::vf_02` when a world is loaded) **[high]**:
1. **Once, for all local players together** (skipped in photo mode): for every player in `clientShell+0x80..+0xa0`
   it reads the camera object position (`cam+0x338 -> +0x8`) and the orientation (`cam+0x458..` composed with
   `cam+0x484..`), then
   - world streaming: engine `vtbl+0xa0` object, `vtbl+0x28(0, 0, positions[], clientShell+0x78)` with all positions;
   - if at least one player: the physics viewers (engine `vtbl+0x18` = `IEnginePhysics`) `vtbl+0x88(&poses)` and the
     vegetation/clutter working sets `FUN_023a39e0(clutterMgr, &poses)` (**not** the audio listener, section 9.3);
   - `FUN_00e861d0()` (not read).
2. **Per player**: walks the same list again; for each player with `FUN_00a36000(p)` (= `cam+0x4f0 != 0`, the
   camera's "render me" byte): `FUN_009ee170(gm, &rt3D, origin {0,0}, p, 2, first = no player rendered yet)`.
3. If no player rendered: `vf_24` (sync) so the frame still synchronises.

So two local players would get **the same target (`rt3D`), the same origin (0,0), their own camera and their own
`cam+0x3ec` rect**. Nothing in this build assigns split rects: `FUN_00a59960`/`FUN_00a5e0f0` always derive `cam+0x3ec`
from the full 3D rect plus the per-camera inset. Split screen would have to come from the insets (`cam+0x3b0`), which
nothing sets to a half **[med]**. Shared/once between players: the streaming/audio pass (step 1), the sync and
interface pre-render (`first`), BeginFrame/Present, the UI target. Per player: everything in section 1, including a
whole RenderScene and a whole HUD pass (`FUN_00a11dc0(im, p, ...)` per player into the same UI target).

**Driving it for two eyes** (one real player, the body called twice):
- Hook the call at `0x9ef51a` (`e8 51 ec ff ff`, preceded by `48 8b 48 f8` at `0x9ef516`) and call the original
  twice: eye L with the caller's `first`, eye R with `first = 0`. Calling the body twice in one frame is what the
  player loop does for N players; the BeginRenderTarget assert stays quiet because `renderer+0x3c` stays 1 until
  Present **[high]**.
- Side by side in one target: rect `{0,0,0.5,1}` for L and `{0.5,0,1,1}` for R, via `cam+0x3ec` from the H1
  post-hook (read after it, step 4) or, without touching the camera, a modified copy of the `rect` argument in the
  existing H2 (`vf_37` slot) for return address `0x9ef2bc` only. `fovX` must shrink with the width:
  `fovX' = 2*atan(0.5*tan(fovX/2))` at the same `fovY`.

**What may block side by side in one target**:
1. **The full-screen passes get a viewport scale but no offset** (`k_vScene_TexCoordScale` = 0.5 x size ratio, no
   companion offset constant; `k_vScene_ScreenRes` `0x4625340` = the bound target's width/height, set at every
   BeginRenderTarget by `FUN_0074b1e0`, may be what the shaders use with the pixel position instead). If a pass builds
   its texture coordinates as `ndc * scale + scale`, the right eye samples the left half of the G-buffer/HDR buffers.
   Not decidable without the shaders - **this is the first thing the experiment (section 8) answers** **[low]**.
2. The engine's own way to place a view elsewhere is the target origin (step 9), but BeginRenderTarget refuses
   `origin + RT size > composite surface size` (`FUN_0074bb70`), and `rt3D` is as big as the surface, so only
   (0,0) passes with `rt3D` **[high]**. A half-width `0x37d7` target with origin `(W/2, 0)` would pass the check;
   whether the pipeline's intermediate buffers follow an origin is unknown **[low]**.
3. Seam bleeding: bloom, DoF, blur and FXAA kernels may read across the middle unless they clamp to the viewport
   **[low]**.
4. Per-call side effects that assume one view (section 3, "other per-call state"): the foliage statics, the cut flag,
   the last-view copies, the HUD (drawn twice; harmless while the UI target is separate and cleared per pass, but the
   world-anchored HUD items end up projected with the last eye).

## 3. Per-frame versus per-view state

Where the work happens: the game thread prepares each view (section 1); **culling, shadows, lighting, exposure and post
all run on the render thread inside each RenderScene** (`FUN_007a24d0` -> frame context `FUN_00810e80` from the
current view state -> `FUN_007d7840` -> `FUN_007d6e20`) **[high for the call chain]**. So most state is per view by
construction; the problems are the few things the engine updates once per frame or carries across calls.

| State | Where | Frame or view | Second view in the same frame | Cheapest fix for eye 2 | Conf. |
|---|---|---|---|---|---|
| Visibility requests | `vf_107(view, flag)` -> `FUN_007b79f0(0x462d700, view, flag)`: builds a request (view + slice of a per-frame buffer, frame number `+0x24`) and runs `FUN_007b6ab0` over two **node lists**: `0x462d6b0` only for the first request of the frame (byte `+0x14`, cleared by BeginFrame), `0x462d6d0` for every request with flag 0 (flag 1 = "per-frame list only"). The lists hold registered render nodes; each node seen in the last 5 frames (`node+0xd4`) or forced gets its per-view callback (`vtbl+0x18`), `CRenderNodeMeshCustom` nodes a render-thread update command. Readers of the lists: only `FUN_007b6890`/`FUN_007b69f0` (registration) | `d6b0` nodes: per frame, first view; `d6d0` nodes: per view | `d6b0` nodes keep eye L's setup for eye R (6 cm apart: small) | Before the eyes, one `vf_107(centreView, 1)` with the head pose and a FOV that covers both eyes: the per-frame nodes get the centre, each eye's own request then goes to `d6d0` only | high (code), med (what the nodes are) |
| Frustum culling | render thread: `FUN_00810e80` copies the view state into the frame context; the frustum corners come from `FUN_00810190` (call `0x811e58`) | per view | correct for each eye, **except** `FUN_00810190` uses `tanHalf` only, not the centre offset (section 5) | widen `tanHalf` by the offset for that call only (hook H6) | high (code), med (that the corners drive culling) |
| LOD | no game-side per-frame LOD pass found; distance LOD is computed while drawing from the frame context (`CharacterProxy.SetLODBias` only biases) | per view | the two eyes can pick different LODs right at a threshold | accept; if visible, bias LOD for eye R or share the centre position | low |
| Streaming | world streaming: all camera positions once per frame (`FUN_009ef390` step 1); texture budget per view (`FUN_009f23c0/9f2410` on `DAT_046a9238`, set and restored around each view, only if `sp+0x324`) | frame / view | none (positions = the gameplay camera; the budget is restored each time) | none; later feed the HMD position to step 1 | med |
| Shadow maps | sun cascades `FUN_007e2940` (writes `k_mShadowMapMatrix 0x4624000`, `k_vShadowSplitDistSquared 0x4623d00`) from `FUN_007dc0b0`; other shadow casters `FUN_007e0be0`/`FUN_007e4a00` from `FUN_007db760`; all inside `FUN_007d6e20`. Also `FUN_007f2cf0`: an orthographic capture re-centred on the camera with texel snapping, every RenderScene | **per view, re-rendered** | correct but twice the shadow cost; the cascades are fitted to each eye's frustum, so the cascade edges sit slightly differently per eye (possible shimmer at the boundaries) | accept first. Cost reduction later: fit to a centre/union frustum and skip the shadow passes for eye R (needs the pass code and keeping the shadow targets) | med (per view), low (reuse plan) |
| Occlusion queries | no general GPU occlusion culling found (culling = frustum + `Vis.AreaCull.*` minimum projected area). Queries exist for: render-target visibility (`CRenderTargetVisQuery`, `IRenderTargetVis`), ClientFX `OcclusionQueryFX` (lens flares, sun glare), `occlusionquery.mat` in the pipeline's material set (`FUN_007b0820`), backend `CVulkanOcclusionQuery` | per view (issued per RenderScene, read back later) | eye R re-issues the same query objects: results flip between eyes -> flare flicker | lens flares off for VR (ClientFX data); nothing else depends on them | med |
| Eye adaptation | `FUN_007ea320(state = 0x4625840, ctx)` per RenderScene (call `0x7d740b` in `FUN_007d6e20`): luminance downsample of the **whole bound target** (no rect input), GPU `ComputeDynamicExposure` (active path, `0x4635f66` = 1) into the target's own buffer `rt+0x50 -> +0xb0` (`DynamicExposureBuffer` `0x4621fa0` is only the binding slot), step limit `k_fMaxAdjustAmount = (k_fRealTime - 0x4625844) x ExposureAdjustSpeed`, then `0x4625844 = k_fRealTime`; `k_fDeltaTime` is not an input (section 9.1) | per view (the pass), **per frame (the step)** | eye R gets step 0 and keeps eye L's value (same `rt3D` buffer); each eye's measurement covers the whole target, so in layout A it mixes both halves | layout B with both eyes in `rt3D` (H8 copy-out): nothing to do. Own eye-R target: copy eye L's buffer before eye R's tonemap (section 9.1). Fixed exposure: `0x4635f58` = `0x4635f5c` = X. H7 dropped | high (CPU side), med (shader) |
| Motion blur, previous camera | `cam+0x3c0..0x3e3` copied once per frame (end of CGameClientShell::vf_02); `FUN_00a5a820` copies it into `sp+0x24c..0x26f` on **every** call (no state change, condition `cam+0x3e4 == cam+0x3e8 + 1`); render side: the context's previous camera feeds the motion-blur pass `FUN_007e7fb0` (`k_mPrevViewToWorld 0x4624be0`, `k_mPreviousMotionTransform 0x4622f00`, prev view built with `FUN_0072c7f0`) and `FUN_007c5020` (from the post pass `FUN_007eb1d0`) | per frame (the data), per view (the pass) | both eyes reproject against last frame's **centre** camera: the eye offset reads as motion (the smear seen in run 1) | off: `Render.Setting.MotionBlurQuality 0` (the only switch that also skips the post-pass velocity blur), or byte `0x46b6651` = 0 (spike1 comfort poke: clears `sp+0x58` only; `sp+0x55` still runs `FUN_007c5020`/`FUN_007c4d10`, so add `0x46a9300` = 0, section 9.2). If ever wanted: in H1 write this eye's previous-frame pose into `sp+0x24c..0x267` | high |
| Time constants | `k_fTime`, `k_fRealTime`, `k_fDeltaTime` set once per frame (`FUN_0072a850`) | per frame | identical for both eyes (good for animated shaders and particles) | none; never zero `k_fDeltaTime` for one eye (it animates every eye-R shader, section 9.1) | high |

**Other per-call state in FUN_009ee170** (found while reading it; each makes eye R differ from eye L unless handled)
**[high for the code]**:
- the player/foliage shader parameter (step 1) keeps the previous position and a smoothed value in
  `DAT_046a9de0..0x46a9e0f`; a second call in the same frame sees zero displacement. Fix: snapshot those 0x30 bytes
  before eye L and restore them before eye R, so both compute the same value;
- the cut flag `gm+0x20` (step 3) is consumed by eye L. Fix: read it before eye L, re-arm it before eye R;
- `gm+0x70..0x98` and the interface's view copy (step 5) end up holding eye R. Fix: after both eyes write the centre
  view back to `gm+0x70..0x98` and call `FUN_00cbd260` once more with it (who reads them was not traced; aim and
  the fight-circle scoring read the camera object, which stays untouched);
- the lens rain (`FUN_00ca53e0`) and the world-anchored HUD items (`FUN_009f1d20`) run per view: rain drops are
  drawn into each eye (screen space, should go for VR anyway), HUD items keep the last eye's projection.

## 4. Render resolution, targets, and what reaches the swapchain

**Where the 3D size comes from** **[high code, med names]**:
- `clientShell+0x80df0..0x80dfc` = the 3D rect `{x0, y0, x1, y1}`, filled by `FUN_00d28b50(rect, letterbox, 0)` from
  `FUN_009ea410` (shell init) and `FUN_009e6b50`: it asks the engine (`DAT_0466afa8 vtbl+0xb0`) for the display mode,
  takes its width/height (the `ScreenWidth`/`ScreenHeight` settings of clientmgr.cpp), and if `letterbox` is set fits
  a centred 16:9 rect inside (bars = `x0/y0`). Fallback 640x480 (or 640x420 letterboxed).
- `rt3D` = `renderer->vf_62(&rt, w = x1 - x0, h = y1 - y0, flags 0x37d7)` in `FUN_009ea410` and again in
  `FUN_009e6d20` (when `rt3D` is null; this one also hands it to the first player's camera: `FUN_00a59960` ->
  `cam+0x398` target, `cam+0x3a0` int rect, `cam+0x3ec` 0..1 rect). The UI target `rtUI` = `vf_62(w, h, 0x4401)`
  from `clientShell+0x80dc8..0x80dd4` (`InterfaceWidth/Height`).
- `vf_62` (`0x703700`) = CreateRenderTarget(out, renderer, w, h, flags): game thread allocates the 0x480-byte target
  (`FUN_0074f270`), render thread `FUN_007cab30` -> `FUN_0074f230` -> `FUN_00740d40` + `FUN_0074df10(rt, w, h, flags, ...)`.
- Run time: `SetResolution <ScreenWidth> <ScreenHeight> <InterfaceWidth> <InterfaceHeight>` (console command) and the
  Feral launcher's `+ScreenWidth/+ScreenHeight/+WindowWidth/+WindowHeight` lines in `feralArguments.cfg`
  (`CShadowOfMordorGame::vf_96`). Whether the game accepts sizes outside its mode list (`CResolutionListGenerator`,
  `AdjustResolution`, `MaxDefaultResolutionArea`) was not traced **[low]**.

**Flag `0x400` = a "screen" target** **[high for the code, med for the name]**:
- `FUN_0074df10` creates the target's own colour texture (`FUN_00747e60` into `rt+0x70`) **only when `0x400` is
  clear**;
- BeginRenderTarget (`FUN_0074bb70`) for `0x400` targets checks `origin + size` against the device's composite surface
  - `0x4000` set: the UI surface (size `device+0x90/0x94`), clear: the 3D surface (size `device+0xd8/0xdc` =
  `0x461fb18/1c`) - and refuses (returns 0, the whole view is skipped) if it does not fit;
- EndRenderTarget(3) copies from `FUN_00741150(device, 1|2)` = the device's composite texture into `rt+0x178`.
So `rt3D` (`0x37d7`) and `rtUI` (`0x4401`) are windows onto two textures the device owns.

**The device's surfaces** (`FUN_007414b0`, at video-mode set-up) **[high]**:

| Surface | Texture | RTV / SRV | Size / format |
|---|---|---|---|
| back buffer (swapchain buffer 0, `IDXGISwapChain::GetBuffer`) | `device+0x1b0` | `+0x1c8` / `+0x1e0` | window |
| **3D composite** | `device+0x1c0` (`CreateTexture2D`, bind RT\|SR) | `+0x1d8` / `+0x1f0` | `device+0xd8/0xdc`, format `+0xe8` |
| UI composite | `device+0x1b8` - **the same texture as the 3D one when both sizes are equal** | `+0x1d0` / `+0x1e8` | `device+0x90/0x94`, format `+0xa0` |

**Present**: `vf_31` -> `FUN_00742ff0` -> `FUN_00742d50`: for the surfaces marked by EndRenderTarget(2),
`FUN_007a9e70(three floats from FUN_007a24a0 (probably gamma/brightness/contrast), blitter 0x461fb60, context, src3D = SRV +0x1f0,
srcUI = SRV +0x1e8, 3D size, dst = back-buffer RTV +0x1c8 ...)` draws the 3D surface scaled to the back buffer and the
UI over it; then `IDXGISwapChain::Present` -> Vulkan. **Neither `rt3D` nor `rtUI` is the swapchain image: the back
buffer receives a scaled composite** **[high for the data flow, med for the blit's parameters]**. A ready-made redirect exists: if the function
pointer `device+0x200` (`0x461fc40`, context `+0x208`) is set, `FUN_00741150` asks it for the composite textures and
`FUN_00742d50` for the destination RTV; nothing in the game sets it (only zeroed in the device ctor `0x73fb7d`)
**[high code, low purpose]**.

**Getting eye-sized images**:
1. **Cheapest: make the game's own 3D size the eye size** - `ScreenWidth 1832 ScreenHeight 1920` (one eye per
   surface) or `3664 x 1920` (side by side). Every internal buffer then follows the game's normal path, and the UI
   stays a separate surface at `InterfaceWidth/Height` (separate as long as the sizes differ). Untested: whether a
   tall, non-listed size is accepted **[low]**.
2. **Our own target**: `vf_62` at 1832x1920. With `0x37d7` (has `0x400`) it is only another window onto the same 3D
   surface (no new pixels) and must fit inside it - useless for a second eye. With `0x400` cleared (e.g. `0x33d7`) it
   gets its own colour texture and keeps the other flags' auxiliary buffers; FUN_009ee170 accepts it (the capture
   path does exactly this with `endFlags 1`). Never seen in game, the pipeline might expect a screen target
   **[low]**.
3. **Resize the game's target**: `rt3D` is re-created by `FUN_009e6d20` when `clientShell+0x80e00` is null and by
   the video-mode path; `SetResolution` is the supported way **[med]**.

**For VR the best tap is the 3D composite surface (`device+0x1c0`) right before `FUN_00742d50`** (render thread): full
render resolution, no HUD, no desktop scaling. The swapchain image (re-view-render.md section 5, option 1) also works
for a first look, but it is scaled to the window and has the HUD composited in.

## 5. The render-thread side: projection, rays, constants

**Commands per player view** **[high]**: one BeginPerspectiveViewport (`vf_37` at `0x9ef2b5`) -> executor
`FUN_007c9e00` -> `FUN_0072c400(near, far, ds, xf, fov, rect)` -> **`FUN_0072bb20`** (call `0x72c749`, spike1's H3);
plus one more of each per overlay scene (`FUN_00d8e180`). Views built *inside* RenderScene do not go through
`FUN_0072bb20`: they use `FUN_0072c7f0` -> `FUN_0072b050` into a local view state (the fixed 1.13 x 0.65 rad
secondary view in `FUN_007d6e20`, the previous-camera views in `FUN_007e7fb0` and `FUN_007c5020`); shadow and draw-list
code re-uploads the camera constants from the current view state with `FUN_007aa880`. Once per frame (not per view):
the 3D UI scenes and interface render-to-texture (`FUN_009f2550`), also through `vf_37`.

**`FUN_0072c400`**: `Render.Ortho` (`0x461ea58`) > 0 switches to orthographic; FOVs clamped to 0..179 deg
(`0x461ea68` = 3.12414); `tanHalf = tan(fov/2)`; pixel rect as in section 1; `centerOffset = {0, 0}` (`local_30`);
then `FUN_0072bb20(ds, xf, tanHalf, centerOffset, pixelRect, type)` with near/far in XMM0/XMM1.

**`FUN_0072b050`** writes the view state `0x461e870` (int index = 4-byte slot) **[high]**: `[0]` type, `[1..4]` pixel
rect, `[5..7]` pos, `[8..b]` quat (normalised), `[c,d]` tanHalf, `[e,f]` centerOffset, `[10,11]` near, far,
`[12..1d]` world-to-view 3x4, `[1e..29]` view-to-world 3x4, `[2a..39]` projection, `[3a..49]` view-projection,
`[4a..59]` viewport x view-projection. The perspective projection is

```
( 1/tx   0     -cx/tx  0    )      x_ndc = (x/z - cx) / tx
( 0      1/ty  -cy/ty  0    )      y_ndc = (y/z - cy) / ty
( 0      0      0      near )      depth = near / z   (reversed Z, infinite far plane; far is only in k_vScene_ZRange)
( 0      0      1      0    )      view space: +x right, +y up, +z forward (left-handed)
```

**So `centerOffset` is in tangent units, the same units as `tanHalf`; positive = frustum shifted right/up; the frustum
spans `[cx - tx, cx + tx] x [cy - ty, cy + ty]`** **[high]** (this settles the [med] in re-view-render.md section 2).
For an OpenXR `XrFovf` (`angleLeft < 0 < angleRight`, `angleDown < 0 < angleUp`):
`tx = (tan R - tan L)/2`, `cx = (tan R + tan L)/2`, `ty = (tan U - tan D)/2`, `cy = (tan U + tan D)/2`. The game side
then passes `fovX = 2*atan(tx)`, `fovY = 2*atan(ty)` (H1) and the render side adds `(cx, cy)` (H3). Pose conversion
from OpenXR (right-handed, -z forward, metres): `pos = 100 * (x, y, -z)`, `quat = (-qx, -qy, qz, qw)` **[med: the
standard mirror conversion, check with one logged frame]**.

**Constants written by `FUN_0072bb20`** (names from the parameter table `_INIT_82` `0x561370`; address = the data the
binding uploads; `FUN_00750080` pushes it when the handle is not -1) **[high]**:

| Constant | Data | Value |
|---|---|---|
| `k_mViewToWorld` | `0x4624f60` | view state `[1e..29]` |
| `k_mWorldToView` | `0x4624ee0` | `[12..1d]` |
| `k_mWorldToClip` | `0x4624e60` | `[3a..49]` (includes the centre offset) |
| `k_vWorldSpaceCameraDir` | `0x46241a0` | forward axis from the quat |
| `k_vPerspOrthoMask` | `0x46250e0` | (1, 0) perspective, (0, 1) ortho |
| **`k_vFullViewPlane`** | `0x4625140` | `(cx - tx, cy + ty, 2 tx, -2 ty)`: the view ray through viewport UV `(u, v)` is `(x0 + u dx, y0 + v dy, 1)` - exactly the projection above, offset included |
| `k_vScene_ZRange` | `0x4625280` | `(near, far, FLT_MAX)` |
| `k_vScene_TexCoordScale` | `0x46252e0` | `0.5 * (viewport size / RT size)`; no offset term |
| (viewport) | | `FUN_00743a00` -> `RSSetViewports(pixel rect)` |

`k_vFullViewPlane` and `k_vScene_TexCoordScale` have no other writer (xrefs). The matrices are re-uploaded by
`FUN_007aa880` (also `k_mViewToClip 0x4624de0`, `k_mObjectToClip/View`, `k_vObjectSpaceEyePos`) **from the same view
state**, and the frame context `FUN_00810e80` copies tanHalf, centerOffset and all matrices into `ctx+0x494..` for the
passes. So **a centre offset given to `FUN_0072bb20` reaches the G-buffer projection, the deferred position
reconstruction and every later re-upload** **[high for the data flow; the shader code itself was not read]**.

**One place ignores it: the frustum corners.** `FUN_00810190` (call `0x811e58` in `FUN_00810e80`: XMM0 near, XMM1 far,
RDI corners out, RSI `&viewstate[5]` (pos/quat), RDX `&viewstate[c]` (tanHalf), ECX ortho) builds the 8 corners from
`tanHalf` only - symmetric. With `cx != 0` the culling (and cascade-fitting) frustum misses a strip on the side the
offset points to. Fix (H6): for that call pass `{tx + |cx|, ty + |cy|}` instead; the hook reads `cx, cy` from
`rdx[2], rdx[3]` (they follow tanHalf in the view state), needs no eye bookkeeping, and changes nothing when the offset
is 0 (the secondary views) **[high for the code, med that culling uses these corners]**.

**Recognising the eye on the render thread**: run 1 showed H3 ~5 us after H2 in the same frame, in order. A small
FIFO filled on the game thread (H1/H2: frame number, sequence, `xf` hash, `cx/cy`) and consumed in H3 by `xf` hash is
enough; spike1 already computes the hash.

## 6. Hook plan: two eyes, one frame

Style as spike1 (`mod/spike1/somvr_spike1.c`): call-site rel32 or vtable-slot patches only, every site byte-checked
first, asm wrappers that save all call-clobbered registers (GCC 8.3 IPA-RA). H1-H3 exist in spike1. Two layouts, to be
chosen by the experiment in section 8:
- **Layout A, side by side in the game's own surface**: `ScreenWidth = 2 x eye width` (3664 x 1920 for Quest 2), eye L
  rect `{0,0,.5,1}`, eye R `{.5,0,1,1}`. No new targets. Works only if the passes honour a viewport that does not start
  at x = 0 (section 2, blocker 1).
- **Layout B, one surface per eye pass**: `ScreenWidth/Height` = eye size, rect `{0,0,1,1}` for both; eye L's result is
  copied out of the 3D surface on the render thread after its EndRenderTarget, before eye R overwrites it (or eye R
  renders into its own non-screen target, section 4 option 2). No viewport-offset risk; needs a copy per eye.

| # | ID | Site | Signature (SysV) | Thread | Change |
|---|---|---|---|---|---|
| 0 | config | `feralArguments`/prefs, data pokes | - | init + every frame (H2) | `ScreenWidth/ScreenHeight` (A or B above), vsync off; spike1 comfort pokes (`0x46b72d2` = 1 shake off, `0x46b6651` = 0 motion blur off, `0x46b6652` = 0 DoF off); `Render.Setting.MotionBlurQuality 0`; near plane `0x46b65e4` = 2.0 later for the hands |
| 1 | H5 BeginFrame | `CRendererHandle` vtable slot 29 (`0x335cb20` -> `0x703300`) | `bool vf_29(CRendererHandle*)` | game | pre: `xrWaitFrame`/`xrBeginFrame`, `xrLocateViews` for the predicted time; store both eye poses + `XrFovf` (converted, section 5) as this frame's record; rearm the per-frame state of H4 |
| 2 | H4 eye loop | call `0x9ef51a` in `FUN_009ef390` -> `FUN_009ee170` | `void(GameMode*, RenderTarget**, const int origin[2], Player*, int endFlags, bool first)` | game | call the original twice. Before eye L: snapshot the foliage statics `0x46a9de0..0x46a9e0f` and the cut byte `gm+0x20`; optionally one `vf_107(centreView, 1)` (section 3, visibility). Eye L: `g_eye = 0`, original args. Between: restore the snapshot, re-arm `gm+0x20`; layout B: the copy of eye L (H8) is triggered by its EndRenderTarget. Eye R: `g_eye = 1`, `first = 0`. After: `gm+0x70..0x98` and `FUN_00cbd260` back to the centre view; `g_eye = -1` |
| 3 | H1 view (exists) | call `0x9ee7d4` -> `FUN_00a5a820` | `void(CPlayerCamera*, SomView*, SceneParams*)` | game | post, only while `g_eye >= 0`: `view->pos/quat` = eye pose (head = camera position, later the head bone; + HMD offset), `view->fovX/fovY = 2*atan(tx), 2*atan(ty)`; push `{frame, eye, xf hash, cx, cy}` into the FIFO for H3. The camera object stays the gameplay camera |
| 4 | H2 viewport (exists) | `CRendererHandle` vtable slot 37 (`0x335cb60` -> `0x705500`) | `bool vf_37(float near, float far, CRendererHandle*, const xf*, const float fov[2], const float rect[4])` | game | only for return address `0x9ef2bc` and `g_eye >= 0`: layout A passes a copy of `rect` = the eye's half (never write the caller's memory); layout B nothing. Keeps the comfort pokes |
| 5 | H3 projection (exists) | call `0x72c749` in `FUN_0072c400` -> `FUN_0072bb20` | `void(float near, float far, DeviceState*, const xf*, float tanHalf[2], float centerOffset[2], int pixelRect[4], int type)` | render | pre: if `xf` matches the next FIFO entry: `centerOffset = (cx, cy)` (and `tanHalf = (tx, ty)` exactly, to avoid the atan/tan round trip) |
| 6 | H6 frustum | call `0x811e58` in `FUN_00810e80` -> `FUN_00810190` | `void(float near, float far, float corners[], const float* xf, const float* tanHalfAndOffset, bool ortho)` | render | pass a pointer to `{rdx[0] + abs(rdx[2]), rdx[1] + abs(rdx[3])}` so culling and cascade fitting cover the asymmetric frustum |
| 7 | H7 (dropped 2026-10-19) | was: call `0x7c9f82` (`FUN_007c9f40` -> `FUN_007a24d0`, site `RS_EXEC` in section 9.4), `k_fDeltaTime` = 0 for eye R | - | render | **not needed and harmful**: exposure steps by `k_fRealTime - 0x4625844`, which is already 0 for the second view of a frame; `k_fDeltaTime` is not an exposure input, it animates every other eye-R shader, and a plain write to `0x4625400` is not even uploaded (committed by `FUN_00750080` at set time). Replacement: none in layout B with both eyes in `rt3D`; the optional probe/copy at `EXPO_HDR` (section 9.1) |
| 8 | H8 eye copy (layout B) | EndRenderTarget executor `FUN_007c9de0` -> `FUN_0072ac00` (find the call site when writing it) | `bool(DeviceState*, int flags)` | render | after eye L's EndRenderTarget of `rt3D`: `CopyResource(eyeTexL, device+0x1c0)` through the D3D11 context `0x461fa48` (vtbl `+0x178`); `eyeTexL` made once with the device's `CreateTexture2D` (`0x461fa40` vtbl `+0x28`) at the surface size and format (`device+0xe8`) |
| 9 | H9 output | call `0x742ffd` in `FUN_00742ff0` -> `FUN_00742d50` (the present composite) | `void(Device*)` | render | pre: the 3D surface holds both eyes (A) or eye R (B): hand it (and `eyeTexL`) to the Vulkan layer for the copy into the XR swapchain; the desktop composite then runs as before (mirror). Mapping IdxD3D11 texture -> `CVulkanTexture` -> `VkImage` still to be traced; until then the layer copies the presented image (re-view-render.md section 5, option 1; HUD off with `UI.Draw`) |
| 10 | Vulkan layer | `vkCreateInstance`, `vkCreateDevice`, `vkQueuePresentKHR` | standard | init / submit | as re-view-render.md section 7 rows 7-8; `xrEndFrame` with the views of the record H5 stored for this frame (same frame now, run 1) |
| 11 | HUD | call `0x9ef1e0` in `FUN_009ee170` -> `FUN_00a11dc0` | `void(InterfaceMgr*, Player*, bool, bool)` | game | eye R: skip (eye L already drew the UI surface). Later: `InterfaceWidth/Height` = the VR panel size, take `device+0x1b8` as a quad layer |

Order of work: 0, H5 (pose record), H4 + H1 + H2 (two eyes on the desktop, symmetric, layout A or B), H3 + H6
(asymmetric frustum), H9/H10 (into the headset), then H11, the audio listener (section 9.3) and the cleanup of
sections 3 and 9.2 (H7 dropped, section 9.1).

## 7. Risks

| Risk | Why | How we find out / fallback |
|---|---|---|
| **Viewport offset in the full-screen passes** (layout A) | `k_vScene_TexCoordScale` has no offset partner; only the rasteriser viewport moves | the experiment (section 8); fallback layout B |
| **GPU cost** | every view is a full deferred pipeline **including shadow rendering** (section 3); two eyes at 1832x1920 are ~7 Mpixel per frame vs ~2 Mpixel at 1080p, plus twice the geometry and shadow passes | measure in the experiment (RATE lines, frame time); then render scale, shadow reuse for eye R, lower settings |
| Non-listed 3D size | `ScreenWidth 3664` or `1832 x 1920` may be clamped to the mode list | log `clientShell+0x80df0` and `device+0xd8` after boot; fallback: our own target (section 4 option 2) |
| Hidden per-call state | 2026-10-19: scene modifiers `FUN_02824890` = the SkyCamera list, stateless (section 9.2); camera-FX listeners `FUN_00d8b0f0` (5 calls per view from `FUN_00a5a5a0`) still untraced; ClientFX update per frame | compare the two eyes' SceneParams after H1 with offset 0 (section 9.5 test 3); pixel comparison only in layout B |
| Exposure | one adapted value per frame (eye R steps 0, section 9.1), measured over the **whole bound target**; an eye-R target of its own never adapts | layout B in `rt3D`; log `EXPO_HDR` (section 9.5 test 1); L/R mean brightness after a light change; fallback fixed exposure `0x4635f58` = `0x4635f5c` = X |
| Culling with the asymmetric frustum | `FUN_00810190` is symmetric; if other culling code reads `tanHalf` directly, H6 is not enough | look for objects popping at the outer edge with a large test offset (e.g. `cx = 0.3`) |
| Eye matching on the render thread | overlay scenes and UI scenes also call `vf_37`/`FUN_0072bb20` | match by `xf` hash, not by order alone |
| Render-thread copies (layout B, H8) | the copy must use the same D3D11 context and thread as the game (`EnableIdxD3D11Threaded` exists) | do the copy inside the EndRenderTarget executor, never from another thread |
| Target -> `VkImage` mapping (H9) | IdxD3D11 texture -> Feral3D -> `CVulkanTexture` not traced | until traced, take the presented image in the Vulkan layer |
| Gameplay reading the last view | `gm+0x70..0x98` / the interface copy are left at eye R unless H4 restores them | restore them (H4); confirm aim and the fight circle are unchanged |
| HUD | drawn per call; world-anchored items projected with the last eye; a separate HUD panel needs `InterfaceWidth/Height` != the 3D size | skip for eye R (H11); 3D markers later |
| One build only | every address is for BuildID `87ec5d6f...`; the game needs the Steam client to boot | spike1's byte checks refuse to patch anything else |

## 8. Next runtime experiment: a second eye in the right half (spike1, one more mode)

Question: **does the engine render a second player view into the right half of the same target in the same frame,
lit and post-processed like the left half?** It decides layout A versus B and gives the first stereo cost number.

Library changes (`mod/spike1/somvr_spike1.c`, same style):
1. New site **H4**, `K_CALL` at `0x9ef51a`; context bytes `0x9ef502..0x9ef51e` =
   `41 b8 02 00 00 00 48 8d 74 24 10 48 c7 44 24 20 00 00 00 00 48 8b 48 f8 e8 51 ec ff ff`; target `0x9ee170`, first
   bytes `41 57 49 89 ff 41 56 49 89 f6 41 55 45 89 cd`. Add the line to the SITES table (check_sites.py reads it).
2. `SOMVR_MODE=sbs` (trigger-gated like `off30`): H4 calls the original once while the trigger is off. With the trigger
   on: snapshot `0x46a9de0..0x46a9e0f` and the byte `gm+0x20` (`gm` = RDI); `g_eye = 0`, call; restore the snapshot and
   the byte; `g_eye = 1`, call with R9 = 0; `g_eye = -1`. Log one line per eye with the frame number.
3. H1 post-hook, `g_eye >= 0`: move `view->pos` by `(g_eye ? +d : -d)` along the right axis (`d` = `SOMVR_SBS_CM`,
   default 3.2) and set `view->fovX = 2*atan(0.5*tan(fovX/2))`.
4. H2, return address `0x9ef2bc` and `g_eye >= 0`: pass a copy of `rect` = `{0,0,0.5,1}` (eye 0) or `{0.5,0,1,1}`
   (eye 1). The H3 log then shows both pixel rects.

Run: the stage-2 boot of the next test (a world must be loaded: H1/H4 only run there; save guard `SOMVR_SAVEDIR`, and
`SOMVR_COMFORT=1` so motion blur cannot smear the eyes, `--game-env SOMVR_MODE=sbs`). Stand still in the world,
`trigger`, wait 2 s, `shot sbs`. Optional control in the same boot: a trigger word (e.g. `sbs0`) for eye offset 0, a
second shot - the halves must then be identical apart from the HUD (the "hidden per-call state" check of section 7).

Reading the one screenshot (16:9 window = two 8:9 halves; at 3.2 cm per eye the disparity of something 3 m away is
~28 px at 1920 wide, so Talion shows it clearly):

| Picture | Meaning | Next |
|---|---|---|
| Two complete, equally lit views, near objects shifted between the halves, no seam | the passes honour the viewport offset | layout A; go on with H5/H3/H6 |
| Right half black, unlit, or showing the left half's lighting/shading misplaced | full-screen passes ignore the offset | layout B (eye-size surface + H8 copy) |
| Left half damaged or cleared | something in eye R's RenderScene writes the whole target | layout B |
| Glow or blur across the middle | post kernels read across the seam | layout B, or clamp later |
| Halves differ in brightness | not per-view adaptation (one value per frame, section 9.1): full-target passes mixing the halves plus the whole-target exposure measurement - the run-2 `sbs` result (L 249.6 / R 26.6) | layout B |

Log checks: per frame exactly two H2 calls with `ra=0x9ef2bc` and two H3 calls with prects `{0,0,W/2,H}` and
`{W/2,0,W,H}` (W, H = the 3D surface); the RATE line gives the frame time with two views. A crash or GPU hang counts as
a "layout B" answer too.

## 9. Per-view state found 2026-10-19 (static, after spike1 run 2)

Run 2 (one boot, tutorial world, H4 `sbs`): `sbs` L 249.6 (white, std 12) / R 26.6 (dark) in one frame, soft ~100 px
seam; `sbs0` 2 s later dark and seamless, but the scene had changed (weak evidence); single-view offset shots (`view`
= H1, `vp` = H2, 30 cm) hazy. Ghidra batches on `som-linux`, `-q2..q5` + objdump; scratch `reference/work-stereo/r2/`
(`a1..a5`, `b1..b5`, `c1..c2` `.txt/.out`, `d_*.txt`). `ctx` = the render-thread frame context = a copy of `sp` at
`ctx = sp + 0x98` (`FUN_00810e80`: `ctx+0xed` = `sp+0x55`, `ctx+0xf0` = `sp+0x58`, `ctx+0xf8` = `sp+0x60`).

### 9.1 Exposure / eye adaptation

Call chain: `FUN_007a24d0` (RenderScene) -> `FUN_007d7840(0x4625840, ctx)` -> `FUN_007d6e20(0x4625840, ...)` -> call
`0x7d740b` -> **`FUN_007ea320(state = 0x4625840, ctx)`**, once per RenderScene. `0x4625840` is a function-local static
(ctor `FUN_007d79a0`, sets the exposure to 1.0): one for all views and all targets. Returns at once unless the bound
target (`ds+8` = `0x461e848`) has the HDR flag (`rt+8 & 0x80`).

| Item | Fact | Conf. |
|---|---|---|
| State | `0x4625840` float exposure (written by the CPU path only), `0x4625844` float `k_fRealTime` of the last call; `0x7ea84d` stores `k_fRealTime` there after **every** call | high |
| Time base | `k_fRealTime` (`0x4625460`) written once per frame by `FUN_0072a850` (BeginFrame, state 1 -> 2). `k_fDeltaTime` (`0x4625400` = ms/1000, `0x4625404` = 1/dt) has no reader on the CPU side (xrefs: that write and the `_INIT_82` registration) | high |
| Inputs (`ctx`) | `+0x10c` -> `k_fBloomScale` (LitBloomScale), `+0x110` ToneMappingEnabled, `+0x111` DynamicExposureEnabled, `+0x114` TargetLuminance, `+0x118` MinimumExposure, `+0x11c` MaximumExposure, `+0x120` ExposureAdjustSpeed, `+0x124` MaxLuminance (= `sp+0x74..0x8c`, filled by `FUN_00a5a5a0` from `CHDRAtmosphereSystemState`) | high (field -> shader name), med (`+0x10c` name, `sp` offsets) |
| Static overrides | plain globals, only writer `_INIT_117` (no cvar): `0x4635f54` speed, `0x4635f58` max exposure, `0x4635f5c` min exposure, `0x4635f60` target luminance (each used when > 0, default 0); `0x4635f64` / `0x4635f65` XOR DynamicExposureEnabled / ToneMappingEnabled (0); **`0x4635f66` = 1 GPU path**; `0x4635f68` = 1.0 (`k_fExposure`); `0x4635f6c` = 1 (tonemap mode) | high |
| Measurement | `set = *(rt+0x50)` (`FUN_0074ab60`: the bound target's HDR set). `LuminanceDownsample` (`0x462d488`) over `set+0x30[i]` (count `set+0x28`); first source the HDR texture `**(ctx+0x308)` with `k_vSourceBufferSize` = the **bound target's full size**; each level bound whole (`FUN_0072ad50`). No rect/viewport input: **the whole render target** (both halves in layout A, including the other eye's half or whatever a clear left there) | high (CPU side), med (shader unseen) |
| GPU path (active) | `ComputeDynamicExposure` (`0x462d4a8`) Dispatch(1,1,1) (`FUN_00746f60` at `0x7eab2e`): `tSourceBuffer` = last level, `k_vSourceBufferDim`, `k_fTargetLuminance`, `k_fMinimum/MaximumExposure`, **`k_fMaxAdjustAmount = (k_fRealTime - [0x4625844]) x speed`** (`0x7eaa3a..0x7eaa4e`), RW `ExposureBuffer` = `set+0xb0`. Then `DynamicExposureBuffer` (`0x4621fa0`) = that buffer's view: **a binding slot, not storage**, cleared again at `0x7d7329`. `HDRToneMap` (`0x462d468`, mode 3) reads it | high (CPU), med (that the shader limits the log2 step by `k_fMaxAdjustAmount` like the CPU twin) |
| Disabled (DynamicExposureEnabled 0) | `0x4621fa0` = `set+0xb8` (fixed buffer), `k_vHDRBloomParams2.x` (`0x4622060`) = 1.0 | high (code), low (content of `+0xb8`) |
| CPU path (dead while `0x4635f66` = 1) | last level -> CopyResource into a 2-slot staging ring of that texture (`FUN_0074ae20`, ctx vtbl `+0x178`, slot index `tex+0x1c8` toggles per copy); Map the other slot (`FUN_0074b070(tex, 1)`, vtbl `+0x70`, blocking) = **the previous call's** copy; texels -> stack array (<= ~250), sort (`FUN_007ea170` + insertion), **median** (mean of the middle two, x0.5); exposure = clamp(TargetLum / max(median, 1e-6), Min, Max); log2 step clamped to +-(realTime - last) x speed; `2^x` -> `0x4625840` and `k_vHDRBloomParams2.x` | high |
| Tonemap viewport | after the chain `FUN_0072af30` rebinds the scene target; every bind (`FUN_0074bb70`, RSSetViewports at `0x74be1e`) sets the viewport to the **whole target** + origin, so the tonemap draw covers the whole target, not the view's half | med |

**sbs / sbs0 mechanically.** Per frame: eye L steps by the frame's dt; eye R gets `k_fMaxAdjustAmount` = 0 and keeps eye
L's value in the same `rt3D` buffer. So exposure gives **one value per frame for both eyes**: no double speed, no L/R
step (H7's premise was wrong). A white/dark split inside one frame therefore means the two halves had very different HDR
input: layout A's full-target passes (viewport reset on every target bind, D3D11 clears ignore the viewport, no
viewport-offset constant: section 2 blocker 1) left eye R's half dark, and the shared exposure, measured over the whole
target (eye L's half + the dark half), was driven toward MaximumExposure: eye L blown out. The soft 100 px seam fits the
bloom `FUN_007a1530`, which runs on the whole bound target in the post pass `FUN_007eb1d0` **[med]**. `sbs0`'s black is
consistent with a dark scene at a lower exposure, nothing more. The shot was in the prologue's counter prompt: its
slow-motion screen effect (camera-FX listeners / `CScreenEffectAtmosphereSystemState`, `sp+0x130/0x144`) also runs per
view and may add to the white; repeat outside a prompt. Conclusion: layout A is out; exposure is not the blocker.

**Trap:** the step time lives in the one static `0x4625844`, the buffer per target. An eye-R target of its own gets its
own `set+0xb0` but always step 0 -> it never adapts. The same holds for any HDR RenderScene that runs before the player
view in a frame (it takes the frame's dt) **[high CPU side]**.

| Rank | Plan | Sites / data | Conf. |
|---|---|---|---|
| 1 | **Layout B, both eyes into `rt3D`** (eye L copied out after its pass, H8): the engine then measures eye L's full image once per frame and eye R reuses it (step 0). No exposure hook. Optional probe | `EXPO_HDR` (log only) | high (CPU), med (shader) |
| 2 | Eye R into its own target: at eye R's `EXPO_HDR` call (the frame's second), after the original, CopyResource eye L's `set+0xb0` buffer into eye R's (D3D11 context `0x461fa48`, vtbl `+0x178`) and redo only the tonemap - simpler: pre-call, point eye R's `rt+0x50` at eye L's set | `EXPO_HDR` | low (untested idea) |
| 3 | Fixed exposure: `0x4635f58` = `0x4635f5c` = X > 0 (target clamped to X; reached at ExposureAdjustSpeed, or `0x4635f54` = 100 for a snap). X unknown: the adapted value exists only in the GPU buffer (read it once with a staging copy in the probe, or try values) | data pokes | high (code), low (X) |
| 4 | Database / per view: `CHDRAtmosphereSystemState` DynamicExposureEnabled, ExposureAdjustSpeed, Minimum/MaximumExposure, TargetLuminance, MaxLuminance (`vf_04` `0xcac9a0` blends them; schema `FUN_0256c1d0`, `BeginHDRSettings` block) via `DBDiff.Apply`; or in H1 post `sp+0x80` = `sp+0x84` = X; `0x4635f64` = 1 only inverts the database flag (turns adaptation **on** where a state has it off) | DB / H1 / poke | med |
| - | Per-eye independent exposure (swap `0x4625840..0x4625847` and separate buffers around each eye's RenderScene at `RS_EXEC`) | `RS_EXEC` | not recommended: the eyes differ in brightness when they see different things |

### 9.2 History / previous-frame state with two views per frame

| State | Keys on | Two views per frame | Fix | Conf. |
|---|---|---|---|---|
| TAA / reprojection AA | none in this build (no strings; FXAA only, inside the motion-blur passes) | - | - | high (strings), med |
| Eye adaptation | `k_fRealTime - 0x4625844`, buffer per target (9.1) | eye R step 0: fine in `rt3D`; own eye-R target never adapts | 9.1 | high / med |
| Motion blur, previous camera | `cam+0x3c0..0x3e3` (from the camera object, end of `CGameClientShell::vf_02`) -> `sp+0x24c..0x26f` -> `ctx+0x2e4` (xf) / `ctx+0x300` (fov). Readers: `FUN_007e7fb0` (camera-blur prep, writes `k_mPrevViewToWorld` `0x4624be0` + `k_mPreviousMotionTransform` `0x4622f00`; MotionBlurQuality > 2 and `ctx+0xf0`); post pass `FUN_007eb1d0`: quality 1/2 -> `FUN_007c5020` (calls `0x7eb28f`/`0x7eba4a`, writes `0x4622f00` = current -> previous clip from the previous camera), quality 3 -> `FUN_007c4d10` (tile/neighbour-max velocity blur, call `0x7eba62`), both when `ctx+0xf0` (`sp+0x58`) **or** `ctx+0xed` (`sp+0x55`); `0x4624be0` also read by `FUN_007cbb90`/`FUN_007d1e80` (per-object velocity draws, low); keyed once per frame on the gameplay camera | both eyes compare with last frame's centre camera: any eye/head offset reads as camera motion | **`Render.Setting.MotionBlurQuality 0`** (`DAT_0461e580`; also drops FXAA) - the one switch that skips all of them; or bytes **`0x46a9300` = 0 and `0x46a9301` = 0** (default 1, set by `FUN_0056ff70`; `FUN_009ee170` `0x9ee7e6..0x9ee835` ANDs `sp+0x55` / `sp+0x58` with them; `0x46a9302` does the same for `sp+0x60`, bloom). COMFORT's `0x46b6651` clears `sp+0x58` only. Per eye later: H1 writes this eye's previous pose into `sp+0x24c..0x267` | high (code), med (`sp+0x55` = object velocity blur) |
| Camera cut | `gm+0x20` -> `sp+0x5c` = 0 (`0x9ee830..0x9ee843`, `ctx+0xf4`) | eye L consumes the cut | H4 re-arms it for eye R (planned) | high (code), low (meaning of `sp+0x5c`) |
| Bloom / sun shafts | `FUN_007a1530` + `FUN_007a21f0` on the whole bound target (post pass, gate `ctx+0xf8`) | no history; layout A crosses the seam (run 2's soft seam) | layout B; off: `0x46a9302` = 0 or `sp+0x60` = 0 | med |
| Volumetric / fog | none temporal found: fog `FUN_007e6820` per view from depth; the atmosphere states blend on game updates (`CHDRAtmosphereSystemState::vf_04` lerps fields per update, not per view) | consistent | none | med |
| SSAO / SSR | `FUN_0079e6f0` per view, no history; no SSR in the build | consistent | none | med |
| Shadows | re-fitted and re-rendered per view, no history | cascade edges per eye | accept | med |
| Occlusion queries | lens flares (`COcclusionQueryFX`) re-issued per view | flicker | off: slot `0x3b5de78` -> `ret` | med |
| Scene modifiers `FUN_02824890` | call `0x9ee7e1` (right after H1): walks the **SkyCamera** list `0x4c48c70..78` (`SkyCamera::vf_00/vf_01` add/remove); `FUN_02823ef0` looks up the sky camera's named objects and writes up to 3 slots `sp+0x1bc + i*0x30` (transform, `+0x1e4`, flag `+0x1e8`) | no time, no accumulation: identical per eye | none | med |
| Camera-FX listeners `FUN_00d8b0f0(mgr, xf, sp)` | 5 calls per view from `FUN_00a5a5a0` (`0xa5a71c/737/752/7ef/80a`), listener `vtbl+0x18`; the related query `FUN_00d8b030` (shake offset + FOV scale, from `FUN_00a5aa30`) | implementations not traced: one that integrates per call would run twice | runtime check (9.5 test 3) | low |
| Foliage/player statics `0x46a9de0..0x46a9e0f` | previous position + smoothed value, per call | second call sees zero displacement | H4 snapshot/restore (planned) | high |
| ClientFX | updated per frame on the game side, drawn per view | consistent; screen-space overlays per eye | re-ui-effects.md 3.2 | med |

**Haze in the single-view offset shots** (`view`/`vp`, 30 cm): most likely the post-pass velocity/motion-blur reprojection
against the **unmodified previous camera** - H1/H2 move only the view, `cam+0x3c0` comes from the camera object, so
every frame looks like a 30 cm sideways camera move. COMFORT=1 clears only `sp+0x58`; with `sp+0x55` set and
MotionBlurQuality >= 1, `FUN_007c5020`/`FUN_007c4d10` still run **[med]**. Second candidate: exposure still adapting to
the new framing (low). Test 4 in 9.5 decides it.

### 9.3 Audio listener

| Item | Fact | Conf. |
|---|---|---|
| Engine | **Wwise** (`AK::*`, `CAk*` RTTI, sinks `CAkSinkDirectSound`/`CAkSinkXAudio2` over `IdxXAudio2`); wrapper object `0x466df00`; game side `CSoundMgr` (instance `0x467b400`, vtable `0x346da18`, 50 slots; holder `CIMgrAPIHolder<ISoundMgr>` `0x466c600`, pointer `0x466c618`, getter `CWBEngine::vf_13` `0x8bd290`) | high |
| Setter | **`CSoundMgr::vf_23`** (slot `0x346dad0` -> `0x8ed8a0`): `bool(CSoundMgr*, const float pos[3], const float front[3], const float top[3], uint idx)`; caches them at `this+0x18/0x24/0x30`, tail-calls `FUN_008c9a40(0x466df00, ...)`, which caches `pos` at `0x466df08..10` and calls `FUN_01461200(&{front, top, pos}, idx)` = `AK::SoundEngine::SetListenerPosition` (AkListenerPosition: OrientationFront, OrientationTop, Position) | high (code), med (AK name) |
| Units / axes | cm, the engine world frame (left-handed, +Y up, +Z forward; NOTES 2026-10-18); unit front/top vectors. Wwise's own frame is left-handed Y-up too: no conversion | med |
| Distance / occlusion | per emitter `FUN_008c9b80`: `AK SetPosition` (`FUN_01460f90`) + RTPC `gp_distance` = \|emitter - cached listener pos\| (cm); `gp_occlusion` `FUN_008c9cc0` | high |
| Smoothing / doppler | none in the setter (copies through); no velocity in AkListenerPosition, no doppler code found. `Game_Velocity*` RTPCs (`FUN_00a3c8d0`) are the player's movement, not the listener's | med |
| Caller | not found statically (a virtual call through a pointer we could not trace; no direct or `0x466c618` callers): camera or Talion unknown | - |
| Not audio | `FUN_023a39e0` (call `0x9ef87a`) = **vegetation/clutter working sets** (code range of `CVegModel*`/`CClutterModelRenderer`; `FUN_023ae020` "Always need to call DropWorkingSet prior to InitWorkingSet", `FUN_023aec10` "Dropped all of our working sets while moving..."). Engine `vtbl+0x18` = `IEnginePhysics` (`CWBEngine::vf_03` -> `0x466c758`), `vtbl+0x88(&poses)` = physics viewers. Pose there: per player 28 bytes `{pos xyz` (`cam+0x338 -> +8`), `quat xyzw = (cam+0x484) (x) (cam+0x458)}` | high (code), med (names) |

VR hook: `AUDIO_LISTENER` (vtable slot): `pos` = the head position in game cm (H1's head pose), `front` = R(q)(0,0,1),
`top` = R(q)(0,1,0) of the head quaternion, then the original. Log the return address and the incoming pos once
(finds the caller, and camera vs Talion). No head velocity needed. The clutter working sets (`CLUTTER_WS`) can stay
on the camera (head = camera + a few cm).

### 9.4 Sites

All checked by `scripts/check_doc_sites.py` against build-id `87ec5d6f...`:

```c
SITE("EXPO_HDR", K_CALL, 0x7d740b, 0x7d7405, "48 89 de 4c 89 ef e8 10 2f 01 00", 0x7ea320, "83 3d 19 45 e3 03 01 0f 8e 5b 04 00 00 55 48"),
SITE("RS_EXEC", K_CALL, 0x7c9f82, 0x7c9f78, "89 d9 48 0f 44 d0 31 f6 31 ff e8 49 85 fd ff", 0x7a24d0, "83 3d 69 c3 e7 03 04 0f 85 33 01 00 00 83 3d"),
SITE("AUDIO_LISTENER", K_VSLOT, 0x346dad0, 0x346dac0, "70 07 8f 00 00 00 00 00 60 d7 8e 00 00 00 00 00 a0 d8 8e 00 00 00 00 00", 0x8ed8a0, "80 7f 08 00 75 0a 31 c0 c3 0f 1f 80 00 00 00"),
SITE("CLUTTER_WS", K_CALL, 0x9ef87a, 0x9ef86e, "48 8b 07 ff 50 60 48 89 ee 48 89 c7 e8 61 41 9b 01", 0x23a39e0, "41 55 49 89 fd 41 54 49 89 f4 55 53 31 db 48"),
SITE("POST_VELBLUR", K_CALL, 0x7eba62, 0x7eba58, "41 0f b6 d5 0f b6 f3 48 89 ef e8 a9 92 fd ff", 0x7c4d10, "41 55 49 89 fd 41 54 55 89 f5 53 89 d3 48 83"),
SITE("SKY_MODS", K_CALL, 0x9ee7e1, 0x9ee7d9, "48 8d bc 24 b0 00 00 00 e8 aa 60 e3 01", 0x2824890, "41 54 4c 8b 25 df 43 42 02 55 53 48 8b 1d ce"),
```

`EXPO_HDR`: `void(float* state, Ctx* ctx)` (render thread; `state` = `0x4625840`). `RS_EXEC`: the RenderScene executor
call (H7's old site). `AUDIO_LISTENER`: see 9.3. `CLUTTER_WS`: `void(ClutterMgr*, const float poses[][7])`.
`POST_VELBLUR`: `void(RenderTarget*, bool ctx_f0, bool ctx_ed)` (the quality-3 velocity blur; camera/object meaning of the two flags med). `SKY_MODS`:
`void(SceneParams*)`.

### 9.5 Runtime tests still needed

1. **Exposure probe** (`EXPO_HDR` pre, layout B or the current sbs): per call log frame, `k_fRealTime` (`0x4625460`),
   `[0x4625844]`, `ds+8` (`0x461e848`), `*(rt+0x50)`. Expect: two calls per frame, the second with realTime ==
   `[0x4625844]` (step 0) and the same set pointer. With layout B, L/R mean luma within 3 % after a light change.
2. **Layout-A cause** (optional, to close it): sbs shot with `0x46a9302` = 0 (no bloom): the soft seam must go; the
   brightness split should stay.
3. **Hidden per-call state**: in H1 post, with `sbs0`, copy the eye-L SceneParams and compare eye R's byte-wise
   (skip pos/quat/fov); log the first differing offsets. Any difference outside the view fields = per-call state
   (camera-FX listeners are the open suspects).
4. **Haze**: repeat the single-view `view` 30 cm shot with `Render.Setting.MotionBlurQuality 0` (or `0x46a9300` =
   `0x46a9301` = 0); log `sp+0x55/0x58` after H1. Haze gone -> the velocity reprojection; keep it off for VR.
5. **Audio**: `AUDIO_LISTENER` hook logging once per second: return address, `idx`, pos/front/top; compare pos with
   `cam+0x338 -> +8` and with Talion's position.

**Results, run 3 (2026-10-19 19:04, logs/boot-check-20261019-190441, still camera on a forge tower; NOTES.md):**
- Test 1 passed: two `EXPO_HDR` calls per frame (same `rf`), the second with step 0 and the same set pointer. Layout B's
  eye R matches the plain frame's mean luma (202.7 vs 203.6). The light-change L/R check is still to do (no light change
  in this scene).
- Layout B (sbsB/sbsB0) ran at a steady 60 fps with two full 1920x1080 eyes per frame (H1-H3 120/s, H4 60/s), no seam.
- Test 4: no haze in any still-camera shot, with the velocity blur off (`view`) or back on (`blur`): sharpness and
  |diff| are within the scene's own frame-to-frame noise. Run 2's haze came from the moving tutorial camera or
  layout A, not from the single-view offset. Velocity blur stays off for VR (COMFORT).
- Test 5: in the world the listener sits at Talion's root (the camera is 1.79 m away), front ~ his facing; in the menu
  and cinematics it is at the camera. VR sets it to the head pose.
- Tests 2 and 3 not run (layout A is dropped; test 3 needs the SceneParams compare in H1 post).
