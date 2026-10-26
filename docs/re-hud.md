# Shadow of Mordor (Feral Linux build): the HUD in VR (H11), the UI scene camera, world markers, prompts

Static analysis only (2026-10-19). Nothing launched, the install not touched. No Ghidra run for this doc (a VR session
was on): `objdump` ranges of `reference/bin/linux/ShadowOfMordor` (`reference/work-ui/tools/fd.sh`), the full listing
`reference/work-ui/full.dis.gz`, the TSV indexes in `reference/index/`, and the decompiles already in
`reference/work-ui/q1-q5.txt`, `reference/work-view/q13-q20.txt`, `reference/work-h8/b10.out`. Scratch:
`reference/work-hud/` (git-ignored): `d_*.txt` (objdump of the functions named below), `tools/mksite.py` (SITE lines
from a spec), `sites.spec` / `sites.txt`.

Builds on `re-ui-effects.md` (sections 1, 2, 6, 7), `re-xr-pipeline.md` 4.6 (layout B, H8), `re-stereo.md` 1, 4, 6,
`re-view-render.md` 3, 6 and `re-firstperson.md` 5. Conventions as there: runtime addresses (non-PIE), `vf_NN` =
vtable slot, **[high]** read in code and unambiguous, **[med]** read in code, meaning inferred, **[low]** inference,
**[RT]** needs a runtime check.

Names: `shell = *(void**)0x46a9560`; `im = *(shell+0x80ff8)` (CInterfaceMgr); `rt3D = shell+0x80e00`; `rtUI =
shell+0x80dc0`; UI rect `shell+0x80dc8..0x80dd4` (InterfaceWidth/Height); `dev = 0x461fa40` (the engine D3D device
struct: `+0x1b8` UI surface texture, `+0x1c0` 3D surface texture, `+0x90/+0x94` UI size, `+0xa0` UI format,
`+0xd8/+0xdc` 3D size, `+0xe8` 3D format); `P` = the local Player; `PUI = P+0x2e0` (CPlayerUI); eye 0/eye 1 = the two
H4 calls of `FUN_009ee170` in a stereo frame.

## 0. Summary

| Item | Answer | Conf. |
|---|---|---|
| 1. Where the HUD is drawn | `FUN_00a11dc0` (the HUD pass) right after each eye's `EndRenderTarget(2)` (`0x9ef1bf`), i.e. **after H8 copied that eye**, into `rtUI` = the device's UI surface `dev+0x1b8`, cleared to `0x00000000` per pass. The Scaleform movies are drawn inside it by `CUIMgr::vf_09` -> `CRendererHandle::vf_105(1, mask)` twice (masks 3, then 0x38). The HUD never reaches an eye image | high |
| 1. HUD into its own texture | The engine already makes one: with `InterfaceWidth/Height` != the eye size the UI surface is its own texture (RGBA, bind RT\|SR). Plan: **skip the HUD pass for eye 0** (keep the last), **CopyResource the UI surface at the present composite** (`0x742ffd`, render thread) into our `uiTex`, publish its VkImage like the eye images, the layer copies it into an XR quad swapchain. No target swap (G18) | high (path), med (format, alpha: RT) |
| 1. Mono vs. stereo frames | Stereo only on the in-world player path (H4). Front end, loading, in-game modal UI (`im+0x2a48`), cinematic camera and "no player" frames render **one** view (or none) and **one** HUD pass outside H4; full-screen videos skip the 3D inside H4. Table 1.3 | high |
| 2. UI scene camera | `CUISceneMgr` `im+0x2ec0`, drawn by `FUN_00c1e370` only from the HUD pass's menu/modal branch, into `rt3D`. Its view is a 0x328-byte scene view on the stack (`rsp+0x50`), filled from a `UISceneCamera` level object (`FUN_02851eb0`: object pos/quat, `FOV` deg, `FarZ`, `DOF*`; fovX from the **UI** aspect), rendered by `FUN_009f2550` at `0xc1ee09`. H1/H4 never see it; H2/H3/H8 leave it alone. The **Nemesis hierarchy is not a UI scene**: it is the player view with the `CHierarchyMenuMgr` camera (`FUN_00dbf3f0`), which H1 does see | high |
| 3. World markers | `CNavMarkerInstance` list `0x46b9200` (node `+0x80`), walked per view by `FUN_00ec4f10` (call `0xec4f83` per marker -> `FUN_00ec4a50`). World position = `FUN_00ebc700(m, &pos)` (target object or bone + offsets). Icon/type = the `NavMarkerData` record `m+0x298` (name `FUN_025aa530`, attributes `IconMovieClip`/`IconMovieClipPC`/`IconTexture`/`IconSize`/`ActionID`, key offsets in 3.4). Hide = the game's own hide path after `FUN_00ec4a50` | high (data, sites), med (hide), RT (icon strings) |
| 4. Prompts | All HUD button prompts go through two event casters (PromptShow `0x46b6bb0`, PromptHide `0x46b6bc8`) into three `CUIHUDPrompts` (Standard/Quicktime/Keyboard, `hud+0x60/+0x68/+0x70`), each holding a `CUIPromptDiamond` (`+0x1d8`) with the live entries `{key, action id, StringId, IconId, category}`. Read them by polling the diamonds or hook the delegate slots. `UI.PrintHUDPrompts` / `UI.PrintHUDElementList` print **nothing** in this build. `Combat.Debug.Prompt` (`0x4c44c18`) makes the game log every combat prompt change | high (structure), med (field meanings) |

**Status 2026-10-20:** item 1 (H11) is **built offline** (nothing launched): `SOMVR_HUD=quad` in somvr.so and the layer's
HUD quad, re-xr-pipeline.md section 8 (1.7 below). Items 2-4 are RE only.

## 1. H11: the HUD in VR

### 1.1 Where the UI is drawn in a stereo frame (layout B)

Per eye, inside `FUN_009ee170` (re-xr-pipeline.md 4.6.1, re-view-render.md 3) **[high]**:

1. H1 view (`0x9ee7d4`), nav markers `FUN_00cbd260` (`0x9ef00e`, section 3), visibility request;
2. eye 0 only (`first`): interface pre-render `FUN_00a12420`: video tick `FUN_00c291e0`, interface render-to-texture
   `FUN_00ad7890` (own target), **Scaleform frame begin** `CUIMgr::vf_08` (`0xa124c6`) -> `vf_105(target 0, mask
   0xffffffff)`. With target 0 the render-thread command `FUN_0072e1b0` only runs `Renderer2D::BeginFrame/EndFrame`
   (`FUN_007398e0` returns 0 unless target == 1): **no movie is drawn there** **[high]**;
3. `BeginRenderTarget(rt3D)` `0x9ef105`, vf_37 (H2/H3), RenderScene with the whole post chain, `EndViewport`;
4. **`EndRenderTarget(2)` `0x9ef1bf` -> H8 copies the eye** (render thread, at that command);
5. **HUD pass `FUN_00a11dc0(im, P, 1, 1)` at `0x9ef1e0`**:
   - menu or modal (`FUN_00a1bd50(im+0x10)` or `im+0x2a48`): `BeginRenderTarget(rt3D, colour 0, clear 7)` (`0xa11e7d`),
     the 3D UI scene `FUN_00c1e370` (`0xa12237`, section 2), `EndRenderTarget(2)` (`0xa11eb9`);
   - `BeginRenderTarget(rtUI, colour 0x00000000, flags 7 when clear)` (`0xa11f80`; `rt3D` only if `rtUI` is null);
   - the 2D fade-in element (`im+0x3228`, `FUN_00a0da00`) if `im+0x32dd`, the loading/video element
     `FUN_00c294a0(im+0x2af0)`, `FUN_00e539c0(im+0x34c0)`, `IGameplayVideoMgr` vf_05;
   - message 9 to the interface MSM. In game (states 3, 4) that is `FUN_00a0db90` (`0xa12210`): the player UI
     `FUN_00ac3ca0(PUI)`, `m_GameUI->vf_07` and `m_MessageBoxUI->vf_07` (Scaleform Advance + capture), then
     **`CUIMgr::vf_09(1)` = `vf_105(1, 3)`** (`0xa0dc39`), `FUN_00a0da00` (here when `im+0x32dd` is clear),
     **`CUIMgr::vf_09(0)` = `vf_105(1, 0x38)`** (`0xa0dc66`). Front end (states 1, 2): `FUN_00a0db40` (`0xa12170`)
     -> the same pair from `FUN_00a0da80` (`0xa0dab4`, tail jump `0xa0dae6`), or only the fade;
   - `Begin2DViewport` + the overlay-texture quad `FUN_00a11bd0` (`im+0x31c8`) + `EndViewport`;
   - **`EndRenderTarget(2)`** (`0xa121f9`): marks the UI surface for present.
