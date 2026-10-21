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
set -euo pipefail
BIN=${1:-./build/shady}
SHOT=${2:-}
T=$(mktemp -d /tmp/shady-test.XXXX); trap 'rm -rf "$T"' EXIT
iso() {
  rm -rf "$T"/{data,config,cache,home}; mkdir -p "$T"/{data,config,cache,home}
  # HOME too: Omarchy's palette lives at a fixed $HOME/.local/state path.
  env -u DISPLAY -u WAYLAND_DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE -u OMARCHY_PATH \
    HOME="$T/home" XDG_DATA_HOME="$T/data" XDG_CONFIG_HOME="$T/config" XDG_CACHE_HOME="$T/cache" \
    QT_FORCE_STDERR_LOGGING=1 "$@"
}
UI=(--ui-test); [[ -n $SHOT ]] && UI+=(--screenshot "$SHOT")

echo "== self-test"
iso QT_QPA_PLATFORM=offscreen timeout 120 dbus-run-session -- "$BIN" --self-test

# gui <timeout> <args...>: offscreen if it has GL, else xcb on a private Xvfb.
PLAT=offscreen
gui() {
  local t=$1; shift
  if [[ $PLAT == offscreen ]]; then
    iso QT_QPA_PLATFORM=offscreen timeout "$t" dbus-run-session -- "$BIN" "$@" && return 0
    command -v xvfb-run >/dev/null || { echo "FAIL offscreen has no GL and xvfb-run is missing"; exit 1; }
    PLAT=xcb; echo "== retrying with xcb on a private Xvfb"
  fi
  iso QT_QPA_PLATFORM=xcb timeout "$t" xvfb-run -a -s "-screen 0 1280x900x24 -nolisten tcp" \
    dbus-run-session -- "$BIN" "$@"
}

echo "== UI test"
gui 120 "${UI[@]}"
# Potato: the benchmark, iChannels on screen, auto-adapt and the too-heavy pause.
# SHADY_SOFTWARE=0 so a software rasterizer is treated as a (very slow) GPU.
echo "== potato test"
SHADY_SOFTWARE=0 gui 240 --potato-test
# Text-fit sweep over every screen, normal and narrow, in Danish and English.
echo "== Danish"
SHADY_LANG=da gui 120 --danish-shot ${SHOT:+--screenshot "${SHOT%.png}-da.png"}
echo "== English sweep"
SHADY_LANG=en gui 120 --danish-shot ${SHOT:+--screenshot "${SHOT%.png}-en.png"}
echo "ALL PASS"
