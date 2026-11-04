# SoMVR: Middle-earth: Shadow of Mordor in VR

> **Disclaimer:** this is vibe coded and still in a rough state.

A VR mod for **Middle-earth: Shadow of Mordor**. You are Talion: you see through his eyes and his hands are yours.

- **Hands and a real sword.** Draw the sword from your shoulder and swing it; a hit lands where the blade meets the orc,
  and how hard you swing decides the blow. Block by holding the blade across a blow. A knife at your hip, a bow at your
  shoulder.
- **Orcs by hand.** Grab them, shove them, question them, brand them, finish them.
- **Climbing by hand.** Grab a wall and pull yourself up.
- **Comfort.** The game never moves your view: only your head, the sticks and the moves you start do. Snap or smooth
  turning, a vignette, seated play.
- **Menus in VR.** A menu on your left wrist, the game's own menus on a screen you point at or touch.

**Status: alpha.** It is developed and played on one Linux PC with a Quest 2 (through WiVRn). The Windows package has
only been tested under Proton on that PC: not yet on a Windows PC, and not with SteamVR, Meta's PC app or Virtual
Desktop. Reports are welcome (see "Reporting a bug").

## What you need

- **The game's Windows version**, as GOG and Steam sell it today (Middle-earth: Shadow of Mordor - Game of the Year
  Edition, 64-bit). The mod checks the game's `ShadowOfMordor.exe` when it starts. With any other version it stays off
  and the game runs as the normal flat game.
- **A PC VR headset** with an OpenXR runtime that supports Vulkan (`XR_KHR_vulkan_enable`): SteamVR, Meta's PC app
  (Quest Link, Air Link), Virtual Desktop's VDXR (not checked yet) on Windows; WiVRn, Monado or SteamVR on Linux.
- **A graphics driver with Vulkan 1.3** (any current AMD, NVIDIA or Intel driver).
- **Controllers:** made with Quest (Touch) controllers. Valve Index controllers have bindings too (untested). Others
  depend on your runtime's own remapping.

**The package:** the mod comes as one folder, `x64`, made by `mod/somvr/win/package.sh` (see "Building"). When a
ready-made package is published, it is on this repository's Releases page.

## Install on Windows

1. Find the game's folder with `ShadowOfMordor.exe` in it: the game's `x64` folder (GOG Galaxy: the game > Manage
   installation > Show folder; Steam: right-click the game > Manage > Browse local files, then `x64`).
2. Copy everything from the package's `x64` folder into that folder, next to `ShadowOfMordor.exe`: `xinput9_1_0.dll`,
   `d3d11.dll`, `dxgi.dll`, `vulkan-1.dll`, `openxr_loader.dll` and the `somvr` folder.
