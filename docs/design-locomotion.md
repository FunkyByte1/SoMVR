# Shadow of Mordor VR - locomotion, traversal and combat movement

Written 2026-10-20 from the user's decisions of that morning (section 0), a web survey of fast VR movement (section 1),
the RE docs (`re-gameplay.md`, `re-traversal.md`, `re-m1m2.md`, `re-input.md`, `re-combat-vr.md`, `re-xr-pipeline.md`
12), the runtime results (`runtime-probes.md` run 3/P5, P7, P8, P9; `NOTES.md`) and new static RE for this doc
(section 2). Docs and static RE only: nothing was launched, the install was only read, no `mod/` file and not
`design-vr.md` changed (section 5 lists the proposed changes). Scratch (git-ignored): `reference/work-locomotion/`
(Ghidra batches `b1.txt`, `b2.txt` on `som-linux-q5`, both through the heavy-job gate; outputs `o1.txt`, `o2.txt`;
`sites.txt`).

Conventions as in `re-traversal.md`: addresses are runtime VAs of BuildID 87ec5d6f...; `FUN_x` = Ghidra's name;
`+0x2d0` = byte offset; **[high]** read in the code, **[med]** meaning inferred, **[low]** guess, **RT** = needs a boot.
`P` = client `Player*`, `pm = *(P+0x2c8)` (PlayerMovement), `body = *(P+0x2d8)`, `mgr = *(body+0x3b8)` (Talion's
`CAnimationMgr`), `pc = *(pm+0x50)` (his Havok character proxy). Sizes as in `design-hla.md`: **S** = up to one
helper-day offline, **M** = 2-3 days, **L** = about a week, **XL** = more than a week or research first; every item
also needs one headless boot and the headset session.

## 0. The user's decisions (2026-10-20, ~09:30), verbatim

- **Combat:** "Assisted physical". Real swings decide hits; orcs take turns from the front with telegraphs; parry by
  putting the sword in the way; a short assisted step toward the orc you swing at.
- **Finishers/executions/brutalize/cutscenes:** "VR-native finishers". You do the motion, the orc plays its
  animation, your view is never dragged, and cutscenes play around you.
- **Traversal:** "Hybrid". Hand climbing (prototype exists: SOMVR_CLIMB) plus a free-run button that lets the game's
  parkour carry you in first person.
- **Speed, the user's own answer:** "I don't get motion sick in Jet Island, even though I am grappling around, using
  rocket thrusters, etc., and moving very very fast. The reason is because of how smooth and fluid it is. In Sprint
  Vector, I'm racing at super speeds. But again, its super smooth, and I get no motion sickness. So I don't think
  speed alone is the issue. It's a combination of smoothness and 1-to-1 input-to-movement. I have complete control over
  my movement, so I'm never fighting the movement controls, having unexpected delays in gaining speed or stopping,
  etc. Acceleration and stopping are native to the movement mechanics. I'm not sure what VR games have parkour,
  climbing, or fast paced normal running, but I feel like there's gotta be some. So while HL:A doesn't do movement,
  maybe we can take ideas from other games or create new ideas based on what works for this game."
- **Vaulting in combat, the user's two ideas:**
  (1) "a sort of Sprint Vector / Gorilla Tag style hand motion. Give the player a prompt to sort of grab on to which
  jumps you over the enemy at a speed that directly is controlled by how you are moving your hand. This gives you
  intrinsic control over your vaulting. This would be ideal, but we'd have to nail it."
  (2) "Instead of vaulting over enemies, we can have the player turn into ghost mode (or whatever it's called) and sort
  of dodge/phase through the enemy. So it feels true to the game without any vaulting."

### 0.1 What this doc concludes, in short

- **The stock locomotion cannot be made 1:1 by tuning.** Talion's speeds, starts and stops are the root motion of
  animation clips chosen by the data animation graph; the 30 attributes of `Client.PlayerMovement` (all found, 2.2) hold
  no speed, acceleration or deceleration (only `BaseSpeedMultiplier`, which no code is found to read). The "VR quick
  acceleration profile" of `design-vr.md` 2/8 has nothing to set.
- **But 1:1 is cheap at the engine level.** In root-motion mode the movement solver sets the proxy's velocity to this
  frame's root delta / dt, with no memory (2.1). One call-site hook on the root-motion read inside
  `PlayerMovement::vf_11` (`LOCO_ROOTDELTA`, `0xab5ea9`) lets somvr replace the animation's translation with its own
  velocity, while the game keeps collision, steps, slopes, gravity, the animation locks and its leg animation (fed by
  our virtual stick). So somvr owns Talion's ground velocity (`SOMVR_LOCO`), and the game carries him only in moves the
  player asked for (free-run parkour, falls, syncs).
- **The gravity of -20** is `Client.PlayerMovement.PhysicsCharacterGravity`, the player record's value, passed to the
  proxy's creation (2.3): a console variable, so a gravity change needs no code patch.
- **Design:** instant stick walking and running; an arm-pump sprint with momentum, coasting and braking that the
  player makes; the existing hand climb plus a fling, an air catch and a push mantle; the free-run latch hands
  Talion to the game's parkour with a velocity-continuous, position-only view; combat movement with the assisted step,
  a dash and the wraith phase; Shadow Strike as a wraith dash. Comfort aids stay, off by default.
- **Vault ideas:** ship (2), the wraith phase, first: a presentation on top of the stock vault, so the game keeps its
  collision, landing spot and the orc's reaction (M). Then (1), the grab-vault, as the skill move: first on the stock
  vault with the hand driving its speed, then as a true fling once the climb fling and the NPC collision are known
  (L).

## 1. The principle and what fast VR games do

### 1.1 The principle, as rules

The user's answer turned into six testable rules for every motion of the view (the house rule "never move the
player without their input" stays above them):

| # | Rule | Test (automatic, design-vr.md 11) |
|---|---|---|
| **L1** | **Velocity is the player's.** Every change of the view's velocity is caused by an input the player makes at that moment (stick, arms, hands, a button just pressed) and starts within one frame; nothing ramps up or slides to a stop on its own | the anchor-cause log: every velocity change > 0.5 m/s in 0.1 s has a tagged input cause in the same or the previous frame |
| **L2** | **Momentum is native.** Speed the player built (arm pumps, a fling, a fall) persists and decays by rules the player can learn: friction, drag, gravity; stops are made by the player (stick released, stick back, a grab, the ground) | scripted drills: the same input gives the same speed curve within 2 % |
| **L3** | **Smooth.** No position steps, no judder: the view's position is continuous and its velocity continuous across hand-offs between our motion and the game's; the view is extrapolated to the display time; the game runs at the display rate | the both-eye pose log at 0.25 s and the per-frame anchor log: no step > the speed x frame time x 1.5; late frames counted while sprinting |
| **L4** | **No rotation from the game.** No pitch, roll or yaw of the view by animations, cameras or moves (design-vr.md 1/2) | existing assert |
| **L5** | **The game carries only what the player asked for.** Game-carried moves (parkour under the free-run latch, a stock vault under the phase, falls) are position-only, smoothed, and enter and leave at the speed the player had | hand-off log: velocity jump at entry/exit < 1 m/s after the filter |
| **L6** | **Assists are small, visible and the same every time.** The assisted step, aim bending of a fling, the vault glyph | logged per use with their size |

Comfort aids (vignette, blink) are options for players who want them, off by default: the user's principle puts
smoothness and control first, and the task brief lists the aids as "optional extras".

### 1.2 What fast VR games do

A web survey (2026-10-20; pages treated as data; **[DEV]** = developer statement, **[3P]** = reviewer or player,
**[OFF]** = publisher docs or official code; the URLs are in Sources at the end, `[Sn]`).

**Sprint Vector** (Survios, 2018). *Mechanics*: the "stride": hold the trigger at the top of an arm swing, swing down
and back, let go at the bottom; alternating arms keep you going, faster swings add more speed, and each stroke's gain is
a haptic pulse [DEV S1]. An "intended motion" layer reads head and hands together so different running styles work [DEV
S1]. Jumping is a gesture (grab the air and throw it down, momentum kept; the faster you go the narrower the jump
angle), because "press X to jump" was judged sickening [DEV S1]. Braking is a button, tuned so the stop does "not induce
nausea because of the sudden stop" [DEV S1]. Turns by a hand-anchored drift, because snap turns "broke the flow" [DEV
S1]; gripstreams (monkey bars) keep and can add momentum, no speed cap [DEV S1]. *Why comfortable*: joystick motion
sickened a sensitive tester at once while 1:1 climbing did not; the constant physical effort "doesn't trigger nausea",
the rhythmic re-sync "resets their expectation for nausea each time"; speed itself was not the problem [DEV S1]. Road to
VR, comfort 9/10: "fast acceleration and deceleration isn't nearly as jarring as it could be" [3P S2]. *Numbers*: claims
up to 300 mph [S3], a reviewer saw ~50 mph flying [S2]. *For SoM*: speed from arm strokes with haptic ticks (3.3); a
jump whose height is the arms' (3.6); a deliberate brake (3.3); no snap turns forced on the player at speed.

**Stride / Stride: Fates** (Joy Way). *Mechanics*: the stick walks, pumping the arms runs much faster; hold A and
release while throwing the arms up = a jump whose height and length come from the swing; wall runs by jumping at a
wall at an angle; slide by ducking; grab ledges and pull over; zip lines, a hook [3P S4]. Joy Way refuses stick-only
running: the game "is built around the locomotion system" [DEV S5]. *Comfort*: "the player's hand movements coincide
with his avatar's movements", minor unease at first that goes away [DEV S6]; reviewers: weighty and physical, but the
arm pumping "can be pretty dizzying", jump timing tricky [3P S4], ledges and wall runs that do not catch when expected
[3P S7]; Fates "one of the most nauseating" when the hands go wild with collision off [3P S8]. *For SoM*: exactly the
"stick walks, arms sprint" split of 3.2-3.3; Stride's jump release (3.6); its complaint list (catches that fail) is
the climb's main risk (3.5).

**Assassin's Creed Nexus VR** (Ubisoft Red Storm, 2023), **the closest analogue**. *Mechanics*: "leg" moves (rooftop
leaps, hops) stay automatic as in the flat games; "hand" moves (ledge grabs, wall climbs) are physical - you "reach out
and actually grab the world" [3P S9]. Free-run = run, hold A, look where you want to jump next; aim by the head [3P S10,
OFF S11]; no timed jumps on purpose, to avoid "feeling like a clumsy Assassin" [DEV S12]. Climb almost any straight
edge, pull up, mantle, push off to far holds, "launch themselves in any direction" [OFF S11, 3P S13]. Leap of Faith =
arms out straight and jump off [OFF S11], optional tunnel vision in the fall [OFF S14]. Comfort: four presets from a
survey (Immersive ... teleport + snap), parkour-teleport and climb-teleport, an Auto-Parkour latch (click the stick,
keep sprinting until you pull back), a Parkour Visualizer showing the arc, head- or hand-steering, proximity vignette,
fall tunnel vision, head-in-geometry guard, fear-of-heights grid, toggle grip and a hand-steadying climb assist, seated
parity [OFF S15]; "a team dedicated to nothing but comfort features" [DEV S16]. *Praise*: many played on Immersive with
no aids [3P S17, S10]; a sickness-prone reviewer was "shocked at how comfortable" [3P S18]. *Criticism*: nearly all
about climbing reliability: grabs not registering and falls, grip loosening, launches hard to trigger, invisible walls,
"climbing can become a death trap" in a chase, parkour "a bit handheld" [3P S19, S10, S13, S20, S21]. *For SoM*: the
same split as the user's "Hybrid" (free-run latch carries the leg moves, the hands climb); head-aimed leaps come free
with G2; a leap marker like the Parkour Visualizer (option); and the lesson that **a grab that fails is an unexpected
fall**, so the climb needs generous, assisted grabs (3.5).

