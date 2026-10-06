/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobilePerf.h"

#include <klocalizedstring.h>

#include <QFile>
#include <QStringList>
#include <QTextStream>

#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <unistd.h>
#endif

namespace mobileui {
namespace perf {

namespace {

qint64 s_interfaceReady = -1;
qint64 s_firstCanvas = -1;

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

qint64 statusValueKb(const QByteArray &key)
{
    const QList<QByteArray> lines = readFile(QStringLiteral("/proc/self/status")).split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith(key)) {
            const QList<QByteArray> parts = line.mid(key.size()).simplified().split(' ');
            bool ok = false;
            const qint64 value = parts.value(0).toLongLong(&ok);
            return ok ? value : -1;
        }
    }
    return -1;
}

QString formatMs(qint64 ms)
{
    return ms < 0 ? i18n("unknown") : i18n("%1 s", QString::number(ms / 1000.0, 'f', 2));
}

QString formatKb(qint64 kb)
{
    return kb < 0 ? i18n("unknown") : i18n("%1 MB", QString::number(kb / 1024.0, 'f', 0));
}

} // namespace

qint64 processUptimeMs()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // Field 22 of /proc/self/stat is the start time in clock ticks after
    // boot; /proc/uptime has the seconds since boot.
    const QByteArray stat = readFile(QStringLiteral("/proc/self/stat"));
    const int close = stat.lastIndexOf(')');
    if (close < 0) {
        return -1;
    }
    const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
    // After the command name the fields start at number 3 (state).
    bool ok = false;
    const qint64 startTicks = fields.value(22 - 3).toLongLong(&ok);
    const long ticks = sysconf(_SC_CLK_TCK);
    const double uptime = readFile(QStringLiteral("/proc/uptime")).split(' ').value(0).toDouble();
    if (!ok || ticks <= 0 || uptime <= 0) {
        return -1;
    }
    return qint64((uptime - double(startTicks) / double(ticks)) * 1000.0);
#else
    return -1;
#endif
}

qint64 residentKb()
{
    return statusValueKb("VmRSS:");
}

qint64 peakResidentKb()
{
    return statusValueKb("VmHWM:");
}

void markInterfaceReady()
{
    if (s_interfaceReady < 0) {
        s_interfaceReady = processUptimeMs();
    }
}

void markFirstCanvas()
{
    if (s_firstCanvas < 0) {
        s_firstCanvas = processUptimeMs();
    }
}

qint64 interfaceReadyMs()
{
    return s_interfaceReady;
}

qint64 firstCanvasMs()
{
    return s_firstCanvas;
}

QString report()
{
    QStringList lines;
    lines << i18n("Start to phone interface: %1", formatMs(s_interfaceReady));
    lines << i18n("Start to first canvas: %1", formatMs(s_firstCanvas));
    lines << i18n("Memory in use: %1", formatKb(residentKb()));
    lines << i18n("Peak memory: %1", formatKb(peakResidentKb()));
    lines << i18n("Running for: %1", formatMs(processUptimeMs()));
    return lines.join(QLatin1Char('\n'));
}

} // namespace perf
} // namespace mobileui
