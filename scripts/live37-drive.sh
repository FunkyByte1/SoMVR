#!/bin/bash
# live37-drive.sh - som-b37-live3's proof in a headless Windows game (docs/re-xr-pipeline.md 162.9): the instant
# acknowledgement, the streamed answer, the paged answer - each stage timed. Boot (a note made with espeak-ng, no
# microphone, nothing played out loud - the run's own null sink, recorded to RUN/live-tts.wav):
#   WIN_MARK_VOICE=NOTE.wav HEADLESS_SLOT=auto HEADLESS_OWNER=som-b37-live3 HEADLESS_SLOT_WAIT=3600 SOMVR_EXTRA="$(grep
#   -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ') SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt
#   SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh --win <worktree>/ShadowOfMordor/mod/somvr
# LV_WT = the worktree's name in launch-cmd.txt; LV_RUN=RUN drives a running boot. Steps (LV_STEPS, default
# "ack say page"):
#   ack   the acknowledgements made at the session's start; the word `mark live37`: the note's end (voice.txt) -> the
#         ack line playing + the buzz word (the trigger file) -> the layer's haptic in the off hand -> the note in
#         the inbox; each time from the note's end
#   say   live_say.sh with three sentences, the hand low (a calm moment): the first sentence ready, the stream started
#         (the time from live_say), all sentences streamed; then the card on the palm and tapped (build 37's layer)
#   page  a long answer: 2 cards ("(1 of 2: tap for the next)", "(2 of 2)", never "..."), the whole said once with
#         page 1, page 2 quiet; each card shown and tapped, a picture of each (RUN/live37-steps.txt names the dumps)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${LV_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${LV_STEPS:-ack say page}
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
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/live37-steps.txt
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$S"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -aE "$2"; }
waitfor() { local n0=$1 re=$2 f=$3 s=${4:-20} i; for i in $(seq 1 $((s * 4))); do since "$n0" "$re" "$f" | grep -q . && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$S"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$S"; }
dt() { python3 -c "print('%.2f' % ($2 - $1))"; }
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$X")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$X")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  echo "dump $1 $(grep -o 'dump stereo frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')" | tee -a "$S"; }
for i in $(seq 1 120); do grep -q '^SOMVR_LIVE_DIR=' "$R/env.txt" 2>/dev/null && break; sleep 1; done
LD=$(sed -n 's/^SOMVR_LIVE_DIR=//p' "$R/env.txt" | tail -1)
[ -n "$LD" ] && [ -d "$LD" ] || bail "no SOMVR_LIVE_DIR in env.txt (SOMVR_LIVE=on in the boot?)"
echo "live dir $LD" | tee -a "$S"
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
grep -E 'xr: live: (on|off|SOMVR)' "$X" | head -2 | cut -c1-200 | tee -a "$S"
x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"; LOW="hand L head -20 -45 20 0 0 0"
tap() { x "hand R touch live $1 $2 5"; x "hand R touch live $1 $2 1.0 s 0.3"; x "hand R touch live $1 $2 -1.0 s 0.15"
        x "hand R touch live $1 $2 4 s 0.2"; x "hand R off"; }
show_card() { local n0; n0=$(lines "$X"); x "$PALM_AWAY"; sleep 0.6; x "$PALM s 0.3"
  if waitfor 0 "xr: live: card $1 shown" "$X" 8; then pass "card $1 shown: $(since 0 "card $1 shown" "$X" | tail -1 | cut -c1-150)"
  else fail "card $1 not shown ($(since "$n0" 'xr: (live|vrmenu)' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"; return 1; fi
  sleep 0.5; dq "card-$2"; n0=$(lines "$X"); tap 0.5 0.5
  if waitfor "$n0" "xr: live: card $1 read" "$X" 5; then pass "card $1: $(since "$n0" "card $1 read" "$X" | tail -1 | cut -c1-140)"
  else fail "card $1 not read by the tap"; fi
  x "$PALM_AWAY"; sleep 0.8; }
# streamed(KEY T_SAY): the line's stages from live.log and its .spoken
streamed() { local k=$1 i
  for i in $(seq 1 120); do [ -s "$LD/say/$k.spoken" ] && break; sleep 0.25; done
  local first play
  first=$(grep -a "tts: $k: the first sentence" "$LD/live.log" | tail -1 | sed 's/^[^ ]* tts: //')
  play=$(grep -a "tts: $k: its calm moment - playing" "$LD/live.log" | tail -1 | sed 's/^[^ ]* tts: //')
  if grep -q ' spoken: on somvr_live_tts_' "$LD/say/$k.spoken" 2>/dev/null; then
    pass "$k: $first | $play | $(cut -c21-200 "$LD/say/$k.spoken")"
  else fail "$k not spoken ($(cat "$LD/say/$k.spoken" 2>/dev/null | cut -c1-120); $play)"; fi; }
