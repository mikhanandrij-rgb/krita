/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later).
 */
#include "MobileWidgets.h"
#include "MobileTheme.h"
#include <QAction>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace mobileui {


ChromeButton::ChromeButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setProperty("mobileChrome", true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(450);
    connect(&m_longPressTimer, &QTimer::timeout, this, [this] {
        if(isDown()) {
            m_longPressFired = true;
            setDown(false);
            Q_EMIT longPressed();
        }
    });
}

ChromeButton::ChromeButton(
    const QString &iconName, const QString &text, QWidget *parent)
    : ChromeButton(parent)
{
    setIconName(iconName);
    setText(text);
    setToolTip(text);
    setAccessibleName(text);
}

void ChromeButton::setIconName(const QString &iconName)
{
    m_iconName = iconName;
    m_useThemeIcon = false;
    update();
}

void ChromeButton::setThemeIcon(const QIcon &icon)
{
    m_themeIcon = icon;
    m_useThemeIcon = true;
    update();
}

void ChromeButton::setBadge(const QString &badge)
{
    if(m_badge != badge) {
        m_badge = badge;
        update();
    }
}

void ChromeButton::setSwatch(const QColor &color, const QColor &secondary)
{
    m_swatch = color;
    m_swatchSecondary = secondary;
    update();
}

void ChromeButton::setButtonSize(int sizeDp)
{
    m_sizeDp = sizeDp;
    updateGeometry();
}

void ChromeButton::setShowLabel(bool showLabel)
{
    m_showLabel = showLabel;
    updateGeometry();
    update();
}

void ChromeButton::setFloating(bool floating)
{
    m_floating = floating;
    update();
}

void ChromeButton::bindAction(QAction *action)
{
    if(m_action) {
        disconnect(m_action, nullptr, this, nullptr);
    }
    m_action = action;
    if(action) {
        connect(
            action, &QAction::changed, this, &ChromeButton::syncFromAction);
        connect(
            action, &QAction::toggled, this, &ChromeButton::syncFromAction);
        if(!m_clickConnection) {
            m_clickConnection =
                connect(this, &ChromeButton::clicked, this, [this] {
                    if(m_action) {
                        m_action->trigger();
                        syncFromAction();
                    }
                });
        }
        syncFromAction();
    }
}

void ChromeButton::refreshIcon()
{
    update();
}

QSize ChromeButton::sizeHint() const
{
    int s = dp(m_sizeDp);
    if(m_showLabel) {
        return QSize(dp(76), s + fontPixelSize(TextRole::Caption) * 2 + dp(6));
    }
    return QSize(s, s);
}

QSize ChromeButton::minimumSizeHint() const
{
    // Allow bars to squeeze buttons a bit on very narrow screens (or with a
    // large interface scale) instead of overflowing the window.
    if(m_showLabel) {
        return sizeHint();
    }
    int s = qMin(dp(m_sizeDp), dp(36));
    return QSize(s, s);
}

