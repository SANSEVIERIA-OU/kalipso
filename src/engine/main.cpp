// SPDX-License-Identifier: AGPL-3.0-or-later
// Optional session D-Bus companion: local speech recognition, speech model downloads and
// window-color sampling for the Plasma widget and GNOME extension.
#include "config.h"
#include "scripts.h"
#include "screen_colors/portal.h"
#include "settings_migration.h"
#include "speech.h"
#include "speech_download.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTranslator>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr auto kService = "io.github.sansevieria_ou.Kalipso.Engine";
constexpr auto kObjectPath = "/io/github/sansevieria_ou/Kalipso/Engine";
constexpr qint64 kLeaseMs = 10000; // Clients poll; a silent client loses its session.
constexpr qint64 kIdleExitMs = 90000;
constexpr int kMaxEvents = 64;
constexpr int kMaxScriptCharacters = 1 << 20; // A script of a hundred thousand words or more.

int shutdownWriteFd = -1;
void requestShutdown(int) {
    const int saved = errno;
    const char byte = 1;
    if (shutdownWriteFd >= 0) {
        [[maybe_unused]] const auto written = ::write(shutdownWriteFd, &byte, 1);
    }
    errno = saved;
}

QString json(const QJsonObject &value) {
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}

// Makes an installed model the one the companion listens with.
bool useSpeech(const InstalledSpeech &speech) {
    QSettings config;
    config.setValue("speech/backend", speech.backend);
    const QString prefix = "profiles/" + speech.backend + "/";
    config.setValue(prefix + "library", speech.library);
    config.setValue(prefix + "model", speech.model);
    config.setValue(prefix + "language", speech.language);
    if (speech.backend == QLatin1String("moonshine"))
        config.setValue(prefix + "architecture", 2); // The catalog has Tiny streaming models.
    config.sync();
    return config.status() == QSettings::NoError;
}

// Every catalog language: whether it is ready, and with which engine (the one that follows the
// language best, unless only another one is installed).
QJsonArray languageChoices() {
    const auto root = SpeechCatalog::writableRoot();
    QJsonArray result;
    for (const auto &code : SpeechCatalog::languages()) {
        const auto installed = SpeechCatalog::installedFor(code);
        const bool ready = !installed.backend.isEmpty();
        const auto backend = ready ? installed.backend : SpeechCatalog::downloadBackend(code);
        result.append(QJsonObject{{"code", code},
                                  {"name", SpeechCatalog::languageName(code)},
                                  {"backend", backend},
                                  {"ready", ready},
                                  {"bytes", ready ? 0 : SpeechCatalog::plan(backend, code, root).bytes()}});
    }
    return result;
}
} // namespace

class Engine final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.sansevieria_ou.Kalipso.Engine")
public:
    Engine() {
        speechWatcher_.setConnection(QDBusConnection::sessionBus());
        speechWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        colorWatcher_.setConnection(QDBusConnection::sessionBus());
        colorWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(&speechWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
            if (name == owner_)
                stopSpeech();
        });
        connect(&colorWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
            if (name == colorOwner_)
                stopColors();
        });
        connect(&colors_, &PortalColors::colorChanged, this, [this](const QColor &color) { color_ = color.name(); });

        state_["backend"] = SpeechConfig::selected();
        state_["message"] = tr("Local engine ready");
        // Frontends give it to their tracker (Tracker.guided): a recognizer that learnt the script
        // takes more words heard right before the reader moves ahead.
        connect(&speech_, &SpeechService::guidedChanged, this, [this](bool guided) {
            state_["guided"] = guided;
            publish();
        });
        connect(&speech_, &SpeechService::listening, this, [this] {
            state_["status"] = "listening";
            publish();
        });
        connect(&speech_, &SpeechService::status, this, [this](const QString &message) {
            state_["message"] = message;
            if (!speech_.active()) {
                state_["status"] = "error";
                releaseOwner();
            }
            publish();
        });
        connect(&speech_, &SpeechService::transcript, this, [this](const QString &text, bool final) {
            state_["text"] = text;
            state_["final"] = final;
            state_["sequence"] = ++sequence_;
            events_.append(QJsonObject{{"sequence", sequence_}, {"text", text}, {"final", final}});
            while (events_.size() > kMaxEvents)
                events_.removeFirst();
            state_["events"] = events_;
            publish();
        });

        connect(&download_, &SpeechDownload::progress, this, [this](qint64 received, qint64 total) {
            install_["received"] = received;
            install_["total"] = total;
        });
        connect(&download_, &SpeechDownload::finished, this, [this](const InstalledSpeech &speech) {
            if (speech_.active() || !owner_.isEmpty() || !useSpeech(speech)) {
                finishInstall("failed", "busy");
                return;
            }
            install_["backend"] = speech.backend;
            selected(speech.backend);
            finishInstall("done");
        });
        connect(&download_, &SpeechDownload::failed, this, [this](SpeechDownload::Error error, const QString &) {
            finishInstall(error == SpeechDownload::Error::Cancelled ? "cancelled" : "failed",
                          SpeechDownload::errorName(error));
        });

        idle_.start();
        connect(&leaseTimer_, &QTimer::timeout, this, [this] {
            if (!owner_.isEmpty() && lease_.elapsed() > kLeaseMs)
                stopSpeech();
            if (!colorOwner_.isEmpty() && colorLease_.elapsed() > kLeaseMs)
                stopColors();
            if (download_.active())
                idle_.restart(); // A download finishes even if every frontend closed.
            if (owner_.isEmpty() && colorOwner_.isEmpty() && idle_.elapsed() > kIdleExitMs)
                QCoreApplication::quit();
        });
        leaseTimer_.start(1000);
    }

