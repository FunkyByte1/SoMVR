# Shadow of Mordor (Feral Linux build): skipping the pre-game window and the unsupported-distro alerts

Static only (2026-10-19, late): annotated `objdump` of `reference/bin/linux/ShadowOfMordor` (BuildID `87ec5d6f...`),
`reference/index/*.tsv`, the install's `ShadowOfMordor.sh`, `config/`, `share/specifications.xml`,
`share/specification_stock.xml`, `share/FeralUI/`, `bin/FeralLinuxMessage` strings (all read-only), the seed
`logs/boot-check-20261018-003728/preferences.seeded` and run `logs/boot-check-20261019-231255/` (prefs, game.log, shots).
Nothing launched; the install, the user's prefs, `mod/*` and `scripts/boot_check.sh` were not touched. A Ghidra batch was
prepared but not run (the heavy-job gate stayed closed: a test was running), so no decompiles: everything below is from
disassembly.
Scratch (git-ignored): `reference/work-launcher/` (`dis.py` annotated disassembly, `riprefs.py` rel32 xref scan,
`dis_*.txt`, `b1.txt` a ready Ghidra batch: `scripts/ghidra_query.sh som-linux OUT @.../b1.txt`).
Conventions as in `re-resolution.md`: addresses = runtime VAs (non-PIE); file offset = VA - 0x400000 for code and rodata;
**[high]** read in code/files, **[med]** inferred, **[low]** guess, **RT** = needs a boot.

## 0. Summary

