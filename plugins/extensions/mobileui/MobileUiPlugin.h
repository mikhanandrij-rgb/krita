/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): view plugin that attaches the phone
// interface to every main window. Loading it as a regular Krita plugin keeps
// the fork's footprint in upstream files at a single CMake line.
#ifndef MOBILE_UI_PLUGIN_H
#define MOBILE_UI_PLUGIN_H

#include <KisActionPlugin.h>

#include <QVariant>

class KisAction;

namespace mobileui {

class MobileUiPlugin : public KisActionPlugin
{
    Q_OBJECT
public:
    MobileUiPlugin(QObject *parent, const QVariantList &);
    ~MobileUiPlugin() override;

private:
    void attach();
    void applyMode();
    void plugSettingsMenu();

    KisAction *m_autoAction;
    KisAction *m_phoneAction;
    KisAction *m_classicAction;
};

} // namespace mobileui

#endif
