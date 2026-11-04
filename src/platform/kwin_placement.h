// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once

// On Plasma with Wayland, applications cannot place their windows or keep them above others.
// Kalipso's KWin script does it for the reader: at the top of the screen, under the camera, above
// other windows and, where KWin supports it, left out of screen captures. The script is built into
// Kalipso and loaded into KWin while Kalipso runs; nothing is installed or enabled permanently.
// Elsewhere (X11, other desktops, macOS, Windows) the reader places itself and this does nothing.
namespace KWinPlacement {
// Loads the script under KWin on Wayland. Returns true when the script runs, also when it was
// already enabled in System Settings. A copy left loaded by an earlier Kalipso (one that crashed
// or was killed) is replaced by this one's.
bool load();
// Unloads the script if load() loaded it.
void unload();
// Whether the script runs: then KWin frames, places and stacks the window, and Kalipso only says
// with its title whether it is reading (see readingMark).
bool active();
// Starts the window title while reading. Invisible; the script looks for it.
inline constexpr char16_t readingMark = u'\u2060';
// Follows the reading mark when the person lets screen sharing show the reader.
inline constexpr char16_t shareableMark = u'\u2063';
} // namespace KWinPlacement
