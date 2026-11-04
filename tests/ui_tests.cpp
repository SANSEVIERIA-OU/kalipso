// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "app_bridge.h"
#include "fake_pipewire.h"
#include "impress.h"
#include "remote.h"
#include "script_highlighter.h"
#include "speech.h"
#include "speech_download.h"
#include "window.h"
#include <QAbstractButton>
#include <QAccessible>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFontInfo>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>
#include <atomic>
#include <thread>
#ifdef KALIPSO_PORTAL_COLORS
#include <QDBusInterface>
#include <QDBusReply>
#endif

namespace {
// One window: the reader's text is also where the script is written.
QTextEdit *editor(MainWindow &window) { return window.prompter()->text; }
// Where reading is, as the status says it while writing ("Word 3 of 4 · …").
QString position(MainWindow &window) { return window.positionText(); }
// The reader's status line, where messages about what just happened show for a few seconds.
QLabel *statusLine(MainWindow &window) { return window.prompter()->findChild<QLabel *>("readerListeningStatus"); }
// The widgets that need the most width in a scroll area, so an overflow seen
// only on another machine (fonts, Qt version) can be fixed without guessing.
QString overflowReport(QScrollArea *area) {
    auto *content = area->widget();
    QList<QPair<int, QString>> needs;
    for (auto *child : content->findChildren<QWidget *>()) {
        if (!child->isVisible())
            continue;
        const int left = child->mapTo(content, QPoint(0, 0)).x();
        const int need = left + std::max(child->minimumSizeHint().width(), child->minimumWidth());
        QString text;
        if (auto *label = qobject_cast<QLabel *>(child))
            text = label->text().left(40);
        else if (auto *button = qobject_cast<QAbstractButton *>(child))
            text = button->text().left(40);
        needs.append({need, QString("%1#%2 '%3' x=%4 min=%5 hint=%6")
                                .arg(child->metaObject()->className(), child->objectName(), text)
                                .arg(left)
                                .arg(child->minimumSizeHint().width())
                                .arg(child->sizeHint().width())});
    }
    std::sort(needs.begin(), needs.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList lines{QString("%1 viewport=%2 content min=%3 width=%4")
                          .arg(area->objectName())
                          .arg(area->viewport()->width())
                          .arg(content->minimumSizeHint().width())
                          .arg(content->width())};
    for (int i = 0; i < std::min<qsizetype>(5, needs.size()); ++i)
        lines.append(QString("  needs %1: %2").arg(needs[i].first).arg(needs[i].second));
    return lines.join(u'\n');
}

QDialog *openSettings(MainWindow &window) {
    window.prompter()->findChild<QPushButton *>("readerSettings")->click(); // In the reader's top bar.
    return window.findChild<QDialog *>("settingsDialog");
}

void setCursor(QTextEdit *edit, int position) {
    auto cursor = edit->textCursor();
    cursor.setPosition(position);
    edit->setTextCursor(cursor);
}

// Horizontal centre of the dark "ink" in an image with a light background.
double inkCentre(const QImage &image) {
    double sum = 0, count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 128) {
                sum += x;
                ++count;
            }
    return count ? sum / count / image.width() : 0.5;
}
} // namespace

