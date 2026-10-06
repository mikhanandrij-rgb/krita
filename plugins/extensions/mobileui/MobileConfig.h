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

// Android's Configuration.smallestScreenWidthDp, -1 elsewhere or on error.
int androidSmallestScreenWidthDp();

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

// Settings for automated test runs: the environment variable, or on Android
// (where an app has no environment) a file of that name in the app's
// external files directory, written with "adb push":
//   /sdcard/Android/data/org.krita.mobilefork/files/krita-mobile-test/<NAME>
// containing the value. Empty when not set.
QString testSetting(const char *name);
// The directory for test output on Android (inside the folder above).
QString androidTestDirectory();

bool firstRunDone();
void setFirstRunDone(bool done);

// Phone-friendly defaults for Krita's own memory and animation cache
// settings, written once on a phone and only for settings the user has never
// changed. None of them changes what painting produces; all of them stay
// adjustable in Settings > Configure Krita > Performance. Returns the keys
// that were written now (empty when nothing changed).
QStringList applyPhoneDefaults();
// Android: Krita's "interface scale" question at every start is turned off
// while the phone interface is used; the setting stays in More.
void moveScaleQuestionToSettings();
// The settings written by applyPhoneDefaults() over all runs, "key=value".
QStringList appliedPhoneDefaults();

} // namespace config
} // namespace mobileui

#endif
