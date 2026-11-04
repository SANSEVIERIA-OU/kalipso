// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QObject>
#include <QVariantMap>

// Kalipso's GNOME Shell extension, as GNOME Shell sees it (org.gnome.Shell.Extensions on the session
// bus). Kalipso's Linux packages install the extension for everyone, but GNOME keeps a newly installed
// extension off until the person turns it on. Settings offers that here, so nobody needs the
// Extensions app or a terminal for it. Outside GNOME, or without Qt D-Bus, the state is Unavailable.
class GnomeExtension : public QObject {
    Q_OBJECT
public:
    static constexpr auto uuid = "kalipso@sansevieria-ou.github.io";
    enum class State {
        Unavailable,   // Not a GNOME session (no GNOME Shell on the session bus).
        NotInstalled,  // GNOME, but the extension is not installed.
        Off,           // Installed and turned off.
        On,            // Installed and running.
        ExtensionsOff, // GNOME's extensions are all switched off (the Extensions app's main switch).
        OutOfDate,     // This GNOME Shell version is not one the extension declares.
        Error,         // GNOME Shell could not start it.
    };
    Q_ENUM(State)

    explicit GnomeExtension(QObject *parent = nullptr);
    State state() const;
    // Turns the extension on. False when GNOME Shell refused or is not there.
    bool turnOn();

signals:
    void changed();

private slots:
    void stateChanged(const QString &extension, const QVariantMap &info);
};
