# Shadow of Mordor (Feral Linux build): the VR laser pointer - the Scaleform mouse feed

Static analysis only (2026-10-20). Nothing launched, the install not touched. Eleven read-only Ghidra batches on the
project copy `som-linux-q5` (`scripts/ghidra_query.sh`, run while the heavy-job gate was open), `objdump` ranges of
`reference/bin/linux/ShadowOfMordor`, the full listing `reference/work-ui/full.dis.gz`, the TSV indexes in
`reference/index/`. Scratch (git-ignored): `reference/work-pointer/` - batches `bN.txt`, answers `oN.txt`,
`tools/site.py` (SITE lines from `name:KIND:addr`).

**Status 2026-10-20: R1 built, offline** (nothing launched): `SOMVR_POINTER=1` (needs `SOMVR_INPUT=1`), see
`re-xr-pipeline.md` 10. The layer computes the ray's hit on the menu quad (hysteresis, a One Euro filter, the hand = the
last one that pulled its trigger, the clicks), draws a dot and a thin ray, and publishes the point in `somvr_input` v2
(the ABI need of 4, made); `somvr.so` feeds `FUN_00a0fa20` / `FUN_00a0f760` / `FUN_00a0f8c0` from the input tick (3.1,
the mapping of 4 with `GV+0x28`), takes the clicking trigger away from the pad's LT/RT, and patches `LP_GUARD_HOVER` /
`LP_GUARD_CLICK` (answer 0 while the pointer is live; byte-checked like every site). Not built: the Nemesis Mouse1
extra and the `LP_SYS_*` desktop-mouse drop (3.1). The probe's P8 words (7) patch the same two guard calls: the
probe's collision check has an EXCLUSIVE entry for each (re-xr-pipeline.md 10.3). The open items of 10 are the next
boot's checks (re-xr-pipeline.md 10.5).

Builds on `re-input.md` (1, 3.4, 7), `re-ui-effects.md` (1, 5, 6), `re-hud.md` 1.4, `re-xr-input.md` 3.5 and
`design-vr.md` 7. Conventions as there: runtime addresses (non-PIE), `vf_NN` = vtable slot, **[high]** read in code and
unambiguous, **[med]** read in code, meaning inferred, **[low]** inference, **[RT]** needs a runtime check.

Names: `shell = *(void**)0x46a9560`; `im = *(shell+0x80ff8)` (CInterfaceMgr); `iw = *(int*)(shell+0x80dd0) -
*(int*)(shell+0x80dc8)`, `ih = *(int*)(shell+0x80dd4) - *(int*)(shell+0x80dcc)` (the UI rect = InterfaceWidth/Height);
`P = GameClientShell_GetLocalPlayer(shell)` (`0x9e3570`, 0 when no world is loaded); `pl = P ? *(u8*)(P+0x431) :
0xff` (local player index as the mouse code passes it); `GV = *(*(im+0x2708+8)+0x50)` = m_GameUI's `CUIView`
(`FUN_00cb52e0`); `MV = *(GV+0x18)` = its `Scaleform::GFx::MovieImpl`.

## 0. Summary

| Question | Answer | Conf. |
|---|---|---|
| 1. How the mouse reaches Scaleform | Feral's Win32 emulation -> WndProc `FUN_008fcc00` -> engine system-event queue (`FUN_008b49c0`, types 14 down / 15 up / 16 move / 17 wheel) -> drained on the main thread by `FUN_008b4b90` -> `CGameSystemEventHandler::vf_07/08/09` (window-active gate) -> **CInterfaceMgr `FUN_00a0f760` (down) / `FUN_00a0f8c0` (up) / `FUN_00a0fa20` (move)** -> menu C++ (state machines, layers) -> if not consumed m_GameUI's `CUIView::vf_65/66/67` -> `MovieImpl::vf_36` HandleEvent (GFx MouseDown/Up/Move, viewport pixels) -> the movie's input queue, run at its next Advance | high |
| 1. Where to inject | **Call the three CInterfaceMgr functions directly** from somvr's input tick (game thread), with UI-surface pixels: no SDL, no X events, no window-focus gate, no Win32Mouse (so no switch to KBM mode, the pad glyphs stay), and the menu C++ that reads the stored mouse position (map drag, sliders, Nemesis) keeps working | high (path), med (calling context), RT |
| 2. Which event activates | Menu buttons: AS2 **onRelease** -> `FUN_00b4e610` -> a synthesized **MenuEnter (54)** to the item's layer. Checkboxes, sliders, list arrows: **onPress**. Dialog/prompt buttons: onRelease -> `FUN_00c04f80` -> the prompt's command ON+OFF. Nemesis screen: the **Mouse1 command (100)** + a C++ hit test at the stored position. No double-click anywhere | high |
| 2. Why clicks only highlight | The menu items' hover and click callbacks (`FUN_00b4e5c0`, `FUN_00b4e610`) **return early in pad mode whenever a local player exists** (`FUN_00a38e80` = profile "using gamepad"). The main menu before a world loads has no player, so attempt 3's clicks worked; run 1/2 clicked with a world loaded. An xdotool `click` (down+up in one frame) is invisible to Win32Mouse's per-frame button levels, so it never switches the game to KBM; mouse moves count only with a visible cursor. Dialog/prompt buttons have no such guard (run 3's Quit "Yes" click worked) | high (code), med (that run 2 was in pad mode: RT) |
| 3. Quad UV -> UI pixels | `x = u*iw + vx`, `y = v*ih + vy` with `(vx, vy) = *(int*)(GV+0x28), *(int*)(GV+0x2c)` (a display-mode offset, expected 0). m_GameUI's movie viewport is the whole UI surface (`CGameUI::vf_15`); GFx's stage scale/alignment is applied inside GFx for drawing and input alike; the safe zone (`CUIView+0x1d00..0x1d0c`) is a layout inset only | high (code), RT (offset 0, image orientation) |
| 4. Pad-style hover by element | Possible (focus `FUN_00b60930`, activate MenuEnter via `FUN_00b5f500`, per-clip hit test `CUIMovieClip::vf_109`), but needs per-component clip hit tests every frame and covers only CUIMenuLayer screens. The mouse route lets GFx pick for every screen the game wired for the mouse. **Use the mouse route for the laser, keep the pad passthrough for A/B/stick** | med |