class UiTests : public QObject {
    Q_OBJECT
    QTemporaryDir scripts_;

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("kalipso-tests");
        QCoreApplication::setApplicationName("isolated-ui-tests");
        QVERIFY(scripts_.isValid());
    }
    void init() {
        QSettings().clear();
        QSettings().setValue("reading/countdown", 0);
        QDir(scripts_.path()).removeRecursively();
        QDir().mkpath(scripts_.path());
    }

    void interfaceLanguageIsVisibleAndSurvivesRestart() {
        {
            MainWindow window(scripts_.path());
            window.show();
            auto *settings = openSettings(window);
            auto *language = window.findChild<QComboBox *>("interfaceLanguage");
            QVERIFY(language);
            auto *nav = settings->findChild<QListWidget *>("settingsPages");
            QVERIFY(nav);
            nav->setCurrentRow(2); // Appearance.
            QTRY_VERIFY(language->isVisible());
            QVERIFY(settings->rect().contains(language->mapTo(settings, language->rect().center())));
            auto *form = language->parentWidget()->findChild<QFormLayout *>();
            QVERIFY(form);
            int row = -1;
            QFormLayout::ItemRole role;
            form->getWidgetPosition(language, &row, &role);
            QCOMPARE(row, 0); // Reachable before any appearance controls, without scrolling.
            QCOMPARE(language->count(), 40); // System default and every supported language.
            language->setCurrentIndex(language->findData("it"));
            QCOMPARE(QSettings().value("appearance/language").toString(), QString("it"));
            language->setCurrentIndex(language->findData("ar"));
            QCOMPARE(QSettings().value("appearance/language").toString(), QString("ar"));
        }
        MainWindow reopened(scripts_.path());
        QCOMPARE(reopened.findChild<QComboBox *>("interfaceLanguage")->currentData().toString(), QString("ar"));
    }

    void engineProfilesKeepTheirOwnModelsAndLanguages() {
        {
            MainWindow window(scripts_.path());
            auto *engine = window.findChild<QComboBox *>("speechBackend");
            auto *language = window.findChild<QComboBox *>("speechLanguage");
            auto *model = window.findChild<QLineEdit *>("speechModel");
            QVERIFY(engine && language && model);
            engine->setCurrentIndex(engine->findData("moonshine"));
            model->setText("moonshine-model");
            language->setEditText("de-DE");
            engine->setCurrentIndex(engine->findData("vosk"));
            model->setText("vosk-model");
            language->setCurrentIndex(language->findData("it-IT"));
            engine->setCurrentIndex(engine->findData("moonshine"));
            QCOMPARE(model->text(), QString("moonshine-model"));
            QCOMPARE(language->currentData().toString(), QString("de-DE"));
            engine->setCurrentIndex(engine->findData("vosk"));
            QCOMPARE(model->text(), QString("vosk-model"));
            QCOMPARE(language->currentData().toString(), QString("it-IT"));
        }
        MainWindow reopened(scripts_.path());
        QCOMPARE(reopened.findChild<QComboBox *>("speechBackend")->currentData().toString(), QString("vosk"));
        QCOMPARE(reopened.findChild<QLineEdit *>("speechModel")->text(), QString("vosk-model"));
    }

    void newProfilesOfferGuidedDownloadsAndKeepSystemSpeechOptional() {
        if (!SpeechCatalog::downloadsSupported())
            QSKIP("No runtime for this platform");
        MainWindow window(scripts_.path());
        window.show();
        auto *settings = openSettings(window);
        auto *engine = window.findChild<QComboBox *>("speechBackend");
        auto *languages = window.findChild<QComboBox *>("voiceLanguage");
        QVERIFY(engine && languages);
        QVERIFY(engine->currentData().toString() != "system");
        QTRY_VERIFY(languages->isVisibleTo(settings));
        QVERIFY(languages->findText("Italiano · download", Qt::MatchStartsWith) > 0);
        QVERIFY(window.findChild<QLineEdit *>("speechModel")->text().isEmpty());
        QVERIFY(!window.findChild<QPushButton *>("speechDownloadAction")->isVisibleTo(settings));
        if (engine->findData("system") >= 0) {
            engine->setCurrentIndex(engine->findData("system"));
            QVERIFY(!languages->isVisibleTo(settings));
            engine->setCurrentIndex(engine->findData("vosk"));
            QTRY_VERIFY(languages->isVisibleTo(settings));
            QVERIFY(languages->findText("Italiano · download", Qt::MatchStartsWith) > 0);
            engine->setCurrentIndex(engine->findData("system"));
            window.close(); // Closing the application persists its selected engine.
            MainWindow reopened(scripts_.path());
            QCOMPARE(reopened.findChild<QComboBox *>("speechBackend")->currentData().toString(), QString("system"));
        }
    }

    void choosingAnEngineDoesNotReuseAnotherEnginesModel() {
        if (!SpeechCatalog::backendsFor("en").contains("moonshine"))
            QSKIP("No Moonshine download on this platform");
        const auto root = SpeechCatalog::writableRoot();
        const auto cleanup = qScopeGuard([root] { QDir(root).removeRecursively(); });
        const auto plan = SpeechCatalog::plan("vosk", "en", root);
        for (const auto &path : {plan.library, plan.modelDir + "/am/final.mdl", plan.modelDir + "/conf/mfcc.conf"}) {
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QCOMPARE(SpeechCatalog::installedFor("en", "vosk").backend, QString("vosk"));
        QVERIFY(SpeechCatalog::installedFor("en", "moonshine").backend.isEmpty());
        QCOMPARE(SpeechCatalog::installedFor("en").backend, QString("vosk"));
        if (SystemSpeechClient::platformSupported()) {
            QSettings().setValue("speech/backend", "system");
            MainWindow window(scripts_.path());
            auto *engine = window.findChild<QComboBox *>("speechBackend");
            auto *languages = window.findChild<QComboBox *>("voiceLanguage");
            engine->setCurrentIndex(engine->findData("moonshine"));
            QVERIFY(languages->findText("English · download", Qt::MatchStartsWith) > 0);
            engine->setCurrentIndex(engine->findData("vosk"));
            QVERIFY(languages->findText("English · ready", Qt::MatchStartsWith) > 0);
        }
    }

    void voiceLanguagesUseInstalledModelsAndDownloadOnlyWhenChosen() {
        // A Vosk runtime and the Italian model, where a download leaves them.
        const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kalipso";
        const auto cleanup = qScopeGuard([root] { QDir(root).removeRecursively(); });
        const auto model = root + "/models/vosk/vosk-model-small-it-0.22";
        for (const auto &file :
             {SpeechCatalog::plan("vosk", "it", root).library, model + "/am/final.mdl", model + "/conf/mfcc.conf"}) {
            QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
            QFile touch(file);
            QVERIFY(touch.open(QIODevice::WriteOnly));
        }
        QSettings().setValue("speech/backend", "moonshine"); // A profile without a model.
        MainWindow window(scripts_.path());
        auto *languages = window.findChild<QComboBox *>("voiceLanguage");
        auto *engine = window.findChild<QComboBox *>("speechBackend");
        auto *modelPath = window.findChild<QLineEdit *>("speechModel");
        QVERIFY(languages && engine && modelPath);
        // The installed model is used without asking, and the engine follows it.
        QCOMPARE(engine->currentData().toString(), QString("vosk"));
        QCOMPARE(modelPath->text(), model);
        if (!SpeechCatalog::downloadsSupported()) {
            QVERIFY(languages->currentText().contains("vosk-model-small-it-0.22"));
            return;
        }
        QCOMPARE(languages->currentText(), QString("Italiano · ready"));
        const int english = languages->findText("English · download", Qt::MatchStartsWith);
        QVERIFY(english > 0);
        QVERIFY(languages->itemText(english).endsWith(" MB"));
        // Keys open the list instead of choosing, and downloading, at once.
        window.show();
        openSettings(window);
        languages->setFocus();
        const int before = languages->currentIndex();
        for (const auto key : {Qt::Key_Down, Qt::Key_End, Qt::Key_E}) {
            QTest::keyClick(languages, key);
            QCOMPARE(languages->currentIndex(), before);
            languages->hidePopup();
        }
        QVERIFY(!window.findChild<QWidget *>("speechDownloadProgress")->isVisible());
    }

    void scriptsAndReadingPositionSurviveRestart() {
        QString id;
        {
            MainWindow window(scripts_.path());
            editor(window)->setPlainText("uno due tre quattro");
            emit window.prompter()->jumpRequested(8); // A word clicked: reading goes on from it.
            QVERIFY(position(window).startsWith("Word 3 of 4"));
            id = window.currentScript();
        }
        MainWindow reopened(scripts_.path());
        QCOMPARE(reopened.currentScript(), id);
        QCOMPARE(editor(reopened)->toPlainText(), QString("uno due tre quattro"));
        QVERIFY(position(reopened).startsWith("Word 3 of 4"));
    }

    void firstRunWelcomesAndTheListFollowsTheLibrary() {
        QTemporaryDir empty;
        MainWindow window(empty.path());
        window.resize(1100, 760);
        window.show();
        auto *welcome = window.findChild<QWidget *>("welcome");
        auto *list = window.findChild<QListWidget *>("scriptList");
        QVERIFY(welcome && list);
        QVERIFY(welcome->isVisible()); // An empty library opens on the three steps, not a blank page.
        QVERIFY(!editor(window)->isVisible());
        // The welcome has the whole window: nothing to list yet, and the empty script is not listed.
        QVERIFY(!list->isVisible());
        // The keys start on the welcome, not on the first button of the top bar (no ring there).
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *scroll = window.findChild<QWidget *>("welcomeScroll");
        QTRY_VERIFY(scroll->hasFocus() || scroll->isAncestorOf(QApplication::focusWidget()));
        QVERIFY(!window.prompter()->findChild<QPushButton *>("readerScripts")->hasFocus());
        // No words yet: the status says what the reader does, not "0 words".
        QVERIFY2(!statusLine(window)->text().startsWith("0 word"), qPrintable(statusLine(window)->text()));
        // Steps still to do are rings, not discs: a filled one read as a black blot.
        const auto steps = window.findChildren<QWidget *>("welcomeStep");
        QCOMPARE(steps.size(), 3);
        const QImage ring = steps.last()->grab().toImage();
        const QColor inside(ring.pixel(ring.width() / 5, ring.height() / 2));
        QVERIFY2(Accessibility::contrast(inside, steps.last()->palette().color(QPalette::Window)) < 1.2,
                 qPrintable(inside.name()));
        window.findChild<QPushButton *>("welcomeSample")->click();
        QVERIFY(editor(window)->isVisible());
        QVERIFY(list->isVisible()); // The list comes with the first script.
        QVERIFY(editor(window)->toPlainText().startsWith("# Welcome"));
        QTRY_COMPARE(list->currentItem()->text(), QString("Welcome"));
        window.findChild<QPushButton *>("newScript")->click();
        QCOMPARE(list->count(), 2);
        QVERIFY(editor(window)->isVisible()); // A second script is not a first run.
        list->setCurrentRow(list->row(list->findItems("Welcome", Qt::MatchExactly).value(0)));
        QVERIFY(editor(window)->toPlainText().startsWith("# Welcome"));
        // Small, as a teleprompter: the scripts close, and open over the text when asked for.
        window.resize(620, 400);
        QTRY_VERIFY(!list->isVisible());
        auto *scripts = window.prompter()->findChild<QPushButton *>("readerScripts");
        scripts->click();
        QTRY_VERIFY(list->isVisible());
        QTest::keyClick(list, Qt::Key_Escape); // Esc closes them, and the keys go back to the text.
        QTRY_VERIFY(!list->isVisible());
        QVERIFY(editor(window)->hasFocus() || !window.isActiveWindow());
        // Closed, no row is current and none has room: a screen reader hears no hidden row.
        QTRY_VERIFY(!list->currentItem());
        QVERIFY(list->rect().isEmpty());
        // Large again: the scripts beside the text, as before, with the open script's row current.
        window.resize(1100, 760);
        QTRY_VERIFY(list->isVisible());
        QVERIFY(list->currentItem());
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), window.currentScript());
    }

    void desktopLookMarksOnlyWhatTheStyleCannotKnow() {
        // Breeze's colours: white on its light blue reads at 2.3:1, so the primary action deepens.
        QPalette breeze;
        breeze.setColor(QPalette::Window, QColor("#202326"));
        breeze.setColor(QPalette::WindowText, QColor("#fcfcfc"));
        breeze.setColor(QPalette::Base, QColor("#141618"));
        breeze.setColor(QPalette::Highlight, QColor("#3daee9"));
        breeze.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        QWidget host;
        auto *quiet = new QPushButton("Quiet", &host);
        quiet->setProperty("quiet", true);
        auto *primary = new QPushButton("Open", &host);
        primary->setObjectName("primaryAction");
        auto *plain = new QPushButton("Plain", &host);
        Accessibility::applyNative(&host, breeze);
        QVERIFY(quiet->isFlat());
        QVERIFY(!plain->isFlat());
        const auto fill = primary->palette().color(QPalette::Button);
        QVERIFY(Accessibility::contrast(primary->palette().color(QPalette::ButtonText), fill) >= 4.5);
        QVERIFY(fill != QColor("#3daee9")); // Negative control: the accent as given fails.
        QVERIFY(Accessibility::contrast(QColor("#ffffff"), QColor("#3daee9")) < 4.5);
        QCOMPARE(plain->palette().color(QPalette::Button), host.palette().color(QPalette::Button));
        // Secondary text keeps 4.5:1 on the window and on fields.
        const auto muted = Accessibility::muted(breeze);
        QVERIFY(Accessibility::contrast(muted, breeze.color(QPalette::Window)) >= 4.5);
        QVERIFY(Accessibility::contrast(muted, breeze.color(QPalette::Base)) >= 4.5);
        QVERIFY(muted != breeze.color(QPalette::WindowText));
    }

    void libraryCreatesSwitchesAndDeletesScripts() {
        MainWindow window(scripts_.path());
        auto *list = window.findChild<QListWidget *>("scriptList");
        editor(window)->setPlainText("# First talk\nHello");
        const auto first = window.currentScript();
        window.findChild<QPushButton *>("newScript")->click();
        QVERIFY(window.currentScript() != first);
        QVERIFY(editor(window)->toPlainText().isEmpty());
        editor(window)->setPlainText("Second talk");
        QTRY_COMPARE(list->count(), 2);
        list->setCurrentRow(list->row(list->findItems("First talk", Qt::MatchExactly).value(0)));
        QCOMPARE(window.currentScript(), first);
        QCOMPARE(editor(window)->toPlainText(), QString("# First talk\nHello"));
        QCOMPARE(list->currentItem()->text(), QString("First talk"));
    }

    // The scripts panel lies on the reader, which stays dark on a light desktop: the selected script is
    // drawn in the reader's colours (its hover colour, as a checked quiet button), not as a block of the
    // desktop's light selection colour.
    void selectedScriptTakesTheReadersColoursOnALightDesktop() {
        const QPalette before = QApplication::palette();
        QPalette desktop = before;
        desktop.setColor(QPalette::Highlight, QColor("#F0F0F0")); // Windows and macOS, light.
        desktop.setColor(QPalette::HighlightedText, QColor("#000000"));
        QApplication::setPalette(desktop);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.prompter()->setReaderTheme("dark");
        editor(window)->setPlainText("# First talk\nHello");
        auto *list = window.findChild<QListWidget *>("scriptList");
        QTRY_VERIFY(list->isVisible() && list->count() == 1);
        list->setCurrentRow(0);
        const QRect row = list->visualItemRect(list->item(0));
        QVERIFY(!row.isEmpty());
        const QImage shot = list->viewport()->grab().toImage();
        // Inside the row, left of the text and away from the rounded corners and the border (in device
        // pixels, for screens that draw two per point).
        const qreal ratio = shot.devicePixelRatio();
        const QColor seen = shot.pixelColor(qRound((row.left() + 4) * ratio), qRound(row.center().y() * ratio));
        QApplication::setPalette(before);
        const QColor expected(Design::dark.hover);
        QVERIFY2(std::abs(seen.red() - expected.red()) + std::abs(seen.green() - expected.green()) +
                         std::abs(seen.blue() - expected.blue()) <= 12,
                 qPrintable(QString("selected row %1, reader hover %2").arg(seen.name(), expected.name())));
    }

    void accessiblePalettesKeepReadTextAndSelectionLegible() {
        for (const auto &p : {Design::light, Design::dark}) {
            const auto colors = Accessibility::palette(p);
            for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                QVERIFY(Accessibility::contrast(colors.color(group, QPalette::Text),
                                                colors.color(group, QPalette::Base)) >= 4.5);
                QVERIFY(Accessibility::contrast(colors.color(group, QPalette::PlaceholderText),
                                                colors.color(group, QPalette::Base)) >= 4.5);
                QVERIFY(Accessibility::contrast(colors.color(group, QPalette::HighlightedText),
                                                colors.color(group, QPalette::Highlight)) >= 4.5);
            }
            ReaderPage reader;
            reader.setReaderTheme(QString(p.background) == Design::light.background ? "light" : "dark");
            reader.text->setPlainText("Read current next");
            reader.highlight(5, 7);
            reader.show();
            QTest::qWait(30);
            QCOMPARE(reader.grab().toImage().pixelColor(3, 3), QColor(p.background));
            const auto selections = reader.text->extraSelections();
            QCOMPARE(selections.size(), 2);
            QVERIFY(Accessibility::contrast(selections[0].format.foreground().color(), QColor(p.surface)) >= 4.5);
            // The current word is accent text on a soft tint, underlined.
            QVERIFY(Accessibility::contrast(selections[1].format.foreground().color(),
                                            selections[1].format.background().color()) >= 4.5);
            QVERIFY(selections[1].format.fontUnderline());
        }
        QVERIFY(Accessibility::contrast(QColor("#777777"), Qt::white) < 4.5);
    }

    void interfaceFontIsEmbedded() {
        Accessibility::useInterfaceFont();
        QCOMPARE(QFontInfo(QApplication::font()).family(), QString("Atkinson Hyperlegible Next"));
        QFont bold = QApplication::font();
        bold.setBold(true);
        QCOMPARE(QFontInfo(bold).family(), QString("Atkinson Hyperlegible Next"));
    }

    // Run twice by CTest: as the system is set, and with KALIPSO_REDUCED_MOTION=1.
    void iconMotionFollowsTheSystemChoice() {
        AccessibleButton button("Next");
        button.setIcon(Icons::icon("next", Qt::black));
        button.setProperty("motion", Icons::motion("next"));
        button.show();
        const QRectF icon(0, 0, 18, 18);
        auto *motion = button.iconMotion();
        QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), QPointF(4, 4));
        if (qEnvironmentVariableIsSet("KALIPSO_REDUCED_MOTION")) // The override must be honoured.
            QCOMPARE(Motion::reduced(), qEnvironmentVariableIntValue("KALIPSO_REDUCED_MOTION") != 0);
        QCoreApplication::sendEvent(&button, &enter);
        if (Motion::reduced()) {
            QTest::qWait(Design::Duration::enter + 100);
            QCOMPARE(motion->progress(), 0.0);
            QVERIFY(motion->transform(icon).isIdentity());
            return;
        }
        QTRY_COMPARE_WITH_TIMEOUT(motion->progress(), 1.0, 1000);
        QCOMPARE(motion->transform(icon).dx(), 3.0);
        button.setLayoutDirection(Qt::RightToLeft); // Forward follows the reading direction.
        QCOMPARE(motion->transform(icon).dx(), -3.0);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(&button, &leave);
        QTRY_COMPARE_WITH_TIMEOUT(motion->progress(), 0.0, 1000);
        // Without a named movement nothing moves.
        button.setProperty("motion", QString());
        QCoreApplication::sendEvent(&button, &enter);
        QTest::qWait(Design::Duration::enter + 100);
        QCOMPARE(motion->progress(), 0.0);
    }

    void statusHasAnAccessibleRoleAndName() {
        StatusLabel status("Paused");
        auto *interface = QAccessible::queryAccessibleInterface(&status);
        QVERIFY(interface);
        QCOMPARE(interface->role(), QAccessible::StatusBar);
        status.setText("Listening");
        QCOMPARE(interface->text(QAccessible::Name), QString("Listening"));
    }

    void settingsFieldsHaveAccessibleNames() {
        MainWindow window(scripts_.path());
        for (const auto *name : {"speechBackend", "speechLanguage", "speechLibrary", "speechModel", "applicationTheme",
                                 "readerTheme", "readerMirror", "readingLine", "voiceLanguage", "scriptList",
                                 "microphone", "testMicrophone", "microphoneLevel"}) {
            auto *field = window.findChild<QWidget *>(name);
            QVERIFY2(field, name);
            auto *interface = QAccessible::queryAccessibleInterface(field);
            QVERIFY(interface);
            QVERIFY2(!interface->text(QAccessible::Name).isEmpty(), name);
        }
    }

    void settingsScrollDoesNotChangeSelections() {
        MainWindow window(scripts_.path());
        window.show();
        auto *settings = openSettings(window);
        QVERIFY(settings && settings->isVisible());
        const auto wheel = [](QComboBox *combo) {
            QWheelEvent event(QPointF(10, 10), combo->mapToGlobal(QPoint(10, 10)), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(combo, &event);
        };
        for (auto *combo : settings->findChildren<QComboBox *>()) {
            if (!combo->isVisible() || combo->count() < 2)
                continue;
            combo->setFocus();
            const int before = combo->currentIndex();
            wheel(combo);
            QCOMPARE(combo->currentIndex(), before);
        }
        QComboBox unsafe;
        unsafe.addItems({"First", "Second"});
        unsafe.show();
        unsafe.setFocus();
        wheel(&unsafe);
        QCOMPARE(unsafe.currentIndex(), 1); // Counterexample: ordinary combos change while scrolling.
    }

    void writingShowsTheFormatsAndOnlyTheMarksAtTheCaret() {
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        auto *reader = window.prompter();
        auto *edit = editor(window);
        // A large window writes: the text takes the caret and the formats are in the top bar.
        QTRY_VERIFY(reader->editing());
        QVERIFY(!edit->isReadOnly());
        QVERIFY(reader->findChild<QWidget *>("readerFormats")->isVisible());
        edit->setPlainText("# Opening\n\nHello **everyone** here\n\n## Details");
        QTest::qWait(30);
        const auto hidden = [edit](int blockNumber, int column) {
            const auto block = edit->document()->findBlockByNumber(blockNumber);
            for (const auto &range : block.layout()->formats())
                if (column >= range.start && column < range.start + range.length &&
                    range.format.hasProperty(QTextFormat::FontPointSize) && range.format.fontPointSize() <= 1)
                    return true;
            return false;
        };
        const auto place = [edit](int position) {
            QTextCursor caret(edit->document());
            caret.setPosition(position);
            edit->setTextCursor(caret);
        };
        const QString text = edit->toPlainText();
        // Away from the caret every mark is hidden, as in the reader.
        place(int(text.indexOf("here")));
        QVERIFY(hidden(0, 0)); // "#"
        QVERIFY(hidden(2, 6)); // "**" before "everyone": the caret is past the bold words.
        place(int(text.indexOf("Hello")));
        QVERIFY(hidden(2, 6));
        QVERIFY(hidden(2, 16));
        // At the caret the marks show, so the caret never moves through marks it cannot see.
        place(int(text.indexOf("everyone")) + 3);
        QVERIFY(!hidden(2, 6));
        QVERIFY(!hidden(2, 16));
        auto *bold = window.findChild<QAction *>("formatBold");
        QVERIFY(bold->isChecked()); // The Bold button shows that the caret is in bold text.
        // Bold again takes it off, as in word processors.
        bold->trigger();
        QCOMPARE(edit->document()->findBlockByNumber(2).text(), QString("Hello everyone here"));
        QVERIFY(!bold->isChecked());
        edit->undo();
        QCOMPARE(edit->document()->findBlockByNumber(2).text(), QString("Hello **everyone** here"));
        // The heading's marks show only on its line; Backspace at its start makes it a line again.
        place(2);
        QVERIFY(!hidden(0, 0));
        QVERIFY(window.findChild<QAction *>("formatHeading")->isChecked());
        QTest::keyClick(edit, Qt::Key_Backspace);
        QVERIFY(edit->toPlainText().startsWith("Opening\n"));
        edit->undo();
        QVERIFY(edit->toPlainText().startsWith("# Opening\n"));
        // Copying gives the script as written, marks included, as plain text.
        QTextCursor all(edit->document());
        all.select(QTextCursor::Document);
        edit->setTextCursor(all);
        edit->copy();
        QCOMPARE(QApplication::clipboard()->text(), edit->toPlainText());
        QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
        // Small, as a teleprompter, it reads: no caret, no formats.
        window.resize(620, 400);
        QTRY_VERIFY(!reader->editing());
        QVERIFY(edit->isReadOnly());
        QVERIFY(!reader->findChild<QWidget *>("readerFormats")->isVisible());
        QVERIFY(hidden(0, 0)); // Every mark hidden again.
    }

    void theTextSizeStepsLikeAWordProcessorAndIsSaved() {
        MainWindow window(scripts_.path());
        window.resize(620, 400);
        window.show();
        auto *reader = window.prompter();
        auto *size = window.findChild<QSpinBox *>("readerTextSize");
        size->setValue(28);
        reader->findChild<QPushButton *>("readerTextLarger")->click();
        QCOMPARE(size->value(), 32); // An eighth larger.
        QCOMPARE(reader->text->font().pointSize(), 32);
        QCOMPARE(QSettings().value("reading/fontSize").toInt(), 32);
        reader->findChild<QPushButton *>("readerTextSmaller")->click();
        reader->findChild<QPushButton *>("readerTextSmaller")->click();
        QCOMPARE(size->value(), 25);
        // Every step up comes back down to where it was.
        for (int i = 0; i < 20; ++i)
            emit reader->textSizeRequested(1);
        QCOMPARE(size->value(), size->maximum());
        for (int i = 0; i < 20; ++i)
            emit reader->textSizeRequested(-1);
        QCOMPARE(size->value(), size->minimum());
        // Ctrl and the wheel step too, instead of a zoom Settings would not know.
        QWheelEvent wheel(QPointF(10, 10), reader->text->viewport()->mapToGlobal(QPointF(10, 10)), QPoint(),
                          QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(reader->text->viewport(), &wheel);
        QCOMPARE(size->value(), 20);
        QCOMPARE(reader->text->font().pointSize(), 20);
    }

    void anEmptyScriptIsNotKept() {
        QTemporaryDir empty;
        MainWindow window(empty.path());
        editor(window)->setPlainText("# Kept\nwords");
        auto *list = window.findChild<QListWidget *>("scriptList");
        window.findChild<QPushButton *>("newScript")->click();
        window.findChild<QPushButton *>("newScript")->click(); // Twice: still one empty script.
        QTRY_COMPARE(list->count(), 2);
        list->setCurrentRow(list->row(list->findItems("Kept", Qt::MatchExactly).value(0)));
        QCOMPARE(list->count(), 1); // Left empty: gone, as with notes.
        QCOMPARE(window.windowTitle(), QString("Kept — Kalipso"));
    }

    void escOrMinimizingPausesAndNothingResumesByItself() {
        QSettings().setValue("reading/countdown", 0);
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three four five six seven eight");
        window.placeUnderCamera();
        QVERIFY(window.underCamera());
        auto *reader = window.prompter();
        QCOMPARE(reader->findChild<QPushButton *>("readerPlace")->accessibleName(), QString("Expand"));
        auto *mode = reader->findChild<QToolButton *>("readerMode");
        for (auto *action : mode->menu()->actions())
            if (action->text() == "Steady speed")
                action->trigger();
        QCOMPARE(mode->accessibleName(), QString("Reading mode: Steady speed"));
        auto *primary = reader->findChild<QPushButton *>("readerListeningButton");
        primary->click();
        QCOMPARE(primary->accessibleName(), QString("Pause"));
        // Esc pauses.
        QTest::keyClick(reader->text, Qt::Key_Escape);
        QCOMPARE(primary->accessibleName(), QString("Start"));
        // Out of sight, the reader stops.
        primary->click();
        QCOMPARE(primary->accessibleName(), QString("Pause"));
        window.setWindowState(Qt::WindowMinimized);
        QTRY_COMPARE(primary->accessibleName(), QString("Start"));
        QVERIFY(statusLine(window)->text().startsWith("Paused"));
        window.setWindowState(Qt::WindowNoState);
        QTest::qWait(600);
        QCOMPARE(primary->accessibleName(), QString("Start"));
        // Expand: the same window, with its frame, larger; the text is the same.
        reader->findChild<QPushButton *>("readerPlace")->click();
        QVERIFY(!window.underCamera());
        QVERIFY(!(window.windowFlags() & Qt::FramelessWindowHint));
        QCOMPARE(editor(window)->toPlainText(), QString("one two three four five six seven eight"));
    }

    void theReaderSaysWhyListeningDidNotStart() {
        QSettings().setValue("speech/backend", "vosk");
        QSettings().setValue("profiles/vosk/model", scripts_.path() + "/no-such-model");
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three four");
        window.placeUnderCamera();
        auto *reader = window.prompter();
        auto *mode = reader->findChild<QToolButton *>("readerMode");
        for (auto *action : mode->menu()->actions())
            if (action->text() == "Follow my voice")
                action->trigger();
        auto *status = reader->findChild<QLabel *>("readerListeningStatus");
        // Before a language is chosen, the reader says what to do first.
        QCOMPARE(status->text(), QString("Microphone off · no language chosen yet"));
        auto *primary = reader->findChild<QPushButton *>("readerListeningButton");
        if (SpeechCatalog::downloadsSupported()) {
            // The start button is the way there: Settings opens over the reader, at the choice.
            QCOMPARE(primary->accessibleName(), QString("Choose language"));
            primary->click();
            auto *settings = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(settings->isVisible());
            QVERIFY(window.underCamera());
            QCOMPARE(settings->focusWidget(), window.findChild<QComboBox *>("voiceLanguage"));
            settings->close();
            // Space still tries, and says why it cannot.
            QTest::keyClick(reader->text, Qt::Key_Space);
            QVERIFY(status->text().startsWith("Choose the language you speak"));
            settings->close();
        } else {
            QCOMPARE(primary->accessibleName(), QString("Start"));
            primary->click();
            // The reason, not just the idle status.
            QVERIFY(status->text() != "Microphone off · no language chosen yet");
            QCOMPARE(status->text(), statusLine(window)->text());
        }
        // Choosing a reading mode again starts from a clean status.
        for (auto *action : mode->menu()->actions())
            if (action->text() == "Follow my voice")
                action->trigger();
        QCOMPARE(status->text(), QString("Microphone off · no language chosen yet"));
    }

#ifndef KALIPSO_QT_AUDIO
    void settingsListTheMicrophonesAndTestThem() {
        FakePipeWire pipewire;
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}, {"bluez_input.headset", "Headset"}}, "alsa_input.usb");
        pipewire.setSound(FakePipeWire::tone(0.5, 200));
        QSettings().setValue("speech/microphone", "alsa_input.webcam");
        QSettings().setValue("speech/microphoneName", "Webcam C920");
        MainWindow window(scripts_.path());
        window.show();
        auto *settings = openSettings(window);
        auto *microphone = window.findChild<QComboBox *>("microphone");
        QCOMPARE(microphone->count(), 4);
        QCOMPARE(microphone->itemText(0), QString("System microphone · Blue Yeti"));
        QCOMPARE(microphone->itemText(1), QString("Blue Yeti"));
        QCOMPARE(microphone->itemText(2), QString("Headset"));
        // Chosen before and unplugged now: kept, and said.
        QCOMPARE(microphone->currentText(), QString("Webcam C920 · not connected"));
        // Plugged in while Settings is open: there when the list opens.
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}, {"alsa_input.webcam", "Webcam C920"}}, "alsa_input.usb");
        microphone->showPopup();
        microphone->hidePopup();
        QCOMPARE(microphone->currentText(), QString("Webcam C920"));
        QCOMPARE(microphone->count(), 3);
        microphone->setCurrentIndex(0);
        emit microphone->activated(0);
        QCOMPARE(QSettings().value("speech/microphone").toString(), QString());

        auto *test = window.findChild<QPushButton *>("testMicrophone");
        auto *status = window.findChild<QLabel *>("microphoneStatus");
        auto *level = window.findChild<QProgressBar *>("microphoneLevel");
        QVERIFY(status->text().startsWith("The test shows whether Kalipso hears you."));
        QVERIFY(!level->isVisible());
        test->click();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Kalipso hears you through Blue Yeti."), 3000);
        QVERIFY(level->isVisible());
        QVERIFY(level->value() > 500);
        test->click();
        QCOMPARE(status->text(), QString("Test over: Kalipso heard you through Blue Yeti."));
        QVERIFY(!level->isVisible());

        // Negative control: the noise of a quiet room is not a voice.
        pipewire.setSound(FakePipeWire::tone(0.0005, 200));
        test->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Speak: the bar moves"), 3000);
        QTest::qWait(500);
        test->click();
        QVERIFY2(status->text().startsWith("Test over: Kalipso did not hear a voice through Blue Yeti."),
                 qPrintable(status->text()));

        // Closing Settings ends the test and releases the microphone.
        test->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Speak: the bar moves"), 3000);
        settings->close();
        QVERIFY(status->text().startsWith("The test shows whether Kalipso hears you."));
        QCOMPARE(test->text(), QString("Test microphone"));
    }

    void listeningNamesTheMicrophoneAndSaysWhenItIsSilent() {
        FakePipeWire pipewire;
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}}, "alsa_input.usb");
        pipewire.setSound(FakePipeWire::tone(0.5, 200));
        QTemporaryDir model;
        QSettings().setValue("speech/backend", "moonshine");
        QSettings().setValue("profiles/moonshine/library", FAKE_MOONSHINE_LIBRARY);
        QSettings().setValue("profiles/moonshine/model", model.path());
        QSettings().setValue("profiles/moonshine/language", "en");
        MainWindow window(scripts_.path());
        editor(window)->setPlainText(QString("completed ").repeated(400)); // What the fake recognizer hears.
        window.placeUnderCamera();
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Voice);
        auto *status = reader->findChild<QLabel *>("readerListeningStatus");
        reader->findChild<QPushButton *>("readerListeningButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Listening · Blue Yeti · on this device"), 5000);
        // Muted while reading: the controls hide as usual, then come back to say why nothing follows.
        pipewire.setSound(FakePipeWire::tone(0, 200));
        QTRY_VERIFY_WITH_TIMEOUT(!reader->controlsVisible(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("No sound from Blue Yeti."), 6000);
        QVERIFY(reader->controlsVisible());
        pipewire.setSound(FakePipeWire::tone(0.5, 200));
        QTRY_VERIFY_WITH_TIMEOUT(!status->text().startsWith("No sound"), 3000);
        reader->findChild<QPushButton *>("readerListeningButton")->click();
    }

    void anInstalledLanguageFollowsWithoutTheInternet() {
        // Every connection would fail: a dead proxy for the application and for the system.
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 9));
        qputenv("http_proxy", "http://127.0.0.1:9");
        qputenv("https_proxy", "http://127.0.0.1:9");
        const auto restore = qScopeGuard([] {
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
            qunsetenv("http_proxy");
            qunsetenv("https_proxy");
        });
        FakePipeWire pipewire;
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}}, "alsa_input.usb");
        pipewire.setSound(FakePipeWire::tone(0.5, 200));
        QTemporaryDir model;
        QSettings().setValue("speech/backend", "moonshine");
        QSettings().setValue("profiles/moonshine/library", FAKE_MOONSHINE_LIBRARY);
        QSettings().setValue("profiles/moonshine/model", model.path());
        QSettings().setValue("profiles/moonshine/language", "en");
        MainWindow window(scripts_.path());
        editor(window)->setPlainText(QString("completed ").repeated(400)); // What the fake recognizer hears.
        window.placeUnderCamera();
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Voice);
        reader->findChild<QPushButton *>("readerListeningButton")->click();
        // It listens and the reader follows: nothing needs the network once a language is installed.
        QTRY_VERIFY_WITH_TIMEOUT(position(window).startsWith("Word ") && !position(window).startsWith("Word 1 "),
                                 8000);
        reader->findChild<QPushButton *>("readerListeningButton")->click();
    }
