# Moving somvr from the Feral Linux build to the Windows build - measured scope (2026-11-02)

Research only: nothing in mod/ changed. Inputs: the mod at build 31 (373 `SITE(...)` entries in 29 files, ~101k lines
of C in mod/somvr, 14.5k in mod/somvr/vklayer), the Ghidra projects som-linux (Feral ELF) and som-win64 (Steam Windows
depot 241931's ShadowOfMordor.exe, 2020-12-19, sha256 f5d72578...). The GOG copy was still downloading (3.7 GB of the
install, no exe yet): its exe could not be compared and no Proton boot was made (5 below).

Tools added (read-only on everything they are given; outputs under logs/winport/, git-ignored):
- `reference/index-win64/` = `SomIndex.java` run read-only on som-win64 (11 s): functions.tsv (73,852), calls.tsv
  (161k direct call pairs), strings_xrefs.tsv (39k). The Linux side's index has 193,908 functions, 366k pairs, 64k strings.
- `scripts/winport_match.py` (tier 1, ~2 min, no Ghidra): pairs Linux and Windows functions by unique strings, then
  by call-graph neighbourhoods (>= 2 shared matched neighbours, Jaccard >= 0.5, unique, mutual) and the lone-child rule,
  then vtables (Windows: runs of .text pointers in .rdata whose start a RIP-relative LEA names, 4,026; Linux: vfuncs.tsv,
  9,144; paired by their matched slot functions and their matched constructors (the Linux ctor's `mov [..], imm32` of
  the vtable, the Windows ctor's LEA)), slot-aligns paired vtables, and repeats. Then it answers every SITE.
- `scripts/winport_verify.py` (tier 2): for a sample of SITEs, decompiles the Linux function and its Windows match or
  its neighbourhood candidates and ranks them by the field offsets, floats and strings both use.

## 1. Inventory: what the mod depends on

| Kind | Count | Notes |
|---|---|---|
| `SITE(...)` hook/call/proof sites | **373**: 169 K_ENTRY (called or proved functions), 102 K_CALL (patched or read calls), 6 K_JMP, 96 K_VSLOT | 306 distinct target functions; 29 files (somvr.c 171, render 39, headparts 29, finish 19, practice 18, collect 15, kills 10, front 9, ...) |
| - of these on Feral's own D3D11-on-Vulkan layer (IndirectX) | **26** K_VSLOT: IdxD3D11DeviceContextThreaded 21, IdxD3D11Texture2D 2, IdxD3D11DeviceContext 1, IdxD3D11Device 1, IdxDXGISwapChain 1 | on Windows the real D3D11/DXGI COM vtables (same slot order: COM has no virtual destructor; slot 47 = CopyResource, 9 = PSSetShader, 21 = DrawInstanced) - hooked on the runtime objects, no search |
| - on Feral's Vulkan texture | **2** (CVulkanTexture, the VkImage at +0x108) | DXVK: `IDXGIVkInteropSurface::GetVulkanImageInfo`; native D3D11: no VkImage at all |
| Game addresses outside SITE lines (globals, data, tables) | **135** literals, 117 distinct (e.g. `BZ_SKILL_CAT_PP 0x4b56640`) | each follows from the function that writes/reads it once that is paired (Linux `DAT_046a9560 + 0xa8` <-> Windows `DAT_1418f1228 + 0xa0` in LP_IM_DOWN) |
| Field offsets / vtable byte offsets in game-side code | **~1,200** small hex literals in the 58 game-side files: 413 `#define` lines + ~470 inline `p + 0x..` / `[0x..]`; ~270 distinct values; 919 `peek_ptr` reads | see 2.2: most survive, large objects shift |
| Hook entry asm wrappers | 16 asm blocks (somvr.c 7, render 6, objective, session, vrhands) | needed because the Linux game is GCC 8.3 with IPA-RA; MSVC code follows the plain Win64 ABI, so on Windows they become ordinary C hooks |
| Linux/ELF-specific code in the preload | LD_PRELOAD entry + `/proc/self/exe`, cmdline and build-id gate; `dlsym(RTLD_DEFAULT)` publishing 9 structs (somvr_shared, _hud, _input, _comfort, _vrmenu, _threat, _objective, _session, _vrhands); `mmap(MAP_FIXED_NOREPLACE)` trampolines near the code (8 places); `mprotect` (33 lines, 7 files); pthreads (147 lines, 7 files); `/proc/self/maps`, `/proc/self/task`; clock_gettime (8); perf_event hardware breakpoints (probes only); sockets/inotify/poll for the test command channel (3+2 files); settings path under ~/.local/share | ~60 places; a small platform shim (VirtualProtect, VirtualAlloc near the image, VirtualQuery, Toolhelp32, QPC, Win32 threads/SRW locks) |
| Calls from game threads into the Vulkan layer's OpenXR | 2: `xrLocateViews` (somvr_views.c) and `xrLocateSpace` (somvr_hands.c) through published function pointers | under Proton these would be PE code calling Linux code - see 3.1 |
| Vulkan layer, Linux/Feral-specific parts | pthreads 110 lines (6 files), dlsym/dlopen 19 (4), timespec/clock 26 (4), file opens 21 (7); the engine-name gate "Feral3D"; the fog edit on IndirectX's SPIR-V (somvr_fogpatch.h); the depth/eye/HUD VkImages taken from Feral's textures | no window-system code at all (no Xlib/xcb/wayland) |

## 2. The sample: 42 SITEs (every 9th of the 373, all subsystems)

### 2.1 Re-found in the Windows exe

| Tier | Sample | Whole set (373) | How sure |
|---|---|---|---|
| 1 automatic (winport_match.py) | **24 / 42** | **176** (47 %): K_ENTRY 99/169, K_CALL 54+9/102, K_JMP 2/6, K_VSLOT 21/96 | the 24 checked side by side in the decompiles: 19 the same code (same constants, same control flow, often 25-66 shared field offsets), 5 plausible but too small to prove (an empty `ret`, a 2-line wrapper); **none found wrong** (one wrong pair of the first matcher run, IN_CMD_OFF, scored 0 shared offsets and was dropped by the final run) |
| COM ABI (no search) | 3 / 42 (D3D_COPYRESOURCE, DC_PSSH, DC_DRAWI) | **26** (7 %) | the D3D11 interface order is fixed |
| 2 neighbourhood + offsets (winport_verify.py, ~1 min machine, ~1 min reading each) | 4 / 15 tried (IN_TICK = CWBInput::vf_04 at 0x140dfc670: identical +0x23c/+0x238/+0x10 logic; PY_STREAK 0x140ce0990; the crosshair delegate 0x140645b60; RD_BRT_B's target 0x140d61a90) | ~1/3 of the remaining 169 = ~55 (extrapolated) | confirmed by reading |
| 3 by hand | 1 quick (BZ_SKILL_CAT: the records DB category "Inventory/Skill/Skills" is registered inline in a 40 KB MSVC static initialiser 0x1407b2420; the category pointer lands in 0x1418f9308 at 0x1407ba8c2 - 1 minute from the string) ; 1 diagnosed as a design change (CP_FX: Linux's 2-line ClientFX create wrapper FUN_00d8c7f0 is inlined into its 70 callers on Windows, so the hook must move to the inner create or the inlined flag test); 9 not resolved in the few minutes tried (G221 WBOnlineService slot 221, HEAD_RENDERABLE_VT, IN_CMD_OFF, GL_UIS_GAMEUI, BR_NODELOG, HP_DRAW_FWD (candidate 0x140dd4c60 has the same LOD-distance maths but other offsets), HP_SUBMIT_SHADOW, HP_CLOTH_STORE, RD_PSYS) | ~114 remaining (extrapolated) | 5 of those 9 are the renderer/model code (headparts, particles) where Feral's port changed object sizes (CRendererHandle::vf_05 allocates 0x130 bytes on Linux, 0x160 on Windows) |

By subsystem (tier 1 + COM over the whole set; "left" = tier 2 or 3):

| Subsystem (files) | Sites | Auto | COM | Left |
|---|---|---|---|---|
| A camera/stereo/HUD core (somvr.c H*, G12, RES, D3D, VK, HUD, ES, ARM, ...) | 45 | 10 | 6 | 27 (+2 VkImage) |
| B input/pointer/menus (somvr.c IN, RUM, LP, GL) | 26 | 11 | 0 | 15 |
| C combat/bow/slots (somvr.c CB, RX, DF, TH, BM, FB, SL, BOW) | 78 | 56 | 0 | 22 |
| D movement/climb (somvr.c LOCO, CL; step) | 28 | 18 | 0 | 10 |
| E render effects (render, headparts, body) | 72 | 6 | 20 | 46 |
| F kills/finish/execute/front/fight/parry/bowhit/quiver | 48 | 33 | 0 | 15 |
| G nemesis/captains/brand/beasts | 16 | 10 | 0 | 6 |
| H practice/records DB/session/story | 21 | 10 | 0 | 11 |
| I UI/HUD prompts/collect/gameopts/minimap/objective/... | 39 | 22 | 0 | 17 |
| **Total** | **373** | **176** | **26** | **169** (+2) |

### 2.2 Struct offsets: GCC/Feral vs MSVC (17 paired functions compared)

- Small offsets (< ~0x200 into an object) are the same almost everywhere: H1's target shares 27 of its 28 field
  offsets with the Windows code, FB_FSM 66 of 78, CO_TGEOM_A 47 of 60, FR_SCORE's caller 47 of 51 (the rest are
  stack/temporary noise of the two compilers).
- **Large objects shift past some member** (8 of the 17 pairs had at least one moved field): the AI/character
  object `+0xd2c` -> `+0xd3c` and an argument struct `+0x44` -> `+0x4c` (KL_AI_ACT1's caller); the render state
  `+0x2708` -> `+0x2700` (H1's caller); the HUD object `+0x2a48` -> `+0x2a38` and `+0x2ec0` -> `+0x2ea8`
  (HUD_PASS_EYE: param_1[0x549] vs [0x547], [0x5d8] vs [0x5d5]); `+0xa8` -> `+0xa0` in the game root (LP_IM_DOWN);
  KP_FM_BIND `0xdfc/0xe08` -> `0xe04/0xe18`. Differences of 8-0x18 bytes: STL member sizes (libstdc++ vs MSVC STL:
  std::map/std::function/std::mutex differ), `long`, and Feral's own members.
- **Vtable slots: one fewer on MSVC after the destructor** (GCC's complete + deleting destructor pair vs MSVC's one
  scalar deleting destructor): every paired vtable aligned with delta -1 (PlayerMovement, CPhysicsCharacter,
  CProjectile, LookAtTrigger, CPlayerObj, NPC); H1's caller calls `*vt + 0x358` on Linux, `+0x350` on Windows.
- So every offset needs checking, but against a paired function it is mechanical (the same code with shifted
  constants). Offsets with no paired function nearby are the expensive ones.

## 3. Renderer and XR

### 3.1 Proton: game hooks in a Windows DLL, the Vulkan layer stays a Linux layer

Under Proton the game is D3D11 -> DXVK -> winevulkan -> the host Vulkan loader -> **our layer** -> RADV. The layer is
loaded exactly as now (`VK_LAYER_PATH`/`VK_INSTANCE_LAYERS` in the launch environment; the host loader is the one
winevulkan uses) and keeps talking OpenXR to WiVRn with the host `libopenxr_loader` - no wineopenxr (whose layer
conversion bug the Dishonored port had to work around), no Windows OpenXR loader. It works because the layer never
identified the game's draws: the eye images are copied by the game-side hooks (H8: `CopyResource` into our own
`eyeTex[e]` on the game's D3D11 context) and handed over as VkImages; the layer only copies them into the XR swapchain
at the present. What changes:

| Piece | Linux now | Under Proton |
|---|---|---|
| Finding the shared structs | `dlsym(RTLD_DEFAULT, "somvr_shared")` (9 structs) | same process, but the PE DLL is not in the ELF scope: the DLL writes the 9 addresses into a small file named by an env token (`SOMVR_*` variables reach the Windows side: Wine copies the Unix environment), the layer reads it in place of dlsym (19 lines) |
| xrLocateViews / xrLocateSpace from the game thread | the preload calls the published PFNs | **decided: no PE -> Linux calls.** The layer runs a locate thread (every 1 ms with the latest predicted display time, plus the action spaces) and publishes the views/poses under the existing seqlocks; the game thread reads the newest (<= 1 ms older than now). A direct `sysv_abi` call would mostly work but runs Linux code on a Wine thread outside Wine's syscall frame (signals, SEH) - not worth the risk |
| Eye/HUD/depth VkImages | Feral's `CVulkanTexture+0x108` | DXVK's `IDXGIVkInteropSurface::GetVulkanImageInfo` (also returns the image's layout); VkImages should reach the layer unwrapped (winevulkan wraps the dispatchable handles and a few others such as surfaces and command pools, not images) - check at the first boot |
| D3D11 hooks (26 Idx* slots) | Feral's IndirectX vtables in the ELF | the runtime's COM vtables (DXVK's), patched on the objects the game creates |
| Present | Feral's present on the render thread | DXVK presents on its own queue thread; the tags still pair 1:1 with the presents (one IDXGISwapChain::Present each) - recheck the pacing (DXVK's frame latency) |
| Engine gate | `engineName == "Feral3D"` | `"DXVK"` plus the exe check |
| Fog edit (re-xr-pipeline.md 137) | SPIR-V of IndirectX's DXBC translation | DXVK's DXBC compiler emits different SPIR-V. **Decided: move the edit to the DXBC** (the DLL hooks `ID3D11Device::CreatePixelShader` and rewrites the two fog shaders' bytecode, checksum recomputed) - one edit for Proton and native Windows |
| Hands' depth test, menus, quads, void, vignette, fade, threat cues, objective markers | the layer's own Vulkan drawing | unchanged |

### 3.2 Native Windows players

| | W1: ship DXVK (d3d11.dll + dxgi.dll in the game folder) + our layer as a Windows Vulkan layer | W2: an OpenXR D3D11 path (XR_KHR_D3D11_enable, no Vulkan) |
|---|---|---|
| Renderer path | the same as Proton (DXVK -> Vulkan -> our layer): one code path for everyone | a second path: the eye copy is easy (CopyResource into the XR swapchain images), but everything the layer draws (menus 1.2k lines, void/env 2.3k, VR hands 1.7k, objective, perf, fade, pointer, vignette, threat glows) is Vulkan and would be rewritten in D3D11 |
| Layer changes | the Linux-specific parts listed in 1 (pthreads, dlopen of the OpenXR loader -> LoadLibrary `openxr_loader.dll`, `XR_KHR_convert_timespec_time` -> `XR_KHR_win32_convert_performance_counter_time`, the struct lookup -> GetProcAddress on our DLL) | the layer is dropped; its OpenXR session code moves into the DLL |
| Installing the layer | no registry, no admin: an explicit layer JSON + DLL in the mod's folder; the proxy DLL sets `VK_ADD_LAYER_PATH` (loader >= 1.3.234; else `VK_LAYER_PATH`) and `VK_INSTANCE_LAYERS` with SetEnvironmentVariable before d3d11.dll (DXVK) creates its instance. An implicit layer in HKCU would load into every Vulkan app - avoid | n/a |
| OpenXR runtimes | needs XR_KHR_vulkan_enable(2): SteamVR and Meta's PC runtime have it; Virtual Desktop's VDXR and others - verify | every Windows runtime has D3D11 |
| Pitfalls | DXVK is not supported upstream on Windows (works for most D3D11 games; no anti-cheat here); overlays that hook D3D11/DXGI (RTSS, some capture tools) may not show or may crash - Steam's overlay supports Vulkan; first-run shader compiles (mostly hidden on drivers with VK_EXT_graphics_pipeline_library); DXVK needs Vulkan 1.3 drivers; the game's own D3D11 is replaced for that install (an uninstall = delete the two DLLs) | none from DXVK; the cost is the rewrite |
| Size | ~2-3 helper-days after the Proton port | ~2-3 helper-weeks |
| Testable here | **no** (no Windows PC with a GPU in this workspace; Wine is not Windows) | no |

## 4. Loading, toolchain, test harness

- **Loading**: a proxy DLL in the game folder. The exe imports XINPUT9_1_0, WINMM, PSAPI, d3d11, dxgi, steam_api64
  (Steam build), ...; **xinput9_1_0.dll** is the choice (tiny API to forward to the system's, not a KnownDLL, no
  clash with DXVK's d3d11/dxgi, and the game's pad input passes through it). Under Proton:
  `WINEDLLOVERRIDES="xinput9_1_0=n,b"`. The DLL starts its work from the loader callback's thread-safe point (a
  thread or the first XInput call), checks the exe's hash (GOG and Steam-Windows tables keyed separately), patches
  by RVA (the exe is DYNAMIC_BASE: base + RVA).
- **Toolchain**: present: clang/clang-cl, lld-link, the xwin MSVC CRT/SDK (shared/toolchain/xwin-*); no mingw.
  Build the DLL with `clang --target=x86_64-pc-windows-msvc` (GNU C extensions and AT&T asm still work) against xwin.
  ABI: the game is MSVC Win64, so casts to game functions and C hooks just compile for that target (the
  IPA-RA asm wrappers go away); hooks that read the caller's registers or stack slots (e.g. `rsp+0x1c` in H8's
  notes, the vignette's `sp+0x144`) are per-site rewrites - part of each site's work.
- **Harness**: `proton run` (Steam's Proton Experimental) with a prefix of our own under logs/winport/, the layer
  and Monado as now, audio into our null sink, `WINEDLLOVERRIDES`; the save guard moves to the prefix's save
  folder (to confirm at the first boot: the non-Steam builds keep saves under the prefix's
  `Documents\WB Games\Shadow of Mordor`); the drivers talk to the mod's command channel (sockets on the layer side
  stay Linux; the DLL side reads commands from a file or the shared struct). selftest.c's stand-ins are Linux-only:
  the Windows build gets the platform-neutral tests (math, logic) and the site checks against the exe file.

## 5. GOG exe and a Proton boot

Not done: the GOG download had no exe yet (3.7 GB of ~50 GB at 18:42). When it lands: hash it, and if it differs
from the Steam Windows exe, analyse it (~6 min auto-analysis like som-win64) and re-run winport_match.py against it -
for a same-compiler rebuild most functions are byte-identical apart from addresses, so the Steam-Windows tables
transfer automatically (estimate 1-2 helper-hours). Key the Windows tables on the GOG exe.

## 6. Estimate

Helper-hours are one helper's working time; calendar time assumes 5 helpers (4-6) and **one game slot** (boots are
serialised, ~5 min each plus analysis).

| Work item | Measured basis | Helper-hours |
|---|---|---|
| W0 DLL scaffold: proxy, exe gate, patch engine (RVA + bytes, VirtualAlloc-near trampolines), platform shim, clang/xwin build, logging/settings paths | ~60 Linux-specific places, 16 asm wrappers | 12-20 |
| W0 bridge: the address file, the layer's lookup, the 1 ms locate thread | 9 structs, 2 PE->XR calls | 6-10 |
| W0 harness: proton run, prefix, null sink, save guard, command channel | | 8-12 |
| Sites tier 1: verify 176 auto pairs side by side (batch decompiles) | 24/24 sampled correct or plausible | 4-6 |
| Sites COM: 26 Idx* slots -> runtime vtables | | 2-4 |
| Sites tier 2: ~55 | 4 of 15 in ~1 min each | 6-10 |
| Sites tier 3: ~114 by hand | 1 in 1 min; 1 inlined (a design change); 9 not in a few minutes, 5 of them renderer | 40-75 |
| Offsets + globals: ~900 offset uses, 117 globals, the slot -1 rule | 17 pairs: small offsets kept, big objects shift | 15-25 |
| Hook semantics: register/stack reads, inlined wrappers | CP_FX-like cases | 10-20 |
| DXVK interop (eye/HUD/depth VkImages), present pacing under DXVK, fog edit in DXBC | 2 VkImage sites, 1 shader pair | 10-20 |
| Bring-up boots under Proton (40-60 boots incl. fixes) | serialised | 15-30 |
| **Proton total** | | **~130-230** |
| Native Windows W1 (DXVK shipped + Windows layer build + loader env) | layer's ~180 Linux-specific lines | +16-30 (untestable here) |
| Native Windows W2 (OpenXR D3D11 path) | ~10k lines of layer drawing | +80-150 (untestable here) |

**Calendar (5 helpers):**
- Day 1: W0 (one helper, the critical path) while four helpers find sites statically by subsystem (no game slot
  needed). End of day 1 / day 2: the first Proton boot (DLL inert -> mono quad -> stereo with group A's sites).
- Days 2-4: the rest of the static work finishes; subsystems come up live one by one (the single slot is the limit).
- **First head-tracked stereo build under Proton (camera/stereo/HUD, input, menus, hands): 2-3 days. Parity with
  build 31 (all 373 sites and switches): 4-6 days.** The coordinator's 1-2 weeks is the right figure for parity
  with risks; **1-2 days is realistic only for the first playable subset** (groups A + B + the bridge), not for the
  combat/kills/climb/render-effects work.

**Critical path:** W0 (DLL + bridge + harness) -> group A (camera/stereo core, 27 left + DXVK interop) -> the first
stereo boot -> combat C/F live (the user's main feature) -> render effects E (the most manual: 46 left).

**What could make it 3x worse:**
1. Tier 3 at 1-2 h per site instead of 20-40 min (renderer/model code that Feral changed; 46 of the 169 left are
   group E). Mitigation: the render effects are comfort/polish - ship parity without the hardest of them first.
2. Hooks that relied on GCC's register allocation or on functions MSVC inlined (CP_FX-like): each becomes a redesign.
3. DXVK interop details (layouts, the queue thread, frame pacing) costing many boots on one slot.
4. The GOG exe being a different build from the analysed one (then a re-match, mostly automatic).
5. The single game slot: 6 helpers' bring-up boots queue behind each other.

**Recommendation: Proton first, on the GOG exe; native Windows later through W1 (ship DXVK + our layer as a Windows
layer), not W2.** Proton keeps the WiVRn/Monado path, the layer, the headless harness and the user's tests on this PC;
W1 reuses all of it (one renderer path) and costs days, W2 weeks; neither native path can be tested here, so it waits
for a Windows tester.

**Split for parallel helpers (static first, by subsystem; each delivers Windows site lines + offset values for its
files, keyed on the exe, plus notes of semantic changes):**
1. `winport-base` (critical path): W0 scaffold, bridge, harness, the DXVK interop, the first boots.
2. `winport-core`: group A (somvr.c camera/stereo/HUD core + the 26 COM slots) - feeds the first stereo boot.
3. `winport-combat`: groups C + F (somvr.c combat/bow/slots; kills, finish, execute, front, fight, parry, bowhit, quiver).
4. `winport-world`: groups B + D + G + H (input/pointer/menus, movement/climb/step, nemesis/captains/brand/beasts,
   practice/records/session/story).
5. `winport-render`: group E (render, headparts, body) + the fog edit in DXBC.
6. (optional) `winport-ui`: group I (prompts, collect, gameopts, minimap, objective, hudfx, vbglow, reticle, screens,
   cprompt) and the shared offset-diff tool for everyone (the side-by-side offset comparison of 2.2 per #define).

Each helper starts from `logs/winport/m3/sites.tsv` (the tier-1 answer per site), `pairs.tsv` (14,868 function pairs)
and `scripts/winport_verify.py` for its remaining sites.
