# Shadow of Mordor (Feral Linux build): Talion's arms on the VR controllers (arm IK)

Static analysis only (2026-10-20): read-only Ghidra batches on `som-linux-q2` (`scripts/ghidra_query.sh`, batches
`b1..b3.txt`, answers `q1..q3.txt` in the git-ignored scratch `$SOM/reference/work-arms/`), the earlier answers
`reference/work-anim/q5.txt`, `objdump` of small ranges of `reference/bin/linux/ShadowOfMordor` (BuildID `87ec5d6f...`),
the TSV indexes, `reference/work-anim/riprefs.py` and run 3's skeleton dump `logs/boot-check-20261019-190441/probe-out/
nodes-2.tsv`. Nothing was launched. `SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`.

Builds on `docs/re-animation.md` (**AN**: the pose pipeline, phases, pose format, node callbacks, write point
`AN_TRK_EVAL`, weapon attachments), `docs/re-xr-input.md` (**XI**: grip -> world -> model space, `C_h`, 4.3),
`docs/re-firstperson.md` (**FP**), `docs/re-gameplay.md` (**GP**), `docs/re-resolution.md` (**RS**: pieces, shadows) and
`docs/design-vr.md` (**DV** section 1, G11). It does not repeat what they establish. Conventions as there: runtime
addresses (non-PIE), `vf_NN` = vtable slot NN, `vtbl+0x130` = byte offset into a vtable, `+0x58` = byte offset into an
object, cm, +Y up, quaternions (x, y, z, w) Hamilton. Confidence: **[high]** read in the code, unambiguous; **[med]**
read in the code, meaning inferred; **[low]** inference; **RT** = needs a runtime check.

Short names as in AN: `m = *(IEngineModel**)0x46a9230`; `sm` = a character's `CSimpleModel*`; `trk = *(sm+0x178)`;
`mgr` = the player's `CAnimationMgr` (the object `FUN_020a4ac0` updates in the player update; `*(mgr+0x560)` = Talion's
HOBJECT, runtime-probes A3); `chain` = one IK chain slot of `mgr` (1.2).

**Status (2026-10-20):** built offline as `SOMVR_ARMS=1` in `mod/somvr` (`somvr_arms.c/h`, the hook `ARM_TRK_EVAL`,
re-xr-pipeline.md 6.6): 2.2's write point and 2.3's solver with the weapon-node target and 4's bone blade. Nothing
launched; section 8 lists what was built differently and what the next boot must show.

## 0. Summary

| Question | Answer | Conf. |
|---|---|---|
| Who writes `IK_LHand`/`IK_RHand` (nodes 4/5) | only the animation job: they are ordinary skeleton nodes (parent 0) sampled from the clips like any bone. No code names them: no string, no hash immediate (`0xbce53fff`, `0x2ee02ac5`) anywhere in the binary | high |
| Who reads them | data-named node consumers only. The engine's arm IK, `Modifier.IK` (`CAnimationMgr::IK`), reads a node or socket **on Talion himself** named by its record field `TargetSocket`, every frame, in world space (`GetNodeTransform` at `0x21857b8`); `IK_RHand`/`IK_LHand` are the natural values (animation-authored targets). Which records name them is data | high (mechanism), RT (names) |
| The solver | Firebird's own, not Havok's (no `hkaTwoJointsIkSolver`; the `hkb*IkModifier` strings are reflection data only): three direct node callbacks per chain inside the animation job: type 0xa `FUN_021e5270` on `RootNode` (two-bone law of cosines), 0xb `FUN_021e4900` on `JointNode` (aim the forearm), 0xc `FUN_021e2b70` on `TargetNode` (hand rotation) | high |
| When | target and weight on the game thread in the player update (`FUN_020a4ac0+0x714` -> `FUN_02185590`, last frame's pose); solve on `EngineAnimationJobThread1..3` in op 3, before phase 2. Our G11 post-hook (phase 2) runs after both, so it overrides them | high |
| Inputs | target pos/quat in world space (`chain+0x74`/`+0x80`), weight `chain+0x108` (blend state `+0x10c`, `BlendIn`/`BlendOut`), pole = the root joint's own rotation with `JointTargetDist` (`+0xa8`, default 100) and `DownJointSign` (`+0xac`, default 1), bone lengths `+0xa0`/`+0xa4` | high (fields), med (pole geometry) |
| Cheapest way to drive both arms | **(b) + (c)**: post-hook `CSimpleModelTrackerNode::vf_03` (`ARM_TRK_EVAL` = AN's `AN_TRK_EVAL`), player's tracker only; our own analytic two-bone IK on `R_Armu 108 -> R_Arml 110 -> R_Hand 112` and `L_Armu 86 -> L_Arml 88 -> L_Hand 90` in the model-space pose `*(*(trk+0x58)+0x40)`, subtrees moved by hand. (a) (feeding the engine's chain) works but costs a frame, a race and the elbow control | high (mechanics), RT (visual) |
| Pose format | model space, absolute, 0x20 bytes per node `{pos[3], pad = 0, quat x,y,z,w}`, no scale; parents have smaller indices | high |
| Shadow, weapons, physics | all follow: one skeletal render node per model feeds every pass; the sword is placed every phase 7 from the edited pose; keyframed bodies and cloth read it after phase 2; a ragdoll (death) wins over it | high / med |
| Server, network | single process, one `IEngineModel`, no pose replication; the game's own melee hits come from precomputed animation sweeps, not the live hand | med |
| The sword | follows `R_Weapon` (or its socket) every frame; the blade segment should come from the edited bone through `C_h⁻¹` (identical to the controller blade whenever the IK reaches), velocity from the controller | high (follow), med (recipe) |

## 1. Who reads and writes `IK_LHand` / `IK_RHand`

### 1.1 The nodes

- Run 3 (`nodes-2.tsv`): 4 `IK_LHand` `0xbce53fff`, 5 `IK_RHand` `0x2ee02ac5`, 6/7 `IK_LFoot`/`IK_RFoot`, all children of
  node 0 `Null` and exactly at the root with the root's rotation while Talion stands **[high, one sample]**.
- The binary holds none of the four names and none of the four hashes (byte search of the whole file; the hashes of
  `R_Hand`, `L_Hand`, `R_Armu`, `R_Arml`, `R_Weapon`, `L_Weapon`, `attach` are absent too) **[high]**. The only arm-bone
  strings in the binary are `L_Armu`/`L_Hand`/`R_Armu`/`R_Hand` in `FUN_020edb30`, the "RagdollStretching" debug check
  (`FUN_020eb270`: distance of upper arm to hand > a limit), called from `FUN_026a3e20` **[high]**: not an IK.
- So nothing in the code writes them by name. They get their transform like every node: the job samples the clips
  (op 0), blends, converts to model space (op 3, AN 1.3) **[high]**. "At the root" = the playing clips carry no motion
  on them (or identity) **[med]**; RT: the `ikwatch` word (6.2) logs them over idle, run, swing, ledge, climb.
- Readers can only find them through names in data: the IK records below (`TargetSocket`, `RootNode`/`JointNode`/
  `TargetNode`), the procedural-body records, the attach records (`ActiveNode`) and the socket defaults **[high]**.

### 1.2 The engine's arm IK: `Modifier.IK` (`CAnimationMgr::IK`)

| Item | Finding | Conf. |
|---|---|---|
| Class | `CAnimationMgr::IK` (typeinfo `0x38a3558`, vtable `0x38a4888`, base `CAnimationMgr::Modifier`), created by the animation state machine as `"Modifier.IK"` (factory slot set by `FUN_0208ad10` -> `FUN_020ac2e0`; the name list in `FUN_02089080`). It exists only while an animation state's modifier list holds it | high |
| Record schema | `FUN_025806f0` (from `_INIT_1316`): `TargetSocket` (string), `BlendIn`, `BlendOut` (floats), then a group `BeginIKChain` ... `EndIKChain` with `RootNode`, `JointNode`, `TargetNode` (strings), `DownJointSign`, `JointTargetDist` (floats). The same chain fields form the DB category `Character/Models/IKChain` (`_INIT_1317`, schema `*0x4c2aa60`, read by `FUN_02185900`) | high |
| Chain slots | 4 per `CAnimationMgr`: `mgr+0x34d8`, `+0x35f0`, `+0x3708`, `+0x3820` (0x118 bytes, in-use byte `+0x110`). `IK::vf_15` (`0x21b68f0`) takes the first free one into `ik+0x78`; none free = no IK; the destructor `IK::vf_00` frees it | high |
| Start | `IK::vf_07` (`0x21b8570`, the modifier's per-frame slot [med]; starts when the state time enters `[ik+0x60, ik+0x64]`) or `IK::vf_12` (`0x21b8b00`): target object = **Talion himself** (`*(mgr+0x560)` into `chain+0x40` and `+0x68`); `chain+0x70` = `FUN_02094f00(mgr, TargetSocket, mirrored)` = `FUN_020bc4a0`: `GetNode(m, *(mgr+0x560), name)` and, when the state plays mirrored (`state vtbl+0x128`), IEngineModel slot 20 -> `CSimpleModel::vf_61` -> `FUN_0238c060` (the tracker's node remap `*(trk+0x808)+0x28`, i.e. left <-> right); then `FUN_02185900` (setup) and `FUN_02185380(BlendIn, chain)` (start) | high (code), med (remap = mirror) |
| Setup `FUN_02185900(chain, hObj, record)` | `+0x18` = hObj, `+0x20` = record; `RootNode`/`JointNode`/`TargetNode` -> GetNode hashes `+0x90`/`+0x94`/`+0x98`; `JointTargetDist` -> `+0xa8` (default 100), `DownJointSign` -> `+0xac` (default 1); lengths `+0xa0 = |J - R|`, `+0xa4 = |T - J|` from IEngineModel slot 35 (`vtbl+0x118` -> `CSimpleModel::vf_80` -> `FUN_0238dc60`, most likely the reference pose) | high (fields), med (slot 35) |
| Start `FUN_02185380` | once per chain: `AddNodeCallback` (IEngineModel `vtbl+0x130`) type **0xa on `+0x90`**, **0xb on `+0x94`**, **0xc on `+0x98`**, owner = the chain (0x110 bytes, direct mode: the job reads the live chain); state `+0x10c = 1`, blend time `+0xe0` | high |
| Per frame `FUN_02185590(chain)` | called for the 4 slots by `FUN_020a4ac0+0x714` (`0x20a51d4`, loop `0x20a51c3..0x20a51f0`), i.e. in the player update. Blend: state `+0x10c` 0 off / 1 in / 2 on / 3 out, weight `+0x108` (sine ease `FUN_02185220`); blends out when the animation on the watched track changed (`FUN_00c73790`, ids from IEngineModel slot 64 -> `CSimpleModel::vf_109`); then **`GetNodeTransform(m, *(chain+0x68), *(chain+0x70), &xf, 1)` at `0x21857b8` -> `+0x74` pos, `+0x80` quat (world)**; clears the solved flag `+0x9d` | high |
| Stop | `FUN_02185480` (from `IK::vf_07+0x2fb`, `IK::vf_14`): state 3 with `BlendOut`; at weight 0 `FUN_021852d0` removes the three callbacks (IEngineModel `vtbl+0x138`) | high |

The solve, inside the job (worker thread, op 3 `FUN_00947f60`, nodes in index order, AN 3.1):

| Callback | Node (player) | Does | Conf. |
|---|---|---|---|
| 0xa `FUN_021e5270` | `RootNode` (an arm: `R_Armu` 108) | returns if weight 0. World target -> model space with the job's object transform (`ctx+0x00`); "current elbow" and pole point from the root joint's rotation, its length and `JointTargetDist` along a local axis signed by `DownJointSign`; law of cosines (`acosf`, cos clamped) for the shoulder angle; slerps the root rotation by the weight; stores `vSolutionUp` `+0xb0`, the model-space target `+0xbc` (pos) and `+0xc8` (quat), sets `+0x9d = 1`. Writes only the rotation (position kept, pad 0); NaN checks ("Model space target position is NaN", "Storing NaN as vSolutionUp") | high (I/O), med (pole geometry) |
| 0xb `FUN_021e5240` -> `FUN_021e4900` | `JointNode` (`R_Arml` 110) | if solved: rotate the joint so the forearm points at the model-space target (`+0xbc`), up from `+0xac`; slerp by weight | high (I/O), med (meaning) |
| 0xc `FUN_021e2b70` | `TargetNode` (`R_Hand` 112) | if solved: slerp the node's rotation toward the model-space target rotation `+0xc8` by weight | high |

Consequences **[high, from the code and AN 1.2]**:
- The target is read with `GetNodeTransform` during the player update, i.e. from the tracker pose = **last frame's**
  animation result (AN 1.2, 2.5). With `TargetSocket = IK_RHand` the hand follows last frame's IK target node.
- The chain is read live by the worker (direct mode) while `FUN_02185590` writes it on the game thread (the player
  update overlaps the jobs): the engine's own race; harmless for its data, a reason not to feed it from our code.
- Everything above is upstream of phase 2: a phase-2 post-hook (`ARM_TRK_EVAL`) sees its result and replaces it.

### 1.3 The other systems that move Talion's arms or hands

| System | Where | What it reads / does | Conf. |
|---|---|---|---|
| ProceduralBody limb elements (0x2a8 bytes each, in `ProceduralBody::Arm`/`LegPair` blendables) | `FUN_02247e30` (from `FUN_02239210+0x101c`, `FUN_0223c510+0x14a1`) adds type 0x11 (direct, `FUN_021ed970` -> `FUN_021ea210`) on node `elem+0x70` and 0x12 (snapshot, `FUN_021edb10`/`FUN_021edaf0`) on `elem+0x74`; nodes from the `Character.ProceduralBody` records | `FUN_021ea210` = a limb solver with world ray casts (physics `vtbl+0xa0/+0x68/+0x50`) on the worker thread; off when the static bools `0x4b57e47` or `0x4b57e34` are 0 (both 1 from `_INIT_1256`/`_INIT_1255`, no console name) | high (adds), med (role) |
| `CAnimationMgr::AlignHands` (vtable `0x38a5908`) | `vf_13` `0x21c8630` | sets an alignment target on both procedural arm elements (`FUN_02237b30(mgr+0x918, 0, side)` -> `FUN_0223c440`) | med |
| `CAnimationMgr::FindMoveAndPinIK` (vtable `0x38a5a88`) | `vf_12` `0x21daaa0` -> `FUN_021da2b0` (reads `ProceduralBody.IK.HandGuardDist`, cvar `0x4b57880`, value `0x4b578b8` = 15.0, at `0x21da966`) | finds a contact and pins a limb element ("FindAndMoveIK") | high (read), low (meaning) |
| `CAnimationMgr::IKOutput` (vtable `0x38a5b48`) | `FUN_021bf980` | reads a limb element's node with `GetNodeTransform` into animation variables (`FUN_020900b0`): output only | med |
| `EnableHandIK` | enum `FUN_025749f0` (None, EnableHandIK, EnableFootIK, EnableGroundIK, EnableGravity, Collision), parsed with the animation link attributes in `FUN_02575920` | a per-state flag; its reader was not traced | med |
| `Character.Models.IKMover` | `FUN_026028b0`: `AlignBone`, `UpdateAlignBone`, `HandBone`, `IKMovers` | names only | low |
| Havok Behavior IK | `hkbHandIkModifier`, `hkbFootIkModifier`, `hkbReachTowardTargetModifier` strings (`0x37b5b02`..`0x37b8178`) | one data reference each (`0x45f27xx` reflection tables), no code; no `hkaTwoJointsIkSolver` string | high (refs), med (unused) |

All of them run in the job or the player update, so G11 overrides them for the arms too **[high]**. They also tell us
when the game wants a hand somewhere itself (ledges, two-handed grips): a cheap blend-out signal (2.4).

## 2. Driving both arms from our code each frame

### 2.1 (a) Feed the engine's IK: possible, not chosen

Mechanics if wanted: claim a free chain slot of the player's `mgr` (`+0x110 = 1`), fill `+0x18` (hObj), `+0x90/+0x94/
+0x98` (hashes `R_Armu`, `R_Arml`, `R_Hand`), `+0xa0 = 33.92`, `+0xa4 = 27.42`, `+0xa8`, `+0xac`, then
`FUN_02185380(0.15, chain)`. Either leave `+0x68 = 0` and write `+0x74/+0x80` (world target) ourselves, or set `+0x68`
= Talion, `+0x70 = 0x2ee02ac5` and write the `IK_RHand` node at phase 2 (the next frame's `FUN_02185590` reads it back
through `GetNodeTransform`). `FUN_02185590` keeps a chain with `+0x40 = 0` in state 2 at weight 1 **[high]**.

Why not **[high for the facts, med for the judgement]**: (1) one frame of latency (the solve of frame N+1 uses a target
written at frame N's phase 2); (2) the worker reads the live chain while the game thread may write it (torn targets);
(3) the elbow pole is the engine's (root rotation + `JointTargetDist`), no twist share, no wall clamp inside; (4) 4
slots shared with the game's own `Modifier.IK` (two chains on `R_Armu` = two callback sets, the later one wins);
(5) direct-mode code on three worker threads. It saves only ~100 lines of math.

### 2.2 (b) Post-process the final pose at phase 2 (the write point)

**Hook**: post-hook the vtable slot `0x38df398` (`CSimpleModelTrackerNode::vf_03` `0x23899b0`, `ARM_TRK_EVAL`, AN 3.2
candidate A); act only when `this == *(playerSm+0x178)` (`playerSm = FUN_008f3c20(hChar)`) **[high]**. Game thread,
after the drain-and-wait `0x9eae7f` and the player update, no job alive, before the phase-2 connections copy the pose
to physics (4/5), cloth (6) and the output pose (7) **[high]**.

**Buffer**: `pb = *(trk+0x58)`, nodes `xf = *(pb+0x40)`, count `*(uint32*)(pb+0x48)` (168 for Talion). Node `i` at
`xf + 0x20*i`: `float pos[3]; float pad /* write 0 */; float q[4] /* x,y,z,w */` **[high, AN 2.1]**.
- **Model space** (Talion's object space, cm), **absolute**: each node holds its final model transform, not a
  parent-relative one. Changing a joint means moving its whole subtree ourselves **[high]**.
- No scale lane **[med]**. Parents always precede children **[high]**: one forward pass over the indices.
- The buffer ping-pongs with the job (AN 1.3): our edit is never fed back; `GetNodeTransform` returns it until the next
  phase 2 (the next player update sees our arms) **[high]**.

**Bones** (run 3 `nodes-2.tsv`; resolve the indices by hash at each model (re)build, check them against these):

| Role | Right | Left | Notes |
|---|---|---|---|
| clavicle | 26 `R_Shoulder` | 25 `L_Shoulder` | children of 23 `Spine4_joint`; 18.81 cm to the upper arm |
| upper arm (S) | 108 `R_Armu` | 86 `L_Armu` | 47.0 cm apart; ~30 cm from `Head` |
| upper-arm twist | 111 `R_Bicep` | 89 `L_Bicep` | child of S, at its midpoint (16.96 cm); rotates about the bone axis |
| forearm (E) | 110 `R_Arml` | 88 `L_Arml` | S -> E 33.92 cm |
| forearm twist | 113 `R_Forearm` | 91 `L_Forearm` | child of E, at its midpoint (13.71 cm); run 3: 87 deg about the bone axis |
| hand (W) | 112 `R_Hand` | 90 `L_Hand` | E -> W 27.42 cm; reach S -> W 61.34 cm |
| weapon node | 119 `R_Weapon` (sockets 14-16) | 97 `L_Weapon` (socket 6) | child of W, 12.07 cm away |
| fingers | 114-118, 120-129 | 92-96, 98-107 | children of W |
| not in the chain | 109 `R_ShoulderPad` | 87 `L_ShoulderPad` | children of the clavicle: leave |

Subtrees (index lists from the dump): `sub(108)` = 110-129 (20 nodes), `sub(110)` = 112-129 (18), `sub(112)` = 114-129
(16); left `sub(86)` = 88-107, `sub(88)` = 90-107, `sub(90)` = 92-107 **[high]**.

Bone frames from run 3's pose **[high for this pose; RT that the bind pose agrees]**: the right chain runs along local
**+X** (`R_Arml` at (33.92, 0, 0) in `R_Armu`, `R_Hand` at (27.42, 0, 0) in `R_Arml`), the left one along **-X** (a
mirrored skeleton). The elbow bends about the joint's local **Z** (`R_Arml` local rotation 27.1 deg about -Z, `L_Arml`
25.2 deg about -Z). The twist nodes rotate about local X. `R_Weapon` in `R_Hand`: pos (10.02, -6.01, -3.01), quat
(0.7041, 0.0982, 0.7012, -0.0530); `L_Weapon` in `L_Hand` mirrored: (-10.02, 6.01, 3.01), (0.0530, -0.7012, 0.0981,
0.7042). Socket 14 sits on `R_Weapon` turned -90 deg about X, 15 and 16 at identity. RT: whether `R_Weapon` is animated
relative to `R_Hand` (two `nodes` dumps, idle and mid-swing).

### 2.3 (c) Our analytic two-bone IK in that hook (the recommended solver)

Per frame, per arm, all in model space (`X` = Talion's object transform from the HOBJECT `trk+0x168`, pos `+0x8`, quat
`+0x14`, FP 2.1; RT A10 whether it moves before H1):
1. Grip -> world with the frame's rig (XI 4.2 steps 1-3, at the frame's `T'`), -> model: `G = X⁻¹ ⊗ grip`.
2. Targets: weapon node `T_w = G ⊗ C_h` (XI 4.2 step 5), hand `T_h = T_w ⊗ L_w⁻¹` with `L_w` = the weapon node's local
   transform in the hand (2.2). After the spring and the wall clamp (DV 1).
