/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileUiPlugin.h"
#include "MobileConfig.h"
#include "MobilePerf.h"
#include "MobileShell.h"
#include "MobileTestDriver.h"

#include <KisMainWindow.h>
#include <KisViewManager.h>
#include <kis_action.h>
#include <kpluginfactory.h>

#include <klocalizedstring.h>

#include <QActionGroup>
#include <QDebug>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

K_PLUGIN_FACTORY_WITH_JSON(MobileUiPluginFactory, "kritamobileui.json", registerPlugin<mobileui::MobileUiPlugin>();)

namespace mobileui {

MobileUiPlugin::MobileUiPlugin(QObject *parent, const QVariantList &)
    : KisActionPlugin(parent)
{
    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);

    auto makeAction = [this, group](const QString &name, const QString &text, InterfaceMode mode) {
        KisAction *action = new KisAction(text, this);
        action->setCheckable(true);
        action->setChecked(config::interfaceMode() == mode);
        action->setActivationFlags(KisAction::NONE);
        group->addAction(action);
        addAction(name, action);
        connect(action, &QAction::triggered, this, [this, mode] {
            config::setInterfaceMode(mode);
            applyMode();
        });
        return action;
    };
    m_autoAction = makeAction(QStringLiteral("mobileui_mode_auto"),
                              i18n("Phone interface: automatic (phones only)"),
                              InterfaceMode::Automatic);
    m_phoneAction = makeAction(QStringLiteral("mobileui_mode_phone"), i18n("Phone interface: always"), InterfaceMode::Phone);
    m_classicAction = makeAction(QStringLiteral("mobileui_mode_classic"),
                                 i18n("Phone interface: off (classic Krita)"),
                                 InterfaceMode::Classic);

    // The main window is still being constructed when view plugins load:
    // dockers, menus and tool bars don't exist yet.
    QTimer::singleShot(0, this, &MobileUiPlugin::attach);
}

MobileUiPlugin::~MobileUiPlugin()
{
}

void MobileUiPlugin::attach()
{
    KisViewManager *vm = viewManager();
    KisMainWindow *mw = vm ? vm->mainWindow() : nullptr;
    if (!mw) {
        return;
    }
    if (!Shell::of(mw)) {
        new Shell(mw);
        // Startup measurement for both interfaces (More > Performance info).
        connect(mw, &KisMainWindow::activeViewChanged, this, [mw] {
            if (mw->activeView()) {
                QTimer::singleShot(0, mw, [] {
                    perf::markFirstCanvas();
                });
            }
        });
    }
    plugSettingsMenu();
    applyMode();
    startTestDriverIfRequested(Shell::of(mw));
}

void MobileUiPlugin::applyMode()
{
    KisViewManager *vm = viewManager();
    KisMainWindow *mw = vm ? vm->mainWindow() : nullptr;
    Shell *shell = Shell::of(mw);
    if (!shell) {
        return;
    }
    const InterfaceMode mode = config::interfaceMode();
    m_autoAction->setChecked(mode == InterfaceMode::Automatic);
    m_phoneAction->setChecked(mode == InterfaceMode::Phone);
    m_classicAction->setChecked(mode == InterfaceMode::Classic);
    if (config::shouldUsePhoneInterface(mw)) {
        const QStringList written = config::applyPhoneDefaults();
        if (!written.isEmpty()) {
            qInfo().noquote() << "Krita Mobile: phone defaults written (effective after restart):" << written.join(QStringLiteral(", "));
        }
        shell->activate();
    } else {
        shell->deactivate();
        // Same measuring point as the phone interface, for comparisons.
        QTimer::singleShot(0, this, [] {
            perf::markInterfaceReady();
        });
    }
}

void MobileUiPlugin::plugSettingsMenu()
{
    // A "Phone interface" submenu in Krita's Settings menu, so the mode can
    // be switched from the classic interface too.
    KisViewManager *vm = viewManager();
    KisMainWindow *mw = vm ? vm->mainWindow() : nullptr;
    if (!mw || mw->findChild<QMenu *>(QStringLiteral("mobileUiModeMenu"))) {
        return;
    }
    QMenu *settings = nullptr;
    for (QAction *top : mw->menuBar()->actions()) {
        if (top->menu() && top->menu()->objectName() == QLatin1String("settings")) {
            settings = top->menu();
            break;
        }
    }
    if (!settings) {
        return;
    }
    QMenu *menu = new QMenu(i18n("Phone interface"), settings);
    menu->setObjectName(QStringLiteral("mobileUiModeMenu"));
    menu->addAction(m_autoAction);
    menu->addAction(m_phoneAction);
    menu->addAction(m_classicAction);
    QAction *first = settings->actions().value(0);
    settings->insertMenu(first, menu);
    settings->insertSeparator(first);
}

} // namespace mobileui

#include "MobileUiPlugin.moc"
