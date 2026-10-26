# Shadow of Mordor (Feral Linux build): the skeletal pose pipeline and a write point for Talion's arms (VR arm IK)

Static analysis only (2026-10-18): read-only Ghidra batches on `som-linux-q2` (`scripts/ghidra_query.sh`, batches
`b1..b6.txt`, answers `q1..q6.txt`), `objdump` of address ranges of `reference/bin/linux/ShadowOfMordor`, the TSV
indexes and small offline scripts. Nothing was launched, nothing in the install was read, nothing was written to a
Ghidra project. `SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`. Scratch (git-ignored): `$SOM/reference/work-anim/`
(`riprefs.py` = RIP-relative/imm32 reference scanner, `rd.py` = read bytes/floats at an address, `somhash.py` = the
node-name hash, `cbtypes.py` = vtable-call scanner, disassembly ranges `d_*.txt`, the extracted `s_*.txt`).

Builds on `docs/re-firstperson.md` (cited **FP**: player objects, camera modifier stack, head-bone API, piece mask),
`docs/re-gameplay.md` (**GP**: `IEngineModel* = *0x46a9230`, stub -> `CSimpleModel` map), `docs/re-view-render.md`
(**VR**: frame loop, render thread), `docs/re-combat.md` (**CB**: schema/attribute decoding) and `docs/design-vr.md`
(**DV**: G11, M3). It does not repeat what those establish. Conventions as there: addresses = runtime addresses
(non-PIE), `vf_NN` = vtable slot NN, `vtbl+0x130` = byte offset into a vtable, `+0x58` = byte offset into an object,
units cm, +Y up. Confidence: **[high]** read in the code, unambiguous; **[med]** read in the code, meaning inferred;
**[low]** inference; **RT** = needs a runtime check.

Short names: `m = *(IEngineModel**)0x46a9230`; `sm` = a character's `CSimpleModel*` (`FUN_008f3c20(hObj)`, FP 2.1);
`trk = *(sm+0x178)` (`CSimpleModelTrackerNode`, the animation node); `out` = the model's `CSimpleModelOutputPose`;
`skel = *(*(sm+0x28)+0x10)` (the skeleton data); `mmgr` = the engine's model manager `CSimpleModelMgr`
(`CWBEngine::vf_09` `0x8bd240` = `*(*(void**)0x466aee0 + 0xbb0)`); `IAE` = `IAnimationEngine*` (`FUN_02ad3f20` =
`*(void**)0x4c5c0d8`, the `CEngineModelAnimationJobEngineImpl`); `IEA` = `IEngineAnimation*` `*(void**)0x4b5b108`
(`CEngineAnimationImpl`, vtable `0x348aef8`). A *pose* below is an array of 0x20-byte node transforms (section 2.1).

## 0. Summary

