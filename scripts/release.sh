#!/usr/bin/env bash
# Release Shady: bump, build, test, commit, tag, push, sign the package,
# update the pacman repo (GitHub release "repo") and publish a GitHub release.
# Modelled on OmaEar's scripts/release.sh.
#
#   scripts/release.sh [--dry-run] X.Y.Z [NOTES_FILE]
#
# Release notes: NOTES_FILE, else $NOTES (text). A real run with neither dies at
# once; no editor or prompt is ever opened (a dry run uses placeholder notes).
#
# Never hangs silently: each step prints a timestamped "[HH:MM:SS] ==> Step"
# line and its commands run under coreutils `timeout` (per-step limits below,
# scale them all with SHADY_TIMEOUT_SCALE=2 etc.). A timeout fails loudly with
# the step name. stdin is /dev/null for anything that could prompt, and
# GIT_EDITOR/EDITOR=false, GIT_TERMINAL_PROMPT=0. gpg signing uses the normal
# gpg-agent pinentry, so a passphrase dialog may pop up on the desktop:
# preflight makes a test signature (300s), and makepkg/repo-add signing
# allow at least 300s. A timeout says a dialog may be waiting there.
# --dry-run does everything locally (bump, build, tests, signed package and
# repo db from the working tree) but never commits, tags, pushes, uploads or
# publishes, and restores CMakeLists.txt / PKGBUILD at the end. A dry run
# skips the remote checks (origin sync, gh login, remote tag) so it also works
# before the GitHub repo exists.
#
# Tests are headless only (tests/headless.sh): offscreen Qt, a private D-Bus
# and throwaway XDG_* dirs. Nothing here sends input to the desktop.
# Stops at the first failure.
set -euo pipefail

REPO=allandehn/shady
KEY=${SHADY_SIGN_KEY:-AC6FF13C76D8916CCCF2DA05C3225162B5257DB4}
DRY=0
[[ ${1:-} == --dry-run ]] && { DRY=1; shift; }
VER=${1:-}; NOTES_FILE=${2:-}

export GIT_EDITOR=false EDITOR=false VISUAL=false GIT_TERMINAL_PROMPT=0 GH_PROMPT_DISABLED=1
export GIT_SSH_COMMAND=${GIT_SSH_COMMAND:-ssh -o BatchMode=yes}
command -v timeout >/dev/null || { echo "missing tool: timeout (coreutils)" >&2; exit 1; }
SCALE=${SHADY_TIMEOUT_SCALE:-1}
STEP=startup
step() { STEP=$*; printf '\n\033[1;36m[%s] ==> %s\033[0m\n' "$(date +%H:%M:%S)" "$*"; }
die()  { printf '\n\033[1;31m[%s] ERROR (step: %s): %s\033[0m\n' "$(date +%H:%M:%S)" "$STEP" "$*" >&2; exit 1; }
trap 'die "failed at line $LINENO: $BASH_COMMAND"' ERR
# t SECS cmd...: run cmd with stdin from /dev/null under a timeout; on timeout
# die naming the current step. Other failures return the command's status.
t() {
  local secs=$(( $1 * SCALE )) rc=0; shift
  timeout -k 10 "$secs" "$@" </dev/null || rc=$?
  if (( rc == 124 || rc == 137 )); then
    die "TIMED OUT after ${secs}s: $*${TIMEOUT_HINT:+ ($TIMEOUT_HINT)}"
  fi
  return $rc
}

