// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "interface_languages.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTranslator>

namespace InterfaceLanguages {
inline const Language *find(QString code) {
    code = code.section('.', 0, 0).section('@', 0, 0).replace('_', '-');
    for (const auto &language : all)
        if (code.compare(QLatin1String(language.code), Qt::CaseInsensitive) == 0)
            return &language;
    const auto base = code.section('-', 0, 0);
    if (base == "zh")
        return find("zh-Hans");
    for (const auto &language : all)
        if (base == QLatin1String(language.code))
            return &language;
    return nullptr;
}

inline Language select(const QString &saved = {}) {
    if (const auto *language = find(saved))
        return *language;
    QStringList preferences;
    // LANGUAGE also represents UI languages that Qt does not yet name (pcm, arz).
    preferences += qEnvironmentVariable("LANGUAGE").split(':', Qt::SkipEmptyParts);
    for (const char *key : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const auto value = qEnvironmentVariable(key);
        if (!value.isEmpty()) {
            preferences += value;
            break;
        }
    }
    preferences += QLocale::system().uiLanguages();
    for (const auto &code : preferences)
        if (const auto *language = find(code))
            return *language;
    return *find("en");
}

// Preserve regional conventions (e.g. en_GB, de_CH, pt_PT) when the UI language matches.
inline QLocale formattingLocale(const Language &language, const QLocale &system = QLocale::system()) {
    const auto *systemLanguage = find(system.bcp47Name());
    return systemLanguage && QString::fromLatin1(systemLanguage->code) == QLatin1String(language.code)
               ? system
               : QLocale(language.locale);
}

// Qt's integer plural tables differ from CLDR for e.g. Irish and Maltese.
// Keep Qt's context lookup while using the same CLDR forms as gettext and the site.
class Translator : public QTranslator {
public:
    bool loadLanguage(const Language &language, const QString &catalog, const QString &directory = ":/i18n") {
        code_ = QString::fromLatin1(language.code);
        QFile forms(":/i18n/plurals/" + code_ + ".json");
        plurals_ = forms.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(forms.readAll()).object() : QJsonObject{};
        auto suffix = code_;
        suffix.replace('-', '_');
        return load(catalog + '_' + suffix, directory);
    }
    QString translate(const char *context, const char *source, const char *disambiguation = nullptr,
                      int n = -1) const override {
        if (n >= 0) {
            const auto forms =
                plurals_.value(QString::fromUtf8(context) + QChar(4) + QString::fromUtf8(source)).toArray();
            const int index = pluralIndex(code_, n);
            if (index >= 0 && index < forms.size())
                return forms[index].toString().replace("%n", "%Ln");
        }
        return QTranslator::translate(context, source, disambiguation, n);
    }

private:
    QString code_;
    QJsonObject plurals_;
};
} // namespace InterfaceLanguages
