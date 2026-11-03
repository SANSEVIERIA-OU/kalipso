// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QSyntaxHighlighter>

// Styles [direction cues], "# section" headings, list markers and YAML front
// matter in the editor and reader.
// Formats are layout-only: the document text and undo history are untouched.
class ScriptHighlighter : public QSyntaxHighlighter {
public:
    explicit ScriptHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
    // The reader hides Markdown marks (# before headings, * and ** around emphasis): they are for
    // writing, not for reading aloud. The editor keeps them visible and dimmed.
    void setHideMarks(bool hide) {
        hideMarks_ = hide;
        rehighlight();
    }
    // While writing, the marks of the heading or emphasis at this position show, dimmed, so the caret
    // never moves through marks it cannot see; every other mark stays hidden. -1: none.
    void setReveal(int position);
    void setColors(const QColor &cue, const QColor &heading) {
        cue_ = cue;
        heading_ = heading;
        rehighlight();
    }
    // The editor: headings larger than the text and their "#" marks hidden, since the editor draws
    // them in its margin. The reader hides every mark instead (setHideMarks).
    // Sizes by level are Kirigami.Heading's (1.35, 1.20, 1.15, 1.10 and 1 below), times `scale`.
    void setHeadingStyle(double scale, QFont::Weight weight, bool hideMarks) {
        headingScale_ = scale;
        headingWeight_ = weight;
        hideHeadingMarks_ = hideMarks;
        rehighlight();
    }
    // The "#" marks of a section heading: their length, or 0 when the line is not a heading (also
    // a "#" comment in the front matter is not).
    int headingMarks(const QTextBlock &block);
    // The heading's size relative to the text: 1 unless setHeadingStyle() asked for sizes.
    double headingSize(const QTextBlock &block);
    // The text's size, which heading sizes multiply (the editor's font, set when it changes).
    void setTextSize(qreal points) {
        if (points == textPoints_)
            return;
        textPoints_ = points;
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override;

private:
    int frontMatterEnd();
    int frontMatterEnd_ = 0;
    int reveal_ = -1;
    bool hideMarks_ = false;
    bool hideHeadingMarks_ = false;
    double headingScale_ = 0; // 0: headings keep the text's size (the reader).
    qreal textPoints_ = 0;
    QFont::Weight headingWeight_ = QFont::Bold;
    QColor cue_{0x62, 0x68, 0x75}, heading_{0x24, 0x47, 0xd3};
};