| Item | Answer | Conf. |
|---|---|---|
| Skip the pre-game window (PGOW) | Command-line switch **`no-feral-options`** (any number of leading dashes: `--no-feral-options`). Skips it unconditionally, even with queued alerts, even in Big Picture. No prefs change. | high (code), RT |
| Prefs route | In the **real** setup key `HKCU\Software\Monolith Productions\Middle Earth Shadow of Mordor\Setup`: `GameOptionsDialogShown 1` + `GameOptionsDialogShouldShow 0`, *and* no alert queued before the decision (a queued alert forces the window) | high |
| Why boot_check still showed it | It seeds `DozeGameSetup` and `Software\Feral Interactive\...\Setup` (dead keys). The real key in its seed file has `GameOptionsDialogShouldShow 1` (the window's "Always show this dialog" box, ticked by default) and the distro alert is queued anyway | high |
| What triggers the distro alert | spec `LnxDistributionUnsupported` in `share/specifications.xml` (and the downloaded copy in the prefs `cache/`): fails unless `/etc/os-release` `ID=ubuntu` with `VERSION_ID` >= 18 or `ID=steamos` >= 2. Nobara 44 -> alert. Hard-coded path, no env override | high |
| Suppress it (pref) | `Setup\SkipOSWarnings = 1` (the spec's `<ignore_if_set>`) or `Setup\SpecificationAlerts\LnxDistributionUnsupported = 1` (what the alert's "Do not show again" checkbox writes). Either one silences both appearances | high (code + XML), RT |
| Suppress it (no prefs file edit) | none without a write: the only command-line way is Feral's `-setreg_` switch, which writes the same value into the prefs through the game | med, RT |
| Why it shows twice | first through the PGOW's CEF alert manager (with the checkbox), then, after PLAY, through the SDL fallback = `bin/FeralLinuxMessage` (no checkbox). Unchecked box = `LnxDistributionUnsupported 0` written back, so it never goes away | high (paths), med (why the 2nd pass) |
| What the PGOW sets that the game needs | nothing at launch time: `feralArguments.cfg` is written by the game from the prefs (`ScreenW/ScreenH/FullScreen` under the GPU key); the seed already has 1920x1080 fullscreen. Without those values the game uses the desktop size | high (args file), med (default) |

## 1. The launch flow and the skip decision

`CLinuxGame::vf_90` (`0x68a830`, the launch routine) **[high]**:

| Address | What |
|---|---|
| `0x68a859` | `CrashCounter` / "Previous run didn't quit gracefully" (crash reporter) |
| `0x68ad4b` | `FUN_00662380` (steering-wheel range setup) |
| `0x68ad56` | `skip = this->vtbl[0x2b0/8]()` = `CFeralGame::vf_86` (`0x657970`) |
| `0x68ad61` | skip -> `0x68aed8`: `FUN_00684970(1)` (analytics event `"PGOW_was_skipped"` `0x3348ffc`; off with `AllowSendUsageData 0`), `FUN_00680f90(alertmgr)` (`+0x59 = 1`: queued alerts now go to the SDL path), continue the launch |
| `0x68ad88`..`0x68adb9` | no skip: `new CCefGameOptionsDialogFeralUI` (`FUN_00680300`), `vf_02` = DoDialog (CEF, `FeralUI/PGOW/Core/feralUI.html`); result at `0x68ae66`; Quit -> return -2 |

`CFeralGame::vf_86` (`0x657970`, 566 B), returns 1 = skip **[high]**:

- key = `vtbl[10]('kger')` unless `vf_10` is not overridden (then `"DozeGameSetup"` `0x33447f1`). SoM overrides it:
  `CShadowOfMordorGame::vf_10` (`0x650ba0`) answers `'manc'` = `"Monolith Productions"` (`0x3341e48`), `'manp'` =
  `"Middle Earth Shadow of Mordor"` (`0x3341e74`), and `'kger'` falls to `CFeralGameLegacy::vf_10` (`0x679dd0`) ->
  `FUN_00679c80` formats `"Software\%s\%s\Setup"` (`0x33486b9`). The game writes all its own values there (run prefs).
- `force` = modifier key held at start (`FUN_01fd99e0`, key states 0x1c/0x1d; the window's "hold down the ... key" tip)
  **[med]** || `GameOptionsDialogForceDialog` (`0x3347eb0`) || arg `feral-options` (`g_4619df0`) || (feature
  `UseFeralUIAlerts` (`FUN_006c2960`, FeralInfo.json says true) && alert queue not empty (`FUN_006810f0` singleton,
  `FUN_00680fb0`: deque start != end)).
- `noopts` = arg `no-feral-options` (`g_4619e00`). Both NStrings from `_INIT_6` (`0x553709`/`0x553732`): `"no-feral-options"`
  at `0x334558a` (offset `0x2f4558a`), `"feral-options"` = the same bytes from `0x334558d`.
- `bp` = Steam Big Picture (`FUN_006c14a0`: `getenv("SteamTenfoot")` `0x334ea03`) **[med]**.
- `skip = noopts || (!force && ((Shown && ShouldShow present && !ShouldShow) || (bp && !ShouldShowBigPicture)))`
  (`Shown` `0x3347f30`, `ShouldShow` `0x3347f10`, `ShouldShowBigPicture` `0x3347ee0`; bp sets `*(u8*)0x4b4d690 = 1`).

Argument matching (`FUN_02dcbe10` over the list from `FUN_02dcaf30`): both the wanted name and every argv entry go through
`FUN_02dcb260` = split at `g_4c61350`, then strip the regex `"-*"` (`0x3818d10`) from the front, then compare. So
`no-feral-options`, `-no-feral-options` and `--no-feral-options` all match **[high]** (exact compare function `FUN_02dfc6e0`
not read: case sensitivity unknown, use lower case). `CFeralGame::vf_47` (`0x65aa1d`) registers both names in a set at
`0x4708b88` (`FUN_01b2eae0`), probably Feral's own switches kept from the engine **[med]**. Feral's `ShadowOfMordor.sh`
passes every argument after its own `--fresh-prefs/--system-asound/--version` to the binary unchanged **[high]**.

The window's footer checkbox "Always show this dialog" is bound to `MainPregameUIShown` (`feralUI.html` line ~92) and
`MainDialogControlHandler::vf_03/06/07` (`0x69d050`, `0x69c0c0`, `0x69bd20`) read/write `GameOptionsDialogShouldShow`
**[med: binding seen, handler not decompiled]**. The seed has it at 1 because attempt 1 never unticked it.

## 2. The unsupported-distro alert

**Trigger [high].** `FUN_006d09b0` (the machine description for specs) opens `"/etc/os-release"` (`0x334f4b0`), reads
`"ID="` and `"VERSION_ID="` into `OS_distribution`, `OS_distribution_version(_major/_minor/_bug_fix)`; `/etc/lsb-release`
is only read by the breakpad minidump writer (`FUN_0305e220`, next to `/proc/cpuinfo`, `cmdline`, `environ`, `auxv`). Spec
(`share/specifications.xml`, the same block in the downloaded `cache/specifications.xml`, etag `1291-5985099ba36d6`;
abridged):

```xml
<uuid>LnxDistributionUnsupported</uuid>
<conditions><not>ID is ubuntu, major >= 18</not><not>ID is steamos, major >= 2</not></conditions>
<alert stock_id="LnxDistributionUnsupported">...</alert>
<ignore_if_set>SkipOSWarnings</ignore_if_set>
```

`/etc/os-release` here: `ID=nobara`, `VERSION_ID=44` -> both `<not>` pass -> alert. `FUN_0202fa10` reads os-release too
and tests `"steamos"` (other SteamOS behaviour): faking the distro is not advised.

**Processing, `FUN_006d5fb0(spec, machine, phase, test_only)` [high, field meanings med]:**
1. `spec+0x10` (the `ignore_if_set` name) non-empty and `FUN_006bb880(props, name)` true -> spec skipped. `FUN_006bb880`
   reads a flag at `HKCU\<setup key>\<name>` (`CFeralGameProperties` singleton `FUN_006bbd30`, path split in
   `FUN_01b1e060`, read `FUN_01b18860`). So **`Setup\SkipOSWarnings = 1` skips the distro spec** (and only it: no other spec
   has `ignore_if_set`).
2. conditions (`FUN_006e1cb0` per condition) fail -> nothing.
3. spec with a uuid and an alert: `FUN_006bb880("SpecificationAlerts\" + uuid)` (`0x334ed0e`) true -> alert not shown.
   Else `"SeenSpecificationAlertUUID" + uuid` is consulted (regkey specs, e.g. `SettingsClass 6GB`).
4. the alert is shown only in phase 0 (`0x6d6106`): `FUN_02004d80(alert)` -> the installed alert manager
   (`*0x4b4d338`); the default `CFeralAlertManager` path `FUN_02004b50` -> `FUN_020492e0` = DoSDLDialog -> exec
   `FeralLinuxMessage` (`0x386b170`). `FUN_006c2680(alert)` = the alert's quit flag.

**The checkbox [high, from `share/specification_stock.xml`].** The stock alert has `<checkbox stock_id="doNotNotifyCheckbox"/>`:
`<action id="setting"><param name="name">#{uuid}</param><param name="key">#{setupregistrykey}\SpecificationAlerts</param>`
("setting is always performed when you close the dialogue") -> writes `Setup\SpecificationAlerts\LnxDistributionUnsupported`
= checkbox state. The seed's `0` is the unticked box from attempt 1. The SDL `FeralLinuxMessage` box has no checkbox
(shot `03-alert.png`), so only the PGOW copy can set it.

**Two appearances [med].** Run `boot-check-20261019-231255`: the alert in the PGOW at ~30 s (CEF, checkbox), then 8-10 s
after PLAY again via `FeralLinuxMessage` (Return = Continue Anyway). Spec alerts are displayed only from `FUN_006d5fb0`
(the sole caller of `FUN_006c2680`), so steps 1/3 suppress both. Which second pass re-queues it was not traced.

**Neighbour: `LnxCPUGovernorSubOptimal`** (uuid, checkbox, no `ignore_if_set`): fires when any CPU's governor is not
`performance`. Today the box runs `performance` x16 (`power.log`), so it is silent; seed
`SpecificationAlerts\LnxCPUGovernorSubOptimal 1` anyway so a governor change cannot add a blocking box.

## 3. `-setreg_`: Feral's command-line registry write [med]

`CShadowOfMordorGame::vf_47` (`0x651190`) -> `CLinuxGame::vf_47` (`0x689340`) -> `CFeralGame::vf_47` (`0x65a900`; call at
`0x65aa10`) -> `FUN_0068ee20`: every argv entry starting with `"-setreg_"` (`g_461abc0`, `0x334a353`)
goes to `FUN_0068e820`, which reads quoted fields from the **same** argv word: `key="` (`0x334a35c`; missing = the setup key
via `vf_10('kger')`, aliases `d3d` = `Software\IndirectX\Direct3D\Config`, `ddraw` = `...\DirectDraw\Config`, `app`),
`name="` (`0x334f2c1`), `value="` (`0x334a362`), `type="` (`0x334a36a`; keywords `double integer string data`, `data` =
hex via `"%2hhx"`), and writes HKCU (`FUN_01b18e40` / `FUN_01b18eb0` / `FUN_01b1a2f0` with REG_BINARY 3). The entries are
then removed from the argument list (`FUN_0068f7a0`, `FUN_02dcbf50`). Example (one argv word):

    '-setreg_name="SkipOSWarnings"value="1"type="integer"'

The value lands in the preferences file like any game write (persistent, flat play too). Unknown: the type-keyword to
code mapping (`FUN_0068e5d0`) and whether `vf_47` runs before the spec pass (if not, the first boot still shows the alert
and later boots don't). RT.

## 4. What the PGOW provides that the game needs

| Thing | Source when the PGOW is skipped | Conf. |
|---|---|---|
| resolution / window mode | `CShadowOfMordorGame::vf_96` (`0x651290`) writes `VFS/Temp/feralArguments.cfg` (`+Windowed`, `+ScreenWidth/Height`, `+WindowWidth/Height`) from `CFeralGame::vf_14` (`0x65ad20`: GPU subkey `Radeon RX 6800 XT Vulkan RADV (0,d,0,0)\ScreenW/ScreenH/FullScreen`). Seed: 1920 x 1080, FullScreen 1 (re-resolution.md 1.1) | high |
| no `ScreenW/H` (fresh prefs) | 0 -> `FUN_00740470` uses the desktop size | med |
| graphics quality | the game's own first-run auto-detection (`AutoDetectedGPURating`, `*Quality` values written by the game in run 1019) and `AppData/WB Games/Shadow of Mordor/settings.cfg`/`render.cfg` | high |
| spec regkeys (`PerfRating`, `CardType`, `SystemType`, `SeenSpecificationAlertUUIDSettingsClass *`) | the spec pass, not the window | high |
| first-run prompts (`AllowSendUsageData`, `EULAPresented`, `DontShowFeralNetWarningAgain`, `SoftwareUpdatedAskedUser`) | already in the seed; on fresh prefs with the window skipped, whether the usage prompt appears through SDL is unknown | high (seed), RT (fresh) |
| FOV / DLC check boxes (`FovSettingEnabled`, `DisableDLCCheck`) | prefs; the seed keeps FOV off | high |

Side effect of skipping: the CEF window is what writes `bin/debug.log` and `bin/blob_storage/` into the install
(NOTES.md trap); no window, no writes **[med]**.

## 5. Proposals

### 5.1 boot_check (owner: whoever holds `scripts/boot_check.sh`)

1. **Seed** (one value set; tested offline on a temp copy, XML parses, 5 lines change):

       sed -e '/name="GameOptionsDialogShouldShow\(BigPicture\)\{0,1\}"/s/>1</>0</' \
           -e '/name="LnxDistributionUnsupported"/s/>0</>1</' \
           -e '/name="LnxDistributionUnsupported"/i\                            <value name="LnxCPUGovernorSubOptimal" type="integer">1</value>' \
           -e '/name="ShowTheHideDockCheckbox"/a\                        <value name="SkipOSWarnings" type="integer">1</value>' \
           logs/boot-check-20261018-003728/preferences.seeded > NEW.preferences

   and in `setup_vals`/the default seed add the real key `Software\Monolith Productions\Middle Earth Shadow of Mordor\Setup`
   (the `DozeGameSetup` and `Feral Interactive` blocks can go).
2. **Argument**: append `--no-feral-options` after `"$BIN"` in both `GARGV` forms (lines ~337 and ~347). The dry-run
   save-dir probe (line ~429) drops the last element assuming it is `$BIN`: drop the argument too.
3. **Cmds** (`somvr-run4.cmd` and descendants): delete steps 1-3 (wait 30 / Continue Anyway click / PLAY click / alert
   Return); start at step 4 with a shot at ~10 s (GUESS: legal text ~10-20 s after start instead of ~60 s).
4. Optional, isolated mode only (bwrap): not recommended to bind a fake `/etc/os-release`; the prefs do it.

### 5.2 `scripts/som-vr.sh` (owner: the launcher; needs the coordinator's OK for the prefs part)

- VR mode only (flat stays byte-for-byte untouched): append `--no-feral-options` before the exec, e.g.
  `set -- "$@" --no-feral-options` right before the `launch.txt` block (so `launch.txt` logs it); update the header
  ("Steam's command line is passed on untouched") and `scripts/tests/test_som_vr.sh`. No prefs write. The user's real
  `~/.local/share/feral-interactive` does not exist yet (`realhome-feral.before`: absent), so the first VR start is a
  first run: window skipped (noopts wins), resolution = desktop size **[med]**.
- That still leaves the SDL distro box on the desktop while the user wears the headset (the game blocks on it). Options,
  in order of preference: (a) also pass `'-setreg_name="SkipOSWarnings"value="1"type="integer"'` in VR mode (the game writes
  one value into the user's prefs; harmless, also silences flat play; RT for syntax and timing); (b) the user ticks
  "Do not show again" once in a flat start; (c) the launcher edits the prefs XML (not recommended: the game rewrites the
  file). (a) and (b) change the user's real prefs: ask first.

## 6. Runtime test plan (one desktop boot, no headset needed)

Boot A: `boot_check.sh --steam --save-copy --seed-prefs NEW.preferences` + `--no-feral-options` + a short cmds file
(shots every 5 s for 60 s, then run 4's Escapes / main menu / quit). Pass if:

- no window: `game.log` has no `gpu_process_transport_factory.cc ... Lost UI shared context` and no `INFO:CONSOLE` lines;
  no `FeralCefSubprocess` in the process tree; boot_check's install check finds no new `bin/debug.log`/`bin/blob_storage`;
- no alert: no `FeralLinuxMessage` child, `ptrace.log` shows no SIGCHLD stop in the first minute;
- the first shots show the legal/logo screen, the main menu without any click or Return;
- `RUN/home/.../VFS/Temp/feralArguments.cfg` still `+Windowed 0 +ScreenHeight 1080 +ScreenWidth 1920`;
- the prefs the game wrote keep `GameOptionsDialogShouldShow 0`, `SkipOSWarnings 1`,
  `SpecificationAlerts\LnxDistributionUnsupported 1`.

If boots are cheap, split the causes: B = new seed without the argument (expect: no window, no alert -> the prefs route
alone works); C = the argument with the old seed (expect: no window, one SDL box -> confirms the alert needs the pref).
D (for 5.2 a): old seed + `--no-feral-options` + the `-setreg_` word; expect at most one SDL box on the first boot and
`SkipOSWarnings 1` in the written prefs; a second boot with those prefs shows none.

## 7. Open (the Ghidra batch `reference/work-launcher/b1.txt`, or RT)

- Where the second spec pass after PLAY is called from, and whether `-setreg_` (`vf_47`) runs before the first pass.
- `FUN_0068e5d0` type mapping; case sensitivity of `FUN_02dfc6e0`; the modifier keys in `FUN_01fd99e0`.
- Fresh prefs + window skipped: does the usage-statistics prompt appear (SDL) or get skipped.

## 8. The boot movies (build 6, 2026-10-23; static RE: reference/work-launcher/splash/b*.txt = the batches, o*.txt = their output)

The user: "make it skip all the launch movies? I just had to sit there waiting 30 seconds for all the company logos".
- **What plays:** five movies, 4.0, 4.0, 4.6, 4.4 and 13.4 s (the Feral logo, the WB / middleware / legal logos, the
  title), from +24 s to +55 s of game time (logs/boot-check-20261023-160221); the main menu right after the fifth.
- **No switch skips them.** `DisableSplashVideos 1` only makes the game write `+SkipSplashScreens 1` into
  feralArguments.cfg (0x6518d4 / 0x33421e8) and no cvar of that name exists in this build. Escape as each movie starts
  skips none (the run above): a splash movie takes a skip (FUN_00c29a60, the video record's bool +0xf8, 0x1f units played)
  only while the gate FUN_00a1a790 is true, and that needs the loading done (the profile jobs, ui+0x30f0).
- **Where they come from:** the menu state machine (CMenuUIStateMachine::vf_03 0xa237e0; states LoadingMenuUI,
  LegalLeadIn, SplashScreen, LoadingCharacterBundles, SignIn ... MainMenu). Its init FUN_00a1f7d0 (one caller: the call
  at 0xa21107 in FUN_00a210f0) builds the SplashScreen queue (Client.SplashMovies' SplashMovieSequence, sm+0x68..; the
  SplashMoviesBundle load into sm+0x60) only when the byte 0x46b2061 is 0 (`cmp byte [0x46b2061],0` at 0xa1fab0).
  That byte is the game's own dead "no splash" switch: only `_INIT_277` writes it (0), only 0xa1fab0 reads it.
  SplashScreen's update FUN_00a1b750 plays the queue front (FUN_00a1b590 -> FUN_00a10930 -> FUN_00c2bc00); each stop
  (event 0x10) pops the front without an empty check, so cutting a movie short or emptying the queue mid-movie is unsafe.
- **SOMVR_SKIP_SPLASH=1** (somvr_session_game.c, SS_SPLASH): the call at 0xa21107 goes through a stub page to
  somvr_w_splash, which sets the byte to 1 for that call and puts it back: the queue is never built, the bundle never
  loaded, no movie starts. SS_SPLASH_FLAG is the read-only proof of the cmp. Fail soft: a mismatch = the movies play.
  The SplashScreen state still waits for the front-end bundle and the profile (some black seconds, the headset's
  loading space).
- **Runtime** (logs/boot-check-20261023-165133, headless): "the menu's init (call 1): no boot movies queued (the byte
  was 0, 1 for the call, 0 again)"; no video at all; SplashScreen (menus=3) from +23.5 s, the next state +30 s, the
  main menu +31 s instead of +55 s (the same main menu picture), the world +83 s instead of +102 s.
