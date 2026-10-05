/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): writes the runtime action inventory: every
// action, docker and tool of the running Krita with its location in the phone
// interface. Compared against mobilefork/inventory/actions.csv (extracted
// from the sources) it proves that nothing became unreachable.
#include "MobileCommandBrowser.h"
#include "MobileShell.h"
#include "MobileTheme.h"

#include <KisMainWindow.h>
#include <KoToolManager.h>

#include <klocalizedstring.h>

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QSet>
#include <QTextStream>

namespace mobileui {

namespace {
QString csv(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}
} // namespace

void Shell::dumpInventory(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "kind,id,text,location,reachable\n";

    QSet<QString> listed;
    if (m_commands) {
        for (const CommandBrowser::Entry &entry : m_commands->allEntries()) {
            if (!entry.action) {
                continue;
            }
            const QString id = entry.action->objectName();
            const QString location = entry.inMenu ? QStringLiteral("Menu › %1").arg(entry.path)
                                                  : QStringLiteral("Menu › All commands › %1").arg(entry.path);
            out << "action," << csv(id) << ',' << csv(stripMnemonic(entry.action->text())) << ',' << csv(location) << ",yes\n";
            listed.insert(id);
        }
    }
    // Actions that exist but are not listed (no text, or deliberately hidden).
    for (QAction *action : allActions()) {
        if (!action || action->objectName().isEmpty() || listed.contains(action->objectName())) {
            continue;
        }
        listed.insert(action->objectName());
        out << "action," << csv(action->objectName()) << ',' << csv(stripMnemonic(action->text())) << ','
            << csv(action->text().isEmpty() ? QStringLiteral("(no text: internal or widget action)") : QStringLiteral("(not listed)"))
            << ",no\n";
    }
    for (const HostedDock &hd : qAsConst(m_docks)) {
        if (hd.dock) {
            out << "docker," << csv(hd.dock->objectName()) << ',' << csv(stripMnemonic(hd.dock->windowTitle())) << ','
                << csv(QStringLiteral("Sheet %1 / tab %2").arg(hd.panelId, hd.tabId)) << ",yes\n";
        }
    }
    for (const HostedPopup &hp : qAsConst(m_popups)) {
        if (hp.widget) {
            out << "popup," << csv(QString::fromLatin1(hp.widget->metaObject()->className())) << ','
                << csv(stripMnemonic(hp.widget->windowTitle())) << ','
                << csv(QStringLiteral("Sheet %1 / tab %2").arg(hp.panelId, hp.tabId)) << ",yes\n";
        }
    }
    for (const ToolEntry &tool : qAsConst(m_tools)) {
        out << "tool," << csv(tool.id) << ',' << csv(tool.name) << ',' << csv(QStringLiteral("Tool drawer (and tool rail)")) << ",yes\n";
    }
}

} // namespace mobileui
