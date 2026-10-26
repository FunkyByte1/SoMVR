# SoMVR: Middle-earth: Shadow of Mordor in VR

A VR mod for the native Linux version of **Middle-earth: Shadow of Mordor** (Feral Interactive's port).

> [!WARNING]
> **Vibe-coded.** This mod was written almost entirely by an AI (Claude, by Anthropic). The author describes what
> they want and playtests it in the headset, but does not review the code line by line. Expect bugs and rough
> edges, read the scripts before you run them, and back up your saves first. Use at your own risk.

- A stereo first-person view with head tracking.
- Tracked hands, with a motion-controlled sword and bow.
- Menus you point at in VR, and comfort options.

**Status: alpha, work in progress.** It is developed and tested on one machine: Linux (Nobara), an AMD GPU and a Meta
Quest 2 streamed with [WiVRn](https://github.com/WiVRn/WiVRn). The scripts still assume that machine's folders, and
there is no ready-made download yet.

## How it works

`somvr.so` is loaded into the game with `LD_PRELOAD`. It patches the game in memory: the camera, the per-eye
rendering, the input, the combat. A Vulkan layer (`mod/somvr/vklayer/`) hands each eye's image to an OpenXR runtime
(WiVRn or Monado). The game's files are not changed, and the game still needs Steam: the mod does not touch its ownership
check.

`docs/re-*.md` are the reverse-engineering notes for the Linux build: addresses, structure layouts and how each part
of the game works. `docs/design-*.md` are the design notes.

## Building

You need your own copy of the game on Steam (the Linux version), gcc and python3.

1. Put the headers in `third_party/` (not included yet):
   - `third_party/OpenXR-SDK-1.1.61/include` from the [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) 1.1.61;
   - `third_party/Vulkan-Headers-1.4.341/include` from [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers)
     v1.4.341.
2. Run `mod/somvr/build.sh`. It builds `mod/somvr/build/somvr.so` and the Vulkan layer. `build.sh --check` also runs
   the unit tests; it reads the game binary from `SOM_BIN`.
3. Run `scripts/som-vr.sh --make-release` to copy a build into `release/`. Then set `/path/to/scripts/som-vr.sh
   %command%` as the game's Steam launch option. With a headset connected the game starts in VR; without one it
   starts as the normal flat game. The other scripts are the author's test tools and still use the author's paths.

## Third-party files

- `mod/somvr/assets/generic-hand/`: a hand model by Amazon (MIT, `LICENSE.md` there).
- The text font in `mod/somvr/somvr_font.c` is based on the IBM PC VGA 8x16 font.

## Notes

- Not affiliated with or endorsed by Warner Bros. Games, Monolith Productions or Feral Interactive. No game files are
  included: you need your own copy.
- Licence: MIT, see [LICENSE](LICENSE). Third-party files keep their own licences.
