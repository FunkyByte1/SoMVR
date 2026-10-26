# Static analysis setup (Shadow of Mordor)

Set up 2026-10-17. Static analysis only: nothing here touches the game install, Steam or VR.
All paths are absolute; `SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`.

## What is where

| Thing | Where |
|---|---|
| Binary copies (read-only, original mtimes) | `$SOM/reference/bin/` (`SHA256SUMS` next to them) |
| Persistent Ghidra projects (main tool) | `$SOM/reference/ghidra/<NAME>/<NAME>.gpr` |
| Ghidra's settings/cache/temp for these runs | `$SOM/reference/ghidra-home/` |
| REA CLI (pinned 6.5.0) | `/home/bara/Projects/flat2vr/shared/tools/rea/` (README there) |
| REA state (sessions, snapshots) | `$SOM/reference/rea-state/` |
| REA docs (downloaded copies, grep them) | `$SOM/reference/rea-docs/` |
| Scripts (tracked) | `$SOM/scripts/ghidra_gate.sh`, `ghidra_env.sh`, `ghidra_analyze.sh`, `ghidra_query.sh`, `ghidra_script.sh`, `ghidra/SomQuery.java`, `ghidra/SomVtables.java`, `ghidra/SomIndex.java`, `ghidra/SomRename.java` (rename identified `FUN_` functions from a TSV; log in docs/re-gameplay.md section 10) |
| Offline text indexes of som-linux (grep these first) | `$SOM/reference/index/*.tsv` (see "Offline indexes") |
| Pristine som-linux analysis (before the class labels) | `$SOM/reference/ghidra/som-linux-base/som-linux-base.gpr` |
| Per-project JVM lock (flock) | `$SOM/reference/ghidra/<NAME>.lock` |
| Logs | `$SOM/logs/` |

Ghidra 12.1.4 (Fedora rpm, `/usr/lib64/ghidra`), OpenJDK 25 (`/usr/lib/jvm/java-25-openjdk`), Node 22.22.2.

| NAME (project) | Binary | sha256 | Notes |
|---|---|---|---|
| `som-win64` | `reference/bin/ShadowOfMordor.exe` (Windows depot 241931, x64/) | `f5d72578f7e5fcf0...` | PE x64, built 2020-12-19, image base `0x140000000`, .text 20.6 MB, ~630 RTTI names |
| `som-linux` | `reference/bin/linux/ShadowOfMordor` (Feral depot 241936, bin/) | `426436a10ac2b696...` | ELF x64 non-PIE (base `0x400000`), BuildID 87ec5d6f..., .text 49.5 MB, ~7,500 RTTI names; best class map |
| `test-steam_api64` | `reference/bin/steam_api64.dll` | `18005605f0e9f60f...` | small fixture for testing the tooling (analysis 9 s) |
| `som-linux-base` | same as `som-linux` | | copy of `som-linux` right after auto-analysis, before SomVtables (2026-10-18) |
| (backup) `som-linux-pre-renames-20261018` | same | | reflink copy of `som-linux` just before the gameplay renames (2026-10-18 01:55); inner pair still named `som-linux.*` (restore by moving it back, not for opening side by side) |

Addresses are the binaries' own virtual addresses (Ghidra keeps the PE image base and the ELF vaddrs), so an
exe address from Ghidra = the address in the running game when the exe loads at its preferred base (check ASLR
at runtime before relying on that).

## The heavy-job gate (house rule, always)

Ghidra/JVM work must not run while the user is in VR or a helper is measuring frame times.
`$SOM/scripts/ghidra_gate.sh check` prints `open` / `closed: <reasons>` (exit 0 / 1). Closed when
`/run/user/1000/vr-session/status.json` is missing, older than 90 s, or has `"user": true`, or when
`/run/user/1000/vr-session/quiet` is less than 120 s old. `"test": true` alone doesn't close it.
The analysis and query scripts wait for it to open and run a watcher (`ghidra_gate.sh watch PID LOG`) that
SIGSTOPs the whole Ghidra process tree within 15 s of the gate closing and SIGCONTs it when it opens. A paused
JVM keeps its memory (up to ~25 GB RSS for the big ones). Before any one-off Ghidra or REA call of your own:
`bash $SOM/scripts/ghidra_gate.sh check || exit 1`.

## The long analysis (detached)

