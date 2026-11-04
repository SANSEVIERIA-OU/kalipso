// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
#pragma once
#include "design_tokens.h"
#include "icons.h"
#include "motion.h"
#include <QAccessibleWidget>
#include <QApplication>
#include <QButtonGroup>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QToolButton>
#include <cmath>
#include <functional>

namespace Accessibility {
inline double luminance(const QColor &color) {
    const auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}
inline double contrast(const QColor &a, const QColor &b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
inline QPalette palette(const Design::Palette &p) {
    QPalette result;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        for (auto role : {QPalette::Window, QPalette::Button})
            result.setColor(group, role, QColor(p.background));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
            result.setColor(group, role, QColor(p.text));
        result.setColor(group, QPalette::Base, QColor(p.surface));
        result.setColor(group, QPalette::AlternateBase, QColor(p.hover));
        result.setColor(group, QPalette::ToolTipBase, QColor(p.background));
        result.setColor(group, QPalette::PlaceholderText, QColor(p.muted));
        result.setColor(group, QPalette::Highlight, QColor(p.selection));
        result.setColor(group, QPalette::HighlightedText, QColor(p.selectionText));
        result.setColor(group, QPalette::Link, QColor(p.accent));
        result.setColor(group, QPalette::LinkVisited, QColor(p.accent));
        result.setColor(group, QPalette::Mid, QColor(p.border));
        result.setColor(group, QPalette::Dark, QColor(p.border));
    }
    return result;
}
// The interface and default reading font: Atkinson Hyperlegible Next, embedded (OFL), at the system's size.
// Bundled Noto fallbacks cover the interface's writing systems without host font dependencies.
inline void useInterfaceFont(const QString &language = {}) {
    static bool loaded = false;
    if (loaded || !qobject_cast<QApplication *>(QCoreApplication::instance()))
        return;
    loaded = true;
    for (const auto &file : QDir(":/fonts").entryList({"Noto*.ttf", "Noto*.otf"}, QDir::Files))
        QFontDatabase::addApplicationFont(":/fonts/" + file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    for (auto script : {QChar::Script_Arabic, QChar::Script_Devanagari, QChar::Script_Bengali, QChar::Script_Telugu,
                        QChar::Script_Greek, QChar::Script_Cyrillic}) {
        const QString family = script == QChar::Script_Arabic       ? "Noto Sans Arabic"
                               : script == QChar::Script_Devanagari ? "Noto Sans Devanagari"
                               : script == QChar::Script_Bengali    ? "Noto Sans Bengali"
                               : script == QChar::Script_Telugu     ? "Noto Sans Telugu"
                                                                    : "Noto Sans";
        QFontDatabase::addApplicationFallbackFontFamily(script, family);
    }
    for (auto script : {QChar::Script_Han, QChar::Script_Hiragana, QChar::Script_Katakana})
        QFontDatabase::addApplicationFallbackFontFamily(script,
                                                        language == "ja" ? "Noto Sans CJK JP" : "Noto Sans CJK SC");
#endif
    int id = -1;
    for (const char *style : {"Regular", "Italic", "SemiBold", "Bold", "BoldItalic"})
        id = std::max(id, QFontDatabase::addApplicationFont(
                              QStringLiteral(":/fonts/AtkinsonHyperlegibleNext-%1.ttf").arg(QLatin1String(style))));
    if (id < 0)
        return; // Keep the system font if the files cannot be loaded.
    QFont font = QApplication::font();
    font.setFamilies({QStringLiteral("Atkinson Hyperlegible Next"), QStringLiteral("Noto Sans"),
                      QStringLiteral("Noto Sans Arabic"), QStringLiteral("Noto Sans Devanagari"),
                      QStringLiteral("Noto Sans Bengali"), QStringLiteral("Noto Sans Telugu"),
                      language == "ja" ? QStringLiteral("Noto Sans CJK JP") : QStringLiteral("Noto Sans CJK SC")});
    QApplication::setFont(font);
}
inline bool &nativeLook() {
    static bool value = false;
    return value;
}
// True when Kalipso's windows use the desktop's own style and colours (Breeze on Plasma), like the
// other apps on that desktop. The reader keeps its own look either way.
inline bool native() { return nativeLook(); }

// On Linux desktops with their own style (Breeze on Plasma, or the one the person chose), Kalipso
// uses it with the desktop's colours, light or dark, and accent. Elsewhere, and where Qt only has its
// Fusion fallback, Kalipso draws its controls from the design tokens with style sheets on Fusion.
// KALIPSO_DRAWN_STYLE=1 forces the drawn look. Safe to call more than once.
inline void useInterfaceStyle() {
    static bool decided = false;
    if (decided || !qobject_cast<QApplication *>(QCoreApplication::instance()))
        return;
    decided = true;
    const auto name = QApplication::style()->name();
    const bool fallback = name.compare(QLatin1String("fusion"), Qt::CaseInsensitive) == 0 ||
                          name.compare(QLatin1String("windows"), Qt::CaseInsensitive) == 0;
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (!fallback && !qEnvironmentVariableIntValue("KALIPSO_DRAWN_STYLE")) {
        nativeLook() = true;
        return;
    }
#endif
    if (name.compare(QLatin1String("fusion"), Qt::CaseInsensitive) == 0)
        return;
    if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);
}
inline QString themeName(const Design::Palette &p) {
    return QString(p.background) == Design::light.background ? QStringLiteral("light") : QStringLiteral("dark");
}

// One visual language for every window. Borders identify controls (>= 3:1);
// dividers are decorative. Focus is a 2px ring in the focus colour.
inline QString style(const Design::Palette &p) {
    using namespace Design::Metric;
    QString css = QStringLiteral(R"(
QWidget { color: @text; }
QMainWindow, QDialog, QWidget#readingWindow { background: @background; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QLabel { background: transparent; }
QLabel[role="title"] { font-weight: 600; }
QLabel[role="muted"] { color: @muted; }
QWidget#appBar, QWidget#readerBar, QWidget#readerControls { background: @background; }
QWidget#appBar { border-bottom: 1px solid @divider; }
QWidget#readerControls { border-top: 1px solid @divider; }

QPushButton, QToolButton {
    background: @surface; color: @text; border: 1px solid @border; border-radius: @radiuspx;
    padding: 7px 14px; min-height: 20px;
}
QPushButton:hover, QToolButton:hover { background: @hover; }
QPushButton:pressed, QToolButton:pressed { background: @hover; border-color: @text; }
QPushButton:checked { background: @hover; border: 2px solid @accent; padding: 6px 13px; font-weight: 600; }
QPushButton:disabled, QToolButton:disabled { color: @muted; border-style: dashed; }
QPushButton[quiet="true"], QToolButton[quiet="true"] { background: transparent; border-color: transparent; }
QPushButton[quiet="true"]:hover, QToolButton[quiet="true"]:hover { background: @hover; }
QPushButton[quiet="true"]:checked { background: @hover; border: 1px solid @divider; padding: 7px 14px; font-weight: normal; }
QPushButton#primaryAction, QPushButton[primary="true"] {
    background: @action; color: @actionText; border: 1px solid @action; font-weight: 600;
}
QPushButton#primaryAction:hover, QPushButton[primary="true"]:hover { background: @actionHover; border-color: @actionHover; }
QToolButton::menu-indicator { image: url(:/icons/generated/chevron-down-@theme.svg); width: 12px; height: 12px;
    subcontrol-position: right center; right: 8px; }
QToolButton[popupMode="2"] { padding-right: 28px; }

QComboBox, QLineEdit, QSpinBox {
    background: @surface; color: @text; border: 1px solid @border; border-radius: @radiuspx;
    padding: 6px 10px; min-height: 22px; selection-background-color: @selection; selection-color: @selectionText;
}
/* The list opens under the field. Fusion's list over the field measured its rows before this style
   sheet and cut the last one off. */
QComboBox { padding-right: 32px; combobox-popup: 0; }
QComboBox:hover, QSpinBox:hover { background: @hover; }
QComboBox::drop-down { border: none; width: 28px; subcontrol-origin: padding; subcontrol-position: center right; }
QComboBox::down-arrow { image: url(:/icons/generated/chevron-down-@theme.svg); width: 14px; height: 14px; }
QComboBox QAbstractItemView { background: @surface; color: @text; border: 1px solid @border; padding: 0;
    selection-background-color: @selection; selection-color: @selectionText; outline: none; }
QComboBox QAbstractItemView::item { background: @surface; color: @text; padding: 6px 10px; border: none; }
QComboBox QAbstractItemView::item:selected { background: @selection; color: @selectionText; }
QSpinBox { padding-right: 30px; }
QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 26px; border: none;
    border-left: 1px solid @divider; background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: @radiuspx; }
QSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: @radiuspx; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; }
QSpinBox::up-arrow { image: url(:/icons/generated/chevron-up-@theme.svg); width: 12px; height: 12px; }
QSpinBox::down-arrow { image: url(:/icons/generated/chevron-down-@theme.svg); width: 12px; height: 12px; }

