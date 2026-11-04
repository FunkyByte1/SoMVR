# somvr on the Windows build (win/)

The Windows DLL is the same game-side code as the Linux preload (somvr.c and the files it includes, the CORE files),
compiled with `clang --target=x86_64-pc-windows-msvc` against the x64 MSVC CRT/SDK that xwin splatted into
`shared/toolchain/xwin-sdk64`, plus the files here: the xinput9_1_0 proxy and DLL entry, the exe gate, the patch
engine, the POSIX shim (`compat/`), the bridge to the Linux Vulkan layer. Design: docs/re-winport.md (3.1, 4, 7.1).

Everything Windows-side is keyed on the **analysed Steam-Windows exe** (`reference/bin/ShadowOfMordor.exe`,
sha256 f5d72578..., image base 0x140000000, the Ghidra project som-win64). **Record RVAs, never absolute
addresses** (RVA = VA - 0x140000000). The GOG exe gets its own table when it is known (7.1 in re-winport.md says how).

## 1. What each group delivers: `sites-<group>.tsv` and `offsets-<group>.tsv`

Plain TSV, one header line (exactly the column names below), `#` starts a comment line, empty cells are `-`.
Hex is `0x...`. One file pair per group (base, core, combat, world, render, ui); never edit another group's file.

### 1.1 `sites-<group>.tsv`: one row per Linux SITE name

```
name	kind	rva	ctx_rva	ctx	target_rva	prologue	confidence	how	notes
H1	call	0x50c449	0x50c436	24	0x45e5a0	15	confirmed	lone	-
D3D_COPYRESOURCE	com	ID3D11DeviceContext:47	-	-	-	-	confirmed	com	DXVK's context vtable
CP_FX	none	-	-	-	-	-	redesign	hand	wrapper inlined into 70 callers; hook the inner create
```

| column | meaning |
|---|---|
| name | the Linux `SITE("NAME", ...)` name (unique over all files; the generator checks it exists in the sources) |
| kind | `call` (E8 rel32 at rva), `jmp` (E9 rel32 tail jump at rva), `vslot` (the 8-byte slot at rva in a vtable in the exe), `entry` (a function we call or prove; rva = its first byte), `com` (a D3D11/DXGI COM slot: no RVA, see 3), `none` (no Windows site: redesign or not found) |
| rva | call/jmp: the E8/E9 byte; vslot: the slot; entry: the function. com: `Interface:slot` (e.g. `ID3D11DeviceContext:47`) |
| ctx_rva | the first byte checked at run time (<= rva; the checked range must hold rva..rva+5 for call/jmp, +8 for vslot); `-` = rva |
| ctx | the bytes checked from ctx_rva: hex (`48 8d 94 24 ...`, checked against the exe by the generator) or a count (`24`: the generator takes them from the exe). 16-24 bytes; a vslot: its neighbour slots (24-40 bytes) |
| target_rva | the call/jmp target, the slot's value (the function; the generator checks the slot holds 0x140000000 + it), entry: = rva |
| prologue | the target's first bytes: hex or a count (`15`) as for ctx; the run-time check compares them |
| confidence | `confirmed` (read side by side with the Linux function), `plausible` (matched, too small to prove), `redesign` (the hook has to change shape: notes say how), `notfound` |
| how | `str` / `nbr` / `lone` / `vt` (winport_match.py tiers), `tier2` (winport_verify.py), `hand`, `com`; add the evidence in notes |
| notes | semantic changes: register or stack reads (`rsp+0x1c` -> ?), inlining, another calling shape, the arguments' order; `-` if none |

Only `confirmed` and `plausible` rows of kind call/jmp/vslot/entry become live sites. `com` rows become run-time
COM slots (3). `none`/`redesign`/`notfound` rows are listed so we know they were looked at: at run time their SITE
is blanked (its check fails "no Windows site" and the feature it belongs to stays off, exactly as a failed check
on Linux).

### 1.2 `offsets-<group>.tsv`: every struct offset, vtable offset and global the group's code uses

```
name	kind	linux	windows	confidence	evidence	notes
HUD_EYE_PARAM	off	0x2a48	0x2a38	confirmed	L 0xa1b2c0 / W 0x140612340: movss xmm0,[rcx+0x2a38]	HUD object grew -0x10
BZ_SKILL_CAT_PP	global	0x4b56640	0x18f9308	confirmed	W 0x1407ba8c2: mov [0x1418f9308],rax	records DB category
PLAYER_VF_STEP	vt	0x358	0x350	confirmed	H1's caller: call [rax+0x350]	slot -1 (one dtor on MSVC)
```

