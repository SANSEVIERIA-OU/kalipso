// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
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
class QScreen;
class QCheckBox;
class QDialog;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QLineEdit;
class QProgressBar;
class QSpinBox;
class RemoteControl;
class AppBridge;
class SpeechDownload;
struct InstalledSpeech;

class GnomeExtension;

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

    // The window goes small under the camera, and back to the size it had, with room to write. The
    // same window and the same text either way: what you write is what you read.
    void placeUnderCamera();
    void expand();
    // The desktop's messages: minimized (Kalipso's KWin script on Wayland), a second start asking
    // for the window.
    void connectBridge(AppBridge *bridge);
    // Brings the window forward: from a second start, with the desktop's activation token.
    void comeForward(const QString &token = {});
    bool underCamera() const { return underCamera_; }
    void setDemoScript();
    // Starts steady scrolling immediately, without changing the saved mode.
    void startSteadyScrolling();
    ReaderPage *prompter() const { return prompter_; }
    // Where reading is and how long the script takes, as the status says it while writing.
    QString positionText() const { return positionText_; }
    QString currentScript() const { return currentId_; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    // Script library
    QWidget *makeDrawer();
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
    // Bold, italic, cue: on the selection, or off the text that has it. Heading: the line.
    void toggleFormat(const QString &kind);
    void updateFormatState();
    // One step larger (1) or smaller (-1), saved as the reader's text size.
    void stepTextSize(int direction);
    // What just happened, for a few seconds in the reader's status.
    void notify(const QString &message);
    void deleteScript();
    // The first-run page: shown instead of an empty script until there is one.
    QWidget *makeWelcome();
    void refreshWelcome();
    // Whether voice following has a model for the chosen language.
    bool voiceReady() const;
    void loadSample();

    // Reading
    // Frame, size, place and stacking of the window for reading or writing.
    void placeWindow(bool reading);
    void saveReaderGeometry(bool resized);
    // Resizes, and keeps this size against the window system's answers for a moment (from the first
    // call: restart = false keeps the moment going).
    void resizeFor(QSize size, bool restart = true);
    static constexpr int kSizeSettles = 1500; // ms
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
    void startListening();
    // Opens Settings; with a field, scrolled to it and focused.
    void showSettings(QWidget *field = nullptr);
    void finishSession();

    // Settings
    QWidget *makeSettings();
    QWidget *makeReadingSettings();
    QWidget *makeSpeechSettings();
    QWidget *makeRemoteSettings();
    QWidget *makeGnomeExtensionSection();
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
    void fitSettingsPages();
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
    bool underCamera_ = false;
    bool placing_ = false;
    bool captureExcluded_ = false;
    // Under the camera when a monitor goes away: back onto one that is left (without Kalipso's KWin
    // script, which does it itself).
    void keepReaderOnScreen(QScreen *gone);
    // The minimized window pauses reading (also when only Kalipso's KWin script can tell).
    void pauseOutOfSight();
    QRect writingGeometry_;
    bool writingMaximized_ = false;
    RemoteControl *remote_ = nullptr;
    class ImpressFollower *impress_ = nullptr;
    QAction *impressAction_ = nullptr;
    QList<QAction *> formatActions_;
    ReadingMode mode_ = ReadingMode::Voice;
    QTimer steady_, countdown_;
    int countdownLeft_ = 0;
    // 150: Italian read aloud (182 words/min, Brysbaert 2019) slowed as a script is read to listeners
    // (audiobooks 155 against 190 read aloud in English): see docs/reading-speed.md.
    int wordsPerMinute_ = 150;
    QString speechStatus_;
    QString speechError_;           // Why listening did not start or stopped; shown in the reader until the next try.
    bool microphoneSilent_ = false; // No sound from the microphone: said before anything else.
    bool waitForNewUtterance_ = false;

    QListWidget *scriptList_ = nullptr;
    QAbstractButton *more_ = nullptr;
    QWidget *welcome_ = nullptr;
    QLabel *welcomeVoice_ = nullptr;
    QPushButton *welcomeVoiceButton_ = nullptr;
    bool welcomeDismissed_ = false;
    QList<class StepBadge *> steps_;
    ReaderView *script_;
    QSpinBox *textSize_ = nullptr;
    QString positionText_; // "120 words · about 1 min", the status while writing.
    QDialog *settings_ = nullptr;
    QWidget *appearanceSettings_ = nullptr;
    GnomeExtension *gnomeExtension_ = nullptr;
    class QFormLayout *readerForm_ = nullptr;
    QListWidget *settingsNav_ = nullptr;
    QStackedWidget *settingsStack_ = nullptr;
    std::function<void(int)> settingsAdapt_;
    QList<QPair<QAbstractButton *, QString>> icons_; // Buttons whose icon follows the theme.

    QComboBox *screen_ = nullptr, *voiceLanguage_ = nullptr, *backend_ = nullptr, *language_ = nullptr,
              *architecture_ = nullptr;
    QWidget *customSpeech_;
    SpeechDownload *download_;
    QProgressBar *downloadProgress_;
    StatusLabel *downloadStatus_;
    QPushButton *downloadAction_;
    // A language Kalipso downloaded: where it is, how big, its licence; remove or download again.
    QWidget *installedBox_ = nullptr;
    StatusLabel *installedStatus_ = nullptr;
    QPushButton *repairModel_ = nullptr, *removeModel_ = nullptr;
    // The chosen language's model, when Kalipso downloaded it (else an empty backend).
    InstalledSpeech chosenDownload() const;
    void removeLanguage(bool downloadAgain);
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
