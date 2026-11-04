#!/usr/bin/env bash
# Records the CRT finish toggled live (off -> on -> off) on a private Xvfb (never the desktop):
# throwaway HOME/XDG dirs, private D-Bus, only the shipped examples/pixel-arcade.glsl shader.
# The switch is driven by the test-only SHADY_TEST_CRT_FILE hook; captions are burnt in by ffmpeg
# at the times this script wrote the file. Needs Xvfb, xdotool and ffmpeg (already installed).
# Usage: tests/record-crt.sh ./build/shady [out-dir]
set -euo pipefail
BIN=$(realpath "$1"); OUT=$(realpath "${2:-.}"); HERE=$(dirname "$(realpath "$0")")
R=$(mktemp -d); D=:$((90 + RANDOM % 9)); export DISPLAY=$D
trap 'kill ${AP:-} ${XV:-} 2>/dev/null || true; rm -rf "$R"' EXIT
Xvfb $D -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & XV=$!; sleep 1
cat > "$R/fonts.conf" <<F
<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig><include ignore_missing="yes">/etc/fonts/fonts.conf</include><dir>$HOME/.local/share/fonts</dir><cachedir>$R/fc</cachedir></fontconfig>
F
mkdir -p "$R/home" "$R/data" "$R/config" "$R/cache"; echo 0 > "$R/crt"
run() { FONTCONFIG_FILE="$R/fonts.conf" HOME="$R/home" XDG_DATA_HOME="$R/data" XDG_CONFIG_HOME="$R/config" XDG_CACHE_HOME="$R/cache" \
  SHADY_NO_NETWORK=1 SHADY_TEST_CRT_FILE="$R/crt" QT_QPA_PLATFORM=xcb ${T:+timeout $T} dbus-run-session -- "$BIN" >>"$R/app.log" 2>&1; }
T=6 run || true
rm -rf "$R"/data/shady/shaders/*            # only the fixture, so it is the first card
d="$R/data/shady/shaders/test-pixel-arcade"; mkdir -p "$d"; cp "$HERE/../examples/pixel-arcade.glsl" "$d/shader.glsl"
printf 'name = "Pixel Arcade"\nauthor = "Shady tests"\n' > "$d/shader.toml"
run & AP=$!
sleep 8
W=$(xdotool search --sync --onlyvisible --name . | tail -1)
xdotool windowsize $W 1280 800 windowmove $W 0 0; sleep 1
xdotool key --window $W Return; sleep 3; xdotool mousemove 1279 799; sleep 3   # open it, let the UI fade
ffmpeg -y -loglevel error -f x11grab -framerate 30 -video_size 1280x800 -i $D -t 16 -pix_fmt yuv420p -c:v libx264 -crf 18 "$R/raw.mp4" & FF=$!
sleep 5; echo 1 > "$R/crt"; sleep 6; echo 0 > "$R/crt"; wait $FF   # off 0-5 s, on 5-11 s, off 11-16 s
F=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
cap() { echo "drawtext=fontfile=$F:text='$1':enable='$2':x=24:y=24:fontsize=34:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=10"; }
ffmpeg -y -loglevel error -i "$R/raw.mp4" -vf "$(cap 'CRT off' 'lt(t,5)+gte(t,11)'),$(cap 'CRT on' 'between(t,5,11)')" \
  -pix_fmt yuv420p -c:v libx264 -crf 20 "$OUT/recording-crt-toggle.mp4"
ffmpeg -y -loglevel error -i "$OUT/recording-crt-toggle.mp4" -vf "fps=10,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" "$OUT/recording-crt-toggle.gif"
ffmpeg -y -loglevel error -ss 3 -i "$OUT/recording-crt-toggle.mp4" -frames:v 1 "$OUT/recording-crt-toggle-frame-off.png"
ffmpeg -y -loglevel error -ss 8.5 -i "$OUT/recording-crt-toggle.mp4" -frames:v 1 "$OUT/recording-crt-toggle-frame-on.png"
