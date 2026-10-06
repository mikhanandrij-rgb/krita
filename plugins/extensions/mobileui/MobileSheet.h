/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from the Drawpile Mobile fork (same author, GPL-3.0-or-later).
 */
// Krita Mobile (unofficial fork): bottom sheet / side panel container.
#ifndef MOBILE_SHEET_H
#define MOBILE_SHEET_H
#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QVector>
#include <QSet>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QVariantAnimation;

namespace mobileui {

class SheetHeader;

// A panel that slides in from the bottom of the screen (portrait phones) or
// from the side (landscape and tablets). It is not modal: the canvas stays
// usable while it is open, and it can be dismissed by swiping it away, by the
// close button or by toggling the button that opened it.
//
// The sheet holds several named panels, each of which can have multiple tabs.
// Tab contents are arbitrary widgets; in practice these are mostly upstream
// dock widgets that get reparented into the sheet while the mobile interface
// is active.
class Sheet final : public QWidget {
    Q_OBJECT
public:
    enum class Placement { Bottom, Side };

    explicit Sheet(QWidget *parent);

    void addPanel(const QString &panelId, const QString &title);
    // Adds a tab to a panel. If scrollable is true, the content is wrapped in
    // a vertical scroll area so that tall panels work on short screens.
    void addTab(
        const QString &panelId, const QString &tabId, const QString &title,
        const QIcon &icon, QWidget *content, bool scrollable);
    // Removes the content widget from its tab without deleting it.
    QWidget *takeTabContent(const QString &panelId, const QString &tabId);
    // Puts a tab's content widget back after something else reparented it.
    void reattachTabContent(const QString &panelId, const QString &tabId);
    void setTabVisible(
        const QString &panelId, const QString &tabId, bool visible);
    void setPanelTitle(const QString &panelId, const QString &title);
    // Big editors (the brush editor) open the bottom sheet at full height.
    void setTabPrefersFullHeight(const QString &panelId, const QString &tabId);

    bool hasPanel(const QString &panelId) const;
    bool isOpen() const { return m_open; }
    QString currentPanel() const { return m_open ? m_currentPanel : QString(); }
    QString currentTab() const;

    // Area of the parent widget the sheet may occupy.
    void setArea(const QRect &area, Placement placement, bool leftSide);
    Placement placement() const { return m_placement; }
    // The rectangle the sheet covers when fully open, or an empty rectangle
    // when it's closed. Used to keep other overlays out of its way.
    QRect coveredRect() const;

    void setAnimationsEnabled(bool enabled) { m_animationsEnabled = enabled; }
    void refreshTheme();

public Q_SLOTS:
    void open(const QString &panelId, const QString &tabId = QString());
    void toggle(const QString &panelId, const QString &tabId = QString());
    void close();
    void selectTab(const QString &tabId);

Q_SIGNALS:
    void opened(const QString &panelId);
    void closed(const QString &panelId);
    void tabChanged(const QString &panelId, const QString &tabId);
    void coveredRectChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Tab {
        QString id;
        QString title;
        QIcon icon;
        QWidget *page;    // what's in the stack (content or scroll area)
        QWidget *content; // the actual widget
        QScrollArea *scroll;
        QToolButton *button;
        bool visible;
    };

    struct Panel {
        QString id;
        QString title;
        QStackedWidget *stack;
        QWidget *tabBar;
        QHBoxLayout *tabLayout;
        QButtonGroup *tabGroup;
        QVector<Tab> tabs;
        QString currentTab;
    };

    Panel *panel(const QString &panelId);
    const Panel *panel(const QString &panelId) const;
    void activatePanel(Panel *p, const QString &tabId);
    void updateTabBar(Panel *p);
    QRect openGeometry() const;
    QRect closedGeometry() const;
    void animateTo(const QRect &target, bool hideAfter);
    void expandForTab(const QString &panelId, const QString &tabId);
    bool isFullTabCurrent() const;
    void relayout();

    // Dragging the header resizes (bottom) or dismisses (both) the sheet.
    void dragStart(const QPoint &globalPos);
    void dragMove(const QPoint &globalPos);
    void dragEnd(const QPoint &globalPos);

    QWidget *m_header;
    QWidget *m_handle;
    QLabel *m_titleLabel;
    QToolButton *m_closeButton;
    QStackedWidget *m_panelStack;
    QStackedWidget *m_tabBarStack;
    QVector<Panel *> m_panels;
    QString m_currentPanel;
    QRect m_area;
    Placement m_placement = Placement::Bottom;
    bool m_leftSide = false;
    bool m_open = false;
    bool m_animationsEnabled = true;
    qreal m_fraction = 0.5;
    QSet<QString> m_fullHeightTabs;
    bool m_autoExpanded = false;
    qreal m_fractionBeforeExpand = 0.5;
    QVariantAnimation *m_animation;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_dragStartPos;
    QRect m_dragStartGeometry;
    QElapsedTimer m_dragTimer;
    QPoint m_lastDragPos;
    qint64 m_lastDragTime = 0;
    qreal m_dragVelocity = 0.0;
};

}

#endif
