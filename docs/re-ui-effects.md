# Shadow of Mordor (Feral Linux build): HUD/UI, screen effects and video in stereo

Static analysis only (2026-10-18). Five read-only Ghidra batches on the project copy `som-linux-q5`
(`scripts/ghidra_query.sh`). Also `objdump` of `reference/bin/linux/ShadowOfMordor` (one full listing, kept as
`full.dis.gz`), the TSV indexes, and the public notes on the HelixMod 3D Vision fix. Nothing was launched. The
install was not touched. Scratch: `$SOM/reference/work-ui/` (git-ignored). It holds `bN.txt`/`qN.txt` (Ghidra batches
and answers), `d_*.txt` (objdump ranges), `tools/fd.sh` (dump one function), `tools/sites.py` (SITE lines for every
rel32 call to a target) and `tools/vcall.py` (virtual calls through one vtable offset). `sites_all.txt` holds the raw
SITE data.

Builds on these docs:
- `docs/re-view-render.md`, sections 1, 3 and 6;
- `docs/re-stereo.md`, sections 1, 3 and 4: the UI surface, the present composite, H11;
- `docs/re-firstperson.md`, section 5: the game-state flags;
- `docs/re-input.md`, section 1.5: command dispatch;
- `docs/design-vr.md`: the needs G17-G19 and the HUD/theatre plans.

Conventions are as in re-view-render.md: addresses are runtime addresses, `vf_NN` is a vtable slot, and claims are
marked **[high]/[med]/[low]**. Extra marks used here: **[RT]** means a runtime check is needed. **static** means read
from code.

Names used below:
- `shell` = `CGameClientShell*` at `*(void**)0x46a9560`;
- `im` = the interface manager `*(shell+0x80ff8)` (`CInterfaceMgr`, InterfaceMgr.cpp);
- `rt3D` = `shell+0x80e00`;
- `rtUI` = `shell+0x80dc0` (UI rect `shell+0x80dc8..0x80dd4` = `InterfaceWidth/Height`);
- `sp` = the scene-parameter record that `FUN_00a5a820` fills per view (its third argument);
- `ctx` = the render-thread frame context built from `sp` by `FUN_00810e80`.

## 0. Summary

