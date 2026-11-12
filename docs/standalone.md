# Standalone app

Run `kalipso` after [building or installing it](../README.md#install). `kalipso --demo --prompter` opens a sample script without touching your saved scripts or the microphone.

## Scripts

The first time, Kalipso opens on three steps instead of a blank page: choose the language you speak, check that Kalipso hears you, and write, import or try a sample script.

Scripts are saved automatically as you type, together with your reading position, and reopen where you left off. **Scripts** (the panel button at the top left, or F9) lists them with how long each takes to read and when it changed; a script left empty is not kept. **+** (Ctrl+N) starts one and **Import…** adds a file. Each script has its own **⋯** menu: **Edit**, **Export…** and **Delete script…** act on that row, even when another script is open. **Select** shows checkboxes and becomes **Done**, in the same place; nothing else in the panel moves. **All**, at the start of the row in line with the checkboxes, includes the whole library, and **Actions** (also the list’s context menu) applies to the displayed selection count. You can set the selected scripts’ reading speed, export them as separate UTF-8 text files into a folder, or delete them after confirmation (Delete asks about the checked scripts). Export adds numbered suffixes when names are already taken. Selecting scripts does not change the one being read, and **+** (Ctrl+N) ends selection before it starts a new script. **Done** and Escape leave selection mode; Ctrl/⌘+A inside the panel selects all. Closing the panel clears the selection. See [writing scripts](scripts.md) for cues, sections and the import formats.

## Look

On a Linux desktop with its own style (Breeze on KDE Plasma), Kalipso uses that style and the desktop's colours, light or dark, and changes with them. **Settings → Appearance → Application appearance** can keep it light or dark instead. Elsewhere Kalipso draws its controls itself. The reader keeps its own look everywhere.

## One window: writing and reading

Kalipso has one window, and it is the reader: the script large, in the reader's colours, with the current word marked. You write in it too. What changes is what the window shows around the text, by its size.

- **Large** (wider than 960 and taller than 600 points, or maximized): you can write. The top bar adds the formats — **Bold**, **Italic**, **Section heading** and **Direction cue** — and the scripts sit beside the text. The text keeps a comfortable column (at most 66 characters of its size). Markdown marks stay hidden except around the caret, so you never type inside marks you cannot see; each format button puts its format on the selection or takes it off text that has it, as in a word processor. Copying gives the script as written, marks included. The caret is where reading starts: click a word, or move there with the arrows, and press **Start**. The status says how long the script is and which word reading starts from.
- **Small**, as a teleprompter: only what reading needs. The text cannot be changed by mistake, Space and the arrows (and foot pedals) control the reader, and the scripts open over the text from the panel button.
- **Under the camera** (top bar) makes the window small, without a frame, above the other windows, at the top of the screen. **Expand** puts it back where it was, as big as it was, with its frame. Pulling its edges larger also brings the writing tools back. In full screen the window always reads.
- **A−** and **A+** (Ctrl+− and Ctrl++, or Ctrl and the mouse wheel) make the text smaller and larger, the same size as **Settings → Text size**. Each step is an eighth larger or smaller (18, 20, 22, 25, 28, 32, 35, 40 … 96 points), the typographic scale of the major second, so every press looks like the same change at any size.
- **Move** the window under the camera by dragging its title, **resize** it from its edges; its size is remembered, and so is its position: for each monitor on X11, macOS and Windows, and on Plasma across restarts, through Kalipso's KWin script, which places it and keeps it above other windows (Wayland lets only the compositor do that); on other Wayland desktops the compositor decides. If its monitor is unplugged, the window comes back under the camera of a monitor that is left.
- The bottom bar has the previous and next word, **Start** (the microphone when following your voice, Pause while reading), the reading mode and, in steady speed, the speed. The bars keep one row whatever the mode; in a narrow window the labels shorten first.
- While reading, the current word is highlighted and already-read words are dimmed. The current line stays at the height chosen in **Settings → Current line position** (top, upper third or centre), below the top bar.
- **Settings → Font** and **Line spacing** change the typeface and spacing. Any installed font works, including ones designed for easier reading such as Atkinson Hyperlegible or OpenDyslexic. Lines cut by the window's edges fade out rather than ending abruptly.
- **Sections** jumps to a `#` heading. Clicking a word moves the reading position there.
- While the reader scrolls or listens, its controls fade out after a moment and the text has the whole window; they come back over the text when you move the mouse, press a key or pause (Settings → Hide the reader's controls while reading). The text never moves when they do.
- In steady speed, the status shows the time left at the current speed.
- Pausing (the button, Space or Escape) turns the microphone off, and so does minimizing the window: on Plasma with Wayland, where an application is not told, Kalipso's KWin script tells it; on other Wayland desktops minimizing does not pause. Nothing starts again by itself. Ctrl+Enter starts or pauses from anywhere, also while writing.
- Starting Kalipso again while it is open brings its window forward.
- Until a voice language is installed, **Start** reads **Choose language** and opens Settings there.

### Reading modes

Choose the mode in the reader:

| Mode | How the reader advances |
| --- | --- |
| **Manual** | Only when you step: **Previous**/**Next**, keys, foot pedal or phone. |
| **Steady speed** | One word at a time at the chosen words per minute, 150 to begin with ([why](reading-speed.md)). **Slower**/**Faster** change it while scrolling; each script remembers its own speed. An optional 3- or 5-second countdown comes first. |
| **Follow my voice** | Local speech recognition matches what you say to the script. If you go off script, the reader says so and catches up when you return — even after skipping ahead or going back. |

After a voice session, the status line summarizes it: duration, words read, pace compared with the steady speed (for example "8% faster than planned"), long pauses and recognized filler words (many recognizers drop fillers, so this is a lower bound). **Reader ⋯ → Save session report…** saves the same as a Markdown file: the words read out of the script's total, the time those words would take at the steady speed against the time taken, the time and pace of each section and the words just before each long pause. Nothing is saved unless you choose to. Filler counting can be turned off in **Settings → Voice**.

**Settings → Voice → Microphone** lists the microphones connected now. **System microphone** names the one the system uses and follows the system's choice when it changes. A microphone you choose stays the one: if it is unplugged, Kalipso says so instead of switching to another. **Test microphone** opens it for up to 30 seconds; a bar moves with your voice and the note says whether Kalipso hears you. Nothing is recorded or sent. While listening, the reader names the microphone and, when no sound arrives from it for a few seconds (muted or unplugged), says so and shows its controls again.

Kalipso has no voice commands: words you say only ever follow the script. Earlier versions had commands for moving between sections; they were removed in 2.2, because a script containing their words could move the reader. Use keys, a pedal, a clicker or the phone remote to move.

### Keys, foot pedals and clickers

With the window small, or while reading:

| Key | Action |
| --- | --- |
| → ↓ Page Down | Next word |
| ← ↑ Page Up | Previous word |
| Space, Play/Pause | Start or pause (scrolling or listening) |
| + and − | Faster / slower steady speed |
| Home | Back to the start |
| Escape | Pause; closes the scripts over the text |
| Ctrl+Enter | Start or pause, also while writing |
| Ctrl++ and Ctrl+− | Larger / smaller text |
| F9 | Show or hide the scripts |

**Settings → Keys and pedals** gives any action another key (for example the key a foot pedal sends), adds keys for the previous and next section, or gives a built-in key a different action, such as the arrows changing the speed. A key belongs to one action at a time; clearing it restores the built-in keys. Escape always pauses.

Most Bluetooth foot pedals and presentation clickers send Page Up/Down or arrow keys, so they work without setup.

### Presenting with LibreOffice Impress

**Reader ⋯ → Present with Impress…** imports the speaker notes of an `.odp` or `.pptx` presentation as a new script, opens the presentation in LibreOffice Impress, and follows the slideshow: when Impress shows a slide, the reader moves to the start of that slide's notes. It matches the notes' first line, not the slide number, so edits to the script and slides without notes are fine. The status line says what is happening ("Following Impress · slide 3", "slide 2 has no notes", "The slideshow ended").

Kalipso talks to LibreOffice only through a local named pipe that it asks LibreOffice to open for this presentation; nothing goes over the network, and the presentation is never changed. **Stop following Impress** (same menu) ends it; LibreOffice stays open. On Linux this needs LibreOffice's Python bridge (`python3-uno` on Debian and Ubuntu). It is tested on Linux with LibreOffice 25.2; Windows and macOS use the Python bundled with LibreOffice but have not been verified yet.

### Phone remote

**Settings → Remote control → Allow a phone on this network to control the reader** shows a link and a QR code. Open it on a phone on the same network to start, pause, step, jump to the previous or next section and change speed. The page never receives the script text. A new secret link is created every time you turn it on, and it stops when Kalipso closes. Windows and some Linux firewalls ask whether to allow Kalipso on the network the first time; the phone must be on the same network as the computer.

### Mirroring

**Settings → Mirror text** flips the reader horizontally (beam-splitter teleprompter glass), vertically, or both. The controls stay readable.

## Screen sharing

On **Windows** 10 version 2004 or later, **Settings → Hide the reader from screen sharing and recordings** asks Windows to leave the reader out of screen captures (Microsoft's `WDA_EXCLUDEFROMCAPTURE`). It is on by default. Check what other participants see before relying on it.

On **macOS** an app cannot reliably hide its own window: Apple documents the old "do not share" window setting as unused, and screen-sharing apps built on ScreenCaptureKit capture the window anyway. Share a single window (for example your slides) instead of the whole screen.

On **Linux**, applications cannot hide their own windows from capture; the compositor decides. On **KDE Plasma 6.6 or later**, Kalipso's KWin script asks KWin to leave the window out of screen sharing while it is under the camera; **Settings → Hide the reader from screen sharing and recordings** turns this off and on (on by default). Without the script, focus the window, press Alt+F3 and choose **More Actions → Hide from Screencast**. On GNOME and on X11 no application or script can hide a window from capture: share a single window instead of the whole screen.

On **Plasma with Wayland** only KWin can place windows. While Kalipso runs it loads its KWin script
into KWin (nothing to install or enable): the reader opens at the top of the screen, under the camera,
stays above other windows and asks for the same exclusion where KWin lets scripts set it. The script
also tells Kalipso when its window is minimized and where you put the reader. It is unloaded when
Kalipso quits, and replaced when Kalipso starts (a copy left by a Kalipso that crashed never stays in
charge). On X11 the reader does this itself. Drag the reader by its title to move
it, and by any edge to resize it.

The same script is also packaged as **Kalipso companion** (System Settings → Window Management → KWin
Scripts) for anyone who wants it enabled permanently; Kalipso then uses that one.

## Speech

**Settings → Voice**:

- On Linux, **Language you speak** lists the languages Kalipso can recognise, each marked *ready* or with the size of its download. Choosing one that is not ready downloads its speech model once, with **Cancel download** while it runs; Kalipso picks the engine (Moonshine where it has the language, Vosk otherwise). Arrow keys and typing open the list rather than choosing at once, so nothing downloads by accident. The note under the list says where the downloads go (`~/.local/share/kalipso`) and their licences (Moonshine's models MIT, Vosk's Apache 2.0). A language Kalipso downloaded shows where it is and how much space it takes, with **Remove** and **Download again** (which replaces it with a fresh copy, checked against its fingerprint); models installed by hand are never touched. Once a language is installed, following your voice needs no internet. See [voice following on Linux](local-engine.md).
- **Other model installed by hand…** takes a Moonshine or Vosk model folder and library you installed yourself.
- **System · on-device** (macOS and Windows): the operating system's recognizer. See [macOS and Windows builds](system-speech.md).

Listening only starts when you press **Start**, and stops when you pause, minimize the window or close Kalipso.

## Interface

**Settings** also has light and dark appearances, reader colors (including automatic colors, see [accessibility](accessibility.md)), interface text size up to 200%, reader text size, and the interface language (system or one of 39 languages, listed by their native names; applies after a restart). This choice is independent of the language you speak. New translations await native-speaker review; see [translation guidelines](translations.md).
