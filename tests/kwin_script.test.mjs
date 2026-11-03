// SPDX-License-Identifier: AGPL-3.0-or-later
// Kalipso's KWin script (packaging/kwin/contents/code/main.js) against a stand-in for KWin's
// scripting API: the title marks frame, stack and exclude the window under the camera, the person's
// choice about screen sharing is followed, the place given to the reader is kept and checked against
// the monitors, and Kalipso is told when its window is minimized.
//
//   node tests/kwin_script.test.mjs
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../packaging/kwin/contents/code/main.js', import.meta.url), 'utf8');

function signal() {
    const handlers = [];
    return {connect: handler => handlers.push(handler), emit: (...args) => handlers.forEach(handler => handler(...args))};
}

function kwin({savedPlace = ''} = {}) {
    const laptop = {name: 'Laptop', area: {x: 0, y: 0, width: 1280, height: 800}};
    const monitor = {name: 'Monitor', area: {x: 1280, y: 0, width: 1920, height: 1080}};
    const calls = [];
    const workspace = {
        screens: [laptop, monitor],
        currentDesktop: {},
        windowAdded: signal(),
        screensChanged: signal(),
        windowList: () => [],
        clientArea: (option, where) => ({...(where.area || laptop.area)}),
    };
    const context = {
        workspace,
        KWin: {PlacementArea: 0},
        callDBus: (service, path, iface, method, ...args) => {
            calls.push({service, path, iface, method, args: args.filter(arg => typeof arg !== 'function')});
            const callback = args.find(arg => typeof arg === 'function');
            if (method === 'ReaderPlace' && callback)
                callback(savedPlace);
        },
    };
    vm.createContext(context);
    vm.runInContext(source, context);
    return {workspace, calls, laptop, monitor};
}

function kalipsoWindow(caption = 'Talk — Kalipso') {
    return {
        resourceClass: 'io.github.brianidavide.kalipso',
        caption,
        internalId: '{kalipso}',
        frameGeometry: {x: 200, y: 150, width: 624, height: 384},
        noBorder: false,
        keepAbove: false,
        excludeFromCapture: false,
        minimized: false,
        move: false,
        resize: false,
        captionChanged: signal(),
        frameGeometryChanged: signal(),
        interactiveMoveResizeFinished: signal(),
        minimizedChanged: signal(),
        closed: signal(),
    };
}

function setCaption(window, caption) {
    window.caption = caption;
    window.captionChanged.emit();
}

const READING = '⁠', SHAREABLE = '⁣';

// Under the camera: no frame, above the others, out of screen captures, centred at the top.
{
    const {workspace, calls} = kwin();
    const window = kalipsoWindow();
    workspace.windowAdded.emit(window);
    assert.equal(calls[0].method, 'ReaderPlace', 'the place kept from an earlier start is asked for');
    setCaption(window, READING + 'Talk — Kalipso');
    assert.equal(window.noBorder, true);
    assert.equal(window.keepAbove, true);
    assert.equal(window.excludeFromCapture, true);
    assert.deepEqual([window.frameGeometry.x, window.frameGeometry.y], [328, 0], 'centred under the camera');
    // The person lets screen sharing show it: the second mark.
    setCaption(window, READING + SHAREABLE + 'Talk — Kalipso');
    assert.equal(window.excludeFromCapture, false);
    assert.equal(window.noBorder, true, 'still under the camera');
    setCaption(window, READING + 'Talk — Kalipso');
    assert.equal(window.excludeFromCapture, true);
    // Back to writing: the frame and the stacking come back, and captures see it again.
    setCaption(window, 'Talk — Kalipso');
    assert.equal(window.noBorder, false);
    assert.equal(window.keepAbove, false);
    assert.equal(window.excludeFromCapture, false);
}

// Writing with screen sharing allowed: nothing is excluded (the second mark only counts under the camera).
{
    const {workspace} = kwin();
    const window = kalipsoWindow(SHAREABLE + 'Talk — Kalipso');
    workspace.windowAdded.emit(window);
    assert.equal(window.excludeFromCapture, false);
    assert.equal(window.noBorder, false);
}

// The place the person gives the reader is told to Kalipso, and used at the next start.
{
    const {workspace, calls} = kwin();
    const window = kalipsoWindow();
    workspace.windowAdded.emit(window);
    setCaption(window, READING + 'Talk — Kalipso');
    window.frameGeometry = {...window.frameGeometry, x: 1500, y: 20};
    window.interactiveMoveResizeFinished.emit();
    const told = calls.find(call => call.method === 'SetReaderPlace');
    assert.ok(told, 'Kalipso is told where the reader is');
    assert.deepEqual(told.args, ['1500,20']);
    assert.equal(told.service, 'io.github.brianidavide.Kalipso.App');
}
{
    const {workspace, monitor} = kwin({savedPlace: '1500,20'});
    const window = kalipsoWindow();
    workspace.windowAdded.emit(window);
    setCaption(window, READING + 'Talk — Kalipso');
    assert.deepEqual([window.frameGeometry.x, window.frameGeometry.y], [1500, 20], 'where it was put last time');
    // That monitor is unplugged: back under the camera of the one that is left.
    workspace.screens.splice(workspace.screens.indexOf(monitor), 1);
    workspace.screensChanged.emit();
    assert.deepEqual([window.frameGeometry.x, window.frameGeometry.y], [328, 0]);
}
// A saved place that no longer fits any monitor is not used (counter-test of the check).
{
    const {workspace} = kwin({savedPlace: '5000,3000'});
    const window = kalipsoWindow();
    workspace.windowAdded.emit(window);
    setCaption(window, READING + 'Talk — Kalipso');
    assert.deepEqual([window.frameGeometry.x, window.frameGeometry.y], [328, 0]);
}

// Minimized: Kalipso is told, since on Wayland nothing else tells it.
{
    const {workspace, calls} = kwin();
    const window = kalipsoWindow();
    workspace.windowAdded.emit(window);
    window.minimized = true;
    window.minimizedChanged.emit();
    const told = calls.filter(call => call.method === 'SetMinimized');
    assert.equal(told.length, 1);
    assert.deepEqual(told[0].args, [true]);
}

// Other applications' windows are left alone.
{
    const {workspace, calls} = kwin();
    const other = {...kalipsoWindow(READING + 'Other'), resourceClass: 'org.kde.dolphin'};
    workspace.windowAdded.emit(other);
    assert.equal(other.noBorder, false);
    assert.equal(calls.length, 0);
}

console.log('kwin_script: all checks passed');