3. `d = p(T_h) - p_S`; clamp `|d|` to `[|a - b| + 0.5, a + b - 0.5]` (a = 33.92, b = 27.42); keep the residual for 4.
4. Bend plane: normal `n = normalize(d × (h - p_S))`, `h` = an elbow hint: the animated elbow blended toward "down,
   out and back" in Talion's body frame (and, optional, rolled by the controller's twist). Law of cosines:
   `cos α = (a² + |d|² - b²) / (2 a |d|)`, elbow `e' = p_S + a (cos α d̂ + sin α (n × d̂))`.
5. `dS` = the shortest rotation `(p_E - p_S) -> (e' - p_S)`, then a twist about `(e' - p_S)` that turns S's local Z
   (the hinge) onto `n`; apply about `p_S` to S and `sub(S)`: `pos' = p_S + R(dS)(pos - p_S)`, `q' = dS ⊗ q`.
6. `dE` = the rotation `(p_W - p_E) -> (p(T_h) - p_E)`; apply about `p_E` to E and `sub(E)`.
7. `dW = q(T_h) ⊗ q_W⁻¹`; apply about `p_W` to W and `sub(W)` (fingers, the weapon node).
8. Twist share: turn the forearm twist node (113/91) about the forearm axis by ~0.5 of the hand's roll relative to the
   forearm, the upper-arm twist (111/89) by ~0.5 of step 5's twist, against candy-wrapper skinning.
