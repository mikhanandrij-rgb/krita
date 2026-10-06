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
#include <QPointer>
#include <QSet>

class QDialog;
class QWidget;

namespace mobileui {

class DialogFitter final : public QObject
{
    Q_OBJECT
public:
    explicit DialogFitter(QObject *parent = nullptr);

    // Fits one dialog; also used by the screenshot run.
    void fit(QDialog *dialog);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool wrapInScrollArea(QDialog *dialog);
    QSet<QDialog *> m_wrapped;
};

} // namespace mobileui

#endif
