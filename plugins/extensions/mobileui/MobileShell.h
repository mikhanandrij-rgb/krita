/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): controller that turns a KisMainWindow into
// the canvas-first phone interface. Nothing of Krita is removed: menus, tool
// bars and dockers are parked or moved into sheets, and every action stays
// reachable through the command browser.
#ifndef MOBILE_SHELL_H
#define MOBILE_SHELL_H

#include "MobileChrome.h"

#include <QAction>

#include <QDockWidget>
#include <QEvent>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

class KisMainWindow;
class KisViewManager;
class QAction;
class QStatusBar;
class QToolBar;
class QWidget;

namespace mobileui {

class CommandBrowser;
class DialogFitter;
class Hub;
class MorePanel;
class QuickSliders;
class Sheet;
class ToolDrawer;
class ToolRail;
class TopBar;
class ValueBubble;

class Shell final : public QObject
{
    Q_OBJECT
public:
    explicit Shell(KisMainWindow *mainWindow);
    ~Shell() override;

    static Shell *of(QObject *mainWindow);

    bool isActive() const { return m_active; }

    void showHub();
    void hideHub();
    void openPanel(const QString &panelId, const QString &tabId = QString());
    void closePanel();

    // Everything below is used by the automated test run and the inventory.
    Sheet *sheet() const;
    Hub *hub() const;
    CommandBrowser *commandBrowser() const;
    KisMainWindow *mainWindow() const;
    QStringList panelIds() const;
    QAction *findAction(const QString &name) const { return action(name); }
    QStringList hostedDockIds() const;
    QString panelOfDock(const QString &dockId) const;
    void dumpInventory(const QString &path);

public Q_SLOTS:
    void activate();
    void deactivate();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct HostedDock {
        QPointer<QDockWidget> dock;
        QString panelId;
        QString tabId;
        QDockWidget::DockWidgetFeatures features;
        Qt::DockWidgetArea area = Qt::RightDockWidgetArea;
        bool titleBarWasVisible = true;
        // Wide utility title bars (the timeline's) get wrapped into a
        // horizontally scrollable strip while hosted.
        QPointer<QWidget> titleWrapper;
        QPointer<QWidget> originalTitle;
    };

    struct ParkedToolBar {
        QPointer<QToolBar> toolBar;
        Qt::ToolBarArea area;
        bool visible;
    };

    // Popup widgets of Krita's popup buttons (the brush editor, for example)
    // that are shown in a sheet tab instead of a floating popup.
    struct HostedPopup {
        QPointer<QWidget> widget;
        QPointer<QWidget> frame;
        QString panelId;
        QString tabId;
    };

    void createChrome();
    void destroyChrome();
    void adoptDocks();
    void releaseDocks();
    static void wrapTitleBar(HostedDock &hd);
    static void unwrapTitleBar(HostedDock &hd);
    void adoptPopups();
    void releasePopups();
    void adoptStatusBar();
    void releaseStatusBar();
    void parkToolBars();
    void unparkToolBars();
    void buildMorePanel();
    void connectKrita();
    void reapplyTheme();
    void updateLayout();
    void updateOverlays();
    void updateTitle();
    void updateColors();
    void updateQuickSliderTargets();
    void updatePanelButtons();
    void updateViewChrome();
    void setInterfaceHidden(bool hidden);
    void scheduleDockVisibilityCheck();
    void checkDockVisibility();
    void reclaimLater(QWidget *widget);
    void showMessage(const QString &message);
    void activateTool(const QString &toolId);
    void eyedropperClicked(bool stay);
    void handleBack(QEvent::Type type);

    QAction *action(const QString &name) const;
    QVector<ToolEntry> collectTools() const;
    QList<QAction *> allActions() const;
    bool isLeftHanded() const;
    bool isLandscape() const;
    bool isWide() const;

    QPointer<KisMainWindow> m_mainWindow;
    bool m_active = false;
    bool m_internalChange = false;
    bool m_interfaceHidden = false;
    bool m_dockCheckPending = false;
    bool m_updatingOverlays = false;
    bool m_themeRefreshPending = false;

    QPointer<QToolBar> m_topHolder;
    QPointer<QToolBar> m_railHolder;
    QPointer<TopBar> m_topBar;
    QPointer<ToolRail> m_rail;
    QPointer<Sheet> m_sheet;
    QPointer<QuickSliders> m_sliders;
    QPointer<ValueBubble> m_bubble;
    QPointer<QWidget> m_restoreButton;
    QPointer<QWidget> m_parking;
    QPointer<CommandBrowser> m_commands;
    QPointer<ToolDrawer> m_drawer;
    QPointer<MorePanel> m_more;
    QPointer<Hub> m_hub;
    QPointer<DialogFitter> m_dialogFitter;
    QPointer<QStatusBar> m_statusBar;
    QPointer<QWidget> m_statusPage;

    Qt::ToolBarArea m_railArea = Qt::BottomToolBarArea;
    QVector<HostedDock> m_docks;
    QVector<ParkedToolBar> m_toolBars;
    QVector<HostedPopup> m_popups;
    QVector<ToolEntry> m_tools;
    QHash<QString, QString> m_lastTabs;
    QString m_activeTool;
    QString m_toolBeforeEyedropper;
    bool m_eyedropperOneShot = false;
    QList<QMetaObject::Connection> m_kritaConnections;
};

} // namespace mobileui

#endif