void ChromeButton::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int s = dp(m_sizeDp);
    QRect iconArea = m_showLabel ? QRect((width() - s) / 2, 0, s, s) : rect();
    QRectF bg = QRectF(iconArea).adjusted(dp(4), dp(4), -dp(4), -dp(4));
    qreal radius = dp(12);
    if(m_floating) {
        QColor backdrop = isChecked() ? t.accentSoft : t.surface;
        backdrop.setAlphaF(0.94);
        painter.setPen(QPen(t.outline, 1.0));
        painter.setBrush(isDown() ? t.surface3 : backdrop);
        qreal d = qMin(iconArea.width(), iconArea.height()) - dp(4);
        painter.drawEllipse(QRectF(
            QRectF(iconArea).center().x() - d / 2.0,
            QRectF(iconArea).center().y() - d / 2.0, d, d));
    } else if(isDown()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.surface3);
        painter.drawRoundedRect(bg, radius, radius);
    } else if(isChecked()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.accentSoft);
        painter.drawRoundedRect(bg, radius, radius);
    }

    QPointF center = QRectF(iconArea).center();
    if(m_swatch.isValid()) {
        qreal d = dp(26);
        QRectF circle(center.x() - d / 2.0, center.y() - d / 2.0, d, d);
        if(m_swatchSecondary.isValid()) {
            qreal d2 = dp(16);
            QRectF second(
                circle.right() - d2 * 0.6, circle.bottom() - d2 * 0.6, d2, d2);
            painter.setPen(QPen(t.surface, dp(2)));
            painter.setBrush(m_swatchSecondary);
            painter.drawEllipse(second);
        }
        painter.setPen(QPen(t.surface, dp(2)));
        painter.setBrush(m_swatch);
        painter.drawEllipse(circle);
        QColor ring = t.text;
        ring.setAlphaF(0.55);
        painter.setPen(QPen(ring, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circle.adjusted(-dp(2), -dp(2), dp(2), dp(2)));
    } else {
        int is = dp(m_sizeDp >= 44 ? 24 : 20);
        QRect iconRect(
            qRound(center.x() - is / 2.0), qRound(center.y() - is / 2.0), is,
            is);
        QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
        if(m_useThemeIcon) {
            m_themeIcon.paint(&painter, iconRect, Qt::AlignCenter, mode);
        } else if(!m_iconName.isEmpty()) {
            QColor color = isChecked() ? t.accent : t.text;
            mobileui::icon(m_iconName, color)
                .paint(&painter, iconRect, Qt::AlignCenter, mode);
        }
    }

    if(m_showLabel) {
        QFont f = font();
        f.setPixelSize(fontPixelSize(TextRole::Caption));
        painter.setFont(f);
        QColor color = isEnabled() ? (isChecked() ? t.accent : t.text)
                                   : t.textDim;
        painter.setPen(color);
        QRect textRect(0, s, width(), height() - s);
        QFontMetrics fm(f);
        QString label = stripMnemonic(text());
        painter.drawText(
            textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            label);
    }

    if(!m_badge.isEmpty()) {
        QFont f = font();
        f.setPixelSize(dp(10));
        f.setBold(true);
        painter.setFont(f);
        QFontMetrics fm(f);
        int h = dp(16);
        int w = qMax(h, fm.horizontalAdvance(m_badge) + dp(8));
        QRectF badge(iconArea.right() - w - dp(2), iconArea.top() + dp(4), w, h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.danger);
        painter.drawRoundedRect(badge, h / 2.0, h / 2.0);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, m_badge);
    }
}

void ChromeButton::mousePressEvent(QMouseEvent *event)
{
    m_longPressFired = false;
    QAbstractButton::mousePressEvent(event);
    if(isDown() && receivers(SIGNAL(longPressed())) > 0) {
        m_longPressTimer.start();
    }
}

void ChromeButton::mouseReleaseEvent(QMouseEvent *event)
{
    m_longPressTimer.stop();
    if(m_longPressFired) {
        // The long press already did something, don't also click.
        m_longPressFired = false;
        setDown(false);
        event->accept();
        return;
    }
    QAbstractButton::mouseReleaseEvent(event);
}

void ChromeButton::changeEvent(QEvent *event)
{
    QAbstractButton::changeEvent(event);
    if(event->type() == QEvent::EnabledChange) {
        update();
    }
}

void ChromeButton::syncFromAction()
{
    if(!m_action) {
        return;
    }
    setEnabled(m_action->isEnabled());
    setCheckable(m_action->isCheckable());
    if(m_action->isCheckable()) {
        setChecked(m_action->isChecked());
    }
    QString label = stripMnemonic(m_action->text());
    setToolTip(label);
    setAccessibleName(label);
    if(m_showLabel) {
        setText(label);
    }
    update();
}

Pill::Pill(QWidget *parent)
    : QAbstractButton(parent)
    , m_dot(Qt::green)
{
    setFocusPolicy(Qt::NoFocus);
    setProperty("mobileChrome", true);
    applyFont(this, TextRole::Caption, true);
}

