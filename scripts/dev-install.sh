#!/usr/bin/env bash
# Try the development version of Shady: builds main as a pacman package and
# installs it. For Arch / Omarchy.
#
#   curl -fsSL https://raw.githubusercontent.com/allandehn/shady/main/scripts/dev-install.sh | bash
#
# Steps: clone or update main in ~/.cache/shady-dev, build a package with
# makepkg (pkgver like 0.2.0.r5.g1a2b3c4, which sorts above 0.2.0 and below
# 0.2.1, so the next real release replaces it on `omarchy update`), run the
# headless self-tests (offscreen Qt, private D-Bus, throwaway dirs; nothing
# touches your desktop), then `sudo pacman -U` (you type your password).
# Never touches ~/.local/bin. Stops at the first failure.
#
# Options (flag or env):
#   --no-install   / SHADY_DEV_NO_INSTALL=1   build and test, but don't install
#   --skip-tests   / SHADY_DEV_SKIP_TESTS=1   skip tests/headless.sh
#   --nodeps       / SHADY_DEV_NODEPS=1       skip pacman dependency checks
#                                             (testing on a non-Arch box only)
#   SHADY_DEV_DIR=path                        cache dir (default ~/.cache/shady-dev)
#   SHADY_DEV_REPO=url                        git URL (default the GitHub repo)
# With curl, pass flags like this: curl -fsSL ... | bash -s -- --no-install
#
# Wrapped in main() so bash reads the whole script before running anything:
# with `curl | bash` stdin is the script, so no command here may read stdin
# (everything runs with </dev/null; sudo asks for the password on the tty).
set -euo pipefail

