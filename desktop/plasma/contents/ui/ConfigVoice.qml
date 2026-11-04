// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Configure Kalipso → Voice: the language you speak. Choosing one downloads its model once, through
// the Kalipso companion; listening then runs on this computer.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: page

    LocalEngine {
        id: audio
        Component.onCompleted: { audio.settings(); audio.refreshLanguages() }
    }
    // The language in the list: the one in use until another is picked, else the desktop's.
    property string picked: ""
    readonly property var pickedLanguage: {
        const desktop = Qt.locale().name.split("_")[0]
        for (const code of [picked, audio.language, desktop])
            for (const item of audio.languages)
                if (code && item.code === code)
                    return item
        return audio.languages.length ? audio.languages[0] : null
    }

    Kirigami.FormLayout {
        RowLayout {
            Kirigami.FormData.label: i18n("Language you speak:")
            spacing: Kirigami.Units.smallSpacing
            QQC2.ComboBox {
                id: languages
                objectName: "languageChoices"
                enabled: !audio.active && !audio.busy && !audio.installing && audio.languages.length > 0
                model: audio.languages
                textRole: "code"
                // Wide enough for the longest choice ("English · download 62 MB"), never cut.
                Layout.minimumWidth: Math.max(implicitWidth, widest.advanceWidth + Kirigami.Units.gridUnit * 3)
                TextMetrics {
                    id: widest
                    font: languages.font
                    text: audio.languages.reduce((longest, item) => {
                        const text = languages.label(item)
                        return text.length > longest.length ? text : longest
                    }, "")
                }
                displayText: page.pickedLanguage ? label(page.pickedLanguage) : ""
                function label(item) {
                    return item.ready ? audio.languageName(item)
                        : i18n("%1 · download %2 MB", audio.languageName(item), audio.megabytes(item.bytes))
                }
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    width: languages.width
                    text: languages.label(modelData)
                    highlighted: languages.highlightedIndex === index
                    required property int index
                }
                onActivated: function(index) { page.picked = audio.languages[index].code }
            }
            QQC2.Button {
                objectName: "useLanguage"
                readonly property var item: page.pickedLanguage
                visible: item !== null && !audio.installing
                enabled: item !== null && !audio.active && !audio.busy
                    && !(item.ready && item.code === audio.language && audio.ready)
                text: item && !item.ready ? i18n("Download and use") : i18n("Use this language")
                icon.name: item && !item.ready ? "download" : "dialog-ok-apply"
                onClicked: audio.chooseLanguage(item.code)
            }
        }
        QQC2.ProgressBar {
            objectName: "languageProgress"
            visible: audio.installing
            from: 0
            to: Math.max(1, audio.install.total || 1)
            value: audio.install.received || 0
            indeterminate: audio.install.stage === "unpacking"
            Layout.fillWidth: true
            Accessible.name: i18n("Download progress")
        }
        QQC2.Label {
            objectName: "speechDetails"
            text: audio.languageStatus !== "" ? audio.languageStatus : audio.status
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        QQC2.Button {
            objectName: "languageAction"
            visible: audio.installing || audio.languageRetry
            text: audio.installing ? i18n("Cancel download") : i18n("Try again")
            icon.name: audio.installing ? "dialog-cancel" : "view-refresh"
            onClicked: audio.installing ? audio.cancelInstall() : audio.chooseLanguage(audio.install.language)
        }
        QQC2.Label {
            text: audio.downloads
                ? i18n("Choosing a language downloads its speech model once, from the model's publisher, and checks it against a fingerprint. Recognition then runs on this computer: nothing you say is sent.")
                : i18n("Kalipso downloads speech models on Linux for x86_64 computers.")
            opacity: 0.7
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }
        Kirigami.UrlButton {
            Kirigami.FormData.label: i18n("Help:")
            text: i18n("Local speech setup")
            url: "https://github.com/SANSEVIERIA-OU/kalipso/blob/main/docs/local-engine.md"
        }
    }
}
