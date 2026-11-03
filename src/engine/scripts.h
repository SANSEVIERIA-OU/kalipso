// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "library.h"
#include "tracker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

// The standalone app's scripts, for the Plasma widget and the GNOME extension. The companion only
// reads them: the scripts are written in the app, and the frontends read them, so one library
// serves the three. The one thing it writes is which script the app opens next (open()).
namespace AppScripts {
// Where the app keeps them (its data folder: organization "brianidavide", application "kalipso").
inline QString directory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/brianidavide/kalipso/scripts";
}

// The script the app has open, from the app's settings.
inline QString current() {
    return QSettings(QStringLiteral("brianidavide"), QStringLiteral("kalipso")).value("library/current").toString();
}

// Newest first, without empty scripts (nothing to read). `available` is false while the app has
// never saved a script on this computer.
inline QJsonObject list(int limit = 200) {
    if (!QDir(directory()).exists())
        return {{"available", false}, {"current", QString()}, {"scripts", QJsonArray()}};
    const ScriptLibrary library(directory());
    QJsonArray scripts;
    for (const auto &entry : library.entries()) {
        if (scripts.size() >= limit)
            break;
        ScriptTracker counter;
        counter.setScript(library.load(entry.id));
        if (counter.count() == 0)
            continue;
        scripts.append(QJsonObject{{"id", entry.id},
                                   {"title", entry.title},
                                   {"modified", entry.modified.toUTC().toString(Qt::ISODate)},
                                   {"words", counter.count()},
                                   {"speed", entry.speed}});
    }
    return {{"available", true}, {"current", current()}, {"scripts", scripts}};
}

// One script with its text, or an empty object when the library has no script with this id (ids
// are checked against the library, so a name can never reach outside its folder).
inline QJsonObject get(const QString &id) {
    if (!QDir(directory()).exists())
        return {};
    const ScriptLibrary library(directory());
    if (!library.contains(id))
        return {};
    for (const auto &entry : library.entries())
        if (entry.id == id)
            return {{"id", id},
                    {"title", entry.title},
                    {"modified", entry.modified.toUTC().toString(Qt::ISODate)},
                    {"text", library.load(id)},
                    {"position", entry.position},
                    {"speed", entry.speed}};
    return {};
}

// Opens the app on a script of its library: the script becomes the app's current one, then the app
// starts. The companion and the app are installed side by side; KALIPSO_APP names another program
// (tests). False when the app cannot be started.
inline bool open(const QString &id) {
    QSettings app(QStringLiteral("brianidavide"), QStringLiteral("kalipso"));
    app.setValue("library/current", id);
    app.sync();
    auto program = qEnvironmentVariable("KALIPSO_APP");
    if (program.isEmpty()) {
        const auto beside = QCoreApplication::applicationDirPath() + "/kalipso";
        program = QFileInfo(beside).isExecutable() ? beside : QStandardPaths::findExecutable("kalipso");
    }
    return !program.isEmpty() && QProcess::startDetached(program, {});
}
} // namespace AppScripts
