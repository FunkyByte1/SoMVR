# Shadow of Mordor (Feral Linux build): input and actions - the map for OpenXR controllers and physical combat

Static analysis only (2026-10-18): Ghidra project copy `som-linux-q3` (read-only queries through
`scripts/ghidra_query.sh`), the TSV indexes in `reference/index/`, `objdump`/`readelf` on `reference/bin/linux/ShadowOfMordor`.
Nothing was launched, the install was not touched. `SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`.
Scratch (git-ignored): `$SOM/reference/work-input/` - batch files `bN.txt`, raw outputs `oN.txt`, `commands.tsv` (the full
command table), `tools/va.py` (read bytes/strings at a VA), `tools/cmdtable2.py` (rebuilds `commands.tsv` from `o1.txt`).

Conventions are those of `docs/re-gameplay.md` (addresses = runtime VAs of the non-PIE ELF, `FUN_xxxxxxxx` = Ghidra's name,
`Class::vf_NN` = vtable slot NN, `+0x2d0` = byte offset, SysV calling convention). Confidence: **[high]** read in the
decompile/disassembly, unambiguous; **[med]** read in the code, meaning inferred from strings or call pattern; **[low]**
inference. "Runtime check" marks what needs a boot to confirm. This doc builds on `re-gameplay.md` section 3 (Input) and
does not repeat it; it corrects one point there: the struct `CSDLController::vf_07` fills is Feral's 0x40-byte pad state,
not a bare `XINPUT_GAMEPAD` (section 1.2).

## 0. Summary

| Need | Where | Conf. |
|---|---|---|
| Pipeline | SDL 2.0.8 -> `CSDLController::vf_07` -> Feral `XInputGetState` (`FUN_01a8c450`, leave untouched) -> "Gamepad Update Thread" `FUN_00903e20` (16 ms) -> `FUN_009044e0` -> `Win32GamePad` (ids 2000..2019) -> `CWBInput` bindings -> `PlayerInput` callback `FUN_00ab2af0` -> command ON `FUN_00aab660` / OFF `FUN_00aab5d0` -> UI, then Player/PlayerCombat | high |
| Action ids | 349 commands, table `FUN_025e1450` at `0x4c3a3c0` (section 2); combat = `CombatPrimary/Secondary/Tertiary*`, `SpecialAttack*` (pad), `CombatKBM01..20` (KBM) | high |
| Default bindings | in the database, not in the binary; one read-only runtime dump gets all (2.2) | high |
| Virtual gamepad | V1 uinput Xbox 360 pad (no game hook); V2 SDL interposition / V3 `CSDLController::vf_07` slot on top of a device | high game side |
| Fire / suppress actions | `FUN_00aab660`/`FUN_00aab5d0`; suppress in `FUN_00ab2af0` | high |
| Analog move | `FUN_00ad0af0` returns the move stick `{strafe, forward}` | high |
| Pad vs KBM | auto switch `FUN_00a13790`; bytes `0x46b0d60`/`0x46b0d61`; switch `FUN_00a0eeb0` | high |
| Camera-relative move | `FUN_00a3c350` (called from `FUN_00a3c8d0` at `0xa3d232`) | high |
| Freeflow targeting | context `FUN_00a97690`/`FUN_00a97550` (stick dir `ctx+0x10`), geometry `FUN_0274ed10`, score `FUN_00a941b0`, set `FUN_026d8190` | high |
| Rumble | `CRumbleFX` -> `FUN_00d8b230` -> `FUN_00a5ad40` -> `FUN_00aaa450(PlayerInput*, ch, v)`; ch0 = left/large, ch1 = right/small -> SDL LEFTRIGHT | high |
| Menus | pad = Menu* commands; mouse = Scaleform at the SDL mouse position (`SDL_GetMouseState` in `FUN_02046500`/`FUN_020465b0`) | med |

## 1. The pipeline: SDL -> Feral -> engine devices -> commands -> game

### 1.1 Overview (bottom to top)

| # | Layer | Thread | Where | Conf. |
|---|---|---|---|---|
| L0 | SDL 2.0.8, bundled (`DT_NEEDED libSDL2-2.0.8.so`, `RPATH $ORIGIN/../lib`); no virtual-joystick API in this SDL version (that came in 2.0.14) | SDL / main | PLT imports: `SDL_GameControllerGetButton` `0x412890`, `..GetAxis` `0x411fe0`, `..GetAttached` `0x4142f0`, `..Open` `0x414450`, `SDL_HapticUpdateEffect` `0x413d40`, `SDL_HapticRunEffect` `0x413f90` | high |
| L1 | Feral device manager `CFeralInputDeviceManager` (singleton `FUN_01fdca30()` = `&DAT_04b4c440`); its controller list (`std::vector<CFeralController*>` at `+0x90`) is indexed by XInput user index. Device added: `FUN_02048930` (`SDL_GameControllerOpen`, `SDL_GameControllerAddMapping` from `share/inputdevices.json`); removed: `FUN_020472b0`; lookup by index: `FUN_02047410(mgr, idx)` (under a mutex) | event loop | `CSDLController` vtable `0x4513130`, typeinfo `0x4592a40` | high |
| L2 | Feral's XInput emulation for the Windows-origin engine (registered by name in `_INIT_1072`/`_INIT_1073`): `XInputGetState` = `FUN_01a8c450`, `XInputSetState` = `FUN_01a8c3a0`, `XInputGetCapabilities` = `FUN_01a8c860`, `XInputEnable` = `FUN_01a8c2f0` (empty) | gamepad thread | see 1.2 | high |
| L3 | Engine "Gamepad Update Thread" `FUN_00903e20`: every ~16 ms polls `XInputGetState(0..3)`, latches presses that happen between two game frames, sends the rumble values with `XInputSetState` (section 6). Started by `FUN_00904180`, paused/stopped by `FUN_00904260`/`FUN_00904360` (both zero all rumble first) | own thread | shared block in the device manager `dm` = `CWBInput+0x10` (1.3) | high |
| L4 | Game-thread device update: `CWBInput::vf_04` (`0x9023b0`, per frame) -> `FUN_00904650` (each device's `vf_03`: current -> previous buffer) -> `FUN_009044e0` (takes the thread's published pad states under the lock, creates/updates `Win32GamePad` objects, converts rumble). Devices `Win32Keyboard`, `Win32Mouse`, `Win32GamePad` x4 (1.3) | game | `CWBInput` vtable `0x34798a0` | high |
| L5 | Command bindings in `CWBInput` (`IEngineInput`): per local player a binding context ("controller") of command records, each with up to 4 bound device inputs. Each frame `FUN_00901a70` evaluates the bindings whose input changed (threshold/hysteresis `FUN_00901790`) and calls the command's callback | game | 1.4 | high |
| L6 | `PlayerInput` (`Player_GetPlayerInput` `0xa36e60`): the callback `FUN_00ab2af0` turns a binding change into **command on/off events**: UI first (`CInterfaceMgr`), then the player (`CPlayerUserInputListener` -> `Player` events -> `PlayerCombat`, camera presentation, ...). Also the polling API (value / is-on) | game | 1.5 | high |
| L7 | Combat input: `PlayerCombat+0x80` = shared combat input (`ISharedCombatInputHost`), maps commands to combat inputs from a database record (`Combat.Input`/`InputString`/`Chord`/`DoubleTap`), rebuilt when the pad/KBM mode changes (`FUN_022791b0` -> `FUN_022765e0`) | game | 1.6 | med |

Feral's `-record`/`-replay` hooks in at L5: `CWBInput+0x238` = mode (0 live, 1 record `FUN_00901c00`, 2 playback
`FUN_00902090`); in mode 2 every value comes from the recording instead of the devices **[high]**.

### 1.2 L1/L2: `CSDLController` and the XInput emulation

