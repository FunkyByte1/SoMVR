# Run 5: the Vulkan layer under Monado's simulated HMD (headless, no headset, no user) - plan

Written 2026-10-19, offline. Nothing was launched for it: no game, no monado-service, no OpenXR app. The goal is
re-xr-pipeline.md section 5 items 1-3, 5, 8 and the log half of 4.6.5 check 2, with `mod/somvr` (layout B, real XR views)
and `VK_LAYER_SOMVR_openxr` in the game for the first time. This is also the layer's first boot inside the game:
`mod/vklayer` has only ever run under `vulkaninfo` (NOTES.md "Untested against WiVRn/Monado and the GPU").
Command file: `scripts/cmds/somvr-run5.cmd`.

## 1. Monado on this machine

**Version.** `monado-25.1.0^20260820git01c1f6b-1.fc44` (Fedora): upstream 25.1.0 (CHANGELOG: 2025-12-09) plus git
`01c1f6b23ab73c1459c8f4cfd2eb16d5142214c2` (2026-08-20), from the build paths in the binaries. Files:
`/usr/bin/monado-service`, `monado-cli`, `monado-ctl`, `monado-gui`, `/usr/lib64/monado/libopenxr_monado.so` and
`libmonado.so.25`, `/usr/share/openxr/1/openxr_monado.json`, and the systemd user units `monado.socket` + `monado.service`.
There is no man page, and `monado-service` has no `--help` (no usage string), so it was not run. `monado-cli` (no
arguments) prints only its usage: info, test, probe, lighthouse, calibrate, calib-dump, slambatch. `monado-ctl`: `-c`
recentre local spaces, `-f/-p/-i <id>`, brightness.

The source is not on this machine, and gitlab.freedesktop.org refuses fetches (Anubis). The facts below come from
two places: strings of the installed binaries (the env names and messages), and Debian's Monado **25.0.1** source
(sources.debian.org: `null_compositor.c`, `simulated_hmd.c`, `target_builder_simulated.c`). Values marked [25.0.1] were
read only there. The run's own logs confirm them.

**Simulated HMD.** `SIMULATED_ENABLE=1`.
- Without it, the "simulated" builder is excluded from automatic discovery. Its priority is -50, so a supported real
  HMD would win; this machine has none.
- [25.0.1] Head centre (0, 1.6, 0) m. Motion is always WOBBLE: a few cm of position, and orientation sines up to
  about 7 degrees of yaw. The head is never still, and that is fine: the logs check that the submitted poses are the
  rendered ones.
- Panel 1280x720 (two views of 640x720), fov 85 degrees, no distortion.
- `SIMULATED_ROTATE` exists in 25.1's strings (a rotating head). Not used.
- `SIMULATED_LEFT/RIGHT` unset = no controllers. `SIMULATED_VIEW_COUNT` defaults to 2.

**Headless: the null compositor.** `XRT_COMPOSITOR_NULL=1`. It is built in: the banner string "Null compositor
starting, if you intended to use the null compositor (for CI integration)..." is in `monado-service`.
- No window and no display. It never renders, reads back or dumps an image [25.0.1].
- [25.0.1] Recommended view 320x240, max 1920x1080 (HoF2 saw the same 320x240).
- [25.0.1] Fake pacing at **20 Hz** (`frame_interval_ns = 1 s / 20`), refresh rate 20 Hz.
- Swapchain formats: whatever its Vulkan device supports.
- Consequence: from the first present with the session running, the layer's `xrWaitFrame` paces the game at
  **20 fps**. That covers the intro, the menus, the load and play, so the waits in somvr-run5.cmd are longer. The
  60 fps check stays with WiVRn.
- Not used: `U_PACING_APP_IMMEDIATE_WAIT_FRAME_RETURN` (it would unpace the app against a 20 Hz timeline).

