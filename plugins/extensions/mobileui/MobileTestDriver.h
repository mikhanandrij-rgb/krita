/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): automated walk through the phone interface
// for CI. With KRITA_MOBILE_SCREENSHOTS=<directory> set, it takes screenshots
// of every screen in portrait and landscape, writes the action inventory and
// quits. Without the variable it does nothing.
#ifndef MOBILE_TEST_DRIVER_H
#define MOBILE_TEST_DRIVER_H

namespace mobileui {

class Shell;

void startTestDriverIfRequested(Shell *shell);

} // namespace mobileui

#endif
