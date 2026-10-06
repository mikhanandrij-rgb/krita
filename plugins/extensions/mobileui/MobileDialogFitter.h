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
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>

#include <functional>

class QDialog;
class QGridLayout;
class QMenu;
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
    // A long-lived part of the window (the sheet, Krita's welcome page)
    // that should follow the phone rules: adapted controls, reflowed rows.
    void adaptPanel(QWidget *root);

    // Turns rows that are too wide for `width` into columns (box, grid and
    // form layouts, splitters, button boxes) and lifts desktop minimum widths,
    // so nothing scrolls sideways. Undone by restoreWidgets().
    int reflow(QWidget *root, int width, QWidget *budgetRoot = nullptr);
    // Popups (Krita's popup buttons: gradients, patterns, workspaces...) get
    // the phone look and are kept on screen.
    void fitPopup(QWidget *popup);
    void styleMenu(QMenu *menu);

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
    int reflowStructure(QWidget *root, int width, QWidget *budgetRoot);
    int reflowControls(QWidget *root, int width, QWidget *budgetRoot);
    void gridToColumn(QGridLayout *grid, int transposeWidth = -1);
    void fillColumn(QWidget *w);
    void fillColumnItems(QLayout *layout);
    void setMinimumWidthUndoable(QWidget *w, int minimum, int maximum);
    void scheduleReflow(QWidget *shown, QWidget *root);
    QList<std::function<void()>> m_undo;
    QHash<QWidget *, QPair<QPointer<QWidget>, QPointer<QWidget>>> m_pendingReflow;
    QSet<QWidget *> m_popups;
    QSet<QByteArray> m_loggedTooWide;
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
