#!/usr/bin/env bash
# Headless tests for Shady. Never touches the user's desktop, data or running app:
# a throwaway HOME and XDG_* dirs, a private D-Bus (dbus-run-session), offscreen Qt, and
# DISPLAY / WAYLAND_DISPLAY / HYPRLAND_INSTANCE_SIGNATURE removed.
#
#   tests/headless.sh [path/to/shady] [screenshot.png]
#
# UI test: QT_QPA_PLATFORM=offscreen first. Some distros (e.g. Debian) build
# the offscreen plugin without GL, which leaves only Qt's software scene graph
# (no ShaderEffect). Then it retries with xcb on a private Xvfb (xvfb-run -a),
# which is still headless and isolated from any real display.
# Each run starts from a fresh library, so first-run seeding is tested too.
# The phases are independent (own dirs, D-Bus and Xvfb each), so they run in
# parallel; each one's output is buffered and printed in the usual order, and
# the exit code is the first failing phase's. SHADY_TEST_JOBS=1 runs them one
# after another, streaming (handy when debugging a hang).
set -euo pipefail
BIN=${1:-./build/shady}
SHOT=${2:-}
T=$(mktemp -d /tmp/shady-test.XXXX); trap 'rm -rf "$T" 2>/dev/null || { sleep 1; rm -rf "$T" 2>/dev/null; } || true' EXIT
iso() {
  # A new dir per run instead of rm -rf: D-Bus-activated helpers of the last run
  # (portals) can still be writing into its cache, and rm then fails under set -e.
  local R; R=$(mktemp -d "$T/run.XXXX"); mkdir -p "$R"/{data,config,cache,home}
  # HOME too: Omarchy's palette lives at a fixed $HOME/.local/state path.
  env -u DISPLAY -u WAYLAND_DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE -u OMARCHY_PATH -u QT_QPA_PLATFORMTHEME -u QT_STYLE_OVERRIDE -u GTK_THEME \
    HOME="$R/home" XDG_DATA_HOME="$R/data" XDG_CONFIG_HOME="$R/config" XDG_CACHE_HOME="$R/cache" SHADY_NO_NETWORK=1 \
    QT_FORCE_STDERR_LOGGING=1 QSG_RHI_BACKEND="${QSG_RHI_BACKEND:-opengl}" "$@"
}
UI=(--ui-test); [[ -n $SHOT ]] && UI+=(--screenshot "$SHOT")

# gui <timeout> <args...>: offscreen if it has GL, else xcb on a private Xvfb.
# A server number per phase (-n, with -a still skipping taken ones) so parallel
# xvfb-runs don't race for the same display.
PLAT=offscreen
gui() {
  local t=$1; shift
  if [[ $PLAT == offscreen ]]; then
    iso QT_QPA_PLATFORM=offscreen timeout "$t" dbus-run-session -- "$BIN" "$@" && return 0
    command -v xvfb-run >/dev/null || { echo "FAIL offscreen has no GL and xvfb-run is missing"; return 1; }
    PLAT=xcb; echo "== retrying with xcb on a private Xvfb"
  fi
  iso QT_QPA_PLATFORM=xcb timeout "$t" xvfb-run -a -n "$XNUM" -s "-screen 0 1280x900x24 -nolisten tcp" \
    dbus-run-session -- "$BIN" "$@"
}

P_NAMES=(); P_CMDS=()
phase() { P_NAMES+=("$1"); P_CMDS+=("$2"); }
phase "self-test" 'iso QT_QPA_PLATFORM=offscreen timeout 120 dbus-run-session -- "$BIN" --self-test'
phase "UI test" 'gui 120 "${UI[@]}"'
# Potato: the benchmark, iChannels on screen, auto-adapt and the too-heavy pause.
# SHADY_SOFTWARE=0 so a software rasterizer is treated as a (very slow) GPU.
[[ -z ${SKIP_POTATO:-} ]] && phase "potato test" 'SHADY_SOFTWARE=0 gui 240 --potato-test'
# Text-fit sweep over every screen, normal and narrow, in Danish and English.
phase "Danish" 'SHADY_LANG=da gui 120 --danish-shot ${SHOT:+--screenshot "${SHOT%.png}-da.png"}'
phase "English sweep" 'SHADY_LANG=en gui 120 --danish-shot ${SHOT:+--screenshot "${SHOT%.png}-en.png"}'

XBASE=$(( 200 + (RANDOM % 150) * 20 ))
RC=0
if [[ ${SHADY_TEST_JOBS:-} == 1 ]]; then
  for i in "${!P_NAMES[@]}"; do
    echo "== ${P_NAMES[i]}"; XNUM=$((XBASE + i)); s=$SECONDS
    ( eval "${P_CMDS[i]}" ) || { RC=$?; exit $RC; }
    echo "-- ${P_NAMES[i]}: $((SECONDS - s)) s"
  done
else
  PIDS=()
  for i in "${!P_NAMES[@]}"; do
    XNUM=$((XBASE + i))
    ( s=$SECONDS; set +e; ( eval "${P_CMDS[i]}" ) </dev/null >"$T/phase.$i" 2>&1; r=$?
      echo "-- ${P_NAMES[i]}: $((SECONDS - s)) s" >>"$T/phase.$i"; exit $r ) &
    PIDS+=($!)
  done
  for i in "${!P_NAMES[@]}"; do
    r=0; wait "${PIDS[i]}" || r=$?
    echo "== ${P_NAMES[i]}"; cat "$T/phase.$i"
    (( r == 0 )) || { echo "FAIL phase ${P_NAMES[i]} (exit $r)"; (( RC )) || RC=$r; }
  done
  (( RC == 0 )) || exit $RC
fi
# Install regression (skipped when this run is already testing an installed binary).
# After the others: it hides the build tree while it runs.
if [[ -z ${SKIP_POTATO:-} && -d $(dirname "$BIN") && -f $(dirname "$BIN")/cmake_install.cmake ]]; then
  echo "== installed-package test"
  "$(dirname "$0")/installed.sh" "$(dirname "$BIN")"
fi
echo "ALL PASS"
