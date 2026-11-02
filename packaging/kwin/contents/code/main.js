// SPDX-License-Identifier: AGPL-3.0-or-later
// Kalipso companion for KWin. Kalipso has one window: you write in it, and it turns into the
// reader. While its title starts with an invisible mark (U+2060) the window is reading: KWin
// takes its frame off, keeps it above other windows, places it near the camera and, where KWin
// supports it, leaves it out of screen captures. When the mark goes, the window is put back as it
// was. Other participants' view is the only proof that a capture excluded it.
const READING_MARK = '\u2060';
const states = {}; // By window id: reading, where it was before (writing), and going back there.
let readerPlace = null; // Where the person last put the reader in this session.
let applying = false; // Geometry changes made here are not the person's.

function isKalipso(window) {
    const identity = String(window.resourceClass);
    return identity === 'io.github.brianidavide.kalipso' || identity === 'kalipso';
}

// Kalipso chooses the reader's size; KWin keeps it where the person put it, or at the top of the
// screen, centred under the camera (or under a top panel).
function placeReader(window) {
    const frame = window.frameGeometry;
    if (readerPlace) {
        frame.x = readerPlace.x;
        frame.y = readerPlace.y;
    } else {
        const area = workspace.clientArea(KWin.PlacementArea, window);
        frame.x = area.x + Math.round((area.width - frame.width) / 2);
        frame.y = area.y;
    }
    applying = true;
    window.frameGeometry = frame;
    applying = false;
}

function update(window) {
    const id = String(window.internalId);
    const state = states[id] || (states[id] = {reading: false, writing: null, returning: false});
    const reading = String(window.caption).indexOf(READING_MARK) === 0;
    if (reading === state.reading)
        return;
    state.reading = reading;
    applying = true;
    if (reading) {
        state.writing = copy(window.frameGeometry);
        window.noBorder = true;
        window.keepAbove = true;
        if ('excludeFromCapture' in window)
            window.excludeFromCapture = true;
        applying = false;
        placeReader(window);
        return;
    }
    window.keepAbove = false;
    window.noBorder = false;
    if ('excludeFromCapture' in window)
        window.excludeFromCapture = false;
    applying = false;
    // A move made while the frame and the size change is lost: the window goes back to its place
    // once it has its writing size and frame again.
    state.returning = true;
}

function copy(frame) {
    return {x: frame.x, y: frame.y, width: frame.width, height: frame.height};
}

function watch(window) {
    if (!isKalipso(window))
        return;
    const state = () => states[String(window.internalId)];
    window.captionChanged.connect(() => update(window));
    // Kalipso resizes the reader: it stays in its place, or centred under the camera.
    window.frameGeometryChanged.connect(() => {
        const current = state();
        if (!current || applying || window.move || window.resize)
            return;
        if (current.reading) {
            placeReader(window);
        } else if (current.returning && current.writing && window.frameGeometry.width === current.writing.width &&
                   window.frameGeometry.height === current.writing.height) {
            current.returning = false;
            const frame = window.frameGeometry;
            frame.x = current.writing.x;
            frame.y = current.writing.y;
            applying = true;
            window.frameGeometry = frame;
            applying = false;
        }
    });
    window.interactiveMoveResizeFinished.connect(() => {
        if (state() && state().reading)
            readerPlace = {x: window.frameGeometry.x, y: window.frameGeometry.y};
    });
    window.closed.connect(() => delete states[String(window.internalId)]);
    update(window);
}

workspace.windowAdded.connect(watch);
workspace.windowList().forEach(watch);
