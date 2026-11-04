// SPDX-License-Identifier: AGPL-3.0-or-later
// Every screen in every combination: the one window large (writing) and small (the teleprompter),
// the scripts open and closed, each reading mode idle and running, light and dark, interface text at 100% and 200%,
// wide and narrow, and every Settings page. Each state is checked for what breaks a layout (controls overlapping or
// outside their bar, labels cut, bars changing height or moving with the mode, the current line under a bar, anything
// wider than the window) and, with a directory argument, photographed for a person to look at.
//
//   layout_matrix [directory]     exit status: the number of problems found
//
// Run it under the desktop being checked (Breeze light, Breeze dark, the drawn style).
#include "accessibility.h"
#include "interface_language.h"
#include "reader.h"
#include "window.h"
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolButton>
#include <QTranslator>
#include <QWindow>

namespace {
QStringList problems;
QString outDir;
int shots = 0;

void settle() {
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents();
        QTest::qWait(20);
    }
}

void shoot(QWidget *widget, const QString &name) {
    if (outDir.isEmpty())
        return;
    widget->grab().save(QDir(outDir).filePath(QString("%1-%2.png").arg(++shots, 3, 10, QChar('0')).arg(name)));
}

void problem(const QString &state, const QString &what) { problems << state + ": " + what; }

QString describe(const QWidget *widget) {
    QString text;
    if (auto *button = qobject_cast<const QAbstractButton *>(widget))
        text = button->text();
    else if (auto *label = qobject_cast<const QLabel *>(widget))
        text = label->text().left(40);
    return QString("%1 %2 \"%3\"").arg(widget->metaObject()->className(), widget->objectName(), text);
}

bool shown(const QWidget *widget, const QWidget *root) { return widget->isVisibleTo(root) && widget->isVisible(); }

// A bar of the reader: its controls inside it, apart from each other, their labels whole.
void checkBar(const QString &state, QWidget *bar) {
    QList<QWidget *> items;
    for (int i = 0; i < bar->layout()->count(); ++i)
        if (auto *widget = bar->layout()->itemAt(i)->widget(); widget && widget->isVisible())
            items << widget;
    for (auto *widget : items) {
        if (!bar->rect().contains(widget->geometry()))
            problem(state, QString("%1 outside its bar (%2 in %3x%4)")
                               .arg(describe(widget))
                               .arg(QDebug::toString(widget->geometry()))
                               .arg(bar->width())
                               .arg(bar->height()));
        if (auto *button = qobject_cast<QAbstractButton *>(widget);
            button && !button->text().isEmpty() && button->sizeHint().width() > button->width() + 1)
            problem(state, QString("%1 label cut (needs %2, has %3)")
                               .arg(describe(button))
                               .arg(button->sizeHint().width())
                               .arg(button->width()));
    }
    for (int i = 0; i < items.size(); ++i)
        for (int j = i + 1; j < items.size(); ++j)
            if (items[i]->geometry().intersects(items[j]->geometry()))
                problem(state, QString("%1 overlaps %2").arg(describe(items[i]), describe(items[j])));
}

