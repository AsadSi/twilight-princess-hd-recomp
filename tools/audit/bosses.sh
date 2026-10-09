#!/bin/bash
# Usage: tools/audit/bosses.sh [boss regex, default all]
# Loads the disc's QA save with the items that boss needs (iron boots, clawshot, ... from the dungeon's own saves), warps into
# the boss room (WWHD_WARP, 60 fps, 1x whatever the saved settings say) and plays routes/bossplay.txt there (qasaves.sh ROUTE=play's random play, 9 s
# after the warp). Shots, in seconds after the warp: single frames through the fight and four consecutive TV frames at
# 40, 70, 100 and 130 s ("alt" counts the groups whose frames alternate, as in qasaves.sh).
# Output in build/audit/bosses/ (OUT=<dir>): <boss>_<t>.png, <boss>.log, summary.tsv (boss, stage, alive, crash, unimplemented, alt)
# and sheet.png. BOSSES overrides the list ("name=save,stage,point,room,layer ...": save = ZTP number or Hero<nn>).
# FIGHT=1 plays routes/bossfight.txt instead (lock on and attack on purpose) and takes a shot every 10 s.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=${OUT:-$ROOT/build/audit/bosses}; mkdir -p "$OUT"
FILTER=${1:-.}
BOSSES=${BOSSES:-"diababa=ZTP14,D_MN05A,0,50,0 fyrus=ZTP26,D_MN04A,0,50,0 morpheel=ZTP41,D_MN01A,0,50,0 stallord=ZTP54,D_MN10A,0,50,0
  blizzeta=ZTP62,D_MN11A,0,50,0 armogohma=ZTP66,D_MN06A,0,50,0 argorok=ZTP75,D_MN07A,0,50,0 zant=ZTP80,D_MN08D,0,50,-1
  ganon_throne=ZTP90,D_MN09A,0,50,0 ganon_pig=ZTP90,D_MN09A,0,50,1 ganon_final=ZTP90,D_MN09B,0,0,0 ganon_horse=ZTP90,D_MN09,0,13,0
  ook=ZTP14,D_MN05B,1,51,0 dangoro=ZTP26,D_MN04B,0,51,0 toad=ZTP41,D_MN01B,0,51,0 deathsword=ZTP54,D_MN10B,0,51,0
  darkhammer=ZTP62,D_MN11B,0,51,0 darknut=ZTP66,D_MN06B,0,51,0 aeralfos=ZTP75,D_MN07B,0,51,0
  phantom1=ZTP80,D_MN08B,0,51,-1 phantom2=ZTP80,D_MN08C,0,52,-1"}
WARP=71  # game second of the warp: the menus have loaded the save by about 66 s
ROUTE=bossplay; SINGLES=(15 25 55 85 115 145)
[ "${FIGHT:-0}" = 1 ] && ROUTE=bossfight SINGLES=($(seq 20 10 160))
SHOTS=(); for T in "${SINGLES[@]}"; do SHOTS+=("$((WARP + T))"); done; GROUPS_AT=(40 70 100 130)
for T in "${GROUPS_AT[@]}"; do for I in 0 1 2 3; do SHOTS+=("$(perl -e "printf '%.4f', $WARP + $T + ($I + 0.25) / 60")"); done; done
printf 'boss\tstage\talive\tcrash\tunimpl\talt\n' > "$OUT/summary.tsv"
for B in $BOSSES; do
  N=${B%%=*}; W=${B#*=}; Q=${W%%,*}; W=${W#*,}
  [[ $N =~ $FILTER ]] || continue
  F=$ROOT/game/content/DebugSaves/QASaves/$Q.dat; [[ $Q == Hero* ]] && F=$ROOT/game/content/DebugSaves/QASaves/HeroMode/ZTP${Q#Hero}.dat
  S=$ROOT/build/audit/saves/qasave; rm -rf "$S"; mkdir -p "$S/user"; cp "$F" "$S/user/ZTP00.dat"
  R=$(SAVE=qasave WWHD_INTERP=1 WWHD_RES_SCALE=${WWHD_RES_SCALE:-1} WWHD_WARP="$((WARP * 60 + 1)):$W" SHOT_W=960 "$ROOT/tools/audit/run_route.sh" "$ROOT/tools/audit/routes/$ROUTE.txt" $((WARP + 170)) "${SHOTS[@]}" < /dev/null 2>&1)
  D=$ROOT/build/audit/$ROUTE
  cp "$D/log.txt" "$OUT/$N.log"
  for T in "${SHOTS[@]:0:${#SINGLES[@]}}"; do [ -f "$D/t$T.png" ] && cp "$D/t$T.png" "$OUT/${N}_$((T - WARP)).png"; done
  cp "$D/t${SHOTS[${#SINGLES[@]} + 4]}.png" "$OUT/${N}_b.png" 2>/dev/null  # the 70 s frame, for the sheet
  ALT=$("$ROOT/build/venv/bin/python" - "$D" "${SHOTS[@]:${#SINGLES[@]}}" <<'PY'
import os, sys
from PIL import Image, ImageChops, ImageStat
d, shots = sys.argv[1], sys.argv[2:]
alt = 0
for g in range(0, len(shots), 4):
    try: ims = [Image.open(os.path.join(d, 't%s.png' % s)).convert('L') for s in shots[g:g + 4]]
    except OSError: continue
    a, b, c = (ImageStat.Stat(ImageChops.difference(ims[i], ims[i + 1])).mean[0] for i in range(3))
    alt += max(a, c) > 3 * b + 1 or b > 3 * max(a, c) + 1
print(alt)
PY
)
  STAGE=$(grep -o 'stageName=.*' "$D/log.txt" | tail -1 | cut -d= -f2)
  ALIVE=$(echo "$R" | grep -q ALIVE && echo yes || echo no)
  CRASH=$( (grep -m1 '^CRASH:' "$D/log.txt"; echo "$R" | grep -o 'NEW CRASH REPORT: .*') | head -1 | cut -c1-60)
  UNIMPL=$(grep -c unimplemented "$D/log.txt")
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$N" "${STAGE:--}" "$ALIVE" "${CRASH:--}" "$UNIMPL" "$ALT" | tee -a "$OUT/summary.tsv"
done
"$ROOT/build/venv/bin/python" "$ROOT/tools/audit/sheet.py" "$OUT" '*_b.png'