QPlainTextEdit, QTextEdit { background: @surface; color: @text; border: none;
    selection-background-color: @selection; selection-color: @selectionText; }
QPlainTextEdit#scriptEditor { border-top: 1px solid @divider; border-bottom: 1px solid @divider; }
QTextEdit#readingScript { background: @background; border: none; }
QWidget#scriptDrawer { background: @surface; border-right: 1px solid @divider; }
QListWidget#scriptList { background: transparent; border: none; }
/* Settings' pages, beside them: like the scripts panel, the current page as a checked quiet button. */
QListWidget#settingsPages { background: @surface; border: none; padding: 8px 6px; outline: none; }
QListWidget#settingsPages::item { color: @text; padding: 0 10px; border: 1px solid transparent; border-radius: 6px; }
QListWidget#settingsPages::item:hover { background: @hover; }
QListWidget#settingsPages::item:selected { background: @hover; color: @text; border: 1px solid @divider; }
QListWidget#settingsPages::item:selected:focus { border: 2px solid @focus; }
QFrame#settingsDivider { color: @divider; } /* Decorative, as the scripts panel's edge. */

QPushButton:focus, QToolButton:focus, QComboBox:focus, QLineEdit:focus, QSpinBox:focus {
    border: 2px solid @focus; padding: 6px 13px;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus { padding: 5px 9px; }
QComboBox:focus { padding-right: 31px; }
QSpinBox:focus { padding-right: 29px; }
QPlainTextEdit#scriptEditor:focus { border-top: 2px solid @focus; border-bottom: 2px solid @focus; }

QCheckBox { spacing: 10px; min-height: 24px; }
QCheckBox::indicator { width: 20px; height: 20px; border: 1px solid @border; border-radius: 5px; background: @surface; }
QCheckBox::indicator:hover { background: @hover; }
QCheckBox::indicator:checked { background: @action; border-color: @action;
    image: url(:/icons/generated/check-@theme.svg); }
QCheckBox:focus { outline: none; } QCheckBox::indicator:focus { border: 2px solid @focus; }

QProgressBar { background: @divider; border: none; max-height: 3px; min-height: 3px; }
QProgressBar::chunk { background: @accent; }
QProgressBar#microphoneLevel { max-height: 8px; min-height: 8px; border-radius: 4px; }
QProgressBar#microphoneLevel::chunk { border-radius: 4px; }
QStatusBar { background: @background; border-top: 1px solid @divider; }
QStatusBar::item { border: none; }
QMenu { background: @surface; color: @text; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 7px 18px; border-radius: 6px; }
QMenu::item:selected { background: @hover; color: @text; }
QToolTip { background: @text; color: @background; border: none; padding: 6px 8px; }
QTabBar::tab { padding: 8px 14px; border: none; border-bottom: 2px solid transparent; background: transparent; color: @muted; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; font-weight: 600; }
QTabBar::tab:focus { border: 2px solid @focus; }

QScrollBar:vertical { background: transparent; width: 16px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 16px; margin: 0; }
QScrollBar::handle { background: @border; border-radius: 4px; margin: 4px; min-height: 32px; min-width: 32px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");
    const QList<QPair<QString, QString>> values{{"@actionHover", p.actionHover},
                                                {"@actionText", p.actionText},
                                                {"@selectionText", p.selectionText},
                                                {"@selection", p.selection},
                                                {"@background", p.background},
                                                {"@text", p.text},
                                                {"@muted", p.muted},
                                                {"@surface", p.surface},
                                                {"@hover", p.hover},
                                                {"@border", p.border},
                                                {"@focus", p.focus},
                                                {"@accent", p.accent},
                                                {"@action", p.action},
                                                {"@divider", p.divider},
                                                {"@theme", themeName(p)},
                                                {"@radius", QString::number(radius)}};
    for (const auto &[name, value] : values)
        css.replace(name, value);
    return css;
}
inline void apply(QWidget *widget, const Design::Palette &p) {
    widget->setPalette(palette(p));
    widget->setAutoFillBackground(true);
    widget->setStyleSheet(style(p));
}
// Secondary text: the text colour moved towards the background while it keeps 4.5:1 on both the
// window and the fields.
inline QColor muted(const QPalette &p) {
    const QColor text = p.color(QPalette::WindowText), back = p.color(QPalette::Window);
    for (double t = 0.45; t > 0.01; t -= 0.05) {
        const QColor mix = QColor::fromRgbF(float(text.redF() * (1 - t) + back.redF() * t),
                                            float(text.greenF() * (1 - t) + back.greenF() * t),
                                            float(text.blueF() * (1 - t) + back.blueF() * t));
        if (contrast(mix, back) >= 4.5 && contrast(mix, p.color(QPalette::Base)) >= 4.5)
            return mix;
    }
    return text;
}
// The desktop's look (see native()): its style draws every control in its colours, or in `colors`
// when the person chose light or dark in Kalipso. Kalipso only marks what the style cannot know:
// quiet buttons are flat, the one primary action takes the accent colour, secondary text is muted.
// No style sheet: under one, Qt fixes each control's colours when it first polishes it, and they
// would stay light when the desktop turns dark. Call again when the desktop's colours change.
// A view marked with the property "ownLook" (the reader) keeps its own colours and style and is
// left alone.
inline void applyNative(QWidget *widget, const QPalette &colors) {
    widget->setStyleSheet(QString());
    widget->setPalette(colors);
    const QPalette used = widget->palette();
    const QColor secondary = muted(used);
    const auto own = [widget](QWidget *child) {
        for (auto *ancestor = child; ancestor && ancestor != widget; ancestor = ancestor->parentWidget())
            if (ancestor->property("ownLook").toBool())
                return true;
        return false;
    };
    for (auto *label : widget->findChildren<QLabel *>()) {
        if (own(label))
            continue;
        const auto role = label->property("role").toString();
        if (role == QLatin1String("muted")) {
            QPalette own; // Only the text colour is Kalipso's; the rest follows the window.
            own.setColor(QPalette::WindowText, secondary);
            label->setPalette(own);
        } else if (role == QLatin1String("title")) {
            auto font = label->font();
            font.setWeight(QFont::DemiBold);
            label->setFont(font);
        }
    }
    for (auto *button : widget->findChildren<QPushButton *>()) {
        if (own(button))
            continue;
        if (button->property("quiet").toBool())
            button->setFlat(true);
        if (button->objectName() == QLatin1String("primaryAction") || button->property("primary").toBool()) {
            // The accent colour, deepened (or lightened under a dark label) until its label keeps
            // 4.5:1: Breeze's light blue under white text does not.
            QColor fill = used.color(QPalette::Active, QPalette::Highlight);
            const QColor label = used.color(QPalette::Active, QPalette::HighlightedText);
            for (int step = 0; step < 24 && contrast(label, fill) < 4.5; ++step)
                fill = label.lightnessF() > 0.5 ? fill.darker(108) : fill.lighter(108);
            QPalette accent;
            for (auto group : {QPalette::Active, QPalette::Inactive}) {
                accent.setColor(group, QPalette::Button, fill);
                accent.setColor(group, QPalette::ButtonText, label);
            }
            button->setPalette(accent);
        }
    }
    for (auto *button : widget->findChildren<QToolButton *>())
        if (button->property("quiet").toBool() && !own(button))
            button->setAutoRaise(true);
}
// Whether the desktop asks for dark colours.
inline bool systemIsDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Unknown)
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
    return luminance(QGuiApplication::palette().color(QPalette::Window)) < 0.2;
}
} // namespace Accessibility

