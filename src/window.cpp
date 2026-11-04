// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "window.h"
#include "app_bridge.h"
#include "design_tokens.h"
#include "icons.h"
#include "import.h"
#include "impress.h"
#include "interface_language.h"
#include "platform/capture_exclusion.h"
#include "platform/gnome_extension.h"
#include "platform/kwin_placement.h"
#include "remote.h"
#include "script_highlighter.h"
#include "speech_assets.h"
#include "speech_download.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <functional>

// A step's number in a ring of the accent colour; a done step is a filled ring with a check mark.
class StepBadge : public QWidget {
public:
    explicit StepBadge(int number) : number_(number) { setAccessibleName(QString::number(number)); }
    void setDone(bool done) {
        done_ = done;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor accent = palette().color(QPalette::Highlight);
        const QRectF ring = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        painter.setPen(QPen(accent, 2));
        painter.setBrush(done_ ? QBrush(accent) : QBrush(Qt::NoBrush)); // Qt::NoBrush alone would turn black.
        painter.drawEllipse(ring);
        auto font = this->font();
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(done_ ? palette().color(QPalette::HighlightedText) : palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter, done_ ? QStringLiteral("✓") : QString::number(number_));
    }

private:
    int number_;
    bool done_ = false;
};

namespace {
// Calls back when the watched widget is resized.
class ResizeWatcher : public QObject {
public:
    ResizeWatcher(QObject *parent, std::function<void(QSize)> resized)
        : QObject(parent), resized_(std::move(resized)) {}

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Resize)
            resized_(static_cast<QResizeEvent *>(event)->size());
        return QObject::eventFilter(object, event);
    }

private:
    std::function<void(QSize)> resized_;
};

// Scrolling a settings page must not silently change a language or model.
class SettingsComboBox : public QComboBox {
public:
    SettingsComboBox() {
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setMinimumContentsLength(1);
    }

protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

// Choosing here can start a download: keys open the list, and Enter chooses, instead of the
// arrow keys or typing changing the choice at once.
class ConfirmedComboBox : public SettingsComboBox {
protected:
    void keyPressEvent(QKeyEvent *event) override {
        const bool browse = event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
                            event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown ||
                            event->key() == Qt::Key_Home || event->key() == Qt::Key_End;
        const bool typing = !event->text().trimmed().isEmpty() &&
                            !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (browse || typing) {
            showPopup();
            return;
        }
        SettingsComboBox::keyPressEvent(event);
    }
};

// The microphones can change while Settings is open (a headset plugged in): the list is read again
// each time it opens.
class MicrophonePicker : public SettingsComboBox {
public:
    std::function<void()> opening;
    void showPopup() override {
        if (opening)
            opening();
        SettingsComboBox::showPopup();
    }
};

QString megabytes(qint64 bytes) { return QString::number((bytes + 524288) >> 20); }

// Shows a useful width of the script title but can still shrink on narrow windows.
class ScriptPicker : public SettingsComboBox {
public:
    QSize sizeHint() const override {
        auto size = SettingsComboBox::sizeHint();
        int text = 0;
        for (int i = 0; i < count(); ++i)
            text = std::max(text, fontMetrics().horizontalAdvance(itemText(i)));
        const int room = fontMetrics().averageCharWidth();
        size.setWidth(std::clamp(text + 84, room * 20, room * 44));
        return size;
    }
};

class SettingsSpinBox : public QSpinBox {
protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

// A titled block of settings. Labels move above their fields when the form
// becomes too narrow for both on one row.
class SettingsSection : public QWidget {
public:
    explicit SettingsSection(const QString &title) : form(new QFormLayout) {
        setObjectName("settingsSection");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, Design::Metric::space5);
        layout->setSpacing(Design::Metric::space3);
        auto *heading = new QLabel(title);
        heading->setProperty("role", "title");
        heading->setWordWrap(true); // Large and bold: on one line it can be wider than a narrow window.
        auto font = heading->font();
        font.setPointSizeF(font.pointSizeF() * 1.25);
        heading->setFont(font);
        layout->addWidget(heading);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        // Fields take the row's width (the combo boxes are only a letter wide on their own), whatever
        // the desktop style would choose.
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setFormAlignment(Qt::AlignLeading | Qt::AlignTop);
        form->setHorizontalSpacing(Design::Metric::space4);
        form->setVerticalSpacing(Design::Metric::space3);
        form->setLabelAlignment(Qt::AlignLeading | Qt::AlignVCenter);
        layout->addLayout(form);
    }
    QFormLayout *form;
    int labelWidth() const {
        int width = 0;
        for (int row = 0; row < form->rowCount(); ++row)
            if (auto *item = form->itemAt(row, QFormLayout::LabelRole); item && item->widget())
                width = std::max(width, item->widget()->fontMetrics().horizontalAdvance(
                                            static_cast<QLabel *>(item->widget())->text()));
        return width + 4;
    }
    void setLabelWidth(int width) {
        labelWidth_ = width;
        adaptForm();
    }
    void fitViewport(QWidget *viewport) {
        viewport_ = viewport;
        viewport_->installEventFilter(this);
        adaptForm();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == viewport_ && event->type() == QEvent::Resize)
            adaptForm();
        return QWidget::eventFilter(object, event);
    }
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        adaptForm();
    }
    void changeEvent(QEvent *event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            adaptForm();
    }

private:
    void adaptForm() {
        int available = std::max(1, contentsRect().width());
        if (viewport_) {
            int margins = 0;
            for (auto *parent = parentWidget(); parent && parent != viewport_; parent = parent->parentWidget())
                if (parent->layout()) {
                    const auto insets = parent->layout()->contentsMargins();
                    margins += insets.left() + insets.right();
                }
            available = std::max(1, std::min(available, viewport_->width() - margins));
        }
        const bool narrow = available < fontMetrics().horizontalAdvance(QStringLiteral("Settings and dialogs")) + 350;
        form->setRowWrapPolicy(narrow ? QFormLayout::WrapAllRows : QFormLayout::WrapLongRows);
        form->setLabelAlignment(Qt::AlignLeading | Qt::AlignVCenter);
        for (int row = 0; row < form->rowCount(); ++row)
            if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
                if (auto *label = qobject_cast<QLabel *>(item->widget())) {
                    label->setWordWrap(narrow);
                    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                    label->setMaximumWidth(narrow ? available : QWIDGETSIZE_MAX);
                    // Above its field, use the real viewport width. Subtracting a hypothetical scroll
                    // bar twice wastes room, while a wrapped label's size hint can split short words.
                    label->setMinimumWidth(narrow ? available : std::min(labelWidth_, available / 2));
                }
    }
    int labelWidth_ = 0;
    QWidget *viewport_ = nullptr;
};

// A script in the list: its title, and under it how long it takes to read and when it changed,
// like notes in a notebook. On the reader, whose colours stay its own whatever the desktop's, the row
// is drawn in the reader's colours, as a quiet button that is checked: the desktop's selection colour
// (light on a light desktop) would show as a bright block on a dark reader. Elsewhere the style draws
// the row (selection, hover) and the delegate the text.
class ScriptItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int MetaRole = Qt::UserRole + 1;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const QString title = item.text;
        item.text.clear();
        const bool selected = item.state & QStyle::State_Selected;
        QColor text, secondary;
        if (const auto *reader = readerOf(item.widget)) {
            const auto &colors = reader->colors();
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QRectF row = QRectF(item.rect).adjusted(0.5, 0.5, -0.5, -0.5);
            constexpr qreal corner = 6; // As the menus' rows.
            if (selected || item.state & QStyle::State_MouseOver) {
                painter->setPen(selected ? QPen(QColor(colors.divider), 1) : QPen(Qt::NoPen));
                painter->setBrush(QColor(colors.hover));
                painter->drawRoundedRect(row, corner, corner);
            }
            if (item.state & QStyle::State_HasFocus && item.widget && item.widget->hasFocus()) {
                painter->setPen(QPen(QColor(colors.focus), 2));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(QRectF(item.rect).adjusted(1, 1, -1, -1), corner, corner);
            }
            painter->restore();
            text = QColor(colors.text);
            secondary = QColor(colors.muted);
        } else {
            const auto *style = item.widget ? item.widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
            const auto group = !(item.state & QStyle::State_Enabled) ? QPalette::Disabled
                               : (item.state & QStyle::State_Active) ? QPalette::Active
                                                                     : QPalette::Inactive;
            text = item.palette.color(group, selected ? QPalette::HighlightedText : QPalette::Text);
            secondary = selected ? text : Accessibility::muted(item.palette);
        }
        const QRect area = item.rect.adjusted(Design::Metric::space3, Design::Metric::space2, -Design::Metric::space3,
                                              -Design::Metric::space2);
        painter->save();
        // As the KDE guidelines ask: the selected script in bold.
        QFont strong = item.font;
        strong.setWeight(selected ? QFont::DemiBold : QFont::Normal);
        const QFontMetrics titleMetrics(strong);
        painter->setFont(strong);
        painter->setPen(text);
        painter->drawText(QRect(area.topLeft(), QSize(area.width(), titleMetrics.height())),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          titleMetrics.elidedText(title, Qt::ElideRight, area.width()));
        const QFont small = metaFont(item.font);
        const QFontMetrics metaMetrics(small);
        painter->setFont(small);
        painter->setPen(secondary);
        painter->drawText(QRect(area.left(), area.top() + titleMetrics.height() + Design::Metric::space1 / 2,
                                area.width(), metaMetrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          metaMetrics.elidedText(index.data(MetaRole).toString(), Qt::ElideRight, area.width()));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        QFont strong = option.font;
        strong.setWeight(QFont::DemiBold);
        return {0, QFontMetrics(strong).height() + Design::Metric::space1 / 2 +
                       QFontMetrics(metaFont(option.font)).height() + 2 * Design::Metric::space2};
    }

private:
    static QFont metaFont(QFont font) {
        font.setPointSizeF(font.pointSizeF() * 0.9);
        return font;
    }
    static const ReaderPage *readerOf(const QWidget *widget) {
        for (; widget; widget = widget->parentWidget())
            if (const auto *reader = qobject_cast<const ReaderPage *>(widget))
                return reader;
        return nullptr;
    }
};

QLabel *note(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// A check box whose label wraps at narrow widths and large text sizes.
class WrappingCheckBox : public QWidget {
public:
    explicit WrappingCheckBox(const QString &text) : box(new QCheckBox) {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        box->setAccessibleName(text);
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        label->setBuddy(box);
        label->installEventFilter(this);
        layout->addWidget(box, 0, Qt::AlignTop);
        layout->addWidget(label, 1);
    }
    QCheckBox *box;

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonRelease && box->isEnabled())
            box->toggle();
        return QWidget::eventFilter(object, event);
    }
};

// A language as the interface names it, so every name can be read and drawn ("Japanese" rather than
// letters a missing font shows as boxes). In a sentence: "in italiano", "in Italian".
QString spokenName(const QString &code) {
    static const QHash<QString, const char *> names{
        {"de", QT_TRANSLATE_NOOP("MainWindow", "German")},  {"en", QT_TRANSLATE_NOOP("MainWindow", "English")},
        {"es", QT_TRANSLATE_NOOP("MainWindow", "Spanish")}, {"fr", QT_TRANSLATE_NOOP("MainWindow", "French")},
        {"it", QT_TRANSLATE_NOOP("MainWindow", "Italian")}, {"ja", QT_TRANSLATE_NOOP("MainWindow", "Japanese")},
        {"zh", QT_TRANSLATE_NOOP("MainWindow", "Chinese")}};
    const char *name = names.value(code);
    return name ? QCoreApplication::translate("MainWindow", name) : SpeechCatalog::languageName(code);
}
// In a list: the language's own name, as people look for it ("Deutsch", "Italiano"), unless no font
// on this computer has its letters; then the interface's name for it, capitalised ("Giapponese").
QString listedName(const QString &code) {
    const auto own = SpeechCatalog::languageName(code);
    const auto system = QHash<QString, QFontDatabase::WritingSystem>{{"ja", QFontDatabase::Japanese},
                                                                     {"zh", QFontDatabase::SimplifiedChinese}}
                            .value(code, QFontDatabase::Latin);
    if (own != code && !QFontDatabase::families(system).isEmpty())
        return own;
    auto name = spokenName(code);
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

QString duration(qint64 ms) {
    const qint64 seconds = (ms + 500) / 1000;
    const auto remainder = QLocale().toString(seconds % 60);
    return QStringLiteral("%1:%2").arg(QLocale().toString(seconds / 60),
                                       seconds % 60 < 10 ? QLocale().toString(0) + remainder : remainder);
}

const char *modeKey(ReadingMode mode) {
    return mode == ReadingMode::Manual ? "manual" : mode == ReadingMode::Steady ? "steady" : "voice";
}
ReadingMode modeFromKey(const QString &key) {
    return key == QLatin1String("manual")   ? ReadingMode::Manual
           : key == QLatin1String("steady") ? ReadingMode::Steady
                                            : ReadingMode::Voice;
}
} // namespace

