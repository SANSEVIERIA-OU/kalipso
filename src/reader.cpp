// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "reader.h"
#include "icons.h"
#include "platform/capture_exclusion.h"
#include "script_highlighter.h"
#include <QAbstractButton>
#include <QAbstractTextDocumentLayout>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
bool horizontal(Mirror mirror) { return mirror == Mirror::Horizontal || mirror == Mirror::Both; }
bool vertical(Mirror mirror) { return mirror == Mirror::Vertical || mirror == Mirror::Both; }
bool wayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }

} // namespace

// Draws its text on one line, cut with "…" when it does not fit; the whole text is in the
// tooltip. A label that wrapped would make its bar taller and push the controls about.
template <class Label>
class OneLine : public Label {
public:
    explicit OneLine(const QString &text = {}) : Label(text) {
        Label::setWordWrap(false);
        Label::setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    void setLine(const QString &value) {
        Label::setText(value);
        Label::setToolTip(value);
        Label::update();
    }
    QSize sizeHint() const override {
        return {Label::fontMetrics().horizontalAdvance(Label::text()) + 4, Label::fontMetrics().height()};
    }
    QSize minimumSizeHint() const override { return {0, Label::fontMetrics().height()}; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const QRect area = Label::contentsRect();
        Label::style()->drawItemText(
            &painter, area, int(Label::alignment() | Qt::AlignVCenter), Label::palette(), Label::isEnabled(),
            Label::fontMetrics().elidedText(Label::text(), Qt::ElideRight, area.width()), Label::foregroundRole());
    }
};

// The reader has no title bar while reading: dragging the grip and title moves the window.
// Works on Wayland too, where applications cannot position their own windows.
class DragHandle : public QWidget {
public:
    DragHandle() : grip(new QLabel), title(new OneLine<QLabel>) {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(Design::Metric::space1);
        layout->addWidget(grip);
        layout->addWidget(title, 1);
        title->setProperty("role", "muted");
        setCursor(Qt::SizeAllCursor);
        setToolTip(QObject::tr("Drag to move the reader"));
    }
    QLabel *grip;
    OneLine<QLabel> *title;
    QSize sizeHint() const override {
        return {std::min(320, layout()->sizeHint().width()), layout()->sizeHint().height()};
    }
    QSize minimumSizeHint() const override { return {grip->sizeHint().width(), layout()->sizeHint().height()}; }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        QWidget::mousePressEvent(event);
    }
};

ReaderView::ReaderView(QWidget *parent)
    : QTextEdit(parent), highlighter_(new ScriptHighlighter(document())),
      scroll_(new QPropertyAnimation(verticalScrollBar(), "value", this)) {
    setObjectName("readingScript");
    setReadOnly(true);
    highlighter_->setHideMarks(true);
    setAcceptRichText(false);
    setAutoFormatting(QTextEdit::AutoNone);
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAccessibleName(tr("Kalipso script"));
    scroll_->setDuration(Design::Duration::glide / 2);
    scroll_->setEasingCurve(QEasingCurve::OutCubic);
    // While reading, the first line can come down to the reading line and the last one up to it.
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &ReaderView::extendScrollRange);
    // New paragraphs inherit the line height; a text that replaced everything gets it here.
    connect(document(), &QTextDocument::contentsChange, this, [this](int position, int, int added) {
        if (spacing_ || spacingQueued_)
            return;
        const auto end = document()->findBlock(position + added).next();
        for (auto block = document()->findBlock(position); block.isValid() && block != end; block = block.next())
            if (int(block.blockFormat().lineHeight()) != lineSpacing_) {
                spacingQueued_ = true; // The document cannot be changed while it reports a change.
                QTimer::singleShot(0, this, &ReaderView::spaceLines);
                return;
            }
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, &ReaderView::caretChanged);
    spaceLines();
}

void ReaderView::setMirror(Mirror mirror) {
    mirror_ = mirror;
    scrollToCharacter(anchor_, false);
    viewport()->update();
}

void ReaderView::setEditable(bool editable) {
    if (editable == !isReadOnly())
        return;
    // The text stays where it is on the screen: the viewport moves under the top bar while writing.
    const int value = verticalScrollBar()->value();
    setReadOnly(!editable);
    const auto caret = textCursor();
    highlighter_->setReveal(editable && !caret.hasSelection() ? caret.position() : -1);
    style()->unpolish(this); // Style sheets can tell writing from reading by [readOnly].
    style()->polish(this);
    relayout(true);
    verticalScrollBar()->setValue(value + (editable ? topInset_ : -topInset_));
    viewport()->update();
}

void ReaderView::setReadingLine(double fraction) {
    readingLine_ = std::clamp(fraction, 0.0, 0.9);
    extendScrollRange();
    scrollToCharacter(anchor_, false);
}

void ReaderView::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    relayout(false);
    if (isReadOnly())
        scrollToCharacter(anchor_, false);
}

void ReaderView::changeEvent(QEvent *event) {
    QTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        relayout(false); // The measure is in characters of the text's size.
}

// The column takes the width it has up to the measure, in the middle. While writing, the text stays
// between the bars, so the caret is never under them; while reading they float over it.
void ReaderView::relayout(bool force) {
    const int column =
        fontMetrics().averageCharWidth() * kMeasure + kMarkerSpace + int(2 * document()->documentMargin());
    const int side = std::max(0, (width() - column) / 2);
    const QMargins wanted = isReadOnly() ? QMargins(side, 0, side, 0) : QMargins(side, topInset_, side, bottomBar_);
    if (wanted != viewportMargins() || force) {
        setViewportMargins(wanted);
        // The text wraps at the viewport's width, and the scroll range follows the viewport: both
        // are worked out again by QTextEdit when it is resized.
        QResizeEvent again(size(), QSize(-1, -1));
        QTextEdit::resizeEvent(&again);
    }
    extendScrollRange();
}

void ReaderView::extendScrollRange() {
    if (extending_ || !isReadOnly())
        return; // Writing: the usual range.
    auto *bar = verticalScrollBar();
    const int view = viewport()->height();
    const int minimum = -topInset_;
    const int maximum =
        std::max(minimum, int(std::ceil(document()->size().height())) - view + int(view * (1.0 - readingLine_)));
    if (bar->minimum() == minimum && bar->maximum() == maximum)
        return;
    extending_ = true;
    bar->setRange(minimum, maximum);
    extending_ = false;
}

void ReaderView::setTopInset(int pixels) {
    if (pixels == topInset_)
        return;
    topInset_ = pixels;
    relayout(false);
    if (isReadOnly())
        scrollToCharacter(anchor_, false);
}

void ReaderView::setBottomBar(int pixels) {
    if (pixels != bottomBar_) {
        bottomBar_ = pixels;
        if (!isReadOnly())
            relayout(false);
    }
    viewport()->update();
}

void ReaderView::setLineSpacing(int percent) {
    lineSpacing_ = std::clamp(percent, 100, 200);
    spaceLines();
    if (isReadOnly())
        scrollToCharacter(anchor_, false); // Same reading line, new line positions.
}

// The line height and the marker's room are formats of the document: right after a new text they are
// not an undo step; later (a new spacing chosen in Settings) they join the last edit, so undo never
// stops on them.
void ReaderView::spaceLines() {
    spacingQueued_ = false;
    QTextBlockFormat format;
    format.setLineHeight(lineSpacing_, QTextBlockFormat::ProportionalHeight);
    const QSignalBlocker block(this); // Only the layout changes: no text change to report.
    spacing_ = true;
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    const bool fresh = document()->availableUndoSteps() == 0 && document()->availableRedoSteps() == 0;
    if (fresh)
        document()->setUndoRedoEnabled(false);
    else if (document()->availableUndoSteps() > 0)
        all.joinPreviousEditBlock();
    else
        all.beginEditBlock();
    all.mergeBlockFormat(format);
    auto frame = document()->rootFrame()->frameFormat();
    if (frame.leftMargin() != kMarkerSpace) {
        frame.setLeftMargin(kMarkerSpace); // Room for the current-line marker.
        document()->rootFrame()->setFrameFormat(frame);
    }
    if (fresh)
        document()->setUndoRedoEnabled(true);
    else
        all.endEditBlock();
    spacing_ = false;
}

