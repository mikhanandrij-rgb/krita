/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later).
 */
#include "MobileSheet.h"
#include "MobileTheme.h"
#include <KisKineticScroller.h>
#include <klocalizedstring.h>

#include <QButtonGroup>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtMath>

namespace mobileui {

namespace {
constexpr qreal MIN_FRACTION = 0.32;
constexpr qreal HALF_FRACTION = 0.5;
constexpr qreal FULL_FRACTION = 0.92;
constexpr int ANIMATION_MS = 170;

QPoint eventGlobalPos(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition().toPoint();
#else
    return event->globalPos();
#endif
}
}

Sheet::Sheet(QWidget *parent)
    : QWidget(parent)
    , m_animation(new QVariantAnimation(this))
{
    setObjectName(QStringLiteral("mobileSheet"));
    setProperty("mobileChrome", true);
    connect(this, &Sheet::tabChanged, this, &Sheet::expandForTab);
    setAttribute(Qt::WA_NoSystemBackground, false);
    hide();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_handle = new QWidget;
    m_handle->setFixedHeight(dp(18));
    m_handle->installEventFilter(this);
    layout->addWidget(m_handle);

    m_header = new QWidget;
    m_header->installEventFilter(this);
    QHBoxLayout *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(dp(20), 0, dp(8), 0);
    headerLayout->setSpacing(dp(8));
    m_titleLabel = new QLabel;
    m_titleLabel->setProperty("mobileRole", QStringLiteral("title"));
    applyFont(m_titleLabel, TextRole::Subtitle, true);
    m_titleLabel->installEventFilter(this);
    headerLayout->addWidget(m_titleLabel, 1);
    m_closeButton = new QToolButton;
    m_closeButton->setObjectName(QStringLiteral("mobileSheetClose"));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setIcon(mobileui::icon(QStringLiteral("close")));
    m_closeButton->setIconSize(QSize(dp(22), dp(22)));
    m_closeButton->setFixedSize(touchTarget(), touchTarget());
    m_closeButton->setToolTip(i18n("Close"));
    m_closeButton->setAccessibleName(i18n("Close"));
    connect(m_closeButton, &QToolButton::clicked, this, &Sheet::close);
    headerLayout->addWidget(m_closeButton);
    layout->addWidget(m_header);

    m_tabBarStack = new QStackedWidget;
    m_tabBarStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_tabBarStack);

    m_panelStack = new QStackedWidget;
    m_panelStack->setObjectName(QStringLiteral("mobileSheetContent"));
    // Krita's dockers in the sheet get the phone look and phone-friendly
    // controls (see DialogFitter::adaptControls).
    m_panelStack->setStyleSheet(dialogStyleSheet());
    m_panelStack->setProperty("mobilePhoneRoot", true);
    layout->addWidget(m_panelStack, 1);

    m_animation->setDuration(ANIMATION_MS);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        m_animation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant &value) {
            setGeometry(value.toRect());
        });
    // Hide once a closing animation is done. Opening animations leave the
    // sheet visible, since m_open is true for them.
    connect(m_animation, &QVariantAnimation::finished, this, [this] {
        if(!m_open) {
            hide();
        }
    });
}

void Sheet::refreshTheme()
{
    m_closeButton->setIcon(mobileui::icon(QStringLiteral("close")));
    m_panelStack->setStyleSheet(dialogStyleSheet());
    update();
}

void Sheet::addPanel(const QString &panelId, const QString &title)
{
    if(panel(panelId)) {
        return;
    }
    Panel *p = new Panel;
    p->id = panelId;
    p->title = title;
    p->stack = new QStackedWidget;
    m_panelStack->addWidget(p->stack);

    p->tabBar = new QWidget;
    p->tabLayout = new QHBoxLayout(p->tabBar);
    p->tabLayout->setContentsMargins(dp(16), dp(2), dp(16), dp(8));
    p->tabLayout->setSpacing(dp(8));
    p->tabLayout->addStretch(1);
    p->tabGroup = new QButtonGroup(p->tabBar);
    p->tabGroup->setExclusive(true);
    m_tabBarStack->addWidget(p->tabBar);
    m_panels.append(p);
}

