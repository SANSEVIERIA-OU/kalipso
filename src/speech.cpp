// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include "session_stats.h"
#include "spoken_words.h"
#include "system_speech/grammar.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef KALIPSO_QT_AUDIO
#include <QCoreApplication>
#include <QPermissions>
#endif

namespace {
constexpr int kSampleRate = MicrophoneCapture::kSampleRate;
constexpr int kChunkBytes = 6400;       // 200 ms of 16-bit mono PCM.
constexpr int kMaxBacklogBytes = 64000; // 2 s; beyond this the model cannot keep up.
} // namespace

QStringList speechVocabulary(const QString &script, const QString &language) {
    auto phrases = SpokenWords::recognizerPhrases(script, language);
    if (phrases.isEmpty())
        return {};
    for (const auto &filler : SessionStats::fillerWords())
        phrases << filler;
    phrases.removeDuplicates();
    return phrases;
}

SpeechWorker::~SpeechWorker() { unload(); }

void SpeechWorker::unload() {
    moonshine_.unload();
    if (recognizer_ && recognizerFree_)
        recognizerFree_(recognizer_);
    recognizer_ = nullptr;
    if (model_ && modelFree_)
        modelFree_(model_);
    model_ = nullptr;
    vosk_.unload();
    backend_.clear();
    applied_.clear();
    betweenUtterances_ = true;
}

QByteArray SpeechWorker::grammar() const {
    // Each word in a spelling the model knows: the first of its alternatives ("ventitré|ventitre"),
    // else, for "dell'anno", its parts. Words the model does not know are left out.
    const auto known = [this](const QString &word) { return findWord_(model_, word.toUtf8().constData()) >= 0; };
    QJsonArray phrases;
    for (const auto &phrase : vocabulary_) {
        QStringList words;
        for (const auto &word : phrase.split(u' ', Qt::SkipEmptyParts)) {
            const auto spellings = word.split(u'|');
            if (const auto found = std::find_if(spellings.begin(), spellings.end(), known); found != spellings.end()) {
                words << *found;
                continue;
            }
            if (const int cut = word.indexOf(u'\''); cut > 0) {
                const auto elided = word.left(cut + 1), bare = word.left(cut), rest = word.mid(cut + 1);
                if (known(elided))
                    words << elided;
                else if (known(bare))
                    words << bare;
                if (known(rest))
                    words << rest;
            }
        }
        if (!words.isEmpty())
            phrases.append(words.join(u' '));
    }
    if (phrases.isEmpty())
        return "[]";
    phrases.append(QStringLiteral("[unk]")); // Weighting it more (tried up to 100 times) changes nothing.
    return QJsonDocument(phrases).toJson(QJsonDocument::Compact);
}

void SpeechWorker::setVocabulary(const QStringList &words) {
    vocabulary_ = words;
    if (recognizer_ && betweenUtterances_)
        applyVocabulary();
}

void SpeechWorker::applyVocabulary() {
    if (!recognizer_ || !setGrammar_ || !findWord_ || vocabulary_ == applied_)
        return;
    const auto words = grammar();
    setGrammar_(recognizer_, words.constData());
    applied_ = vocabulary_;
    emit guided(words != "[]", generation_);
}

void SpeechWorker::load(const SpeechRequest &request, quint64 generation) {
    if (!current(generation))
        return;
    unload();
    generation_ = generation;
    backend_ = request.backend;
    if (request.backend == QLatin1String("moonshine")) {
        QString message;
        if (!moonshine_.load(request.library, request.model, request.language, request.architecture, message)) {
            emit error(generation, message);
            return;
        }
        emit guided(false, generation);
    } else if (request.backend == QLatin1String("vosk")) {
        if (!loadVosk(request, generation))
            return;
    } else {
        emit error(generation, tr("Unsupported speech engine."));
        return;
    }
    if (!current(generation)) {
        unload();
        return;
    }
    emit ready(generation);
}

