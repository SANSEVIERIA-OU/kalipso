# Changes

Kalipso's versions go up one step at a time (0.1, 0.2 … 1.0, 1.1 …), one per update, and each is
published for every system (see [docs/releasing.md](docs/releasing.md)). This file lists what each
version brings, for the people who use it.

## 3.2 — 7 November 2026

- Each script now has its own menu for editing, exporting and deleting that script. New and Import
  remain visible above the list; reading-session reports and Impress controls live in the reader menu.
- Select scripts explicitly with checkboxes, a selection count and an All control that distinguishes
  none, some and all. The selected group can be exported, deleted after confirmation, or assigned a
  reading speed. Selecting scripts does not switch the one being read.
- Bulk export writes separate UTF-8 text files and adds numbered suffixes without overwriting files.
  Failed deletions keep their entries visible; failed library-index writes can be retried.
- The small library panel uses the whole window height and keeps keyboard focus on its visible
  controls. Escape leaves selection mode before closing the panel, and still pauses reading.
  Controls adapt to large text and right-to-left layouts; new wording is included in all 39 languages.

## 3.1 — 7 November 2026

- Guided voice downloads now use Moonshine 0.1.6 on Linux x64, Windows x64 and Apple Silicon Mac,
  with revised English Tiny streaming weights. Existing voice selections are preserved; for an
  already downloaded English model, use **Download again** in Settings → Voice to get the new files.
- On one Linux host with the same recorded English input, the full update reduced median speech CPU
  from 30.7% to 9.8% of one core and maximum processing backlog from 1.35 to 0.34 seconds across three
  runs. These measurements are not guarantees for other hardware, languages or operating systems.
- Added repeated native startup, editing, resource-use and speech benchmarks, with recorded methods,
  regression budgets and results in [the performance report](https://github.com/SANSEVIERIA-OU/kalipso/blob/v3.1/docs/performance.md). Browser checks use
  Playwright 1.65.0.
- **Known macOS limitation:** automated functional and speech tests passed, but startup p95 exceeded
  the 2-second laboratory budget in two runs (2.29 and 2.52 seconds). This release accepts only that
  known startup ceiling; other tests and budgets remain enforced. Physical Mac validation and the
  cause of the startup variability remain open.

## 3.0 — 4 November 2026

- Windows x64 and macOS now offer guided downloads of local speech engines and language models,
  including Italian with Vosk. New profiles start with downloadable models; existing engine choices
  and manually configured models are preserved. Moonshine on Mac requires Apple Silicon and macOS 15+.
- Downloads select a runtime for the app's operating system and architecture, verify its fingerprint,
  and install its native dependencies. Incomplete runtime installations can be repaired on retry.
- Windows system speech reports an unavailable desktop recognizer clearly instead of suggesting a
  microphone permission problem when only OneCore speech packs are present.
- Native Windows and Mac checks download the real engines and exercise recognition and script
  following with generated Italian and English audio.
- Current original app, companion, website, Plasma widget and KWin script use SANSEVIERIA License 1.0.
  The GNOME extension keeps its separate AGPL terms; third-party licences and previously valid
  permissions remain unaffected. See LICENSE and NOTICE for the scope and conditions.

## 2.9 — 4 November 2026

- The website's language picker stays outside the scrolling navigation on small screens. The app's
  language picker is first in Settings → Appearance, with a persistent explanation that the new
  language takes effect at the next start.
- Fix the website header check and use the pinned full-ICU Node runtime for macOS and Windows
  translation checks, so these checks no longer block delivery of the translated versions.

## 2.8 — 4 November 2026

- The app, Linux companion, Plasma widget, GNOME extension and website now have interfaces in 39
  languages. Choose the app language in Settings; the website's language switch keeps your section
  and reader options. Speech recognition keeps its own language and model availability.
- Arabic, Egyptian Arabic and Urdu use right-to-left layouts. Bundled Noto fonts cover the added
  writing systems, including Indic scripts, Chinese and Japanese.
- Translations share an offline source catalog and CLDR plural rules. Generated translations are
  marked as awaiting native-speaker review; reviewer and date are recorded separately.

## 2.7 — 4 November 2026

- Settings, in Kalipso's own look (GNOME, Windows, macOS): the current page is marked like the other
  pressed buttons, not by a blue block against the window's edge, and the line beside the list is a
  light divider.
- Settings, everywhere: a label above its field no longer breaks over two lines when the row has room
  for it ("Start or" / "pause").

## 2.6 — 3 November 2026

- **Present with Impress** recognises the presentation however its folder is written. Kalipso compared
  the file's address letter by letter. When LibreOffice wrote the same file another way, Kalipso waited
  for the slideshow for ever and never followed it. That happens with `/private/var` for `/var` on
  macOS, a long Windows folder name for its short form, or a folder reached through a link.
- While LibreOffice has not opened the presentation yet, Kalipso says it is waiting for it, instead of
  "Connected to Impress".
- Following Impress is now checked with a real LibreOffice on Linux, Windows and macOS.

## 2.5 — 3 November 2026

- On GNOME, **Settings → Appearance → GNOME top bar** says whether Kalipso's top bar reader is on. It
  can turn the reader on, so the Extensions app is no longer needed. GNOME keeps a newly installed
  extension off until it is turned on. The section also says when GNOME's extensions are all switched
  off, or when this GNOME version is not one the extension supports.

## 2.4 — 3 November 2026

- Windows: upgrading Kalipso with the `.msi` no longer breaks it. On a real Windows, an upgrade removed
  the earlier version's Qt, C++ runtime and FFmpeg files and did not copy them back. Kalipso then
  stopped with "Qt6Widgets.dll was not found". If that happened to you, install this version over it,
  or remove Kalipso in Settings → Apps and install it again; your scripts stay.
- Each change is now checked by installing, upgrading, starting and removing Kalipso on a real Windows.

## 2.3 — 3 November 2026

- Debian and Ubuntu: the `.deb` also installs Qt's SVG image plugin (`qt6-svg-plugins` on Debian 13
  and newer Ubuntu; Ubuntu 24.04 has it in `libqt6svg6`). Without it,
  on GNOME and other desktops without a Qt style of their own, check boxes showed no tick and drop-down
  lists no arrow.

