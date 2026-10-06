/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): settings of the phone interface. They live in
// their own "MobileUi" group of kritarc, so they never interfere with Krita's
// own settings.
#ifndef MOBILE_CONFIG_H
#define MOBILE_CONFIG_H

#include <QString>
#include <QStringList>

class QWidget;

namespace mobileui {

enum class InterfaceMode {
    Automatic, // phone interface on small screens, classic otherwise
    Phone,     // always the phone interface
    Classic,   // always Krita's normal interface
};

namespace config {

InterfaceMode interfaceMode();
void setInterfaceMode(InterfaceMode mode);

// Whether the phone interface should be used for a window right now. Takes
// the interface mode, the screen size and the KRITA_MOBILE_UI environment
// variable ("phone" / "classic", for testing) into account.
bool shouldUsePhoneInterface(const QWidget *window);

// Smallest screen side in density-independent pixels.
int smallestScreenSideDp(const QWidget *window);

qreal uiScale();
void setUiScale(qreal scale);

bool animationsEnabled();
void setAnimationsEnabled(bool enabled);

bool leftHanded();
void setLeftHanded(bool leftHanded);

QStringList toolSlots();
void setToolSlots(const QStringList &slotNames);

// Soft maximum of the quick size slider in pixels.
qreal quickSizeMaximum();
void setQuickSizeMaximum(qreal maximum);

bool firstRunDone();
void setFirstRunDone(bool done);

} // namespace config
} // namespace mobileui

#endif
