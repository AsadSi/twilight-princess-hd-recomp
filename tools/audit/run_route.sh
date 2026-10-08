#!/bin/bash
# Usage: tools/audit/run_route.sh <route.txt> <total seconds> [shots...]
# Boots the game with the route as scripted GamePad input, takes shots, quits, and reports a new crash report.
# Run from anywhere; output goes to build/audit/<route name>/ (log.txt, tNN.png, vNN/frames).
#
# Route lines (# comments): "<from>-<to> <buttons>" holds buttons, e.g. "13-13.3 PLUS" or "20-20.2 A+B";
# "<from>-<to> STICK <x> <y>" / "RSTICK <x> <y>" holds a stick (-1..1). Times are game seconds (logic steps / 30)
# from the first frame, so a route replays the same whatever the frame pacing.
# Shots: "<t>" saves the TV image at game time t (the runtime's frame dump, scaled to 480 wide);
# "v<t>:<seconds>" records the game window from wall-clock time t (bin/wincap, built from wincap.swift on first
# use; needs screen recording permission).
# Env: SAVE (save folder under build/audit/saves, default "route"), FRESH=1 starts that save folder empty,
# LOAD=<slot> loads save state <slot> from build/audit/states at frame 60 and starts the route's clock at frame 90
# (take one with WWHD_STATE_SAVE_AT=<frame>:<slot>).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ROUTE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); TOTAL=$2; shift 2
NAME=$(basename "$ROUTE" .txt); OUT=$ROOT/build/audit/$NAME; rm -rf "$OUT"; mkdir -p "$OUT"
SAVEDIR=$ROOT/build/audit/saves/${SAVE:-route}; [ "${FRESH:-0}" = 1 ] && rm -rf "$SAVEDIR"; mkdir -p "$SAVEDIR"
APP=$ROOT/build/cmake-tphd/wwhd
LOCK=$ROOT/build/audit/.run.lock
until mkdir "$LOCK" 2>/dev/null; do sleep 2; done; trap 'rmdir "$LOCK"' EXIT

bit() {  # VPAD button bits
  case $1 in A) echo 8000;; B) echo 4000;; X) echo 2000;; Y) echo 1000;; LEFT) echo 800;; RIGHT) echo 400;; UP) echo 200;;
    DOWN) echo 100;; ZL) echo 80;; ZR) echo 40;; L) echo 20;; R) echo 10;; PLUS) echo 8;; MINUS) echo 4;; HOME) echo 2;;
    STICK_R) echo 20000;; STICK_L) echo 40000;; esac
}
PRESS=; STICK=; RSTICK=
while read -r RANGE WHAT X Y; do
  [[ -z $RANGE || $RANGE == \#* ]] && continue
  case $WHAT in
    STICK) STICK+="${STICK:+,}$RANGE:$X:$Y" ;;
    RSTICK) RSTICK+="${RSTICK:+,}$RANGE:$X:$Y" ;;
    *) BITS=0
       for B in ${WHAT//+/ }; do
         [ -n "$(bit "$B")" ] || { echo "unknown button $B in $ROUTE"; exit 2; }
         BITS=$((BITS | 16#$(bit "$B")))
       done
       PRESS+="${PRESS:+,}$RANGE:$(printf %x $BITS)" ;;
  esac
done < "$ROUTE"

ORIGIN=1; [ -n "${LOAD:-}" ] && ORIGIN=90
DUMPS=; CLIPS_AT=()
for SHOT in "$@"; do
  if [[ $SHOT == v* ]]; then CLIPS_AT+=("$SHOT"); else DUMPS+="${DUMPS:+,}$(perl -e "print int($SHOT * 30 + $ORIGIN)")"; fi
done

pkill -TERM -x wwhd 2>/dev/null && sleep 2
BEFORE=$(ls ~/Library/Logs/DiagnosticReports/ | grep -c '^wwhd')
cd "$OUT"
WWHD_STATE_DIR=$ROOT/build/audit/states WWHD_STATE_LOAD_AT=${LOAD:+60:$LOAD} \
WWHD_TEST_ORIGIN=$ORIGIN WWHD_TEST_PRESS=$PRESS WWHD_TEST_STICK=$STICK WWHD_TEST_RSTICK=$RSTICK WWHD_DUMP_FRAMES=$DUMPS \
  "$APP" --game "$ROOT/game" --save "$SAVEDIR" > log.txt 2>&1 &
PID=$!; disown $PID; START=$(date +%s)
elapsed_lt() { perl -MTime::HiRes=time -e "exit !(time - $START < $1)"; }
CLIPS=()
for SHOT in "${CLIPS_AT[@]+"${CLIPS_AT[@]}"}"; do
  T=${SHOT#v}; T=${T%%:*}
  while elapsed_lt "$T"; do sleep 0.05; done
  kill -0 $PID 2>/dev/null || { echo "EXITED before ${T}s"; break; }
  [ -x "$ROOT/build/audit/bin/wincap" ] || { mkdir -p "$ROOT/build/audit/bin"; swiftc -O "$ROOT/tools/audit/wincap.swift" -o "$ROOT/build/audit/bin/wincap"; }
  "$ROOT/build/audit/bin/wincap" $PID "${SHOT#*:}" "$OUT/v$T" > /dev/null & CLIPS+=($!)
done
while elapsed_lt "$TOTAL" && kill -0 $PID 2>/dev/null; do sleep 0.5; done
kill -0 $PID 2>/dev/null && echo "ALIVE at ${TOTAL}s" || echo "DIED"
(( ${#CLIPS[@]} )) && wait "${CLIPS[@]}"
kill -TERM $PID 2>/dev/null; for _ in 1 2 3 4 5 6; do kill -0 $PID 2>/dev/null || break; sleep 0.5; done
kill -9 $PID 2>/dev/null
# frame dumps -> tNN.png at 480 wide (the GamePad copies are dropped)
rm -f frame_*_drc.png
for SHOT in "$@"; do
  [[ $SHOT == v* ]] && continue
  F=frame_$(perl -e "print int($SHOT * 30 + $ORIGIN)").png
  [ -f "$F" ] && sips -Z 480 "$F" --out "t$SHOT.png" >/dev/null && rm "$F" || echo "no frame for ${SHOT}s"
done
AFTER=$(ls ~/Library/Logs/DiagnosticReports/ | grep -c '^wwhd')
(( AFTER > BEFORE )) && echo "NEW CRASH REPORT: $(ls -t ~/Library/Logs/DiagnosticReports/wwhd* | head -1)"
grep -E '\[test\]|unimplemented|fatal|stageName' log.txt | tail -8
