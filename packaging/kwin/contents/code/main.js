// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Kalipso companion for KWin. Kalipso has one window: you write in it, and it goes under the camera to
// read. While its title starts with an invisible mark (U+2060) the window is under the camera: KWin
// takes its frame off, keeps it above other windows, places it near the camera and, where KWin
// supports it and the person did not turn it off (a second mark, U+2063), leaves it out of screen
// captures. When the mark goes, the window is put back as it was. Other participants' view is the
// only proof that a capture excluded it.
//
// On Wayland Kalipso is not told when its window is minimized, nor where the window is: this script
// tells it (D-Bus), so reading pauses with the window out of sight, and the place the person gives
// the reader is kept for the next start.
const READING_MARK = '⁠';
const SHAREABLE_MARK = '⁣';
const APP = ['io.github.sansevieria_ou.Kalipso.App', '/App', 'io.github.sansevieria_ou.Kalipso.App'];
const states = {}; // By window id: reading, excluded, where it was before (writing), going back there.
const windows = {}; // By window id: Kalipso's windows.
let readerPlace = null; // Where the person last put the reader (kept by Kalipso across starts).
let applying = false; // Geometry changes made here are not the person's.

function isKalipso(window) {
    const identity = String(window.resourceClass);
    return identity === 'io.github.sansevieria_ou.kalipso' || identity === 'kalipso';
}

function tell(method, value) {
    callDBus(APP[0], APP[1], APP[2], method, value);
}

// The saved place, only while the whole reader fits on a screen there: a monitor that was
// unplugged, or a smaller one, puts it back under the camera.
function placeFits(place, frame) {
    try {
        for (const output of workspace.screens) {
            const area = workspace.clientArea(KWin.PlacementArea, output, workspace.currentDesktop);
            if (place.x >= area.x && place.y >= area.y && place.x + frame.width <= area.x + area.width &&
                place.y + frame.height <= area.y + area.height)
                return true;
        }
        return false;
    } catch (error) {
        return true; // An older KWin without these: keep the person's place.
    }
}

// Kalipso chooses the reader's size; KWin keeps it where the person put it, or at the top of the
// screen, centred under the camera (or under a top panel).
function placeReader(window) {
    const frame = window.frameGeometry;
    if (readerPlace && placeFits(readerPlace, frame)) {
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
    const state = states[id] || (states[id] = {reading: false, excluded: false, writing: null, returning: false});
    const caption = String(window.caption);
    const reading = caption.indexOf(READING_MARK) === 0;
    const excluded = reading && caption.charAt(1) !== SHAREABLE_MARK;
    if (excluded !== state.excluded && 'excludeFromCapture' in window)
        window.excludeFromCapture = excluded;
    state.excluded = excluded;
    if (reading === state.reading)
        return;
    state.reading = reading;
    applying = true;
    if (reading) {
        state.writing = copy(window.frameGeometry);
        window.noBorder = true;
        window.keepAbove = true;
        applying = false;
        placeReader(window);
        return;
    }
    window.keepAbove = false;
    window.noBorder = false;
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
    const id = String(window.internalId);
    windows[id] = window;
    const state = () => states[id];
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
        if (!state() || !state().reading)
            return;
        readerPlace = {x: window.frameGeometry.x, y: window.frameGeometry.y};
        tell('SetReaderPlace', readerPlace.x + ',' + readerPlace.y);
    });
    window.minimizedChanged.connect(() => tell('SetMinimized', window.minimized));
    window.closed.connect(() => {
        delete states[id];
        delete windows[id];
    });
    // The place kept from an earlier start, before the reader first goes under the camera.
    callDBus(APP[0], APP[1], APP[2], 'ReaderPlace', function (place) {
        const parts = String(place || '').split(',');
        if (parts.length === 2 && !readerPlace && !isNaN(Number(parts[0])) && !isNaN(Number(parts[1])))
            readerPlace = {x: Number(parts[0]), y: Number(parts[1])};
    });
    update(window);
}

// A monitor added or removed: the reader goes back onto a screen where it fits.
workspace.screensChanged.connect(() => {
    for (const id in windows)
        if (states[id] && states[id].reading)
            placeReader(windows[id]);
});
workspace.windowAdded.connect(watch);
workspace.windowList().forEach(watch);
