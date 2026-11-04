// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// The widget's settings, in Plasma's own configuration dialog (right-click → Configure Kalipso…).
import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18n("Reading")
        icon: "media-playback-start"
        source: "ConfigReading.qml"
    }
    ConfigCategory {
        name: i18n("Voice")
        icon: "audio-input-microphone"
        source: "ConfigVoice.qml"
    }
    ConfigCategory {
        name: i18n("Appearance")
        icon: "preferences-desktop-color"
        source: "ConfigAppearance.qml"
    }
}