void ReaderView::scrollToCharacter(int character, bool animate) {
    anchor_ = character;
    if (!isReadOnly())
        return; // While writing, the text stays where the person scrolled it.
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(character, 0, std::max(0, document()->characterCount() - 1)));
    const QRect line = cursorRect(cursor);
    const int center = line.center().y() + verticalScrollBar()->value();
    // Through the glass, a mirrored line appears where the unmirrored one would. In a low window the
    // reading line stays below the top bar, which covers the text until it fades.
    const int reading = std::max(int(viewport()->height() * readingLine_), topInset_ + line.height() / 2 + 2);
    const int target = std::clamp(center - reading, verticalScrollBar()->minimum(), verticalScrollBar()->maximum());
    scroll_->stop();
    if (!animate || Motion::reduced() || std::abs(target - verticalScrollBar()->value()) > viewport()->height()) {
        verticalScrollBar()->setValue(target);
        return;
    }
    scroll_->setStartValue(verticalScrollBar()->value());
    scroll_->setEndValue(target);
    scroll_->start();
}

// While writing: the marks around the caret show, and the caret is where reading starts, as the
// playhead follows the cursor in editors that cut video by its text.
void ReaderView::caretChanged() {
    if (isReadOnly())
        return;
    const auto caret = textCursor();
    highlighter_->setReveal(caret.hasSelection() ? -1 : caret.position());
    if (!caret.hasSelection())
        emit caretMoved(caret.position());
}

// Backspace at the start of a heading makes it a line of text again, as in word processors.
void ReaderView::keyPressEvent(QKeyEvent *event) {
    auto caret = textCursor();
    if (!isReadOnly() && event->key() == Qt::Key_Backspace && !caret.hasSelection() &&
        event->modifiers() == Qt::NoModifier) {
        const auto block = caret.block();
        const int marks = highlighter_->headingMarks(block);
        if (marks > 0 && caret.position() == block.position() + marks) {
            caret.setPosition(block.position());
            caret.setPosition(block.position() + marks, QTextCursor::KeepAnchor);
            caret.removeSelectedText();
            setTextCursor(caret);
            return;
        }
    }
    QTextEdit::keyPressEvent(event);
}

// Ctrl and the wheel change the text size in steps, like the buttons (not QTextEdit's own zoom,
// which Settings would not know).
void ReaderView::wheelEvent(QWheelEvent *event) {
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QTextEdit::wheelEvent(event);
        return;
    }
    wheel_ += event->angleDelta().y();
    while (std::abs(wheel_) >= 120) {
        emit textSizeStep(wheel_ > 0 ? 1 : -1);
        wheel_ -= wheel_ > 0 ? 120 : -120;
    }
    event->accept();
}

// Pasted and dropped text is plain and takes the paragraphs' spacing.
void ReaderView::insertFromMimeData(const QMimeData *source) {
    if (isReadOnly() || !source || !source->hasText())
        return;
    auto caret = textCursor();
    caret.insertText(source->text());
    setTextCursor(caret);
    ensureCursorVisible();
}

// Copied text is the script as written, Markdown marks included: not the hidden formats.
QMimeData *ReaderView::createMimeDataFromSelection() const {
    auto *data = new QMimeData;
    data->setText(textCursor().selection().toPlainText());
    return data;
}

QPoint ReaderView::unmirrored(QPoint point) const {
    const auto area = viewport()->rect();
    if (horizontal(mirror_))
        point.setX(area.width() - 1 - point.x());
    if (vertical(mirror_))
        point.setY(area.height() - 1 - point.y());
    return point;
}

// A small triangle in the left margin points at the current line.
void ReaderView::paintMarker(QPainter &painter, int lineCenter) const {
    if (!isReadOnly() || !markerColor_.isValid() || document()->isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(markerColor_);
    const int half = std::max(5, fontMetrics().height() / 6);
    painter.drawPolygon(
        QPolygon({QPoint(2, lineCenter - half), QPoint(2 + half + 2, lineCenter), QPoint(2, lineCenter + half)}));
    painter.restore();
}

// Lines cut by the top and bottom edges fade into the background instead of
// ending abruptly. The top fade stops above the current line, so it never dims it.
void ReaderView::paintEdges(QPainter &painter) const {
    if (!isReadOnly() || !fadeColor_.isValid())
        return;
    const auto area = viewport()->rect();
    const int line = fontMetrics().lineSpacing();
    QColor clear = fadeColor_;
    clear.setAlpha(0);
    // Over the bottom bar, when it shows, the lines it covers fade out above it instead of being cut.
    const int edge = area.bottom() - std::clamp(bottomBar_, 0, area.height() / 2);
    const int bottom = std::min(line, area.height() / 5);
    QLinearGradient down(0, edge - bottom, 0, edge);
    down.setColorAt(0, clear);
    down.setColorAt(1, fadeColor_);
    painter.fillRect(QRect(0, edge - bottom, area.width(), bottom + 1), down);
    if (edge < area.bottom())
        painter.fillRect(QRect(0, edge, area.width(), area.bottom() - edge + 1), fadeColor_);
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(anchor_, 0, std::max(0, document()->characterCount() - 1)));
    const int top = std::clamp(cursorRect(cursor).top() - 2, 0, line / 2);
    if (top <= 0)
        return;
    QLinearGradient up(0, 0, 0, top);
    up.setColorAt(0, fadeColor_);
    up.setColorAt(1, clear);
    painter.fillRect(QRect(0, 0, area.width(), top), up);
}

int ReaderView::currentLineCenter() const {
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(anchor_, 0, std::max(0, document()->characterCount() - 1)));
    return cursorRect(cursor).center().y();
}

void ReaderView::paintEvent(QPaintEvent *event) {
    // Editing shows the text unmirrored, so the caret and mouse work normally.
    if (mirror_ == Mirror::None || !isReadOnly()) {
        QTextEdit::paintEvent(event);
        QPainter painter(viewport());
        paintEdges(painter);
        paintMarker(painter, currentLineCenter());
        return;
    }
    // Draw the document through a flipped painter; the viewport background is
    // already filled with the reader palette.
    QPainter painter(viewport());
    const auto area = viewport()->rect();
    painter.translate(horizontal(mirror_) ? area.width() : 0, vertical(mirror_) ? area.height() : 0);
    painter.scale(horizontal(mirror_) ? -1 : 1, vertical(mirror_) ? -1 : 1);
    const int dx = horizontalScrollBar()->value(), dy = verticalScrollBar()->value();
    painter.translate(-dx, -dy);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    // Style sheets restyle the widget's own paint path only; use the reader colors.
    context.palette.setColor(QPalette::Text, textColor_);
    context.clip = QRectF(dx, dy, area.width(), area.height());
    for (const auto &selection : extraSelections())
        context.selections.append({selection.cursor, selection.format});
    document()->documentLayout()->draw(&painter, context);
    painter.translate(dx, dy); // Marker and edges in viewport coordinates, mirrored with the text.
    paintEdges(painter);
    paintMarker(painter, currentLineCenter());
}

void ReaderView::mouseReleaseEvent(QMouseEvent *event) {
    QTextEdit::mouseReleaseEvent(event);
    if (isReadOnly() && event->button() == Qt::LeftButton && !textCursor().hasSelection())
        emit characterClicked(cursorForPosition(unmirrored(event->position().toPoint())).position());
}

namespace {
// A quiet button whose label goes away when the bar is short of room; the icon and the
// accessible name stay.
AccessibleButton *quietButton(const QString &label, const QString &tip) {
    auto *button = new AccessibleButton(label);
    button->setAccessibleName(label);
    button->setToolTip(tip);
    button->setProperty("quiet", true);
    button->setProperty("label", label);
    return button;
}

// The formats' icons, by the name of the action that applies them.
QString formatIcon(const QString &action) {
    static const QHash<QString, QString> icons{
        {"formatBold", "bold"}, {"formatItalic", "italic"}, {"formatHeading", "heading"}, {"formatCue", "cue"}};
    return icons.value(action);
}
} // namespace