**Other knobs** (all from the strings):
- `XRT_NO_STDIN` (stops monado-service reading stdin; replaces HoF2's `tail -f /dev/null |`).
- `XRT_LOG`, `XRT_COMPOSITOR_LOG`, `XRT_PRINT_OPTIONS`.
- `IPC_EXIT_ON_DISCONNECT`, `IPC_EXIT_WHEN_IDLE(_DELAY_MS)`. Not used: idle-exit could quit before the game connects.
- `XRT_COMPOSITOR_FORCE_GPU_INDEX` / `_FORCE_CLIENT_GPU_INDEX`.
- `XRT_COMPOSITOR_FORCE_XCB` / `_XCB_DISPLAY` / `_XCB_FULLSCREEN` (the window mode, section 5.2).
- `P_OVERRIDE_ACTIVE_CONFIG` (HoF2's `remote`; not used).
- Client side: `IPC_LOG`, `OXR_DEBUG_*`.

**Socket, pid file, and running beside wivrn-server.**
- Client and server both use `$XDG_RUNTIME_DIR/monado_comp_ipc` (`u_file_get_path_in_runtime_dir`). This version has
  no env override for the socket path.
- The pid file is `$XDG_RUNTIME_DIR/monado.pid` (libbsd `pidfile_open` with flock). A stale file is fine ("No other
  Monado instance was running, got new pidfile"); a live one gives "Monado is already running, pid N".
- WiVRn uses `$XDG_RUNTIME_DIR/wivrn/comp_ipc` and `wivrn.pid`, so there is **no conflict with the running
  wivrn-server**. It is not touched.
- The only possible clash is another Monado in `/run/user/1000`: HoF2's env-1 Monado (`HandOfFate2/scripts/monado.sh`,
  same socket, remote driver on TCP 4242). `/run/user/1000/monado.pid` from 16:22 today is its stale file. Pre-check
  that no monado-service runs; the SoM slot covers the run.
- The game cannot get another runtime dir, so a SoM-private one (design-vr.md's "own runtime directory") is not
  possible now (section 3).
- `monado.socket` (`ListenStream=%t/monado_comp_ipc`) would socket-activate a full `monado.service` in real-hardware
  mode on the desktop. It is not active (no socket in /run/user/1000). Never enable it.
- Do **not** use `shared/scripts/monado-start.sh`. It pkills every monado-service and symlinks
  `~/.config/openxr/1/active_runtime.json`.
- `vr-session-watch` counts any `monado_comp_ipc` connection as **test** (`status.json` `"test": true`, `"user": false`).
  LACT's VR rule is "process vr-session" only, so the GPU profile stays as it is. boot_check's `user_in_vr` looks only
  at WiVRn's socket and `vr-session`, so Monado does not abort the run.

**Pointing only the game at Monado.** `--game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json`. The loader
then uses that runtime only, never the user's active runtime. monado-service gets its own `XDG_CONFIG_HOME` /
`XDG_CACHE_HOME` / `XDG_DATA_HOME` / `HOME` under the run folder, so `~/.config/monado`, `~/.config/openxr` and
`~/.config/wivrn` are never read or written.

## 2. Command lines (bash, outside the Bash sandbox; HoF2's "slot free" first)

### 2.1 Pre-flight

Offline:
- `mod/somvr/build.sh --check` and `mod/somvr/vklayer/build.sh --check`. The real-loader test there runs with an empty
  HOME/XDG and a missing `XR_RUNTIME_JSON`.
- The builds of 2026-10-19 22:04 (commit 616ebfd: `SOMVR_XR_DUMP` and the display-time error) are newer than their sources.

Before the launch, run these checks and stop if any fails:

```bash
chrt -o -p 0 $$; ps -o cls= -p $$                                  # TS (never keyd's FF)
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; RT=/run/user/1000
pgrep -a -x monado-service                                         # nothing (no HoF2 Monado either)
[ -e $RT/monado_comp_ipc ] && echo "STOP: a Monado socket exists"
systemctl --user is-active monado.socket monado.service            # inactive, inactive
journalctl -k -b | grep -E 'GPU reset|VRAM is lost|ring .* timeout'  # nothing; else stop, tell the user, suggest a reboot
cat $RT/vr-session/status.json                                     # "user": false
ls -la --time-style=+%T $RT/vr-session/                            # no fresh reserved-env* / game-env*
# the scout LD_LIBRARY_PATH (run 4's launch-cmd.txt) in front of /lib64 must not shadow the XR libraries:
LLP=$(tr ' ' '\n' < $SOM/logs/boot-check-20261019-211128/launch-cmd.txt | sed -n 's/^LD_LIBRARY_PATH=//p')
LD_LIBRARY_PATH=$LLP ldd /usr/lib64/libopenxr_loader.so.1 /usr/lib64/monado/libopenxr_monado.so | grep -v -E '=> /(usr/)?lib64/'
#   prints only the two file names and linux-vdso / ld-linux lines; anything from steam-runtime or pinned_libs = note it
```

### 2.2 Monado (headless) and the run

Run it as one bash script (`bash -s` or a throwaway file outside the repo), so a STOP exits through the trap:

```bash
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; RT=/run/user/1000; MSVC=""
TS=$(date +%Y%m%d-%H%M%S); R5=$SOM/logs/run5-$TS
mkdir -p "$R5/mxdg/config" "$R5/mxdg/cache" "$R5/mxdg/data"
stop_monado() {
  [ -n "$MSVC" ] || return 0
  kill -INT "$MSVC" 2>/dev/null; for i in $(seq 1 25); do kill -0 "$MSVC" 2>/dev/null || break; sleep 0.2; done
  kill -0 "$MSVC" 2>/dev/null && kill -TERM "$MSVC"; sleep 1
  [ -e $RT/monado_comp_ipc ] && ! ss -xlH | grep -q monado_comp_ipc && rm -f $RT/monado_comp_ipc
}
trap stop_monado EXIT          # every exit path, also a STOP below or a VR abort of boot_check
setsid chrt -o 0 env -i HOME="$R5/mxdg" PATH=/usr/bin:/bin XDG_RUNTIME_DIR=$RT \
  XDG_CONFIG_HOME="$R5/mxdg/config" XDG_CACHE_HOME="$R5/mxdg/cache" XDG_DATA_HOME="$R5/mxdg/data" \
  SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
  XRT_LOG=info XRT_COMPOSITOR_LOG=info XRT_PRINT_OPTIONS=on \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
  timeout -s INT -k 10 1800 /usr/bin/monado-service > "$R5/monado.log" 2>&1 < /dev/null &
TPID=$!
for i in $(seq 1 50); do [ -S $RT/monado_comp_ipc ] && break; sleep 0.2; done
MSVC=$(pgrep -P "$TPID" -x monado-service)      # timeout's child (setsid execs: no job control in a script)
[ -n "$MSVC" ] && tr '\0' '\n' < /proc/$MSVC/environ | grep -qx "XDG_CONFIG_HOME=$R5/mxdg/config" ||
  { echo "STOP: no Monado of ours (pid ${MSVC:-none})"; tail -30 "$R5/monado.log"; kill -INT "$TPID" 2>/dev/null; exit 1; }
grep -q 'Null compositor starting' "$R5/monado.log" || { echo "STOP: no null compositor"; tail -30 "$R5/monado.log"; exit 1; }
GE=(--game-env SOMVR_STEREO=B --game-env SOMVR_VIEWS=xr --game-env SOMVR_COMFORT=1
    --game-env VK_LAYER_PATH=$SOM/mod/somvr/vklayer --game-env VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
    --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json --game-env SOMVR_XR=1
    --game-env SOMVR_XR_LOG=@RUN@/xr-layer.log --game-env SOMVR_XR_DUMP=60,300,700
    --game-env XR_LOADER_DEBUG=info --game-env VK_LOADER_DEBUG=layer)
B=(--steam --save-copy --seed-prefs $SOM/logs/boot-check-20261018-003728/preferences.seeded
   --preload $SOM/mod/somvr/build/somvr.so "${GE[@]}" --cmds $SOM/scripts/cmds/somvr-run5.cmd)
$SOM/scripts/boot_check.sh --dry-run "${B[@]}" || exit 1
DRY=$(ls -d $SOM/logs/boot-check-dryrun-* | sort | tail -1)   # the game-only env must name Monado's runtime
grep -q '^# game only (--game-env):.* XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado\.json' "$DRY/launch-cmd.txt" ||
  { echo "STOP: XR_RUNTIME_JSON missing in $DRY/launch-cmd.txt"; exit 1; }
grep -q "^# game only (--game-env):.* SOMVR_XR_LOG=$DRY/xr-layer\.log" "$DRY/launch-cmd.txt" ||
  { echo "STOP: @RUN@ not replaced in $DRY/launch-cmd.txt"; exit 1; }
$SOM/scripts/boot_check.sh "${B[@]}"
```

Notes on the environment:
- The layer's own README keys: `VK_LAYER_PATH` (the manifest's `./build/libVkLayer_somvr_openxr.so` is relative to
  it), `VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr`, `SOMVR_XR` unset or `1` (= auto: OpenXR for the "Feral3D"
  instance only), and `SOMVR_XR_LOG`.
- Defaults are kept: `SOMVR_XR_LAYER=quad` (mono presents), `SOMVR_XR_SPACE=local`, the loader
  `/usr/lib64/libopenxr_loader.so.1` (1.1.62), `SOMVR_XR_TIMEOUT_MS=5000`.
- `--game-env` accepts every one of these keys: none matches its blocked list, which is HOME, PATH, `XDG_*`,
  `GAMESCOPE_*`, `LD_*`, `SOMVR_LOG`, `SOMVR_TRIGGER` and a few more.
- `SOMVR_XR_LOG=@RUN@/xr-layer.log`: boot_check replaces `@RUN@` with the run folder (section 3 (a); in the dry run
  with the dry-run folder). RUN exists before the launch, as the layer's `fopen "ae"` needs. The layer log and the
  dumps (`xr-layer.log.stereoN.ppm`, section 5.1) land in RUN. `$R5` keeps only Monado's files (monado.log, mxdg/).
- `SOMVR_XR_DUMP=60,300,700`: the XR image after the 60th, 300th and 700th layout-B stereo submit. At 20 Hz the 40 s
  window has about 800, so 700 lands near its end (a shorter window just misses it). Each dump stalls the present
  thread once (one fence wait, about one 50 ms frame) and writes about 12 MB.
- `VK_LAYER_PATH` hides the system validation layer. For a validation run (4.6.5 check 3), use `VK_ADD_LAYER_PATH`
  instead (loader 1.4.341).
- `VK_DRIVER_FILES` applies to monado-service only. lavapipe's ICD is installed, and a compositor on it would fail
  the layer's GPU check ("DIFFERENT").

### 2.3 After the run

`RUN=$(ls -d $SOM/logs/boot-check-2* | sort | tail -1); cp -p $R5/monado.log $RUN/` (the layer log is in RUN already).
The pictures: `for n in 60 300 700; do magick $RUN/xr-layer.log.stereo$n.ppm $RUN/xr-stereo$n.png; done`, then look at
both eyes as a player. The trap has stopped Monado. Then
`pgrep -x monado-service` must print nothing, and `$RT/monado_comp_ipc` must be gone. The stale `monado.pid` stays,
as HoF2's did. Finally the steps after every run: the Steam playtime, a NOTES.md entry, and "slot released".

### 2.4 Optional during the run

`monado-ctl -c` once, in the 10 s after the `MARK recenter-window` line appears in RUN/run.log. Same shell,
`XDG_RUNTIME_DIR=$RT`. This is the runtime's recentre. It is a brief second IPC client, counted as "test" by the
watcher. Expect the layer to log `xr: reference space 2 changes (recentre): the game side re-latches its reference`
and somvr to log a `RIG ... recentre=1 (2 so far)` line (item 6). **[med]** whether 25.1's `-c` sends
`REFERENCE_SPACE_CHANGE_PENDING` for LOCAL.

## 3. Isolation: the game reaches Monado's socket (nothing to change)

- **`--steam` (the only mode that boots the game).** No namespace. `ptrace_parent.py` forks and execs
  `env -i ... ShadowOfMordor` with `XDG_RUNTIME_DIR=/run/user/1000`, the real filesystem and the network. The Monado
  client in the game connects to `/run/user/1000/monado_comp_ipc`.
- **Isolated mode** (`bwrap --dev-bind / / --unshare-net`). A pathname Unix socket ignores the network namespace,
  and `/run/user/1000` is bound in, so the socket would be reachable here too. Only abstract sockets would not be,
  and Monado's is not one. This mode cannot boot the game (no Steam) anyway.
- **The limit.** The game's `XDG_RUNTIME_DIR` is fixed (blocked in `--game-env`; gamescope's WSI layer finds
  `$RT/gamescope-N` through it). So Monado must use the default runtime dir.

Harness proposals ((a) and (b) are done, 2026-10-19; the rest are for runs after this one):
- (a) **Done.** `@RUN@` in `--game-env` values becomes the run folder (the dry-run folder in a dry run), so
  `SOMVR_XR_LOG=@RUN@/xr-layer.log` lands in RUN. The K=V check is unchanged, and a `SOMVR_PROBE_*` path may start
  with `@RUN@`.
- (b) **Done.** `trigger recenter|turn+|turn-`. somvr acts on them when the file's content changes while it stands.
  A new file's first content only turns stereo on, and run.log says so. One change per 250 ms poll, so the cmd file
  puts 2 s between the two words.
- (c) An `--xr-monado` switch: start and stop monado-service (section 2.2) inside the slot and cleanup, refuse
  `VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr` without `XR_RUNTIME_JSON`, and stop Monado on a VR abort.
- (d) The layer log's and monado.log's tails in summary.txt.
- (e) Later, a SoM-private Monado runtime dir. This needs the game's `XDG_RUNTIME_DIR` moved, with an absolute
  `GAMESCOPE_WAYLAND_DISPLAY` (a path is accepted by `wl_display_connect`; not tried with the WSI layer).

## 4. What to read: pass / fail

Files: `RUN/xr-layer.log` (L), `RUN/somvr.log` (S), `$R5/monado.log` (M), `RUN/game.log` (loader lines),
`RUN/summary.txt`, `RUN/xr-layer.log.stereo{60,300,700}.ppm` (P).

| # | Check | Pass | Fail |
|---|---|---|---|
| M | Monado up, headless | the options print shows `SIMULATED_ENABLE` and `XRT_COMPOSITOR_NULL` true; the null-compositor banner; the HMD is "Simulated HMD"; one client connects (the game) and leaves at quit | `already running`, `Could not bind socket`, a window or display error |
| L0 | layer loaded, XR init | `VK_LAYER_SOMVR_openxr loaded: SOMVR_XR=1 -> auto (Feral3D only)...`; `vkCreateInstance ... engine "Feral3D" ... -> XR candidate`; `phase pending -> ready`; `stereo views, recommended 320x240 per eye (max 1920x1080)` ([25.0.1] values); `system ...; Vulkan ..; instance exts ...; device exts ...`; `device ... created (XR on)` | `no OpenXR loader`, `xrCreateInstance: XR_ERROR_...`, `did not answer within`, `lacks the runtime's device extension`, `... with the XR extensions: N; retry` |
| 1a | struct found, session runs | `runtime GPU "...RADV..." 1002:73bf, game GPU ... -> same`; `somvr_shared found at 0x.. (v2, preload pid P, layout B): per-eye path on, 2 slot(s)` (P = S's START pid); `session created on queue family F index I ... space LOCAL`; `session state 1`, `2`, `session running`, `published to somvr_shared ... (state running)`, states 3/4/5 | `DIFFERENT`, `no active somvr_shared`, `xrCreateSession failed` |
| 1b | provider on the game thread | S: RATE `xr=1` after that, `locates` grows by H4's count, `fail=0`; H4 burst lines `views=xr`, no `+reused`, **every `locate_ms` < 1.000** (max and median noted), `complete=1`; H4 stays at the 20/s pacing (no stall against `xrWaitFrame`) | `locate_ms` > 5, `+reused`, H4 mono with `xr state 1`, RATE `fail` > 0 |
| 2 | tag on the present thread | the `[pid/tid]` of L's `stereo frame` / `layout B frame` lines = S's H9 `tid=` (WinMain's render thread). If they differ, the FIFO path is in use: its counts must stay in step (L `stereo N` = S END `stereo`, give or take one at each trigger edge) | another tid with reuse growing; `a present on another queue` |
| 3 | per frame | S RATE: H1 = H3 = 2 x H4, `h3=` +2 per stereo frame; L `stereo frame ... applied 0x3, flags 0x0 ... rects 0+1920 / 1920+1920 (stereo k, reuse 0, mono m)`; the two view positions differ by the runtime's IPD (a few cm; note it - `SOMVR_IPD_CM` is for fake views only) | `applied` 0x1 / 0x2 (fov_cover sent), `reuse` > 0 in steady stereo |
| 6 | trigger words (cmd file, after the recenter window) | S: `TRIGGER cmd=recenter`, then a `RIG ... recentre=1` line; 2 s later `TRIGGER cmd=turn+`, then `RIG ... snap=1`; RUN/run.log `trigger: ... (recenter)` / `(turn+)` without "does not act"; stereo stays on (no `TRIGGER off` before the untrigger) | no `TRIGGER cmd=` line, "does not act" in run.log, a `TRIGGER off` in between |
| 5 | prediction | S: count `+extrapolated` among the burst's H4 lines; consecutive `time=` 50 ms apart (20 Hz); L flags 0x1 = EXTRAPOLATED; L's `display-time error` (the record's time minus the frame's predicted one) noted: +0.000 ms = located for exactly that frame | most frames extrapolated (the provider then never sees a fresh wait) |
| 8 | reuse (log half) | any `has no complete record: the last stereo frame again (reuse N)` is followed by no `xrEndFrame failed`. With no record mismatch there is nothing to see: not forced in run 5 | `xrEndFrame failed` after a reuse |
| C2 | 4.6.5 check 2 (log half) | `vk: image 0x.. 1920x1080 format 37 ...: an eye image candidate` x2 with S's `EYES gen=.. vkimage=A,B`; at most one `without its eye images: vkimage ... not known yet`; `xr: swapchain 3840x1080 format 43 (n images) for eye image format 37: copy` (no "brightness will be off"); `layout B frame N from the eye images A/B (1920x1080 format 37, gen G; ..)` x3; S as run 4 (H8 `-> copied` x2 per frame, `END ... eye_copies = 2 x stereo, eye_misses=0`) | any other `without its eye images` reason (note it: a bug), `eye image format 37 unusable` |
| P | pictures (5.1) | L: `dump after layout-B stereo submit 60,300,700 -> ...`, one `dump buffer N bytes, memory type T`, three `dump stereo frame N -> RUN/xr-layer.log.stereoN.ppm (3840x1080 format F...)`; each picture: eye L left, eye R right, the same scene with a small horizontal offset, no black or stale half | `dump off: ...` (its reason), a black or garbage half, the halves swapped |
| T | mono / transitions | before the trigger: `swapchain 1920x1080 format 50 ... for game format 44: copy` (formats per the WSI swapchain), `mono frame (tag yes, frame 0) ... layer quad`; at stereo off a mono swapchain again | `ended incomplete` in steady play |
| E | teardown | `vkDestroyDevice: session down (frames F, copies C)`, `xr.state -> dead` without "still running after 1 s"; no `failed:` / `pass-through from now on` anywhere | any `failed:` line: its name says where |
| H | harness | SAVE OK / GameData.sav UNCHANGED, guard read 1 / write 3 into RUN/saves; install unchanged (CEF leftovers moved); no new real-home feral*; nothing left alive; dmesg clean afterwards | any `!!!` |

To note for later:
- The game.log loader lines (`XR_LOADER_DEBUG`: the manifest used, `VK_LOADER_DEBUG`: the layer order next to
  gamescope's WSI layer).
- Whether the first XR candidate instance comes from the pre-game window.
- Extra eye-size candidates made by Monado's client after the first stereo episode (harmless with one episode;
  `MAX_EYE_CAND` is 16).
- The shots should show one normal desktop view as in run 4, at 20 fps.

## 5. Pictures

### 5.1 `SOMVR_XR_DUMP` in the layer (implemented 2026-10-19, commit 616ebfd)

The null compositor writes no image, so the layer does. `SOMVR_XR_DUMP=N[,M,...]` takes at most 4 positive values.
After the N-th layout-B stereo submit, the XR image goes to `<SOMVR_XR_LOG>.stereoN.ppm`. That is both eyes side by
side, exactly what `xrReleaseSwapchainImage` hands the runtime. Unset: no code path changes (no buffer, no extra
barrier). Without `SOMVR_XR_LOG` it is off and silent. Any error is logged once (`xr: dump off: <why>`) and the dump
goes off; XR is never affected.
- **Hook.** `eyes_submit()`. After the two `copy_region` calls the XR image is in `TRANSFER_DST_OPTIMAL`. When
  armed, the submit adds:
  - a barrier to `TRANSFER_SRC_OPTIMAL`;
  - `vkCmdCopyImageToBuffer` of the whole image (`X.sc_w x X.sc_h`, tightly packed) into the buffer;
  - a buffer barrier `TRANSFER_WRITE -> HOST_READ` at the HOST stage;
  - then the existing final barrier, from `TRANSFER_SRC_OPTIMAL`.

  `dump_arm()` arms it in `xr_frame()` right before the submit, when `X.n_eyes + 1` is in the list. After a successful
  submit (`X.n_eyes` counts it), `dump_write()` waits the slot's fence (1 s cap, then `pending = 0`, as `slot_wait`
  leaves it) and writes the mapped buffer as P6. RGBA or BGRA comes from the XR format; the sRGB bytes are written as
  they are.
- **Entry points.** `DEV_FUNCS` gains `CreateBuffer DestroyBuffer GetBufferMemoryRequirements AllocateMemory
  FreeMemory BindBufferMemory MapMemory UnmapMemory CmdCopyImageToBuffer`; `inst_st` gains
  `GetPhysicalDeviceMemoryProperties`. If any is missing, the dump is off.
- **Buffer.** `TRANSFER_DST`, `X.sc_w x X.sc_h x 4` bytes. It is made at the first eye frame (again if the XR image
  grows), stays mapped, and is freed in `L_DestroyDevice`. A failed step frees what was made.
- **Log.** `xr: dump after layout-B stereo submit 60,300,700 -> <log>.stereoN.ppm` at load;
  `xr: dump buffer N bytes, memory type T (flags F)`; `xr: dump stereo frame N -> PATH (3840x1080 format 43, sRGB bytes
  as is)`.
- **Cost.** Once per N, on the present thread: one stall of about one frame and about 12 MB written. Review with
  `magick X.ppm X.png` (as a player: both eyes).
- **Deviations from the proposal's sketch (no longer in this file), and why.**
  - The HOST_READ buffer barrier. A fence wait makes device writes available, not visible to the host.
  - The final barrier's `srcAccessMask` stays `TRANSFER_WRITE`. The sketch OR-ed `TRANSFER_READ` in always, which
    would have changed the unset path. The TRANSFER stage dependency already orders the read before the transition.
  - The memory type is HOST_VISIBLE | HOST_COHERENT, preferring HOST_CACHED and not DEVICE_LOCAL. That gives fast CPU
    reads and no VRAM: on RADV, the GTT cached type rather than the write-combined one or the BAR.
  - The list parser, the pixel order and the memory pick live in `somvr_logic.c` (unit-tested in test_logic).
- **Tests** (test_layer; the fake driver implements the new functions over host memory and tracks what each image
  holds; the device's buffer writes reach the mapping only when the submit's fence is waited, so a dump read too early
  sees 0xcd):
  - `dump` and `dump-bgra` (RGBA eyes into format 43, BGRA eyes into 50): three stereo frames with `SOMVR_XR_DUMP=2`.
    The buffer is made at frame 1. Only frame 2 has the extra barriers, chained correctly. The PPM exists from frame 2
    on, and every pixel of its left half is eye L's colour, of the right half eye R's. The memory type is the cached
    one. Nothing is left after vkDestroyDevice.
  - `dump-nobuffer`, `dump-nomemory`: one `dump off` line, all three frames still stereo with stereo-b's barriers, no
    file, nothing leaked.
  - `dump-nolog`: no log, so no buffer, no barrier and no file.
  - `stereo-b` (dump unset): no buffer, no buffer copy, exactly 6 barriers per frame.
- **Variant (not built).** Dump the two eye images instead, inside the existing window where they are in
  `TRANSFER_SRC_OPTIMAL`, with `bufferRowLength = 2W` and `bufferOffset = e*W*4`. It needs no XR-image barrier
  change, but it does not prove our copy into the XR image.

Run 5 uses `--game-env SOMVR_XR_DUMP=60,300,700` (section 2.2).

### 5.2 Without code: a real compositor on its own virtual screen (run 5b, only if a picture is needed first)

- This is HoF2's pattern: `XRT_COMPOSITOR_FORCE_XCB=1`, `DISPLAY` = a **second** headless gamescope, and a
  `gamescopectl screenshot` of it. The simulated HMD has no distortion, so the window shows both eyes side by side.
- Not the game's gamescope: gamescope shows one focused window.
- More GPU work: a compositor pass plus a second gamescope, so it is not the first choice after 10-16's amdgpu reset.
- It also shows the compositor side of item 8 (the last released image after a reuse).
- Needs a SoM copy of the virtual-screen start, because HoF2's `vdisplay.sh` is not ours to use.

## 6. Risks

- **The layer's first boot in the game.**
  - New barriers, semaphores and a queue shared with Monado's client, which submits on the game's present queue
    inside our `xrEndFrame`/release.
  - A Vulkan mistake can hang a ring, and that means a GPU reset.
  - Mitigations: the null compositor (no rendering, 20 fps game), a short stereo window, the 4 GB VRAM guard, and
    dmesg checked before and after. The layer is fail-soft: fences time out after 1 s and `xr_fail` passes through.
  - On any "ring timeout"/"GPU reset": stop testing, tell the user, suggest a reboot (memory rule).
- **20 Hz for the whole session.** The menus and load run slower. The cmd file's waits are guesses; read the shots
  if a step misses.
- **XR_RUNTIME_JSON missing** (typo). The loader would take the user's active runtime (WiVRn), and the game would
  become a wivrn-server client. boot_check's VR monitor would abort the run within 2 s, but WiVRn would have seen a
  client. The dry run's `game only` line is the check; proposal (c) makes it a refusal.
- **A HoF2 env-1 Monado** started during the run would `rm` our socket, and its `stop_own` could kill ours. Mitigation:
  the slot and the HoF2 coordinator's OK; don't run while a HoF2 chain is active.
- **The user's WiVRn config: nothing touches it.**
  - No `active_runtime.json` change: not `shared/scripts/monado-start.sh`.
  - Monado's XDG dirs are in `$R5`, and its socket and pid file differ from WiVRn's.
  - wivrn-server and the Quest are untouched: no adb, no WiVRn client.
  - LACT stays.
- **Monado's prober** enumerates USB/HID/V4L2 at start (read-only queries, as in every HoF2 Monado start). It has no
  driver for a Quest.
- **Library shadowing.** `pinned_libs_64` and the scout dirs sit in the game's `LD_LIBRARY_PATH`, and dlopen of the
  XR loader or runtime could pick a wrong lib. The `ldd` pre-check covers it; the failure would show as `no OpenXR
  loader` or a loader error in game.log.
- **Steam playtime** about 6 min (304 s of waits plus keys; run 4 counted 4 min for 247 s). Covered by the user's
  "SoM can test on steam for now".

## 7. Open questions (the run answers them)

- 25.1's null compositor: still 20 Hz, 320x240, max 1920x1080? Seen in `stereo views, recommended ...`, the H4
  `time=` deltas, and the RATE H4/s.
- Does it accept the 3840x1080 swapchain (twice the max view width) and 1920x1080 rects, i.e. no `xrCreateSwapchain`
  / `xrEndFrame` error? If not, the next try is the main compositor (5.2) or smaller eyes.
- Item 2: does `vkQueuePresentKHR` run on H9's thread (`Vk_UseSeparateSubmitThread` off)?
- Item 5's display-time error is now in the layer's `stereo frame` line (commit 616ebfd):
  `display-time error %+.3f ms` = (record `display_time` - `frameState.predictedDisplayTime`) / 1e6. What does it
  read under Monado's 20 Hz pacing?
- Item 8 cannot be forced. A switch (e.g. `SOMVR_XR_FORCE_REUSE=N`: treat every Nth tagged stereo present as a
  mismatch) plus a picture would close it. 5.1 dumps only fresh stereo submits, not reuses, so the picture of a
  reuse would need 5.2.
- Does `monado-ctl -c` raise `REFERENCE_SPACE_CHANGE_PENDING` (2.4)?
- The eye images are still 1920x1080 against an 85 x 85 degree fov. That is fine for the logs, but the pixels are not
  square in a picture. The tall eye size (`SetResolution`, re-m1m2.md 2.3) is a later run.
