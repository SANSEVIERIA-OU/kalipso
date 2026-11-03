# Releasing Kalipso

## Versions

Kalipso goes up one step at a time: 0.1, 0.2 … 0.9, 1.0, 1.1 … 1.9, 2.0. There are ten steps between
public releases:

| Version | What happens when it is tagged |
|---|---|
| 0.1 (the first) and every X.0 | Built and **published** as a GitHub release, marked latest. |
| Every other step (0.2 … 0.9, 1.1 … 1.9, …) | Built and kept as a **draft** release: the same files, visible to maintainers only. |

`tools/version.py` keeps the version the same in every place that states it (CMake, the AppStream
metadata, the Plasma widget, the GNOME extension and the KWin script); CI fails if they differ.

## Making a version

1. Check that `main` is green and that the manual checks in [manual-testing.md](manual-testing.md) that
   matter for this version have been done.
2. Write the version. For a published version, give the release date:

   ```sh
   python3 tools/version.py set "$(python3 tools/version.py next)"            # a step: draft
   python3 tools/version.py set 2.0 --stable 2026-11-03                         # a public release
   ```

3. Add the version's section to [CHANGELOG.md](../CHANGELOG.md) (for a published version: what it brings,
   for the people who use it). Commit through a pull request as usual, then tag the merged commit and
   push the tag:

   ```sh
   git tag v2.0 && git push origin v2.0
   ```

4. The **Release** workflow builds every package on Linux (Ubuntu, and Arch for `kalipso-bin`), macOS
   and Windows, makes the Windows installer, writes `SHA256SUMS`, attests where each file was built
   (`gh attestation verify <file> -R SANSEVIERIA-OU/kalipso`), and creates the release: published for 0.1
   and X.0, a draft otherwise. Its notes list the merged pull requests and explain how to open unsigned
   packages. For a published version it then updates `kalipso-bin` and `kalipso` on AUR.

## What is in a release

- Linux:
  - a `.deb` (Debian 13, Ubuntu 24.04 or newer), an `.rpm` (current Fedora) and a `.tar.gz`, each with
    the voice companion, `kalipso-voice-setup`, the Plasma widget, the GNOME Shell extension and the KWin
    script (`cmake --install` puts the add-ons where each desktop looks for them);
  - `kalipso-X.Y-archlinux-x86_64.tar.zst`: the installed files built on Arch Linux against Arch's Qt,
    which `kalipso-bin` on AUR unpacks.
- `kalipso-companion-vX.Y-linux-x86_64.tar.gz`: the companion alone, for people who use only the Plasma
  widget or the GNOME extension. `./install` puts it under `~/.local`; the language is then chosen in
  the widget's or extension's Settings.
- Windows x64:
  - `kalipso-vX.Y-windows-x64.msi`, an installer;
  - `kalipso-vX.Y-windows-x64.zip`, the same files to run without installing.
- macOS Apple Silicon: `kalipso-vX.Y-macos-arm64.dmg` (drag to Applications) and the same app bundle in a
  `.tar.gz`, signed ad hoc only.
- The Plasma widget (`.plasmoid`), the GNOME Shell extension (`.zip`) and the KWin script, for using them
  without the app's packages.

The **Linux packages** workflow takes the `.deb` and the `.rpm` through installing, starting, upgrading
to a newer version and removing, on every change: the `.deb` on Ubuntu, the `.rpm` in a Fedora container.
The Linux release build uses Ubuntu 24.04's Qt (6.4) on purpose, so the packages depend on the Qt that
Debian 13, Ubuntu 24.04 and newer ship; newer Qt versions run them. The pinned Qt in
`tools/dependency-versions.json` is for the macOS and Windows builds, which carry their own Qt.

## The Windows installer

`tools/make-msi.sh` makes the `.msi` on Linux with msitools' `wixl`, from the folder the Windows job builds
and starts on Windows. The release workflow runs it; it also runs by hand, for example on the folder of a
CI run:

```sh
sudo apt install wixl imagemagick                     # wixl-heat comes with wixl
gh run download <run id> -R SANSEVIERIA-OU/kalipso -n kalipso-windows-2022 -D windows
tools/make-msi.sh windows/kalipso 2.0 dist            # → dist/kalipso-v2.0-windows-x64.msi
```

