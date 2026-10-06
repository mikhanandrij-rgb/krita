/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Structure ported from the Drawpile Mobile fork (same author,
 * GPL-3.0-or-later) and adapted to Krita's main window, dockers and tools.
 */
#include "MobileShell.h"
#include "MobileCommandBrowser.h"
#include "MobileConfig.h"
#include "MobileHub.h"
#include "MobilePanels.h"
#include "MobileSheet.h"
#include "MobileTheme.h"
#include "MobileWidgets.h"

#include <KisDocument.h>
#include <KoDocumentInfo.h>
#include <KisMainWindow.h>
#include <KisView.h>
#include <KisViewManager.h>
#include <KoColor.h>
#include <KoToolManager.h>
#include <kis_action_manager.h>
#include <kis_canvas_controller.h>
#include <kis_canvas_resource_provider.h>
#include <kis_config.h>
#include <kis_config_notifier.h>
#include <kis_icon_utils.h>
#include <kis_paintop_box.h>
#include <kis_slider_spin_box.h>
#include <widgets/kis_widget_chooser.h>
#include <kactioncollection.h>
#include <kxmlguifactory.h>

#include <klocalizedstring.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QFileInfo>
#include <QResizeEvent>
#include <QScrollArea>
#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QLayout>
#include <QMdiArea>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace mobileui {

namespace {

struct DockPlacement {
    const char *dockId;
    const char *panelId;
    const char *tabId;
    const char *iconName;
};

// Where each of Krita's dockers goes. Dockers that aren't listed here
// (including ones added upstream in the future) get a panel of their own that
// is listed in the "more" panel, so they never become unreachable.
constexpr DockPlacement DOCK_PLACEMENTS[] = {
    {"PresetDocker", "brush", "presets", "brush"},
    {"sharedtooldocker", "brush", "tool", "sliders"},
    {"PresetHistory", "brush", "history", "recover"},
    {"PatternDocker", "brush", "patterns", "square"},
    {"ColorSelectorNg", "color", "advanced", "triangle-wheel"},
    {"WideGamutColorSelector", "color", "widegamut", "wheel"},
    {"PaletteDocker", "color", "palette", "palette"},
    {"SpecificColorSelector", "color", "specific", "sliders"},
    {"ArtisticColorSelector", "color", "artistic", "rings"},
    {"DigitalMixer", "color", "mixer", "mixer"},
    {"SmallColorSelector", "color", "small", "light"},
    {"GamutMask", "color", "gamut", "gamut"},
    {"KisLayerBox", "layers", "layers", "layers"},
    {"ChannelDocker", "layers", "channels", "channels"},
    {"CompositionDocker", "layers", "compositions", "compositions"},
    {"TimelineDocker", "animation", "timeline", "timeline"},
    {"OnionSkinsDocker", "animation", "onion", "onion"},
    {"AnimationCurvesDocker", "animation", "curves", "curves"},
    {"StoryboardDocker", "animation", "storyboard", "film"},
    {"OverviewDocker", "view", "overview", "navigator"},
    {"History", "view", "history", "undo"},
    {"Snapshot", "view", "snapshots", "snapshot"},
    {"GridDocker", "view", "grid", "grid"},
    {"HistogramDocker", "view", "histogram", "histogram"},
    {"TextProperties", "text", "text", "text"},
    {"SvgSymbolCollectionDocker", "text", "symbols", "symbols"},
    {"ArrangeDocker", "text", "arrange", "align"},
};

// Which panel wins when Krita shows several dockers at once.
int panelPriority(const QString &panelId)
{
    static const QStringList order = {
        QStringLiteral("layers"),
        QStringLiteral("brush"),
        QStringLiteral("animation"),
        QStringLiteral("color"),
        QStringLiteral("view"),
        QStringLiteral("text"),
    };
    const int i = order.indexOf(panelId);
    return i == -1 ? order.size() : i;
}

// Tools whose brush size and opacity are taken from the brush tool bar.
bool usesBrushSize(const QString &toolId)
{
    static const QStringList tools = {
        QStringLiteral("KritaShape/KisToolBrush"),
        QStringLiteral("KritaShape/KisToolDyna"),
        QStringLiteral("KritaShape/KisToolMultiBrush"),
        QStringLiteral("KritaShape/KisToolLine"),
        QStringLiteral("KritaShape/KisToolRectangle"),
        QStringLiteral("KritaShape/KisToolEllipse"),
        QStringLiteral("KisToolPolygon"),
        QStringLiteral("KisToolPolyline"),
        QStringLiteral("KisToolPath"),
        QStringLiteral("KisToolPencil"),
    };
    return tools.contains(toolId);
}

bool usesOpacity(const QString &toolId)
{
    return usesBrushSize(toolId) || toolId == QLatin1String("KritaFill/KisToolFill")
        || toolId == QLatin1String("KritaFill/KisToolGradient") || toolId == QLatin1String("KisToolEncloseAndFill");
}

const QString SAMPLER_TOOL = QStringLiteral("KritaSelected/KisToolColorSampler");

// QToolBar shows an overflow button when its content is squeezed; the phone
// bars lay themselves out to fit, so it would only show up as a glitch.
void hideToolBarExtension(QToolBar *toolBar)
{
    for (QToolButton *button : toolBar->findChildren<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"))) {
        button->setFixedSize(0, 0);
        button->hide();
    }
}

} // namespace

Shell::Shell(KisMainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    setObjectName(QStringLiteral("mobileUiShell"));
}

Shell::~Shell()
{
    if (m_active) {
        qApp->removeEventFilter(this);
    }
}

Shell *Shell::of(QObject *mainWindow)
{
    return mainWindow ? mainWindow->findChild<Shell *>(QStringLiteral("mobileUiShell"), Qt::FindDirectChildrenOnly) : nullptr;
}

Sheet *Shell::sheet() const
{
    return m_sheet;
}

Hub *Shell::hub() const
{
    return m_hub;
}

CommandBrowser *Shell::commandBrowser() const
{
    return m_commands;
}

KisMainWindow *Shell::mainWindow() const
{
    return m_mainWindow;
}

QStringList Shell::panelIds() const
{
    QStringList ids = {QStringLiteral("tools"), QStringLiteral("menu"), QStringLiteral("more")};
    for (const HostedDock &hd : m_docks) {
        if (!ids.contains(hd.panelId)) {
            ids.append(hd.panelId);
        }
    }
    for (const HostedPopup &hp : m_popups) {
        if (!ids.contains(hp.panelId)) {
            ids.append(hp.panelId);
        }
    }
    return ids;
}

QStringList Shell::hostedDockIds() const
{
    QStringList ids;
    for (const HostedDock &hd : m_docks) {
        if (hd.dock) {
            ids.append(hd.dock->objectName());
        }
    }
    return ids;
}

QString Shell::panelOfDock(const QString &dockId) const
{
    for (const HostedDock &hd : m_docks) {
        if (hd.dock && hd.dock->objectName() == dockId) {
            return QStringLiteral("%1/%2").arg(hd.panelId, hd.tabId);
        }
    }
    return QString();
}

void Shell::showHub()
{
    if (!m_active || !m_mainWindow) {
        return;
    }
    if (!m_hub) {
        m_hub = new Hub(m_mainWindow, m_mainWindow);
        m_hub->setStyleSheet(chromeStyleSheet());
        connect(m_hub, &Hub::closeRequested, this, [this] {
            hideHub();
            if (m_hub && m_hub->property("openAnimationAfterCreate").toBool()) {
                m_hub->setProperty("openAnimationAfterCreate", false);
                openPanel(QStringLiteral("animation"), QStringLiteral("timeline"));
            }
        });
    }
    if (m_sheet) {
        m_sheet->close();
    }
    m_hub->setGeometry(m_mainWindow->rect());
    m_hub->showMainPage();
    m_hub->raise();
    m_hub->show();
}