bool SpeechWorker::loadVosk(const SpeechRequest &request, quint64 generation) {
    vosk_.setFileName(request.library);
    if (!vosk_.load()) {
        emit error(generation, tr("Cannot load libvosk. Select a valid Vosk library in Settings."));
        return false;
    }
    const auto resolve = [this](auto &member, const char *name) {
        member = reinterpret_cast<std::remove_reference_t<decltype(member)>>(vosk_.resolve(name));
        return member != nullptr;
    };
    const bool complete =
        resolve(modelNew_, "vosk_model_new") & resolve(modelFree_, "vosk_model_free") &
        resolve(recognizerNew_, "vosk_recognizer_new") & resolve(recognizerFree_, "vosk_recognizer_free") &
        resolve(accept_, "vosk_recognizer_accept_waveform") & resolve(result_, "vosk_recognizer_result") &
        resolve(partial_, "vosk_recognizer_partial_result");
    if (!complete) {
        unload();
        emit error(generation, tr("This library does not provide the required Vosk API."));
        return false;
    }
    if (auto setLogLevel = reinterpret_cast<void (*)(int)>(vosk_.resolve("vosk_set_log_level")))
        setLogLevel(-1);
    // Without these, Vosk hears any word of the language, as before 0.3.42.
    if (!(resolve(findWord_, "vosk_model_find_word") & resolve(recognizerNewGrammar_, "vosk_recognizer_new_grm") &
          resolve(setGrammar_, "vosk_recognizer_set_grm")))
        findWord_ = nullptr;
    model_ = modelNew_(request.model.toUtf8().constData());
    if (!current(generation)) {
        unload();
        return false;
    }
    if (!model_) {
        unload();
        emit error(generation, tr("Cannot open this speech model. Choose the extracted model folder."));
        return false;
    }
    // Models without runtime graphs (the large ones) ignore a grammar and hear any word.
    const auto words = findWord_ ? grammar() : QByteArray("[]");
    recognizer_ = words == "[]" ? recognizerNew_(model_, float(kSampleRate))
                                : recognizerNewGrammar_(model_, float(kSampleRate), words.constData());
    applied_ = vocabulary_;
    betweenUtterances_ = true;
    if (!recognizer_) {
        unload();
        emit error(generation, tr("Not enough resources to start speech recognition."));
        return false;
    }
    emit guided(words != "[]", generation);
    return true;
}

void SpeechWorker::consume(const QByteArray &pcm, quint64 generation, quint64 sequence) {
    if (sequence < skipBefore_->load()) {
        emit consumed(int(pcm.size()), generation, true);
        return;
    }
    const bool live = generation == generation_ && current(generation);
    if (live && backend_ == QLatin1String("moonshine")) {
        QList<MoonshineRecognizer::Event> events;
        QString message;
        if (!moonshine_.consume(pcm, events, message))
            emit error(generation, message);
        else if (current(generation))
            for (const auto &event : events)
                emit transcript(event.text, event.final, generation);
    } else if (live && recognizer_) {
        const int result = accept_(recognizer_, pcm.constData(), int(pcm.size()));
        if (result < 0) {
            emit error(generation, tr("The speech recognizer could not process the audio."));
        } else {
            const auto json = result ? result_(recognizer_) : partial_(recognizer_);
            const auto data = QJsonDocument::fromJson(json).object();
            betweenUtterances_ = result != 0;
            emit transcript(data.value(result ? "text" : "partial").toString(), result != 0, generation);
            if (betweenUtterances_)
                applyVocabulary(); // The script changed while this utterance was heard.
        }
    }
    emit consumed(int(pcm.size()), generation, false);
}

