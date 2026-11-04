// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// The window under the camera with two monitors (the offscreen platform, configured with
// tests/fixtures/two-screens.json): when the reader's monitor goes away, it comes back onto the one
// that is left.
#include "window.h"
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class ScreensTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("sansevieria-ou");
        QCoreApplication::setApplicationName("kalipso-screens-tests");
        if (QGuiApplication::screens().size() < 2)
            QSKIP("Needs the offscreen platform with tests/fixtures/two-screens.json");
    }

    void theReaderLeavesAMonitorThatGoesAway() {
        QSettings().clear();
        QSettings().setValue("reading/screen", "Monitor");
        QTemporaryDir library;
        MainWindow window(library.path());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QScreen *laptop = nullptr, *monitor = nullptr;
        for (auto *screen : QGuiApplication::screens())
            (screen->name() == QLatin1String("Monitor") ? monitor : laptop) = screen;
        QVERIFY(laptop && monitor);
        window.placeUnderCamera();
        QTRY_VERIFY(monitor->geometry().contains(window.frameGeometry().center()));
        // Another monitor going away changes nothing.
        const auto before = window.geometry();
        emit qApp->screenRemoved(laptop);
        QTest::qWait(50);
        QCOMPARE(window.geometry(), before);
        // The reader's own: it comes onto the laptop, at the top, under the camera.
        emit qApp->screenRemoved(monitor);
        QTRY_VERIFY2(laptop->availableGeometry().contains(window.frameGeometry().center()),
                     qPrintable(QDebug::toString(window.frameGeometry())));
        QCOMPARE(window.windowHandle()->screen(), laptop);
        QVERIFY(window.underCamera());
        QVERIFY(window.y() <= laptop->availableGeometry().top() + 40);
        // Writing (not under the camera): the desktop keeps the window where it likes.
        window.expand();
        const auto writing = window.geometry();
        emit qApp->screenRemoved(monitor);
        QTest::qWait(50);
        QCOMPARE(window.geometry(), writing);
    }
};

QTEST_MAIN(ScreensTests)
#include "screens_tests.moc"
