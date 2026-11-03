// SPDX-License-Identifier: AGPL-3.0-or-later
// Kalipso's GNOME Shell extension as GNOME Shell reports it, and the Settings section that turns it on.
// A stand-in GNOME Shell on a private session bus (run under dbus-run-session) answers as GNOME Shell 51
// does (checked against a real one: GetExtensionInfo gives "state" and "enabled", EnableExtension
// returns true, ExtensionStateChanged follows).
#include "platform/gnome_extension.h"
#include "window.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QPushButton>
#include <QLabel>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class FakeShell : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.Shell.Extensions")
    Q_PROPERTY(bool UserExtensionsEnabled READ userExtensionsEnabled)
public:
    bool installed = true;
    bool userExtensions = true;
    QVariantMap info{{"uuid", GnomeExtension::uuid}, {"state", 2.0}, {"enabled", false}};
    bool userExtensionsEnabled() const { return userExtensions; }

public slots:
    QVariantMap GetExtensionInfo(const QString &uuid) {
        return installed && uuid == QLatin1String(GnomeExtension::uuid) ? info : QVariantMap();
    }
    bool EnableExtension(const QString &uuid) {
        if (uuid != QLatin1String(GnomeExtension::uuid) || !installed)
            return false;
        info["enabled"] = true;
        info["state"] = 1.0;
        emit ExtensionStateChanged(uuid, info);
        return true;
    }

signals:
    void ExtensionStateChanged(const QString &uuid, const QVariantMap &state);
};

class GnomeExtensionTests : public QObject {
    Q_OBJECT
    FakeShell shell_;
    QTemporaryDir scripts_;
    bool real_ = false; // Against the real GNOME Shell: only realGnomeShellTurnsItOn runs.

    static bool registerShell(FakeShell &shell) {
        auto bus = QDBusConnection::sessionBus();
        return bus.registerObject("/org/gnome/Shell", &shell,
                                  QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals |
                                      QDBusConnection::ExportAllProperties) &&
               bus.registerService("org.gnome.Shell");
    }
    static void unregisterShell() {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterService("org.gnome.Shell");
        bus.unregisterObject("/org/gnome/Shell");
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        real_ = qEnvironmentVariable("KALIPSO_REAL_GNOME_SHELL") == "1";
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("Needs a session bus (run under dbus-run-session)");
    }

    // After every test, failed ones included: the next one starts without a GNOME Shell.
    void init() {
        if (real_ && QString(QTest::currentTestFunction()) != QLatin1String("realGnomeShellTurnsItOn"))
            QSKIP("Runs against the stand-in GNOME Shell only");
    }
    void cleanup() {
        if (!real_)
            unregisterShell();
    }

    void unavailableWithoutGnomeShell() {
        unregisterShell();
        QCOMPARE(GnomeExtension().state(), GnomeExtension::State::Unavailable);
    }

    void stateFollowsGnomeShell() {
        QVERIFY(registerShell(shell_));
        GnomeExtension extension;
        using State = GnomeExtension::State;
        shell_.installed = false;
        QCOMPARE(extension.state(), State::NotInstalled);
        shell_.installed = true;
        shell_.info = {{"state", 2.0}, {"enabled", false}};
        QCOMPARE(extension.state(), State::Off);
        shell_.userExtensions = false;
        QCOMPARE(extension.state(), State::ExtensionsOff);
        shell_.userExtensions = true;
        shell_.info = {{"state", 4.0}, {"enabled", true}};
        QCOMPARE(extension.state(), State::OutOfDate);
        shell_.info = {{"state", 3.0}, {"enabled", true}};
        QCOMPARE(extension.state(), State::Error);
        shell_.info = {{"state", 1.0}, {"enabled", true}};
        QCOMPARE(extension.state(), State::On);
        // An older GNOME Shell without "enabled": running means on.
        shell_.info = {{"state", 1.0}};
        QCOMPARE(extension.state(), State::On);
        shell_.info = {{"state", 2.0}};
        QCOMPARE(extension.state(), State::Off);
    }

    void turningOnIsHeardBack() {
        QVERIFY(registerShell(shell_));
        shell_.info = {{"state", 2.0}, {"enabled", false}};
        GnomeExtension extension;
        QSignalSpy changed(&extension, &GnomeExtension::changed);
        QVERIFY(extension.turnOn());
        QCOMPARE(extension.state(), GnomeExtension::State::On);
        QTRY_VERIFY(changed.count() >= 1);
    }

    // Settings offers "Turn on" only while the extension is off, and the section shows the new state.
    void settingsTurnTheExtensionOn() {
        QVERIFY(registerShell(shell_));
        shell_.info = {{"state", 2.0}, {"enabled", false}};
        MainWindow window(scripts_.path());
        auto *status = window.findChild<QLabel *>("gnomeExtensionStatus");
        auto *turnOn = window.findChild<QPushButton *>("gnomeExtensionTurnOn");
        QVERIFY(status && turnOn);
        QVERIFY(status->text().startsWith("Off."));
        QVERIFY(!turnOn->isHidden());
        turnOn->click();
        QTRY_VERIFY(status->text().startsWith("On:"));
        QVERIFY(turnOn->isHidden());
    }

    // Opt-in (KALIPSO_REAL_GNOME_SHELL=1, inside a GNOME session where the extension is installed and
    // off): the real GNOME Shell turns it on when Settings asks.
    void realGnomeShellTurnsItOn() {
        if (qEnvironmentVariable("KALIPSO_REAL_GNOME_SHELL") != "1")
            QSKIP("Set KALIPSO_REAL_GNOME_SHELL=1 inside a GNOME session");
        MainWindow window(scripts_.path());
        auto *status = window.findChild<QLabel *>("gnomeExtensionStatus");
        auto *turnOn = window.findChild<QPushButton *>("gnomeExtensionTurnOn");
        QVERIFY2(status && turnOn, "no GNOME top bar section: is GNOME Shell running and the extension installed?");
        QVERIFY2(status->text().startsWith("Off."), qPrintable(status->text()));
        turnOn->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("On:"), 10000);
        QCOMPARE(GnomeExtension().state(), GnomeExtension::State::On);
    }

    void noSectionOutsideGnome() {
        unregisterShell();
        MainWindow window(scripts_.path());
        QVERIFY(!window.findChild<QLabel *>("gnomeExtensionStatus"));
    }
};

QTEST_MAIN(GnomeExtensionTests)
#include "gnome_extension_tests.moc"