#endif

    void aDownloadedLanguageCanBeRemovedOrDownloadedAgain() {
        if (!SpeechCatalog::downloadsSupported())
            QSKIP("No native runtime is catalogued for this platform");
        // The Vosk runtime and the Italian model, where a download leaves them.
        const auto root = SpeechCatalog::writableRoot();
        const auto cleanup = qScopeGuard([root] { QDir(root).removeRecursively(); });
        const auto model = root + "/models/vosk/vosk-model-small-it-0.22";
        const auto install = [&] {
            for (const auto &file :
                 {SpeechCatalog::plan("vosk", "it", root).library, model + "/am/final.mdl", model + "/conf/mfcc.conf"}) {
                QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
                QFile touch(file);
                QVERIFY(touch.open(QIODevice::WriteOnly));
                touch.write(QByteArray(1024, 'x'));
            }
        };
        install();
        MainWindow window(scripts_.path());
        window.show();
        auto *settings = openSettings(window);
        auto *languages = window.findChild<QComboBox *>("voiceLanguage");
        auto *box = window.findChild<QWidget *>("speechInstalledBox");
        auto *status = window.findChild<QLabel *>("speechInstalledStatus");
        QCOMPARE(languages->currentText(), QString("Italiano · ready"));
        // Where it is, how big, and its licence; and the note before any download says where they go.
        QTRY_VERIFY(box->isVisibleTo(settings));
        // Display paths may contain zero-width breaks between Windows folders; the spelling stays intact.
        QVERIFY2(QString(status->text()).remove(QChar(0x200b)).contains(QDir::toNativeSeparators(model)),
                 qPrintable(status->text()));
        QVERIFY(status->text().contains("Apache-2.0"));
        auto *note = window.findChild<QLabel *>("speechDownloadNote");
        QVERIFY(QString(note->text()).remove(QChar(0x200b)).contains(QDir::toNativeSeparators(root)));
        QVERIFY(note->text().contains("MIT") && note->text().contains("Apache 2.0"));
        const auto answer = [](QMessageBox::StandardButton button) {
            QTimer::singleShot(0, [button] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    box->button(button)->click();
            });
        };
        // Asked first: Cancel keeps it.
        answer(QMessageBox::Cancel);
        window.findChild<QPushButton *>("speechRemoveModel")->click();
        QVERIFY(QFileInfo::exists(model));
        // Removed: the files go, the language is no longer in use, and the list offers it again.
        answer(QMessageBox::Yes);
        window.findChild<QPushButton *>("speechRemoveModel")->click();
        QVERIFY(!QFileInfo::exists(model));
        QVERIFY(QFileInfo::exists(SpeechCatalog::plan("vosk", "it", root).library)); // Other languages use it.
        QVERIFY(window.findChild<QLineEdit *>("speechModel")->text().isEmpty());
        QVERIFY(languages->findText("Italiano · download", Qt::MatchStartsWith) > 0);
        QVERIFY2(window.findChild<QLabel *>("speechDownloadStatus")->text().startsWith("Removed Italian:"), qPrintable(window.findChild<QLabel *>("speechDownloadStatus")->text()));
        QTRY_VERIFY(!box->isVisibleTo(settings));
        // Downloading again removes it and fetches it anew (here the network is dead, so it fails
        // and says so, with Try again).
        install();
        // An OS proxy lookup can ignore environment variables (macOS). Use a local unreachable
        // catalog instead, so this test cannot accidentally download from the public internet.
        auto catalog = SpeechCatalog::builtIn();
        auto vosk = catalog.value("vosk").toObject();
        auto runtime = SpeechCatalog::runtimeFor("vosk");
        runtime["url"] = "http://127.0.0.1:9/runtime";
        vosk["runtimes"] = QJsonObject{{speechPlatform(), runtime}};
        auto models = vosk.value("models").toObject();
        auto italian = models.value("it").toObject();
        italian["url"] = "http://127.0.0.1:9/model";
        models["it"] = italian;
        vosk["models"] = models;
        catalog["vosk"] = vosk;
        auto *download = window.findChild<SpeechDownload *>();
        QVERIFY(download);
        download->setCatalog(catalog);
        download->allowLocalHttp();
        languages->setCurrentIndex(languages->findText("Italiano", Qt::MatchStartsWith));
        emit languages->activated(languages->currentIndex());
        QTRY_VERIFY(box->isVisibleTo(settings));
        answer(QMessageBox::Yes);
        window.findChild<QPushButton *>("speechRepairModel")->click();
        QVERIFY(!QFileInfo::exists(model));
        auto *downloadStatus = window.findChild<QLabel *>("speechDownloadStatus");
        QTRY_VERIFY_WITH_TIMEOUT(downloadStatus->text().startsWith("Couldn't download Italian"), 15000);
        QCOMPARE(window.findChild<QPushButton *>("speechDownloadAction")->text(), QString("Try again"));
        // A model installed by hand is never offered for removal, nor removed.
        QTemporaryDir byHand;
        QVERIFY(!SpeechCatalog::downloaded({"vosk", {}, byHand.path(), "it"}));
        QVERIFY(!SpeechCatalog::removeDownloaded({"vosk", {}, byHand.path(), "it"}));
        QVERIFY(QFileInfo::exists(byHand.path()));
    }

    void minimizingOnWaylandIsToldByKWinAndPauses() {
        // On Wayland Qt is not told: Kalipso's KWin script calls SetMinimized over D-Bus.
        QSettings().setValue("reading/countdown", 0);
        MainWindow window(scripts_.path());
        AppBridge bridge;
        window.connectBridge(&bridge);
        editor(window)->setPlainText("one two three four five six seven eight");
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Steady);
        auto *primary = reader->findChild<QPushButton *>("readerListeningButton");
        primary->click();
        QCOMPARE(primary->accessibleName(), QString("Pause"));
        bridge.SetMinimized(false); // Brought back: nothing changes.
        QCOMPARE(primary->accessibleName(), QString("Pause"));
        bridge.SetMinimized(true);
        QCOMPARE(primary->accessibleName(), QString("Start"));
        QVERIFY(statusLine(window)->text().startsWith("Paused while the window was minimized"));
        // Through the session bus, as the KWin script calls it, when there is one.