class StatusLabel : public QLabel {
public:
    explicit StatusLabel(const QString &value, QWidget *parent = nullptr) : QLabel(value, parent) {
        setWordWrap(true);
        setTextFormat(Qt::PlainText);
        setAccessibleName(value);
        QAccessible::registerAccessibleInterface(new QAccessibleWidget(this, QAccessible::StatusBar));
    }
    QSize sizeHint() const override {
        const int width = fontMetrics().horizontalAdvance(text()) + 4;
        return {width, heightForWidth(width)};
    }
    void setText(const QString &value) {
        if (text() == value)
            return;
        QLabel::setText(value);
        setAccessibleName(value);
        if (!isVisible() || value.isEmpty())
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QAccessibleAnnouncementEvent event(this, value);
#else
        QAccessibleEvent event(this, QAccessible::Alert);
#endif
        QAccessible::updateAccessibility(&event);
    }
};

// A push button whose label wraps instead of being cut off at large text sizes
// or narrow widths, with an optional icon before the label. The icon moves by its
// meaning on hover and keyboard focus ("motion" property); a press shrinks the button.
class AccessibleButton : public QPushButton {
public:
    explicit AccessibleButton(const QString &text, QWidget *parent = nullptr)
        : QPushButton(text, parent), motion_(new IconMotion(this)) {
        auto policy = sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        setIconSize({18, 18});
    }
    int heightForWidth(int width) const override {
        const int textHeight = text().isEmpty() ? 0
                                                : fontMetrics()
                                                      .boundingRect(QRect(0, 0, textWidthFor(width), 10000),
                                                                    Qt::TextWordWrap | Qt::AlignCenter, text())
                                                      .height();
        return std::max({Design::Metric::control, textHeight + 18, iconSize().height() + 18});
    }
    QSize sizeHint() const override {
        // Slack for bold labels: style sheets set the weight after the first measure.
        const int label = int(std::ceil(fontMetrics().horizontalAdvance(text()) * 1.08));
        const int width = std::min(320, label + kPadding * 2 + iconSpace());
        return {std::max(width, Design::Metric::control), heightForWidth(width)};
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::Polish || event->type() == QEvent::FontChange ||
            event->type() == QEvent::StyleChange)
            updateGeometry();
        return QPushButton::event(event);
    }
    QSize minimumSizeHint() const override { return {std::min(120, sizeHint().width()), Design::Metric::control}; }
    IconMotion *iconMotion() const { return motion_; }

