/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileConfig.h"

#include <KConfigGroup>
#include <KSharedConfig>

#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#include <QtAndroid>
#endif

#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QVariant>
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
    const QByteArray env = testSetting("KRITA_MOBILE_UI").toLatin1();
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
    const QByteArray env = testSetting("KRITA_MOBILE_UI_SCALE").toLatin1();
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

QString androidTestDirectory()
{
#ifdef Q_OS_ANDROID
    static const QString dir = []() -> QString {
        QAndroidJniObject context = QtAndroid::androidContext();
        if (!context.isValid()) {
            return QString();
        }
        QAndroidJniObject file = context.callObjectMethod("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", nullptr);
        if (!file.isValid()) {
            return QString();
        }
        const QString path = file.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
        return path.isEmpty() ? QString() : path + QStringLiteral("/krita-mobile-test");
    }();
    return dir;
#else
    return QString();
#endif
}

QString testSetting(const char *name)
{
    const QByteArray env = qgetenv(name);
    if (!env.isEmpty()) {
        return QString::fromLocal8Bit(env);
    }
#ifdef Q_OS_ANDROID
    const QString dir = androidTestDirectory();
    if (!dir.isEmpty()) {
        QFile file(dir + QLatin1Char('/') + QString::fromLatin1(name));
        if (file.open(QIODevice::ReadOnly)) {
            return QString::fromUtf8(file.readAll()).trimmed();
        }
    }
#endif
    return QString();
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

QStringList applyPhoneDefaults()
{
    QStringList written;
#ifndef Q_OS_ANDROID
    // Desktop builds only do this for testing.
    if (qgetenv("KRITA_MOBILE_PHONE_DEFAULTS") != "1") {
        return written;
    }
#endif
    constexpr int version = 1;
    KConfigGroup own = group();
    if (own.readEntry("PhoneDefaultsVersion", 0) >= version) {
        return written;
    }
    // Krita keeps these in the root group of kritarc.
    KConfigGroup krita = KSharedConfig::openConfig()->group(QString());
    struct Default {
        const char *key;
        QVariant value;
    };
    const Default defaults[] = {
        // Krita's default of 50% of the RAM for image tiles is a lot for a
        // phone, where Android kills apps under memory pressure; above this
        // Krita swaps tiles to its own swap file instead.
        {"memoryHardLimitPercent", 35.0},
        // Every animation rendering clone holds a full copy of the image.
        {"frameRenderingClones", 1},
        // Cached frames for playback are scaled down above this size; the
        // frames themselves and exported renders keep full resolution.
        {"animationCacheFrameSizeLimit", 1920},
    };
    QStringList applied = own.readEntry("PhoneDefaultsApplied", QStringList());
    for (const Default &d : defaults) {
        const QString key = QString::fromLatin1(d.key);
        if (!krita.hasKey(key)) {
            krita.writeEntry(key, d.value);
            const QString entry = key + QLatin1Char('=') + d.value.toString();
            written.append(entry);
            applied.append(entry);
        }
    }
    own.writeEntry("PhoneDefaultsApplied", applied);
    own.writeEntry("PhoneDefaultsVersion", version);
    own.sync();
    krita.sync();
    return written;
}

QStringList appliedPhoneDefaults()
{
    return group().readEntry("PhoneDefaultsApplied", QStringList());
}

} // namespace config
} // namespace mobileui
