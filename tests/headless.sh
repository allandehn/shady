#!/usr/bin/env bash
# Headless tests for Shady. Never touches the user's desktop, data or running app:
# throwaway XDG_* dirs, a private D-Bus (dbus-run-session), offscreen Qt.
#
#   tests/headless.sh [path/to/shady] [screenshot.png]
#
# Render test: QT_QPA_PLATFORM=offscreen first. Some distros (e.g. Debian) build
# the offscreen plugin without GL, which leaves only Qt's software scene graph
# (no ShaderEffect). Then it retries with xcb on a private Xvfb (xvfb-run -a),
# which is still headless and isolated from any real display.
set -euo pipefail
BIN=${1:-./build/shady}
SHOT=${2:-}
T=$(mktemp -d /tmp/shady-test.XXXX); trap 'rm -rf "$T"' EXIT
mkdir -p "$T"/{data,config,cache}
OUT=${SHOT:-$T/frame.png}
iso() { env -u DISPLAY -u WAYLAND_DISPLAY XDG_DATA_HOME="$T/data" XDG_CONFIG_HOME="$T/config" \
  XDG_CACHE_HOME="$T/cache" QT_FORCE_STDERR_LOGGING=1 "$@"; }

echo "== self-test"
iso QT_QPA_PLATFORM=offscreen timeout 60 dbus-run-session -- "$BIN" --self-test

echo "== render test (offscreen)"
if iso QT_QPA_PLATFORM=offscreen timeout 60 dbus-run-session -- "$BIN" --screenshot "$OUT" --after-ms 1500; then
  echo "ALL PASS"; exit 0
fi
command -v xvfb-run >/dev/null || { echo "FAIL offscreen has no GL and xvfb-run is missing"; exit 1; }
echo "== render test (xcb on private Xvfb)"
iso QT_QPA_PLATFORM=xcb timeout 60 xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp" \
  dbus-run-session -- "$BIN" --screenshot "$OUT" --after-ms 1500
echo "ALL PASS"
