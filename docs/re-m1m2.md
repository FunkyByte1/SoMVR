# Shadow of Mordor (Feral Linux build): the M1/M2 gates - console executor, locomotion input, head pieces, pad bindings, sync actions, presentations

Static analysis only (2026-10-19): Ghidra copies `som-linux-q4`/`som-linux-q5` (read-only, `scripts/ghidra_query.sh`), the
TSV indexes in `reference/index/`, `objdump`/`readelf` on `reference/bin/linux/ShadowOfMordor`. Nothing was launched, the
install was not touched. Scratch (git-ignored): `reference/work-m1m2/` (batches `bN.txt`, outputs `oN.txt`, disassembly
ranges `d_*.txt`). Conventions and confidence marks as in `re-gameplay.md` / `re-input.md` (**[high]** read in the code,
**[med]** meaning inferred, **[low]** guess, **RT** = needs a boot). `objsys = *(void**)0x466afa0` (object system),
`eng = *(void**)0x466afa8` (`IWBEngine`), `P` = client `Player*`.

Section 1 reconciles every **needs RE** of `design-vr.md` against the deeper docs; sections 2-8 are new RE for the items
that gate M1/M2 (section 12 of the design), best value first.

## 0. Summary

| Item | Answer | Conf. | Site |
|---|---|---|---|
| **G22** console/cfg executor | `CWBEngine::vf_32` `0x8bd8b0` = `RunConsoleString(eng, "cmd args; cmd2 ...")` -> `FUN_008b9ac0`; call `(*(void***)eng)[32](eng, s)` on the game thread. Every wanted command is registered in the release build (static initialisers); server-side ones (`SlowMO.*`, `Character.*`) run only while a world is up. `Faction.Dump <file>` writes a file. Some debug bodies are compiled out (`PlayerInput.Bindings.Dump`, `list`) | high | `G22_RUNSTRING`, `G22_EXEC` |
| **PlayerMovement** input | there is no velocity input: locomotion is root motion. `PlayerMovement::vf_11` `0xab5e70` applies the animation's root-motion delta to the next transform and composes an **unused one-shot extra delta** at `pm+0x428` (pos) / `+0x434` (quat) / `+0x444` (pending byte). Writing it is the direct body-follow input | high (code), RT (collision) | `PM_ROOTMOTION` |
| **G12** piece names | not in the binary (confirmed); enumerate at runtime (P1 `pieces`, runtime-probes P1q) | high | - |
| G12 shadow-only flags | no new static result; candidates in re-firstperson.md 3.2; P2q decides whether it is needed | low | - |
| Pad buttons FreeRun / attack / counter / stun | data (`Profile/BindingSets`), not in the binary; `PlayerInput.Bindings.Dump` prints nothing in this build, so only the probe's own reader works (runtime-probes I2) | high | - |
| **G14** sync actions | start `FUN_028081a0(sync, record, target)` called once from `Combat_StartAttack` (`0x26eb93c`); stop = teardown `FUN_028060a0(sync)` (all end paths) plus the inlined queued abort `FUN_02805fa0` | high (calls), med (fields) | `G14_SYNC_START`, `G14_SYNC_END`, `G14_SYNC_QABORT` |
| G14 struggles | `PlayerCombat+0x8e0` (sync struggle, `FUN_00e16ef0`) and `+0x8d8` (state struggle, `FUN_00e15690`) hold the live `ICombatStruggle` | med | `G14_STRUGGLE_SYNC`, `G14_STRUGGLE_STATE` |
| **G16** presentations | `PlayerCameraPresentation` = `P+0x308`; per frame `FUN_00a78220` sets the animation flag `InPresentation` = `pcp+0x90 != 0`; start `FUN_00a6d740`. Poll `pcp+0x90` | med | `G16_PRES_UPDATE`, `G16_PRES_START_A/B` |
| G9 object iteration (bonus) | the `list <class>` command's loop (`FUN_008cd098`): object lists at `*(0x466aee0)+0x3f8`, `objsys` vtbl+0x78 GetClass, +0x80 IsKindOf | med | - |

## 1. Reconciliation of design-vr.md (the `needs RE` markers as of 2026-10-19 morning)