SpeechService::SpeechService(QObject *parent) : QObject(parent), worker_(new SpeechWorker(epoch_, skipBefore_)) {
    connect(&system_, &SystemSpeechClient::ready, this, [this] {
        if (active_ && systemMode_)
            captureReady();
    });
    connect(&system_, &SystemSpeechClient::transcript, this, [this](const QString &text, bool final) {
        if (active_ && systemMode_)
            emit transcript(text, final);
    });
    connect(&system_, &SystemSpeechClient::failed, this, [this](const QString &message) {
        if (systemMode_)
            fail(message);
    });
    connect(&system_, &SystemSpeechClient::message, this, &SpeechService::status);
    connect(&capture_, &MicrophoneCapture::started, this, &SpeechService::captureReady);
    connect(&capture_, &MicrophoneCapture::audio, this, &SpeechService::queueAudio);
    connect(&capture_, &MicrophoneCapture::failed, this, [this](const QString &message) {
        if (active_)
            fail(message);
    });
    connect(&capture_, &MicrophoneCapture::silent, this, [this](bool silent) {
        if (!active_ || systemMode_)
            return;
        emit status(silent ? tr("No sound from %1. If you are speaking, check that it is not muted in the system "
                                "sound settings.")
                                 .arg(capture_.name())
                           : listeningText());
        emit this->silent(silent);
    });

    qputenv("MOONSHINE_ORT_SINGLE_THREAD", "1"); // Must be set before the worker loads ONNX Runtime.
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &SpeechWorker::ready, this, [this](quint64 generation) {
        if (active_ && generation == generation_)
            startCapture();
    });
    connect(worker_, &SpeechWorker::error, this, [this](quint64 generation, const QString &message) {
        if (active_ && generation == generation_)
            fail(message);
    });
    connect(worker_, &SpeechWorker::transcript, this, [this](const QString &text, bool final, quint64 generation) {
        if (active_ && generation == generation_)
            emit transcript(text, final);
    });
    connect(worker_, &SpeechWorker::guided, this, [this](bool learnt, quint64 generation) {
        if (active_ && generation == generation_)
            setGuided(learnt);
    });
    connect(worker_, &SpeechWorker::consumed, this, [this](int bytes, quint64 generation, bool skipped) {
        if (generation == generation_ && !skipped) // Skipped audio left the count when it was skipped.
            queued_ = std::max(0, queued_ - bytes);
    });
    thread_.start(QThread::LowPriority);
}

