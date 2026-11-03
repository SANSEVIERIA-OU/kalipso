// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "accessibility.h"
#include "backdrop.h"
#include "tracker.h"
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTextEdit>
#include <QTimer>

class QComboBox;
class QMenu;
class QProgressBar;
class QPropertyAnimation;
class QToolButton;
class QPushButton;
class QAbstractButton;
class QPainter;
class QStackedWidget;
class ScriptHighlighter;
class DragHandle;

enum class Mirror { None, Horizontal, Vertical, Both };
enum class ReadingMode { Manual, Steady, Voice };
// What keys, foot pedals and presentation clickers can do in the reader.
enum class ReaderAction { Toggle, Next, Previous, Faster, Slower, Reset, PreviousSection, NextSection };

// The script, as you write it and as you read it: the same text, the same size and the same place.
// While reading it is read-only, can be mirrored for beam-splitter rigs and keeps the reading
// position on a fixed line. While writing it is an editor: Markdown marks stay hidden except around
// the caret, pasted text is plain, and the text keeps its own undo history.
class ReaderView : public QTextEdit {
    Q_OBJECT
public:
    explicit ReaderView(QWidget *parent = nullptr);
    void setMirror(Mirror mirror);
    Mirror mirror() const { return mirror_; }
    // Writing (the caret, the marks around it) or reading (read-only, mirrored, the reading line).
    void setEditable(bool editable);
    bool editable() const { return !isReadOnly(); }
    // Fraction of the visible height, from the top, where the current line sits.
    void setReadingLine(double fraction);
    // The bars over the text. While reading they float over it and fade: the first line starts
    // under the top bar and the lines under the bottom bar fade above it. While writing the text
    // stays between them.
    void setTopInset(int pixels);
    void setBottomBar(int pixels);
    void scrollToCharacter(int character, bool animate);
    void setTextColor(const QColor &color) { textColor_ = color; }
    void setFadeColor(const QColor &color) { fadeColor_ = color; }
    void setMarkerColor(const QColor &color) {
        markerColor_ = color;
        viewport()->update();
    }
    ScriptHighlighter *highlighter() const { return highlighter_; }
    // Line height in percent of the normal one (100 = font default).
    void setLineSpacing(int percent);
    // Gives every paragraph the line height and the margin for the marker. Paragraphs made by
    // typing inherit them; call this after replacing the whole text (setPlainText).
    void spaceLines();
    // Characters per line at most: in a wide window the text keeps a comfortable measure, in the
    // middle (the measure typographers give as ideal).
    static constexpr int kMeasure = 66;
signals:
    void characterClicked(int character);
    // While writing: the caret is somewhere else, where reading would start.
    void caretMoved(int character);
    // Ctrl and the mouse wheel: one step larger (1) or smaller (-1).
    void textSizeStep(int direction);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;

private:
    QPoint unmirrored(QPoint point) const;
    void paintMarker(QPainter &painter, int lineCenter) const;
    void paintEdges(QPainter &painter) const;
    int currentLineCenter() const;
    // The column's side margins, the bars' room while writing, and the scroll range that lets the
    // first and last lines reach the reading line while reading.
    void relayout(bool force);
    void extendScrollRange();
    void caretChanged();
    static constexpr int kMarkerSpace = 22;
    Mirror mirror_ = Mirror::None;
    double readingLine_ = 0.3;
    int anchor_ = 0;
    QColor textColor_, markerColor_, fadeColor_;
    ScriptHighlighter *highlighter_;
    int topInset_ = 0;
    int bottomBar_ = 0;
    int lineSpacing_ = 100;
    int wheel_ = 0;
    bool extending_ = false;
    bool spacing_ = false;
    bool spacingQueued_ = false;
    QPropertyAnimation *scroll_;
};

// The one Kalipso window: the script large, in the reader's colours, whether you write it or read
// it. Small, as a teleprompter under the camera, it shows only what reading needs. Large, it also
// shows the formats, the scripts and the editing; the text is the same either way.
class ReaderPage : public QWidget {
    Q_OBJECT
public:
    explicit ReaderPage(QWidget *parent = nullptr);
    ~ReaderPage() override;
    ReaderView *text;

    void highlight(int position, int length);
    // Character at the reading line (the current word, or where reading starts).
    int anchor() const { return position_; }
    void setTextSize(int points);
    // Empty: the interface font. Any installed family, e.g. one designed for dyslexia.
    void setTextFont(const QString &family);
    void setLineSpacing(int percent) { text->setLineSpacing(percent); }
    void setReaderTheme(const QString &theme);
    // The colours the reader draws with now (its theme, or the window behind it).
    const Design::Palette &colors() const { return colors_; }
    void setMirror(Mirror mirror) { text->setMirror(mirror); }
    void setReadingLine(double fraction) { text->setReadingLine(fraction); }
    void setMode(ReadingMode mode);
    void setRunning(bool running, const QString &status);
    void setStatus(const QString &status);
    // A message about what just happened ("Script saved."), shown in place of the status for a
    // few seconds and said to assistive technology.
    void showMessage(const QString &message);
    // Voice following cannot start yet (no language): the start button says "Choose language" and
    // sends blockedRequested() instead.
    void setBlocked(bool blocked);
    // Shows the controls and the status, which hide while reading, until the reader is idle again.
    void revealControls();
    void setSpeed(int wordsPerMinute);
    void setSections(const QVector<Section> &sections);
    // Shows a large number over the text; 0 hides it.
    void showCountdown(int seconds);
    void setTitle(const QString &title);
    // Fade the bars out while reading; they return with the mouse or a key.
    void setAutoHideControls(bool hide);
    bool controlsVisible() const { return controlsVisible_; }
    void setProgress(int read, int total);
    // The window has no frame under the camera: the page's edges resize it.
    void setResizableEdges(bool resizable);