53 markers (plus the two in the header that define the term). **19 answered** (marker replaced with the answer and a
pointer), **15 runtime only** (static RE is exhausted: the answer is data or a measurement; marker replaced with
**RT: ...** and the probe that measures it), **19 remain** (marked; a pointer added where a doc has part of it).

| design-vr.md place | Status | Answer / pointer |
|---|---|---|
| G9 object manager iteration | answered | section 8 (the `list` command's loop) |
| G11 / section 1 "where to write the arm pose" | answered | post-hook `CSimpleModelTrackerNode::vf_03` (slot `0x38df398`), re-animation.md 3.2, 8 |
| G14, section 1 "sync-action record name and participants" | answered | section 6; re-combat.md 2.5 |
| G16, section 6 "presentation start/stop" | answered (stop and camera pose RT) | section 7 |
| G22 | answered | section 2 |
| section 1 PlayerMovement velocity input | answered | section 3 |
| section 3 parry recoil starter | answered | `FUN_02810af0` with the attack's `RecoilTypeParry`, or `FUN_02811140` with a zero amount, re-combat.md 2.1, 3.6 |
| section 3 execution kill without the paired animation | answered (RT C2) | `Combat_DealHit(..., forceKill = 1)`, re-combat.md 0, 1.1 |
| section 3 arrow spawn origin / FreeAim camera | answered | fire our own `fireXf` with `FUN_02332c50`; the camera aim is not used then, re-combat.md 4.2 |
| section 3 hit counter advances | answered (RT) | counts distinct attack records, re-combat.md 1.5 |
| section 3 damage amount field | answered | `DamageStruct+0x50`, re-combat.md 1.3 |
| section 5 mount object | answered (saddle node RT) | `P+0x1d8` = mount `HOBJECT`, re-firstperson.md 5 |
| section 6 video surface | answered | the UI target, `FUN_00c28220` from the HUD pass, re-ui-effects.md 0, 4 |
| section 7 KBM/pad prompt switch | answered (pushed SDL events RT) | force pad mode, re-input.md 3.4 |
| section 8 orc's half of a counter | answered | cancel + parry recoil, re-combat.md 3.6 B |
| section 10 shadow passes in `FUN_007d6e20` | answered | sun cascades `FUN_007e2940` from `FUN_007dc0b0`, other casters `FUN_007e0be0`/`FUN_007e4a00` from `FUN_007db760`, re-stereo.md 3 |
| head node name; eye height | RT | no head name in the binary (re-firstperson.md 2.2, re-animation.md 2.4, 5); P1/P3 `nodes` (runtime-probes A2) |
| piece names | RT | runtime-probes P1q (`pieces`); section 4 |
| FreeRun, attack/counter/stun, chord, stock pad bindings (4 places) | RT | runtime-probes I2 (`bindings`); section 5 |
| `Client.PlayerMovement` attribute names | RT | list the console repository at runtime (section 2.5); set them with G22 |
| sword length; orc/beast node names and attachments | RT | weapon model nodes and `*(sm+0x90)` attachments (re-animation.md 4; probe word not written yet), AimAtNode records (re-combat.md 5) |
| player attack record names; mounted attack records | RT | `Combat.Debug.*` logging, now switchable with G22 |
| vault duration | RT | measure in a traversal drill |
| HUD tile rectangles | RT | HUD-only frames (runtime-probes P4) |
| "climbing now" flag | RT | animation state tags, re-firstperson.md 5 |
| remain | needs RE | render-node shadow flags (section 4), `Combat.RagdollReaction`, the AI block event (twice), per-character animation rate, slot 51 and `APPLYDAMAGE`, sampler at an arbitrary time, projectile hit path (`FUN_027be5d0`, re-combat.md 4.2), sword glow FX, dagger shown / detached (2), brutalize, Wwise events, the climb state machine, UI scene camera, marker icon field, prompt list, the layer's G-buffer passes, achievements |

## 2. G22: the console/cfg executor

### 2.1 The pieces

| Piece | Where | Conf. |
|---|---|---|
| **Run a string** | `CWBEngine::vf_32` `0x8bd8b0` (vtable slot `0x344d650`): `int (IWBEngine*, const char*)` = `FUN_008b9ac0(FUN_008bd130(), s)`, returns 0; `this` is not used | high |
| Executor | `FUN_008b9ac0(varTable, const char* s)`: tokenizer `FUN_008bacf0`, then per command `repo->vf_02(name, argc-1, argv+1)`; if no program has the name: `repo->vf_10` (find-or-create an entry `FUN_024566e0`, store the args `FUN_02456040`; most likely a pending value for a variable registered later **[med]**), and the engine's own variable table (`0x466b9a0`, 128 hash buckets, name hash `h*0x397 + T[c]`) stores it as float + string | high |
| Syntax | tokens split on spaces; `;` (or any control character) ends a command, so one string can carry several; `"..."` and `(...)` group; `%` escapes the next character; at most 64 tokens, a 0x1800-byte token buffer; the input is not written | high |
| cfg files | `FUN_008b9df0(varTable, stream)`: one line (NUL/LF/CR, max 511 chars) at a time into `FUN_008b9ac0` (call `0x8b9ebd`); the engine init `FUN_008b69d0(engine, 5, slots)` (two direct executor calls `0x8b6c44`, `0x8b71ad`) gets the five config slots that `FUN_008fe0b0` (the `PrimaryEngine` entry) fills: `projectconfig`=`projectconsole.cfg`, `settingsconfig`=`\USER\settings.cfg`, `renderconfig`=`\USER\render.cfg`, `cinematicconfig`, `userconfig`=`autoexec.cfg` (command-line overrides through the args table `*0x467b758` vtbl+0x48) | high (calls), med (order) |
| Repository | `CConsoleRepository` static at `0x4b5e920` (`FUN_02455fd0()`), vtable `0x3910cc0`: 2 execute(name, argc, argv) (all case-insensitive matches; true if any), 3 set output fn (`+0x8`), 4 printf to the output, 5 for each(fn, ctx), 6/7 by index, 8 find(name) -> program, 9 count (`+0x10`), 10 pending value, 11 register (`FUN_0072ebc0` = `repo->vf_11(prog)`); lock `+0x48` | high |
| Programs | `CConsoleUserProgram` (vtable `0x336b4b0`): `+0x8` name, `+0x10` help, `+0x18` source path, `+0x20` line, `+0x38` handler `void(int argc, char** argv)` (argv without the name); `vf_02` jumps to `+0x38`. Variables `CConsoleProgramVariable_Bool/Int/UInt/Float/String` parse the value in `vf_02` (`0x7a0a60` bool: "expecting 0 or 1", echo `%s = %d`) | high |
| Dispatch thunks | `MOV EDI,impl; JMP FUN_02aeea40` = client command, runs `impl(argc, argv)`. `... JMP FUN_02aeea50` = server command: runs only if `CWBEngine::vf_79` (`0x8bd4f0`: `*(*(void**)0x466aee0 + 0xc58) != 0`), else silently nothing; the field is most likely the local server, i.e. a loaded world | high (gate), med (meaning) |
| Release build | all wanted registrations are static initialisers in `.init_array`: `_INIT_281` (`Teleport`), `_INIT_1264` (`Faction.*`), `_INIT_1336` (`Character.*`), `_INIT_1406` (`SlowMO.*`), `_INIT_374` (`PlayerInput.Bindings.Dump`), `FUN_0056ff70` (`SetResolution`, `RunWorld`, `MapCapture`; reached by `jmp` from the init entry `0x571020`). Some debug bodies are compiled out: `PlayerInput.Bindings.Dump` (`FUN_00d9ea10`) and `list` (`FUN_008cd098`) walk their data and print nothing | high |

### 2.2 Calling it from the mod

```c
void **eng = *(void ***)0x466afa8;                                   /* IWBEngine* */
int (*run)(void *, const char *) = (int (*)(void *, const char *))(*(void ***)eng)[32];   /* 0x8bd8b0 */
run(eng, "Combat.Debug.Attack 1; Teleport 1520 300 -880");
```

On the game thread only (G1), never from inside a console handler or a cfg run. For server commands wait until
`CWBEngine::vf_79` is true (a world is loaded) and the client player exists (`FUN_009e3570(*0x46a9560)`). No hook is needed.

### 2.3 The commands the mod and the tests want

| Command | Impl | Args | Gate | Effect | Conf. |
|---|---|---|---|---|---|
| `Teleport x y z` | `Cmd_Teleport` `0xa36ae0` (thunk `0xa33620`, program `0x46b24a0`) | 3 numbers, game units (cm) | needs the local player | `Player_Teleport(P, {x,y,z}, current rotation)` | high |
| `SlowMO.Set r` / `SlowMO.Set n d` | `FUN_02824cd0` | one rate (`<= *(double*)0x39acc28`) or two integers | server | writes the game timer's scale `*(*(void**)0x463e290 + 8) + 0x20/+0x24/+0x28` (num, den, float); only if `*0x4c48cc0` (the slow-mo manager) exists | high (code), med (limit) |
| `SlowMO.Start/Resume/Pause/Stop` | `0x2824b30`, `0x2824bc0`, `0x2824a60`, `0x2824a30` | - | server | 13-48 byte bodies (manager calls) | med |
| `Character.SetLevel <name> <n>` | `FUN_0268e9e0` | object name, level | server | `objsys` vtbl+0x150(name) = find by name -> `*(*(obj+0x38)+0x6d0)` = its `Combat` -> `FUN_026ee7c0(combat, n-1)` | med (parser `FUN_00c78740`) |
| `Character.PurchaseSkill / GiveItem / GiveRunesBySlot / SetPowerLevel / SetPersonality / DumpInventory` | `0x268eee0`, `0x268ea70`, `0x26917c0`, `0x268e810`, `0x268f830`, `0x268e940` | object name first (the player's: RT) | server | | med |
| `Faction.Dump <file>` | `FUN_022a6ed0` (thunk `0x22a60f0`) | exactly one file path | none | serialises the faction (Nemesis) state (`*0x46b7310` vtbl+0x78 -> `FUN_023d6220`) and writes it to the file (`FUN_00fbc460`) | high (code), RT (path base, format) |
| cvars, e.g. `Combat.Debug.Attack 1` | the variable's `vf_02` | value | none | sets it, echoes `%s = %d` | high |
| `SetResolution w h iw ih` | in `FUN_0056ff70` | | | registered, so runtime-probes P4 can use it instead of seeded prefs | med |
| `PlayerInput.Bindings.Dump` | `FUN_00d9ea10` | | | prints nothing in this build (section 5) | high |

### 2.4 Output

`repo->vf_04` formats into 0x200 bytes and calls `*(fn*)(0x4b5e920 + 8)` as `fn(const u32* colour, int level, const char*
text)`; the default `FUN_008bb120` forwards to the engine console text buffer (`FUN_008bce00(0x466b8e0, ...)`). To capture
command output: save the pointer, put ours there (or `repo->vf_03(fn)`), chain to the saved one **[med]**.

### 2.5 Traps

- Unknown names produce no error: they become pending variables (typos are silent). Log `repo->vf_08(name)` (find) first.
- Server commands do nothing before a world is up, also silently.
- The repository can be listed: `vf_09` count, `vf_07(i)` program by index (`+0x8` name). That gives the database-driven
  cvars such as the `Client.PlayerMovement` attributes at runtime (design section 2) **RT**.
- `Faction.Dump`, `Character.*` touch the save state only through the game's own systems; on the dev save only (D16).

## 3. PlayerMovement (`P+0x2c8`): where VR locomotion writes

### 3.1 The frame

| Step | Where | Conf. |
|---|---|---|
| stick -> animation inputs | `FUN_00a3c8d0` / `FUN_00a3c350` (re-input.md 4) | high |
| the solve per step | `PlayerMovement::vf_10` `0xab59a0`: the engine movement solver `FUN_020cda10` fills a record `{u64, float dt, LTRigidTransform xf @ +0xc}`, then keeps the character upright | med |
| **root motion onto the transform** | `PlayerMovement::vf_11` `0xab5e70` `(pm, LTRigidTransform* xf)`, vtable slot `0x34fc838` (below) | high |
| apply | `PlayerMovement::vf_04` `0xab79b0` `(pm, record*)`: `FUN_00ab77c0`, `FUN_020da420(animMgr+0x3968, &xf)`, `objsys` vtbl+0x98(playerObj, &xf) | med |

The caller of slot 11 is the engine's movement code (an indirect call; not found statically) **RT**. There is no velocity
or speed input anywhere in the 35 slots: the speed is the walk/run animation's root motion.

### 3.2 `vf_11` and the free extra delta

1. `delta = FUN_00a513a0(PlayerBody)` = this frame's root motion `{pos, quat}` (identity while `FUN_00a85590(PlayerCombat)`).
2. If the byte `pm+0x444` is set: `delta.pos += R(delta.rot) * extra.pos`, `delta.rot = delta.rot * extra.rot`; then the
   extra is reset to identity and `+0x444` cleared.
3. Unless an attack runs (`pm+0x38` and `Combat_GetCurrentAttack`): `vf_30` (`*(PlayerBody+0x3b8)+0x62a`) zeroes the
   translation, `vf_31` (`+0x629`) the rotation (animation locks).
4. `xf.pos += R(xf.rot) * delta.pos`; `xf.rot = normalize(xf.rot * delta.rot)`.

| Field | Meaning |
|---|---|
| `pm+0x428` | float[3] extra translation, Talion's local frame (x right, y up, z forward), game units |
| `pm+0x434` | float[4] extra rotation quat x, y, z, w (`w` at `+0x440`) |
| `pm+0x444` | byte: pending, consumed by the next `vf_11` |

Only the constructor `FUN_00ab6640` and the reset in `vf_11` write these (scan of the whole text for writes to `+0x444`,
`+0x428`, `+0x440`): the game never uses the slot **[high]**.

### 3.3 Recipe (design section 1, room-scale step 2)

Each frame in G1, before the player update: `d` = (HMD - root) in the horizontal plane, clamped to the catch-up speed x dt;
`extra.pos = R(rootRot)^-1 * d`, `extra.rot = identity`, `pm+0x444 = 1`. It rides on the animation's own motion, so stairs
and the object move are the game's; the walk animation does not play for it (fine for small catch-ups; above ~0.5 m/s keep
the virtual stick of G3, or the stick entry M1 `FUN_00ad0af0`, re-input.md 3.3). Translation is dropped while an animation
locks it (step 3): the right behaviour in paired actions. Whether the move collides is **RT**; until known, clip `d` with
a G21 ray from the root.

**Status (2026-10-19): it collides, and the recipe is built (offline).** Run 3 (P5 K2, runtime-probes.md): one vf_11 per
tick on the game thread, `pm = Player+0x2c8`, the extra consumed by the next call in Talion's frame, and at a wall the
collision after vf_11 slides the move (17.8 of 30 cm kept), so no G21 ray is needed. `mod/somvr` `SOMVR_FOLLOW=1` (with
`SOMVR_BASE=head` or `player`; re-xr-pipeline.md 6.1): hook PM on slot `0x34fc838` writes `+0x428 = R(xf.rot)^-1 d`
and `+0x444 = 1` right before vf_11, only on the player's PlayerMovement with the fields at rest (never two pending);
`d` = the tracked head's horizontal offset from the base once it exceeds 8 cm (design section 1), at most 30 cm a frame
(a step clamp, not the catch-up speed x dt yet); the next frame's H1 takes the root's move minus vf_11's own part as
what the game applied and shifts the rig's origin by it (the view never moves with the body); `FOLLOW` logs requested
vs applied per second. Selftest only (stand-in vf_11 + a wall); the first runtime check is the next headset boot.
Still open: the ~0.5 m/s case (stick instead), the head-in-wall fade (design step 4), the body yaw follow.

## 4. G12: head pieces and a shadow-only head

- **Piece names: not in the binary** (re-firstperson.md 3.1, re-animation.md 6; no new string in this pass). They are in
  the model files; the probe enumerates them: IEngineModel slot 3 count (vtbl+0x18) and slot 14 name by index
  (vtbl+0x70) over `*(model+0x28)+0x38` (runtime-probes P1q, word `pieces`). Expected: the parts that
  `FUN_0236b820` assembles (`SMC_HeadModel`, `SMC_HairModel`, `SMC_HelmetModel`, ...) **[high]**.
- **Shadow-only flags: open.** One hidden-piece mask per skeletal node (`node+0x130`); the candidate bits are
  re-firstperson.md 3.2's table. This pass mapped the render code that tests `+0x41`/`+0x42` (list builder
  `FUN_007e91a0` bit 0, `FUN_007fc160` bit 2, `FUN_00800ff0`/`FUN_008023f0` bits 1-2, the paired draw paths
  `FUN_008229a0`/`FUN_00824570` `+0x42` bit 2) but not which one the shadow lists use **[low]**. Order: P2q first (a
  hidden piece may keep its shadow, then nothing is needed), then the near plane (P3q), then this trace.