6. eye 1: the same, `first = 0`. Then present: the composite call `0x742ffd` -> `FUN_00742d50` blends the UI surface
   over the 3D surface into the back buffer (`FUN_007a9e70`: both SRVs into one pixel shader, slot 0 = 3D, slot 1 = UI).

So the HUD is drawn **after** each eye's H8 copy, never into an eye image, into the UI surface (or, when the UI and
3D sizes are equal, into the shared surface after the copy: run 4's "no HUD in either eye") **[high]**.

`CUIMgr` (vtable `0x347bcf8`, from engine vf_16 `0x8bd510` -> `*0x467ba28`): `vf_08` (`0x905800`) = `vf_105(0, -1)`,
`vf_09(bool)` (`0x905830`) = `vf_105(1, b ? 3 : 0x38)`, both tail jumps on `*0x467ba48` (= the `CRendererHandle`
`0x461df60` of run 3), gated by the byte `0x467ba20` **[high]**. This answers re-ui-effects.md 1.3's open caller
(U1): the display is queued inside the HUD pass, between `BeginRenderTarget(rtUI)` and `EndRenderTarget(2)`. The
movies a mask selects are the `{handle, mask}` pairs registered at view creation (`CUIFile::vf_05` `0x909b30` ->
`FUN_00914770(view, .., ecx mask1, r8d mask2)` -> `FUN_0072dac0`); which movie has which layer bit is data
(mask 4 and bits >= 6 are never displayed by these two calls) **[med, RT]**.