ReaderPage::ReaderPage(QWidget *parent) : QWidget(parent) {
    Accessibility::useInterfaceStyle();
    Accessibility::useInterfaceFont();
    setAccessibleName(tr("Kalipso reader"));
    setObjectName("readingPage");
    setProperty("ownLook", true); // Its own colours and style, whatever the window's.
    resize(680, 360);
    setMinimumSize(320, 180);

    using namespace Design::Metric;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top bar: the scripts and what you are reading on the left; on the right the formats (while
    // writing), the text size, sections, settings and where the window goes.
    auto *topBar = new QWidget;
    topBar->setObjectName("readerBar");
    bars_.append(topBar);
    auto *bar = new QHBoxLayout(topBar);
    bar->setContentsMargins(space2, space1, space2, space1);
    bar->setSpacing(space1);
    drawerButton_ = quietButton(QString(), tr("Your scripts (F9)"));
    drawerButton_->setObjectName("readerScripts");
    drawerButton_->setAccessibleName(tr("Scripts"));
    drawerButton_->setCheckable(true);
    handle_ = new DragHandle;
    handle_->title->setText(tr("Kalipso"));
    formats_ = new QWidget;
    formats_->setObjectName("readerFormats");
    auto *formatRow = new QHBoxLayout(formats_);
    formatRow->setContentsMargins(0, 0, space2, 0);
    formatRow->setSpacing(space1);
    smaller_ = quietButton(QString(), tr("Smaller text (Ctrl+−)"));
    smaller_->setObjectName("readerTextSmaller");
    smaller_->setAccessibleName(tr("Smaller text"));
    larger_ = quietButton(QString(), tr("Larger text (Ctrl++)"));
    larger_->setObjectName("readerTextLarger");
    larger_->setAccessibleName(tr("Larger text"));
    sections_ = new MenuButton;
    sections_->setText(tr("Sections"));
    sections_->setProperty("label", sections_->text());
    sections_->setObjectName("readerSections");
    sections_->setAccessibleName(tr("Jump to section"));
    sections_->setToolTip(tr("Jump to a section"));
    sections_->setProperty("quiet", true);
    sections_->setAutoRaise(true);
    sections_->setPopupMode(QToolButton::InstantPopup);
    sections_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sections_->setMenu(new QMenu);
    sections_->hide();
    actions_ = quietButton(QString(), tr("More"));
    actions_->setObjectName("readerActions");
    actions_->hide();
    settings_ = quietButton(QString(), tr("Settings"));
    settings_->setObjectName("readerSettings");
    settings_->setAccessibleName(tr("Settings"));
    place_ = quietButton(QString(), QString());
    place_->setObjectName("readerPlace");
    bar->addWidget(drawerButton_);
    bar->addWidget(handle_, 1);
    bar->addWidget(formats_);
    bar->addWidget(smaller_);
    bar->addWidget(larger_);
    bar->addWidget(sections_);
    bar->addWidget(actions_);
    bar->addWidget(settings_);
    bar->addWidget(place_);
    topBar->setParent(this); // Over the text, placed by placeBars().

    progress_ = new QProgressBar;
    progress_->setObjectName("readerProgress");
    progress_->setAccessibleName(tr("Reading progress"));
    progress_->setTextVisible(false);
    progress_->setRange(0, 1);
    if (Accessibility::native())
        progress_->setFixedHeight(3); // A line, as with the drawn style, not the desktop's thick bar.
    layout->addWidget(progress_);

    text = new ReaderView;
    text->setTabChangesFocus(true); // Tab reaches the controls; a script has no use for tab characters.
    text->setPlaceholderText(tr("Write or paste your script here."));
    text->installEventFilter(this);
    pages_ = new QStackedWidget;
    pages_->setObjectName("readerPages");
    pages_->addWidget(text);
    pageLayout_ = new QVBoxLayout;
    pageLayout_->setContentsMargins(space4, space2, space5, space2);
    pageLayout_->addWidget(pages_);
    layout->addLayout(pageLayout_, 1);
    countdown_ = new QLabel(text);
    countdown_->setObjectName("readerCountdown");
    countdown_->setAlignment(Qt::AlignCenter);
    countdown_->setAttribute(Qt::WA_TransparentForMouseEvents);
    countdown_->hide();

    // Bottom bar, always one row: step, start or pause, how the reader advances, and what is
    // happening. Changing the mode changes what is in the row, never its height.
    auto *controlBar = new QWidget;
    controlBar->setObjectName("readerControls");
    bars_.append(controlBar);
    auto *controls = new QHBoxLayout(controlBar);
    controls->setContentsMargins(space3, space2, space3, space2);
    controls->setSpacing(space2);
    auto *previous = quietButton(QString(), tr("Previous word (← or Page Up)"));
    previous->setAccessibleName(tr("Previous word"));
    auto *next = quietButton(QString(), tr("Next word (→ or Page Down)"));
    next->setAccessibleName(tr("Next word"));
    primary_ = new AccessibleButton(tr("Start"));
    primary_->setObjectName("readerListeningButton");
    primary_->setProperty("primary", true);
    primary_->setToolTip(tr("Start or pause (Space)"));
    // The reading mode: one compact drop-down that fits a narrow reader.
    modes_ = new MenuButton;
    modes_->setObjectName("readerMode");
    modes_->setPopupMode(QToolButton::InstantPopup);
    modes_->setProperty("quiet", true);
    modes_->setAutoRaise(true);
    modes_->setToolTip(tr("How the reader advances"));
    auto *modeMenu = new QMenu;
    auto *modeGroup = new QActionGroup(modes_);
    for (const auto &[label, mode] :
         {std::pair{tr("Manual"), ReadingMode::Manual}, std::pair{tr("Steady speed"), ReadingMode::Steady},
          std::pair{tr("Follow my voice"), ReadingMode::Voice}}) {
        auto *action = modeMenu->addAction(label);
        action->setCheckable(true);
        action->setData(int(mode));
        modeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { emit modeRequested(mode); });
    }
    modes_->setMenu(modeMenu);
    speedBox_ = new QWidget;
    speedBox_->setObjectName("readerSpeedBox");
    auto *speedLayout = new QHBoxLayout(speedBox_);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->setSpacing(0);
    auto *slower = quietButton(QStringLiteral("−"), tr("Slower (−)"));
    slower->setAccessibleName(tr("Slower"));
    auto *faster = quietButton(QStringLiteral("+"), tr("Faster (+)"));
    faster->setAccessibleName(tr("Faster"));
    speed_ = new QLabel;
    speed_->setObjectName("readerSpeed");
    speed_->setAlignment(Qt::AlignCenter);
    speedLayout->addWidget(slower);
    speedLayout->addWidget(speed_);
    speedLayout->addWidget(faster);
    status_ = new OneLine<StatusLabel>(tr("Microphone off"));
    status_->setObjectName("readerListeningStatus");
    status_->setProperty("role", "muted");
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    for (auto *widget : QList<QWidget *>{previous, primary_, next, modes_, speedBox_})
        controls->addWidget(widget);
    controls->addWidget(status_, 1);
    controls->addStretch(0); // Keeps the controls together on the left when the status is hidden.
    steps_ = {previous, next};
    controlBar->setParent(this);
    icons_ = {{previous, "previous"},     {next, "next"},           {sections_, "sections"},
              {settings_, "settings"},    {actions_, "more"},       {drawerButton_, "sidebar"},
              {smaller_, "text-smaller"}, {larger_, "text-larger"}, {place_, "camera-top"}};
    setUnderCamera(false);

    // The menus are the desktop's own, in the reader's colours: without a parent they keep the
    // desktop's look (rounded, shadowed) instead of the reader's style sheet.
    for (auto *menu : {sections_->menu(), modeMenu})
        connect(menu, &QMenu::aboutToShow, this, [this, menu] { styleMenu(menu); });

    // Clicking a control must not move keyboard focus off the script: pedals and
    // clickers send Space and arrows, which would otherwise activate or edit.
    for (auto *control : findChildren<QAbstractButton *>())
        control->setFocusPolicy(Qt::TabFocus);
    // While reading, the bars fade out (the text keeps its place) and come back with the mouse,
    // a key, or a pause.
    idle_.setSingleShot(true);
    idle_.setInterval(2500);
    connect(&idle_, &QTimer::timeout, this, [this] { setControlsVisible(false); });
    message_.setSingleShot(true);
    message_.setInterval(5000);
    connect(&message_, &QTimer::timeout, this, [this] { status_->setText(statusText_); });
    for (auto *widget : bars_) {
        auto *effect = new QGraphicsOpacityEffect(widget);
        effect->setOpacity(1.0);
        new QPropertyAnimation(effect, "opacity", effect);
        widget->setGraphicsEffect(effect);
        widget->setMouseTracking(true);
        widget->installEventFilter(this);
        widget->setAutoFillBackground(true);
    }
    text->viewport()->setMouseTracking(true);
    text->viewport()->installEventFilter(this);
    setMouseTracking(true); // The edges resize when the window has no frame: see edgesAt().

    connect(previous, &QPushButton::clicked, this, [this] { emit step(-1); });
    connect(next, &QPushButton::clicked, this, [this] { emit step(1); });
    connect(settings_, &QPushButton::clicked, this, &ReaderPage::settingsRequested);
    connect(place_, &QPushButton::clicked, this, &ReaderPage::placeRequested);
    connect(drawerButton_, &QPushButton::clicked, this, [this] { setDrawerOpen(!drawerOpen_); });
    connect(smaller_, &QPushButton::clicked, this, [this] { emit textSizeRequested(-1); });
    connect(larger_, &QPushButton::clicked, this, [this] { emit textSizeRequested(1); });
    connect(text, &ReaderView::textSizeStep, this, &ReaderPage::textSizeRequested);
    connect(text, &ReaderView::caretMoved, this, &ReaderPage::jumpRequested);
    connect(primary_, &QPushButton::clicked, this, [this] {
        if (!blocked_)
            emit toggleRequested();
        else
            emit blockedRequested();
    });
    connect(slower, &QPushButton::clicked, this, [this] { emit speedRequested(-10); });
    connect(faster, &QPushButton::clicked, this, [this] { emit speedRequested(10); });
    connect(text, &ReaderView::characterClicked, this, &ReaderPage::jumpRequested);

    backdrop_ = new Backdrop(this);
    connect(backdrop_, &Backdrop::unavailable, this, &ReaderPage::colorStatus);
    connect(backdrop_, &Backdrop::colorChanged, this, [this](const QColor &sample) {
        // Hysteresis: switch only on a clear change and at most every 2.5 s.
        const auto luminance = Accessibility::luminance(sample);
        const QString next = luminance > 0.35 ? "dark" : luminance < 0.15 ? "light" : resolvedTheme_;
        if (next == resolvedTheme_ || (paletteAge_.isValid() && paletteAge_.elapsed() < 2500))
            return;
        resolvedTheme_ = next;
        paletteAge_.restart();
        applyPalette(next == "light" ? Design::light : Design::dark);
    });
    setReaderTheme("dark");
    setTextSize(28);
    setMode(ReadingMode::Voice);
    sizeIcons();
}