9. Weight `w` (ease 0.1-0.2 s, AN 8.1): apply `slerp(1, d*, w)` instead of `d*` in steps 5-7. Normalise, pad = 0.
Cost: 3 rigid subtree moves of at most 20 nodes per arm, a few µs **[high]**. Report per arm: `reached` (residual < 1
cm), the residual, the clamp reason (reach, wall) for the blade (section 4).

### 2.4 Verdict

(b) + (c) **[high for the mechanics, RT visual]**: zero added latency (this frame's controllers, this frame's pose),
full control of the elbow and the twist, no contention with the game's IK slots, game thread only. (a) is the
fallback if our math misbehaves. The engine's own arm IKs need no switch: they run before phase 2. Their state is a
signal: a chain slot of `mgr` with `+0x110` set, `+0x10c != 0` and `+0x90` = `R_Armu`/`L_Armu`, or procedural limb
callbacks on the arm nodes (`ikcb`, 6.2), means the game is placing that hand itself (ledge, climb, two-handed grip):
DV decides per case whether our weight blends down **[high for the fields, RT which states]**. To mute the engine's
arm IK anyway: write `chain+0x108 = 0` after `ARM_IKCHAIN_UPD` (all three callbacks return at weight 0) **[high]**.

Rejected as primary (AN 3.2): (c) as our own node-callback type inside the job (targets frozen at phase 1, worker
thread), the skinning copy (attachments already placed).

