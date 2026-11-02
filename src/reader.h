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
class QProgressBar;
class QPropertyAnimation;
class QToolButton;
class QPushButton;
class QAbstractButton;
class QPainter;
class ScriptHighlighter;
class DragHandle;

enum class Mirror { None, Horizontal, Vertical, Both };
enum class ReadingMode { Manual, Steady, Voice };
// What keys, foot pedals and presentation clickers can do in the reader.
enum class ReaderAction { Toggle, Next, Previous, Faster, Slower, Reset, PreviousSection, NextSection };

// The script text inside the reader. Paints mirrored for beam-splitter rigs and
// keeps the reading position on a fixed line.
class ReaderView : public QTextEdit {
    Q_OBJECT
public:
    explicit ReaderView(QWidget *parent = nullptr);
    void setMirror(Mirror mirror);
    Mirror mirror() const { return mirror_; }
    // Fraction of the visible height, from the top, where the current line sits.
    void setReadingLine(double fraction);
    // Room above the first line for the bar that floats over the text, and the height of the bar
    // over the last lines: the lines under it fade out above it instead of being cut.
    void setTopInset(int pixels);
    void setBottomBar(int pixels) {
        bottomBar_ = pixels;
        viewport()->update();
    }
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
signals:
    void characterClicked(int character);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPoint unmirrored(QPoint point) const;
    void paintMarker(QPainter &painter, int lineCenter) const;
    void paintEdges(QPainter &painter) const;
    int currentLineCenter() const;
    static constexpr int kMarkerSpace = 22;
    Mirror mirror_ = Mirror::None;
    double readingLine_ = 0.3;
    int anchor_ = 0;
    QColor textColor_, markerColor_, fadeColor_;
    ScriptHighlighter *highlighter_;
    int topInset_ = 0;
    int bottomBar_ = 0;
    QPropertyAnimation *scroll_;
};

// The reading view of the one Kalipso window: the script large, the current line at a fixed
// height, and two bars that float over the text and fade while reading. MainWindow shows it in
// place of the editor and turns the window into the reader near the camera.
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
    void setMirror(Mirror mirror) { text->setMirror(mirror); }
    void setReadingLine(double fraction) { text->setReadingLine(fraction); }
    void setMode(ReadingMode mode);
    void setRunning(bool running, const QString &status);
    void setStatus(const QString &status);
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
    // The window has no frame while reading: the page's edges resize it.
    void setResizableEdges(bool resizable);

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
    // Back to writing (the Write button or Esc).
    void writeRequested();
    void settingsRequested();
    // The primary button while blocked (see setBlocked()).
    void blockedRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &point) const;
    void applyPalette(const Design::Palette &palette);
    void updatePrimary();
    void updateLabels();
    void setControlsVisible(bool visible);
    void trigger(ReaderAction action);
    void placeBars();

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
    QPushButton *write_;
    QLabel *countdown_;
    Backdrop *backdrop_;
    Design::Palette colors_ = Design::dark;
    QString resolvedTheme_ = "dark";
    QElapsedTimer paletteAge_;
    ReadingMode readingMode_ = ReadingMode::Voice;
    bool running_ = false;
    bool blocked_ = false;
    int compact_ = -1; // 0 full labels … 3 fewest controls; -1 before the first layout.
    int overlap_ = 0;  // How much of the text the bottom bar covers while it shows.
    QString fontFamily_;
    int textPoints_ = 28;
    bool autoHide_ = true;
    bool controlsVisible_ = true;
    bool resizable_ = false;
    QTimer idle_;
    QList<QWidget *> bars_; // Float over the text: hidden, they give it their room.
    QList<QWidget *> steps_; // Previous and next word.
    QHash<int, ReaderAction> customKeys_;
    int position_ = 0, length_ = 0;
};