Started 2026-10-17 20:31 as systemd user unit `som-ghidra`: a queue that analyses `som-win64` first and then
`som-linux`, never both at once. `som-win64` finished at 20:38 (auto-analysis 369 s, project 408 MB, 74,204 functions);
`som-linux` at 21:05 (auto-analysis 1596 s, peak RSS ~18 GB, project 1.5 GB, 194,872 functions). Both exit=0,
saved. Auto-analysis did not turn the GCC typeinfo into classes; `SomVtables.java` did that afterwards (2026-10-18,
see "Class map"), so `classes`, `Class::vtable` and `Class::vf_NN` work in `som-linux` now. The unit runs with Nice=19, CPUAffinity=12-15, IOSchedulingClass=idle,
OOMScoreAdjust=500; analyzeHeadless gets `-max-cpu 4`, the JVM `-Xmx24G` (via `GHIDRA_HEADLESS_MAXMEM`, set from
`SOM_GHIDRA_MAXMEM`), Ghidra's default analyzers, and no `-analysisTimeoutPerFile` (no wall-clock deadline, so
gate pauses can't kill it). Run these OUTSIDE the Claude Bash sandbox (it has its own PID namespace):

```bash
bash $SOM/scripts/ghidra_analyze.sh status                 # unit, markers, JVM state (S/R running, T paused), CPU s, RSS, current analyzer
systemctl --user status som-ghidra --no-pager | head -5
tail -n 5 $SOM/logs/ghidra-som-win64.gate.log              # pauses/resumes
grep -E 'REPORT|Total Time|job ' $SOM/logs/ghidra-som-win64.log
cat $SOM/reference/analysis-done-som-win64                 # appears when that job ends: exit=0 ... saved
cat $SOM/reference/analysis-done                           # appears when the whole queue ends (one line per job)
systemctl --user stop som-ghidra                           # abort (works while paused); nothing is saved mid-analysis
```

Ghidra saves the program only at the end of auto-analysis: an aborted or crashed job leaves no usable project.
To redo one: `SOM_GHIDRA_OVERWRITE=1 bash $SOM/scripts/ghidra_analyze.sh start som-win64 $SOM/reference/bin/ShadowOfMordor.exe`
(a job refuses to touch an existing `.gpr` without it). Several NAME BINARY pairs = a sequential queue.
`SOM_GHIDRA_UNIT=...` picks another unit name (one unit name can only run once at a time).
Progress: headless Ghidra prints nothing between "ANALYZING all memory and code" and the final per-analyzer timing
table, so watch the JVM CPU time and the `analyzer:` line of `status` (a jcmd thread dump).

## Query an analysed binary

Grep the offline indexes first (next sections); start a JVM only for decompiles, disassembly and xrefs.

`ghidra_query.sh` opens the project `-process -noanalysis -readOnly` (nothing is ever saved), runs
`scripts/ghidra/SomQuery.java`, and writes the answer to OUTFILE. It takes the project lock (concurrent callers
queue, see "The project lock"), waits for the gate and is gate-watched,
runs nice 19 on CPUs 12-15, `-Xmx8G` (`SOM_QUERY_MAXMEM`). It works inside the sandbox. It refuses while the
project's analysis job is still running (Ghidra locks the project). Each call costs one JVM start + project open,
so batch questions in a file.

```bash
Q=$SOM/scripts/ghidra_query.sh
bash $Q som-win64 $SOM/logs/q.txt info
bash $Q som-win64 $SOM/logs/q.txt classes '(?i)camera'           # RTTI classes + their vftable labels
bash $Q som-linux $SOM/logs/q.txt classes '^Camera(Pitch|Yaw)$'                   # som-linux: CameraPitch  vtable@0356a300
bash $Q som-linux $SOM/logs/q.txt decompile CameraPitch::vf_08                     # functions by their class label
bash $Q som-win64 $SOM/logs/q.txt strings '(?i)fov|fieldofview' 50 # defined strings + the functions using them
bash $Q som-win64 $SOM/logs/q.txt decompile 0x140f10134           # function containing the address (the entry)
bash $Q som-linux $SOM/logs/q.txt xrefs 0x64f6c8 100
# batch: one command per line, '#' comments
printf '%s\n' 'functions (?i)camera 100' 'symbols vftable 20' 'disasm 0x140f10134 30' > $SOM/logs/batch.txt
bash $Q som-win64 $SOM/logs/q.txt @$SOM/logs/batch.txt
```

