// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#include "interface_language.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QRawFont>
#include <QSet>
#include <algorithm>

QJsonObject object(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(path));
    return QJsonDocument::fromJson(file.readAll()).object();
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const QString root = KALIPSO_SOURCE_DIR;
    const auto structure = object(root + "/translations/structure.json");
    QList<QRawFont> fonts;
    for (const auto &name : QDir(":/fonts").entryList({"*.ttf", "*.otf"}, QDir::Files)) {
        QRawFont font(":/fonts/" + name, 16);
        if (!font.isValid())
            qFatal("Invalid bundled font: %s", qPrintable(name));
        fonts.append(font);
    }
    if (fonts.isEmpty())
        qFatal("Bundled fonts were not linked");
    int checks = 0;
    for (const auto &language : InterfaceLanguages::all) {
        const QString code = QString::fromLatin1(language.code);
        if (argc > 1 && code != QString::fromLocal8Bit(argv[1]))
            continue;
        QLocale::setDefault(QLocale(language.locale));
        const auto catalog = object(root + "/translations/catalogs/" + code + ".json");
        QString text = QString::fromUtf8(language.name);
        for (const auto &value : catalog) {
            if (value.isString())
                text += value.toString();
            else
                for (const auto &form : value.toObject())
                    text += form.toString();
        }
        const auto characters = text.toUcs4();
        const QSet<char32_t> unique(characters.begin(), characters.end());
        for (char32_t character : unique) {
            const auto category = QChar::category(character);
            if (category == QChar::Other_Control || category == QChar::Other_Format ||
                (character >= 0xfe00 && character <= 0xfe0f))
                continue;
            const bool covered = std::any_of(fonts.begin(), fonts.end(), [character](const QRawFont &font) {
                return font.supportsCharacter(character);
            });
            if (!covered)
                qFatal("%s: no bundled font for U+%04X", qPrintable(code), unsigned(character));
        }
        for (const QString surface : {QStringLiteral("app"), QStringLiteral("engine")}) {
#ifndef Q_OS_LINUX
            if (surface == "engine")
                continue;
#endif
            InterfaceLanguages::Translator translator;
            if (!translator.loadLanguage(language, surface == "app" ? "kalipso" : "kalipso-engine",
                                         QStringLiteral(KALIPSO_BINARY_DIR)))
                qFatal("Missing compiled catalog: %s/%s", qPrintable(code), qPrintable(surface));
            app.installTranslator(&translator);
            const auto plurals = object(":/i18n/plurals/" + code + ".json");
            for (const auto &value : structure[surface].toArray()) {
                const auto unit = value.toObject();
                const auto source = unit["source"].toString().toUtf8();
                const auto context = unit["context"].toString().toUtf8();
                for (int n :
                     unit["numerus"].toBool() ? QList<int>{0, 1, 2, 3, 5, 11, 21, 100, 101, 1000000} : QList<int>{-1}) {
                    QString expected;
                    if (n >= 0) {
                        const auto forms =
                            plurals[QString::fromUtf8(context) + QChar(4) + QString::fromUtf8(source)].toArray();
                        expected = forms[InterfaceLanguages::pluralIndex(code, n)].toString().replace(
                            "%n", QLocale().toString(n));
                    } else
                        expected = catalog[unit["key"].toString()].toString();
                    const auto actual =
                        QCoreApplication::translate(context.constData(), source.constData(), nullptr, n);
                    if (actual != expected)
                        qFatal("%s/%s/%s n=%d: expected %s, got %s", qPrintable(code), context.constData(),
                               source.constData(), n, qPrintable(expected), qPrintable(actual));
                    ++checks;
                }
            }
            app.removeTranslator(&translator);
        }
    }
    if (QString(InterfaceLanguages::select("arz").code) != "arz" || !InterfaceLanguages::select("ur_PK").rtl ||
        QString(InterfaceLanguages::select("zh_CN").code) != "zh-Hans")
        qFatal("Locale resolution failed");
    for (const auto *region : {"en_GB", "de_CH", "pt_PT"}) {
        const QLocale system(region);
        const auto language = InterfaceLanguages::select(system.bcp47Name());
        if (InterfaceLanguages::formattingLocale(language, system) != system)
            qFatal("Regional formatting lost for %s", region);
    }
    if (!checks)
        qFatal("No language was tested");
    qInfo() << checks << "compiled translation checks passed";
}