void Shell::hideHub()
{
    if (m_hub) {
        m_hub->hide();
    }
}

void Shell::openPanel(const QString &panelId, const QString &tabId)
{
    if (m_sheet) {
        m_sheet->open(panelId, tabId.isEmpty() ? m_lastTabs.value(panelId) : tabId);
    }
}

void Shell::closePanel()
{
    if (m_sheet) {
        m_sheet->close();
    }
}

// ---------------------------------------------------------------------------
// Event handling

void Shell::handleBack(QEvent::Type type)
{
    const bool release = type == QEvent::KeyRelease;
    if (m_hub && m_hub->isVisible()) {
        if (!m_hub->handleBack(release) && release && m_mainWindow) {
            // Nothing open: leave Krita like any other Android app.
            QAction *quit = action(QStringLiteral("file_quit"));
            if (quit) {
                quit->trigger();
            }
        }
        return;
    }
    if (release) {
        if (m_sheet && m_sheet->isOpen()) {
            m_sheet->close();
        } else if (m_interfaceHidden) {
            setInterfaceHidden(false);
        } else {
            showHub();
        }
    }
}

bool Shell::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // Android back button.
    if ((type == QEvent::KeyPress || type == QEvent::KeyRelease) && m_active && m_mainWindow) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Back && !QApplication::activeModalWidget() && !QApplication::activePopupWidget()
            && (QApplication::activeWindow() == m_mainWindow || !QApplication::activeWindow())) {
            handleBack(type);
            return true;
        }
        return false;
    }

    if (!m_active || !m_mainWindow) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == m_mainWindow) {
        switch (type) {
        case QEvent::Resize:
            updateLayout();
            if (m_hub && m_hub->isVisible()) {
                m_hub->setGeometry(m_mainWindow->rect());
            }
            break;
        case QEvent::WindowTitleChange:
        case QEvent::ModifiedChange:
            updateTitle();
            break;
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            if (!m_themeRefreshPending) {
                m_themeRefreshPending = true;
                QTimer::singleShot(0, this, &Shell::reapplyTheme);
            }
            break;
        default:
            break;
        }
        return false;
    }

    if (watched == m_mainWindow->centralWidget()) {
        if (type == QEvent::Resize || type == QEvent::Move) {
            updateOverlays();
        }
        return false;
    }

    // Krita shows the menu bar and the status bar again in various places
    // (welcome page, canvas-only mode, configuration changes). Keep them
    // hidden while the phone interface is active.
    if ((watched == m_mainWindow->menuBar() || watched == m_statusBar.data()) && type == QEvent::Show && !m_internalChange) {
        QPointer<QWidget> w = qobject_cast<QWidget *>(watched);
        QTimer::singleShot(0, this, [this, w] {
            if (m_active && w) {
                QScopedValueRollback<bool> rollback(m_internalChange, true);
                w->hide();
            }
        });
        return false;
    }

    // A popup of one of Krita's popup buttons whose content is shown in a
    // sheet tab: open the tab instead of the (empty) floating popup.
    if (type == QEvent::Show) {
        for (const HostedPopup &hp : qAsConst(m_popups)) {
            if (hp.frame == watched) {
                const QString panelId = hp.panelId;
                const QString tabId = hp.tabId;
                QPointer<QWidget> frame = hp.frame;
                QTimer::singleShot(0, this, [this, frame, panelId, tabId] {
                    if (frame) {
                        frame->hide();
                    }
                    openPanel(panelId, tabId);
                });
                return false;
            }
        }
    }

    // Something in Krita moved one of the hosted dockers away (restoring a
    // workspace, for example). Take it back.
    if (type == QEvent::ParentChange && !m_internalChange) {
        if (QWidget *widget = qobject_cast<QWidget *>(watched)) {
            reclaimLater(widget);
        }
        return false;
    }

    if ((type == QEvent::ShowToParent || type == QEvent::HideToParent) && !m_internalChange) {
        if (QWidget *widget = qobject_cast<QWidget *>(watched)) {
            for (const HostedDock &hd : qAsConst(m_docks)) {
                if (hd.dock == widget) {
                    widget->setProperty("mobileVisibilityRequest",
                                        type == QEvent::ShowToParent ? QStringLiteral("show") : QStringLiteral("hide"));
                    scheduleDockVisibilityCheck();
                    break;
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Activation

void Shell::activate()
{
    if (m_active || !m_mainWindow) {
        return;
    }
    m_active = true;
    KisMainWindow *mw = m_mainWindow;
    const bool updatesWereEnabled = mw->updatesEnabled();
    mw->setUpdatesEnabled(false);
    Theme::refresh();

    m_parking = new QWidget(mw);
    m_parking->setObjectName(QStringLiteral("mobileParking"));
    m_parking->hide();
    m_parking->setGeometry(0, 0, 0, 0);

    {
        QScopedValueRollback<bool> rollback(m_internalChange, true);
        mw->menuBar()->hide();
        m_statusBar = mw->statusBar();
        if (m_statusBar) {
            m_statusBar->hide();
        }
    }
    mw->menuBar()->installEventFilter(this);
    if (m_statusBar) {
        m_statusBar->installEventFilter(this);
    }

    m_tools = collectTools();
    parkToolBars();
    createChrome();
    adoptDocks();
    adoptPopups();
    buildMorePanel();
    connectKrita();

    mw->installEventFilter(this);
    if (mw->centralWidget()) {
        mw->centralWidget()->installEventFilter(this);
    }
    qApp->installEventFilter(this);

    updateLayout();
    updateTitle();
    updateColors();
    updateQuickSliderTargets();
    updateViewChrome();
    mw->setUpdatesEnabled(updatesWereEnabled);

    if (!mw->activeView()) {
        showHub();
    }
}

void Shell::deactivate()
{
    if (!m_active || !m_mainWindow) {
        return;
    }
    KisMainWindow *mw = m_mainWindow;
    qApp->removeEventFilter(this);
    mw->removeEventFilter(this);
    if (mw->centralWidget()) {
        mw->centralWidget()->removeEventFilter(this);
    }
    mw->menuBar()->removeEventFilter(this);
    if (m_statusBar) {
        m_statusBar->removeEventFilter(this);
    }
    for (const QMetaObject::Connection &c : qAsConst(m_kritaConnections)) {
        disconnect(c);
    }
    m_kritaConnections.clear();

    if (m_sheet) {
        m_sheet->setAnimationsEnabled(false);
        m_sheet->close();
    }
    if (m_commands) {
        m_commands->leave();
    }
    hideHub();

    releasePopups();
    releaseDocks();
    unparkToolBars();
    destroyChrome();
    {
        QScopedValueRollback<bool> rollback(m_internalChange, true);
        mw->menuBar()->show();
        if (m_statusBar) {
            m_statusBar->setVisible(KisConfig(true).showStatusBar());
        }
    }
    delete m_parking;
    m_interfaceHidden = false;
    m_active = false;
    updateViewChrome();
}

QVector<ToolEntry> Shell::collectTools() const
{
    QVector<ToolEntry> tools;
    for (KoToolAction *toolAction : KoToolManager::instance()->toolActionList()) {
        ToolEntry entry;
        entry.id = toolAction->id();
        QAction *qaction = action(entry.id);
        entry.name = qaction ? stripMnemonic(qaction->text()) : QString();
        if (entry.name.isEmpty()) {
            entry.name = toolAction->iconText().isEmpty() ? toolAction->toolTip() : toolAction->iconText();
        }
        entry.icon = KisIconUtils::loadIcon(toolAction->iconName());
        entry.section = toolAction->section();
        entry.priority = toolAction->priority();
        tools.append(entry);
    }
    return tools;
}

void Shell::activateTool(const QString &toolId)
{
    for (KoToolAction *toolAction : KoToolManager::instance()->toolActionList()) {
        if (toolAction->id() == toolId) {
            toolAction->trigger();
            return;
        }
    }
}

void Shell::createChrome()
{
    KisMainWindow *mw = m_mainWindow;
    const QString styleSheet = chromeStyleSheet();

    m_topBar = new TopBar(action(QStringLiteral("edit_undo")), action(QStringLiteral("edit_redo")));
    m_topHolder = new QToolBar(mw);
    m_topHolder->setObjectName(QStringLiteral("mobileTopBarHolder"));
    m_topHolder->setMovable(false);
    m_topHolder->setFloatable(false);
    m_topHolder->setContextMenuPolicy(Qt::PreventContextMenu);
    m_topHolder->toggleViewAction()->setVisible(false);
    m_topHolder->setStyleSheet(styleSheet);
    m_topHolder->layout()->setContentsMargins(0, 0, 0, 0);
    m_topHolder->layout()->setSpacing(0);
    m_topHolder->addWidget(m_topBar);
    hideToolBarExtension(m_topHolder);
    mw->addToolBar(Qt::TopToolBarArea, m_topHolder);

    m_rail = new ToolRail(m_tools);
    m_railHolder = new QToolBar(mw);
    m_railHolder->setObjectName(QStringLiteral("mobileToolRailHolder"));
    m_railHolder->setMovable(false);
    m_railHolder->setFloatable(false);
    m_railHolder->setContextMenuPolicy(Qt::PreventContextMenu);
    m_railHolder->toggleViewAction()->setVisible(false);
    m_railHolder->setStyleSheet(styleSheet);
    m_railHolder->layout()->setContentsMargins(0, 0, 0, 0);
    m_railHolder->layout()->setSpacing(0);
    m_railHolder->addWidget(m_rail);
    hideToolBarExtension(m_railHolder);
    m_railArea = Qt::BottomToolBarArea;
    mw->addToolBar(m_railArea, m_railHolder);

    m_sliders = new QuickSliders(mw);
    m_sliders->setStyleSheet(styleSheet);
    m_bubble = new ValueBubble(mw);
    m_bubble->setStyleSheet(styleSheet);
    connect(m_sliders, &QuickSliders::dragStateChanged, this, [this](bool dragging, const QString &label) {
        if (!m_bubble || !m_sliders) {
            return;
        }
        m_bubble->setText(label);
        m_bubble->setVisible(dragging);
        if (dragging) {
            const QRect s = m_sliders->geometry();
            const int x = isLeftHanded() ? s.left() - m_bubble->width() - dp(8) : s.right() + dp(8);
            m_bubble->move(x, s.center().y() - m_bubble->height() / 2);
            m_bubble->raise();
        }
    });
    connect(m_sliders, &QuickSliders::eyedropperClicked, this, [this] {
        eyedropperClicked(false);
    });
    connect(m_sliders, &QuickSliders::eyedropperLongPressed, this, [this] {
        eyedropperClicked(true);
    });

    ChromeButton *restore = new ChromeButton(QStringLiteral("fullscreen"), i18n("Show interface"), mw);
    restore->setStyleSheet(styleSheet);
    restore->setAutoFillBackground(false);
    restore->setFloating(true);
    restore->hide();
    connect(restore, &ChromeButton::clicked, this, [this] {
        setInterfaceHidden(false);
    });
    m_restoreButton = restore;

    m_sheet = new Sheet(mw);
    m_sheet->setStyleSheet(styleSheet);
    m_sheet->setAnimationsEnabled(config::animationsEnabled());

    // Panels that always exist; docker tabs get added by adoptDocks().
    m_sheet->addPanel(QStringLiteral("brush"), i18n("Brushes and tool options"));
    m_sheet->addPanel(QStringLiteral("color"), i18n("Color"));
    m_sheet->addPanel(QStringLiteral("layers"), i18n("Layers"));
    m_sheet->addPanel(QStringLiteral("animation"), i18n("Animation"));
    m_sheet->addPanel(QStringLiteral("view"), i18n("Navigation and history"));
    m_sheet->addPanel(QStringLiteral("text"), i18n("Text and vector"));

    m_drawer = new ToolDrawer(m_tools);
    m_sheet->addTab(QStringLiteral("tools"), QStringLiteral("tools"), i18n("Tools"), QIcon(), m_drawer, true);
    m_sheet->setPanelTitle(QStringLiteral("tools"), i18n("Tools"));
    connect(m_drawer, &ToolDrawer::toolChosen, this, [this](const QString &toolId) {
        activateTool(toolId);
        if (m_sheet) {
            m_sheet->close();
        }
    });
    connect(m_drawer, &ToolDrawer::toolSettingsRequested, this, [this](const QString &toolId) {
        activateTool(toolId);
        openPanel(QStringLiteral("brush"), QStringLiteral("tool"));
    });

    m_commands = new CommandBrowser(mw->menuBar());
    m_commands->setActionSource([this] {
        return allActions();
    });
    QVector<QAction *> quick;
    for (const char *name : {"file_new", "file_open", "file_save", "file_save_as", "file_export_file", "render_animation",
                             "edit_copy", "edit_paste", "select_all", "deselect", "options_configure"}) {
        if (QAction *a = action(QString::fromLatin1(name))) {
            quick.append(a);
        }
    }
    m_commands->setQuickActions(quick);
    m_sheet->addTab(QStringLiteral("menu"), QStringLiteral("menu"), i18n("Menu"), QIcon(), m_commands, false);
    m_sheet->setPanelTitle(QStringLiteral("menu"), i18n("Menu"));
    connect(m_commands, &CommandBrowser::closeRequested, m_sheet, &Sheet::close);

    m_more = new MorePanel;
    m_sheet->addTab(QStringLiteral("more"), QStringLiteral("more"), i18n("More"), QIcon(), m_more, true);
    m_sheet->setPanelTitle(QStringLiteral("more"), i18n("More"));

    connect(m_sheet, &Sheet::opened, this, [this](const QString &panelId) {
        if (panelId == QLatin1String("menu") && m_commands) {
            m_commands->reset();
        }
        updatePanelButtons();
    });
    connect(m_sheet, &Sheet::closed, this, [this](const QString &panelId) {
        if (panelId == QLatin1String("menu") && m_commands) {
            m_commands->leave();
        }
        updatePanelButtons();
        // Give the keyboard focus back to the canvas.
        if (m_mainWindow && m_mainWindow->activeView()) {
            m_mainWindow->activeView()->setFocus();
        }
    });
    connect(m_sheet, &Sheet::tabChanged, this, [this](const QString &panelId, const QString &tabId) {
        m_lastTabs.insert(panelId, tabId);
    });
    connect(m_sheet, &Sheet::coveredRectChanged, this, &Shell::updateOverlays);

    // Top bar.
    auto togglePanel = [this](const QString &panelId) {
        return [this, panelId] {
            if (m_sheet) {
                m_sheet->toggle(panelId, m_sheet->isOpen() ? QString() : m_lastTabs.value(panelId));
            }
        };
    };
    connect(m_topBar, &TopBar::projectsRequested, this, &Shell::showHub);
    connect(m_topBar, &TopBar::menuRequested, this, togglePanel(QStringLiteral("menu")));
    connect(m_topBar, &TopBar::moreRequested, this, togglePanel(QStringLiteral("more")));
    connect(m_topBar, &TopBar::layersRequested, this, togglePanel(QStringLiteral("layers")));
    connect(m_topBar, &TopBar::brushesRequested, this, togglePanel(QStringLiteral("brush")));
    connect(m_topBar, &TopBar::animationRequested, this, togglePanel(QStringLiteral("animation")));
    connect(m_topBar, &TopBar::colorRequested, this, togglePanel(QStringLiteral("color")));

    // Tool rail.
    connect(m_rail, &ToolRail::toolRequested, this, &Shell::activateTool);
    connect(m_rail, &ToolRail::drawerRequested, this, togglePanel(QStringLiteral("tools")));
    connect(m_rail, &ToolRail::layersRequested, this, togglePanel(QStringLiteral("layers")));
    connect(m_rail, &ToolRail::brushesRequested, this, togglePanel(QStringLiteral("brush")));
    connect(m_rail, &ToolRail::animationRequested, this, togglePanel(QStringLiteral("animation")));
    connect(m_rail, &ToolRail::colorRequested, this, togglePanel(QStringLiteral("color")));
    connect(m_rail, &ToolRail::toolSettingsRequested, this, [this] {
        if (m_sheet) {
            m_sheet->toggle(QStringLiteral("brush"), QStringLiteral("tool"));
        }
    });
    auto swapColors = [this] {
        if (QAction *swap = action(QStringLiteral("toggle_fg_bg"))) {
            swap->trigger();
        }
    };
    connect(m_rail, &ToolRail::swapColorsRequested, this, swapColors);
    connect(m_topBar, &TopBar::swapColorsRequested, this, swapColors);
}

void Shell::destroyChrome()
{
    KisMainWindow *mw = m_mainWindow;
    if (m_topHolder) {
        mw->removeToolBar(m_topHolder);
        m_topHolder->deleteLater();
    }
    if (m_railHolder) {
        mw->removeToolBar(m_railHolder);
        m_railHolder->deleteLater();
    }
    for (QWidget *w : {static_cast<QWidget *>(m_sliders), static_cast<QWidget *>(m_bubble), m_restoreButton.data(),
                       static_cast<QWidget *>(m_sheet), static_cast<QWidget *>(m_hub)}) {
        if (w) {
            w->deleteLater();
        }
    }
}

// ---------------------------------------------------------------------------
// Dockers

void Shell::adoptDocks()
{
    KisMainWindow *mw = m_mainWindow;
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    const QList<QDockWidget *> dockList = mw->dockWidgets();

    auto host = [this, mw](QDockWidget *dock, const QString &panelId, const QString &tabId, const QIcon &tabIcon) {
        m_docks.append(HostedDock());
        HostedDock &hd = m_docks.last();
        hd.dock = dock;
        hd.panelId = panelId;
        hd.tabId = tabId;
        hd.features = dock->features();
        hd.area = mw->dockWidgetArea(dock);
        hd.titleBarWasVisible = dock->titleBarWidget() ? dock->titleBarWidget()->isVisibleTo(dock) : true;
        mw->removeDockWidget(dock);
        dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        if (QWidget *title = dock->titleBarWidget()) {
            if (title->inherits("KisUtilityTitleBar")) {
                // Timeline, curves and similar dockers keep their controls
                // in the title bar. Keep it, scrollable when it's too wide.
                wrapTitleBar(hd);
            } else {
                // Plain title bar: the sheet shows the title already.
                title->hide();
            }
        }
        if (!m_sheet->hasPanel(panelId)) {
            m_sheet->addPanel(panelId, dock->windowTitle());
        }
        m_sheet->addTab(panelId, tabId, stripMnemonic(dock->windowTitle()), tabIcon, dock, true);
        dock->show();
        dock->installEventFilter(this);
    };

    for (const DockPlacement &placement : DOCK_PLACEMENTS) {
        for (QDockWidget *dock : dockList) {
            if (dock && dock->objectName() == QLatin1String(placement.dockId)) {
                host(dock,
                     QString::fromLatin1(placement.panelId),
                     QString::fromLatin1(placement.tabId),
                     mobileui::icon(QString::fromLatin1(placement.iconName)));
            }
        }
    }
    for (QDockWidget *dock : dockList) {
        if (!dock) {
            continue;
        }
        bool known = false;
        for (const HostedDock &hd : qAsConst(m_docks)) {
            if (hd.dock == dock) {
                known = true;
                break;
            }
        }
        if (!known) {
            const QString panelId = QStringLiteral("other:%1").arg(dock->objectName());
            m_sheet->addPanel(panelId, stripMnemonic(dock->windowTitle()));
            host(dock, panelId, dock->objectName(), dock->windowIcon());
        }
    }

    // Finger-sized rows in the layer list. Krita derives the row height from
    // the thumbnail size and font, so a larger font gives taller rows.
    for (const HostedDock &hd : qAsConst(m_docks)) {
        if (hd.panelId == QLatin1String("layers") && hd.dock) {
            for (QAbstractItemView *view : hd.dock->findChildren<QAbstractItemView *>()) {
                view->setProperty("mobileOriginalFont", view->font());
                QFont font = view->font();
                font.setPixelSize(dp(16));
                view->setFont(font);
            }
        }
    }
    for (const QString &panelId : {QStringLiteral("layers"), QStringLiteral("animation")}) {
        for (const HostedDock &hd : qAsConst(m_docks)) {
            if (hd.panelId == panelId && hd.dock && hd.tabId == panelId) {
                m_sheet->setPanelTitle(panelId, stripMnemonic(hd.dock->windowTitle()));
            }
        }
    }
}

void Shell::wrapTitleBar(HostedDock &hd)
{
    QDockWidget *dock = hd.dock;
    QWidget *title = dock ? dock->titleBarWidget() : nullptr;
    if (!title || hd.titleWrapper) {
        return;
    }
    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("mobileTitleScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    scroll->setFixedHeight(qMax(title->sizeHint().height(), title->minimumSizeHint().height()));
    enableKineticScrolling(scroll);
    // Replacing the title bar widget hides the old one without deleting it.
    dock->setTitleBarWidget(scroll);
    scroll->setWidget(title);
    title->show();
    hd.titleWrapper = scroll;
    hd.originalTitle = title;
}

void Shell::unwrapTitleBar(HostedDock &hd)
{
    QDockWidget *dock = hd.dock;
    QScrollArea *scroll = qobject_cast<QScrollArea *>(hd.titleWrapper.data());
    if (dock && scroll && dock->titleBarWidget() == scroll) {
        QWidget *title = scroll->takeWidget();
        dock->setTitleBarWidget(title);
        if (title) {
            title->show();
        }
        scroll->deleteLater();
    }
    hd.titleWrapper = nullptr;
    hd.originalTitle = nullptr;
}

void Shell::releaseDocks()
{
    KisMainWindow *mw = m_mainWindow;
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    for (HostedDock &hd : m_docks) {
        if (QDockWidget *dock = hd.dock) {
            dock->removeEventFilter(this);
            for (QAbstractItemView *view : dock->findChildren<QAbstractItemView *>()) {
                const QVariant original = view->property("mobileOriginalFont");
                if (original.isValid()) {
                    view->setFont(original.value<QFont>());
                    view->setProperty("mobileOriginalFont", QVariant());
                }
            }
            if (m_sheet) {
                m_sheet->takeTabContent(hd.panelId, hd.tabId);
            }
            unwrapTitleBar(hd);
            if (dock->titleBarWidget()) {
                dock->titleBarWidget()->setVisible(hd.titleBarWasVisible);
            }
            dock->setParent(mw);
            dock->setFeatures(hd.features);
            mw->addDockWidget(hd.area == Qt::NoDockWidgetArea ? Qt::RightDockWidgetArea : hd.area, dock);
            dock->hide();
        }
    }
    m_docks.clear();
}

void Shell::adoptPopups()
{
    KisMainWindow *mw = m_mainWindow;
    if (!mw) {
        return;
    }
    struct PopupPlacement {
        const char *className;
        const char *panelId;
        const char *tabId;
        const char *iconName;
    };
    // Popups of Krita's popup buttons that hold whole panels.
    static const PopupPlacement placements[] = {
        {"KisPaintOpPresetsEditor", "brush", "editor", "settings"},
        {"KisToolOptionsPopup", "brush", "tool", "sliders"},
    };
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    for (QWidget *frame : mw->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!frame->isWindow() || !frame->layout() || frame->layout()->count() != 1) {
            continue;
        }
        QWidget *content = frame->layout()->itemAt(0)->widget();
        if (!content) {
            continue;
        }
        for (const PopupPlacement &p : placements) {
            if (content->inherits(p.className)) {
                const QString panelId = QString::fromLatin1(p.panelId);
                const QString tabId = QString::fromLatin1(p.tabId);
                // Krita's tool options docker takes precedence when present.
                bool taken = false;
                for (const HostedDock &hd : qAsConst(m_docks)) {
                    taken = taken || (hd.panelId == panelId && hd.tabId == tabId);
                }
                if (taken) {
                    continue;
                }
                frame->layout()->removeWidget(content);
                HostedPopup hp;
                hp.widget = content;
                hp.frame = frame;
                hp.panelId = panelId;
                hp.tabId = tabId;
                const QString title = content->windowTitle().isEmpty() ? frame->windowTitle() : content->windowTitle();
                m_sheet->addTab(panelId, tabId, stripMnemonic(title), mobileui::icon(QString::fromLatin1(p.iconName)), content, true);
                content->show();
                frame->installEventFilter(this);
                m_popups.append(hp);
                if (content->inherits("KisPaintOpPresetsEditor")) {
                    // The editor's preset strip and scratchpad sit side by
                    // side with the settings; on a phone the presets are in
                    // their own tab, so collapse both. They remain one tap
                    // away through the editor's own toggle buttons.
                    for (const char *name : {"showPresetsButton", "showScratchpadButton"}) {
                        QAbstractButton *toggle = content->findChild<QAbstractButton *>(QString::fromLatin1(name));
                        if (toggle && toggle->isCheckable() && toggle->isChecked()) {
                            toggle->click();
                        }
                    }
                }
            }
        }
    }
}

void Shell::releasePopups()
{
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    for (const HostedPopup &hp : qAsConst(m_popups)) {
        if (hp.frame) {
            hp.frame->removeEventFilter(this);
        }
        QWidget *content = m_sheet ? m_sheet->takeTabContent(hp.panelId, hp.tabId) : hp.widget.data();
        if (content && hp.frame && hp.frame->layout()) {
            content->setParent(hp.frame);
            hp.frame->layout()->addWidget(content);
        }
    }
    m_popups.clear();
}

void Shell::parkToolBars()
{
    KisMainWindow *mw = m_mainWindow;
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    for (QToolBar *toolBar : mw->findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (toolBar == m_topHolder || toolBar == m_railHolder) {
            continue;
        }
        ParkedToolBar parked;
        parked.toolBar = toolBar;
        parked.area = mw->toolBarArea(toolBar);
        parked.visible = !toolBar->isHidden();
        mw->removeToolBar(toolBar);
        toolBar->setParent(m_parking);
        toolBar->installEventFilter(this);
        m_toolBars.append(parked);
    }
}

void Shell::unparkToolBars()
{
    KisMainWindow *mw = m_mainWindow;
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    for (const ParkedToolBar &parked : qAsConst(m_toolBars)) {
        if (QToolBar *toolBar = parked.toolBar) {
            toolBar->removeEventFilter(this);
            toolBar->setParent(mw);
            mw->addToolBar(parked.area == Qt::NoToolBarArea ? Qt::TopToolBarArea : parked.area, toolBar);
            toolBar->setVisible(parked.visible);
        }
    }
    m_toolBars.clear();
}

// ---------------------------------------------------------------------------
// "More" panel

void Shell::buildMorePanel()
{
    CardSection *panels = m_more->addSection(i18n("Panels"));
    auto addPanelCard = [this, panels](const QString &iconName, const QString &title, const QString &panelId, const QString &tabId) {
        ActionCard *card = panels->addCard(iconName, title);
        connect(card, &ActionCard::clicked, this, [this, panelId, tabId] {
            openPanel(panelId, tabId);
        });
        return card;
    };
    addPanelCard(QStringLiteral("sliders"), i18n("Tool options"), QStringLiteral("brush"), QStringLiteral("tool"));
    addPanelCard(QStringLiteral("settings"), i18n("Brush editor"), QStringLiteral("brush"), QStringLiteral("editor"));
    addPanelCard(QStringLiteral("navigator"), i18n("Navigation and history"), QStringLiteral("view"), QString());
    addPanelCard(QStringLiteral("text"), i18n("Text and vector"), QStringLiteral("text"), QString());
    for (const HostedDock &hd : qAsConst(m_docks)) {
        if (hd.panelId.startsWith(QLatin1String("other:")) && hd.dock) {
            ActionCard *card = addPanelCard(QString(), stripMnemonic(hd.dock->windowTitle()), hd.panelId, hd.tabId);
            if (!hd.dock->windowIcon().isNull()) {
                card->setThemeIcon(hd.dock->windowIcon());
            } else {
                card->setThemeIcon(mobileui::icon(QStringLiteral("more")));
            }
        }
    }

    CardSection *view = m_more->addSection(i18n("View"));
    const char *viewActions[][2] = {
        {"zoom_to_100pct", ""},
        {"zoom_to_fit", ""},
        {"reset_canvas_rotation", ""},
        {"mirror_canvas", ""},
        {"view_toggle_reference_images", ""},
        {"view_show_canvas_only", ""},
    };
    for (const auto &va : viewActions) {
        if (QAction *a = action(QString::fromLatin1(va[0]))) {
            view->addActionCard(a, QString::fromLatin1(va[1]));
        }
    }
    ActionCard *hideUi = view->addCard(QStringLiteral("hide-ui"), i18n("Hide interface"));
    connect(hideUi, &ActionCard::clicked, this, [this] {
        if (m_sheet) {
            m_sheet->close();
        }
        setInterfaceHidden(true);
    });

    CardSection *touch = m_more->addSection(i18n("Finger and stylus"));
    struct TouchMode {
        KisConfig::TouchPainting mode;
        QString icon;
        QString title;
    };
    const QVector<TouchMode> modes = {
        {KisConfig::TOUCH_PAINTING_AUTO, QStringLiteral("brush"), i18n("Finger paints until a stylus is used")},
        {KisConfig::TOUCH_PAINTING_ENABLED, QStringLiteral("brush"), i18n("Finger always paints")},
        {KisConfig::TOUCH_PAINTING_DISABLED, QStringLiteral("eraser"), i18n("Stylus only, finger navigates")},
    };
    QVector<ActionCard *> modeCards;
    for (const TouchMode &m : modes) {
        ActionCard *card = touch->addCard(m.icon, m.title);
        card->setCheckable(true);
        modeCards.append(card);
    }
    auto syncModes = [modeCards, modes] {
        const KisConfig::TouchPainting current = KisConfig(true).touchPainting();
        for (int i = 0; i < modes.size(); ++i) {
            modeCards[i]->setChecked(modes[i].mode == current);
        }
    };
    syncModes();
    for (int i = 0; i < modes.size(); ++i) {
        const KisConfig::TouchPainting mode = modes[i].mode;
        connect(modeCards[i], &ActionCard::clicked, this, [mode, syncModes] {
            KisConfig(false).setTouchPainting(mode);
            KisConfigNotifier::instance()->notifyConfigChanged();
            syncModes();
        });
    }

    CardSection *ui = m_more->addSection(i18n("Interface"));
    ActionCard *lefty = ui->addCard(QStringLiteral("swap"), i18n("Left-handed layout"));
    lefty->setCheckable(true);
    lefty->setChecked(config::leftHanded());
    connect(lefty, &ActionCard::toggled, this, [this](bool checked) {
        config::setLeftHanded(checked);
        updateLayout();
    });
    ActionCard *smaller = ui->addCard(QStringLiteral("resize"), i18n("Smaller interface"));
    connect(smaller, &ActionCard::clicked, this, [this] {
        config::setUiScale(config::uiScale() - 0.1);
        showMessage(i18n("Restart Krita to apply the new interface size."));
    });
    ActionCard *larger = ui->addCard(QStringLiteral("resize"), i18n("Larger interface"));
    connect(larger, &ActionCard::clicked, this, [this] {
        config::setUiScale(config::uiScale() + 0.1);
        showMessage(i18n("Restart Krita to apply the new interface size."));
    });
    ActionCard *animations = ui->addCard(QStringLiteral("play"), i18n("Animations"));
    animations->setCheckable(true);
    animations->setChecked(config::animationsEnabled());
    connect(animations, &ActionCard::toggled, this, [this](bool checked) {
        config::setAnimationsEnabled(checked);
        if (m_sheet) {
            m_sheet->setAnimationsEnabled(checked);
        }
    });
    if (QAction *prefs = action(QStringLiteral("options_configure"))) {
        ui->addActionCard(prefs, QStringLiteral("settings"));
    }
    ActionCard *classic = ui->addCard(QStringLiteral("classic"), i18n("Classic Krita interface"));
    connect(classic, &ActionCard::clicked, this, [this] {
        QMessageBox box(QMessageBox::Question,
                        i18n("Classic interface"),
                        i18n("Switch to Krita's classic interface with menus and dockers? It is meant for tablets and large "
                             "screens. You can switch back in Settings > Phone interface."),
                        QMessageBox::Yes | QMessageBox::No,
                        m_mainWindow);
        if (box.exec() == QMessageBox::Yes) {
            config::setInterfaceMode(InterfaceMode::Classic);
            deactivate();
        }
    });

    m_more->addNote(i18n("Krita Mobile is an unofficial build of Krita with a phone interface. It is not made or supported by "
                         "the Krita project. Every Krita command is in the menu, including the ones that only have keyboard "
                         "shortcuts on the desktop."));
}

// ---------------------------------------------------------------------------
// Connections to Krita

void Shell::connectKrita()
{
    KisMainWindow *mw = m_mainWindow;
    KisViewManager *vm = mw->viewManager();

    m_kritaConnections << connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this] {
        m_activeTool = KoToolManager::instance()->activeToolId();
        if (m_rail) {
            m_rail->setActiveTool(m_activeTool);
        }
        if (m_drawer) {
            m_drawer->setActiveTool(m_activeTool);
        }
        if (m_sliders) {
            m_sliders->setEyedropperChecked(m_activeTool == SAMPLER_TOOL);
        }
        if (m_activeTool != SAMPLER_TOOL) {
            m_eyedropperOneShot = false;
        }
        updateQuickSliderTargets();
    });

    if (KisCanvasResourceProvider *provider = vm->canvasResourceProvider()) {
        m_kritaConnections << connect(provider, &KisCanvasResourceProvider::sigFGColorChanged, this, [this] {
            updateColors();
            // One-shot eyedropper: go back to the previous tool once sampling
            // has settled (the sampler updates the color while dragging).
            if (m_eyedropperOneShot && m_activeTool == SAMPLER_TOOL) {
                QTimer *timer = findChild<QTimer *>(QStringLiteral("mobileEyedropperTimer"));
                if (!timer) {
                    timer = new QTimer(this);
                    timer->setObjectName(QStringLiteral("mobileEyedropperTimer"));
                    timer->setSingleShot(true);
                    timer->setInterval(700);
                    connect(timer, &QTimer::timeout, this, [this] {
                        if (m_eyedropperOneShot && m_activeTool == SAMPLER_TOOL && !m_toolBeforeEyedropper.isEmpty()) {
                            m_eyedropperOneShot = false;
                            activateTool(m_toolBeforeEyedropper);
                        }
                    });
                }
                timer->start();
            }
        });
        m_kritaConnections << connect(provider, &KisCanvasResourceProvider::sigBGColorChanged, this, &Shell::updateColors);
    }

    m_kritaConnections << connect(mw, &KisMainWindow::activeViewChanged, this, [this] {
        updateTitle();
        updateViewChrome();
        // The tool is switched while the view is being set up; refresh the
        // quick sliders once everything is in place.
        m_activeTool = KoToolManager::instance()->activeToolId();
        updateQuickSliderTargets();
        QTimer::singleShot(300, this, [this] {
            m_activeTool = KoToolManager::instance()->activeToolId();
            if (m_rail) {
                m_rail->setActiveTool(m_activeTool);
            }
            updateQuickSliderTargets();
        });
        if (m_mainWindow && m_mainWindow->activeView() && m_hub && m_hub->isVisible()) {
            hideHub();
        } else if (m_mainWindow && !m_mainWindow->activeView()) {
            showHub();
        }
    });
    m_kritaConnections << connect(mw, &KisMainWindow::themeChanged, this, &Shell::reapplyTheme);
    m_kritaConnections << connect(KisConfigNotifier::instance(), &KisConfigNotifier::configChanged, this, [this] {
        // Krita re-applies tool bar and scroll bar settings on configuration
        // changes; assert the phone layout again afterwards.
        QTimer::singleShot(0, this, &Shell::updateViewChrome);
    });

    // Krita fixes the main window's size to the screen on Android. Follow the
    // screen when it rotates or the system bars change, so portrait and
    // landscape both fill the screen.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        m_kritaConnections << connect(screen, &QScreen::availableGeometryChanged, this, [this](const QRect &geometry) {
            if (m_mainWindow && m_mainWindow->minimumSize() == m_mainWindow->maximumSize() && geometry.isValid()) {
                m_mainWindow->setFixedSize(geometry.size());
            }
        });
    }

    m_activeTool = KoToolManager::instance()->activeToolId();
    if (m_rail) {
        m_rail->setActiveTool(m_activeTool);
    }
    if (m_drawer) {
        m_drawer->setActiveTool(m_activeTool);
    }
}