Run 3's "3 calls per frame in the world" are exactly these: `target=0 mask=0xffffffff ra=0xa124c9`, `target=1
mask=0x3 ra=0xa0dc3c`, `target=1 mask=0x38 ra=0xa0dc69` (the return addresses vf_105 sees through the tail jumps).
Layout B without H11 has five (one begin + two per eye). Front-end display calls (through `FUN_00a0da80`) return to
`0xa0dab7` and, through its tail jump, to `0xa0db65` in `FUN_00a0db40` **[high, prediction for the `ui` word]**.

**Runtime result (P7, 2026-10-20, `logs/boot-check-20261020-001117`; runtime-probes.md P7 results).** Holds exactly: in
the world `0xa124c9`/0/all, `0xa0dc3c`/1/3, `0xa0dc69`/1/0x38 per frame; at the main menu (with its 3D scene)
`0xa124c9`, `0xa0dab7`/1/3, `0xa0db65`/1/0x38; all `this=0x461df60`, game thread (U1 done).

### 1.2 The UI surface: size, format, clear

**[high unless marked]**
- `FUN_007414b0` creates both device surfaces with `CreateTexture2D` (`dev+0` vtbl+0x28): W x H, 1 mip, 1 layer, 1
  sample, usage DEFAULT, **bind 0x28 (RT | SR)**, misc 0, plus an RTV and an SRV each. 3D: size `dev+0xd8`, format
  `dev+0xe8` -> `dev+0x1c0` (RTV `+0x1d8`, SRV `+0x1f0`). UI: size `dev+0x90`, format `dev+0xa0` -> `dev+0x1b8` (RTV
  `+0x1d0`, SRV `+0x1e8`). **If the two sizes are equal** (compared as one 8-byte value), the UI pointers are aliases of
  the 3D ones (refcount +1): one texture.
- Formats: the 3D surface is 28 = `DXGI_FORMAT_R8G8B8A8_UNORM` (run 4, `EYES made 1`); the UI format is the separate
  field `dev+0xa0`, not measured **[RT; expect 28]**. In Vulkan: `CVulkanTexture` (`*(tex+0x80)`), VkImage at
  `+0x108`, R8G8B8A8_UNORM = VkFormat 37, as for the eyes (re-xr-pipeline.md 4.6.2).
- `rtUI = vf_62(iw, ih, 0x4401)` is a window (flag 0x4000) onto `dev+0x1b8`. The HUD pass clears it with colour 0 and
  flags 7 when `clear` is set, because `rtUI` and `rt3D` are different objects even when they share a texture
  (`FUN_00a11dc0`, `bVar8 = ~-(clear == 0) & 7`). In the shared case the 3D pixels survive (run 4's desktop shot),
  so BeginRenderTarget must not clear a shared UI window; not traced **[med]**.
- Alpha: the composite blends in a pixel shader (`FUN_007a9e70`: shader `+0x40` when a UI SRV is bound, `+0x70` is the
  blend state), so straight vs. premultiplied alpha is not visible in code **[RT, U2]**. Scaleform drawing "over" into
  a cleared transparent target normally leaves premultiplied colour **[low]**.
- After `FUN_00a0db90`, `CGameClientShell::vf_02` may draw a debug UI (`im+0x20`) into `rtUI` without a clear when engine
  vtbl+0x1e0 says so (console overlay) **[med]**: it lands in the HUD texture too.

**Runtime result (P7).** Holds: the UI format `dev+0xa0` is **28** like the 3D surface's; at equal sizes one texture
(`same=1`), with a 1832x1031 or 1832x1920 3D size the UI surface is its own 1920x1080 texture (re-resolution.md 1.5's
result). Open: straight or premultiplied alpha (U2's CPU read; planned through H11b and the layer, U9).

### 1.3 Mono frames and stereo frames

Who renders what, per frame type (`CGameClientShell::vf_02` `0x9eabd0`, `CGameModeClientHydra::vf_02` `0x9ef900`)
**[high for the branches, med for the names]**:

| Frame | Condition | 3D | HUD pass | In VR |
|---|---|---|---|---|
| **in world** (play, pause menu, map, skills, Nemesis hierarchy) | interface state 3, `im+0x2a48 == 0`, a player, no cinematic camera | `FUN_009ef390` -> **H4** -> `FUN_009ee170` x2 (stereo) | `0x9ef1e0` once **per eye** | eyes = projection layer, HUD = quad layer(s) from `uiTex` |
| full-screen video | `FUN_00a10d30(im)` (`0x9ef0a9`) inside each H4 call | none (no H8 copy: the layer reuses) | `0x9ef20a` per eye, `(im, NULL, 1, 1)` | theatre: `uiTex` is the picture (re-ui-effects.md 4) |
| cinematic camera | `*(shell+0xb0)+0x20 != 0` | `FUN_009edba0`: one view, **no H4** | `0x9ee14f` once | mono: theatre / window (design 6) |
| in game, **modal UI** (which screens set it: RT; the load screen's `modal=1` of run 3 goes through the loading row) | `FUN_00a10d10(im)` = `im+0x2a48` (`0x9ebad5`) | **none**: the world is not rendered at all; only the 3D UI scene if one is loaded | `0x9ebb45` once, `(im, NULL, 1, 1)` | mono menu panel |
| front end / loading | interface state != 3 (`FUN_00a10f00` false, `0x9eb69f`) | none (the 3D UI scene, if any) | `FUN_00a12400` (`0x9eb703`) = `(im, NULL, 1, 1)` once | mono menu panel |
| in game, no player | game mode without a player | clear `rt3D` | `0x9efab5` per player | mono |

In the mono frames the 3D surface holds either opaque black (`0xff000000`, not modal, no menu, state != 3) or a
transparent clear plus the 3D UI scene (menu/modal branch); both are marked for present and composited **[high]**.
The 3D UI scene is rendered with the **UI** aspect (section 2.2) into the 3D surface, so it only looks right when shown
at the UI aspect, as the desktop composite does.

### 1.4 Getting the HUD into a texture of its own

Options, best first:
1. **The engine's UI surface + a copy at the present** (recommended). Set `InterfaceWidth/Height` to the panel size
   (e.g. 1920x1080 via `SetResolution sw sh iw ih` or the prefs); with layout B's tall eye size the sizes differ anyway.
   - **H11a (game thread)**: in a stereo frame, skip the HUD pass of **eye 0** at `0x9ef1e0` and `0x9ef20a`; keep
     eye 1's. Reasons: the pass then runs where it runs in the flat game (after the last view); a menu's 3D UI scene
     (drawn into `rt3D` by the pass) survives to present instead of being overwritten by eye 1's world; Scaleform
     Advance runs once per frame (twice would double-step the movies' `dt` **[med]**). Skipping eye R (the current
     wording of design-vr.md H11) also gives a correct HUD texture; it only loses the in-world 3D UI scene case.
   - **H11b (render thread)**: pre-hook the present composite call `0x742ffd` (re-ui-effects.md U2; somvr's H9 is the
     Present slot `0x4503048` right after it, also usable: the composite only samples the UI surface). If
     `*(dev+0x1b8) != *(dev+0x1c0)`: GetDesc, (re)make `uiTex` with the same desc when it changed,
     `CopyResource(uiTex, *(dev+0x1b8))` on the immediate context (slot 47, as `et_copy`; Feral's tracking does the
     layout barriers); publish its VkImage through the same chain and checks as `et_publish` (re-xr-pipeline.md
     4.6.2: resource `+8` -> `+0x78` -> `*(+0x80)` CVulkanTexture -> `+0x108`) with a generation. In the present
     tag: `ui = 1`, the frame kind of 1.3 (world, video, mono, modal), the UI rect. **[high: the same objects and
     slots as the verified eye copy]**
   - **Layer**: one XR swapchain of the UI size (VkFormat 37 source -> R8G8B8A8_SRGB, raw copy, as the eyes), copied
     in the same submit as the eyes; one `XrCompositionLayerQuad` for the visor, or several quads whose
     `XrSwapchainSubImage.imageRect` cut tiles out of the one image (the body tiles of design-vr.md 7: no extra
     copies; mind `maxLayerCount`). Flags: `BLEND_TEXTURE_SOURCE_ALPHA`, plus `UNPREMULTIPLIED_ALPHA` if U2 shows
     straight alpha.
   - Mono frames: also copy `*(dev+0x1c0)` into `menuTex` there; the menu panel = two quads at the same pose and size
     (the 3D surface opaque behind, `uiTex` in front), both shown at the UI aspect. Until then the layer's existing mono
     quad of the presented image works, but must use the UI rect's aspect, not the window's.
2. **Redirect `rtUI`** (fallback if the sizes cannot differ): swap `shell+0x80dc0` around the HUD pass for a target of
   our own from `vf_62(iw, ih, flags without 0x400)`. `EndRenderTarget(2)` then marks nothing for present (the desktop
   loses the HUD), and the RenderTarget -> IdxD3D11 texture mapping of a non-screen target is not traced **[low]**.
3. The back buffer: it already holds UI blended over the 3D surface: unusable for the in-world HUD **[high]**.

Not in the HUD texture: the 3D UI scenes (in the 3D surface), markers we hide (section 3). In it but unwanted in VR:
the screen fade `im+0x2ad8` (`FUN_00ae54c0`: black quad: fade the XR layers instead), the overlay-texture quad
`im+0x31c8`, the fade-in element `im+0x3228`, the safe-zone inset (`CUIView+0x1d00`: set it to 0 for a full-size
HUD texture) **[med]**.

**Runtime result (P7, U10).** `im+0x2ad8..+0x2aef` at the menu and in the world, with no fade running, holds two
pointers (`0x7f104d580280`, `0x7f104dd1fc20`) and zeros: not a fade value at this offset (the fade state is behind
them or elsewhere). Open: the same dump during `UI.ForceFadeIn`.

### 1.5 Sites

```c
// HUD pass call sites (FUN_00a11dc0(im, player, beginTarget, clear))
SITE("HUD_PASS_EYE", K_CALL, 0x9ef1e0, 0x9ef1cd, "01 00 00 00 ba 01 00 00 00 48 89 de 48 8b b8 f8 0f 08 00 e8 db 2b 02 00", 0xa11dc0, "41 57 41 56 41 55 41 89 d5 41 54 49 89 f4 55"),   // H11a: skip for eye 0
SITE("HUD_PASS_COVER", K_CALL, 0x9ef20a, 0x9ef1f7, "b9 01 00 00 00 ba 01 00 00 00 31 f6 48 8b b8 f8 0f 08 00 e8 b1 2b 02 00", 0xa11dc0, "41 57 41 56 41 55 41 89 d5 41 54 49 89 f4 55"), // H11a: skip for eye 0
SITE("HUD_PASS_CINE", K_CALL, 0x9ee14f, 0x9ee13c, "b9 01 00 00 00 ba 01 00 00 00 31 f6 48 8b b8 f8 0f 08 00 e8 6c 3c 02 00", 0xa11dc0, "41 57 41 56 41 55 41 89 d5 41 54 49 89 f4 55"),  // mono (cinematic)
SITE("HUD_PASS_NOWORLD", K_CALL, 0x9efab5, 0x9efaa2, "ba f8 0f 08 00 48 8b 70 f8 b9 01 00 00 00 ba 01 00 00 00 e8 06 23 02 00", 0xa11dc0, "41 57 41 56 41 55 41 89 d5 41 54 49 89 f4 55"),
SITE("HUD_PASS_SHELL", K_CALL, 0x9ebb45, 0x9ebb32, "48 8b bb f8 0f 08 00 b9 01 00 00 00 31 f6 ba 01 00 00 00 e8 76 62 02 00", 0xa11dc0, "41 57 41 56 41 55 41 89 d5 41 54 49 89 f4 55"), // in-game modal
SITE("HUD_PASS_FRONTEND", K_CALL, 0x9eb703, 0x9eb6f0, "c0 01 00 00 48 89 c7 e8 24 85 4a 00 48 8b bb f8 0f 08 00 e8 f8 6c 02 00", 0xa12400, "b9 01 00 00 00 ba 01 00 00 00 31 f6 e9 af f9"),
SITE("SHELL_INGAME_CHECK", K_CALL, 0x9eb69f, 0x9eb68c, "e9 db f7 ff ff 0f 1f 80 00 00 00 00 48 8b bb f8 0f 08 00 e8 5c 58 02 00", 0xa10f00, "48 8b 17 8b 42 0c 83 f8 ff 0f 44 42 08 83 f8"),
SITE("SHELL_MODAL_CHECK", K_CALL, 0x9ebad5, 0x9ebac2, "bb 18 10 08 00 00 0f 84 3a fc ff ff 48 8b bb f8 0f 08 00 e8 36 52 02 00", 0xa10d10, "0f b6 87 48 2a 00 00 c3 0f 1f 84 00 00 00 00"),
// inside the HUD pass (indirect calls: the slot plus the call site's bytes)
SITE("HUD_BEGIN_UI_RT", K_VSLOT, 0x335cb40, 0xa11f76, "1c 48 8d 74 24 20 48 8b 04 24 ff d0", 0x7034d0, "41 57 49 89 f7 41 56 49 89 d6 41 55 41 89 cd"),
SITE("HUD_BEGIN_3D_MENU", K_VSLOT, 0x335cb40, 0xa11e73, "48 8d 74 24 30 48 8b 44 24 08 ff d0", 0x7034d0, "41 57 49 89 f7 41 56 49 89 d6 41 55 41 89 cd"),
SITE("HUD_END_UI_RT", K_VSLOT, 0x335cb48, 0xa121f3, "48 8b 10 48 89 c7 ff 92 10 01 00 00", 0x704eb0, "55 89 f5 53 48 89 fb 48 83 c3 08 48 83 ec 08"),
SITE("HUD_DRAW_INGAME", K_CALL, 0xa12210, 0xa121fd, "00 00 48 83 c4 48 5b 5d 41 5c 41 5d 41 5e 41 5f c3 66 90 e8 7b b9 ff ff", 0xa0db90, "53 48 89 fb 48 83 ec 10 48 8b 35 c1 b9 c9 03"),
SITE("HUD_DRAW_FRONTEND", K_CALL, 0xa12170, 0xa1215d, "47 01 00 00 83 fe 04 77 57 ff 24 f5 10 72 4e 03 0f 1f 00 e8 cb b9 ff ff", 0xa0db40, "53 48 8b 47 10 48 89 fb 48 8d b8 08 27 00 00"),
// the Scaleform display (CUIMgr::vf_09 / vf_08 -> CRendererHandle::vf_105)
SITE("SF_DISPLAY_LAYERS_A", K_VSLOT, 0x347bd40, 0xa0dc30, "00 00 00 48 8b 10 48 89 c7 ff 52 48", 0x905830, "80 3d e9 61 d7 03 00 74 27 48 8b 3d 08 62 d7"),
SITE("SF_DISPLAY_LAYERS_B", K_VSLOT, 0x347bd40, 0xa0dc5d, "00 31 f6 48 8b 10 48 89 c7 ff 52 48", 0x905830, "80 3d e9 61 d7 03 00 74 27 48 8b 3d 08 62 d7"),
SITE("SF_FRAME_BEGIN", K_VSLOT, 0x347bd38, 0xa124bd, "00 00 00 48 8b 10 48 89 c7 ff 52 40", 0x905800, "80 3d 19 62 d7 03 00 74 1f 48 8b 3d 38 62 d7"),
SITE("SF_DISPLAY_CMD", K_VSLOT, 0x335cd80, 0x335cd80, "b0 51 70 00 00 00 00 00", 0x7051b0, "41 54 41 89 f4 55 89 d5 53 e8 02 87 02 00 bf"),
// H11b: pre-hook of the present composite (somvr's H9 = the Present slot 0x4503048 just after it):
// copy the UI surface (in mono frames also the 3D surface)
SITE("UI_PRESENT_COMPOSITE", K_CALL, 0x742ffd, 0x742fea, "00 00 00 00 00 90 41 54 55 89 f5 53 48 89 fb 48 83 ec 20 e8 4e fd ff ff", 0x742d50, "41 54 55 53 48 89 fb 48 83 ec 20 0f b6 05 d6"),
```

### 1.6 Risks

- The UI format `dev+0xa0` and the alpha mode are unmeasured (U2).
- `SetResolution` with a UI size != screen size must keep the UI rect, the Scaleform stage and the mouse mapping
  consistent (re-ui-effects.md 5) **[RT]**.
- Skipping eye 0's pass: anything in the pass that eye 1's 3D needs? Nothing found (`FUN_009f23b0`, the pass's last
  call, is an empty function) **[med]**.
- Fades and full-screen overlays inside the UI surface (1.4) need their own VR handling.
- If `rtUI` is null (never seen; `FUN_009e6d20` always makes it) the HUD falls back to `rt3D` **[med]**.

### 1.7 Status (2026-10-20, offline; nothing launched)

- **H11a built**: `HUD_PASS_EYE` and `HUD_PASS_COVER` (the sites of 1.5, byte-checked, `check_sites.py` 61/61) skip eye
  0's pass in layout-B stereo frames, eye 1's runs; flat frames and other layouts pass through.
- **H11b built**: `UI_PRESENT_COMPOSITE` pre-hook copies the UI surface into our HUD texture (made once at the UI size
  and format, again on a desc change) when the present's stereo frame had its eye-1 pass; the VkImage is published in a
  third exported object `somvr_hud` (v2 `somvr_shared` had no room: re-xr-pipeline.md 8.2), named per present tag.
- **Layer built**: the HUD image in its own XR swapchain, copied in the eyes' submit, a second layer
  `XrCompositionLayerQuad` (world-locked by default, 1.2 m wide at 1.5 m, 0.1 m below eye level; `SOMVR_XR_HUD=head`;
  straight alpha by default, `SOMVR_XR_HUD_ALPHA=premul`); every HUD failure turns the HUD off, stereo goes on.
- Not built (later steps): the mono frames' `menuTex` / menu panel (1.4 "Mono frames"), the video theatre from the HUD
  image (`HUD_PASS_COVER` frames are copied with kind video but have no eye images, so the layer reuses), the body
  tiles, hiding the screen fade / overlay quad / safe-zone inset, `HUD_PASS_CINE` / `_NOWORLD` / `_SHELL` /
  `_FRONTEND` (mono, untouched).
- **Open**: U2 (the UI format `dev+0xa0`: the `HUD made` line logs it; the alpha mode: by eye in the headset, re-xr-
  pipeline.md 8.5), U9 (the first layout-B HUD boot).

## 2. The UI scene camera (3D UI scenes)

### 2.1 Who renders a 3D UI scene

- `CUISceneMgr` (vtable `0x3542998`, a `CUIUpdateElement`) at `im+0x2ec0`. `+0xd0` = the loaded UI scene level
  (unloaded by `FUN_00c1de00`: engine vtbl+0x48, `+0x128`), `+0xf8` = the scene's render context (engine vtbl+0x70
  object, vtbl+0xb0(obj, +0xf8, &view)), `+0x80` = the current `UISceneCamera`, `+0xb8` = the next one, blend: `+0x240`
  start, `+0x248` duration, `+0x250`/`+0x260` alt clocks, `+0x268`/`+0x269` flags **[high offsets, med names]**.
- API callers: the front-end menu MSM (`CMenuUIStateMachine::vf_03` `0xa237e0` -> `FUN_00a1af70`, `FUN_00a1b320`),
  `m_GameUI` (`0xe3fce0` -> `FUN_00b48630`, `FUN_00b47140`), pause/front-screen layers (`FUN_00f81630`, `FUN_00f835a0`,
  `FUN_00f83f70`, `FUN_00f97de0`), the hierarchy menu (`FUN_00db2e40`, `FUN_00db44b0`, `FUN_00db4680` ->
  `FUN_00c1efc0` = set the camera with a blend: writes `+0x80`/`+0xb8`/`+0x240..+0x268`) **[high calls, low which menu
  shows what]**.
- **Drawn only by the HUD pass's menu/modal branch** (`0xa12237`, or `0xa120ef` without a target begin), into `rt3D`
  bound at `0xa11e7d` with a transparent clear **[high]**. In play (pause menu: run 3 `modal=0 menus=0`) it does not
  run; on the load screen (`modal=1`) and in the front end it does when a scene is loaded **[med]**.

### 2.2 The view record

`FUN_00c1e370` builds a 0x328-byte scene view on its stack at `rsp+0x50` (the same record `FUN_009f2550` takes, re-
view-render.md 3) and renders it only if fovY > 0 **[high]**:

| Offset | Field | Source |
|---|---|---|
| `+0x00` | pos x, y, z (cm) | the camera object's position: `*(cam+8)` `+0x8..+0x10` |
| `+0x0c` | quat x, y, z, w | the object's `+0x14..+0x20` |
| `+0x1c` | fovX (rad) | `FUN_00d2be80(aspect, fovY)` = `2 atan(aspect * tan(fovY/2))`, **aspect = UI rect w/h** (`shell+0x80dd0-0x80dc8` / `0x80dd4-0x80dcc`, `0xc1e760`) |
| `+0x20` | fovY (rad) | attribute `FOV` (degrees) x pi/180 (`0x3402a28`); < 0.001 -> a default |
| `+0x28` | viewport rect `{0,0,1,1}` | `DAT_0342e920` |
| `+0x40`, `+0x44` | near, far | `FUN_00d28f80()` default; `FarZ` attribute |
| `+0x50` | scene params (atmosphere) | `FUN_00ca4c00(cam+0x28, view+0x50)`: the camera's own atmosphere states |
| `+0x164`, `+0x168..+0x17c` | DoF on; six floats from the `DOFNear/Far*` attributes and one more (`+0x17c`, default 14) **[med which is which]** | `UISceneCamera` attributes |
| `+0x318` | `cam+0x28` | |
| `+0x325` | visibility flag byte (vf_107's argument) | |

Fill: `FUN_02851eb0(UISceneCamera*, view, aspect)` at `0xc1e871`; between two cameras `FUN_02852e20(cam, view,
nextCam)` at `0xc1ec9b` with the blend factor from `FUN_023b9490`. Without a camera or a scene (`+0x80`/`+0xf8` null)
a fixed default is built (pos `(0, 300, -500)`, fovY 0: nothing drawn) **[high]**. `UISceneCamera` is a level object
(`GameBase`, vtable `0x39b81a8`, plugin `UISceneCameraPlugin`; tag `Interface.UISceneCameraTag`).

Then, in order: `FUN_00cbd260(im+0x2708, view)` (`0xc1ede2`: the interface's view copy; nav markers still project
with the **player** camera), `FUN_009f23c0`, **`FUN_009f2550(renderer, view)` at `0xc1ee09`** (vf_107, vf_37,
vf_43 RenderScene, vf_41), `FUN_009f2460` overlay scenes (`0xc1ee26`) **[high]**.

**Runtime result (P7, U8).** Holds: the main menu loads a UI scene (`CUISceneMgr` `0x7f104da4e1f0`, a level, a camera
`+0x80`, the next one at `+0xb8`, blend start 39.07 s, duration 6, flags 0/1) and `0xc1ee09` renders it with
`renderer=0x461df60`: pos (-3580.0, 754.4, -1134.9), quat (-0.026, 0, 0, 1) (3 degrees down), **fovY 12.00 degrees
(`FOV`), fovX 21.17 = 2 atan(16/9 tan 6 degrees)** from the UI aspect as above, near 0.25, far 100000,
DoF on; the same values after the scene was rebuilt. Consequence seen at the tall size (re-resolution.md 1.5): with
the 3D surface at 1832x1920 and the UI rect still 1920x1080, the menu scene renders **squeezed sideways** into the
portrait target (its fovX keeps 16:9). For a menu scene in an eye, the eye's projection (H3) or the eye aspect must
replace the UI aspect here. Open: the load screen (not covered), which other menus load one.

### 2.3 Interaction with H1-H4 and H8

**[high from code (somvr.c as of 2026-10-19)]**
- **H1** (call `0x9ee7d4` in `FUN_009ee170`) is never on this path: the UI scene view is built in `FUN_00c1e370`.
- **H4** (the player loop) is not on the mono paths (front end, loading, modal) that normally draw UI scenes. If a
  CMenuUI menu were open in play, the scene would run inside each eye's HUD pass; with H11a only eye 1's pass draws it,
  after eye 1's copy, so it reaches the desktop and `menuTex`, never an eye image.
- **H2** (vf_37 slot): sees the scene's call (return address `0x9f258e`); somvr changes the rect only for `ra ==
  0x9ef2bc` or an eye's xf hash: untouched.
- **H3** (render thread): the scene's xf hash is not in the FIFO and differs from the last eye: untouched.
- **H8**: the scene's `EndRenderTarget(2)` comes after the eye's end consumed the H3 match: no copy.
- **G2** (gameplay camera = head) does not affect the scene (it uses its own camera).

### 2.4 Not a UI scene: the Nemesis hierarchy

`FUN_00a5a820` (the H1 target) checks `FUN_009fa370(*(cam->owner+0x310))` at `0xa5a841`; when the
`CHierarchyMenuMgr` is active it fills the player's SomView from `FUN_00dbf3f0(hier+0x8b8, P, view, sp)` (tail jump
`0xa5a9b4`) instead of the gameplay camera, and the world path renders it **[high]**. So H1 post-processes the
hierarchy camera's view: with `SOMVR_BASE=camera` the eyes sit at that camera plus head motion (yaw = its heading); with
`SOMVR_BASE=player` (the design's anchor) they would show Talion's surroundings instead. Rule for M2/M8: while
`FUN_009fa370(*(P+0x310))` is true, run H4 mono (one call, the flat Nemesis screen on the menu panel, design-vr.md 7);
the stereo "stand in the screen" variant takes the base from the game's view **[med]**. The nav markers in that screen
project with the hierarchy camera (`FUN_00a60920`, re-ui-effects.md 2.1). The hierarchy code also sets a UI scene
camera (`FUN_00c1efc0`, 2.1); in play that scene is not drawn (no menu/modal branch), so it matters only if the
hierarchy is shown from a mono path **[med; RT]**.

### 2.5 A stereo UI scene (stretch, after M9)

Replace the call `0xc1ee09`: for eye e, copy the view, move `pos` by `R(view.quat) * (s * eye_e)` (the HMD eye
offset in the camera's frame, `s` = diorama scale), set fovX/fovY to the eye's covering FOV, push `{hash, proj, rt3D}`
into the H3 FIFO, call `FUN_009f2550`; between the eyes issue `vf_34(2)` (H8 copies eye 0) and `vf_33(rt3D, clear)`;
the pass's own `EndRenderTarget(2)` at `0xa11eb9` ends eye 1. The tag then carries `eyes = 3` on a mono path
**[med: reuses H3/H8 as built; RT]**. Do the overlay scenes (`0xc1ee26`) per eye too.

```c
SITE("UI_SCENE_MENU", K_CALL, 0xa12237, 0xa12224, "28 14 04 48 7e 4e 03 e9 fd fe ff ff 48 8d bb c0 2e 00 00 e8 34 c1 20 00", 0xc1e370, "48 83 bf d0 00 00 00 00 0f 84 62 08 00 00 55"),
SITE("UI_SCENE_NOBEGIN", K_CALL, 0xa120ef, 0xa120dc, "e9 02 fd ff ff 0f 1f 80 00 00 00 00 48 8d bb c0 2e 00 00 e8 7c c2 20 00", 0xc1e370, "48 83 bf d0 00 00 00 00 0f 84 62 08 00 00 55"),
SITE("UI_SCENE_CAM_FILL", K_CALL, 0xc1e871, 0xc1e85e, "08 0f 11 83 98 00 00 00 f3 0f 10 44 24 04 48 8d 74 24 50 e8 3a 36 c3 01", 0x2851eb0, "41 54 49 89 fc 55 48 89 f5 53 48 83 ec 20 48"),
SITE("UI_SCENE_CAM_BLEND", K_CALL, 0xc1ec9b, 0xc1ec88, "48 8d 74 24 50 48 8b 93 b8 00 00 00 48 8b bb 80 00 00 00 e8 80 41 c3 01", 0x2852e20, "41 56 49 89 d6 41 55 49 89 fd 41 54 55 48 89"),
SITE("UI_SCENE_MARKERS", K_CALL, 0xc1ede2, 0xc1edcf, "48 8d 74 24 50 48 8b b8 f8 0f 08 00 48 81 c7 08 27 00 00 e8 79 e4 09 00", 0xcbd260, "55 48 89 fd 53 48 89 f3 48 83 ec 08 48 8b 3d"),
SITE("UI_SCENE_RENDER", K_CALL, 0xc1ee09, 0xc1edf6, "a4 03 48 8b 07 ff 90 d8 00 00 00 48 8d 74 24 50 48 89 c7 e8 42 37 dd ff", 0x9f2550, "55 48 89 fd 53 48 89 f3 48 83 ec 08 48 8b 07"),  // rsi = the view (2.2)
SITE("UI_SCENE_OVERLAYS", K_CALL, 0xc1ee26, 0xc1ee13, "a4 03 48 8b 07 ff 90 d8 00 00 00 48 8d 74 24 50 48 89 c7 e8 35 36 dd ff", 0x9f2460, "55 48 8d 8e c0 02 00 00 48 8d 56 50 48 89 fd"),
SITE("HIER_CHECK", K_CALL, 0xa5a841, 0xa5a82e, "fb 48 83 ec 08 48 8b 47 08 4c 8b a0 10 03 00 00 4c 89 e7 e8 2a fb f9 ff", 0x9fa370, "8b 47 0c 83 f8 ff 0f 44 47 08 85 c0 74 5a 83"),
```

Risks: which menus load a UI scene (the main menu's Talion, character/rune pages) is data **[RT]**; the scene's DoF
fields (`+0x164`) must be zeroed for VR like the player's; a diorama scale that is not 1:1 needs the user's OK.

## 3. World markers: positions, icons, hiding

### 3.1 The list and the per-view update

`FUN_00cbd260` (per view: `0x9ef00e` in `FUN_009ee170`, `0xc1ede2` in the UI scene, `FUN_009edba0` for cinematics) ->
`FUN_00ec4f10(localPlayer)`: if `FUN_00a36880(P)` (a player body), for each node of the list head `0x46b9200`
(first `*(0x46b9208)`, next `node+8`, instance = `node - 0x80`): **`FUN_00ec4a50(m, P)` at `0xec4f83`**, then
`FUN_00ebd200` + `FUN_00ebedc0` (rebuild the icon) when `m+0x2ea` != the current pad/keyboard mode
(`FUN_00cbe650`) **[high]**. In layout B it runs twice per frame with the same result (it projects with the gameplay
camera, not the eye view).

`FUN_00ec4a50(m, P)` **[high, decompile q2 + objdump]**:
1. timer `m+0x278` expired -> `FUN_00ec0ef0(m, 0)`;
2. needs `m+0x2e0 && m+0x2e1 && m+0x298`, and one of `m+0x260/+0x268/+0x270/+0xb0`; skips markers whose target is the
   player's own body (`FUN_00a36860(P)`) and the marker the player UI owns (`FUN_00ac11e0(PUI)`);
3. `FUN_00ebc700(m, &pos)` (the world position, 3.3); distance player -> pos (`FUN_00ad1610(P)`) / 100 inside the
   range `FUN_00ebced0(m)` (metres);
4. `RenderOffScreen` (NavMarkerData, 3.4) and the global bit `0x46b9120 & 0x40` pick on-screen-only or edge arrows;
5. **projection** `FUN_00a60920(*(P+0x2d0), &pos, &onScreen, &behind)` at `0xec4d7f` (camera object `cam+0x338`,
   `cam+0x404`), u, v times the UI rect size;
6. place: `FUN_00ec2660` (on screen: scale/alpha fall-offs, LOS ray `IEnginePhysics` vtbl+0x200) or `FUN_00ec3900`
   (edge arrow); they set the shown bytes `m+0x45` (primary) / `m+0x6d` (secondary);
7. if not placed (or out of range, inactive): **hide**: `m+0x45 = 0`, `m+0x6d = 0`, and `clip = *(m+0xb0)`:
   `clip->vtbl[0x208/8](clip, 0)` (Scaleform clip visible = false).

### 3.2 Per-item data (`CNavMarkerInstance`)

| Offset | What | Conf. |
|---|---|---|
| `+0x80` | list node (`+0x88` next) | high |
| `+0xa0` | `IUIMovieClipRef` of the icon; its clip at `+0xb0` (vtbl+0x208 = set visible) | high (type), med (role) |
| `+0x108`, `+0x170` (clip `+0x180`) | two more clip refs (secondary icon, distance/arrow) | med |
| `+0x45`, `+0x6d` | bytes: primary / secondary shown this frame | med |
| `+0x210`, `+0x230` | target object (`+0x230` first) | high |
| `+0x250` | parent marker (the position comes from the root) | high |
| `+0x260`, `+0x268`, `+0x270` | ref-counted objects (released in `CNavMarkerInstance::vf_00`); one of them (or `+0xb0`) must exist | med |
| `+0x278` | timer (`FUN_02390e20`) | med |
| `+0x27c..+0x284` | fixed world position (no target) | high |
| `+0x288..+0x290` | world offset | high |
| `+0x298` | **the `NavMarkerData` record** (type, icon, ranges) | high |
| `+0x2d0` | node index on the target, -1 = object origin | high |
| `+0x2e0`, `+0x2e1` | active, enabled | med |
| `+0x2ea` | icon built for the pad (vs. keyboard) | med |

Combat prompt markers are a larger variant (0x310-byte entries in a vector of `PlayerCombat`, walked by `FUN_00e079d0`
from the combat update `FUN_00a8e120`): the same `+0x230`/`+0x298` fields plus `+0x2f8` (the prompt key record),
`+0x300` (current `Combat.Prompt` record), `+0x308` (u16 action id), `+0x30a..+0x30c` flags (section 4.4) **[med]**.

### 3.3 The world position

`FUN_00ebc700(m, float out[3])` (K_ENTRY-safe, game thread): walk `m = m->+0x250` to the root, then `FUN_00ebc530`
**[high]**:

```
obj = m+0x230 ? m+0x230 : m+0x210
if obj:   base = (m+0x2d0 != -1) ? IEngineModel(*0x46a9230)->vtbl[0xb8/8](obj, m+0x2d0, &xf, 1).pos   // bone
                                 : obj+0x8..0x10                                                     // origin
