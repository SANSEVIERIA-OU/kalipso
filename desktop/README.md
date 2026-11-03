# Install the Plasma widget or GNOME extension

Kalipso's Linux packages (the `.deb`, the `.rpm`, `kalipso-bin` and `kalipso` on AUR) already install
both, with the KWin script: on Plasma, add the **Kalipso** widget to the desktop or panel; on GNOME, log
out and back in, then turn **Kalipso** on in the Extensions app. This page is for using them without
the app's packages.

Their files are on the [releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases)
(`kalipso-plasma.plasmoid`, `kalipso-gnome.zip`). To build them instead, from a clone of the repository:

```sh
git clone https://github.com/SANSEVIERIA-OU/kalipso.git
cd kalipso
python3 desktop/package.py
```

The archives are written to `artifacts/packages/`.

## KDE Plasma 6

Use **Install Widget From Local File** in the widget chooser, or:

```sh
kpackagetool6 --type Plasma/Applet --install artifacts/packages/kalipso-plasma.plasmoid
```

Use `--upgrade` instead of `--install` to update an existing installation. Add **Kalipso** to the desktop or a panel and place it near your webcam. To uninstall:

```sh
kpackagetool6 --type Plasma/Applet --remove io.github.sansevieria_ou.kalipso
```

## GNOME Shell 48 and 51

```sh
gnome-extensions install --force artifacts/packages/kalipso-gnome.zip
```

Log out and back in, then enable it:

```sh
gnome-extensions enable kalipso@sansevieria-ou.github.io
```

The play icon in the top bar shows or hides the reader. The extension declares GNOME Shell 48 and 51, the versions it is tested with (51 headless, on Arch Linux); GNOME refuses to enable it on other versions, 49 and 50 included until they are tested. To uninstall:

```sh
gnome-extensions uninstall kalipso@sansevieria-ou.github.io
```

## Using the reader

- **Scripts**: the widget and the extension read the scripts you write in the Kalipso app, through the local companion. At first they open the script the app has open; the script menu (Plasma) or **Scripts** (GNOME) chooses another one, or **A script written here**. A script of the app follows the app when it saves it, keeps your reading place, and **Edit in Kalipso** opens it in the app. Without the app, **Edit script** writes one here, saved automatically with your reading position.
- `[Bracketed cues]` are shown in italics but never read by voice following. Lines starting with `#` are sections: use **Sections** (Plasma) or **‹ Section / Section ›** (GNOME) to jump between them. Headings show without their `#`, and `**bold**` and `*italic*` without their stars.
- How the reader advances (the mode menu under the text in Plasma, **Settings** in GNOME):
  - **Manual**: **Next words** and **Previous** move four words; **Reset** returns to the start.
  - **Steady speed**: scrolls word by word at the chosen words per minute (150 to begin with), after an optional 3- or 5-second countdown.
  - **Follow my voice**: needs the [local speech companion](../docs/local-engine.md). Choose the language you speak once (Plasma: **Configure Kalipso… → Voice**; GNOME: **Settings → Language you speak**); its speech model downloads once, with its progress and **Cancel download**.
- Plasma: **Configure Kalipso…** (in the reader's ⋯ menu, or right-click the widget) has the settings: **Reading** (mode, steady speed, countdown, mirror), **Voice** (the language you speak) and **Appearance** (colors, text size, font, line spacing). **Follow a window's colors…** in the ⋯ menu takes the colors of a window behind the reader.
- **Mirror** flips the text for beam-splitter teleprompter glass.
- Keys (for foot pedals and clickers): arrows or Page Up/Down step, Space starts or pauses, + and − change the steady speed, Home returns to the start.

The interface follows your desktop language (English or Italian). Hiding the reader stops the microphone. The reader can appear in screen sharing; check what other participants see.