public slots:
    // --- Window colors (Plasma "Choose background window…") ---
    bool StartColors(const QString &clientToken) {
        if (!calledFromDBus())
            return false;
        if (clientToken.isEmpty() || clientToken.size() > 128) {
            sendErrorReply(QDBusError::InvalidArgs, tr("A widget identifier is required."));
            return false;
        }
        const auto caller = message().service();
        if (!colorOwner_.isEmpty() && (caller != colorOwner_ || clientToken != colorToken_)) {
            sendErrorReply(QDBusError::AccessDenied, tr("Another widget is sharing a background window."));
            return false;
        }
        colorOwner_ = caller;
        colorToken_ = clientToken;
        colorWatcher_.addWatchedService(caller);
        colorLease_.start();
        idle_.restart();
        color_.clear();
        colors_.start();
        return true;
    }

    QString GetColorState(const QString &clientToken) {
        if (!ownsColors(clientToken)) {
            sendErrorReply(QDBusError::AccessDenied, tr("Only the sharing widget can read its color state."));
            return {};
        }
        colorLease_.restart();
        idle_.restart();
        return json({{"active", colors_.active()}, {"message", colors_.status()}, {"color", color_}});
    }

    void StopColors(const QString &clientToken) {
        if (!ownsColors(clientToken)) {
            sendErrorReply(QDBusError::AccessDenied, tr("Only the sharing widget can stop its session."));
            return;
        }
        stopColors();
    }

    // --- Speech ---
    QString GetDependencies() {
        idle_.restart();
        return json(SpeechConfig::dependencies(SpeechConfig::selected()));
    }

    QString GetBackends() {
        idle_.restart();
        return json({{"selected", SpeechConfig::selected()}, {"backends", SpeechConfig::backends()}});
    }

    bool SelectBackend(const QString &backend) {
        if (!calledFromDBus())
            return false;
        if (speech_.active() || !owner_.isEmpty()) {
            sendErrorReply(QDBusError::AccessDenied, tr("Stop listening before changing the speech engine."));
            return false;
        }
        if (!SpeechConfig::supported(backend)) {
            sendErrorReply(QDBusError::InvalidArgs, tr("Choose Moonshine or Vosk."));
            return false;
        }
        QSettings config;
        config.setValue("speech/backend", backend);
        config.sync();
        if (config.status() != QSettings::NoError) {
            sendErrorReply(QDBusError::Failed, tr("Cannot save the speech engine selection."));
            return false;
        }
        resetTranscript();
        state_["backend"] = backend;
        state_["status"] = "ready";
        state_["message"] = tr("Speech engine selected. Start listening when its model is configured.");
        idle_.restart();
        publish();
        return true;
    }

    // --- Language models ---
    // The catalog languages with their state, and the download in progress (stage: idle,
    // downloading, unpacking, done, failed or cancelled; error: network, fingerprint, archive,
    // disk, unsupported or busy). Clients poll it while a download runs.
    QString GetLanguages() {
        idle_.restart();
        const auto backend = SpeechConfig::selected();
        auto install = install_;
        install["active"] = download_.active();
        if (download_.active())
            install["stage"] = download_.unpacking() ? "unpacking" : "downloading";
        return json({{"downloads", SpeechCatalog::downloadsSupported()},
                     {"backend", backend},
                     {"language", SpeechConfig::value(backend, "language").toString()},
                     {"languages", languageChoices()},
                     {"install", install}});
    }

    // Listens in this language from now on: at once if its model is installed, otherwise after
    // downloading it (and the engine's runtime, if missing). False with an error if not possible.
    bool InstallLanguage(const QString &code) {
        if (!calledFromDBus())
            return false;
        idle_.restart();
        if (speech_.active() || !owner_.isEmpty()) {
            sendErrorReply(QDBusError::AccessDenied, tr("Stop listening before changing the language."));
            return false;
        }
        if (!SpeechCatalog::languages().contains(code)) {
            sendErrorReply(QDBusError::InvalidArgs, tr("Kalipso has no speech model for this language."));
            return false;
        }
        if (download_.active()) {
            if (code == download_.language())
                return true;
            sendErrorReply(QDBusError::AccessDenied, tr("Another language is downloading. Cancel it first."));
            return false;
        }
        install_ = {{"language", code}, {"received", 0}, {"total", 0}, {"error", ""}};
        const auto installed = SpeechCatalog::installedFor(code);
        if (!installed.backend.isEmpty()) {
            if (!useSpeech(installed)) {
                sendErrorReply(QDBusError::Failed, tr("Cannot save the language."));
                return false;
            }
            install_["backend"] = installed.backend;
            selected(installed.backend);
            finishInstall("done");
            return true;
        }
        const auto backend = SpeechCatalog::downloadBackend(code);
        install_["backend"] = backend;
        install_["stage"] = "downloading";
        install_["total"] = SpeechCatalog::plan(backend, code, SpeechCatalog::writableRoot()).bytes();
        download_.start(backend, code);
        return true;
    }

    void CancelInstall() {
        idle_.restart();
        download_.cancel();
    }

    QString GetState() {
        idle_.restart();
        if (calledFromDBus() && message().service() == owner_)
            lease_.restart();
        return json(state_);
    }

    bool Start() {
        if (!calledFromDBus())
            return false;
        const auto caller = message().service();
        if (!owner_.isEmpty()) {
            if (caller == owner_) {
                lease_.restart();
                return true;
            }
            sendErrorReply(QDBusError::AccessDenied,
                           tr("Another frontend owns the microphone. Stop listening there first."));
            return false;
        }
        const auto check = SpeechConfig::dependencies(SpeechConfig::selected());
        if (!check.value("ready").toBool()) {
            state_["status"] = "missing-dependencies";
            state_["message"] = check.value("missing").toArray().first().toString();
            publish();
            return false;
        }
        owner_ = caller;
        speechWatcher_.addWatchedService(owner_);
        lease_.start();
        resetTranscript();
        state_["status"] = "loading";
        state_["message"] = tr("Loading local speech model");
        state_["backend"] = check.value("backend");
        publish();

        SpeechRequest request;
        request.backend = check.value("backend").toString();
        request.library = check.value("library").toString();
        request.model = check.value("model").toString();
        request.language = check.value("language").toString();
        request.architecture = check.value("architecture").toInt();
        request.microphone = QSettings().value("speech/microphone").toString();
        speech_.start(request);
        return speech_.active();
    }

    void Stop() {
        if (calledFromDBus() && !owner_.isEmpty() && message().service() != owner_) {
            sendErrorReply(QDBusError::AccessDenied, tr("Only the listening frontend can stop its session."));
            return;
        }
        stopSpeech();
    }

    // The script being read, before Start and whenever it changes: Vosk then expects its sentences
    // and follows a reading through room noise far better (see speechVocabulary). Frontends that
    // do not call it, and Moonshine, hear any words of the language.
    void SetScript(const QString &script) {
        if (calledFromDBus() && !owner_.isEmpty() && message().service() != owner_) {
            sendErrorReply(QDBusError::AccessDenied, tr("Only the listening frontend can set the script."));
            return;
        }
        if (script.size() > kMaxScriptCharacters) {
            sendErrorReply(QDBusError::InvalidArgs, tr("The script is too long."));
            return;
        }
        idle_.restart();
        speech_.setScript(script, -1);
    }

    // --- The app's scripts (read only) ---
    // The scripts of the standalone app, newest first: {available, current, scripts: [{id, title,
    // modified, words, speed}]}. The widget and the extension read the same library as the app.
    QString ListScripts() {
        idle_.restart();
        return json(AppScripts::list());
    }

    // One script of the app's library: {id, title, modified, text, position, speed}.
    QString GetScript(const QString &id) {
        idle_.restart();
        const auto script = AppScripts::get(id);
        if (script.isEmpty()) {
            sendErrorReply(QDBusError::InvalidArgs, tr("This script is not in Kalipso's library."));
            return {};
        }
        if (script.value("text").toString().size() > kMaxScriptCharacters) {
            sendErrorReply(QDBusError::InvalidArgs, tr("The script is too long."));
            return {};
        }
        return json(script);
    }

    // Opens the standalone app on this script, to change it there.
    bool OpenInApp(const QString &id) {
        if (!calledFromDBus())
            return false;
        idle_.restart();
        if (AppScripts::get(id).isEmpty()) {
            sendErrorReply(QDBusError::InvalidArgs, tr("This script is not in Kalipso's library."));
            return false;
        }
        if (!AppScripts::open(id)) {
            sendErrorReply(QDBusError::Failed, tr("Kalipso could not be started. Is the app installed?"));
            return false;
        }
        return true;
    }