MainWindow::MainWindow(const QString &libraryDirectory)
    : library_(libraryDirectory), speech_(this), prompter_(new ReaderPage) {
    Accessibility::useInterfaceStyle();
    Accessibility::useInterfaceFont();
    setWindowTitle(tr("Kalipso"));
    resize(1100, 760);
    setMinimumSize(320, 240);
    if (!Accessibility::native())
        Accessibility::apply(this, Design::light);

    // One window, one text: the reader is also where the script is written. Large, it shows the
    // formats and the scripts beside the text; small, under the camera, only what reading needs.
    script_ = prompter_->text;
    script_->setAccessibleName(tr("Script"));
    // The formats as Markdown marks the reader hides: buttons in the reader's top bar and the usual
    // shortcuts. Each button puts the format on or takes it off again, as in word processors.
    const QList<std::tuple<QString, QString, QKeySequence, QString>> formats{
        {"formatBold", tr("Bold"), QKeySequence::Bold, QStringLiteral("bold")},
        {"formatItalic", tr("Italic"), QKeySequence::Italic, QStringLiteral("italic")},
        {"formatHeading", tr("Section heading"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H),
         QStringLiteral("heading")},
        {"formatCue", tr("Direction cue, not read aloud"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
         QStringLiteral("cue")}};
    for (const auto &[name, label, shortcut, kind] : formats) {
        auto *action = new QAction(label, this);
        action->setObjectName(name);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setCheckable(true);
        action->setToolTip(tr("%1 (%2)").arg(label, shortcut.toString(QKeySequence::NativeText)));
        formatActions_.append(action);
        connect(action, &QAction::triggered, this, [this, kind] { toggleFormat(kind); });
    }
    script_->addActions(formatActions_); // The shortcuts work while the text has the focus.
    prompter_->setFormatActions(formatActions_);
    connect(script_, &QTextEdit::cursorPositionChanged, this, &MainWindow::updateFormatState);
    prompter_->setDrawer(makeDrawer());
    welcome_ = makeWelcome();
    prompter_->setWelcome(welcome_);
    setCentralWidget(prompter_);
    settings_ = new QDialog(this);
    settings_->setWindowTitle(tr("Kalipso settings"));
    settings_->setObjectName("settingsDialog");
    auto *dialogLayout = new QVBoxLayout(settings_);
    dialogLayout->setContentsMargins(0, 0, 0, Design::Metric::space4);
    dialogLayout->addWidget(makeSettings(), 1);
    // Our own Close button, with the app's close icon (the platform's standard one does not match).
    auto *closeRow = new QHBoxLayout;
    closeRow->setContentsMargins(Design::Metric::space5, 0, Design::Metric::space5, 0);
    closeRow->addStretch();
    auto *close = new AccessibleButton(tr("Close"));
    close->setObjectName("closeSettings");
    connect(close, &QPushButton::clicked, settings_, &QDialog::close);
    closeRow->addWidget(close);
    dialogLayout->addLayout(closeRow);
    icons_.append({close, "close"});
    settings_->resize(860, 640);
    // The page list beside the pages when the dialog is wide, a picker above them when narrow.
    settings_->installEventFilter(new ResizeWatcher(settings_, [this](QSize size) {
        if (settingsAdapt_)
            settingsAdapt_(size.width());
    }));
    connect(settings_, &QDialog::finished, this, [this] {
        if (microphoneTestEnd_.isActive())
            stopMicrophoneTest(false);
    });
    applyTheme();

    autosave_.setSingleShot(true);
    autosave_.setInterval(800);
    connect(&autosave_, &QTimer::timeout, this, &MainWindow::saveScript);
    steady_.setTimerType(Qt::PreciseTimer);
    connect(&steady_, &QTimer::timeout, this, &MainWindow::steadyTick);
    countdown_.setInterval(1000);
    connect(&countdown_, &QTimer::timeout, this, [this] {
        prompter_->showCountdown(--countdownLeft_);
        if (countdownLeft_ > 0)
            return;
        countdown_.stop();
        steady_.start();
        refreshRunning();
    });

    connect(script_, &QTextEdit::textChanged, this, [this] {
        const auto text = script_->toPlainText();
        if (text == scriptText_)
            return; // Formatting-only document change.
        scriptText_ = text;
        tracker_.setScript(text);
        if (speech_.active()) {
            waitForNewUtterance_ = true;
            speech_.setScript(text, tracker_.position());
        }
        prompter_->setSections(ScriptTracker::sections(text));
        prompter_->setTitle(ScriptLibrary::titleFor(text));
        updateWindowTitle();
        if (!loading_)
            refreshCurrentRow();
        if (!loading_)
            autosave_.start();
        refreshReading();
        if (prompter_->welcomeShown() && !text.trimmed().isEmpty())
            refreshWelcome(); // Imported or pasted from elsewhere: the script takes the page.
    });
    connect(prompter_, &ReaderPage::step, this, [this](int delta) { seekWord(tracker_.position() + delta); });
    connect(prompter_, &ReaderPage::resetRequested, this, [this] { seekWord(-1); });
    connect(prompter_, &ReaderPage::jumpRequested, this, [this](int character) {
        tracker_.seekCharacter(character);
        seekWord(tracker_.position());
    });
    connect(prompter_, &ReaderPage::toggleRequested, this, &MainWindow::toggleRunning);
    connect(prompter_, &ReaderPage::sectionRequested, this, &MainWindow::jumpSection);
    connect(prompter_, &ReaderPage::speedRequested, this, &MainWindow::changeSpeed);
    connect(prompter_, &ReaderPage::modeRequested, this, &MainWindow::setMode);
    connect(prompter_, &ReaderPage::placeRequested, this, [this] {
        if (underCamera_)
            expand();
        else
            placeUnderCamera();
    });
    connect(prompter_, &ReaderPage::textSizeRequested, this, &MainWindow::stepTextSize);
    connect(prompter_, &ReaderPage::editingChanged, this, [this] { prompter_->setStatus(statusText()); });
    connect(prompter_, &ReaderPage::settingsRequested, this, [this] { showSettings(); });
    connect(prompter_, &ReaderPage::blockedRequested, this, [this] { showSettings(voiceLanguage_); });

    connect(&speech_, &SpeechService::status, this, [this](const QString &message) {
        speechStatus_ = message;
        // A message while not listening says why listening did not start or ended: the reader shows it.
        speechError_ = speech_.active() ? QString() : message;
        refreshRunning();
    });
    impress_ = new ImpressFollower(this);
    connect(impress_, &ImpressFollower::slideChanged, this, &MainWindow::followSlide);
    connect(impress_, &ImpressFollower::statusChanged, this, &MainWindow::notify);
    connect(impress_, &ImpressFollower::finished, this,
            [this] { impressAction_->setText(tr("Present with Impress…")); });
    // The reader hides its status while you read: a silent microphone shows it again.
    connect(&speech_, &SpeechService::silent, this, [this](bool silent) {
        microphoneSilent_ = silent;
        refreshRunning();
        if (silent)
            prompter_->revealControls();
    });
    connect(&speech_, &SpeechService::listening, this,
            [this] { session_.start(tracker_.position(), QDateTime::currentMSecsSinceEpoch()); });
    // A recognizer that learnt the script hears its words in anything said off it: the tracker then
    // asks for more words heard right before it skips or jumps.
    connect(&speech_, &SpeechService::guidedChanged, this, [this](bool guided) { tracker_.setGuided(guided); });
    connect(&speech_, &SpeechService::stopped, this, [this] {
        microphoneSilent_ = false;
        finishSession();
        backend_->setEnabled(!download_->active());
        voiceLanguage_->setEnabled(!download_->active());
        language_->setEnabled(true);
        checkSystem_->setEnabled(true);
        refreshRunning();
    });
    connect(&speech_, &SpeechService::transcript, this, [this](const QString &text, bool final) {
        if (waitForNewUtterance_) {
            if (final) {
                waitForNewUtterance_ = false;
                refreshReading();
            }
            return;
        }
        const bool wasLost = tracker_.lost();
        const int before = tracker_.position();
        if (tracker_.follow(text, final)) {
            // A recovery jump is not reading pace; restart the measurement from there.
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (tracker_.position() - before > 10)
                session_.rebase(tracker_.position(), now);
            else
                session_.advance(tracker_.position(), now);
            refreshReading();
        }
        if (final) {
            session_.transcript(text);
            speech_.setScript(script_->toPlainText(), tracker_.position());
        }
        if (tracker_.lost() != wasLost)
            refreshRunning();
    });

    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this] {
        saveScript();
        notify(tr("Script saved."));
    });
    // Start or pause from anywhere, also while writing (where Space types a space).
    auto *startShortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(startShortcut, &QShortcut::activated, this, [this] {
        if (prompter_->editing() && !running())
            prompter_->text->setFocus(); // Pedals and keys go to the reader from now on.
        if (mode_ == ReadingMode::Voice && !running() && !voiceReady() && SpeechCatalog::downloadsSupported())
            showSettings(voiceLanguage_);
        else
            toggleRunning();
    });
    // The text size, as in word processors and browsers.
    for (const auto &[keys, direction] :
         {std::pair{QKeySequence(QKeySequence::ZoomIn), 1}, std::pair{QKeySequence(QStringLiteral("Ctrl+=")), 1},
          std::pair{QKeySequence(QKeySequence::ZoomOut), -1}}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this, direction] { stepTextSize(direction); });
    }

    setMode(modeFromKey(QSettings().value("reading/mode", "voice").toString()));
    prompter_->setSpeed(wordsPerMinute_);
    refreshLibrary();
    const auto saved = QSettings().value("library/current").toString();
    if (library_.contains(saved))
        openScript(saved);
    else if (!library_.entries().isEmpty())
        openScript(library_.entries().first().id);
    else
        newScript();
    refreshWelcome();
}

MainWindow::~MainWindow() {
    dropIfEmpty();
    saveScript();
    saveSettings();
    speech_.stop();
    disconnect(&speech_, nullptr, this, nullptr);
    disconnect(prompter_, nullptr, this, nullptr);
}

// --- Script library ---------------------------------------------------------

// The scripts, in a panel of the reader: their names with how long they take and when they changed,
// a button for a new one, and a menu for the rest.
QWidget *MainWindow::makeDrawer() {
    auto *drawer = new QWidget;
    drawer->setObjectName("scriptDrawer");
    drawer->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(drawer);
    layout->setContentsMargins(Design::Metric::space2, Design::Metric::space3, Design::Metric::space2, 0);
    layout->setSpacing(Design::Metric::space2);
    auto *headerRow = new QWidget;
    headerRow->setObjectName("scriptDrawerHeader");
    auto *header = new QHBoxLayout(headerRow);
    header->setContentsMargins(Design::Metric::space2, 0, 0, 0);
    header->setSpacing(Design::Metric::space1);
    auto *title = new QLabel(tr("Scripts"));
    title->setObjectName("libraryTitle");
    title->setProperty("role", "muted");
    auto titleFont = title->font();
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    auto *add = new AccessibleButton(tr("New"));
    add->setObjectName("newScript");
    add->setAccessibleName(tr("New script"));
    add->setToolTip(tr("New script (Ctrl+N)"));
    add->setProperty("quiet", true);
    add->setProperty("label", add->text()); // The reader shortens it to its icon in a narrow panel.
    add->setFocusPolicy(Qt::TabFocus);      // A click leaves the keys, and the pedals, with the script.
    // The menu of everything else, at the end of the row like in the desktop's own apps.
    auto *more = new AccessibleButton(QString());
    more->setObjectName("scriptActions");
    more->setToolTip(tr("More"));
    more->setAccessibleName(tr("More script actions"));
    more->setProperty("quiet", true);
    more->setFocusPolicy(Qt::TabFocus);
    more_ = more;
    auto *menu = new QMenu(this);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] { prompter_->styleMenu(menu); });
    auto *import = menu->addAction(tr("Import…"));
    import->setToolTip(tr("Text, Markdown, Word (.docx) and OpenDocument (.odt) documents, or speaker notes from "
                          "PowerPoint (.pptx) and Impress (.odp)"));
    impressAction_ = menu->addAction(tr("Present with Impress…"));
    impressAction_->setObjectName("presentWithImpress");
    impressAction_->setToolTip(tr("Open a presentation in LibreOffice Impress and follow its slideshow: the reader "
                                  "jumps to the notes of the slide being shown."));
    auto *exportAction = menu->addAction(tr("Export…"));
    reportAction_ = menu->addAction(tr("Save session report…"));
    reportAction_->setObjectName("saveSessionReport");
    reportAction_->setEnabled(false); // Available after a voice session.
    menu->addSeparator();
    auto *remove = menu->addAction(tr("Delete script…"));
    remove->setObjectName("deleteScript");
    menu->setToolTipsVisible(true);
    connect(more, &QPushButton::clicked, menu,
            [more, menu] { menu->popup(more->mapToGlobal(QPoint(0, more->height()))); });
    header->addWidget(title, 1);
    header->addWidget(add);
    header->addWidget(more);
    layout->addWidget(headerRow);
    prompter_->addIconButton(add, "plus");
    prompter_->addIconButton(more, "more");

    scriptList_ = new QListWidget;
    scriptList_->setObjectName("scriptList");
    scriptList_->setAccessibleName(tr("Scripts"));
    scriptList_->setFrameShape(QFrame::NoFrame);
    scriptList_->setTextElideMode(Qt::ElideRight);
    scriptList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scriptList_->setItemDelegate(new ScriptItemDelegate(scriptList_));
    scriptList_->setSpacing(1);
    layout->addWidget(scriptList_, 1);
    connect(scriptList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        openScript(item->data(Qt::UserRole).toString());
    });
    // Closed, no row is current: a screen reader does not hear a hidden row as focused. Open, the
    // script being read is the current row again.
    connect(prompter_, &ReaderPage::drawerShown, this, [this](bool shown) {
        const QSignalBlocker block(scriptList_);
        if (!shown) {
            scriptList_->setCurrentItem(nullptr);
            scriptList_->clearSelection();
            // Hidden, its layout does not size it; Qt reports rows as on the screen from their rectangles.
            scriptList_->resize(0, 0);
            return;
        }
        for (int row = 0; row < scriptList_->count(); ++row)
            if (scriptList_->item(row)->data(Qt::UserRole).toString() == currentId_ || (!persist_ && row == 0))
                scriptList_->setCurrentRow(row);
    });
    // Over the text, in a small window, the panel goes once a script is chosen.
    connect(scriptList_, &QListWidget::itemClicked, this, [this] {
        if (!prompter_->roomy())
            prompter_->setDrawerOpen(false);
    });
    auto *newShortcut = new QShortcut(QKeySequence::New, this);
    connect(newShortcut, &QShortcut::activated, this, &MainWindow::newScript);

    connect(add, &QPushButton::clicked, this, &MainWindow::newScript);
    connect(import, &QAction::triggered, this, &MainWindow::importScript);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportScript);
    connect(remove, &QAction::triggered, this, &MainWindow::deleteScript);
    connect(impressAction_, &QAction::triggered, this, [this] {
        if (impress_->active()) {
            impress_->stop();
            return;
        }
        const auto path =
            QFileDialog::getOpenFileName(this, tr("Present with Impress"), {}, tr("Presentations (*.odp *.pptx)"));
        if (!path.isEmpty() && importFile(path) && impress_->start(path))
            impressAction_->setText(tr("Stop following Impress"));
    });
    connect(reportAction_, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getSaveFileName(
            this, tr("Save session report"), tr("%1 - session.md").arg(ScriptLibrary::titleFor(lastSessionScript_)),
            tr("Markdown (*.md);;Text (*.txt)"));
        if (!path.isEmpty())
            saveSessionReport(path);
    });
    return drawer;
}

bool MainWindow::saveSessionReport(const QString &path) {
    const auto report = SessionStats::report(lastSession_, lastSessionScript_,
                                             ScriptLibrary::titleFor(lastSessionScript_), lastSessionAt_);
    QSaveFile file(path);
    const auto data = report.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(this, tr("Cannot save the report"), tr("Check the destination and available disk space."));
        return false;
    }
    notify(tr("Session report saved to %1.").arg(QDir::toNativeSeparators(path)));
    return true;
}

namespace {
// The drawn look's palette closest to these colours.
const Design::Palette &tokensFor(const QPalette &colors) {
    return Accessibility::luminance(colors.color(QPalette::Window)) < 0.2 ? Design::dark : Design::light;
}
} // namespace

void MainWindow::applyTheme() {
    // "system" follows the desktop, light or dark, and changes with it.
    const auto theme = QSettings().value("appearance/theme", "system").toString();
    const Design::Palette *tokens = theme == QLatin1String("dark")    ? &Design::dark
                                    : theme == QLatin1String("light") ? &Design::light
                                                                      : nullptr;
    if (Accessibility::native()) {
        const QPalette colors = tokens ? Accessibility::palette(*tokens) : QPalette();
        Accessibility::applyNative(this, colors);
        Accessibility::applyNative(settings_, colors);
    } else {
        if (!tokens)
            tokens = Accessibility::systemIsDark() ? &Design::dark : &Design::light;
        Accessibility::apply(this, *tokens);
        Accessibility::apply(settings_, *tokens);
    }
    const QPalette &used = palette();
    for (const auto &[button, name] : icons_) {
        // The window's new colours: while the desktop's change travels down, a button can still have
        // the old ones. The primary action's colours were just set above.
        const bool primary = button->objectName() == QLatin1String("primaryAction");
        const auto color = primary && Accessibility::native() ? button->palette().color(QPalette::ButtonText)
                           : primary                          ? QColor(tokensFor(used).actionText)
                                                              : used.color(QPalette::ButtonText);
        button->setProperty("motion", Icons::motion(name));
        button->setIcon(Icons::icon(name, color, Accessibility::muted(used)));
    }
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    // The desktop switched between light and dark, or changed its colours.
    if (event->type() == QEvent::ApplicationPaletteChange && !applyingTheme_) {
        applyingTheme_ = true;
        applyTheme();
        applyingTheme_ = false;
    }
    // Out of sight, the reader stops: the microphone is off until Start is pressed again.
    if (event->type() == QEvent::WindowStateChange && isMinimized())
        pauseOutOfSight();
}

void MainWindow::pauseOutOfSight() {
    if (!running())
        return;
    pauseRunning();
    notify(tr("Paused while the window was minimized · microphone off"));
}

void MainWindow::connectBridge(AppBridge *bridge) {
    // On Wayland only the compositor knows the window was minimized: Kalipso's KWin script says so.
    connect(bridge, &AppBridge::minimizedChanged, this, [this](bool minimized) {
        if (minimized)
            pauseOutOfSight();
    });
    connect(bridge, &AppBridge::activationRequested, this, &MainWindow::comeForward);
}

void MainWindow::comeForward(const QString &token) {
    // The desktop lets a window take the focus with the token it gave the start that asked.
    if (!token.isEmpty())
        qputenv(QGuiApplication::platformName().startsWith(QLatin1String("wayland")) ? "XDG_ACTIVATION_TOKEN"
                                                                                     : "DESKTOP_STARTUP_ID",
                token.toUtf8());
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    // "Edit in Kalipso" from the widget or the extension chose a script before starting Kalipso again:
    // the window opens it.
    QSettings settings;
    settings.sync(); // Written by the companion, another process.
    if (const auto wanted = settings.value("library/current").toString();
        !wanted.isEmpty() && wanted != currentId_ && library_.contains(wanted))
        openScript(wanted);
    show();
    raise();
    activateWindow();
}

void MainWindow::notify(const QString &message) { prompter_->showMessage(message); }

void MainWindow::refreshLibrary() {
    const QSignalBlocker blockList(scriptList_);
    scriptList_->clear();
    for (const auto &entry : library_.entries()) {
        auto *item = new QListWidgetItem(entry.title, scriptList_);
        item->setData(Qt::UserRole, entry.id);
        item->setData(ScriptItemDelegate::MetaRole, scriptMeta(entry));
        item->setToolTip(entry.title);
        if (entry.id == currentId_)
            scriptList_->setCurrentItem(item);
    }
}

