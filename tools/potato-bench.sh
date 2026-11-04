#!/bin/sh
# potato-bench: tiny start-up / RAM / size / frame-timing tracker.
# Usage: tools/potato-bench.sh [-n runs] [-i idle_s] [-o results.csv] -- BINARY [ARGS...]
# Env: POTATO_START_CMD = a command that starts the app and exits (e.g. "app --self-test")
#      to time start-up; unset = start-up skipped (na).
# Env: POTATO_COMMIT overrides the commit id (e.g. when run outside the repo).
# Deps: coreutils, awk; hyperfine if on PATH. One CSV line appended per run.
set -u
runs=5 idle=5 out=""
while [ $# -gt 0 ]; do case $1 in
  -n) runs=$2; shift 2;; -i) idle=$2; shift 2;; -o) out=$2; shift 2;;
  --) shift; break;; *) break;; esac; done
[ $# -ge 1 ] || { sed -n '2,6p' "$0"; exit 2; }
bin=$1; app=$(basename "$bin")
out=${out:-${XDG_CACHE_HOME:-$HOME/.cache}/potato-bench/$app.csv}
mkdir -p "$(dirname "$out")"
commit=${POTATO_COMMIT:-}; [ -n "$commit" ] || commit=$(git -C "$(dirname "$0")" rev-parse --short HEAD 2>/dev/null || echo na)
size=$(wc -c <"$bin")

now() { date +%s%N; }
startcmd=${POTATO_START_CMD:-}
cold=na warm=na
if [ -n "$startcmd" ]; then
  if command -v hyperfine >/dev/null 2>&1; then
    j=$(mktemp); hyperfine -N -w 2 -r "$runs" --export-json "$j" "$startcmd" >/dev/null 2>&1
    warm=$(awk -F: '/"mean"/{gsub(/[ ,]/,"",$2); printf "%.0f", $2*1000; exit}' "$j"); rm -f "$j"
  else
    t=0 i=0; while [ $i -lt "$runs" ]; do s=$(now); sh -c "$startcmd" >/dev/null 2>&1; t=$((t+$(now)-s)); i=$((i+1)); done
    warm=$((t/runs/1000000))
  fi
  # cold: only truly cold if page cache can be dropped (root); otherwise first run after evicting is approximated
  if [ -w /proc/sys/vm/drop_caches ]; then sync; echo 3 >/proc/sys/vm/drop_caches
    s=$(now); sh -c "$startcmd" >/dev/null 2>&1; cold=$((($(now)-s)/1000000)); fi
fi

# idle RAM (+ optional frame timing from QSG_RENDER_TIMING)
log=$(mktemp)
QSG_RENDER_TIMING=1 "$@" >"$log" 2>&1 & pid=$!
sleep "$idle"
rss=na pss=na
if [ -r /proc/$pid/smaps_rollup ]; then
  set -- $(awk '/^Rss:/{r=$2}/^Pss:/{p=$2}END{print r,p}' /proc/$pid/smaps_rollup); rss=$1 pss=$2
fi
kill $pid 2>/dev/null; wait $pid 2>/dev/null
# "Frame rendered ... in Xms" style lines; offscreen/software usually prints none -> na
frames=$(grep -Eo '(time in|rendered in|Frame [^,]*)[^0-9]*[0-9]+(\.[0-9]+)?ms' "$log" | grep -Eo '[0-9]+(\.[0-9]+)?ms' | tr -d m | tr -d s)
if [ -n "$frames" ]; then
  fm=$(echo "$frames" | awk '{s+=$1; if($1>m)m=$1; if($1>16.7)h++} END{printf "%.1f/%.1f/%d", s/NR, m, h+0}')
else fm=na; fi
rm -f "$log"

[ -s "$out" ] || echo "date,commit,app,size_b,cold_ms,warm_ms,idle_s,rss_kb,pss_kb,frame_avg/max/hitches" >"$out"
line="$(date -Iseconds),$commit,$app,$size,$cold,$warm,$idle,$rss,$pss,$fm"
echo "$line" >>"$out"; echo "$line"
