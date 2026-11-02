// SPDX-License-Identifier: AGPL-3.0-or-later
#include "spoken_words.h"
#include "tracker.h"
#include <algorithm>

namespace SpokenWords {
namespace {
bool apostrophe(QChar c) { return c == u'\'' || c == u'’' || c == u'ʼ'; }
bool vowel(QChar c) { return QStringLiteral("aeiou").contains(c); }

const char *const kUnits[] = {"zero",     "uno",         "due",      "tre",         "quattro",
                              "cinque",   "sei",         "sette",    "otto",        "nove",
                              "dieci",    "undici",      "dodici",   "tredici",     "quattordici",
                              "quindici", "sedici",      "diciassette", "diciotto", "diciannove"};
const char *const kTens[] = {"", "", "venti", "trenta", "quaranta", "cinquanta", "sessanta", "settanta", "ottanta",
                             "novanta"};

// 1..99. "Tre" at the end of a longer number takes its accent: ventitré.
QString below100(int n) {
    if (n < 20)
        return QString::fromLatin1(kUnits[n]);
    QString tens = QString::fromLatin1(kTens[n / 10]);
    const int unit = n % 10;
    if (unit == 0)
        return tens;
    if (unit == 1 || unit == 8)
        tens.chop(1); // ventuno, ventotto
    return tens + (unit == 3 ? QStringLiteral("tré") : QString::fromLatin1(kUnits[unit]));
}

// 1..999
QString below1000(int n) {
    const int hundreds = n / 100, rest = n % 100;
    QString words = hundreds == 0   ? QString()
                    : hundreds == 1 ? QStringLiteral("cento")
                                    : QString::fromLatin1(kUnits[hundreds]) + QStringLiteral("cento");
    if (rest == 0)
        return words;
    QString tail = rest == 3 && hundreds ? QStringLiteral("tré") : below100(rest);
    if (hundreds && tail.startsWith(u'o'))
        words.chop(1); // centotto, centottanta
    return words + tail;
}
} // namespace

// The same rule as fold() in desktop/tracker.mjs: combining diacritics (U+0300–U+036F) after a
// Latin letter (U+0000–U+024F, U+1E00–U+1EFF) are dropped.
QString fold(const QString &word) {
    const auto decomposed = word.toLower().normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(decomposed.size());
    bool latin = false;
    for (const QChar c : decomposed) {
        const auto code = c.unicode();
        if (code >= 0x300 && code <= 0x36f) {
            if (!latin)
                result += c;
            continue;
        }
        latin = code < 0x250 || (code >= 0x1e00 && code <= 0x1eff);
        result += c;
    }
    return result.normalized(QString::NormalizationForm_C);
}

QString italianNumber(const QString &digits) {
    if (digits.isEmpty() || digits.size() > 6 || (digits.size() > 1 && digits.front() == u'0'))
        return {};
    for (const QChar c : digits)
        if (c < u'0' || c > u'9')
            return {};
    const int n = digits.toInt();
    if (n == 0)
        return QStringLiteral("zero");
    if (n < 1000)
        return below1000(n);
    const int thousands = n / 1000, rest = n % 1000;
    QString words = thousands == 1 ? QStringLiteral("mille") : below1000(thousands) + QStringLiteral("mila");
    if (rest)
        words += rest == 3 ? QStringLiteral("tré") : below1000(rest);
    return words;
}

Key key(const QString &text, int start, int length, const QString &normalized) {
    Key result;
    result.folded = fold(normalized);
    result.number = fold(italianNumber(normalized));
    const int end = start + length;
    result.elided = end + 1 < text.size() && apostrophe(text[end]) && text[end + 1].isLetter();
    return result;
}

bool same(const Key &heard, const Key &written) {
    if (heard.folded == written.folded)
        return true;
    if ((!written.number.isEmpty() && heard.folded == written.number) ||
        (!heard.number.isEmpty() && written.folded == heard.number))
        return true;
    const auto elisionOf = [](const QString &cut, const QString &full) {
        return full.size() == cut.size() + 1 && full.startsWith(cut) && vowel(full.back());
    };
    if (heard.elided && (elisionOf(heard.folded, written.folded) ||
                         (!written.number.isEmpty() && elisionOf(heard.folded, written.number))))
        return true;
    if (written.elided && elisionOf(written.folded, heard.folded))
        return true;
    const auto &a = heard.folded, &b = written.folded;
    return a.size() >= 6 && a.size() == b.size() && vowel(a.back()) && vowel(b.back()) &&
           QStringView(a).chopped(1) == QStringView(b).chopped(1);
}

QStringList recognizerPhrases(const QString &script, const QString &language) {
    const auto words = ScriptTracker::tokenize(script);
    const bool italian = language.section(u'-', 0, 0).compare(QLatin1String("it"), Qt::CaseInsensitive) == 0;
    QStringList phrases, phrase;
    const auto close = [&] {
        if (!phrase.isEmpty())
            phrases << phrase.join(u' ');
        phrase.clear();
    };
    static const QString stops = QStringLiteral(".!?;:\n[#");
    for (int i = 0; i < words.size(); ++i) {
        const auto &word = words[i];
        if (i > 0) {
            // A sentence ends at its punctuation, a line break, or a cue or heading left out.
            const int from = words[i - 1].start + words[i - 1].length;
            const auto between = QStringView(script).mid(from, word.start - from);
            if (std::any_of(between.begin(), between.end(), [](QChar c) { return stops.contains(c); }))
                close();
        }
        const auto written = script.mid(word.start, word.length).toLower();
        const auto number = italian ? italianNumber(written) : QString();
        if (!number.isEmpty()) {
            phrase << (number.endsWith(QStringLiteral("tré")) ? number + u'|' + number.chopped(1) + u'e' : number);
            continue;
        }
        // "l'incidente": recognizers write an elided word together with the next one.
        const int end = word.start + word.length;
        if (end + 1 < script.size() && apostrophe(script[end]) && i + 1 < words.size() &&
            words[i + 1].start == end + 1) {
            const auto next = script.mid(words[i + 1].start, words[i + 1].length).toLower();
            phrase << written + u'\'' + next;
            ++i;
            continue;
        }
        phrase << written;
    }
    close();
    return phrases;
}

} // namespace SpokenWords