// Anything wider than the window, labels cut at the bottom, push buttons cut.
void checkWindow(const QString &state, QWidget *root) {
    for (auto *widget : root->findChildren<QWidget *>()) {
        if (!shown(widget, root) || widget->isWindow())
            continue;
        // Inside a scroll area what scrolls is not cut; the area itself must not scroll sideways.
        bool scrolled = false;
        for (auto *parent = widget->parentWidget(); parent && parent != root; parent = parent->parentWidget())
            if (qobject_cast<QAbstractScrollArea *>(parent) && parent != widget)
                scrolled = scrolled || qobject_cast<QScrollArea *>(parent);
        const QRect area(widget->mapTo(root, QPoint(0, 0)), widget->size());
        if (!scrolled && area.right() > root->width() + 1)
            problem(state, QString("%1 wider than the window (right %2, window %3)")
                               .arg(describe(widget))
                               .arg(area.right())
                               .arg(root->width()));
        if (auto *scroll = qobject_cast<QScrollArea *>(widget);
            scroll && scroll->horizontalScrollBar()->maximum() > 0 &&
            scroll->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff)
            problem(state, describe(widget) + " scrolls sideways");
        if (auto *label = qobject_cast<QLabel *>(widget); label && label->wordWrap() && !label->text().isEmpty() &&
                                                          label->heightForWidth(label->width()) > label->height() + 1)
            problem(state, describe(label) + " cut at the bottom");
        // A field's label ("Start or" / "pause") that breaks over lines although its row has room for
        // it on one: it reads as two things.
        if (auto *label = qobject_cast<QLabel *>(widget); label && label->buddy() && !label->text().isEmpty()) {
            const int oneLine = label->fontMetrics().horizontalAdvance(label->text());
            const int room = label->isRightToLeft()
                                 ? label->geometry().right() - label->parentWidget()->contentsRect().left()
                                 : label->parentWidget()->contentsRect().right() - label->geometry().left();
            // Shaping can select a taller fallback font (Arabic/Indic): height alone is not a line count.
            QTextLayout shaped(label->text(), label->font());
            QTextOption option;
            option.setWrapMode(QTextOption::WordWrap);
            shaped.setTextOption(option);
            shaped.beginLayout();
            int lines = 0;
            while (true) {
                auto line = shaped.createLine();
                if (!line.isValid())
                    break;
                line.setLineWidth(label->wordWrap() ? label->width() : 100000);
                ++lines;
            }
            shaped.endLayout();
            if (lines > 1 && oneLine + 4 <= room)
                problem(state, QString("%1 breaks over lines (needs %2 on one, has %3 in its row, is %4 wide)")
                                   .arg(describe(label))
                                   .arg(oneLine)
                                   .arg(room)
                                   .arg(label->width()));
        }
        // A button cut: too narrow for its one-line label, or (buttons whose label wraps) too low.
        if (auto *button = qobject_cast<QPushButton *>(widget); button && !button->text().isEmpty()) {
            const bool wraps = button->sizePolicy().hasHeightForWidth();
            if (wraps ? button->heightForWidth(button->width()) > button->height() + 1
                      : button->sizeHint().width() > button->width() + 1 &&
                            button->fontMetrics().horizontalAdvance(button->text()) > button->width() - 8)
                problem(state, describe(button) + " label cut");
        }
    }
}

// A word cut between two lines ("Benven" / "uti"): the column is too narrow for it.
void checkWords(const QString &state, QTextEdit *edit) {
    if (!edit->isVisible())
        return;
    for (auto block = edit->document()->begin(); block.isValid(); block = block.next()) {
        auto *layout = block.layout();
        const auto text = block.text();
        for (int i = 0; layout && i + 1 < layout->lineCount(); ++i) {
            const auto line = layout->lineAt(i), next = layout->lineAt(i + 1);
            const int end = line.textStart() + line.textLength() - 1;
            if (end >= 0 && end < text.size() && next.textStart() < text.size() && text.at(end).isLetterOrNumber() &&
                text.at(next.textStart()).isLetterOrNumber())
                problem(state, QString("%1 breaks \"%2\" inside a word")
                                   .arg(edit->objectName(), text.mid(std::max(0, end - 8), 16)));
        }
    }
}

// Two scroll bars side by side: a scrolling area inside another one that scrolls too.
void checkScrollBars(const QString &state, QWidget *root) {
    for (auto *area : root->findChildren<QAbstractScrollArea *>()) {
        if (!area->isVisible() || !area->verticalScrollBar()->isVisible() || area->verticalScrollBar()->maximum() == 0)
            continue;
        for (auto *parent = area->parentWidget(); parent && parent != root->parentWidget();
             parent = parent->parentWidget())
            if (auto *outer = qobject_cast<QAbstractScrollArea *>(parent);
                outer && outer->verticalScrollBar()->isVisible() && outer->verticalScrollBar()->maximum() > 0)
                problem(state, QString("two scroll bars: %1 inside %2").arg(describe(area), describe(outer)));
    }
}

const char *modeName(ReadingMode mode) {
    return mode == ReadingMode::Manual ? "manual" : mode == ReadingMode::Steady ? "steady" : "voice";
}