## 3. Risks

| Risk | Finding | Conf. |
|---|---|---|
| **Shadow** | one `CRenderNodeMeshSkeletal` per model (RS 2.1), its skeletal transforms copied once at phase 7 (AN 1.4) and drawn by both skeletal submitters (G-buffer and shadow maps): the shadow shows the IK'd arms | high (one node), med (same palette in every pass) |
| **Weapon** | `CSimpleModelAttachNode::vf_02` (`0x235a660`, phase 7) places each child at `header ⊗ pose[node] ⊗ offset` from the output pose's input, which the phase-2 connections fill from our edited buffer: Urfael and Acharn follow the hand (AN 4) | high |
| **Physics** | the physics node's keyframed bodies (phase 4) are fed from the current pose (follow); a ragdoll (phase 5) writes over the arms and `GetNodeTransform` returns it (death: correct); RT `Combat.RagdollReaction` while alive (AN 7) | med |
| **Cloth, hair** | the cape (`..._pd_ragged_cloak.hkt`, phase 6) collides with the edited arms; the hair bones (47-85) hang under `Head`, outside the arm subtrees | med |
| **Server / network / save** | SoM is single-player; the "server" objects (Player, Combat) live in the same process and read the one `IEngineModel`; no pose is replicated. The game's melee hits come from animation sweeps sampled between `BCOL`/`ECOL` keys (`CCombatColliderShapecast::vf_03` `0x270f600`, GP 4) and distance/angle cones, not from the live hand, so the edit changes no stock hit rule. Readers of hand nodes through `GetNodeTransform` (FX, the bow's fire transform) see the visible hand | med |
| **Engine IK in the same frame** | `Modifier.IK` and the procedural limbs run in the job before phase 2: overwritten; nothing to switch off | high |
| **Object motion after phase 2** | if the physics step (phases 4-5) moves Talion's object, the drawn hand moves with it away from the controller: AN 9.4 / runtime-probes A10 (`pose 120`: `obj_pre/post/vis`); fix by predicting `X` or doing the IK in `ARM_OUT_EVAL` (pre, phase 7) | RT |
| **Display time** | the IK needs this frame's grips at phase 2, before H4: one `T'` per frame picked before the player update and shared with H4 (XI 1.5, "One `T'` per game frame"), the rig step moved to `ARM_PLAYERUPDATE` (post, XI 4.2 step 3) | high (order), med (plan) |
| **Cinematics, pause** | the cinematic node (phase 4) replaces the pose; a paused game skips phase 2 (the arm keeps its last pose) | med |
| **Indices** | from data: rebuilds (armour swaps, FP 3.1) can change the skeleton; resolve by hash, invalidate on a new `trk` or skeleton pointer | high |