// "2 min · 14:32": how long the script takes to read at its steady speed, and when it changed.
QString MainWindow::scriptMeta(const ScriptLibrary::Entry &entry) const {
    ScriptTracker counter;
    counter.setScript(entry.id == currentId_ ? script_->toPlainText() : library_.load(entry.id));
    const int speed = entry.speed > 0 ? entry.speed : wordsPerMinute_;
    const int words = counter.count();
    const int minutes = (words + speed - 1) / std::max(1, speed);
    const auto length = words == 0 ? tr("Empty") : minutes <= 1 ? tr("under 1 min") : tr("%n min", nullptr, minutes);
    const auto when = entry.modified.toLocalTime();
    const auto today = QDate::currentDate();
    const auto date = when.date() == today               ? QLocale().toString(when.time(), QLocale::ShortFormat)
                      : when.date() == today.addDays(-1) ? tr("yesterday")
                      : when.date().year() == today.year()
                          ? QLocale().toString(when.date(), QStringLiteral("d MMM"))
                          : QLocale().toString(when.date(), QStringLiteral("d MMM yyyy"));
    return when.isValid() ? tr("%1 · %2").arg(length, date) : length;
}

void MainWindow::openScript(const QString &id) {
    if (id == currentId_ && !id.isEmpty())
        return;
    if (id != currentId_)
        dropIfEmpty(); // An empty script is not kept, as with notes.
    if (!saveScript()) {
        refreshLibrary(); // Keep the picker on the script that could not be saved.
        return;
    }
    if (running())
        pauseRunning();
    // Read the saved position first: loading the text refreshes the reading state.
    const int character = library_.position(id);
    currentId_ = id;
    persist_ = true;
    QSettings().setValue("library/current", id);
    loading_ = true;
    script_->setPlainText(library_.load(id));
    script_->spaceLines();
    loading_ = false;
    tracker_.setScript(script_->toPlainText());
    if (character >= 0)
        tracker_.seekCharacter(character);
    else
        tracker_.seek(-1);
    if (const int speed = library_.speed(id); speed > 0) {
        wordsPerMinute_ = std::clamp(speed, 40, 400);
        prompter_->setSpeed(wordsPerMinute_);
        steady_.setInterval(60000 / wordsPerMinute_);
    }
    refreshLibrary();
    seekWord(tracker_.position());
    prompter_->placeCaret(tracker_.character()); // While writing, the caret where reading stopped.
    refreshWelcome();
}

bool MainWindow::saveScript() {
    autosave_.stop();
    if (!persist_ || currentId_.isEmpty())
        return true;
    const auto text = script_->toPlainText();
    if (text != library_.load(currentId_)) {
        if (!library_.save(currentId_, text)) {
            notify(tr("Cannot save the script. Check the available disk space."));
            return false;
        }
        refreshCurrentRow();
    }
    library_.flush();
    return true;
}

// The current script's title and length in the list and the picker, as you type.
void MainWindow::refreshCurrentRow() {
    if (!persist_ || currentId_.isEmpty())
        return;
    const auto title = ScriptLibrary::titleFor(script_->toPlainText());
    auto *item = scriptList_->currentItem();
    if (!item || item->data(Qt::UserRole).toString() != currentId_)
        return;
    item->setText(title);
    item->setToolTip(title);
    for (auto entry : library_.entries())
        if (entry.id == currentId_) {
            entry.modified = QDateTime::currentDateTime(); // Changed now, saved in a moment.
            item->setData(ScriptItemDelegate::MetaRole, scriptMeta(entry));
        }
}

void MainWindow::dropIfEmpty() {
    if (!persist_ || currentId_.isEmpty() || !script_->toPlainText().trimmed().isEmpty())
        return;
    autosave_.stop();
    library_.remove(currentId_);
    currentId_.clear();
}

void MainWindow::newScript() {
    const auto id = library_.create();
    if (id.isEmpty()) {
        notify(tr("Cannot create a script in %1.").arg(library_.directory()));
        return;
    }
    openScript(id);
    script_->setFocus();
}

void MainWindow::importScript() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Import script"), {}, ScriptImport::dialogFilter());
    if (!path.isEmpty())
        importFile(path);
}

bool MainWindow::importFile(const QString &path) {
    const auto result = ScriptImport::read(path);
    if (!result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot import"), result.error);
        return false;
    }
    const auto id = library_.create(result.text);
    if (id.isEmpty()) {
        notify(tr("Cannot save the imported script."));
        return false;
    }
    openScript(id);
    auto message = result.sections ? tr("Imported speaker notes from %n slide(s). Use Sections in the reader "
                                        "to jump between slides.",
                                        nullptr, result.sections)
                                   : tr("Script imported.");
    if (result.hidden) // Not in the slideshow, so not in the script either.
        message += ' ' + tr("%n hidden slide(s) left out.", nullptr, result.hidden);
    notify(message);
    return true;
}

// Wraps the selection (or an empty pair at the caret) in Markdown markers. Spaces at the
// edges stay outside, since "** bold**" is not bold in Markdown.
void MainWindow::wrapSelection(const QString &before, const QString &after) {
    auto cursor = script_->textCursor();
    const auto selected = cursor.selectedText();
    const auto core = selected.trimmed();
    cursor.beginEditBlock();
    if (core.isEmpty()) {
        cursor.insertText(selected + before + after);
        cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, int(after.size()));
    } else {
        const auto lead = selected.indexOf(core);
        const auto trail = selected.mid(lead + core.size());
        cursor.insertText(selected.left(lead) + before + core + after + trail);
        cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, int(after.size() + trail.size()));
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, int(core.size()));
    }
    cursor.endEditBlock();
    script_->setTextCursor(cursor);
    script_->setFocus();
}

// Makes the current line a "# " section heading, or a plain line again.
void MainWindow::toggleHeading() {
    auto cursor = script_->textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::StartOfBlock);
    const auto line = cursor.block().text();
    static const QRegularExpression marker(QStringLiteral("^ {0,3}#{1,6} ?"));
    if (const auto found = marker.match(line); found.hasMatch()) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, int(found.capturedLength()));
        cursor.removeSelectedText();
    } else {
        cursor.insertText("# ");
    }
    cursor.endEditBlock();
    script_->setFocus();
}

namespace {
// The Markdown the formats write, as the highlighter reads it.
const QRegularExpression &strongMarks() {
    static const QRegularExpression pattern(QStringLiteral(R"(\*\*(?=\S)(.+?)(?<=\S)\*\*)"));
    return pattern;
}
const QRegularExpression &emphasisMarks() {
    static const QRegularExpression pattern(QStringLiteral(R"((?<![*\w])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?![*\w]))"));
    return pattern;
}
const QRegularExpression &strongEmphasisMarks() {
    static const QRegularExpression pattern(QStringLiteral(R"(\*\*\*(?=\S)(.+?)(?<=\S)\*\*\*)"));
    return pattern;
}
const QRegularExpression &cueMarks() {
    static const QRegularExpression pattern(QStringLiteral(R"(\[[^\[\]\n]*\])"));
    return pattern;
}

// The format around the caret or the selection (both ends inside the same marks), as document
// positions of its first and past its last mark.
struct Span {
    int start = -1, end = -1;
    bool found() const { return start >= 0; }
};
Span spanAround(const QTextCursor &cursor, const QRegularExpression &pattern) {
    const auto *document = cursor.document();
    const auto block = document->findBlock(cursor.selectionStart());
    if (block != document->findBlock(cursor.selectionEnd()))
        return {};
    const int from = cursor.selectionStart() - block.position(), to = cursor.selectionEnd() - block.position();
    for (auto matches = pattern.globalMatch(block.text()); matches.hasNext();) {
        const auto match = matches.next();
        if (from >= match.capturedStart() && to <= match.capturedEnd())
            return {block.position() + int(match.capturedStart()), block.position() + int(match.capturedEnd())};
    }
    return {};
}
} // namespace