else:     base = m+0x27c..0x284
pos = base + m+0x288..0x290 + NavMarkerData.WorldOffset (vec3, 0 if absent)
```

### 3.4 Icon and type: the NavMarkerData record

The type = the record `R = *(m+0x298)`, its name `FUN_025aa530(R)` (DBRecord_GetName). Attribute keys live in the
table `T = *(void**)0x4b66f20`; key k = `T + 8 + 0x30k`: `+0` name, `+0x10` mode (<= 1: look up by name,
`FUN_00ebc410`/`FUN_009ec610`), `+0x20` index byte. Read as the game does: `e = *(R+8) + idx*16`; present if
`*(u16*)(e+0xc)`; value slot `*(*(R+0x10) + *(u16*)(e+0xe)*8)` (float inline for type 2, pointer to 3 floats for type
8 = vec3, bools as bits: slot `vi>>5`, bit `vi&31`; type = `*(u8*)(e+0xa) & 0x3f`) **[high for the keys, verified
against `FUN_00ec4a50` (`RenderOffScreen` `+0x338`), `FUN_00ec2660` (`IconSize` `+0x398`, `DisplayInLOS` `+0x4b8`,
`DisplayNotInLOS` `+0x4e8`) and `FUN_00ebc530` (`WorldOffset` `+0x8`); med for string values]**. From the loader
`FUN_024856e0`:

| Key (name at `T+`) | Attribute | Use in VR |
|---|---|---|
| `0x8` | `WorldOffset` | already in the position |
| `0x38`, `0x68` | `InclusiveStates`, `ExclusiveStates` | when the game shows it |
| `0x98` | `AttachNode` | the bone (resolved into `m+0x2d0`) |
| `0x128` | `IconTexture` | texture name: candidate for our own icon art |
| `0x158` | `ActionID` | the command a prompt marker asks for (section 4) |
| `0x188`, `0x1b8`, `0x1e8` | `GrabTexture`, `SecondaryTexture`, `DisplaySecondaryOnScreen` | |
| **`0x218`, `0x248`** | **`IconMovieClip`, `IconMovieClipPC`** | the Scaleform symbol drawn (pad / keyboard): the icon id |
| `0x278`, `0x2a8` | `DistanceFormat`, `DistanceScale` | distance text |
| `0x2d8`, `0x308` | `FlashUseWorldTransform`, `WorldSize` | real-size icons |
| `0x338`, `0x368` | `RenderOffScreen`, `AlphaOffScreen` | edge arrows |
| `0x398`, `0x3c8`, `0x3f8` | `IconSize`, `SecondaryIconSize`, `OnScreenSecondaryOffset` | |
| `0x4b8`, `0x4e8`, `0x518` | `DisplayInLOS`, `DisplayNotInLOS`, `DisplayRange` | |
| `0x608..0x788` | on/off-screen alpha and scale fall-offs/ranges, `DistanceAlpha` | |
| `0x938` | `CombatPromptPriority` | |

**Runtime result (P7, U3').** `navdump` at the tower top (292 markers) and in a camp (295): records `Explosive` 87,
`Ammo` 79, `Firepit` 37-38, `LockedGate` 24, `InfantryNormal` 11-15, `NPC` 11-13, `HangingBait` 8, `RessurrectionStone`
5, collectibles, `Troll` + `TrollHealthBar`, `PlayerLocal`, fast-travel and quest records. Holds: string attributes
read as C strings. `IconTexture` is the icon on most (265-267: `interface\textures\hud\interactibles\break.tex`,
`...\questicons\new\collectibles\ammo.tex`, `flame.tex`, `lock.tex`, `warg.tex`, `barrows.tex`, ...); `IconMovieClip`
is set on one only (`TrollHealthBar`: `SPH-HealthBar`), `IconMovieClipPC` on none; `ActionID` is one value
(`0x886d5ca8`) on every marker; `RenderOffScreen` 0 on all; the orcs' `InfantryNormal`/`NPC` markers have no icon
string (drawn by other code). Shown: none at the tower, one in the camp (the troll's, 81 m). Open: the hide (G19), a
quest giver, positions for all markers (the probe asked 31-32).

### 3.5 Capture and hide (G19)

- **Post-hook the call `0xec4f83`** (`rdi` = marker, `rsi` = P): after the original, if `m+0x45 | m+0x6d` (shown this
  frame), record `{m, R, FUN_025aa530(R), pos = FUN_00ebc700(m), target m+0x230/+0x210, node m+0x2d0}`; then, with
  VR markers on, run the game's own hide (3.1 step 7). Same frame, before the HUD pass: no flicker **[med; RT that
  nothing re-shows it later in the frame]**. Hide in every call; record once per frame (eye 1's call).
- Forcing `*onScreen = 0` at `0xec4d7f` (re-ui-effects.md U3) hides on-screen-only markers but still draws the edge
  arrow when `RenderOffScreen` is set **[high]**; the post-hook is the complete hide.
- Icon art, options: (a) our own glyph per record name (a table filled from the `navdump` probe) **[recommended
  first]**; (b) let the game draw each icon into a reserved atlas cell of the HUD texture (overwrite the projected
  u, v at `0xec4d7f` with the cell centre, keep `onScreen`) and cut the cell for the 3D quad: the real art with its
  state, distance and pad glyph **[low: the stage must have room outside the visible HUD]**; (c) `IconTexture` loaded
  ourselves **[low]**.
- Struggle QTE glyphs project through their own sites (`0xed1e58`, `0xc83ccb`, `0xc84051`, re-ui-effects.md 2.2).

```c
SITE("NAV_FRAME", K_CALL, 0x9ef00e, 0x9eeffb, "00 00 f3 0f 11 84 24 40 03 00 00 4c 89 a4 24 38 06 00 00 e8 4d e2 2c 00", 0xcbd260, "55 48 89 fd 53 48 89 f3 48 83 ec 08 48 8b 3d"),
SITE("NAV_EACH", K_CALL, 0xec4f83, 0xec4f70, "48 89 dd 4c 8b 63 08 48 83 c5 80 74 2a 4c 89 ee 48 89 ef e8 c8 fa ff ff", 0xec4a50, "41 57 41 56 41 55 41 54 49 89 fc 55 53 48 89"),  // G19: post = capture + hide
SITE("NAV_PROJ", K_CALL, 0xec4d7f, 0xec4d6c, "48 8d 54 24 2a 4c 89 c7 48 8d 74 24 38 f3 0f 11 54 24 14 e8 9c bb b9 ff", 0xa60920, "41 56 49 89 f6 41 55 49 89 cd 41 54 49 89 d4"),   // = UI_MARK_NAV
SITE("NAV_POS", K_ENTRY, 0xebc700, 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f", 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f"),                                               // called by us
SITE("NAV_POS_RESOLVE", K_ENTRY, 0xebc530, 0xebc530, "55 48 89 fd 53 48 89 f3 48 83 ec 38", 0xebc530, "55 48 89 fd 53 48 89 f3 48 83 ec 38"),
```

Risks: the string encoding of `IconMovieClip` **[RT]**; markers in cinematics and the hierarchy (projected with other
cameras) should stay hidden or flat; markers on the player (`FUN_00a36860(P)`) are skipped by the game too.

## 4. Prompts: which prompt is up for which action

### 4.1 The debug commands print nothing

`UI.PrintHUDPrompts` (handler `0xb088b0`, registered in `_INIT_301`): `hud = FUN_00ac29d0(PUI)` = `*(PUI+0xb0) +
0x16e0`, then for `hud+0x60`, `+0x68`, `+0x70`: `FUN_00b22500` -> `FUN_00c0b7b0(*(x+0x1d8))`, a 1-byte `ret`.
`UI.PrintHUDElementList` (`0xb08860` -> `FUN_00eeb590(*(hud+0x10), 1)`) formats `"%*s-%*s Active: %d Enabled: %d
Visible: %d AllowVisible: %d TransitionOut: %d Frame: %d"` into a stack buffer and recurses into the children
(`+0x10..+0x18`), but never outputs it **[high]**. They still give the structure: the HUD element tree (element
`+0x28` name record, `+0x40` movie clip (vtbl+0x2c0 frame), `+0xc8`/`+0xc9`/`+0xca`/`+0xbd`/`+0xbb` state bytes), and
the three prompt elements (`FUN_00b08a50`): `hud+0x60` **StandardPrompts**, `+0x68` **QuicktimePrompts**, `+0x70`
**KeyboardPrompts** (`CUIHUDPrompts`, vtable `0x3515d18`) **[high]**.