// The one window at every size: large (writing, with the formats and the scripts beside the text)
// and small (the teleprompter), in each mode, idle and reading.
void readerStates(MainWindow &window, const QString &prefix) {
    auto *reader = window.prompter();
    for (const QSize size :
         {QSize(1280, 800), QSize(1100, 760), QSize(960, 600), QSize(624, 384), QSize(480, 300), QSize(320, 240)}) {
        window.resize(size);
        settle();
        QHash<QString, QList<QRect>> topBar;
        QHash<QString, int> heights;
        // Large enough to write in: wider than 960 and taller than 600 (written here, not asked of the
        // window, so a wrong rule in the window is caught).
        const bool roomy = size.width() > 960 && size.height() > 600;
        for (const auto mode : {ReadingMode::Manual, ReadingMode::Steady, ReadingMode::Voice}) {
            emit reader->modeRequested(mode);
            for (const bool running : {false, true}) {
                if (running)
                    reader->setRunning(true, mode == ReadingMode::Voice
                                                 ? QObject::tr("Listening · System microphone · on this device")
                                                 : QStringLiteral("3 min left"));
                else
                    reader->setRunning(false, reader->findChild<QLabel *>("readerListeningStatus")->text());
                reader->revealControls();
                settle();
                const auto state = QString("%1 %2 %3x%4 %5%6")
                                       .arg(prefix, roomy ? "large" : "small")
                                       .arg(size.width())
                                       .arg(size.height())
                                       .arg(modeName(mode), running ? " running" : "");
                auto *top = reader->findChild<QWidget *>("readerBar");
                auto *bottom = reader->findChild<QWidget *>("readerControls");
                checkBar(state, top);
                checkBar(state, bottom);
                // Writing only in a large window that is not reading; the formats only in a large one.
                if (reader->editing() != (roomy && !running))
                    problem(state, QString("writing is %1").arg(reader->editing() ? "on" : "off"));
                if (reader->text->isReadOnly() == reader->editing())
                    problem(state, "the text's read-only state does not follow writing");
                if (auto *formats = reader->findChild<QWidget *>("readerFormats"); formats->isVisible() != roomy)
                    problem(state, QString("the formats are %1").arg(formats->isVisible() ? "shown" : "hidden"));
                // Nothing in the top bar moves with the mode; neither bar changes height.
                QList<QRect> places;
                for (int i = 0; i < top->layout()->count(); ++i)
                    if (auto *widget = top->layout()->itemAt(i)->widget())
                        places << (widget->isVisible() ? widget->geometry() : QRect());
                const auto key = QString::number(running);
                if (!topBar.contains(key))
                    topBar[key] = places;
                else if (topBar[key] != places)
                    problem(state, "the top bar moved with the mode");
                for (auto *bar : {top, bottom}) {
                    const auto name = bar->objectName();
                    if (!heights.contains(name))
                        heights[name] = bar->height();
                    else if (heights[name] != bar->height())
                        problem(state,
                                QString("%1 changed height: %2 → %3").arg(name).arg(heights[name]).arg(bar->height()));
                }
                const QRect view =
                    reader->text->viewport()->geometry().translated(reader->text->mapTo(reader, QPoint()));
                if (reader->editing()) {
                    // Writing: the text between the bars, never under them, so the caret is never hidden.
                    if (view.top() < top->geometry().bottom() || view.bottom() > bottom->geometry().top())
                        problem(state,
                                QString("the text (%1) goes under a bar while writing").arg(QDebug::toString(view)));
                } else {
                    // Reading: the current line between the bars.
                    QTextCursor anchor(reader->text->document());
                    anchor.setPosition(std::clamp(reader->anchor(), 0, reader->text->document()->characterCount() - 1));
                    const QRect line =
                        reader->text->cursorRect(anchor).translated(reader->text->viewport()->mapTo(reader, QPoint()));
                    if (line.top() < top->geometry().bottom() || line.bottom() > bottom->geometry().top())
                        problem(state, QString("the current line (%1) is under a bar").arg(QDebug::toString(line)));
                }
                // The scripts beside the text in a large window: the text starts after them.
                if (auto *drawer = reader->findChild<QWidget *>("scriptDrawer"); drawer && drawer->isVisible()) {
                    checkBar(state + " scripts", drawer->findChild<QWidget *>("scriptDrawerHeader"));
                    if (roomy && (reader->isRightToLeft() ? view.right() > drawer->geometry().left()
                                                          : view.left() < drawer->geometry().right()))
                        problem(state, "the text starts under the scripts");
                }
                checkWindow(state, &window);
                checkWords(state, reader->text);
                shoot(&window, QString(state).replace(' ', '-'));
            }
        }
        reader->setRunning(false, QString());
        // The scripts: closed in a small window until asked for, then over the text.
        if (!roomy) {
            reader->setDrawerOpen(true);
            settle();
            QTest::qWait(300); // The panel slides in.
            const auto state = QString("%1 small %2x%3 scripts").arg(prefix).arg(size.width()).arg(size.height());
            auto *drawer = reader->findChild<QWidget *>("scriptDrawer");
            if (!drawer->isVisible() ||
                drawer->x() != (reader->isRightToLeft() ? reader->width() - drawer->width() : 0))
                problem(state, "the scripts did not open");
            checkBar(state, drawer->findChild<QWidget *>("scriptDrawerHeader"));
            checkWindow(state, &window);
            shoot(&window, QString(state).replace(' ', '-'));
            reader->setDrawerOpen(false);
            QTest::qWait(300);
            settle();
        }
    }
    // A long status and an error must not change the bars either.
    window.resize(624, 384);
    emit reader->modeRequested(ReadingMode::Voice);
    reader->setStatus(
        QStringLiteral("No sound from Blue Yeti. Check that it is not muted, or choose another microphone in "
                       "Settings"));
    settle();
    checkBar(prefix + " reader long status", reader->findChild<QWidget *>("readerControls"));
    shoot(&window, prefix + "-reader-long-status");
}