## 2.2 — 3 November 2026

- Voice commands are gone. Saying "go next", "vai avanti" and the like moved the reader, and a script
  containing those words could set them off while you read. Words you say now only ever follow the
  script. Move with keys, a pedal, a clicker or the phone remote.

## 2.1 — 3 November 2026

- The selected script in the scripts panel keeps the reader's colours. On a light Windows, macOS or
  Plasma desktop it showed as a light block on the dark reader; it now looks like the other pressed
  buttons on the reader.
- From this version on, every update is a new version, published for Linux, Windows and macOS, and
  `kalipso-bin` and `kalipso` on AUR follow it.

## 2.0 — 3 November 2026

The first version published since the project started in October 2026: everything below is new.

2.0 was built again on the same day, after Kalipso moved to the organization SANSEVIERIA OÜ
(github.com/SANSEVIERIA-OU/kalipso, website at sansevieria-ou.github.io/kalipso). On AUR the new build is
2.0-2. If you used an earlier build, or a development version:
- the identifiers changed: the app and the Plasma widget are `io.github.sansevieria_ou.kalipso`, the
  companion on D-Bus is `io.github.sansevieria_ou.Kalipso.Engine`, the GNOME extension is
  `kalipso@sansevieria-ou.github.io`;
- the first start moves your scripts and settings from the earlier folders by itself;
- add the widget to the panel again, and turn the extension on again, after updating.

### One window that writes and reads

- The teleprompter is the editor. In a large window you write in the reader's own text, with buttons
  for **bold**, *italic*, section headings and direction cues, and your scripts in a panel beside it.
  In a small window, under the camera, it shows only what reading needs.
- **Under the camera** makes the window small, without a frame and above the others, at the top of the
  screen; **Expand** puts it back. On Plasma, Kalipso's KWin script places it, also on Wayland.
- **A−** and **A+** change the text size like in a word processor.
- The scripts are saved as you type, with your reading position. Kalipso imports text, Markdown, Word
  and OpenDocument documents, and the speaker notes of PowerPoint and Impress presentations.
- On the first start, three steps: choose your language, test the microphone, write or import a script.
- Starting Kalipso again brings the open window forward. Kalipso remembers where you put it under the
  camera; if that monitor is unplugged, the window comes back onto one that is left.

### Reading

- Three ways to advance: **Follow my voice**, **Steady speed** (150 words a minute to begin with, chosen
  from studies of reading aloud: [docs/reading-speed.md](docs/reading-speed.md)) and **Manual**.
- Voice following runs on your computer (Vosk or Moonshine). It keeps up through room noise, says when
  you go off script, and catches up when you come back, even after skipping ahead or going back.
  The voice model for your language downloads from Settings, once. On Linux, Vosk is 0.3.50, built
  from source by Kalipso.
- Mirroring for beam-splitter glass, a countdown, the current line at the height you choose, any font
  (also ones designed for dyslexia) and line spacing, controls that fade while you read.
- Foot pedals, presentation clickers and keys you choose; a phone remote over your network; optional
  voice commands to jump between sections.
- **Present with Impress** follows a LibreOffice Impress slideshow and jumps to each slide's notes.
- After a voice session: your pace against the time you planned, long pauses and filler words, and a
  report you can save.
- Pausing, Esc or minimizing the window turns the microphone off. On Plasma, also on Wayland, where
  Kalipso's KWin script tells the app.
- A downloaded voice language can be removed or downloaded again from Settings. Before downloading,
  Settings says where the files go and what licence they have.
- Hidden slides stay out when you import a presentation's speaker notes.
- Under the camera, Plasma 6.6 and later and Windows leave the reader out of screen sharing.

### Everywhere

- A Plasma widget and a GNOME Shell extension with the same reader, voice following through the
  companion `kalipso-engine`. Both open the app's scripts, and **Edit in Kalipso** takes you to the
  script in the app. The widget's settings are in Plasma's own settings window. The extension runs on
  GNOME Shell 48 and 51.
- The interface in English and Italian, the companion's messages too. Atkinson Hyperlegible Next
  throughout, contrast checked to WCAG 2.2 AA levels, text up to twice as large, reduced motion
  respected. Every control has a name for screen readers, and Tab goes the way the window reads
  ([the audit](docs/accessibility.md#wcag-22-aa-audit-of-the-standalone-app)).
- On Plasma, Kalipso looks like a Plasma app (Breeze, the desktop's colours, light and dark).

### Downloads

- Linux: a `.deb` (Debian 13, Ubuntu 24.04 and newer), an `.rpm` (current Fedora), and on Arch
  `kalipso-bin` and `kalipso` on AUR. Each also installs the Plasma widget, the GNOME extension and the
  KWin script.
- Windows: an `.msi` installer, which upgrades an older Kalipso in place, and a portable `.zip`.
- macOS (Apple Silicon): a `.dmg`.
- None of them is signed yet: see the release notes for how to open them, and `SHA256SUMS` with the
  build attestations to check them.