SpeechService::~SpeechService() {
    stop();
    // Drain pending recognizer work before its shared library is unloaded.
    QMetaObject::invokeMethod(worker_, &SpeechWorker::unload, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void SpeechService::start(const SpeechRequest &request) {
    catchUp_.reset();
    stop();
    if (request.backend == QLatin1String("system")) {
        if (!SystemSpeechClient::platformSupported()) {
            emit status(tr("System speech is available only on macOS and Windows."));
            return;
        }
        systemMode_ = true;
        active_ = true;
#ifdef Q_OS_WIN
        setGuided(true); // Windows recognizes only the script's phrases near the reading position.
#endif
        emit status(tr("Starting on-device system speech…"));
        system_.start(request.language, scriptPhrases(script_, scriptPosition_));
        return;
    }
    if (request.backend != QLatin1String("moonshine") && request.backend != QLatin1String("vosk")) {
        emit status(tr("Unsupported speech engine."));
        return;
    }
#ifndef KALIPSO_QT_AUDIO
    // Before the model loads: no microphone, or not the one chosen, is said at once.
    if (const auto problem = MicrophoneCapture::problem(request.microphone); !problem.isEmpty()) {
        emit status(problem);
        return;
    }
#endif
    if (!QFileInfo(request.model).isDir()) {
        emit status(tr("Choose an extracted speech model folder in Settings."));
        return;
    }
#ifdef Q_OS_MACOS
    const QMicrophonePermission permission;
    const auto access = qApp->checkPermission(permission);
    if (access == Qt::PermissionStatus::Denied) {
        emit status(tr("Enable microphone access for Kalipso in System Settings."));
        return;
    }
    if (access == Qt::PermissionStatus::Undetermined) {
        active_ = true;
        const auto requestGeneration = generation_;
        emit status(tr("Waiting for microphone permission…"));
        qApp->requestPermission(permission, this, [=, this](const QPermission &result) {
            if (!active_ || generation_ != requestGeneration)
                return;
            if (result.status() == Qt::PermissionStatus::Granted) {
                active_ = false; // Not a user-visible stop: continue starting.
                start(request);
            } else {
                fail(tr("Microphone access was denied."));
            }
        });
        return;
    }
#endif
    active_ = true;
    microphone_ = request.microphone.trimmed();
    if (language_ != request.language) {
        language_ = request.language; // Numbers are spelt in the language spoken.
        updateVocabulary();
    }
    const auto generation = ++generation_;
    epoch_->store(generation);
    emit status(tr("Loading the local speech model…"));
    QMetaObject::invokeMethod(worker_, [worker = worker_, request, generation] { worker->load(request, generation); });
}

void SpeechService::setScript(const QString &script, int position) {
    if (script_ == script && scriptPosition_ == position)
        return;
    const bool changed = script_ != script;
    script_ = script;
    scriptPosition_ = position;
    if (changed)
        updateVocabulary();
#ifdef Q_OS_WIN
    if (active_ && systemMode_)
        system_.updateScript(scriptPhrases(script_, scriptPosition_));
#endif
}

// The script goes to the recognizer, which learns its sentences (Vosk). The
// vocabulary depends on the script and the language, not on the reading position.
void SpeechService::updateVocabulary() {
    const auto words = speechVocabulary(script_, language_);
    QMetaObject::invokeMethod(worker_, [worker = worker_, words] { worker->setVocabulary(words); });
}

void SpeechService::captureReady() {
    if (!active_)
        return;
    emit listening();
    emit status(listeningText());
}

QString SpeechService::listeningText() const {
    return systemMode_ ? tr("Listening · microphone · on this device")
                       : tr("Listening · %1 · on this device").arg(capture_.name());
}

void SpeechService::fail(const QString &message) {
    stop();
    emit status(message);
}

void SpeechService::stop() {
    system_.stop();
    systemMode_ = false;
    capture_.stop();
    const bool wasActive = active_;
    active_ = false;
    epoch_->store(++generation_);
    queued_ = 0;
    buffer_.clear();
    QMetaObject::invokeMethod(worker_, &SpeechWorker::unload);
    setGuided(false);
    if (wasActive)
        emit stopped();
}

void SpeechService::setGuided(bool guided) {
    if (guided_ == guided)
        return;
    guided_ = guided;
    emit guidedChanged(guided);
}

void SpeechService::queueAudio(const QByteArray &pcm) {
    if (!active_ || systemMode_)
        return;
    buffer_.append(pcm);
    if (buffer_.size() + queued_ > kMaxBacklogBytes) {
        if (catchUp_.overloaded(QDateTime::currentMSecsSinceEpoch()) == CatchUpPolicy::Stop) {
            fail(tr("Recognition cannot keep up on this computer. Listening stopped; try a smaller model."));
            return;
        }
        // Skip what is waiting and follow from the next words; the reading position stays.
        skipBefore_->store(sequence_);
        queued_ = 0;
        buffer_.clear();
        emit status(tr("Catching up: a moment of audio was skipped. Following continues."));
        return;
    }
    while (buffer_.size() >= kChunkBytes) {
        const auto data = buffer_.first(kChunkBytes);
        buffer_.remove(0, kChunkBytes);
        queued_ += kChunkBytes;
        QMetaObject::invokeMethod(worker_, [worker = worker_, data, generation = generation_, sequence = sequence_++] {
            worker->consume(data, generation, sequence);
        });
    }
}

void SpeechService::startCapture() { capture_.start(microphone_); }
