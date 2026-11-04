// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import "tracker.mjs" as Tracking
import "Tokens.js" as Design
import "contrast.mjs" as Contrast

PlasmoidItem {
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    id: root

    // Saved per widget in the Plasma configuration (contents/config/main.xml).
    readonly property var config: Plasmoid.configuration
    // "window" while following a window chosen through the sharing portal.
    property bool followWindow: false
    readonly property string appearance: followWindow ? "window" : config.appearance
    property string mode: config.mode          // "manual", "steady" or "voice"
    property int speed: config.speed           // Steady speed, words per minute
    property string mirror: config.mirror      // "none", "horizontal" or "vertical"
    property real readerSize: config.readerSize
    property string readerFont: config.readerFont      // "" is the interface font
    property int lineSpacing: config.lineSpacing       // Percent of the normal line height
    property int countdownLeft: 0                      // Seconds before steady scrolling starts
    // Font choices: a generic serif, plus fonts designed for easier reading when installed.
    readonly property var fontChoices: {
        const installed = Qt.fontFamilies()
        return ["Serif"].concat(["Atkinson Hyperlegible", "Atkinson Hyperlegible Next", "OpenDyslexic", "Lexend"]
            .filter(family => installed.indexOf(family) >= 0))
    }

    property string windowTheme: "dark"
    property var windowPalette: new Contrast.PaletteChoice()
    readonly property var n: Design.tokens.native
    readonly property var p: Design.tokens.palette[appearance === "window" ? windowTheme
        : appearance === "auto" ? (desktopPalette.window.hslLightness < 0.5 ? "dark" : "light") : appearance]
    readonly property var c: ({
        overlay: p.background, overlayText: p.text, overlayMuted: p.muted, overlaySurface: p.surface,
        overlayLine: p.hover, overlayBorder: p.border, focus: p.focus, reading: p.accent})
    // The app's typeface, embedded in the package (see contents/fonts); the desktop font until it loads.
    readonly property string uiFont: atkinson.status === FontLoader.Ready ? atkinson.font.family : Qt.application.font.family
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSans-Regular.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSans-Bold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansArabic-Regular.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansArabic-Bold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansDevanagari-Regular.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansDevanagari-Bold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansBengali-Regular.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansBengali-Bold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansTelugu-Regular.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansTelugu-Bold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansCJKjp-Regular.otf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansCJKjp-Bold.otf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansCJKsc-Regular.otf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/NotoSansCJKsc-Bold.otf") }
    FontLoader { id: atkinson; source: Qt.resolvedUrl("../fonts/AtkinsonHyperlegibleNext-Regular.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/AtkinsonHyperlegibleNext-Italic.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/AtkinsonHyperlegibleNext-SemiBold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/AtkinsonHyperlegibleNext-Bold.ttf") }
    FontLoader { source: Qt.resolvedUrl("../fonts/AtkinsonHyperlegibleNext-BoldItalic.ttf") }
    // How each icon moves on hover and keyboard focus, as in the app and on the website.
    function motionOf(icon) {
        return ({"go-next": "forward", "go-previous": "back", "go-first": "back", "media-playback-start": "grow",
                 "media-playback-pause": "grow", "view-list-details": "down", "document-edit": "tilt",
                 "configure": "grow", "dialog-ok": "grow", "document-open": "down", "overflow-menu": "down"})[icon] || ""
    }

    // Until the person writes one, the sample script is in the desktop's language.
    readonly property string sampleScript: i18n("A little preparation makes room for a better conversation. [smile] Take your time. Read the first sentence aloud and watch the words follow you.")
    property var tracker: new Tracking.Tracker(config.script || sampleScript)

    // The app's scripts, through the companion: the widget reads the same library as Kalipso.
    // config.scriptId is the script read from it; "" is the widget's own script (config.script).
    property var libraryScripts: []
    readonly property bool libraryAvailable: libraryScripts.length > 0
    readonly property bool fromLibrary: config.scriptId !== ""
    property string loadedModified: ""   // The library script's change time, to reload it when the app saves.
    function positions() {
        try { return JSON.parse(config.positions || "{}") } catch (error) { return {} }
    }
    function rememberPosition() {
        const character = tracker.position >= 0 ? tracker.words[tracker.position].start : -1
        if (!fromLibrary) {
            config.position = character
            return
        }
        const saved = positions()
        saved[config.scriptId] = character
        config.positions = JSON.stringify(saved)
    }
    function minutesOf(script) {
        const wordsPerMinute = script.speed > 0 ? script.speed : speed
        return Math.max(1, Math.ceil(script.words / wordsPerMinute))
    }
    function refreshLibrary() {
        audio.listScripts(function(info) {
            if (info.failed)
                return   // The companion stopped or is restarting: keep what is being read.
            const listed = info.available ? info.scripts : []
            if (JSON.stringify(listed) !== JSON.stringify(libraryScripts))
                libraryScripts = listed
            const ids = libraryScripts.map(item => item.id)
            if (fromLibrary) {
                const entry = libraryScripts.find(item => item.id === config.scriptId)
                if (!entry)
                    useOwnScript()               // Deleted in the app, or the companion is gone.
                else if (entry.modified !== loadedModified)
                    loadScript(entry.id, loadedModified === "")
            } else if (!config.script && ids.length) {
                // Nothing written here yet: the script open in the app, or the newest.
                chooseScript(ids.indexOf(info.current) >= 0 ? info.current : ids[0])
            }
        })
    }
    function chooseScript(id) {
        if (id === config.scriptId && loadedModified !== "")
            return
        stop()
        rememberPosition()
        config.scriptId = id
        loadedModified = ""
        loadScript(id, true)
    }
    // Loads a library script; `fresh` restores where reading was (here, or else in the app). A script
    // changed in the app keeps the reading place through the change.
    function loadScript(id, fresh) {
        audio.getScript(id, function(script) {
            if (script.id !== config.scriptId)
                return
            loadedModified = script.modified
            tracker.setText(script.text)
            if (fresh) {
                const saved = positions()[id]
                const character = saved !== undefined ? saved : script.position
                if (character >= 0)
                    tracker.seekCharacter(character)
                else
                    tracker.seek(-1)
            }
            sectionList = Tracking.sections(tracker.text)
            audio.setScript(tracker.text)
            refresh()
        }, useOwnScript)
    }
    function useOwnScript() {
        if (fromLibrary)
            rememberPosition()
        stop()
        config.scriptId = ""
        loadedModified = ""
        tracker.setText(config.script || sampleScript)
        if (config.position >= 0)
            tracker.seekCharacter(config.position)
        else
            tracker.seek(-1)
        sectionList = Tracking.sections(tracker.text)
        refresh()
    }
    property string markup: ""
    property int readCount: 0
    property int wordCount: 0
    property bool lost: false
    property var sectionList: []
    property string scriptTitle: ""
    property string pane: ""
    readonly property bool atEnd: wordCount > 0 && readCount === wordCount

    signal revealRequested(var item)

    implicitWidth: n.width
    implicitHeight: 360
    // On the desktop the reader is the widget. In a panel there is no room for it: Kalipso's icon
    // opens it in a popup, which stays open while you click into the call or the slides.
    readonly property bool inPanel: Plasmoid.formFactor === PlasmaCore.Types.Horizontal
        || Plasmoid.formFactor === PlasmaCore.Types.Vertical
    preferredRepresentation: inPanel ? compactRepresentation : fullRepresentation
    hideOnWindowDeactivate: false
    Plasmoid.icon: Qt.resolvedUrl("../images/kalipso.svg")

    SystemPalette { id: desktopPalette; colorGroup: SystemPalette.Active }

    function refresh() {
        markup = tracker.words.length
            ? tracker.readingMarkup(c.overlayMuted, c.overlayText, 4, {text: c.overlayText, tint: p.tint, line: p.accent}, true)
            : i18n("Add your script to begin.")
        scriptTitle = Tracking.title(tracker.text) || i18n("Kalipso")
        readCount = tracker.position + 1
        wordCount = tracker.words.length
        lost = tracker.lost
        savePosition.restart()
        if (Qt.application.name === "plasmawindowed")
            console.log("KALIPSO_TEST_STATE " + JSON.stringify({position: tracker.position, status: audio.status}))
    }
    function seek(index) { tracker.seek(index); refresh() }
    function jumpTo(character) { tracker.seekCharacter(character); refresh() }
    function showPane(next) { pane = pane === next ? "" : next }
    function setMode(next) {
        config.mode = next   // onModeChanged stops what was running.
    }
    onModeChanged: {
        stop()
        if (mode === "voice")
            audio.settings()
    }
    readonly property var modeNames: ({manual: i18n("Manual"), steady: i18n("Steady speed"), voice: i18n("Follow my voice")})
    property string openMessage: ""   // Why the app could not be opened, in the companion's words.
    function changeSpeed(delta) { config.speed = Math.max(40, Math.min(400, speed + delta)) }
    // The primary action for the current mode.
    function toggle() {
        if (mode === "manual")
            seek(tracker.position + 4)
        else if (mode === "steady")
            (steady.running || countdown.running) ? stop() : startSteady()
        else if (audio.active || audio.busy)
            audio.stop()
        else
            audio.start(tracker.text)
    }
    function startSteady() {
        if (atEnd)
            seek(-1)
        countdownLeft = Math.max(0, config.countdown)
        countdownLeft > 0 ? countdown.start() : steady.start()
    }
    function stop() {
        countdown.stop()
        countdownLeft = 0
        steady.stop()
        if (audio.active || audio.busy)
            audio.stop()
    }

    onAppearanceChanged: { if (appearance !== "window") windowColors.stop(); refresh() }
    // The excerpt carries its colours: built again when they change (the desktop turned dark, a
    // window behind changed), or it keeps the old ones and can vanish on the new background.
    onCChanged: Qt.callLater(refresh)
    onWindowThemeChanged: refresh()
    onVisibleChanged: if (!visible) { windowColors.stop(); stop() }
    Component.onCompleted: {
        if (!fromLibrary && config.position >= 0)
            tracker.seekCharacter(config.position)
        sectionList = Tracking.sections(tracker.text)
        refresh()
        refreshLibrary()
    }
    // The app may change a script or add one: the list follows while the reader is on screen.
    Timer {
        interval: 10000
        repeat: true
        running: root.expanded || !root.inPanel
        triggeredOnStart: true
        onTriggered: root.refreshLibrary()
    }

    Connections {
        target: root.Window.window
        function onVisibleChanged() { if (!root.Window.window.visible) { windowColors.stop(); root.stop() } }
        function onVisibilityChanged() { if (root.Window.window.visibility === Window.Minimized) windowColors.stop() }
    }

    Timer {
        id: saveScript
        interval: 800
        onTriggered: root.config.script = root.tracker.text
    }
    Timer {
        id: savePosition
        interval: 1000
        onTriggered: root.rememberPosition()
    }
    Timer {
        id: countdown
        interval: 1000
        repeat: true
        onTriggered: {
            root.countdownLeft -= 1
            if (root.countdownLeft <= 0) {
                countdown.stop()
                steady.start()
            }
        }
    }
    Timer {
        id: steady
        interval: Math.max(150, Math.round(60000 / root.speed))
        repeat: true
        onTriggered: {
            if (root.tracker.position >= root.tracker.words.length - 1)
                steady.stop()
            else
                root.seek(root.tracker.position + 1)
        }
    }

    LocalEngine {
        id: audio
        onTranscript: function(event) {
            const lostBefore = root.tracker.lost
            root.tracker.guided = audio.guided
            if (root.tracker.follow(event.text, event.final) || root.tracker.lost !== lostBefore)
                root.refresh()
        }
    }
    WindowColors {
        id: windowColors
        onSampled: function(color) {
            if (root.appearance === "window")
                root.windowTheme = root.windowPalette.sample(color, Date.now())
        }
    }

    // Buttons follow the same rules as the standalone app: one filled primary
    // action, outlined buttons, quiet buttons for window actions, and chosen
    // options marked by a thicker accent border and bold label.
    component ReaderButton: Button {
        id: control
        property bool primary: false
        property bool quiet: false
        property bool chosen: false
        property string iconName: ""
        Accessible.name: text
        activeFocusOnTab: true
        onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { root.revealRequested(control) })
        font.pointSize: Qt.application.font.pointSize
        font.family: root.uiFont
        font.bold: primary || chosen
        // A press shrinks the button slightly, as in the app.
        scale: down ? 0.97 : 1
        implicitHeight: Math.max(root.n.controlHeight, contentItem.implicitHeight + 16)
        implicitWidth: Math.max(contentItem.implicitWidth + 24,
            primary ? Math.max(root.n.primaryWidth, metrics.advanceWidth(i18n("Next words")) + 48) : 0)
        FontMetrics { id: metrics; font: control.font }
        width: Math.min(implicitWidth, parent ? parent.width : implicitWidth)
        padding: 8; leftPadding: 12; rightPadding: 12
        opacity: enabled ? 1 : 0.5 // Disabled controls stay readable but look unavailable.
        background: Rectangle {
            radius: root.n.buttonRadius
            color: control.primary ? (control.down || control.hovered ? root.p.actionHover : root.p.action)
                : control.hovered || control.down || control.chosen ? root.c.overlayLine
                : control.quiet ? "transparent" : root.c.overlaySurface
            // The focus ring shows for keyboard focus only, like :focus-visible on the website.
            border.width: control.visualFocus || control.chosen ? 2 : control.quiet ? 0 : 1
            border.color: control.visualFocus ? root.c.focus : control.chosen ? root.c.reading : root.c.overlayBorder
        }
        // Icon and label centred together; the label wraps when the button is narrow.
        contentItem: Item {
            implicitWidth: row.implicitWidth
            implicitHeight: row.implicitHeight
            RowLayout {
                id: row
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width)
                spacing: 6
                Kirigami.Icon {
                    id: glyph
                    visible: control.iconName !== ""
                    source: control.iconName
                    isMask: true
                    color: control.primary ? root.p.actionText : root.c.overlayText
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: Qt.AlignVCenter
                    // Moves by meaning while hovered or focused from the keyboard. Kirigami durations
                    // follow the Plasma animation speed and are zero when animations are off.
                    readonly property string motion: root.motionOf(control.iconName)
                    readonly property real progress: control.hovered || control.visualFocus ? 1 : 0
                    readonly property real ahead: control.mirrored ? -1 : 1
                    transform: [
                        Translate {
                            x: glyph.progress * glyph.ahead * (glyph.motion === "forward" ? 3 : glyph.motion === "back" ? -3 : 0)
                            y: glyph.progress * (glyph.motion === "down" ? 2 : 0)
                            Behavior on x { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }
                            Behavior on y { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }
                        },
                        Rotation {
                            origin.x: 9; origin.y: 9
                            angle: glyph.progress * (glyph.motion === "turn" ? 45 : glyph.motion === "tilt" ? -14 : 0)
                            Behavior on angle { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }
                        },
                        Scale {
                            origin.x: 9; origin.y: 9
                            xScale: 1 + glyph.progress * (glyph.motion === "grow" ? 0.1 : 0)
                            yScale: xScale
                            Behavior on xScale { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }
                        }
                    ]
                }
                Text {
                    visible: control.text !== ""
                    text: control.text
                    color: control.primary ? root.p.actionText : root.c.overlayText
                    font: control.font
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Layout.fillWidth: true
                }
            }
        }
    }
    component ReaderScrollBar: ScrollBar {
        orientation: Qt.Vertical
        policy: ScrollBar.AsNeeded
        implicitWidth: 24
        height: parent.height
        x: parent.width - width
        y: 0
        visible: size < 1
        contentItem: Rectangle { implicitWidth: 24; implicitHeight: 24; color: root.c.overlayBorder; radius: 4 }
        background: Rectangle { color: root.c.overlay }
    }
    component Note: Label {
        font.family: root.uiFont
        color: root.c.overlayMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    fullRepresentation: Rectangle {
        id: body
        color: root.c.overlay
        radius: root.n.radius
        border.width: 2
        border.color: root.c.overlayBorder
        implicitWidth: root.n.width
        implicitHeight: content.implicitHeight + root.n.padding * 2
        Layout.preferredWidth: root.n.width
        Layout.preferredHeight: Math.min(700, content.implicitHeight + root.n.padding * 2)
        Layout.minimumWidth: 320
        Layout.minimumHeight: 240
        onVisibleChanged: if (!visible) { windowColors.stop(); root.stop() }

        Connections {
            target: body.Window.window
            function onVisibleChanged() { if (!body.Window.window.visible) windowColors.stop() }
            function onVisibilityChanged() { if (body.Window.window.visibility === Window.Minimized) windowColors.stop() }
        }

        // Foot pedals and presentation clickers send these keys.
        Keys.onPressed: function(event) {
            if (root.pane === "edit")
                return
            switch (event.key) {
            case Qt.Key_Right: case Qt.Key_Down: case Qt.Key_PageDown:
                root.seek(root.tracker.position + 1); break
            case Qt.Key_Left: case Qt.Key_Up: case Qt.Key_PageUp:
                root.seek(root.tracker.position - 1); break
            case Qt.Key_Home:
                root.seek(-1); break
            case Qt.Key_Space: case Qt.Key_MediaTogglePlayPause:
                root.toggle(); break
            case Qt.Key_Plus: case Qt.Key_Equal:
                root.changeSpeed(10); break
            case Qt.Key_Minus:
                root.changeSpeed(-10); break
            default:
                return
            }
            event.accepted = true
        }
        Keys.onEscapePressed: function(event) { root.pane = ""; editToggle.forceActiveFocus(); event.accepted = true }

        ScrollView {
            id: outerScroll
            ScrollBar.vertical: ReaderScrollBar {}
            anchors.fill: parent
            anchors.margins: root.n.padding
            rightPadding: ScrollBar.vertical.visible ? 28 : 0
            contentWidth: availableWidth
            clip: true
            Connections {
                target: root
                function onRevealRequested(item) {
                    const top = item.mapToItem(content, 0, 0).y
                    const flick = outerScroll.contentItem
                    if (top < flick.contentY)
                        flick.contentY = top
                    else if (top + item.height > flick.contentY + outerScroll.availableHeight)
                        flick.contentY = Math.min(flick.contentHeight - outerScroll.availableHeight,
                            top + item.height - outerScroll.availableHeight)
                }
            }

            ColumnLayout {
                id: content
                width: outerScroll.availableWidth
                spacing: root.n.gap

                // The script and what to do with it; the settings are in Plasma's dialog (Configure Kalipso…).
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        font.family: root.uiFont
                        visible: !root.libraryAvailable
                        text: root.scriptTitle
                        color: root.c.overlayMuted
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, 220)
                        height: root.n.controlHeight
                        verticalAlignment: Text.AlignVCenter
                    }
                    // Kalipso's scripts, the same as in the app, and the one written here.
                    ReaderButton {
                        objectName: "scriptsButton"
                        visible: root.libraryAvailable
                        quiet: true
                        iconName: "document-open"
                        text: root.scriptTitle
                        Accessible.name: i18n("Script: %1", root.scriptTitle)
                        Accessible.description: i18n("Choose one of Kalipso's scripts")
                        onClicked: scriptMenu.popup(this, 0, height)
                        Menu {
                            id: scriptMenu
                            Instantiator {
                                model: root.libraryScripts
                                delegate: MenuItem {
                                    required property var modelData
                                    font.family: root.uiFont
                                    checkable: true
                                    autoExclusive: true
                                    checked: root.config.scriptId === modelData.id
                                    text: i18n("%1 · %2 min", modelData.title, root.minutesOf(modelData))
                                    onTriggered: root.chooseScript(modelData.id)
                                }
                                onObjectAdded: (index, object) => scriptMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => scriptMenu.removeItem(object)
                            }
                            MenuSeparator {}
                            MenuItem {
                                objectName: "ownScript"
                                font.family: root.uiFont
                                checkable: true
                                autoExclusive: true
                                checked: !root.fromLibrary
                                text: i18n("A script written here")
                                onTriggered: root.useOwnScript()
                            }
                        }
                    }
                    ReaderButton {
                        objectName: "sectionsButton"
                        quiet: true
                        iconName: "view-list-details"
                        text: i18n("Sections")
                        visible: root.sectionList.length > 0
                        onClicked: sectionMenu.popup(this, 0, height)
                        Menu {
                            id: sectionMenu
                            Instantiator {
                                model: root.sectionList
                                delegate: MenuItem {
                                    font.family: root.uiFont
                                    required property var modelData
                                    text: modelData.title
                                    onTriggered: root.jumpTo(modelData.start)
                                }
                                onObjectAdded: (index, object) => sectionMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => sectionMenu.removeItem(object)
                            }
                        }
                    }
                    // A script of the app is changed in the app; one written here, here.
                    ReaderButton {
                        id: editToggle
                        objectName: "editToggle"
                        quiet: true
                        iconName: root.pane === "edit" ? "dialog-ok" : "document-edit"
                        text: root.fromLibrary ? i18n("Edit in Kalipso")
                            : root.pane === "edit" ? i18n("Done editing") : i18n("Edit script")
                        onClicked: {
                            if (!root.fromLibrary) {
                                root.showPane("edit")
                                return
                            }
                            root.openMessage = ""
                            audio.openInApp(root.config.scriptId, function(message) { root.openMessage = message })
                        }
                    }
                    // Everything else, as in the desktop's own apps: colors from a window, and Configure.
                    ReaderButton {
                        objectName: "moreActions"
                        quiet: true
                        iconName: "overflow-menu"
                        Accessible.name: i18n("More")
                        onClicked: moreMenu.popup(this, 0, height)
                        Menu {
                            id: moreMenu
                            MenuItem {
                                objectName: "chooseBackgroundWindow"
                                font.family: root.uiFont
                                text: i18n("Follow a window's colors…")
                                enabled: !windowColors.active && !windowColors.busy
                                onTriggered: { root.followWindow = true; windowColors.start() }
                            }
                            MenuItem {
                                objectName: "stopWindowSharing"
                                font.family: root.uiFont
                                text: i18n("Stop following the window")
                                enabled: windowColors.active || windowColors.busy
                                onTriggered: { windowColors.stop(); root.followWindow = false }
                            }
                            MenuSeparator {}
                            MenuItem {
                                objectName: "configure"
                                font.family: root.uiFont
                                text: i18n("Configure Kalipso…")
                                icon.name: "configure"
                                onTriggered: Plasmoid.internalAction("configure").trigger()
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "readingProgress"
                    Layout.fillWidth: true
                    implicitHeight: 3
                    color: root.c.overlayLine
                    Accessible.role: Accessible.ProgressBar
                    Accessible.name: i18n("Reading progress")
                    Rectangle {
                        width: parent.width * (root.wordCount ? root.readCount / root.wordCount : 0)
                        height: parent.height
                        color: root.c.reading
                    }
                }
                Label {
                    objectName: "reader"
                    text: root.markup
                    textFormat: Text.RichText
                    color: root.c.overlayText
                    font.pointSize: root.readerSize
                    font.family: root.readerFont || root.uiFont
                    lineHeightMode: Text.ProportionalHeight
                    lineHeight: root.lineSpacing / 100
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.minimumHeight: font.pixelSize * 4.5
                    topPadding: 12
                    bottomPadding: 12
                    // Seconds left before steady scrolling starts, over the text.
                    Label {
                        font.family: root.uiFont
                        objectName: "countdown"
                        visible: root.countdownLeft > 0
                        anchors.centerIn: parent
                        text: root.countdownLeft
                        color: root.c.overlayText
                        font.pointSize: root.readerSize * 2
                        font.bold: true
                        padding: 16
                        background: Rectangle { color: root.c.overlaySurface; border.color: root.c.overlayLine; radius: 12 }
                        Accessible.ignored: true
                    }
                    Accessible.name: i18n("Reading excerpt")
                    Accessible.description: i18n("Open Edit script to read or change the complete script.")
                    // Beam-splitter rigs show the reader through a mirror.
                    transform: Scale {
                        origin.x: width / 2
                        origin.y: height / 2
                        xScale: root.mirror === "horizontal" ? -1 : 1
                        yScale: root.mirror === "vertical" ? -1 : 1
                    }
                    MouseArea { anchors.fill: parent; onClicked: body.forceActiveFocus() }
                }

                Flow {
                    spacing: 8
                    Layout.fillWidth: true
                    ReaderButton {
                        objectName: "readAction"
                        primary: true
                        // As in the app: the microphone and Start when following the voice, Pause while reading.
                        iconName: root.mode === "manual" ? "go-next"
                            : (steady.running || countdown.running || audio.active) ? "media-playback-pause"
                            : root.mode === "voice" ? "audio-input-microphone" : "media-playback-start"
                        text: root.mode === "manual" ? i18n("Next words")
                            : root.mode === "steady" ? ((steady.running || countdown.running) ? i18n("Pause") : i18n("Start"))
                            : audio.active ? i18n("Pause") : audio.busy ? i18n("Cancel") : i18n("Start")
                        enabled: root.wordCount > 0 && (root.mode !== "manual" || !root.atEnd)
                        onClicked: root.toggle()
                    }
                    ReaderButton { quiet: true; iconName: "go-previous"; text: i18n("Previous"); enabled: root.readCount > 0; onClicked: root.seek(root.tracker.position - 4) }
                    ReaderButton {
                        objectName: "resetPosition"
                        quiet: true
                        iconName: "go-first"
                        text: i18n("Reset")
                        enabled: root.readCount > 0
                        onClicked: root.seek(-1)
                    }
                    // How the reader advances, in reach while reading, as in the app's bottom bar.
                    ReaderButton {
                        objectName: "readingMode"
                        quiet: true
                        iconName: root.mode === "voice" ? "audio-input-microphone" : root.mode === "steady" ? "chronometer" : "go-next"
                        text: root.modeNames[root.mode] || root.modeNames.manual
                        Accessible.name: i18n("Reading mode: %1", text)
                        onClicked: modeMenu.popup(this, 0, height)
                        Menu {
                            id: modeMenu
                            Repeater {
                                model: ["manual", "steady", "voice"]
                                MenuItem {
                                    required property string modelData
                                    objectName: modelData + "Mode"
                                    font.family: root.uiFont
                                    checkable: true
                                    autoExclusive: true
                                    checked: root.mode === modelData
                                    text: root.modeNames[modelData]
                                    onTriggered: root.setMode(modelData)
                                }
                            }
                        }
                    }
                    Label {
                        font.family: root.uiFont
                        text: Number(root.readCount).toLocaleString(Qt.locale(), "f", 0) + " / " + Number(root.wordCount).toLocaleString(Qt.locale(), "f", 0)
                        color: root.c.overlayMuted
                        height: root.n.controlHeight
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8
                    }
                }
                Flow {
                    visible: root.mode === "steady"
                    spacing: 8
                    Layout.fillWidth: true
                    ReaderButton { quiet: true; text: "−"; Accessible.name: i18n("Slower"); onClicked: root.changeSpeed(-10) }
                    Label { font.family: root.uiFont; text: i18n("%1 words/min", root.speed); color: root.c.overlayText; height: root.n.controlHeight; verticalAlignment: Text.AlignVCenter }
                    ReaderButton { quiet: true; text: "+"; Accessible.name: i18n("Faster"); onClicked: root.changeSpeed(10) }
                }
                Label {
                    font.family: root.uiFont
                    objectName: "readingState"
                    color: root.c.reading
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.StatusBar
                    text: root.mode === "manual" ? (root.atEnd ? i18n("End of script · Microphone off") : i18n("Manual · Microphone off"))
                        : root.mode === "steady" ? (countdown.running ? i18np("Starting in %1 second…", "Starting in %1 seconds…", root.countdownLeft)
                            : steady.running ? i18n("Scrolling · Microphone off")
                            : root.atEnd ? i18n("End of script · Microphone off") : i18n("Paused · Microphone off"))
                        : audio.active ? (root.lost ? i18n("Off script · Kalipso catches up when you return to the text")
                            : i18n("Listening · On this device"))
                        : audio.busy ? i18n("Starting local speech…")
                        : audio.failed ? i18n("Speech unavailable · Configure Kalipso → Voice") : i18n("The microphone turns on when you start")
                }
                Note {
                    objectName: "windowColorsStatus"
                    visible: (root.followWindow || windowColors.active || windowColors.busy) && text !== ""
                    text: windowColors.status
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Note {
                    objectName: "openMessage"
                    visible: text !== ""
                    text: root.openMessage
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Rectangle { visible: root.pane !== ""; Layout.fillWidth: true; implicitHeight: 1; color: root.c.overlayLine }

                ColumnLayout {
                    visible: root.pane === "edit" && !root.fromLibrary
                    Layout.fillWidth: true
                    spacing: root.n.gap
                    Label { font.family: root.uiFont; text: i18n("Script"); font.capitalization: Font.AllUppercase; color: root.c.overlayMuted }
                    ScrollView {
                        ScrollBar.vertical: ReaderScrollBar {}
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.n.editorHeight
                        TextArea {
                            font.family: root.uiFont
                            id: editor
                            objectName: "scriptEditor"
                            text: root.tracker.text
                            color: Design.tokens.palette.light.text
                            font.pointSize: Qt.application.font.pointSize * 1.15
                            selectionColor: Design.tokens.palette.light.selection
                            selectedTextColor: Design.tokens.palette.light.selectionText
                            wrapMode: TextEdit.Wrap
                            padding: root.n.gap
                            selectByMouse: true
                            background: Rectangle {
                                color: Design.tokens.palette.light.surface
                                radius: root.n.buttonRadius
                                border.width: 2
                                border.color: editor.activeFocus ? Design.tokens.palette.light.focus : Design.tokens.palette.light.border
                            }
                            Accessible.name: i18n("Script")
                            onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { root.revealRequested(editor) })
                            Connections {
                                target: root
                                function onPaneChanged() { if (root.pane === "edit") Qt.callLater(function() { editor.forceActiveFocus() }) }
                            }
                            onTextChanged: {
                                if (text === root.tracker.text || root.fromLibrary)
                                    return
                                root.tracker.setText(text)
                                audio.setScript(text)
                                root.sectionList = Tracking.sections(text)
                                saveScript.restart()
                                root.refresh()
                            }
                        }
                    }
                    Note { text: i18n("Saved automatically. [Bracketed cues] are shown but not read aloud. Lines starting with # are sections.") }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