## 4. The sword and the blade segment

- **Does Urfael follow `R_Weapon` every frame?** Yes: the attach entry (`*(playerSm+0x90)` -> `+0x48..+0x50`, 0x68
  bytes) holds the resolved node index `+0x20` and the effective offset `+0x28` (socket offset ⊗ user offset); phase 7
  places the child from the pose node every frame (AN 4) **[high]**. Which node or socket (`ActiveNode` of the item's
  slot-model record): RT `weapons` (P7). Expected: `R_Weapon` or one of its sockets 14-16 **[low]**.
- **The blade from the bone** **[med, the recipe]**: once G11 runs, compute the segment in the `ARM_TRK_EVAL` post-hook
  right after the IK (same frame, before phase 7):
  `blade_world = X ⊗ pose'[R_Weapon] ⊗ C_h⁻¹ ⊗ B_grip`, `B_grip` = today's controller-frame segment of
  `somvr_blade_controller` (`mod/somvr/somvr_combat.c`: `SOMVR_BLADE_OFS_CM`/`_PITCH_DEG`, base 8 cm back, tip 90 cm
  along the grip's +Z). Because `T_w = G ⊗ C_h`, this equals the controller blade exactly whenever the arm reaches
  (residual < 1 cm) and is the visible sword when it does not (reach, wall clamp, spring): what you see is what hits.
  It needs no sword-model data for the hits; the sword model only decides `C_h` (the drawn blade must lie on `B_grip`).
- **Velocity**: keep the controller's tracking-space `v + ω × r` (XI 4.1) while `reached`; when clamped, finite
  differences of the bone tip mapped back into tracking space with the frame's rig (never world differences: walking
  with a still sword must not slash) **[med]**.