void Sheet::addTab(
    const QString &panelId, const QString &tabId, const QString &title,
    const QIcon &tabIcon, QWidget *content, bool scrollable)
{
    Panel *p = panel(panelId);
    if(!p) {
        addPanel(panelId, title);
        p = panel(panelId);
    }

    Tab tab;
    tab.id = tabId;
    tab.title = title;
    tab.icon = tabIcon;
    tab.content = content;
    tab.visible = true;
    if(scrollable) {
        QScrollArea *scroll = new QScrollArea;
        scroll->setProperty("mobileChrome", true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(content);
        // Krita's own scroller configuration: on Android it reacts to touch
        // gestures only, so sliders and color wheels in Krita's panels keep
        // working with the finger and the stylus.
        KisKineticScroller::createPreconfiguredScroller(scroll);
        tab.scroll = scroll;
        tab.page = scroll;
    } else {
        tab.scroll = nullptr;
        tab.page = content;
    }
    p->stack->addWidget(tab.page);

    QToolButton *button = new QToolButton;
    button->setProperty("mobileChip", true);
    button->setCheckable(true);
    button->setText(title);
    button->setIcon(tabIcon);
    button->setIconSize(QSize(dp(18), dp(18)));
    button->setToolButtonStyle(
        tabIcon.isNull() ? Qt::ToolButtonTextOnly
                         : Qt::ToolButtonTextBesideIcon);
    button->setMinimumHeight(dp(36));
    button->setAccessibleName(title);
    applyFont(button, TextRole::Label, true);
    p->tabGroup->addButton(button);
    // Insert before the trailing stretch.
    p->tabLayout->insertWidget(p->tabLayout->count() - 1, button);
    connect(button, &QToolButton::clicked, this, [this, tabId] {
        selectTab(tabId);
    });
    tab.button = button;
    p->tabs.append(tab);
    if(p->currentTab.isEmpty()) {
        p->currentTab = tabId;
        button->setChecked(true);
    }
    updateTabBar(p);
}

QWidget *Sheet::takeTabContent(const QString &panelId, const QString &tabId)
{
    Panel *p = panel(panelId);
    if(!p) {
        return nullptr;
    }
    for(int i = 0; i < p->tabs.size(); ++i) {
        Tab &tab = p->tabs[i];
        if(tab.id == tabId) {
            QWidget *content = tab.content;
            if(tab.scroll) {
                tab.scroll->takeWidget();
            }
            p->stack->removeWidget(tab.page);
            if(tab.page != content) {
                tab.page->deleteLater();
            }
            if(content) {
                content->setParent(nullptr);
            }
            delete tab.button;
            p->tabs.removeAt(i);
            if(p->currentTab == tabId) {
                p->currentTab =
                    p->tabs.isEmpty() ? QString() : p->tabs.first().id;
            }
            updateTabBar(p);
            return content;
        }
    }
    return nullptr;
}

void Sheet::reattachTabContent(const QString &panelId, const QString &tabId)
{
    Panel *p = panel(panelId);
    if(!p) {
        return;
    }
    for(Tab &tab : p->tabs) {
        if(tab.id == tabId && tab.content) {
            if(tab.scroll) {
                if(tab.scroll->widget() == tab.content &&
                   tab.content->parentWidget() == tab.scroll->viewport()) {
                    return;
                }
                // QScrollArea ignores setting the same widget again, so take
                // the stale one out first.
                tab.scroll->takeWidget();
                tab.scroll->setWidget(tab.content);
            } else if(tab.content->parentWidget() != p->stack) {
                int index = p->stack->indexOf(tab.page);
                if(index != -1) {
                    p->stack->removeWidget(tab.page);
                }
                p->stack->insertWidget(
                    index == -1 ? p->stack->count() : index, tab.content);
                if(p->currentTab == tabId) {
                    p->stack->setCurrentWidget(tab.content);
                }
            }
            return;
        }
    }
}

void Sheet::setTabVisible(
    const QString &panelId, const QString &tabId, bool visible)
{
    Panel *p = panel(panelId);
    if(p) {
        for(Tab &tab : p->tabs) {
            if(tab.id == tabId) {
                tab.visible = visible;
                tab.button->setVisible(visible);
            }
        }
        updateTabBar(p);
    }
}

void Sheet::setPanelTitle(const QString &panelId, const QString &title)
{
    Panel *p = panel(panelId);
    if(p) {
        p->title = title;
        if(m_currentPanel == panelId) {
            m_titleLabel->setText(title);
        }
    }
}

bool Sheet::hasPanel(const QString &panelId) const
{
    return panel(panelId) != nullptr;
}

QString Sheet::currentTab() const
{
    const Panel *p = panel(m_currentPanel);
    return p && m_open ? p->currentTab : QString();
}

void Sheet::setArea(const QRect &area, Placement placement, bool leftSide)
{
    bool placementChanged = placement != m_placement;
    m_area = area;
    m_placement = placement;
    m_leftSide = leftSide;
    if(placementChanged) {
        m_handle->setVisible(placement == Placement::Bottom);
    }
    if(m_open && !m_dragging) {
        QRect target = openGeometry();
        if(target != geometry() && m_animation->state() != QAbstractAnimation::Running) {
            setGeometry(target);
            Q_EMIT coveredRectChanged();
        }
    }
    update();
}

QRect Sheet::coveredRect() const
{
    // The target geometry: while the sheet animates open, geometry() is
    // still the closed position.
    return m_open ? openGeometry() : QRect();
}

void Sheet::open(const QString &panelId, const QString &tabId)
{
    Panel *p = panel(panelId);
    if(!p) {
        qWarning("Mobile UI: unknown sheet panel '%s'", qUtf8Printable(panelId));
        return;
    }

    QString previousPanel = m_open ? m_currentPanel : QString();
    activatePanel(p, tabId);
    if(m_open) {
        if(previousPanel != panelId) {
            Q_EMIT closed(previousPanel);
            Q_EMIT opened(panelId);
        }
        return;
    }

    m_open = true;
    m_handle->setVisible(m_placement == Placement::Bottom);
    if(m_animationsEnabled) {
        setGeometry(closedGeometry());
        show();
        animateTo(openGeometry(), false);
    } else {
        setGeometry(openGeometry());
        show();
    }
    Q_EMIT opened(panelId);
    Q_EMIT coveredRectChanged();
}

void Sheet::toggle(const QString &panelId, const QString &tabId)
{
    if(m_open && m_currentPanel == panelId &&
       (tabId.isEmpty() || currentTab() == tabId)) {
        close();
    } else {
        open(panelId, tabId);
    }
}

void Sheet::close()
{
    if(!m_open) {
        return;
    }
    m_open = false;
    QString panelId = m_currentPanel;
    if(m_animationsEnabled && isVisible()) {
        animateTo(closedGeometry(), true);
    } else {
        m_animation->stop();
        hide();
    }
    if(m_autoExpanded) {
        m_autoExpanded = false;
        m_fraction = m_fractionBeforeExpand;
    }
    Q_EMIT closed(panelId);
    Q_EMIT coveredRectChanged();
}

void Sheet::setTabPrefersFullHeight(const QString &panelId, const QString &tabId)
{
    m_fullHeightTabs.insert(panelId + QLatin1Char('/') + tabId);
}

bool Sheet::isFullTabCurrent() const
{
    const Panel *p = const_cast<Sheet *>(this)->panel(m_currentPanel);
    return p && m_fullHeightTabs.contains(p->id + QLatin1Char('/') + p->currentTab);
}

void Sheet::expandForTab(const QString &panelId, const QString &tabId)
{
    if(m_placement == Placement::Side) {
        // The side sheet's width follows the current tab.
        if(m_open && !m_dragging) {
            setGeometry(openGeometry());
            Q_EMIT coveredRectChanged();
        }
        return;
    }
    const bool wantsFull = m_fullHeightTabs.contains(panelId + QLatin1Char('/') + tabId);
    if(!wantsFull) {
        // Back to the height the user had before a big editor expanded it.
        if(m_autoExpanded) {
            m_autoExpanded = false;
            m_fraction = m_fractionBeforeExpand;
            if(m_open && !m_dragging) {
                if(m_animationsEnabled) {
                    animateTo(openGeometry(), false);
                } else {
                    setGeometry(openGeometry());
                }
                Q_EMIT coveredRectChanged();
            }
        }
        return;
    }
    if(m_fraction >= FULL_FRACTION) {
        return;
    }
    m_autoExpanded = true;
    m_fractionBeforeExpand = m_fraction;
    m_fraction = FULL_FRACTION;
    if(m_open) {
        if(m_animationsEnabled) {
            animateTo(openGeometry(), false);
        } else {
            setGeometry(openGeometry());
        }
        Q_EMIT coveredRectChanged();
    }
}

void Sheet::selectTab(const QString &tabId)
{
    Panel *p = panel(m_currentPanel);
    if(p) {
        for(const Tab &tab : p->tabs) {
            if(tab.id == tabId) {
                p->currentTab = tabId;
                p->stack->setCurrentWidget(tab.page);
                tab.button->setChecked(true);
                Q_EMIT tabChanged(p->id, tabId);
                return;
            }
        }
    }
}

void Sheet::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    qreal radius = radiusLarge();
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    if(m_placement == Placement::Bottom) {
        // Only round the top corners, the bottom sits flush on the tool bar.
        path.addRoundedRect(r.adjusted(0, 0, 0, radius), radius, radius);
    } else {
        path.addRoundedRect(r, radius, radius);
    }
    painter.fillPath(path, t.surface);
    QPen pen(t.outline);
    pen.setWidthF(1.0);
    painter.setPen(pen);
    painter.drawPath(path);
}