What the installer does (`packaging/windows/kalipso.wxs`):
- it shows the licence to accept and installs into `Program Files\Kalipso` for every user;
- it adds a Start menu entry and an entry in Settings → Apps with Kalipso's icon and links;
- a fixed UpgradeCode makes each new version replace the installed one, and an older installer refuses
  to replace a newer version;
- scripts and settings live in the user's AppData and registry, and are never touched.

`tools/test-msi-wine.sh` checks an installer under Wine. In a new Wine prefix it:
1. installs an older one;
2. upgrades it with the new one;
3. starts the installed app;
4. removes it;
5. checks the user's files are still there.

```sh
sudo apt install wine wine64 gcc-mingw-w64-x86-64
tools/make-msi.sh windows/kalipso 1.9 old             # the same files, as an older version
xvfb-run -a tools/test-msi-wine.sh dist/kalipso-v2.0-windows-x64.msi old/kalipso-v1.9-windows-x64.msi
```

Wine 10 lacks two things that Windows 10 and 11 have. The check stands in for them inside its own
prefix only:
- ICU (`icuuc.dll`), for Qt's text codecs;
- a colour-profile function (`WcsCreateIccProfile`).

## Arch Linux (AUR)

Three PKGBUILDs:

| PKGBUILD | What it does |
| --- | --- |
| `packaging/arch/kalipso-bin` | Unpacks the files the release built on Arch (`tools/build-arch-tarball.sh`), so nothing is compiled. |
| `packaging/arch/kalipso` | Builds a published version from source. |
| `packaging/arch/kalipso-git` | Builds the latest `main`. Not published on AUR: CI builds it from every change and installs it in an Arch container. |

The **AUR** workflow publishes `kalipso-bin` and `kalipso`:
- the release workflow calls it for 0.1 and every X.0, with the version and the checksums of the
  source and of the Arch tarball;
- it can be run by hand from the Actions tab for a published version;
- `pkgrel` is 1 for a new version. If a published version is built again, its files change but its
  number does not: the workflow then publishes one `pkgrel` more than AUR has, so pacman offers the new
  build (2.0 was built again on 3 November 2026, after the move to SANSEVIERIA-OU: 2.0-2).

`kalipso-git` was published until 2.0. Its last PKGBUILD on AUR still builds `main`; it calls
`packaging/linux/install-extras.sh`, which is kept for it.

It needs the repository secret `AUR_SSH_KEY`, the private SSH key of the AUR account that maintains the
packages; without it the job only warns. The first push creates each package on AUR.

To try `kalipso-bin` before a release, on Arch:
1. Run `tools/build-arch-tarball.sh . 2.0 out`.
2. Put the tarball next to a copy of the PKGBUILD with `pkgver=2.0` and
   `source=("kalipso-$pkgver-archlinux-x86_64.tar.zst")`.
3. Run `makepkg -si`.

## Not done yet

- **Signing.** Windows and macOS packages are unsigned (#9, #10). SmartScreen warns on every new
  version, and Smart App Control blocks unsigned apps outright. Options for an open-source project:
  - the SignPath Foundation: free for OSI-licensed projects, on application and approval, with a GitHub
    Actions integration;
  - a Certum open-source certificate in the developer's name.

  Azure Artifact Signing: Microsoft's page of 29 August 2026 lists organisations in the European Union
  as eligible, which may include SANSEVIERIA OÜ (not checked). On macOS,
  Developer ID signing and notarization need the Apple Developer Program (99 USD a year). The `.rpm` is
  not signed either: Fedora's `rpm -i` refuses it, and `dnf install` accepts it after a warning.
- **Microsoft Store**: an MSIX package, which the Store signs, so people who install from it see no
  SmartScreen warning. It needs a company Partner Center account for SANSEVIERIA OÜ (#68).
- The Plasma widget and the GNOME extension are not listed on the KDE Store or extensions.gnome.org:
  every Linux package installs them with the app (decided on 3 November 2026).
