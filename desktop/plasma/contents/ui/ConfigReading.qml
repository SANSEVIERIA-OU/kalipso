// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Configure Kalipso → Reading: how the reader advances, and how it looks on a beam-splitter rig.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: page

    // Bound to the widget's configuration (contents/config/main.xml) by Plasma's dialog.
    property string cfg_mode
    property string cfg_modeDefault
    property alias cfg_speed: speed.value
    property int cfg_speedDefault
    property int cfg_countdown
    property int cfg_countdownDefault
    property string cfg_mirror
    property string cfg_mirrorDefault

    Kirigami.FormLayout {
        QQC2.ButtonGroup { id: modes }
        Repeater {
            model: [["manual", i18n("Manual"), i18n("You step with the keys, a pedal or a clicker.")],
                    ["steady", i18n("Steady speed"), i18n("The words advance at the speed below.")],
                    ["voice", i18n("Follow my voice"), i18n("Local speech recognition follows what you say.")]]
            ColumnLayout {
                required property var modelData
                required property int index
                Kirigami.FormData.label: index === 0 ? i18n("Reading mode:") : ""
                spacing: 0
                QQC2.RadioButton {
                    objectName: modelData[0] + "Mode"
                    text: modelData[1]
                    QQC2.ButtonGroup.group: modes
                    checked: page.cfg_mode === modelData[0]
                    onToggled: if (checked) page.cfg_mode = modelData[0]
                }
                QQC2.Label {
                    text: modelData[2]
                    leftPadding: Kirigami.Units.gridUnit * 1.5
                    opacity: 0.7
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 22
                }
            }
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.SpinBox {
            id: speed
            objectName: "steadySpeed"
            Kirigami.FormData.label: i18n("Steady speed:")
            from: 40
            to: 400
            stepSize: 10
            editable: true
            textFromValue: function(value) { return i18n("%1 words/min", value) }
            valueFromText: function(text) { return parseInt(text) || 150 }
        }
        QQC2.ComboBox {
            id: countdown
            objectName: "countdown"
            Kirigami.FormData.label: i18n("Countdown before steady scrolling:")
            textRole: "text"
            valueRole: "value"
            model: [{text: i18n("Off"), value: 0},
                    {text: i18np("%1 second", "%1 seconds", 3), value: 3},
                    {text: i18np("%1 second", "%1 seconds", 5), value: 5}]
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(page.cfg_countdown))
            onActivated: page.cfg_countdown = currentValue
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.ComboBox {
            objectName: "mirror"
            Kirigami.FormData.label: i18n("Mirror:")
            textRole: "text"
            valueRole: "value"
            model: [{text: i18n("Off"), value: "none"},
                    {text: i18n("Horizontal"), value: "horizontal"},
                    {text: i18n("Vertical"), value: "vertical"}]
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(page.cfg_mirror))
            onActivated: page.cfg_mirror = currentValue
        }
        QQC2.Label {
            text: i18n("For beam-splitter rigs, which show the reader through a mirror.")
            opacity: 0.7
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.Label {
            text: i18n("Keys: arrows or Page Up/Down step, Space starts or pauses, + and − change the steady speed. Screen sharing may include this widget; check what other participants see.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }
    }
}
