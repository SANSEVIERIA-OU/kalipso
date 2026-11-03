// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_bridge.h"
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSettings>
#ifdef KALIPSO_PORTAL_COLORS
#include <QDBusConnection>
#endif

namespace {
// One name per person and data folder: two people on one computer each have their own Kalipso.
QString serverName(const QString &key) {
    return QStringLiteral("kalipso-") +
           QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}
constexpr int kAnswerMs = 1500;
} // namespace

AppBridge::AppBridge(QObject *parent) : QObject(parent) {}

AppBridge::~AppBridge() {
#ifdef KALIPSO_PORTAL_COLORS
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.objectRegisteredAt(QString::fromLatin1(kPath)) == this) {
        bus.unregisterObject(QString::fromLatin1(kPath));
        bus.unregisterService(QString::fromLatin1(kService));
    }
#endif
}

bool AppBridge::registerOnBus() {
#ifdef KALIPSO_PORTAL_COLORS
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerObject(QString::fromLatin1(kPath), this, QDBusConnection::ExportScriptableSlots))
        return false;
    if (!bus.registerService(QString::fromLatin1(kService))) {
        bus.unregisterObject(QString::fromLatin1(kPath));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool AppBridge::listen(const QString &key) {
    const auto name = serverName(key);
    // Only the Kalipso that holds the lock listens: a socket left by one that crashed is stale.
    QLocalServer::removeServer(name);
    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_->listen(name))
        return false;
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (!socket->canReadLine())
                    return;
                const auto line = QString::fromUtf8(socket->readLine()).trimmed();
                if (!line.startsWith(QLatin1String("activate")))
                    return;
                socket->write("ok\n");
                socket->flush();
                emit activationRequested(line.mid(int(qstrlen("activate"))).trimmed());
            });
        }
    });
    return true;
}

bool AppBridge::activateRunning(const QString &key) {
    QLocalSocket socket;
    socket.connectToServer(serverName(key));
    if (!socket.waitForConnected(kAnswerMs))
        return false;
    // The token the launcher gave this start lets the running window take the focus on Wayland
    // (xdg-activation) and on X11 (startup notification).
    auto token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (token.isEmpty())
        token = qEnvironmentVariable("DESKTOP_STARTUP_ID");
    socket.write(QByteArray("activate ") + token.toUtf8() + '\n');
    if (!socket.waitForBytesWritten(kAnswerMs))
        return false;
    while (!socket.canReadLine())
        if (!socket.waitForReadyRead(kAnswerMs))
            return false;
    return socket.readLine().trimmed() == "ok";
}

void AppBridge::SetMinimized(bool minimized) { emit minimizedChanged(minimized); }

namespace {
// "x,y" in whole pixels, or nothing.
QString place(const QString &text) {
    const auto parts = text.split(u',');
    bool okX = false, okY = false;
    if (parts.size() != 2)
        return {};
    const int x = qRound(parts[0].trimmed().toDouble(&okX)), y = qRound(parts[1].trimmed().toDouble(&okY));
    return okX && okY ? QStringLiteral("%1,%2").arg(x).arg(y) : QString();
}
} // namespace

void AppBridge::SetReaderPlace(const QString &text) {
    if (const auto value = place(text); !value.isEmpty())
        QSettings().setValue("reader/place", value);
}

QString AppBridge::ReaderPlace() const { return place(QSettings().value("reader/place").toString()); }