- **Check**: post-hook `ARM_ATTACH_LISTENER` (phase 7): the child's transform written there must equal
  `X ⊗ pose'[node] ⊗ entry+0x28` (proves the blade and the drawn sword agree, RT).
- **Left hand**: the same with `L_Weapon` 97 for Acharn (30 cm segment) and the palm sphere of the empty hand at
  `L_Hand` ⊗ a palm offset (socket 7 on `L_Hand` sits 7.5 cm out, a candidate) **[low]**.
- Proposed change for `mod/somvr` (not made here): a provider `somvr_blade_bone(pose'[weapon], X, C_h⁻¹ ⊗ B_grip,
  reached, grip velocities)` called from the G11 hook; H4's `combat_frame` uses its result for that frame.

## 5. Sites

All checked by `python3 -I scripts/check_doc_sites.py reference/bin/linux/ShadowOfMordor docs/re-arms-ik.md`.

| # | Name | Kind | Use | Conf. |
|---|---|---|---|---|
| 1 | `ARM_TRK_EVAL` | vtable slot `0x38df398` -> `0x23899b0` (= AN `AN_TRK_EVAL`) | **the write point**: post-hook, player's tracker only, IK + blade | high |
| 2 | `ARM_OUT_EVAL` | vtable slot `0x38de568` -> `0x237ce40` | fallback (pre-hook, phase 7) if A10 shows object motion after phase 2 | high |
| 3 | `ARM_ATTACH_LISTENER` | vtable slot `0x38d8958` -> `0x235a660` | post-hook: the weapon child's final transform (4, check) | high |
| 4 | `ARM_PLAYERUPDATE` | call `0x9ebcae` -> `FUN_00ab3f80` | post-hook: the rig step and `T'` before phase 2 (XI 4.2 step 3) | high |
| 5 | `ARM_IKCHAIN_UPD` | call `0x20a51d4` -> `FUN_02185590` (`rdi` = chain) | post-hook: read (or mute) the engine's chain; `ikwatch` | high |
| 6, 7 | `ARM_IK_SETUP_A`/`_B` | calls `0x21b875e` (`IK::vf_07`) / `0x21b8c8a` (`IK::vf_12`) -> `FUN_02185900(chain, hObj, record)` | post-hook: log each chain setup (record, nodes); `ikwatch` | high |
| 8, 9 | `ARM_IEM_ADDCB`/`_REMCB` | IEngineModel slots 38/39 (`0x3484260` -> `0x91ad70`, `0x3484268` -> `0x91adf0`) | `ikcb`: every node callback added/removed on Talion | high |
| 10, 11 | `ARM_IEM_GETNODE`/`_GETNODEXF` | IEngineModel slots 16/23 -> `0x91a750` / `0x91c360` | check data: the calls the hook makes (no hook) | high |