protected:
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);
        if (isDown())
            Motion::pressed(painter, rect());
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = QIcon();
        painter.drawControl(QStyle::CE_PushButton, option);
        const QRect area = rect().adjusted(kPadding, 6, -kPadding, -6);
        const int textWidth =
            text().isEmpty()
                ? 0
                : std::min(textWidthFor(width()),
                           fontMetrics()
                               .boundingRect(QRect(0, 0, textWidthFor(width()), 10000), Qt::TextWordWrap, text())
                               .width());
        int x = area.center().x() - (iconSpace() + textWidth) / 2;
        if (!icon().isNull()) {
            const auto mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
            const QRect target(QPoint(x, area.center().y() - iconSize().height() / 2 + 1), iconSize());
            painter.save();
            painter.setTransform(motion_->transform(target), true);
            icon().paint(&painter, target, Qt::AlignCenter, mode);
            painter.restore();
            x += iconSpace();
        }
        if (textWidth > 0)
            style()->drawItemText(&painter, QRect(x, area.top(), textWidth + 2, area.height()),
                                  Qt::TextWordWrap | Qt::AlignCenter, option.palette, isEnabled(), text(),
                                  QPalette::ButtonText);
    }

private:
    static constexpr int kPadding = 14;
    int iconSpace() const { return icon().isNull() ? 0 : iconSize().width() + (text().isEmpty() ? 0 : 8); }
    int textWidthFor(int width) const { return std::max(24, width - kPadding * 2 - iconSpace()); }
    IconMotion *motion_;
};

