#!/usr/bin/env bash
# Install regression test: `cmake --install` into a temp DESTDIR, hide the build
# tree, and run the installed binary from another cwd. Catches a package that
# ships a binary which only works next to its build dir (e.g. ./Shady/qmldir).
#
#   tests/installed.sh [build-dir]        (or: tests/installed.sh --destdir DIR to check an existing tree)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
if [[ ${1:-} == --destdir ]]; then
  ROOT=$2
else
  B=$(cd "${1:-./build}" && pwd)
  ROOT=$(mktemp -d /tmp/shady-dest.XXXX); trap 'rm -rf "$ROOT" "${HID:-}"' EXIT
  DESTDIR="$ROOT" cmake --install "$B" --prefix /usr >/dev/null
fi
fail() { echo "FAIL $*"; exit 1; }
echo "== installed files"
for f in usr/bin/shady usr/share/applications/shady.desktop usr/share/icons/hicolor/scalable/apps/shady.svg usr/share/icons/hicolor/16x16/apps/shady.png usr/share/icons/hicolor/256x256/apps/shady.png usr/share/licenses/shady/LICENSE; do
  [[ -s $ROOT/$f ]] || fail "missing $f"
done
D=$ROOT/usr/share/applications/shady.desktop
for k in 'Name=Shady' 'Exec=shady' 'Icon=shady' 'StartupWMClass=shady' 'Type=Application'; do grep -qx "$k" "$D" || fail "desktop file lacks $k"; done
command -v desktop-file-validate >/dev/null && { desktop-file-validate "$D" || fail "desktop-file-validate"; }
[[ $(find "$ROOT" -name qmldir | wc -l) == 0 ]] || fail "qmldir on disk; the QML module must be in the binary"
# Hide the build tree (if given) so nothing can be found beside it.
if [[ -n ${B:-} ]]; then HID="$B.hidden.$$"; mv "$B" "$HID"; trap 'mv "$HID" "$B"; rm -rf "$ROOT"' EXIT; fi
cd "$(mktemp -d /tmp/shady-cwd.XXXX)"
echo "== installed binary: --version, --self-test, UI load"
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= "$ROOT/usr/bin/shady" --version | grep -qx "shady .*" || fail "--version"
# headless.sh's UI test loads Main for real (offscreen, or private Xvfb).
SKIP_POTATO=1 "$HERE/headless.sh" "$ROOT/usr/bin/shady" | tail -3
echo "INSTALLED PASS"
