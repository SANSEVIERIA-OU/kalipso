// SPDX-License-Identifier: AGPL-3.0-or-later
// Configure Kalipso → Appearance: the reader's colors, typeface, size and spacing.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: page

    property string cfg_appearance
    property string cfg_appearanceDefault
    property alias cfg_readerSize: size.value
    property int cfg_readerSizeDefault
    property string cfg_readerFont
    property string cfg_readerFontDefault
    property int cfg_lineSpacing
    property int cfg_lineSpacingDefault

    Kirigami.FormLayout {
        QQC2.ButtonGroup { id: colors }
        Repeater {
            model: [["auto", i18n("Desktop theme")], ["dark", i18n("Dark")], ["light", i18n("Light")]]
            QQC2.RadioButton {
                required property var modelData
                required property int index
                objectName: modelData[0] + "Appearance"
                Kirigami.FormData.label: index === 0 ? i18n("Reader colors:") : ""
                text: modelData[1]
                QQC2.ButtonGroup.group: colors
                checked: page.cfg_appearance === modelData[0]
                onToggled: if (checked) page.cfg_appearance = modelData[0]
            }
        }
        QQC2.Label {
            text: i18n("To take the colors of a window behind the reader, choose Follow a window's colors… in the reader's ⋯ menu.")
            opacity: 0.7
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.SpinBox {
            id: size
            objectName: "readerSize"
            Kirigami.FormData.label: i18n("Text size:")
            from: 18
            to: 64
            stepSize: 2
        }
        QQC2.ComboBox {
            objectName: "readerFont"
            Kirigami.FormData.label: i18n("Font:")
            textRole: "text"
            valueRole: "value"
            // A generic serif, plus fonts designed for easier reading when installed.
            model: {
                const installed = Qt.fontFamilies()
                return [{text: i18n("Interface font"), value: ""}, {text: i18n("Serif"), value: "Serif"}]
                    .concat(["Atkinson Hyperlegible", "Atkinson Hyperlegible Next", "OpenDyslexic", "Lexend"]
                        .filter(family => installed.indexOf(family) >= 0)
                        .map(family => ({text: family, value: family})))
            }
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(page.cfg_readerFont))
            onActivated: page.cfg_readerFont = currentValue
        }
        QQC2.ComboBox {
            objectName: "lineSpacing"
            Kirigami.FormData.label: i18n("Line spacing:")
            textRole: "text"
            valueRole: "value"
            model: [{text: i18n("Normal"), value: 100}, {text: i18n("Relaxed"), value: 125}, {text: i18n("Wide"), value: 150}]
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(page.cfg_lineSpacing))
            onActivated: page.cfg_lineSpacing = currentValue
        }
    }
}
