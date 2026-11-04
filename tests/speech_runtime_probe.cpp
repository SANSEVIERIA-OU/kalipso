// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Opt-in network test: the production downloader, native recognizer and tracker, with synthetic
// speech. Runs on each native CI host; no microphone and no system recognizer is required.
#include "speech.h"
#include "speech_download.h"
#include "tracker.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4) {
        qWarning("Usage: speech_runtime_probe vosk|moonshine it|en FIXTURE_DIRECTORY");
        return 2;
    }
    QFile textFile(args[3] + "/" + args[2] + ".txt"), pcmFile(args[3] + "/" + args[2] + ".pcm");
    if (!textFile.open(QIODevice::ReadOnly) || !pcmFile.open(QIODevice::ReadOnly))
        return 2;
    const auto script = QString::fromUtf8(textFile.readAll());
    const auto pcm = pcmFile.readAll();
    QTemporaryDir root(QDir::tempPath() + "/Kalipso voice test-XXXXXX");
    if (!root.isValid())
        return 2;
    SpeechDownload download;
    download.setRoot(root.path());
    InstalledSpeech installed;
    QString failure;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        failure = "Download timed out";
        loop.quit();
    });
    QObject::connect(&download, &SpeechDownload::failed, &loop, [&](SpeechDownload::Error, const QString &message) {
        failure = message;
        loop.quit();
    });
    QObject::connect(&download, &SpeechDownload::finished, &loop, [&](const InstalledSpeech &speech) {
        installed = speech;
        loop.quit();
    });
    const auto install = [&] {
        deadline.start(300000);
        download.start(args[1], args[2]);
        loop.exec();
        deadline.stop();
        return failure.isEmpty() && !installed.library.isEmpty();
    };
    if (!install()) {
        qWarning() << failure;
        return 1;
    }
    // A restart must discover the native library and must not need another download.
    if (installedRuntime(root.path(), args[1]) != installed.library ||
        !SpeechCatalog::plan(args[1], args[2], root.path()).parts.isEmpty() || !install()) {
        qWarning("Installed runtime cannot be reused");
        return 1;
    }
    qputenv("MOONSHINE_ORT_SINGLE_THREAD", "1");
    auto epoch = std::make_shared<std::atomic<quint64>>(1);
    SpeechWorker worker(epoch);
    ScriptTracker tracker;
    tracker.setScript(script);
    bool ready = false;
    QStringList finals;
    QObject::connect(&worker, &SpeechWorker::error, &app, [&](quint64, const QString &message) { failure = message; });
    QObject::connect(&worker, &SpeechWorker::ready, &app, [&](quint64) { ready = true; });
    QObject::connect(&worker, &SpeechWorker::guided, &app, [&](bool guided, quint64) { tracker.setGuided(guided); });
    QObject::connect(&worker, &SpeechWorker::transcript, &app, [&](const QString &text, bool final, quint64) {
        tracker.follow(text, final);
        if (final && !text.isEmpty())
            finals << text;
    });
    worker.setVocabulary(speechVocabulary(script, args[2]));
    SpeechRequest request;
    request.backend = args[1];
    request.language = args[2];
    request.library = installed.library;
    request.model = installed.model;
    worker.load(request, 1);
    if (ready && failure.isEmpty()) {
        for (qsizetype offset = 0; offset < pcm.size() && failure.isEmpty(); offset += 6400)
            worker.consume(pcm.mid(offset, 6400), 1);
        for (int i = 0; i < 20 && failure.isEmpty(); ++i)
            worker.consume(QByteArray(6400, 0), 1);
    }
    worker.unload();
    const bool passed = ready && failure.isEmpty() && !finals.isEmpty() && tracker.count() > 10 &&
                        tracker.position() >= tracker.count() * 0.8;
    const QJsonObject report{{"platform", speechPlatform()},
                             {"backend", args[1]},
                             {"language", args[2]},
                             {"library", installed.library},
                             {"ready", ready},
                             {"error", failure},
                             {"transcript", finals.join(" | ")},
                             {"position", tracker.position()},
                             {"words", tracker.count()},
                             {"passed", passed}};
    const auto json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return passed ? 0 : 1;
}