ReaderPage::~ReaderPage() {
    delete sections_->menu();
    delete modes_->menu();
}

void ReaderPage::applyPalette(const Design::Palette &palette) {
    colors_ = palette;
    Accessibility::apply(this, palette);
    text->setPalette(Accessibility::palette(palette));
    text->setTextColor(QColor(palette.text));
    text->highlighter()->setColors(QColor(palette.muted), QColor(palette.text));
    text->setMarkerColor(QColor(palette.accent));
    text->setFadeColor(QColor(palette.background));
    countdown_->setStyleSheet(QString("color:%1; background:%2; border: 1px solid %3; border-radius:16px;")
                                  .arg(palette.text, palette.surface, palette.border));
    for (const auto &[button, name] : icons_) {
        if (!button)
            continue;
        button->setProperty("motion", Icons::motion(name));
        if (button->property("shortIcon").toBool() && !button->property("shortened").toBool())
            continue; // Its icon shows only in place of its label.
        button->setIcon(Icons::icon(name, QColor(palette.text), QColor(palette.muted)));
    }
    handle_->grip->setPixmap(Icons::icon("grip", QColor(palette.muted)).pixmap(18, 18));
    updatePrimary();
    highlight(position_, length_);
}

void ReaderPage::styleMenu(QMenu *menu) const {
    menu->setPalette(Accessibility::palette(colors_));
    menu->setStyleSheet(Accessibility::native() ? QString() : Accessibility::style(colors_));
}

void ReaderPage::addIconButton(QAbstractButton *button, const QString &icon) {
    icons_.removeIf([](const auto &entry) { return entry.first.isNull(); });
    icons_.append({button, icon});
    const int side = std::max(20, qRound(fontMetrics().height() * 1.15));
    button->setIconSize(QSize(side, side));
    button->setProperty("motion", Icons::motion(icon));
    button->setIcon(Icons::icon(icon, QColor(colors_.text), QColor(colors_.muted)));
}

void ReaderPage::addShortIconButton(QAbstractButton *button, const QString &icon) {
    addIconButton(button, icon);
    button->setProperty("shortIcon", true);
    button->setIcon(QIcon());
    sizeIcons();
}

void ReaderPage::setIconName(QAbstractButton *button, const QString &icon) {
    for (auto &[known, name] : icons_)
        if (known == button)
            name = icon;
    button->setProperty("motion", Icons::motion(icon));
    if (!button->property("shortIcon").toBool() || button->property("shortened").toBool())
        button->setIcon(Icons::icon(icon, QColor(colors_.text), QColor(colors_.muted)));
}

void ReaderPage::setShortened(QAbstractButton *button, bool shortened) {
    const QString text = shortened ? QString() : button->property("label").toString();
    if (button->property("shortened").toBool() == shortened && button->text() == text)
        return;
    button->setProperty("shortened", shortened);
    button->setText(text);
    if (auto *tool = qobject_cast<QToolButton *>(button))
        tool->setToolButtonStyle(shortened ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly);
    if (button->property("shortIcon").toBool()) {
        QIcon icon;
        if (shortened)
            for (const auto &[known, name] : icons_)
                if (known == button)
                    icon = Icons::icon(name, QColor(colors_.text), QColor(colors_.muted));
        button->setIcon(icon);
    }
    // The layout keeps each item's last size until its widget says it changed.
    for (QWidget *changed = button; changed && changed != drawer_; changed = changed->parentWidget())
        changed->updateGeometry();
}

void ReaderPage::setReaderActions(const QList<QAction *> &actions) {
    auto *menu = new QMenu(this);
    menu->addActions(actions);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] { styleMenu(menu); });
    connect(actions_, &QPushButton::clicked, menu,
            [this, menu] { menu->popup(actions_->mapToGlobal(QPoint(0, actions_->height()))); });
    actions_->setVisible(!actions.isEmpty());
    orderTabs();
}

void ReaderPage::setReaderTheme(const QString &theme) {
    resolvedTheme_ = theme == "light" ? "light" : "dark";
    paletteAge_.invalidate();
    applyPalette(resolvedTheme_ == "light" ? Design::light : Design::dark);
    backdrop_->setEnabled(theme == "auto");
}

void ReaderPage::setTextFont(const QString &family) {
    fontFamily_ = family;
    setTextSize(textPoints_);
}

void ReaderPage::setTextSize(int points) {
    textPoints_ = points;
    QFont font = fontFamily_.isEmpty() ? QApplication::font() : QFont(fontFamily_);
    font.setPointSize(points);
    text->setFont(font);
    QFont large = countdown_->font();
    large.setPointSize(points * 3);
    large.setBold(true);
    countdown_->setFont(large);
}

void ReaderPage::setMode(ReadingMode mode) {
    readingMode_ = mode;
    if (message_.isActive()) { // A new choice: what happened before is no longer news.
        message_.stop();
        status_->setText(statusText_);
    }
    for (auto *action : modes_->menu()->actions())
        if (action->data().toInt() == int(mode)) {
            action->setChecked(true);
            modes_->setProperty("label", action->text());
            modes_->setAccessibleName(tr("Reading mode: %1").arg(action->text()));
        }
    speedBox_->setVisible(mode == ReadingMode::Steady);
    updatePrimary();
    updateLabels();
}

void ReaderPage::setRunning(bool running, const QString &status) {
    const bool changed = running != running_;
    running_ = running;
    updatePrimary();
    setStatus(status);
    if (changed) { // Status updates while reading must not bring the controls back.
        updateEditing();
        revealControls();
    }
}

void ReaderPage::setBlocked(bool blocked) {
    if (blocked_ == blocked)
        return;
    blocked_ = blocked;
    updatePrimary();
    updateLabels();
}

void ReaderPage::setAutoHideControls(bool hide) {
    autoHide_ = hide;
    revealControls();
}

void ReaderPage::revealControls() {
    setControlsVisible(true);
    if (running_ && autoHide_)
        idle_.start();
    else
        idle_.stop();
}