| column | meaning |
|---|---|
| name | the name in the source's `OFS(NAME, linux)` / `GADDR(NAME, linux)` (2); unique over all files |
| kind | `off` (a field offset), `vt` (a vtable byte offset), `size` (an object or record size), `global` (an address: linux = the absolute ELF address, windows = the **RVA**), `const` (any other number that differs) |
| linux | the value in the source today (the generator checks the source still says it) |
| windows | the value on Windows (`global`: an RVA); `?` = not known yet |
| confidence | `confirmed` / `plausible` / `same` (checked: unchanged) / `unknown` |
| evidence | the matched function pair (Linux address / Windows address) and the instruction that uses it |
| notes | anything else |

## 2. How the source uses them (the Linux build is unchanged)

`somvr_win.h` (included by somvr.c before everything game-side) defines, for both targets:

- **Site tables**: `static SITE_TABLE struct site_spec g_x_sites_game[] = { SITE(...), ... };`. On Linux
  `SITE_TABLE` is `const`. On Windows it puts the table into the writable section `.somvrst`; at start (before any
  check) the DLL walks that section and rewrites every entry by its name from the generated table (base + RVA), or
  blanks it when the name has no live row. **Every site_spec table must use `SITE_TABLE`** (the generator warns about
  tables that do not: their Linux addresses would stay and fail their checks on Windows).
- **Offsets**: write a value that may differ as `OFS(NAME, 0x2a48)` (inline or in a `#define`:
  `#define HUD_EYE_PARAM OFS(HUD_EYE_PARAM, 0x2a48)`). Linux: `(0x2a48)`. Windows: `WOFS_NAME`, which the generator
  defines from the windows column, or - no row or `?` - as the Linux value, counted as **unchecked** in its report.
  It stays an integer constant (case labels, array sizes, static initialisers all work).
