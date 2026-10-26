# Shadow of Mordor VR - the Half-Life: Alyx standard

The user, 2026-10-20: "Half-Life Alyx is by far the best VR game ever made. [...] the way it handles VR is way ahead
of it's time. The hand-movement-assist, the feel about grabbing things, the way your hands interact with the
environment, the places enemies spawn in relation to you, the warnings you get from enemies, the menus, etc. [...] It
never feels clunky, it never makes you motion sick, it's never hard to do the thing you are trying to do. Even the
loading screen is a 3D environment so you don't get pulled out of the immersion. [...] I want HL:A to be the standard
we hold this to." On snap turn: "It should be adjustable in-game".

This review holds `design-vr.md` (design and its 2026-10-20 status) and `re-xr-pipeline.md` 6-13 (what is built) to
that standard. It is docs only: nothing was launched, no `mod/` file changed. Section 1 lists HL:A's mechanics and why
they work, section 2 maps each one to SoM, section 3 ranks the gaps, section 4 lists proposed changes to
`design-vr.md`. Sources are listed at the end. "Built" means built and tested offline. Unless a run number is given,
it has not been seen in a headset with controllers. VR build 1b = `HIDE_HEAD=near`, `FALLBACK=auto`, `VIGNETTE=0.4`
(32b7199). Sizes: **S** = up to 1 helper-day offline, **M** = 2-3 days, **L** = about a week, **XL** = more than a
week or research first. Each item also needs one headless boot and the headset session to confirm it.

### The test this doc applies (six rules distilled from section 1)

1. **The view belongs to the player.** Nothing moves, turns, bobs or cuts the view without their input. When the game
   must take the body, the player keeps the view and the world explains what happens.
2. **Hands are believable first and exact second.** They stop at surfaces, close around what they hold, and the game
   quietly helps them reach what the player meant.
3. **Information lives in the world:** on the body, on the weapon, on the enemy. Nothing floats in front of the face.
4. **Threats are fair:** they come from where you can see them and warn before they hit. Sound covers the rest.
5. **There is never a void.** Menus, pauses, loads and deaths happen in a calm 3D place. Options are changed there, in
   the headset, and take effect at once.
6. **Comfort is the player's choice, made in-game.** The default is the gentlest one that still feels good.

## 1. What HL:A does, as mechanics

[V] = Valve's own words (commentary node, interview, AMA), [3P] = third party. Commentary items are close
paraphrases of a community transcript; only quoted text is verbatim.

### 1.1 Hands

- **Gloves only, no arms.** "We actually simulate invisible arms though, which connect from your hands back up to your
  HMD." They catch impossible actions such as "completely closing a drawer over your wrist". Playtesters "don't
  notice them missing" [V AMA]. Valve knows where the head and hands are, but not the arms, and a wrong arm "stands out
  an enormous amount" [V GI]. *Why:* presence. Nothing on screen contradicts what the player feels.
- **Hands that stop at surfaces.** The shown hand collides with the world, so it can part from the real one [3P TI].
  The user also describes a ghost of the real hand when it goes through a surface. Valve's material found here does
  not describe one. *Why:* the world feels solid, and the hand never vanishes into a wall.
- **Hands that conform.** The goal was hands "so natural you don't really think about them anymore". Valve uses "a
  sliding scale" from full finger tracking to "100% animation" [V UVR-hands]. On Index, the fingers follow the finger
  sensors (SteamVR skeletal input; general knowledge, not from the sources below).
- **Hand-movement assist.** At a door handle you only close your fist and the door opens; turning the handle yourself
  is optional [V UVR-hands]. Hands latch onto levers and knobs until released [3P TI]. When the off hand comes near the
  pistol it takes a natural bracing pose, and the flashlight turns to follow the aim [V CMT Flashlight Articulation].
  *Why:* the player's intent wins over the player's precision, so "it's never hard to do the thing you are trying to
  do".

### 1.2 Gravity gloves, the highlight, catching

- **Three steps, each with its own sound.** Point and the target glows, lock it, then flick to pull it [3P RTV-XR].
  Key items get small lights instead of bright outlines [3P RTV-XR].
- **Why gloves exist:** reaching and bending in VR is tiring, and real rooms are small [V CMT Gravity Glove Training
  1]. The pull "reduced the amount of teleportation... you can pick up three things without having to move" [V TG].
- **The catch.** The object has to be caught, not auto-attached, which feels capable [V CMT Training 1]. Flight time is
  always the same, so players build a rhythm. Small impulses steer the object to the moving hand. A generous catch
  range and a subtle haptic pulse make the catch near-certain even when distracted [V CMT Training 2]. With a separate
  pull button, testers used the gloves far less [3P COW-gloves].

### 1.3 Holsters and body slots

- **Ammo over the shoulder.** Reaching over the shoulder gives a magazine for the gun in hand. Grenades and syringes go
  in two wrist pockets. The weapon hand gets a quick weapon select [V AMA; 3P DES]. *Why:* there is no inventory
  screen, the body is the inventory, and the slots can be found without looking.

### 1.4 Weapons, the weapon wheel

- **One-handed weapons.** Every weapon needs one hand only, so the player always has a free hand [V AMA]. Two-handed
  guns felt wrong because nothing joins the hands [V CMT Shotgun Design 1].
- **Physical reloads.** Eject, take a magazine from the backpack, insert, chamber. Players blamed their own fumbles
  and called the reload a highlight [V CMT Pistol Reloading]. Upgrades never remove a core interaction [V CMT Weapon
  Upgrades]. Melee was cut because swings against no resistance were unpredictable [V CMT No Crowbar?].
- **The weapon wheel.** Hold, move the hand to the icon, release [3P, forum description].

### 1.5 Diegetic HUD, no floating HUD

- **Status on the gear.** Health, ammo and resin show on the back of the off-hand glove. A wristwatch was tried first
  and moved, because wrists aren't drawn [3P COW-gloves]. Pistol magazines light per bullet and go dark when empty
  [3P RTV-XR]. A HUD exists only on the desktop spectator view [V CMT Spectator HUD].

### 1.6 The environment

- **Interactivity follows player behaviour.** Valve built "one 'room' at a time": "everyone looks at the vent, everyone
  pokes their head under the desk", so the effort went there [V RTV-Walker]. Testers spent up to 30 minutes with one
  object, so the interactivity budget grew [V CMT Budget].
- **Analog controls.** Players expect to grab analog controls, so the hand's position drives the animation [V CMT Anim
  Interactables]. Doors open both ways, because VR gives no push/pull feedback [3P ENG paraphrase].

### 1.7 Locomotion

- **Four modes:** Blink is the default: "Teleport to destinations with a brief screen fade. (Most comfortable)". The
  others are Shift (fast linear movement), Continuous (head) and Continuous Hand [3P NW, the settings text].
- **Teleport made to feel like walking.** Players said "I teleported to the desk", so the marker became holographic
  feet, with footsteps along the path. Players then said they walked [V CMT Teleporting & Immersion]. Teleport uses one
  standard body size, because tall testers hit low pipes [V PCG].
- **No jumping.** You "mantle up rather than jump up". Short gaps use a teleport-style hop, even in continuous mode
  [V AMA].
- **The AI sees teleports.** A teleport leaves invisible markers along its path, so fights work the same in every mode
  [V CMT AI in VR].
- **Ladders.** Continuous players climb hand over hand. Teleport players get a short timed move [V CMT Ladders].

### 1.8 Turning and comfort options