void ReaderPage::setControlsVisible(bool visible) {
    controlsVisible_ = visible;
    // The lines under the bottom bar come back as it fades out.
    text->setBottomBar(visible ? overlap_ : 0);
    for (auto *widget : bars_) {
        auto *effect = static_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
        auto *fade = effect->findChild<QPropertyAnimation *>();
        fade->stop();
        if (Motion::reduced() || !isVisible()) {
            effect->setOpacity(visible ? 1.0 : 0.0);
            continue;
        }
        // Out slowly, so it does not pull the eye from the text; back quickly when wanted.
        fade->setDuration(visible ? Design::Duration::enter : Design::Duration::glide);
        fade->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InOutCubic);
        fade->setStartValue(effect->opacity());
        fade->setEndValue(visible ? 1.0 : 0.0);
        fade->start();
    }
}

void ReaderPage::setStatus(const QString &status) {
    statusText_ = status;
    if (!message_.isActive())
        status_->setText(status);
    status_->setToolTip(status_->text());
    status_->update();
}

void ReaderPage::showMessage(const QString &message) {
    status_->setText(message);
    status_->setToolTip(message);
    status_->update();
    message_.start();
}

void ReaderPage::placeBars() {
    if (bars_.size() < 2)
        return;
    auto *top = bars_.first(), *bottom = bars_.last();
    const int topHeight = top->sizeHint().height(), bottomHeight = bottom->sizeHint().height();
    top->setGeometry(0, progress_->height(), width(), topHeight);
    bottom->setGeometry(0, height() - bottomHeight, width(), bottomHeight);
    // The first line starts under the top bar; while reading, the bars fade and the text has the
    // whole window.
    text->setTopInset(topHeight);
    // The pages are laid out by the page's own layout, before this runs; the text fills them.
    overlap_ = std::max(0, pages_->geometry().bottom() + 1 - (height() - bottomHeight));
    text->setBottomBar(controlsVisible_ ? overlap_ : 0);
    placeDrawer();
    top->raise();
    bottom->raise();
    if (drawer_ && drawerOpen_ && !roomy_) {
        scrim_->raise();
        drawer_->raise();
    }
}

