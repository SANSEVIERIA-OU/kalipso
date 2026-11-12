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
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkProxy>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolButton>
#include <QTranslator>
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

// Where a control is in the window, in the window's coordinates; an empty rectangle when it is not shown.
QRect placeIn(const QWidget &window, const QWidget *widget) {
    if (!widget || !widget->isVisibleTo(&window))
        return {};
    return QRect(widget->mapTo(&window, QPoint()), widget->size());
}
// Where a check box draws its box (the part a person aims at), in the window's coordinates.
QRect boxIn(const QWidget &window, const QCheckBox *check) {
    QStyleOptionButton option;
    option.initFrom(check);
    const QRect box = check->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, check);
    return QRect(check->mapTo(&window, box.topLeft()), box.size());
}
// The panel's control words in languages with longer ones (French, German, Russian: as the catalogs have them),
// installed before a window is made: its tools row then has to shorten labels to icons at a large text size.
class ControlWords : public QTranslator {
public:
    explicit ControlWords(const QString &language) {
        if (language == "fr")
            words_ = {{"Select", "Sélectionner"}, {"Done", "Terminé"}, {"Import…", "Importer…"}, {"All", "Tout"},
                      {"Actions", "Actions"}};
        else if (language == "de")
            words_ = {{"Select", "Auswählen"}, {"Done", "Fertig"}, {"Import…", "Importieren…"}, {"All", "Alle"},
                      {"Actions", "Aktionen"}};
        else if (language == "ru")
            words_ = {{"Select", "Выбрать"}, {"Done", "Готово"}, {"Import…", "Импорт…"}, {"All", "Все"},
                      {"Actions", "Действия"}};
        else if (language == "longselect") // French, but for a short Import: only the toggle's word is long.
            words_ = {{"Select", "Sélectionner"}, {"Done", "Terminé"}, {"Import…", "Add…"}, {"All", "Tout"},
                      {"Actions", "Actions"}};
        // The titles: as the catalogs have them, and patterns that put the number first, in the middle, and in
        // right-to-left text (only the title's words change in these three).
        if (language == "fr")
            words_.insert("Scripts", "Textes"), words_.insert("Selected: %1", "Sélectionnés : %1");
        else if (language == "de")
            words_.insert("Scripts", "Redetexte"), words_.insert("Selected: %1", "Ausgewählt: %1");
        else if (language == "ru")
            words_.insert("Scripts", "Тексты"), words_.insert("Selected: %1", "Выбрано: %1");
        else if (language == "numberfirst")
            words_.insert("Selected: %1", "%1 textes sélectionnés pour l'export");
        else if (language == "numbermiddle")
            words_.insert("Selected: %1", "Vos %1 textes sont sélectionnés");
        else if (language == "arabic")
            words_.insert("Selected: %1", "النصوص المحددة للتصدير: %1");
    }
    QString word(const char *source) const { return words_.value(source, QString::fromUtf8(source)); }
    QString translate(const char *context, const char *source, const char *, int) const override {
        return QByteArray(context) == "MainWindow" ? words_.value(source) : QString();
    }
    bool isEmpty() const override { return false; }

private:
    QHash<QByteArray, QString> words_;
};
// The first column with ink in a control (the last one in right-to-left text), in the window's coordinates.
int inkEdge(const QWidget &window, QWidget *control, bool rtl) {
    const QImage image = control->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QRgb background = image.pixel(image.width() / 2, 1);
    const auto differs = [&](int x, int y) {
        const QRgb pixel = image.pixel(x, y);
        return std::abs(qRed(pixel) - qRed(background)) + std::abs(qGreen(pixel) - qGreen(background)) +
                   std::abs(qBlue(pixel) - qBlue(background)) >
               90;
    };
    const int origin = control->mapTo(&window, QPoint()).x();
    for (int i = 0; i < image.width(); ++i) {
        const int x = rtl ? image.width() - 1 - i : i;
        for (int y = 0; y < image.height(); ++y)
            if (differs(x, y))
                return origin + x;
    }
    return -1;
}
// The controls of the scripts panel that must not move when selection starts and ends.
struct LibrarySlots {
    QRect header, title, create, close, tools, leading, toggle, list;
    QString toggleText, toggleName, closeName;
    bool toggleChecked = false, toggleIcon = false;
};
LibrarySlots librarySlots(MainWindow &window) {
    const auto find = [&window](const char *name) { return window.findChild<QWidget *>(name); };
    LibrarySlots places;
    places.header = placeIn(window, find("scriptDrawerHeader"));
    places.title = placeIn(window, find("libraryTitle"));
    places.create = placeIn(window, find("newScript"));
    places.close = placeIn(window, find("closeScriptDrawer"));
    places.list = placeIn(window, find("scriptList"));
    if (auto *tools = find("scriptLibraryTools")) {
        places.tools = placeIn(window, tools);
        // The control at the leading edge (left, or right when the text reads right to left).
        for (int i = 0; places.tools.isValid() && i < tools->layout()->count(); ++i)
            if (auto *widget = tools->layout()->itemAt(i)->widget(); widget && widget->isVisibleTo(&window)) {
                places.leading = placeIn(window, widget);
                break;
            }
    }
    if (auto *toggle = qobject_cast<QAbstractButton *>(find("selectScripts"))) {
        places.toggle = placeIn(window, toggle);
        places.toggleText = toggle->text();
        places.toggleName = toggle->accessibleName();
        places.toggleChecked = toggle->isChecked();
        places.toggleIcon = !toggle->icon().isNull();
    }
    if (auto *close = qobject_cast<QAbstractButton *>(find("closeScriptDrawer")))
        places.closeName = close->accessibleName();
    return places;
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
            QCOMPARE(row, 0);                // Reachable before any appearance controls, without scrolling.
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
        QTRY_VERIFY(editor(window)->toPlainText().startsWith("# Welcome"));
        QTRY_COMPARE(list->currentItem()->text(), QString("Welcome"));
        window.findChild<QPushButton *>("newScript")->click();
        QCOMPARE(list->count(), 2);
        QVERIFY(editor(window)->isVisible()); // A second script is not a first run.
        list->setCurrentRow(list->row(list->findItems("Welcome", Qt::MatchExactly).value(0)));
        QTRY_VERIFY(editor(window)->toPlainText().startsWith("# Welcome"));
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

    void scriptSelectionHasExplicitScopeAndEscape() {
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nOriginal");
        const auto second = stored.create("Second talk\nOther");
        const auto third = stored.create("Third talk\nOther");
        QSettings().setValue("library/current", first);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *list = window.findChild<QListWidget *>("scriptList");
        auto *select = window.findChild<QAbstractButton *>("selectScripts");
        auto *all = window.findChild<QCheckBox *>("selectAllScripts");
        select->click();
        QVERIFY(!window.findChild<QAbstractButton *>("selectedScriptActions")->isEnabled());
        const auto checkFor = [&](const QString &id) {
            for (auto *box : list->findChildren<QCheckBox *>("scriptCheck"))
                if (box->property("scriptId").toString() == id)
                    return box;
            return static_cast<QCheckBox *>(nullptr);
        };
        QVERIFY(checkFor(second));
        const auto unchecked = all->grab().toImage();
        checkFor(second)->click();
        QCOMPARE(window.currentScript(), first);
        QCOMPARE(all->checkState(), Qt::PartiallyChecked);
        const auto mixed = all->grab().toImage();
        QVERIFY(unchecked != mixed);
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), QString("Selected: 1"));
        all->click();
        QCOMPARE(all->checkState(), Qt::Checked);
        QVERIFY(mixed != all->grab().toImage());
        QVERIFY(checkFor(first)->isChecked() && checkFor(second)->isChecked() && checkFor(third)->isChecked());
        all->click();
        QCOMPARE(all->checkState(), Qt::Unchecked);
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString() == second)
                list->setCurrentRow(row);
        auto *rowWidget = list->itemWidget(list->currentItem());
        QVERIFY(rowWidget);
        QTest::mouseClick(rowWidget, Qt::LeftButton, Qt::NoModifier, rowWidget->rect().center());
        QVERIFY(checkFor(second)->isChecked());
        QCOMPARE(window.currentScript(), first);
        QTest::mouseClick(rowWidget, Qt::LeftButton, Qt::NoModifier, rowWidget->rect().center());
        QVERIFY(!checkFor(second)->isChecked());
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Space);
        QVERIFY(checkFor(second)->isChecked());
        QCOMPARE(window.currentScript(), first);
        QTest::keySequence(list, QKeySequence::SelectAll);
        QCOMPARE(all->checkState(), Qt::Checked);
        QTest::keyClick(list, Qt::Key_Escape);
        QVERIFY(!select->isChecked());
        QVERIFY(window.prompter()->drawerOpen());
        QCOMPARE(window.currentScript(), first);
        // Clearing and rebuilding dynamic row controls must survive a theme/font change.
        window.findChild<QPushButton *>("newScript")->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.prompter()->setReaderTheme("light");
        window.prompter()->setReaderTheme("dark");
    }

    void smallLibraryKeepsKeyboardFocusOnVisibleControls() {
        ScriptLibrary stored(scripts_.path());
        stored.create("First talk\nWords");
        stored.create("Second talk\nMore words");
        stored.create("Third talk\nWords");
        stored.create("Fourth talk\nMore words");
        MainWindow window(scripts_.path());
        window.resize(320, 240);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *reader = window.prompter();
        reader->setDrawerOpen(true);
        QTest::qWait(300);
        auto *drawer = window.findChild<QWidget *>("scriptDrawer");
        QVERIFY(drawer->isAncestorOf(QApplication::focusWidget()));
        for (const bool selecting : {false, true}) {
            window.findChild<QAbstractButton *>("selectScripts")->setChecked(selecting);
            for (int i = 0; i < 25; ++i) {
                auto *focus = QApplication::focusWidget();
                QVERIFY(focus && drawer->isAncestorOf(focus));
                QVERIFY(focus->visibleRegion().boundingRect().contains(focus->rect()));
                QTest::keyClick(focus, i < 15 ? Qt::Key_Tab : Qt::Key_Backtab);
            }
        }
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QVERIFY(reader->drawerOpen());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QVERIFY(!reader->drawerOpen());
        QTRY_VERIFY(editor(window)->hasFocus());
    }

    // The owner's report: controls moved from side to side depending on where you clicked. Every control of
    // the panel keeps its slot: starting and ending selection only changes what the leading slot, the label
    // of the one toggle and the title say. Large and small windows, large text, and right to left.
    void libraryControlsKeepTheirSlotsWhenSelectionStartsAndEnds_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<double>("scale");
        QTest::addColumn<bool>("rtl");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("importIcon"); // Import is shortened to its icon (the third step).
        QTest::addColumn<bool>("toggleIcon"); // The toggle is shortened to its icon (the fourth).
        QTest::newRow("1100x760") << QSize(1100, 760) << 1.0 << false << "" << false << false;
        QTest::newRow("624x384") << QSize(624, 384) << 1.0 << false << "" << false << false;
        QTest::newRow("320x240") << QSize(320, 240) << 1.0 << false << "" << false << false;
        QTest::newRow("624x384 text 150%") << QSize(624, 384) << 1.5 << false << "" << false << false;
        QTest::newRow("624x384 text 200%") << QSize(624, 384) << 2.0 << false << "" << false << false;
        QTest::newRow("1100x760 text 200%") << QSize(1100, 760) << 2.0 << false << "" << false << false;
        QTest::newRow("320x240 text 200%") << QSize(320, 240) << 2.0 << false << "" << false << false;
        QTest::newRow("1100x760 right to left") << QSize(1100, 760) << 1.0 << true << "" << false << false;
        QTest::newRow("320x240 right to left") << QSize(320, 240) << 1.0 << true << "" << false << false;
        // Long words: at 320x240 and 200% text, Import and the toggle do not both fit, nor do the master box,
        // Actions and the toggle. Russian and German need only Import shortened to its icon (the third step:
        // the toggle keeps its word in both modes); French needs the toggle shortened to its icon too (the
        // fourth, the last to go), and then nothing else has a word.
        QTest::newRow("320x240 text 200% ru") << QSize(320, 240) << 2.0 << false << "ru" << true << false;
        QTest::newRow("320x240 text 200% de") << QSize(320, 240) << 2.0 << false << "de" << true << false;
        QTest::newRow("320x240 text 200% fr") << QSize(320, 240) << 2.0 << false << "fr" << true << true;
        QTest::newRow("320x240 text 200% fr right to left") << QSize(320, 240) << 2.0 << true << "fr" << true << true;
        QTest::newRow("320x240 text 200% ru right to left") << QSize(320, 240) << 2.0 << true << "ru" << true << false;
        // Only the toggle's word is long: the browsing row fits with Import's word and the toggle's, the selecting
        // row does not fit even bare. The toggle goes to its icon, and then Import is an icon too, though its row
        // would fit: nothing shows a word once the toggle's is gone.
        QTest::newRow("320x240 text 200% long toggle word")
            << QSize(320, 240) << 2.0 << false << "longselect" << true << true;
        QTest::newRow("624x384 text 200% fr") << QSize(624, 384) << 2.0 << false << "fr" << false << false;
    }

    void libraryControlsKeepTheirSlotsWhenSelectionStartsAndEnds() {
        QFETCH(QSize, size);
        QFETCH(double, scale);
        QFETCH(bool, rtl);
        QFETCH(QString, language);
        QFETCH(bool, importIcon);
        QFETCH(bool, toggleIcon);
        const auto direction = QGuiApplication::layoutDirection();
        QGuiApplication::setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
        ControlWords words(language);
        if (!language.isEmpty())
            QCoreApplication::installTranslator(&words);
        const auto restore = qScopeGuard([direction, &words] {
            QCoreApplication::removeTranslator(&words);
            QGuiApplication::setLayoutDirection(direction);
        });
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nWords");
        stored.create("Second talk\nMore words");
        stored.create("Third talk with a title that is long enough to need its room\nWords");
        QSettings().setValue("library/current", first);
        QSettings().setValue("appearance/textScale", scale);
        MainWindow window(scripts_.path());
        window.resize(size);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *reader = window.prompter();
        reader->setDrawerOpen(true);
        auto *drawer = window.findChild<QWidget *>("scriptDrawer");
        QTRY_COMPARE(drawer->x(), rtl ? reader->width() - drawer->width() : 0);
        QTest::qWait(250); // The text size is applied by a queued call.
        QCOMPARE(reader->roomy(), size.width() > 960);
        QCOMPARE(window.isRightToLeft(), rtl);

        auto *select = window.findChild<QAbstractButton *>("selectScripts");
        auto *all = window.findChild<QCheckBox *>("selectAllScripts");
        auto *actions = window.findChild<QAbstractButton *>("selectedScriptActions");
        auto *list = window.findChild<QListWidget *>("scriptList");
        QVERIFY(select && all && actions && list);
        const auto rowBox = [&](const QString &id) {
            for (auto *box : list->findChildren<QCheckBox *>("scriptCheck"))
                if (box->property("scriptId").toString() == id)
                    return box;
            return static_cast<QCheckBox *>(nullptr);
        };
        // Every difference is listed, not only the first.
        const auto sameSlots = [rtl](const char *when, const LibrarySlots &a, const LibrarySlots &b) {
            QStringList moved;
            const auto compare = [&moved](const char *what, const QRect &was, const QRect &now) {
                if (was != now)
                    moved << QString("%1 %2 -> %3").arg(what, QDebug::toString(was), QDebug::toString(now));
            };
            compare("header", a.header, b.header);
            compare("title", a.title, b.title);
            compare("New", a.create, b.create);
            compare("close", a.close, b.close);
            compare("tools row", a.tools, b.tools);
            compare("Select/Done", a.toggle, b.toggle);
            compare("list", a.list, b.list);
            // The leading slot starts at the same edge, whatever stands in it.
            if (rtl ? a.leading.right() != b.leading.right() : a.leading.left() != b.leading.left())
                moved << QString("leading slot %1 -> %2").arg(QDebug::toString(a.leading), QDebug::toString(b.leading));
            QVERIFY2(moved.isEmpty(), qPrintable(QString("%1, moved: %2").arg(when, moved.join("; "))));
        };

        auto *import = window.findChild<QAbstractButton *>("importScript");
        const auto selectedTitle = [&words](int count) {
            return QString(words.word("Selected: %1")).arg(count);
        };
        const QString select_ = words.word("Select"), done = words.word("Done"), importWord = words.word("Import…");
        // The tools row never overflows its room (so nothing is cut), in the mode shown.
        const auto fits = [&window] {
            auto *row = window.findChild<QWidget *>("scriptLibraryTools");
            return row->layout()->totalSizeHint().width() <= row->width();
        };
        // A control shows its words or its icon, never both, and the words stay in its names.
        const auto wordsOrIcon = [](QAbstractButton *button) {
            return button->text().isEmpty() == !button->icon().isNull();
        };

        const auto normal = librarySlots(window);
        QVERIFY(normal.tools.isValid());
        QVERIFY(normal.toggle.isValid() && normal.create.isValid() && normal.list.isValid());
        // Shortened to the icons only where the words do not fit (the third and fourth steps).
        QCOMPARE(normal.toggleIcon, toggleIcon);
        QCOMPARE(import->text().isEmpty(), importIcon);
        QVERIFY(wordsOrIcon(select) && wordsOrIcon(import));
        QCOMPARE(select->text(), toggleIcon ? QString() : select_);
        QCOMPARE(import->text(), importIcon ? QString() : importWord);
        QCOMPARE(normal.toggleName, select_);
        QCOMPARE(select->toolTip(), select_);
        QCOMPARE(import->accessibleName(), importWord);
        QVERIFY(import->toolTip().startsWith(importWord));
        // The toggle's word is the last to go: when it has gone, Import's has.
        QVERIFY(!toggleIcon || importIcon);
        QVERIFY2(fits(), "the row is wider than its room while browsing");
        // Import's words or icon start where the master check box below starts, so the slot does not shift.
        const int importStart = inkEdge(window, import, rtl);
        QVERIFY(import->sizeHint().width() <= import->width() && select->sizeHint().width() <= select->width());
        QVERIFY(!normal.toggleChecked);
        // The cross is where an overlay is closed from, and only there.
        QCOMPARE(normal.close.isValid(), !reader->roomy());
        QVERIFY(!normal.closeName.isEmpty());
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), words.word("Scripts"));
        QVERIFY(window.findChild<QWidget *>("importScript")->isVisibleTo(&window));
        QVERIFY(!all->isVisibleTo(&window) && !actions->isVisibleTo(&window));

        select->click();
        QTest::qWait(50);
        const auto selecting = librarySlots(window);
        sameSlots("selection started", normal, selecting);
        // The same word-or-icon state in both modes, and the icon says the new thing.
        QCOMPARE(selecting.toggleText, toggleIcon ? QString() : done);
        QCOMPARE(selecting.toggleName, done);
        QCOMPARE(selecting.toggleIcon, toggleIcon);
        QCOMPARE(select->toolTip(), done);
        QVERIFY(wordsOrIcon(select));
        QVERIFY2(fits(), "the row is wider than its room while selecting");
        QVERIFY(selecting.toggleChecked);
        QCOMPARE(selecting.closeName, normal.closeName); // The cross is Close in both.
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), selectedTitle(0));
        QVERIFY(all->isVisibleTo(&window) && actions->isVisibleTo(&window));
        QVERIFY(!window.findChild<QWidget *>("importScript")->isVisibleTo(&window));
        QVERIFY(!actions->isEnabled());
        // The master box is the head of the column of the rows' boxes, to the pixel or two.
        QVERIFY(rowBox(first)->isVisibleTo(&window));
        const QRect master = boxIn(window, all), row = boxIn(window, rowBox(first));
        QVERIFY2(std::abs(master.center().x() - row.center().x()) <= 2,
                 qPrintable(QString("master box %1, row box %2").arg(QDebug::toString(master), QDebug::toString(row))));
        QCOMPARE(master.size(), row.size());
        QVERIFY(master.bottom() < row.top());
        const int boxEdge = rtl ? master.right() : master.left();
        QVERIFY2(importStart >= 0 && std::abs(importStart - boxEdge) <= 2,
                 qPrintable(QString("Import starts at %1, the master box at %2").arg(importStart).arg(boxEdge)));
        // The title never loses the count, and the menu that opens from Actions says it too.
        QVERIFY(window.findChild<QLabel *>("libraryTitle")->property("shown").toString().contains("0"));
        // Once the toggle has gone to its icon, nothing has a word: the master box's, Actions' and Import's.
        QVERIFY(!toggleIcon || (all->text().isEmpty() && actions->text().isEmpty()));
        // Actions follows the master box and stays clear of the toggle, whichever way the text reads.
        const QRect menu = placeIn(window, actions), toggle = selecting.toggle;
        QVERIFY(!menu.intersects(toggle));
        QVERIFY(selecting.tools.contains(menu));
        QVERIFY(rtl ? menu.right() < placeIn(window, all).left() : menu.left() > placeIn(window, all).right());
        // Labels shorten in order: the master box's before the Actions button's; each keeps its name.
        QVERIFY2(all->text().isEmpty() || !actions->text().isEmpty(), "Actions lost its label while All kept its own");
        QVERIFY(actions->accessibleName().startsWith(words.word("Actions")));
        QCOMPARE(all->accessibleName(), QString("Select all"));
        QVERIFY(select->sizeHint().width() <= select->width());
        QVERIFY(actions->sizeHint().width() <= actions->width());
        // At the default text size the panel keeps every word ("All" among them), even in the narrowest window.
        if (scale == 1.0 && language.isEmpty()) {
            QCOMPARE(all->text(), QString("All"));
            QCOMPARE(actions->text(), QString("Actions"));
            // Actions stands right after the master check box: its frame starts a few pixels from the box.
            const int gap = rtl ? master.left() - menu.right() : menu.left() - master.right();
            QVERIFY2(std::abs(gap) < 70, qPrintable(QString("master %1, Actions %2")
                                                        .arg(QDebug::toString(master), QDebug::toString(menu))));
        }

        // Checking rows, and checking all of them, moves nothing either.
        rowBox(first)->click();
        QVERIFY(window.findChild<QLabel *>("libraryTitle")->property("shown").toString().contains("1"));
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), selectedTitle(1));
        QVERIFY(actions->isEnabled());
        sameSlots("one checked", normal, librarySlots(window));
        all->click();
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), selectedTitle(3));
        sameSlots("all checked", normal, librarySlots(window));

        // Done: the same button, in the same place, says Select again; the cross is where it was.
        select->click();
        QTest::qWait(50);
        QVERIFY(!select->isChecked());
        const auto ended = librarySlots(window);
        sameSlots("selection ended", normal, ended);
        QCOMPARE(ended.toggleText, toggleIcon ? QString() : select_);
        QCOMPARE(ended.toggleName, select_);
        QCOMPARE(ended.toggleIcon, toggleIcon);
        QVERIFY(window.findChild<QWidget *>("importScript")->isVisibleTo(&window));
        QVERIFY(!all->isVisibleTo(&window) && !actions->isVisibleTo(&window));
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), words.word("Scripts"));

        // Escape ends it too, without moving anything. The cross closes the panel in both modes.
        select->click();
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Escape);
        QVERIFY(!select->isChecked());
        sameSlots("escape", normal, librarySlots(window));
        if (!reader->roomy()) {
            select->click();
            QTest::qWait(50);
            auto *close = window.findChild<QAbstractButton *>("closeScriptDrawer");
            QVERIFY(close->isVisibleTo(&window));
            close->click(); // Close, not Cancel: the panel goes, and with it the selection.
            QVERIFY(!reader->drawerOpen());
            QTRY_VERIFY(!select->isChecked()); // When it has slid out.
        }
    }

    void newScriptEndsSelectionThenCreatesAScript() {
        for (const bool shortcut : {false, true}) {
            QDir(scripts_.path()).removeRecursively();
            QDir().mkpath(scripts_.path());
            ScriptLibrary stored(scripts_.path());
            const auto first = stored.create("First talk\nWords");
            const auto second = stored.create("Second talk\nMore words");
            QSettings().setValue("library/current", first);
            MainWindow window(scripts_.path());
            window.resize(1100, 760);
            window.show();
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            auto *list = window.findChild<QListWidget *>("scriptList");
            auto *select = window.findChild<QAbstractButton *>("selectScripts");
            auto *add = window.findChild<QAbstractButton *>("newScript");
            QCOMPARE(list->count(), 2);
            select->click();
            for (auto *box : list->findChildren<QCheckBox *>("scriptCheck"))
                if (box->property("scriptId").toString() == second)
                    box->click();
            QVERIFY(add->isVisibleTo(&window) && add->isEnabled()); // New is there while selecting.
            QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), QString("Selected: 1"));
            if (shortcut)
                QTest::keySequence(editor(window), QKeySequence::New);
            else
                add->click();
            QVERIFY(!select->isChecked()); // Selection ended first, then a script was made.
            QCOMPARE(list->count(), 3);
            QVERIFY(window.currentScript() != first && window.currentScript() != second);
            QVERIFY(editor(window)->toPlainText().isEmpty());
            QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), QString("Scripts"));
            QVERIFY(ScriptLibrary(scripts_.path()).contains(first));
            QVERIFY(ScriptLibrary(scripts_.path()).contains(second)); // Nothing was deleted or changed.
        }
    }

    void bulkMenuContextMenuAndDeleteKeyActOnCheckedScriptsOnly() {
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nKeep this");
        const auto second = stored.create("Second talk\nChange this");
        const auto third = stored.create("Third talk\nChange this too");
        QSettings().setValue("library/current", first);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *list = window.findChild<QListWidget *>("scriptList");
        auto *select = window.findChild<QAbstractButton *>("selectScripts");
        auto *actions = window.findChild<QAbstractButton *>("selectedScriptActions");
        auto *menu = window.findChild<QMenu *>("selectedScriptsMenu");
        QVERIFY(list && select && actions && menu);
        const auto contextMenu = [&] {
            const QPoint point(10, 10);
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
            QApplication::sendEvent(list->viewport(), &event);
        };
        // Outside selection, the list's context menu is not the bulk one.
        contextMenu();
        QVERIFY(!menu->isVisible());
        for (auto *rowMenu : window.findChildren<QMenu *>("scriptRowMenu"))
            rowMenu->close();

        select->click();
        QVERIFY(!actions->isEnabled());
        // Nothing checked: Delete asks nothing and removes nothing; every entry of the menu is unavailable.
        bool asked = false;
        QTimer watch;
        QObject::connect(&watch, &QTimer::timeout, &window, [&asked] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                asked = true;
                dialog->reject();
            }
        });
        watch.start(10);
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Delete);
        QTest::qWait(60);
        QVERIFY(!asked);
        window.findChild<QAction *>("deleteSelectedScripts")->trigger();
        QTest::qWait(60);
        QVERIFY(!asked);
        for (const auto &id : {first, second, third})
            QVERIFY(ScriptLibrary(scripts_.path()).contains(id));
        contextMenu();
        QVERIFY(menu->isVisible());
        for (auto *action : menu->actions())
            QVERIFY2(action->isSeparator() || !action->isEnabled(), qPrintable(action->text()));
        QCOMPARE(menu->findChild<QAction *>("selectedScriptsTitle")->text(), QString("Selected: 0"));
        menu->close();
        watch.stop();

        // Two checked: the context menu and the Actions button open the same menu, naming its target.
        for (auto *box : list->findChildren<QCheckBox *>("scriptCheck"))
            if (box->property("scriptId").toString() != first)
                box->click();
        QVERIFY(actions->isEnabled());
        for (const bool fromButton : {true, false}) {
            if (fromButton)
                actions->click();
            else
                contextMenu();
            QVERIFY(menu->isVisible());
            // The first line names the target and is not a command; the rest are enabled.
            QStringList entries;
            for (auto *action : menu->actions()) {
                const bool title = action->objectName() == "selectedScriptsTitle";
                entries << (action->isSeparator() ? QString("---")
                            : title               ? "[" + action->text() + "]"
                                                  : action->text());
                QVERIFY(action->isSeparator() || action->isEnabled() != title);
            }
            QCOMPARE(entries, (QStringList{"[Selected: 2]", "---", "Reading speed…", "Export…", "---", "Delete…"}));
            menu->close();
        }
        // Delete asks about the two checked scripts; Cancel keeps them, Yes removes only them.
        int answered = 0;
        for (const auto answer : {QMessageBox::Cancel, QMessageBox::Yes}) {
            QTimer::singleShot(50, &window, [answer, &answered] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                ++answered;
                QCOMPARE(dialog->informativeText(), QString("Selected: 2"));
                QVERIFY(dialog->detailedText().contains("Second talk"));
                QVERIFY(dialog->detailedText().contains("Third talk"));
                QVERIFY(!dialog->detailedText().contains("First talk"));
                QCOMPARE(dialog->defaultButton(), dialog->button(QMessageBox::Cancel));
                dialog->button(answer)->click();
            });
            window.activateWindow(); // A closed confirmation does not always give the focus back.
            QVERIFY(QTest::qWaitForWindowActive(&window));
            list->setFocus();
            QTRY_VERIFY(list->hasFocus());
            QTest::keyClick(list, Qt::Key_Delete);
            const ScriptLibrary now(scripts_.path());
            QVERIFY(now.contains(first));
            QCOMPARE(now.contains(second), answer == QMessageBox::Cancel);
            QCOMPARE(now.contains(third), answer == QMessageBox::Cancel);
        }
        QCOMPARE(answered, 2); // Both times the Delete key asked.
        QCOMPARE(window.currentScript(), first);
    }

    // The title, and the first line of the menu Actions opens, shorten their words and never their number,
    // wherever the translation puts it: at the start, in the middle, at the end, and in right-to-left text.
    void titleKeepsItsNumberWhenItsWordsDoNotFit_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("rtl");
        QTest::addColumn<bool>("cut"); // The words are shortened at this size.
        QTest::newRow("fr") << "fr" << false << true;
        QTest::newRow("de") << "de" << false << true;
        QTest::newRow("ru") << "ru" << false << true;
        QTest::newRow("number first") << "numberfirst" << false << true;
        QTest::newRow("number in the middle") << "numbermiddle" << false << true;
        QTest::newRow("right to left") << "arabic" << true << true;
    }

    void titleKeepsItsNumberWhenItsWordsDoNotFit() {
        QFETCH(QString, language);
        QFETCH(bool, rtl);
        QFETCH(bool, cut);
        const auto direction = QGuiApplication::layoutDirection();
        QGuiApplication::setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
        ControlWords words(language);
        QCoreApplication::installTranslator(&words);
        const auto restore = qScopeGuard([direction, &words] {
            QCoreApplication::removeTranslator(&words);
            QGuiApplication::setLayoutDirection(direction);
        });
        ScriptLibrary stored(scripts_.path());
        for (int i = 0; i < 12; ++i)
            stored.create(QString("Talk %1\nWords").arg(i));
        QSettings().setValue("appearance/textScale", 2.0);
        MainWindow window(scripts_.path());
        window.resize(320, 240); // The panel is 288 px wide, the text twice its size: the title has little room.
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.prompter()->setDrawerOpen(true);
        QTest::qWait(500);
        auto *select = window.findChild<QAbstractButton *>("selectScripts");
        select->click();
        window.findChild<QCheckBox *>("selectAllScripts")->click();
        QTest::qWait(50);
        auto *title = window.findChild<QLabel *>("libraryTitle");
        const auto shown = title->property("shown").toString();
        QVERIFY2(title->text().contains("12"), qPrintable(title->text()));
        QVERIFY2(shown.contains("12"), qPrintable(QString("%1 (of %2)").arg(shown, title->text())));
        QCOMPARE(shown != title->text(), cut);
        if (cut)
            QCOMPARE(title->toolTip(), title->text()); // Whole in its tooltip.
        QVERIFY(title->fontMetrics().horizontalAdvance(shown) <= title->width());
        // The menu says it too, and its first line is not a command.
        auto *line = window.findChild<QAction *>("selectedScriptsTitle");
        QVERIFY(line && !line->isEnabled() && !line->isSeparator());
        QVERIFY2(line->text().contains("12"), qPrintable(line->text()));
        QCOMPARE(line->toolTip(), title->text());
    }

    // A menu made when it is asked for has the text size of the panel's own controls, and a title that is no
    // wider than a menu's first line should be (shortened in the middle, whole in its tooltip).
    void rowMenuFollowsTheTextSizeAndShortensItsTitle_data() {
        QTest::addColumn<double>("scale");
        QTest::newRow("100%") << 1.0;
        QTest::newRow("150%") << 1.5;
        QTest::newRow("200%") << 2.0;
    }

    void rowMenuFollowsTheTextSizeAndShortensItsTitle() {
        QFETCH(double, scale);
        const QString longTitle = ScriptLibrary::titleFor(
            "A title so long that no menu should be as wide as it is, said at length, and then some more");
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nWords");
        const auto second = stored.create(longTitle + "\nMore words");
        QSettings().setValue("library/current", first);
        QSettings().setValue("appearance/textScale", scale);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(250);
        QAbstractButton *button = nullptr;
        for (auto *candidate : window.findChildren<QAbstractButton *>("scriptRowActions"))
            if (candidate->property("scriptId").toString() == second)
                button = candidate;
        QVERIFY(button);
        button->click();
        auto *menu = window.findChild<QMenu *>("scriptRowMenu");
        QVERIFY(menu);
        // The same text size as the bulk menu, which the panel scaled with its controls; so, bigger at 200%.
        auto *bulk = window.findChild<QMenu *>("selectedScriptsMenu");
        QVERIFY2(qFuzzyCompare(menu->font().pointSizeF(), bulk->font().pointSizeF()),
                 qPrintable(QString("%1 and %2").arg(menu->font().pointSizeF()).arg(bulk->font().pointSizeF())));
        QVERIFY(qFuzzyCompare(menu->font().pointSizeF(), std::max(8.0, window.font().pointSizeF()) * scale));
        const auto actions = menu->actions();
        QCOMPARE(actions.size(), 6); // Title, separator, Edit, Export…, separator, Delete script….
        QVERIFY(!actions[0]->isEnabled() && !actions[0]->isSeparator() && actions[1]->isSeparator());
        QVERIFY(actions[4]->isSeparator());
        QVERIFY(actions[0]->text() != longTitle);
        QCOMPARE(actions[0]->toolTip(), longTitle);
        QVERIFY(QFontMetrics(menu->font()).horizontalAdvance(actions[0]->text()) <= qRound(240 * scale));
        menu->close();
    }

    void rowMenuTargetsItsOwnScriptAndDeleteCanCancel() {
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nKeep this");
        const auto second = stored.create("Second talk\nDelete this");
        QSettings().setValue("library/current", first);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto rowButton = [&]() {
            for (auto *button : window.findChildren<QAbstractButton *>("scriptRowActions"))
                if (button->property("scriptId").toString() == second)
                    return button;
            return static_cast<QAbstractButton *>(nullptr);
        };
        QVERIFY(rowButton());
        for (const auto answer : {QMessageBox::Cancel, QMessageBox::Yes}) {
            QTest::mouseClick(rowButton(), Qt::LeftButton);
            QCOMPARE(window.currentScript(), first);
            QPointer<QMenu> menu = window.findChild<QMenu *>("scriptRowMenu");
            QVERIFY(menu);
            QTimer::singleShot(50, &window, [answer] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QVERIFY(dialog->text().contains("Second talk"));
                QCOMPARE(dialog->defaultButton(), dialog->button(QMessageBox::Cancel));
                dialog->button(answer)->click();
            });
            menu->findChild<QAction *>("deleteScript")->trigger();
            if (menu)
                menu->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCOMPARE(window.currentScript(), first);
            QCOMPARE(editor(window)->toPlainText(), QString("First talk\nKeep this"));
            QCOMPARE(ScriptLibrary(scripts_.path()).contains(second), answer == QMessageBox::Cancel);
        }
    }

    void exportsUseTheirTargetsAndIncludeUnsavedSelectedText() {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto nativeDialogs =
            qScopeGuard([] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false); });
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nOld text");
        const auto second = stored.create("Second talk\nOther text");
        const auto third = stored.create("Third talk\nUnselected");
        QSettings().setValue("library/current", first);
        QTemporaryDir destination;
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        editor(window)->setPlainText("First talk\nUnsaved edit");
        for (auto *button : window.findChildren<QAbstractButton *>("scriptRowActions"))
            if (button->property("scriptId").toString() == second) {
                button->click();
                break;
            }
        auto *menu = window.findChild<QMenu *>("scriptRowMenu");
        QVERIFY(menu);
        // Report unexpected export errors immediately instead of hanging in a warning dialog.
        QTimer warnings;
        connect(&warnings, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                const auto error = dialog->text();
                dialog->reject();
                QFAIL(qPrintable(error));
            }
        });
        warnings.start(50);
        const auto path = destination.filePath("single.txt");
        QTimer::singleShot(100, &window, [path] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *filename = dialog->findChild<QLineEdit *>("fileNameEdit");
            if (!filename) {
                dialog->reject();
                QFAIL("The non-native file dialog has no filename field");
            }
            filename->setText(path);
            const auto selected = dialog->selectedFiles();
            if (selected != QStringList{path}) {
                dialog->reject();
                QCOMPARE(selected, QStringList{path});
            }
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        menu->findChild<QAction *>("exportScript")->trigger();
        QFile single(path);
        QVERIFY2(single.open(QIODevice::ReadOnly), qPrintable(QDir(destination.path()).entryList().join(", ")));
        QCOMPARE(single.readAll(), QByteArray("Second talk\nOther text"));
        QCOMPARE(window.currentScript(), first);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.findChild<QAbstractButton *>("selectScripts")->click();
        for (auto *box : window.findChildren<QCheckBox *>("scriptCheck"))
            if (box->property("scriptId").toString() != third)
                box->click();
        QTimer::singleShot(100, &window, [&destination] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setDirectory(destination.path());
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        window.findChild<QAction *>("exportSelectedScripts")->trigger();
        QFile firstExport(destination.filePath("First talk.txt"));
        QVERIFY(firstExport.open(QIODevice::ReadOnly));
        QCOMPARE(firstExport.readAll(), QByteArray("First talk\nUnsaved edit"));
        QFile secondExport(destination.filePath("Second talk.txt"));
        QVERIFY(secondExport.open(QIODevice::ReadOnly));
        QCOMPARE(secondExport.readAll(), QByteArray("Second talk\nOther text"));
        QVERIFY(!QFile::exists(destination.filePath("Third talk.txt")));
        QCOMPARE(window.currentScript(), first);
    }

    void batchSpeedUpdatesTheOpenScriptAndDeleteAllReturnsToEmptyLibrary() {
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nWords");
        const auto second = stored.create("Second talk\nMore words");
        QSettings().setValue("library/current", first);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QAbstractButton *>("selectScripts")->click();
        for (auto *box : window.findChildren<QCheckBox *>("scriptCheck"))
            if (box->property("scriptId").toString() == first)
                box->click();
        for (const bool accept : {false, true}) {
            QTimer::singleShot(50, &window, [accept] {
                auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->setIntValue(240);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            window.findChild<QAction *>("selectedScriptsSpeed")->trigger();
            QCOMPARE(ScriptLibrary(scripts_.path()).speed(first), accept ? 240 : 0);
            QCOMPARE(ScriptLibrary(scripts_.path()).speed(second), 0);
        }
        auto *speed = window.findChild<QLabel *>("readerSpeed");
        QVERIFY(speed);
        QVERIFY(speed->text().contains("240"));
        window.findChild<QAbstractButton *>("selectScripts")->setChecked(false);
        auto *list = window.findChild<QListWidget *>("scriptList");
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString() == second)
                list->setCurrentRow(row);
        QTRY_COMPARE(window.currentScript(), second);
        QVERIFY(speed->text().contains("150")); // An unselected default-speed script keeps the general pace.
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString() == first)
                list->setCurrentRow(row);
        QTRY_COMPARE(window.currentScript(), first);
        QVERIFY(speed->text().contains("240"));
        window.findChild<QAbstractButton *>("selectScripts")->setChecked(true);
        window.findChild<QCheckBox *>("selectAllScripts")->click();
        QTimer::singleShot(50, &window, [] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->button(QMessageBox::Yes)->click();
        });
        editor(window)->setFocus(); // Native confirmations can restore focus here on Windows.
        window.findChild<QAction *>("deleteSelectedScripts")->trigger();
        const ScriptLibrary after(scripts_.path());
        QVERIFY(!after.contains(first));
        QVERIFY(!after.contains(second));
        QVERIFY(editor(window)->toPlainText().isEmpty());
        QVERIFY(!window.findChild<QAbstractButton *>("selectScripts")->isChecked());
        QVERIFY(window.prompter()->welcomeShown());
    }

    void batchSpeedAndDeletionOnlyAffectCheckedScripts() {
        ScriptLibrary stored(scripts_.path());
        const auto first = stored.create("First talk\nKeep this");
        const auto second = stored.create("Second talk\nChange this");
        const auto third = stored.create("Third talk\nChange this too");
        QSettings().setValue("library/current", first);
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QAbstractButton *>("selectScripts")->click();
        for (auto *box : window.findChildren<QCheckBox *>("scriptCheck"))
            if (box->property("scriptId").toString() != first)
                box->click();
        QTimer::singleShot(50, &window, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setIntValue(230);
            dialog->accept();
        });
        window.findChild<QAction *>("selectedScriptsSpeed")->trigger();
        ScriptLibrary afterSpeed(scripts_.path());
        QCOMPARE(afterSpeed.speed(first), 0);
        QCOMPARE(afterSpeed.speed(second), 230);
        QCOMPARE(afterSpeed.speed(third), 230);
        QCOMPARE(window.currentScript(), first);
        QTimer::singleShot(50, &window, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const int initial = dialog->intValue();
            dialog->reject();
            QCOMPARE(initial, 230); // Uses the selected scripts' pace while another script is open.
        });
        window.findChild<QAction *>("selectedScriptsSpeed")->trigger();
        QTimer::singleShot(50, &window, [] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(dialog->informativeText(), QString("Selected: 2"));
            QVERIFY(!dialog->detailedText().contains("First talk"));
            dialog->button(QMessageBox::Yes)->click();
        });
        window.findChild<QAction *>("deleteSelectedScripts")->trigger();
        ScriptLibrary afterDelete(scripts_.path());
        QVERIFY(afterDelete.contains(first));
        QVERIFY(!afterDelete.contains(second));
        QVERIFY(!afterDelete.contains(third));
        QCOMPARE(window.currentScript(), first);
        QCOMPARE(window.findChild<QLabel *>("libraryTitle")->text(), QString("Selected: 0"));
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
        QTRY_COMPARE(window.currentScript(), first);
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
                         std::abs(seen.blue() - expected.blue()) <=
                     12,
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
        QTRY_COMPARE(list->count(), 1); // Left empty: gone, as with notes.
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
        QTRY_VERIFY_WITH_TIMEOUT(position(window).startsWith("Word ") && !position(window).startsWith("Word 1 "), 8000);
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
            for (const auto &file : {SpeechCatalog::plan("vosk", "it", root).library, model + "/am/final.mdl",
                                     model + "/conf/mfcc.conf"}) {
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
        QVERIFY2(window.findChild<QLabel *>("speechDownloadStatus")->text().startsWith("Removed Italian:"),
                 qPrintable(window.findChild<QLabel *>("speechDownloadStatus")->text()));
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
        for (int i = 0; i < 40; ++i) {
            auto *focus = QApplication::focusWidget();
            order << (focus ? focus->objectName() : QString("(none)"));
            QTest::keyClick(focus, Qt::Key_Tab);
        }
        const QStringList expected{"readerScripts",   "formatBoldButton",  "formatItalicButton", "formatHeadingButton",
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
        for (const auto *name : {"newScript", "importScript", "readerActions"})
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
        QVERIFY(expect("section-previous", "5 word"));      // Back to One
        QVERIFY(expect("section-previous", "5 word"));      // Stays on the first

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
        QTRY_COMPARE(window.currentScript(), first);
        QCOMPARE(speed->text(), firstSpeed);
        choose(second);
        QTRY_COMPARE(speed->text(), secondSpeed);
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
            QVERIFY2(position(window).startsWith("8 word"),
                     qPrintable(QString::fromUtf8(said) + ": " + position(window)));
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
