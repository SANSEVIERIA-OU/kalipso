// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include "engine/config.h"
#include "speech_platform.h"
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOperatingSystemVersion>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVector>

// Speech runtimes and models installed by tools/install-local-engine.py under
// <data>/kalipso/{runtimes,models}, so users do not have to type library paths.
struct InstalledSpeech {
    QString backend, library, model, language;
    QString label() const {
        const auto engine =
            backend == QLatin1String("moonshine") ? QStringLiteral("Moonshine") : QStringLiteral("Vosk");
        const auto name = QDir(model).dirName();
        return language.isEmpty() ? engine + " · " + name : engine + " · " + language + " · " + name;
    }
};

inline QString guessModelLanguage(const QString &backend, const QString &model) {
    QFile source(model + "/kalipso-source.json");
    if (source.open(QIODevice::ReadOnly)) {
        const auto language = QJsonDocument::fromJson(source.readAll()).object().value("language").toString();
        if (!language.isEmpty())
            return language;
    }
    static const QRegularExpression vosk("^vosk-model-(?:small-)?([a-z]{2})(?:-|$)");
    static const QRegularExpression moonshine("streaming-([a-z]{2})(?:-|_|$)");
    const auto match = (backend == QLatin1String("vosk") ? vosk : moonshine).match(QDir(model).dirName());
    if (!match.hasMatch())
        return {};
    return match.captured(1) == QLatin1String("cn") ? QStringLiteral("zh") : match.captured(1);
}

inline QString installedRuntime(const QString &root, const QString &backend) {
    QDir runtimes(root + "/runtimes");
    // Newest version first: directory names are "<backend>-<version>".
    auto names = runtimes.entryList({backend + "-*"}, QDir::Dirs, QDir::Name | QDir::Reversed);
#if defined(Q_OS_WIN)
    const QStringList libraryNames = backend == QLatin1String("moonshine")
                                         ? QStringList{"moonshine.dll", "libmoonshine.dll"}
                                         : QStringList{"libvosk.dll"};
#elif defined(Q_OS_MACOS)
    const QStringList libraryNames = backend == QLatin1String("moonshine")
                                         ? QStringList{"libmoonshine.dylib"}
                                         : QStringList{"libvosk.dylib", "libvosk.dyld"};
#else
    const QStringList libraryNames{backend == QLatin1String("moonshine") ? "libmoonshine.so" : "libvosk.so"};
#endif
    for (const auto &name : names) {
        QFile source(runtimes.filePath(name) + "/source.json");
        if (source.open(QIODevice::ReadOnly)) {
            const auto metadata = QJsonDocument::fromJson(source.readAll()).object();
            const auto target = metadata.value("target").toString();
            if (!target.isEmpty() && target != speechPlatform())
                continue;
#ifdef Q_OS_MACOS
            if (metadata.contains("minimumMacOS")) {
                const auto minimum = metadata.value("minimumMacOS").toString().split('.');
                if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS,
                                                                                 minimum.value(0).toInt(),
                                                                                 minimum.value(1).toInt()))
                    continue;
            }
#endif
            bool complete = true;
            for (const auto &file : metadata.value("nativeFiles").toArray())
                complete &= QFileInfo(runtimes.filePath(name) + "/" + file.toString()).isFile();
            if (!complete)
                continue;
        }
        QDirIterator it(runtimes.filePath(name), libraryNames, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext())
            return it.next();
    }
    return {};
}

inline QVector<InstalledSpeech> installedSpeech() {
    QVector<InstalledSpeech> found;
    QStringList seen;
    for (const auto &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        const auto root = base + "/kalipso";
        for (const QString backend : {"moonshine", "vosk"}) {
            const auto library = installedRuntime(root, backend);
            if (library.isEmpty())
                continue;
            QDir models(root + "/models/" + backend);
            for (const auto &name : models.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                const auto model = models.filePath(name);
                if (seen.contains(model) || !SpeechConfig::modelComplete(backend, model))
                    continue;
                seen.append(model);
                found.push_back({backend, library, model, guessModelLanguage(backend, model)});
            }
        }
    }
    return found;
}