void settingsStates(MainWindow &window, const QString &prefix) {
    auto *dialog = window.findChild<QDialog *>("settingsDialog");
    auto *nav = dialog->findChild<QListWidget *>();
    dialog->show();
    for (const QSize size : {QSize(860, 640), QSize(560, 600), QSize(360, 640)}) {
        dialog->resize(size);
        settle();
        auto *picker = dialog->findChild<QComboBox *>("settingsPagePicker");
        const int pages = nav ? nav->count() : 0;
        for (int page = 0; page < pages; ++page) {
            nav->setCurrentRow(page);
            if (picker && picker->isVisible()) {
                picker->setCurrentIndex(page);
                emit picker->activated(page);
            }
            settle();
            const auto state =
                QString("%1 settings %2x%3 page %4").arg(prefix).arg(size.width()).arg(size.height()).arg(page);
            checkWindow(state, dialog);
            checkScrollBars(state, dialog);
            // The page names in the list are never cut ("Appeara…").
            if (auto *pages = dialog->findChild<QListWidget *>("settingsPages"); pages && pages->isVisible())
                for (int row = 0; row < pages->count(); ++row) {
                    const auto *item = pages->item(row);
                    const int needs = pages->fontMetrics().horizontalAdvance(item->text()) +
                                      (item->icon().isNull() ? 0 : pages->iconSize().width() + 8);
                    if (needs > pages->visualItemRect(item).width() - 8)
                        problem(state, QString("page name cut: %1 needs %2, has %3")
                                           .arg(item->text())
                                           .arg(needs)
                                           .arg(pages->visualItemRect(item).width()));
                }
            shoot(dialog, QString(state).replace(' ', '-'));
        }
    }
    dialog->hide();
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("kalipso-layout-matrix");
    app.setOrganizationName("sansevieria-ou");
    app.setDesktopFileName("io.github.sansevieria_ou.kalipso");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir configuration;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configuration.path());
    // The interface in the system's language, from the translations built next to this program.
    const auto language = InterfaceLanguages::select();
    QLocale::setDefault(QLocale(language.locale));
    app.setLayoutDirection(language.rtl ? Qt::RightToLeft : Qt::LeftToRight);
    Accessibility::useInterfaceFont(QString::fromLatin1(language.code));
    InterfaceLanguages::Translator translator;
    if (translator.loadLanguage(language, "kalipso", QStringLiteral(KALIPSO_BINARY_DIR)))
        app.installTranslator(&translator);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!outDir.isEmpty())
        QDir().mkpath(outDir);
    QTemporaryDir library;
    const QString script = QStringLiteral(
        "# Benvenuti\n\nBuongiorno a tutti e grazie di essere qui. [sorridi] Oggi vi racconto come prepariamo la "
        "nuova stagione del teatro, e perché abbiamo scelto di cominciare dalle scuole.\n\n# Il progetto\n\n"
        "Abbiamo lavorato per sei mesi con le scuole della città. I risultati sono stati migliori del previsto: "
        "più di duemila ragazzi hanno partecipato ai laboratori.\n\n## I prossimi passi\n\n[pausa] Da gennaio "
        "apriremo le prove al pubblico, ogni giovedì sera. Vi aspettiamo numerosi.\n");
    for (const double scale : {1.0, 2.0}) {
        QSettings().clear();
        QSettings().setValue("appearance/textScale", scale);
        QSettings().setValue("reading/countdown", 0);
        MainWindow window(library.path());
        window.show();
        const auto prefix = QString("text%1").arg(int(scale * 100));
        if (!QTest::qWaitForWindowExposed(&window))
            problem(prefix, "the window did not appear");
        auto *editor = window.prompter()->text;
        editor->setPlainText(script);
        editor->spaceLines();
        settle();
        if (qEnvironmentVariableIsSet("KALIPSO_SETTINGS_ONLY"))
            window.prompter()->findChild<QPushButton *>("readerSettings")->click();
        else
            readerStates(window, prefix);
        settingsStates(window, prefix);
    }
    for (const auto &line : problems)
        fprintf(stderr, "%s\n", qPrintable(line));
    fprintf(stderr, "%lld problem(s); %d photo(s)%s\n", qlonglong(problems.size()), shots,
            outDir.isEmpty() ? "" : qPrintable(" in " + outDir));
    return int(std::min<qsizetype>(problems.size(), 125));
}
