# Shadow of Mordor VR - the design

A full, first-class VR port of Middle-earth: Shadow of Mordor: first person, 6DoF, motion controls and a new physical
combat. The target is Feral's native Linux Vulkan build (BuildID 87ec5d6f..., addresses = runtime addresses, non-PIE),
modded in-process. The runtime is WiVRn on a Quest 2, the GPU an RX 6800 XT, the OS Nobara.

Written 2026-10-18 from the RE docs (`docs/re-gameplay.md`, `re-view-render.md`, `re-stereo.md`, `re-strings-map.md`,
`re-setup.md`), `NOTES.md` (spike1 run 1) and the Hand of Fate 2 VR project's test patterns. Nothing was launched for it.
Every mechanic names the game function or hook it rests on, or says **needs RE: <what>** (static RE still to do) or
**RT: <what>** (static RE is done: the answer is data or a measurement; the probe that gets it is named). Section 14
holds the **decisions for the user**. Reconciled 2026-10-19 with the later RE docs (`re-firstperson.md`, `re-input.md`,
`re-animation.md`, `re-combat.md`, `re-ui-effects.md`) and `re-m1m2.md` (the M1/M2 gates), whose section 1 lists
every marker and what became of it. Updated the night of 2026-10-19 with runs 3-4 and that evening's RE
(`re-combat-vr.md`, `re-combat-items.md`, `re-hud.md`, `re-resolution.md`, `re-xr-input.md`, `re-traversal.md`), and
on 2026-10-20 with runs 5-8, the first headset look, P7, P8 and the night's RE (`re-arms-ik.md`, `re-ui-pointer.md`,
`re-launcher.md`, `re-combat-spawn.md`, `re-bow.md`). Later that day the two design reviews were folded in:
`design-hla.md` (the HL:A standard: its 16 proposed changes, the HG/SH/NH gap list) and `design-locomotion.md`
(movement: its 18 proposed changes, the LO build plan), with the user's decisions of ~09:30 and P9's results. Where
they disagreed, the user's decisions won, then HL:A, then the locomotion doc.

### Status and roadmap (2026-10-20, late morning)

- **Design direction (user, 2026-10-20)** (verbatim in design-locomotion.md 0; the house rules in section 0):
  - **The standard is Half-Life: Alyx** for VR feel: the hands and grabbing, the menus, comfort, where enemies appear
    and how they warn, loading in 3D; every setting adjustable in-game ("It should be adjustable in-game").
    `design-hla.md` holds this design to it.
  - **Combat: "assisted physical"**: real swings decide hits; orcs take turns from the front with telegraphs; parry by
    putting the sword in the way; a short assisted step toward the orc you swing at (section 3). D8 is answered.
  - **Finishers, executions, brutalize, cutscenes: "VR-native"**: you do the motion, the orc plays its animation, the
    view is never dragged, cutscenes play around you (sections 1, 3, 6). D1, D2 and D3 follow from it.
  - **Traversal: "hybrid"**: hand climbing (`SOMVR_CLIMB`) plus a free-run button that lets the game's parkour carry
    you in first person (sections 2, 4). D4 is answered.
  - **Speed: comfort = smoothness + 1:1 input, not low speed** (Jet Island, Sprint Vector): velocity is the player's,
    momentum is native, starts and stops are the player's and instant; rules L1-L6 (section 0, design-locomotion.md
    1.1). Comfort aids (vignette, blink) are options, **off by default**. D9 follows.
  - **Combat vaults: the user's two ideas**: the wraith phase-dodge first (A at an orc), the hand-driven grab-vault as
    the skill move (section 3). D5 is answered.
