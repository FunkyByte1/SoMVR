# Shadow of Mordor (Feral Linux build): the tall eye size, a shadow-only head, the render pass list

Static analysis only (2026-10-19): `objdump` of `reference/bin/linux/ShadowOfMordor` (BuildID `87ec5d6f...`), the TSV
indexes in `reference/index/`, a full disassembly listing, and one read-only Ghidra batch on `som-linux` (after the user's
VR session had ended). Nothing was launched, the install and the Feral preferences were not touched. Scratch
(git-ignored): `reference/work-resolution/` (`dis.py` annotated disassembly, `xref.py`, `rpo.py` call order by reverse
post-order, `mrt.py` render-target binds, `gen_sites.py` the SITE lines below, `d_*.txt` listings, `q1.txt` decompiles).
Conventions as in `re-view-render.md`: addresses = runtime addresses (non-PIE), **[high]** read in the code,
**[med]** meaning inferred, **[low]** guess, **RT** = needs a boot. `dev` = the engine device struct at `0x461fa40`
(re-xr-pipeline.md 4.6.2), `cs` = `CGameClientShell*` (`*0x46a9560`), `eng` = `*0x466afa8`.

**Status (2026-10-19, offline, nothing launched):** both routes are built in `mod/somvr` (re-xr-pipeline.md 6.5):
`SOMVR_EYE_SIZE` = 1.4 steps 1 and 3 (RES_3D_DESC gives the eye size itself, so the boot's own mode set needs no
`exec SetResolution`; both letterbox sites are forced off with it) and `SOMVR_HIDE_HEAD` = 2.3's recipe for piece
indices given by hand plus the four HEAD_SHADOW_* windows (`auto` waits for 2.4's shots). Open: the runtime checks of
re-xr-pipeline.md 6.5 (1.5 and 2.5 here).

**Status (2026-10-20, after run 6; offline, nothing launched):** run 6 (`logs/boot-check-20261020-012142`, the eye
size 2015x2217 at every mode set) showed 1.3's prediction in the headset path too: every mono frame (menus, the
front end, loading) is the layer's menu quad = the window image, a 982x1080 pillarboxed column with the UI squeezed into
it, and the front end's 3D scene squeezed sideways (P7). Fixed by **switching the size at runtime** (re-xr-pipeline.md
9.1): the eye size only while layout-B stereo frames render (a mode set `SetResolution EW EH` through the console from
the input tick when an H4 first wants eyes; those frames render flat until it is done), the game's 1920x1080 after
`SOMVR_EYE_SIZE_HOLD_MS` (2000) without such a frame (the pause menu keeps H4: no switch); `=always` is the old
behaviour. So 1.4 step 4 ("back to flat") is now automatic, and the mode set in the world (4's open risk) happens at
every stereo start: the next boot's first check.

## 0. Summary

| Item | Answer | Conf. |
|---|---|---|
| Tall 3D target with a 1920x1080 window | **Not without code.** The 3D surface always gets the back buffer's aspect: its height is computed as `round(ScreenWidth * bbH / bbW)` in `FUN_00741980`; `ScreenHeight` is never read there. `ScreenWidth` is only a render-scale knob. `SetResolution 1832 1920` gives a 1832x1031 3D surface | high |
| Cheapest route | one call-site hook `RES_3D_DESC` (`0x741ceb`, the 3D surface desc) that replaces the computed height with the `ScreenHeight` cvar, then `exec SetResolution 1832 1920` (two args: the UI size and the window stay). Every engine buffer follows (the 3D rect, `rt3D` and its G-buffer, the camera aspect, H8's eye textures). **Built 10-19 (offline):** `SOMVR_EYE_SIZE=WxH` passes W x H itself at every mode set (no exec needed), letterbox sites included | high (code), RT (effects) |
| What breaks | the desktop mirror becomes a pillarboxed portrait image with the HUD squeezed into it (cosmetic); a data flag (`Video.UseWidescreen`) may letterbox the 3D rect to 16:9 (RT, a second hook if so); world-anchored HUD items are projected into the 16:9 UI rect (wrong until G19); 3D menu scenes render into the portrait target (RT) | high (code), RT |
| Hidden pieces and shadows | a hidden piece is skipped by **every** pass, shadow maps included: both skeletal piece submitters test the one mask `node+0x130` unconditionally. P2q is answered statically: the shadow loses the head | high |
| Shadow-only head | no per-piece render flag exists. Hide the head with G12 and clear the head bits of the render-thread mask only around the four shadow-map calls inside the opaque stage (sites `HEAD_SHADOW_*`), restoring it after each. **Built 10-19 (offline):** `SOMVR_HIDE_HEAD=LIST` + `SOMVR_HEAD_SHADOW` | high (call graph), med (that these four are all the shadow maps) |
| Head pieces among the 17 | not findable statically (empty names, part order not recorded); one shot per piece hidden (somvr's trigger word `hide N` with `SOMVR_HIDE_HEAD=none` does it without a probe change) | RT |
| Pass list | per view, in order: opaque stage `FUN_007dc0b0` (G-buffer, SSAO, sun cascades + shadow mask, deferred lights with their shadow maps), deep-buffer geometry, fog, deep-buffer blend, exposure + tonemap, forward, velocity, post, a late fixed-FOV pass. G-buffer = MRT config 3: `R11G11B10_FLOAT` + 3x `R8G8B8A8` + `D32_S8` at the 3D size; lighting accumulates into the same `R11G11B10` target (config 1) | high (order, targets), med (Vulkan mapping) |

## 1. The render resolution

### 1.1 Where the sizes come from **[high]**

| Step | Where | What |
|---|---|---|
| cvars | `_INIT_150` (`0x569e20`), `CConsoleProgramVariable_UInt` (vtable `0x3392208`, value at `+0x38`) | `ScreenWidth` `0x466ae18` (default 1280), `ScreenHeight` `0x466add8` (720), `InterfaceWidth` `0x466ad98` (0 = "uses the desktop resolution"), `InterfaceHeight` `0x466ad58`, `WindowWidth` `0x466ad18`, `WindowHeight` `0x466acd8`, `Windowed` `0x466ac98` (0 fullscreen, 1 windowed, 2 borderless, 3 maximised), `ScreenX/Y` `0x466abd8/0x466ab98` |
| Feral prefs -> cvars | `CShadowOfMordorGame::vf_96` (`0x651290`) writes `feralArguments.cfg` (`+Windowed`, `+ScreenWidth`, `+ScreenHeight`, `+WindowWidth/Height`) from `CFeralGame::vf_14` (`0x65ad20`: prefs `ScreenW`/`ScreenH`/`FullScreen`; replaced by the largest same-aspect display mode only if `MaxDefaultResolutionArea` is set and exceeded). The seeded prefs (`logs/boot-check-20261018-003728/preferences.seeded`) carry `ScreenW 1920`, `ScreenH 1080`, `FullScreen 1` under the GPU key and no `MaxDefaultResolutionArea` | high (code), med (cap) |
| `SetResolution W H [IW IH]` | `FUN_009e1d80` (thunk `0x9e0b60`, client command): runs `ScreenWidth W`, `ScreenHeight H` (negative -> 0) through `eng` vtbl+0x100, with 4 args also `InterfaceWidth/Height`, then `RestartRender` | high |
| `RestartRender` | `FUN_009e0e60`: `interfaceMgr+0x31b0 = 1`, `CWBEngine::vf_26` (`0x8bd7f0` -> `FUN_008b37d0`: mode record from the cvars -> `FUN_008c0290`), flag off, UI relayout `FUN_00a0ff60`. On success `FUN_008b37d0` calls slot 21 of the object the client shell's vtbl+0x60 returns; `CGameSystemEventHandler::vf_21` calls `FUN_009e6f90` = release + re-create `rt3D`/`rtUI` (`FUN_009e66c0`, `FUN_009e6d20`) and `FUN_00a0ff60`. `FUN_00cbf930` runs the same sequence (most likely the options apply), so a change in the world is a shipped path | high (code), med (handler link, in-world) |
| Mode record (0xe8 bytes) | `FUN_008b3120`/`FUN_008b37d0` | `+0x80/+0x84` Screen, `+0x88/+0x8c` Interface, `+0x90/+0x94` Window, `+0xb8/+0xbc` ScreenX/Y, `+0xc0` Windowed, `+0xc4/+0xc8` adapter/output |
| Set mode | `FUN_008c0290`: first time `*(0x466ca60)` = `FUN_007a2ff0` (renderer init -> `FUN_007432f0` -> `FUN_00742320`), later `*(0x466ca70)` = `FUN_00743540` (a render-thread command, executor `FUN_00741ff0`); both call `FUN_00740470` (window) -> `FUN_00741980` (sizes) -> `FUN_007414b0` (surfaces). The result record is written back to the cvars (`ScreenWidth/Height` = the actual 3D size) and stored as the current mode `*0x466ca50` | high |

### 1.2 The size computation **[high]**

- `FUN_00740470` (window): `ScreenWidth` or `ScreenHeight` 0 -> the desktop size; minimum 320x180 for Screen and
  Window. Fullscreen: Interface 0 -> the desktop size. Windowed (1): the window is placed at ScreenX/Y with
  WindowWidth/Height and **Interface := the window's client size**. `RunningUnderToolControl` (`0x466d678`, 0 in the
  game) would force Interface := Screen.
- `FUN_00741980` (decompile in `q1.txt`):
  1. back buffer `dev+0x48` = `FUN_0073fce0(Interface W, H, windowed, 1)` (DXGI desc: format 28, buffer count `*0x4620054`
     clamped to 1..3); in exclusive fullscreen it is snapped by `IDXGIOutput::FindClosestMatchingMode`
     (output vtbl+0x48).
  2. `Render.Setting.AspectRatioLock` (TRenderSetting `0x461e2a0`, value `+0x3c` = `0x461e2dc`, render-thread copy
     `+0x40` = `0x461e2e0`, **default 1**, range 0..1): if on, the back-buffer aspect is snapped to the nearest of 16:9,
     16:10, 4:3, 5:4, 17:9, 21:9, 3:2, 5:3 (`0x3381090..0x33810ac`), shrinking width or height (bars).
  3. **3D surface** `dev+0xd8` = `FUN_0073fce0(ScreenWidth, round(ScreenWidth * h' / w'), ...)` (call `0x741ceb`).
  4. **UI surface** `dev+0x90` = `(w', h')` = the (snapped) back buffer.
  5. out record: `+0x80/+0x84` = 3D size, `+0x88/+0x8c` = UI size.
- `FUN_007414b0` makes the textures: 3D `dev+0x1c0` (`0x461fc00`), UI `dev+0x1b8` (`0x461fbf8`), the same texture when
  both sizes are equal (re-stereo.md 4).

So with the window at 1920x1080 (lock on or off: 16:9 snaps to itself):

| Request | 3D surface | UI / back buffer |
|---|---|---|
| `SetResolution 1920 1080` (today) | 1920x1080 | 1920x1080 (shared texture) |
| `SetResolution 1832 1920` | **1832x1031** | 1920x1080 |
| `SetResolution 3664 1920` (layout A) | 3664x2061 | 1920x1080 |
| `SetResolution 1832 1920` + `RES_3D_DESC` | **1832x1920** | 1920x1080 (separate texture) |

No mode list, no dynamic resolution: the 3D size changes only in `FUN_00741980` at a mode set **[high for the code
path, med for "no other resize"]**. `Performance_ScreenResolution`, `ResolutionIsVeryHigh`, `MainBodyResolution` are
launcher strings.

### 1.3 Downstream of the 3D size **[high unless marked]**

| Consumer | Where | With a portrait 3D surface |
|---|---|---|
| 3D rect `cs+0x80df0..0x80dfc` | `FUN_009e6b50` -> `FUN_00d28b50(rect, letterbox, 0)`: the current mode's `+0x80/+0x84`; `letterbox` = the bool `UseWidescreen` of the game-database `Video` record (`FUN_02178e90`, descriptor `*0x4c1eea0` = `0x4c1eee0`, built by `FUN_02619960`); also at shell init `0x9ea53f` | letterbox off: `{0,0,1832,1920}`. **Letterbox on: a centred 16:9 band `{0,445,1832,1475}` -> `rt3D` 1832x1030.** The value is data: RT (invisible at 16:9) |
| UI rect `cs+0x80dc8` | `FUN_00d28b50(rect, letterbox, 1)`: the mode's `+0x88` | 1920x1080, unchanged |
| `rt3D` (`0x37d7`, a window onto the 3D surface) | `FUN_009e6d20`: `vf_62(rect w, rect h)` | follows the rect; its aux buffers (`FUN_0074df10`: depth, G-buffer, ...) use the target's own w/h, so the whole pipeline follows |
| Camera | `FUN_00a59960`/`FUN_00a5e0f0`: `cam+0x3a0` = the 3D rect; `FUN_00d2bec0`: `fovX = 2 atan(tan(fovY/2) * rectW/rectH) * xscale` | the game's own fovX narrows (0.954 aspect); H1 overrides both FOVs, H3 the tangents; the projection maps the frustum onto whatever rect is bound (re-xr-pipeline.md 2.1). Nothing to do |
| Present composite | `FUN_00742d50` -> `FUN_007a9e70`: the 3D surface is drawn aspect-fitted (pillarbox/letterbox) into the back buffer; when the UI surface is separate it is sampled **in the same draw and viewport** | desktop mirror: a 1031-pixel-wide portrait image centred in 1920x1080, the HUD squeezed into it **[med: one draw, shader not read]**. Cosmetic; the eye images come from H8 |
| HUD world items | `FUN_009ee170` -> `FUN_009f1d20(&view, UI rect)` | the eye's frustum is mapped onto the 16:9 UI: markers misplaced (flat and in the panel) until G19 |
| UI surface | `FUN_007414b0` | separate from the 3D texture now: the HUD is drawn into `rtUI` (`cs+0x80dc0`, cleared transparent); the eye images are HUD-free without H11, and `dev+0x1b8` is a ready-made HUD texture for the panel **[med]** |
| 3D menu scenes (`UISceneCamera`, `FUN_00c1e370` -> `FUN_009f2550`) | render into `rt3D` | aspect source not traced: they may look squeezed in the front end **[low, RT]** |
| H8 eye textures | `et_make` (re-xr-pipeline.md 4.6.2) | re-made at the new desc; XR swapchain 3664x1920 |
| Screenshot/capture `FUN_00816480` | 3D size | consistent |
| Memory | rt3D aux buffers | 3.52 MP instead of 2.07 MP (one `rt3D` for both eyes in layout B) |

Traps:
- Without the hook, `SetResolution 1832 1920` silently produces 1832x1031 and writes 1031 back into `ScreenHeight`.
- Windowed mode with a portrait window (e.g. `Windowed 1`, `WindowWidth 916`, `WindowHeight 960`,
  `Render.Setting.AspectRatioLock 0`, `ScreenWidth 1832`) gives a 1832x1920 3D surface with no code, but the UI becomes
  916x960 portrait (menus laid out for a portrait stage, gamescope scales a small window). Desktop tests only.
- The windowed path writes `SavedWindowedWidth/Height` prefs (`FUN_006537a0`); the options apply path may write
  `ScreenW/H`. Run only with the harness's seeded prefs home.
- Fullscreen `InterfaceWidth/Height` portrait would be snapped to a display mode (`FindClosestMatchingMode`): no route.

### 1.4 The route

1. `RES_3D_DESC`: replace the call at `0x741ceb` (`FUN_0073fce0(dev, &dev+0xd8, w, h, windowed, 0)`, SysV: rdi, rsi,
   edx, ecx, r8d, r9b) with a wrapper: when somvr layout B is configured and `*(uint32_t*)0x466add8` (ScreenHeight) is
   1..16384, pass it as `h`; else unchanged. It runs on the render thread during a mode set (the game thread waits),
   once per SetResolution, so a plain read of the cvar is safe **[med]**.
2. At the front end or a loading screen: `exec SetResolution 1832 1920` (2 args; the probe's executor, re-m1m2.md 2.3),
   or the runtime's recommended size the layer logs. Seeding `ScreenW 1832`/`ScreenH 1920` in the prefs also reaches
   the cvars at boot (`CFeralGame::vf_14` passes them through), but only the hook makes the height count.
3. If the rect is letterboxed (`cs+0x80df4 != 0`): `RES_RECT_LETTERBOX` (`0x9e6bd0`) and `RES_RECT_LETTERBOX_INIT`
   (`0x9ea53f`) are the two `FUN_00d28b50(&cs->rect3D, letterbox, 0)` calls; pass `esi = 0`.
4. Back to flat: `exec SetResolution 1920 1080` with the hook off. Design rule stays: change it only at loading screens
   or in the front end (a mode set re-creates every surface; one black or held frame) **[med]**.

Built 2026-10-19 as `SOMVR_EYE_SIZE` (mod/somvr, re-xr-pipeline.md 6.5): in layout B the wrapper passes W x H as both
`edx` and `ecx` (not `ScreenHeight`), so step 2 is not needed and the boot's first mode set already makes the eye-size
surface; `xr` takes the layer's recommended size at a mode set after the session runs. Step 3's two sites always pass
`esi = 0` with the switch. Back to flat = a boot without the switch. Since the 2026-10-20 merge both `xr` sizes (the
runtime's and the fallback) are scaled by 1 + `SOMVR_OVERSCAN` (default 0.1 with real views), the eyes' fov margin, so
the pixel density stays the runtime's (re-xr-pipeline.md 6.5, 7.2); a plain `WxH` is used as given. Since run 6
(2026-10-20) the boot's mode set is the game's own size again and the eye size comes with the first stereo frame, by
the console's `SetResolution` from the input tick, and goes after the hold (re-xr-pipeline.md 9.1).

```c
SITE("RES_BB_DESC", K_CALL, 0x741a34, 0x741a1e, "41 8b 94 24 88 00 00 00 41 8b 8c 24 8c 00 00 00 41 b9 01 00 00 00 e8 a7 e2 ff ff 8b 15 a1 c8 ed 03", 0x73fce0, "44 8b 15 6d 03 ee 03 66 0f ef c0"),
SITE("RES_3D_DESC", K_CALL, 0x741ceb, 0x741cc0, "41 8b 94 24 80 00 00 00 48 8d b3 d8 00 00 00 89 44 24 0c f3 0f 2a c2 f3 44 0f 59 c0 f3 44 0f 5e c7 f3 44 0f 58 c1 f3 49 0f 2c c8 e8 f0 df ff ff", 0x73fce0, "44 8b 15 6d 03 ee 03 66 0f ef c0"),
SITE("RES_UI_DESC", K_CALL, 0x741d09, 0x741cfa, "48 8d b3 90 00 00 00 44 89 f9 48 89 df 89 c2 e8 d2 df ff ff", 0x73fce0, "44 8b 15 6d 03 ee 03 66 0f ef c0"),
SITE("RES_SURF_CHANGE", K_CALL, 0x74219a, 0x742188, "44 89 ab 44 02 00 00 83 e0 fd 83 f8 01 74 29 48 89 df e8 11 f3 ff ff", 0x7414b0, "55 53 48 89 fb 48 81 ec 98 00 00 00 e8 cf fe ff"),
SITE("RES_SURF_INIT", K_CALL, 0x7429b5, 0x7429a6, "83 e0 fd 83 f8 01 0f 84 ee 00 00 00 48 89 df e8 f6 ea ff ff", 0x7414b0, "55 53 48 89 fb 48 81 ec 98 00 00 00 e8 cf fe ff"),
SITE("RES_MODE_WINDOW", K_CALL, 0x742806, 0x7427f0, "c7 84 24 60 01 00 00 00 00 00 00 48 8d b4 24 80 00 00 00 48 89 df e8 65 dc ff ff", 0x740470, "41 57 49 89 ff 41 56 41 55 49 89 f5 41 54 55 53"),
SITE("RES_MODE_SIZES", K_CALL, 0x74298d, 0x74297f, "4c 89 e2 48 89 df 48 8d b4 24 80 00 00 00 e8 ee ef ff ff", 0x741980, "41 57 41 56 41 55 41 54 49 89 f4 55 48 89 d5 53"),
SITE("RES_SETRES", K_ENTRY, 0x9e1d80, 0x9e1d80, "85 ff 75 04 c3 0f 1f 00 55 89 fd ba 0a 00 00 00", 0x9e1d80, "85 ff 75 04 c3 0f 1f 00 55 89 fd ba 0a 00 00 00"),
SITE("RES_RESTARTRENDER", K_ENTRY, 0x9e0e60, 0x9e0e60, "48 8b 05 f9 86 cc 03 48 85 c0 74 44 48 83 ec 08", 0x9e0e60, "48 8b 05 f9 86 cc 03 48 85 c0 74 44 48 83 ec 08"),
SITE("RES_RECT_LETTERBOX", K_CALL, 0x9e6bd0, 0x9e6bc5, "48 8d bb f0 0d 08 00 89 ee 31 d2 e8 7b 1f 34 00", 0xd28b50, "48 b8 00 00 00 00 01 00 00 00 41 54"),
SITE("RES_RECT_LETTERBOX_INIT", K_CALL, 0x9ea53f, 0x9ea533, "31 d2 48 8d bb f0 0d 08 00 44 89 e6 e8 0c e6 33 00", 0xd28b50, "48 b8 00 00 00 00 01 00 00 00 41 54"),
SITE("RES_PRESENT_BLIT", K_ENTRY, 0x7a9e70, 0x7a9e70, "41 55 45 89 cd 41 54 49 89 fc 55 44 89 c5 53 48", 0x7a9e70, "41 55 45 89 cd 41 54 49 89 fc 55 44 89 c5 53 48"),
```

`RES_BB_DESC`'s context ends with the AspectRatioLock read (`mov edx, [0x461e2e0]`). `RES_3D_DESC`: `edx = [r12+0x80]`
(ScreenWidth), `ecx = cvttss2si(bbH * ScreenWidth / bbW + 0.5)`; the wrapper rewrites only `ecx`. The `0x9e6bd0` call
passes `esi = ebp` (the letterbox bool), `edx = 0`.

### 1.5 First runtime test (desktop, layout B, fake views)

Order: boot as run 4 (`SOMVR_STEREO=B SOMVR_VIEWS=fake`), in the front end `exec SetResolution 1832 1920` first with
the hook **off** (control), then with it on, then load the save. Log after each mode set (one line each):

- cvars `0x466ae18`, `0x466add8` (expect 1832/1031 without the hook, 1832/1920 with it), `0x461e2dc` (lock);
- `dev` sizes: back buffer `0x461fa88/8c`, UI `0x461fad0/d4`, 3D `0x461fb18/1c`; textures `0x461fbf8` (UI) vs `0x461fc00`
  (3D): must differ with the hook;
- `cs+0x80df0..0x80dfc` (3D rect; `y0 != 0` = UseWidescreen letterbox -> enable the letterbox sites), `cs+0x80dc8..`
  (UI rect), `rt3D` = `*(cs+0x80e00)`: `+0xc/+0x10` (w, h), `+0x8` flags `0x37d7`;
- in the world: `cam+0x3a0..0x3ac`, `cam+0x3fc/0x400`, the H1/H3 lines (rect `{0,0,1,1}`, pixel rect 1832x1920), somvr's
  `EYES made 1: 2 textures 1832x1920 format 28`, H8 `-> copied` twice per frame, frame time vs run 4;
- shots: front end (3D menu scene look), world (desktop: pillarboxed portrait view, HUD squeezed), pause menu.
Pass: 3D 1832x1920, UI 1920x1080, separate textures, no letterbox (or letterbox found and fixed), H8 copies at the new
size, no `other-target`/`not-3d-surface`, steady frame rate.

With the built `SOMVR_EYE_SIZE` the control needs no second mode set: each `RES ... 3D surface` line logs the game's own
size next to the one passed, and the `RES ... rect3D` lines log `UseWidescreen` (the open data question) before it is
forced to 0. The exact lines to expect: re-xr-pipeline.md 6.5, runtime check 1.

**Runtime result (P7, 2026-10-20, `logs/boot-check-20261020-001117`; runtime-probes.md P7 results).** Done at the main
menu with the probe's own `RES_3D_DESC` wrapper (`restall`, passing the `ScreenHeight` cvar), not with somvr. Holds,
exactly: the control `exec SetResolution 1832 1920` (hook off) gives a **1832x1031** 3D surface (and the game writes
1031 back into `ScreenHeight`), UI 1920x1080, separate textures; with the hook the same exec gives **3D 1832x1920, UI
1920x1080**, back buffer 1920x1080, separate textures, the 3D rect `0,0,1832,1920` (**no letterbox**: `UseWidescreen`
needs no fix here), `rt3D` 1832x1920 (flags `0x37d7`); `SetResolution 1920 1080` with the hook off restores 1920x1080
and one texture. The hook ran once per mode set (heights 1031 -> 1031, 1031 -> 1920, 1080 -> 1080), each exec took
53 ms on the game thread, no hang or crash. The desktop (shot 81) is the predicted pillarboxed portrait (1031x1080)
with the menu squeezed into it; also squeezed: **the 3D menu scene**, because a UI scene's fovX comes from the UI
aspect (re-hud.md 2.2): in an eye it needs the eye's projection. Not checked yet: the world at the tall size (the
camera's `+0x3a0`/`+0x3fc`, H1/H3, H8's copies at 1832x1920, the frame rate): the somvr `SOMVR_EYE_SIZE` boot.

## 2. Hiding the head, keeping its shadow

### 2.1 The mask reaches every pass **[high]**

- Render-thread node: `CRenderNodeMeshSkeletal` = `*(renderable+0x50) + 0x10`, `renderable = *(CSimpleModel+0xb0)`:
  the handle (`TRenderNodeMeshSkeletalHandle`) embeds the node at `+0x10` (slot 53 `0x712460` queues `cmd+0x10 =
  handle+0x10` with the member-pointer executor `0x29` = `CRenderNodeMeshSkeletal::vf_05` `0x71d510`, which writes the
  hidden-piece mask `node+0x130` (pieces 0-31) / `+0x134` (32-63)).
- `FUN_0071fb80(node, piece)` = IsPieceHidden (bit test on `node+0x130`). Its only render callers are the two skeletal
  piece submitters `FUN_0081ff80` (3 calls: `0x8201f3`, `0x82038c`, `0x82046b`) and `FUN_0081fd10` (`0x81fee3`); each
  skips the mesh section (`section+7` = piece) when hidden, with no pass condition. (The third caller, `FUN_00732880`
  from `FUN_00733260`, was not traced.)
- Every draw-list builder sends skeletal nodes (`+0xb0 == 1`) to one of them: G-buffer `FUN_008230d0` (debug variant
  `FUN_00823b20`), deep buffer `FUN_00822a40`, forward `FUN_00822c70`, late pass `FUN_00822eb0`, velocity
  `FUN_00823760`, sun cascades `FUN_00823f10` and local-light/sky shadows `FUN_00823b20`/`FUN_00824570` (via
  `FUN_008229a0` -> `FUN_0081fd10`).
- So **P2q = yes, the shadow loses the head** (no boot needed for the question; the shot is still worth taking).

### 2.2 No per-piece shadow flag **[high]**

The node bytes the builders filter on are per node, not per piece: `+0x40` (nonzero = skip everywhere), `+0xb0` node
type (0 rigid, 1 skeletal, 2 custom, set by the ctors `FUN_00721xxx`), `+0xb1` pass category (handle slot 31
`0x711390` -> executor `0x70a690`; the G-buffer builder `FUN_008230d0` takes 1, the deep-buffer builder `FUN_00822a40`
2, the forward builder 1 and 3, the late pass 4), `+0x42` bit 2 (slot 28, executor `0x70a630`: set = drawn only by
`FUN_00824570`, the `FUN_007f5820` shadow; clear = drawn by the `FUN_008229a0` users: cascades `FUN_00823f10`, light
shadows and the debug G-buffer `FUN_00823b20`), `+0x41` bits (bit 0 = "in this view's list", set and cleared by the
pipeline `FUN_007d6e20` itself). None of re-firstperson.md 3.2's
candidates is "shadow only" for part of a model.

### 2.3 The swap

The order inside the opaque stage `FUN_007dc0b0` (decompile) is: `FUN_007f5820` (a depth-only pass for the `+0x42`
bit-2 casters, most likely the sky shadow map **[med]**) -> G-buffer (bind `0x7dc656`, build `FUN_008230d0` at
`0x7dcdd3`, execute, extra views `FUN_007d9ef0`) -> light bucket 2 `FUN_007db760` (`0x7dd0e2`, gated by `0x4635ac1`)
-> SSAO -> sun cascades `FUN_007f6250` (`0x7dd605`) + shadow mask `FUN_007e2940`
-> deferred lights `FUN_007dbd30` (`0x7dca9f`: ambient, sun, then per light `FUN_007db760` -> `FUN_007f4f10` = that
light's shadow map, then its light volume). The light functions draw volumes and full-screen passes, never the view's
meshes **[high for the calls, med for "never"]**.

Recipe (all on the render thread; nothing races: the mask is only written by the executor on the same thread):
1. Game thread, once and after each model rebuild: G12 `SetPieceHidden(m, hChar, piece, 1)` for the head pieces
   (bits `headBits`). Publish `node = *(*(sm+0xb0)+0x50) + 0x10` for the render thread.
2. Wrap the four calls `HEAD_SHADOW_PRE` (`0x7dc603`), `HEAD_SHADOW_LIGHTS` (`0x7dd0e2`), `HEAD_SHADOW_CASCADES`
   (`0x7dd605`), `HEAD_SHADOW_LIGHTS2` (`0x7dca9f`): before, `saved = node->mask; node->mask &= ~headBits`; after,
   `node->mask = saved`. The G-buffer, deep buffer, forward, velocity and late passes keep the head hidden.
3. Per eye nothing extra: every RenderScene runs the stage again.

Built 2026-10-19 as `SOMVR_HIDE_HEAD=LIST` (mod/somvr, re-xr-pipeline.md 6.5): step 1 in H4 on stereo frames only (the
pieces are shown again by the first flat frame), the node published per renderer frame and used only while that
frame's eyes render; step 2 restores only the bits it cleared. Only pieces the game had not hidden itself are touched.
`SOMVR_HEAD_SHADOW=0` (or the trigger word `headshadow-`) gives the headless-shadow control shot of 2.5.

The cheap alternative stays the design's: accept the headless shadow (milestone 2), or clip with the near plane
(re-firstperson.md 3.3: shadow maps use their own projections, so a 10-15 cm eye near plane keeps the head's shadow,
at the cost of hands closer than that).

```c
SITE("HEAD_PIECE_TEST", K_ENTRY, 0x71fb80, 0x71fb80, "31 c0 83 fe 3f 77 1a 89 f1 c1 ee 05", 0x71fb80, "31 c0 83 fe 3f 77 1a 89 f1 c1 ee 05"),
SITE("HEAD_TEST_SUBMIT_A", K_CALL, 0x8201f3, 0x8201e1, "e8 2a 41 fa ff 3b 45 00 75 cd 41 0f b6 75 07 4c 89 e7 e8 88 f9 ef ff", 0x71fb80, "31 c0 83 fe 3f 77 1a 89 f1 c1 ee 05"),
SITE("HEAD_TEST_SUBMIT_B", K_CALL, 0x82038c, 0x82037c, "ff 49 89 c5 48 85 c0 74 db 0f b6 70 07 4c 89 ff e8 ef f7 ef ff", 0x71fb80, "31 c0 83 fe 3f 77 1a 89 f1 c1 ee 05"),
SITE("HEAD_TEST_SUBMIT_C", K_CALL, 0x82046b, 0x82045c, "49 89 c5 48 85 c0 74 dc 0f b6 70 07 4c 89 ff e8 10 f7 ef ff", 0x71fb80, "31 c0 83 fe 3f 77 1a 89 f1 c1 ee 05"),
SITE("HEAD_TEST_SUBMIT_D", K_CALL, 0x81fee3, 0x81fed4, "49 89 c5 48 85 c0 74 dc 0f b6 70 07 4c 89 ff e8 98 fc ef ff", 0x71fb80, "31 c0 83 fe 3f 77 1a 89 f1 c1 ee 05"),
SITE("HEAD_MASK_EXEC", K_ENTRY, 0x71d510, 0x71d510, "89 b7 30 01 00 00 89 97 34 01 00 00", 0x71d510, "89 b7 30 01 00 00 89 97 34 01 00 00"),
SITE("HEAD_SETMASK_SLOT", K_VSLOT, 0x335b5e0, 0x335b5e0, "60 24 71 00 00 00 00 00", 0x712460, "41 54 41 89 f4 55 89 d5 53 48 89 fb bf 30 00 00"),
SITE("HEAD_CAT_SLOT", K_VSLOT, 0x335b530, 0x335b530, "90 13 71 00 00 00 00 00", 0x711390, "55 89 f5 53 48 89 fb bf 30 00 00 00 48 83 c3 10"),
SITE("HEAD_SHADOW_PRE", K_CALL, 0x7dc603, 0x7dc5f0, "4c 89 a4 24 90 00 00 00 e8 e3 c8 ff ff 48 89 de 48 89 ef e8 18 92 01 00", 0x7f5820, "31 c0 48 83 bf 00 38 00 00 00 0f 84 70 02 00 00"),
SITE("HEAD_SHADOW_LIGHTS", K_CALL, 0x7dd0e2, 0x7dd0d3, "31 c9 48 89 de 48 8d 95 30 71 00 00 48 89 ef e8 79 e6 ff ff", 0x7db760, "41 57 49 89 f7 41 56 41 55 41 54 49 89 d4 55 48"),
SITE("HEAD_SHADOW_CASCADES", K_CALL, 0x7dd605, 0x7dd5f6, "48 89 de 48 89 ef f3 0f 10 8c 24 90 00 00 00 e8 46 8c 01 00", 0x7f6250, "80 3d 6c f8 e3 03 00 75 07 c3 66 0f 1f 44 00 00"),
SITE("HEAD_SHADOW_LIGHTS2", K_CALL, 0x7dca9f, 0x7dca91, "48 8d 95 00 71 00 00 48 89 de 44 0f b6 00 e8 8c f2 ff ff", 0x7dbd30, "41 56 49 89 d6 41 55 49 89 fd 41 54 49 89 cc 55"),
SITE("HEAD_GBUF_DRAW_A", K_CALL, 0x7dc6af, 0x7dc6a1, "00 48 89 de 48 89 ef 4c 8d a5 60 38 00 00 e8 1c 94 01 00", 0x7f5ad0, "41 57 66 0f ef c0 66 0f ef d2 4c 8d bf 58 39 00"),
SITE("HEAD_GBUF_DRAW_B", K_CALL, 0x7dcdd3, 0x7dcdc4, "48 8b 4c 24 38 48 8b 54 24 30 48 8b 74 24 28 e8 f8 62 04 00", 0x8230d0, "41 57 41 56 41 55 49 89 fd 41 54 49 89 cc 55 4c"),
```

`HEAD_GBUF_DRAW_A` is the debug G-buffer variant (`DAT_04635aac`), `_B` the normal G-buffer build (pass type 6, lists
`param_4..6`); not hooked, they mark where the head must stay hidden. `HEAD_SHADOW_LIGHTS2` covers both light buckets
(it calls `FUN_007db760` twice).

### 2.4 Finding the head pieces

- Names: slot 14 gives `..._Talion_Hair_Bones.` (0) and `..._ragged_cloak.hkt` (1), both Havok physics data, and empty
  strings for 2-16 (run 3). The part order of `FUN_0236b820` (`SMC_BaseModel`, Body, Head, Legs, Helmet, Hair,
  Armor1-4, Accessory1-2, HeadScar ...) is not recorded per piece in anything traced **[med]**.
- Probe (needs a probe change, runtime-probes P3): a word `piecehide N` (SetPieceHidden on piece N alone) and a fixed
  close camera on the head (photo mode, or `fpcam` pulled back): baseline shot, then one shot per piece 2..16 with only
  that piece hidden, pixel diff in the head box. Pieces whose hiding changes the head box = head/hood/hair/beard. Same
  run: the sun-behind shot with the swap on and off (2.3).
- The hair may also be a separate deep-buffer node (category 2, `FUN_007feb00`); then hiding pieces leaves it: the shot
  shows it. Hide that node's own pieces or its whole node (`+0x40`) the same way.

**Runtime result (P7).** The sweep ran (`piecehide 0` .. `16`, G12 `SetPieceHidden` on `hChar`'s model, each call
returning 0, one shot per piece from behind Talion against a base shot) and **no piece's hiding changed anything
visible**: head, hair, cloak, quiver, sword and dagger are in all 17 shots; the differences are the idle motion and the
fog. So the head, hood and hair are **not among these 17 pieces**, and `SOMVR_HIDE_HEAD` has no indices to take (its
`auto` has nothing to wait for). Most likely the 17 are the base model's (the only names are Havok files: hair bones,
cloak) and the visible parts are separate models attached to it (the orcs are built from `SMC_BaseModel`,
`SMC_BodyModel`, `SMC_HeadModel`, `SMC_HelmetModel`, ... part models, as the P7 faction dump shows; Talion likely the
same). Next (probe change, then a run): log the render node mask `node+0x130` after `piecehide` (did the bit get
set?), list the player model's attached children (`*(playerSm+0x90)`, re-animation.md 4) with their own piece counts
and names, and sweep or node-hide (`+0x40`) those; 2.3's mask swap then has to work on the head model's node, not the
base model's.

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results).** Both
probe changes ran:
- `piecehide all` sets the bits all the way down: model `0x1ffff`, renderable `0x1ffff`, consumed (dirty 1 -> 0) two
  ticks later, the handle's node `0x1ffff`. Still nothing vanished (shot 21). The mask works, but hChar's model does
  not draw the head.
- `attached` lists **7 entries, all weapon models**, each with one unnamed piece, and 0 one level down:
  - 0 = the sword's item object, 1 and 2 on the same socket 24 on `scabbard_back`;
  - 3 = the dagger's item object, 4 and 5 on socket 23 on `Dagger_Sheath`;
  - 6 = the bow's item object (no node).
- `attachhide 0`..`6` (piece) and `0`..`3 node` (`+0x40`): **child 1 removes the sheathed sword's hilt** (by either
  path), **child 4 the dagger's hilt**; the others nothing visible.
- The head, the hair with its tie and the hood/cloak are in every shot.
So **the head, hood and hair have no attached-model IDs**: `SOMVR_HIDE_HEAD` still has nothing to take. 2.3's swap
needs the render node that draws the head, which is not reachable from hChar's model or its attach list. Candidates:
- this section's deep-buffer hair node (`FUN_007feb00`, category 2);
- the part models `FUN_0236b820` builds (the `SMC_*` parts of re-firstperson.md 3.1), which may live as separate render
  nodes;
- find them by the player's node pointer in the per-view list builders (2.5), or scale the `Head` node in the phase-2
  pose (the `armpose` write point passed in P8; the head's shadow goes too);
- the near plane (re-firstperson.md 3.3 option 1) is the fallback that needs no IDs.

### 2.5 Runtime checks

- Count per frame, per wrapper window, the entries of `FUN_0081ff80`/`FUN_0081fd10` with `rsi`/`rdi` = the player's
  node (entry hooks, log only): expect calls inside `HEAD_SHADOW_CASCADES` (sun) and `HEAD_SHADOW_PRE` when the sky
  shadow is on, inside `_LIGHTS*` near torches, and the G-buffer build `HEAD_GBUF_DRAW_B` outside all windows.
- Shots: sun behind Talion, head pieces hidden, swap off (headless shadow) and on (full shadow); looking down: no
  inside of the neck in either eye.
- With the built switch: the per-5-s `HIDE ... shadow windows pre=a/b lights=c/d cascades=e/f lights2=g/h` line counts
  the swaps per window (calls with the player's node in them are not counted separately yet; the shots decide).

## 3. The render passes

### 3.1 Order per view **[high for order and calls, med for names unless a material names it]**

`FUN_007a24d0` (RenderScene) -> `FUN_007d7840` -> `FUN_007d6e20(P = 0x4625840, frame, lists ...)`, the pipeline
materials in a table at `0x462ce60` (`FUN_007b0820`; a material's handle = `0x462ce60 + slot`):

| # | Call site | Function | What (evidence) |
|---|---|---|---|
| 1 | `0x7d6ee6` | `FUN_007e6ad0` | unknown; its result gates step 4. Then visibility phase 2 `FUN_007b8000(.., 2)` |
| 2 | `0x7d6f82` | **`FUN_007dc0b0` opaque stage** | 2a sky shadow `FUN_007f5820` (depth-only MRT 10, casters with `+0x42` bit 2, matrix `0x4623a40`); 2b **G-buffer**: bind MRT 3 with clear (`0x7dc656`), build `FUN_008230d0` (pass 6), execute `FUN_007d3a70`, extra views `FUN_007d9ef0` over list `param_3` (own view and culling, pass 6; purpose **[low]**), debug G-buffer views (`deferred_debuggbuffer*`); 2c depth copy (`FUN_0074b700`, `deferred_depth_copy`), light bucket 2 (`FUN_007db760`), `SortDeepBuffer` (`FUN_007e72b0`); 2d **SSAO** `FUN_0079e6f0` (`SpecDiffOcclusion`, `SSAO_*`, `depth_downscale`), then `StencilDeepBuffer`; 2e **sun cascades** `FUN_007f6250` (depth-only MRT 4) + **shadow mask** `FUN_007e2940` (`deferred_shadow_mask` into MRT 8 = R8), or a white clear of MRT 8 when off; 2f **lighting**: bind MRT 1 (`0x7dc9c6`, clear 0) and `FUN_007dbd30`: ambient (`deferred_ambientlight`), sun `FUN_007e24d0` (`deferred_outdoorlight`), each local light `FUN_007db760`: its shadow map `FUN_007f4f10`, then its volume (`deferred_point/spot/cube/directional`, `deferred_slv`) |
| 3 | `0x7d7674` | `FUN_007feb00` | deep-buffer geometry: category-2 nodes, pass 6, gated by `0x4636534`/`0x4636532` (most likely hair/fur OIT) |
| 4 | `0x7d75fe` | `FUN_007e6820` | `fog_overlay` |
| 5 | `0x7d77b6` | `FUN_007e7b10` | `BlendDeepBuffer` (when the deep buffer had draws) |
| 6 | `0x7d740b` | `FUN_007ea320` | exposure (`LuminanceDownsample`, `ComputeDynamicExposure`) + **tonemap** `HDRToneMap` into the target's own colour (for `rt3D`: the 3D surface) |
| 7 | `0x7d76eb` | `FUN_007fedc0` | forward/translucent: categories 1 (skeletal) and 3 |
| 8 | `0x7d7700` | `FUN_007dd880` | a sub-view with its own view state (re-xr-pipeline.md 3) **[low]** |
| 9 | `0x7d75bd` | `FUN_007e7fb0` | object velocity `motion_objectvelocity` into MRT 12 (G-buffer RT3), only `MotionBlurQuality > 2` |
| 10 | `0x7d74e6` | `FUN_007eb1d0` | **post**: DOF (`dof_*`), bloom/sun shafts `FUN_007a1530`/`FUN_007a21f0`, motion blur `FUN_007c5020`, FXAA, vignette, gamma (re-xr-pipeline.md 4.6.1) |
| 11 | `0x7d7138` | `FUN_007fefa0` | a late pass for category-4 nodes from the camera position with a **fixed** (1.13, 0.65) pair instead of the eye's FOV (`FUN_0072c7f0` at `0x7d70c5`, constant `0x3f2666663f90a3d7`; re-stereo.md 5 calls it 1.13 x 0.65 rad), after post **[low purpose]** |

Overlay-layer callbacks `FUN_00724690(&0x461e0e0, frame, stage)` run at stages 0-3 inside the opaque stage and 4, 5, 6
around steps 6-11 (the renderer's own overlay hooks, `Render.Draw.Overlays`).

VR note: step 11 ignores the eye's projection; anything in category 4 would be mis-projected in VR (RT: count its
draws, re-stereo.md section 3 table).

### 3.2 Targets and formats **[high]**

`FUN_0074bb70` (BeginRenderTarget internals; `FUN_00743990(rtHolder, &clear, clearFlags, mrtConfig, depthMode, x)`)
binds one of 13 MRT configs (argument names **[med]**; jump table `0x33899a0`, colour lists `0x338b4d0..`), each a
list of sub-target ids mapped by `FUN_00747b10` to the target's own textures (`FUN_0074df10`, sized from the target):

| Config | Colour targets (`rt+` field, DXGI format) | Used by |
|---|---|---|
| 3 | `+0x2d0` R11G11B10_FLOAT, `+0x2e8` R8G8B8A8, `+0x2d8` R8G8B8A8, `+0x2e0` R8G8B8A8 | **G-buffer** (2b) |
| 1 | `+0x2d0` R11G11B10_FLOAT alone | **light accumulation** (2f): the lights add into G-buffer RT0 |
| 8 | `+0x2f0` R8_UNORM | shadow mask (2e) |
| 12 | `+0x2e0` R8G8B8A8 | velocity (9) |
| 2 | `+0x208` R32_FLOAT (flag `0x40`) | light shadow functions `FUN_007f3f10` |
| 0 / 9 | own colour (screen targets: the 3D surface `dev+0x1c0`) | tonemap/post output |
| 4-7, 10, 11 | none (depth only) | shadow maps |

Depth-stencil for flag 1: `rt+0x200` (`FUN_0074ecb0`, typeless 19 / DSV D32_FLOAT_S8X24_UINT 20 / SRV
R32_FLOAT_X8X24 21 at `+0x210`, stencil SRV 22 at `+0x270`). Reversed-Z, infinite far (re-xr-pipeline.md 1.1). The
debug views name the G-buffer channels diffuse, specular and "clear/cavity" (which RT holds which: **[low]**).

Expected Vulkan formats (Feral's DXGI -> Vk table not traced **[med]**): R11G11B10_FLOAT -> `B10G11R11_UFLOAT_PACK32`
(122), R8G8B8A8 -> `R8G8B8A8_UNORM` (37), D32_S8X24 -> `D32_SFLOAT_S8_UINT` (130), R32_FLOAT -> `R32_SFLOAT` (100),
R8_UNORM -> `R8_UNORM` (9).

### 3.3 Recognising them

- **In the layer**: images at the 3D size (`somvr_shared.eyes.expect_w/h`) from `vkCreateImage`; framebuffers by their
  image views; then each `vkCmdBeginRenderPass` by attachment set: the **G-buffer pass is the only one with 4 colour
  attachments** (B10G11R11 + 3x RGBA8) + D32S8; **lighting** = 1 colour attachment that is the G-buffer's RT0 image;
  shadow mask = R8 at the 3D size; shadow maps = depth-only at their own size; tonemap/post write the 3D surface image
  that H8 copies. Assumes Feral turns each `OMSetRenderTargets` set into one render pass (it keeps render-pass and
  framebuffer caches, `CVulkanFramebufferAttachment`) **[med, RT: log the tuples once]**.
- **In the engine**: a render-thread entry hook on `FUN_0074bb70` sees `r8d` = the config (3 = G-buffer begins, 1 =
  lighting, 8 = shadow mask). With `EnableIdxD3D11Threaded=1` (the seeded prefs) the Vulkan commands are recorded later
  on Feral's worker, so an engine-side marker must travel in the command stream; use it to confirm the layer's
  classification, not to time the layer.
- Uses (design-vr.md 10): VRS for the G-buffer and lighting passes needs a fragment-shading-rate attachment, i.e.
  recreating the two matching render passes with `vkCreateRenderPass2` + `VkFragmentShadingRateAttachmentInfoKHR`
  and their framebuffers; the hidden-area mesh can be drawn at depth 1.0 (nearest, reversed-Z) right after the
  G-buffer pass begins, which rejects G-buffer writes there (the lights still run on those pixels unless stencil is
  used too). Injecting our own geometry into the G-buffer needs the channel encoding (RT, 3.2).
- Depth for depth-tested overlays (design-vr.md 0): `rt+0x200`'s image (D32S8, 3D size). In layout B eye R's pass
  overwrites it before the present: an H8-style copy per eye (depth, after the eye's pass) is needed.

```c
SITE("PASS_BIND", K_ENTRY, 0x74bb70, 0x74bb70, "41 57 45 89 c7 41 56 41 55 41 54 55 53 48 89 fb", 0x74bb70, "41 57 45 89 c7 41 56 41 55 41 54 55 53 48 89 fb"),
SITE("PASS_MRT_GBUFFER", K_VSLOT, 0x33899b8, 0x338b500, "05 00 00 00 04 00 00 00 06 00 00 00 08 00 00 00", 0x74bf68, "66 0f 6f 05 90 f5 c3 02 0f 29 44 24"),
SITE("PASS_SUBTARGET_RT0", K_VSLOT, 0x3389588, 0x3389588, "00 7c 74 00 00 00 00 00", 0x747c00, "48 8b 87 d0 02 00 00 48 85 c0 0f 85"),
SITE("PASS_RT_AUX", K_ENTRY, 0x74df10, 0x74df10, "41 57 41 56 41 55 41 89 d5 41 54 55 53 48 83 ec", 0x74df10, "41 57 41 56 41 55 41 89 d5 41 54 55 53 48 83 ec"),
SITE("PASS_OPAQUE_STAGE", K_CALL, 0x7d6f82, 0x7d6f72, "48 89 de 48 8d 44 24 36 50 ff b4 24 68 08 00 00 e8 29 51 00 00", 0x7dc0b0, "41 57 41 56 41 55 41 54 55 53 48 81 ec b8 00 00"),
SITE("PASS_DEEP", K_CALL, 0x7d7674, 0x7d7666, "4c 89 e1 48 89 de 48 8b 94 24 50 08 00 00 e8 87 74 02 00", 0x7feb00, "80 3d 2d 7a e3 03 00 74 09 80 3d 22 7a e3 03 00"),
SITE("PASS_TONEMAP", K_CALL, 0x7d740b, 0x7d73fb, "48 89 44 24 08 e8 ab c6 f6 ff 48 89 de 4c 89 ef e8 10 2f 01 00", 0x7ea320, "83 3d 19 45 e3 03 01 0f 8e 5b 04 00 00 55 48 89"),
SITE("PASS_FORWARD", K_CALL, 0x7d76eb, 0x7d76dd, "48 89 de 4c 89 ef 48 8b 94 24 50 08 00 00 e8 d0 76 02 00", 0x7fedc0, "80 3d 6d 77 e3 03 00 74 09 80 3d 63 77 e3 03 00"),
SITE("PASS_SUBVIEW", K_CALL, 0x7d7700, 0x7d76f0, "4c 89 f2 48 89 de 4c 89 ef c6 05 c9 b6 e4 03 01 e8 7b 61 00 00", 0x7dd880, "41 57 41 56 41 55 41 54 55 48 89 fd 53 48 89 f3"),
SITE("PASS_VELOCITY", K_CALL, 0x7d75bd, 0x7d75ad, "ff b4 24 58 08 00 00 4c 89 f9 48 89 de 4c 89 ef e8 ee 09 01 00", 0x7e7fb0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 78 02 00"),
SITE("PASS_POST", K_CALL, 0x7d74e6, 0x7d74d8, "84 c0 0f 85 30 fb ff ff 48 89 de 4c 89 ef e8 e5 3c 01 00", 0x7eb1d0, "83 3d 69 36 e3 03 01 0f 8e 43 01 00 00 41 57 b9"),
SITE("PASS_LATE", K_CALL, 0x7d7138, 0x7d7129, "08 4c 89 ef 4c 89 e1 48 8b 94 24 50 08 00 00 e8 63 7e 02 00", 0x7fefa0, "80 3d 8a 75 e3 03 00 75 07 c3 66 0f 1f 44 00 00"),
SITE("PASS_GBUF_BIND", K_CALL, 0x7dc656, 0x7dc642, "48 8b bb 08 03 00 00 41 b9 01 00 00 00 89 84 24 90 00 00 00 e8 35 73 f6 ff", 0x743990, "48 83 ec 20 8b 36 48 89 f8 48 8b 3f 48 83 c0 10"),
SITE("PASS_LIGHT_BIND", K_CALL, 0x7dc9c6, 0x7dc9b8, "c7 84 24 90 00 00 00 00 00 00 00 4c 89 ee e8 c5 6f f6 ff", 0x743990, "48 83 ec 20 8b 36 48 89 f8 48 8b 3f 48 83 c0 10"),
SITE("PASS_SHMASK_BIND", K_CALL, 0x7dcb48, 0x7dcb37, "41 b9 02 00 00 00 c7 84 24 90 00 00 00 ff ff ff ff e8 43 6e f6 ff", 0x743990, "48 83 ec 20 8b 36 48 89 f8 48 8b 3f 48 83 c0 10"),
SITE("PASS_CASCADE_BIND", K_CALL, 0x7f7319, 0x7f7306, "48 8d b4 24 00 04 00 00 c7 84 24 00 04 00 00 ff ff ff 00 e8 72 c6 f4 ff", 0x743990, "48 83 ec 20 8b 36 48 89 f8 48 8b 3f 48 83 c0 10"),
```

`PASS_MRT_GBUFFER`: jump-table case 3 of `FUN_0074bb70`, context = its colour list `{5, 4, 6, 8}`. `PASS_SUBTARGET_RT0`:
sub-target 5 -> `rt+0x2d0`.

### 3.4 Runtime checks

- Layer (log only, one frame in the world): every image created at the 3D size (format, usage), every render pass
  (attachment formats, load/store ops) and, per `vkCmdBeginRenderPass` of one frame, the attachment tuple in order.
  Expect per eye: [sky shadow] -> G-buffer (4 colour + D32S8) -> ... -> depth-only shadow passes -> R8 -> 1 colour
  (RT0) lights -> ... -> the 3D surface.
- Engine: a `PASS_BIND` entry hook logging `r8d` per call for one frame, to pair with the layer's list.
- Category 4 (step 11): log the draw count of `FUN_00822eb0`'s list per frame in the world; 0 = nothing to fix.

## 4. Open risks

| Risk | Where | Check |
|---|---|---|
| `Video.UseWidescreen` letterboxes a portrait 3D rect to 16:9 | `FUN_009e6b50`, `FUN_00d28b50` | 1.5 (`cs+0x80df4`); fix with the two `RES_RECT_LETTERBOX*` sites (built: forced off with `SOMVR_EYE_SIZE`, the flag logged) |
| 3D menu scenes squeezed at the portrait size | `FUN_00c1e370`/`FUN_009f2550` | **seen** (P7, run 6); fixed: the front end keeps 1920x1080 (re-xr-pipeline.md 9.1) |
| A mode set in the world drops a frame or stalls H8/the layer | `FUN_009e6f90`, `et_make` | now at every stereo start (the switch): the `ESIZE ... in N ms` line and the frame time after it, the next boot |
| The four shadow windows miss a caster pass, or one of them also draws view geometry | 2.3 | 2.5 counters and shots |
| Feral's render-pass mapping differs (subpasses, merged passes) | 3.3 | 3.4 layer dump |
| Category-4 late pass with a fixed frustum | `FUN_007fefa0` | 3.4 |
