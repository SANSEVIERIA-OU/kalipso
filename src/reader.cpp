// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSizeGrip>
#include <QTextBlock>
#include <QTextFrame>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace {
bool horizontal(Mirror mirror) { return mirror == Mirror::Horizontal || mirror == Mirror::Both; }
bool vertical(Mirror mirror) { return mirror == Mirror::Vertical || mirror == Mirror::Both; }
bool wayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }

} // namespace

// Draws its text on one line, cut with "…" when it does not fit; the whole text is in the
// tooltip. A label that wrapped would make its bar taller and push the controls about.
template <class Label> class OneLine : public Label {
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
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAccessibleName(tr("Kalipso script"));
    // Edits are mirrored into the editor, which owns the undo history.
    document()->setUndoRedoEnabled(false);
    scroll_->setDuration(Design::Duration::glide / 2);
    scroll_->setEasingCurve(QEasingCurve::OutCubic);
}

void ReaderView::setMirror(Mirror mirror) {
    mirror_ = mirror;
    scrollToCharacter(anchor_, false);
    viewport()->update();
}

void ReaderView::setReadingLine(double fraction) {
    readingLine_ = std::clamp(fraction, 0.0, 0.9);
    QResizeEvent event(size(), size());
    resizeEvent(&event);
}

void ReaderView::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    // Leave room after the last line so it can reach the reading line.
    const QSignalBlocker block(this);
    auto format = document()->rootFrame()->frameFormat();
    const double bottom = viewport()->height() * (1.0 - readingLine_);
    if (std::abs(format.bottomMargin() - bottom) > 1 || format.leftMargin() != kMarkerSpace ||
        format.topMargin() != topInset_) {
        format.setBottomMargin(bottom);
        format.setTopMargin(topInset_);
        format.setLeftMargin(kMarkerSpace); // Room for the current-line marker.
        document()->rootFrame()->setFrameFormat(format);
    }
    scrollToCharacter(anchor_, false);
}

void ReaderView::setTopInset(int pixels) {
    if (pixels == topInset_)
        return;
    topInset_ = pixels;
    QResizeEvent event(size(), size());
    resizeEvent(&event);
}

void ReaderView::setLineSpacing(int percent) {
    // New blocks inherit the block format, so the minimal edits of the script
    // keep this spacing; only the format change itself must not reach the editor.
    const QSignalBlocker block(this);
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(std::clamp(percent, 100, 200), QTextBlockFormat::ProportionalHeight);
    all.mergeBlockFormat(format);
    QResizeEvent event(size(), size()); // Same reading line, new line positions.
    resizeEvent(&event);
}

void ReaderView::scrollToCharacter(int character, bool animate) {
    anchor_ = character;
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(character, 0, std::max(0, document()->characterCount() - 1)));
    const QRect line = cursorRect(cursor);
    const int center = line.center().y() + verticalScrollBar()->value();
    // Through the glass, a mirrored line appears where the unmirrored one would.
    const int target = std::clamp(center - int(viewport()->height() * readingLine_), verticalScrollBar()->minimum(),
                                  verticalScrollBar()->maximum());
    scroll_->stop();
    if (!animate || Motion::reduced() || std::abs(target - verticalScrollBar()->value()) > viewport()->height()) {
        verticalScrollBar()->setValue(target);
        return;
    }
    scroll_->setStartValue(verticalScrollBar()->value());
    scroll_->setEndValue(target);
    scroll_->start();
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

    // Top bar: what you are reading; sections, settings, and back to writing.
    auto *topBar = new QWidget;
    topBar->setObjectName("readerBar");
    bars_.append(topBar);
    auto *bar = new QHBoxLayout(topBar);
    bar->setContentsMargins(space3, space1, space2, space1);
    bar->setSpacing(space1);
    handle_ = new DragHandle;
    handle_->title->setText(tr("Kalipso"));
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
    settings_ = quietButton(QString(), tr("Settings"));
    settings_->setObjectName("readerSettings");
    settings_->setAccessibleName(tr("Settings"));
    write_ = quietButton(tr("Write"), tr("Back to writing (Esc)"));
    write_->setObjectName("readerWrite");
    bar->addWidget(handle_, 1);
    bar->addWidget(sections_);
    bar->addWidget(settings_);
    bar->addWidget(write_);
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
    text->setPlaceholderText(tr("Write your script first: press Write."));
    text->installEventFilter(this);
    auto *page = new QVBoxLayout;
    page->setContentsMargins(space4, space2, space5, space2);
    page->addWidget(text);
    layout->addLayout(page, 1);
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
    icons_ = {{previous, "previous"}, {next, "next"}, {sections_, "sections"}, {settings_, "settings"},
              {write_, "edit"}};

    // The menus are the desktop's own, in the window's colours rather than the reader's: without a
    // parent they keep the desktop's look (rounded, shadowed) instead of the reader's style sheet.
    for (auto *menu : {sections_->menu(), modeMenu})
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->setPalette(window()->palette());
            if (!Accessibility::native())
                menu->setStyleSheet(window()->styleSheet());
        });

    // Clicking a control must not move keyboard focus off the script: pedals and
    // clickers send Space and arrows, which would otherwise activate or edit.
    for (auto *control : findChildren<QAbstractButton *>())
        control->setFocusPolicy(Qt::TabFocus);
    // While reading, the bars fade out (the text keeps its place) and come back with the mouse,
    // a key, or a pause.
    idle_.setSingleShot(true);
    idle_.setInterval(2500);
    connect(&idle_, &QTimer::timeout, this, [this] { setControlsVisible(false); });
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
    connect(write_, &QPushButton::clicked, this, &ReaderPage::writeRequested);
    connect(settings_, &QPushButton::clicked, this, &ReaderPage::settingsRequested);
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
        button->setProperty("motion", Icons::motion(name));
        button->setIcon(Icons::icon(name, QColor(palette.text), QColor(palette.muted)));
    }
    handle_->grip->setPixmap(Icons::icon("grip", QColor(palette.muted)).pixmap(18, 18));
    updatePrimary();
    highlight(position_, length_);
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
    if (changed) // Status updates while reading must not bring the controls back.
        revealControls();
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
    status_->setText(status);
    status_->setToolTip(status);
    status_->update();
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
    overlap_ = std::max(0, text->geometry().bottom() + 1 - (height() - bottomHeight));
    text->setBottomBar(controlsVisible_ ? overlap_ : 0);
    top->raise();
    bottom->raise();
}

