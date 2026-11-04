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
| Installing the layer | no registry, no admin: an explicit layer JSON + DLL in the mod's folder; the proxy DLL sets `VK_ADD_LAYER_PATH` (loader >= 1.3.234; else `VK_LAYER_PATH`) and `VK_INSTANCE_LAYERS` with SetEnvironmentVariable before d3d11.dll (DXVK) creates its instance. An implicit layer in HKCU would load into every Vulkan app - avoid. The loader ignores the env path at high integrity (run as administrator; Wine's token): 7.1 "W1" | n/a |
| OpenXR runtimes | needs XR_KHR_vulkan_enable(2): SteamVR and Meta's PC runtime have it; Virtual Desktop's VDXR and others - verify | every Windows runtime has D3D11 |
| Pitfalls | DXVK is not supported upstream on Windows (works for most D3D11 games; no anti-cheat here); overlays that hook D3D11/DXGI (RTSS, some capture tools) may not show or may crash - Steam's overlay supports Vulkan; first-run shader compiles (mostly hidden on drivers with VK_EXT_graphics_pipeline_library); DXVK needs Vulkan 1.3 drivers; the game's own D3D11 is replaced for that install (an uninstall = delete the two DLLs) | none from DXVK; the cost is the rewrite |
| Size | ~2-3 helper-days after the Proton port | ~2-3 helper-weeks |
| Testable here | the Windows-side stack under Proton (7.1 "W1": DXVK + the Khronos loader + our Windows layer + openxr_loader.dll -> wineopenxr), not the Windows runtimes | no |

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

### 7.1 winport-base: the DLL, the bridge, the harness, the first runs (2026-11-02, branch t3/som-winport-base-task)