## 1. The mouse path today (bottom to top)

### 1.1 Window messages and the system-event queue

| # | Step | Where | Conf. |
|---|---|---|---|
| M0 | Feral turns SDL mouse events into Win32 messages for the engine's WndProc **`FUN_008fcc00(hwnd, msg, wParam, lParam)`** (no static caller: called through the window class). Gate: byte `0x466cbe0` != 0 and the client object `*(void**)0x466aee0` != 0. `WM_LBUTTONDOWN/UP` -> queue(14/15, button 1, lParam); `WM_RBUTTON*` -> button 2, `WM_MBUTTON*` -> 3, `WM_XBUTTON*` -> 4/5 (`HIWORD(wParam)`); `WM_MOUSEMOVE` -> queue(16, lParam, 0); `WM_MOUSEWHEEL` -> queue(17, lParam, wParam); `WM_CAPTURECHANGED` releases every held button (mask byte `0x466cbe8`); `WM_LBUTTONDBLCLK` (0x203) is **not** queued. Every message also goes to `*(void**)0x467b718` vtbl+0x30 (the CWBInput device hook `FUN_00904aa0`) and to `FUN_01af3000` (default proc) | high |
| M0' | `FUN_00904aa0` feeds the `Win32Mouse` device (`dm+0x10`): buttons as **levels** at `mouse+0x12+i` (`FUN_009a7aa0`; 0x203 sets the same byte, `FUN_009a7ac0`), position `mouse+0x30/+0x34` (`FUN_009a7ae0`), gated by `dm+0x1cc`. The bindings (commands 100..105) and the KBM auto-switch (re-input.md 3.4) read these per frame | high |
| M1 | `FUN_008b49c0(q, type, a, b)` appends a 0x18-byte record `{u32 type, u64 a, u64 b}` to the vector `q+0xc78..+0xc88` (no lock). `FUN_008b4b90(q)` drains it once per frame (caller `FUN_008b50d0` <- the WinMain loop `FUN_008fe0b0`, main thread) through `ISystemEventHandler* h = (*(q+0xba0))->vf_12()` (`CGameClientShell::vf_12` = `shell+0x80e38`): type 14 -> `h->vf_07(a = button, &{(short)b, (short)(b>>16)})`, 15 -> `vf_08`, 16 -> `vf_09(&pos from a)`, 17 -> `vf_10(delta = (short)(b>>16), &pos from a)` (jump table `0x34479f0`) | high |
| M2 | `CGameSystemEventHandler` (vtable `0x34e41f8`) `vf_07` `0x9f4120` / `vf_08` `0x9f41c0` / `vf_09` `0x9f4260`: **drop** unless `h+0x12` (interface up: set after `CInterfaceMgr::Init` in `FUN_009ea000`, cleared in `CGameClientShell::vf_09`) and (`*(u8*)0x46a95c4 == 0` or `h+0x10`). `h+0x10` = window active (`vf_19` `0x9f3d50` sets it, `vf_18` `0x9f3d30` clears it); `0x46a95c4` = 1 from `_INIT` (`0x5700dc`). Then `pl` (or 0xff if `FUN_00a10ee0(im)` = front end, else drop) and a tail jump to M3 | high (code), med (flag meanings) |

### 1.2 CInterfaceMgr: routing to the menus and the movie

| Function | Args | What it does | Conf. |
|---|---|---|---|
| **`FUN_00a0fa20`** move | `(im, u8 pl, const int pos[2])` | stores `im+0x3210/+0x3214` = pos (pixels) and `im+0x3218/+0x321c` = pos / (iw, ih); MSM state 1 (front end) -> CMenuUI msg 12 (`FUN_00a23080`); state 3 (in game) -> player UI `FUN_00acbdc0(P+0x2e0)`; if not consumed -> `GV->vf_67(x, y)` | high |
| **`FUN_00a0f760`** down | `(im, u8 pl, int button, const int pos[2])` | event `{int button; int x; int y; u8 pl}`; state 1 -> CMenuUI msg 10 (`FUN_00a22fe0`), state 3 -> `FUN_00acbca0`; not consumed -> `GV->vf_65(button, x, y)` | high |
| **`FUN_00a0f8c0`** up | same | msg 11 (`FUN_00a23030`) / `FUN_00acbd30`; then `GV->vf_66` | high |
| `FUN_00a0fc00` wheel | `(im, u8 pl, int delta, const int pos[2])` | always the **message box** view `im+0x2a50` (`vf_68`), never m_GameUI: the menus get no Scaleform wheel | high |

- Interface MSM states 0, 2, 4 (start-up, loading, waiting for the world) drop the mouse (jump tables `0x34e7198`,
  `0x34e71c0`, `0x34e71e8`) **[high]**.