[[ $VER =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "usage: $0 [--dry-run] X.Y.Z [NOTES_FILE]"
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
ROOT=$PWD
PKG=shady-$VER-1-x86_64.pkg.tar.zst
WORK=$(mktemp -d /tmp/shady-rel.XXXX)
cleanup() {
  rm -rf "$WORK"
  if (( DRY )); then git -C "$ROOT" checkout -q -- CMakeLists.txt packaging/PKGBUILD; fi
}
trap cleanup EXIT

step "Preflight"
[[ -z $(git status --porcelain) ]] || die "working tree not clean (commit or stash first)"
[[ $(git branch --show-current) == main ]] || die "not on main"
for t in cmake ninja makepkg repo-add dbus-run-session gpg curl; do command -v $t >/dev/null || die "missing tool: $t"; done
t 30 gpg --batch --list-secret-keys "$KEY" >/dev/null 2>&1 || die "signing key $KEY not in gpg"
bash -n install.sh || die "install.sh has a syntax error"
if command -v shellcheck >/dev/null; then shellcheck install.sh || die "shellcheck failed on install.sh"; fi
git rev-parse -q --verify "refs/tags/v$VER" >/dev/null && die "tag v$VER exists locally"
echo "Test signature with $KEY (gpg's passphrase dialog may pop up on the desktop)..."
TIMEOUT_HINT="a gpg passphrase dialog may be waiting on the desktop; enter the passphrase there and rerun"
t 300 gpg -u "$KEY" --clearsign >/dev/null 2>&1 \
  || die "test signature with $KEY failed (passphrase dialog cancelled or wrong passphrase?)"
TIMEOUT_HINT=
if (( ! DRY )); then
  command -v gh >/dev/null || die "missing tool: gh"
  t 120 git fetch -q origin main
  [[ $(git rev-parse HEAD) == $(git rev-parse origin/main) ]] || die "main is not in sync with origin/main (pull/push first)"
  t 60 gh auth status >/dev/null 2>&1 || die "gh is not logged in (run: gh auth login)"
  t 60 git ls-remote --exit-code --tags origin "v$VER" >/dev/null 2>&1 && die "tag v$VER exists on origin"
fi

step "Release notes"
NOTES_OUT=$WORK/notes.md
if [[ -n $NOTES_FILE ]]; then [[ -f $NOTES_FILE ]] || die "notes file not found: $NOTES_FILE"; cp "$NOTES_FILE" "$NOTES_OUT"
elif [[ -n ${NOTES:-} ]]; then printf '%s\n' "$NOTES" > "$NOTES_OUT"
elif (( DRY )); then echo "(dry run notes)" > "$NOTES_OUT"
else die "no release notes: pass NOTES_FILE or set \$NOTES (no editor is opened)"
fi
[[ -s $NOTES_OUT ]] || die "release notes are empty"

step "Bump version to $VER"
sed -i "s/^project(shady VERSION [0-9.]*/project(shady VERSION $VER/" CMakeLists.txt
sed -i "s/^pkgver=.*/pkgver=$VER/; s/^pkgrel=.*/pkgrel=1/" packaging/PKGBUILD
grep -q "project(shady VERSION $VER " CMakeLists.txt || die "version bump in CMakeLists.txt failed"

step "Build"
rm -rf build
t 300 cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
t 900 cmake --build build >/dev/null
[[ $(QT_QPA_PLATFORMTHEME= QT_QPA_PLATFORM=offscreen t 30 ./build/shady --version) == "shady $VER" ]] || die "built binary reports $(QT_QPA_PLATFORMTHEME= QT_QPA_PLATFORM=offscreen ./build/shady --version 2>&1)"

step "Headless tests (offscreen, private D-Bus, throwaway XDG dirs)"
t 900 tests/headless.sh ./build/shady > "$WORK/test.log" 2>&1 || { grep -E 'FAIL' "$WORK/test.log" >&2 || true; die "headless tests failed"; }
grep -q 'ALL PASS' "$WORK/test.log" || die "headless tests did not report ALL PASS"

if (( DRY )); then
  step "[dry-run] skip commit, tag and push; package the working tree instead"
  SNAP=$(t 60 git stash create 2>/dev/null || true); SNAP=${SNAP:-HEAD}
  t 120 git archive --format=tar.gz --prefix="shady-$VER/" -o "$WORK/shady-$VER.tar.gz" "$SNAP"
else
  step "Commit, tag v$VER, push"
  git diff --quiet HEAD || t 60 git commit -qam "Release $VER"
  t 60 git tag -a "v$VER" -m "Shady $VER"
  t 180 git push -q origin main
  t 180 git push -q origin "v$VER"
fi

step "Build and sign the package (makepkg --sign)"
cp packaging/PKGBUILD "$WORK/"
TIMEOUT_HINT="a gpg passphrase dialog may be waiting on the desktop"
( cd "$WORK" && t 1200 makepkg --sign --key "$KEY" -f --noconfirm >"$WORK/makepkg.log" 2>&1 ) \
  || { tail -20 "$WORK/makepkg.log" >&2; die "makepkg failed (if gpg asked for a passphrase, unlock the key with: echo | gpg --clearsign -u $KEY >/dev/null, then rerun)"; }

step "Signed repo database"
mkdir -p "$WORK/r" "$WORK/up"
cp "$WORK/$PKG" "$WORK/$PKG.sig" "$WORK/r/"
( cd "$WORK/r" && t 300 repo-add -q -s -k "$KEY" shady.db.tar.gz "$PKG" )
for f in db files; do
  cp "$WORK/r/shady.$f.tar.gz" "$WORK/up/shady.$f"
  cp "$WORK/r/shady.$f.tar.gz.sig" "$WORK/up/shady.$f.sig"
done
cp "$WORK/$PKG" "$WORK/$PKG.sig" "$WORK/up/"
for f in shady.db shady.files "$PKG"; do t 30 gpg --batch --verify "$WORK/up/$f.sig" "$WORK/up/$f" 2>/dev/null || die "bad signature on $f"; done
tar tzf "$WORK/up/shady.db" | grep -q "^shady-$VER-1/" || die "repo db does not list shady-$VER-1"
TIMEOUT_HINT=

if (( DRY )); then
  step "[dry-run] would upload to release 'repo': $(cd "$WORK/up" && echo *)"
  step "[dry-run] would create release 'Shady $VER' (latest). Notes:"; cat "$NOTES_OUT"
  step "Dry run OK. Version files restored; nothing pushed or published."
  exit 0
fi

step "Upload to the 'repo' release"
t 60 gh release view repo -R "$REPO" >/dev/null 2>&1 \
  || t 120 gh release create repo -R "$REPO" --title "Package repository" --notes "Signed pacman repo for Shady." --latest=false
OLD=$(t 60 gh release view repo -R "$REPO" --json assets -q '.assets[].name' | grep -E '^shady-.*\.pkg\.tar\.zst(\.sig)?$' | grep -v "^$PKG" || true)
t 900 gh release upload repo -R "$REPO" --clobber "$WORK"/up/*
for a in $OLD; do t 60 gh release delete-asset repo "$a" -R "$REPO" -y; done

step "Publish release Shady $VER"
t 120 gh release create "v$VER" -R "$REPO" --title "Shady $VER" --notes-file "$NOTES_OUT" --latest --verify-tag

step "Public verification"
sleep 3
TAG=$(curl -fsSL --max-time 60 "https://api.github.com/repos/$REPO/releases/latest" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p')
[[ $TAG == "v$VER" ]] || die "latest release is '$TAG', expected v$VER"
curl -fsSL --max-time 120 "https://github.com/$REPO/releases/download/repo/shady.db" | tar tz | grep -q "^shady-$VER-1/" \
  || die "public repo db does not list shady-$VER-1"
for u in "https://raw.githubusercontent.com/$REPO/main/install.sh" "https://github.com/$REPO/releases/download/repo/shady.db.sig" \
         "https://github.com/allandehn/omaear/releases/download/repo/omaear.asc"; do
  curl -fsIL --max-time 60 -o /dev/null "$u" || die "installer URL not reachable: $u"
done
step "Released Shady $VER: https://github.com/$REPO/releases/tag/v$VER"