signals:
    void StateChanged(const QString &state);

private:
    void selected(const QString &backend) {
        resetTranscript();
        state_["backend"] = backend;
        state_["status"] = "ready";
        state_["message"] = tr("Speech language selected.");
        publish();
    }
    void finishInstall(const QString &stage, const QString &error = {}) {
        install_["stage"] = stage;
        install_["error"] = error;
        idle_.restart();
    }
    bool ownsColors(const QString &token) const {
        return calledFromDBus() &&
               (colorOwner_.isEmpty() || (message().service() == colorOwner_ && token == colorToken_));
    }
    void publish() {
        state_["active"] = speech_.active();
        emit StateChanged(json(state_));
    }
    void resetTranscript() {
        events_ = {};
        state_["events"] = events_;
        state_["text"] = "";
        state_["final"] = false;
        state_["sequence"] = ++sequence_;
    }
    void releaseOwner() {
        if (!owner_.isEmpty())
            speechWatcher_.removeWatchedService(owner_);
        owner_.clear();
        idle_.restart();
    }
    void stopSpeech() {
        speech_.stop();
        releaseOwner();
        state_["status"] = "stopped";
        state_["message"] = tr("Microphone stopped");
        publish();
    }
    void stopColors() {
        colors_.stop();
        colorWatcher_.removeWatchedService(colorOwner_);
        colorOwner_.clear();
        colorToken_.clear();
        color_.clear();
        idle_.restart();
    }

    SpeechService speech_;
    QDBusServiceWatcher speechWatcher_;
    QString owner_;
    QElapsedTimer lease_;
    QJsonArray events_;
    int sequence_ = 0;
    QJsonObject state_{
        {"status", "ready"}, {"message", "Local engine ready"}, {"active", false}, {"text", ""}, {"final", false},
        {"sequence", 0},     {"backend", "moonshine"}};

    PortalColors colors_;
    QDBusServiceWatcher colorWatcher_;
    QString colorOwner_, colorToken_, color_;
    QElapsedTimer colorLease_;

    SpeechDownload download_;
    QJsonObject install_{{"stage", "idle"}, {"language", ""}, {"backend", ""},
                         {"received", 0},   {"total", 0},     {"error", ""}};

    QTimer leaseTimer_;
    QElapsedTimer idle_;
};