// Labels shorten before anything wraps or overlaps. Each bar decides for itself, and the bottom
// bar decides for its widest mode (steady, with the speed), so changing the mode never changes the
// other controls: the mode takes its short name, then the start button keeps its icon and the speed
// its number, then the word steps go (keys and pedals still step). The status has the room left;
// with too little it goes (assistive technology still hears it).
void ReaderPage::updateLabels() {
    const auto need = [this](QWidget *bar, const QList<QWidget *> &extra) {
        const auto margins = bar->layout()->contentsMargins();
        int width = margins.left() + margins.right(), visible = 0;
        for (int i = 0; i < bar->layout()->count(); ++i)
            if (auto *widget = bar->layout()->itemAt(i)->widget();
                widget && widget != status_ && (!widget->isHidden() || extra.contains(widget))) {
                width += widget == static_cast<QWidget *>(handle_) ? handle_->minimumSizeHint().width()
                                                                   : widget->sizeHint().width();
                ++visible;
            }
        return width + std::max(0, visible - 1) * bar->layout()->spacing();
    };
    const int words = fontMetrics().averageCharWidth() * 12;
    // Top bar: labels, or icons.
    for (const bool icons : {false, true}) {
        for (auto *button : {static_cast<QAbstractButton *>(place_), static_cast<QAbstractButton *>(sections_)})
            button->setText(icons ? QString() : button->property("label").toString());
        sections_->setToolButtonStyle(icons ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        place_->updateGeometry();
        sections_->updateGeometry();
        if (need(bars_.first(), {}) + words <= width())
            break;
    }
    // Bottom bar, measured with the speed shown and the longest mode name.
    QString longest;
    for (auto *action : modes_->menu()->actions())
        if (modes_->fontMetrics().horizontalAdvance(action->text()) > modes_->fontMetrics().horizontalAdvance(longest))
            longest = action->text();
    QString longestShort;
    for (const auto &label : {tr("Manual"), tr("Steady"), tr("Voice")})
        if (modes_->fontMetrics().horizontalAdvance(label) > modes_->fontMetrics().horizontalAdvance(longestShort))
            longestShort = label;
    QString widest;
    for (const auto &label : {tr("Start"), tr("Pause"), tr("Choose language")})
        if (fontMetrics().horizontalAdvance(label) > fontMetrics().horizontalAdvance(widest))
            widest = label;
    const auto apply = [this, &longest, &longestShort, &widest](int level, bool measuring) {
        compact_ = level;
        const auto mode = modes_->property("label").toString();
        modes_->setText(level == 0                            ? (measuring ? longest : mode)
                        : measuring                           ? longestShort
                        : readingMode_ == ReadingMode::Steady ? tr("Steady")
                        : readingMode_ == ReadingMode::Voice  ? tr("Voice")
                                                              : mode);
        primary_->setText(level >= 2 ? QString() : measuring ? widest : primary_->property("label").toString());
        const auto number = QLocale().toString(speed_->property("wpm").toInt());
        speed_->setText(level >= 2 ? number : tr("%1 words/min").arg(number));
        for (auto *step : steps_)
            step->setVisible(level < 3);
        speedBox_->setVisible(readingMode_ == ReadingMode::Steady && level < 4); // + and − keys still work.
        for (auto *widget : bars_.last()->findChildren<QWidget *>())
            widget->updateGeometry();
    };
    int level = 0;
    while (level < 4) {
        apply(level, true);
        if (need(bars_.last(), level < 4 ? QList<QWidget *>{speedBox_} : QList<QWidget *>{}) <= width())
            break;
        ++level;
    }
    apply(level, false);
    status_->setVisible(width() - need(bars_.last(), {}) - bars_.last()->layout()->spacing() >= words);
}

void ReaderPage::updatePrimary() {
    primary_->setVisible(readingMode_ != ReadingMode::Manual || blocked_);
    // Voice: the microphone and Start; reading: Pause. Blocked: what to do first.
    const QString icon = blocked_                             ? QStringLiteral("settings")
                         : running_                           ? QStringLiteral("pause")
                         : readingMode_ == ReadingMode::Voice ? QStringLiteral("microphone")
                                                              : QStringLiteral("play");
    const QString label = blocked_ ? tr("Choose language") : running_ ? tr("Pause") : tr("Start");
    primary_->setProperty("label", label);
    primary_->setAccessibleName(label);
    primary_->setToolTip(blocked_ ? tr("Choose the language you speak, in Settings") : tr("Start or pause (Space)"));
    primary_->setText(compact_ >= 2 ? QString() : label);
    primary_->setIcon(Icons::icon(icon, QColor(colors_.actionText)));
    primary_->setProperty("motion", Icons::motion(icon));
    primary_->updateGeometry();
}

void ReaderPage::setTitle(const QString &title) { handle_->title->setLine(title.isEmpty() ? tr("Kalipso") : title); }

void ReaderPage::setProgress(int read, int total) {
    progress_->setRange(0, std::max(1, total));
    progress_->setValue(std::clamp(read, 0, std::max(1, total)));
}

void ReaderPage::setSpeed(int wordsPerMinute) {
    speed_->setProperty("wpm", wordsPerMinute);
    speed_->setText(compact_ >= 2 ? QLocale().toString(wordsPerMinute)
                                  : tr("%1 words/min").arg(QLocale().toString(wordsPerMinute)));
    speed_->setAccessibleName(tr("%1 words/min").arg(QLocale().toString(wordsPerMinute)));
    speed_->setToolTip(speed_->accessibleName());
    updateLabels();
}

void ReaderPage::setSections(const QVector<Section> &sections) {
    auto *menu = sections_->menu();
    menu->clear();
    for (const auto &section : sections) {
        auto *action = menu->addAction(section.title);
        connect(action, &QAction::triggered, this, [this, start = section.start] { emit jumpRequested(start); });
    }
    sections_->setVisible(!sections.isEmpty());
    updateLabels();
}

void ReaderPage::setResizableEdges(bool resizable) {
    resizable_ = resizable;
    if (!resizable)
        unsetCursor();
}

// --- Writing ----------------------------------------------------------------

void ReaderPage::setFormatActions(const QList<QAction *> &actions) {
    for (auto *action : actions) {
        auto *button = quietButton(QString(), action->toolTip());
        button->setObjectName(action->objectName() + "Button");
        button->setAccessibleName(action->text());
        button->setCheckable(action->isCheckable());
        button->setFocusPolicy(Qt::TabFocus); // The text keeps the caret and the selection.
        connect(button, &QPushButton::clicked, action, [action] { action->trigger(); });
        const auto follow = [action, button] {
            const QSignalBlocker block(button);
            button->setChecked(action->isChecked());
        };
        connect(action, &QAction::changed, button, follow);
        follow();
        formats_->layout()->addWidget(button);
        addIconButton(button, formatIcon(action->objectName()));
    }
    updateLabels();
    orderTabs();
}

void ReaderPage::orderTabs() {
    placeDrawer();
    QList<QWidget *> chain;
    const auto takes = [](QWidget *widget) { return bool(widget->focusPolicy() & Qt::TabFocus); };
    const auto bar = [&](QWidget *row) {
        for (int i = 0; i < row->layout()->count(); ++i) {
            auto *widget = row->layout()->itemAt(i)->widget();
            if (!widget)
                continue;
            if (takes(widget))
                chain << widget;
            else // A group (the formats, the speed): its buttons in their order.
                for (auto *button : widget->findChildren<QAbstractButton *>(Qt::FindDirectChildrenOnly))
                    if (takes(button))
                        chain << button;
        }
    };
    bar(bars_.first());
    if (drawer_)
        for (auto *widget : drawer_->findChildren<QWidget *>())
            if (takes(widget) && !qobject_cast<QAbstractScrollArea *>(widget->parentWidget()))
                chain << widget;
    chain << text;
    bar(bars_.last());
    for (int i = 1; i < chain.size(); ++i)
        QWidget::setTabOrder(chain[i - 1], chain[i]);
}

bool ReaderPage::isRoomy(const QSize &window, Qt::WindowStates states) {
    if (states & Qt::WindowFullScreen)
        return false;
    return (states & Qt::WindowMaximized) || (window.width() > 960 && window.height() > 600);
}

// Writing when the window is large and the reader is not reading: the text takes the caret, the
// formats come into the top bar and the scripts go beside the text.
void ReaderPage::updateEditing() {
    const auto *top = window();
    const bool roomy = isRoomy(top->size(), top->windowState());
    const bool roomyChanged = roomy != roomy_ || compact_ < 0;
    roomy_ = roomy;
    const bool editing = roomy && !running_;
    formats_->setVisible(roomy);
    formats_->setEnabled(editing && !welcomeShown());
    if (roomyChanged && drawer_ && !welcomeShown())
        setDrawerOpen(drawerWanted_[roomy]);
    if (editing != editing_) {
        editing_ = editing;
        text->setEditable(editing);
        if (editing)
            placeCaret(position_); // Where reading stopped, to fix a word there or start again from it.
        highlight(position_, length_);
        emit editingChanged(editing);
    }
    if (roomyChanged) {
        updateLabels();
        placeBars();
        // The panel that had the focus may have gone: the keys go to the text again.
        if (auto *focus = QApplication::focusWidget(); !focus || (isAncestorOf(focus) && focus != text)) {
            if (drawerOpen_ && !roomy_)
                drawer_->findChild<QWidget *>("scriptList")->setFocus();
            else
                text->setFocus();
        }
    }
}

void ReaderPage::sizeIcons() {
    const int side = std::max(20, qRound(fontMetrics().height() * 1.15));
    // A button that shows its icon only in place of its label has an icon no taller than its text, so it
    // is as tall with the one as with the other.
    const int short_ = std::max(18, fontMetrics().height());
    for (const auto &[button, name] : icons_)
        if (button) {
            const int own = button->property("shortIcon").toBool() ? short_ : side;
            button->setIconSize(QSize(own, own));
        }
    primary_->setIconSize(QSize(side, side));
}

// The text behind the scripts dims, so what is on top is clear; touching it closes them.
class Scrim : public QWidget {
public:
    explicit Scrim(QWidget *parent) : QWidget(parent) {}
    QColor color;
    std::function<void()> pressed;

protected:
    void paintEvent(QPaintEvent *) override { QPainter(this).fillRect(rect(), color); }
    void mousePressEvent(QMouseEvent *event) override {
        event->accept();
        if (pressed)
            pressed();
    }
};

void ReaderPage::setDrawer(QWidget *drawer) {
    auto *scrim = new Scrim(this);
    scrim->setObjectName("readerScrim");
    scrim->pressed = [this] { setDrawerOpen(false); };
    scrim->hide();
    scrim_ = scrim;
    drawer_ = drawer;
    drawer_->setParent(this);
    drawer_->hide();
    drawer_->installEventFilter(this);
    qApp->installEventFilter(this);
    drawerSlide_ = new QPropertyAnimation(drawer_, "pos", this);
    connect(drawerSlide_, &QPropertyAnimation::valueChanged, this, [this] {
        // The scrim follows the panel: fully there when it is all in.
        auto *scrim = static_cast<Scrim *>(scrim_);
        const double shown = isRightToLeft() ? double(width() - drawer_->x()) / std::max(1, drawer_->width())
                                             : 1.0 + double(drawer_->x()) / std::max(1, drawer_->width());
        QColor color(colors_.background);
        color.setAlphaF(float(0.6 * std::clamp(shown, 0.0, 1.0)));
        scrim->color = color;
        scrim->update();
    });
    connect(drawerSlide_, &QPropertyAnimation::finished, this, [this] {
        if (!drawerOpen_) {
            drawer_->hide();
            scrim_->hide();
            placeDrawer(); // The text takes back the panel's room.
            emit drawerShown(false);
        }
        placeDrawer(); // Resizing during the slide may have moved the RTL leading edge.
    });
    auto *toggle = new QAction(this);
    toggle->setShortcut(Qt::Key_F9); // Like the panels of the desktop's own apps.
    toggle->setShortcutContext(Qt::WindowShortcut);
    connect(toggle, &QAction::triggered, this, [this] { setDrawerOpen(!drawerOpen_); });
    addAction(toggle);
    updateEditing();
    orderTabs();
}

// About 32 characters: room for a title and its length. Beside the text it takes at most two fifths
// of the window; over it, all but a strip of the text, which closes it when touched.
int ReaderPage::drawerWidth() const {
    const int wanted =
        fontMetrics().averageCharWidth() * 32 + std::max(44, fontMetrics().height() * 2 + Design::Metric::space2);
    const int limit = roomy_ ? width() * 2 / 5 : width() - Design::Metric::space5;
    return std::max(std::min(200, width()), std::min(wanted, limit));
}

// The panel comes in from the leading edge and goes back the same way. In a large window it
// sits beside the text, which makes room for it; in a small one it lies over the text until a script
// is chosen, the text is touched or Esc is pressed.
void ReaderPage::setDrawerOpen(bool open) {
    if (!drawer_)
        return;
    drawerWanted_[roomy_] = open;
    drawerButton_->setChecked(open);
    if (open == drawerOpen_ && (open == drawer_->isVisible()))
        return;
    drawerOpen_ = open;
    // Keyboard focus never stays in a panel that goes away: it goes back to the text.
    if (!open && drawer_->isAncestorOf(QApplication::focusWidget()))
        text->setFocus();
    placeDrawer();
    const int width = drawerWidth();
    const QPoint shown(isRightToLeft() ? this->width() - width : 0, drawer_->y());
    const QPoint hidden(isRightToLeft() ? this->width() : -width, drawer_->y());
    drawerSlide_->stop();
    auto *scrim = static_cast<Scrim *>(scrim_);
    if (open) {
        if (!drawer_->isVisible())
            drawer_->move(hidden);
        scrim->setVisible(!roomy_);
        scrim->raise();
        drawer_->show();
        drawer_->raise();
        placeDrawer();
        if (roomy_)
            for (auto *bar : bars_)
                bar->raise();
        emit drawerShown(true);
        if (!roomy_)
            if (auto *list = drawer_->findChild<QWidget *>("scriptList"))
                list->setFocus(Qt::ShortcutFocusReason);
    }
    if (Motion::reduced() || !isVisible()) {
        drawer_->move(open ? shown : hidden);
        QColor color(colors_.background);
        color.setAlphaF(open ? 0.6f : 0.0f);
        scrim->color = color;
        scrim->update();
        if (!open) {
            drawer_->hide();
            scrim->hide();
            placeDrawer();
            emit drawerShown(false);
        }
        return;
    }
    drawerSlide_->setDuration(open ? Design::Duration::enter : Design::Duration::exit);
    drawerSlide_->setEasingCurve(open ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    drawerSlide_->setStartValue(drawer_->pos());
    drawerSlide_->setEndValue(open ? shown : hidden);
    drawerSlide_->start();
}

void ReaderPage::placeDrawer() {
    using namespace Design::Metric;
    const bool docked = drawer_ && roomy_ && drawerOpen_;
    const int width = drawerWidth();
    const auto margins = pageLayout_->contentsMargins();
    const int left = (isRightToLeft() ? space5 : space4) + (docked && !isRightToLeft() ? width : 0);
    const int right = (isRightToLeft() ? space4 : space5) + (docked && isRightToLeft() ? width : 0);
    if (margins.left() != left || margins.right() != right)
        pageLayout_->setContentsMargins(left, margins.top(), right, margins.bottom());
    if (!drawer_ || bars_.size() < 2)
        return;
    // An overlay uses the full height, including over the reading bars. At 200% text a
    // 320×240 window still has room for library controls and complete, scrollable rows.
    const int top = roomy_ ? progress_->height() + bars_.first()->sizeHint().height() : 0;
    const int bottom = roomy_ ? height() - bars_.last()->sizeHint().height() : height();
    // The cross is where an overlay is closed from; beside the text there is nothing to close. Selecting
    // does not change that: the cross is Close in both modes.
    if (auto *close = drawer_->findChild<QWidget *>("closeScriptDrawer"))
        close->setVisible(!roomy_);
    const bool sliding = drawerSlide_->state() == QAbstractAnimation::Running;
    // Closed, the panel takes no room at all: assistive technology does not report its rows as on
    // the screen (Qt decides that from their rectangles, not from the panel being hidden).
    const bool closed = !drawer_->isVisible() && !drawerOpen_;
    drawer_->setGeometry(sliding || !drawer_->isVisible() ? drawer_->x()
                         : isRightToLeft()                ? this->width() - width
                                                          : 0,
                         top, closed ? 0 : width, std::max(0, bottom - top));
    scrim_->setGeometry(0, top, this->width(), std::max(0, bottom - top));
    if (roomy_ && scrim_->isVisible())
        scrim_->hide(); // Beside the text, nothing is behind the scripts.
    // Its header keeps one row: labels go before anything is cut. The title says "Scripts" or "Selected: 3": it
    // is one line that gives way to the buttons (shortened with "…" if it must), and the room kept for it is
    // that of the longest word of either text ("reserve"), so that what New does and how tall the header is
    // do not change when selection starts.
    if (auto *header = drawer_->findChild<QWidget *>("scriptDrawerHeader"); header && header->layout()) {
        const auto buttons = header->findChildren<QAbstractButton *>();
        auto *title = header->findChild<QLabel *>("libraryTitle");
        int titleWord = 0;
        if (title)
            for (const auto &text : title->property("reserve").toStringList())
                for (const auto &word : text.split(u' ', Qt::SkipEmptyParts))
                    titleWord = std::max(titleWord, title->fontMetrics().horizontalAdvance(word));
        const auto need = [header, title, titleWord] {
            header->layout()->activate();
            const auto margins = header->layout()->contentsMargins();
            int needed = margins.left() + margins.right(), shown = 0;
            for (int i = 0; i < header->layout()->count(); ++i)
                if (auto *widget = header->layout()->itemAt(i)->widget(); widget && !widget->isHidden()) {
                    needed += widget == title && titleWord > 0 ? titleWord : widget->minimumSizeHint().width();
                    ++shown;
                }
            return needed + std::max(0, shown - 1) * header->layout()->spacing();
        };
        for (const bool icons : {false, true}) {
            for (auto *button : buttons)
                if (button->property("label").isValid())
                    button->setText(icons ? QString() : button->property("label").toString());
            if (need() + fontMetrics().averageCharWidth() * 6 <= width)
                break;
        }
        header->setMinimumHeight(
            std::max(header->layout()->minimumSize().height(), header->layout()->heightForWidth(header->width())));
    }
    // The tools row is one row in both modes, and what is shortened does not depend on the mode shown:
    // Import and the toggle ("Select", "Done") are there while browsing, the master check box and Actions
    // while selecting, and the toggle in both, so it is shortened for both or for neither. Each button has a
    // step (property "shortenOrder"), taken in this order and only while a mode's row still does not fit:
    // 1 the master check box's label, 2 Actions' label (its icon and chevron stay), 3 Import's label (its
    // icon stays), 4 the toggle's label (its icon stays): the toggle's word is the last to go, and once it
    // has gone nothing gets a word back. Accessible names and tooltips keep the words.
    if (auto *row = drawer_->findChild<QWidget *>("scriptLibraryTools"); row && row->layout() && row->isVisible()) {
        auto *layout = row->layout();
        QList<QAbstractButton *> steps[5];
        QString only[5]; // The mode whose row the step is for: "browsing", "selecting", or both.
        for (auto *button : row->findChildren<QAbstractButton *>())
            if (const int step = button->property("shortenOrder").toInt(); step >= 1 && step <= 4) {
                steps[step] << button;
                QWidget *item = button;
                while (item->parentWidget() && item->parentWidget() != row)
                    item = item->parentWidget();
                only[step] = item->property("only").toString();
            }
        const auto margins = drawer_->layout()->contentsMargins();
        const int room = width - margins.left() - margins.right();
        // The width a mode's row needs now, as the layout adds it: its controls and a gap between neighbours
        // (a stretch between two does not take their gap away).
        const auto needed = [&](const QString &mode) {
            const auto edges = layout->contentsMargins();
            int total = edges.left() + edges.right();
            bool neighbour = false;
            for (int i = 0; i < layout->count(); ++i) {
                auto *item = layout->itemAt(i)->widget();
                if (!item)
                    continue; // A stretch.
                if (const auto own = item->property("only").toString(); !own.isEmpty() && own != mode)
                    continue;
                item->ensurePolished();
                total += std::max(item->sizeHint().width(), item->minimumWidth()) + (neighbour ? layout->spacing() : 0);
                neighbour = true;
            }
            return total;
        };
        const auto set = [&](int step, bool shortened) {
            for (auto *button : steps[step])
                setShortened(button, shortened);
        };
        // Steps 1 to 3 for each mode, with the toggle's word still there; if a row still does not fit with
        // all of them taken, the toggle goes too and the others stay as they are: all shortened.
        set(4, false);
        bool fitsAll = true;
        for (const QString &mode : {QStringLiteral("selecting"), QStringLiteral("browsing")}) {
            for (int step = 1; step <= 3; ++step)
                if (only[step] == mode)
                    set(step, false);
            for (int step = 1; step <= 3 && needed(mode) > room; ++step)
                if (only[step] == mode)
                    set(step, true);
            fitsAll = fitsAll && needed(mode) <= room;
        }
        if (!fitsAll) {
            for (int step = 1; step <= 3; ++step)
                set(step, true);
            set(4, true);
        }
        layout->invalidate();
        layout->activate();
    }
}

void ReaderPage::placeCaret(int character) {
    if (!editing_)
        return;
    QTextCursor caret(text->document());
    caret.setPosition(std::clamp(character, 0, std::max(0, text->document()->characterCount() - 1)));
    text->setTextCursor(caret);
    text->ensureCursorVisible();
}

void ReaderPage::setWelcome(QWidget *welcome) {
    welcome_ = welcome;
    pages_->addWidget(welcome);
}

void ReaderPage::showWelcome(bool shown) {
    if (!welcome_ || shown == welcomeShown())
        return;
    pages_->setCurrentWidget(shown ? welcome_ : static_cast<QWidget *>(text));
    formats_->setEnabled(editing_ && !shown);
    // The keys start on the page that is shown: the welcome's steps, or the script.
    if (shown)
        welcome_->setFocus(Qt::OtherFocusReason);
    else if (!text->hasFocus() && isAncestorOf(QApplication::focusWidget()))
        text->setFocus(Qt::OtherFocusReason);
    // The welcome has the whole window: there is nothing to list yet. The scripts come with the first.
    if (drawer_) {
        const bool wanted = drawerWanted_[roomy_];
        setDrawerOpen(!shown && wanted);
        drawerWanted_[roomy_] = wanted;
    }
}

bool ReaderPage::welcomeShown() const { return welcome_ && pages_->currentWidget() == welcome_; }

void ReaderPage::setUnderCamera(bool underCamera) {
    underCamera_ = underCamera;
    // Under the camera it offers room to write; in a large window, the place under the camera.
    const auto label = underCamera ? tr("Expand") : tr("Under the camera");
    place_->setProperty("label", label);
    place_->setAccessibleName(label);
    place_->setToolTip(underCamera ? tr("Larger, to write and manage your scripts")
                                   : tr("Small, at the top of the screen, under the camera"));
    if (!place_->text().isEmpty())
        place_->setText(label);
    const QString icon = underCamera ? QStringLiteral("expand") : QStringLiteral("camera-top");
    for (auto &[button, name] : icons_)
        if (button == place_)
            name = icon;
    place_->setProperty("motion", Icons::motion(icon));
    place_->setIcon(Icons::icon(icon, QColor(colors_.text), QColor(colors_.muted)));
    updateLabels();
}

// ---------------------------------------------------------------------------

void ReaderPage::showCountdown(int seconds) {
    countdown_->setVisible(seconds > 0);
    if (seconds <= 0)
        return;
    countdown_->setText(QString::number(seconds));
    countdown_->adjustSize();
    const int side = std::max(countdown_->width(), countdown_->height()) + 32;
    countdown_->setGeometry(
        QRect(QPoint(0, 0), QSize(side, side)).translated(text->rect().center() - QPoint(side, side) / 2));
    countdown_->raise();
}

void ReaderPage::highlight(int position, int length) {
    position_ = position;
    length_ = length;
    // While writing, the caret is where reading starts: no marks over the text being written.
    if (editing_) {
        text->setExtraSelections({});
        text->scrollToCharacter(position, false);
        return;
    }
    const int limit = std::max(0, text->document()->characterCount() - 1);
    QTextEdit::ExtraSelection read;
    read.cursor = QTextCursor(text->document());
    read.cursor.setPosition(std::clamp(position, 0, limit), QTextCursor::KeepAnchor);
    read.format.setForeground(QColor(colors_.muted));
    QTextEdit::ExtraSelection current;
    current.cursor = QTextCursor(text->document());
    current.cursor.setPosition(std::clamp(position, 0, limit));
    current.cursor.setPosition(std::clamp(position + length, 0, limit), QTextCursor::KeepAnchor);
    // The current word, as on the website: text on a tint of the "you are here" blue, underlined in it.
    current.format.setBackground(QColor(colors_.tint));
    current.format.setForeground(QColor(colors_.text));
    current.format.setFontUnderline(true);
    current.format.setUnderlineColor(QColor(colors_.accent));
    text->setExtraSelections({read, current});
    text->scrollToCharacter(position, isVisible());
}

void ReaderPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (bars_.size() < 2 || !status_)
        return;
    updateLabels();
    placeBars();
    updateEditing();
    if (countdown_->isVisible())
        showCountdown(countdown_->text().toInt());
}

// The page around the text is the reader's own background, light or dark: the window's style sheet
// (the app's colours) must not show through at the sides of the text.
void ReaderPage::paintEvent(QPaintEvent *) { QPainter(this).fillRect(rect(), QColor(colors_.background)); }

void ReaderPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateEditing();
    // The keys start on the page that is shown, not on the first button of the top bar.
    if (welcomeShown())
        welcome_->setFocus(Qt::OtherFocusReason);
    else if (!isAncestorOf(QApplication::focusWidget()) || bars_.first()->isAncestorOf(QApplication::focusWidget()))
        text->setFocus(Qt::OtherFocusReason);
}

