#!/bin/bash
# Usage: [ROUTE=play] tools/audit/qasaves.sh [save regex, default all]
# Plays from each of the disc's QA saves (content/DebugSaves/QASaves/ZTP<nn>.dat; HeroMode/ as Hero<nn>), loaded as
# Quest Log 1 through the menus. ROUTE=qasave (default, routes/qasave.txt): shots on arrival (69 s), after a walk
# (75 s) and in Collection (81 s); ROUTE=map (routes/map.txt): the same shot times, with the map open at 75 and 81 s. ROUTE=play (routes/play.txt): 2.5 minutes of fixed pseudo-random play; four
# consecutive TV frames at 90, 130, 170 and 210 s, and "alt" counts the groups whose frames alternate (one frame of
# each pair differs much more than the other: something drawn differently on hold passes; run with WWHD_INTERP=1).
# Output in build/audit/qasaves/: <save>_a/_b/_c.png (play: <save>_b.png, the 130 s frame), <save>.log, summary.tsv
# (save, the save's place, the stage reached, alive, crash, unimplemented calls, brightness of the b shot, alt) and
# sheet.png (the b shots).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/build/audit/qasaves; mkdir -p "$OUT"
FILTER=${1:-.}
ROUTE=${ROUTE:-qasave}
if [ "$ROUTE" = play ]; then
  TOTAL=235; SHOTS=()
  for T in 90 130 170 210; do for I in 0 1 2 3; do SHOTS+=("$(perl -e "printf '%.4f', $T + ($I + 0.25) / 60")"); done; done
elif [ "$ROUTE" = map ]; then
  TOTAL=95; SHOTS=(69 75 81)
else
  TOTAL=90; SHOTS=(69 75 81)
fi
printf 'save\tplace\tstage\talive\tcrash\tunimpl\tluma\talt\n' > "$OUT/summary.tsv"
for F in "$ROOT"/game/content/DebugSaves/QASaves/ZTP*.dat "$ROOT"/game/content/DebugSaves/QASaves/HeroMode/ZTP*.dat; do
  N=$(basename "$F" .dat); [[ $F == */HeroMode/* ]] && N=Hero${N#ZTP}
  [[ $N =~ $FILTER ]] || continue
  S=$ROOT/build/audit/saves/qasave; rm -rf "$S"; mkdir -p "$S/user"; cp "$F" "$S/user/ZTP00.dat"
  PLACE=$(dd if="$F" bs=1 skip=88 count=8 2>/dev/null | tr -d '\0'),$(od -An -tu1 -j96 -N2 "$F" | awk 'NF {print $1","$2}')
  R=$(SAVE=qasave "$ROOT/tools/audit/run_route.sh" "$ROOT/tools/audit/routes/$ROUTE.txt" $TOTAL "${SHOTS[@]}" < /dev/null 2>&1)
  D=$ROOT/build/audit/$ROUTE
  cp "$D/log.txt" "$OUT/$N.log"
  ALT=-
  if [ "$ROUTE" = play ]; then
    cp "$D/t${SHOTS[4]}.png" "$OUT/${N}_b.png" 2>/dev/null
    ALT=$("$ROOT/build/venv/bin/python" - "$D" "${SHOTS[@]}" <<'EOF'
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
EOF
)
  else
    for k in a:69 b:75 c:81; do [ -f "$D/t${k#*:}.png" ] && cp "$D/t${k#*:}.png" "$OUT/${N}_${k%%:*}.png"; done
  fi
  STAGE=$(grep -o 'stageName=.*' "$D/log.txt" | tail -1 | cut -d= -f2)
  ALIVE=$(echo "$R" | grep -q ALIVE && echo yes || echo no)
  CRASH=$( (grep -m1 '^CRASH:' "$D/log.txt"; echo "$R" | grep -o 'NEW CRASH REPORT: .*') | head -1 | cut -c1-60)
  UNIMPL=$(grep -c unimplemented "$D/log.txt")
  LUMA=$([ -f "$OUT/${N}_b.png" ] && "$ROOT/build/venv/bin/python" -c "from PIL import Image, ImageStat; import sys; print(round(ImageStat.Stat(Image.open(sys.argv[1]).convert('L')).mean[0]))" "$OUT/${N}_b.png" || echo -)
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$N" "$PLACE" "${STAGE:--}" "$ALIVE" "${CRASH:--}" "$UNIMPL" "$LUMA" "$ALT" | tee -a "$OUT/summary.tsv"
done
"$ROOT/build/venv/bin/python" "$ROOT/tools/audit/sheet.py" "$OUT" '*_b.png'
