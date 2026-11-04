// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "script_highlighter.h"
#include "tracker.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>

// Front matter spans lines. It is measured from the first lines (at most 200), which
// stays cheap and also sees text replaced with setPlainText (no revision change).
int ScriptHighlighter::frontMatterEnd() {
    static const QRegularExpression open(QStringLiteral(R"(^---[ \t]*$)"));
    static const QRegularExpression close(QStringLiteral(R"(^(---|\.\.\.)[ \t]*$)"));
    int end = 0;
    auto block = document()->firstBlock();
    if (open.match(block.text()).hasMatch())
        for (int lines = 0; (block = block.next()).isValid() && lines < 200; ++lines)
            if (close.match(block.text()).hasMatch()) {
                end = block.position() + block.length() - 1;
                break;
            }
    if (end != frontMatterEnd_) {
        frontMatterEnd_ = end;
        QTimer::singleShot(0, this, &QSyntaxHighlighter::rehighlight); // Lines away from the edit change too.
    }
    return end;
}

int ScriptHighlighter::headingMarks(const QTextBlock &block) {
    if (!block.isValid() || block.position() < frontMatterEnd())
        return 0;
    const auto text = block.text();
    if (ScriptTracker::headings(text).isEmpty())
        return 0;
    static const QRegularExpression mark(QStringLiteral(R"(^\s*#+\s*)"));
    const auto found = mark.match(text);
    return found.hasMatch() ? int(found.capturedLength()) : 0;
}

double ScriptHighlighter::headingSize(const QTextBlock &block) {
    if (headingScale_ <= 0 || headingMarks(block) == 0)
        return 1.0;
    const auto text = block.text().trimmed();
    int level = 0;
    while (level < text.size() && text.at(level) == u'#')
        ++level;
    static constexpr double levels[] = {1.35, 1.20, 1.15, 1.10};
    return headingScale_ * (level >= 1 && level <= 4 ? levels[level - 1] : 1.0);
}

void ScriptHighlighter::setReveal(int position) {
    if (position == reveal_)
        return;
    const auto before = document()->findBlock(reveal_);
    reveal_ = position;
    const auto after = document()->findBlock(position);
    if (before.isValid())
        rehighlightBlock(before);
    if (after.isValid() && after != before)
        rehighlightBlock(after);
}

void ScriptHighlighter::highlightBlock(const QString &text) {
    if (currentBlock().position() < frontMatterEnd()) {
        QTextCharFormat metadata;
        metadata.setForeground(cue_);
        setFormat(0, int(text.size()), metadata);
        return;
    }
    // Cues and headings never span lines, so each block is self-contained.
    // A mark the reader does not show: transparent and next to no width.
    QTextCharFormat hidden;
    hidden.setForeground(Qt::transparent);
    hidden.setFontPointSize(1);
    QTextCharFormat dimmed;
    dimmed.setForeground(cue_);
    // The caret's place in this block, when it is here: the marks around it show.
    const int caret = reveal_ - currentBlock().position();
    const bool caretHere = reveal_ >= 0 && caret >= 0 && caret <= text.size();
    const auto shown = [&](qsizetype start, qsizetype end) { return caretHere && caret >= start && caret <= end; };
    if (const int marks = headingMarks(currentBlock()); marks > 0) {
        QTextCharFormat heading;
        heading.setFontWeight(headingWeight_);
        heading.setForeground(heading_);
        if (const double size = headingSize(currentBlock()); size != 1.0)
            heading.setFontPointSize((textPoints_ > 0 ? textPoints_ : document()->defaultFont().pointSizeF()) * size);
        setFormat(0, int(text.size()), heading);
        if (hideHeadingMarks_ || (hideMarks_ && !caretHere))
            setFormat(0, marks, hidden);
        else if (hideMarks_)
            setFormat(0, marks, dimmed);
        return;
    }
    // Markdown emphasis: **bold** and *italic*, with the markers dimmed.
    static const QRegularExpression strong(QStringLiteral(R"(\*\*(?=\S)(.+?)(?<=\S)\*\*)"));
    static const QRegularExpression emphasis(QStringLiteral(R"((?<![*\w])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?![*\w]))"));
    const auto marker = [&](const QRegularExpressionMatch &match) {
        return hideMarks_ && !shown(match.capturedStart(), match.capturedEnd()) ? hidden : dimmed;
    };
    // List markers ("- ", "* ", "+ ", "1. "): dimmed like other Markdown markers, and kept in the reader.
    static const QRegularExpression list(QStringLiteral(R"(^\s*([-*+]|\d{1,3}[.)])(?=\s))"));
    if (const auto item = list.match(text); item.hasMatch()) {
        QTextCharFormat bullet = dimmed;
        bullet.setFontWeight(QFont::Bold);
        setFormat(item.capturedStart(1), item.capturedLength(1), bullet);
    }
    for (auto it = strong.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        QTextCharFormat bold = format(match.capturedStart(1));
        bold.setFontWeight(QFont::Bold);
        setFormat(match.capturedStart(1), match.capturedLength(1), bold);
        setFormat(match.capturedStart(), 2, marker(match));
        setFormat(match.capturedEnd() - 2, 2, marker(match));
    }
    for (auto it = emphasis.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        QTextCharFormat italic = format(match.capturedStart(1));
        italic.setFontItalic(true);
        setFormat(match.capturedStart(1), match.capturedLength(1), italic);
        setFormat(match.capturedStart(), 1, marker(match));
        setFormat(match.capturedEnd() - 1, 1, marker(match));
    }
    // ***bold italic***: applied last, so it replaces what the two rules above made of it.
    static const QRegularExpression both(QStringLiteral(R"(\*\*\*(?=\S)(.+?)(?<=\S)\*\*\*)"));
    for (auto it = both.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        QTextCharFormat strongItalic = format(match.capturedStart(1));
        strongItalic.setFontWeight(QFont::Bold);
        strongItalic.setFontItalic(true);
        setFormat(match.capturedStart(1), match.capturedLength(1), strongItalic);
        setFormat(match.capturedStart(), 3, marker(match));
        setFormat(match.capturedEnd() - 3, 3, marker(match));
    }
    QTextCharFormat cue;
    cue.setFontItalic(true);
    cue.setForeground(cue_);
    for (const auto &range : ScriptTracker::cues(text))
        setFormat(range.start, range.length, cue);
}