- **Proven at runtime** (one boot at a time, headless unless noted; the real save byte-identical every time; NOTES.md
  has every run, runtime-probes.md the probe results):
  - Run 1 (10-18): hooks H1-H4 and the camera offset. Run 2: layout A fails (one half blown out, a seam), so
    **layout B**.
  - Run 3: layout B with two full 1080p eyes at a steady 60 fps; eye R reuses eye L's exposure; G22 `exec` in-process
    (`Teleport` exact); the one-shot `pm+0x428` move in Talion's frame, clipped by the game's collision; the stock
    bindings (KBM mode); `Head` = node 28; the arm chains; the live player update `FUN_00a40fe0` (not `FUN_00a416a0`);
    the audio listener at Talion's root.
  - Run 4: the real `mod/somvr` in the game, layout B with fake views: 1320 eye copies, 0 misses, 60 fps.
  - Runs 5/5b (Monado, simulated HMD): the layer under a real runtime; the menus as a quad; layout B stereo in the
    world; the dumps pass `tools/review_stereo.py`.
  - The first headset look (WiVRn, 10-19 23:12): below.
  - P7 (10-20 00:11): `Combat_DealHit` from our code (damage, hit reaction, blood, hit sound, hit counter); hit-stop by
    the orc's own timer, swapped and restored cleanly (120 ms delays the reaction, 600 ms loses it); `Combat+0xcc` =
    the impact within a frame; `SampleNode` is model space; the tall 3D size through `RES_3D_DESC` (1832x1920 with
    the UI at 1920x1080, no letterbox); the dagger to `L_Weapon` and back; brutalize is a sync; `Faction.Dump` to `C:`.
  - Run 6 (Monado): the prediction fix (display-time error 0), the overscan, tall 2015x2217 eyes in the world.
  - P8 (02:21): `aggro` puts orcs and archers on Talion within 1 s, `minhealth 20` holds; the parry window opens at the
    attack start and closes 16-70 ms before the impact; a 120 ms hit-stop holds the attack's animation (its impact
    133 ms late); the menu mouse hovers and clicks (KBM mode, 1:1); our phase-2 arm pose reaches the screen exactly,
    and the engine runs no arm IK in idle or melee; the sword tip 100.7 cm along +Y; arrows 28 m/s, no homing,
    `+0x228` removes one; gravity off latches (`pc+0x60 = 1` undoes it).
  - Run 7 (Monado): the tall size only while stereo runs (the menus 16:9 again), pad glyphs as icons, the HUD quad
    (world-locked, 1.2 x 0.675 m, 1.5 m ahead), the audio listener at the head, display-time error 0.
  - Run 8 (Monado, VR build 1 headless, 04:53): first person at Talion's Head node (root +73 cm, `SOMVR_BASE=head`);
    the 12 cm near plane (`SOMVR_HIDE_HEAD=near`) hides most of the head (left: a few hair/hood strands, pale shapes
    on the right, probably the quiver's arrows); without controllers the arms and the blade stay idle, as designed.
  - Run 9 (Monado, VR build 1b headless, 08:17): the fallback, comfort and the bone collapse initialise, display-time
    error 0; `bone+near` leaves hood and hair fragments floating in front of the eyes, worse than `near` alone, so
    VR build 1b uses `near`.
  - P9 (09:46): test orcs from our code (a shield defender, a berserker, the Nemesis captain Ghâm by
    `Faction.SetTarget` row, column); **the shield blocks every frontal hit by itself**, ours too (`block=1`, no
    damage); `AI_Defender` and `AI_Berserker` have **no AIBlockTable** (no roll to win); our `incoming`
    (`FUN_029ac890`) made a defender raise its block once (1 of 3, the call outside its own attack); a forced notify
    gives an infantry no block or dodge; the counter by `CombatKBM09` in KBM mode; **the bow aim is 124**
    `CombatSecondary03` (held 0.2 s); 125 is the stealth mode (`PC_Stealth`), not the fire, which is an attack input.
  - P10 (15:46): **our velocity instead of the root motion is 1:1**: `LOCO_ROOTDELTA` replaced moves Talion at the
    asked 200-900 cm/s from the first tick and stops him in one, with no locks and `dt` from his own timer. The clips
    follow the stick and `FreeRun`, not the speed. The stock speeds: one `Run` band at 222-232 cm/s for any stick from
    0.2 (90 % after 0.82 s, stopped after 0.6 s / ~57 cm), `FreeRun` 636-641 cm/s; no walk band (KBM mode). None of
    the 30 `PlayerMovement` attributes is a console variable, and `BaseSpeedMultiplier` has no reader. Talion slides
    around orcs rather than stopping. A launch from the ground does not lift off (`vy` lost while supported). The
    vault, the tracker rate, parkour under loco and Shadow Strike were not seen (a patrol fought Talion from mid-run).
  - Not seen at runtime: an infantry's or berserker's block/dodge AI against our swing; the aim ray and the fire
    command (P9: not run); the head as a piece or an attached model (17 pieces, 7 attached models, all weapons). **The
    head is no piece and no attachment**: hiding it uses the near plane (section 1).
- **Built in main, tested offline, waiting for a boot** (re-xr-pipeline.md 6-15; nothing of it seen with
  controllers):
  - VR build 1b (`scripts/cmds/somvr-vr1.cmd`, `logs/build-vr1b`, the launcher's VR mode): layout B stereo on the XR
    views with the tall eye size and the overscan, `SOMVR_AUDIO=head`, `SOMVR_BASE=head`, `SOMVR_INPUT=1` (the
    controls, snap/smooth turn, haptics, the game's rumble), `SOMVR_POINTER=1`, `SOMVR_HUD=quad`, `SOMVR_ARMS=1` (our
    two-bone IK in G11), `SOMVR_COMBAT=1` with `SOMVR_BLADE=controller` (hits through G8, `SOMVR_HITSTOP_MS`),
    `SOMVR_HIDE_HEAD=near`, `SOMVR_FALLBACK=auto` with the layer's fade `SOMVR_XR_FADE`, `SOMVR_VIGNETTE=0.4` (0 under
    the user's principle: a launcher change, scripts/som-vr.sh line 181 and the cmd);
  - built, not in VR build 1b, off by default: `SOMVR_COMBAT_REACT=1|force` (the orcs' block/dodge roll for a swing,
    6.3.1; P9: defenders and berserkers have no table to roll), `SOMVR_FOLLOW=1` (room-scale body follow),
    `SOMVR_HIDE_HEAD=bone`, `SOMVR_SNAP_BLINK_MS`, `SOMVR_SEATED` with `SOMVR_HEIGHT_CM` (11), `SOMVR_CLIMB=b` (hand
    over hand, 12; first boot T10), `SOMVR_BOW=1` (the VR bow on the game's aim mode, 13), `SOMVR_XR_ENV=on|world`
    (the calm Mordor-dusk sky behind mono frames, world mode the last stereo frame dimmed, 14), `SOMVR_VRMENU=1` with
    `SOMVR_SETTINGS=FILE` (the HL:A-style wrist settings panel, live, saved; the launcher passes
    `~/.local/share/somvr/settings.conf`, 15);
  - the user launcher `scripts/som-vr.sh` (VR build 1b's env, `som-vr.conf`, `SkipOSWarnings=1` by the user's OK,
    `--no-feral-options`, `--make-release`; no release made yet), `PLAY.md` (DRAFT), probe P9's words (f96e10d).
  - **What the built pieces do not do yet** (the honest list, design-hla.md 2): the head base is raw (no Root anchor,
    no low-pass: the walk bob, lunges and staggers reach the view); the fallback fades to the game camera and then
    shows its pans and cuts; G2 (the gameplay camera = the head) is not built; the stock melee can leave Urfael
    sheathed while the player swings; there is no defence (blocks, parries, telegraphs); the movement is the stock
    root motion (0.6 s to the first run, stop slides); the bow's fire default (`SOMVR_BOW_FIRE_CMD=125`) is P9's
    stealth entry, not a fire (the real fire command: P10).
- **In a worktree**: the feature-interaction review (`worktree-agent-a5f54f43b2a77b946`: phantom swings, pause, the
  climb, the third-person head, the comfort jump, follow vs arms, the layer's composition order and fail-soft),
  merging main.
- **The first user headset look** (10-19 23:12, WiVRn, `logs/boot-check-20261019-231255`): the menus as a floating
  screen, layout B stereo in the world at 60 fps, the dump passed review. The user: **"That's insanely good!!!"**, with
  black bars at the sides when moving the head: WiVRn reprojects our 60 fps images, which had no margin and were
  located 33.4 ms early. Fixed since (re-xr-pipeline.md 7): the prediction lead (run 6: error 0), `SOMVR_OVERSCAN`
  0.1 with the eye size scaled to match, the menu quad's guard columns. No headset has seen the fix yet.
- **Next steps** (one boot at a time, HoF2's OK first):
  1. The user's headset session with VR build 1b (WiVRn, the user's OK): the controls, the arms, the controller
     blade, the pointer, the HUD quad, first person with `near`, the fallback's fade, no black bars; `SOMVR_VRMENU=1`
     added once a headless boot has shown the panel (the user's "adjustable in-game").
  2. Then the merged plan below, from the top. P9's leftovers (the aim ray and the fire, a berserker's dodge, a
     captain after his showdown, the counter in pad mode) ride in P10.
  3. Static RE that can run beside it: "stand in the scene" (NH-4: stereo on `FUN_009edba0`, culling), the knockdown
     tags, `Combat.RagdollReaction` (section 3), the achievements block in test mode (D16), `AISpawnMgr` (SH-9), the
     beast's movement `FUN_02a76eb0` (LO-14).
- **The merged build plan**: design-hla.md 3 (HG must-have, SH should-have, NH nice-to-have), design-locomotion.md 4.1
  (LO) and the work in flight, ranked by the user's decisions, then HL:A, then the locomotion doc. Sizes as in
  design-hla.md: S = up to one helper-day offline, M = 2-3 days, L = about a week, XL = more or research first; each
  item also needs a headless boot and the headset. Every new switch is off by default until its boot passes.

| # | Item | From | Switch / status | Size |
|---|---|---|---|---|
| 1 | The feature-interaction review's fixes into main | review | worktree `agent-a5f54f43`, merging main | S |
| 2 | Probe P10: the stock speeds, the 30 `PlayerMovement` values, `LOCO_ROOTDELTA` replaced, launches, the stock vault's path, NPC collision, the tracker rate (LR1-LR10), plus P9's leftovers | LO-0 | planned (`probe-P10.cmd`) | S + 1 boot |
| 3 | **Root anchor**: no animation motion in the view; Y low-pass 80 ms; extrapolated to the display time; the anchor-cause log and assert | HG-1 = LO-1 | planned (today `SOMVR_BASE=head`, raw) | M |
| 4 | **1:1 motion core**: somvr owns the ground velocity (G23); instant stick walk and run, body yaw, the leg feed, the follow in the same delta | LO-2 | planned, `SOMVR_LOCO=1` | M |
| 5 | **First person through the body moments**: combat camera overrides off, the kill cam first person, the frozen anchor for syncs and struggles, knockdowns dim, our climb exempt, death first person; the fallback only for unknown states, to the window | HG-2 | planned (the detector built: `SOMVR_FALLBACK=auto`) | L |
| 6 | **Game-carried follower**: free-run parkour, landings, staggers position-only; velocity-continuous hand-offs (the free-run half of "hybrid") | LO-3 | planned | M |
| 7 | **Body slots**: draw and sheathe Urfael at the right shoulder (`FUN_023116f0`), Acharn at the hip, quick draw, slot ticks | HG-7 | built offline, `SOMVR_SLOTS=1` (re-xr-pipeline.md 21; no blade from an empty hand) | M |
| 8 | **Defence and telegraphs**: G4 attack starts, block bars, ground arrows, block/parry/dodge judged at the impact, the parry recoil and open window, haptics, bone capsules; the orcs' reactions after P9 (`incoming` straight for archetypes without a table) | HG-3 | built, off by default (`SOMVR_DEFENCE`), re-xr-pipeline.md 22; no boot yet | XL |
| 9 | **Front-first enemies**: G2 (the gameplay camera = the head), the 110 deg / 1 s rule, edge glow and side haptic, the VR fight-circle tuning, damage x0.5 | HG-5 | planned (G2 not built) | L |
| 10 | **The assisted step**, the dash, the fight speed cap | LO-5 | built, off by default (`SOMVR_STEP`, `SOMVR_DASH`), re-xr-pipeline.md 23; no boot yet (the fight cap is LO-2's run band) | S-M |
| 11 | **The wraith phase** on the stock vault (the user's idea 2) | LO-6 | built, off by default (`SOMVR_PHASE`), re-xr-pipeline.md 23; no boot yet (P10's LR6/LR7 first) | M |
| 12 | **VR-native finishers**: executions by your own swing (rune glow, slow-mo, `forceKill`); stealth kills by your own stab; drains, brands, interrogations in first person with a latched hand | SH-1 + SH-8 | built, off by default (`SOMVR_FINISH`), re-xr-pipeline.md 24; no boot yet | M + M |
| 13 | Hands that stop at surfaces: the arm wall clamp (`CL_RAYCAST`), the blade clamp, clank and void, the ghost hand, the reach ghost, the IK spring | HG-6 | planned | M |
| 14 | The VR menu, the rest: turning off, snap 22.5, move direction, the main hand swapping the controls, finger poke, per-trigger vignette, the LO rows, presets | HG-4, LO-11, SH-7 | `SOMVR_VRMENU=1` built (9ed2205) | S-M |
| 15 | Climb additions: fling, air catch, push mantle, grab assist, fall catch, toggle grip, the fling-gravity option | LO-7 | after T10 (`SOMVR_CLIMB=b` built) | M |
| 16 | Nothing floats: health, focus and arrows on the left wrist, arrows on the bow, the streak on the runes, markers and the counter icon in stereo; the quad smaller and lower meanwhile | HG-8 | planned (`SOMVR_HUD=quad` built) | L |
| 17 | Never a void, the rest: the layer's keep-alive frames in load stalls, the tip panel, the progress light | HG-9 | `SOMVR_XR_ENV` built (the sky, world mode) | M |
| 18 | Crouch by ducking, the seated reach assist, height re-measure | HG-11 | planned | S |
| 19 | The bow's runtime check (aim ray, fire), the arrow count on the bow; Shadow Strike as the wraith dash | bow, LO-9 | `SOMVR_BOW=1` built; the dash planned | S + S |
| 20 | Arm-pump sprint and the free-run latch hand-off | LO-4 | planned | M |
| 21 | Hop, arm-throw jump, the fall view, the long-fall fade option | LO-8 | planned | S-M |
| 22 | Cinematics around you: the window in the calm space with scripted level cameras ignored (interim), captains in the world; then "stand in the scene" | SH-4 + NH-4 | planned (RE first for NH-4) | M-L + XL |
| 23 | Hands that conform: finger curl from the Touch sensors, per-object poses | HG-10 | planned | M |
| 24 | Leg match: the tracker rate to the speed | LO-10 | planned | S-M |
| 25 | The grab-vault (the user's idea 1): v1 on the stock vault, v2 the true fling | LO-12 | planned | M + L |
| 26 | Seat mode, car-style yaw, hand steering | LO-13 = SH-2 | planned | L |
| 27 | Blink and Shift on the ground (options, off) | SH-3 | planned (D17) | M-L |
| 28 | Touch-to-use, the quick wheel, arrivals behind, the last place as the backdrop | SH-5, SH-6, SH-9, SH-10 | planned | S-M .. L |
| 29 | Later: the 1:1 beast, gravity pull, the dagger throw, artifacts in the hand, blade cuts, one-handed mode, the Nemesis diorama, the open-side rule, speaker subtitles | LO-14, NH-1..3, NH-5..9 | planned | L-XL |

- **Open user decisions**: D1-D5, D8, D9 and D19 are answered by the decisions above (section 14); D6, D7, D10-D18 and
  D20 stay at their defaults (D7 now picks only the first-boot default: every angle is set in the headset); WiVRn
  auto-launching HoF2 on connect.

## 0. Ground rules and architecture

### What the user asked for, and the house rules this design keeps

- "I will not compromise on it being a full first-class VR port. We will have to change the combat completely."
- **Zero testing burden**: the user plays at milestones only; everything else is tested automatically (section 11).
- **The user's decisions of 2026-10-20 ~09:30** (verbatim in design-locomotion.md 0): combat "assisted physical"
  (section 3); "VR-native finishers": you do the motion, the orc plays its animation, the view is never dragged,
  cutscenes play around you (sections 1, 3, 6); traversal "hybrid": hand climbing plus a free-run button (sections 2,
  4); the speed answer (below); the two vault ideas, the wraith phase first and the grab-vault as the skill move
  (section 3).
- **Never move the player without their input** (the user's hard rule since Hand of Fate 2). Motion that the player
  started (the stick, an arm pump, a fling, a free-run move, a mount's run) is allowed and follows rules L1-L6 below.
  Motion the game starts on its own (paired animations, knockdowns, cinematic cameras) never drives the view.
- **The speed principle** (the user, 2026-10-20): "I don't get motion sick in Jet Island, even though I am grappling
  around, using rocket thrusters, etc., and moving very very fast. The reason is because of how smooth and fluid it
  is. In Sprint Vector, I'm racing at super speeds. But again, its super smooth, and I get no motion sickness. So I
  don't think speed alone is the issue. It's a combination of smoothness and 1-to-1 input-to-movement. I have complete
  control over my movement, so I'm never fighting the movement controls, having unexpected delays in gaining speed or
  stopping, etc. Acceleration and stopping are native to the movement mechanics." As testable rules
  (design-locomotion.md 1.1, the movement standard):
  - **L1** velocity is the player's: every change of the view's velocity comes from an input made at that moment and
    starts within a frame; nothing ramps up or slides to a stop on its own;
  - **L2** momentum is native: speed the player built persists and decays by learnable rules (friction, drag,
    gravity); stops are the player's;
  - **L3** smooth: no position steps, the view's velocity continuous across hand-offs, extrapolated to the display
    time, the game at the display rate;
  - **L4** no rotation of the view from the game: no pitch, roll or yaw by animations, cameras or moves;
  - **L5** the game carries only what the player asked for (free-run parkour, a stock vault under the phase, falls),
    position-only and smoothed, entered and left at the player's speed;
  - **L6** assists are small, visible and the same every time (the assisted step, a fling's aim bend, the vault
    glyph).
  Comfort aids (vignette, blink, fades) are options for players who want them, **off by default**.
- **The standard: Half-Life: Alyx** (the user, 2026-10-20: "I want HL:A to be the standard we hold this to"; every
  setting "adjustable in-game"). `docs/design-hla.md` holds this design to it with six rules: (1) the view belongs to
  the player; when the game must take the body, the player keeps the view; (2) hands are believable first and exact
  second: they stop at surfaces, close around what they hold, and the game quietly helps them reach; (3) information
  lives in the world, nothing floats in front of the face; (4) threats are fair: they come from where you can see them
  and warn before they hit, sound covers the rest; (5) there is never a void: menus, pauses, loads and deaths happen in
  a calm 3D place; (6) comfort is the player's choice, made in-game and applied at once (its defaults follow the speed
  principle above, not HL:A's "gentlest"). HL:A has little fast movement (teleport by default, no jumping), so for
  movement the references are design-locomotion.md's (Jet Island, Sprint Vector, Lone Echo, Gorilla Tag, AC Nexus);
  for combat Until You Fall and Blade & Sorcery. The Hand of Fate 2 lesson: when the user loves a reference, **stay
  close to it**; "simplified and inspired by" designs underdelivered there.
- 1:1 scale, real floor height: the game's centimetres are real centimetres (HoF2's "too small" lesson).
- Saves: the user's `GameData.sav` is never written. Test runs use the fail-closed save guard (spike1, `SOMVR_SAVEDIR`,
  WBOnlineService slots 219-222). Never patch the Steam layer.

### Units and conversions

- World: cm, +Y up; view space +x right, +y up, +z forward (left-handed); quats (x, y, z, w).
- OpenXR (stage space, metres, right-handed, -z forward) to the game: `pos = 100 * (x, y, -z)`,
  `quat = (-qx, -qy, qz, qw)` (re-stereo.md section 5; check against one logged frame in M1).
- Per-eye FOV: `tx = (tanR - tanL)/2`, `cx = (tanR + tanL)/2`, likewise `ty`, `cy`; the game side gets
  `fovX = 2 atan(tx)`, `fovY = 2 atan(ty)` (H1), the render side gets the offset `(cx, cy)` (H3).

### The two binaries we add

| Part | What it does |
|---|---|
| `libsomvr.so` (LD_PRELOAD, grows out of `mod/spike1`) | All engine hooks below, the VR logic (anchor, body, combat, input), the save guard, the test command channel. Game thread for gameplay, render thread for projection. |
| `VK_LAYER_SOMVR_openxr` (`mod/vklayer`) | OpenXR instance/session, frame timing, the eye images into the XR swapchain, quad layers (HUD, menus, theatre), **the controller input** (the action set, bindings, `xrSyncActions` and haptics, on the present thread; somvr only locates the grip spaces on the game thread at the frame's display time; ABI: a second object `somvr_input`, re-xr-input.md 1-2), and **the overlay renderer**: a small Vulkan pass that draws our own VR-only elements (block bars, ground arrows, markers, laser, wrist tiles) into each eye image before submission. |

The overlay renderer matters because we cannot easily add meshes to the game's renderer. Everything the game already
draws (Talion's body, sword, dagger, bow, the orcs, effects) stays the game's; everything new and VR-only is ours, drawn
on top. Without the game's depth buffer the overlays are always in front, which suits indicators. Depth-tested overlays
use the G-buffer depth target, `rt+0x200`'s image (D32S8 at the 3D size); in layout B eye R's passes overwrite it
before the present, so it needs an H8-style copy per eye (re-resolution.md 3.2-3.3; RT: the layer's pass log, 3.4).

### Hook register (referred to by ID in every section)

Render side, from re-stereo.md section 6 (H1-H4 exist in spike1): **H1** view post-hook (call 0x9ee7d4 -> `FUN_00a5a820`),
**H2** BeginPerspectiveViewport (vtable slot 0x335cb60 -> 0x705500), **H3** projection (call 0x72c749 -> `FUN_0072bb20`),
**H4** eye loop (call 0x9ef51a -> `FUN_009ee170` twice), **H5** BeginFrame `CRendererHandle::vf_29` 0x703300 (xrWaitFrame,
xrLocateViews), **H6** frustum corners (call 0x811e58), **H7** eye-R exposure time, **H8** eye-L copy (layout B), **H9**
present-composite tap (call 0x742ffd), **H10** the Vulkan layer, **H11** HUD pass skipped for eye 0, eye 1's kept (calls
0x9ef1e0, 0x9ef20a; re-hud.md 1.4: the pass then runs once, after the last view, as in the flat game).

Gameplay side (new in this design):

| ID | Site | Use |
|---|---|---|
| G1 | `CGameClientShell::vf_02` 0x9eabd0, entry and exit | the mod's per-frame game-thread tick: poses, anchor, IK targets, combat, input |
| G2 | post `FUN_00a5d780` (camera pose, from `FUN_00a35340`); every-frame player hooks go on the live update `FUN_00a40fe0` (call `0x9ebcae`; F1 = `FP_CAMPOST_B` `0xa413c5`), run 3, re-firstperson.md 1.3 | write the **gameplay camera object** (`cam+0x338`) = the HMD centre pose, so aiming, prompts, the stock leap and the fight circle's `Camera` score follow the head. **Not built** (the bow overrides its own ray instead, `BOW_FA_CAMXF`); HG-5 needs it |
| G3 | the binding-layer virtual pad (re-xr-input.md 3.1): I0 post `CWBInput::vf_04` (slot `0x34798c0`, the input tick), I1-I3 overrides `vf_22`/`vf_23`/`vf_25` for the held command records, ON/OFF events through `FUN_00aab660`/`FUN_00aab5d0`; game thread; held axes proven in run 3. Not `CSDLController::vf_07` or SDL (they need a device) | the virtual gamepad: every game command we want comes from here, fed by the layer's controller snapshot |
| G4 | pre `Combat_StartAttack` `FUN_026eb380` | veto the player's own melee swings; log every orc attack start (the telegraph) |
| G5 | post `SharedCombat_ParseWindowKeys` `FUN_022667f0` | per-attack parry (`BPW/EPW`), dodge (`BDW/EDW`), hit windows |
| G6 | pre `Combat_ApplyDamageKey_RangeAngle` `FUN_026e49a0` | an orc's blow on the player: block, parry, dodge or out of range cancels it |
| G7 | pre `Combat_OnColliderHit` `FUN_026d5530` | the same for collider attacks |
| G8 | call `Combat_DealHit` `FUN_026d4ff0` (fallback `ServerCombat_ApplyAttackDamage` `FUN_0281a200`) | **our blade hits**, through the game's own reactions, deaths, XP and Nemesis bookkeeping |
| G9 | `AICombatMgr_ScoreAttacker` `FUN_028cd7a0`, `AICombatMgr_OnStartAttack` `FUN_028d0ef0` | the orcs in the fight (until the object iteration gives all characters in range: the `list` command's loop, re-m1m2.md 8, RT) |
| G10 | `Difficulty_Recompute` `FUN_02114ec0`, grant `FUN_028cd4b0`, lockout `FUN_028ccc90` | VR turn-taking and attack delay |
| G11 | post-hook `CSimpleModelTrackerNode::vf_03` (vtable slot `0x38df398`, the player's tracker only): edit the model-space arm chain after the animation jobs, before physics, skinning and attachments (re-animation.md 3.2, 8) | arm IK: Talion's real hands, sword and dagger on the controllers |
| G12 | `IEngineModel` (`*0x46a9230`) vtbl+0x20 GetPiece, +0x38 SetPieceHidden; +0x80 GetNode, +0xb8 GetNodeTransform | read any bone; hide pieces (none of them is the head: P7/P8, section 1) |
| G13 | `Player_Teleport` `FUN_00a368f0` (same position, new rotation) | turn Talion's body in place |
| G14 | sync start: call `0x26eb93c` -> `FUN_028081a0(sync, record, target)`; stop: `FUN_028060a0` and `FUN_02805fa0`; struggles: `PlayerCombat+0x8e0`/`+0x8d8` (re-m1m2.md 6) | freeze the anchor, choose first person or stage view |
| G15 | `SlowMO.Set/Start/Stop` (SlowMoMgr), `Combat.Enable.SlowMo` 0x4c44318 | short slow motion |
| G16 | cinematic camera manager `clientShell+0xb0`, active camera at `+0x20`; `PlayerCameraPresentation` = `P+0x308`, in a presentation while `pcp+0x90 != 0` (anim flag `InPresentation`), start `FUN_00a6d740` (re-m1m2.md 7) | theatre and window |
| G17 | `FUN_00a10f00` (interface state 3 = in game), `FUN_00a10d30` (UI hides the world) | menu screen on/off |
| G18 | no target swap: the engine's UI surface (`dev+0x1b8`, its own texture once `InterfaceWidth/Height` differ from the eye size) is copied at the present composite `0x742ffd` into our `uiTex` and published like the eye images (re-hud.md 1.4); swapping `clientShell+0x80dc0` is the fallback | HUD and menus into our texture |
| G19 | `NAV_EACH` call `0xec4f83` (per marker in `FUN_00ec4f10`, list `0x46b9200`) + world position `FUN_00ebc700` (re-hud.md 3) | world markers: capture their world positions and icons |
| G20 | post-hooks of the two `FUN_00aaa450` rumble calls in `FUN_00a5ad40` (R1 `0xa5af58`, R2 `0xa5af97`) -> the haptic requests in `somvr_input` (re-xr-input.md 3.7) | the game's rumble to the controllers, plus our own haptics |
| G21 | `Physics_RayCast` `FUN_008de5e0` (IEnginePhysics `*0x466c758` vtbl+0x200) | walls for the blade, head-in-wall, ground under the hands |
| G22 | `CWBEngine::vf_32` `0x8bd8b0` = RunConsoleString(eng, "cmd args; cmd2"), `eng = *(void**)0x466afa8`, on the game thread; server commands need a loaded world (re-m1m2.md 2) | run `Teleport`, `SlowMO.*`, `Character.*`, `Faction.Dump` from the mod (tests and features) |
| G23 | `LOCO_ROOTDELTA`: call `0xab5ea9` in `PlayerMovement::vf_11` -> `FUN_00a513a0(body, &delta)` (design-locomotion.md 2.6; inside the PM slot hook somvr already has, `PM_ROOTMOTION`) | **replace the root translation** with our velocity x Talion's frame time: somvr owns his ground speed (`SOMVR_LOCO`); the locks, attacks, collision, steps and gravity stay the game's |
| G24 | `LOCO_VELMODE`: `FUN_020c6ff0(pm, 0)` (momentum mode) + `pm+0xe0..+0xe8` (cm/s, world), `pc+0x60` = 1 (design-locomotion.md 2.7) | launches: the climb fling, the hop and arm-throw jump, the grab-vault's v2 |
| G25 | `LOCO_ANIM_INPUTS`: post `FUN_00a3c8d0` at `0xa8c8d7`; graph variables through `FUN_020900b0(value, mgr, FUN_0208dd90(mgr, "name"), 1)` (design-locomotion.md 2.5) | feed the legs: `RawMoveMag` and the clip family from our speed, if the virtual stick alone does not (RT LR4) |

Comfort pokes stay on in every VR mode: shake off `0x46b72d2` = 1, player motion blur off `0x46b6651` = 0, DoF off
`0x46b6652` = 0, camera roll `cam+0x4dc` = 0 each frame; plus `Render.Setting.MotionBlurQuality 1` (FXAA, no blur);
to add (HG-2): `Combat.EnableCameraOverride` `0x4c44298` = 0, the per-attack camera overrides off (re-strings-map.md;
RT whether it also stops the kill cam `FinalBlow`).

## 1. Player body and camera

### The view: the HMD drives it, the body follows

The view is never the head bone's animated pose: that would bob, tilt and roll with every animation, which is the
fastest way to make a player sick. The **real head is Talion's head**: the eye poses come from OpenXR, placed in the
world by a mod-owned **anchor** (a floor point `A` plus a yaw `psi`):

`eye_world = A + R(psi) * eye_stage` (per eye, from `xrLocateViews` in H5, applied in H1).

The anchor has two modes:

| Mode | When | `A` follows | Why |
|---|---|---|---|
| **Root** | walking, standing, fighting, stealth, and the game-carried moves (free-run parkour, vaults, landings, staggers) | Talion's root (the server `CPlayerObj` transform, `PlayerBody+0x1b8`), by its per-frame displacement minus our own body-follow correction; Y low-passed (80 ms); exact and extrapolated to the display time while somvr owns the velocity (G23), through the follower below when the game carries him | stable, room-scale; walk bob never reaches the view |
| **Head** | climbs and other moves where the root does not follow the body (RT: re-traversal.md T1/T2) | the head bone's **position** (G12 GetNodeTransform of `Head`, node 28, the parent of `SocketHead`: run 3, runtime-probes A2; somvr's `SOMVR_BASE=head` reads it, run 8: the view at the node, root +73 cm), offset by the calibrated eye-to-head-bone vector, low-passed (Y 80 ms, XZ 40 ms); never its rotation | the view stays where Talion's head really is while the game moves the body |

This is "first person from the head bone" where it matters: in climbs the eyes ride the head bone's position;
everywhere else they ride the stable root under the real head. Pitch and roll always come from the HMD only. Mounts
have their own Seat mode (section 5); paired actions freeze the anchor (below).

**Status: neither mode is built.** VR build 1b runs `SOMVR_BASE=head` raw (no low-pass, no Root mode): the walk bob,
attack lunges and staggers reach the view, the HoF2 "moved without input" complaint. The Root anchor is the first fix
(HG-1 = LO-1, plan item 3).

**Prediction at speed** (L3): while somvr owns the velocity, the Root anchor is extrapolated to the frame's display time
with that velocity (the eye records already carry the display-time lead, re-xr-pipeline.md 7); at 9 m/s a 20 ms error
is 18 cm, so this is not optional.

**Game-carried motion** (free-run parkour, vaults, landings, staggers; design-locomotion.md 3.4): the anchor follows the
root (the head bone in Head mode) through a critically damped follower **with the root's velocity fed forward** (no lag
at constant speed, no overshoot), XZ 40 ms, Y 80 ms, two stages in a row (each twice as fast, the same total delay) so
the jerk is smooth too; position only, never rotation. **Hand-offs are velocity-continuous**: a carried move starts
with the anchor's current velocity, and our motion resumes with the follower's (a vault entered at 6 m/s that the clip
runs at 7 m/s eases from 6 to 7).

The gameplay camera object (`cam+0x338`) is written to the HMD centre pose every frame (G2), and the rendered eyes are
set in H1. So the game's camera-relative stick movement, the bow's centre-screen aim, interaction prompts and the
fight circle's `Camera` score (re-gameplay.md section 6) all follow the head. Motion blur stays off regardless: both
eyes would reproject against the one previous camera (`cam+0x3c0`), the smear of spike1 run 1.

**Audio**: in the world the game's Wwise listener sits at Talion's root, not at the camera (run 3); `SOMVR_AUDIO=head`
gives listener 0 the head pose, mapped like the eyes (re-xr-pipeline.md 6.1; run 7: it follows the head).

**Calibration** (once per session, at the first world load, and after every recentre with the system button): the
player's standing eye height from the HMD vs Talion's eye height (run 3 `nodes`: the eye nodes' midpoint 180.6 cm
above `World_Ground`, 11 cm ahead of and 4 cm above `Head`; `Head` 72.5 cm above the root, which sits at pelvis height;
runtime-probes P1 results). The scale stays 1:1; only `A`'s height offset is set so the floor is the floor.

### Room-scale: the body follows the head

When the player walks in the room, the HMD leaves Talion's body. The body chases it:
1. Every frame (G1) compute the horizontal offset `d` = HMD (world) - Talion's root.
2. If `|d|` > 8 cm, move Talion toward the head **with the game's own collision**: a direct input in
   `PlayerMovement` (`Player+0x2c8`): there is no velocity input (root motion), but `vf_11` composes an unused one-shot
   extra delta, `pm+0x428` pos in Talion's frame / `+0x434` quat / `+0x444` pending (re-m1m2.md 3). Run 3: the move
   lands exactly in Talion's frame and the collision slides it along a wall (runtime-probes P5 K2), so no G21 ray is
   needed for the body. Built as `SOMVR_FOLLOW=1` (at most 30 cm a frame, re-xr-pipeline.md 6.1). With LOCO the
   follow's request joins our velocity in the one replaced root delta (G23, `d_follow`; LO-2). Fallback: `d` on the
   virtual left stick (G3).
3. That correction does not move `A` (the root's displacement is subtracted from the anchor update), so the view does
   not move while the body catches up.
4. If the head pushes into a wall (G21 ray from the neck to the HMD hits within the head radius, 12 cm), fade the view
   to black over 0.2 s until it comes back; the body never follows into the wall.

Turning in place (snap/smooth, section 2) changes `psi`; Talion's body yaw follows the head yaw lazily: when the
difference exceeds 50 deg standing (immediately when moving or fighting), turn him in place with `Player_Teleport` (G13:
same position, new rotation, eased over 0.25 s) **[med: check that Teleport keeps the movement state]**.

### The body you see when you look down

- Talion's own model is drawn as usual. His locomotion animation drives the legs and torso; the torso yaw follows the
  head yaw as above.
- **Arms**: inverse kinematics to the controllers after the animation, before skinning (G11). The sword (Urfael) and
  dagger (Acharn) are attachments on the hand sockets, so moving the hands moves the real weapons: no new meshes. The
  IK target is the controller pose through a stiff spring (at most 25-50 ms lag, the Until You Fall weight feel) and is
  stopped by walls: a G21 ray from the shoulder to the target (the climb's `CL_RAYCAST` with the Talion filter, built
  in re-xr-pipeline.md 12.3); a hit pulls the target back to the hit minus 3 cm, so the hand and the blade do not pass
  through stone (HG-6; the spring and the clamp are not built).
  **Where to write the arm pose** (was this section's biggest RE item; answered in re-animation.md 3.2, 8: the post-hook
  of `CSimpleModelTrackerNode::vf_03`, G11, which writes the pose buffer of (c) right after the jobs; (b) is its
  alternative C). The chains (run 3): `R_Armu` 108 -> `R_Arml` 110 -> `R_Hand` 112 -> `R_Weapon` 119, left 86 -> 88 ->
  90 -> `L_Weapon` 97; the controller grip is the weapon node's target (re-xr-input.md 4.2). The root children
  `IK_LHand`/`IK_RHand` (nodes 4/5, at the root while standing) look like animation-authored IK targets. Who reads
  them: the engine's `Modifier.IK` through its data `TargetSocket`, before G11 (re-arms-ik.md 1.2); P8 saw no arm IK on
  Talion in idle or melee, so nothing fights G11 there. Built: (c) with our own two-bone solver, `SOMVR_ARMS=1`
  (re-xr-pipeline.md 6.6); P8: a phase-2 edit reaches the screen and the next read-back exactly (0.0 cm). The
  candidates as first listed:
  (a) the procedural body's arm IK input (`ProceduralBody::Arm`, `ProceduralBody.IK.HandGuardDist`: the engine already
  runs an arm IK after the animation); (b) register our own node entries like `FUN_02240f40` does (IEngineModel slot
  38/39); (c) write the pose buffer (`animNode+0x58 -> +0x40`, 0x20 bytes per node) after evaluation.
  **Fallback** if none works: hide the arm pieces and draw simple wraith-glow hands and weapon outlines with the overlay
  renderer (always in front; clearly worse, kept only as the safety net).
- **The ghost hand** (HG-6): when the shown hand is more than 4 cm from the tracked one (the spring, a wall, past
  Talion's reach `a + b - 0.5`), the layer draws a faint wraith-blue hand sprite at the real grip: a quad facing the
  head, 0.1 s fade in, no mesh (the pointer's dot pattern). It reads as "your real hand" and fits the lore. Without
  IK (`SOMVR_ARMS` off) the same sprite shows where the hands are.
- **Fingers** (HG-10): the finger joints in the same G11 pose (right 114-118 and 120-129, left 92-96 and 98-107,
  re-arms-ik.md 2): the grip curls the three lower fingers, the trigger the index, touch on A/B/the stick the thumb.
  Per-object poses (the sword fist, the bow grip, the dagger, an open palm on a wall hold, a claw on an orc's neck),
  sampled once from the game's own animations with `SampleNode`. HoF2 precedent: `FingerCurl.cs`.
- **The arms stay** (the honest compromise, design-hla.md 2.1): HL:A shows gloves only; Talion's arms cannot be hidden
  (no piece hides them, the bone collapse would smear the sleeves). So they get plausible elbows (the elbow hint and its
  turn limit, built) and stop at walls. If the headset shows them wrong, hiding the sleeves needs RE (a per-material or
  per-mesh-section hide).
- The cape is Havok cloth (`hcl*`). Run 13's shots showed its display mesh (the white fur mantle at the shoulders)
  in the view: `SOMVR_HEAD_PARTS=main` now skips the model's cloth display meshes in the eye passes too (they are
  custom render nodes, re-xr-pipeline.md 26.7; the shadow keeps them).

### Hiding the head

- **The head is no piece and no attachment**: hiding each of the 17 pieces of Talion's model hides nothing visible
  (P7), and his 7 attached models are all weapons (P8; child 1 and 4 = the sheathed hilts). So `SetPieceHidden` (G12)
  cannot hide the head, hood or hair (runtime-probes P1q; `SOMVR_HIDE_HEAD=LIST`, re-xr-pipeline.md 6.5, has nothing
  to hide).
- Built instead, no IDs needed, both with `SOMVR_BASE=head` (`SOMVR_HIDE_HEAD=near|bone|bone+near`,
  re-xr-pipeline.md 6.7): **near** = a 12 cm near plane while the eyes are on the Head node (run 8: it hides most of
  the head; a few hair/hood strands and pale shapes, probably the quiver's arrows, stay in view); **bone** = the 57
  face, eye, jaw and hair joints below `Head` collapsed onto it in the phase-2 pose (G11's hook). The pose has no
  scale, so the skull stays; the near plane takes it. Run 9: `bone+near` leaves floating hood and hair fragments,
  worse than `near` alone, so VR build 1b uses `near`. Next if fragments stay: an eye-forward offset or hiding the
  quiver.
- Shadows: `near` keeps the whole shadow (the shadow maps are drawn from the lights); `bone` pulls the face's and
  hair's shadow onto the head too (one skin copy per frame), the skull's stays.
- **Near plane**: the level database sets 27.5 cm in the world (spike1 run 1); `SOMVR_HIDE_HEAD=near` sets 12 cm
  (`SOMVR_NEAR_CM` wins), so a hand or the bow nearer than 12 cm to the eyes is clipped too: the headset decides
  whether 12 cm stays. The projection is reversed-Z with an infinite far plane, so precision does not suffer.
- Check in every milestone's shots: no inside of the neck or hood when looking down, up or sideways, no eyelashes.

### When the game takes the body: first person, the window, the fallback

Paired animations (`CCombatSyncAction`: executions, counters, drains, brands, interrogations, grabs, mount and dismount,
captain showdowns) move Talion's body on a fixed path. **The view is never dragged** (the user's "VR-native" decision,
HL:A's rule 1): the player stays in first person and the world explains what happens. Three presentations:

| Presentation | How | Used for (default) |
|---|---|---|
| **First person, anchor frozen** | G14 detects the start; the anchor stops following (Root) or follows the head bone's position only (Head, when the action carries Talion to a new place); arm IK still wins, so the player's hands stay theirs; at the end, a short blink (0.12 s fade) if the body ended > 40 cm from the view | **every paired action**: drains, brands, grabs, interrogations, struggles, stealth kills, counters, executions, getting on and off a mount (with the follower), tower synchronisations, death (then the window) |
| **Window** | the game's cinematic or presentation camera rendered into a framed panel in front of the player, in the calm space (section 6) | in-engine scenes until "around you" exists (section 6), captain presentations when the captain is not in front, the Nemesis promotion, and **unknown states** (the fallback) |
| **Stage view** (third person, still; an option, D2) | fade 0.15 s; the view is placed where the game's own camera is at the start of the action, **frozen there** (head-tracked 6DoF, the game camera's later motion ignored), Talion and the orc perform in front of the player; fade back to first person at the end | off by default; for players who want it: long, large-motion moments (graug mount/dismount, tower synchronisation, captain showdowns), by the rule below |

**Status 2026-10-20 (offline, nothing launched):** a first cut is built as `SOMVR_FALLBACK=auto` (off by default;
re-xr-pipeline.md 6.8): cinematics, presentations, syncs over 600 ms or 75 cm, struggles, mounts, attach, climbs,
death (P8: life stays 2, so the `PreDeath*` tags), photo mode and the kill cam fade (150 ms out, 200 ms in) to the
game camera as the base, the HMD still applied around it, so after the fade the game camera's own pans and cuts reach
the view. Not built yet: "first person, anchor frozen", the window, the frozen stage view, the per-record table. The
VR menu's View row switches it (first / third).

**The fallback's future** (HG-2, plan item 5): a safety net for **unknown** states only, logged with the state's name,
fading to the window in the calm space, never to a moving camera in the world. Per state (design-hla.md 2.9 has the
whole table):
- the per-attack combat cameras and the kill cam: `Combat.EnableCameraOverride` = 0 (section 0's pokes; RT whether it
  also stops `FinalBlow`): the kill stays first person, only its slow-mo kept;
- short syncs (counters in place) and struggles: the Root anchor frozen for the sync, the hands the player's; a
  struggle is the orc in your face, shake or shove to break free (section 3);
- knockdowns: the view stays at the real head height and dims red until Talion is up (RT the knockdown recoil's TAG);
- staggers and hit recoils: the Root anchor, at most 0.5 m smoothed, or none (D20), with a red edge pulse;
- the automatic traversal (vault, mantle, wall-run, auto-climb, ledge pull-up, drops): the game-carried follower of
  the anchor (above), position only;
- our hand climb: the fallback's `climb` condition off while `SOMVR_CLIMB` holds (today it ends the climb: HL:A's
  ladders are the player's own hands);
- mounts: Seat mode (section 5);
- scripted level cameras (dialogue framings, reveals): ignored, first person kept, a soft light cue toward the subject
  (SH-4; RT `KeyframedCameraDisablePlayer`, re-firstperson.md 7);
- death: first person to the end (the anchor frozen, a 1 s grey-out facing the killer), then the game's "death /
  Nemesis promotion" sequence in the window.

**The stage view's rule** (the option): an action longer than 1.5 s that moves Talion more than 2 m or rotates him more
than 90 deg goes to the stage view; everything else is first person. A table of action records -> presentation is
built from the logs of the flat tests (G14 logs the record name, duration and root displacement) and can be overridden
per record in the mod's config. The record name and the participants (for the table and for "first person" to know
which orc is the partner) come from G14's start hook: `FUN_028081a0(sync, record, target)`, source and target
`Combat*` at `sync+0x1c0`/`+0x1f8` (re-m1m2.md 6.1).

## 2. Locomotion and turning

The movement standard is `design-locomotion.md` (the user's speed principle, rules L1-L6 of section 0); this section
is its summary.

### Where movement comes from

Talion's ground speed, his starts and his stops are animation: the root motion of the clips the data graph picks,
which the code integrates without memory (design-locomotion.md 2.1). Run 3: the first `Run` came 0.6 s after a full
stick. Tuning cannot make that 1:1: none of the 30 `Client.PlayerMovement` attributes is a speed or acceleration (only
`BaseSpeedMultiplier`, which no code reads; 2.2). So **somvr owns Talion's ground velocity** (`SOMVR_LOCO=1`, LO-2,
not built): in FSM 6/7, G23 replaces the root translation with our velocity x Talion's own frame time. Root-motion mode
has no memory, so he moves at that speed in the same tick and stops in the same tick. The game keeps collision and
sliding, steps, slopes, gravity, the animation locks (syncs), attacks and the legs (fed by the virtual stick: its
magnitude and `FreeRun` pick the clip family, and `Game_Velocity` reports our speed by itself). It carries Talion only
in the moves the player asked for: free-run parkour, falls, syncs. The ownership rules are in design-locomotion.md
3.1; every hand-over is logged with its cause.

- **Speed from the stick, at once**: `|s|` 0..0.5 -> 0..`V_walk`, 0.5..1 -> `V_walk`..`V_run`, the stock speeds (LR1),
  so the legs and the speed agree; no start clip, no stop slide. Backward and sideways at full speed; Talion faces the
  move direction within 100 deg of the head's heading (up to 720 deg/s), else the head; in a fight the engaged orc.
- **Head-relative movement** (default): `v` along the stick in the head's frame. The game's own camera-relative
  movement (the legs, the stock moves under the free-run latch) needs G2; until G2 the stick is rotated by
  `Δ = hv - hc` (head heading minus game camera heading), with the rig in `SOMVR_YAW=fixed` (re-xr-input.md 3.3-3.4).
- **Hand-relative movement** (option, `SOMVR_MOVE=hand`, built): the stick in the left controller's yaw.
- The right stick never reaches the game (it would orbit the game camera); it is the mod's turn control.
- Dead zone 0.15 radial. The room-scale follow (`SOMVR_FOLLOW`, built) joins our velocity in the same delta.

### Turning

Every turning setting is set in the headset and applies at once (the user: "It should be adjustable in-game"; the VR
menu, section 7, built).

| Option | Default | Details |
|---|---|---|
| Snap turn | **on**; 15 / 22.5 / 30 / 45 / 60 / 90 deg (the VR menu has 15/30/45/60/90; 22.5 to add); the default **30** (`SOMVR_SNAP_DEG`, HoF2's) or 45: D7 picks only the default | `psi += step`, rotating the anchor **about the head**, not the floor point, so the head does not swing sideways; instant, or with a blink (`SOMVR_SNAP_BLINK_MS`, option, off). Talion's body follows through G13. |
| Smooth turn | off | 60-300 deg/s (built: `SOMVR_TURN=smooth`, 120 deg/s), eased start; a vignette while turning (option, off) |
| Off | - | no stick turning (HL:A 1.1's option; to add to the VR menu): physical turning only |
| Physical turning | always | the player can just turn; the body follows (section 1) |

No other code ever changes `psi`: not the game camera, not attack snap-align, not cinematics (section 6). The two
allowed exceptions, both player-started: the wraith phase's 180 deg re-face at the fog's peak (section 3; on by
default, an option) and mounts in car-style mode (section 5).

### Free-running, sprinting, jumping

In Shadow of Mordor running fast **is** free-running: while `FreeRun` (command 18; run 3's bindings put it on **pad A**,
together with `Jump` and `CombatPrimary04`) is held, Talion auto-vaults low walls, runs up climbable walls and jumps
gaps (runtime-probes P1 results).
- **The free-run button = the left stick click latch** (built: `FreeRun` held until the stick rests at the centre for
  0.3 s). While it is on the game's parkour is armed, and **the game carries Talion** through each move in first
  person (L5): position only, through the follower of section 1, entering and leaving at the speed the player had.
  This is AC Nexus' split: the "leg" moves automatic, the "hand" moves (the climb, section 4) physical. Leaps are aimed
  by looking (the stock leap follows the camera-relative stick; with G2 the camera is the head); option: a leap marker
  at the context action the graph found (RT `FindContextAction`). With LOCO, A no longer holds `FreeRun` (today's
  pad-A passthrough, re-xr-input.md 3.3).
- **The arm-pump sprint** (LO-4): with the stick past 0.9 within 45 deg of forward and no engaged orc within 4 m,
  pumping the arms adds speed above the run, up to the stock sprint speed (`SOMVR_LOCO_SPRINT_SCALE` up to 1.25), with
  a haptic tick per stroke (Sprint Vector's feedback). It coasts down to the run at 1.5 m/s² when the pumping stops; the
  stick at the centre brakes at 12 m/s², pulled back at 25 m/s²; above the run a hard turn sheds speed (20 m/s²
  lateral). `SOMVR_LOCO_ARMS=stick|stride|free|off` (design-locomotion.md 3.3). **No VR speed cap**: speed is fine when
  the rest holds (the user, section 0).
- **A outside combat**: free-run off = a hop (a tap: 0.6 m at the live gravity) or the arm-throw jump (hold A, release
  it while throwing both arms up: 0.6-1.2 m, Stride's), our launch through G24, the current speed kept; free-run on =
  the stock context jump and leap (the game carries it). **A in combat** = the wraith phase or the dash (section 3).
- **No teleport by default.** Free-running, climbing, stealth and the fight circle's spacing are the game's continuous
  motion. **Blink and Shift** are options for players who cannot use stick motion (SH-3, D17, off by default): an arc
  from the left hand lands on walkable ground within 6 m (a ray, the normal within 35 deg of up); on release Talion
  moves along the straight path through the game's collision at the follow's 30 cm a frame, so the AI and stealth see
  a fast walk (HL:A's footstep markers); Blink fades the view out over 80 ms and in over 120 ms, Shift moves it over
  0.2 s without a fade; a blocked path stops where the collision stops it; the stock leap at an edge is a blink in
  either mode. RT: the walkable test (the surface record), how stealth and the fight circle take a 0.2 s move of 4 m.

### Crouch, stealth, seated play

- **Physical crouch** (default on): HMD height below 75 % of the calibrated standing eye height for 0.2 s injects the
  stock `Crouch` command; standing back above 82 % ends it. The game's stealth (detection, stealth-kill prompts)
  then works as designed. Not built (`somvr_ctl.c` has the stick pulse only; HG-11); RT whether `Crouch` is a toggle
  or a hold.
- Right stick down = crouch toggle (for seated play, long stealth, or players who do not want to crouch).
- The view always stays at the player's real head height; Talion's crouch animation lowers his body under it; LOCO's
  speeds switch to the stock crouch speeds (LR1).
- **Seated** (`SOMVR_SEATED` with `SOMVR_HEIGHT_CM`, built; in the VR menu): the **reach assist** (HG-11): the
  ground-execution stab counts at knee height, touch-to-use needs no floor reach; "re-measure my height" on the VR
  menu.

### Smoothness first, comfort aids optional

- **No pitch or roll from the game, ever** (L4). The anchor carries position and our own yaw only.
- **Smooth, not slow** (L3): the view's position and velocity are continuous across every hand-off (section 1); the
  anchor is extrapolated to the display time; the game runs at the display rate (D15): a dropped frame at 9 m/s is an
  11 cm stutter that rotation-only reprojection cannot hide.
- **Vertical smoothing**: the anchor's Y follows the root (or head bone) through an 80 ms critically damped filter:
  stairs and slopes glide; a 50 cm drop still lands within ~0.2 s.
- **Speed and ramps**: no VR cap and no "acceleration profile" (the game has no such value: speed and ramps are
  animation data, design-locomotion.md 2.2). The settings are a **stick response** (0 ms default; up to 120 ms of
  exponential softening, `SOMVR_LOCO_STICK_MS`), a **speed scale** (`SOMVR_LOCO_SPEED` 0.6-1.3, default 1.0) and the
  **brakes** (12 / 25 m/s²). The known attributes: fall damage, the landing camera, the capsule, and
  `PhysicsCharacterGravity` = -20 (a console variable, RT its name: `pmrec`).
- **Vignette** (an option, **off by default** for every trigger: walking, sprint, smooth turns, carried moves, falls,
  mounts; the user's principle: the aids are optional extras). **Status 2026-10-20 (offline, nothing launched):**
  built as `SOMVR_VIGNETTE=0..1` (one strength on the rig's measured artificial speed, not per trigger yet; a
  head-locked layer quad, not the overlay renderer; in the VR menu), with `SOMVR_SNAP_BLINK_MS` (the snap lands on a
  black frame) and `SOMVR_SEATED`/`SOMVR_HEIGHT_CM`: re-xr-pipeline.md 11. Per-trigger levels later (SH-7). VR build
  1b still sets 0.4 (a launcher change to 0).
- Root motion of the player's own attacks is gone with the stock melee (section 3); snap-align only runs for game
  attacks, which the player no longer starts.

## 3. Physical combat

### The idea: "assisted physical"

**The blade decides contact; the game decides the outcome.** Freeflow (press X, Talion leaps to the next orc and plays
a canned swing) is replaced by real swings of the tracked sword. Every contact is real geometry: blade against bone
capsules. Every consequence is the game's own: the orc's recoil, death, sever, XP, the hit streak and the Nemesis
memory. The orcs keep their whole AI, their attacks and their turn-taking. The assists are small and explicit: slightly
fat hit capsules, generous block zones, telegraphs drawn in the world, a short step toward the orc you swing at. The
user chose this on 2026-10-20 ("assisted physical": real swings decide hits; orcs take turns from the front with
telegraphs; parry by putting the sword in the way; the assisted step).

**Built** (offline, `SOMVR_COMBAT=1` + `SOMVR_BLADE=controller`, re-xr-pipeline.md 6.3-6.4): the swept blade vs one
capsule per orc, the tiers, charges and cooldowns, hits through G8, hit-stop, the swing's stimulus, and
`SOMVR_COMBAT_REACT` (the AIBlockTable roll, 6.3.1). **Not built**: bone capsules, walls stopping the blade, blocks,
parries, the telegraphs, stun, flurry, executions, the open window, the G4 veto, the fight-circle tuning, body slots.

### What transfers from shipped VR games

| Game | Its pattern | Here |
|---|---|---|
| **Skyrim VR + HIGGS/PLANCK** | animation-driven enemies on Havok; PLANCK detects weapon contact and feeds the game's own hit function with synthesised hit data; the game plays the stagger; ragdoll on death; HIGGS grabs | **our core pattern, transfers fully** (G8). Its partial active ragdoll on living enemies does not (only death ragdolls here; `Combat.RagdollReaction` **needs RE** (names only, re-combat.md 2.1), it might give partial hit ragdolls) |
| **Until You Fall** | fixed-lead telegraphs, geometric block bars judged at the impact instant, head dodges, one engaged enemy, swing-speed and travel thresholds, hit charges, enemy hit-stop, short strong haptics | **transfers almost fully**: orcs already have start-attack events, parry windows and an impact key (`APPLYDAMAGE`), and the fight circle is already a ticket system. Its guard/crit layer is not added: Shadow of Mordor's own progression (hit streak, captain traits) stays |
| **Blade & Sorcery** | physics weapons, damage from momentum, stab vs slash, dismemberment, slow motion as a power, throw and recall | speed tiers, stab vs slash, slow-mo moments, an optional dagger throw with wraith recall. Physics-driven enemies, pinning and limb grabbing do not transfer |
| **Boneworks** | a full physics body, weapon weight | no physics player body here. The weight feel transfers as a stiff spring between controller and drawn sword, and walls that stop the blade |
| **Asgard's Wrath** | semi-physical melee vs animated enemies; a parry at the right moment staggers and opens the enemy; finishers on stunned enemies; slow motion on big moments | perfect parry -> stagger -> "open" window; executions on a ready meter with short slow-mo |
| **Batman: Arkham VR** | gestures for gadgets, a vision-mode toggle | the stun becomes a cloak-hand backhand gesture; wraith vision is a held button, like detective mode |

What does not survive: freeflow's leap-to-any-orc rhythm. VR fights face a few orcs at a time; mobility comes from the
player's feet, the 1:1 stick, the assisted step, the dash, the wraith phase and the Shadow Strike.

### Switching off the stock melee

- G3 never presses the stock strike, counter or stun (`CombatPrimary01-03` = pad X, Y, B; run 3 bindings) and holds
  them, the camera axes, `CenterView` and the KBM combat commands at 0/off against polled reads: the deny list of
  re-xr-input.md 3.6. **Pad mode is forced** (`FUN_00a0eeb0(..., 1)`, `0x46b0d61 = 0`): the combat input map is per
  mode, and the glyphs stay pad style (3.2; RT I4: it holds with no pad connected). A real keyboard or mouse still
  reaches the game (blocking it needs the A1 entry hook); test runs type no combat keys.
- G4 vetoes any `Combat_StartAttack` whose combat is the player's and whose record is a melee swing, as a safety net
  (logged; a test asserts zero).
- The orcs' `Combat` objects are untouched: they pick, start and land attacks as before.

### Blade and bodies: hit detection

**The blades** (world space, every game frame in G1, from the controller pose predicted for this frame's display time,
after the IK spring, so what you see is what hits):

| Weapon | Shape | Notes |
|---|---|---|
| Urfael (sword, right hand) | segment from 8 cm behind the grip to the tip, radius 3 cm | P8: the blade along the model's +Y, the tip 100.7 cm from its origin; the grip in the hand is RT (`wnodes` with the sword drawn) |
| Acharn (dagger, left hand when drawn) | segment, ~30 cm, radius 2 cm | |
| Empty left hand | sphere 10 cm at the palm | stun, wraith flash, grab, drain |

**The orcs**: 13 capsules per character from bones (head, neck, chest, belly, pelvis, upper arms, forearms, thighs,
shins), read with GetNodeTransform (G12) for every character within 6 m (G9). Radii per bone and per creature
(Uruk, caragor, graug, ghul) from a table, +5 cm assist. Weapons and shields get their own capsule or box.
**RT: the orc/beast skeleton node names** (enumerate the skeleton once per type, cache; re-animation.md 2.4; the
game's own `AimAtNode` hit spheres give radii, re-combat.md 5) and **the weapon and shield attachment objects** (the
model's attachment list `*(sm+0x90)`, re-animation.md 4). Cost: ~20 orcs x 13 bones per frame, trivial.

**The test**: the blade's sweep from last frame to this frame, in 4 sub-steps (at 90 Hz a tip at 10 m/s moves 11 cm
per frame), segment-vs-capsule distance. Plain math in our own code, unit-tested offline; no physics library needed.
**The world**: a G21 ray from the blade base to the tip each frame. A wall in between clamps the IK target and voids
hits beyond it; a fast blade hitting stone gives a clank haptic.

**A hit counts** (UYF's starting numbers, tuned in the drills). Every speed here and in the tiers comes from the grip's
XR-space velocity (tip: `v + ω x r`) rotated into the world, never from world-position differences, so walking or
turning with a still blade is no slash (re-xr-input.md 4.1):
- slash: tip speed >= 3.0 m/s (mean over the last 80 ms) and >= 15 cm travel since crossing it;
- stab: >= 1.8 m/s along the blade axis (cos >= 0.8);
- per-orc re-hit cooldown 0.25 s (0.12 s in a flurry); 3 hit charges, one back every 0.25 s; flailing without a charge
  only gives a glancing tick (no damage, no streak).

### Hit tiers and how a hit is dealt

| Tier | Rule | Game attack record |
|---|---|---|
| Light | 3-6 m/s | the stock basic attack, variation by swing direction |
| Heavy | >= 6 m/s and >= 40 cm travel | the stock heavy/combo-ender variation (can knock down) |
| Stab | thrust rule | light, or the ground-execution record on a downed orc |
| Flurry | any valid hit on a stunned orc | the stock flurry strike records |
| Open | any valid hit within 1 s after a parry | heavy |

Record names **RT** (the fields are decoded, re-combat.md 1.4; the switches go through G22): log the player's records
in a flat combat drill (`Combat.Debug.Attack`, `.ApplyDamage` and `Combat.Value.LogFileName` into the run folder; G8's
own log).

**Dealing it** (G8, on the game thread in G1 after the animation update):
`Combat_DealHit(hitDir = tip velocity normalised, playerCombat = Character_GetCombat(CPlayerObj), hOrc, record, false)`.
The game then applies its rules: alignment, "cannot hit" cases (a shield from the front, captain immunities), damage,
recoil by direction, buffs, XP, death, sever and Nemesis bookkeeping. If the record path insists on an active attack,
drop one level to `ServerCombat_ApplyAttackDamage` with `HitInfo_Init`. `DamageStruct_DoDamage` stays a test-only
fallback (no recoils).

**When the game refuses** (shield in front, an immunity): a clank, the "blocked" haptic, and the blade bounces back
10 cm on its spring for 0.15 s. The ways past are the stock ones: the phase (vault over), stun, attack from behind.
P9: a shield defender blocks every frontal hit by itself, stock and ours (`block=1`, no damage), with no AI involved.
**Orcs that block**: the AI sees a stock attack through **AI stimuli**: `Combat_StartAttack` calls
`FUN_026c7180(c, attackData, target)`, which registers the record's `RegisterStimulus`/`PersistentStimulus` with
`CAIStimulusMgr` (not `CombatAttackStartArgs`, a script hook). When a swing starts toward an orc (speed crosses 3 m/s
within 1.5 m, heading at it), make the same call, `FUN_026c7180(pc, FUN_00a91300(rec), hOrc)`, and remove the
persistent stimulus at the swing's end; if the block/dodge AI also needs a live player attack, expose `rec` as
`pc+0x4d0` for the swing, last resort a server-only `Combat_StartAttack` that G4 lets through (re-combat-vr.md 1.4).
The block/dodge pick comes from the orc's own pools, and whether a hit is blocked is decided at damage time
(`IsBlock`, the shield), so the game rejects the hit by its own rules. P7/P8: no AI reaction to the swing event, and
in P8 none to the stock strikes either. re-combat-spawn.md 2: the block/dodge trigger is not a stimulus but the target's
`AIBlockTable` roll at the attacker's attack start (chance 0.5 by default); on success `FUN_029ac890` writes an
incoming-attack fact, so a VR swing calls `FUN_029ac890` itself (P9's `incoming`). Built as `SOMVR_COMBAT_REACT`
(the game's roll per swing). P9: `AI_Defender` and `AI_Berserker` have no AIBlockTable, so there is nothing to roll for
them; a direct `incoming` made a defender raise its block (1 of 3, the call outside its own attack); a forced notify
gave an infantry no block or dodge. Next (plan item 8): for archetypes without a table, a VR chance per archetype and
`FUN_029ac890` straight, judged in P10 against the defender and the berserker.
**Hit-stop**: on heavy hits freeze the struck orc's animation for 0.12 s: animation time comes from each object's own
`IEngineTimer` timer (`HOBJECT+0x68`), so give the orc a frozen timer with `SetObjectTimer` and give the old one back,
the engine's own sync/companion pattern; `SlowMO.*` is global only (re-combat-vr.md 2). P7/P8: the swap and the
restore are clean; 120 ms holds the orc's animation and its reaction or blow comes late by the hold; 600 ms lost the
hit reaction, so holds stay near 120 ms.

### Defence: blocks, counters, dodges

**The telegraph** for every orc attack aimed at the player:
- start: G4 (record, target); parry window: G5 (`BPW`..`EPW`); impact: the `APPLYDAMAGE` key time;
- lead time from the animation's key times: `Combat_StartAttack` already stores them per attack (ushort ms from the
  start: `Combat+0xcc` = `APPLYDAMAGE`, `+0xd4`/`+0xd6` = `BPW`/`EPW`, `+0xca` = the first `BCOL`), and IEngineModel
  +0x198 (`KeyLookup`, slot 51) finds any key, `APPLYDAMAGE` too, e.g. a second impact (re-combat-vr.md 3.1-3.2).
  P7/P8: the live impact within a frame of `Combat+0xcc`; the parry window opens at the attack start and closes
  16-70 ms before the impact (melee 0.80 s, arrows released 82 ms after `EPW`). Fallback: an **attack timing table**
  (record -> start-to-parry-window and start-to-impact times, attack direction) that the flat drills build
  automatically from the logs.

**Counterable blows** (the stock blue warning) get a **block bar** (UYF, in Hand of Fate 2's approved style), drawn by
the overlay renderer 40 cm in front of the head toward the orc, across the blow's path. Its place comes from the orc's
weapon bone sampled at the impact time: IEngineModel +0x168 `SampleNode(anim, node, ms, &xf)` samples any node at
any time (the raw animation: no blend, IK or root motion; re-combat-vr.md 3.2, 3.4; P7/P8: model space, the live hand
7-22 cm off on average, at most 34), or from the timing table's direction class (left, right, overhead, thrust). The
bar turns green while the blade is lined up. The **block magnet** (HL:A's quiet help): near the bar, the drawn blade
eases up to 5 cm onto it, visual only; the judging stays geometric.

**Judged at impact** in G6 (and G7):

| Situation at the impact instant | Result |
|---|---|
| blade within 16.5 cm of the bar in its plane, 40 cm in depth, +-30 deg aligned, and the blow has a parry window | **counter**: the hit is cancelled; the orc gets its parry recoil (`FUN_02810af0` with the orc attack's `RecoilTypeParry`, or `FUN_02811140` with a zero amount, re-combat.md 2.1, 3.6; fallback: a G8 hit with the stock counter-strike record) and is open for 1 s; hit streak +1 |
| the same, blow without a parry window but blockable | **block**: cancelled, no stagger |
| the player out of the blow's range or angle | miss, by the game's own range/angle test (the body follows the head with a 4 cm threshold in combat) |
| otherwise | the blow lands |

**Unblockable blows** (the stock red warning) get Hand of Fate 2's **ground arrow**: grey, from the orc toward the
player, filling red until the impact. Ways out: step or lean out (head moved >= 30 cm sideways since the attack
started: G6 cancels it, UYF's head dodge), or evade.

**Evade (A)** (the user's vault idea 2 first; design-locomotion.md 3.7-3.8):
- **Toward an orc** in front within 3 m = **the wraith phase** (LO-6): G3 presses the stock evade (`CombatPrimary04`)
  with the stick at the orc, and the game's own target test decides. **The body does the stock vault**: the game's
  collision, landing spot, the orc's reaction (it turns) and the streak rules (record, duration, path: RT LR6, P10's
  `vaultlog`). **The view phases through**: horizontally it follows the root's real path (the vault crosses the orc on a
  nearly straight line), vertically it stays at the start eye height (the flip's 1.5-2 m rise never reaches the view)
  and returns to the normal height over 150 ms at the landing; the game-carried follower smooths take-off and landing;
  no rotation. **Ghost mode**: a white-blue wraith fog that peaks while the eyes are inside the orc's capsule (+10 cm),
  0.3 s in all (a layer quad like the vignette's; the near plane clips the orc's mesh, the fog hides its inside), the
  stock wraith vision tint where it switches fast enough (122 `CombatSecondary01`, a toggle by P9; RT its speed), a
  wraith whoosh (the Shadow Strike's sound event, RT) and a haptic sweep from the front hand to the back one.
  **Re-face**: the 180 deg snap at the fog's peak, so the orc is in front again (on by default for seated play; an
  option). Fallback (the stock vault refused, or LR7 shows a clean way): our own phase, a free-space check behind the
  orc (two G21 rays and a ground ray), `Player_Teleport` (G13) at the fog's peak, the view along the chord over 0.3 s
  (RT the AI's response). Option: the blink (80 ms out, black while the vault runs, 80 ms in, facing the orc).
- **Sideways, back, or nothing in front** = **the dash**: the stock roll is pressed for its dodge window
  (`BDW`/`EDW`) and its root motion replaced by ours (G23): 2.0 m in 0.3 s, a raised-cosine velocity, position only.
  Option: a blink.
- **The grab-vault, the skill move** (the user's idea 1, LO-12, after M6): when an orc in front within 1.6 m can be
  vaulted, a **handhold glyph** shows on its shoulders (at the `Neck`/`Head` bones + 10 cm, while a hand is within 60
  cm; RT the bone names). A grip within 15 cm of it = a hold pinned to the orc's moving bone (haptic); Talion's body
  then moves by minus the hand's motion relative to that bone, the climb's own rule on a moving hold. **v1** (M): the
  hand's pull past 1.5 m/s triggers the stock vault, its animation rate following the hand's speed (0.6-1.5x, the
  tracker rate, LR8), the view on the real arc (the player drives it), no fog. **v2** (L): the release velocity
  (`-v_hand` x 1.3) launches Talion ballistically (G24) over the orc, the direction bent up to 25 deg toward the landing
  point, the fling gravity -12 until the landing, the orc's capsule out of the way (LR7), a 1.2 s timeout. Both stay:
  A = the phase, a grip on the glyph = the grab-vault.

**Projectiles** (archers, thrown spears): a `CProjectile` flies itself: per frame `CProjectile::vf_13` -> move and ray
cast -> impact `FUN_027be5d0` (the target's `AimAtNode` spheres) -> `FUN_026c8e10` (a DealHit for projectiles) ->
`ServerCombat_ApplyAttackDamage`. A pre-hook on the update slot sees every live projectile (position, direction,
speed, target): `+0x228` = 1 removes it (a block), a rewritten quaternion redirects it (a deflect)
(re-combat-items.md 1). P8: arrows 28 m/s, spears 30 m/s, no homing; `+0x228` removed an arrow cleanly; a reflected
spear flew back as the player's but missed, so a deflect needs a lead or homing on the shooter. First: ground arrows
and dodging; later: blocking and deflecting with the blade.

**When Talion is hit**: the game's reaction keeps its gameplay effect (our blade hits are disabled while the game has
Talion in a stagger or knockdown) but not its camera effect. Staggers move the anchor at most 0.5 m, smoothed (the
game-carried follower), or not at all (an option, D20), with a red edge pulse; a knockdown dims the view red for its
duration while the view stays at the real head height. Grabs and struggles
(`CStruggle*`): shake both controllers or shove forward (>= 2 m/s, repeated) for the stock struggle inputs
(`CombatStruggle01..10` through G3); the button prompt is the fallback.

### The assisted step

The user's decision ("a short assisted step toward the orc you swing at"; design-locomotion.md 3.7; LO-5): when a
swing starts toward an orc (the combat module's "heading at" test: the tip past 3 m/s, the orc within reach + 0.8 m and
within 35 deg of the swing), LOCO adds a step toward that orc's chest: `d = clamp(dist - ideal_reach, 0, 0.5 m)`, over
0.18 s with a raised-cosine velocity (a peak of ~4.4 m/s for 0.5 m), along the floor, stopping 0.7 m from the orc. None
when already in reach, when blocking, or with the stick pointing away. The view moves with it: the swing caused it
(L1), it is short and always the same (L6). Logged per step; an option (on). In fights the stick stays 1:1, capped at
the run; no arm sprint near an engaged orc; Talion faces the engaged orc.

### Stun and flurry

- **Stun**: a backhand or open-hand sweep of the empty left hand through an orc's head or chest capsule (>= 2.5 m/s)
  -> G8 with the stock stun record (Talion's cloak stun).
- **Flurry**: every valid hit on a stunned orc is a flurry strike: speed threshold 2.5 m/s, re-hit cooldown 0.12 s,
  the stock flurry records (they break the stock shield and "combat master" defences, as in the flat game).

### Executions and finishers

**VR-native** (the user's decision, D1 answered): you do the motion, the orc plays its animation, the view is never
dragged. With the stock melee off there are **no executions at all until this is built** (SH-1, plan item 12).

- **Ready**: the hit streak reaches the stock threshold (8; 5 with the skill). Urfael's runes glow with one haptic
  pulse: a ClientFX on the sword the way the `WEAPONFX` key does it (`FUN_00a09e70` create struct with the active
  weapon's model as parent, `FUN_00d8c7f0` create; we keep the handle). The stock glow most likely comes from data
  (`Combat.BuffCounter.HitCounterBuffs`), so our G8 hits may get it for free (re-combat-items.md 2; P7: the stock
  swing trail and our hits' blood trail come on the sword by themselves; the streak glow is not seen yet, only x1 was
  reached). Fallback: an overlay glow along the blade.
- **Execute**: hold the right trigger (the "wraith charge", the stock `SpecialAttackModifier`) and land a heavy hit.
  G8 with the stock execution record; on contact a 0.3x slow-mo for 0.5 s (G15, the stock executions have one); the
  orc dies the game's way, decapitated when the record severs. The player's own swing is the animation, in first
  person. Holding the trigger makes executions deliberate, never accidental. `FUN_026d4ff0` takes a `forceKill` flag and
  needs no animation (re-combat.md 0, 1.1; P7: DealHit from our code works, `forceKill` not tried yet); fallback:
  the kill record plus `Combat.Debug.AlwaysSever` for that one hit.
- **Cinematic executions** (an option, off): inject the stock execution chord with the virtual left stick pointed at
  the target, and show the paired animation first person with the anchor frozen (the stage view if chosen).
- **Ground execution**: a downward stab (tip >= 1.8 m/s, blade within 35 deg of straight down) into a downed orc's
  torso -> the stock ground-execution record.
- **Decapitation** only where the game's own records sever (D11): a captain's permanent death stays as rare as in the
  flat game.

### Body slots: the sword and the dagger

**Built** as `SOMVR_SLOTS=1` (offline, re-xr-pipeline.md 21; needs `SOMVR_INPUT=1`): the slots on the head in tracking
space with a lag-free torso yaw (the head's heading, held within 30 deg while the sword hand is near its slot); the
sword by the game's own `FUN_023116f0` (confirmed statically: its only caller is the model keys' tail jump, which
somvr uses to refuse the game's SHEATH of a held sword; the game's other sheathes of it are answered by drawing it
again), the dagger by P7's `L_Weapon` re-attach; a tick on entering a slot, a pulse and the game's sound on a draw or
a put-back; the bow and climb holds put the hand's weapon back and bring it back after; no blade from an empty hand.
The design as first written:

HL:A's over-the-shoulder slot works from the head, not from the drawn body, so the slots live in the head's frame with
the body's lagged yaw (HG-7, plan item 7; design-hla.md 2.3):
- **Urfael**: the right grip within 20 cm of a point 15 cm behind, 10 cm right of and 10 cm below the head base =
  draw; squeezing there with the sword = sheathe; a double squeeze anywhere = quick draw. The game's own draw:
  `FUN_023116f0(inv, activeItem, 1/0)` (the model keys `SHEATH`/`UNSHEATH`, re-combat-items.md 3; RT whether it plays
  the draw animation from our code, the arms' IK covers it, and whether the game re-sheathes on its own out of combat).
  Then the sword stays in the hand (D14).
- **Today the sword can stay sheathed while the player swings**: the stock melee is never pressed (the deny list), P8
  saw it sheathed, and our hits then land from an empty fist. The slot or an automatic draw at the first swing fixes
  it.
- **Acharn**: the left grip at the left hip (option: lower back).
- A haptic tick on entering a slot, the game's unsheathe sound.

### The dagger: stealth kills and grabbing

- Acharn sits at the left hip (option: lower back). Left grip within 15 cm of it = draw; release there = put back.
  Showing it: weapons are per-item model objects attached by node hash (model entry `+0x30` active node, `+0x34`
  inactive node); write `+0x34` = the hash of `L_Weapon` and call the game's re-attach `FUN_0231e2e0(item)`, restore to
  sheathe (re-combat-items.md 3; P7: slot 1 `PC_Dagger_01`, to `L_Weapon` and back by the game's own call).
- **Stealth kill**: when the game offers it (unaware orc, from behind: the stock prompt condition), a stab or slash
  with the dagger into the head, neck or upper back -> G8 with the stock stealth-kill record (with the wraith charge
  held: brutalize, when unlocked, the `Torment` family). Paired = the record's `AnimationData.Sync` is set (then
  G14's `FUN_028081a0` aligns both bodies); P7 `syncinfo`: the torment (brutalize) records use the StealthBrand sync,
  the loud sneak kill its own (re-combat-items.md 4). **VR-native** (the user; D18): your own stab decides the kill
  and the moment, the orc plays its animation, the view is never dragged. Two ways, picked from the shots: (a) the
  HL:A-like default, G8 with the stealth-kill record **without** the sync, a generic death with hit-stop and blood (RT
  its look); (b) the stock command (G3, target in front) at the stab's contact, so the victim plays its half of the
  sync, first person with the anchor frozen (section 1).
- **Drop and ledge kills**: stab down from above, or pull a ledge orc while the stock prompt shows -> the stock command
  (they move Talion: the game-carried follower, position only; a vignette only if chosen).
- **Grab**: left grip on a stunned orc's head or neck (or from behind) -> the stock grab (G3). While held: dagger stab =
  kill; hold the left trigger = drain (then brand, below); hold B = interrogate. First person, anchor frozen, the orc
  held at arm's length by the latched hand (the grab assist: a gripping hand within 25 cm of an orc's neck or shoulder
  bone latches onto it; SH-8). If the root ended more than 40 cm away, a 120 ms blink re-seats the view.

### The bow: two hands, wraith slow motion

**Build 11 (2026-10-25, re-bow.md 12)**: proven headless on build 10's switches - the left trigger brings the bow, the
right grip at the bow nocks, pull and let go: our own arrow (with the stock shot's attack and arrow records) leaves the
bow along the bow line, an arrow less, it hurts and kills orcs; the bow held upright (`SOMVR_BOW_HOLD=auto`), the string
on the string hand (`SOMVR_BOW_STRING_POS=hand`), no wraith body over our hands (`SOMVR_BOW_GHOST=nomat`), the shot not
cancelled by a hit (`SOMVR_BOW_RELEASE=now`). The text below is the 10-20 state.

**Built** as `SOMVR_BOW=1` (offline, re-xr-pipeline.md 13; needs `SOMVR_INPUT=1`): the bow enters when the string
hand's grip closes within 15 cm of the nock point (or by the bow hand's trigger), arms at 20 cm of draw, full at 55
cm, shoots on the grip's release; the aim command is held (124 `CombatSecondary03`, right by P9), the game's free-aim
ray takes the bow line at `BOW_FA_CAMXF`, and the fire command is tapped once the game's aim runs. **P9 caveat**: the
built fire default `SOMVR_BOW_FIRE_CMD=125` is `PC_Stealth`'s entry, not a fire (the fire is an attack input in the aim
mode's pools: RT, next probe; somvr's wait for the game's aim keeps 125 from entering stealth). The design as first
written:

- **Draw**: hold the left trigger: the wraith bow forms in the left hand (the stock aim command held through G3, with
  the game's own Focus slow-mo while Focus lasts). Release it: the bow fades.
- **Nock and pull**: the right trigger within 12 cm of the nocking point grabs the string; pull back (full draw >= 35
  cm); release the trigger to shoot.
- **Aim**: the arrow points from the string hand to the bow hand. In the release frame G2 points the gameplay camera
  along that line (the stock aim is the camera centre), then G3 presses the stock fire button for one frame; the eyes
  are untouched (H1). Simpler (re-combat.md 4.2): skip the camera and the button and fire our own `fireXf` from the
  controllers with `FUN_02332c50` (spawn at `fireXf.pos`, along its +Z). re-bow.md: the aim is a data-driven player
  weapon mode, arrows are tokens, Focus is energy; the stock aim ray reads `cam+0x338` through one call
  (`BOW_FA_CAMXF`), so the cheapest VR aim overwrites that ray with the bow line (built). P9: aim = 124 held 0.2 s;
  `aimray` not run (RT, P10).
- The overlay draws a wraith arrow on the string; the arrow count shows at the bow grip while aiming (HG-8). An aim
  line is a setting (off).
- **Shadow Strike** (when unlocked): X while an arrow is aimed at an orc -> the stock Shadow Strike (its range is
  `AimingTeleportRange`). **The wraith dash** (default, LO-9): the view travels the straight line to the arrival point
  at constant speed with 60 ms eased ends, position only, under a white-blue wraith tunnel (the vignette quad's ring at
  40 %): fast, smooth and started by the player, the Jet Island case. If the game teleports, the dash runs over 0.3 s
  while the body is already there; if it moves him, the follower carries it (RT LR10). The player was aiming at the
  orc, so no turn is needed. Option: a white wraith blink (0.1 s). **Built in build 12** (re-bow.md 14, off by
  default: `SOMVR_BOW_STRIKE=dash|blink`): the stock strike is a 1.6 m lunge then a 7.8 m teleport into a 3.6 s
  knockdown sync; the view holds still through the lunge, then dashes (9.2 m in 0.3 s) to the arrival under the
  white-blue tunnel; the body moments hold it there through the sync and ease it back.

### Wraith powers

All of them are the game's own commands, so the game's rules (skill unlocked, meter, valid target) decide. The gesture
supplies the intent and the target: for the gesture's frames G3 points the virtual left stick at the target orc in
camera space (the attack predicates use "Direction (To Input)"). A refusal gives the "denied" haptic.

| Power | Gesture | Stock command (G3) | Presentation |
|---|---|---|---|
| Wraith vision | hold Y (left) | `CombatSecondary01` 122, a toggle (P9; not `VisionModeToggle` 243) | the game's effect (stereo check: re-ui-effects.md) |
| Drain | left hand on a stunned/grabbed orc's head + hold the left trigger 1 s | stock drain | first person, wraith glow from the palm |
| Brand (unlocked) | the same, held >= 2.5 s | stock brand | first person |
| Combat drain | wraith charge + left grab on an orc's head mid-fight | stock chord | first person |
| Wraith flash | wraith charge + open left palm thrust toward an orc (>= 2 m/s) | stock chord | first person, overlay flash |
| Shadow Strike | bow aimed + X | stock | the wraith dash (a blink as option) |
| Other skills (wraith burn, ...) | the nearest gesture above + the wraith charge | stock | as their kind |

Chord buttons: run 3 has `SpecialAttackModifier` on pad RT; **RT** which `CombatSecondary`/`CombatTertiary` slot is
which power (the pad-mode `bindings` dump and `Combat.Debug.Input`; runtime-probes I2, re-xr-input.md 3.6).

### Throwing (a VR addition, D12)

The flat game has no player throw besides the bow. Optional, after M5: release the drawn dagger with the hand at >= 4
m/s: it flies on our own ballistic path; on capsule contact, G8 with a light record; after 1 s it returns to the hip
(wraith recall). Detach = `CSimpleModel::vf_43(parentSm, hChild)` (`FUN_0235b3d0`), re-attach = `FUN_0231e2e0(item)`;
the `DETACH` key is unrelated (re-combat-items.md 3; P7: the re-attach works). Until the throw is proven the overlay
draws a wraith-dagger ghost in flight.

### Hit streak (combo meter) and multiplier

- Our hits run the game's attacker bookkeeping (`FUN_026c06b0`, inside `FUN_026d4ff0`). The hit counter counts distinct
  attack records that hit (re-combat.md 1.5): use a different record per swing (RT); fallback: the hit-counter
  functions, last resort `Combat.Lock.HitCounter.Value` (0x4c43ed8).
- Light, heavy, flurry, stun and counter each count as their stock equivalent; waggle never counts (charges, speed).
  The streak breaks the stock way (Talion hit, or the stock idle timer).
- Shown on the sword (rune glow grows with the streak, full glow = execution ready) and on the right wrist tile
  (section 7). The XP multiplier stays the game's.

### Turn-taking, difficulty, feedback

- **The fight circle stays** (G9/G10). VR tuning at Normal: one attacker in the inner ring (two at Hard), lockout timer
  +50 %, `AttackDelayScalar` x1.5, `DoubleAttackChance` x0.5, at most two archers shooting at once. The `Camera`
  score already prefers orcs in view, but it rates the game's orbit camera until G2 is built.
- **Front-first, the VR rule** (HG-5, plan item 9; HL:A's fair threats, the user's "orcs take turns from the front"):
  an orc outside a 110 deg cone around the head may not start an attack until it has been announced for at least
  1.0 s: a positional warning sound at the orc (a Wwise 3D post), an edge glow on that side and a haptic tick on that
  side's controller. The grant (G10) is vetoed meanwhile (re-gameplay.md 6's "veto ... until the player has seen the
  warning"). Needs G2 or the `Camera` term's weight in the score (**needs RE**), or a post-adjust of the total. The
  HoF2 user asked for "LOTS of extra warning" for off-view attacks.
- **Arrivals** (SH-9): reinforcements and captains arriving within 15 m behind the player get a positional cue and a
  marker. Moving the spawns themselves **needs RE** (`AISpawnMgr`, `AlarmLevelChanger`: names only). Near the guardian
  edge (`xrGetReferenceSpaceBoundsRect`), prefer attackers on the room's open side (NH-8).
- **Damage taken** x0.5 at Normal (D10): scale the amount on G6's path when the target is the player (`DamageStruct+0x50`,
  re-combat.md 1.3), or relative difficulty.
- **Haptics** (UYF recipes): own hit 0.2 s at 100 %; shield clank 0.15 s 80 %; parry 0.2 s 100 % both hands;
  execution a 0.6 s ramp; stun 0.1 s; denied 2 x 40 ms; taking a hit 0.3 s 70 % both. The game's camera-FX rumble
  (G20) goes to both controllers at 50 %.
- **Sounds**: the game's hit sounds come with the damage path; the extra ones (clank, lock-in, denied) are posted
  like the game does: 2D `CSoundMgr::vf_13(mgr, "event")` on its global emitter; 3D (the clank at the blade) our own
  registered Wwise object, positioned with `FUN_008c9b80`, then `FUN_008c9840` (re-combat-items.md 5). Candidates
  `ui_lastchance_fail`/`_succeed` for denied/lock-in; the clank names are data (`AudioBlockName`); P7: both kinds of
  post return playing ids.

## 4. Traversal: climbing, ledges, vaults, drops, the wraith-run

**Hybrid** (the user's decision, D4 answered): hand climbing is the climb; the free-run button (section 2) lets the
game's parkour carry Talion in first person. design-locomotion.md 3.5-3.6 is the detail.

### The two ways to climb

| | **A. Hand-over-hand** (Alyx, Boneworks, B&S): **the climb** | **B. Gesture-fed stock climb**: under the free-run latch |
|---|---|---|
| What the player does | grabs any point of a wall and pulls; the body moves by exactly the hand's motion | runs into a climbable wall with free-run on; the hands' pull can feed the stock climb's stick |
| Who moves Talion | the mod: the hold, gravity off on the proxy, the one-shot extra move clipped by the game's collision | the game's own climbing state machine, climb data, ledges, jumps and animations |
| Status | **built** as `SOMVR_CLIMB=b` (re-xr-pipeline.md 12: holds by ray + `CanWallClimb`, the body by minus the holding hand's motion, gravity off by `pc+0x60`, tops by `release`/`lift`/`cmd`; first boot pending, T10). P8: gravity off holds Talion but latches (the undo writes `pc+0x60 = 1`); the surface ray from the head hits Talion first (the filter, 12.3) | the free-run latch is built; the follower that carries it (LO-3) is not. **RT: the "climbing now" read** narrowed to FSM, proxy support, gravity, attachment and the state TAGS (re-traversal.md 1.4); the TAG names from one drill (T2) |
| Feel | the best presence; HL:A's ladders | the stock speed and moves, carried smoothly, position only |

### A in detail: hand over hand (`SOMVR_CLIMB=b`)

- **Grip**: a squeeze looks for a hold for 0.25 s: two rays from 10 cm behind the hand along the palm and the fingers,
  35 cm (`SOMVR_CLIMB_REACH_CM`); a `CanWallClimb` hit = a hold, pinned where it grabbed (the IK target with it). An
  edge hold is found by a ray 25 cm higher and one down onto the top.
- **Pull**: the newest hold drives; the body moves by minus that hand's tracked motion, at most 8 cm a frame; a hand
  that opens or loses tracking lets go. The left stick can walk alongside (`SOMVR_CLIMB_STICK=1`, the fallback "stick
  climbing").
- **The top**: built `release` (default), `lift` (the mod's pull-up) or `cmd` (the stock `LedgePullUp`). To add
  (LO-7): **the push mantle** as the default (`SOMVR_CLIMB_TOP=push`): at an edge hold with the hand on the top
  surface, pushing the hand down lifts the body 1:1; once the eyes clear the top by 20 cm, the push forward steps onto
  it; `lift` stays the fallback when a push stalls, `cmd` for authored ledges.
- **The fling** (LO-7): releasing a hold while the hand moves (the body's velocity `-v_hand` over the last 60 ms > 1.0
  m/s) launches Talion with 1.3 x that velocity (cap 8 m/s) through G24: Climbey's and Gorilla Tag's rule, the release
  speed is the flight speed (the mean over the last 4-6 frames). The graph's fall (FSM 8) and landing follow.
  **Built differently** (2026-10-25, build 13 candidate `SOMVR_CLIMB_FLING=on`, re-xr-pipeline.md 32.14), by the user's
  rule ("Gorilla tag inspired. But don't push me in the opposite direction. Just let me sling myself up"): only up
  (away from the wall and down dropped, a little sideways), x 1.0, cap 5 m/s, our own arc on the climb's ticks with
  gravity still off (no game launch, no fall): a grip catches, the arc's top hangs (build 10's hang), A drops.
- **The air catch**: a grab within 1.5 s of a fling or during any fall stops the fall at the hold (the climb's start in
  FSM 8: momentum zeroed, root-motion mode back, gravity off; RT T10 whether the graph keeps playing a fall).
- **Fling gravity** (an option, off): `pc+0x58` scaled to -12 for our own launches until the landing (the game's -20
  makes hand flings short: 4 m/s up = 0.4 m).
- **Grabs that do not fail** (AC Nexus' and Stride's main complaint: a missed grab is an unexpected fall): the grab
  assist (a squeeze whose rays miss but pass within 10 cm of a climbable hit snaps to it), the fall catch (a squeezing
  hand within 15 cm of a climbable surface during a fall grabs it), a toggle grip option, a haptic on every grab and
  let-go. Every miss is logged with its nearest climbable distance, so the radii are tuned from the drills.
- **The view**: Head mode while climbing (the head bone's position, smoothed; never its rotation; section 1). Talion
  faces the wall; the player's own yaw stays theirs. The fallback's `climb` condition stays off while `SOMVR_CLIMB`
  holds (HG-2).

### B: the stock climb under the free-run latch

- Running into a climbable wall with free-run on runs Talion up it; the game climbs, the view follows through the
  game-carried follower (section 1), position only, entered and left at the player's speed (L5).
- **Pull feed** (option): a held hand moving down feeds the stock climb's stick up (G3; 1 m/s of pull = full stick);
  sideways = shimmy.
- **Fast climb**: the stock climbing leaps (1-2 m up per jump) under the latch.
- **Jumping off or across**: A + stick direction while on the stock climb = the stock wall leap; releasing both grips
  and pulling the stick back = the stock drop.
- No vignette by default (an option).

### Vaults, drops and falls

- **Vaulting at speed** (free-run): stock and automatic; the game-carried follower, position only, no vignette by
  default. Combat vaults are the phase (section 3).
- **Drops and falls**: the fall is physics in momentum mode (FSM 8 keeps the horizontal speed Talion had, ours too);
  the view follows the root exactly (ballistic motion is predictable, the Jet Island case) with only the vertical
  80 ms filter. The landing (stock roll or hard landing) never tumbles the view: position only, the landing dip capped
  at 30 cm and smoothed. Fall damage stays the game's (`FallDamage*` of the player record). Options: the fall vignette
  from 3 m (built, off) and a **long-fall fade** (falls over 8 m fade out after 0.5 s and back in at the landing; off).
- **Leaps between rooftops and ledges**: stock (A at an edge with free-run on); carried by the follower, aimed by
  looking.

### Towers and the wraith-run

- **Forge towers**: climbed by hand (A) or by the stock climb under the latch (B). The synchronisation at the top is a
  short paired action plus a camera presentation sweeping over the region: first person for the action; the sweep is
  skipped by default (the player looks out over Mordor themselves) or shown in the window (section 6).
- **Leaving a tower**: SoM has no Leap of Faith (no traversal move in the binary names one). A jump from the top is a
  long fall at the game's gravity (-20: 20 m in 1.4 s, ~28 m/s at the ground), started by the player and shown as it
  is, ending in the game's landing; the fall vignette and the long-fall fade are the options.
- **The wraith-run** (the fast free-run sprint with its automatic vaults and leaps): the arm-pump sprint (section 2)
  under the free-run latch, carried smoothly; no vignette by default (each trigger an option).
- **The mode switch for the whole section** (re-traversal.md 1.2-1.5): code-level FSM ids 8 Falling, 9
  TransitionAnim, 10 Landing (`P+0x7f0`); climb, ledge, vault, mantle, leap and drop are graph data. LOCO owns FSM 6/7
  (design-locomotion.md 3.1); until the TAG table exists: Seat if mounted; the climb's own Head mode while
  `SOMVR_CLIMB` holds; the follower at FSM 8/9/10, or FSM 6 without proxy support, or a fast vertical root speed; else
  Root (1.4). RT: the `trav` drill (T1, T2, T5).

## 5. Mounts: caragors and graugs

### Riding

- **Getting on**: the stock mount (drop onto a beast, or the stock prompt after branding it) is a short paired action:
  first person with the game-carried follower (a blink as option), then the **Seat** anchor mode (SH-2 = LO-13).
- **Seat mode**: the anchor follows the saddle (the mount `HOBJECT` is `P+0x1d8`, polled in G1: rising = on, falling =
  off; re-traversal.md 3.1-3.2; **RT: its saddle node**: the rider is pinned by `AttachToObject`, node `movement+0x200`,
  its name is data, re-traversal.md 3.4, T9; fallback: Talion's root or pelvis while mounted, which follows the
  saddle; no G13 turns or `pm+0x428` moves while mounted), smoothed (Y 120 ms on a caragor, 200 ms on a graug;
  XZ 30 ms), never pitched or rolled. A graug's rearing and a caragor's bounding never reach the view.
- **Yaw** (D13): **car-style** (default): the beast's yaw change turns `psi`, as in a car seat (steering is the player's
  input); a vignette above 60 deg/s of turning as an option (off). **Free**: `psi` stays; the beast turns under the
  player.
- **Steering**: the left stick goes to the game (the beast steers relative to the camera, which is the head: confirmed,
  its graph reads the rider's camera-relative `DeltaYaw` and camera quat, re-traversal.md 3.3, 3.5; one frame late,
  RT T8); stick click = run; A = the stock mount jump/pounce (`MNT_Jump`). Car-style yaw = the beast's quat at
  `*(P+0x1d8)+0x14`.
- **Hand steering** (an option; design-locomotion.md 3.10): a caragor has no reins, the rider holds its neck ruff.
  Both grips on two hold points at the beast's neck; the hands' mean lateral offset turns it (handlebar-style), written
  as `DeltaYaw`/the camera quat through `MOUNT_AI_INPUT` (re-traversal.md 3.5); speed from the stick or by leaning
  forward (the head's forward offset from the seat, 10-30 cm); a pounce by a shove of both hands.
- **The beast's lag**: its starts and stops are its own clips (`CAIMovement::vf_11` `0x2a781e0`, the NPC slot shared by
  every character); Seat mode makes them smooth, not 1:1. The 1:1 beast (its translation replaced like Talion's) is
  LO-14, after **RE** of `FUN_02a76eb0`.
- **Getting off**: hold X for 1 s (the stock `MNT_GetOff`), first person with the follower (a blink as option). Thrown
  off (the beast killed, bucked): the knockdown rule (the view stays and dims), then a blink to where Talion lands.

### Fighting from the saddle

- The sword works while riding: the same blade-vs-capsule hits and G8 path as on foot (**RT: the mounted
  attack records**, `Combat.Debug.Attack` logging through G22; any light/heavy record that the game accepts while
  mounted).
- The beast's own attacks: B = bite/claw (`MNT_Attack_1`); wraith charge + B = its special (`MNT_Attack_2`, a
  caragor's frenzy or a graug's smash); the stock `MNT_Attack_3/4` go on a hold of B (1 s) and on the wraith charge + A.
- Graugs are big (eyes ~3 m up): strong smoothing as above. Option: graug riding in the stage view, for players who
  still find it rough (a setting, not a separate decision).

## 6. Cutscenes and cinematics

**Cutscenes play around you** (the user's "VR-native" decision; D3 and D19 answered): the view is never dragged by a
cinematic camera; the player stands in the scene, or, where the game only has a picture, watches it in a calm space.
Shadow of Mordor has four kinds:

| Kind | How the game shows it | VR default | Option |
|---|---|---|---|
| Story cutscenes | pre-rendered WebM videos (`share/data/game/cinematics/videos`, Feral's Bink emulation) | **theatre** in the calm space (a video has no 3D to stand in) | - |
| In-engine scenes | the cinematic camera (`clientShell+0xb0`, active camera at `+0x20`, G16; the world render switches to `FUN_009edba0`) | **around you** once its RE allows (NH-4); until then the **window** in the calm space | fixed camera, window |
| Captain presentations (introductions, showdowns, promotions) | `PlayerCameraPresentation` sequences in the player camera; time paused | **in the world** when the captain is in front and close, else the window | always the window |
| Scripted level cameras (dialogue framings, reveals) | the player camera's scripted mode (`cam+0x390`) | **ignored**: first person kept, a soft light cue toward the subject (SH-4) | - |
| Loading screens | `CUILoadingScreen` with tips | the calm space (built, `SOMVR_XR_ENV`) with the tip panel and a progress light | - |

**The calm space** (HG-9, HL:A's rule 5): never a void behind a mono frame. Phase 1 is **built** as `SOMVR_XR_ENV=on`
(re-xr-pipeline.md 14, off by default): a generated Mordor-dusk sky (Orodruin's glow, ridges all round, nothing
brighter than 0.6) as one `XR_KHR_composition_layer_equirect2` layer, or six world-locked quads where the runtime lacks
it; `world` also keeps the last stereo frame dimmed (40 %) behind the menu. Phase 2 (SH-10): the **360 degree backdrop
of the place the player just left**, six extra views from the head position on the last world frame (the H4
mechanism), dimmed to ~30 %. Not built: the layer's **keep-alive frames** (when no game present has come for 100 ms, a
layer thread submits its own frames, the space, the last menu image and the progress light, with real head tracking
until the game presents again; it shares the layer's frame-call lock; RT the longest present gap at a load), the tip
text on a small panel low in front, the **progress light** (HoF2's user asked for one: `LimboCamp.cs`, `WaitBoard.cs`).

**The theatre**: a curved 16:9 screen, 5 m wide at 5 m, world-locked at eye height in the direction the player faces
when it starts, in the calm space. The picture is the UI composite during video states (G18; the video frame is drawn
into the UI target by `FUN_00c28220`, re-ui-effects.md 4). Cylinder layers are used if WiVRn offers
`XR_KHR_composition_layer_cylinder` (**RT: log the runtime's layer extensions on the next boot**); otherwise a flat
quad.

**Around you** (the target for in-engine scenes, NH-4, XL, **needs RE**): one vantage at 1:1 for the whole scene, no
cuts: the player stands in the scene, first person and head-tracked, and the characters perform around them (HL:A's
choreography). Needs stereo on the cinematic render path `FUN_009edba0` and culling and streaming for a viewpoint that
is not the camera's; the vantage per scene (Talion's place, or the first shot's camera with pitch and roll dropped).
An experiment until the RE says yes; the static RE can start now.

**The window** (in-engine scenes, the interim): in-engine cinematics already reach the layer as mono frames (one view,
no H4: re-hud.md 1.3), so the window is mostly a placement and backdrop job: a framed 16:9 panel 3 m wide at 3 m in
the calm space. Later in stereo with a reduced eye separation (2 cm, which keeps the scale gentle through cuts and lens
changes). The cinematic camera's moves and cuts stay inside the frame, so they cannot make the player sick.

**The fixed camera** (option): the player stands in the scene. At each cut the view is placed at the cinematic
camera's position and yaw (pitch and roll ignored, head-tracked 6DoF), held there until the next cut, with a 0.15 s fade
at each cut; the camera's motion within a shot is ignored. Closer to "around you", but cuts come every few seconds and
the speaker may be off to the side.

**Captain presentations in the world**: when the captain is within 15 m and within 60 deg of the view, the head view
stays; the presentation camera is ignored; time is paused as in the flat game; the captain's name, title and traits
card (the HUD element, carved out as in section 7) floats beside him; a soft light cue points at him. Otherwise the
window shows the presentation camera's view (a third, low-resolution view for the few paused seconds). Start/stop: G16
(`pcp+0x90`, re-m1m2.md 7); **RT: its camera pose** (the modifier-stack result `cam+0x340`).

Skipping works as in the flat game (its skip button through G3). Subtitles go under the screen or window (section 7).

## 7. UI

### The HUD

The game's HUD (Scaleform) is drawn once per frame (H11 skips eye 0's pass) into the engine's own UI surface, which
we copy at the present into our UI texture (G18; re-hud.md 1.4), at a fixed safe zone and size. Built first: the whole
HUD as one world-locked quad layer (`SOMVR_HUD=quad`, H11; run 7: 1.2 x 0.675 m, 1.5 m ahead, 0.1 m below the eyes;
the VR menu switches it on and off and sets its distance, 0.75-3 m). That is a floating HUD, which HL:A never shows
(rule 3), and its world markers sit at one eye's screen positions, not on the orcs, the counter icon over an attacker
included (SoM's main warning, re-hud.md 4.4). So:
- **the quad is the interim**: smaller and lower (0.9 m ahead, 35 deg below the horizon, 0.6 m wide), re-placed at
  every snap turn and every menu opening;
- **the release target** (HG-8, plan item 16) is the body tiles below plus stereo markers, **the counter icon first**
  (until the block bars of section 3 replace its role), then the left wrist (health, focus, elf-shot), the arrow count
  on the bow, the streak on Urfael's runes and the right wrist, the popups;
- the quad keeps only the in-world modal screens (pause, map, skills, Nemesis), re-placed 2 m in front of the head
  whenever one opens (G17).

Two presentations (D6):

**On the body** (default): the HUD texture is **cut into tiles** (fixed rectangles of the stock layout) shown as small
world quads on the player:

| Tile | Where | Shown |
|---|---|---|
| Health, elf-shot (arrows), focus | inside of the left wrist (the user chose the same in HoF2: "health on the back of the glove") | when the wrist faces the eyes, and for 3 s after a change |
| Arrows left | at the bow grip | while aiming |
| Hit streak, specials ready | Urfael's runes (a ClientFX, section 3) and the back of the right wrist | in combat |
| Minimap | left forearm (raise it like a watch) | when looked at |
| Objectives, XP and skill-point popups | a small panel low in front, **world-locked where it appeared**, fades after 4 s | on change |
| Captain / boss health | above the captain, world-locked | in that fight |

Tile rectangles **RT: measure them from HUD-only frames** (runtime-probes P4; a script diffs frames with elements on
and off; `UI.PrintHUDElementList` prints nothing in this build, so the probe walks the element tree for the names,
re-hud.md 4.1). The tiles are quads cutting the one UI swapchain image by `imageRect` (re-hud.md 1.4). No tile ever
follows the head.

**Visor** (option): the whole HUD texture on a curved panel 2 m away, 70 deg wide, slightly below eye level, lazily
following the head (it re-centres over 0.5 s when the head has turned more than 35 deg away).

### Menus, map and the Nemesis screens

- When G17 says "not in game" (pause, map, Nemesis hierarchy, skills, runes, main menu), the **menu screen**: the UI
  texture over the 3D surface (which holds the 3D UI scene, rendered at the UI aspect: two quads at one pose, both at
  the UI aspect; front end, loading and modal menus render one view, re-hud.md 1.3-1.4) on a curved panel 3.2 m wide at
  2.5 m, world-locked where the player looked when it opened (re-placed in front of the head at each opening, G17),
  in front of the calm space (section 6). A finger poke on any panel = a click (HL:A and HoF2 both touch menus; not
  built).
- **First, pad-style menus**: in menus the controllers are an Xbox pad at the binding layer (passthrough: A/B/X/Y, the
  grips as LB/RB, triggers, sticks, START = the left menu button), so every menu works from the game's own data
  (re-xr-input.md 3.5). The laser pointer (next bullet) works alongside it.
- **Laser pointer** (built offline as `SOMVR_POINTER=1`, re-xr-pipeline.md 10; the feed: re-ui-pointer.md R1) from
  the hand that last pulled its trigger (a dot and a thin ray, layer quads). Its hit point on the panel becomes the
  game's mouse position; right trigger = click, B = back, the right stick = scroll and map pan/zoom (pad stick through
  G3), A = confirm. Mouse input: not `SDL_PushEvent`, but the game's own CInterfaceMgr mouse calls (`FUN_00a0fa20`
  move, `FUN_00a0f760` down, `FUN_00a0f8c0` up) from the input tick; P8: hover and click work, 1:1. They skip
  Win32Mouse, so the game stays in pad mode with pad glyphs; the pad-mode guards on hover and click
  (`LP_GUARD_HOVER`/`_CLICK`) answer 0 while the pointer is live (RT: P8 could not test them).
- **The Nemesis screen** shows flat on the panel at first. It is not a 3D UI scene: it is the player view with the
  `CHierarchyMenuMgr` camera (`FUN_00dbf3f0`), which H1 sees; while `FUN_009fa370(*(P+0x310))` is true, H4 runs mono
  (re-hud.md 2.4). Stretch (after M9): that view in stereo as a diorama table of captains in front of the player, its
  base from the game's view. The 3D UI scenes of other menus (`UISceneCamera`, `FUN_00c1e370` -> `FUN_009f2550`) have a
  0x328-byte view record filled from the camera object (re-hud.md 2.2; RT U8).

### World prompts and markers, in stereo

The world-anchored items (enemy and captain markers, objective markers: the `CNavMarkerInstance` list `0x46b9200`)
are projected per view by `FUN_00ec4f10` (per marker `0xec4f83` -> `FUN_00ec4a50`) and drawn by the flat HUD, so they
would show with one eye's projection only (re-hud.md 3; this replaces the earlier `DAT_046b8580`/`FUN_009f1d20` reading).
- G19 captures every item's world position (`FUN_00ebc700`) and icon each frame; the HUD stops drawing them. Icon and
  type = the `NavMarkerData` record `m+0x298` (`IconMovieClip`, `IconTexture`, `IconSize`, `ActionID`); hide = the
  game's own hide path after `FUN_00ec4a50` (med). The old candidates `+0x268`/`+0x26a`/`+0x278` were misreads (an
  object ref, the pad/keyboard icon byte, a timer). RT U3' (`navdump`), re-hud.md 3.4-3.5;
- the overlay renderer draws them per eye at their world positions (correct depth in both eyes), facing the head, a
  constant 1.5 deg beyond 3 m and real-sized closer, in the game's icon art cut from the HUD texture;
- **interaction prompts** ("Hold B to drain") become VR hints: a gesture glyph by the target or the hand while the
  stock prompt for that action is up. Every HUD prompt goes through the PromptShow/PromptHide casters (`0x46b6bb0`,
  `0x46b6bc8`) into three `CUIHUDPrompts`, each with a `CUIPromptDiamond` (`+0x1d8`) of live entries {key, action id,
  StringId, IconId, category}: poll them or hook the delegates (re-hud.md 4). P8: the counter prompt is not a diamond
  entry but the icon over the attacker (a combat marker, re-hud.md 4.4), and `Combat.Debug.Prompt` logged nothing.

### Subtitles

The stock subtitle element, carved out as a tile: 1.8 m away, 12 deg below the view centre, following the view
lazily (0.3 s); letter height at least 1.6 deg. In the theatre and the window: under the picture. Speaker-anchored
subtitles are a stretch goal.

### The VR settings page

**Built** as `SOMVR_VRMENU=1` (re-xr-pipeline.md 15, merged 9ed2205; offline, off by default, needs `SOMVR_INPUT=1`):
HL:A's wrist menu, a 20 x 25 cm panel on the left wrist, opened by turning the left palm toward the face for 0.5 s or
a 0.6 s press of the left menu button, worked with the right hand's laser and trigger, with haptic ticks. Every change
applies live and is saved 1 s later to `SOMVR_SETTINGS` (the launcher passes `~/.local/share/somvr/settings.conf`; a
key the user never changed keeps following `som-vr.conf`). Its rows: turn (snap / smooth), snap angle (15/30/45/60/90),
turn speed (60-300 deg/s), vignette (off, 10-100 %), snap blink, seated and seat height, HUD on/off and distance, main
hand, view (first / third, with the fallback), haptics, Recentre. The label art is a built-in font (no atlas file).

To add (HG-4, LO-11, SH-7; plan item 14): turning off; snap 22.5; movement direction (head / hand); Blink / Shift /
Continuous (SH-3); the LO rows of section 8 (stick response, speed scale, arm sprint, brakes, carried moves, the
phase/dash/Shadow Strike presentation, re-face, the assisted step, fling gravity, long falls, climb grip and assist,
the leap marker, mount steering); per-trigger vignette levels; "re-measure my height"; the main hand swapping the
controls themselves (today it moves only the panel and the pointing hand); a finger poke as a click; a "VR" tile next
to the game's pause menu. **The first page** follows AC Nexus' setup survey: one question, then presets (Immersive ...
Most comfortable) fill the rows; this design's defaults are "Immersive" (every aid off), the user's answer of section
0. HoF2's `VRSettings.cs` and `TouchSlider.cs` are the model for the layout.

## 8. Comfort and accessibility options

Every option is on the VR settings page (in the headset, applied at once) and in the settings file. **Bold** =
default. The defaults are the "Immersive" preset: smooth, 1:1 and every comfort aid off (the user's principle); the
presets fill the rows for players who want more aids. (built) = in `SOMVR_VRMENU` today.

| Area | Option | Choices |
|---|---|---|
| Forced in VR (no option) | camera shake, motion blur, DoF, camera roll, lens rain/vignette/flares (screen-space), the per-attack combat cameras and the kill cam (first person) | off |
| Height | play height | **real floor**, seated (adds the standing-minus-seated difference, measured once; built); re-measure |
| | seated reach assist (low stabs at knee height, touch-to-use without the floor) | **on when seated**, off |
| Movement | mode | **continuous, 1:1**, Blink, Shift (D17; Blink and Shift off until built) |
| | direction | **head**, left hand |
| | stick response | **0 ms**, up to 120 ms of softening |
| | speed scale | **1.0**, 0.6-1.3 of the stock speeds |
| | sprint | **stick + arm pump**, arms free, stride (grab the air), off (the latch's stock sprint only) |
| | brakes (stick centred / pulled back) | **12 / 25 m/s²**, 6-40 |
| | free-run | **click to latch**, hold |
| | crouch | **physical + right stick down**, stick only |
| Turning | snap | **on**; angle 15 / 22.5 / 30 / 45 / 60 / 90, default **30** (D7; built without 22.5); blink on snap: **off** (built) |
| | smooth | **off**; 60-300 deg/s (built) |
| | stick turning off (physical only) | **no**, yes |
| Vignette | per trigger (walk, sprint, smooth turn, carried moves, falls, mounts) | **all off**; each off/light/medium/strong (built: one strength, 0-100 %) |
| Game-driven motion | combat vault toward an orc | **the wraith phase**, blink |
| | re-face after the phase | **on**, off |
| | dash (evade away), Shadow Strike | **dash / wraith dash**, blink |
| | assisted step | **on**, off |
| | carried moves (parkour, landings) | **smooth**, smooth + vignette, blink |
| | long falls | **as they are**, fade over 8 m |
| | paired actions | **first person, the view never dragged**, stage view by the rule of section 1 |
| | staggers | **0.5 m, smoothed**, none (D20) |
| | knockdowns | **dim, view stays**, follow the body down (not recommended) |
| | view (the fallback) | **first person**, third (built) |
| Climbing | climb | **hand over hand** + the stock climb under the free-run latch, stick |
| | grip | **hold**, toggle; grab assist **on**, off |
| | fling gravity | **game (-20)**, light (-12) |
| | top of a hold | **push mantle**, lift, stock pull-up, release (built: release / lift / cmd) |
| | leap marker | **off**, on |
| Mounts | yaw | **car-style**, free; steering **stick**, hand; graug in stage view: **off** |
| Cinematics | in-engine scenes | **around you** (once its RE allows; until then the window), window, fixed camera |
| | captain presentations | **in the world when close**, always the window |
| Combat | executions | **your own swing + slow-mo** (VR-native), cinematic (the stock paired animation, first person) |
| | stealth kills | **your own stab** (D18), the stock synced kill |
| | slow motion (executions, kills, bow focus) | **on**, off (`Combat.Enable.SlowMo` 0x4c44318) |
| | block assist | **generous** (UYF tolerances, the block magnet), strict |
| | button counter (accessibility: a button in the parry window counters, the game's own outcome without the paired move) | **off**, on (the orc's half = its parry recoil, re-combat.md 3.6 B) |
| | damage taken / attack delay (VR difficulty) | **x0.5 / x1.5**, sliders |
| | hit charges (anti-waggle) | **on**, off |
| | indicators | block bars, ground arrows: **on**; aim line: **off** |
| HUD | presentation | **on the body**, visor; interim quad on/off and 0.75-3 m (built) |
| | subtitles | **on**, size S/M/L |
| Hands | dominant hand | **right** (sword right, dagger and bow left), left (all mirrored; built for the panel and the pointer only) |
| | ghost hand | **on**, off |
| | sword | **stays in the hand once drawn**, hold the grip |
| | haptics | **100 %**, 0-150 % (built) |
| | one-handed mode (later, NH-6) | **off**, on (the bow from the sword hand by a button and the head's aim, the dagger by a button) |
| Warnings | blows from behind | **sound + edge glow + side haptic + ground arrow**, ground arrow only |
| Colour | indicator colours | **game palette** (green/grey/red, each with its own shape), colour-blind palette |

## 9. The Quest 2 Touch controller map

Right-handed layout (the left-handed one mirrors it). "Stock" = the game's own command, injected through G3 by command,
not by pad button (run 3 has the KBM-mode bindings, pad inputs included; **RT: the pad-mode dump**, runtime-probes I2;
re-xr-input.md 3.6 maps every row). In menus the controllers are a passthrough pad (section 7).

| Control | On foot | Fighting | Bow drawn | Climbing | Mounted | Menus |
|---|---|---|---|---|---|---|
| Left stick | move 1:1 (speed by deflection, head-relative) | move, capped at the run | move (slow) | walk along (`SOMVR_CLIMB_STICK`); the stock climb's stick | steer | navigate |
| Left stick click | free-run latch (the game's parkour) | - | - | - | run | - |
| Arms (no button) | pump with the stick forward = sprint | - | - | pull = climb; release while pulling = fling | lean forward = speed (hand steering) | - |
| Left trigger | hold = bow (built: the bow hand's button) | hold = bow | keep holding = keep the bow | - | hold = bow | - |
| Left grip | draw Acharn (at the left hip); grab an orc | grab (drain, combat drain, brand); on a vault glyph = grab-vault | - | hold the wall; air catch | the neck ruff (hand steering) | - |
| X | hold = quick wheel (the stock d-pad actions, SH-6) | - | Shadow Strike | - | hold 1 s = dismount | - |
| Y | wraith vision (the stock toggle, 122; P9) | same | same | same | same | - |
| Menu (left; the left system button on Index) | pause menu (on the release); held 0.6 s = the VR settings panel (built; also the left palm toward the face for 0.5 s) | same | same | same | same | back to the game |
| Right stick X | snap / smooth turn (off: physical only) | same | same | same | same | scroll / map pan |
| Right stick Y | down = crouch toggle | down = crouch | - | - | - | scroll / map zoom |
| Right stick click | map | map | - | - | map | - |
| Right trigger | - | hold = wraith charge (execution, wraith flash, combat drain) | - | - | wraith charge (beast specials) | pad RT; click once the laser lands; the VR panel's click |
| Right grip | draw Urfael (at the right shoulder; double squeeze anywhere = quick draw); squeeze at the shoulder = sheathe | on a vault glyph = grab-vault | at the nock = grab the string; open = shoot (built) | hold the wall (the sword sheathes itself to climb); air catch | the neck ruff (hand steering) | - |
| A | hop; hold and release with an arm throw = jump; with free-run on: the stock context jump and leap | toward an orc: the wraith phase; away: the dash | - | the stock climb: wall leap (with the stick) | jump / pounce | confirm |
| B | interact (pick up, use) = `Activate` (the stock RB), never pad B (the stock stun); in a grab: hold = interrogate | (button counter, if enabled) | - | - | bite / claw; hold = beast attack 3 | back |
| Oculus button | system (WiVRn recentre) | | | | | |

**Gestures** (no button): swing = attack (light/heavy by speed), with the assisted step toward the orc; thrust = stab;
blade into the block bar = block or counter; empty left hand backhand = stun; open left palm thrust (with the wraith
charge) = wraith flash; downward stab on a downed orc = ground execution; dagger stab from behind = stealth kill;
arm pump with the stick forward = sprint; grip-and-pull on walls = climb; release while pulling = fling; a grab in a
fall = air catch; push down on an edge = mantle; grip on a vault glyph and pull = grab-vault; both arms thrown up at A's
release = jump; string hand closed at the nock and pulled = draw the bow; shake or shove in a struggle = break free;
the left palm toward the face = the VR settings panel; physical crouch = stealth; an open hand squeezed at an
`Activate` prompt = use (touch-to-use, SH-5).

## 10. Performance budget

**The load**: two full deferred pipelines per frame (each view runs G-buffer, lighting, its own shadow maps, exposure
and post: re-stereo.md section 3). WiVRn recommends 1832 x 2015 per Quest 2 eye (headset look 1), ~3.7 MP; with the
10 % FOV overscan the eyes are 2015 x 2217 (runs 6-8), ~4.5 MP each, 8.9 MP per frame: 4.3x the pixels of 1080p, plus
twice the geometry and shadow passes. The game thread's simulation and animation are not doubled; the render thread's
command execution and Feral's D3D11-to-Vulkan translation are, for the scene passes.

**Targets**: 72 Hz (13.9 ms) while developing M1-M3; **80 Hz (12.5 ms) as the release default** (D15); 90 Hz (11.1 ms)
only if the measurements allow. WiVRn's headset app sets the rate; the game is paced by `xrWaitFrame` in H5, with the
game's vsync (`DAT_046a9e78`) and Feral's limiter (`VulkanMaxFramesPerSecond`) off.

**GPU budget at 80 Hz** (estimates, to be replaced by M1 measurements):

| Item | ms | Lever |
|---|---|---|
| Eye L: G-buffer, lighting, shadows, post | 4.5 | settings, render scale |
| Eye R: the same | 4.5 (3.8 with shadow reuse) | reuse eye L's shadow maps (the shadow passes: cascades `FUN_007e2940` from `FUN_007dc0b0`, other casters `FUN_007e0be0`/`FUN_007e4a00` from `FUN_007db760`, re-stereo.md 3) |
| HUD (Scaleform, once) | 0.2 | |
| Overlay renderer, copies into the XR swapchain (layout B adds eye L's copy) | 0.3 | |
| WiVRn's encode (Vulkan video on the same GPU) | ~1.0 | its encoder settings |
| Margin (streaming, effects, big fights) | 2.0 | |
| **Total** | **12.5** | |

**Settings to cut, in order** (a VR profile in the game's `render.cfg`; the auto-detected start was Ultra/High):
1. motion blur -> 1 (FXAA, no blur) and DoF off: forced anyway;
2. ambient occlusion High -> Medium;
3. shadows Ultra -> High;
4. vegetation Ultra -> High, and `VegSys.MaxDistOverride` if needed;
5. tessellation off;
6. lighting High -> Medium, mesh Ultra -> High.
Textures stay Ultra (16 GB of VRAM; the test rule of 4.5 GB free still applies) and texture filtering stays Ultra
(cheap, and it helps a lot in a headset).

**Per-eye resolution**: WiVRn's recommended size x a render scale (default 1.0, range 0.7-1.2), one eye in layout B.
`ScreenWidth`/`ScreenHeight` alone cannot give the tall eye: the 3D surface's height is `ScreenWidth` x the back
buffer's aspect (`FUN_00741980`; `ScreenHeight` is never read there), so `SetResolution 1832 1920` gives 1832x1031.
The route: the call-site hook `RES_3D_DESC` (`0x741ceb`) passes `ScreenHeight` as the height, then `exec
SetResolution 1832 1920` (the UI size and the window stay); every engine buffer follows; the desktop mirror becomes a
pillarboxed portrait (cosmetic); a `Video.UseWidescreen` letterbox gets the two `RES_RECT_LETTERBOX*` sites
(re-resolution.md 1; P7: 3D 1832x1920 with the UI at 1920x1080, no letterbox). Built: `SOMVR_EYE_SIZE=xr` (the
runtime's size x the overscan) set only while stereo runs, so the menus stay 16:9 (run 7: `SetResolution` in 151 ms,
one frame held). Per-region scales come from measurements, not a live controller (the game has no dynamic resolution).
Anti-aliasing is FXAA only (no TAA, no MSAA in this build); if vegetation shimmers, 1.2x supersampling where the
budget allows.

**Foveation**: WiVRn offers foveated *encoding* (its default 0.5), which shrinks the video stream but not the
rendering. Fixed foveated rendering would mean injecting variable-rate shading (`VK_KHR_fragment_shading_rate`, which
RADV offers on RDNA2) into the game's G-buffer and lighting passes from our Vulkan layer, a stretch goal after M9. The
layer can tell them apart: the G-buffer is the only render pass with 4 colour attachments (`B10G11R11` + 3x RGBA8) +
D32S8 at the 3D size, lighting the one with a single colour attachment = the G-buffer's RT0 (re-resolution.md 3; RT:
log the attachment tuples once, 3.4); VRS means re-creating those two render passes with a shading-rate attachment.
The hidden-area mesh (`XR_KHR_visibility_mask`; ~15-20 % of each eye is never seen on a Quest 2) is the same kind of
stretch goal: drawn at depth 1.0 right after the G-buffer pass begins, it rejects the G-buffer writes there
(re-resolution.md 3.3).

**CPU**: the risk is the render thread with doubled draw calls. Measure it in M0/M1 (spike1's RATE lines,
`Report.Render.ThreadTimerRate`). Levers: Feral's threaded recording (`Vk_UseThreadedRecording`), fewer draws (vegetation
distance, `Vis.AreaCull.*` thresholds), one widened centre view for the per-frame visibility list (re-stereo.md section
3), so eye R does not redo the per-frame node work.

**Measuring**: the Vulkan layer logs `xrWaitFrame` to `xrEndFrame`, late frames per second and GPU time from timestamp
queries around each frame's submissions; runs follow the "quiet" protocol (all workers paused, the LACT VR profile
active), never while the user is in VR.

## 11. Automated tests: the user only plays at milestones

### The rig

- **One test boot at a time**, with the slot from the Hand of Fate 2 coordinator (HoF2 has testing priority), never
  while `status.json` says `"user": true`, and with at least 4.5 GB of free VRAM.
- `scripts/boot_check.sh` as today: headless gamescope, Steam visible (the user's choice), `ptrace_parent.py`, the
  real session D-Bus, prefs in the run folder, the CEF leftovers moved out of the install, the install listed before
  and after, the save guard on (fail-closed), `GameData.sav` compared before and
  after: any difference fails the run.
- **Monado** with its remote test driver and our own copy of Hand of Fate 2's `xrsim.py` command set (head, turn, hand,
  rel, btn, hold, stick, trig, squeeze, swing, move, path, fling, wait, seat, noise, `fov quest2`), on its own runtime
  directory and port (4250), so HoF2's environments stay untouched. HoF2's traps apply: one daemon connection (the
  remote driver leaks a socket per connection), paced at 20 Hz (frame rates are measured on the real headset only), and
  a real compositor on a virtual display (the null compositor gives 320 x 240 eyes). Controller profiles: Monado's
  remote driver offers `valve/index_controller` (HoF2 logs), WiVRn's Quest 2 `oculus/touch_controller`; the layer
  suggests both plus `khr/simple_controller`. The cheap first input check, before any game boot: **R0** = vkcube + the
  layer under Monado with xrsim (re-xr-input.md 5.1). Run 5 (the first layer boot in the game) uses the simulated HMD
  with the null compositor, for the pipeline checks: logs and `SOMVR_XR_DUMP` images (`docs/run5-monado-plan.md`).
- **The command channel**: spike1's `SOMVR_TRIGGER` file grows into a command FIFO (HoF2's `cmd.py` pattern): `shot`,
  `scenario`, `pose`, `state`, `log`.

### Save and account isolation

- The guard serves a **dev save**: a copy (of the user's save, or one made in test runs) in which test runs unlock
  skills, runes and abilities with the game's console commands (`Character.PurchaseSkill/SetLevel/GiveItem`, G22), only
  while the guard is on. The user's file is never opened for writing.
- **Achievements and stats**: a test fight with Steam visible can unlock achievements on the user's account. Block
  them in test mode at the game's own layer (`Interface.Achievements`, `StatTally`, the WB online service's stat calls;
  **needs RE**, never the Steam layer). D16.

### Scenarios

A catalog as in Hand of Fate 2 (`Tab | Label | steps`), run by the mod. Steps: teleport (`Teleport x y z`), spawn (from
our code: the server's debug spawn `FUN_029a4830`, a captain from the faction target; no console command spawns and the
debug spawner's input is dead: re-combat-spawn.md 1, P9's `spawn`), relative difficulty, give skills, time scale, god
mode (`Combat.Lock.*`, guard on only), wait for a state. The set: walk path, the stick sweep with starts and stops
(1:1), the arm-pump sprint, a free-run path with vaults, hand climbing with a fling, an air catch and a push mantle, the
stock climb under the latch, ledge pull-up, forge tower, drop and the long fall; duel, three orcs, shield orc, two
archers, a captain with traits; stealth kill; grab, drain, brand; bow headshot; Shadow Strike; the wraith phase, the
dash and the assisted step in a duel; the grab-vault; caragor ride with kills; graug ride; captain presentation; a story
video; a load in the calm space; pause, map and Nemesis menus by the passthrough pad and by the laser; the VR settings
panel by the laser; death and the Nemesis promotion.

### Driving and judging

- **Poses**: per-scenario xrsim scripts. Combat drills read the telegraph from the command channel (bar pose, impact
  time) and put the blade into the bar at the impact (must pass) or beside it (must fail): every rule is tested in both
  directions.
- **The game's own logs**: `Combat.Debug.Attack/ApplyDamage/Collider/RejectedAttack/Stats` with
  `Combat.Value.LogFileName` in the run folder (hits, rejects, recoils, deaths, pre-death, hit counter);
  `Faction.Dump` before and after (the captain dead, killed by the player, decapitated or not).
- **Our log** (`somvr.log`): every hit (speed, capsule, tier, record), every block and parry with its time relative to
  the impact, every veto, every anchor change with its cause, every mode change (Root, Head, Seat, frozen, stage view,
  window), frame timing.
- **Asserts**: zero player game attacks started; **no anchor motion without a tagged cause** (input, body-follow,
  allowed game motion; an untagged delta over 1 mm fails); no pitch or roll from the game; the head hidden (near/bone);
  the eye poses equal the predicted XR poses of the same frame. The movement rules (design-locomotion.md 1.1):
  - **L1**: every change of the view's velocity over 0.5 m/s within 0.1 s has a tagged input cause in the same or the
    previous frame;
  - **L2**: the same scripted input gives the same speed curve within 2 %;
  - **L3**: in the per-frame anchor log and the 0.25 s both-eye pose log no step exceeds the speed x the frame time x
    1.5; late frames are counted while sprinting;
  - **L5**: the hand-off log: the velocity jump at a carried move's entry and exit < 1 m/s after the filter;
  - **L6**: every assist (the assisted step, a fling's aim bend, the grab assist) logged per use with its size.
- **Both-eye shots**: on `shot NAME` the Vulkan layer saves both eye images of the submitted frame (left | right).
  Automatic image checks: left/right mean brightness within 3 % (exposure), structure equal after disparity alignment
  (one-eye shadows, flares, rain), no magenta or black squares, no flat HUD in the eye images. Head-motion runs (turn
  sweeps, walk paths) take a shot every 0.25 s and keep a pose log: still shots hid both the eye mismatch and the
  judder in Hand of Fate 2's first real test.
- **Reviewed as a player** before any build reaches the user: an AI reviewer walks every place's shots with a SoM
  checklist built like HoF2's `playtest-checklist.md`: comfort (eye height, horizon, nothing within 20 cm of the eyes
  but hands and weapons, nothing head-locked), the body (no neck or hood from inside, hands on the controllers, the
  sword in the fist edge-first), indicators (bars on the correct side, readable, the right colours), text (readable,
  not cut off), markers at the right depth, screens and menus. Findings are filed with their shot paths and fixed first.
  **The HL:A checks** (design-hla.md 4, item 14) join the list: no view motion without a tagged cause in the walk,
  fight, stagger and knockdown runs; nothing head-locked or floating in play; a visible warning before every orc
  impact in the drills; no hand or blade inside a wall in the shots; no black or frozen frame across loads (the
  keep-alive log).
- **Performance runs**: three reference places (a quiet outpost, a big fight, dense vegetation) and a sprint path
  (late frames counted while `|v| > 5 m/s`: at speed a dropped frame shows as translation judder), 2 minutes each, late
  frames and GPU ms, under the quiet protocol. **Soak**: 30 minutes of scripted fight loops. After any amdgpu reset,
  testing stops and the user is told.
- **The real headset** only at milestones, with the Quest on USB and the user's OK: adb screencap of both eyes; every
  adb override undone; the Quest stays offline.

## 12. Milestones

The user plays at three milestones only (M4, M8, M9). Every exit criterion is checked by the automated rig.

| M | Content | Exit criteria (all automatic) |
|---|---|---|
| **M0** (done 2026-10-19) | spike1: hooks H1-H4, the camera offset (run 1 done 2026-10-18: hooks work, view layout right, same-frame render), save guard, comfort pokes, the sbs two-eye test (run 2: layout A fails; run 3: layout B at 60 fps; run 4: the same with `mod/somvr`) | the guard serves the profile read in a loaded world and the save stays byte-identical; the sbs shot decides layout A or B (re-stereo.md section 8); a frame time for two views; no crash |
| **M1** stereo in the simulator | H5 (`xrWaitFrame`, `xrLocateViews`), H1 per-eye poses, H3 + H6 asymmetric frusta, H4 (A or B), H9/H10 into a projection layer, near plane 5 cm, G2 gameplay camera = head, Root anchor without body-follow, HUD on the visor quad (the stereo part done in runs 5-9; G2 and the Root anchor are still open: plan items 9 and 3) | Monado both-eye shots: 1:1 scale (a known object measured), level horizon, left/right brightness within 3 %; eye poses = predicted poses; 10 minutes without a crash; GPU ms measured; cylinder-layer support known |
| **M2** locomotion and body | G3 virtual pad (move, sprint, jump, crouch, interact), snap/smooth turn, body-follow and body turn (G13), wall fade, the Root anchor and Head mode (HG-1 = LO-1), P10 and the 1:1 motion core (LO-0, LO-2), the carried-move follower (LO-3), first person through the body moments (HG-2), head hidden (`SOMVR_HIDE_HEAD=near`, section 1), the VR settings panel (HG-4, built), the calm space and keep-alive frames (HG-9), crouch by ducking and seated parity (HG-11), menus by the passthrough pad and the laser (G17, G18, section 7) | walk/run/stop/free-run/vault/fall scenarios pass the anchor-cause assert and L1-L5; looking down shows the body and no head; main menu -> load the dev save -> pause -> map, all by pad in a script; a scripted panel change applies live; no black or frozen frame across a load |
| **M3** hands and weapons | arm IK (G11) with Urfael and Acharn in the hands, spring and wall stop, the ghost hand (HG-6), body slots (HG-7), finger curl and per-object poses (HG-10), HUD tiles on the body and stereo world markers with the counter icon first (G19, HG-8), the 80 Hz settings | hand-to-controller error < 3 cm over the arm-motion scripts; reviewed shots: weapons in the fist; 80 Hz with < 1 % late frames in the three reference places (or a reasoned 72 Hz) |
| **M4** physical combat | capsules and hits (G8), stock melee off (G4), blocks and counters with bars (G5-G7), ground arrows (HG-3), the assisted step and the dash (LO-5), the wraith phase (LO-6), stun and flurry, executions by your own swing (SH-1), hit streak, haptics, front-first turn-taking with G2 and difficulty (G9, G10, HG-5), knockdowns and struggles | the duel, three-orc, shield-orc and captain drills pass in both directions; a visible warning before every impact; Nemesis credit verified with `Faction.Dump`; zero stock player attacks; indicator shots reviewed. **User playtest 1**: exploring at 1:1, hand climbing and free-running, the VR menu, fights |
| **M5** dagger, bow, wraith | grab, stealth kills by your own stab, drain, brand, combat drain (SH-8), wraith flash, wraith vision, the bow (built: its aim ray through `BOW_FA_CAMXF`; the fire command from P10), Shadow Strike as the wraith dash (LO-9) | each power's scenario passes (stealth kill from behind, bow headshot at 20 m, brand a captain, Shadow Strike); the anchor-cause assert holds through every paired action |
| **M6** traversal | hand climbing with the fling, air catch, push mantle and grab assist (LO-7), the stock climb, ledges, leaps, towers and drops under the free-run latch, the arm-pump sprint (LO-4), hops, jumps and falls (LO-8); the grab-vault (LO-12) right after | a forge tower climbed by a pull script, ledge mantles, a fling and an air catch, the tower sync; no game pitch or roll in the logs; L1-L5 hold on a free-run path |
| **M7** mounts | caragor and graug riding and fighting, getting on and off, Seat mode and hand steering (LO-13 = SH-2) | a caragor ride with three kills from the saddle and a dismount; a graug ride; seat-motion shots every 0.25 s reviewed |
| **M8** cinematics and UI | theatre in the calm space with the last place as the backdrop (SH-10), the window with scripted level cameras ignored (SH-4), "around you" if its RE allowed (NH-4), captain presentations in the world, subtitles, VR hints for prompts, touch-to-use (SH-5), the VR settings panel complete with presets | the first story missions scripted through their videos and menus; every presentation kind reviewed. **User playtest 2**: the whole loop |
| **M9** release candidate | performance everywhere, the 30-minute soak, the achievements block proven, `PLAY.md`, uninstall and undo list, handover | all scenarios green; the user's save untouched. **User playtest 3**: sign-off |

The arm-IK write point (G11) gates M3 and most of M4-M6: its static RE and one runtime probe run in parallel with
M1-M2 (both done: re-arms-ik.md, P8 A5 passed). Hand-over-hand climbing is no longer a stretch goal: it is the climb
(built, M6 completes it). Stretch goals after M9: the Nemesis diorama, "stand in the scene" if its RE came late, the 1:1
beast (LO-14), foveated rendering, depth-tested overlays, speaker-anchored subtitles, and the dagger throw if it was
not done in M5.

## 13. Risks

| Risk | Why | Mitigation |
|---|---|---|
| **No usable arm-pose write point** (G11) | the pose pipeline runs as animation jobs | P8: the phase-2 write point works (A5, 0.0 cm) and no engine arm IK runs in idle or melee; built `SOMVR_ARMS=1`, the controllers' test is the headset session; fallback: overlay hands and weapon outlines |
| **Our hits outside an attack confuse `Combat`** | "cannot be damaged during attack", the hit counter, the `+0x287` flag | drop to `FUN_0281a200`; a synthetic attack state if needed; drills log every reject reason |
| **Paired actions move Talion** | executions, drains, grabs, mounts are fixed animation paths | the view is never dragged: anchor freeze, Head mode, the window; executions and stealth kills by your own motion (no sync); the stage view only as an option; a per-record table from the logs |
| **Two pipelines too slow** | shadows and post per eye; doubled draw calls on the render thread | settings order (section 10), shadow reuse, render scale, 72 Hz fallback |
| **Side-by-side seams or wrong sampling** (layout A) | no viewport-offset constant in the full-screen passes | decided: layout B with a copy (H8; runs 2-4) |
| **Exposure and other per-view state differ per eye** | exposure adapts per RenderScene; foliage statics; the cut flag | H7, the snapshot/restore in H4, the left/right brightness check in every run (run 3: eye R reuses eye L's exposure) |
| **AI relies on player attack events** | orcs block and dodge in reaction to player attacks we no longer start | P7/P8: the swing's stimuli through `FUN_026c7180` gave no reaction; the trigger is the target's `AIBlockTable` roll (re-combat-spawn.md 2), built as `SOMVR_COMBAT_REACT`; P9: defenders and berserkers have no table, a direct `incoming` (`FUN_029ac890`) made a defender block once, a forced notify gave an infantry nothing; shields block by themselves at damage time. Next: a VR chance per archetype with `FUN_029ac890` straight; else accept less blocking and tune by difficulty |
| **Hit streak does not advance** | the counter may be tied to the stock attack flow | the hit-counter functions; last resort the `Combat.Lock.HitCounter` value |
| **Nemesis broken by a wrong kill path** | kills outside the damage path lose `PlayerKiller` | every kill through G8; `Faction.Dump` asserts in the drills |
| **Achievements unlocked by tests** | Steam stays visible | block at the game's layer in test mode (D16); never patch the Steam layer |
| **The user's save** | `GameData.sav` and Nemesis state are irreplaceable | the fail-closed guard, the dev save, before/after hashes on every run |
| **Motion sickness from leftover game motion** | snap-align, root motion, landings, mounts; today the raw head base | the Root anchor (HG-1) and the velocity-fed follower (position only, velocity-continuous hand-offs), the anchor-cause assert and L1-L5, no game pitch, roll or yaw; vignettes and blinks as options |
| **The graph fights the replaced root motion** | turn clips, foot sliding, the FSM leaving 6 when the root delta is ours | RT LR3/LR4 in P10; our body yaw keeps `DeltaYaw` small; the leg match (LO-10); fallbacks: the slot hook post-call or the one-shot extra move |
| **Late frames at speed** | at 9 m/s a dropped frame is an 11 cm translation stutter that rotation-only reprojection cannot hide (L3) | the 80 Hz target matters more (D15); display-time extrapolation; the sprint path in the performance runs |
| **NPC collision for our own flings** | the grab-vault's v2 and climb flings fly Talion through or into orcs (LR7) | v1 on the stock vault keeps the game's collision; v2 moves the body to the landing point when the arc would hit the capsule, while the view flies the arc |
| **The built bow's fire command is a guess** | P9: 125 is `PC_Stealth`'s entry, the fire is an attack input in the aim mode's pools | the fire waits for the game's aim (a 125 press outside the aim mode would enter stealth); P10 finds the command (`wmhold PC_WraithRanged` + `cmd CombatKBM05`, then pad mode) |
| **Input injection fights the game's device logic** | SDL attach state, the KBM/pad UI switch | the binding-layer path (G3, held axes proven in run 3) needs no device; pad mode forced; menus as a passthrough pad (re-xr-input.md 3); the laser through the game's own CInterfaceMgr mouse calls, no device and no switch to KBM (re-ui-pointer.md; P8: hover and click work) |
| **One build only; Feral could patch it** | every address is for BuildID 87ec5d6f... | the BuildID check and byte-checked sites refuse to patch anything else; the binary copy in `reference/bin` |
| **GPU resets with a new Vulkan layer** | an amdgpu reset kills the desktop session's GPU work | Monado first; one game at a time; stop testing after a reset and tell the user |
| **Helpers stopped by safety classifiers** | earlier helpers that headed into anti-debug or the Steam layer were stopped | briefs leave both out; no debugger is needed (LD_PRELOAD and the layer) |

## 14. DECISIONS FOR THE USER

Each one has a default that the work follows until the user answers; every one of them stays a setting as well. The
user's decisions of 2026-10-20 (section 0) answered D1-D5, D8, D9 and D19.

| # | Decision | Options | Recommendation, and why |
|---|---|---|---|
| **D1** | Executions and finishers | (a) first person: your own heavy swing with the wraith charge, short slow-mo, the orc dies the game's way; (b) the stock cinematic paired animation; (c) both, chosen in settings | **Answered (user, 2026-10-20): "VR-native finishers"** = (a): you do the motion, the orc plays its animation, the view is never dragged. (b) stays an option (off), shown first person with the anchor frozen |
| **D2** | When the view leaves first person | (a) the rule of section 1 (stage view for paired actions over 1.5 s that move Talion more than 2 m or 90 deg); (b) never (always first person, anchor frozen); (c) always for paired actions | **Answered: (b) never** ("your view is never dragged"). The stage view stays an option (off); the fallback only catches unknown states, fading to the window |
| **D3** | Cutscenes and in-engine scenes | videos: theatre (no real alternative). In-engine scenes: (a) the window (stereo, framed, in the calm space); (b) the fixed camera (you stand in the scene, a fade at each cut); (c) around you (one vantage at 1:1, no cuts) | **Answered: "cutscenes play around you"** = (c) once its RE allows (NH-4: stereo on `FUN_009edba0`, culling); (a) until then; (b) an option |
| **D4** | Climbing | (a) gesture-driven auto-climb; (b) true hand-over-hand climbing | **Answered: "hybrid"**: hand over hand (built, `SOMVR_CLIMB=b`) plus the free-run button for the game's parkour and its stock climb |
| **D5** | Vault, dodge and Shadow Strike presentation | (a) blink; (b) arc (the view rides the move); the user's ideas: the wraith phase, the grab-vault | **Answered by the user's two ideas**: toward an orc the wraith phase first, the grab-vault as the skill move; away the dash; Shadow Strike the wraith dash; the blink an option for each. A new decision only if the user wants the grab-vault before the phase, or the arm pump as the only sprint (`SOMVR_LOCO_ARMS=free`) |
| **D6** | HUD | (a) on the body: health on the left wrist, the minimap on the forearm, the streak on the right wrist, popups placed in the world; (b) the whole HUD on a floating visor panel | **(a)**, (b) as an option. Your Hand of Fate 2 choice ("nothing floats in front of the player") and HL:A point to (a); nothing follows the head in either. The built quad is the interim |
| **D7** | Movement and turning defaults | movement relative to the head or to the left hand; snap 30 or 45 deg, or smooth | Every angle and mode is now set in the headset (the VR menu), so this picks only the first-boot default: **head, snap 45 deg** (HL:A's); the build has 30 (HoF2's) until you choose |
| **D8** | Counters | (a) physical only: the blade into the block bar at the impact, with Until You Fall's generous tolerances; (b) also an accessibility button that counters in the window | **Answered: (a)** ("parry by putting the sword in the way"); (b) stays an accessibility option, off |
| **D9** | Vignette defaults | off everywhere; or light for sprint and mounts, medium for auto-traversal and falls | **Answered by the speed principle: off everywhere**; every aid (vignette, blink, the long-fall fade) stays an option on the VR menu |
| **D10** | VR difficulty at Normal | damage taken x1.0 / x0.5 / x0.33; enemy attack delay x1.0 / x1.5 | **x0.5 and x1.5**, tuned after your first playtest. Real blocks take longer than a button press; in Hand of Fate 2 you asked for a third of the damage |
| **D11** | Decapitation | (a) only where the game's own moves sever (executions, some killing blows); (b) any fast hit on the neck | **(a)**. A decapitated captain is dead for good in the Nemesis system; (b) would make that routine |
| **D12** | Throwing the dagger (new; not in the flat game) | yes, with wraith recall after 1 s; no | **yes, after M5**, as an option. It fits the wraith, Blade & Sorcery shows how good it feels, and it costs little once the dagger can detach |
| **D13** | Riding: how the view turns | (a) car-style: the beast turns, you turn with it; (b) free: the beast turns under you | **(a)**. You steer it, so the turn is your input, and it is how riding works in other VR games; (b) stays an option. Hand steering is a further option |
| **D14** | Holding the sword | (a) it stays in your hand once drawn; (b) only while you hold the grip | **(a)**. Fights are long, and holding the grip for minutes tires the hand; a double squeeze anywhere draws it quickly |
| **D15** | Refresh rate target | 72 / 80 / 90 Hz | **80 Hz**. Two full pipelines per frame make 90 Hz uncertain at good settings; 80 Hz looks clearly smoother than 72 and leaves margin; at sprint speed every dropped frame shows (L3). Decided again after the M3 measurements |
| **D16** | Test runs and your Steam account | achievements: block them during test runs, or allow them; the dev save: a copy of your save with skills and abilities unlocked by console commands | **block achievements in test runs; use the dev save**. Tests must not unlock achievements on your account, and every power needs testing early, without anyone playing the story to unlock it |
| **D17** | Teleport-style movement (new, from HL:A) | offer Blink and Shift on the ground as options (off by default); or none | **offer them, off by default**. HL:A's lesson: players who cannot use stick motion still get a way to play; your own play stays continuous and 1:1. Free-running, climbing and fights stay the game's continuous motion either way |
| **D18** | Stealth kills (new) | (a) your own stab, G8 without the sync: a generic death with hit-stop and blood; (b) your stab triggers the stock synced kill, the victim plays its half, the anchor frozen | **VR-native either way** (your motion, the orc's animation, the view stays). **(a)** by default (no sync moves Talion; the HL:A-like answer), **(b)** where the shots show the generic death looks wrong |
| **D19** | In-engine cinematics (new, from HL:A) | the window now; research "stand in the scene" | **Answered ("cutscenes play around you")**: the research (NH-4) goes ahead, the window is the interim (D3) |
| **D20** | Knockback from staggers (new) | smoothed to at most 0.5 m (UYF-like, your HoF2 combat exception); or none | **0.5 m smoothed**, none as an option. The orc's hit causes it, it is short, and it tells you that you were hit |
