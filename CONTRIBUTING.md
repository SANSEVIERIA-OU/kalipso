# Contributing

Original project code uses [SANSEVIERIA License 1.0](LICENSE), except for the GNOME component
and other separately licensed material listed in [NOTICE](NOTICE).
Contributors retain their copyright; no assignment is requested. Preserve third-party notices.
The GNOME extension retains its existing terms while its licensing scope is resolved.

## Licence assent for contributions under SANSEVIERIA License

Before submitting code covered by SANSEVIERIA License 1.0, read the complete licence. Include this express declaration
in your PR or an associated signed record, identifying the contribution and licence version:

> I intentionally submit the identified Contribution under the SANSEVIERIA License 1.0, whose complete text has been provided to me. I have the authority necessary to make the copyright and patent grants applicable to this Contribution. I expressly agree to the limited authority in section 7 allowing SANSEVIERIA OÜ to authorize specified commercial offerings and charge for those authorizations while preserving the source, reciprocity, notice and recipient-rights conditions. I retain my rights and do not grant a general proprietary relicensing power. I have identified separately licensed material and other rights holders whose permission is relevant, and I do not purport to grant rights they have not authorized me to grant. I also authorize SANSEVIERIA OÜ to make the identified Contribution available under any later version of the SANSEVIERIA License that it publishes as the common licence of all its projects, provided that version preserves source availability, reciprocity, the patent permission, my retained rights and the limits on SANSEVIERIA OÜ's authority at least to the extent of version 1.0; recipients keep the rights they received under earlier versions.

This declaration is specific to SANSEVIERIA License 1.0, not the standard DCO. Maintainers must
record the assent and verify the scope of any employer or third-party permission. An automatically
filled checkbox, silence or merely opening a PR does not establish that assent. Contributions to
components retaining AGPL use those component terms; this declaration must not silently relicense them.

SANSEVIERIA License is a single licence for all SANSEVIERIA OÜ projects: improvements found in one
project go into a new common version, never into a per-product variant. Section 15 does not convert
code to later versions automatically, so the declaration includes consent to later common versions
that keep at least source availability, reciprocity, the patent permission, contributors' rights and
the limits on SANSEVIERIA OÜ's authority of version 1.0. It allows no proprietary relicensing, and
recipients of earlier copies keep the rights they received.

SANSEVIERIA OÜ and its appointed maintainers decide which contributions to merge and which releases
are official. Submission does not guarantee acceptance. Reusing code from forks requires preserving
its licence and notices and verifying the grants; no compulsory PR is imposed on compliant forks.

## Layout

| Path | Contents |
| --- | --- |
| `src/` | Standalone Qt app (`window`, `reader`, `library`, `import`, `remote`), shared tracker, speech service and the `kalipso-engine` companion (`src/engine`) |
| `desktop/` | Plasma widget, GNOME extension and the shared JavaScript tracker (`desktop/*.mjs`) |
| `site/` | Static website |
| `tests/` | Qt tests, frontend runtime tests and fixtures |
| `tools/` | Installers, generators and repository checks |
| `translations/` | Qt Linguist translations of the standalone app |

The maintained tracker implementations are C++ (`src/tracker.cpp`) and JavaScript (`desktop/tracker.mjs`); change both and their tests together. GNOME currently uses its separate snapshot in `desktop/gnome/`; changes there require GPL-compatible rights and a separate review. `desktop/*.mjs` are the sources for the copies in `desktop/plasma/contents/ui/` and `site/assets/`: edit them, then run `python3 tools/sync-shared.py`. Design values live in `design/tokens.json`; regenerate derived files with `python3 tools/generate-design-tokens.py`.

C++ follows `.clang-format` (`clang-format -i` on the files you touch).

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs the Qt tests (offscreen), the JavaScript tracker in Node and — when `qmltestrunner` and `gjs` are installed — in the real QML and GJS engines used by Plasma and GNOME. On Debian and Ubuntu, `gjs qml6-module-qttest qml6-module-qtqml-workerscript qml6-module-qtquick-window qt6-declarative-dev-tools` provide those. To run the UI tests on a real X11 server as CI does:

```sh
xvfb-run -a env QT_QPA_PLATFORM=xcb build/ui_tests
```

### Checks

- `python3 tools/check-repository.py`: only source and distribution files are tracked.
- Website: `npm ci --prefix tools/browser && tools/browser/node_modules/.bin/playwright install --with-deps`, serve `site/` on port 8770 (`python3 -m http.server 8770 --directory site`), then run `PLAYWRIGHT=tools/browser/node_modules/playwright/index.mjs AXE_CORE=tools/browser/node_modules/axe-core/axe.min.js node tools/check-accessibility.mjs` and `node tools/check-design.mjs`.
- Speech changes: check alignment, latency, resource use and start/stop behaviour. `benchmarks/` has the probes.
- Real computers: [testing on real computers](docs/manual-testing.md) lists the checks CI cannot do (microphone, screen sharing, screen readers, monitors, pedals, current desktops).
- Speaker-note fixtures in `tests/fixtures/` are generated by `tests/fixtures/make-presentations.py` with LibreOffice.

Use synthetic test data. Never commit recordings, transcripts, models, API keys, packages, screenshots (other than curated `site/assets/media/`) or machine-specific configuration; put local output under the ignored `artifacts/`.

## Translations

```sh
lupdate -no-obsolete -locations none src -ts translations/kalipso_it.ts
```

Open the `.ts` file in Qt Linguist, translate, and rebuild. Add a language by creating `translations/kalipso_<code>.ts` and listing it in Settings → Interface language (`src/window.cpp`).

The Plasma widget (`i18n()`) uses `desktop/po/`. The GNOME extension (`_()`) keeps a separately
licensed snapshot in `desktop/gnome/po/`. The common generator does not overwrite that snapshot.
Update GNOME code and translations only with rights compatible with its component licence;
do not copy new SANSEVIERIA-only contributions into it without a separate sufficient grant.

For the Plasma catalog:

```sh
cd desktop
xgettext --from-code=UTF-8 --language=JavaScript --keyword=i18n --keyword=i18np:1,2 --keyword=_ \
  --keyword=ngettext:1,2 --no-location \
  -o po/kalipso-desktop.pot plasma/contents/ui/*.qml
msgmerge --update po/it.po po/kalipso-desktop.pot
```

`desktop/package.py` compiles the relevant component’s catalogs into its own package.

## Dependencies and CI

Build-tool versions are pinned in `tools/dependency-versions.json` (Qt, Node LTS, Python, CMake, aqt) and GitHub Actions are pinned to commits. `python3 tools/check-dependencies.py` and a weekly workflow report newer stable releases without changing anything, also of the speech runtimes the voice download installs (Vosk and Moonshine, pinned with their fingerprints in `tools/data/speech-catalog.json`). Linux CI builds against both the distribution's Qt and the latest stable Qt; macOS and Windows CI build, test and package the standalone app. Windows temporarily installs aqt from an upstream commit because aqt 3.3.0 cannot locate the Qt 6.11+ Windows repositories (#1).

## Releases

Versions go up one step at a time (0.1, 0.2 … 1.0, 1.1 …), one step per update, and every step is
released for every system: when the pull request that sets a new version is merged, the release
workflow tags it, builds and publishes it, and updates AUR. `tools/version.py set` writes a version
everywhere. The whole procedure, what each
release contains and how the Windows installer and the AUR packages are made: [docs/releasing.md](docs/releasing.md).

## Reporting problems

Describe how to reproduce the problem and include your desktop and version, Qt version, speech engine and audio setup.
