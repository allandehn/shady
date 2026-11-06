#!/usr/bin/env bash
# Records the grid-motion try-out on a private Xvfb (never the desktop):
# throwaway HOME/XDG dirs, private D-Bus, plus tests/fixtures/grid/*.glsl
# so the grid has several rows. Needs Xvfb, xdotool and ffmpeg (already installed).
# Usage: tests/record-grid.sh ./build/shady [out-dir]
set -euo pipefail
BIN=$(realpath "$1"); OUT=$(realpath "${2:-.}"); HERE=$(dirname "$(realpath "$0")")
R=$(mktemp -d); D=:$((90 + RANDOM % 9)); export DISPLAY=$D
trap 'kill ${AP:-} ${XV:-} 2>/dev/null || true; rm -rf "$R"' EXIT
Xvfb $D -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & XV=$!; sleep 1
cat > "$R/fonts.conf" <<F
<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig><include ignore_missing="yes">/etc/fonts/fonts.conf</include><dir>$HOME/.local/share/fonts</dir><cachedir>$R/fc</cachedir></fontconfig>
F
mkdir -p "$R/home" "$R/data" "$R/config" "$R/cache"
run() { FONTCONFIG_FILE="$R/fonts.conf" HOME="$R/home" XDG_DATA_HOME="$R/data" XDG_CONFIG_HOME="$R/config" XDG_CACHE_HOME="$R/cache" \
  SHADY_NO_NETWORK=1 QT_QPA_PLATFORM=xcb ${T:+timeout $T} dbus-run-session -- "$BIN" >>"$R/app.log" 2>&1; }
# First run seeds the bundled examples; then add the fixtures (one dir per slug).
T=6 run || true
for f in "$HERE"/fixtures/grid/*.glsl; do d="$R/data/shady/shaders/test-$(basename "$f" .glsl)"; mkdir -p "$d"; cp "$f" "$d/shader.glsl"
  n=$(sed -n 's|^// @name ||p' "$f" | head -1); printf 'name = "%s"\nauthor = "Shady tests"\n' "$n" > "$d/shader.toml"; done
run & AP=$!
sleep 8
W=$(xdotool search --sync --onlyvisible --name . | tail -1)
xdotool windowsize $W 1280 800 windowmove $W 0 0 mousemove 1279 799; sleep 2
grab() { ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i $D -frames:v 1 "$1"; }
ffmpeg -y -loglevel error -f x11grab -framerate 30 -video_size 1280x800 -i $D -t 8 -pix_fmt yuv420p -c:v libx264 "$OUT/recording-grid-motion.mp4" & FF=$!
sleep 0.6
k() { xdotool key --window $W "$1"; sleep "${2:-0.45}"; }
k Right; k Down; k Down; k Right; k Up
k Right 0.06; k Down 0.6          # fast course change mid-glide
k Left; k Down
xdotool keydown --window $W Right; sleep 0.9; xdotool keyup --window $W Right; sleep 0.6  # held: no lift, then it settles
wait $FF
k End 0.15; k Up 0.15; sleep 0.6   # an edge card: last column, above the New card
grab "$OUT/screenshot-grid-lift.png"
ffmpeg -y -loglevel error -i "$OUT/recording-grid-motion.mp4" -vf "fps=15,scale=800:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" "$OUT/recording-grid-motion.gif"