// Labels shorten before anything wraps or overlaps. Each bar decides for itself, and the bottom
// bar decides for its widest mode (steady, with the speed), so changing the mode never changes the
// other controls: the mode takes its short name, then the start button keeps its icon and the speed
// its number, then the word steps go (keys and pedals still step). The status has the room left;
// with too little it goes (the editor keeps it, and assistive technology hears it).
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
        for (auto *button : {static_cast<QAbstractButton *>(write_), static_cast<QAbstractButton *>(sections_)})
            button->setText(icons ? QString() : button->property("label").toString());
        sections_->setToolButtonStyle(icons ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        write_->updateGeometry();
        sections_->updateGeometry();
        if (need(bars_.first(), {}) + words <= width())
            break;
    }
    // Bottom bar, measured with the speed shown and the longest mode name.
    QString longest;
    for (auto *action : modes_->menu()->actions())
        if (action->text().size() > longest.size())
            longest = action->text();
    QString widest;
    for (const auto &label : {tr("Start"), tr("Pause"), tr("Choose language")})
        if (fontMetrics().horizontalAdvance(label) > fontMetrics().horizontalAdvance(widest))
            widest = label;
    const auto apply = [this, &longest, &widest](int level, bool measuring) {
        compact_ = level;
        const auto mode = modes_->property("label").toString();
        modes_->setText(level == 0 ? (measuring ? longest : mode)
                        : measuring || readingMode_ == ReadingMode::Steady ? tr("Steady")
                        : readingMode_ == ReadingMode::Voice                ? tr("Voice")
                                                                            : mode);
        primary_->setText(level >= 2 ? QString() : measuring ? widest : primary_->property("label").toString());
        speed_->setText(level >= 2 ? QString::number(speed_->property("wpm").toInt())
                                   : tr("%1 words/min").arg(speed_->property("wpm").toInt()));
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

void ReaderPage::setTitle(const QString &title) {
    handle_->title->setLine(title.isEmpty() ? tr("Kalipso") : title);
}

void ReaderPage::setProgress(int read, int total) {
    progress_->setRange(0, std::max(1, total));
    progress_->setValue(std::clamp(read, 0, std::max(1, total)));
}

void ReaderPage::setSpeed(int wordsPerMinute) {
    speed_->setProperty("wpm", wordsPerMinute);
    speed_->setText(compact_ >= 2 ? QString::number(wordsPerMinute) : tr("%1 words/min").arg(wordsPerMinute));
    speed_->setAccessibleName(tr("%1 words/min").arg(wordsPerMinute));
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
    if (countdown_->isVisible())
        showCountdown(countdown_->text().toInt());
}

void ReaderPage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    // The style can change while the page is still being built (the first page sets the app's style).
    if (bars_.size() < 2 || !status_)
        return;
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
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
    if (event->key() != Qt::Key_Escape) {
        const auto custom = customKeys_.constFind(event->keyCombination().toCombined());
        if (custom != customKeys_.cend()) {
            trigger(*custom);
            return;
        }
    }
    // Foot pedals and presentation remotes send these keys.
    switch (event->key()) {
    case Qt::Key_Escape: emit writeRequested(); return;
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
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter ||
        event->type() == QEvent::MouseButtonPress)
        revealControls();
    // The text view forwards control keys instead of scrolling.
    if (object == text && event->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return event->isAccepted();
    }
    return QWidget::eventFilter(object, event);
}
