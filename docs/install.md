# Installing and maintaining Kalipso

| System | Download | Checked on |
| --- | --- | --- |
| Arch Linux, EndeavourOS | AUR: `kalipso-bin` (ready to run), `kalipso` (built on your computer) | Arch with Plasma 6.8 and Qt 6.12: packages built, installed and started |
| Debian 13, Ubuntu 24.04 or newer | `.deb` | Ubuntu 24.04 in CI and Debian 13: installed, started, upgraded, removed |
| Fedora (current release) | `.rpm` | Fedora container in CI: installed, started, upgraded, removed |
| Windows 10 (1809) or 11, x64 | `.msi` installer, portable `.zip` | Windows Server 2022 in CI: built and started, and the installer installed, upgraded, started and removed (also under Wine) |
| macOS 14.4 or newer, Apple Silicon | `.dmg`, `.tar.gz` | macOS 26 in CI: built and started, pictures light and dark |
| KDE Plasma 6 | Plasma widget (`.plasmoid`) | Plasma 6.3 and 6.8 |
| GNOME Shell 48 | GNOME extension (`.zip`) | GNOME Shell 48 |

Windows and macOS checks run on CI machines; microphone behavior, capture exclusions and usability on physical user hardware still need broader manual testing.

### Arch Linux and EndeavourOS

[`kalipso-bin`](https://aur.archlinux.org/packages/kalipso-bin) installs the app, the voice companion, the
Plasma widget, the GNOME Shell extension and the KWin script, built on Arch for each published version.
[`kalipso`](https://aur.archlinux.org/packages/kalipso) builds the same on your computer:

```sh
yay -S kalipso-bin
```

Then open **Kalipso** from the application menu, or add the **Kalipso** widget to the desktop or panel
(right click → Add or Manage Widgets). For voice following, choose **Settings → Voice → Language you speak**:
Kalipso downloads that language's speech model once (the size is shown before download). Without an AUR helper:
`git clone https://aur.archlinux.org/kalipso-bin.git && cd kalipso-bin && makepkg -si`.

### Debian, Ubuntu and Fedora

Download the `.deb` or the `.rpm` from the [releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases), then:

```sh
sudo apt install ./kalipso_*_amd64.deb      # Debian 13, Ubuntu 24.04 or newer
sudo dnf install ./kalipso-*.x86_64.rpm     # Fedora
```

The packages pull in Qt and PipeWire from your distribution. Fedora's `rpm -i` refuses packages that are
not signed; `dnf install` installs them after a warning. Run `kalipso`, or try it with a sample script:
`kalipso --demo --prompter`. For voice following, choose **Settings → Voice → Language you speak**.

Like the Arch packages, they also install the Plasma widget, the GNOME Shell extension and the KWin
script. On Plasma, add the **Kalipso** widget to the desktop or panel. On GNOME, log out and back in,
then turn it on in Kalipso's **Settings → Appearance → GNOME top bar** (or in the Extensions app).

To build from source instead (Debian, Ubuntu):

```sh
sudo apt install build-essential cmake ninja-build git qt6-base-dev qt6-svg-dev qt6-l10n-tools qt6-tools-dev libpipewire-0.3-dev pkg-config pipewire-bin
git clone https://github.com/SANSEVIERIA-OU/kalipso.git
cd kalipso
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

### Windows and macOS

From the [releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases): on Windows, the `.msi` installs
Kalipso for every user with an entry in the Start menu (the `.zip` runs without installing); on macOS,
open the `.dmg` and drag Kalipso to Applications. The packages are not signed yet: Windows SmartScreen
asks you to confirm (*More info → Run anyway*); on macOS open it once, then choose *Open Anyway* in
System Settings → Privacy & Security. Voice following uses the speech recognition built into the system;
see [macOS and Windows builds](system-speech.md), also for building them yourself.

### Plasma widget and GNOME extension only

Download the widget (`kalipso-plasma.plasmoid`) or the extension (`kalipso-gnome.zip`) from the
[releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases), or build them with
`python3 desktop/package.py`, then follow [installing the widget or extension](../desktop/README.md).
For voice following they need the **companion**: download `kalipso-companion` from the
[releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases), unpack it and run `./install`, then
choose **Settings → Language you speak** in the widget or extension. See
[voice following on Linux](local-engine.md).

### Update and uninstall

Updating keeps your scripts, your reading positions and your settings; so does uninstalling.

| System | Update | Uninstall |
| --- | --- | --- |
| Arch | `yay -Syu` | `sudo pacman -R kalipso-bin` (or `kalipso`, `kalipso-git`) |
| Debian, Ubuntu | `sudo apt install ./kalipso_<new>_amd64.deb` | `sudo apt remove kalipso` |
| Fedora | `sudo dnf install ./kalipso-<new>-1.x86_64.rpm` | `sudo dnf remove kalipso` |
| Windows | run the new `.msi`: it replaces the old version | Settings → Apps → Installed apps → Kalipso → Uninstall |
| macOS | drag the new Kalipso to Applications, replacing the old one | move Kalipso from Applications to the Bin |

To go back to an earlier version, install its package from the [releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases) over the new one; your scripts and settings stay:

| System | Back to an earlier version |
| --- | --- |
| Arch | `sudo pacman -U ~/.cache/yay/kalipso-bin/kalipso-bin-<old>-1-x86_64.pkg.tar.zst` (yay keeps the packages it installed there) |
| Debian, Ubuntu | `sudo apt install --allow-downgrades ./kalipso_<old>_amd64.deb` |
| Fedora | `sudo dnf downgrade ./kalipso-<old>-1.x86_64.rpm` |
| Windows | the installer never replaces a newer version: uninstall Kalipso, then run the older `.msi` |
| macOS | drag the older Kalipso to Applications, replacing the newer one |

A script saved by a newer Kalipso opens in an older one: scripts are plain text.

To remove your scripts, settings and downloaded voice models too, delete:
- Linux: `~/.local/share/sansevieria-ou/kalipso`, `~/.local/share/kalipso` and `~/.config/sansevieria-ou/kalipso.conf`;
- Windows: the folder `%APPDATA%\sansevieria-ou\kalipso` and the registry key `HKEY_CURRENT_USER\Software\sansevieria-ou\kalipso`;
- macOS: `~/Library/Application Support/sansevieria-ou/kalipso` and `~/Library/Preferences/com.sansevieria-ou.kalipso.plist`.

On Windows and macOS, Kalipso listens with the system's own speech recognition and downloads no models.

