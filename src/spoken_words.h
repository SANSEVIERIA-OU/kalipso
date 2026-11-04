// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include <QString>
#include <QStringList>

// How a written word and a heard word are compared, and how a script is turned into the words a
// recognizer can hear. Measured with benchmarks/follow_probe.cpp on Italian read speech: in a room
// (reverberation, steady noise) the small Vosk model mishears about one word in four, mostly as
// words that are not in the script, and as near forms of the right word ("è" for "e",
// "caratterizzata" for "caratterizzati", "duemilaundici" for "2011").
namespace SpokenWords {

// A word as the tracker compares it: lower case, Latin letters without accents ("È" → "e",
// "perché" → "perche"). Other scripts keep their marks (Japanese dakuten change the word).
QString fold(const QString &word);

// A whole number said as one word, as Italian writes numbers ("2011" → "duemilaundici", "20" →
// "venti"); empty for numbers with no one-word form (a million and more) or with leading zeros.
QString italianNumber(const QString &digits);

// Where the tracker meets a word: its folded form, the number it spells out, and whether it was
// cut short by an apostrophe ("vent'", "dell'", "l'").
struct Key {
    QString folded;
    QString number; // Italian words for a number written in digits, folded; else empty.
    bool elided = false;
};
// The key of the word at [start, start + length) in text.
Key key(const QString &text, int start, int length, const QString &normalized);

// Whether a heard word is the script's word: the same once folded; a number and its words; an
// elided form and its full word ("vent'" and "venti"); or the same long word with another final
// vowel ("caratterizzata", "caratterizzati"), the commonest small-model slip in Italian.
bool same(const Key &heard, const Key &written);

// The sentences of a script as a recognizer writes them, for a recognizer that can learn them
// (Vosk turns them into a two-word language model): lower case, numbers spelt out in the language
// when it says them as one word, an elided word joined to the next one ("l'incidente"). A word
// with spellings the recognizer may or may not know lists them with "|" ("ventitré|ventitre").
QStringList recognizerPhrases(const QString &script, const QString &language);

} // namespace SpokenWords
