// SPDX-License-Identifier: AGPL-3.0-or-later
#include "script_editor.h"
#include "design_tokens.h"
#include "script_highlighter.h"
#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextFrame>
#include <QTimer>
#include <algorithm>

// The margin left of the column, where each heading's "#" marks are drawn: quiet, outside the text,
// so headings line up with the paragraphs.
class HeadingMargin : public QWidget {
public:
    explicit HeadingMargin(ScriptEditor *editor) : QWidget(editor), editor_(editor) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        auto *document = editor_->document();
        auto *layout = document->documentLayout();
        const int scroll = editor_->verticalScrollBar()->value();
        painter.setPen(editor_->muted_);
        // The text starts at the document's margin inside the viewport.
        const qreal right = width() + document->documentMargin() - Design::Metric::space3;
        auto block = editor_->cursorForPosition(QPoint(0, 0)).block();
        for (; block.isValid(); block = block.next()) {
            const QRectF box = layout->blockBoundingRect(block).translated(0, -scroll);
            if (box.top() > height())
                break;
            const int marks = editor_->highlighter_->headingMarks(block);
            if (marks == 0 || !block.layout() || block.layout()->lineCount() == 0)
                continue;
            const auto level = int(block.text().trimmed().indexOf(QLatin1Char(' ')));
            const QString sign(std::clamp(level < 0 ? int(block.text().trimmed().size()) : level, 1, 6), QLatin1Char('#'));
            QFont font = editor_->font();
            font.setPointSizeF(font.pointSizeF() * editor_->highlighter_->headingSize(block));
            painter.setFont(font);
            const auto line = block.layout()->lineAt(0);
            const qreal baseline = box.top() + line.y() + line.ascent();
            painter.drawText(QPointF(right - QFontMetricsF(font).horizontalAdvance(sign), baseline), sign);
        }
    }

private:
    ScriptEditor *editor_;
};

ScriptEditor::ScriptEditor(QWidget *parent)
    : QTextEdit(parent), highlighter_(new ScriptHighlighter(document())), margin_(new HeadingMargin(this)) {
    setAcceptRichText(false);
    setAutoFormatting(QTextEdit::AutoNone);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    highlighter_->setHeadingStyle(kHeadingScale, QFont::DemiBold, true);
    highlighter_->setTextSize(font().pointSizeF());
    // New paragraphs inherit the line height; a text that replaced everything gets it here.
    connect(document(), &QTextDocument::contentsChange, this, [this](int position, int, int added) {
        if (spacing_ || spacingQueued_)
            return;
        const auto end = document()->findBlock(position + added).next();
        for (auto block = document()->findBlock(position); block.isValid() && block != end; block = block.next())
            if (int(block.blockFormat().lineHeight()) != lineHeight()) {
                spacingQueued_ = true; // The document cannot be changed while it reports a change.
                QTimer::singleShot(0, this, &ScriptEditor::spaceLines);
                return;
            }
    });
    connect(document(), &QTextDocument::contentsChanged, margin_, qOverload<>(&QWidget::update));
    connect(this, &QTextEdit::cursorPositionChanged, this, &ScriptEditor::keepCaretOutOfMarks);
    spaceLines();
}

void ScriptEditor::setColors(const QColor &muted, const QColor &text) {
    muted_ = muted;
    highlighter_->setColors(muted, text);
    margin_->update();
}

int ScriptEditor::lineHeight() const {
    const QFontMetricsF metrics(font());
    const qreal size = QFontInfo(font()).pixelSize();
    return metrics.lineSpacing() > 0 ? qRound(100 * kLineHeight * size / metrics.lineSpacing()) : 100;
}