void ReaderPage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    // The style can change while the page is still being built (the first page sets the app's style).
    if (bars_.size() < 2 || !status_)
        return;
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        sizeIcons();
        updateLabels();
        placeBars();
    }
}

void ReaderPage::trigger(ReaderAction action) {
    switch (action) {
    case ReaderAction::Toggle: emit toggleRequested(); break;
    case ReaderAction::Next: emit step(1); break;
    case ReaderAction::Previous: emit step(-1); break;
    case ReaderAction::Faster: emit speedRequested(10); break;
    case ReaderAction::Slower: emit speedRequested(-10); break;
    case ReaderAction::Reset: emit resetRequested(); break;
    case ReaderAction::PreviousSection: emit sectionRequested(-1); break;
    case ReaderAction::NextSection: emit sectionRequested(1); break;
    }
}

void ReaderPage::keyPressEvent(QKeyEvent *event) {
    revealControls();
    // Esc closes the scripts over the text, or pauses.
    if (event->key() == Qt::Key_Escape) {
        if (drawerOpen_ && !roomy_)
            setDrawerOpen(false);
        else if (running_)
            emit toggleRequested();
        return;
    }
    const auto custom = customKeys_.constFind(event->keyCombination().toCombined());
    if (custom != customKeys_.cend()) {
        trigger(*custom);
        return;
    }
    // Foot pedals and presentation remotes send these keys.
    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaTogglePlayPause: emit toggleRequested(); return;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown: emit step(1); return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp: emit step(-1); return;
    case Qt::Key_Home: emit resetRequested(); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal: emit speedRequested(10); return;
    case Qt::Key_Minus: emit speedRequested(-10); return;
    default: QWidget::keyPressEvent(event);
    }
}

