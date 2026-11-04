Shadow of Mordor VR - for Windows (the GOG or Steam "Middle-earth: Shadow of Mordor GOTY", 64-bit)

An alpha VR mod: you are Talion, with your hands as his, a sword you swing, climbing by hand and menus in VR. It has
been tested under Proton on Linux only, not yet on a Windows PC or with SteamVR, Meta's PC app or Virtual Desktop.
The full guide: README.md in the mod's repository.

What you need
- The game's Windows version as GOG and Steam sell it today. The mod checks ShadowOfMordor.exe at the start; with any
  other version it stays off and the game runs flat.
- A PC VR headset and an OpenXR runtime that supports Vulkan (XR_KHR_vulkan_enable): SteamVR and Meta's PC app (Quest
  Link, Air Link) have it; Virtual Desktop's VDXR is not checked yet. Make the one you use the active OpenXR runtime
  (SteamVR: Settings > OpenXR; Meta's app: Settings > General > OpenXR Runtime).
- A graphics driver with Vulkan 1.3 (any current AMD, NVIDIA or Intel driver).
- Quest (Touch) controllers are the ones played so far; Index controllers have bindings too (untested).

Install
1. Find the game's folder with ShadowOfMordor.exe in it, the game's x64 folder (GOG Galaxy: the game > Manage
   installation > Show folder; Steam: right-click the game > Manage > Browse local files, then the x64 folder).
2. Copy everything from this package's x64 folder into that folder (next to ShadowOfMordor.exe): xinput9_1_0.dll,
   d3d11.dll, dxgi.dll, vulkan-1.dll, openxr_loader.dll and the "somvr" folder.
3. Start your VR runtime, put the headset on, then start the game as usual - not "as administrator": the Vulkan
   loader then ignores the mod's layer (somvr\somvr.log says "integrity ... high") and the game runs flat.

First start
- The game's menus show on a large screen in front of you: point and pull the trigger (within reach: a fingertip).
- About 5 s after you are in the world you arrive in the practice meadow (you cannot die there; a card shows the first
  controls). The VR menu: turn your left palm toward your face and look at it, or hold the menu button. Its Practice
  tab spawns orcs and takes you back ("Back to where I was"); its Controls tab lists every control.
- The Practice tab's "Captain" brings one of your save's real captains: his death is for good, as in the game.

Settings
- The VR menu's settings are saved in somvr\settings.conf (made at your first change) and come back at the next
  start; Handedness takes effect from the next start. The mod's switches are in somvr\somvr.env (e.g.
  SOMVR_TURN=smooth, SOMVR_VIGNETTE=0.3; one a line, "#" at the start of a line turns it off); a setting you changed
  in the menu wins over it.
- The VR menu's "Quit game" is not available in this package: quit from the game's own menu.

Bug reports
- In the headset: hold B + Y together for half a second, then let go (or VR menu > Settings > Report). The mod saves
  a picture of both eyes and the game's state in a new folder in somvr\marks\.
- Send what you saw and roughly when, with somvr\somvr.log (every start, the newest at the end) and the marks folders.
  If the headset shows nothing, add a line SOMVR_XR_LOG=<a full path, e.g. C:\Users\Public\somvr-xr.log> to
  somvr\somvr.env for the Vulkan layer's own log and send that too.

Uninstall: delete those five DLLs and the somvr folder, and DXVK's log files if it wrote any next to the game
(ShadowOfMordor_d3d11.log, ShadowOfMordor_dxgi.log). Nothing else of the game is changed.

Good to know
- d3d11.dll/dxgi.dll are DXVK: the game's Direct3D 11 runs on Vulkan, so the mod can hand its frames to the headset.
  Tools that hook Direct3D 11 (RivaTuner/MSI Afterburner overlays, some capture tools) may not show or may crash - turn
  them off for this game.
- The first minutes may stutter while the GPU's shaders are built for the first time.
- Your saves are the game's own; the mod does not read or write them.

Licences: somvr\licenses - DXVK 2.7.1 (zlib/libpng); the Khronos Vulkan loader 1.4.341 (Apache-2.0, with MIT parts);
the Khronos OpenXR loader 1.1.61 (Apache-2.0); MinHook 1.3.4 (BSD-2-Clause, built into xinput9_1_0.dll); the VR hands'
model, the WebXR Input Profiles' generic hand by Amazon (MIT, built into the layer). The mod itself: MIT.
