#!/bin/bash
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
# The installed files of Kalipso, built on Arch Linux against Arch's own Qt, as one tarball: what the
# kalipso-bin package on AUR unpacks (packaging/arch/kalipso-bin). The release workflow runs it in an
# archlinux:base-devel container; it also runs in any up-to-date Arch with the build dependencies.
#
# Usage: build-arch-tarball.sh <source tree> <version> <output directory>
# Needs: cmake ninja python qt6-base qt6-svg qt6-tools libpipewire pipewire (and zstd, tar)
set -euo pipefail
source_tree=$(realpath "$1") version=$2 out=$(realpath "$3")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# A copy, without earlier builds, archives or the repository's history.
mkdir -p "$work/src"
tar -C "$source_tree" --exclude='./build*' --exclude='./artifacts' --exclude='./.git' --exclude='./.claude' \
    -cf - . | tar -C "$work/src" --no-same-owner -xf -
cmake -S "$work/src" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF >/dev/null
cmake --build "$work/build" --parallel "${JOBS:-4}"
# The Plasma widget, the GNOME extension and the KWin script come with the install (CMakeLists.txt).
DESTDIR="$work/stage" cmake --install "$work/build" --strip >/dev/null
# The binaries start (no missing library or plugin) before anything is packed.
QT_QPA_PLATFORM=offscreen "$work/stage/usr/bin/kalipso" --version
test -x "$work/stage/usr/bin/kalipso-engine"
name="kalipso-$version-archlinux-x86_64.tar.zst"
mkdir -p "$out"
tar -C "$work/stage" --owner=0 --group=0 --numeric-owner --sort=name --zstd -cf "$out/$name" usr
echo "$out/$name"