void Pill::setDotColor(const QColor &color)
{
    m_dot = color;
    update();
}

QSize Pill::sizeHint() const
{
    QFontMetrics fm(font());
    return QSize(
        fm.horizontalAdvance(text()) + dp(10) + dp(8) + dp(12), dp(48));
}

void Pill::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    int h = dp(28);
    QRectF r(0.5, (height() - h) / 2.0, width() - 1.0, h);
    painter.setPen(QPen(t.outline, 1.0));
    painter.setBrush(isDown() ? t.surface3 : t.surface2);
    painter.drawRoundedRect(r, h / 2.0, h / 2.0);
    qreal d = dp(8);
    QRectF dot(r.left() + dp(10), r.center().y() - d / 2.0, d, d);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_dot);
    painter.drawEllipse(dot);
    painter.setPen(t.text);
    painter.drawText(
        r.adjusted(dp(10) + d + dp(6), 0, -dp(10), 0),
        Qt::AlignVCenter | Qt::AlignLeft, text());
}

namespace {
// Greedy word wrap into at most two lines, eliding the second one.
QStringList wrapTwoLines(const QString &text, const QFontMetrics &fm, int width)
{
    if(fm.horizontalAdvance(text) <= width) {
        return {text};
    }
    QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString first;
    int i = 0;
    for(; i < words.size(); ++i) {
        QString candidate =
            first.isEmpty() ? words[i] : first + QLatin1Char(' ') + words[i];
        if(!first.isEmpty() && fm.horizontalAdvance(candidate) > width) {
            break;
        }
        first = candidate;
    }
    QStringList restWords;
    for(int j = i; j < words.size(); ++j) {
        restWords.append(words[j]);
    }
    QString rest = restWords.join(QLatin1Char(' '));
    QStringList lines = {fm.elidedText(first, Qt::ElideRight, width)};
    if(!rest.isEmpty()) {
        lines.append(fm.elidedText(rest, Qt::ElideRight, width));
    }
    return lines;
}
}

