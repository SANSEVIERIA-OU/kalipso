# Kalipso

A local-first teleprompter for your desktop. Keep your script next to your camera, read naturally, and let Kalipso follow your voice — speech recognition runs on your device.

[Website](https://brianidavide.github.io/kalipso/) · [Writing scripts](docs/scripts.md) · [Standalone app](docs/standalone.md) · [Plasma and GNOME](desktop/README.md) · [Voice following](docs/local-engine.md)

> **Packages are not signed yet.** Download them from the [releases page](https://github.com/brianidavide/kalipso/releases) and check them with `SHA256SUMS`; the release notes say how to open them on Windows and macOS. What changed: [CHANGELOG](CHANGELOG.md).

## What it does

- **Three ways to advance:** follow your voice, scroll at a steady speed (words per minute, with a countdown), or step manually with the keyboard, a foot pedal, a presentation clicker or your phone.
- **Live editing:** change the script while you read; your place is kept.
- **Your scripts are kept:** saved automatically on your device with your reading position.
- **Script cues:** `[look at camera]` is shown but never read by voice following; `# Heading` lines become sections you can jump to. See [writing scripts](docs/scripts.md).
- **Import:** speaker notes from PowerPoint (`.pptx`) and LibreOffice Impress (`.odp`), one section per slide; Word (`.docx`) and Writer (`.odt`) documents with their headings, lists and emphasis.
- **Presenting with LibreOffice Impress:** the reader follows the running slideshow and moves to each slide's notes (Linux; Windows and macOS not yet verified).
- **Teleprompter rigs:** mirror the text horizontally or vertically for beam-splitter glass.
- **Readable everywhere:** light, dark or automatic colors with WCAG AA contrast, large text and full keyboard control. See [accessibility](docs/accessibility.md).
- **Private by design:** recognition runs locally (Moonshine, Vosk, or the operating system's on-device recognizer on macOS and Windows). Nothing is uploaded.

## Choose your version

| Version | Runs on | Best for |
| --- | --- | --- |
| [Standalone app](docs/standalone.md) | Linux, Windows x64, Apple Silicon Macs (macOS 14.4+) | The full feature set: script library, import, movable reader, phone remote |
| [Plasma widget](desktop/README.md#kde-plasma-6) | KDE Plasma 6 | A reader on your desktop or panel |
| [GNOME extension](desktop/README.md#gnome-shell-48-and-51) | GNOME Shell 48 and 51 | A reader under the top bar |

All three work without speech components (manual and steady modes). Voice following on Linux needs the [local speech companion and a language model](docs/local-engine.md).

## Install

| System | Download | Checked on |
| --- | --- | --- |
| Arch Linux, EndeavourOS | AUR: `kalipso-bin` (ready to run), `kalipso` (built on your computer), `kalipso-git` (development) | Arch with Plasma 6.8 and Qt 6.12: packages built, installed and started |
| Debian 13, Ubuntu 24.04 or newer | `.deb` | Ubuntu 24.04 in CI and Debian 13: installed, started, upgraded, removed |
| Fedora (current release) | `.rpm` | Fedora container in CI: installed, started, upgraded, removed |
| Windows 10 (1809) or 11, x64 | `.msi` installer, portable `.zip` | Windows Server 2022 in CI: built and started; the installer under Wine: installed, upgraded, started, removed |
| macOS 14.4 or newer, Apple Silicon | `.dmg`, `.tar.gz` | macOS 26 in CI: built and started |
| KDE Plasma 6 | Plasma widget (`.plasmoid`) | Plasma 6.3 and 6.8 |
| GNOME Shell 48 | GNOME extension (`.zip`) | GNOME Shell 48 |

Not yet tried on real Windows and Mac computers, only on the build machines.

### Arch Linux and EndeavourOS

[`kalipso-bin`](https://aur.archlinux.org/packages/kalipso-bin) installs the app, the voice companion, the
Plasma widget, the GNOME Shell extension and the KWin script, built on Arch for each published version.
[`kalipso`](https://aur.archlinux.org/packages/kalipso) builds the same on your computer, and
[`kalipso-git`](https://aur.archlinux.org/packages/kalipso-git) builds the latest development:

```sh
yay -S kalipso-bin
```

Then open **Kalipso** from the application menu, or add the **Kalipso** widget to the desktop or panel
(right click → Add or Manage Widgets). For voice following, choose **Settings → Language you speak**:
Kalipso downloads that language's speech model once (40 to 60 MB). Without an AUR helper:
`git clone https://aur.archlinux.org/kalipso-bin.git && cd kalipso-bin && makepkg -si`.

### Debian, Ubuntu and Fedora

Download the `.deb` or the `.rpm` from the [releases page](https://github.com/brianidavide/kalipso/releases), then:

```sh
sudo apt install ./kalipso_2.0_amd64.deb      # Debian 13, Ubuntu 24.04 or newer
sudo dnf install ./kalipso-2.0-1.x86_64.rpm   # Fedora
```

The packages pull in Qt and PipeWire from your distribution. Fedora's `rpm -i` refuses packages that are
not signed; `dnf install` installs them after a warning. Run `kalipso`, or try it with a sample script:
`kalipso --demo --prompter`. For voice following, choose **Settings → Language you speak**.

To build from source instead (Debian, Ubuntu):

```sh
sudo apt install build-essential cmake ninja-build git qt6-base-dev qt6-svg-dev qt6-l10n-tools qt6-tools-dev libpipewire-0.3-dev pkg-config pipewire-bin
git clone https://github.com/brianidavide/kalipso.git
cd kalipso
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

### Windows and macOS

From the [releases page](https://github.com/brianidavide/kalipso/releases): on Windows, the `.msi` installs
Kalipso for every user with an entry in the Start menu (the `.zip` runs without installing); on macOS,
open the `.dmg` and drag Kalipso to Applications. The packages are not signed yet: Windows SmartScreen
asks you to confirm (*More info → Run anyway*); on macOS open it once, then choose *Open Anyway* in
System Settings → Privacy & Security. Voice following uses the speech recognition built into the system;
see [macOS and Windows builds](docs/system-speech.md), also for building them yourself.

### Plasma widget and GNOME extension only

Install the widget from the KDE Store or the extension from extensions.gnome.org (when listed), or build
them: `python3 desktop/package.py`, then follow [installing the widget or extension](desktop/README.md).
For voice following they need the **companion**: download `kalipso-companion` from the
[releases page](https://github.com/brianidavide/kalipso/releases), unpack it and run `./install`, then
choose **Settings → Language you speak** in the widget or extension. See
[voice following on Linux](docs/local-engine.md).

### Update and uninstall

Updating keeps your scripts, your reading positions and your settings; so does uninstalling.

| System | Update | Uninstall |
| --- | --- | --- |
| Arch | `yay -Syu` | `sudo pacman -R kalipso-bin` (or `kalipso`, `kalipso-git`) |
| Debian, Ubuntu | `sudo apt install ./kalipso_<new>_amd64.deb` | `sudo apt remove kalipso` |
| Fedora | `sudo dnf install ./kalipso-<new>-1.x86_64.rpm` | `sudo dnf remove kalipso` |
| Windows | run the new `.msi`: it replaces the old version | Settings → Apps → Installed apps → Kalipso → Uninstall |
| macOS | drag the new Kalipso to Applications, replacing the old one | move Kalipso from Applications to the Bin |

To remove your scripts, settings and downloaded voice models too, delete:
- Linux: `~/.local/share/brianidavide/kalipso`, `~/.local/share/kalipso` and `~/.config/brianidavide/kalipso.conf`;
- Windows: the folder `%APPDATA%\brianidavide\kalipso` and the registry key `HKEY_CURRENT_USER\Software\brianidavide\kalipso`;
- macOS: `~/Library/Application Support/brianidavide/kalipso` and `~/Library/Preferences/com.brianidavide.kalipso.plist`.

On Windows and macOS, Kalipso listens with the system's own speech recognition and downloads no models.

## Limitations

- **Screen sharing:** on Windows the standalone app asks the system to keep the reader out of screen shares and recordings; on KDE Plasma 6.6+ Kalipso's KWin script does the same under the camera ([how](docs/standalone.md#screen-sharing)). macOS offers no reliable way for an app to do this, so share a single window there. Always check what other participants see.
- **Desktop versions:** the GNOME extension declares Shell 48 and 51 (51 tested headless); the Plasma widget is tested with Plasma 6.3 and 6.8.
- **Speech models:** the download catalog has Moonshine models for English, German, Spanish, Japanese and Chinese, and Vosk models for those plus Italian. Other compatible models can be configured by hand.
- **Interface languages:** English and Italian.

## License

Kalipso is free software under the **AGPL-3.0-or-later** ([LICENSE](LICENSE)); third-party components keep their own licenses ([notices](THIRD_PARTY_NOTICES.md)). AppStream metadata is CC0-1.0. Kalipso is an independent project, not affiliated with KDE or GNOME.

Contributing, tests and the release process: [CONTRIBUTING.md](CONTRIBUTING.md). Resource use and latency: [performance](docs/performance.md).