main() {
  local REPO=${SHADY_DEV_REPO:-https://github.com/allandehn/shady.git}
  local DIR=${SHADY_DEV_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/shady-dev}
  local NO_INSTALL=${SHADY_DEV_NO_INSTALL:-0} SKIP_TESTS=${SHADY_DEV_SKIP_TESTS:-0} NODEPS=${SHADY_DEV_NODEPS:-0} a
  for a in "$@"; do
    case $a in
      --no-install) NO_INSTALL=1 ;;
      --skip-tests) SKIP_TESTS=1 ;;
      --nodeps) NODEPS=1 ;;
      -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}" 2>/dev/null || echo "see the header of scripts/dev-install.sh"; return 0 ;;
      *) die "unknown option: $a (use --no-install or --skip-tests)" ;;
    esac
  done
  export GIT_TERMINAL_PROMPT=0 GIT_EDITOR=false

  step "Checking tools"
  (( EUID != 0 )) || die "run this as your normal user, not root (makepkg refuses root; sudo is used only for the install)"
  local miss=() t
  for t in git makepkg cmake ninja dbus-run-session timeout; do command -v "$t" >/dev/null || miss+=("$t"); done
  if (( ${#miss[@]} )); then
    die "missing: ${miss[*]}
  Install with: sudo pacman -S --needed git base-devel cmake ninja dbus"
  fi
  if (( ! NO_INSTALL )); then command -v pacman >/dev/null || die "pacman not found: this script is for Arch / Omarchy"; fi

  step "Getting latest main into $DIR"
  if [[ -d $DIR/src/.git ]]; then
    git -C "$DIR/src" fetch -q --tags --force origin main </dev/null
    git -C "$DIR/src" checkout -q -B main FETCH_HEAD </dev/null
    git -C "$DIR/src" reset -q --hard FETCH_HEAD </dev/null
    git -C "$DIR/src" clean -qfdx </dev/null
  else
    mkdir -p "$DIR"; rm -rf "$DIR/src"
    git clone -q --branch main "$REPO" "$DIR/src" </dev/null
  fi
  local SRC=$DIR/src TAG N HASH BASE VER
  TAG=$(git -C "$SRC" describe --tags --abbrev=0 --match 'v[0-9]*' HEAD </dev/null) || die "no v* release tag found"
  N=$(git -C "$SRC" rev-list --count "$TAG..HEAD" </dev/null)
  HASH=$(git -C "$SRC" rev-parse --short=7 HEAD </dev/null)
  BASE=${TAG#v}; VER=$BASE.r$N.g$HASH
  echo "main is $HASH, $N commit(s) after $TAG -> package version $VER"
  if command -v vercmp >/dev/null; then
    (( $(vercmp "$VER" "$BASE") > 0 )) || die "$VER does not sort above $BASE"
  fi

  step "Building the package (makepkg)"
  local B=$DIR/build; rm -rf "$B"; mkdir -p "$B"
  git -C "$SRC" archive --format=tar.gz --prefix="shady-$VER/" -o "$B/shady-$VER.tar.gz" HEAD </dev/null
  # The release PKGBUILD, pointed at that tarball. The binary reports the
  # base version (CMakeLists), so check() compares against $BASE.
  sed -e "s/^pkgver=.*/pkgver=$VER/" -e 's/^pkgrel=.*/pkgrel=1/' \
      -e "s|^source=.*|source=(\"\$pkgname-\$pkgver.tar.gz\")|" \
      -e "s|\"shady \$pkgver\"|\"shady $BASE\"|" \
      "$SRC/packaging/PKGBUILD" > "$B/PKGBUILD"
  if ! grep -q "^pkgver=$VER\$" "$B/PKGBUILD" || ! grep -q "\"shady $BASE\"" "$B/PKGBUILD"; then
    die "could not adapt packaging/PKGBUILD (has its format changed?)"
  fi
  local deps
  deps=$(cd "$B" && makepkg --printsrcinfo </dev/null | sed -n 's/^\t\(make\|check\)\?depends = //p' | tr '\n' ' ')
  local MP=(-f --noconfirm)
  if (( NODEPS )); then MP+=(--nodeps)
  elif command -v pacman >/dev/null; then
    local need
    # shellcheck disable=SC2086  # $deps is a word list on purpose
    need=$(pacman -T $deps 2>/dev/null </dev/null | tr '\n' ' ' || true)
    [[ -z ${need// } ]] || die "missing build packages: $need
  Install with: sudo pacman -S --needed $need"
  fi
  ( cd "$B" && env -u DISPLAY -u WAYLAND_DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE -u QT_QPA_PLATFORMTHEME \
      makepkg "${MP[@]}" </dev/null >"$B/makepkg.log" 2>&1 ) \
    || { tail -25 "$B/makepkg.log" >&2; die "makepkg failed (full log: $B/makepkg.log)"; }
  local PKG
  PKG=$(cd "$B" && find . -maxdepth 1 -name "shady-$VER-1-*.pkg.tar*" ! -name "*.sig" -printf '%f\n' | head -1)
  [[ -n $PKG ]] || die "makepkg made no package (log: $B/makepkg.log)"
  echo "built $B/$PKG"

  if (( SKIP_TESTS )); then
    step "Skipping headless tests (--skip-tests)"
  else
    step "Headless self-tests (offscreen, private D-Bus, throwaway dirs)"
    ( cd "$B/src/shady-$VER" && timeout 1200 tests/headless.sh "$B/src/build/shady" </dev/null >"$B/test.log" 2>&1 ) \
      || { grep -E 'FAIL' "$B/test.log" >&2 || tail -25 "$B/test.log" >&2; die "headless tests failed (log: $B/test.log)"; }
    grep -q 'ALL PASS' "$B/test.log" || die "tests did not report ALL PASS (log: $B/test.log)"
    echo "ALL PASS"
  fi

  if (( NO_INSTALL )); then
    step "Done (--no-install). Package: $B/$PKG"
    echo "Install it later with: sudo pacman -U $B/$PKG"
    return 0
  fi

  step "Installing (sudo pacman -U; type your password if asked)"
  sudo pacman -U --noconfirm "$B/$PKG" </dev/null || die "pacman -U failed"

  step "Installed: $(pacman -Q shady)"
  echo "The next release (above $BASE) replaces this on 'omarchy update'."
}

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

main "$@"