**Build** (`mod/somvr/win/build.sh --check`, README there): the whole game side (somvr.c with every *_game.c, the 52 CORE
files) compiles unchanged in substance for `x86_64-pc-windows-msvc` (clang 22 + lld-link) against a new x64 xwin splat,
`shared/toolchain/xwin-sdk64` (the old `xwin-sdk` had x86 libraries only; same CRT 14.44 / SDK 26100 from the cached
manifest). Output `build/win/xinput9_1_0.dll`: static CRT, imports KERNEL32 only, exports the four XInput functions +
`somvr_win_bridge`. The Linux-only code is fenced with `#ifdef _WIN32` in five places (the perf_event write watches,
the ELF gate + constructor, /proc/self/maps -> VirtualQuery, st_mtim, WRAPPER's ELF directives); everything else goes
through `win/compat/` (stand-in POSIX headers + `somvr_compat.h`, forced into every TU): pthreads on SRW locks /
condition variables / _beginthreadex, clock_gettime on QPC, nanosleep on a high-resolution waitable timer, mmap /
mprotect / munmap on VirtualAlloc / VirtualProtect, `somvr_alloc_near` (a page within +-2 GB of the image),
process_vm_readv/writev (the peeks) = a cached VirtualQuery check + an SEH-guarded copy (no syscall per read),
newlocale/uselocale, dlsym on GetProcAddress, and **paths**: every `SOMVR_*` path is a Unix path; under Wine `/x/y` is
opened as `Z:\x\y` (open/fopen/stat/access/unlink/rename/mkdir wrappers). Traps: `long` is 32-bit on Win64 - the logs'
`(unsigned long)addr` casts print 0x40xxxxxx for 0x140xxxxxx (log only; -Wshorten-64-to-32 finds no real truncation);
`strtod_l` = `_strtod_l`; posix_memalign = malloc (free() must work on it).

**Tables** (`win/README.md` 1-3, `wintables.py`): one row per SITE name, keyed on the Steam-Win exe's RVAs; every site
table is `SITE_TABLE` (Linux: const; Windows: the writable section `.somvrst`). At DllMain the DLL rewrites each entry
by name (base + RVA; relocated qwords in vtable contexts moved by base - 0x140000000) or blanks it (check_site: "no
Windows site"). **Trap found at the first run**: each table is its own 16-aligned piece of `.somvrst$m`, so 8 zero
bytes sit between tables whose size is 8 mod 16 (an entry is 56 bytes): a 56-byte stride walk shifts by 8 and
scribbles over the next table (a NULL ctx_hex -> an access violation in parse_hex); the walk steps over zero qwords.
`SOMVR_WIN_SITES=0` blanks every site (the inert first game boot). After the merge of t3/winport-integration: 178
live rows, 26 COM rows, 24 none, 0 errors.

**Start**: DllMain (loader lock) does only: the exe's sha256 (the tables' exe, else nothing and no log), the site
rewrite, the IAT redirects of d3d11!D3D11CreateDevice and dxgi!CreateDXGIFactory, and a 16-byte jump over the exe's
entry point. `win_entry` then runs on the main thread before any game code and outside the loader lock: puts the
entry back, `somvr_core_win_globals()` (winport-core's globals from RVAs), `somvr_start` (all switches, checks, hooks,
threads - 4-5 ms), the bridge file, then the game's own entry. `SOMVR_WIN_TEST=1`: any exe passes with every site
blank (the stand-in program).

**Patch engine**: somvr.c's own call/slot/jmp patches (write_code, the stub page) work as on Linux through the shim
(the stub page's low-address probes fail on Windows: those sites move to `somvr_alloc_near`). Entry trampolines and
mid-function detours: `win/somvr_win_hook.h` on **MinHook 1.3.4** (BSD-2-Clause, fetched pinned by
scripts/fetch_third_party.sh into third_party/minhook-1.3.4; its decoder relocates the overwritten instructions):
`somvr_win_hook_entry(fn, hook, &orig)` and `somvr_win_hook_mid(at, cfn)` (cfn gets `struct somvr_win_regs` = somvr.c's
struct regs + rbx/rbp/r12-r15/rflags/rsp, may change them; the flags are kept). Both proven under Wine on the stand-in
(an entry hook adding 1000 to a result, a detour changing rcx). For OB_RANGE's replacement, the FX wrappers' entry
0x140432b00, RES_3D_DESC's detour at 0xd4dc69 (7.2, 7.6). Hooks via WRAPPER keep working: on COFF the wrapper passes
the regs in rcx with the 32-byte home area; the C side must read the Win64 argument registers (rcx, rdx, r8, r9).

**Bridge** (`somvr_xrloc.h`, `vklayer/somvr_bridge_layer.c`, `vklayer/somvr_xrloc_layer.c`, `win/somvr_win_bridge.c`):
the DLL writes `SOMVR_BRIDGE` (a Unix path from the launcher; text: "somvr-bridge 1", `pid` = the Unix pid read from
`Z:\proc\self\stat`, the exe's sha, then `name 0xaddr` for the 9 shared objects, `somvr_xrloc`, and the two PE
stand-ins `locate_views` / `locate_space`). The layer's `somvr_obj(name)` replaces the 9 `dlsym(RTLD_DEFAULT)` calls
(a file whose pid is not ours is ignored); with a PE bridge it stores the DLL's stand-ins in `somvr_shared.xr.locate_views`
and `somvr_input.xr.locate_space` instead of the runtime's functions and starts the **locate thread** at the session's
RUNNING (joined in xr_unpublish): every 1 ms the views and the 4 action spaces (+ velocities) at the newest predicted
time and one period later, under `somvr_xrloc.seq`, every call inside the provider guard. somvr_views.c and
somvr_hands.c are unchanged: their PFN calls land in the DLL, which answers from the nearest sample. Engine gate:
`Feral3D`, or `DXVK` with a valid bridge file for this process (= the DLL's exe gate passed).

**DXVK interop** (`win/somvr_win_d3d.c`): D3D11CreateDevice -> the device and immediate context (their vtables fill the
ID3D11Device / ID3D11DeviceContext COM rows), CreateDXGIFactory -> its CreateSwapChain (slot 10) -> the swap chain's
Present (slot 8, hooked first; H9's COM row then records our hook as its target and chains through it). First Present:
the IDXGISwapChain and ID3D11Texture2D rows from the back buffer, its VkImage through
`IDXGIVkInteropSurface::GetVulkanImageInfo` (IID 5546cf8c-77e7-4341-b05d-8d4d5000e77d, slot 4), then
`somvr_core_win_com()`. Every Present: an interval histogram (a WIN PRESENT line per 10 s). The late install of the COM
hooks themselves (patching the filled slots) is still to do - see the hand-off.

**Harness** (`scripts/headless_win.sh MOD --test xinput | --test d3d S | --game S`): our own prefix
`logs/winport/prefix` (made once by `proton run`, so DXVK is in its system32), the DLL installed into its system32
before every run, `WINEDLLOVERRIDES=xinput9_1_0=n,b;d3d11,d3d10core,dxgi,d3d9=n` and **no audio driver at all**
(winepulse/winealsa/wineoss disabled: nothing can reach a real output), Monado's null compositor and a headless
gamescope as headless_vr.sh, the layer by VK_LAYER_PATH. **Trap**: `proton run` / `waitforexitandrun` go through
Proton's steam.exe, which here (the user's Steam client running, OpenComposite as the OpenVR runtime) initialises
OpenVR and never lets even `cmd /c echo` exit; the harness runs Proton's own `files/bin/wine` directly with
WINEPREFIX - no steam.exe, no Steam contact at all, which is what the save rule wants anyway. XDG_CONFIG_HOME points
into the run folder (no openvrpaths).

**Runs so far** (no game: the GOG download was at 4.8 GB of ~50 at 21:00, no exe): the stand-in `d3dtest.exe` (imports
XINPUT9_1_0, D3D11CreateDevice and CreateDXGIFactory like the game): (a) DLL loaded native, gate (test), 373 sites
blank, entry redirect, somvr_start rc=0 in 4.2 ms with the whole CONFIG block, the bridge file with the right Unix pid,
XInput forwarded to xinput1_4 (1167 = not connected), clean exit with the END lines; the hook test 1012 / 84.
(b) and (c) on the stand-in (`headless_win.sh MOD --test d3d 60`: DXVK in a headless gamescope, the layer, Monado's
null compositor; runs 20261102-225448 / -225900 / -230309): the layer takes the DXVK instance, reads the bridge file,
finds somvr_shared ("per-eye path on"), runs the session and the locate thread (~1000 ticks/s); the stand-in's
`somvr_views_locate` through the PE stand-in got 5402 fresh views in 60 s with the simulated head moving (no PE ->
Linux call anywhere); H9 installed late on DXVK's IDXGISwapChain::Present and its tags reach the layer ("tag yes");
the frames go to Monado as the mono quad ("mono frame ... copied 1, layer quad", 600+ of them at the null
compositor's 20 Hz; the WIN PRESENT histogram: 20.0 fps, 50 ms each). **The VkImages reach the layer unwrapped**: the
two eye textures' handles from IDXGIVkInteropSurface::GetVulkanImageInfo (0x5555849b1150 / ...1760) are the ones
the layer's vkCreateImage hook logs as eye image candidates. DXVK keeps them idle in **VK_IMAGE_LAYOUT_GENERAL**
(1), not Feral's TRANSFER_DST_OPTIMAL: the DLL records each of its images' idle layout in somvr_xrloc (v2) and the
layer's img_barrier uses it (one place for the eyes, the HUD and the hands' depth). Traps found: the DLL's
`Z:\proc\self` is the wineserver's (Wine opens files in the server), so the bridge file is matched by a nonce in
somvr_xrloc, read with process_vm_readv; the stand-in has to make its eye textures after the session runs (as H8
does) or the layer's image hook does not know somvr_shared yet. The stand-in's views count as "stale" because it
asks every 11 ms against the 50 ms frame (somvr_views extrapolates a period each time) - the game asks once a frame.

**The GOG exe** (x64/ShadowOfMordor.exe, landed 22:44) **is byte-identical to the analysed Steam-Windows exe** (sha256
f5d72578...): every table applies as it is, no re-match (the tables' tag "steamwin" covers both; GOG ships its own
steam_api64.dll over Galaxy64.dll). `scripts/winport_exe_map.py` + `wintables.py --extra-exe` stay ready for another
build (identity test: 309 of 311 rows, 55/55 globals carried byte-identical).

**The first GOG boots** (`headless_win.sh MOD --game 150`, runs 20261102-230848 .. -232947): (a) the DLL inert
(`SOMVR_WIN_SITES=0`): loads from our system32, gate "steamwin", somvr_start in 4 ms, the bridge; (b) the layer takes
DXVK's instance and shows the game as the mono quad ("mono frame ... copied 1, layer quad", 2400+ in 2 min at the null
compositor's 20 Hz; WIN PRESENT 20.0 fps); the game reaches its first-run Gamma screen; (c) the back buffer's VkImage
through the interop. Then **with the sites live** (295 live, 18 SysV-blank, the stereo branch merged): H1 H2 H3 H4 H6
H8 H3_OVR installed at start, H9 late on DXVK's Present, the stub page at image base - 0x10000; H2/H3/H6/H9 fire 20/s
in the menu and the game still renders the Gamma screen. Traps: (1) a read-only bind of the install makes the game
fail "unable to load game resources" (it opens its archives for writing): the harness mounts the install as an
overlay whose writes land in the run folder (it wrote nothing); the working folder is entered inside the namespace
(`bwrap --chdir`), else the game gets the real folder's inode; (2) with no audio driver the game stalls black after
CreateSwapChain (Wwise wants a device): the harness gives Wine ALSA's null plugin only, in a namespace without
/dev/snd and without the PulseAudio/PipeWire sockets - a sound device for the game, no path to a real output;
(3) the kills group's stub pages still probe low Linux addresses (KL_AI_* "no stub page within +-2 GB") - for
winport-hooks. Saves and settings: the prefix's `Documents\WB Games\Shadow of Mordor` (settings.cfg, render.cfg).

**Stereo in the world** (11-03 00:49-01:00, runs 20261103-004901 / -005524; `WIN_ENV_FILE=scripts/headset-build26.env`,
`SOMVR_EYE_SIZE=2015x2217`, `WIN_SAVE_FROM=` a copy of the user's Linux save, `WIN_CMDS=scripts/cmds/win-stereo2.cmd`):
**the Linux save loads on the GOG build as it is** (Continue Story Mode 21 %; the GOG steam_api64.dll keeps its "remote
storage" files in the prefix's `Documents\WB Games\Shadow of Mordor\Saves`; the prefix copy changes, the user's file is
only read). The world in ~40 s, `SESSION ui ... ctx=world`, SOMVR_PRACTICE=auto's blink to the meadow (arrived, quiet,
the card), stereo frames at the null compositor's 20 Hz: H1/H2/H3 40 a second, layout B from the eye images, both
eyes applied, 5000+ stereo frames in 4 minutes, `locates` = one per frame through the PE stand-ins; the HUD quad
(1.5 m, 43 x 25 deg) and the VR menu's card; the scripted head (`xrtest head 0 0 0 yaw 60`) turns the game's views
60 deg (frame 700 vs 300 composed). Fixed on the way: the late D3D check counted Feral's VK_TEX_COPY sites (none under
DXVK) and H8's first call came before the first Present (eyes off for the run); two offsets (offsets-base.tsv):
IN_PAUSE_FLAG 0x34 -> 0x3c (paused stuck = ctx menu = the HUD as a menu board, no practice), FB_SH_CINE 0xb0 -> 0xa8
(the fallback stuck in "cine" = flat frames). Unchecked, for its owners: FI_SH_FXMGR 0x80c78 (the shell shifts -8 in
that range; the Windows exe uses both 0x80c70 and 0x80c78).

**The first combat in the world** (11-03 01:29, run 20261103-012944, `cmds/win-combat1.cmd`, SOMVR_HANDS_TEST=1): the
records-DB schema checker (winport-hooks) confirms 12 of 12 holders by their fields in 1 s; the card put away by the
test controller's A, the test hands at rest, the sword drawn from the right shoulder (SLOTS: drawn, kept in the hand),
an infantry spawned in the meadow (after the ray fix below), the test guard learning and blocking its strikes (48
attacks, `DEFENCE contact ... the blades met` + the orc's recoil, 26 PARRY lines incl. a dodge's opening), 21
COMBAT hits (light and opening tiers), and the killing blow: `KILLS start` picks the row ('AI_Bip_PC_Standing_InstaKill_F'
for a front stab) but **its half does not play on Windows** (`KILLS kill ... 268.44 s in (its half not playing ...)`: the
row's play time reads garbage, so it ends by Combat_DealHit) - for winport-combat/hooks; the death comes, the "Kill +10"
popup shows, both eyes composed with the orc at arm's length, the blade, the hurt flash. Fixes on the way: the physics
ray's slot (the live IEnginePhysics vptr is 0x141481d30, 2 qwords after the tables' vtable start: slot 63 = the usual
-1, not 65 - every practice spawn and climb ray failed), FI_SH_FXMGR 0x80c78 -> 0x80c70 (the -8 shift; checked before
the ClientFX create calls: 32 Linux / 38 Windows loads).

**Note (11-03 21:40, som-b33-grass, re-xr-pipeline.md 148.6): every Windows headless number above and below until then was taken at Low** - the slots' prefixes (logs/winport/prefix, -slot2, -slot3) hold `VegetationQuality`, `MeshQuality`, `ShadowQuality`, `LightingQuality`, `TextureQuality`, `AmbientOcclusionQuality` 0, and headless_vr.sh did not apply SOMVR_RENDER_SET on Windows (no grass cards in the meadow, the low meshes and shadows), while the user's sessions run the PC's detection with the VR preset (som-win-session.sh). From then on headless_win.sh writes the same render.cfg into the slot's prefix for each game run and puts the old one back on exit (`render.cfg: the PC's detection with the VR preset ..` in summary.txt; RUN/render.cfg.seeded / .before); `HEADLESS_RENDER=asis` keeps the Low file. Re-measure frame costs before comparing them with Linux's. **11-04 (som-b37-slotsvram, re-xr-pipeline.md 158.5):** the preset is now a per-boot mode, `WIN_RENDER=vr|light` - vr = this preset (slot 1's default; every looks / render / perf / soak boot), light = the same with TextureQuality 1 (2.3 instead of 6.2 GB of VRAM; slots 2-3's default, the behaviour boots); RUN/render.mode and launch-cmd.txt's `# render` line say which look a run had; game_slot.sh's take waits until the boot's VRAM fits.

**Frame cost, fog, climb and bow on Windows** (11-03 01:51-02:39; runs 20261103-015105 / -021548 / -022457 / -023452;
headless: Monado's null compositor at 20 Hz, the GPU idling at 500 MHz between frames - absolute GPU times are not
comparable with a paced 80-90 Hz session, the CPU split is). Per stereo frame, p50 (perf27's places):

| Place | Game thread busy | ours in H4 | the game's render thread (CPU) | dxvk-cs | dxvk-submit | dxvk-queue | GPU, the game |
|---|---|---|---|---|---|---|---|
| Meadow | 2.03 | 0.23 | 5.89 | 3.48 | 1.40 | 0.54 | 3.99 |
| Meadow, turning | 1.93 | 0.21 | 5.18 | 3.10 | 1.44 | 0.46 | 3.97 |
| Crowd of 8 | 2.81 | 0.29 | 6.11 | 3.60 | 1.50 | 0.56 | 4.21 |
| Campfire | 2.19 | 0.23 | 6.10 | 3.59 | 1.39 | 0.59 | 4.62 |

- **The render thread is no longer the limit**: on Linux it did the game's D3D11 calls and Feral's translation in one
  thread (10-13 ms a frame, 115.3); under DXVK the game's render thread is 5.2-6.1 ms and the Vulkan recording runs on
  DXVK's CS thread in parallel (3.1-3.6 ms), its submit thread 1.4 ms: the critical path about half of Linux's. The
  game thread 1.9-2.8 ms (Linux 3.4-5.4) and our H4 work 0.21-0.29 ms (Linux 1.1-1.9: the peeks are plain copies
  behind a cached VirtualQuery here, not process_vm_readv calls). The whole process: 18-23 ms of CPU a frame over 74
  threads. The present path (ours) 0.9-1.0 ms wall.
- How measured: the per-thread CPU from /proc/self/task/*/schedstat by thread name (the layer; the DLL names the
  Present caller somvr-render) - under Wine GetThreadTimes ticks 10 ms and a raw syscall from PE code is caught by
  the syscall user dispatch, so the DLL's own PERF cpu columns are 0/10 ms noise (its wall-based "busy" stands); the
  GPU from the layer's timestamps around DXVK's vkQueueSubmit2 (8-9 submits a frame). Not yet: the split at the eye
  copies (no vkCmdCopyImage(2) into the eye images seen: DXVK copies otherwise), the game thread's CPU (somvr-game's
  name does not stick: the game renames its main thread).
- **The split at the eye copies and the game thread** (11-03 11:17, run 20261103-111733, win-perf1 again; the same
  idle-clock caveat): per stereo frame, p50 ms -

  | Place | GPU, the game | eye 0 + the frame's shared work | eye 1 | the rest (HUD, UI, composite) | ours | game thread CPU | render thread CPU |
  |---|---|---|---|---|---|---|---|
  | Meadow | 4.24 | 2.12 | 1.90 | 0.22 | 0.25 | 1.97 | 5.40 |
  | Meadow, turning | 4.25 | 2.23 | 1.81 | 0.22 | 0.25 | 1.99 | 4.57 |
  | Crowd of 8 | 4.29 | 2.10 | 1.95 | 0.22 | 0.24 | 2.87 | 6.21 |
  | Campfire | 4.87 | 2.43 | 2.19 | 0.22 | 0.26 | 1.97 | 5.45 |

  Eye 1 costs 85-90 % of eye 0 + shared: the second eye's pass is almost the whole first eye's again (shadows and the
  other shared work are a small part under DXVK). How: DXVK writes the eye images with vkCmdCopyImage2 (H8's
  CopyResource; seen from the start - the split was empty because DXVK records frame N+1 on its CS thread while frame N
  is still submitted and presented, so every mark carried the previous frame's number and fell outside its spans): a
  mark now takes its frame from its command buffer's submit (92-99 % of the frames split; Feral the same as before).
  The game thread: `somvr-game` is set at H4's first call (the game names its threads after our start; the render
  thread's name was already set late, at the first Present).
- **(2) The fog**: SOMVR_XR_FOG=radial rewrites both fog shaders at CreatePixelShader (`FOG shader 393bfbda...
  (9048 -> 9096 bytes)`, `e2c07968... (8168 -> 8216)`), both eyes' haze even in the meadow pictures (run -023452 frames
  404/484).
- **(3) The climb** (fling31-drive.sh's ladder plan, driven on the Windows run by `headless_win.sh ... driver`): the
  grab, the glide down over the edge and the hang (`CLIMB ... down ... hang`), PASS the throw = the hang (flingdown
  off), PASS no draw while hanging (sword off), PASS drawn while hanging (sword on); FAIL the fling down (no landing),
  FAIL the hanging chop (no hit) - for winport-world (climb) and -combat.
- **(4) The bow** (bow20-drive.sh auto + aim on Windows): 14 PASS, 1 FAIL - out by the button, nock, full draw 56 cm,
  the shot, the next arrow, none left; at far orcs the assist's exact line and **HIT on 'Bip_Head'** twice; FAIL: the
  3.5 deg-beside shot logged no assist line.

**W1, native Windows players (3.2), built and run under Proton** (11-03 03:00-10:40; commits c0511a5 .. this one).
- **The package** (`scripts/fetch_win_redist.sh` once, `win/build.sh`, `win/package.sh` -> `build/win/package/x64`, 13
  files, 16 MB, SHA256SUMS): next to ShadowOfMordor.exe `xinput9_1_0.dll` (our proxy = the whole game side),
  `d3d11.dll` + `dxgi.dll` (DXVK 2.7.1), `vulkan-1.dll` (the Khronos loader 1.4.341), `openxr_loader.dll` (1.1.61), and
  `somvr\` with `VkLayer_somvr_openxr.dll` + `.json` (our layer, the same sources through win/compat), `somvr.env` (the
  switches, one a line), `README.txt` (win/README-Windows.txt) and `licenses\`. The proxy reads somvr.env, sets
  `VK_ADD_LAYER_PATH=<exe dir>\somvr`, `VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr`, `SOMVR_XR=1` before DXVK's instance
  (natively; under Wine only with `SOMVR_LAYER=win` - the Linux layer stays the default for the user's sessions), and
  the layer finds the game side through the proxy's `somvr_win_bridge` export (no bridge file).
- **The test** (`headless_win.sh MOD --game S` with `WIN_W1=1`): the package into the overlay's upper layer only (the GOG
  folder never sees it), the stack DXVK -> the Khronos loader -> our Windows layer -> openxr_loader.dll -> Proton's
  wineopenxr -> Monado (null). In its own prefix, `logs/winport/prefix-w1` (a reflink copy, made once). Six things
  were found by the boots - 1-4 only under Wine (a Windows PC does not have them), 5-6 on Windows too:
  1. **DXVK prefers winevulkan.dll** (vulkan_loader.cpp tries it before vulkan-1.dll) and so skips the loader and our
     layer: under Wine + SOMVR_LAYER=win the proxy patches d3d11.dll's and dxgi.dll's LoadLibraryA (their IAT entries)
     to refuse that one name. Proven: `Found vkGetInstanceProcAddr in vulkan-1.dll` (0xf200 = the Khronos loader's
     export). Other DXVK instances in the same log are Proton's own DXVK 3.1.1 from system32, loaded by something by
     full path - not the game's device (its swapchain factory is ours, 2.7.1).
  2. **Wine's token has a high integrity level** (0x3000, though not elevated) and the loader then ignores
     VK_ADD_LAYER_PATH and VK_LOADER_DEBUG ("Loader is running with elevated permissions"; its messages go to
     OutputDebugString only - WINEDEBUG=+debugstr shows them): the harness registers the manifest in the prefix's HKLM
     `Khronos\Vulkan\ExplicitLayers` (VK_INSTANCE_LAYERS still enables it). A Windows player at medium integrity
     takes the env path; one who starts the game as administrator gets none (the proxy logs elevation + integrity;
     README says so). Proven: `VK_LAYER_SOMVR_openxr loaded ... loader openxr_loader.dll`, `bridge: W1: the proxy
     DLL's somvr_win_bridge`.
  3. **wineopenxr waits for `HKCU\Software\Wine\VR`**, which Proton's steam.exe fills at a Steam launch
     (vrclient_init_registry -> wineopenxr's `wineopenxr_init_registry`, then `state`=1); without it the runtime's
     negotiation fails at once (-6, `RuntimeInterface::LoadRuntime skipping manifest file C:\openxr\wineopenxr64.json`).
     `d3dtest.exe vrinit` does the same against Monado before the game: the key first (the export opens it and returns
     with only a WARN if it is missing), the call (a BOOL), state=1 - then `openxr_vulkan_instance_extensions` etc. are
     there. These wine runs start a wineserver OUTSIDE the game's namespace, and Wine opens files in the server: the
     harness waits it out (`wineserver -w`) and stops if one still runs, or the game would see the real install.
  4. **wineopenxr reads Vulkan handles as winevulkan's client objects** (openxr.c: `vulkan_instance_from_handle(h)` =
     its second qword; the same for the physical device and the device). Under the Khronos loader the layer's VkDevice
     is winevulkan's (the loader only writes its dispatch over the first qword), but its VkInstance is the loader's and
     its VkPhysicalDevice the loader's terminator: under wineopenxr only, the layer takes winevulkan's own pair from the
     terminator (the ICD's object by winevulkan's magic 0x01CDC0DE, its instance from the ICD term), checks them with
     winevulkan's exports (the instance lists the device; same name/IDs as the game's GPU) and passes them to
     xrGetVulkanGraphicsDeviceKHR and the session binding. Also wineopenxr passes the host's device extensions
     unchanged: `VK_KHR_external_{memory,semaphore,fence}_fd` become winevulkan's `_win32` twins. And the 1 mm lead
     quad of xrEndFrame (Proton's layer-array size bug; Dishonored's workaround) opens every frame under wineopenxr.
  5. **The Khronos loader unloads a layer at vkDestroyInstance** (FreeLibrary), and DXVK makes and drops instances (its
     probes): the XR instance the layer keeps across Vulkan instances leaked with the DLL and the next xrCreateInstance
     failed (XR_ERROR_LIMIT_REACHED, one per process). The layer pins its DLL at load (GetModuleHandleEx PIN) - this
     one is for Windows too.
  6. **The CRT's text mode**: the Linux code opens files without "b"; on Windows every 0x0A written got a 0x0D before
     it, so the first W1 SOMVR_XR_DUMP pictures looked broken (a pink sky, no grass, a band of garbage rows, the card's
     gold turned green: the bytes shifted at each inserted CR). `somvr_fopen` (win/compat) now always adds "b", as
     Linux behaves; the run's dumps repaired offline (each CR before an LF removed) show the right pictures.
- **The stereo boot through the Windows-side stack** (run 20261103-102006, boot 7; scripts/cmds/win-stereo2.cmd, a copy
  of the user's Linux save): `VK_LAYER_SOMVR_openxr loaded`, `the layer DLL ... pinned`, `bridge: W1: the proxy DLL's
  somvr_win_bridge`, `system "Monado: Simulated HMD"` through openxr_loader.dll -> wineopenxr, winevulkan's instance /
  GPU found under the loader's (`wineopenxr's GPU ... = winevulkan's`), `session running`, the 1 mm lead quad, no
  xrEndFrame error in 260 s; Play -> Continue -> `ctx=world`, the practice meadow; stereo frames 789.. with the HUD
  quad, the VR menu's card, the popups; the scripted head's 60 deg turn moves both eyes' views (frame 700: the cliffs
  to the side, the card at the edge). Pictures: the run's compose of the repaired dumps (frames 300 / 700 / 1100).
  The present path 0.5 ms acquire+copy a frame, like the Linux layer's.
- None of 1-4 and 6's repair runs differently on Windows (1-4 are gated on Wine or on wineopenxr being the registry's
  runtime); on Windows the untested parts are the real runtimes (SteamVR, Meta, VDXR: their Vulkan support) - the
  loader's handles go to them as they are.

### 7.3 winport-combat: groups C + F (static, 2026-11-02)

Scope: the 126 SITEs of somvr.c's CB/RX/DF/TH/BM/FB/SL/BOW blocks (78) and somvr_kills/finish/execute/front/fight/
parry/bowhit/quiver_game.c (48). Table: `mod/somvr/win/sites-combat.tsv` (winport-base's format; `wintables.py --check`:
0 errors, 113 live). Work files (git-ignored): logs/winport/combat/ (results.tsv = the verdicts with evidence, the
batch decompiles, tools/: xref.py RIP-relative xrefs of both images, gmap.py a Linux global's Windows twin by votes,
vt2.py a Windows vtable by its paired slots, bycallees.py an unpaired function's twin by its paired callees, ctx.py,
callsto.py, hasoff.py).

| | confirmed | plausible | redesign | notfound |
|---|---|---|---|---|
| sites (126) | 104 | 9 | 9 (rva known, the mod code changes first) | 4 |

Every tier-1 pair was read against the Linux decompile or its call context (constants, offsets, call shape); 4 tier-1
answers were wrong and are replaced: FI_OBJSYS_MH (the slot one too low: 0x14148cb10 holds a forwarder, the +0xe0
getter is at 0x14148cb18), FI_SELECT (0x1408c6cb0 is the pool validation that Linux inlines - its string paired it;
SelectAttackFromPool = 0x1408ccdc0), FB_SYNC_REC/KL_SYNC_END (0x140c8b9c0 is a record getter; the sync teardown is
inlined on Windows), BOW_PJ_HIT (the call is in 0x140cd5c20, not in tier 1's caller). pairs.tsv has Linux 0x23303b0 =
W 0x1407bc610 wrong: W 0x1407bc610 = Linux 0x23116f0 (sheathe), Linux 0x23303b0 = W 0x14091fa40.

**ABI differences that change our calls (every group will meet them):**
1. *A member function that returns a struct by value*: Itanium puts the hidden return pointer **before** `this`
   (rdi = out, rsi = this), MSVC **after** it (rcx = this, rdx = out). CWBTimer CreateTimer / GetObjectTimer: our
   typedefs (out, T[, h]) become (T, out[, h]) on Windows.
2. *A struct > 8 bytes by value* is passed by pointer by MSVC, in its own position; SysV puts it in xmm regs, so the
   Linux typedef could list it first. Combat_DealHit's source order is (attacker, target, LTVector pos, attack, bool
   kill) - declared in that order the typedef is right for both ABIs.
3. *Float arguments are positional* on Win64: Combat_StartAttack is (c, node, attack, target, float rate, bool flag);
   every game call passes rate = 1.0f (xmm0 on Linux, [rsp+0x20] on Windows). Our 5-int kl_start_fn puts the flag in
   the float's slot on Windows. (On Linux the same typedef leaves xmm0 as it happens to be - worth the
   (c, node, attack, target, 1.0f, 0) form there too.)
4. *Vtable slots*: the D1/D0 pair -> one scalar deleting dtor (slot -1) held in CWBTimer, CWBEngine, IEngineModel's
   stubs, the FX manager (+0x30 -> +0x28), the records interface (+0x88 -> +0x80), the destroy slot of ref-counted
   handles (+0x10 -> +0x08). It does **not** hold everywhere: MSVC orders an overload group in reverse - IEngineModel
   GetNode(name) is stub slot 16 on both (Linux 16 calls the model's +0x1f0, Windows 16 the model's +0x1f8; Windows 15 is
   the uint32 overload); CObjectSystemInterface vf_35 keeps byte offset 0x118; CPlayerObj vf_59 is +0x1d8 on Linux,
   +0x1a8 on Windows (-6). Read each slot.
5. MSVC folds identical tiny functions (one scalar deleting dtor 0x1400c2140 in many vtables) and duplicates tails
   (the model-string handler jumps to ActivateSlot 6 times, to the sheathe wrapper twice).

**Done in the code (#ifdef _WIN32, Linux unchanged):** every Combat_DealHit call goes through CB_DEALHIT_CALL (somvr.c:
the source order, so clang's msvc target passes r8 = &pos, kill on the stack - checked in its output); the kills'
KL_START_CALL passes the rate 1.0f and somvr_w_klai takes and forwards (c, node, attack, target, float rate, flag);
g_cbg's 8 globals are set at the combat start (cb_globals_win: GADDR is no constant on Windows); the CWBTimer slot
indices are OFS() (3/9/10 -> 2/8/9). So CB_DEALHIT, KL_START and KL_AI_* are live.

**Redesigns left (rva in the row, the code change in its notes):** DF_IMPACT (a plain C impact hook in DealHit's
Windows order); CB_TIMER_CREATE / CB_OBJTIMER_GET (sret order, slots 3/10 -> 2/9); CB_TIMER_RELEASE (inlined: our own
-1 and vtbl+8); CB_ATK_BYNAME (no Windows function: our own hash h*0x397+fold[c] over the table *0x1418f9078, fold
table at 0x1413bad70 byte-identical); KL_SYNC_END (inlined);
SL_SHEATH_JMP (the handler jumps to a 2-argument wrapper 0x1407bdc70(inv, bool sheathe) that finds the item itself);
FR_CAMCOPY_B (one copy call on Windows serves both paths); FR_DMG_ALL (a call, not a tail jump; read-only).
Not found: FB_SERVER (an inlined +0xa08 getter), FB_SYNC_REC (inlined), FR_REARM (a static initialiser, not
searched), BOW_TOKENS_ADD (inlined; lead: 0x140cdb3d0(res, value)).

**Hooks:** the three IPA-RA asm wrappers of the group (somvr_w_bowcamxf, somvr_w_slsheath, somvr_w_dfimpact) read
rdi/rsi/rdx/xmm; on Windows each is a plain C hook with the Windows prototype (BOW_FA_CAMXF (cam, xf) as on Linux).
The plain C hooks keep their prototypes (ffscore, ffgrant, ffcam_a, ffdmg, aimove, bs_post, bo_mat/slow/fx) except
somvr_w_klai. Wwise is linked into the exe with exported names (AK::SoundEngine::PostEvent 0x140fcc180,
RegisterGameObj, SetPosition): the sound sites could be found by export name on any build.

**Offsets** (`win/offsets-combat.tsv`, 27 rows, 15 known; wrapped in the source as OFS()/GADDR(), Linux values
unchanged): the group's offsets were drafted by winport_offsets.py over the C sections of somvr.c, the 8 F files and
the combat files without sites (stealth, strike, struggle, hitfeel, combat21, hold, bowaim): 319 values, 116 read the
same in the aligned pairs, the rest checked at the sites above; only what differs or is an address is wrapped:
FF_O_POS / STL_NPC_EYE (the server character's position +0xd2c -> +0xd34), the class descriptors NPC and
InventoryShield (named on Windows), CPlayerObj's vtable, the hash fold table, g_cbg (Combat/NPC vtables, the attack
category, the stimulus manager, the timer holder and CWBTimer's vtable), the timer slots. Unknown (GADDR = 0 on
Windows: the reads fail safely, the features that need them stay off): the schema holders AI.CharacterTypes,
CombatAttackPredicate, Combat.Sync; the strike cvars (written - leads 0x1989ed3/ed4 not trusted for a write); the
struggle vtables (built in factories); CEngineTimer's vtable (hit-stop stays off until it and the timer redesign).
The tool's two 'moved' rows for PY_STREAK +0xcf and the projectile's +0x188/+0x190 were alignment errors (read by hand:
+0xcf/+0xca both kept; the projectile pair left unwrapped, to read).

**Offsets seen moving at the sites:** AI movement-disable bits +0xe98 -> +0xea0
(and its +0xea0 -> +0xea8); the AI camera term *(+0x180)+0xd2c -> +0xd34; the execute counter's clock "now" +0x18 ->
+0x20; the HOBJECT class list +0x48/+0x38 -> +0x50/+0x40 in KL_PREP_B's walk. Same at the sites: Combat +0x4d0/+0x55c/
+0x268/+0x288/+0x27f/+0x287/+0x4a0/+0x4c0/+0xb0/+0x188/+0x55b/+0x562, PlayerCombat +0x8d8/+0x8e0, P +0x2d0/+0x2d8/
+0x2e8/+0x308/+0x318/+0x420/+0x4c4/+0x7f0, CPlayerObj +0xb94..+0xbac, the execute counter +0xc6/+0xc8/+0xca/+0xcf/
+0xd2/+0xd4, the projectile +0x1b8/+0x228, inventory +0x280/+0x30/0x38/+0x910, the timer +0x68/+0xe0.
Globals: IEngineTimer slot 0x466c718 = W 0x1419d7d90 (CWBEngine::GetTimer slot 6 -> Windows slot 5); the engine
0x466aee0 = W 0x1419d80e8; the CWBTimer vtable 0x3473cd0 = W 0x14147f4e0; the game root 0x46a9560 = W 0x1418f1228.
### 7.4 winport-world: groups B, D, G, H on the Steam-Win exe (2026-11-02)

Tables: `mod/somvr/win/sites-world.tsv` (93 rows), `win/offsets-world.tsv` (149 rows); `wintables.py --check` 0
errors, `build.sh --check` green. Every value is an RVA on the analysed Steam-Windows exe. Scope: input/pointer/menus
(somvr.c IN, RUM, LP, GL), movement/climb/step (LOCO, CL, somvr_step_game.c), nemesis/captains/brand/beasts,
practice/records/session/story, plus the SITE-less movement/world files nobody owned (map, ride, surface, jump; above,
climb, crouch and wstep read no game offsets of their own).

**Method, no Ghidra for most of it.** `scripts/winport_pairdiff.py` lines up the disassembly of a function pair
(llvm-objdump; instruction shapes with registers ignored, difflib) and lists the aligned field offsets, RIP globals
and calls of both sides; `ALL` runs it over all 14,868 pairs in ~10 min from whole-text dumps (`WP_DIS_L`/`WP_DIS_W`):
logs/winport/world/all.{tsv,disp.tsv,calls.tsv} = 112k aligned offsets, 37k aligned calls (the "new" call pairs find
unpaired callees from paired callers). `scripts/winport_vtfind.py` gives a Linux vtable's Windows vtable by slot
votes. Every site was read side by side (the short ones whole, the long ones at the hooked call); two needed one
Ghidra decompile pair (PR_SPAWN_AI, PR_ZONE_AT: logs/winport/world/g{l,w}.out).

**Sites: 83 confirmed, 9 redesign, 1 not found (83 live, 10 none).** 42 of the 93 had no tier-1 answer; all but
PRN_NAV_POS were found by hand.
- B: the CWBInput hooks (IN_TICK/VALUE/VALUE2/ISON) are CWBInput 0x1483100's slots 3/21/22/24; MSVC built vf_22 and
  vf_23 as two thunks into one body with a "scaled" flag in r8b - hooking the slots works as on Linux. IN_CMD_ON/OFF
  (no tier-1 pair) sit next to IN_CMD_ACTIVE. RUM1/2, LP_GUARD_HOVER/CLICK and LOCO_ROOTDELTA are asm-wrapper call
  hooks on Linux; on Windows each is a plain C hook of the call's arguments (the rumble's float is the 3rd argument:
  xmm2). The pointer's LP_IM_* keep their (im, player, button, pos) shape.
- B, the device layer: the Linux build reads pads through Feral's SDL controller manager (GL_PADMGR/GL_PADCOUNT);
  the Windows build imports XInputGetState/XInputSetState from XINPUT9_1_0.dll - our proxy. Our input injects at the
  command layer (CWBInput, PlayerInput), which is unchanged, so nothing moves there. **GLYPHS is not needed on
  Windows**: the Windows CBaseUI update (0x52a360) registers every record's glyph pair when the view is made, with no
  controller test (Feral's no-controller skip, the cause of the Linux bug, does not exist), and its "is this texture
  loaded" CUIView slot is a Feral addition with no Windows slot. The GL_* rows are listed (none / the found vtables)
  and the feature stays off. To check at the first boot: pad mode forced, no pad, prompts show pictures. If pad mode
  ever wants a device on Windows, the proxy's XInputGetState can report pad 0 connected with a neutral state.
- D: LOCO/CL/ST all found; ST_COL_SOLVER is a semantic change (Linux inlined BaseMovement::vf_07's Combat+0x56c test
  into the solver, Windows calls it virtually and multiplies by its vec3 - same rule). CL_SURF_FROM_RAY reads the
  surface id as (u32 +0x58) >> [0x1863dec] (16 in the exe) = Linux's u16 +0x5a. CL_CTXACT's patched slot is vtable
  0x13bed20 slot 1 with (this, a2, a3, out) in rcx/rdx/r8/r9.
- G: the brand's root-loop calls (BR_SELECT/GATE_A/GATE_B/START) are in 0x4591a0 (= Linux FUN_00a8d720, which GCC
  inlined into the root loop and MSVC did not; its only caller is the root loop 0x459e10). **BR_NODELOG is a
  redesign: Combat_LogPrintf (0xe90860, 363 callers) is an empty stub on Windows** - the node log only exists if we
  hook the stub's entry with our own formatter (the node byte is 0x1989eca).
- H: **PR_TMPL_BYNAME is a redesign**: Linux's lookup has no caller, MSVC dropped it; write it in the DLL (the 0x397
  hash with the fold table 0x13bad70 - byte-identical to Linux's - over category *0x18f8998 "AI/Templates": entries
  of 0x30 at +0x38, count +0x28, key +0x28). PRQ_* are three thunks next to each other (spawn manager 0x1984e60).
  PRN_NAV_POS (a test word's nav-marker dump) not found: the parent walk is in no Windows function.

**Findings for every group:**
- **Static vtables without a LEA**: an object initialised in .data has its vtable named by no code, so
  winport_match.py's vtable list misses it (IEngineTimer = CWBTimer 0x147f4e0, found from its CreateTimer's global).
- **No RTTI for the game's classes on Windows**: the exe's 539 `.?AV` type descriptors are all library classes
  (Havok, ...). Code that names a game object's class by its typeinfo (ride's struggle kind, practice's pr_rtti) must
  use vtable addresses there: ride does (`mt_win_class`, the five struggle vtables read by their fields), pr_rtti logs
  "" on Windows.
- **Struct-returning members take (this, result)** (as ui found): GetObjectTimer is (T rcx, out rdx, h r8) - combat's
  cb_tget_fn must swap - and BaseMovement's vec3 getter returns through rdx.
- **Slot deltas**: -1 for most (CPhysicsCharacter, PlayerMovement, CGameServices, CObjectSystemInterface, CWBInput,
  CWBTimer), **+1 for IEnginePhysics** (slot 64 -> 65; the zone query interface's vt+0x78 -> +0x80 too), -2/-3 where
  Feral added virtuals (CUIView after slot 36, CBaseUI's update 5 -> 2, CUIPDAZoneMapLayer's handler 10 -> 8), -6 in
  the NPC/character vtable (NPC 81 -> 75, CPlayerObj vt+0x340 -> +0x310).
- **Tier-1 pairs found wrong**: 0x2261c10 = 0x1408c6cb0 (str; SelectAttackFromPool is 0x1408ccdc0), 0x2751a50 =
  0x1405a7480 (Combat_LogPrintf is 0x140e90860), 0x1243730/0x1243740 = 0x14011b110/0x14011b0c0 (lone, shifted by one:
  0x14011b100 is the twin of 0x1243730), and the vslot-inferred CUIView pairs for Linux slots 28..37 (shifted by one).
- The records DB's two index rules (a record without flag bit 2: (rec - type+0x48) >> 5; with it: (rec -
  type+0x38) / 48) are the same on both, as is the categories' layout (count +0x28, entries +0x38 of 0x30).

**Offsets: 149 rows - 25 globals (19 found as RVAs), 108 field offsets, 8 slot constants, 4 vtable offsets, 4 sizes;
29 confirmed, 68 same, 42 plausible, 10 unknown.** Different (15): the profile's "using gamepad" byte 0x238 -> 0x218
(pad mode writes it), the UI rect 0x80dc8 -> 0x80dc0, CInterfaceMgr's CGameUI/CMessageBoxUI 0x2708/0x2a50 ->
0x2700/0x2a40 (and the GameUI holder 0x2710 -> 0x2708, the subtitle 0x27b0 -> 0x27a0), the slot constants above.
CGameClientShell shifts -8 between +0x78 and +0x80dc8 and is even again at +0x80ff8. Same where checked: PlayerInput,
CWBInput's records, PlayerMovement, CAnimationMgr's locks, Combat (+0x4c0/+0x4c8/+0x4d0/+0x56c..f/+0xa08), Player
(+0x1d8/+0x2f0/+0x431/+0x4c4/+0x7f0), the faction members, the resource object, the spawn manager. Unknown: Feral's
controller manager (Linux only), the node-log sink (none), the nav-marker list. Plausible = the global distribution
of aligned uses keeps the value but no aligned use was in a function of ours (the faction member's fields, CCharacter's
health parts, the struggle vtables). Source: the group's #defines are OFS()/GADDR() (Linux values unchanged), the four
static-struct globals (g_ing, g_glg, g_clg, g_stepg) are set by world_win_globals() at somvr_start on Windows.
Not wrapped (logs and test words only): practice's player and nav-marker dumps (+0x178/+0xbb0/+0x260..), beasts'
practice-target dump (+0x2f8/+0x55c), ride's struggle fields (+0xfc..+0x58c: the Windows classes read the same).

**Next:** the first Proton boots with SOMVR_INPUT=1: the pad-mode byte (+0x218), prompts without GLYPHS, the CWBInput
hooks; then LOCO/CLIMB (the IEnginePhysics holder 0x19d7da0 is one of ten registered for "IEnginePhysics.Default");
BR_NODELOG and PR_TMPL_BYNAME need DLL code (an entry hook on a stub, a 20-line lookup).

**Live on the GOG build (11-03 03:10-03:22; runs logs/winport/run-20261103-031028 / -032052; `headless_win.sh` with
WIN_ENV_FILE=scripts/headset-build26.env, the test hands, WIN_CMDS=scripts/cmds/win-climb1.cmd / win-move1.cmd):**
- **The cause of the climb's failed fling down was Windows-only**: the capsule's position slots CL_SLOT_PC_SETPOS /
  _GETPOS were decimal `#define`s (5, 9) the static pass did not wrap; on Windows they are 4 and 8 (OFS rows now).
  With them off the unpin and SOMVR_CLIMB_THROUGH (the vertical part of the glide and of the flings, set on the
  proxy) did nothing: the glide over the edge stopped at the lip after 0.4 s (15 cm out, 9 cm down: run -022457) and
  the fling down never launched (the hold had not taken him down).
- **fling31 after the fix** (`ladder wall swordlow`): 9 PASS, 2 FAIL. The ladder's glide now goes down as on Linux
  (220 cm in 1.00 s, "the hand holds the lip"), the fling down launches (553 cm/s, the hold took him 91 cm down) and is
  then held by the ladder wall's own collision ("blocked below") - the same spot fails on Linux too (115645: launched,
  "blocked above"; Linux dropped the ladder plan after it), and the hanging chop there misses as on Linux. The wall
  plan, the one Linux passes (211430), passes in full on Windows: the throw = the hang with flingdown off, **the fling
  down caught by the left hand, to rest, landed at the foot** (`fling down landed ... fell 28.6 cm, the feet 5.0 cm
  over the floor`), the sword drawn while hanging and **a hit on the orc while hanging** (cut 2).
- **jump22 `stand run sprint`**: three snap turns (30 deg each) face him along a free heading; the stick's walk moves him
  1:1 (asked 173.7 cm, the body 175.5 cm in 1 s), the sprint latch 440 cm/s (asked 440.3, body 435.3); the standing
  jump 60 cm up, landed in 0.60 s; the running jump 160 cm along the floor, the sprint jump 401 cm, both landed; the
  view's yaw moved 0.00 deg in every jump.

**The Windows suite's group G/H/B failures (build-vr32-win, 11-03; fixed in 1941f19, proven with the suite's own
boots 72 and 105 on this tree: logs/coord/t3-4712e8f0-suite-win):**
- **Captains could not spawn** (captain24, polish23, nemesis27's world captain): `kp_faction` compared CGameServices'
  vtable + 0x78 (Linux slot 15) with KP_GS_FMGR's site - on Windows the getter is slot 14 (+0x70), so no faction was
  read. Now `OFS(KP_GS_FMGR_SLOT, 15)` (14 on Windows). Boot 72: `PASS measure: the Captain came (Role_Flogger
  level 9)`, the strikes' own damage on him; boot 105: `PASS struggle: the introduction in the world` and the world
  captain met (member 17, 48 m away). Both games then exited by themselves in the fight (below).
- **The brand's "is B enabled" read** called the input mode's vt+0x18 on Windows, where the node predicate calls
  +0x10 (L 0x22719a8 / W 0x1408c237f): the next virtual was called. Now `OFS(BR_MODE_CMDON_VT, 0x18)` (0x10).
- **GLYPHS**: the Windows prompts show the pad pictures in pad mode with no pad and without the re-arm (run -031028
  shots/t30.png: "(A) Select"), so `gl_start` says `GLYPHS off: not needed on Windows` and returns - no WARN
  (the boots' WARN count 5 -> 4).
- **The games that exit by themselves** (here in both fights, in the suite's abil27, sword31, render31): `WIN CRASH
  0xc0000005 at exe+0xd11df9 reading 0xffffffffffffffff` - 0x140d11df0(rcx, rdx = -1) dereferences rdx, called from
  the FX create 0x140432b00 (at 0x140432ed7) whose caller is the game's 0x1404321d0; the DLL return address on the
  stack is somvr_w_vbglow's call of the original (dll+0x90040: VG_FXCREATE's entry hook). For winport-hooks/combat.
- **pcar29 "no mount offer"** (2 of 3 chains): not this group's offsets. In the failing chains the warg's knockdown
  ('KnockDown' after the countered leap, as in the passing one) ended without its idle phase ('AI_Warg_Knockdown_Idle_C'
  never came; the warg stood up and attacked 11 s later), and the game offers the mount only in that idle (the third
  chain: idle 4.7 s after the knockdown, the offer 0.6 s later; Linux 5.3 s / 0.7 s). The beasts' asks were lifted
  through both failing knockdowns. The struggle vtables (matched by their fields) come into play only after the mount
  - the third chain mounted, seated and got off by B.

**The nav markers on Windows (11-03 evening; 5ccd092):** PRN_NAV_POS is the same Linux function as winport-ui's OB_POS
(0xebc700; Linux has an iterative and a recursive copy, Windows only the recursive one): Windows 0x1404e1cf0 (marker,
float out[3]) -> 1. The list it walks: the sentinel PRN_LIST 0x46b9200 = RVA 0x17fbda0 (the marker walk FUN_00ec4f10
= 0x1404e6bf0: `mov rbx,[0x1417fbda8]; lea r12,[0x1417fbda0]; cmp`, the walk winport-ui's OB_EACH hooks); the node -> marker
-0x80, the record +0x298, active/enabled +0x2e0/+0x2e1 unchanged. Everything that walks the markers now runs on
Windows: the herbs' exact place (somvr_hands31_game.c), the practice's navdump, abil2's props.
- Boot (final.sh boot 133, H31_STEPS=herb, run boot-check-20261103-221425): `CONFIG herbs ... the herbs' places by
  PRN_NAV_POS`; `HERB cue: 'Collectible_Flower_O' at 31314 2190 -25167` - the same spot the Linux run 152929 found
  - the left hand put onto it `in reach` (10 cm), `taken`, `eaten` (2 cm, 0.25 s), the game's pick: **Talion's
  health 60.0 -> 120.0**, the plant picked; 2 WARN (the late H9/eye images at start, as every Windows boot).
  The same again with the dumps kept (run -222305): in reach at 11-15 cm, taken; the game's eye image at the reach
  (frame 711, left eye, looking down) shows the flower at the view's lower edge; the cue's own layer was not composed
  over it (wound down) - the glow's place is the cue line's point (15 cm over the marker), as on Linux.
- pcar29 again (boot 106, run -221611): the leap became the caragor's takedown struggle this time (Talion's
  `'19CStrugglePressCount' (vtable 0x413cded0)` - the struggle classes by their Windows vtables, live), the later
  counter gave no knockdown idle: no mount offer. Across the Windows runs 1 of 5 chains got the offer (Linux and the
  02:33 Windows suite 1 of 1); no offset of the offer path is known to differ - left open.

### 7.6 winport-ui: group I on the Steam-Win exe, and the offset-diff tool (2026-11-02)

Tables: `mod/somvr/win/sites-ui.tsv` (39 rows), `win/offsets-ui.tsv` (58 rows); `wintables.py --check` 0 errors.

**The tool, `scripts/winport_offsets.py`** (for every helper; no Ghidra of its own, batches go through
`ghidra_query.sh`): `prep OUT files.c` scans a mod file (its `#define` hex values, the `+ 0x..` literals in code,
the SITE lines, the functions its comments name) and writes the decompile batches, skipping what any
`logs/winport/*/{lin,win}*.out` already holds; `diff OUT --group G` lines the paired decompiles up (normalised tokens,
calls through pairs.tsv + the helpers' sites-*.tsv, lines then tokens, access types checked) and drafts
offsets-G.tsv rows (same / plausible = moved / unknown, with the aligned instruction of both sides); `pair`, `verify`
(one line per pair: aligned share, offsets same/moved, paired calls), `show` (any cached decompile), `bytes` (exe
bytes + RVA). It drafts, it does not decide: here it called GO_SND_GET "same" (it aligned another +0x68) and the
player's +0xbb0 "moved" (MSVC reordered the loads) - both wrong. Read every row before `confirmed`.

**Sites: 29 confirmed, 7 plausible, 3 redesign, 0 not found.** Plausible: CO_TSCORE_1..6 (logged calls, paired by
their order in the same caller), CO_TXT_ICON (the right call, a much shorter target). Findings that matter beyond I:
- **MSVC's TypedDelegate vtables have 3 slots** (dtor, invoke, -) and no RTTI: the delegate's invoke is slot 1 (Linux
  2); several delegates of one class sit back to back in the Linux order, each LEA'd by its dtor and the class's
  register/unregister. The invoke is often a thunk `mov rcx,[rcx+0x18]; jmp member` (GO_HINT, GO_TUTORIAL): hooking
  the slot keeps the Linux (dlg, sender, params) shape. 7 sites (PP_*, GO_HINT/TUTORIAL/PROMPT, XH_SET).
- **Slot -1 is not a rule**: the object system's vtable is unchanged at +0xb0 but -1 at +0xf8 (-> +0xf0);
  CUIHierarchyLayer goes 2 -> 1, 10 -> 8, 24..28 -> 19..23. Read each slot from a paired call or a paired slot function.
- **A member function returning a struct by value takes (this, hidden result, args) on MSVC, (hidden result, this,
  args) on GCC**: MM_CAMQ's quaternion product is called (a, out, b) on Windows; the hook's order is `#ifdef _WIN32`
  in somvr_minimap_game.c. Check every hooked or called member that returns a struct.
- **Tier-1 pairs found wrong (4)**: CO_LAT_SEE's string pair (MSVC outlined the property-name getters, the unique
  string landed in a 2-line getter), 0x2319520 <-> 0x140c92560 (that is CO_COLLECT; AddItem = 0x1407c4d60),
  0xa498c0 <-> 0x1403f15b0 (a quat->matrix; the product is 0x1403f1f10), 0xa36e70 <-> 0x1403f1f10. A pair from
  `str` or `lone` on a tiny function needs a look.
- **Inlined (redesign)**: OB_RANGE (in the marker update: rec = *(m+0x298); 0x1404e0bf0(rec) & DAT_1418f4308 picks
  0x1404e0b30 or 0x1404e0c90 (out*, rec) - call those three on Windows); VG_FXC/VG_FXC1 (the FX wrappers inlined into
  66 call sites of 0x140432b00, which starts with the wrapper's gate byte DAT_1417fab60: one entry hook there covers
  both; it then also sees gated-off creates - test the result). CP_FX stays a call patch: it calls 0x140432b00 with the
  same 6 arguments. OB_POS: Linux has an iterative and a recursive copy, Windows only the recursive 0x1404e1cf0 (same
  result); GO_PAUSETUT's page test is out of line (0x140466d30(record) -> page).

**Offsets: 9 globals (8 found, RVAs), 49 offsets (30 same, 6 differ, 7 plausible, 6 unknown).** Different: GameOptions
-0x20 (music 0x190 -> 0x170, rotate-minimap 0x203 -> 0x1e3), the sound holder's getter +0x68 -> +0x60 and SetRTPC
+0xb0 -> +0xa8, the nav clip's show +0x208 -> +0x200, CUIHierarchyLayer's vf_10 at +0x40 (Linux +0x50). Same: the
LookAtTrigger, look-at list, targeting candidate, camera (+0x44c/+0x458/+0x484, written), InventoryItemUI, nav marker,
records DB (entries +8, values +0x10, kind +0xa, set +0xc, slot +0xe), the schemas' fields. Unknown: the vision ramp
schema (GO_VSCHEMA: Windows fills it field by field in static initialisers; 0 on Windows = SOMVR_VISION_RAMP_MS off)
and its record offset, and CPlayerUI's state machine / menu-manager fields (SCR_UI_SM, SCR_SM_MGR, SCR_MGR_*: no paired
function yet; they keep the Linux values, unchecked - SOMVR_POINTER_MOUSE1's screen test reads them).
Not in the table (inline, same by the paired code): the delegates' params, the records DB layout, the look-at list's
vector (+0x10/+0x18; its stride 0x20/+0x18 not read), obj+0x48/+0x38 class chains. Not checked: co_vision's
p+0x10 -> +0x118.
### 7.2 winport-core: group A + the 26 COM slots (2026-11-02, branch t3/som-winport-core-research)

Tables: `mod/somvr/win/sites-core.tsv` (63 rows) and `offsets-core.tsv` (53 rows), all RVAs on the Steam-Windows
exe, checked by `win/wintables.py --check` (0 errors). Method: every pair read side by side in llvm-objdump
(`scripts/winport_xref.py`: shared field offsets, vcall slots, mapped calls; no Ghidra needed for any row), vtables
found from their slot functions' content, ctors and relocated pointers (static objects such as IEngineModel and
CWBEngine have no LEA, only a .data pointer to the vtable - winport_match.py's vtable finder misses them).

| | count | sites |
|---|---|---|
| live, confirmed | 29 | H1 H2 H3 H4 H6 H8, AUDIO PM, ARM_TRK_EVAL, G12_NODEXF/HIDEPIECE/NUMPIECES/MODELOF, ARM_MODELOF, HEAD_MODEL_VT/MASK/RENDERABLE, HEAD_RENDERABLE_VT, HEAD_HANDLE_SETMASK, HEAD_NODE_SETMASK, RES_RECT_LETTERBOX(_INIT), HEAD_SHADOW_PRE/LIGHTS/CASCADES/LIGHTS2, HUD_PASS_EYE, UI_PRESENT_COMPOSITE, ES_RUNSTRING |
| com | 26 | H9 (IDXGISwapChain:8), D3D_* (5), the 20 render DC_* (ID3D11DeviceContext, the same indices as Feral's slots) |
| none: redesign | 8 | RES_3D_DESC (detour), HUD_PASS_COVER (merged), VK_TEX_COPY(_MT) (DXVK interop), G219-G222 (save guard not needed) |

Semantic changes (each also in the row's notes):
- **Win64 argument order**: hooks become plain C functions in declaration order; floats keep their position (H2
  `(this, xf, fov, rect, nz, fz)`, H3 `(ds, xf, tan, center, prect, type, nz, fz)`, H6 `(corners, xf, tan, nz, fz,
  ortho)`, HEAD_SHADOW_CASCADES `(ctx, view, float, float)`): the Linux register pass-through (win_fn) and the
  floats-first typedefs cannot be reused. None of group A's hooked members returns a struct (lessons.md 4).
- **H3** has a second Windows call to the same target (RVA 0xd74db8, the fixed-tanHalf override branch Linux
  inlined into one call): it needs the same hook (a Windows-only SITE).
- **H9 under DXVK**: vkQueuePresentKHR runs on DXVK's submit thread, not on the thread that called Present, so the
  present_tid bracket cannot pair tags; use a FIFO (one push per Present without DXGI_PRESENT_TEST, one pop per layer
  present). The game imports d3d11!D3D11CreateDevice (IAT RVA 0x13ada90) and dxgi!CreateDXGIFactory (0x13adab8): the
  swap chain comes from IDXGIFactory::CreateSwapChain (slot 10).
- **HUD_PASS_EYE/COVER** are one call on Windows (RVA 0x50c960, tail-merged; rdx = the view or NULL): one hook,
  the cover pass = rdx NULL.
- **RES_3D_DESC**: the 3D (+0xd8) and UI (+0x90) swap-chain-like descs are filled inline in the mode set
  0x140d4d870; only the back buffer keeps the call. Detour 13 bytes at RVA 0xd4dc69, save every volatile register,
  call res3d(holder + 0xd8), back at 0xd4dc76 (needs base's detour primitive).
- **VK_TEX_***: no CVulkanTexture; the VkImage + layout from IDXGIVkInteropSurface::GetVulkanImageInfo (DXVK).
- **G219-222**: no Steam remote storage without the Steam client; our own Proton prefix keeps the saves apart.
- Not the -1 rule: IEngineModel Linux 3/6/7 -> 2/5/6 but 23 -> 23; CSimpleModel 50 -> 50 and 162 -> 162 (its
  CSimpleModel vcalls shift +8 past 0x208); the small classes are -1 (Renderable 2 -> 1, Handle 53 -> 52, Node 5 -> 4,
  CWBEngine 32 -> 31).
- Offsets that differ: NODE_MASK 0x130 -> 0x148 (CRenderNodeMeshSkeletal +0x18; also its +0xc8 -> +0xe0, the
  handle's +0x118 -> +0x130), the renderable's dirty/mask copy +4 (not read by the mod). Same: player/PlayerMovement/
  camera/holder/DeviceState/RenderTarget fields, SM_*, the scene params' +0x55/+0x58/+0x60/+0x114. Every global is a
  new RVA (offsets-core.tsv); `somvr_core_win_globals()` assigns them on Windows (GADDR is no constant there: the
  static initialisers keep the Linux values) and `somvr_core_win_com()` takes H8's vtable checks from the COM rows -
  base's start must call both. MGR_CHAR not found: player_objs uses bodyObj on Windows.
- **Wrong tier-1 pairs** in pairs.tsv (cost here, fixed in the tables): 0x8f3c20 -> 0x140e54b20 (lone; really
  0x140e0a620), 0x7dc0b0 -> 0x140db1c60 (str "k_vDiffuseColor"; really 0x140db4650), 0x7cdeb0 -> 0x140db4650 (lone),
  0x7f3f10 -> 0x140da7a20 (really Linux 0x7f6250). Every `lone` pair needs a side-by-side look.
- ARM_TRK_EVAL: CSimpleModelTrackerNode (unmatched code) found by a slot's bytes - Linux slot 8 `mov byte
  [rdi+0x810],1; mov byte [rdi+0x815],6` is Windows slot 7 at +0x8d0/+0x8d5 of vtable 0x141402bf0; the eval is
  slot 2 (Linux 3). The tracker grew +0xc0 past 0x340: TRK_MDATA 0x808 -> 0x8c8; TRK_POSE, the pose nodes, the
  skeleton pointer unchanged.

`scripts/winport_xref.py` (new, read-only): `cmp L W` (shared field offsets / vcall slots / mapped calls from
llvm-objdump), `cands L` (call-graph candidates), `dis`, `vt` / `vtsize` (Windows vtables incl. static objects via the
relocations), `lvt`. A pickle of the indexes in $TMPDIR (first run ~5 s).
### 7.5 winport-render: group E + the fog edit in DXBC (2026-11-02, branch t3/som-winport-render-research)

Static, no game slot used. Files: `mod/somvr/win/sites-render.tsv` (72 rows), `win/offsets-render.tsv` (33 rows),
`win/somvr_fogdxbc.h` + `win/tests/test_fogdxbc.c` (in `build.sh --check`), `scripts/winport_dxbc.py`; the
`OFS()/GADDR()` wrappers in somvr_render/headparts/body/perf_game.c (Linux unchanged). Work files (decompiles, notes,
the extracted fog shader): logs/winport/render/.

**Counts.** Sites: 50 live (37 confirmed side by side, 13 plausible; 18 calls, 17 entries, 15 vtable slots), 20 COM
rows (the `fx d3d` capture's IdxD3D11DeviceContextThreaded slots = ID3D11DeviceContext's own indices), 2 rows with no
Windows site (RD_OVERLAY and RD_BRT_B: redesigns). Offsets/globals: 33 rows - 11 confirmed (6 globals, 5 changed
render-node fields), 12 checked unchanged, 5 plausible, 5 unknown (below).
`wintables.py --check`: 0 errors.

**What the player sees if a part is missing** (so the first Proton builds can ship without the hardest):
1. Head parts (SOMVR_HIDE_HEAD=parts, 29 sites): the inside of Talion's head, hood and hair in the eyes; with the cloth
   group (its own fail-soft group) the white fur mantle at the shoulders. 29/29 live.
2. The body (SOMVR_BODY=hands, 4 sites): the whole body and arms instead of the tracked hands. 4/4 live.
3. The fog (SOMVR_XR_FOG=radial): the wall of fog that turns with the head comes back. Done in DXBC (below).
4. FLATFX=all: blood splashes and the low-health red in the face (RD_OVERLAY_IF, live), lens flares (live), sun
   shafts (live), lens rain (live). The game's vignette / box blur part is a redesign: its two bytes are H1's
   caller-stack slots (`RD_SP_*`), and H1 on Windows is a plain C hook without the caller's sp.
5. FX_BILLBOARD=eye / FX_NEAR (RD_PSYS, RD_SPRITE, RD_FLARESPR, live): the sun's star rolls with the head, the mist
   cards swing; needs the proxy fixed for Windows (below).
6. KTIME=frame (RD_KT_UPLOAD, RD_KT_LATE, 4 globals, live): the fire and the wraith sky animate differently per eye.
7. Test-only (never in a player's build): the 20 COM rows, RD_GPUPSYS, RD_PSYS_UPD/PREP, RD_BRT_A/B, RD_FOG,
   RD_CF_MAKE (SOMVR_CURFRAME, off everywhere), SOMVR_SHADOW_SHARE (perf: `SSH_P` unknown = never shared, both eyes
   draw their shadows - correct, a little slower).

**Findings that change the code or the method.**
- **The ten ClientFX renderers are 8-byte .data singletons** (Linux 0x4604db0.., alphabetical; Windows rva 0x188fe20
  Sprite, 0x188feb0 FlareSprite, 0x188feb8 Tracer, 0x188fec0 LensFlare, 0x188fee0 PolyString, 0x188fee8 Volumetric,
  0x188fef0 ParticleSystem, 0x188ff10 Lightning, 0x188ffb0 PolyTrail, 0x188ffb8 OcclusionQuery): no LEA names their
  vtables, which is why tier 1 found none. Their 7-slot vtables: [0] the ICF-shared scalar deleting dtor, then Linux
  slot n at n-1; the draw is slot 3. Found by the .data scan + the slot pattern, confirmed by decompiles (lens flare
  59/61 offsets, flare sprite read line by line).
- **The particle draws' render context (a[4]) is one slot lower**: map 8, unmap 9, draw 14 (Linux 9/10/15; read in
  0x140ee0e80). SOMVR_FX_NEAR's and FX_BILLBOARD's proxy (`g_pxn_vt`, `g_px_vt`, `g_pxc_vt`, the naked forwarders
  `somvr_rd_pxN`, `somvr_rd_pxu10`) must use those slots and the Win64 registers (`mov rcx,[rcx+8]; mov rax,[rcx];
  jmp [rax+8k]`) - Windows-only code; the positional initialisers cannot take OFS().
- **Itanium-appended overriders have no MSVC slot.** COverlayFX's vf_18 (Linux main-vtable slot 18) is the
  IClientFXOverlay draw appended to the primary vtable; on Windows only the interface vtable (0x141491ab0, at +0xb0)
  has it, slot 2, with FUN_02d7a700 inlined. One hook covers every call: `rd_start` must accept RD_OVERLAY_IF alone
  under `_WIN32`. The same pattern: CParticleSystemFX's and CSimpleModelRenderable's last Linux slots live in their
  second vtables on Windows (BD_REND_COPY's listener is Windows slot 1 of the primary one).
- **Slot rules vary per class** (as winport-ui saw): CRendererHandle has no destructor pair (slots 1:1), CSimpleModel,
  the ClientFX renderers, CSimpleModelClothNode and CSimpleModelRenderable are -1; the deleting destructors map to
  Windows slot 0.
- **A wrong tier-1 pair**: pairs.tsv's 0x2357e60 (CSimpleModel::vf_20) -> 0x140909640 (nbr) is wrong; the right one is
  0x1409099f0 (slot 19; the same `slot < 20`, part[slot] at +0xb8).
- **The head parts' per-pass code is factored differently.** Windows builders call per-node dispatchers; the skeletal
  draw FUN_0081ff80 = FUN_140dd2b90 is called only by the deep/forward/late dispatchers (HP_DRAW_DEEP/FWD/LATE = those
  three calls, node in rdx), and inlined into the G-buffer and velocity dispatchers (HP_DRAW_GBUF/VEL = the builders'
  calls of the dispatchers: skipping one skips the node - the hook must pass non-skeletal nodes through,
  node+0xc0 == 1). The custom draw FUN_007ccec0 = FUN_140d72230 keeps its arguments (dl, custom+8, node, pass,
  flag); the deep and forward passes share one pass-6 wrapper (HP_CUST_DEEP/FWD = its two calls). The render node grew:
  kind 0xb0 -> 0xc0, category 0xb1 -> 0xc1, position 0x4c -> 0x60, mesh 0xc8 -> 0xe0, overrides 0x380 -> 0x398, the
  custom node's flag/renderer +0xe1/+0x100 -> +0x101/+0x120; SK_POSE 0xf0 -> 0x108 and CSkeletalTransforms' vtable
  0x1473a20 are plausible (checked at run time, fail-safe).
- **Moved code**: the body's palette copy is out of the listener in FUN_1408fd5f0 (the source = param 4: SysV rcx,
  Win64 r9 - a C hook with the same prototype gets it); the custom handle's node is built in the handle's ctor
  FUN_140d40b30 (HP_CUST_INLINE); BeginRenderTarget's two Linux functions are one on Windows (RD_BRT_A = B).
- **ICF**: RD_KT_LATE's call target 0x1406de2a0 is the empty function folded for hundreds of callers - verify the call,
  never hook or patch the target.
- Globals: k_fTime's shader-constant object 0x199e300 (data +0x40, handle +8, dirty +0xa, pointer +0x10), the shaft
  byte 0x19a1b41, the visibility buffer 0x198f548; the D3D11 device / immediate context pointers are DAT_14198ef80 /
  DAT_14198ef88 (rva 0x198ef80/0x198ef88; for base's COM install).
- CRendererHandle's custom-handle create is called as [vt+0x20] on Windows (FUN_140ef50a0, HP_CLOTH_HANDLE; Linux
  +0x28): its vtable starts at 0x14146fe30, the slot is Linux - 1 like the others.
- Unknown: RD_SP_BOXBLUR/VIGNETTE (redesign above), RD_CF_OFF (CURFRAME only), NODE_BOX (logs only), SSH_P (perf; the
  cascade pass FUN_007f6250 is 22 KB, its Windows image not pinned).

**The fog edit in DXBC** (re-winport.md 3.1's decision; the SPIR-V edit it replaces: re-xr-pipeline.md 137.3).
- The shaders are not in the exe (its 927 embedded DXBC blobs are UI/debug ones) but in the LTAR archives' zlib streams:
  `scripts/winport_dxbc.py scan` found both fog overlay shaders in global.arch05: the opaque one (stream 0x4c054, 8168 bytes, checksum
  d7d9be1a647264a1c3242705b1a07fa5): its program is the SPIR-V decode line for line (instructions 13-74 = fog_a.dec):
  `eq r0.y, r3.x, l(1e7)` (36), `movc r0.x, r0.y, r0.x, r3.x` (38), r3 = (z, x*z, y*z) from `mul r3.yz, r0.yywy,
  r3.xxxx` (35). The OIT twin is in the same archive (9048 bytes, checksum 14fcc056850deb276a8ff5048b0f40d1): it crosses
  one of the archive's 64 KB zlib chunks, so the scan reads the chunks as a rolling window (17 s for global.arch05).
- `somvr_fogdxbc()` matches by structure, not by hash (the same code for the GOG exe's data): l(6.371e8) present; an
  `eq rC.c, rN.x, l(1e7)` whose rN.yz were last written by `mul rN.yz, ?, rN.xxxx`; the first later movc with that
  condition component, a one-component destination and src2 = rN.x; nothing in between writes rN.x or the condition.
  The edit: `dp3 rT.x, rN.xyzx, rN.xyzx; sqrt rT.x, rT.x` before the movc, its src2 -> rT.x, dcl_temps + 1; the
  container rebuilt (program length, chunk size, later chunk offsets, total size) and the checksum recomputed (DXBC's
  MD5 variant; the C and the Python versions agree with all 927 exe blobs). On the real shader the result differs from
  the original in exactly dcl_temps 6 -> 7, the two new instructions and the movc's src2 (scripts/winport_dxbc.py dis).
  The OIT twin: `eq r3.w, r6.x, l(1e7)` (63), the 2-component `movc r4.zw, ...` skipped by the rule, `movc r5.x, r3.w,
  r1.y, r6.x` (65) -> dcl_temps 10 -> 11, `dp3 r10.x, r6.xyzx, r6.xyzx; sqrt r10.x, r10.x`, src2 r10.x; nothing else
  differs. test_fogdxbc runs both when given the files (they are game data: not in the repository).
- The DLL side (not written: base's COM install): hook ID3D11Device::CreatePixelShader (COM slot 15) when
  SOMVR_XR_FOG=radial; call somvr_fogdxbc on each blob; a result = create from it (log `fog: shader <checksum> created
  with the fog by the distance`), NULL = the game's blob untouched (log the `why` only for blobs holding l(6.371e8)).
  Native Windows uses the same edit (the checksum is valid for the real runtime).

**Next.** At the first Proton boots: the overlay's IF-only path (written: `#ifdef _WIN32` in rd_start), the proxy's Windows forwarders and slots, the hook
semantics of HP_DRAW_GBUF/VEL (kind 1 only), the vignette redesign (with group A's H1), SSH_P; the fog hook logs which blobs it changed (the OIT one).

### 7.7 winport-stereo: the camera/stereo/HUD hooks on Win64 (2026-11-02, branch t3/som-winport-stereo)

Static + the stand-in under Proton's wine; no game slot used. Code: somvr.c (the hooks, ex_install, install_all,
the late COM path), somvr_hands31_game.c (ov_pm), win/somvr_win_sites.c (g_sysv), win/somvr_win_main.c (the hook
test), win/somvr_win_d3d.c (`somvr_win_tex_vkimage`); tables `win/sites-stereo.tsv` (2 rows), `win/offsets-stereo.tsv`
(1 row). Every Windows argument order below was read in the Windows caller's disassembly (llvm-objdump), not taken
from the notes alone.

**Ported (21 names out of g_sysv; live on Windows when their rows are):** H1, H2, H3 (+ H3_OVR), H4, H6, H8, AUDIO,
PM (and the climb's `cl_pm`, the vault's `ov_pm`), RUM1, RUM2, RES_3D_DESC (as RES_3D_DESC_MID), RES_RECT_LETTERBOX,
RES_RECT_LETTERBOX_INIT, HEAD_SHADOW_PRE / LIGHTS / CASCADES / LIGHTS2, HUD_PASS_EYE, HUD_PASS_COVER (merged),
UI_PRESENT_COMPOSITE. **Still blank: none of this group.** The hooks keep the WRAPPER on Windows (one install path;
the C side reads the Win64 registers), the Linux path is the same code through RA1-RA4 (= rdi rsi rdx rcx there).

| hook | Win64 shape (the caller read) | the C side on Windows |
|---|---|---|
| H1 | (cam rcx, SomView* rdx = rsp+0x40, sp r8 = rbp+0x310) at 0x50c449 | RA1-RA3; h1_fn unchanged |
| H2 | (this, xf, fov, rect, nz [5], fz [6]) at 0x14050c824: [rsp+0x20] = near (view+0x24), [rsp+0x28] = far (+0x28) | RF_STK(5/6); `H2_CALL` in that order |
| H3, H3_OVR | (ds, xf, tan_half, center, prect [5], type [6], nz [7], fz [8]); 0xd750f3 type 0, 0xd74db8 type 1 | RA_STK / RF_STK(7/8); `H3_CALL`; H3_OVR patched with H3's stub (type 1 passes through, as on Linux) |
| H4 | (gm, rt, origin, player, end_flags [5] = 2, first [6] byte) at 0x50d25f | RA_STK(5/6) |
| H6 | (corners, xf, tan_half, nz xmm3, fz [5], ortho [6] byte) at 0xd997b5 | xmm[3], RF_STK(5); `H6_CALL` |
| H8 | the executor's `lea rcx,[ds]; jmp` (0xd511a7), flags edx | RA1/RA2 |
| AUDIO | (this, pos, front, top, idx [5]) | RA_STK(5) |
| PM | (pm rcx, xf rdx) | RA1/RA2 (also cl_pm, ov_pm) |
| RUM1/2 | (pi rcx, ch edx, v xmm2) at 0x45c5ac / 0x45bc80 | xmm[2] (the C call puts v in xmm2 itself) |
| RES_RECT_LETTERBOX(_INIT) | (rect, letterbox dl, ui r8b) | RA1-RA3 |
| HEAD_SHADOW_PRE/LIGHTS/LIGHTS2 | (ctx, view[, r8, r9, [5]]): integers only | passed on as 5 integers (`win_fn`, Windows typedef) |
| HEAD_SHADOW_CASCADES | (ctx, view, float xmm2, float xmm3) | `win_casc_fn` (ctx, view, f, f) |
| HUD_PASS_EYE/COVER | one call 0x50c960 (hud = [root+0x80ff8] rcx, player rdx, 1, 1): the cover path jumps in with `xor edx,edx` (0x14050c74c) | `somvr_hudeye_c`: RA2 != 0 -> EX_HUD_EYE, 0 -> EX_HUD_COVER; ex_install marks COVER installed with EYE |
| UI_PRESENT_COMPOSITE | (holder rcx) at 0xd4d4ba | RA1 |
| RES_3D_DESC | no call: the 3D desc filled inline in 0x140d4d870 | `somvr_res3d_mid` at RES_3D_DESC_MID (below) |

**Findings / changes beyond the argument order.**
- **RES_3D_DESC_MID** (RVA 0xd4dc69, a K_ENTRY row: its bytes checked, then `somvr_win_hook_mid`): the detour sits on
  `mov [rdi+0xdc],eax` (6 bytes, MinHook moves it whole). There: the width is already in the desc (+0xd8, ScreenWidth
  stored at 0xd4dc17), the height in eax (cvttss2si at 0xd4dc64), the windowed flag in cl, the format (+0xe8 = 0x1c)
  set. The C side writes w into the desc and hh into eax (stored by the relocated instruction, rax reloaded right
  after). The Linux hook and the detour share `res3d_pick` / `res3d_done` (the size, the state es_tick reads, the RES
  line).
- **H2's player-view return address** `g_ra_vp` was the Linux code address 0x9ef2bc on Windows too (never equal: H2
  would only match the eye views by their hash). Now `GADDR(CORE_RA_VP)`: RVA 0x50c82b, the return of `call
  [r10+0x120]` at 0x14050c824 (= Linux 0x9ef2b5, slot 37 -> 36, same SomView layout); set in somvr_core_win_globals.
- **The vignette / box blur (render's RD_SP_* "redesign") needs none**: `sp` is H1's third argument on both builds
  (Linux rdx = rsp+0xb0, Windows r8 = rbp+0x310), never the caller's stack pointer, so rd_h1(sp) works as it is. The
  offsets are the same: the Windows DoF consumer 0x140dad640 (= L 0x7eb1d0, the strings k_fBoxBlurParams /
  k_fVignetteParams) tests `byte [r12+0x130]` and `byte [r12+0x144]` and reads the vignette colour bytes +0x154..0x156
  of the same scene-param block (Linux: ctx+0x1c8 / +0x1dc = that block + 0x98). render's rows (windows `?` = the Linux
  value) are therefore right; they may become `same`.
- **ex_install's stub page** used the Linux low-address mmap probes only: on Windows (the exe at 0x140000000+) none is
  within +-2 GB, so every EX hook would have been "no stub page". It now takes `somvr_alloc_near` there (as install_all's
  stub_page). The same Linux-only probe loop is in 7 more stub pages - ptr_install (LP_GUARD_*), bo_install (BOW_FA_CAMXF),
  lo_install (LOCO_ROOTDELTA), sl_install (SL_SHEATH_JMP), df_install (DF_IMPACT), hp_install_group (HP_*),
  scr_install (SCR_NEM_PAD): exactly the other g_sysv hooks; each one's port needs the same few lines.
- **The late COM path** (somvr_win_late_h9, called by base at the first Present after the COM fill): the D3D sites
  are checked again there (`win_late_d3d`: at start they are blank, so `g_d3d_ok` stayed 0 and H8's layout-B copy and
  the HUD copy were off for the whole session), then H9, then SOMVR_FX_TEST's 20 DC_* slots (`dc_install`, test only).
- **The eye / HUD / hands-depth VkImages**: `tex_vkimage` on Windows asks DXVK's `IDXGIVkInteropSurface`
  (`somvr_win_tex_vkimage`, base's vk_image_of). Before, it read Feral's +0x80/+0x108 with `vt_vktex` 0 - a texture
  whose +0x80 pointed at memory starting with a zero qword would have handed the layer a garbage VkImage. **Open for
  the layer**: under DXVK the image's layout is DXVK's (GetVulkanImageInfo returns it); the layer's copy assumes Feral's.

**Test.** The stand-in's hook test (`SOMVR_WIN_TEST=1`, `scripts/headless_win.sh MOD --test xinput`, run in a reflink
copy of base's prefix: WIN_PREFIX) calls `somvr_win_abi_test`: a WRAPPER called in each hook's Win64 shape (H2, H3, H4,
H6, RUM, AUDIO, SHADOW, CASCADES), its C side reading the arguments with the hooks' own macros and calling a stand-in
original through the hooks' typedefs: `HOOKTEST wrapper-abi rc=0 12 checks ok` (run-20261102-223251). Linux
build.sh --check: 174/174, wintables 0 errors (313 live, stereo 2 live, 1 offset).

**Next.** The first stereo boot (base): the HOOK lines of all 21 (+ H3_OVR, RES_3D_DESC_MID), H2's mods with the
eye rects (the ra check), RES lines at the mode set, `D3D ... (late, COM)` ok, EYES with interop VkImages; then the
layer's copy layout under DXVK.

### 7.8 winport-hooks: the remaining register hooks and the Windows-only redesigns (2026-11-02, branch t3/4eb13695)

Static, no boot (base's harness not yet open to others). Every change is `#ifdef _WIN32` or semantically identical on
Linux; `build.sh --check` and `win/build.sh --check` green. Tables: `win/sites-hooks.tsv` (5 Windows-only sites),
`win/offsets-hooks.tsv` (11 rows); rows of other groups changed in place (confidence + a "PORTED/DONE (winport-hooks,
7.8)" note): combat SL_SHEATH_JMP, DF_IMPACT, CB_TIMER_CREATE, CB_OBJTIMER_GET -> confirmed, CB_VT_ETIMER found;
CB_ATK_BYNAME, CB_TIMER_RELEASE, PR_TMPL_BYNAME, BR_NODELOG, OB_RANGE, VG_FXC/VG_FXC1 stay `none` with "DONE".

**A finding for every Windows hook: the stub pages.** 13 install paths (bo_patch_call - 31 call patches in 10 files -,
ptr_install, bo_install, lo_install, sl_install, df_install, ex_install, the head parts' group, hudfx, minimap,
screens, session, vbglow) searched their own stub page at low Linux addresses (0x3f0000 down, then 0x40000000..
0x70000000): never within +-2 GB of an image at 0x140000000, so every one of those hooks would have been SKIPPED "no
stub page" on Windows. One helper now, `stub_alloc(lo, hi, top)`: Linux the same probes, Windows `somvr_alloc_near`.

**Ported register hooks (removed from g_sysv; RA1..RA7 in the new `somvr_regs.h`, RA5-7 = entry rsp + 0x28/0x30/0x38):**
- LP_GUARD_HOVER/CLICK, SCR_NEM_PAD: `FUN_00a38e80(P)` = RA1 (rcx). BOW_FA_CAMXF (cam, xf) and LOCO_ROOTDELTA (body,
  delta): RA1/RA2 (rcx/rdx); neither returns a struct (the out pointers are explicit on both ABIs).
- SL_SHEATH_JMP: the handler's dl=1 tail jump goes to the 2-argument wrapper 0x1407bdc70(inv rcx, bool dl) (`mov
  eax,[rcx+0x280]; cmp 8; rdx = [rax*0x38 + rcx + 0x30]; jmp 0x1407bc610`): the hook reads the item the same way for its
  test and calls the original with its two arguments.
- DF_IMPACT: (attacker rcx, target rdx, const LTVector *pos r8, attack r9, bool kill = RA5's low byte). Every original
  DealHit call (the impact passed on, a held blow's release, the late recoil) goes through DF_ORIG (the Windows order).
- HP_DRAW_DEEP/FWD/LATE: FUN_140dd2b90(ctx, node rdx, r8, r9, 2 stack) - node = RA2 on both ABIs. HP_DRAW_GBUF/VEL call
  the per-node dispatchers (7 arguments: ctx, node, r8, r9, [rsp+0x20..0x30]; the skeletal draw inlined): a node whose
  kind (NODE_KIND, +0xc0) is not 1 passes through untouched, the original gets all 7 (one more than the skeletal draw
  reads: harmless). HP_CUST_*: FUN_140d72230(dl, obj, node r8, pass r9, flag [rsp+0x20]) - node = RA3 on both, flag RA5.

**The redesigns built:**
- **Hit-stop timers** (combat 7.3). CreateTimer/GetObjectTimer return an HTIMER by value: Windows (T, out[, h])
  (CB_TCREATE/CB_TGET). **CEngineTimer's Windows vtable = RVA 0x1485598** (CreateTimer 0x140dea830 -> 0x140e13630 ->
  0x140e12c00 -> its ctor 0x140e12b50 stores it): 3 slots (scalar deleting dtor, destroy = what a release calls at
  vtbl+8, the update 0x140e127b0). **Its fields moved**: Itanium places a derived class's first fields in the tail
  padding of the {vptr, i32 refcount} base, MSVC after it - frame ms +0xc -> +0x10, frame s +0x10 -> +0x14, total ms
  +0x14 -> +0x18, total s +0x18 -> +0x20, num/den +0x20/+0x24 -> +0x28/+0x2c, the clamps +0x2c/+0x30 -> +0x34/+0x38, the
  carry +0x78 -> +0x80 (both updates read side by side; TM_* are OFS() now). This also feeds the loco dt and the orc
  attack clock (both read TM_FRAME_S of a CEngineTimer). TM_SCALE +0x30 is plausible (the ctor's 1.0f; no update reads
  it; only our own frozen timer is written there). **Any class whose base ends in a 4-byte member can shift like this.**
- **CB_TIMER_RELEASE, CB_ATK_BYNAME, PR_TMPL_BYNAME: DLL-provided entries** (`somvr_win_provide`, win/README.md 6):
  the SITE points at our function (its own bytes as the check), the features' code unchanged. The release: -1 at +8,
  at 0 vtbl[1]. The lookups: `somvr_recdb.h` = Linux FUN_02166790/FUN_02153910 (hash h*0x397 + (signed char)fold[c],
  binary search of 0x30-byte records by key +0x28) over *CB_ATKCAT_PP / *PR_TMPL_CAT_PP (0x18f8998) with the fold table
  RDB_FOLD 0x13bad70 (all 256 bytes identical to Linux 0x3898a10, checked by tests/test_winabi.c with the game files).
- **BR_NODELOG**: Windows-only SITE BR_LOGPRINTF = the empty stub Combat_LogPrintf 0x140e90860, an entry hook
  (MinHook) with a varargs formatter (prefix rcx, fmt rdx, ...) only while the node-log window is open; the byte
  0x1989eca as before, no sink on Windows (br_nl_frame leaves it alone). Test switch only.
- **OB_RANGE**: Windows-only SITEs OB_RANGE_KIND 0x4e0bf0 (rec -> kind bits), OB_RANGE_A 0x4e0b30, OB_RANGE_B 0x4e0c90
  ((out*, rec) -> {min, max} m) and OB_RANGE_MASK 0x18f4308: `ob_range_win` does what the marker update 0x1404e6770
  inlines. All three take a NULL record.
- **VG_FXC/VG_FXC1**: Windows-only SITE VG_FXCREATE 0x432b00 (the inlined wrappers' gate + create, 66 callers), one
  entry hook, somvr_w_vbglow unchanged (6 integer arguments). CP_FX's call and FI_FX_CREATE's check name the same
  function; both start before vg_start (the hook changes its first bytes).
- **FX_NEAR / FX_BILLBOARD's proxy** (render 7.5): the thunks take `this` in rcx; the render context's interface on
  Windows is map 8, unmap 9, draw 14 (RD_PXS_*); the unmap thunk keeps rcx/rdx/r8/r9 and xmm0-3 with a 32-byte home
  area; the three proxy vtables have their Windows layouts. Not checked on Windows: the custom render callback's
  fields rd_px_draw reads for its rotation log (+0xc, +0x80..+0x98, +0xa0, +0x100: logs only).
- RD_OVERLAY: done by winport-render (RD_OVERLAY_IF alone, d4c7f18).

**Still blank on Windows** (none rows, not this task's): KL_SYNC_END, FR_CAMCOPY_B (redesigns in 7.3), FB_SERVER,
FB_SYNC_REC, FR_REARM, BOW_TOKENS_ADD, PRN_NAV_POS (not found), GL_* (not needed: 7.4), G219-222 and VK_TEX_* (core:
not needed / DXVK), RES_3D_DESC and HUD_PASS_COVER (stereo 7.7: covered by RES_3D_DESC_MID and HUD_PASS_EYE), RD_BRT_B
(= RD_BRT_A).

**Tests**: tests/test_winabi.c (in build.sh --check): the lookup against an instruction-by-instruction transcription
of FUN_02166790 (every count 0..64, keys found/missing, unsigned compare, signed fold bytes), RA_SYSV*/RA_WIN* over a
built WRAPPER frame (the entry rsp at +0x150, arguments 5-7 past the home area, a bool's low byte), and with the game
files the two fold tables and the lookup's `imul edx,edx,0x397`.

**Next (first boots):** SOMVR_SLOTS (SL_SHEATH_JMP) and SOMVR_DEFENCE (DF_IMPACT) with an orc; SOMVR_HIDE_HEAD=parts
(GBUF/VEL pass-through: no holes in the world's G-buffer); the hit-stop line `COMBAT hitstop on ... frozen at N ms`;
SOMVR_POINTER over a menu; SOMVR_OBJECTIVE's range; SOMVR_FX_NEAR / FX_BILLBOARD draws; `SITES provided by the DLL: 3
of 3` and every HOOK line's stub page within 2 GB.

**Follow-up after base's first GOG boots (2026-11-02 23:50, on t3/winport-integration fbbf208).** Base's boot predates
this branch: its 18 "SysV-blank" sites are exactly the 18 live rows ported above (LP_GUARD_* 2, BOW_FA_CAMXF,
LOCO_ROOTDELTA, SCR_NEM_PAD, HP_DRAW_* 5, HP_CUST_* 8), and KL_AI_*'s "no stub page" is bo_patch_call's old loop -
both fixed in the merged tree (g_sysv empty; no low-address stub loop left: grep MAP_FIXED / far_tries = stub_page's and
stub_alloc's Linux branches only). Then:
- **Linux-image ranges**: `game_range(rodata, &lo, &hi)` (somvr.c; Windows the PE's .text / .rdata via the new
  `somvr_pe.h`): the brand's "is the slot's function in the game's code" test (0x400000..0x3400000: on Windows every
  B check answered -1) and hpskip's vtable filter.
- **Static global sets with Linux addresses** (nine, not wrapped by any group; the selftest's stand-ins): g_fig
  (finish), g_prg (practice), g_rxg (reactions), g_bog (the bow's item schema), g_thg / g_slg (the Wwise wrapper),
  g_dfg (defence), g_ffg (front), g_kp_dbg (captain), and the verbs' VB_SCH_* / VB_CMD_TABLE defines. On Windows they
  read - and with SOMVR_FINISH_SEVER (default on) or the front's gamelog *wrote* - Linux addresses in the Windows
  process. `hooks_win_globals()` now sets every field from offsets-hooks.tsv (GADDR: unknown = 0, fail-safe). Found
  (confirmed side by side or by hundreds of aligned uses): the object system holder 0x19d80d0, its vtable 0x148ca00, the
  FX gate byte 0x17fab60, CBaseTarget's vtable 0x13bfe48, IGameServices 0x18f1450, the spawn manager 0x1984e60, the Wwise
  wrapper 0x19e6430, the front's two debug bools 0x1989ed0 / 0x1989ece; the sever bool 0x1989ebb plausible. The
  stealth bubble's raw Linux getter call is skipped on Windows.
- **Not found: the records DB's schema holders** (CharacterRecord, node, Attack, AnimationData, the block table and
  entry, AttackData, the category list, the item schema, the verbs' three): 0 on Windows, so the finishers, the
  reactions' block reads, the defence's attack data and the bow's RangedWeaponData read nothing there. Windows keeps
  its schemas in a registry (FUN_140aa4a20: `[rcx + 8k] = &schema_k`, objects 0x40-0x70 bytes apart in .data from
  0x141803a40); Linux's holders are code-initialised (`mov qword [H], H+0x40` then a zero-filled field table the DB
  load fills). Leads (aligned loads): CCR 0x1846d80, node 0x1846d40 (adjacent registry slots +0x12e8 / +0x12e0),
  Attack 0x1846c60 | 0x1843ec0, AttackData 0x1845430, the item 0x1804f70 | 0x1803e70. Next: resolve each at run time by
  its field names (holder -> entries, +0 the name: CCR field 1 "Nodes", Attack 0 "AnimationData", AnimationData 8
  "Sync", AttackData 16 "RecoilTypeHit", the item 6 "RangedWeaponData") among the registry's schemas, once the DB is
  loaded - or confirm the leads that way at the first boot.
- **Combat's "not found" rows that exist**: FB_SERVER = 0x14044a9d0 (`mov rax,[rcx+0xa08]; ret`, called by
  0x14058d050 where Linux 0xa97550 calls FUN_00a7f820); FB_SYNC_REC = KL_SYNC_END = 0x140c8b870, FUN_028060a0
  instruction for instruction (the fallback's sync read and the held kill's end were off on Windows). BOW_TOKENS_ADD:
  the DLL's add over the setter BOW_TOKENS_SET 0x140cdb3d0(res, value) - it is in the bow's fire_bits, so **our own bow
  shot was off on Windows**. Live rows now 326 (29 none: GL_* 7 and G219-222/VK_TEX_* not needed, the DLL-provided /
  redesigned ones, FR_CAMCOPY_B (nothing to patch: CAM_A alone sees both), FR_REARM, PRN_NAV_POS).
- tests/test_winabi.c: somvr_pe.h on the exe (.text 0x1000+0x139fadf, .rdata 0x13ad000+0x44594f; the new RVAs inside).

### 7.9 winport-hooks: the records DB's schema holders on Windows, found by name and checked at run time (2026-11-03)

**The schema object is the same on both builds**: +0 the field array (0x30-byte fields: +0 name, +8 hash; filled when
the database loads), +8 u32 the field count, +0x10 short name, +0x18 description, **+0x20 the full name**. Linux builds
them in code (`mov qword [H], H+0x40; mov dword [H+8], count; mov qword [H+0x20], "Combat.Tree"`), Windows
constant-initialises them in .data and lists all 1208 in one registry function, FUN_140aa4a20 (`lea rax,[rip+S]; mov
[rcx+8k],rax` x 1208, then `mov rax,rcx; ret`). Every holder the mod reads has exactly one Windows schema with its
Linux name and field count, so 7.8's open rows are now confirmed by identity (offsets-hooks.tsv):

| holder (users) | schema | fields | Windows RVA |
|---|---|---|---|
| SCH_CCR (finish, reactions) | Combat.Tree | 106 | 0x1846d80 |
| SCH_NODE (finish, verbs) | Combat.Node | 11 | 0x1846d40 |
| SCH_ATTACK (finish, reactions) | Combat.Attack | 7 | 0x1846c60 |
| SCH_ANIM (finish) | CombatAttackAnimationData | 34 | 0x18453f0 |
| SCH_BTBL / SCH_BENT (reactions' block roll) | Combat.AIBlockTable / CombatAIBlockChance | 1 / 2 | 0x1845580 / 0x1845510 |
| SCH_AD (reactions, defence) | CombatAttackData | 104 | 0x1845430 |
| SCH_CL (defence) | Combat.CategoryList | 1 | 0x1843f30 |
| SCH_ITEM (the bow's RangedWeaponData) | Inventory.Item | 27 | 0x1804f70 |
| VB_SCH_CHORD / _INPUT / _INREC (verbs, abilities) | CombatNodePredicate / Combat.Chord / Combat.Input | 3 / 3 / 10 | 0x18451f0 / 0x183f8d0 / 0x183f600 |

(AI.Constants, the stealth bubble's 0x4b97240, is 0x184bb60 - unused: its getter call stays Linux-only.) The leads of
7.8 were right for CCR / node / Attack / AttackData / the item / the Input and wrong for AnimationData.

**The checker** (`somvr_schema.h` + somvr.c `sch_pass`; the writer thread once a second from the start, 30 min at
most; the test word `schema` at once with a line per holder): on Windows it decodes the registry (Windows-only SITE
SCH_REGISTRY, its bytes checked; read only, never called), finds each wanted schema by full name + count (a name with
another count or two with the name = none, never a guess), logs `SCHEMA <name> (<n> fields): the registry's <addr>
(rva ..), the table's rva ..: the same | DIFFERENT: the registry's taken | ...` and sets the mod's holder(s) from it;
then, once the database has filled the field arrays, checks the names of the fields the mod uses (Combat.Tree 0
Weapons, 1 Nodes, 2 RecoilNodes; Combat.Attack 0 AnimationData, 1 AttackData; CombatAttackAnimationData 0 Name, 2
BehaviorNode, 8 Sync; CombatAttackData 0 AlignToTargetType, 16 RecoilTypeHit, 23 CanInterrupt; Inventory.Item 0
MaxQuantity, 6 RangedWeaponData; the block table's Entries, the block chance's Category / BlockChance, the verbs' Input
/ Chord / DoubleTap, Command1 / Command2 / MaxPressDelta, ActionId / InputType / MaxHeldDuration; the category list by
identity only - its field's name is not recorded): `SCHEMA <name>: CONFIRMED by its fields at <addr> (rva ..)` or
`NOT the schema ... its holder 0`. Only the mod's own variables are written; nothing in the game. A holder whose
fields never load in the 30 min stays on its identity (`SCHEMA done ...: N confirmed by their fields, M by their
identity only, K none`). On Linux the same checks run on the Linux holders and only log (a self-test of the
fingerprints; the holders never change there).

**Tests** (tests/test_winabi.c): a stand-in registry (code bytes with disp8/disp32 slots, five schema objects, a
duplicate name) - decode, the error shapes (no ret, a slot past max, an unaligned store, another instruction), find by
name + count, the field check (no name yet, no array yet, empty names, confirmed, a field that differs, another schema,
a fingerprint past the count); with the game files the exe's own FUN_140aa4a20: 1208 slots, the 12 holders found by
name at the table's RVAs.

**At the next boot** (winport-base): the 12 `SCHEMA ... the same` lines at once, the `CONFIRMED by its fields` lines
after the first load, `SCHEMA done`; then the finishers (FI_* reads), the block roll (`COMBAT block`), the defence's
attack data, the bow's own shot (RangedWeaponData + BOW_TOKENS_ADD), `verbs tree`.

### 7.10 winport-launch: the user's launcher for the Windows (GOG) build (2026-11-03, branch t3/som-winport-launch-task)

**Start**: WiVRn's app list -> `~/.local/share/applications/Shadow of Mordor VR (Windows).desktop` (the one file
outside the repo; its copy `scripts/som-vr-headset-win.desktop`; Categories X-WiVRn-VR, Comment "the GOG game - no
Steam needed, adds no Steam hours") -> `scripts/som-vr-headset-win.sh` (the main checkout's) -> the systemd user unit
**somvr-headset** (the Linux sessions' unit: one game at a time, every helper's "the user plays" guard sees it, the PC
window's Stop works unchanged) running a private copy of `scripts/som-win-session.sh`. som-vr-headset.sh and its
.desktop are untouched; they share `$XDG_RUNTIME_DIR/somvr-headset/` (the ending / next-start files), so a start of
either while the other's session ends waits for its end.

**The frozen build** (`WIN_BUILD=` line; `scripts/som-win-freeze.sh NAME [--from CHECKOUT] [--env FILE]`): logs/NAME =
`git archive HEAD` of mod/ (+ handmesh_gen.py, the reference/third_party links), built there (`build.sh --check` ->
check.txt: the Linux layer; `win/build.sh --check` -> win-check.txt: the DLL), headset.env (default
scripts/headset-build26.env; the launcher reads it with som-vr-headset.sh's rules - SOMVR_* only, plain values, never
sourced; SOMVR_BRIDGE / SOMVR_XR_TEST / SOMVR_WIN_TEST / SOMVR_WIN_SITES / SOMVR_SAVEDIR refused), `proton/` = a
`cp --reflink=always` copy of Steam's Proton - Experimental files (no extra space on this btrfs disk; Steam's updates
cannot change a frozen build; proton.version), FROZEN.txt (commit, sha256 of DLL / layer / env, Proton).

**The session** (som-win-session.sh, the Windows twin of boot_check.sh --headset): the guards (the GOG exe's sha256 =
f5d72578..., the tables' exe - a GOG update refuses with "the VR build does not know this version"; no other SoM game;
the slot marker reserved-envsom held; 4 GB VRAM; WiVRn running), the game on Proton's own wine (never `proton run`:
no steam.exe, no Steam), `WINEFSYNC=1` (as Proton sets it), `WINEDLLOVERRIDES=xinput9_1_0=n,b;d3d11,d3d10core,dxgi,
d3d9=n;winealsa.drv=d;wineoss.drv=d`, the install as a bwrap overlay (writes -> RUN/game-upper; the GOG folder is
never written), a headless gamescope 1920x1080 at **90 Hz** (the headset's, as the Linux sessions), the layer from the
build with `XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json`, `SOM_BOOTCHECK_RUN` = the session folder (the VR
menu's Quit and the bug marks work: the same quit.request / marks/<id>/want serving as boot_check, voice from
"wivrn.source" through voice_rec.py, mark_compose.sh after), the start sequence scripts/cmds/somvr-vr6.cmd unchanged
(the Windows build's `SESSION ui` lines are the Linux build's: the main menu `ifs=1 menus=15` ~7 s after the start,
SOMVR_SKIP_SPLASH works), the end watch (the game exits; 10 s after the layer's exit handler; 60 s without a frame;
a Wine crash line), the stop (the word `endlogs`, the save left quiet for 2 s, `wineserver -k`, the tree killed), the
4 h limit (hold 4 h - 200 s). DXVK_STATE_CACHE_PATH = logs/win-play/dxvk-cache (persistent; the overlay would lose it).
The VR graphics preset: SOMVR_RENDER_SET -> the prefix's `Documents\WB Games\Shadow of Mordor\render.cfg` = the PC's
detected values (boot_check's rating-10 set; the Windows build under Wine detected rating 1, everything 0) with the
preset over them, before every session (the old file kept as RUN/render.cfg.before).

**The prefix and the save**: `logs/win-play/prefix` (ours alone; never the Heroic prefix) is made at the first session
as a reflink copy of base's logs/winport/prefix (Proton-made, DXVK in system32, Proton's ShowCrashDialog=0, the
first-run screens passed) **without its Saves**; the frozen DLL goes into its system32 before every session. The game
plays on the prefix's own `Saves\GameData.sav` (the GOG steam_api64.dll's remote storage, 7.1), so the Windows
progress stays. None there: the newest Windows backup if any (a lost prefix keeps its progress), else a **copy** of the
user's Steam save `userdata/*/241930/remote/GameData.sav` (only read). Before every session a backup
`backups/win-save/<ts>-before/` (GameData.sav, SHA256SUMS, session.txt); after it the prefix save's sha and the Steam
save's sha + size/mtime ("SAVE OK: the Steam save is unchanged" - what the PC window shows as "your real save is
unchanged"). A test prefix never seeds from or writes into backups/win-save.

**Sound** (how the Linux sessions do it: `shared/tools/vr-session/vr-session-watch.py` AudioSwitch - while an app that is
not a test is connected to WiVRn's comp_ipc, the PC's default output is WiVRn's sink "wivrn.sink" and what plays on the
old default moves there; boot_check --headset leaves the game on the default): the session gives the game winepulse
only, to the default output - the same path. 30 s in, the game's stream (application.name "ShadowOfMordor.exe") is
checked once and moved to WiVRn's sink if it is elsewhere (the watcher's switch off); a line every 5 minutes. Proven
with silence (`scripts/som-win-audio-probe.sh`, `mod/somvr/win/tests/silence.c`: WASAPI eRender/eConsole as Wwise,
zero buffers flagged silent): Wine's default endpoint is "Speakers (PulseAudio Output)" and honours PULSE_SINK
(target.object = the named sink); the stream's application.name is the exe's name, process.binary wine64-preloader.
Tests (`--sink null`): a null sink of the session's own (unloaded at the end), PULSE_SINK to it, /dev/snd hidden, and a
guard every 0.25 s: a game stream (by name or by the bwrap tree's pids) on any other sink is muted at once and the
session stops ("!!! AUDIO GUARD").

**Proven** (2026-11-03 01:49, `scripts/som-vr-headset-win-test.sh`: the game slot as common.md, a fresh test prefix,
`SOMVR_HEADSET_TEST=1` = the launcher's own command line with Monado's null compositor for WiVRn and the session's own
null sink; run logs/win-session-20261103-014905): the unit somvr-headset, the prefix made (377 MB, reflinked), the save
seeded from a copy of the Steam save (9d97bf3f...), the backup, render.cfg with the preset, the DLL in system32, the
game found at +3 s, somvr-vr6.cmd unchanged: the main menu at 13 s, Continue -> the world at 34 s, SOMVR_PRACTICE's trip
to the meadow, stereo at the null compositor's 20 Hz (H1 40/s, 1124 stereo frames, one locate per frame through the
bridge, SOMVR_EYE_SIZE=xr), the game's stream on the null sink unmuted (the guard never fired), a bug mark (the word
`mark`) served with flat.jpg, eyes.jpg and seen.jpg (both eyes as the headset shows them, the card), `quitgame` -> the
DLL's ENDLOGS -> wineserver -k -> gone in 1 s; the prefix save 9d97 -> 3a18 (the meadow's autosave: progress kept),
"SAVE OK: the Steam save is unchanged", the PC window (on a headless screen of its own in tests) "In the game" with
the live picture and the mark, then "Ended - your real save is unchanged"; WirePlumber's remembered state for
ShadowOfMordor.exe mute false, the default sink unchanged. The dry run (`SOMVR_HEADSET_DRYRUN=1`, the user's path:
WiVRn, the play prefix "would be made", the seed from the Steam save, 389 environment entries, the overlay probe)
passes. Proton's wine reports "ntsync: up and running" (WINEFSYNC=1 is then unused).

**Traps found**: (1) the game names its main thread `PrimaryEngine`, so /proc/PID/comm is not the exe (and pgrep's
names stop at 15 characters): the Windows game is found by its command line (`Z:\...\ShadowOfMordor.exe`) and, for the
session's own game, its environment's WINEPREFIX; (2) Wine's processes do not stay in bwrap's process tree; (3) Wine
first opens a probe stream with sink index 4294967295 (no sink), then the real one: a guard must not count it; (4) a
mute set on a stream is kept by WirePlumber per application name (stream-properties) - the next session of the user
would start muted: the test guard moves a stray stream to the null sink and ends the game instead; (5) Wine's device
probe also opens a capture stream: tests give PULSE_SOURCE = the null sink's monitor.

**Freeze steps** (the coordinator): merge main into the branch to freeze; `ShadowOfMordor/scripts/som-win-freeze.sh
build-winN --from <that checkout>` (outside the sandbox, ~5 minutes; refuses an existing name; exit 1 if a check
failed); `WIN_BUILD=build-winN` in scripts/som-vr-headset-win.sh on main; `SOMVR_HEADSET_DRYRUN=1
scripts/som-vr-headset-win.sh`; optionally `scripts/som-vr-headset-win-test.sh` (takes the slot, ~2 minutes).
**build-win1** is frozen from d68a83e (= t3/winport-integration c65a946 + main 7bc4516's build 31c; both checks green,
Proton 11.0-20261027b, headset-build26.env's 371 switches) and is the line's value on this branch.

**Unproven (only the headset can judge)**: the sound in the headset (the watcher's switch to wivrn.sink for a Wine
client, the 30 s check's move); the frame pacing at the Quest's refresh under Wine/DXVK (gamescope 90 Hz,
SOMVR_XR_REFRESH=80 from the switches) and the real eye size from WiVRn; the controllers' bug-mark chord and the VR
menu's Quit button (headless: the trigger words); the voice notes (wivrn.source exists only with the Quest connected);
the PC window on the real KDE desktop (DISPLAY from WiVRn's application unit or systemd's environment).
**7.3 live (11-03, runs 20261103-020950 and -022120 on the GOG build, `cmds/win-kills1.cmd` / `win-kills2.cmd`):**
the kill's half never played in base's run 012944 because the kills' game clock (`kl_gnow`, and front's `ff_clock`)
read the game timer's total ms at +0x14 - on Windows +0x14 is the frame's seconds (a float); the total ms are at +0x18
(TM_TOTAL_MS: L 0xa85092 `mov 0x14(%rdi)` = W 0x140454179 `mov 0x18(%rcx)` in FUN_00a84b70 = 0x140453cc0), so the
elapsed time read 268 s after 0.2 s and the stall rule ended the kill at once. Fixed (OFS TM_TOTAL_MS). 020950: the
times are real (0.85 s), the edge-cut kill on a mid-attack infantry ends "not playing" exactly as the same blow does on
Linux (boot-check 013551 / 211847: that row's half does not take on an orc in that state). 022120 (deterministic):
`kills play AI_Bip_PC_Standing_InstaKill_F` on an infantry - started, the head +38 -> -29 cm at +0.75 s (the belly
stab's double-over), traced 6 s; `kills kill` with the row - `down (the head 58 cm lower at +0.30 s, its fall over)` ->
DealHit dead 0->1, the root 57 cm lower, "Kill +15 / +5 Execution"; the AI hold refused 3 of the orc's own starts (the
KL_AI_* hooks with the float rate). `kills vt` (live vtable pointers = the tables): CWBTimer 0x14147f4e0 (Set/Get/Create
at slots 8/9/2), CEngineTimer 0x141485598, the object system 0x14148ca00 (the model-handle getter at +0x118), the model
0x141482210 (GetNode at slot 16), Combat 0x141461880, NPC 0x1414498b0, CPlayerObj 0x141460160 (FR_VF59 at +0x1a8,
slot 53). The user's save only read (9d97bf3f... before and after).

**7.3 live 2 (11-03 02:51-03:20, runs 20261103-025133, -030808, -031626): base's two Windows FAILs were the tests' staging.**
- The hanging chop: base's run (022457) used fling31's `ladder` plan, whose sword step hangs at the ladder's top with the
  far orc ~3.6 m below (`its chest -364 cm from the eyes`) - Linux fails it the same way (114233, 115645); the hit while
  hanging is `swordlow`'s (Linux 125711, 182258). On Windows `swordlow` (025133): the orc came to 1.1 m and opened with
  ReactToSurprised (range 1-2 m), out of the chop's reach; its hit ended the hang (SOMVR_CLIMB_HIT=drop) before cut 2.
  With SOMVR_CLIMB_HIT=hold (030808): cut 2 `COMBAT hit tier=light ... -> Combat_DealHit ... health 95.0->85.0` with the
  climb on - `PASS sword: a hit on the orc while hanging`. The Windows hit path from a hang works (cmds/win-swordlow.cmd).
- The bow's beside shot: the headset envs since build 20 set SOMVR_BOW_ASSIST_DEG=0, so `bow aim assist` had no cone and
  logged no assist line (the code logs it only with a cone or exact) - Linux the same. New test word `bow assist DEG`
  (somvr_bowaim_game.c); bow20-drive.sh sets 5 for its beside step, then 0. 031626 (cmds/win-bow-assist.cmd): 15 PASS,
  0 FAIL - the beside line `assist: the nearest orc's head ... 7.6 deg off the ray: straight along the ray
  (SOMVR_BOW_ASSIST_DEG 5.0)` (outside the cone: no hit), exact through the body `Bip_Torso`, through the head
  `Bip_Head`. combat11 and parry18 already ran on Windows (winport-drivers: combat11 012418, parry18 in 0124..0229).

### 7.11 winport-drivers: the Linux regression drivers on the Windows build (2026-11-03, branch t3/som-winport-drivers-test)

**The boot** (`scripts/headless_vr.sh --win MOD`): the same switch list as the Linux boot (headless_vr.sh's own + SOMVR_EXTRA;
the layer/runtime/log ones are headless_win.sh's) through `scripts/headless_win.sh MOD --game HOLD`, with the run folder
`logs/boot-check-<ts>/` (WIN_RUN) so that every `scripts/*-drive.sh` finds it unchanged (their `ls -dt
logs/boot-check-2026*` + `launch-cmd.txt` holding their build's name: the MOD path). headless_win.sh grew boot_check's
interface for a game run: `launch-cmd.txt` (`# game only (--game-env): ...`), the Linux names beside its own (game.log ->
wine.log, run.log -> summary.txt, xr-layer.log -> vklayer.log), `@RUN@` in WIN_ENV_FILE / SOMVR_EXTRA values
(final.sh's `SOMVR_XR_TEST=@RUN@/xrtest.txt`), `SOM_BOOTCHECK_RUN=RUN` (the mod's quit.request and marks/ through the
Z:\ path shim), the command mode (`RUN/cmd`: WIN_CMDS = HEADLESS_CMDS, default `cmds/win-vr6.cmd`, goes in at the launch;
lines the drivers append later - `quit`, `shot`, `key` - run in order; `cmd.done`), the player's requests served every
0.5 s (quit.request = a clean stop; marks/<id>/want -> served + flat.png + the log tails, never a voice), the word
`endlogs` before every stop (the END lines), and the save guard's line `save after: GameData.sav UNCHANGED (the user's
.../GameData.sav, only read: sha256 ...)` (the source save is copied into the prefix; its sha before/after), a
`!!! crash (wine.log)` line on an unhandled exception. HEADLESS_SAVE_FROM = the file to copy (default the user's one
Steam save, `userdata/*/241930/remote/GameData.sav`, as boot_check finds it). Not on Windows yet: HEADLESS_UNIT,
HEADLESS_HARD (soak), HEADLESS_MONADO_ENV, HEADLESS_LAUNCH=old, the render preset (`SOMVR_RENDER_SET` is noted, the
prefix's render.cfg stays as it is). WIN_PREFIX passes through (the helper used a reflink copy,
logs/winport/prefix-drivers). Trap: never edit a running bash script in place (bash reads it by offset as it goes) -
the harness was patched between boots only.

**The coordinator's mode** (`logs/coord/final.sh`, not in git): `FINAL_WIN=1 final.sh build-vrNN` = the same boot list
through `headless_vr.sh --win` (the suite folder `...-suite-win`; the frozen build's `build/win/xinput9_1_0.dll` is
built there once if missing: `win/build.sh --check` > wincheck.txt); `FINAL_TREE=/ABS/ROOT` boots a worktree instead of
logs/BUILD (BUILD = its folder name), `FINAL_SCRIPTS=DIR` takes the harness and the drivers from DIR. Needs this branch
merged (headless_vr.sh --win). The helper's own runner: logs/coord/winport/drivers/wboot.sh (the guards, the slot, the
release only if it names us).

**Found by the drivers and fixed** (rows in `win/offsets-world.tsv` / new `win/offsets-drivers.tsv`):
1. *The physics cast* (combat11's first boot: every `practice spawn` "the ray down hit nothing", the anchor's SIZE
   "the ground nan"; the climb's rays and the bow aim use the same cast): the published IEnginePhysics.Default instance
   0x141868c50 (`*0x141868c48`, given to the interface registry by FUN_1403eecf0+0x1d1) has vtable **0x141481d30**;
   0x141481d20 is a 1-slot vtable and 0x141481d28 d30's RTTI locator. The cast 0x141481f28 is slot **63** (Linux 64:
   MSVC's one destructor), not 65: CL_SLOT_RAYCAST 63, PR_SLOT_RAY 0x1f8; CL_PHYS_PP = 0x1868c48 (the instance pointer
   itself, valid from load) instead of one TU's holder 0x1419d7da0 (filled only if its TU registers after the registry
   exists). winport-base found the same vtable live (run 20261103-012113); its rows are the ones kept in the merge.
2. *The game timer* `*(*IN_PAUSE_PP + 8)` (combat11's second boot: a kill "268.44 s in" 0.1 s after its start, the next
   kill never dealt): its ms clock is at **+0x18** (Linux +0x14: W 0x140ad19c7 / 0x140ad1d79 hand the fight circle's
   grant `[rcx+0x18]`), the SlowMO scale num/den/rate at **+0x28/+0x2c/+0x30** (Linux +0x20/+0x24/+0x28: SlowMO.Set's
   setter FUN_140c38000); with the pause byte's +0x34 -> +0x3c (7.1) the struct shifts +4 then +8. The ms: winport-combat's
   TM_TOTAL_MS (the same field, found at the same time; kept in the merge: the kills' kl_gnow, the front's ff_clock);
   FI_TM_NUM (win/offsets-drivers.tsv: the finisher's and the execution's slow motion).
3. A test word `winpeek RVA|@ADDR [N]` (Windows only): N qwords at the image base + RVA and each one's first qword as an
   RVA - a vtable check at run time without a debugger.

**The drivers on Windows** (one boot each, `wboot.sh` = final.sh FINAL_WIN=1 on this branch's worktree; suites
logs/coord/t3-4a93142c-suite-win{,-r2,-v2}; the Linux reference = build-vr31b/31c's suites; every boot: the save guard
UNCHANGED, no crash, 6-7 WARN lines = the known open sites: HIDE_HEAD parts, GLYPHS, CLIMB_UNPIN, H9's late COM tag):

| driver | Windows | Linux reference | note |
|---|---|---|---|
| combat11 (2) | PASS: learn 16 hits, path 24 of 25 blocked, hold 16 hits, 2 of 2 kills (20.8 / 20.0 s) | 17-22 hits, 27 of 27 blocked, 14-22 hits, 2 of 2 (18-19 s) | v2 run 024000; the first two runs found fixes 1 and 2 |
| kills31 (131) | PASS: front, neck, stab, ground start; takes 4 kills 4; stealth/exec no start | 31c: stab, ground; takes 3 kills 3; front/neck/stealth/exec no start | see "open" for the orc's half |
| parry18 (13) | PASS: every phase runs (opens 0/3/5/5, ripostes 3, a shield broken), infantry 14 parries | opens 1/6/8/6, ripostes 1-2, a shield broken, infantry 19 | fewer infantry contacts: the learnt paths (open 2) |
| polish31 (134) | PASS 9 of 9 (the ring, the bow draw, place, the palm menu, Test tab, the body judge) | 9 of 9 | identical |
| sword31 (132) | PASS: two hands, a wound, none from the weak cut; the stab's wound 1 of 2 runs; blood made (fix 3) | all, the stab at once | open 3 |
| hands31 (133) | PASS: cues 8, grabs 3, the chord's kill; no vault cue | cues 8, grabs 5, the kill; no vault cue | same outcome |
| hla27 (101) | PASS 23, FAIL 0 | 23 / 0 | gesture bits 0x...fd027 vs 0x...ff027 (bit 13) |
| pcar29 (106) | PASS 6 of 6 (the leap countered, mounted, off by B) | 6 / 6 | identical |
| the quit path (1) | PASS: the game exited by itself after 83 s (scripts/cmds/win-quitpath.cmd) | exits by itself (144 s) | the GOG main menu has 5 rows: 4 Downs to Quit (quitpath.cmd's 5 wrap to PLAY and load the game again) |
| fling31 (136) | PASS 6 of 6 (wall off, the fling down caught / to rest, landed, the sword drawn and a hit while hanging) | 31b: 5 PASS, 1 FAIL (sword: no hit) | the climb's rays: fix 1 |
| combat11 on the merged build (cee2684, t3/b26-integration in) | PASS: learn 16, path 25 of 27 blocked, hold 17, 2 of 2 kills, both halves played (the head 97 / 99 cm down) | as above | open 1 looks fixed by combat's merged work; recheck with kills31 |
| kills31 on the merged build | front, neck, stab start: 3 kills, 2 halves played (the head 48 / 78 cm down), 1 not playing; ground: no start this run | as above | ground started in the first Windows run and on Linux 31c: recheck |

**Fixed while running them**: 1 and 2 above; 4: GO_RTPC's context (win/sites-ui.tsv) 24 -> 16 bytes as Linux's - its third
qword is AUDIO's slot, already patched when go_start checks (no Music row, no SOMVR_LOAD_VOICES; hla27's avail bit 13);
and 3: `df_fx_check` refused FI_FX_CREATE once VG_FXCREATE's MinHook entry
had rewritten 0x140432b00's first bytes (its own check of them passed first) - hitfeel's blood, the stealth kill's and the
defence's FX: "an FX site did not match"; on Windows the create counts as matched while that hook is in.

**Open (recorded, not fixed)**:
1. *The orc's half of a standing kill* (mostly fixed on the merged build: 4 of 5 played over combat11 + kills31 there) did not play on Windows in the first kills31 (3 of 3: "its half not playing (the head
   0.0 cm down in 0.85 s)"; the kill then ends by the stall rule's DealHit - the orc dies, without the animation); in
   combat11 v2 one of two played ("down: the head 102 cm lower"). Linux: 3 of 8 not playing over 31b/31c. Small samples;
   the start itself takes (VB_C_ATTACK = the row). For winport-combat: KL_START's Windows call and what makes the
   victim's half advance.
2. *TGUARD's learnt blow points* differ (Front_1 -56,-19,99 vs Linux -37,-40,108; Front_2 5,-67,28 vs 4,-70,61 cm):
   1-3 of 25 path guards unblocked, parry18's infantry 14 contacts vs 19. The orc weapon's node or the orc's frame on
   Windows (combat group's FIGHT orc weapon reads).
3. *sword31's stab* missed by 1.2 / 9.2 cm in two of three thrusts (the blade never in the body's surface) - the same
   geometry question as 2 (the body surface or the orc's spot).
4. hla27's gesture availability differs in bit 13 (no FAIL); the render preset is not applied on Windows (the prefix's
   render.cfg); the harness serves marks without a voice by design.

**7.3 offline 3 (11-03 04:00, winport-drivers' runs read): the open differences in the combat area.**
- *The ground kill "did not start" on the merged build*: it never ran - the game had exited during kills31's stealth
  step (boot-check-20261103-033929: `the game exited by itself after 134 s`, `program exit 5`, no wine exception: the
  game's own handler ends it quietly; somvr.log stops 0.5 s into the stab at the unaware orc's back). In the first run
  (013551) the same step logged `blood ... not made: an FX site did not match` and went on; winport-drivers' fix 3 made
  the FX (world blood, as sword31's that worked) - not proven to be the cause. A class to know: the game's
  "Critical Error" asserts store the message on Linux (DAT_04b54ad0) but on Windows also write to address 0 (`movl $0,
  0x0` after storing it at RVA 0x18f5b08): a call Linux survives ends the Windows process. Of the called entries in all
  sites tables only the active-item getter 0x1407bc970 has one (slot > 7): fi_active_item now checks the slot first.
  The DLL logs any fault before the game's handler (win/somvr_win_main.c `crash_note`: `WIN ... CRASH code .. at
  exe+RVA ... the game's error message '...' ... on the stack: exe+...`, at most 8) - the next kills31 boot names it.
- *The orc's half of a standing kill* (Windows 4 of 5 played on the merged build; Linux 31b/31c 5 of 8): the same
  spread; the start takes on both (the row on +0x4d0) and the clock is fixed (7.3 live) - nothing Windows-specific.
- *The learnt blow points / path guards / parries / the stab*: the guard samples the orc's weapon (the active item's
  entries +0x28/+0x30, 0x48 apart, the object +0x20 - the same on Windows, SL_REATTACH's twin; its `weapon_tip` node
  through GetNode slot 16 and NODEXF, both live-checked) at the key times (Combat+0xc8, the same: impact 0.800 s on
  both). Linux's own learnt points vary as much as Windows' differ: AI_Infantry_Melee_Front_1_LL over 34 Linux learns
  x -61..61, y -34..111, z -56..167 cm; Windows' 8 lie inside. Path guards: Linux 27/27 in two suites, Windows 23-25 of
  25-27 in three; the unblocked ones' checks look like Linux's (the blade 0.6-2 cm on the predicted point, its weapon
  20 cm off it at -0.10 s). No Windows offset found in these reads; the remaining gap is timing-sized, not geometric.
- The execute counter's lapse log read the clock's seconds at +0x18 (Windows +0x20): TM_TOTAL_S now (log only).

**7.3 live 3 (11-03 08:31-11:37): the quiet exits named by the DLL's crash notes and fixed.** "Exit 5" = 0xc0000005 & 0xff
(an access violation; the game's handler ends the process without a wine message).
- abil27 (083149, 181 s) and sword31 (083509, 84 s): `CRASH code 0xc0000005 at exe+0xd11df9 reading 0xffffffffffffffff`
  (a non-canonical pointer) in 0x140d11df0, called from the ClientFX create 0x140432b00 -> 0x1404321d0 with its 6th
  argument. Our creates (fi_create_fn: hitfeel's blood, the execute glow, the finisher, the clank) declared it `int c`;
  on Win64 the 6th argument is a stack slot and an int writes only its low half - the create dereferenced the stack's
  old high half. c is uintptr_t now (0b50fca); the VG_FXCREATE hook forwards uint64 and was right. abil27 reran clean
  (101559, 192 s, stopped by the harness). A rule for every typedef: an argument from the 5th on that the game reads as
  64 bits must be declared 64-bit (ints there leave the high half to chance).
- render31 (084037, 102814, SOMVR_FX_TEST): `at dll+0x68f76 / 0x68f50` = rd_fnv - first in somvr_dc_updsub (the
  UpdateSubresource source read for the buffer's ByteWidth, now through peek, 0b50fca), then in rd_vr_present: the
  previous frame's CPU-particle vertex ranges hashed again at the next present - Map pointers DXVK has unmapped by then
  (D3D11 keeps a mapping only until Unmap; IndirectX kept them readable). rd_fnv_peek for them (de02815). render31
  reran clean (113204, 296 s, stopped by the harness). winport-hooks reruns 102 132 135 45 13 twice on a tree with both.
### 7.12 winport-hooks: the quiet exits at exe+0xd11df9 - an `int` in a Win64 stack slot (2026-11-03)

The crash note (`CRASH code 0xc0000005 at exe+0xd11df9 reading 0xffffffffffffffff`, the game thread, right after a
sword swing) has `somvr_w_vbglow`'s call of the original on its stack, but the hook is innocent: its compiled frame
(0x4a8) passes the entry's four registers and its arguments 5/6 ([entry+0x28]/[+0x30] -> [rsp+0x20]/[+0x28]) through
unchanged. The value comes from **our own FX creates** (hitfeel's hit FX - hence the swings -, the finisher's glow, the
execution, the defence's flash): `fi_create_fn` declared the create's 6th argument `int c`. 0x140432b00 reads it as a
qword (`mov rax,[rbp+0x7f]`) and hands it to 0x1404321d0, whose 0x140d11df0 dereferences it when non-zero. **Win64
passes argument 5+ in an 8-byte stack slot and an `int` writes only its low 4 bytes** (clang: `movl $0, 40(%rsp)`,
with `uintptr_t` `movq`): the high half is whatever the stack held. SysV passes it in r9, where a 32-bit move
zero-extends - Linux never saw it. Fixed as `uintptr_t c` (winport-combat's 0b50fca, found independently; merged here
with its UpdateSubresource fix). **Rule for every game-call typedef**: a parameter the game reads as 64 bits must be
declared 64-bit from position 5 on (an int/bool/float the callee reads as such is fine). The audit of the 23 typedefs
with a 32-bit type at position 5+ found no other: D3D11 COM methods and Wwise PostEvent take real u32s, the bools are
read as bytes, the floats positionally, practice's spawn has u8s there and 64-bit p7-p9.

**Proven** on this tree (935e58e: t3/b26-integration + 0b50fca + de02815, combat's render31 fixes) with
`final.sh FINAL_TREE=<tree> FINAL_WIN=1 FINAL_BOOTS="102 132 135 45 13"`, twice (logs/coord/t3-4eb13695-suite-win-fx1,
-fx2; runs boot-check-20261103-115459 .. -121613 and -122938 .. -125100): all ten boots ran until the harness stopped
them (191-477 s), no `exited by itself`, no CRASH note, 0 SIGSEGV lines; our hit FX made in each (2-12 `rc=1 on the
sword` lines); and the schema checker of 7.9 live for the first time: `SCHEMA done after 1 s: 12 of 12 holders
confirmed by their fields` in all ten.

### 7.13 tests: the platform-neutral unit tests on the Windows build (2026-11-03, som-b34-win-tests, branch t3/som-windows-tests-b34)

**What runs.** `win/build.sh --check` ends with `win/wintests.py` (win/README.md 7): every C unit test of build.sh and
vklayer/build.sh except the Linux-only three is built for x86_64-pc-windows-msvc (the DLL's flags + -Werror, win/compat,
static CRT) and run under Proton's wine in a throwaway reflink copy of logs/winport/prefix (no display, no audio
driver, HOME in the copy; deleted at the end). 60 tests: the 55 of build.sh (test_pose ... test_objective, test_fogdxbc
and test_winabi on the two binaries as Z: paths, and test_sha256 - win/tests/test_sha256.c, never run before, now in
build.sh too: FIPS 180-4 + the Steam-Win exe's f5d72578...) and vklayer's test_logic, test_env, test_void,
test_fogpatch. **All 60 pass under wine**; 59 print the very same last line as on Linux (the check counts equal);
test_finish counts 652 checks on Windows, 650 on Linux - its 200 random swings come from the CRT's `rand()` (srand 7),
another sequence in UCRT, and its per-swing checks are conditional: both pass. Linux-only: test_layer (the .so under a
fake driver, dlopen), hands_lvp (lavapipe), selftest (dlsym stand-ins patched in place). Time: the test step 11 s
(build 5-6 s with every source new, run 5 s: 0.1-0.4 s a test, the first wine start ~1 s, the prefix copy 0.1 s); the
whole `win/build.sh --check` 38 s after a header change (the DLL rebuilt), `build.sh --check` 3 min 13 s as before.

**The Windows-only bug they found: SOMVR_VMA_STRIKE.** test_vrmenu failed 3 checks on Windows only (the Controls page:
61 lines of 62, the gaze strike's "Wraith: hold Y; A = Strike" missing, and with any one build-27 bit alone the plain
Wraith line gone). `SOMVR_VMA_STRIKE = 2147483648u` was an enumerator: gcc gives that enum an unsigned type, the MSVC
ABI keeps every enum `int`, so it was INT_MIN and g_ctl's `uint64_t need, unless` held 0xffffffff80000000 - bits
31..63. Where: somvr_vrmenu.c in the **W1 Windows layer DLL** (native Windows players): the gaze-strike line never
shown, the plain Wraith line hidden whenever any of bits 31..63 was set (the dodge, the stealth kill, every build-27
gesture line). The game DLL's avail word was right by luck (`sg_on() ? SOMVR_VMA_STRIKE : 0u` converts to unsigned),
and under Proton the layer is the gcc-built .so: build-win2 under Proton is not affected. Fix: a macro
(`#define SOMVR_VMA_STRIKE 2147483648u` after the enum, same value). The same shape, harmless today (only ever in
uint32_t fields): SOMVR_CLIMB_E_EDGE and SOMVR_CLIMB_E_DN_END (`1u << 31` in somvr_climb.h's two event enums) - macros
now too. **win/build.sh's CF has `-Werror=microsoft-enum-value`**: a new enumerator past INT_MAX fails the Windows build
(clang's scan of every source found exactly these three).

**Two tests that assumed Linux.** test_winabi checked "the build's RA = SysV" unconditionally: on Windows RA1..RA7 are
the Win64 family by design - the check is per build now (`#ifdef _WIN32`: rcx rdx r9, the stack slots 5 and 7).
test_smooth's "a 40 deg jump: raw at once" measured `2 acosf(dot) < 1e-4 rad`; one float step under 1 is already 4.9e-4
rad, and UCRT's sinf/cosf round q2's norm differently from glibc's (0.056 deg "off" = two float steps under 1, with the pose untouched): it compares
the raw pose component by component (< 1e-6) and counts the reset now, like the 30 cm check above it. Neither was a
fault of the product code.

**For the owners.** somvr_vrmenu.h (the VMA bits): any bit 31 and up is a macro (`SOMVR_VMA_BIT(n)` for 32..63, a plain
`2147483648u` for 31). A new unit test: write it in build.sh's two-line form (`gcc ... -o "$D/build/tests/NAME"
SOURCES`, then `"$D/build/tests/NAME" ARGS`) and it runs on Windows without more; if it cannot (a Linux API with no
win/compat shim), add it to LINUX_ONLY in win/wintests.py with the reason.

### 7.13 som-b34-win-unknowns: the Windows values still unknown, found statically (2026-11-03, branch t3/som-b34-unknowns-research)

From `wintables.py --check`'s list (26 unknown globals + the unknown offsets of the groups' notes), each read side by
side on the Steam-Windows exe without Ghidra: the whole-text disassemblies (logs/winport/world/{lin,win}.dis),
pairs.tsv, the PE/ELF data, the schema registry FUN_140aa4a20. **Globals unknown 26 -> 5; 0 errors; both build checks
green.** The rows' evidence columns hold the instructions.

- **Console variables do not exist on Windows.** None of the `Combat.Debug.*` / `Combat.Disable*Cost` names is in the
  exe: each Linux `CConsoleProgramVariable_Bool` value is a plain .bss bool there, a block at 0x1989ea6..0x1989edf
  (no writer; the two front bools FR_DBG_SEL/AIMGR were already in it). Mapped by the pairs of every Linux reader of
  the value byte: Combat.DisableEnergyCost **0x1989ed3** (SK_CV_ENERGY; its four Linux cost copies are one folded
  function on Windows), DisableChargeCost **0x1989ed4** (SK_CV_CHARGE), Debug.Stats **0x1989eaf**, .XP **0x1989eac**,
  .HitCounter **0x1989ec0**, .ApplyDamage **0x1989ea6** (KP_DBG_*: on Linux the cvar object, on Windows the bool itself -
  kp_gamelog now writes it directly there, without the name check; the stat lines still go nowhere on Windows:
  Combat_LogPrintf is an empty stub). The strike's test word writes SK_CV_* on Windows now.
- **Schema holders by name** (as 7.9; one schema each with Linux's name and field count, the field indices from the
  Windows field builders - static initialisers that write each field's name, hash and index byte +0x28):
  GO_VSCHEMA = 'Client.VisionMode' **0x180b6d0** (fields 5/6 = RampInDuration/RampOutDuration, the mod's +0xf8/+0x128),
  XC_SYNC_SCHEMA = 'Combat.Sync' **0x1844fe0** (1 TargetAttack, 33 EnableDynamicSlowMo), GC_SCH_PRED =
  'CombatAttackPredicate' **0x1845120**, STL_AI_HOLDER = 'AI.CharacterTypes' **0x184c200** (0x2f
  CharacterSneakingAudibleStimulus). Not added to 7.9's run-time checker (these are GADDR macros, not variables).
- **The struggle vtables** (STG_VT_*, = world's MT_VT_*, now confirmed): five 5-slot vtables at 0x13cddf8 (InputDirection),
  0x13cded0 (PressCount), 0x13cdef8 (QuickTime), 0x13cdf20 (Hold), 0x13cdf48 (Aim), no RTTI; constructed by the factory
  0x1405ae840 (= FUN_00e15690); slot 1 of Dir and Aim are the pairs of their vf_02; each vtable's functions read its own
  class's fields at the Linux offsets (+0x2f0..+0x2f6, +0xd0..+0xd5, +0xfc/+0x100, +0x200/+0x204, +0x58c/+0x591/+0x592).
  The struggle code's inline field reads are the same offsets.
- **The front's lockout**: FR_REARM = 0x140ac9f60 (an entry row: FUN_028d6ef0 instruction for instruction), FR_LOCK_A
  **0x18598a0** (0.25 in the file), FR_LOCK_B **0x185989c** (0.75; read by 0x140acf860 = FUN_028d11b0's tail - the
  Linux disarm FUN_028d6de0 takes it in xmm1 on Windows).
- **The rest**: PR_SPAWN_LEVEL **0x1988a7c**, CB_NOSTIM **0x1988a80**, VB_CMD_TABLE **0x1901ac0** (the same lazy fill and
  `count = 0x15d`), SSH_P **0x1995560** (a function-local static: built by 0x140d987f0, rendered by 0x140d98480; the
  cascade pass's tail block has the same P offsets 0x3809/0x70f0/0x3918..; SOMVR_SHADOW_SHARE is off in build-win2 - turn
  it on on Windows only after perf27-eyediff.py --alt there), GO_VM_REC **0x28** (same), the Nemesis screen's
  SCR_UI_SM 0xb0, SCR_SM_MGR 0x10, SCR_MGR_LAYERS 0x198, COUNT 0x1e8, OVERRIDE 0x1ec (all the same: CPlayerUI
  FUN_00aca7d0 / 0x1404663b0, the menu manager's vf_12 = Windows slot 11 0x1405ce990).
- **Live effects in the Windows build** (build-win2's headset.env sets them): SOMVR_VISION_RAMP_MS=150,150 now reads and
  writes the vision ramps there, SOMVR_STRUGGLE=thrust now recognises the struggle kinds and presses their command,
  SOMVR_POINTER_MOUSE1=nemesis reads checked offsets (they were the Linux values unchecked before), SOMVR_PRACTICE reads
  the spawn level. The next Windows suite should show `STRUGGLE` lines with a kind (not "unknown"), and the vision ramp's
  CONFIG/apply line.
- **Still unknown**: MGR_CHAR (the +0x560 reads are the same offset in the animation code's pairs, but that class is not
  tied to BODY_MGR's; unused on Windows: player_objs takes bodyObj there), BR_NL_SINK (none on Windows: the empty
  Combat_LogPrintf), GL_* 5 (GLYPHS is off on Windows, 7.4), PRN_* (winport-world's), TEX_F3D/VKTEX_IMAGE (no Feral
  textures: DXVK interop), RD_SP_BOXBLUR/VIGNETTE (render's redesign), RD_CF_OFF (CURFRAME, off everywhere), NODE_BOX
  (a log).
### 7.13 som-b34-review-win: a correctness review of the DLL, the bridge, the layer's Windows paths and the scripts (2026-11-03, branch t3/som-b34-windows-review)

Read: win/* (the proxy, the exe gate, the entry redirect, the site rewrite, MinHook and the mid stub, the COM fill and
the late H9, the bridge file and nonce, the D3D/DXGI hooks and the interop, the compat shim, crash_note, the W1 layer
build and package), the layer's Windows paths (the bridge reader, the 1 ms locate thread, img_barrier's idle layout,
schedstat perf, W1's wineopenxr handles, the pin), headless_win.sh, som-vr-headset-win.sh and som-win-session.sh.

**Fixed** (each with a test that failed before; `build.sh --check` and `win/build.sh --check` green):
- **The safe reads were never guarded.** somvr_safe_read / somvr_safe_write (every peek, process_vm_readv, the W1
  layer's reads) copy under `__try/__except`, but clang without `-fasync-exceptions` catches a fault only inside a call
  that may unwind, and `memcpy` is a builtin known not to: the `__except` was dropped. The shipped DLL's
  somvr_safe_read had unwind flags 0 (no handler at all). A region cached readable and freed since - the case the guard
  exists for (DXVK's unmapped ranges, a freed heap block) - ended the game. Now the copy is an indirect call through a
  volatile pointer (the unwind entry: ExceptionHandler|TerminateHandler), IN_PAGE_ERROR is caught as well, and
  crash_note skips the faults a guarded copy handles (`somvr_win_in_guard`: no false CRASH note, the 8 notes kept for
  real crashes). Rule: under clang an SEH guard needs a call the compiler cannot see into.
- **The region cache tore.** Its slots were updated lo=0, hi, lo: a reader could take [0, hi), or one region's lo with
  another's hi, and call an unmapped address readable (then the fault above). Seqlocked slots now
  (win/somvr_win_rcache.h). win/tests/test_rcache.c (Linux, in build.sh --check: two writers re-caching two regions,
  two readers asking for the gap): before, 46k-68k false hits in ~35M reads per 1.5 s (3 runs); after, 0.
- **dlopen(name, RTLD_NOLOAD) took no reference** (GetModuleHandleA), so the dlclose after it released someone
  else's. On native Windows (W1) the layer's same_gpu does `dlopen("vulkan-1.dll", RTLD_NOLOAD) ... dlclose` at every
  session setup, so it dropped DXVK's own reference to the Vulkan loader (unloaded at 0 under a running game). xe_pin's
  pin pinned nothing. Now GetModuleHandleExA(0): one reference, as with glibc.
- **dladdr's dli_fname was one static buffer.** xe_pin calls dladdr twice and then opens the first name, which by then
  was the second module's. Now one kept name per module, as with glibc.
- win/tests/test_compat.c (win/build.sh --check builds build/win/test_compat.exe; `headless_win.sh MOD --test compat`
  runs it under Proton): before, 3 FAILs (version.dll unloaded by dlopen NOLOAD + dlclose; the first dladdr's name
  changed to the exe's), then exit 5: the guarded read of a freed page crashed. After: `test_compat: ok`.
- **headless_win.sh's slot-free `--test xinput` ended other helpers' games.** It needs no slot, but it ran in the
  shared logs/winport/prefix: its wine joined a running boot's wineserver, and its cleanup's `wineserver -k` ended that
  boot. Now xinput and compat run in logs/winport/prefix-xinput (a reflink copy made once, like prefix-w1), one at a
  time (flock).
- **headless_win.sh's picture loop outlived a TERM.** The player's launcher stops a test run with TERM; the loop kept
  shooting every 30 s for up to SECS, into whatever gamescope later took the same socket name. cleanup kills it, and
  the loop also ends with the script.
- wintables.py (a guard, no row affected): a DIR64 relocation that straddles a row's context/prologue window is an
  error. somvr_win_sites.c moves only whole qwords, and the exe is DYNAMIC_BASE, so native Windows relocates it while
  Wine loads it at 0x140000000. Such a row would fail on a player's machine only. 0 of the 327 live rows have one.

**Checked, no bug:** the XInput forwards (InitOnce, loaded outside the loader lock, the 5 exports); the gate (another
exe: nothing, no log); the entry redirect (bytes restored before the call; rcx = PEB untouched by `mov rax; jmp rax`);
the site rewrite (kind checked, whole relocated qwords; the GOG exe = the analysed one, so crash_note's RVA 0x18f5b08
holds); the COM fill (once per interface, 26 rows of the 256 buffers); the late H9 chain (game -> somvr_w_h9 ->
my_present -> DXVK); the mid stub (0x190 bytes of registers in the 0x1c0 frame, the call 16-aligned with its 32-byte home,
rbp and the flags restored through the frame, the RIP-relative slots copied with the code); the D3D typedefs
(D3D11CreateDevice's 10 arguments; slots 8/10/15 = Present / CreateSwapChain / CreatePixelShader); the interop (QI and
Release balanced; VkImageCreateInfo with pNext NULL, as DXVK requires); img_barrier's idle layout (both directions, the
first barrier from ALL_COMMANDS with MEMORY_WRITE); the bridge (tmp + MoveFileEx, the nonce, the process_vm_readv
check); the locate thread (one writer, every call inside somvr_xr_enter, joined in xl_stop; readers copy under the
seqlock); Win64 typedefs (7.12's rule: the RA5+/RA_STK users cast their 32-bit reads, cb_ltvec goes by pointer,
ob_range_t's out pointer comes first, the timer members put `this` first; no new case). The scripts: the save is
seeded only by a copy (.part + mv, sha checked), backed up before each session, and the Steam save's sha and stat are
compared before and after; the prefix guard refuses Heroic/compatdata/Games; the GOG folder is a bwrap overlay
(probed); sound is moved, never muted (the session: the game's stream onto WiVRn's sink once; tests: onto their own null
sink, picked by the app name or our WINEPREFIX).

**Noted, not fixed (latent):**
- somvr_win_hook_mid flips the whole 64 KB stub page to RW and back at each install. That is safe while every mid
  detour installs before the game runs (today: one, RES_3D_DESC_MID at the start). A later install would make the
  running stubs on that page non-executable for a moment: give each stub its own page, or keep the page RWX.
- somvr_xrloc's 16 image entries are never evicted. More than ~5 eye-texture remakes in one session would leave new
  images without their idle layout. Not reached: at most 5 images per Windows run, EYES gen <= 3 in every run.
- nanosleep's per-thread waitable timer is never closed: one handle per exiting thread that slept (W1 layer: the mark
  and screenshot writers). pft_scan's "all threads" dips when a thread exits (log only). load_env_file: a UTF-8 BOM
  makes the first line's key junk (harmless: package.sh's first line is a comment).
- Not run: a Windows game boot on this tree as a regression check of the peek path (the slot was held by other
  helpers for 100 minutes, until 20:27). The next Windows suite on a tree with these commits covers it: no CRASH note,
  the HOOK ok/SKIPPED counts as 171313's (96 / 2), PRESENT as before.
- som-win-session.sh keeps a backup from *before* each session only. If logs/win-play/prefix were lost, seeding from
  the newest backup would lose that last session's progress. An "-after" copy in save_lines would close this. It is the
  user's live launcher (main's script is copied at every start), so the change wants a SOMVR_HEADSET_TEST run
  first.
### 7.13 som-b33-soak: the long session on the Windows build (2026-11-03, branch t3/som-windows-soak-test)

**Soak 1** (boot-check-20261103-171756, logs/coord/b33/soak/): build-win2 as frozen (the user's next Windows build, its
headset.env, a copy of the settings and of the Steam save), `scripts/soak33-drive.sh` under `headless_vr.sh --win`:
77 minutes, two whole passes of 23 items - soak29's cycle (walk, fight, bow, flash, fire, map, menus, drop, captain,
death, vision, climb, brutal, pcar, tower, struggle, stealth, crowd, trip, death) + build 31's kills (kills31 front /
neck / stab / ground), the hands (hands31 shove / grab / kill) and a real load (travel: the forge tower top, the map,
X = Fast Travel: the world after 2 s, the game's cover 0.3 s, the HUD back after 4.2 s). Eye dumps off
(`SOMVR_XR_DUMP=` empty: none, the drivers' `dump` lines included), flat pictures every 5 min (`WIN_SHOT_S`).
Result: no crash, no CRASH note, VR the whole run, the save UNCHANGED, WARN 2 (the release suite's two: H9 not
installed, layout B without H8/H3), every item back in the world.

| | soak 1 (Windows, build-win2, 77 min) | Linux soak 4 (build 29c, 67 min; re-xr-pipeline.md 129.4) |
|---|---|---|
| RSS (MB) | 1073 -> 1259, +72 MB/h; pass 2 flat (1229 -> 1259 in 44 min) | 1292 -> 1422, +74 MB/h |
| the steps | first visits / deaths: flash +51, death +32, struggle +28, trip +23 | the deaths and first visits |
| the game's own VRAM / GTT (DRM fdinfo) | 1748-1901 / 84-203 MB, no trend | - |
| threads (Linux / Windows) | 74-78 / 66-67 | 80-83 |
| fds | 816 -> 931 (= the Windows sync objects: ntsync) | 202-203 |
| Windows handles | 671 -> 961 (+240/h; below) | - |
| wineserver RSS | 19 -> 24 MB | - |
| stereo rate (20 Hz headless) | 19.5 first 10 min, 19.7 last 10; the worst minute 15.0 | 19.7 / 19.2 |
| the layer's gaps 100+ ms | 12 in 77 min (Linux: 610 in 67) | 610 |
| game thread busy / ours a frame | 2.0-3.1 ms / 0.23-0.35 ms per item, no creep pass 1 -> 2 | 3.0-4.6 / 0.7-1.2 |
| somvr.log / layer log | 21.7 MB (16 MB/h) / 1.6 MB | 17 MB/h / 1.9 MB |
Every module's lines alive at the end (soak33_report.py's liveness: per kind, lines per 10 min by the log's own
clock; the only kinds without lines in the last 10 minutes are event-driven ones - VAULT, the layer's `menus`).

**The handles (not ours, harmless)**: by type (tools/win_hcount.c --names) the growth is Key +144, Event +41, Mutant
+27 in 51 minutes; 48 of the keys' 3-key sets are the same `\Registry\Machine\System\ControlSet001\Control\
DeviceClasses\{e6f07b5f-...}\##?#DISPLAY#Default_Monitor...` / `Enum\DISPLAY\Default_Monitor\0000&0000` keys,
opened and never closed. They come with DXVK's `Setting display mode: 1920x1080@60` (70 in soak 1; 6 in a short boot):
the game sets its fullscreen mode again at every pause/unpause (4 per transition: the map, menus, each death's
screens, the Nemesis screens - the release suite's nemesis27 boot had 38) and Wine's display code leaves the monitor's
device keys open. Nothing in our DLL calls a DXGI mode/fullscreen function; no frame gap at those moments. ~250
handles an hour = ~1000 in a 4-hour session: no limit is near.

**Found and fixed (Windows-only, both silent in the release suite)**:
1. *The last chance never answered on Windows* (77c1aef): `STG_VT_*` were `?` (winport-combat: "not separated yet"),
   so every struggle read as OTHER and the hands' thrusts pressed nothing - soak 1 lost three last chances in ~4.5 s
   each and knelt **227 s** in the captain's tutorial struggle (the game waits for an answer) until the driver
   removed the captain. The four 5-slot struggle vtables lie together after the string "Struggle = %s Passed"
   (0x13cde98): 0x13cded0 (its slots read +0x2f0/+0x2f4/+0x2f5/+0x2f6: PressCount), 0x13cdef8 (+0xd0/+0xd4/+0xd5:
   QuickTime), 0x13cdf20 (only the base's fields: Hold or InputDirection - Linux has five tables, Windows four),
   0x13cdf48 (its OnCommandOn W 0x1405ae360 reads +0x57a/+0x57b/+0x58c/+0x591/+0x592: Aim, = MT_VT_STRUGGLE_AIM).
   Every Linux struggle in soaks 3-4 was a CStruggleAim with the same +84/+88 values the Windows ones had.
2. *No pointer in the game's menus on Windows after ~1 hour of PC uptime* (b693982): the DLL's
   `clock_gettime(CLOCK_MONOTONIC)` is QueryPerformanceCounter, which under Wine is Linux's **CLOCK_MONOTONIC_RAW**
   (measured: a QPC read fell between two RAW reads, 0.305 s behind CLOCK_MONOTONIC at 22.6 h of uptime, ~13 ms an
   hour); the layer stamps its pointer block with CLOCK_MONOTONIC, and `ptr_tick` wanted `0 <= now - stamp < 250 ms`:
   never true once the gap passed the block's age. The map's pointer took 0 presses (soak 1, twice) and the trigger
   went to the game as RT (Sauron's Army); the pause menus' laser never moved the mouse (no `POINTER t=` line in the
   whole run). Now the age is taken against the clocks' offset when there is one (the smallest now - stamp over the
   last 64 new blocks, used past 50 ms); one clock (Linux) = the old test, the selftest's stale case unchanged, a new
   one for stamps 0.3 s ahead. The only other cross-clock use (loco's anchor jitter) takes differences: unaffected.
   The release suite did not catch it (no POINTER line there either: check `POINTER t=` in a Windows run).

**Also seen, left**: the map's `the selection under the cross is not a travel point` at the tower top (MAP_TYPE_TRAVEL
on Windows: logging only; X still travels).

**Recheck (soak 2, boot-check-20261103-203514)**: this branch's build (t3/b26-integration b24af4b = build-win2 + 23
commits, + both fixes), its scripts/headset-build26.env. The pointer fix live: `POINTER clock ... stamped +281.9 ms off
this clock`, then `POINTER t=` lines every 5 s with `the clocks' offset taken off` counting every tick (soak 1: no
POINTER line at all). **The game then exited by itself after 566 s (9 minutes: walk, fight, kills, bow, flash, fire
done), exit code 0**: no CRASH note, no Wine exception, nothing in the kernel log, no quit request, no END lines; the
last line is the driver's `hands squeeze` as it put the test tools back after `fire`; the handles, RSS and threads
flat before it. Our code exits only with 3 (ExitProcess(3) / _exit(3)), so 0 is the game's own exit (or a library's).
Not seen in soak 1's 77 minutes on build-win2.

**The cause (found 11-03 21:50): not the game, not the mod - another helper's test killed it.** At 20:44:35 the
hot-reload helper's worktree (t3/som-hot-reload ae49276, a pre-slots copy of headless_win.sh) ran `headless_win.sh MOD
--test xinput` (logs/winport/run-20261103-204435) with the old default prefix **logs/winport/prefix = slot 1's**, where
this recheck's game ran: its d3dtest joined the game's wineserver, ended at 20:44:39, and that copy's `trap cleanup EXIT`
ran `wineserver -k` - the game was gone at 20:44:40 ("exited by itself after 566 s"). A wineserver-killed game gives
exactly this run's signature: `program exit 0`, no END lines (no DLL detach), no CRASH or EXIT note (the VR stops of
21:02 show the same). The b35 slots harness already runs `--test xinput|compat` in their own prefix-xinput and makes a
slot >= 2 boot refuse a prefix in use; an OLD copy can still do it to any slot-1 Windows game, so every worktree that
runs headless_win.sh needs t3/b26-integration (or main) merged first.
**The control runs** (11-03 21:24-22:1x, slots 2 and 3 at once, WINEDEBUG err+all,warn+seh on A): A4 = this branch
(b26-integration 379fc8e + the exit note 26fc85b), B3 = build-win2's tree 2c57801 + only 77c1aef/b693982 + the exit
note (t3/soak-b-win2fix, a throwaway): the soak items with `fire` four times a pass. **Neither quit**: A4 ran 45 min (32
items, `fire` 5 times, 6 last chances, 4 map presses), B3 46 min (30 items, `fire` 4 times, 3 last chances, 2 map
presses), both until the harness stopped them at their hold's end; no CRASH note, no EXIT note, the save copies
unchanged; A4's wine.log with err+all: 4 lines (openxr/vrclient registry keys at start). Both fixes live
in both: `MAP ... a press ...: the map brought there` then `A (the waypoint)`; `STRUGGLE ... start: CStruggleAim`, and
in A4 a last chance **won by the hands** (`result(+8c) 1`, Talion up, not dead). The exit note (win/somvr_win_main.c:
RtlExitUserProcess / TerminateProcess / PostQuitMessage / a top-level DestroyWindow, the caller's RVA and the stack)
stays in: the next quiet exit names its caller, or - with no EXIT line - a kill from outside.
Not seen in soak 1's 77 minutes on build-win2. **Open**: rerun the recheck on this branch, and if it repeats, the same
items on build-win2 + only 77c1aef/b693982 (the two fixes touch only a struggle's class and the pointer's freshness;
the 23 newer b26-integration commits - build 33's watch, knife, Test page... - are the other candidates). The struggle
fix and the map's press were not reached in this run (the first death is item 11).

### 7.14 som-b36-winlooks: the Windows build's look against the Linux build's - the same at the same weather; the session starts on a clear day: `SOMVR_PRACTICE_SKY` (2026-11-03, branch t3/som-winlooks-b36)
The first real Windows session (11-03 21:06, build-win2) was reported smoother than Linux, but its grass looked worse
and denser, walls and ground flatter (less relief), and the bank under the ruins very hazy - was it another graphics
setting?

**1. The settings are the same, and mean the same.** The play prefix's render.cfg is the Linux session's (the rating-10
detection with the VR preset Vegetation 3, Mesh 2, Shadow 2, Lighting 1 over it; tessellation 1, textures 3, filtering
3, AO 3, OIT 0); the game kept every value and only rewrote AutoDetectedGPURating 10 -> 5 (the top of its 0..5 range on
both builds). The Windows exe's registration in rendersettings.cpp (RVA 0x13767e0..0x1376bc8: index, default, min 0,
max, name, help text per setting) against Feral's `TRenderSetting<E, 0, max>` names: the same ranges and help texts -
Lighting 0..2 ("0=low, 1=medium, 2=high"), Mesh 0..3, MotionBlur 0..3, OIT 0..1, Shadow 0..3, Tessellation 0..1,
TextureFiltering 0..3, TextureQuality 0..3, Vegetation 0..4 ("0=off, 1=low, 2=medium, 3=high, 4=ultra"). DXVK runs
feature level 11_0 with tessellationShader (the session's d3d11 log). Both builds carry the vegetation's per-platform
density fields (DensityScalePC, ...Orbis); their values are not decoded - the pictures below show the same grass.

**2. The same places in both eyes** (scripts/looks36-drive.sh: the meadow, the fort's wall, its paved floor, the dry grass
on its top, the bank under the ruins; 12 views, a bug mark each; scripts/looks36-cmp.py pairs two runs' views; the
pictures in logs/winlooks36/):
- Linux 225858 and Windows 230044 both arrived in `SON_Day02`: **the same picture** - the grass cards' number, size and
  layout at the feet and on the top, the floor's slabs and gaps, Talion's sun shadow, the lit arches, the bank's distance
  (cmp-L1-W2). The DXBC fog edit gives the SPIR-V edit's fog.
- Windows 223609 arrived in `SON_Day01`: a pale green-yellow sky, a low sun, haze in the valleys, the ground darker
  (cmp-W1-W2); the player's own Linux sessions of 11-02 and 11-03 were `SON_Day01` and show that sky.
- The reported session arrived in `SON_Day03`, the overcast day: grey even light without sun shadows (the slabs' and
  walls' relief reads only under a directional light), thick haze, the grass one dark mass. The player's 10-26 Linux
  session in `SON_Day03` looks the same (linux-day03-sheet.jpg). **The fog, the flatness and the grass's look were the
  weather**, not the build.

**3. Frame times** (the player's sessions, the real GPU, the same preset): Windows 11-03 21:06 at 120 Hz - the render
thread's frame work p50 4.74 ms (p95 6.55), GPU total p50 7.36 ms (p95 8.05; the game 6.90 + ours 0.39), ~100 frames/s;
Linux 11-03 09:32 (120 asked, the display 90) - frame work p50 11.0 ms wall (7.4 CPU): the render thread was the limit;
GPU p50 7.36-7.76 ms. The same GPU work = the same settings in effect; Windows' smoothness is DXVK's lighter render
thread. **No richer preset**: at 120 Hz (the VR menu's setting now) the GPU has ~0.9 ms p50 left. At 90 Hz there would
be ~3.7 ms (Mesh 3, Shadow 3, Lighting 2 fit by 119.4's ratios) - not measured on the headset, not shipped.

**4. ChangeWeather takes a weather definition, not a weather state.** The practice reads the state (`SON_Day01..03`,
`SON_Day03_Rain`, `BG_Day_02` ...); `GameServerShell.ChangeWeather` takes one of the 79 `Weather/Definition` records
(runtime-probes.md BW, probe-out/weathers.tsv). `ChangeWeather SON_Day0N` does nothing - no reset pause, the state
unchanged (boots 225858, 230044, 231242): the drivers' weather pins with it (render31, grass33, looks36's first boots)
never took. Measured (232408, the test word `practice weather DEF`; the state 10 s later; each a 0.5-1.1 s world reset):
`DayNoRain` -> SON_Day03 (overcast), the next time SON_Day03_Rain; `AnyTimeNoRain` -> SON_Night_Rain01;
`TEST_Day_01/02/03` -> BG_Day_01 (red dusk) / BG_Day_02 (clear, cold light; 231852) / BG_Day_03 (golden low sun);
`GP_BGIntro_Day02` -> IntroDay02 (dark); **`TEST_Day_NoWeather` -> SON_Day02, SON_Day01** (233052: three times, from
SON_Day03_Rain, SON_Day03 and SON_Day02). So the practice's wet-day target `DayNoRain` (build 6 on) can itself land on the
overcast or the rainy day.

**5. The change: `SOMVR_PRACTICE_SKY=dry|clear|always`** (somvr_practice.c `somvr_pr_is_overcast`,
`somvr_pr_sky_change`; somvr_practice_game.c; code default `dry` = the builds before: only rain or storm is changed;
**the som-b36-winlooks env block: `clear`** with **`SOMVR_PRACTICE_WEATHER=TEST_Day_NoWeather`**). `clear` also
changes an overcast day of Nurn (`SON_*` with Day03 / overcast / fog in its name) at the practice trip there - the
ChangeWeather goes with the trip's teleport, its reset under the blink's black (233052: `the game paused 0.06 s after our
ChangeWeather (under the blink's black)`, over 1.10 s later; `SOMVR_PRACTICE_LOADHOLD` holds the black). `always` (a test
aid) changes any weather. New lines: `light: ...: an overcast day, a clear one now (SOMVR_PRACTICE_SKY=clear)`, `light
later (10 s): weather 'S' ... (was 'O': changed|UNCHANGED; asked DEF)`; the test word `practice weather DEF`
(ChangeWeather on the game thread). Unit: test_practice (the names, the region, the modes).

**Runs** (headless Windows unless named, the VR preset in the slot's render.cfg, each 0 WARN beyond the Windows build's
two standing ones, the source save UNCHANGED): 223609 (W, SON_Day01), 225858 (Linux, SON_Day02), 230044 (W, SON_Day02),
231242 (`ChangeWeather SON_Day03` at the trip: no pause, no change), 231852 (`TEST_Day_02` at the trip: BG_Day_02),
232408 (the definitions in turn), 233052 (the shipped target at every trip, the five places: user-vs-W6.jpg - the
session's overcast marks beside the same places now).

**Open / for the headset**: the game's weather keeps changing during play (233052: SON_Day03 came back on its own ~30 s
after a trip) - the start is clear, the session may still turn overcast; holding a clear sky all session would need a
ChangeWeather in view (a 0.5-1.1 s world freeze) or the weather system's own hold - not done. The trip back's
console line `ChangeWeather <the old state>; Teleport ...` names a state: a no-op, so the clear day stays (harmless; not
changed).
Udûn's weather names are unknown: `clear` acts on Nurn's `SON_*` only.

### 7.15 som-b37-looks: no graphics setting lifts the overcast's flat look; the game's own 100 fps cap at 120 Hz; the preset by the refresh rate: `SOMVR_RENDER_BY_HZ`, `SOMVR_RENDER_SET_90`, `SOMVR_RENDER_SET_120` (2026-11-04, branch t3/som-b37-looks)
The player asked whether more mesh detail would make the walls and the ground less flat in the overcast weather
(`SON_Day03`, 7.14). Today's VR preset: Vegetation 3, Mesh 2, Shadow 2, Lighting 1 over the PC's detection (textures 3,
filtering 3, AO 3, tessellation 1).

**1. The look (headless Windows, `SON_Day03` forced at the practice and checked at each place; looks36's places, both
eyes; logs/looks37/).** Five boots, one preset each: today's; Mesh 3; Shadow 3; Lighting 2; all three at max. Sheets:
`sheet-today-vs-{mesh3,shadow3,lighting2,max}/` (today left; each view's mark nearest today's in head yaw, the
difference on the label), `grid5-left.jpg` / `grid5-left-crop.jpg` / `grid5-right-crop.jpg` (fog-ahead, dry-down,
wall-up, ground-down, meadow-feet x the five), `max-ground-down-botheyes.jpg`. **None of them gives the walls or the
paving visible relief or less flat light**: the slabs, their gaps and wet sheen, the wall's stones, the dry grass and
the meadow are the same in every column. Mesh is the level of detail at a distance (up close every preset draws the
full model); Shadow 3 only sharpens Talion's own soft shadow on the paving (the overcast keeps a faint sky shadow);
Lighting 2 adds a light shaft in the far mist. The stones' relief is their normal maps, which read only under a
directional light: the flatness is the weather (7.14), not the settings. Both eyes agree in every preset (no eye-1
shadow difference).

**2. The cost at a real frame rate (solo, `WIN_MONADO_RATE=90`: Monado's real compositor paces the game; the env file's
clear day, perf27's meadow / crowd of 8 / fire, then the fire in `SON_Day03`; scripts/looks37-perf.py, logs/looks37/
perf-table.md).** Each preset held 90.0 frames a second everywhere (frame p95 11.4-11.5 ms), except Mesh 3's first boot:
render-thread stalls of 29-35 ms in the crowd and at the fire (67 / 72 fps) that the all-max boot (Mesh 3 too) did not
have - a first-use stall, not a steady cost. GPU ms alone say little here: the tests' GPU profile (Default; the
player's sessions get LACT's VR profile) moves the shader clock (1600-2435 MHz) to hold ~65 % busy, so every preset
reads 8.3-9.7 ms. The work is GPU ms x the clock (Mcycles a frame; the layer reads the clock once a line, so a window
read at an idle moment - 500 MHz - is left out). Today's preset twice: 14.5-16.9 Mcycles, a +-10 % spread; the player's
11-03 session at 120 Hz (today's preset): 16.0 p50 / 17.1 p95 Mcycles, GPU 7.23 / 7.68 ms at 2235 MHz - the same work.
p95 work against today's (the heaviest of the four places):

| Preset | Meadow | Crowd 8 | Fire | Fire, overcast | Session p95 estimate | 90 Hz (<= 10.1 ms) | 120 Hz (<= 7.33 ms) |
|---|---|---|---|---|---|---|---|
| today's (session) | - | - | - | - | 7.68 ms (7.06 at 2430 MHz) | fits: 3.4 ms to spare (2.3 after WiVRn's ~1.1 ms) | only at a high clock, and WiVRn's ~1.1 ms on top leaves ~0.2 ms |
| today's again | 0.95 | 1.08 | 0.95 | 1.01 | - | (the spread) | |
| Mesh 3 | 1.25 (p50 1.11) | stalls (p50 1.12) | stalls (p50 1.31) | 1.53 | over 10 ms | no | no |
| Shadow 3 | 0.95 | 1.16 | 1.34 | 1.45 | 11.2 (10.3) | no | no |
| Lighting 2 | 0.93 | 1.08 | 1.00 | 1.29 | 9.9 (9.1) | fits: 1.2-2.0 ms to spare | no |
| all at max | 1.34 | 1.25 | 1.48 | (clock not read) | 11.4 (10.5) | no | no |

**3. The game caps itself at 100 frames a second.** `Render.Setting.FrameRateLimit` (registered default 2, range 0..3;
its help text says "0=30 fps, 1=60 fps, 2=Unlimited") sets the engine's `MaxFPS` (clientmgr.cpp) through the table
at FUN_140d5b3e0: 0 -> `MaxFPS 30`, 1 -> `MaxFPS 60`, **2 -> `MaxFPS 100`**, 3 -> `MaxFPS 0` (none). Every session's
render.cfg seeds 2 (the detection table), so the player's 120 Hz session (11-03 21:06) ran **exactly 100.0 frames a
second in 92 of its 95 ten-second windows** with xrWaitFrame returning at once (0.03 ms) - the display at 120, a
repeated frame every sixth; the GPU had 16 % left. Measured: the 120 Hz boot (174256, `FrameRateLimit:3`, Monado at
120) ran 120.0 frames a second at the fire (GPU 7.7 ms) - the cap is gone; in the meadow and the crowd the GPU needed
~10 ms on the tests' clock and Monado's compositor held the game at half rate (60) - WiVRn did not pace the player's
session so (it let the game run at its own 100). At 90 Hz and below the cap does not bind.

**4. Recommendation.** At 90 Hz or less: **today's preset** - it fits with 2-3 ms to spare and no richer setting looks
better (Lighting 2 fits but only adds a far light shaft). At 120 Hz: **today's preset with `FrameRateLimit:3`** - the
cap guarantees a repeated frame six times a second; lifted, the GPU keeps up in most places but no preset tested leaves
1 ms at p95 there (today's ~0.2 ms with WiVRn's share) - **90 Hz is the steadier rate on this PC**. A cheaper preset for
120 Hz (~11 % less GPU work; AO 2 or Shadow 1 the candidates) and the eye size (SOMVR_OVERSCAN 0.1 = 21 % more pixels)
were not measured.

**5. The switch (launcher only; scripts/som-vr-headset-win.sh `render_by_hz`).** `SOMVR_RENDER_BY_HZ=on` (code default
off): the session's refresh rate = the VR menu's saved `XR_REFRESH` (settings.conf, only read), else `SOMVR_XR_REFRESH`
(60..144; 0 = unknown: the preset alone); at 90 Hz or less `SOMVR_RENDER_SET_90` over `SOMVR_RENDER_SET`, over 90
`SOMVR_RENDER_SET_120` (NAME:V,...; a name in both takes the overlay's value). The preset is written at the start: a
rate changed in the menu during a session counts from the next one. som-win-session.sh takes `FrameRateLimit` in
`--render-set`. The som-b37-looks env block: `SOMVR_RENDER_BY_HZ=off` (the coordinator turns it on),
`SOMVR_RENDER_SET_120=FrameRateLimit:3`, no `_90`. The log line: `SOMVR_RENDER_BY_HZ: 120 Hz (the VR menu's saved
rate): SOMVR_RENDER_SET_120 (FrameRateLimit:3) over SOMVR_RENDER_SET`, then `the VR graphics preset: ...`. Test:
scripts/tests/test_render_by_hz.sh (20 cases, in build.sh). The Linux launcher is unchanged (its render.cfg also seeds
`FrameRateLimit 2`).

**Tools.** looks36-drive.sh `LK_SKY` (a weather forced by `practice weather DEF` until `LK_SKY_WANT`, checked after each
trip; boot with `SOMVR_PRACTICE_SKY=dry`, as `clear` undoes an overcast at the next trip) and `LK_AIM_TOL` (a view
aimed again when off; Talion's body turns in steps behind the head, so one aim lands within +-15 deg - the sheets pick
each view's mark nearest the reference: looks36-cmp.py `--match`, `--eye`, `--crop`; looks37-grid.py); perf27-drive.sh
steps `sky` / `mgo`; looks37-boots.sh (one preset boot: `pics` in a free slot or `LOOKS_SOLO=1`, `perf LABEL PRESET HZ`
in a solo); looks37-perf.py (each window's first 2048 frames, PF_WIN). `DayNoRain` lands on `SON_Day03` in 1-3 tries
(`SON_Day03_Rain`, `SON_Day01/02` between).

**Runs** (Windows, each the two standing WARN only, the source save UNCHANGED): pictures 155435 (today), 161349
(Mesh 3), 155434 (Shadow 3), 164102 (Lighting 2), 161350 (max), with the single-aim first set 154039 / 154043 /
154742 / 154743 / 154744; 155640 stayed in the main menu (the start sequence's world wait); frame times (solo, 90 Hz)
164602 + 174702 (today), 172539 (Mesh 3), 173014 (Shadow 3), 173428 (Lighting 2), 173842 (max); 174256 (120 Hz,
`FrameRateLimit:3`, the live `MaxFPS 100/0` words ran - unreadable at the half rate).

**Open / for the headset:** at 120 Hz with the cap lifted - the frames a second (`PERF` / `xr: perf` lines) and whether
WiVRn keeps the game near 120 in the meadow and the crowd or it judders more than 100 did; Mesh 3's stalls on first use
(not chased); a cheaper 120 Hz preset; holding a clear sky (7.14) is the lever for the overcast's look.