ActionCard::ActionCard(
    const QString &iconName, const QString &title, const QString &subtitle,
    QWidget *parent)
    : QAbstractButton(parent)
    , m_iconName(iconName)
    , m_subtitle(subtitle)
{
    setText(title);
    setAccessibleName(title);
    setFocusPolicy(Qt::NoFocus);
    setProperty("mobileChrome", true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ActionCard::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
    updateGeometry();
    update();
}

void ActionCard::setBadge(const QString &badge)
{
    m_badge = badge;
    update();
}

void ActionCard::setCompact(bool compact)
{
    m_compact = compact;
    updateGeometry();
    update();
}

void ActionCard::setThemeIcon(const QIcon &icon)
{
    m_themeIcon = icon;
    update();
}

QSize ActionCard::sizeHint() const
{
    if(m_compact) {
        return QSize(dp(96), dp(84));
    }
    return QSize(dp(160), m_subtitle.isEmpty() ? dp(56) : dp(68));
}

QSize ActionCard::minimumSizeHint() const
{
    return m_compact ? QSize(dp(72), dp(84)) : QSize(dp(120), dp(56));
}

void ActionCard::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    qreal radius = radiusMedium();
    bool on = isCheckable() && isChecked();
    painter.setPen(QPen(on ? t.accent : t.outline, on ? 1.5 : 1.0));
    painter.setBrush(isDown() ? t.surface3 : on ? t.accentSoft : t.surface2);
    painter.drawRoundedRect(r, radius, radius);

    QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
    QIcon ic = m_themeIcon.isNull() ? mobileui::icon(m_iconName, t.accent) : m_themeIcon;
    QFont titleFont = font();
    titleFont.setPixelSize(fontPixelSize(m_compact ? TextRole::Label : TextRole::Body));
    titleFont.setBold(true);
    QFont subFont = font();
    subFont.setPixelSize(fontPixelSize(TextRole::Caption));

    if(m_compact) {
        int is = dp(26);
        QRect iconRect((width() - is) / 2, dp(14), is, is);
        ic.paint(&painter, iconRect, Qt::AlignCenter, mode);
        // Fit the label into two lines: shrink the font if a single word is
        // too long, then elide words that still don't fit.
        QRectF textRect(dp(4), dp(46), width() - dp(8), height() - dp(48));
        QStringList words = text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QFontMetrics fm(titleFont);
        int longest = 0;
        for(const QString &word : words) {
            longest = qMax(longest, fm.horizontalAdvance(word));
        }
        if(longest > textRect.width()) {
            titleFont.setPixelSize(fontPixelSize(TextRole::Caption));
            fm = QFontMetrics(titleFont);
        }
        for(QString &word : words) {
            word = fm.elidedText(word, Qt::ElideRight, int(textRect.width()));
        }
        painter.setFont(titleFont);
        painter.setPen(isEnabled() ? t.text : t.textDim);
        painter.drawText(
            textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            words.join(QLatin1Char(' ')));
    } else {
        int is = dp(24);
        QRect iconRect(dp(16), (height() - is) / 2, is, is);
        ic.paint(&painter, iconRect, Qt::AlignCenter, mode);
        qreal x = dp(16) + is + dp(14);
        QRectF textRect(x, 0, width() - x - dp(12), height());
        painter.setFont(titleFont);
        painter.setPen(isEnabled() ? t.text : t.textDim);
        QFontMetrics tfm(titleFont);
        // Long titles (common in translations) wrap onto a second line
        // instead of being cut off after a few letters.
        QStringList lines = wrapTwoLines(text(), tfm, int(textRect.width()));
        QFontMetrics sfm(subFont);
        bool showSubtitle = !m_subtitle.isEmpty();
        qreal total = tfm.height() * lines.size() +
                      (showSubtitle ? dp(2) + sfm.height() : 0);
        if(showSubtitle && total > height() - dp(8)) {
            showSubtitle = false;
            total = tfm.height() * lines.size();
        }
        qreal y = (height() - total) / 2.0;
        for(const QString &line : lines) {
            painter.drawText(
                QRectF(x, y, textRect.width(), tfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter, line);
            y += tfm.height();
        }
        if(showSubtitle) {
            painter.setFont(subFont);
            painter.setPen(t.textDim);
            painter.drawText(
                QRectF(x, y + dp(2), textRect.width(), sfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText(
                    m_subtitle, Qt::ElideRight, int(textRect.width())));
        }
    }

    if(!m_badge.isEmpty()) {
        QFont f = font();
        f.setPixelSize(dp(11));
        f.setBold(true);
        painter.setFont(f);
        QFontMetrics fm(f);
        int h = dp(18);
        int w = qMax(h, fm.horizontalAdvance(m_badge) + dp(10));
        QRectF badge(width() - w - dp(8), dp(8), w, h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.accent);
        painter.drawRoundedRect(badge, h / 2.0, h / 2.0);
        painter.setPen(t.accentText);
        painter.drawText(badge, Qt::AlignCenter, m_badge);
    }
}

FlowLayout::FlowLayout(QWidget *parent, int spacing)
    : QLayout(parent)
    , m_spacing(spacing < 0 ? dp(8) : spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while(QLayoutItem *item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index)
                                                 : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return Qt::Orientations();
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for(const QLayoutItem *item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    QMargins m = contentsMargins();
    QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for(QLayoutItem *item : m_items) {
        if(item->widget() && item->widget()->isHidden()) {
            continue;
        }
        QSize hint = item->sizeHint();
        int nextX = x + hint.width() + m_spacing;
        if(nextX - m_spacing > area.right() + 1 && lineHeight > 0) {
            x = area.x();
            y += lineHeight + m_spacing;
            nextX = x + hint.width() + m_spacing;
            lineHeight = 0;
        }
        if(!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), hint));
        }
        x = nextX;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

}
