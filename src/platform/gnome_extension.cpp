// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "gnome_extension.h"
#ifdef KALIPSO_PORTAL_COLORS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#endif

namespace {
constexpr auto service = "org.gnome.Shell";
constexpr auto path = "/org/gnome/Shell";
constexpr auto interface = "org.gnome.Shell.Extensions";
// GNOME Shell's ExtensionState (js/misc/extensionUtils.js): 1 running, 2 off, 3 error, 4 out of date.
constexpr int running = 1, error = 3, outOfDate = 4;
} // namespace

GnomeExtension::GnomeExtension(QObject *parent) : QObject(parent) {
#ifdef KALIPSO_PORTAL_COLORS
    QDBusConnection::sessionBus().connect(service, path, interface, "ExtensionStateChanged", this,
                                          SLOT(stateChanged(QString, QVariantMap)));
#endif
}

GnomeExtension::State GnomeExtension::state() const {
#ifdef KALIPSO_PORTAL_COLORS
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface() || !bus.interface()->isServiceRegistered(service))
        return State::Unavailable;
    QDBusInterface shell(service, path, interface, bus);
    if (!shell.isValid())
        return State::Unavailable;
    const QDBusReply<QVariantMap> reply = shell.call("GetExtensionInfo", QString::fromLatin1(uuid));
    if (!reply.isValid())
        return State::Unavailable;
    const auto info = reply.value();
    if (info.isEmpty())
        return State::NotInstalled;
    const int value = qRound(info.value("state").toDouble());
    if (value == outOfDate)
        return State::OutOfDate;
    if (value == error)
        return State::Error;
    // The main switch in the Extensions app stops every extension, this one included.
    if (!shell.property("UserExtensionsEnabled").toBool())
        return State::ExtensionsOff;
    // Newer GNOME Shells say whether the person turned it on; older ones only whether it runs.
    const bool on = info.contains("enabled") ? info.value("enabled").toBool() : value == running;
    return on ? State::On : State::Off;
#else
    return State::Unavailable;
#endif
}

bool GnomeExtension::turnOn() {
#ifdef KALIPSO_PORTAL_COLORS
    QDBusInterface shell(service, path, interface, QDBusConnection::sessionBus());
    if (!shell.isValid())
        return false;
    const QDBusReply<bool> reply = shell.call("EnableExtension", QString::fromLatin1(uuid));
    emit changed();
    return reply.isValid() && reply.value();
#else
    return false;
#endif
}

void GnomeExtension::stateChanged(const QString &extension, const QVariantMap &) {
    if (extension == QLatin1String(uuid))
        emit changed();
}