    // Writing: the formats (bold, italic, section, cue) as buttons in the top bar.
    void setFormatActions(const QList<QAction *> &actions);
    // The scripts: a panel that opens from the left. Beside the text in a large window, over it in a
    // small one.
    void setDrawer(QWidget *drawer);
    void setDrawerOpen(bool open);
    bool drawerOpen() const { return drawerOpen_; }
    // The caret at a character, while writing (where reading stopped, or a script just opened).
    void placeCaret(int character);
    // The first-run page, shown in the text's place until there is a script.
    void setWelcome(QWidget *welcome);
    void showWelcome(bool shown);
    bool welcomeShown() const;
    // A button drawn in the reader's colours, with an icon that follows them.
    void addIconButton(QAbstractButton *button, const QString &icon);
    // A menu opened from the reader, in the reader's colours: the desktop's own menu with them, or
    // the drawn one.
    void styleMenu(QMenu *menu) const;
    // Whether the window sits under the camera (the place button then says it makes it larger).
    void setUnderCamera(bool underCamera);
    // Large enough to write in: wider than 960 and taller than 600, the largest reader Kalipso
    // proposes (two fifths of the screen at most), or maximized. Full screen is for reading.
    static bool isRoomy(const QSize &window, Qt::WindowStates states);
    bool roomy() const { return roomy_; }
    bool editing() const { return editing_; }

    QString colorSource() const { return backdrop_->source(); }
    bool usesWindowPortal() const { return backdrop_->usesWindowPortal(); }
    void chooseBackgroundWindow() { backdrop_->chooseWindow(); }
    void stopWindowSharing() { backdrop_->stopWindowSharing(); }

    // Keys chosen in Settings (key combination → action). They take precedence over the
    // built-in keys, so a built-in key can also be given another action.
    void setCustomKeys(const QHash<int, ReaderAction> &keys) { customKeys_ = keys; }
signals:
    void step(int direction);
    void sectionRequested(int direction);
    void toggleRequested();
    void resetRequested();
    void speedRequested(int delta);
    void modeRequested(ReadingMode mode);
    void jumpRequested(int character);
    void colorStatus(const QString &text);
    void settingsRequested();
    // The primary button while blocked (see setBlocked()).
    void blockedRequested();
    // A size step for the text: 1 larger, -1 smaller.
    void textSizeRequested(int direction);
    // Under the camera, or back to a large window.
    void placeRequested();
    // Writing started or ended (the window's size, or reading).
    void editingChanged(bool editing);
    // The scripts panel opened (true) or is closed and out of sight (false).
    void drawerShown(bool shown);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &point) const;
    void applyPalette(const Design::Palette &palette);
    void updatePrimary();
    void updateLabels();
    void updateEditing();
    void setControlsVisible(bool visible);
    void trigger(ReaderAction action);
    void placeBars();
    void placeDrawer();
    int drawerWidth() const;
    // Icons as large as the text beside them, at any interface size.
    void sizeIcons();
    // Tab goes the way the eye does: the top bar from left to right, the scripts, the text, the bottom bar.
    void orderTabs();

    StatusLabel *status_ = nullptr;
    QPushButton *primary_;
    QToolButton *modes_;
    DragHandle *handle_;
    QProgressBar *progress_;
    QList<QPair<QAbstractButton *, QString>> icons_;
    QWidget *speedBox_;
    QLabel *speed_;
    QToolButton *sections_;
    QPushButton *settings_;
    QPushButton *drawerButton_;
    QPushButton *smaller_, *larger_;
    QPushButton *place_;
    QWidget *formats_;
    QStackedWidget *pages_;
    class QVBoxLayout *pageLayout_;
    QWidget *welcome_ = nullptr;
    QWidget *drawer_ = nullptr;
    QPropertyAnimation *drawerSlide_ = nullptr;
    QWidget *scrim_ = nullptr; // Over the text behind the scripts in a small window.
    bool drawerOpen_ = false;
    bool drawerWanted_[2] = {false, true}; // Closed in a small window, open in a large one, until changed.
    QLabel *countdown_;
    Backdrop *backdrop_;
    Design::Palette colors_ = Design::dark;
    QString resolvedTheme_ = "dark";
    QElapsedTimer paletteAge_;
    ReadingMode readingMode_ = ReadingMode::Voice;
    bool running_ = false;
    bool blocked_ = false;
    bool underCamera_ = false;
    bool roomy_ = false;
    bool editing_ = false;
    int compact_ = -1; // 0 full labels … 3 fewest controls; -1 before the first layout.
    int overlap_ = 0;  // How much of the text the bottom bar covers while it shows.
    QString fontFamily_;
    int textPoints_ = 28;
    bool autoHide_ = true;
    bool controlsVisible_ = true;
    bool resizable_ = false;
    QTimer idle_;
    QTimer message_;
    QString statusText_; // The status under a message.
    QList<QWidget *> bars_; // Float over the text while reading: hidden, they give it their room.
    QList<QWidget *> steps_; // Previous and next word.
    QHash<int, ReaderAction> customKeys_;
    int position_ = 0, length_ = 0;
};
