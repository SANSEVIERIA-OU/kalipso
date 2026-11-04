#!/bin/bash
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
# Packs the Windows build into an installer (.msi) on Linux, with msitools' wixl: no Windows needed.
# The input is the portable folder the "macOS and Windows" workflow builds and tests on Windows (the
# artifact kalipso-windows-2022, folder kalipso/ with bin/ and share/). The installer puts it in
# Program Files\Kalipso, adds a Start menu entry and an entry in Settings → Apps, and upgrades an
# older Kalipso in place (packaging/windows/kalipso.wxs).
#
# Usage: make-msi.sh <portable folder> <version X.Y> <output directory>
# Needs: wixl and wixl-heat (Debian/Ubuntu: apt install wixl), ImageMagick (the icon), python3.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
folder=$(realpath "$1") version=$2 out=$(realpath -m "$3")
[[ $version =~ ^[0-9]+\.[0-9]$ ]] || { echo "Not a Kalipso version: $version" >&2; exit 1; }
test -f "$folder/bin/kalipso.exe" || { echo "No bin/kalipso.exe in $folder" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The files, one component each, under Program Files\Kalipso.
(cd "$folder" && find . -type f | sed 's|^\./||' | sort) |
    sed "s|^|$folder/|" |
    wixl-heat -p "$folder/" --var var.SourceDir --directory-ref INSTALLDIR --component-group AppFiles --win64 \
        > "$work/files.wxs"

# The icon for the Start menu and Settings → Apps, from the app's own.
magick=$(command -v magick || command -v convert)
"$magick" -background none -density 384 "$here/packaging/io.github.sansevieria_ou.kalipso.svg" \
    -define icon:auto-resize=256,64,48,32,16 "$work/kalipso.ico"

# The licence the welcome page shows, as the RTF that Windows Installer's text box reads (License.rtf:
# the name wixl's welcome page looks for).
python3 - "$here/LICENSE" "$work/License.rtf" <<'PY'
import sys
text = open(sys.argv[1], encoding='utf-8').read()
body = text.replace('\\', '\\\\').replace('{', '\\{').replace('}', '\\}')
body = ''.join(c if ord(c) < 128 else f'\\u{ord(c)}?' for c in body)
body = body.replace('\n', '\\par\n')
open(sys.argv[2], 'w', encoding='ascii').write('{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs16\n' + body + '}')
PY

mkdir -p "$out"
msi="$out/kalipso-v$version-windows-x64.msi"
# The welcome page reads License.rtf from the directory wixl runs in.
(cd "$work" && wixl --arch x64 --ext ui -D SourceDir="$folder" -D Win64=yes -D Version="$version.0" -D Icon="$work/kalipso.ico" \
    -o "$msi" "$here/packaging/windows/kalipso.wxs" "$work/files.wxs")
echo "$msi"
