/*
 * SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MobileDialogFitter.h"
#include "MobileTheme.h"

#include <KisKineticScroller.h>

#include <QApplication>
#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>

namespace mobileui {

namespace {

QRect availableGeometry(const QWidget *widget)
{
    QScreen *screen = widget && widget->screen() ? widget->screen() : QGuiApplication::primaryScreen();
    QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 400, 800);
    // The main window covers the usable screen on a phone; following it also
    // makes the phone-sized test window behave like a phone on the desktop.
    const QWidget *top = widget && widget->parentWidget() ? widget->parentWidget()->window() : nullptr;
    if (!top || !top->isVisible()) {
        top = QApplication::activeWindow();
    }
    if (top && top != widget && top->isVisible()) {
        const QRect window = top->frameGeometry().intersected(area);
        if (window.width() > 200 && window.height() > 200) {
            area = window;
        }
    }
    return area;
}

bool isButtonRow(QLayoutItem *item)
{
    if (QWidget *w = item->widget()) {
        return qobject_cast<QDialogButtonBox *>(w) != nullptr;
    }
    // KoDialog puts its buttons into a QHBoxLayout of push buttons.
    if (QLayout *l = item->layout()) {
        bool onlyButtons = l->count() > 0;
        for (int i = 0; i < l->count(); ++i) {
            QLayoutItem *child = l->itemAt(i);
            if (child->spacerItem()) {
                continue;
            }
            QWidget *w = child->widget();
            if (!w || !(w->inherits("QPushButton") || qobject_cast<QDialogButtonBox *>(w))) {
                onlyButtons = false;
                break;
            }
        }
        return onlyButtons;
    }
    return false;
}

// Widgets of a layout moved into another widget's layout keep their old
// parent; move them along, keeping their visibility.
void reparentLayoutWidgets(QLayout *layout, QWidget *parent)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (QWidget *w = item->widget()) {
            if (w->parentWidget() != parent) {
                const bool hidden = w->isHidden();
                w->setParent(parent);
                w->setHidden(hidden);
            }
        } else if (QLayout *l = item->layout()) {
            reparentLayoutWidgets(l, parent);
        }
    }
}

} // namespace

DialogFitter::DialogFitter(QObject *parent)
    : QObject(parent)
{
}

bool DialogFitter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show) {
        QDialog *dialog = qobject_cast<QDialog *>(watched);
        // Message boxes and native file dialogs size themselves well.
        if (dialog && dialog->isWindow() && !qobject_cast<QMessageBox *>(dialog) && !qobject_cast<QFileDialog *>(dialog)) {
            fit(dialog);
        }
    }
    return QObject::eventFilter(watched, event);
}

void DialogFitter::fit(QDialog *dialog)
{
    const QRect screen = availableGeometry(dialog);
    const QSize needed = dialog->minimumSizeHint().expandedTo(dialog->minimumSize());
    if (!m_wrapped.contains(dialog) && (needed.width() > screen.width() || needed.height() > screen.height())) {
        if (wrapInScrollArea(dialog)) {
            m_wrapped.insert(dialog);
        }
    }
    // Phones: dialogs that are close to the screen size use all of it, the
    // rest are kept on screen and centered.
    QSize size = dialog->size().expandedTo(dialog->minimumSizeHint());
    if (m_wrapped.contains(dialog) || size.width() > screen.width() * 0.85 || size.height() > screen.height() * 0.85) {
        dialog->setMinimumSize(0, 0);
        dialog->setGeometry(screen);
    } else {
        size = size.boundedTo(screen.size());
        dialog->resize(size);
        dialog->move(screen.x() + (screen.width() - size.width()) / 2, screen.y() + (screen.height() - size.height()) / 2);
    }
}

bool DialogFitter::wrapInScrollArea(QDialog *dialog)
{
    // Only box layouts can be moved item by item without losing structure;
    // dialogs with other top-level layouts are just sized to the screen.
    QBoxLayout *outer = qobject_cast<QBoxLayout *>(dialog->layout());
    if (!outer || outer->direction() != QBoxLayout::TopToBottom || dialog->findChild<QScrollArea *>(QStringLiteral("mobileDialogScroll"), Qt::FindDirectChildrenOnly)) {
        return false;
    }

    QWidget *content = new QWidget;
    content->setObjectName(QStringLiteral("mobileDialogContent"));
    QVBoxLayout *inner = new QVBoxLayout(content);
    inner->setContentsMargins(outer->contentsMargins());
    inner->setSpacing(outer->spacing());

    // Move everything except the trailing button row into the scroll area.
    QList<QLayoutItem *> keep;
    QList<QLayoutItem *> move;
    while (outer->count() > 0) {
        QLayoutItem *item = outer->takeAt(0);
        move.append(item);
    }
    while (!move.isEmpty() && (isButtonRow(move.last()) || move.last()->spacerItem())) {
        keep.prepend(move.takeLast());
    }
    for (QLayoutItem *item : move) {
        if (QWidget *w = item->widget()) {
            const bool hidden = w->isHidden();
            delete item;
            inner->addWidget(w);
            w->setHidden(hidden);
        } else if (QLayout *l = item->layout()) {
            l->setParent(nullptr);
            inner->addLayout(l);
            reparentLayoutWidgets(l, content);
        } else {
            inner->addItem(item);
        }
    }

    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("mobileDialogScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    KisKineticScroller::createPreconfiguredScroller(scroll);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll, 1);
    for (QLayoutItem *item : keep) {
        if (QWidget *w = item->widget()) {
            const bool hidden = w->isHidden();
            delete item;
            outer->addWidget(w);
            w->setHidden(hidden);
        } else if (QLayout *l = item->layout()) {
            l->setParent(nullptr);
            outer->addLayout(l);
        } else {
            outer->addItem(item);
        }
    }
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        m_wrapped.remove(dialog);
    });
    return true;
}

} // namespace mobileui