```c
SITE("ARM_TRK_EVAL", K_VSLOT, 0x38df398, 0x38df380, "80 79 38 02 00 00 00 00 30 82 38 02 00 00 00 00 30 9b 35 02 00 00 00 00 b0 99 38 02 00 00 00 00 f0 e8 38 02 00 00 00 00", 0x23899b0, "48 8b 97 08 08 00 00 c6 87 48 01 00 00 00 48"),
SITE("ARM_OUT_EVAL", K_VSLOT, 0x38de568, 0x38de550, "10 f3 37 02 00 00 00 00 70 f7 37 02 00 00 00 00 d0 9f 6f 00 00 00 00 00 40 ce 37 02 00 00 00 00 50 9a 6f 00 00 00 00 00", 0x237ce40, "48 83 bf 88 03 00 00 00 0f 84 f2 02 00 00 41"),
SITE("ARM_ATTACH_LISTENER", K_VSLOT, 0x38d8958, 0x38d8948, "20 a2 35 02 00 00 00 00 10 a5 35 02 00 00 00 00 60 a6 35 02 00 00 00 00", 0x235a660, "48 83 bf 80 00 00 00 00 0f 84 9a 06 00 00 41"),
SITE("ARM_PLAYERUPDATE", K_CALL, 0x9ebcae, 0x9ebca6, "e9 50 fb ff ff 4c 89 e7 e8 cd 82 0c 00 e9 b4 f1 ff ff", 0xab3f80, "55 48 89 f8 48 8d 6f 28 53 48 8d 5f 08 48 83"),
SITE("ARM_IKCHAIN_UPD", K_CALL, 0x20a51d4, 0x20a51c3, "48 8d ab d8 34 00 00 4c 8d a3 38 39 00 00 48 89 ef e8 b7 03 0e 00", 0x2185590, "8b 87 0c 01 00 00 85 c0 0f 84 4a 01 00 00 53"),
SITE("ARM_IK_SETUP_A", K_CALL, 0x21b875e, 0x21b874f, "4d 89 6c 24 68 48 8b 53 50 4c 89 e7 4c 89 ee e8 9d d1 fc ff", 0x2185900, "41 55 41 54 55 53 48 89 fb 48 89 d7 48 83 ec"),
SITE("ARM_IK_SETUP_B", K_CALL, 0x21b8c8a, 0x21b8c7c, "4c 89 63 68 48 8b 55 50 4c 89 e6 48 89 df e8 71 cc fc ff", 0x2185900, "41 55 41 54 55 53 48 89 fb 48 89 d7 48 83 ec"),
SITE("ARM_IEM_ADDCB", K_VSLOT, 0x3484260, 0x3484258, "80 a9 91 00 00 00 00 00 70 ad 91 00 00 00 00 00 f0 ad 91 00 00 00 00 00", 0x91ad70, "48 85 f6 74 6b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("ARM_IEM_REMCB", K_VSLOT, 0x3484268, 0x3484260, "70 ad 91 00 00 00 00 00 f0 ad 91 00 00 00 00 00", 0x91adf0, "48 85 f6 74 5b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("ARM_IEM_GETNODE", K_VSLOT, 0x34841b0, 0x34841b0, "50 a7 91 00 00 00 00 00", 0x91a750, "41 54 55 48 89 cd 53 48 85 f6 74 34 48 89 f7"),
SITE("ARM_IEM_GETNODEXF", K_VSLOT, 0x34841e8, 0x34841e8, "60 c3 91 00 00 00 00 00", 0x91c360, "48 85 f6 0f 84 97 00 00 00 41 55 48 89 f7 49"),
```

`FUN_02185590` and `FUN_02185900` have only these direct callers (xrefs: `0x20a51d4`; `0x21b875e`, `0x21b8c8a`)
**[high]**. Data the hook reads (check at start): `0x46a9230` (IEngineModel), `playerSm+0x178`, `*(*(sm+0x28)+0x10)`
(skeleton: names `+0x38`, hashes `+0x40`, AN 2.2), the chain slots `mgr+0x34d8 + 0x118 i` (i < 4).

## 6. Runtime probe words

### 6.1 Existing words (mod/probe)

| Word | Gives here | When |
|---|---|---|
| `nodes` | the skeleton with world transforms: check the indices of 2.2, `L_w` and whether `R_Weapon` moves inside `R_Hand` (dump idle, sword drawn, mid-swing) and where nodes 4/5 sit | any world visit |
| `weapons` | each weapon entry's `active_node`/`inactive_node` names: Urfael's and Acharn's grip node or socket (4) | sword drawn and sheathed |
| `pose 120` | `obj_pre/post/vis`: does Talion's object move between phase 2 and H1 (A10, 3) | while running |
| `piecehide N` + shots | the arms' pieces stay visible with the head hidden | P3 |

### 6.2 New words (proposals; read-only unless marked A)

