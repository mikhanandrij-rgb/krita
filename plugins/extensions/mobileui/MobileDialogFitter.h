/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Krita Mobile (unofficial fork): makes Krita's desktop dialogs usable on a
// phone screen without rewriting them. When a dialog is about to be shown and
// does not fit the screen, its contents are moved into a scroll area (the
// dialog buttons stay visible at the bottom) and the dialog is sized to the
// screen. Dialogs that fit are only kept on screen. Nothing inside the
// dialogs changes, so every option stays available.
#ifndef MOBILE_DIALOG_FITTER_H
#define MOBILE_DIALOG_FITTER_H

#include <QObject>
#include <QBoxLayout>
#include <QList>
#include <QPointer>
#include <QSet>

class QDialog;
class QRect;
class QWidget;

namespace mobileui {

class DialogFitter final : public QObject
{
    Q_OBJECT
public:
    explicit DialogFitter(QObject *parent = nullptr);

    // Fits one dialog; also used by the screenshot run.
    void fit(QDialog *dialog);
    // Undoes the changes to long-lived widgets (brush settings) when the
    // phone interface is switched off. Dialogs are recreated by Krita.
    void restoreWidgets();

    // Phone-friendly controls inside a dialog or a sheet: long check box,
    // radio button and label texts wrap instead of running off the screen,
    // combo boxes shrink to the screen width, lists wrap their items. Every
    // widget shown later inside a "phone root" (a fitted dialog, the sheet)
    // is adapted too. restoreControls() undoes it for widgets that outlive
    // the phone interface (Krita's dockers).
    static void markPhoneRoot(QWidget *root);
    void adaptControls(QWidget *root);
    void restoreControls(QWidget *root);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Moves the contents of a widget's layout (box, grid or form) into a
    // scroll area inside the same widget; a trailing button row can stay
    // outside of it.
    bool wrapContents(QWidget *host, bool keepButtons);
    // Page dialogs (Configure Krita, document information) get tabs instead
    // of a side list and scrollable pages.
    bool adaptPageDialog(QDialog *dialog, const QRect &screen);
    // The new-document dialog gets its section list above the pages.
    bool adaptOpenPane(QDialog *dialog, const QRect &screen);

    void stackSideLists(QWidget *root);
    void adaptControl(QWidget *widget);
    void rewrapButton(QWidget *button);
    bool m_rewrapping = false;

    struct StackedView {
        QPointer<QWidget> view;
        int minimumWidth = 0;
        int minimumHeight = 0;
        int maximumHeight = 0;
    };
    struct StackedLayout {
        QPointer<QBoxLayout> layout;
        QList<StackedView> views;
    };
    QList<StackedLayout> m_stackedLayouts;
    QSet<QDialog *> m_wrapped;
    QSet<QDialog *> m_adapted;
    QSet<QDialog *> m_fullScreen;
};

} // namespace mobileui

#endif
