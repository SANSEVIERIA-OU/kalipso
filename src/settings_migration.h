// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

// Kalipso's settings and scripts under an earlier name. The organization was "brianidavide" until
// Kalipso moved to SANSEVIERIA-OU (November 2026); earlier still the app was "kde-teleprompter".

// Copies the settings of an earlier name, once: only while the current settings are empty.
inline void importLegacySettings(const QString &organization, const QString &application) {
    QSettings current;
    if (!current.allKeys().isEmpty())
        return;
    const QSettings legacy(organization, application);
    for (const auto &key : legacy.allKeys())
        current.setValue(key, legacy.value(key));
    current.sync();
}

// The data folder (scripts, reading positions) of an earlier organization name, next to the current
// one: <data>/<organization>/<application>. It is moved when the current folder does not exist yet, or
// copied when it cannot be moved (an older Kalipso still open). Returns the folder now in use.
inline QString importLegacyData(const QString &organization) {
    const QString current = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir base(QFileInfo(current).absolutePath()); // <data>/<current organization>
    base.cdUp();                                  // <data>
    const QString legacy = base.filePath(organization + "/" + QCoreApplication::applicationName());
    if (QFileInfo::exists(current) || !QFileInfo(legacy).isDir() || QFileInfo(legacy) == QFileInfo(current))
        return current;
    QDir().mkpath(QFileInfo(current).absolutePath());
    if (QDir().rename(legacy, current)) {
        QDir().rmdir(QFileInfo(legacy).absolutePath()); // The earlier organization's folder, if now empty.
        return current;
    }
    for (QDirIterator it(legacy, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();) {
        const QString from = it.next();
        if (from.endsWith(QLatin1String(".lock")))
            continue; // The older Kalipso's own lock.
        const QString to = current + "/" + QDir(legacy).relativeFilePath(from);
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::copy(from, to);
    }
    return current;
}
