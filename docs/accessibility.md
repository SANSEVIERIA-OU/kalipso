# Accessibility

Kalipso targets **WCAG 2.2 Level AA**, including applicable Level A criteria, across the website, standalone application, Plasma widget and GNOME extension. For native software, the interpretation follows [WCAG2ICT](https://www.w3.org/TR/wcag2ict-22/). WCAG 3.0 is a draft, not this product's conformance target.

Kalipso 2.0 does **not** claim full AA conformance yet: screen-reader, scaling and multi-monitor testing on real machines is still to be done.

## Colors and reading

Both light and dark reading palettes have an opaque background. Normal text, placeholders, remaining words and already-read words use at least **4.5:1** contrast. Essential control borders and focus indicators use at least **3:1** against their intended backgrounds. Read words remain available in the script; dimming does not remove them. Position counters and explicit state labels supplement color.

Choose reader colors in Settings. The standalone application's interface also has light/dark appearances and a 100%, 150% or 200% text-size setting. The shell frontends respect desktop text scaling and provide separate reading-size controls. Keyboard navigation, visible focus, scrolling and explicit edit labels remain available.

Automatic color changes select a complete, verified palette, rather than applying an arbitrary sampled background or using transparent text surfaces. Changes are rate limited and use a luminance dead band to avoid rapid alternation. A denied, failed or unsupported sample leaves an opaque palette in place.

| Surface | Automatic color source |
| --- | --- |
| macOS standalone | ScreenCaptureKit samples the region behind the reader, excluding the reading window. Selecting automatic screen colors can request Screen Recording permission. No image files are saved. Real-device capture and multi-monitor placement still need validation. |
| Windows standalone | A few pixels immediately outside the reader, every two seconds. These describe the surroundings, not pixels hidden behind the window. No video stream is opened. |
| Linux standalone on X11 | Small samples immediately outside the reader, every two seconds. |
| Linux standalone on Wayland | **Choose background window…** opens the desktop ScreenCast portal. Colors follow a sparse sample of that selected window through PipeWire, including when the reader overlaps it. The selection is explicit and is not restored automatically. |
| Plasma widget | **Desktop theme** follows the desktop palette. **Choose background window…** uses the same portal/PipeWire window sampling as the standalone app, through the updated Kalipso companion. Desktop theme and manual colors work without the companion. |
| GNOME extension | A surrounding pixel sampled through GNOME Shell every two seconds while automatic mode is visible. No image files or video stream are produced. |
| Browser demonstration | The computed opaque background of its containing page. It cannot inspect other applications. |

Automatic mode is separate from microphone capture. Sampling stops when the reader is hidden or automatic mode is disabled. No captured screen data is sent anywhere.

### Window colors on KDE/Wayland

In Settings, choose **Choose background window…**, then select the application behind the reader in the desktop permission dialog. Only windows are requested, with the cursor excluded and without a persistent permission token. The standalone app opens its reader before asking. Choose **Stop window sharing**, Light, Dark or (in Plasma) Desktop theme to end capture. Hiding the reader also closes the session; reopening requires another explicit selection. Closing the selected window, cancelling permission or revoking sharing leaves the last opaque palette in place and reports the state. A disconnected Plasma client releases its session, with a ten-second heartbeat timeout as a fallback.

This follows the **selected window's overall colors**, not a geometrically exact patch beneath the reader. Moving the reader over a different application does not select that application automatically: stop sharing and choose the new background window. This distinction avoids relying on global window coordinates that Wayland clients do not reliably have. The reader must not be selected as its own background.

The desktop streams that window continuously while sharing is active. Kalipso requests at most one frame per second, samples no more than 256 pixels every two seconds, and discards each buffer after use. Actual compositor costs depend on the desktop and source window; the low sampling rate is not a zero-cost capture guarantee. Frames stay in memory, are never saved or sent, and are independent of speech processing. Both frontends use the existing contrast palettes, luminance dead band and minimum palette hold time.

Runtime requirements: PipeWire, `xdg-desktop-portal`, and a desktop backend with window ScreenCast support (on KDE, `xdg-desktop-portal-kde`). Plasma also requires the updated Kalipso companion. The build requires PipeWire development headers and pkg-config. Missing services, unsupported stream formats and permission failures keep the readable fallback and allow an explicit retry.

## Keyboard and text

- Tab/Shift+Tab move between controls; Enter/Space activate buttons. Escape closes a settings pane or the scripts panel, and pauses the reader.
- In the standalone reader and the shell frontends, arrow keys and Page Up/Down step through the script, Space starts or pauses, + and − change the steady speed and Home returns to the start. In the standalone app Ctrl+Enter starts or pauses, Ctrl++ and Ctrl+− change the text size and F9 shows the scripts.
- Manual and steady modes are full alternatives to speaking. Pause stops voice following without changing the script.
- Mirroring affects only the script text; the controls stay readable.
- The browser demonstration has a skip link, labelled fields, status messages, reduced-motion behaviour and forced-colors support.

## WCAG 2.2 AA audit of the standalone app

Each Level A and AA criterion, read for native software as [WCAG2ICT](https://www.w3.org/TR/wcag2ict-22/) does, with what shows it. **Meets** means a check in this repository shows it. **Partly** says what is missing. **Not applicable** is explained. The website is checked by the browser checker (axe-core A/AA in three engines); the Plasma widget and the GNOME extension are not covered by this table yet.

How it was checked:
- **AT-SPI:** `tests/atspi_audit.py`, the interface Orca uses on Linux. It lists every control on the screen without a name, and walks the Tab key through the window.
- **Orca 48:** run against Kalipso, it read the window title. Its speech log could not be kept in this environment, so the spoken walkthrough is left for a real session.
- **UI and layout tests:** `tests/ui_tests.cpp` and `tests/layout_matrix.cpp`.
- **Contrast:** `tests/contrast.test.mjs`.

| Criterion | Status | Evidence |
| --- | --- | --- |
| 1.1.1 Non-text content | Meets | Icon-only buttons have names (`atspi_audit.py --require-name`: 0 unnamed); icons follow the text colour. |
| 1.2.1–1.2.5 Time-based media | Not applicable | The app plays no audio or video. |
| 1.3.1 Info and relationships | Meets | Settings fields are labelled (`settingsFieldsHaveAccessibleNames`); the status is a status bar (`statusHasAnAccessibleRoleAndName`). Sections keep their `#` in the text a screen reader reads. |
| 1.3.2 Meaningful sequence | Meets | Reading and focus order go top bar, scripts, text, bottom bar (`atspi_audit.py --tab`, `tabGoesTheWayTheScreenReads`). |
| 1.3.3 Sensory characteristics | Meets | No instruction depends on shape, colour or place alone. |
| 1.3.4 Orientation | Meets | Any window shape works; tested from 320×240 to 1280×800. |
| 1.3.5 Identify input purpose | Not applicable | No fields ask for personal data. |
| 1.4.1 Use of color | Meets | The current word is underlined as well as tinted; states are written ("Listening", "Paused"). |
| 1.4.2 Audio control | Not applicable | No sound plays. |
| 1.4.3 Contrast (minimum) | Meets | 4.5:1 for text in every palette (`contrast.test.mjs`, `accessiblePalettesKeepReadTextAndSelectionLegible`). |
| 1.4.4 Resize text | Meets | Interface text at 200% with no overlap or clipping (`layout_matrix`, `interfaceTextCanGrowWithoutHorizontalScrolling`); script text 18–96 pt. |
| 1.4.5 Images of text | Meets | None. |
| 1.4.10 Reflow | Meets | 200% at 320 px without horizontal scrolling (`interfaceTextCanGrowWithoutHorizontalScrolling`). |
| 1.4.11 Non-text contrast | Meets | Control borders and focus 3:1 (`contrast.test.mjs`). |
| 1.4.12 Text spacing | Meets | Line spacing up to 150% in Settings; nothing clips at 150% (`layout_matrix`). |
| 1.4.13 Content on hover or focus | Partly | Tooltips close with Esc and moving the pointer, but they cannot be hovered: Qt closes a tooltip when the pointer leaves the control. Everything in a tooltip is also in the accessible name or the docs. |
| 2.1.1 Keyboard | Meets | Every action has a key (reader keys, Ctrl+Enter, F9, Ctrl±, format shortcuts); Settings by Tab. |
| 2.1.2 No keyboard trap | Meets | Tab and Shift+Tab leave the text in both sizes of the window (`tabLeavesTheScriptAndComesBack`). Before 2.0 they did not: the AT-SPI audit found it. |
| 2.1.4 Character key shortcuts | Meets | Single keys work only while the reader has the focus, and **Settings → Keys and pedals** changes them. |
| 2.2.1 Timing adjustable | Meets | Steady speed changes while reading, and reading pauses; the bars' fading can be turned off. Messages stay in the status for 5 s and are also announced. |
| 2.2.2 Pause, stop, hide | Meets | Scrolling pauses with Space, Esc or the button. |
| 2.3.1 Three flashes | Meets | Nothing flashes; automatic colours change at most every 2.5 s (`contrast.test.mjs`). |
| 2.4.1 Bypass blocks | Not applicable | One window, no repeated blocks (WCAG2ICT). |
| 2.4.2 Page titled | Meets | The window is titled "Script — Kalipso"; Settings has its own title (Orca reads them). |
| 2.4.3 Focus order | Meets | The top bar from left to right, the scripts when they are open, the text, the bottom bar (`tabGoesTheWayTheScreenReads`, with a counter-test). |
| 2.4.4 Link purpose | Not applicable | No links in the app. |
| 2.4.5 Multiple ways | Not applicable | Not a set of pages. |
| 2.4.6 Headings and labels | Meets | Settings pages and sections are named after what they set. |
| 2.4.7 Focus visible | Meets | A 2 px focus ring in the drawn style; Breeze's own ring in the desktop's style. |
| 2.4.11 Focus not obscured (minimum) | Meets | While writing, the text stays between the bars (`layout_matrix`: "goes under a bar while writing"). |
| 2.5.1 Pointer gestures | Meets | No path or multi-finger gestures. |
| 2.5.2 Pointer cancellation | Meets | Buttons act on release (Qt). |
| 2.5.3 Label in name | Meets | Accessible names repeat the visible labels. |
| 2.5.4 Motion actuation | Not applicable | Nothing reacts to device motion. |
| 2.5.7 Dragging movements | Partly | Under the camera, the window moves by dragging its title. Under the camera, Expand and the system's keyboard move (Alt+F3 on Plasma) are the alternatives, but there is no move-by-keys inside Kalipso. |
| 2.5.8 Target size (minimum) | Meets | Controls are at least 36 px (`Design::Metric::control`). |
| 3.1.1 Language of page | Meets | The interface follows the system or Settings language. |
| 3.1.2 Language of parts | Not applicable | The script is in the person's language. |
| 3.2.1 On focus | Meets | Focus never changes a setting or the window. |
| 3.2.2 On input | Meets | Settings apply in place, without moving the focus or the window. |
| 3.2.3 Consistent navigation | Not applicable | One window. |
| 3.2.4 Consistent identification | Meets | The same action has the same name and icon everywhere. |
| 3.2.6 Consistent help | Not applicable | No help mechanism repeated across pages. |
| 3.3.1 Error identification | Meets | Import, export, save and download errors say what failed. |
| 3.3.2 Labels or instructions | Meets | Every field has a label; the welcome says the three steps. |
| 3.3.3 Error suggestion | Meets | Errors say what to do ("Check the destination and available disk space"). |
| 3.3.4 Error prevention (legal, financial, data) | Not applicable | No such transactions. Deleting a script asks first. |
| 3.3.7 Redundant entry | Not applicable | No multi-step forms. |
| 3.3.8 Accessible authentication (minimum) | Not applicable | No sign-in. |
| 4.1.2 Name, role, value | Meets | `atspi_audit.py`: every control has a name and its role (button, check box, combo box, list, text, status bar); Orca reads them. |
| 4.1.3 Status messages | Meets | Status changes are announced without moving the focus (QAccessibleAnnouncementEvent on Qt 6.8+, an alert before). |

Still to do before claiming conformance:
- NVDA on Windows and VoiceOver on macOS;
- Orca on a real Plasma and a real GNOME session, with speech;
- the Plasma widget and the GNOME extension with the same audit;
- the two **Partly** rows.

## Checks

`node tests/contrast.test.mjs` checks every palette pair, the exact contrast thresholds, opaque colours and rapid-alternation protection. The Qt UI tests check read-text and selection contrast, accessible names and status roles, wrapping controls, 200% interface text at 320 px without horizontal scrolling, and editing behaviour. `portal_colors` and `portal_colors_stream` exercise the window-colour portal lifecycle with simulated consent and real PipeWire frames.

The browser checker runs axe-core A/AA rules in Chromium, Firefox and WebKit, plus text enlargement, spacing overrides, narrow viewports, keyboard editing and deliberate failures that prove the checks catch regressions. See [CONTRIBUTING.md](../CONTRIBUTING.md#checks) to run it.

Automated checks do not establish conformance on their own. Screen-reader testing (Orca, NVDA, VoiceOver), native display scaling, multiple monitors and operating-system permission dialogs still need people and real machines; please report what you find.