| Question | Answer | Conf. |
|---|---|---|
| Pipeline | per model a pose graph of `IModelPoseNode`s run in 8 phases by `CSimpleModelMgr` (`*(engine+0xbb0)`): phase 0 time, phase 1 **job build+queue**, (player update), **drain-and-wait `0x9eae7f`**, phase 2 tracker takes the model-space result, 4/5 physics, 6 cloth, 7 skinning + attachments + render-node copy | high |
| Threads | sampling, blending, local->model space and the node callbacks run on `EngineAnimationJobThread1..3` between `0x9eae55` and `0x9eae7f`; everything else on the game thread | high |
| Order vs player update / camera / H1 | the **player update runs while the jobs run**, so it (and FP's camera F1 and head read F6) sees last frame's pose; this frame's pose exists from phase 2 (`0x9eae9a`), skinned at phase 7 (`0x9eb0ae`), both before BeginFrame/H5 and H1 | high |
| Pose format | model space, 0x20 bytes per node `{pos[3], pad, quat x,y,z,w}`; local poses exist only inside the job; skin = model x inverse bind (still pos+quat) | high |
| Skeleton | `*(*(sm+0x28)+0x10)`: 0x40-byte nodes (parent byte `+0`, bind pos `+0x24`, bind quat `+0x30`), sockets 0x30 bytes, **name pointer per node `+0x38`**, hash per node `+0x40`, inverse bind `+0x48` | high (RT: names resident) |
| Name hash | `h = h*0x397 + T[c]`, 32-bit, table `0x38d7480` (case-insensitive); `somhash.py`; e.g. `R_Hand` `0xc7485ad3`, `Neck` `0x87ec5687`, `SocketHead` `0x7b92d623` | high |
| Engine's own bone override | per-node callbacks in the job: types (32, `0x46a7e60`) via IEngineModel vtbl+0x120/+0x128, per node via vtbl+0x130/+0x138; the game uses 0x00-0x12 (incl. `CAnimationMgr::IK` = a two-bone IK, `ProceduralBody::Arm`) | high |
| **Recommended G11 write point** | **post-hook `CSimpleModelTrackerNode::vf_03`** (vtable slot `0x38df398`), player's tracker only: game thread, after the jobs and the player update; edit the model-space arm chain + subtree; physics, cloth, skinning, attachments and `GetNodeTransform` all follow | high (mechanics), RT (visual proof, check 3) |
| Alternatives | B: pre-hook `CSimpleModelOutputPose::vf_03` (`0x38de568`, render + attachments only); C: own node-callback type through the public IEngineModel API (no patch, worker thread, targets frozen at phase 1) | high |
| Weapons | separate `CClientInventoryItemModel` objects attached with `CSimpleModel::vf_42` to the node/socket named by the item record's `ActiveNode`/`InactiveNode`; placed every phase 7 from the parent pose x offset, so **they follow an overridden hand**; offset per entry `+0x44..+0x5c` | high |
| Head / eyes | no head name in the binary; head = parent of socket `SocketHead` (UI default socket, likely the top of the head) or from the name dump; eyes = head x calibrated offset, read after phase 2 | high / RT |
| Pieces | no Talion piece names in the binary (FP 3 stands) | high |
| Physics | optional physics node (phase 4/5): keyframed bodies fed from the pose (follow an upstream override), ragdoll overrides pose and `GetNodeTransform` when active | med |
| Corrections | GP's IEngineModel slot 17 is "hash -> skeleton index", not GetNodeParent; FP's head read in the player update is one frame old; the per-frame player update is `FUN_00a40fe0` (call `0x9ebcae`), not `FUN_00a416a0` (`0x9eb687`, run 3: 0 calls) | high |

## 1. (a) The per-frame pose pipeline

### 1.1 The objects: one pose graph per model, evaluated in 8 phases

Every `CSimpleModel` owns a small pose graph built in one memory block by `FUN_02357740` (the "MultiNode" builder:
output pose, tracker, optional physics node, optional cloth node, optional attach node, model2move, optional
renderable; no direct caller, it is reached through a pointer; the FX variant is `FUN_023574d0`) **[high]**. The nodes are
`IModelPoseNode`s (13-15 slots); each carries a **phase** number at `+0xe4` (set in its constructor through the qword
written at `+0xe0`) and dirty flags at `+0xe8`, a back pointer to its `CModel` container at `+0x48`, up to 16
dependents at `+0x60..+0xd8` (count `+0xe0`), and two pose buffers: input `+0x50` (spec `+0x28..+0x34`) and output
`+0x58` (spec `+0x38..+0x44`) **[high for the fields, med for "input/output"]**.

| Node (ctor) | Phase | Its evaluate (`vf_03`, vtbl+0x18) does | Conf. |
|---|---|---|---|
| `CSimpleModelRootPose` (`FUN_0244e810`) | 0 | root pose | high (phase) |
| `CSimpleModelTrackerNode` (`FUN_02388270`), at `sm+0x178` | 2 (+ `vf_06` in phases 0/1) | swaps the finished animation job's pose into its output `*(trk+0x58)` (section 1.3) | high |
| `CSimpleModelPositionPin` (`FUN_0237c410`) | 3 | root pin | high (phase) |
| `CSimpleModelCinematicNode` (`FUN_02b17aa0`/`FUN_02b18ce0`) | 4 | cinematic pose | high (phase) |
| `CSimpleModelPhysicsNode` (`FUN_02374260`), only if the builder's flag is set | 5 (+ `vf_06` in phase 4) | ragdoll / physics blend (section 7) | high (phase), med (role) |
| `CSimpleModelClothNode` (`FUN_0235d540`) | 6 | Havok cloth | high (phase) |
| `CSimpleModelOutputPose` (`FUN_02385180`) | 7 | skinning transforms + output listeners (section 1.4) | high |
| `CModel2MoveNode` (`FUN_0244e990`) | 7 | root motion to movement | high (phase), med (role) |

Data moves between nodes through **connections** run at the end of each phase: `CModel::vf_04` (`0x951620`, "evaluate
phase p of this model") calls `vf_03` of every node of that phase, clears `+0xe8`, then runs the model's 0x30-byte
connection records for that phase with `FUN_0094d3a0`: kind 0 = copy node ranges `src->data[a..] -> dst->data[b..]`
(with index remapping), 3 = reset ranges to identity, 4 = copy the buffer header (object transform), 5 = copy the root
motion **[high]**. So a node's output written during phase p reaches the next node's input at the end of phase p.

`CSimpleModelMgr` (`mmgr`, vtable `0x3497430`) runs the phases for all models: `vf_03(phase)` (`0x94d110`) = per-phase
node list A (`node->vf_06(phase, 1)`), then `FUN_0094cc40` (every model marked dirty for that phase ->
`CModel::vf_04`), then list B (`node->vf_06(phase, 0)`). Nodes get into the dirty lists through `FUN_00951ac0(node,
flag)`, which also marks all dependents **[high]**.

### 1.2 One frame (game thread `PrimaryEngine`, inside `CGameClientShell::vf_02`)

```
0x9eae24  mmgr->vf_03(0)   phase 0: trk->vf_06(0): advance animation time and keys (FUN_0238f7b0 -> FUN_02ad9370
                           per tracker); if anything moved, FUN_0238f6e0 re-registers the tracker for phase 1 and
                           marks it + its dependents dirty (flag 4)
0x9eae3f  mmgr->vf_03(1)   phase 1: trk->vf_06(1): BUILD AND QUEUE THE ANIMATION JOB (FUN_02389530(trk, 1))
0x9eae55  mmgr->vf_04()    wake the 3 animation workers (IAE vtbl+0x38 -> FUN_00935800 -> FUN_00942fb0)
0x9eae5f  if interface state != 1 (normal play):  -> 0x9eb610
  0x9eb617    FUN_00ab3e00(players)   (per player FUN_00a42fc0)
  0x9ebcae    FUN_00ab3f80(players) = THE PLAYER UPDATE, app-active byte shell+0x80e48 != 0 (FP 1.3:
              FUN_00a40fe0 -> animation manager FUN_020a4ac0, camera modifier stack, FUN_00a35340 ...)
                                                            <- runs WHILE the jobs run on the workers
              (else 0x9eb687 FUN_00ab3ec0 -> FUN_00a416a0, the same update compiled twice; run 3: never)
  jmp 0x9eae6c
0x9eae7f  mmgr->vf_06()    DRAIN AND WAIT: IAE vtbl+0x48 (0x8ab7c0) -> FUN_00935820 -> FUN_00942fe0 (the game thread
                           runs queued jobs itself, then waits on each worker's semaphore)
0x9eae9a  mmgr->vf_03(2)   phase 2: trk->vf_03 swaps the job result into *(trk+0x58); connections copy it on
0x9eaed5  *(engine+0xba8)->vf_06(dt)   (manager built by FUN_0094c950; not traced)
0x9eaef0  mmgr->vf_03(3)   phase 3 (position pin)
0x9eaf2b  *(engine+0xba8)->vf_06(dt)
0x9eaf46  mmgr->vf_03(4)   phase 4 (cinematic node; physics node pre-step)
0x9eaf7c..0x9eb05a         physics world fixed-step loop (VR 1, step 2: vtbl+0x188 / +0x190 / +0x198 / +0x1a0)
0x9eb078  mmgr->vf_03(5)   phase 5 (physics node: ragdoll result -> pose)
0x9eb093  mmgr->vf_03(6)   phase 6 (cloth)
0x9eb0ae  mmgr->vf_03(7)   phase 7 (output pose: skinning transforms, attachments, copy to the render node)
0x9eb0f2  renderer->vf_29  BeginFrame (spike1 H5: xrWaitFrame / xrLocateViews)
...       world render: FUN_009ee170 -> 0x9ee7d4 H1 (FUN_00a5a820)
```
**[high]** for the calls and the control flow (`d_shellvf02.txt`); the jump to `0x9eb610` is taken whenever the
interface state is not 1 (`FUN_00a10ee0`); in game it is 3 (FP 5), and the player update runs only if the state is 3
(per player in `FUN_00ab3f80`: `FUN_00a10f80`), `FUN_00ab41c0`/`FUN_00ab4270` agree and the game timer is not paused
**[high code, med meaning of state 1]**. **Correction (run 3, 2026-10-19):** the update that runs every frame is
`FUN_00ab3f80` -> `FUN_00a40fe0` (`0x9ebcae`), not `FUN_00ab3ec0` -> `FUN_00a416a0` (`0x9eb687`) as this section said
before: the probe's `P_ANIM_B`/`P_CAMPOST_B` (in `FUN_00a40fe0`) fired 13285 times (= `PMROOT`, once per frame),
`P_ANIM_A`/`P_CAMPOST_A` (in `FUN_00a416a0`) 0 times (runtime-probes.md P1 results; FP 1.3 for the selector byte and
why A is a twin) **[high]**. Both calls sit at the same place between the job wake and the wait and jump back to
`0x9eae6c`, so the order below holds for B unchanged.

Consequences **[high, from the order]**:
- The animation **state** set in the player update of frame N (animation manager, camera modifiers) is sampled by the
  job of frame N+1: one frame of state latency, as in most engines.
- During the player update the tracker's pose (what `GetNodeTransform` reads, section 2.5) is **the previous frame's**:
  this frame's job is still running on the workers. This answers FP 2.3's first runtime check statically: FP's F6
  (post `FUN_00a51210`) reads a one-frame-old head; the **current** frame's final pose exists from phase 2
  (`0x9eae9a`) on, its skinned and attached form from phase 7 (`0x9eb0ae`), both before H5 and H1. The camera written
  by FP's F1 (in the player update) is therefore built from last frame's head bone; a head read after phase 7 (or at
  H1) is exact **[high]**.
- Everything after the drain-and-wait (phases 2-7, H5, H1) runs on the game thread with no animation job alive
  **[high for the phase calls; med overall]**: the next jobs are queued in the next frame's phase 1; job submissions
  outside the phase calls (tools, preview models, forced synchronous updates) were not searched.

### 1.3 The animation job: build, queue, workers, result

| Stage | Where | Thread | Conf. |
|---|---|---|---|
| Time advance, animation keys | `CSimpleModelTrackerNode::vf_06(0)` `0x238fa50` -> `FUN_0238f7b0` (`FUN_02ad9370` per tracker, string-key callbacks `CSimpleModelCallback`) | game, phase 0 | high |
| Build + queue | `FUN_02389530(trk, async)`: job pose = `IEA vtbl+0x1b0(trk+0x138)` (root reset), then `IAE->vf_05` (vtbl+0x28, `0x8ad330`, async) or `vf_04` (vtbl+0x20, `0x8ad140`, synchronous "Decode") with `(trk+0x190 /*anim state*/, trk+0x138 /*job pose*/, trk+0x3b0 /*node callbacks, 3.1*/, *(trk+0x168) /*HOBJECT*/, ...)`; `FUN_008abee0` turns the blend tree into an op list (`FUN_0093b8c0(job, op, data)`) | game, phase 1 | high |
| Async or not | `vf_05`: if `DAT_0463e6c0` (set to 1 by the static init `_INIT_145`) -> `IEA vf_51` (vtbl+0x198, `0x933a90`) = push to the worker queue `+0xc018` (`FUN_009431c0`); else `IEA vf_30` (vtbl+0xf0) = run now | - | high |
| Workers | `EngineAnimationJobThread1..3` (`FUN_00942ec0`), loop `FUN_009433c0`: pop, `IEA vf_30` -> `FUN_00933d30` = run the job's ops from the op table `DAT_046a7180` (0x20 per op) on a stack of up to 16 poses | `EngineAnimationJobThread1..3` | high |
| Ops (`FUN_00942a80`, registered with `FUN_0093bc70`) | op 0 `FUN_00947360` = sample one animation (`FUN_00932620`, per-bone mask) into a new stack pose; op 1 `FUN_009474e0` = masked blend of the top two poses; op 2 `FUN_00948530` = push the skeleton's bind pose; **op 3 `FUN_009485a0` = finish**: local -> model space into the job pose (`FUN_00947d60`, or `FUN_00947f60` when node callbacks exist), then the completion object `+0x38` vtbl+0x10(5) | worker | high (op 3), med (op 0 meaning) |
| Wait | `mmgr->vf_06()` at `0x9eae7f` (above) | game | high |
| Result | `CSimpleModelTrackerNode::vf_03` `0x23899b0` (phase 2): if dirty flag 4, `*(*(trk+0x58)+0x40) = IEA vf_55(trk+0x138, old)` (`0x933160`: pointer **swap** of the job pose's data with the tracker's), root motion `trk+0x118..0x130` -> buffer `+0x20..+0x38`. Without an animation it builds the bind pose in model space itself | game, phase 2 | high |

The local pose exists only inside the job (on the op stack); what leaves the job is the **model-space** pose. The
swap means the tracker and the job ping-pong two buffers: the buffer the tracker held last frame is the job's output
next frame, so a write into `*(trk+0x58)+0x40` after phase 2 is overwritten by the next job and never fed back
**[high for the swap, med for "no op reads the previous output"]**.

### 1.4 From the model pose to the GPU

| Stage | Where | What | Conf. |
|---|---|---|---|
| Physics, cloth | phases 5, 6 (section 7) | modify the pose when active | med |
| **Skinning transforms** | `CSimpleModelOutputPose::vf_03` `0x237ce40` (phase 7) | input `*(out+0x50)`: sets node 0 to identity, then `skin[i] = model[i] x invBind[i]` with the skeleton's 0x1c-byte inverse bind transforms `skel+0x48`, into the buffer of `*(out+0x348)` (allocated by `*(0x46a9240)` vtbl+0x60); still rigid transforms (pos + quat), no matrices | high |
| Output listeners | same function: for each of up to 32 `IOutputPoseListener`s at `out+0x220` (count `+0x338`): `vf_02(listener, skel, model pose, skin pose, count, header /*object world xf*/, flag, flag)`; then a second list `out+0x320` (`FUN_020c4440`, `CAttachedRenderable`) | high |
| Copy to the render node | `CSimpleModelRenderable::vf_02` `0x237fde0` (a listener): gets the render node's `ISkeletalTransforms` (`node->vtbl+0x178`, `CSkeletalTransforms` vtable `0x33606d8`), copies the skin transforms of the mesh's bones (remap table `+0x238`, ushort per mesh bone; "Bone count mismatch ..." check), `FUN_0237e1e0`, then releases (`vf_01`). The piece-mask update of FP 3.1 happens in the same function | game, phase 7 | high |
| Attachments | `CSimpleModelAttachNode::vf_02` `0x235a660` (a listener): section 4 | game, phase 7 | high |
| GPU | the render thread uses the render node's skeletal transforms when it executes the frame's commands (VR 1: one frame behind); the constant-buffer upload itself was not traced | render thread | med |

### 1.5 Thread summary

| Thread | Animation work |
|---|---|
| Game (`PrimaryEngine`) | phase 0-1 (time, job build), player update (state for the next frame), drain-and-wait, phases 2-7 (result, physics, cloth, skinning, attachments, render-node copy) |
| `EngineAnimationJobThread1..3` | sampling, blending, local -> model space **and the node callbacks** (section 3.1), between `0x9eae55` and `0x9eae7f` |
| Render thread | draws with the copied skeletal transforms |

## 2. (b) Data layout

### 2.1 Pose buffers and node transforms

A pose buffer object (allocated by `CModel::vf_06(count)` `0x949cc0`, 0x50 bytes) **[high]**:

| Offset | Content |
|---|---|
| `+0x00..+0x1f` | header: the root/object world transform `{pos x,y,z; quat x,y,z,w; scale}` (identity + scale 1 at creation; filled by the root/pin nodes and the kind-4 connections **[med]**); passed to the output listeners as the parent world transform (section 4) |
| `+0x20..+0x3b` | root motion `{pos x,y,z; quat x,y,z,w}` (written by the tracker from `trk+0x118..0x130`) |
| `+0x40` | pointer to the node transforms, `count` x 0x20 bytes, 16-byte aligned |
| `+0x48` | count (= skeleton node count) |

A **node transform is 0x20 bytes, model space, rigid**: `{float pos[3]; float pad; float quat[4] /* x,y,z,w */}`
**[high]**. Model space = the character object's space (`world = objectXf x node`, FP 2.1); children are composed as
`pos_c = pos_p + R(q_p) pos_local`, `q_c = q_p (x) q_local` (bind-pose loop in `CSimpleModelTrackerNode::vf_03`, the
job's `FUN_00947d60`/`FUN_00947f60`), i.e. the Hamilton (x, y, z, w) convention of re-xr-pipeline.md 1.1 **[high]**.
The 4th position lane carries no meaning for us (GetNodeTransform masks it; write 0). Bone scale is not used in the
pose (no per-node scale lane; the 1.0 at node entry `+0x20` below is the only candidate) **[med]**. The local-space
pose exists only on the job's op stack (section 1.3) **[high]**.

The poses a mod can reach, all model space:

| Pose | Address | Valid | Conf. |
|---|---|---|---|
| animation result (what `GetNodeTransform` returns, FP 2.1) | `*(*(trk+0x58)+0x40)` | after phase 2 of the frame; during the player update it is last frame's | high |
| output pose input (after physics, cloth) | `*(*(out+0x50)+0x40)` | after the phase-6 connections, before `out->vf_03` (phase 7) | high (flow), med (exact connection list) |
| skinning transforms (`model x invBind`) | the buffer of `*(out+0x348)` (`vf_00` returns `{data, ...}`) | after `out->vf_03` | high |
| render node copy | `ISkeletalTransforms` of the render node (`CSimpleModelRenderable+0x50` vtbl+0x178), mesh-bone order | after phase 7 | high |

### 2.2 The skeleton (`skel = *(*(sm+0x28)+0x10)`, loaded by `FUN_02ad4a40`)

| Offset | Content | Conf. |
|---|---|---|
| `+0x00` / `+0x08` | node entries, 0x40 bytes each / node count | high |
| `+0x10` / `+0x18` | sockets ("alias" nodes), 0x30 bytes each: `+0x00` name hash, `+0x04` parent node index, `+0x08` offset pos (3 floats), `+0x14` offset quat (x,y,z,w), `+0x24` a scale-like float / count | high (fields used by GetNode/GetNodeTransform), med (names) |
| `+0x20..+0x28` | sorted vector of `{uint hash; uint index}` (binary search `FUN_02ad5290`); indices >= node count address sockets | high |
| **`+0x38`** | **`char*` per node: the node's name** (written by the parser `FUN_02ad3110`: `names[i] = stringTable + offset`, the same pass hashes it) | high (written), **RT** (still valid after loading) |
| `+0x40` | `uint` per node: the name hash (`FUN_02ad5270(skel, i)` returns it) | high |
| `+0x48` | inverse bind transforms, 0x1c bytes per node `{pos[3]; quat[4]}` (used by the skinning) | high |
| `+0x50` | passed to the pose nodes as their buffer spec (`node+0x28/+0x38`); not decoded | low |

Node entry (0x40 bytes) **[high for +0x00/+0x24/+0x30 from the bind-pose loop, med for the rest]**:
`+0x00` byte parent index (`0xff` = root, only node 0; "Only Node 0 can have no parent"), `+0x01` byte own index,
`+0x20` float 1.0 (scale?), `+0x24` local bind position (3 floats), `+0x30` local bind rotation (x,y,z,w). Parents
always have smaller indices than their children (the job converts in index order and reads the parent's final
transform) **[high]**, so one forward pass over the indices visits a chain root before its descendants.

Per model there is a second socket list at `sm+0x158..0x160` (0x30-byte entries, same layout) checked before the
skeleton's sockets (`CSimpleModel::vf_62`, `vf_63`, `vf_68`) **[high]**.

### 2.3 The node-name hash

A node id is the hash of its name (FP 2.1); the code is `CSimpleModel::vf_62` (`0x23530c0`, also inlined in the
skeleton parser `0x2ad3198` and in `FUN_0238d3c0`) **[high]**:

```python
# 32-bit, case-insensitive; T = the 256-byte table at 0x38d7480 (identical copies at 0x38de6c0, 0x3925cc0, 0x3b16c10)
# T: 'A'-'Z' and 'a'-'z' -> 1..26, '0'-'9' -> 27..36, ' ' 37, '_' 38, '-' 39, '+' 40, '=' 41, '[' 44, '{' 46, ...,
#    bytes >= 0x7f and control characters -> 0 (they are skipped in effect)
h = 0
for b in name.encode():
    h = (h * 0x397 + T[b]) & 0xffffffff
```

`reference/work-anim/somhash.py` implements it and asserts its table against the binary. Values (offline):

| Name | Hash | Name | Hash |
|---|---|---|---|
| `Neck` | `0x87ec5687` | `SocketHead` | `0x7b92d623` |
| `R_Hand` | `0xc7485ad3` | `L_Hand` | `0x9b878e09` |
| `R_Armu` | `0x844d7d3d` | `L_Armu` | `0x588cb073` |
| `Pelvis` | `0xaa947741` | `Spine4_joint` | `0x7af43530` |
| `Torso_Base` | `0x4d5b5cb4` | `aimer` | `0xb402f342` |

The binary contains no precomputed hash of these names (byte search), so the values cannot be cross-checked
offline; `GetNode` (IEngineModel vtbl+0x80) returns exactly this hash for an existing name, which is the runtime check
**[high algorithm, RT values]**.

### 2.4 The bone names

- The binary has no bone-name list (FP 2.2; this helper's searches for `Head`, `R_Clav`, `R_Arm`, `R_Weapon`,
  `RightHand`, ... found nothing) **[high]**.
- The skeleton keeps a name pointer per node (`skel+0x38`, section 2.2). **RT (first check of this doc)**: for the
  player's model, for `i < *(skel+0x8)`, log `i`, the parent byte, `((char**)*(skel+0x38))[i]` (guarded by a readable
  check) and `((uint*)*(skel+0x40))[i]`; `somhash(name) == hash` proves the pointers. That one log gives the whole
  arm chain names (expected pattern from the code: `R_Armu`/`L_Armu` upper arm, a forearm, `R_Hand`/`L_Hand`, finger
  and twist children; `_joint` suffixes elsewhere, GP 2) and the head bone (the parent of socket `SocketHead`).
- If the pointers turn out stale: walk the structure instead. The hand node = `GetNode("R_Hand")`; its parent chain
  (`entry+0x00`) gives forearm, upper arm, clavicle; the socket table gives `SocketHead` -> head node index.

### 2.5 API corrections and the read path

- IEngineModel slot 17 (vtbl+0x88, `CSimpleModel::vf_63` `0x2352fa0`) returns the **skeleton index of a node or
  socket hash** (-1 if absent), not the parent (GP 2 calls it GetNodeParent) **[high]**. There is no parent getter
  among the stubs read; use the node entry byte `+0x00`.
- `GetNodeTransform` (vtbl+0xb8, `CSimpleModel::vf_68`): first the physics node's override (`*(sm+0x180)`,
  `FUN_02379c50`, section 7), then - only while a cinematic model sets `DAT_04c5d7d1` - `*(sm+0x170)`, else the
  **tracker** `*(sm+0x178)` through `AnimNode_GetNodeTransform` `0x238fe60` -> `FUN_0238b720` (sockets: node x socket
  offset; `world` flag: x the object transform). So it returns the animation result of section 2.1 row 1, not the
  output pose **[high]**.

## 3. (c) Where to override bones after the animation and before skinning

### 3.1 The engine's own mechanism: per-node callbacks inside the job

Firebird has LithTech-style node controls **[high]**:

- **Callback types**: a global table of 32 entries at `0x46a7e60` (0x28 bytes each, cleared by `_INIT_223`):
  `+0x00` user data, `+0x08` direct function, `+0x10` exec function, `+0x18` snapshot function, `+0x20` mode byte
  (1 = direct). Registered through `IEngineModel` slot 36 (vtbl+0x120, stub `0x91a960` -> `FUN_00946ef0`:
  `(m, type, fnDirect, userData)`) or slot 37 (vtbl+0x128, stub `0x91a980` -> `FUN_00946f30`:
  `(m, type, fnExec, fnSnapshot, userData)`).
- **Per-node callbacks** on one model: `IEngineModel` slot 38 (vtbl+0x130, `0x91ad70` -> `CSimpleModel::vf_81` ->
  `FUN_0238d4e0`) `AddNodeCallback(m, hObj, nodeHash, type, flags, owner, ownerSize)` and slot 39 (vtbl+0x138,
  `0x91adf0` -> `vf_82` -> `FUN_0238d560`) `RemoveNodeCallback(m, hObj, nodeHash, type, flags, owner)`. They edit the
  tracker's table at `trk+0x3b0` (`FUN_02ad3f50` / `FUN_02ad4130`): up to 48 entries of 12 bytes `{byte type; byte
  nodeIndex; int flags; uint ownerSlot}` kept sorted by node index (a new entry goes after the existing ones of the
  same node), and up to 32 owner slots at `+0x248` `{owner*, ushort size, ushort refs}`; overflow logs "Too many
  callbacks".
- **Job build** (game thread, phase 1): `FUN_00946fd0` copies the table into the job step and, for every
  snapshot-mode entry, calls `snapshot(owner, slot)` with a private **64-byte slot** per entry (`step+0x490`).
- **Execution** (worker thread): op 3's `FUN_00947f60` converts local to model space in index order; right after node
  `i`'s model transform exists it runs that node's callbacks with a context
  `ctx = { +0x00 const xf32* objectXf /*the object's world transform captured at job build, {pos,pad,quat}*/,
  +0x08 xf32* node /*node i, model space, writable*/, +0x10 xf32* pose /*all nodes; parents final*/,
  +0x18 extra output or 0, +0x20 the node's position before the callbacks }`
  and calls `exec(ctx, slot64)` (snapshot mode) or `direct(ctx, owner)` (direct mode; reads the live owner from the
  worker thread). Afterwards the node's quaternion is renormalised ("Error: Detected garbage rotation" if its norm is
  off). Children are computed later from the modified parent, so **a callback on a joint moves its whole subtree**.