// A format button puts its format on the selection (or at the caret), or takes it off the text it is
// already on, keeping the selection on the same words.
void MainWindow::toggleFormat(const QString &kind) {
    if (kind == QLatin1String("heading")) {
        toggleHeading();
        updateFormatState();
        return;
    }
    auto cursor = script_->textCursor();
    const bool emphasis = kind == QLatin1String("bold") || kind == QLatin1String("italic");
    const int marks = kind == QLatin1String("bold") ? 2 : 1;
    Span span = emphasis ? spanAround(cursor, strongEmphasisMarks()) : Span{};
    if (!span.found())
        span = spanAround(cursor, kind == QLatin1String("bold")     ? strongMarks()
                                  : kind == QLatin1String("italic") ? emphasisMarks()
                                                                    : cueMarks());
    if (!span.found()) {
        if (kind == QLatin1String("cue"))
            wrapSelection("[", "]");
        else
            wrapSelection(QString(marks, QLatin1Char('*')), QString(marks, QLatin1Char('*')));
        updateFormatState();
        return;
    }
    const auto shift = [&](int position) {
        if (position > span.end - marks)
            position = std::min(position, span.end - marks) - marks;
        else if (position > span.start)
            position = std::max(span.start, position - marks);
        return position;
    };
    const int anchor = shift(cursor.anchor()), position = shift(cursor.position());
    QTextCursor edit(script_->document());
    edit.beginEditBlock();
    edit.setPosition(span.end - marks);
    edit.setPosition(span.end, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.setPosition(span.start);
    edit.setPosition(span.start + marks, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.endEditBlock();
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    script_->setTextCursor(cursor);
    script_->setFocus();
    updateFormatState();
}

// The format buttons show which formats the caret is in.
void MainWindow::updateFormatState() {
    if (formatActions_.size() < 4)
        return;
    const auto cursor = script_->textCursor();
    const bool both = spanAround(cursor, strongEmphasisMarks()).found();
    const bool states[] = {
        both || spanAround(cursor, strongMarks()).found(), both || spanAround(cursor, emphasisMarks()).found(),
        script_->highlighter()->headingMarks(cursor.block()) > 0, spanAround(cursor, cueMarks()).found()};
    for (int i = 0; i < 4; ++i)
        formatActions_[i]->setChecked(states[i]);
}

// The text size in steps an eighth apart (the typographic scale of the major second, 9:8), so every
// press looks like the same change at any size; Word's list jumps from 28 to 36, 48 and 72.
void MainWindow::stepTextSize(int direction) {
    if (!textSize_)
        return;
    static constexpr int steps[] = {18, 20, 22, 25, 28, 32, 35, 40, 45, 51, 57, 64, 72, 81, 91, 96};
    const int now = textSize_->value();
    int next = now;
    if (direction > 0) {
        for (const int step : steps)
            if (step > now) {
                next = step;
                break;
            }
    } else {
        for (auto it = std::rbegin(steps); it != std::rend(steps); ++it)
            if (*it < now) {
                next = *it;
                break;
            }
    }
    next = std::clamp(next, textSize_->minimum(), textSize_->maximum());
    if (next == now) {
        notify(direction > 0 ? tr("The text is as large as it gets.") : tr("The text is as small as it gets."));
        return;
    }
    textSize_->setValue(next); // Saved, and the same as in Settings.
    notify(tr("Text size %1").arg(QLocale().toString(next)));
}

void MainWindow::exportScript() {
    auto path = QFileDialog::getSaveFileName(this, tr("Export script"), ScriptLibrary::titleFor(script_->toPlainText()),
                                             tr("Text (*.txt);;Markdown (*.md)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const auto data = script_->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        QMessageBox::warning(this, tr("Cannot export"), tr("Check the destination and available disk space."));
    else
        notify(tr("Exported to %1.").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::deleteScript() {
    if (!persist_ || currentId_.isEmpty())
        return;
    const auto title = ScriptLibrary::titleFor(script_->toPlainText());
    if (!script_->toPlainText().trimmed().isEmpty() &&
        QMessageBox::question(this, tr("Delete script?"), tr("Delete “%1” from this device?").arg(title),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const auto removed = currentId_;
    autosave_.stop();
    currentId_.clear();
    library_.remove(removed);
    const auto remaining = library_.entries();
    if (remaining.isEmpty())
        newScript();
    else
        openScript(remaining.first().id);
    notify(tr("Deleted “%1”.").arg(title));
}

namespace {
QString sampleScript() {
    return MainWindow::tr("# Welcome\n\nThank you for joining today. [Smile]\n\n"
                          "Let us start with the question that matters most. What would make this project useful "
                          "to the people who depend on it?\n\n# Next steps\n\nWe can take this one step at a time. "
                          "[Pause] Listen carefully, check the question, and choose the words we want to share.");
}
} // namespace

void MainWindow::setDemoScript() {
    // Demonstrations and screenshots never touch the user's library.
    saveScript();
    persist_ = false;
    loading_ = true;
    script_->setPlainText(sampleScript());
    script_->spaceLines();
    loading_ = false;
    {
        const QSignalBlocker blockList(scriptList_);
        scriptList_->clear();
        scriptList_->addItem(tr("Sample script (not saved)"));
        scriptList_->setCurrentRow(0);
    }
    refreshWelcome();
    tracker_.setScript(script_->toPlainText());
    tracker_.seek(6);
    refreshReading();
}

void MainWindow::loadSample() {
    welcomeDismissed_ = true;
    script_->setPlainText(sampleScript());
    script_->spaceLines(); // Saved like any script: it is the person's to change.
    refreshWelcome();
    script_->setFocus();
}

bool MainWindow::voiceReady() const {
    if (!backend_ || !modelPath_)
        return false;
    if (backend_->currentData().toString() == QLatin1String("system"))
        return true;
    const auto model = modelPath_->text();
    return !model.isEmpty() && QFileInfo::exists(model);
}

QWidget *MainWindow::makeWelcome() {
    auto *page = new QWidget;
    page->setObjectName("welcome");
    page->setBackgroundRole(QPalette::Base);
    page->setAutoFillBackground(true);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(Design::Metric::space5, Design::Metric::space6, Design::Metric::space5,
                              Design::Metric::space6);
    auto *column = new QWidget;
    column->setMaximumWidth(fontMetrics().averageCharWidth() * 76);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Design::Metric::space3);
    auto *title = new QLabel(tr("Welcome to Kalipso"));
    title->setObjectName("welcomeTitle");
    title->setWordWrap(true);
    auto titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.9);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    auto *lead = new QLabel(tr("A teleprompter that follows your voice and keeps your place. Everything stays on "
                               "this computer."));
    lead->setWordWrap(true);
    lead->setProperty("role", "muted");
    layout->addWidget(title);
    layout->addWidget(lead);
    layout->addSpacing(Design::Metric::space5);

    // Three steps, each with what it is for and the one button that does it.
    const auto step = [&](int number, const QString &heading, QLabel *detail, const QList<QPushButton *> &buttons) {
        auto *row = new QWidget;
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(0, Design::Metric::space3, 0, Design::Metric::space3);
        grid->setHorizontalSpacing(Design::Metric::space4);
        grid->setVerticalSpacing(Design::Metric::space1);
        auto *badge = new StepBadge(number);
        badge->setObjectName("welcomeStep");
        const int size = fontMetrics().height() * 2;
        badge->setFixedSize(size, size);
        steps_.append(badge);
        auto *name = new QLabel(heading);
        name->setWordWrap(true);
        auto nameFont = name->font();
        nameFont.setPointSizeF(nameFont.pointSizeF() * 1.15);
        nameFont.setWeight(QFont::DemiBold);
        name->setFont(nameFont);
        detail->setWordWrap(true);
        detail->setProperty("role", "muted");
        auto *actions = new FlowLayout;
        for (auto *button : buttons)
            actions->addWidget(button);
        grid->addWidget(badge, 0, 0, 3, 1, Qt::AlignTop);
        grid->addWidget(name, 0, 1);
        grid->addWidget(detail, 1, 1);
        grid->addLayout(actions, 2, 1);
        grid->setColumnStretch(1, 1);
        layout->addWidget(row);
    };
    welcomeVoice_ = new QLabel;
    welcomeVoiceButton_ = new AccessibleButton(tr("Choose language"));
    welcomeVoiceButton_->setObjectName("welcomeLanguage");
    connect(welcomeVoiceButton_, &QPushButton::clicked, this, [this] { showSettings(voiceLanguage_); });
    step(1, tr("Choose the language you speak"), welcomeVoice_, {welcomeVoiceButton_});

    auto *test = new AccessibleButton(tr("Test the microphone"));
    test->setObjectName("welcomeMicrophone");
    connect(test, &QPushButton::clicked, this, [this] {
        showSettings(testMicrophone_);
        if (!microphoneTestEnd_.isActive())
            startMicrophoneTest();
    });
    step(2, tr("Check that Kalipso hears you"),
         new QLabel(tr("Speak for a moment: a bar moves with your voice. Nothing is recorded or sent.")), {test});

    auto *write = new AccessibleButton(tr("Write"));
    write->setObjectName("welcomeWrite");
    connect(write, &QPushButton::clicked, this, [this] {
        welcomeDismissed_ = true;
        refreshWelcome();
        script_->setFocus();
    });
    auto *import = new AccessibleButton(tr("Import…"));
    import->setObjectName("welcomeImport");
    connect(import, &QPushButton::clicked, this, &MainWindow::importScript);
    auto *sample = new AccessibleButton(tr("Try a sample"));
    sample->setObjectName("welcomeSample");
    sample->setProperty("quiet", true);
    connect(sample, &QPushButton::clicked, this, &MainWindow::loadSample);
    step(3, tr("Write or import your script"),
         new QLabel(tr("Paste your text, or import a document, Markdown or the speaker notes of a presentation. "
                       "To read, press Under the camera: the window goes small, at the top of the screen.")),
         {write, import, sample});

    // The column takes the width it has up to a comfortable measure, so the notes wrap at it.
    auto *centred = new QHBoxLayout;
    centred->addStretch(1);
    centred->addWidget(column, 100);
    centred->addStretch(1);
    outer->addStretch(1);
    outer->addLayout(centred);
    outer->addStretch(2);
    // It scrolls by itself when the window is low, rather than making the whole window scroll.
    auto *scroll = new QScrollArea;
    scroll->setObjectName("welcomeScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setBackgroundRole(QPalette::Base);
    scroll->setWidget(page);
    return scroll;
}

void MainWindow::refreshWelcome() {
    if (!welcome_ || !modelPath_ || !backend_)
        return;
    const bool first =
        !welcomeDismissed_ && persist_ && script_->toPlainText().trimmed().isEmpty() && library_.entries().size() <= 1;
    prompter_->showWelcome(first && !script_->hasFocus());
    const bool ready = voiceReady();
    const auto name = spokenName(speechLanguage());
    welcomeVoice_->setText(ready ? tr("Ready: Kalipso listens in %1, on this computer.").arg(name)
                                 : tr("Kalipso downloads the voice model for it once, about 50 MB."));
    welcomeVoiceButton_->setText(ready ? tr("Change language") : tr("Choose language"));
    if (!steps_.isEmpty())
        steps_.first()->setDone(ready);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    // KWin answers the change of frame (and its own moves) with the old size, sometimes more than
    // once: for a moment the size just chosen wins every time, and nothing is saved.
    if (wantedSize_.isValid() && wanted_.isValid() && wanted_.elapsed() < kSizeSettles) {
        if (event->size() != wantedSize_ && !placing_)
            QTimer::singleShot(0, this, [this] {
                if (wantedSize_.isValid() && wanted_.elapsed() < kSizeSettles && size() != wantedSize_)
                    resizeFor(wantedSize_, false);
            });
        return;
    }
    saveReaderGeometry(true);
}

// --- Reading ----------------------------------------------------------------

void MainWindow::startSteadyScrolling() {
    pauseRunning();
    mode_ = ReadingMode::Steady;
    prompter_->setMode(mode_);
    steady_.setInterval(60000 / wordsPerMinute_);
    steady_.start();
    refreshRunning();
}

// Under the camera: small, without a frame, above the other windows, at the top of the screen.
// Reading goes on as it was; the text is the same.
void MainWindow::placeUnderCamera() {
    if (!underCamera_) {
        saveScript();
        underCamera_ = true;
        prompter_->setUnderCamera(true);
        updateWindowTitle(); // KWin takes the frame off first; then the window takes the reader's size.
        placeWindow(true);
    }
    refreshReading();
    prompter_->revealControls();
    if (!isVisible())
        show();
    raise();
    activateWindow();
    prompter_->text->setFocus();
}

// Back to the size and place the window had before, with its frame: room to write.
void MainWindow::expand() {
    if (!underCamera_)
        return;
    underCamera_ = false;
    prompter_->setUnderCamera(false);
    updateWindowTitle();
    placeWindow(false);
    prompter_->text->setFocus();
}

namespace {
bool wayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }
} // namespace

// While reading, the window has no frame, stays above the others and sits at the top of the screen,
// under the camera, at the reader's size. Kalipso sets the size; with Kalipso's KWin script
// (Plasma) KWin does the rest when the title says so. Elsewhere the window does what its platform
// allows: on Wayland it cannot choose its place.
void MainWindow::placeWindow(bool reading) {
    prompter_->setResizableEdges(reading);
    if (isVisible() || reading)
        CaptureExclusion::apply(this, reading && captureExcluded_);
    const bool kwin = KWinPlacement::active();
    constexpr auto readerFlags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
    placing_ = true;
    const bool shown = isVisible();
    if (reading) {
        writingGeometry_ = normalGeometry();
        writingMaximized_ = isMaximized();
        if (!writingMaximized_ && !ReaderPage::isRoomy(writingGeometry_.size(), Qt::WindowNoState))
            writingGeometry_ = {}; // Expanding from here gives the window room to write.
        if (writingMaximized_)
            showNormal(); // A maximized window cannot become the reader.
    }
    if (!kwin)
        setWindowFlags(reading ? windowFlags() | readerFlags : windowFlags() & ~readerFlags);
    if (reading) {
        const auto name = screen_->currentData().toString();
        QScreen *target = QGuiApplication::primaryScreen();
        for (auto *screen : QGuiApplication::screens())
            if (screen->name() == name)
                target = screen;
        if (target) {
            winId();
            windowHandle()->setScreen(target);
            const auto available = target->availableGeometry();
            const auto saved = QSettings().value("reader/geometry/" + target->name()).toRect();
            if (!kwin && saved.isValid() && available.contains(saved.center()) && !wayland()) {
                setGeometry(saved);
            } else {
                // The size the person gave the reader, or about two fifths of the screen: five or six
                // lines at the default size.
                const int extra =
                    std::max(0, qRound((prompter_->font().pointSizeF() - QApplication::font().pointSizeF()) * 12));
                QSize size(std::clamp(available.width() * 2 / 5, 560, 960),
                           std::clamp(available.height() * 2 / 5, 360, 600) + extra);
                if (const auto chosen = QSettings().value("reader/size").toSize(); chosen.isValid())
                    size = chosen;
                size = size.boundedTo(available.size() - QSize(32, 32)).expandedTo(minimumSize());
                resizeFor(size);
                if (!kwin)
                    move(available.center().x() - width() / 2, available.top()); // Under the camera.
            }
        }
    } else if (writingGeometry_.isValid()) {
        if (kwin) {
            resizeFor(writingGeometry_.size()); // KWin puts it back in its place.
        } else {
            // The size held for the reader a moment ago must not pull the window back to it.
            wantedSize_ = writingGeometry_.size();
            wanted_.start();
            setGeometry(writingGeometry_);
        }
    } else if (auto *screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen()) {
        // It was small before going under the camera: it gets the room of a writing window, or the
        // whole screen when the screen is small.
        const auto available = screen->availableGeometry();
        const QSize room(std::min(1100, available.width() * 4 / 5), std::min(760, available.height() * 17 / 20));
        writingMaximized_ = !ReaderPage::isRoomy(room, Qt::WindowNoState);
        resizeFor(room);
        if (!kwin)
            move(available.center() - QPoint(room.width(), room.height()) / 2);
    }
    placing_ = false;
    if (!kwin && shown) {
        show();
        raise();
    }
    if (!reading && writingMaximized_) {
        wantedSize_ = {}; // Maximized: the screen decides the size.
        showMaximized();
    }
}

void MainWindow::keepReaderOnScreen(QScreen *gone) {
    if (!underCamera_ || KWinPlacement::active())
        return; // Writing, Qt and the desktop keep the window on a screen; KWin's script does too.
    // The chosen monitor if it is still there, otherwise the main one, otherwise any other.
    const auto chosen = screen_ ? screen_->currentData().toString() : QString();
    QScreen *target = nullptr;
    for (auto *screen : QGuiApplication::screens())
        if (screen != gone && (!target || screen->name() == chosen ||
                               (screen == QGuiApplication::primaryScreen() && target->name() != chosen)))
            target = screen;
    if (!target || !windowHandle())
        return;
    const auto available = target->availableGeometry();
    if (windowHandle()->screen() != gone && available.contains(frameGeometry().center()))
        return;
    placing_ = true;
    windowHandle()->setScreen(target);
    resizeFor(size().boundedTo(available.size() - QSize(32, 32)).expandedTo(minimumSize()));
    if (!wayland())
        move(available.center().x() - width() / 2, available.top()); // Under the camera again.
    placing_ = false;
}

void MainWindow::resizeFor(QSize size, bool restart) {
    wantedSize_ = size;
    if (restart || !wanted_.isValid())
        wanted_.start();
    const bool was = placing_;
    placing_ = true;
    resize(size);
    placing_ = was;
}

void MainWindow::saveReaderGeometry(bool resized) {
    if (!underCamera_ || placing_ || !isVisible() || !windowHandle())
        return;
    if (resized)
        QSettings().setValue("reader/size", size());
    if (wayland() || KWinPlacement::active())
        return; // Only the window manager knows where it is.
    if (auto *screen = windowHandle()->screen())
        QSettings().setValue("reader/geometry/" + screen->name(), geometry());
}

void MainWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    saveReaderGeometry(false);
}

// "Script — Kalipso", as the desktop's document apps. While reading it starts with the invisible
// mark that tells Kalipso's KWin script to frame and place the window as the reader.
void MainWindow::updateWindowTitle() {
    const auto title = ScriptLibrary::titleFor(script_->toPlainText());
    auto text = title.isEmpty() ? tr("Kalipso") : tr("%1 — Kalipso").arg(title);
    // With the second mark, the person lets screen sharing show the reader.
    if (underCamera_ && !captureExcluded_)
        text.prepend(QChar(KWinPlacement::shareableMark));
    if (underCamera_)
        text.prepend(QChar(KWinPlacement::readingMark));
    if (windowTitle() != text)
        setWindowTitle(text);
}

void MainWindow::setCaptureExcluded(bool excluded) {
    captureExcluded_ = excluded;
    if (underCamera_)
        CaptureExclusion::apply(this, excluded);
    updateWindowTitle(); // Kalipso's KWin script follows the choice too.
    // Said to assistive technology; a tooltip over the whole reader would cover the text whenever the
    // pointer rests on it.
    prompter_->setAccessibleDescription(
        excluded ? tr("Kalipso asked the system to hide this reader from screen sharing. Check the view "
                      "from another participant.")
                 : tr("Screen sharing may include this reader."));
}

void MainWindow::seekWord(int word) {
    tracker_.seek(word);
    if (session_.active())
        session_.rebase(tracker_.position(), QDateTime::currentMSecsSinceEpoch());
    speech_.setScript(script_->toPlainText(), tracker_.position());
    waitForNewUtterance_ = speech_.active();
    refreshReading();
}

void MainWindow::refreshReading() {
    // The status depends on the position (time left), so it is refreshed here too. Without a
    // language to listen in, the start button opens Settings at the choice.
    prompter_->setBlocked(mode_ == ReadingMode::Voice && !running() && !voiceReady() &&
                          SpeechCatalog::downloadsSupported());
    prompter_->setRunning(running(), statusText());
    // Before the first word, the reading line shows where the script starts: after any front
    // matter, which is metadata and never read.
    int anchor = tracker_.character();
    if (tracker_.position() < 0)
        if (const int meta = ScriptTracker::frontMatter(scriptText_).length; meta > 0)
            anchor = std::min(int(scriptText_.size()), meta + 1);
    prompter_->highlight(anchor, tracker_.length());
    prompter_->setProgress(tracker_.position() + 1, tracker_.count());
    if (persist_ && !currentId_.isEmpty())
        library_.setPosition(currentId_, tracker_.position() >= 0 ? tracker_.character() : -1);
    if (remote_ && remote_->active())
        remote_->setState({{"mode", modeKey(mode_)},
                           {"running", running()},
                           {"position", tracker_.position() + 1},
                           {"total", tracker_.count()},
                           {"speed", wordsPerMinute_}});
    if (waitForNewUtterance_) {
        positionText_ = tr("Resumes after your next pause");
        prompter_->setStatus(statusText());
        return;
    }
    // Reading time of the whole script at the steady speed, as a rehearsal guide.
    const int minutes = (tracker_.count() + wordsPerMinute_ - 1) / wordsPerMinute_;
    const auto length = tracker_.count() == 0 ? QString()
                        : minutes <= 1        ? tr("under a minute")
                                              : tr("about %n min", nullptr, minutes);
    // Nothing written yet: the status says what the reader is doing instead.
    if (tracker_.count() == 0) {
        positionText_.clear();
        prompter_->setStatus(statusText());
        return;
    }
    // Before reading: how long the script is. Once reading started: where it stopped.
    const auto where = tracker_.position() < 0 ? tr("%n word(s)", nullptr, tracker_.count())
                                               : tr("Word %1 of %2")
                                                     .arg(QLocale().toString(tracker_.position() + 1),
                                                          QLocale().toString(tracker_.count()));
    positionText_ = length.isEmpty() ? where : tr("%1 · %2").arg(where, length);
    prompter_->setStatus(statusText());
}

void MainWindow::setMode(ReadingMode mode) {
    if (running())
        pauseRunning();
    mode_ = mode;
    speechError_.clear();
    QSettings().setValue("reading/mode", modeKey(mode));
    prompter_->setMode(mode);
    refreshRunning();
}

bool MainWindow::running() const {
    if (mode_ == ReadingMode::Voice)
        return speech_.active();
    return steady_.isActive() || countdown_.isActive();
}

void MainWindow::toggleRunning() {
    if (running())
        pauseRunning();
    else
        startRunning();
}

void MainWindow::startRunning() {
    if (mode_ == ReadingMode::Manual)
        return;
    if (mode_ == ReadingMode::Voice)
        return startListening();
    if (tracker_.count() == 0)
        return;
    if (tracker_.position() >= tracker_.count() - 1)
        seekWord(-1);
    steady_.setInterval(60000 / wordsPerMinute_);
    countdownLeft_ = QSettings().value("reading/countdown", 3).toInt();
    if (countdownLeft_ > 0) {
        prompter_->showCountdown(countdownLeft_);
        countdown_.start();
    } else {
        steady_.start();
    }
    refreshRunning();
}

void MainWindow::pauseRunning() {
    countdown_.stop();
    countdownLeft_ = 0;
    prompter_->showCountdown(0);
    steady_.stop();
    if (speech_.active()) {
        speech_.stop();
        notify(tr("Paused · microphone off"));
    }
    refreshRunning();
}

void MainWindow::steadyTick() {
    if (tracker_.position() >= tracker_.count() - 1) {
        pauseRunning();
        prompter_->setStatus(tr("End of script"));
        return;
    }
    tracker_.seek(tracker_.position() + 1);
    refreshRunning(); // Also refreshes the time left.
}

void MainWindow::changeSpeed(int delta) {
    wordsPerMinute_ = std::clamp(wordsPerMinute_ + delta, 40, 400);
    // Each script remembers its own pace; the general setting is the default for new ones.
    QSettings().setValue("reading/speed", wordsPerMinute_);
    if (persist_ && !currentId_.isEmpty())
        library_.setSpeed(currentId_, wordsPerMinute_);
    prompter_->setSpeed(wordsPerMinute_);
    steady_.setInterval(60000 / wordsPerMinute_); // Also applies after a countdown.
    refreshReading();
}

// Impress showed another slide: continue just before the first line of its notes.
// Matching the text, not the slide number, keeps working after edits and in any language.
void MainWindow::followSlide(int number, const QString &notes) {
    const auto lines = notes.split(u'\n');
    const auto first =
        std::find_if(lines.begin(), lines.end(), [](const QString &line) { return !line.trimmed().isEmpty(); });
    if (first == lines.end()) {
        notify(tr("Impress · slide %1 has no notes.").arg(QLocale().toString(number)));
        return;
    }
    const int at = int(script_->toPlainText().indexOf(first->trimmed()));
    if (at < 0) {
        notify(tr("Impress · the notes of slide %1 are not in this script.").arg(QLocale().toString(number)));
        return;
    }
    if (at == 0)
        tracker_.seek(-1);
    else
        tracker_.seekCharacter(at - 1);
    seekWord(tracker_.position());
    notify(tr("Following Impress · slide %1").arg(QLocale().toString(number)));
}

void MainWindow::jumpSection(int direction) {
    if (const int target = tracker_.sectionTarget(direction); target >= 0) {
        tracker_.seekCharacter(target);
        seekWord(tracker_.position());
    }
}

QString MainWindow::statusText() const {
    const bool active = running();
    // While writing: how long the script is, or where reading stopped, and its reading time.
    if (prompter_->editing() && !active && speechError_.isEmpty() && !positionText_.isEmpty())
        return positionText_;
    QString status;
    switch (mode_) {
    case ReadingMode::Manual: status = tr("Manual · microphone off"); break;
    case ReadingMode::Steady: {
        // Time left at the current speed, rounded up to whole minutes.
        const int wordsLeft = std::max(0, tracker_.count() - tracker_.position() - 1);
        const int minutes = (wordsLeft + wordsPerMinute_ - 1) / wordsPerMinute_;
        const auto left = wordsLeft == 0 ? tr("end of script")
                          : minutes <= 1 ? tr("under a minute left")
                                         : tr("%n min left", nullptr, minutes);
        // Running or paused, the button says it; the status says how long is left.
        status = countdown_.isActive() ? tr("Starting…") : left;
        break;
    }
    case ReadingMode::Voice:
        status = !active && !speechError_.isEmpty() ? speechError_
                 : !active && !voiceReady()         ? tr("Microphone off · no language chosen yet")
                 : !active                          ? tr("Microphone off until you start")
                 : microphoneSilent_                ? speechStatus_
                 : tracker_.lost()                  ? tr("Off script · Kalipso catches up when you return to the text")
                 : speechStatus_.isEmpty()          ? tr("Listening")
                                                    : speechStatus_;
        break;
    }
    return status;
}

void MainWindow::refreshRunning() { refreshReading(); }

void MainWindow::showSettings(QWidget *field) {
    if (!settings_->isVisible())
        refreshMicrophones();
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
    if (!field)
        return;
    for (int i = 0; i < settingsStack_->count(); ++i)
        if (auto *scroll = qobject_cast<QScrollArea *>(settingsStack_->widget(i));
            scroll && scroll->widget()->isAncestorOf(field)) {
            settingsNav_->setCurrentRow(i);
            scroll->ensureWidgetVisible(field, 0, Design::Metric::space8);
        }
    field->setFocus(Qt::OtherFocusReason);
}

void MainWindow::startListening() {
    saveSettings();
    systemCheck_.stop();
    installingSystem_ = false;
    waitForNewUtterance_ = false;
    speechStatus_.clear();
    speechError_.clear();
    microphoneSilent_ = false;
    if (microphoneTestEnd_.isActive())
        stopMicrophoneTest(false); // Listening takes the microphone over.
    speech_.setScript(script_->toPlainText(), tracker_.position());
    SpeechRequest request;
    request.backend = backend_->currentData().toString();
    request.library = speechLibrary_->text();
    request.model = modelPath_->text();
    request.microphone = request.backend == QLatin1String("system") ? QString() : microphone_->currentData().toString();
    request.language = speechLanguage();
    request.architecture = architecture_->currentIndex() + 2;
    // No language yet: say so in the reader too, and open Settings at the choice.
    if (request.backend != QLatin1String("system") && !QFileInfo(request.model).isDir() &&
        SpeechCatalog::downloadsSupported()) {
        speechError_ = tr("Choose the language you speak in Settings: Kalipso downloads it once.");
        notify(speechError_);
        refreshRunning();
        showSettings(voiceLanguage_); // Over the reader, which stays where it is.
        return;
    }
    speech_.start(request);
    if (speech_.active()) {
        checkSystem_->setEnabled(false);
        installSystem_->setEnabled(false);
        backend_->setEnabled(false);
        voiceLanguage_->setEnabled(false);
        language_->setEnabled(false);
    }
    refreshRunning();
}

void MainWindow::finishSession() {
    if (!session_.active())
        return;
    auto summary = session_.finish(QDateTime::currentMSecsSinceEpoch());
    if (summary.words < 5)
        return;
    summary.plannedWordsPerMinute = wordsPerMinute_; // The steady speed: what the rehearsal aims at.
    QString text = tr("Last session: %1 · %n word(s)", nullptr, summary.words).arg(duration(summary.durationMs));
    if (summary.wordsPerMinute > 0) {
        text += " · " + tr("%1 words/min").arg(QLocale().toString(qRound(summary.wordsPerMinute)));
        const int difference = SessionStats::paceAgainstPlan(summary);
        text += " " + (std::abs(difference) < 3 ? tr("(as planned)")
                       : difference > 0         ? tr("(%1% faster than planned)").arg(QLocale().toString(difference))
                                                : tr("(%1% slower than planned)").arg(QLocale().toString(-difference)));
    }
    text += " · " + tr("%n pause(s) over 3 s", nullptr, summary.longPauses);
    if (summary.longPauses)
        text += " " + tr("(longest %1)").arg(duration(summary.longestPauseMs));
    if (summary.fillers)
        text += " · " + tr("%n filler word(s) recognized", nullptr, summary.fillers);
    notify(text);
    lastSession_ = summary;
    lastSessionScript_ = script_->toPlainText();
    lastSessionAt_ = QDateTime::currentDateTime();
    reportAction_->setEnabled(true);
}

// --- Settings ---------------------------------------------------------------

QWidget *MainWindow::makeKeySettings() {
    auto *group = new SettingsSection(tr("Keys and pedals"));
    // Action, settings key, label and the built-in keys (shown while no key is chosen).
    const QList<std::tuple<ReaderAction, QString, QString, QList<int>>> actions{
        {ReaderAction::Toggle, "toggle", tr("Start or pause"), {Qt::Key_Space}},
        {ReaderAction::Next, "next", tr("Next word"), {Qt::Key_Right, Qt::Key_Down, Qt::Key_PageDown}},
        {ReaderAction::Previous, "previous", tr("Previous word"), {Qt::Key_Left, Qt::Key_Up, Qt::Key_PageUp}},
        {ReaderAction::NextSection, "section-next", tr("Next section"), {}},
        {ReaderAction::PreviousSection, "section-previous", tr("Previous section"), {}},
        {ReaderAction::Faster, "faster", tr("Faster"), {Qt::Key_Plus}},
        {ReaderAction::Slower, "slower", tr("Slower"), {Qt::Key_Minus}},
        {ReaderAction::Reset, "reset", tr("Back to the start"), {Qt::Key_Home}}};
    QSettings settings;
    QList<QKeySequenceEdit *> edits;
    for (const auto &[action, id, label, builtIn] : actions) {
        auto *edit = new QKeySequenceEdit(
            QKeySequence::fromString(settings.value("keys/" + id).toString(), QKeySequence::PortableText));
        edit->setObjectName("key-" + id);
        edit->setProperty("keyId", id);
        edit->setProperty("action", int(action));
        edit->setClearButtonEnabled(true);
        edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        QStringList names;
        for (const int key : builtIn)
            names << QKeySequence(key).toString(QKeySequence::NativeText);
        if (auto *line = edit->findChild<QLineEdit *>())
            line->setPlaceholderText(names.isEmpty() ? tr("Press a key") : tr("Built in: %1").arg(names.join(", ")));
        group->form->addRow(label, edit);
        edits.append(edit);
    }
    auto *note = new QLabel(tr("Pedals and clickers usually send the arrows, Page Up/Down or Space, which work without "
                               "any setup. Choose a key here to add one for an action, or to give a built-in key "
                               "another action, for example the arrows to change speed. Escape always pauses."));
    note->setWordWrap(true);
    note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    note->setProperty("role", "muted");
    group->form->addRow(note);

    const auto save = [this, edits] {
        QSettings settings;
        QHash<int, ReaderAction> keys;
        for (auto *edit : edits) {
            const auto sequence = edit->keySequence();
            settings.setValue("keys/" + edit->property("keyId").toString(),
                              sequence.toString(QKeySequence::PortableText));
            if (!sequence.isEmpty())
                keys.insert(sequence[0].toCombined(), ReaderAction(edit->property("action").toInt()));
        }
        prompter_->setCustomKeys(keys);
    };
    for (auto *edit : edits) {
        connect(edit, &QKeySequenceEdit::editingFinished, this, [edit, edits, save] {
            // One key per action, as pedals send single keys; a key belongs to one action.
            auto sequence = edit->keySequence();
            if (sequence.count() > 1)
                edit->setKeySequence(sequence = QKeySequence(sequence[0]));
            for (auto *other : edits)
                if (other != edit && !sequence.isEmpty() && other->keySequence() == sequence)
                    other->clear();
            save();
        });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [save](const QKeySequence &sequence) {
            if (sequence.isEmpty()) // The clear button.
                save();
        });
    }
    save();
    return group;
}

QWidget *MainWindow::makeSettings() {
    // One page per subject, listed on the left like the desktop's own settings. The language you
    // speak comes first: voice following needs it before anything else.
    struct Page {
        QString name, icon;
        QWidget *content;
    };
    QList<Page> pages{{tr("Voice"), "audio-input-microphone", makeSpeechSettings()},
                      {tr("Reader"), "view-presentation", makeReadingSettings()}};
    pages.append({tr("Appearance"), "preferences-desktop-theme", appearanceSettings_});
    pages.append({tr("Keys and pedals"), "input-keyboard", makeKeySettings()});
    pages.append({tr("Phone"), "smartphone", makeRemoteSettings()});

    QList<QWidget *> contents;
    for (const auto &page : pages) {
        auto *content = new QWidget;
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(Design::Metric::space5, Design::Metric::space5, Design::Metric::space5,
                                   Design::Metric::space4);
        layout->setSpacing(0);
        layout->addWidget(page.content);
        layout->addStretch();
        contents.append(content);
    }
    for (auto *content : contents)
        for (auto *form : content->findChildren<QFormLayout *>())
            for (int row = 0; row < form->rowCount(); ++row) {
                auto *labelItem = form->itemAt(row, QFormLayout::LabelRole);
                auto *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
                if (!labelItem || !fieldItem)
                    continue;
                auto *label = qobject_cast<QLabel *>(labelItem->widget());
                auto *input = fieldItem->widget();
                if (!label || !input)
                    continue;
                label->setWordWrap(true);
                label->setMinimumWidth(0);
                label->setBuddy(input);
                input->setAccessibleName(label->text());
            }
    // One label column for every page, so fields line up when moving between them.
    QList<SettingsSection *> sections;
    for (auto *content : contents)
        for (auto *widget : content->findChildren<QWidget *>(QStringLiteral("settingsSection")))
            sections.append(static_cast<SettingsSection *>(widget));
    int labelWidth = 0;
    for (auto *section : sections)
        labelWidth = std::max(labelWidth, section->labelWidth());
    for (auto *section : sections)
        section->setLabelWidth(labelWidth);

    auto *root = new QWidget;
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto *nav = new QListWidget;
    nav->setObjectName("settingsPages");
    nav->setAccessibleName(tr("Settings pages"));
    nav->setFrameShape(QFrame::NoFrame);
    nav->setBackgroundRole(QPalette::Window);
    nav->viewport()->setBackgroundRole(QPalette::Window);
    nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav->setIconSize({22, 22});
    nav->setSpacing(2);
    auto *picker = new SettingsComboBox; // The same pages, above the page, when the window is narrow.
    picker->setObjectName("settingsPagePicker");
    picker->setAccessibleName(tr("Settings pages"));
    auto *stack = new QStackedWidget;
    stack->setObjectName("settingsStack");
    for (int i = 0; i < pages.size(); ++i) {
        const auto icon = Accessibility::native() ? QIcon::fromTheme(pages[i].icon) : QIcon();
        auto *item = new QListWidgetItem(icon, pages[i].name, nav);
        item->setSizeHint({0, fontMetrics().height() + 2 * Design::Metric::space3});
        picker->addItem(icon, pages[i].name);
        auto *scroll = new QScrollArea;
        scroll->setObjectName("settingsScroll");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(contents[i]);
        for (auto *section : contents[i]->findChildren<QWidget *>(QStringLiteral("settingsSection")))
            static_cast<SettingsSection *>(section)->fitViewport(scroll->viewport());
        scroll->setMinimumSize(0, 0);
        stack->addWidget(scroll);
    }
    settingsNav_ = nav;
    fitSettingsPages();
    connect(nav, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    connect(nav, &QListWidget::currentRowChanged, picker, &QComboBox::setCurrentIndex);
    connect(picker, &QComboBox::currentIndexChanged, nav, [nav](int row) { nav->setCurrentRow(row); });
    nav->setCurrentRow(0);
    auto *line = new QFrame;
    line->setObjectName("settingsDivider");
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    auto *pickerRow = new QWidget;
    auto *pickerLayout = new QHBoxLayout(pickerRow);
    pickerLayout->setContentsMargins(Design::Metric::space5, Design::Metric::space4, Design::Metric::space5, 0);
    pickerLayout->addWidget(picker);
    rightLayout->addWidget(pickerRow);
    rightLayout->addWidget(stack, 1);
    rootLayout->addWidget(nav);
    rootLayout->addWidget(line);
    rootLayout->addWidget(right, 1);
    settingsNav_ = nav;
    settingsStack_ = stack;
    // Narrow: the list gives way to the picker above the page.
    settingsAdapt_ = [nav, line, pickerRow](int width) {
        const bool wide = width >= nav->width() * 3;
        nav->setVisible(wide);
        line->setVisible(wide);
        pickerRow->setVisible(!wide);
    };
    root->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    return root;
}

QWidget *MainWindow::makeReadingSettings() {
    QSettings settings;
    auto *group = new SettingsSection(tr("Reader"));
    auto *form = group->form;

    screen_ = new SettingsComboBox;
    screen_->setObjectName("readerScreen");
    refreshScreens();
    connect(qApp, &QGuiApplication::screenAdded, this, &MainWindow::refreshScreens);
    connect(qApp, &QGuiApplication::screenRemoved, this, &MainWindow::refreshScreens);
    connect(qApp, &QGuiApplication::screenRemoved, this, &MainWindow::keepReaderOnScreen);
    connect(screen_, &QComboBox::activated, this,
            [this] { QSettings().setValue("reading/screen", screen_->currentData().toString()); });
    form->addRow(tr("Monitor"), screen_);
    readerForm_ = form;
    refreshScreens();

    auto *size = new SettingsSpinBox;
    textSize_ = size;
    size->setObjectName("readerTextSize");
    size->setRange(18, 96);
    size->setValue(settings.value("reading/fontSize", 28).toInt());
    prompter_->setTextSize(size->value());
    connect(size, &QSpinBox::valueChanged, this, [this](int value) {
        prompter_->setTextSize(value);
        QSettings().setValue("reading/fontSize", value);
    });
    form->addRow(tr("Text size"), size);

    auto *font = new SettingsComboBox;
    font->setObjectName("readerFont");
    font->addItem(tr("Interface font"), QString());
    for (const auto &family : QFontDatabase::families())
        if (!QFontDatabase::isPrivateFamily(family) &&
            QFontDatabase::writingSystems(family).contains(QFontDatabase::Latin))
            font->addItem(family, family);
    font->setToolTip(tr("Any installed font, for example one designed for easier reading such as Atkinson "
                        "Hyperlegible or OpenDyslexic."));
    const auto savedFont = settings.value("reading/fontFamily").toString();
    font->setCurrentIndex(std::max(0, font->findData(savedFont)));
    prompter_->setTextFont(font->currentData().toString());
    connect(font, &QComboBox::activated, this, [this, font] {
        prompter_->setTextFont(font->currentData().toString());
        QSettings().setValue("reading/fontFamily", font->currentData().toString());
    });
    form->addRow(tr("Font"), font);

    auto *spacing = new SettingsComboBox;
    spacing->setObjectName("readerLineSpacing");
    spacing->addItem(tr("Normal"), 100);
    spacing->addItem(tr("Relaxed"), 125);
    spacing->addItem(tr("Wide"), 150);
    spacing->setCurrentIndex(std::max(0, spacing->findData(settings.value("reading/lineSpacing", 100).toInt())));
    prompter_->setLineSpacing(spacing->currentData().toInt());
    connect(spacing, &QComboBox::activated, this, [this, spacing] {
        prompter_->setLineSpacing(spacing->currentData().toInt());
        QSettings().setValue("reading/lineSpacing", spacing->currentData().toInt());
    });
    form->addRow(tr("Line spacing"), spacing);

    auto *line = new SettingsComboBox;
    line->setObjectName("readingLine");
    line->addItem(tr("Top"), 0.12);
    line->addItem(tr("Upper third"), 0.3);
    line->addItem(tr("Center"), 0.5);
    line->setCurrentIndex(std::max(0, line->findData(settings.value("reading/line", 0.3).toDouble())));
    prompter_->setReadingLine(line->currentData().toDouble());
    connect(line, &QComboBox::currentIndexChanged, this, [this, line] {
        prompter_->setReadingLine(line->currentData().toDouble());
        QSettings().setValue("reading/line", line->currentData());
    });
    form->addRow(tr("Current line position"), line);

    auto *mirror = new SettingsComboBox;
    mirror->setObjectName("readerMirror");
    mirror->addItem(tr("Off"), int(Mirror::None));
    mirror->addItem(tr("Horizontal (beam-splitter glass)"), int(Mirror::Horizontal));
    mirror->addItem(tr("Vertical"), int(Mirror::Vertical));
    mirror->addItem(tr("Horizontal and vertical"), int(Mirror::Both));
    mirror->setCurrentIndex(std::max(0, mirror->findData(settings.value("reading/mirror", 0).toInt())));
    prompter_->setMirror(Mirror(mirror->currentData().toInt()));
    connect(mirror, &QComboBox::currentIndexChanged, this, [this, mirror] {
        prompter_->setMirror(Mirror(mirror->currentData().toInt()));
        QSettings().setValue("reading/mirror", mirror->currentData());
    });
    form->addRow(tr("Mirror text"), mirror);

    wordsPerMinute_ = std::clamp(settings.value("reading/speed", 150).toInt(), 40, 400);
    auto *countdown = new SettingsComboBox;
    countdown->setObjectName("readerCountdown");
    countdown->addItem(tr("Off"), 0);
    countdown->addItem(tr("3 seconds"), 3);
    countdown->addItem(tr("5 seconds"), 5);
    countdown->setCurrentIndex(std::max(0, countdown->findData(settings.value("reading/countdown", 3).toInt())));
    connect(countdown, &QComboBox::currentIndexChanged, this,
            [countdown] { QSettings().setValue("reading/countdown", countdown->currentData()); });
    form->addRow(tr("Countdown before steady scrolling"), countdown);

    auto *appearanceGroup = new SettingsSection(tr("Appearance"));
    form = appearanceGroup->form;
    auto *language = new SettingsComboBox;
    language->setObjectName("interfaceLanguage");
    language->addItem(tr("System language"), QString());
    for (const auto &entry : InterfaceLanguages::all)
        language->addItem(QString::fromUtf8(entry.name), QString::fromLatin1(entry.code));
    language->setCurrentIndex(std::max(0, language->findData(settings.value("appearance/language").toString())));
    connect(language, &QComboBox::currentIndexChanged, this, [this, language] {
        QSettings().setValue("appearance/language", language->currentData());
        notify(tr("The interface language changes the next time Kalipso starts."));
    });
    form->addRow(tr("Interface language"), language);
    form->addRow(note(tr("The interface language changes the next time Kalipso starts.")));
    if (!InterfaceLanguages::select(settings.value("appearance/language").toString()).reviewed)
        form->addRow(tr("Translations"), note(tr("Generated translation; native-speaker review pending.")));
    form->addRow(note(tr("Speech recognition is available only for the languages shown in Voice settings.")));

    auto *appearance = new SettingsComboBox;
    appearance->setObjectName("applicationTheme");
    appearance->addItem(tr("Like the system"), "system");
    appearance->addItem(tr("Light"), "light");
    appearance->addItem(tr("Dark"), "dark");
    appearance->setCurrentIndex(std::max(0, appearance->findData(settings.value("appearance/theme", "system"))));
    connect(appearance, &QComboBox::currentIndexChanged, this, [this, appearance] {
        QSettings().setValue("appearance/theme", appearance->currentData().toString());
        applyTheme();
    });
    // The window is the reader, in the reader's colours: this is for Settings and the dialogs.
    form->addRow(tr("Settings and dialogs"), appearance);

    auto *readerTheme = new SettingsComboBox;
    readerTheme->setObjectName("readerTheme");
    readerTheme->addItem(tr("Dark"), "dark");
    readerTheme->addItem(tr("Light"), "light");
    readerTheme->addItem(tr("Automatic screen colors"), "auto");
    const auto savedReader = settings.value("reading/theme", "dark").toString();
    readerTheme->setCurrentIndex(std::max(0, readerTheme->findData(savedReader)));
    prompter_->setReaderTheme(savedReader);
    connect(readerTheme, &QComboBox::currentIndexChanged, this, [this, readerTheme] {
        const auto theme = readerTheme->currentData().toString();
        prompter_->setReaderTheme(theme);
        QSettings().setValue("reading/theme", theme);
    });
    form->addRow(tr("Kalipso window"), readerTheme);
    auto *colorSource = new StatusLabel(prompter_->colorSource());
    form->addRow(colorSource);
    connect(prompter_, &ReaderPage::colorStatus, colorSource, &StatusLabel::setText);
    if (prompter_->usesWindowPortal()) {
        form->addRow(note(tr("The reader can take its colors from a window you choose, even while it covers it. "
                             "Nothing is saved or sent.")));
        auto *choose = new AccessibleButton(tr("Choose background window…"));
        choose->setObjectName("chooseBackgroundWindow");
        auto *stop = new AccessibleButton(tr("Stop window sharing"));
        stop->setObjectName("stopWindowSharing");
        form->addRow(choose);
        form->addRow(stop);
        stop->hide(); // Offered only once a window is shared.
        connect(choose, &QPushButton::clicked, this, [this, readerTheme, stop] {
            readerTheme->setCurrentIndex(readerTheme->findData("auto"));
            prompter_->chooseBackgroundWindow();
            stop->show();
        });
        connect(stop, &QPushButton::clicked, prompter_, &ReaderPage::stopWindowSharing);
        connect(stop, &QPushButton::clicked, stop, &QWidget::hide);
        connect(readerTheme, &QComboBox::currentIndexChanged, stop, [readerTheme, stop] {
            if (readerTheme->currentData().toString() != QLatin1String("auto"))
                stop->hide(); // Other colours end the sharing.
        });
    }

    auto *scale = new SettingsComboBox;
    scale->setObjectName("interfaceTextSize");
    scale->addItem("100%", 1.0);
    scale->addItem("150%", 1.5);
    scale->addItem("200%", 2.0);
    form->addRow(tr("Interface text size"), scale);
    connect(scale, &QComboBox::currentIndexChanged, this, [this, scale] {
        const double value = scale->currentData().toDouble();
        applyInterfaceScale(value);
        QSettings().setValue("appearance/textScale", value);
    });
    const double savedScale = settings.value("appearance/textScale", 1.0).toDouble();
    if (savedScale != 1.0)
        QTimer::singleShot(0, this, [scale, savedScale] {
            const int index = scale->findData(savedScale);
            if (index >= 0 && scale->currentIndex() == 0)
                scale->setCurrentIndex(index);
        });

    auto *fadeRow = new WrappingCheckBox(tr("Hide the reader's controls while reading"));
    fadeRow->box->setObjectName("autoHideControls");
    fadeRow->box->setChecked(settings.value("reading/autoHideControls", true).toBool());
    prompter_->setAutoHideControls(fadeRow->box->isChecked());
    connect(fadeRow->box, &QCheckBox::toggled, this, [this](bool checked) {
        prompter_->setAutoHideControls(checked);
        QSettings().setValue("reading/autoHideControls", checked);
    });
    group->form->addRow(fadeRow);

    auto *hiddenRow = new WrappingCheckBox(tr("Hide the reader from screen sharing and recordings"));
    auto *hidden = hiddenRow->box;
    hidden->setObjectName("excludeFromCapture");
    // Windows hides the window itself; on Plasma, Kalipso's KWin script asks KWin to, under the camera.
    if (CaptureExclusion::supported() || KWinPlacement::active()) {
        hidden->setChecked(settings.value("reading/excludeFromCapture", true).toBool());
        setCaptureExcluded(hidden->isChecked());
        connect(hidden, &QCheckBox::toggled, this, [this](bool checked) {
            setCaptureExcluded(checked);
            QSettings().setValue("reading/excludeFromCapture", checked);
        });
        group->form->addRow(hiddenRow);
        group->form->addRow(note(
            CaptureExclusion::supported()
                ? tr("Screen sharing apps that use the system capture service leave the reader out. Check the view "
                     "from another participant before relying on it.")
                : tr("Under the camera, Plasma 6.6 or later leaves the reader out of screen sharing. Check the view "
                     "from another participant before relying on it.")));
    } else {
        hiddenRow->deleteLater();
        group->form->addRow(
            note(tr("Others may see the reader when you share the whole screen. Share a single window, or on "
                    "Plasma 6.6 or later hide the reader from screen sharing in its window menu (Alt+F3).")));
    }
    // A page of its own in Settings, with GNOME's top bar after the looks where GNOME is the desktop.
    auto *appearancePage = new QWidget;
    auto *appearanceLayout = new QVBoxLayout(appearancePage);
    appearanceLayout->setContentsMargins(0, 0, 0, 0);
    appearanceLayout->setSpacing(0);
    appearanceLayout->addWidget(appearanceGroup);
    if (auto *gnome = makeGnomeExtensionSection())
        appearanceLayout->addWidget(gnome);
    appearanceSettings_ = appearancePage;
    return group;
}

// Kalipso in GNOME's top bar: the packages install the extension, but GNOME keeps it off until the
// person turns it on. Shown only on GNOME, and only where the extension is installed.
QWidget *MainWindow::makeGnomeExtensionSection() {
    gnomeExtension_ = new GnomeExtension(this);
    const auto installedOnDisk = [] {
        return !QStandardPaths::locate(
                    QStandardPaths::GenericDataLocation,
                    QStringLiteral("gnome-shell/extensions/%1/metadata.json").arg(QLatin1String(GnomeExtension::uuid)))
                    .isEmpty();
    };
    const auto first = gnomeExtension_->state();
    if (first == GnomeExtension::State::Unavailable ||
        (first == GnomeExtension::State::NotInstalled && !installedOnDisk()))
        return nullptr;
    auto *group = new SettingsSection(tr("GNOME top bar"));
    auto *status = note(QString());
    status->setObjectName("gnomeExtensionStatus");
    auto *turnOn = new AccessibleButton(tr("Turn on"));
    turnOn->setObjectName("gnomeExtensionTurnOn");
    group->form->addRow(status);
    // Compact and on the left, like Settings' other buttons (Test microphone).
    auto *turnOnRow = new QHBoxLayout;
    turnOnRow->setContentsMargins(0, 0, 0, 0);
    turnOnRow->addWidget(turnOn);
    turnOnRow->addStretch(1);
    group->form->addRow(turnOnRow);
    const auto refresh = [this, status, turnOn] {
        using State = GnomeExtension::State;
        const auto state = gnomeExtension_->state();
        switch (state) {
        case State::On:
            status->setText(tr("On: the play icon in the top bar shows Kalipso's reader over any app, with the "
                               "scripts you write here."));
            break;
        case State::Off:
            status->setText(tr("Off. Kalipso's reader can also live in the top bar, over any app, with the "
                               "scripts you write here."));
            break;
        case State::ExtensionsOff:
            status->setText(tr("GNOME's extensions are switched off. Turn them on in the Extensions app; Kalipso's "
                               "top bar reader then starts by itself."));
            break;
        case State::OutOfDate:
            status->setText(tr("Not on this version of GNOME: the top bar reader runs on GNOME Shell 48 and 51."));
            break;
        case State::Error:
            status->setText(tr("GNOME could not start the top bar reader. Log out and back in, then look here "
                               "again."));
            break;
        case State::NotInstalled:
        case State::Unavailable: status->setText(tr("Installed. Log out and back in, then turn it on here.")); break;
        }
        turnOn->setVisible(state == State::Off);
    };
    connect(turnOn, &QPushButton::clicked, this, [this] {
        if (!gnomeExtension_->turnOn())
            notify(tr("GNOME did not turn on the top bar reader."));
    });
    connect(gnomeExtension_, &GnomeExtension::changed, group, refresh);
    refresh();
    return group;
}

void MainWindow::refreshScreens() {
    const auto saved = QSettings().value("reading/screen").toString();
    const QSignalBlocker block(screen_);
    screen_->clear();
    for (auto *screen : QGuiApplication::screens()) {
        auto label = screen->name();
        if (!screen->model().isEmpty())
            label = screen->model() + " (" + screen->name() + ")";
        if (screen == QGuiApplication::primaryScreen())
            label = tr("%1 · primary").arg(label);
        screen_->addItem(label, screen->name());
    }
    // With one monitor there is nothing to choose.
    if (readerForm_)
        readerForm_->setRowVisible(screen_, screen_->count() > 1);
    const int index = screen_->findData(saved);
    screen_->setCurrentIndex(index >= 0 ? index
                                        : std::max(0, screen_->findData(QGuiApplication::primaryScreen()
                                                                            ? QGuiApplication::primaryScreen()->name()
                                                                            : QString())));
}

void MainWindow::applyInterfaceScale(double scale) {
    // Scales the controls of both views; the script's own size in the reader is set separately.
    auto widgets = findChildren<QWidget *>();
    widgets.removeIf(
        [this](QWidget *child) { return child == prompter_->text || prompter_->text->isAncestorOf(child); });
    // Every size at 100% first, before any changes: a control that inherits its parent's font would
    // otherwise take the parent's new size as its own and grow again, level after level. One made
    // since the last change inherits that size, so it is brought back to 100%.
    for (auto *widget : widgets)
        if (!widget->property("baseAccessibleFont").isValid()) {
            auto font = widget->font();
            if (!widget->testAttribute(Qt::WA_SetFont) && interfaceScale_ != 1.0 && font.pointSizeF() > 0)
                font.setPointSizeF(font.pointSizeF() / interfaceScale_);
            widget->setProperty("baseAccessibleFont", font);
        }
    for (auto *widget : widgets) {
        auto font = widget->property("baseAccessibleFont").value<QFont>();
        font.setPointSizeF(std::max(8.0, font.pointSizeF()) * scale);
        widget->setFont(font);
    }
    interfaceScale_ = scale;
    fitSettingsPages();
}

// The page list is as wide as its longest name at the interface's text size: names are never cut.
void MainWindow::fitSettingsPages() {
    if (!settingsNav_)
        return;
    int names = 0;
    for (int i = 0; i < settingsNav_->count(); ++i) {
        auto *item = settingsNav_->item(i);
        names = std::max(names, settingsNav_->fontMetrics().horizontalAdvance(item->text()));
        item->setSizeHint({0, settingsNav_->fontMetrics().height() + 2 * Design::Metric::space3});
    }
    settingsNav_->setFixedWidth(names + settingsNav_->iconSize().width() + 3 * Design::Metric::space4);
}

QWidget *MainWindow::makeSpeechSettings() {
    QSettings settings;
    auto *group = new SettingsSection(tr("Speech"));
    auto *form = group->form;
    const bool downloads = SpeechCatalog::downloadsSupported();
    backend_ = new SettingsComboBox;
    backend_->setObjectName("speechBackend");
    if (SystemSpeechClient::platformSupported())
        backend_->addItem(tr("System · on-device"), "system");
    backend_->addItem("Moonshine", "moonshine");
    backend_->addItem("Vosk", "vosk");
    voiceLanguage_ = new ConfirmedComboBox;
    voiceLanguage_->setObjectName("voiceLanguage");
    language_ = new SettingsComboBox;
    language_->setObjectName("speechLanguage");
    language_->setEditable(true);
    language_->addItem(tr("Choose…"), QString());
    const QList<QPair<QString, QString>> languages{
        {"English (US)", "en-US"}, {"Italiano", "it-IT"}, {"Deutsch", "de-DE"}, {"Español", "es-ES"},
        {"Français", "fr-FR"},     {"日本語", "ja-JP"},   {"中文", "zh-CN"}};
    for (const auto &[label, code] : languages)
        language_->addItem(label, code);
    speechLibrary_ = new QLineEdit;
    speechLibrary_->setObjectName("speechLibrary");
    modelPath_ = new QLineEdit;
    modelPath_->setObjectName("speechModel");
    architecture_ = new SettingsComboBox;
    architecture_->addItems({tr("Tiny"), tr("Base"), tr("Small"), tr("Medium")});
    auto *microphone = new MicrophonePicker;
    microphone->setObjectName("microphone");
    microphone->opening = [this] { refreshMicrophones(); };
    microphone_ = microphone;
    refreshMicrophones();
    connect(microphone_, &QComboBox::activated, this, [this] {
        saveMicrophone();
        if (microphoneTest_.active() || microphoneTestEnd_.isActive())
            startMicrophoneTest(); // Trying one microphone after another.
    });

    // The test: what Kalipso hears, on a meter, and in words.
    auto *microphoneBox = new QWidget;
    microphoneBox->setObjectName("microphoneTestBox");
    auto *microphoneLayout = new QVBoxLayout(microphoneBox);
    microphoneLayout->setContentsMargins(0, 0, 0, 0);
    microphoneLayout->setSpacing(Design::Metric::space2);
    testMicrophone_ = new AccessibleButton(tr("Test microphone"));
    testMicrophone_->setObjectName("testMicrophone");
    microphoneLevel_ = new QProgressBar;
    microphoneLevel_->setObjectName("microphoneLevel");
    microphoneLevel_->setRange(0, 1000);
    microphoneLevel_->setTextVisible(false);
    microphoneLevel_->setAccessibleName(tr("Microphone level"));
    microphoneLevel_->hide();
    microphoneStatus_ = new StatusLabel(QString());
    microphoneStatus_->setObjectName("microphoneStatus");
    auto *testRow = new QHBoxLayout;
    testRow->setContentsMargins(0, 0, 0, 0);
    testRow->addWidget(testMicrophone_);
    testRow->addStretch();
    microphoneLayout->addLayout(testRow);
    microphoneLayout->addWidget(microphoneLevel_);
    microphoneLayout->addWidget(microphoneStatus_);
    stopMicrophoneTest(false);
    microphoneTestEnd_.setSingleShot(true);
    microphoneTestEnd_.setInterval(30000);
    connect(&microphoneTestEnd_, &QTimer::timeout, this, [this] { stopMicrophoneTest(true); });
    connect(testMicrophone_, &QPushButton::clicked, this, [this] {
        if (microphoneTestEnd_.isActive())
            stopMicrophoneTest(true);
        else
            startMicrophoneTest();
    });
    connect(&microphoneTest_, &MicrophoneCapture::started, this, [this](const QString &name) {
        microphoneStatus_->setText(tr("Speak: the bar moves when Kalipso hears you through %1.").arg(name));
    });
    connect(&microphoneTest_, &MicrophoneCapture::audio, this, [this](const QByteArray &pcm) {
        // The bar jumps up with the voice and falls back gently.
        const double level = MicrophoneCapture::level(pcm);
        meter_ = std::max(level, meter_ * 0.85);
        microphoneLevel_->setValue(qRound(meter_ * 1000));
        if (level >= 0.45 && !heard_) { // Louder than -33 dB: a voice near the microphone, not the room.
            heard_ = true;
            microphoneStatus_->setText(tr("Kalipso hears you through %1.").arg(microphoneTest_.name()));
        }
    });
    connect(&microphoneTest_, &MicrophoneCapture::silent, this, [this](bool silent) {
        const auto name = microphoneTest_.name();
        if (silent) { // Nothing arrives to move the bar down: it says so at once.
            meter_ = 0;
            microphoneLevel_->setValue(0);
        }
        microphoneStatus_->setText(silent ? tr("No sound from %1. Check that it is not muted in the system sound "
                                               "settings, or choose another microphone.")
                                                .arg(name)
                                   : heard_ ? tr("Kalipso hears you through %1.").arg(name)
                                            : tr("Speak: the bar moves when Kalipso hears you through %1.").arg(name));
    });
    connect(&microphoneTest_, &MicrophoneCapture::failed, this, [this](const QString &message) {
        stopMicrophoneTest(false);
        microphoneStatus_->setText(message);
    });

    // The download of the chosen language: progress, what is happening, and Cancel or Try again.
    download_ = new SpeechDownload(this);
    auto *downloadBox = new QWidget;
    auto *downloadLayout = new QVBoxLayout(downloadBox);
    downloadLayout->setContentsMargins(0, 0, 0, 0);
    downloadLayout->setSpacing(Design::Metric::space2);
    downloadProgress_ = new QProgressBar;
    downloadProgress_->setObjectName("speechDownloadProgress");
    downloadProgress_->setRange(0, 1000);
    downloadProgress_->setTextVisible(false);
    downloadProgress_->setAccessibleName(tr("Download progress"));
    downloadStatus_ = new StatusLabel(QString());
    downloadStatus_->setObjectName("speechDownloadStatus");
    downloadAction_ = new AccessibleButton(QString());
    downloadAction_->setObjectName("speechDownloadAction");
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(downloadAction_);
    actionRow->addStretch();
    downloadLayout->addWidget(downloadProgress_);
    downloadLayout->addWidget(downloadStatus_);
    downloadLayout->addLayout(actionRow);
    // Before anything is downloaded: where it goes, and under which licence. The size is in the list.
    auto *downloadNote =
        note(downloads ? tr("Kalipso downloads a language once, into %1, and checks it against its pinned "
                            "fingerprint. The models are free software: Moonshine's under the MIT licence, Vosk's "
                            "under the Apache 2.0 licence. Then Kalipso listens only on this computer: nothing you "
                            "say is sent.")
                             .arg(QDir::toNativeSeparators(SpeechCatalog::writableRoot()))
                       : tr("Moonshine and Vosk need a runtime and a model installed by hand: choose Other model "
                            "installed by hand."));
    downloadNote->setObjectName("speechDownloadNote");
    // A downloaded language: what it takes and where, and the way to remove it or download it again.
    installedBox_ = new QWidget;
    installedBox_->setObjectName("speechInstalledBox");
    auto *installedLayout = new QVBoxLayout(installedBox_);
    installedLayout->setContentsMargins(0, 0, 0, 0);
    installedLayout->setSpacing(Design::Metric::space2);
    installedStatus_ = new StatusLabel(QString());
    installedStatus_->setObjectName("speechInstalledStatus");
    repairModel_ = new AccessibleButton(tr("Download again"));
    repairModel_->setObjectName("speechRepairModel");
    repairModel_->setToolTip(tr("Removes this language and downloads it again, checked against its fingerprint"));
    removeModel_ = new AccessibleButton(tr("Remove"));
    removeModel_->setObjectName("speechRemoveModel");
    auto *installedActions = new QHBoxLayout;
    installedActions->setContentsMargins(0, 0, 0, 0);
    installedActions->addWidget(repairModel_);
    installedActions->addWidget(removeModel_);
    installedActions->addStretch();
    installedLayout->addWidget(installedStatus_);
    installedLayout->addLayout(installedActions);
    connect(repairModel_, &QPushButton::clicked, this, [this] { removeLanguage(true); });
    connect(removeModel_, &QPushButton::clicked, this, [this] { removeLanguage(false); });

    form->addRow(tr("Speech engine"), backend_);
    form->addRow(tr("Language you speak"), voiceLanguage_);
    form->addRow(downloadBox);
    form->addRow(installedBox_);
    form->addRow(downloadNote);
    // Library and model paths are only needed for models installed by hand.
    customSpeech_ = new QWidget;
    auto *custom = new QFormLayout(customSpeech_);
    custom->setContentsMargins(0, 0, 0, 0);
    custom->setRowWrapPolicy(QFormLayout::WrapAllRows);
    custom->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    custom->addRow(tr("Native library"), speechLibrary_);
    custom->addRow(tr("Model folder"), modelPath_);
    auto *browse = new AccessibleButton(tr("Choose model folder…"));
    custom->addRow(browse);
    form->addRow(customSpeech_);
    form->addRow(tr("Language"), language_);
    form->addRow(tr("Moonshine model size"), architecture_);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Choose speech model"));
        if (!path.isEmpty())
            modelPath_->setText(path);
    });

    auto *systemActions = new QWidget;
    auto *systemLayout = new FlowLayout(systemActions);
    checkSystem_ = new AccessibleButton(tr("Check availability"));
    installSystem_ = new AccessibleButton(tr("Install system language"));
    installSystem_->setEnabled(false);
    systemLayout->addWidget(checkSystem_);
    systemLayout->addWidget(installSystem_);
#ifndef Q_OS_MACOS
    installSystem_->hide();
#endif
    systemStatus_ = note(tr("Choose a language and check its availability. No microphone is opened by this check."));
    systemStatus_->setObjectName("systemSpeechStatus");
    form->addRow(systemActions);
    form->addRow(systemStatus_);
    form->addRow(tr("Microphone"), microphone_);
    form->addRow(microphoneBox);
    // Voice commands were removed in 2.2: a script that contained their words could move the reader.
    QSettings().remove("speech/voiceCommands");
    auto *fillers = new WrappingCheckBox(tr("Count filler words (um, uh) in the session summary"));
    fillers->box->setObjectName("countFillers");
    fillers->box->setChecked(settings.value("speech/countFillers", true).toBool());
    session_.setCountFillers(fillers->box->isChecked());
    connect(fillers->box, &QCheckBox::toggled, this, [this](bool checked) {
        session_.setCountFillers(checked);
        QSettings().setValue("speech/countFillers", checked);
    });
    form->addRow(fillers);
    if (SystemSpeechClient::platformSupported())
        form->addRow(note(tr("Recognition stays on this computer, and Kalipso never switches to a cloud service. "
                             "System speech uses the default microphone.")));

    updateSpeechFields_ = [this, form, systemActions, downloadBox, downloadNote, microphoneBox] {
        const auto backend = backend_->currentData().toString();
        const bool system = backend == QLatin1String("system");
        const bool custom = voiceLanguage_->currentData().toMap().value("kind").toString() == QLatin1String("custom");
        form->setRowVisible(backend_, SystemSpeechClient::platformSupported() || custom);
        form->setRowVisible(voiceLanguage_, !system);
        downloadProgress_->setVisible(download_->active());
        downloadAction_->setVisible(!downloadAction_->text().isEmpty());
        form->setRowVisible(downloadBox, !system && (download_->active() || !downloadStatus_->text().isEmpty()));
        const auto installed = system || download_->active() ? InstalledSpeech() : chosenDownload();
        if (!installed.backend.isEmpty()) {
            const auto code = voiceLanguage_->currentData().toMap().value("code").toString();
            installedStatus_->setText(tr("%1 is installed: %2 MB in %3, %4 licence.")
                                          .arg(spokenName(code),
                                               megabytes(SpeechCatalog::installedBytes(installed.model)),
                                               QDir::toNativeSeparators(installed.model),
                                               SpeechCatalog::modelLicense(installed.backend, code)));
        }
        form->setRowVisible(installedBox_, !installed.backend.isEmpty());
        form->setRowVisible(downloadNote, !system);
        form->setRowVisible(customSpeech_, !system && custom);
        form->setRowVisible(language_, system || custom);
        form->setRowVisible(microphone_, !system);
        form->setRowVisible(microphoneBox, !system);
        form->setRowVisible(architecture_, backend == QLatin1String("moonshine") && custom);
        form->setRowVisible(systemActions, system);
        form->setRowVisible(systemStatus_, system);
    };
    const auto initial = settings
                             .value("speech/backend", settings.contains("speech/model")         ? "vosk"
                                                      : SystemSpeechClient::platformSupported() ? "system"
                                                                                                : "moonshine")
                             .toString();
    backend_->setCurrentIndex(std::max(0, backend_->findData(initial)));
    profile_ = backend_->currentData().toString();
    loadSpeechProfile();
    refreshVoiceLanguages();
    // A new profile listens with a model installed before (from a terminal, or by the Plasma widget),
    // in the interface language if there is one.
    const auto installed = installedSpeech();
    if (profile_ != QLatin1String("system") && modelPath_->text().isEmpty() && !installed.isEmpty()) {
        const auto preferred = SpeechCatalog::installedFor(QLocale::system().name().left(2));
        useInstalledSpeech(preferred.backend.isEmpty() ? installed.first() : preferred, false);
    }
    updateSpeechFields_();

    connect(backend_, &QComboBox::currentIndexChanged, this, [this] {
        systemCheck_.stop();
        installingSystem_ = false;
        saveSpeechProfile();
        profile_ = backend_->currentData().toString();
        loadSpeechProfile();
        refreshVoiceLanguages();
        updateSpeechFields_();
        systemStatus_->setText(tr("Choose a language and check its availability."));
        checkSystem_->setEnabled(true);
        installSystem_->setEnabled(false);
    });
    connect(voiceLanguage_, &QComboBox::activated, this, [this] {
        const auto choice = voiceLanguage_->currentData().toMap();
        const auto kind = choice.value("kind").toString();
        if (kind == QLatin1String("catalog")) {
            const auto code = choice.value("code").toString();
            const auto found = SpeechCatalog::installedFor(code);
            if (found.backend.isEmpty())
                downloadLanguage(code);
            else
                useInstalledSpeech(found);
        } else if (kind == QLatin1String("installed")) {
            for (const auto &item : installedSpeech())
                if (item.model == choice.value("model").toString())
                    useInstalledSpeech(item, false);
        } else if (kind == QLatin1String("custom")) {
            downloadStatus_->clear();
            downloadAction_->setText({});
        }
        updateSpeechFields_();
    });
    connect(download_, &SpeechDownload::progress, this, [this](qint64 received, qint64 total) {
        downloadProgress_->setValue(total > 0 ? int(received * 1000 / total) : 0);
        // The words keep up with the bar, at most once a second so a screen reader is not interrupted.
        if (downloadShown_.isValid() && downloadShown_.elapsed() < 1000 && received < total)
            return;
        downloadShown_.start();
        downloadStatus_->setText(
            tr("Downloading %1: %2 of %3 MB").arg(spokenName(downloadCode_), megabytes(received), megabytes(total)));
    });
    connect(download_, &SpeechDownload::unpackingStarted, this, [this] {
        downloadShown_.invalidate();
        downloadStatus_->setText(tr("Checking and unpacking %1…").arg(spokenName(downloadCode_)));
    });
    const auto downloadEnded = [this] {
        const bool focus = downloadAction_->hasFocus();
        voiceLanguage_->setEnabled(!speech_.active());
        backend_->setEnabled(!speech_.active());
        if (focus)
            voiceLanguage_->setFocus();
    };
    connect(download_, &SpeechDownload::finished, this, [this, downloadEnded](const InstalledSpeech &speech) {
        downloadAction_->setText({});
        downloadEnded();
        useInstalledSpeech(speech);
    });
    connect(download_, &SpeechDownload::failed, this, [this, downloadEnded](SpeechDownload::Error error) {
        const auto name = spokenName(downloadCode_);
        QString message;
        bool retry = true;
        switch (error) {
        case SpeechDownload::Error::Network:
            message = tr("Couldn't download %1. Check the internet connection and try again.").arg(name);
            break;
        case SpeechDownload::Error::Fingerprint:
            message = tr("The %1 download didn't match its fingerprint, so it was deleted. Try again later.").arg(name);
            break;
        case SpeechDownload::Error::Archive:
            message = tr("The %1 model couldn't be unpacked. Try again later.").arg(name);
            break;
        case SpeechDownload::Error::Disk:
            message =
                tr("Couldn't save %1: there isn't enough free space, or the data folder can't be written.").arg(name);
            break;
        case SpeechDownload::Error::Unsupported:
            message = tr("Kalipso downloads speech models on Linux for x86_64 computers. Choose Other model "
                         "installed by hand.");
            retry = false;
            break;
        case SpeechDownload::Error::Cancelled:
        case SpeechDownload::Error::None:
            message = tr("Download cancelled. Nothing was changed.");
            retry = false;
            break;
        }
        downloadStatus_->setText(message);
        downloadAction_->setText(retry ? tr("Try again") : QString());
        downloadEnded();
        refreshVoiceLanguages(); // Back to the language in use.
        updateSpeechFields_();
        if (retry && voiceLanguage_->hasFocus())
            downloadAction_->setFocus();
    });
    connect(downloadAction_, &QPushButton::clicked, this, [this] {
        if (download_->active())
            download_->cancel();
        else if (!downloadCode_.isEmpty())
            downloadLanguage(downloadCode_);
    });
    connect(&systemCheck_, &SystemSpeechClient::capabilities, this, [this](const QJsonObject &result) {
        systemStatus_->setText(result.value("message").toString());
        installSystem_->setEnabled(result.value("supported").toBool() && !result.value("installed").toBool());
        for (const auto &locale : result.value("locales").toArray())
            if (language_->findData(locale.toString()) < 0)
                language_->addItem(locale.toString(), locale.toString());
    });
    connect(language_, &QComboBox::currentTextChanged, this, [this] {
        systemCheck_.stop();
        installingSystem_ = false;
        installSystem_->setEnabled(false);
        checkSystem_->setEnabled(true);
        systemStatus_->setText(tr("Check availability for the selected language."));
    });
    connect(checkSystem_, &QPushButton::clicked, this, [this] {
        systemStatus_->setText(tr("Checking installed system speech…"));
        systemCheck_.check(speechLanguage());
    });
    connect(installSystem_, &QPushButton::clicked, this, [this] {
        installingSystem_ = true;
        installSystem_->setEnabled(false);
        checkSystem_->setEnabled(false);
        systemCheck_.install(speechLanguage());
    });
    connect(&systemCheck_, &SystemSpeechClient::message, systemStatus_, &QLabel::setText);
    connect(&systemCheck_, &SystemSpeechClient::failed, this, [this](const QString &message) {
        installingSystem_ = false;
        systemStatus_->setText(message);
        checkSystem_->setEnabled(true);
    });
    connect(&systemCheck_, &SystemSpeechClient::completed, this, [this] {
        checkSystem_->setEnabled(true);
        if (installingSystem_) {
            installingSystem_ = false;
            systemCheck_.check(speechLanguage());
        }
    });
    return group;
}

void MainWindow::refreshVoiceLanguages() {
    const QSignalBlocker block(voiceLanguage_);
    voiceLanguage_->clear();
    const bool downloads = SpeechCatalog::downloadsSupported();
    const auto installed = installedSpeech();
    const auto current = modelPath_->text();
    // Nothing chosen yet: "Choose…" where Kalipso downloads, the hand-installed fields elsewhere.
    int selected = current.isEmpty() && downloads ? 0 : -1;
    voiceLanguage_->addItem(tr("Choose…"), QVariantMap{{"kind", "none"}});
    const auto catalog = downloads ? SpeechCatalog::languages() : QStringList();
    for (const auto &code : catalog) {
        const auto name = listedName(code);
        const auto ready = SpeechCatalog::installedFor(code);
        const auto bytes =
            SpeechCatalog::plan(SpeechCatalog::downloadBackend(code), code, SpeechCatalog::writableRoot()).bytes();
        voiceLanguage_->addItem(ready.backend.isEmpty() ? tr("%1 · download %2 MB").arg(name, megabytes(bytes))
                                                        : tr("%1 · ready").arg(name),
                                QVariantMap{{"kind", "catalog"}, {"code", code}});
        for (const auto &item : installed)
            if (item.model == current && item.language == code)
                selected = voiceLanguage_->count() - 1;
    }
    // Models installed by hand, in other languages or where Kalipso does not download.
    for (const auto &item : installed) {
        if (catalog.contains(item.language))
            continue;
        voiceLanguage_->addItem(item.label(), QVariantMap{{"kind", "installed"}, {"model", item.model}});
        if (item.model == current)
            selected = voiceLanguage_->count() - 1;
    }
    voiceLanguage_->addItem(tr("Other model installed by hand…"), QVariantMap{{"kind", "custom"}});
    voiceLanguage_->setCurrentIndex(selected >= 0 ? selected : voiceLanguage_->count() - 1);
}

void MainWindow::useInstalledSpeech(const InstalledSpeech &speech, bool announce) {
    if (backend_->currentData().toString() != speech.backend) {
        saveSpeechProfile();
        const QSignalBlocker block(backend_);
        backend_->setCurrentIndex(backend_->findData(speech.backend));
        profile_ = speech.backend;
        loadSpeechProfile();
    }
    speechLibrary_->setText(speech.library);
    modelPath_->setText(speech.model);
    architecture_->setCurrentIndex(0); // The catalog installs Tiny streaming models.
    {
        const QSignalBlocker block(language_);
        language_->setEditText(speech.language);
    }
    saveSettings();
    refreshVoiceLanguages();
    downloadAction_->setText({});
    downloadStatus_->setText(
        announce
            ? tr("Ready to listen in %1. Choose Follow my voice and start the reader.").arg(spokenName(speech.language))
            : QString());
    if (updateSpeechFields_)
        updateSpeechFields_();
    refreshWelcome();
}

InstalledSpeech MainWindow::chosenDownload() const {
    const auto choice = voiceLanguage_ ? voiceLanguage_->currentData().toMap() : QVariantMap();
    if (choice.value("kind").toString() != QLatin1String("catalog"))
        return {};
    const auto found = SpeechCatalog::installedFor(choice.value("code").toString());
    return !found.backend.isEmpty() && SpeechCatalog::downloaded(found) ? found : InstalledSpeech();
}

// Removes the chosen language Kalipso downloaded, and downloads it again when repairing. Asks first:
// without the internet, a removed language cannot come back.
void MainWindow::removeLanguage(bool downloadAgain) {
    const auto installed = chosenDownload();
    if (installed.backend.isEmpty() || download_->active())
        return;
    const auto code = voiceLanguage_->currentData().toMap().value("code").toString();
    const auto name = spokenName(code);
    const auto size = megabytes(SpeechCatalog::installedBytes(installed.model));
    const auto question =
        downloadAgain ? tr("Kalipso removes %1 and downloads it again (%2 MB), checking it against its fingerprint. "
                           "Until it is done, it cannot follow your voice in this language.")
                            .arg(name, size)
                      : tr("Kalipso removes %1 and frees %2 MB. To use it again, choose it here: Kalipso downloads it "
                           "again, which needs the internet.")
                            .arg(name, size);
    if (QMessageBox::question(settings_,
                              downloadAgain ? tr("Download %1 again?").arg(name) : tr("Remove %1?").arg(name), question,
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const bool inUse =
        QFileInfo(modelPath_->text()).canonicalFilePath() == QFileInfo(installed.model).canonicalFilePath();
    if (!SpeechCatalog::removeDownloaded(installed)) {
        downloadStatus_->setText(tr("Couldn't remove %1: check that its folder can be written.").arg(name));
        updateSpeechFields_();
        return;
    }
    if (inUse) {
        modelPath_->clear();
        saveSettings();
    }
    refreshVoiceLanguages();
    if (downloadAgain) {
        // Back on the language, so the download that follows is the one shown.
        for (int i = 0; i < voiceLanguage_->count(); ++i)
            if (voiceLanguage_->itemData(i).toMap().value("code").toString() == code) {
                const QSignalBlocker block(voiceLanguage_);
                voiceLanguage_->setCurrentIndex(i);
            }
        downloadLanguage(code);
    } else {
        downloadAction_->setText({});
        downloadStatus_->setText(tr("Removed %1: %2 MB freed.").arg(name, size));
        updateSpeechFields_();
    }
    refreshReading();
    refreshWelcome();
}

void MainWindow::downloadLanguage(const QString &code) {
    downloadCode_ = code;
    downloadShown_.invalidate();
    const bool focus = voiceLanguage_->hasFocus() || downloadAction_->hasFocus();
    downloadProgress_->setValue(0);
    downloadStatus_->setText(tr("Downloading %1…").arg(spokenName(code)));
    downloadAction_->setText(tr("Cancel download"));
    voiceLanguage_->setEnabled(false);
    backend_->setEnabled(false);
    download_->start(SpeechCatalog::downloadBackend(code), code);
    updateSpeechFields_();
    if (focus)
        downloadAction_->setFocus(); // The list is disabled while downloading; Cancel takes its place.
}

QWidget *MainWindow::makeRemoteSettings() {
    // Pedals and clickers are on the Keys and pedals page; this page is the phone.
    auto *group = new SettingsSection(tr("Phone as a remote"));
    auto *form = group->form;
    auto *enableRow = new WrappingCheckBox(tr("Allow a phone on this network to control the reader"));
    auto *enable = enableRow->box;
    enable->setObjectName("remoteControl");
    auto *link = new QLabel;
    link->setObjectName("remoteLink");
    link->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    link->setWordWrap(true);
    auto *code = new QLabel;
    code->setObjectName("remoteCode");
    code->setAccessibleName(tr("QR code for the remote control link"));
    // The network the phone uses; containers, VMs and VPNs come last and are never the default.
    auto *network = new SettingsComboBox;
    network->setObjectName("remoteNetwork");
    for (const auto &entry : RemoteControl::networks())
        network->addItem(QStringLiteral("%1 · %2").arg(entry.address.toString(), entry.name), entry.address.toString());
    if (network->count() == 0)
        network->addItem(tr("No network found"), QString());
    network->setCurrentIndex(std::max(0, network->findData(QSettings().value("remote/address").toString())));
    form->addRow(enableRow);
    form->addRow(tr("Network"), network);
    form->addRow(link);
    form->addRow(code);
    form->addRow(note(tr("Anyone on this network who has the link can start, pause and step the reader; the script "
                         "text is never shared. A new link is created each time you turn this on, and it stops when "
                         "Kalipso closes.")));
    link->hide();
    code->hide();
    remote_ = new RemoteControl(this);
    connect(remote_, &RemoteControl::command, this, [this](const QString &name) {
        if (name == QLatin1String("toggle")) {
            // The microphone only ever runs with the reader in sight.
            if (running() || (isVisible() && !isMinimized()))
                toggleRunning();
        } else if (name == QLatin1String("next"))
            seekWord(tracker_.position() + 1);
        else if (name == QLatin1String("previous"))
            seekWord(tracker_.position() - 1);
        else if (name == QLatin1String("faster"))
            changeSpeed(10);
        else if (name == QLatin1String("slower"))
            changeSpeed(-10);
        else if (name == QLatin1String("reset"))
            seekWord(-1);
        else if (name == QLatin1String("section-next"))
            jumpSection(1);
        else if (name == QLatin1String("section-previous"))
            jumpSection(-1);
    });
    const auto restart = [this, network, enable] {
        if (enable->isChecked())
            emit enable->toggled(true);
        QSettings().setValue("remote/address", network->currentData().toString());
    };
    connect(network, &QComboBox::activated, this, restart);
    connect(enable, &QCheckBox::toggled, this, [this, link, code, enable, network](bool on) {
        const QHostAddress address(network->currentData().toString());
        if (on && !remote_->start(address.isNull() ? QHostAddress(QHostAddress::LocalHost) : address,
                                  quint16(QSettings().value("remote/port", 0).toUInt()))) {
            const QSignalBlocker block(enable);
            enable->setChecked(false);
            link->setText(tr("Remote control could not start on this network."));
            link->show();
            return;
        }
        if (!on)
            remote_->stop();
        link->setVisible(on);
        code->setVisible(on);
        if (on) {
            QSettings().setValue("remote/port", remote_->port()); // Reused next time.
            link->setText(tr("Open on your phone: %1").arg(remote_->url()));
            code->setPixmap(QPixmap::fromImage(RemoteControl::qrCode(remote_->url())));
            refreshReading();
        }
    });
    return group;
}

QString MainWindow::speechLanguage() const {
    if (language_->currentIndex() >= 0 && language_->currentText() == language_->itemText(language_->currentIndex()))
        return language_->currentData().toString();
    return language_->currentText().trimmed();
}

void MainWindow::saveSpeechProfile() {
    if (profile_.isEmpty())
        return;
    QSettings settings;
    const auto prefix = "profiles/" + profile_ + "/";
    settings.setValue(prefix + "library", speechLibrary_->text());
    settings.setValue(prefix + "model", modelPath_->text());
    settings.setValue(prefix + "language", speechLanguage());
    settings.setValue(prefix + "architecture", architecture_->currentIndex() + 2);
}

void MainWindow::loadSpeechProfile() {
    QSettings settings;
    const auto prefix = "profiles/" + profile_ + "/";
    QString defaultLibrary = profile_ == QLatin1String("moonshine") ? "libmoonshine" : "libvosk";
#ifdef Q_OS_WIN
    defaultLibrary = (profile_ == QLatin1String("moonshine") ? QString("moonshine") : QString("vosk")) + ".dll";
#elif defined(Q_OS_MACOS)
    defaultLibrary += ".dylib";
#else
    defaultLibrary += ".so";
#endif
    const QVariant legacyLibrary =
        profile_ == QLatin1String("vosk") ? settings.value("speech/library", defaultLibrary) : QVariant(defaultLibrary);
    speechLibrary_->setText(settings.value(prefix + "library", legacyLibrary).toString());
    modelPath_->setText(
        settings
            .value(prefix + "model", profile_ == QLatin1String("vosk") ? settings.value("speech/model") : QVariant())
            .toString());
    const auto language = settings.value(prefix + "language").toString();
    const QSignalBlocker block(language_);
    const int index = language_->findData(language);
    if (index >= 0)
        language_->setCurrentIndex(index);
    else
        language_->setEditText(language);
    architecture_->setCurrentIndex(std::clamp(settings.value(prefix + "architecture", 2).toInt() - 2, 0, 3));
}

void MainWindow::saveSettings() {
    QSettings settings;
    saveSpeechProfile();
    settings.setValue("speech/backend", backend_->currentData().toString());
    saveMicrophone();
}

void MainWindow::saveMicrophone() {
    QSettings settings;
    settings.setValue("speech/microphone", microphone_->currentData());
    settings.setValue("speech/microphoneName", microphone_->currentData(Qt::UserRole + 1));
}

void MainWindow::refreshMicrophones() {
    QSettings settings;
    const auto chosen = microphone_->count() ? microphone_->currentData().toString()
                                             : settings.value("speech/microphone").toString().trimmed();
    const auto devices = Microphones::list();
    struct Item {
        QString text, id, name;
        bool operator==(const Item &) const = default;
    };
    QList<Item> items;
    const auto system = devices.find(QString());
    items.append({!devices.known ? tr("System microphone")
                  : system       ? tr("System microphone · %1").arg(system->name)
                                 : tr("No microphone found"),
                  QString(), QString()});
    for (const auto &input : devices.inputs)
        items.append({input.name, input.id, input.name});
    // Also a name typed by hand in earlier versions.
    const auto found = chosen.isEmpty() ? std::nullopt : devices.find(chosen);
    QString current = found ? found->id : chosen;
    if (!chosen.isEmpty() && !found) {
        // Unplugged: kept, so that it is used again when it comes back.
        const auto name = settings.value("speech/microphoneName").toString();
        items.append(
            {devices.known ? tr("%1 · not connected").arg(name.isEmpty() ? chosen : name) : chosen, chosen, name});
    }
    QList<Item> shown;
    for (int i = 0; i < microphone_->count(); ++i)
        shown.append({microphone_->itemText(i), microphone_->itemData(i).toString(),
                      microphone_->itemData(i, Qt::UserRole + 1).toString()});
    const QSignalBlocker block(microphone_);
    if (shown != items) {
        microphone_->clear();
        for (const auto &item : items) {
            microphone_->addItem(item.text, item.id);
            microphone_->setItemData(microphone_->count() - 1, item.name, Qt::UserRole + 1);
        }
    }
    microphone_->setCurrentIndex(std::max(0, microphone_->findData(current)));
}

void MainWindow::startMicrophoneTest() {
    heard_ = false;
    meter_ = 0;
    microphoneLevel_->setValue(0);
    microphoneLevel_->show();
    testMicrophone_->setText(tr("Stop the test"));
    microphoneStatus_->setText(tr("Opening the microphone…"));
    microphoneTestEnd_.start();
    microphoneTest_.start(microphone_->currentData().toString());
}

void MainWindow::stopMicrophoneTest(bool finished) {
    const bool opened = microphoneTest_.active();
    const auto name = microphoneTest_.name();
    microphoneTest_.stop();
    microphoneTestEnd_.stop();
    microphoneLevel_->hide();
    testMicrophone_->setText(tr("Test microphone"));
    if (finished && opened)
        microphoneStatus_->setText(heard_ ? tr("Test over: Kalipso heard you through %1.").arg(name)
                                          : tr("Test over: Kalipso did not hear a voice through %1. Raise its "
                                               "volume in the system sound settings, or choose another microphone.")
                                                .arg(name));
    else if (!finished)
        microphoneStatus_->setText(tr("The test shows whether Kalipso hears you. Nothing is recorded or sent."));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!saveScript() &&
        QMessageBox::warning(this, tr("Close without saving?"),
                             tr("Your latest changes could not be saved. Close Kalipso anyway and lose them?"),
                             QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
        event->ignore();
        return;
    }
    pauseRunning();
    saveSettings();
    event->accept();
}
