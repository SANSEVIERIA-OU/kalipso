// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Real production recognizers with file input: no microphone, GUI or word alignment claim.
#include "speech.h"
#include "speech_download.h"
#include "tracker.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include "process_stats.h"

double percentile(QList<double> values, double fraction) {
    if (values.isEmpty())
        return -1;
    std::sort(values.begin(), values.end());
    return values[std::max<qsizetype>(0, qsizetype(std::ceil(fraction * values.size())) - 1)];
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 6 || !QStringList{"prepare", "batch", "paced"}.contains(args[5])) {
        qWarning("Usage: speech_benchmark BACKEND LANGUAGE FIXTURES RUNTIME_ROOT prepare|batch|paced");
        return 2;
    }
    const auto plan = SpeechCatalog::plan(args[1], args[2], args[4]);
    if (!plan.valid)
        return 2;
    if (args[5] == "prepare") {
        SpeechDownload download;
        download.setRoot(args[4]);
        QEventLoop loop;
        bool success = false;
        QObject::connect(&download, &SpeechDownload::finished, &loop, [&](const InstalledSpeech &) {
            success = true;
            loop.quit();
        });
        QObject::connect(&download, &SpeechDownload::failed, &loop, [&](SpeechDownload::Error, const QString &error) {
            qWarning() << error;
            loop.quit();
        });
        QTimer::singleShot(300000, &loop, &QEventLoop::quit);
        download.start(args[1], args[2]);
        loop.exec();
        return success ? 0 : 1;
    }
    if (!plan.parts.isEmpty()) {
        qWarning("Run prepare first: downloads are deliberately outside measurements");
        return 2;
    }
    QFile textFile(args[3] + "/" + args[2] + ".txt"), pcmFile(args[3] + "/" + args[2] + ".pcm");
    if (!textFile.open(QIODevice::ReadOnly) || !pcmFile.open(QIODevice::ReadOnly))
        return 2;
    const QString script = QString::fromUtf8(textFile.readAll());
    const QByteArray pcm = pcmFile.readAll();
    if (pcm.isEmpty() || pcm.size() % 2 || script.trimmed().isEmpty())
        return 2;
    qputenv("MOONSHINE_ORT_SINGLE_THREAD", "1");
    auto epoch = std::make_shared<std::atomic<quint64>>(1);
    SpeechWorker worker(epoch);
    ScriptTracker tracker;
    tracker.setScript(script);
    bool ready = false;
    QString failure;
    int transcripts = 0, finalTranscripts = 0, moves = 0;
    double firstTranscriptMs = -1, firstAdvanceMs = -1, lastAdvanceMs = -1;
    QList<double> decodeMs, trackerUs;
    QElapsedTimer load, stream;
    QObject::connect(&worker, &SpeechWorker::ready, &app, [&](quint64) { ready = true; });
    QObject::connect(&worker, &SpeechWorker::error, &app, [&](quint64, const QString &error) { failure = error; });
    QObject::connect(&worker, &SpeechWorker::guided, &app, [&](bool guided, quint64) { tracker.setGuided(guided); });
    QObject::connect(&worker, &SpeechWorker::transcript, &app, [&](const QString &text, bool final, quint64) {
        if (text.isEmpty())
            return;
        ++transcripts;
        finalTranscripts += final;
        if (firstTranscriptMs < 0)
            firstTranscriptMs = stream.nsecsElapsed() / 1e6;
        QElapsedTimer timer;
        timer.start();
        const bool moved = tracker.follow(text, final);
        trackerUs.append(timer.nsecsElapsed() / 1e3);
        if (moved) {
            ++moves;
            lastAdvanceMs = stream.nsecsElapsed() / 1e6;
            if (firstAdvanceMs < 0)
                firstAdvanceMs = lastAdvanceMs;
        }
    });
    worker.setVocabulary(speechVocabulary(script, args[2]));
    SpeechRequest request;
    request.backend = args[1];
    request.language = args[2];
    request.library = plan.library;
    request.model = plan.modelDir;
    const double cpuBeforeLoad = processCpuSeconds();
    load.start();
    worker.load(request, 1);
    const double loadMs = load.nsecsElapsed() / 1e6;
    const double cpuAfterLoad = processCpuSeconds();
    if (!ready || !failure.isEmpty()) {
        qWarning() << failure;
        return 1;
    }
    const bool paced = args[5] == "paced";
    // Four seconds allow both engines to finalize; include this silence in the RTF denominator.
    const QByteArray input = pcm + QByteArray(4 * 32000, 0);
    const auto start = std::chrono::steady_clock::now();
    stream.start();
    double decodeTotalMs = 0, maxBacklogMs = 0;
    for (qsizetype offset = 0; offset < input.size() && failure.isEmpty(); offset += 6400) {
        const auto chunk = input.mid(offset, 6400);
        const double endMs = (offset + chunk.size()) / 32.0;
        if (paced)
            std::this_thread::sleep_until(start + std::chrono::microseconds(qint64(endMs * 1000)));
        QElapsedTimer timer;
        timer.start();
        worker.consume(chunk, 1);
        const double elapsed = timer.nsecsElapsed() / 1e6;
        decodeMs.append(elapsed);
        decodeTotalMs += elapsed;
        if (paced)
            maxBacklogMs = std::max(maxBacklogMs, stream.nsecsElapsed() / 1e6 - endMs);
    }
    const double streamMs = stream.nsecsElapsed() / 1e6;
    const double cpuAfterStream = processCpuSeconds();
    const double peak = peakRssKiB();
    const bool passed = failure.isEmpty() && finalTranscripts > 0 && tracker.count() > 10 &&
                        tracker.position() >= tracker.count() * 0.8;
    QJsonObject report{{"schema", 1}, {"platform", speechPlatform()}, {"qt", qVersion()},
                       {"os", QSysInfo::prettyProductName()}, {"backend", args[1]}, {"language", args[2]},
                       {"runtime_version", plan.runtime.value("version")}, {"model", plan.modelName},
                       {"mode", args[5]}, {"audio_sha256", QString::fromLatin1(QCryptographicHash::hash(pcm, QCryptographicHash::Sha256).toHex())},
                       {"script_sha256", QString::fromLatin1(QCryptographicHash::hash(script.toUtf8(), QCryptographicHash::Sha256).toHex())},
                       {"audio_seconds", pcm.size() / 32000.0}, {"flush_seconds", 4},
                       {"model_load_ms", loadMs}, {"model_load_cpu_seconds", cpuAfterLoad - cpuBeforeLoad},
                       {"stream_wall_ms", streamMs}, {"stream_cpu_seconds", cpuAfterStream - cpuAfterLoad},
                       {"stream_cpu_percent_one_core", (cpuAfterStream - cpuAfterLoad) / (streamMs / 1000) * 100},
                       {"decode_rtf", decodeTotalMs / (input.size() / 32.0)},
                       {"chunk_p50_ms", percentile(decodeMs, .5)}, {"chunk_p95_ms", percentile(decodeMs, .95)},
                       {"chunk_max_ms", percentile(decodeMs, 1)}, {"max_backlog_ms", paced ? maxBacklogMs : -1},
                       {"tracker_p50_us", percentile(trackerUs, .5)}, {"tracker_p95_us", percentile(trackerUs, .95)},
                       {"first_transcript_ms", firstTranscriptMs}, {"first_advance_ms", firstAdvanceMs},
                       {"last_advance_ms", lastAdvanceMs}, {"peak_rss_kib", peak},
                       {"transcripts", transcripts}, {"final_transcripts", finalTranscripts}, {"moves", moves},
                       {"position", tracker.position()}, {"words", tracker.count()}, {"passed", passed}, {"error", failure}};
    worker.unload();
    const auto json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return passed && peak >= 0 && cpuBeforeLoad >= 0 ? 0 : 1;
}