`CSDLController` fields **[high]**: `+0x08` SDL joystick instance id, `+0x10` `SDL_GameController*`, `+0x20` name,
`+0x28` controller type (wireless check in `vf_02`), `+0x31` "has produced input" (sticky, set by `vf_07`), `+0x32`
connected (read by `XInputGetState` through `CFeralController::vf_04`), `+0x58` haptic opened, `+0x60` `SDL_Haptic*`,
`+0x68` effect id, `+0x6c/+0x6e` last motor values, `+0x70` effect running, `+0x71` rumble paused.

Slots: `vf_02` (`0x20456d0`) open(device index); `vf_04` (`0x20455d0`) `SDL_GameControllerGetAttached`; `vf_07`
(`0x2045810`) read state; `vf_10` (`0x2045ef0`) set rumble(left, right); `vf_11` (`0x2045d70`) pause rumble(bool);
`vf_12` (`0x2045510`) open haptics **[high]**.

`vf_07(this, FeralPadState* out)` reads SDL buttons 0..14 and axes 0..5 and writes **[high]**:

| Offset | Type | Content |
|---|---|---|
| `+0x00` | u16 | XInput button bits: A `0x1000` (SDL 0), B `0x2000` (1), X `0x4000` (2), Y `0x8000` (3), BACK `0x20` (4), START `0x10` (6), L3 `0x40` (7), R3 `0x80` (8), LB `0x100` (9), RB `0x200` (10), d-pad up/down/left/right `1/2/4/8` (11..14); GUIDE (5) is not read |
| `+0x02/+0x03` | u8 | LT, RT = SDL axis 4/5 `>> 7` (0..255) |
| `+0x04..+0x0a` | s16 x4 | LX, LY, RX, RY; **LY and RY are negated** (SDL +down -> XInput +up: `y<=0 -> -y*32767/32768`, `y>0 -> -y*32768/32767`) |
| `+0x10/+0x18` | double | LT/255, RT/255 |
| `+0x20..+0x38` | double x4 | LX, LY, RX, RY normalised (`/32767` or `*1/32768`) |

It returns `true` once the pad has ever produced non-zero input (`+0x31`).

