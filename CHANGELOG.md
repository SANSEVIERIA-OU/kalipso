# Changes

Kalipso's versions go up one step at a time (0.1, 0.2 … 1.0, 1.1 …); only the first version and every
X.0 are published (see [docs/releasing.md](docs/releasing.md)). This file lists what each published
version brings, for the people who use it.

## Since 2.0 (in the next published version)

- Kalipso moved to the organization SANSEVIERIA OÜ: github.com/SANSEVIERIA-OU/kalipso, and the website
  at sansevieria-ou.github.io/kalipso. Its identifiers follow:
  - the app and the Plasma widget are `io.github.sansevieria_ou.kalipso`;
  - the companion on D-Bus is `io.github.sansevieria_ou.Kalipso.Engine`;
  - the GNOME extension is `kalipso@sansevieria-ou.github.io`.

  The first start moves your scripts and settings from the earlier folders by itself. The widget and the
  extension also find a companion from 2.0. Add the widget to the panel again, and enable the extension
  again, after updating.
- Voice following on Linux uses Vosk 0.3.50, built from source by Kalipso.

## 2.0 — 3 November 2026

The first version published since the project started in October 2026: everything below is new.

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
  The voice model for your language downloads from Settings, once.
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
  `kalipso` and `kalipso-bin` on AUR (`kalipso-git` follows the development).
- Windows: an `.msi` installer, which upgrades an older Kalipso in place, and a portable `.zip`.
- macOS (Apple Silicon): a `.dmg`.
- None of them is signed yet: see the release notes for how to open them, and `SHA256SUMS` with the
  build attestations to check them.
