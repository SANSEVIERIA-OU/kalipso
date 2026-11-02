// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
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
    void setColors(const QColor &cue, const QColor &heading) {
        cue_ = cue;
        heading_ = heading;
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override;

private:
    int frontMatterEnd();
    int frontMatterEnd_ = 0;
    bool hideMarks_ = false;
    QColor cue_{0x62, 0x68, 0x75}, heading_{0x24, 0x47, 0xd3};
};