Types in use (registered by `FUN_021e9f90`, called from the `CGameClientShell` constructor `0x9e9691`; owners from
the `AddNodeCallback` call sites) **[high for the calls, med for the owner names]**:

| Type | Mode | exec / snapshot (or direct) | Added by (nodes, owner size) |
|---|---|---|---|
| 0 | snapshot | `0x21e39f0` / `0x21e1aa0` | procedural body (`FUN_02236bc0` removes) |
| 1 | snapshot | `0x21e8060` / `0x21e1ce0` (copies 0x38 bytes) | `FUN_022383d0`: record `BoneName`/`SmoothTime`/`Pitch`/`Roll` (`*0x4c29ae0`), a damped single bone |
| 2, 3, 4 | snapshot | `0x21e2500`/`0x21e1ad0`, `0x21e2980`/`0x21e1b20` (3 and 4) | `ProceduralBody::Pendulum` (`FUN_0223efa0`) |
| 5, 6, 7, 8 | snapshot | `0x21e3480`/`0x21e1c60`, `0x21e26e0`/`0x21e1b70`, `0x21e2950`/`0x21e1ba0`, `0x21e26b0`/`0x21e1c30` | `ProceduralBody_RegisterNodes` (LeanAndTwist record `Enabled`/`Root`/`UpperBody`/`Shoulders`/`Head`, defaults `Pelvis`, `Torso_Base`, `Spine4_joint`, `Neck`; owner 0x1c8) |
| 9 | snapshot | `0x21e1ec0` / `0x21ed980` | `FUN_020bb220`: nodes `Pelvis` and `aimer` (owner 0x118), an aiming modifier |
| 0xa, 0xb, 0xc | **direct** | `0x21e5270`, `0x21e5240`, `0x21e2b70` | **`CAnimationMgr::IK`** (`FUN_02185380` from `IK::vf_07`/`vf_12`): a 3-joint chain (owner `+0x90/+0x94/+0x98`), owner 0x110. `0x21e5270` is a two-bone solver in model space ("Model space target position is NaN", "Storing NaN as vSolutionUp", weight `+0x108`) |
| 0xd | snapshot | `0x21e1d10` / `0x21ed9b0` | `CAnimationMgr::Rotator` (`Pelvis`, FP 4) |
| 0xe | snapshot | `0x21e2e70` / `0x21ed9d0` | `CAnimationMgr::AlignToBrush` |
| 0xf | snapshot | `0x21e9010` / `0x21ed9e0` | `CAnimationMgr::SyncBone` |
| 0x10 | snapshot | `0x21e48e0` / `0x21eda50` | `CNodeTracker` (`0x244e..0x2450`) |
| 0x11 | direct | `0x21ed970` | `ProceduralBody::Arm` elements (`FUN_02247e30` adds, `FUN_02247d00` removes) |
| 0x12 | snapshot | `0x21edb10` / `0x21edaf0` | `ProceduralBody::Arm` (`FUN_02247e30`) |
| 0x13, 0x14 | ? | **no registration found** (scanned every IEngineModel+0x120/+0x128 call and tail jump) | PlayerBody `FUN_00a50d20` (from the body (re)build `FUN_00a52a50`): the client-settings record's `PrimaryCameraShakeNodes` / `SecondaryCameraShakeNodes` (`*0x4c23440` fields 17/18), owner = PlayerBody (0x5f0) |
| 0x15..0x1f | free as far as the code shows | | |