## 5. The stock pad buttons (FreeRun, attack, counter, stun)

- Data only: `Profile/BindingSets` / `CommandSets` in the game database (re-input.md 2.2); no button table in the binary
  **[high]**.
- New: `PlayerInput.Bindings.Dump` cannot help: its body `FUN_00d9ea10` walks the command list and looks up each name
  (`FUN_025aa580`) but the print was compiled out **[high]**. The probe's reader (runtime-probes I2: `PlayerInput+0x60`
  entries, input ids `h+0x20..+0x2c`, decoded with re-input.md 1.3) is the only route.
- The combat level (which `CombatPrimaryNN` is strike, counter, stun) needs `Combat.Debug.Input` / `InputString` logging,
  now switchable with G22, while P1 presses each pad button. The published layout (X strike, Y counter, B stun, A
  vault/evade, RT free-run) stays unverified.

## 6. G14: paired actions and struggles

### 6.1 Sync actions (`CCombatSyncAction`, `*(Combat+0x188)`)

| Event | Where | Conf. |
|---|---|---|
| **Start** | `Combat_StartAttack` (`0x26eb380`) calls `FUN_028081a0(sync = *(Combat+0x188), HRECORD syncRecord, HOBJECT target)` at `0x26eb93c` (its only call); returns bool; logs "New Sync (%s)", aborts the source's / target's previous sync, "Failed to start properly" | high |
| Participants | `sync+0x10` = the active record (the teardown returns early when 0), `sync+0x1c0` = source `Combat*`, `sync+0x1f8` = target `Combat*` (`FUN_026c2d70(Combat*)` -> `HOBJECT`), handles at `sync+0x38` / `+0x58` | med |
| **Stop** | teardown `FUN_028060a0(sync)`: `FUN_026c6f30(combat, otherHObj, record)` on both sides, release `FUN_02252bb0(sync+8, +0x38 / +0x58)`. Called by every end path: attack ended `FUN_02806170` ("Release/Abort (%s) AttackEnded"), interrupted `FUN_028062d0` (from the recoils `FUN_02811140`, `FUN_02810af0`), a new attack `FUN_02806210`, attach failure `FUN_02806550`, the start's failures, the destructor `CCombatSyncAction::vf_01` | high |
| Stop (2) | the queued abort `FUN_02805fa0` (from the per-frame `Combat::vf_13`) inlines the same teardown: hook it too | high |
| Visual end | "Sync fully blended out '%s'" in `FUN_02252f20` (shared sync update) | med |

