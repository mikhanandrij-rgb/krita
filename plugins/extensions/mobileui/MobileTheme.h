/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): design tokens of the phone interface. The
// look follows the Drawpile Mobile fork so both apps feel the same.
#ifndef MOBILE_THEME_H
#define MOBILE_THEME_H

#include <QColor>
#include <QIcon>
#include <QString>

class QAbstractScrollArea;
class QWidget;

namespace mobileui {

// Color tokens. They follow Krita's current color theme: dark themes get the
// dark tokens, bright themes the light ones, so there is no separate setting.
struct Theme {
    bool dark = true;
    QColor background; // behind everything (hub, canvas surround)
    QColor surface;    // bars, sheets
    QColor surface2;   // cards, inputs
    QColor surface3;   // pressed
    QColor outline;    // hairlines
    QColor text;
    QColor textDim;
    QColor accent;
    QColor accentText; // text on accent
    QColor accentSoft; // selected backgrounds
    QColor danger;
    QColor shadow;

    static const Theme &current();
    static void refresh();
};

// Density-independent pixels. On Android one Qt logical pixel is one dp at
// Krita's default interface scale; the phone interface has its own scale on
// top that the user can change.
int dp(qreal value);

// Touch target size, 48dp per Material guidelines.
inline int touchTarget() { return dp(48); }

inline int radiusLarge() { return dp(20); }
inline int radiusMedium() { return dp(14); }
inline int radiusSmall() { return dp(10); }

enum class TextRole { Title, Subtitle, Body, Label, Caption };
void applyFont(QWidget *widget, TextRole role, bool bold = false);
int fontPixelSize(TextRole role);

// Style sheet of the phone chrome (bars, sheets, hub). Scoped by object names
// and dynamic properties so Krita's own widgets are never affected.
QString chromeStyleSheet();

// Style sheet for containers that host Krita's own panels inside sheets. It
// only enlarges touch targets (scroll bars, check boxes, combo boxes, buttons).
QString panelStyleSheet();
// Look of Krita's dialogs while the phone interface is active.
QString dialogStyleSheet();

// Icons. Plain names are the fork's own SVG icons (":/mobileui/icons/<name>.svg",
// tinted with the given color); names starting with "krita:" load Krita's
// themed icons.
QIcon icon(const QString &name);
QIcon icon(const QString &name, const QColor &color);

// Removes keyboard accelerator markers ("&") from action texts.
QString stripMnemonic(QString text);

// Finger-friendly kinetic scrolling for a scroll area.
void enableKineticScrolling(QAbstractScrollArea *area);

} // namespace mobileui

#endif
