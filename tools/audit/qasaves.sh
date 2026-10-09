#!/bin/bash
# Usage: tools/audit/qasaves.sh [save regex, default all]
# Plays from each of the disc's QA saves (content/DebugSaves/QASaves/ZTP<nn>.dat; HeroMode/ as Hero<nn>), loaded as
# Quest Log 1 through the menus (routes/qasave.txt): shots on arrival (69 s), after a walk (75 s) and in Collection
# (81 s). Output in build/audit/qasaves/: <save>_a/_b/_c.png, <save>.log, summary.tsv (save, the save's place, the
# stage reached, alive, crash, unimplemented calls, brightness of the walk shot) and sheet.png (the walk shots).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/build/audit/qasaves; mkdir -p "$OUT"
FILTER=${1:-.}
printf 'save\tplace\tstage\talive\tcrash\tunimpl\tluma\n' > "$OUT/summary.tsv"
for F in "$ROOT"/game/content/DebugSaves/QASaves/ZTP*.dat "$ROOT"/game/content/DebugSaves/QASaves/HeroMode/ZTP*.dat; do
  N=$(basename "$F" .dat); [[ $F == */HeroMode/* ]] && N=Hero${N#ZTP}
  [[ $N =~ $FILTER ]] || continue
  S=$ROOT/build/audit/saves/qasave; rm -rf "$S"; mkdir -p "$S/user"; cp "$F" "$S/user/ZTP00.dat"
  PLACE=$(dd if="$F" bs=1 skip=88 count=8 2>/dev/null | tr -d '\0'),$(od -An -tu1 -j96 -N2 "$F" | awk 'NF {print $1","$2}')
  R=$(SAVE=qasave "$ROOT/tools/audit/run_route.sh" "$ROOT/tools/audit/routes/qasave.txt" 90 69 75 81 < /dev/null 2>&1)
  D=$ROOT/build/audit/qasave
  cp "$D/log.txt" "$OUT/$N.log"
  for k in a:69 b:75 c:81; do [ -f "$D/t${k#*:}.png" ] && cp "$D/t${k#*:}.png" "$OUT/${N}_${k%%:*}.png"; done
  STAGE=$(grep -o 'stageName=.*' "$D/log.txt" | tail -1 | cut -d= -f2)
  ALIVE=$(echo "$R" | grep -q ALIVE && echo yes || echo no)
  CRASH=$( (grep -m1 '^CRASH:' "$D/log.txt"; echo "$R" | grep -o 'NEW CRASH REPORT: .*') | head -1 | cut -c1-60)
  UNIMPL=$(grep -c unimplemented "$D/log.txt")
  LUMA=$([ -f "$OUT/${N}_b.png" ] && "$ROOT/build/venv/bin/python" -c "from PIL import Image, ImageStat; import sys; print(round(ImageStat.Stat(Image.open(sys.argv[1]).convert('L')).mean[0]))" "$OUT/${N}_b.png" || echo -)
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$N" "$PLACE" "${STAGE:--}" "$ALIVE" "${CRASH:--}" "$UNIMPL" "$LUMA" | tee -a "$OUT/summary.tsv"
done
"$ROOT/build/venv/bin/python" "$ROOT/tools/audit/sheet.py" "$OUT" '*_b.png'
