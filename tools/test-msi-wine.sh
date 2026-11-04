#!/bin/bash
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
# Checks a Windows installer made on Linux (tools/make-msi.sh) under Wine, in a new Wine prefix:
#   1. the older installer installs (when given);
#   2. the new one replaces it in place, leaving one entry in the installed programs;
#   3. the installed app starts and quits by itself;
#   4. a file in the user's AppData survives the upgrade and the removal;
#   5. removing it leaves no program files, no entry, no Start menu item.
# The "macOS and Windows" workflow runs the same checks on a real Windows (job installer-on-windows).
#
# Usage: xvfb-run -a tools/test-msi-wine.sh <new.msi> [<older.msi>]
# Needs: wine (64-bit), x86_64-w64-mingw32-gcc (Debian/Ubuntu: gcc-mingw-w64-x86-64).
set -euo pipefail
new=$(realpath "$1")
old=${2:+$(realpath "$2")}
prefix=$(mktemp -d)
work=$(mktemp -d)
trap 'wineserver -k 2>/dev/null || true; rm -rf "$prefix" "$work"' EXIT
export WINEPREFIX=$prefix WINEARCH=win64 WINEDEBUG=-all
wineboot -i >/dev/null 2>&1
wineserver -w
# A crash ends the check with an error instead of waiting at Wine's crash dialog.
wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null 2>&1

# Windows 10 and 11 ship ICU (icuuc.dll), which Qt's text codecs use; Wine does not. A stand-in that
# answers "no such converter" lets Qt start (UTF-8 and UTF-16 do not use it). Only for this check.
cat > "$work/icuuc.c" <<'C'
#include <stddef.h>
#include <stdint.h>
typedef int UErrorCode;
static void fail(UErrorCode *error) { if (error) *error = 4; /* U_FILE_ACCESS_ERROR */ }
void *ucnv_open(const char *name, UErrorCode *error) { (void)name; fail(error); return NULL; }
void ucnv_close(void *converter) { (void)converter; }
int32_t ucnv_countAvailable(void) { return 0; }
const char *ucnv_getAvailableName(int32_t n) { (void)n; return NULL; }
const char *ucnv_getStandardName(const char *name, const char *standard, UErrorCode *error) {
    (void)name; (void)standard; fail(error); return NULL; }
const char *ucnv_getName(const void *converter, UErrorCode *error) { (void)converter; fail(error); return NULL; }
int8_t ucnv_getMaxCharSize(const void *converter) { (void)converter; return 4; }
void ucnv_reset(void *converter) { (void)converter; }
#define NOTHING(name) int64_t name(void) { return 0; }
NOTHING(ucnv_fromUnicode) NOTHING(ucnv_toUnicode) NOTHING(ucnv_fromUCountPending) NOTHING(ucnv_toUCountPending)
NOTHING(ucnv_setFromUCallBack) NOTHING(ucnv_setToUCallBack) NOTHING(ucnv_getFromUCallBack) NOTHING(ucnv_getToUCallBack)
NOTHING(ucnv_cbFromUWriteUChars) NOTHING(ucnv_cbToUWriteUChars)
NOTHING(UCNV_FROM_U_CALLBACK_SUBSTITUTE) NOTHING(UCNV_TO_U_CALLBACK_SUBSTITUTE)
C
x86_64-w64-mingw32-gcc -shared -O1 -o "$prefix/drive_c/windows/system32/icuuc.dll" "$work/icuuc.c"
# Qt's Windows plugin asks the colour management API for the screen's profile; Wine 10 has no
# WcsCreateIccProfile and stops the app there. A stand-in that has no profiles makes Qt use sRGB.
cat > "$work/mscms.c" <<'C'
#include <windows.h>
HANDLE WINAPI OpenColorProfileW(void *profile, DWORD access, DWORD share, DWORD creation) {
    (void)profile; (void)access; (void)share; (void)creation; return NULL; }
BOOL WINAPI CloseColorProfile(HANDLE profile) { (void)profile; return TRUE; }
BOOL WINAPI GetColorProfileFromHandle(HANDLE profile, BYTE *buffer, DWORD *size) {
    (void)profile; (void)buffer; (void)size; return FALSE; }
HANDLE WINAPI WcsCreateIccProfile(HANDLE profile, DWORD options) { (void)profile; (void)options; return NULL; }
C
x86_64-w64-mingw32-gcc -shared -O1 -o "$prefix/drive_c/windows/system32/mscms.dll" "$work/mscms.c"

entries() { wine reg query 'HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall' /s 2>/dev/null |
    tr -d '\r' | grep -A12 -i 'DisplayName.*Kalipso' | grep -i 'DisplayVersion' || true; }
appdata="$prefix/drive_c/users/$(id -un)/AppData/Roaming/sansevieria-ou"
program="$prefix/drive_c/Program Files/Kalipso"

if [ -n "$old" ]; then
    wine msiexec /i "$old" /qn
    echo "older: $(entries)"
fi
mkdir -p "$appdata" && echo "my script" > "$appdata/kept.txt"
wine msiexec /i "$new" /qn
installed=$(entries)
echo "installed: $installed"
[ "$(printf '%s\n' "$installed" | grep -c DisplayVersion)" = 1 ] || { echo "Expected one Kalipso entry" >&2; exit 1; }
test -f "$program/bin/kalipso.exe"
find "$prefix/drive_c" -iname 'Kalipso.lnk' | grep -q . || { echo "No Start menu item" >&2; exit 1; }
WINEDLLOVERRIDES=mscms=n wine "$program/bin/kalipso.exe" --demo --quit-after 1500
echo "app: started and quit"
wine msiexec /x "$new" /qn
[ -z "$(entries)" ] || { echo "Still in the installed programs" >&2; exit 1; }
[ ! -e "$program/bin/kalipso.exe" ] || { echo "Program files left behind" >&2; exit 1; }
! find "$prefix/drive_c" -iname 'Kalipso.lnk' | grep -q . || { echo "Start menu item left behind" >&2; exit 1; }
grep -q "my script" "$appdata/kept.txt"
echo "removed; the user's files are still there"
