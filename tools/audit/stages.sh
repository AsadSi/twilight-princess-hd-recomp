#!/bin/bash
# Usage: tools/audit/stages.sh [stage regex, default the real stages ^(D_|F_|R_)]
# Warps from save state 1 to the first map-select place of each stage (tools/warplist.py) and takes two TV
# shots per stage (game time 8 s and 16 s). Output in build/audit/stages/: <stage>_a.png / _b.png, <stage>.log,
# summary.tsv (stage, arrived, alive, crash, unimplemented calls, brightness of the second shot) and sheet.png
# (the second shots). Stages missing from the disc (cut developer stages in the list) never arrive.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/build/audit/stages; mkdir -p "$OUT"
FILTER=${1:-'^(D_|F_|R_)'}
printf 'stage\twarp\tarrived\talive\tcrash\tunimpl\tluma\tplace\n' > "$OUT/summary.tsv"
"$ROOT/build/venv/bin/python" "$ROOT/tools/warplist.py" "$ROOT/game" |
  awk -F'\t' '{split($1, a, ","); if (!(a[1] in seen)) { seen[a[1]] = 1; print } }' |
while IFS=$'\t' read -r WARP PLACE; do
  S=${WARP%%,*}
  [[ $S =~ $FILTER ]] || continue
  R=$(LOAD=1 WWHD_WARP=150:$WARP "$ROOT/tools/audit/run_route.sh" "$ROOT/tools/audit/routes/idle.txt" 26 8 16 < /dev/null 2>&1)
  D=$ROOT/build/audit/idle
  cp "$D/log.txt" "$OUT/$S.log"
  for k in a:8 b:16; do [ -f "$D/t${k#*:}.png" ] && cp "$D/t${k#*:}.png" "$OUT/${S}_${k%%:*}.png"; done
  ARRIVED=$(grep -q "stageName=$S\$" "$D/log.txt" && echo yes || echo no)
  ALIVE=$(echo "$R" | grep -q ALIVE && echo yes || echo no)
  CRASH=$( (grep -m1 '^CRASH:' "$D/log.txt"; echo "$R" | grep -o 'NEW CRASH REPORT: .*') | head -1 | cut -c1-60)
  UNIMPL=$(grep -c unimplemented "$D/log.txt")
  LUMA=$([ -f "$OUT/${S}_b.png" ] && "$ROOT/build/venv/bin/python" -c "from PIL import Image, ImageStat; import sys; print(round(ImageStat.Stat(Image.open(sys.argv[1]).convert('L')).mean[0]))" "$OUT/${S}_b.png" || echo -)
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$S" "$WARP" "$ARRIVED" "$ALIVE" "${CRASH:--}" "$UNIMPL" "$LUMA" "$PLACE" | tee -a "$OUT/summary.tsv"
done
"$ROOT/build/venv/bin/python" "$ROOT/tools/audit/sheet.py" "$OUT" '*_b.png'