| Question | Answer | Conf. |
|---|---|---|
| Where the HUD and menus are drawn | `FUN_00a11dc0` (HUD pass), once per player view at the end of `FUN_009ee170` (call `0x9ef1e0`) and from the non-world paths. It binds `rtUI` (or `rt3D` if there is no UI target), runs the per-frame UI draws, sends the interface state machine message 9 (draw), then calls `EndRenderTarget(2)` = "mark the UI surface for present" | high |
| HUD in its own texture for a quad layer | **Already there.** When `InterfaceWidth/Height` differ from `ScreenWidth/Height`, the UI surface (`device+0x1b8`, SRV `+0x1e8`) is its own texture. It is cleared to transparent black every pass and is blended over the 3D surface only by the present composite `FUN_00742d50`. So: pick a UI size different from the eye size and copy `device+0x1b8` before the composite. No target swap is needed (that was G18's plan) | high (code), med (clear in the shared case) |
| What `UI.Draw` does | A console **command** (not a cvar). Handler `FUN_00a0d930` -> `FUN_00a0d940`. With one argument it calls `CBaseUI::vf_18(show)` on `im+0x2708` (m_GameUI, `CGameUI`) and on `im+0x2a50` (m_MessageBoxUI). That hides those two Scaleform views; it keeps the menus, videos, fades and overlay quads | high |
| How world-anchored UI is projected | `FUN_00a60920(CPlayerCamera*, worldPos, &onScreen, &behind)` -> `FUN_00d28080`. It uses the **camera object's** transform (`cam+0x338`) and `cam+0x404` (tan of the half FOVs), **not** the per-view `SomView`. Users: nav markers (objective/enemy/captain icons and prompt glyphs), the struggle-QTE UI, and gameplay (`CombatTargetSpheres`). `FUN_009f1d20` (the "HUD items" of re-view-render.md) does nothing: its per-item function is empty | high |
| Screen effects to switch off for VR | Motion blur, DoF, vignette, box blur, sun shafts (radial blur), lens rain, lens flares, camera shake; keep SSAO, bloom, fog and colour grading at first. Switches and per-view/global status in section 3 | high (switches), med (visual effect) |
| Video playback | Bink is emulated as WebM (`CMovieTexture` / `CD3D11MovieTexture`, `CWBMovieMgr` = `IRenderMovieMgr`). Full-screen movies belong to `im+0x2af0`, which is the loading-screen/video element. They are drawn as a 2D quad into the **UI target** by `FUN_00c28220` ("RenderVideo"), called from the HUD pass. While a full-screen video covers the world, `FUN_00a10d30(im)` is true and `FUN_009ee170` skips the 3D view. So the UI surface is the theatre picture | high |
| UI state | Interface MSM `*(*(im)+0xc)` (or `+0x8`): 1 front end, 2 loading, 3 in game, 4 waiting for the world. Menus: `FUN_00a1bd50(im+0x10)` (menu MSM state > 3). Videos: `FUN_00a10d30(im)`. Subtitles: `FUN_00a10670(im, key, channel)` -> `FUN_00b39c40` | high (reads), med (meanings) |

## 1. The Scaleform HUD/menu render path (a)

### 1.1 Objects

`CInterfaceMgr` members, as set up in `CInterfaceMgr::Init` (`FUN_00a17710`, strings "Could not initialize CMenuUI /
m_GameUI / m_MessageBoxUI") and used in the HUD pass **[high for the offsets, med for the names]**:

| Offset | What | Evidence |
|---|---|---|
| `im+0x0` | `CInterfaceMgrStateMachine*` (`MacroStateMachine`: `+0x8` state, `+0xc` pending state or -1) | `FUN_00a10f00`, `FUN_00a0e000` ("MSM InterfaceMgr", "%s Set State: %d -> %d") |
| `im+0x10` | `CMenuUI*` (0x2e58 bytes, `FUN_00a210f0`); it starts with its own `MacroStateMachine` ("MSM MenuUI") | `FUN_00a1bd50`, `FUN_00a1a800` |
| `im+0x2708` | **m_GameUI**, a `CGameUI` (base `CBaseUI`, vtable `0x3597da8`): the in-game HUD view. `+0x8` = its view record (`+0x50` = the `IUIView`, a `CUIView`; `+0xd0` = visible flag), `+0xb0` = an optional full-screen texture | `CBaseUI::vf_07/vf_18`, `CGameUI::vf_07` |
| `im+0x27b0` | `CUIHUDSubtitle*` (`m_GameUI+0xa8`) | `FUN_00a10670`, `FUN_00a0def0` |
| `im+0x2a48` | byte: the game UI is modal. It takes the input and makes the HUD pass draw the 3D UI scene | `FUN_00a15170`, `FUN_00a11dc0` |
| `im+0x2a50` | **m_MessageBoxUI** (`CMessageBoxUI`, vtable `0x35803c8`) | Init, `FUN_00a0db90` |
| `im+0x2ad8` | screen fade (drawn by `FUN_00ae54c0`; "FadeMgr", `UI.ForceFadeIn`) | 7 call sites |
| `im+0x2af0` | loading screen + full-screen video element ("RenderVideo", "VideoSkipPrompt", "UI_LoadingScreen_Loading") | `FUN_00c28220`, `FUN_00c2bc00` |
| `im+0x2ec0` | the 3D UI scene manager (`UISceneCamera` menus): `FUN_00c1e370` | `FUN_00a11dc0` |
| `im+0x31c8` | the "overlay texture": a texture handle, `+0x31d0..+0x31dc` rect, `+0x31e0` "rect still in 0..1" | `FUN_00a11bd0` |
| `im+0x3228` | a 2D element drawn in a 2D viewport by `FUN_00a0da00` unless `im+0x32dd` | `FUN_00c209e0` |
| `im+0x34c0` | a callback-driven element (`FUN_00e539c0`) | |
| `im+0x36b0` | the interface render-to-texture scenes (`FUN_00ad7890`, re-view-render.md 3) | `FUN_00a12420` |
| `im+0x37b0` | byte: the local player index, -1 = none | `FUN_00a12d70` |

Other render interfaces (registered in `_INIT_269`, read from its holder blocks) **[high]**:
- `DAT_046a9250` = `IRenderDrawPrim` (2D quads: vtbl+0x38 = set texture, vtbl+0x90 = draw quad(verts, 1));
- `DAT_046a9248` = `IRenderTextureMgr`;
- `DAT_046a9240` = `IRenderer`;
- `DAT_046a9238` = `IRenderCustomDraw`. It is the object that `FUN_009f23c0` (the "texture budget" of re-view-render.md 3) works on.

### 1.2 Order inside `FUN_00a11dc0(im, Player*, bool beginTarget, bool clear)`

**[high, decompile q1]**
1. If no player is passed and `im+0x37b0 != -1`, it takes the local player.
2. **Menu / modal branch.** If `im+0x2a48` is set or a menu is open (`FUN_00a1bd50(im+0x10)`):
   - `BeginRenderTarget(rt3D)`, then the **3D UI scene** `FUN_00c1e370(im+0x2ec0)` (a full `FUN_009f2550` scene render
     into the 3D target), then `EndRenderTarget(2)`.
   - Otherwise, when the interface state is not 3 (not in game) and `beginTarget && clear`, it clears `rt3D` to
     `0xff000000`.
3. **UI target.** It takes `rtUI` if it exists, else `rt3D`, and calls `BeginRenderTarget(target, &colour, flags,
   &uiRect, 0)` (`renderer->vf_33`, vtbl+0x108).
   - If `rtUI` is a separate object, the clear colour is 0 (transparent black) and the flags are 7 when `clear` is set.
   - If it falls back to `rt3D`, the flags are 0 or 7 depending on the state.
4. Per-frame UI draws into that target:
   - `FUN_00a0da00` (2D element `im+0x3228`) if `im+0x32dd`;
   - **`FUN_00c294a0(im+0x2af0)`**, the loading screen and the full-screen video frame (section 4);
   - `FUN_00e539c0(im+0x34c0)`;
   - `IGameplayVideoMgr` (`DAT_046b5190`) vf_05. This is the console "gameplay recording" manager; slot 5 is empty in
     the base class **[med]**.
5. **Message 9 (draw)** to the interface MSM, with the default handler `CInterfaceMgrStateMachine::vf_03`
   (`0xa19160`):
   - state 0: the fade `FUN_00ae54c0(im+0x2ad8, 1)`;
   - states 1, 2: `FUN_00a0db40`, which runs `m_GameUI->vf_07()`, then `FUN_00a0da80` (`m_MessageBoxUI->vf_07()` and
     `FUN_00a0da00`) or the fade;
   - states 3, 4: `FUN_00a0db90`, which runs `FUN_00ac3ca0(Player+0x2e0)` for every local player (the player UI), then
     `m_GameUI->vf_07()`, `m_MessageBoxUI->vf_07()`, then `FUN_00a0da00`.
6. If the draw returned true: `Begin2DViewport({0,0,1,1})` (vf_40), then `FUN_00a11bd0(im)` (the overlay-texture
   quad), then `EndViewport` (vf_41).
7. `FUN_009f23b0(shell+0x80dd8)` if there is a player, then **`EndRenderTarget(2)`** (vf_34, vtbl+0x110) if
   `beginTarget`.

`CBaseUI::vf_07` (`0xcb2a30`) calls the view's `IUIView::vf_04` = `CUIView::vf_04` (`0x90c830`). That is the Scaleform
**Advance with capture** of the movie: `movie->vtbl+0xc8(dt, 2, true)`. `CGameUI::vf_07` (`0xe408e0`) first draws the
optional full-screen texture `+0xb0` as a 2D quad. It then calls `CBaseUI::vf_07` **[high]**.

**Correction to re-view-render.md 6, step 5:** `FUN_00a11bd0` is not the Scaleform display. It draws one textured
full-screen quad (`im+0x31c8`) through `IRenderDrawPrim` **[high]**.

### 1.3 Where the Scaleform movies are actually drawn

**[high for the chain, low for the caller]**
- Every `CUIView` registers its movie display handle with the UI renderer object at `0x461eca0` (UIRender.cpp). The
  object layout:
  - `+0x8` = `Scaleform::Render::Renderer2D`;
  - `+0x18..+0x20` = the list of `{displayHandle, layerMask}`;
  - `+0x30` = lock;
  - `+0x58` = HAL holder;
  - `+0x68` = `CUIRenderCommandQueue`.
- The registration calls `FUN_0072dac0(ui, &handle, mask)` from `FUN_00914770` (view creation). `FUN_0072e040`
  removes it.
- The render thread creates the HAL (`D3D1x::HAL`, ctor `FUN_01171140` via `FUN_00739680`) and the Renderer2D in
  `FUN_0072d320` ("Scaleform failed to initialize!" / "...to create renderer!").
- **The display is the render-thread command `FUN_0072e1b0(ui, int target, uint layerMask)`.** It runs:
  1. `Renderer2D::BeginFrame` (`FUN_0139ea90`);
  2. HAL begin (`FUN_007398e0`, only for `target == 1`);
  3. for each registered movie whose mask matches: `FUN_012c5ab0` (next capture), then `Renderer2D::Display`
     (`FUN_0139eb10`);
  4. `EndFrame`.

  It draws into whatever render target is bound at that moment.
- The only thing that queues `FUN_0072e1b0` is **`CRendererHandle::vf_105`** (`0x7051b0`, vtable slot address
  `0x335cd80`; a command of 0x30 bytes, `+0x28` = target, `+0x2c` = mask).
- **No caller of vf_105 was found statically.** `tools/vcall.py` lists every virtual call through vtable offset `0x348`
  in the binary, and none of them is on a `CRendererHandle`. Ghidra shows no data reference to the slot either.
- The likely caller is inside the HUD pass, between `BeginRenderTarget(rtUI)` and `EndRenderTarget(2)` (the movies
  must land in the UI surface), reached through a call form the scan does not see **[RT]**.
- Check: hook slot `0x335cd80` and log the return address, `esi` and `edx` once per frame.

`Render.UI.ProfileMode.*` (`_INIT_56`) only writes `0x461ecb0`, which nothing reads **[high]**. `Render.UI.Wireframe`
was not traced.

### 1.4 Resolution, aspect and the UI surface

**[high unless marked]**
- `rtUI` = `vf_62(w, h, 0x4401)` with `w, h` = `InterfaceWidth/Height` (`shell+0x80dc8..0x80dd4`). Flag `0x4000` makes
  it a window onto the device's **UI composite surface** (`device+0x1b8`, SRV `device+0x1e8`, size `device+0x90/0x94`).
  That surface is a separate texture **only when its size differs from the 3D surface** (re-stereo.md 4).
- **Present** (`FUN_00742d50`) blits the marked surfaces to the back buffer through `FUN_007a9e70`, with three cases:
  - only 3D marked: one blit;
  - only UI marked, or both SRVs the same: one blit;
  - both marked: the 3D surface, then the UI blended over it.
- The HUD's aspect follows the UI rect. The Scaleform views keep a **safe-zone inset** at `CUIView+0x1d00..+0x1d0c`
  (four fractions). `CUIView::vf_17/vf_18` return the visible frame rect with and without it. The options page
  `CUIPauseMenuSafeZoneOptionsLayer` sets it; `MenuUI.SkipUserConfig` skips the first-run prompt.
- The world-marker positions are scaled with the **UI** size (`shell+0x80dc8..`, section 2). So markers stay in the
  UI's own pixel space whatever the 3D size is.

### 1.5 The UI into its own texture for an OpenXR quad layer

Best first (all static; the RT items are listed in section 7):
1. **Use the engine's own UI surface.**
   - Set `InterfaceWidth/Height` (`SetResolution <sw> <sh> <iw> <ih>` or the prefs) to the panel size, e.g. 1920x1080,
     different from the eye-size 3D surface.
   - The HUD pass then always draws into `device+0x1b8`, cleared to `0x00000000` per pass.
   - Copy that texture on the render thread right before `FUN_00742d50` (re-stereo H9 at call `0x742ffd`), or hand it
     to the Vulkan layer.
   - The present composite still blends it over the desktop mirror. **[high code, RT: alpha is straight or
     premultiplied, and the clear happens once per frame, not per view.]**
2. **Draw it only once per frame** (eye R would clear and redraw it): skip the call `0x9ef1e0` for eye R (re-stereo H11).
   Every per-frame element, the movie Advance included, then runs once.
3. G18's original plan (swap `shell+0x80dc0` to our own target around `FUN_00a11dc0`) also works but is not needed.

What does **not** go into the UI surface:
- the **3D UI scenes** of menus and the Nemesis screen (`FUN_00c1e370` into `rt3D`, step 2 of 1.2): they are in the
  3D image;
- the world itself during menus (the front-end world, `Interface.FrontEndWorld`).

So for menus the panel needs the 3D surface too, or the menu scene must be rendered into a target of ours (the
`FUN_009f2550` scene view takes any target, re-view-render.md 3) **[med]**.

## 2. World-anchored UI (b)

### 2.1 The projection

```c
// FUN_00a60920 (0xa60920): project a world point for the UI. Returns {u, v} in XMM0 (0..1 of the view), depth z in XMM1.
float2 FUN_00a60920(CPlayerCamera* cam, const float pos[3], bool* onScreen /*rdx*/, bool* behind /*rcx*/);
//   normal:              xf = camera object (cam+0x338)+0x8 (pos[3], quat[4] x,y,z,w, scale), tanHalf = cam+0x404 (tan(fovX/2), tan(fovY/2))
//   Nemesis hierarchy:   xf = (cam->owner+0x310)+0x8d4.., tanHalf = +0x9a8   (FUN_009fa370 true; re-firstperson.md: P+0x310)
//   -> FUN_00d28080(pos, &xf, &tanHalf, onScreen, behind): view-space p = inverse(xf)*pos;
//      u = 0.5 + 0.5*p.x/(p.z*tx), v = 0.5 - 0.5*p.y/(p.z*ty); behind = (p.z <= 0); onScreen = u,v in [0,1] and !behind
// FUN_00a60a30 (0xa60a30): the same, then scaled to pixels with the 3D int rect cam+0x3a0..0x3ac (gameplay users).
```

**[high: decompile q2 plus disassembly]**
- `cam+0x404` is written by `FUN_009f2850(&cam->fov)` in `FUN_00a5c6c0` (lens update). It holds
  `{tan(fovX/2), tan(fovY/2)}` with the FOVs clamped to 0..3.124.
- The projection is symmetric (no centre offset) and uses the **gameplay camera object**.
- So the per-eye `SomView` changes of H1 (re-stereo.md) do **not** reach the markers. They stay in the mono gameplay
  projection, drawn into the single HUD. In VR the camera object is no longer where the eyes are, so the markers sit
  at the wrong place and at panel depth.

### 2.2 Who calls it

**[high for the sites, med for the meanings]**

| Caller | Call site | What | Notes |
|---|---|---|---|
| `FUN_00ec4a50(marker, player)` | `0xec4d7f` | **nav marker per-view update**: projects the marker's world position, scales `u, v` by the UI size (`shell+0x80dc8..`), then places the on-screen icon (`FUN_00ec2660`) or the edge arrow (`FUN_00ec3900` / `FUN_00ec1e80`) | reached from `FUN_00cbd260` -> `FUN_00ec4f10(localPlayer)`, which walks the global list `0x46b9200` (`CNavMarkerInstance`, node at `+0x80`). `FUN_00cbd260` runs from `FUN_009ee170` (`0x9ef00e`), `FUN_009edba0` (cinematic) and `FUN_00c1e370` (UI scene), so **once per view** |
| `FUN_00ec1e80` | `0xec227b` | nav marker edge/secondary placement (from `FUN_00ec2660`, `FUN_00ec3900`) | |
| `FUN_00ed1db0` | `0xed1e58` | struggle sequence UI ("StruggleTarget", "StruggleCursor", "Menu_slideIn_struggleSeq"; StruggleSequenceUIMgr.cpp) | QTE glyph on the enemy |
| `FUN_00c83c70`, `FUN_00c84000` | `0xc83ccb`, `0xc84051` | the same struggle UI (from `FUN_00ed07d0`, `FUN_00ed12a0`, `FUN_00ed1880`, `FUN_00ed3390`) | |
| `FUN_0274ed10` | `0x274f1a1`, `0x274f39a` | **gameplay**: "CombatTargetSpheres" (from `FUN_00a9c8e0` / `FUN_00a9ce70`) | must keep the gameplay camera: do not change the function itself |
| `FUN_00a60a30` users | `0xa389a7` (`FUN_00a38980`), `0x2723474` (`Combat::vf_13`), `0x2791074` | **gameplay** on-screen tests (fight circle, re-gameplay.md 6) | leave alone |

**Nav markers** are the world-anchored UI:
- GameBase `NavMarker` / `CNavMarkerPlugin` objects, records `NavMarkerData`. Its DB loader `FUN_024856e0` reads
  `RenderOffScreen`, `AlphaOffScreen`, `DisplaySecondaryOnScreen`, `OnScreenSecondaryOffset`, the on/off-screen alpha
  and scale fall-offs and ranges.
- Script events `NavMarkerScriptTimerArgs`; prompt glyphs on markers ("Backing", "PCKeyboard", "GlowFX" in
  `FUN_00ebe9f0` / `FUN_00ebedc0` / `FUN_00ec1200`).
- The Nemesis preview characters draw theirs through `CForEachPreviewCharCallback_RenderNavMarkers`.

So objective and waypoint markers, captain/enemy icons and the button glyphs over enemies (counter, interact, drain)
most likely all come through this path **[med; RT: dump the list `0x46b9200` in a fight and at a quest giver]**.

Not world-anchored (screen-fixed Scaleform elements): health/focus/elf-shot (`CUIHUDCombat`), hit streak
(`CUIHUDCombo`), boss/captain bars (`CUIHUDBossInfo`, `CUIHUDCurrentTarget`), `CUIHUDEnemyManager` (its update
`FUN_00f08b80` has no projection call), prompts (`CUIHUDPrompts`, `CUIHUDBehaviorPrompt`), subtitles. They carry no
world position **[med: no call into either projection function]**.

`FUN_009f1d20` (`0x9f1d20`, called at `0x9ef354` with the view) loops over `DAT_046b8580..88` and calls `FUN_00e7a490`,
which is a 1-byte `ret`. It has no effect, which agrees with re-firstperson.md **[high]**. The list's writers were not
traced (`xrefs 0x46b8580` found only this reader).

### 2.3 Making the markers right in stereo

1. **Capture and re-draw in 3D (G19).**
   - Post-hook the UI call sites `0xec4d7f`, `0xec227b`, `0xed1e58`, `0xc83ccb`, `0xc84051` (not the function, because
     combat uses it).
   - Each call gives `cam`, the world position (`rsi`) and the marker (`FUN_00ec4a50`'s `rdi` is the instance, from
     `node-0x80`).
   - Record `{marker, pos, onScreen}` and force `*onScreen = 0` and `*behind = 1`, so the game draws no icon.
   - The overlay renderer then draws an icon per eye at `pos`.
   - The marker's icon/type lives in the instance (`+0x268`, `+0x26a`, `+0x278` are read in `FUN_00ec4a50`).
   - **[med for the mechanism, RT for the field meanings and for "forcing off-screen hides the arrow too"]**
2. **Cheap interim (the HUD on a head-locked or lazy panel).** Re-project with the HMD centre pose and the *panel's*
   angular size instead of the camera.
   - Wrap the call `0xa6099c` (`FUN_00a60920` -> `FUN_00d28080`) only when the return address is one of the UI sites.
   - Replace `xf` with the head pose and `tanHalf` with the panel's half-angles.
   - The icon then points at the right direction on the panel but sits at panel depth. That is acceptable for
     objective markers and wrong for prompts on enemies.
3. Per eye in the HUD is not possible with one HUD draw per frame (H11) **[high]**.

## 3. Screen-space and post effects (c)

### 3.1 Where the passes run

`FUN_007d6e20` (the deferred pipeline per RenderScene, re-stereo.md 3) runs these steps in order **[high for the order
and the pass functions, from the decompile in q2 and the material handles in q3]**:
1. G-buffer and shadows (`FUN_007dc0b0`, which also calls the AO pass `FUN_0079e6f0`);
2. the fog overlay `FUN_007e6820` (`fog_overlay.mat`);
3. HDR/tonemap/exposure `FUN_007ea320` (`HDRToneMap.mat`, `ComputeDynamicExposure.mat`);
4. **overlay layer 4** `FUN_00724690(&DAT_0461e0e0, ctx, 4)`;
5. transparency, OIT and the draw lists;
6. overlay layer 5;
7. object motion blur `FUN_007e7fb0` (if MotionBlurQuality > 2);
8. **final post `FUN_007eb1d0`**;
9. overlay layer 6;
10. the fixed 1.13 x 0.65 rad secondary view.

The material handles live in the table `0x462ce60` (filled by `FUN_007b0820`; each handle is at `0x462ce60 + offset`).
That table maps the passes:

| Material(s) | Offset | Used in | Pass |
|---|---|---|---|
| `SpecDiffOcclusion`, `SSAO_blur*`, `SSAO_blend`, `depth_downscale` | `0x88..0x108` | `FUN_0079e6f0` | SSAO |
| `bloom_decodeanddownsample` ... `bloom_combine` | `0x148..0x1c8` | `FUN_007a1530` | bloom |
| `bloom_blend` | `0x1e8` | `FUN_007a21f0` | bloom blend |
| `radialblur_filter`, `_blur`, `_blurhorizontal`, `_blurvertical` | `0x208..0x268` | `FUN_007a1530` | sun shafts |
| `dof_*` | `0x288..0x2e8` | `FUN_007eb1d0` | DoF (+ box blur + vignette in the same shader) |
| `deferred_motion_blend`, `objmotion_*`, `motion_objectvelocity` | `0x328..0x3a8` | `FUN_007c5020`, `FUN_007c4d10`, `FUN_007e7fb0` | motion blur |
| `fog_overlay` | `0x3c8` | `FUN_007e6820` | depth fog |
| `HDRToneMap`, `LuminanceDownsample`, `ComputeDynamicExposure` | `0x608..0x648` | `FUN_007ea320` | tonemap, eye adaptation |
| `occlusionquery` | `0x6c8` | `FUN_00720da0` | lens flare / sun visibility queries |
| `uvspace_impact`, `uvspace_flow` | `0x708`, `0x728` | `FUN_007324d0` | blood in character UV space |
| `gamma_ramp` | `0x308` | (no reference) | |

`FUN_007eb1d0(pipe, ctx)` runs these steps **[high, decompile q2]**:
1. **bloom and sun shafts** `FUN_007a1530` + `FUN_007a21f0` when `ctx+0xf8` (= `sp+0x60`, the HDR state's bloom enable)
   and the target flag `0x80` are set;
2. **camera motion blur** `FUN_007c5020` (MotionBlurQuality 1-2) or `FUN_007c4d10` (3);
3. **the DoF shader**, when one of these is set:
   - `ctx+0x1ac` (DoF on, and `Render.Setting.DepthofFieldToggle` `DAT_0461e700 == 1`);
   - `ctx+0x1c8` (**box blur**, `k_fBoxBlurParams` from `ctx+0x1cc..0x1d4`);
   - `ctx+0x1dc` (**vignette**: `k_fVignetteParams` = inner/outer/strength from `ctx+0x1e0..0x1e8`, `k_fVignetteColor`
     = the bytes `ctx+0x1ec..0x1ee`).

Where these come from in `sp` (`FUN_00810e80` copies them) **[high]**:
- `ctx+0x1ac..` = `sp+0x114..` (DoF, already in re-view-render.md);
- `ctx+0x1c8..0x1d4` = `sp+0x130..0x13f`;
- `ctx+0x1dc..0x1ee` = `sp+0x144..0x156`;
- `ctx+0xf8` = `sp+0x60`;
- `ctx+0xf0` = `sp+0x58` (motion blur).

`FUN_00a5a5a0` fills the HDR block `sp+0x60..` from `CHDRAtmosphereSystemState` (`FUN_00ca4c00` -> `FUN_00caf8c0`).
The writers of the box-blur and vignette fields (`sp+0x130`, `sp+0x144`) were not found. The candidates are the
camera-FX listeners `FUN_00d8b0f0` (`vtbl+0x18(listener, xf, sp)` on the three FX managers) **[low; RT: log
`sp+0x130` / `sp+0x144` after `FUN_00a5a820` at low health and in wraith mode]**.

### 3.2 Effect table

"Per view" means the effect is computed inside each RenderScene from that view's own camera. Per view is correct for
each eye, but a screen-space effect still differs between the eyes.

| Effect | Pass / data | Switch (static) | Per view or global | In VR | Conf. |
|---|---|---|---|---|---|
| SSAO | `FUN_0079e6f0` (from `FUN_007dc0b0`) | `Render.Setting.AmbientOcclusionQuality 0` (TRenderSetting 0..5) | per view, from that view's depth | mostly fine: each eye gets its own AO; small L/R differences at depth edges. Keep, lower it if it shimmers | med |
| Screen-space reflections | **not in this build**: no SSR material or constant (reflections are cube maps, `CCubeMapTextureUpdater`) | - | - | nothing to do | med |
| Sun shafts / god rays ("radial blur") | inside the bloom pass `FUN_007a1530`: `radialblur_*`, sun position projected with the matrix at `0x4625760`; per-view flag `DAT_046257ac` written by `FUN_007a1f70` (from the RenderScene executor `FUN_007a24d0`) | database: `CHDRAtmosphereSystemState` field `RadialBlurEnabled` (`CHDRAtmosphereSystemState::vf_04` `0xcac9a0` reads it with `RadialBlurExposure`, `...Density`, `...Weight`, `...Decay`, `UseRadialBlurPosition`, `RadialBlurScreenSpace`) via `DBDiff.Apply`; no cvar | per view | **screen-space streaks differ between the eyes** (HelixMod: "sun rays are a bit wrong"). Off at first | high (pass), low (which byte turns it off) |
| Lens flares | ClientFX `CLensFlareFX` (`COcclusionQueryFX`), drawn by `CLensFlareRender::vf_04` (`0x2d6f170`) | no cvar. Hook: make vtable slot `0x3b5de78` point at a `ret` | per view, but the occlusion queries are re-issued per eye (re-stereo.md 3: flicker) | off | med |
| Bloom | `FUN_007a1530` + `FUN_007a21f0` | `sp+0x60 = 0` per view after `FUN_00a5a820` (kills bloom **and** sun shafts) | per view | fine; keep. Watch the middle seam in layout A | high (gate), med (meaning of `sp+0x60`) |
| Vignette | DoF shader in `FUN_007eb1d0` (`k_fVignetteParams/Color`) | `sp+0x144 = 0` (byte) in the H1 post-hook | per view | a screen-fixed vignette in each eye looks like a visor; off, or replace with our own comfort vignette | high (data flow), low (when the game sets it) |
| Box blur (full-screen blur) | same shader, `k_fBoxBlurParams` | `sp+0x130 = 0` | per view | off (blur in VR is nausea) | high (data flow), low (source) |
| Chromatic aberration, film grain | **not in this build** (no material, constant or string) | - | - | - | med |
| Motion blur | re-stereo.md 3 | `Render.Setting.MotionBlurQuality 0` (`DAT_0461e580`; also read by `FUN_00823760/aa0`) or byte `0x46b6651 = 0`; MotionBlurQuality 0 also turns off FXAA | per view, but compares with last frame's centre camera | off (run 1 smear) | high |
| Depth of field | re-stereo.md 3 | `Render.Setting.DepthofFieldToggle 0` (`DAT_0461e700`) or byte `0x46b6652 = 0` (`sp+0x114 = 0`) | per view | off | high |
| Camera shake | re-view-render.md 2 | byte `0x46b72d2 = 1` | per frame (camera) | off | high |
| Lens rain drops | `FUN_00ca53e0(Player+0x438, rt)` from `FUN_009ee170` (call `0x9ef2f3`) -> `FUN_00adeee0` -> `FUN_00ade140` / `FUN_00add700` | skip the call `0x9ef2f3` | **per view** (each eye draws its own screen-space drops) | off | high (site), med (what it draws) |
| Colour grading (weather, time of day, probably wraith) | `CColorGradingAtmosphereSystemState` -> `FUN_00ca5180` -> `FUN_00eca850` ("Overlays.ColorGrading"): a full-screen 2D pass with `tColorGrade3DLUT1/2` and `k_fLUTBlend`, in a 2D viewport `{0,0,1,1}` (`DAT_046b9330`) | none needed | **per view**, called from `FUN_009f2460` after each RenderScene | a LUT is fine in stereo. **Layout A trap:** the static viewport `DAT_046b9330 = {0,0,1,1}` covers the whole target, so eye L's half is graded twice. Set it to the eye's rect between the eyes (H4), or use layout B | high (code), med (the double grading) |
| Fog overlay | `FUN_007e6820` | - | per view, depth based | fine | med |
| Wraith-world tint | `PlayerVisionMode` (a player component with an empty vtable), script events `CPlayerObj.OnWraithEnter/Exit`, `WraithArgs`, `VisionModeToggle` (cmd 243). The look most likely comes from atmosphere controllers (colour grading, HDR, fog states) plus ClientFX | none found | per view (atmosphere states feed `sp`) | the LUT part is fine; any vignette/blur part goes through `sp+0x130/0x144` (switch above) | low (RT: log the active atmosphere states in wraith mode) |
| Full-screen damage / low-health overlays | (1) ClientFX `COverlayFX` (`IClientFXOverlay`; draw `COverlayFX::vf_18` `0x2d7af10` -> `FUN_02d7a700`); (2) the per-weather `CScreenEffectAtmosphereSystemState`, which plays a `CClientFXSequence` (`vf_04` `0xca5bd0`); (3) the vignette/box-blur fields | overlays: `Render.Draw.Overlays 0` (byte `0x461e1d8`, read only by `FUN_00724690`, the overlay layers 4/5/6 of every view; all ClientFX overlays go away, particles included); a narrower hook: `COverlayFX` slot 18 `0x3b624f8` -> `ret 1` | per view | screen-space sprites in each eye feel bad; turn them into a comfort-safe peripheral tint drawn by us | med (classes), low (which effect uses which) |
| Screen-space decals | **none**. Impact decals are world-space (`CImpactDecalFX`, deferred `DecalLightDirectional` `ApplyToGbuffer`); blood on characters is UV-space (`uvspace_impact/flow`) | - | per view | fine | med |
| Eye adaptation | re-stereo.md 3 (H7) | - | per view (twice per frame) | see re-stereo.md | med |
| OIT (deep buffer) | `SortDeepBuffer` / `BlendDeepBuffer` / `StencilDeepBuffer` | `Render.Setting.OITToggle 0` | per view | HelixMod had to turn it off ("double hair"): that was a 3D Vision artefact; with true per-eye views it should be fine **[RT]** | low |
| FXAA | MotionBlurQuality >= 1 (re-view-render.md 4) | MotionBlurQuality 0 | per view | fine without it at VR resolution | med |

### 3.3 Cross-check with the HelixMod 3D Vision fix

Fix by mike_ar69 and bo3b (3Dmigoto, DX11, last updated 2017-05-03). Sources:
[helixmod.blogspot.com](https://helixmod.blogspot.com/2014/11/middle-earth-shadow-of-mordor-dx11-3d.html) and the
mirror at [xanz-mods](https://xanz-mods.blogspot.com/2017/05/middle-earth-shadow-of-mordor-dx11-3d.html?m=1).

3D Vision renders one view and lets the driver shift the vertices per eye. Every full-screen pass that reconstructs
position from depth (deferred lights, shadows, fog) then needs a shader fix. We render two real views with their own
matrices and `k_vFullViewPlane` (re-stereo.md 5), so that class of breakage does not apply. What remains relevant:

| HelixMod note | Here |
|---|---|
| Fixed: shadows, lights, fog, smoke, fire, explosions, "special effects" | per view with true projections: correct by construction **[med; RT: one stereo screenshot]** |
| "HUD is put at screen depth except for cross-hair"; menus at screen depth | sections 1-2: the HUD is one 2D layer (quad panel); markers need the 3D re-draw (2.3) |
| "Sun rays are a bit wrong, but not too bad" | radial blur is screen-space per eye: off at first (table above) |
| Particles wrong "in wraith mode using the bow" and in the benchmark | some particle effect in wraith/bow aim is screen-space or uses a fixed projection **[low; RT: look at wraith bow aim in stereo]** |
| "Disable Order Independent Transparency" (double hair) | probably 3D Vision only; check (table) |
| "Triangle flicker" at the screen edges, "not to do with the fix" | game behaviour; may show at the eye edges too |
| Users turned off motion blur and FXAA with shader overrides; menu cursor glow at wrong depth | motion blur off (re-stereo.md); the cursor glow is part of the menu movie, so on the panel it is at panel depth anyway |
| Bow aiming showed two cursors | the crosshair is a HUD element (`CUIHUDCrosshair`, `CAnimationMgr::UICrosshairModifier`, `WraithPip`): in VR it belongs on the bow/aim ray, not on the HUD |

## 4. Video and cutscene playback (d)

**[high unless marked]**
- **Player.** Bink is emulated: `CMovieTexture` / `CD3D11MovieTexture` (IRenderTexture) decode WebM (nestegg, `.vib`
  -> `.webm`). Its errors read "Failed to open bink file: %s"; the IO thread is `MovieIOThread` (`FUN_007c79b0`). The
  manager is `CWBMovieMgr` (`IRenderMovieMgr`, engine vtbl+0xf0); its vf_26 "RenderCreateTexture" must run on the
  render thread. Shader: `DrawPrimVideo.fxo` with `tDrawPrimVideoY/R/BPlane` and `k_vBinkConstants` (YUV -> RGB on the
  GPU).
- **Full-screen movies** (story cutscenes, intro):
  - `CInterfaceMgr` API `FUN_00a10930(im, record)` -> `FUN_00c2bc00(im+0x2af0, record)` ("Create", "VideoSkipPrompt");
    the debug command `UI.PlayVideo <record>` goes through `FUN_00abe950`.
  - Per frame `FUN_00c294a0(im+0x2af0)` (in the HUD pass, step 4 of 1.2) and `FUN_00c291e0` (in the interface
    pre-render `FUN_00a12420`) call **`FUN_00c28220(videoElem, bool)` = "RenderVideo"**.
  - `FUN_00c28220` asks the movie manager for the movie state (`vtbl+0xa8(handle +0x200)`, 2 = playing). It then
    draws the current frame as a 2D quad through `IRenderDrawPrim` (engine vtbl+0xf8: `+0x60` mode, `+0x98` quad)
    with the rect `+0x1d0..+0x1dc` (letterboxed in UI pixels).
  - Target: the **UI target** (`rtUI` / the UI surface).
- **While a video covers the screen**, `FUN_00a10d30(im)` = `FUN_00c24c30(im+0x2af0)` is true.
  - It checks the video handle `+0x200` and the flags `+0x215`/`+0x216`, then the player record.
  - `FUN_009ee170` then renders **no 3D view** at all: it only calls `FUN_00a11dc0(im, 0, 1, 1)` (call `0x9ef20a`).
- So **the theatre screen = the UI surface while `FUN_00a10d30(im)` is true**, at `InterfaceWidth x InterfaceHeight`.
  Subtitles and the skip prompt are in the same surface **[high]**.
  - Cleaner option: post-hook `FUN_00c28220` and take the movie texture itself (the `CMovieTexture` behind handle
    `+0x200`) for the screen, with the UI surface as an overlay **[med; RT: how to get the texture from the handle]**.
- **Other video users**:
  - `CUIMenuLayerVideo`, `CUIPauseMenuRuneVideoLayer` (menu backgrounds and rune previews): textures inside Scaleform,
    so they appear in the menu panel by themselves;
  - `Movie` / `CMoviePlugin` (in-world movie objects);
  - `CVideoControllerFX` (ClientFX);
  - `IsAnyFullScreenVideoPlaying` (a script query, `FUN_023e3180`).
- **In-engine cutscenes** are the cinematic camera path (`*(shell+0xb0)+0x20`, `FUN_009edba0`, re-firstperson.md 5), a
  3D view, not a video.
- `IGameplayVideoMgr` / `NonSup_GameplayVideoMgr` is the console gameplay-recording manager ("not supported" on PC),
  not playback **[med]**.

## 5. Menu cursor and selection: the UI entry points (e)

Raw devices, bindings and the command ids are in re-input.md. On the UI side **[high for the calls, med for the
meanings]**:
- **Commands into the UI.** `PlayerInput`'s command-on `FUN_00aab660` offers every event `{cmd, value, player}`
  (built by `FUN_00eb8540`) to **`FUN_00a15330(im, event)`** first; true means consumed (re-input.md).
  - Command-off / value paths: `FUN_00a151d0`, `FUN_00a15500`, `FUN_00a15640`.
  - Inside, the interface MSM gets **message 6** (command on) or **7** (off):
    - in game (state 3): `FUN_00a15170` / `FUN_00a154a0`. They go to `m_GameUI` (`FUN_00e3fbf0` / `FUN_00e3fc00`) when
      `im+0x2a48` is set, otherwise to the player UI `FUN_00acb250` / `FUN_00acbfd0` (`Player+0x2e0`);
    - in the front end (state 1): `CMenuUI` through `FUN_00a22ea0` (msg 6) / `FUN_00a22ef0` (msg 7) into the menu MSM
      (`CMenuUIStateMachine::vf_03` `0xa237e0`).
  - The other MenuUI messages (8, 9, 0xb, 0xe, 0xf; `FUN_00a22f40` ... `FUN_00a23080`) come from
    `CGameSystemEventHandler::vf_05..09` (system events), not from the mouse.
- **Menu navigation and activation.** Menu managers (`CUIMenuManagerBase`, 31 slots, and `CUIPauseMenuManager`,
  `CUIMainMenuMenuManager`, `CDebugUIMenuManager`) are `IUINavPaneReceiver`s (nav-pane focus; slot 3 `0xb5d430`).
  - Items are `CUIMenuComponentButton/Checkbox/Cycle/TextEntry` (13 slots each).
  - Keys and pad arrive as commands 44..58 (`MenuUp` ... `MenuDelete`), 79 `MenuExit`, 216..226 (`MoveUp`, `Select`,
    `Back` ...).
- **Mouse.** The movies are AS2. `CUIMovieClip::vf_90..vf_95` attach `onPress`, `onRelease`, `onReleaseOutside`,
  `onRollOver`, `onRollOut` and `onMouseMove` handlers. `vf_108/vf_109` are `hitTestDisable` / `hitTest`.
  - So **Scaleform does its own picking** once the movie gets mouse events. The menu items react in those callbacks.
  - Mouse buttons and wheel also exist as commands (100 `Mouse1`, 101 `Mouse2`, 102/103 wheel, 104/105 axes).
  - **Not found statically:** the call that feeds the cursor position into the `GFx::Movie` (a `MovieImpl`
    `HandleEvent`/`NotifyMouseState` slot). Feral's Win32 layer reads `SDL_GetMouseState` (calls at `0x204654c`,
    `0x20465fc`) and serves `GetCursorPos`.
  - So the laser pointer can drive the stock cursor through SDL (design-vr.md's `SDL_PushEvent` / `SDL_WarpMouseInWindow`
    route) **[RT: that menus follow a warped or pushed mouse while the game window is unfocused or headless]**.
- **Mapping a panel hit to UI pixels.** The UI surface is `InterfaceWidth x InterfaceHeight`, and the movie's visible
  frame is `CUIView::vf_18` (without) or `vf_17` (with the safe-zone fractions `CUIView+0x1d00..0x1d0c`). A hit at
  panel `(u, v)` is UI pixel `(u*iw, v*ih)`. If the window and the UI sizes differ, the OS mouse is in window pixels,
  scaled by the present blit **[med]**.

## 6. UI state detection (f)

Reads (the game thread, any time after the shell exists) **[high for the reads, med for the meanings]**:

| State | Read | Notes |
|---|---|---|
| shell / interface manager | `shell = *(void**)0x46a9560`; `im = *(void**)(shell+0x80ff8)` | |
| interface MSM state | `sm = *(void**)im; s = *(int*)(sm+0xc); if (s == -1) s = *(int*)(sm+8)` | 0: start-up (fade only); 1: front end (`MainMenu_OnEnter` `FUN_00a14290`); 2: loading (message box + fade); **3: in game** (`FUN_00a10f00`); 4: waiting for the world, then switching to the stored target (`sm+0x38`) |
| a menu is up (front end, options, save slots) | `FUN_00a1bd50(im+0x10)`: `m = *(void**)(im+0x10)`; state at `m+0xc` (or `m+0x8`) `> 3` | the HUD pass then draws the 3D menu scene |
| pause menu, map, skills, runes (in game) | the player UI state machine `*(*(P+0x2e0)+0xb0)` (re-firstperson.md 5; "PauseMenu", "menu_pausemenu_enter") and the game timer pause `FUN_009e4770(shell)` | state numbers **[RT]** |
| game UI modal | byte `im+0x2a48` | input goes to `m_GameUI` |
| Nemesis hierarchy screen | `FUN_009fa370(*(P+0x310))` (re-firstperson.md) | its own camera; the markers project with it (2.1) |
| full-screen video | `FUN_00a10d30(im)` (call it; it walks a record) | no 3D view that frame |
| loading screen | interface state 2/4; `im+0x2af0` active (`FUN_00c29830`: "UI_LoadingScreen_Loading", skip prompt) | |
| cinematic camera | `*(*(shell+0xb0)+0x20) != 0` (re-firstperson.md) | |
| fades | `im+0x2ad8` (`FUN_00ae54c0` draws it; `UI.ForceFadeIn`; `FadeMgr.AllowScreenFadeLocks`; "[FADEMGR] Blocked by cinematic" in `FUN_00a10a90`) | a full-screen black quad in the UI surface: fade the XR layers instead **[RT: alpha field]** |
| HUD shown | `UI.Draw`; `CBaseUI::vf_18(show)` sets `view+0xd0` and `IUIView` vtbl+0xa0 | |

State changes are easy to log: every `MacroStateMachine` prints `"%s Set State: %d -> %d"` through its vtbl+0x28
logger. The interface one is `FUN_00a0e000` (name "MSM InterfaceMgr"), the menu one `FUN_00a1a800` ("MSM MenuUI"). A
post-hook on those setters gives the transitions without polling.

**Subtitles and dialogue** **[high for the calls, med for the semantics]**:
- `FUN_00a10670(im, const char* stringKey, int channel)` is **ShowSubtitle**. It hashes the key, looks it up in the
  string database (`*(0x461de78)` vtbl+0x48), then jumps to `FUN_00b39c40(*(im+0x27b0), const wchar16* text, channel)`.
  - Callers: `CSoundEmitterDialog::vf_07` (call `0xd5adf8`), `CSoundEmitterNarrator::vf_07` (tail jump `0xe885a6`),
    `CSoundFXEmitter::vf_07` (tail jump `0xecbc5f`), and the video element `FUN_00c26900` (`0xc26b06`, FMV
    subtitles).
  - Spoken lines therefore come with their audio emitter, which gives a world position for speaker-anchored subtitles
    later.
- `FUN_00a10740(im, channel)` clears a line (`FUN_00b39ee0`).
- `CUIHUDSubtitle` keeps 3 channels of 0x210 bytes at `+0xc0`. Each line is `text|seconds`, up to 256 wchar16.
  `FUN_00b39890` puts a line into the movie clip (`+0x60` vtbl+0x318 = set text).
- The subtitle option is the byte `+0x1f6` of `FUN_00c40c60(shell+0x10)` (checked by `UI.TestSubtitle`,
  `FUN_00a0def0`).
- Conversations as such: `CDialogManager`, script events `OnConversationBegin/End` (re-firstperson.md) **[low]**.

## 7. Hook plan with check data (g)

Style as spike1: call-site rel32 or vtable-slot patches only, every site byte-checked. All of them are static and
read from this build's file with `tools/sites.py`. The two jumps (`K_JMP`) need a new kind in check_sites.py (a
`jmp rel32` site).

| # | Name | Change | Thread | Conf. |
|---|---|---|---|---|
| U1 | `UI_HUD_EYE` | the HUD pass for eye R: skip (re-stereo H11) | game | high |
| U2 | `UI_SURFACE` | before the present composite: copy the UI surface `device+0x1b8` (and the 3D surface) out (re-stereo H9). With `InterfaceWidth/Height` != the 3D size this is the HUD/menu/video layer | render | high (site), med (alpha) |
| U3 | `UI_MARK_*` (5 sites) | post: record `{rdi marker/elem, rsi pos, result}` for the 3D re-draw; optionally force `*onScreen = 0` | game | high (sites), RT (hiding) |
| U4 | `UI_MARK_PROJ` | interim: when the return address of `FUN_00a60920` is a U3 site, swap `xf`/`tanHalf` for the head pose and the panel's half-angles | game | med |
| U5 | `UI_VIDEO_*` | post: the video is playing; take the movie texture or mark "theatre" | game | high (sites), RT (texture) |
| U6 | `UI_VIDEO_COVER` | read the result: true = no 3D this frame (theatre only) | game | high |
| U7 | `UI_SUB_*` | pre: copy `rsi` (`const char* key`) and `edx` (channel); the text arrives at `FUN_00b39c40` | game | high |
| U8 | `FX_SP` (= spike1 H1 site) | in the existing H1 post-hook: `sp+0x130 = 0` (box blur), `sp+0x144 = 0` (vignette); optionally `sp+0x60 = 0` (bloom and sun shafts) | game | high (data flow) |
| U9 | `FX_RAIN` | skip the lens-rain call | game | high |
| U10 | `FX_FLARE` | lens-flare render slot -> a stub that returns | render | med |
| U11 | `FX_OVERLAY` | `COverlayFX` draw slot -> a stub that returns 1 (or `Render.Draw.Overlays 0` for everything) | render | med |
| U12 | `FX_GRADE` (layout A only) | write the eye's rect `{x0,y0,x1,y1}` into `DAT_046b9330` (4 floats, initialised to `{0,0,1,1}` by `FUN_00eca850`'s guard) before each eye, `{0,0,1,1}` after | game | med |
| U13 | `UI_RENDERUI_PROBE` | log the return address, `esi` (target) and `edx` (layer mask) of `CRendererHandle::vf_105` once per frame | game | RT probe |
| U14 | `UI_MSM_*` | post: log state transitions (interface, menu) | game | high |

```c
// SITE(name, kind, addr, ctxaddr, "ctx bytes", target, "target prologue")
SITE("UI_HUD_EYE",      K_CALL, 0x9ef1e0, 0x9ef1cd, "01 00 00 00 ba 01 00 00 00 48 89 de 48 8b b8 f8 0f 08 00 e8 db 2b 02 00", 0xa11dc0, "41 57 41 56 41 55 41 89 d5 41 54 49 89 f4 55"),
SITE("UI_SURFACE",      K_CALL, 0x742ffd, 0x742fea, "00 00 00 00 00 90 41 54 55 89 f5 53 48 89 fb 48 83 ec 20 e8 4e fd ff ff", 0x742d50, "41 54 55 53 48 89 fb 48 83 ec 20 0f b6 05 d6"),
SITE("UI_MARK_NAV",     K_CALL, 0xec4d7f, 0xec4d6c, "48 8d 54 24 2a 4c 89 c7 48 8d 74 24 38 f3 0f 11 54 24 14 e8 9c bb b9 ff", 0xa60920, "41 56 49 89 f6 41 55 49 89 cd 41 54 49 89 d4"),
SITE("UI_MARK_NAV2",    K_CALL, 0xec227b, 0xec2268, "d9 f3 0f 11 6c 24 64 f3 0f 11 74 24 60 f3 0f 11 5c 24 68 e8 a0 e6 b9 ff", 0xa60920, "41 56 49 89 f6 41 55 49 89 cd 41 54 49 89 d4"),
SITE("UI_MARK_STRUG",   K_CALL, 0xed1e58, 0xed1e45, "8b bd d0 02 00 00 48 8d 4c 24 43 48 8d 54 24 42 48 89 ee e8 c3 ea b8 ff", 0xa60920, "41 56 49 89 f6 41 55 49 89 cd 41 54 49 89 d4"),
SITE("UI_MARK_STRUG2",  K_CALL, 0xc83ccb, 0xc83cb8, "8d 4c 24 13 48 8d 54 24 12 48 89 de 48 8b b8 d0 02 00 00 e8 50 cc dd ff", 0xa60920, "41 56 49 89 f6 41 55 49 89 cd 41 54 49 89 d4"),
SITE("UI_MARK_STRUG3",  K_CALL, 0xc84051, 0xc8403e, "24 13 48 8d 54 24 12 48 8b b8 d0 02 00 00 48 8d 6c 24 20 e8 ca c8 dd ff", 0xa60920, "41 56 49 89 f6 41 55 49 89 cd 41 54 49 89 d4"),
SITE("UI_MARK_PROJ",    K_CALL, 0xa6099c, 0xa60989, "4d 89 e8 4c 89 e1 48 8d 54 24 1c 4c 89 f7 48 8d 74 24 30 e8 df 76 2c 00", 0xd28080, "f3 0f 10 46 08 f3 0f 10 26 f3 0f 10 0d af 84"),
SITE("UI_VIDEO_HUD",    K_CALL, 0xc29666, 0xc29653, "30 48 c1 e8 35 85 34 c2 40 0f 95 c6 40 0f b6 f6 48 89 df e8 b5 eb ff ff", 0xc28220, "41 57 41 56 41 55 41 89 f5 41 54 55 53 48 89"),
SITE("UI_VIDEO_PRE",    K_CALL, 0xc293aa, 0xc29397, "ff ff ff 48 85 ff 74 06 48 8b 07 ff 50 18 31 f6 4c 89 e7 e8 71 ee ff ff", 0xc28220, "41 57 41 56 41 55 41 89 f5 41 54 55 53 48 89"),
SITE("UI_VIDEO_COVER",  K_CALL, 0x9ef0a9, 0x9ef096, "e8 85 4b 4a 00 48 8b 05 be a4 cb 03 48 8b b8 f8 0f 08 00 e8 82 1c 02 00", 0xa10d30, "48 81 c7 f0 2a 00 00 e9 f4 3e 21 00 0f 1f 40"),
SITE("UI_SUB_DIALOG",   K_CALL, 0xd5adf8, 0xd5ade5, "77 e7 94 03 4c 89 fe ba 01 00 00 00 48 8b b8 f8 0f 08 00 e8 73 58 cb ff", 0xa10670, "48 8b 05 e9 8e c9 03 41 54 55 89 d5 53 48 8b"),
SITE("UI_SUB_VIDEO",    K_CALL, 0xc26b06, 0xc26af3, "78 0c 00 74 0c 0f b7 50 0e 48 8b 41 10 48 8b 34 d0 31 d2 e8 65 9b de ff", 0xa10670, "48 8b 05 e9 8e c9 03 41 54 55 89 d5 53 48 8b"),
// UI_SUB_NARRATOR (K_JMP 0xe885a6 -> 0xa10670) and UI_SUB_FX (K_JMP 0xecbc5f -> 0xa10670): tail jumps
SITE("FX_SP",           K_CALL, 0x9ee7d4, 0x9ee7c1, "48 8d 94 24 b0 00 00 00 48 8d b4 24 80 00 00 00 48 89 ef e8 47 c0 06 00", 0xa5a820, "41 55 49 89 d5 41 54 55 48 89 f5 53 48 89 fb"),   // = spike1 H1
SITE("FX_RAIN",         K_CALL, 0x9ef2f3, 0x9ef2e0, "00 4c 89 e7 e8 e7 5f 2b 00 84 c0 74 0b 4c 89 f6 4c 89 e7 e8 e8 60 2b 00", 0xca53e0, "48 8b 57 10 48 8b 47 18 48 29 d0 48 c1 f8 03"),
SITE("FX_FLARE",        K_VSLOT, 0x3b5de78, 0x3b5de78, "70 f1 d6 02 00 00 00 00", 0x2d6f170, "41 57 41 56 41 55 41 54 55 53 48 81 ec 98 00"),
SITE("FX_OVERLAY",      K_VSLOT, 0x3b624f8, 0x3b624f8, "10 af d7 02 00 00 00 00", 0x2d7af10, "48 83 bf c0 00 00 00 00 74 26 80 bf e8 00 00"),
SITE("FX_GRADE_CALL",   K_CALL, 0xca528e, 0xca527b, "44 24 18 00 00 00 00 e8 59 ab 00 00 48 8d 7b 48 48 89 e6 e8 bd 55 22 00", 0xeca850, "55 48 89 fd 53 48 89 f3 48 83 ec 28 48 8b 3d"),
SITE("UI_RENDERUI_PROBE", K_VSLOT, 0x335cd80, 0x335cd80, "b0 51 70 00 00 00 00 00", 0x7051b0, "41 54 41 89 f4 55 89 d5 53 e8 02 87 02 00 bf"),
```

No-code switches for the first tests (prefs or console):
- `UI.Draw 0` (HUD and message boxes off);
- `Render.Draw.Overlays 0`;
- `Render.Setting.MotionBlurQuality 0`;
- `Render.Setting.DepthofFieldToggle 0`;
- `Render.Setting.AmbientOcclusionQuality N`;
- `SetResolution <sw> <sh> <iw> <ih>` (the UI surface size).

Data pokes:
- `0x46b72d2 = 1` (shake);
- `0x46b6651 = 0` (player motion blur);
- `0x46b6652 = 0` (DoF);
- `0x461e1d8 = 0` (overlays; this is `Render.Draw.Overlays`).

The pokes are BSS. Check that the module is the right one by BuildID, as spike1 does.

## 8. Runtime checks, corrections and open questions

**Runtime checks** (cheapest first):
1. U13 probe: who queues the Scaleform display (`CRendererHandle::vf_105`), with which target and mask, and whether
   it runs between `BeginRenderTarget(rtUI)` and `EndRenderTarget(2)`.
2. With `InterfaceWidth/Height` != the 3D size, run a dump of `device+0x1b8` at U2 and check four things:
   - the HUD is alone on transparent black;
   - alpha is premultiplied or straight;
   - the video frame shows during a cutscene;
   - menus show the 3D UI scene in the 3D surface only.
3. U3 log in a fight and at a quest marker. Which markers go through which site? Does forcing off-screen hide the icon
   and the edge arrow?
4. `sp+0x130` / `sp+0x144` values (box blur, vignette) at low health, in wraith mode and in a cutscene. Who sets them?
5. The sun-shaft byte: compare a sunny scene with `sp+0x60 = 0` against `RadialBlurEnabled = 0`.
6. Layout A: is eye L double-graded (`FUN_00eca850`)? Do the lens rain and the overlays cover both halves?
7. The pause-menu state numbers of `CPlayerUIStateMachine`.
8. Does a pushed or warped SDL mouse move the menu cursor (laser pointer)?

**Corrections to earlier docs** (static, **[high]**):
- re-view-render.md 6, step 5: `FUN_00a11bd0` draws the overlay-texture quad through `IRenderDrawPrim`
  (`DAT_046a9250`), not the Scaleform display. The display is `FUN_0072e1b0` via `CRendererHandle::vf_105` (1.3).
- re-view-render.md 6: the 3D UI scene is `FUN_00c1e370(im+0x2ec0)` (byte offset). `0x5d8` is the `long*` index
  Ghidra prints.
- re-view-render.md 3 and 6, re-stereo.md 3, design-vr.md (G19): the world-anchored items are **not**
  `FUN_009f1d20` / `DAT_046b8580` (that per-item function is empty, as re-firstperson.md noted). They are the nav
  markers (`0x46b9200`), projected in `FUN_00cbd260` -> `FUN_00ec4f10` -> `FUN_00a60920` with the camera object.
- re-view-render.md 3 (step 8) and 4: `DAT_046a9238` is `IRenderCustomDraw` (`IRenderCustomDraw.Default` holder in
  `_INIT_269`), not a texture manager.
- design-vr.md G18: no target swap is needed. The engine already gives the UI its own surface when the UI and 3D sizes
  differ. design-vr.md theatre "needs RE: which surface the video frame is in": the UI surface (section 4).

**Open questions**:
- The caller of `CRendererHandle::vf_105` (U13).
- Where the cursor position enters the `GFx::Movie` (section 5).
- The writers of the vignette and box-blur fields.
- The wraith-mode look (which atmosphere controller).
- The icon/type fields of `CNavMarkerInstance`.
- Whether the HelixMod "particles in wraith bow aim" issue is a screen-space particle trick that also breaks true
  stereo.