// Without a frame the window manager draws no borders, so the reader's own edges resize it.
// The window system does the resizing, which also works on Wayland, where applications cannot
// resize or move their windows themselves. Presses on the edges reach here because the bars and
// the progress line do not use them.
Qt::Edges ReaderPage::edgesAt(const QPoint &point) const {
    const auto *top = window();
    if (!resizable_ || top->isMaximized() || top->isFullScreen())
        return {};
    const QPoint at = mapTo(top, point);
    constexpr int border = Design::Metric::space2;
    Qt::Edges edges;
    if (at.x() < border)
        edges |= Qt::LeftEdge;
    else if (at.x() >= top->width() - border)
        edges |= Qt::RightEdge;
    if (at.y() < border)
        edges |= Qt::TopEdge;
    else if (at.y() >= top->height() - border)
        edges |= Qt::BottomEdge;
    return edges;
}

void ReaderPage::mouseMoveEvent(QMouseEvent *event) {
    const auto edges = edgesAt(event->position().toPoint());
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge), vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
    if (horizontal && vertical)
        setCursor(edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge)
                      ? Qt::SizeFDiagCursor
                      : Qt::SizeBDiagCursor);
    else if (horizontal || vertical)
        setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    else
        unsetCursor();
    QWidget::mouseMoveEvent(event);
}

void ReaderPage::mousePressEvent(QMouseEvent *event) {
    const auto edges = edgesAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && edges && window()->windowHandle() &&
        window()->windowHandle()->startSystemResize(edges)) {
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ReaderPage::leaveEvent(QEvent *event) {
    unsetCursor();
    QWidget::leaveEvent(event);
}

bool ReaderPage::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || (widget != this && !isAncestorOf(widget)))
        return QWidget::eventFilter(object, event);
    if (drawerOpen_ && !roomy_ && event->type() == QEvent::KeyPress &&
        !QApplication::activePopupWidget() && !QApplication::activeModalWidget()) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            QList<QWidget *> controls;
            for (auto *control : drawer_->findChildren<QWidget *>())
                if (control->isVisible() && control->isEnabled() && (control->focusPolicy() & Qt::TabFocus) &&
                    control->visibleRegion().boundingRect().contains(control->rect()))
                    controls << control;
            if (!controls.isEmpty()) {
                const bool back = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier);
                int next = int(controls.indexOf(QApplication::focusWidget())) + (back ? -1 : 1);
                next = (next + controls.size()) % controls.size();
                controls[next]->setFocus(back ? Qt::BacktabFocusReason : Qt::TabFocusReason);
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter ||
        event->type() == QEvent::MouseButtonPress)
        revealControls();
    // The scripts over the text close when the text is touched.
    if (object == text->viewport() && event->type() == QEvent::MouseButtonPress && drawerOpen_ && !roomy_) {
        setDrawerOpen(false);
        return true;
    }
    // While reading, the text view forwards control keys instead of scrolling; while writing it
    // takes them, except Esc.
    if (object == text && event->type() == QEvent::KeyPress && text->isReadOnly() &&
        static_cast<QKeyEvent *>(event)->key() != Qt::Key_Tab &&
        static_cast<QKeyEvent *>(event)->key() != Qt::Key_Backtab) { // Tab and Shift+Tab move the focus.
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return event->isAccepted();
    }
    if (object == drawer_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && !roomy_) {
        setDrawerOpen(false);
        text->setFocus();
        return true;
    }
    return QWidget::eventFilter(object, event);
}
