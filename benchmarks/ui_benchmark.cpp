// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Production widgets on an isolated profile. Rendering ends at a pixmap, not the display.
#include "accessibility.h"
#include "backdrop.h"
#include "window.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "process_stats.h"

double percentile(QList<double> values, double fraction) {
    if (values.isEmpty())
        return -1;
    std::sort(values.begin(), values.end());
    return values[std::max<qsizetype>(0, qsizetype(std::ceil(fraction * values.size())) - 1)];
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3 || args.size() > 4 || !QStringList{"editor", "reader", "steady", "colors", "interaction"}.contains(args[1]))
        return 2;
    bool valid = false;
    const int seconds = args[2].toInt(&valid);
    if (!valid || seconds < 1 || seconds > 300)
        return 2;
    const QString mode = args[1];
    QTemporaryDir profile;
    if (!profile.isValid())
        return 2;
    app.setOrganizationName("kalipso-benchmark");
    app.setApplicationName("isolated-ui-benchmark");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings().setValue("reading/countdown", 0);
    QSettings().setValue("appearance/theme", "system");
    QSettings().setValue("reading/theme", mode == "colors" ? "auto" : "dark");
    Accessibility::useInterfaceStyle();
    Accessibility::useInterfaceFont("en");
    MainWindow window(profile.path() + "/scripts");
    window.setDemoScript();
    window.show();
    if (mode == "reader" || mode == "steady" || mode == "colors")
        window.placeUnderCamera();
    else
        window.expand();
    if (mode == "steady")
        window.startSteadyScrolling();
    auto *text = window.prompter()->text;
    if (mode == "interaction") {
        QString script;
        for (int i = 0; i < 100; ++i)
            script += "We read this short sentence and follow every word carefully.\n";
        text->setPlainText(script);
    }
    int colorUpdates = 0;
    QString colorFailure;
    auto *backdrop = window.prompter()->findChild<Backdrop *>();
    if (!backdrop)
        return 1;
    QObject::connect(backdrop, &Backdrop::colorChanged, &app, [&](const QColor &) { ++colorUpdates; });
    QObject::connect(backdrop, &Backdrop::unavailable, &app, [&](const QString &reason) { colorFailure = reason; });
    // Window placement and entrance animation settle before measuring.
    QTest::qWait(2200);
    if (args.size() == 4 && !window.grab().save(args[3]))
        return 1;
    QList<double> latency, rss;
    QElapsedTimer elapsed;
    const double cpuBefore = processCpuSeconds();
    const int colorsBefore = colorUpdates;
    const QString positionBefore = window.positionText();
    elapsed.start();
    if (mode == "interaction") {
        if (text->isReadOnly())
            return 1;
        text->setFocus();
        auto cursor = text->textCursor();
        cursor.movePosition(QTextCursor::Start);
        text->setTextCursor(cursor);
        for (int i = 0; i < 65; ++i) {
            QElapsedTimer timer;
            timer.start();
            const int before = text->document()->characterCount();
            QTest::keyClick(text, Qt::Key_X);
            QCoreApplication::processEvents();
            const auto rendered = window.grab();
            const double ms = timer.nsecsElapsed() / 1e6;
            if (rendered.isNull() || text->document()->characterCount() != before + 1)
                return 1;
            if (i >= 5)
                latency.append(ms);
            QTest::keyClick(text, Qt::Key_Backspace);
            QTest::qWait(20);
        }
    } else {
        QEventLoop loop;
        QTimer sample;
        sample.setInterval(100);
        QObject::connect(&sample, &QTimer::timeout, &loop, [&] {
            rss.append(currentRssKiB());
        });
        sample.start();
        QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    const double wallSeconds = elapsed.nsecsElapsed() / 1e9;
    const double cpuSeconds = processCpuSeconds() - cpuBefore;
    const bool colorWorked = mode != "colors" || (colorUpdates - colorsBefore >= 2 && colorFailure.isEmpty());
    const bool scrolled = mode != "steady" || window.positionText() != positionBefore;
    QJsonArray samples;
    for (double value : latency)
        samples.append(value);
    QJsonObject report{{"schema", 1}, {"mode", mode}, {"qt", qVersion()},
                       {"platform", QGuiApplication::platformName()}, {"device_pixel_ratio", window.devicePixelRatioF()},
                       {"width", window.width()}, {"height", window.height()},
                       {"warmup_seconds", 2.2}, {"elapsed_seconds", wallSeconds},
                       {"cpu_seconds", cpuSeconds}, {"cpu_percent_one_core", cpuSeconds / wallSeconds * 100},
                       {"rss_samples", rss.size()}, {"median_rss_kib", percentile(rss, .5)},
                       {"peak_rss_kib", peakRssKiB()}, {"interaction_samples", samples},
                       {"interaction_p50_ms", percentile(latency, .5)}, {"interaction_p95_ms", percentile(latency, .95)},
                       {"color_updates", colorUpdates - colorsBefore}, {"color_error", colorFailure},
                       {"position_before", positionBefore}, {"position_after", window.positionText()},
                       {"passed", colorWorked && scrolled && cpuBefore >= 0 && peakRssKiB() > 0 &&
                                      (mode == "interaction" || percentile(rss, .5) > 0)}};
    const auto json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return report.value("passed").toBool() ? 0 : 1;
}
