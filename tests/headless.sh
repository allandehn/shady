#!/usr/bin/env bash
# Headless tests for Shady. Never touches the user's desktop, data or running app:
# throwaway XDG_* dirs, a private D-Bus (dbus-run-session), offscreen Qt, and
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
  rm -rf "$T"/{data,config,cache}; mkdir -p "$T"/{data,config,cache}
  env -u DISPLAY -u WAYLAND_DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE \
    XDG_DATA_HOME="$T/data" XDG_CONFIG_HOME="$T/config" XDG_CACHE_HOME="$T/cache" \
    QT_FORCE_STDERR_LOGGING=1 "$@"
}
UI=(--ui-test); [[ -n $SHOT ]] && UI+=(--screenshot "$SHOT")

echo "== self-test"
iso QT_QPA_PLATFORM=offscreen timeout 120 dbus-run-session -- "$BIN" --self-test

echo "== UI test (offscreen)"
if iso QT_QPA_PLATFORM=offscreen timeout 120 dbus-run-session -- "$BIN" "${UI[@]}"; then
  echo "ALL PASS"; exit 0
fi
command -v xvfb-run >/dev/null || { echo "FAIL offscreen has no GL and xvfb-run is missing"; exit 1; }
echo "== UI test (xcb on private Xvfb)"
iso QT_QPA_PLATFORM=xcb timeout 120 xvfb-run -a -s "-screen 0 1280x900x24 -nolisten tcp" \
  dbus-run-session -- "$BIN" "${UI[@]}"
echo "ALL PASS"
