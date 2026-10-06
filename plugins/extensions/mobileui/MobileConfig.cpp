/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileConfig.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

namespace mobileui {
namespace config {

namespace {
KConfigGroup group()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("MobileUi"));
}

QScreen *screenFor(const QWidget *window)
{
    if (window && window->windowHandle() && window->windowHandle()->screen()) {
        return window->windowHandle()->screen();
    }
    return QGuiApplication::primaryScreen();
}
} // namespace

InterfaceMode interfaceMode()
{
    const QString mode = group().readEntry("InterfaceMode", QStringLiteral("auto"));
    if (mode == QLatin1String("phone")) {
        return InterfaceMode::Phone;
    } else if (mode == QLatin1String("classic")) {
        return InterfaceMode::Classic;
    }
    return InterfaceMode::Automatic;
}

void setInterfaceMode(InterfaceMode mode)
{
    QString value;
    switch (mode) {
    case InterfaceMode::Phone:
        value = QStringLiteral("phone");
        break;
    case InterfaceMode::Classic:
        value = QStringLiteral("classic");
        break;
    default:
        value = QStringLiteral("auto");
        break;
    }
    KConfigGroup g = group();
    g.writeEntry("InterfaceMode", value);
    g.sync();
}

int smallestScreenSideDp(const QWidget *window)
{
    QScreen *screen = screenFor(window);
    if (!screen) {
        return 10000;
    }
    // Qt's logical pixels follow Android's density-independent pixels at the
    // default interface scale. Krita's own interface scale setting changes
    // the device pixel ratio, so compute dp from physical pixels instead.
    const QSize logical = screen->size();
    const qreal dpr = screen->devicePixelRatio();
    const qreal physicalDpi = screen->physicalDotsPerInch();
#ifdef Q_OS_ANDROID
    if (physicalDpi > 1.0) {
        const qreal pxPerDp = physicalDpi / 160.0;
        return qRound(qMin(logical.width(), logical.height()) * dpr / pxPerDp);
    }
#endif
    Q_UNUSED(physicalDpi);
    Q_UNUSED(dpr);
    return qMin(logical.width(), logical.height());
}

bool shouldUsePhoneInterface(const QWidget *window)
{
    const QByteArray env = qgetenv("KRITA_MOBILE_UI");
    if (env == "phone") {
        return true;
    } else if (env == "classic") {
        return false;
    }
    switch (interfaceMode()) {
    case InterfaceMode::Phone:
        return true;
    case InterfaceMode::Classic:
        return false;
    default:
        break;
    }
#ifdef Q_OS_ANDROID
    // Phones have a smallest width below 600dp, tablets above.
    return smallestScreenSideDp(window) < 600;
#else
    Q_UNUSED(window);
    return false;
#endif
}

qreal uiScale()
{
    const QByteArray env = qgetenv("KRITA_MOBILE_UI_SCALE");
    if (!env.isEmpty()) {
        bool ok = false;
        const qreal value = env.toDouble(&ok);
        if (ok && value > 0.0) {
            return value;
        }
    }
    return qBound(0.75, group().readEntry("UiScale", 1.0), 1.5);
}

void setUiScale(qreal scale)
{
    KConfigGroup g = group();
    g.writeEntry("UiScale", qBound(0.75, scale, 1.5));
}

bool animationsEnabled()
{
    return group().readEntry("Animations", true);
}

void setAnimationsEnabled(bool enabled)
{
    KConfigGroup g = group();
    g.writeEntry("Animations", enabled);
}

bool leftHanded()
{
    return group().readEntry("LeftHanded", false);
}

void setLeftHanded(bool leftHanded)
{
    KConfigGroup g = group();
    g.writeEntry("LeftHanded", leftHanded);
}

QStringList toolSlots()
{
    return group().readEntry("ToolSlots", QStringList());
}

void setToolSlots(const QStringList &slotNames)
{
    KConfigGroup g = group();
    g.writeEntry("ToolSlots", slotNames);
}

qreal quickSizeMaximum()
{
    return qBound(10.0, group().readEntry("QuickSizeMaximum", 300.0), 10000.0);
}

void setQuickSizeMaximum(qreal maximum)
{
    KConfigGroup g = group();
    g.writeEntry("QuickSizeMaximum", qBound(10.0, maximum, 10000.0));
}

bool firstRunDone()
{
    return group().readEntry("FirstRunDone", false);
}

void setFirstRunDone(bool done)
{
    KConfigGroup g = group();
    g.writeEntry("FirstRunDone", done);
}

} // namespace config
} // namespace mobileui
