// SPDX-License-Identifier: AGPL-3.0-or-later
// Voice following, end to end, as the app does it: recorded speech goes through the real speech
// worker (Vosk or Moonshine) and its transcripts through the real ScriptTracker, at recording time.
//
//   follow_probe LIBRARY MODEL SPEC.json [--backend vosk|moonshine] [--bias] [--gap-ms N]
//                [--record EVENTS.json | --replay EVENTS.json]
//
// SPEC.json: {"language": "it", "segments": [{"pcm": "a.pcm", "text": "…", "kind": "read"}, …]}
// kind "read": in the script and spoken; "skipped": in the script, not spoken (the speaker jumps
// over it); "aside": spoken, not in the script (the speaker improvises). PCM is 16-bit mono 16 kHz.
// The script is the "read" and "skipped" texts joined by blank lines.
//
// Prints one JSON report: for every read segment, how many words the reader was behind (or ahead)
// when the speaker finished it, plus false advances during asides, jumps ahead of the speaker,
// backward moves and the transcript word error rate against the segment texts.
//
// --record saves the recognizer's transcripts with the audio chunk each came after; --replay feeds
// them again instead of recognizing, so trackers can be compared on the same transcripts (Moonshine
// does not give the same transcripts twice).
#include "speech.h"
#include "tracker.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMultiMap>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
int editDistance(const QStringList &a, const QStringList &b) {
    QVector<int> previous(b.size() + 1), current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
        previous.swap(current);
    }
    return previous[b.size()];
}
QStringList plainWords(const QString &text) {
    QStringList words;
    for (const auto &word : ScriptTracker::tokenize(text))
        words << text.mid(word.start, word.length).toLower();
    return words;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments();
    const bool bias = args.removeAll("--bias") > 0;
    QString backend = "vosk", record, replay;
    int gapMs = 300;
    for (int i = 1; i + 1 < args.size();) {
        if (args[i] == "--record" || args[i] == "--replay") {
            (args[i] == "--record" ? record : replay) = args[i + 1];
            args.remove(i, 2);
        } else if (args[i] == "--backend") {
            backend = args[i + 1];
            args.remove(i, 2);
        } else if (args[i] == "--gap-ms") {
            gapMs = args[i + 1].toInt();
            args.remove(i, 2);
        } else {
            ++i;
        }
    }
    if (args.size() != 4) {
        std::fprintf(stderr, "Usage: follow_probe LIBRARY MODEL SPEC.json [--backend vosk|moonshine] [--bias] "
                             "[--gap-ms N] [--record EVENTS.json | --replay EVENTS.json]\n");
        return 2;
    }
    QFile specFile(args[3]);
    if (!specFile.open(QIODevice::ReadOnly))
        return 2;
    const auto spec = QJsonDocument::fromJson(specFile.readAll()).object();
    const auto segments = spec.value("segments").toArray();

    // The script, and where each read segment's words are in it.
    QString script;
    struct Part {
        QString kind, text;
        QByteArray audio;
        int first = -1, last = -1; // Word indexes in the script (read and skipped).
    };
    QList<Part> parts;
    for (const auto &value : segments) {
        const auto object = value.toObject();
        Part part{object.value("kind").toString("read"), object.value("text").toString(), {}};
        if (part.kind != "skipped") {
            QFile pcm(object.value("pcm").toString());
            if (!pcm.open(QIODevice::ReadOnly))
                return 2;
            part.audio = pcm.readAll();
        }
        if (part.kind != "aside") {
            if (!script.isEmpty())
                script += "\n\n";
            const int before = int(ScriptTracker::tokenize(script).size());
            script += part.text;
            part.first = before;
            part.last = int(ScriptTracker::tokenize(script).size()) - 1;
        }
        parts.append(part);
    }

    qputenv("MOONSHINE_ORT_SINGLE_THREAD", "1");
    auto epoch = std::make_shared<std::atomic<quint64>>(1);
    SpeechWorker worker(epoch);
    bool failed = false;
    QObject::connect(&worker, &SpeechWorker::error, &app, [&](quint64, const QString &error) {
        failed = true;
        std::fprintf(stderr, "%s\n", qPrintable(error));
    });
    SpeechRequest request;
    request.library = args[1];
    request.model = args[2];
    request.backend = backend;
    request.language = spec.value("language").toString("en");
    ScriptTracker tracker;
    tracker.setScript(script);
#ifndef FOLLOW_BASELINE
    if (bias)
        worker.setVocabulary(speechVocabulary(script, request.language));
#endif
    QMultiMap<int, QPair<QString, bool>> replayed; // Audio chunk -> transcripts after it.
    if (!replay.isEmpty()) {
        QFile events(replay);
        if (!events.open(QIODevice::ReadOnly))
            return 2;
        for (const auto &value : QJsonDocument::fromJson(events.readAll()).array()) {
            const auto event = value.toArray();
            replayed.insert(event[0].toInt(), {event[1].toString(), event[2].toBool()});
        }
    } else {
        worker.load(request, 1);
    }
    if (failed)
        return 1;

    // The app's handling of transcripts (MainWindow): follow, and after each utterance tell the
    // recognizer where reading is.
    double now = 0;
    int truth = -1;     // The last script word the speaker has finished, as far as segments tell.
    int ceiling = -1;   // The last script word of the segment being spoken.
    bool aside = false; // Speaking off the script.
    int backward = 0, ahead = 0, asideAdvance = 0;
    QStringList finals;
    QJsonArray moves, jumps;
    // When each word was said (estimated, see spoken()) and when the reader first reached it.
    QVector<double> said(tracker.count(), -1), reachedAt(tracker.count(), -1);
    double chunkEnd = 0;
    int chunk = 0;
    QJsonArray recorded;
    const auto follow = [&](const QString &text, bool final) {
        const int before = tracker.position();
        if (tracker.follow(text, final)) {
            const int after = tracker.position();
            for (int word = std::max(0, before + 1); word <= after; ++word)
                if (reachedAt[word] < 0)
                    reachedAt[word] = chunkEnd;
            if (after < before)
                ++backward;
            if (after > ceiling + 2)
                ++ahead;
            if (aside && after > before)
                asideAdvance += after - before;
            moves.append(QJsonArray{qRound(now * 100) / 100.0, after});
            if (std::abs(after - before) > 10)
                jumps.append(QJsonArray{qRound(now * 100) / 100.0, before, after, text, final});
        }
        if (final && !text.isEmpty())
            finals << text;
    };
    QObject::connect(&worker, &SpeechWorker::transcript, &app, [&](const QString &text, bool final, quint64) {
        recorded.append(QJsonArray{chunk, text, final});
        follow(text, final);
    });
    const auto feed = [&](const QByteArray &audio) {
        for (qsizetype offset = 0; offset < audio.size(); offset += 6400, ++chunk) {
            const auto pcm = audio.mid(offset, 6400);
            chunkEnd = now + pcm.size() / 32000.0;
            if (replay.isEmpty()) {
                worker.consume(pcm, 1);
            } else {
                auto events = replayed.values(chunk);
                std::reverse(events.begin(), events.end()); // QMultiMap returns the newest first.
                for (const auto &[text, final] : events)
                    follow(text, final);
            }
            now = chunkEnd;
        }
    };
    const QByteArray gap(qsizetype(gapMs) * 32, 0);
    // No word timings come with the recordings: within the stretch of a segment that has speech
    // (20 ms frames louder than a tenth of its loudest), each word is said in proportion to its
    // letters. Good to a word or two, and the same for every configuration compared.
    const auto spoken = [&](const QByteArray &audio, double start, int first, int last) {
        const auto *samples = reinterpret_cast<const qint16 *>(audio.constData());
        const int frames = int(audio.size() / 2 / 320);
        QVector<double> energy(frames);
        double loudest = 0;
        for (int f = 0; f < frames; ++f) {
            double sum = 0;
            for (int i = 0; i < 320; ++i)
                sum += double(samples[f * 320 + i]) * samples[f * 320 + i];
            energy[f] = std::sqrt(sum / 320);
            loudest = std::max(loudest, energy[f]);
        }
        int onset = 0, offset = frames - 1;
        while (onset < frames && energy[onset] < loudest / 10)
            ++onset;
        while (offset > onset && energy[offset] < loudest / 10)
            --offset;
        const auto &words = tracker.words();
        double letters = 0;
        for (int w = first; w <= last; ++w)
            letters += words[w].length + 1;
        double done = 0;
        for (int w = first; w <= last; ++w) {
            done += words[w].length + 1;
            said[w] = start + (onset + (offset + 1 - onset) * done / letters) * 0.02;
        }
    };
    QJsonArray results;
    QStringList referenceWords;
    int behindTotal = 0, reached = 0, read = 0;
    for (const auto &part : parts) {
        if (part.kind == "skipped")
            continue;
        aside = part.kind == "aside";
        if (!aside) {
            ceiling = part.last;
            spoken(part.audio, now, part.first, part.last);
        }
        feed(part.audio);
        feed(gap);
        if (aside)
            continue;
        truth = part.last;
        const int behind = truth - tracker.position();
        behindTotal += std::abs(behind);
        reached += std::abs(behind) <= 2;
        ++read;
        referenceWords << plainWords(part.text);
        results.append(QJsonObject{{"end", truth}, {"position", tracker.position()}, {"behind", behind}});
    }
    feed(QByteArray(64000, 0)); // Two seconds of silence: the last utterance ends.
    QStringList heard;
    for (const auto &text : finals)
        heard << plainWords(text);
    QVector<double> delays;
    int unreached = 0;
    for (int w = 0; w < tracker.count(); ++w) {
        if (said[w] < 0)
            continue;
        if (reachedAt[w] < 0)
            ++unreached;
        else
            delays.append(reachedAt[w] - said[w]);
    }
    std::sort(delays.begin(), delays.end());
    const auto quantile = [&](double q) {
        return delays.isEmpty() ? 0.0 : delays[std::min(int(delays.size()) - 1, int(q * delays.size()))];
    };
    const QJsonObject report{
        {"words", tracker.count()},
        {"delayMedian", quantile(0.5)},
        {"delayP90", quantile(0.9)},
        {"unreached", unreached},
        {"segments", results},
        {"reached", read ? double(reached) / read : 0.0},
        {"meanAbsBehind", read ? double(behindTotal) / read : 0.0},
        {"final", tracker.position()},
        {"backward", backward},
        {"aheadOfSpeaker", ahead},
        {"asideAdvance", asideAdvance},
        {"wer", referenceWords.isEmpty() ? 0.0 : double(editDistance(heard, referenceWords)) / referenceWords.size()},
        {"transcript", finals.join(" | ")},
        {"jumps", jumps},
        {"moves", moves}};
    if (!record.isEmpty()) {
        QFile events(record);
        if (!events.open(QIODevice::WriteOnly) || events.write(QJsonDocument(recorded).toJson()) < 0)
            return 2;
    }
    std::fputs(QJsonDocument(report).toJson(QJsonDocument::Indented).constData(), stdout);
    return failed ? 1 : 0;
}