- A modal message box (`FUN_00a24410(*(shell+0xa8))` and `FUN_00a243f0`, the top message's `+0x24` byte) sends down,
  up and move straight to m_MessageBoxUI's view `im+0x2a50`, skipping the state machines **[high]**.
- **CMenuUI** (front end, `FUN_00a22400` = `CMenuUIStateMachine::vf_03`): only menu states **0xc** (manager at
  `MenuUI+0x1788`) and **0xf** (`CUIMainMenuMenuManager` at `MenuUI+0x158`) pass msgs 10/11/12 to
  `CUIMenuManagerBase::vf_12/13/14` (`0xb712f0/0xb71380/0xb71410`); most other states swallow them (return 1)
  **[high]**; which screen is which state: **[RT]** (U5).
- **Player UI** (`FUN_00acbca0` etc., `CPlayerUIStateMachine` at `P+0x2e0` -> `+0xb0`): state **5** (pause menus,
  manager at `sm+0x10`) -> `CUIMenuManagerBase::vf_12/13/14`; states 0, 2, 3, 4, 9, 10 swallow; the rest pass to
  the movie **[high code, RT state names]**.
- `CUIMenuManagerBase::vf_12/13/14` -> the top layer (`mgr+0x198 + 8*(n-1)`, `n = mgr+0x1e8`; or the override layer
  `FUN_00b70f00` when `mgr+0x1ec`) -> `FUN_00b5f5f0(layer, ev, down)` / `FUN_00b5f630` -> layer vtbl+0x70/+0x78/+0x80
  (slots 14/15/16). In `CUIMenuLayerBase` all three are `0xb5b980` (return 0). Only three layers use them:
  `CUIPDAZoneMapLayer` (left drag: down `0xbf1c10` latches `im+0x3210`, up `0xbf1b60`), `CUIPauseMenuControlBindingLayer`
  (key/mouse rebinding), `CUIPauseMenuTutorialLayer` (swallow). **Every other menu layer returns 0, so the event goes
  on to the movie** **[high]**.

### 1.3 CUIView -> GFx

`CUIView` (vtable `0x3482040`) input forwarders, all on the movie `MV = view+0x18` **[high]**:

| Slot | Address | GFx event |
|---|---|---|
| vf_65 | `0x90b6d0` | `(button, int x, int y)` -> MouseDown (type 2) at `(x - view+0x28, y - view+0x2c)`, `Button = table 0x34822d8[button]` (1 -> 0 left, 2 -> 1 right, 3 -> 2 middle, 4 -> 3, 5 -> 4), `MouseIndex 0`; then `MV->vtbl+0x138` HitTest(x, y, 2, 0), returned |
| vf_66 | `0x90b770` | the same with MouseUp (type 3) |
| vf_67 | `0x90b810` | `(short x, short y)` -> MouseMove (type 1) |
| vf_68 | `0x90b870` | `(short x, short y, int delta)` -> MouseWheel (type 4) |
| vf_69/vf_70 | `0x90b8e0/0x90b920` | KeyDown/KeyUp (5/6) |
| vf_71 | `0x90b960` | `MV->GetMouseState(0)` -> `{x, y}` in xmm0 (read-back for probes) |
| vf_72/vf_73 | `0x90b990/0x90b9c0` | SetFocus/KillFocus (8/9); **no caller found** (direct or indirect `0x240/0x248` calls) |

GFx 4 `MovieImpl` (vtable `0x3763da0`) **[high]**:
- **vf_36 `0x124a3d0` HandleEvent(const Event&)**. Event = `{u32 Type; u8 Modifiers; ...}`; MouseEvent = `+0x08 float x,
  +0x0c float y, +0x10 float ScrollDelta, +0x14 u32 Button, +0x18 u32 MouseIndex`. While the movie's focus bit
  (`MV+0x52d0` bit 18) is clear only SetFocus (8) is handled; the constructor `FUN_012519d0` sets it (`|= 0x40180`) and
  the game never sends KillFocus, so the movies stay focused. Mouse events need `MouseIndex < MV+0x23d8` (cursor count,
  1 at construction; set by vf_44 `0x1253a70`, never called by game code). The point is mapped by the inverse of
  `MV+0x110` (`FUN_011da9d0`) and queued in `MV+0x268` (move `FUN_01224b30`, button `FUN_01224b70` with flag `0x80`
  = up, wheel `FUN_01224c40`); the queue is processed at the movie's next Advance (`CUIView::vf_04` -> vtbl+0xc8,
  re-ui-effects.md 1.2).
- vf_37 `0x1242bb0` GetMouseState, vf_38 `0x124ac40` NotifyMouseState(x, y, buttons, idx) (absolute state, diffed into
  the same queue), vf_39 `0x124ad20` HitTest. Mouse state per cursor: `MV+0x21f8 + 0x50*i` (`+0x30` buttons, `+0x38/+0x3c`
  position in twips; GetMouseState returns `(twips*0.05 - MV+0xe0) / MV+0xd8`, `(.. - MV+0xe4) / MV+0xdc`).

### 1.4 From AS2 back into C++: who acts on a click

`CUIMovieClip::vf_90..vf_95` (`0x982400..0x982720`) attach `onPress`, `onRelease`, `onReleaseOutside`, `onRollOver`,
`onRollOut`, `onMouseMove` as `CGFxFunction` objects (`FUN_009821d0`) calling `fn(clip, args, ctx)` **[high]**. The
attachments in the menu code (scan of every `call *0x2d0..0x2f8` with its `esi` constant):

