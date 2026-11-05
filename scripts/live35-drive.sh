#!/bin/bash
# live35-drive.sh - build 35's live loop in a headless Windows game (docs/re-xr-pipeline.md 162, som-b35-live). Boot:
#   WIN_MARK_VOICE=NOTE.wav HEADLESS_SLOT=auto HEADLESS_OWNER=som-b35-live SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+='
#   scripts/headset-build26.env | tr '\n' ' ') SOMVR_LIVE=on SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt
#   SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh --win <worktree>/ShadowOfMordor/mod/somvr
# (WIN_MARK_VOICE: headless_win.sh puts the recorded note into each mark as voice_rec.py would - no microphone, no sound
# out.) LV_WT = the worktree's name in launch-cmd.txt; LV_RUN=RUN drives a running boot. Every picture is a stereo dump
# (RUN/live35-steps.txt names them; scripts/xr_compose.py RUN -f N composes both eyes with the quads). LV_STEPS (default
# "voice say patch"):
#   voice  live_wait.sh started, then the word `mark live35`: the note in the inbox (its latency from the note's end),
#          live_wait woken by it
#   say    live_say.sh: the message's arrival in the layer, the dot (the palm down), the card (the left palm up: the
#          menu's gesture held), the right fingertip's tap -> read (say/1.read)
#   patch  live_patch.sh stage (LV_PATCH_DLL, default the build's somvr_logic.dll, else its xinput9_1_0.dll with
#          --allow-no-abi --force-unchecked: the stub path): its card, read; the menu by the palm, the Test tab (the
#          patch box), Apply patch pressed -> the request -> the game side's answer -> its card; with the hot reload
#          (the host's somvr_logic.dll): Undo patch the same way
#   perf   frame time windows (SOMVR_PERF=on SOMVR_XR_PERF=on SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600 in the boot):
#          `idle` 25 s, `stt` 25 s with the lane's worker transcribing in a loop, `after` 10 s (solo slot for this)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${LV_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${LV_STEPS:-voice say patch}
T0=$(date +%s); R=${LV_RUN:-}
for i in $(seq 1 "${LV_FIND:-720}"); do   # (a slot may be busy: up to an hour)
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -L -c %Y "$d")" -ge "$T0" ] && R=$(readlink -f "$d") && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/live35-steps.txt
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$S"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -E "$2"; }
waitfor() { local n0=$1 re=$2 f=$3 s=${4:-20} i; for i in $(seq 1 $((s * 4))); do since "$n0" "$re" "$f" | grep -q . && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$S"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$S"; }
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$X")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$X")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  echo "dump $1 $(grep -o 'dump stereo frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')" | tee -a "$S"; }
for i in $(seq 1 120); do grep -q '^SOMVR_LIVE_DIR=' "$R/env.txt" 2>/dev/null && break; sleep 1; done   # (b37: a pinned run may be new)
LD=$(sed -n 's/^SOMVR_LIVE_DIR=//p' "$R/env.txt" | tail -1)
[ -n "$LD" ] && [ -d "$LD" ] || bail "no SOMVR_LIVE_DIR in env.txt (SOMVR_LIVE=on in the boot?)"
echo "live dir $LD" | tee -a "$S"
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
grep -E '^LIVE config|^WARN' "$L" | cut -c1-260 | tee -a "$S"
grep -E 'xr: live: (on|off|SOMVR)' "$X" | head -2 | cut -c1-200 | tee -a "$S"
x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"; LOW="hand L head -20 -45 20 0 0 0"
tap() { x "hand R touch live $1 $2 5"; x "hand R touch live $1 $2 1.0 s 0.3"; x "hand R touch live $1 $2 -1.0 s 0.15"
        x "hand R touch live $1 $2 4 s 0.2"; x "hand R off"; }
press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
          x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
