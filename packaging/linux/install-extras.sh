#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# `cmake --install` now installs the Plasma widget, the GNOME Shell extension and the KWin script
# itself (CMakeLists.txt). This script is kept for PKGBUILDs already on AUR that still call it after the
# install, such as the last kalipso-git: it unpacks the same files over the same paths.
# Usage: install-extras.sh <source tree> <staging prefix, e.g. $pkgdir/usr>
set -eu
src=$1 prefix=$2
python3 "$src/desktop/package.py" >/dev/null
unpack() {
    mkdir -p "$2"
    python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$1" "$2"
}
packages=$src/artifacts/packages
unpack "$packages/kalipso-plasma.plasmoid" "$prefix/share/plasma/plasmoids/io.github.sansevieria_ou.kalipso"
unpack "$packages/kalipso-gnome.zip" "$prefix/share/gnome-shell/extensions/kalipso@sansevieria-ou.github.io"
unpack "$packages/kalipso-kwin.kwinscript" "$prefix/share/kwin/scripts/kalipso-companion"