for s in $STEPS; do
  case $s in
    ack)
      for i in $(seq 1 120); do grep -q 'the acknowledgements made' "$LD/live.log" && break; sleep 0.5; done
      grep -a 'tts: ready\|acknowledgements made\|stt: ready' "$LD/live.log" | sed 's/^[^ ]* //' | cut -c1-200 | tee -a "$S"
      n_in=$(ls "$LD/inbox" 2>/dev/null | grep -c '\.json$'); nx=$(lines "$X"); nl=$(lines)
      w "mark live37 voice" 1
      want=$((n_in + 1)); f=$LD/inbox/$want.json
      for i in $(seq 1 240); do [ -s "$f" ] && break; sleep 0.25; done
      if [ -s "$f" ]; then
        md=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["dir"])' "$f"); mid=$(basename "$md")
        te=$(stat -c %.6Y "$md/voice.txt"); ti=$(stat -c %.6Y "$f")
        al=$(grep -a "ack $mid:" "$LD/live.log" | tail -1 | sed 's/^[^ ]* //')
        for i in $(seq 1 20); do [ -s "$LD/ack/$mid.spoken" ] && break; sleep 0.25; done
        ta=$(stat -c %.6Y "$LD/ack/$mid.spoken" 2>/dev/null || echo 0)
        case $al in *playing*"a buzz in the L hand"*) pass "the ack: $al" ;; *) fail "the ack: ${al:-none}" ;; esac
        grep -q ' spoken: on somvr_live_tts_' "$LD/ack/$mid.spoken" 2>/dev/null && pass "the ack said on the run's sink, done $(dt "$te" "$ta") s after the note ended: $(cut -c21-150 "$LD/ack/$mid.spoken")" \
          || fail "the ack not said ($(cat "$LD/ack/$mid.spoken" 2>/dev/null))"
        waitfor "$nl" 'haptic' "$L" 3 && pass "the game side: $(since "$nl" 'haptic' | tail -1 | cut -c1-150)" || fail "no trigger word 'haptic' in somvr.log"
        waitfor "$nx" 'xr: haptic L .* 9[0-9] ms' "$X" 3 && pass "the layer: $(since "$nx" 'xr: haptic L .* 9[0-9] ms' "$X" | head -1 | cut -c1-140)" \
          || fail "no 90 ms haptic in the left hand in the layer's log ($(since "$nx" 'xr: haptic' "$X" | tail -3 | tr '\n' ' ' | cut -c1-260))"
        pass "note $want in the inbox $(dt "$te" "$ti") s after the note ended: $(python3 -I -c 'import json,sys; j=json.load(open(sys.argv[1])); print(j["audio_s"], "s of voice,", j["stt_s"], "s to transcribe, ack", j.get("ack"), j.get("ack_s"), "s:", repr(j["transcript"][:100]))' "$f")"
      else fail "no inbox/$want.json within 60 s ($(tail -3 "$LD/live.log" | tr '\n' ' ' | cut -c1-250))"; fi ;;
    say)
      x "$LOW"; sleep 4   # (a calm moment: nothing in the hands for the calm gate's 3 s)
      "$HERE/live_say.sh" --dir "$LD" "Got it: the wall at the tower. A helper is on it now. Try the next ledge on the left while you wait." | tee -a "$S"
      streamed say-1
      show_card say:1 say1 ;;
    page)
      x "$LOW"; sleep 4
      "$HERE/live_say.sh" --dir "$LD" "Your orc was 10 metres away with his back turned, so the finisher never started. Get within 2 metres, face him, then hold the trigger to charge it. The blue outline means it is ready. Then push the blade in slowly, and pull it out the same way." | tee -a "$S"
      n=$(ls "$LD/say" | grep -cE '^[0-9]+\.txt$')
      p1=$((n - 1)); p2=$n
      grep -q '(1 of 2: tap for the next)' "$LD/say/$p1.txt" && grep -q '(2 of 2)' "$LD/say/$p2.txt" && ! grep -q '\.\.\.' "$LD/say/$p1.txt" "$LD/say/$p2.txt" \
        && [ -e "$LD/say/$p2.quiet" ] && pass "the answer in 2 pages (say/$p1.txt, say/$p2.txt quiet), no '...'" || fail "the pages ($(ls "$LD/say" | tr '\n' ' '))"
      streamed "say-$p1"
      show_card "say:$p1" "page1"
      show_card "say:$p2" "page2"
      grep -aq "tts: say-$p2: quiet" "$LD/live.log" && pass "page 2 quiet (the whole answer said with page 1)" || fail "page 2 not quiet" ;;
  esac
done
grep -a '^20.* ack \|tts: .*\(first sentence\|playing\|spoken\)' "$LD/live.log" | sed 's/^[^ ]* //' | cut -c1-220 >> "$S"
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; grep -E 'PASS|FAIL|dump' "$S" | cut -c1-220