// A drop-down button (sections, reading mode, more actions) drawn like AccessibleButton: the style
// paints the frame, the button paints its icon, label and arrow, which move down on hover.
class MenuButton : public QToolButton {
public:
    explicit MenuButton(QWidget *parent = nullptr) : QToolButton(parent), motion_(new IconMotion(this)) {
        setPopupMode(QToolButton::InstantPopup);
        setProperty("motion", "down");
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);
        if (isDown())
            Motion::pressed(painter, rect());
        QStyleOptionToolButton option;
        initStyleOption(&option);
        QStyleOptionToolButton frame = option;
        frame.text.clear();
        frame.icon = QIcon();
        frame.features &= ~(QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup);
        painter.drawComplexControl(QStyle::CC_ToolButton, frame);

        const QRect area = rect().adjusted(kPadding, 0, -kArrowSpace, 0);
        const QTransform moved = motion_->transform(QRectF(area));
        int x = area.left();
        const bool showIcon = !icon().isNull() && toolButtonStyle() != Qt::ToolButtonTextOnly;
        if (showIcon) {
            const QRect target(QPoint(x, area.center().y() - iconSize().height() / 2 + 1), iconSize());
            painter.save();
            painter.setTransform(moved, true);
            icon().paint(&painter, target, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
            painter.restore();
            x += iconSize().width() + 8;
        }
        if (!(showIcon && toolButtonStyle() == Qt::ToolButtonIconOnly)) {
            const QRect label(x, area.top(), std::max(0, area.right() - x + 1), area.height());
            style()->drawItemText(
                &painter, label, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextShowMnemonic, option.palette, isEnabled(),
                fontMetrics().elidedText(text(), Qt::ElideRight, label.width()), QPalette::ButtonText);
        }
        const QColor color =
            option.palette.color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
        if (color != arrowColor_) {
            arrow_ = Icons::icon("chevron-down", color);
            arrowColor_ = color;
        }
        painter.setTransform(moved, true);
        arrow_.paint(&painter, QRect(width() - 8 - 12, height() / 2 - 6, 12, 12));
    }

private:
    static constexpr int kPadding = 14, kArrowSpace = 28;
    IconMotion *motion_;
    QIcon arrow_;
    QColor arrowColor_;
};

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr) : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(8);
    }
    ~FlowLayout() override {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { items_.append(item); }
    // Adds a widget aligned to the right edge of its line; add these last.
    void addTrailing(QWidget *widget) {
        addWidget(widget);
        trailing_.append(items_.last());
    }
    int count() const override { return int(items_.size()); }
    QLayoutItem *itemAt(int i) const override { return items_.value(i); }
    QLayoutItem *takeAt(int i) override {
        if (i < 0 || i >= items_.size())
            return nullptr;
        trailing_.removeAll(items_.at(i));
        return items_.takeAt(i);
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override {
        QSize size;
        for (auto *item : items_)
            if (!item->isEmpty())
                size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    // Preferred: everything on one line. A parent layout narrower than that
    // asks heightForWidth and the items wrap.
    QSize sizeHint() const override {
        int width = 0, height = 0, visible = 0;
        for (auto *item : items_)
            if (!item->isEmpty()) {
                width += item->sizeHint().width();
                height = std::max(height, item->sizeHint().height());
                ++visible;
            }
        const auto margins = contentsMargins();
        return {width + std::max(0, visible - 1) * spacing() + margins.left() + margins.right(),
                height + margins.top() + margins.bottom()};
    }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    // Places items left to right, wrapping when a line is full. Items in a line
    // are centred vertically; trailing items move to the right edge.
    int arrange(const QRect &outer, bool apply) const {
        const auto margins = contentsMargins();
        const QRect rect = outer.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
        QList<QPair<QLayoutItem *, QRect>> placed;
        int x = rect.x(), y = rect.y(), line = 0;
        const auto finishLine = [&] {
            if (apply && !placed.isEmpty()) {
                const int free = rect.right() - placed.last().second.right();
                for (auto &[item, geometry] : placed) {
                    if (trailing_.contains(item))
                        geometry.translate(free, 0);
                    geometry.moveTop(y + (line - geometry.height()) / 2);
                    item->setGeometry(geometry);
                }
            }
            placed.clear();
        };
        for (auto *item : items_) {
            if (item->isEmpty())
                continue;
            auto size = item->sizeHint();
            size.setWidth(std::min(size.width(), rect.width()));
            if (item->hasHeightForWidth())
                size.setHeight(item->heightForWidth(size.width()));
            if (x > rect.x() && x + size.width() > rect.right() + 1) {
                finishLine();
                x = rect.x();
                y += line + spacing();
                line = 0;
            }
            placed.append({item, QRect(QPoint(x, y), size)});
            x += size.width() + spacing();
            line = std::max(line, size.height());
        }
        finishLine();
        return y - rect.y() + line + margins.top() + margins.bottom();
    }
    QList<QLayoutItem *> items_;
    QList<QLayoutItem *> trailing_;
};
