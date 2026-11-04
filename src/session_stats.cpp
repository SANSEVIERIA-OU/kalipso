// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "session_stats.h"
#include <QCoreApplication>
#include <QLocale>

namespace {
QString clock(qint64 ms) {
    const qint64 seconds = (ms + 500) / 1000;
    const auto remainder = QLocale().toString(seconds % 60);
    return QStringLiteral("%1:%2").arg(QLocale().toString(seconds / 60),
                                       seconds % 60 < 10 ? QLocale().toString(0) + remainder : remainder);
}

// Markdown table cells cannot contain "|" or line breaks.
QString cell(QString text) { return text.replace(u'|', u'/').simplified(); }
} // namespace

QString SessionStats::report(const Summary &summary, const QString &script, const QString &title,
                             const QDateTime &when) {
    const auto words = ScriptTracker::tokenize(script);
    QStringList lines{QCoreApplication::translate("SessionStats", "# Session report: %1").arg(cell(title)), QString(),
                      QLocale().toString(when, QLocale::LongFormat), QString(),
                      QCoreApplication::translate("SessionStats", "- Duration: %1").arg(clock(summary.durationMs))};
    ScriptTracker counter;
    counter.setScript(script);
    lines << QCoreApplication::translate("SessionStats", "- Words read: %n of %1", nullptr, summary.words)
                 .arg(QLocale().toString(counter.count()));
    if (summary.wordsPerMinute > 0)
        lines << QCoreApplication::translate("SessionStats", "- Pace: %1 words/min")
                     .arg(QLocale().toString(qRound(summary.wordsPerMinute)));
    // Against the steady speed: the time these words would have taken, and the difference.
    if (summary.plannedWordsPerMinute > 0 && summary.wordsPerMinute > 0) {
        const qint64 planned = qint64(summary.words) * 60000 / summary.plannedWordsPerMinute;
        const int difference = paceAgainstPlan(summary);
        const auto comparison =
            std::abs(difference) < 3 ? QCoreApplication::translate("SessionStats", "as planned")
            : difference > 0
                ? QCoreApplication::translate("SessionStats", "%1% faster").arg(QLocale().toString(difference))
                : QCoreApplication::translate("SessionStats", "%1% slower").arg(QLocale().toString(-difference));
        lines << QCoreApplication::translate("SessionStats",
                                             "- At the planned %1 words/min: %2 expected, %3 taken (%4)")
                     .arg(QLocale().toString(summary.plannedWordsPerMinute))
                     .arg(clock(planned), clock(summary.durationMs), comparison);
    }
    lines << QCoreApplication::translate("SessionStats", "- Pauses over 3 s: %n", nullptr, summary.longPauses);
    if (summary.longPauses)
        lines.last() += QCoreApplication::translate("SessionStats", " (longest %1)").arg(clock(summary.longestPauseMs));
    if (summary.fillersCounted)
        lines << QCoreApplication::translate(
            "SessionStats", "- Filler words recognized: %n (many recognizers drop them, so this is a lower bound)",
            nullptr, summary.fillers);

    // Time per section.
    const auto sections = ScriptTracker::sections(script);
    if (!sections.isEmpty() && !summary.reached.isEmpty()) {
        const auto firstWord = [&words](int character) {
            int index = 0;
            while (index < words.size() && words[index].start < character)
                ++index;
            return index;
        };
        QStringList rows;
        for (int i = 0; i < sections.size(); ++i) {
            const int begin = firstWord(sections[i].start);
            const int end = i + 1 < sections.size() ? firstWord(sections[i + 1].start) : int(words.size());
            // From when the word before the section was reached to when its last word was.
            qint64 previousAt = 0, entered = -1, left = -1;
            int last = -1;
            for (const auto &step : summary.reached) {
                if (step.position >= begin && step.position < end) {
                    if (entered < 0)
                        entered = previousAt;
                    left = step.atMs;
                    last = std::max(last, step.position);
                }
                previousAt = step.atMs;
            }
            if (entered < 0)
                continue;
            const int read = last - begin + 1;
            const double minutes = (left - entered) / 60000.0;
            rows << QStringLiteral("| %1 | %2 | %3 | %4 |")
                        .arg(cell(sections[i].title), clock(left - entered))
                        .arg(read)
                        .arg(left - entered >= 1000 ? QString::number(qRound(read / minutes)) : QStringLiteral("–"));
        }
        if (!rows.isEmpty())
            lines << QString() << QCoreApplication::translate("SessionStats", "## Sections") << QString()
                  << QCoreApplication::translate("SessionStats", "| Section | Time | Words | Words/min |")
                  << QStringLiteral("| --- | ---: | ---: | ---: |") << rows;
    }

    if (!summary.pauses.isEmpty()) {
        lines << QString() << QCoreApplication::translate("SessionStats", "## Long pauses") << QString();
        for (const auto &pause : summary.pauses) {
            if (pause.position < 0 || pause.position >= words.size()) {
                lines << QCoreApplication::translate("SessionStats", "- %1 before the first word").arg(clock(pause.ms));
                continue;
            }
            const auto &from = words[std::max(0, pause.position - 5)];
            const auto &to = words[pause.position];
            const auto context = script.mid(from.start, to.start + to.length - from.start).simplified();
            lines << QCoreApplication::translate("SessionStats", "- %1 after “…%2”").arg(clock(pause.ms), context);
        }
    }
    return lines.join(u'\n') + u'\n';
}