So the game already runs a data-driven two-bone IK (`CAnimationMgr::IK`, "Modifier.IK" in the state machine) and a
procedural arm (`ProceduralBody::Arm`, `ProceduralBody.IK.HandGuardDist`) **inside the animation job** - both are
candidates that could move Talion's arms and must be overridden by (or ordered before) ours **[high mechanism, RT
whether they are active on Talion in normal play]**.

### 3.2 Candidates

| # | Write point | Site | What it gets | Space | Thread / timing | Player only? | Verdict |
|---|---|---|---|---|---|---|---|
| **A** | **post-hook `CSimpleModelTrackerNode::vf_03`** (phase 2 evaluate) | vtable slot `0x38df398` -> `0x23899b0` (`AN_TRK_EVAL`) | `this = trk`; after the call `*(*(trk+0x58)+0x40)` = this frame's final animation pose (model space), `trk+0x168` = the HOBJECT | model | game thread, after the drain-and-wait and after the player update (object transform as the player update left it; the physics step between phases 4 and 5 may still move it, RT check 4); nothing else touches the buffer until the next phase 2 | filter `this == *(playerSm+0x178)` (all characters pass through) | **recommended** |
| B | pre-hook `CSimpleModelOutputPose::vf_03` (phase 7) | vtable slot `0x38de568` -> `0x237ce40` (`AN_OUT_EVAL`) | input `*(*(out+0x50)+0x40)` after physics and cloth | model | game thread, last moment before skinning and attachments | filter `*(out+0x340) == playerSm` | fallback: immune to physics/cloth; but `GetNodeTransform`, the cloth and the physics node keep the animated arm |
| C | own **node-callback type** (section 3.1) | API only: `IEngineModel` vtbl+0x128 once, vtbl+0x130 per joint (`AN_IEM_CBTYPE`, `AN_IEM_ADDCB` check the stubs) | `ctx` + our 64-byte snapshot | model | worker thread; targets frozen at phase 1 (before the player update, so the body's movement in that update is not known) | per model by construction | engine-native alternative; no code patch at all; best if IK is wanted on orcs too (grabs) |
| D | post-hook op 3's `call FUN_00947f60` | `0x9486e0` (`AN_OP3_CB`), also `0x9486b3` (`AN_OP3_NOCB`) | `rdi` = job step, `rsi` = model pose being produced | model | worker thread, any character's job | must identify the job (step `+0x4b0` object xf / `+0x28` skeleton) | not recommended (C does the same cleanly) |
| E | reuse `CAnimationMgr::IK` (types 0xa-0xc) | its owner object (+0x74.. targets, +0x108 weight) | - | model | worker thread, direct mode | only while the state machine has the modifier | no: data-driven, appears and disappears with animation states |
| F | skinning / render-node copy | `AN_RENDER_LISTENER` slot `0x38de488` -> `0x237fde0` | skin transforms in mesh-bone order | model x inverse bind | game thread, phase 7 | filter by renderable | no: attachments are already placed; the hand would move without the sword |

Why A **[high for the facts, med for the judgement]**:
1. It runs on the game thread with no job alive and after the player update, so the controller poses, the anchor and
   Talion's object transform are those of the frame being rendered (F1 of FP has also run; RT check 4 for a late
   move by the physics step).