### 4.2 The event path

- Two global event casters: **PromptShow** list `0x46b6bb0`, **PromptHide** list `0x46b6bc8` (also PromptHoldUpdate,
  PromptAllowInFMV, InputDeviceChanged; `CCharacterProxy::PlayerEvents`). Raisers: the combat prompt switch
  `FUN_00e05ea0`, `FUN_00e052f0`/`FUN_00e05660` (PlayerCombat), `FUN_00e4fe70` (spirit stone / forge tower actions),
  `FUN_00a62230` (presentation identify/skip prompts), `FUN_00dff100`, `FUN_00e0cf60`, `FUN_00e0db80`, `FUN_00ecbf20`,
  `FUN_00ecdc20`, `FUN_027a4a40`, `PlayerBody::vf_08` **[high refs, low roles]**.
- **PromptShowParams** (built at `rsp+0x30` in `FUN_00e05ea0`, read by the delegate `0xb231d0`): `+0x0` key (8 bytes,
  a record: the prompt's source), `+0x8` action id (int; command name `FUN_025aa580(id)`, 349 = none), `+0x10`
  StringId (`const char*`), `+0x18` IconId (`const char*`), `+0x20` category (bit index), `+0x24..+0x26` flags.
  PromptHideParams: `+0x0` key, `+0x8` action **[high layout, med names: from the log format "ShowPrompt %s, Action =
  %s, StringId = %s, IconId = %s"]**.
- Each `CUIHUDPrompts` subscribes one delegate per event (vtables `0x3515bc8` show, `0x3515bf8` hide, `0x3515c58` hold;
  invoke = slot 2; `+0x18` = the CUIHUDPrompts). Show: if bit `params+0x20` of `prompts+0x1e0` is set (the element's
  categories; set by `FUN_00b22510`), `diamond = *(prompts+0x1d8)`, `diamond->vf_02(key, action, string, icon,
  category, flags...)`, then a visibility update `FUN_00eee370`. Hide: `diamond->vf_03(key, action)` **[high]**. So a
  show event reaches all three elements; only the one with the category bit keeps it.
- Behaviour-state text prompts are a second channel: `CPlayerUI` event `BehaviorStatePromptParams` -> delegate
  `0x351cdd0` slot 2 (`0xb3ccd0`): `{+0 bool show, +8 text}` -> `FUN_00e55380(*(textMgr+0x1e8), text)` **[med]**.

### 4.3 The live state: the prompt diamond

`CUIPromptDiamondGamePad` (vtable `0x353fb78`) or `...Keyboard` (`0x353fbb8`) at `prompts+0x1d8`. Slots at
`diamond+0x18`, count `+0x20`, 0x90 bytes each (`slot+0` = the layout record from `FUN_00a13940(im)`); entries in the
vector `slot+0x70..+0x78`, 0x38 bytes each **[high layout (`FUN_00c0b290`, `vf_02` `0xc0e8f0`), med meanings]**:

| Entry offset | Field |
|---|---|
| `+0x00` | key (the PromptShow key) |
| `+0x10` | StringId |
| `+0x20` | IconId |
| `+0x28` | **action id** (int; 349 `0x15d` = free entry) |
| `+0x2c` | category |
| `+0x30..+0x32` | flags |

**Read** (game thread, G1, any time): for each of `hud+0x60/+0x68/+0x70`, for each slot and entry with `action !=
349`: a prompt is up for that action. The pad diamond resolves the bound button itself (`FUN_00aaaf50` on the player's
input, `vf_02` after the store), so the VR side only needs the action id. `FUN_00c0b290(diamond)` = "any prompt up".

### 4.4 Combat prompts (counter, stun, drain over orcs)

`FUN_00e05ea0(entry, P, Combat.Prompt record, bool)`: per combat prompt marker (3.2), when the record or the flag
changes: PromptHide for the old `{entry+0x2f8, entry+0x308}`, then PromptShow with the record's StringId, IconId and
category, `entry+0x300 = record`, `entry+0x30a = flag`; with **`Combat.Debug.Prompt`** (bool `0x4c44c18`, "Prompt
messages") it logs `[COMBAT|PROMPT]: ShowPrompt <record>, Action = <command>, StringId = .., IconId = ..` and
`HidePrompt ..` through the combat logger `FUN_02751a50` **[high]**. The entry is also the marker over the attacker
(re-combat.md 3.3: `NormalNavMarker` etc.), so its `FUN_00ebc700` position is where a VR hint goes **[med]**.

**Runtime result (P7, U7).** Not answered: `prompts` found 0 live entries at the tower and in the camp, the watch saw
no change in 110 s, and `Combat.Debug.Prompt` (the variable's `+0x38` = `0x4c44c18` went 0 -> 1 -> 0) printed nothing
anywhere in the run's files, because no combat prompt came up (no orc attacked Talion). Next: the same words in a
fight the orcs start.

**Runtime result (P8, U7, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results).** The
orcs attacked Talion (74 counter prompts at him), and the counter prompt is **not a prompt-diamond entry**:
- at 71 of the 74 prompts the diamonds held 0 live entries;
- at the other 3 they held only `UI_DropToHang` (CombatPrimary03, a ledge context prompt), which also turned on and off
  6 times in the watch;
- what the player sees is the icon over the attacker's head (shot 48 in KBM mode: the right-mouse glyph in a white
  flare);
- `Combat.Debug.Prompt 1` printed no `[COMBAT|PROMPT]` line anywhere in the run, and the probe's console-output capture
  (`CONOUT`) got 0 lines from every exec.
So the VR counter hint has to come from the combat marker entries of 4.4 (`FUN_00e05ea0`'s record and flag, the
`NormalNavMarker` position) or straight from the orc's window bytes (`Combat+0xe9`, the BPW/EPW key times;
re-combat-vr.md 3), not from the diamonds.

### 4.5 VR hints

- Poll the diamonds at G1 (no hooks), or hook the delegate slots / diamond slots below for events. Map action id ->
  VR hint (`Drain`, `Activate`, the CombatPrimary/Secondary ids of re-input.md); place it at the target (combat entries,
  prompt nav markers via `ActionID`) or at the hand.
- The key is a record, not the target object: tying a non-combat prompt to an object needs the raiser's context
  **[RT]**.
- Hold prompts: PromptHoldUpdate gives the fill **[med]**.

```c
SITE("PROMPT_SHOW_HUD", K_VSLOT, 0x3515bd8, 0x3515bd8, "d0 31 b2 00 00 00 00 00", 0xb231d0, "55 53 48 83 ec 08 48 8b 5f 18 48 85 db 74 71"),
SITE("PROMPT_HIDE_HUD", K_VSLOT, 0x3515c08, 0x3515c08, "40 31 b2 00 00 00 00 00", 0xb23140, "55 53 48 83 ec 08 48 8b 5f 18 48 85 db 74 39"),
SITE("PROMPT_HOLD_HUD", K_VSLOT, 0x3515c68, 0x3515c68, "b0 30 b2 00 00 00 00 00", 0xb230b0, "48 8b 4f 18 48 89 d0 48 85 c9 74 1c 8b 52 08"),
SITE("PROMPT_DIAMOND_PAD_SHOW", K_VSLOT, 0x353fb88, 0x353fb88, "f0 e8 c0 00 00 00 00 00", 0xc0e8f0, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("PROMPT_DIAMOND_PAD_HIDE", K_VSLOT, 0x353fb90, 0x353fb90, "70 ee c0 00 00 00 00 00", 0xc0ee70, "41 54 41 89 d4 55 48 89 fd 53 48 8b 07 48 89"),
SITE("PROMPT_DIAMOND_KB_SHOW", K_VSLOT, 0x353fbc8, 0x353fbc8, "50 0f c1 00 00 00 00 00", 0xc10f50, "41 57 49 89 cf 41 56 45 89 ce 41 55 49 89 f5"),
SITE("PROMPT_BEHAVIOR_TEXT", K_VSLOT, 0x351cde0, 0x351cde0, "d0 cc b3 00 00 00 00 00", 0xb3ccd0, "48 8b 47 18 48 85 c0 74 27 80 3a 00 48 8b b8"),
SITE("COMBAT_PROMPT_SET", K_ENTRY, 0xe05ea0, 0xe05ea0, "4c 8b 87 00 03 00 00 49 39 d0 0f 84", 0xe05ea0, "4c 8b 87 00 03 00 00 49 39 d0 0f 84"),
SITE("PRINT_HUD_PROMPTS", K_ENTRY, 0xb088b0, 0xb088b0, "53 48 8b 3d a8 0c ba 03 e8 b3 ac ed", 0xb088b0, "53 48 8b 3d a8 0c ba 03 e8 b3 ac ed"),   // structure reference only
```

## 5. design-vr.md markers this resolves

- Line 641, "**needs RE: the UI scene camera's view record.**": section 2.2 (and 2.4: the Nemesis screen is the
  player view with the hierarchy camera, not a UI scene).
- Line 647-648, "**needs RE: an item's icon/type field and how to hide it**; candidates `+0x268`/`+0x26a`/`+0x278`":
  sections 3.4-3.5. The candidates were misread: `+0x268` is an object ref, `+0x26a` is node-relative (= `m+0x2ea`,
  the pad/keyboard icon byte), `+0x278` a timer.
- Line 652, "**needs RE: the prompt list**, `UI.PrintHUDElementList`/`Prompts`": section 4.
- Also to update:
  - lines 58, 608 (H11 "eye R skips"): skip eye 0's pass instead (1.4);
  - lines 81, 608 (G18 target swap): not needed, copy the UI surface at the present composite;
  - line 82 (G19 `FUN_009f1d20`/`FUN_00e7a490`, an empty function) and 645-646 (`DAT_046b8580`): G19 = `NAV_EACH`
    `0xec4f83` + `FUN_00ebc700`;
  - line 623 (`UI.PrintHUDElementList` gives the names): it prints nothing, the probe must walk the element tree (4.1);
  - line 631 ("the 3D UI scene, which the game draws into the composite"): into the 3D surface, at the UI aspect (1.3).

## 6. Runtime probes still needed

| ID | Question | Probe |
|---|---|---|
| U1 | confirm 1.1: in the world exactly `0xa124c9`/target 0, `0xa0dc3c`/1/3, `0xa0dc69`/1/0x38 per frame | existing `ui` word in the world (P4 step `world+3 ui`) |
| U2 | separate UI texture with UI size != 3D size; **UI format `dev+0xa0`** (and `+0xe8`); alpha straight or premultiplied | `ui` + log `r32(d+0xa0)`, `r32(d+0xe8)` (one-line change); new `uidump`: CopyResource into a STAGING texture, Map (context slots 14/15), write a PAM with alpha. Since H11 (2026-10-20): the format is in somvr's `HUD made 1: texture WxH format F` line; the alpha by eye with `SOMVR_XR_HUD_ALPHA=straight|premul` (re-xr-pipeline.md 8.5) |
| U3' | which markers exist, their record names, `IconMovieClip(PC)` strings and encoding, `ActionID`, positions vs. shots; does the post-`0xec4f83` hide hold | new `navdump` word: walk `0x46b9200`, per marker the fields of 3.2 + `FUN_00ebc700` + raw attribute entries (3.4) -> TSV; in a fight and at a quest giver |
| U7 | the prompts: which actions/categories appear for counter, drain, interact, mount | new `prompts` word: walk the three diamonds (4.3) -> log; plus `exec Combat.Debug.Prompt 1` (G22) and check where `FUN_02751a50` prints |
| U8 | which menus load a UI scene; its camera values | new `uiscene` word: `im+0x2ec0` `+0xd0/+0x80/+0xb8/+0xf8` and the view at `0xc1ee09` (2.2) in the main menu and on the load screen |
| U9 | H11 in the first layout-B HUD boot: one HUD pass per frame (eye 1), `uiTex` copied, its VkImage published, the quad shows the HUD alone | built 2026-10-20 (`SOMVR_HUD=quad`): somvr log lines per frame `HUD HUD_PASS_EYE .. eye=0 .. -> skipped` / `eye=1 .. -> kept`, `UI .. -> copied gen=..`, H9 `hud=..`, `SUMMARY hud: ..`; layer `xr: hud quad frame ..`; the list in re-xr-pipeline.md 8.5 |
| U10 | the fade alpha in `im+0x2ad8` for XR-layer fades | `ui` word: dump `im+0x2ad8..+0x2af0` during `UI.ForceFadeIn` |
| U11 | the layer bits: which movie is in mask 3 / 0x38 | log `FUN_0072dac0(ui, &handle, mask)` calls with the movie's file name at boot |

**P7 (2026-10-20):** U1 confirmed; U2 answered but for the alpha; U3' answered for the list and the strings (the hide
open); U8 answered for the main menu; U7 and U10 open (see the notes in 1.1, 1.2, 1.4, 2.2, 3.4 and 4.4).

## 7. Corrections to earlier docs

- re-ui-effects.md 1.3 / runtime-probes U1: the display caller is `CUIMgr::vf_09` from `FUN_00a0db90`/`FUN_00a0da80`
  (tail jumps, so `tools/vcall.py` did not see a `CRendererHandle` call); the pre-render call (`target 0`) draws
  nothing **[high]**.
- re-ui-effects.md 2.2/2.3: `+0x268`/`+0x26a`/`+0x278` are not icon fields (5); the per-marker call to hook is
  `0xec4f83`.
- re-ui-effects.md 1.2 step 2 / re-view-render.md 6: the 3D UI scene draws only with a loaded scene and camera; in
  game the menu/modal branch means **no world render** that frame when `im+0x2a48` is set (`0x9ebad5`) **[high]**.
- re-firstperson.md / re-view-render.md "photo mode" at `P+0x310`: confirmed as the hierarchy menu; its view comes
  through H1's target (2.4).