void ScriptEditor::spaceLines() {
    spacingQueued_ = false;
    QTextBlockFormat format;
    format.setLineHeight(lineHeight(), QTextBlockFormat::ProportionalHeight);
    const QSignalBlocker block(this); // Only the layout changes: no text change to report.
    spacing_ = true;
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    if (document()->availableUndoSteps() == 0) {
        // A new text: nothing to undo yet, so the spacing is not an undo step either. Room above
        // the first line and after the last, which scroll with the text.
        document()->setUndoRedoEnabled(false);
        all.mergeBlockFormat(format);
        auto frame = document()->rootFrame()->frameFormat();
        frame.setTopMargin(Design::Metric::space6);
        frame.setBottomMargin(Design::Metric::space8);
        document()->rootFrame()->setFrameFormat(frame);
        document()->setUndoRedoEnabled(true);
    } else {
        // Undone with the edit that made the paragraphs.
        all.joinPreviousEditBlock();
        all.mergeBlockFormat(format);
        all.endEditBlock();
    }
    spacing_ = false;
    margin_->update();
}

void ScriptEditor::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    updateColumn();
}

void ScriptEditor::changeEvent(QEvent *event) {
    QTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        if (document()->firstBlock().blockFormat().lineHeight() != lineHeight())
            spaceLines(); // Another family can have other proportions.
        updateColumn();
        highlighter_->setTextSize(font().pointSizeF()); // Headings take their size from the text's.
    }
}

void ScriptEditor::scrollContentsBy(int dx, int dy) {
    QTextEdit::scrollContentsBy(dx, dy);
    margin_->update();
}

void ScriptEditor::updateColumn() {
    const int document = 2 * int(this->document()->documentMargin());
    const int column = fontMetrics().averageCharWidth() * kMeasure + document;
    // The margins hold the headings' marks: at least "##" and a gap, even in a narrow window.
    QFont heading = font();
    heading.setPointSizeF(heading.pointSizeF() * 1.35); // The largest heading.
    const int marks = QFontMetrics(heading).horizontalAdvance(QStringLiteral("##")) + Design::Metric::space3;
    // Wide: the column in the middle. Narrow: the marks' room on the left only, so the column keeps
    // every pixel it can and words are not broken.
    const int side = (width() - column) / 2;
    const QMargins margins = side >= marks ? QMargins(side, 0, side, 0)
                                           : QMargins(marks, 0, std::max(Design::Metric::space3, width() - column - marks), 0);
    if (margins != viewportMargins()) {
        setViewportMargins(margins);
        // The text wraps at the viewport width: lay it out again for the new column.
        QResizeEvent relayout(size(), QSize(-1, -1));
        QTextEdit::resizeEvent(&relayout);
    }
    margin_->setGeometry(0, contentsRect().top(), margins.left(), viewport()->height());
    margin_->update();
}

// The caret never stops inside a heading's hidden marks: it goes past them, or, moving back from
// the start of the heading, to the end of the line before.
void ScriptEditor::keepCaretOutOfMarks() {
    auto caret = textCursor();
    const int position = caret.position(), previous = lastPosition_;
    lastPosition_ = position;
    if (caret.hasSelection())
        return;
    const auto block = caret.block();
    const int marks = highlighter_->headingMarks(block);
    const int start = block.position() + marks;
    if (marks == 0 || position >= start)
        return;
    const bool back = previous == start && position < previous && block.previous().isValid();
    caret.setPosition(back ? block.position() - 1 : start);
    lastPosition_ = caret.position();
    setTextCursor(caret);
}

void ScriptEditor::keyPressEvent(QKeyEvent *event) {
    // Backspace at the start of a heading makes it a line of text again, as in word processors.
    auto caret = textCursor();
    if (event->key() == Qt::Key_Backspace && !caret.hasSelection() && event->modifiers() == Qt::NoModifier) {
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

// Pasted and dropped text is plain and takes the paragraphs' spacing.
void ScriptEditor::insertFromMimeData(const QMimeData *source) {
    if (!source || !source->hasText())
        return;
    auto caret = textCursor();
    caret.insertText(source->text());
    setTextCursor(caret);
    ensureCursorVisible();
}