void Shell::eyedropperClicked(bool stay)
{
    if (m_activeTool == SAMPLER_TOOL) {
        if (!m_toolBeforeEyedropper.isEmpty() && !stay) {
            activateTool(m_toolBeforeEyedropper);
        }
        return;
    }
    m_toolBeforeEyedropper = m_activeTool;
    activateTool(SAMPLER_TOOL);
    m_eyedropperOneShot = !stay;
    if (stay) {
        showMessage(i18n("The color sampler stays active. Tap the eyedropper again to go back."));
    }
}

void Shell::reapplyTheme()
{
    m_themeRefreshPending = false;
    if (!m_active) {
        return;
    }
    Theme::refresh();
    const QString styleSheet = chromeStyleSheet();
    for (QWidget *w : {static_cast<QWidget *>(m_topHolder),
                       static_cast<QWidget *>(m_railHolder),
                       static_cast<QWidget *>(m_sliders),
                       static_cast<QWidget *>(m_bubble),
                       m_restoreButton.data(),
                       static_cast<QWidget *>(m_sheet),
                       static_cast<QWidget *>(m_hub)}) {
        if (w) {
            w->setStyleSheet(styleSheet);
            w->update();
            for (QWidget *child : w->findChildren<QWidget *>()) {
                if (child->property("mobileChrome").toBool()) {
                    child->update();
                }
            }
        }
    }
    if (m_sheet) {
        m_sheet->refreshTheme();
    }
}