- **Turning.** Update 1.1 (March 2020) added continuous turning with speed options, renamed Quick Turn to Snap Turn,
  and allowed controller turning to be switched off [3P RTV-1.1]. Update 1.2 made the weapon hand independent of the
  movement hand and clarified Height Adjust [3P RTV-1.2].
- **Options in the headset.** Every one of these is set in the game's own menus, in VR, and applies at once.

### 1.9 The camera is never taken

- **No forced motion.** "moving the player without their input in VR didn't work very well" [V AMA]. A cable platform
  that made testers sick lost its motion [V CMT Chasm Puzzle].
- **Scenes play around you.** Characters are choreographed around the player and never push them aside; the room
  keeps space, or the player is held back until a character has moved [V CMT Character Choreography]. Even the ending
  is the player gripping two handles [V CMT G-Man Room].
- **Vehicles.** Elevators are "one of the few ways" to direct the view: slow, with the car filling most of the view
  [V CMT Elevator Comfort]. The van keeps its interior dominant [V CMT Van Crash].

### 1.10 Enemies built for VR

- **Slower, fairer soldiers.** HL2's soldier speed was "simply too overwhelming in VR", and players hated being shot.
  Soldiers keep their distance, mostly don't fire while running, and flush players out of cover. The first fights have
  no grenades [V CMT Fighting Combine].
- **Learn before pressure.** New enemies are introduced at a distance, never under pressure [V CMT Combine Sighting].
  The armoured headcrab shows its weak spot before it jumps: "really hard to learn something new while under attack"
  [V CMT Armored Headcrabs].
- **Sound as a warning.** Manhacks alarm when deployed, barnacles make idle sounds above you, and the Strider has a
  lock-on warning before it fires [V CMT audio nodes]. Players react badly to damage that "seemed unfair" [V CMT
  Strider Sounds].
- **No jump scares.** Fast zombies and fast headcrabs were cut: "jump scares are terrible in VR" [V GS].
- **Where threats appear.** The user observes that enemies spawn in front of the player and away from walls behind.
  Valve's material found here says so only indirectly (introductions in view and at a distance). No explicit "never
  behind" rule was found.

### 1.11 Pacing

- **Short sessions and downtime.** Planned for 30-45 minute sessions; more downtime was added, after which testers
  played for hours [V CMT Player Comfort & Pacing]. Gaps between fights, some fights removed: players "grow weary of
  being shot at without any downtime" [V CMT Combat Pacing].
- **VR players are slower.** Valve noticed "how much slower most players were in VR compared to WASD" [V ASE]. Long crouching was disliked, so a
  ceiling was raised [V CMT Crouching].

### 1.12 Menus and loading in 3D

- **Menus.** They work with a laser or by direct touch [3P RTV-XR]. The user names the menus among HL:A's strengths.
  No Valve source on the menus' design was found.
- **Loading.** Loads show a calm 3D space (the user; HoF2's user-approved "Alyx-style calm 3D spaces", with a progress
  indicator). No Valve comment on loads was found.

### 1.13 Seated, standing, accessibility

- **Crouch and height.** Crouching is physical by default. Height Adjust offers crouch and stand toggles for seated
  play [3P search excerpt, unverified wording].
- **Single-controller mode.** It is "just for accessibility reasons" [V UVR-1hand]. Reloading means touching the
  weapon to the shoulder and pressing a button [V CMT Single Controller Mode].
- **Hands and subtitles.** Left-handed options; subtitles can go on the spectator view only [3P RTV-1.2].
- **Process** (it matches our "look like a player" rule). Playtests start when 10-15 minutes of game exist [V CMT
  Budget]. "In intense situations, players frequently fail [to] notice or retain critical information" [V CMT Reviver
  Foreshadowing].

## 2. The mapping to Shadow of Mordor

Each item has four lines: **Built** (what we have), **Gap** (what falls short), **Fix** (what to do) and **Hooks**
(what it needs). `HG-n` = the gap's number in section 3.

### 2.1 Hands (1.1)

- **Built:**
  - `SOMVR_ARMS=1` puts Talion's real arms on the controllers through our two-bone IK at G11. P8 proved the write
    point at 0.0 cm.
  - The drawn sword follows `R_Weapon`, and the blade hits from the bone grip, so what you see is what hits (6.6).
  - Hit haptics (6.4) and the climb's hold, which moves the body so the hand stays where it grabbed (12.1).
- **Gap:**
  - Hands and blade go through walls: the IK spring and the wall clamp are not built (6.6 "Not built").
  - There is no ghost of the real hand.
  - The fingers play the game's animation, whatever the player's fingers do.
  - There is no grab assist outside climbing.
  - HL:A's gloves-only look cannot be copied (see Compromise).
- **Fix:**
  - **(a) Wall stop.** Each frame, cast a ray from the shoulder to the IK target. A hit pulls the target back to the
    hit minus 3 cm.
  - **(b) Blade.** A base-to-tip ray (G21) clamps the blade's drawn pose and voids hits beyond it. Add a clank: a
    haptic, plus the 3D sound at the contact.
  - **(c) Ghost hand.** When the shown hand is more than 4 cm from the tracked one, the layer draws a faint wraith-blue
    hand sprite at the real grip. It is a quad facing the head and fades in over 0.1 s. Like the pointer's dot, it
    needs no mesh. The wraith look fits the lore and reads as "your real hand".
  - **(d) Reach.** When the controller is past Talion's reach (the clamp `a + b - 0.5`), the ghost shows too.
  - **(e) Assists:**
    - while gripped, a hand within 25 cm of an orc's neck or shoulder bone latches onto it (grab, drain, brand);
    - at a block, the drawn blade eases up to 5 cm onto the bar (visual only, the judging stays geometric);
    - the bow's nock window (15 cm, built in 13);
    - the dagger draw zone (2.3).
- **Hooks:** `CL_RAYCAST` with the Talion filter (12.3, built for the climb); G11 (6.6); the layer quad pattern
  (10.1); 3D sound posts `FUN_008c9b80`/`FUN_008c9840` (re-combat-items.md 5; P7 got playing ids). The clank's event
  name is data (`AudioBlockName`, RT).
- **Compromise:** SoM draws Talion's whole body. His arms cannot be hidden: hiding each of the 17 pieces hides nothing
  visible (P7), and the bone collapse would smear the sleeves. So the arms stay, with HL:A's lesson applied: plausible
  elbows (the elbow hint and its turn limit, built), and arms that stop at walls rather than through them. The
  both-eye review judges the elbows in view. If they look wrong in the headset, hiding the sleeves needs RE: a
  per-material or per-mesh-section hide.
