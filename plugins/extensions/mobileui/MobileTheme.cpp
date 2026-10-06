/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileTheme.h"
#include "MobileConfig.h"

#include <kis_icon_utils.h>

#include <QAbstractScrollArea>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScroller>
#include <QScrollerProperties>
#include <QSvgRenderer>
#include <QWidget>
#include <QtMath>

namespace mobileui {

namespace {

Theme g_theme;
bool g_themeInitialized = false;

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

qreal luminance(const QColor &c)
{
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

QString css(const QColor &c)
{
    if (c.alpha() == 255) {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// Renders one of the fork's SVG icons in a solid color. The icon set uses
// "currentColor" for strokes and fills, which gets substituted here.
class TintedIconEngine final : public QIconEngine
{
public:
    TintedIconEngine(const QByteArray &svg, const QColor &color)
        : m_svg(svg)
        , m_color(color)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        Q_UNUSED(state);
        QColor color = m_color;
        if (mode == QIcon::Disabled) {
            color.setAlphaF(color.alphaF() * 0.38);
        }
        QByteArray data = m_svg;
        data.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
        QSvgRenderer renderer(data);
        if (renderer.isValid()) {
            painter->save();
            painter->setOpacity(painter->opacity() * color.alphaF());
            renderer.render(painter, QRectF(rect));
            painter->restore();
        }
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QIconEngine *clone() const override
    {
        return new TintedIconEngine(m_svg, m_color);
    }

private:
    QByteArray m_svg;
    QColor m_color;
};

QByteArray loadSvg(const QString &name)
{
    static QHash<QString, QByteArray> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return it.value();
    }
    QFile file(QStringLiteral(":/mobileui/icons/%1.svg").arg(name));
    QByteArray data;
    if (file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
    } else {
        qWarning("Krita Mobile UI: missing icon '%s'", qUtf8Printable(name));
    }
    cache.insert(name, data);
    return data;
}

} // namespace

const Theme &Theme::current()
{
    if (!g_themeInitialized) {
        refresh();
    }
    return g_theme;
}

void Theme::refresh()
{
    g_themeInitialized = true;
    const QPalette pal = QApplication::palette();
    const QColor window = pal.color(QPalette::Window);
    Theme &t = g_theme;
    t.dark = window.lightness() < 128;

    QColor highlight = pal.color(QPalette::Highlight);
    // Very desaturated highlights (some themes use gray) make a poor accent.
    if (highlight.hsvSaturationF() < 0.15) {
        highlight = t.dark ? QColor(0x5b, 0xa8, 0xff) : QColor(0x1f, 0x6f, 0xeb);
    }

    if (t.dark) {
        t.background = QColor(0x10, 0x11, 0x14);
        t.surface = QColor(0x1b, 0x1c, 0x21);
        t.surface2 = QColor(0x25, 0x27, 0x2d);
        t.surface3 = QColor(0x31, 0x33, 0x3a);
        t.outline = QColor(0x3a, 0x3c, 0x44);
        t.text = QColor(0xf1, 0xf2, 0xf5);
        t.textDim = QColor(0xa3, 0xa6, 0xb0);
        t.danger = QColor(0xff, 0x6b, 0x6b);
        t.shadow = QColor(0, 0, 0, 140);
    } else {
        t.background = QColor(0xec, 0xee, 0xf2);
        t.surface = QColor(0xff, 0xff, 0xff);
        t.surface2 = QColor(0xf2, 0xf3, 0xf6);
        t.surface3 = QColor(0xe3, 0xe5, 0xea);
        t.outline = QColor(0xd6, 0xd9, 0xe0);
        t.text = QColor(0x17, 0x18, 0x1c);
        t.textDim = QColor(0x5b, 0x5f, 0x6b);
        t.danger = QColor(0xd3, 0x2f, 0x2f);
        t.shadow = QColor(0, 0, 0, 60);
    }
    t.accent = highlight;
    t.accentText = luminance(highlight) > 0.55 ? QColor(0x10, 0x11, 0x14) : QColor(Qt::white);
    t.accentSoft = mix(t.surface, highlight, t.dark ? 0.28 : 0.18);
}

int dp(qreal value)
{
    return qRound(value * config::uiScale());
}

int fontPixelSize(TextRole role)
{
    switch (role) {
    case TextRole::Title:
        return dp(20);
    case TextRole::Subtitle:
        return dp(16);
    case TextRole::Body:
        return dp(15);
    case TextRole::Label:
        return dp(13);
    case TextRole::Caption:
        return dp(12);
    }
    return dp(15);
}

void applyFont(QWidget *widget, TextRole role, bool bold)
{
    QFont font = widget->font();
    font.setPixelSize(fontPixelSize(role));
    font.setBold(bold);
    widget->setFont(font);
}

QString chromeStyleSheet()
{
    const Theme &t = Theme::current();
    QString s;
    s += QStringLiteral(
             "QWidget#mobileTopBar, QWidget#mobileToolRail { background: %1; }"
             "QToolBar#mobileTopBarHolder, QToolBar#mobileToolRailHolder {"
             " background: %1; border: none; padding: 0px; spacing: 0px; }")
             .arg(css(t.surface));
    s += QStringLiteral(
             "QWidget[mobileChrome=\"true\"] { color: %1; }"
             "QLabel[mobileRole=\"dim\"] { color: %2; }"
             "QLabel[mobileRole=\"title\"] { color: %1; }")
             .arg(css(t.text), css(t.textDim));
    s += QStringLiteral(
             "QLineEdit[mobileChrome=\"true\"] {"
             " background: %1; color: %2; border: 1px solid %3;"
             " border-radius: %4px; padding: 0px %5px;"
             " min-height: %6px; selection-background-color: %7; }"
             "QLineEdit[mobileChrome=\"true\"]:focus { border-color: %7; }")
             .arg(css(t.surface2), css(t.text), css(t.outline))
             .arg(radiusSmall())
             .arg(dp(12))
             .arg(dp(44))
             .arg(css(t.accent));
    s += QStringLiteral(
             "QScrollArea[mobileChrome=\"true\"] { background: transparent; border: none; }"
             "QScrollArea[mobileChrome=\"true\"] > QWidget > QWidget { background: transparent; }"
             "QScrollArea[mobileChrome=\"true\"] QScrollBar:vertical {"
             " background: transparent; width: %1px; margin: 0px; }"
             "QScrollArea[mobileChrome=\"true\"] QScrollBar::handle:vertical {"
             " background: %2; border-radius: %3px; min-height: %4px; }"
             "QScrollArea[mobileChrome=\"true\"] QScrollBar::add-line,"
             "QScrollArea[mobileChrome=\"true\"] QScrollBar::sub-line { height: 0px; width: 0px; }"
             "QScrollArea[mobileChrome=\"true\"] QScrollBar::add-page,"
             "QScrollArea[mobileChrome=\"true\"] QScrollBar::sub-page { background: transparent; }")
             .arg(dp(4))
             .arg(css(t.outline))
             .arg(dp(2))
             .arg(dp(32));
    s += QStringLiteral(
             "QToolButton[mobileChip=\"true\"] {"
             " background: %1; color: %2; border: 1px solid %3;"
             " border-radius: %4px; padding: 0px %5px; min-height: %6px; }"
             "QToolButton[mobileChip=\"true\"]:checked {"
             " background: %7; color: %2; border: 2px solid %8; }"
             "QToolButton[mobileChip=\"true\"]:pressed { background: %9; }")
             .arg(css(t.surface2), css(t.text), css(t.outline))
             .arg(dp(18))
             .arg(dp(12))
             .arg(dp(36))
             .arg(css(t.accentSoft), css(t.accent), css(t.surface3));
    s += QStringLiteral(
             "QPushButton[mobileRole=\"primary\"] {"
             " background: %1; color: %2; border: none; border-radius: %3px;"
             " min-height: %4px; padding: 0px %5px; font-weight: bold; }"
             "QPushButton[mobileRole=\"primary\"]:pressed { background: %6; }"
             "QPushButton[mobileRole=\"primary\"]:disabled { background: %7; color: %8; }"
             "QPushButton[mobileRole=\"secondary\"] {"
             " background: %7; color: %9; border: 1px solid %10;"
             " border-radius: %3px; min-height: %4px; padding: 0px %5px; }"
             "QPushButton[mobileRole=\"secondary\"]:pressed { background: %11; }")
             .arg(css(t.accent), css(t.accentText))
             .arg(radiusSmall())
             .arg(dp(48))
             .arg(dp(20))
             .arg(css(t.accent.darker(115)),
                  css(t.surface2),
                  css(t.textDim),
                  css(t.text),
                  css(t.outline),
                  css(t.surface3));
    s += QStringLiteral(
             "QSpinBox[mobileChrome=\"true\"], QDoubleSpinBox[mobileChrome=\"true\"] {"
             " background: %1; color: %2; border: 1px solid %3;"
             " border-radius: %4px; min-height: %5px; padding: 0px %6px; }")
             .arg(css(t.surface2), css(t.text), css(t.outline))
             .arg(radiusSmall())
             .arg(dp(44))
             .arg(dp(10));
    s += QStringLiteral("QWidget#mobileHub { background: %1; }").arg(css(t.background));
    return s;
}

QString panelStyleSheet()
{
    const Theme &t = Theme::current();
    // Only geometry tweaks, so Krita's panels keep their look but get
    // finger-sized scroll bars, indicators and buttons.
    return QStringLiteral(
               "QScrollBar:vertical { width: %1px; }"
               "QScrollBar:horizontal { height: %1px; }"
               "QScrollBar::handle:vertical { min-height: %2px; }"
               "QScrollBar::handle:horizontal { min-width: %2px; }"
               "QCheckBox::indicator, QRadioButton::indicator { width: %3px; height: %3px; }"
               "QComboBox { min-height: %4px; }"
               "QToolButton { min-width: %5px; min-height: %5px; }"
               "QAbstractItemView { selection-background-color: %6; }")
        .arg(dp(10))
        .arg(dp(40))
        .arg(dp(22))
        .arg(dp(40))
        .arg(dp(36))
        .arg(css(t.accentSoft));
}

QString dialogStyleSheet()
{
    const Theme &t = Theme::current();
    // Krita's dialogs keep every control, but get the phone interface's
    // colors, rounded finger-sized buttons and fields, and tabs that are easy
    // to hit. Custom-painted Krita widgets (sliders, color selectors) are not
    // affected by these rules.
    return QStringLiteral(
               "QDialog { background: %1; }"
               "QPushButton { background: %2; color: %4; border: 1px solid %3; border-radius: %8px;"
               "  min-height: %9px; padding: 0 %10px; }"
               "QPushButton:pressed { background: %5; }"
               "QPushButton:default { background: %6; color: %7; border-color: %6; }"
               "QPushButton:disabled { color: %11; }"
               "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %2; color: %4; border: 1px solid %3;"
               "  border-radius: %12px; min-height: %13px; padding: 0 %12px; }"
               "QComboBox QAbstractItemView { background: %2; color: %4; }"
               "QTabBar::tab { min-height: %13px; padding: 0 %10px; background: transparent; color: %11;"
               "  border: none; border-bottom: 2px solid transparent; }"
               "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %6; }"
               "QGroupBox { border: 1px solid %3; border-radius: %8px; margin-top: %14px; padding-top: %12px; }"
               "QGroupBox::title { subcontrol-origin: margin; left: %12px; padding: 0 %15px; color: %11; }"
               "QAbstractItemView { background: %2; border: 1px solid %3; border-radius: %12px;"
               "  selection-background-color: %16; }"
               "QAbstractItemView::item { min-height: %9px; }"
               "QCheckBox, QRadioButton { min-height: %13px; spacing: %12px; }"
               "QSpinBox, QDoubleSpinBox { padding: 0 %15px; }"
               // The angle selector sizes its spin box itself.
               "KisAngleSelectorSpinBox { min-height: 0px; padding: 0px; }")
        .arg(css(t.surface))      // 1
        .arg(css(t.surface2))     // 2
        .arg(css(t.outline))      // 3
        .arg(css(t.text))         // 4
        .arg(css(t.surface3))     // 5
        .arg(css(t.accent))       // 6
        .arg(css(t.accentText))   // 7
        .arg(dp(12))              // 8
        .arg(dp(44))              // 9
        .arg(dp(16))              // 10
        .arg(css(t.textDim))      // 11
        .arg(dp(8))               // 12
        .arg(dp(40))              // 13
        .arg(dp(18))              // 14
        .arg(dp(4))               // 15
        .arg(css(t.accentSoft)) + panelStyleSheet(); // 16
}

QString popupStyleSheet()
{
    const Theme &t = Theme::current();
    // Krita's popup panels (gradients, patterns, workspaces, brush values):
    // the sheet's surface with rounded corners instead of a desktop frame.
    return QStringLiteral("[mobilePopup=\"true\"] { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; }")
        .arg(css(t.surface))
        .arg(css(t.text))
        .arg(css(t.outline))
        .arg(dp(12));
}

QIcon icon(const QString &name)
{
    return icon(name, Theme::current().text);
}

QIcon icon(const QString &name, const QColor &color)
{
    if (name.startsWith(QStringLiteral("krita:"))) {
        return KisIconUtils::loadIcon(name.mid(6));
    }
    const QByteArray svg = loadSvg(name);
    if (svg.isEmpty()) {
        return QIcon();
    }
    return QIcon(new TintedIconEngine(svg, color));
}

QString stripMnemonic(QString text)
{
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
    return text.trimmed();
}

void enableKineticScrolling(QAbstractScrollArea *area)
{
    if (!area) {
        return;
    }
    QWidget *viewport = area->viewport();
    // Touch input arrives as synthesized mouse events on Android, so the
    // left mouse button gesture covers fingers, styluses and mice alike.
    QScroller::grabGesture(viewport, QScroller::LeftMouseButtonGesture);
    QScroller *scroller = QScroller::scroller(viewport);
    QScrollerProperties props = scroller->scrollerProperties();
    props.setScrollMetric(QScrollerProperties::DragStartDistance, 0.003);
    props.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.3);
    props.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.1);
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                          QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff));
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                          QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff));
    scroller->setScrollerProperties(props);
}

} // namespace mobileui
