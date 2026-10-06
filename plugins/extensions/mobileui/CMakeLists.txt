# SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Krita Mobile (unofficial fork): touch-first phone interface. Everything of
# the phone interface lives in this directory; upstream files only get a
# single add_subdirectory() line.

# The fork's strings use their own translation catalog (po/<lang>/kritamobileui.po),
# so upstream translation updates never conflict with them.
remove_definitions(-DTRANSLATION_DOMAIN=\"krita\")
add_definitions(-DTRANSLATION_DOMAIN=\"kritamobileui\")

set(kritamobileui_SOURCES
    MobileUiPlugin.cpp
    MobileConfig.cpp
    MobileTheme.cpp
    MobileWidgets.cpp
    MobileSheet.cpp
    MobileChrome.cpp
    MobilePanels.cpp
    MobileCommandBrowser.cpp
    MobileHub.cpp
    MobileShell.cpp
    MobileInventory.cpp
    MobileTestDriver.cpp
    MobileDialogFitter.cpp
)

qt_add_resources(kritamobileui_SOURCES mobileui.qrc)

kis_add_library(kritamobileui MODULE ${kritamobileui_SOURCES})
target_link_libraries(kritamobileui kritaui KF${KF_MAJOR}::WidgetsAddons Qt${QT_MAJOR_VERSION}::Svg Qt${QT_MAJOR_VERSION}::Concurrent)
install(TARGETS kritamobileui DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