- **Finger poses** (HG-10):
  - **The fingers.** Write the finger joints in the same G11 pose (right 114-118 and 120-129, left 92-96 and 98-107,
    children of W: re-arms-ik.md 2). The grip value curls the three lower fingers, the trigger the index, and touch on
    A/B/the stick the thumb.
  - **Per-object targets:** the sword fist (the game's own drawn-sword pose), the bow grip, the dagger, an open palm on
    a wall hold, a claw on an orc's neck.
  - **Where the target poses come from:** sample them once from the game's own animations with `SampleNode`
    (IEngineModel +0x168, re-combat-vr.md 3.2; P7: model space).
  - HoF2 precedent: `HandOfFate2/src/HoF2VR/VR/FingerCurl.cs` (the user asked for it on 10-11).

### 2.2 Gravity gloves and catching (1.2)

- **Built:** B = `Activate` (the stock context action: herbs, doors, ropes, cages, towers). It works only within the
  game's own prompt range.
- **Gap:** there is no reach, highlight or catch. In HoF2 the user dropped gravity pulls ("you don't pick much up in
  this game"). SoM has more pickups (herbs, artifacts, ithildin, plants), but still few.
- **Fix (should-have):**
  - **Touch-to-use (SH-5).** With an `Activate` prompt up, an open hand reaching out and squeezing = `Activate`, so
    no button is needed. Later the hand must also be near the prompt's object.
  - **Gravity pull for collectibles (nice-to-have).** The palm points at a collectible within 8 m and a glow sprite
    marks it. A flick fires the stock collect, a sprite flies to the hand, and a haptic pulse plays at the catch.
  - **Catching (nice-to-have).** The thrown dagger returns to an open hand. The catch counts with the grip within
    20 cm, with a haptic pulse (D12).
- **Hooks:** the prompt casters and diamonds (re-hud.md 4.2-4.3). Collectibles carry NavMarker icons (re-hud.md 3.4),
  so G19 would give their positions. **Needs RE:** the prompt raiser's target object (re-hud.md 4.5), whether a collect
  works out of the prompt's range, and whether it plays a pick-up animation, which would be forced motion.

### 2.3 Holsters and body slots (1.3)

- **Built:**
  - The bow forms by closing the string hand at the bow hand or by the left trigger (13).
  - The dagger's move to `L_Weapon` and back works by the game's own re-attach (P7).
- **Gap:**
  - The player cannot draw or sheathe Urfael: nothing in somvr calls the game's draw.
  - The stock melee is never pressed (the deny list), so the sword can stay on the back while the player swings, and
    the hits then land from an empty fist. P8 saw it sheathed.
  - The dagger draw is not in somvr.
- **Fix:**
  - **Slots in the head's frame,** with the body's lagged yaw (HL:A's over-the-shoulder slot works from the head, not
    from the drawn body):
    - Urfael: right grip within 20 cm of a point 15 cm behind, 10 cm right of and 10 cm below the head base = draw;
      squeezing there with the sword = sheathe;
    - a double squeeze anywhere = quick draw (design 9);
    - Acharn: left grip at the left hip.
  - A haptic tick on entering a slot, the game's unsheathe sound, and the sword stays in the hand (D14).
- **Hooks:** model keys `SHEATH`/`UNSHEATH` -> `FUN_023116f0(inv, activeItem, 1/0)`; the dagger `FUN_0231e2e0` with
  `+0x34` = `L_Weapon` (re-combat-items.md 3, 234-264). RT: whether `FUN_023116f0` from our code plays the draw
  animation (the arms' IK covers it), and whether the game re-sheathes on its own out of combat. **HG-7.**

### 2.4 Weapons and the wheel (1.4)

- **Built:** the sword in the right hand and the bow with a physical nock and draw (13). This is HL:A's reload lesson:
  nocking is the skill.
- **Gap:** none in weapons themselves. Every weapon has a slot, so no weapon wheel is needed. The stock d-pad quick
  actions (design 9: "X hold = quick menu") have no VR form yet.
- **Fix:** an HL:A-style wheel. Hold X, four icons ring the left hand, move the hand onto one and release = its stock
  command. Each icon gives a haptic tick on hover. *Hooks:* G3, layer quads with an icon atlas; RT which commands are
  the d-pad's (re-xr-input.md 3.6). **SH-6.**
- **Compromise:** the bow needs two hands, unlike HL:A's one-handed rule. The free left hand comes back as soon as the
  bow fades (13's exit).

### 2.5 Diegetic HUD (1.5)

- **Built:** `SOMVR_HUD=quad` puts the whole Scaleform HUD on one world-locked quad, 1.2 x 0.675 m, 1.5 m ahead (run
  7; `som-vr.conf`'s normal is `world`). That is a floating HUD, which HL:A never shows.
- **Gap:**
  - The world markers are drawn into that HUD at one eye's screen positions, so on a quad 1.5 m away they do not sit on
    the orcs. This includes the counter icon over an attacker, which is SoM's main warning (re-hud.md 4.4: it is a
    combat marker, not a prompt).
  - Health, focus, arrows and the streak are not on the body.
  - The quad is placed once, so a menu opened facing the other way is behind the player.
- **Fix, in order:**
  1. **Left wrist.** Health, focus and elf-shot go on the inside of the left wrist: a tile cut from the HUD texture by
     `imageRect`, shown when the wrist faces the eyes and for 3 s after a change. The user chose the same in HoF2
     ("health on the back of the glove").
  2. **The arrow count on the bow,** a tile at the bow grip while aiming.
  3. **The streak.** Urfael's runes glow with the streak (a ClientFX through the `WEAPONFX` path, re-combat-items.md
     2), plus a right-wrist tile.
  4. **Markers in stereo.** Draw markers at their world positions, per eye, as sprites (G19), and hide the HUD's
     copies. The counter icon over the attacker goes first, until HG-3's bars replace its role.
  5. **Popups.** Objectives and XP popups are world-locked, low in front, and fade after 4 s (design 7).
  6. **What stays on the quad.** It keeps only the in-world modal screens (pause, map, skills, Nemesis). It is re-placed
     2 m in front of the head whenever one opens (G17).
- **Hooks:** H11 (built); the quad by `imageRect` (re-hud.md 1.4); the tile rects (**RT**: P4's HUD-only frame diff;
  `UI.PrintHUDElementList` prints nothing, re-hud.md 4.1); G19 `NAV_EACH` + `FUN_00ebc700` (static), the hide path
  (re-hud.md 3.5, med); G17 for the re-placement. **HG-8.**
- **Interim** (until the tiles exist): the HUD quad 0.9 m ahead, 35 deg below the horizon, 0.6 m wide, and re-placed
  at every snap turn and every menu opening. Smaller and lower is less "floating".

### 2.6 The environment (1.6)

- **Built:**
  - Interaction goes through B = `Activate`.
  - The bow shoots the game's own arrows at anything the game allows (grog barrels, fires, fly nests).
  - The blade's hits run through the game's own reactions (6.3).
- **Gap:** there is no touch. The blade passes through stone, wood and props. Artifacts are turned with the stick in a
  flat 3D UI scene.
- **Fix:**
  - **Touch-to-use** (2.2).
  - **The blade clanks on the world** (2.1b).
  - **Every object with an `Activate` prompt takes a touch:** towers, cages, ropes, doors.
  - **(Nice-to-have) Hold the artifact in your hand:** the artifact's UI scene in stereo near the hand (re-hud.md 2.5,
    the stretch goal).
  - **(Nice-to-have) Blade cuts:** ropes, meat sacks and barrels cut by the blade. Needs RE: whether props have a
    damage path at all.
- **Compromise:** SoM's world was not built for hands, and we cannot add physics props (no meshes of ours). The part
  of HL:A we can honour is "what you reach for responds": prompts take a touch, stone stops the blade, orcs react to
  it.

### 2.7 Locomotion (1.7)

- **Built:**
  - Head- or hand-relative stick movement through the game's own `PlayerMovement` and its collision.
  - The sprint latch, i.e. free-running.
  - The room-scale body follow (`SOMVR_FOLLOW=1`, 6.1; needs a walking player).
  - Hand-over-hand climbing (`SOMVR_CLIMB=b`, 12): HL:A's ladder model.
  - The vignette (11).
- **Gap:**
  - `design-vr.md` 2 rules out teleport ("it would break free-running, climbing, stealth and the fight circle").
    Players who cannot use stick motion therefore have no way to play.
  - The game's automatic traversal (vault, mantle, wall-run, auto-climb, leaps, drops) moves the view along animation
    paths (2.9).
- **Fix:**
  - **Blink (an option, off by default).** An arc from the left hand lands on walkable ground within 6 m, checked by a
    ray (normal within 35 deg of up). On release the view fades out over 80 ms. Talion then moves along the straight
    path through the game's collision, at most 30 cm a frame (the follow's limit), so the AI and stealth see a fast
    walk, as with HL:A's footstep markers. The view fades back in over 120 ms. A blocked path stops where the
    collision stops it.
  - **Shift:** the same move without the fade, over 0.2 s.
  - **Leaps:** the stock leap at an edge as a blink: HL:A's teleport-style hop for gaps, even in continuous mode.
- **Hooks:** the PM hook's one-shot move (6.1, 12.2; built), `CL_RAYCAST`, the fade quad (6.8) or the snap blink (11).
  **RT:** the walkable test (the surface record), and how the game's stealth and the fight circle take a 0.2 s move of
  4 m. **SH-3.**
- **Compromise:** free-running is the game. Blink and Shift serve exploration and ground fights. Climbing and parkour
  stay physical or stick-driven.

### 2.8 Turning and comfort options (1.8)

- **Built:**
  - snap (`SOMVR_SNAP_DEG`, default 30, HoF2's) and smooth turns (120 deg/s, eased);
  - the snap blink;
  - one vignette strength on the measured artificial speed;
  - seated height.
  All of it is read once at start from the environment or `som-vr.conf` ("This file is read only when the game starts
  in VR").
- **Gap:**
  - **The user's explicit ask: change it in-game.** Nothing can be changed in the headset.
  - There is no "turning off" option (HL:A 1.1 has one).
  - The vignette has no per-trigger levels.
  - 30 vs 45 deg (D7) is still open.
- **Fix: the VR settings panel** (HG-4). It opens by holding the left menu button and also shows as a "VR" tile beside
  the pause menu (design 7). It is a layer panel driven by the built laser, with a finger poke as a click too (HL:A and
  HoF2 both touch menus). Live entries:
  - snap angle (15 / 22.5 / 30 / 45 / 60 / 90), snap / smooth / off, smooth speed (60-180 deg/s), the snap blink;
  - vignette (off / light / medium / strong, per trigger later);
  - movement direction (head / hand); Blink / Shift / Continuous (SH-3);
  - standing / seated, and "re-measure my height";
  - haptics %; dominant hand.
  Every change applies at once, with a haptic, and is written to `som-vr.conf`, which becomes the panel's save file.
- **Hooks:**
  - the pointer (10, built);
  - a label atlas made offline (PNG; no font code at runtime; the pointer's texture pattern);
  - a settings block in the ABI (layer -> game, a seqlock like `somvr_comfort`);
  - the game side re-reads these switches live (today only at start);
  - `scripts/som-vr.sh`'s conf parser for the writer.
  HoF2 precedent: `HandOfFate2/src/HoF2VR/VR/VRSettings.cs`, `TouchSlider.cs`.

### 2.9 The camera is never taken (1.9)

This is where SoM differs most from HL:A, and where the most honest compromises are.

- **Built:**
  - `SOMVR_BASE=head` places the view on Talion's `Head` node every frame, raw: no low-pass, no Root mode (6.1;
    `somvr_eyes.c` has no smoothing). So every animation's head translation is in the view: the walk bob, attack
    anticipation, staggers and knockdowns. That is the HoF2 "moved without input" complaint.
  - `SOMVR_FALLBACK=auto` (on in VR build 1b) fades to the **game camera as the base** for cinematics, presentations,
    syncs (over 600 ms or 75 cm), struggles, mounts, attach, climbs, death, photo mode and the kill cam. After the fade,
    the game camera's own pans and cuts reach the view (6.8: the frozen stage view is not built).
- **HL:A would do neither.** The answer for each moment:

| SoM moment | Today (build 1b) | Most HL:A-like answer | Hooks / RE | Gap |
|---|---|---|---|---|
| walking, idle, fighting | raw `Head` node (bob, lunges) | **Root anchor** (design 1): Talion's root + the calibrated eye offset, Y low-passed 80 ms; the head node only in traversal | root = `PlayerBody+0x1b8` (design 1), the H4 body read exists; run 3's eye calibration | HG-1 |
| staggers, hit recoils | raw head node | Root anchor; the view moves at most 0.5 m, smoothed (design 3), or not at all (an option); a red edge pulse | HG-1 | HG-1 |
| knockdowns | rides the head to the ground | the view stays at the real head height and dims red until he is up (design 3) | **RT:** the knockdown recoil's TAG or recoil state | HG-2 |
| per-attack combat cameras, the kill cam | third person (`FinalBlow`) | no camera: stay first person, keep only the slow-mo | `Combat.EnableCameraOverride` `0x4c44298` = 0 ("per attack camera overrides", re-strings-map.md 84); **RT:** whether it also stops `FinalBlow` | HG-2 |
| executions | the stock paired kill is no longer reachable (the stock melee is off); a fallback if injected | **your own swing** (D1 a): wraith charge + a heavy hit -> G8 with the execution record, `forceKill`, 0.3x slow-mo for 0.5 s; no paired animation | G8 (built), G15, **RT:** the record, `forceKill` (P7: not tried) | SH-1 |
| short syncs (counters in place) | first person on the raw head | Root anchor, frozen for the sync (no follow), hands stay the player's | the sync read (`FB_SYNC`, built) | HG-2 |
| stealth kills | (fallback if long) | **your own stab** with Acharn -> G8 with the stealth-kill record **without** the sync: a generic death (design 3), with hit-stop and blood; review the look | G8; **RT:** the victim's death in shots | SH-8 |
| drains, brands, interrogations, grabs | third person | first person, the anchor frozen at the start, the orc held at arm's length by the latched hand (2.1e); if the root ended more than 40 cm away, a 120 ms blink re-seats the view | G14 (read built; per-sync duration and travel logged by 6.8) | SH-8 |
| struggles (grabbed, pinned) | third person | first person, frozen anchor, the orc in your face; shake or shove to break free (design 3) | `PlayerCombat+0x8d8/+0x8e0` (read built) | HG-2 |
| automatic traversal (vault, mantle, wall-run, auto-climb, ledge pull-up, drops) | third person at FSM 9 (`climb`), else raw head | Head mode with position-only low-pass (Y 80 ms, XZ 40 ms) + medium vignette; a vault over an orc = a blink (D5 a) | FSM `P+0x7f0` (read built); **RT:** T1/T2 (which states, the TAG names) | HG-2 |
| our hand-over-hand climb | the fallback's `climb` ends it (12.1: "third person shown by `SOMVR_FALLBACK`") | first person: the player's own hands move the body (HL:A's ladders); the fallback's `climb` condition off while `SOMVR_CLIMB` holds | 6.8 + 12 | HG-2 |
| mounts (caragor, graug) | third person | **Seat mode** (design 5): first person on the saddle, Y 120/200 ms, XZ 30 ms, no pitch or roll, car-style yaw with a light vignette; the beast's head and back fill the view like HL:A's elevator car; on/off = a blink | `P+0x1d8` (read built), `*(P+0x1d8)+0x14` quat; **RT:** the saddle node (T9) | SH-2 |
| scripted level cameras (dialogue framings, reveals) | third person (`scripted`) | ignore the camera, stay first person; a soft light cue toward the subject; player input off if the camera says so | `cam+0x390` (read built); RT `KeyframedCameraDisablePlayer` (re-firstperson.md 7) | SH-4 |
| captain presentations (time paused) | third person (`pres`) | in the world when the captain is in front and close, with the name card beside him; else the window (design 6) | G16 `pcp+0x90`; **RT:** the pose `cam+0x340` | SH-4 |
| in-engine cinematics | rendered mono (one view, no H4, re-hud.md 1.3), so already shown as a floating flat screen in black | the **window** (design D3 a), inside the calm space (2.12). The ideal, "stand in the scene" (one vantage at 1:1 for the whole scene, no cuts), **needs RE:** stereo on `FUN_009edba0` and culling/streaming for a viewpoint that is not the camera's | re-hud.md 1.3, re-ui-effects.md 4 | SH-4 / NH-4 |
| story videos (WebM) | the theatre picture (`uiTex`) on the mono quad | the theatre inside the calm space (no alternative) | re-ui-effects.md 4 | HG-9 |
| death, the Nemesis promotion | third person (`PreDeath*` tags) | **first person to the end:** the anchor frozen, a 1 s grey-out facing the killer; then the promotion in the window | the death tags (built); the window | HG-2 |

- **The fallback's future:** a safety net for **unknown** states only, logged with the state's name. It fades to the
  window in the calm space (2.12), not to a moving third-person camera in the world. D2's "(b) never" becomes the
  target and the stage view becomes the user's option.

### 2.10 Enemies built for VR (1.10)

- **SoM:**
  - freeflow fights against 5-30 orcs from all sides;
  - the counter icon over the attacker (counterable / unblockable);
  - barks; archers; captains with traits; alarms and reinforcements;
  - the fight circle (G9) with a `Camera` score that prefers orcs on screen (re-gameplay.md 6; predicates
    `AttackerOnScreen`, `MustBeOffScreen`).
- **Built:**
  - blade hits through `Combat_DealHit`;
  - the orcs' block/dodge roll for a VR swing (`SOMVR_COMBAT_REACT`, 6.3.1);
  - hit-stop;
  - the audio listener at the head (`SOMVR_AUDIO=head`, run 7).
- **Gap:**
  - **The player cannot defend.** Blocks, parries, dodges, block bars and ground arrows are not built (6.3 "Not yet
    built").
  - The counter icon sits at the wrong place (2.5).
  - Off-view cues are only the game's own sounds.
  - The fight circle is untuned (G9/G10 not built), so attacks come from behind at flat-game rates.
  - G2 (gameplay camera = head) is not built, so the `Camera` score rates the game's orbit camera's screen, not the
    player's view.
  - Damage is not scaled (D10).
- **Fix:**
  - **(1) Defence and telegraphs (HG-3).**
    - G4 logs every orc attack start.
    - Counterable blows get a **block bar**: a world quad 40 cm ahead toward the orc, across the blow's path. Its
      place is the weapon bone sampled at the impact time, or the direction class.
    - Unblockable blows get a **ground arrow** that fills red until the impact.
    - The impact is judged in G6/G7 (design 3's table).
    - A parry = the orc's parry recoil, and it is open for 1 s.
  - **(2) Front-first fight circle (HG-5).**
    - Tuning at Normal: one inner-ring attacker, lockout +50 %, `AttackDelayScalar` x1.5, `DoubleAttackChance` x0.5,
      at most 2 archers shooting, damage x0.5 (D10).
    - **The VR rule:** an orc outside a 110 deg cone around the head may not start an attack until it has been
      announced for at least 1.0 s. The announcement is a positional warning sound at the orc (Wwise 3D), an edge glow
      on that side, and a haptic tick on that side's controller. The grant is vetoed meanwhile, as re-gameplay.md 6
      proposes ("veto ... until the player has seen the warning"). The HoF2 user asked for "LOTS of extra warning" for
      off-view attacks.
  - **(3) Arrivals (SH-9).** Reinforcements and captains arriving within 15 m behind the player get a positional cue
    and a marker. Moving the spawns themselves **needs RE** (`AISpawnMgr`, `AlarmLevelChanger`: names only).
  - **(4) The play area (nice-to-have).** Near the guardian edge (`xrGetReferenceSpaceBoundsRect`), prefer attackers
    on the room's open side, so nobody turns into a wall.
  - Captain introductions are SoM's own "introduce at a distance" moment. Keep them in the world (2.9).
- **Hooks:**
  - G4 `FUN_026eb380`, key times `Combat+0xcc/+0xd4/+0xd6` (P7/P8: within a frame), `SampleNode`, G5/G6/G7 (static,
    re-combat-vr.md 3, re-combat.md 2-3), the parry recoil `FUN_02810af0`;
  - G9/G10 `FUN_028cd7a0`, `FUN_028cd4b0`, `FUN_028ccc90`, `FUN_02114ec0` (static);
  - the `Camera` term and the veto point: re-xr-pipeline.md 42.3 (the score's only call `0x28d7d31`; a negative
    score after it = never granted); the AI's camera copy `CPlayerObj+0xb94` (42.4);
  - G2 `cam+0x338` (design 0, not built);
  - Wwise posts (re-combat-items.md 5); P9 for shield orcs and captains.

### 2.11 Pacing (1.11)

- **SoM:** an open world. The player picks fights, but a fight snowballs through alarms, reinforcements and captains
  joining. We cannot restructure missions.
- **Fix:**
  - HG-5's tuning (fewer attackers, longer lockouts) is the pacing lever inside fights.
  - **RT:** whether the fight circle caps the number of engaged melee orcs; reinforcement rate and alarm level **need
    RE**.
  - Long crouching: physical crouch plus a toggle, so nobody has to stay crouched for minutes (HL:A raised a ceiling;
    we give the toggle).
  - Breaks are already the player's: the game saves often, and quitting from the pause menu is always possible.

### 2.12 Menus in 3D (1.12)

- **Built:**
  - The pause menu, map, skills and Nemesis screens are in-world frames: the world in stereo with the menu on the HUD
    quad (re-hud.md 1.3, H11). That is already HL:A-like.
  - The front end, modal screens and loading are the mono quad: a flat 16:9 screen in black.
  - The laser with haptic ticks on hover and click (10), and the pad passthrough.
- **Gap:**
  - the black void around the mono quad;
  - no settings in the headset (2.8);
  - in-world menus can open behind the player (2.5);
  - the Nemesis screen is flat.
- **Fix:**
  - **(1) The calm space (HG-9),** in two phases:
    - phase 1: a fixed 360 degree backdrop captured once in a test run at a calm spot (e.g. the Udun plain at dusk). The
      capture needs no new game code: six `SOMVR_XR_DUMP` shots with the head turned by xrsim, stitched offline. It is
      shown by the layer as a cube layer if WiVRn has one, else six quads, dimmed to about 30 %;
    - phase 2: the last place you stood in (design 6, six extra views through H4; SH-10).
  - **(2) The menu panel** 2.5 m ahead, curved if cylinder layers exist, world-locked where you looked when it opened.
  - **(3) The VR settings panel** (2.8).
  - **(4) A finger poke on any panel = click.**
  - (Nice-to-have) the Nemesis diorama (re-hud.md 2.4, 2.5).
- **Hooks:** the layer only. **RT:** which composition-layer extensions WiVRn offers (cube, cylinder, equirect).
  Design 6 already asks for that "in M1": log it on the next boot.

### 2.13 Loading in 3D (1.12)

- **Built:** loading screens are mono frames on the mono quad.
- **Gap:**
  - The void, as in 2.12.
  - **Unknown (RT):** whether the game keeps presenting during a load. If it stalls, WiVRn shows the last frame
    reprojected, or black: the one thing HL:A never does.
- **Fix (HG-9):**
  - the calm space of 2.12 behind the loading panel;
  - the tip text on a small panel low in front;
  - a progress light (HoF2's user asked for one: `LimboCamp.cs`, `WaitBoard.cs`);
  - **the layer keeps the frames going.** When no game present has come for 100 ms, a layer thread submits its own
    frames (the space, the last menu image, the progress light) with real head tracking until the game presents again.
    It shares the layer's frame-call lock.
- **Hooks:** the layer's frame loop (4.3, 6.2). **RT:** the longest present gap at a load (the layer already logs
  `xrWaitFrame` timing).

### 2.14 Seated and standing, crouch (1.13)

- **Built:** `SOMVR_SEATED` + `SOMVR_HEIGHT_CM` (11); 1:1 scale on the real floor (design 0); the right stick down
  pulses `Crouch`.
- **Gap:**
  - Physical crouch is designed but not built (design 2: below 75 % of the standing eye height for 0.2 s = `Crouch`,
    above 82 % = up; `somvr_ctl.c` only has the stick pulse).
  - Seated players cannot reach the floor (ground executions, low targets).
- **Fix (HG-11):**
  - physical crouch as designed;
  - a seated reach assist: when seated, the ground-execution stab counts at knee height, and touch-to-use needs no
    floor reach;
  - re-measure the height from the panel.
- **Hooks:** the rig's calibrated standing height; **RT:** whether `Crouch` is a toggle or a hold (6.4 runtime list).

### 2.15 Accessibility (1.13)

- **Built:** haptics % (`SOMVR_HAPTICS`), move direction head/hand.
- **Gap:**
  - left-handed mirroring (design 8) is not built;
  - subtitles ride the HUD quad or the menu quad;
  - the button counter (D8 b) is not built;
  - there is no one-handed mode.
- **Fix:**
  - **left-handed** (SH-7): mirror the controls map, the arms and the bow hands;
  - **subtitles** as a tile 1.8 m away, 12 deg below the view centre, following the view lazily (0.3 s), sizes
    S/M/L (SH-7);
  - **the button counter** once HG-3's windows exist (S);
  - **one-handed mode** (nice-to-have, L): the bow from the sword hand by a button and the head's aim, the dagger by a
    button.

### 2.16 The honest compromises in one place

- **The whole body.** Talion's arms stay (2.1). Gloves-only is impossible without hiding mesh parts (needs RE).
- **Paired animations.** Where the kill itself can be ours, avoid them (executions, stealth kills). Otherwise freeze
  the anchor and keep the hands; never a camera.
- **Mounts.** HL:A has no mounts. Its vehicle rules apply: a car that fills the view, slow and smoothed motion, and the
  player steering.
- **Cinematics.** The scenes are authored for a cutting camera, so they go in a window in a calm space. "Around you"
  needs RE and is an experiment (NH-4). Videos can only be a theatre.
- **Fights.** SoM is open-world fights at scale. The fight circle becomes the stage director, since encounters cannot
  be staged.
- **Few interactables, no props of ours.** So: touch-to-use, and a blade that clanks.
- **Teleport.** Only an option (Blink and Shift on the ground). Free-running stays the game.

## 3. The HL:A gap list for the next builds

Ranked within each tier. Sizes as in the header. Every item is off by default until its boot passes, then on in the
next VR build.

### 3.1 Must-have before a user release

| # | Item | Size | Depends on |
|---|---|---|---|
| **HG-1** | **No animation motion in the view:** the Root anchor in walking, idle and fighting, Y low-passed 80 ms; Head mode (position only, Y 80 / XZ 40 ms) only in traversal; staggers capped and smoothed; the anchor-cause log and assert (design 11) | M | the root read (`PlayerBody+0x1b8`), the eye calibration (run 3), the FSM and fallback reads (built) |
| **HG-2** | **First person through the game's body moments:** combat camera overrides off, the kill cam kept first person; short syncs and struggles with the frozen anchor; knockdowns dim with the view kept; traversal in low-passed Head mode with a vignette and vault blinks; our climb exempt from the fallback; death in first person, then the promotion in the window; the fallback demoted to unknown states -> window | L | HG-1; `Combat.EnableCameraOverride` (RT); the 6.8 detector (built); RT knockdown tags, T1/T2 |
| **HG-3** | **Defence and readable telegraphs:** G4 attack starts; block bars, ground arrows, block/parry/dodge judged at impact (G6/G7); the parry recoil and open window; the haptics of design 3 | XL | G4-G7 sites (static), `SampleNode` (P7), `FUN_02810af0`, layer world quads, P9 (shield orcs, the roll) |
| **HG-4** | **The VR settings panel in the headset**, turning first (snap angle, snap/smooth/off, speed, blink), then the vignette, seated/height, move direction, haptics, dominant hand; live; saved to `som-vr.conf` | L | the pointer (built, headset check pending), a label atlas, a settings ABI block, live re-reads in somvr.so |
| **HG-5** | **Front-first enemies:** attackers picked in view (G2 or the head into the `Camera` term); behind-the-player attacks only after at least 1 s of positional sound + edge glow + haptic; VR tuning; damage x0.5 | L | G9/G10 (static), G2 (not built) or RE of the score weight, G4 (HG-3), Wwise 3D posts |
| **HG-6** | **Hands that stop at surfaces:** arm wall clamp, blade clamp + clank + void, the ghost hand, the reach ghost, the IK spring. **Built for build 13 (2026-10-26, `SOMVR_HANDS_SURFACE=on`, re-xr-pipeline.md 38):** the grips stop at the world (the IK arm, the drawn sword, the bone blade and our VR hands follow), the blade's push up to 25 cm then the void, the clank (haptics + the defence's sparks; no sound), the ghost (our hand mesh, wraith blue, at the real grip), the spring; orcs never clamp; the reach ghost not needed. Headless-proven (boots 002822, 004528); the feel is the headset's | M | `CL_RAYCAST` + filter (built), G11 (built), layer sprite, the clank event name (RT) |
| **HG-7** | **Body slots:** draw and sheathe Urfael over the right shoulder, Acharn at the hip, quick draw, slot ticks; the sword never sheathed under a swinging hand | M | `FUN_023116f0` SHEATH/UNSHEATH, `FUN_0231e2e0` (P7); RT the draw animation and auto-sheathe |
| **HG-8** | **Nothing floats:** health/focus/arrows on the left wrist, arrows on the bow, the streak on the runes and right wrist, markers and the counter icon in stereo at the orcs, the quad only for in-world menus and re-placed in front | L | H11 (built), tile rects (RT P4), G19 + hide path (static/med), G17 |
| **HG-9** | **Never a void:** a captured 360 degree backdrop behind every mono frame (menus, loads, videos, cinematic window), the progress light, layer keep-alive frames during load stalls | M | RT present gaps at loads, RT WiVRn layer extensions, one capture run |
| **HG-10** | **Hands that conform:** finger curl from the Touch sensors; per-object poses (sword, bow, dagger, wall palm, neck claw); the grab latch on orcs | M | G11 finger nodes (re-arms-ik.md 2), `SampleNode` source poses |
| **HG-11** | **Crouch by ducking, seated parity:** physical crouch, the seated reach assist, height re-measure in the panel. **Physical crouch built for build 13 (2026-10-26, `SOMVR_CROUCH=duck`, re-xr-pipeline.md 41):** RT: the stock `Crouch` (15) does nothing; the stealth crouch is `CombatSecondary04` (125) held (`PC_Stealth`); the duck holds it, the right stick down toggles it (seated: the button only), the standing height at each recentre; the menu's Crouch row. Headless-proven (boots 032244, 032944, 033425); the seated reach assist not built | S | HG-4 for the panel part |

**Status HG-3 / HG-5 (2026-10-20, offline; nothing launched): the warnings are built** as `SOMVR_THREAT=1` (off by
default, needs `SOMVR_COMBAT`; re-xr-pipeline.md 18). Every orc attack at Talion is tracked from the key times the game
stores at its start (`Combat+0xc8..`: the impact, the parry window; P7/P8). In view (110 deg): a world-locked warm glow
at the attacker's weapon hand that peaks in the parry window. Out of view: the edge glow on that side, one pulse on that
side's controller and a warning sound at the orc, then an urgent pulse 0.35 s before the impact; an engaged orc close
behind gets a faint edge before it strikes; an inbound arrow gets the edge (or a glow on it) and a pulse;
`SOMVR_THREAT_PARRY=1` ticks the sword hand when the nearest parry window opens. THREAT log lines, unit tests, selftest
stand-ins, layer mock scenarios: green. **Not built:** HG-3's defence (block bars, ground arrows, judging at the impact,
the parry recoil), HG-5's tuning and the 1 s grant veto (G9/G10, RE) and damage x0.5. Infantry strikes come 0.8 s
after their start, so their announcement is under the 1 s HG-5 asks for (`LATE` in the log) until the veto exists.

**Status HG-5 / SH-9 RE (2026-10-26, static only; re-xr-pipeline.md 42):** the attacker choice is mapped. One
call scores each candidate (`AICombatMgr_ScoreAttacker`, `0x28d7d31`): Total = Distance + Priority + Camera +
LastAttacker + State + Buff, at least 0.01. A negative score is never granted, so the veto is a negative score written
after that call. The Camera term is -w off screen and w x cos on screen (weights from DB data, defaults 1). It reads
the AI's camera copy `CPlayerObj+0xb94..+0xbd3`: Talion's position and the game camera's rotation, so in VR the rig's
heading, not the head. The same copy places spawns off screen and picks the on/off-screen attacks. The timeline: the
grant -> the AI's attack start (RT latency) -> the impact 0.815 s later; the slot re-arms 0.25 s x `AttackDelayScalar`
after a streak (writable statics `0x4c4e1b0` / `0x4c4e1b4`). Arrivals come from `AISpawnerAmbientBase::vf_32`
(off-screen point test `FUN_028f54b0`). Damage at Talion: the call at `0x2811007` (`ds+0x50`). Built: the pure
`somvr_front.c` (the veto with a 1 s warning, only when nobody in view was accepted; the damage scale; the camera copy
from the head), tests/test_front.c 92 checks, not hooked. Build 14's design, 9 byte-checked sites and its headless
proof: section 42.9.

**Status HG-9 (2026-10-25, build 13 candidates, headless-proven; re-xr-pipeline.md 36, handoff/HANDOFF-void.md):**
measured first - SoM's loads keep presenting (the loading screen is live on the board in the sky); the stalls are the
eye-size mode sets (154-1038 ms) and anything that stops the render thread, which sent no XR frame at all. Built: the
gap log (always), the keep-alive frames (`SOMVR_XR_KEEPALIVE=on`: a 3 s stall's longest XR gap 3050 -> 103 ms, the view
world-locked with live tracking), the loading space (`SOMVR_XR_LOADSPACE=on`: a floor, our hands in menus and loads,
the loading screen on a calm 2.4 m panel at 2.4 m, HoF2's ember progress lights through every load and stall, a world
stall of 1 s fades to the space), no mode set from a stall
(`SOMVR_EYE_SIZE_STALL_MS=250`), the theatre for videos and the cinematic window (`SOMVR_XR_THEATRE=on`, SH-4's
placement; mock-proven only). Not built: the captured backdrop (SH-10), a curved screen, a separate tip panel (SoM's
loading screen is the panel). The headset still has to show WiVRn's own stall behaviour and the space's look.

### 3.2 Should-have

| # | Item | Size | Depends on |
|---|---|---|---|
| SH-1 | Executions as your own swing (D1 a) with the rune glow and slow-mo | M | HG-3 (streak), RT the execution record + `forceKill`, G15, ClientFX (re-combat-items.md 2) |
| SH-2 | Mounts in first-person Seat mode, car-style yaw, blink on/off | L | HG-1, RT saddle node (T9), the vignette (built) |
| SH-3 | Blink and Shift locomotion on the ground, and the leap as a blink | M-L | PM hook (built), `CL_RAYCAST`, fade (built), RT walkable test and AI response |
| SH-4 | Cinematics and presentations: the window in the calm space, scripted level cameras ignored, captains in the world | M-L | HG-9, G16, RT `cam+0x340`, `cam+0x390` |
| SH-5 | Touch-to-use for every `Activate` prompt (open-hand squeeze first, then near the object) | S -> M | prompt diamonds (static), RT the raiser's target |
| SH-6 | The hand-held quick wheel (hold X) for the d-pad actions | S-M | G3, layer quads, RT the d-pad commands |
| SH-7 | Left-handed mirror; subtitles as a lazy tile with sizes; per-trigger vignette levels | M | HG-4, H11 tile cuts |
| SH-8 | Stealth kills by your own stab (G8, no sync); drains, brands, interrogations in first person with a latched hand | M | G8 (built), HG-2's frozen anchor, RT the generic death's look |
| SH-9 | Arrivals behind the player get a positional cue and marker | M | G19, Wwise posts; the spawner is `AISpawnerAmbientBase::vf_32` (off-screen placement via the AI camera copy) and the cue needs no spawner hook (re-xr-pipeline.md 42.6) |
| SH-10 | The 360 degree backdrop of the place you just left | L | HG-9, six extra views through H4 (design 6) |

### 3.3 Nice-to-have

| # | Item | Size | Depends on |
|---|---|---|---|
| NH-1 | Gravity pull for herbs and collectibles: highlight, flick, catch | L | G19 collectible markers, RE remote collect (HoF2's user dropped pulls there) |
| NH-2 | Dagger throw with wraith recall and a caught return (D12) | M | detach/re-attach (P7), G8 |
| NH-3 | Artifacts held in the hand (the UI scene in stereo) | L | re-hud.md 2.5 |
| NH-4 | "Stand in the scene" cinematics: one vantage at 1:1, no cuts | XL | RE stereo on `FUN_009edba0`, culling |
| NH-5 | The blade cuts ropes, sacks, barrels | L | RE prop damage |
| NH-6 | One-handed mode | L | HG-4, the bow and dagger by buttons |
| NH-7 | The Nemesis diorama | XL | re-hud.md 2.4-2.5 |
| NH-8 | Attackers on the room's open side near the guardian | S | HG-5, `xrGetReferenceSpaceBoundsRect` |
| NH-9 | Speaker-anchored subtitles | M | SH-7 |

## 4. Proposed changes to design-vr.md (for its owner; not applied here)

1. **Section 0, benchmarks:** make HL:A **the** standard (the user's words of 2026-10-20), name this doc, and add its
   six rules to the house rules.
2. **Status block, next steps:**
   - After the headset session, the first items are HG-1, HG-2 and HG-4 (cheap, comfort, the user's ask), then HG-6,
     HG-7 and HG-11, then HG-3, HG-5 and HG-8.
   - Note under "Built": the head base is raw, and the fallback shows the game camera's own motion.
3. **Section 1, the anchor:** mark Root mode and the low-pass as **not built** (VR build 1 runs `SOMVR_BASE=head` raw)
   and as the first fix. Add "staggers: option none" beside "at most 0.5 m".
4. **Section 1, the fallback:** reframe it as a last resort for unknown states, fading to the window, not to a moving
   game camera. Add the per-state table of 2.9:
   - `Combat.EnableCameraOverride = 0` in the forced comfort pokes (RT);
   - the `climb` condition off while `SOMVR_CLIMB` holds;
   - mounts -> Seat mode;
   - death -> first person + window.
   D2's recommendation becomes "(b) never", with the stage view as an option.
5. **Section 1, the body:**
   - the wall clamp through `CL_RAYCAST` (built in 12.3);
   - the ghost hand and reach ghost as layer sprites (the overlay renderer is not needed);
   - finger curl and per-object poses through G11's finger nodes and `SampleNode`;
   - the arms-stay compromise and its RE escape (hiding the sleeves).
6. **Section 2:**
   - replace "No teleport locomotion" with Blink and Shift on the ground as options (off by default), moving Talion
     through the game's collision along the path;
   - the snap list 15/22.5/30/45/60/90 plus "turning off", **all adjustable in the headset** (the user, 10-20). D7 then
     only picks the default;
   - add the seated reach assist.
7. **Section 3:**
   - add the VR rule for off-view attacks (no attack start from outside 110 deg until it has been announced for 1 s),
     the edge glow, the near-side haptic tick, and G2 or the `Camera` weight as its dependency;
   - add arrivals behind the player (RE `AISpawnMgr`) and the visual block magnet;
   - executions: say that with the stock melee off there are **no** executions until D1 (a) is built;
   - stealth kills: offer the non-synced G8 stab as the HL:A-like default (a new decision, below).
8. **Section 3, the dagger and the sword:** add body slots in the head's yaw frame, `FUN_023116f0` SHEATH/UNSHEATH for
   Urfael and slot haptic ticks. Note that today the sword can stay sheathed while the player swings.
9. **Section 6:**
   - in-engine cinematics already reach the layer as mono frames (re-hud.md 1.3), so the window is mostly a placement
     and backdrop job;
   - scripted level cameras are ignored in VR by default;
   - "stand in the scene" is an RE experiment;
   - phase 1 of the backdrop is a fixed captured cubemap, which needs no game code.
10. **Sections 6-7, loading:** add the layer's keep-alive frames during present stalls (RT the gaps) and the progress
    light (HoF2).
11. **Section 7, the HUD:**
    - the built world quad is the interim (smaller and lower, re-placed at snap turns and menu openings);
    - the release target is the body tiles plus stereo markers, with the counter icon first because the HUD quad shows
      markers at one eye's screen positions;
    - in-world menus are re-placed in front of the head when they open (G17).
12. **Section 7, the VR settings page:**
    - a layer panel with an offline label atlas, laser and finger poke, live values;
    - `som-vr.conf` as its save file;
    - the list in 2.8;
    - HoF2's `VRSettings.cs` as the model.
13. **Section 8, options:** add turning off; snap 15 and 22.5; Blink/Shift/Continuous; staggers none; the ghost hand
    on/off; the seated reach assist; one-handed mode (later); kill cam first person (forced).
14. **Section 11, the player review:** add an HL:A checklist:
    - no view motion without a tagged cause in walk, fight, stagger or knockdown runs;
    - nothing head-locked or floating in play;
    - a visible warning before every orc impact in the drills;
    - no hand or blade inside a wall in the shots;
    - no black or frozen frame across loads (the keep-alive log).
15. **Section 12, milestones:** HG-1, HG-2, HG-4, HG-9 and HG-11 move into M2 (before user playtest 1); HG-6, HG-7,
    HG-8 and HG-10 into M3; HG-3 and HG-5 into M4.
16. **Section 14, new decisions:**
    - **D17:** offer Blink/Shift (default off), or none;
    - **D18:** stealth kills by your own stab without the sync (HL:A-like, a generic death) vs the stock synced kill in
      first person with a frozen anchor;
    - **D19:** in-engine cinematics in the window (now) vs research into "stand in the scene";
    - **D20:** knockback from staggers: smoothed to 0.5 m (UYF-like, the user's HoF2 combat exception) or none.

## Sources

Web pages were read as data. One page met during the research (tcrf.net's HL:A page) carried instructions aimed at
AI agents; nothing from it is used.

Valve's own words:
- **CMT**: the HL:A developer commentary (free update, 2020-11-12), transcript by node:
  https://combineoverwiki.net/wiki/Developer_commentary/Half-Life:_Alyx
- **AMA**: Valve's Reddit AMA, January 2020, as quoted by UploadVR: https://www.uploadvr.com/half-life-alyx-ama/
- **GI**: Game Informer, Robin Walker, 2020-03-23:
  https://gameinformer.com/interview/2020/03/23/valve-talks-half-life-alyx-and-why-arms-dont-work-in-vr
- **UVR-hands**: UploadVR, Tristan Reidford on hands: https://www.uploadvr.com/importance-believable-vr-hands-presence/
- **ENG**: Engadget interview, 2020-03-31: https://www.engadget.com/2020-03-31-half-life-alyx-valve-interview-vr.html
- **TG**: TheGamer, Robin Walker: https://www.thegamer.com/valve-half-life-alyx-vr-exclusive-interview/
- **RTV-Walker**: Road to VR, Robin Walker on design:
  https://roadtovr.com/valve-half-life-alyx-game-design-interview-robin-walker/
- **PCG**: PC Gamer, Greg Coomer on teleport body size:
  https://www.pcgamer.com/half-life-alyxs-movement-system-was-changed-because-tall-testers-kept-getting-stuck/
- **UVR-1hand**: UploadVR, one-hand mode: https://www.uploadvr.com/half-life-alyx-one-hand/
- **GS**: GameSpot, cut enemies (read through a search excerpt):
  https://www.gamespot.com/articles/half-life-alyx-cut-enemies-that-were-too-scary-in-/1100-6475051/
- **ASE**: A Sound Effect, HL:A's sound: https://www.asoundeffect.com/half-life-alyx-vr-sound/

Third party:
- **RTV-XR**: Road to VR, Inside XR Design:
  https://roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/
- **TI**: Transform Interactive: https://transforminteractive.com/what-half-life-alyx-taught-us/
- **RTV-1.1**: https://www.roadtovr.com/half-life-alyx-continuous-turning-smooth-crash-fix/
- **RTV-1.2**: https://roadtovr.com/half-life-alyx-update-1-2-left-hand-options/
- **NW**: Newsweek, the locomotion settings text:
  https://www.newsweek.com/half-life-alyx-vr-gameplay-pc-specs-vive-oculus-movement-1490121
- **COW-gloves**: Combine OverWiki, Gravity Gloves: https://combineoverwiki.net/wiki/Gravity_Gloves
- **DES**: Destructoid, upgrades and healing:
  https://www.destructoid.com/half-life-alyx-guide-what-upgrades-to-get-first-and-how-to-heal/

Not verified (no source found): a ghost of the real hand; the snap-turn angle list; a comfort vignette; an explicit
"never spawn behind" rule; Valve's reason for wrist pockets.

Project sources:
- `design-vr.md`, `re-xr-pipeline.md` 6-13, `re-hud.md`, `re-gameplay.md` 6, `re-combat-items.md`, `re-combat-vr.md`,
  `re-arms-ik.md`, `re-firstperson.md`, `re-strings-map.md`;
- `NOTES.md` (run 9) and `som-vr.conf`;
- Hand of Fate 2: `docs/decisions.md` and `src/HoF2VR/VR/` (VRSettings, FingerCurl, Limbo).