#ifdef KALIPSO_PORTAL_COLORS
        primary->click();
        if (!bridge.registerOnBus())
            return;
        QDBusInterface app(AppBridge::kService, AppBridge::kPath, "io.github.sansevieria_ou.Kalipso.App");
        QVERIFY(app.isValid());
        QVERIFY(app.call("SetMinimized", true).errorName().isEmpty());
        QTRY_COMPARE(primary->accessibleName(), QString("Start"));
        // The place the person gave the reader under the camera is kept for the next start.
        QVERIFY(app.call("SetReaderPlace", "1500,20").errorName().isEmpty());
        QCOMPARE(QDBusReply<QString>(app.call("ReaderPlace")).value(), QString("1500,20"));
        QVERIFY(app.call("SetReaderPlace", "nonsense").errorName().isEmpty());
        QCOMPARE(QDBusReply<QString>(app.call("ReaderPlace")).value(), QString("1500,20"));
#endif
    }

    void aSecondStartBringsTheFirstForward() {
        MainWindow window(scripts_.path());
        AppBridge bridge;
        const auto key = scripts_.path() + "/second-start";
        QVERIFY(bridge.listen(key));
        window.connectBridge(&bridge);
        window.show();
        window.setWindowState(Qt::WindowMinimized);
        QTRY_VERIFY(window.isMinimized());
        // The second Kalipso asks from its own thread and waits for the answer.
        qputenv("XDG_ACTIVATION_TOKEN", "token-from-the-launcher");
        std::atomic<int> answered{-1};
        std::thread second([&] { answered = AppBridge::activateRunning(key) ? 1 : 0; });
        QTRY_VERIFY(answered >= 0);
        second.join();
        qunsetenv("XDG_ACTIVATION_TOKEN");
        QCOMPARE(answered.load(), 1);
        QTRY_VERIFY(!window.isMinimized());
        QVERIFY(window.isVisible());
        // The launcher's token is handed to the window system with the request.
        QVERIFY(qgetenv("XDG_ACTIVATION_TOKEN") == "token-from-the-launcher" ||
                qgetenv("DESKTOP_STARTUP_ID") == "token-from-the-launcher");
        qunsetenv("DESKTOP_STARTUP_ID");
        qunsetenv("XDG_ACTIVATION_TOKEN");
        // No Kalipso under another name: nothing answers.
        QVERIFY(!AppBridge::activateRunning(key + "-nobody"));
        // "Edit in Kalipso" from the widget: the companion chose a script, then started Kalipso again.
        editor(window)->setPlainText("The first talk");
        const auto first = window.currentScript();
        window.findChild<QPushButton *>("newScript")->click();
        editor(window)->setPlainText("The second talk");
        QTRY_VERIFY(ScriptLibrary(scripts_.path()).contains(first));
        QSettings().setValue("library/current", first);
        QSettings().sync();
        std::thread again([&] { AppBridge::activateRunning(key); });
        QTRY_COMPARE(window.currentScript(), first);
        again.join();
        QCOMPARE(editor(window)->toPlainText(), QString("The first talk"));
    }

    void screenSharingFollowsTheChoiceUnderTheCamera() {
        // The title tells Kalipso's KWin script: a second mark lets screen sharing show the reader.
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("Talk");
        window.placeUnderCamera();
        const auto title = window.windowTitle();
        QCOMPARE(title.at(0), QChar(0x2060));
        auto *exclude = window.findChild<QCheckBox *>("excludeFromCapture");
        if (!exclude) { // Nothing can hide it here: the reader is shareable.
            QCOMPARE(title.at(1), QChar(0x2063));
            return;
        }
        exclude->setChecked(true);
        QVERIFY(window.windowTitle().at(1) != QChar(0x2063));
        exclude->setChecked(false);
        QCOMPARE(window.windowTitle().at(1), QChar(0x2063));
        window.expand();
        QVERIFY(!window.windowTitle().contains(QChar(0x2063))); // Writing: no marks at all.
    }

    void tabLeavesTheScriptAndComesBack() {
        // No keyboard trap: Tab reaches the controls from the script, and Shift+Tab comes back.
        for (const QSize size : {QSize(1100, 760), QSize(620, 400)}) {
            MainWindow window(scripts_.path());
            window.resize(size);
            window.show();
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            auto *text = editor(window);
            text->setPlainText("one two three");
            text->setFocus();
            QTRY_VERIFY(text->hasFocus());
            QTest::keyClick(text, Qt::Key_Tab);
            QVERIFY2(!text->hasFocus(), qPrintable(QString("%1x%2").arg(size.width()).arg(size.height())));
            QVERIFY(text->toPlainText() == "one two three"); // No tab character typed.
            auto *focus = QApplication::focusWidget();
            QVERIFY(qobject_cast<QAbstractButton *>(focus));
            QVERIFY(window.prompter()->isAncestorOf(focus));
            QTest::keyClick(focus, Qt::Key_Backtab);
            QTRY_VERIFY(text->hasFocus());
        }
    }

    void tabGoesTheWayTheScreenReads() {
        // Top bar from left to right, the scripts beside the text, the text, then the bottom bar.
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("A script, so the welcome page does not take the window");
        window.resize(1100, 760);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(window.findChild<QListWidget *>("scriptList")->isVisible());
        auto *reader = window.prompter();
        reader->findChild<QPushButton *>("readerScripts")->setFocus(Qt::TabFocusReason);
        QStringList order;
        for (int i = 0; i < 18; ++i) {
            auto *focus = QApplication::focusWidget();
            order << (focus ? focus->objectName() : QString("(none)"));
            QTest::keyClick(focus, Qt::Key_Tab);
        }
        const QStringList expected{"readerScripts", "formatBoldButton", "formatItalicButton", "formatHeadingButton",
                                   "formatCueButton", "readerTextSmaller", "readerTextLarger"};
        QCOMPARE(order.mid(0, expected.size()), expected);
        const auto at = [&order](const char *name) { return order.indexOf(QString::fromLatin1(name)); };
        QVERIFY2(at("readerPlace") < at("newScript") && at("newScript") < at("scriptList") &&
                     at("scriptList") < at("readingScript") && at("readingScript") < at("readerListeningButton"),
                 qPrintable(order.join(", ")));
    }

    void menusFromTheReaderUseItsColours() {
        MainWindow window(scripts_.path());
        window.prompter()->setReaderTheme("dark");
        auto *mode = window.prompter()->findChild<QToolButton *>("readerMode");
        emit mode->menu()->aboutToShow();
        const QColor menu = mode->menu()->palette().color(QPalette::Window);
        QCOMPARE(menu, QColor(Design::dark.background));
        QMenu more;
        window.prompter()->styleMenu(&more);
        QCOMPARE(more.palette().color(QPalette::Window), QColor(Design::dark.background));
        // The scripts' buttons leave the keys, and the pedals, with the script.
        for (const auto *name : {"newScript", "scriptActions"})
            QCOMPARE(window.findChild<QAbstractButton *>(name)->focusPolicy(), Qt::TabFocus);
        // Escape pauses: the note in Keys and pedals says so.
        bool said = false;
        for (auto *label : window.findChildren<QLabel *>())
            said = said || label->text().contains("Escape always pauses");
        QVERIFY(said);
    }

    void theReaderResizesFromItsEdges() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three four");
        window.placeUnderCamera();
        auto *reader = window.prompter();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.windowFlags() & Qt::FramelessWindowHint); // Without KWin's script, the window itself.
        // Mouse events straight to the reader, at points of its current size: real pointers differ
        // per platform (Windows moves the system cursor, X11 window managers may resize the window).
        const auto hover = [reader](double x, double y) {
            const QPoint point(qRound(x < 0 ? reader->width() + x : x), qRound(y < 0 ? reader->height() + y : y));
            QMouseEvent move(QEvent::MouseMove, point, reader->mapToGlobal(point), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QApplication::sendEvent(reader, &move);
            return reader->cursor().shape();
        };
        QCOMPARE(hover(-2, reader->height() / 2), Qt::SizeHorCursor); // Right edge.
        QCOMPARE(hover(1, reader->height() / 2), Qt::SizeHorCursor);  // Left edge.
        QCOMPARE(hover(reader->width() / 2, -2), Qt::SizeVerCursor);  // Bottom edge.
        QCOMPARE(hover(-2, -2), Qt::SizeFDiagCursor);                 // Bottom right corner.
        QCOMPARE(hover(1, -2), Qt::SizeBDiagCursor);                  // Bottom left corner.
        QCOMPARE(hover(12, reader->height() / 2), Qt::ArrowCursor);   // Beside the text: no resizing.
    }

    void steadyScrollingAdvancesAndStopsAtTheEnd() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three");
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Steady);
        for (int i = 0; i < 30; ++i)
            emit reader->speedRequested(10); // Clamped at 400 words per minute.
        emit reader->toggleRequested();
        QTRY_VERIFY_WITH_TIMEOUT(position(window).startsWith("Word 3 of 3"), 3000);
        QTRY_COMPARE(reader->findChild<QLabel *>("readerListeningStatus")->text(), QString("End of script"));
    }

    void readerControlsFadeWhileReadingAndReturn() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText(QString("word ").repeated(200));
        window.placeUnderCamera();
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Steady);
        auto *status = reader->findChild<QLabel *>("readerListeningStatus");
        emit reader->toggleRequested();
        QVERIFY(status->text().startsWith("2 min left"));
        QVERIFY(reader->controlsVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!reader->controlsVisible(), 4000);
        QTest::keyClick(reader->text, Qt::Key_Plus); // Any key brings the controls back.
        QVERIFY(reader->controlsVisible());
        emit reader->toggleRequested(); // Paused: controls stay.
        QTest::qWait(3000);
        QVERIFY(reader->controlsVisible());
        QCOMPARE(reader->findChild<QPushButton *>("readerListeningButton")->accessibleName(), QString("Start"));
    }

    void readerFontAndLineSpacingFollowSettingsAndEdits() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText(QString("word ").repeated(300));
        QVERIFY(position(window).endsWith(" · about 2 min")); // 300 words at 150 words/min.
        window.placeUnderCamera();
        auto *reader = window.prompter();
        openSettings(window);
        auto *spacing = window.findChild<QComboBox *>("readerLineSpacing");
        auto *font = window.findChild<QComboBox *>("readerFont");
        QVERIFY(spacing && font);
        const auto lineHeight = [reader] { return reader->text->document()->lastBlock().blockFormat().lineHeight(); };
        const int before = reader->text->cursorRect(QTextCursor(reader->text->document()->lastBlock())).height();
        spacing->setCurrentIndex(spacing->findData(150));
        emit spacing->activated(spacing->currentIndex());
        QCOMPARE(lineHeight(), 150.0);
        QCOMPARE(QSettings().value("reading/lineSpacing").toInt(), 150);
        // The script keeps the spacing through edits that add paragraphs, and a whole new text gets it.
        QTextCursor end(editor(window)->document());
        end.movePosition(QTextCursor::End);
        end.insertText("\n\nA typed paragraph");
        QCOMPARE(lineHeight(), 150.0);
        editor(window)->setPlainText(QString("word ").repeated(300) + "\n\nA new paragraph");
        QTRY_COMPARE(lineHeight(), 150.0);
        QVERIFY(reader->text->toPlainText().endsWith("A new paragraph"));
        QVERIFY(before > 0);
        // A chosen font reaches the reader; "Interface font" goes back to the default.
        QVERIFY(font->count() > 1);
        font->setCurrentIndex(1);
        emit font->activated(1);
        QCOMPARE(reader->text->font().family(), font->itemData(1).toString());
        font->setCurrentIndex(0);
        emit font->activated(0);
        QCOMPARE(reader->text->font().family(), QApplication::font().family());
    }

    void eachScriptKeepsItsSpeedAndTheRemoteJumpsBetweenSections() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# One\nalpha beta\n# Two\ngamma delta\n# Three\nepsilon");
        auto *remote = window.findChild<RemoteControl *>();
        QVERIFY(remote);
        // A jump leaves the position just before the section's first word.
        const auto expect = [&window](const char *command, const char *label) {
            emit window.findChild<RemoteControl *>()->command(command);
            return position(window).startsWith(label);
        };
        QVERIFY(expect("section-next", "Word 2 of 5"));     // Before "gamma"
        QVERIFY(expect("section-next", "Word 4 of 5"));     // Before "epsilon"
        QVERIFY(expect("section-next", "Word 4 of 5"));     // No further section
        QVERIFY(expect("section-previous", "Word 2 of 5")); // Back to Two
        QVERIFY(expect("section-previous", "5 word")); // Back to One
        QVERIFY(expect("section-previous", "5 word")); // Stays on the first

        // Each script keeps its own steady speed when switching between them.
        auto *list = window.findChild<QListWidget *>("scriptList");
        auto *speed = window.prompter()->findChild<QLabel *>("readerSpeed");
        const auto first = window.currentScript();
        emit window.prompter()->speedRequested(30);
        const auto firstSpeed = speed->text();
        window.findChild<QPushButton *>("newScript")->click();
        const auto second = window.currentScript();
        QVERIFY(second != first);
        editor(window)->setPlainText("A second talk"); // An empty script would not be kept.
        emit window.prompter()->speedRequested(-60);
        const auto secondSpeed = speed->text();
        QVERIFY(secondSpeed != firstSpeed);
        const auto choose = [list](const QString &id) {
            for (int row = 0; row < list->count(); ++row)
                if (list->item(row)->data(Qt::UserRole).toString() == id)
                    list->setCurrentRow(row);
        };
        choose(first);
        QCOMPARE(window.currentScript(), first);
        QCOMPARE(speed->text(), firstSpeed);
        choose(second);
        QCOMPARE(speed->text(), secondSpeed);
    }

    void aRecognizerThatLearntTheScriptNeedsMoreWordsToJump() {
        MainWindow window(scripts_.path());
        QString script = "alpha beta gamma delta ";
        for (int i = 0; i < 40; ++i)
            script += QString("filler%1 ").arg(i);
        script += "orange violet copper silver golden";
        editor(window)->setPlainText(script);
        auto *speech = window.findChild<SpeechService *>();
        emit speech->listening();
        emit speech->transcript("alpha beta", true);
        const auto before = position(window);
        // Three words further on: enough to skip ahead for a recognizer that hears anything (two), not
        // for one guided by the script (four), which hears script words in what is said off it.
        emit speech->guidedChanged(true);
        emit speech->transcript("orange violet copper", true);
        QCOMPARE(position(window), before);
        emit speech->guidedChanged(false);
        emit speech->transcript("orange violet copper", true);
        QVERIFY2(position(window).startsWith("Word 47 of"), qPrintable(position(window)));
        emit speech->stopped();
    }

    void sessionReportCanBeSavedAfterAVoiceSession() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# Intro\nalpha beta gamma delta epsilon zeta eta\n# Main\niota kappa");
        auto *action = window.findChild<QAction *>("saveSessionReport");
        QVERIFY(action && !action->isEnabled());
        auto *speech = window.findChild<SpeechService *>();
        QVERIFY(speech);
        emit speech->listening();
        emit speech->transcript("alpha beta gamma delta epsilon zeta", true);
        emit speech->stopped();
        QVERIFY(action->isEnabled());
        QTemporaryDir out;
        const auto path = out.filePath("report.md");
        QVERIFY(window.saveSessionReport(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto report = QString::fromUtf8(file.readAll());
        QVERIFY2(report.startsWith("# Session report: Intro\n"), qPrintable(report));
        QVERIFY2(report.contains("- Words read: 6"), qPrintable(report));
        QVERIFY2(report.contains("| Intro |"), qPrintable(report));
    }

    void customKeysCanReplaceBuiltInKeys() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# One\nalpha beta gamma\n# Two\ndelta");
        window.placeUnderCamera();
        auto *reader = window.prompter();
        openSettings(window);
        const auto edit = [&window](const char *id) { return window.findChild<QKeySequenceEdit *>(id); };
        const auto assign = [](QKeySequenceEdit *edit, QKeySequence sequence) {
            edit->setKeySequence(sequence);
            emit edit->editingFinished();
        };
        auto *speed = reader->findChild<QLabel *>("readerSpeed");
        const auto before = speed->text();
        // The up arrow can change speed instead of stepping back.
        assign(edit("key-faster"), QKeySequence(Qt::Key_Up));
        QTest::keyClick(reader->text, Qt::Key_Up);
        QVERIFY(speed->text() != before);
        QVERIFY(position(window).startsWith("4 word"));
        // A pedal key for the next section.
        assign(edit("key-section-next"), QKeySequence(Qt::Key_B));
        QTest::keyClick(reader->text, Qt::Key_B);
        QVERIFY2(position(window).startsWith("Word 3 of 4"), qPrintable(position(window)));
        // A key belongs to one action: giving it to another moves it.
        assign(edit("key-toggle"), QKeySequence(Qt::Key_B));
        QVERIFY(edit("key-section-next")->keySequence().isEmpty());
        QCOMPARE(QSettings().value("keys/toggle").toString(), QString("B"));
        // Only one key per action; built-in keys that were not reassigned still work.
        assign(edit("key-reset"), QKeySequence(Qt::Key_R, Qt::Key_S));
        QCOMPARE(edit("key-reset")->keySequence(), QKeySequence(Qt::Key_R));
        QTest::keyClick(reader->text, Qt::Key_Left);
        QVERIFY(position(window).startsWith("Word 2 of 4"));
        // Clearing a key gives the built-in one back.
        edit("key-faster")->clear();
        QTest::keyClick(reader->text, Qt::Key_Up);
        QVERIFY(position(window).startsWith("Word 1 of 4"));
        QSettings().remove("keys");
    }

    void impressSlidesMoveTheReaderToTheirNotes() {
        auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            python = QStandardPaths::findExecutable("python");
        if (python.isEmpty())
            QSKIP("Needs Python to stand in for LibreOffice");
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# Slide 1: Welcome\n\nThank everyone for coming.\nMention the  two  spaces.\n\n"
                                     "# Slide 3: Results\n\nRevenue grew by twelve percent.\nPause for questions.\n");
        auto *impress = window.findChild<ImpressFollower *>();
        auto *status = statusLine(window);
        QVERIFY(impress && status);
        // The same JSON lines impress_follow.py prints for a real slideshow.
        impress->startHelper(python, {"-c", R"(import json, time
for event in [{"state": "connecting"}, {"state": "waiting"},
              {"state": "slide", "slide": 3, "notes": "Revenue grew by twelve percent.\nPause for questions."}]:
    print(json.dumps(event), flush=True)
time.sleep(0.4)
for event in [{"state": "slide", "slide": 2, "notes": ""},
              {"state": "slide", "slide": 4, "notes": "Not in this script"}, {"state": "closed"}]:
    print(json.dumps(event), flush=True)
    time.sleep(0.2)
)"});
        QTRY_VERIFY_WITH_TIMEOUT(position(window).startsWith("Word 8 of 16"), 5000); // Before "Revenue".
        QCOMPARE(status->text(), QString("Following Impress · slide 3"));
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Impress · slide 2 has no notes."), 5000);
        QVERIFY(position(window).startsWith("Word 8 of 16")); // Stays put.
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Impress · the notes of slide 4 are not in this script."),
                                  5000);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("The presentation was closed. Stopped following Impress."),
                                  5000);
        QTRY_VERIFY_WITH_TIMEOUT(!impress->active(), 5000);
        QCOMPARE(window.findChild<QAction *>("presentWithImpress")->text(), QString("Present with Impress…"));
    }

    // "Connected" only once LibreOffice has the presentation open: before, Kalipso waits for it. A helper
    // that never found the document used to say "Connected… Start the slideshow" and then nothing.
    void impressSaysWhatItWaitsFor() {
        auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            python = QStandardPaths::findExecutable("python");
        if (python.isEmpty())
            QSKIP("Needs Python to stand in for LibreOffice");
        MainWindow window(scripts_.path());
        auto *impress = window.findChild<ImpressFollower *>();
        QVERIFY(impress);
        QSignalSpy said(impress, &ImpressFollower::statusChanged);
        impress->startHelper(python, {"-c", R"(import json, time
for event in [{"state": "connecting"}, {"state": "waiting", "waitingFor": "document"},
              {"state": "waiting", "waitingFor": "slideshow"}]:
    print(json.dumps(event), flush=True)
    time.sleep(0.2)
)"});
        QTRY_VERIFY_WITH_TIMEOUT(said.count() >= 3, 5000);
        QCOMPARE(said.at(0).first().toString(), QString("Connecting to LibreOffice…"));
        QCOMPARE(said.at(1).first().toString(), QString("Waiting for LibreOffice to open the presentation…"));
        QVERIFY(said.at(2).first().toString().startsWith("Connected to Impress"));
        impress->stop();
    }

    // Opt-in (KALIPSO_IMPRESS_E2E=1, with LibreOffice, xdotool and an X11 display): opens the
    // fixture in real Impress and presses F5 and the right arrow there, as a presenter would.
    void realImpressSlideshowIsFollowed() {
        if (qEnvironmentVariable("KALIPSO_IMPRESS_E2E") != "1")
            QSKIP("Set KALIPSO_IMPRESS_E2E=1 to drive a real LibreOffice Impress");
        const auto xdotool = QStandardPaths::findExecutable("xdotool");
        if (xdotool.isEmpty() || ImpressFollower::findOffice().soffice.isEmpty())
            QSKIP("Needs LibreOffice and xdotool");
        QTemporaryDir work;
        const auto path = work.filePath("talk notes.odp");
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/notes.odp"), path));
        MainWindow window(scripts_.path());
        QVERIFY(window.importFile(path));
        auto *impress = window.findChild<ImpressFollower *>();
        auto *status = statusLine(window);
        QSignalSpy said(impress, &ImpressFollower::statusChanged);
        const auto messages = [&said] {
            QStringList all;
            for (const auto &arguments : said)
                all << arguments.first().toString();
            return all.join(" | ");
        };
        QVERIFY(impress->start(path));
        QTRY_VERIFY2_WITH_TIMEOUT(status->text().startsWith("Connected to Impress"), qPrintable(messages()), 90000);
        const auto press = [&xdotool](const QString &key) {
            // windowactivate with a window manager; windowfocus also without one, where nothing has focus.
            QProcess::execute(xdotool, {"search", "--sync", "--name", "talk notes", "windowactivate", "--sync"});
            QProcess::execute(xdotool, {"search", "--name", "talk notes", "windowfocus", "--sync"});
            QProcess::execute(xdotool, {"key", key});
        };
        // LibreOffice's first start opens "Welcome to LibreOffice!" over the presentation, and it takes the
        // keys; close it as a presenter would. Later starts do not show it. windowfocus, not windowactivate,
        // which needs a window manager.
        for (QElapsedTimer waited; !waited.isValid() || waited.elapsed() < 8000; QTest::qWait(250)) {
            if (!waited.isValid())
                waited.start();
            if (QProcess::execute(xdotool, {"search", "--name", "Welcome to LibreOffice", "windowfocus", "--sync",
                                            "key", "Escape"}) == 0)
                break;
        }
        press("F5");
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Following Impress · slide 1"), 30000);
        QVERIFY(position(window).contains(" word")); // Not started: how long it is.
        // A click advances the full-screen slideshow (keyboard focus is unreliable without a window manager).
        const auto advance = [&xdotool] {
            QTest::qWait(2000); // The slideshow reports a slide before its window takes input.
            QProcess::execute(xdotool, {"mousemove", "300", "300", "click", "1"});
        };
        advance();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Impress · slide 2 has no notes."), 30000);
        advance();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Following Impress · slide 3"), 30000);
        QVERIFY2(position(window).startsWith("Word 8 of"), qPrintable(position(window)));
        impress->stop();
        QProcess::execute(xdotool, {"key", "Escape"});
        QProcess::execute("pkill", {"-f", "--", "--accept=pipe,name=kalipso-"}); // The LibreOffice started here.
    }

    void formattingWrapsTheSelectionInMarkdown() {
        MainWindow window(scripts_.path());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window)); // Shortcuts need the active window.
        auto *text = editor(window);
        text->setPlainText("Say hello world now");
        text->setFocus();
        auto cursor = text->textCursor();
        cursor.setPosition(3);
        cursor.setPosition(10, QTextCursor::KeepAnchor); // " hello " with spaces at both edges.
        text->setTextCursor(cursor);
        QTest::keySequence(text, QKeySequence::Bold);
        QCOMPARE(text->toPlainText(), QString("Say **hello** world now"));
        QCOMPARE(text->textCursor().selectedText(), QString("hello")); // Still selected for more formatting.
        window.findChild<QAction *>("formatItalic")->trigger();
        QCOMPARE(text->toPlainText(), QString("Say ***hello*** world now"));
        // An empty selection gets an empty pair with the caret inside.
        cursor.movePosition(QTextCursor::End);
        text->setTextCursor(cursor);
        window.findChild<QAction *>("formatCue")->trigger();
        QTest::keyClicks(text, "smile");
        QCOMPARE(text->toPlainText(), QString("Say ***hello*** world now[smile]"));
        // The heading toggles on the current line.
        QTest::keySequence(text, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
        QCOMPARE(text->toPlainText(), QString("# Say ***hello*** world now[smile]"));
        window.findChild<QAction *>("formatHeading")->trigger();
        QCOMPARE(text->toPlainText(), QString("Say ***hello*** world now[smile]"));
        text->document()->undo(); // One undo step per formatting action.
        QCOMPARE(text->toPlainText(), QString("# Say ***hello*** world now[smile]"));
    }

    // Voice commands were removed (owner, 3 November 2026): a script containing their words could move
    // the reader. Saying those words is now just reading, and an old saved setting has no effect.
    void spokenCommandWordsNeverMoveTheReader() {
        QSettings().setValue("speech/voiceCommands", true); // As an earlier version that had them enabled left it.
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# One\nalpha beta\n# Two\nthen we go back home\n# Three\nend");
        auto *speech = window.findChild<SpeechService *>();
        auto *status = statusLine(window);
        openSettings(window);
        QVERIFY(!window.findChild<QCheckBox *>("voiceCommands"));
        QVERIFY(!QSettings().contains("speech/voiceCommands"));
        for (const auto *said : {"go next", "Go next.", "vai avanti", "torna indietro", "da capo", "go start"}) {
            emit speech->transcript(QString::fromUtf8(said), true);
            QVERIFY2(position(window).startsWith("8 word"), qPrintable(QString::fromUtf8(said) + ": " + position(window)));
            QVERIFY(!status->text().startsWith("Voice command"));
        }
        // The script's own "go back" is read like any other words.
        emit speech->transcript("then we go back", true);
        QVERIFY2(position(window).startsWith("Word 6 of 8"), qPrintable(position(window)));
    }

    void readingStartsAfterFrontMatter() {
        MainWindow window(scripts_.path());
        const QString script = "---\ntitle: Launch\n---\n# Opening\nHello everyone";
        editor(window)->setPlainText(script);
        QCOMPARE(window.prompter()->anchor(), int(script.indexOf("# Opening"))); // Not at the metadata.
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QListWidget *>("scriptList")->currentItem()->text(),
                                  QString("Launch"), 5000); // After autosave.
        QTest::keyClick(window.prompter()->text, Qt::Key_Right);
        QCOMPARE(window.prompter()->anchor(), int(script.indexOf("Hello")));
    }

    void highlighterShowsMarkdownMetadataListsAndEmphasis() {
        QTextDocument document;
        document.documentLayout(); // As in an editor: without a layout, no change signals reach the highlighter.
        ScriptHighlighter highlighter(&document);
        const QColor cue(0x11, 0x22, 0x33), heading(0x44, 0x55, 0x66);
        highlighter.setColors(cue, heading);
        document.setPlainText("---\ntitle: Launch\n---\n- item\n***both*** **bold** *it*\n# Head");
        QTest::qWait(20); // The front matter rehighlight is queued.
        const auto formatAt = [&document](int blockNumber, int position) {
            const auto block = document.findBlockByNumber(blockNumber);
            for (const auto &range : block.layout()->formats())
                if (position >= range.start && position < range.start + range.length)
                    return range.format;
            return QTextCharFormat();
        };
        QCOMPARE(formatAt(1, 0).foreground().color(), cue);                                 // Front matter is dimmed.
        QCOMPARE(formatAt(3, 0).foreground().color(), cue);                                 // "-" list marker.
        QVERIFY(!formatAt(3, 2).hasProperty(QTextFormat::ForegroundBrush));                 // The item text is not.
        QVERIFY(formatAt(4, 3).fontWeight() == QFont::Bold && formatAt(4, 3).fontItalic()); // ***both***
        QCOMPARE(formatAt(4, 9).foreground().color(), cue);                                 // Closing *** markers.
        QVERIFY(formatAt(4, 13).fontWeight() == QFont::Bold && !formatAt(4, 13).fontItalic());
        QVERIFY(formatAt(4, 21).fontItalic());
        QCOMPARE(formatAt(5, 2).foreground().color(), heading);
        QVERIFY(formatAt(5, 0).foreground().color() != QColor(Qt::transparent)); // The editor shows "#".
        // The reader hides the marks for writing: "#" and the emphasis stars, not the list markers.
        highlighter.setHideMarks(true);
        QCOMPARE(formatAt(5, 0).foreground().color(), QColor(Qt::transparent));
        QCOMPARE(formatAt(5, 2).foreground().color(), heading);
        QCOMPARE(formatAt(4, 17).foreground().color(), QColor(Qt::transparent)); // "**" after bold.
        QCOMPARE(formatAt(3, 0).foreground().color(), cue);                      // "-" stays.
        highlighter.setHideMarks(false);
        QCOMPARE(formatAt(5, 0).foreground().color(), heading);
        // Without the closing "---", the first line is plain text again.
        document.setPlainText("---\ntitle: Launch\n- item");
        QTest::qWait(20);
        QVERIFY(!formatAt(1, 0).hasProperty(QTextFormat::ForegroundBrush));
    }

    void readerKeysStepTheScript() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three four");
        window.placeUnderCamera();
        auto *view = window.prompter()->text;
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_PageDown);
        QVERIFY(position(window).startsWith("Word 2 of 4"));
        QTest::keyClick(view, Qt::Key_Left);
        QVERIFY(position(window).startsWith("Word 1 of 4"));
        QTest::keyClick(view, Qt::Key_Home);
        QVERIFY(position(window).startsWith("4 word"));
    }

    void readerControlsKeepKeyboardFocusOnTheScript() {
        // Clicking a control must not let a pedal's Space or arrows activate it or type.
        ReaderPage reader;
        for (auto *button : reader.findChildren<QAbstractButton *>())
            QCOMPARE(button->focusPolicy(), Qt::TabFocus);
    }

    void sectionsJumpAndCuesStayOutOfTheWordCount() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# Opening\nHello everyone [smile]\n\n# Results\nRevenue grew");
        QVERIFY(position(window).startsWith("4 word"));
        auto *sections = window.prompter()->findChild<QToolButton *>("readerSections");
        QVERIFY(!sections->isHidden());
        const auto actions = sections->menu()->actions();
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions[1]->text(), QString("Results"));
        actions[1]->trigger();
        // The last word before the heading is read; "Revenue" is next.
        QVERIFY(position(window).startsWith("Word 2 of 4"));
    }

    void mirroringFlipsTheReaderText() {
        ReaderPage reader;
        reader.setReaderTheme("light");
        reader.text->setPlainText("Mirror");
        reader.resize(600, 300);
        reader.show();
        QTest::qWait(30);
        const auto normal = reader.text->viewport()->grab().toImage();
        reader.setMirror(Mirror::Horizontal);
        const auto mirrored = reader.text->viewport()->grab().toImage();
        QVERIFY(inkCentre(normal) < 0.4);
        QVERIFY(inkCentre(mirrored) > 0.6);
        reader.setMirror(Mirror::None);
        QVERIFY(inkCentre(reader.text->viewport()->grab().toImage()) < 0.4);
    }

    void navigationAndReaderTextScaling() {
        MainWindow window(scripts_.path());
        window.show();
        // Settings is its own window; the main window is the script.
        QVERIFY(!window.findChild<QTabWidget *>());
        auto *settings = openSettings(window);
        QVERIFY(settings && settings->isVisible());
        // The window goes under the camera from its own top bar.
        QCOMPARE(window.prompter()->findChild<QPushButton *>("readerPlace")->accessibleName(),
                 QString("Under the camera"));
        auto *button = window.prompter()->findChild<QPushButton *>("readerListeningButton");
        const double original = button->font().pointSizeF();
        const double scriptSize = window.prompter()->text->font().pointSizeF();
        window.findChild<QComboBox *>("interfaceTextSize")->setCurrentIndex(2);
        QCOMPARE(button->font().pointSizeF(), std::max(8.0, original) * 2);
        QCOMPARE(window.prompter()->text->font().pointSizeF(), scriptSize);
    }

    void toolbarWrapsAtNarrowWidths() {
        QWidget host;
        auto *flow = new FlowLayout(&host);
        for (int i = 0; i < 4; ++i)
            flow->addWidget(new AccessibleButton("Readable control"));
        host.resize(320, 300);
        host.show();
        QTest::qWait(20);
        QVERIFY(flow->heightForWidth(320) > flow->heightForWidth(1000));
        for (int i = 0; i < flow->count(); ++i) {
            const auto rect = flow->itemAt(i)->geometry();
            QVERIFY(rect.left() >= 0);
            QVERIFY(rect.right() < 320);
            QVERIFY(rect.height() >= 24);
        }
    }

    void interfaceTextCanGrowWithoutHorizontalScrolling() {
        MainWindow window(scripts_.path());
        window.resize(640, 760);
        window.show();
        auto *settings = openSettings(window);
        auto *scale = window.findChild<QComboBox *>("interfaceTextSize");
        const double base = QApplication::font().pointSizeF();
        scale->setCurrentIndex(2);
        QTest::qWait(80);
        QVERIFY(scale->font().pointSizeF() >= 16);
        // Twice the size, once: nested controls do not double their parent's doubled size again.
        // (The desktop's style lets fonts inherit; it took scroll bars to 81920 points.)
        for (auto *widget : window.findChildren<QWidget *>())
            QVERIFY2(widget->font().pointSizeF() <= base * 2 * 2 || window.prompter()->text->isAncestorOf(widget),
                     qPrintable(QString("%1 %2: %3 pt")
                                    .arg(widget->metaObject()->className(), widget->objectName())
                                    .arg(widget->font().pointSizeF())));
        scale->setCurrentIndex(1);
        scale->setCurrentIndex(2); // Changing again starts from the same sizes.
        QCOMPARE(scale->font().pointSizeF(), std::max(8.0, base) * 2);
        for (const int width : {640, 320}) {
            window.resize(width, 760);
            settings->resize(width, 700);
            QTest::qWait(100);
            for (auto *area : window.findChildren<QScrollArea *>())
                if (area->isVisible())
                    QVERIFY2(area->horizontalScrollBar()->maximum() == 0,
                             qPrintable(QString("width %1: ").arg(width) + overflowReport(area)));
            // At 320 px every text in the settings can wrap: a single-line label as wide as the
            // window overflows with wider fonts (Linux CI) even when it fits here.
            if (width == 320)
                for (auto *label : settings->findChildren<QLabel *>())
                    if (label->isVisible() && !label->text().isEmpty())
                        QVERIFY2(label->wordWrap(), qPrintable(label->text()));
            // Every visible control fits inside its window.
            for (QWidget *top : {static_cast<QWidget *>(&window), static_cast<QWidget *>(settings)})
                for (auto *control : top->findChildren<QAbstractButton *>())
                    if (control->isVisible() && control->window() == top) {
                        const auto right = control->mapTo(top, QPoint(control->width(), 0)).x();
                        QVERIFY2(right <= top->width(), qPrintable(control->text()));
                    }
        }
        for (auto *form : settings->findChildren<QFormLayout *>()) {
            if (!form->parentWidget()->isVisible())
                continue;
            const auto bounds = form->parentWidget()->contentsRect();
            for (int row = 0; row < form->rowCount(); ++row)
                if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
                    if (auto *label = qobject_cast<QLabel *>(item->widget()); label && label->isVisible()) {
                        QVERIFY(label->geometry().right() <= bounds.right());
                        QVERIFY(label->height() >= label->heightForWidth(label->width()));
                    }
        }
        QScrollArea *settingsScroll = nullptr; // The page on show.
        for (auto *area : settings->findChildren<QScrollArea *>("settingsScroll"))
            if (area->isVisible())
                settingsScroll = area;
        QVERIFY(settingsScroll);
        const int minimum = settingsScroll->widget()->minimumWidth();
        settingsScroll->widget()->setMinimumWidth(900);
        QTest::qWait(30);
        QVERIFY(settingsScroll->horizontalScrollBar()->maximum() > 0); // Detects the nested-overflow regression.
        settingsScroll->widget()->setMinimumWidth(minimum);
        QTRY_COMPARE(settingsScroll->horizontalScrollBar()->maximum(), 0); // And back, before any screenshot.
        if (const auto directory = qEnvironmentVariable("KALIPSO_A11Y_SHOTS"); !directory.isEmpty()) {
            QDir().mkpath(directory);
            window.grab().save(directory + "/qt-editor-320-200.png");
            settings->grab().save(directory + "/qt-settings-320-200.png");
            scale->setCurrentIndex(0);
            window.resize(1100, 760);
            settings->resize(620, 700);
            window.setDemoScript();
            QTest::qWait(80);
            window.grab().save(directory + "/qt-editor.png");
            settings->grab().save(directory + "/qt-settings.png");
            settings->findChild<QScrollArea *>()->ensureWidgetVisible(window.findChild<QWidget *>("key-reset"));
            QTest::qWait(30);
            settings->grab().save(directory + "/qt-settings-keys.png");
            window.placeUnderCamera();
            for (const auto *theme : {"light", "dark"}) {
                window.prompter()->setReaderTheme(theme);
                QTest::qWait(80);
                window.prompter()->grab().save(directory + QString("/qt-reader-%1.png").arg(theme));
            }
            window.prompter()->setMode(ReadingMode::Steady);
            window.prompter()->resize(320, 300);
            QTest::qWait(80);
            window.prompter()->grab().save(directory + "/qt-reader-320.png");
            window.findChild<QComboBox *>("applicationTheme")->setCurrentIndex(1);
            window.resize(1100, 760);
            QTest::qWait(80);
            window.grab().save(directory + "/qt-editor-dark.png");
            settings->grab().save(directory + "/qt-settings-dark.png");
        }
    }

    void editorWrapsLongLinesInsideTheColumn() {
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        editor(window)->setPlainText(QString("A long line of words ").repeated(40));
        for (const int width : {1100, 700, 1400}) {
            window.resize(width, 760);
            QTest::qWait(50);
            QCOMPARE(editor(window)->horizontalScrollBar()->maximum(), 0);
        }
    }

    void readerEditsPreserveReadingPositionAndUndo() {
        MainWindow window(scripts_.path());
        auto *script = editor(window);
        script->setPlainText("uno due tre quattro");
        emit window.prompter()->jumpRequested(8);
        QVERIFY(position(window).startsWith("Word 3 of 4"));
        auto *reader = window.prompter()->text;
        QTextCursor insert(reader->document());
        insert.setPosition(0);
        insert.insertText("prima ");
        QCOMPARE(script->toPlainText(), QString("prima uno due tre quattro"));
        QVERIFY(position(window).startsWith("Word 4 of 5"));
        script->undo();
        QCOMPARE(script->toPlainText(), QString("uno due tre quattro"));
        QCOMPARE(reader->toPlainText(), script->toPlainText());
        QVERIFY(position(window).startsWith("Word 3 of 4"));
    }

    void whileWritingTheCaretIsWhereReadingStarts() {
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QTRY_VERIFY(window.prompter()->editing());
        auto *script = editor(window);
        script->setPlainText("uno due tre quattro");
        setCursor(script, 8); // The caret put on "tre", by the mouse or the arrows.
        QVERIFY(position(window).startsWith("Word 3 of 4"));
        // Text added before it keeps reading on the same word.
        QTextCursor before(script->document());
        before.insertText("prima ");
        QVERIFY(position(window).startsWith("Word 4 of 5"));
        // Anywhere the caret goes, reading follows, also while typing.
        setCursor(script, int(script->toPlainText().indexOf("due")));
        QVERIFY(position(window).startsWith("Word 3 of 5"));
        QTest::keyClicks(script, "e ");
        QCOMPARE(script->toPlainText(), QString("prima uno e due tre quattro"));
        QVERIFY(position(window).startsWith("Word 4 of 6"));
        // The words already read are not dimmed while writing; the caret marks the place.
        QVERIFY(script->extraSelections().isEmpty());
    }

    void demoScriptNeverTouchesTheLibrary() {
        MainWindow window(scripts_.path());
        const auto id = window.currentScript();
        editor(window)->setPlainText("My real script");
        window.setDemoScript();
        QTest::qWait(1000); // Longer than the autosave delay.
        QCOMPARE(ScriptLibrary(scripts_.path()).load(id), QString("My real script"));
    }

    void realScreenSamplingExcludesReaderAndStopsWhenDisabled() {
        if (QGuiApplication::platformName() != "xcb")
            QSKIP("Real X11 framebuffer required; offscreen tests cannot verify pixels behind windows.");
        QWidget background;
        background.setGeometry(100, 100, 400, 400);
        background.setStyleSheet("background:#000000;");
        background.show();
        QWidget reader(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        reader.setGeometry(220, 220, 100, 100);
        reader.setStyleSheet("background:#ffffff;");
        reader.show();
        reader.raise();
        QTest::qWait(100);
        Backdrop sampler(&reader);
        QSignalSpy colors(&sampler, &Backdrop::colorChanged);
        sampler.setEnabled(true);
        QTRY_VERIFY(!colors.isEmpty());
        QCOMPARE(colors.last()[0].value<QColor>(), QColor("#000000"));
        background.setStyleSheet("background:#ffffff;");
        const auto before = colors.size();
        QTRY_VERIFY_WITH_TIMEOUT(colors.size() > before, 4000);
        QCOMPARE(colors.last()[0].value<QColor>(), QColor("#ffffff"));
        sampler.setEnabled(false);
        const auto stopped = colors.size();
        QTest::qWait(2200);
        QCOMPARE(colors.size(), stopped);
    }

    void realWaylandWindowSharingFollowsColorsAndStopsOnHide() {
        if (!qEnvironmentVariableIsSet("KALIPSO_WAYLAND_CAPTURE_TEST") ||
            !QGuiApplication::platformName().startsWith("wayland"))
            QSKIP("Requires a real Wayland compositor, portal and explicit selection of the test source window.");
        QWidget source;
        source.setWindowTitle("Kalipso color test source");
        source.resize(800, 600);
        source.setStyleSheet("background:#000000;");
        source.show();
        MainWindow window(scripts_.path());
        window.setDemoScript();
        window.show();
        auto *sampler = window.prompter()->findChild<Backdrop *>();
        QVERIFY(sampler);
        auto *choose = window.findChild<QPushButton *>("chooseBackgroundWindow");
        QVERIFY(choose);
        QSignalSpy samples(sampler, &Backdrop::colorChanged);
        QTest::qWait(500);
        choose->click();
        QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty(), 120000);
        QCOMPARE(samples.last()[0].value<QColor>(), QColor(Qt::black));
        QCOMPARE(window.prompter()->palette().color(QPalette::Window), QColor(Design::light.background));
        source.setStyleSheet("background:#ffffff;");
        QTRY_COMPARE_WITH_TIMEOUT(samples.last()[0].value<QColor>(), QColor(Qt::white), 12000);
        QTRY_COMPARE_WITH_TIMEOUT(window.prompter()->palette().color(QPalette::Window), QColor(Design::dark.background),
                                  12000);
        window.prompter()->hide();
        const auto count = samples.size();
        source.setStyleSheet("background:#000000;");
        QTest::qWait(3000);
        QCOMPARE(samples.size(), count);
        window.placeUnderCamera();
        QTest::qWait(2500);
        QCOMPARE(samples.size(), count);
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