- **Globals**: `GADDR(NAME, 0x4b56640)`. Linux: `((uintptr_t)0x4b56640)`. Windows: `somvr_img + WGA_NAME` (RVA), and
  `0` when unknown (a read through `peek_*` then fails safely; never dereference a GADDR directly before its
  feature's sites passed). Not a constant on Windows: no static initialisers - a static struct of game addresses (the
  selftest's stand-ins) keeps its Linux values unless a `*_win_globals()` sets each field from GADDR at start
  (somvr_core_win_globals, world_win_globals, cb_globals_win, hooks_win_globals). A raw Linux address read on Windows
  is a read of whatever the Windows process has there; written, it corrupts it (found 11-02: finish's sever byte).
- **Image ranges**: `game_range(0|1, &lo, &hi)` (somvr.c) for "a function / a vtable of the game": never the Linux
  constants.
- **Code that cannot be the same**: `#ifdef _WIN32` only where the shim (`compat/`) cannot cover it - the ELF and
  `/proc` code, the GCC IPA-RA asm wrappers (on Windows a hook is a plain C function: MSVC code keeps the Win64 ABI).
  Hooks that read the caller's registers or stack slots are per-site rewrites (say so in the site row's notes).

## 3. COM slots (the 26 Idx* K_VSLOTs)

On Windows the D3D11/DXGI objects are DXVK's (Proton) - their vtables exist only at run time. A `com` row names the
interface and the slot (`ID3D11DeviceContext:47` = CopyResource; COM order has no destructor pair, so the slot is
the interface's method index, IUnknown's 3 included). The generator emits them into `g_win_com[]`; the DLL fills the
SITE (addr = &vtbl[slot], target = vtbl[slot], the slot's bytes as its context) when the game's device, immediate
context and swap chain exist, then the group's install path patches them (winport-core).

## 4. The generator: `wintables.py`

```
python3 -I win/wintables.py --check            # validate every row against the exe file and the sources
python3 -I win/wintables.py --out build/win     # + write somvr_wintables.c and somvr_winofs.h (win/build.sh does it)
```

Reads `win/sites-*.tsv`, `win/offsets-*.tsv` (`--tsv-dir` for another folder), the exe (`--exe`, default `reference/bin/ShadowOfMordor.exe`, its
sha256 must be the analysed one) and the sources (every `SITE("NAME"`, `OFS(`, `GADDR(` in somvr*.c/h). Fails on:
a duplicate name, a name that is no SITE, a bad kind, bytes that differ from the exe, a call/jmp that is no E8/E9 to
target_rva, a slot that does not hold the target, an entry whose rva != target_rva, an offset row whose linux value
is not the source's. Prints per group: live / com / none / unchecked offsets. The output is generated, never
committed (build/ is ignored).

## 5. Build

`win/build.sh` (also called by `../build.sh --win`): the tables, then the DLL `build/win/xinput9_1_0.dll`, then its
checks (exports, imports, the tables against the exe). The Linux `build.sh --check` does not need the Windows
toolchain; it runs `wintables.py --check` (python only) so a Linux change that breaks a Windows row shows at once.

## 6. Hooks on Windows (the patch engine)

- **Call / jmp / slot patches**: somvr.c's own (install_all, bo_patch_call, go_slot, ...) work unchanged through the shim;
  the stub page comes from `somvr_alloc_near` (within +-2 GB of the sites).
- **Entry trampolines and mid-function detours**: `somvr_win_hook.h` on MinHook 1.3.4 (third_party/minhook-1.3.4,
  fetched pinned by scripts/fetch_third_party.sh): `somvr_win_hook_entry(fn, hook, &orig, why, cap)` (calls of fn go to
  hook; orig = the original through MinHook's trampoline) and `somvr_win_hook_mid(at, cfn, why, cap)` (cfn gets
  `struct somvr_win_regs *`: every register, the flags kept, then the overwritten instructions run relocated). Use them
  for an inlined wrapper (hook the inner function's entry) and for a callee inlined into its caller (a detour at an
  instruction boundary no branch enters within its first 5 bytes). No hand-written instruction relocation.
- **WRAPPER hooks** (the register-passing asm wrapper, 42 of them): on Windows the wrapper is COFF and calls the C side
  with the regs in rcx (+ the 32-byte home area); `0x150(regs)` holds the entry rsp (`somvr_win_regs_rsp(r)`:
  [rsp] = the return address, argument 5 at rsp+0x28). Read the integer arguments 1-4 with `RA1(r)`..`RA4(r)` (Win64
  rcx rdx r8 r9, SysV rdi rsi rdx rcx); floats are positional on Win64 (argument k in xmm(k-1)); a member function
  returning a struct takes (this, out, ...) on MSVC, (out, this, ...) on Itanium. **Until a hook's C side reads the
  Win64 registers its SITE stays blank on Windows even with a live row**: the list `g_sysv` in somvr_win_sites.c. Port
  the C side, then remove the name there. H9 is ported (the template: RA1-RA3, DXGI_PRESENT_TEST skipped, present_tid
  0 = the layer's tag queue under DXVK's present thread); winport-hooks ported LP_GUARD_*, BOW_FA_CAMXF,
  LOCO_ROOTDELTA, SL_SHEATH_JMP, DF_IMPACT, HP_DRAW_* / HP_CUST_*, SCR_NEM_PAD (docs/re-winport.md 7.8). The macros
  are in `somvr_regs.h`: RA1..RA7 = the build's ABI (RA5-RA7 Windows: entry rsp + 0x28/0x30/0x38), RA_SYSV*/RA_WIN*
  both always (tests/test_winabi.c checks the slots on Linux).
- **Stub pages**: a hook group that writes its own `jmp [rip]` stubs takes its page from `stub_alloc(lo, hi, top)`
  (somvr.c): Linux the low-address probes as before, Windows `somvr_alloc_near`. The old per-group low-address loops
  never reach an image at 0x140000000 (every such hook was "no stub page within +-2 GB" on Windows).
- **Windows-only SITEs** (a redesign that hooks or calls something Linux has no site for): `SITE("NAME", K_ENTRY, 0, 0,
  "", 0, "")` in a `SITE_TABLE` under `#ifdef _WIN32`, outside the SITES-BEGIN/END blocks (check_sites.py never sees
  them); the row in `sites-<group>.tsv` fills it like any other (BR_LOGPRINTF, OB_RANGE_KIND/_A/_B, VG_FXCREATE in
  sites-hooks.tsv).
- **DLL-provided entries**: a game function MSVC inlined away everywhere (no Windows function at all) is provided by the
  DLL: `somvr_win_provide(name, fn)` (somvr_win_sites.c; called by somvr.c `win_provide_all` before somvr_start) points
  the K_ENTRY SITE at our function with its own first 16 bytes as the check, so the feature's checks and calls are
  unchanged. CB_ATK_BYNAME and PR_TMPL_BYNAME (the records DB lookup, `somvr_recdb.h`), CB_TIMER_RELEASE. Their rows
  stay `none` (redesign, "DONE" in the notes); the start log says `SITES provided by the DLL: 3 of 3`.
- **COM slots**: the DLL fills the rows at D3D11CreateDevice (ID3D11Device, ID3D11DeviceContext) and at the first
  Present (IDXGISwapChain, ID3D11Texture2D from the back buffer), then calls `somvr_core_win_com()` and installs H9
  (`somvr_win_late_h9`, as install_all patches a K_VSLOT). The other COM hooks need their late install the same way.
- `SOMVR_WIN_SITES=0`: every SITE blank and no COM fill - the inert boot.

## 7. The unit tests on Windows (`wintests.py`, in `win/build.sh --check`)

- **The list is the Linux one**: every `gcc ... -o .../tests/NAME SOURCES` in `../build.sh` and `../vklayer/build.sh`
  with its run line (`run "$D/build/tests/NAME" ARGS`: buildplan.sh, docs/re-xr-pipeline.md 161; the first such line
  after it, even past another test's gcc line). Add a test there in that form and it runs on Windows too; a test
  line the parser cannot read (no run line, a pipe or `||` in it, a variable left) fails the Windows check - never a
  silent skip. `python3 -I win/wintests.py --xwin X --list` prints what it found.
- **Built** for x86_64-pc-windows-msvc with the DLL's flags (`CF` of win/build.sh, `-include somvr_compat.h`) +
  `-Werror`, each source once into `build/win/tests/obj/` (clang's `.d` files: only changed ones again), one exe per
  test with `somvr_win_compat.c` and the static CRT.
- **Run** under Proton's wine (WIN_PROTON, as headless_win.sh) in a throwaway reflink copy of the harness prefix
  (`logs/winport/prefix` -> `logs/winport/tests-<ts>-<pid>/pfx`, deleted at the end; never a play prefix): no display,
  no audio driver (winepulse/winealsa/wineoss disabled, an empty XDG_RUNTIME_DIR), HOME in the copy. Absolute paths among
  the arguments become `Z:` paths. A canary (`tests/exitcode.c`, exit 7) runs first: wine must hand the code back or
  no verdict would be seen. The verdict is the exit code, as on Linux; all tests run, then any failure fails the check
  with its last 25 lines (the whole output in `build/win/tests/logs/NAME.log`). One job when the user is in VR
  (status.json), else SOMVR_JOBS compiles and runs at once (win/build.sh's runner passes its count; alone: 8).
  `SOMVR_WIN_TESTS=build` builds only (no Proton). The prefix copy takes ~0.1 s (reflinks, 2,199 files): it stays a
  fresh copy per run.
- **Linux-only** (and why): `test_layer` (the Linux layer .so under a fake Vulkan driver: dlopen of the .so files,
  -rdynamic), `hands_lvp` (lavapipe, a Linux ICD), `selftest` (an ELF -no-pie program whose stand-ins the hooks find by
  dlsym and patch; Windows hooks = MinHook + the tables, proven by the boots), the inert LD_PRELOAD check, the .so's
  manifest/NEEDED/exports and vulkaninfo. The python checks (check_sites, handmesh_gen, wintables, build_spv) are the
  same on any host.
- **What only Windows can catch** (docs/re-winport.md 7.13): an enum is always `int` on the MSVC ABI - an enumerator
  of `1u << 31` / `2147483648u` is INT_MIN there and sign-extends in a uint64_t (gcc gives such an enum an unsigned
  type). win/build.sh now has `-Werror=microsoft-enum-value`: put bit 31 (and up) in a macro. Also `long` is 32 bits;
  the CRT's `rand()` is another sequence (RAND_MAX 32767); `sinf`/`cosf` may round a step differently from glibc's -
  a test tolerance under float precision fails here first.