G14 = post-hook the call at `0x26eb93c` (if it returned true and the Combat is the player's, `*(CPlayerObj+0x6d0)`: log
the record, duration and root displacement for the presentation table) and entry hooks on `FUN_028060a0` and
`FUN_02805fa0` (if `sync` is the player's).

### 6.2 Struggles (`ICombatStruggle`: `CStrugglePressCount`, `QuickTime`, `Aim`, `Hold`, `InputDirection`)

| Event | Where | Conf. |
|---|---|---|
| Start, from a sync action | `FUN_00e16ef0(PlayerCombat*, ...)` at `0xa84a6c` ("Struggle = %s; Sync Action = %s") -> stored at `PlayerCombat+0x8e0` | high |
| Start, from a combat state | `FUN_00e15690(PlayerCombat*, ...)` at `0xa86d64` ("Struggle = %s; Combat State = %s") -> `PlayerCombat+0x8d8` | high |
| Stop | the old object is deleted (vtbl+0x8) and the pointer zeroed before a new one; event `CCharacter.OnCombatStruggleEnd` (`CombatStruggleEndArgs`). Poll `+0x8d8`/`+0x8e0` != 0 per frame | med, RT (cleared at the end) |

`PlayerCombat = *(P+0x2e8)`.

## 7. G16: `PlayerCameraPresentation`

| Piece | Where | Conf. |
|---|---|---|
| Object | **`pcp = *(P+0x308)`** (ctor `FUN_00a78670`, vtable `0x34f5c68`, 0xc78 bytes; re-gameplay.md's [low] `+0x308` row); `pcp+0x8` = `P` | high |
| Per frame | `FUN_00a78220(pcp)` from the Player update (call `0xa41250` in `FUN_00a40fe0`, the update that runs every frame - run 3, re-firstperson.md 1.3; `0xa41939` is the same call in the twin `FUN_00a416a0`, which never ran): sets the animation flag **`InPresentation` = `pcp+0x90 != 0`** (`FUN_0208dc90(animMgr, "InPresentation", bool)`), counts down `+0x1f4`/`+0x1f8`, runs the queue (`FUN_00a751c0`; "Queued sequences disabled: level camera active") | high |
| Start | `FUN_00a6d740` ("New presentation is NULL", "Previous presentation type %s is not a %s", predicates), from `FUN_00a73a50` / `FUN_00a72d50` (calls `0xa73f5b` / `0xa7325b`; both also log "Break predicate succeeded"); combat presentations through `FUN_00a775f0` (from `0x26e05f0`) | med |
| Detect | poll `*(pcp+0x90)` (the current presentation) in G1: rising = start, falling = stop | med, RT |
| Camera pose | the presentation drives the camera through the modifier stack (design: `cam+0x340`) | RT |

## 8. G9: iterating all objects of a class

The `list <class>` command (`FUN_008cd098`; its print is compiled out) has the loop: `cls = objsys vtbl+0x78(name)`
(GetClass, e.g. "NPC"); for each object list node `n` from `*(*(void**)0x466aee0 + 0x400)` until the sentinel `+0x3f8`
(next at `n+8`, container `c = n - 0x18`; if `n+0xc038` != 0 it first calls `c` vtbl+0x20): `o = c vtbl+0x10(c, prev)`
until 0, and `objsys vtbl+0x80(*(o+0x48), cls)` (or `*(o+0x50)`) = IsKindOf **[med; RT: the meaning of `o+0x48/+0x50`,
whether vtbl+0x20 is a lock]**. That gives G9 every orc in range without waiting for the AI's attacker scoring.

## 9. Check sites

Format of `mod/spike1/check_sites.py` (verified with `scripts/check_doc_sites.py`):

```
SITE("G22_RUNSTRING", K_VSLOT, 0x344d650, 0x344d640, "20 d3 8b 00 00 00 00 00 30 d3 8b 00 00 00 00 00 b0 d8 8b 00 00 00 00 00 80 d8 8b 00 00 00 00 00 40 d3 8b 00 00 00 00 00", 0x8bd8b0, "53 48 89 f3 e8 77 f8 ff ff 48 89 de 48 89 c7"),
SITE("G22_SERVERGATE", K_VSLOT, 0x344d7c8, 0x344d7c0, "40 e0 8b 00 00 00 00 00 f0 d4 8b 00 00 00 00 00 f0 d9 8b 00 00 00 00 00", 0x8bd4f0, "48 8b 05 e9 d9 da 03 48 83 b8 58 0c 00 00 00"),
SITE("G22_EXEC", K_ENTRY, 0x8b9ac0, 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a 00", 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a"),
SITE("G22_REPO_EXEC", K_VSLOT, 0x3910cd0, 0x3910cc0, "20 65 45 02 00 00 00 00 30 66 45 02 00 00 00 00 50 5c 45 02 00 00 00 00 10 5a 45 02 00 00 00 00 20 5a 45 02 00 00 00 00", 0x2455c50, "41 57 41 56 41 55 45 31 ed 41 54 55 53 48 83"),
SITE("G22_CFG_LINE", K_CALL, 0x8b9ebd, 0x8b9eb0, "2c 10 00 48 8d 74 24 10 4c 89 ef 31 ed e8 fe fb ff ff", 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a"),
SITE("PM_ROOTMOTION", K_VSLOT, 0x34fc838, 0x34fc828, "30 bb ab 00 00 00 00 00 a0 59 ab 00 00 00 00 00 70 5e ab 00 00 00 00 00 40 bb ab 00 00 00 00 00", 0xab5e70, "55 48 89 fd 66 0f ef db 53 48 89 f3 48 83 ec"),
SITE("PM_APPLYXF", K_VSLOT, 0x34fc800, 0x34fc7f0, "b0 bb ab 00 00 00 00 00 00 bc ab 00 00 00 00 00 b0 79 ab 00 00 00 00 00 60 b2 ab 00 00 00 00 00", 0xab79b0, "41 55 41 54 55 48 89 f5 53 48 89 fb 48 83 c5"),
SITE("G14_SYNC_START", K_CALL, 0x26eb93c, 0x26eb925, "48 8b bb 88 01 00 00 48 85 ff 0f 84 2d 19 00 00 48 8b 34 24 4c 89 e2 e8 5f c8 11 00", 0x28081a0, "41 57 41 56 49 89 f6 41 55 41 54 49 89 d4 55"),
SITE("G14_SYNC_END", K_ENTRY, 0x28060a0, 0x28060a0, "41 55 41 54 55 53 48 83 ec 08 4c 8b 6f 10 4d 85", 0x28060a0, "41 55 41 54 55 53 48 83 ec 08 4c 8b 6f 10 4d"),
SITE("G14_SYNC_QABORT", K_ENTRY, 0x2805fa0, 0x2805fa0, "41 55 41 54 55 53 48 89 fb 48 83 ec 08 80 3d a4", 0x2805fa0, "41 55 41 54 55 53 48 89 fb 48 83 ec 08 80 3d"),
SITE("G14_STRUGGLE_SYNC", K_CALL, 0xa84a6c, 0xa84a5b, "48 c7 85 e0 08 00 00 00 00 00 00 4c 89 fe 48 89 ef e8 7f 24 39 00 48 89", 0xe16ef0, "41 57 49 89 f7 41 56 41 55 41 54 55 48 89 fd"),
SITE("G14_STRUGGLE_STATE", K_CALL, 0xa86d64, 0xa86d50, "48 c7 83 d8 08 00 00 00 00 00 00 48 89 ea 4c 89 e6 48 89 df e8 27 e9 38 00 48 89", 0xe15690, "41 57 41 56 41 55 41 54 55 48 89 d5 53 48 89"),
SITE("G16_PRES_UPDATE", K_CALL, 0xa41250, 0xa41243, "00 00 85 d2 75 0c 48 8b bb 08 03 00 00 e8 cb 6f 03 00", 0xa78220, "55 66 0f ef c0 53 48 89 fb 48 83 ec 18 48 8b"),
SITE("G16_PRES_START_A", K_CALL, 0xa73f5b, 0xa73f4c, "80 e6 01 00 00 01 48 8b b0 90 00 00 00 41 57 e8 e0 97 ff ff", 0xa6d740, "41 57 41 56 41 55 49 89 cd 41 54 55 53 48 81"),
SITE("G16_PRES_START_B", K_CALL, 0xa7325b, 0xa7324c, "80 e6 01 00 00 01 48 8b b0 90 00 00 00 41 57 e8 e0 a4 ff ff", 0xa6d740, "41 57 41 56 41 55 49 89 cd 41 54 55 53 48 81"),
```

`G22_*` need no hook (the mod calls `0x8bd8b0`); the sites only prove the build. `G14_SYNC_END`'s first 14 bytes are
plain (pushes, `sub`, `mov r13,[rdi+0x10]`); `G14_SYNC_QABORT`'s byte 13 starts a RIP-relative `cmp byte [rip+..]`, so a
patch longer than 13 bytes needs a relocating trampoline.

## 10. Runtime checks (what to measure)

| ID | Question | Measure | M |
|---|---|---|---|
| X1 | G22 works in process | in a loaded world: `run("Combat.Debug.Attack 1")` then read the bool (address in `reference/work-gameplay/combat_cvars.tsv`); `repo->vf_08("Teleport")` != 0; `Teleport` to the current position + 100 cm and log the root; `Faction.Dump <abs path>`: does the file appear, where (VFS base or cwd)? | M1 |
| X2 | Root-motion slot | count `PM_ROOTMOTION` calls per frame (expect 1 per step); write `extra.pos = {0,0,10}` once and log the root next frame (+10 cm forward); repeat facing a wall: does it stop at the wall? | M2 |
| X3 | Sync start/stop | in a counter or execution drill: the two G14 hooks fire in pairs; `sync+0x10` before/after; record name, duration, root displacement | M4 |
| X4 | Struggle pointers | `PlayerCombat+0x8d8/+0x8e0` over an interrogation or a caragor struggle: set at start, 0 at the end? | M4 |
| X5 | Presentation flag | `*(pcp+0x90)` over a captain introduction: rising/falling with the presentation | M8 |
| X6 | Object iteration | the section 8 loop with GetClass("NPC") near an orc camp: count and positions match the visible orcs | M4 |