3. Make your runtime the active OpenXR runtime (SteamVR: Settings > OpenXR; Meta's app: Settings > General > OpenXR
   Runtime; Virtual Desktop: VDXR in its streamer's settings).
4. Start the runtime, put the headset on, then start the game as usual. **Not as administrator**: then the mod's
   Vulkan layer is not loaded and the game runs flat.

**Uninstall:** delete those five DLLs and the `somvr` folder. DXVK (below) may also have written its own log files next
to the game (`ShadowOfMordor_d3d11.log`, `ShadowOfMordor_dxgi.log`): delete them too. Nothing else of the game is
changed.

`d3d11.dll` and `dxgi.dll` are [DXVK](https://github.com/doitsujin/dxvk): the game's Direct3D 11 runs on Vulkan, so the
mod can hand its pictures to the headset. Overlays that hook Direct3D 11 (RivaTuner / MSI Afterburner, some capture
tools) may not show or may crash: turn them off for this game. The first minutes may stutter while the shaders are
built for the first time.

## Install on Linux (Proton) - experimental

The same package runs under Proton. So far it has run in one test start under Proton in the developer's test setup
(outside Steam); a start through Steam's own Proton has not been tried yet.

1. Steam: the game > Properties > Compatibility > force a Steam Play tool: **Proton Experimental** (the one the tests
   use). Steam then installs the Windows version. Start it once, flat, so Proton makes its prefix, and quit.
2. Copy the package's `x64` folder into the game's `x64` folder, as on Windows.
3. Register the mod's Vulkan layer in the game's prefix, once. Under Wine the Vulkan loader ignores the folder the mod
   points it to, so the layer has to be in the registry (paths for Steam's default library; change them to yours):

   ```
   STEAM=~/.local/share/Steam
   GAME="$STEAM/steamapps/common/ShadowOfMordor"
   WINEPREFIX="$STEAM/steamapps/compatdata/241930/pfx" "$STEAM/steamapps/common/Proton - Experimental/files/bin/wine" \
     reg add 'HKLM\Software\Khronos\Vulkan\ExplicitLayers' \
     /v "Z:$(printf '%s' "$GAME/x64/somvr/VkLayer_somvr_openxr.json" | tr / '\\')" /t REG_DWORD /d 0 /f
   ```

4. Launch options (the game > Properties > General):

   ```
   WINEDLLOVERRIDES="xinput9_1_0,d3d11,dxgi,d3d10core,vulkan-1,openxr_loader=n,b" SOMVR_LAYER=win %command%
   ```

   With WiVRn or Monado, also add the launch options their own instructions give for Steam games (they let a game
   inside Steam's container reach the runtime).
5. Start the runtime and connect the headset, then start the game in Steam.

**Uninstall:** delete the files from the `x64` folder, clear the launch options, and remove the registry value (the
same command with `reg delete 'HKLM\Software\Khronos\Vulkan\ExplicitLayers' /v "Z:..." /f`). Switch the
compatibility setting back if you want Steam's Linux version again.

**The native Linux version** (Feral's port, Steam's default on Linux) is where the mod began, and the developer still
plays both. A launcher for other players is not ready for it: `scripts/som-vr.sh` (a Steam launch option) still starts
an early set of the features, and the full set runs through the developer's own session scripts, which expect the
developer's PC. Use the Windows version under Proton.

## First start

1. The company movies are skipped. The game's menus show on a large screen in front of you: point a controller at it
   and pull the trigger (within reach, a fingertip works too). Continue your game.
2. About 5 seconds after you are in the world, the view fades to black and you arrive in the **practice meadow**: a
   quiet spot in daylight where no enemies come near and you cannot die (except to a captain, below). A card in front
   of you shows the first controls; press A, squeeze a grip or open the VR menu to put it away. This happens once per
   start.
3. **The VR menu:** turn your left palm toward your face and look at it (or hold the menu button). It opens on that
   wrist; point at it with the other hand or touch it. Its tabs:
   - **Settings**: comfort and controls (below); its second page, Fight settings, is for the sword and the bow.
   - **Practice**: spawn an orc (choose the type: infantry, shield, berserker, caragor, ghul, graug), a group, a far orc
     that has not seen you, or the orc you aim at; refill health or arrows; **Back to where I was** returns you to the
     world, **Go to practice area** takes you to the meadow again. **Captain** brings one of your save's real Nemesis
     captains: he can kill you there, his death is for good and a kill promotes him, as anywhere in the game.
   - **Controls**: every control of this build, in groups (Prev / Next for the pages). It is the list below.
   - **Test**: notes on what changed in the build - only in builds that have them (not in the Windows package).

## Controls

The names are the Quest (Touch) controllers': A and B on the right controller, X and Y on the left, the menu button
(≡) on the left; RT / LT are the triggers, the grips are under your middle fingers. On Index controllers the left
controller's A / B are X / Y here and its system button is the menu button. **Left-handed:** Settings > Handedness =
Left swaps the hands (the sword, A / B and turning go to the left hand; the bow, X / Y and walking to the right) from
the next start.

**Moving**

| Action | How |
|---|---|
| Walk | Left stick, toward where you look (Settings > Walk toward: Hand = where your left hand points) |
| Sprint | Click the left stick; it stays on while you keep the stick pushed |
| Turn | Right stick left or right |
| Crouch | Duck for real, or push the right stick down |
| Jump | A while on foot |
| Vault | A at the gold ring on a low wall or obstacle |
| Blink | Push the right stick forward and hold it: a ring shows where you will land (gold = you can, red = you can't). Let go to blink there; move the left stick to cancel |
| Map | Click the right stick; hold it for the army screen |
| Minimap | Turn the back of your left wrist toward your eyes, as if to read a watch |
| Use | B: interact, pick up, start missions |
| Herb | Hold your open hand at a glowing healing herb and squeeze the grip, then bring your fist to your mouth to eat it |

**Climbing**

| Action | How |
|---|---|
| Climb | Grab a wall with a grip and pull yourself along it. Let go in the middle of a pull to fling yourself up; push yourself down hard and let go to fling down |
| Ledge | Pull yourself up at the ring on a top |
| Onto top | Holding a wall's top with your eyes over it, a gold ring shows where you will stand: A climbs onto it |
| Overhang | Holding a lip from below: A at its gold ring climbs up onto it |
| Down | On a wall's top, reach down over the edge and close your hand: you climb over and down |
| Sword | While climbing, the hand that is off the wall can draw the sword at the shoulder |
| Drop | On a wall, hold A: the spot where you will land shows. Let go of A to drop there |

**The sword**

| Action | How |
|---|---|
| Sword | Reach to your right shoulder and squeeze the grip to draw it |
| Sheathe | Squeeze the grip there again |
| 2 hands | Left grip (or the left trigger) on the hilt holds it with both hands |
| Block | Hold the blade across an orc's blow |
| Dodge | A in a fight: a quick 1.5 m dash out of the blow |
| Knife | Left hand at your left hip, hold the grip: the knife stays in your hand while you hold it |
| Shove | Push hard at an orc with your free hand |
| Stealth | Behind an orc that has not seen you: a hard stab into his back kills him |
| Above | Crouched on a ledge over an orc (a faint blue glow): stab down at him, fast |
| Over orc | Aim the blink at an orc close to you: you go over him |
| Struggle | When an orc has you down: move your hand to bring the cursor into the ring, then stab hard |
| Downed | An orc on the ground you can finish glows blue: stab down into him |
| Brutal | Hold the left trigger during a stealth stab: the game's brutal kill |

**Orcs**

| Action | How |
|---|---|
| Grab | An orc with a blue glow: your left grip on him grabs him |
| Stun | While you hold him: tap the left trigger |
| Drain | While you hold him: the left trigger again, held |
| Question | While you hold him: pull him to your face |
| Kill | While you hold him: any hit with the sword |
| Let go | Open your hand, or push him away |
| Execute | An orc glowing after your run of hits: one hard hit |
| Brand | Your left palm on his head and the left trigger (where the game offers it) |
| Rally | Left hand up high and the left trigger: your branded orcs attack |
| Whistle | Crouched, a hand at your mouth |
| Dominate | When you dominate an orc: B dismisses him, Y gives him a command |

**Bow and wraith**

| Action | How |
|---|---|
| Bow | Left grip at your left shoulder takes the bow |
| Shoot | Right trigger on the string, pull back, let go |
| Wraith | Hold Y for wraith vision; A while in it = Shadow Strike at the marked orc |
| Glyph | Hold Y and look at a glowing glyph to collect it |
| Flash | Hold the right trigger and push your open left palm out at an orc (where the game offers it) |
| Ignite | B while an arrow is drawn (its tip glows); the shot ignites |
| Distract | X while an arrow is drawn |

**Beasts**

| Action | How |
|---|---|
| Caragor | Hold the sword across a caragor's leap: it is stunned |
| Mount | Left grip on the stunned beast |
| Hold on | Keep the grip closed |
| Steer | Left stick |
| Riding | A leaps, the right trigger bites, B gets off |
| Sprint | Hold A, or click the left stick |

**Menus and help**

| Action | How |
|---|---|
| Menu | Turn your left palm toward your face and look at it |
| Pause | Tap the menu button: the game's pause. Hold it: the VR menu |
| Screens | The game's menus: point and pull the trigger, or touch them with a fingertip when they are within reach |
| Failed | After a failed mission: A tries again, B gives up |
| Nemesis | B closes the Nemesis pages |
| Recentre | VR menu > Settings > Recentre view |
| Report | Hold B and Y together for half a second and let go (see "Reporting a bug") |
| Quit | VR menu > Settings > Quit game, twice. The Windows package has no such button (and no Quit line on the Controls page): quit from the game's own menu |

## Comfort and settings

The game never moves your view on its own. Talion's eyes follow your head; his body moves only with the sticks and the
moves you start. A move that takes you somewhere (the blink, the drop, the dodge) shows where you will land or always
goes the same short distance, and the blink can be cancelled while you aim. When the game takes Talion over (story
scenes, some scripted moments), the view fades and the scene plays on a screen in front of you.

If you are new to VR: start seated, with short sessions, and keep the vignette on. Take the headset off if you feel
unwell; the game keeps running on the monitor.

**Settings** (the VR menu's Settings tab):

| Setting | Default | What it does |
|---|---|---|
| Turn | Snap | Snap turns in steps; Smooth turns while you hold the stick |
| Snap angle | 30° | The step, 15 to 90° |
| Turn speed | 120°/s | Smooth turning's speed, 60 to 300°/s |
| Vignette | 60 % | Darkens the edges while the sticks move or turn you; 0 % = off |
| Snap blink | Off | On: a short fade to black for each snap turn |
| Seated | Off | On: play seated, the view stays at Talion's height. Seat height: Auto or a fixed height |
| Walk toward | Head | Head: the left stick walks where you look; Hand: where your left hand points |
| Crouch | Duck | Duck: crouch by ducking for real (or the stick); Button: the right stick only |
| Climb fling | On | Let go mid-pull to fling yourself |
| Hands at walls | On | Your hands stop at walls instead of going through |
| Player size | 100 % | Up to 140 %: you are bigger next to the world |
| HUD | On | The game's health and prompts on a panel in front of you; HUD distance 1.5 m |
| View | First | First: Talion's eyes; Third: the game's own camera on a screen |
| Haptics | 100 % | The controllers' buzz, 0 to 150 % |
| Music | 30 % | The game's music volume |
| Refresh rate | 80 Hz | Asked from the headset where the runtime allows it (steadier in busy places than 90); Headset = its own rate |
| Pointer hand | Right | The hand that points at the menus |
| Handedness | Right | Left swaps the hands, from the next start |
| Recentre view | | Puts the view and the menus in front of you again |

**Fight settings** (Settings tab > Fight settings):

| Setting | Default | What it does |
|---|---|---|
| Sword angle | 0° | The blade's angle in your fist (+ tips it up, - down) |
| Sword weight | Heavy | How far the blade lags behind your hand in a fast swing: Heavy, Light or Off |
| Sword hand | Fixed | Fixed: your hands stay put; Follow: they turn with the weighted blade |
| Blade bite | On | A full cut stops in the body for a moment |
| Arrows | Auto | Auto: the arrow comes onto the string by itself; Quiver: reach behind your shoulder for each one |
| Bow string | Trigger | The right trigger holds the string, or the right grip (Grip) |
| Dodge (A) | On | A in a fight: the quick dash out of the blow |
| Wraith step | On | The blink on the right stick |
| Stealth kill | On | Behind an orc that has not seen you, a hard stab kills him |
| Crowd pace | 0.7 s | The least time between two orcs' blows at you; Off = all at once |
| Counter icon | Hidden | Shown: the game's flashing icon over an orc about to strike |

**On Windows the menu's changes are saved** in `somvr\settings.conf` (the menu makes it at your first change) and come
back at the next start. `somvr\somvr.env` holds the mod's switches, one a line (for example `SOMVR_TURN=smooth`,
`SOMVR_VIGNETTE=0.3`; a `#` at the start of a line turns it off); a setting you changed in the menu wins over it.

## Known limits

- **Untested on a Windows PC**, and with SteamVR, Meta's PC app and Virtual Desktop. Under Proton on Linux it has run in
  test starts only.
- **One version of the game:** the GOG / Steam Windows `ShadowOfMordor.exe` of today. After a game update the mod stays
  off until it is updated too.
- **No Quit game button in the Windows package**: quit from the game's own menu.
- **Controllers:** only Quest (Touch) controllers have been played; Index bindings are untested.
- **Played from saves in the open world.** A new game's opening has not been played through with this build.
- Overlays that hook Direct3D 11 can break the game (above). Starting the game as administrator gives the flat game.

## Reporting a bug

**In the headset:** hold **B and Y together for half a second, then let go** (or VR menu > Settings > Report). The
controllers buzz, and the mod saves a picture of both eyes and the game's state in a new folder in `somvr\marks\`.
Then carry on playing.

**Then** open an issue on this repository's Issues page with what you did and saw, roughly when, your headset,
runtime and graphics card, and attach `somvr\somvr.log` (it keeps every start, the newest at the end) and the
`somvr\marks` folders. If the headset shows nothing, also add a line with a full path for the Vulkan layer's own log to
`somvr\somvr.env`, for example `SOMVR_XR_LOG=C:\Users\Public\somvr-xr.log`, start once more and attach that file.

## How it works

The game loads the mod's `xinput9_1_0.dll` (as its XInput). The mod patches the game in memory: the camera, the
per-eye rendering, the input, the combat. DXVK runs the game's Direct3D 11 on Vulkan, and the mod's Vulkan layer
(`somvr\VkLayer_somvr_openxr.dll`) hands each eye's picture to the OpenXR runtime and draws the hands, the menus and
the comfort aids. The game's files are not changed; your saves are the game's own. On the native Linux version the
same code is a preload (`somvr.so`) and a Linux Vulkan layer.

`docs/re-*.md` are the reverse-engineering notes: addresses, structure layouts and how each part of the game works
(`docs/re-winport.md` for the Windows version). `docs/design-*.md` are the design notes.

## Building

You need your own copy of the game, gcc and python3; for the Windows package also clang, lld-link and the x64 MSVC
CRT and SDK (for example through [xwin](https://github.com/Jake-Shadle/xwin)).

1. `scripts/fetch_third_party.sh` downloads the headers into `third_party/` and checks them against pinned hashes: the
   [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) 1.1.61,
   [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) v1.4.341 and
   [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4.
2. **Linux:** `mod/somvr/build.sh` builds `mod/somvr/build/somvr.so` and the Vulkan layer. `build.sh --check` also
   runs the unit tests and checks the patch sites against the game's files in `reference/bin/` (`linux/ShadowOfMordor`
   and `ShadowOfMordor.exe`: copies from your install).
3. **Windows package:** `scripts/fetch_win_redist.sh` (DXVK, the Vulkan and OpenXR loaders, pinned), then
   `mod/somvr/win/build.sh` (it needs `reference/bin/ShadowOfMordor.exe` and the CRT/SDK in `XWIN64`, default
   `../shared/toolchain/xwin-sdk64`), then `mod/somvr/win/package.sh`. The package is
   `mod/somvr/build/win/package/x64`.

The other scripts are the developer's test tools; many expect the developer's PC.

## Licences and third-party files

- The mod: MIT, see [LICENSE](LICENSE).
- In the Windows package (their licence texts in `somvr\licenses`): [DXVK](https://github.com/doitsujin/dxvk) 2.7.1
  (zlib/libpng); the Khronos [Vulkan loader](https://github.com/KhronosGroup/Vulkan-Loader) 1.4.341 (Apache-2.0, as
  LunarG's Vulkan Runtime, with MIT parts); the Khronos [OpenXR loader](https://github.com/KhronosGroup/OpenXR-SDK-Source)
  1.1.61 (Apache-2.0); [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4 (BSD-2-Clause, built into
  `xinput9_1_0.dll`).
- The VR hands' model (`mod/somvr/assets/generic-hand/`, built into the Vulkan layer): the WebXR Input Profiles'
  generic hand, by Amazon (MIT, `LICENSE.md` there).
- The text font in `mod/somvr/somvr_font.c` is based on the IBM PC VGA 8x16 font.
- Not affiliated with or endorsed by Warner Bros. Games, Monolith Productions or Feral Interactive. No game files are
  included: you need your own copy.