bool Sheet::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == m_handle && event->type() == QEvent::Paint) {
        QPainter painter(m_handle);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor color = Theme::current().textDim;
        color.setAlphaF(0.6);
        int w = dp(36);
        int h = dp(4);
        QRectF pill(
            (m_handle->width() - w) / 2.0, (m_handle->height() - h) / 2.0 + dp(2),
            w, h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(pill, h / 2.0, h / 2.0);
        return true;
    }

    if(watched == m_handle || watched == m_header || watched == m_titleLabel) {
        switch(event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if(me->button() == Qt::LeftButton) {
                dragStart(eventGlobalPos(me));
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
            if(m_dragging) {
                dragMove(eventGlobalPos(static_cast<QMouseEvent *>(event)));
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if(m_dragging) {
                dragEnd(eventGlobalPos(static_cast<QMouseEvent *>(event)));
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

Sheet::Panel *Sheet::panel(const QString &panelId)
{
    for(Panel *p : m_panels) {
        if(p->id == panelId) {
            return p;
        }
    }
    return nullptr;
}

const Sheet::Panel *Sheet::panel(const QString &panelId) const
{
    for(const Panel *p : m_panels) {
        if(p->id == panelId) {
            return p;
        }
    }
    return nullptr;
}

void Sheet::activatePanel(Panel *p, const QString &tabId)
{
    m_currentPanel = p->id;
    m_titleLabel->setText(p->title);
    m_panelStack->setCurrentWidget(p->stack);
    m_tabBarStack->setCurrentWidget(p->tabBar);
    updateTabBar(p);
    QString wantedTab = tabId.isEmpty() ? p->currentTab : tabId;
    for(const Tab &tab : p->tabs) {
        if(tab.id == wantedTab) {
            p->currentTab = wantedTab;
            p->stack->setCurrentWidget(tab.page);
            tab.button->setChecked(true);
            Q_EMIT tabChanged(p->id, wantedTab);
            break;
        }
    }
}

void Sheet::updateTabBar(Panel *p)
{
    int visibleTabs = 0;
    for(const Tab &tab : p->tabs) {
        if(tab.visible) {
            ++visibleTabs;
        }
    }
    bool show = visibleTabs > 1;
    p->tabBar->setVisible(show);
    if(m_tabBarStack->currentWidget() == p->tabBar) {
        m_tabBarStack->setVisible(show);
        if(show) {
            // Collapse chips to icons if they don't fit the available width.
            int available = (m_area.isValid() ? openGeometry().width() : width()) -
                            dp(32);
            int needed = 0;
            for(const Tab &tab : p->tabs) {
                if(tab.visible) {
                    tab.button->setToolButtonStyle(
                        tab.icon.isNull() ? Qt::ToolButtonTextOnly
                                          : Qt::ToolButtonTextBesideIcon);
                    needed += tab.button->sizeHint().width() + dp(8);
                }
            }
            if(needed > available) {
                for(const Tab &tab : p->tabs) {
                    if(!tab.icon.isNull()) {
                        tab.button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                        tab.button->setMinimumWidth(dp(48));
                    }
                }
            }
            m_tabBarStack->setFixedHeight(p->tabBar->sizeHint().height());
        }
    }
}

QRect Sheet::openGeometry() const
{
    const QRect &a = m_area;
    if(m_placement == Placement::Bottom) {
        int w = qMin(a.width(), dp(720));
        int x = a.x() + (a.width() - w) / 2;
        int h = qBound(
            qMin(dp(220), a.height()), qRound(a.height() * m_fraction),
            a.height());
        return QRect(x, a.y() + a.height() - h, w, h);
    } else {
        int margin = dp(8);
        int w = qBound(dp(300), qRound(a.width() * 0.4), dp(440));
        // Big editors (the brush editor) get a wider side sheet.
        if(isFullTabCurrent()) {
            w = qMax(w, qMin(qRound(a.width() * 0.7), dp(720)));
        }
        w = qMin(w, a.width() - dp(64));
        int h = a.height() - margin * 2;
        int x = m_leftSide ? a.x() + margin : a.x() + a.width() - margin - w;
        return QRect(x, a.y() + margin, w, h);
    }
}

QRect Sheet::closedGeometry() const
{
    QRect r = openGeometry();
    if(m_placement == Placement::Bottom) {
        r.moveTop(m_area.y() + m_area.height());
    } else if(m_leftSide) {
        r.moveRight(m_area.x() - 1);
    } else {
        r.moveLeft(m_area.x() + m_area.width());
    }
    return r;
}

void Sheet::animateTo(const QRect &target, bool hideAfter)
{
    m_animation->stop();
    if(!m_animationsEnabled) {
        setGeometry(target);
        if(hideAfter && !m_open) {
            hide();
        }
        return;
    }
    m_animation->setStartValue(geometry());
    m_animation->setEndValue(target);
    m_animation->start();
}

void Sheet::dragStart(const QPoint &globalPos)
{
    m_animation->stop();
    m_dragging = true;
    m_dragMoved = false;
    m_dragStartPos = globalPos;
    m_dragStartGeometry = geometry();
    m_dragTimer.start();
    m_lastDragPos = globalPos;
    m_lastDragTime = 0;
    m_dragVelocity = 0.0;
}

void Sheet::dragMove(const QPoint &globalPos)
{
    QPoint delta = globalPos - m_dragStartPos;
    int primary = m_placement == Placement::Bottom ? delta.y()
                  : m_leftSide                     ? -delta.x()
                                                   : delta.x();
    if(!m_dragMoved && qAbs(primary) < dp(6)) {
        return;
    }
    m_dragMoved = true;

    qint64 now = m_dragTimer.elapsed();
    qint64 dt = now - m_lastDragTime;
    if(dt > 0) {
        QPoint step = globalPos - m_lastDragPos;
        int stepPrimary = m_placement == Placement::Bottom ? step.y()
                          : m_leftSide                     ? -step.x()
                                                           : step.x();
        // Smooth the velocity a bit, touch input is jittery.
        m_dragVelocity = m_dragVelocity * 0.4 + (qreal(stepPrimary) / dt) * 0.6;
    }
    m_lastDragPos = globalPos;
    m_lastDragTime = now;

    QRect g = m_dragStartGeometry;
    if(m_placement == Placement::Bottom) {
        int bottom = m_area.y() + m_area.height();
        int top = qBound(m_area.y(), g.top() + delta.y(), bottom - dp(48));
        setGeometry(g.x(), top, g.width(), bottom - top);
    } else {
        int offset = qMax(0, primary);
        setGeometry(g.translated(m_leftSide ? -offset : offset, 0));
    }
    Q_EMIT coveredRectChanged();
}

void Sheet::dragEnd(const QPoint &globalPos)
{
    Q_UNUSED(globalPos);
    m_dragging = false;
    if(!m_dragMoved) {
        return;
    }

    const qreal flingVelocity = 0.9; // px per millisecond
    if(m_placement == Placement::Bottom) {
        int startHeight = m_dragStartGeometry.height();
        int currentHeight = height();
        bool flungDown = m_dragVelocity > flingVelocity;
        bool flungUp = m_dragVelocity < -flingVelocity;
        qreal fraction = m_area.height() > 0
                             ? qreal(currentHeight) / qreal(m_area.height())
                             : HALF_FRACTION;
        if(flungDown ||
           (fraction < MIN_FRACTION && currentHeight < startHeight)) {
            close();
            return;
        }
        if(flungUp || fraction > (HALF_FRACTION + FULL_FRACTION) / 2.0) {
            m_fraction = FULL_FRACTION;
        } else {
            m_fraction = HALF_FRACTION;
        }
        // The user picked this height; keep it.
        m_autoExpanded = false;
        animateTo(openGeometry(), false);
    } else {
        int offset = qAbs(geometry().x() - m_dragStartGeometry.x());
        if(m_dragVelocity > flingVelocity || offset > width() / 3) {
            close();
        } else {
            animateTo(openGeometry(), false);
        }
    }
    Q_EMIT coveredRectChanged();
}

void Sheet::relayout()
{
    if(m_open) {
        setGeometry(openGeometry());
    }
}

}
