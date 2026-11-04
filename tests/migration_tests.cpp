// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Kalipso moved from the organization "brianidavide" to SANSEVIERIA-OU: the first start under the new
// name brings the settings and the scripts along, and never overwrites what is already there.
#include "settings_migration.h"
#include <QTest>

class MigrationTests : public QObject {
    Q_OBJECT
    QString current() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
    QString earlier() const {
        QDir base(QFileInfo(current()).absolutePath());
        base.cdUp();
        return base.filePath("brianidavide/kalipso");
    }
    void clean() {
        QDir(current()).removeRecursively();
        QDir(earlier()).removeRecursively();
        QSettings().clear();
        QSettings("brianidavide", "kalipso").clear();
        QSettings().sync();
        QSettings("brianidavide", "kalipso").sync();
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() { clean(); }
    void cleanupTestCase() { clean(); }

    void settingsAndScriptsComeAlong() {
        QSettings("brianidavide", "kalipso").setValue("library/current", "a1");
        QDir().mkpath(earlier() + "/scripts");
        QFile script(earlier() + "/scripts/a1.txt");
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("Buongiorno a tutti.");
        script.close();
        importLegacySettings("brianidavide", "kalipso");
        QCOMPARE(importLegacyData("brianidavide"), current());
        QCOMPARE(QSettings().value("library/current").toString(), QString("a1"));
        QFile moved(current() + "/scripts/a1.txt");
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("Buongiorno a tutti."));
        QVERIFY(!QFileInfo::exists(earlier())); // Moved, not left behind twice.
    }

    void whatIsAlreadyThereIsKept() {
        QSettings().setValue("library/current", "new");
        QDir().mkpath(current() + "/scripts");
        QFile mine(current() + "/scripts/new.txt");
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write("Already here");
        mine.close();
        QSettings("brianidavide", "kalipso").setValue("library/current", "old");
        QDir().mkpath(earlier() + "/scripts");
        importLegacySettings("brianidavide", "kalipso");
        importLegacyData("brianidavide");
        QCOMPARE(QSettings().value("library/current").toString(), QString("new"));
        QVERIFY(QFileInfo::exists(current() + "/scripts/new.txt"));
        QVERIFY(QFileInfo::exists(earlier())); // Left alone: nothing is deleted.
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("kalipso");
    app.setOrganizationName("sansevieria-ou");
    MigrationTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "migration_tests.moc"