**Gorilla Tag** (Another Axiom). *Mechanics* (official MIT code [OFF S22]): each hand is swept from its anchor to its
tracked pose; if geometry blocks it the body moves by the blocked amount (the hand stays, the body goes); two hands
average; the body's velocity is a short rolling average; while a hand is planted the velocity is zero; on release above
a minimum speed the launch is `jumpMultiplier x average`, capped at `maxJumpSpeed`; hands slide on surfaces; arm reach
clamped. *Why comfortable*: "You're using your arms like you would be using your feet" [DEV S23]; "I push on the world
and the world pushes back" [DEV S24]. *Numbers*: map SDK values speed cap 6.5 / multiplier 1.1 (survivor), 8.5 / 1.3
(infected), gravity -9.8 [3P S25]. *For SoM*: the fling rule and its constants (3.5), "velocity is what you really did"
(2.6), and the grab-vault's push-off (3.8).

**Jet Island** (Eric Tereshinski / Master Indie, 2018). *Mechanics*: a frictionless hoverboard, two wrist jets aimed by
the hands, grappling hooks to swing and to climb monsters [OFF S26]; the board faces the travel direction, hard turns by
hooking the ground, a full-stop brake, slow down by counter-thrust [3P S27]. *Comfort*: the developer calls it "like a
roller coaster, except you are in control" [DEV, excerpt only, S28]; players report hours without sickness, but the
optional auto-turn camera is "possibly one of the most nauseating experiences" [3P S29]. *Numbers*: ~200-250 mph
claims [S28, S30]. *For SoM*: the user's own reference; its lesson is that speed is fine when every change is the
player's and the view never turns itself (L1, L4).

**Windlands / Windlands 2** (Psytec). *Mechanics*: one hook per trigger (aimed by the head in 1, by the hands in 2),
swinging momentum, stick movement with momentum, a slingshot from two offset hooks, low gravity, a slide on landing [3P
S31, S32]. Comfort cage, floor markers, vignette, stepped turns, strafing off, head or hand forward, seated [3P S32].
*Comfort*: steady 90 fps matters, cages help some [DEV S33]; swinging "a little bit intense" for some [DEV S34];
reviewers split on the cage [3P S32]. *For SoM*: frame rate is comfort (3.4); aids as options only.

**Population: One** (BigBox VR). *Mechanics*: no jump button; climb any surface with an empty hand; glide in a T-pose,
bank to turn; fling = "pull down rapidly and release your grip button to launch yourself upwards" [OFF S35, 3P S36].
*Comfort*: "Falling is one of the biggest triggers in VR"; banking "tricks your brain"; vignettes by level; players
"desensitized ... to a point where you don't need the help" [DEV S37]. *Numbers*: flying twice as fast as walking
[OFF S35]. *For SoM*: the climb fling (3.5); falls are the hard moment (3.6).

**Climbey; The Climb 1/2** (Brian Lindenhof; Crytek). *Climbey*: physics climbing and flings, the launch is the
controller's velocity at release [3P S38]; floor dots, FOV reduction, a cage [OFF, excerpt only, S38]. *The Climb*: grab
with the trigger, stamina rings, chalk, a half-grip jump to clear gaps [3P S39, S40]. Crytek: falls start but "don't
plunge to the bottom and bounce off the rocks" because "that makes people really uncomfortable"; "we adjust the camera
as you pull your body around a corner. We smooth it out" [DEV S41]. Reviewers: "pulling the world with me", a direct 1:1
hand shift keeps you "feeling in control", never nauseous; comfort 8.5 [3P S42, S40]. *For SoM*: 1:1 hand climbing
(built), camera smoothing on the carried corners (3.4), and a fade option for long falls (3.6).

