#!/bin/bash
# Shady installer for Omarchy: trusts the signing key (shared with OmaEar), adds
# the [shady] pacman repo, and installs the app. Updates then arrive with
# `omarchy update`. Safe to run again.
set -euo pipefail
KEY=AC6FF13C76D8916CCCF2DA05C3225162B5257DB4
REPO=https://github.com/allandehn/shady/releases/download/repo
KEYURL=https://github.com/allandehn/omaear/releases/download/repo/omaear.asc

# Skip the key steps if it's already trusted (e.g. OmaEar was installed first).
if ! sudo pacman-key --list-keys "$KEY" >/dev/null 2>&1; then
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT
    curl -fsSL "$KEYURL" -o "$tmp"
    if ! gpg --show-keys --with-colons "$tmp" 2>/dev/null | grep -q "^fpr:::::::::$KEY:"; then
        echo "Downloaded key does not match $KEY, aborting." >&2
        exit 1
    fi
    sudo pacman-key --add "$tmp"
fi
sudo pacman-key --lsign-key "$KEY" >/dev/null

if ! grep -q '^\[shady\]' /etc/pacman.conf; then
    printf '\n[shady]\nServer = %s\n' "$REPO" | sudo tee -a /etc/pacman.conf >/dev/null
fi
sudo pacman -Sy
omarchy pkg add shady
echo "Shady is installed. Open it from the app launcher, or run shady."
