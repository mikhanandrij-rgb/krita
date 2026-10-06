/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): simple startup and memory numbers, shown in
// More > Performance info and written by the screenshot run, so phones and CI
// can be compared without extra tools.
#ifndef MOBILE_PERF_H
#define MOBILE_PERF_H

#include <QString>

namespace mobileui {
namespace perf {

// Milliseconds since the process started; -1 when unknown.
qint64 processUptimeMs();
// Resident and peak resident memory of the process in KiB; -1 when unknown.
qint64 residentKb();
qint64 peakResidentKb();

// Remembers when the phone interface became ready / the first canvas
// appeared (only the first call counts).
void markInterfaceReady();
void markFirstCanvas();
qint64 interfaceReadyMs();
qint64 firstCanvasMs();

// One line per value, for the user and for logs.
QString report();

} // namespace perf
} // namespace mobileui

#endif