**Boneworks / Bonelab** (Stress Level Zero). *Mechanics*: a physics body on a rolling "locoball"; sprint, jump and
crouch by buttons or the body; climbing pushes forces into the body [DEV S43, 3P S44]. *Comfort*: "Motion sickness
dramatically reduces when you get into higher derivatives (jerk and jounce)" [DEV S45]; ~1.5 % refund for locomotion
comfort [DEV S46]; reviewers call it intense, worst in motions the player does not make (jump pads, carts, the body
caught on pipes, an arm brushing a wall swaying the view) [3P S47, S48, S49]. *For SoM*: smooth the derivatives of
game-carried motion (3.4's cascaded follower); never let a collision of the hands move the view.

**Lone Echo / Echo VR** (Ready At Dawn). *Mechanics*: "when you grab onto something, we start positioning the head
1:1 relative to the hand" - crawling, climbing, pushing off and stopping all follow; wrist thrusters for corrections,
a booster for bursts, an instant brake [DEV S50, 3P S51, S52]. *The key lesson*: with mass-ratio physics the head moved
when the player waved a held object - "neither predictable or controllable by the player" - so the player was made
infinitely heavy against held objects and only real level collisions may push them [DEV S50]. Slow acceleration "was
mostly done away with ... velocity would ramp up almost instantaneously" [DEV S53]. *For SoM*: the climb's head-to-hand
rule (built), instant ramps (3.2), held things (the sword, an orc's shoulder in the grab-vault) never push the view.

**Guidance and studies.** Oculus' old best practices: "Instantaneous accelerations are more comfortable than gradual
accelerations"; let the user start and control them; no head bob [OFF S54]. Meta today: the inner ear does not sense
constant velocity; keep accelerations brief or quantized; physical activity (walking in place, climbing) "can improve
comfort"; predictable camera motion [OFF S55]. Changing vection sickens more than steady vection [S56]; but a 2021 study
found no clear advantage of constant over variable speed in short trials [S57]. Sickness rose from 3 to 10 m/s and then
not further up to 59 m/s [S58]. Drivers got less sick than passengers watching the same drive [S59]. Arm-cycling no
worse than teleport, joystick worse [S60]; another study found arm swinging worse than teleport [S61].

### 1.3 What transfers to SoM

| Principle (cross-game) | Evidence | Where in this design |
|---|---|---|
| The hands make the velocity; the head is pinned to the grip | Lone Echo, Gorilla Tag, The Climb, Pop: One, Sprint Vector's origin | climb (built) + fling + mantle (3.5), grab-vault (3.8), arm pump (3.3) |
| The release speed is the gesture's, with hard caps | Gorilla Tag 1.1-1.3 x, 6.5-8.5 m/s; Climbey; Pop: One | fling 1.3 x, cap 8 m/s (3.5); Stride's jump release (3.6) |
| Momentum persists; stopping is the player's act | Echo's brake, Sprint Vector's tuned brake, Jet Island's counter-thrust | coast and brakes of the arm sprint (3.3); falls keep speed (2.4) |
| Ramps are instant or player-made, never imposed | Lone Echo, Oculus/Meta guidance, the user | 1:1 stick, no start/stop clips in the speed (2.6, 3.2) |
| Smooth the higher derivatives of anything carried | Boneworks (jerk, jounce), Crytek's corner smoothing | the cascaded follower (3.4) |
| Never rotate the view; never move it by motion the player did not make | Jet Island's auto-turn, Boneworks' shoves, Lone Echo's infinite-mass pass | L4; body yaw only, view yaw only by turns (3.2); hands never push the view |
| Help the intent, not the motion; reliable catches | AC Nexus (no timed jumps, aim by look), its climb complaints; Stride's failed catches | free-run latch carries leg moves (3.5); grab assist, toggle grip, fall catch (3.5) |
| Falls are the hardest moment | Pop: One, Crytek, AC Nexus' fall tunnel | short game falls (-20), fall fade and vignette as options (3.6) |
| Speed is fine when the rest holds | Survios, Lone Echo, So et al., the user | the sprint reaches the stock free-run speed, no VR cap below it (3.3) |
| Comfort aids are optional layers | every title; players turn them off | all off by default (3.11) |

## 2. SoM's locomotion engine

### 2.1 The frame, from stick to proxy

Per game tick on the game thread (run 3: one `vf_11` per tick, `P_PMROOT` = `ANIM_B`):

| Step | Where | What | Conf. |
|---|---|---|---|
| 1. animation inputs | `FUN_00a3c8d0` (called from PlayerCombat at `0xa8c8d7`; returns at once in FSM 1) | stick = `FUN_00ad0af0` (`0xa3c911`); **`RawMoveMag`** = \|stick\| clamped to 1.0 (`0xa3c9af`; the walk-and-talk follower `P+0x738` overrides it); **`Game_Velocity`** = \|the body object's displacement since last tick\| / dt (`0xa3cb4b`, positions kept at `P+0x8bc..+0x8c4`); **`Game_SmoothedVelocity`** = \|`pm+0xf8..+0x100`\| (averaged over `Movement.SmoothVelocity.Frames`, a shader parameter too); `Game_VelocityX/Y/Z` (in Talion's frame); `Game_FallingTime`; a "moving" bool from the graph's own `MoveThreshold` value (none: stick > 1e-11, `0x34647f0`); then `FUN_00a3c350` (`0xa3d232`) writes **`DeltaYaw`**/`CameraDeltaYaw` = the yaw from Talion's facing to the camera-rotated stick | high |
| 2. the graph | `CAnimationMgr` data (pieces registered in `_INIT_1316`) | picks idle, start, walk, run, sprint, stop and turn clips from those values and from commands (`Predicate.Command`: `FreeRun` 18 held); `Value.Dampener`, `Predicate.GreaterThan/LessThan/ParameterInRange`, `Modifier.Turn/Rotator/DesiredFacing/TranslationScaler` are its tools; the clips' root motion accumulates in `mgr+0x518` | high (pieces), data (which ones) |
| 3. root motion onto the transform | `PlayerMovement::vf_11` `0xab5e70` (slot `0x34fc838`) | `delta = FUN_00a513a0(body, &delta)` (**`0xab5ea9`**): `FUN_020bd780(mgr+0x518)` = the accumulated root motion {pos in Talion's frame, quat}, plus the combat translation scaling `FUN_023a1290(body+0x320)` while `body+0x398`; identity while `FUN_00a85590(PlayerCombat)` (`PlayerCombat+0xa08` set and a record flag: **[med]** a combat move that owns the movement). Then the one-shot extra (`pm+0x428`/`+0x434`/`+0x444`). Then, **unless an attack runs** (`pm+0x38` and `Combat_GetCurrentAttack`), the animation locks: `mgr+0x62a` (`vf_30`) zeroes the whole translation, the extra included; `mgr+0x629` (`vf_31`) the rotation. Last `xf.pos += R(xf.rot)·delta.pos`, `xf.rot = norm(xf.rot·delta.rot)` | high |
| 4. the solve | `PlayerMovement::vf_10` `0xab59a0` -> `FUN_020cda10` (`0xab59b5`) | desired velocity `(delta + shape offset)/dt` (cm/s) into `pm+0xe0..+0xe8`. **Root-motion mode** (`pm+0x1a2` = 1): x and z := it (y too only while gravity is off) - **no memory: the velocity is exactly this tick's delta**. **Momentum mode** (0): `v += delta/dt` (less the last root velocity `pm+0x104..` when `pm+0x1a1`). Park: supported, not sliding and \|v\| <= 75 cm/s -> gravity off and `vy = 0` for the step. Then the proxy: set velocity, `CPhysicsCharacter::vf_08` integrate (gravity `pc+0x58` while `pc+0x60`), position, velocity read back | high (`reference/work-traversal/acp.c`) |
| 5. collision | Havok `hkpCharacterProxy::integrate` | slides along walls (run 3: 17.8 of 30 cm kept at a wall), steps up to `StandCapsuleStepHeight` | high (run 3) |

So **all of Talion's ground speed, his starts and his stops are animation**: the start clip's root motion is the
acceleration, the stop clip's the deceleration, and the code integrates nothing. This is exactly the "unexpected delay
in gaining speed or stopping" the user names. Run 3 shows it: after `move 0 1 4000` (full stick held from rest) the
first `Run` tag came 0.6 s after the press (`Gnd Lnd` before it), and Talion covered 616 cm in the 4 s, ending at a
wall at the tower top (not a clean speed figure; P10 measures it).

**Runtime result (P10, 2026-10-20 15:46, `logs/boot-check-20261020-154655`; runtime-probes.md P10 results).** The
stock speeds (LR1, KBM mode, the `InCombat` tag on all run long):
- **One band: `Run` at 222-232 cm/s for every stick from 0.2 to 1.0** (`RawMoveMag` read exactly 0.2-1.0). No walk band,
  no stick-to-speed curve.
- **`FreeRun` (the sprint): 636-641 cm/s**, `Spr` from the first ticks.
- Starts from rest without a turn clip: first motion 0.18 s, 50 % at 0.52 s, **90 % at 0.82 s**. With a turn clip first
  (the stick behind Talion): 90 % at 0.95-1.08 s. The sprint reached 90 % at 0.58 s.
- Stops: 0.60 s and 55-59 cm on the flat; 0.97 s / 135 cm downhill; 1.27 s / 200 cm after a reversal. Two bands were
  still moving 1 s after the release.
- A reversal at full speed turned ~166 deg in ~0.5 s with no dip.

That is the lag the user names: 0.8 s to speed, 0.6 s and over half a metre to stop. Crouch, the combat strafe and a
walk (pad mode, out of `InCombat`) are still unmeasured. `SOMVR_LOCO`'s bands are now run 230 and sprint 640
(re-xr-pipeline.md 17.8).

### 2.2 Where speed and acceleration live: the 30 attributes of `Client.PlayerMovement`

The schema `Client.PlayerMovement` (short name `PlayerMovement`, its help text "This schema exposes console variables
associated with player movement. The attributes listed here are all console variables, modifiable in-game.") is built
by **`FUN_02609430`** into the descriptor array at **`0x4c25520`** (30 x 0x30 bytes; header at `0x4c254e0`: pointer,
count 30, names; `_INIT_1317` at `0x619a36`). The player's record is cached at **`pm+0x448`** by `FUN_00ab8700`
(`PlayerMovement::vf_34` and `FUN_00ab8c10` call it). A reader takes attribute N's descriptor `D = 0x4c25520 + 0x30·N`:
fast path when `*(int*)(D+0x18) >= 2` (entry `*(rec+8) + 16·*(u8*)(D+0x28)`, value `*(rec+0x10) + 8·*(u16*)(entry+0xe)`
when `*(u16*)(entry+0xc)`), else the slow hash search (`D+0x8`) **[high]**:

| N | Attribute | Type | Read by | Use | Conf. |
|---|---|---|---|---|---|
| 0-3 | `FallDamageMinHeight`, `FallDamageMaxHeight`, `FallDamageMin`, `FallDamageMax` | float | `FUN_00ab9c90` (landing), `FUN_00ab6ff0`, `0x2393852` | fall damage from the fall height | high (reads), med (use) |
| 4 | `FallMinHeight` | float | `FUN_00ab8700` -> `pm+0x454` (default 100) | the least height that counts as a fall | high |
| 5-14 | `CamLandMinHeight`, `CamLandMoveDist`, `CamLandDownTime`, `CamLandUpTime`, `CamLandRollVal`, `CamLandRollTime1/2`, `CamLandPitchVal`, `CamLandPitchTime1/2` | float / ms | `FUN_00ab9c90` (also at `0xaba6b2` inside it) | the landing camera dip, roll and pitch (VR: the camera is ours; the dip is capped in design-vr.md 4) | high |
| 15 | `PlayerYawInterpolateTime` | ms | `FUN_00ab8700` -> `pm+0x450` (default 0.4 s) | body-yaw interpolation (readers not traced) | high (cache), med |
| **16** | **`BaseSpeedMultiplier`** | float | `FUN_00ab8700` -> **`pm+0x32c`** (`0xab885a`; the ctor writes 1.0 at `0xab67cd`) | **no reader found**: a scan of all 166 instructions with offset `0x32c` in the text finds none in the movement, player or animation code | high (store), med (unused) |
| **17** | **`PhysicsCharacterGravity`** | float | `FUN_00ab7a60` (default -9.8 = `0x34fd360`) | the proxy's gravity (2.3) | high |
| 18-21 | `UsePenCollision`, `PenOriginOffset`, `StandingPenOriginOffset`, `AimingPenOriginOffset` | bool / vec3 | `FUN_00ab7a60` (18, 20 seen; 19, 21 not traced) | penetration tests of the capsule | high (reads), med (use) |
| 22-24 | `AimingRadiusScale`, `AimingTeleportRange`, `AimingTeleportExclusiveTags` | float / range / tags | `FUN_00ab7a60` (22, 23), `FUN_00ab6880` (24) | the capsule while aiming; the aimed (Shadow Strike) teleport's range | high (reads), med (use) |
| 25-27 | `StandCapsuleHeight`, `StandCapsuleRadius`, `StandCapsuleStepHeight` | float | `FUN_00ab7a60` (27 seen; 25, 26 most likely the capsule shape code) | the capsule and its step height | high (27), med |
| 28-29 | `SpectatorSpeedMultiplier1/2` | float | `SpectatorMovementController::vf_04` | the free camera only | high |

None of them is a walk, run or sprint speed, an acceleration or a stop time **[high]**. The console names of these
variables are not in the binary (the schema is data-driven): RT, `pmrec` lists them (section 4.2).

**Runtime result (P10, LR2).** `pmrec` read all 30 on the fast path. The values: FallDamageMin/MaxHeight 500/1200 cm
with **FallDamageMin/Max 0/0**, FallMinHeight 100, CamLand* 100 / -3 / 150 / 250 ms / 5 / 50 / 150 ms / 5 / 50 / 150 ms,
PlayerYawInterpolateTime 400 ms, BaseSpeedMultiplier 1, **PhysicsCharacterGravity -2000** (cm/s^2; the proxy's
`pc+0x58` reads -20 in m/s^2: 2.3's chain holds), AimingRadiusScale 1.8, AimingTeleportExclusiveTags `Ral`,
`Mount_Warg`, `Mount_Troll`, StandCapsuleHeight 125, Radius 35, **StepHeight 40**, Spectator 10 / 0.1. **None is a
console variable** by `PlayerMovement.X`, `Client.PlayerMovement.X` or `X`, so G22 cannot set them by name. **The
"no reader" holds**: `BaseSpeedMultiplier` 1.5 written into `pm+0x32c` left the run at 229.8 cm/s (229.3 and 224.2
without).

### 2.3 Gravity -20

`FUN_00ab7a60(pm, mode)` (10 call sites: entering the world, the alive state, the record change `FUN_00ab8700`, the
spectator, and small switches in PlayerMovement `FUN_00ab9370/93a0/93d0/9c00` and PlayerCombat `FUN_00a9bea0`, most
likely the crouch and aim capsules **[med]**) reads `PhysicsCharacterGravity` (default -9.8), `StandCapsuleStepHeight`
and `UsePenCollision` and passes them to the capsule rebuild **`FUN_020c8560`** (call **`0xab7c52`**: gravity in
`xmm3`), which stores it at `-0xac(%rbp)` and hands it in `xmm0` to **IEnginePhysics vtbl+0xc0** (`mov 0xc0(%rax),%r15;
call *%r15`, `0x20c87b4`), the create-character call **[high]**. P7 read `pc+0x58` = -20 in the world while the constant
and the `CPhysicsCharacter` ctor say -9.8: so the player record's `PhysicsCharacterGravity` is **-20** and it reaches
the proxy through that call **[med: the store inside the create call is not read; the chain and the value fit]**. This
answers re-traversal.md 2.4's open "find the writer".

Consequences: the game's falls and jumps are twice as fast as real (a 5 m drop takes 0.71 s instead of 1.01 s and hits
at 14.1 m/s; a 1 m hop needs 6.3 m/s up and lasts 0.63 s). Any gravity change can go through the console variable (G22,
name RT) and takes effect at the next capsule rebuild, or through `pc+0x58` directly (live, next step), always from
the live value.

### 2.4 The FSM and the two velocity modes

From re-traversal.md 1.2, with the mode switch `FUN_020c6ff0(pm, rootMotion)` (`LOCO_VELMODE`: when switching to
momentum it zeroes the last root velocity `pm+0x104..+0x10c`) and its seven callers:

| FSM `P+0x7f0` | State | Mode on enter | Caller | VR owner (section 3) |
|---|---|---|---|---|
| 6 | `AliveStateSupported` | root motion | `0xc97c60` | **somvr** (LOCO), except the graph's own transitions and locks |
| 7 | `AliveStateCombat` (also Talion's hit recoils, P8) | root motion | `0xc97a10` | **somvr** between recoils; recoils game-carried, capped |
| 8 | `PlayerStateFalling` | momentum | `0xabc7e0` (call `0xabc846`) | physics (ours if we launched him) |
| 9 | `PlayerStateTransitionAnim` | root motion | `0xa49540` (call `0xa495a3`) | **the game** (parkour, vaults), position-only view |
| 10 | `PlayerStateLanding` | root motion | `0xabd760` | the game, or somvr carrying the landing speed (RT) |
| 3, 11 | spectating, MP | momentum / - | `0xabdba0`, `0xebba40` | - |

`PlayerStateFalling::vf_03` stores the start velocity `pm+0xe0..+0xe8` and the start height **[high, o2.txt]**: a fall
keeps whatever horizontal speed Talion had, ours included. Falls, landings and transitions come from the graph's
`Modifier.PlayerStateEvent`; climbs, vaults, mantles and leaps are graph states inside FSM 6 or 9 (re-traversal.md 1).

### 2.5 How the graph picks walk, run and sprint, and feeding it our speed

The code picks nothing: the choice is graph data (RT). Its inputs from the code are step 1's values, all of which we
control or which follow our motion:

- **`RawMoveMag`** = the stick magnitude: set by our virtual stick (G3, re-xr-input.md 3.3), or overwritten after the
  game writes it (`LOCO_RAWMOVEMAG`, the call at `0xa3c9af`; or a post-hook of `FUN_00a3c8d0` at `0xa8c8d7` that sets
  graph variables with the game's own setter `FUN_020900b0(value, mgr, FUN_0208dd90(mgr, "name"), 1)`).
- **`FreeRun`** held = the sprint/free-run branch (stock pad A in run 3's bindings).
- **`DeltaYaw`** = the turn the graph plays; it follows the stick direction relative to Talion's facing, so a facing we
  set (3.1) keeps it small.
- **`Game_Velocity`** is measured from the body's real displacement, so **it reports our speed automatically**; if the
  graph blends by it (RT), the legs follow us without any write.

So "can we feed it our speed": yes. The virtual stick's magnitude and `FreeRun` choose the clip family (walk, run,
sprint), `Game_Velocity` already carries the true speed, and the leg rate can be matched to it (2.7).

**Runtime result (P10, LR4): the stick and `FreeRun` pick the family, the speed does not.** With our velocity at 500 and
900 cm/s and the stick at rest the tags stayed `Idl`: Talion glides standing. With the stick at 0.3 or 1.0 the legs ran
`Run` under our 500 cm/s (the game's own root delta 253 / 236 cm/s), and with `FreeRun` `Run Spr` (639). **`Game_Velocity`
is not a variable of this graph** (the name lookup finds none). `Game_SmoothedVelocity` is, and it followed our speed
(498 / 897 cm/s), but it changed no clip. So the leg feed must be the virtual stick (3.1 step 5, as built), plus
`FreeRun` above the run speed. With no walk band, any stick from 0.2 plays the same `Run`; the floor below 0.2 is RT.

### 2.6 The 1:1 answer: replace the root translation

Tuning cannot do it (2.2). Driving velocity directly can, at one place:

- **`LOCO_ROOTDELTA`**: the call `0xab5ea9` in `vf_11` (-> `FUN_00a513a0(body, &delta)`, `delta` = {px, py, pz, qx,
  qy, qz, qw}, pos in Talion's frame, the caller pre-sets `qw` = 1). After the original returns, when the frame is
  ours (3.1), write `delta.pos = R(xf.rot)^-1 · (v·dt + d_follow)` with `y = 0`, and `delta.rot` = the body-yaw rule's
  step (3.2) or the game's. `xf` and `pm` come from the PM slot hook that already wraps this call (`LOCO_PM_ROOT` =
  `PM_ROOTMOTION`, built for `SOMVR_FOLLOW`/`SOMVR_CLIMB`).
- What stays the game's: the locks (`mgr+0x62a`/`+0x629` still zero our translation when an animation owns the body:
  the right behaviour in syncs), attacks (no locks), collision and sliding, steps, slopes, gravity and the park rule
  (y is ours = 0: in root-motion mode with gravity on, `vy` keeps integrating), the proxy's NaN checks.
- **Velocity is instant**: root-motion mode has no memory (2.1 step 4), so the proxy moves at `v` in the same tick
  and stops in the same tick. Velocity is read back next tick from the root (`applied / dt`), so a wall takes the
  into-wall part out of `v` (Gorilla Tag's rule: your velocity is what you really did).
- Fallback if the call-site hook misbehaves: the slot hook alone, post-call (`xf.pos = xf_in.pos + d` when
  `mgr+0x62a` is clear and no attack runs), or the existing extra with `extra = R^-1 d - game delta` (needs the
  game's delta first: the call-site hook is cleaner).
- `FUN_00a513a0` has one more caller, the thunk `FUN_00ab6c80` (pm -> the same delta, for another reader; RT who):
  the replacement is local to `vf_11`, so that reader still sees the animation's delta.

**Runtime result (P10, LR3): the replacement is 1:1.** The probe's `loco` (the same call, `P_ROOTDELTA`) in six windows:
the root's mean speed 0.996-1.012 of the asked 200, 500 and 900 cm/s, **at 90 % from the first tick**, and **0, 0, 0
cm/s the tick after the end or the stick's release**. Every vf_11 call was replaced: the translation and rotation locks
were never set while running (also over two hit recoils), and `dt` came from Talion's object timer in every tick.
Slopes stay the proxy's: the root's vertical speed followed the ground (-80..+70 cm/s) at ratio 1.000. FSM 6 stays 6.
The stock turn clips still play when the stick points behind Talion (163 and 152 deg) and our speed holds through
them; 3.2's body yaw keeps `DeltaYaw` small to avoid them. **Hand-offs:** when our velocity ended with the stick still
held, the game's `Run` went on at 240-262 cm/s (and `FreeRun` at 619-661) in the very next tick, with no start clip: a
hand-off is velocity-continuous only if our speed is the clip's, hence the run band 230 (2.1).

### 2.7 Launches, air control and animation rate

- **Launch (fling, jump, vault arc)**: `FUN_020c6ff0(pm, 0)` (momentum mode), then write `pm+0xe0/+0xe4/+0xe8` = the
  launch velocity (cm/s, world) before the next step, `pc+0x60` = 1 (gravity on). The solver then adds this tick's delta
  (our replaced delta: 0, or a small air-control term) to it and the proxy integrates gravity; the graph sees the lost
  support and declares the fall (FSM 8, which keeps momentum and stores the start velocity). In root-motion mode a
  vertical launch alone also works: with gravity on, only x and z are reset each tick, so `pm+0xe4` = `vy` survives
  **[high code, RT effect]**. Landing: FSM 10 puts root-motion mode back; a launch the graph never called a fall
  needs our own return (`FUN_020c6ff0(pm, 1)` once `pc->vf_14()` reports support and `vy <= 0`).
- **Animation rate** (vault speed, leg match): per tracker `rate = *(float*)(sub+0xa0)` (re-combat-vr.md 2.1,
  recipe B), or the player's whole object timer (`FUN_02824bf0(num, den)`, the SlowMo record's `PlayerTimeScale`) -
  the second slows everything of Talion's, combat timers included, so the tracker rate is the tool **[high code, RT
  effect on the root motion]**.
- **Body-yaw interpolation**: `PlayerYawInterpolateTime` 0.4 s (`pm+0x450`) is a likely part of the stock turning
  lag; in our frames the yaw step is ours (3.2).

**Runtime result (P10, LR5 and LR8).**
- **A launch from the ground does not lift off.** `fling 0 600 0` (momentum mode, `pm+0xe4` = 600, gravity on) had
  `vy` = 0 one tick later, supported, height 0, FSM 6. `fling 0 500 400` lost `vy` the same way. **Its horizontal 4 m/s
  held in momentum mode along the ground**: 3.7 m in 0.98 s, 376 -> 331 cm/s on a slight upslope. The graph never
  called it a fall and never restored root-motion mode; the probe did. So "a vertical launch alone also works" above is
  **wrong for a supported body**. The probe tick's write does reach the step (x and z moved), but support takes `vy`.
  LO-7/LO-8 need a liftoff first: the write inside the PM hook after the solve, a lift above the 40 cm step height, or
  the stock `Jump` first (RT, P11). A launch that fails leaves Talion gliding in momentum mode: put root-motion mode
  back when no liftoff comes within a few ticks.
- **The tracker rate: not answered.** The root stopped at a rock in the very tick the rate 0.7 was set. The game's own
  root delta read x0.79-0.80 for 0.3 s, then the graph stopped him. The game never wrote the rate back (P11 on a clear,
  level heading).

### 2.8 Mounts: the beast's motion

The rider is attached (`AttachToObject`, re-traversal.md 3.4); the beast moves by its own graph from the rider's stick
(`CAIAnimationController::vf_29`, re-traversal.md 3.3) and its own movement, **`CAIMovement::vf_11`** `0x2a781e0`
(slot `0x3ad3ef0`, the NPC vtable shared by every character; it skips while its combat owns movement, then
`FUN_02a76eb0(dt, movement, xf, out)`) **[high, o2.txt]**. The beast's starts and stops are its clips too, with the
same lag. A 1:1 beast would replace its translation the same way, filtered to the mount (`*(P+0x1d8)`'s movement):
**needs RE** of `FUN_02a76eb0` (section 3.10).

### 2.9 Sites

Generated from the binary with `reference/work-firstperson/sitegen.py` (`reference/work-locomotion/sites.txt`);
checked with `python3 -I scripts/check_doc_sites.py reference/bin/linux/ShadowOfMordor docs/design-locomotion.md`.
K_CALL/K_VSLOT = hook candidates in the house style (call-site rel32 or vtable slot); K_ENTRY = functions the mod calls,
proved only. `LOCO_PM_ROOT`, `LOCO_PM_STEP`, `LOCO_ACP`, `LOCO_ANIM_INPUTS`, `LOCO_FSM_GET` and `LOCO_GRAVITY_WISH` are
the same bytes as the existing `PM_ROOTMOTION`, `TRAV_PM_STEP`, `TRAV_ACP`, `MOUNT_INPUTS`, `TRAV_FSM_GET` and
`TRAV_GRAVITY_WISH` (one hook each, shared by the features that need it; `LOCO_GRAVITY_WISH` is the probe's `P_GRAV`).

```c
SITE("LOCO_PM_ROOT", K_VSLOT, 0x34fc838, 0x34fc828, "30 bb ab 00 00 00 00 00 a0 59 ab 00 00 00 00 00 70 5e ab 00 00 00 00 00 40 bb ab 00 00 00 00 00", 0xab5e70, "55 48 89 fd 66 0f ef db 53 48 89 f3 48 83 ec"),
SITE("LOCO_PM_STEP", K_VSLOT, 0x34fc830, 0x34fc828, "30 bb ab 00 00 00 00 00 a0 59 ab 00 00 00 00 00", 0xab59a0, "41 55 31 c9 41 54 49 89 d4 55 48 89 f5 53 48"),
SITE("LOCO_ROOTDELTA", K_CALL, 0xab5ea9, 0xab5e99, "44 24 28 00 00 80 3f 48 c7 44 24 20 00 00 00 00 e8 f2 b4 f9 ff", 0xa513a0, "55 48 89 fd 53 48 89 f3 48 83 ec 18 48 8b 47"),
SITE("LOCO_ACP", K_CALL, 0xab59b5, 0xab59a0, "41 55 31 c9 41 54 49 89 d4 55 48 89 f5 53 48 89 fb 48 83 ec 38 e8 56 80 61 01", 0x20cda10, "55 48 89 e5 41 57 41 56 49 89 d6 41 55 41 54"),
SITE("LOCO_VELMODE", K_ENTRY, 0x20c6ff0, 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40 88", 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40"),
SITE("LOCO_VELMODE_TRANS", K_CALL, 0xa495a3, 0xa49593, "c0 48 8b 43 08 40 0f 94 c6 48 8b b8 c8 02 00 00 e8 48 da 67 01", 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40"),
SITE("LOCO_VELMODE_FALL", K_CALL, 0xabc846, 0xabc836, "c0 48 8b 43 08 40 0f 94 c6 48 8b b8 c8 02 00 00 e8 a5 a7 60 01", 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40"),
SITE("LOCO_ANIM_INPUTS", K_CALL, 0xa8c8d7, 0xa8c8c7, "c6 83 02 0b 00 00 01 48 8b 7b 08 48 85 ff 74 05 e8 f4 ff fa ff", 0xa3c8d0, "80 bf f0 07 00 00 01 0f 84 fb 10 00 00 41 57"),
SITE("LOCO_RAWMOVEMAG", K_CALL, 0xa3c9af, 0xa3c991, "be 58 da 4e 03 48 89 ef e8 f2 13 65 01 f3 0f 10 44 24 20 ba 01 00 00 00 48 89 ef 48 89 c6 e8 fc 36 65 01", 0x20900b0, "48 85 f6 74 0d 48 8b 46 18 48 39 87 58 32 00"),
SITE("LOCO_GAMEVEL", K_CALL, 0xa3cb4b, 0xa3cb3b, "c1 f3 0f 58 c2 f3 0f 10 f0 0f 28 c6 f3 0f 51 c6 e8 60 35 65 01", 0x20900b0, "48 85 f6 74 0d 48 8b 46 18 48 39 87 58 32 00"),
SITE("LOCO_MOVEDIR", K_CALL, 0xa3d232, 0xa3d222, "f0 00 00 00 48 8d 8c 24 98 00 00 00 41 0f b6 f5 e8 19 f1 ff ff", 0xa3c350, "41 57 41 89 f7 be 2d 32 8a 03 41 56 49 89 d6"),
SITE("LOCO_STICK", K_ENTRY, 0xad0af0, 0xad0af0, "55 89 f5 be 03 00 00 00 53 48 89 fb 48 83 ec 18", 0xad0af0, "55 89 f5 be 03 00 00 00 53 48 89 fb 48 83 ec"),
SITE("LOCO_PMREC", K_ENTRY, 0xab8700, 0xab8700, "48 8b 0d d9 cd 16 04 53 48 89 fb 83 b9 e8 02 00", 0xab8700, "48 8b 0d d9 cd 16 04 53 48 89 fb 83 b9 e8 02"),
SITE("LOCO_CAPSULE", K_CALL, 0xab7c52, 0xab7c3a, "f3 0f 7e 64 24 20 44 89 e6 48 89 ef 0f 28 ce f3 0f 10 6c 24 28 0f 28 c7 e8 09 09 61 01", 0x20c8560, "55 48 89 e5 41 57 41 56 41 55 41 54 41 89 f4"),
SITE("LOCO_GRAVITY_WISH", K_VSLOT, 0x34fc810, 0x34fc800, "b0 79 ab 00 00 00 00 00 60 b2 ab 00 00 00 00 00 a0 58 ab 00 00 00 00 00", 0xab58a0, "41 54 55 53 48 8b 5f 50 48 85 db 0f 84 7f 00"),
SITE("LOCO_TELEPORT", K_ENTRY, 0xa368f0, 0xa368f0, "48 8b 87 20 04 00 00 48 85 c0 0f 84 08 01 00 00", 0xa368f0, "48 8b 87 20 04 00 00 48 85 c0 0f 84 08 01 00"),
SITE("LOCO_FSM_GET", K_VSLOT, 0x34f2808, 0x34f2808, "90 57 a5 00 00 00 00 00", 0xa55790, "48 8b 47 f0 0f b6 80 f0 07 00 00 c3 0f 1f 40"),
SITE("LOCO_MOUNT_AIMOVE", K_VSLOT, 0x3ad3ef0, 0x3ad3ee0, "80 5e a7 02 00 00 00 00 80 8c a7 02 00 00 00 00 e0 81 a7 02 00 00 00 00", 0x2a781e0, "41 54 49 89 f4 55 53 48 89 fb 48 83 ec 20 c6"),
```

Notes: `LOCO_ROOTDELTA`'s context holds the end of the caller's `qw = 1.0` store (`movl $0x3f800000,0x28(%rsp)`) and
the zeroing of `rsp+0x20`: the delta record is on the stack at `rsp+0x10..+0x2b`, and `rsi` at the call is its address
(`lea 0x10(%rsp),%rsi` at `0xab5e87`); `rdi` = `body`, and `rbp`/`rbx` hold `pm`/`xf` across the call. `LOCO_PMREC`'s
first bytes are a RIP-relative load (`DAT_04c254e0`): a proof, never a patch. `LOCO_MOUNT_AIMOVE` is the shared NPC slot
(filter by object before acting). `LOCO_TELEPORT` = G13.

### 2.10 What only a boot can answer

| ID | Question | Probe (4.2) |
|---|---|---|
| LR1 | the stock speeds: walk, jog, run, sprint (`FreeRun`), crouch, combat strafe; the stick-to-speed curve; start time to 90 % and stop distance per band | `speed` + the stick sweep |
| LR2 | the console names of the 30 attributes; the player record's values (gravity -20, fall heights, step height, `BaseSpeedMultiplier`) and whether `BaseSpeedMultiplier` changes anything | `pmrec`, then `exec <name> 1.5` + `speed` |
| LR3 | `LOCO_ROOTDELTA` replaced: root speed = asked from the first tick, a stop in one tick, the wall slide, stairs and slopes, the FSM staying 6, what the legs play (TAGS), the turn clips | `loco const`, `loco stick` |
| LR4 | which graph value picks the clip family (stick magnitude or `Game_Velocity`) | `loco const` with the stick at 0.3 / 1.0 and `FreeRun` |
| LR5 | launches: the arc in momentum mode, the FSM path (6 -> 8 -> 10), the landing's root motion, air control | `fling` |
| LR6 | the stock vault over an orc: sync or not, record, duration, root path, the orc's reaction, the 180 deg re-face | `vaultlog` |
| LR7 | does Talion's proxy collide with orcs (does `pmmove` stop at one) | `npcpush` |
| LR8 | a tracker rate on Talion: does the root motion scale with it | `animrate` |
| LR9 | does the graph keep free-run parkour (auto-vault, wall-run) while we own the velocity under `FreeRun` | the free-run drill with `loco stick` |
| LR10 | Shadow Strike's path: a sync with root motion, or a teleport | `vaultlog` during a strike |

## 3. The SoM VR movement design

### 3.1 The motion core: `SOMVR_LOCO=1`

One horizontal velocity `v` (world, m/s) owned by somvr, integrated on the game thread once per game tick in the PM
slot hook, applied through `LOCO_ROOTDELTA` (2.6):

1. **Inputs** of this tick: the left stick (after the 0.15 radial dead zone, head- or hand-relative as
   `SOMVR_MOVE`), the arm pump (3.3), buttons (A, the free-run latch), the combat module's swing events (3.7).
2. **The model** (3.2-3.3): the stick layer sets the walking/running velocity outright; the momentum layer adds what
   the arms built, with its coast and brakes; a launch hands `v` to physics (2.7).
3. **Apply**: `delta.pos = R(xf.rot)^-1 (v·dt + d_follow)`, `y = 0`; `dt` = Talion's own object timer's frame time
   (`GetObjectTimer`, `+0x10` s, re-combat-vr.md 2.2), so distance = speed x his time and the game's slow motion slows
   our motion as it slows his (RT that it equals the solver's `MoveParams+0x0`, `LOCO_PM_STEP`). `d_follow` = the
   room-scale follow's request (it moves to this one path; its rig shift is unchanged).
4. **Read back** next tick: `v_applied = (root - root_last)/dt` (horizontal); `v` loses what the collision took.
5. **Feed the legs**: the virtual stick (G3) = `v`'s direction in the head frame x the band magnitude that plays the
   stock clip nearest `|v|` (from LR1's table: walk / run), `FreeRun` held when the latch is on (3.5) or `|v|` is above
   the stock run speed; optionally the tracker rate `|v| / v_clip` within 0.8-1.25 to stop foot sliding (LO-10).

**When somvr owns the velocity** (all must hold, else the game's delta passes untouched and `v` is kept only as the
hand-off speed of 3.4): FSM 6 or 7; no lock (`mgr+0x62a` clear); no attack owning movement (`FUN_00a85590` false)
except our dash (3.7); not mounted (`P+0x1d8`), not attached (`pm+0x1a6`); no sync (G14), struggle, presentation (G16)
or scripted camera; not climbing (`SOMVR_CLIMB` owns those ticks); alive (`P+0x4c4` = 2); and no traversal transition
(FSM 9 or a traversal TAG). Every hand-over is logged with its cause (the anchor-cause log of design-vr.md 11).

### 3.2 1:1 stick walking and running

- **Speed from the stick, at once**: `|s|` 0..0.5 -> 0..`V_walk`; 0.5..1 -> `V_walk`..`V_run`, where `V_walk` and
  `V_run` are the stock walk and run speeds (LR1), so the legs and the speed agree; `SOMVR_LOCO_SPEED` scales both
  (0.6-1.3, default 1.0). The velocity follows the stick in the same tick: no start clip, no stop slide ("Instantaneous
  accelerations are more comfortable than gradual accelerations" [S54]; Lone Echo's ramp "almost instantaneously"
  [S53]). An option `SOMVR_LOCO_STICK_MS` (0 default, up to 120) adds a short exponential softening for players who
  want it.
- **Direction**: the head (default) or the left hand (`SOMVR_MOVE=hand`), as built (re-xr-input.md 3.3). Moving
  backward or sideways is allowed at full speed; the stock game has no backward run, so the legs turn to the move
  direction (next bullet).
- **Body yaw** (the delta's rotation, our frames only): moving within 100 deg of the head's heading -> Talion faces
  the move direction at up to 720 deg/s (the legs run forward); moving further back or standing -> he faces the head
  (the existing G13 rule); in a fight he faces the engaged orc (the stock combat facing). This bypasses
  `PlayerYawInterpolateTime` and keeps `DeltaYaw` small, so the graph's 180 deg turn clips rarely start.
- **Stairs, slopes, steps**: the proxy's (y = 0 in our delta; gravity and steps do the rest); the view's Y low-pass of
  80 ms (design-vr.md 2) keeps steps from popping.
- **Crouch**: physical or stick (design-vr.md 2); `V_walk`/`V_run` switch to the stock crouch speeds (LR1).
- **Snap and smooth turns** stay the rig's (built); turning never changes `v`'s world direction by itself.

### 3.3 The arm-pump sprint

Stride and Sprint Vector both make speed a product of the arms (1.2). SoM's version adds speed on top of the stick's
run, so a still player never moves by arm noise and a fight never turns into a sprint:

- **Engage**: the stick pushed past 0.9 within 45 deg of forward, out of combat (no engaged orc within 4 m, FSM 6).
- **Pump power** `p`: each hand's velocity relative to the head (the body's own motion removed), its component in the
  head's vertical-forward plane, both hands' magnitudes averaged, RMS over 0.3 s (one stride; no per-stroke surges, so
  the view's acceleration stays smooth).
- **Target** `V_t = V_run + (V_max - V_run)·smoothstep(1.0, 3.0, p)` m/s, `V_max` = the stock sprint speed (LR1;
  `SOMVR_LOCO_SPRINT_SCALE` up to 1.25). Each detected stroke (a hand's swing reversing at the bottom) gives a short
  haptic tick scaled by the speed it added, Sprint Vector's feedback [S1].
- **Momentum**: `v` rises toward `V_t` with a 0.35 s time constant while the arms pump harder than the speed (each
  stroke adds speed, all of it the player's); when the pumping stops it **coasts** down to `V_run` at 1.5 m/s²; the
  stick back at center **brakes** at 12 m/s² down to `V_run`, then stops at once (below `V_run` the stick rules,
  3.2); the stick pulled back brakes at 25 m/s². All four constants are options.
- **Steering at speed**: above `V_run` the direction follows the stick with a lateral limit of 20 m/s² (at 9 m/s, 127
  deg/s), so a hard turn sheds speed the way a runner does; at and below `V_run` the stick's direction is instant.
- **The legs**: above `V_run` the virtual `FreeRun` is held only while the free-run latch is on (3.5); otherwise the
  run clip plays at a tracker rate up to 1.25.
- Option `SOMVR_LOCO_ARMS=stick|stride|free|off`: `stride` counts only strokes made with the grip held from the top of
  the swing to its bottom (Sprint Vector's "grab the air" [S1]; the presses come at the top of the swing in front of the
  chest, away from the sword's shoulder slot and the dagger's hip slot, and a release at the hip with nothing held does
  nothing; with a weapon drawn the mode falls back to `stick`); `free` lets the arms alone run in the head's heading
  (Stride's way [S4]); `off` = the stick click latch's stock sprint only. Arm swinging is not comfortable for everyone
  (Stride "can be pretty dizzying" [S4]; one study found it worse than teleport [S61]), which is why it only adds to the
  stick and can be switched off.

### 3.4 The view: Root anchor, prediction, hand-offs

- **Our motion is exact.** While LOCO owns the velocity the anchor is the Root anchor of HG-1 (design-hla.md 3.1):
  Talion's root + the calibrated eye offset, Y low-passed 80 ms, no animation bob. The anchor is **extrapolated to the
  frame's display time** with `v` (the eye records already carry the display-time lead, re-xr-pipeline.md 7); at 9 m/s a
  20 ms error is 18 cm, so this is not optional at sprint speed.
- **Game-carried motion is filtered** (parkour, vaults, landings, staggers, the stock moves of 3.5-3.9): the anchor
  follows the root with a critically damped follower **with the root's velocity fed forward** (no lag at constant
  speed, no overshoot), horizontal 40 ms, vertical 80 ms (design-vr.md 1's numbers); position only, never rotation.
  Two such stages in a row (each at twice the rate, the same total delay) also smooth the jerk, the derivative Stress
  Level Zero names as the one that matters ("motion sickness dramatically reduces when you get into higher
  derivatives" [S45]); Crytek smooths the camera the same way around corners [S41].
- **Hand-offs** are velocity-continuous: entering a carried move the follower starts with the anchor's current velocity
  (ours), leaving it LOCO starts with the follower's velocity as `v` (the momentum layer then coasts or the stick rules
  per 3.2-3.3). So a vault entered at 6 m/s that the clip runs at 7 m/s eases from 6 to 7; nothing jumps.
- **Frame rate is part of smoothness.** A dropped frame at 9 m/s is an 11 cm stutter that no rotation-only
  reprojection hides. The game must run at the display rate (D15: 80 Hz); the performance runs of design-vr.md 11 add
  a sprint path and count late frames while `|v| > 5 m/s`.

### 3.5 Hybrid traversal

**Hand climbing** is the built prototype `SOMVR_CLIMB=b` (re-xr-pipeline.md 12: holds by ray + `CanWallClimb`, the
body moves by minus the holding hand's motion through the extra move, gravity off by `pc+0x60`, tops by
release/lift/cmd; first boot pending, T10). Additions:

- **Fling**: releasing a hold while the hand moves (body velocity `-v_hand` over the last 60 ms > 1.0 m/s) launches
  Talion with `1.3 x` that velocity (cap 8 m/s), through 2.7's launch: momentum mode, `pm+0xe0..`, gravity on. The
  graph's fall (FSM 8) and landing follow. Climbey's and Gorilla Tag's rule: the release speed is the flight speed
  (Gorilla Tag: a short average of the hand speed x 1.1-1.3, capped at 6.5-8.5 m/s [S22, S25]; Pop: One: "pull down
  rapidly and release" [S35]); the velocity is the mean over the last 4-6 frames, as Gorilla Tag's history.
- **Air catch**: a grab within 1.5 s of a fling or during any fall stops the fall at the hold (the climb's start in
  FSM 8: momentum zeroed, root-motion mode back, gravity off). Needs the climb's stand-down at FSM != 6 relaxed for FSM
  8 and the supported lie of re-traversal.md 2.5 step 3 if the graph keeps playing a fall (RT, T10).
- **Mantle by pushing**: at an edge hold with the hand on the top surface, pushing the hand down lifts the body 1:1
  (the climb's own motion, already up when the hand moves down); when the eyes clear the top by 20 cm, the push
  forward steps onto it. The prototype's `lift` (automatic) stays as the fallback when a push stalls; `cmd` (the stock
  `LedgePullUp`) for authored ledges. Default `SOMVR_CLIMB_TOP=push`.
- **Fling gravity** (option): `pc+0x58` scaled to -12 for our own launches until the landing (the game's -20 makes
  hand flings short: 4 m/s up = 0.4 m); off by default until a boot shows how the graph lands.
- **Grabs that do not fail** (AC Nexus' and Stride's main complaint [S19, S10, S7]: a missed grab is an unexpected
  fall): the prototype's 0.25 s grab window and 35 cm rays stay; add a **grab assist** (a squeeze whose rays miss but
  pass within 10 cm of a climbable hit snaps to it), a **fall catch** (a squeezing hand that comes within 15 cm of a
  climbable surface during a fall grabs it), a **toggle grip** option (AC Nexus ships one [S15]) and the haptic on
  every grab and let-go. Every miss is logged with its nearest climbable distance, so the assist radius is tuned from
  the drills, not guessed.

**The free-run button** = the left stick click latch (built: `FreeRun` held until the stick rests 0.3 s). Today A
also holds `FreeRun` (the stock pad A passthrough, re-xr-input.md 3.3); with LOCO only the latch does, and A is the
jump (3.6). While it is on the game's parkour is armed: running into a low wall vaults, into a climbable wall runs up
it, at an edge A leaps. Each move is a graph transition (FSM 9 or traversal TAGS) and **the game carries Talion**;
somvr hands over with 3.4's velocity-continuous follower: position only, horizontal 40 ms, vertical 80 ms, never
rotation. LOCO resumes at the transition's end with the follower's velocity. What makes it smooth in first person:
no rotation, no bob (the root, not the head bone, except where the root does not follow the body: climbs, then the
`Head` node position with the same filter), velocity-matched entries and exits, the display-rate frame, and the
stock climb's own speed (B of design-vr.md 4: the hands' pull can still feed its stick up). Vignette only if chosen.
This is AC Nexus' split ("leg" moves automatic, "hand" moves physical [S9]) and its Auto-Parkour latch [S15]. As in AC
Nexus, leaps are aimed by looking: the stock leap follows the camera-relative stick, and with G2 the camera is the
head. Option: a **leap marker**, AC Nexus' Parkour Visualizer [S15], at the context action the graph found (the
`FindContextAction` output, RT).

### 3.6 Jumps and drops

- **A outside combat**, free-run off: a LOCO hop if the graph does not jump on flat ground (RT, `Predicate.CanJump`):
  a tap = `vy` for 0.6 m at the live gravity (4.9 m/s at -20), the current `v` kept, through 2.7. **Hold A and release
  it while throwing both arms up** = Stride's jump [S4]: the arms' mean upward speed at the release sets the height,
  0.6 m at 2.5 m/s up to 1.2 m at 4 m/s, and their forward part adds to `v` (capped by `V_max`). With free-run on, A is
  the stock context jump and leap (the game carries it; AC Nexus has no timed jumps for the same reason [S12]).
- **Drops** (stepping off a ledge, a jump, a fling): the fall is physics in momentum mode; the view follows the root
  exactly (ballistic motion is predictable, the Jet Island case) with only the vertical 80 ms filter. The landing is the
  game's (FSM 10, roll or hard landing), its forward roll shown as carried motion; the dip capped at 30 cm
  (design-vr.md 4). Fall damage stays the game's (`FallDamage*` of the player record, LR2).
- **Leaving a forge tower**: SoM has no Leap of Faith or haystack dive (AC Nexus has one, arms out and jump [S11]; no
  traversal move in SoM's binary names one, and the moves are data). A jump from the top is a long fall: 20 m in 1.4 s
  at -20, ~28 m/s at the ground. Falls are the hardest moment in every survey ("Falling is one of the biggest triggers
  in VR" [S37]; Crytek cut its falls short [S41]; AC Nexus darkens them [S14]), and the game's double gravity already
  keeps them short, so it stays: shown as it is, ballistic and started by the player, ending in the game's landing.
  Options: the fall vignette from 3 m (built, off), and a **long-fall fade** (falls over 8 m fade out after 0.5 s and
  back in at the landing).

### 3.7 Combat movement

- **Stick 1:1 in fights** (FSM 7 between recoils): the same model, capped at `V_run`; no arm sprint near an engaged
  orc (3.3). Talion faces the engaged orc.
- **The assisted step** (the user's decision): when the combat module's swing starts toward an orc (its "heading at"
  test: tip speed past 3 m/s, the orc within reach + 0.8 m and within 35 deg of the swing), LOCO adds a step toward
  that orc's chest: `d = clamp(dist - ideal_reach, 0, 0.5 m)`, over 0.18 s with a raised-cosine velocity (peak ~4.4 m/s
  for 0.5 m), along the floor, stopping 0.7 m from the orc; none when already in reach, when blocking, or with the
  stick pointing away. The view moves with it: the swing caused it (L1), it is short and always the same (L6). Logged
  per step; option off.
- **Staggers and recoils** (FSM 7 on every hit on Talion, P8): game-carried, at most 0.5 m, filtered (design-vr.md 3).
- **The dash** (evade sideways or back, A with the stick away from the orcs): the stock roll is pressed for its dodge
  window (`BDW`/`EDW`), and its root motion is replaced by ours: 2.0 m in 0.3 s, raised-cosine velocity, position
  only (LOCO owns the roll's frames explicitly). Option: blink.
- **Toward an orc**: the wraith phase (3.8).

### 3.8 The combat vault: the user's two ideas

**(2) The wraith phase, the reliable one.** A with an orc in front within 3 m (the stock evade's own target test
decides: G3 presses `CombatPrimary04` with the stick at the orc):
- **The body does the stock vault**: the game's collision, landing spot, the orc's reaction (it turns), the hit streak
  rules. Its record, duration and path come from LR6 (G14 logs syncs).
- **The view phases through**: horizontally it follows the root's real path (the vault crosses the orc on a nearly
  straight line), vertically it stays at the start eye height - the flip's 1.5-2 m rise never reaches the view - and
  it returns to the normal eye height over 150 ms at the landing. The game-carried follower of 3.4 smooths the
  take-off and landing. No rotation.
- **Ghost mode, the game's look**: a wraith fog that peaks while the eyes are inside the orc's capsule (+10 cm),
  0.3 s in all (a layer quad like the vignette's, white-blue; the near plane clips the orc's mesh, the fog hides its
  inside), with the stock wraith vision mode's tint where it switches fast enough (`VisionModeToggle`, RT), a wraith
  whoosh (the Shadow Strike's sound event, data, RT) and a haptic sweep from the front hand to the back one.
- **Re-face**: the stock vault ends facing the orc; the view's 180 deg snap (design-vr.md 2's allowed exception) happens
  at the fog's peak, so the player sees the orc again in front. On by default (seated play needs it); standing players
  who would rather turn themselves can switch it off.
- **Fallback** (the stock vault refused, or LR7 shows a way to do it cleanly ourselves): our own phase: a free-space
  check behind the orc (two G21 rays and a ground ray), `Player_Teleport` (G13) at the fog's peak, the view along the
  chord over 0.3 s. RT the AI's response to the jump of its target.

**(1) The grab-vault, the skill move.** When an orc in front within 1.6 m can be vaulted, a **handhold glyph** shows on
its shoulders (overlay, at the `Neck`/`Head` bones + 10 cm, visible while a hand is within 60 cm; RT the bone names):
- **Grab**: a grip within 15 cm of the glyph = a hold pinned to the orc's moving bone (haptic). Talion's body then moves
  by minus the hand's motion relative to that bone, the climb's own rule (3.5) on a moving hold.
- **v1, on the stock vault** (M): the hand's pull past 1.5 m/s triggers the stock vault (as in (2)); its animation rate
  follows the hand's speed, 0.6-1.5 x (tracker rate, 2.7; LR8), so a hard shove vaults fast and a slow one floats. The
  view follows the real arc (Root/Head position, no rotation) because the player is driving it; the phase fog is off.
- **v2, the true fling** (L): the release velocity (`-v_hand` x 1.3) launches Talion ballistically (2.7) over the orc;
  assists: the direction bent up to 25 deg toward the landing point behind the orc, fling gravity -12 until landing,
  the orc's capsule out of the way (LR7: if characters collide, the body is teleported to the landing point when the
  computed arc would hit the capsule, while the view flies the arc), a 1.2 s timeout.
- **Why it can be "nailed"**: the hold, the pinning and the 1:1 body motion are the climb's (built and tested
  offline), the launch is 2.7's, and v1 keeps the game's collision, so the only new risk is feel; v2 waits for the
  climb fling and LR7.

**Recommendation: ship (2) first, then (1) v1, then v2.** (2) costs a presentation on a stock move (M, no physics of
ours, no AI risk) and it is "true to the game" in the user's words. (1) is the more physical move and the user's
"ideal"; v1 delivers its feel (your hand sets the speed) on the same stock vault, v2 is the full Gorilla Tag version.
Both stay: A = phase, grip on the glyph = grab-vault.

**Runtime result (P10, LR6 and LR7).**
- **LR6, the vault: not seen.** The evade came with the orc 3.2 m away, backing off, and Talion in a patrol recoil.
  It was **`PC_Dash`**: an attack with root motion, FSM 7, no sync. It covered 5.9 m in 0.95 s, peaking at ~12 m/s
  (20.7 cm a tick) and slowing to ~0.7 m/s. Talion's yaw was set once at the start, with no re-face. So outside the
  vault's range A is a long dash. The wraith phase's view rule (follow the root's path) fits it too, but its speed
  needs the follower's easing (LO-3). The vault itself is P11's (the orc 1.5-2.5 m ahead, Talion idle).
- **LR7, Talion against an orc: no stop, a slide.** Walking at 250 cm/s at an `AI_Infantry` 2.3 m ahead, the solver's
  velocity turned up to ~57 deg near it, and the root slowed to 136 cm/s at the closest point (56.5 cm between the roots;
  Talion's radius is 35). He passed it at 0.81 of the asked speed on average. The one-shot push passed too, and the orc
  moved 33 cm. **So characters deflect each other, they do not block**: v2's flung body would slide around the orc's
  capsule, not stop at it. The arc must still be kept clear of it (or the teleport fallback kept) for a clean
  landing.

### 3.9 Shadow Strike and the wraith teleport

- The stock Shadow Strike (bow aimed + X; the aimed teleport's range is `AimingTeleportRange`, 2.2) carries Talion to
  the orc; its path is LR10.
- **Default: the wraith dash**: the view travels the straight line to the arrival point at constant speed with 60 ms
  eased ends, position only, under a wraith tunnel (the vignette quad's ring in white-blue at 40 %): fast, smooth and
  started by the player, the Jet Island case. If the game teleports, the dash runs over 0.3 s while the body is already
  there; if it moves him, the follower carries it. The player was aiming at the orc, so no turn is needed. Option:
  blink.
- **Runtime result (P10, LR10): not seen.** `wmhold PC_WraithRanged` raised the aim (bow drawn, wraith vision,
  reticle), but the game camera pointed at a rock: "No Target Selected", and the strike command was never polled. The
  aim follows the camera, not Talion's facing. P11 turns the camera to the orc first.
- **LR10 answered (build 12, re-bow.md 14.2, runs 064438-072205)**: both. `PC_WraithDash_KD_Start` lunges him 1.6 m in
  0.2 s (root motion, up to 14 m/s), then the knockdown sync `PC_WraithDash_KD_Bip_Standing_F` teleports him 7.7-7.8
  m to the orc in one frame and plays ~3.6 s. The wraith dash (`SOMVR_BOW_STRIKE=dash`) holds the view through the
  lunge and dashes from there to the arrival over 0.3 s; the body moments hold it at the arrival through the sync.

### 3.10 Mounts

- **Seat mode** (design-vr.md 5; SH-2): the saddle (T9) or the root, Y 120 ms (caragor) / 200 ms (graug), XZ 30 ms,
  no pitch or roll; car-style yaw (D13). The beast's bounding never reaches the view.
- **Steering, stick (default)**: the left stick, head-relative for free (re-traversal.md 3.3/3.5, T8).
- **Hand steering (option)**: a caragor has no reins; the rider holds its neck ruff. Both grips on two hold points at
  the beast's neck; the hands' mean lateral offset turns it (handlebar-style), written as `DeltaYaw`/the camera quat
  through `MOUNT_AI_INPUT` (re-traversal.md 3.5); speed from the stick or by leaning forward (the head's forward offset
  from the seat, 10-30 cm). A pounce (`MNT_Jump`) by a shove of both hands.
- **The beast's lag**: its starts and stops are its clips (2.8); Seat-mode smoothing makes them smooth, not 1:1. The
  1:1 beast (its translation replaced like Talion's) is LO-14, after RE of `FUN_02a76eb0`.
- Mounting and dismounting: first person with the follower, a blink as option.

### 3.11 Comfort options that remain (all on the VR settings panel, HG-4)

| Option | Default | Choices |
|---|---|---|
| vignette (built, one strength) | off | per trigger later: walk, sprint, carried moves, falls, mounts |
| snap blink (built) | off | 0-200 ms |
| stick response | 0 ms | 0-120 ms |
| speed scale | 1.0 | 0.6-1.3 of stock |
| arm sprint | stick + arms | arms free, off |
| brakes | 12 / 25 m/s² | 6-40 |
| carried moves (parkour, landings) | smooth | smooth + vignette, blink |
| phase, dash, Shadow Strike | dash/phase | blink |
| re-face after the phase | on | off |
| assisted step | on | off |
| fling gravity | game (-20) | light (-12) |
| long falls | as they are | fade over 8 m |
| climb grip | hold | toggle; grab assist on/off |
| leap marker | off | on |
| mounts | stick | hand steering; graug in stage view |

AC Nexus' setup survey with presets (Immersive ... Most Comfortable) [S15] is the model for the panel's first page:
one question, then the presets fill these rows; this doc's defaults are its "Immersive" (all aids off), which fits
the user's answer of section 0.

### 3.12 Controller map changes (design-vr.md 9)

| Control | On foot | Fighting | Climbing |
|---|---|---|---|
| Left stick | move, 1:1 (speed by deflection) | move, capped at run | climb fallback (built) |
| Left stick click | free-run latch (the game's parkour) | - | - |
| Arms (no button) | pump with the stick forward = sprint | - | pull = climb, release while pulling = fling |
| A | hop (or the stock context jump with free-run on) | toward an orc: wraith phase; away: dash | - |
| Grips | weapons (built); on a wall = hold | on a vault glyph = grab-vault | hold; air catch |

## 4. The build plan

### 4.1 Ranked items

| # | Item | Size | Depends on | Probes |
|---|---|---|---|---|
| **LO-0** | Probe run **P10 (locomotion)**: the words of 4.2 and the drill | S + 1 boot | the probe build; P9's arena (`spawn` works) | all LR |
| **LO-1** | Root anchor (HG-1) with display-time extrapolation by the body's velocity | M | the root read, run 3's eye calibration | `speed` |
| **LO-2** | **LOCO core** (`SOMVR_LOCO=1`): `LOCO_ROOTDELTA`, the ownership rules, 1:1 stick, body yaw, the leg feed, the follow merged into one delta, `LOCO` logs; pure model + tests | M | LO-1 for the view; LR1, LR3 | `loco`, `speed` |
| **LO-3** | Game-carried follower and velocity-continuous hand-offs (parkour, landings, staggers) | M | LO-1; T1/T2 (re-traversal.md drill) | `trav`, `speed` |
| **LO-4** | Arm-pump sprint and the free-run latch hand-off | M | LO-2, LO-3; LR1 (`V_run`, `V_max`), LR9 | free-run drill |
| **LO-5** | Combat movement: the assisted step, the dash, fight caps | S-M | LO-2; the combat module (built) | `loco`, combat drill |
| **LO-6** | **Wraith phase** (idea 2) on the stock vault: path view, fog quad, sound, haptic, re-face | M | LO-3; LR6 | `vaultlog` |
| **LO-7** | Climb fling, air catch, push mantle (extends `SOMVR_CLIMB`) | M | T10 (the climb's first boot); LR5 | `fling`, climb drill |
| **LO-8** | Hop and arm-throw jump; the fall view; the long-fall fade option | S-M | LO-7's launch; LR5 | `fling` |
| **LO-9** | Shadow Strike wraith dash | S | LO-3; LR10 | `vaultlog` |
| **LO-10** | Leg match: tracker rate to the speed | S-M | LO-2; LR8 | `animrate` |
| **LO-11** | Panel entries for 3.11 | S | HG-4 | - |
| **LO-12** | **Grab-vault** (idea 1): v1 on the stock vault (M), v2 the true fling (L) | M + L | LO-6, LO-7; LR7, LR8; the orc bone names | `npcpush`, `animrate` |
| **LO-13** | Seat mode and hand steering | L | HG-1, T8/T9 | `mount` |
| **LO-14** | 1:1 beast: the mount's translation replaced | XL | RE of `FUN_02a76eb0` (`LOCO_MOUNT_AIMOVE`) | - |

Order: LO-0 -> LO-1 -> LO-2 -> LO-3 -> LO-4 -> LO-5 -> LO-6 -> LO-7 -> LO-8 -> LO-9 -> LO-10 -> LO-11 -> LO-12 ->
LO-13 -> LO-14. LO-0 to LO-2 make the user's speed principle playable (walk, run, stop, all 1:1); LO-3 and LO-4 make
it fast; LO-5 and LO-6 make fights move; the rest is traversal depth and the skill move.

**Status (2026-10-20, offline; nothing launched; branch `worktree-agent-a1c5a48b5a1e7a348`, re-xr-pipeline.md 17):**
- **LO-1 built** as `SOMVR_ANCHOR=root` (needs `SOMVR_BASE=head`; default `head` = the raw Head node as before): the
  eyes at the root + 76.6 cm (run 3: 180.6 cm above `World_Ground`), horizontally exact, Y a 3rd-order low-pass with
  80 ms ramp lag (exact in falls and our climb; the landing's dip capped at 30 cm), LO-2's hand-offs eased
  velocity- and acceleration-continuously (a 5 m/s jump over ~17 cm), extrapolated to the display time by the body's
  velocity x each frame's timing error. **Deviation:** no constant lead by default (`SOMVR_ANCHOR_LEAD_MS`, 0): with
  1:1 starts and stops a lead makes every stop step back by v x lead (16 cm at 5 m/s, 33 ms); the timing error part
  is what removes the judder at speed (offline: 0.6 cm vs 5+ cm on a 60-on-72 Hz cadence). Not in LO-1: Head mode and
  the velocity-fed follower for game-carried moves (LO-3).
- **LO-2 built** as `SOMVR_LOCO=1` (needs `SOMVR_INPUT=1`): `LOCO_ROOTDELTA` (byte-checked K_CALL `0xab5ea9`, its own
  stub, plus 10 read-only proofs; `check_sites.py` 131/131 with main's P9 sites), the ownership rules of 3.1 (logged per
  change with cause), the stick's velocity in the very tick (the bands 150 / 500 / 800 cm/s are **placeholders until
  LR1**), body yaw (3.2's rule at 720 deg/s; the game's in fights, aiming and under the rotation lock), the leg feed,
  the follow in the same delta (one path per tick), the climb owning its motion, the bow's aim cap, fights capped at the
  run band. Tests: `test_loco` 454, selftest `loco_tests` on a stand-in vf_11 with the real call site. Open for a boot:
  LR1 (the speeds), LR3/LR4 (the replaced delta, the legs, the turn clips, `dt` from Talion's timer), the owners in play
  (17.7). P10's `P_ROOTDELTA` (63b6cff) hooks the same call: `mod/probe/build.sh --check` names that collision until its
  `EXCLUSIVE` list gets `0xab5ea9` (runtime-probes.md P10 asks for it; not added here: mod/probe untouched).
- **LO-0 ran** as P10 (2026-10-20 15:46, runtime-probes.md P10 results). Answered: LR2, LR3 and LR4. Partly: LR1 (the
  Run and sprint speeds, no walk band) and LR7 (a slide, no stop). Open: LR5 (no liftoff), LR6, LR8, LR9 and LR10. A
  patrol fought Talion from mid-run on. **LO-2 tuned (P10)**: the run band 230, the latch's 640 (re-xr-pipeline.md
  17.8); the walk band, the stick's floor and the default top speed are open there.

### 4.2 Proposed probe words (P10)

Read-only (R) and active (A, under spike1's guard on the dev save):

- **`speed SECS`** (R): per tick the root's horizontal and vertical speed, `pm+0xe0..+0xe8`, `pm+0x1a2`, FSM, the
  graph's `RawMoveMag`/`Game_Velocity` read back (`FUN_0208dd90` + the value getter `FUN_0208de50`), the game's root
  delta when `LOCO_ROOTDELTA` is in, TAGS on change. Answers LR1.
- **`pmrec`** (R): the 30 values of the player record `pm+0x448` through the descriptors at `0x4c25520` (2.2's
  reader), `pm+0x32c/+0x450/+0x454`, `pc+0x58`; `repo->vf_08` (re-m1m2.md 2) on `PlayerMovement.<attr>`,
  `Client.PlayerMovement.<attr>` and `<attr>` for the cvar names. Answers LR2.
- **`loco const VX VZ SECS` | `loco stick SECS` | `loco off`** (A): the `LOCO_ROOTDELTA` replacement with a constant
  world velocity (cm/s) or the probe's held stick mapped 1:1 at `V` = 500 cm/s. Root speed vs asked from the first
  tick, the stop, a wall, stairs, FSM/TAGS. Answers LR3, LR4, LR9.
- **`fling VX VY VZ`** (A): 2.7's launch; the arc, FSM path, landing. Answers LR5.
- **`animrate R SECS`** (A): Talion's main tracker rate (recipe B) during a run: root speed x R? Answers LR8.
- **`vaultlog SECS`** (R): G14 start/stop with record, duration and root path at 60 Hz; the orc's yaw. Answers LR6,
  LR10.
- **`npcpush`** (A): `spawn AI_Infantry 150`, then `pmmove 0 0 120` toward it: does the root stop at the orc. Answers
  LR7.
- **The drill** (`scripts/cmds/probe-P10.cmd`, P9's arena, after `despawn all`): stick sweep `move 0 0.2 3000` ...
  `move 0 1 3000` (5 steps) with `speed 4` around each; `FreeRun` held + `move 0 1 5000`; reversal `move 0 -1 2000`;
  stop from each band; `pmrec`; `loco const 0 500 3`, `loco const 0 900 3`, `loco stick 5` with `move 0 0.4 2000`;
  `fling 0 600 0`, `fling 400 500 0`; `spawn AI_Infantry 300` then `cmd CombatPrimary04` with `move` at it under
  `vaultlog 3`; `npcpush`; `animrate 0.7 3` during a run; the free-run run into the arena's low wall with `loco stick`.

## 5. Proposed changes to design-vr.md (for its owner; not applied here)

1. **Section 0, house rules**: add the user's speed principle verbatim (section 0 here) and rules L1-L6 (1.1); name
   this doc as the locomotion standard, beside design-hla.md.
2. **Section 0, the user's decisions**: add the three decisions of 2026-10-20 ~09:30 (combat "Assisted physical",
   "VR-native finishers", traversal "Hybrid") with pointers to sections 3, 3 and 4.
3. **Hook register**: add **G23** `LOCO_ROOTDELTA` (call `0xab5ea9` -> `FUN_00a513a0`, replace the root
   translation), **G24** `LOCO_VELMODE` (`FUN_020c6ff0` + `pm+0xe0..+0xe8`, launches), **G25** `LOCO_ANIM_INPUTS`
   (post `FUN_00a3c8d0` at `0xa8c8d7`, graph variables through `FUN_020900b0`).
4. **Section 1, the anchor**: Root mode is exact while LOCO owns the velocity and extrapolated to the display time with
   it; game-carried motion goes through the velocity-fed-forward follower (40/80 ms); hand-offs are
   velocity-continuous (3.4).
5. **Section 2, "Where movement comes from"**: replace "All locomotion stays the game's own ... The mod only supplies
   stick input" by: somvr owns Talion's ground velocity in FSM 6/7 through G23; the game keeps collision, steps, slopes,
   gravity, locks and the legs (fed by the virtual stick), and carries the moves the player asked for.
6. **Section 2, "Comfort shaping", "Speed and acceleration"**: delete the "VR profile lowers the acceleration time ...
   RT: the attribute names" bullet. The 30 attributes are known (2.2 here); none is a speed or acceleration; speed and
   ramps are animation data; 1:1 comes from G23. Section 8's "acceleration profile" row becomes "stick response
   (0 ms)" and "brakes (12 / 25 m/s²)".
7. **Section 2, "Sprinting and free-running"**: the stick click latch is the free-run button (the game's parkour);
   the arm pump adds sprint speed (3.3); A no longer holds `FreeRun` (re-xr-input.md 3.3's passthrough): outside combat
   it is the hop or arm-throw jump, or the stock context jump with free-run on (3.6).
8. **Section 3, "Evade (A)"**: toward an orc = the wraith phase on the stock vault (3.8 (2)) instead of the blink;
   sideways/back = the dash (stock roll window, our motion); the blink only as an option. Add the grab-vault (3.8 (1))
   as the skill move after M6.
9. **Section 3, new "The assisted step"** (the user's decision): 3.7's rule.
10. **Section 3, Shadow Strike**: the wraith dash by default, blink optional (3.9).
11. **Section 4**: A (hand over hand, `SOMVR_CLIMB=b`) is now the primary climb (the user's "Hybrid"); B (gesture-fed
    stock climb) remains under the free-run latch; add the fling, the air catch, the push mantle and the fling gravity
    option (3.5); D4 is answered.
12. **Section 4, vaults/falls/towers**: carried moves are position-only with the follower and no vignette by default;
    the tower drop as a real fall at the game's gravity, with the fall vignette and the long-fall fade as options
    (3.6); the climb's grab assist, fall catch and toggle grip (3.5).
13. **Section 5**: hand steering as an option; the beast's own lag and the 1:1 beast as later RE (3.10, LO-14).
14. **Section 8**: vignette defaults all **off** (the user's principle; the aids as optional extras); add 3.11's
    rows; D9 follows.
15. **Section 9**: the controller rows of 3.12 (arm pump, A in combat = phase/dash, grip on the glyph = grab-vault,
    release while pulling = fling).
16. **Section 12, milestones**: M2 gets LO-0..LO-3 (1:1 walking, running, stopping, carried moves); M4 gets LO-5 and
    LO-6; M6 gets LO-4, LO-7, LO-8; LO-12 after M6; M7 gets LO-13.
17. **Section 13, risks**: add "the graph fights the replaced root motion (turn clips, foot sliding; RT LR3/LR4)",
    "late frames at speed show as translation judder (L3; the 80 Hz target matters more)" and "NPC collision for our
    own flings (LR7)".
18. **Section 14**: D4 answered by the user (hybrid). D5 and D9: the user's speed answer and vault idea (2) point to
    the phase and dashes instead of the blink and to aids off; this doc proposes closing both that way (blink and
    vignettes stay options). A new decision only if the user wants the arm pump as the only sprint
    (`SOMVR_LOCO_ARMS=free`) or the grab-vault before the phase.

## Sources

Web pages read 2026-10-20 as data (nothing in them was acted on). "Excerpt only" = seen in a search excerpt, the page
did not open.

- S1 Survios postmortem, Game Developer: https://www.gamedeveloper.com/design/breaking-the-vr-speed-barrier-sprint-vector-s-fluid-locomotion-technology
- S2 Road to VR, Sprint Vector review: https://www.roadtovr.com/sprint-vector-review/ (hands-on: https://www.roadtovr.com/hands-on-survios-sprint-vector-fluid-locomotion-system/)
- S3 UploadVR, GDC 2017 announcement: https://www.uploadvr.com/gdc-2017-raw-data-studio-survious-announces-sprint-vector-blazing-fast-vr/
- S4 UploadVR, Stride review: https://www.uploadvr.com/stride-review/
- S5 Virtual Uncle, Joy Way on the controls: https://virtualuncle.com/2021/08/11/joyway-explains-why-it-isnt-changing-strides-controls/
- S6 Destructoid, Joy Way interview: https://www.destructoid.com/the-vr-parkour-game-stride-isnt-for-the-faint-of-heart/
- S7 Worthplaying, Stride review: https://worthplaying.com/article/2021/11/2/reviews/129189-pc-vr-review-stride/
- S8 CGMagazine, Stride: Fates review: https://www.cgmagonline.com/review/game/stride-fates-vr-review/
- S9 Road to VR, AC Nexus preview: https://www.roadtovr.com/assassins-creed-nexus-vr-preview-parkour-quest-3/
- S10 PCGamesN, AC Nexus review: https://www.pcgamesn.com/assassins-creed-nexus/review
- S11 Ubisoft News, the full assassin experience: https://news.ubisoft.com/en-us/article/6JvZqDzOKwmGG5cMny6bMb/how-assassins-creed-nexus-vr-brings-the-full-assassin-experience-to-virtual-reality
- S12 Ubisoft News, out now (Votypka): https://news.ubisoft.com/en-us/article/5xSmgL0t8f4hQ1y9putRWX/assassins-creed-nexus-vr-out-now
- S13 GamingBible, AC Nexus review: https://www.gamingbible.com/reviews/assassins-creed-nexus-vr-review-784239-20231114
- S14 Ubisoft News, accessibility spotlight: https://news.ubisoft.com/en-us/article/631psH9E2m8W2jNvn9nROv/assassins-creed-nexus-vr-accessibility-spotlight
- S15 Ubisoft, comfort feature manual: https://www.ubisoft.com/en-us/game/assassins-creed/news/4Yb9RvSj2ouXX2ZvSiebie/assassins-creed-nexus-accessibility-comfort-feature-manual
- S16 Ubisoft News, master assassins (Votypka): https://news.ubisoft.com/en-us/article/14FPy9oly95ZNJ3w4tTTg9/how-assassins-creed-nexus-vr-lets-players-become-master-assassins
- S17 TheSixthAxis, AC Nexus review: https://www.thesixthaxis.com/2023/11/22/assassins-creed-nexus-vr-review/
- S18 Gaming Nexus, AC Nexus: https://www.gamingnexus.com/Article/13957/Assassins-Creed-Nexus/
- S19 UploadVR, AC Nexus review: https://www.uploadvr.com/assassins-creed-nexus-vr-review/
- S20 Mixed, AC Nexus review: https://mixed-news.com/en/assassins-creed-nexus-vr-review/
- S21 Glitched, AC Nexus review: https://www.glitched.online/assassins-creed-nexus-vr-review/
- S22 Another Axiom, GorillaLocomotion `Player.cs` (MIT): https://github.com/Another-Axiom/GorillaLocomotion/blob/main/Player.cs
- S23 Road to VR, Gorilla Tag's first weeks (Smith): https://roadtovr.com/unassuming-one-man-vr-project-gorilla-tag-sees-42k-players-in-first-two-weeks/
- S24 Voices of VR 1489 (Smith): https://voicesofvr.com/1489-gorilla-tag-design-philosophy-embodied-gameplay-deep-dive-with-kerestell-smith/
- S25 GorillaTagMapProject (community SDK values): https://github.com/legoandmars/GorillaTagMapProject
- S26 Jet Island on Steam: https://store.steampowered.com/app/587220/Jet_Island/
- S27 Steam discussion, Jet Island techniques: https://steamcommunity.com/app/587220/discussions/0/1733213724905931728/
- S28 VR Fitness Insider, developer interview (excerpt only): https://www.vrfitnessinsider.com/interview-vr-game-developer-reveals-fitness-huge-vr-gaming/
- S29 Steam discussion, Jet Island sickness: https://steamcommunity.com/app/587220/discussions/0/3414307257853129440/
- S30 VR Today Magazine: https://vrtodaymagazine.com/7-underrated-vr-games-you-need-to-try/
- S31 Twinfinite, Windlands review: https://twinfinite.net/reviews/windlands-review/
- S32 VR Geeks, Windlands 2 review: https://vr-geeks.com/windlands-2-review/
- S33 Operation Rainfall, Windlands interview (Hibbins): https://operationrainfall.com/2016/11/02/interview-windlands-vr/
- S34 PowerUp Gaming, Psytec interview (Pittom): https://powerupgaming.co.uk/2016/10/01/windlands-interview-psytec-games-on-creating-a-spider-man-inspired-vr-experience/
- S35 Meta blog, Population: One tips: https://www.meta.com/blog/oculus-tips-tricks-population-one/
- S36 Road to VR, Population: One hands-on: https://roadtovr.com/hands-on-population-one-vr-fortnite/
- S37 VentureBeat, BigBox VR on vertical combat (Brown, Lee): https://venturebeat.com/business/how-bigbox-vr-designed-its-population-one-vertical-combat-for-virtual-reality
- S38 Climbey on Steam (store text excerpt only; player reviews): https://store.steampowered.com/app/520010/Climbey/
- S39 UploadVR, The Climb 2 review: https://www.uploadvr.com/the-climb-2-review-vr/
- S40 Road to VR, The Climb 2 review: https://roadtovr.com/the-climb-2-review-oculus-quest-2/
- S41 GamesBeat, Crytek on The Climb (Bowman): https://gamesbeat.com/cryteks-the-climb-lets-you-scale-cliffs-in-virtual-reality-that-you-wont-attempt-in-real-life/
- S42 Road to VR, The Climb with Touch: https://roadtovr.com/crytek-the-climb-oculus-touch-feels-like-an-entirely-new-experience/
- S43 Brandon Laatsch on the body: https://x.com/BrandonJLa/status/2009741997897044046
- S44 Boneworks on Wikipedia: https://en.wikipedia.org/wiki/Boneworks
- S45 Brandon Laatsch on jerk and jounce (tweet text via a search index): https://twitter.com/BrandonJLa/status/1187070017164103680
- S46 Brandon Laatsch on refunds: https://x.com/brandonjla/status/1581688755932450817
- S47 Road to VR, Boneworks overview: https://roadtovr.com/boneworks-physics-driven-mechanics-gameplay-overview/
- S48 bit-tech, Boneworks review: https://bit-tech.net/reviews/gaming/pc/boneworks-review/1/
- S49 ResetEra thread: https://www.resetera.com/threads/highly-anticipated-vr-title-boneworks-is-a-collection-of-vr-sins.158449/page-11
- S50 Jacob Copenhaver, GDC 2017 "It's All in the Hands": https://www.gdcvault.com/play/1024446/It-s-All-in-the (slides: https://media.gdcvault.com/gdc2017/Presentations/Copenhaver_Jacob_ItsAllIn.pdf)
- S51 Road to VR, Lone Echo II review: https://roadtovr.com/lone-echo-2-review-oculus-rift-quest/
- S52 VentureBeat, Echo Arena hands-on (excerpt only): https://venturebeat.com/business/echo-arena-hands-on-gameplay-of-zero-gravity-multiplayer-in-vr
- S53 Road to VR, Lone Echo hands-on: https://roadtovr.com/hands-on-lone-echo-nails-zero-gravity-in-vr-with-oculus-touch/
- S54 Oculus Best Practices (archived copy): https://www.readkong.com/page/oculus-best-practices-version-2599757
- S55 Meta, locomotion comfort and usability: https://developers.meta.com/vr/resources/locomotion-comfort-usability/
- S56 Bonato et al. 2008, Presence 17:283-292, doi 10.1162/pres.17.3.283 (abstract: https://jov.arvojournals.org/article.aspx?articleid=2133336)
- S57 Widdowson et al. 2021, Human Factors, https://doi.org/10.1177/0018720819881254
- S58 So, Lo and Ho 2001, Human Factors 43:452: https://www.researchgate.net/publication/11496344_Effects_of_Navigation_Speed_on_Motion_Sickness_Caused_by_an_Immersive_Virtual_Environment
- S59 Dong, Yoshida and Stoffregen 2011: https://eric.ed.gov/?id=EJ934190
- S60 Coomer et al. 2018, ACM SAP: https://www.researchgate.net/publication/326760529
- S61 Loup and Loup-Escande 2019, as cited in https://www.frontiersin.org/journals/virtual-reality/articles/10.3389/frvir.2023.1027552/full