2. Everything downstream follows without extra work: the phase-2 connections copy the pose to the physics node
   (whose keyframed bodies are driven from it in phase 4, section 7), the cloth (cape collision), the output pose
   (skinning) and the attach-node listener (Urfael and Acharn follow the hand, section 4). `GetNodeTransform` returns
   the overridden arm, so the game's own hand reads (combat colliders, targeting `LeftHand`, FX) agree with what is
   drawn.
3. It overrides whatever the job did to the arms (animation, `ProceduralBody::Arm`, `CAnimationMgr::IK`).
4. The job's double-buffer swap means the override never feeds back into the next frame (section 1.3).
5. One vtable slot; the hook is a few lines; the IK itself is plain math in our module.

Costs of A: (i) the pose is model space with **absolute** child transforms, so after changing a joint we must move
its subtree ourselves; (ii) all characters' trackers go through the slot, so the filter must be cheap (one pointer
compare); (iii) during cinematics the cinematic node (phase 4) replaces the pose anyway; in the game's pause the
tracker is not evaluated and the arm keeps its last pose **[med]**.

### 3.3 The two-bone arm IK in A (recipe)

Per arm, once per skeleton: `S` = upper arm (`R_Armu`/`L_Armu`, GP 2), `E` = its child on the hand path, `W` = hand
(`R_Hand`/`L_Hand`) - indices from section 2.4; subtree lists `sub(S)`, `sub(E)`, `sub(W)` = all indices > joint whose
parent chain reaches the joint (parents precede children, section 2.2). Bone lengths `a = |pos_E - pos_S|`,
`b = |pos_W - pos_E|` from the bind pose (`entry+0x24`).

Per frame in the post-hook (all in model space, `X = objectXf` from the HOBJECT `trk+0x168`: pos `+0x8`, quat `+0x14`,
FP 2.1):
1. Target `T = X^-1 x (anchor x controller x gripOffset)`; clamp `|T.pos - pos_S|` to `[|a-b|+eps, (a+b)(1-eps)]`.
2. Elbow: from `pos_S`, `T.pos`, `a`, `b` and a pole (the animated elbow direction, blended toward "down and back"
   in body space) the law of cosines gives the new elbow point `e'`.
3. `dS` = the rotation taking `(pos_E - pos_S)` to `(e' - pos_S)` (plus a twist so the elbow plane matches the pole);
   apply about `pos_S` to `S` and to every node of `sub(S)`: `pos' = pos_S + R(dS)(pos - pos_S)`, `q' = dS (x) q`.
