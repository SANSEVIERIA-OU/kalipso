// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QObject>

class QLocalServer;

// Kalipso's channel to the desktop and to itself.
// - A second start asks the running Kalipso to come forward instead of opening a second editor
//   (a local socket, on every platform).
// - Kalipso's KWin script reports what Qt is not told on Wayland: that the window was minimized,
//   and where the person put the reader under the camera, which Kalipso keeps for the next start
//   (D-Bus, on Linux).
class AppBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.sansevieria_ou.Kalipso.App")
public:
    explicit AppBridge(QObject *parent = nullptr);
    ~AppBridge() override;
    static constexpr const char *kService = "io.github.sansevieria_ou.Kalipso.App";
    static constexpr const char *kPath = "/App";

    // Offers the D-Bus object to Kalipso's KWin script. False without a session bus.
    bool registerOnBus();
    // Waits for a second Kalipso. The key names this person's Kalipso (its data folder).
    bool listen(const QString &key);
    // In a second Kalipso: asks the first one to come forward, passing on the activation token the
    // desktop gave this start (Wayland, X11). True when the first one answered.
    static bool activateRunning(const QString &key);

public slots:
    // From the KWin script: the window was minimized (true) or brought back.
    Q_SCRIPTABLE void SetMinimized(bool minimized);
    // From the KWin script: "x,y" where the person put the reader under the camera.
    Q_SCRIPTABLE void SetReaderPlace(const QString &place);
    // To the KWin script: "x,y", or empty when the reader was never moved.
    Q_SCRIPTABLE QString ReaderPlace() const;

signals:
    void minimizedChanged(bool minimized);
    // A second start asked for the window; token: the desktop's activation token, if any.
    void activationRequested(const QString &token);

private:
    QLocalServer *server_ = nullptr;
};