| Word | Hooks | Logs | Answers |
|---|---|---|---|
| `ikwatch [SECS]` | post `ARM_IK_SETUP_A/_B`, post `ARM_IKCHAIN_UPD` (player's `mgr` only) | per setup: slot, record pointer, `RootNode`/`JointNode`/`TargetNode` and the target node `+0x70` as names, `+0xa0/+0xa4/+0xa8/+0xac`; every 10th frame per active slot: state `+0x10c`, weight `+0x108`, target `+0x74` in Talion's frame; nodes 4, 5, 90, 112 in model space and `|IK_RHand - R_Hand|`, `|IK_LHand - L_Hand|`; the `STATE` tags | which animations run the engine's arm IK, whether `TargetSocket` = `IK_RHand`/`IK_LHand` (1.1), XI 4.3's RT |
| `ikcb [SECS]` | slots `ARM_IEM_ADDCB`/`_REMCB`, `hObj` = Talion only | type, node name, flags, owner, size per add/remove | runtime-probes A7: which types (0xa-0xc, 0x11/0x12) touch the arm nodes and when (climb, ledge, grabs) |
| `armpose rot DEG` / `armpose ik X Y Z` / `armpose off` (A) | post `ARM_TRK_EVAL` (player's tracker), post `ARM_ATTACH_LISTENER` | `rot`: the `R_Hand` subtree turned DEG about the forearm axis; `ik`: 2.3 to a fixed target in Talion's frame (cm, X right, Y up, Z forward); once a second: `R_Hand` before/after, `GetNodeTransform(R_Hand)` in the next player update (`P_ANIM_B`), the sword child's transform vs `X ⊗ pose'[node] ⊗ offset` | runtime-probes A5 (the write-point proof), 3 (shadow, cape, sword in shots), 4 (attach check) |
| `wnodes [SLOT]` | none (reads) | the active weapon item model's own skeleton and sockets (as `nodes`, for its `CSimpleModel`), plus its attach entry (`+0x20`, `+0x24` -> name, `+0x28`, `+0x44`) | the blade axis and length in the sword model (DV 3's blade-length RT), the grip for `C_h` |

Order: `nodes`, `weapons`, `ikwatch 60` and `ikcb 60` while walking, a swing, a ledge and a climb (flat, R); then
`armpose rot 30`, `armpose ik 25 10 35` with shots (A, under spike1's save guard), `armpose off`.

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results).**
- **`armpose` (the write point, 2.2): pass.** `rot 60`, `ik 25 10 35` and `ik 30 55 40` at the tower top, about 210
  frames each, 0 skipped.
  - The next player update read `R_Hand` 0.0 cm from our edit every time (542 logged checks), and the IK targets were
    reached.
  - Shots: the right hand forward at hip height (31), raised to the shoulder (32), back to idle after `off` (33); no
    flicker.
  - Not checked: the sword following the hand. It was sheathed on `scabbard_back`, outside the edited subtrees.
- **`ikwatch`/`ikcb` (1.1-1.3): the engine runs no arm IK on Talion** in 20 s of idle and 90 s of melee (3 strikes, 39
  recoils).
  - 0 `Modifier.IK` setups; the 4 slots of his manager stayed `in_use 0`.
  - `IK_LHand`/`IK_RHand` stayed at the model origin (0, 0, 0), 33-72 cm from the hands.
  - 0 node callbacks were added or removed on his object.
  - So 2.4's blend-out has nothing to do in these states. Ledges, climbs, grabs and drawn-sword idles are not covered.
- **`wnodes` (4):** `PC_Sword_01` = `root`, `sword_handle`, `sword_tip`. The blade lies along the model's +Y, the tip
  100.7 cm from the object's origin; 8 sockets on `sword_handle`, from the tip (y 100.45) to the pommel end (-32.23).
  The attach entry was the sheathed one (socket 24 on `scabbard_back`, offset (0, -14.07, 0)). `C_h` still needs the
  in-hand entry (socket 16 on `R_Weapon`): `wnodes` with the sword drawn.

## 7. design-vr.md markers and open items

Resolved by this doc (design-vr.md is not edited here):
- Section 1, "The body you see when you look down", **"needs RE: who reads them"** (`IK_LHand`/`IK_RHand`, with
  "an engine IK reading them would fight G11"): the reader is `Modifier.IK` through its `TargetSocket` (1.2) and it
  does not fight G11, which runs after it (phase 2). The listed candidates (a) procedural body arm IK, (b) own node
  entries, (c) pose buffer: (c) with our own solver (2.4).
- XI 4.3 and 5.4's "needs RE: who reads/writes `IK_LHand`/`IK_RHand` and the IK callbacks' targets": 1.1-1.3.

Narrowed, still RT: section 3's blade table, "length **RT: from the sword model's nodes**": the hit segment no longer
needs it (4, `C_h⁻¹ ⊗ B_grip`); the model's blade axis only sets `C_h` (`wnodes`).

Open:
- RT: which animation states use `Modifier.IK` on Talion and with which `TargetSocket` (`ikwatch`); which procedural
  limbs he has (`ikcb`); `R_Weapon` fixed or animated in `R_Hand`; A10.
- The axis the engine's solver treats as "down" (`DownJointSign`) and `EnableHandIK`'s reader: not needed for (b)+(c).
- `C_h` per hand (XI 4.2 step 5) from `wnodes` and a shot.

## 8. Status: built offline (2026-10-20)

`mod/somvr` (re-xr-pipeline.md 6.6 has the switches, the logs and the runtime checks):
- **Hook** `ARM_TRK_EVAL` (site 1 of section 5, byte-checked, in `check_sites.py`), only with `SOMVR_ARMS=1` (which needs
  `SOMVR_INPUT=1`); plain C in the slot: `vf_03` takes only `this`. The filter is `this == *(FUN_008f3c20(obj) +
  0x178)` with the tracker vtable checked, resolved at every stereo H4 (read-only site `ARM_MODELOF`); `trk+0x168` is
  only logged against the object (RT). The skeleton comes from `*(*(trk+0x808)+0x10)` (the bind-pose path of `vf_03`
  reads it; entries, count, hashes as AN 2.2), both chains by hash and parent checks.
- **Solver** (`somvr_arms.c`) = 2.3 steps 3-9 with: the hint a fixed body-frame direction (down, out, back) plus 0.25
  of the animated elbow, and the elbow's plane rate-limited (720 deg/s) instead of a blend toward the animation (every
  constant hint has two directions where it gives no plane; the limit turns a flip there into a quick swing); the roll
  of S onto the hinge so E stays a pure hinge about its -Z; the twist shares measured against the parent (upper arm)
  and the forearm (hand) by swing-twist and unwrapped frame to frame; the weight slerps the joints' local rotations.
- **Targets**: the weapon node `G (x) C_h`, the hand `(x) L_w^-1` from this pose (2.3 step 2); default `C_h` = 90 deg
  about the grip's X (run 3's hands: the thumb side is `R_Weapon`/`L_Weapon` +Y; the forearm then lies on the grip's
  +Y, OpenXR's wrist side); `SOMVR_ARMS_CAL_R`/`_L` override it (RT: the sword's blade axis in `R_Weapon`, `wnodes`).
- **Time and rig** (3, "Display time"): not moved to `ARM_PLAYERUPDATE`. The grips are located at
  `somvr_views_peek_time` (the time this frame's H4 locates the eyes for), and the previous stereo frame's rig is carried
  by Talion's move since; exact for the head/player base while walking, a frame behind on snap turns and recentres.
- **Blade** (4): from `R_Weapon` whenever the right arm was solved that frame, else the controller grip; the runtime's
  velocities only while reached.
- Not built: the IK spring and the wall clamp (DV 1), the blend-out for the engine's own arm IK states (2.4), the left
  blade, ARM_OUT_EVAL (only if A10 says so), the probe words of 6.2.
