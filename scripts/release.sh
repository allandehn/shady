#!/usr/bin/env bash
# Release Shady: bump, build, test, commit, tag, push, sign the package,
# update the pacman repo (GitHub release "repo") and publish a GitHub release.
# Modelled on OmaEar's scripts/release.sh.
#
#   scripts/release.sh [--dry-run] X.Y.Z [NOTES_FILE]
#
# Release notes: NOTES_FILE, else $NOTES (text), else $EDITOR opens a template.
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

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
trap 'die "failed at line $LINENO: $BASH_COMMAND"' ERR

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
gpg --list-secret-keys "$KEY" >/dev/null 2>&1 || die "signing key $KEY not in gpg"
git rev-parse -q --verify "refs/tags/v$VER" >/dev/null && die "tag v$VER exists locally"
if (( ! DRY )); then
  command -v gh >/dev/null || die "missing tool: gh"
  git fetch -q origin main
  [[ $(git rev-parse HEAD) == $(git rev-parse origin/main) ]] || die "main is not in sync with origin/main (pull/push first)"
  gh auth status >/dev/null 2>&1 || die "gh is not logged in (run: gh auth login)"
  git ls-remote --exit-code --tags origin "v$VER" >/dev/null 2>&1 && die "tag v$VER exists on origin"
fi

step "Release notes"
NOTES_OUT=$WORK/notes.md
if [[ -n $NOTES_FILE ]]; then [[ -f $NOTES_FILE ]] || die "notes file not found: $NOTES_FILE"; cp "$NOTES_FILE" "$NOTES_OUT"
elif [[ -n ${NOTES:-} ]]; then printf '%s\n' "$NOTES" > "$NOTES_OUT"
elif (( DRY )); then echo "(dry run notes)" > "$NOTES_OUT"
else printf 'Shady %s\n\n- \n\nUpdate with `omarchy update`.\n' "$VER" > "$NOTES_OUT"; ${EDITOR:-nvim} "$NOTES_OUT"
fi
[[ -s $NOTES_OUT ]] || die "release notes are empty"

step "Bump version to $VER"
sed -i "s/^project(shady VERSION [0-9.]*/project(shady VERSION $VER/" CMakeLists.txt
sed -i "s/^pkgver=.*/pkgver=$VER/; s/^pkgrel=.*/pkgrel=1/" packaging/PKGBUILD
grep -q "project(shady VERSION $VER " CMakeLists.txt || die "version bump in CMakeLists.txt failed"

step "Build"
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev >/dev/null
cmake --build build >/dev/null
[[ $(QT_QPA_PLATFORMTHEME= QT_QPA_PLATFORM=offscreen ./build/shady --version) == "shady $VER" ]] || die "built binary reports $(QT_QPA_PLATFORMTHEME= QT_QPA_PLATFORM=offscreen ./build/shady --version 2>&1)"

step "Headless tests (offscreen, private D-Bus, throwaway XDG dirs)"
tests/headless.sh ./build/shady > "$WORK/test.log" 2>&1 || { grep -E 'FAIL' "$WORK/test.log" >&2 || true; die "headless tests failed"; }
grep -q 'ALL PASS' "$WORK/test.log" || die "headless tests did not report ALL PASS"

if (( DRY )); then
  step "[dry-run] skip commit, tag and push; package the working tree instead"
  SNAP=$(git stash create 2>/dev/null || true); SNAP=${SNAP:-HEAD}
  git archive --format=tar.gz --prefix="shady-$VER/" -o "$WORK/shady-$VER.tar.gz" "$SNAP"
else
  step "Commit, tag v$VER, push"
  git diff --quiet HEAD || git commit -qam "Release $VER"
  git tag -a "v$VER" -m "Shady $VER"
  git push -q origin main
  git push -q origin "v$VER"
fi

step "Build and sign the package (makepkg --sign)"
cp packaging/PKGBUILD "$WORK/"
( cd "$WORK" && makepkg --sign --key "$KEY" -f --noconfirm >"$WORK/makepkg.log" 2>&1 ) \
  || { tail -20 "$WORK/makepkg.log" >&2; die "makepkg failed (if gpg asked for a passphrase, unlock the key with: echo | gpg --clearsign -u $KEY >/dev/null, then rerun)"; }

step "Signed repo database"
mkdir -p "$WORK/r" "$WORK/up"
cp "$WORK/$PKG" "$WORK/$PKG.sig" "$WORK/r/"
( cd "$WORK/r" && repo-add -q -s -k "$KEY" shady.db.tar.gz "$PKG" )
for f in db files; do
  cp "$WORK/r/shady.$f.tar.gz" "$WORK/up/shady.$f"
  cp "$WORK/r/shady.$f.tar.gz.sig" "$WORK/up/shady.$f.sig"
done
cp "$WORK/$PKG" "$WORK/$PKG.sig" "$WORK/up/"
for f in shady.db shady.files "$PKG"; do gpg --verify "$WORK/up/$f.sig" "$WORK/up/$f" 2>/dev/null || die "bad signature on $f"; done
tar tzf "$WORK/up/shady.db" | grep -q "^shady-$VER-1/" || die "repo db does not list shady-$VER-1"

if (( DRY )); then
  step "[dry-run] would upload to release 'repo': $(cd "$WORK/up" && echo *)"
  step "[dry-run] would create release 'Shady $VER' (latest). Notes:"; cat "$NOTES_OUT"
  step "Dry run OK. Version files restored; nothing pushed or published."
  exit 0
fi

step "Upload to the 'repo' release"
gh release view repo -R "$REPO" >/dev/null 2>&1 \
  || gh release create repo -R "$REPO" --title "Package repository" --notes "Signed pacman repo for Shady." --latest=false
OLD=$(gh release view repo -R "$REPO" --json assets -q '.assets[].name' | grep -E '^shady-.*\.pkg\.tar\.zst(\.sig)?$' | grep -v "^$PKG" || true)
gh release upload repo -R "$REPO" --clobber "$WORK"/up/*
for a in $OLD; do gh release delete-asset repo "$a" -R "$REPO" -y; done

step "Publish release Shady $VER"
gh release create "v$VER" -R "$REPO" --title "Shady $VER" --notes-file "$NOTES_OUT" --latest --verify-tag

step "Public verification"
sleep 3
TAG=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p')
[[ $TAG == "v$VER" ]] || die "latest release is '$TAG', expected v$VER"
curl -fsSL "https://github.com/$REPO/releases/download/repo/shady.db" | tar tz | grep -q "^shady-$VER-1/" \
  || die "public repo db does not list shady-$VER-1"
step "Released Shady $VER: https://github.com/$REPO/releases/tag/v$VER"