4. `dE` = rotation taking the new `(pos_W - pos_E)` to `(T.pos - pos_E)`; apply about `pos_E` to `E` and `sub(E)`.
5. `dW = q_T (x) q_W^-1`; apply about `pos_W` to `W` and `sub(W)` (fingers, weapon sockets if they are nodes).
6. Twist bones (forearm/upper-arm twist children, if the skeleton has them) can take a share of the hand roll;
   otherwise skip.
Write `pad = 0`, keep quaternions normalised. Cost: two arms x ~40 subtree nodes, negligible.

For C instead: callbacks on `S` and `E` (and `W`) with the same math; the subtree is moved by the engine (steps 3-5
only touch `ctx->node`), the target comes from the 64-byte snapshot (store it in **model space**, computed on the
game thread at phase 1 from the anchor and the object transform of that moment).

## 4. (d) How the weapons are bound to the hand

| Question | Answer | Conf. |
|---|---|---|
| What a weapon is | a separate client object `CClientInventoryItemModel` (created by `objsys` vtbl+0x78 "CClientInventoryItemModel" in `FUN_02320b00`/`FUN_0232e360`) with its own `CSimpleModel` (`CSimpleModel::vf_14` from `FUN_02affd20`) and render node | high |
| How it is attached | the parent model's **`CSimpleModel::vf_42` (vtbl+0x150) = AttachChild(hChild, nodeOrSocketHash, const xf* offset)** (`0x2357cb0`), called by `FUN_02affd20` for the item object and its extra objects (`+0x218`, `+0x238`); offset = an `LTRigidTransform` `{pos[3]; quat[4]}` with pos 0 **[med]** and quat = `item+0x240` when set, else identity. `vf_42` creates the model's `CSimpleModelAttachNode` at `sm+0x90` on first use, registers it as an output-pose listener (`FUN_0235c680`) and adds/updates an entry with `FUN_0235bde0` (`AN_ATTACH_ADD`) | high |
| Which node | data: the item's slot-model record (`*0x4c34760`, CB-style schema) fields **`ActiveNode`** (drawn) and **`InactiveNode`** (sheathed/holstered), plus `Models`, `ActiveAnimation`/`InactiveAnimation`; `FUN_02320b00` reads them and passes the hash. So Urfael's grip node, Acharn's and Azkar's are named in the game database, not in the code. Azkar (the bow) is shown only while aiming; whether it is an item model on this path or an FX was not traced **[low]** | high (mechanism), RT (names) |
| Entry layout (0x68 bytes, vector `attach+0x48..0x50`) | `+0x18` child HOBJECT (WBObjRef), `+0x20` resolved node index, `+0x24` node/socket hash, `+0x28` **effective offset** `{pos[3]; quat[4]}` (7 floats, to `+0x43`) = socket offset (x) user offset (`FUN_0235ba30` re-resolves through `FUN_0235b880`), `+0x44` user offset (7 floats, to `+0x5f`), `+0x60` flag, `+0x61` resolved | high |
| Per-frame update | `CSimpleModelAttachNode::vf_02` (`0x235a660`, `AN_ATTACH_LISTENER`), called by the output pose in **phase 7** with the model pose: `child = header /*object world xf incl. scale*/ x pose[node] x offset`, normalised, then `objsys vtbl+0xb8(hChild, &xf)` and `objsys vtbl+0xe8(hChild) -> vtbl+0x118 -> vtbl+0x50(&xf)` (the child's own render object) | high |
| Does the weapon follow an overridden hand? | **yes**, if the override reaches the output pose's input (candidates A, B, C of section 3.2); not with F | high |
| Offset a weapon (grip correction for VR) | write the entry's user offset `+0x44..+0x5c` and call `FUN_0235ba30(attachNode, entry)` (recomposes `+0x28..+0x40` and re-resolves), or write `+0x28..+0x40` directly between frames (game thread, not during phase 7) | high (fields), med (side effects) |
| Detach / free-float a weapon | simplest: leave it attached and drive the hand (A); to put a weapon exactly on the controller without the hand, overwrite the child's transform **after phase 7** (`0x9eb0ae`) and before the render with the same two `objsys` calls the listener uses. A real detach goes through the attach node's remove path (`vf_42`'s neighbours; not traced) and would fight the game's own re-attach on draw/sheathe | med |

RT: dump the player's attach entries (`*(playerSm+0x90)` -> `+0x48..0x50`) with sword drawn and sheathed: child
object, node index, hash -> name via section 2.4. That names Urfael's grip node and gives the hand-to-grip offset the
VR hand pose needs.

## 5. (e) The head and the eyes

- No head bone name exists in the binary (FP 2.2). The head node is found at runtime: (1) the parent node of the
  socket **`SocketHead`** (`0x7b92d623`; socket entry `+0x04`, section 2.2), or (2) the name list of section 2.4, or
  (3) the LeanAndTwist "Head" field, whose default is `Neck` (`0x87ec5687`) - so the procedural body treats `Neck` as
  its head joint **[high for the paths, RT for the result]**.
- `SocketHead` is used as the default socket by the Nemesis/character-preview UI (`FUN_00dc2720`, markers over heads),
  so it most likely sits on top of the head, not at the eyes **[med]**. Eye position = head node (or `SocketHead`)
  transform x a fixed local offset calibrated once (FP 2.2's plan).
- `GetNodeTransform(m, hChar, 0x7b92d623, &xf, 1)` resolves sockets itself (node x socket offset x object) **[high]**.
- Freshness: read it **after phase 2** (exact for this frame; e.g. in candidate A's hook right after the IK, or at
  H1) - during the player update it is one frame old (section 1.2).
- With candidate A the head is untouched by the arm IK (different subtree), so the camera can use the head node
  after our override in the same hook.

## 6. (f) Head, hood and hair pieces

- Statically there are no piece names for Talion in the binary: no `Hood`, `Cowl`, `Hair` model/piece strings beyond
  the `SMC_*` part keys (`SMC_HeadModel`, `SMC_HairModel`, `SMC_HelmetModel`, `SMC_HeadScarModel`, ...) and the
  `Constructing SimpleModel` log of `FUN_0236b820` (FP 3.1) **[high]**. The piece table lives in the model data
  (`*(sm+0x28)+0x38`, count `+0x40`, FP 2.2/3.1); its names come from the resource. This helper found nothing to add
  to FP 3; the names stay **RT** (FP 7 check 3).
- The `SMC_*` parts suggest the head, hair and helmet are separate parts of the assembled model, i.e. separate pieces
  **[med]**; the hood is probably `SMC_HairModel` with `HairOverrideIsHelmet` or a helmet part (FP 3.1) **[low]**.

## 7. (g) Physics on the bones

| Item | Finding | Conf. |
|---|---|---|
| Where | `CSimpleModelPhysicsNode` (phase 5, pre-step work in its phase-4 `vf_06` `0x2378ab0`), created only when the model is built with physics (`CSimpleModel::vf_14`'s last flag -> `FUN_02357190`; MultiNode flag 7), kept at `sm+0x180` | high |
| Keyframed bodies | in phase 4 it walks its rigid bodies (`node+0x190`, count `+0x198`, per-body flags `+0x1c0`) and, for bodies flagged keyframed, feeds them the **current pose** (`vtbl+0x48`, `+0x130`, `+0x140`) before the physics world steps (between phases 4 and 5, section 1.2) | med |
| Ragdoll | when its ragdoll is active (`+0x118`, flag `+0x15d`) the node writes the simulated bones into the pose (phase 5) and `GetNodeTransform` returns them (`FUN_02379c50`, mapped range `+0x1f8..+0x1fc`, map `+0x1d0`); `hkaRagdollInstance`, `ConstraintRagdoll` (GP 5) | high (code path), med (when active) |
| Consequence for the arm IK | with candidate A the override is upstream of phase 4, so keyframed bodies on the arms move with it automatically; nothing has to be moved by hand. If a hit reaction or death activates the ragdoll, its result (phase 5) wins over our arm - which is the right behaviour for death; for living hit reactions **RT** (does the player ever blend ragdoll while alive? `Combat.RagdollReaction`) | med |
| Cloth (cape) | phase 6 reads the pose after our override: cape collision follows the IK'd arms | med |
| Character proxy | the player's movement capsule (`CPhysicsCharacter`) is not bone-driven | med |

## 8. (h) Hook plan

### 8.1 Design (milestone M3, DV G11)

```
frame N, CGameClientShell::vf_02 (game thread)
  phase 0/1                     job built and queued (C would snapshot its targets here)
  player update (0x9ebcae)      FUN_00a40fe0: F1 camera (FP 6) - uses last frame's head; G-side combat, input
  drain-and-wait (0x9eae7f)
  phase 2: trk->vf_03           [AN_TRK_EVAL post-hook, player's tracker only]
                                  1. read controller poses for this frame (OpenXR, predicted display time)
                                  2. two-bone IK both arms in model space (3.3), subtree update
                                  3. read the head node / SocketHead for the eyes (exact for this frame)
            connections -> physics (keyframed bodies), cloth, output pose
  phase 7: out->vf_03           skinning; attach listener places Urfael/Acharn on the IK'd hand
  BeginFrame (H5), world render (H1 per eye)
```

Which player update runs does not matter for the IK itself: `AN_TRK_EVAL` is a vtable slot of the tracker (phase 2),
not a call inside the player update, so it fires every frame either way. Hooks that do sit inside the player update
(FP's F1 camera, F6 head read) must use the sites in `FUN_00a40fe0` (`FP_CAMPOST_B`, `FP_ANIM_B`; 1.2 correction)
**[high]**.

Setup once per model (re)build (PlayerBody `FUN_00a52a50`, armour/head swaps, FP 3.1): resolve `S/E/W` indices
(section 2.4), subtree lists, bind lengths; cache `playerTrk = *(playerSm+0x178)`. Invalidate when `*(playerSm+0x178)`
or the skeleton pointer changes.

Gating: only in first-person VR states (FP 5: not cinematic, not photo mode, not the hierarchy menu, alive); ease the
IK weight in/out over 0.1-0.2 s on state changes (paired animations: DV 1 keeps the player's hands theirs - weight 1;
death and stage view: weight 0).

### 8.2 Sites

| # | Name | Kind | Site | Use | Conf. |
|---|---|---|---|---|---|
| 1 | `AN_TRK_EVAL` | vtable slot | `CSimpleModelTrackerNode` vtable `0x38df380` slot 3 (`0x38df398`) -> `0x23899b0` | **the G11 write point** (candidate A): post-hook, filter `this == playerTrk`, edit `*(*(this+0x58)+0x40)` | high (site), RT (visual) |
| 2 | `AN_OUT_EVAL` | vtable slot | `CSimpleModelOutputPose` vtable `0x38de550` slot 3 (`0x38de568`) -> `0x237ce40` | candidate B (pre-hook, edit `*(*(this+0x50)+0x40)`, filter `*(this+0x340) == playerSm`) | high |
| 3 | `AN_MGR_PHASE` | vtable slot | `CSimpleModelMgr` vtable `0x3497430` slot 3 (`0x3497448`) -> `0x94d110` `(mmgr, phase)` | tracing: one hook sees every phase boundary (e.g. log the head node after phase 2 vs during the player update; measure where the time goes) | high |
| 4 | `AN_PLAYERUPDATE` (+ `_A`) | call | `0x9ebcae` -> `FUN_00ab3f80` (-> `FUN_00a40fe0`, the live update); `_A` = `0x9eb687` -> `FUN_00ab3ec0` (-> `FUN_00a416a0`, only with the app deactivated, FP 1.3) | tracing: proves that the player update runs between job wake and wait; `_A` only to see whether A ever runs | high (run 3: B every frame, A 0) |
| 5 | `AN_JOBWAIT` | call | `0x93582b` (in `FUN_00935820`) -> `FUN_00942fe0` | tracing: how long the game thread waits for the animation jobs (VR frame budget) | high |
| 6 | `AN_JOBSUBMIT` | call | `0x238fa89` (in `CSimpleModelTrackerNode::vf_06`, phase 1) -> `FUN_02389530(trk, 1)` | tracing / candidate C's snapshot moment | high |
| 7 | `AN_ATTACH_ADD` | call | `0x2357cdb` (end of `CSimpleModel::vf_42`) -> `FUN_0235bde0(attach, hChild, hash, &offset)` | log weapon attachments: node hash and offset per draw/sheathe (section 4 RT) | high |
| 8 | `AN_ATTACH_LISTENER` | vtable slot | `CSimpleModelAttachNode` vtable `0x38d8948` slot 2 (`0x38d8958`) -> `0x235a660` | optional: post-hook to override a weapon's final transform (section 4, free-float) | high |
| 9 | `AN_RENDER_LISTENER` | vtable slot | `CSimpleModelRenderable` vtable `0x38de478` slot 2 (`0x38de488`) -> `0x237fde0` | no hook planned; check data for the skinning copy | high |
| 10 | `AN_JOBSWAP` | vtable slot | `CEngineAnimationImpl` vtable slot 55 (`0x348b0b0`) -> `0x933160` | check data: the job/tracker pose swap | high |
| 11 | `AN_OP3_CB` | call | `0x9486e0` (op 3) -> `FUN_00947f60` | candidate D only (worker thread); check data | high |
| 12 | `AN_OP3_NOCB` | call | `0x9486b3` (op 3) -> `FUN_00947d60` | as 11, the no-callback path | high |
| 13 | `AN_IEM_CBTYPE` | vtable slot | `IEngineModel` vtable `0x3484130` slot 37 (`0x3484258`) -> `0x91a980` | candidate C: register a snapshot-mode callback type (call, no hook) | high |
| 14 | `AN_IEM_ADDCB` | vtable slot | `IEngineModel` slot 38 (`0x3484260`) -> `0x91ad70` (slot 39 `0x91adf0` removes) | candidate C: add/remove per-node callbacks (call, no hook) | high |
| 15 | `AN_ATTACHCHILD` | vtable slot | `CSimpleModel` vtable `0x38d82c8` slot 42 (`0x38d8418`) -> `0x2357cb0` | check data for the attach call | high |

Each hooked function is referenced only by its one vtable slot (no direct `call`/`jmp` and no other pointer in the
binary), so a slot patch sees every call **[high]**.

Check data in the `SITE(...)` format of `mod/spike1/check_sites.py` (generated with `work-firstperson/sitegen.py`
from the binary; all verified by `scripts/check_doc_sites.py` against build-id `87ec5d6f...`):

```c
SITE("AN_TRK_EVAL", K_VSLOT, 0x38df398, 0x38df380, "80 79 38 02 00 00 00 00 30 82 38 02 00 00 00 00 30 9b 35 02 00 00 00 00 b0 99 38 02 00 00 00 00 f0 e8 38 02 00 00 00 00", 0x23899b0, "48 8b 97 08 08 00 00 c6 87 48 01 00 00 00 48"),
SITE("AN_OUT_EVAL", K_VSLOT, 0x38de568, 0x38de550, "10 f3 37 02 00 00 00 00 70 f7 37 02 00 00 00 00 d0 9f 6f 00 00 00 00 00 40 ce 37 02 00 00 00 00 50 9a 6f 00 00 00 00 00", 0x237ce40, "48 83 bf 88 03 00 00 00 0f 84 f2 02 00 00 41"),
SITE("AN_MGR_PHASE", K_VSLOT, 0x3497448, 0x3497430, "f0 97 94 00 00 00 00 00 00 9b 94 00 00 00 00 00 e0 ad 94 00 00 00 00 00 10 d1 94 00 00 00 00 00 80 92 94 00 00 00 00 00", 0x94d110, "41 57 41 89 f7 41 56 41 55 41 54 49 89 fc 55"),
SITE("AN_PLAYERUPDATE", K_CALL, 0x9ebcae, 0x9ebca6, "e9 50 fb ff ff 4c 89 e7 e8 cd 82 0c 00 e9 b4 f1 ff ff", 0xab3f80, "55 48 89 f8 48 8d 6f 28 53 48 8d 5f 08 48 83"),
SITE("AN_PLAYERUPDATE_A", K_CALL, 0x9eb687, 0x9eb67e, "0f 85 e8 f7 ff ff 4c 89 e7 e8 34 88 0c 00 e9 db f7 ff ff", 0xab3ec0, "55 48 89 f8 48 8d 6f 28 53 48 8d 5f 08 48 83"),
SITE("AN_JOBWAIT", K_CALL, 0x93582b, 0x935820, "53 48 89 fb 48 8d bf 18 c0 00 00 e8 b0 d7 00 00", 0x942fe0, "41 55 41 54 55 48 89 fd 53 48 8d 9f 9c 80 00"),
SITE("AN_JOBSUBMIT", K_CALL, 0x238fa89, 0x238fa7b, "80 bf 48 01 00 00 00 75 11 be 01 00 00 00 e8 a2 9a ff ff", 0x2389530, "41 57 41 56 41 55 41 89 f5 41 54 55 53 48 89"),
SITE("AN_ATTACH_ADD", K_CALL, 0x2357cdb, 0x2357cd2, "4c 89 e9 44 89 e2 48 89 ee e8 00 41 00 00", 0x235bde0, "41 57 41 56 41 55 41 54 55 53 48 81 ec a8 00"),
SITE("AN_ATTACH_LISTENER", K_VSLOT, 0x38d8958, 0x38d8948, "20 a2 35 02 00 00 00 00 10 a5 35 02 00 00 00 00 60 a6 35 02 00 00 00 00", 0x235a660, "48 83 bf 80 00 00 00 00 0f 84 9a 06 00 00 41"),
SITE("AN_RENDER_LISTENER", K_VSLOT, 0x38de488, 0x38de478, "b0 f7 37 02 00 00 00 00 b0 fc 37 02 00 00 00 00 e0 fd 37 02 00 00 00 00", 0x237fde0, "41 57 41 56 41 55 41 54 55 53 48 81 ec b8 00"),
SITE("AN_JOBSWAP", K_VSLOT, 0x348b0b0, 0x348b0a0, "20 41 93 00 00 00 00 00 50 31 93 00 00 00 00 00 60 31 93 00 00 00 00 00", 0x933160, "48 8b 0e 48 8b 41 10 48 89 51 10 c3 0f 1f 40"),
SITE("AN_OP3_CB", K_CALL, 0x9486e0, 0x9486db, "ff e0 0f 1f 00 e8 7b f8 ff ff", 0x947f60, "41 57 4c 8d 5f 40 41 56 41 55 41 54 55 53 48"),
SITE("AN_OP3_NOCB", K_CALL, 0x9486b3, 0x9486a0, "8b 83 80 02 00 00 4c 89 ea 4c 89 f6 48 89 df 85 c0 75 2d e8 a8 f6 ff ff", 0x947d60, "8b 47 10 4c 8b 4f 20 53 4c 8b 5f 18 0f b6 5f"),
SITE("AN_IEM_CBTYPE", K_VSLOT, 0x3484258, 0x3484250, "60 a9 91 00 00 00 00 00 80 a9 91 00 00 00 00 00 70 ad 91 00 00 00 00 00", 0x91a980, "48 83 ec 08 89 f7 48 89 d6 48 89 ca 4c 89 c1"),
SITE("AN_IEM_ADDCB", K_VSLOT, 0x3484260, 0x3484260, "70 ad 91 00 00 00 00 00 f0 ad 91 00 00 00 00 00", 0x91ad70, "48 85 f6 74 6b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("AN_ATTACHCHILD", K_VSLOT, 0x38d8418, 0x38d8418, "b0 7c 35 02 00 00 00 00", 0x2357cb0, "41 55 49 89 cd 41 54 41 89 d4 55 48 89 f5 53"),
```

Functions and data the mod reads or calls (check their first bytes too): `CSimpleModel::vf_62` GetNode inner
`0x23530c0`, `FUN_008f3c20` (object -> model), `FUN_0235ba30` (re-resolve an attach entry), the IEngineModel stubs of
FP 6.2; data: `0x46a7e60` (32 callback types, check the chosen type's 0x28 bytes are zero before registering),
`playerSm+0x178` (tracker), `+0x90` (attach node), `+0x180` (physics node), `*(sm+0x28)+0x10` (skeleton).

### 8.3 Thread safety

| Candidate | Risk | Mitigation |
|---|---|---|
| A, B | none from the animation jobs (all finished at `0x9eae7f`); the render thread reads only the render node copy made in phase 7 | keep the hook short; no allocation |
| C | exec runs on 3 worker threads in parallel (one job per model, so one model's callbacks run on one thread); snapshot runs on the game thread at phase 1 | snapshot mode only; the exec reads only `slot64` and `ctx`; never touch game objects from the exec |
| D | worker thread, all characters | not used |

## 9. Runtime checks and open questions

Runtime checks, in the order they unblock M3 (all read-only logging except 3):
1. **Skeleton dump** of the player's model (section 2.4): index, parent, `names[i]`, `hashes[i]`, check
   `somhash(name) == hash`; the `SocketHead` socket's parent index and offset. Gives the arm chains, twist/finger
   children and the head bone in one log.
2. **Frame order** (cheap): timestamps from `AN_MGR_PHASE` (every phase), `AN_PLAYERUPDATE`, `AN_JOBWAIT` (wait time)
   and the thread id inside one exec of an existing callback type, for 100 frames. Expected: player update between
   phase 1 and the wait; jobs on `EngineAnimationJobThread1..3`.
3. **Write-point proof** (flat, no VR): in the `AN_TRK_EVAL` post-hook rotate the player's `R_Hand` subtree by 30 deg
   about the forearm axis every frame. Pass = the hand and Urfael turn on screen, `GetNodeTransform(R_Hand)` reports
   the turned hand in the next player update, no flicker (flicker = something downstream overwrites: cinematic node,
   ragdoll). Then the same with the full two-bone IK to a fixed target in front of Talion.
4. **Object motion after phase 2**: log Talion's object transform (`*(body+0x1b8)` +0x8/+0x14) in `AN_TRK_EVAL` and in
   H1 while running and sprinting. If it differs (the physics step between phases 4 and 5 moves the character), express
   the IK target relative to the object transform of H1 (predict it with this frame's velocity) or move the IK to
   candidate B.
5. **Callback type table** `0x46a7e60` (32 x 0x28 bytes) after the first world load: which types are live
   (0x13/0x14?), which are free for candidate C.
6. **Arm-moving systems on Talion**: log `AddNodeCallback` (`IEngineModel` vtbl+0x130) calls for the player's model with
   types 0xa-0xc (`CAnimationMgr::IK`) and 0x11/0x12 (`ProceduralBody::Arm`), and when they are removed (climbing,
   ledges, interactions?). Our override wins anyway (section 3.2), but they show which animations rely on the game's
   own hand placement (climbing grips) - candidates for blending our IK out.
7. **Weapon attachments**: dump `*(playerSm+0x90)` entries with the sword drawn and sheathed, the dagger in a stealth
   kill, the bow aimed (section 4).
8. **Physics node**: `*(playerSm+0x180)` present? ragdoll flag `+0x15d` ever set while alive (hit reactions, knockdowns)?
9. **Eye offset**: head node / `SocketHead` vs the gameplay camera in a first-person pose (FP 7 check 2), now read
   after phase 2.

Open static questions (not needed for a first M3 build):
- Where types 0x13/0x14 (camera-shake nodes of PlayerBody) are registered; if never, those entries would call a null
  exec in the job, so they must be registered somewhere not found by the byte scans.
- What `*(engine+0xba8)` (`FUN_0094c950`) updates between phases 2-3 and 3-4.
- The connection list per model (which node feeds which) and whether the player's output pose takes the cloth or the
  physics node output; static flow is clear, the exact list is data built at model creation.
- Whether the skeleton's name strings stay resident (section 2.2 `+0x38`).
- The render-thread side of the skeletal transforms (`CSkeletalTransforms` -> constant buffer) - only needed if a
  render-side override is ever wanted.
- The attachment remove path, and how the bow (Azkar) is shown.