Commands: `info`, `func ADDR`, `decompile ADDR [SECS]`, `disasm ADDR [N]`, `xrefs ADDR [LIMIT]`,
`strings REGEX [LIMIT]`, `symbols REGEX [LIMIT]`, `functions REGEX [LIMIT]`, `classes REGEX [LIMIT]`,
`bytes ADDR [N]`. ADDR = `0x` hex, a symbol/function name, or `Namespace::name`. Regexes are Java `find()`.
Ghidra's console output is in `$SOM/logs/ghidra-query-<NAME>.log`. Add commands to `SomQuery.java` as needed
(Ghidra compiles it on the fly; the OSGi cache is under `reference/ghidra-home/`).

To browse interactively, open `$SOM/reference/ghidra/<NAME>/<NAME>.gpr` in the Ghidra GUI (`ghidra`) when no
headless job or query holds it (the GUI uses the user's normal `~/.config/ghidra`); the gate rule still applies.

## The project lock

A Ghidra project can be open in one JVM at a time (Ghidra's own lock `NAME/NAME.lock` inside the project folder makes
a second one fail). So `ghidra_query.sh`, `ghidra_script.sh` and `ghidra_analyze.sh run` first take an flock on
`$SOM/reference/ghidra/<NAME>.lock` (`som_lock` in `ghidra_env.sh`, fd 9, inherited by the JVM): a second caller
prints `waiting for the project lock ... (held: pid=.. since=.. by=<command>)`, blocks, and runs when the first one
ends. `SOM_LOCK_WAIT=SECONDS` caps the wait (then exit 4). The lock file names the last holder. flock works across the
sandbox boundary (same inode). Tested 2026-10-18: two `ghidra_query.sh som-linux` batches started 1 s apart both
succeeded, the second one after waiting 14 s. The lock is taken before the gate wait, so callers keep their order.

## Running our own scripts: ghidra_script.sh

```bash
S=$SOM/scripts/ghidra_script.sh
bash $S run   som-linux ro SomIndex.java $SOM/reference/index        # foreground, read-only (works in the sandbox)
bash $S start som-linux rw SomVtables.java $SOM/reference/index      # detached unit som-script-som-linux, WRITES + saves
bash $S status som-linux                                             # unit, done marker, lock holder, log tail
cat $SOM/logs/ghidra-script-som-linux.done                           # exit=0 ... secs=13 saved | read-only | check <log>
```

`MODE` = `ro` (`-readOnly`) or `rw` (the script's changes are saved). Always `-process -noanalysis`, lock, gate wait +
watch, nice 19, CPUs 12-15, `-max-cpu 4`, `-Xmx` from `SOM_SCRIPT_MAXMEM` (default 16G rw / 8G ro, capped at 24G).
Console log `logs/ghidra-script-NAME.log` (appended), gate log `logs/ghidra-script-NAME.gate.log`. `start` and
`status` need the real PID namespace (outside the sandbox). Back the project up before an `rw` run.

## Class map (som-linux): SomVtables.java

The ELF links libstdc++ statically and is non-PIE, so all RTTI is plain absolute qwords. `SomVtables.java`
(run 2026-10-18 `rw` on `som-linux`; 13 s wall in total: model 1.2 s, labels 6 s, save included) finds:

- the `__cxxabiv1` typeinfo classes by their name strings, then every typeinfo object by its vptr
  (`__class_type_info` vtable+16 etc.): **9,641 class typeinfos** (1,802 `class` = no bases, 6,901 `si` = one public
  base at offset 0, 938 `vmi` = several/virtual/non-public bases) + 174 pointer/enum/function/fundamental ones;
- every vtable = an aligned qword pointing at a class typeinfo, preceded by a plausible offset-to-top (0 or -8k), not
  inside a typeinfo object: **9,224 vtables** (7,311 primary, **1,913 secondary** in 1,211 classes), 7,138 classes with
  a vtable (2,503 without: lambdas/functors/structs used only for typeid, exceptions or `std::function`);
- slots: qwords that are 0 or a function entry, until the next vtable's offset-to-top+typeinfo, a typeinfo, a pointer
  into the middle of a function (a switch table follows; 44 cases), or anything else. Trailing zero slots are dropped.
  **111,438 slots, 50,143 distinct functions.**

Labels (source ANALYSIS; nothing that already had a non-default name was renamed; re-running is idempotent):

| Label | Where | Count |
|---|---|---|
| `Class::typeinfo` (+ EOL comment with the bases) | the typeinfo object | 9,641 |
| `Class::typeinfo_name` | its mangled name string | 9,641 |
| `Class::vtable` | primary vtable **at its address point** (= what an object's vptr holds = `_ZTV`+16) | 7,311 |
| `Class::vtable_0x48` | secondary vtable of the base subobject at offset 0x48 | 1,913 |
| `Class::vf_NN` | function in slot NN (decimal) of the primary vtable, address = vtable + 8*NN | 44,061 |
| `Class::vf_0x48_NN` | slot NN of the secondary vtable 0x48 (mostly `sub rdi,0x48; jmp` thunks) | 5,668 |
| `__cxa_pure_virtual` 0x32a1270, `__cxa_deleted_virtual` 0x32a1290 | by their message strings | 2 |

9,481 Ghidra classes + 824 plain namespaces were created (139 namespaces became classes). Class names are the GNU
demangler's (bundled `demangler_gnu_v2_41`), split template-aware; spaces become `_`, e.g.
`(anonymous_namespace)::Foo`. Order: bases before derived (inheritance depth), primary before secondary vtables, so a
function inherited unchanged is named after the class that introduced it (CameraPitch slots 2-6 =
`CAnimationMgr::CameraModifier::vf_02..06`). A function in several vtables keeps that first name; `vfuncs.tsv` lists
every slot. 7,899 functions sit in more than one class's vtables; for 576 of them no class in the set is a base of all
the others (identical-code-folded stubs, `__cxa_pure_virtual` in 5,710 slots): `vfuncs.tsv` notes `no common owner`.
Limits: 21 class names belong to two typeinfos (anonymous namespaces in two .cpp files: same namespace, two
`typeinfo` labels); with virtual inheritance (CryptoPP, std iostreams; 38 classes) construction vtables carry the
base's typeinfo, so those classes list several "primary" vtables; non-virtual member functions stay `FUN_` (only
constructors/destructors can be found from the `vtable` xrefs). Restore the pristine analysis:
`rm -rf $SOM/reference/ghidra/som-linux && cp -a $SOM/reference/ghidra/som-linux-base $SOM/reference/ghidra/som-linux`
then rename `som-linux/som-linux-base.{gpr,rep}` back to `som-linux.{gpr,rep}` (no JVM may hold either).
Ghidra's RecoverClassesFromRTTIScript was not tried (not needed for this map; it would be a long run on a copy).

## Offline indexes (som-linux, `$SOM/reference/index/`, git-ignored)

Tab-separated, one header line, one record per line, addresses `0x` lower-case hex. Exported 2026-10-18 01:09-01:13
by read-only runs after the labelling (SomVtables `dry` re-export = byte-identical to the write run's TSVs).

| File | Columns | Rows | Size |
|---|---|---|---|
| `classes.tsv` | class, typeinfo, kind (class/si/vmi), bases (`;`-separated: `Name`, `Name@0xOFF`, `nonpublic `, `virtual Name@vbo-0x18`), vtables (`;`: `0xAP` primary, `0xAP@0xOFF` secondary), vfuncs (primary slot count, `-` = no vtable), mangled, ghidra_ns | 9,641 | 1.9 MB |
| `vfuncs.tsv` | class, vtable, subobject (0x0 = primary), slot, function, name (current Ghidra name), note (`null`, `thunk`, `in N slots`, `no common owner`) | 111,438 | 12.4 MB |
| `functions.tsv` | addr, name, size (body bytes), callers (distinct calling functions), callees (distinct called functions) | 193,908 | 7.4 MB |
| `strings_xrefs.tsv` | addr, nrefs, text (<= 200 chars; `\t \n \r \\` escaped), refs (`0xENTRY:name`, max 20, then `+N more`; `0xFROM:-` = not in a function) | 64,592 | 4.1 MB |
| `calls.tsv` | caller, callee (distinct pairs of direct call references; calls through a vptr are not in it) | 365,829 | 7.1 MB |

Only strings Ghidra defined as data are in `strings_xrefs.tsv` (14,030 of them have no reference); for every raw
string use `$SOM/reference/som-linux.strings.txt`. Re-export after any `rw` run that renames things (~35 s):

```bash
S=$SOM/scripts/ghidra_script.sh; I=$SOM/reference/index
bash $S run som-linux ro SomVtables.java $I dry     # classes.tsv, vfuncs.tsv (dry = change nothing)
bash $S run som-linux ro SomIndex.java $I           # functions.tsv, calls.tsv, strings_xrefs.tsv (nocalls = skip calls.tsv)
```

### Worked examples

```bash
I=$SOM/reference/index; Q=$SOM/scripts/ghidra_query.sh
# 1. class -> its vtable and virtual functions (own ones carry the class name, inherited ones the base's)
grep -P '^CameraPitch\t' $I/classes.tsv | cut -f1-6     # si, base CAnimationMgr::CameraModifier, vtable 0x356a300, 10
grep -P '^CameraPitch\t' $I/vfuncs.tsv | cut -f3-7      # slots 0,1,7,8,9 = CameraPitch::vf_NN; 2-6 = CameraModifier's
bash $Q som-linux $SOM/logs/q.txt decompile CameraPitch::vf_08    # the per-frame pitch math: SQRT, sinf
# 2. all classes deriving from a base (here the 51 camera modifiers: CameraYaw, CameraCollide, CameraFX, ...)
awk -F'\t' '$4 ~ /(^|;)CAnimationMgr::CameraModifier(@|;|$)/ {print $1}' $I/classes.tsv
# 3. string -> function -> decompile
grep -F 'FOVYDegOverride' $I/strings_xrefs.tsv          # "+Camera.FOVYDegOverride %d" <- CShadowOfMordorGame::vf_96
bash $Q som-linux $SOM/logs/q.txt decompile CShadowOfMordorGame::vf_96  # writes feralArguments.cfg from the Feral prefs
# 4. multiple inheritance: secondary vtables and their thunks
grep -P '^CShadowOfMordorGame\t' $I/classes.tsv | cut -f4-6     # CLinuxGame; 0x3342b50;0x3342e78@0x48;...@0x108; 99
grep -P '^CShadowOfMordorGame\t0x3342e78\t' $I/vfuncs.tsv | head -3   # vf_0x48_00 = "sub rdi,0x48; jmp vf_00"
# 5. function address -> which vtable slots hold it, and its direct callers
grep -P '\t0x6501b0\t' $I/vfuncs.tsv | cut -f1-6        # CShadowOfMordorGame slot 0 (the destructor)
grep -P '\t0x6501b0$' $I/calls.tsv | head               # caller addresses; names via: grep -P '^0x....\t' $I/functions.tsv
```

## REA (small targets and quick looks)

REA 6.5.0 works with the Ghidra provider (`rea doctor --provider ghidra`: healthy) but **does not persist
analysis between CLI calls**: every Ghidra-backed command imports the file into a fresh temp project
(`-import ... -readOnly -deleteProject`) and re-runs full auto-analysis. `--snapshot` caches only
`analyze`/`inspect` results, not `decompile`/`xrefs`. It can't open our persistent projects. Use it for small
DLLs (~15 s per call), not for the game binaries. It must run outside the sandbox (its bridge uses a Unix
socket the sandbox refuses).

```bash
bash $SOM/scripts/ghidra_gate.sh check || exit 1
export REA_STATE_DIR=$SOM/reference/rea-state
R=/home/bara/Projects/flat2vr/shared/tools/rea/rea.sh
nice -n 19 taskset -c 12-15 $R inspect   $SOM/reference/bin/bink2w64.dll --provider ghidra --format json
nice -n 19 taskset -c 12-15 $R decompile $SOM/reference/bin/steam_api64.dll SteamAPI_Init --provider ghidra --format json
nice -n 19 taskset -c 12-15 $R xrefs     $SOM/reference/bin/steam_api64.dll FUN_3b4046b0 --provider ghidra --format json
```

The output is one Evidence JSON object; the answer is in `.normalized_result`.
REA can keep one analysed Program alive only inside a long-running MCP server session (stdio). That would
need an MCP client process of our own and was not set up (CLI only, no agent registration).

## Traps

- The sandbox: game files under `~/.local/share/Steam` read as missing; Unix sockets are refused (REA);
  `systemctl --user`, `jcmd`, signals to the unit's processes need the real PID namespace. Run those unsandboxed.
- `shared/tools-venv/bin/python` doesn't resolve inside the sandbox (its interpreter lives outside); use
  `objdump`/`readelf` or run it unsandboxed.
- Editing `ghidra_analyze.sh` while `som-ghidra` runs: the running shells keep the old (replaced) file, but the
  queue's next job starts a fresh `bash ghidra_analyze.sh run` from the current file, so keep it valid. Replace
  files atomically (editor/`mv`); never append to it in place.
- Default analysis tries the PDB analyzer: no PDB and no symbol server are configured, so it only logs a hint.
- The decompiler prints a class label without its namespace inside that class's functions (`*param_1 = &vtable;`
  in `CPlayerCamera::vf_00` is `CPlayerCamera::vtable`).
- `vf_NN` is a slot number, not a meaning: the names say where a function sits, not what it does. Slot 0/1 of a
  primary vtable are usually the complete/deleting destructors.
