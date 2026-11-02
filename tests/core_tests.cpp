// SPDX-License-Identifier: AGPL-3.0-or-later
#include "spoken_words.h"
#include "tracker.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void multilingualAlignment() {
        QFile fixtures(QFINDTESTDATA("multilingual-tracker.json"));
        QVERIFY(fixtures.open(QIODevice::ReadOnly));
        for (const auto &value : QJsonDocument::fromJson(fixtures.readAll()).array()) {
            const auto data = value.toObject();
            const auto script = data["script"].toString();
            ScriptTracker tracker;
            tracker.setScript(script);
            QVERIFY2(tracker.follow(data["partial"].toString()), qPrintable(script));
            QCOMPARE(script.left(tracker.character() + tracker.length()), data["read"].toString());
            const auto before = tracker.character();
            tracker.setScript(QString::fromUtf8("🎤 ") + script);
            QCOMPARE(tracker.character(), before + 3);
        }
    }
    void italianWords() {
        const auto words = ScriptTracker::tokenize("È l'obiettivo: più qualità, 2026.");
        QCOMPARE(words.size(), 6);
        QCOMPARE(words[0].normalized, QString("è"));
        QCOMPARE(words[2].normalized, QString("obiettivo"));
        QCOMPARE(words[2].start, 4);
    }
    void italianNumbersAsSaid() {
        const QList<QPair<QString, QString>> numbers{
            {"0", "zero"},           {"1", "uno"},
            {"15", "quindici"},      {"20", "venti"},
            {"21", "ventuno"},       {"23", "ventitré"},
            {"28", "ventotto"},      {"60", "sessanta"},
            {"100", "cento"},        {"103", "centotré"},
            {"108", "centotto"},     {"180", "centottanta"},
            {"999", "novecentonovantanove"}, {"1000", "mille"},
            {"1977", "millenovecentosettantasette"}, {"2011", "duemilaundici"},
            {"2017", "duemiladiciassette"}, {"2027", "duemilaventisette"},
            {"21000", "ventunomila"}, {"100003", "centomilatré"}};
        for (const auto &[digits, words] : numbers)
            QCOMPARE(SpokenWords::italianNumber(digits), words);
        for (const auto *none : {"", "007", "1000000", "12a", "-3"})
            QVERIFY2(SpokenWords::italianNumber(none).isEmpty(), none);
    }
    void heardWordsMatchTheirWrittenForms() {
        const auto keyOf = [](const QString &text, int index = 0) {
            const auto words = ScriptTracker::tokenize(text);
            return SpokenWords::key(text, words[index].start, words[index].length, words[index].normalized);
        };
        QCOMPARE(SpokenWords::fold("È PERCHÉ più"), QString("e perche piu"));
        QCOMPARE(SpokenWords::fold("がっこう"), QString("がっこう")); // Kana keep their marks.
        QVERIFY(SpokenWords::same(keyOf("e"), keyOf("è")));               // The commonest slip.
        QVERIFY(SpokenWords::same(keyOf("duemilaundici"), keyOf("2011"))); // Numbers as said.
        QVERIFY(SpokenWords::same(keyOf("vent'anni"), keyOf("20 anni")));  // Elided number.
        QVERIFY(SpokenWords::same(keyOf("dell'anno"), keyOf("della")));
        QVERIFY(SpokenWords::same(keyOf("caratterizzata"), keyOf("caratterizzati")));
        QVERIFY(!SpokenWords::same(keyOf("vent"), keyOf("venti")));       // Not cut by an apostrophe.
        QVERIFY(!SpokenWords::same(keyOf("casa"), keyOf("case")));        // Too short to be the same word.
        QVERIFY(!SpokenWords::same(keyOf("cantare"), keyOf("cantano")));  // More than the last vowel.
        QVERIFY(!SpokenWords::same(keyOf("ventuno"), keyOf("20")));
    }
    void followsItalianAsTheRecognizerWritesIt() {
        // The small Vosk model writes numbers in words, confuses "è" and "e", and gets endings wrong.
        ScriptTracker tracker;
        tracker.setScript("Sotto il ponte lo spazio libero è di 15 metri. La costruzione è terminata nel 2011.");
        QVERIFY(tracker.follow("sotto il ponte lo spazio libero e di quindici metri"));
        QCOMPARE(tracker.position(), 9);
        QVERIFY(tracker.follow("la costruzione e terminato nel duemilaundici", true));
        QCOMPARE(tracker.position(), tracker.count() - 1);
    }
    void recognizerPhrasesFollowTheScript() {
        const QString script = "# Benvenuti\nBuongiorno a tutti. [sorridi] L'incidente è avvenuto nel 2023.\n"
                               "Sono passati 20 anni";
        QCOMPARE(SpokenWords::recognizerPhrases(script, "it"),
                 QStringList({"buongiorno a tutti", "l'incidente è avvenuto nel duemilaventitré|duemilaventitre",
                              "sono passati venti anni"}));
        // Other languages keep the digits, which their recognizers do not know: Vosk leaves them out.
        QCOMPARE(SpokenWords::recognizerPhrases("In 2023 we grew", "en"), QStringList({"in 2023 we grew"}));
    }
    void followsPartialAndFinalWithoutDuplicateAdvance() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre quattro cinque sei");
        QVERIFY(tracker.follow("uno due"));
        QCOMPARE(tracker.position(), 1);
        QVERIFY(!tracker.follow("uno due"));
        QVERIFY(!tracker.follow("uno due", true));
        QVERIFY(tracker.follow("tre quattro"));
        QCOMPARE(tracker.position(), 3);
    }
    void resumesAfterPauseAndRejectsUnrelatedSpeech() {
        ScriptTracker tracker;
        tracker.setScript("Buongiorno a tutti oggi parliamo del nuovo progetto");
        QVERIFY(tracker.follow("buongiorno a tutti", true));
        QVERIFY(!tracker.follow("", true));
        QVERIFY(!tracker.follow("il gatto mangia il pesce", true));
        QCOMPARE(tracker.position(), 2);
        QVERIFY(tracker.follow("oggi parliamo del nuovo progetto", true));
        QCOMPARE(tracker.position(), 7);
    }
    void repeatsAndSkipsWithinBoundedWindow() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre quattro cinque sei sette otto nove dieci");
        tracker.follow("uno due tre quattro cinque", true);
        QVERIFY(tracker.follow("tre quattro", true));
        QCOMPARE(tracker.position(), 3);
        QVERIFY(tracker.follow("otto nove dieci", true));
        QCOMPARE(tracker.position(), 9);
    }
    void toleratesOneSubstitution() {
        ScriptTracker tracker;
        tracker.setScript("oggi presentiamo il nuovo progetto");
        QVERIFY(tracker.follow("oggi presentiamo un nuovo progetto", true));
        QCOMPARE(tracker.position(), 4);
    }
    void preservesAnchorWhenEditingBeforeIt() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre quattro");
        tracker.seek(2);
        tracker.setScript("introduzione uno due tre quattro");
        QCOMPARE(tracker.position(), 3);
        QCOMPARE(tracker.character(), 21);
        tracker.setScript("uno due tre quattro");
        QCOMPARE(tracker.position(), 2);
        tracker.setScript("");
        QCOMPARE(tracker.position(), -1);
    }
    void rejectsDistantFalseResynchronization() {
        ScriptTracker tracker;
        tracker.setScript(QString("parola ").repeated(120) + "obiettivo chiaro");
        QVERIFY(!tracker.follow("obiettivo chiaro", true));
        QCOMPARE(tracker.position(), -1);
    }
    void cuesAndHeadingsAreShownButNotSpoken() {
        const QString script = "# Opening\nHello [smile] everyone\n[look at camera]\nThanks [unclosed\n## Q&A ##";
        const auto words = ScriptTracker::tokenize(script);
        QStringList spoken;
        for (const auto &word : words)
            spoken << word.normalized;
        QCOMPARE(spoken, QStringList({"hello", "everyone", "thanks", "unclosed"}));
        QCOMPARE(ScriptTracker::cues(script).size(), 2);
        const auto sections = ScriptTracker::sections(script);
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections[0].title, QString("Opening"));
        QCOMPARE(sections[1].title, QString("Q&A"));
        QCOMPARE(sections[1].start, int(script.indexOf("## Q&A")));
        QVERIFY(ScriptTracker::headings("#hashtag is text").isEmpty());

        // Section jumps advance one section at a time (a jump leaves the position just before
        // the section's first word) and go back to the previous section.
        ScriptTracker sectioned;
        sectioned.setScript("# One\nalpha beta\n# Two\ngamma delta\n# Three\nepsilon");
        const auto jump = [&sectioned](int direction) {
            if (const int target = sectioned.sectionTarget(direction); target >= 0)
                sectioned.seekCharacter(target);
            return sectioned.position();
        };
        QCOMPARE(QList<int>({jump(1), jump(1), jump(1)}), QList<int>({1, 3, 3}));
        QCOMPARE(QList<int>({jump(-1), jump(-1), jump(-1)}), QList<int>({1, -1, -1}));
        sectioned.seek(2);
        QCOMPARE(jump(-1), -1);
        ScriptTracker plain;
        plain.setScript("no headings here");
        QCOMPARE(plain.sectionTarget(1), -1);

        // YAML front matter: shown, never spoken, never a section; its title names the script.
        const QString withMeta =
            "---\ntitle: \"Launch day\"\n# a YAML comment\nspeaker: [me]\n---\n# Opening\nHello everyone";
        QCOMPARE(ScriptTracker::frontMatter(withMeta).length, int(withMeta.indexOf("\n# Opening")));
        QStringList metaWords;
        for (const auto &word : ScriptTracker::tokenize(withMeta))
            metaWords << word.normalized;
        QCOMPARE(metaWords, QStringList({"hello", "everyone"}));
        QCOMPARE(ScriptTracker::sections(withMeta).size(), 1);
        QCOMPARE(ScriptTracker::title(withMeta), QString("Launch day"));
        QCOMPARE(ScriptTracker::title("---\nauthor: me\n...\n\n# Q&A\nText"), QString("Q&A"));
        QCOMPARE(ScriptTracker::frontMatter("---\nnot closed\nHello").length, 0);
        QCOMPARE(ScriptTracker::frontMatter("Intro\n---\nx\n---").length, 0);
    }
    void voiceSkipsCuesAndRecognizerTags() {
        ScriptTracker tracker;
        tracker.setScript("Welcome everyone [pause] today we launch");
        QVERIFY(tracker.follow("welcome everyone today", true));
        QCOMPARE(tracker.position(), 2);
        QVERIFY(!tracker.follow("[unk]", true)); // Vosk marks unknown words like this.
    }
    void recoversAfterLongSkipOrBacktrack() {
        QStringList filler;
        for (int i = 0; i < 150; ++i)
            filler << QString("parola%1").arg(i);
        ScriptTracker tracker;
        tracker.setScript("inizio del discorso " + filler.join(' ') + " la conclusione arriva adesso finalmente");
        QVERIFY(tracker.follow("inizio del discorso", true));
        QCOMPARE(tracker.position(), 2);
        // Far beyond the local window: four distinctive words in one place.
        QVERIFY(tracker.follow("la conclusione arriva adesso", true));
        QCOMPARE(tracker.position(), tracker.count() - 2);
        // And back again.
        QVERIFY(tracker.follow("inizio del discorso parola0", true));
        QCOMPARE(tracker.position(), 3);
        // After a recognizer that learnt the script, five: it hears four of its words in a row in
        // an off-script sentence.
        tracker.setGuided(true);
        QVERIFY(!tracker.follow("la conclusione arriva adesso", true));
        QVERIFY(tracker.follow("la conclusione arriva adesso finalmente", true));
        QCOMPARE(tracker.position(), tracker.count() - 1);
        QVERIFY(tracker.follow("inizio del discorso parola0 parola1", true));
        QCOMPARE(tracker.position(), 4);
    }
    void skipsAheadOnFewerWordsUnlessTheRecognizerLearntTheScript() {
        const QString script = "uno due tre quattro cinque sei sette otto nove dieci undici dodici tredici quattordici";
        ScriptTracker open;
        open.setScript(script);
        QVERIFY(open.follow("uno due", true));
        QVERIFY(open.follow("nove dieci", true));
        QCOMPARE(open.position(), 9);
        ScriptTracker guided;
        guided.setGuided(true);
        guided.setScript(script);
        QVERIFY(guided.follow("uno due", true));
        QVERIFY(!guided.follow("nove dieci undici", true)); // What an off-script sentence gives.
        QVERIFY(guided.follow("nove dieci undici dodici", true));
        QCOMPARE(guided.position(), 11);
        // Reading a passage again, or on from the position, takes two either way.
        QVERIFY(guided.follow("tre quattro", true));
        QCOMPARE(guided.position(), 3);
    }
    void ambiguousLongJumpsAreRejected() {
        const QString chorus = "we will build it together ";
        ScriptTracker tracker;
        QStringList filler;
        for (int i = 0; i < 120; ++i)
            filler << QString("x%1").arg(i);
        tracker.setScript(chorus + filler.join(' ') + " " + chorus + filler.join(' ') + " " + chorus);
        QVERIFY(!tracker.follow("we will build it together", true) || tracker.position() < 10);
    }
    void reportsLostAfterRepeatedOffScriptSpeech() {
        ScriptTracker tracker;
        tracker.setScript("today we present the new project plan");
        QVERIFY(tracker.follow("today we present", true));
        QVERIFY(!tracker.lost());
        tracker.follow("by the way the weather is lovely", true);
        QVERIFY(!tracker.lost());
        tracker.follow("and my cat says hello to everyone", true);
        QVERIFY(tracker.lost());
        QVERIFY(tracker.follow("the new project", true));
        QVERIFY(!tracker.lost());
    }
    void seekCharacterPicksTheWordAtOrBefore() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre");
        tracker.seekCharacter(5);
        QCOMPARE(tracker.position(), 1);
        tracker.seekCharacter(0);
        QCOMPARE(tracker.position(), 0);
        tracker.seekCharacter(-1);
        QCOMPARE(tracker.position(), -1);
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