namespace {
// True when the running companion has the microphone open.
bool listening() {
    QDBusInterface running(kService, kObjectPath, kService);
    if (!running.isValid())
        return false;
    const QDBusReply<QString> reply = running.call("GetState");
    return reply.isValid() && QJsonDocument::fromJson(reply.value().toUtf8()).object().value("active").toBool();
}

int listModels() {
    for (const auto &value : languageChoices()) {
        const auto item = value.toObject();
        const auto state = item.value("ready").toBool()
                               ? QCoreApplication::translate("main", "installed")
                               : QCoreApplication::translate("main", "%1 MB to download")
                                     .arg((item.value("bytes").toInteger() + 524288) >> 20);
        std::printf("%s  %s  %s · %s\n", qPrintable(item.value("code").toString()),
                    qPrintable(item.value("name").toString()), qPrintable(item.value("backend").toString()),
                    qPrintable(state));
    }
    if (!SpeechCatalog::downloadsSupported())
        std::printf("%s\n", qPrintable(QCoreApplication::translate(
                                 "main", "Kalipso downloads speech models on Linux for x86_64 computers only.")));
    return 0;
}

// Downloads the model for --language (and the engine's runtime, if missing), then listens with it.
int download(const QCommandLineParser &args, QCoreApplication &app) {
    const auto code = args.value("language");
    const auto languages = SpeechCatalog::languages();
    if (!languages.contains(code)) {
        std::fprintf(stderr, "Choose --language %s.\n", qPrintable(languages.join('|')));
        return 2;
    }
    if (args.isSet("model") || args.isSet("library")) {
        std::fprintf(stderr, "--download installs a catalog model; use --configure for --model and --library.\n");
        return 2;
    }
    const auto backend = args.value("backend");
    if (!backend.isEmpty() && !SpeechCatalog::backendsFor(code).contains(backend)) {
        std::fprintf(stderr, "No %s model for %s: choose --backend %s.\n", qPrintable(backend), qPrintable(code),
                     qPrintable(SpeechCatalog::backendsFor(code).join('|')));
        return 2;
    }
    if (listening()) {
        std::fprintf(stderr, "Stop listening before changing the language.\n");
        return 2;
    }
    auto installed = SpeechCatalog::installedFor(code, backend);
    if (!backend.isEmpty() && installed.backend != backend)
        installed = {};
    if (installed.backend.isEmpty()) {
        SpeechDownload download;
        int result = 0, tenth = -1;
        QObject::connect(&download, &SpeechDownload::progress, &app, [&](qint64 received, qint64 total) {
            const int now = total > 0 ? int(received * 10 / total) : 10;
            if (now != tenth) {
                tenth = now;
                std::printf("%s\n", qPrintable(QCoreApplication::translate("main", "Downloading %1: %2% of %3 MB")
                                                     .arg(SpeechCatalog::languageName(code))
                                                     .arg(now * 10)
                                                     .arg((total + 524288) >> 20)));
                std::fflush(stdout);
            }
        });
        QObject::connect(&download, &SpeechDownload::unpackingStarted, &app, [] {
            std::printf("%s\n", qPrintable(QCoreApplication::translate("main", "Unpacking and checking")));
            std::fflush(stdout);
        });
        QObject::connect(&download, &SpeechDownload::finished, &app, [&](const InstalledSpeech &speech) {
            installed = speech;
            app.quit();
        });
        QObject::connect(&download, &SpeechDownload::failed, &app,
                         [&](SpeechDownload::Error error, const QString &detail) {
                             std::fprintf(stderr, "Download failed (%s): %s\n",
                                          qPrintable(SpeechDownload::errorName(error)), qPrintable(detail));
                             result = 1;
                             app.quit();
                         });
        download.start(SpeechCatalog::downloadBackend(code, backend), code);
        app.exec();
        if (result)
            return result;
    }
    if (!useSpeech(installed)) {
        std::fprintf(stderr, "Cannot write configuration\n");
        return 1;
    }
    std::printf("%s\n",
                qPrintable(QCoreApplication::translate(
                               "main", "%1 is ready, with %2 (%3). No microphone was opened.\nIn Kalipso, the Plasma "
                                       "widget or the GNOME extension choose Follow my voice, then Start.")
                               .arg(SpeechCatalog::languageName(code),
                                    installed.backend == QLatin1String("moonshine") ? QStringLiteral("Moonshine")
                                                                                    : QStringLiteral("Vosk"),
                                    installed.model)));
    return 0;
}

// Updates the saved profile from command-line options. Returns a process exit
// code, or -1 to continue.
int configure(const QCommandLineParser &args) {
    const auto backend = args.isSet("backend") ? args.value("backend") : SpeechConfig::selected();
    bool architectureOk = true;
    int architecture = 2;
    if (args.isSet("architecture")) {
        architecture = args.value("architecture").toInt(&architectureOk);
        architectureOk = architectureOk && architecture >= 2 && architecture <= 5;
    }
    if (!SpeechConfig::supported(backend) || !architectureOk) {
        std::fprintf(stderr, "Choose --backend moonshine|vosk and a streaming --architecture 2|3|4|5.\n");
        return 2;
    }
    if (listening()) {
        std::fprintf(stderr, "Stop listening before configuring a speech engine.\n");
        return 2;
    }
    QSettings config;
    config.setValue("speech/backend", backend);
    for (const auto *name : {"library", "model", "language"})
        if (args.isSet(name))
            config.setValue("profiles/" + backend + "/" + name, args.value(name));
    if (args.isSet("architecture"))
        config.setValue("profiles/" + backend + "/architecture", architecture);
    if (args.isSet("microphone"))
        config.setValue("speech/microphone", args.value("microphone"));
    config.sync();
    if (config.status() != QSettings::NoError) {
        std::fprintf(stderr, "Cannot write configuration\n");
        return 1;
    }
    return -1;
}

void print(const QJsonObject &value) { std::printf("%s\n", json(value).toUtf8().constData()); }
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Kalipso");
    QCoreApplication::setApplicationName("engine");
    importLegacySettings("Teleprompter", "engine");
    // Messages in the session's language: frontends show the companion's words as they are.
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("kalipso-engine"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    QCommandLineParser args;
    args.setApplicationDescription("Local speech companion for the Plasma widget and GNOME extension.");
    args.addHelpOption();
    args.addOptions({
        {"serve", "Run the session D-Bus service."},
        {"check", "Report missing speech components as JSON."},
        {"configure", "Save the given options, then report as with --check."},
        {"list-backends", "List speech engines and their saved profiles as JSON."},
        {"download", "Download the speech model for --language, then listen with it."},
        {"list-models", "List the languages Kalipso can download, and which are installed."},
        {"backend", "Speech engine: moonshine or vosk.", "name"},
        {"library", "Native library path or name.", "path"},
        {"model", "Extracted model folder.", "path"},
        {"language", "Model language, for example en or it.", "code"},
        {"architecture", "Moonshine streaming architecture: 2 tiny, 3 base, 4 small, 5 medium.", "number"},
        {"microphone", "PipeWire source node name; empty for the default.", "node"},
    });
    args.process(app);

    if (args.isSet("list-models"))
        return listModels();
    if (args.isSet("download"))
        return download(args, app);
    if (args.isSet("configure")) {
        if (const int code = configure(args); code >= 0)
            return code;
    }
    if (args.isSet("list-backends")) {
        print({{"selected", SpeechConfig::selected()}, {"backends", SpeechConfig::backends()}});
        return 0;
    }
    if (args.isSet("check") || args.isSet("configure")) {
        const auto result = SpeechConfig::dependencies(SpeechConfig::selected());
        print(result);
        return result.value("ready").toBool() ? 0 : 1;
    }
    if (!args.isSet("serve"))
        args.showHelp(2);

    auto bus = QDBusConnection::sessionBus();
    Engine engine;
    if (!bus.registerObject(kObjectPath, &engine,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals) ||
        !bus.registerService(kService)) {
        std::fprintf(stderr, "Cannot register session D-Bus service: %s\n", qPrintable(bus.lastError().message()));
        return 1;
    }

    // Quit cleanly on SIGTERM/SIGINT so the microphone and portal sessions are released.
    int shutdownPipe[2];
    if (::pipe2(shutdownPipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        std::perror("shutdown pipe");
        return 1;
    }
    shutdownWriteFd = shutdownPipe[1];
    std::signal(SIGTERM, requestShutdown);
    std::signal(SIGINT, requestShutdown);
    QSocketNotifier notifier(shutdownPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
        char bytes[32];
        [[maybe_unused]] const auto read = ::read(shutdownPipe[0], bytes, sizeof(bytes));
        app.quit();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &engine, &Engine::Stop);
    const int result = app.exec();
    shutdownWriteFd = -1;
    ::close(shutdownPipe[0]);
    ::close(shutdownPipe[1]);
    return result;
}

#include "main.moc"
