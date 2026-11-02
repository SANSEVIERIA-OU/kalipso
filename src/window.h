// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "library.h"
#include "reader.h"
#include "session_stats.h"
#include "speech.h"
#include "tracker.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <functional>

class QAbstractButton;
class QCheckBox;
class QDialog;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QLineEdit;
class ScriptEditor;
class QProgressBar;
class QSpinBox;
class RemoteControl;
class SpeechDownload;
struct InstalledSpeech;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // An empty libraryDirectory uses the application data folder.
    explicit MainWindow(const QString &libraryDirectory = {});
    // Writes the last voice session's report (More → Save session report…).
    bool saveSessionReport(const QString &path);
    // Imports a text, document or presentation file as a new script and opens it.
    bool importFile(const QString &path);
    ~MainWindow() override;

    // The window turns into the reader near the camera, and back into the editor. The same
    // window and the same script: what you write is what you read.
    void showPrompter();
    void showWriting();
    bool reading() const { return reading_; }
    void setDemoScript();
    // Starts steady scrolling immediately, without changing the saved mode.
    void startSteadyScrolling();
    ReaderPage *prompter() const { return prompter_; }
    QString currentScript() const { return currentId_; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    // Script library
    void buildLibraryBar(QWidget *bar);
    void refreshLibrary();
    QString scriptMeta(const ScriptLibrary::Entry &entry) const;
    void refreshCurrentRow();
    void openScript(const QString &id);
    bool saveScript(); // False when the current script could not be written.
    void newScript();
    // Removes the current script if it has no text.
    void dropIfEmpty();
    void importScript();
    void followSlide(int number, const QString &notes);
    void exportScript();
    void wrapSelection(const QString &before, const QString &after);
    void toggleHeading();
    void deleteScript();
    // The first-run page: shown instead of an empty editor until there is a script.
    QWidget *makeWelcome();
    void refreshWelcome();
    // Wide windows list the scripts beside the editor; narrow ones pick them from the app bar.
    void adaptLayout();
    // Whether voice following has a model for the chosen language.
    bool voiceReady() const;
    void loadSample();

    // Reading
    // Frame, size, place and stacking of the window for reading or writing.
    void placeWindow(bool reading);
    void saveReaderGeometry(bool resized);
    // Resizes, and keeps this size against the window system's answers for a moment.
    void resizeFor(QSize size);
    QSize wantedSize_;
    QElapsedTimer wanted_;
    void updateWindowTitle();
    void setCaptureExcluded(bool excluded);
    void refreshReading();
    void seekWord(int word);
    void setMode(ReadingMode mode);
    void toggleRunning();
    void startRunning();
    void pauseRunning();
    bool running() const;
    void refreshRunning();
    QString statusText() const;
    void steadyTick();
    void changeSpeed(int delta);
    void jumpSection(int direction);
    bool voiceCommand(const QString &utterance);
    void startListening();
    // Opens Settings; with a field, scrolled to it and focused.
    void showSettings(QWidget *field = nullptr);
    void finishSession();

    // Settings
    QWidget *makeSettings();
    QWidget *makeReadingSettings();
    QWidget *makeSpeechSettings();
    QWidget *makeRemoteSettings();
    QWidget *makeKeySettings();
    void applyTheme();
    bool applyingTheme_ = false;
    void refreshScreens();
    void refreshVoiceLanguages();
    // Listens with this model from now on; announce says so under the language list.
    void useInstalledSpeech(const InstalledSpeech &speech, bool announce = true);
    void downloadLanguage(const QString &code);
    // The microphones connected now, keeping the one chosen even when it is unplugged.
    void refreshMicrophones();
    void saveMicrophone();
    void startMicrophoneTest();
    // A finished test says whether Kalipso heard a voice; otherwise the note comes back.
    void stopMicrophoneTest(bool finished);
    void loadSpeechProfile();
    void saveSpeechProfile();
    void saveSettings();
    void applyInterfaceScale(double scale);
    double interfaceScale_ = 1.0;
    QString speechLanguage() const;

    ScriptLibrary library_;
    QString currentId_;
    QString scriptText_;
    bool persist_ = true;
    bool loading_ = false;
    QTimer autosave_;

    ScriptTracker tracker_;
    SpeechService speech_;
    SystemSpeechClient systemCheck_;
    SessionStats session_;
    SessionStats::Summary lastSession_; // The last voice session, for "Save session report…".
    QString lastSessionScript_;
    QDateTime lastSessionAt_;
    QAction *reportAction_ = nullptr;
    ReaderPage *prompter_;
    QStackedWidget *views_ = nullptr;
    QWidget *writingView_ = nullptr;
    bool reading_ = false;
    bool placing_ = false;
    bool captureExcluded_ = false;
    QRect writingGeometry_;
    bool writingMaximized_ = false;
    RemoteControl *remote_ = nullptr;
    class ImpressFollower *impress_ = nullptr;
    QAction *impressAction_ = nullptr;
    QList<QAction *> formatActions_;
    bool voiceCommands_ = false;
    ReadingMode mode_ = ReadingMode::Voice;
    QTimer steady_, countdown_;
    int countdownLeft_ = 0;
    int wordsPerMinute_ = 140;
    QString speechStatus_;
    QString speechError_;           // Why listening did not start or stopped; shown in the reader until the next try.
    bool microphoneSilent_ = false; // No sound from the microphone: said before anything else.
    bool waitForNewUtterance_ = false;

    QComboBox *scripts_;
    QListWidget *scriptList_ = nullptr;
    QWidget *libraryPanel_ = nullptr;
    class QToolButton *more_ = nullptr;
    QStackedWidget *editorStack_ = nullptr;
    QWidget *welcome_ = nullptr;
    QLabel *welcomeVoice_ = nullptr;
    QPushButton *welcomeVoiceButton_ = nullptr;
    bool welcomeDismissed_ = false;
    QList<class StepBadge *> steps_;
    QPushButton *readHere_ = nullptr;
    ScriptEditor *script_;
    QDialog *settings_ = nullptr;
    QWidget *appearanceSettings_ = nullptr;
    class QFormLayout *readerForm_ = nullptr;
    QListWidget *settingsNav_ = nullptr;
    QStackedWidget *settingsStack_ = nullptr;
    std::function<void(int)> settingsAdapt_;
    QList<QPair<QAbstractButton *, QString>> icons_; // Buttons whose icon follows the theme.
    StatusLabel *audioStatus_;
    QLabel *position_;

    QComboBox *screen_ = nullptr, *voiceLanguage_ = nullptr, *backend_ = nullptr, *language_ = nullptr,
              *architecture_ = nullptr;
    QWidget *customSpeech_;
    SpeechDownload *download_;
    QProgressBar *downloadProgress_;
    StatusLabel *downloadStatus_;
    QPushButton *downloadAction_;
    QString downloadCode_;
    QElapsedTimer downloadShown_;
    std::function<void()> updateSpeechFields_;
    QLineEdit *speechLibrary_ = nullptr, *modelPath_ = nullptr;
    QComboBox *microphone_;
    // Settings → Microphone → Test: a level meter, and whether a voice came through.
    MicrophoneCapture microphoneTest_;
    QPushButton *testMicrophone_;
    QProgressBar *microphoneLevel_;
    StatusLabel *microphoneStatus_;
    QTimer microphoneTestEnd_;
    double meter_ = 0;
    bool heard_ = false;
    QLabel *systemStatus_;
    QPushButton *checkSystem_, *installSystem_;
    QString profile_;
    bool installingSystem_ = false;
};