`XInputGetState(idx, XINPUT_STATE*)` (`FUN_01a8c450`) gets controller `idx` from the device manager, returns
`0x48f` (`ERROR_DEVICE_NOT_CONNECTED`) when there is none or it is not connected, else calls the controller's `vf_07`
(vtable `+0x38`) on a local buffer, copies buttons (same bits), triggers and sticks into a standard 16-byte
`XINPUT_STATE` (`dwPacketNumber` from a global counter `DAT_04707060`), and clamps each stick to a circle of radius
32767 unless the controller type (`+0x28`) is 5..11 **[high]**. **Do not hook or patch `FUN_01a8c450`**: besides the
input copy it contains inlined, obfuscated code that is not input logic (TLS-based, the protection layer's style). It is
out of scope; everything in this doc hooks below it (SDL) or above it (engine/game) and leaves it running unchanged
**[high that the code is there; its purpose was deliberately not analysed]**.

`XInputSetState(idx, XINPUT_VIBRATION*)` (`FUN_01a8c3a0`) -> controller `vtable+0x50` = `CSDLController::vf_10(left,
right)` (skipped when the slot is the empty base `CFeralController::vf_10`) **[high]**.

### 1.3 L3/L4: the gamepad thread, the device manager and `Win32GamePad`

The device manager `dm` (`CWBInput+0x10`, the `param_1` of `FUN_00903e20`/`FUN_009044e0`) **[high]**:

| `dm+` | Content |
|---|---|
| `0x08` | `Win32Keyboard*` (device slot 0; input ids 0..255) |
| `0x10` | `Win32Mouse*` (slot 1; ids 1000..1008) |
| `0x18 + 8*i` | `Win32GamePad*` for pad i = 0..3 (slots 2..5; ids 2000..2021), created on first connect (`FUN_009a7c50`, 0x60 bytes) |
| `0x38 + 4*(7*slot + k)` | effect values per device slot, 7 floats each; for pads `k=0/1` = left/right motor (0..1) |
| `0x110` | mutex shared by the thread and the game thread |
| `0x138 + 16*i` | the game-side copy of pad i's last `XINPUT_STATE` |
| `0x178 + 4*i` | pad i `XINPUT_VIBRATION` (u16 left, u16 right) = motor value * `DAT_03440790` |
| `0x188 + 16*i` | pad i `XINPUT_STATE` as published by the thread |
| `0x1c8 + i` | pad i connected (byte) |
| `0xe8` | thread handle; `0x108` pause counter (thread runs while 0) |

Per game frame `FUN_009044e0`: lock; copy `+0x188..` -> `+0x138..`; for each pad: if connected, create the
`Win32GamePad` if needed, set its connected flag (`vf_08(1)`, `+0x5c`) and fill it (`FUN_009a7d40`); else clear it
(`vf_09`) and set not connected; write the rumble shorts; unlock **[high]**.

`Win32GamePad` (vtable `0x34c73f8`, 0x60 bytes): two 0x28-byte state buffers at `+0x08` and `+0x30`, current index
`+0x58`; in a buffer: `+0x04` u16 buttons (XInput bits), `+0x10..` floats LX, LY, RX, RY (-1..1, Y up), LT, RT (0..1);
`+0x5c` connected. `vf_04(id)` = current value of input `id`, `vf_05(id)` = previous value, `vf_02(id)` = changed,
`vf_10` = any button down, `vf_06` = debug print ("L % .2f x % .2f ...") **[high]**. Input ids (from `vf_04` and the
mask table `DAT_045af5a0` = `1 2 4 8 0x10 0x20 0x40 0x80 0x100 0x200 0x1000 0x2000 0x4000 0x8000`) **[high]**:

| Id | Input | Id | Input |
|---|---|---|---|
| 2000 | D-pad up | 2010 | A |
| 2001 | D-pad down | 2011 | B |
| 2002 | D-pad left | 2012 | X |
| 2003 | D-pad right | 2013 | Y |
| 2004 | START | 2014 | RT (float `+0x2c`, from `bRightTrigger`) |
| 2005 | BACK | 2015 | LT (float `+0x28`, from `bLeftTrigger`) |
| 2006 | L3 | 2016 | LX |
| 2007 | R3 | 2017 | LY (+up) |
| 2008 | LB | 2018 | RX |
| 2009 | RB | 2019 | RY (+up) |

Ids 2020/2021 pass the range check but return 0. `FUN_009a7d40(pad, XINPUT_STATE*)` refreshes a pad only when
`dwPacketNumber` changed, and converts each stick axis separately (`s16/32768`, then a per-axis dead zone
`(|v| - dz)/(1 - dz)` clamped to 0..1: left stick dz 0.2395 = 7849/32768, right stick dz 0.2652 = 8689/32768; triggers
`b/255` with dz 0.1176 = 30/255; constants `DAT_034c7450..68`) **[high]**. The dead zone is per axis, not radial.

### 1.4 L5: `CWBInput` commands and bindings

Interface pointer: the `CIMgrAPIHolder<IEngineInput>` holders keep it at `*(void**)0x46b3318` (PlayerInput.cpp) and
`*(void**)0x46b2048` (InterfaceMgr.cpp) **[med: holder layout `holder+0x18`]**.

- A binding context ("controller", created by `vf_08(n)`) = `{cmd records*, n, value records vector (+0x10/+0x18),
  ..., +0x2c u8 pad index or 0xff}`; `vf_10(ctx, pad)` assigns the pad (its error text says "SetControllerEffectValue":
  Feral reused the string) **[high]**.
- A command record is 0x40 bytes: `+0x00` user command id, `+0x08` callback `(cmd, inputId, user)`, `+0x10` user data,
  `+0x18` owning context, `+0x20..+0x2c` four device input ids (-1 = none), `+0x30..+0x3c` four value-record indices
  (value record = 0x14 bytes `{scale, threshold, on-accumulator, off-accumulator, bool on}`) **[high]**.
- Queries: `vf_22` (`0x903590`) = command value (max |device value * scale| over the 4 bindings); `vf_25`
  (`0x9011e0`) = command is on; `vf_23` (`0x903990`) = a second value query (return value lost in the decompile; used
  for camera/aim axes) **[med]**; `vf_12(slot)` = device connected; `vf_28(cb, ctx)` = call `cb(inputId, padSlot, ctx)`
  for every device input whose value changed this frame (keyboard 0..255, mouse 1000..1008, pads 2000..2021;
  `FUN_009048e0`) - the game uses it for "press any key", rebinding and the pad/KBM switch **[high]**.

### 1.5 L6: `PlayerInput` and the command events

`PlayerInput` fields **[high]**: `+0x20` simulated-input queue (`PlayerInput.SetSimInput`, see re-gameplay.md), `+0x60`
vector of 0x30-byte per-command entries indexed by command id (`+0x00` the `CWBInput` command handle, `+0x29` "swallow
the next release"), `+0x78` enabled, `+0x130` `std::bitset<349>` = commands active in the current command sets,
`+0x160` local player index, `+0x170` listener (`CPlayerUserInputListener`, vtable `0x34ef7d0`).

| Function | Meaning | Conf. |
|---|---|---|
| `FUN_00ab2af0(cmd, inputId, PlayerInput*)` | the `CWBInput` callback of every bound command: `IsOn(cmd)` -> `FUN_00aab660`, else -> `FUN_00aab5d0` (unless `+0x29` swallows it) | high |
| `FUN_00aab660(PlayerInput*, cmd, value)` | **command ON**: builds `{cmd, value, player}` (`FUN_00eb8540`), offers it to `CInterfaceMgr` (`FUN_00a15330`, `*(shell+0x80ff8)`; returns true = consumed by UI); if not consumed: `*(shell+0x81018)` vtbl+0x68, then the listener vtbl+0x18 (`CPlayerUserInputListener::vf_03` -> `FUN_00a375a0`) | high |
| `FUN_00aab5d0(PlayerInput*, cmd, value)` | **command OFF**: same path with `FUN_00a15640`, vtbl+0x70, listener vtbl+0x20 (`vf_04` `0xa370f0`) | high |
| `FUN_00aab6f0(PlayerInput*, cmd, value)` | same dispatch as `FUN_00ab2af0` for a given command (used by UI code) | high |
| `FUN_00aab7a0` | analog-threshold helper: fires ON for a command in the active set when a value passes a level | med |
| `FUN_00aab2e0(PlayerInput*, cmd)` | command value (float) -> `CWBInput::vf_22` | high |
| `FUN_00aab3a0(PlayerInput*, cmd)` | -> `CWBInput::vf_23` (camera/aim axes 22/23, struggle aim) | med |
| `FUN_00aab3f0(PlayerInput*, cmd, bool ignoreSwallow)` | command is on -> `CWBInput::vf_25` | high |
| `FUN_00aab900(PlayerInput*, cmd)` | command is in the active command set (bitset `+0x130`) | high |
| `FUN_00a375a0(Player*, cmd)` | Player "command on": fires the `Player+0x18` delegate list (`PlayerCombat::OnCommandOn` -> `FUN_00a8bed0`, `PlayerCameraPresentation` prompts, ...), appends cmd to the recent list `Player+0x7f8` (`FUN_00a37080`), then special cases: 7 Strafe, 32 CenterView (pad mode only), 38 DPadDown, 260 CinematicSkip, 265, 281 EnterHierarchyMenu, 310 SkipKeyframedCamera, 317/318 PhotoMode | high |
| `CPlayerUserInputListener::vf_04` | Player "command off": `Player+0x30` delegate list (`PlayerCombat::OnCommandOff` -> `FUN_022709c0(+0x80, cmd)` + `FUN_00a8c050`) | high |

So there are two kinds of consumers: **event** consumers (attack presses, counters, menu navigation, prompts) and
**polling** consumers that read `FUN_00aab2e0`/`FUN_00aab3f0` every frame (movement and camera axes, held buttons).
Known polling sites (command id passed in `esi`, from the disassembly) **[high]**: movement 1/2/4/5 and 3/6 in
`FUN_00ad0af0` (11 callers), `FUN_00ad1540`, `FUN_00a7d5e0` (PlayerCombat); camera 22/23 in `FUN_00a3c8d0`,
`FUN_00ad0940`, `FUN_00d75490`, `FUN_00ecf640`; mouse 100..105 in `FUN_00a07730`/`FUN_00c65b10`; menu axes 52/53/56/57
in `FUN_00b7ee10`/`FUN_00c68e50`; map 59..64; struggle 1/2/4/5 + 52/56 in `FUN_00e0b0e0`/`FUN_00e14540`; photo mode
324..332.

### 1.6 L7: combat input

`PlayerCombat+0x80` is the shared combat-input object (`ISharedCombatInputHost` side, `[COMBAT|INPUT]` logger
`FUN_02270280`..; double-tap/chord/input-string logic `FUN_0226e360`, `[COMBAT|INPUTSTRING]` `FUN_00a7e000`..)
**[med]**. Its byte `+0xdd` (= `PlayerCombat+0x15d`, read by `PlayerCombat::vf_12`) is "using gamepad"; setting it
(`FUN_022791b0`) rebuilds the combat input map from the record at `+0x88` (`FUN_022765e0`) **[high mechanism]**. The
command names show the split: gamepad combat uses `CombatPrimary01..04`, `CombatSecondary01..04`,
`CombatTertiary01..06`, `SpecialAttack1..4`/`SpecialAttackModifier`, analog `CombatPrimaryAnalogX/Y`; KBM combat uses
`CombatKBM01..20` (+ `InputDodgeCommandID_KBM`, `InputBlockCommandID_KBM`) **[high names, med meaning]**.

## 2. The game's commands (action ids) and their bindings

### 2.1 The command table

`FUN_025e1450(&table, &count)` builds a static table at `0x4c3a3c0` (349 entries of `{const char* name; u32 hash;
u32 id}`, 16 bytes each, id = index) and returns `count = 0x15d` (349) **[high]**. The id is what `PlayerInput`, the
`CWBInput` command records and the event parameters carry; 349 (`0x15d`) is the "wait" pseudo-command of the simulated
input queue. Full list (also `reference/work-input/commands.tsv`):

```
0 None, 1 Forward, 2 Reverse, 3 ForwardAxis, 4 StrafeLeft, 5 StrafeRight, 6 StrafeAxis, 7 Strafe, 8 PitchPositive,
9 PitchNegative, 10 YawPositive, 11 YawNegative, 12 PitchAxis, 13 YawAxis, 14 Menu, 15 Crouch, 16 Jump, 17 Run,
18 FreeRun, 19 RunLock, 20 LeanLeft, 21 LeanRight, 22 PitchAccel, 23 YawAccel, 24 Stand, 25 Kneel, 26 Grab,
27 LedgePullUp, 28 LedgeDropdown, 29 QuickSave, 30 QuickLoad, 31 Mission, 32 CenterView, 33 Message, 34 Activate,
35 Abort, 36 Flashlight, 37 Radio, 38 DPadDown, 39 DPadUp, 40 DPadLeft, 41 DPadRight, 42 TriggerLeft, 43 TriggerRight,
44 MenuUp, 45 MenuDown, 46 MenuLeft, 47 MenuRight, 48 MenuAltUp, 49 MenuAltDown, 50 MenuAltLeft, 51 MenuAltRight,
52 MenuAxisY, 53 MenuAltAxisY, 54 MenuEnter, 55 MenuEscape, 56 MenuAxisX, 57 MenuAltAxisX, 58 MenuDelete, 59 MapMoveX,
60 MapMoveY, 61 MapCenterOnPlayer, 62 MapZoom, 63 MapZoomIn, 64 MapZoomOut, 65 MapSelect, 66 MapTeleport,
67 MessageBoxConfirm, 68 MessageBoxDeny, 69..77 Spectate*/Spectator*, 78 TutorialCancel, 79 MenuExit, 80 SyncBreak,
81..90 MenuAux0..9, 91 RadialRightX, 92 RadialRightY, 93 RadialLeftX, 94 RadialLeftY, 95 PDA, 96 BlendAxis,
97 TimeAxis, 98 ParamAxis, 99 SpeedAxis, 100 Mouse1, 101 Mouse2, 102 MouseWheelUp, 103 MouseWheelDown, 104 MouseAxisX,
105 MouseAxisY, 106..111 ParamEval_{Left,Right}Stick_{XAxis,YAxis,Radius}, 112 ParamEval_LeftTrigger,
113 ParamEval_RightTrigger, 114..117 PredEval_X/Y/A/B, 118..121 CombatPrimary01..04, 122..125 CombatSecondary01..04,
126..131 CombatTertiary01..06, 132 CombatPrimaryAnalogX, 133 CombatPrimaryAnalogY, 134 CombatSecondaryAnalogX,
135 CombatSecondaryAnalogY, 136..155 CombatKBM01..20, 156..159 SpecialAttack1..4, 160 SpecialAttackModifier,
161..170 CombatStruggle01..10, 171..180 CombatHidden01..10, 181 MNT_Mount, 182 MNT_GetOff, 183 MNT_Jump, 184 MNT_Charge,
185..188 MNT_Attack_1..4, 189..215 DebugCamera*/DebugButtonDims*/DebugMenu*/Respawn(209)/LHInput_DebugCam*,
216 BeginUICommands, 217 ToggleBladeMenu, 218 MoveDown, 219 MoveLeft, 220 MoveRight, 221 MoveUp, 222 Back, 223 Select,
224 TabLeft, 225 TabRight, 226 EndUICommands, 227 TerritoryMapX, 228 TerritoryMapY, 229 TerritoryMapSelect,
230 TerritoryMapBack, 231..242 Debug_AIInfo*/Debug_Spawn*, 243 VisionModeToggle, 244/245 DebugMenuActivate3/4,
246..249 DebugFactionMember*, 250..261 Cinematic* (260 CinematicSkip, 261 CinematicSkipConfirm), 262 VisionModeZoomOut,
263 BeaconExitMenu, 264 InterrogateExitMenu, 265 SkipPresentations, 266 UpdateQuestUIInfo, 267 RequestInterregationInfo,
268 ChangeActivatedPerk, 269 DeathVow, 270 InterrogationAbort, 271 FriendHeal, 272 FriendArrows, 273 FriendLure,
274 FriendAlarm, 275 FriendFillEnergy, 276 FriendInfightAoE, 277..280 EquipSword1..4, 281 EnterHierarchyMenu,
282 QuestConfirm, 283 QuestConfirmStart, 284 QuestConfirmCancel, 285 QuestFailedRetry, 286 QuestFailedAbandon,
287 StruggleSequenceEnter, 288 StruggleSequenceAxisX, 289 StruggleSequenceAxisY, 290/291 StruggleSequenceDebug*,
292 StartOptionalStory, 293..305 AICockpit*, 306 IdentifyNamed, 307 IdentifySkip, 308 IdentifySetTarget, 309 ViewReplay,
310 SkipKeyframedCamera, 311 SpiritStone_FastTravel, 312 SpiritStone_PassTime, 313 SpiritStone_Forge,
314 SpiritStone_Cancel, 315 EvaluationReplay, 316 InformationShortcut, 317..332 PhotoMode_*, 333..344 DebugSyncAction*,
345 RestoreDefaults, 346 RestoreActionDefault, 347 RemoveBinding, 348 CancelBinding
```

### 2.2 Which command is "attack", "counter", ... - data, two levels

1. **Command -> button**: `Profile/BindingSets`, `Profile/CommandSets`, `Profile/CommandSetPriority`,
   `Profile/UIActionIDBindings(Keyboard)` in the game database (`GameDBCategories::vf_04` registers them; readers
   `FUN_02171000`, `FUN_021700e0`, `FUN_0216ff80`, `FUN_021703a0`, `FUN_02170500`) **[high names]**. Command sets are
   switched by game state (`MainMenuCommandSet`, `InGameMenuCommandSet`, `PDACommandSet`, `InterrogationCommandSet`,
   `Modifier.InputCommandSet` per animation state; `PlayerInput.ApplyCommandSet/ReleaseCommandSet`); the active set is
   the bitset `PlayerInput+0x130`. KBM rebinds are saved in the profile chunk `ControlBindingsChunk`
   ("ControlRebindings"); the PC rebinding screen is `CUIPauseMenuControlBindingLayer` **[high names]**.
2. **Command -> combat action**: `Combat.Input` / `Combat.InputString` / `Combat.Chord` / `Combat.DoubleTap` records and
   setup fields such as `InputDodgeCommandID(_KBM)`, `InputBlockCommandID_KBM`, `DoubleTapWindow` (reader
   `FUN_025209b0`/`FUN_02520560`); attacks are then chosen from attack pools (re-gameplay.md section 4) **[high names]**.

The default bindings are **not in the binary** (no button-name table next to the command names; the only hard-coded
button texts are the debug camera's help). What the code does fix: the device input ids (section 1.3) and that
`CenterView` (32) only re-centres in pad mode. The commonly published pad layout (X strike, Y counter, B stun,
A vault/evade) is **not verified here**.

**Runtime check (cheap, read-only, first boot that reaches the world)**: for every command id `c` in
`PlayerInput+0x60` read the record `h = entry[c].handle` and its four input ids `*(int*)(h+0x20..0x2c)`; decode with
the table in 1.3 (0..255 keyboard scancodes, 1000..1008 mouse, 2000..2019 pad). That gives the exact default binding of
all 349 commands for the current command set, in both pad and KBM mode. The game's own `PlayerInput.Bindings.Dump`
(handler `FUN_00d9ea00`, PlayerControlBindings.cpp) and `PlayerInput.ShowCommandSet` (handler `FUN_00aa9230`, both handlers jump through
`FUN_02aeea40`; the "--- ComSets ---" dump text is printed by `FUN_00aab4d0`) do the same if a console command can be issued (`--cmds`/cfg); their output format is not
mapped. The combat-action level (which `CombatPrimaryNN` is strike vs counter) needs `Combat.Debug.Input`/`InputString`
logging (`*(bool*)` values in `reference/work-gameplay/combat_cvars.tsv`) while pressing each button once.

### 2.3 Gameplay actions -> commands (what the names and code say)

| Action | Command(s) | Evidence | Conf. |
|---|---|---|---|
| Move | 3 ForwardAxis, 6 StrafeAxis (analog), 1/2/4/5 Forward/Reverse/StrafeLeft/Right (digital) | polled together in `FUN_00ad0af0` | high |
| Camera | 22 PitchAccel, 23 YawAccel (pad stick), 12/13 PitchAxis/YawAxis, 104/105 MouseAxisX/Y | `FUN_00a3c8d0` (`RawCameraMag`), `FUN_00a07730` | high |
| Re-centre camera | 32 CenterView | `FUN_00a375a0` | high |
| Sprint / free-run / parkour | 17 Run, 18 FreeRun, 19 RunLock | names | med |
| Vault / jump | 16 Jump | name | low |
| Climb up / drop | 27 LedgePullUp, 28 LedgeDropdown, 26 Grab | names | med |
| Crouch / stealth | 15 Crouch | name | med |
| Interact / pick up | 34 Activate | name | low |
| Wraith vision | 243 VisionModeToggle, 262 VisionModeZoomOut | names | med |
| Aim bow / shoot | 42 TriggerLeft, 43 TriggerRight (raw trigger commands), or combat commands via `CPlayerCombatWeaponMode` | names | low |
| Strike, counter, stun, evade/vault, execute, drain, brand, flurry, combat abilities | 118..131 CombatPrimary/Secondary/Tertiary, 156..160 SpecialAttack1..4 + SpecialAttackModifier (pad); 136..155 CombatKBM01..20 (KBM) | data (2.2) | high for the slots, unknown for the meaning |
| QTE / struggles (interrogate, dominate, caragor, graug) | 161..170 CombatStruggle01..10, 287..289 StruggleSequence* | names, `CStruggle*` | med |
| Break a sync action | 80 SyncBreak | name | med |
| Mount (caragor/graug) | 181..188 MNT_* | names | high |
| Weapon / rune menu | 277..280 EquipSword1..4, 217 ToggleBladeMenu | names | med |
| Pause / map / menus | 14 Menu, 95 PDA, 31 Mission, 44..58 Menu*, 59..66 Map*, 79 MenuExit, 81..90 MenuAux0..9, 216..226 UI commands | names; UI consumes them in `CInterfaceMgr` first | med |
| Skip cinematics | 260 CinematicSkip, 261 CinematicSkipConfirm, 265 SkipPresentations, 310 SkipKeyframedCamera | `FUN_00a375a0` | high |
| Nemesis menu | 281 EnterHierarchyMenu | `FUN_00a375a0` | high |
| Photo mode | 317..332 | `FUN_00a375a0`, `CUIHUDPhotoMode` | high |
| Bright Lord / Lord of the Hunt extras | 269 DeathVow, 271..276 Friend*, 306..308 Identify* | names | med |

## 3. Injection points (task b)

All of these keep the game's own input code running unchanged (including `XInputGetState`, see 1.2); they either feed
a normal device below it or act on the game's command layer above it.

### 3.1 (i) A virtual gamepad

| Option | How | Robustness | Conf. |
|---|---|---|---|
| **V1 OS-level virtual pad (recommended first)** | the mod (or a helper process) creates a uinput Xbox 360 pad (`045e:028e`) and writes the Touch state into it; SDL opens it, Feral adds a `CSDLController`, the game sees an ordinary pad | highest on the game side: no game code touched, analog, all layers above work (bindings, menus, prompts, struggles, the pad/KBM switch, rumble back through the device). Unknowns are outside the game: `/dev/uinput` access, Steam Input re-wrapping the device, Feral's `inputdevices.json` mapping | high (game side), runtime check (OS side) |
| V2 SDL interposition | the LD_PRELOAD library exports `SDL_GameControllerGetButton`, `SDL_GameControllerGetAxis` (and `SDL_GameControllerGetAttached`); in this binary they are only called from `CSDLController::vf_07`/`vf_04` | high, but needs a controller handle to exist (V1 or a real pad); runs on the gamepad thread (keep a lock-free snapshot); values in SDL conventions (Y down, triggers 0..32767) | high |
| V3 `CSDLController::vf_07` slot | vtable slot `0x4513168` -> own function filling the Feral pad state (1.2 layout) | same precondition as V2; a data-slot swap, no code patch | high |

Values: because of the per-axis dead zones (1.3), a stick value `v` in -1..1 arrives unchanged only if the device gets
`sign(v) * (dz + |v| * (1 - dz)) * 32767` (left dz 0.2395, right 0.2652, triggers 0.1176). Write `dwPacketNumber`-style
changes every frame (V1 does that implicitly). Latency: up to one 16 ms thread poll plus one game frame **[med]**.

### 3.2 (ii) The action layer

- **Fire a command**: `FUN_00aab660(PlayerInput*, cmd, 0)` = ON, `FUN_00aab5d0(PlayerInput*, cmd, 0)` = OFF, with
  `PlayerInput* = Player_GetPlayerInput(player)` (`0xa36e60`, = `*(player+0xd48)+0x10`), on the game thread. This is the
  exact path a bound button takes after `CWBInput` (UI first, then player events and `PlayerCombat`) **[high]**.
  Limit: polled state (`FUN_00aab3f0`/`FUN_00aab2e0`, i.e. `CWBInput` reading the device) does not see it, so "hold"
  actions (sprint, aim, wraith hold, chords, charge, struggle holds) do not work this way. Good for discrete taps
  (counter, stun, skip, menu confirm) **[high mechanism, runtime check per action]**.
- **Combat only**: `PlayerCombat::OnCommandOn` = `FUN_00a8bed0(PlayerCombat*, &cmd)` feeds the combat
  input-string/chord machine `FUN_02270600(PlayerCombat+0x80, cmd, PlayerCombat+0x838, timeMs)`; OFF = `FUN_00a92000`
  path (`FUN_022709c0` + `FUN_00a8c050`) **[high]**.
- **Suppress a command**: entry of `FUN_00ab2af0` (the one callback every bound command goes through; registered at
  `0xaadd1f`) - drop the ids the VR combat replaces; for polled commands also return 0 from `FUN_00aab3f0` /
  `FUN_00aab2e0` for those ids. Easiest of all: with V1 the virtual pad simply never presses them **[high]**.
- Debug: `PlayerInput.SetSimInput` queue (re-gameplay.md section 3), digital, once per frame.

### 3.3 (iii) Analog movement and camera

- **Move stick**: `FUN_00ad0af0(PlayerInput*, playerIdx)` returns `{x = StrafeAxis(6), y = ForwardAxis(3)}` packed in
  XMM0 (in KBM mode the digital keys 1/2/4/5 override with +-1); it is the single source for the player animation
  inputs, struggles and 9 more callers (call sites `0xa3c911`, `0xa3d2aa`, `0xa4114b`, `0xa41820`, `0xabc9a5`,
  `0xabda0e`, `0xad0c26`, `0xce8465`, `0xe153f6`, `0xe2f3ce`, `0xebb999`, `0xed4258`). An entry hook here gives exact,
  dead-zone-free analog movement (and is the place to rotate the stick for hand-relative locomotion) **[high]**.
  "Is there move input" checks: `FUN_00ad1540`, `FUN_00a7d5e0` (PlayerCombat, also counts 16 Jump).
- **Camera stick**: commands 22 PitchAccel / 23 YawAccel, read through `FUN_00aab3a0` in `FUN_00a3c8d0`
  (`RawCameraMag`) and through `FUN_00aab2e0` at `0xd75507..` / `0xecf668..`; dead-zone test `FUN_00ad0940` (radius
  `*(float*)0x46b3fa4`). In VR the HMD drives the view; feed 0 (or use the stick for snap/smooth turn implemented on
  the player yaw by the camera helper) **[high sites, design open]**.

### 3.4 Gamepad vs mouse/keyboard mode (glyphs) and forcing gamepad

- Per frame `FUN_00a13790` (CInterfaceMgr) asks `CWBInput::vf_28` for every input that changed and classifies it with
  `FUN_00a0d830`: pad ids 2000..2019 -> pad activity (`+0x36ad`); mouse movement (1005/1006) counts as KBM activity
  only while the cursor is visible; keyboard counts except ids 0x10..0x12 (Shift/Ctrl/Alt); mouse buttons count
  **[high]**.
- In KBM mode with pad activity it switches to pad if byte **`0x46b0d60` == 1** (call `0xa138fb`); in pad mode with KBM
  activity it switches to KBM if byte **`0x46b0d61` == 1** (call `0xa138b5`). Both bytes are set to 1 by `_INIT`
  (`0x57166f`/`0x571676`, InterfaceMgr.cpp TU) and are not console variables **[high]**.
- The switch itself: `FUN_00a0eeb0(CInterfaceMgr* = *(shell+0x80ff8), localPlayerEntry, bool pad)` -> profile
  `+0x238` (`FUN_00cbe610`, read by `FUN_00cbe650` = "using gamepad", used everywhere incl. `FUN_00ad0af0`), cursor
  mode (`FUN_00e6bf30`), menus (`FUN_00a22d30` -> `CUIMenuManagerBase::vf_15`), HUD prompts (`FUN_00c118a0`), and
  `FUN_00a39600` = Player "using gamepad" (`Player+0x2c4`, `PlayerCombat+0x15d` -> combat input map rebuild, CPlayerUI,
  photo mode) **[high]**. Also called by the PC options layer (`CUIPCGamepadLayer`, `PC_UseGamepad`) and the key
  rebinding screen.
- Disconnect: `FUN_00a0ec30` (device callback) switches a pad-mode player back to KBM when no pad is left and shows
  `IDS_Reconnect_Controller` (`FUN_00a3c150`).
- **Force gamepad**: write 0 to `0x46b0d61` (never fall back to KBM) and either produce pad activity once or call
  `FUN_00a0eeb0(...,1)`. Prompts and glyphs then stay in pad style **[high static, runtime check]**.
- Glyph family: `CUIMgr::vf_25` (`0x907f90`, device listener) uses PlayStation (`*_ps.tex`) glyphs when exactly one
  Feral controller of type 8..10 is present, Steam Controller (`*_sc.tex`) for type 11, otherwise the default (Xbox)
  set; a uinput Xbox 360 pad gives Xbox glyphs **[med: type values from the code, the type of a uinput pad needs a
  runtime check]**.

## 4. Camera-relative movement (task c)

The player's locomotion is animation-driven; the stick reaches it as animation parameters set each frame in
**`FUN_00a3c8d0` = Player "update animation inputs"** (5682 bytes; strings `RawMoveMag`, `MoveThreshold`, `DeltaYaw`,
`CameraDeltaYaw`, `PlayerCameraDeltaYaw`, `CameraYawMinusPlayerYaw`, `Game_CameraPitch`) **[high]**:

1. stick = `FUN_00ad0af0(...)` (`0xa3c911`), magnitude clamped to 1 -> `RawMoveMag`;
2. camera rotation = `FUN_00a498c0(out, cam+0x484, cam+0x458)` (two quaternions of `CPlayerCamera` = `Player+0x2d0`
   composed), forward = `FUN_009f1b00(quat)`, flattened to XZ and normalised (`local_e0`, `local_d8`);
3. **`FUN_00a3c350(Player*, bool moving, float stick[2], float camFwd[3], playerXform*, anim)`** (call `0xa3d232`):
   builds a rotation from the normalised stick and one from the camera forward, composes them (= desired world move
   direction), and writes the signed yaw from the player's facing to it into `DeltaYaw` (also `CameraDeltaYaw`); keeps
   the normalised stick at `Player+0xcb0/+0xcb4` and a turn amount at `+0xcb8`. This is the function where the left
   stick is rotated by the camera yaw;
4. right after it, the stick rotated by the camera forward is also written as a world vector into an animation-side
   object (`*(*(anim+0x918)+0x38)` `+0x124..+0x138`, negated), and `FUN_00ad0c00` computes `PlayerCameraDeltaYaw`.

For VR: either hand the HMD/controller forward to `FUN_00a3c350` (swap `rcx` at `0xa3d232`), or rotate the stick in
`FUN_00ad0af0` by (controller yaw - camera yaw); the second also covers combat and struggle users of the stick
**[high sites, med on the anim-side vector's meaning]**.

## 5. Freeflow targeting (task d)

### 5.1 Data flow

| Step | Function | What it does | Conf. |
|---|---|---|---|
| combat update | `FUN_00a8e120` (PlayerCombat, from the Player updates `FUN_00a40fe0`/`FUN_00a416a0`) | builds the targeting context and runs targeting, then validates attack nodes ("Root/Linked node validated") | high |
| context | `FUN_00a97690(ctx, Player*, PlayerCombat+0xc0, ...)` (call `0xa8e334`) -> `FUN_00a97550` | `ctx+0x00` Player, `+0x10..+0x18` **input direction in world space** copied from `PlayerCombat+0xc0` (vec3), `+0x4c` **has stick input** (`PlayerCombat+0xdc`), `+0x1c..+0x24` player forward (from its quaternion), `+0x28/+0x30` player transforms, `+0x48`, `+0x4d/+0x4e` attack flags | high |
| update | `FUN_00a9ce70(PlayerCombat+0x2f0, ctx, PlayerCombat+0x360, PlayerCombat+0x880)` (call `0xa8e35f`) | `FUN_00a98030` refreshes the current targets of all target slots; then time-sliced candidate refresh over three distance bands (`+0x2f0 + 0/0x18/0x30`, budgets `DAT_046b30c8/cc/d0`, band limit `DAT_046b30c0`) with `FUN_00a9c8e0`; current target kept via `Object_GetCombatHost(ctx+0x38)` | high |
| candidate geometry | `FUN_0274ed10(candInfo = CombatHost+0x2f8, ctx)` | `+0x80` distance, **`+0x84` angle (deg) between the stick direction and the direction to the enemy**, `+0x88` its cosine, `+0x8c` dot with the player forward, `+0x94` angle player-facing -> enemy, `+0x98` enemy-facing -> player, then camera-space values (on-screen tests) from `CPlayerCamera +0x458..+0x490` | high |
| scoring | `FUN_00a98850(slots, ctx, current, candInfo)` -> **`FUN_00a941b0(record, ctx, candInfo, float* score)`** | per target record: ~35 rejection rules ("Rejected (%s) - Out of Range (stick input)", "Distance (no stick input)", LOS, state lists, parry/counter windows, on-screen, height, ...), else a score from distance, stick angle, current/last target bonuses; logs under `[COMBAT|TARGET]` | high |
| result | `FUN_026d8190(score, candInfo, slot)` | slot `+0x68` = target object, `+0x70` its current attack, `+0x78` distance, `+0x7c/+0x80` angles, `+0x84`, `+0x88` = score; `FUN_02280b90(slot)` reads it back; slot `vf_02` (`vtbl+0x10`) resets it | high |

Target slots live in the object at `PlayerCombat+0x360` (arrays at `+0x18`/`+0x78` stride 0x98 and `+0x30`/`+0x90`
stride 0x108; each slot's record at `+0x40`). Which slot serves which move (strike, counter, stun, vault, execution,
drain...) comes from the target records in the database **[high structure, data for the meaning]**. The game already
forces targets itself: `FUN_00a99d70(owner, attack, slot)` sets a slot to `owner+0x40` with score 1.0 (callers in
PlayerCombat `0xa7edd0`, `0xa85a10`, `0xa85b40`, `0xa85b80`) **[high]**.

### 5.2 Override with "the enemy my sword hit"

1. Gentle: after `FUN_00a97690` returns (call `0xa8e334`), set `ctx+0x10..+0x18` to the normalised direction from the
   player to that enemy and `ctx+0x4c = 1`; the game's own rules then pick it (angle 0) and keep every validation.
2. Firm: wrap `FUN_00a941b0` (6 call sites `0xa96310`, `0xa9641c`, `0xa9865d`, `0xa987b7`, `0xa98942`, `0xa98a3c`):
   call it, then raise the score of the chosen enemy (`candInfo+0x68` == its object) and reject others.
3. Direct: after `FUN_00a9ce70` (call `0xa8e35f`), write the slots with `FUN_026d8190(1.0, enemyHost+0x2f8, slot)`,
   the way `FUN_00a99d70` does.
Option 1 first; all three run on the game thread inside `FUN_00a8e120` **[high sites, runtime check of the effect]**.

## 6. Rumble (task e)

| Step | Where | Detail | Conf. |
|---|---|---|---|
| source | `CRumbleFX` (ClientFX "Rumble" key; vtable `0x3b66b20`, controller sub-object vtable `0x3b66bc0`), props `CRumbleProps` (`FUN_02d9bc30` ctor, `vf_02` `0x2d9bd30` loads them) | 4 channel curves over normalised lifetime (`props+0x90/+0xa0/+0xb0/+0xc0`), optional distance falloff (`+0x70` on, `+0x74` which position, `+0x78` inner radius, `+0x80` outer-radius curve, `+0x7c` 0 linear / 1 quadratic); `CRumbleFX::vf_17` / controller `vf_02` (`0x2d9b890`) add `curve_k(t) * falloff` into `out[k]` | high |
| sum | `FUN_00d8b230(fxMgr, camXform, playerXform, float out[7])` over the controller list at `fxMgr+0xf8`, for the three FX managers `*(shell+0x80c80)`, `+0x80c78`, `+0x80c88`; each channel clamped 0..1 | per frame, in **`FUN_00a5ad40` (CPlayerCamera)**; skipped (zeros) unless `FUN_00ad0a50(player,1)` (profile byte `+0x1fc` = Vibration option [med], PlayerInput rumble gate `FUN_00aaa560`) | high |
| per channel | **`FUN_00aaa450(PlayerInput*, int ch, float v)`** = PlayerInput set rumble, called at `0xa5af58` (sum loop, ch 0..6) and `0xa5af97` (`FUN_00a5af70`, explicit array) -> `CWBInput::vf_13(ctx = PlayerInput+0x58, ch, v)` -> `FUN_009046c0` stores it in `dm+0x38` | the only two callers; `FUN_00aaa4c0` zeroes ch 0..3 (pause/menus, 5 callers) | high |
| device | `FUN_009044e0`: pad i `XINPUT_VIBRATION = {ch0 * 65535, ch1 * 65535}` at `dm+0x178+4*i` -> gamepad thread `XInputSetState` -> `CSDLController::vf_10(left, right)` -> `SDL_HAPTIC_LEFTRIGHT`, infinite length, `large = left`, `small = right`, updated only on change; `vf_11` pauses/resumes | ch 0 = left/large (low-frequency) motor, ch 1 = right/small (high-frequency); ch 2..6 are not sent to pads (2/3 are probably trigger motors from the console version) | high (0/1), low (2/3) |

Forwarding to the Touch controllers: with the V1 virtual pad, the game's rumble arrives at the uinput device as a
force-feedback upload (`FF_RUMBLE` strong/weak from SDL's LEFTRIGHT effect) - no game hook at all; map strong -> one
hand, weak -> the other or both, as amplitude of an `XrHapticVibration` refreshed every frame **[high game side, runtime
check of the kernel/SDL path]**. Game-side alternative: entry hook on `FUN_00aaa450` (values 0..1 per frame, before
quantisation). Our own sword hits should drive haptics directly from the physics code, not through this path.

## 7. Menus (task f)

- Two navigation modes, switched together with the input mode (3.4, `FUN_00a22d30` -> `CUIMenuManagerBase::vf_15`):
  **pad mode** = command navigation (44..47 MenuUp/Down/Left/Right, 54 MenuEnter, 55 MenuEscape, 52/56 MenuAxisY/X,
  48..51/53/57 MenuAlt*, 81..90 MenuAux0..9, 224/225 TabLeft/Right, 67/68 MessageBox*); the UI consumes these first
  (`FUN_00a15330`/`FUN_00a15640`, state machine `CInterfaceMgrStateMachine`, menu layers `CUIMenuLayerBase`,
  nav panes `IUINavPaneReceiver`) **[high]**. **Mouse mode** = Scaleform (GFx 4, ActionScript 2 movies: `AS2::Mouse*`,
  `onMouseMove`, `hitTest`, `mouseWheelEnabled`) hit-testing at the cursor; mouse buttons are inputs 1000..1004, the
  wheel 1007/1008 (`Win32Mouse::vf_04`) **[high names, med flow]**.
- Cursor: `CursorMgr` (CursorMgr.cpp; small flag struct, update `FUN_00e6bdb0`) drives `IEngineCursor` (`CWBCursor`,
  vtable `0x3478310`; `vf_03` = mode/capture, `vf_06` = show) -> Feral/SDL (`SDL_SetRelativeMouseMode` in
  `FUN_020464b0`/`FUN_02040100`, warp `FUN_020466e0`). Cvars `CursorMgr.MouseCapture` (value `*(bool*)0x46b83d8`,
  "centre the mouse every frame based on input mode") and `CursorMgr.AllowCursorVisible` (`*(bool*)0x46b8398`)
  **[high]**. In captured mode inputs 1005/1006 are the offset from the window centre (`Win32Mouse::vf_04`).
- Absolute mouse position comes from `SDL_GetMouseState`/`SDL_GetGlobalMouseState`, both only called in Feral's
  `FUN_02046500` and `FUN_020465b0` **[high]**.
- **Laser-pointer menus, two ways**: (A) in menus switch the player to KBM mode (`FUN_00a0eeb0(...,0)`, keep the
  auto-switch bytes under our control) and feed the laser's hit point on the UI quad as the mouse position by
  interposing `SDL_GetMouseState`/`SDL_GetGlobalMouseState`, the trigger as left button (button events probably come
  through `SDL_PeepEvents` `FUN_0203c5a0` - runtime check), then back to pad mode in game; glyphs flip to KBM style in
  menus. (B) stay in pad mode and translate laser gestures into Menu* commands (3.2), no hover. (A) is the real laser
  pointer **[med]**. Not mapped yet: the function that passes the mouse to the Scaleform movie (`CUIView`, 78 slots;
  `vf_41..vf_50` are ActionScript invoke helpers, not input) - next step.

## 8. Hook plan (task g)

Per-frame order on the game thread: `CWBInput::vf_04` (devices, bindings, command callbacks) runs before the player
update; inside the player update `FUN_00a3c8d0` (movement) and `FUN_00a8e120` (combat: targeting at `0xa8e334`/
`0xa8e35f`); `CPlayerCamera` sends rumble (`FUN_00a5ad40`). Thread/timing assumptions need the runtime check in 9.

| Site | Purpose | Notes |
|---|---|---|
| (none: V1 uinput pad) | virtual gamepad + rumble back | first spike: no game hook |
| LD_PRELOAD exports `SDL_GameControllerGetButton/GetAxis/GetAttached` | V2 exact pad values | needs a device; gamepad thread |
| `I2` vtable slot `CSDLController::vf_07` | V3 | needs a device; gamepad thread |
| `I3` vtable slot `CSDLController::vf_10` | read the game's rumble (left, right u16) for a real/virtual SDL pad | gamepad thread |
| `A1` entry `FUN_00ab2af0` | suppress bound commands by id | `(cmd, inputId, PlayerInput*)` |
| `A2`/`A3` call targets `FUN_00aab660`/`FUN_00aab5d0` | fire command ON/OFF | game thread only |
| `M1` entry `FUN_00ad0af0` | exact analog move stick, hand-relative rotation | returns `{x,y}` in XMM0 |
| `M2`/`M3` entries `FUN_00aab2e0`/`FUN_00aab3a0` | per-command value override (camera 22/23, polled holds) | hot (called often) |
| `C1` call `0xa3d232` | substitute the camera forward in the camera-relative move | arg 4 (`rcx`) = `float camFwd[3]` |
| `T1` call `0xa8e334` | after it: write the targeting input direction (`ctx+0x10`, `ctx+0x4c`) | ctx at `rbp-0x120` of `FUN_00a8e120` = `rdi` of the call |
| `T2` call `0xa8e35f` | after it: force target slots (`FUN_026d8190`) | slots = `rdx` (`PlayerCombat+0x360`) |
| `T3` entry `FUN_00a941b0` | score override | `bool (record, ctx, candInfo, float* score)` |
| `R1`/`R2` calls / `R3` entry `FUN_00aaa450` | game-side rumble per channel | `(PlayerInput*, int ch, float v)` |
| `U1` call target / `U2`,`U3` calls | input mode (KBM `edx=0` at `U2`, pad `edx=1` at `U3`) | plus bytes `0x46b0d60` (allow -> pad) / `0x46b0d61` (allow -> KBM), in .bss, expect 1/1 at run time |

Check data (format of `mod/spike1/check_sites.py`; `K_ENTRY` is a new kind = function-entry hook: ctx = the first 16
bytes, target = itself; `check_sites.py` would need that kind added). Entry hooks whose first instructions are
relative (`A1`, `A2`, `A3`, `M2`, `M3`: `jcc rel8`; `R3`: RIP-relative `mov`) need a relocating trampoline:

```
SITE("I2_sdl_getstate", K_VSLOT, 0x4513168, 0x4513168, "10 58 04 02 00 00 00 00", 0x2045810, "41 55 41 54 55 53 48 83 ec 08 48 8b 6f 10 48"),
SITE("I3_sdl_rumble", K_VSLOT, 0x4513180, 0x4513180, "f0 5e 04 02 00 00 00 00", 0x2045ef0, "41 55 41 54 41 89 d4 48 8d 15 12 f6 ff ff 55"),
SITE("A1_cmd_callback", K_ENTRY, 0xab2af0, 0xab2af0, "48 85 d2 74 6b 48 8b 4a 60 48 8b 42 68 48 29 c8", 0xab2af0, "48 85 d2 74 6b 48 8b 4a 60 48 8b 42 68 48 29"),
SITE("A2_cmd_on", K_ENTRY, 0xaab660, 0xaab660, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00 00", 0xaab660, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00"),
SITE("A3_cmd_off", K_ENTRY, 0xaab5d0, 0xaab5d0, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00 00", 0xaab5d0, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00"),
SITE("M1_move_stick", K_ENTRY, 0xad0af0, 0xad0af0, "55 89 f5 be 03 00 00 00 53 48 89 fb 48 83 ec 18", 0xad0af0, "55 89 f5 be 03 00 00 00 53 48 89 fb 48 83 ec"),
SITE("M2_cmd_value", K_ENTRY, 0xaab2e0, 0xaab2e0, "80 7f 78 00 74 42 48 8b 57 60 48 8b 47 68 48 29", 0xaab2e0, "80 7f 78 00 74 42 48 8b 57 60 48 8b 47 68 48"),
SITE("M3_cmd_value2", K_ENTRY, 0xaab3a0, 0xaab3a0, "80 7f 78 00 74 42 48 8b 57 60 48 8b 47 68 48 29", 0xaab3a0, "80 7f 78 00 74 42 48 8b 57 60 48 8b 47 68 48"),
SITE("C1_camrel_move", K_CALL, 0xa3d232, 0xa3d222, "f0 00 00 00 48 8d 8c 24 98 00 00 00 41 0f b6 f5 e8 19 f1 ff ff", 0xa3c350, "41 57 41 89 f7 be 2d 32 8a 03 41 56 49 89 d6"),
SITE("T1_target_ctx", K_CALL, 0xa8e334, 0xa8e324, "00 00 48 8d bd e0 fe ff ff 4c 8d b3 58 04 00 00 e8 57 93 00 00", 0xa97690, "48 89 74 24 f0 f3 0f 7e 44 24 f0 31 c0 48 89"),
SITE("T2_target_update", K_CALL, 0xa8e35f, 0xa8e34f, "00 00 48 8d b5 e0 fe ff ff 48 89 85 98 fe ff ff e8 0c eb 00 00", 0xa9ce70, "41 57 41 56 49 89 ce 41 55 41 54 49 89 d4 55"),
SITE("T3_target_score", K_ENTRY, 0xa941b0, 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49"),
SITE("R1_rumble_sum", K_CALL, 0xa5af58, 0xa5af48, "48 89 ef 48 83 c3 01 0f 28 c4 f3 0f 11 64 24 04 e8 f3 f4 04 00", 0xaaa450, "48 8b 05 c1 8e c0 03 89 f2 48 8b 77 58 48 8b"),
SITE("R2_rumble_set", K_CALL, 0xa5af97, 0xa5af87, "00 f3 41 0f 10 04 9c 89 de 48 89 ef 48 83 c3 01 e8 b4 f4 04 00", 0xaaa450, "48 8b 05 c1 8e c0 03 89 f2 48 8b 77 58 48 8b"),
SITE("R3_rumble_setter", K_ENTRY, 0xaaa450, 0xaaa450, "48 8b 05 c1 8e c0 03 89 f2 48 8b 77 58 48 8b 08", 0xaaa450, "48 8b 05 c1 8e c0 03 89 f2 48 8b 77 58 48 8b"),
SITE("U1_set_input_mode", K_ENTRY, 0xa0eeb0, 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f b6", 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f"),
SITE("U2_autoswitch_to_kbm", K_CALL, 0xa138b5, 0xa138a5, "03 01 75 b7 84 c0 75 b3 31 d2 48 89 ee 4c 89 e7 e8 f6 b5 ff ff", 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f"),
SITE("U3_autoswitch_to_pad", K_CALL, 0xa138fb, 0xa138eb, "e9 56 ff ff ff ba 01 00 00 00 48 89 ee 4c 89 e7 e8 b0 b5 ff ff", 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f"),
```

## 9. Runtime checks and open questions

Runtime checks (all read-only, in the order they pay off):
1. Dump the bindings of all 349 commands from `PlayerInput+0x60` (2.2) in pad and KBM mode; log `[COMBAT|INPUT]`
   (`Combat.Debug.Input`/`InputString`) while pressing each pad button once -> attack/counter/stun/evade/... mapping.
2. V1: does a uinput Xbox 360 pad appear as controller 0 (`FUN_02048930`), with which type (`+0x28`, glyphs), and does
   Steam Input leave it alone? Do rumble uploads reach the uinput owner?
3. Confirm the per-frame order (device update before player update; targeting inside `FUN_00a8e120`), the thread of
   `CSDLController::vf_07` (gamepad thread) and that `FUN_00a3c350`'s `rcx` is the flattened camera forward.
4. Targeting: log `FUN_00a941b0` (record name, candidate, score) and the slot contents after `T2` during a fight;
   `Combat.TargetingDebugEnable` / `Combat.Vis.Target` / `Combat.Dump.TargetingStats` (PlayerCombatTargeting.cpp) may do
   it without code.
5. Rumble: log `FUN_00aaa450` per channel during hits, executions, graug footsteps (channels 2..6 used?).
6. The Vibration option byte (profile `+0x1fc`) and the input-mode bytes `0x46b0d60/61` at run time.

Open questions (static, next steps):
- Who writes `PlayerCombat+0xc0` (the world-space combat input direction): presumably the camera-rotated move stick;
  find the writer to know whether `C1`/`M1` changes also move the targeting direction.
- The Scaleform mouse feed (which `CUIView`/`CUIMgr` function passes position and buttons to the movie) for laser menus.
- `CWBInput::vf_23` return value (second value query used for camera axes).
- The command-set records (`Profile/CommandSets`) and `Combat.Input` records are data; their readers are listed in 2.2
  if a static decode of `GlobalDatabase.bndl` is ever wanted.

No renames or other writes were made in any Ghidra project.