| Element | Attached by | Hover | Activation | Conf. |
|---|---|---|---|---|
| Menu button (`CUIMenuComponentButton`) | ctor `FUN_00f9cfc0` | onRollOver -> **`FUN_00b4e5c0`**: `FUN_00b60930(item+0x228 (layer), item, 1)` = focus the item | **onRelease** -> **`FUN_00b4e610`**: `FUN_00eb8540(ev, 0x36 MenuEnter, 0, 0)`, `FUN_00b5f500(layer, ev, 1)` = MenuEnter on the layer | high |
| Checkbox (`CUIMenuComponentCheckbox`) | ctor `FUN_00f9f110` | same `FUN_00b4e5c0` | **onPress** -> `FUN_00b4e610` | high |
| Slider (`CUIMenuComponentSlider`) | `FUN_00b570d0` | Value/Label clips onRollOver `FUN_00b4e5c0` | onPress on track, thumb, arrows (`FUN_00b5ad80`, `FUN_00b5ae40`, `FUN_00b5b050`, ...), which read `im+0x3218` | high |
| List scroll arrows | `FUN_00b5d8b0`, `FUN_00b67fc0`, `FUN_00b68620` | "Arrow" in/out | onPress -> step (`FUN_00b649e0`/`FUN_00b64a70`), onRelease empty | high |
| Dialog / prompt-bar button | `FUN_00c08720`, `FUN_00c099c0` | onRollOver `FUN_00c04e60` plays "in" (no guard) | **onRelease** -> `FUN_00c04f80`: the prompt's callback `+0x148`, then its command `+0x08` ON and OFF through the active user's PlayerInput (`FUN_00c40c20(shell+0x10)+0x10`, `FUN_00aab660`/`FUN_00aab5d0`); nothing if no user (`shell+0x10` index 6) | high |
| Nemesis hierarchy (`CUIHierarchyLayer::vf_10` `0xedf470`) | - (C++) | - | the **commands 100/101 (Mouse1/Mouse2)** first: `FUN_00b6fd70`/`FUN_00b72240(layer+0x308, *(u64*)(im+0x3210))` -> clip view (`vtbl+0x3c0`), pixels -> stage (`FUN_00c83ba0`), clip hitTest (`vtbl+0x368` = `CUIMovieClip::vf_109`) | high |
| World/zone map (`CUIPDAZoneMapLayer`) | - (C++ layer slots 14/15) | - | left drag from `im+0x3210` | high |

Both menu-item callbacks start with the **pad guard**: `if (P && FUN_00a38e80(P)) return;` (calls at `0xb4e5d8` and
`0xb4e62c`). `FUN_00a38e80(P)` = `FUN_00cbe650(profile of P+0x431)` = profile `+0x238` "using gamepad" (no profile ->
1) **[high]**. No other AS2 callback calls `FUN_00a38e80` (its 29 callers checked) **[high]**.

### 1.5 The cursor

`CursorMgr` (`FUN_00e6bf30`, set from `FUN_00cbe610` when the input mode changes; `FUN_00e6bdb0`) only drives the OS
cursor through `IEngineCursor::vf_03` (`*(*(void**)0x466afa8)->vtbl+0x78`) **[high]**. Nothing in HandleEvent or the
forwarders tests the cursor, so a hidden cursor does not block Scaleform input **[high]**. The cursor matters only for
re-input.md 3.4 (mouse moves count as KBM activity only while it is visible).

## 2. Why clicks only highlight (Q2)

1. **The pad guard** (1.4) **[high]**: with a world loaded (a local player exists, including the pause menu and every
   in-game dialog) and the profile in pad mode, hover does not move the menu focus and a release does not send
   MenuEnter. With no world (the main menu after boot) the guard is skipped: attempt 3's main-menu clicks (OPTIONS,
   Benchmark) worked; run 1 clicked the benchmark results with a benchmark player alive (H1 ran behind them) and run 2
   clicked after loading **[med: their pad state is RT]**.
