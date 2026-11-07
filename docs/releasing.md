# Releasing Kalipso

## Versions

Kalipso goes up one step at a time: 0.1, 0.2 … 0.9, 1.0, 1.1 … 1.9, 2.0. Every update that changes
what people install is a new step, and every step is published for every system, marked latest, with
AUR following it (owner's decision, 3 November 2026: GitHub's minutes cost nothing for this public
repository, so there is no reason to hold versions back). X.0 is only the tenth step.

`tools/version.py` keeps the version the same in every place that states it (CMake, the AppStream
metadata, the Plasma widget, the GNOME extension and the KWin script); CI fails if they differ.

## Making a version

1. Check that `main` is green and that the manual checks in [manual-testing.md](manual-testing.md) that
   matter for this version have been done.
2. In the pull request with the update, write the next version with today's date, and summarise it in
   the AppStream entry that `set` adds (`packaging/io.github.sansevieria_ou.kalipso.metainfo.xml`):

   ```sh
   python3 tools/version.py set "$(python3 tools/version.py next)" --stable 2026-11-04
   ```

3. Add the version's section to [CHANGELOG.md](../CHANGELOG.md): what it brings, for the people who use
   it.
4. Merge the pull request. The **Release** workflow sees a version on `main` that has no tag yet, tags
   that commit `vX.Y`, builds every package on Linux (Ubuntu, and Arch for `kalipso-bin`), macOS and
   Windows, makes the Windows installer, writes `SHA256SUMS`, attests where each file was built
   (`gh attestation verify <file> -R SANSEVIERIA-OU/kalipso`), publishes the release as latest, and
   updates `kalipso-bin` and `kalipso` on AUR. Its notes list the merged pull requests and explain how to
   open unsigned packages. A push whose version is already released does nothing; a tag pushed by hand
   (`git tag v2.1 && git push origin v2.1`) releases the same way.

GitHub sometimes finds no free macOS runner and cancels the job ("not acquired by Runner"). The workflow
`retry-runners.yml` then runs the failed jobs again, up to three more times. The release is created only
when every package is there. For any other failure, look at the run and re-run it by hand once fixed.

### Explicit exception for 3.1 and 3.2

The owner authorized integration and publication on 7 November 2026 despite the recorded macOS
startup p95 exceeding 2 seconds. The same known limitation is retained for the requested library
update in 3.2; it is not a new performance exemption. Only release tags `v3.1` and `v3.2` pass
`allow_macos_startup_over_budget` to the native workflow. It accepts only a finite macOS startup
ceiling violation, keeps it in the performance report and emits a warning. Functional failures,
incomplete results, crashes and every other budget still block publication. Ordinary branch/PR
checks and release tags after 3.2 retain the startup gate. The full measurement sequence now runs
before budget evaluation, so a slow startup does not hide subsequent results. This is an accepted
limitation, not a startup fix or physical Mac validation.

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

On every change the "macOS and Windows" workflow checks the installer on a real Windows (Windows
Server 2022): the job `installers` makes this version's installer and a previous version's from the
tested build, as the release does, and `installer-on-windows` installs the older one, upgrades it (one
entry in Settings → Apps, the new version), starts the installed app, removes it (no program files, no
entry, no Start menu item) and checks that a file in the user's AppData is still there.

`tools/test-msi-wine.sh` runs the same checks under Wine, without Windows. In a new Wine prefix it:
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
- the release workflow calls it for every version, with the version and the checksums of the
  source and of the Arch tarball;
- it can be run by hand from the Actions tab for a released version;
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
  version, and Smart App Control blocks unsigned apps outright. Signing options (eligibility must match the current licence):
  - the SignPath Foundation: free for OSI-licensed projects, on application and approval, with a GitHub
    Actions integration;
  - a Certum open-source certificate only if the release qualifies; do not assume the current source-available licence meets that requirement.

  Azure Artifact Signing: Microsoft's page of 29 August 2026 lists organisations in the European Union
  as eligible, which may include SANSEVIERIA OÜ (not checked). On macOS,
  Developer ID signing and notarization need the Apple Developer Program (99 USD a year). The `.rpm` is
  not signed either: Fedora's `rpm -i` refuses it, and `dnf install` accepts it after a warning.
- **Microsoft Store**: an MSIX package, which the Store signs, so people who install from it see no
  SmartScreen warning. It needs a company Partner Center account for SANSEVIERIA OÜ (#68).
- The Plasma widget and the GNOME extension are not listed on the KDE Store or extensions.gnome.org:
  every Linux package installs them with the app (decided on 3 November 2026).