# show_card KEY NAME: the palm up -> the card KEY shown (a dump), tapped -> read
show_card() { local n0; n0=$(lines "$X"); x "$PALM_AWAY"; sleep 0.6; x "$PALM s 0.3"
  # (b37: the keys are the session's own - a card shown already by the step's picture pose counts)
  if waitfor 0 "xr: live: card $1 shown" "$X" 6; then pass "card $1 shown: $(since 0 "card $1 shown" "$X" | tail -1 | cut -c1-150)"
  else fail "card $1 not shown ($(since "$n0" 'xr: (live|vrmenu)' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"; return 1; fi
  since "$n0" 'vrmenu: opened' "$X" | grep -q . && fail "the menu opened under the card" || pass "the menu's gesture held while the card shows"
  sleep 0.5; dq "card-$2"; n0=$(lines "$X"); tap 0.5 0.5
  if waitfor "$n0" "xr: live: card $1 read" "$X" 5; then pass "card $1: $(since "$n0" "card $1 read" "$X" | tail -1 | cut -c1-140)"
  else fail "card $1 not read by the tap ($(since "$n0" 'xr: live' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"; fi
  x "$PALM_AWAY"; sleep 0.8; spoke "$1"; }
# b37 (162.8): a card's spoken line - its calm marker (the nudge) and the daemon's answer on the run's null sink
spoke() { local k=${1//:/-} i
  [ "$(sed -n 's/^SOMVR_LIVE_TTS=//p' "$R/env.txt" | tail -1)" = on ] || return 0
  for i in $(seq 1 60); do [ -s "$LD/say/$k.spoken" ] && break; sleep 0.25; done
  if grep -q '^.* spoken: on somvr_live_tts_' "$LD/say/$k.spoken" 2>/dev/null; then
    pass "card $1 spoken at its calm moment: $(cut -c1-150 "$LD/say/$k.spoken") ($(grep "tts: $k: " "$LD/live.log" | sed 's/^[^ ]* tts: //' | tr '\n' ';' | cut -c1-220))"
  else fail "card $1 not spoken ($(cat "$LD/say/$k.spoken" 2>/dev/null | cut -c1-120); calm $([ -e "$LD/say/$k.calm" ] && echo yes || echo no); $(grep "tts: $k" "$LD/live.log" | tail -2 | tr '\n' ' ' | cut -c1-200))"; fi; }
for s in $STEPS; do
  case $s in
    voice)
      n_in=$(ls "$LD/inbox" 2>/dev/null | grep -c '\.json$')
      ( "$HERE/live_wait.sh" --dir "$LD" --timeout 90 > "$R/live-wait.out" 2>&1; echo "exit $? at $(date +%s.%N)" >> "$R/live-wait.out" ) &
      sleep 1; n0=$(lines); w "mark live35 voice" 1
      want=$((n_in + 1)); f=$LD/inbox/$want.json
      for i in $(seq 1 240); do [ -s "$f" ] && break; sleep 0.25; done
      if [ -s "$f" ]; then
        md=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["dir"])' "$f")
        te=$(stat -c %.6Y "$md/voice.txt"); ti=$(stat -c %.6Y "$f")
        pass "note $want in the inbox $(python3 -c "print(round($ti - $te, 2))") s after the note ended: $(python3 -I -c 'import json,sys; j=json.load(open(sys.argv[1])); print(j["audio_s"], "s of voice,", j["stt_s"], "s to transcribe:", repr(j["transcript"][:120]))' "$f")"
        for i in $(seq 1 20); do grep -q '^exit' "$R/live-wait.out" && break; sleep 0.25; done
        tw=$(sed -n 's/^exit 0 at //p' "$R/live-wait.out")
        [ -n "$tw" ] && pass "live_wait woke $(python3 -c "print(round($tw - $te, 2))") s after the note ended: $(head -1 "$R/live-wait.out" | cut -c1-160)" \
          || fail "live_wait: $(tail -2 "$R/live-wait.out" | tr '\n' ' ')"
      else fail "no inbox/$want.json within 60 s ($(tail -3 "$LD/live.log" | tr '\n' ' ' | cut -c1-250))"; fi ;;
    say)
      n0=$(lines "$X"); x "$LOW"
      "$HERE/live_say.sh" --dir "$LD" "Live test: the coordinator's answer comes back on your palm. Tap it away when read." | tee -a "$S"
      waitfor "$n0" 'xr: live: message 1 arrived' "$X" 5 && pass "the layer: $(since "$n0" 'message 1 arrived' "$X" | tail -1 | cut -c1-140)" \
        || fail "no 'message 1 arrived' in the layer's log"
      sleep 3.5; x "hand L head 0 -12 35 0 -90 -40"; sleep 1; dq "dot-palm-turned"
      show_card say:1 say1
      [ -e "$LD/say/1.read" ] && [ -e "$LD/say/1.shown" ] && pass "say/1.shown and say/1.read written ($(cat "$LD/say/1.read"))" \
        || fail "say/1.shown / .read missing" ;;
    patch)
      DLL=${LV_PATCH_DLL:-}; EXTRA=()
      MODD=$(sed -n -e 's/^# mod (windows, headless_win.sh): //p' -e 's/^# headless_win.sh [^ ]* (the Windows build; MOD \(.*\))$/\1/p' "$R/launch-cmd.txt" | head -1)
      [ -z "$DLL" ] && [ -f "$MODD/build/win/somvr_logic.dll" ] && DLL=$MODD/build/win/somvr_logic.dll
      [ -z "$DLL" ] && DLL=$MODD/build/win/xinput9_1_0.dll && EXTRA=(--allow-no-abi --force-unchecked)
      [ -n "${LV_PATCH_FORCE:-}" ] && EXTRA+=(--force-unchecked)
      n0=$(lines "$X")
      "$HERE/live_patch.sh" stage "$DLL" "The live test's patch: the same code again" "Apply it, then undo it" --dir "$LD" "${EXTRA[@]}" | tee -a "$S"
      pid=$(cat "$LD/patches/ready" 2>/dev/null)
      waitfor "$n0" "xr: live: patch $pid staged" "$X" 5 && pass "the layer: $(since "$n0" 'patch .* staged' "$X" | tail -1 | cut -c1-150)" \
        || fail "no 'patch $pid staged' in the layer's log"
      sleep 3.5; show_card "patch:$pid" "patch$pid"
      # the menu: the palm (no card waiting now), the Test tab, Apply patch
      n0=$(lines "$X"); x "$PALM_AWAY"; sleep 0.8; x "$PALM s 0.3"; sleep 1.5
      since "$n0" 'vrmenu: opened' "$X" | grep -q . && pass "the menu opened by the palm" || fail "the menu did not open ($(since "$n0" 'vrmenu' "$X" | tail -1 | cut -c1-120))"
      AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
      WTD=$(cd "$HERE/.." && pwd -P)
      gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
          "$WTD/mod/somvr/somvr_font.c" -lm 2>> "$S" && "$R/vm_uv" "$(python3 -c "print(hex(int('${AV:-0}', 16) | 1 << 63))")" > "$R/vm_uv.txt"
      tu=$(awk -F'\t' '$1 == "Test" && $2 == "button" { print $4, $5; exit }' "$R/vm_uv.txt")   # (the layer's Test page bit 63)
      [ -n "$tu" ] || tu="0.900 0.0395"
      n0=$(lines "$X"); press $tu
      since "$n0" 'click Test' "$X" | grep -q . && pass "the Test tab: $(since "$n0" 'click Test' "$X" | tail -1 | cut -c1-120)" || fail "the Test tab ($tu)"
      sleep 1; dq "menu-test-box"
      # b37: with SOMVR_PERF=on in the boot, frame-time windows around each swap (pre / the swap's 15 s / post)
      PW=0; grep -qx 'SOMVR_PERF=on' "$R/env.txt" && PW=1
      pw() { [ $PW = 1 ] || return 0; w "perf $1" 0.3; x "perf $1"; echo "$(date +%T) perf window $1" | tee -a "$S"; }
      pw pre-apply; [ $PW = 1 ] && sleep 10; pw apply
      # Apply patch: its rect 16,616 232x56 of the 512x816 panel (768 before b37 live3) (somvr_vrmenu.h SOMVR_VM_LIVE_BTN_Y)
      n0=$(lines "$X"); nl=$(lines); press 0.2578 0.7892
      waitfor "$n0" 'xr: live: Apply patch pressed' "$X" 3 && pass "$(since "$n0" 'Apply patch pressed' "$X" | tail -1 | cut -c1-140)" \
        || fail "Apply patch not pressed ($(since "$n0" 'xr: (live|vrmenu)' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"
      waitfor "$nl" '^LIVE request [0-9]+: (applied|stub|refused|bad|restart)' "$L" 40 \
        && pass "the game side: $(since "$nl" '^LIVE request' | tail -1 | cut -c1-220)" || fail "no answer from the game side"
      waitfor "$n0" "xr: live: the game's answer" "$X" 5 && pass "the layer: $(since "$n0" "the game's answer" "$X" | tail -1 | cut -c1-160)"
      sleep 1; dq "menu-test-after-apply"
      [ $PW = 1 ] && { sleep 8; pw post-apply; sleep 10; }
      grep -E '^RELOAD' "$L" | tail -3 | cut -c1-260 | tee -a "$S"
      grep -aE '^LIVE config on' "$L" | tail -2 | cut -c1-120 | tee -a "$S"   # (b37: the module's own line - a test patch marks itself)
      ans=$(sed -n 's/^last=//p' "$LD/patches/state.txt")
      if [ "$ans" = applied ]; then
        sleep 2; pw undo; n0=$(lines "$X"); nl=$(lines); press 0.7422 0.7892   # Undo patch (264,616 232x56 of 816)
        waitfor "$nl" '^LIVE request [0-9]+: (undone|refused|bad)' "$L" 40 \
          && pass "undo: $(since "$nl" '^LIVE request' | tail -1 | cut -c1-220)" || fail "no answer to Undo"
        sleep 1; dq "menu-test-after-undo"
        [ $PW = 1 ] && { sleep 8; pw post-undo; sleep 10; }
        grep -aE '^LIVE config on' "$L" | tail -1 | cut -c1-120 | tee -a "$S"
      fi
      # the menu closed by the palm again; the answer's card on the next palm
      n0=$(lines "$X"); x "$PALM_AWAY"; sleep 0.8; x "$PALM s 0.3"; sleep 1.2; x "$PALM_AWAY"; sleep 0.5
      since "$n0" 'vrmenu: closed' "$X" | grep -q . && pass "the menu closed" || fail "the menu still open"
      sleep 3.5
      for k in $(sed -n "s/.*xr: live: the game's answer \([0-9]*\).*/\1/p" "$X"); do show_card "state:$k" "state$k"; done
      if [ $PW = 1 ]; then w "perf end" 0.5; x "perf end"
        grep -E '^PERF (pre-apply|apply|post-apply|undo|post-undo) ' "$L" | cut -c1-300 | tee -a "$S"
        grep -E 'xr: perf (pre-apply|apply|post-apply|undo|post-undo):' "$X" | cut -c1-300 | tee -a "$S"; fi
      grep -aE '^RELOAD' "$L" | cut -c1-260 | tee -a "$S"
      cat "$LD/patches/log.txt" | tee -a "$S" ;;
    perf)
      win() { w "perf $1" 0.3; x "perf $1"; echo "$(date +%T) window $1 (${2} s)" | tee -a "$S"; sleep "$2"; }
      x "$LOW"; win idle 25
      V=${WIN_MARK_VOICE:-}; [ -r "$V" ] || V=$(ls "$R"/marks/*/voice.wav 2>/dev/null | head -1)
      args=(); for i in $(seq 1 30); do args+=("$V"); done
      "$HERE/live_lane.sh" exec stt "${LIVE_STT_PY:-$HOME/Projects/flat2vr/shared/tools-venv/bin/python}" -I "$HERE/live_stt.py" \
          --model base.en --threads 2 --bench "${args[@]}" > "$R/perf-stt.out" 2>&1 &
      SP=$!; sleep 1; win stt 25; kill "$SP" 2>/dev/null   # (the lane execs: $SP is the worker itself)
      win after 10; w "perf end" 0.5; x "perf end"
      echo "transcriptions in the stt window: $(grep -c '^bench' "$R/perf-stt.out")" | tee -a "$S"
      grep -E '^PERF (idle|stt|after) ' "$L" | cut -c1-300 | tee -a "$S"
      grep -E 'xr: perf (idle|stt|after):' "$X" | cut -c1-300 | tee -a "$S" ;;
  esac
done
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; cat "$S" | grep -E 'PASS|FAIL|dump' | cut -c1-200