2. **The harness never leaves pad mode** **[high code, med as the cause]**: `xdotool click` sends down and up back to
   back; Win32Mouse keeps button levels sampled once per frame (M0'), so the click shows no change to
   `FUN_00a13790`/`FUN_00a0d830` and is not KBM activity; mouse moves are KBM activity only with the cursor visible,
   and pad mode hides it. Why the profile was in pad mode (saved setting, Steam Input's virtual pad) is not decidable
   statically **[RT: `mouselog`, section 6]**.
3. **What "highlight" was** **[med]**: GFx's own button states and the guard-free "in"/"down" animations of prompt
   buttons, or the focus the keyboard Downs had put there. GFx itself does receive the click.
4. **Dialog buttons work without the guard** (`FUN_00c04f80`): run 3's in-world Quit "Yes" by click worked; a click
   on one does nothing only when no user is active **[high code]**.
5. Not used: double-click (0x203 does not reach the UI queue, so a fast second click loses its press there **[high]**);
   the wheel in menus (1.2) **[high]**.
6. Press and release in one frame: GFx queues both and processes them in order at the next Advance **[med]**; send them
   at least one frame apart anyway (the trigger does).

Harness fix (no code): `xdo mousedown 1` and `xdo mouseup 1` on separate lines with a `wait 1` between them (a visible
button change switches the game to KBM; the next click then activates), or keep using Return **[med]**.

## 3. The injection (Q1)

### 3.1 Recommended: R1, the CInterfaceMgr calls from the input tick

On the game thread, inside somvr's input tick (IN_TICK, `CWBInput` slot 4 post: the same context in which the command
callbacks offer events to the UI, `FUN_00a15330`) **[med]**:

```c
shell = *(void **)0x46a9560;  im = *(void **)(shell + 0x80ff8);
P = GameClientShell_GetLocalPlayer(shell);                         /* 0x9e3570 */
if (P) pl = *(uint8_t *)(P + 0x431); else if (FUN_00a10ee0(im)) pl = 0xff; else skip;   /* as vf_07..09 do */
int pos[2] = { lround(u * iw) + vx, lround(v * ih) + vy };          /* 4 */
moved >= 1 px since the last call:  FUN_00a0fa20(im, pl, pos);      /* hover, im+0x3210 */
trigger down edge:   FUN_00a0fa20(im, pl, pos); FUN_00a0f760(im, pl, 1, pos);
trigger up edge:     FUN_00a0f8c0(im, pl, 1, pos);                  /* >= 1 frame after the down */
ray leaves the panel: (trigger held -> the up first) FUN_00a0fa20(im, pl, {-1000, -1000});  /* GFx onRollOut */
```

plus **the guard patch**: `LP_GUARD_HOVER` (`0xb4e5d8`) and `LP_GUARD_CLICK` (`0xb4e62c`), call -> our stub = `return
pointer_live ? 0 : FUN_00a38e80(P)`. Everything else (glyphs, combat map, `0x46b0d61 = 0`, the forced pad mode of
re-xr-input.md 3.2) stays as it is.

Why R1 **[high unless marked]**:
- no SDL, X or window-focus dependency: it starts below M2, so an unfocused game window (likely in VR) does not drop
  the laser;
- no Win32Mouse activity: no auto-switch to KBM, no keyboard glyphs (design-vr.md 7's glyph problem disappears);
- the C++ that reads `im+0x3210/+0x3218` (zone map drag, sliders, Nemesis hit tests) sees the laser's position;
- the routing (message boxes first, then menu state machines, then the movie) is the game's own.

Extras on top of R1:
- **Nemesis screen**: after the position update, the trigger also presses command **100 (Mouse1)** through the
  existing A2/A3 path (`FUN_00aab660`/`FUN_00aab5d0`, re-input.md 8) **[high: CUIHierarchyLayer::vf_10 handles 100/101
  before anything else]**; whether 100 is in that screen's command set is irrelevant (the UI gets ON events first).
- **Real mouse while in VR** (optional): `LP_SYS_DOWN/UP/MOVE` vslot hooks that drop the desktop mouse while the
  pointer is live, so a stray mouse cannot fight the laser.
- **Scrolling**: no Scaleform wheel for menus (1.2); the right stick through the pad passthrough (design-vr.md 7).

### 3.2 Alternatives (not recommended)

| Route | Why not | Conf. |
|---|---|---|
| R2 queue records with `FUN_008b49c0(*(void**)0x466aee0, 14/15/16, ...)` | identical to a real mouse: needs the window-active gate (`shell+0x80e48`) and the main-thread queue; still hits the pad guard | high |
| R3 `SDL_PushEvent`/warp (design-vr.md 7, U6) | R2's gate plus Feral's coordinate handling and Win32Mouse (KBM switch unless `0x46b0d61 = 0`); still hits the pad guard | high (path), RT (Feral's scaling) |
| R4 `CUIView::vf_65..67` or `MovieImpl::vf_36/vf_38` directly | skips the menu state machines and `im+0x3210`: no message-box routing, no map drag, no Nemesis; fine for a probe | high |

## 4. Quad UV -> UI pixels (Q3)

- The menu panel shows the UI surface (`dev+0x1b8`, `iw x ih`; re-hud.md 1.4). m_GameUI's (and m_MessageBoxUI's)
  viewport is set by `CGameUI::vf_15` `0xe3f9f0` / `CMessageBoxUI::vf_15` `0xd9ddf0` -> `CUIView::vf_15` `0x90c2f0`:
  buffer `{iw, ih}`, rect `{0, 0, iw, ih}`, scissor the same, flags 4, scale 1 -> **the movie covers the whole UI
  surface** **[high]**. `view+0x28/+0x2c` = `*(int*)(*(void**)0x466ca50 + 0xb0/+0xb4)` (the display mode's offset)
  + rect left/top (0) **[high]**, expected 0 **[RT]**.
- The forwarders subtract `view+0x28/+0x2c`, so pass `x = u*iw + vx`, `y = v*ih + vy` (`u` right, `v` down, origin =
  the UI image's top-left; `vx, vy` read from `GV`) and GFx sees `(u*iw, v*ih)` **[high]**. If the panel shows only a
  sub-rectangle (`imageRect`), map through it first.
- The Flash stage's scale mode and alignment live inside GFx (`MV+0x110`, `+0xd8..+0xe4`); drawing and input use the
  same matrix, so nothing to correct on our side **[high]**.
- **Safe zone**: `CUIView+0x1d00..+0x1d0c` (four fractions, `CUIPauseMenuSafeZoneOptionsLayer`) is read only by
  `CUIView::vf_17/vf_18` (the visible frame for layout); it moves the elements inside the surface, not the coordinates.
  Ignore it in the mapping **[high]**.
- **UI scale**: none besides `iw/ih` (`SetResolution <sw> <sh> <iw> <ih>`); `im+0x3218` = pos / (iw, ih) without the
  offset, consistent while the offset is 0 **[high]**.
- Check at run time: the image orientation of the layer's copy (a corner hit gives `(0,0)`/`(iw,ih)` in `mstate`)
  **[RT]**.

ABI need (proposal for re-xr-input.md 2; **made 2026-10-20** as `somvr_input` v2's pointer block, re-xr-pipeline.md
9.2, with the click edges as counters the layer owns): the layer publishes the panel hit per frame, e.g. `float ptr_u,
ptr_v; u32 ptr_flags (bit 0 on the menu panel, bit 1 the trigger); u32 ptr_seq` in `somvr_input`; somvr owns the
edges and the calls (3.1).

## 5. The pad-style route with hover by element (Q4)

What exists **[high]**:
- focus: `FUN_00b60930(layer, component, 1)` (what onRollOver does) -> `FUN_00b60810(layer+0x318, index, 1)`;
  current index `layer+0x64c`, components `layer+0x650..+0x658` (`CUIMenuComponent*`, clip `+0x28`, layer `+0x228`);
- activate: MenuEnter as `FUN_00b4e610` builds it (`FUN_00eb8540` + `FUN_00b5f500(layer, ev, 1)`), or the pad A;
- hit test per clip: `clip->vtbl+0x368` (`CUIMovieClip::vf_109`) at stage coordinates from `FUN_00c83ba0(view, x, y)`,
  as the Nemesis layer does.

Missing: a loop over the top layer's components every frame (the clip list per layer type, scrolling lists, the
override layer), and nothing for dialogs, prompts, sliders or the map **[med]**.

| | R1 mouse route | Element route |
|---|---|---|
| Picking | GFx, every clip the game wired (buttons, checkboxes, sliders, arrows, dialog buttons, map drag, Nemesis via Mouse1) | our loop, menu layers only |
| Patches | 2 call sites + 3 calls | calls only, but a per-layer walk |
| Fails where | screens whose state machine swallows the mouse (1.2) -> pad passthrough | every non-layer screen -> pad passthrough |
| Focus in step with the pad | yes (hover = the same focus call) | yes |

**Recommendation**: R1 for the laser; the passthrough pad stays on for A (confirm), B (back), the sticks and the
screens that swallow the mouse. The element route is the fallback if the guard patch misbehaves **[med]**.

## 6. Patch points and check data

Patch (R1): `LP_GUARD_*` (call -> stub). Optional: `LP_SYS_*` (vslot, drop the desktop mouse while the pointer is
live). Probe-only read hooks: `LP_VIEW_*`, `LP_GFX_EVENT` (vslot, log). The rest are checked functions somvr or the
probe calls (K_ENTRY = entry bytes only, nothing patched there).

```c
SITE("LP_GUARD_HOVER", K_CALL, 0xb4e5d8, 0xb4e5c8, "48 89 d3 e8 a0 4f e9 ff 48 85 c0 74 0c 48 89 c7 e8 a3 a8 ee ff", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("LP_GUARD_CLICK", K_CALL, 0xb4e62c, 0xb4e61c, "af b5 03 e8 4c 4f e9 ff 48 85 c0 74 0c 48 89 c7 e8 4f a8 ee ff", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("LP_SYS_DOWN", K_VSLOT, 0x34e4230, 0x34e4230, "20 41 9f 00 00 00 00 00", 0x9f4120, "80 3d 9d 54 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("LP_SYS_UP", K_VSLOT, 0x34e4238, 0x34e4238, "c0 41 9f 00 00 00 00 00", 0x9f41c0, "80 3d fd 53 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("LP_SYS_MOVE", K_VSLOT, 0x34e4240, 0x34e4240, "60 42 9f 00 00 00 00 00", 0x9f4260, "80 3d 5d 53 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("LP_VIEW_DOWN", K_VSLOT, 0x3482248, 0x3482248, "d0 b6 90 00 00 00 00 00", 0x90b6d0, "53 48 63 f6 66 0f ef c0 66 0f ef c9 48 89 fb"),
SITE("LP_VIEW_UP", K_VSLOT, 0x3482250, 0x3482250, "70 b7 90 00 00 00 00 00", 0x90b770, "53 48 63 f6 66 0f ef c0 66 0f ef c9 48 89 fb"),
SITE("LP_VIEW_MOVE", K_VSLOT, 0x3482258, 0x3482258, "10 b8 90 00 00 00 00 00", 0x90b810, "48 83 ec 28 66 2b 57 2c 66 0f ef c0 66 2b 77"),
SITE("LP_GFX_EVENT", K_VSLOT, 0x3763ec0, 0x3763ec0, "d0 a3 24 01 00 00 00 00", 0x124a3d0, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 20 48"),
SITE("LP_IM_DOWN", K_ENTRY, 0xa0f760, 0xa0f760, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53 48", 0xa0f760, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53"),
SITE("LP_IM_UP", K_ENTRY, 0xa0f8c0, 0xa0f8c0, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53 48", 0xa0f8c0, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53"),
SITE("LP_IM_MOVE", K_ENTRY, 0xa0fa20, 0xa0fa20, "41 54 66 0f ef c9 49 89 d4 66 0f ef c0 55 89 f5", 0xa0fa20, "41 54 66 0f ef c9 49 89 d4 66 0f ef c0 55 89"),
SITE("LP_IM_WHEEL", K_ENTRY, 0xa0fc00, 0xa0fc00, "48 8b 05 59 99 c9 03 41 54 41 89 d4 55 48 89 cd", 0xa0fc00, "48 8b 05 59 99 c9 03 41 54 41 89 d4 55 48 89"),
SITE("LP_LOCALPLAYER", K_ENTRY, 0x9e3570, 0x9e3570, "53 48 8d 9f a0 00 00 00 48 8d b7 80 00 00 00 48", 0x9e3570, "53 48 8d 9f a0 00 00 00 48 8d b7 80 00 00 00"),
SITE("LP_IFS_FRONTEND", K_ENTRY, 0xa10ee0, 0xa10ee0, "48 8b 17 8b 42 0c 83 f8 ff 0f 44 42 08 83 f8 01", 0xa10ee0, "48 8b 17 8b 42 0c 83 f8 ff 0f 44 42 08 83 f8"),
SITE("LP_PADMODE", K_ENTRY, 0xa38e80, 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31 04", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("LP_ITEM_HOVER", K_ENTRY, 0xb4e5c0, 0xb4e5c0, "53 48 8b 3d 98 af b5 03 48 89 d3 e8 a0 4f e9 ff", 0xb4e5c0, "53 48 8b 3d 98 af b5 03 48 89 d3 e8 a0 4f e9"),
SITE("LP_ITEM_CLICK", K_ENTRY, 0xb4e610, 0xb4e610, "53 48 89 d3 48 83 ec 10 48 8b 3d 41 af b5 03 e8", 0xb4e610, "53 48 89 d3 48 83 ec 10 48 8b 3d 41 af b5 03"),
SITE("LP_PROMPT_CLICK", K_ENTRY, 0xc04f80, 0xc04f80, "48 85 d2 74 6b 55 53 48 89 d3 48 83 ec 08 48 8b", 0xc04f80, "48 85 d2 74 6b 55 53 48 89 d3 48 83 ec 08 48"),
SITE("LP_NEMESIS_CMD", K_ENTRY, 0xedf470, 0xedf470, "41 56 49 89 fe 41 55 41 54 55 53 0f bf 06 48 89", 0xedf470, "41 56 49 89 fe 41 55 41 54 55 53 0f bf 06 48"),
SITE("LP_FOCUS_ITEM", K_ENTRY, 0xb60930, 0xb60930, "8b 87 c4 02 00 00 85 c0 75 66 48 85 f6 74 61 80", 0xb60930, "8b 87 c4 02 00 00 85 c0 75 66 48 85 f6 74 61"),
SITE("LP_LAYER_CMD", K_ENTRY, 0xb5f500, 0xb5f500, "8b 87 c4 02 00 00 85 c0 0f 85 82 00 00 00 0f b6", 0xb5f500, "8b 87 c4 02 00 00 85 c0 0f 85 82 00 00 00 0f"),
SITE("LP_MAKE_CMDEV", K_ENTRY, 0xeb8540, 0xeb8540, "66 89 37 89 57 04 88 4f 08 c7 47 0c 00 00 00 00", 0xeb8540, "66 89 37 89 57 04 88 4f 08 c7 47 0c 00 00 00"),
SITE("LP_VIEW_TOSTAGE", K_ENTRY, 0xc83ba0, 0xc83ba0, "55 53 48 83 ec 28 48 85 ff 74 18 48 8b 07 89 d5", 0xc83ba0, "55 53 48 83 ec 28 48 85 ff 74 18 48 8b 07 89"),
SITE("LP_CLIP_HITTEST", K_ENTRY, 0x979db0, 0x979db0, "55 48 89 f8 66 0f ef c0 41 b9 04 00 00 00 f2 0f", 0x979db0, "55 48 89 f8 66 0f ef c0 41 b9 04 00 00 00 f2"),
SITE("LP_VIEW_MSTATE", K_ENTRY, 0x90b960, 0x90b960, "48 83 ec 18 48 8b 7f 18 31 f6 48 8d 4c 24 0c 4c", 0x90b960, "48 83 ec 18 48 8b 7f 18 31 f6 48 8d 4c 24 0c"),
SITE("LP_VIEW_VIEWPORT", K_ENTRY, 0x90c2f0, 0x90c2f0, "53 48 89 fb 48 83 ec 40 48 8b 3d 51 07 d6 03 c7", 0x90c2f0, "53 48 89 fb 48 83 ec 40 48 8b 3d 51 07 d6 03"),
SITE("LP_GAMEUI_VIEWPORT", K_ENTRY, 0xe3f9f0, 0xe3f9f0, "48 83 ec 38 48 8b 0d 65 9b 86 03 48 89 f7 48 c7", 0xe3f9f0, "48 83 ec 38 48 8b 0d 65 9b 86 03 48 89 f7 48"),
SITE("LP_GFX_HITTEST", K_ENTRY, 0x124ad20, 0x124ad20, "41 56 41 89 d6 41 55 49 89 fd 48 81 c7 10 01 00", 0x124ad20, "41 56 41 89 d6 41 55 49 89 fd 48 81 c7 10 01"),
SITE("LP_GFX_NOTIFYMOUSE", K_ENTRY, 0x124ac40, 0x124ac40, "41 57 41 56 41 55 41 89 d5 41 54 41 89 f4 55 53", 0x124ac40, "41 57 41 56 41 55 41 89 d5 41 54 41 89 f4 55"),
SITE("LP_SYSQ_PUSH", K_ENTRY, 0x8b49c0, 0x8b49c0, "48 b8 00 00 00 00 ff ff ff ff 41 57 41 56 41 55", 0x8b49c0, "48 b8 00 00 00 00 ff ff ff ff 41 57 41 56 41"),
SITE("LP_SYSQ_DRAIN", K_ENTRY, 0x8b4b90, 0x8b4b90, "48 b9 ab aa aa aa aa aa aa aa 41 56 41 55 41 54", 0x8b4b90, "48 b9 ab aa aa aa aa aa aa aa 41 56 41 55 41"),
SITE("LP_WNDPROC", K_ENTRY, 0x8fcc00, 0x8fcc00, "41 57 41 56 41 55 49 89 cd 41 54 49 89 d4 55 48", 0x8fcc00, "41 57 41 56 41 55 49 89 cd 41 54 49 89 d4 55"),
```

Collisions: `LP_GUARD_*` patch calls inside two small callbacks no other doc hooks; the vslots are new. The probe's
command hooks (`P_*`, A1-A3) are untouched; `mcmd` (below) uses A2/A3 as callers only.

## 7. Runtime probe words to propose for `mod/probe` (not implemented here)

| Word | Kind | Does | Answers |
|---|---|---|---|
| `mouselog [on\|off]` | read-only (LP_SYS_*, LP_VIEW_*, LP_GFX_EVENT; call-through) | one line per event: `MOUSE src=sys\|view\|gfx type btn x y`, plus at `sys`: `h+0x10`, `h+0x12`, `0x46a95c4`, interface state, menu state, player-UI state, message-box flags, `P`, `FUN_00a38e80(P)`, user index `*(int*)(shell+0x10)`; at `view`: which view (`GV` or the message box) and the HitTest result; at `gfx`: the movie, focus bit, `+0x23d8` | why harness clicks fail (2), which states swallow (1.2), Steam/pad state |
| `mstate` | read-only | `im+0x3210..+0x321c`, `GV+0x28/+0x2c`, `iw ih`, `CUIView::vf_71` (the movie's own mouse position), `MV+0x52d0` bit 18, `MV+0x23d8` | the mapping (4), the display offset |
| `pointer X Y` | active | R1 move: `FUN_00a0fa20(im, pl, {X, Y})` on the game thread (the probe's exec context) | hover reaches the movie |
| `click [BTN] [FRAMES]` | active | R1 down now, up after FRAMES (default 2) at the last `pointer` | activation |
| `padguard off\|on` | active | `LP_GUARD_HOVER`/`LP_GUARD_CLICK` -> a stub returning 0 / restored | the guard (2.1) |
| `mcmd CMD` | active | command CMD ON, next frame OFF through A2/A3 (100 = Mouse1) | Nemesis picks |
| `hitscan` | read-only | for the top menu layer: each component's clip `vf_109` at the last `pointer` (via `FUN_00c83ba0`) | the element route (5) |

Run plan (one boot, ~6 min, pass/fail):
1. `menu`: `mouselog on; mstate; pointer <OPTIONS>; click` -> Options opens (no player: no guard). A corner `pointer
   0 0` / `pointer iw ih` in `mstate` -> the movie position matches.
2. Load, `world`, Escape (pause menu): `pointer <item>; click` with `padguard on` -> only a highlight (reproduces run 2;
   `mouselog` shows `FUN_00a38e80 = 1`); `padguard off; click` -> the item opens.
3. Quit dialog: `pointer <Yes>; click` (no guard needed) -> closes.
4. The map: `pointer A; click 1 30` while `pointer` moves -> the map pans.
5. Sauron's Army: `pointer <captain>; mcmd 100` -> the captain is selected.
6. Harness check: `xdo click 1` vs `xdo mousedown 1` / `wait 1` / `xdo mouseup 1` with `mouselog` on -> the first shows
   no Win32Mouse change, the second switches to KBM.

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results).** The words
were built into `mod/probe` and run. Talion's death in the fight (the save copy only) shifted the script, so step 2
ran with no menu open, and step 1's click ran in the pause menu instead of the main menu.
- **R1 hovers:** at the main menu `pointer` -> `MOUSE view move (m_GameUI) result=0x3` + `MOUSE gfx move`. OPTIONS,
  then PLAY lit up with their descriptions. In the pause menu, OPTIONS and then QUIT lit up.
- **R1 clicks:** in the pause menu (KBM mode) `click` on QUIT -> `view down/up result=0x1`, `MOUSE guard click P=...
  pad=0 -> 0`, and the Quit Game dialog opened. This fits 2.1: in KBM mode the guard answers 0 and the click acts.
- **The pad guard (2, `padguard`): not tested.** The `padmode on` hover/click and the `padguard off` click ran during
  the death screens (`player-ui=1`, no menu; view down result 0).
- **The mapping (4):** `GV+0x28/+0x2c` = (0, 0). The movie's own mouse equals our `im+0x3210` pixels (one word later)
  at (0, 0), (1919, 1079) and (960, 540): 1:1, no flip, UI 1920x1080. The GFx stage is 1280x720 (`hitscan`'s stage
  point = pixels x 2/3).
- **`hitscan` (5):** 6 components in both menus. The hit was the item under the pointer (main menu: component 4 =
  OPTIONS, focus 0 = PLAY; pause menu: 5 = QUIT, focus 4 = OPTIONS). In the world it fails cleanly (player UI state 1,
  0 layers).
- Not covered: the system path's gates (no harness mouse move while `mouselog` was on), `mcmd`, the map, the quit
  dialog's Yes by click.
- Next: steps 1 (main menu click) and 2 (pause menu in pad mode, the guard) with no fight before them.

## 8. design-vr.md markers this resolves (edits proposed, not made)

- Section 7, "**needs RE: the Scaleform mouse feed**, re-input.md 7": resolved by 1 and 3 (R1).
- Section 7, "**RT: that the menus take the pushed events**" and the `SDL_PushEvent` plan: replaced by R1 (no SDL); the
  RT becomes the run plan in 7. U6 (runtime-probes.md, pushed SDL mouse) is superseded by `pointer`/`click`.
- Section 7, "the mouse turns the prompts to keyboard glyphs ... either hold pad mode or the overlay shows controller
  hints": with R1 the game stays in pad mode (no Win32Mouse activity); pad glyphs stay.
- Also closed: re-input.md 7 "Not mapped yet: the function that passes the mouse to the Scaleform movie" and 9 "The
  Scaleform mouse feed"; re-ui-effects.md 5 "Not found statically: the call that feeds the cursor position" and 8
  "Where the cursor position enters the GFx::Movie"; re-xr-input.md 3.5 "the Scaleform mouse feed (re-input 7, needs
  RE)" and its U6 dependency.

Corrections to earlier docs **[high]**:
- re-ui-effects.md 5: "The other MenuUI messages (8, 9, 0xb, 0xe, 0xf; `FUN_00a22f40` ... `FUN_00a23080`) come from
  `CGameSystemEventHandler::vf_05..09` (system events), not from the mouse." Menu MSM messages **10/11/12**
  (`FUN_00a22fe0`/`FUN_00a23030`/`FUN_00a23080`) **are** the mouse (down/up/move from `vf_07/08/09` through
  `FUN_00a0f760`/`FUN_00a0f8c0`/`FUN_00a0fa20`); 8/9 (`FUN_00a22f40`/`FUN_00a22f90`) are key down/up (`vf_05/06` via
  `FUN_00a0f340`/`FUN_00a0f4d0`).
- re-ui-effects.md 0/1.1 says `UI.Draw 0` "keeps the menus": the menus get the mouse through m_GameUI's view (the only
  movie that receives down/up/move outside modal message boxes), so their clips are most likely in m_GameUI's movie
  **[med; RT with `UI.Draw 0` in a menu]**.

## 9. Open items

- Which CMenuUI states are 0xc and 0xf, and the player-UI states 0/2/3/4/9/10 that swallow the mouse **[RT, `mouselog`
  + U5]**.
- The display offset `GV+0x28/+0x2c` (expected 0) and the layer image's orientation **[RT, `mstate`]**.
- Calling the IM functions from IN_TICK (inside `CWBInput::vf_04`): the same context as the command events, so expected
  safe **[med, RT]**.
- Press and release in one frame **[med]** (avoid it).
- `FUN_00c04f80` needs an active user (`shell+0x10` != 6) **[RT: is there one at the boot dialogs]**.
