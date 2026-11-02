// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QTextEdit>

class ScriptHighlighter;

// The script as you write it: plain text in a comfortable reading column (about 66 characters, the
// measure typographers give as ideal), lines one and a half apart, and sections shown as headings
// whose "#" marks sit in the margin, like the heading marks of Bear. The marks stay in the text:
// the caret steps over them, and Backspace at the start of a heading turns it back into a line.
class ScriptEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit ScriptEditor(QWidget *parent = nullptr);
    ScriptHighlighter *highlighter() const { return highlighter_; }
    // Secondary marks (cues, "#" in the margin) and text.
    void setColors(const QColor &muted, const QColor &text);
    // Line height: one and a half times the text size, as typographers and WCAG 1.4.12 measure it
    // (Qt measures it from the font's own line spacing; lineHeight() converts).
    static constexpr double kLineHeight = 1.5;
    int lineHeight() const;
    // Characters per line of the column.
    static constexpr int kMeasure = 66;
    // Headings keep Kirigami's proportions to the text (see ScriptHighlighter::setHeadingStyle).
    static constexpr double kHeadingScale = 1.0;
    // Gives every paragraph the line height. Paragraphs made by typing inherit it; call this after
    // replacing the whole text (setPlainText), or it follows a moment later.
    void spaceLines();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    friend class HeadingMargin;
    void updateColumn();
    void keepCaretOutOfMarks();
    ScriptHighlighter *highlighter_;
    QWidget *margin_;
    QColor muted_;
    int lastPosition_ = 0;
    bool spacing_ = false;
    bool spacingQueued_ = false;
};