void Shell::updateViewChrome()
{
    if (!m_mainWindow) {
        return;
    }
    // Document tabs: the top bar shows the current document; switching
    // documents is in the Window menu.
    if (QMdiArea *mdi = m_mainWindow->findChild<QMdiArea *>()) {
        for (QTabBar *tabBar : mdi->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (m_active) {
                if (!tabBar->property("mobileOriginalStyleSheet").isValid()) {
                    tabBar->setProperty("mobileOriginalStyleSheet", tabBar->styleSheet());
                }
                tabBar->setStyleSheet(QStringLiteral("QTabBar { max-height: 0px; } QTabBar::tab { height: 0px; max-height: 0px;"
                                                     " min-height: 0px; padding: 0px; margin: 0px; border: none; }"));
                tabBar->setVisible(false);
            } else if (tabBar->property("mobileOriginalStyleSheet").isValid()) {
                tabBar->setStyleSheet(tabBar->property("mobileOriginalStyleSheet").toString());
                tabBar->setProperty("mobileOriginalStyleSheet", QVariant());
                tabBar->setVisible(true);
            }
        }
        // Make QMdiArea recompute its viewport margins.
        QResizeEvent resize(mdi->size(), mdi->size());
        QCoreApplication::sendEvent(mdi, &resize);
    }
    // Gesture scrolling instead of tiny desktop scroll bars.
    if (KisView *view = m_mainWindow->activeView()) {
        if (KisCanvasController *controller = view->canvasController()) {
            if (m_active) {
                controller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                controller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            } else {
                const Qt::ScrollBarPolicy policy = KisConfig(true).hideScrollbars() ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
                controller->setHorizontalScrollBarPolicy(policy);
                controller->setVerticalScrollBarPolicy(policy);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Layout

void Shell::updateLayout()
{
    if (!m_active || !m_mainWindow || !m_rail) {
        return;
    }
    KisMainWindow *mw = m_mainWindow;
    const bool landscape = isLandscape();
    const bool lefty = isLeftHanded();

    const Qt::ToolBarArea railArea = landscape ? (lefty ? Qt::RightToolBarArea : Qt::LeftToolBarArea) : Qt::BottomToolBarArea;
    if (railArea != m_railArea) {
        m_railArea = railArea;
        mw->addToolBar(railArea, m_railHolder);
    }
    m_rail->setOrientation(landscape ? Qt::Vertical : Qt::Horizontal);
    m_rail->setLeftHanded(lefty);
    m_rail->setShowPanelButtons(!landscape);
    m_topBar->setShowPanelButtons(landscape);

    const int button = dp(48);
    int slotCount;
    if (landscape) {
        const int length = mw->height() - m_topBar->sizeHint().height() - dp(8);
        slotCount = (length - button) / dp(48);
    } else {
        // Drawer button plus four panel buttons and the separator.
        const int length = mw->width() - dp(8);
        slotCount = (length - button * 5 - dp(9)) / dp(48);
    }
    m_rail->setSlotCount(qBound(2, slotCount, 8));

    m_railHolder->setVisible(!m_interfaceHidden);
    m_topHolder->setVisible(!m_interfaceHidden);
    updatePanelButtons();
    updateOverlays();
}

void Shell::updateOverlays()
{
    if (!m_active || !m_mainWindow || !m_sheet || m_updatingOverlays) {
        return;
    }
    QScopedValueRollback<bool> rollback(m_updatingOverlays, true);
    QWidget *central = m_mainWindow->centralWidget();
    if (!central) {
        return;
    }
    const QRect area = central->geometry();
    const bool lefty = isLeftHanded();
    const bool side = isLandscape() || isWide();
    m_sheet->setArea(area, side ? Sheet::Placement::Side : Sheet::Placement::Bottom, lefty);

    // Quick sliders along the edge, kept clear of the sheet.
    QRect free = area;
    const QRect covered = m_sheet->coveredRect();
    if (!covered.isEmpty() && m_sheet->placement() == Sheet::Placement::Bottom) {
        free.setBottom(qMin(free.bottom(), covered.top() - 1));
    }
    const bool hubVisible = m_hub && m_hub->isVisible();
    bool showSliders = !m_interfaceHidden && !hubVisible && m_sliders->hasTargets() && free.height() > dp(150)
        && m_mainWindow->activeView();
    if (showSliders && m_sheet->placement() == Sheet::Placement::Side && !covered.isEmpty()) {
        // A side sheet on the same side as the sliders hides them.
        const bool sheetLeft = covered.center().x() < area.center().x();
        const bool slidersLeft = !lefty;
        if (sheetLeft == slidersLeft) {
            showSliders = false;
        }
    }
    if (showSliders) {
        m_sliders->fitHeight(qMin(free.height() - dp(24), dp(460)));
        const QSize s = m_sliders->sizeHint();
        const int x = lefty ? free.right() - s.width() - dp(6) : free.left() + dp(6);
        const int y = free.top() + (free.height() - s.height()) / 2;
        m_sliders->setGeometry(x, y, s.width(), s.height());
        m_sliders->show();
        m_sliders->raise();
    } else {
        m_sliders->hide();
    }

    if (m_restoreButton) {
        const QSize s = m_restoreButton->sizeHint();
        m_restoreButton->setGeometry(lefty ? area.left() + dp(8) : area.right() - s.width() - dp(8), area.top() + dp(8), s.width(), s.height());
        m_restoreButton->setVisible(m_interfaceHidden);
        m_restoreButton->raise();
    }

    // Stacking: canvas < sliders < sheet < bars < hub.
    if (m_sheet->isVisible()) {
        m_sheet->raise();
    }
    if (m_topHolder) {
        m_topHolder->raise();
    }
    if (m_railHolder) {
        m_railHolder->raise();
    }
    if (hubVisible) {
        m_hub->raise();
    }
}

void Shell::updateTitle()
{
    if (!m_topBar || !m_mainWindow) {
        return;
    }
    KisView *view = m_mainWindow->activeView();
    KisDocument *doc = view ? view->document() : nullptr;
    if (!doc) {
        m_topBar->setTitle(QStringLiteral("Krita"), false);
        return;
    }
    QString title;
    if (!doc->path().isEmpty()) {
        title = QFileInfo(doc->path()).completeBaseName();
    }
    if (title.isEmpty() && doc->documentInfo()) {
        title = doc->documentInfo()->aboutInfo(QStringLiteral("title"));
    }
    if (title.isEmpty()) {
        title = i18n("Untitled");
    }
    m_topBar->setTitle(title, doc->isModified());
    // Keep the dirty marker up to date.
    if (!doc->property("mobileUiTitleConnected").toBool()) {
        doc->setProperty("mobileUiTitleConnected", true);
        connect(doc, &KisDocument::modified, this, &Shell::updateTitle);
    }
}

void Shell::updateColors()
{
    if (!m_mainWindow) {
        return;
    }
    KisCanvasResourceProvider *provider = m_mainWindow->viewManager()->canvasResourceProvider();
    if (!provider) {
        return;
    }
    const QColor fg = provider->fgColor().toQColor();
    const QColor bg = provider->bgColor().toQColor();
    if (m_rail) {
        m_rail->setColors(fg, bg);
    }
    if (m_topBar) {
        m_topBar->setColors(fg, bg);
    }
}

void Shell::updateQuickSliderTargets()
{
    if (!m_sliders || !m_mainWindow) {
        return;
    }
    KisDoubleSliderSpinBox *size = nullptr;
    KisDoubleSliderSpinBox *opacity = nullptr;
    if (KisPaintopBox *box = m_mainWindow->viewManager()->paintOpBox()) {
        const QList<KisWidgetChooser *> choosers = box->findChildren<KisWidgetChooser *>();
        if (!choosers.isEmpty()) {
            KisWidgetChooser *chooser = choosers.first();
            if (usesBrushSize(m_activeTool)) {
                size = chooser->getWidget<KisDoubleSliderSpinBox>(QStringLiteral("size"));
            }
            if (usesOpacity(m_activeTool)) {
                opacity = chooser->getWidget<KisDoubleSliderSpinBox>(QStringLiteral("opacity"));
            }
        }
    }
    m_sliders->setTargets(size, opacity);
    updateOverlays();
}

void Shell::updatePanelButtons()
{
    const QString panel = m_sheet ? m_sheet->currentPanel() : QString();
    if (m_rail) {
        m_rail->setPanelChecked(panel);
    }
    if (m_topBar) {
        m_topBar->setPanelChecked(panel);
        m_topBar->setMenuChecked(panel == QLatin1String("menu"));
        m_topBar->setMoreChecked(panel == QLatin1String("more"));
    }
}

void Shell::setInterfaceHidden(bool hidden)
{
    m_interfaceHidden = hidden;
    updateLayout();
}

void Shell::scheduleDockVisibilityCheck()
{
    if (!m_dockCheckPending) {
        m_dockCheckPending = true;
        QTimer::singleShot(0, this, &Shell::checkDockVisibility);
    }
}

void Shell::checkDockVisibility()
{
    m_dockCheckPending = false;
    if (!m_active || !m_sheet) {
        return;
    }

    const HostedDock *bestShow = nullptr;
    int showRequests = 0;
    bool currentHidden = false;
    for (const HostedDock &hd : qAsConst(m_docks)) {
        if (!hd.dock) {
            continue;
        }
        const QString request = hd.dock->property("mobileVisibilityRequest").toString();
        hd.dock->setProperty("mobileVisibilityRequest", QVariant());
        const bool shown = !hd.dock->isHidden();
        if (request == QLatin1String("show") && shown) {
            ++showRequests;
            if (!bestShow || panelPriority(hd.panelId) < panelPriority(bestShow->panelId)) {
                bestShow = &hd;
            }
        } else if (request == QLatin1String("hide") && !shown) {
            if (m_sheet->isOpen() && m_sheet->currentPanel() == hd.panelId && m_sheet->currentTab() == hd.tabId) {
                currentHidden = true;
            }
        }
    }

    // Krita shows and hides all dockers at once when switching between the
    // welcome page and documents or restoring workspaces. That is not a
    // request to look at a docker, so only single requests open the sheet.
    if (bestShow && showRequests <= 2) {
        m_sheet->open(bestShow->panelId, bestShow->tabId);
    } else if (currentHidden && showRequests == 0) {
        m_sheet->close();
    }

    // Hosted dockers always stay "shown" inside their tab; whether they are
    // on screen is decided by the sheet.
    QScopedValueRollback<bool> rollback(m_internalChange, true);
    for (const HostedDock &hd : qAsConst(m_docks)) {
        if (hd.dock && hd.dock->isHidden()) {
            hd.dock->show();
        }
    }
}

void Shell::reclaimLater(QWidget *widget)
{
    QPointer<QWidget> guarded = widget;
    QTimer::singleShot(0, this, [this, guarded] {
        if (!guarded || !m_active || !m_mainWindow || !m_sheet) {
            return;
        }
        QWidget *w = guarded;
        QScopedValueRollback<bool> rollback(m_internalChange, true);
        for (const HostedDock &hd : qAsConst(m_docks)) {
            if (hd.dock == w) {
                if (w->parentWidget() == m_mainWindow) {
                    m_mainWindow->removeDockWidget(hd.dock);
                    hd.dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
                }
                m_sheet->reattachTabContent(hd.panelId, hd.tabId);
                w->show();
                return;
            }
        }
        for (const ParkedToolBar &parked : qAsConst(m_toolBars)) {
            if (parked.toolBar == w && w->parentWidget() != m_parking) {
                m_mainWindow->removeToolBar(parked.toolBar);
                w->setParent(m_parking);
                return;
            }
        }
    });
}

void Shell::showMessage(const QString &message)
{
    if (m_mainWindow && m_mainWindow->viewManager()) {
        m_mainWindow->viewManager()->showFloatingMessage(message, QIcon());
    }
}

QAction *Shell::action(const QString &name) const
{
    if (!m_mainWindow) {
        return nullptr;
    }
    if (KisViewManager *vm = m_mainWindow->viewManager()) {
        if (QAction *a = vm->actionCollection()->action(name)) {
            return a;
        }
    }
    if (QAction *a = m_mainWindow->actionCollection()->action(name)) {
        return a;
    }
    return m_mainWindow->findChild<QAction *>(name);
}

QList<QAction *> Shell::allActions() const
{
    QList<QAction *> result;
    if (!m_mainWindow) {
        return result;
    }
    for (KisKXMLGUIClient *client : m_mainWindow->guiFactory()->clients()) {
        if (client && client->actionCollection()) {
            result += client->actionCollection()->actions();
        }
    }
    if (KisViewManager *vm = m_mainWindow->viewManager()) {
        result += vm->actionCollection()->actions();
    }
    // Docker toggles, so a docker can always be brought up by name.
    for (QDockWidget *dock : m_mainWindow->dockWidgets()) {
        if (dock) {
            result.append(dock->toggleViewAction());
        }
    }
    return result;
}

bool Shell::isLeftHanded() const
{
    return config::leftHanded();
}

bool Shell::isLandscape() const
{
    return m_mainWindow && m_mainWindow->width() > m_mainWindow->height();
}

bool Shell::isWide() const
{
    return m_mainWindow && qMin(m_mainWindow->width(), m_mainWindow->height()) >= dp(600);
}

} // namespace mobileui
